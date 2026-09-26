// Original synthetic end-to-end scene-boundary adapter fixture.
// Links actual ownership, adapter, selector, allocation IDs and depth decoder.
// No X3 assets, game launch, installation or display output is involved.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/ownership/d3d9_ownership.h"
#include "../../src/proxy/scene_capture.h"
#include "../../src/renderer/scene_boundary.h"
#include <cstdarg>
#include <vector>
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
using Compiler = decltype(&D3DXCompileShader);
static void compile(Compiler compiler, const char* source, const char* profile, ID3DXBuffer** out) {
    Com<ID3DXBuffer> errors;
    HRESULT hr = compiler(source, UINT(std::strlen(source)), nullptr, nullptr, "main", profile,
                          D3DXSHADER_OPTIMIZATION_LEVEL3, out, &errors.p, nullptr);
    if (errors.p) std::printf("compiler %s\n", static_cast<char*>(errors->GetBufferPointer()));
    ok(profile, hr);
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

static std::vector<std::string> trace;
namespace x3m {
void log(const char* format, ...) {
    char buffer[4096];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    trace.emplace_back(buffer);
    std::printf("TRACE %s\n", buffer);
}
}
// Test-only, single-device vtable fault. The real Clear and its epoch bookkeeping
// finish before installation; only the adapter's post-Clear source query fails.
struct PostClearQueryFault {
    IDirect3DDevice9* native;
    void** original;
    std::array<void*, 119> table{};
    unsigned calls = 0;
    static PostClearQueryFault* active;
    explicit PostClearQueryFault(IDirect3DDevice9* value)
        : native(value)
        , original(*reinterpret_cast<void***>(value)) {
        expect("post-Clear fault is not nested", active == nullptr);
        std::memcpy(table.data(), original, sizeof(table));
        table[40] = reinterpret_cast<void*>(&getDepth);
        active = this;
        *reinterpret_cast<void***>(native) = table.data();
    }
    ~PostClearQueryFault() {
        *reinterpret_cast<void***>(native) = original;
        active = nullptr;
    }
    static HRESULT WINAPI getDepth(IDirect3DDevice9* device, IDirect3DSurface9** out) {
        auto& fault = *active;
        if (device != fault.native) return D3DERR_INVALIDCALL;
        ++fault.calls;
        if (out) *out = nullptr;
        return E_FAIL;
    }
};
PostClearQueryFault* PostClearQueryFault::active = nullptr;
static std::uint64_t hash(ID3DXBuffer* code) {
    auto* bytes = static_cast<const unsigned char*>(code->GetBufferPointer());
    std::uint64_t result = 14695981039346656037ull;
    for (unsigned i = 0; i < code->GetBufferSize(); ++i) {
        result ^= bytes[i];
        result *= 1099511628211ull;
    }
    return result;
}
struct SurfaceDescFault {
    static HRESULT WINAPI fail(IDirect3DSurface9*, D3DSURFACE_DESC*) { return E_FAIL; }
    IDirect3DSurface9* surface;
    void** original;
    std::array<void*, 17> table{};
    explicit SurfaceDescFault(IDirect3DSurface9* s)
        : surface(s)
        , original(*reinterpret_cast<void***>(s)) {
        std::copy(original, original + 17, table.begin());
        auto fn = &fail;
        std::memcpy(&table[12], &fn, sizeof fn);
        *reinterpret_cast<void***>(surface) = table.data();
    }
    ~SurfaceDescFault() { *reinterpret_cast<void***>(surface) = original; }
};
struct Scene {
    IDirect3DDevice9* d;
    Com<IDirect3DSurface9> main, depth, copySurface, aSurface, bSurface;
    Com<IDirect3DTexture9> copied, a, b;
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> background, material, bloom[4];
    Com<IDirect3DVertexDeclaration9> decl;
    x3m::renderer::SceneSignatures signatures;
    Scene(IDirect3DDevice9* device, Compiler compiler)
        : d(device) {
        ok("scene main", d->GetRenderTarget(0, &main.p));
        ok("scene depth", d->GetDepthStencilSurface(&depth.p));
        ok("copy color texture",
           d->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &copied.p, nullptr));
        ok("A texture",
           d->CreateTexture(32, 32, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &a.p, nullptr));
        ok("B texture",
           d->CreateTexture(32, 32, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &b.p, nullptr));
        ok("copy surface", copied->GetSurfaceLevel(0, &copySurface.p));
        ok("A surface", a->GetSurfaceLevel(0, &aSurface.p));
        ok("B surface", b->GetSurfaceLevel(0, &bSurface.p));
        Com<ID3DXBuffer> vc, pc;
        compile(
            compiler,
            "void main(float4 p:POSITION0,float2 t:TEXCOORD0,out float4 o:POSITION0,out float2 u:TEXCOORD0){o=p;u=t;}",
            "vs_3_0", &vc.p);
        const auto vh = hash(vc.p);
        ok("scene VS", d->CreateVertexShader(static_cast<DWORD*>(vc->GetBufferPointer()), &vs.p));
        compile(compiler, "float4 main():COLOR0{return float4(.1,.2,.3,1);}", "ps_3_0", &pc.p);
        ok("background PS", d->CreatePixelShader(static_cast<DWORD*>(pc->GetBufferPointer()), &background.p));
        pc.reset();
        compile(compiler, "float4 main():COLOR0{return float4(.8,.2,.1,1);}", "ps_3_0", &pc.p);
        ok("material PS", d->CreatePixelShader(static_cast<DWORD*>(pc->GetBufferPointer()), &material.p));
        pc.reset();
        for (auto& pair : signatures.background) pair = {}; // Deprecated slots: background is recognized structurally.
        for (unsigned i = 0; i < 4; ++i) {
            const std::string
                ps = "sampler2D color:register(s0);float4 main(float2 uv:TEXCOORD0):COLOR0{return tex2D(color,uv)*" +
                     std::to_string(.5f + i * .125f) + ";}";
            compile(compiler, ps.c_str(), "ps_3_0", &pc.p);
            signatures.bloom[i] = {vh, hash(pc.p)};
            ok("bloom PS", d->CreatePixelShader(static_cast<DWORD*>(pc->GetBufferPointer()), &bloom[i].p));
            pc.reset();
        }
        const D3DVERTEXELEMENT9 elements[] = {
            {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
            {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
            D3DDECL_END()};
        ok("scene declaration", d->CreateVertexDeclaration(elements, &decl.p));
        ok("scene VS bind", d->SetVertexShader(vs.p));
        ok("scene declaration bind", d->SetVertexDeclaration(decl.p));
        for (auto pair : {std::pair<D3DRENDERSTATETYPE, DWORD>{D3DRS_CULLMODE, D3DCULL_NONE},
                          {D3DRS_ALPHABLENDENABLE, FALSE},
                          {D3DRS_ALPHATESTENABLE, FALSE},
                          {D3DRS_FOGENABLE, FALSE},
                          {D3DRS_SRGBWRITEENABLE, FALSE},
                          {D3DRS_ZFUNC, D3DCMP_LESS},
                          {D3DRS_STENCILENABLE, FALSE},
                          {D3DRS_SCISSORTESTENABLE, FALSE},
                          {D3DRS_COLORWRITEENABLE, 15}})
            ok("scene render state", d->SetRenderState(pair.first, pair.second));
        for (auto pair : {std::pair<D3DSAMPLERSTATETYPE, DWORD>{D3DSAMP_MINFILTER, D3DTEXF_POINT},
                          {D3DSAMP_MAGFILTER, D3DTEXF_POINT},
                          {D3DSAMP_MIPFILTER, D3DTEXF_NONE},
                          {D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP},
                          {D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP},
                          {D3DSAMP_SRGBTEXTURE, FALSE}})
            ok("scene sampler", d->SetSamplerState(0, pair.first, pair.second));
        viewport(64);
    }
    ~Scene() {
        d->SetTexture(0, nullptr);
        d->SetVertexShader(nullptr);
        d->SetPixelShader(nullptr);
        d->SetVertexDeclaration(nullptr);
    }
    void viewport(UINT size) {
        D3DVIEWPORT9 vp = {0, 0, size, size, 0, 1};
        ok("scene viewport", d->SetViewport(&vp));
    }
    void clear(x3m::SceneCapture& capture, DWORD flags) {
        capture.before_clear(d, 0, nullptr, flags, 1);
        const HRESULT hr = d->Clear(0, nullptr, flags, 0, 1, 0);
        capture.after_clear(d, hr);
        ok("scene clear", hr);
    }
    struct V {
        float x, y, z, w, u, v;
    };
    V point(float x, float y, float z) { return {2 * x / 64 - 1, 1 - 2 * y / 64, z, 1, 0, 0}; }
    void draw(x3m::SceneCapture& capture, IDirect3DPixelShader9* ps, bool writes, bool geometry = false) {
        ok("draw PS", d->SetPixelShader(ps));
        ok("draw depth enable", d->SetRenderState(D3DRS_ZENABLE, writes));
        ok("draw depth write", d->SetRenderState(D3DRS_ZWRITEENABLE, writes));
        const V quad[] = {{-1, 1, .8f, 1, 0, 0}, {1, 1, .8f, 1, 1, 0}, {-1, -1, .8f, 1, 0, 1}, {1, -1, .8f, 1, 1, 1}};
        const V triangles[] = {point(3.5, 3.5, .25),  point(27.5, 3.5, .25),  point(3.5, 27.5, .25),
                               point(35.5, 3.5, .75), point(59.5, 3.5, .75),  point(59.5, 27.5, .75),
                               point(3.5, 3.5, .875), point(27.5, 3.5, .875), point(3.5, 27.5, .875)};
        const auto kind = geometry ? D3DPT_TRIANGLELIST : D3DPT_TRIANGLESTRIP;
        const UINT primitives = geometry ? 3 : 2;
        ok("draw BeginScene", d->BeginScene());
        capture.before_draw(d, kind, primitives);
        const HRESULT hr = d->DrawPrimitiveUP(kind, primitives, geometry ? triangles : quad, sizeof(V));
        capture.after_draw(hr);
        ok("draw result", hr);
        ok("draw EndScene", d->EndScene());
    }
    void fill(x3m::SceneCapture& capture, IDirect3DSurface9* target, const RECT* rect = nullptr,
              bool callbackFailure = false, bool queryFailure = false) {
        const HRESULT hr = d->ColorFill(target, rect, 0);
        ok("actual ColorFill", hr);
        if (queryFailure) {
            SurfaceDescFault fault(target);
            capture.after_color_fill(d, target, rect, hr);
            std::puts("INJECT ColorFill target GetDesc E_FAIL");
        } else if (callbackFailure) {
            // Callback-only fault: never submit these invalid arguments to D3D.
            capture.after_color_fill(d, nullptr, reinterpret_cast<const RECT*>(std::uintptr_t(1)), E_FAIL);
            std::puts("INJECT failed ColorFill callback with null target and unreadable RECT sentinel");
        } else
            capture.after_color_fill(d, target, rect, hr);
    }
    void prefix(x3m::SceneCapture& capture, bool failCopy, unsigned fillMode = 0, bool badInitial = false) {
        clear(capture, badInitial ? D3DCLEAR_TARGET : D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER);
        for (unsigned i = 0; i < 5; ++i) draw(capture, background.p, true);
        clear(capture, D3DCLEAR_ZBUFFER);
        draw(capture, material.p, true, true);
        if (fillMode == 6) fill(capture, aSurface.p); // Still in Scene: prohibited phase.
        HRESULT hr = d->SetDepthStencilSurface(nullptr);
        capture.after_set_depth(d, hr);
        ok("depth unbind", hr);
        if (fillMode == 1) {
            const RECT partial = {0, 0, 16, 16};
            fill(capture, copySurface.p);
            fill(capture, aSurface.p, &partial);
            fill(capture, bSurface.p);
        } else if (fillMode == 2)
            fill(capture, main.p);
        else if (fillMode == 3) {
            hr = d->ColorFill(depth.p, nullptr, 0);
            capture.after_color_fill(d, depth.p, nullptr, hr);
            std::printf("OBS depth ColorFill=%08lx\n", static_cast<unsigned long>(hr));
        } else if (fillMode == 4)
            fill(capture, aSurface.p, nullptr, false, true);
        else if (fillMode == 5)
            fill(capture, aSurface.p, nullptr, true);
        else if (fillMode == 7) {
            Com<IDirect3DSurface9> ordinary;
            ok("standalone scratch",
               d->CreateRenderTarget(32, 32, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &ordinary.p, nullptr));
            fill(capture, ordinary.p);
        }
        const RECT outside = {0, 0, 65, 64};
        const RECT* sourceRect = failCopy ? &outside : nullptr;
        hr = d->StretchRect(main.p, sourceRect, copySurface.p, nullptr, D3DTEXF_NONE);
        if (failCopy) {
            expect("actual color copy failure", FAILED(hr));
            // Only the callback sees unreadable surface sentinels; the backend
            // received the valid source/destination above and rejected its RECT.
            capture.after_stretch(d, reinterpret_cast<IDirect3DSurface9*>(std::uintptr_t(1)), sourceRect,
                                  reinterpret_cast<IDirect3DSurface9*>(std::uintptr_t(3)), nullptr, hr);
            std::puts("INJECT failed StretchRect callback with unreadable source/destination surfaces");
        } else {
            capture.after_stretch(d, main.p, sourceRect, copySurface.p, nullptr, hr);
            ok("full color copy", hr);
        }
        IDirect3DSurface9* surfaces[] = {aSurface.p, bSurface.p, aSurface.p, main.p};
        IDirect3DTexture9* textures[] = {copied.p, a.p, b.p, a.p};
        for (unsigned i = 0; i < 4; ++i) {
            ok("unbind sampled target", d->SetTexture(0, nullptr));
            hr = d->SetRenderTarget(0, surfaces[i]);
            capture.after_set_rt(d, 0, hr);
            ok("bloom target", hr);
            viewport(i == 3 ? 64 : 32);
            ok("bloom input", d->SetTexture(0, textures[i]));
            draw(capture, bloom[i].p, false);
        }
        hr = d->SetDepthStencilSurface(depth.p);
        capture.after_set_depth(d, hr);
        ok("scene depth rebind", hr);
    }
};
enum class Case {
    Positive,
    Unsupported,
    FailedColorCopy,
    FailedFinalClear,
    RejectedDepthCopy,
    GenerationMismatch,
    FailedDrawAfter,
    FailedPresentAfter,
    Inactive,
    PostClearQueryFailure,
    ScratchFills,
    FillMain,
    FillDepth,
    FillUnknown,
    FillFailed,
    FillWrongPhase,
    FillStandalone,
    FirstRejection
};
static const char* names[] = {"positive",
                              "unsupported",
                              "failed-color-copy",
                              "failed-final-clear",
                              "rejected-depth-copy",
                              "generation-mismatch",
                              "failed-draw-after-selection",
                              "failed-present-after-selection",
                              "inactive-capture",
                              "post-clear-binding-query-failure",
                              "scratch-color-fills",
                              "main-color-fill",
                              "depth-color-fill",
                              "unknown-color-fill",
                              "failed-color-fill",
                              "wrong-phase-color-fill",
                              "standalone-color-fill",
                              "first-rejection-preserved"};
static void exercise(IDirect3D9* api, HWND window, Compiler compiler, DWORD flags, Case test, std::uint64_t frame) {
    trace.clear();
    std::printf("CASE name=%s flags=%08lx frame=%llu\n", names[unsigned(test)], static_cast<unsigned long>(flags),
                frame);
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
    Com<IDirect3DDevice9> d;
    ok("create case device", api->CreateDevice(0, D3DDEVTYPE_HAL, window, flags, &pp, &d.p));
    {
        Scene scene(d.p, compiler);
        x3m::SceneCapture capture(scene.signatures);
        capture.configure(true);
        capture.begin_frame(d.p, frame, frame, test != Case::Inactive);
        const unsigned fillMode = test >= Case::ScratchFills && test <= Case::FillStandalone
                                      ? unsigned(test) - unsigned(Case::ScratchFills) + 1
                                      : 0;
        scene.prefix(capture, test == Case::FailedColorCopy, fillMode, test == Case::FirstRejection);
        if (test == Case::FirstRejection) capture.unsupported("later-unsupported-after-pattern", S_OK);
        if (test == Case::Unsupported) capture.unsupported("fixture-untracked-operation", S_OK);
        Com<IDirect3DStateBlock9> block;
        if (test == Case::RejectedDepthCopy) ok("begin blocking state recording", d->BeginStateBlock());
        capture.before_clear(d.p, 0, nullptr, D3DCLEAR_ZBUFFER, 1);
        if (test == Case::GenerationMismatch) {
            own::CopyDepthView old{}, now{};
            ok("prior generation", own::get_copy_depth_view(d.p, &old));
            expect("actual held-resource Reset failure", d->Reset(&pp) == D3DERR_INVALIDCALL);
            ok("retired generation", own::get_copy_depth_view(d.p, &now));
            expect("generation actually advanced", now.generation > old.generation && !now.available);
        }
        HRESULT clearResult;
        if (test == Case::FailedFinalClear) {
            // Deliberate callback-result fault injection, not a normal app trace.
            // Retain a real valid GPU Clear but report failure to the adapter.
            ok("real Clear for failed callback test", d->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1, 0));
            clearResult = D3DERR_INVALIDCALL;
            std::puts("INJECT final Clear callback D3DERR_INVALIDCALL");
        } else
            clearResult = d->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1, 0);
        if (test == Case::GenerationMismatch) {
            std::printf("INJECT Clear callback S_OK after real generation retirement; actual=%08lx\n",
                        static_cast<unsigned long>(clearResult));
            capture.after_clear(d.p, S_OK);
        } else if (test == Case::PostClearQueryFailure) {
            ok("real Clear before source-query fault", clearResult);
            own::CopyDepthView before{}, after{};
            ok("pre-fault complete copy view", own::get_copy_depth_view(d.p, &before));
            expect("query fault starts from otherwise confirmable copy",
                   before.available && before.copy_valid && before.source_bound && SUCCEEDED(before.status) &&
                       before.source_epoch == before.copy_epoch + 1);
            auto* native = own::borrowed_native_device(d.p);
            expect("post-Clear native seam", native != nullptr);
            void** original = *reinterpret_cast<void***>(native);
            {
                PostClearQueryFault fault(native);
                capture.after_clear(d.p, clearResult);
                expect("only adapter post-Clear source query was faulted", fault.calls == 1);
                std::puts("INJECT native GetDepthStencilSurface E_FAIL for adapter post-Clear view query only");
            }
            expect("native query vtable restored", *reinterpret_cast<void***>(native) == original);
            ok("post-fault live copy view", own::get_copy_depth_view(d.p, &after));
            expect("ordinary query failure preserves copied storage and epochs",
                   after.available && after.copy_valid && after.source_bound && SUCCEEDED(after.status) &&
                       after.texture == before.texture && after.generation == before.generation &&
                       after.copy_epoch == before.copy_epoch && after.source_epoch == before.source_epoch);
        } else
            capture.after_clear(d.p, clearResult);
        if (test == Case::RejectedDepthCopy) ok("end blocking state recording", d->EndStateBlock(&block.p));
        if (test == Case::Positive || test == Case::ScratchFills) {
            ok("final Clear", clearResult);
            own::CopyDepthView view{};
            ok("preserved view", own::get_copy_depth_view(d.p, &view));
            expect("real pre-clear copy and advanced source epoch",
                   view.copy_valid && view.copy_epoch + 1 == view.source_epoch);
            auto* native = own::borrowed_native_device(d.p);
            Snapshot saved(native, 64, 64);
            saved.capture(native, view.texture, compiler);
            saved.verify(native, "adapter-preserved-before-clear", false);
        }
        if (test == Case::FailedDrawAfter) {
            ok("negative draw BeginScene", d->BeginScene());
            capture.before_draw(d.p, D3DPT_TRIANGLELIST, 1);
            // Indexed drawing without an index buffer is a real invalid call.
            const HRESULT hr = d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 3, 0, 1);
            capture.after_draw(hr);
            expect("actual postselection indexed draw failed", FAILED(hr));
            ok("negative draw EndScene", d->EndScene());
        }
        const HRESULT present = d->Present(nullptr, nullptr, nullptr, nullptr);
        if (test == Case::GenerationMismatch) {
            std::printf("INJECT Present callback S_OK after real generation retirement; actual=%08lx\n",
                        static_cast<unsigned long>(present));
            capture.end_frame(S_OK);
        } else {
            ok("actual Present", present);
            capture.end_frame(test == Case::FailedPresentAfter ? D3DERR_DEVICELOST : present);
        }
        if (test == Case::FailedPresentAfter)
            std::puts("INJECT Present callback DEVICELOST after successful native Present");
        unsigned ends = 0, confirmed = 0, copies = 0, validCopies = 0, boundaries = 0, boundaryConfirmed = 0;
        for (const auto& line : trace) {
            if (line.find("scene_depth_frame phase=end") != std::string::npos) {
                ++ends;
                if (line.find("confirmed=1") != std::string::npos) ++confirmed;
            }
            if (line.find("scene_depth_copy ") != std::string::npos) {
                ++copies;
                if (line.find("valid=1") != std::string::npos) ++validCopies;
            }
            if (line.find("scene_depth_boundary ") != std::string::npos) {
                ++boundaries;
                if (line.find("confirmed=1") != std::string::npos) ++boundaryConfirmed;
            }
        }
        const bool positive = test == Case::Positive || test == Case::ScratchFills;
        expect("end confirmation matches scenario", confirmed == (positive ? 1u : 0u));
        expect("bounded frame end", ends == (test == Case::Inactive ? 0u : 1u));
        if (positive)
            expect("exactly one successful copy and boundary", copies == 1 && validCopies == 1 && boundaries == 1);
        if (test == Case::Unsupported || test == Case::FailedColorCopy || test == Case::Inactive ||
            test >= Case::FillMain) {
            own::CopyDepthView untouched{};
            ok("inactive copy view", own::get_copy_depth_view(d.p, &untouched));
            expect("rejected prefix never copies", copies == 0 && !untouched.copy_valid);
        }
        if (test == Case::RejectedDepthCopy)
            expect("candidate copy failure not confirmed", copies == 1 && validCopies == 0);
        if (test == Case::GenerationMismatch)
            expect("retired successful copy never confirmed", copies == 1 && validCopies == 1 && boundaries == 1);
        if (test == Case::PostClearQueryFailure)
            expect("query failure rejects otherwise selected copy",
                   copies == 1 && validCopies == 1 && boundaries == 1 && boundaryConfirmed == 0);
        if (test == Case::FailedFinalClear)
            expect("failed Clear does not publish boundary", validCopies == 1 && boundaries == 0);
        if (test == Case::FailedDrawAfter || test == Case::FailedPresentAfter)
            expect("postselection failure tested after actual copy", validCopies == 1 && boundaries == 1);
        if (test == Case::FirstRejection) {
            unsigned firstLogs = 0;
            bool firstEvent = false, endFirst = false;
            for (const auto& line : trace) {
                if (line.find("scene_depth_reject ") != std::string::npos) {
                    ++firstLogs;
                    firstEvent = line.find("event=1 operation=Clear") != std::string::npos &&
                                 line.find("reason=Pattern") != std::string::npos;
                }
                if (line.find("scene_depth_frame phase=end") != std::string::npos)
                    endFirst = line.find("rejection=5 rejection_event=1") != std::string::npos;
            }
            expect("first Pattern rejection logged once and preserved", firstLogs == 1 && firstEvent && endFirst);
        }
        if (test == Case::FailedColorCopy) {
            bool unqueried = false;
            for (const auto& line : trace)
                if (line.find("scene_depth_reject ") != std::string::npos &&
                    line.find("operation=StretchRect") != std::string::npos &&
                    line.find("reason=FailedCall") != std::string::npos &&
                    line.find("destination_known=0") != std::string::npos)
                    unqueried = true;
            expect("failed StretchRect surface arguments remain unqueried", unqueried);
        }
        if (test == Case::FillFailed) {
            bool unknown = false;
            for (const auto& line : trace)
                if (line.find("scene_depth_color_fill ") != std::string::npos &&
                    line.find("result=80004005") != std::string::npos &&
                    line.find("target_known=0") != std::string::npos)
                    unknown = true;
            expect("failed ColorFill arguments remain unqueried", unknown);
        }
        if (test >= Case::FillMain && test <= Case::FillStandalone) {
            bool logged = false;
            for (const auto& line : trace)
                if (line.find("scene_depth_reject ") != std::string::npos &&
                    line.find("operation=ColorFill") != std::string::npos)
                    logged = true;
            expect("unsafe ColorFill first rejection logged", logged);
        }
        std::printf("SCENARIO %s flags=%08lx confirmed=%u copies=%u valid_copies=%u PASS\n", names[unsigned(test)],
                    static_cast<unsigned long>(flags), confirmed, copies, validCopies);
    }
    expect("case device final logical release", d.p->Release() == 0);
    d.p = nullptr;
}
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    HWND window = nullptr;
    int result = 1;
    try {
        if (argc != 3) throw std::runtime_error("expected D3DX and production decoder paths");
        std::ifstream shader(argv[2]);
        std::ostringstream source;
        source << shader.rdbuf();
        decode_source = source.str();
        expect("production decoder source", !decode_source.empty());
        WNDCLASSA wc{};
        wc.lpfnWndProc = DefWindowProcA;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = "X3SceneCaptureFixture";
        RegisterClassA(&wc);
        window = CreateWindowA(wc.lpszClassName, "X3 original scene adapter fixture", WS_OVERLAPPEDWINDOW, 0, 0, 128,
                               128, nullptr, nullptr, wc.hInstance, nullptr);
        expect("hidden window", window != nullptr);
        auto compiler = entry<Compiler>(LoadLibraryA(argv[1]), "D3DXCompileShader");
        auto create = entry<IDirect3D9*(WINAPI*)(UINT)>(LoadLibraryA("d3d9.dll"), "Direct3DCreate9");
        auto* native = create(D3D_SDK_VERSION);
        expect("native factory", native != nullptr);
        Com<IDirect3D9> factory;
        own::Options options;
        options.capture_auto_depth = true;
        const HRESULT hr = own::wrap_factory(native, &factory.p, options);
        if (FAILED(hr)) native->Release();
        ok("copy factory", hr);
        std::uint64_t frame = 0;
        for (DWORD flags : {DWORD(D3DCREATE_HARDWARE_VERTEXPROCESSING),
                            DWORD(D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE)})
            for (unsigned test = 0; test < 18; ++test)
                exercise(factory.p, window, compiler, flags, Case(test), ++frame);
        std::printf("RESULT PASS checks=%u samples=%u scenarios=36\n", checks, samples);
        result = 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL %s checks=%u samples=%u\n", e.what(), checks, samples);
    }
    if (window) DestroyWindow(window);
    return result;
}
