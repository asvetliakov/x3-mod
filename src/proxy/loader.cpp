#include "capture.h"
#include "telemetry.h"
#include "object_trace.h"
#include "camera_state.h"
#include "scene_hook.h"
#include "chase_camera.h"
#include "chase_aim_trace.h"
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
        x3m::camera_state::initialize(); // X3M_MOTION_OUTPUT=1 X3M_TAA=1; reads only, no patch
        x3m::log("camera_state active=%u status=%s",x3m::camera_state::available(),x3m::camera_state::status());
        // X3M_SCENE_HOOK (default on with X3M_MOTION_OUTPUT=1, 0 off): the frame
        // routine's compositing callsite, exact executable and exact bytes
        // only, otherwise fails closed (the route keeps the copy/selector
        // boundary); restored when the last device goes.
        x3m::scene_hook::initialize(&x3m::scene_end_signal);
        x3m::log("scene_hook active=%u status=%s",x3m::scene_hook::active(),x3m::scene_hook::status());
        // X3M_CAMERA=chase: the cockpit-update trampoline (exact executable and
        // bytes, install window open here); unset or anything else leaves the
        // vanilla camera and patches nothing. Kept for the process lifetime.
        if(x3m::chase_camera::wanted() && x3m::chase_camera::initialize())
            x3m::chase_aim_trace::initialize(); // chase + telemetry, observation only
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
// One `d3d9_export` line per export the first time it resolves (W1 of the
// native-Windows audit): whether the backend supplied it, and for the
// factory-creating entry points whether the returned object escaped the
// proxy's hooks (unproxied=1 also vetoes admission for the process).
void log_export(const char* name, bool forwarded, int unproxied, volatile LONG* logged) {
    if (InterlockedExchange(logged, 1) != 0) return;
    if (unproxied < 0) x3m::log("d3d9_export name=%s forwarded=%u", name, forwarded);
    else x3m::log("d3d9_export name=%s forwarded=%u unproxied=%u", name, forwarded, unproxied);
}
volatile LONG logged_create9ex = 0, logged_on12 = 0, logged_on12ex = 0;
}

// Signature-agnostic forwarders (W1): the remaining names of the system
// d3d9.dll export table that no documented signature in this project needs.
// Each export is a naked `jmp` through a slot that initially points at its
// resolver: the resolver saves every register, asks the backend for the name
// (loading it if needed, which also opens the log), publishes the target or
// the `ret N` fallback into the slot, logs once and jumps on. From the second
// call on the export is one indirect jump: no prologue, no register or stack
// assumption, so any caller convention and argument count pass through
// unchanged. Fallbacks return 0 and pop the documented argument bytes
// (DebugSetLevel 4, PSGPError 12, PSGPSampleTexture 20, the maximized-window
// shim 4: the stdcall decorations of the D3D9 SDK import library,
// _DebugSetLevel@4 / _PSGPError@12 / _PSGPSampleTexture@20 / ..Shim@4) so a backend lacking the export (Wine lacks the shim) still gets a
// well-formed return. The resolver runs outside loader lock, like `entry`.
extern "C" FARPROC __cdecl x3m_resolve_export(const char* name, FARPROC fallback, FARPROC* slot, FARPROC resolver) {
    FARPROC target = entry(name);
    const bool forwarded = target != nullptr;
    if (!target) target = fallback;
    // First publisher logs; a concurrent first call resolves the same target.
    if (InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(slot), reinterpret_cast<void*>(target), reinterpret_cast<void*>(resolver)) == reinterpret_cast<void*>(resolver))
        x3m::log("d3d9_export name=%s forwarded=%u", name, forwarded);
    return target;
}
#define X3M_FORWARDED_EXPORT(name, ret_instruction) \
extern "C" void x3m_resolve_##name(); \
extern "C" void x3m_fallback_##name(); \
extern "C" const char x3m_name_##name[]; \
extern "C" FARPROC x3m_slot_##name; \
const char x3m_name_##name[] = #name; \
FARPROC x3m_slot_##name = reinterpret_cast<FARPROC>(&x3m_resolve_##name); \
__asm__( \
    ".text\n" \
    ".globl _" #name "\n" \
    "_" #name ":\n" \
    "    jmp *_x3m_slot_" #name "\n" \
    ".globl _x3m_resolve_" #name "\n" \
    "_x3m_resolve_" #name ":\n" \
    "    pushfl\n" \
    "    pushal\n" \
    "    pushl $_x3m_resolve_" #name "\n" \
    "    pushl $_x3m_slot_" #name "\n" \
    "    pushl $_x3m_fallback_" #name "\n" \
    "    pushl $_x3m_name_" #name "\n" \
    "    call _x3m_resolve_export\n" \
    "    addl $16, %esp\n" \
    "    popal\n" \
    "    popfl\n" \
    "    jmp *_x3m_slot_" #name "\n" \
    ".globl _x3m_fallback_" #name "\n" \
    "_x3m_fallback_" #name ":\n" \
    "    xorl %eax, %eax\n" \
    "    " ret_instruction "\n" \
    ".text\n");
