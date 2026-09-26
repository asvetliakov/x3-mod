// Original synthetic scene: verifies the production non-substituting depth copy.
// No X3 assets, game launch, installation or display output is involved.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/ownership/d3d9_ownership.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <array>
#include <fstream>
#include <sstream>
#include <string>

namespace own = x3m::ownership;
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { reset(); }
    void reset() {
        if (p) {
            p->Release();
            p = nullptr;
        }
    }
    T* operator->() const { return p; }
};
static unsigned checks, samples;
static void expect(const char* label, bool pass) {
    ++checks;
    std::printf("CHECK %s %s\n", label, pass ? "PASS" : "FAIL");
    if (!pass) throw std::runtime_error(label);
}
static void ok(const char* label, HRESULT hr) {
    std::printf("API %s result=%08lx\n", label, static_cast<unsigned long>(hr));
    expect(label, SUCCEEDED(hr));
}
template <class T> static T entry(HMODULE module, const char* name) {
    FARPROC address = GetProcAddress(module, name);
    T fn = nullptr;
    static_assert(sizeof(fn) == sizeof(address));
    std::memcpy(&fn, &address, sizeof(fn));
    if (!fn) throw std::runtime_error(name);
    return fn;
}
static std::string decode_source;
static own::CopyDepthView copy_view(IDirect3DDevice9* device) {
    own::CopyDepthView view{};
    ok("copy view", own::get_copy_depth_view(device, &view));
    return view;
}
using Compiler = decltype(&D3DXCompileShader);
static void compile(Compiler compiler, const char* source, const char* profile, ID3DXBuffer** out) {
    Com<ID3DXBuffer> errors;
    HRESULT hr = compiler(source, UINT(std::strlen(source)), nullptr, nullptr, "main", profile,
                          D3DXSHADER_OPTIMIZATION_LEVEL3, out, &errors.p, nullptr);
    if (errors.p) std::printf("compiler %s\n", static_cast<char*>(errors->GetBufferPointer()));
    ok(profile, hr);
}
struct Geometry {
    float x, y, z, rhw;
    D3DCOLOR color;
};
static void draw_scene(IDirect3DDevice9* d) {
    ok("geometry no VS", d->SetVertexShader(nullptr));
    ok("geometry no PS", d->SetPixelShader(nullptr));
    ok("geometry FVF", d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE));
    ok("geometry no texture", d->SetTexture(0, nullptr));
    ok("geometry diffuse", d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1));
    ok("geometry diffuse arg", d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
    ok("geometry Z", d->SetRenderState(D3DRS_ZENABLE, TRUE));
    ok("geometry write Z", d->SetRenderState(D3DRS_ZWRITEENABLE, TRUE));
    ok("geometry less", d->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS));
    ok("geometry cull", d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
    const Geometry tri[] = {
        {3.5f, 3.5f, .25f, 1, 0xffff00ff},  {27.5f, 3.5f, .25f, 1, 0xffff00ff},  {3.5f, 27.5f, .25f, 1, 0xffff00ff},
        {35.5f, 3.5f, .75f, 1, 0xff00ffff}, {59.5f, 3.5f, .75f, 1, 0xff00ffff},  {59.5f, 27.5f, .75f, 1, 0xff00ffff},
        {3.5f, 3.5f, .875f, 1, 0xffffffff}, {27.5f, 3.5f, .875f, 1, 0xffffffff}, {3.5f, 27.5f, .875f, 1, 0xffffffff}};
    ok("geometry begin", d->BeginScene());
    ok("geometry draw", d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 3, tri, sizeof(Geometry)));
    ok("geometry end", d->EndScene());
}
// This bounded fixture copy models consumption before X3's destructive clear.
// Production owns a comparison texture; this uses the production decoder to R32F.
struct Snapshot {
    Com<IDirect3DSurface9> color, readback;
    UINT width, height;
    Snapshot(IDirect3DDevice9* d, UINT w, UINT h)
        : width(w)
        , height(h) {
        ok("snapshot target",
           d->CreateRenderTarget(w, h, D3DFMT_R32F, D3DMULTISAMPLE_NONE, 0, FALSE, &color.p, nullptr));
        ok("snapshot readback",
           d->CreateOffscreenPlainSurface(w, h, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &readback.p, nullptr));
    }
    void capture(IDirect3DDevice9* d, IDirect3DTexture9* texture, Compiler compiler) {
        Com<IDirect3DStateBlock9> state;
        Com<IDirect3DSurface9> rt, ds;
        D3DVIEWPORT9 vp{};
        ok("save RT", d->GetRenderTarget(0, &rt.p));
        ok("save depth", d->GetDepthStencilSurface(&ds.p));
        ok("save viewport", d->GetViewport(&vp));
        ok("save state", d->CreateStateBlock(D3DSBT_ALL, &state.p));
        Com<ID3DXBuffer> vc, pc;
        Com<IDirect3DVertexShader9> vs;
        Com<IDirect3DPixelShader9> ps;
        Com<IDirect3DVertexDeclaration9> decl;
        compile(
            compiler,
            "void main(float4 p:POSITION0,float2 t:TEXCOORD0,out float4 o:POSITION0,out float2 u:TEXCOORD0){o=p;u=t;}",
            "vs_3_0", &vc.p);
        compile(compiler, decode_source.c_str(), "ps_3_0", &pc.p);
        ok("snapshot VS", d->CreateVertexShader(static_cast<DWORD*>(vc->GetBufferPointer()), &vs.p));
        ok("snapshot PS", d->CreatePixelShader(static_cast<DWORD*>(pc->GetBufferPointer()), &ps.p));
        const D3DVERTEXELEMENT9 elements[] = {
            {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            D3DDECL_END()};
        ok("snapshot decl", d->CreateVertexDeclaration(elements, &decl.p));
        ok("snapshot unbind depth", d->SetDepthStencilSurface(nullptr));
        ok("snapshot RT", d->SetRenderTarget(0, color.p));
        D3DVIEWPORT9 full = {0, 0, width, height, 0, 1};
        ok("snapshot viewport", d->SetViewport(&full));
        ok("snapshot bind VS", d->SetVertexShader(vs.p));
        ok("snapshot bind PS", d->SetPixelShader(ps.p));
        ok("snapshot bind decl", d->SetVertexDeclaration(decl.p));
        const D3DRENDERSTATETYPE disabled[] = {D3DRS_ZENABLE,           D3DRS_ZWRITEENABLE,    D3DRS_ALPHABLENDENABLE,
                                               D3DRS_ALPHATESTENABLE,   D3DRS_FOGENABLE,       D3DRS_STENCILENABLE,
                                               D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE, D3DRS_CLIPPLANEENABLE};
        for (auto s : disabled) ok("snapshot disable state", d->SetRenderState(s, 0));
        ok("snapshot cull", d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
        ok("snapshot fill", d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID));
        ok("snapshot color write", d->SetRenderState(D3DRS_COLORWRITEENABLE, 15));
        ok("snapshot texture", d->SetTexture(0, texture));
        ok("snapshot point min", d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT));
        ok("snapshot point mag", d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT));
        ok("snapshot no mip", d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
        ok("snapshot clamp U", d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
        ok("snapshot clamp V", d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
        ok("snapshot no srgb", d->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, FALSE));
        struct V {
            float x, y, z, w, u, v;
        };
        const float l = -1.f - 1.f / width, r = 1.f - 1.f / width, t = 1.f + 1.f / height, b = -1.f + 1.f / height;
        const V quad[] = {{l, t, 0, 1, 0, 0}, {r, t, 0, 1, 1, 0}, {l, b, 0, 1, 0, 1}, {r, b, 0, 1, 1, 1}};
        ok("snapshot begin", d->BeginScene());
        ok("snapshot draw", d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(V)));
        ok("snapshot end", d->EndScene());
        ok("snapshot unbind texture", d->SetTexture(0, nullptr));
        ok("restore RT", d->SetRenderTarget(0, rt.p));
        ok("restore depth", d->SetDepthStencilSurface(ds.p));
        ok("restore state", state->Apply());
        ok("restore viewport", d->SetViewport(&vp));
    }
    void verify(IDirect3DDevice9* d, const char* epoch, bool cleared) {
        ok("read snapshot", d->GetRenderTargetData(color.p, readback.p));
        D3DLOCKED_RECT lr{};
        ok("lock snapshot", readback->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        struct P {
            UINT x, y;
            float depth;
        };
        const P points[] = {{8, 8, .25f}, {56, 8, .75f}, {32, 40, 1}, {24, 24, 1}};
        bool pass = true;
        for (const auto& p : points) {
            float actual;
            std::memcpy(&actual, static_cast<const char*>(lr.pBits) + p.y * lr.Pitch + p.x * 4, 4);
            const float expected = cleared ? 1 : p.depth;
            const bool valid = std::isfinite(actual) && std::fabs(actual - expected) <= 1.3e-7f;
            pass &= valid;
            ++samples;
            std::printf("SAMPLE epoch=%s xy=%u,%u expected=%.6f actual=%.6f %s\n", epoch, p.x, p.y, expected, actual,
                        valid ? "PASS" : "FAIL");
        }
        ok("unlock snapshot", readback->UnlockRect());
        expect(epoch, pass);
    }
};

static void exercise_copy(IDirect3D9* api, HWND window, Compiler compiler, DWORD flags) {
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24X8;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    Com<IDirect3DDevice9> app;
    ok("create ordinary auto-depth device", api->CreateDevice(0, D3DDEVTYPE_HAL, window, flags, &pp, &app.p));
    auto* native = own::borrowed_native_device(app.p);
    expect("native seam", native != nullptr);
    std::uint64_t previous_generation = 0;
    for (unsigned generation = 0; generation < 2; ++generation) {
        std::printf("CASE copy flags=%08lx generation=%u size=%ux%u\n", static_cast<unsigned long>(flags), generation,
                    pp.BackBufferWidth, pp.BackBufferHeight);
        auto view = copy_view(app.p);
        expect("copy storage ready with no stale content",
               view.available && view.texture && view.source_bound && !view.copy_valid);
        expect("copy generation advances", view.generation > previous_generation);
        previous_generation = view.generation;
        Com<IDirect3DSurface9> logical, physical, again, other;
        ok("logical original depth", app->GetDepthStencilSurface(&logical.p));
        ok("native original depth", native->GetDepthStencilSurface(&physical.p));
        D3DSURFACE_DESC desc{}, physical_desc{};
        ok("logical descriptor", logical->GetDesc(&desc));
        ok("native descriptor", physical->GetDesc(&physical_desc));
        expect("native depth remains original D24X8",
               desc.Format == D3DFMT_D24X8 && physical_desc.Format == D3DFMT_D24X8 && desc.Width == pp.BackBufferWidth);
        ok("other depth allocation", app->CreateDepthStencilSurface(desc.Width, desc.Height, D3DFMT_D24X8,
                                                                    D3DMULTISAMPLE_NONE, 0, TRUE, &other.p, nullptr));
        ok("unbind depth for copy parity", app->SetDepthStencilSurface(nullptr));
        expect("no copy from an unbound source", FAILED(own::copy_auto_depth(app.p)));
        ok("native depth-copy original to other", app->StretchRect(logical.p, nullptr, other.p, nullptr, D3DTEXF_NONE));
        ok("native depth-copy other to original", app->StretchRect(other.p, nullptr, logical.p, nullptr, D3DTEXF_NONE));
        ok("bind other depth", app->SetDepthStencilSurface(other.p));
        expect("no copy from unrelated depth", FAILED(own::copy_auto_depth(app.p)));
        ok("rebind original", app->SetDepthStencilSurface(logical.p));
        other.reset();
        // Stateblock-recording draws retain native no-stencil semantics: the
        // producer injects nothing into those draws and rejects explicit copy.
        ok("native stencil no-op clear", app->Clear(0, nullptr, D3DCLEAR_STENCIL, 0, 1, 0));
        ok("logical stencil enable", app->SetRenderState(D3DRS_STENCILENABLE, TRUE));
        ok("logical stencil never", app->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_NEVER));
        const auto epoch = copy_view(app.p).source_epoch;
        ok("background epoch", app->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, .875f, 0));
        ok("main scene epoch", app->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1, 0));
        expect("source clear epochs", copy_view(app.p).source_epoch == epoch + 2);
        draw_scene(app.p);
        Com<IDirect3DTexture9> sentinel;
        ok("sentinel texture", app->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &sentinel.p, nullptr));
        ok("bind sentinel texture", app->SetTexture(0, sentinel.p));
        float point = 4.25f;
        DWORD point_bits;
        std::memcpy(&point_bits, &point, 4);
        ok("sentinel pointsize", app->SetRenderState(D3DRS_POINTSIZE, point_bits));
        ok("begin application stateblock", app->BeginStateBlock());
        expect("explicit copy rejected during recording", FAILED(own::copy_auto_depth(app.p)));
        Com<IDirect3DStateBlock9> block;
        ok("end application stateblock", app->EndStateBlock(&block.p));
        block.reset();
        ok("copy original depth outside scene", own::copy_auto_depth(app.p));
        view = copy_view(app.p);
        expect("valid copy epoch", view.copy_valid && view.copy_epoch == view.source_epoch);
        Com<IDirect3DBaseTexture9> restored;
        ok("restored sampler getter", app->GetTexture(0, &restored.p));
        DWORD restored_point = 0;
        ok("restored pointsize getter", app->GetRenderState(D3DRS_POINTSIZE, &restored_point));
        expect("copy restores exact sampler and pointsize", restored.p == sentinel.p && restored_point == point_bits);
        restored.reset();
        ok("copy keeps source bound", app->GetDepthStencilSurface(&again.p));
        expect("source logical identity unchanged", again.p == logical.p);
        again.reset();
        Com<IDirect3DSurface9> physical_after;
        ok("copy keeps native source bound", native->GetDepthStencilSurface(&physical_after.p));
        expect("source native identity unchanged", physical_after.p == physical.p);
        physical_after.reset();
        physical.reset();
        const auto copied_epoch = view.copy_epoch;
        ok("overlay destroys original depth", app->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1, 0));
        auto after = copy_view(app.p);
        expect("saved copy survives source clear",
               after.copy_valid && after.copy_epoch == copied_epoch && after.source_epoch == copied_epoch + 1);
        {
            Snapshot saved(native, desc.Width, desc.Height);
            saved.capture(native, view.texture, compiler);
            saved.verify(native, "preserved-original-scene-depth", false);
            ok("begin scene for second resolve", app->BeginScene());
            ok("copy original depth inside scene", own::copy_auto_depth(app.p));
            ok("end scene for second resolve", app->EndScene());
            saved.capture(native, view.texture, compiler);
            saved.verify(native, "fresh-copy-after-source-clear", true);
        }
        ok("unbind sentinel", app->SetTexture(0, nullptr));
        sentinel.reset();
        if (generation == 0) {
            HRESULT hr = app->Reset(&pp);
            std::printf("API held-source Reset result=%08lx\n", static_cast<unsigned long>(hr));
            expect("held logical source still rejects Reset", hr == D3DERR_INVALIDCALL);
            auto lost = copy_view(app.p);
            expect("failed Reset retires copied storage", !lost.available && !lost.copy_valid && !lost.texture);
            logical.reset();
            pp.BackBufferWidth = 80;
            pp.BackBufferHeight = 48;
            ok("copy reset retry", app->Reset(&pp));
        }
    }
    expect("copy device reaches final release", app.p->Release() == 0);
    app.p = nullptr;
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int result = 1;
    HWND window = nullptr;
    try {
        if (argc != 3) throw std::runtime_error("usage: copied_depth_fixture.exe <D3DX path> <depth_decode.hlsl>");
        std::ifstream shader(argv[2]);
        std::ostringstream contents;
        contents << shader.rdbuf();
        decode_source = contents.str();
        expect("production depth decoder loaded", !decode_source.empty());
        WNDCLASSA wc{};
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "X3CopiedDepthFixture";
        RegisterClassA(&wc);
        window = CreateWindowA(wc.lpszClassName, "X3 copied depth fixture", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128,
                               nullptr, nullptr, wc.hInstance, nullptr);
        expect("hidden window", window != nullptr);
        auto compiler = entry<Compiler>(LoadLibraryA(argv[1]), "D3DXCompileShader");
        auto create = entry<IDirect3D9*(WINAPI*)(UINT)>(LoadLibraryA("d3d9.dll"), "Direct3DCreate9");
        auto* backend = create(D3D_SDK_VERSION);
        expect("native factory", backend != nullptr);
        Com<IDirect3D9> api;
        own::Options options;
        options.capture_auto_depth = true;
        HRESULT hr = own::wrap_factory(backend, &api.p, options);
        if (FAILED(hr)) backend->Release();
        ok("wrap factory copy enabled", hr);
        exercise_copy(api.p, window, compiler, D3DCREATE_HARDWARE_VERTEXPROCESSING);
        exercise_copy(api.p, window, compiler, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE);
        std::printf("RESULT PASS checks=%u samples=%u\n", checks, samples);
        result = 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL %s checks=%u samples=%u\n", e.what(), checks, samples);
    }
    if (window) DestroyWindow(window);
    return result;
}
