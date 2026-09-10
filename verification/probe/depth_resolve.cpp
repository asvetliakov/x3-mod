// Original RESZ depth-copy verification. Ordinary D24X8 remains the active source.
// No game DLLs or settings are copied or modified.
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

template<class T> struct Com {
    T* p = nullptr;
    ~Com() { if (p) p->Release(); }
    T* operator->() const { return p; }
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
};
template<class T> static T symbol(HMODULE module, const char* name) {
    FARPROC address = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(address));
    std::memcpy(&result, &address, sizeof(result));
    if (!result) throw std::runtime_error(name);
    return result;
}
static void check(const char* name, HRESULT result) {
    std::printf("%s: 0x%08lx %s\n", name, (unsigned long)result,
                SUCCEEDED(result) ? "OK" : "FAILED");
    if (FAILED(result)) throw std::runtime_error(name);
}
static void modulePath(const char* name) {
    char path[MAX_PATH] = {};
    HMODULE module = GetModuleHandleA(name);
    if (module) GetModuleFileNameA(module, path, sizeof(path));
    std::printf("module %s: %s\n", name, module ? path : "not loaded");
}
using Compiler = decltype(&D3DXCompileShader);
using Assembler = decltype(&D3DXAssembleShader);
static Assembler assembler=nullptr;
static bool shadowProbe=false;
static bool dummyDraw=true;
static DWORD resolveTrigger=0x7fa05000;
static float shadowReference=.1f;
static void compile(Compiler compiler, const char* source, const char* target,
                    ID3DXBuffer** result) {
    Com<ID3DXBuffer> errors;
    HRESULT hr = compiler(source, UINT(std::strlen(source)), nullptr, nullptr,
                          "main", target, D3DXSHADER_OPTIMIZATION_LEVEL3,
                          result, &errors.p, nullptr);
    if (errors.p) std::printf("compiler: %s\n", (char*)errors->GetBufferPointer());
    check(target, hr);
}
struct Vertex { float x, y, z, w, u, v; };
constexpr UINT size = 64;
static D3DFORMAT sourceFormat = D3DFMT_D24X8;
static D3DFORMAT destinationFormat = D3DFORMAT(MAKEFOURCC('I','N','T','Z'));
static Vertex vertex(float x, float y, float z, float u = 0, float v = 0) {
    // D3D9's pixel centers are integers; input coordinates denote pixel corners.
    return {2 * (x - .5f) / size - 1, 1 - 2 * (y - .5f) / size, z, 1, u, v};
}
static float halfToFloat(unsigned short h) {
    const unsigned exponent = (h >> 10) & 31;
    const float sign = (h & 0x8000) ? -1.f : 1.f;
    if (exponent == 31) return NAN;
    return sign * (exponent ? std::ldexp(float(1024 + (h & 1023)), int(exponent) - 25)
                            : std::ldexp(float(h & 1023), -24));
}

