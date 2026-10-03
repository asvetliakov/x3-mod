// Engine plumes fixture (docs/architecture/engine-effects-modern.md sections 3-6; docs/verification/engine-effects.md).
// Standalone D3D9 (builtin d3d9 through the runner's override), no game, no proxy: the production EnginePlumesPass, the
// production record -> vertex builder (engine_plumes_core.h) and the production TemporalPass on synthetic FP16 scenes,
// lanes and motion targets at 1920x1080 and 5120x1440. Identity view (world = view), square pixels, m11 1.7.
//   attach        programs, buffers, slots; the FP16 blending refusal (fault) -> D3DERR_NOTAVAILABLE, nothing held
//   length        s = 0 / 0.5 / 1 (z 0.25 / 1.125 / 2), the still look: the axis's drawn length and the column's
//                 width at u = 0.5 against the CPU replica of the pixel law, the readback against the replica (law)
//   resolve       a plume at rest and at 4 / 8 px per frame through the real resolve (the flown sentinel configuration:
//                 PerPixel, far camera + camera gate, strict sky, exit 0.25), over a dark sky and a sky flickering in
//                 every 3x3; core survival against the frame's own radiance, the trail behind the tip
//   occlusion     a plane at the nozzle depth, head-on (the exhaust pointing away) and at 20 degrees, both lane forms:
//                 the core hidden inside the silhouette, visible outside, the soft rim's width; tail-on (the exhaust at
//                 the camera, the occlusion bias) reported
//   chase         the own ship's plume tail-on close to the camera: its projected extent against 0.12 H, the fade's 0.5
//   presets       restrained / default / strong: the core and halo radiance ratios
//   temporal      30 frames at 60 fps: the core's variation (alive, not strobing), its lag-1 correlation (the flow
//                 moves, it does not jump) and mean (the design); resolved
//   shape         the body's mean half-width at u = 0.1 (the bulge) and 0.9 (the taper) over the nozzle's half-width,
//                 against the mock-up's law
//   shock         the axis's local maxima to u = 0.6 (the shock diamonds)
//   off_path      no record: S_FALSE and no device call; an idle callback leaves the resolve byte-identical
//   fault         a failed draw reports its step; the next frame draws
//   reset         every object released before Reset, recreated after; the next frame draws
//   timing        EVENT-fenced stage cost in a frame tail at 30 and 100 nozzles; the CPU build for 30 / 100 records
// Validation-only readback; never launches the game.
#include "../../src/renderer/engine_plumes_pass.h"
#include "../../src/renderer/temporal_pass.h"
#include "../../src/renderer/temporal_resolve_program.h"
#include "../../src/renderer/quad_vertex_program.h"
#include "../../src/renderer/hdr_writeback_program.h"
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
namespace ep = x3m::engine_plumes;
namespace ee = x3m::engine_effects::core;
namespace rr = x3m::renderer;
template <class T> struct Com {
    T* p = nullptr;
    ~Com() {
        if (p) p->Release();
    }
    T* operator->() const { return p; }
};
unsigned checks = 0, failures = 0;
void report(const char* label, bool ok) {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s\n", label, ok ? "PASS" : "FAIL");
}
void check(const char* label, HRESULT hr) {
    if (FAILED(hr)) {
        std::printf("FATAL %s %08lx\n", label, hr);
        throw std::runtime_error(label);
    }
}
float half_to_float(unsigned short h) {
    const unsigned s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    float v;
    if (e == 0) v = std::ldexp(float(m), -24);
    else if (e == 31) v = m ? NAN : INFINITY;
    else v = std::ldexp(float(m + 1024), int(e) - 25);
    return s ? -v : v;
}
using Compiler = decltype(&D3DXCompileShader);
Compiler compiler = nullptr;
void compile(const char* source, const char* target, std::vector<DWORD>& out) {
    Com<ID3DXBuffer> code, errors;
    const HRESULT hr = compiler(source, UINT(std::strlen(source)), nullptr, nullptr, "main", target,
                                D3DXSHADER_OPTIMIZATION_LEVEL3, &code.p, &errors.p, nullptr);
    if (errors.p) std::printf("COMPILER %s\n", static_cast<char*>(errors->GetBufferPointer()));
    check(target, hr);
    out.assign(static_cast<DWORD*>(code->GetBufferPointer()),
               static_cast<DWORD*>(code->GetBufferPointer()) + code->GetBufferSize() / 4);
}
double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
double halton(unsigned i, unsigned b) {
    double f = 1, r = 0;
    while (i) {
        f /= b;
        r += f * (i % b);
        i /= b;
    }
    return r;
}
HRESULT complete_fence(IDirect3DQuery9* q) {
    check("fence issue", q->Issue(D3DISSUE_END));
    HRESULT hr;
    const DWORD start = GetTickCount();
    while ((hr = q->GetData(nullptr, 0, D3DGETDATA_FLUSH)) == S_FALSE && GetTickCount() - start < 5000) Sleep(0);
    return hr;
}
const float m11 = 1.7f, projection_m22 = 1.000003f, projection_m32 = -6.0000184f;
// Full-screen quad through the production vs_3_0 pass-through.
struct Quad {
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DVertexDeclaration9> decl;
    explicit Quad(IDirect3DDevice9* d) {
        check("quad vs", d->CreateVertexShader(reinterpret_cast<const DWORD*>(rr::quad_vertex_program()), &vs.p));
        check("quad decl", d->CreateVertexDeclaration(rr::quad_declaration, &decl.p));
    }
    void draw(IDirect3DDevice9* d, UINT w, UINT h, IDirect3DPixelShader9* ps) {
        rr::QuadVertex q[4];
        rr::quad_vertices(w, h, q);
        check("quad vs set", d->SetVertexShader(vs.p));
        check("quad decl set", d->SetVertexDeclaration(decl.p));
        check("quad ps", d->SetPixelShader(ps));
        D3DVIEWPORT9 vp{0, 0, w, h, 0, 1};
        check("quad viewport", d->SetViewport(&vp));
        check("quad draw", d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof q[0]));
    }
};
struct Targets {
    UINT w, h;
    Com<IDirect3DTexture9> scene, lane4, lane1, motion, other;
    Com<IDirect3DSurface9> scene_s, lane4_s, lane1_s, motion_s, other_s, depth, staging;
    Targets(IDirect3DDevice9* d, UINT width, UINT height)
        : w(width), h(height) {
        auto rt = [&](D3DFORMAT f, Com<IDirect3DTexture9>& t, Com<IDirect3DSurface9>& s, const char* what) {
            check(what, d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, f, D3DPOOL_DEFAULT, &t.p, nullptr));
            check(what, t->GetSurfaceLevel(0, &s.p));
        };
        rt(D3DFMT_A16B16G16R16F, scene, scene_s, "scene");
        rt(D3DFMT_A32B32G32R32F, lane4, lane4_s, "lane4");
        rt(D3DFMT_R32F, lane1, lane1_s, "lane1");
        rt(D3DFMT_A32B32G32R32F, motion, motion_s, "motion");
        rt(D3DFMT_A16B16G16R16F, other, other_s, "other");
        check("depth", d->CreateDepthStencilSurface(w, h, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &depth.p, nullptr));
        check("staging", d->CreateOffscreenPlainSurface(w, h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &staging.p, nullptr));
    }
    std::vector<float> read(IDirect3DDevice9* d, IDirect3DSurface9* source = nullptr) {
        check("readback", d->GetRenderTargetData(source ? source : scene_s.p, staging.p));
        D3DLOCKED_RECT r{};
        check("lock staging", staging->LockRect(&r, nullptr, D3DLOCK_READONLY));
        std::vector<float> out(std::size_t(w) * h * 4);
        for (UINT y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<const unsigned short*>(static_cast<const unsigned char*>(r.pBits) + y * r.Pitch);
            for (UINT x = 0; x < w * 4; ++x) out[std::size_t(y) * w * 4 + x] = half_to_float(row[x]);
        }
        staging->UnlockRect();
        return out;
    }
    float m00() const { return m11 * float(h) / float(w); } // square pixels
    float ppu(float z) const { return m11 * float(h) * .5f / z; }
    void window(float x, float y, float z, float jx, float jy, float& px, float& py) const {
        px = ((m00() * x / z + 2.f * jx / float(w)) + 1.f) * float(w) * .5f;
        py = ((-(m11 * y / z - 2.f * jy / float(h))) + 1.f) * float(h) * .5f;
    }
};
struct Scene {
    IDirect3DDevice9* d;
    Targets& t;
    Quad& quad;
    Com<IDirect3DPixelShader9> lane4_ps, lane1_ps, motion_ps, sky_ps, mrt_ps, zero_ps;
    Scene(IDirect3DDevice9* device, Targets& targets, Quad& q)
        : d(device), t(targets), quad(q) {
        std::vector<DWORD> code;
        // The lane: (z/w, 0, view depth, 0) inside the occluder rectangle (pixels, c0 = x0 y0 x1 y1; c1.x = view
        // depth), the sentinel (-1) outside.
        compile("float4 rect:register(c0);float4 depth:register(c1);float4 main(float2 p:VPOS):COLOR0{bool inside=p.x>=rect.x&&p.x<rect.z&&p.y>=rect.y&&p.y<rect.w;float z=depth.x;return inside?float4(1.000003+(-6.0000184)/z,0,z,0):float4(-1,0,0,0);}",
                "ps_3_0", code);
        check("lane4 ps", d->CreatePixelShader(code.data(), &lane4_ps.p));
        compile("float4 rect:register(c0);float4 depth:register(c1);float4 main(float2 p:VPOS):COLOR0{bool inside=p.x>=rect.x&&p.x<rect.z&&p.y>=rect.y&&p.y<rect.w;float z=depth.x;return inside?float4(1.000003+(-6.0000184)/z,0,0,0):float4(-1,0,0,0);}",
                "ps_3_0", code);
        check("lane1 ps", d->CreatePixelShader(code.data(), &lane1_ps.p));
        // The motion target of an all-unrouted frame: the route's fill sentinel (alpha -1).
        compile("float4 main(float2 p:VPOS):COLOR0{return float4(0,0,0,-1);}", "ps_3_0", code);
        check("motion ps", d->CreatePixelShader(code.data(), &motion_ps.p));
        // The flickering sky: every pixel a fresh hash in [0, 1] each frame (c0.x), so every 3x3 spans about [0, 1].
        compile("float4 frame:register(c0);float4 main(float2 p:VPOS):COLOR0{float h=frac(sin(dot(p,float2(12.9898,78.233))+frame.x*1.61803)*43758.5453);return float4(h,h,h,0);}",
                "ps_3_0", code);
        check("sky ps", d->CreatePixelShader(code.data(), &sky_ps.p));
        compile("struct O{float4 c0:COLOR0;float4 c1:COLOR1;float4 c2:COLOR2;};O main(float2 p:VPOS){O o;o.c0=float4(0.001,0.001,0.002,0);o.c1=float4(0,0,0,-1);o.c2=float4(-1,0,0,0);return o;}",
                "ps_3_0", code);
        check("mrt ps", d->CreatePixelShader(code.data(), &mrt_ps.p));
        compile("float4 main(float2 p:VPOS):COLOR0{return float4(0,0,0,0);}", "ps_3_0", code);
        check("zero ps", d->CreatePixelShader(code.data(), &zero_ps.p));
    }
    void states() {
        check("blend off", d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
        check("z off", d->SetRenderState(D3DRS_ZENABLE, FALSE));
        check("cull", d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
        check("no rt1", d->SetRenderTarget(1, nullptr));
        check("no rt2", d->SetRenderTarget(2, nullptr));
        check("no ds", d->SetDepthStencilSurface(nullptr));
    }
    // Both lanes (the occluder rectangle at view depth z; an empty rectangle = sky everywhere), the motion target, the
    // scene (dark, or the flickering sky of frame n); RT0 = the scene at the end.
    void frame(float x0, float y0, float x1, float y1, float z, bool flicker = false, unsigned n = 0) {
        states();
        const float rect[4] = {x0, y0, x1, y1}, dz[4] = {z, 0, 0, 0}, fr[4] = {float(n), 0, 0, 0};
        check("rect", d->SetPixelShaderConstantF(0, rect, 1));
        check("depth", d->SetPixelShaderConstantF(1, dz, 1));
        check("rt lane4", d->SetRenderTarget(0, t.lane4_s.p));
        quad.draw(d, t.w, t.h, lane4_ps.p);
        check("rt lane1", d->SetRenderTarget(0, t.lane1_s.p));
        quad.draw(d, t.w, t.h, lane1_ps.p);
        check("rt motion", d->SetRenderTarget(0, t.motion_s.p));
        quad.draw(d, t.w, t.h, motion_ps.p);
        check("rt scene", d->SetRenderTarget(0, t.scene_s.p));
        if (flicker) {
            check("frame", d->SetPixelShaderConstantF(0, fr, 1));
            quad.draw(d, t.w, t.h, sky_ps.p);
        } else
            check("clear scene", d->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0));
    }
};
// A synthetic record: identity view, so the origin and axis are view-space; cluster white (tint 1, 1, 1).
ee::Record record(float x, float y, float z, float ax, float ay, float az, float value, float zscale, bool steering = false) {
    ee::Record r{};
    const float n = std::sqrt(ax * ax + ay * ay + az * az);
    r.origin[0] = x;
    r.origin[1] = y;
    r.origin[2] = z;
    r.axis[0] = ax / n;
    r.axis[1] = ay / n;
    r.axis[2] = az / n;
    r.size = value;
    r.z = zscale;
    float s = (zscale - .25f) / 1.75f;
    r.s = s < 0.f ? 0.f : s > 1.f ? 1.f : s;
    r.ratio = zscale;
    r.node_handle = 0x1234;
    r.model = 20000;
    r.body = -1;
    r.flags = std::uint16_t((unsigned(ee::white) << ee::cluster_shift) | (steering ? unsigned(ee::flag_steering) : 0u));
    r.serial = 0;
    return r;
}
// The looks the cases draw with: the production look (null: ep::default_look), and a still one (no turbulence, erosion
// or length pulse) where a readback is compared with the CPU replica of the law; the shape case's: no pulse, no halo
// and no ring (the body alone).
ep::Look make_still() {
    ep::Look k = ep::default_look;
    k.turb = 0.f;
    k.erode = 0.f;
    k.pulse = 0.f;
    return k;
}
ep::Look make_body() {
    ep::Look k = ep::default_look;
    k.pulse = 0.f;
    k.hb = 0.f;
    k.ring = 0.f;
    return k;
}
const ep::Look still = make_still(), body_only = make_body();
// The flow phase of the stage clock's `seconds` (the proxy advances it per frame at a constant rate: the same value).
float phase_at(float seconds, const ep::Look* look) {
    float rate = 0.f, out = 0.f;
    ep::flow_rate(look ? *look : ep::default_look, &rate);
    ep::FlowPhase phase;
    phase.advance(double(seconds), rate);
    phase.wrapped(&out);
    return out;
}
rr::EnginePlumesFrame frame_for(Targets& t, bool four, const ee::Record* records, unsigned count,
                                ep::Preset preset = ep::Preset::standard, float seconds = 0.f, float jx = 0.f,
                                float jy = 0.f, const ep::Look* look = nullptr) {
    rr::EnginePlumesFrame f{};
    f.width = t.w;
    f.height = t.h;
    const float rows[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    std::memcpy(f.view.rows, rows, sizeof rows);
    f.view.m00 = t.m00();
    f.view.m11 = m11;
    f.view.height = float(t.h);
    f.view.near_z = 1.f;
    f.m20 = 2.f * jx / float(t.w);
    f.m21 = -2.f * jy / float(t.h);
    f.m22 = projection_m22;
    f.m32 = projection_m32;
    f.lane_four_channel = four;
    f.lane = four ? t.lane4.p : t.lane1.p;
    f.records = records;
    f.record_count = count;
    f.body = nullptr;
    f.preset = preset;
    f.seconds = seconds;
    f.phase = phase_at(seconds, look);
    f.look = look;
    return f;
}
// The frame's first nozzle as the builder writes it (the same inputs the pass draws).
struct Built {
    ep::Vertex v[8];
    ep::BuildStats stats;
    unsigned nozzles = 0;
};
Built build_cpu(const rr::EnginePlumesFrame& f) {
    Built b{};
    b.nozzles = ep::build(f.records, f.record_count, f.body, f.view, f.preset, f.seconds, b.v, 1, &b.stats, nullptr, f.look);
    return b;
}
// The CPU replica of the pixel program's law (src/effects/engine_plume_ps.hlsl) for the axial quad and a white tint:
// the luma (the red channel: white stays 1 under the heat and ring mixes) at (x, y) in nozzle widths, the noise given
// (n1 the erosion's, n2 the turbulence's; 0.4375 = their mean, the still look ignores them; noise_at the CPU's value
// noise at the pixel program's coordinates, statistically the GPU's: the hash's float rounding differs).
struct LawInputs {
    float L = 0.f, s = 0.f, i_core = 0.f, i_halo = 0.f, sigma0 = 0.f, ring2 = 0.f, aa = 0.f, n_px = 0.f, seed = 0.f;
};
LawInputs law_of(const ep::Vertex& v, float ppu) {
    LawInputs in;
    in.n_px = v.local[3] * ppu;
    in.L = v.local[2] / v.local[3];
    in.s = float((v.params >> 16) & 255u) / 255.f;
    in.i_core = v.intensity[0];
    in.i_halo = v.intensity[1];
    in.sigma0 = v.shape[0];
    in.ring2 = float(v.params & 255u) / 255.f;
    in.seed = float((v.params >> 8) & 255u) / 255.f;
    in.aa = 1.f / in.n_px;
    return in;
}
void noise_at(const LawInputs& in, float x, float y, float phase, float seconds, float& n1, float& n2) {
    const float p[3] = {(x - phase) * 1.6f, y * 3.f, in.seed * 1861.5f + seconds * .7f};
    const float p2[3] = {p[0] * 2.2f + 5.f, p[1] * 2.2f + 2.f, p[2] * 2.2f + 1.f};
    ep::noise::fbm(p, &n1);
    ep::noise::fbm(p2, &n2);
}
float smooth(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    t = t < 0.f ? 0.f : t > 1.f ? 1.f : t;
    return t * t * (3.f - 2.f * t);
}
float replica(const ep::Look& k, const LawInputs& in, float x, float y, float n1 = .4375f, float n2 = .4375f) {
    float c[20];
    ep::pixel_constants(k, c);
    const float L = in.L, r = std::fabs(y), u = x / L, uc = std::min(std::max(u, 0.f), 1.f);
    const float b = c[0] * (1.f - .55f * std::exp(-9.f * uc)) * (1.f + .35f * smooth(0.f, .25f, uc) * std::exp(-4.f * uc));
    const float w = std::min(c[0] + c[1] * uc, b + c[2]) * std::max(1.f - c[3] * smooth(.6f, 1.f, uc), .05f);
    const float radial = r / w;
    const float edge = 1.f - smooth(.55f, 1.f, radial + c[5] * (n1 - .5f));
    const float tail = (1.f - smooth(c[7], 1.f, u)) * std::exp(-u * c[11]);
    const float inside = u >= 0.f && u <= 1.f ? 1.f : 0.f;
    const float cells = 1.f + c[8] * std::cos(c[9] * u) * std::exp(-u * c[10]) * (1.f - smooth(0.f, .8f, radial)) * in.s;
    const float turbulence = 1.f + c[6] * (n2 - .5f);
    const float core_mask = 1.f - smooth(0.f, 1.f, radial * c[13]);
    const float body = in.i_core * edge * tail * inside * cells * turbulence * (1.f + .6f * core_mask);
    const float du = x - std::min(std::max(x, 0.f), L), d = std::sqrt(du * du + r * r);
    const float dn = d / std::max(in.sigma0, 1e-4f);
    const float window = std::min(std::max(2.f * (ep::halo_reach - dn), 0.f), 1.f);
    const float halo = in.i_halo * std::exp(-dn) * std::exp(-2.2f * uc) * window;
    const float s2 = c[15] + .25f * in.aa * in.aa, rr2 = r - c[14];
    const float ring = in.i_core * in.ring2 * c[19] * std::sqrt(c[15] / s2) * std::exp(-rr2 * rr2 / (2.f * s2)) *
                       std::exp(-std::max(x, 0.f) * c[18]) * (x >= -.05f ? 1.f : 0.f);
    return body + ring + halo;
}
// Luma (the largest channel) of the rectangle [x0, x1) x [y0, y1) of `source` (null: the scene), row-major.
std::vector<float> read_region(IDirect3DDevice9* d, Targets& t, IDirect3DSurface9* source, int x0, int y0, int x1, int y1) {
    check("region readback", d->GetRenderTargetData(source ? source : t.scene_s.p, t.staging.p));
    D3DLOCKED_RECT r{};
    check("region lock", t.staging->LockRect(&r, nullptr, D3DLOCK_READONLY));
    std::vector<float> out;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            float m = 0.f;
            if (x >= 0 && y >= 0 && x < int(t.w) && y < int(t.h)) {
                const auto* p = reinterpret_cast<const unsigned short*>(static_cast<const unsigned char*>(r.pBits) + y * r.Pitch) + x * 4;
                m = std::max(half_to_float(p[0]), std::max(half_to_float(p[1]), half_to_float(p[2])));
            }
            out.push_back(m);
        }
    t.staging->UnlockRect();
    return out;
}
float luma(const std::vector<float>& px, UINT w, int x, int y) {
    const std::size_t i = (std::size_t(y) * w + std::size_t(x)) * 4;
    return std::max(px[i], std::max(px[i + 1], px[i + 2]));
}
float peak(const std::vector<float>& px, UINT w, UINT h, int x0, int y0, int x1, int y1) {
    float m = 0;
    for (int y = std::max(y0, 0); y < std::min(y1, int(h)); ++y)
        for (int x = std::max(x0, 0); x < std::min(x1, int(w)); ++x) m = std::max(m, luma(px, w, x, y));
    return m;
}
void core_levels(float s, float scale, float& core, float& halo) {
    const ep::Look& k = ep::default_look;
    core = (k.core_low + (k.core_high - k.core_low) * s) * scale;
    halo = k.hb * (k.halo_low + (k.halo_high - k.halo_low) * s) * scale;
}
rr::EnginePlumesReport draw(IDirect3DDevice9* d, rr::EnginePlumesPass& pass, const rr::EnginePlumesFrame& f) {
    rr::EnginePlumesReport r{};
    check("scene begin", d->BeginScene());
    const HRESULT hr = pass.run(f, &r);
    check("scene end", d->EndScene());
    check("plume run", hr);
    return r;
}

void attach_case(IDirect3DDevice9* d, const D3DCAPS9& caps, D3DFORMAT format, rr::EnginePlumesPass& pass) {
    const HRESULT attached = pass.attach(d, *reinterpret_cast<void* const* const*>(d), caps, format);
    const auto& c = pass.caps();
    std::printf("ATTACH result=%08lx enabled=%u reason=%s vs_slots=%u ps_slots=%u fp16_blending=%08lx max_vs_slots=%lu max_ps_slots=%lu references=%u vb_bytes=%u ib_bytes=%u\n",
                attached, unsigned(c.enabled), c.reason, c.vs_slots, c.ps_slots, c.fp16_blending,
                static_cast<unsigned long>(caps.MaxVertexShader30InstructionSlots),
                static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots), pass.references(),
                rr::EnginePlumesPass::vertex_bytes, rr::EnginePlumesPass::index_bytes);
    report("attach", SUCCEEDED(attached) && c.enabled && pass.references() == 5 && c.fp16_blending == D3D_OK);
    rr::EnginePlumesPass refused;
    refused.set_faults(1);
    const HRESULT hr = refused.attach(d, *reinterpret_cast<void* const* const*>(d), caps, format);
    std::printf("FP16_REFUSED result=%08lx enabled=%u reason=%s references=%u\n", hr, unsigned(refused.caps().enabled),
                refused.caps().reason, refused.references());
    report("fp16_refused", hr == D3DERR_NOTAVAILABLE && !refused.caps().enabled &&
                               !std::strcmp(refused.caps().reason, "fp16_blending") && refused.references() == 0);
    rr::EnginePlumesFrame f{};
    rr::EnginePlumesReport rep{};
    report("fp16_refused_run_refused", refused.run(f, &rep) == E_INVALIDARG && rep.calls == 0);
}

