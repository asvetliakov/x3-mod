#include "media_root.h"
namespace x3m::media_root {
namespace {
template<class Patch> bool patch_debt(const Patch& p) noexcept {
    return p.flush_debt||p.protection_debt||p.emission_flush_debt||p.emission_protection_debt;
}
template<class Group> bool group_ready(const Group& g) noexcept {
    if(!g.installed)return false;
    for(const auto& p:g.patches)if(!p.ready||!p.may_redirect||patch_debt(p))return false;
    return true;
}
}
bool Ingress::owner() const noexcept {
    if(!probe_)return false;
    const auto p=probe_();return p.thread&&p.thread==owner_.thread&&p.stack>=owner_.low&&p.stack<owner_.high;
}
bool Ingress::stack(std::uint32_t p,unsigned n) const noexcept {
    return n&&p>=owner_.low&&p<owner_.high&&n<=owner_.high-p;
}
void Ingress::retiring(media_playback::EngineKey key) noexcept {
    std::lock_guard<std::mutex> lock(destination_.domain());auto& state=destination_.state_locked();
    for(auto& watch:state.watches)if(watch.used&&watch.owner.record==key)state.cancel(watch.owner.session);
}
void Ingress::retiring_address(std::uint32_t address) noexcept {
    std::lock_guard<std::mutex> lock(destination_.domain());auto& state=destination_.state_locked();
    for(auto& watch:state.watches)if(watch.used&&watch.owner.record.address==address)state.cancel(watch.owner.session);
}
void Ingress::clearing() noexcept {
    std::lock_guard<std::mutex> lock(destination_.domain());destination_.state_locked().clear_watches();
}
void Ingress::foreign_refusal() noexcept {
    std::lock_guard<std::mutex> lock(destination_.domain());
    veto_.store(true,std::memory_order_release);destination_.disable();
    auto& state=destination_.state_locked();state.disabled=true;state.clear_watches();
}
bool Ingress::read(std::uint32_t p,void* out,unsigned n) noexcept {
    if(!owner()||!p||!out||!n||p>UINT32_MAX-n)return false;
    if(stack(p,n))return memory_.read(p,out,n);
    // A span intersecting the owner stack must fit wholly; never let a stack
    // overflow turn into a heap copy (including after permanent veto).
    if(p<owner_.high&&p+n>owner_.low)return false;
    std::lock_guard<std::mutex> lock(destination_.domain());return healthy()&&memory_.read(p,out,n);
}
bool Ingress::write(std::uint32_t p,const void* in,unsigned n) noexcept {
    if(!owner()||!p||!in||!n||p>UINT32_MAX-n)return false;
    if(stack(p,n))return memory_.write(p,in,n);
    if(p<owner_.high&&p+n>owner_.low)return false;
    std::lock_guard<std::mutex> lock(destination_.domain());return healthy()&&memory_.write(p,in,n);
}
std::uint32_t Ingress::allocate_shell() noexcept {return owner()&&healthy()?memory_.allocate_shell():0;}
void Ingress::release_unpublished_shell(std::uint32_t p) noexcept {if(owner())memory_.release_unpublished_shell(p);}
Root::Root(media_presentation_gate::Platform& p,Hooks& h,media_destination::IdentitySource& identity,
    media_engine::Memory& raw,media_destination::Memory& records,media_startup::Controller& startup,
    media_engine::OwnerDomain owner,media_engine::ExecutionProbe probe,media_services::Counter counter,
    media_destination::CopyBackend* copy) noexcept
    :destination(admission,identity,owner.thread),ingress(destination,raw,owner,probe),
     services(adapter,destination,startup,counter,copy),consumer(adapter,ingress,services,consumers.routes),
     observer(destination,records,destinations.routes,{&consumer,&binding}),platform_(p),hooks_(h),owner_(owner){}
bool Root::binding(void* context,std::uint32_t address,media::SessionHandle& session,media_playback::EngineKey& key) noexcept {
    return static_cast<media_engine::Consumer*>(context)->binding_owner(address,session,key);
}
bool Root::debt() const noexcept {
    if(patch_debt(gate)||(!gate.installed&&gate.may_redirect))return true;
    for(const auto& p:destinations.patches)if(patch_debt(p)||(!destinations.installed&&p.may_redirect))return true;
    for(const auto& p:consumers.patches)if(patch_debt(p)||(!consumers.installed&&p.may_redirect))return true;
    return false;
}
bool Root::rollback() noexcept {
    consumer.disable();admission.disable();failed_=true;installed_=false;
    // Clear only this root's predicate; an unrelated owner is never overwritten.
    const bool cue=hooks_.compose(false);
    const bool quiescent=ingress.owner()&&platform_.install_window()&&admission.depth()==0;
    const bool c=media_engine::restore(platform_,consumers,quiescent,consumer.shells()==0);
    const bool d=media_destination::restore(platform_,destinations,quiescent,admission.depth()==0);
    const bool g=!gate.admission||media_presentation_gate::restore(platform_,gate,admission,quiescent);
    installation_={};
    destination.disable();return cue&&c&&d&&g&&!debt();
}
bool Root::install() noexcept {
    if(attempted_||!ingress.owner())return false;
    attempted_=true;pinned_=hooks_.retain();
    if(!pinned_)return false;
    bindings_=hooks_.bind(*this);
    ingress_bound_=bindings_.consumer&&consumer.bind_ingress(ingress);
    composition_=hooks_.cue();
    if(!bindings_.consumer||!bindings_.observer||!bindings_.reset||!ingress_bound_||
       !admission.qualify_owner(owner_.thread)||composition_==Composition::unavailable){rollback();return false;}
    // All 44 original spans and emitted routes are staged/sealed before the
    // first publication. Groups retain the actual Routes objects above.
    if(!media_presentation_gate::stage(platform_,gate,media_presentation_gate::game_site(),admission)||
       !media_destination::stage(platform_,destinations,hooks_.destination_dispatch())||
       !media_engine::stage(platform_,consumers,hooks_.consumer_dispatch())){rollback();return false;}
    if(!media_presentation_gate::install(platform_,gate,admission)||
       !media_destination::install(platform_,destinations)||!media_engine::install(platform_,consumers)||
       debt()||!hooks_.compose(true)){rollback();return false;}
    installed_=true;record_installation();return true;
}
void Root::record_installation() noexcept {
    auto& r=installation_;r={};
    r.consumer_group=group_ready(consumers);
    for(auto route:consumers.routes.forward)r.consumer_group=r.consumer_group&&route;
    for(auto route:consumers.routes.unobserved)r.consumer_group=r.consumer_group&&route;
    for(auto route:consumers.routes.after)r.consumer_group=r.consumer_group&&route;
    const auto& routes=consumers.routes;
    r.consumer_group=r.consumer_group&&routes.return_plain&&routes.destroy_free&&routes.manager_drop4&&
        routes.manager_drop8&&routes.manager_next8&&routes.manager_error8&&routes.speech_drop4;
    r.cue_composed=composition_!=Composition::unavailable&&hooks_.composed();
    r.destination=group_ready(destinations)&&gate.installed&&gate.ready&&!patch_debt(gate)&&ingress_bound_&&bindings_.reset;
    r.speech_transaction=r.consumer_group&&routes.after[9]&&routes.after[10]&&routes.speech_drop4&&ingress_bound_;
    r.mixed_list_returns=r.consumer_group&&ingress_bound_&&routes.manager_drop4&&routes.manager_drop8;
    r.callback_lifetime=pinned_&&bindings_.consumer&&bindings_.observer&&ingress_bound_;
    r.exception_cleanup=r.callback_lifetime&&r.mixed_list_returns&&r.destination;
    r.owner_thread=owner_.thread&&ingress.owner()&&ingress_bound_&&bindings_.consumer;
}
media_engine::Readiness Root::readiness() noexcept {
    media_engine::Readiness r{};if(!ingress.owner()||failed_||!installed_)return r;
    // Immutable successful installation evidence is cached once. The hot path
    // inspects only dynamic owner, device, recovery and permanent veto state.
    r=installation_;r.cue_composed=r.cue_composed&&hooks_.composed();
    bool recovered=false;
    {std::lock_guard<std::mutex> lock(destination.domain());const auto& state=destination.state_locked();
     recovered=state.recovery_ready&&!state.disabled;}
    r.destination=r.destination&&device_&&destination.ready()&&recovered&&ingress.healthy();
    r.owner_thread=r.owner_thread&&device_&&ingress.healthy();return r;
}
Report Root::report() const noexcept {
    Report out{};out.owner=owner_.thread;
    if(!ingress.owner()){out.state=ReportState::wrong_owner;return out;}
    if(maintaining_||admission.depth()){out.state=ReportState::busy;return out;}
    out.state=ReportState::available;out.device=device_;out.installed=installed_;out.debt=debt();
    out.veto=failed_||closing_.load(std::memory_order_acquire)||!ingress.healthy();
    out.consumer_admission=consumer.enabled();out.copy_admission=admission.enabled();out.capacity_closed=consumer.admission_closed();
    out.services=services.diagnostics();return out;
}
void Root::device_published(std::uint32_t application,bool canonical) noexcept {
    if(!ingress.owner()||!installed_||!application||!ingress.healthy())return;
    // A successful replacement invalidates old provenance even if its address
    // is reused. A noncanonical replacement closes this domain conservatively.
    if(!canonical){publication_blocked();return;}
    device_=application;destination.set_device_key(application,owner_.thread);
}
void Root::present(std::uint32_t application) noexcept {
    if(!ingress.owner()||admission.depth()||maintaining_)return;
    const bool closing=closing_.load(std::memory_order_acquire)||failed_||!ingress.healthy();
    if(!closing&&(!device_||application!=device_))return;
    maintaining_=true;
    if(closing||!destination.ready()){
        consumer.disable();admission.disable();installation_={};services.close_admission_and_cancel_on_owner();
    }else{
        const auto r=readiness();services.maintenance_on_owner(r);
        if(r.complete()&&services.ready()){
            if(!admission_started_){admission_started_=admission.enable(owner_.thread);}
            // Historical-key capacity exhaustion closes new constructors only;
            // the already-owned sessions retain their qualified copy gate.
            if(consumer.admission_closed()){consumer.disable();}
            else if(!admission_started_||!admission.enabled()||!consumer.enable(r)){consumer.disable();admission.disable();}
        }else consumer.disable();
    }
    maintaining_=false;
}
}
#ifdef _WIN32
#include "media_presentation_gate_win32.h"
#include "media_cue.h"
#include "../ownership/d3d9_ownership.h"
#include <windows.h>
#include <new>
namespace x3m::media_root {
namespace {
std::atomic<Root*> process_root{nullptr};
std::atomic<bool> creation_attempted{false};
class NativeHooks final:public Hooks {
public:
    HMODULE pin=nullptr;
    bool retain() noexcept override {
        return pin||GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&configure),&pin);
    }
    Bindings bind(Root& r) noexcept override {
        const bool consumer=media_engine::bind_dispatcher(&r.consumer);
        const bool observer=consumer&&media_destination::bind_dispatcher(&r.observer);
        const bool reset=observer&&media_destination::bind_reset_observer(&r.destination);
        return {consumer,observer,reset};
    }
    Composition cue() noexcept override {
        return static_cast<Composition>(media_cue::composition());
    }
    bool compose(bool enable) noexcept override {
        return enable?media_cue::set_owned_eligibility(&media_engine::owned_eligible):
            media_cue::clear_owned_eligibility(&media_engine::owned_eligible);
    }
    bool composed() const noexcept override {return media_cue::owned_eligibility_is(&media_engine::owned_eligible);}
    std::uint32_t consumer_dispatch() noexcept override {return media_engine::dispatcher_address();}
    std::uint32_t destination_dispatch() noexcept override {return media_destination::dispatcher_address();}
};
std::uint64_t counter(void*) noexcept {LARGE_INTEGER n{};return QueryPerformanceCounter(&n)&&n.QuadPart>=0?std::uint64_t(n.QuadPart):0;}
struct NativeRoot {
    media_presentation_gate::Win32Platform platform;NativeHooks hooks;
    media_destination::NativeIdentitySource identity;media_engine::NativeMemory memory;
    media_destination::NativeMemory observations;Root root;
    NativeRoot(media_engine::OwnerDomain owner,std::uint64_t frequency) noexcept
        :root(platform,hooks,identity,memory,observations,media_startup::process(),owner,
              &media_engine::native_execution_point,{frequency,nullptr,&counter}){}
};
bool install(void*) noexcept {
    if(creation_attempted.exchange(true))return false;
    media_engine::OwnerDomain owner{};LARGE_INTEGER frequency{};
    if(!media_engine::query_native_owner(owner)||!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0)return false;
    // Pin before constructing/publishing process-lifetime callback contexts.
    HMODULE pin=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&configure),&pin))return false;
    void* storage=VirtualAlloc(nullptr,sizeof(NativeRoot),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!storage)return false;
    auto* native=new(storage)NativeRoot(owner,std::uint64_t(frequency.QuadPart));native->hooks.pin=pin;
    process_root.store(&native->root,std::memory_order_release);
    return native->root.install();
}
bool bootstrap(void*,const media_startup::BootstrapContext& context) noexcept {
    auto* root=process_root.load(std::memory_order_acquire);
    return root&&root->installed()&&media_services::MediaServices::bootstrap(&root->services,context);
}
}
bool configure() noexcept {return media_startup::configure(&bootstrap,nullptr,&install);}
void on_device_created(void* application,bool capture_clear) noexcept {
    auto* root=process_root.load(std::memory_order_acquire);
    if(!root||!root->ingress.owner()||!application)return;
    if(!capture_clear){root->publication_blocked();return;}
    // Caller has completed hook_device and released capture's HookGuard. This
    // canonical membership lookup completes before the destination domain lock.
    const bool canonical=ownership::borrowed_native_device(static_cast<IDirect3DDevice9*>(application))!=nullptr;
    root->device_published(reinterpret_cast<std::uint32_t>(application),canonical);
}
Report report_on_owner() noexcept {
    Report out{};if(auto* root=process_root.load(std::memory_order_acquire))out=root->report();
    out.startup=media_startup::snapshot();return out;
}
void on_present_owner(void* application) noexcept {
    if(auto* root=process_root.load(std::memory_order_acquire))root->present(reinterpret_cast<std::uint32_t>(application));
}
}
#endif
