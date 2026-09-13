// Focused production scene-hook patch/binding integration. The lower-level
// CPU/SEH matrix remains covered by compositor_bridge's unchanged fixture.
#include "../../src/proxy/scene_hook.h"
#include "../../src/proxy/engine_patch.h"
#include <cstdio>
#include <cstring>

namespace x3m::object_trace { bool executable_verified() { return true; } }
namespace {
unsigned checks=0, failures=0, originals=0, signals=0, pres=0, posts=0, cleanups=0;
unsigned char* site=nullptr;
bool refuse=false, active_shutdown=false;
int context_value=19;
void check(bool value, const char* name) {
    ++checks; if(!value) { ++failures; std::printf("FAIL %s\n",name); }
}
void original() { ++originals; }
void listener() { ++signals; }
void pre(const X3mCompositorFrame* f, void* storage, void* context) {
    ++pres;
    check(context==&context_value,"pre_context");
    check(f->caller_pc==reinterpret_cast<uintptr_t>(site+5),"actual_caller_pc");
    check(x3m::scene_hook::compositor_caller_pc()==f->caller_pc,"site_caller_pc");
    check(!(f->flags&X3M_CB_ORIGINAL_RETURNED),"pre_output_not_valid");
    unsigned char zero[X3M_CB_STORAGE_SIZE]{};
    check(!std::memcmp(storage,zero,sizeof zero),"fresh_invocation_storage");
    *static_cast<unsigned*>(storage)=refuse?0:0x1234;
    if(active_shutdown) {
        check(!x3m::scene_hook::shutdown(),"active_shutdown_refused");
        check(x3m::scene_hook::compositor_active(),"active_shutdown_keeps_binding");
    }
}
void post(const X3mCompositorFrame* f, void* storage, void* context) {
    ++posts;
    check(context==&context_value,"post_context");
    check(f->flags&X3M_CB_ORIGINAL_RETURNED,"post_output_valid");
    check(*static_cast<unsigned*>(storage)==(refuse?0u:0x1234u),"post_storage");
}
void cleanup(const X3mCompositorFrame*, void*, void* context, int abnormal) {
    ++cleanups;
    check(context==&context_value&&!abnormal,"normal_cleanup");
}
}
int main() {
    site=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    if(!site) return 2;
    unsigned char initial[6]={0xe8,0,0,0,0,0xc3};
    const auto displacement=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&original)-reinterpret_cast<uintptr_t>(site+5));
    std::memcpy(initial+1,&displacement,4); std::memcpy(site,initial,sizeof initial);
    DWORD old=0;
    if(!VirtualProtect(site,4096,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),site,6))return 2;
    const auto invoke=reinterpret_cast<void(*)()>(site);
    invoke(); check(originals==1,"unpatched_original");
    check(x3m::scene_hook::fixture_install(site,reinterpret_cast<void*>(&original),&listener),"tail_install");
    check(!x3m::scene_hook::compositor_active(),"tail_not_bridge");
    invoke(); check(originals==2&&signals==1,"tail_original_once");
    check(x3m::scene_hook::fixture_shutdown(),"tail_restore");
    check(!std::memcmp(site,initial,6),"tail_bytes_restored");
    X3mCompositorBinding callbacks{nullptr,&pre,&post,&cleanup,&context_value};
    check(!x3m::scene_hook::fixture_install_compositor(site,reinterpret_cast<void*>(1),&callbacks),"wrong_target_refused");
    check(!x3m::scene_hook::installed()&&x3m_compositor_bridge_active()==0,"wrong_target_no_live_patch");
    auto invalid=callbacks; invalid.cleanup=nullptr;
    check(!x3m::scene_hook::fixture_install_compositor(site,reinterpret_cast<void*>(&original),&invalid),"missing_cleanup_refused");
    check(x3m::scene_hook::fixture_install_compositor(site,reinterpret_cast<void*>(&original),&callbacks),"bridge_install");
    check(x3m::scene_hook::compositor_active(),"bridge_active");
    // Mutating caller storage cannot mutate the hook-owned binding copy.
    callbacks.context=nullptr; callbacks.post=nullptr;
    active_shutdown=true; invoke(); active_shutdown=false;
    check(originals==3&&pres==1&&posts==1&&cleanups==1,"bridge_original_once");
    refuse=true; invoke();
    check(originals==4&&pres==2&&posts==2&&cleanups==2,"decline_original_once");
    check(x3m::scene_hook::signals()==3,"signals_tail_and_bridge");
    check(x3m_compositor_bridge_active()==0,"invocations_drained");
    check(x3m::scene_hook::fixture_shutdown(),"bridge_restore");
    check(!x3m::scene_hook::compositor_active()&&!x3m::scene_hook::compositor_caller_pc(),"bridge_caller_revoked");
    check(!std::memcmp(site,initial,6),"bridge_bytes_restored");
    invoke(); check(originals==5&&pres==2,"restored_original_once");
    check(x3m::scene_hook::fixture_shutdown(),"shutdown_idempotent");
    SetEnvironmentVariableW(L"X3M_SCENE_HOOK",L"1");
    x3m::engine_patch::close_install_window("fixture");
    check(!x3m::scene_hook::initialize(&listener),"late_production_install_refused");
    check(!std::strcmp(x3m::scene_hook::status(),"late_claim"),"late_claim_reason");
    check(!std::memcmp(site,initial,6),"late_install_no_mutation");
    VirtualFree(site,0,MEM_RELEASE);
    std::printf("scene_compositor checks=%u failures=%u\n",checks,failures);
    return failures?1:0;
}
