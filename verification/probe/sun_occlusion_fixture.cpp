// Detached GPU fixture of renderer::SunOcclusionPass (docs/architecture/sun-partial-occlusion.md,
// "Fixtures"): the 1x1 visibility fraction against a CPU twin of the 32 taps over synthetic RT2
// scenes (open, covered, half-plane edges, off-screen, no valid tap, first-frame radius), the
// temporal step response and the dead band, the three depth formats, a failed draw with recovery,
// the wrapped lens draw under every admitted blend law for ps_2_0 and ps_3_0 originals, every
// refusal, exact state restoration around each transaction under hostile state, native Reset and
// teardown. Validation-only readback lives here, never in production (the pass's own readback()
// is its diagnostic one). Built by build_sun_occlusion.py; run by run_sun_occlusion.py.
#include <windows.h>
#include <d3d9.h>
#include "../../src/renderer/sun_occlusion_pass.h"
#include "../../src/renderer/quad_vertex_program.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace x3m::renderer;
namespace core = x3m::sun_occlusion::core;
namespace {
template<class T> struct Com {
    T* p = nullptr;
    ~Com() { reset(); }
    void reset() { if (p) { p->Release(); p = nullptr; } }
    T* operator->() const { return p; }
    Com() = default; Com(const Com&) = delete; Com& operator=(const Com&) = delete;
};
unsigned checks = 0, failures = 0;
void check(const char* label, HRESULT hr) { if (FAILED(hr)) { std::printf("API FAIL %s %08lx\n", label, (unsigned long)hr); throw std::runtime_error(label); } }
bool require(const char* label, bool value) { ++checks; if (!value) ++failures; std::printf("CHECK %s %s\n", label, value ? "PASS" : "FAIL"); return value; }
#define X3M_SUN_TAP(x, y) {x, y},
const float taps[32][2] = {
#include "../../src/renderer/sun_visibility_taps_inc.h"
};
// ---- fault injection through the table the pass is given ----
void* original[119]; void* hooked[119];
struct Faults { unsigned draw_fail_at = 0, draw_calls = 0, set_ps_fail_at = 0, set_ps_calls = 0, create_ps_calls = 0; } faults;
using DrawUpFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using CreatePsFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(IDirect3DDevice9*, IDirect3DPixelShader9*);
HRESULT WINAPI hook_draw(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT c, const void* v, UINT s) {
    if (++faults.draw_calls == faults.draw_fail_at) return E_FAIL;
    return reinterpret_cast<DrawUpFn>(original[83])(d, t, c, v, s);
}
HRESULT WINAPI hook_create_ps(IDirect3DDevice9* d, const DWORD* w, IDirect3DPixelShader9** out) { ++faults.create_ps_calls; return reinterpret_cast<CreatePsFn>(original[106])(d, w, out); }
HRESULT WINAPI hook_set_ps(IDirect3DDevice9* d, IDirect3DPixelShader9* ps) {
    if (++faults.set_ps_calls == faults.set_ps_fail_at) return E_FAIL;
    return reinterpret_cast<SetPsFn>(original[107])(d, ps);
}
// ---- the caller's state, compared byte for byte around every transaction ----
struct Snapshot {
    std::vector<unsigned char> bytes;
    template<class T> void add(const T& v) { auto* p = reinterpret_cast<const unsigned char*>(&v); bytes.insert(bytes.end(), p, p + sizeof v); }
    template<class T> void object(T* v) { add(reinterpret_cast<std::uintptr_t>(v)); if (v) v->Release(); }
    explicit Snapshot(IDirect3DDevice9* d) {
        for (UINT i = 0; i < 4; ++i) { IDirect3DSurface9* s = nullptr; HRESULT hr = d->GetRenderTarget(i, &s); add(hr); object(s); }
        IDirect3DSurface9* ds = nullptr; HRESULT hr = d->GetDepthStencilSurface(&ds); add(hr); object(ds);
        D3DVIEWPORT9 vp{}; check("snapshot viewport", d->GetViewport(&vp)); add(vp);
        RECT sc{}; check("snapshot scissor", d->GetScissorRect(&sc)); add(sc);
        IDirect3DVertexShader9* vs = nullptr; check("snapshot vs", d->GetVertexShader(&vs)); object(vs);
        IDirect3DPixelShader9* ps = nullptr; check("snapshot ps", d->GetPixelShader(&ps)); object(ps);
        IDirect3DVertexDeclaration9* decl = nullptr; check("snapshot decl", d->GetVertexDeclaration(&decl)); object(decl);
        DWORD fvf = 0; check("snapshot fvf", d->GetFVF(&fvf)); add(fvf);
        IDirect3DVertexBuffer9* vb = nullptr; UINT off = 0, stride = 0, freq = 0;
        check("snapshot stream", d->GetStreamSource(0, &vb, &off, &stride)); check("snapshot freq", d->GetStreamSourceFreq(0, &freq)); object(vb); add(off); add(stride); add(freq);
        IDirect3DIndexBuffer9* ib = nullptr; check("snapshot indices", d->GetIndices(&ib)); object(ib);
        for (UINT n = 0; n < 4; ++n) { IDirect3DBaseTexture9* t = nullptr; check("snapshot vertex texture", d->GetTexture(D3DVERTEXTEXTURESAMPLER0 + n, &t)); object(t); }
        for (UINT n = 0; n < 16; ++n) {
            IDirect3DBaseTexture9* t = nullptr; check("snapshot texture", d->GetTexture(n, &t)); object(t);
            for (UINT j = 1; j <= 13; ++j) { DWORD v = 0; hr = d->GetSamplerState(n, D3DSAMPLERSTATETYPE(j), &v); add(hr); add(v); }
        }
        const D3DRENDERSTATETYPE states[] = {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_STENCILENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC, D3DRS_ALPHABLENDENABLE,
            D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP, D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRCBLENDALPHA, D3DRS_DESTBLENDALPHA, D3DRS_FOGENABLE,
            D3DRS_SRGBWRITEENABLE, D3DRS_SCISSORTESTENABLE, D3DRS_CLIPPLANEENABLE, D3DRS_CLIPPING, D3DRS_LIGHTING, D3DRS_INDEXEDVERTEXBLENDENABLE,
            D3DRS_VERTEXBLEND, D3DRS_FILLMODE, D3DRS_CULLMODE, D3DRS_COLORWRITEENABLE, D3DRS_COLORWRITEENABLE1, D3DRS_MULTISAMPLEMASK, D3DRS_WRAP0,
            D3DRS_POINTSPRITEENABLE, D3DRS_DITHERENABLE, D3DRS_ANTIALIASEDLINEENABLE};
        for (auto s : states) { DWORD v = 0; check("snapshot rs", d->GetRenderState(s, &v)); add(v); }
        for (auto s : {D3DTSS_TEXCOORDINDEX, D3DTSS_TEXTURETRANSFORMFLAGS}) { DWORD v = 0; check("snapshot tss", d->GetTextureStageState(0, s, &v)); add(v); }
        float c[16]{}; check("snapshot constants", d->GetPixelShaderConstantF(0, c, 4)); add(c);
    }
    bool operator==(const Snapshot& o) const { return bytes == o.bytes; }
};
// The route's bindings at the lens bracket plus hostile state on every unit the pass touches
// (samplers 0-1 for the quad, sampler 15 for the wrap) and on the states its quad must override.
struct Bindings {
    Com<IDirect3DTexture9> rt1, rt2, junk;
    Com<IDirect3DSurface9> rt1_surface, rt2_surface, depth, back;
    Bindings(IDirect3DDevice9* d, unsigned w, unsigned h) {
        check("rt1", d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rt1.p, nullptr)); check("rt1 surface", rt1->GetSurfaceLevel(0, &rt1_surface.p));
        check("rt2", d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &rt2.p, nullptr)); check("rt2 surface", rt2->GetSurfaceLevel(0, &rt2_surface.p));
        check("junk", d->CreateTexture(4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &junk.p, nullptr));
        check("auto depth", d->GetDepthStencilSurface(&depth.p)); check("back buffer", d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &back.p));
    }
    void hostile(IDirect3DDevice9* d) {
        check("bind rt0", d->SetRenderTarget(0, back.p)); // the MRT set is one size
        check("bind rt1", d->SetRenderTarget(1, rt1_surface.p)); check("bind rt2", d->SetRenderTarget(2, rt2_surface.p)); check("bind depth", d->SetDepthStencilSurface(depth.p));
        for (auto [s, v] : {std::pair{D3DRS_ALPHABLENDENABLE, DWORD(TRUE)}, std::pair{D3DRS_SRCBLEND, DWORD(D3DBLEND_DESTCOLOR)}, std::pair{D3DRS_DESTBLEND, DWORD(D3DBLEND_ZERO)},
                            std::pair{D3DRS_BLENDOP, DWORD(D3DBLENDOP_REVSUBTRACT)}, std::pair{D3DRS_SCISSORTESTENABLE, DWORD(TRUE)}, std::pair{D3DRS_ZENABLE, DWORD(TRUE)},
                            std::pair{D3DRS_COLORWRITEENABLE, DWORD(1)}, std::pair{D3DRS_CULLMODE, DWORD(D3DCULL_CW)}, std::pair{D3DRS_FILLMODE, DWORD(D3DFILL_WIREFRAME)},
                            std::pair{D3DRS_SRGBWRITEENABLE, DWORD(TRUE)}, std::pair{D3DRS_FOGENABLE, DWORD(TRUE)}, std::pair{D3DRS_ALPHATESTENABLE, DWORD(TRUE)},
                            std::pair{D3DRS_ALPHAREF, DWORD(255)}, std::pair{D3DRS_ALPHAFUNC, DWORD(D3DCMP_NEVER)},
                            std::pair{D3DRS_STENCILENABLE, DWORD(TRUE)}, std::pair{D3DRS_MULTISAMPLEMASK, DWORD(0)}, std::pair{D3DRS_CLIPPING, DWORD(TRUE)}})
            check("hostile rs", d->SetRenderState(s, v));
        RECT sc{3, 3, 5, 5}; check("hostile scissor", d->SetScissorRect(&sc));
        D3DVIEWPORT9 vp{2, 2, 7, 7, .2f, .7f}; check("hostile viewport", d->SetViewport(&vp));
        for (UINT i : {0u, 1u, 15u}) {
            check("hostile texture", d->SetTexture(i, junk.p));
            check("hostile sampler", d->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR)); check("hostile sampler", d->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR));
            check("hostile sampler", d->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));
            check("hostile sampler", d->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER)); check("hostile sampler", d->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_MIRROR));
            check("hostile sampler", d->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, TRUE)); check("hostile sampler", d->SetSamplerState(i, D3DSAMP_BORDERCOLOR, 0x00000000));
        }
        const float garbage[16] = {9, 8, 7, 6, 5, 4, 3, 2, 1, .5f, .25f, .125f, -1, -2, -3, -4};
        check("hostile constants", d->SetPixelShaderConstantF(0, garbage, 4));
        check("hostile fvf", d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE));
        check("hostile tss", d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 2));
    }
};
// ---- synthetic RT2: covered (device depth 0.5) from column `edge` on, the -1 sentinel before it ----
struct Depth {
    unsigned w, h; D3DFORMAT format;
    Com<IDirect3DTexture9> texture, staging;
    Depth(IDirect3DDevice9* d, unsigned width, unsigned height, D3DFORMAT f) : w(width), h(height), format(f) {
        check("depth texture", d->CreateTexture(w, h, 1, 0, f, D3DPOOL_DEFAULT, &texture.p, nullptr));
        check("depth staging", d->CreateTexture(w, h, 1, 0, f, D3DPOOL_SYSTEMMEM, &staging.p, nullptr));
    }
    void edge(IDirect3DDevice9* d, int column) {
        const unsigned channels = format == D3DFMT_R32F ? 1 : format == D3DFMT_G32R32F ? 2 : 4;
        D3DLOCKED_RECT lr{}; check("lock depth", staging->LockRect(0, &lr, nullptr, 0));
        for (unsigned y = 0; y < h; ++y) { auto* row = reinterpret_cast<float*>(static_cast<char*>(lr.pBits) + y * lr.Pitch);
            for (unsigned x = 0; x < w; ++x) for (unsigned c = 0; c < channels; ++c) row[x * channels + c] = c ? 123.f : (int(x) >= column ? .5f : -1.f); }
        check("unlock depth", staging->UnlockRect(0));
        check("update depth", d->UpdateTexture(staging.p, texture.p));
    }
};
struct Twin { unsigned open = 0, valid = 0; double raw() const { return valid ? double(open) / valid : -1.; } };
Twin twin(unsigned w, int column, float u, float v, float ru, float rv) {
    Twin t;
    for (const auto& tap : taps) {
        const float x = u + tap[0] * ru, y = v + tap[1] * rv;
        if (x < 0.f || x > 1.f || y < 0.f || y > 1.f) continue;
        ++t.valid;
        int texel = int(std::floor(x * float(w))); if (texel >= int(w)) texel = int(w) - 1;
        if (texel < column) ++t.open;
    }
    return t;
}
double band(double f, double curve = 1.) { double b = (f - .03) / .94; b = b < 0 ? 0 : b > 1 ? 1 : b; return b <= 0 ? 0 : std::pow(b, curve); }
struct Read { float f[4]{}; bool ok = false; };
struct Rig {
    IDirect3DDevice9* d; SunOcclusionPass& pass; Bindings& bindings;
    // One execute under hostile state: snapshot equality is part of the result.
    bool run(const SunVisibilityFrame& in, SunVisibilityResult& out, HRESULT& hr) {
        bindings.hostile(d);
        const Snapshot before(d);
        hr = pass.execute(in, &out);
        const Snapshot after(d);
        return before == after;
    }
    Read read() { Read r; r.ok = SUCCEEDED(pass.readback(r.f)); return r; }
};
SunVisibilityFrame frame_of(Depth& depth, float u, float v, float ru, float rv, bool seed, float alpha = 1.f, float curve = 1.f) {
    SunVisibilityFrame f; f.depth = depth.texture.p; f.u = u; f.v = v; f.radius_u = ru; f.radius_v = rv; f.seed = seed; f.alpha = alpha; f.curve = curve; f.caller_scene_open = false;
    return f;
}
bool close_to(double a, double b, double tolerance) { return std::fabs(a - b) <= tolerance; }
// ---- the lens draw ----
// ps_2_0: def c0, 0.8, 0.6, 0.4, 0.5 / mov r0, c0 / mov oC0, r0.   ps_3_0: def c0 / mov oC0, c0.   ps_1_1: def c0 / mov r0, c0.
const DWORD ps20_words[] = {0xffff0200, 0x05000051, 0xa00f0000, 0x3f4ccccd, 0x3f19999a, 0x3ecccccd, 0x3f000000, 0x02000001, 0x800f0000, 0xa0e40000, 0x02000001, 0x800f0800, 0x80e40000, 0x0000ffff};
const DWORD ps30_words[] = {0xffff0300, 0x05000051, 0xa00f0000, 0x3f4ccccd, 0x3f19999a, 0x3ecccccd, 0x3f000000, 0x02000001, 0x800f0800, 0xa0e40000, 0x0000ffff};
const DWORD ps11_words[] = {0xffff0101, 0x00000051, 0xa00f0000, 0x3f4ccccd, 0x3f19999a, 0x3ecccccd, 0x3f000000, 0x00000001, 0x800f0000, 0xa0e40000, 0x0000ffff};
const double source[4] = {.8, .6, .4, .5}, background[4] = {.2, .4, .6, 1.};
struct Canvas {
    Com<IDirect3DTexture9> texture; Com<IDirect3DSurface9> surface, sink;
    explicit Canvas(IDirect3DDevice9* d) {
        check("canvas", d->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture.p, nullptr)); check("canvas surface", texture->GetSurfaceLevel(0, &surface.p));
        check("canvas sink", d->CreateOffscreenPlainSurface(16, 16, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sink.p, nullptr));
    }
    void pixel(IDirect3DDevice9* d, double out[4]) {
        check("canvas read", d->GetRenderTargetData(surface.p, sink.p));
        D3DLOCKED_RECT lr{}; check("canvas lock", sink->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        const DWORD c = static_cast<const DWORD*>(lr.pBits)[8 + 8 * (lr.Pitch / 4)];
        check("canvas unlock", sink->UnlockRect());
        out[0] = ((c >> 16) & 255) / 255.; out[1] = ((c >> 8) & 255) / 255.; out[2] = (c & 255) / 255.; out[3] = (c >> 24) / 255.;
    }
};
struct Law { const char* name; D3DBLEND src, dst; core::Scale scale; };
const Law laws[] = {{"srcalpha_invsrcalpha", D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, core::Scale::Alpha}, {"srcalpha_one", D3DBLEND_SRCALPHA, D3DBLEND_ONE, core::Scale::Alpha},
                    {"one_one", D3DBLEND_ONE, D3DBLEND_ONE, core::Scale::Rgb}, {"one_invsrccolor", D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, core::Scale::Rgb},
                    {"one_invsrcalpha", D3DBLEND_ONE, D3DBLEND_INVSRCALPHA, core::Scale::Both}};
