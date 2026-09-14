// Detached qualification of the production AmbientOcclusionPass
// (docs/architecture/ambient-occlusion.md, section 7, step 1): synthetic R32F
// device depth for analytic scenes through the real chain, the CPU reference
// of ambient_occlusion_reference.h, the flat-plane and sentinel identities,
// the multiply application against the CPU law, hostile state restoration,
// the fault ladder through a hooked vtable, native Reset and EVENT-fenced
// timing windows. Validation-only readback; never production. No game.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include "../../src/renderer/ambient_occlusion_pass.h"
#include "ambient_occlusion_reference.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
using namespace x3m::renderer;
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
double half_to_double(std::uint16_t h) {
    const unsigned e = (h >> 10) & 31; const double s = (h & 0x8000) ? -1 : 1;
    if (e == 31) return (h & 1023) ? NAN : s * INFINITY;
    return s * (e ? std::ldexp(double(1024 + (h & 1023)), int(e) - 25) : std::ldexp(double(h & 1023), -24));
}
std::uint16_t half_bits(double r) { // r already representable in binary16
    float f = float(r); std::uint32_t bits; std::memcpy(&bits, &f, 4);
    const std::uint32_t sign = (bits >> 16) & 0x8000; const int e = int((bits >> 23) & 255) - 127 + 15;
    if (r == 0) return std::uint16_t(sign);
    if (e <= 0) return std::uint16_t(sign | (((bits & 0x7fffff) | 0x800000) >> (14 - e)));
    return std::uint16_t(sign | (std::uint32_t(e) << 10) | ((bits & 0x7fffff) >> 13));
}
std::uint16_t double_to_half(double v) { return half_bits(ao_reference::fp16(v)); } // round to nearest even
std::uint16_t double_to_half_truncate(double v) { // toward zero: the other documented-plausible store model
    const double r = ao_reference::fp16(v);
    if (r == v || v == 0) return half_bits(r);
    const std::uint16_t h = half_bits(r);
    return std::fabs(r) > std::fabs(v) ? std::uint16_t(h - 1) : h;
}
constexpr double kRadius = 10, kFloor = -20, kSphereR = 10, kSphereZ = 100, kWallZ = 100, kPlaneZ = 200, kStepNearZ = 196;
// The route's projection (camera_reprojection.h; the default scratch m22/m32, zn 6, zf 2e6).
ao_reference::Params projection(unsigned w, unsigned h) {
    ao_reference::Params p;
    p.m11 = 1.7320508; p.m00 = p.m11 * double(h) / double(w); p.m20 = p.m21 = 0;
    p.m22 = 1.0000030; p.m32 = -6.0000184;
    p.radius = kRadius; p.strength = .5; p.falloff = .615; p.max_radius_px = 64; p.depth_tolerance = .05; p.jitter_index = 5;
    return p;
}
AmbientOcclusionParams pass_params(const ao_reference::Params& p) {
    AmbientOcclusionParams q;
    q.m00 = float(p.m00); q.m11 = float(p.m11); q.m20 = float(p.m20); q.m21 = float(p.m21); q.m22 = float(p.m22); q.m32 = float(p.m32);
    q.radius_metres = float(p.radius / 5.); q.units_per_metre = 5.f; q.strength = float(p.strength); q.falloff = float(p.falloff); q.max_radius_px = float(p.max_radius_px);
    q.depth_tolerance = float(p.depth_tolerance); q.jitter_index = p.jitter_index;
    return q;
}
// Analytic scenes in view units (5 per metre): camera at the origin looking
// +z (view y up), floor 4 m below (y = -20), a 2 m sphere resting on it 20 m
// ahead, a fronto-parallel wall 20 m ahead (corner) or plane 40 m ahead, a
// 0.8 m step edge in the plane, the AO radius 2 m.
struct Scene { std::string name; unsigned w, h; std::vector<float> depth; std::vector<double> z; };
Scene scene(const std::string& name, unsigned w, unsigned h, const ao_reference::Params& p) {
    Scene s{name, w, h, std::vector<float>(std::size_t(w) * h, -1.f), std::vector<double>(std::size_t(w) * h, -1)};
    for (unsigned y = 0; y < h; ++y) for (unsigned x = 0; x < w; ++x) {
        const double nx = 2 * (x + .5) / w - 1, ny = 1 - 2 * (y + .5) / h;
        const double dx = nx / p.m00, dy = ny / p.m11; // ray (dx, dy, 1) * t, z = t
        double t = INFINITY;
        auto floor_hit = [&](double zmax) { if (dy < 0) { const double tf = kFloor / dy; if (tf < t && tf <= zmax) t = tf; } };
        if (name == "plane") t = kPlaneZ;
        else if (name == "tilted") floor_hit(4000);
        else if (name == "step") t = nx < 0 ? kPlaneZ : kStepNearZ;
        else if (name == "corner") { t = kWallZ; floor_hit(kWallZ); }
        else if (name == "sphere") {
            floor_hit(1000);
            const double cx = 0, cy = kFloor + kSphereR, cz = kSphereZ;
            const double a = dx * dx + dy * dy + 1, b = -2 * (dx * cx + dy * cy + cz), c = cx * cx + cy * cy + cz * cz - kSphereR * kSphereR;
            const double disc = b * b - 4 * a * c;
            if (disc >= 0) { const double ts = (-b - std::sqrt(disc)) / (2 * a); if (ts > 0 && ts < t) t = ts; }
        }
        if (std::isfinite(t) && t >= 6) {
            s.z[std::size_t(y) * w + x] = t;
            s.depth[std::size_t(y) * w + x] = float(p.m22 + p.m32 / t);
        }
    }
    return s;
}
// Hooked vtable: fault injection for the fault ladder. Every other slot is the device's own.
using CreateTextureFn = HRESULT(WINAPI*)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
using CreatePsFn = HRESULT(WINAPI*)(IDirect3DDevice9*, const DWORD*, IDirect3DPixelShader9**);
using DrawUpFn = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
struct Faults { unsigned texture_fail_at = 0, texture_calls = 0, shader_fail_at = 0, shader_calls = 0, draw_fail_at = 0, draw_calls = 0; HRESULT draw_error = E_FAIL; } faults;
void* hooked[119]; void* original[119];
HRESULT WINAPI hook_create_texture(IDirect3DDevice9* d, UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DTexture9** t, HANDLE* s) {
    if (++faults.texture_calls == faults.texture_fail_at) return E_OUTOFMEMORY;
    return reinterpret_cast<CreateTextureFn>(original[23])(d, w, h, l, u, f, p, t, s);
}
HRESULT WINAPI hook_create_ps(IDirect3DDevice9* d, const DWORD* words, IDirect3DPixelShader9** out) {
    if (++faults.shader_calls == faults.shader_fail_at) return E_OUTOFMEMORY;
    return reinterpret_cast<CreatePsFn>(original[106])(d, words, out);
}
HRESULT WINAPI hook_draw(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT c, const void* v, UINT s) {
    if (++faults.draw_calls == faults.draw_fail_at) return faults.draw_error;
    return reinterpret_cast<DrawUpFn>(original[83])(d, t, c, v, s);
}
// Everything the chain may touch, compared byte for byte around execute.
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
        for (UINT n = 0; n < 8; ++n) {
            IDirect3DBaseTexture9* t = nullptr; check("snapshot texture", d->GetTexture(n, &t)); object(t);
            for (UINT j = 1; j <= 13; ++j) { DWORD v = 0; hr = d->GetSamplerState(n, D3DSAMPLERSTATETYPE(j), &v); add(hr); add(v); }
        }
        const D3DRENDERSTATETYPE states[] = {D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ZFUNC, D3DRS_STENCILENABLE, D3DRS_ALPHATESTENABLE, D3DRS_ALPHABLENDENABLE,
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
void hostile(IDirect3DDevice9* d, IDirect3DBaseTexture9* texture) {
    for (auto [s, v] : {std::pair{D3DRS_ALPHABLENDENABLE, DWORD(TRUE)}, std::pair{D3DRS_SRCBLEND, DWORD(D3DBLEND_SRCALPHA)}, std::pair{D3DRS_DESTBLEND, DWORD(D3DBLEND_INVSRCALPHA)},
                        std::pair{D3DRS_BLENDOP, DWORD(D3DBLENDOP_REVSUBTRACT)}, std::pair{D3DRS_SCISSORTESTENABLE, DWORD(TRUE)}, std::pair{D3DRS_ZENABLE, DWORD(TRUE)},
                        std::pair{D3DRS_COLORWRITEENABLE, DWORD(1)}, std::pair{D3DRS_CULLMODE, DWORD(D3DCULL_CW)}, std::pair{D3DRS_FILLMODE, DWORD(D3DFILL_WIREFRAME)},
                        std::pair{D3DRS_SRGBWRITEENABLE, DWORD(TRUE)}, std::pair{D3DRS_FOGENABLE, DWORD(TRUE)}, std::pair{D3DRS_ALPHATESTENABLE, DWORD(TRUE)},
                        std::pair{D3DRS_STENCILENABLE, DWORD(TRUE)}, std::pair{D3DRS_MULTISAMPLEMASK, DWORD(0)}, std::pair{D3DRS_CLIPPING, DWORD(TRUE)}})
        check("hostile rs", d->SetRenderState(s, v));
    RECT sc{3, 3, 9, 9}; check("hostile scissor", d->SetScissorRect(&sc));
    D3DVIEWPORT9 vp{2, 2, 7, 7, .2f, .7f}; check("hostile viewport", d->SetViewport(&vp));
    for (UINT i = 0; i < 3; ++i) {
        check("hostile texture", d->SetTexture(i, texture));
        check("hostile sampler", d->SetSamplerState(i, D3DSAMP_MINFILTER, D3DTEXF_LINEAR)); check("hostile sampler", d->SetSamplerState(i, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR));
        check("hostile sampler", d->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP)); check("hostile sampler", d->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_MIRROR));
        check("hostile sampler", d->SetSamplerState(i, D3DSAMP_SRGBTEXTURE, TRUE));
    }
    const float garbage[16] = {9, 8, 7, 6, 5, 4, 3, 2, 1, .5f, .25f, .125f, -1, -2, -3, -4};
    check("hostile constants", d->SetPixelShaderConstantF(0, garbage, 4));
    check("hostile fvf", d->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE));
    check("hostile tss", d->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 2));
}
// Default-pool inputs of one frame size: the R32F depth (uploaded from system
// memory), the FP16 target and the readback surfaces.
struct Frame {
    unsigned w, h, hw, hh;
    Com<IDirect3DTexture9> depth, depth_sys, target, target_sys;
    Com<IDirect3DSurface9> target_surface, read_term, read_half, read_target;
    Frame(IDirect3DDevice9* d, unsigned width, unsigned height) : w(width), h(height), hw((width + 1) / 2), hh((height + 1) / 2) {
        check("depth texture", d->CreateTexture(w, h, 1, 0, D3DFMT_R32F, D3DPOOL_DEFAULT, &depth.p, nullptr));
        check("depth staging", d->CreateTexture(w, h, 1, 0, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &depth_sys.p, nullptr));
        check("target texture", d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &target.p, nullptr));
        check("target staging", d->CreateTexture(w, h, 1, 0, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &target_sys.p, nullptr));
        check("target surface", target->GetSurfaceLevel(0, &target_surface.p));
        check("term readback", d->CreateOffscreenPlainSurface(hw, hh, D3DFMT_R16F, D3DPOOL_SYSTEMMEM, &read_term.p, nullptr));
        check("half readback", d->CreateOffscreenPlainSurface(hw, hh, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &read_half.p, nullptr));
        check("target readback", d->CreateOffscreenPlainSurface(w, h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &read_target.p, nullptr));
    }
    void upload_depth(IDirect3DDevice9* d, const std::vector<float>& values) {
        D3DLOCKED_RECT lr{}; check("lock depth", depth_sys->LockRect(0, &lr, nullptr, 0));
        for (unsigned y = 0; y < h; ++y) std::memcpy(static_cast<char*>(lr.pBits) + y * lr.Pitch, &values[std::size_t(y) * w], w * 4);
        check("unlock depth", depth_sys->UnlockRect(0));
        check("update depth", d->UpdateTexture(depth_sys.p, depth.p));
    }
    // A pattern with values below and above one and a non-trivial alpha.
    void fill_target(IDirect3DDevice9* d) {
        D3DLOCKED_RECT lr{}; check("lock target", target_sys->LockRect(0, &lr, nullptr, 0));
        for (unsigned y = 0; y < h; ++y) { auto* row = reinterpret_cast<std::uint16_t*>(static_cast<char*>(lr.pBits) + y * lr.Pitch);
            for (unsigned x = 0; x < w; ++x) {
                row[x * 4 + 0] = double_to_half(.25 + .75 * double(x) / w); row[x * 4 + 1] = double_to_half(2.0 * double(y) / h + .01);
                row[x * 4 + 2] = double_to_half(1.0); row[x * 4 + 3] = double_to_half(.375);
            } }
        check("unlock target", target_sys->UnlockRect(0));
        check("update target", d->UpdateTexture(target_sys.p, target.p));
    }
    std::vector<std::uint16_t> read16(IDirect3DDevice9* d, IDirect3DSurface9* source, IDirect3DSurface9* sink, unsigned width, unsigned height, unsigned channels) {
        check("readback", d->GetRenderTargetData(source, sink));
        D3DLOCKED_RECT lr{}; check("lock readback", sink->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        std::vector<std::uint16_t> out(std::size_t(width) * height * channels);
        for (unsigned y = 0; y < height; ++y) std::memcpy(&out[std::size_t(y) * width * channels], static_cast<char*>(lr.pBits) + y * lr.Pitch, width * channels * 2);
        check("unlock readback", sink->UnlockRect());
        return out;
    }
    std::vector<float> read32(IDirect3DDevice9* d, IDirect3DSurface9* source, IDirect3DSurface9* sink, unsigned width, unsigned height) {
        check("readback", d->GetRenderTargetData(source, sink));
        D3DLOCKED_RECT lr{}; check("lock readback", sink->LockRect(&lr, nullptr, D3DLOCK_READONLY));
        std::vector<float> out(std::size_t(width) * height);
        for (unsigned y = 0; y < height; ++y) std::memcpy(&out[std::size_t(y) * width], static_cast<char*>(lr.pBits) + y * lr.Pitch, width * 4);
        check("unlock readback", sink->UnlockRect());
        return out;
    }
};
AmbientOcclusionFrame frame_inputs(Frame& f, const ao_reference::Params& p, bool with_target) {
    AmbientOcclusionFrame in;
    in.depth = f.depth.p; in.target = with_target ? f.target_surface.p : nullptr; in.width = f.w; in.height = f.h;
    in.params = pass_params(p); in.caller_scene_open = false; in.caller_queries_idle = true;
    return in;
}
std::vector<double> term_values(Frame& f, IDirect3DDevice9* d, IDirect3DTexture9* term) {
    Com<IDirect3DSurface9> s; check("term surface", term->GetSurfaceLevel(0, &s.p));
    auto raw = f.read16(d, s.p, f.read_term.p, f.hw, f.hh, 1);
    std::vector<double> out(raw.size()); for (std::size_t i = 0; i < raw.size(); ++i) out[i] = 1 - half_to_double(raw[i]); // visibility from the stored occlusion
    return out;
}
// One scene through the chain with the CPU reference, the identities and the multiply law.
struct SceneRun { std::vector<double> term; std::vector<std::uint16_t> before, after; std::vector<float> half; };
SceneRun run_scene(IDirect3DDevice9* d, AmbientOcclusionPass& pass, Frame& f, const Scene& s, const ao_reference::Params& p, bool oracle) {
    f.upload_depth(d, s.depth); f.fill_target(d);
    SceneRun r;
    r.before = f.read16(d, f.target_surface.p, f.read_target.p, f.w, f.h, 4);
    hostile(d, f.target.p);
    Snapshot pre(d);
    AmbientOcclusionResult out;
    const HRESULT hr = pass.execute(frame_inputs(f, p, true), &out);
    Snapshot post(d);
    const std::string label = "scene_" + s.name;
    require((label + "_execute").c_str(), SUCCEEDED(hr) && out.applied && out.term && out.half_width == f.hw && out.half_height == f.hh && out.failed == AmbientOcclusionStage::None);
    require((label + "_state_preserved").c_str(), pre == post);
    require((label + "_references").c_str(), pass.references() == 13);
    r.term = term_values(f, d, out.term);
    Com<IDirect3DSurface9> hs; check("half surface", pass.fixture_half_depth()->GetSurfaceLevel(0, &hs.p));
    r.half = f.read32(d, hs.p, f.read_half.p, f.hw, f.hh);
    r.after = f.read16(d, f.target_surface.p, f.read_target.p, f.w, f.h, 4);
    if (!oracle) return r;
    // Linearization: the GPU half depth against the CPU one (float32 division of the same terms).
    std::vector<double> ref_half;
    std::vector<double> ref = ao_reference::term(s.depth, f.w, f.h, p, &ref_half);
    for (double& v : ref) v = 1 - v; // visibility space, like r.term
    double max_rel = 0; unsigned sentinel_mismatch = 0, sentinel = 0;
    for (std::size_t i = 0; i < ref_half.size(); ++i) {
        if (ref_half[i] < 0) { ++sentinel; if (r.half[i] != -1.f) ++sentinel_mismatch; continue; }
        max_rel = std::max(max_rel, std::fabs(r.half[i] - ref_half[i]) / ref_half[i]);
    }
    require((label + "_linearize").c_str(), max_rel <= 2e-3 && sentinel_mismatch == 0);
    // The term against the CPU reference: mean, 99.9th percentile and maximum absolute difference.
    std::vector<double> diffs(ref.size()); double mean = 0; unsigned within = 0, ones = 0;
    for (std::size_t i = 0; i < ref.size(); ++i) { diffs[i] = std::fabs(r.term[i] - ref[i]); mean += diffs[i]; within += diffs[i] <= .02; ones += r.term[i] == 1; }
    mean /= double(ref.size());
    std::vector<double> sorted = diffs; std::sort(sorted.begin(), sorted.end());
    const double p999 = sorted[std::size_t(double(sorted.size() - 1) * .999)], maximum = sorted.back();
    std::printf("REFERENCE scene=%s width=%u height=%u pixels=%zu sentinel=%u ones=%u mean_abs=%.6f p999=%.6f max=%.6f within_002=%u half_depth_max_rel=%.3e\n",
                s.name.c_str(), f.w, f.h, ref.size(), sentinel, ones, mean, p999, maximum, within, max_rel);
    require((label + "_reference").c_str(), mean <= 2e-3 && double(within) / double(ref.size()) >= .999);
    if (std::getenv("X3M_AO_DUMP")) { // local diagnostics only: raw float64 term images beside the executable (blurred and unblurred)
        pass.fixture_skip_blur(true);
        AmbientOcclusionResult raw_out; check("raw execute", pass.execute(frame_inputs(f, p, false), &raw_out));
        pass.fixture_skip_blur(false);
        const std::vector<double> raw_gpu = term_values(f, d, raw_out.term);
        std::vector<double> raw_cpu = ao_reference::gtao(ref_half, f.hw, f.hh, p);
        for (double& v : raw_cpu) v = 1 - v;
        for (auto [suffix, image] : {std::pair<const char*, const std::vector<double>*>{"gpu", &r.term}, std::pair<const char*, const std::vector<double>*>{"cpu", &ref}, std::pair<const char*, const std::vector<double>*>{"half", &ref_half},
                                     std::pair<const char*, const std::vector<double>*>{"rawgpu", &raw_gpu}, std::pair<const char*, const std::vector<double>*>{"rawcpu", &raw_cpu}})
            if (FILE* out = std::fopen(("ao_" + s.name + "_" + suffix + ".f64").c_str(), "wb")) { std::fwrite(image->data(), sizeof(double), image->size(), out); std::fclose(out); }
    }
    // Sentinel pixels: term exactly 1, target bit-identical (factor 1).
    unsigned term_sentinel_bad = 0, target_sentinel_bad = 0;
    for (std::size_t i = 0; i < ref_half.size(); ++i) if (ref_half[i] < 0 && r.term[i] != 1) ++term_sentinel_bad;
    for (std::size_t i = 0; i < s.depth.size(); ++i) if (s.depth[i] < 0) for (unsigned c = 0; c < 4; ++c) if (r.before[i * 4 + c] != r.after[i * 4 + c]) ++target_sentinel_bad;
    require((label + "_sentinel_identity").c_str(), term_sentinel_bad == 0 && target_sentinel_bad == 0);
    // The multiply law: expected = fp16(before * factor) per RGB channel from
    // the GPU's own term/half depth, alpha untouched; reported in fp16 ulps.
    unsigned exact = 0, one_ulp = 0, over = 0, alpha_changed = 0, exact_truncate = 0; int max_ulp = 0;
    const std::vector<double> half_depth(r.half.begin(), r.half.end());
    std::vector<double> occlusion(r.term.size()); for (std::size_t i = 0; i < occlusion.size(); ++i) occlusion[i] = 1 - r.term[i]; // the stored term again
    for (unsigned y = 0; y < f.h; ++y) for (unsigned x = 0; x < f.w; ++x) {
        const std::size_t i = std::size_t(y) * f.w + x;
        const double factor = ao_reference::factor(occlusion, half_depth, f.hw, f.hh, x, y, s.depth[i], p);
        for (unsigned c = 0; c < 3; ++c) {
            const double product = half_to_double(r.before[i * 4 + c]) * factor;
            const std::uint16_t expected = double_to_half(product), actual = r.after[i * 4 + c];
            exact_truncate += double_to_half_truncate(product) == actual;
            const int ulps = std::abs(int(expected) - int(actual));
            if (ulps == 0) ++exact; else if (ulps == 1) ++one_ulp; else ++over;
            max_ulp = std::max(max_ulp, ulps);
        }
        alpha_changed += r.before[i * 4 + 3] != r.after[i * 4 + 3];
    }
    std::printf("APPLY scene=%s channels=%zu exact=%u one_ulp=%u over=%u max_ulp=%d exact_truncate=%u alpha_changed=%u\n", s.name.c_str(), std::size_t(f.w) * f.h * 3, exact, one_ulp, over, max_ulp, exact_truncate, alpha_changed);
    require((label + "_apply_law").c_str(), over == 0 && alpha_changed == 0);
    return r;
}
double region_mean(const Scene& s, const std::vector<double>& term, unsigned hw, unsigned hh, const ao_reference::Params& p, bool (*inside)(double, double, double), unsigned* count, double* minimum) {
    double sum = 0; *count = 0; *minimum = 2;
    for (unsigned j = 0; j < hh; ++j) for (unsigned i = 0; i < hw; ++i) {
        const unsigned x = std::min(2 * i, s.w - 1), y = std::min(2 * j, s.h - 1);
        const double z = s.z[std::size_t(y) * s.w + x];
        if (z < 0) continue;
        const double nx = 2 * (x + .5) / s.w - 1, ny = 1 - 2 * (y + .5) / s.h;
        const double px = nx / p.m00 * z, py = ny / p.m11 * z;
        if (!inside(px, py, z)) continue;
        const double v = term[std::size_t(j) * hw + i];
        sum += v; ++*count; *minimum = std::min(*minimum, v);
    }
    return *count ? sum / *count : NAN;
}
void oracle_line(const char* scene, const char* label, double mean, double minimum, unsigned count, bool pass) {
    std::printf("ORACLE scene=%s label=%s mean=%.4f min=%.4f pixels=%u %s\n", scene, label, mean, minimum, count, pass ? "PASS" : "FAIL");
    require((std::string("oracle_") + scene + "_" + label).c_str(), pass);
}
HRESULT complete_fence(IDirect3DQuery9* q) {
    HRESULT hr = q->Issue(D3DISSUE_END); if (FAILED(hr)) return hr;
    const DWORD begin = GetTickCount();
    for (;;) { hr = q->GetData(nullptr, 0, D3DGETDATA_FLUSH); if (hr != S_FALSE) return hr; if (GetTickCount() - begin > 5000) return E_FAIL; Sleep(0); }
}
double ms(LONGLONG ticks, LONGLONG f) { return double(ticks) * 1000. / double(f); }
double median(std::vector<double> v) { std::sort(v.begin(), v.end()); return v[v.size() / 2]; }
void timing(IDirect3DDevice9* d, AmbientOcclusionPass& pass, unsigned w, unsigned h, LONGLONG freq) {
    const auto p = projection(w, h);
    Frame f(d, w, h); const Scene s = scene("sphere", w, h, p);
    f.upload_depth(d, s.depth); f.fill_target(d);
    Com<IDirect3DQuery9> fence; check("event query", d->CreateQuery(D3DQUERYTYPE_EVENT, &fence.p));
    check("timing begin", d->BeginScene());
    AmbientOcclusionFrame in = frame_inputs(f, p, true); in.caller_scene_open = true;
    AmbientOcclusionResult out;
    check("timing warm", pass.execute(in, &out));
    std::vector<double> on, off, submit;
    for (unsigned i = 0; i < 20; ++i) { // 3 warmup pairs, 7 measured pairs (on, off alternating)
        LARGE_INTEGER a{}, b{}, c{};
        check("lead fence", complete_fence(fence.p));
        QueryPerformanceCounter(&a);
        if (i % 2 == 0) check("timed execute", pass.execute(in, &out));
        QueryPerformanceCounter(&b);
        check("tail fence", complete_fence(fence.p));
        QueryPerformanceCounter(&c);
        if (i < 3 * 2) continue; // warmup pairs
        (i % 2 == 0 ? on : off).push_back(ms(c.QuadPart - a.QuadPart, freq));
        if (i % 2 == 0) submit.push_back(ms(b.QuadPart - a.QuadPart, freq));
    }
    // Submit-only windows (no trailing fence) for the fixed CPU-side cost.
    for (unsigned i = 0; i < 6; ++i) {
        LARGE_INTEGER a{}, b{};
        QueryPerformanceCounter(&a); check("submit execute", pass.execute(in, &out)); QueryPerformanceCounter(&b);
        submit.push_back(ms(b.QuadPart - a.QuadPart, freq));
    }
    check("final fence", complete_fence(fence.p));
    check("timing end", d->EndScene());
    const double on_m = median(on), off_m = median(off), submit_m = median(submit);
    std::printf("TIMING width=%u height=%u fenced_on_ms=%.4f fenced_off_ms=%.4f submit_ms=%.4f chain_ms=%.4f gpu_ms=%.4f samples=%zu\n",
                w, h, on_m, off_m, submit_m, on_m - off_m, std::max(0., on_m - off_m - submit_m), on.size());
    // Diagnostic only: the chain without its two blur quads (three passes),
    // to separate per-pass overhead from shader cost.
    pass.fixture_skip_blur(true);
    std::vector<double> short_on;
    check("short begin", d->BeginScene());
    for (unsigned i = 0; i < 6; ++i) {
        LARGE_INTEGER a{}, c{};
        check("short lead fence", complete_fence(fence.p)); QueryPerformanceCounter(&a);
        check("short execute", pass.execute(in, &out));
        check("short tail fence", complete_fence(fence.p)); QueryPerformanceCounter(&c);
        if (i >= 2) short_on.push_back(ms(c.QuadPart - a.QuadPart, freq));
    }
    check("short end", d->EndScene());
    pass.fixture_skip_blur(false);
    std::printf("TIMING_VARIANT width=%u height=%u variant=no_blur fenced_on_ms=%.4f chain_ms=%.4f samples=%zu\n", w, h, median(short_on), median(short_on) - off_m, short_on.size());
}
int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        WNDCLASSA cls{}; cls.lpfnWndProc = DefWindowProcA; cls.hInstance = GetModuleHandleA(nullptr); cls.lpszClassName = "X3AmbientOcclusionFixture"; RegisterClassA(&cls);
        HWND window = CreateWindowA(cls.lpszClassName, "X3 ambient occlusion fixture", WS_OVERLAPPEDWINDOW, 90, 90, 128, 128, nullptr, nullptr, cls.hInstance, nullptr);
        if (!window) throw std::runtime_error("window");
        HMODULE runtime = LoadLibraryA("d3d9.dll"); if (!runtime) throw std::runtime_error("d3d9.dll");
        auto address = GetProcAddress(runtime, "Direct3DCreate9"); IDirect3D9*(WINAPI* create)(UINT) = nullptr; std::memcpy(&create, &address, sizeof create);
        if (!create) throw std::runtime_error("Direct3DCreate9");
        Com<IDirect3D9> api; api.p = create(D3D_SDK_VERSION); if (!api.p) throw std::runtime_error("Create9");
        const unsigned W = 1280, H = 768;
        D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window; pp.BackBufferWidth = W; pp.BackBufferHeight = H;
        pp.BackBufferFormat = D3DFMT_A8R8G8B8; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device; check("CreateDevice", api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device.p));
        IDirect3DDevice9* d = device.p;
        D3DCAPS9 caps{}; check("caps", d->GetDeviceCaps(&caps));
        D3DDISPLAYMODE mode{}; check("display mode", api->GetAdapterDisplayMode(0, &mode));
        LARGE_INTEGER freq{}; QueryPerformanceFrequency(&freq);
        std::memcpy(original, *reinterpret_cast<void***>(d), sizeof original); std::memcpy(hooked, original, sizeof hooked);
        hooked[23] = reinterpret_cast<void*>(&hook_create_texture); hooked[106] = reinterpret_cast<void*>(&hook_create_ps); hooked[83] = reinterpret_cast<void*>(&hook_draw);
        std::printf("CAPS ps=%08lx vs=%08lx ps30_slots=%u rts=%u src_zero=%d dest_srccolor=%d\n", (unsigned long)caps.PixelShaderVersion, (unsigned long)caps.VertexShaderVersion,
                    unsigned(caps.MaxPixelShader30InstructionSlots), unsigned(caps.NumSimultaneousRTs), (caps.SrcBlendCaps & D3DPBLENDCAPS_ZERO) != 0, (caps.DestBlendCaps & D3DPBLENDCAPS_SRCCOLOR) != 0);
        // Capability twins: refused by name before any creation.
        { AmbientOcclusionPass twin; D3DCAPS9 c = caps; c.PixelShaderVersion = D3DPS_VERSION(2, 0);
          require("twin_ps_2_0", FAILED(twin.attach(d, hooked, c, mode.Format, D3DFMT_A16B16G16R16F)) && std::string(twin.caps().reason) == "ps_3_0" && twin.references() == 0);
          c = caps; c.MaxPixelShader30InstructionSlots = 400;
          require("twin_ps_slots", FAILED(twin.attach(d, hooked, c, mode.Format, D3DFMT_A16B16G16R16F)) && std::string(twin.caps().reason) == "ps_slots" && twin.references() == 0);
          c = caps; c.DestBlendCaps &= ~DWORD(D3DPBLENDCAPS_SRCCOLOR);
          require("twin_blend_factors", FAILED(twin.attach(d, hooked, c, mode.Format, D3DFMT_A16B16G16R16F)) && std::string(twin.caps().reason) == "blend_factors" && twin.references() == 0);
          faults = {}; faults.shader_fail_at = 3;
          require("fault_shader_create", FAILED(twin.attach(d, hooked, caps, mode.Format, D3DFMT_A16B16G16R16F)) && std::string(twin.caps().reason) == "programs" && twin.references() == 0 && !twin.caps().enabled);
          faults = {}; }
        AmbientOcclusionPass pass;
        check("attach", pass.attach(d, hooked, caps, mode.Format, D3DFMT_A16B16G16R16F));
        std::printf("ATTACH enabled=%d largest_program_slots=%u references=%u\n", pass.caps().enabled, pass.caps().largest_program_slots, pass.references());
        require("attach_enabled", pass.caps().enabled && pass.references() == 6);
        // Fault: target creation fails on the second texture; every partial target is released.
        faults = {}; faults.texture_fail_at = 2;
        require("fault_target_create", FAILED(pass.prepare(W, H)) && pass.references() == 6 && pass.allocations() == 0 && pass.half_width() == 0);
        faults = {};
        check("prepare", pass.prepare(W, H));
        require("prepare_targets", pass.references() == 12 && pass.allocations() == 1 && pass.half_width() == 640 && pass.half_height() == 384);
        const auto p = projection(W, H);
        Frame f(d, W, H);
        // Refusals: an 8-bit target, unknown query state, the pass's own term as depth.
        { Com<IDirect3DTexture9> eight; check("8-bit target", d->CreateTexture(W, H, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &eight.p, nullptr));
          Com<IDirect3DSurface9> es; check("8-bit surface", eight->GetSurfaceLevel(0, &es.p));
          AmbientOcclusionFrame in = frame_inputs(f, p, true); in.target = es.p; AmbientOcclusionResult out;
          require("refuse_8bit_target", pass.execute(in, &out) == E_INVALIDARG && out.failed == AmbientOcclusionStage::Validate);
          in = frame_inputs(f, p, true); in.caller_queries_idle = false;
          require("refuse_unknown_queries", pass.execute(in, &out) == E_INVALIDARG);
          in = frame_inputs(f, p, true); in.params.strength = 2.f;
          require("refuse_bad_params", pass.execute(in, &out) == E_INVALIDARG); }
        const char* names[] = {"plane", "tilted", "sphere", "corner", "step"};
        std::vector<double> clean_sphere;
        for (const char* name : names) {
            if (const char* only = std::getenv("X3M_AO_SCENES")) if (!std::strstr(only, name)) continue; // local debugging: run a subset
            const Scene s = scene(name, W, H, p);
            SceneRun r = run_scene(d, pass, f, s, p, true);
            unsigned count = 0; double minimum = 0, mean = 0;
            if (!std::strcmp(name, "plane") || !std::strcmp(name, "tilted")) {
                // Fronto-parallel: exact. Tilted: the R32F device depth quantizes the floor
                // (dz ~ z^2 1e-8), so a few far pixels carry an occlusion <= 1e-3 that the
                // CPU reference reproduces; at most 0.1 % of pixels, none below 0.999.
                unsigned not_one = 0, below = 0, covered = 0;
                for (std::size_t i = 0; i < r.term.size(); ++i) if (r.half[i] >= 0) { ++covered; not_one += r.term[i] != 1; below += r.term[i] < .999; }
                const bool exact = !std::strcmp(name, "plane");
                const bool pass = covered > 0 && below == 0 && (exact ? not_one == 0 : not_one * 1000 <= covered);
                std::printf("ORACLE scene=%s label=flat_identity covered=%u not_one=%u below_0999=%u %s\n", name, covered, not_one, below, pass ? "PASS" : "FAIL");
                require((std::string("oracle_") + name + "_flat_identity").c_str(), pass);
            } else if (!std::strcmp(name, "sphere")) {
                clean_sphere = r.term;
                // Contact ring on the floor (radial distance 0.1..0.4 r from the contact point): darkened; floor farther than r + 1.3 R from the sphere surface: no halo; upper sphere: unoccluded.
                mean = region_mean(s, r.term, f.hw, f.hh, p, [](double x, double y, double z) { const double r = std::hypot(x, z - kSphereZ); return std::fabs(y - kFloor) < 1e-9 && r >= .1 * kSphereR && r <= .4 * kSphereR; }, &count, &minimum);
                oracle_line(name, "contact_ring", mean, minimum, count, count > 0 && mean < .97 && minimum < .9);
                mean = region_mean(s, r.term, f.hw, f.hh, p, [](double x, double y, double z) { const double dist = std::sqrt(x * x + (y - (kFloor + kSphereR)) * (y - (kFloor + kSphereR)) + (z - kSphereZ) * (z - kSphereZ)) - kSphereR; return std::fabs(y - kFloor) < 1e-9 && dist > 1.3 * kRadius && z < 600; }, &count, &minimum);
                oracle_line(name, "no_halo", mean, minimum, count, count > 0 && mean >= .999 && minimum >= .995);
                mean = region_mean(s, r.term, f.hw, f.hh, p, [](double, double y, double z) { return y > kFloor + kSphereR + .1 * kSphereR && z < kSphereZ; }, &count, &minimum);
                oracle_line(name, "convex_top", mean, minimum, count, count > 0 && mean >= .995);
            } else if (!std::strcmp(name, "corner")) {
                mean = region_mean(s, r.term, f.hw, f.hh, p, [](double, double y, double z) { return std::fabs(y - kFloor) < 1e-9 && kWallZ - z <= .4 * kRadius; }, &count, &minimum);
                oracle_line(name, "crease_near", mean, minimum, count, count > 0 && mean < .92);
                mean = region_mean(s, r.term, f.hw, f.hh, p, [](double, double y, double z) { return std::fabs(y - kFloor) < 1e-9 && kWallZ - z <= kRadius; }, &count, &minimum);
                oracle_line(name, "crease_floor", mean, minimum, count, count > 0 && mean < .98 && minimum < .85);
                mean = region_mean(s, r.term, f.hw, f.hh, p, [](double, double y, double z) { return std::fabs(y - kFloor) < 1e-9 && kWallZ - z >= 1.3 * kRadius && kWallZ - z <= 6 * kRadius; }, &count, &minimum);
                oracle_line(name, "floor_beyond_radius", mean, minimum, count, count > 0 && mean >= .999 && minimum >= .995);
                mean = region_mean(s, r.term, f.hw, f.hh, p, [](double, double y, double z) { return std::fabs(z - kWallZ) < 1e-9 && y - kFloor >= 1.3 * kRadius; }, &count, &minimum);
                oracle_line(name, "wall_beyond_radius", mean, minimum, count, count > 0 && mean >= .999 && minimum >= .995);
            } else {
                mean = region_mean(s, r.term, f.hw, f.hh, p, [](double x, double, double z) { return z == kStepNearZ && x > .3 * kRadius; }, &count, &minimum);
                oracle_line(name, "near_side_unoccluded", mean, minimum, count, count > 0 && minimum >= .995);
                mean = region_mean(s, r.term, f.hw, f.hh, p, [](double x, double, double z) { return z == kPlaneZ && x > -.2 * kRadius && x < 0; }, &count, &minimum);
                oracle_line(name, "far_side_edge_darkened", mean, minimum, count, count > 0 && mean < .95 && minimum < .8);
            }
        }
        // Fault ladder mid-chain: an ordinary draw failure restores and publishes nothing; a
        // lost device stops restoration; after native Reset the chain is bit-identical.
        const Scene sphere = scene("sphere", W, H, p);
        { f.upload_depth(d, sphere.depth); f.fill_target(d);
          auto before = f.read16(d, f.target_surface.p, f.read_target.p, W, H, 4);
          Snapshot pre(d); faults = {}; faults.draw_fail_at = 3; faults.draw_error = E_FAIL;
          AmbientOcclusionResult out; const HRESULT hr = pass.execute(frame_inputs(f, p, true), &out); faults = {};
          Snapshot post(d); auto after = f.read16(d, f.target_surface.p, f.read_target.p, W, H, 4);
          require("fault_draw_failed", hr == E_FAIL && out.failed == AmbientOcclusionStage::BlurH && out.term == nullptr && !out.applied && SUCCEEDED(out.restore));
          require("fault_draw_restored", pre == post && before == after);
          SceneRun again = run_scene(d, pass, f, sphere, p, false);
          require("fault_draw_recovered", again.term == clean_sphere);
          // Lost mid-chain inside the caller's scene (the route's case): restoration is
          // skipped for the caller's recovery, and a later chain still normalizes itself.
          faults = {}; faults.draw_fail_at = 4; faults.draw_error = D3DERR_DEVICELOST;
          check("lost scene", d->BeginScene());
          AmbientOcclusionFrame lost_frame = frame_inputs(f, p, true); lost_frame.caller_scene_open = true;
          const HRESULT lost = pass.execute(lost_frame, &out); faults = {};
          check("lost scene end", d->EndScene());
          require("fault_lost_mid_chain", lost == D3DERR_DEVICELOST && out.failed == AmbientOcclusionStage::BlurV && out.restore == D3DERR_DEVICELOST && out.term == nullptr);
          require("execute_after_lost_still_runs", SUCCEEDED(pass.execute(frame_inputs(f, p, true), &out))); }
        { pass.before_reset();
          require("before_reset_released", pass.references() == 6 && pass.reset_pending());
          AmbientOcclusionResult out;
          require("refuse_while_reset_pending", pass.execute(frame_inputs(f, p, true), &out) == D3DERR_DEVICENOTRESET);
          require("prepare_refused_while_reset_pending", pass.prepare(W, H) == D3DERR_DEVICENOTRESET); }
        { // Release every default-pool object of the fixture, Reset, re-create.
          Frame* released = new Frame(d, 16, 16); delete released; }
        f.depth.reset(); f.target_surface.reset(); f.target.reset();
        const HRESULT reset = d->Reset(&pp); check("Reset", reset); pass.after_reset(reset);
        require("after_reset_accepts", !pass.reset_pending());
        { Frame g(d, W, H);
          SceneRun after = run_scene(d, pass, g, sphere, p, false);
          require("reset_bit_identical", after.term == clean_sphere && pass.references() == 13 && pass.allocations() == 2);
          std::printf("RESET PASS references=%u allocations=%u\n", pass.references(), pass.allocations()); }
        timing(d, pass, 1280, 768, freq.QuadPart);
        timing(d, pass, 1920, 1080, freq.QuadPart);
        pass.detach();
        require("detach_released", pass.references() == 0 && !pass.caps().enabled);
        std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::printf("RESULT FAIL exception=%s checks=%u failures=%u\n", e.what(), checks, failures);
        return 2;
    }
}