// s = 0 / 0.5 / 1: a side view (axis -x), value 100 px, the still look (no turbulence, erosion or pulse). The axis's
// drawn length (the farthest pixel from the nozzle at 20 % of I_core or more) and the column's half-width at u = 0.5
// (down to 10 % of its peak) against the CPU replica of the law on the same pixel centres; the readback against the
// replica on a 16 x 9 grid over the plume (law check: within 3 % of I_core).
void length_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass) {
    const float Z = 2000.f, ppu = t.ppu(Z), value = 100.f / ppu;
    for (const float zs : {.25f, 1.125f, 2.f}) {
        const float Lpx = zs * value * ppu;
        const float x0 = (Lpx * .5f) / ppu; // the nozzle right of centre by half the plume
        const ee::Record r = record(x0, 0, Z, -1, 0, 0, value, zs);
        const rr::EnginePlumesFrame f = frame_for(t, true, &r, 1, ep::Preset::standard, 0.f, 0.f, 0.f, &still);
        scene.frame(0, 0, 0, 0, 500);
        const auto rep = draw(d, pass, f);
        const auto px = t.read(d);
        const Built b = build_cpu(f);
        const LawInputs in = law_of(b.v[0], ppu);
        float cx, cy;
        t.window(x0, 0, Z, 0, 0, cx, cy);
        const int iy = int(std::floor(cy + .5f)), ix = int(std::floor(cx + .5f));
        auto at = [&](int X, int Y) { // the replica at a pixel centre (the axis runs to -x on screen)
            return replica(still, in, (cx - float(X)) / in.n_px, (float(Y) - cy) / in.n_px); // D3D9: pixel centres on integers
        };
        const float threshold = .2f * in.i_core;
        int left = ix, want_left = ix;
        while (left > 0 && luma(px, t.w, left - 1, iy) >= threshold) --left;
        while (want_left > 0 && at(want_left - 1, iy) >= threshold) --want_left;
        const float measured = float(ix - left), expected = float(ix - want_left);
        // The column at u = 0.5.
        const int column = int(std::floor(cx - .5f * in.L * in.n_px));
        auto half_width = [&](auto&& sample) {
            float pk = 0.f;
            for (int y = iy - 200; y <= iy + 200; ++y) pk = std::max(pk, sample(column, y));
            int up = 0, down = 0;
            while (up < 400 && sample(column, iy - up - 1) >= .1f * pk) ++up;
            while (down < 400 && sample(column, iy + down + 1) >= .1f * pk) ++down;
            return .5f * float(up + down + 1);
        };
        const float width = half_width([&](int X, int Y) { return Y >= 0 && Y < int(t.h) ? luma(px, t.w, X, Y) : 0.f; });
        const float want_width = half_width(at);
        // The law: the readback against the replica over the plume.
        float worst = 0.f;
        for (int gy = 0; gy < 9; ++gy)
            for (int gx = 0; gx < 16; ++gx) {
                const int X = int(cx - (float(gx) + .5f) / 16.f * Lpx * 1.1f), Y = iy + int((float(gy) - 4.f) * .3f * in.n_px);
                if (X < 0 || Y < 0 || X >= int(t.w) || Y >= int(t.h)) continue;
                worst = std::max(worst, std::fabs(luma(px, t.w, X, Y) - at(X, Y)) / in.i_core);
            }
        std::printf("LENGTH width=%u height=%u z=%.3f s=%.3f value_px=%.1f L_px=%.1f nozzle_px=%.1f measured_px=%.0f expected_px=%.0f half_width_u05_px=%.1f expected_half_width_px=%.1f law_max_error=%.4f peak=%.3f core=%.3f nozzles=%u discs=%u\n",
                    t.w, t.h, double(zs), double(r.s), double(value * ppu), double(Lpx), double(in.n_px), double(measured),
                    double(expected), double(width), double(want_width), double(worst),
                    double(peak(px, t.w, t.h, ix - int(Lpx), iy - 2, ix + 1, iy + 3)), double(in.i_core), rep.stats.nozzles,
                    rep.stats.discs);
        char label[64];
        std::snprintf(label, sizeof label, "length_s%.2f_%u", double(r.s), t.w);
        report(label, rep.stats.nozzles == 1 && b.nozzles == 1 && std::fabs(b.v[0].local[2] * ppu - Lpx) <= .01f * Lpx + .01f &&
                          std::fabs(measured - expected) <= std::max(2.f, .03f * Lpx));
        std::snprintf(label, sizeof label, "width_u05_s%.2f_%u", double(r.s), t.w);
        report(label, std::fabs(width - want_width) <= 2.f);
        std::snprintf(label, sizeof label, "law_s%.2f_%u", double(r.s), t.w);
        report(label, worst <= .03f);
    }
}

