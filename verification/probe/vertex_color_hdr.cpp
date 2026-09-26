// Original standalone numeric interpolation probe. No game assets or rendering.
// COLOR0 and TEXCOORD0 carry identical FLOAT4 vertex data into an FP16 target.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

template <class T> struct Com {
    T* p = nullptr;
    ~Com() {
        if (p) p->Release();
    }
    T* operator->() const { return p; }
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
};
struct Module {
    HMODULE h;
    explicit Module(const char* path)
        : h(LoadLibraryA(path)) {
        if (!h) throw std::runtime_error("LoadLibrary");
        char resolved[MAX_PATH]{};
        GetModuleFileNameA(h, resolved, MAX_PATH);
        std::printf("MODULE requested=%s resolved=%s\n", path, resolved);
    }
    ~Module() { FreeLibrary(h); }
};
template <class T> T symbol(HMODULE module, const char* name) {
    FARPROC address = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    if (!result) throw std::runtime_error(name);
    return result;
}
void check(const char* name, HRESULT result) {
    std::printf("API %s result=%08lx\n", name, result);
    if (FAILED(result)) throw std::runtime_error(name);
}
using Compiler = decltype(&D3DXCompileShader);
void compile(Compiler compiler, const std::string& source, const char* target, ID3DXBuffer** code) {
    Com<ID3DXBuffer> errors;
    HRESULT hr = compiler(source.c_str(), UINT(source.size()), nullptr, nullptr, "main", target,
                          D3DXSHADER_OPTIMIZATION_LEVEL3, code, &errors.p, nullptr);
    if (errors.p) std::printf("COMPILER %s\n", static_cast<char*>(errors->GetBufferPointer()));
    check(target, hr);
}
float halfFloat(unsigned short h) {
    unsigned e = (h >> 10) & 31;
    float sign = h & 0x8000 ? -1.f : 1.f;
    if (e == 31) return NAN;
    return sign * (e ? std::ldexp(float(1024 + (h & 1023)), int(e) - 25) : std::ldexp(float(h & 1023), -24));
}
float saturate(float value) {
    return std::max(0.f, std::min(1.f, value));
}
constexpr UINT width = 64;
struct Vertex {
    float p[4];
    float value[4];
};
Vertex vertex(float x, float y, float red) {
    return {{2 * (x - .5f) / width - 1, 1 - 2 * (y - .5f) / width, .5f, 1}, {red, 4, 16, 1}};
}

