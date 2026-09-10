#include "capture.h"
#include "telemetry.h"
#include "../ownership/d3d9_ownership.h"
#include <string>

namespace {
HMODULE self_module;
HMODULE backend;
bool ownership_enabled = false;
bool sampleable_depth_enabled = false;
INIT_ONCE once = INIT_ONCE_STATIC_INIT;
BOOL CALLBACK load_backend(PINIT_ONCE, PVOID, PVOID*) {
    x3m::initialize_log(self_module);
    // Process-local experimental switch; default remains the native capture
    // path. Read once, outside loader lock, before exposing any factory.
    wchar_t setting[8]{};
    ownership_enabled = GetEnvironmentVariableW(L"X3M_OWNERSHIP", setting, 8) == 1 && setting[0] == L'1';
    const bool depth_requested = GetEnvironmentVariableW(L"X3M_SAMPLEABLE_DEPTH", setting, 8) == 1 && setting[0] == L'1';
    sampleable_depth_enabled = ownership_enabled && depth_requested;
    if (ownership_enabled || depth_requested)
        x3m::log("ownership_mode requested=%u sampleable_depth_requested=%u sampleable_depth_enabled=%u scope=normal9 fallback=native", ownership_enabled, depth_requested, sampleable_depth_enabled);
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
    auto fn = reinterpret_cast<IDirect3D9* (WINAPI*)(UINT)>(entry("Direct3DCreate9"));
    const auto begin = x3m::telemetry::now();
    IDirect3D9* result = fn ? fn(sdk) : nullptr;
    const auto end = x3m::telemetry::now();
    if (x3m::telemetry::enabled())
        x3m::log("telemetry_span name=direct3d_create9 qpc_begin=%llu qpc_end=%llu thread=%lu success=%u",begin,end,GetCurrentThreadId(),result!=nullptr);
    if (result && ownership_enabled) {
        IDirect3D9* wrapped = nullptr;
        x3m::ownership::Options options{};
        options.sampleable_auto_depth = sampleable_depth_enabled;
        const HRESULT adopted = x3m::ownership::wrap_factory(result, &wrapped, options);
        if (SUCCEEDED(adopted)) {
            // Successful adoption consumes the native factory reference. Capture
            // observes the public wrapper, so all child getters share its COM
            // identity. Internal parent Release dispatches through this vtable.
            x3m::log("ownership_factory mode=wrapped native=%p public=%p result=%08lx", result, wrapped, adopted);
            result = wrapped;
        } else {
            // Failed adoption leaves the original reference with this caller.
            x3m::log("ownership_factory mode=native_fallback native=%p result=%08lx", result, adopted);
        }
    }
    if (result) x3m::hook_direct3d(result);
    return result;
}
extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT sdk, IDirect3D9Ex** out) {
    // X3AP imports only Create9. Ex is transparently forwarded, uninstrumented.
    auto fn = reinterpret_cast<HRESULT (WINAPI*)(UINT, IDirect3D9Ex**)>(entry("Direct3DCreate9Ex"));
    if (!fn) { if (out) *out = nullptr; return D3DERR_NOTAVAILABLE; }
    return fn(sdk, out);
}
extern "C" int WINAPI D3DPERF_BeginEvent(D3DCOLOR c, LPCWSTR n) {
    auto fn = reinterpret_cast<int (WINAPI*)(D3DCOLOR,LPCWSTR)>(entry("D3DPERF_BeginEvent"));
    return fn ? fn(c,n) : -1;
}
extern "C" int WINAPI D3DPERF_EndEvent() {
    auto fn = reinterpret_cast<int (WINAPI*)()>(entry("D3DPERF_EndEvent"));
    return fn ? fn() : -1;
}
extern "C" DWORD WINAPI D3DPERF_GetStatus() {
    auto fn = reinterpret_cast<DWORD (WINAPI*)()>(entry("D3DPERF_GetStatus"));
    return fn ? fn() : 0;
}
extern "C" BOOL WINAPI D3DPERF_QueryRepeatFrame() {
    auto fn = reinterpret_cast<BOOL (WINAPI*)()>(entry("D3DPERF_QueryRepeatFrame"));
    return fn ? fn() : FALSE;
}
#define FORWARD_MARKER(name) \
extern "C" void WINAPI name(D3DCOLOR c, LPCWSTR n) { \
    auto fn = reinterpret_cast<void (WINAPI*)(D3DCOLOR,LPCWSTR)>(entry(#name)); \
    if (fn) fn(c,n); \
}
FORWARD_MARKER(D3DPERF_SetMarker)
FORWARD_MARKER(D3DPERF_SetRegion)
extern "C" void WINAPI D3DPERF_SetOptions(DWORD o) {
    auto fn = reinterpret_cast<void (WINAPI*)(DWORD)>(entry("D3DPERF_SetOptions"));
    if (fn) fn(o);
}
extern "C" void WINAPI DebugSetMute() {
    auto fn = reinterpret_cast<void (WINAPI*)()>(entry("DebugSetMute"));
    if (fn) fn();
}
extern "C" void* WINAPI Direct3DShaderValidatorCreate9() {
    auto fn = reinterpret_cast<void* (WINAPI*)()>(entry("Direct3DShaderValidatorCreate9"));
    return fn ? fn() : nullptr;
}
BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    // Backend loading, file I/O and hooks intentionally happen outside loader lock.
    if (reason == DLL_PROCESS_ATTACH) {
        self_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