struct StageContext {
    rr::EnginePlumesPass* pass;
    rr::EnginePlumesFrame frame;
    HRESULT result;
    rr::EnginePlumesReport report;
};
HRESULT stage_callback(void* context, IDirect3DDevice9*) noexcept {
    auto* c = static_cast<StageContext*>(context);
    c->result = c->pass->run(c->frame, &c->report);
    return c->result;
}
HRESULT idle_callback(void*, IDirect3DDevice9*) noexcept {
    return S_FALSE;
}
// The flown resolve configuration (temporal_pass_fixture case (m)): PerPixel with the all-unrouted motion target,
// sentinel camera with strict sky, exit 0.25, far camera + camera gate; history weight the production default.
bool flown = true;
rr::FrameInputs resolve_inputs(Targets& t, float jx, float jy, float pjx, float pjy, bool four) {
    rr::FrameInputs in;
    in.color = t.scene.p;
    in.current_depth = four ? t.lane4.p : t.lane1.p;
    in.width = t.w;
    in.height = t.h;
    in.epoch = 1;
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::copy(identity, identity + 16, in.clip_to_previous);
    in.current_jitter[0] = jx;
    in.current_jitter[1] = jy;
    in.previous_jitter[0] = pjx;
    in.previous_jitter[1] = pjy;
    in.history_allowed = true;
    in.caller_scene_open = true;
    in.caller_queries_idle = true;
    if (flown) {
        in.motion = t.motion.p;
        in.motion_policy = rr::MotionPolicy::PerPixel;
        in.reactive_policy = rr::ReactivePolicy::DerivedFromDepthSentinel;
        in.sentinel_camera = true;
        in.sentinel_strict_sky = true;
        in.sky_history_exit_px = .25f;
        in.far_weight = .985f;
        in.far_d0 = .9995f;
        in.far_inv = 1.f / (.9999f - .9995f);
        in.far_speed_lo = x3::temporal::kFarSpeedLo;
        in.far_speed_hi = x3::temporal::kFarSpeedHi;
        in.thin_region_weight = .97f;
        in.thin_region_relax = 1;
        in.thin_region_camera_gate = true;
    } else {
        in.motion_policy = rr::MotionPolicy::KnownCameraOnly;
        in.reactive_policy = rr::ReactivePolicy::KnownNonReactive;
    }
    return in;
}
HRESULT make_resolver(IDirect3DDevice9* d, rr::TemporalPass& taa) {
    // As the route initialises it: the resolve and the identity copy (the four-channel lane's .r copy draw).
    HRESULT hr = taa.initialize(d, nullptr, reinterpret_cast<const DWORD*>(rr::temporal_resolve_program()), nullptr, nullptr,
                                reinterpret_cast<const DWORD*>(rr::hdr_writeback_program()));
    if (SUCCEEDED(hr) && flown) hr = taa.configure_far();
    return hr;
}
// A plume (axis -x, s = 1) moving +x at `speed` px per frame through `frames` resolves; on = with the stage, off = the
// same sequence without it. The last frame's current scene and resolved output of both.
struct Sequence {
    std::vector<float> current_on, current_off, resolved_on, resolved_off;
    float cx = 0, cy = 0;
};
Sequence run_sequence(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass, float speed_px,
                      bool flicker, const ee::Record& base, float Z, unsigned frames) {
    Sequence out;
    for (unsigned on = 0; on < 2; ++on) {
        rr::TemporalPass taa;
        check("taa initialize", make_resolver(d, taa));
        rr::Output o{};
        float pjx = 0, pjy = 0;
        for (unsigned n = 0; n < frames; ++n) {
            const float jx = float(halton(n % 8 + 1, 2) - .5), jy = float(halton(n % 8 + 1, 3) - .5);
            ee::Record r = base;
            r.origin[0] = base.origin[0] + (float(n) - float(frames - 1)) * speed_px / t.ppu(Z);
            scene.frame(0, 0, 0, 0, 500, flicker, n);
            StageContext ctx{&pass, frame_for(t, true, &r, 1, ep::Preset::standard, 0, jx, jy), S_OK, {}};
            rr::FrameInputs in = resolve_inputs(t, jx, jy, pjx, pjy, true);
            if (on) {
                in.stage_callback = &stage_callback;
                in.stage_context = &ctx;
            }
            check("resolve begin", d->BeginScene());
            const HRESULT hr = taa.run(in, &o);
            check("resolve end", d->EndScene());
            check("resolve run", hr);
            if (on && (!taa.diagnostics().stage_ran || FAILED(ctx.result)))
                throw std::runtime_error("stage did not run inside the resolve");
            pjx = jx;
            pjy = jy;
            if (n + 1 == frames) {
                (on ? out.current_on : out.current_off) = t.read(d);
                Com<IDirect3DSurface9> resolved;
                check("resolved surface", o.color->GetSurfaceLevel(0, &resolved.p));
                (on ? out.resolved_on : out.resolved_off) = t.read(d, resolved.p);
                t.window(r.origin[0], 0, Z, jx, jy, out.cx, out.cy);
            }
        }
    }
    return out;
}
void resolve_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass, float speed_px, bool flicker) {
    const float Z = 2000.f, ppu = t.ppu(Z), value = 30.f / ppu; // value 30 px: L 60 px at s = 1, nozzle width 7.5 px
    const ee::Record base = record(0, 0, Z, -1, 0, 0, value, 2.f);
    const unsigned frames = 16;
    const Sequence s = run_sequence(d, t, scene, pass, speed_px, flicker, base, Z, frames);
    const int iy = int(std::floor(s.cy + .5f)), ix = int(std::floor(s.cx + .5f));
    // The production look at the clock 0 (every frame): the pulsed length and the quad's front past the tip.
    const Built b = build_cpu(frame_for(t, true, &base, 1));
    const float Lpx = b.v[0].local[2] * ppu, front_px = b.v[2].local[0] * ppu, r0px = .7f * b.v[0].local[3] * ppu;
    float core, halo;
    core_levels(1.f, 1.f, core, halo);
    // Core survival: the plume's own share (on - off) of the resolved frame over its share of the current frame, on the
    // core's centre rows from 10 % to 70 % of its length (the overlapped part at every speed).
    double num = 0, den = 0;
    for (int y = iy - 1; y <= iy + 1; ++y)
        for (int x = ix - int(.7f * Lpx); x <= ix - int(.1f * Lpx); ++x) {
            const double c = luma(s.current_on, t.w, x, y) - luma(s.current_off, t.w, x, y);
            const double r = luma(s.resolved_on, t.w, x, y) - luma(s.resolved_off, t.w, x, y);
            if (c >= .5 * core) {
                num += r;
                den += c;
            }
        }
    const double ratio = den > 0 ? num / den : 0;
    // Trail: behind the quad's front edge by half the nozzle's halo reach + 1 px (the first look's margin: its edge sat
    // 1.125 sigma, half its 2.25-sigma reach, past the drawn front), the farthest pixel whose resolved plume share
    // exceeds 5 % of I_core, on the rows the body and its halo cover.
    const float half_reach_px = .5f * ep::halo_reach * b.v[0].shape[0] * b.v[0].local[3] * ppu;
    const int edge = ix - int(std::ceil(front_px + half_reach_px + 1.f));
    int trail = 0;
    float trail_max = 0.f;
    for (int x = edge - 1; x >= 0 && x > edge - 400; --x)
        for (int y = iy - int(r0px) - 3; y <= iy + int(r0px) + 3; ++y) {
            const float v = luma(s.resolved_on, t.w, x, y) - luma(s.resolved_off, t.w, x, y);
            trail_max = std::max(trail_max, v);
            if (v > .05f * core) trail = std::max(trail, edge - x);
        }
    std::printf("RESOLVE width=%u height=%u sky=%s speed_px=%.0f config=%s frames=%u core_survival=%.4f trail_px=%d trail_max=%.4f core=%.3f length_px=%.1f\n",
                t.w, t.h, flicker ? "flicker" : "dark", double(speed_px), flown ? "flown" : "camera_only", frames, ratio,
                trail, double(trail_max), double(core), double(Lpx));
    char label[64];
    std::snprintf(label, sizeof label, "resolve_core_%s_%.0fpx_%u", flicker ? "flicker" : "dark", double(speed_px), t.w);
    report(label, ratio >= .9);
    if (!flicker) {
        std::snprintf(label, sizeof label, "resolve_trail_dark_%.0fpx_%u", double(speed_px), t.w);
        report(label, trail <= 3);
    }
}

