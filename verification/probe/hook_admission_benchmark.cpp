// Standalone original workload against a retained/current proxy or system D3D9.
// No draws, Presents, copies or readbacks. Timings include the real backend call.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <cstdint>
namespace {
constexpr unsigned iterations = 20000, trials = 7;
unsigned checks = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) throw std::runtime_error(label);
}
void ok(HRESULT hr, const char* label) {
    check(hr == S_OK, label);
}
template <class T> struct Com {
    T* p = nullptr;
    ~Com() {
        if (p) p->Release();
    }
    T* operator->() const { return p; }
};
using Create = IDirect3D9*(WINAPI*)(UINT);
LONGLONG ticks() {
    LARGE_INTEGER t{};
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}
enum class Route { GetState, SetState, InvalidTarget, InvalidClear };
const char* name(Route r) {
    switch (r) {
    case Route::GetState: return "get_render_state";
    case Route::SetState: return "set_render_state";
    case Route::InvalidTarget: return "invalid_set_rt";
    case Route::InvalidClear: return "invalid_clear";
    }
    return "invalid";
}
void batch(IDirect3DDevice9* device, Route route, unsigned sample, double frequency, bool output) {
    DWORD known = 0;
    ok(device->GetRenderState(D3DRS_ALPHAREF, &known), "prebatch state");
    const auto begin = ticks();
    for (unsigned i = 0; i < iterations; ++i) {
        HRESULT hr;
        switch (route) {
        case Route::GetState: {
            DWORD value = 0xdeadbeef;
            hr = device->GetRenderState(D3DRS_ALPHAREF, &value);
            check(hr == S_OK && value == known, "GetRenderState HRESULT/output");
            break;
        }
        case Route::SetState:
            hr = device->SetRenderState(D3DRS_ALPHAREF, i & 255);
            check(hr == S_OK, "SetRenderState HRESULT");
            break;
        case Route::InvalidTarget:
            hr = device->SetRenderTarget(0, nullptr);
            check(hr == D3DERR_INVALIDCALL, "SetRenderTarget rejected null RT0");
            break;
        case Route::InvalidClear:
            hr = device->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.0f, 0);
            check(hr == D3DERR_INVALIDCALL, "Clear rejected absent depth");
            break;
        }
    }
    const auto elapsed = ticks() - begin;
    DWORD after = 0;
    ok(device->GetRenderState(D3DRS_ALPHAREF, &after), "postbatch state");
    check(after == (route == Route::SetState ? ((iterations - 1) & 255) : known),
          "state output validated after timed batch");
    if (output)
        std::printf("SAMPLE route=%s iteration=%u calls=%u elapsed_ticks=%lld total_us=%.6f ns_per_call=%.6f\n",
                    name(route), sample, iterations, static_cast<long long>(elapsed), double(elapsed) * 1e6 / frequency,
                    double(elapsed) * 1e9 / frequency / iterations);
}
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        const bool native = argc == 2 && !std::strcmp(argv[1], "native");
        check(argc == 2 && (native || !std::strcmp(argv[1], "proxy")), "mode");
        HMODULE module = LoadLibraryA(native ? "C:\\windows\\system32\\d3d9.dll" : "d3d9.dll");
        check(module != nullptr, "load D3D9");
        const auto entry = GetProcAddress(module, "Direct3DCreate9");
        Create create = nullptr;
        std::memcpy(&create, &entry, sizeof create);
        check(create != nullptr, "D3D9 entry");
        char module_path[2048]{};
        check(GetModuleFileNameA(module, module_path, sizeof module_path) != 0, "module path");
        std::printf("MODULE %s\n", module_path);
        HWND window = CreateWindowExA(0, "STATIC", "hook admission benchmark", WS_OVERLAPPEDWINDOW, 0, 0, 32, 32,
                                      nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
        check(window != nullptr, "window");
        LARGE_INTEGER frequency{};
        check(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0, "QPC frequency");
        std::printf("BENCHMARK frequency=%lld iterations=%u trials=%u routes=4 draws=0 presents=0\n",
                    static_cast<long long>(frequency.QuadPart), iterations, trials);
        {
            Com<IDirect3D9> factory;
            factory.p = create(D3D_SDK_VERSION);
            check(factory.p != nullptr, "factory");
            Com<IDirect3DDevice9> device;
            D3DPRESENT_PARAMETERS pp{};
            pp.Windowed = TRUE;
            pp.hDeviceWindow = window;
            pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            pp.BackBufferWidth = pp.BackBufferHeight = 16;
            pp.BackBufferFormat = D3DFMT_A8R8G8B8;
            pp.EnableAutoDepthStencil = FALSE;
            pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            ok(factory->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p),
               "device");
            ok(device->SetRenderState(D3DRS_ALPHAREF, 0x5a), "seed state");
            Com<IDirect3DSurface9> initial_target;
            ok(device->GetRenderTarget(0, &initial_target.p), "initial target");
            Com<IDirect3DSurface9> depth;
            check(device->GetDepthStencilSurface(&depth.p) == D3DERR_NOTFOUND && !depth.p,
                  "device has no depth surface");
            for (auto route : {Route::GetState, Route::SetState, Route::InvalidTarget, Route::InvalidClear}) {
                batch(device.p, route, 0, double(frequency.QuadPart), false);
                for (unsigned sample = 0; sample < trials; ++sample)
                    batch(device.p, route, sample, double(frequency.QuadPart), true);
                Com<IDirect3DSurface9> target;
                ok(device->GetRenderTarget(0, &target.p), "postroute target");
                check(target.p == initial_target.p, "invalid calls preserve render target identity");
            }
        }
        DestroyWindow(window);
        FreeLibrary(module);
        std::printf("RESULT PASS checks=%u samples=28 calls_per_sample=%u draws=0 presents=0\n", checks, iterations);
        return 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL %s checks=%u\n", e.what(), checks);
        return 1;
    }
}
