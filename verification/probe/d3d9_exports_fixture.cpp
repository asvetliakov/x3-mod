// Export-table fixture of the proxy (W1 and W3 of the native-Windows audit).
// Loads the d3d9.dll next to this executable (the proxy, under Wine's
// d3d9=n,b), resolves the seventeen names of the system DLL's export table
// and calls the entry points whose behaviour is defined on this backend:
// D3DPERF_GetStatus (the existing C++ forwarder), Direct3DCreate9On12 (the
// C++ forwarder of the 9On12 factory: forwarded where the backend exports it,
// the object released here), Direct3D9EnableMaximizedWindowedModeShim(FALSE)
// (a naked forwarder whose backend export is absent under Wine: the `ret 4`
// fallback must return 0 and pop exactly its argument) and
// Direct3DCreate9On12Ex (the C++ forwarder's D3DERR_NOTAVAILABLE answer
// without a backend export). The other naked forwarders (DebugSetLevel,
// PSGPError, PSGPSampleTexture) are resolved but not called: Wine's are
// unimplemented spec stubs that raise. Prints one line per export and call
// and a RESULT line; the runner reads the proxy's session log for the
// d3d9_export lines and, in the read-only-directory case, for the
// capture_dir fallback (%LOCALAPPDATA%\x3-modern-renderer\captures).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>

