#pragma once
#include "media_destination_state.h"
#include "media_presentation_gate.h"
#include <atomic>
#include <mutex>
namespace x3m::media {class FrameLease;}
namespace x3m::media_destination {
struct CopyRequest {BindingOwner owner{};media::OperationId operation=0;media::Epoch epoch=0;media_playback::TraversalTicket traversal{};};
struct Current {bool operation_live=false;media_playback::Continuation continuation=media_playback::Continuation::live;};
struct CurrentCheck {void* stable_adapter=nullptr;Current(*read)(void*,const CopyRequest&) noexcept=nullptr;};
enum class CopyKind {written,unavailable,superseded,copy_failed,abort_traversal};
enum class Reason {none,admission,binding,frame,identity,descriptor,lock,pitch,unlock,internal};
struct CopyResult {CopyKind kind=CopyKind::unavailable;std::int32_t status=1;media_playback::Continuation continuation=media_playback::Continuation::live;std::uint64_t binding_epoch=0,frame_sequence=0;Reason reason=Reason::none;};
struct Descriptor {std::uint32_t width=0,height=0,format=0,pool=0,usage=0,multisample=0;};
struct Mapping {void* bits=nullptr;std::int32_t pitch=0;};
// CPU-only registry snapshot. No destination domain lock is held on this call.
struct IdentitySource {virtual ~IdentitySource()=default;virtual bool snapshot(std::uint32_t device,std::uint32_t surface,ownership::SurfaceLeaseIdentity&) noexcept=0;};
// One stack-owned backend for one synchronous copy. The native implementation
// owns the accepted SurfaceLease. Test implementations exercise the SAME core.
struct CopyBackend {
    virtual ~CopyBackend()=default;
    virtual std::int32_t acquire(const State::Snapshot&) noexcept=0;
    virtual std::int32_t describe(Descriptor&) noexcept=0;
    virtual std::int32_t lock(Mapping&) noexcept=0;
    virtual std::int32_t unlock() noexcept=0;
    virtual void release() noexcept=0;
    // Internal completed-stage checkpoint only; normal implementation is empty.
    virtual void checkpoint(unsigned) {}
};
class Destination {
public:
    Destination(media_presentation_gate::Admission& gate,IdentitySource& source,std::uint32_t thread) noexcept:gate_(gate),source_(source),thread_(thread){}
    bool same_thread(std::uint32_t thread) const noexcept {return thread&&thread==thread_;}
    bool owner(std::uint32_t thread) noexcept;
    void disable() noexcept {disabled_.store(true);gate_.disable();}
    bool ready() noexcept;
    void set_device_key(std::uint32_t key,std::uint32_t thread) noexcept;
    bool observe_record(BindingOwner,std::uint32_t slot,bool bound) noexcept;
    void cancel(media::SessionHandle) noexcept;
    void clear_records() noexcept;
    // Raw memory producer replay is confined to this domain by Observer below.
    std::mutex& domain() noexcept {return mutex_;}
    State& state_locked() noexcept {return state_;}
    void qualify(State::Publication,bool recovery=false) noexcept;
    void recover_watches() noexcept;
    CopyResult try_copy(const CopyRequest&,const media::FrameLease&,CurrentCheck,CopyBackend&) noexcept;
#ifdef _WIN32
    CopyResult try_copy(const CopyRequest&,const media::FrameLease&,CurrentCheck) noexcept;
#endif
    bool begin_engine_reset(std::uint32_t thread) noexcept;
    void end_engine_reset(std::uint32_t thread,bool result) noexcept;
    void native_reset(std::uint32_t thread,std::uint32_t device,bool begin,std::int32_t result) noexcept;
private:
    bool current(const State::Snapshot&) noexcept;
    media_presentation_gate::Admission& gate_;IdentitySource& source_;const std::uint32_t thread_;
    std::mutex mutex_;State state_;
    std::atomic<bool> disabled_{false};
    bool engine_reset_=false,native_seen_=false,native_success_=false;
};
#ifdef _WIN32
class NativeIdentitySource final:public IdentitySource {public:bool snapshot(std::uint32_t,std::uint32_t,ownership::SurfaceLeaseIdentity&) noexcept override;};
// Serialized startup binding. Both objects are process-lifetime and owner-qualified.
void bind_reset_observer(Destination*) noexcept;
#endif
}
#include "media_destination_sites.h"
