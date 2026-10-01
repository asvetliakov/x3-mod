// Engine ribbons fixture, phase 3 (docs/architecture/engine-effects-modern.md sections 3-6; docs/verification/
// engine-effects.md). Standalone D3D9 (builtin d3d9 through the runner's override), no game, no proxy: the production
// EngineRibbonsPass and EnginePlumesPass (the stage callback runs both as run_engine_plumes does: plumes, then ribbons),
// the production pool / builder (engine_ribbons_core.h), the fog law (fog_transmittance.h) and the production
// TemporalPass on synthetic FP16 scenes and lanes at 1920x1080 and 5120x1440. Identity view, square pixels, m11 1.7,
// a 60 Hz clock (frame n at n / 60 s); value 30 px at the nozzle's depth (T 0.5 s).
//   attach      programs, buffers, slots; the FP16 refusal (fault) -> D3DERR_NOTAVAILABLE, nothing held
//   resolve     a nozzle moving 4 / 8 px per frame through the real resolve (the flown configuration) over the dark
//               and the flickering sky, with and without ribbons: no gap at the nozzle, the near-nozzle survival
//               (resolved / current share, u 0.05..0.3), the trailing length against T v s, its continuity
//   floor       a distant nozzle: the strip's width held at 3 px
//   stationary  a nozzle at rest for 40 frames: one sample, nothing drawn
//   seta        4 px per frame, then 80 px per frame for 10 frames: the strip continuous over the stretched path
//   cut         a 150 px step back (under the 8-value jump rule): with the cut no bridge, without it the bridge
//   loss        the record gone: half the radiance after 0.15 s, evicted after 0.3 s
//   cap         300 nozzles: 256 ribbons, 44 overflow
//   fog         a fogged law: plume core and ribbon attenuated by the law's T at the nozzle's distance
//   off/fault   nothing live: S_FALSE and no device call; a failed draw reports its step, the next frame draws
//   reset       objects released and the pool cleared before Reset, recreated after; the next frames draw
//   timing      EVENT-fenced stage cost of the ribbon draw at 30 / 100 ribbons; the CPU update + build
// Validation-only readback; never launches the game.
#include "../../src/renderer/engine_ribbons_pass.h"
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
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
namespace ep = x3m::engine_plumes;
namespace er = x3m::engine_ribbons;
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
const double frame_seconds = 1. / 60.;
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
    Com<IDirect3DTexture9> scene, lane4, motion, other;
    Com<IDirect3DSurface9> scene_s, lane4_s, motion_s, other_s, depth, staging;
    Targets(IDirect3DDevice9* d, UINT width, UINT height)
        : w(width), h(height) {
        auto rt = [&](D3DFORMAT f, Com<IDirect3DTexture9>& t, Com<IDirect3DSurface9>& s, const char* what) {
            check(what, d->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, f, D3DPOOL_DEFAULT, &t.p, nullptr));
            check(what, t->GetSurfaceLevel(0, &s.p));
        };
        rt(D3DFMT_A16B16G16R16F, scene, scene_s, "scene");
        rt(D3DFMT_A32B32G32R32F, lane4, lane4_s, "lane4");
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
    float m00() const { return m11 * float(h) / float(w); }
    float ppu(float z) const { return m11 * float(h) * .5f / z; }
    float world_x(float px, float z) const { return (px - float(w) * .5f) / ppu(z); } // pixel column -> view x at z
    void window(float x, float y, float z, float jx, float jy, float& px, float& py) const {
        px = ((m00() * x / z + 2.f * jx / float(w)) + 1.f) * float(w) * .5f;
        py = ((-(m11 * y / z - 2.f * jy / float(h))) + 1.f) * float(h) * .5f;
    }
};
struct Scene {
    IDirect3DDevice9* d;
    Targets& t;
    Quad& quad;
    Com<IDirect3DPixelShader9> lane4_ps, motion_ps, sky_ps, mrt_ps, zero_ps;
    Scene(IDirect3DDevice9* device, Targets& targets, Quad& q)
        : d(device), t(targets), quad(q) {
        std::vector<DWORD> code;
        compile("float4 rect:register(c0);float4 depth:register(c1);float4 main(float2 p:VPOS):COLOR0{bool inside=p.x>=rect.x&&p.x<rect.z&&p.y>=rect.y&&p.y<rect.w;float z=depth.x;return inside?float4(1.000003+(-6.0000184)/z,0,z,0):float4(-1,0,0,0);}",
                "ps_3_0", code);
        check("lane4 ps", d->CreatePixelShader(code.data(), &lane4_ps.p));
        compile("float4 main(float2 p:VPOS):COLOR0{return float4(0,0,0,-1);}", "ps_3_0", code);
        check("motion ps", d->CreatePixelShader(code.data(), &motion_ps.p));
        compile("float4 frame:register(c0);float4 main(float2 p:VPOS):COLOR0{float h=frac(sin(dot(p,float2(12.9898,78.233))+frame.x*1.61803)*43758.5453);return float4(h,h,h,0);}",
                "ps_3_0", code);
        check("sky ps", d->CreatePixelShader(code.data(), &sky_ps.p));
        compile("struct O{float4 c0:COLOR0;float4 c1:COLOR1;float4 c2:COLOR2;};O main(float2 p:VPOS){O o;o.c0=float4(0.001,0.001,0.002,0);o.c1=float4(0,0,0,-1);o.c2=float4(-1,0,0,0);return o;}",
                "ps_3_0", code);
        check("mrt ps", d->CreatePixelShader(code.data(), &mrt_ps.p));
        compile("float4 main(float2 p:VPOS):COLOR0{return float4(0,0,0,0);}", "ps_3_0", code);
        check("zero ps", d->CreatePixelShader(code.data(), &zero_ps.p));
    }
    void frame(bool flicker = false, unsigned n = 0) {
        check("blend off", d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE));
        check("z off", d->SetRenderState(D3DRS_ZENABLE, FALSE));
        check("cull", d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE));
        check("no rt1", d->SetRenderTarget(1, nullptr));
        check("no rt2", d->SetRenderTarget(2, nullptr));
        check("no ds", d->SetDepthStencilSurface(nullptr));
        const float rect[4] = {0, 0, 0, 0}, dz[4] = {500, 0, 0, 0}, fr[4] = {float(n), 0, 0, 0};
        check("rect", d->SetPixelShaderConstantF(0, rect, 1));
        check("depth", d->SetPixelShaderConstantF(1, dz, 1));
        check("rt lane4", d->SetRenderTarget(0, t.lane4_s.p));
        quad.draw(d, t.w, t.h, lane4_ps.p);
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
// A synthetic main-jet record (identity view: origin and axis in view space), cluster white.
ee::Record record(float x, float y, float z, float value, float s, std::uint32_t handle = 0x1234) {
    ee::Record r{};
    r.origin[0] = x;
    r.origin[1] = y;
    r.origin[2] = z;
    r.axis[0] = -1.f;
    r.size = value;
    r.s = s;
    r.z = .25f + 1.75f * s;
    r.ratio = r.z;
    r.node_handle = handle;
    r.model = 20000;
    r.body = -1;
    r.flags = std::uint16_t(unsigned(ee::white) << ee::cluster_shift);
    return r;
}
rr::EnginePlumesFrame frame_for(Targets& t, const ee::Record* records, unsigned count, std::uint32_t clock = 0,
                                float jx = 0.f, float jy = 0.f, ep::Preset preset = ep::Preset::standard) {
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
    f.lane_four_channel = true;
    f.lane = t.lane4.p;
    f.records = records;
    f.record_count = count;
    f.preset = preset;
    f.frame = clock;
    return f;
}
float luma(const std::vector<float>& px, UINT w, int x, int y) {
    const std::size_t i = (std::size_t(y) * w + std::size_t(x)) * 4;
    return std::max(px[i], std::max(px[i + 1], px[i + 2]));
}
float row_peak(const std::vector<float>& px, UINT w, UINT h, int x, int y0, int y1) {
    float m = 0;
    for (int y = std::max(y0, 0); y <= std::min(y1, int(h) - 1); ++y) m = std::max(m, luma(px, w, x, y));
    return m;
}
// The stage as run_engine_plumes runs it: the plumes, then the ribbons; S_OK when either drew, a failure fails it.
struct StageContext {
    rr::EnginePlumesPass* plumes = nullptr;
    rr::EngineRibbonsPass* ribbons = nullptr;
    rr::EnginePlumesFrame frame{};
    rr::EngineRibbonsFrame rframe{};
    HRESULT result = S_FALSE;
    rr::EnginePlumesReport preport{};
    rr::EngineRibbonsReport rreport{};
};
HRESULT run_stage(StageContext& c) {
    HRESULT hr = S_FALSE;
    if (c.plumes) hr = c.plumes->run(c.frame, &c.preport);
    if (SUCCEEDED(hr) && c.ribbons) {
        c.rframe.base = &c.frame;
        const HRESULT r = c.ribbons->run(c.rframe, &c.rreport);
        if (FAILED(r) || r == S_OK) hr = r;
    }
    c.result = hr;
    return hr;
}
HRESULT stage_callback(void* context, IDirect3DDevice9*) noexcept {
    return run_stage(*static_cast<StageContext*>(context));
}
bool flown = true;
rr::FrameInputs resolve_inputs(Targets& t, float jx, float jy, float pjx, float pjy) {
    rr::FrameInputs in;
    in.color = t.scene.p;
    in.current_depth = t.lane4.p;
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
    HRESULT hr = taa.initialize(d, nullptr, reinterpret_cast<const DWORD*>(rr::temporal_resolve_program()), nullptr, nullptr,
                                reinterpret_cast<const DWORD*>(rr::hdr_writeback_program()));
    if (SUCCEEDED(hr) && flown) hr = taa.configure_far();
    return hr;
}
// One frame of a sequence: the nozzle (present or not) at pixel column x, the cut flag.
struct Step {
    bool present = true;
    float x_px = 0.f;
    bool cut = false;
};
struct Run {
    std::vector<float> current, resolved;
    rr::EngineRibbonsReport last{};
    float jx = 0, jy = 0;
};
// `frames` resolves with the stage (plumes and/or ribbons), the nozzle per `at(n)` at depth Z, value `value`; the last
// frame's current scene and resolved output. The first frame carries a cut so the pool starts empty.
Run run_sequence(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass* plumes, rr::EngineRibbonsPass* ribbons,
                 unsigned frames, bool flicker, float Z, float value, const std::function<Step(unsigned)>& at,
                 bool resolve = true) {
    Run out;
    rr::TemporalPass taa;
    if (resolve) check("taa initialize", make_resolver(d, taa));
    rr::Output o{};
    float pjx = 0, pjy = 0;
    for (unsigned n = 0; n < frames; ++n) {
        const float jx = resolve ? float(halton(n % 8 + 1, 2) - .5) : 0.f, jy = resolve ? float(halton(n % 8 + 1, 3) - .5) : 0.f;
        const Step s = at(n);
        const ee::Record r = record(t.world_x(s.x_px, Z), 0, Z, value, 1.f);
        scene.frame(flicker, n);
        StageContext ctx;
        ctx.plumes = plumes;
        ctx.ribbons = ribbons;
        ctx.frame = frame_for(t, s.present ? &r : nullptr, s.present ? 1u : 0u, n, jx, jy);
        ctx.rframe.seconds = 10. + double(n) * frame_seconds;
        ctx.rframe.cut = s.cut || n == 0;
        if (resolve) {
            rr::FrameInputs in = resolve_inputs(t, jx, jy, pjx, pjy);
            in.stage_callback = &stage_callback;
            in.stage_context = &ctx;
            check("resolve begin", d->BeginScene());
            const HRESULT hr = taa.run(in, &o);
            check("resolve end", d->EndScene());
            check("resolve run", hr);
            if (!taa.diagnostics().stage_ran) throw std::runtime_error("stage did not run inside the resolve");
        } else {
            check("scene begin", d->BeginScene());
            run_stage(ctx);
            check("scene end", d->EndScene());
        }
        check("stage", ctx.result);
        pjx = jx;
        pjy = jy;
        if (n + 1 == frames) {
            out.current = t.read(d);
            if (resolve) {
                Com<IDirect3DSurface9> resolved;
                check("resolved surface", o.color->GetSurfaceLevel(0, &resolved.p));
                out.resolved = t.read(d, resolved.p);
            }
            out.last = ctx.rreport;
            out.jx = jx;
            out.jy = jy;
        }
    }
    return out;
}

