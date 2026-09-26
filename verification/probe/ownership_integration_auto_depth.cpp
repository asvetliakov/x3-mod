// Loader-to-ownership option wiring smoke test, not depth sampling conformance.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
static unsigned failures;
static bool check(const char* name, HRESULT result) {
    std::printf("%s %08lx\n", name, result);
    failures += FAILED(result);
    return SUCCEEDED(result);
}
static void expect(const char* name, bool pass) {
    std::printf("%s %s\n", name, pass ? "PASS" : "FAIL");
    failures += !pass;
}
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    HMODULE lib = LoadLibraryA("d3d9.dll");
    auto address = lib ? GetProcAddress(lib, "Direct3DCreate9") : nullptr;
    IDirect3D9*(WINAPI * create)(UINT) = nullptr;
    std::memcpy(&create, &address, sizeof create);
    IDirect3D9* factory = create ? create(D3D_SDK_VERSION) : nullptr;
    if (!factory) return 2;
    WNDCLASSA cls{};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "X3AutoDepthIntegration";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "X3 auto-depth loader verification", WS_OVERLAPPEDWINDOW, 80, 80,
                                160, 160, nullptr, nullptr, cls.hInstance, nullptr);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24X8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device = nullptr;
    if (!check("create auto-depth device",
               factory->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device)))
        return 2;
    for (unsigned round = 0; round < 2; ++round) {
        IDirect3DSurface9* depth = nullptr;
        if (check("logical auto depth", device->GetDepthStencilSurface(&depth))) {
            D3DSURFACE_DESC desc{};
            check("logical depth description", depth->GetDesc(&desc));
            expect("logical format stays D24X8", desc.Format == D3DFMT_D24X8);
            expect("logical dimensions follow reset",
                   desc.Width == pp.BackBufferWidth && desc.Height == pp.BackBufferHeight);
            depth->Release();
        }
        check("clear target and depth",
              device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff203040, .4f, 0));
        check("present", device->Present(nullptr, nullptr, nullptr, nullptr));
        if (!round) {
            pp.BackBufferWidth = 80;
            check("reset auto depth", device->Reset(&pp));
        }
    }
    expect("auto-depth device final zero", device->Release() == 0);
    expect("factory final zero", factory->Release() == 0);
    DestroyWindow(window);
    std::printf("AUTO DEPTH INTEGRATION RESULT failures=%u\n", failures);
    return failures ? 1 : 0;
}
