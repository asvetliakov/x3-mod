// Detached GPU fixture of renderer::SunOcclusionPass (docs/architecture/sun-partial-occlusion.md,
// "Fixtures"): the 1x1 visibility fraction against a CPU twin of the 32 taps over synthetic RT2
// scenes (open, covered, half-plane edges, off-screen, no valid tap, first-frame radius), the
// temporal step response and the dead band, the three depth formats, a failed draw with recovery,
// the wrapped lens draw under every admitted blend law for ps_2_0 and ps_3_0 originals, every
// refusal, the step-2 clip pair (a core body over a depth edge: the covered half at the background,
// the open half at f, a soft edge of fifths; a ghost at f; another record's body untouched), exact
// state restoration around each transaction under hostile state, native Reset and teardown. Validation-only readback lives here, never in production (the pass's own readback()
// is its diagnostic one). Built by build_sun_occlusion.py; run by run_sun_occlusion.py.
#include <windows.h>
#include <d3d9.h>
#include "../../src/renderer/sun_occlusion_pass.h"
#include "../../src/renderer/quad_vertex_program.h"
#include <array>
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
struct Faults { unsigned draw_fail_at = 0, draw_calls = 0, set_ps_fail_at = 0, set_ps_calls = 0, create_ps_calls = 0, create_vs_calls = 0; } faults;
using DrawUpFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using CreatePsFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const DWORD*, IDirect3DPixelShader9**);
using SetPsFn = HRESULT(WINAPI*)(IDirect3DDevice9*, IDirect3DPixelShader9*);
using CreateVsFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const DWORD*, IDirect3DVertexShader9**);
HRESULT WINAPI hook_create_vs(IDirect3DDevice9* d, const DWORD* w, IDirect3DVertexShader9** out) { ++faults.create_vs_calls; return reinterpret_cast<CreateVsFn>(original[91])(d, w, out); }
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
        float c[128]{}; check("snapshot constants", d->GetPixelShaderConstantF(0, c, 32)); add(c); // c0..c31: the wraps' def registers (c28..c31) included
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
        for (UINT i : {0u, 1u, 14u, 15u}) {
            check("hostile texture", d->SetTexture(i, junk.p));
            check("hostile sampler", d->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR)); check("hostile sampler", d->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR));
            check("hostile sampler", d->SetSamplerState(i, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR));
            check("hostile sampler", d->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_BORDER)); check("hostile sampler", d->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_MIRROR));
            check("hostile sampler", d->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, TRUE)); check("hostile sampler", d->SetSamplerState(i, D3DSAMP_BORDERCOLOR, 0x00000000));
        }
        const float garbage[16] = {9, 8, 7, 6, 5, 4, 3, 2, 1, .5f, .25f, .125f, -1, -2, -3, -4};
        check("hostile constants", d->SetPixelShaderConstantF(0, garbage, 4));
        check("hostile constants high", d->SetPixelShaderConstantF(28, garbage, 4)); // the registers the clip wrap's defs name on a program using c0 only
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
// vs_2_0 with the lens scene's shape (run223 fingerprint d5e1c753...): dcl_position v0 / mov r0, v0 / dp4 oPos.{w,x,y,z}, r0, c{3,0,1,2}.
const DWORD vs20_words[] = {0xfffe0200, 0x0200001f, 0x80000000, 0x900f0000, 0x02000001, 0x800f0000, 0x90e40000,
                            0x03000009, 0xc0080000, 0x80e40000, 0xa0e40003, 0x03000009, 0xc0010000, 0x80e40000, 0xa0e40000,
                            0x03000009, 0xc0020000, 0x80e40000, 0xa0e40001, 0x03000009, 0xc0040000, 0x80e40000, 0xa0e40002, 0x0000ffff};
// The same with `mul r0, r0, c4` after the mov: the origin is no longer (v.xyz, 1) (c4 is left at the device default 0, the draw is never issued).
const DWORD vs20_scaled_words[] = {0xfffe0200, 0x0200001f, 0x80000000, 0x900f0000, 0x02000001, 0x800f0000, 0x90e40000, 0x03000005, 0x800f0000, 0x80e40000, 0xa0e40004,
                                   0x03000009, 0xc0080000, 0x80e40000, 0xa0e40003, 0x03000009, 0xc0010000, 0x80e40000, 0xa0e40000,
                                   0x03000009, 0xc0020000, 0x80e40000, 0xa0e40001, 0x03000009, 0xc0040000, 0x80e40000, 0xa0e40002, 0x0000ffff};
