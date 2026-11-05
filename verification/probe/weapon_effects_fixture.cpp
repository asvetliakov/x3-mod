// D3D fixture of the weapon-effects shield-hit stage (src/renderer/weapon_effects_pass.cpp, the production
// WeaponEffectsPass and its patch / decal programs; docs/architecture/weapon-effects.md 5.1 acceptance (b) and 5.1.1).
// It creates a plain D3D9 HAL device, attaches the pass, and draws hex patches and flat decals onto an A16B16G16R16F
// scene target with a controlled four-channel depth lane (.b = view depth, a sentinel .r < 0 = sky), built by a small FP
// lane shader compiled through the game's d3dx9_37.dll (argv[1], run_weapon_effects.py's contract). Patches are built
// with the production ws::fill_patch on an owner whose anchor region faces the camera (the hit at the anchor sits at
// the screen centre). It reads the FP16 target back and reports the structural relations of 5.1.1: per tier the lit
// area is bounded by the patch radius and zero outside, the edge count across the patch orders fighter < corvette <
// capital, the core is present at 0.05 s and gone by 0.2 s, the ring front sits at ripple_speed R age at two ages, the
// patch fades monotonically to zero at the record's lifetime, the colour bands at shield 0.9 / 0.5 / 0.2, a repeated hit
// re-lights (max, not sum) and four hits accumulate, the CPU cap culls nothing lit, the decal fallback (through the
// production builder: an owner without an ellipsoid) draws on a hull and 0 over sky, the game-time law at SETA 6
// measured, the core through the production TemporalPass at 0 / 4 / 8 px per frame, a retired owner draws nothing,
// Reset recovery, the fault cases, and the stage cost at 1080p and 5120x1440 (two patches on a fighter, two on a
// capital at close range, and the worst case one owner can submit). Every draw and readback runs inside
// BeginScene/EndScene (native D3D9 refuses a draw outside a scene). Never launches the game.
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/renderer/weapon_effects_pass.h"
#include "../../src/proxy/weapon_effects_stage.h"
#include "../../src/renderer/temporal_pass.h"
#include "../../src/renderer/temporal_resolve_program.h"
#include "../../src/renderer/hdr_writeback_program.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <utility>
#include <vector>

namespace rr = x3m::renderer;
namespace ws = x3m::weapon_effects::stage;
namespace wc = x3m::weapon_effects::core;