static void renderAndRead(IDirect3DDevice9* device, Compiler compiler,
                          D3DFORMAT format, unsigned generation) {
    const char* label = format == D3DFMT_A8R8G8B8 ? "RGBA8" :
                        format == D3DFMT_R32F ? "R32F" : "RGBA16F";
    std::printf("CASE generation=%u output=%s depth_source=%u depth_copy=%u samples=NONE shadow=%u reference=%.2f\n", generation, label, unsigned(sourceFormat), unsigned(destinationFormat),shadowProbe,shadowReference);
    Com<IDirect3DSurface9> backbuffer, depth, destinationDepth, color, readback;
    Com<IDirect3DTexture9> sentinel;
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
    if(shadowProbe) {
        // Original assembly supplies the third coordinate explicitly. HLSL tex2D
        // takes float2 and would discard a comparison reference before texld.
        const char* source="ps_3_0\ndef c1,1,0,0,0\ndcl_texcoord v0.xy\ndcl_2d s0\n"
            "mov r0.xy,v0\nmov r0.z,c0.x\ntexld r0,r0,s0\nmov r1.xyz,r0.x\nmov r1.w,c1.x\nmov oC0,r1\n";
        Com<ID3DXBuffer> errors;
        HRESULT hr=assembler(source,UINT(std::strlen(source)),nullptr,nullptr,0,&sampleCode.p,&errors.p);
        if(errors.p)std::printf("assembler: %s\n",static_cast<char*>(errors->GetBufferPointer()));
        check("Assemble original depth comparison PS",hr);
    } else compile(compiler,
        "sampler2D depth:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{"
        "float d=tex2D(depth,uv).r;return float4(d,d,d,1);}", "ps_3_0", &sampleCode.p);
    check("CreateVertexShader", device->CreateVertexShader((DWORD*)vsCode->GetBufferPointer(), &vs.p));
    check("CreatePixelShader solid", device->CreatePixelShader((DWORD*)solidCode->GetBufferPointer(), &solid.p));
    check("CreatePixelShader depth sampling", device->CreatePixelShader((DWORD*)sampleCode->GetBufferPointer(), &sample.p));
    const D3DVERTEXELEMENT9 elements[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END()
    };
    check("CreateVertexDeclaration", device->CreateVertexDeclaration(elements, &decl.p));
    check("GetBackBuffer", device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer.p));
    check("CreateTexture resolve destination", device->CreateTexture(size, size, 1, D3DUSAGE_DEPTHSTENCIL,
          destinationFormat, D3DPOOL_DEFAULT, &depthTexture.p, nullptr));
    check("GetSurfaceLevel resolve destination", depthTexture->GetSurfaceLevel(0, &destinationDepth.p));
    check("CreateDepthStencilSurface ordinary source", device->CreateDepthStencilSurface(size,size,sourceFormat,
          D3DMULTISAMPLE_NONE,0,FALSE,&depth.p,nullptr));
    check("CreateTexture sentinel", device->CreateTexture(2,2,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&sentinel.p,nullptr));
    check("CreateRenderTarget", device->CreateRenderTarget(size, size, format,
          D3DMULTISAMPLE_NONE, 0, FALSE, &color.p, nullptr));
    check("CreateOffscreenPlainSurface", device->CreateOffscreenPlainSurface(size, size, format,
          D3DPOOL_SYSTEMMEM, &readback.p, nullptr));
    check("SetRenderTarget", device->SetRenderTarget(0, color.p));
    // Poison the destination so a successful API no-op cannot look like a copy.
    check("Bind resolve destination for poison",device->SetDepthStencilSurface(destinationDepth.p));
    check("Clear resolve destination poison",device->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,.125f,0));
    check("SetDepthStencilSurface original source", device->SetDepthStencilSurface(depth.p));
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
    const Vertex triangles[] = {
        vertex(4,4,.25f), vertex(28,4,.25f), vertex(4,28,.25f),
        vertex(36,4,.75f), vertex(60,4,.75f), vertex(60,28,.75f),
        // Submitted after the near triangle: correct depth testing rejects it.
        vertex(4,4,.875f), vertex(28,4,.875f), vertex(4,28,.875f),
        vertex(4,36,.9999f), vertex(28,36,.9999f), vertex(4,60,.9999f),
        vertex(36,36,.99999f), vertex(60,36,.99999f), vertex(60,60,.99999f)
    };
    check("DrawPrimitiveUP five depth triangles", device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 5, triangles, sizeof(Vertex)));
    check("EndScene geometry", device->EndScene());
    // Trigger only: texture stage 0 is the destination; original D24X8 stays bound.
    check("Bind sentinel texture",device->SetTexture(0,sentinel.p));
    float pointSize=3.25f;DWORD originalPointBits=0;std::memcpy(&originalPointBits,&pointSize,4);
    check("Set original POINTSIZE",device->SetRenderState(D3DRS_POINTSIZE,originalPointBits));
    check("Bind RESZ destination texture",device->SetTexture(0,depthTexture.p));
    // AMD's contract requests a draw after binding to flush texture state.
    // It cannot change either the original depth contents or target color.
    if(dummyDraw) {
    check("Dummy color writes off",device->SetRenderState(D3DRS_COLORWRITEENABLE,0));
    check("Dummy depth writes off",device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
    check("BeginScene dummy",device->BeginScene());
    check("Dummy DrawPrimitiveUP flush",device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,triangles,sizeof(Vertex)));
    }
    // RESZ FourCC advertises capability; the POINTSIZE trigger uses a different value.
    std::printf("TRIGGER pointsize=%08lx\n",resolveTrigger);
    check("Trigger RESZ selected value",device->SetRenderState(D3DRS_POINTSIZE,resolveTrigger));
    if(dummyDraw) {
    check("EndScene dummy after RESZ",device->EndScene());
    check("Restore color writes",device->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    check("Restore depth writes",device->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));
    }
    check("Restore POINTSIZE",device->SetRenderState(D3DRS_POINTSIZE,originalPointBits));
    check("Restore sentinel texture",device->SetTexture(0,sentinel.p));
    Com<IDirect3DSurface9> observedDepth;
    Com<IDirect3DBaseTexture9> observedTexture;
    DWORD observedPointBits=0;
    check("GetDepthStencilSurface after RESZ",device->GetDepthStencilSurface(&observedDepth.p));
    check("GetTexture after restore",device->GetTexture(0,&observedTexture.p));
    check("GetRenderState POINTSIZE after restore",device->GetRenderState(D3DRS_POINTSIZE,&observedPointBits));
    D3DSURFACE_DESC observedDesc{};check("GetDesc original depth",observedDepth->GetDesc(&observedDesc));
    const bool stateOK=observedDepth.p==depth.p && observedTexture.p==sentinel.p &&
        observedPointBits==originalPointBits && observedDesc.Format==sourceFormat;
    std::printf("STATE generation=%u output=%s original_depth_identity=%u original_depth_format=%u "
                "texture_restored=%u pointsize_expected=%08lx pointsize_actual=%08lx %s\n",
                generation,label,observedDepth.p==depth.p,unsigned(observedDesc.Format),observedTexture.p==sentinel.p,
                originalPointBits,observedPointBits,stateOK?"PASS":"FAIL");
    if(!stateOK)throw std::runtime_error("RESZ state restoration");
    check("Unbind sentinel",device->SetTexture(0,nullptr));
    // If the resolve merely aliases the source, this clear destroys expected data.
    check("Clear original source after copy",device->Clear(0,nullptr,D3DCLEAR_ZBUFFER,0,.9375f,0));
    // Never sample an INTZ texture while it is the active depth surface.
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
    if(shadowProbe){float ref[4]={shadowReference,0,0,0};check("Set shadow reference",device->SetPixelShaderConstantF(0,ref,1));}
    const Vertex quad[] = {vertex(0,0,0,0,0), vertex(size,0,0,1,0),
                           vertex(0,size,0,0,1), vertex(size,size,0,1,1)};
    check("BeginScene sampling", device->BeginScene());
    check("DrawPrimitiveUP sample quad", device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(Vertex)));
    check("EndScene sampling", device->EndScene());
    check("Unbind depth texture", device->SetTexture(0, nullptr));
    check("GetRenderTargetData", device->GetRenderTargetData(color.p, readback.p));
    D3DLOCKED_RECT lock = {};
    check("LockRect", readback->LockRect(&lock, nullptr, D3DLOCK_READONLY));
    struct Point { UINT x, y; float expected; const char* name; };
    bool success = true;
    for (const auto& point : {Point{8,8,.25f,"near_with_far_rejected"},
                            Point{56,8,.75f,"far"}, Point{32,48,1,"clear"},
                            Point{24,24,1,"outside_near_triangle"},
                            Point{8,40,.9999f,"far_precision_9999"},
                            Point{56,40,.99999f,"far_precision_99999"}}) {
        const auto* pixel = (const unsigned char*)lock.pBits + point.y * lock.Pitch;
        float actual[4] = {};
        if (format == D3DFMT_A8R8G8B8) {
            pixel += point.x * 4;
            actual[0] = pixel[2] / 255.f; actual[1] = pixel[1] / 255.f;
            actual[2] = pixel[0] / 255.f; actual[3] = pixel[3] / 255.f;
        } else if(format == D3DFMT_R32F) {
            // R32F stores only red; replicate for the common scalar-depth check.
            std::memcpy(&actual[0],pixel+point.x*4,4);
            actual[1]=actual[2]=actual[0];actual[3]=1;
        } else {
            for (unsigned channel = 0; channel < 4; ++channel) {
                unsigned short half;
                std::memcpy(&half, pixel + point.x * 8 + channel * 2, 2);
                actual[channel] = halfToFloat(half);
            }
        }
        const float expectedSample=shadowProbe?(shadowReference<=point.expected?1.f:0.f):point.expected;
        const float tolerance = format == D3DFMT_A8R8G8B8 ? 1.5f/255 :
                                format == D3DFMT_R32F ? 1.2e-7f : .001f;
        bool pass = true;
        for (unsigned c = 0; c < 4; ++c)
            pass &= std::isfinite(actual[c]) && std::fabs(actual[c] - (c == 3 ? 1 : expectedSample)) <= tolerance;
        success &= pass;
        std::printf("SAMPLE generation=%u output=%s name=%s xy=%u,%u expected=%.9f "
                    "rgba=%.9f,%.9f,%.9f,%.9f tolerance=%.9f %s\n", generation, label,
                    point.name, point.x, point.y, expectedSample, actual[0], actual[1],
                    actual[2], actual[3], tolerance, pass ? "PASS" : "FAIL");
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
    std::puts("X3 original RESZ D24X8-to-INTZ copy probe; not game integration");
    WNDCLASSA cls = {};
    cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(nullptr);
    cls.lpszClassName = "X3DepthResolveProbe";
    RegisterClassA(&cls);
    HWND window = CreateWindowA(cls.lpszClassName, "X3 disposable depth resolve probe",
        WS_OVERLAPPEDWINDOW, 100, 100, 128, 128, nullptr, nullptr, cls.hInstance, nullptr);
    int result = 1;
    try {
        if (!window) throw std::runtime_error("CreateWindowA");
        if (argc < 2 || argc > 3) throw std::runtime_error("usage: depth_resolve.exe <D3DX path> [D24X8_INTZ|D24S8_INTZ|D24X8_DF24|D24X8_D24X8|D24X8_SHADOW|D24S8_FOURCC|D24X8_SHADOW_NODUMMY]");
        const char* variant=argc==3?argv[2]:"D24X8_INTZ";
        if (!std::strcmp(variant,"D24S8_INTZ")) sourceFormat=D3DFMT_D24S8;
        else if (!std::strcmp(variant,"D24X8_DF24")) destinationFormat=D3DFORMAT(MAKEFOURCC('D','F','2','4'));
        else if (!std::strcmp(variant,"D24X8_D24X8")) destinationFormat=D3DFMT_D24X8;
        else if (!std::strcmp(variant,"D24X8_SHADOW")) {destinationFormat=D3DFMT_D24X8;shadowProbe=true;}
        else if (!std::strcmp(variant,"D24S8_FOURCC")) {sourceFormat=D3DFMT_D24S8;resolveTrigger=MAKEFOURCC('R','E','S','Z');}
        else if (!std::strcmp(variant,"D24X8_SHADOW_NODUMMY")) {destinationFormat=D3DFMT_D24X8;shadowProbe=true;dummyDraw=false;}
        else if (std::strcmp(variant,"D24X8_INTZ")) throw std::runtime_error("unknown format variant");
        std::printf("VARIANT %s\n",variant);
        HMODULE d3dx = LoadLibraryA(argv[1]);
        if (!d3dx) throw std::runtime_error("LoadLibrary D3DX9_37");
        Compiler compiler = symbol<Compiler>(d3dx, "D3DXCompileShader");
        assembler=symbol<Assembler>(d3dx,"D3DXAssembleShader");
        HMODULE runtime = LoadLibraryA("d3d9.dll");
        auto create = symbol<IDirect3D9* (WINAPI*)(UINT)>(runtime, "Direct3DCreate9");
        Com<IDirect3D9> api;
        api.p = create(D3D_SDK_VERSION);
        if (!api.p) throw std::runtime_error("Direct3DCreate9");
        D3DDISPLAYMODE mode = {};
        check("GetAdapterDisplayMode", api->GetAdapterDisplayMode(0, &mode));
        for (D3DFORMAT color : {D3DFMT_A8R8G8B8, D3DFMT_A16B16G16R16F, D3DFMT_R32F}) {
            std::printf("MATCH color=%u\n", unsigned(color));
            check("CheckDepthStencilMatch original source", api->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL,
                  mode.Format, color, sourceFormat));
        }
        const HRESULT reszSupport=api->CheckDeviceFormat(0,D3DDEVTYPE_HAL,mode.Format,D3DUSAGE_RENDERTARGET,
              D3DRTYPE_SURFACE,D3DFORMAT(MAKEFOURCC('R','E','S','Z')));
        std::printf("CAP RESZ result=%08lx (numeric trigger result is authoritative)\n",reszSupport);
        D3DPRESENT_PARAMETERS pp = {};
        pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
        pp.BackBufferWidth = size; pp.BackBufferHeight = size; pp.BackBufferFormat = D3DFMT_A8R8G8B8;
        pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        const DWORD flags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE;
        std::printf("DEVICE flags=0x%08lx HARDWARE_VERTEXPROCESSING|PUREDEVICE\n", (unsigned long)flags);
        Com<IDirect3DDevice9> device;
        check("CreateDevice", api->CreateDevice(0, D3DDEVTYPE_HAL, window, flags, &pp, &device.p));
        for (unsigned generation = 0; generation < 2; ++generation) {
            for(unsigned ref=0;ref<(shadowProbe?3u:1u);++ref){
                shadowReference=ref==0?.1f:ref==1?.5f:.9f;
                for (D3DFORMAT color : {D3DFMT_A8R8G8B8, D3DFMT_A16B16G16R16F, D3DFMT_R32F})
                    renderAndRead(device.p, compiler, color, generation);
            }
            if (generation == 0) check("Reset after resource release", device->Reset(&pp));
        }
        for (const char* name : {"d3d9.dll", "wined3d.dll", "d3dx9_37.dll"}) modulePath(name);
        std::printf("RESULT PASS: %u copied depth checks after original clear, %u state preservation checks, Reset\n",shadowProbe?108:36,shadowProbe?18:6);
        result = 0;
    } catch (const std::exception& error) {
        std::printf("RESULT FAIL: %s\n", error.what());
    }
    if (window) DestroyWindow(window);
    return result;
}