// A plane at the nozzle depth over x >= edge (the nozzle 10 px inside it).
void occlusion_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass, bool four) {
    const float Z = 2000.f, ppu = t.ppu(Z);
    float cx, cy;
    t.window(0, 0, Z, 0, 0, cx, cy);
    const int ix = int(std::floor(cx + .5f)), iy = int(std::floor(cy + .5f)), edge = ix - 10;
    float core, halo;
    core_levels(1.f, 1.f, core, halo);
    // Head-on: the exhaust points away (axis +z), behind the plane; the disc carries the look. Value 192 px: the nozzle
    // 48 px wide (the first look's case had value 48 px, its halo sigma 24 px).
    {
        const float value = 192.f / ppu;
        const ee::Record r = record(0, 0, Z, 0, 0, 1, value, 2.f);
        scene.frame(0, 0, 0, 0, 500);
        draw(d, pass, frame_for(t, four, &r, 1));
        const auto open = t.read(d);
        scene.frame(float(edge), 0, float(t.w), float(t.h), Z);
        draw(d, pass, frame_for(t, four, &r, 1));
        const auto cut = t.read(d);
        const float open_peak = peak(open, t.w, t.h, ix - 3, iy - 3, ix + 4, iy + 4);
        const float inside = peak(cut, t.w, t.h, edge, iy - 40, ix + 60, iy + 41);
        int rim = 0;
        for (int x = edge - 1; x >= 0 && x > edge - 300; --x)
            if (peak(cut, t.w, t.h, x, iy - 2, x + 1, iy + 3) >= .05f * open_peak) rim = edge - x;
        const float outside = peak(cut, t.w, t.h, edge - 30, iy - 30, edge, iy + 31);
        std::printf("OCCLUSION_HEADON width=%u lane=%s open_peak=%.3f inside_max=%.5f outside_max=%.4f rim_px=%d sigma_px=%.1f\n",
                    t.w, four ? "4ch" : "r32f", double(open_peak), double(inside), double(outside), rim,
                    double(.5f * ep::default_look.halo * ep::default_look.nozzle_width * value * ppu));
        char label[64];
        std::snprintf(label, sizeof label, "headon_core_hidden_%s_%u", four ? "4ch" : "r32f", t.w);
        report(label, open_peak >= .9f * core && inside <= 1e-3f);
        std::snprintf(label, sizeof label, "headon_rim_outside_%s_%u", four ? "4ch" : "r32f", t.w);
        report(label, outside > 0.f && rim >= 1);
    }
    // 20 degrees: the axis tilted towards the uncovered side; the core inside the silhouette hidden, past the edge
    // visible.
    {
        const float a = 20.f * 3.14159265f / 180.f, value = 90.f / ppu;
        const ee::Record r = record(0, 0, Z, -std::sin(a), 0, std::cos(a), value, 2.f);
        scene.frame(float(edge), 0, float(t.w), float(t.h), Z);
        draw(d, pass, frame_for(t, four, &r, 1));
        const auto cut = t.read(d);
        float tx, ty;
        t.window(-std::sin(a) * 2.f * value, 0, Z + std::cos(a) * 2.f * value, 0, 0, tx, ty);
        const float inside = peak(cut, t.w, t.h, edge, iy - 40, ix + 60, iy + 41);
        const float beyond = peak(cut, t.w, t.h, int(tx) + 4, iy - 1, edge - 4, iy + 2);
        int visible = 0;
        for (int x = edge - 1; x > int(tx); --x) visible += luma(cut, t.w, x, iy) >= .5f * core;
        int rim = 0;
        for (int y = iy + 1; y < iy + 200 && y < int(t.h); ++y)
            if (luma(cut, t.w, edge - 2, y) >= .05f * core) rim = y - iy;
        std::printf("OCCLUSION_20DEG width=%u lane=%s inside_max=%.5f beyond_max=%.3f core_visible_px=%d projected_tip_px=%.1f rim_rows_px=%d\n",
                    t.w, four ? "4ch" : "r32f", double(inside), double(beyond), visible, double(float(ix) - tx), rim);
        char label[64];
        std::snprintf(label, sizeof label, "deg20_core_hidden_inside_%s_%u", four ? "4ch" : "r32f", t.w);
        report(label, inside <= 1e-3f);
        std::snprintf(label, sizeof label, "deg20_core_visible_outside_%s_%u", four ? "4ch" : "r32f", t.w);
        report(label, beyond >= .9f * core && visible >= 10);
    }
    // Tail-on (reported): the exhaust points at the camera from a hull face at the nozzle depth; the occlusion bias
    // (0.5 value x facing) keeps the core visible over its own hull.
    {
        const float value = 48.f / ppu;
        const ee::Record r = record(0, 0, Z, 0, 0, -1, value, 2.f);
        scene.frame(0, 0, 0, 0, 500);
        draw(d, pass, frame_for(t, four, &r, 1));
        const float open_peak = peak(t.read(d), t.w, t.h, ix - 3, iy - 3, ix + 4, iy + 4);
        scene.frame(0, 0, float(t.w), float(t.h), Z);
        draw(d, pass, frame_for(t, four, &r, 1));
        const float on_hull = peak(t.read(d), t.w, t.h, ix - 3, iy - 3, ix + 4, iy + 4);
        std::printf("OCCLUSION_TAILON width=%u lane=%s open_peak=%.3f on_hull_peak=%.3f ratio=%.3f\n", t.w,
                    four ? "4ch" : "r32f", double(open_peak), double(on_hull), double(open_peak > 0 ? on_hull / open_peak : 0));
        char label[64];
        std::snprintf(label, sizeof label, "tailon_core_over_own_hull_%s_%u", four ? "4ch" : "r32f", t.w);
        report(label, on_hull >= .5f * open_peak);
    }
    // Off-centre (review fix 1): the nozzle at 90 % of the width (near the horizontal screen edge, where the camera-
    // facing billboard's side vector has a large view z component), the axis tilted 20 degrees towards the uncovered
    // side (up); a plane at the nozzle depth over the rows y >= edge (the nozzle 10 px inside it). The core inside the
    // silhouette hidden, the axis past the edge visible.
    {
        const float a = 20.f * 3.14159265f / 180.f, value = 90.f / ppu;
        const float X = (2.f * .9f - 1.f) * Z / t.m00();
        float ox, oy;
        t.window(X, 0, Z, 0, 0, ox, oy);
        const int jx = int(std::floor(ox + .5f)), jy = int(std::floor(oy + .5f)), edge_y = jy - 10;
        const float ay = std::sin(a), az = std::cos(a);
        const ee::Record r = record(X, 0, Z, 0, ay, az, value, 2.f);
        scene.frame(0, float(edge_y), float(t.w), float(t.h), Z);
        draw(d, pass, frame_for(t, four, &r, 1));
        const auto cut = t.read(d);
        const float inside = peak(cut, t.w, t.h, jx - 60, edge_y, jx + 61, jy + 61);
        // Along the projected axis above the edge (perspective moves it towards the centre as it recedes).
        const float L = 2.f * value;
        float beyond = 0.f;
        int visible = 0, last_row = -1;
        float tip_x = 0.f, tip_y = 0.f;
        t.window(X, ay * L, Z + az * L, 0, 0, tip_x, tip_y);
        for (unsigned k = 0; k <= 4000; ++k) {
            const float u = L * float(k) / 4000.f;
            float px, py;
            t.window(X, ay * u, Z + az * u, 0, 0, px, py);
            const int ix2 = int(std::floor(px + .5f)), iy2 = int(std::floor(py + .5f));
            if (iy2 >= edge_y - 4 || iy2 <= int(tip_y) + 4 || ix2 < 1 || ix2 + 1 >= int(t.w) || iy2 < 0) continue;
            const float l = peak(cut, t.w, t.h, ix2 - 1, iy2, ix2 + 2, iy2 + 1);
            beyond = std::max(beyond, l);
            if (iy2 != last_row) {
                last_row = iy2;
                visible += l >= .5f * core;
            }
        }
        std::printf("OCCLUSION_OFFCENTRE width=%u lane=%s nozzle_px=%d,%d inside_max=%.5f beyond_max=%.3f core_visible_px=%d projected_tip_px=%.1f,%.1f\n",
                    t.w, four ? "4ch" : "r32f", jx, jy, double(inside), double(beyond), visible, double(tip_x), double(tip_y));
        char label[64];
        std::snprintf(label, sizeof label, "offcentre_core_hidden_inside_%s_%u", four ? "4ch" : "r32f", t.w);
        report(label, inside <= 1e-3f);
        std::snprintf(label, sizeof label, "offcentre_core_visible_outside_%s_%u", four ? "4ch" : "r32f", t.w);
        report(label, beyond >= .9f * core && visible >= 10);
    }
}

