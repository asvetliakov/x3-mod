// Independent synthetic depth verification. No game DLLs are copied or modified.
// The local D3DX compiler compiles only the small original shaders below.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <initializer_list>

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
template <class T> static T symbol(HMODULE module, const char* name) {
    FARPROC address = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    if (!result) throw std::runtime_error(name);
    return result;
}
static void check(const char* name, HRESULT result) {
    std::printf("%s: 0x%08lx %s\n", name, (unsigned long)result, SUCCEEDED(result) ? "OK" : "FAILED");
    if (FAILED(result)) throw std::runtime_error(name);
}
static void modulePath(const char* name) {
    char path[MAX_PATH] = {};
    HMODULE module = GetModuleHandleA(name);
    if (module) GetModuleFileNameA(module, path, sizeof(path));
    std::printf("module %s: %s\n", name, module ? path : "not loaded");
}
using Compiler = decltype(&D3DXCompileShader);
static void compile(Compiler compiler, const char* source, const char* target, ID3DXBuffer** result) {
    Com<ID3DXBuffer> errors;
    HRESULT hr = compiler(source, UINT(std::strlen(source)), nullptr, nullptr, "main", target,
                          D3DXSHADER_OPTIMIZATION_LEVEL3, result, &errors.p, nullptr);
    if (errors.p) std::printf("compiler: %s\n", (char*)errors->GetBufferPointer());
    check(target, hr);
}
struct Vertex {
    float x, y, z, w, u, v;
};
constexpr UINT size = 64;
static Vertex vertex(float x, float y, float z, float u = 0, float v = 0) {
    // D3D9's pixel centers are integers; input coordinates denote pixel corners.
    return {2 * (x - .5f) / size - 1, 1 - 2 * (y - .5f) / size, z, 1, u, v};
}
static float halfToFloat(unsigned short h) {
    const unsigned exponent = (h >> 10) & 31;
    const float sign = (h & 0x8000) ? -1.f : 1.f;
    if (exponent == 31) return NAN;
    return sign *
           (exponent ? std::ldexp(float(1024 + (h & 1023)), int(exponent) - 25) : std::ldexp(float(h & 1023), -24));
}