const D3DVERTEXELEMENT9 clip_declaration[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
// A render target of any size with a row reader (the clip test runs at RT2's size so the soft edge is in pixels).
struct Sheet {
    unsigned w, h; Com<IDirect3DTexture9> texture; Com<IDirect3DSurface9> surface, sink;
    Sheet(IDirect3DDevice9* d, unsigned width, unsigned height) : w(width), h(height) {
        check("sheet", d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &texture.p, nullptr)); check("sheet surface", texture->GetSurfaceLevel(0, &surface.p));
        check("sheet sink", d->CreateOffscreenPlainSurface(w, h, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sink.p, nullptr));
    }
    std::vector<std::array<double, 3>> row(IDirect3DDevice9* d, unsigned y) {
        check("sheet read", d->GetRenderTargetData(surface.p, sink.p));
        D3DLOCKED_RECT lr{}; check("sheet lock", sink->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        std::vector<std::array<double, 3>> out(w);
        for (unsigned x = 0; x < w; ++x) { const DWORD c = static_cast<const DWORD*>(lr.pBits)[x + y * (lr.Pitch / 4)]; out[x] = {((c >> 16) & 255) / 255., ((c >> 8) & 255) / 255., (c & 255) / 255.}; }
        check("sheet unlock", sink->UnlockRect());
        return out;
    }
};
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
    LensState state; state.known = true; state.hash = hash; state.body = core::Body::Ghost; // a ghost: the step-1 wrap
    const std::pair<D3DRENDERSTATETYPE, std::uint32_t*> reads[] = {
        {D3DRS_ALPHABLENDENABLE, &state.blend.enable}, {D3DRS_SRCBLEND, &state.blend.src}, {D3DRS_DESTBLEND, &state.blend.dst}, {D3DRS_BLENDOP, &state.blend.op},
        {D3DRS_SRGBWRITEENABLE, &state.blend.srgb_write}, {D3DRS_ALPHATESTENABLE, &state.blend.alpha_test}, {D3DRS_ALPHAREF, &state.blend.alpha_ref},
        {D3DRS_ALPHAFUNC, &state.blend.alpha_func}, {D3DRS_FOGENABLE, &state.blend.fog}};
    for (const auto& r : reads) { DWORD v = 0; check("lens state", d->GetRenderState(r.first, &v)); *r.second = v; }
    check("lens shader", d->GetPixelShader(&state.shader)); if (state.shader) state.shader->Release(); // the binding keeps it alive
    return state;
}
// What a blended sprite draw sets itself.
void sprite_states(IDirect3DDevice9* d, const Law& law) {
    for (auto [s, v] : {std::pair{D3DRS_ALPHABLENDENABLE, DWORD(TRUE)}, std::pair{D3DRS_SRCBLEND, DWORD(law.src)}, std::pair{D3DRS_DESTBLEND, DWORD(law.dst)}, std::pair{D3DRS_BLENDOP, DWORD(D3DBLENDOP_ADD)},
                        std::pair{D3DRS_SCISSORTESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_ZENABLE, DWORD(FALSE)}, std::pair{D3DRS_COLORWRITEENABLE, DWORD(15)}, std::pair{D3DRS_CULLMODE, DWORD(D3DCULL_NONE)},
                        std::pair{D3DRS_FILLMODE, DWORD(D3DFILL_SOLID)}, std::pair{D3DRS_SRGBWRITEENABLE, DWORD(FALSE)}, std::pair{D3DRS_FOGENABLE, DWORD(FALSE)}, std::pair{D3DRS_ALPHATESTENABLE, DWORD(FALSE)},
                        std::pair{D3DRS_STENCILENABLE, DWORD(FALSE)}, std::pair{D3DRS_MULTISAMPLEMASK, DWORD(0xffffffff)}, std::pair{D3DRS_CLIPPING, DWORD(FALSE)}})
        check("lens rs", d->SetRenderState(s, v));
}
// The step-2 clip pair over a depth edge: the vs_2_0 above with identity clip rows, a clip-space quad over a sheet of RT2's size.
struct ClipRig {
    IDirect3DDevice9* d; SunOcclusionPass& pass; Bindings& bindings; Sheet& sheet; Depth& depth;
    IDirect3DVertexShader9* vs; IDirect3DVertexDeclaration9* declaration; IDirect3DPixelShader9* ps;
    void prepare(const Law& law) {
        bindings.hostile(d);
        for (UINT i = 1; i < 4; ++i) check("clip rt off", d->SetRenderTarget(i, nullptr));
        check("clip rt", d->SetRenderTarget(0, sheet.surface.p)); check("clip depth", d->SetDepthStencilSurface(nullptr));
        check("clip clear", d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_COLORVALUE(float(background[0]), float(background[1]), float(background[2]), float(background[3])), 1.f, 0));
        sprite_states(d, law);
        check("clip decl", d->SetVertexDeclaration(declaration)); check("clip vs", d->SetVertexShader(vs)); check("clip ps", d->SetPixelShader(ps));
        const float rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        check("clip rows", d->SetVertexShaderConstantF(0, rows, 4));
    }
    LensState state(core::Body body, bool with_depth = true) {
        LensState s = lens_state(d, 20); s.vertex_shader = vs; s.vertex_hash = 200; s.body = body;
        s.depth = with_depth ? depth.texture.p : nullptr; s.depth_width = depth.w; s.depth_height = depth.h;
        return s;
    }
    void draw() {
        check("clip begin scene", d->BeginScene());
        const float v[4][4] = {{-1, -1, .5f, 1}, {1, -1, .5f, 1}, {-1, 1, .5f, 1}, {1, 1, .5f, 1}};
        check("clip draw", d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, 16));
        check("clip end scene", d->EndScene());
    }
    // The fragment and the eight knight moves (two taps in each of the columns x +- 1, x +- 2), one ninth each: the fraction of
    // taps left of the covered column `edge` for pixel column x.
    static double open_fraction(int x, int edge) {
        unsigned open = x < edge;
        for (int dx : {-2, -1, 1, 2}) open += 2 * (x + dx < edge);
        return open / 9.;
    }
    // The former +-1 / +-2 cross (ninths), for the before / after step comparison.
    static double open_fraction_cross(int x, int edge) {
        unsigned open = 0;
        for (int t : {x, x + 1, x - 1, x + 2, x - 2, x, x, x, x}) open += t < edge;
        return open / 9.;
    }
};
// ---- RT2 on the jittered raster: an occluder quad drawn through the lens vertex program with the route's jittered rows ----
// The temporal pass's eight offsets (motion_jitter_sample: Halton bases 2 / 3, 1-based index, minus one half, in pixels).
float halton(unsigned index, unsigned base) { float fraction = 1.f, result = 0.f; while (index) { fraction /= float(base); result += fraction * float(index % base); index /= base; } return result; }
struct Phase { unsigned index; float jx, jy; };
std::array<Phase, 8> phases() { std::array<Phase, 8> out{}; for (unsigned i = 0; i < 8; ++i) out[i] = {i, halton(i + 1, 2) - .5f, halton(i + 1, 3) - .5f}; return out; }
// MotionOutput::apply_jitter's arithmetic (fade_region::jitter_rows), bit for bit: rows[k] += jx_ndc * rows[12 + k], rows[4 + k] += jy_ndc * rows[12 + k]
// with jx_ndc = 2 jx / W, jy_ndc = -2 jy / H: the raster image moves +jx px right and +jy px down.
void jitter_rows(float rows[16], float jx_px, float jy_px, unsigned width, unsigned height) {
    const float jx = 2.f * jx_px / float(width), jy = -2.f * jy_px / float(height);
    for (unsigned k = 0; k < 4; ++k) { rows[k] += jx * rows[12 + k]; rows[4 + k] += jy * rows[12 + k]; }
}
struct Raster {
    IDirect3DDevice9* d; unsigned w, h; IDirect3DVertexShader9* vs; IDirect3DVertexDeclaration9* declaration;
    Com<IDirect3DTexture9> texture; Com<IDirect3DSurface9> surface; Com<IDirect3DPixelShader9> open_ps, cover_ps;
    Raster(IDirect3DDevice9* device, unsigned width, unsigned height, IDirect3DVertexShader9* vertex, IDirect3DVertexDeclaration9* decl) : d(device), w(width), h(height), vs(vertex), declaration(decl) {
        check("raster", d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &texture.p, nullptr)); check("raster surface", texture->GetSurfaceLevel(0, &surface.p));
        DWORD words[sizeof ps20_words / sizeof ps20_words[0]]; std::memcpy(words, ps20_words, sizeof words);
        for (auto [value, out] : {std::pair{-1.f, &open_ps}, std::pair{.5f, &cover_ps}}) { DWORD bits = 0; std::memcpy(&bits, &value, 4); for (unsigned i = 3; i < 7; ++i) words[i] = bits; check("raster ps", d->CreatePixelShader(words, &out->p)); }
    }
    // The whole target open (-1), then the occluder (device depth 0.5) covering NDC x >= x_edge and y <= y_edge, drawn with the jittered rows.
    // RT0 stays bound to the raster afterwards; every rig re-binds RT0 before the texture is sampled.
    void draw(float jx, float jy, float x_edge, float y_edge) {
        for (UINT i = 1; i < 4; ++i) check("raster rt off", d->SetRenderTarget(i, nullptr));
        check("raster rt", d->SetRenderTarget(0, surface.p)); check("raster depth", d->SetDepthStencilSurface(nullptr));
        for (auto [s, v] : {std::pair{D3DRS_ALPHABLENDENABLE, DWORD(FALSE)}, std::pair{D3DRS_SCISSORTESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_ZENABLE, DWORD(FALSE)}, std::pair{D3DRS_COLORWRITEENABLE, DWORD(15)},
                            std::pair{D3DRS_CULLMODE, DWORD(D3DCULL_NONE)}, std::pair{D3DRS_FILLMODE, DWORD(D3DFILL_SOLID)}, std::pair{D3DRS_SRGBWRITEENABLE, DWORD(FALSE)}, std::pair{D3DRS_FOGENABLE, DWORD(FALSE)},
                            std::pair{D3DRS_ALPHATESTENABLE, DWORD(FALSE)}, std::pair{D3DRS_STENCILENABLE, DWORD(FALSE)}, std::pair{D3DRS_MULTISAMPLEMASK, DWORD(0xffffffff)}, std::pair{D3DRS_CLIPPING, DWORD(FALSE)}})
            check("raster rs", d->SetRenderState(s, v));
        check("raster decl", d->SetVertexDeclaration(declaration)); check("raster vs", d->SetVertexShader(vs));
        float rows[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        jitter_rows(rows, jx, jy, w, h);
        check("raster rows", d->SetVertexShaderConstantF(0, rows, 4));
        check("raster begin", d->BeginScene());
        const float open[4][4] = {{-1, -1, .5f, 1}, {1, -1, .5f, 1}, {-1, 1, .5f, 1}, {1, 1, .5f, 1}};
        check("raster open ps", d->SetPixelShader(open_ps.p)); check("raster open", d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, open, 16));
        const float cover[4][4] = {{x_edge, -1, .5f, 1}, {1, -1, .5f, 1}, {x_edge, y_edge, .5f, 1}, {1, y_edge, .5f, 1}};
        check("raster cover ps", d->SetPixelShader(cover_ps.p)); check("raster cover", d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, cover, 16));
        check("raster end", d->EndScene());
    }
};
// An edge at texel coordinate e (covered from e on, along x or y): texel k of the jittered raster holds the coverage at k + 0.5 - j.
// The tap at texel coordinate t reads texel floor(t + j) with the correction, floor(t) without.
unsigned twin_open_jittered(const float* coordinates, unsigned count, double edge, double j, bool corrected) {
    unsigned open = 0;
    for (unsigned i = 0; i < count; ++i) { const double k = std::floor(double(coordinates[i]) + (corrected ? j : 0.)); open += (k + .5 - j) < edge; }
    return open;
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
        sprite_states(d, law);
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
        hooked[83] = reinterpret_cast<void*>(&hook_draw); hooked[106] = reinterpret_cast<void*>(&hook_create_ps); hooked[107] = reinterpret_cast<void*>(&hook_set_ps); hooked[91] = reinterpret_cast<void*>(&hook_create_vs);
        Com<IDirect3DVertexShader9> vs20; Com<IDirect3DVertexDeclaration9> clip_decl;
        check("vs20", d->CreateVertexShader(vs20_words, &vs20.p)); check("clip declaration", d->CreateVertexDeclaration(clip_declaration, &clip_decl.p));
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
            // ---- step 2: the clip pair over the depth edge at W / 2 (f is the mid fraction above) ----
            Sheet sheet(d, W, H);
            ClipRig clip{d, pass, bindings, sheet, depth, vs20.p, clip_decl.p, lens.ps20.p};
            const int edge = int(W / 2);
            unsigned clip_calls = 0;
            { SunOcclusionPass::Prepared prepared; const unsigned ps_created = faults.create_ps_calls, vs_created = faults.create_vs_calls;
              const LensVerdict scanned = pass.lens_prepare(clip.state(core::Body::Core), &prepared);
              require("clip_prepare_scans_without_creating", scanned == LensVerdict::Applied && prepared.matrix_register == 0 && prepared.origin_known && prepared.first && pass.pairs() == 1 &&
                                                            faults.create_ps_calls == ps_created && faults.create_vs_calls == vs_created);
              require("clip_prepare_second_time_not_first", pass.lens_prepare(clip.state(core::Body::Core), &prepared) == LensVerdict::Applied && !prepared.first && prepared.origin_known);
              LensState no_vs = clip.state(core::Body::Core); no_vs.vertex_shader = nullptr;
              require("clip_prepare_needs_vertex_shader", pass.lens_prepare(no_vs, &prepared) == LensVerdict::NoShader && prepared.matrix_register == ~0u);
              LensState no_vs_hash = clip.state(core::Body::Core); no_vs_hash.vertex_hash = 0;
              require("clip_prepare_needs_vertex_hash", pass.lens_prepare(no_vs_hash, &prepared) == LensVerdict::Unhashed);
              // A vertex program whose origin is not the rows' .w column: scanned (origin_known = 0), and a core draw through it is refused, nothing created.
              Com<IDirect3DVertexShader9> scaled; check("scaled vs", d->CreateVertexShader(vs20_scaled_words, &scaled.p));
              LensState other = clip.state(core::Body::Core); other.vertex_shader = scaled.p; other.vertex_hash = 201;
              require("clip_prepare_origin_unknown", pass.lens_prepare(other, &prepared) == LensVerdict::Applied && prepared.first && !prepared.origin_known && prepared.matrix_register == 0 && pass.pairs() == 2);
              clip.prepare(laws[2]); const Snapshot before(d); LensDraw draw; const LensVerdict verdict = pass.lens_begin(other, draw); const Snapshot after(d);
              require("clip_core_origin_unknown_refused", verdict == LensVerdict::Variant && !draw.applied && before == after && faults.create_vs_calls == vs_created); }
            // Default: a core body is clipped only (out *= open_px); with core_f it is clipped and scaled (out *= open_px * f).
            for (unsigned pass_core_f = 0; pass_core_f < 2; ++pass_core_f) for (const Law* law : (pass_core_f ? std::vector<const Law*>{&laws[2]} : std::vector<const Law*>{&laws[2], &laws[0], &laws[4]})) { // rgb, alpha and both scales
                pass.set_core_fraction(pass_core_f != 0); const double body_f = pass_core_f ? f : 1.;
                clip.prepare(*law);
                const Snapshot before(d);
                LensDraw draw; const LensVerdict verdict = pass.lens_begin(clip.state(core::Body::Core), draw);
                IDirect3DVertexShader9* bound_vs = nullptr; d->GetVertexShader(&bound_vs); const bool vs_substituted = bound_vs && bound_vs != vs20.p; if (bound_vs) bound_vs->Release();
                IDirect3DPixelShader9* bound_ps = nullptr; d->GetPixelShader(&bound_ps); const bool ps_substituted = bound_ps && bound_ps != lens.ps20.p; if (bound_ps) bound_ps->Release();
                IDirect3DBaseTexture9* on_depth = nullptr; d->GetTexture(draw.depth_sampler, &on_depth); const bool depth_bound = on_depth == depth.texture.p; if (on_depth) on_depth->Release();
                const bool clipped = draw.clipped;
                clip.draw();
                const HRESULT ended = pass.lens_end(draw); clip_calls = pass.last_device_calls();
                const Snapshot after(d);
                const auto row = sheet.row(d, H / 2);
                bool far_open = true, far_covered = true, soft = true; unsigned soft_count = 0;
                auto matches = [&](int x, double open) { double want[3]; expected_pixel(*law, body_f * open, want); for (unsigned c = 0; c < 3; ++c) if (!close_to(row[x][c], want[c], 2.5 / 255)) return false; return true; };
                for (int x = 0; x < edge - 2; ++x) far_open = matches(x, 1.) && far_open;
                for (int x = edge + 2; x < int(W); ++x) far_covered = matches(x, 0.) && far_covered;
                // The soft edge: every tap sits on a texel centre, so columns edge-2 .. edge+1 are exact ninths (7, 5, 4, 2).
                for (int x = edge - 2; x <= edge + 1; ++x) { soft = matches(x, ClipRig::open_fraction(x, edge)) && soft; soft_count += !matches(x, 1.) && !matches(x, 0.); }
                std::printf("CLIP %s%s f=%.4f verdict=%s clipped=%u samplers=%u/%u calls=%u open=%.4f,%.4f,%.4f edge=%.4f,%.4f,%.4f covered=%.4f,%.4f,%.4f model=%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n", law->name, pass_core_f ? "_core_f" : "", f, lens_verdict_name(verdict), clipped ? 1u : 0u,
                            unsigned(draw.sampler), unsigned(draw.depth_sampler), clip_calls, row[edge - 3][0], row[edge - 3][1], row[edge - 3][2], row[edge - 1][0], row[edge][0], row[edge + 1][0],
                            row[edge + 3][0], row[edge + 3][1], row[edge + 3][2], ClipRig::open_fraction(edge - 3, edge), ClipRig::open_fraction(edge - 2, edge), ClipRig::open_fraction(edge - 1, edge),
                            ClipRig::open_fraction(edge, edge), ClipRig::open_fraction(edge + 1, edge), ClipRig::open_fraction(edge + 2, edge));
                const std::string name = std::string("clip_core_") + law->name + (pass_core_f ? "_core_f" : "");
                require((name + "_applied").c_str(), verdict == LensVerdict::Applied && clipped && SUCCEEDED(ended) && vs_substituted && ps_substituted && depth_bound && draw.sampler != draw.depth_sampler);
                require((name + (pass_core_f ? "_open_half_at_f" : "_open_half_unscaled")).c_str(), far_open);
                require((name + "_covered_half_background").c_str(), far_covered);
                require((name + "_soft_edge").c_str(), soft && soft_count == 4);
                require((name + "_state_restored").c_str(), before == after);
            }
            pass.set_core_fraction(false);
            require("clip_calls_seven", clip_calls == 7);
            // A ghost: the step-1 wrap, uniform f over the whole row, five calls; RT2 untouched on its sampler.
            { clip.prepare(laws[2]);
              const Snapshot before(d); LensDraw draw; const LensVerdict verdict = pass.lens_begin(clip.state(core::Body::Ghost), draw); const bool clipped = draw.clipped;
              clip.draw(); const HRESULT ended = pass.lens_end(draw); const unsigned calls = pass.last_device_calls(); const Snapshot after(d);
              const auto row = sheet.row(d, H / 2); double want[3]; expected_pixel(laws[2], f, want); bool uniform = true;
              for (unsigned x = 0; x < W; x += 7) for (unsigned c = 0; c < 3; ++c) uniform = uniform && close_to(row[x][c], want[c], 2.5 / 255);
              require("clip_ghost_uniform_f", verdict == LensVerdict::Applied && !clipped && SUCCEEDED(ended) && uniform && calls == 5 && before == after); }
            // Another record's body, an unclassified body: nothing touched; a core body without RT2: refused.
            for (auto [body, label] : {std::pair{core::Body::Other, "clip_other_untouched"}, std::pair{core::Body::Unknown, "clip_unknown_refused"}}) {
                clip.prepare(laws[2]); const Snapshot before(d); LensDraw draw; const LensVerdict verdict = pass.lens_begin(clip.state(body), draw); const Snapshot after(d);
                require(label, verdict == LensVerdict::Body && !draw.applied && before == after);
            }
            { clip.prepare(laws[2]); const Snapshot before(d); LensDraw draw; const LensVerdict verdict = pass.lens_begin(clip.state(core::Body::Core, false), draw); const Snapshot after(d);
              require("clip_core_without_depth_refused", verdict == LensVerdict::NoShader && !draw.applied && before == after); }
            // Built once per pair: no program creation over repeated core draws.
            { { clip.prepare(laws[2]); LensDraw draw; pass.lens_begin(clip.state(core::Body::Core), draw); clip.draw(); pass.lens_end(draw); } // the switch back from core_f rebuilt the pixel wrap once
              const unsigned ps_created = faults.create_ps_calls, vs_created = faults.create_vs_calls;
              for (unsigned i = 0; i < 3; ++i) { clip.prepare(laws[2]); LensDraw draw; pass.lens_begin(clip.state(core::Body::Core), draw); clip.draw(); pass.lens_end(draw); }
              require("clip_no_per_draw_creation", faults.create_ps_calls == ps_created && faults.create_vs_calls == vs_created && pass.pairs() == 2);
              std::printf("CLIP_PAIRS pairs=%u vertex_shaders_created=%u\n", pass.pairs(), faults.create_vs_calls); }
            // ---- jitter: RT2 on the jittered raster under the temporal pass's eight offsets, the disc and the clip fragments unjittered ----
            // Vertical edge at texel x = 164.703 (every tap at least 0.70 texels from it, three taps within one texel: the uncorrected
            // read flips in three phases; the corrected read equals the unjittered raster's in all eight), horizontal edge at texel
            // y = 82.153 (the y sign; one phase flips uncorrected). Neither edge sits on a raster tie of any phase (k + 0.5 - j != e).
            {
                Raster raster(d, W, H, vs20.p, clip_decl.p);
                const auto sequence = phases();
                std::printf("JITTER_PHASES");
                for (const Phase& ph : sequence) std::printf(" %u:%.4f,%.4f", ph.index, ph.jx, ph.jy);
                std::printf("\n");
                float tx[32], ty[32];
                for (unsigned i = 0; i < 32; ++i) { tx[i] = (u + taps[i][0] * ru) * float(W); ty[i] = (v + taps[i][1] * rv) * float(H); }
                struct Axis { const char* name; double edge; float x_edge, y_edge; const float* coordinates; unsigned expected_open; };
                const double ex = 164.703, ey = 82.153;
                const Axis axes[2] = {{"x", ex, float(2. * (ex - .5) / W - 1.), 1.f, tx, twin_open_jittered(tx, 32, ex, 0., true)},
                                      {"y", ey, -1.f, float(1. - 2. * (ey - .5) / H), ty, twin_open_jittered(ty, 32, ey, 0., true)}};
                for (const Axis& axis : axes) {
                    raster.draw(0.f, 0.f, axis.x_edge, axis.y_edge);
                    SunVisibilityFrame in = frame_of(depth, u, v, ru, rv, true); in.depth = raster.texture.p;
                    bool restored = rig.run(in, out, hr); const Read unjittered = rig.read();
                    bool ok = hr == S_OK && out.ran && restored && unjittered.ok && unjittered.f[3] == 1.f && close_to(unjittered.f[2], axis.expected_open / 32., 1e-6);
                    bool corrected_invariant = true, corrected_twin = true; unsigned uncorrected_distinct = 0; float seen[8]{};
                    for (const Phase& ph : sequence) {
                        raster.draw(ph.jx, ph.jy, axis.x_edge, axis.y_edge);
                        in.jitter_u = ph.jx / float(W); in.jitter_v = ph.jy / float(H);
                        restored = rig.run(in, out, hr); const Read corrected = rig.read();
                        in.jitter_u = in.jitter_v = 0.f;
                        const bool restored2 = rig.run(in, out, hr); const Read uncorrected = rig.read();
                        const double j = axis.name[0] == 'x' ? ph.jx : ph.jy;
                        const unsigned twin_corrected = twin_open_jittered(axis.coordinates, 32, axis.edge, j, true), twin_uncorrected = twin_open_jittered(axis.coordinates, 32, axis.edge, j, false);
                        std::printf("JITTER_PHASE axis=%s index=%u jx=%.4f jy=%.4f corrected=%.5f uncorrected=%.5f unjittered=%.5f twin_corrected=%u twin_uncorrected=%u valid=%.3f\n", axis.name, ph.index, ph.jx, ph.jy,
                                    corrected.f[2], uncorrected.f[2], unjittered.f[2], twin_corrected, twin_uncorrected, corrected.f[3]);
                        ok = ok && hr == S_OK && out.ran && restored && restored2 && corrected.ok && uncorrected.ok && corrected.f[3] == 1.f;
                        corrected_invariant = corrected_invariant && corrected.f[2] == unjittered.f[2] && corrected.f[0] == unjittered.f[0] && corrected.f[1] == unjittered.f[1];
                        corrected_twin = corrected_twin && close_to(corrected.f[2], twin_corrected / 32., 1e-6);
                        bool fresh = true; for (unsigned k = 0; k < uncorrected_distinct; ++k) fresh = fresh && seen[k] != uncorrected.f[2];
                        if (fresh) seen[uncorrected_distinct++] = uncorrected.f[2];
                    }
                    std::printf("JITTER_VISIBILITY axis=%s edge=%.3f unjittered=%.5f corrected_invariant=%u uncorrected_distinct=%u\n", axis.name, axis.edge, unjittered.f[2], corrected_invariant ? 1u : 0u, uncorrected_distinct);
                    require((std::string("jitter_") + axis.name + "_corrected_invariant").c_str(), ok && corrected_invariant && corrected_twin);
                    require((std::string("jitter_") + axis.name + "_uncorrected_varies").c_str(), uncorrected_distinct >= 2);
                }
                // The clip pair reads the jittered RT2 at the fragment's own texel (a texel-centred point tap: adding |j| < 0.5 texel would not change
                // the texel), so its edge column follows the raster's silhouette column ceil(e - 0.5 + jx) in every phase: the per-frame wobble of
                // a single jittered raster, which the resolve averages away for the scene but nothing averages for the disc. The kernel bounds
                // what that one-texel shift does to a pixel: the largest |open(x) - open'(x)| between any two phases, measured per column from
                // the readback (ONE/ONE: open = (out - background) / source) and from the models of this kernel and of the former cross.
                {
                    pass.set_core_fraction(false);
                    bool model = true; unsigned distinct = 0; int edges[8]{};
                    double lo[W], hi[W]; for (unsigned x = 0; x < W; ++x) { lo[x] = 2.; hi[x] = -1.; }
                    for (const Phase& ph : sequence) {
                        raster.draw(ph.jx, ph.jy, axes[0].x_edge, axes[0].y_edge);
                        clip.prepare(laws[2]);
                        LensState state = clip.state(core::Body::Core); state.depth = raster.texture.p;
                        LensDraw draw; const LensVerdict verdict = pass.lens_begin(state, draw); const bool clipped = draw.clipped; clip.draw(); const HRESULT ended = pass.lens_end(draw);
                        const auto row = sheet.row(d, H / 2);
                        const int b = int(std::ceil(ex - .5 + double(ph.jx)));
                        bool fresh = true; for (unsigned k = 0; k < distinct; ++k) fresh = fresh && edges[k] != b;
                        if (fresh) edges[distinct++] = b;
                        bool phase_ok = verdict == LensVerdict::Applied && clipped && SUCCEEDED(ended);
                        for (int x = b - 4; x < b + 4; ++x) { double want[3]; expected_pixel(laws[2], ClipRig::open_fraction(x, b), want); for (unsigned c = 0; c < 3; ++c) phase_ok = phase_ok && close_to(row[x][c], want[c], 2.5 / 255); }
                        for (int x = b - 4; x < b + 3; ++x) phase_ok = phase_ok && row[x][0] + 1.5 / 255 >= row[x + 1][0]; // monotone across the five soft columns
                        for (unsigned x = 0; x < W; ++x) { const double open = (row[x][0] - background[0]) / source[0]; lo[x] = open < lo[x] ? open : lo[x]; hi[x] = open > hi[x] ? open : hi[x]; }
                        std::printf("CLIP_JITTER_PHASE index=%u jx=%.4f silhouette=%d row=%.4f,%.4f,%.4f,%.4f,%.4f,%.4f model=%u\n", ph.index, ph.jx, b, row[b - 3][0], row[b - 2][0], row[b - 1][0], row[b][0], row[b + 1][0], row[b + 2][0], phase_ok ? 1u : 0u);
                        model = model && phase_ok;
                    }
                    double measured = 0., kernel_model = 0., cross_model = 0.; unsigned columns_moving = 0;
                    for (int x = int(ex) - 6; x < int(ex) + 6; ++x) {
                        measured = hi[x] - lo[x] > measured ? hi[x] - lo[x] : measured; columns_moving += hi[x] - lo[x] > 1.5 / 255;
                        double klo = 2., khi = -1., clo = 2., chi = -1.;
                        for (unsigned k = 0; k < distinct; ++k) { const double kv = ClipRig::open_fraction(x, edges[k]), cv = ClipRig::open_fraction_cross(x, edges[k]);
                            klo = kv < klo ? kv : klo; khi = kv > khi ? kv : khi; clo = cv < clo ? cv : clo; chi = cv > chi ? cv : chi; }
                        kernel_model = khi - klo > kernel_model ? khi - klo : kernel_model; cross_model = chi - clo > cross_model ? chi - clo : cross_model;
                    }
                    // Monotone across the edge: the unjittered-phase profile falls without a rise over the five columns b-3 .. b+2.
                    std::printf("CLIP_JITTER edge=%.3f silhouettes=%d,%d distinct=%u max_delta_measured=%.4f max_delta_kernel=%.4f max_delta_cross=%.4f columns_moving=%u\n", ex, edges[0], distinct > 1 ? edges[1] : edges[0], distinct,
                                measured, kernel_model, cross_model, columns_moving);
                    require("clip_jitter_follows_rt2_silhouette", model && distinct == 2);
                    require("clip_jitter_step_bounded", measured <= 2. / 9 + 2.5 / 255 * 1.25 && kernel_model <= 2. / 9 + 1e-9 && cross_model > .5);
                }
                for (UINT i = 0; i < 16; ++i) d->SetTexture(i, nullptr);
            }
            // ---- Reset: the DEFAULT-pool targets go, programs and wraps stay ----
            const unsigned held = pass.references();
            pass.before_reset();
            require("before_reset_releases_targets", pass.reset_pending() && pass.references() == held - 13 && !pass.valid()); // two textures, two surfaces, the quad's two blocks, the ps_2_0 and ps_3_0 ghost wraps' two blocks each (one def register each, different), the clip pair's two blocks, the diagnostic readback surface
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
            // The clip pair survives the Reset (programs kept, blocks re-recorded): a core draw clips again without creating a program.
            { Com<IDirect3DPixelShader9> ps20; check("ps20 after reset", d->CreatePixelShader(ps20_words, &ps20.p));
              Sheet sheet(d, W, H); ClipRig clip{d, pass, bindings, sheet, depth, vs20.p, clip_decl.p, ps20.p};
              const unsigned ps_created = faults.create_ps_calls, vs_created = faults.create_vs_calls; const int edge = int(W / 2);
              clip.prepare(laws[2]); const Snapshot before(d); LensDraw draw; const LensVerdict verdict = pass.lens_begin(clip.state(core::Body::Core), draw); const bool clipped = draw.clipped;
              clip.draw(); const HRESULT ended = pass.lens_end(draw); const Snapshot after(d);
              const auto row = sheet.row(d, H / 2); double open[3], covered[3]; expected_pixel(laws[2], 1., open); expected_pixel(laws[2], 0., covered); bool ok = true;
              for (unsigned c = 0; c < 3; ++c) ok = ok && close_to(row[edge - 4][c], open[c], 2.5 / 255) && close_to(row[edge + 3][c], covered[c], 2.5 / 255);
              std::printf("CLIP_RESET verdict=%s clipped=%u f=%.4f open=%.4f,%.4f,%.4f covered=%.4f,%.4f,%.4f ps_created=%u vs_created=%u pairs=%u\n", lens_verdict_name(verdict), clipped ? 1u : 0u, r.f[1],
                          row[edge - 4][0], row[edge - 4][1], row[edge - 4][2], row[edge + 3][0], row[edge + 3][1], row[edge + 3][2], faults.create_ps_calls - ps_created, faults.create_vs_calls - vs_created, pass.pairs());
              require("clip_after_reset", verdict == LensVerdict::Applied && clipped && SUCCEEDED(ended) && ok && before == after && faults.create_ps_calls == ps_created && faults.create_vs_calls == vs_created && pass.pairs() == 2);
              d->SetVertexShader(nullptr); d->SetPixelShader(nullptr); d->SetVertexDeclaration(nullptr); }
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