namespace {
unsigned checks = 0, failures = 0;
void report(const char* name, bool ok, const char* detail = "") {
    ++checks;
    if (!ok) ++failures;
    std::printf("CHECK %s %s%s%s\n", name, ok ? "PASS" : "FAIL", *detail ? " " : "", detail);
}
template <class T> struct Com {
    T* p = nullptr;
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    ~Com() { release(); }
    void release() { if (p) { p->Release(); p = nullptr; } }
};
float half_to_float(std::uint16_t h) {
    const std::uint32_t s = (h & 0x8000u) << 16;
    std::uint32_t e = (h >> 10) & 0x1f, m = h & 0x3ff, out;
    if (e == 0) {
        if (m == 0) out = s;
        else { while (!(m & 0x400)) { m <<= 1; --e; } ++e; m &= 0x3ff; out = s | (e + 112) << 23 | m << 13; }
    } else if (e == 0x1f) out = s | 0x7f800000u | m << 13;
    else out = s | (e + 112) << 23 | m << 13;
    float f;
    std::memcpy(&f, &out, 4);
    return f;
}
IDirect3DDevice9* dev = nullptr;
D3DCAPS9 caps{};
D3DFORMAT adapter_format = D3DFMT_X8R8G8B8;
HMODULE d3dx = nullptr;
using CompileFn = HRESULT(WINAPI*)(LPCSTR, UINT, const D3DXMACRO*, LPD3DXINCLUDE, LPCSTR, LPCSTR, DWORD, LPD3DXBUFFER*,
                                   LPD3DXBUFFER*, LPD3DXCONSTANTTABLE*);
CompileFn compile_shader = nullptr;
IDirect3DPixelShader9* lane_ps = nullptr;
IDirect3DVertexShader9* quad_vs = nullptr;
IDirect3DVertexDeclaration9* quad_decl = nullptr;
IDirect3DPixelShader9* motion_ps = nullptr;

bool compile(const char* src, const char* profile, const DWORD** out, LPD3DXBUFFER* hold) {
    LPD3DXBUFFER code = nullptr, err = nullptr;
    const HRESULT hr = compile_shader(src, UINT(std::strlen(src)), nullptr, nullptr, "main", profile, 0, &code, &err, nullptr);
    if (err) { std::printf("COMPILE_ERR %.200s\n", static_cast<const char*>(err->GetBufferPointer())); err->Release(); }
    if (FAILED(hr) || !code) return false;
    *out = static_cast<const DWORD*>(code->GetBufferPointer());
    *hold = code;
    return true;
}
bool compile_helpers() {
    if (lane_ps) return true;
    if (!compile_shader) return false;
    // A pass-through vertex shader for a full-screen quad (POSITION float4 already in clip space).
    const char* vs = "float4 main(float4 p:POSITION):POSITION{ return p; }";
    // The lane: hull (.b = depth, .r = 0.5 occluder) where VPOS.x < c0.z * c0.x, else sky (.r = -1 sentinel). c0 =
    // (width, depth, occluder_frac, unused).
    const char* ps = "float4 c0:register(c0); float4 main(float2 vp:VPOS):COLOR0{"
                     " return (c0.z >= 0 && vp.x < c0.z * c0.x) ? float4(0.5,0,c0.y,0) : float4(-1,0,0,0); }";
    const DWORD* vw = nullptr;
    const DWORD* pw = nullptr;
    static Com<ID3DXBuffer> vh, ph;
    if (!compile(vs, "vs_3_0", &vw, &vh.p) || !compile(ps, "ps_3_0", &pw, &ph.p)) return false;
    if (FAILED(dev->CreateVertexShader(vw, &quad_vs)) || FAILED(dev->CreatePixelShader(pw, &lane_ps))) return false;
    // The motion target of an all-unrouted frame (the route's fill sentinel, alpha -1): the resolve's camera-only path.
    const DWORD* mw = nullptr;
    static Com<ID3DXBuffer> mh;
    if (!compile("float4 main(float2 p:VPOS):COLOR0{ return float4(0,0,0,-1); }", "ps_3_0", &mw, &mh.p) ||
        FAILED(dev->CreatePixelShader(mw, &motion_ps)))
        return false;
    const D3DVERTEXELEMENT9 elems[] = {{0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0}, D3DDECL_END()};
    return SUCCEEDED(dev->CreateVertexDeclaration(elems, &quad_decl));
}
struct Target {
    UINT w = 0, h = 0;
    Com<IDirect3DTexture9> scene_tex, lane_tex;
    Com<IDirect3DSurface9> scene_s, lane_s, sysmem;
    void release() { scene_s.release(); lane_s.release(); sysmem.release(); scene_tex.release(); lane_tex.release(); }
};
bool make_target(Target& t, UINT w, UINT h) {
    t.w = w;
    t.h = h;
    return SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &t.scene_tex.p, nullptr)) &&
           SUCCEEDED(t.scene_tex.p->GetSurfaceLevel(0, &t.scene_s.p)) &&
           SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &t.lane_tex.p, nullptr)) &&
           SUCCEEDED(t.lane_tex.p->GetSurfaceLevel(0, &t.lane_s.p)) &&
           SUCCEEDED(dev->CreateOffscreenPlainSurface(w, h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &t.sysmem.p, nullptr));
}
// Fill the lane: occluder_frac < 0 = sky everywhere; else a hull at view depth `depth` where x < occluder_frac*w.
void draw_lane(Target& t, float depth, float occluder_frac) {
    const float c0[4] = {float(t.w), depth, occluder_frac, 0.f};
    const float quad[4][4] = {{-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
    dev->BeginScene();
    dev->SetRenderTarget(0, t.lane_s.p);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetVertexDeclaration(quad_decl);
    dev->SetVertexShader(quad_vs);
    dev->SetPixelShader(lane_ps);
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
    dev->EndScene();
}
// The whole target read back as RGB floats (row-major, 3 per pixel); empty on failure.
std::vector<float> read_rgb(Target& t, IDirect3DSurface9* source) {
    std::vector<float> out;
    if (FAILED(dev->GetRenderTargetData(source, t.sysmem.p))) return out;
    D3DLOCKED_RECT lr{};
    if (FAILED(t.sysmem.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return out;
    out.resize(std::size_t(t.w) * t.h * 3);
    for (UINT y = 0; y < t.h; ++y) {
        const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
        for (UINT x = 0; x < t.w; ++x)
            for (unsigned k = 0; k < 3; ++k) out[(std::size_t(y) * t.w + x) * 3 + k] = half_to_float(row[x * 4 + k]);
    }
    t.sysmem.p->UnlockRect();
    return out;
}
std::vector<float> read_luma(Target& t, IDirect3DSurface9* source) {
    const std::vector<float> rgb = read_rgb(t, source);
    std::vector<float> out(rgb.size() / 3);
    for (std::size_t i = 0; i < out.size(); ++i) out[i] = std::max(rgb[i * 3], std::max(rgb[i * 3 + 1], rgb[i * 3 + 2]));
    return out;
}
double ticks_us(LARGE_INTEGER a, LARGE_INTEGER b, LARGE_INTEGER freq) {
    return double(b.QuadPart - a.QuadPart) * 1e6 / double(freq.QuadPart);
}
Target g;
rr::WeaponEffectsFrame base_frame(Target& t) {
    rr::WeaponEffectsFrame f{};
    f.width = t.w;
    f.height = t.h;
    f.lane = t.lane_tex.p;
    f.four_channel = true;
    f.m00 = 1.6f;
    f.m11 = 1.6f * float(t.w) / float(t.h);
    f.near_z = 6.f;
    f.look = rr::default_weapon_look();
    return f;
}
std::pair<HRESULT, rr::WeaponEffectsReport> draw(rr::WeaponEffectsPass& pass, const rr::WeaponEffectsFrame& f) {
    dev->BeginScene();
    dev->SetRenderTarget(0, g.scene_s.p);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
    rr::WeaponEffectsReport r{};
    const HRESULT hr = pass.run(f, &r);
    dev->EndScene();
    return {hr, r};
}
void fence(IDirect3DQuery9* q) {
    if (!q) return;
    q->Issue(D3DISSUE_END);
    const DWORD start = GetTickCount();
    while (q->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE && GetTickCount() - start < 5000) {
    }
}
// ------------------------------------------------------------------ patch geometry
// Owners by tier (view units, 5 per metre): a mean axis S, the centre z that frames the patch at 1080p, and the
// patch's planar radius / cell (the production Tuning through fill_patch).
struct TierScene {
    unsigned tier;
    float surface, centre_z;
};
constexpr TierScene tier_scene[3] = {{wc::patch_fighter, 100.f, 244.f},     // 20 m owner, hit 28.8 m away
                                     {wc::patch_corvette, 400.f, 800.f},    // 80 m owner, hit 80 m away
                                     {wc::patch_capital, 2000.f, 3280.f}};  // 400 m owner, hit 256 m away
constexpr unsigned anchor0 = 0;
// The unit-sphere direction of the plane point q (view units) of the anchor0 lattice on a surface S (the inverse
// stereographic projection), so a hit can be placed at a chosen lattice position.
void plane_dir(float S, float qx, float qy, float out[3]) {
    float a[3], u[3], v[3];
    ws::anchor_frame(anchor0, a, u, v);
    const float X = qx / (2.f * S), Y = qy / (2.f * S), r2 = X * X + Y * Y;
    for (unsigned k = 0; k < 3; ++k) out[k] = (2.f * X * u[k] + 2.f * Y * v[k] + (1.f - r2) * a[k]) / (1.f + r2);
}
// The owner's view axes: anchor a -> -z (toward the eye), u -> +x, v -> -y (det +1), radius S.
void facing_axes(float S, float axes[9]) {
    float a[3], u[3], v[3];
    ws::anchor_frame(anchor0, a, u, v);
    for (unsigned k = 0; k < 3; ++k) {
        axes[3 * k] = S * u[k];
        axes[3 * k + 1] = -S * v[k];
        axes[3 * k + 2] = -S * a[k];
    }
}
struct Hit {
    float qx, qy, age;
};
rr::WeaponPatch make_patch(const TierScene& s, std::initializer_list<Hit> hits, float shield = 1.f, float cx = 0.f,
                           const ws::Tuning& t = ws::Tuning{}) {
    rr::WeaponPatch p{};
    float axes[9], centre[3] = {cx, 0.f, s.centre_z};
    facing_axes(s.surface, axes);
    float dirs[4][3], ages[4], colour[3];
    unsigned n = 0;
    for (const Hit& h : hits) {
        if (n >= 4) break;
        plane_dir(s.surface, h.qx, h.qy, dirs[n]);
        ages[n++] = h.age;
    }
    wc::shield_colour(shield, t.bands, colour);
    ws::fill_patch(p, centre, axes, t.soft * s.surface, s.tier, anchor0, dirs, ages, n, colour, t);
    return p;
}
// Screen x (px) of the plane point (q, 0) on the scanline through the anchor (the surface point, perspective).
double screen_x(const Target& t, const TierScene& s, double q) {
    const double S = s.surface, X = q / (2.0 * S), r2 = X * X;
    const double x = S * 2.0 * X / (1.0 + r2), z = s.centre_z - S * (1.0 - r2) / (1.0 + r2);
    return double(t.w) * 0.5 * (1.0 + 1.6 * x / z);
}
float planar_radius(const TierScene& s) {
    return std::min(ws::metres_to_view(wc::radius_m[s.tier]), 0.9f * s.surface);
}
float planar_cell(const TierScene& s) {
    return ws::metres_to_view(wc::cell_m[s.tier]);
}
// Draw one frame of patches over sky and read the luma back.
std::vector<float> render(rr::WeaponEffectsPass& pass, const rr::WeaponPatch* p, unsigned n,
                          const rr::WeaponLook* look = nullptr, std::vector<float>* rgb = nullptr) {
    rr::WeaponEffectsFrame f = base_frame(g);
    f.patches = p;
    f.patch_count = n;
    if (look) f.look = *look;
    draw(pass, f);
    if (rgb) *rgb = read_rgb(g, g.scene_s.p);
    return read_luma(g, g.scene_s.p);
}
double total(const std::vector<float>& l) {
    double s = 0;
    for (float v : l) s += double(v);
    return s;
}
unsigned lit_count(const std::vector<float>& l, float threshold = 1e-3f) {
    unsigned n = 0;
    for (float v : l) n += v > threshold;
    return n;
}
float centre_luma(const std::vector<float>& l) {
    return l.empty() ? -1.f : l[std::size_t(g.h / 2) * g.w + g.w / 2];
}
// The production stage builder's input: one owner in the fixture's own memory (object block and node block), read
// through a reader bounded to those two blocks (engine_memory::read's contract: false outside readable memory).
alignas(16) unsigned char fake_obj[0x100], fake_node[0x200];
bool fake_read(std::uintptr_t a, void* out, std::size_t n) {
    for (const unsigned char* base : {static_cast<const unsigned char*>(fake_obj), static_cast<const unsigned char*>(fake_node)}) {
        const std::uintptr_t b = reinterpret_cast<std::uintptr_t>(base);
        const std::size_t size = base == fake_obj ? sizeof fake_obj : sizeof fake_node;
        if (a >= b && n <= size && a - b <= size - n) {
            std::memcpy(out, reinterpret_cast<const void*>(a), n);
            return true;
        }
    }
    return false;
}
void put32(unsigned char* b, unsigned off, std::uint32_t v) {
    std::memcpy(b + off, &v, 4);
}
std::uint32_t addr(const void* p) {
    return std::uint32_t(reinterpret_cast<std::uintptr_t>(p));
}
// A live owner at view (0, 0, z) with radius r (view units; identity camera, identity rows), and the ring holding one
// stage-owned shield hit on its front (t0 game ms, the header's patch lifetime, a fighter, full shields).
wc::Ring stage_ring;
void stage_owner(float z, float r, std::uint32_t t0) {
    std::memset(fake_obj, 0, sizeof fake_obj);
    std::memset(fake_node, 0, sizeof fake_node);
    const auto engine = [](float v) { return std::uint32_t(std::int32_t(v / wc::world_scale)); };
    put32(fake_obj, wc::layout::obj_node, addr(fake_node));
    put32(fake_obj, wc::layout::obj_radius, engine(r));
    put32(fake_node, wc::layout::node_handle, 0xd00du);
    put32(fake_node, wc::layout::node_pos + 8, engine(z));
    for (unsigned k = 0; k < 3; ++k) put32(fake_node, wc::layout::node_rot + 16 * k + 4 * k, 0x10000u);
    wc::ring_clear(stage_ring);
    wc::Event* e = wc::ring_acquire(stage_ring);
    e->role = wc::role_shield_hit;
    e->style = wc::shape_slug;
    e->flags = std::uint8_t(wc::flag_stage | wc::flag_forwarded | wc::flag_shield_read);
    e->shield_q = 0xffff;
    e->patch_tier = wc::patch_fighter;
    e->t0_game_ms = t0;
    e->lifetime_ms = std::uint16_t(wc::patch_lifetime_ms);
    e->damage = 1200;
    e->tint_rgb = 0xffffffu | wc::rgb_present;
    e->owner = addr(fake_obj);
    e->node = addr(fake_node);
    e->handle = 0xd00du;
    e->pos[2] = -std::int32_t(engine(r));
    e->radius = std::int32_t(engine(r));
}
ws::Camera identity_camera() {
    ws::Camera c;
    c.r[0] = c.r[4] = c.r[8] = 1.f;
    return c;
}
unsigned ring_live(const wc::Ring& r) {
    unsigned n = 0;
    for (unsigned i = 0; i < wc::ring_capacity; ++i) n += r.slots[i].sequence != 0;
    return n;
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
struct StageContext {
    rr::WeaponEffectsPass* pass;
    rr::WeaponEffectsFrame frame;
    HRESULT result;
};
HRESULT stage_callback(void* context, IDirect3DDevice9*) noexcept {
    auto* c = static_cast<StageContext*>(context);
    rr::WeaponEffectsReport r{};
    c->result = c->pass->run(c->frame, &r);
    return c->result;
}
// The patch's core through the production resolve (the engine plumes fixture's flown configuration: PerPixel with the
// all-unrouted motion target, sentinel camera with strict sky, exit 0.25, far camera + camera gate): 16 frames of
// Halton jitter with a fighter patch at age 0.05 (the core lit) moving +x at `speed_px` per frame, once with the stage
// and once without. The core is the current frame's patch pixels at half its peak or more; the ratio is their resolved
// share (on - off) over their current share. -1 when the resolve could not be driven.
double resolve_core(rr::WeaponEffectsPass& pass, Com<IDirect3DTexture9>& motion, Com<IDirect3DSurface9>& motion_s,
                    float speed_px, float* peak_out) {
    constexpr unsigned frames = 16;
    const TierScene& s = tier_scene[0];
    const float z_hit = s.centre_z - s.surface, m00 = base_frame(g).m00;
    const float units_per_px = 2.f * z_hit / (m00 * float(g.w));
    std::vector<float> cur[2], res[2];
    for (unsigned on = 0; on < 2; ++on) {
        rr::TemporalPass taa;
        HRESULT hr = taa.initialize(dev, nullptr, reinterpret_cast<const DWORD*>(rr::temporal_resolve_program()), nullptr,
                                    nullptr, reinterpret_cast<const DWORD*>(rr::hdr_writeback_program()));
        if (SUCCEEDED(hr)) hr = taa.configure_far();
        if (FAILED(hr)) {
            std::printf("RESOLVE_FAIL step=initialize result=%08lx\n", hr);
            return -1;
        }
        rr::Output o{};
        float pjx = 0, pjy = 0;
        for (unsigned n = 0; n < frames; ++n) {
            const float jx = float(halton(n % 8 + 1, 2) - .5), jy = float(halton(n % 8 + 1, 3) - .5);
            draw_lane(g, 0.f, -1.f); // sky everywhere
            const float quad[4][4] = {{-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
            dev->BeginScene();
            dev->SetRenderTarget(0, motion_s.p);
            dev->SetVertexDeclaration(quad_decl);
            dev->SetVertexShader(quad_vs);
            dev->SetPixelShader(motion_ps);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
            dev->SetRenderTarget(0, g.scene_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
            dev->EndScene();
            const rr::WeaponPatch patch =
                make_patch(s, {{0.f, 0.f, 0.05f}}, 1.f, (float(n) - float(frames - 1)) * speed_px * units_per_px);
            StageContext ctx{&pass, base_frame(g), S_FALSE};
            ctx.frame.m20 = 2.f * jx / float(g.w);
            ctx.frame.m21 = -2.f * jy / float(g.h);
            ctx.frame.patches = &patch;
            ctx.frame.patch_count = 1;
            rr::FrameInputs in;
            in.color = g.scene_tex.p;
            in.current_depth = g.lane_tex.p;
            in.width = g.w;
            in.height = g.h;
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
            in.motion = motion.p;
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
            if (on) {
                in.stage_callback = &stage_callback;
                in.stage_context = &ctx;
            }
            dev->BeginScene();
            hr = taa.run(in, &o);
            dev->EndScene();
            if (FAILED(hr) || (on && (!taa.diagnostics().stage_ran || FAILED(ctx.result)))) {
                std::printf("RESOLVE_FAIL step=run frame=%u result=%08lx stage=%08lx ran=%u\n", n, hr, ctx.result,
                            unsigned(taa.diagnostics().stage_ran));
                return -1;
            }
            pjx = jx;
            pjy = jy;
            if (n + 1 == frames) {
                cur[on] = read_luma(g, g.scene_s.p);
                Com<IDirect3DSurface9> resolved;
                if (!o.color || FAILED(o.color->GetSurfaceLevel(0, &resolved.p))) return -1;
                res[on] = read_luma(g, resolved.p);
            }
        }
        taa.shutdown();
    }
    if (cur[0].empty() || cur[1].empty() || res[0].empty() || res[1].empty()) return -1;
    float peak = 0.f;
    for (std::size_t i = 0; i < cur[1].size(); ++i) peak = std::max(peak, cur[1][i] - cur[0][i]);
    double num = 0, den = 0;
    for (std::size_t i = 0; i < cur[1].size(); ++i) {
        const double c = double(cur[1][i]) - double(cur[0][i]);
        if (c >= .5 * double(peak)) {
            num += double(res[1][i]) - double(res[0][i]);
            den += c;
        }
    }
    *peak_out = peak;
    return den > 0 ? num / den : 0;
}
// The scanline through the screen centre, x from the centre to the right edge.
std::vector<float> right_scanline(const std::vector<float>& l) {
    std::vector<float> out;
    if (l.empty()) return out;
    const std::size_t row = std::size_t(g.h / 2) * g.w;
    for (UINT x = g.w / 2; x < g.w; ++x) out.push_back(l[row + x]);
    return out;
}
// Edge bands crossed by the full scanline through the centre: rising crossings of 1.25 x a one-cell moving average.
unsigned edge_crossings(const std::vector<float>& l, unsigned cell_px) {
    const std::size_t row = std::size_t(g.h / 2) * g.w;
    std::vector<double> line(g.w);
    for (UINT x = 0; x < g.w; ++x) line[x] = double(l[row + x]);
    const int half = int(cell_px / 2 > 1 ? cell_px / 2 : 1);
    unsigned n = 0;
    bool above = false;
    for (int x = 0; x < int(g.w); ++x) {
        double sum = 0;
        int cnt = 0;
        for (int k = x - half; k <= x + half; ++k)
            if (k >= 0 && k < int(g.w)) {
                sum += line[std::size_t(k)];
                ++cnt;
            }
        const double base = cnt ? sum / cnt : 0;
        const bool now = line[std::size_t(x)] > 1e-3 && line[std::size_t(x)] > 1.25 * base;
        if (now && !above) ++n;
        above = now;
    }
    return n;
}
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    d3dx = LoadLibraryA(argc > 1 ? argv[1] : "d3dx9_37.dll");
    compile_shader = d3dx ? reinterpret_cast<CompileFn>(GetProcAddress(d3dx, "D3DXCompileShader")) : nullptr;
    if (d3dx) { char path[MAX_PATH]{}; if (GetModuleFileNameA(d3dx, path, MAX_PATH)) std::printf("D3DX_MODULE %s\n", path); }

    HMODULE runtime = LoadLibraryA("d3d9.dll");
    auto* create = runtime ? reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(runtime, "Direct3DCreate9")) : nullptr;
    Com<IDirect3D9> api;
    if (create) api.p = create(D3D_SDK_VERSION);
    if (!api.p) { std::printf("CHECK d3d_created FAIL\nRESULT FAIL checks=1 failures=1\n"); return 1; }
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "x3mweapon";
    RegisterClassA(&wc);
    HWND window = CreateWindowA("x3mweapon", "x3m", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
    D3DADAPTER_IDENTIFIER9 id{};
    if (SUCCEEDED(api.p->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id))) {
        std::printf("ADAPTER vendor=0x%04lx device=0x%04lx driver_version=%u.%u.%u.%u\n", id.VendorId, id.DeviceId,
                    unsigned(HIWORD(id.DriverVersion.HighPart)), unsigned(LOWORD(id.DriverVersion.HighPart)),
                    unsigned(HIWORD(id.DriverVersion.LowPart)), unsigned(LOWORD(id.DriverVersion.LowPart)));
        id.Description[sizeof id.Description - 1] = 0;
        id.Driver[sizeof id.Driver - 1] = 0;
        std::printf("ADAPTER_DESC %s\nADAPTER_DRIVER %s\n", id.Description, id.Driver);
    }
    char rpath[MAX_PATH]{};
    if (GetModuleFileNameA(runtime, rpath, MAX_PATH)) std::printf("D3D9_MODULE %s\n", rpath);
    D3DPRESENT_PARAMETERS pp{};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.hDeviceWindow = window;
    Com<IDirect3DDevice9> device;
    if (FAILED(api.p->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device.p))) {
        std::printf("CHECK device FAIL\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    dev = device.p;
    dev->GetDeviceCaps(&caps);
    D3DDISPLAYMODE mode{};
    dev->GetDisplayMode(0, &mode);
    adapter_format = mode.Format;
    report("d3dx_lane_shader", compile_helpers(), compile_shader ? "" : "D3DXCompileShader unavailable");

    rr::WeaponEffectsPass pass;
    const HRESULT attached = pass.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format);
    report("attach", SUCCEEDED(attached) && pass.caps().enabled, pass.caps().reason);
    std::printf("ATTACH result=%08lx shell_vs=%u shell_ps=%u decal_vs=%u decal_ps=%u fp16=%08lx max_ps=%lu cap_vertices=%u cap_triangles=%u\n",
                attached, pass.caps().shell_vs_slots, pass.caps().shell_ps_slots, pass.caps().decal_vs_slots,
                pass.caps().decal_ps_slots, pass.caps().fp16_blending,
                static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots), rr::WeaponEffectsPass::cap_vertices,
                rr::WeaponEffectsPass::cap_triangles);
    if (!pass.caps().enabled || !lane_ps) {
        std::printf("RESULT FAIL checks=%u failures=%u\n", checks, failures + 1);
        return 1;
    }
    report("pixel_program_low_hundreds", pass.caps().shell_ps_slots < 400 && pass.caps().decal_ps_slots < 400, "");
    report("target_1080", make_target(g, 1920, 1080));
    draw_lane(g, 0.f, -1.f);
    rr::WeaponLook cells_only = rr::default_weapon_look(), ring_only = cells_only, no_core = cells_only;
    cells_only.ring_gain = cells_only.core_gain = 0.f;
    ring_only.cell_gain = ring_only.core_gain = 0.f;
    no_core.core_gain = 0.f;

    // Per tier at age 0.5 (every cell lit, core and ring gone): the lit area within the radius, zero outside, and the
    // edge bands across the patch (2 R / cell expected).
    {
        unsigned edges[3]{};
        bool bounded = true, ordered = true;
        for (unsigned t = 0; t < 3; ++t) {
            const TierScene& s = tier_scene[t];
            const rr::WeaponPatch p = make_patch(s, {{0.f, 0.f, 0.5f}});
            const std::vector<float> l = render(pass, &p, 1);
            const double cx = g.w * 0.5, cy = g.h * 0.5;
            const double R = planar_radius(s), cell = planar_cell(s);
            const double r_px = screen_x(g, s, R) - cx, cell_px = screen_x(g, s, cell) - cx;
            double max_px = 0, outside = 0;
            for (UINT y = 0; y < g.h; ++y)
                for (UINT x = 0; x < g.w; ++x) {
                    const float v = l[std::size_t(y) * g.w + x];
                    const double d = std::sqrt((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy));
                    if (v > 1e-3f) max_px = std::max(max_px, d);
                    if (d > r_px + 1.5) outside += double(v);
                }
            edges[t] = edge_crossings(l, unsigned(cell_px));
            const double expected = 2.0 * R / cell;
            std::printf("PATCH tier=%s radius_view=%.2f cell_view=%.2f radius_px=%.1f cell_px=%.1f lit_px=%.1f outside_sum=%.6f edges=%u expected_edges=%.1f lit_pixels=%u peak=%.4f\n",
                        wc::patch_tier_name(s.tier), R, cell, r_px, cell_px, max_px, outside, edges[t], expected,
                        lit_count(l), double(*std::max_element(l.begin(), l.end())));
            bounded = bounded && max_px <= r_px + 1.5 && max_px >= 0.8 * r_px && outside == 0.0;
            ordered = ordered && std::fabs(double(edges[t]) - expected) <= 3.0;
        }
        report("lit_area_bounded_by_radius_per_tier", bounded, "");
        report("edge_count_matches_radius_over_cell", ordered, "");
        report("cell_count_order_by_tier", edges[0] < edges[1] && edges[1] < edges[2] && wc::cell_m[0] < wc::cell_m[1] &&
                                               wc::cell_m[1] < wc::cell_m[2] && wc::radius_m[0] < wc::radius_m[1] &&
                                               wc::radius_m[1] < wc::radius_m[2], "");
    }
    const TierScene& fighter = tier_scene[0];
    // The core: the hit point's pixel with and without the core term, at 0.05 s and at 0.2 s.
    {
        const rr::WeaponPatch young = make_patch(fighter, {{0.f, 0.f, 0.05f}}), later = make_patch(fighter, {{0.f, 0.f, 0.2f}});
        const float c05 = centre_luma(render(pass, &young, 1)), n05 = centre_luma(render(pass, &young, 1, &no_core));
        const float c20 = centre_luma(render(pass, &later, 1)), n20 = centre_luma(render(pass, &later, 1, &no_core));
        std::printf("CORE at05=%.4f at05_no_core=%.4f at20=%.4f at20_no_core=%.4f\n", c05, n05, c20, n20);
        report("core_present_at_50ms", c05 - n05 > 1.0f, "");
        report("core_gone_by_200ms", std::fabs(c20 - n20) < 1e-3f && c20 > 0.f, "");
    }
    // The ring front: ring-only and cells-only scanlines at 0.1 s and 0.2 s against r = ripple_speed R age.
    {
        const double R = planar_radius(fighter), cell = planar_cell(fighter), cx = g.w * 0.5;
        bool ring_ok = true, cells_ok = true;
        double peaks[2]{}, fronts[2]{};
        const float ages[2] = {0.1f, 0.2f};
        for (unsigned k = 0; k < 2; ++k) {
            const rr::WeaponPatch p = make_patch(fighter, {{0.f, 0.f, ages[k]}});
            const std::vector<float> ring = right_scanline(render(pass, &p, 1, &ring_only));
            const std::vector<float> cl = right_scanline(render(pass, &p, 1, &cells_only));
            std::size_t peak = 0;
            for (std::size_t i = 0; i < ring.size(); ++i)
                if (ring[i] > ring[peak]) peak = i;
            std::size_t front = 0;
            for (std::size_t i = 0; i < cl.size(); ++i)
                if (cl[i] > 1e-3f) front = i;
            const double r = double(wc::ripple_speed) * R * double(ages[k]);
            const double want = screen_x(g, fighter, r) - cx;
            const double lo = screen_x(g, fighter, r - 0.6 * cell) - cx, hi = screen_x(g, fighter, r + 0.6 * cell) - cx;
            peaks[k] = double(peak) + 0.5;
            fronts[k] = double(front) + 0.5;
            std::printf("RING age=%.2f r_view=%.2f expected_px=%.1f ring_peak_px=%.1f cell_front_px=%.1f cell_window_px=%.1f..%.1f\n",
                        double(ages[k]), r, want, peaks[k], fronts[k], lo, hi);
            ring_ok = ring_ok && std::fabs(peaks[k] - want) <= std::max(2.0, 0.03 * want);
            cells_ok = cells_ok && fronts[k] >= lo - 1.0 && fronts[k] <= hi + 1.0;
        }
        report("ring_front_at_two_ages", ring_ok && peaks[1] > peaks[0], "");
        report("cells_lit_as_the_ring_passes", cells_ok && fronts[1] > fronts[0], "");
    }
    // The fade: the whole patch's radiance at increasing ages after the ring, monotone down to zero at the lifetime.
    {
        const float ages[] = {0.35f, 0.5f, 0.7f, 0.9f, 1.1f, 1.3f, 1.5f, 1.7f, float(wc::patch_lifetime_ms) * 1e-3f};
        double prev = 1e30;
        bool monotone = true;
        std::printf("FADE");
        double last = 0;
        for (float a : ages) {
            const rr::WeaponPatch p = make_patch(fighter, {{0.f, 0.f, a}});
            const double sum = total(render(pass, &p, 1));
            std::printf(" t%.0f=%.3f", double(a) * 1000.0, sum);
            if (!(sum < prev)) monotone = monotone && sum == 0.0 && prev == 0.0;
            prev = sum;
            last = sum;
        }
        std::printf(" lifetime_ms=%u\n", wc::patch_lifetime_ms);
        report("fade_monotone_to_zero_at_lifetime", monotone && last == 0.0, "");
    }
    // Colour bands: the hit cell's colour at shield 0.9 / 0.5 / 0.2.
    {
        const float fractions[3] = {0.9f, 0.5f, 0.2f};
        float rgb[3][3]{};
        for (unsigned k = 0; k < 3; ++k) {
            const rr::WeaponPatch p = make_patch(fighter, {{0.f, 0.f, 0.5f}}, fractions[k]);
            std::vector<float> c;
            render(pass, &p, 1, nullptr, &c);
            const std::size_t at = (std::size_t(g.h / 2) * g.w + g.w / 2) * 3;
            for (unsigned i = 0; i < 3; ++i) rgb[k][i] = c.empty() ? 0.f : c[at + i];
            std::printf("COLOUR shield=%.2f r=%.4f g=%.4f b=%.4f\n", double(fractions[k]), rgb[k][0], rgb[k][1], rgb[k][2]);
        }
        const bool blue = rgb[0][2] > rgb[0][1] && rgb[0][1] > rgb[0][0];
        const bool yellow = std::min(rgb[1][0], rgb[1][1]) > 1.5f * rgb[1][2] && rgb[1][0] >= rgb[1][1];
        const bool red = rgb[2][0] > 2.f * rgb[2][1] && rgb[2][0] > 2.f * rgb[2][2];
        report("colour_bands_blue_yellow_red", blue && yellow && red, "");
    }
    // Repeated hits: the same point hit at 1.0 s and 0.4 s ago re-lights (the cell maximum, not the sum); four hits
    // spread over the region accumulate (their union is lit, the fourth slot draws); the cap culls nothing lit.
    {
        const rr::WeaponPatch a = make_patch(fighter, {{0.f, 0.f, 0.7f}}), b = make_patch(fighter, {{0.f, 0.f, 0.3f}});
        const rr::WeaponPatch ab = make_patch(fighter, {{0.f, 0.f, 0.3f}, {0.f, 0.f, 0.7f}});
        const float la = centre_luma(render(pass, &a, 1, &cells_only)), lb = centre_luma(render(pass, &b, 1, &cells_only));
        const float lab = centre_luma(render(pass, &ab, 1, &cells_only));
        std::printf("RELIGHT old=%.4f new=%.4f both=%.4f\n", la, lb, lab);
        report("repeated_hit_relights_max_not_sum", la > 0.f && lb > la && std::fabs(lab - std::max(la, lb)) < 1e-3f * lb &&
                                                        lab < la + lb - 0.5f * la, "");
        const float d = 0.6f * planar_radius(fighter);
        const Hit spread[4] = {{d, 0.f, 0.4f}, {-d, 0.f, 0.6f}, {0.f, d, 0.8f}, {0.f, -d, 1.0f}};
        unsigned single_max = 0;
        for (const Hit& h : spread) {
            const rr::WeaponPatch one = make_patch(fighter, {h});
            single_max = std::max(single_max, lit_count(render(pass, &one, 1)));
        }
        const rr::WeaponPatch four = make_patch(fighter, {spread[0], spread[1], spread[2], spread[3]});
        const std::vector<float> l4 = render(pass, &four, 1);
        // The cap against a cap 0.25 rad wider (similar facets: 9 against 5.2 deg per ring): a cull of lit pixels would
        // show as more lit pixels and radiance in the wider one. The whole sphere (pi, 22.5 deg per ring) is reported
        // only: its coarse facets move the interpolated surface point by a fraction of a cell.
        rr::WeaponPatch wider = four, whole = four;
        wider.cap_angle = four.cap_angle + 0.25f;
        whole.cap_angle = 3.14159265f;
        const std::vector<float> lw = render(pass, &wider, 1);
        const std::vector<float> lpi = render(pass, &whole, 1);
        const double t4 = total(l4), tw = total(lw);
        const double n4 = lit_count(l4), nw = lit_count(lw);
        const float diff = float(std::max(std::fabs(t4 - tw) / std::max(tw, 1e-9), std::fabs(n4 - nw) / std::max(nw, 1.0)));
        std::printf("CAPCHECK cap_lit=%u wider_lit=%u sphere_lit=%u cap_total=%.3f wider_total=%.3f sphere_total=%.3f\n",
                    unsigned(n4), unsigned(nw), lit_count(lpi), t4, tw, total(lpi));
        // The fourth (oldest) hit at (0, -d): plane v maps to view -y, so -d is view +y, up on screen (a smaller row).
        const double cx = g.w * 0.5, off = screen_x(g, fighter, d) - cx;
        const float fourth = l4[std::size_t(g.h / 2 - unsigned(off)) * g.w + g.w / 2];
        std::printf("ACCUMULATE four_lit=%u single_max_lit=%u fourth_slot=%.4f cap_angle=%.4f cap_vs_wider_rel_diff=%.6f\n",
                    lit_count(l4), single_max, fourth, double(four.cap_angle), double(diff));
        report("four_hits_accumulate", lit_count(l4) > single_max * 3 / 2 && fourth > 1e-3f, "");
        report("cpu_cap_culls_nothing_lit", diff < 0.01f && four.cap_angle < 1.5f, "");
    }
    // The decal fallback through the production builder: an owner without an ellipsoid (radius 0) gives one flat
    // patch at the hit point and no patch; it draws on a hull at its depth and 0 over sky.
    {
        rr::WeaponPatch patches[wc::max_patches];
        rr::WeaponDecal decals[wc::max_decals];
        stage_owner(400.f, 100.f, 1000);
        put32(fake_obj, wc::layout::obj_radius, 0);
        stage_ring.slots[0].pos[2] = -10000; // the hit point 100 view units in front of the owner's centre
        const ws::Counts n = ws::build(stage_ring, 1300, &fake_read, identity_camera(), ws::Tuning{}, patches,
                                       wc::max_patches, decals, wc::max_decals);
        rr::WeaponEffectsFrame f = base_frame(g);
        f.decals = decals;
        f.decal_count = n.decals;
        draw_lane(g, 0.f, -1.f);
        draw(pass, f);
        const double over_sky = total(read_luma(g, g.scene_s.p));
        draw_lane(g, n.decals ? decals[0].centre[2] : 300.f, 1.0f);
        auto res = draw(pass, f);
        const std::vector<float> l = read_luma(g, g.scene_s.p);
        const double on_hull = total(l);
        std::printf("DECAL patches=%u decals=%u centre_z=%.1f radius=%.2f cell=%.2f over_sky=%.4f on_hull=%.4f edges=%u\n",
                    n.patches, n.decals, n.decals ? decals[0].centre[2] : 0.f, n.decals ? decals[0].radius : 0.f,
                    n.decals ? decals[0].cell : 0.f, over_sky, on_hull,
                    edge_crossings(l, n.decals ? unsigned(decals[0].cell * 1.6f * 960.f / decals[0].centre[2]) : 4));
        report("decal_fallback_hex_flat", n.patches == 0 && n.decals == 1 && SUCCEEDED(res.first) && res.second.decals == 1 &&
                                              over_sky == 0.0 && on_hull > 1.0, "");
        draw_lane(g, 0.f, -1.f);
    }
    // The age law at SETA 6, measured: the game clock advances six game ms per wall ms (QueryPerformanceCounter); every
    // frame the production builder ages the ring's one hit in game time and the pass draws it, fenced like a frame,
    // until the builder has nothing to draw. The record's patch_lifetime_ms end after a sixth of it in wall time.
    {
        stage_owner(400.f, 100.f, 0);
        Com<IDirect3DQuery9> q;
        dev->CreateQuery(D3DQUERYTYPE_EVENT, &q.p);
        LARGE_INTEGER freq{}, start{}, now{};
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        rr::WeaponPatch patches[wc::max_patches];
        rr::WeaponDecal decals[wc::max_decals];
        double wall_ms = 0, last_ms = 0, max_frame_ms = 0, last_drawn_ms = 0;
        unsigned frames = 0;
        for (;;) {
            QueryPerformanceCounter(&now);
            wall_ms = ticks_us(start, now, freq) * 1e-3;
            const std::uint32_t game = std::uint32_t(wall_ms * 6.0);
            const ws::Counts n = ws::build(stage_ring, game, &fake_read, identity_camera(), ws::Tuning{}, patches,
                                           wc::max_patches, decals, wc::max_decals);
            max_frame_ms = std::max(max_frame_ms, wall_ms - last_ms);
            last_ms = wall_ms;
            if (!n.patches || wall_ms > 4000.0) break;
            rr::WeaponEffectsFrame sf = base_frame(g);
            sf.patches = patches;
            sf.patch_count = n.patches;
            draw(pass, sf);
            fence(q.p);
            ++frames;
            last_drawn_ms = wall_ms;
        }
        const double expected = double(wc::patch_lifetime_ms) / 6.0;
        std::printf("AGE lifetime_ms=%u seta6_expected_wall_ms=%.2f seta6_wall_ms=%.2f last_drawn_ms=%.2f frames=%u max_frame_ms=%.3f\n",
                    wc::patch_lifetime_ms, expected, wall_ms, last_drawn_ms, frames, max_frame_ms);
        report("age_law_seta6_measured", frames > 0 && last_drawn_ms <= expected + 0.2 && wall_ms >= expected &&
                                             wall_ms <= expected + 0.2 + max_frame_ms + 0.5, "");
    }
    // Retired owner, through the production builder: the live owner's hit gives one patch that draws; once the owner
    // carries the kill routine's dying bit the builder retires its record (the ring is empty) and nothing draws.
    {
        rr::WeaponPatch patches[wc::max_patches];
        rr::WeaponDecal decals[wc::max_decals];
        stage_owner(400.f, 100.f, 1000);
        const ws::Counts live = ws::build(stage_ring, 1300, &fake_read, identity_camera(), ws::Tuning{}, patches,
                                          wc::max_patches, decals, wc::max_decals);
        rr::WeaponEffectsFrame f = base_frame(g);
        f.patches = patches;
        f.patch_count = live.patches;
        auto drawn = draw(pass, f);
        const double live_sum = total(read_luma(g, g.scene_s.p));
        put32(fake_obj, wc::layout::obj_flags, wc::layout::obj_dying);
        const ws::Counts dead = ws::build(stage_ring, 1300, &fake_read, identity_camera(), ws::Tuning{}, patches,
                                          wc::max_patches, decals, wc::max_decals);
        f.patch_count = dead.patches;
        f.decal_count = dead.decals;
        auto res = draw(pass, f);
        const double dead_sum = total(read_luma(g, g.scene_s.p));
        std::printf("RETIRED live_patches=%u live_drew=%u live_sum=%.4f patches=%u decals=%u retired=%u ring_live=%u result=%08lx drew=%u sum=%.4f\n",
                    live.patches, unsigned(drawn.second.drew), live_sum, dead.patches, dead.decals, dead.retired,
                    ring_live(stage_ring), res.first, unsigned(res.second.drew), dead_sum);
        report("stage_build_live_owner_draws", live.patches == 1 && drawn.second.drew && live_sum > 1.0, "");
        report("retired_owner_draws_nothing", dead.patches == 0 && dead.decals == 0 && dead.retired == 1 &&
                                                  ring_live(stage_ring) == 0 && res.first == S_FALSE && !res.second.drew &&
                                                  dead_sum == 0.0, "");
    }
    // The core through the production resolve at 0 / 4 / 8 px per frame.
    {
        Com<IDirect3DTexture9> motion;
        Com<IDirect3DSurface9> motion_s;
        const bool made = SUCCEEDED(dev->CreateTexture(g.w, g.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F,
                                                       D3DPOOL_DEFAULT, &motion.p, nullptr)) &&
                          SUCCEEDED(motion.p->GetSurfaceLevel(0, &motion_s.p));
        for (const float speed : {0.f, 4.f, 8.f}) {
            float peak = 0.f;
            const double ratio = made ? resolve_core(pass, motion, motion_s, speed, &peak) : -1.0;
            std::printf("RESOLVE width=%u height=%u speed_px=%.0f frames=16 core=%.4f peak=%.3f\n", g.w, g.h, double(speed),
                        ratio, double(peak));
            char label[48];
            std::snprintf(label, sizeof label, "resolve_core_%.0fpx_%u", double(speed), g.w);
            report(label, ratio >= 0.95, "");
        }
        draw_lane(g, 0.f, -1.f);
    }
    // Program creation refused (fault bit 3): attach refuses with program_create and keeps no object; run refuses at once.
    {
        rr::WeaponEffectsPass refused;
        refused.set_faults(8);
        const HRESULT hr = refused.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format);
        rr::WeaponEffectsFrame f = base_frame(g);
        const rr::WeaponPatch p = make_patch(fighter, {{0.f, 0.f, 0.f}});
        f.patches = &p;
        f.patch_count = 1;
        rr::WeaponEffectsReport rep{};
        const HRESULT run = refused.run(f, &rep);
        std::printf("REFUSED result=%08lx enabled=%u reason=%s references=%u run=%08lx calls=%u\n", hr,
                    unsigned(refused.caps().enabled), refused.caps().reason, refused.references(), run, rep.calls);
        report("program_create_refused", FAILED(hr) && !refused.caps().enabled &&
                                             !std::strcmp(refused.caps().reason, "program_create") &&
                                             refused.references() == 0 && run == E_INVALIDARG && rep.calls == 0, "");
    }
    // Fault once, then recover; then Reset.
    {
        rr::WeaponEffectsFrame f = base_frame(g);
        const rr::WeaponPatch p = make_patch(fighter, {{0.f, 0.f, 0.f}});
        f.patches = &p;
        f.patch_count = 1;
        pass.set_faults(2);
        auto res = draw(pass, f);
        report("fault_reported", FAILED(res.first) && res.second.failed != rr::WeaponEffectsStep::None, "");
        pass.set_faults(0);
        auto res2 = draw(pass, f);
        report("recovers_after_fault", SUCCEEDED(res2.first) && res2.second.drew, "");
        pass.before_reset();
        g.release();                       // only DEFAULT-pool targets are lost across Reset; shaders survive
        D3DPRESENT_PARAMETERS pp2 = pp;
        const HRESULT reset = dev->Reset(&pp2);
        pass.after_reset(reset);
        std::printf("RESET result=%08lx\n", reset);
        const bool mt = SUCCEEDED(reset) && make_target(g, 1920, 1080);
        std::printf("RESET make_target=%u\n", unsigned(mt));
        const HRESULT er = pass.ensure_resources();
        std::printf("RESET ensure=%08lx\n", er);
        bool drew_after = false;
        if (mt && SUCCEEDED(er)) {
            draw_lane(g, 0.f, -1.f);
            rr::WeaponEffectsFrame f2 = base_frame(g);
            f2.patches = &p;
            f2.patch_count = 1;
            auto after = draw(pass, f2);
            drew_after = SUCCEEDED(after.first) && after.second.drew && total(read_luma(g, g.scene_s.p)) > 1.0;
        }
        report("reset_recovers", mt && SUCCEEDED(er) && pass.resources_ready() && drew_after, "");
        draw_lane(g, 0.f, -1.f);
    }
    // Timing, EVENT-fenced (best of 60), over a sky lane: an empty fence (the fence's own round trip); two patches on a
    // fighter (a 20 m owner at 100 m), once as the stage draws two hits of one anchor region (one draw, two hit slots)
    // and once as two regions (two draws); the same on a capital at close range (a 400 m owner, the hits 100 m from the
    // camera, each patch over most of the frame); and the worst one owner can submit (that capital with its four
    // newest hits in four regions: four draws, each cap over the frame), at 1080p and 5120x1440.
    for (auto size : {std::pair<UINT, UINT>{1920, 1080}, std::pair<UINT, UINT>{5120, 1440}}) {
        Target tt;
        if (!make_target(tt, size.first, size.second)) { report("timing_target", false, ""); continue; }
        draw_lane(tt, 0.f, -1.f);
        const TierScene fighter_far{wc::patch_fighter, 100.f, 600.f};
        const TierScene capital_close{wc::patch_capital, 2000.f, 2500.f};
        const float rf = planar_radius(fighter_far), rc = planar_radius(capital_close);
        const rr::WeaponPatch fighter_one[1] = {make_patch(fighter_far, {{-0.8f * rf, 0.f, 0.2f}, {0.8f * rf, 0.f, 0.5f}})};
        const rr::WeaponPatch fighter_regions[2] = {make_patch(fighter_far, {{-0.8f * rf, 0.f, 0.2f}}),
                                                    make_patch(fighter_far, {{0.8f * rf, 0.f, 0.5f}})};
        const rr::WeaponPatch capital_one[1] = {make_patch(capital_close, {{-0.3f * rc, 0.f, 0.2f}, {0.3f * rc, 0.f, 0.5f}})};
        const rr::WeaponPatch capital_regions[2] = {make_patch(capital_close, {{-0.3f * rc, 0.f, 0.2f}}),
                                                    make_patch(capital_close, {{0.3f * rc, 0.f, 0.5f}})};
        rr::WeaponPatch worst[4];
        for (unsigned k = 0; k < 4; ++k)
            worst[k] = make_patch(capital_close, {{(k & 1 ? 0.3f : -0.3f) * rc, (k & 2 ? 0.3f : -0.3f) * rc, 0.1f + 0.1f * k}});
        struct Case {
            const char* name;
            const rr::WeaponPatch* patches;
            unsigned count;
        } cases[] = {{"fence", nullptr, 0},
                     {"fighter_two_hits_one_region", fighter_one, 1}, {"fighter_two_regions", fighter_regions, 2},
                     {"capital_close_two_hits_one_region", capital_one, 1},
                     {"capital_close_two_regions", capital_regions, 2}, {"worst_four_regions", worst, 4}};
        Com<IDirect3DQuery9> ev;
        dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev.p);
        LARGE_INTEGER freq, a, b;
        QueryPerformanceFrequency(&freq);
        for (const Case& c : cases) {
            rr::WeaponEffectsFrame f = base_frame(tt);
            f.patches = c.patches;
            f.patch_count = c.count;
            double best = 1e30;
            unsigned drawn = 0;
            for (int i = 0; i < 60; ++i) {
                dev->BeginScene();
                dev->SetRenderTarget(0, tt.scene_s.p);
                dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
                fence(ev.p);
                QueryPerformanceCounter(&a);
                rr::WeaponEffectsReport r{};
                if (c.count) pass.run(f, &r);
                fence(ev.p);
                QueryPerformanceCounter(&b);
                dev->EndScene();
                drawn = r.patches;
                const double us = ticks_us(a, b, freq);
                if (us < best) best = us;
            }
            // Coverage: the share of the frame the patches lit (one readback after the timed runs).
            dev->BeginScene();
            dev->SetRenderTarget(0, tt.scene_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            if (c.count) {
                rr::WeaponEffectsReport r{};
                pass.run(f, &r);
            }
            dev->EndScene();
            const std::vector<float> l = read_luma(tt, tt.scene_s.p);
            std::printf("TIMING width=%u height=%u case=%s patches=%u drawn=%u lit_share=%.4f gpu_us=%.1f\n", size.first,
                        size.second, c.name, c.count, drawn, l.empty() ? 0.0 : double(lit_count(l)) / double(l.size()),
                        best);
        }
        tt.release();
    }

    pass.detach();
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