static void renderAndRead(IDirect3DDevice9* device, Compiler compiler, D3DFORMAT format, unsigned generation) {
    const char* label = format == D3DFMT_A8R8G8B8 ? "RGBA8" : "RGBA16F";
    std::printf("CASE generation=%u output=%s depth=INTZ samples=NONE\n", generation, label);
    Com<IDirect3DSurface9> backbuffer, depth, color, readback;
    Com<IDirect3DTexture9> depthTexture;
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> solid, sample;
    Com<IDirect3DVertexDeclaration9> decl;
    Com<ID3DXBuffer> vsCode, solidCode, sampleCode;
    compile(compiler,
            "void main(float4 p:POSITION0,float2 uv:TEXCOORD0,"
            "out float4 op:POSITION0,out float2 ou:TEXCOORD0){op=p;ou=uv;}",
            "vs_3_0", &vsCode.p);
    compile(compiler, "float4 main():COLOR0{return float4(1,0,1,1);}", "ps_3_0", &solidCode.p);
    compile(compiler,
            "sampler2D depth:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{"
            "float d=tex2D(depth,uv).r;return float4(d,d,d,1);}",
            "ps_3_0", &sampleCode.p);
    check("CreateVertexShader", device->CreateVertexShader((DWORD*)vsCode->GetBufferPointer(), &vs.p));
    check("CreatePixelShader solid", device->CreatePixelShader((DWORD*)solidCode->GetBufferPointer(), &solid.p));
    check("CreatePixelShader depth sampling",
          device->CreatePixelShader((DWORD*)sampleCode->GetBufferPointer(), &sample.p));
    const D3DVERTEXELEMENT9 elements[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                          {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
                                          D3DDECL_END()};
    check("CreateVertexDeclaration", device->CreateVertexDeclaration(elements, &decl.p));
    check("GetBackBuffer", device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer.p));
    check("CreateTexture INTZ",
          device->CreateTexture(size, size, 1, D3DUSAGE_DEPTHSTENCIL, D3DFORMAT(MAKEFOURCC('I', 'N', 'T', 'Z')),
                                D3DPOOL_DEFAULT, &depthTexture.p, nullptr));
    check("GetSurfaceLevel", depthTexture->GetSurfaceLevel(0, &depth.p));
    check("CreateRenderTarget",
          device->CreateRenderTarget(size, size, format, D3DMULTISAMPLE_NONE, 0, FALSE, &color.p, nullptr));
    check("CreateOffscreenPlainSurface",
          device->CreateOffscreenPlainSurface(size, size, format, D3DPOOL_SYSTEMMEM, &readback.p, nullptr));
    check("SetRenderTarget", device->SetRenderTarget(0, color.p));
    check("SetDepthStencilSurface INTZ", device->SetDepthStencilSurface(depth.p));
    D3DVIEWPORT9 viewport = {0, 0, size, size, 0, 1};
    check("SetViewport", device->SetViewport(&viewport));
    check("SetVertexDeclaration", device->SetVertexDeclaration(decl.p));
    check("SetVertexShader", device->SetVertexShader(vs.p));
    check("SetPixelShader solid", device->SetPixelShader(solid.p));
    check("CULL_NONE", device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    check("ZENABLE", device->SetRenderState(D3DRS_ZENABLE, TRUE));
    check("ZWRITEENABLE", device->SetRenderState(D3DRS_ZWRITEENABLE, TRUE));
    check("ZFUNC LESS", device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS));
    check("ALPHABLENDENABLE false", device->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
    check("SRGBWRITEENABLE false", device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE));
    check("Clear", device->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff000000, 1, 0));
    check("BeginScene geometry", device->BeginScene());
    const Vertex triangles[] = {vertex(4, 4, .25f), vertex(28, 4, .25f), vertex(4, 28, .25f), vertex(36, 4, .75f),
                                vertex(60, 4, .75f), vertex(60, 28, .75f),
                                // Submitted after the near triangle: correct depth testing rejects it.
                                vertex(4, 4, .875f), vertex(28, 4, .875f), vertex(4, 28, .875f)};
    check("DrawPrimitiveUP three depth triangles",
          device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 3, triangles, sizeof(Vertex)));
    check("EndScene geometry", device->EndScene());
    // Never bind the texture for sampling while it remains the active depth target.
    check("Unbind depth", device->SetDepthStencilSurface(nullptr));
    check("Disable depth", device->SetRenderState(D3DRS_ZENABLE, FALSE));
    check("Disable depth writes", device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE));
    check("SetTexture INTZ", device->SetTexture(0, depthTexture.p));
    check("MINFILTER POINT", device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT));
    check("MAGFILTER POINT", device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT));
    check("MIPFILTER NONE", device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
    check("ADDRESSU CLAMP", device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
    check("ADDRESSV CLAMP", device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
    check("SRGBTEXTURE false", device->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE));
    check("SetPixelShader sampling", device->SetPixelShader(sample.p));
    const Vertex quad[] = {vertex(0, 0, 0, 0, 0), vertex(size, 0, 0, 1, 0), vertex(0, size, 0, 0, 1),
                           vertex(size, size, 0, 1, 1)};
    check("BeginScene sampling", device->BeginScene());
    check("DrawPrimitiveUP sample quad", device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)));
    check("EndScene sampling", device->EndScene());
    check("Unbind depth texture", device->SetTexture(0, nullptr));
    check("GetRenderTargetData", device->GetRenderTargetData(color.p, readback.p));
    D3DLOCKED_RECT lock = {};
    check("LockRect", readback->LockRect(&lock, nullptr, D3DLOCK_READONLY));
    struct Point {
        UINT x, y;
        float expected;
        const char* name;
    };
    bool success = true;
    for (const auto& point : {Point{8, 8, .25f, "near_with_far_rejected"}, Point{56, 8, .75f, "far"},
                              Point{32, 48, 1, "clear"}, Point{24, 24, 1, "outside_near_triangle"}}) {
        const auto* pixel = (const unsigned char*)lock.pBits + point.y * lock.Pitch;
        float actual[4] = {};
        if (format == D3DFMT_A8R8G8B8) {
            pixel += point.x * 4;
            actual[0] = pixel[2] / 255.f;
            actual[1] = pixel[1] / 255.f;
            actual[2] = pixel[0] / 255.f;
            actual[3] = pixel[3] / 255.f;
        } else {
            for (unsigned channel = 0; channel < 4; ++channel) {
                unsigned short half;
                std::memcpy(&half, pixel + point.x * 8 + channel * 2, 2);
                actual[channel] = halfToFloat(half);
            }
        }
        const float tolerance = format == D3DFMT_A8R8G8B8 ? 1.5f / 255 : .001f;
        bool pass = true;
        for (unsigned c = 0; c < 4; ++c)
            pass &= std::isfinite(actual[c]) && std::fabs(actual[c] - (c == 3 ? 1 : point.expected)) <= tolerance;
        success &= pass;
        std::printf("SAMPLE generation=%u output=%s name=%s xy=%u,%u expected=%.6f "
                    "rgba=%.6f,%.6f,%.6f,%.6f tolerance=%.6f %s\n",
                    generation, label, point.name, point.x, point.y, point.expected, actual[0], actual[1], actual[2],
                    actual[3], tolerance, pass ? "PASS" : "FAIL");
    }
    check("UnlockRect", readback->UnlockRect());
    check("Restore backbuffer", device->SetRenderTarget(0, backbuffer.p));
    check("Unbind vertex shader", device->SetVertexShader(nullptr));
    check("Unbind pixel shader", device->SetPixelShader(nullptr));
    check("Unbind declaration", device->SetVertexDeclaration(nullptr));
    if (!success) throw std::runtime_error("numeric sampled depth mismatch");
}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::puts("X3 synthetic INTZ sampling probe; not game integration or TAA");
    WNDCLASSA cls = {};
    cls.lpfnWndProc = DefWindowProcA;
    cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "X3DepthSamplingProbe";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "X3 disposable depth probe", WS_OVERLAPPEDWINDOW, 100, 100, 128, 128,
                                nullptr, nullptr, cls.hInstance, nullptr);
    int result = 1;
    try {
        if (!window) throw std::runtime_error("CreateWindowA");
        if (argc != 2) throw std::runtime_error("usage: depth_sampling.exe <absolute D3DX9_37.dll path>");
        HMODULE d3dx = LoadLibraryA(argv[1]);
        if (!d3dx) throw std::runtime_error("LoadLibrary D3DX9_37");
        Compiler compiler = symbol<Compiler>(d3dx, "D3DXCompileShader");
        HMODULE runtime = LoadLibraryA("d3d9.dll");
        auto create = symbol<IDirect3D9*(WINAPI*)(UINT)>(runtime, "Direct3DCreate9");
        Com<IDirect3D9> api;
        api.p = create(D3D_SDK_VERSION);
        if (!api.p) throw std::runtime_error("Direct3DCreate9");
        D3DDISPLAYMODE mode = {};
        check("GetAdapterDisplayMode", api->GetAdapterDisplayMode(0, &mode));
        for (D3DFORMAT color : {D3DFMT_A8R8G8B8, D3DFMT_A16B16G16R16F}) {
            std::printf("MATCH color=%u\n", unsigned(color));
            check("CheckDepthStencilMatch INTZ",
                  api->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, mode.Format, color,
                                              D3DFORMAT(MAKEFOURCC('I', 'N', 'T', 'Z'))));
        }
        D3DPRESENT_PARAMETERS pp = {};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.hDeviceWindow = window;
        pp.BackBufferWidth = size;
        pp.BackBufferHeight = size;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        const DWORD flags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE;
        std::printf("DEVICE flags=0x%08lx HARDWARE_VERTEXPROCESSING|PUREDEVICE\n", (unsigned long)flags);
        Com<IDirect3DDevice9> device;
        check("CreateDevice", api->CreateDevice(0, D3DDEVTYPE_HAL, window, flags, &pp, &device.p));
        for (unsigned generation = 0; generation < 2; ++generation) {
            for (D3DFORMAT color : {D3DFMT_A8R8G8B8, D3DFMT_A16B16G16R16F})
                renderAndRead(device.p, compiler, color, generation);
            if (generation == 0) check("Reset after resource release", device->Reset(&pp));
        }
        for (const char* name : {"d3d9.dll", "wined3d.dll", "d3dx9_37.dll"}) modulePath(name);
        std::puts("RESULT PASS: 16 depth/coverage checks including resource recreation after Reset");
        result = 0;
    } catch (const std::exception& error) {
        std::printf("RESULT FAIL: %s\n", error.what());
    }
    if (window) DestroyWindow(window);
    return result;
}
