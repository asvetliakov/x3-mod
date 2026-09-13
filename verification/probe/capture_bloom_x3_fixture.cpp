// Real capture bridge/Release/Reset/BloomPass integration, with no game image.
// All selected seam inputs are borrowed by bind; the invocation must retain
// them before original can drop every caller-owned DEFAULT resource reference.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
using Bind = int (*)(void (*)(), IDirect3DDevice9*, IDirect3DTexture9*, IDirect3DSurface9*, IDirect3DSurface9*);
using Entry = void* (*)();
using Unbind = int (*)();
using Query = unsigned (*)(unsigned);
Bind bind_fixture; Entry entry_fixture; Unbind unbind_fixture; Query query;
IDirect3DDevice9* device;
IDirect3DTexture9* scene;
IDirect3DSurface9* main_surface;
D3DPRESENT_PARAMETERS parameters;
const char* case_name;
unsigned checks, failures, originals, caught, continued, resumed;
unsigned native_release, during_absent, during_expired, extra_release;
HRESULT reset_hr;
bool scene_active, use_ex;
void check(bool ok, const char* name) {
    ++checks;
    if (!ok) { ++failures; std::printf("CHECK_FAIL case=%s name=%s\n", case_name, name); }
}
bool api(HRESULT hr, const char* name) {
    check(SUCCEEDED(hr), name);
    if (FAILED(hr)) std::printf("API_FAIL case=%s name=%s hr=%08lx\n", case_name, name, (unsigned long)hr);
    return SUCCEEDED(hr);
}
bool is(const char* name) { return std::strcmp(case_name, name)==0; }
bool terminal() { return is("final_release") || is("thread_final_release"); }
bool resetting() { return std::strncmp(case_name,"reset",5)==0; }
void drop_external_resources() {
    if (scene) { auto* old=scene; scene=nullptr; old->Release(); }
    if (main_surface) { auto* old=main_surface; main_surface=nullptr; old->Release(); }
}
DWORD WINAPI release_worker(void*) { native_release=device->Release(); return 0; }
void original() {
    ++originals;
    DWORD value=0;
    // Any exception below originates after this normally completed hook, never
    // inside injected GPU work or a GCC hook frame with a live RAII lock.
    api(device->GetRenderState(D3DRS_ZENABLE,&value),"original_GetRenderState");
    api(device->SetRenderState(D3DRS_ZENABLE,value),"original_SetRenderState");
    if (is("get_device")) {
        IDirect3DDevice9* alias=nullptr;
        if (api(main_surface->GetDevice(&alias),"GetDevice")) {
            check(alias==device,"GetDevice_identity"); extra_release=alias->Release();
            check(extra_release>0,"GetDevice_nonterminal_count");
        }
    } else if (terminal()) {
        drop_external_resources();
        if (is("thread_final_release")) {
            HANDLE worker=CreateThread(nullptr,0,release_worker,nullptr,0,nullptr);
            check(worker!=nullptr,"CreateThread");
            if (!worker) ExitProcess(20);
            DWORD wait=WaitForSingleObject(worker,10000);
            check(wait==WAIT_OBJECT_0,"unlocked_original_worker_completes");
            if (wait!=WAIT_OBJECT_0) ExitProcess(21);
            CloseHandle(worker);
        } else native_release=device->Release();
        check(native_release>0,"deferred_native_count_positive");
        during_absent=query(8); during_expired=query(9);
        check(during_absent==0,"context_mapped_during_original");
        check(during_expired==0,"CPU_pin_alive_during_original");
    } else if (resetting()) {
        api(device->EndScene(),"original_EndScene"); scene_active=false;
        drop_external_resources();
        D3DPRESENT_PARAMETERS reset=parameters;
        if (is("reset_fail") || is("reset_ex_fail")) reset.SwapEffect=static_cast<D3DSWAPEFFECT>(0);
        reset_hr=use_ex?static_cast<IDirect3DDevice9Ex*>(device)->ResetEx(&reset,nullptr):device->Reset(&reset);
        check((is("reset_fail")||is("reset_ex_fail"))?FAILED(reset_hr):SUCCEEDED(reset_hr),"Reset_expected_result");
    } else if (is("escape") || is("continue")) {
        RaiseException(0xe042b100,0,0,nullptr);
        ++resumed;
    }
}
template<class T> bool symbol(HMODULE dll,const char* name,T& out) {
    FARPROC proc=GetProcAddress(dll,name);
    static_assert(sizeof(proc)==sizeof(out)); std::memcpy(&out,&proc,sizeof(out));
    check(out!=nullptr,name); return out!=nullptr;
}
}
extern "C" int capture_bloom_outer_filter(void* opaque) {
    auto* exception=static_cast<EXCEPTION_POINTERS*>(opaque);
    if (exception->ExceptionRecord->ExceptionCode!=0xe042b100) return EXCEPTION_CONTINUE_SEARCH;
    if (is("continue")) { ++continued; return EXCEPTION_CONTINUE_EXECUTION; }
    return EXCEPTION_EXECUTE_HANDLER;
}
extern "C" void capture_bloom_outer_caught() { ++caught; }
extern "C" void capture_bloom_outer_call(void (*)());