// Return all interpolation models consistent with the numeric samples:
// 1=unclamped, 2=clamped at vertex output, 4=clamped after interpolation.
unsigned render(IDirect3DDevice9* device, Compiler compiler, unsigned generation, unsigned profile, bool color,
                bool psSaturate, bool gradient) {
    const char* semantic = color ? "COLOR0" : "TEXCOORD0";
    const char* vsProfile = profile == 3 ? "vs_3_0" : "vs_2_0";
    const char* psProfile = profile == 3 ? "ps_3_0" : "ps_2_0";
    Com<ID3DXBuffer> vc, pc;
    std::string vs = "struct O{float4 p:POSITION0;float4 v:" + std::string(semantic) +
                     ";};O main(float4 p:POSITION0,float4 v:TEXCOORD0){O o;o.p=p;o.v=v;return o;}";
    std::string ps = "float4 main(float4 v:" + std::string(semantic) + "):COLOR0{return " +
                     (psSaturate ? "saturate(v)" : "v") + ";}";
    compile(compiler, vs, vsProfile, &vc.p);
    compile(compiler, ps, psProfile, &pc.p);
    Com<IDirect3DVertexShader9> vshader;
    Com<IDirect3DPixelShader9> pshader;
    Com<IDirect3DVertexDeclaration9> declaration;
    Com<IDirect3DSurface9> backbuffer, target, readback;
    check("CreateVertexShader", device->CreateVertexShader(static_cast<DWORD*>(vc->GetBufferPointer()), &vshader.p));
    check("CreatePixelShader", device->CreatePixelShader(static_cast<DWORD*>(pc->GetBufferPointer()), &pshader.p));
    const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 16, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          D3DDECL_END()};
    check("CreateVertexDeclaration", device->CreateVertexDeclaration(elements, &declaration.p));
    check("GetBackBuffer", device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer.p));
    check("CreateRenderTarget FP16", device->CreateRenderTarget(width, width, D3DFMT_A16B16G16R16F, D3DMULTISAMPLE_NONE,
                                                                0, FALSE, &target.p, nullptr));
    check("CreateReadback FP16", device->CreateOffscreenPlainSurface(width, width, D3DFMT_A16B16G16R16F,
                                                                     D3DPOOL_SYSTEMMEM, &readback.p, nullptr));
    check("SetRenderTarget", device->SetRenderTarget(0, target.p));
    check("SetDepthStencilSurface null", device->SetDepthStencilSurface(nullptr));
    D3DVIEWPORT9 viewport = {0, 0, width, width, 0, 1};
    check("SetViewport", device->SetViewport(&viewport));
    check("SetVertexDeclaration", device->SetVertexDeclaration(declaration.p));
    check("SetVertexShader", device->SetVertexShader(vshader.p));
    check("SetPixelShader", device->SetPixelShader(pshader.p));
    check("ZENABLE false", device->SetRenderState(D3DRS_ZENABLE, FALSE));
    check("ZWRITE false", device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE));
    check("CULL none", device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    check("ALPHABLEND false", device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
    check("ALPHATEST false", device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE));
    check("SRGBWRITE false", device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE));
    check("FOGENABLE false", device->SetRenderState(D3DRS_FOGENABLE, FALSE));
    check("COLORWRITE all", device->SetRenderState(D3DRS_COLORWRITEENABLE, 15));
    check("Clear", device->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1, 0));
    Vertex triangle[] = {vertex(0, 0, .25f), vertex(width, 0, gradient ? 4.f : .25f),
                         vertex(0, width, gradient ? 16.f : .25f)};
    check("BeginScene", device->BeginScene());
    check("DrawPrimitiveUP", device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle, sizeof(Vertex)));
    check("EndScene", device->EndScene());
    check("GetRenderTargetData", device->GetRenderTargetData(target.p, readback.p));
    D3DLOCKED_RECT lock{};
    check("LockRect", readback->LockRect(&lock, nullptr, D3DLOCK_READONLY));
    unsigned models = color ? 7 : 1;
    const UINT points[][2] = {{8, 8}, {16, 8}, {8, 16}};
    for (const auto& point : points) {
        const float bx = (point[0] + .5f) / width, by = (point[1] + .5f) / width;
        const float red = gradient ? .25f + (4 - .25f) * bx + (16 - .25f) * by : .25f;
        float expected[3][4] = {
            {red, 4, 16, 1}, {gradient ? .25f + .75f * (bx + by) : .25f, 1, 1, 1}, {saturate(red), 1, 1, 1}};
        float actual[4]{};
        const auto* row = static_cast<unsigned char*>(lock.pBits) + point[1] * lock.Pitch;
        for (unsigned channel = 0; channel < 4; ++channel) {
            unsigned short half;
            std::memcpy(&half, row + point[0] * 8 + channel * 2, 2);
            actual[channel] = halfFloat(half);
        }
        unsigned matching = 0;
        for (unsigned model = 0; model < 3; ++model) {
            bool match = true;
            for (unsigned channel = 0; channel < 4; ++channel) {
                float want = psSaturate ? saturate(expected[model][channel]) : expected[model][channel];
                const float tolerance = std::max(.002f, std::fabs(want) * .0015f);
                match &= std::isfinite(actual[channel]) && std::fabs(actual[channel] - want) <= tolerance;
            }
            if (match) matching |= 1u << model;
        }
        models &= matching;
        std::printf("SAMPLE generation=%u profile=%u semantic=%s ps_saturate=%u gradient=%u xy=%u,%u "
                    "unclamped_red=%.8f rgba=%.8f,%.8f,%.8f,%.8f matching_models=%u %s\n",
                    generation, profile, semantic, psSaturate, gradient, point[0], point[1], red, actual[0], actual[1],
                    actual[2], actual[3], matching, (matching & (color ? 7 : 1)) ? "PASS" : "FAIL");
    }
    check("UnlockRect", readback->UnlockRect());
    check("RestoreBackbuffer", device->SetRenderTarget(0, backbuffer.p));
    check("UnbindVertexShader", device->SetVertexShader(nullptr));
    check("UnbindPixelShader", device->SetPixelShader(nullptr));
    check("UnbindDeclaration", device->SetVertexDeclaration(nullptr));
    std::printf("CASE generation=%u profile=%u semantic=%s ps_saturate=%u gradient=%u consistent_models=%u\n",
                generation, profile, semantic, psSaturate, gradient, models);
    if (!models) throw std::runtime_error("no consistent numeric model");
    return models;
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    WNDCLASSA cls{};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "X3VertexColorProbe";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "X3 original interpolation probe", WS_OVERLAPPEDWINDOW, 100, 100,
                                128, 128, nullptr, nullptr, cls.hInstance, nullptr);
    int result = 1;
    try {
        if (argc != 2 || !window) throw std::runtime_error("expected absolute D3DX path and probe window");
        Module d3dx(argv[1]), runtime("d3d9.dll");
        Compiler compiler = symbol<Compiler>(d3dx.h, "D3DXCompileShader");
        auto create = symbol<IDirect3D9*(WINAPI*)(UINT)>(runtime.h, "Direct3DCreate9");
        Com<IDirect3D9> api;
        api.p = create(D3D_SDK_VERSION);
        if (!api.p) throw std::runtime_error("Direct3DCreate9");
        D3DADAPTER_IDENTIFIER9 adapter{};
        check("GetAdapterIdentifier", api->GetAdapterIdentifier(0, 0, &adapter));
        std::printf("ADAPTER description=%s driver=%s\n", adapter.Description, adapter.Driver);
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.BackBufferWidth = width;
        pp.BackBufferHeight = width;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;
        check("CreateDevice",
              api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE,
                                &pp, &device.p));
        for (unsigned generation = 0; generation < 2; ++generation) {
            for (unsigned profile : {2u, 3u})
                for (bool color : {false, true})
                    for (bool clamp : {false, true})
                        for (bool gradient : {false, true})
                            render(device.p, compiler, generation, profile, color, clamp, gradient);
            if (generation == 0) check("Reset after resource release", device->Reset(&pp));
        }
        char path[MAX_PATH]{};
        HMODULE wine = GetModuleHandleA("wined3d.dll");
        if (wine) GetModuleFileNameA(wine, path, MAX_PATH);
        std::printf("BACKEND wined3d=%s\n", wine ? path : "not loaded");
        std::puts("RESULT PASS: 96 numeric samples, 32 cases, two generations with Reset");
        result = 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL: %s\n", e.what());
    }
    if (window) DestroyWindow(window);
    UnregisterClassA(cls.lpszClassName, cls.hInstance);
    return result;
}