// The own ship's plume tail-on, three values from the camera: the cap shrinks it to 0.12 H and halves its radiance.
void chase_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass) {
    const float value = 10.f;
    const ee::Record r = record(0, -2.f, 3.f * value, 0, 0, -1, value, 2.f);
    scene.frame(0, 0, 0, 0, 500);
    const auto rep = draw(d, pass, frame_for(t, true, &r, 1));
    const auto px = t.read(d);
    float pk = 0.f;
    for (UINT y = 0; y < t.h; ++y)
        for (UINT x = 0; x < t.w; ++x) pk = std::max(pk, luma(px, t.w, int(x), int(y)));
    int x0 = int(t.w), x1 = -1, y0 = int(t.h), y1 = -1;
    for (UINT y = 0; y < t.h; ++y)
        for (UINT x = 0; x < t.w; ++x)
            if (luma(px, t.w, int(x), int(y)) >= .05f * pk) {
                x0 = std::min(x0, int(x));
                x1 = std::max(x1, int(x));
                y0 = std::min(y0, int(y));
                y1 = std::max(y1, int(y));
            }
    const int extent = std::max(x1 - x0 + 1, y1 - y0 + 1);
    // The fade: the still look's peak (the disc's centre) on the view axis (the axial quad exactly edge-on: the disc
    // alone) here against the same nozzle at 30 values (q 0.25: no fade); the law is scale-free in nozzle widths, so
    // the ratio is the fade's 0.5.
    auto still_peak = [&](float z) {
        const ee::Record q = record(0, 0, z, 0, 0, -1, value, 2.f);
        scene.frame(0, 0, 0, 0, 500);
        draw(d, pass, frame_for(t, true, &q, 1, ep::Preset::standard, 0.f, 0.f, 0.f, &still));
        const auto img = t.read(d);
        float m = 0.f;
        for (UINT y = 0; y < t.h; ++y)
            for (UINT x = 0; x < t.w; ++x) m = std::max(m, luma(img, t.w, int(x), int(y)));
        return m;
    };
    const float near_peak = still_peak(3.f * value), far_peak = still_peak(30.f * value);
    const float ratio = far_peak > 0.f ? near_peak / far_peak : 0.f;
    std::printf("CHASE width=%u height=%u extent_px=%d cap_px=%.1f peak=%.3f capped=%u faded=%u nozzles=%u still_peak=%.3f far_still_peak=%.3f fade_ratio=%.3f\n",
                t.w, t.h, extent, double(ep::chase_cap * float(t.h)), double(pk), rep.stats.capped, rep.stats.faded,
                rep.stats.nozzles, double(near_peak), double(far_peak), double(ratio));
    char label[64];
    std::snprintf(label, sizeof label, "chase_cap_%u", t.w);
    report(label, rep.stats.capped == 1 && extent > 0 && float(extent) <= ep::chase_cap * float(t.h) + 2.f);
    std::snprintf(label, sizeof label, "chase_fade_%u", t.w);
    report(label, pk > 0.f && ratio >= .45f && ratio <= .55f);
}

void preset_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass) {
    const float Z = 2000.f, ppu = t.ppu(Z), value = 60.f / ppu;
    const ee::Record r = record(value * .5f, 0, Z, -1, 0, 0, value, 1.125f);
    float cx, cy;
    t.window(r.origin[0], 0, Z, 0, 0, cx, cy);
    const int ix = int(std::floor(cx + .5f)), iy = int(std::floor(cy + .5f));
    float cores[3], halos[3];
    for (unsigned p = 0; p < 3; ++p) {
        scene.frame(0, 0, 0, 0, 500);
        draw(d, pass, frame_for(t, true, &r, 1, ep::Preset(p)));
        const auto px = t.read(d);
        cores[p] = luma(px, t.w, ix - int(.2f * value * ppu), iy);
        halos[p] = luma(px, t.w, ix - int(.2f * value * ppu), iy + int(.15f * value * ppu)); // 0.6 nozzle widths off the axis
    }
    std::printf("PRESETS width=%u core=%.3f,%.3f,%.3f halo_at_06n=%.4f,%.4f,%.4f core_ratio=%.3f,%.3f halo_ratio=%.3f,%.3f\n",
                t.w, double(cores[0]), double(cores[1]), double(cores[2]), double(halos[0]), double(halos[1]),
                double(halos[2]), double(cores[0] / cores[1]), double(cores[2] / cores[1]), double(halos[0] / halos[1]),
                double(halos[2] / halos[1]));
    char label[64];
    std::snprintf(label, sizeof label, "presets_core_%u", t.w);
    report(label, std::fabs(cores[0] / cores[1] - .6f) <= .05f && std::fabs(cores[2] / cores[1] - 1.5f) <= .08f);
    std::snprintf(label, sizeof label, "presets_halo_order_%u", t.w);
    report(label, halos[0] < halos[1] && halos[1] < halos[2]);
}