void attach_case(IDirect3DDevice9* d, const D3DCAPS9& caps, D3DFORMAT format, rr::EngineRibbonsPass& pass) {
    const HRESULT attached = pass.attach(d, *reinterpret_cast<void* const* const*>(d), caps, format);
    const auto& c = pass.caps();
    std::printf("ATTACH result=%08lx enabled=%u reason=%s vs_slots=%u ps_slots=%u fp16_blending=%08lx references=%u vb_bytes=%u ib_bytes=%u pool_bytes=%u\n",
                attached, unsigned(c.enabled), c.reason, c.vs_slots, c.ps_slots, c.fp16_blending, pass.references(),
                rr::EngineRibbonsPass::vertex_bytes, rr::EngineRibbonsPass::index_bytes, unsigned(sizeof(er::Pool)));
    report("attach", SUCCEEDED(attached) && c.enabled && pass.references() == 5 && c.fp16_blending == D3D_OK);
    auto* refused = new rr::EngineRibbonsPass;
    refused->set_faults(1);
    const HRESULT hr = refused->attach(d, *reinterpret_cast<void* const* const*>(d), caps, format);
    std::printf("FP16_REFUSED result=%08lx enabled=%u reason=%s references=%u\n", hr, unsigned(refused->caps().enabled),
                refused->caps().reason, refused->references());
    report("fp16_refused", hr == D3DERR_NOTAVAILABLE && !refused->caps().enabled &&
                               !std::strcmp(refused->caps().reason, "fp16_blending") && refused->references() == 0);
    delete refused;
}

