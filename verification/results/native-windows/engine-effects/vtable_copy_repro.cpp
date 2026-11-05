// Minimal native reproduction of the engine-effects fixture fault (docs/verification/engine-effects.md, "2026-11-05:
// native host"): the proxy's Hooks (src/proxy/capture.cpp) installs a private copy of the device's 119 documented
// IDirect3DDevice9 slots as the object's vtable pointer. Windows' d3d9.dll is a C++ implementation whose
// SetRenderTarget(0, surface) resets the viewport through its own private virtual slot 156 (this->vtbl[0x270]), read
// through the object's vtable pointer: through the short copy it reads past the end and calls whatever follows.
// Usage: vtable_copy_repro.exe original|copy|padded. original: the backend table (expected rc 0); copy: a 119-slot
// heap copy, the Hooks layout (expected c0000005); padded: the same 119 copied slots followed by 64 self-pointing
// sentinels, so the backend's call through slot 156 lands on a known address (expected c0000005 at SENTINEL).
// Build: i686-w64-mingw32-g++ -std=c++17 -O2 -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static
//        vtable_copy_repro.cpp -o vtable_copy_repro.exe -luser32 -ld3d9
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <vector>

static void* volatile sentinel_hit = nullptr;
static LONG WINAPI report(EXCEPTION_POINTERS* info) {
    std::printf("EXCEPTION code=%08lx eip=%08lx\n", info->ExceptionRecord->ExceptionCode, info->ContextRecord->Eip);
    std::fflush(stdout);
    ExitProcess(5);
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const char* mode = argc > 1 ? argv[1] : "copy";
    SetUnhandledExceptionFilter(report);
    WNDCLASSA cls{};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "x3m_vtable_copy_repro";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "repro", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr,
                                cls.hInstance, nullptr);
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return 2;
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.hDeviceWindow = window;
    IDirect3DDevice9* device = nullptr;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp,
                                   &device);
    std::printf("CreateDevice %08lx\n", hr);
    if (FAILED(hr)) return 3;
    IDirect3DSurface9* back = nullptr;
    hr = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back);
    std::printf("GetBackBuffer %08lx\n", hr);
    void** const original = *reinterpret_cast<void***>(device);
    std::vector<void*> table(original, original + 119); // the Hooks layout: documented slots only
    if (!std::strcmp(mode, "padded"))
        for (unsigned i = 0; i < 64; ++i) table.push_back(reinterpret_cast<void*>(0x5e000000u + i * 4u)); // SENTINEL
    if (std::strcmp(mode, "original")) *reinterpret_cast<void***>(device) = table.data();
    std::printf("MODE %s table=%p slots=%u\n", mode, static_cast<void*>(table.data()), unsigned(table.size()));
    hr = device->SetRenderTarget(0, back); // a call the proxy forwards unchanged; native resets the viewport here
    std::printf("SetRenderTarget %08lx\n", hr);
    *reinterpret_cast<void***>(device) = original;
    back->Release();
    device->Release();
    d3d->Release();
    std::printf("RESULT PASS\n");
    return 0;
}