double mean_of(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) s += x;
    return v.empty() ? 0. : s / double(v.size());
}
double cv_of(const std::vector<double>& v) {
    const double m = mean_of(v);
    double s = 0;
    for (double x : v) s += (x - m) * (x - m);
    return v.size() > 1 && m > 0 ? std::sqrt(s / double(v.size() - 1)) / m : 0.;
}
// The lag-1 autocorrelation of a series (1: each frame its predecessor; 0: uncorrelated frame to frame).
double lag1_of(const std::vector<double>& v) {
    const double m = mean_of(v);
    double num = 0, den = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        den += (v[i] - m) * (v[i] - m);
        if (i + 1 < v.size()) num += (v[i] - m) * (v[i + 1] - m);
    }
    return den > 0 ? num / den : 0.;
}
// (a) Alive, not strobing: the same nozzle over 30 frames at 60 fps (side view, value 200 px, s = 1, the production
// look): the luma of a 3x3 box on the axis at u = 0.2 of the game's length, per frame; its coefficient of variation
// (0.05..0.4), its lag-1 correlation (at least 0.5: the flow translates the noise at a constant speed, so a frame
// resembles its predecessor; a phase following the pulsed L decorrelates it from t ~ 10 s) and its mean against the design (the replica on the box's pixels with the noise at its mean, 0.4375, and
// each frame's pulsed length; within 10 %). The same frames through the resolve (the flown configuration, the stage
// callback, 8 frames of history first): the resolved box's variation, reported (the history's smoothing of the flow).
void temporal_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass) {
    const float Z = 2000.f, ppu = t.ppu(Z), value = 100.f / ppu, L0px = 2.f * value * ppu;
    const ee::Record r = record(L0px * .5f / ppu, 0, Z, -1, 0, 0, value, 2.f);
    float cx, cy;
    t.window(r.origin[0], 0, Z, 0, 0, cx, cy);
    const int bx = int(std::floor(cx - .2f * L0px + .5f)), by = int(std::floor(cy + .5f));
    const unsigned frames = 30, history = 8;
    std::vector<double> raw, design, resolved;
    for (unsigned n = 0; n < frames; ++n) {
        const float seconds = 10.f + float(n) / 60.f;
        const rr::EnginePlumesFrame f = frame_for(t, true, &r, 1, ep::Preset::standard, seconds);
        scene.frame(0, 0, 0, 0, 500);
        draw(d, pass, f);
        const auto box = read_region(d, t, nullptr, bx - 1, by - 1, bx + 2, by + 2);
        double m = 0;
        for (float v : box) m += v;
        raw.push_back(m / 9.);
        const Built b = build_cpu(f);
        const LawInputs in = law_of(b.v[0], ppu);
        double want = 0;
        for (int y = by - 1; y <= by + 1; ++y)
            for (int x = bx - 1; x <= bx + 1; ++x)
                want += replica(ep::default_look, in, (cx - float(x)) / in.n_px, (float(y) - cy) / in.n_px);
        design.push_back(want / 9.);
    }
    {
        rr::TemporalPass taa;
        check("taa initialize", make_resolver(d, taa));
        rr::Output o{};
        float pjx = 0, pjy = 0;
        for (unsigned n = 0; n < frames + history; ++n) {
            const float jx = float(halton(n % 8 + 1, 2) - .5), jy = float(halton(n % 8 + 1, 3) - .5);
            const float seconds = 10.f + (float(n) - float(history)) / 60.f;
            scene.frame(0, 0, 0, 0, 500);
            StageContext ctx{&pass, frame_for(t, true, &r, 1, ep::Preset::standard, seconds, jx, jy), S_OK, {}};
            rr::FrameInputs in = resolve_inputs(t, jx, jy, pjx, pjy, true);
            in.stage_callback = &stage_callback;
            in.stage_context = &ctx;
            check("resolve begin", d->BeginScene());
            const HRESULT hr = taa.run(in, &o);
            check("resolve end", d->EndScene());
            check("resolve run", hr);
            pjx = jx;
            pjy = jy;
            if (n < history) continue;
            Com<IDirect3DSurface9> surface;
            check("resolved surface", o.color->GetSurfaceLevel(0, &surface.p));
            const auto box = read_region(d, t, surface.p, bx - 1, by - 1, bx + 2, by + 2);
            double m = 0;
            for (float v : box) m += v;
            resolved.push_back(m / 9.);
        }
    }
    const double raw_mean = mean_of(raw), design_mean = mean_of(design), raw_cv = cv_of(raw), raw_lag1 = lag1_of(raw);
    const double ratio = design_mean > 0 ? raw_mean / design_mean : 0.;
    double step = 0;
    for (std::size_t i = 1; i < raw.size(); ++i) step = std::max(step, std::fabs(raw[i] - raw[i - 1]) / raw_mean);
    std::printf("TEMPORAL width=%u height=%u frames=%u dt_ms=16.7 box_u=0.2 raw_mean=%.4f raw_cv=%.4f raw_lag1=%.4f raw_max_step=%.4f design_mean=%.4f mean_over_design=%.4f resolved_mean=%.4f resolved_cv=%.4f\n",
                t.w, t.h, frames, raw_mean, raw_cv, raw_lag1, step, design_mean, ratio, mean_of(resolved), cv_of(resolved));
    char label[64];
    std::snprintf(label, sizeof label, "temporal_alive_%u", t.w);
    report(label, raw_cv >= .05 && raw_cv <= .4);
    std::snprintf(label, sizeof label, "temporal_lag1_%u", t.w);
    report(label, raw_lag1 >= .5);
    std::snprintf(label, sizeof label, "temporal_mean_design_%u", t.w);
    report(label, std::fabs(ratio - 1.) <= .1);
}
// (b) Bulge and taper: side view, value 100 px (the nozzle 25 px wide), s = 1, the production look without the length
// pulse, the halo and the ring (the body alone), 30 frames 0.1 s apart; on the frames' mean, the half-width of the
// columns at u = 0.1 and 0.9 down to 10 % of the column's peak, over the nozzle's half-width (0.5 n), against the same
// crossing of the CPU replica over the same frames (the mock-up's law with the CPU value noise at the pixel program's
// coordinates): within 6 % at u = 0.1 and 12 % at u = 0.9. The mock-up's width itself, w(0.1) / 0.5 >= 1.05 (the
// bulge past the nozzle), is checked from the law.
double crossing_of(const std::vector<double>& v, int rows, int by, float cy) {
    double pk = 0;
    for (double x : v) pk = std::max(pk, x);
    const double threshold = .1 * pk;
    // The crossings of 10 % of the peak on both sides of the axis row, interpolated between pixel centres (D3D9: on
    // integers; the row by + k lies k + by - cy from the axis).
    auto crossing = [&](int direction) {
        int k = 0;
        while (std::abs(k + direction) <= rows && v[std::size_t(rows + k + direction)] >= threshold) k += direction;
        if (std::abs(k + direction) > rows) return double(rows);
        const double a = v[std::size_t(rows + k)], b = v[std::size_t(rows + k + direction)];
        const double f = a > b ? (a - threshold) / (a - b) : 0.;
        return std::fabs(double(k) + double(direction) * f + double(by) - double(cy));
    };
    return .5 * (crossing(-1) + crossing(1));
}
float lab_width(const ep::Look& k, float u) { // the mock-up's w(u), nozzle widths
    float c[20];
    ep::pixel_constants(k, c);
    const float b = c[0] * (1.f - .55f * std::exp(-9.f * u)) * (1.f + .35f * smooth(0.f, .25f, u) * std::exp(-4.f * u));
    return std::min(c[0] + c[1] * u, b + c[2]) * std::max(1.f - c[3] * smooth(.6f, 1.f, u), .05f);
}
void shape_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass) {
    const float Z = 2000.f, ppu = t.ppu(Z), value = 100.f / ppu, Lpx = 2.f * value * ppu;
    const ee::Record r = record(Lpx * .5f / ppu, 0, Z, -1, 0, 0, value, 2.f);
    float cx, cy;
    t.window(r.origin[0], 0, Z, 0, 0, cx, cy);
    const int by = int(std::floor(cy + .5f)), rows = 80;
    const int columns[2] = {int(std::floor(cx - .1f * Lpx)), int(std::floor(cx - .9f * Lpx))};
    std::vector<double> sum[2] = {std::vector<double>(2 * rows + 1, 0.), std::vector<double>(2 * rows + 1, 0.)};
    std::vector<double> want[2] = {std::vector<double>(2 * rows + 1, 0.), std::vector<double>(2 * rows + 1, 0.)};
    const unsigned frames = 30;
    float n_px = 0.f;
    for (unsigned n = 0; n < frames; ++n) {
        const float seconds = 20.f + .1f * float(n);
        const rr::EnginePlumesFrame f = frame_for(t, true, &r, 1, ep::Preset::standard, seconds, 0.f, 0.f, &body_only);
        const Built b = build_cpu(f);
        const LawInputs in = law_of(b.v[0], ppu);
        n_px = in.n_px;
        scene.frame(0, 0, 0, 0, 500);
        draw(d, pass, f);
        for (unsigned c = 0; c < 2; ++c) {
            const auto col = read_region(d, t, nullptr, columns[c], by - rows, columns[c] + 1, by + rows + 1);
            const float x = (cx - float(columns[c])) / in.n_px;
            for (int i = 0; i <= 2 * rows; ++i) {
                sum[c][std::size_t(i)] += col[std::size_t(i)];
                const float y = (float(by - rows + i) - cy) / in.n_px;
                float n1 = 0.f, n2 = 0.f;
                noise_at(in, x, y, f.phase, seconds, n1, n2);
                want[c][std::size_t(i)] += replica(body_only, in, x, y, n1, n2);
            }
        }
    }
    float ratio[2], expected[2];
    for (unsigned c = 0; c < 2; ++c) {
        ratio[c] = float(crossing_of(sum[c], rows, by, cy)) / (.5f * n_px);
        expected[c] = float(crossing_of(want[c], rows, by, cy)) / (.5f * n_px);
    }
    const float lab01 = lab_width(body_only, .1f) / .5f, lab09 = lab_width(body_only, .9f) / .5f;
    std::printf("SHAPE width=%u height=%u frames=%u nozzle_px=%.1f L_px=%.1f half_width_u01_over_nozzle_half=%.3f half_width_u09_over_nozzle_half=%.3f expected_u01=%.3f expected_u09=%.3f lab_w_u01=%.3f lab_w_u09=%.3f\n",
                t.w, t.h, frames, double(n_px), double(Lpx), double(ratio[0]), double(ratio[1]), double(expected[0]),
                double(expected[1]), double(lab01), double(lab09));
    char label[64];
    std::snprintf(label, sizeof label, "shape_bulge_u01_%u", t.w);
    report(label, lab01 >= 1.05f && expected[0] > 0.f && std::fabs(ratio[0] / expected[0] - 1.f) <= .06f);
    std::snprintf(label, sizeof label, "shape_taper_u09_%u", t.w);
    report(label, expected[1] > 0.f && std::fabs(ratio[1] / expected[1] - 1.f) <= .12f && ratio[1] < ratio[0]);
}
// (c) Shock diamonds: side view, value 200 px, s = 1; the axis's radiance (r = 0) from the nozzle to u = 0.6, a 5-px
// running mean, its local maxima (the largest within +-6 px) at u in [0.02, 0.6]: at least 3 with the still look (the
// cells alone); the production look's count (the cells and the turbulence) reported.
void shock_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass) {
    const float Z = 2000.f, ppu = t.ppu(Z), value = 100.f / ppu, Lpx = 2.f * value * ppu;
    const ee::Record r = record(Lpx * .5f / ppu, 0, Z, -1, 0, 0, value, 2.f);
    float cx, cy;
    t.window(r.origin[0], 0, Z, 0, 0, cx, cy);
    const int by = int(std::floor(cy + .5f)), x_nozzle = int(std::floor(cx + .5f)), span = int(.6f * Lpx);
    unsigned counts[2] = {0, 0};
    float positions[2][16] = {};
    for (unsigned which = 0; which < 2; ++which) {
        scene.frame(0, 0, 0, 0, 500);
        draw(d, pass, frame_for(t, true, &r, 1, ep::Preset::standard, 30.f, 0.f, 0.f, which ? nullptr : &still));
        const auto row = read_region(d, t, nullptr, x_nozzle - span - 8, by, x_nozzle + 1, by + 1); // left to right
        // Profile from the nozzle (k = 0) to u = 0.6, a 5-px running mean.
        std::vector<float> axis(std::size_t(span) + 1, 0.f);
        const int n_row = int(row.size());
        for (int k = 0; k <= span; ++k) {
            float s = 0;
            for (int j = -2; j <= 2; ++j) {
                const int i = n_row - 1 - (k + j);
                s += i >= 0 && i < n_row ? row[std::size_t(i)] : 0.f;
            }
            axis[std::size_t(k)] = s / 5.f;
        }
        for (int k = int(.02f * Lpx); k <= span - 1; ++k) {
            bool top = axis[std::size_t(k)] > 0.f;
            for (int j = -6; j <= 6 && top; ++j) {
                const int i = k + j;
                if (j && i >= 0 && i <= span && axis[std::size_t(i)] > axis[std::size_t(k)]) top = false;
                if (j > 0 && i >= 0 && i <= span && axis[std::size_t(i)] == axis[std::size_t(k)]) top = false;
            }
            if (top && counts[which] < 16) positions[which][counts[which]++] = float(k) / Lpx;
        }
    }
    std::printf("SHOCK width=%u height=%u L_px=%.1f still_maxima=%u production_maxima=%u still_u=", t.w, t.h, double(Lpx), counts[0], counts[1]);
    for (unsigned i = 0; i < counts[0]; ++i) std::printf("%s%.3f", i ? "," : "", double(positions[0][i]));
    std::printf("\n");
    char label[64];
    std::snprintf(label, sizeof label, "shock_cells_%u", t.w);
    report(label, counts[0] >= 3);
}

