#include "media_destination.h"
#include "lav_worker.h"
#include <cstring>
#include <limits>
#ifdef _WIN32
#include "../ownership/d3d9_ownership.h"
#include <windows.h>
#endif
namespace x3m::media_destination {
bool Destination::owner(std::uint32_t thread) noexcept {if(thread!=thread_||!thread){disable();return false;}return !disabled_.load();}
bool Destination::ready() noexcept {std::lock_guard<std::mutex> lock(mutex_);return !disabled_.load()&&!state_.disabled&&state_.device_key;}
void Destination::set_device_key(std::uint32_t key,std::uint32_t thread) noexcept {if(!owner(thread))return;std::lock_guard<std::mutex> lock(mutex_);state_.set_device(key);}
bool Destination::observe_record(BindingOwner o,std::uint32_t slot,bool bound) noexcept {
    {std::lock_guard<std::mutex> lock(mutex_);if(disabled_.load()||!state_.watch(o,slot,bound))return false;}
    recover_watches();return true;
}
void Destination::cancel(media::SessionHandle s) noexcept {std::lock_guard<std::mutex> lock(mutex_);state_.cancel(s);}
void Destination::clear_records() noexcept {std::lock_guard<std::mutex> lock(mutex_);state_.clear_watches();}
void Destination::qualify(State::Publication p,bool recovery) noexcept {
    if(p.wrapper==State::none||!p.device||!p.surface)return;
    ownership::SurfaceLeaseIdentity id{};if(!source_.snapshot(p.device,p.surface,id)){if(recovery){std::lock_guard<std::mutex> lock(mutex_);state_.refuse(p);}return;}
    std::lock_guard<std::mutex> lock(mutex_);state_.qualify(p,id,recovery);
}
void Destination::recover_watches() noexcept {
    for(unsigned i=0;i<media::Runtime::max_sessions;++i){State::Publication p;
        {std::lock_guard<std::mutex> lock(mutex_);if(!state_.recovery_ready||state_.depth)return;p=state_.watched_candidate(i);}
        qualify(p,true);
    }
}
bool Destination::current(const State::Snapshot& s) noexcept {std::lock_guard<std::mutex> lock(mutex_);return !disabled_.load()&&state_.current(s);}
namespace {
struct Cleanup {
    CopyBackend& backend;bool held=false,locked=false;std::int32_t unlock_result=0;
    static void close(void* p) noexcept {
        auto& c=*static_cast<Cleanup*>(p);
        if(c.locked){c.locked=false;c.unlock_result=c.backend.unlock();}
        if(c.held){c.held=false;c.backend.release();}
    }
};
CopyResult classify(Current c,CopyResult out) noexcept {
    out.continuation=c.continuation;
    if(c.continuation!=media_playback::Continuation::live)out.kind=CopyKind::abort_traversal;
    else if(!c.operation_live)out.kind=CopyKind::superseded;
    return out;
}
}
CopyResult Destination::try_copy(const CopyRequest& request,const media::FrameLease& frame,CurrentCheck check,CopyBackend& backend) noexcept {
    CopyResult out{};if(!check.read)return out;
    auto live=check.read(check.stable_adapter,request);out=classify(live,out);
    if(!live.operation_live||live.continuation!=media_playback::Continuation::live)return out;
    if(!frame){out.reason=Reason::frame;return out;}const auto& view=frame.view();out.frame_sequence=view.sequence;
    if(!(view.identity.session==request.owner.session)||view.identity.operation!=request.operation||view.identity.epoch!=request.epoch||
       !view.bgra||view.slot>=media::lav_detail::slot_count||!view.graph||!view.width||!view.height||view.width>512||view.height>512||
       view.pitch<view.width*4||std::uint64_t(view.height-1)*view.pitch+view.width*4>media::lav_detail::frame_bytes){out.reason=Reason::frame;return out;}
    Cleanup cleanup{backend};media_presentation_gate::CopyScope scope(gate_,thread_,Cleanup::close,&cleanup);
    if(!scope){out.reason=Reason::admission;return out;}
    State::Snapshot captured;bool have=false,wrote=false;
    const auto valid=[&]() noexcept {const auto now=check.read(check.stable_adapter,request);return now.operation_live&&now.continuation==media_playback::Continuation::live&&current(captured);};
    try {
        do {
            {std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock||disabled_.load()||!state_.snapshot(request.owner,captured)){out.reason=Reason::binding;break;}have=true;}
            out.binding_epoch=captured.binding;out.status=backend.acquire(captured);
            if(out.status!=0){out.reason=Reason::identity;break;}cleanup.held=true;backend.checkpoint(1);
            if(!valid())break;
            Descriptor desc{};out.status=backend.describe(desc);backend.checkpoint(2);
            if(out.status<0){out.kind=CopyKind::copy_failed;out.reason=Reason::descriptor;break;}
            if(!valid())break;
            // Public D3DFMT A8R8G8B8/X8R8G8B8, nonmultisampled, writable BGRA.
            // DEFAULT supports only actual lockable offscreen/dynamic resources;
            // a denied LockRect remains the real HRESULT, never a retry/Reset.
            if((desc.format!=21&&desc.format!=22)||desc.width!=view.width||desc.height!=view.height||desc.multisample||desc.pool>2||
               (desc.usage&~std::uint32_t(1|0x200))){out.reason=Reason::descriptor;break;}
            Mapping mapping{};out.status=backend.lock(mapping);
            if(out.status<0){out.kind=CopyKind::copy_failed;out.reason=Reason::lock;break;}cleanup.locked=true;backend.checkpoint(3);
            if(!valid())break;
            if(!mapping.bits||mapping.pitch<0||std::uint32_t(mapping.pitch)<view.width*4||
               std::uint64_t(view.height-1)*std::uint32_t(mapping.pitch)+view.width*4>std::numeric_limits<std::size_t>::max()||
               reinterpret_cast<std::uintptr_t>(mapping.bits)>UINTPTR_MAX-(std::uint64_t(view.height-1)*std::uint32_t(mapping.pitch)+view.width*4)){
                out.reason=Reason::pitch;out.kind=CopyKind::copy_failed;break;
            }
            for(unsigned row=0;row<view.height;++row)std::memcpy(static_cast<unsigned char*>(mapping.bits)+std::size_t(row)*mapping.pitch,view.bgra+std::size_t(row)*view.pitch,view.width*4);
            wrote=true;backend.checkpoint(4);
        }while(false);
    }catch(...){out.kind=CopyKind::copy_failed;out.reason=Reason::internal;out.status=std::int32_t(0x80004005u);}
    scope.close();
    const bool binding_current=have&&current(captured);
    if(cleanup.unlock_result<0){{std::lock_guard<std::mutex> lock(mutex_);state_.refuse(captured);}out.kind=CopyKind::copy_failed;out.reason=Reason::unlock;out.status=cleanup.unlock_result;}
    const auto now=check.read(check.stable_adapter,request);out=classify(now,out);
    if(out.kind==CopyKind::abort_traversal||out.kind==CopyKind::superseded)return out;
    if(have&&!binding_current){out.kind=CopyKind::unavailable;out.reason=Reason::binding;return out;}
    if(wrote&&cleanup.unlock_result>=0&&out.kind!=CopyKind::copy_failed){out.kind=CopyKind::written;out.status=0;}
    return out;
}
bool Destination::begin_engine_reset(std::uint32_t thread) noexcept {
    if(!owner(thread)||!gate_.begin_reset(thread,media_presentation_gate::Admission::Reset::engine)){disable();return false;}
    engine_reset_=true;native_seen_=native_success_=false;
    std::lock_guard<std::mutex> lock(mutex_);state_.recovery_ready=false;return true;
}
void Destination::end_engine_reset(std::uint32_t thread,bool result) noexcept {
    if(!owner(thread))return;
    const bool success=engine_reset_&&native_seen_&&native_success_&&result;
    if(!gate_.end_reset(thread,media_presentation_gate::Admission::Reset::engine,success)){disable();return;}
    engine_reset_=false;
    if(success&&gate_.observe_recovery(thread)){{std::lock_guard<std::mutex> lock(mutex_);state_.recovery_ready=true;}recover_watches();}
}
void Destination::native_reset(std::uint32_t thread,std::uint32_t device,bool begin,std::int32_t result) noexcept {
    if(!owner(thread))return;
    {std::lock_guard<std::mutex> lock(mutex_);if(device!=state_.device_key)return;state_.recovery_ready=false;}
    if(begin){native_seen_=true;native_success_=false;if(!gate_.begin_reset(thread,media_presentation_gate::Admission::Reset::native))disable();}
    else {native_success_=result>=0;if(!gate_.end_reset(thread,media_presentation_gate::Admission::Reset::native,native_success_))disable();}
}
#ifdef _WIN32
namespace {
class NativeBackend final:public CopyBackend {
    ownership::SurfaceLease lease_;
public:
    std::int32_t acquire(const State::Snapshot& s) noexcept override {return ownership::acquire_surface_lease(reinterpret_cast<IDirect3DDevice9*>(s.device),reinterpret_cast<IDirect3DSurface9*>(s.surface),s.identity,lease_);}
    std::int32_t describe(Descriptor& d) noexcept override {D3DSURFACE_DESC value{};auto hr=lease_.get()->GetDesc(&value);if(SUCCEEDED(hr))d={value.Width,value.Height,std::uint32_t(value.Format),std::uint32_t(value.Pool),value.Usage,std::uint32_t(value.MultiSampleType)};return hr;}
    std::int32_t lock(Mapping& m) noexcept override {D3DLOCKED_RECT value{};auto hr=lease_.get()->LockRect(&value,nullptr,0);if(SUCCEEDED(hr))m={value.pBits,value.Pitch};return hr;}
    std::int32_t unlock() noexcept override {return lease_.get()->UnlockRect();}
    void release() noexcept override {lease_.reset();}
};
std::atomic<Destination*> reset_destination{nullptr};
void reset_event(const ownership::ResetEvent& e) noexcept {if(auto* d=reset_destination.load())d->native_reset(GetCurrentThreadId(),reinterpret_cast<std::uint32_t>(e.application),e.phase==ownership::ResetPhase::begin,e.result);}
}
bool NativeIdentitySource::snapshot(std::uint32_t d,std::uint32_t s,ownership::SurfaceLeaseIdentity& id) noexcept {return ownership::snapshot_surface_identity(reinterpret_cast<IDirect3DDevice9*>(d),reinterpret_cast<IDirect3DSurface9*>(s),&id)==S_OK;}
CopyResult Destination::try_copy(const CopyRequest& r,const media::FrameLease& f,CurrentCheck c) noexcept {if(!owner(GetCurrentThreadId()))return {};NativeBackend backend;return try_copy(r,f,c,backend);}
bool bind_reset_observer(Destination* d) noexcept {
    if(!d)return false;
    Destination* empty=nullptr;
    if(!reset_destination.compare_exchange_strong(empty,d,std::memory_order_acq_rel,std::memory_order_acquire)&&empty!=d)return false;
    return ownership::claim_reset_observer(reset_event);
}
bool reset_observer_bound(const Destination* d) noexcept {
    return d&&reset_destination.load(std::memory_order_acquire)==d&&ownership::reset_observer_is(reset_event);
}
#endif
#include "media_destination_stubs_inc.h"
#ifdef _WIN32
namespace {std::atomic<Observer*> dispatcher{nullptr};}
#pragma GCC push_options
#pragma GCC optimize("no-exceptions")
extern "C" __attribute__((force_align_arg_pointer,noinline)) void __cdecl
x3m_media_destination_dispatch(Frame* frame,unsigned event) noexcept {
    // Inline transport only: an exception-enabled out-of-line CpuState helper
    // could register SJLJ before save or after restore despite this shell.
    unsigned char fp[108];unsigned mxcsr;
    asm volatile("fnsave %0\n\tfninit\n\tstmxcsr %1":"=m"(fp),"=m"(mxcsr)::"memory");
    const DWORD error=GetLastError();
    if(auto* observer=dispatcher.load(std::memory_order_acquire))observer->dispatch(event,*frame,GetCurrentThreadId());
    SetLastError(error);
    asm volatile("frstor %0\n\tldmxcsr %1"::"m"(fp),"m"(mxcsr):"memory");
}
#pragma GCC pop_options
bool bind_dispatcher(Observer* value) noexcept {
    if(!value)return false;
    Observer* empty=nullptr;
    return dispatcher.compare_exchange_strong(empty,value,std::memory_order_acq_rel,std::memory_order_acquire)||empty==value;
}
std::uint32_t dispatcher_address() noexcept {static_assert(sizeof(void*)==4);return reinterpret_cast<std::uint32_t>(&x3m_media_destination_dispatch);}
bool NativeMemory::read(std::uint32_t address,void* out,unsigned bytes) noexcept {if(!address||!out||address>UINT32_MAX-bytes)return false;std::memcpy(out,reinterpret_cast<void*>(address),bytes);return true;}
bool NativeMemory::write(std::uint32_t address,const void* value,unsigned bytes) noexcept {if(!address||!value||address>UINT32_MAX-bytes)return false;std::memcpy(reinterpret_cast<void*>(address),value,bytes);return true;}
#endif
}
