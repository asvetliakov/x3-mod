// Bare-route refusal fixture (run_bare_device_refusal.py; docs/verification/engine-effects.md, "Native fixture
// fault"): the production proxy d3d9.dll next to this EXE, created through its Direct3DCreate9 as the game does. With
// X3M_OWNERSHIP=0 capture.cpp's create_device must return the native device unhooked: none of its 119 documented
// slots points into the proxy image (a hooked table would carry the proxy's Release, Reset, Present, SetRenderTarget,
// draw entries), and the calls that reach the runtime's own undocumented virtuals through the object's vtable pointer
// (SetRenderTarget(0, surface): slot 156 on Windows' d3d9.dll) succeed. Windows' device table itself lies outside every
// image (the VTABLE row reads heap unhooked as well, measured 2026-11-05), so the table's location proves nothing; the
// slot targets do. With X3M_OWNERSHIP=1 (the control) the device is the ownership wrapper, hooked: every slot is the
// proxy's. The runner reads the VTABLE / SLOTS / CALL / RESULT rows and the session log's device_hook / device_hooked
// rows.
// Usage: bare_device_refusal_fixture.exe (no arguments). Exit 0 = every call succeeded; 3 = setup failed; 4 = a call
// failed; 5 = an exception (the fault this fixture guards against).
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>

namespace {
using Create9 = IDirect3D9*(WINAPI*)(UINT);
unsigned failures = 0;

LONG WINAPI report(EXCEPTION_POINTERS* info) {
    std::printf("EXCEPTION code=%08lx eip=%08lx\n", info->ExceptionRecord->ExceptionCode, info->ContextRecord->Eip);
    std::fflush(stdout);
    ExitProcess(5);
}

void call(const char* name, HRESULT hr) {
    std::printf("CALL %s hr=%08lx\n", name, static_cast<unsigned long>(hr));
    if (FAILED(hr)) ++failures;
}

// The module whose image holds `address`, or "heap" when none does.
void owner(const char* tag, const void* address) {
    HMODULE module = nullptr;
    char path[MAX_PATH] = "heap";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<const char*>(address), &module) &&
        !GetModuleFileNameA(module, path, MAX_PATH))
        std::strcpy(path, "unnamed");
    std::printf("%s %s\n", tag, path);
}

// Where the 119 documented IDirect3DDevice9 slot targets lie: the proxy image, the system d3d9.dll image, elsewhere.
void slots(const char* tag, IDirect3DDevice9* device, HMODULE proxy, HMODULE system) {
    void* const* table = *reinterpret_cast<void* const* const*>(device);
    unsigned in_proxy = 0, in_system = 0, other = 0;
    for (unsigned i = 0; i < 119; ++i) {
        HMODULE module = nullptr;
        if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                static_cast<const char*>(table[i]), &module))
            module = nullptr;
        if (module && module == proxy)
            ++in_proxy;
        else if (module && module == system)
            ++in_system;
        else
            ++other;
    }
    std::printf("%s proxy=%u system=%u other=%u\n", tag, in_proxy, in_system, other);
}
} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SetUnhandledExceptionFilter(report);
    char local[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, local, MAX_PATH);
    char* slash = length ? std::strrchr(local, '\\') : nullptr;
    if (!slash || (slash - local) + 10 >= MAX_PATH) return 3;
    std::strcpy(slash + 1, "d3d9.dll");
    HMODULE proxy = LoadLibraryA(local); // the app-local proxy by its full path, never the system one by search order
    if (!proxy) return 3;
    char path[MAX_PATH] = {};
    if (GetModuleFileNameA(proxy, path, MAX_PATH)) std::printf("PROXY_MODULE %s\n", path);
    const auto create9 = reinterpret_cast<Create9>(reinterpret_cast<void*>(GetProcAddress(proxy, "Direct3DCreate9")));
    IDirect3D9* d3d = create9 ? create9(D3D_SDK_VERSION) : nullptr;
    if (!d3d) return 3;
    D3DADAPTER_IDENTIFIER9 id{};
    if (SUCCEEDED(d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id))) {
        std::printf("ADAPTER vendor=0x%04lx device=0x%04lx subsys=0x%08lx revision=%lu whql=%lu driver_version=%u.%u.%u.%u\n",
                    id.VendorId, id.DeviceId, id.SubSysId, id.Revision, id.WHQLLevel,
                    unsigned(HIWORD(id.DriverVersion.HighPart)), unsigned(LOWORD(id.DriverVersion.HighPart)),
                    unsigned(HIWORD(id.DriverVersion.LowPart)), unsigned(LOWORD(id.DriverVersion.LowPart)));
        id.Description[sizeof id.Description - 1] = 0;
        id.Driver[sizeof id.Driver - 1] = 0;
        std::printf("ADAPTER_DESC %s\nADAPTER_DRIVER %s\n", id.Description, id.Driver);
    }
    char system[MAX_PATH] = {};
    HMODULE forwarded = nullptr;
    const UINT n = GetSystemDirectoryA(system, MAX_PATH);
    if (n && n + 10 < MAX_PATH) {
        std::strcat(system, "\\d3d9.dll");
        forwarded = GetModuleHandleA(system);
        if (forwarded && GetModuleFileNameA(forwarded, path, MAX_PATH)) std::printf("D3D9_MODULE %s\n", path);
    }
    if (!forwarded) return 3;
    WNDCLASSA cls{};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "x3m_bare_device_refusal";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "bare device refusal", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr,
                                nullptr, cls.hInstance, nullptr);
    if (!window) return 3;
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24X8;
    pp.hDeviceWindow = window;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device = nullptr;
    const HRESULT created = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                              D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    call("CreateDevice", created);
    if (FAILED(created) || !device) return 3;
    owner("VTABLE", *reinterpret_cast<void**>(device));
    slots("SLOTS", device, proxy, forwarded);
    IDirect3DSurface9 *back = nullptr, *depth = nullptr;
    call("GetBackBuffer", device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back));
    call("GetDepthStencilSurface", device->GetDepthStencilSurface(&depth));
    if (!back || !depth) return 3;
    // The repro's faulting call first, then a frame's worth of the calls the game makes on its device.
    call("SetRenderTarget", device->SetRenderTarget(0, back));
    call("SetDepthStencilSurface", device->SetDepthStencilSurface(depth));
    D3DVIEWPORT9 viewport{0, 0, 64, 64, 0.f, 1.f};
    call("SetViewport", device->SetViewport(&viewport));
    for (unsigned frame = 0; frame < 3; ++frame) {
        call("Clear", device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(32, 64, 96), 1.f, 0));
        call("BeginScene", device->BeginScene());
        call("SetRenderTarget", device->SetRenderTarget(0, back));
        call("EndScene", device->EndScene());
        call("Present", device->Present(nullptr, nullptr, nullptr, nullptr));
    }
    back->Release();
    depth->Release();
    back = depth = nullptr;
    call("Reset", device->Reset(&pp)); // every default-pool reference released first
    call("GetBackBuffer", device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back));
    if (back) {
        call("SetRenderTarget", device->SetRenderTarget(0, back));
        back->Release();
    }
    owner("VTABLE_AFTER", *reinterpret_cast<void**>(device));
    slots("SLOTS_AFTER", device, proxy, forwarded);
    std::printf("RELEASE device=%lu\n", device->Release());
    std::printf("RELEASE factory=%lu\n", d3d->Release());
    DestroyWindow(window);
    std::printf("RESULT %s failures=%u\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 4 : 0;
}
