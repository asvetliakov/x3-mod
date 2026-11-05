// D3D fixture of the weapon-effects shield-hit stage (src/renderer/weapon_effects_pass.cpp, the production
// WeaponEffectsPass and its patch / decal programs; docs/architecture/weapon-effects.md 5.1 acceptance (b) and 5.1.1).
// It creates a plain D3D9 HAL device, attaches the pass, and draws hex patches and flat decals onto an A16B16G16R16F
// scene target with a controlled four-channel depth lane (.b = view depth, a sentinel .r < 0 = sky), built by a small FP
// lane shader compiled through the game's d3dx9_37.dll (argv[1], run_weapon_effects.py's contract). Patches are built
// with the production ws::fill_patch on an owner whose anchor region faces the camera (the hit at the anchor sits at
// the screen centre). It reads the FP16 target back and reports the structural relations of 5.1.1 (the cell-map model):
// per tier the footprint's cell count 1 + 3 rings (rings + 1), the lit area bounded by hexagon edges (the outermost cell
// centres plus a circumradius) and zero outside, lit on both sides of the hit in both axes, the edge bands across it;
// the hit's centre cell brighter than a ring cell by the marker law; the ring moving by whole cells and lighting a cell when the front reaches
// its centre; the patch fading monotonically to zero at the record's lifetime and one cell's own fade duration against
// its hash; the colour bands at shield 0.9 / 0.5 / 0.2; a repeated hit re-lights (the newest clock, not a sum); a second
// hit on the next cell resets only the shared cells and no cell is drawn twice; the CPU cap culls nothing lit; through
// the production builder the grid fixed on a moved and rotated owner, 12 hits per second without a pop on a fighter and
// spread over a capital's face (the map overflows into spill draws; with one draw per region the evicted cells' pops
// are the recorded witness), two hits across a region seam never brighter than one hit, the stage's CPU cost; the decal
// fallback (an owner without an ellipsoid) on a hull and 0 over sky; the game-time law at SETA 6 measured; the marker
// through the production TemporalPass at 0 / 4 / 8 px per frame; a retired owner draws nothing; Reset recovery, the
// fault cases, and the stage cost at 1080p and 5120x1440 (two hits on a fighter, two on a capital at close range, the
// worst case one owner can submit, the 12 hits per second map, a full 128-cell map, one whose cap fills the frame and
// the capital spread's spill draws, each with its lit and rasterized screen shares). Every draw and readback runs inside
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
rr::WeaponPatch make_patch(const TierScene& s, const Hit* hits, unsigned count, float shield = 1.f, float cx = 0.f,
                           const ws::Tuning& t = ws::Tuning{}) {
    rr::WeaponPatch p{};
    float axes[9], centre[3] = {cx, 0.f, s.centre_z};
    facing_axes(s.surface, axes);
    float dirs[wc::owner_hits][3], ages[wc::owner_hits], colour[3];
    unsigned n = 0;
    for (unsigned k = 0; k < count; ++k) {
        const Hit& h = hits[k];
        if (n >= wc::owner_hits) break;
        plane_dir(s.surface, h.qx, h.qy, dirs[n]);
        ages[n++] = h.age;
    }
    wc::shield_colour(shield, t.bands, colour);
    ws::fill_patch(p, centre, axes, t.soft * s.surface, s.tier, anchor0, dirs, ages, n, colour, t);
    return p;
}
// Hits newest first (the stage's order).
rr::WeaponPatch make_patch(const TierScene& s, std::initializer_list<Hit> hits, float shield = 1.f, float cx = 0.f,
                           const ws::Tuning& t = ws::Tuning{}) {
    return make_patch(s, hits.begin(), unsigned(hits.size()), shield, cx, t);
}
// Screen x (px) of the plane point (q, 0) on the scanline through the anchor (the surface point, perspective).
double screen_x(const Target& t, const TierScene& s, double q) {
    const double S = s.surface, X = q / (2.0 * S), r2 = X * X;
    const double x = S * 2.0 * X / (1.0 + r2), z = s.centre_z - S * (1.0 - r2) / (1.0 + r2);
    return double(t.w) * 0.5 * (1.0 + 1.6 * x / z);
}
unsigned tier_rings(const TierScene& s) {
    return wc::rings[s.tier];
}
// The footprint's outermost cell centres (view units) and the lit extent: those plus a cell's circumradius.
float planar_reach(const TierScene& s) {
    return float(tier_rings(s)) * ws::metres_to_view(wc::cell_m[s.tier]);
}
float planar_extent(const TierScene& s) {
    return planar_reach(s) + 0.5773503f * ws::metres_to_view(wc::cell_m[s.tier]);
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
// Another stage-owned shield hit on the staged owner (local engine position, t0 game ms), like stage_owner's.
void add_stage_hit(std::uint32_t t0, const std::int32_t pos[3], std::uint8_t tier = wc::patch_fighter) {
    wc::Event* e = wc::ring_acquire(stage_ring);
    if (!e) return;
    e->role = wc::role_shield_hit;
    e->style = wc::shape_slug;
    e->flags = std::uint8_t(wc::flag_stage | wc::flag_forwarded | wc::flag_shield_read);
    e->shield_q = 0xffff;
    e->patch_tier = tier;
    e->t0_game_ms = t0;
    e->lifetime_ms = std::uint16_t(wc::patch_lifetime_ms);
    e->damage = 1200;
    e->tint_rgb = 0xffffffu | wc::rgb_present;
    e->owner = addr(fake_obj);
    e->node = addr(fake_node);
    e->handle = 0xd00du;
    for (unsigned k = 0; k < 3; ++k) e->pos[k] = pos[k];
    std::int32_t r = 0;
    std::memcpy(&r, fake_obj + wc::layout::obj_radius, 4);
    e->radius = r;
}
// The luma on the scanline through the anchor at the plane point (q, 0) (view units).
float scan_luma(const std::vector<float>& l, const TierScene& s, double q) {
    if (l.empty()) return -1.f;
    const int x = int(std::floor(screen_x(g, s, q)));
    if (x < 0 || x >= int(g.w)) return -1.f;
    return l[std::size_t(g.h / 2) * g.w + std::size_t(x)];
}
// weapon_hex_patch.hlsl hex_hash(key, seed) in float: .x the cell's brightness stagger input, .y its fade rate's.
std::pair<double, double> cell_hash(float kx, float ky, float seed) {
    auto fr = [](float v) { return v - std::floor(v); };
    float x = fr(kx * 0.1031f), y = fr(ky * 0.1030f), z = fr((kx + ky + seed * 17.f) * 0.0973f);
    const float d = x * (y + 33.33f) + y * (z + 33.33f) + z * (x + 33.33f);
    x += d;
    y += d;
    z += d;
    return {double(fr((x + y) * z)), double(fr((x + z) * y))};
}
double cell_hash_x(float kx, float ky, float seed) {
    return cell_hash(kx, ky, seed).first;
}
double cell_hash_y(float kx, float ky, float seed) {
    return cell_hash(kx, ky, seed).second;
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
    rr::WeaponLook cells_only = rr::default_weapon_look(), ring_only = cells_only, uniform = cells_only;
    cells_only.ring_gain = 0.f;
    ring_only.cell_gain = 0.f;
    // The pop gate's look: cells only, the marker at the ring cells' level, so a re-lit cell never changes level
    // (the user's rule: a re-hit cell takes the newest hit's centre or ring level) and every drop is the map's.
    uniform.ring_gain = 0.f;
    uniform.centre_gain = 1.f;
    uniform.centre_fill = uniform.fill_ratio;

    // Per tier at age 0.5 (every footprint cell lit, core and ring gone): the lit area within the footprint's extent
    // (its outermost cell centres plus a cell's circumradius: the boundary is hexagon edges, lit pixels lie past the
    // outermost centres), zero outside, lit on both sides of the hit in both axes, the edge bands across the scanline
    // (2 rings + 2 cell borders) and the footprint's cell count 1 + 3 rings (rings + 1) per tier.
    {
        bool bounded = true, edges_ok = true, symmetric = true, counts_ok = true;
        for (unsigned t = 0; t < 3; ++t) {
            const TierScene& s = tier_scene[t];
            const rr::WeaponPatch p = make_patch(s, {{0.f, 0.f, 0.5f}});
            const std::vector<float> l = render(pass, &p, 1);
            const double cx = g.w * 0.5, cy = g.h * 0.5;
            const double reach = planar_reach(s), extent = planar_extent(s), cell = planar_cell(s);
            const double reach_px = screen_x(g, s, reach) - cx, bound_px = screen_x(g, s, extent) - cx;
            const double cell_px = screen_x(g, s, cell) - cx;
            double max_px = 0, outside = 0;
            for (UINT y = 0; y < g.h; ++y)
                for (UINT x = 0; x < g.w; ++x) {
                    const float v = l[std::size_t(y) * g.w + x];
                    const double d = std::sqrt((x + 0.5 - cx) * (x + 0.5 - cx) + (y + 0.5 - cy) * (y + 0.5 - cy));
                    if (v > 1e-3f) max_px = std::max(max_px, d);
                    if (d > bound_px + 1.5) outside += double(v);
                }
            // Both sides of the hit in both axes (a hit facing the camera lights a whole patch, not a half): the lit
            // pixels left / right / above / below the hit and their extents from it.
            double side[4]{}, ext[4]{};
            for (UINT y = 0; y < g.h; ++y)
                for (UINT x = 0; x < g.w; ++x) {
                    if (!(l[std::size_t(y) * g.w + x] > 1e-3f)) continue;
                    const double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
                    if (dx < 0) { side[0] += 1; ext[0] = std::max(ext[0], -dx); }
                    if (dx > 0) { side[1] += 1; ext[1] = std::max(ext[1], dx); }
                    if (dy < 0) { side[2] += 1; ext[2] = std::max(ext[2], -dy); }
                    if (dy > 0) { side[3] += 1; ext[3] = std::max(ext[3], dy); }
                }
            const double side_lo = std::min(std::min(side[0], side[1]), std::min(side[2], side[3]));
            const double side_hi = std::max(std::max(side[0], side[1]), std::max(side[2], side[3]));
            const double ext_lo = std::min(std::min(ext[0], ext[1]), std::min(ext[2], ext[3]));
            const double ext_hi = std::max(std::max(ext[0], ext[1]), std::max(ext[2], ext[3]));
            std::printf("SYMMETRY tier=%s left=%.0f right=%.0f up=%.0f down=%.0f ext_left=%.1f ext_right=%.1f ext_up=%.1f ext_down=%.1f\n",
                        wc::patch_tier_name(s.tier), side[0], side[1], side[2], side[3], ext[0], ext[1], ext[2], ext[3]);
            symmetric = symmetric && side_lo > 0 && side_lo >= 0.8 * side_hi && ext_lo >= 0.8 * ext_hi && ext_lo >= reach_px;
            const unsigned edges = edge_crossings(l, unsigned(cell_px));
            const unsigned rings = tier_rings(s), expected = 2 * rings + 2, cells = 1 + 3 * rings * (rings + 1);
            std::printf("PATCH tier=%s rings=%u cells=%u expected_cells=%u reach_view=%.2f cell_view=%.2f reach_px=%.1f bound_px=%.1f cell_px=%.1f lit_px=%.1f outside_sum=%.6f edges=%u expected_edges=%u lit_pixels=%u peak=%.4f\n",
                        wc::patch_tier_name(s.tier), rings, p.cells, cells, reach, cell, reach_px, bound_px, cell_px,
                        max_px, outside, edges, expected, lit_count(l), double(*std::max_element(l.begin(), l.end())));
            bounded = bounded && max_px <= bound_px + 1.5 && max_px > reach_px && outside == 0.0;
            edges_ok = edges_ok && (edges + 1 >= expected && edges <= expected + 1);
            counts_ok = counts_ok && p.cells == cells;
        }
        report("lit_area_bounded_by_cell_edges_per_tier", bounded, "");
        report("patch_lit_both_sides_both_axes", symmetric, "");
        report("edge_count_matches_rings", edges_ok, "");
        report("ring_count_per_tier", counts_ok && wc::cell_m[0] < wc::cell_m[1] && wc::cell_m[1] < wc::cell_m[2] &&
                                          wc::rings[0] <= wc::rings[1] && wc::rings[1] <= wc::rings[2], "");
    }
    const TierScene& fighter = tier_scene[0];
    // The marker (user decision 2026-11-06, no core flash): cells only at 0.5 s, the hit cell's fill (key (0, 0), lit at
    // 0.5 s) over the next cell's fill (key (2, 0), lit by the ring 0.2 s later) against the law centre_gain centre_fill
    // / fill_ratio with both cells' brightness and fade hashes; and the hit point at 0.05 s equals the hit cell's level
    // there (no per-pixel term at the hit point: the core is gone).
    {
        const rr::WeaponPatch p = make_patch(fighter, {{0.f, 0.f, 0.5f}});
        const std::vector<float> l = render(pass, &p, 1, &cells_only);
        const double c = planar_cell(fighter);
        const double hit_cell = scan_luma(l, fighter, 0.0), ring_cell = scan_luma(l, fighter, c);
        const double travel = 1.0 / (double(wc::ripple_speed) * (double(tier_rings(fighter)) + 0.5));
        auto level = [](float kx, float ky, double since, double gain) {
            const double s = double(wc::stagger), fade = double(wc::fade_ms) * 1e-3 * (1.0 + s * (cell_hash_y(kx, ky, 0.f) - 0.5));
            return gain * (1.0 + s * (cell_hash_x(kx, ky, 0.f) - 0.5)) * (1.0 - since / fade);
        };
        const double expected = level(0.f, 0.f, 0.5, double(wc::centre_gain) * double(wc::centre_fill)) /
                                level(2.f, 0.f, 0.5 - travel, double(wc::fill_ratio));
        const double ratio = ring_cell > 0.0 ? hit_cell / ring_cell : 0.0;
        const rr::WeaponPatch young = make_patch(fighter, {{0.f, 0.f, 0.05f}});
        const double at05 = centre_luma(render(pass, &young, 1)), cells05 = centre_luma(render(pass, &young, 1, &cells_only));
        std::printf("CENTRE hit_cell=%.4f ring_cell=%.4f ratio=%.4f expected=%.4f centre_gain=%.2f centre_fill=%.2f fill_ratio=%.2f at05=%.4f at05_cells=%.4f\n",
                    hit_cell, ring_cell, ratio, expected, double(wc::centre_gain), double(wc::centre_fill),
                    double(wc::fill_ratio), at05, cells05);
        report("centre_cell_brighter_than_ring_cell", ratio > 1.5 && std::fabs(ratio - expected) <= 0.02 * expected, "");
        report("no_core_at_the_hit_point", at05 > 0.0 && at05 <= cells05 + 0.25 * cells05, "");
    }
    // The ring moves by whole cells: ring-only at the hit cell's centre, a point inside the same cell's fill and the
    // next cell's centre (plane x = 1 cell) at 0.05 s (front 0.25 cells out: the hit cell) and 0.2 s (front at the next
    // centre); the cells-only next cell is dark until the front reaches its centre (0.2 s) and lit after (0.25 s).
    {
        const double cell = planar_cell(fighter);
        const float ages[2] = {0.05f, 0.2f};
        float centre_v[2]{}, inside_v[2]{}, next_v[2]{};
        for (unsigned k = 0; k < 2; ++k) {
            const rr::WeaponPatch p = make_patch(fighter, {{0.f, 0.f, ages[k]}});
            const std::vector<float> l = render(pass, &p, 1, &ring_only);
            centre_v[k] = scan_luma(l, fighter, 0.0);
            inside_v[k] = scan_luma(l, fighter, 0.2 * cell);
            next_v[k] = scan_luma(l, fighter, cell);
            std::printf("RING age=%.2f front_cells=%.3f hit_cell=%.4f hit_cell_inside=%.4f next_cell=%.4f\n", double(ages[k]),
                        double(wc::ripple_speed) * (double(tier_rings(fighter)) + 0.5) * double(ages[k]), centre_v[k],
                        inside_v[k], next_v[k]);
        }
        const rr::WeaponPatch before = make_patch(fighter, {{0.f, 0.f, 0.1f}}), after = make_patch(fighter, {{0.f, 0.f, 0.25f}});
        const float dark = scan_luma(render(pass, &before, 1, &cells_only), fighter, cell);
        const float lit = scan_luma(render(pass, &after, 1, &cells_only), fighter, cell);
        std::printf("RINGCELLS next_at100=%.4f next_at250=%.4f\n", dark, lit);
        bool whole = true;
        for (unsigned k = 0; k < 2; ++k) whole = whole && std::fabs(centre_v[k] - inside_v[k]) <= 2e-3f * std::max(centre_v[k], 1.f);
        report("ring_moves_by_whole_cells", whole && centre_v[0] > 0.01f && centre_v[0] > 10.f * next_v[0] &&
                                                next_v[1] > 0.05f && next_v[1] > 10.f * centre_v[1], "");
        report("cells_lit_as_the_ring_passes", dark == 0.f && lit > 0.1f, "");
    }
    // The fade: the whole patch's radiance at increasing ages after the ring, monotone down to zero at the lifetime;
    // and one cell's own fade duration (the hit cell's fill, cells only, at 0.5 s and 1.0 s, extrapolated to zero)
    // against the law fade x (1 + stagger (hash - 1/2)) with the cell's hash.
    {
        const float ages[] = {0.35f, 0.6f, 0.9f, 1.2f, 1.5f, 1.8f, 2.1f, 2.4f, 2.7f, float(wc::patch_lifetime_ms) * 1e-3f};
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
        const rr::WeaponPatch p0 = make_patch(fighter, {{0.f, 0.f, 0.5f}}), p1 = make_patch(fighter, {{0.f, 0.f, 1.0f}});
        const double b0 = scan_luma(render(pass, &p0, 1, &cells_only), fighter, 0.0);
        const double b1 = scan_luma(render(pass, &p1, 1, &cells_only), fighter, 0.0);
        const double measured = b0 > b1 ? 1.0 + b1 * 0.5 / (b0 - b1) : 0.0;
        const double fade = double(wc::fade_ms) * 1e-3, expected = fade * (1.0 + double(wc::stagger) * (cell_hash_y(0, 0, float(anchor0)) - 0.5));
        std::printf("FADECELL at500=%.4f at1000=%.4f measured_s=%.4f expected_s=%.4f fade_s=%.3f min_s=%.3f max_s=%.3f\n", b0,
                    b1, measured, expected, fade, fade * (1.0 - 0.5 * wc::stagger), fade * (1.0 + 0.5 * wc::stagger));
        report("per_cell_fade_duration", std::fabs(measured - expected) <= 0.02 * expected && fade >= 2.0 && fade <= 2.5, "");
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
    // Repeated hits: the same point hit 0.7 s and 0.3 s ago re-lights (the cell's clock is the newest hit's, not a
    // sum); a second hit on the next cell (plane x = 1 cell, 0.5 s after the first at 1.0 s) resets only the cells the
    // two footprints share (the hit cells, centres x = 0 and 1 cell, back to the second hit's value) while the first
    // footprint's other cells (x = -1 cell) keep fading on their own clock and the second's (x = 2 cells) carry its
    // clock; no cell is drawn twice (the pair's peak never exceeds the brighter single hit's); the cap culls nothing lit.
    {
        const rr::WeaponPatch a = make_patch(fighter, {{0.f, 0.f, 0.7f}}), b = make_patch(fighter, {{0.f, 0.f, 0.3f}});
        const rr::WeaponPatch ab = make_patch(fighter, {{0.f, 0.f, 0.3f}, {0.f, 0.f, 0.7f}});
        const float la = centre_luma(render(pass, &a, 1, &cells_only)), lb = centre_luma(render(pass, &b, 1, &cells_only));
        const float lab = centre_luma(render(pass, &ab, 1, &cells_only));
        std::printf("RELIGHT old=%.4f new=%.4f both=%.4f\n", la, lb, lab);
        report("repeated_hit_relights_max_not_sum", la > 0.f && lb > la && std::fabs(lab - std::max(la, lb)) < 1e-3f * lb &&
                                                        lab < la + lb - 0.5f * la, "");
        const float c = float(planar_cell(fighter));
        const Hit first{0.f, 0.f, 1.0f}, second{c, 0.f, 0.5f};
        const rr::WeaponPatch pa = make_patch(fighter, {first}), pb = make_patch(fighter, {second});
        const rr::WeaponPatch pab = make_patch(fighter, {second, first});
        const std::vector<float> la1 = render(pass, &pa, 1, &cells_only), lb1 = render(pass, &pb, 1, &cells_only);
        const std::vector<float> lab1 = render(pass, &pab, 1, &cells_only);
        const double xs[4] = {-1.0, 0.0, 1.0, 2.0};
        float va[4], vb[4], vab[4];
        for (unsigned k = 0; k < 4; ++k) {
            va[k] = scan_luma(la1, fighter, xs[k] * c);
            vb[k] = scan_luma(lb1, fighter, xs[k] * c);
            vab[k] = scan_luma(lab1, fighter, xs[k] * c);
        }
        const float peak_single = std::max(*std::max_element(la1.begin(), la1.end()), *std::max_element(lb1.begin(), lb1.end()));
        const float peak_pair = *std::max_element(lab1.begin(), lab1.end());
        std::printf("ADJACENT cells=%u first_m1=%.4f first_0=%.4f first_1=%.4f second_0=%.4f second_1=%.4f second_2=%.4f pair_m1=%.4f pair_0=%.4f pair_1=%.4f pair_2=%.4f peak_single=%.4f peak_pair=%.4f\n",
                    pab.cells, va[0], va[1], va[2], vb[1], vb[2], vb[3], vab[0], vab[1], vab[2], vab[3], peak_single, peak_pair);
        const auto same = [](float x, float y) { return std::fabs(x - y) <= 2e-3f * std::max(std::fabs(y), 1.f); };
        report("adjacent_hit_resets_shared_cells_only", pab.cells == 10 && same(vab[0], va[0]) && same(vab[1], vb[1]) &&
                                                            same(vab[2], vb[2]) && same(vab[3], vb[3]) &&
                                                            va[0] > 0.f && vb[0] == 0.f, ""); // x = 0: the second's ring level
        report("no_cell_drawn_twice", peak_pair <= peak_single * (1.f + 2e-3f), "");
        // The cap against a cap 0.25 rad wider (similar facets: a cull of lit pixels would show as more lit pixels and
        // radiance in the wider one), on four hits spread two cells about the hit.
        const Hit spread[4] = {{2.f * c, 0.f, 0.4f}, {-2.f * c, 0.f, 0.6f}, {0.f, 2.f * c, 0.8f}, {0.f, -2.f * c, 1.0f}};
        const rr::WeaponPatch four = make_patch(fighter, spread, 4);
        rr::WeaponPatch wider = four, whole = four;
        wider.cap_angle = four.cap_angle + 0.25f;
        whole.cap_angle = 3.14159265f;
        const std::vector<float> l4 = render(pass, &four, 1), lw = render(pass, &wider, 1), lpi = render(pass, &whole, 1);
        const double t4 = total(l4), tw = total(lw);
        const double n4 = lit_count(l4), nw = lit_count(lw);
        const float diff = float(std::max(std::fabs(t4 - tw) / std::max(tw, 1e-9), std::fabs(n4 - nw) / std::max(nw, 1.0)));
        std::printf("CAPCHECK cells=%u cap_lit=%u wider_lit=%u sphere_lit=%u cap_total=%.3f wider_total=%.3f sphere_total=%.3f cap_angle=%.4f rel_diff=%.6f\n",
                    four.cells, unsigned(n4), unsigned(nw), lit_count(lpi), t4, tw, total(lpi), double(four.cap_angle),
                    double(diff));
        report("cpu_cap_culls_nothing_lit", diff < 0.01f && four.cap_angle < 1.5f && n4 > 0, "");
    }
    // The grid is fixed on the owner (through the production builder): the same local hits on an owner moved and
    // rotated (another position, the axes turned 90 degrees about y) give the identical cell map: region, keys, clocks.
    {
        rr::WeaponPatch pa[wc::max_patches], pb[wc::max_patches];
        rr::WeaponDecal decals[wc::max_decals];
        const std::int32_t local[3][3] = {{3000, 1000, -9400}, {-2000, 2500, -9300}, {800, -1500, -9800}};
        stage_owner(400.f, 100.f, 1000);
        for (unsigned k = 0; k < 3; ++k) add_stage_hit(1100 + 200 * k, local[k]);
        const ws::Counts na = ws::build(stage_ring, 1700, &fake_read, identity_camera(), ws::Tuning{}, pa, wc::max_patches,
                                        decals, wc::max_decals);
        const auto engine = [](float v) { return std::uint32_t(std::int32_t(v / wc::world_scale)); };
        put32(fake_node, wc::layout::node_pos + 0, engine(50.f));
        put32(fake_node, wc::layout::node_pos + 4, engine(-30.f));
        put32(fake_node, wc::layout::node_pos + 8, engine(600.f));
        const std::int32_t rows[3][3] = {{0, 0, -65536}, {0, 65536, 0}, {65536, 0, 0}};
        for (unsigned r = 0; r < 3; ++r)
            for (unsigned c = 0; c < 3; ++c) put32(fake_node, wc::layout::node_rot + 16 * r + 4 * c, std::uint32_t(rows[r][c]));
        const ws::Counts nb = ws::build(stage_ring, 1700, &fake_read, identity_camera(), ws::Tuning{}, pb, wc::max_patches,
                                        decals, wc::max_decals);
        bool same = na.patches == nb.patches && na.patches > 0;
        unsigned cells = 0;
        for (unsigned i = 0; same && i < na.patches; ++i) {
            same = pa[i].seed == pb[i].seed && pa[i].cells == pb[i].cells;
            for (unsigned k = 0; same && k < pa[i].cells; ++k)
                same = pa[i].cell_key[k] == pb[i].cell_key[k] && pa[i].cell_since[k] == pb[i].cell_since[k];
            cells += pa[i].cells;
        }
        const bool moved = na.patches && nb.patches && (pa[0].centre[2] != pb[0].centre[2]) && (pa[0].axes[0] != pb[0].axes[0]);
        std::printf("GRID patches=%u moved_patches=%u cells=%u identical=%u owner_moved=%u\n", na.patches, nb.patches, cells,
                    unsigned(same), unsigned(moved));
        report("grid_fixed_on_owner", same && moved, "");
    }
    // 12 hits per second on one owner through the production builder, frames of 1/60 s game time for 4 s (the cell
    // maps in steady state, older hits' cells fading on their own clocks), the uniform-level look: between consecutive
    // frames no pixel drops by more than one frame's fade step (the fastest cell: cell_gain (1 + stagger / 2) dt / (fade
    // (1 - stagger / 2))) plus the FP16 step. Fighter: hits within two cells of a point on the owner's front (one map).
    // Capital spread: a 400 m owner, hits spread over its front face (within 35 degrees of the axis, one anchor
    // region): its lit cells overflow one map, so the region spills into further draws and nothing is dropped. The
    // witness: the same capital fire with one draw per region (Tuning::region_draws 1), the cells with the oldest
    // clocks evicted; its pops are recorded, not gated.
    {
        struct NoPop {
            unsigned hits = 0, frames = 0, max_live = 0, max_patches = 0, max_cells = 0, max_region_cells = 0, popped = 0;
            unsigned spills = 0, cells_dropped = 0, hits_dropped = 0, dropped = 0, worst_frame = 0, worst_x = 0, worst_y = 0;
            double worst = 0, worst_rel = 0, allowed = 0;
            float worst_prev = 0.f, worst_cur = 0.f;
        };
        auto run_nopop = [&](const char* name, std::uint8_t tier, float owner_r, float owner_z, double spread_rad,
                             const ws::Tuning& tuning) {
            NoPop o;
            stage_owner(owner_z, owner_r, 0);
            wc::ring_clear(stage_ring);
            rr::WeaponPatch patches[wc::max_patches];
            rr::WeaponDecal decals[wc::max_decals];
            const double dt = 1.0 / 60.0, fade = double(wc::fade_ms) * 1e-3;
            o.allowed = double(wc::cell_gain) * (1.0 + 0.5 * wc::stagger) * dt / (fade * (1.0 - 0.5 * wc::stagger));
            const double r_engine = double(owner_r) / double(wc::world_scale);
            std::vector<float> prev;
            for (unsigned f = 0; f <= 240; ++f) {
                const std::uint32_t now = std::uint32_t(double(f) * dt * 1000.0 + 0.5);
                while (o.hits * 1000u / 12u <= now) {
                    const double th = spread_rad * std::sqrt(halton(o.hits + 1, 2)), a = 6.2831853 * halton(o.hits + 1, 3);
                    const std::int32_t pos[3] = {std::int32_t(r_engine * std::sin(th) * std::cos(a)),
                                                 std::int32_t(r_engine * std::sin(th) * std::sin(a)),
                                                 -std::int32_t(r_engine * std::cos(th))};
                    add_stage_hit(o.hits * 1000u / 12u, pos, tier);
                    ++o.hits;
                }
                const ws::Counts n = ws::build(stage_ring, now, &fake_read, identity_camera(), tuning, patches,
                                               wc::max_patches, decals, wc::max_decals);
                unsigned cells = 0;
                for (unsigned i = 0; i < n.patches; ++i) {
                    cells += patches[i].cells;
                    o.max_cells = std::max(o.max_cells, patches[i].cells);
                }
                o.max_region_cells = std::max(o.max_region_cells, cells);
                o.max_patches = std::max(o.max_patches, n.patches);
                o.max_live = std::max(o.max_live, n.hits);
                o.spills += n.spills;
                o.cells_dropped += n.cells_dropped;
                o.hits_dropped += n.hits_dropped;
                o.dropped += n.dropped;
                rr::WeaponEffectsFrame fr = base_frame(g);
                fr.patches = patches;
                fr.patch_count = n.patches;
                fr.look = uniform;
                draw(pass, fr);
                std::vector<float> l = read_luma(g, g.scene_s.p);
                if (!prev.empty() && l.size() == prev.size()) {
                    for (std::size_t i = 0; i < l.size(); ++i) {
                        const double drop = double(prev[i]) - double(l[i]);
                        const double over = drop - (o.allowed + 2e-3 * double(prev[i]) + 1e-3);
                        if (drop > o.worst) {
                            o.worst = drop;
                            o.worst_frame = f;
                            o.worst_x = unsigned(i % g.w);
                            o.worst_y = unsigned(i / g.w);
                            o.worst_prev = prev[i];
                            o.worst_cur = l[i];
                        }
                        if (over > o.worst_rel) o.worst_rel = over;
                        o.popped += over > 0.0;
                    }
                    ++o.frames;
                }
                prev.swap(l);
            }
            std::printf("NOPOP case=%s hits=%u frames=%u max_live_hits=%u max_patches=%u max_cells=%u max_region_cells=%u spills=%u cells_dropped=%u hits_dropped=%u dropped=%u max_drop=%.5f allowed=%.5f excess=%.5f popped_pixels=%u worst_frame=%u worst_x=%u worst_y=%u worst_prev=%.4f worst_cur=%.4f\n",
                        name, o.hits, o.frames, o.max_live, o.max_patches, o.max_cells, o.max_region_cells, o.spills,
                        o.cells_dropped, o.hits_dropped, o.dropped, o.worst, o.allowed, o.worst_rel, o.popped,
                        o.worst_frame, o.worst_x, o.worst_y, double(o.worst_prev), double(o.worst_cur));
            return o;
        };
        const NoPop fighter_fire = run_nopop("fighter_12hps", wc::patch_fighter, 100.f, 400.f, 0.15, ws::Tuning{});
        report("no_pop_at_12_hits_per_second", fighter_fire.frames == 240 && fighter_fire.worst_rel <= 0.0 &&
                                                   fighter_fire.max_live >= 30 && fighter_fire.max_patches == 1 &&
                                                   fighter_fire.cells_dropped == 0, "");
        const NoPop capital_fire = run_nopop("capital_spread_12hps", wc::patch_capital, 2000.f, 4000.f, 0.61, ws::Tuning{});
        report("no_pop_spread_capital_map_overflows", capital_fire.frames == 240 && capital_fire.worst_rel <= 0.0 &&
                                                          capital_fire.max_live >= 30 &&
                                                          capital_fire.max_region_cells > rr::weapon_patch_cells &&
                                                          capital_fire.spills > 0 && capital_fire.cells_dropped == 0 &&
                                                          capital_fire.dropped == 0, "");
        ws::Tuning one_draw{};
        one_draw.region_draws = 1;
        const NoPop witness = run_nopop("capital_spread_one_draw_witness", wc::patch_capital, 2000.f, 4000.f, 0.61, one_draw);
        report("one_draw_evicts_the_oldest_clocks", witness.cells_dropped > 0 && witness.spills == 0 &&
                                                        witness.max_cells == rr::weapon_patch_cells, "");
    }
    // A region seam (through the production builder): the owner turned 45 degrees about y, so the edge between its -z
    // and -x regions faces the camera; two hits half a cell either side of the seam (ages 0.5 s, cells only), each
    // footprint crossing it. Two draws (one per region) that name each other as a seam; the pair's peak never exceeds
    // the brighter single hit's (the light is shared across the seam, not added); the same pair with its seams cleared
    // is the witness of the two lattices adding.
    {
        rr::WeaponPatch patches[wc::max_patches];
        rr::WeaponDecal decals[wc::max_decals];
        const float r = 100.f;
        const double r_engine = double(r) / double(wc::world_scale);
        auto turned_owner = [&]() {
            stage_owner(400.f, r, 0);
            wc::ring_clear(stage_ring);
            const std::int32_t c = 46341; // 0.7071 x 65536
            const std::int32_t rows[3][3] = {{c, 0, c}, {0, 65536, 0}, {-c, 0, c}};
            for (unsigned k = 0; k < 3; ++k)
                for (unsigned j = 0; j < 3; ++j)
                    put32(fake_node, wc::layout::node_rot + 16 * k + 4 * j, std::uint32_t(rows[k][j]));
        };
        const double half_cell = 0.5 * double(ws::metres_to_view(wc::cell_m[wc::patch_fighter])) / double(r);
        auto hit_at = [&](double phi) {
            const std::int32_t pos[3] = {std::int32_t(-r_engine * std::sin(phi)), 0, std::int32_t(-r_engine * std::cos(phi))};
            add_stage_hit(1000, pos);
        };
        const double seam = 0.78539816;
        auto peak_of = [&](std::initializer_list<double> phis, bool clear_seams, unsigned* drawn, unsigned* seams) {
            turned_owner();
            for (double phi : phis) hit_at(phi);
            const ws::Counts n = ws::build(stage_ring, 1500, &fake_read, identity_camera(), ws::Tuning{}, patches,
                                           wc::max_patches, decals, wc::max_decals);
            unsigned bits = 0;
            for (unsigned i = 0; i < n.patches; ++i) {
                bits |= patches[i].seams;
                if (clear_seams) patches[i].seams = 0;
            }
            if (drawn) *drawn = n.patches;
            if (seams) *seams = bits;
            const std::vector<float> l = render(pass, patches, n.patches, &cells_only);
            return l.empty() ? 0.0 : double(*std::max_element(l.begin(), l.end()));
        };
        unsigned pair_draws = 0, pair_seams = 0, a_draws = 0;
        const double peak_a = peak_of({seam - half_cell}, false, &a_draws, nullptr);
        const double peak_b = peak_of({seam + half_cell}, false, nullptr, nullptr);
        const double peak_pair = peak_of({seam - half_cell, seam + half_cell}, false, &pair_draws, &pair_seams);
        const double peak_added = peak_of({seam - half_cell, seam + half_cell}, true, nullptr, nullptr);
        const double single = std::max(peak_a, peak_b);
        std::printf("SEAM single_draws=%u pair_draws=%u seams=0x%02x peak_a=%.4f peak_b=%.4f peak_pair=%.4f peak_without_sharing=%.4f\n",
                    a_draws, pair_draws, pair_seams, peak_a, peak_b, peak_pair, peak_added);
        report("seam_pair_never_above_one_hit", a_draws == 1 && pair_draws == 2 && pair_seams == ((1u << 1) | (1u << 3)) &&
                                                    single > 0.0 && peak_pair <= single * (1.0 + 2e-3) + 1e-3, "");
    }
    // The stage's CPU cost in this i686 build (SSE2, the production flags): ws::build alone, best and mean of 200 calls,
    // on the worst cases: 64 live hits on one capital owner spread over its front (the region spills), and 16 capital
    // owners with 16 spread hits each (the ring's 256), against the 0.2 ms stage_us target (which also holds the draws).
    {
        struct Fake {
            unsigned char obj[0x100], node[0x200];
        };
        static Fake owners[wc::max_owners];
        static wc::Ring ring;
        auto reader = [](std::uintptr_t a, void* out, std::size_t n) {
            const std::uintptr_t b = reinterpret_cast<std::uintptr_t>(&owners[0]), size = sizeof owners;
            if (a < b || n > size || a - b > size - n) return false;
            std::memcpy(out, reinterpret_cast<const void*>(a), n);
            return true;
        };
        const auto engine = [](float v) { return std::uint32_t(std::int32_t(v / wc::world_scale)); };
        auto make_owner = [&](unsigned k, float x, float z, float radius) {
            Fake& o = owners[k];
            std::memset(&o, 0, sizeof o);
            put32(o.obj, wc::layout::obj_node, addr(o.node));
            put32(o.obj, wc::layout::obj_radius, engine(radius));
            put32(o.node, wc::layout::node_handle, 0xe000u + k);
            put32(o.node, wc::layout::node_pos + 0, engine(x));
            put32(o.node, wc::layout::node_pos + 8, engine(z));
            for (unsigned i = 0; i < 3; ++i) put32(o.node, wc::layout::node_rot + 16 * i + 4 * i, 0x10000u);
        };
        auto add_hit = [&](unsigned k, unsigned i, std::uint32_t t0, float radius) {
            wc::Event* e = wc::ring_acquire(ring);
            if (!e) return;
            const double r_engine = double(radius) / double(wc::world_scale);
            const double th = 0.61 * std::sqrt(halton(i + 1, 2)), a = 6.2831853 * halton(i + 1, 3);
            e->role = wc::role_shield_hit;
            e->style = wc::shape_slug;
            e->flags = std::uint8_t(wc::flag_stage | wc::flag_forwarded | wc::flag_shield_read);
            e->shield_q = 0xffff;
            e->patch_tier = wc::patch_capital;
            e->t0_game_ms = t0;
            e->lifetime_ms = std::uint16_t(wc::patch_lifetime_ms);
            e->owner = addr(owners[k].obj);
            e->node = addr(owners[k].node);
            e->handle = 0xe000u + k;
            e->pos[0] = std::int32_t(r_engine * std::sin(th) * std::cos(a));
            e->pos[1] = std::int32_t(r_engine * std::sin(th) * std::sin(a));
            e->pos[2] = -std::int32_t(r_engine * std::cos(th));
            e->radius = std::int32_t(engine(radius));
        };
        rr::WeaponPatch patches[wc::max_patches];
        rr::WeaponDecal decals[wc::max_decals];
        LARGE_INTEGER freq, a, b;
        QueryPerformanceFrequency(&freq);
        auto time_build = [&](const char* name, std::uint32_t now) {
            double best = 1e30, sum = 0;
            ws::Counts n{};
            for (unsigned rep = 0; rep < 200; ++rep) {
                QueryPerformanceCounter(&a);
                n = ws::build(ring, now, reader, identity_camera(), ws::Tuning{}, patches, wc::max_patches, decals,
                              wc::max_decals);
                QueryPerformanceCounter(&b);
                const double us = ticks_us(a, b, freq);
                best = std::min(best, us);
                sum += us;
            }
            unsigned cells = 0;
            for (unsigned i = 0; i < n.patches; ++i) cells += patches[i].cells;
            std::printf("STAGE_CPU case=%s owners=%u hits=%u patches=%u cells=%u spills=%u cells_dropped=%u dropped=%u best_us=%.1f mean_us=%.1f target_us=200\n",
                        name, n.owners, n.hits, n.patches, cells, n.spills, n.cells_dropped, n.dropped, best, sum / 200.0);
            return best;
        };
        wc::ring_clear(ring);
        make_owner(0, 0.f, 4000.f, 2000.f);
        for (unsigned i = 0; i < wc::owner_hits; ++i) add_hit(0, i, 1000 + 40 * i, 2000.f); // 25 hits/s over 2.5 s
        const double one = time_build("one_capital_64_hits", 1000 + 40 * wc::owner_hits);
        wc::ring_clear(ring);
        for (unsigned k = 0; k < wc::max_owners; ++k) {
            make_owner(k, (float(k) - 7.5f) * 5000.f, 20000.f, 2000.f);
            for (unsigned i = 0; i < 16; ++i) add_hit(k, 16 * k + i, 1000 + 150 * i, 2000.f);
        }
        const double many = time_build("sixteen_capitals_16_hits", 1000 + 150 * 16);
        report("stage_cpu_measured", one > 0.0 && many > 0.0, "");
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
    // until the builder has nothing to draw: the last lit cell's game ms end after a sixth of them in wall time.
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
        // The patch's last lit game ms: the outermost footprint cells light at rings / ((rings + 0.5) ripple_speed) and
        // the slowest fade lasts fade (1 + stagger / 2); the record's lifetime bounds it.
        const double rings = double(wc::rings[wc::patch_fighter]);
        const double lit_ms = double(wc::fade_ms) * (1.0 + 0.5 * double(wc::stagger)) +
                              1000.0 * rings / ((rings + 0.5) * double(wc::ripple_speed));
        const double expected = lit_ms / 6.0;
        std::printf("AGE lifetime_ms=%u lit_ms=%.1f seta6_expected_wall_ms=%.2f seta6_wall_ms=%.2f last_drawn_ms=%.2f frames=%u max_frame_ms=%.3f\n",
                    wc::patch_lifetime_ms, lit_ms, expected, wall_ms, last_drawn_ms, frames, max_frame_ms);
        report("age_law_seta6_measured", frames > 0 && last_drawn_ms <= expected + 0.2 && wall_ms >= expected &&
                                             wall_ms <= expected + 0.2 + max_frame_ms + 0.5 &&
                                             lit_ms <= double(wc::patch_lifetime_ms), "");
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
    // Timing, EVENT-fenced (best of 60), over a sky lane: an empty fence (the fence's own round trip); two hits on a
    // fighter (a 20 m owner at 100 m) two cells apart, once in one anchor region (one draw, one cell map) and once as two
    // regions (two draws); the same on a capital at close range (a 400 m owner, the hits 100 m from the camera, three
    // cells apart); the worst one owner can submit at once (that capital hit in four regions: four draws); a fighter
    // under 12 hits per second (36 live hits within two cells: the steady-state map), a capital map of all 128 cells
    // (16 disjoint footprints, the newest 128 cells), the same map with the camera 30 m from the hull so its cap fills
    // the frame, and the capital spread at 12 hits/s through the builder (its spill draws), at 1080p and 5120x1440.
    // Each row carries lit_share (pixels the patches lit) and raster_share (pixels the draws rasterized, summed over
    // the draws: an occlusion query, Z off, so every pixel the caps cover counts).
    for (auto size : {std::pair<UINT, UINT>{1920, 1080}, std::pair<UINT, UINT>{5120, 1440}}) {
        Target tt;
        if (!make_target(tt, size.first, size.second)) { report("timing_target", false, ""); continue; }
        draw_lane(tt, 0.f, -1.f);
        const TierScene fighter_far{wc::patch_fighter, 100.f, 600.f};
        const TierScene capital_close{wc::patch_capital, 2000.f, 2500.f};
        const float cf = planar_cell(fighter_far), cc = planar_cell(capital_close);
        const rr::WeaponPatch fighter_one[1] = {make_patch(fighter_far, {{-cf, 0.f, 0.2f}, {cf, 0.f, 0.5f}})};
        const rr::WeaponPatch fighter_regions[2] = {make_patch(fighter_far, {{-cf, 0.f, 0.2f}}),
                                                    make_patch(fighter_far, {{cf, 0.f, 0.5f}})};
        const rr::WeaponPatch capital_one[1] = {make_patch(capital_close, {{-1.5f * cc, 0.f, 0.2f}, {1.5f * cc, 0.f, 0.5f}})};
        const rr::WeaponPatch capital_regions[2] = {make_patch(capital_close, {{-1.5f * cc, 0.f, 0.2f}}),
                                                    make_patch(capital_close, {{1.5f * cc, 0.f, 0.5f}})};
        rr::WeaponPatch worst[4];
        for (unsigned k = 0; k < 4; ++k)
            worst[k] = make_patch(capital_close, {{(k & 1 ? 1.5f : -1.5f) * cc, (k & 2 ? 1.5f : -1.5f) * cc, 0.1f + 0.1f * k}});
        Hit burst[36], wide[16];
        for (unsigned k = 0; k < 36; ++k) {
            const float r = 2.f * cf * float(std::sqrt(halton(k + 1, 2))), a = 6.2831853f * float(halton(k + 1, 3));
            burst[k] = Hit{r * std::cos(a), r * std::sin(a), float(k) / 12.f};
        }
        for (unsigned k = 0; k < 16; ++k) // 16 disjoint footprints of 19 cells: the map fills at 64
            wide[k] = Hit{(float(k % 4) - 1.5f) * 5.f * cc, (float(k / 4) - 1.5f) * 5.f * 0.8660254f * cc, 0.3f + 0.05f * float(k)};
        const rr::WeaponPatch fighter_burst[1] = {make_patch(fighter_far, burst, 36)};
        const rr::WeaponPatch capital_map[1] = {make_patch(capital_close, wide, 16)};
        const TierScene capital_fill{wc::patch_capital, 2000.f, 2150.f};
        const rr::WeaponPatch capital_fill_map[1] = {make_patch(capital_fill, wide, 16)};
        // The capital spread at 12 hits/s, 2.5 s in (30 live hits within 35 degrees of the front), through the builder.
        rr::WeaponPatch spread[wc::max_patches];
        unsigned spread_count = 0;
        {
            rr::WeaponDecal decals[wc::max_decals];
            stage_owner(4000.f, 2000.f, 0);
            wc::ring_clear(stage_ring);
            for (unsigned i = 0; i * 1000u / 12u <= 2500u; ++i) {
                const double th = 0.61 * std::sqrt(halton(i + 1, 2)), a = 6.2831853 * halton(i + 1, 3), re = 200000.0;
                const std::int32_t pos[3] = {std::int32_t(re * std::sin(th) * std::cos(a)),
                                             std::int32_t(re * std::sin(th) * std::sin(a)), -std::int32_t(re * std::cos(th))};
                add_stage_hit(i * 1000u / 12u, pos, wc::patch_capital);
            }
            spread_count = ws::build(stage_ring, 2500, &fake_read, identity_camera(), ws::Tuning{}, spread, wc::max_patches,
                                     decals, wc::max_decals).patches;
        }
        unsigned spread_cells = 0;
        for (unsigned i = 0; i < spread_count; ++i) spread_cells += spread[i].cells;
        std::printf("TIMING_MAPS fighter_12hps_cells=%u capital_full_map_cells=%u capital_fill_map_cells=%u capital_spread_draws=%u capital_spread_cells=%u\n",
                    fighter_burst[0].cells, capital_map[0].cells, capital_fill_map[0].cells, spread_count, spread_cells);
        struct Case {
            const char* name;
            const rr::WeaponPatch* patches;
            unsigned count;
        } cases[] = {{"fence", nullptr, 0},
                     {"fighter_two_hits_one_region", fighter_one, 1}, {"fighter_two_regions", fighter_regions, 2},
                     {"capital_close_two_hits_one_region", capital_one, 1},
                     {"capital_close_two_regions", capital_regions, 2}, {"worst_four_regions", worst, 4},
                     {"fighter_12hps_map", fighter_burst, 1}, {"capital_close_full_map", capital_map, 1},
                     {"capital_fill_full_map", capital_fill_map, 1}, {"capital_spread_spill", spread, spread_count}};
        Com<IDirect3DQuery9> ev, occlusion;
        dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev.p);
        dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, &occlusion.p);
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
            // Coverage: the share of the frame the patches lit (one readback after the timed runs) and the share the
            // draws rasterized (an occlusion query around the same run).
            dev->BeginScene();
            dev->SetRenderTarget(0, tt.scene_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            DWORD rasterized = 0;
            if (c.count) {
                rr::WeaponEffectsReport r{};
                if (occlusion.p) occlusion.p->Issue(D3DISSUE_BEGIN);
                pass.run(f, &r);
                if (occlusion.p) occlusion.p->Issue(D3DISSUE_END);
            }
            dev->EndScene();
            if (c.count && occlusion.p) {
                const DWORD start = GetTickCount();
                while (occlusion.p->GetData(&rasterized, sizeof rasterized, D3DGETDATA_FLUSH) == S_FALSE &&
                       GetTickCount() - start < 5000) {
                }
            }
            const std::vector<float> l = read_luma(tt, tt.scene_s.p);
            std::printf("TIMING width=%u height=%u case=%s patches=%u drawn=%u lit_share=%.4f raster_share=%.4f gpu_us=%.1f\n",
                        size.first, size.second, c.name, c.count, drawn,
                        l.empty() ? 0.0 : double(lit_count(l)) / double(l.size()),
                        double(rasterized) / (double(size.first) * double(size.second)), best);
        }
        tt.release();
    }

    pass.detach();
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
