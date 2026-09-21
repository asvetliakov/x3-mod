#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace x3m::ownership::clone_upload {

// This core owns CPU bytes only. Every operation requires the ownership
// registry mutex. The adapter must authenticate public COM metadata first,
// release its qualifier references, then construct the live CPU guard under
// that mutex. No callback is permitted between the guard and stage()/finish().
// It is NOT an API for observing an arbitrary mapped or draw-time resource.
constexpr std::size_t arena_bytes = 2u * 1024u * 1024u;
constexpr std::size_t pair_count = 2;
constexpr std::size_t pair_capacity = arena_bytes / pair_count;
enum class Buffer : unsigned { Vertex = 0, Index = 1 };
enum class Refusal : unsigned {
    None, Selector, Busy, Exhausted, Device, Thread, Allocation, Contract,
    Mapping, Source, Reentry, Unlock, Clone, Linkage, Mutation, Unwind
};
struct Shape { std::uint32_t vertices, faces, stride; };
constexpr Shape shapes[pair_count] = {{9680,3784,40},{1267,940,40}};
constexpr std::uint32_t bytes_for(unsigned slot, Buffer kind) noexcept {
    return kind == Buffer::Vertex ? shapes[slot].vertices * shapes[slot].stride
                                 : shapes[slot].faces * 6u;
}
struct Identity {
    std::uint64_t allocation = 0, revision = 0;
    friend bool operator==(Identity a, Identity b) noexcept {
        return a.allocation == b.allocation && a.revision == b.revision;
    }
};
struct DeviceGuard {
    std::uint64_t owner = 0, generation = 0;
    std::uint32_t thread = 0;
    bool resetting = false, lost = false, retiring = false, tracking = false;
};
struct Creation {
    Identity identity;
    std::uint32_t bytes = 0, actual_usage = 0, requested_usage = 0;
    // Set only from the ACTUAL native descriptor; never application GetDesc.
    bool managed = false, index16 = false, own_dispatch = false;
};
struct MapGuard {
    DeviceGuard device;
    Identity identity;
    const void* pointer = nullptr;
    std::uint64_t lock_serial = 0;
    std::uint32_t pending = 0, in_flight_locks = 0, in_flight_unlocks = 0;
    bool own_dispatch = false, authenticated = false, ambiguous = true;
};
struct FinalGuard {
    DeviceGuard device;
    Identity buffers[2];
    bool authenticated = false, quiet = false, own_dispatch = false;
};
struct Statistics {
    std::uint64_t invocations = 0, staged_bytes = 0, wiped_bytes = 0;
    std::uint64_t publications = 0, refusals = 0;
};
struct Record {
    std::uint64_t invocation = 0, owner = 0, generation = 0;
    Identity buffers[2];
    std::uint32_t bytes[2]{};
    bool producer_payload_valid = false;
};