void off_path_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass) {
    rr::EnginePlumesReport rep{};
    scene.frame(0, 0, 0, 0, 500);
    check("scene begin", d->BeginScene());
    const HRESULT hr = pass.run(frame_for(t, true, nullptr, 0), &rep);
    check("scene end", d->EndScene());
    report("off_no_record_no_call", hr == S_FALSE && rep.calls == 0 && !rep.drew);
    // A record that the screen rules drop (far away: under 1.5 px): S_FALSE, no render state.
    const ee::Record tiny = record(0, 0, 1e6f, -1, 0, 0, 1.f, 2.f);
    check("scene begin", d->BeginScene());
    const HRESULT hr2 = pass.run(frame_for(t, true, &tiny, 1), &rep);
    check("scene end", d->EndScene());
    std::printf("OFF_PATH empty=%08lx culled=%08lx culled_small=%u calls=%u\n", hr, hr2, rep.stats.culled_small, rep.calls);
    report("off_culled_no_state", hr2 == S_FALSE && rep.stats.culled_small == 1 && !rep.drew);
    std::vector<float> outputs[2];
    for (unsigned which = 0; which < 2; ++which) {
        rr::TemporalPass taa;
        check("taa", make_resolver(d, taa));
        scene.frame(0, 0, 0, 0, 500, true, 3);
        rr::FrameInputs in = resolve_inputs(t, .25f, -.25f, 0, 0, true);
        if (which) in.stage_callback = &idle_callback;
        rr::Output o{};
        check("resolve begin", d->BeginScene());
        check("resolve", taa.run(in, &o));
        check("resolve end", d->EndScene());
        Com<IDirect3DSurface9> rs;
        check("resolved surface", o.color->GetSurfaceLevel(0, &rs.p));
        outputs[which] = t.read(d, rs.p);
    }
    report("off_idle_callback_resolve_identical", outputs[0] == outputs[1]);
}

void fault_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass) {
    const float Z = 2000.f, value = 30.f / t.ppu(Z);
    const ee::Record r = record(0, 0, Z, -1, 0, 0, value, 2.f);
    pass.set_faults(2);
    scene.frame(0, 0, 0, 0, 500);
    rr::EnginePlumesReport rep{};
    check("scene begin", d->BeginScene());
    const HRESULT failed = pass.run(frame_for(t, true, &r, 1), &rep);
    check("scene end", d->EndScene());
    const auto failed_step = rep.failed;
    const auto again = draw(d, pass, frame_for(t, true, &r, 1));
    std::printf("FAULT result=%08lx step=%u next=%08lx drew=%u\n", failed, unsigned(failed_step), again.operation,
                unsigned(again.drew));
    report("fault_draw_step", failed == E_FAIL && failed_step == rr::EnginePlumesStep::Draw);
    report("fault_next_frame_draws", SUCCEEDED(again.operation) && again.drew);
}

void reset_case(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS& pp, rr::EnginePlumesPass& pass, Targets*& t, Scene*& scene,
                Quad& quad) {
    const unsigned before = pass.references();
    pass.before_reset();
    const unsigned released = pass.references();
    const HRESULT pending = pass.ensure_resources();
    delete scene;
    scene = nullptr;
    const UINT w = t->w, h = t->h;
    delete t;
    t = nullptr;
    const HRESULT reset = d->Reset(&pp);
    pass.after_reset(reset);
    const HRESULT ensured = pass.ensure_resources();
    const unsigned after = pass.references();
    t = new Targets(d, w, h);
    scene = new Scene(d, *t, quad);
    const float Z = 2000.f, value = 30.f / t->ppu(Z);
    const ee::Record r = record(0, 0, Z, -1, 0, 0, value, 2.f);
    scene->frame(0, 0, 0, 0, 500);
    const auto rep = draw(d, pass, frame_for(*t, true, &r, 1));
    float cx, cy;
    t->window(0, 0, Z, 0, 0, cx, cy);
    const float pk = peak(t->read(d), t->w, t->h, int(cx) - 20, int(cy) - 3, int(cx) + 1, int(cy) + 4);
    std::printf("RESET before=%u released=%u pending=%08lx reset=%08lx ensured=%08lx after=%u drew=%u peak=%.3f\n", before,
                released, pending, reset, ensured, after, unsigned(rep.drew), double(pk));
    report("reset_released", before == 5 && released == 0 && pending == E_FAIL); // internal flag: not a device code
    report("reset_recreated", SUCCEEDED(reset) && SUCCEEDED(ensured) && after == 5 && rep.drew && pk > 1.f);
}

