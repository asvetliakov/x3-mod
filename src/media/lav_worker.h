#pragma once
#include "playback_runtime.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace x3m::media {
namespace lav_detail {
static_assert(std::atomic<unsigned>::is_always_lock_free,
              "transport publication requires lock-free 32-bit atomics");
// Exactly one writer and one reader. Each exclusively owns one cell; exchange
// transfers the third. No reader/writer access to the same non-atomic payload.
// The reader keeps its acquired cell until its next exchange. No retries/spins.
template<class T> class Latest {
public:
    void publish(const T& value) noexcept {
        cells_[back_] = value;
        back_ = middle_.exchange(back_ | dirty, std::memory_order_acq_rel) & index_mask;
    }
    bool observe(T& value) noexcept {
        if (!(middle_.load(std::memory_order_acquire) & dirty)) return false;
        front_ = middle_.exchange(front_, std::memory_order_acq_rel) & index_mask;
        value = cells_[front_];
        return true;
    }
private:
    static constexpr unsigned dirty=4,index_mask=3;
    T cells_[3]{};
    std::atomic<unsigned> middle_{1};
    unsigned back_=2,front_=0;
};
// SPSC transfer: a rejected push has no effect, and pop first copies the entire
// payload before returning the cell to its producer.
template<class T> class Mailbox {
public:
    bool push(const T& value) noexcept {
        if (ready_.load(std::memory_order_acquire)) return false;
        value_=value;ready_.store(true,std::memory_order_release);return true;
    }
    bool pop(T& value) noexcept {
        if (!ready_.load(std::memory_order_acquire)) return false;
        value=value_;ready_.store(false,std::memory_order_release);return true;
    }
    bool occupied() const noexcept {return ready_.load(std::memory_order_acquire);}
private:
    T value_{};std::atomic<bool> ready_{false};
};
} // namespace lav_detail

struct FrameIdentity {
    SessionHandle session{};
    OperationId operation=0;
    Epoch epoch=0;
};
inline bool operator==(const FrameIdentity& a,const FrameIdentity& b) noexcept {
    return a.session==b.session&&a.operation==b.operation&&a.epoch==b.epoch;
}
inline FrameIdentity identity(const Command& c) noexcept {return {c.session,c.operation,c.epoch};}
inline FrameIdentity identity(const Publication& p) noexcept {return {p.session,p.operation,p.epoch};}

enum class WorkerState : unsigned {idle,starting,ready,failed,retired,unsafe_retained};
namespace lav_detail {
inline bool admit_ready(WorkerState state,bool shutdown,const Publication& desired,const FrameIdentity& frame) noexcept {
    return !shutdown&&state==WorkerState::ready&&desired.live&&desired.playing&&identity(desired)==frame;
}
} // namespace lav_detail
enum class ObservationKind : unsigned {
    owner,call,failure,operation,command,desired,slot,lease,notify,clock,cooperative,
    service,dd_identity,session,surface,seek_position,retirement,facts,source_error,
    event_batch,provider_event,drain,deadline_anchor,source_copy,window,
    session_cleanup,cleanup,eof,identity
};
// Scalar synchronous verification observations. Strings are static operation
// names, never ownership predicates. Observer return values cannot change safety.
struct Observation {
    ObservationKind kind{};
    const char* label="";
    FrameIdentity identity{};
    unsigned graph=0;
    std::int64_t begin=0,end=0;
    std::int64_t a=0,b=0,c=0,d=0,e=0,f=0;
    std::int32_t status=0;
};
enum class MetadataKind : unsigned {pin,type,context,created_class,module,dither,
                                    pixel_format,sink,rgb,allocator,decommit,seek_caps,cohort};