// The moving nozzle through the resolve: with ribbons (a) and without (b), plumes on in both.
void resolve_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& plumes, rr::EngineRibbonsPass& ribbons,
                  float speed, bool flicker) {
    const float Z = 2000.f, value = 30.f / t.ppu(Z);
    const unsigned frames = 48;
    const float x_end = float(t.w) * .5f + 300.f;
    auto at = [&](unsigned n) {
        Step s;
        s.x_px = x_end - float(frames - 1 - n) * speed;
        return s;
    };
    const Run a = run_sequence(d, t, scene, &plumes, &ribbons, frames, flicker, Z, value, at);
    const Run b = run_sequence(d, t, scene, &plumes, nullptr, frames, flicker, Z, value, at);
    float cx, cy;
    t.window(t.world_x(x_end, Z), 0, Z, a.jx, a.jy, cx, cy);
    const int ix = int(std::floor(cx + .5f)), iy = int(std::floor(cy + .5f));
    float T = 0.f;
    er::trail_seconds(value, &T);
    const float expected = T * speed * 60.f; // px: T x v x s, s = 1
    const float i0 = er::radiance_high;
    // The ribbon's share of the current frame along the centre row: no gap from the nozzle back, the trailing length.
    auto share = [&](const std::vector<float>& on, const std::vector<float>& off, int x, int y) {
        return luma(on, t.w, x, y) - luma(off, t.w, x, y);
    };
    float at_nozzle = 0.f;
    for (int x = ix - 3; x <= ix - 1; ++x) at_nozzle = std::max(at_nozzle, row_peak(a.current, t.w, t.h, x, iy - 1, iy + 1) - row_peak(b.current, t.w, t.h, x, iy - 1, iy + 1));
    int length = 0, gaps = 0;
    for (int x = ix - 1; x >= 0 && x > ix - int(2.f * expected); --x) {
        const float v = std::max(share(a.current, b.current, x, iy), std::max(share(a.current, b.current, x, iy - 1), share(a.current, b.current, x, iy + 1)));
        if (v >= .02f * i0) length = ix - x;
        else if (x > ix - int(.9f * expected)) ++gaps;
    }
    // Near-nozzle survival: resolved over current share, u 0.05..0.3, centre rows.
    double num = 0, den = 0;
    for (int y = iy - 1; y <= iy + 1; ++y)
        for (int x = ix - int(.3f * expected); x <= ix - int(.05f * expected); ++x) {
            const double c = share(a.current, b.current, x, y), r = share(a.resolved, b.resolved, x, y);
            if (c >= .25 * i0) {
                num += r;
                den += c;
            }
        }
    const double survival = den > 0 ? num / den : 0;
    int resolved_length = 0;
    for (int x = ix - 1; x >= 0 && x > ix - int(2.f * expected); --x)
        if (share(a.resolved, b.resolved, x, iy) >= .02f * i0) resolved_length = ix - x;
    std::printf("RIBBON_RESOLVE width=%u height=%u sky=%s speed_px=%.0f frames=%u expected_px=%.1f length_px=%d resolved_length_px=%d gaps=%d at_nozzle=%.4f survival=%.4f ribbons=%u samples=%u live=%u calls=%u\n",
                t.w, t.h, flicker ? "flicker" : "dark", double(speed), frames, double(expected), length, resolved_length, gaps,
                double(at_nozzle), survival, a.last.stats.ribbons, a.last.stats.samples, a.last.update.live, a.last.calls);
    char label[80];
    std::snprintf(label, sizeof label, "ribbon_no_gap_at_nozzle_%s_%.0fpx_%u", flicker ? "flicker" : "dark", double(speed), t.w);
    report(label, at_nozzle >= .5f * i0 && gaps == 0);
    std::snprintf(label, sizeof label, "ribbon_survival_%s_%.0fpx_%u", flicker ? "flicker" : "dark", double(speed), t.w);
    report(label, survival >= .9);
    std::snprintf(label, sizeof label, "ribbon_length_%s_%.0fpx_%u", flicker ? "flicker" : "dark", double(speed), t.w);
    report(label, std::fabs(float(length) - expected) <= .1f * expected);
}

