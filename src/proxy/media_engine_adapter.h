#pragma once
#include "media_playback.h"
#include "media_engine_sites.h"
#include "media_presentation_gate.h"
#include <atomic>

namespace x3m::media_engine {
using media_playback::Adapter;
using media_playback::EngineKey;
using media::SessionHandle;
// Actual emitted PUSHAD/XMM frame. saved_esp is PUSHAD's pre-push ESP,
// eight bytes below the incoming stack (reserved target and saved EFLAGS).
struct Frame {
    unsigned char xmm[128];
    std::uint32_t edi,esi,ebp,saved_esp,ebx,edx,ecx,eax,eflags,target;
    std::uint32_t input_esp() const noexcept {return saved_esp+8;}
};
static_assert(sizeof(Frame)==168 && offsetof(Frame,edi)==128 && offsetof(Frame,target)==164);
struct Routes {
    std::uint32_t forward[site_count]{},unobserved[site_count]{};
    std::uint32_t after[return_count]{};
    std::uint32_t return_plain=0, manager_drop4=0,manager_drop8=0,manager_next8=0,manager_error8=0,speech_drop4=0,destroy_free=0;
};
// Engine memory only. Allocation MUST be the matching game family and account
// live/cumulative counters exactly; rollback decrements live counters only.
// No retry/engine eviction callback in an owned allocation attempt.
struct Memory {
    virtual ~Memory()=default;
    virtual bool read(std::uint32_t,void*,unsigned) noexcept=0;
    virtual bool write(std::uint32_t,const void*,unsigned) noexcept=0;
    virtual std::uint32_t allocate_shell() noexcept=0;
    virtual void release_unpublished_shell(std::uint32_t) noexcept=0;
};
struct PumpRequest {
    SessionHandle session{}; EngineKey record{};
    media::OperationId operation=0; media::Epoch epoch=0;
    media_playback::TraversalTicket traversal{};
};
struct BindingObservation {
    EngineKey record{};std::uint32_t slot=0,initial_flags=0,published_flags=0;
    bool published=false;
};
enum class PumpResult : std::uint8_t { pending, complete, failed, invalidated };
// One implementation combines canonical worker/clock/destination services. All
// methods except pump are bounded local, noexcept, allocation-free, cannot call
// the engine/COM, and cannot reenter. reserve binds an already prepared worker
// slot; false has no effect. publish drives offer/submit/ack and cancellation;
// a full worker queue leaves the canonical command pending. It never waits.
// pump may reenter through destination D3D; it retains its own FrameLease and
// must check CurrentCheck after final destination release before clock commit.
// No record address may be delivered to a worker. Complete/failed means an
// observed scheduling/provider result, never fake graph readiness.
struct Services {
    virtual ~Services()=default;
    virtual bool ready() const noexcept=0;
    virtual bool reserve(SessionHandle,media::SourceKey) noexcept=0;
    virtual bool observe_record(SessionHandle,const BindingObservation&) noexcept=0;
    virtual void cancel(SessionHandle) noexcept=0;
    virtual void publish(Adapter&,SessionHandle) noexcept=0;
    using CurrentCheck=bool(*)(void*,const PumpRequest&) noexcept;
    virtual PumpResult pump(const PumpRequest&,CurrentCheck,void*) noexcept=0;
    virtual bool position(SessionHandle,std::uint32_t&) noexcept=0;
    virtual media_playback::ClockTransaction clock(SessionHandle) noexcept=0;
};
// Explicit integration dependencies. These are evidence supplied by the single
// startup owner, not discovered/probed on a game hook. The 24-site group alone
// does not meet this contract. Default construction cannot enable admission.
struct Readiness {
    bool consumer_group=false, cue_composed=false, destination=false;
    bool speech_transaction=false, mixed_list_returns=false;
    bool callback_lifetime=false, exception_cleanup=false, owner_thread=false;
    bool complete() const noexcept {
        return consumer_group&&cue_composed&&destination&&speech_transaction&&
            mixed_list_returns&&callback_lifetime&&exception_cleanup&&owner_thread;
    }
};
struct ExecutionPoint {std::uint32_t thread=0,stack=0;};
using ExecutionProbe=ExecutionPoint(*)() noexcept;
struct OwnerDomain {std::uint32_t thread=0,low=0,high=0;};
// Append-only atomic tombstones. Historical address reuse can conservatively
// refuse FOREIGN unowned calls; the owner uses its live identity map. No key is
// ever evicted, so a miss cannot forget an admitted shell. Exhaustion permanently
// closes new admission before allocation; existing owner cleanup remains usable.
class OwnedKeys {
public:
    static constexpr unsigned capacity=256;
    bool room(std::uint32_t record) const noexcept; // owner writer only
    bool publish(std::uint32_t record,std::uint32_t shell) noexcept; // one owner writer
    bool record(std::uint32_t) const noexcept;
    bool shell(std::uint32_t) const noexcept;
private:
    static bool contains(const std::atomic<std::uint32_t>*,std::uint32_t) noexcept;
    static bool append(std::atomic<std::uint32_t>*,std::uint32_t) noexcept;
    std::atomic<std::uint32_t> records_[capacity]{},shells_[capacity]{};
};
// A shared record-memory domain, bound before admission. Callbacks invalidate
// only value watches under its short lock; never call Adapter/Services/COM.
struct RecordIngress {
    virtual ~RecordIngress()=default;
    virtual void retiring(EngineKey) noexcept=0;
    virtual void retiring_address(std::uint32_t) noexcept=0;
    virtual void clearing() noexcept=0;
    virtual void foreign_refusal() noexcept=0;
    virtual bool healthy() const noexcept=0;
};
class Consumer {
public:
    Consumer(Adapter& state,Memory& memory,Services& service,const Routes& routes) noexcept
      : state_(state),memory_(memory),service_(service),routes_(routes) {}
    // Serialized startup binding; immutable after first successful bind. The
    // probe is a process-lifetime, CPU-only public-platform function.
    bool bind_owner(OwnerDomain,ExecutionProbe) noexcept;
    bool bind_ingress(RecordIngress&) noexcept;
    bool enable(Readiness) noexcept;
    void disable() noexcept {enabled_.store(false,std::memory_order_release);}
    bool enabled() const noexcept {return enabled_.load(std::memory_order_acquire);}
    bool eligible(std::uint32_t source,std::uint32_t flags) const noexcept;
    void dispatch(SiteId,Frame&) noexcept;
    bool current(const PumpRequest&) const noexcept;
    unsigned shells() const noexcept; // UINT_MAX on unsupported domain
    // Owner-thread/stack only, value lookup; failure leaves both outputs intact.
    bool binding_owner(std::uint32_t,SessionHandle&,EngineKey&) noexcept;
    bool admission_closed() const noexcept {return closed_.load(std::memory_order_acquire);}
private:
    // Only identity/allocation ownership lives here. Playback, commands, clock,
    // destination and frame storage stay in their canonical owners. Entries
    // remain until the shell destructor, even after record retirement.
    struct Identity {EngineKey record{},shell{};SessionHandle session{};bool live=false,allocated=false;};
    Identity* by_record(std::uint32_t) noexcept;
    Identity* by_shell(std::uint32_t) noexcept;
    bool publish_id(Identity&) noexcept;
    void retire(Identity&) noexcept;
    void clear() noexcept;
    static bool check(void*,const PumpRequest&) noexcept;
    bool read_word(std::uint32_t,std::uint32_t&) noexcept;
    void construct(Frame&) noexcept;
    bool on_owner() const noexcept;
    bool frame_on_owner(const Frame&) const noexcept;
    void unsupported(SiteId,Frame&) noexcept;
    void close_admission() noexcept {closed_.store(true,std::memory_order_release);disable();}
    bool guard_before(SiteId,Frame&) noexcept;
    void guard_after(SiteId,Frame&) noexcept;
    struct ReturnGuard {
        std::uint32_t stack=0,continuation=0;
        media_playback::TraversalTicket ticket{};
        SiteId after=SiteId::count;
        EngineKey record{};SessionHandle session{};
        media::OperationId operation=0;media::Epoch epoch=0;
        bool check_operation=false;
    };
    ReturnGuard returns_[32]{};
    unsigned return_depth_=0;
    Adapter& state_; Memory& memory_; Services& service_; const Routes& routes_;
    Identity entries_[media::Runtime::max_sessions]{};
    std::uint64_t generation_=1;
    std::atomic<bool> enabled_{false},claimed_{false},closed_{false};
    std::atomic<std::uint32_t> owner_thread_{0};
    std::uint32_t stack_low_=0,stack_high_=0;
    ExecutionProbe execution_=nullptr;
    OwnedKeys keys_;
    RecordIngress* ingress_=nullptr;
};
// One 4 KiB process-lifetime code allocation per site; never reclaimed after
// publication, even rollback. Main/owner must also keep dispatcher context live.
struct Patch {
    std::uint32_t code=0,word_address=0,protection=0;
    unsigned code_size=0;
    unsigned char original[8]{},replacement[8]{};
    bool ready=false,may_redirect=false,flush_debt=false,protection_debt=false;
    bool emission_flush_debt=false,emission_protection_debt=false;
};
struct Group {
    Patch patches[site_count]{}; Routes routes{};
    bool installed=false,ever_published=false;
    const char* status="empty";
};
using Platform=media_presentation_gate::Platform;
// Serialized original-code startup window required. On failure reverse every
// touched site, retaining debts/ownership after any failed restoration.
bool stage(Platform&,Group&,std::uint32_t dispatcher) noexcept;
bool install(Platform&,Group&) noexcept;
bool restore(Platform&,Group&,bool quiescent,bool no_owned_shells) noexcept;
// Public emitter for production-driven actual x86 ABI fixtures.
bool encode_stub(unsigned char*,unsigned,std::uint32_t,unsigned,std::uint32_t,Routes&,unsigned&) noexcept;
#ifdef _WIN32
// Bind once before staging; context is process-lifetime, owner-thread use only.
bool query_native_owner(OwnerDomain&) noexcept;
ExecutionPoint native_execution_point() noexcept;
bool bind_dispatcher(Consumer*) noexcept;
std::uint32_t dispatcher_address() noexcept;
bool owned_eligible(std::uint32_t source,std::uint32_t flags) noexcept;
class NativeMemory final:public Memory {
public:
    bool read(std::uint32_t,void*,unsigned) noexcept override;
    bool write(std::uint32_t,const void*,unsigned) noexcept override;
    std::uint32_t allocate_shell() noexcept override;
    void release_unpublished_shell(std::uint32_t) noexcept override;
};
#endif
}