// N nozzles spread over the view: depths 1,500..30,000, value 3..60 px, random axes and throttles (deterministic).
std::vector<ee::Record> crowd(Targets& t, unsigned n) {
    std::vector<ee::Record> out;
    std::uint32_t seed = 12345;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / 16777216.f;
    };
    for (unsigned i = 0; i < n; ++i) {
        const float z = 1500.f + 28500.f * rnd() * rnd();
        const float x = (rnd() * 1.6f - .8f) * z / t.m00(), y = (rnd() * 1.6f - .8f) * z / m11;
        const float px = 3.f + 57.f * rnd() * rnd();
        out.push_back(record(x, y, z, rnd() - .5f, rnd() - .5f, rnd() - .5f, px / t.ppu(z), .25f + 1.75f * rnd()));
    }
    return out;
}
struct Fence {
    Com<IDirect3DQuery9> q;
    explicit Fence(IDirect3DDevice9* d) { check("event query", d->CreateQuery(D3DQUERYTYPE_EVENT, &q.p)); }
};
// One frame tail (the shape the stage rides in production): an MRT quad into RT0 + RT1 + RT2 with the depth surface
// attached, the unbind the resolve's normalize does, optionally the stage, then a first target change with a quad on
// the other target. Fenced wall time of the whole tail; the stage's CPU submit (build + calls) measured separately.
double tail_ms(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass, Fence& fence,
               const rr::EnginePlumesFrame* stage, double* submit) {
    LARGE_INTEGER freq{};
    QueryPerformanceFrequency(&freq);
    auto ms = [&](LONGLONG a, LONGLONG b) { return double(b - a) * 1000. / double(freq.QuadPart); };
    check("scene begin", d->BeginScene());
    check("lead fence", complete_fence(fence.q.p));
    LARGE_INTEGER a{}, s0{}, s1{}, c{};
    QueryPerformanceCounter(&a);
    check("rt0", d->SetRenderTarget(0, t.scene_s.p));
    check("rt1", d->SetRenderTarget(1, t.motion_s.p));
    check("rt2", d->SetRenderTarget(2, t.lane4_s.p));
    check("ds", d->SetDepthStencilSurface(t.depth.p));
    check("blend off", d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
    check("z", d->SetRenderState(D3DRS_ZENABLE, FALSE));
    scene.quad.draw(d, t.w, t.h, scene.mrt_ps.p);
    check("no rt1", d->SetRenderTarget(1, nullptr));
    check("no rt2", d->SetRenderTarget(2, nullptr));
    check("no ds", d->SetDepthStencilSurface(nullptr));
    QueryPerformanceCounter(&s0);
    if (stage) {
        rr::EnginePlumesReport r;
        check("stage", pass.run(*stage, &r));
    }
    QueryPerformanceCounter(&s1);
    check("rt other", d->SetRenderTarget(0, t.other_s.p));
    check("blend off", d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
    scene.quad.draw(d, t.w, t.h, scene.zero_ps.p);
    check("rt scene", d->SetRenderTarget(0, t.scene_s.p));
    check("tail fence", complete_fence(fence.q.p));
    QueryPerformanceCounter(&c);
    check("scene end", d->EndScene());
    if (submit) *submit = ms(s0.QuadPart, s1.QuadPart);
    return ms(a.QuadPart, c.QuadPart);
}
void timing_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& pass, unsigned nozzles) {
    Fence fence(d);
    const auto records = crowd(t, nozzles);
    const rr::EnginePlumesFrame f = frame_for(t, true, records.data(), unsigned(records.size()));
    rr::EnginePlumesReport first{};
    check("scene begin", d->BeginScene());
    check("timing first", pass.run(f, &first));
    check("scene end", d->EndScene());
    std::vector<double> on, off, submit;
    for (unsigned i = 0; i < 126; ++i) {
        const bool run = i % 2 == 0;
        double s = 0;
        const double ms = tail_ms(d, t, scene, pass, fence, run ? &f : nullptr, &s);
        if (i < 6) continue;
        (run ? on : off).push_back(ms);
        if (run) submit.push_back(s);
    }
    const double chain = median(on) - median(off), gpu = std::max(0., chain - median(submit));
    std::printf("TIMING width=%u height=%u nozzles=%u drawn=%u fenced_on_ms=%.4f fenced_off_ms=%.4f submit_ms=%.4f chain_ms=%.4f gpu_ms=%.4f samples=%zu calls=%u method=tail_event_fenced\n",
                t.w, t.h, nozzles, first.stats.nozzles, median(on), median(off), median(submit), chain, gpu, on.size(),
                first.calls);
}
// The CPU build alone (into system memory, the same code the locked buffer receives): median microseconds per build.
void build_timing(Targets& t) {
    for (const unsigned n : {30u, 100u, 1024u}) {
        const auto records = crowd(t, n);
        const rr::EnginePlumesFrame f = frame_for(t, true, records.data(), unsigned(records.size()));
        std::vector<ep::Vertex> out(std::size_t(n) * ep::vertices_per_nozzle);
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        std::vector<double> us;
        unsigned drawn = 0;
        for (unsigned rep = 0; rep < 41; ++rep) {
            LARGE_INTEGER a{}, b{};
            QueryPerformanceCounter(&a);
            for (unsigned k = 0; k < 50; ++k)
                drawn = ep::build(f.records, f.record_count, nullptr, f.view, f.preset, float(rep * 50 + k) / 60.f, out.data(), n, nullptr);
            QueryPerformanceCounter(&b);
            us.push_back(double(b.QuadPart - a.QuadPart) * 1e6 / double(freq.QuadPart) / 50.);
        }
        std::printf("BUILD records=%u drawn=%u median_us=%.2f min_us=%.2f method=qpc_50x41\n", n, drawn, median(us),
                    *std::min_element(us.begin(), us.end()));
        if (n == 100) report("build_100_within_0.1ms", median(us) <= 100.);
    }
}
} // namespace
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        if (argc < 2) {
            std::puts("usage: engine_plumes_fixture <d3dx9_37.dll> [--only case,...]");
            return 2;
        }
        std::string only = argc >= 4 && !std::strcmp(argv[2], "--only") ? argv[3] : "";
        auto wanted = [&](const char* name) { return only.empty() || only.find(name) != std::string::npos; };
        HMODULE d3dx = LoadLibraryA(argv[1]);
        if (!d3dx) throw std::runtime_error("d3dx9_37.dll");
        auto cp = GetProcAddress(d3dx, "D3DXCompileShader");
        std::memcpy(&compiler, &cp, sizeof compiler);
        if (!compiler) throw std::runtime_error("D3DXCompileShader");
        WNDCLASSA cls{};
        cls.lpfnWndProc = DefWindowProcA;
        cls.hInstance = GetModuleHandleA(nullptr);
        cls.lpszClassName = "X3EnginePlumesFixture";
        RegisterClassA(&cls);
        HWND window = CreateWindowA(cls.lpszClassName, "X3 engine plumes fixture", WS_OVERLAPPEDWINDOW, 90, 90, 128, 128,
                                    nullptr, nullptr, cls.hInstance, nullptr);
        if (!window) throw std::runtime_error("window");
        HMODULE runtime = LoadLibraryA("d3d9.dll");
        if (!runtime) throw std::runtime_error("d3d9.dll");
        auto address = GetProcAddress(runtime, "Direct3DCreate9");
        IDirect3D9*(WINAPI * create)(UINT) = nullptr;
        std::memcpy(&create, &address, sizeof create);
        if (!create) throw std::runtime_error("Direct3DCreate9");
        IDirect3D9* api = create(D3D_SDK_VERSION);
        if (!api) throw std::runtime_error("Create9");
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferWidth = 64;
        pp.BackBufferHeight = 64;
        pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        pp.hDeviceWindow = window;
        Com<IDirect3DDevice9> device;
        check("device", api->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                          D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device.p));
        IDirect3DDevice9* d = device.p;
        D3DCAPS9 caps{};
        check("caps", d->GetDeviceCaps(&caps));
        D3DDISPLAYMODE mode{};
        check("mode", d->GetDisplayMode(0, &mode));
        rr::EnginePlumesPass pass;
        attach_case(d, caps, mode.Format, pass);
        {
            // The flown resolve configuration needs the far and camera-gate programs; without them the fixture falls back to
            // the camera-only resolve and says so.
            rr::TemporalPass probe;
            const HRESULT hr = make_resolver(d, probe);
            flown = SUCCEEDED(hr) && probe.far_available() && probe.camera_gate_available();
            std::printf("RESOLVE_CONFIG config=%s configure=%08lx far=%u camera_gate=%u\n", flown ? "flown" : "camera_only", hr,
                        unsigned(probe.far_available()), unsigned(probe.camera_gate_available()));
            report("resolve_flown_configuration", flown);
        }
        Quad quad(d);
        for (const auto size : {std::pair{1920u, 1080u}, std::pair{5120u, 1440u}}) {
            Targets* t = new Targets(d, size.first, size.second);
            Scene* scene = new Scene(d, *t, quad);
            std::printf("SIZE width=%u height=%u\n", size.first, size.second);
            if (wanted("length")) length_case(d, *t, *scene, pass);
            if (wanted("resolve"))
                for (const bool flicker : {false, true})
                    for (const float speed : {0.f, 4.f, 8.f}) resolve_case(d, *t, *scene, pass, speed, flicker);
            if (wanted("occlusion")) {
                occlusion_case(d, *t, *scene, pass, true);
                if (size.first == 1920) occlusion_case(d, *t, *scene, pass, false);
            }
            if (wanted("chase")) chase_case(d, *t, *scene, pass);
            if (wanted("presets")) preset_case(d, *t, *scene, pass);
            if (wanted("temporal")) temporal_case(d, *t, *scene, pass);
            if (wanted("shape")) shape_case(d, *t, *scene, pass);
            if (wanted("shock")) shock_case(d, *t, *scene, pass);
            if (wanted("timing"))
                for (const unsigned n : {30u, 100u}) timing_case(d, *t, *scene, pass, n);
            if (size.first == 1920) {
                if (wanted("build")) build_timing(*t);
                if (wanted("off")) off_path_case(d, *t, *scene, pass);
                if (wanted("fault")) fault_case(d, *t, *scene, pass);
                if (wanted("reset")) reset_case(d, pp, pass, t, scene, quad);
            }
            delete scene;
            delete t;
        }
        pass.detach();
        report("detach_released", pass.references() == 0);
        std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
        // The device and runtime teardown is not under test, and Wine has hung in it once after a complete run
        // (2026-10-01): leave without it.
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), failures ? 1u : 0u);
        return failures ? 1 : 0;
    } catch (const std::exception& error) {
        std::printf("RESULT FAIL exception=%s checks=%u failures=%u\n", error.what(), checks, failures + 1);
        std::fflush(stdout);
        TerminateProcess(GetCurrentProcess(), 1u);
        return 1;
    }
}