// A distant nozzle: value 4 px, the strip's half-width 1.2 px at the nozzle -> held at 1.5 px (3 px wide).
void floor_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EngineRibbonsPass& ribbons) {
    const float Z = 20000.f, value = 4.f / t.ppu(Z), speed = 4.f;
    const unsigned frames = 40;
    const float x_end = float(t.w) * .5f + 100.f;
    const Run a = run_sequence(d, t, scene, nullptr, &ribbons, frames, false, Z, value, [&](unsigned n) {
        Step s;
        s.x_px = x_end - float(frames - 1 - n) * speed;
        return s;
    }, false);
    float cx, cy;
    t.window(t.world_x(x_end, Z), 0, Z, 0, 0, cx, cy);
    const int ix = int(std::floor(cx + .5f)), iy = int(std::floor(cy + .5f));
    const int probe = ix - 30; // u ~ 0.25 of the 120 px trail
    const float pk = row_peak(a.current, t.w, t.h, probe, iy - 8, iy + 8);
    int width = 0;
    for (int y = iy - 8; y <= iy + 8; ++y) width += luma(a.current, t.w, probe, y) >= .1f * pk && pk > 0.f;
    std::printf("RIBBON_FLOOR width=%u value_px=%.1f nozzle_half_px=%.2f width_px=%d peak=%.4f floored=%u\n", t.w,
                double(value * t.ppu(Z)), double(er::width_scale * er::nozzle_half_width * value * t.ppu(Z)), width, double(pk),
                a.last.stats.floored);
    char label[64];
    std::snprintf(label, sizeof label, "ribbon_floor_3px_%u", t.w);
    report(label, a.last.stats.floored >= 1 && width >= 2 && width <= 4 && pk > .1f);
}

void stationary_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EngineRibbonsPass& ribbons) {
    const float Z = 2000.f, value = 30.f / t.ppu(Z);
    const Run a = run_sequence(d, t, scene, nullptr, &ribbons, 40, false, Z, value, [&](unsigned) {
        Step s;
        s.x_px = float(t.w) * .5f;
        return s;
    }, false);
    const er::Ribbon* r = nullptr;
    for (const auto& x : ribbons.pool().ribbons)
        if (x.live) r = &x;
    float pk = 0;
    for (std::size_t i = 0; i < a.current.size(); i += 4) pk = std::max(pk, a.current[i]);
    std::printf("RIBBON_STATIONARY width=%u live=%u samples=%u length=%.3f drawn=%u peak=%.5f\n", t.w, a.last.update.live,
                r ? r->count : 0u, double(r ? r->length : -1.f), a.last.stats.ribbons, double(pk));
    char label[64];
    std::snprintf(label, sizeof label, "ribbon_stationary_one_sample_%u", t.w);
    report(label, r && r->count == 1 && r->length == 0.f && a.last.stats.ribbons == 0 && pk == 0.f);
}

// 4 px per frame for 40 frames, then 80 px per frame for 10: the strip covers the stretched path without a gap.
void seta_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EngineRibbonsPass& ribbons) {
    const float Z = 2000.f, value = 30.f / t.ppu(Z);
    const unsigned frames = 50;
    const float start = 300.f;
    auto x_of = [&](unsigned n) { return n < 40 ? start + 4.f * float(n) : start + 4.f * 39.f + 80.f * float(n - 39); };
    const Run a = run_sequence(d, t, scene, nullptr, &ribbons, frames, false, Z, value, [&](unsigned n) {
        Step s;
        s.x_px = x_of(n);
        return s;
    });
    float cx, cy;
    t.window(t.world_x(x_of(frames - 1), Z), 0, Z, a.jx, a.jy, cx, cy);
    const int ix = int(std::floor(cx + .5f)), iy = int(std::floor(cy + .5f));
    const int seta_px = 80 * 10;
    int gaps = 0;
    float weakest = 1e9f;
    for (int x = ix - 2; x > ix - int(.9f * float(seta_px)); --x) {
        const float v = row_peak(a.current, t.w, t.h, x, iy - 2, iy + 2);
        weakest = std::min(weakest, v);
        gaps += v <= 1e-3f;
    }
    std::printf("RIBBON_SETA width=%u path_px=%d gaps=%d weakest=%.4f samples=%u jumps=%llu live=%u\n", t.w, seta_px, gaps,
                double(weakest), a.last.stats.samples, static_cast<unsigned long long>(ribbons.pool().jumps), a.last.update.live);
    char label[64];
    std::snprintf(label, sizeof label, "ribbon_seta_continuous_%u", t.w);
    report(label, gaps == 0 && a.last.update.live == 1 && ribbons.pool().jumps == 0);
}