class Store final {
    struct Part {
        Creation creation;
        Identity written;
        const void* mapping = nullptr;
        std::uint64_t lock_serial = 0;
        bool created = false, mapped = false, staged = false, closed = false;
        bool source_lock = false, source_closed = false;
    };
    struct Pair {
        Record record;
        std::size_t used = 0;
    };
    // No allocation in any scope or map callback. Construct once before arming.
    std::array<unsigned char, arena_bytes> arena_{};
    Pair pairs_[pair_count]{};
    bool duplicate_[pair_count]{};
    Part parts_[2]{};
    Identity sources_[2]{};
    DeviceGuard beginning_{};
    Statistics stats_{};
    std::uint64_t serial_ = 0, active_ = 0;
    unsigned slot_ = 0;
    Refusal reason_ = Refusal::None;
    static unsigned index(Buffer kind) noexcept { return static_cast<unsigned>(kind); }
    bool device(const DeviceGuard& g) const noexcept {
        return g.owner && g.generation && g.owner == beginning_.owner &&
            g.generation == beginning_.generation && g.tracking &&
            !g.resetting && !g.lost && !g.retiring;
    }
    bool current(std::uint64_t invocation, const DeviceGuard& g) noexcept {
        if (!active_ || invocation != active_ || reason_ != Refusal::None) return false;
        if (!device(g)) { refuse(Refusal::Device); return false; }
        if (!g.thread || g.thread != beginning_.thread) { refuse(Refusal::Thread); return false; }
        return true;
    }
    std::size_t offset(unsigned slot, Buffer kind) const noexcept {
        return slot * pair_capacity + (kind == Buffer::Index ? bytes_for(slot,Buffer::Vertex) : 0);
    }
    void wipe(unsigned slot) noexcept {
        // Volatile stores are intentional: failed speculative bytes must be
        // erased even if no successful consumer will subsequently read them.
        volatile unsigned char* p = arena_.data() + slot * pair_capacity;
        const auto n = pairs_[slot].used;
        for (std::size_t i=0;i<n;++i) p[i]=0;
        stats_.wiped_bytes += n;
        pairs_[slot] = Pair{};
    }
public:
    Store() noexcept = default;
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    ~Store() { for(unsigned s=0;s<pair_count;++s) wipe(s); }
    std::uint64_t begin(unsigned slot, const DeviceGuard& g,
                        Identity vertex_source, Identity index_source) noexcept {
        if (active_) { refuse(Refusal::Busy); return 0; }
        reason_=Refusal::None;
        if(slot>=pair_count) { reason_=Refusal::Selector; ++stats_.refusals; return 0; }
        // An already retained signature is ambiguous, not a first-object wins
        // policy. A caller must explicitly disarm/clear before trying again.
        if(pairs_[slot].record.producer_payload_valid || duplicate_[slot]) {
            wipe(slot);duplicate_[slot]=true;
            reason_=Refusal::Busy; ++stats_.refusals; return 0;
        }
        if(serial_==std::numeric_limits<std::uint64_t>::max()) {
            reason_=Refusal::Exhausted; ++stats_.refusals; return 0;
        }
        wipe(slot);slot_=slot;beginning_=g;parts_[0]=Part{};parts_[1]=Part{};
        sources_[0]=vertex_source;sources_[1]=index_source;
        active_=++serial_;++stats_.invocations;
        if(!current(active_,g)) { active_=0; return 0; }
        if(!vertex_source.allocation || !index_source.allocation ||
           vertex_source.allocation==index_source.allocation) {
            refuse(Refusal::Source);active_=0;return 0;
        }
        return active_;
    }
    void refuse(Refusal reason) noexcept {
        if(!active_ || reason_!=Refusal::None) return;
        reason_=reason; ++stats_.refusals;wipe(slot_);
        for(auto& p:parts_) p.mapping=nullptr;
    }
    bool created(std::uint64_t invocation, Buffer kind, const DeviceGuard& g,
                 const Creation& c) noexcept {
        if(!current(invocation,g))return false;
        auto& p=parts_[index(kind)];
        const auto id=c.identity.allocation;
        if(p.created || !id || c.identity.revision ||
           id==sources_[0].allocation || id==sources_[1].allocation ||
           (parts_[1-index(kind)].created && id==parts_[1-index(kind)].creation.identity.allocation)) {
            refuse(Refusal::Allocation);return false;
        }
        // D3DUSAGE_WRITEONLY=8, DYNAMIC=0x200. Other requested Usage bits
        // remain metadata; clearing them is not this core's creation policy.
        if(!c.managed || (c.actual_usage & (8u|0x200u)) || !c.own_dispatch ||
           c.bytes!=bytes_for(slot_,kind) || (kind==Buffer::Index && !c.index16)) {
            refuse(Refusal::Contract);return false;
        }
        p.creation=c;p.created=true;return true;
    }
    // Feed EVERY observed source lock, including failures. A native source
    // bypass cannot set source_lock and therefore cannot authorize staging.
    bool source_lock(std::uint64_t invocation, Buffer kind, const DeviceGuard& g,
                     Identity source, std::uint32_t offset, std::uint32_t size,
                     std::uint32_t flags, bool success) noexcept {
        if(!current(invocation,g))return false;
        auto& p=parts_[index(kind)];
        if(!success || p.source_lock || !(source==sources_[index(kind)]) ||
           offset || size || flags!=0x810u) {
            refuse(Refusal::Source);return false;
        }
        p.source_lock=true;return true;
    }
    bool source_unlock(std::uint64_t invocation, Buffer kind, const DeviceGuard& g,
                       Identity source, bool success) noexcept {
        if(!current(invocation,g))return false;
        auto& p=parts_[index(kind)];
        if(!success || !p.source_lock || p.source_closed || !(source==sources_[index(kind)])) {
            refuse(Refusal::Source);return false;
        }
        p.source_closed=true;return true;
    }
    bool mapped(std::uint64_t invocation, Buffer kind, const DeviceGuard& g,
                Identity id, const void* pointer, std::uint64_t lock_serial,
                std::uint32_t offset, std::uint32_t size, std::uint32_t flags,
                bool success) noexcept {
        if(!current(invocation,g))return false;
        auto& p=parts_[index(kind)];
        if(!success || !p.created || p.mapped || !pointer || !lock_serial ||
           id.allocation!=p.creation.identity.allocation || id.revision!=1 ||
           offset || size || flags!=0x800u) {
            refuse(Refusal::Mapping);return false;
        }
        p.written=id;p.mapping=pointer;p.lock_serial=lock_serial;p.mapped=true;return true;
    }
    bool stage(std::uint64_t invocation, Buffer kind, const MapGuard& g) noexcept {
        if(!current(invocation,g.device))return false;
        auto& p=parts_[index(kind)];
        if(!p.source_lock) { refuse(Refusal::Source);return false; }
        if(!p.mapped || p.staged || p.closed || !g.authenticated || !g.own_dispatch ||
           g.ambiguous || !(g.identity==p.written) || g.pointer!=p.mapping ||
           g.lock_serial!=p.lock_serial || g.pending!=1 || g.in_flight_locks ||
           g.in_flight_unlocks!=1) {
            refuse(Refusal::Mapping);return false;
        }
        // This is raw access only. Successful source locks do not prove fill:
        // D3DX may encounter a private IB allocation failure before first store.
        const auto n=p.creation.bytes;
        const auto target=offset(slot_,kind);
        const auto used=target-slot_*pair_capacity+n;
        if(used>pair_capacity) { refuse(Refusal::Exhausted);return false; }
        // Record the full potential write range BEFORE copying, so abort/unwind
        // erases it even if a foreign exception interrupts the copy itself.
        if(used>pairs_[slot_].used)pairs_[slot_].used=used;
        std::atomic_thread_fence(std::memory_order_seq_cst);
        std::memcpy(arena_.data()+target,p.mapping,n);
        stats_.staged_bytes+=n;p.staged=true;return true;
    }
    bool unlocked(std::uint64_t invocation, Buffer kind, const DeviceGuard& g,
                  Identity id, bool exact_s_ok, bool quiet) noexcept {
        if(!current(invocation,g))return false;
        auto& p=parts_[index(kind)];p.mapping=nullptr;
        if(!exact_s_ok || !quiet || !p.staged || p.closed || !(id==p.written)) {
            refuse(Refusal::Unlock);return false;
        }
        p.closed=true;return true;
    }
    bool finish(std::uint64_t invocation, bool clone_succeeded, const FinalGuard& g) noexcept {
        if(invocation!=active_ || !active_)return false;
        bool accepted=current(invocation,g.device);
        if(accepted && !clone_succeeded) { refuse(Refusal::Clone);accepted=false; }
        if(accepted && (!g.authenticated || !g.quiet || !g.own_dispatch ||
           !parts_[0].closed || !parts_[1].closed ||
           !parts_[0].source_closed || !parts_[1].source_closed ||
           !(g.buffers[0]==parts_[0].written) || !(g.buffers[1]==parts_[1].written))) {
            refuse(Refusal::Linkage);accepted=false;
        }
        if(accepted) {
            auto& r=pairs_[slot_].record;
            r.invocation=active_;r.owner=beginning_.owner;r.generation=beginning_.generation;
            for(unsigned i=0;i<2;++i) { r.buffers[i]=parts_[i].written;r.bytes[i]=parts_[i].creation.bytes; }
            r.producer_payload_valid=true;++stats_.publications;
        } else wipe(slot_);
        for(auto& p:parts_)p.mapping=nullptr;
        active_=0;return accepted;
    }
    void abort(std::uint64_t invocation) noexcept {
        if(invocation && active_==invocation) {
            refuse(Refusal::Unwind);wipe(slot_);active_=0;
        }
    }
    void invalidate(std::uint64_t allocation) noexcept {
        for(unsigned s=0;s<pair_count;++s)
            if(pairs_[s].record.buffers[0].allocation==allocation ||
               pairs_[s].record.buffers[1].allocation==allocation)wipe(s);
        if(active_ && (parts_[0].creation.identity.allocation==allocation ||
                       parts_[1].creation.identity.allocation==allocation))refuse(Refusal::Mutation);
    }
    void reset() noexcept {
        if(active_)refuse(Refusal::Device);
        for(unsigned s=0;s<pair_count;++s){wipe(s);duplicate_[s]=false;}
    }
    Record record(unsigned slot) const noexcept {
        return slot<pair_count ? pairs_[slot].record : Record{};
    }
    bool copy_retained(unsigned slot, Buffer kind, const FinalGuard& g,
                       void* output, std::size_t capacity) const noexcept {
        if(slot>=pair_count || !output)return false;
        const auto& r=pairs_[slot].record;
        if(!r.producer_payload_valid || !g.authenticated || !g.quiet || !g.own_dispatch ||
           !g.device.tracking || g.device.resetting || g.device.lost || g.device.retiring ||
           g.device.owner!=r.owner || g.device.generation!=r.generation ||
           !(g.buffers[0]==r.buffers[0]) || !(g.buffers[1]==r.buffers[1]) ||
           capacity<r.bytes[index(kind)])return false;
        std::memcpy(output,arena_.data()+offset(slot,kind),r.bytes[index(kind)]);return true;
    }
    bool duplicate(unsigned slot) const noexcept { return slot<pair_count&&duplicate_[slot]; }
    // The caller holds one registry guard and validates disjoint output spans.
    // Both capacities and all identity facts are checked before either write.
    bool copy_pair(unsigned slot,const FinalGuard& g,void* vertex,std::size_t vertex_capacity,
                   void* indices,std::size_t index_capacity,Record& result) const noexcept {
        if(slot>=pair_count||!vertex||!indices)return false;
        const auto& r=pairs_[slot].record;
        if(!r.producer_payload_valid||!g.authenticated||!g.quiet||!g.own_dispatch||
           !g.device.tracking||g.device.resetting||g.device.lost||g.device.retiring||
           g.device.owner!=r.owner||g.device.generation!=r.generation||
           !(g.buffers[0]==r.buffers[0])||!(g.buffers[1]==r.buffers[1])||
           vertex_capacity<r.bytes[0]||index_capacity<r.bytes[1])return false;
        std::memcpy(vertex,arena_.data()+offset(slot,Buffer::Vertex),r.bytes[0]);
        std::memcpy(indices,arena_.data()+offset(slot,Buffer::Index),r.bytes[1]);
        result=r;return true;
    }
    Statistics statistics() const noexcept { return stats_; }
    Refusal refusal() const noexcept { return reason_; }
    std::uint64_t active() const noexcept { return active_; }
#ifdef X3M_LATTICE_UPLOAD_FIXTURE
    // Never expose speculative bytes, even to tests. This verifies erasure only
    // after abort/finish has removed the active scope and all valid records.
    bool discarded_arena_is_zero() const noexcept {
        if(active_ || pairs_[0].record.producer_payload_valid || pairs_[1].record.producer_payload_valid)return false;
        for(auto c:arena_)if(c)return false;
        return true;
    }
#endif
};
static_assert(sizeof(Store)<=arena_bytes+4096,"bounded CPU snapshot overhead");
} // namespace x3m::ownership::clone_upload