int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    case_name="setup";
    HMODULE dll=LoadLibraryA("d3d9.dll"); check(dll!=nullptr,"LoadLibrary");
    if (!dll) return 1;
    using Create9=IDirect3D9*(WINAPI*)(UINT);
    using Create9Ex=HRESULT(WINAPI*)(UINT,IDirect3D9Ex**);
    Create9 create9=nullptr; Create9Ex create9ex=nullptr;
    if (!symbol(dll,"Direct3DCreate9",create9) || !symbol(dll,"Direct3DCreate9Ex",create9ex)
        || !symbol(dll,"x3m_bloom_lifetime_fixture_bind",bind_fixture)
        || !symbol(dll,"x3m_bloom_lifetime_fixture_entry",entry_fixture)
        || !symbol(dll,"x3m_bloom_lifetime_fixture_unbind",unbind_fixture)
        || !symbol(dll,"x3m_bloom_lifetime_fixture_query",query)) return 2;
    HWND window=CreateWindowExA(0,"STATIC","Capture bloom lifetime fixture",WS_OVERLAPPEDWINDOW,
                               0,0,64,64,nullptr,nullptr,GetModuleHandleA(nullptr),nullptr);
    check(window!=nullptr,"CreateWindow"); if (!window) return 3;
    const char* cases[]={"normal","get_device","final_release","thread_final_release",
                         "reset","reset_fail","reset_ex","reset_ex_fail","escape","continue"};
    unsigned completed=0, skipped=0;
    for (const char* name: cases) {
        case_name=name;
        unsigned failures_before=failures;
        originals=caught=continued=resumed=native_release=extra_release=0;
        during_absent=during_expired=999; reset_hr=S_FALSE;
        scene_active=false; device=nullptr; scene=nullptr; main_surface=nullptr;
        use_ex=is("reset_ex")||is("reset_ex_fail");
        IDirect3D9* d3d=nullptr;
        if (use_ex) {
            IDirect3D9Ex* ex=nullptr;
            HRESULT hr=create9ex(D3D_SDK_VERSION,&ex);
            if (hr==D3DERR_NOTAVAILABLE && !ex) {
                std::printf("CASE name=%s status=SKIP reason=create9ex_unavailable hr=%08lx\n",name,(unsigned long)hr);
                ++skipped; continue;
            }
            if (!api(hr,"Create9Ex") || !ex) return 10;
            d3d=ex;
        } else d3d=create9(D3D_SDK_VERSION);
        check(d3d!=nullptr,"Create9"); if (!d3d) return 4;
        parameters={}; parameters.BackBufferWidth=16; parameters.BackBufferHeight=16;
        parameters.BackBufferFormat=D3DFMT_A8R8G8B8; parameters.BackBufferCount=1;
        parameters.SwapEffect=D3DSWAPEFFECT_DISCARD; parameters.hDeviceWindow=window;
        parameters.Windowed=TRUE; parameters.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        DWORD flags=D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED;
        HRESULT hr;
        if (use_ex) {
            IDirect3DDevice9Ex* ex=nullptr;
            hr=static_cast<IDirect3D9Ex*>(d3d)->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,flags,&parameters,nullptr,&ex);
            device=ex;
        } else hr=d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,flags,&parameters,&device);
        if (!api(hr,"CreateDevice")) return 5;
        if (!api(device->GetRenderTarget(0,&main_surface),"GetRenderTarget")
            || !api(device->CreateTexture(16,16,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&scene,nullptr),"CreateScene")) return 6;
        IDirect3DSurface9* source=nullptr;
        if (!api(scene->GetSurfaceLevel(0,&source),"SceneSurface")) return 7;
        api(device->ColorFill(source,nullptr,D3DCOLOR_XRGB(80,120,160)),"FillScene"); source->Release();
        // Ex adoption attaches actual MotionOutput/HDR resources and runs
        // its attach self-tests, which must occur outside BeginScene.
        const bool bound=bind_fixture(original,device,scene,main_surface,nullptr)!=0;
        check(bound,"bind"); if (!bound) return 11;
        api(device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_ARGB(255,20,30,40),1.f,0),"ClearMain");
        if (!api(device->BeginScene(),"BeginScene")) return 8;
        scene_active=true;
        void* entry=entry_fixture(); check(entry!=nullptr,"entry"); if (!entry) return 9;
        void (*target)(); static_assert(sizeof(target)==sizeof(entry)); std::memcpy(&target,&entry,sizeof(target));
        capture_bloom_outer_call(target);
        check(originals==1,"original_once");
        check(query(0)==1,"pre_once"); check(query(2)==1,"cleanup_once");
        check(query(4)==1,"real_prepare_success"); check(query(11)==0,"bridge_quiescent");
        check(query(14)>0,"persistent_motion_resources_at_pre");
        check(query(15)==1,"HDR_enabled_at_pre");
        check(query(16)==1,"TAA_enabled_at_pre");
        check(query(3)==(is("escape")?1u:0u),"abnormal_count");
        check(query(1)==(is("escape")?0u:1u),"post_count");
        check(query(5)==((resetting()||is("escape"))?0u:1u),"commit_admission");
        check(caught==(is("escape")?1u:0u),"outer_catch");
        check(continued==(is("continue")?1u:0u),"continued_search");
        check(resumed==(is("continue")?1u:0u),"original_resumed");
        if (resetting()) {
            check(query(6)==1,"reset_hook_once"); check(query(7)==1,"DEFAULT_refs_gone_before_Reset");
            check(query(10)==1,"native_pin_kept_before_Reset");
        } else check(query(6)==0,"no_reset");
        if (terminal()) {
            check(query(8)==1,"device_map_erased_by_cleanup");
            check(query(9)==1,"CPU_context_destroyed_after_cleanup");
            device=nullptr; scene_active=false;
        } else {
            check(query(8)==0,"nonterminal_context_mapped");
            if (scene_active) { api(device->EndScene(),"EndScene"); scene_active=false; }
            drop_external_resources();
            native_release=device->Release(); device=nullptr;
            check(native_release==0,"final_native_Release_zero");
            check(query(8)==1,"device_map_erased"); check(query(9)==1,"CPU_context_expired");
        }
        std::printf("CASE name=%s status=%s pre=%u post=%u cleanup=%u abnormal=%u prepared=%u committed=%u resets=%u defaults_clear=%u map_absent=%u cpu_expired=%u pin_kept=%u active=%u prepare_hr=%08x commit_hr=%08x original=%u caught=%u continued=%u resumed=%u release=%u during_absent=%u during_expired=%u reset_hr=%08lx motion_refs=%u hdr=%u taa=%u\n",
            name,failures==failures_before?"PASS":"FAIL",query(0),query(1),query(2),query(3),query(4),query(5),query(6),query(7),query(8),query(9),query(10),query(11),query(12),query(13),originals,caught,continued,resumed,native_release,during_absent,during_expired,(unsigned long)reset_hr,query(14),query(15),query(16));
        check(unbind_fixture()!=0,"unbind_quiescent");
        d3d->Release(); ++completed;
    }
    DestroyWindow(window);
    // Keep the proxy mapped through process exit: bridge count alone does not
    // assert arbitrary module unload safety.
    std::printf("RESULT %s checks=%u failures=%u cases=%u skipped=%u\n",failures?"FAIL":"PASS",checks,failures,completed,skipped);
    return failures?1:0;
}
