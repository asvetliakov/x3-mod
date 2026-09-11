#include "capture.h"
#include "telemetry.h"
#include "object_trace.h"
#include "object_lifetime.h"
#include "../ownership/d3d9_ownership.h"
#include "../ownership/application_admission_abi.h"
#include "cpu_state.h"
#include <string>

namespace {
HMODULE self_module;
HMODULE backend;
bool ownership_enabled = false;
bool depth_copy_enabled = false;
bool finite_positions_enabled = false;
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
BOOL CALLBACK load_backend(PINIT_ONCE, PVOID, PVOID*) {
    x3m::initialize_log(self_module);
    const bool admission_requested=x3m::ownership::process_admission_monitor()!=nullptr;
    x3m::log("application_admission_mode requested=%u enabled=%u live_replay=0 coverage_complete=0",
        admission_requested,admission_requested);
    // Process-local experimental switch; default remains the native capture
    // path. Read once, outside loader lock, before exposing any factory.
    wchar_t setting[8]{};
    ownership_enabled = GetEnvironmentVariableW(L"X3M_OWNERSHIP", setting, 8) == 1 && setting[0] == L'1';
    const bool depth_requested = GetEnvironmentVariableW(L"X3M_DEPTH_COPY", setting, 8) == 1 && setting[0] == L'1';
    depth_copy_enabled = ownership_enabled && depth_requested;
    const bool finite_requested = GetEnvironmentVariableW(L"X3M_FINITE_POSITIONS", setting, 8) == 1 && setting[0] == L'1';
    finite_positions_enabled = ownership_enabled && finite_requested;
    if (ownership_enabled || depth_requested)
        x3m::log("ownership_mode requested=%u depth_copy_requested=%u depth_copy_enabled=%u scope=normal9 fallback=native", ownership_enabled, depth_requested, depth_copy_enabled);
    if (finite_requested)
        x3m::log("finite_upload_mode requested=1 enabled=%u scope=verified_managed_uploads payload_retained=0", finite_positions_enabled);
    wchar_t path[32768]{};
    // Absolute system path avoids reloading this app-local proxy. Never search PATH.
    UINT length = GetSystemDirectoryW(path, 32750);
    if (!length || length >= 32750) return TRUE;
    std::wstring full = std::wstring(path) + L"\\d3d9.dll";
    const auto load_begin = x3m::telemetry::now();
    backend = LoadLibraryExW(full.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    const DWORD load_error = GetLastError();
    const auto load_end = x3m::telemetry::now();
    if (x3m::telemetry::enabled())
        x3m::log("telemetry_span name=backend_load qpc_begin=%llu qpc_end=%llu thread=%lu success=%u error=%lu",load_begin,load_end,GetCurrentThreadId(),backend!=nullptr,load_error);
    if (backend == self_module) {
        FreeLibrary(backend);
        backend = nullptr;
    }
    if (backend) {
        GetModuleFileNameW(backend, path, 32768);
        x3m::log("backend path=%ls", path);
        x3m::object_trace::initialize();
        x3m::log("object_trace active=%u status=%s recovery_required=%u",x3m::object_trace::active(),x3m::object_trace::status(),x3m::object_trace::recovery_required());
        x3m::object_lifetime::initialize();
        const auto lifetime_stats=x3m::object_lifetime::stats();
        x3m::log("object_lifetime active=%u status=%s recovery_required=%u baseline_complete=%u baseline_entries=%lu",
            x3m::object_lifetime::active(),x3m::object_lifetime::status(),x3m::object_lifetime::recovery_required(),
            lifetime_stats.baseline_complete,static_cast<DWORD>(lifetime_stats.baseline_entries));
    } else {
        x3m::log("ERROR backend load failed error=%lu", load_error);
    }
    return TRUE;
}
FARPROC entry(const char* name) {
    InitOnceExecuteOnce(&once, load_backend, nullptr, nullptr);
    return backend ? GetProcAddress(backend, name) : nullptr;
}
}

extern "C" IDirect3D9* WINAPI Direct3DCreate9(UINT sdk) {
    x3m::CpuCallBoundary cpu;
    x3m::ownership::ApplicationAdmissionAbi admission(x3m::ownership::process_admission_monitor());
    auto fn = reinterpret_cast<IDirect3D9* (WINAPI*)(UINT)>(entry("Direct3DCreate9"));
    const auto begin = x3m::telemetry::now();
    cpu.before_original();
    IDirect3D9* result = fn ? fn(sdk) : nullptr;
    cpu.after_original();
    const auto end = x3m::telemetry::now();
    if (x3m::telemetry::enabled())
        x3m::log("telemetry_span name=direct3d_create9 qpc_begin=%llu qpc_end=%llu thread=%lu success=%u",begin,end,GetCurrentThreadId(),result!=nullptr);
    if (result && ownership_enabled) {
        IDirect3D9* wrapped = nullptr;
        x3m::ownership::Options options{};
        options.capture_auto_depth = depth_copy_enabled;
        options.track_buffer_writes = x3m::object_trace::active() || finite_positions_enabled;
        options.capture_finite_positions = finite_positions_enabled;
        // Live application-call admission is not yet serialized with replay.
        // Keep execution observation off until live replay consumes it. Its
        // synchronized snapshots do not provide write exclusion. Native
        // component fixtures opt in explicitly.
        options.track_execution_state = false;
        const HRESULT adopted = x3m::ownership::wrap_factory(result, &wrapped, options);
        if (SUCCEEDED(adopted)) {
            // Successful adoption consumes the native factory reference. Capture
            // observes the public wrapper, so all child getters share its COM
            // identity. Internal parent Release dispatches through this vtable.
            x3m::log("ownership_factory mode=wrapped native=%p public=%p result=%08lx", result, wrapped, adopted);
            result = wrapped;
        } else {
            // Failed adoption leaves the original reference with this caller.
            x3m::ownership::admission_veto(x3m::ownership::process_admission_monitor(),
                x3m::ownership::AdmissionVeto::UnobservedRoute);
            x3m::log("ownership_factory mode=native_fallback native=%p result=%08lx", result, adopted);
        }
    } else if(result) {
        // Capture patches only selected native slots; it is not full admission
        // coverage for an unwrapped factory/device returned to the application.
        x3m::ownership::admission_veto(x3m::ownership::process_admission_monitor(),
            x3m::ownership::AdmissionVeto::UnobservedRoute);
    }
    if (result) x3m::hook_direct3d(result);
    return result;
}
extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex** out) {
    x3m::CpuCallBoundary cpu;
    x3m::ownership::ApplicationAdmissionAbi admission(x3m::ownership::process_admission_monitor());
    // X3AP imports only Create9. The native Ex object is not wrapped; its escape
    // must permanently refuse replay, while keeping the original API result.
    auto fn = reinterpret_cast<HRESULT (WINAPI*)(UINT, IDirect3D9Ex**)>(entry("Direct3DCreate9Ex"));
    if (!fn) { if (out) *out = nullptr; return D3DERR_NOTAVAILABLE; }
    cpu.before_original();
    const HRESULT result=fn(sdk,out);
    cpu.after_original();
    if(SUCCEEDED(result)&&out&&*out)
        x3m::ownership::admission_veto(x3m::ownership::process_admission_monitor(),
            x3m::ownership::AdmissionVeto::UnobservedRoute);
    return result;
}
extern "C" int WINAPI D3DPERF_BeginEvent(D3DCOLOR c, LPCWSTR n) {
    x3m::CpuCallBoundary cpu;
    x3m::ownership::ApplicationAdmissionAbi admission(x3m::ownership::process_admission_monitor());
    auto fn = reinterpret_cast<int (WINAPI*)(D3DCOLOR,LPCWSTR)>(entry("D3DPERF_BeginEvent"));
    cpu.before_original();const int result=fn ? fn(c,n) : -1;cpu.after_original();return result;
}
extern "C" int WINAPI D3DPERF_EndEvent() {
    x3m::CpuCallBoundary cpu;
    x3m::ownership::ApplicationAdmissionAbi admission(x3m::ownership::process_admission_monitor());
    auto fn = reinterpret_cast<int (WINAPI*)()>(entry("D3DPERF_EndEvent"));
    cpu.before_original();const int result=fn ? fn() : -1;cpu.after_original();return result;
}
extern "C" DWORD WINAPI D3DPERF_GetStatus() {
    x3m::CpuCallBoundary cpu;
    x3m::ownership::ApplicationAdmissionAbi admission(x3m::ownership::process_admission_monitor());
    auto fn = reinterpret_cast<DWORD (WINAPI*)()>(entry("D3DPERF_GetStatus"));
    cpu.before_original();const DWORD result=fn ? fn() : 0;cpu.after_original();return result;
}
extern "C" BOOL WINAPI D3DPERF_QueryRepeatFrame() {
    x3m::CpuCallBoundary cpu;
    x3m::ownership::ApplicationAdmissionAbi admission(x3m::ownership::process_admission_monitor());
    auto fn = reinterpret_cast<BOOL (WINAPI*)()>(entry("D3DPERF_QueryRepeatFrame"));
    cpu.before_original();const BOOL result=fn ? fn() : FALSE;cpu.after_original();return result;
}
#define FORWARD_MARKER(name) \
extern "C" void WINAPI name(D3DCOLOR c, LPCWSTR n) { \
    x3m::CpuCallBoundary cpu; \
    x3m::ownership::ApplicationAdmissionAbi admission(x3m::ownership::process_admission_monitor()); \
    auto fn = reinterpret_cast<void (WINAPI*)(D3DCOLOR,LPCWSTR)>(entry(#name)); \
    cpu.before_original();if (fn) fn(c,n);cpu.after_original(); \
}
FORWARD_MARKER(D3DPERF_SetMarker)
FORWARD_MARKER(D3DPERF_SetRegion)
extern "C" void WINAPI D3DPERF_SetOptions(DWORD o) {
    x3m::CpuCallBoundary cpu;
    x3m::ownership::ApplicationAdmissionAbi admission(x3m::ownership::process_admission_monitor());
    auto fn = reinterpret_cast<void (WINAPI*)(DWORD)>(entry("D3DPERF_SetOptions"));
    cpu.before_original();if (fn) fn(o);cpu.after_original();
}
extern "C" void WINAPI DebugSetMute() {
    x3m::CpuCallBoundary cpu;
    x3m::ownership::ApplicationAdmissionAbi admission(x3m::ownership::process_admission_monitor());
    auto fn = reinterpret_cast<void (WINAPI*)()>(entry("DebugSetMute"));
    cpu.before_original();if (fn) fn();cpu.after_original();
}
extern "C" void* WINAPI Direct3DShaderValidatorCreate9() {
    x3m::CpuCallBoundary cpu;
    x3m::ownership::ApplicationAdmissionAbi admission(x3m::ownership::process_admission_monitor());
    auto fn = reinterpret_cast<void* (WINAPI*)()>(entry("Direct3DShaderValidatorCreate9"));
    cpu.before_original();void* result=fn ? fn() : nullptr;cpu.after_original();
    // A future validated D3DX helper may supply narrow lifetime authority.
    // Nested admission by itself does not certify the returned native interface.
    if(result)x3m::ownership::admission_veto(x3m::ownership::process_admission_monitor(),
        x3m::ownership::AdmissionVeto::UnobservedRoute);
    return result;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    // Backend loading, file I/O and hooks intentionally happen outside loader lock.
    if (reason == DLL_PROCESS_ATTACH) {
        self_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