// A 150 px step back (under the 8-value jump rule) with and without the cut: the cut leaves no bridge.
void cut_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EngineRibbonsPass& ribbons) {
    const float Z = 2000.f, value = 30.f / t.ppu(Z);
    const unsigned frames = 34; // 30 frames at 4 px, the step at frame 30, then 3 more
    const float start = float(t.w) * .5f;
    auto x_of = [&](unsigned n) { return n < 30 ? start + 4.f * float(n) : start + 4.f * 29.f - 150.f + 4.f * float(n - 30); };
    int bridge[2] = {0, 0};
    float old_x = 0.f, new_x = 0.f;
    unsigned long long cut_clears[2] = {0, 0};
    for (unsigned with_cut = 0; with_cut < 2; ++with_cut) {
        const unsigned long long before = ribbons.pool().cut_clears;
        const Run a = run_sequence(d, t, scene, nullptr, &ribbons, frames, false, Z, value, [&](unsigned n) {
            Step s;
            s.x_px = x_of(n);
            s.cut = with_cut && n == 30;
            return s;
        });
        cut_clears[with_cut] = ribbons.pool().cut_clears - before;
        float cx, cy, ox, oy;
        t.window(t.world_x(x_of(frames - 1), Z), 0, Z, a.jx, a.jy, cx, cy);
        t.window(t.world_x(x_of(29), Z), 0, Z, a.jx, a.jy, ox, oy);
        new_x = cx;
        old_x = ox;
        const int iy = int(std::floor(cy + .5f));
        // Between the new nozzle (+3 px) and the old one: the bridge's pixels.
        for (int x = int(cx) + 3; x <= int(ox); ++x) bridge[with_cut] += row_peak(a.current, t.w, t.h, x, iy - 2, iy + 2) > 1e-3f;
    }
    std::printf("RIBBON_CUT width=%u old_px=%.1f new_px=%.1f bridge_without_cut=%d bridge_with_cut=%d cut_clears=%llu,%llu\n", t.w,
                double(old_x), double(new_x), bridge[0], bridge[1], cut_clears[0], cut_clears[1]);
    char label[64];
    std::snprintf(label, sizeof label, "ribbon_cut_no_bridge_%u", t.w);
    // The first frame of each run carries a cut too (the pool starts empty): one clear without the step's cut, two with.
    report(label, bridge[1] == 0 && bridge[0] > 50 && cut_clears[1] == cut_clears[0] + 1);
}

// The record gone at frame 30: radiance x 0.5 at 0.15 s (9 frames), evicted at 0.3 s (18 frames).
void loss_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EngineRibbonsPass& ribbons) {
    const float Z = 2000.f, value = 30.f / t.ppu(Z);
    float peaks[3] = {0, 0, 0};
    unsigned live[3] = {0, 0, 0}, fading[3] = {0, 0, 0};
    const unsigned extra[3] = {1, 10, 19}; // frames after the last record: 1 = the first missing frame (age 1/60 s)
    unsigned long long evictions = 0;
    for (unsigned k = 0; k < 3; ++k) {
        const unsigned long long before = ribbons.pool().evictions;
        const Run a = run_sequence(d, t, scene, nullptr, &ribbons, 30 + extra[k], false, Z, value, [&](unsigned n) {
            Step s;
            s.present = n < 30;
            s.x_px = float(t.w) * .5f + 4.f * float(std::min(n, 29u));
            return s;
        }, false);
        for (std::size_t i = 0; i < a.current.size(); i += 4) peaks[k] = std::max(peaks[k], a.current[i]);
        live[k] = a.last.update.live;
        fading[k] = a.last.update.fading;
        if (k == 2) evictions = ribbons.pool().evictions - before;
    }
    // Radiance at age a: 1 - a / 0.3; the peaks are at the strip's nozzle end.
    const float age1 = float(1. / 60.), age10 = float(10. / 60.);
    const float ratio = peaks[1] / peaks[0], want = (1.f - age10 / er::fade_seconds) / (1.f - age1 / er::fade_seconds);
    std::printf("RIBBON_LOSS width=%u peak_1=%.4f peak_10=%.4f peak_19=%.5f ratio=%.4f expected=%.4f live=%u,%u,%u fading=%u,%u,%u evictions=%llu\n",
                t.w, double(peaks[0]), double(peaks[1]), double(peaks[2]), double(ratio), double(want), live[0], live[1], live[2],
                fading[0], fading[1], fading[2], evictions);
    char label[64];
    std::snprintf(label, sizeof label, "ribbon_loss_fade_%u", t.w);
    report(label, fading[0] == 1 && fading[1] == 1 && std::fabs(ratio - want) <= .05f);
    std::snprintf(label, sizeof label, "ribbon_loss_evicted_%u", t.w);
    report(label, live[2] == 0 && peaks[2] == 0.f && evictions == 1);
}