X3M_FORWARDED_EXPORT(DebugSetLevel, "ret $4")
X3M_FORWARDED_EXPORT(PSGPError, "ret $12")
X3M_FORWARDED_EXPORT(PSGPSampleTexture, "ret $20")
X3M_FORWARDED_EXPORT(Direct3D9EnableMaximizedWindowedModeShim, "ret $4")
#undef X3M_FORWARDED_EXPORT

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
    if (!fn) { log_export("Direct3DCreate9Ex", false, 0, &logged_create9ex); if (out) *out = nullptr; return D3DERR_NOTAVAILABLE; }
    cpu.before_original();
    const HRESULT result=fn(sdk,out);
    cpu.after_original();
    const bool escaped=SUCCEEDED(result)&&out&&*out;
    log_export("Direct3DCreate9Ex", true, escaped, &logged_create9ex);
    if(escaped)
        x3m::ownership::admission_veto(x3m::ownership::process_admission_monitor(),
            x3m::ownership::AdmissionVeto::UnobservedRoute);
    return result;
}
// The D3D9On12 factories (Windows 10 2004+; absent from Wine's d3d9 for the
// Ex form): forwarded with their documented signatures. Their objects are not
// hooked (X3AP imports only Direct3DCreate9), so an escape vetoes admission
// and is logged as unproxied=1; a backend without the export answers as the
// documented failure of each (null / D3DERR_NOTAVAILABLE).
extern "C" IDirect3D9* WINAPI Direct3DCreate9On12(UINT sdk, void* overrides, UINT override_count) {
    x3m::CpuCallBoundary cpu;
    x3m::ownership::ApplicationAdmissionAbi admission(x3m::ownership::process_admission_monitor());
    auto fn = reinterpret_cast<IDirect3D9* (WINAPI*)(UINT, void*, UINT)>(entry("Direct3DCreate9On12"));
    if (!fn) { log_export("Direct3DCreate9On12", false, 0, &logged_on12); return nullptr; }
    cpu.before_original();
    IDirect3D9* result = fn(sdk, overrides, override_count);
    cpu.after_original();
    log_export("Direct3DCreate9On12", true, result != nullptr, &logged_on12);
    if (result) x3m::ownership::admission_veto(x3m::ownership::process_admission_monitor(), x3m::ownership::AdmissionVeto::UnobservedRoute);
    return result;
}
extern "C" HRESULT WINAPI Direct3DCreate9On12Ex(UINT sdk, void* overrides, UINT override_count, IDirect3D9Ex** out) {
    x3m::CpuCallBoundary cpu;
    x3m::ownership::ApplicationAdmissionAbi admission(x3m::ownership::process_admission_monitor());
    auto fn = reinterpret_cast<HRESULT (WINAPI*)(UINT, void*, UINT, IDirect3D9Ex**)>(entry("Direct3DCreate9On12Ex"));
    if (!fn) { log_export("Direct3DCreate9On12Ex", false, 0, &logged_on12ex); if (out) *out = nullptr; return D3DERR_NOTAVAILABLE; }
    cpu.before_original();
    const HRESULT result = fn(sdk, overrides, override_count, out);
    cpu.after_original();
    const bool escaped = SUCCEEDED(result) && out && *out;
    log_export("Direct3DCreate9On12Ex", true, escaped, &logged_on12ex);
    if (escaped) x3m::ownership::admission_veto(x3m::ownership::process_admission_monitor(), x3m::ownership::AdmissionVeto::UnobservedRoute);
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
        LARGE_INTEGER stamp{}; QueryPerformanceCounter(&stamp); x3m::dll_load_qpc = static_cast<unsigned long long>(stamp.QuadPart);
    }
    return TRUE;
}