namespace {
unsigned checks = 0, failures = 0;
void check(bool ok, const char* label) {
    ++checks; if (!ok) ++failures;
    std::printf("CHECK %s %s\n", label, ok ? "PASS" : "FAIL");
}
const char* const names[] = {
    "D3DPERF_BeginEvent", "D3DPERF_EndEvent", "D3DPERF_GetStatus", "D3DPERF_QueryRepeatFrame",
    "D3DPERF_SetMarker", "D3DPERF_SetOptions", "D3DPERF_SetRegion",
    "DebugSetLevel", "DebugSetMute", "Direct3D9EnableMaximizedWindowedModeShim",
    "Direct3DCreate9", "Direct3DCreate9Ex", "Direct3DCreate9On12", "Direct3DCreate9On12Ex",
    "Direct3DShaderValidatorCreate9", "PSGPError", "PSGPSampleTexture"};
}
// Calls a one-argument stdcall entry point and reports how many bytes the
// callee popped (4 for a well-formed `ret 4`, 0 when it left the argument):
// the naked forwarder's fallback is signature-agnostic, so its stack
// discipline is verified here rather than assumed by the compiler.
extern "C" unsigned __cdecl call_stdcall1(void* function, unsigned argument, int* popped);
__asm__(
    ".text\n"
    ".globl _call_stdcall1\n"
    "_call_stdcall1:\n"
    "    pushl %ebp\n"
    "    movl %esp, %ebp\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    movl 12(%ebp), %eax\n"
    "    pushl %eax\n"
    "    movl %esp, %esi\n"
    "    movl 8(%ebp), %eax\n"
    "    call *%eax\n"
    "    movl %esp, %ecx\n"
    "    subl %esi, %ecx\n"
    "    movl 16(%ebp), %edx\n"
    "    movl %ecx, (%edx)\n"
    "    leal 4(%esi), %esp\n"
    "    popl %edi\n"
    "    popl %esi\n"
    "    popl %ebp\n"
    "    ret\n");

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const bool readonly = argc > 1 && !std::strcmp(argv[1], "readonly");
    char value[MAX_PATH * 4]{};
    GetEnvironmentVariableA("LOCALAPPDATA", value, sizeof value);
    std::printf("ENV LOCALAPPDATA=%s\n", value[0] ? value : "-");
    value[0] = 0; GetEnvironmentVariableA("USERPROFILE", value, sizeof value);
    std::printf("ENV USERPROFILE=%s\n", value[0] ? value : "-");
    HMODULE proxy = LoadLibraryA("d3d9.dll");
    if (!proxy) { std::printf("RESULT FAIL LoadLibrary error=%lu\n", GetLastError()); return 1; }
    char path[MAX_PATH]{}; GetModuleFileNameA(proxy, path, MAX_PATH);
    std::printf("MODULE %s readonly=%u\n", path, readonly);
    unsigned resolved = 0;
    for (const char* name : names) {
        FARPROC address = GetProcAddress(proxy, name);
        resolved += address != nullptr;
        std::printf("EXPORT name=%s address=%p\n", name, reinterpret_cast<void*>(address));
    }
    check(resolved == 17, "all seventeen system d3d9 export names resolve on the proxy");
    // 1. An existing forwarder: loads the backend (and opens the session log).
    auto get_status = reinterpret_cast<DWORD (WINAPI*)()>(GetProcAddress(proxy, "D3DPERF_GetStatus"));
    const DWORD status = get_status ? get_status() : 0xffffffffu;
    std::printf("CALL name=D3DPERF_GetStatus result=%08lx\n", status);
    check(get_status != nullptr, "D3DPERF_GetStatus callable");
    // 2. The 9On12 factory: an object where the backend exports the name (released here).
    auto on12 = reinterpret_cast<IDirect3D9* (WINAPI*)(UINT, void*, UINT)>(GetProcAddress(proxy, "Direct3DCreate9On12"));
    IDirect3D9* factory = on12 ? on12(D3D_SDK_VERSION, nullptr, 0) : nullptr;
    std::printf("CALL name=Direct3DCreate9On12 result=%p\n", static_cast<void*>(factory));
    check(on12 != nullptr, "Direct3DCreate9On12 callable");
    if (factory) {
        UINT adapters = factory->GetAdapterCount();
        std::printf("FACTORY adapters=%u\n", adapters);
        check(adapters >= 1, "the 9On12 factory is a live IDirect3D9");
        const ULONG refs = factory->Release();
        check(refs == 0, "the 9On12 factory released to zero");
    }
    // 3. The naked forwarder's fallback (Wine has no Direct3D9EnableMaximizedWindowedModeShim).
    void* shim = reinterpret_cast<void*>(GetProcAddress(proxy, "Direct3D9EnableMaximizedWindowedModeShim"));
    int popped = -1;
    const unsigned shim_result = shim ? call_stdcall1(shim, 0, &popped) : 0xffffffffu;
    std::printf("CALL name=Direct3D9EnableMaximizedWindowedModeShim result=%08x popped=%d\n", shim_result, popped);
    check(shim != nullptr && shim_result == 0 && popped == 4, "the shim fallback returns 0 and pops its one argument");
    // A second call goes through the resolved slot (no second resolution, one log line).
    popped = -1;
    const unsigned shim_again = shim ? call_stdcall1(shim, 1, &popped) : 0xffffffffu;
    check(shim_again == 0 && popped == 4, "the shim's second call is the same fallback");
    // 4. The Ex factory of 9On12 without a backend export: the documented failure, no object.
    auto on12ex = reinterpret_cast<HRESULT (WINAPI*)(UINT, void*, UINT, IDirect3D9Ex**)>(GetProcAddress(proxy, "Direct3DCreate9On12Ex"));
    IDirect3D9Ex* ex = reinterpret_cast<IDirect3D9Ex*>(static_cast<void*>(&checks)); // poisoned: the call must clear it
    const HRESULT ex_result = on12ex ? on12ex(D3D_SDK_VERSION, nullptr, 0, &ex) : E_FAIL;
    std::printf("CALL name=Direct3DCreate9On12Ex result=%08lx out=%p\n", ex_result, static_cast<void*>(ex));
    if (SUCCEEDED(ex_result) && ex) { ex->Release(); check(true, "a 9On12Ex factory was returned and released"); }
    else check(on12ex != nullptr && ex_result == D3DERR_NOTAVAILABLE && ex == nullptr, "Direct3DCreate9On12Ex without a backend export answers D3DERR_NOTAVAILABLE and null");
    FreeLibrary(proxy);
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