// 300 nozzles moving together: 256 ribbons drawn in one call, 44 overflow.
void cap_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EngineRibbonsPass& ribbons) {
    const float Z = 4000.f, value = 6.f / t.ppu(Z);
    std::vector<ee::Record> rs(300);
    rr::EngineRibbonsReport rep{};
    for (unsigned n = 0; n < 12; ++n) {
        for (unsigned i = 0; i < 300; ++i)
            rs[i] = record(t.world_x(100.f + float(i % 20) * 80.f + 4.f * float(n), Z), (float(i / 20) - 7.f) * 30.f / t.ppu(Z), Z,
                           value, 1.f, 5000 + i);
        scene.frame();
        StageContext ctx;
        ctx.ribbons = &ribbons;
        ctx.frame = frame_for(t, rs.data(), 300, n);
        ctx.rframe.seconds = 50. + double(n) * frame_seconds;
        ctx.rframe.cut = n == 0;
        check("scene begin", d->BeginScene());
        run_stage(ctx);
        check("scene end", d->EndScene());
        check("cap stage", ctx.result);
        rep = ctx.rreport;
    }
    std::printf("RIBBON_CAP width=%u live=%u overflow=%u drawn=%u calls=%u draws=1 overflow_total=%llu\n", t.w, rep.update.live,
                rep.update.overflow, rep.stats.ribbons, rep.calls, static_cast<unsigned long long>(ribbons.pool().overflows));
    char label[64];
    std::snprintf(label, sizeof label, "ribbon_cap_256_overflow_44_%u", t.w);
    report(label, rep.update.live == 256 && rep.update.overflow == 44 && rep.stats.ribbons == 256 && rep.drew);
}

// The fog law (white family: k = 1): plume core and ribbon x T(d) at the nozzle's distance.
void fog_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EnginePlumesPass& plumes, rr::EngineRibbonsPass& ribbons) {
    const float Z = 2000.f, value = 30.f / t.ppu(Z), speed = 4.f;
    const unsigned frames = 40;
    const float x_end = float(t.w) * .5f + 200.f;
    rr::FogTransmittanceLaw law;
    const float white[3] = {1.f, 1.f, 1.f};
    rr::fog_transmittance_law(2.5e-6f * 8.f, .12f, white, .6f, &law);
    law.extinction = .6931472f / 2000.f; // a dense synthetic column: T = 0.5 at 2,000 units (the law's shape, not a family)
    float want[3];
    const float dist = std::sqrt(t.world_x(x_end, Z) * t.world_x(x_end, Z) + Z * Z);
    rr::fog_transmittance(law, dist, want);
    float core[2] = {0, 0}, trail[2] = {0, 0};
    for (unsigned fogged = 0; fogged < 2; ++fogged) {
        Run a; // run_sequence's direct path, with the law in the frame
        for (unsigned n = 0; n < frames; ++n) {
            const ee::Record r = record(t.world_x(x_end - float(frames - 1 - n) * speed, Z), 0, Z, value, 1.f);
            scene.frame();
            StageContext ctx;
            ctx.plumes = &plumes;
            ctx.ribbons = &ribbons;
            ctx.frame = frame_for(t, &r, 1, 7); // a fixed flicker clock: the same core in both runs
            if (fogged) ctx.frame.view.fog = law;
            ctx.rframe.seconds = 80. + double(n) * frame_seconds;
            ctx.rframe.cut = n == 0;
            check("scene begin", d->BeginScene());
            run_stage(ctx);
            check("scene end", d->EndScene());
            check("fog stage", ctx.result);
            if (n + 1 == frames) a.current = t.read(d);
        }
        float cx, cy;
        t.window(t.world_x(x_end, Z), 0, Z, 0, 0, cx, cy);
        const int ix = int(std::floor(cx + .5f)), iy = int(std::floor(cy + .5f));
        core[fogged] = row_peak(a.current, t.w, t.h, ix - 15, iy - 1, iy + 1); // inside the 60 px core
        trail[fogged] = row_peak(a.current, t.w, t.h, ix - 100, iy - 1, iy + 1); // past the plume and its halo: the ribbon
    }
    const float core_ratio = core[1] / core[0], trail_ratio = trail[1] / trail[0];
    std::printf("RIBBON_FOG width=%u distance=%.1f expected_T=%.4f core=%.4f,%.4f core_ratio=%.4f trail=%.4f,%.4f trail_ratio=%.4f\n",
                t.w, double(dist), double(want[0]), double(core[0]), double(core[1]), double(core_ratio), double(trail[0]),
                double(trail[1]), double(trail_ratio));
    char label[64];
    std::snprintf(label, sizeof label, "fog_plume_attenuated_%u", t.w);
    report(label, std::fabs(core_ratio - want[0]) <= .02f);
    std::snprintf(label, sizeof label, "fog_ribbon_attenuated_%u", t.w);
    report(label, trail[0] > 0.f && std::fabs(trail_ratio - want[0]) <= .02f);
}