double factor(D3DBLEND b, const double s[4], unsigned channel) {
    switch (b) { case D3DBLEND_ONE: return 1; case D3DBLEND_SRCALPHA: return s[3]; case D3DBLEND_INVSRCALPHA: return 1 - s[3]; case D3DBLEND_INVSRCCOLOR: return 1 - s[channel]; default: return 0; }
}
void expected_pixel(const Law& law, double f, double out[3]) {
    double s[4] = {source[0], source[1], source[2], source[3]};
    if (law.scale != core::Scale::Alpha) for (unsigned i = 0; i < 3; ++i) s[i] *= f;
    if (law.scale != core::Scale::Rgb) s[3] *= f;
    for (unsigned i = 0; i < 3; ++i) { const double v = s[i] * factor(law.src, s, i) + background[i] * factor(law.dst, s, i); out[i] = v > 1 ? 1 : v; }
}
LensState lens_state(IDirect3DDevice9* d, std::uint64_t hash) {
    LensState state; state.known = true; state.hash = hash;
    const std::pair<D3DRENDERSTATETYPE, std::uint32_t*> reads[] = {
        {D3DRS_ALPHABLENDENABLE, &state.blend.enable}, {D3DRS_SRCBLEND, &state.blend.src}, {D3DRS_DESTBLEND, &state.blend.dst}, {D3DRS_BLENDOP, &state.blend.op},
        {D3DRS_SRGBWRITEENABLE, &state.blend.srgb_write}, {D3DRS_ALPHATESTENABLE, &state.blend.alpha_test}, {D3DRS_ALPHAREF, &state.blend.alpha_ref},
        {D3DRS_ALPHAFUNC, &state.blend.alpha_func}, {D3DRS_FOGENABLE, &state.blend.fog}};
    for (const auto& r : reads) { DWORD v = 0; check("lens state", d->GetRenderState(r.first, &v)); *r.second = v; }
    check("lens shader", d->GetPixelShader(&state.shader)); if (state.shader) state.shader->Release(); // the binding keeps it alive
    return state;
}
struct LensRig {
    IDirect3DDevice9* d; SunOcclusionPass& pass; Bindings& bindings; Canvas& canvas;
    Com<IDirect3DPixelShader9> ps20, ps30, ps11; Com<IDirect3DVertexShader9> quad_vs; Com<IDirect3DVertexDeclaration9> quad_decl;
    // The application's state for one lens draw: hostile first, then what a blended sprite draw sets itself.
    void prepare(bool sm3, const Law& law) {
        bindings.hostile(d);
        for (UINT i = 1; i < 4; ++i) check("lens rt off", d->SetRenderTarget(i, nullptr));
        check("lens rt", d->SetRenderTarget(0, canvas.surface.p)); check("lens depth", d->SetDepthStencilSurface(nullptr));
        check("lens clear", d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_COLORVALUE(float(background[0]), float(background[1]), float(background[2]), float(background[3])), 1.f, 0));
        for (auto [s, v] : {std::pair{D3DRS_ALPHABLENDENABLE, DWORD(TRUE)}, std::pair{D3DRS_SRCBLEND, DWORD(law.src)}, std::pair{D3DRS_DESTBLEND, DWORD(law.dst)}, std::pair{D3DRS_BLENDOP, DWORD(D3DBLENDOP_ADD)},
                            std::pair{D3DRS_SCISSORTESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_ZENABLE, DWORD(FALSE)}, std::pair{D3DRS_COLORWRITEENABLE, DWORD(15)}, std::pair{D3DRS_CULLMODE, DWORD(D3DCULL_NONE)},
                            std::pair{D3DRS_FILLMODE, DWORD(D3DFILL_SOLID)}, std::pair{D3DRS_SRGBWRITEENABLE, DWORD(FALSE)}, std::pair{D3DRS_FOGENABLE, DWORD(FALSE)}, std::pair{D3DRS_ALPHATESTENABLE, DWORD(FALSE)},
                            std::pair{D3DRS_STENCILENABLE, DWORD(FALSE)}, std::pair{D3DRS_MULTISAMPLEMASK, DWORD(0xffffffff)}, std::pair{D3DRS_CLIPPING, DWORD(FALSE)}})
            check("lens rs", d->SetRenderState(s, v));
        if (sm3) { check("lens decl", d->SetVertexDeclaration(quad_decl.p)); check("lens vs", d->SetVertexShader(quad_vs.p)); check("lens ps", d->SetPixelShader(ps30.p)); }
        else { check("lens fvf", d->SetFVF(quad_fvf)); check("lens vs", d->SetVertexShader(nullptr)); check("lens ps", d->SetPixelShader(ps20.p)); }
    }
    void draw(bool sm3) {
        check("lens begin scene", d->BeginScene());
        if (sm3) { QuadVertex v[4]; quad_vertices(16, 16, v); check("lens draw", d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(QuadVertex))); }
        else { const float v[4][6] = {{-.5f, -.5f, 0, 1, 0, 0}, {15.5f, -.5f, 0, 1, 1, 0}, {-.5f, 15.5f, 0, 1, 0, 1}, {15.5f, 15.5f, 0, 1, 1, 1}}; check("lens draw", d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, 24)); }
        check("lens end scene", d->EndScene());
    }
};
} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        WNDCLASSA cls{}; cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(nullptr); cls.lpszClassName = "X3SunOcclusionFixture"; RegisterClassA(&cls);
        HWND window = CreateWindowA(cls.lpszClassName, "X3 sun occlusion fixture", WS_OVERLAPPEDWINDOW, 90, 90, 128, 128, nullptr, nullptr, cls.hInstance, nullptr);
        if (!window) throw std::runtime_error("window");
        HMODULE runtime = LoadLibraryA("d3d9.dll"); if (!runtime) throw std::runtime_error("d3d9.dll");
        auto address = GetProcAddress(runtime, "Direct3DCreate9"); IDirect3D9*(WINAPI* create)(UINT) = nullptr; std::memcpy(&create, &address, sizeof create);
        if (!create) throw std::runtime_error("Direct3DCreate9");
        Com<IDirect3D9> api; api.p = create(D3D_SDK_VERSION); if (!api.p) throw std::runtime_error("Create9");
        const unsigned W = 320, H = 180;
        D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window; pp.BackBufferWidth = W; pp.BackBufferHeight = H;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE; pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8;
        Com<IDirect3DDevice9> device; check("CreateDevice", api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p));
        IDirect3DDevice9* d = device.p;
        D3DCAPS9 caps{}; check("caps", d->GetDeviceCaps(&caps));
        D3DDISPLAYMODE mode{}; check("display mode", api->GetAdapterDisplayMode(0, &mode));
        std::memcpy(original, *reinterpret_cast<void***>(d), sizeof original); std::memcpy(hooked, original, sizeof hooked);
        hooked[83] = reinterpret_cast<void*>(&hook_draw); hooked[106] = reinterpret_cast<void*>(&hook_create_ps); hooked[107] = reinterpret_cast<void*>(&hook_set_ps);
        { SunOcclusionPass twin_pass; D3DCAPS9 c = caps; c.PixelShaderVersion = D3DPS_VERSION(2, 0);
          require("twin_ps_2_0_refused", FAILED(twin_pass.attach(d, hooked, c, mode.Format)) && std::string(twin_pass.caps().reason) == "shader_model" && twin_pass.references() == 0);
          c = caps; c.MaxPixelShader30InstructionSlots = 8;
          require("twin_ps_slots_refused", FAILED(twin_pass.attach(d, hooked, c, mode.Format)) && std::string(twin_pass.caps().reason) == "ps_slots" && twin_pass.references() == 0); }
        SunOcclusionPass pass;
        require("attach", SUCCEEDED(pass.attach(d, hooked, caps, mode.Format)) && pass.caps().enabled && pass.references() == 3);
        std::printf("ATTACH slots=%u ps30_slots=%u\n", pass.caps().program_slots, unsigned(caps.MaxPixelShader30InstructionSlots));
        const float u = .5f, v = .5f, ru = .1f, rv = ru * float(W) / float(H);
        {
            Bindings bindings(d, W, H); Depth depth(d, W, H, D3DFMT_R32F); Canvas canvas(d);
            Rig rig{d, pass, bindings};
            SunVisibilityResult out; HRESULT hr = S_OK; unsigned execute_calls = 0;
            auto scene = [&](const char* label, int column, float cu, float cv, float r_u, float r_v, double geometric, double geometric_tolerance) {
                depth.edge(d, column);
                const bool restored = rig.run(frame_of(depth, cu, cv, r_u, r_v, true), out, hr);
                const Read r = rig.read(); const Twin t = twin(W, column, cu, cv, r_u, r_v);
                std::printf("SCENE %s column=%d twin_open=%u twin_valid=%u raw=%.5f smoothed=%.5f used=%.5f valid=%.5f\n", label, column, t.open, t.valid, r.f[2], r.f[0], r.f[1], r.f[3]);
                require((std::string(label) + "_ran_restored").c_str(), hr == S_OK && out.ran && out.seeded && restored && r.ok);
                execute_calls = out.device_calls;
                require((std::string(label) + "_twin").c_str(), close_to(r.f[2], t.raw(), 1. / 32 + 2e-3) && close_to(r.f[3], t.valid / 32., 1e-3));
                require((std::string(label) + "_geometric").c_str(), close_to(r.f[2], geometric, geometric_tolerance));
                require((std::string(label) + "_seed_and_band").c_str(), close_to(r.f[0], r.f[2], 1e-3) && close_to(r.f[1], band(r.f[0]), 3e-3));
                return r;
            };
            { const Read r = scene("open", int(W), u, v, ru, rv, 1., 0.); require("open_exact_one", r.f[0] == 1.f && r.f[1] == 1.f && r.f[3] == 1.f); }
            { const Read r = scene("covered", 0, u, v, ru, rv, 0., 0.); require("covered_exact_zero", r.f[0] == 0.f && r.f[1] == 0.f && r.f[2] == 0.f); }
            scene("half", int(W / 2), u, v, ru, rv, .5, 1. / 32 + 2e-3);
            scene("three_quarter", int(W / 2 + .5f * ru * W), u, v, ru, rv, .8045, 2. / 32);
            scene("quarter", int(W / 2 - .5f * ru * W), u, v, ru, rv, .1955, 2. / 32);
            { const Read r = scene("screen_edge_open", int(W), 0.f, v, ru, rv, 1., 0.); require("screen_edge_valid_about_half", r.f[3] > .3f && r.f[3] < .7f); }
            { const Read r = scene("screen_edge_covered", 0, 0.f, v, ru, rv, 0., 0.); require("screen_edge_covered_zero", r.f[1] == 0.f); }
            // No valid tap at all: a seed answers 1, a smoothed frame keeps the history.
            { depth.edge(d, 0);
              bool restored = rig.run(frame_of(depth, -.5f, v, ru, rv, true), out, hr); Read r = rig.read();
              require("offscreen_seed_one", hr == S_OK && out.ran && restored && r.f[0] == 1.f && r.f[3] == 0.f);
              rig.run(frame_of(depth, u, v, ru, rv, true), out, hr); // history := 0 (covered)
              restored = rig.run(frame_of(depth, -.5f, v, ru, rv, false, .5f), out, hr); r = rig.read();
              require("offscreen_keeps_history", hr == S_OK && out.ran && !out.seeded && restored && r.f[0] == 0.f && r.f[1] == 0.f && r.f[3] == 0.f); }
            // A record's first frame has no radius: refused untouched; the caller's fallback radius runs.
            { depth.edge(d, int(W));
              const bool restored = rig.run(frame_of(depth, u, v, 0.f, 0.f, true), out, hr);
              require("radius_zero_skipped", hr == S_FALSE && out.skipped && std::string(out.skipped_reason) == "params" && restored && pass.valid());
              core::Footprint disc; disc.u = u; disc.v = v; core::apply_radius(disc, .012f, W, H);
              rig.run(frame_of(depth, disc.u, disc.v, disc.radius_u, disc.radius_v, true), out, hr); const Read r = rig.read();
              require("radius_fallback_runs", hr == S_OK && out.ran && close_to(disc.radius_u, .012, 1e-6) && close_to(disc.radius_v, .012 * W / H, 1e-6) && r.f[0] == 1.f); }
            // Temporal step response: open, then covered at alpha 0.25: (3/4)^n, then the dead band's exact ends.
            { depth.edge(d, int(W)); rig.run(frame_of(depth, u, v, ru, rv, true), out, hr);
              depth.edge(d, 0); bool ok = true, restored = true; double expected = 1.;
              for (unsigned n = 1; n <= 8; ++n) {
                  restored = rig.run(frame_of(depth, u, v, ru, rv, false, .25f), out, hr) && restored; const Read r = rig.read(); expected *= .75;
                  std::printf("STEP n=%u smoothed=%.5f expected=%.5f used=%.5f\n", n, r.f[0], expected, r.f[1]);
                  ok = ok && hr == S_OK && !out.seeded && close_to(r.f[0], expected, 2e-3) && close_to(r.f[1], band(r.f[0]), 3e-3) && r.f[2] == 0.f;
              }
              require("step_response_down", ok && restored);
              unsigned frames = 0; Read r{};
              for (; frames < 64; ++frames) { rig.run(frame_of(depth, u, v, ru, rv, false, .25f), out, hr); r = rig.read(); if (r.f[1] == 0.f) break; }
              require("fade_reaches_exact_zero", frames < 20 && r.f[1] == 0.f);
              depth.edge(d, int(W));
              for (frames = 0; frames < 64; ++frames) { rig.run(frame_of(depth, u, v, ru, rv, false, .25f), out, hr); r = rig.read(); if (r.f[1] == 1.f) break; }
              std::printf("RISE frames=%u smoothed=%.5f used=%.5f\n", frames, r.f[0], r.f[1]);
              require("fade_reaches_exact_one", frames < 20 && r.f[1] == 1.f && r.f[0] >= .97f); }
            { depth.edge(d, int(W / 2)); rig.run(frame_of(depth, u, v, ru, rv, true, 1.f, 2.f), out, hr); const Read r = rig.read();
              require("curve_exponent", hr == S_OK && close_to(r.f[1], band(r.f[0], 2.), 3e-3)); }
            // Formats of RT2.
            for (auto [format, label] : {std::pair{D3DFMT_A32B32G32R32F, "format_a32b32g32r32f"}, std::pair{D3DFMT_G32R32F, "format_g32r32f"}}) {
                if (FAILED(api->CheckDeviceFormat(0, D3DDEVTYPE_HAL, mode.Format, 0, D3DRTYPE_TEXTURE, format))) { std::printf("SKIPPED %s unsupported\n", label); continue; }
                Depth other(d, W, H, format); other.edge(d, int(W / 2));
                const bool restored = rig.run(frame_of(other, u, v, ru, rv, true), out, hr); const Read r = rig.read();
                require(label, hr == S_OK && out.ran && restored && close_to(r.f[2], twin(W, int(W / 2), u, v, ru, rv).raw(), 1. / 32 + 2e-3));
            }
            { Depth wrong(d, W, H, D3DFMT_A8R8G8B8); SunVisibilityFrame in = frame_of(wrong, u, v, ru, rv, true);
              bool restored = rig.run(in, out, hr); require("format_refused", hr == S_FALSE && out.skipped && std::string(out.skipped_reason) == "format" && restored);
              in = frame_of(depth, u, v, ru, rv, true); in.caller_stateblock_recording = true;
              restored = rig.run(in, out, hr); require("recording_refused", hr == S_FALSE && out.skipped && std::string(out.skipped_reason) == "input" && restored);
              in = frame_of(depth, u, v, ru, rv, true); in.alpha = NAN;
              restored = rig.run(in, out, hr); require("nan_refused", hr == S_FALSE && std::string(out.skipped_reason) == "params" && restored); }
            // A failed draw: restored, the fraction invalid, lens draws not ready, the next frame seeds by itself.
            { faults = {}; faults.draw_fail_at = 1;
              const bool restored = rig.run(frame_of(depth, u, v, ru, rv, false, .25f), out, hr); faults = {};
              LensDraw lens; const LensVerdict verdict = pass.lens_begin(lens_state(d, 1), lens);
              require("draw_fault_restored", FAILED(hr) && out.failed == SunOcclusionStage::Draw && SUCCEEDED(out.restore) && restored && !pass.valid() && verdict == LensVerdict::NotReady && !lens.applied);
              depth.edge(d, 0); const bool again = rig.run(frame_of(depth, u, v, ru, rv, false, .25f), out, hr); const Read r = rig.read();
              require("draw_fault_recovery_seeds", hr == S_OK && out.ran && out.seeded && again && r.f[0] == 0.f); }

            // ---- lens draws at a fraction near one half ----
            depth.edge(d, int(W / 2)); rig.run(frame_of(depth, u, v, ru, rv, true), out, hr);
            const double f = rig.read().f[1];
            std::printf("LENS fraction=%.5f\n", f);
            require("lens_fraction_mid", f > .35 && f < .65);
            LensRig lens{d, pass, bindings, canvas, {}, {}, {}, {}, {}};
            check("ps20", d->CreatePixelShader(ps20_words, &lens.ps20.p)); check("ps30", d->CreatePixelShader(ps30_words, &lens.ps30.p)); check("ps11", d->CreatePixelShader(ps11_words, &lens.ps11.p));
            check("quad vs", d->CreateVertexShader(reinterpret_cast<const DWORD*>(quad_vertex_program()), &lens.quad_vs.p)); check("quad decl", d->CreateVertexDeclaration(quad_declaration, &lens.quad_decl.p));
            unsigned lens_calls = 0;
            for (bool sm3 : {false, true}) for (const Law& law : laws) {
                const std::string name = std::string(sm3 ? "ps30_" : "ps20_") + law.name;
                double plain[4], wrapped[4], want_plain[3], want_wrapped[3];
                lens.prepare(sm3, law); lens.draw(sm3); canvas.pixel(d, plain);
                lens.prepare(sm3, law);
                const Snapshot before(d);
                LensDraw draw; const LensVerdict verdict = pass.lens_begin(lens_state(d, sm3 ? 30 : 20), draw); const unsigned begin_calls = pass.last_device_calls();
                IDirect3DPixelShader9* bound = nullptr; d->GetPixelShader(&bound); const bool substituted = bound && bound != (sm3 ? lens.ps30.p : lens.ps20.p); if (bound) bound->Release();
                lens.draw(sm3);
                const HRESULT ended = pass.lens_end(draw); lens_calls = pass.last_device_calls(); (void)begin_calls;
                const Snapshot after(d);
                canvas.pixel(d, wrapped);
                Law identity = law; expected_pixel(identity, 1., want_plain); expected_pixel(law, f, want_wrapped);
                bool plain_ok = true, wrapped_ok = true;
                for (unsigned i = 0; i < 3; ++i) { plain_ok = plain_ok && close_to(plain[i], want_plain[i], 2.5 / 255); wrapped_ok = wrapped_ok && close_to(wrapped[i], want_wrapped[i], 2.5 / 255); }
                std::printf("LENS_DRAW %s plain=%.4f,%.4f,%.4f wrapped=%.4f,%.4f,%.4f expected=%.4f,%.4f,%.4f sampler=%u\n", name.c_str(), plain[0], plain[1], plain[2], wrapped[0], wrapped[1], wrapped[2],
                            want_wrapped[0], want_wrapped[1], want_wrapped[2], unsigned(draw.sampler));
                require((name + "_wrapped").c_str(), verdict == LensVerdict::Applied && substituted && SUCCEEDED(ended) && plain_ok && wrapped_ok);
                require((name + "_state_restored").c_str(), before == after);
            }
            // Built once per program and scale mode, never per draw.
            { const unsigned created = faults.create_ps_calls, variants = pass.variants();
              for (unsigned i = 0; i < 5; ++i) { lens.prepare(false, laws[0]); LensDraw draw; pass.lens_begin(lens_state(d, 20), draw); lens.draw(false); pass.lens_end(draw); }
              require("lens_no_per_draw_creation", faults.create_ps_calls == created && pass.variants() == variants && variants == 2);
              std::printf("LENS_VARIANTS programs=%u shaders_created=%u\n", variants, faults.create_ps_calls);
              std::printf("CALLS execute=%u lens_draw=%u\n", execute_calls, lens_calls);
              require("device_calls_bounded", execute_calls > 0 && execute_calls <= 40 && lens_calls > 0 && lens_calls <= 6); }
            // Refusals: nothing changes.
            auto untouched = [](LensState&) {};
            auto refusal = [&](const char* label, LensVerdict expected, std::uint64_t hash, auto&& arrange, auto&& tweak) {
                lens.prepare(false, laws[0]); arrange();
                const Snapshot before(d); LensDraw draw; LensState state = lens_state(d, hash); tweak(state); const LensVerdict verdict = pass.lens_begin(state, draw); const Snapshot after(d);
                require(label, verdict == expected && !draw.applied && before == after);
            };
            refusal("refuse_blend_off", LensVerdict::Blend, 20, [&] { d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); }, untouched);
            refusal("refuse_blend_law", LensVerdict::Blend, 20, [&] { d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_DESTCOLOR); }, untouched);
            refusal("refuse_srgb_write", LensVerdict::Blend, 20, [&] { d->SetRenderState(D3DRS_SRGBWRITEENABLE, TRUE); }, untouched);
            refusal("refuse_alpha_test", LensVerdict::Blend, 20, [&] { d->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE); d->SetRenderState(D3DRS_ALPHAREF, 128); d->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER); }, untouched);
            refusal("refuse_fixed_function", LensVerdict::NoShader, 20, [&] { d->SetPixelShader(nullptr); }, untouched);
            refusal("refuse_unhashed", LensVerdict::Unhashed, 0, [] {}, untouched);
            refusal("refuse_ps_1_1", LensVerdict::Variant, 11, [&] { d->SetPixelShader(lens.ps11.p); }, untouched);
            refusal("refuse_fog", LensVerdict::Blend, 20, [&] { d->SetRenderState(D3DRS_FOGENABLE, TRUE); }, untouched);
            refusal("refuse_state_unknown", LensVerdict::Unhashed, 20, [] {}, [](LensState& state) { state.known = false; });
            require("refuse_ps_1_1_reason", std::string(pass.last_variant_refusal()) == "unsupported_version");
            { lens.prepare(false, laws[0]); faults.set_ps_calls = 0; faults.set_ps_fail_at = 1;
              const Snapshot before(d); LensDraw draw; const LensVerdict verdict = pass.lens_begin(lens_state(d, 20), draw); const Snapshot after(d); faults = {};
              require("lens_bind_fault_restored", verdict == LensVerdict::Device && !draw.applied && before == after); }
            // ---- Reset: the DEFAULT-pool targets go, programs and wraps stay ----
            const unsigned held = pass.references();
            pass.before_reset();
            require("before_reset_releases_targets", pass.reset_pending() && pass.references() == held - 9 && !pass.valid()); // two textures, two surfaces, the quad's two blocks, sampler 15's two lens blocks, the diagnostic readback surface
            std::printf("RESET held_before=%u held_after=%u\n", held, pass.references());
            { SunVisibilityResult refused; require("reset_pending_refused", pass.execute(frame_of(depth, u, v, ru, rv, true), &refused) == S_FALSE && std::string(refused.skipped_reason) == "reset_pending"); }
            for (UINT i = 0; i < 16; ++i) d->SetTexture(i, nullptr);
            for (UINT i = 1; i < 4; ++i) d->SetRenderTarget(i, nullptr);
            d->SetPixelShader(nullptr); d->SetVertexShader(nullptr);
        } // every fixture DEFAULT-pool object released
        {
            Com<IDirect3DSurface9> backbuffer; check("backbuffer", d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer.p)); d->SetRenderTarget(0, backbuffer.p); backbuffer.reset();
            const HRESULT reset = d->Reset(&pp);
            pass.after_reset(reset);
            require("native_reset", SUCCEEDED(reset) && !pass.reset_pending());
            Bindings bindings(d, W, H); Depth depth(d, W, H, D3DFMT_R32F); Rig rig{d, pass, bindings};
            depth.edge(d, int(W / 2));
            SunVisibilityResult out; HRESULT hr = S_OK;
            const bool restored = rig.run(frame_of(depth, u, v, ru, rv, false, .25f), out, hr); const Read r = rig.read();
            require("after_reset_seeds_and_matches", hr == S_OK && out.ran && out.seeded && restored && close_to(r.f[2], twin(W, int(W / 2), u, v, ru, rv).raw(), 1. / 32 + 2e-3));
            for (UINT i = 0; i < 16; ++i) d->SetTexture(i, nullptr);
            for (UINT i = 1; i < 4; ++i) d->SetRenderTarget(i, nullptr);
        }
        pass.detach();
        require("detach_releases_everything", pass.references() == 0 && !pass.caps().enabled);
        std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL exception=%s checks=%u failures=%u\n", e.what(), checks, failures);
        return 2;
    }
}