struct Metadata {
    MetadataKind kind{};
    FrameIdentity identity{};
    unsigned graph=0;
    const wchar_t* text=nullptr;
    const wchar_t* expected=nullptr;
    const char* role=nullptr;
    std::array<unsigned char,16> guid[3]{};
    std::int64_t a=0,b=0,c=0,d=0,e=0,f=0,g=0;
};
class WorkerObserver {
public:
    virtual ~WorkerObserver()=default;
    virtual void observe(const Observation&) noexcept=0;
    virtual void metadata(const Metadata&) noexcept=0;
};
// Test-only finite pump bound. Zero is production/unlimited. It never changes
// end_ms, reports EOF, advances a clock, or overrides a retirement predicate.
class VerificationPump {
public:
    virtual ~VerificationPump()=default;
    virtual unsigned picture_budget(const Command&) const noexcept=0;
};
struct SourcePath {SourceKey key=0;std::wstring path;};
struct PackageConfig;
struct WorkerConfig {
    unsigned instance=0;
    std::wstring provider_manifest;
    std::vector<SourcePath> sources; // owned immutable paths; no resolver callback
    std::shared_ptr<const PackageConfig> package_owner; // identity pins outlive detached/unsafe worker
    std::shared_ptr<WorkerObserver> observer;
    std::shared_ptr<const VerificationPump> verification_pump;
};
struct FrameView {
    FrameIdentity identity{};
    unsigned graph=0,slot=0;
    std::uint64_t sequence=0;
    std::int64_t start=0,end=0;
    unsigned width=512,height=512,pitch=2048;
    const unsigned char* bgra=nullptr;
};
enum class LeaseRelease : unsigned {discard,clock_consumed,revoked,selected_uploaded};
namespace lav_detail {struct FrameStorage;}
class FrameLease {
public:
    FrameLease() noexcept=default;
    ~FrameLease();
    FrameLease(FrameLease&&) noexcept;
    FrameLease& operator=(FrameLease&&) noexcept;
    FrameLease(const FrameLease&)=delete;
    FrameLease& operator=(const FrameLease&)=delete;
    explicit operator bool() const noexcept {return bool(storage_);}
    const FrameView& view() const noexcept {return view_;}
    void release(LeaseRelease=LeaseRelease::discard) noexcept;
private:
    friend class LavWorker;
    friend struct lav_detail::FrameStorage;
    std::shared_ptr<lav_detail::FrameStorage> storage_;
    FrameView view_{};
};
namespace lav_detail {
constexpr unsigned frame_bytes=512*512*4,slot_count=3;
enum SlotState : unsigned {free_slot,writing_slot,ready_slot,reading_slot};
struct FrameStorage {
    struct Slot {std::atomic<unsigned> state{free_slot};FrameView view{};std::array<unsigned char,frame_bytes> pixels{};};
    Slot slots[slot_count];
    std::shared_ptr<WorkerObserver> observer;
    explicit FrameStorage(std::shared_ptr<WorkerObserver> o):observer(std::move(o)) {
        for(unsigned i=0;i<slot_count;++i){slots[i].view.slot=i;slots[i].view.bgra=slots[i].pixels.data();}
    }
    void emit(unsigned index,const char* label,bool interval=false) const noexcept {
        if(!observer)return;
        const auto& v=slots[index].view;Observation o{};o.kind=ObservationKind::slot;o.label=label;o.identity=v.identity;o.graph=v.graph;
        o.a=index;o.b=static_cast<std::int64_t>(v.identity.epoch);o.c=static_cast<std::int64_t>(v.sequence);
        o.d=interval?v.start:v.graph;o.e=interval?v.end:0;o.f=interval?frame_bytes:0;observer->observe(o);
    }
    bool reserve(unsigned index,FrameIdentity id,unsigned graph,std::uint64_t sequence) noexcept {
        auto& slot=slots[index];if(slot.state.load(std::memory_order_acquire)!=free_slot)return false;
        slot.view.identity=id;slot.view.graph=graph;slot.view.sequence=sequence;
        slot.state.store(writing_slot,std::memory_order_relaxed);emit(index,"writing");return true;
    }
    void publish(unsigned index) noexcept {emit(index,"ready",true);slots[index].state.store(ready_slot,std::memory_order_release);}
    // Caller must have proved actual public sample retirement first.
    void abandon_retired(unsigned index) noexcept {emit(index,"abandon");slots[index].state.store(free_slot,std::memory_order_release);}
    FrameLease acquire(const std::shared_ptr<FrameStorage>& self,unsigned index) noexcept {
        FrameLease out;auto& slot=slots[index];if(slot.state.load(std::memory_order_acquire)!=ready_slot)return out;
        slot.state.store(reading_slot,std::memory_order_relaxed);emit(index,"reading");out.storage_=self;out.view_=slot.view;return out;
    }
    bool all_free() const noexcept {
        for(const auto& slot:slots)if(slot.state.load(std::memory_order_acquire)!=free_slot)return false;
        return true;
    }
};
} // namespace lav_detail
inline FrameLease::~FrameLease(){release();}
inline FrameLease::FrameLease(FrameLease&& other) noexcept:storage_(std::move(other.storage_)),view_(other.view_){}
inline FrameLease& FrameLease::operator=(FrameLease&& other) noexcept {
    if(this!=&other){release();storage_=std::move(other.storage_);view_=other.view_;}return *this;
}
inline void FrameLease::release(LeaseRelease reason) noexcept {
    if(!storage_)return;
    if(storage_->observer){
        static constexpr const char* names[]={"discard","clock_consumed","revoked","selected_uploaded"};
        Observation o{};o.kind=ObservationKind::lease;o.label=names[unsigned(reason)];o.identity=view_.identity;o.graph=view_.graph;
        o.a=view_.slot;o.b=static_cast<std::int64_t>(view_.identity.epoch);o.c=static_cast<std::int64_t>(view_.sequence);o.d=view_.graph;storage_->observer->observe(o);
    }
    storage_->emit(view_.slot,"free");storage_->slots[view_.slot].state.store(lav_detail::free_slot,std::memory_order_release);storage_.reset();view_={};
}