void off_fault_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EngineRibbonsPass& ribbons) {
    // Nothing live, no record: the pool updates, S_FALSE, no device call.
    StageContext ctx;
    ctx.ribbons = &ribbons;
    ctx.frame = frame_for(t, nullptr, 0);
    ctx.rframe.seconds = 500.;
    ctx.rframe.cut = true;
    scene.frame();
    check("scene begin", d->BeginScene());
    run_stage(ctx);
    check("scene end", d->EndScene());
    std::printf("OFF_PATH result=%08lx calls=%u live=%u\n", ctx.result, ctx.rreport.calls, ctx.rreport.update.live);
    report("off_nothing_live_no_call", ctx.result == S_FALSE && ctx.rreport.calls == 0 && !ctx.rreport.drew);
    // A failed draw: its step; the next frame draws.
    const float Z = 2000.f, value = 30.f / t.ppu(Z);
    rr::EngineRibbonsReport failed{}, again{};
    HRESULT failed_hr = S_OK;
    for (unsigned n = 0; n < 12; ++n) {
        const ee::Record r = record(t.world_x(800.f + 4.f * float(n), Z), 0, Z, value, 1.f);
        if (n == 10) ribbons.set_faults(2);
        StageContext c;
        c.ribbons = &ribbons;
        c.frame = frame_for(t, &r, 1, n);
        c.rframe.seconds = 600. + double(n) * frame_seconds;
        c.rframe.cut = n == 0;
        check("scene begin", d->BeginScene());
        run_stage(c);
        check("scene end", d->EndScene());
        if (n == 10) {
            failed = c.rreport;
            failed_hr = c.result;
        }
        if (n == 11) again = c.rreport;
    }
    std::printf("FAULT result=%08lx step=%u next=%08lx drew=%u\n", failed_hr, unsigned(failed.failed), again.operation,
                unsigned(again.drew));
    report("fault_draw_step", failed_hr == E_FAIL && failed.failed == rr::EngineRibbonsStep::Draw);
    report("fault_next_frame_draws", SUCCEEDED(again.operation) && again.drew);
}

void reset_case(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS& pp, rr::EnginePlumesPass& plumes, rr::EngineRibbonsPass& ribbons,
                Targets*& t, Scene*& scene, Quad& quad) {
    // A live ribbon before the Reset.
    {
        const float Z = 2000.f, value = 30.f / t->ppu(Z);
        run_sequence(d, *t, *scene, nullptr, &ribbons, 20, false, Z, value, [&](unsigned n) {
            Step s;
            s.x_px = 900.f + 4.f * float(n);
            return s;
        }, false);
    }
    const unsigned before = ribbons.references(), live_before = ribbons.live();
    const unsigned long long clears = ribbons.pool().reset_clears;
    plumes.before_reset();
    ribbons.before_reset();
    const unsigned released = ribbons.references(), live_after = ribbons.live();
    const HRESULT pending = ribbons.ensure_resources();
    delete scene;
    scene = nullptr;
    const UINT w = t->w, h = t->h;
    delete t;
    t = nullptr;
    const HRESULT reset = d->Reset(&pp);
    plumes.after_reset(reset);
    ribbons.after_reset(reset);
    const HRESULT ensured = ribbons.ensure_resources();
    const unsigned after = ribbons.references();
    t = new Targets(d, w, h);
    scene = new Scene(d, *t, quad);
    const float Z = 2000.f, value = 30.f / t->ppu(Z);
    const Run a = run_sequence(d, *t, *scene, nullptr, &ribbons, 20, false, Z, value, [&](unsigned n) {
        Step s;
        s.x_px = 900.f + 4.f * float(n);
        return s;
    }, false);
    float pk = 0;
    for (std::size_t i = 0; i < a.current.size(); i += 4) pk = std::max(pk, a.current[i]);
    std::printf("RESET before=%u live_before=%u released=%u live_after=%u reset_clears=%llu pending=%08lx reset=%08lx ensured=%08lx after=%u drew=%u peak=%.3f\n",
                before, live_before, released, live_after, static_cast<unsigned long long>(ribbons.pool().reset_clears - clears),
                pending, reset, ensured, after, unsigned(a.last.drew), double(pk));
    report("reset_released_and_cleared", before == 5 && live_before == 1 && released == 0 && live_after == 0 &&
                                             ribbons.pool().reset_clears == clears + 1 && pending == E_FAIL);
    report("reset_recreated", SUCCEEDED(reset) && SUCCEEDED(ensured) && after == 5 && a.last.drew && pk > .5f);
}

// N nozzles spread over the view, moving (deterministic); frame n of the crowd.
std::vector<ee::Record> crowd(Targets& t, unsigned count, unsigned n) {
    std::vector<ee::Record> out;
    std::uint32_t seed = 12345;
    auto rnd = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return float(seed >> 8) / 16777216.f;
    };
    for (unsigned i = 0; i < count; ++i) {
        const float z = 1500.f + 28500.f * rnd() * rnd();
        const float x = (rnd() * 1.6f - .8f) * z / t.m00(), y = (rnd() * 1.6f - .8f) * z / m11;
        const float px = 3.f + 57.f * rnd() * rnd();
        const float value = px / t.ppu(z), speed = (2.f + 10.f * rnd()) / t.ppu(z); // 2..12 px per frame
        const float ax = rnd() - .5f, ay = rnd() - .5f, al = std::sqrt(ax * ax + ay * ay) + 1e-3f;
        out.push_back(record(x + speed * ax / al * float(n), y + speed * ay / al * float(n), z, value, .3f + .7f * rnd(), 100 + i));
    }
    return out;
}
struct Fence {
    Com<IDirect3DQuery9> q;
    explicit Fence(IDirect3DDevice9* d) { check("event query", d->CreateQuery(D3DQUERYTYPE_EVENT, &q.p)); }
};
double tail_ms(IDirect3DDevice9* d, Targets& t, Scene& scene, Fence& fence, StageContext* stage, double* submit) {
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
        run_stage(*stage);
        check("stage", stage->result);
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
// The ribbon draw's EVENT-fenced cost at `count` warmed ribbons (the pool at a steady clock: no append, no eviction).
void timing_case(IDirect3DDevice9* d, Targets& t, Scene& scene, rr::EngineRibbonsPass& ribbons, unsigned count) {
    Fence fence(d);
    std::vector<ee::Record> records;
    double seconds = 900.;
    rr::EngineRibbonsReport warm{};
    for (unsigned n = 0; n < 40; ++n, seconds += frame_seconds) {
        records = crowd(t, count, n);
        StageContext c;
        c.ribbons = &ribbons;
        c.frame = frame_for(t, records.data(), count, n);
        c.rframe.seconds = seconds;
        c.rframe.cut = n == 0;
        check("scene begin", d->BeginScene());
        run_stage(c);
        check("scene end", d->EndScene());
        warm = c.rreport;
    }
    StageContext stage;
    stage.ribbons = &ribbons;
    stage.frame = frame_for(t, records.data(), count, 40);
    stage.rframe.seconds = seconds - frame_seconds;
    std::vector<double> on, off, submit;
    for (unsigned i = 0; i < 126; ++i) {
        const bool run = i % 2 == 0;
        double s = 0;
        const double ms = tail_ms(d, t, scene, fence, run ? &stage : nullptr, &s);
        if (i < 6) continue;
        (run ? on : off).push_back(ms);
        if (run) submit.push_back(s);
    }
    const double chain = median(on) - median(off), gpu = std::max(0., chain - median(submit));
    std::printf("TIMING width=%u height=%u ribbons=%u drawn=%u samples=%u fenced_on_ms=%.4f fenced_off_ms=%.4f submit_ms=%.4f chain_ms=%.4f gpu_ms=%.4f samples_n=%zu calls=%u method=tail_event_fenced\n",
                t.w, t.h, count, warm.stats.ribbons, warm.stats.samples, median(on), median(off), median(submit), chain, gpu,
                on.size(), stage.rreport.calls);
}
// The CPU side alone (into system memory): the pool update and the strip build per frame, the crowd moving.
void cpu_timing(Targets& t) {
    for (const unsigned count : {30u, 100u}) {
        static er::Pool pool;
        pool.clear();
        std::vector<er::Vertex> out(std::size_t(count) * er::vertices_per_ribbon);
        const rr::EnginePlumesFrame f = frame_for(t, nullptr, 0);
        LARGE_INTEGER freq{};
        QueryPerformanceFrequency(&freq);
        std::vector<double> us;
        unsigned drawn = 0;
        double seconds = 0.;
        std::vector<std::vector<ee::Record>> frames;
        for (unsigned n = 0; n < 64; ++n) frames.push_back(crowd(t, count, n));
        for (unsigned rep = 0; rep < 41; ++rep) {
            LARGE_INTEGER a{}, b{};
            QueryPerformanceCounter(&a);
            for (unsigned k = 0; k < 50; ++k, seconds += frame_seconds) {
                const auto& rs = frames[(rep * 50 + k) % 64];
                er::update(pool, rs.data(), count, seconds, (rep * 50 + k) % 64 == 0, 0, nullptr, 1.f, nullptr);
                drawn = er::build(pool, f.view, ep::Preset::standard, seconds, out.data(), count, nullptr);
            }
            QueryPerformanceCounter(&b);
            us.push_back(double(b.QuadPart - a.QuadPart) * 1e6 / double(freq.QuadPart) / 50.);
        }
        std::printf("CPU ribbons=%u drawn=%u median_us=%.2f min_us=%.2f method=qpc_50x41_update_build\n", count, drawn, median(us),
                    *std::min_element(us.begin(), us.end()));
        if (count == 100) report("cpu_100_within_0.1ms", median(us) <= 100.);
    }
}
} // namespace
int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        if (argc < 2) {
            std::puts("usage: engine_ribbons_fixture <d3dx9_37.dll> [--only case,...]");
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
        cls.lpszClassName = "X3EngineRibbonsFixture";
        RegisterClassA(&cls);
        HWND window = CreateWindowA(cls.lpszClassName, "X3 engine ribbons fixture", WS_OVERLAPPEDWINDOW, 90, 90, 128, 128,
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
        auto* plumes = new rr::EnginePlumesPass;
        auto* ribbons = new rr::EngineRibbonsPass;
        check("plumes attach", plumes->attach(d, *reinterpret_cast<void* const* const*>(d), caps, mode.Format));
        attach_case(d, caps, mode.Format, *ribbons);
        {
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
            if (wanted("resolve"))
                for (const bool flicker : {false, true})
                    for (const float speed : {4.f, 8.f}) resolve_case(d, *t, *scene, *plumes, *ribbons, speed, flicker);
            if (wanted("floor")) floor_case(d, *t, *scene, *ribbons);
            if (wanted("timing"))
                for (const unsigned n : {30u, 100u}) timing_case(d, *t, *scene, *ribbons, n);
            if (size.first == 1920) {
                if (wanted("stationary")) stationary_case(d, *t, *scene, *ribbons);
                if (wanted("seta")) seta_case(d, *t, *scene, *ribbons);
                if (wanted("cut")) cut_case(d, *t, *scene, *ribbons);
                if (wanted("loss")) loss_case(d, *t, *scene, *ribbons);
                if (wanted("cap")) cap_case(d, *t, *scene, *ribbons);
                if (wanted("fog")) fog_case(d, *t, *scene, *plumes, *ribbons);
                if (wanted("cpu")) cpu_timing(*t);
                if (wanted("fault")) off_fault_case(d, *t, *scene, *ribbons);
                if (wanted("reset")) reset_case(d, pp, *plumes, *ribbons, t, scene, quad);
            }
            delete scene;
            delete t;
        }
        ribbons->detach();
        report("detach_released", ribbons->references() == 0 && ribbons->live() == 0);
        std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
        // The device and runtime teardown is not under test (the plume fixture's rule): leave without it.
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