enum WorkerEventFlags : unsigned {
    worker_prepared=1,worker_source_eof=2,worker_failed=4,
    worker_graph_retired=8,worker_service_retired=16,worker_unsafe_retained=32
};
struct WorkerEvent {
    FrameIdentity identity{};
    unsigned graph=0,flags=0,revision=0;
    std::int32_t status=0;
    bool graph_guard_safe=false,graph_released=false,events_clean=false;
};
namespace lav_detail {
// One worker writes cumulative terminal facts; one main owner polls/acks. A
// final result cannot be replaced by another identity until main observed it.
// Intermediate snapshots may coalesce, but failure/EOF/retirement flags persist.
class TerminalPublication {
public:
    bool ready_for_next() const noexcept {return value_.flags==0||(final_&&ack_.load(std::memory_order_acquire)==revision_);}
    bool reset(FrameIdentity id,unsigned graph) noexcept {
        if(!ready_for_next())return false;
        value_={};value_.identity=id;value_.graph=graph;final_=false;return true;
    }
    bool update(WorkerEvent next,bool final) noexcept {
        if(!(next.identity==value_.identity)||revision_==UINT32_MAX)return false;
        next.flags|=value_.flags;if(value_.status<0)next.status=value_.status;
        next.revision=++revision_;value_=next;final_=final_||final;latest_.publish(value_);return true;
    }
    bool failure(WorkerEvent next) noexcept {
        next.flags|=worker_failed;
        // No graph exists to retire. Acknowledge this final failure and permit
        // a subsequent valid command rather than waiting for a nonexistent guard.
        if(next.graph_released)next.flags|=worker_graph_retired;
        return update(next,next.graph_released);
    }
    bool poll(WorkerEvent& out) noexcept {
        WorkerEvent next;if(!latest_.observe(next)||next.revision==observed_)return false;
        observed_=next.revision;out=next;ack_.store(next.revision,std::memory_order_release);return true;
    }
private:
    Latest<WorkerEvent> latest_;
    std::atomic<unsigned> ack_{0};
    WorkerEvent value_{};unsigned revision_=0,observed_=0;bool final_=false;
};
} // namespace lav_detail
namespace lav_detail {
// One worker publishes a fact only after consuming cancellation and settling
// every ownership domain. Main compares its own publication serial, so an old
// fact cannot authorize reuse after even an identical tuple was republished.
class AssignmentQuiescence {
public:
    bool publish(const Publication& canceled,std::uint64_t serial,bool service_usable,
                 bool graph_absent,bool command_absent,bool ordinary_absent,
                 bool terminal_acknowledged,bool slots_free) noexcept {
        if(!serial||canceled.live||!service_usable||!graph_absent||!command_absent||
           !ordinary_absent||!terminal_acknowledged||!slots_free||serial==published_)return false;
        latest_.publish({canceled,serial});published_=serial;return true;
    }
    bool poll(const Publication& expected,const Publication& current,std::uint64_t serial) noexcept {
        latest_.observe(observed_);
        return serial&&observed_.serial==serial&&!expected.live&&!current.live&&
            expected.playing==current.playing&&observed_.canceled.playing==current.playing&&
            identity(expected)==identity(current)&&identity(observed_.canceled)==identity(current);
    }
private:
    struct Fact {Publication canceled{};std::uint64_t serial=0;};
    Latest<Fact> latest_;Fact observed_{};std::uint64_t published_=0;
};
} // namespace lav_detail
struct WorkerPoll {
    WorkerState state=WorkerState::idle;
    std::int32_t error=0;
    unsigned call_tick=0,progress_tick=0;
};

// One main owner per service; one private STA worker. Main methods do no COM,
// wait, join or source processing. A FrameLease keeps its bytes alive separately
// from the service handle. Observer/config lifetime is owned, not borrowed.
class LavWorker {
public:
    LavWorker() noexcept=default;
    ~LavWorker();
    LavWorker(LavWorker&&) noexcept;
    LavWorker& operator=(LavWorker&&) noexcept;
    LavWorker(const LavWorker&)=delete;
    LavWorker& operator=(const LavWorker&)=delete;
    bool start_service(WorkerConfig);
    bool try_submit(const Command&) noexcept;
    void publish_desired(const Publication&) noexcept;
    FrameLease try_acquire_frame() noexcept;
    bool poll_event(WorkerEvent&) noexcept;
    // Poll events and discard/release revoked FrameLeases while waiting. True
    // means this exact canceled assignment has no graph, queued command/event,
    // unacknowledged terminal result, or non-FREE slot. It is not service shutdown.
    // Single main owner must publish new desired + submit without reentrancy.
    bool poll_assignment_quiescent(const Publication& canceled) noexcept;
    WorkerPoll poll_service_state() const noexcept;
    void request_shutdown() noexcept;
    bool poll_retired() const noexcept;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace x3m::media
