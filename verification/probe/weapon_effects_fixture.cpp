// D3D fixture of the weapon-effects shield-hit stage (src/renderer/weapon_effects_pass.cpp, the production
// WeaponEffectsPass and its patch / decal programs; docs/architecture/weapon-effects.md 5.1 acceptance (b) and 5.1.1).
// It creates a plain D3D9 HAL device, attaches the pass, and draws hex patches and flat decals onto an A16B16G16R16F
// scene target with a controlled four-channel depth lane (.b = view depth, a sentinel .r < 0 = sky), built by a small FP
// lane shader compiled through the game's d3dx9_37.dll (argv[1], run_weapon_effects.py's contract): a plane at a depth
// over a share of the screen, or a hull (an ellipsoid of the owner's frame, hull_ps, with its CPU twin hull_point). The
// patch is the screen-space decal (5.1.1, Run236): it lights the lane's surface, never the sky. Patches are built with
// the production ws::fill_patch on an owner whose +z face looks at the camera (its lattice plane square to the view,
// the hit at the plane's origin at the screen centre, the lane that plane). It reads the FP16 target back and reports the structural relations of 5.1.1 (the cell-map model):
// per tier the footprint's cell count 1 + 3 rings (rings + 1), the lit area bounded by hexagon edges (the outermost cell
// centres plus a circumradius) and zero outside, lit on both sides of the hit in both axes, the edge bands across it;
// the hit's centre cell brighter than a ring cell by the marker law; the surface from the lane (whole on it, nothing
// on sky or past the extent along the axis, only the lane's half); the ring moving by whole cells and lighting a cell when the front reaches
// its centre; the patch fading monotonically to zero at the record's lifetime and one cell's own fade duration against
// its hash; the colour bands at shield 0.9 / 0.5 / 0.2; a repeated hit re-lights (the newest clock, not a sum); a second
// hit on the next cell resets only the shared cells and no cell is drawn twice; through the production builder the grid
// fixed on a moved and rotated owner, the placement on hull lanes (the Flight B station at 0.68 R on the hull decal; a
// capital ship without a profile draws nothing) with the normal's error (ddx / ddy against a 4-neighbourhood); the
// ships draw on the lathe of their offline profile (the lathe section; the runtime hull walk and its ellipsoid cases
// were removed on 2026-11-12, user decision FL 69 A), the own ship in the cockpit on its eye sphere sized from the
// profile; 12 hits per second without a pop on a fighter's and a capital's flat face (the capital's cells
// overflow into spill draws) and on a sphere's front; with one draw per face the evicted cells' pops are the recorded
// witness), two hits across a 45-degree face edge never brighter than one hit (the triplanar shares), the stage's CPU cost (with the Flight B station cluster); the decal
// fallback (an owner without an ellipsoid) on a hull and 0 over sky; the game-time law at SETA 6 measured; the marker
// through the production TemporalPass at 0 / 4 / 8 px per frame; a retired owner draws nothing; Reset recovery, the
// fault cases, and the stage cost at 1080p and 5120x1440 (two hits on a fighter, two on a capital at close range, the
// worst case one owner can submit, the 12 hits per second map, a full 128-cell map, one whose cells fill the frame and
// the capital spread's spill draws, each with its lit and rasterized screen shares). Every draw and readback runs inside
// BeginScene/EndScene (native D3D9 refuses a draw outside a scene). Never launches the game.
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/renderer/weapon_effects_pass.h"
#include "../../src/proxy/weapon_effects_stage.h"
#include "../../src/proxy/weapon_projectile_family.h"
#include "../../src/proxy/engine_memory.h"
#include "../../src/renderer/temporal_pass.h"
#include "../../src/renderer/temporal_resolve_program.h"
#include "../../src/renderer/hdr_writeback_program.h"
#include "weapon_bullet_reshade_fixture.h"
#include "weapon_muzzle_reshade_fixture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
// The lost-device guard (2026-11-08: a screensaver lost the device mid-run and 55 blank frames were read as passing
// timing rows): after each section, at each target and before every timing row the device's TestCooperativeLevel;
// anything but D3D_OK prints CHECK device_lost FAIL section=<n> and stops the run with a non-zero exit.
unsigned guard_section = 0;
void device_guard(const char* at) {
    const HRESULT hr = dev ? dev->TestCooperativeLevel() : D3D_OK;
    if (hr == D3D_OK) return;
    std::printf("CHECK device_lost FAIL section=%u at=%s hr=%08lx\nRESULT FAIL checks=%u failures=%u\n", guard_section, at,
                static_cast<unsigned long>(hr), checks + 1, failures + 1);
    std::fflush(stdout);
    std::_Exit(3);
}
void guard_next_section(const char* name) {
    ++guard_section;
    device_guard(name);
}
D3DCAPS9 caps{};
D3DFORMAT adapter_format = D3DFMT_X8R8G8B8;
HMODULE d3dx = nullptr;
using CompileFn = HRESULT(WINAPI*)(LPCSTR, UINT, const D3DXMACRO*, LPD3DXINCLUDE, LPCSTR, LPCSTR, DWORD, LPD3DXBUFFER*,
                                   LPD3DXBUFFER*, LPD3DXCONSTANTTABLE*);
CompileFn compile_shader = nullptr;
IDirect3DPixelShader9* lane_ps = nullptr;
IDirect3DPixelShader9* hull_ps = nullptr;
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
    // The hull lane: the near root of the eye ray (D3D9 pixel centres at integer raster coordinates: ndc = 2 r / size
    // - 1; the fixture's projection has m20 = m21 = 0) against an ellipsoid of half-extents e in the owner's frame (the
    // far root when the eye is inside it): .b = the view depth (= t, the ray's z is 1), .r = 0.5; else the sky
    // sentinel. c0 = (W, H, m00, m11), c1 the owner's centre (view), c2..c4 its axes in view, c5 = 1 / e^2.
    const char* hull = "float4 c0:register(c0); float4 c1:register(c1); float4 c2:register(c2); float4 c3:register(c3);"
                       " float4 c4:register(c4); float4 c5:register(c5); float4 main(float2 vp:VPOS):COLOR0{"
                       " float2 ndc = float2(2.0 * vp.x / c0.x - 1.0, 1.0 - 2.0 * vp.y / c0.y);"
                       " float3 d = float3(ndc.x / c0.z, ndc.y / c0.w, 1.0), o = -c1.xyz;"
                       " float3 od = float3(dot(o, c2.xyz), dot(o, c3.xyz), dot(o, c4.xyz));"
                       " float3 dd = float3(dot(d, c2.xyz), dot(d, c3.xyz), dot(d, c4.xyz));"
                       " float A = dot(dd * dd, c5.xyz), B = dot(od * dd, c5.xyz), C = dot(od * od, c5.xyz) - 1.0;"
                       " float disc = B * B - A * C; if (disc < 0.0) return float4(-1, 0, 0, 0);"
                       " float s = sqrt(disc), t = (-B - s) / A; if (t <= 0.0) t = (-B + s) / A;"
                       " return t > 0.0 ? float4(0.5, 0, t, 0) : float4(-1, 0, 0, 0); }";
    const DWORD* hw = nullptr;
    static Com<ID3DXBuffer> hh;
    if (!compile(hull, "ps_3_0", &hw, &hh.p) || FAILED(dev->CreatePixelShader(hw, &hull_ps))) return false;
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
    device_guard("make_target");
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
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE); // the pass leaves its patches' cull mode set
    dev->SetVertexDeclaration(quad_decl);
    dev->SetVertexShader(quad_vs);
    dev->SetPixelShader(lane_ps);
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
    dev->EndScene();
}
// The hull lane (hull_ps): an ellipsoid of half-extents ext (view units) about centre (view) in the frame of axes
// (the owner's local axes in view, rows), the fixture's projection (m00 1.6, m11 1.6 W / H).
struct HullLane {
    double centre[3], axes[3][3], ext[3];
};
void draw_hull_lane(Target& t, const HullLane& h) {
    const float m00 = 1.6f, m11 = 1.6f * float(t.w) / float(t.h);
    const float c[6][4] = {{float(t.w), float(t.h), m00, m11},
                           {float(h.centre[0]), float(h.centre[1]), float(h.centre[2]), 0.f},
                           {float(h.axes[0][0]), float(h.axes[0][1]), float(h.axes[0][2]), 0.f},
                           {float(h.axes[1][0]), float(h.axes[1][1]), float(h.axes[1][2]), 0.f},
                           {float(h.axes[2][0]), float(h.axes[2][1]), float(h.axes[2][2]), 0.f},
                           {float(1.0 / (h.ext[0] * h.ext[0])), float(1.0 / (h.ext[1] * h.ext[1])),
                            float(1.0 / (h.ext[2] * h.ext[2])), 0.f}};
    const float quad[4][4] = {{-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
    dev->BeginScene();
    dev->SetRenderTarget(0, t.lane_s.p);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetVertexDeclaration(quad_decl);
    dev->SetVertexShader(quad_vs);
    dev->SetPixelShader(hull_ps);
    dev->SetPixelShaderConstantF(0, &c[0][0], 6);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
    dev->EndScene();
}
// Its CPU twin in double: the surface point under pixel (x, y) in the owner's frame (view units); false for sky.
bool hull_point(const Target& t, const HullLane& h, unsigned x, unsigned y, double p[3]) {
    const double m00 = 1.6, m11 = 1.6 * double(t.w) / double(t.h);
    const double d[3] = {(2.0 * x / t.w - 1.0) / m00, (1.0 - 2.0 * y / t.h) / m11, 1.0};
    double od[3], dd[3], A = 0, B = 0, C = -1;
    for (unsigned k = 0; k < 3; ++k) {
        od[k] = -(h.centre[0] * h.axes[k][0] + h.centre[1] * h.axes[k][1] + h.centre[2] * h.axes[k][2]);
        dd[k] = d[0] * h.axes[k][0] + d[1] * h.axes[k][1] + d[2] * h.axes[k][2];
        const double w = 1.0 / (h.ext[k] * h.ext[k]);
        A += dd[k] * dd[k] * w;
        B += od[k] * dd[k] * w;
        C += od[k] * od[k] * w;
    }
    const double disc = B * B - A * C;
    if (disc < 0.0) return false;
    double tt = (-B - std::sqrt(disc)) / A;
    if (tt <= 0.0) tt = (-B + std::sqrt(disc)) / A;
    if (!(tt > 0.0)) return false;
    for (unsigned k = 0; k < 3; ++k) p[k] = od[k] + tt * dd[k];
    return true;
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
// The owner's view frame: its local x -> +x, y -> -y, z -> -z (det +1), so the +z region (anchor0: a = +z, u = +x,
// v = +y) faces the eye with its plane square to the view.
void facing_frame(float frame[9]) {
    const float f[9] = {1.f, 0.f, 0.f, 0.f, -1.f, 0.f, 0.f, 0.f, -1.f};
    std::memcpy(frame, f, sizeof f);
}
struct Hit {
    float qx, qy, age;
};
// A patch of hits at plane points (qx, qy) of the +z region's lattice on the owner's front face at depth S (view z
// centre_z - S), hits newest first (the stage's order).
rr::WeaponPatch make_patch(const TierScene& s, const Hit* hits, unsigned count, float shield = 1.f, float cx = 0.f,
                           const ws::Tuning& t = ws::Tuning{}) {
    rr::WeaponPatch p{};
    float frame[9], centre[3] = {cx, 0.f, s.centre_z};
    facing_frame(frame);
    float points[wc::owner_hits][3], ages[wc::owner_hits], colour[3];
    unsigned n = 0;
    for (unsigned k = 0; k < count; ++k) {
        const Hit& h = hits[k];
        if (n >= wc::owner_hits) break;
        points[n][0] = h.qx;
        points[n][1] = h.qy;
        points[n][2] = s.surface;
        ages[n++] = h.age;
    }
    wc::shield_colour(shield, t.bands, colour);
    ws::fill_patch(p, centre, frame, s.tier, anchor0, points, ages, n, colour, t);
    return p;
}
rr::WeaponPatch make_patch(const TierScene& s, std::initializer_list<Hit> hits, float shield = 1.f, float cx = 0.f,
                           const ws::Tuning& t = ws::Tuning{}) {
    return make_patch(s, hits.begin(), unsigned(hits.size()), shield, cx, t);
}
// Screen x (px) of the plane point (q, 0) on the scanline through the hit (the plane at view z centre_z - S).
double screen_x(const Target& t, const TierScene& s, double q) {
    return double(t.w) * 0.5 * (1.0 + 1.6 * q / (double(s.centre_z) - double(s.surface)));
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
// The plane a make_patch patch lies on (square to the view at its hits' depth), as the lane: the surface the
// screen-space patch lights.
void plane_lane(Target& t, const rr::WeaponPatch& p) {
    draw_lane(t, p.centre[2] + p.view_a[2] * p.cell_anchor[0], 1.f);
}
// Draw one frame of patches on a surface (the hull lane, else the first patch's plane) and read the luma back.
std::vector<float> render(rr::WeaponEffectsPass& pass, const rr::WeaponPatch* p, unsigned n,
                          const rr::WeaponLook* look = nullptr, std::vector<float>* rgb = nullptr,
                          const HullLane* hull = nullptr, bool keep_lane = false) {
    if (keep_lane) {
    } else if (hull)
        draw_hull_lane(g, *hull);
    else if (n)
        plane_lane(g, p[0]);
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
// The staged owner's hull (identity camera: view = world): an ellipsoid of half-extents ext (view units) about its node
// position in the frame of its rows.
HullLane staged_hull(const double ext[3], const double offset[3] = nullptr) {
    HullLane h{};
    std::int32_t pos[3], rows[12];
    std::memcpy(pos, fake_node + wc::layout::node_pos, 12);
    std::memcpy(rows, fake_node + wc::layout::node_rot, 48);
    for (unsigned k = 0; k < 3; ++k) {
        h.centre[k] = double(pos[k]) * double(wc::world_scale);
        for (unsigned j = 0; j < 3; ++j) h.axes[k][j] = double(rows[4 * k + j]) / 65536.0;
        h.ext[k] = ext[k];
    }
    if (offset) // the hull's centre off the owner's origin (owner frame)
        for (unsigned j = 0; j < 3; ++j)
            for (unsigned k = 0; k < 3; ++k) h.centre[j] += h.axes[k][j] * offset[k];
    return h;
}
// A live owner at view (0, 0, z) with radius r (view units; identity camera, identity rows), and the ring holding one
// stage-owned shield hit on its front (t0 game ms, the header's patch lifetime, a fighter, full shields).
wc::Ring stage_ring;
// The staged records' TierSource: the station's (the hull decal) unless a case stages a ship (the lathe of its profile:
// stage_lathe_type; a ship without one draws nothing since the walk's removal, 2026-11-12).
std::uint8_t stage_tier_source = wc::tier_by_station;
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
    e->tier_source = stage_tier_source;
    e->t0_game_ms = t0;
    e->lifetime_ms = std::uint16_t(wc::patch_lifetime_ms);
    e->damage = 1200;
    e->tint_rgb = 0xffffffu | wc::rgb_present;
    e->alt_rgb332 = 0;
    e->dir[0] = e->dir[1] = e->dir[2] = 0; // no travel: the radial region
    e->owner = addr(fake_obj);
    e->node = addr(fake_node);
    e->handle = 0xd00du;
    e->pos[0] = e->pos[1] = 0; // the slot may carry an earlier case's point
    e->pos[2] = -std::int32_t(engine(r));
    e->radius = std::int32_t(engine(r));
}
ws::Camera identity_camera() {
    ws::Camera c;
    c.r[0] = c.r[4] = c.r[8] = 1.f;
    c.m00 = 1.6f; // base_frame's projection at 16:9 (the stage's screen-area budget)
    c.m11 = 1.6f * 16.f / 9.f;
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
    e->tier_source = stage_tier_source;
    e->t0_game_ms = t0;
    e->lifetime_ms = std::uint16_t(wc::patch_lifetime_ms);
    e->damage = 1200;
    e->tint_rgb = 0xffffffu | wc::rgb_present;
    e->alt_rgb332 = 0;
    e->dir[0] = e->dir[1] = e->dir[2] = 0; // no travel: the radial region
    e->owner = addr(fake_obj);
    e->node = addr(fake_node);
    e->handle = 0xd00du;
    for (unsigned k = 0; k < 3; ++k) e->pos[k] = pos[k];
    std::int32_t r = 0;
    std::memcpy(&r, fake_obj + wc::layout::obj_radius, 4);
    e->radius = r;
}
// ---- the lathe bubble (5.1.1 "Placement", 2026-11-12; src/proxy/weapon_effects_lathe.h)
// The Argon Titan's offline profile (FL TShips row 11, ships\argon\argon_M2_scene, OBJ_SHIP_M2 = 2022): the derived
// figures of tools/effects/ship_profiles.py (verification/results/ship-profiles/titan_profile.json), scene units =
// owner-frame engine units (cx, cy, hx, hy per slice of (z1 - z0) / 32).
constexpr float titan_z0 = -511093.f, titan_z1 = 545751.f;
constexpr float titan_slices[32][4] = {
    {-1068.358f, 15785.811f, 9857.115f, 7142.189f}, {2930.93f, -20302.835f, 13856.403f, 44709.778f},
    {0.f, -23826.401f, 71471.14f, 54006.509f}, {0.f, -30591.325f, 81856.386f, 56385.117f},
    {0.f, -38473.72f, 73954.568f, 68902.314f}, {0.f, -41831.258f, 70882.535f, 66463.967f},
    {0.f, -46493.985f, 70471.318f, 78198.015f}, {0.f, -50208.798f, 75817.14f, 78534.398f},
    {0.f, -51001.603f, 72164.565f, 77690.397f}, {0.f, -49760.386f, 74123.893f, 74239.896f},
    {0.f, -7244.677f, 65996.308f, 61944.611f}, {0.f, 23878.811f, 80832.375f, 142494.778f},
    {0.f, 7071.321f, 82686.884f, 125663.099f}, {0.f, -26221.136f, 80679.177f, 92394.832f},
    {0.f, -53603.355f, 60287.648f, 93443.032f}, {0.f, -71358.257f, 78985.93f, 77816.783f},
    {0.f, -56400.1f, 151432.702f, 108901.1f}, {0.f, -58760.154f, 85355.763f, 107613.436f},
    {0.f, -57106.763f, 67399.284f, 106839.839f}, {0.f, -53405.809f, 43742.208f, 103219.516f},
    {0.f, -40904.004f, 43508.379f, 93418.843f}, {0.f, -33743.989f, 46209.511f, 87516.669f},
    {0.f, 19548.937f, 46225.637f, 144292.845f}, {0.f, -3584.039f, 57272.056f, 133786.652f},
    {0.f, -39875.961f, 53740.427f, 95341.887f}, {0.f, -40089.633f, 68007.648f, 83412.561f},
    {0.f, -42818.985f, 68161.594f, 75635.72f}, {0.f, -38505.816f, 57312.371f, 70224.816f},
    {0.f, -30655.83f, 58618.59f, 42065.087f}, {931.286f, -37295.776f, 18347.538f, 27257.241f},
    {2011.738f, -32853.019f, 19492.495f, 25870.392f}, {975.633f, -13445.186f, 3757.395f, 2850.299f}};
constexpr std::int32_t titan_row = 11, titan_class = 2022, titan_radius = 545751;
wc::ShipProfile titan_profile() {
    wc::ShipProfile p;
    p.row = titan_row;
    p.class_id = titan_class;
    p.z0 = titan_z0;
    p.z1 = titan_z1;
    std::memcpy(p.slice, titan_slices, sizeof p.slice);
    return p;
}
// The installed Titan profile since FL run 70 (the hull rules: masts dropped, extents trimmed, widths eased; z0 != -z1):
// verification/results/ship-profiles/titan_profile_smoothed.json, the weapon_lathe row's hull_lo_m -302.9,-354.4,-955.8
// hull_hi_m 302.9,332.7,1001.2 of the FL 70 A log.
constexpr float titan_trim_z0 = -477889.237f, titan_trim_z1 = 500605.816f;
constexpr float titan_trim_slices[32][4] = {
    {2930.93f, -20302.835f, 33061.315f, 47808.688f}, {0.0f, -23826.401f, 71471.14f, 54808.785f},
    {0.0f, -30337.338f, 81509.674f, 62060.182f}, {0.0f, -43790.587f, 81856.386f, 67918.009f},
    {0.0f, -41831.258f, 79765.361f, 72225.793f}, {0.0f, -42000.583f, 77752.279f, 75700.981f},
    {0.0f, -48183.2f, 75739.197f, 77029.575f}, {0.0f, -51853.666f, 75817.14f, 77423.441f},
    {0.0f, -51001.603f, 75252.724f, 77690.397f}, {0.0f, -37457.037f, 77542.639f, 99291.858f},
    {0.0f, -7337.403f, 79832.553f, 120893.318f}, {0.0f, 23878.811f, 82686.884f, 142494.778f},
    {0.0f, -23012.03f, 82017.648f, 126856.435f}, {0.0f, -26221.136f, 81348.412f, 110505.853f},
    {0.0f, -53603.355f, 104263.685f, 99307.96f}, {0.0f, -73430.468f, 127848.194f, 103748.411f},
    {0.0f, -56400.1f, 151432.702f, 108901.1f}, {0.0f, -61118.145f, 130154.235f, 107685.881f},
    {0.0f, -65766.511f, 102341.985f, 105778.582f}, {0.0f, -53446.125f, 66445.153f, 103871.282f},
    {0.0f, -42540.809f, 52654.599f, 110117.481f}, {0.0f, -38360.102f, 45397.827f, 125012.677f},
    {0.0f, 4757.217f, 49907.776f, 139907.874f}, {0.0f, 15976.993f, 53589.916f, 147864.79f},
    {0.0f, -15763.321f, 57642.958f, 139112.316f}, {0.0f, -39875.961f, 58013.859f, 121604.682f},
    {0.0f, -40089.633f, 58384.761f, 100120.606f}, {0.0f, -44673.494f, 58462.704f, 84178.553f},
    {0.0f, -37956.948f, 58540.647f, 75989.152f}, {0.0f, -30655.83f, 58618.59f, 62206.661f},
    {931.286f, -37267.555f, 45576.559f, 46236.387f}, {2011.738f, -32853.019f, 19492.495f, 26342.082f}};
wc::ShipProfile titan_trimmed_profile() {
    wc::ShipProfile p = titan_profile();
    p.z0 = titan_trim_z0;
    p.z1 = titan_trim_z1;
    std::memcpy(p.slice, titan_trim_slices, sizeof p.slice);
    return p;
}
// The profiles block (one entry, the Titan) and a TShips type table of twelve rows with its slots after it (table
// pointer, count), read through lathe_read beside the fake owner.
wc::ShipProfiles lathe_profiles;
constexpr unsigned type_rows = 12, type_slots_at = type_rows * wc::layout::type_stride;
alignas(16) unsigned char fake_types[type_slots_at + 16];
bool lathe_read(std::uintptr_t a, void* out, std::size_t n) {
    if (fake_read(a, out, n)) return true;
    const std::uintptr_t b = reinterpret_cast<std::uintptr_t>(fake_types);
    if (a < b || n > sizeof fake_types || a - b > sizeof fake_types - n) return false;
    std::memcpy(out, reinterpret_cast<const void*>(a), n);
    return true;
}
// The staged owner as a TShips row-11 ship of class `class_id` (the type record's +0xc8; 2022 the profile's), unit
// root axis scales, and the profiles block holding the Titan's (none when `profile` is false). After stage_owner.
void stage_lathe_type(std::int32_t class_id = titan_class, bool profile = true) {
    std::memset(fake_types, 0, sizeof fake_types);
    put32(fake_types, type_slots_at, addr(fake_types));
    put32(fake_types, type_slots_at + 4, type_rows);
    put32(fake_types, unsigned(titan_row) * wc::layout::type_stride + wc::layout::ship_class_id, std::uint32_t(class_id));
    const std::uint16_t cls = wc::layout::class_ship, sub = std::uint16_t(titan_row);
    std::memcpy(fake_obj + wc::layout::obj_class, &cls, 2);
    std::memcpy(fake_obj + wc::layout::obj_subtype, &sub, 2);
    for (unsigned k = 0; k < 3; ++k) put32(fake_node, wc::layout::node_axis_scale + 4 * k, 0x10000);
    lathe_profiles.clear();
    if (profile) {
        lathe_profiles.entries[0] = titan_profile();
        lathe_profiles.slot_of[titan_row] = 1;
        lathe_profiles.count = 1;
        lathe_profiles.tships_rows = type_rows; // the generator's TShips row count: the live count (not stale)
    }
}
ws::Tuning lathe_tuning() {
    ws::Tuning t{};
    t.profiles = &lathe_profiles;
    t.ship_table_slot = addr(fake_types) + type_slots_at;
    t.ship_count_slot = addr(fake_types) + type_slots_at + 4;
    return t;
}
// The visible hull of a profile (the lane's surface): rings at the 32 slice centres with the slices' ellipses
// (uninflated), closed at z0 and z1 by the end slices' ellipses and a point each, 24 segments, in view space through the
// staged owner's node and rows (identity camera).
struct LatheHullLane {
    std::vector<float> v;              // view xyz per vertex
    std::vector<std::uint16_t> idx;    // triangle list
};
// A point of the profile's surface (scene units): slice i's centre z (or z0 / z1 for i = -1 / 32) at angle phi (the
// lathe's: x = cx + hx sin phi, y = cy + hy cos phi).
void profile_point(const wc::ShipProfile& p, int i, double phi, double out[3]) {
    const int s = i < 0 ? 0 : i > 31 ? 31 : i;
    const double dz = (double(p.z1) - double(p.z0)) / 32.0;
    const double z = i < 0 ? double(p.z0) : i > 31 ? double(p.z1) : double(p.z0) + (s + 0.5) * dz;
    out[0] = double(p.slice[s][0]) + double(p.slice[s][2]) * std::sin(phi);
    out[1] = double(p.slice[s][1]) + double(p.slice[s][3]) * std::cos(phi);
    out[2] = z;
}
// Owner frame (view units) to view through the staged node (identity camera).
void owner_to_view(const double p[3], double out[3]) {
    std::int32_t pos[3], rows[12];
    std::memcpy(pos, fake_node + wc::layout::node_pos, 12);
    std::memcpy(rows, fake_node + wc::layout::node_rot, 48);
    for (unsigned j = 0; j < 3; ++j) {
        out[j] = double(pos[j]) * double(wc::world_scale);
        for (unsigned k = 0; k < 3; ++k) out[j] += double(rows[4 * k + j]) / 65536.0 * p[k];
    }
}
LatheHullLane lathe_hull_lane(const wc::ShipProfile& p) {
    LatheHullLane h;
    const unsigned rings = 36, cols = 25;
    const double pi = 3.14159265358979;
    for (unsigned r = 0; r < rings; ++r)
        for (unsigned j = 0; j < cols; ++j) {
            double q[3], v[3];
            const int i = r == 0 || r == 1 ? -1 : r >= rings - 2 ? 32 : int(r) - 2;
            profile_point(p, i, -pi + 2.0 * pi * double(j) / 24.0, q);
            if (r == 0 || r == rings - 1) { // the end's centre point
                const int s = r ? 31 : 0;
                q[0] = p.slice[s][0];
                q[1] = p.slice[s][1];
            }
            for (double& x : q) x *= double(wc::world_scale);
            owner_to_view(q, v);
            for (double x : v) h.v.push_back(float(x));
        }
    for (unsigned r = 0; r + 1 < rings; ++r)
        for (unsigned j = 0; j < 24; ++j) {
            const std::uint16_t a = std::uint16_t(r * cols + j), b = std::uint16_t(a + cols);
            for (std::uint16_t x : {a, b, std::uint16_t(a + 1), std::uint16_t(a + 1), b, std::uint16_t(b + 1)})
                h.idx.push_back(x);
        }
    return h;
}
// Draws the hull into the lane (sky elsewhere): its triangles depth-tested on a scratch depth surface, .r = 0.5 and .b
// the view depth where the hull is nearest (the fixture's projection: m00 1.6, m11 1.6 W / H, near 6).
void draw_lathe_hull(Target& t, const LatheHullLane& h) {
    static IDirect3DVertexShader9* vs = nullptr;
    static IDirect3DPixelShader9* ps = nullptr;
    static IDirect3DVertexDeclaration9* decl = nullptr;
    if (!vs) {
        const char* vsrc = "float4 c0:register(c0); struct O{ float4 p:POSITION; float z:TEXCOORD0; };"
                           " O main(float3 v:POSITION){ O o; o.p = float4(c0.x * v.x, c0.y * v.y, (v.z - c0.z) * c0.w, v.z);"
                           " o.z = v.z; return o; }";
        const char* psrc = "float4 main(float z:TEXCOORD0):COLOR0{ return float4(0.5, 0, z, 0); }";
        const DWORD *vw = nullptr, *pw = nullptr;
        static Com<ID3DXBuffer> vh, ph;
        const D3DVERTEXELEMENT9 e[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                       D3DDECL_END()};
        if (!compile(vsrc, "vs_3_0", &vw, &vh.p) || !compile(psrc, "ps_3_0", &pw, &ph.p) ||
            FAILED(dev->CreateVertexShader(vw, &vs)) || FAILED(dev->CreatePixelShader(pw, &ps)) ||
            FAILED(dev->CreateVertexDeclaration(e, &decl)))
            return;
    }
    draw_lane(t, 0.f, -1.f);
    Com<IDirect3DSurface9> ds, old;
    if (FAILED(dev->CreateDepthStencilSurface(t.w, t.h, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &ds.p, nullptr))) return;
    dev->GetDepthStencilSurface(&old.p);
    const float n = 6.f, f = 1e6f;
    const float c0[4] = {1.6f, 1.6f * float(t.w) / float(t.h), n, f / (f - n)};
    dev->BeginScene();
    dev->SetRenderTarget(0, t.lane_s.p);
    dev->SetDepthStencilSurface(ds.p);
    dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.f, 0);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetVertexDeclaration(decl);
    dev->SetVertexShader(vs);
    dev->SetPixelShader(ps);
    dev->SetVertexShaderConstantF(0, c0, 1);
    dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, UINT(h.v.size() / 3), UINT(h.idx.size() / 3), h.idx.data(),
                                D3DFMT_INDEX16, h.v.data(), 12);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetDepthStencilSurface(old.p);
    dev->EndScene();
}
// FL 69 A (2026-11-12): the hull as the scene colour, lit and light (the stand-in a darkening would show on): its
// triangles depth-tested into t.scene_s over a dark blue sky (alpha 1 everywhere), Lambert from the screen-space normal
// with a 5 m two-tone panel pattern, linear 0.24 to 0.8.
void draw_lathe_hull_colour(Target& t, const LatheHullLane& h) {
    static IDirect3DVertexShader9* vs = nullptr;
    static IDirect3DPixelShader9* ps = nullptr;
    static IDirect3DVertexDeclaration9* decl = nullptr;
    if (!vs) {
        const char* vsrc = "float4 c0:register(c0); struct O{ float4 p:POSITION; float3 v:TEXCOORD0; };"
                           " O main(float3 v:POSITION){ O o; o.p = float4(c0.x * v.x, c0.y * v.y, (v.z - c0.z) * c0.w, v.z);"
                           " o.v = v; return o; }";
        const char* psrc = "float4 main(float3 v:TEXCOORD0):COLOR0{ float3 n = normalize(cross(ddx(v), ddy(v)));"
                           " n = n.z > 0 ? -n : n; float lam = saturate(dot(n, normalize(float3(-0.45, 0.6, -0.66))));"
                           " float2 q = floor(v.xy * 0.04); float panel = fmod(abs(q.x + q.y), 2.0) > 0.5 ? 1.0 : 0.8;"
                           " float g = (0.3 + 0.5 * lam) * panel; return float4(0.92 * g, 0.96 * g, g, 1.0); }";
        const DWORD *vw = nullptr, *pw = nullptr;
        static Com<ID3DXBuffer> vh, ph;
        const D3DVERTEXELEMENT9 e[] = {{0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
                                       D3DDECL_END()};
        if (!compile(vsrc, "vs_3_0", &vw, &vh.p) || !compile(psrc, "ps_3_0", &pw, &ph.p) ||
            FAILED(dev->CreateVertexShader(vw, &vs)) || FAILED(dev->CreatePixelShader(pw, &ps)) ||
            FAILED(dev->CreateVertexDeclaration(e, &decl)))
            return;
    }
    Com<IDirect3DSurface9> ds, old;
    if (FAILED(dev->CreateDepthStencilSurface(t.w, t.h, D3DFMT_D24X8, D3DMULTISAMPLE_NONE, 0, TRUE, &ds.p, nullptr))) return;
    dev->GetDepthStencilSurface(&old.p);
    const float n = 6.f, f = 1e6f;
    const float c0[4] = {1.6f, 1.6f * float(t.w) / float(t.h), n, f / (f - n)};
    dev->BeginScene();
    dev->SetRenderTarget(0, t.scene_s.p);
    dev->SetDepthStencilSurface(ds.p);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(255, 10, 14, 24), 1.f, 0);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetVertexDeclaration(decl);
    dev->SetVertexShader(vs);
    dev->SetPixelShader(ps);
    dev->SetVertexShaderConstantF(0, c0, 1);
    dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, UINT(h.v.size() / 3), UINT(h.idx.size() / 3), h.idx.data(),
                                D3DFMT_INDEX16, h.v.data(), 12);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev->SetDepthStencilSurface(old.p);
    dev->EndScene();
}
// The pass over the scene as it stands (no clear: the bright hull stays under the draws).
std::pair<HRESULT, rr::WeaponEffectsReport> draw_over(rr::WeaponEffectsPass& pass, const rr::WeaponEffectsFrame& f) {
    dev->BeginScene();
    dev->SetRenderTarget(0, g.scene_s.p);
    rr::WeaponEffectsReport r{};
    const HRESULT hr = pass.run(f, &r);
    dev->EndScene();
    return {hr, r};
}
// The whole FP16 target read back as RGBA floats (row-major, 4 per pixel); empty on failure.
std::vector<float> read_rgba(Target& t, IDirect3DSurface9* source) {
    std::vector<float> out;
    if (FAILED(dev->GetRenderTargetData(source, t.sysmem.p))) return out;
    D3DLOCKED_RECT lr{};
    if (FAILED(t.sysmem.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return out;
    out.resize(std::size_t(t.w) * t.h * 4);
    for (UINT y = 0; y < t.h; ++y) {
        const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
        for (UINT x = 0; x < t.w * 4; ++x) out[std::size_t(y) * t.w * 4 + x] = half_to_float(row[x]);
    }
    t.sysmem.p->UnlockRect();
    return out;
}
// A judging frame as it stands (RGBA linear, no added hull grey) into dump-shield/<name>.pfm.
void dump_plain(const char* name, const std::vector<float>& rgba, unsigned w, unsigned h) {
    if (rgba.size() != std::size_t(w) * h * 4) return;
    CreateDirectoryA("dump-shield", nullptr);
    char path[128];
    std::snprintf(path, sizeof path, "dump-shield/%s.pfm", name);
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "PF\n%u %u\n-1.0\n", w, h);
    std::vector<float> row(std::size_t(w) * 3);
    for (unsigned yy = h; yy-- > 0;) {
        for (unsigned x = 0; x < w; ++x)
            for (unsigned k = 0; k < 3; ++k) row[3 * x + k] = rgba[(std::size_t(yy) * w + x) * 4 + k];
        std::fwrite(row.data(), sizeof(float), row.size(), f);
    }
    std::fclose(f);
}
// The lane's view depth per pixel (0 on sky), read back.
std::vector<float> read_lane_depth(Target& t) {
    std::vector<float> out;
    Com<IDirect3DSurface9> sys;
    if (FAILED(dev->CreateOffscreenPlainSurface(t.w, t.h, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &sys.p, nullptr)) ||
        FAILED(dev->GetRenderTargetData(t.lane_s.p, sys.p)))
        return out;
    D3DLOCKED_RECT lr{};
    if (FAILED(sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return out;
    out.resize(std::size_t(t.w) * t.h);
    for (UINT y = 0; y < t.h; ++y) {
        const float* row = reinterpret_cast<const float*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
        for (UINT x = 0; x < t.w; ++x)
            out[std::size_t(y) * t.w + x] = row[4 * x] >= 0.f && row[4 * x] <= 1.f ? row[4 * x + 2] : 0.f;
    }
    sys.p->UnlockRect();
    return out;
}
// A judging frame (judge-looks-from-stage-dumps): the hull lit flat grey by its depth normal plus the effect's linear
// RGB, written as PFM into dump-shield/<name>.pfm under the run directory (run_weapon_effects.py encodes it as PNG).
void dump_shield(const char* name, const std::vector<float>& rgb, const std::vector<float>& depth, unsigned w, unsigned h) {
    if (rgb.size() != std::size_t(w) * h * 3 || depth.size() != std::size_t(w) * h) return;
    CreateDirectoryA("dump-shield", nullptr);
    char path[128];
    std::snprintf(path, sizeof path, "dump-shield/%s.pfm", name);
    FILE* f = std::fopen(path, "wb");
    if (!f) return;
    std::fprintf(f, "PF\n%u %u\n-1.0\n", w, h);
    const double px = 2.0 / (1.6 * double(w)); // view units per pixel per unit depth
    std::vector<float> row(std::size_t(w) * 3);
    for (unsigned yy = h; yy-- > 0;) {
        for (unsigned x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(yy) * w + x;
            float g = 0.f;
            const float z = depth[i];
            if (z > 0.f && x > 0 && x + 1 < w && yy > 0 && yy + 1 < h) {
                auto at = [&](std::size_t k) { return depth[k] > 0.f ? double(depth[k]) : double(z); };
                const double s = px * double(z), dx = (at(i + 1) - at(i - 1)) / (2 * s), dy = (at(i - w) - at(i + w)) / (2 * s);
                const double n[3] = {-dx, -dy, -1.0}, l[3] = {-0.45, 0.6, -0.66};
                const double nn = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                const double lambert = std::max(0.0, (n[0] * l[0] + n[1] * l[1] + n[2] * l[2]) / nn);
                g = float(0.015 + 0.09 * lambert);
            }
            for (unsigned k = 0; k < 3; ++k) row[3 * x + k] = g + rgb[i * 3 + k];
        }
        std::fwrite(row.data(), sizeof(float), row.size(), f);
    }
    std::fclose(f);
}
// The lathe meshes the fixture's builds point into (static: the timing section draws the lathe section's patches), and
// the Titan's two-hit draws for the timing rows (0 the hull decal without a profile, 1 the lathe) with its hull lane.
ws::LatheCache fixture_lathes;
// The staged owner as a ship whose profile is a box of half-extents ext (view units) about its origin (TShips row 11, the
// Titan's class, unit axis scales): the eye sphere's cases (its radius the smallest half-extent x bubble_scale). The
// lathe cache is cleared (its row-11 mesh would answer for the previous profile).
void stage_box_profile(const double ext[3]) {
    stage_lathe_type();
    wc::ShipProfile& p = lathe_profiles.entries[0];
    const double to_engine = 1.0 / double(wc::world_scale);
    p.z0 = float(-ext[2] * to_engine);
    p.z1 = float(ext[2] * to_engine);
    for (auto& slice : p.slice) {
        slice[0] = slice[1] = 0.f;
        slice[2] = float(ext[0] * to_engine);
        slice[3] = float(ext[1] * to_engine);
    }
    fixture_lathes.clear();
}
rr::WeaponPatch titan_timing[2][wc::max_patches];
unsigned titan_timing_count[2] = {0, 0};
LatheHullLane titan_timing_lane;
// FL 69 A: the Titan's flank 446 m from the eye under 16 spread live hits (the lathe draws and the hull lane), timed too.
rr::WeaponPatch titan_flight[wc::max_patches], titan_flight64[wc::max_patches];
unsigned titan_flight_count = 0, titan_flight64_count = 0; // 16 and 64 (core::owner_hits) live hits
LatheHullLane titan_flight_lane;
// Bubble draws of hits on an owner whose +z face looks at the camera (make_patch's frame and centre), its bubble a sphere
// of the scene's surface radius about its centre: each hit at the plane point (qx, qy) of depth S maps radially onto
// it, through the production stage::fill_bubble (the builder's bubble draws). Returns the draws written.
unsigned bubble_patches(const TierScene& s, const Hit* hits, unsigned count, rr::WeaponPatch* out, unsigned cap) {
    float frame[9], centre[3] = {0.f, 0.f, s.centre_z}, points[wc::owner_hits][3], ages[wc::owner_hits], colour[3];
    facing_frame(frame);
    unsigned n = 0;
    for (unsigned k = 0; k < count && n < wc::owner_hits; ++k, ++n) {
        points[n][0] = hits[k].qx;
        points[n][1] = hits[k].qy;
        points[n][2] = s.surface;
        ages[n] = hits[k].age;
    }
    wc::shield_colour(1.f, ws::Tuning{}.bands, colour);
    ws::Bubble b;
    for (unsigned k = 0; k < 3; ++k) b.e[k] = s.surface;
    ws::Counts c{};
    unsigned regions_left = 0;
    ws::fill_bubble(out, cap, &regions_left, centre, frame, s.tier, b, points, ages, n, colour, ws::Tuning{}, &c);
    return c.patches;
}
unsigned bubble_patches(const TierScene& s, std::initializer_list<Hit> hits, rr::WeaponPatch* out, unsigned cap) {
    return bubble_patches(s, hits.begin(), unsigned(hits.size()), out, cap);
}
// The hull inside a facing owner's bubble: a sphere of `share` of the scene's surface radius (the lane under a bubble).
HullLane facing_hull(const TierScene& s, double share) {
    HullLane h{};
    float frame[9];
    facing_frame(frame);
    for (unsigned k = 0; k < 3; ++k) {
        h.centre[k] = k == 2 ? double(s.centre_z) : 0.0;
        h.ext[k] = share * double(s.surface);
        for (unsigned j = 0; j < 3; ++j) h.axes[k][j] = frame[3 * k + j];
    }
    return h;
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
            draw_lane(g, z_hit, 1.f); // the patch's plane everywhere
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
// One run of the pass into a cleared target inside its own scene (the cockpit cases' draw).
std::pair<HRESULT, rr::WeaponEffectsReport> draw_on(rr::WeaponEffectsPass& pass, Target& t, const rr::WeaponEffectsFrame& f) {
    dev->BeginScene();
    dev->SetRenderTarget(0, t.scene_s.p);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
    rr::WeaponEffectsReport r{};
    const HRESULT hr = pass.run(f, &r);
    dev->EndScene();
    return {hr, r};
}
}

// The bullet re-shade (X3M_WEAPON_EFFECTS=reshade; bullet-draw-batch.md section 6; weapon_bullet_reshade_fixture.h):
// (a) the stand-in pair compiles and the production ps_2_0 programs (plain and far-flag) create beside the vs_1_1
// stand-in; (b) per flat texel colour and vertex alpha, the substituted draw's output (blend off) equals
// bullet_reshade_core.h's model and the stand-in's equals vanilla's t.rgb * a; (b2) the additive route's path of the
// production sequence (bullet_reshade_route.h begin / end, which MotionOutput runs; MotionOutput itself is not
// instantiated here): DESTBLEND ONE, our program and the gain-folded c31 while drawing, the output, the far flag, and
// everything put back; (c) the plain path's production sequence on the stand-in bolt puts the game's program and the
// application's c31 back (Get*); (d) the stand-in bolt (procedural IRE topology and strip) re-shaded is dimmer than vanilla
// overall; (e) the substitution's CPU cost per draw (the four calls alone, and the draw loop with and without them,
// best of 7 runs of 2000 draws) and a 469-bolt batch's fenced time vanilla against re-shaded at 1920x1080 (the GPU
// side of the longer program). RESHADE rows; never the game's bytes.
void reshade_cases() {
    namespace rf = reshade_fx;
    namespace rs = x3m::weapon_effects::reshade;
    LPD3DXBUFFER vs_hold = nullptr, ps_hold = nullptr;
    const DWORD *vs_code = nullptr, *ps_code = nullptr;
    const bool compiled = compile_shader && compile(rf::vs_source, "vs_1_1", &vs_code, &vs_hold) &&
                          rf::assemble(d3dx, rf::vanilla_ps_asm, &ps_hold);
    if (compiled) ps_code = static_cast<const DWORD*>(ps_hold->GetBufferPointer());
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> vanilla, ours, ours_flag;
    Com<IDirect3DVertexDeclaration9> decl;
    const bool created = compiled && SUCCEEDED(dev->CreateVertexShader(vs_code, &vs.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(ps_code, &vanilla.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(rf::reshade_program, &ours.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(rf::reshade_flag_program, &ours_flag.p)) &&
                         SUCCEEDED(dev->CreateVertexDeclaration(rf::declaration(), &decl.p));
    // the game's B1 body (FNV-1a 32 of its instruction tokens, from the FL archive's effects ps ec1f5c4a2f4e1445)
    constexpr std::uint32_t game_ps_body = 0xb006e837u;
    const std::uint32_t standin_body = ps_code ? rf::body_hash(ps_code) : 0u;
    std::printf("RESHADE case=programs compiled=%u created=%u standin_ps_body=%08lx game_ps_body=%08lx body_match=%u "
                "reshade_words=%u\n",
                unsigned(compiled), unsigned(created), static_cast<unsigned long>(standin_body),
                static_cast<unsigned long>(game_ps_body), unsigned(standin_body == game_ps_body),
                unsigned(sizeof rf::reshade_program / sizeof rf::reshade_program[0]));
    report("reshade_programs_create", created, "");
    if (vs_hold) vs_hold->Release();
    if (ps_hold) ps_hold->Release();
    if (!created) return;
    const auto tune = x3m::weapon_effects::projectile_family::baked;
    float constants[4];
    rs::constants(tune, constants);
    // the application's c29..c31 the substitution must put back (c31 = app_c31)
    const float app_block[12] = {3.f, -1.f, 0.25f, 9.f, -4.f, 0.5f, 2.f, -0.75f, 0.125f, -2.5f, 7.f, 0.0625f};
    // the drawn body's block without a layout: c29..c30 identity_layout, c31 the look
    float plain_block[12];
    for (unsigned i = 0; i < 8; ++i) plain_block[i] = rs::identity_layout[i];
    for (unsigned i = 0; i < 4; ++i) plain_block[8 + i] = constants[i];
    // (b) flat texels through a full-target quad, blend off: our program against the CPU model
    {
        Com<IDirect3DTexture9> rt, flat;
        Com<IDirect3DSurface9> rt_s, sys;
        bool ok = SUCCEEDED(dev->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt.p, nullptr)) &&
                  SUCCEEDED(rt.p->GetSurfaceLevel(0, &rt_s.p)) &&
                  SUCCEEDED(dev->CreateOffscreenPlainSurface(16, 16, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys.p, nullptr)) &&
                  SUCCEEDED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &flat.p, nullptr));
        const unsigned char texels[6][4] = {{99, 77, 17, 70}, {26, 13, 3, 20}, {255, 247, 239, 238}, {0, 0, 0, 0},
                                            {51, 51, 64, 40}, {140, 107, 47, 120}};
        const BYTE alphas[6] = {255, 255, 128, 255, 204, 255};
        float worst = 0.f, worst_vanilla = 0.f;
        IDirect3DSurface9* old_rt = nullptr;
        dev->GetRenderTarget(0, &old_rt);
        if (ok) dev->SetRenderTarget(0, rt_s.p);
        float rows[16];
        rf::clip_rows(1.f, 1.f, rows);
        for (unsigned c = 0; ok && c < 6; ++c) {
            D3DLOCKED_RECT lr{};
            ok = SUCCEEDED(flat.p->LockRect(0, &lr, nullptr, 0));
            if (!ok) break;
            unsigned char* b = static_cast<unsigned char*>(lr.pBits);
            b[0] = texels[c][2], b[1] = texels[c][1], b[2] = texels[c][0], b[3] = texels[c][3];
            flat.p->UnlockRect(0);
            const DWORD col = DWORD(alphas[c]) << 24;
            const rf::Vertex quad[6] = {{-1, -1, 1, 0, 0, col}, {-1, 1, 1, 0, 0, col}, {1, 1, 1, 0, 0, col},
                                        {-1, -1, 1, 0, 0, col}, {1, 1, 1, 0, 0, col}, {1, -1, 1, 0, 0, col}};
            for (unsigned which = 0; which < 2 && ok; ++which) {
                dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
                dev->BeginScene();
                rf::bind_pass(dev, vs.p, decl.p, flat.p, rows, false);
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); // the program's output itself
                dev->SetPixelShaderConstantF(rs::block_register, plain_block, rs::block_count);
                dev->SetPixelShader(which ? ours.p : vanilla.p);
                ok = SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(rf::Vertex)));
                dev->EndScene();
                ok = ok && SUCCEEDED(dev->GetRenderTargetData(rt_s.p, sys.p));
                D3DLOCKED_RECT out{};
                ok = ok && SUCCEEDED(sys.p->LockRect(&out, nullptr, D3DLOCK_READONLY));
                if (!ok) break;
                const auto* px = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(out.pBits) + 8 * out.Pitch) + 8 * 4;
                const float got[3] = {half_to_float(px[0]), half_to_float(px[1]), half_to_float(px[2])};
                sys.p->UnlockRect();
                const float t[3] = {texels[c][0] / 255.f, texels[c][1] / 255.f, texels[c][2] / 255.f};
                const float a = alphas[c] / 255.f;
                float want[3];
                if (which)
                    rs::model(constants, t, a, want);
                else
                    for (unsigned k = 0; k < 3; ++k) want[k] = t[k] * a;
                for (unsigned k = 0; k < 3; ++k) {
                    const float err = std::fabs(got[k] - want[k]) / (0.004f + 0.004f * std::fabs(want[k]));
                    float& w = which ? worst : worst_vanilla;
                    w = std::max(w, err);
                }
                if (which)
                    std::printf("RESHADE case=texel index=%u t=%.3f,%.3f,%.3f a=%.3f got=%.4f,%.4f,%.4f model=%.4f,%.4f,%.4f\n", c,
                                double(t[0]), double(t[1]), double(t[2]), double(a), double(got[0]), double(got[1]),
                                double(got[2]), double(want[0]), double(want[1]), double(want[2]));
            }
        }
        dev->SetRenderTarget(0, old_rt);
        std::printf("RESHADE case=model worst_rel=%.3f vanilla_worst_rel=%.3f\n", double(worst), double(worst_vanilla));
        report("reshade_program_matches_model", ok && worst <= 1.f, "");
        report("reshade_standin_vanilla_is_t_times_a", ok && worst_vanilla <= 1.f, "");
        // (b3) the geometric size (projectile-look.md "Geometric size (2026-11-10)"): the IRE's table layout (the
        // generator's FL record) at rs_head 0.5 / rs_width 0.6, a flat texel drawn with one UV per quad at points in the
        // head's footprint (r / R 0.1..0.6 and a box corner past the circle), in the fin band (d -0.1..0.7) and outside
        // both: the output equals model x geometric(); at the keys' default 1 every point's output equals the
        // layout-free program's, bit for bit (the accepted look unchanged).
        const std::int32_t ire_words[8] = {3, 57881, 56830, 6546, 23063, 18300, 858, 64010};
        auto shrink = tune;
        shrink.rs_head = 0.5f;
        shrink.rs_width = 0.6f;
        float shrunk[8], kept[8];
        rs::layout_constants(ire_words, shrink, shrunk);
        rs::layout_constants(ire_words, tune, kept);
        const float head_u = ire_words[1] / 65536.f, head_v = ire_words[2] / 65536.f, head_r = ire_words[3] / 65536.f;
        const float axis_v = ire_words[4] / 65536.f, rim_v = ire_words[5] / 65536.f;
        const float points[11][2] = {
            {head_u + 0.1f * head_r, head_v}, {head_u + 0.35f * head_r, head_v}, {head_u, head_v + 0.45f * head_r},
            {head_u - 0.6f * head_r, head_v}, {head_u + 0.9f * head_r, head_v + 0.9f * head_r},
            {0.5f, axis_v + 0.1f * (rim_v - axis_v)}, {0.3f, axis_v + 0.35f * (rim_v - axis_v)},
            {0.7f, axis_v + 0.5f * (rim_v - axis_v)}, {0.2f, axis_v + 0.7f * (rim_v - axis_v)},
            {0.4f, axis_v - 0.1f * (rim_v - axis_v)}, {0.5f, 0.6f}};
        float worst_geo = 0.f;
        bool same_at_one = true;
        if (ok) {
            D3DLOCKED_RECT lr{};
            ok = SUCCEEDED(flat.p->LockRect(0, &lr, nullptr, 0));
            if (ok) {
                unsigned char* b = static_cast<unsigned char*>(lr.pBits);
                b[0] = texels[0][2], b[1] = texels[0][1], b[2] = texels[0][0], b[3] = texels[0][3];
                flat.p->UnlockRect(0);
            }
        }
        if (ok) dev->SetRenderTarget(0, rt_s.p);
        for (unsigned c = 0; ok && c < 11; ++c) {
            const float u = points[c][0], v = points[c][1];
            const DWORD col = 0xff000000u;
            const rf::Vertex quad[6] = {{-1, -1, 1, u, v, col}, {-1, 1, 1, u, v, col}, {1, 1, 1, u, v, col},
                                        {-1, -1, 1, u, v, col}, {1, 1, 1, u, v, col},  {1, -1, 1, u, v, col}};
            std::uint16_t raw[3][3]{};
            float got[3][3]{};
            for (unsigned which = 0; which < 3 && ok; ++which) { // shrunk, keys at 1, no layout
                float block[12];
                const float* layout = which == 0 ? shrunk : which == 1 ? kept : rs::identity_layout;
                for (unsigned i = 0; i < 8; ++i) block[i] = layout[i];
                for (unsigned i = 0; i < 4; ++i) block[8 + i] = constants[i];
                dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
                dev->BeginScene();
                rf::bind_pass(dev, vs.p, decl.p, flat.p, rows, false);
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
                dev->SetPixelShaderConstantF(rs::block_register, block, rs::block_count);
                dev->SetPixelShader(ours.p);
                ok = SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(rf::Vertex)));
                dev->EndScene();
                ok = ok && SUCCEEDED(dev->GetRenderTargetData(rt_s.p, sys.p));
                D3DLOCKED_RECT out{};
                ok = ok && SUCCEEDED(sys.p->LockRect(&out, nullptr, D3DLOCK_READONLY));
                if (!ok) break;
                const auto* px = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(out.pBits) + 8 * out.Pitch) + 8 * 4;
                for (unsigned k = 0; k < 3; ++k) raw[which][k] = px[k], got[which][k] = half_to_float(px[k]);
                sys.p->UnlockRect();
            }
            if (!ok) break;
            const float t[3] = {texels[0][0] / 255.f, texels[0][1] / 255.f, texels[0][2] / 255.f};
            float want[3];
            rs::model(constants, t, 1.f, want);
            const float g = rs::geometric(shrunk, u, v);
            for (unsigned k = 0; k < 3; ++k) {
                const float w = want[k] * g;
                worst_geo = std::max(worst_geo, std::fabs(got[0][k] - w) / (0.004f + 0.004f * std::fabs(w)));
            }
            same_at_one = same_at_one && !std::memcmp(raw[1], raw[2], sizeof raw[1]);
            std::printf("RESHADE case=geometric index=%u u=%.4f v=%.4f factor=%.4f got=%.4f model=%.4f kept=%.4f "
                        "plain=%.4f\n",
                        c, double(u), double(v), double(g), double(got[0][1]), double(want[1] * g), double(got[1][1]),
                        double(got[2][1]));
        }
        dev->SetRenderTarget(0, old_rt);
        if (old_rt) old_rt->Release();
        std::printf("RESHADE case=geometric_model worst_rel=%.3f same_at_one=%u rs_head=0.5 rs_width=0.6\n",
                    double(worst_geo), unsigned(same_at_one));
        report("reshade_geometric_matches_model", ok && worst_geo <= 1.f, "");
        report("reshade_geometric_keys_at_one_unchanged", ok && same_at_one, "");
    }
    // (b2) the additive route's path through the production sequence (bullet_reshade_route.h, as
    // prepare_screen_additive runs it under HDR): the game's ONE / INVSRCCOLOR pass, its program and an application
    // c31 bound; begin with the conversion and the route's gain 2 -> DESTBLEND ONE, our program and c31 = (gain x 2,
    // edge, core x 2, normaliser) while drawing, the output the background plus 2 x the model; end -> the game's
    // program, the application's c31, DESTBLEND INVSRCCOLOR. With three targets and independent write masks the same
    // with the flag variant and the route's far-flag targets (RT1 masked off, RT2 GREEN): RT2.g = 32 x max(rgb).
    {
        namespace rr2 = x3m::weapon_effects::reshade;
        Com<IDirect3DTexture9> rt[3], flat;
        Com<IDirect3DSurface9> rt_s[3], sys;
        bool ok = SUCCEEDED(dev->CreateOffscreenPlainSurface(16, 16, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys.p, nullptr)) &&
                  SUCCEEDED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &flat.p, nullptr));
        for (unsigned i = 0; i < 3 && ok; ++i)
            ok = SUCCEEDED(dev->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt[i].p, nullptr)) &&
                 SUCCEEDED(rt[i].p->GetSurfaceLevel(0, &rt_s[i].p));
        D3DLOCKED_RECT lr{};
        ok = ok && SUCCEEDED(flat.p->LockRect(0, &lr, nullptr, 0));
        if (ok) {
            unsigned char* b = static_cast<unsigned char*>(lr.pBits);
            b[0] = 17, b[1] = 77, b[2] = 99, b[3] = 70; // t = (99, 77, 17) / 255
            flat.p->UnlockRect(0);
        }
        const float t[3] = {99 / 255.f, 77 / 255.f, 17 / 255.f}, gain = 2.f, bg = 64 / 255.f;
        float model[3];
        rs::model(constants, t, 1.f, model);
        const bool mrt = caps.NumSimultaneousRTs >= 3 && (caps.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS);
        const auto read_px = [&](IDirect3DSurface9* s, float out[4]) {
            if (FAILED(dev->GetRenderTargetData(s, sys.p))) return false;
            D3DLOCKED_RECT o{};
            if (FAILED(sys.p->LockRect(&o, nullptr, D3DLOCK_READONLY))) return false;
            const auto* px = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(o.pBits) + 8 * o.Pitch) + 8 * 4;
            for (unsigned k = 0; k < 4; ++k) out[k] = half_to_float(px[k]);
            sys.p->UnlockRect();
            return true;
        };
        IDirect3DSurface9* old_rt = nullptr;
        dev->GetRenderTarget(0, &old_rt);
        float rows[16];
        rf::clip_rows(1.f, 1.f, rows);
        const rf::Vertex quad[6] = {{-1, -1, 1, 0, 0, 0xff000000u}, {-1, 1, 1, 0, 0, 0xff000000u}, {1, 1, 1, 0, 0, 0xff000000u},
                                    {-1, -1, 1, 0, 0, 0xff000000u}, {1, 1, 1, 0, 0, 0xff000000u}, {1, -1, 1, 0, 0, 0xff000000u}};
        bool during_all = true, after_all = true;
        float worst = 0.f, flag_got = -1.f, flag_want = 0.f;
        for (unsigned variant = 0; variant < (mrt ? 2u : 1u) && ok; ++variant) {
            const bool flag = variant == 1;
            if (flag) { // RT2 cleared alone, then bound beside RT0 with the route's masks
                dev->SetRenderTarget(0, rt_s[2].p);
                dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            }
            dev->SetRenderTarget(0, rt_s[0].p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 64, 64, 64), 1.f, 0);
            if (flag) {
                dev->SetRenderTarget(1, rt_s[1].p);
                dev->SetRenderTarget(2, rt_s[2].p);
                dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 0);
                dev->SetRenderState(D3DRS_COLORWRITEENABLE2, D3DCOLORWRITEENABLE_GREEN);
            }
            dev->BeginScene();
            rf::bind_pass(dev, vs.p, decl.p, flat.p, rows, false);
            dev->SetPixelShader(vanilla.p);
            dev->SetPixelShaderConstantF(rs::block_register, app_block, rs::block_count);
            auto ops = rf::device_ops(dev);
            rr2::Plan plan;
            plan.program = flag ? ours_flag.p : ours.p;
            plan.constants = constants;
            plan.gain = gain;
            plan.convert = true;
            rr2::Applied applied;
            ok = SUCCEEDED(rr2::begin(ops, plan, app_block, applied)) && applied.destblend && applied.block && applied.program;
            DWORD blend = 0;
            IDirect3DPixelShader9* bound = nullptr;
            float block[12]{};
            const float* const c31 = block + 8;
            const bool during = ok && SUCCEEDED(dev->GetRenderState(D3DRS_DESTBLEND, &blend)) && blend == D3DBLEND_ONE &&
                                SUCCEEDED(dev->GetPixelShader(&bound)) && bound == plan.program &&
                                SUCCEEDED(dev->GetPixelShaderConstantF(rs::block_register, block, rs::block_count)) &&
                                !std::memcmp(block, rs::identity_layout, sizeof rs::identity_layout) &&
                                c31[0] == constants[0] * gain && c31[1] == constants[1] && c31[2] == constants[2] * gain &&
                                c31[3] == constants[3];
            if (bound) bound->Release();
            bound = nullptr;
            ok = ok && SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(rf::Vertex)));
            const HRESULT back = rr2::end(ops, applied, vanilla.p, app_block);
            const bool after = SUCCEEDED(back) && SUCCEEDED(dev->GetRenderState(D3DRS_DESTBLEND, &blend)) &&
                               blend == D3DBLEND_INVSRCCOLOR && SUCCEEDED(dev->GetPixelShader(&bound)) && bound == vanilla.p &&
                               SUCCEEDED(dev->GetPixelShaderConstantF(rs::block_register, block, rs::block_count)) &&
                               !std::memcmp(block, app_block, sizeof block);
            if (bound) bound->Release();
            dev->EndScene();
            during_all = during_all && during;
            after_all = after_all && after;
            if (flag) {
                dev->SetRenderTarget(1, nullptr);
                dev->SetRenderTarget(2, nullptr);
                dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 15);
                dev->SetRenderState(D3DRS_COLORWRITEENABLE2, 15);
            }
            float px[4]{};
            ok = ok && read_px(rt_s[0].p, px);
            for (unsigned k = 0; k < 3 && ok; ++k) {
                const float want = bg + gain * model[k];
                worst = std::max(worst, std::fabs(px[k] - want) / (0.004f + 0.004f * want));
            }
            if (flag && ok && read_px(rt_s[2].p, px)) {
                flag_got = px[1];
                flag_want = 32.f * gain * std::max(model[0], std::max(model[1], model[2]));
            }
        }
        dev->SetRenderTarget(0, old_rt);
        if (old_rt) old_rt->Release();
        const bool flag_ok = !mrt || std::fabs(flag_got - flag_want) <= 0.01f * flag_want + 0.01f;
        std::printf("RESHADE case=additive gain=%.1f during=%u restored=%u worst_rel=%.3f mrt=%u flag_g=%.3f flag_want=%.3f\n",
                    double(gain), unsigned(during_all), unsigned(after_all), double(worst), unsigned(mrt),
                    double(flag_got), double(flag_want));
        report("reshade_route_additive_binds_converts_restores", ok && during_all && after_all && worst <= 1.f && flag_ok, "");
    }
    // (c), (d), (e) on a 1920x1080 FP16 target with the stand-in bolt
    Com<IDirect3DTexture9> rt, tex;
    Com<IDirect3DSurface9> rt_s, sys;
    Com<IDirect3DVertexBuffer9> vb;
    unsigned side = 0;
    const std::vector<unsigned char> texels = rf::standin_texture(&side);
    tex.p = rf::make_texture(dev, texels.data(), side);
    constexpr unsigned batch = 469; // the per-draw cap of an IRE-only selection (0x78f0 vertices / 66)
    bool ok = tex.p && SUCCEEDED(dev->CreateTexture(1920, 1080, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt.p, nullptr)) &&
              SUCCEEDED(rt.p->GetSurfaceLevel(0, &rt_s.p)) &&
              SUCCEEDED(dev->CreateOffscreenPlainSurface(1920, 1080, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys.p, nullptr)) &&
              SUCCEEDED(dev->CreateVertexBuffer(batch * 66 * sizeof(rf::Vertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                                D3DPOOL_DEFAULT, &vb.p, nullptr));
    report("reshade_targets", ok, "");
    if (!ok) return;
    const rf::Mesh mesh = rf::standin_mesh();
    float rows[16];
    rf::clip_rows(1.6f, 1.6f * 1920.f / 1080.f, rows);
    const auto fill = [&](unsigned bolts, float distance) {
        void* p = nullptr;
        if (FAILED(vb.p->Lock(0, 0, &p, D3DLOCK_DISCARD))) return false;
        auto* v = static_cast<rf::Vertex*>(p);
        for (unsigned b = 0; b < bolts; ++b) {
            const float spread = bolts > 1 ? float(b % 24) / 23.f - 0.5f : 0.f, rise = bolts > 1 ? float(b / 24) / 20.f - 0.5f : 0.f;
            const float centre[3] = {spread * distance, rise * distance * 0.5f, distance};
            v += rf::place(mesh, centre, 0.44f, 0.35f, 255, v);
        }
        vb.p->Unlock();
        return true;
    };
    IDirect3DSurface9* old_rt = nullptr;
    dev->GetRenderTarget(0, &old_rt);
    dev->SetRenderTarget(0, rt_s.p);
    const auto image_sum = [&](double* sum, float* peak) {
        if (FAILED(dev->GetRenderTargetData(rt_s.p, sys.p))) return false;
        D3DLOCKED_RECT lr{};
        if (FAILED(sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
        *sum = 0.0;
        *peak = 0.f;
        for (UINT y = 0; y < 1080; ++y) {
            const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
            for (UINT x = 0; x < 1920; ++x) {
                const float l = 0.2126f * half_to_float(row[x * 4]) + 0.7152f * half_to_float(row[x * 4 + 1]) +
                                0.0722f * half_to_float(row[x * 4 + 2]);
                *sum += l;
                *peak = std::max(*peak, l);
            }
        }
        sys.p->UnlockRect();
        return true;
    };
    double sums[2]{};
    float peaks[2]{};
    bool restored_all = true, getters_ok = true;
    ok = fill(1, 50.f);
    for (unsigned which = 0; which < 2 && ok; ++which) {
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
        dev->BeginScene();
        rf::bind_pass(dev, vs.p, decl.p, tex.p, rows, false);
        dev->SetStreamSource(0, vb.p, 0, sizeof(rf::Vertex));
        dev->SetPixelShader(vanilla.p);
        dev->SetPixelShaderConstantF(rs::block_register, app_block, rs::block_count);
        bool restored = true;
        const HRESULT hr = which ? rf::reshaded_draw(dev, ours.p, vanilla.p, constants, app_block, 22, &restored)
                                 : dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 22);
        dev->EndScene();
        ok = SUCCEEDED(hr) && image_sum(&sums[which], &peaks[which]);
        restored_all = restored_all && restored;
        if (which) { // (c) the game's binding back
            IDirect3DPixelShader9* bound = nullptr;
            float block[12]{};
            getters_ok = SUCCEEDED(dev->GetPixelShader(&bound)) && bound == vanilla.p &&
                         SUCCEEDED(dev->GetPixelShaderConstantF(rs::block_register, block, rs::block_count)) &&
                         !std::memcmp(block, app_block, sizeof block);
            if (bound) bound->Release();
        }
    }
    std::printf("RESHADE case=bolt distance_m=50 vanilla_sum=%.2f reshade_sum=%.2f energy_ratio=%.3f vanilla_peak=%.4f "
                "reshade_peak=%.4f gain=%.3g edge=%.3g core=%.3g\n",
                sums[0], sums[1], sums[0] > 0 ? sums[1] / sums[0] : 0.0, double(peaks[0]), double(peaks[1]),
                double(constants[0]), double(constants[1]), double(constants[2]));
    // the plain path's production sequence (rf::reshaded_draw runs bullet_reshade_route.h) on the stand-in bolt
    report("reshade_route_plain_restores_program_and_c31", ok && restored_all && getters_ok, "");
    report("reshade_bolt_dimmer_than_vanilla", ok && sums[0] > 0.0 && sums[1] < 0.8 * sums[0], "");
    // (e) the cost: CPU per draw of the four calls alone, the draw loop with and without them; the 469-bolt batch fenced
    LARGE_INTEGER freq{}, a{}, b{};
    QueryPerformanceFrequency(&freq);
    Com<IDirect3DQuery9> fence;
    dev->CreateQuery(D3DQUERYTYPE_EVENT, &fence.p);
    const auto finish = [&] {
        if (!fence.p) return;
        fence.p->Issue(D3DISSUE_END);
        while (fence.p->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
        }
    };
    constexpr unsigned draws = 2000, runs = 7;
    double best_calls = 1e30, best_vanilla = 1e30, best_reshaded = 1e30;
    ok = ok && fill(1, 1500.f);
    for (unsigned run = 0; run < runs && ok; ++run) {
        dev->BeginScene();
        rf::bind_pass(dev, vs.p, decl.p, tex.p, rows, false);
        dev->SetStreamSource(0, vb.p, 0, sizeof(rf::Vertex));
        dev->SetPixelShader(vanilla.p);
        finish();
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i) {
            dev->SetPixelShaderConstantF(rs::block_register, plain_block, rs::block_count);
            dev->SetPixelShader(ours.p);
            dev->SetPixelShader(vanilla.p);
            dev->SetPixelShaderConstantF(rs::block_register, app_block, rs::block_count);
        }
        QueryPerformanceCounter(&b);
        best_calls = std::min(best_calls, ticks_us(a, b, freq) / draws);
        finish();
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i) dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 22);
        finish();
        QueryPerformanceCounter(&b);
        best_vanilla = std::min(best_vanilla, ticks_us(a, b, freq) / draws);
        QueryPerformanceCounter(&a);
        bool restored = true;
        for (unsigned i = 0; i < draws; ++i) rf::reshaded_draw(dev, ours.p, vanilla.p, constants, app_block, 22, &restored);
        finish();
        QueryPerformanceCounter(&b);
        best_reshaded = std::min(best_reshaded, ticks_us(a, b, freq) / draws);
        dev->EndScene();
    }
    device_guard("reshade_cost");
    double batch_us[2] = {1e30, 1e30};
    ok = ok && fill(batch, 300.f);
    for (unsigned run = 0; run < runs && ok; ++run)
        for (unsigned which = 0; which < 2; ++which) {
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 40, 30, 50), 1.f, 0);
            dev->BeginScene();
            rf::bind_pass(dev, vs.p, decl.p, tex.p, rows, false);
            dev->SetStreamSource(0, vb.p, 0, sizeof(rf::Vertex));
            dev->SetPixelShader(vanilla.p);
            finish();
            QueryPerformanceCounter(&a);
            bool restored = true;
            if (which)
                rf::reshaded_draw(dev, ours.p, vanilla.p, constants, app_block, batch * 22, &restored);
            else
                dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, batch * 22);
            finish();
            QueryPerformanceCounter(&b);
            dev->EndScene();
            batch_us[which] = std::min(batch_us[which], ticks_us(a, b, freq));
        }
    device_guard("reshade_batch");
    dev->SetRenderTarget(0, old_rt);
    if (old_rt) old_rt->Release();
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetTexture(0, nullptr);
    dev->SetStreamSource(0, nullptr, 0, 0);
    std::printf("RESHADE case=cost draws=%u runs=%u calls_us_per_draw=%.3f draw_us_vanilla=%.3f draw_us_reshaded=%.3f "
                "substitution_us_per_draw=%.3f batch_bolts=%u batch_us_vanilla=%.1f batch_us_reshaded=%.1f fenced=%u\n",
                draws, runs, best_calls, best_vanilla, best_reshaded, best_reshaded - best_vanilla, batch, batch_us[0],
                batch_us[1], unsigned(fence.p != nullptr));
    // the geometric size's CPU at a body change (MotionOutput::bullet_reshade_layout: weapon_effects_patch.cpp's linear
    // scan of the mapped body ids, then layout_constants): 30 mapped bodies, every draw another body (the worst case:
    // the batch submits one body per call), best of 7 runs of 2000
    double best_layout = 1e30;
    {
        std::uint32_t ids[30];
        std::int32_t words[30][8];
        for (unsigned i = 0; i < 30; ++i) {
            ids[i] = 0x100u + 7u * i;
            const std::int32_t w[8] = {3, 57881 + std::int32_t(i), 56830, 6546, 23063, 18300, 858, 64010};
            std::memcpy(words[i], w, sizeof w);
        }
        auto shrink = tune;
        shrink.rs_head = 0.5f;
        shrink.rs_width = 0.6f;
        volatile float sink = 0.f;
        for (unsigned run = 0; run < runs; ++run) {
            QueryPerformanceCounter(&a);
            for (unsigned i = 0; i < draws; ++i) {
                const std::uint32_t body = ids[(i * 7u) % 30u];
                unsigned k = 0;
                while (k < 30 && ids[k] != body) ++k;
                float out[8];
                rs::layout_constants(k < 30 ? words[k] : nullptr, shrink, out);
                sink = sink + out[2];
            }
            QueryPerformanceCounter(&b);
            best_layout = std::min(best_layout, ticks_us(a, b, freq) / draws);
        }
    }
    std::printf("RESHADE case=layout_cost draws=%u runs=%u mapped_bodies=30 layout_us_per_body_change=%.4f\n", draws, runs,
                best_layout);
    report("reshade_cost_measured", ok && fence.p && best_calls < 50.0 && best_layout < 1.0, "");
}

// The bullet re-shade's lone path (bullet-draw-batch.md section 7; motion_output_bullet_reshade_inc.h lone_bullet_match
// / prepare_lone_bullet): a bullet body drawn through the material submit with its effects.fx material.
// (a) Recognition by the production pieces: the effects DEFAULT / INSTANCE VS hash (lone_vertex_shader), the blend
// (lone_blend), the scope node's body id at +0x144 through engine_memory::read and the body map built by
// build_body_map from a table and a TBullets block (body_layout): the IRE and the PPC rows map, a hull body, another
// VS, a SRCALPHA blend, blend off and a reversed op do not; ONE / ONE is the additive class.
// (b) The production sequence on the lone stand-in pair (reshade_fx lone_*; the game pair's interface): an indexed
// draw under the material's ONE / INVSRCCOLOR with an application c29..c31 bound; the stand-in's own output is the
// texel x the fade (g_AlphaValue 0.8 in COLOR0.x, COLOR0.a 0); our lone program's output is the model at alpha = the
// fade; then as prepare_screen_additive runs a lone draw under HDR: DESTBLEND ONE, the lone flag variant, c29..c30 the
// mapped body's layout and c31 (gain x 2, edge, core x 2, normaliser) while drawing, the output the background plus 2 x
// the model x the fade, RT2.g = 32 x max(rgb) with RT1 masked off and RT2 GREEN; end -> the game's program, the
// application's c29..c31, DESTBLEND INVSRCCOLOR, and the stand-in's VS, its c10..c12 and the PS colour matrix c0..c2
// untouched. The route case starts from the lone draw's own state (FL 68 A: the route refused every lone draw for
// `state`): Z write on, alpha test on (GREATEREQUAL / 1), colour mask 7, sRGB write on, separate alpha on (ZERO / ZERO);
// the route's verdict (bullet_reshade_route.h route_state_refuses, read back as the shadow holds it) admits it as a
// lone draw and refuses it as a non-bullet draw; the sequence writes the four states the route requires (alpha test and
// Z test kept), the flag targets are bound and unbound by bullet_reshade_route.h flag_bind / flag_unbind (MotionOutput's
// code), the output and the flag are the batch's, and after end and flag_unbind the four are back, RT1 / RT2 unbound,
// the application's masks back (alpha test, its function, reference and the alpha factors untouched); the verdict
// table: each bit alone, the exact state, a SRCALPHA and a blend-off draw; (b3) the flag outside the route below.
// (c) Cost: the recognition per candidate draw (the node read and a 30-body map: the worst hit and a miss, an engine
// glow's), and the indexed draw loop with and without the sequence, and with it from the lone draw's own state (the
// route's four states written and put back each draw; best of 7 runs of 2000).
void lone_reshade_cases() {
    namespace rf = reshade_fx;
    namespace rs = x3m::weapon_effects::reshade;
    // (a) recognition
    constexpr std::uint32_t ire_body = 1207u, ppc_body = 1518u, hull_body = 333u;
    static wc::Table table{}; // zero: the fields below are the whole table
    table.rows = 3;
    for (unsigned r = 0; r < 3; ++r) table.bits[r] = wc::row_present;
    table.body_hash[0] = table.body_hash[2] = 0x1e1e1e1eu; // rows 0 and 2 share the IRE body (as the flak fragments)
    table.body_hash[1] = 0x50c50c50u;
    table.bullet_bodies = 2;
    table.bullet_body_hash[0] = 0x1e1e1e1eu;
    table.bullet_body_hash[1] = 0x50c50c50u;
    const std::int32_t ire_words[8] = {3, 57881, 56830, 6546, 23063, 18300, 858, 64010};
    const std::int32_t ppc_words[8] = {3, 58354, 58106, 6239, 48926, 40824, 27736, 60073};
    std::memcpy(table.bullet_body_layout[0], ire_words, sizeof ire_words);
    std::memcpy(table.bullet_body_layout[1], ppc_words, sizeof ppc_words);
    std::vector<unsigned char> tbullets(3 * wc::layout::type_stride, 0);
    const std::uint16_t row_ids[3] = {std::uint16_t(ire_body), std::uint16_t(ppc_body), std::uint16_t(ire_body)};
    for (unsigned r = 0; r < 3; ++r) std::memcpy(&tbullets[r * wc::layout::type_stride + rs::tbullets_body], &row_ids[r], 2);
    const std::uint32_t table_slot = std::uint32_t(reinterpret_cast<std::uintptr_t>(tbullets.data())), count_slot = 3;
    const auto reader = [](std::uintptr_t a, void* out, std::size_t n) { return x3m::engine_memory::read(a, out, n); };
    x3m::engine_memory::next_frame();
    rs::BodyMap map;
    const bool built = rs::build_body_map(map, &table, reinterpret_cast<std::uintptr_t>(&table_slot),
                                          reinterpret_cast<std::uintptr_t>(&count_slot), reader) &&
                       map.state == 1 && map.count == 2;
    std::vector<std::uint32_t> node(0x160 / 4, 0u);
    const auto recognise = [&](std::uint64_t vs, bool blend, DWORD src, DWORD dst, DWORD op, std::uint32_t id,
                               const std::int32_t** words) -> unsigned {
        *words = nullptr;
        if (!rs::lone_vertex_shader(vs)) return rs::lone_none;
        const unsigned b = rs::lone_blend(blend, src, dst, op);
        if (b == rs::lone_none) return b;
        node[rs::node_body_offset / 4] = id;
        std::uint32_t got = 0;
        if (!x3m::engine_memory::read(reinterpret_cast<std::uintptr_t>(node.data()) + rs::node_body_offset, &got, sizeof got))
            return rs::lone_none;
        *words = rs::body_layout(map, &table, got);
        return *words ? b : rs::lone_none;
    };
    constexpr std::uint64_t effects_default = 0xd5e1c75351ed3f04ull, effects_instance = 0x89193868c61c3846ull;
    const struct {
        const char* name;
        std::uint64_t vs;
        bool blend;
        DWORD src, dst, op;
        std::uint32_t id;
        unsigned want;
        const std::int32_t* words;
    } rows_a[] = {
        {"ire_default_screen", effects_default, true, D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, D3DBLENDOP_ADD, ire_body,
         rs::lone_screen, table.bullet_body_layout[0]},
        {"ppc_instance_screen", effects_instance, true, D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, D3DBLENDOP_ADD, ppc_body,
         rs::lone_screen, table.bullet_body_layout[1]},
        {"ire_additive", effects_default, true, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD, ire_body, rs::lone_additive,
         table.bullet_body_layout[0]},
        {"hull_body", effects_default, true, D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, D3DBLENDOP_ADD, hull_body, rs::lone_none,
         nullptr},
        {"instance_bullets_vs", 0x5e484a06672e28fbull, true, D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, D3DBLENDOP_ADD, ire_body,
         rs::lone_none, nullptr},
        {"standard_lighting_vs", 0x494fe349b8bc12ecull, true, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD, ire_body,
         rs::lone_none, nullptr},
        {"srcalpha", effects_default, true, D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA, D3DBLENDOP_ADD, ire_body, rs::lone_none,
         nullptr},
        {"blend_off", effects_default, false, D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, D3DBLENDOP_ADD, ire_body, rs::lone_none,
         nullptr},
        {"revsubtract", effects_default, true, D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, D3DBLENDOP_REVSUBTRACT, ire_body,
         rs::lone_none, nullptr},
    };
    bool recognition = built;
    for (const auto& r : rows_a) {
        const std::int32_t* words = nullptr;
        const unsigned got = recognise(r.vs, r.blend, r.src, r.dst, r.op, r.id, &words);
        const bool ok = got == r.want && words == r.words;
        recognition = recognition && ok;
        std::printf("RESHADE case=lone_recognition row=%s body=%lu class=%u want=%u layout=%u ok=%u\n", r.name,
                    static_cast<unsigned long>(r.id), got, r.want, unsigned(words != nullptr), unsigned(ok));
    }
    std::printf("RESHADE case=lone_map built=%u state=%u bodies_mapped=%u reason=%s\n", unsigned(built), unsigned(map.state),
                map.count, map.reason);
    report("reshade_lone_recognised_by_body_id", recognition, "");
    // (b) the device sequence on the lone stand-in pair
    LPD3DXBUFFER vs_hold = nullptr, ps_hold = nullptr;
    const DWORD *vs_code = nullptr, *ps_code = nullptr;
    const bool compiled = compile_shader && compile(rf::lone_vs_source, "vs_2_0", &vs_code, &vs_hold) &&
                          compile(rf::lone_ps_source, "ps_2_0", &ps_code, &ps_hold);
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> vanilla, ours, ours_flag;
    Com<IDirect3DVertexDeclaration9> decl;
    const bool created = compiled && SUCCEEDED(dev->CreateVertexShader(vs_code, &vs.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(ps_code, &vanilla.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(rf::lone_program, &ours.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(rf::lone_flag_program, &ours_flag.p)) &&
                         SUCCEEDED(dev->CreateVertexDeclaration(rf::lone_declaration(), &decl.p));
    if (vs_hold) vs_hold->Release();
    if (ps_hold) ps_hold->Release();
    std::printf("RESHADE case=lone_programs compiled=%u created=%u lone_words=%u lone_flag_words=%u\n", unsigned(compiled),
                unsigned(created), unsigned(sizeof rf::lone_program / sizeof rf::lone_program[0]),
                unsigned(sizeof rf::lone_flag_program / sizeof rf::lone_flag_program[0]));
    report("reshade_lone_programs_create", created, "");
    if (!created) return;
    const auto tune = x3m::weapon_effects::projectile_family::baked;
    float constants[4];
    rs::constants(tune, constants);
    float ire_layout[8];
    rs::layout_constants(ire_words, tune, ire_layout);
    const float app_block[12] = {3.f, -1.f, 0.25f, 9.f, -4.f, 0.5f, 2.f, -0.75f, 0.125f, -2.5f, 7.f, 0.0625f};
    Com<IDirect3DTexture9> rt[3], flat;
    Com<IDirect3DSurface9> rt_s[3], sys;
    Com<IDirect3DVertexBuffer9> vb;
    Com<IDirect3DIndexBuffer9> ib;
    bool ok = SUCCEEDED(dev->CreateOffscreenPlainSurface(16, 16, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys.p, nullptr)) &&
              SUCCEEDED(dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &flat.p, nullptr)) &&
              SUCCEEDED(dev->CreateVertexBuffer(4 * sizeof(rf::LoneVertex), D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &vb.p,
                                                nullptr)) &&
              SUCCEEDED(dev->CreateIndexBuffer(6 * sizeof(std::uint16_t), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_MANAGED,
                                               &ib.p, nullptr));
    for (unsigned i = 0; i < 3 && ok; ++i)
        ok = SUCCEEDED(dev->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt[i].p,
                                          nullptr)) &&
             SUCCEEDED(rt[i].p->GetSurfaceLevel(0, &rt_s[i].p));
    if (ok) {
        D3DLOCKED_RECT lr{};
        void* p = nullptr;
        ok = SUCCEEDED(flat.p->LockRect(0, &lr, nullptr, 0));
        if (ok) {
            unsigned char* b = static_cast<unsigned char*>(lr.pBits);
            b[0] = 17, b[1] = 77, b[2] = 99, b[3] = 70; // t = (99, 77, 17) / 255
            flat.p->UnlockRect(0);
        }
        const rf::LoneVertex quad[4] = {{-1, -1, 1, 0, 0}, {-1, 1, 1, 0, 0}, {1, 1, 1, 0, 0}, {1, -1, 1, 0, 0}};
        ok = ok && SUCCEEDED(vb.p->Lock(0, 0, &p, 0));
        if (ok) {
            std::memcpy(p, quad, sizeof quad);
            vb.p->Unlock();
        }
        const std::uint16_t idx[6] = {0, 1, 2, 0, 2, 3};
        ok = ok && SUCCEEDED(ib.p->Lock(0, 0, &p, 0));
        if (ok) {
            std::memcpy(p, idx, sizeof idx);
            ib.p->Unlock();
        }
    }
    report("reshade_lone_targets", ok, "");
    if (!ok) return;
    const float t[3] = {99 / 255.f, 77 / 255.f, 17 / 255.f}, fade = 0.8f, gain = 2.f, bg = 64 / 255.f;
    float model[3];
    rs::model(constants, t, fade, model);
    const bool mrt = caps.NumSimultaneousRTs >= 3 && (caps.PrimitiveMiscCaps & D3DPMISCCAPS_INDEPENDENTWRITEMASKS);
    const auto read_px = [&](IDirect3DSurface9* s, float out[4]) {
        if (FAILED(dev->GetRenderTargetData(s, sys.p))) return false;
        D3DLOCKED_RECT o{};
        if (FAILED(sys.p->LockRect(&o, nullptr, D3DLOCK_READONLY))) return false;
        const auto* px = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(o.pBits) + 8 * o.Pitch) + 8 * 4;
        for (unsigned k = 0; k < 4; ++k) out[k] = half_to_float(px[k]);
        sys.p->UnlockRect();
        return true;
    };
    IDirect3DSurface9* old_rt = nullptr;
    dev->GetRenderTarget(0, &old_rt);
    float rows[16];
    rf::clip_rows(1.f, 1.f, rows);
    const auto bind = [&] {
        rf::bind_lone_pass(dev, vs.p, decl.p, flat.p, rows, false, fade);
        dev->SetStreamSource(0, vb.p, 0, sizeof(rf::LoneVertex));
        dev->SetIndices(ib.p);
        dev->SetPixelShader(vanilla.p);
        dev->SetPixelShaderConstantF(rs::block_register, app_block, rs::block_count);
    };
    // the stand-in's own output and our lone program's, blend off (the programs' outputs themselves)
    float stand[4]{}, plain[4]{};
    for (unsigned which = 0; which < 2 && ok; ++which) {
        dev->SetRenderTarget(0, rt_s[0].p);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
        dev->BeginScene();
        bind();
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        float block[12];
        for (unsigned i = 0; i < 8; ++i) block[i] = rs::identity_layout[i];
        for (unsigned i = 0; i < 4; ++i) block[8 + i] = constants[i];
        if (which) {
            dev->SetPixelShaderConstantF(rs::block_register, block, rs::block_count);
            dev->SetPixelShader(ours.p);
        }
        ok = SUCCEEDED(dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2));
        dev->EndScene();
        ok = ok && read_px(rt_s[0].p, which ? plain : stand);
    }
    float worst_stand = 0.f, worst_plain = 0.f;
    for (unsigned k = 0; k < 3; ++k) {
        const float ws = t[k] * fade;
        worst_stand = std::max(worst_stand, std::fabs(stand[k] - ws) / (0.004f + 0.004f * ws));
        worst_plain = std::max(worst_plain, std::fabs(plain[k] - model[k]) / (0.004f + 0.004f * model[k]));
    }
    std::printf("RESHADE case=lone_program fade=%.2f standin=%.4f,%.4f,%.4f standin_want=%.4f,%.4f,%.4f lone=%.4f,%.4f,%.4f "
                "model=%.4f,%.4f,%.4f worst_standin=%.3f worst_lone=%.3f\n",
                double(fade), double(stand[0]), double(stand[1]), double(stand[2]), double(t[0] * fade),
                double(t[1] * fade), double(t[2] * fade), double(plain[0]), double(plain[1]), double(plain[2]),
                double(model[0]), double(model[1]), double(model[2]), double(worst_stand), double(worst_plain));
    report("reshade_lone_program_matches_model", ok && worst_stand <= 1.f && worst_plain <= 1.f, "");
    // the route's path (converted, flagged with MRT), from the lone draw's own state: lone_app (lone_states order: Z
    // write on, colour mask 7, sRGB write on, separate alpha on, measured on the effects DEFAULT draws with ZERO / ZERO
    // alpha factors) and alpha test on (the effects pass's GREATEREQUAL / 1), which no path writes; Z test as bound.
    const DWORD lone_app[rs::lone_state_count] = {TRUE, 7u, TRUE, TRUE};
    const auto lone_own_state = [&] {
        for (unsigned i = 0; i < rs::lone_state_count; ++i) dev->SetRenderState(rs::lone_states[i], lone_app[i]);
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
        dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
        dev->SetRenderState(D3DRS_ALPHAREF, 1);
        dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ZERO);
        dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
    };
    // the states no path writes, unchanged: alpha test (on, GREATEREQUAL / 1), the separate alpha factors, Z test
    const auto untouched = [&](DWORD zenable_want) {
        DWORD t = 0, func = 0, ref = ~0u, src_a = 0, dst_a = 0, z = ~0u;
        return SUCCEEDED(dev->GetRenderState(D3DRS_ALPHATESTENABLE, &t)) && t == TRUE &&
               SUCCEEDED(dev->GetRenderState(D3DRS_ALPHAFUNC, &func)) && func == D3DCMP_GREATEREQUAL &&
               SUCCEEDED(dev->GetRenderState(D3DRS_ALPHAREF, &ref)) && ref == 1 &&
               SUCCEEDED(dev->GetRenderState(D3DRS_SRCBLENDALPHA, &src_a)) && src_a == D3DBLEND_ZERO &&
               SUCCEEDED(dev->GetRenderState(D3DRS_DESTBLENDALPHA, &dst_a)) && dst_a == D3DBLEND_ZERO &&
               SUCCEEDED(dev->GetRenderState(D3DRS_ZENABLE, &z)) && z == zenable_want;
    };
    // the lone states equal want[] for the bits in `which`, lone_app for the others
    const auto lone_states_are = [&](unsigned which, const DWORD* want) {
        for (unsigned i = 0; i < rs::lone_state_count; ++i) {
            DWORD v = ~0u;
            if (FAILED(dev->GetRenderState(rs::lone_states[i], &v)) || v != ((which & (1u << i)) ? want[i] : lone_app[i]))
                return false;
        }
        return true;
    };
    // the application's flag masks (MotionOutput saves and restores them through the shadow)
    constexpr DWORD app_write1 = 5u, app_write2 = 9u;
    // after flag_unbind: RT1 and RT2 unbound, the application's masks back
    const auto flag_targets_restored = [&] {
        DWORD w1 = 0, w2 = 0;
        IDirect3DSurface9 *s1 = nullptr, *s2 = nullptr;
        const bool ok1 = dev->GetRenderTarget(1, &s1) == D3DERR_NOTFOUND && dev->GetRenderTarget(2, &s2) == D3DERR_NOTFOUND;
        if (s1) s1->Release();
        if (s2) s2->Release();
        return ok1 && SUCCEEDED(dev->GetRenderState(D3DRS_COLORWRITEENABLE1, &w1)) && w1 == app_write1 &&
               SUCCEEDED(dev->GetRenderState(D3DRS_COLORWRITEENABLE2, &w2)) && w2 == app_write2;
    };
    bool during_all = true, after_all = true, verdict_ok = true, states_all = true;
    float worst = 0.f, flag_got = -1.f, flag_want = 0.f;
    for (unsigned variant = 0; variant < (mrt ? 2u : 1u) && ok; ++variant) {
        const bool flag = variant == 1;
        if (flag) {
            dev->SetRenderTarget(0, rt_s[2].p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
        }
        dev->SetRenderTarget(0, rt_s[0].p);
        dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 64, 64, 64), 1.f, 0);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE1, app_write1);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE2, app_write2);
        dev->BeginScene();
        bind();
        lone_own_state();
        // the route's verdict on that state read back as MotionOutput's shadow holds it: the lone draw admitted, the
        // same state on a non-bullet draw refused `state` with its four route bits
        rs::ScreenState screen;
        for (unsigned i = 0; i < rs::lone_state_count; ++i) dev->GetRenderState(rs::lone_states[i], &screen.lone[i]);
        dev->GetRenderState(D3DRS_ALPHABLENDENABLE, &screen.blend);
        dev->GetRenderState(D3DRS_SRCBLEND, &screen.src);
        dev->GetRenderState(D3DRS_DESTBLEND, &screen.dst);
        dev->GetRenderState(D3DRS_BLENDOP, &screen.op);
        const unsigned differs = rs::state_differs(screen);
        verdict_ok = verdict_ok && differs == rs::lone_state_bits && rs::route_state_refuses(screen, true) == 0u &&
                     rs::route_state_refuses(screen, false) == rs::lone_route_bits;
        DWORD zenable_before = 0;
        dev->GetRenderState(D3DRS_ZENABLE, &zenable_before);
        // the route's flag targets (bullet_reshade_route.h flag_bind, as MotionOutput::bind_bolt_flag_targets)
        auto fops = rf::flag_ops(dev, rt_s[1].p, rt_s[2].p);
        rf::FlagTargets targets;
        HRESULT bind_back = S_OK;
        if (flag) ok = ok && SUCCEEDED(rs::flag_bind(fops, targets, &bind_back)) && targets.bolt_flag;
        auto ops = rf::device_ops(dev);
        rs::Plan plan;
        plan.program = flag ? ours_flag.p : ours.p;
        plan.constants = constants;
        plan.gain = gain;
        plan.layout = ire_layout;
        plan.convert = true;
        plan.states = differs & rs::lone_route_bits; // begin_bullet_reshade's plan for a lone draw on the route
        plan.state_app = screen.lone;
        rs::Applied applied;
        ok = ok && SUCCEEDED(rs::begin(ops, plan, app_block, applied)) && applied.destblend && applied.block && applied.program &&
             applied.states == rs::lone_route_bits;
        const bool states_during = ok && lone_states_are(rs::lone_route_bits, rs::lone_values) && untouched(zenable_before);
        DWORD blend = 0;
        IDirect3DPixelShader9* bound = nullptr;
        float block[12]{};
        const float* const c31 = block + 8;
        const bool during = ok && SUCCEEDED(dev->GetRenderState(D3DRS_DESTBLEND, &blend)) && blend == D3DBLEND_ONE &&
                            SUCCEEDED(dev->GetPixelShader(&bound)) && bound == plan.program &&
                            SUCCEEDED(dev->GetPixelShaderConstantF(rs::block_register, block, rs::block_count)) &&
                            !std::memcmp(block, ire_layout, sizeof ire_layout) && c31[0] == constants[0] * gain &&
                            c31[1] == constants[1] && c31[2] == constants[2] * gain && c31[3] == constants[3];
        if (bound) bound->Release();
        bound = nullptr;
        ok = ok && SUCCEEDED(dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2));
        const HRESULT back = rs::end(ops, applied, vanilla.p, app_block);
        const HRESULT flag_back = rs::flag_unbind(fops, targets); // the production restore
        IDirect3DVertexShader9* bound_vs = nullptr;
        float vs_c[12]{}, ps_c[12]{};
        const float tex_matrix[8] = {1, 0, 0, 0, 0, 1, 0, 0}, colour_matrix[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
        const bool after = SUCCEEDED(back) && SUCCEEDED(flag_back) && SUCCEEDED(dev->GetRenderState(D3DRS_DESTBLEND, &blend)) &&
                           blend == D3DBLEND_INVSRCCOLOR && SUCCEEDED(dev->GetPixelShader(&bound)) && bound == vanilla.p &&
                           SUCCEEDED(dev->GetPixelShaderConstantF(rs::block_register, block, rs::block_count)) &&
                           !std::memcmp(block, app_block, sizeof block) && SUCCEEDED(dev->GetVertexShader(&bound_vs)) &&
                           bound_vs == vs.p && SUCCEEDED(dev->GetVertexShaderConstantF(10, vs_c, 3)) &&
                           !std::memcmp(vs_c, tex_matrix, sizeof tex_matrix) && vs_c[8] == fade &&
                           SUCCEEDED(dev->GetPixelShaderConstantF(0, ps_c, 3)) &&
                           !std::memcmp(ps_c, colour_matrix, sizeof ps_c) && flag_targets_restored();
        // the lone draw's own states back, alpha test, its function / reference, the alpha factors and Z test untouched
        const bool states_after = SUCCEEDED(back) && lone_states_are(0u, rs::lone_values) && untouched(zenable_before);
        states_all = states_all && states_during && states_after;
        if (bound) bound->Release();
        if (bound_vs) bound_vs->Release();
        dev->EndScene();
        during_all = during_all && during;
        after_all = after_all && after;
        float px[4]{};
        ok = ok && read_px(rt_s[0].p, px);
        for (unsigned k = 0; k < 3 && ok; ++k) {
            const float want = bg + gain * model[k];
            worst = std::max(worst, std::fabs(px[k] - want) / (0.004f + 0.004f * want));
        }
        if (flag && ok && read_px(rt_s[2].p, px)) {
            flag_got = px[1];
            flag_want = 32.f * gain * std::max(model[0], std::max(model[1], model[2]));
        }
    }
    dev->SetRenderTarget(0, old_rt);
    if (old_rt) old_rt->Release();
    const auto device_defaults = [&] { // bind_pass resets the four lone states; the rest here
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        dev->SetRenderState(D3DRS_SRCBLENDALPHA, D3DBLEND_ONE);
        dev->SetRenderState(D3DRS_DESTBLENDALPHA, D3DBLEND_ZERO);
        dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);
        dev->SetRenderState(D3DRS_ALPHAREF, 0);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE1, 15);
        dev->SetRenderState(D3DRS_COLORWRITEENABLE2, 15);
        for (unsigned i = 0; i < rs::lone_state_count; ++i) dev->SetRenderState(rs::lone_states[i], rs::lone_values[i]);
    };
    device_defaults();
    const bool flag_ok = !mrt || std::fabs(flag_got - flag_want) <= 0.01f * flag_want + 0.01f;
    std::printf("RESHADE case=lone_additive gain=%.1f fade=%.2f during=%u restored=%u worst_rel=%.3f mrt=%u flag_g=%.3f "
                "flag_want=%.3f lone_state=zwrite:%lu,mask:%lu,srgb:%lu,sepalpha:%lu,alphatest:1 verdict=%u "
                "states_normalised_and_restored=%u\n",
                double(gain), double(fade), unsigned(during_all), unsigned(after_all), double(worst), unsigned(mrt),
                double(flag_got), double(flag_want), static_cast<unsigned long>(lone_app[0]),
                static_cast<unsigned long>(lone_app[1]), static_cast<unsigned long>(lone_app[2]),
                static_cast<unsigned long>(lone_app[3]), unsigned(verdict_ok), unsigned(states_all));
    report("reshade_lone_route_binds_converts_flags_restores", ok && during_all && after_all && worst <= 1.f && flag_ok, "");
    report("reshade_lone_state_admitted_normalised_restored", ok && verdict_ok && states_all, "");
    {
        // the verdict alone (bullet_reshade_route.h route_state_refuses, the code prepare_screen_additive runs): the exact
        // native screen state admits any draw; each lone_states bit off refuses a non-bullet draw and never a lone one; a
        // blend other than ONE / INVSRCCOLOR / ADD refuses both
        rs::ScreenState exact;
        for (unsigned i = 0; i < rs::lone_state_count; ++i) exact.lone[i] = rs::lone_values[i];
        exact.blend = TRUE, exact.src = D3DBLEND_ONE, exact.dst = D3DBLEND_INVSRCCOLOR, exact.op = D3DBLENDOP_ADD;
        bool table_ok = rs::route_state_refuses(exact, false) == 0u && rs::route_state_refuses(exact, true) == 0u;
        for (unsigned i = 0; i < rs::lone_state_count; ++i) {
            rs::ScreenState s = exact;
            s.lone[i] = lone_app[i];
            table_ok = table_ok && rs::state_differs(s) == (1u << i) && rs::route_state_refuses(s, false) == (1u << i) &&
                       rs::route_state_refuses(s, true) == 0u;
        }
        rs::ScreenState srcalpha = exact;
        srcalpha.src = D3DBLEND_SRCALPHA;
        rs::ScreenState off = exact;
        off.blend = FALSE;
        table_ok = table_ok && rs::route_state_refuses(srcalpha, true) == rs::state_blend &&
                   rs::route_state_refuses(off, false) == rs::state_blend;
        std::printf("RESHADE case=lone_state_verdict rows=%u ok=%u\n", 4u + rs::lone_state_count, unsigned(table_ok));
        report("reshade_lone_state_verdict", table_ok, "");
    }
    // (b3) the far flag outside the route (motion_output_bullet_reshade_inc.h lone_native_flag): a lone draw the route
    // never takes, under the game's own blend, from the lone draw's own state (every lone_states bit off, alpha test on),
    // with RT2 holding the far-depth lane base 1.0 (the station's z/w limit; the composite flags g > 1). ONE / ONE: the
    // flag variant through the plain sequence (no conversion, gain 1, Z write the only state written) with the flag
    // targets bound and unbound by bullet_reshade_route.h flag_bind / flag_unbind (MotionOutput's code): RT2.g = base +
    // 32 x max(model) > 1 with the colour mask 7, sRGB write, separate alpha and alpha test the game's; after the
    // sequence's end and flag_unbind, Z write the game's, RT1 and RT2 unbound and the application's masks back. RT0 is
    // checked against bg + model when this device does not sRGB-encode the FP16 target (D3DUSAGE_QUERY_SRGBWRITE). ONE /
    // INVSRCCOLOR (the witness for bullet_reshade_route.h lone_native_flag): the same draw leaves RT2.g = s + base
    // (1 - s) = 1, no flag, so the proxy refuses it (screen_unrouted) instead of binding the targets.
    if (mrt && ok) {
        bool srgb_fp16 = false;
        {
            IDirect3D9* d3d = nullptr;
            D3DDEVICE_CREATION_PARAMETERS cp{};
            D3DDISPLAYMODE mode{};
            if (SUCCEEDED(dev->GetDirect3D(&d3d)) && SUCCEEDED(dev->GetCreationParameters(&cp)) &&
                SUCCEEDED(d3d->GetAdapterDisplayMode(cp.AdapterOrdinal, &mode)))
                srgb_fp16 = SUCCEEDED(d3d->CheckDeviceFormat(cp.AdapterOrdinal, cp.DeviceType, mode.Format,
                                                            D3DUSAGE_RENDERTARGET | D3DUSAGE_QUERY_SRGBWRITE,
                                                            D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F));
            if (d3d) d3d->Release();
        }
        float native_rt0[4]{}, native_g = -1.f, witness_g = -1.f, worst_native = 0.f;
        bool native_during = true, native_after = true;
        const float s_flag = 32.f * std::max(model[0], std::max(model[1], model[2]));
        const DWORD zwrite_off[rs::lone_state_count] = {FALSE, 0, 0, 0};
        for (unsigned pass = 0; pass < 2 && ok; ++pass) {
            const bool additive = pass == 0;
            dev->SetRenderTarget(0, rt_s[2].p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 255, 255, 255), 1.f, 0); // the lane: base 1.0
            dev->SetRenderTarget(0, rt_s[0].p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 64, 64, 64), 1.f, 0);
            dev->SetRenderState(D3DRS_COLORWRITEENABLE1, app_write1);
            dev->SetRenderState(D3DRS_COLORWRITEENABLE2, app_write2);
            dev->BeginScene();
            bind();
            dev->SetRenderState(D3DRS_DESTBLEND, additive ? D3DBLEND_ONE : D3DBLEND_INVSRCCOLOR);
            lone_own_state();
            DWORD zenable_before = 0;
            dev->GetRenderState(D3DRS_ZENABLE, &zenable_before);
            const unsigned blend_class =
                rs::lone_blend(true, D3DBLEND_ONE, additive ? D3DBLEND_ONE : D3DBLEND_INVSRCCOLOR, D3DBLENDOP_ADD);
            ok = ok && blend_class == (additive ? rs::lone_additive : rs::lone_screen) &&
                 rs::lone_native_flag(blend_class) == additive;
            auto fops = rf::flag_ops(dev, rt_s[1].p, rt_s[2].p);
            rf::FlagTargets targets;
            HRESULT bind_back = S_OK;
            ok = ok && SUCCEEDED(rs::flag_bind(fops, targets, &bind_back)) && targets.bolt_flag;
            rs::ScreenState own;
            for (unsigned i = 0; i < rs::lone_state_count; ++i) own.lone[i] = lone_app[i];
            auto ops = rf::device_ops(dev);
            rs::Plan plan;
            plan.program = ours_flag.p;
            plan.constants = constants;
            plan.layout = ire_layout;
            plan.states = rs::state_differs(own) & rs::lone_plain_bits; // begin_bullet_reshade's plan off the route
            plan.state_app = lone_app;
            rs::Applied applied;
            ok = ok && SUCCEEDED(rs::begin(ops, plan, app_block, applied)) && !applied.destblend &&
                 applied.states == rs::state_zwrite;
            DWORD v = 0;
            if (additive)
                native_during = lone_states_are(rs::state_zwrite, zwrite_off) && untouched(zenable_before) &&
                                SUCCEEDED(dev->GetRenderState(D3DRS_DESTBLEND, &v)) && v == D3DBLEND_ONE;
            ok = ok && SUCCEEDED(dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2));
            const HRESULT back = rs::end(ops, applied, vanilla.p, app_block);
            const HRESULT flag_back = rs::flag_unbind(fops, targets); // MotionOutput::finish_lone_native_flag's restore
            if (additive)
                native_after = SUCCEEDED(back) && SUCCEEDED(flag_back) && lone_states_are(0u, rs::lone_values) &&
                               untouched(zenable_before) && SUCCEEDED(dev->GetRenderState(D3DRS_DESTBLEND, &v)) &&
                               v == D3DBLEND_ONE && flag_targets_restored();
            dev->EndScene();
            float px[4]{};
            if (additive) {
                ok = ok && read_px(rt_s[0].p, native_rt0);
                for (unsigned k = 0; k < 3 && ok; ++k) {
                    const float want = bg + model[k];
                    worst_native = std::max(worst_native, std::fabs(native_rt0[k] - want) / (0.004f + 0.004f * want));
                }
            }
            ok = ok && read_px(rt_s[2].p, px);
            (additive ? native_g : witness_g) = px[1];
        }
        device_defaults();
        dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCCOLOR);
        dev->SetRenderTarget(0, rt_s[0].p);
        const bool flag_native = native_g > 1.f && std::fabs(native_g - (1.f + s_flag)) <= 0.01f * s_flag + 0.01f;
        const bool rt0_ok = srgb_fp16 || worst_native <= 1.f;
        const bool witness = witness_g <= 1.001f; // s + base (1 - s) = 1 at base 1: below the composite's g > 1
        std::printf("RESHADE case=lone_native_flag blend=one_one base=1.0 rt2_g=%.3f want=%.3f rt0_worst_rel=%.3f "
                    "srgb_write_fp16=%u during=%u restored=%u\n",
                    double(native_g), double(1.f + s_flag), double(worst_native), unsigned(srgb_fp16),
                    unsigned(native_during), unsigned(native_after));
        std::printf("RESHADE case=lone_flag_invsrccolor_witness blend=one_invsrccolor base=1.0 rt2_g=%.4f s=%.3f "
                    "flag_test=%u refused=%u\n",
                    double(witness_g), double(s_flag), unsigned(witness_g > 1.f),
                    unsigned(!rs::lone_native_flag(rs::lone_screen)));
        report("reshade_lone_native_flag_one_one", ok && flag_native && rt0_ok && native_during && native_after, "");
        report("reshade_lone_flag_invsrccolor_witness", ok && witness && !rs::lone_native_flag(rs::lone_screen), "");
    }
    // (c) cost
    LARGE_INTEGER freq{}, a{}, b{};
    QueryPerformanceFrequency(&freq);
    Com<IDirect3DQuery9> fence;
    dev->CreateQuery(D3DQUERYTYPE_EVENT, &fence.p);
    const auto finish = [&] {
        if (!fence.p) return;
        fence.p->Issue(D3DISSUE_END);
        while (fence.p->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
        }
    };
    constexpr unsigned draws = 2000, runs = 7;
    double best_vanilla = 1e30, best_reshaded = 1e30, best_normalised = 1e30, best_hit = 1e30, best_miss = 1e30;
    dev->SetRenderTarget(0, rt_s[0].p);
    for (unsigned run = 0; run < runs && ok; ++run) {
        dev->BeginScene();
        bind();
        finish();
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i) dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);
        finish();
        QueryPerformanceCounter(&b);
        best_vanilla = std::min(best_vanilla, ticks_us(a, b, freq) / draws);
        QueryPerformanceCounter(&a);
        bool restored = true;
        for (unsigned i = 0; i < draws; ++i)
            rf::lone_reshaded_draw(dev, ours.p, vanilla.p, constants, app_block, 4, 2, &restored, ire_layout);
        finish();
        QueryPerformanceCounter(&b);
        best_reshaded = std::min(best_reshaded, ticks_us(a, b, freq) / draws);
        // the same with the lone draw's own state (lone_app): on the route the four states written and put back per draw
        for (unsigned i = 0; i < rs::lone_state_count; ++i) dev->SetRenderState(rs::lone_states[i], lone_app[i]);
        finish();
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i)
            rf::lone_reshaded_draw(dev, ours.p, vanilla.p, constants, app_block, 4, 2, &restored, ire_layout,
                                   rs::lone_route_bits, lone_app);
        finish();
        QueryPerformanceCounter(&b);
        best_normalised = std::min(best_normalised, ticks_us(a, b, freq) / draws);
        ok = ok && restored;
        dev->EndScene();
    }
    device_guard("reshade_lone_cost");
    {
        // a 30-body map (the FL table's 30 bullet bodies): the last id (the longest hit) and an unmapped id (a miss: every
        // engine glow or other effects draw with the same VS and blend pays it)
        rs::BodyMap big;
        big.state = 1;
        big.count = 30;
        for (unsigned i = 0; i < 30; ++i) big.id[i] = 0x100u + 7u * i, big.entry[i] = 0;
        unsigned hits = 0;
        for (unsigned pass = 0; pass < 2; ++pass) {
            node[rs::node_body_offset / 4] = pass ? 0x9999u : big.id[29];
            for (unsigned run = 0; run < runs; ++run) {
                QueryPerformanceCounter(&a);
                for (unsigned i = 0; i < draws; ++i) {
                    std::uint32_t id = 0;
                    if (rs::lone_vertex_shader(effects_default) &&
                        rs::lone_blend(true, D3DBLEND_ONE, D3DBLEND_INVSRCCOLOR, D3DBLENDOP_ADD) != rs::lone_none &&
                        x3m::engine_memory::read(reinterpret_cast<std::uintptr_t>(node.data()) + rs::node_body_offset, &id,
                                                 sizeof id) &&
                        rs::body_layout(big, &table, id))
                        ++hits;
                }
                QueryPerformanceCounter(&b);
                (pass ? best_miss : best_hit) = std::min(pass ? best_miss : best_hit, ticks_us(a, b, freq) / draws);
            }
        }
        ok = ok && hits == draws * runs;
    }
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetTexture(0, nullptr);
    dev->SetStreamSource(0, nullptr, 0, 0);
    dev->SetIndices(nullptr);
    for (unsigned i = 0; i < rs::lone_state_count; ++i) dev->SetRenderState(rs::lone_states[i], rs::lone_values[i]);
    std::printf("RESHADE case=lone_cost draws=%u runs=%u draw_us_vanilla=%.3f draw_us_reshaded=%.3f "
                "substitution_us_per_draw=%.3f draw_us_normalised=%.3f normalise_us_per_draw=%.3f match_us_hit_30=%.4f "
                "match_us_miss_30=%.4f fenced=%u\n",
                draws, runs, best_vanilla, best_reshaded, best_reshaded - best_vanilla, best_normalised,
                best_normalised - best_reshaded, best_hit, best_miss, unsigned(fence.p != nullptr));
    report("reshade_lone_cost_measured", ok && fence.p && best_hit < 10.0 && best_miss < 10.0, "");
}
// The bullet re-shade's per-texture normaliser (bullet_reshade_core.h; bullet-draw-batch.md section 6): the production
// texture_key over a table built as weapon_effects_table.h stores tables.bullet_textures (two entries: a 349680-byte
// buffer, fx_bullets_diff's byte count, and another size): the buffer is entry 0; one byte changed past the head, a
// changed head, another size and no table are none; texture_scale gives the anchor's 2.75 at fx_bullets_diff's p99
// (0.6169) and 2.75 x 0.6169 / 0.9888 at fx_bullets2_diff's; the device sequence's plan uploads the draw's normaliser
// in c31.w. The cost of the observer's match at creation: a size miss, a same-size head miss, a full match (best of 7
// runs). RESHADE case=texture_key rows.
void reshade_texture_cases() {
    namespace rs = x3m::weapon_effects::reshade;
    namespace core = x3m::weapon_effects::core;
    std::vector<unsigned char> bytes(349680);
    std::uint32_t seed = 0x5eedu;
    for (auto& b : bytes) {
        seed = seed * 1664525u + 1013904223u;
        b = (unsigned char)(seed >> 24);
    }
    core::Table t;
    const auto q = [](float p99) { return std::uint16_t(p99 * 65535.f + 0.5f); };
    t.bullet_textures = 2;
    t.bullet_texture_bytes[0] = std::uint32_t(bytes.size());
    t.bullet_texture_head[0] = rs::fnv1a64(bytes.data(), rs::head_bytes);
    t.bullet_texture_hash[0] = rs::fnv1a64(bytes.data(), bytes.size());
    t.bullet_texture_p99_q[0] = q(0.6169f);
    t.bullet_texture_bytes[1] = 1398256;
    t.bullet_texture_head[1] = 1;
    t.bullet_texture_hash[1] = 2;
    t.bullet_texture_p99_q[1] = q(0.9888f);
    const int hit = rs::texture_key(t, bytes.data(), bytes.size());
    std::vector<unsigned char> tail = bytes, head = bytes;
    tail.back() ^= 1u;
    head[17] ^= 1u;
    const int tail_miss = rs::texture_key(t, tail.data(), tail.size());
    const int head_miss = rs::texture_key(t, head.data(), head.size());
    const int size_miss = rs::texture_key(t, bytes.data(), bytes.size() - 1);
    const int no_table = rs::texture_key(core::Table{}, bytes.data(), bytes.size());
    const float ire = rs::texture_scale(t.bullet_texture_p99_q[0]), ppc = rs::texture_scale(t.bullet_texture_p99_q[1]);
    const float none = rs::texture_scale(0);
    rs::Plan plan;
    float c31[4] = {0.4f, 0.4f, 0.2f, rs::luminance_scale}, got[12];
    plan.constants = c31;
    plan.scale = ppc;
    rs::plan_constants(plan, got);
    const bool plan_ok = got[11] == ppc && got[8] == c31[0] && !std::memcmp(got, rs::identity_layout, 32);
    plan.scale = 0.f;
    rs::plan_constants(plan, got);
    const bool fallback_ok = got[11] == rs::luminance_scale;
    LARGE_INTEGER freq{}, a{}, b{};
    QueryPerformanceFrequency(&freq);
    constexpr unsigned runs = 7, calls = 64;
    double best[3] = {1e30, 1e30, 1e30};
    volatile int sink = 0;
    for (unsigned run = 0; run < runs; ++run) {
        const std::vector<unsigned char>* bufs[3] = {&bytes, &head, &bytes};
        const std::size_t sizes[3] = {bytes.size() - 1, head.size(), bytes.size()};
        for (unsigned k = 0; k < 3; ++k) {
            QueryPerformanceCounter(&a);
            for (unsigned i = 0; i < calls; ++i) sink = sink + rs::texture_key(t, bufs[k]->data(), sizes[k]);
            QueryPerformanceCounter(&b);
            best[k] = std::min(best[k], ticks_us(a, b, freq) / calls);
        }
    }
    const bool keys_ok = hit == 0 && tail_miss < 0 && head_miss < 0 && size_miss < 0 && no_table < 0;
    const bool scale_ok = std::fabs(ire / rs::luminance_scale - 1.f) < 1e-3f &&
                          std::fabs(ppc / (rs::luminance_scale * 0.6169f / 0.9888f) - 1.f) < 1e-3f &&
                          none == rs::luminance_scale;
    std::printf("RESHADE case=texture_key hit=%d tail_miss=%d head_miss=%d size_miss=%d no_table=%d scale_ire=%.4f "
                "scale_ppc=%.4f scale_none=%.4f plan=%u fallback=%u bytes=%u us_size_miss=%.4f us_head_miss=%.3f "
                "us_match=%.1f\n",
                hit, tail_miss, head_miss, size_miss, no_table, double(ire), double(ppc), double(none), unsigned(plan_ok),
                unsigned(fallback_ok), unsigned(bytes.size()), best[0], best[1], best[2]);
    report("reshade_texture_key_matches_exact_bytes", keys_ok, "");
    report("reshade_texture_scale_per_texture", scale_ok && plan_ok && fallback_ok, "");
}

// The muzzle flash re-shade (X3M_WEAPON_EFFECTS=reshade; muzzle-flash-look.md; weapon_muzzle_reshade_fixture.h):
// (a) the stand-in pair (the game VS's output ABI, the game PS's emissive law) compiles and the production ps_3_0
// program creates beside the vs_3_0 stand-in; (b) per flat texel, view vector, glow and alpha, the substituted draw's
// output (blend off) equals muzzle_reshade_core.h's model with c192 from the production draw_constants, and the
// stand-in's equals vanilla's emissive law; (c) the production sequence (muzzle_reshade_route.h begin / end, which
// MotionOutput runs; MotionOutput itself is not instantiated here): our program and the draw's c192..c195 while drawing,
// the game's program and the application's c192..c195 after; (d) the stand-in flash (50 fanned quads, procedural streak)
// at the scale peak, 30 m, 1920x1080 under ONE / ONE, at three poses (the flash axis 25 degrees out of the screen plane,
// along the view ray, in the screen plane: the per-draw overlap's range), then the same for body 444's stand-in (its
// four-plane cross and tex/true/376 cell, its own bodies[] row; rows flash444_*): re-shaded energy <= mz_gain x vanilla's, the
// extent at 0.1 linear (a tenth of scene white) <= mz_size x vanilla's + 1 px and the flash lit (peak >= mz_gain / 4)
// at each
// (FL 65 A baked no white core, mz_core 0, so the earlier extent at scene white is reported only, as is the extent at a
// tenth of each image's own peak); (e) the cost per draw: the four calls
// alone, the 100-triangle
// draw loop with and without them (best of 7 runs of 2000), the match (TLS lookup, the 4-byte validated node read
// through the production engine_memory::read, the body list) and the per-draw c192 (draw_constants): for the first
// listed body (445), the last (every compare) and an unlisted id (12009: every compare, no c192).
// MUZZLE_RESHADE rows; never the game's bytes.
void muzzle_reshade_cases() {
    namespace mf = muzzle_fx;
    namespace rf = reshade_fx;
    namespace mz = x3m::weapon_effects::muzzle_reshade;
    LPD3DXBUFFER vs_hold = nullptr, ps_hold = nullptr;
    const DWORD *vs_code = nullptr, *ps_code = nullptr;
    const bool compiled = compile_shader && compile(mf::vs_source, "vs_3_0", &vs_code, &vs_hold) &&
                          compile(mf::vanilla_ps_source, "ps_3_0", &ps_code, &ps_hold);
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> vanilla, ours;
    Com<IDirect3DVertexDeclaration9> decl;
    const bool created = compiled && SUCCEEDED(dev->CreateVertexShader(vs_code, &vs.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(ps_code, &vanilla.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(mf::program, &ours.p)) &&
                         SUCCEEDED(dev->CreateVertexDeclaration(mf::declaration(), &decl.p));
    std::printf("MUZZLE_RESHADE case=programs compiled=%u created=%u words=%u ps_major=%u\n", unsigned(compiled),
                unsigned(created), unsigned(sizeof mf::program / sizeof mf::program[0]),
                unsigned((mf::program[0] >> 8) & 0xffu));
    report("muzzle_reshade_programs_create", created, "");
    if (vs_hold) vs_hold->Release();
    if (ps_hold) ps_hold->Release();
    if (!created) return;
    const auto tune = x3m::weapon_effects::projectile_family::baked;
    const mz::Body& body = mz::bodies[0];
    float base[16];
    mz::constants(body, tune, base);
    float app[16];
    for (unsigned i = 0; i < 16; ++i) app[i] = 0.125f * float(i) - 0.75f; // application values to put back
    Com<IDirect3DTexture9> black_light;
    {
        const unsigned char zero[4] = {0, 0, 0, 0};
        black_light.p = mf::flat_texture(dev, zero);
    }
    // (b) flat texels at fixed view vectors through a full-target quad, blend off: our program against the CPU model.
    // The rows put the gun at (1, 2, 30) in camera space with world scale 0.001 per model unit: radius 45.6 x size.
    {
        float rows[36]{};
        rows[0] = rows[5] = rows[10] = 0.001f;
        rows[3] = 1.f, rows[7] = 2.f, rows[11] = 30.f;
        rows[24] = rows[29] = rows[34] = 1.f;
        float block[16];
        std::memcpy(block, base, sizeof block);
        const bool drawn = mz::draw_constants(body, rows, block);
        const float radius = 1.f / block[3];
        Com<IDirect3DTexture9> rt;
        Com<IDirect3DSurface9> rt_s, sys;
        bool ok = drawn && black_light.p &&
                  SUCCEEDED(dev->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt.p, nullptr)) &&
                  SUCCEEDED(rt.p->GetSurfaceLevel(0, &rt_s.p)) &&
                  SUCCEEDED(dev->CreateOffscreenPlainSurface(16, 16, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys.p, nullptr));
        struct Case {
            float off;            // distance from the gun over the radius, along (0.6, 0, 0.8)
            unsigned char t[4], light_a;
            float alpha, glow;
        };
        const Case cases[7] = {
            {0.f, {170, 82, 28, 255}, 0, 1.f, 0.f},    // at the gun: body + core
            {0.15f, {99, 48, 17, 255}, 0, 1.f, 0.f},   // the core's range
            {0.5f, {20, 9, 3, 255}, 0, 0.5f, 0.f},     // the glow, a dark texel (the modulator's floor)
            {0.8f, {255, 247, 200, 255}, 0, 1.f, 0.f}, // the glow's rim, a bright texel
            {1.2f, {200, 97, 33, 255}, 0, 1.f, 0.f},   // past the radius: black, vanilla alpha
            {0.3f, {170, 82, 28, 200}, 64, 0.8f, 1.f}, // glow: alpha from the light map
            {0.05f, {0, 0, 0, 255}, 0, 1.f, 0.f},      // a black texel at the gun: the core alone
        };
        float worst = 0.f, worst_vanilla = 0.f;
        IDirect3DSurface9* old_rt = nullptr;
        dev->GetRenderTarget(0, &old_rt);
        if (ok) dev->SetRenderTarget(0, rt_s.p);
        float clip[16];
        rf::clip_rows(1.f, 1.f, clip);
        for (unsigned c = 0; ok && c < 7; ++c) {
            const Case& k = cases[c];
            Com<IDirect3DTexture9> flat, light;
            const unsigned char la[4] = {0, 0, 0, k.light_a};
            flat.p = mf::flat_texture(dev, k.t);
            light.p = mf::flat_texture(dev, la);
            ok = flat.p && light.p;
            if (!ok) break;
            // P = gun + off x radius x dir; TEXCOORD1 = camera - P = -P
            const float dir[3] = {0.6f, 0.f, 0.8f};
            const float view[3] = {-(1.f + k.off * radius * dir[0]), -(2.f + k.off * radius * dir[1]),
                                   -(30.f + k.off * radius * dir[2])};
            const float u = 0.2f, v = 0.3f;
            const mf::Vertex quad[6] = {{-1, -1, 1, u, v, view[0], view[1], view[2]}, {-1, 1, 1, u, v, view[0], view[1], view[2]},
                                        {1, 1, 1, u, v, view[0], view[1], view[2]},   {-1, -1, 1, u, v, view[0], view[1], view[2]},
                                        {1, 1, 1, u, v, view[0], view[1], view[2]},   {1, -1, 1, u, v, view[0], view[1], view[2]}};
            const float colour[4] = {mf::emissive[0], mf::emissive[1], mf::emissive[2], k.alpha};
            for (unsigned which = 0; which < 2 && ok; ++which) {
                dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
                dev->BeginScene();
                mf::bind_pass(dev, vs.p, decl.p, flat.p, light.p, clip, false, colour, k.glow);
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE); // the program's output itself
                dev->SetPixelShaderConstantF(mz::constant_register, block, mz::constant_count);
                dev->SetPixelShader(which ? ours.p : vanilla.p);
                ok = SUCCEEDED(dev->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, quad, sizeof(mf::Vertex)));
                dev->EndScene();
                ok = ok && SUCCEEDED(dev->GetRenderTargetData(rt_s.p, sys.p));
                D3DLOCKED_RECT out{};
                ok = ok && SUCCEEDED(sys.p->LockRect(&out, nullptr, D3DLOCK_READONLY));
                if (!ok) break;
                const auto* px = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(out.pBits) + 8 * out.Pitch) + 8 * 4;
                const float got[4] = {half_to_float(px[0]), half_to_float(px[1]), half_to_float(px[2]), half_to_float(px[3])};
                sys.p->UnlockRect();
                const float t[4] = {k.t[0] / 255.f, k.t[1] / 255.f, k.t[2] / 255.f, k.t[3] / 255.f};
                float want[4];
                if (which)
                    mz::model(block, t, view, k.alpha, k.light_a / 255.f, k.glow, want);
                else {
                    for (unsigned i = 0; i < 3; ++i) want[i] = std::min(1.f, mf::emissive[i]) * t[i];
                    want[3] = (t[3] + (k.light_a / 255.f - t[3]) * k.glow) * k.alpha;
                }
                for (unsigned i = 0; i < 4; ++i) {
                    const float err = std::fabs(got[i] - want[i]) / (0.004f + 0.004f * std::fabs(want[i]));
                    float& w = which ? worst : worst_vanilla;
                    w = std::max(w, err);
                }
                if (which)
                    std::printf("MUZZLE_RESHADE case=texel index=%u off=%.2f got=%.4f,%.4f,%.4f,%.4f "
                                "model=%.4f,%.4f,%.4f,%.4f\n",
                                c, double(k.off), double(got[0]), double(got[1]), double(got[2]), double(got[3]),
                                double(want[0]), double(want[1]), double(want[2]), double(want[3]));
            }
        }
        dev->SetRenderTarget(0, old_rt);
        if (old_rt) old_rt->Release();
        std::printf("MUZZLE_RESHADE case=model radius_m=%.4f worst_rel=%.3f vanilla_worst_rel=%.3f\n", double(radius),
                    double(worst), double(worst_vanilla));
        report("muzzle_reshade_program_matches_model", ok && worst <= 1.f, "");
        report("muzzle_reshade_standin_vanilla_is_emissive_law", ok && worst_vanilla <= 1.f, "");
    }
    // (c), (d), (e) on a 1920x1080 FP16 target with the stand-in flash
    Com<IDirect3DTexture9> rt, tex, tex444;
    Com<IDirect3DSurface9> rt_s, sys;
    Com<IDirect3DVertexBuffer9> vb;
    unsigned side = 0, side444 = 0;
    const std::vector<unsigned char> texels = mf::standin_texture(&side);
    tex.p = rf::make_texture(dev, texels.data(), side);
    const mf::Mesh mesh = mf::standin_mesh();
    const std::vector<unsigned char> texels444 = mf::standin_texture_444(&side444);
    tex444.p = rf::make_texture(dev, texels444.data(), side444);
    const mf::Mesh mesh444 = mf::standin_mesh_444();
    const unsigned body444 = mz::body_index(444u);
    bool ok = tex.p && tex444.p && body444 < mz::body_count && black_light.p &&
              SUCCEEDED(dev->CreateTexture(1920, 1080, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &rt.p, nullptr)) &&
              SUCCEEDED(rt.p->GetSurfaceLevel(0, &rt_s.p)) &&
              SUCCEEDED(dev->CreateOffscreenPlainSurface(1920, 1080, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &sys.p, nullptr)) &&
              SUCCEEDED(dev->CreateVertexBuffer(mf::max_vertices * sizeof(mf::Vertex), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                                &vb.p, nullptr));
    report("muzzle_reshade_targets", ok, "");
    if (!ok) return;
    float rows[36]{}, plain[16];
    std::memcpy(plain, base, sizeof plain);
    const UINT prims = mesh.n / 3;
    float base444[16];
    mz::constants(mz::bodies[body444 < mz::body_count ? body444 : 0], tune, base444);
    float clip[16];
    rf::clip_rows(1.6f, 1.6f * 1920.f / 1080.f, clip);
    IDirect3DSurface9* old_rt = nullptr;
    dev->GetRenderTarget(0, &old_rt);
    dev->SetRenderTarget(0, rt_s.p);
    std::vector<float> image(std::size_t(1920) * 1080 * 3);
    const auto read_image = [&] {
        if (FAILED(dev->GetRenderTargetData(rt_s.p, sys.p))) return false;
        D3DLOCKED_RECT lr{};
        if (FAILED(sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
        for (UINT y = 0; y < 1080; ++y) {
            const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
            for (UINT x = 0; x < 1920; ++x)
                for (unsigned k = 0; k < 3; ++k) image[(std::size_t(y) * 1920 + x) * 3 + k] = half_to_float(row[x * 4 + k]);
        }
        sys.p->UnlockRect();
        return true;
    };
    const float size = mz::clamp_key(tune.mz_size, x3m::weapon_effects::projectile_family::key_mz_size),
                gain = mz::clamp_key(tune.mz_gain, x3m::weapon_effects::projectile_family::key_mz_gain);
    // the poses: yaw of the flash axis out of the screen plane (the first is the dumps' and the cost's pose)
    const struct {
        const char* name;
        float yaw;
    } poses[3] = {{"tilt", 0.4363f}, {"axis", 1.5707963f}, {"side", 0.f}};
    bool restored_all = true, getters_ok = true, during_ok = true, energy_ok = true, extent_ok = true;
    float tilt_rows[36]{};
    // the subjects: body 445's stand-in (bodies[0]; rows flash_*) and body 444's (rows flash444_*), the same poses
    // and gates, each through its own bodies[] row. 444's four planes contain its axis: with the camera on that line
    // (the axis pose at x 0) every plane is edge-on and vanilla draws nothing (sum 0.14 against ~4000 tilted), so its
    // gun sits 3 m beside the view line, as a chase view's camera sits beside the gun's (cos^2 0.99 at the axis pose)
    const struct {
        const char* prefix;
        const mf::Mesh* mesh;
        IDirect3DTexture9* tex;
        const mz::Body* body;
        const float* base;
        float x;
    } subjects[2] = {{"flash", &mesh, tex.p, &body, base, 0.f},
                     {"flash444", &mesh444, tex444.p, &mz::bodies[body444 < mz::body_count ? body444 : 0], base444, 3.f}};
    for (unsigned item = 0; item < 2 * 3 && ok; ++item) {
        const unsigned subject = item / 3, pose = item % 3;
        const auto& sub = subjects[subject];
        const UINT sub_prims = sub.mesh->n / 3;
        void* p = nullptr;
        ok = SUCCEEDED(vb.p->Lock(0, 0, &p, 0));
        if (ok) {
            const float centre[3] = {sub.x, 0.f, 30.f};
            mf::place(*sub.mesh, centre, mf::peak_scale, poses[pose].yaw, 0.3491f, static_cast<mf::Vertex*>(p), rows);
            vb.p->Unlock();
        }
        std::memcpy(plain, sub.base, sizeof plain);
        ok = ok && mz::draw_constants(*sub.body, rows, plain);
        if (!subject && !pose) std::memcpy(tilt_rows, rows, sizeof rows);
        mf::Figures figs[2];
        for (unsigned which = 0; which < 2 && ok; ++which) {
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            dev->BeginScene();
            mf::bind_pass(dev, vs.p, decl.p, sub.tex, black_light.p, clip, false);
            dev->SetStreamSource(0, vb.p, 0, sizeof(mf::Vertex));
            dev->SetPixelShader(vanilla.p);
            dev->SetPixelShaderConstantF(mz::constant_register, app, mz::constant_count);
            HRESULT hr = S_OK;
            if (which) { // (c) the sequence, with the bindings read while it holds them
                auto ops = mf::device_ops(dev);
                mz::Applied applied;
                hr = mz::begin(ops, ours.p, plain, app, applied);
                IDirect3DPixelShader9* bound = nullptr;
                float got_c[16]{};
                during_ok = during_ok && SUCCEEDED(hr) && SUCCEEDED(dev->GetPixelShader(&bound)) && bound == ours.p &&
                            SUCCEEDED(dev->GetPixelShaderConstantF(mz::constant_register, got_c, mz::constant_count)) &&
                            !std::memcmp(got_c, plain, sizeof got_c);
                if (bound) bound->Release();
                if (SUCCEEDED(hr)) hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, sub_prims);
                restored_all = restored_all && SUCCEEDED(mz::end(ops, applied, vanilla.p, app)) && SUCCEEDED(hr);
                bound = nullptr;
                getters_ok = getters_ok && SUCCEEDED(dev->GetPixelShader(&bound)) && bound == vanilla.p &&
                             SUCCEEDED(dev->GetPixelShaderConstantF(mz::constant_register, got_c, mz::constant_count)) &&
                             !std::memcmp(got_c, app, sizeof got_c);
                if (bound) bound->Release();
            } else
                hr = dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, sub_prims);
            dev->EndScene();
            ok = SUCCEEDED(hr) && read_image();
            if (ok) figs[which] = mf::figures(image.data(), 1920, 1080, 0.f);
        }
        const double energy = figs[0].sum > 0 ? figs[1].sum / figs[0].sum : 0.0;
        energy_ok = energy_ok && ok && figs[0].sum > 0.0 && energy <= double(gain);
        // the extent at 0.1 linear binds only where the re-shade reaches 0.1: at the baked keys 445's stand-in does
        // (9.3 px), 444's does not (its peak 0.06..0.08), so the extent at a tenth of each image's own peak no wider
        // than vanilla's (the dump's gate) binds for both
        extent_ok = extent_ok && ok && figs[1].peak >= 0.25f * gain && figs[1].tenth_r <= size * figs[0].tenth_r + 1.f &&
                    figs[1].peak_tenth_r <= figs[0].peak_tenth_r;
        // the overlap the draw used: c194.x = gain / N
        const double overlap = plain[8] > 0.f ? double(plain[10]) / double(plain[8]) : 0.0;
        std::printf("MUZZLE_RESHADE case=%s_%s body=%u distance_m=30 yaw_deg=%.1f scale=%.3f overlap=%.3f vanilla_sum=%.2f "
                    "reshade_sum=%.2f energy_ratio=%.3f vanilla_peak=%.4f reshade_peak=%.4f vanilla_white_r=%.1f "
                    "reshade_white_r=%.1f vanilla_tenth_r=%.1f reshade_tenth_r=%.1f vanilla_peak_tenth_r=%.1f "
                    "reshade_peak_tenth_r=%.1f size=%.3g gain=%.3g core=%.3g smooth=%.3g\n",
                    sub.prefix, poses[pose].name, unsigned(sub.body->id), double(poses[pose].yaw) * 57.29578,
                    double(mf::peak_scale), overlap, figs[0].sum,
                    figs[1].sum, energy, double(figs[0].peak), double(figs[1].peak), double(figs[0].white_r),
                    double(figs[1].white_r), double(figs[0].tenth_r), double(figs[1].tenth_r),
                    double(figs[0].peak_tenth_r), double(figs[1].peak_tenth_r), double(size), double(gain),
                    double(tune.mz_core), double(tune.mz_smooth));
    }
    std::memcpy(rows, tilt_rows, sizeof rows);
    std::memcpy(plain, base, sizeof plain);
    ok = ok && mz::draw_constants(body, rows, plain);
    report("muzzle_reshade_route_plain_binds_and_restores", ok && during_ok && restored_all && getters_ok, "");
    report("muzzle_reshade_energy_within_gain", ok && energy_ok, "");
    // the design's gate is the extent at 0.1 linear: lit, and within mz_size x vanilla's + 1 px; and the extent at a
    // tenth of its own peak within vanilla's (the one extent gate that binds for 444's stand-in at the baked keys)
    report("muzzle_reshade_extent_within_size", ok && extent_ok, "");
    // (e) the cost: CPU per draw of the four calls alone, the draw loop with and without them, the match, the c192
    LARGE_INTEGER freq{}, a{}, b{};
    QueryPerformanceFrequency(&freq);
    Com<IDirect3DQuery9> fence;
    dev->CreateQuery(D3DQUERYTYPE_EVENT, &fence.p);
    const auto finish = [&] {
        if (!fence.p) return;
        fence.p->Issue(D3DISSUE_END);
        while (fence.p->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) {
        }
    };
    constexpr unsigned draws = 2000, runs = 7;
    double best_calls = 1e30, best_vanilla = 1e30, best_reshaded = 1e30, best_match = 1e30;
    for (unsigned run = 0; run < runs && ok; ++run) {
        dev->BeginScene();
        mf::bind_pass(dev, vs.p, decl.p, tex.p, black_light.p, clip, false);
        dev->SetStreamSource(0, vb.p, 0, sizeof(mf::Vertex));
        dev->SetPixelShader(vanilla.p);
        finish();
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i) {
            dev->SetPixelShaderConstantF(mz::constant_register, plain, mz::constant_count);
            dev->SetPixelShader(ours.p);
            dev->SetPixelShader(vanilla.p);
            dev->SetPixelShaderConstantF(mz::constant_register, app, mz::constant_count);
        }
        QueryPerformanceCounter(&b);
        best_calls = std::min(best_calls, ticks_us(a, b, freq) / draws);
        finish();
        QueryPerformanceCounter(&a);
        for (unsigned i = 0; i < draws; ++i) dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, prims);
        finish();
        QueryPerformanceCounter(&b);
        best_vanilla = std::min(best_vanilla, ticks_us(a, b, freq) / draws);
        QueryPerformanceCounter(&a);
        bool restored = true;
        for (unsigned i = 0; i < draws; ++i) mf::reshaded_draw(dev, ours.p, vanilla.p, plain, app, prims, &restored);
        finish();
        QueryPerformanceCounter(&b);
        best_reshaded = std::min(best_reshaded, ticks_us(a, b, freq) / draws);
        dev->EndScene();
    }
    device_guard("muzzle_reshade_cost");
    // the match and the draw's c192: a TLS slot read (the scope's top), the node's 4-byte body id through
    // engine_memory::read, the list, then draw_constants from the shadowed rows into the draw's block
    double best_match_last = 1e30, best_match_miss = 1e30;
    {
        std::vector<std::uint32_t> node(0x160 / 4, 0u);
        const DWORD slot = TlsAlloc();
        TlsSetValue(slot, node.data());
        x3m::engine_memory::next_frame();
        float block[16];
        // the first listed id (445), the last (every compare) and an unlisted one (12009, PRG: every compare, no c192)
        const std::uint32_t ids[3] = {mz::bodies[0].id, mz::bodies[mz::body_count - 1].id, 12009u};
        double* best[3] = {&best_match, &best_match_last, &best_match_miss};
        for (unsigned which = 0; which < 3; ++which) {
            node[mz::node_body_offset / 4] = ids[which];
            unsigned hits = 0;
            for (unsigned run = 0; run < runs; ++run) {
                QueryPerformanceCounter(&a);
                for (unsigned i = 0; i < draws; ++i) {
                    const auto at = reinterpret_cast<std::uintptr_t>(TlsGetValue(slot));
                    std::uint32_t id = 0;
                    if (at && x3m::engine_memory::read(at + mz::node_body_offset, &id, sizeof id) &&
                        mz::body_index(id) < mz::body_count) {
                        std::memcpy(block, base, sizeof block);
                        if (mz::draw_constants(mz::bodies[mz::body_index(id)], rows, block)) ++hits;
                    }
                }
                QueryPerformanceCounter(&b);
                *best[which] = std::min(*best[which], ticks_us(a, b, freq) / draws);
            }
            ok = ok && hits == (which < 2 ? draws * runs : 0u);
        }
        TlsFree(slot);
    }
    dev->SetRenderTarget(0, old_rt);
    if (old_rt) old_rt->Release();
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetTexture(0, nullptr);
    dev->SetTexture(2, nullptr);
    dev->SetStreamSource(0, nullptr, 0, 0);
    std::printf("MUZZLE_RESHADE case=cost draws=%u runs=%u primitives=%u calls_us_per_draw=%.3f draw_us_vanilla=%.3f "
                "draw_us_reshaded=%.3f substitution_us_per_draw=%.3f match_us_per_draw=%.3f bodies=%u "
                "match_last_us_per_draw=%.3f match_miss_us_per_draw=%.3f fenced=%u\n",
                draws, runs, prims, best_calls, best_vanilla, best_reshaded, best_reshaded - best_vanilla, best_match,
                mz::body_count, best_match_last, best_match_miss, unsigned(fence.p != nullptr));
    report("muzzle_reshade_cost_measured",
           ok && fence.p && best_calls < 50.0 && best_match < 10.0 && best_match_last < 10.0 && best_match_miss < 10.0, "");
}
#include "weapon_lone_classic_cases.h" // the bullet re-shade's lone classic path (lone_classic_cases)

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
    std::printf("ATTACH result=%08lx shell_vs=%u shell_ps=%u decal_vs=%u decal_ps=%u fp16=%08lx max_ps=%lu patch_vertices=%u patch_triangles=%u\n",
                attached, pass.caps().shell_vs_slots, pass.caps().shell_ps_slots, pass.caps().decal_vs_slots,
                pass.caps().decal_ps_slots, pass.caps().fp16_blending,
                static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots), rr::WeaponEffectsPass::patch_vertices,
                rr::WeaponEffectsPass::patch_triangles);
    if (!pass.caps().enabled || !lane_ps) {
        std::printf("RESULT FAIL checks=%u failures=%u\n", checks, failures + 1);
        return 1;
    }
    std::printf("ATTACH_SLOTS shell_vs=%u shell_ps=%u bubble_ps=%u decal_vs=%u decal_ps=%u lathe_vs=%u lathe_ps=%u "
                "patch_bytes=%u frame_patches_bytes=%u\n",
                pass.caps().shell_vs_slots, pass.caps().shell_ps_slots, pass.caps().bubble_ps_slots, pass.caps().decal_vs_slots,
                pass.caps().decal_ps_slots, pass.caps().lathe_vs_slots, pass.caps().lathe_ps_slots,
                unsigned(sizeof(rr::WeaponPatch)), unsigned(sizeof(rr::WeaponPatch) * wc::max_patches));
    // The cell map's sort (review F5): rr::sort_by_key (heap sort) against the insertion sort it replaced, on 128 keys
    // shuffled, ascending and descending (best of 2000 sorts each, us).
    {
        float keys[128];
        LARGE_INTEGER freq{}, a{}, b{};
        QueryPerformanceFrequency(&freq);
        auto insertion = [](std::uint8_t* idx, unsigned n, const float* key) {
            for (unsigned i = 1; i < n; ++i) {
                const std::uint8_t v = idx[i];
                unsigned j = i;
                while (j > 0 && key[idx[j - 1]] > key[v]) {
                    idx[j] = idx[j - 1];
                    --j;
                }
                idx[j] = v;
            }
        };
        bool sorted_ok = true;
        for (unsigned layout = 0; layout < 3; ++layout) {
            for (unsigned i = 0; i < 128; ++i)
                keys[i] = layout == 0 ? float(unsigned(halton(i + 1, 3) * 8388607.0)) : layout == 1 ? float(i) : float(127 - i);
            double best[2] = {1e30, 1e30};
            for (unsigned which = 0; which < 2; ++which)
                for (unsigned rep = 0; rep < 2000; ++rep) {
                    std::uint8_t idx[128];
                    for (unsigned i = 0; i < 128; ++i) idx[i] = std::uint8_t(i);
                    QueryPerformanceCounter(&a);
                    if (which) insertion(idx, 128, keys); else rr::sort_by_key(idx, 128, keys);
                    QueryPerformanceCounter(&b);
                    best[which] = std::min(best[which], ticks_us(a, b, freq));
                    for (unsigned i = 1; i < 128 && !which; ++i) sorted_ok = sorted_ok && keys[idx[i - 1]] <= keys[idx[i]];
                }
            std::printf("SORT case=%s heap_us=%.3f insertion_us=%.3f\n", layout == 0 ? "shuffled" : layout == 1 ? "ascending" : "descending",
                        best[0], best[1]);
        }
        report("cell_map_sort_orders", sorted_ok, "");
    }
    report("pixel_program_low_thousands", pass.caps().shell_ps_slots < 2000 && pass.caps().decal_ps_slots < 2000 &&
                                              pass.caps().bubble_ps_slots < 2000, "");
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
    // The surface is the lane (the screen-space patch): a fighter patch (its hit on the plane square to the view at
    // z = centre_z - S) over a full-screen lane at that plane draws its whole footprint (on); over the sky nothing; the
    // plane moved half an extent toward the eye still draws (within the extent along the axis); moved past the extent
    // nothing; over a lane holding the plane on the left half only, nothing right of the centre (the hit) and the left
    // half of the footprint as on.
    {
        const rr::WeaponPatch p = make_patch(fighter, {{0.f, 0.f, 0.5f}});
        const float z = fighter.centre_z - fighter.surface, ext = p.extent;
        auto sum_at = [&](float lane_z, float frac, unsigned* right_lit = nullptr, double* left = nullptr) {
            draw_lane(g, lane_z, frac);
            rr::WeaponEffectsFrame f = base_frame(g);
            f.patches = &p;
            f.patch_count = 1;
            draw(pass, f);
            const std::vector<float> l = read_luma(g, g.scene_s.p);
            double sum = 0, lsum = 0;
            unsigned right = 0;
            for (UINT y = 0; y < g.h; ++y)
                for (UINT x = 0; x < g.w; ++x) {
                    const double v = l[std::size_t(y) * g.w + x];
                    sum += v;
                    if (x < g.w / 2) lsum += v; else right += v > 1e-3;
                }
            if (right_lit) *right_lit = right;
            if (left) *left = lsum;
            return sum;
        };
        double on_left = 0, half_left = 0;
        unsigned half_right = 0;
        const double on = sum_at(z, 1.f, nullptr, &on_left), sky = sum_at(0.f, -1.f);
        const double nearer = sum_at(z - 0.5f * ext, 1.f), past = sum_at(z - 1.01f * ext, 1.f);
        const double half = sum_at(z, 0.5f, &half_right, &half_left);
        draw_lane(g, 0.f, -1.f);
        std::printf("SURFACE plane_z=%.2f extent=%.2f on=%.3f sky=%.3f nearer=%.3f past=%.3f half=%.3f half_left=%.3f "
                    "on_left=%.3f half_right_lit=%u\n", z, ext, on, sky, nearer, past, half, half_left, on_left, half_right);
        report("surface_from_the_lane", on > 1.0 && sky == 0.0 && nearer > 0.0 && past == 0.0 &&
                                            half_right == 0 && std::fabs(half_left - on_left) <= 2e-3 * on_left, "");
    }
    // Repeated hits: the same point hit 0.7 s and 0.3 s ago re-lights (the cell's clock is the newest hit's, not a
    // sum); a second hit on the next cell (plane x = 1 cell, 0.5 s after the first at 1.0 s) resets only the cells the
    // two footprints share (the hit cells, centres x = 0 and 1 cell, back to the second hit's value) while the first
    // footprint's other cells (x = -1 cell) keep fading on their own clock and the second's (x = 2 cells) carry its
    // clock; no cell is drawn twice (the pair's peak never exceeds the brighter single hit's).
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
    }
    // The grid is fixed on the owner (through the production builder): the same local hits on an owner moved and
    // rotated (another position, the axes turned 90 degrees about y) give the identical cells: region, keys, clocks,
    // depths.
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
        // The faces both views draw (the face cull follows the view) carry the identical cells.
        unsigned cells = 0, common = 0;
        bool same = na.patches > 0 && nb.patches > 0;
        for (unsigned i = 0; same && i < na.patches; ++i) {
            unsigned j = 0;
            while (j < nb.patches && pb[j].seed != pa[i].seed) ++j;
            if (j == nb.patches) continue;
            ++common;
            const rr::WeaponPatch& b = pb[j];
            same = b.cells == pa[i].cells;
            for (unsigned k = 0; same && k < pa[i].cells; ++k)
                same = pa[i].cell_key[k] == b.cell_key[k] && pa[i].cell_since[k] == b.cell_since[k] &&
                       pa[i].cell_anchor[k] == b.cell_anchor[k];
            cells += pa[i].cells;
        }
        same = same && common > 0;
        const bool moved = na.patches && nb.patches && (pa[0].centre[2] != pb[0].centre[2]) && (pa[0].view_a[0] != pb[0].view_a[0]);
        std::printf("GRID patches=%u moved_patches=%u cells=%u identical=%u owner_moved=%u\n", na.patches, nb.patches, cells,
                    unsigned(same), unsigned(moved));
        report("grid_fixed_on_owner", same && moved, "");
    }
    // Placement (5.1.1 "Placement"): through the production builder with the identity camera, one hit 0.5 s old (every
    // footprint cell lit), the lane holding the case's hull (an ellipsoid of the owner's frame: the modelled
    // half-extents, else a sphere through the hit point). The station keeps the hull decal (tier_by_station): the Flight
    // B station (R 1304 m) at its recorded contact point (0.676 R), on the hull lane (nothing on sky, nothing past the
    // footprint), the lit near points' centroid within a cell of the contact, the covered share near it. A ship without an
    // offline profile draws nothing (user decision 2026-11-12 on FL 69 A: the hook forwards its hit native): the Titan's
    // hull (radius 545751, half-extents 151433 x 166374 x 528422 engine units) hit on its flank 30 % aft, no profile. The
    // walked ellipsoid's ship cases (narrow and flat hulls, the off-centre origin, the 42-degree bolt) left with the walk;
    // their hulls stay in the table for the NORMALS rows.
    {
        struct Placement {
            const char* name;
            std::uint8_t tier;
            float radius, z;            // the owner's radius and its centre's view z (view units)
            std::int32_t pos[3];        // the contact point (engine units, owner frame)
            std::int32_t rows[3][3];    // the owner's rows (16.16)
            float ext[3];               // the hull's half-extents (owner frame, view units; 0: a sphere through the hit)
            std::int8_t dir[3];         // the bolt's travel (recorded, unused)
            unsigned ref;               // the case whose lit count is the reference (its own index: itself)
            float off[3] = {0.f, 0.f, 0.f}; // the hull's centre off the owner's origin (owner frame, view units)
        };
        const auto eng = [](double v) { return std::int32_t(v / double(wc::world_scale)); };
        const std::int32_t side[3][3] = {{0, 0, -65536}, {0, 65536, 0}, {65536, 0, 0}}; // local x -> view -z (the flank)
        const std::int32_t facing[3][3] = {{65536, 0, 0}, {0, -65536, 0}, {0, 0, -65536}}; // local +z -> view -z
        const std::int32_t top[3][3] = {{65536, 0, 0}, {0, 0, -65536}, {0, 65536, 0}};    // local +y -> view -z
        const std::int32_t bottom[3][3] = {{65536, 0, 0}, {0, 0, 65536}, {0, -65536, 0}}; // local -y -> view -z
        const float a6 = 250.f / 6.f;
        const double s84 = std::sqrt(1.0 - 0.16), s82 = std::sqrt(1.0 - 0.18);
        constexpr unsigned count = 11, station = 8, capital = 10;
        Placement cases[count] = {
            {"narrow_mid_flank", wc::patch_corvette, 250.f, 600.f, {eng(a6 * std::sqrt(1.0 - 0.09 * 0.09)), 0, eng(0.09 * 250.0)}, {}, {a6, a6, 250.f}, {-127, 0, 0}, 0},
            {"aft_flank", wc::patch_corvette, 250.f, 600.f, {eng(a6 * s84), 0, eng(0.4 * 250.0)}, {}, {a6, a6, 250.f}, {-127, 0, 0}, 0},
            {"y_top_narrow", wc::patch_corvette, 250.f, 600.f, {0, eng(a6 * s84), eng(0.4 * 250.0)}, {}, {a6, a6, 250.f}, {0, -127, 0}, 0},
            {"y_bottom_narrow", wc::patch_corvette, 250.f, 600.f, {0, -eng(a6 * s84), eng(0.4 * 250.0)}, {}, {a6, a6, 250.f}, {0, 127, 0}, 0},
            {"flat_flank", wc::patch_fighter, 60.f, 400.f, {eng(60.0 * std::sqrt(1.0 - 0.09)), 0, eng(0.3 * 40.0)}, {}, {60.f, 10.f, 40.f}, {-127, 0, 0}, 4},
            {"y_top_flat", wc::patch_fighter, 60.f, 400.f, {eng(0.3 * 60.0), eng(10.0 * s82), eng(0.3 * 40.0)}, {}, {60.f, 10.f, 40.f}, {0, -127, 0}, 4},
            {"y_bottom_flat", wc::patch_fighter, 60.f, 400.f, {eng(0.3 * 60.0), -eng(10.0 * s82), eng(0.3 * 40.0)}, {}, {60.f, 10.f, 40.f}, {0, 127, 0}, 4},
            {"y_top_flat_42deg", wc::patch_fighter, 60.f, 400.f, {eng(0.3 * 60.0), eng(10.0 * s82), eng(0.3 * 40.0)}, {}, {60.f, 10.f, 40.f}, {0, -85, 95}, 4},
            {"station_flight_b_0p68R", wc::patch_capital, 6517.64f, 14000.f, {-114699, 85160, 416994}, {}, {}, {0, 0, -127}, 8},
            // review F1: the flat fighter's hull centred 2 cells (6 m) below the owner's origin, hit on its top
            {"y_top_flat_offcentre", wc::patch_fighter, 60.f, 400.f,
             {eng(0.3 * 60.0), eng(-30.0 + 10.0 * s82), eng(0.3 * 40.0)}, {}, {60.f, 10.f, 40.f}, {0, -127, 0}, 5,
             {0.f, -30.f, 0.f}},
            // FL 67 A: the Titan by its class (M2, patch_capital), the camera 8000 view units from the contact
            {"capital_titan_flank", wc::patch_capital, 5457.51f, 9444.6f,
             {eng(1514.33 * std::sqrt(1.0 - 0.09)), 0, eng(0.3 * 5284.22)}, {}, {1514.33f, 1663.74f, 5284.22f}, {-127, 0, 0},
             10}};
        const std::int32_t (*rows_of[count])[3] = {side, side, top, bottom, side, top, bottom, top, facing, top, side};
        for (unsigned k = 0; k < count; ++k) std::memcpy(cases[k].rows, rows_of[k], sizeof side);
        struct Result {
            unsigned lit = 0, sky_lit = 0, outside = 0, eligible = 0, covered = 0, patches = 0, lathes = 0, no_profile = 0;
            double centroid_cells = 1e30, lit_ratio = 0;
            bool drew = false;
        } res[count];
        for (unsigned i : {station, capital}) {
            const Placement& c = cases[i];
            rr::WeaponPatch patches[wc::max_patches];
            rr::WeaponDecal decals[wc::max_decals];
            stage_tier_source = i == station ? std::uint8_t(wc::tier_by_station) : std::uint8_t(wc::tier_by_ship_class);
            stage_owner(c.z, c.radius, 1000);
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned j = 0; j < 3; ++j) put32(fake_node, wc::layout::node_rot + 16 * r + 4 * j, std::uint32_t(c.rows[r][j]));
            put32(fake_obj, wc::layout::obj_radius, std::uint32_t(eng(c.radius)));
            stage_ring.slots[0].patch_tier = c.tier;
            for (unsigned k = 0; k < 3; ++k) {
                stage_ring.slots[0].pos[k] = c.pos[k];
                stage_ring.slots[0].dir[k] = c.dir[k];
            }
            stage_ring.slots[0].radius = eng(c.radius);
            double hp[3], pr = 0;
            for (unsigned k = 0; k < 3; ++k) {
                hp[k] = double(c.pos[k]) * double(wc::world_scale);
                pr += hp[k] * hp[k];
            }
            pr = std::sqrt(pr);
            const double ext[3] = {c.ext[0] > 0.f ? double(c.ext[0]) : pr, c.ext[0] > 0.f ? double(c.ext[1]) : pr,
                                   c.ext[0] > 0.f ? double(c.ext[2]) : pr};
            const double off[3] = {c.off[0], c.off[1], c.off[2]};
            const ws::Counts n = ws::build(stage_ring, 1500, &fake_read, identity_camera(), ws::Tuning{}, patches,
                                           wc::max_patches, decals, wc::max_decals);
            const HullLane hull = staged_hull(ext, off);
            const std::vector<float> l = render(pass, patches, n.patches, nullptr, nullptr, &hull);
            const double extent = n.patches ? double(patches[0].extent) : 0.0, cell = n.patches ? double(patches[0].cell) : 1.0;
            const double core = n.patches ? 0.5 * double(patches[0].reach) : 0.0; // surely inside the footprint's cells
            Result& o = res[i];
            o.patches = n.patches;
            o.lathes = n.lathes;
            o.no_profile = n.lathe_no_profile;
            double wsum = 0, cs[3] = {0, 0, 0};
            for (UINT y = 0; y < g.h; ++y)
                for (UINT x = 0; x < g.w; ++x) {
                    const double v = l[std::size_t(y) * g.w + x];
                    const bool lit = v > 1e-3;
                    double q[3];
                    const bool on = hull_point(g, hull, x, y, q);
                    for (unsigned k = 0; k < 3; ++k) q[k] += off[k]; // the hull lane's frame is centred on the hull
                    o.lit += lit;
                    if (!on) {
                        o.sky_lit += lit;
                        continue;
                    }
                    const double d = std::sqrt((q[0] - hp[0]) * (q[0] - hp[0]) + (q[1] - hp[1]) * (q[1] - hp[1]) +
                                               (q[2] - hp[2]) * (q[2] - hp[2]));
                    if (d <= core) {
                        ++o.eligible;
                        o.covered += lit;
                    }
                    if (lit && d > extent * 1.4142 * 1.01) ++o.outside; // the plane and the axis each within the extent
                    if (lit) {
                        wsum += v;
                        for (unsigned k = 0; k < 3; ++k) cs[k] += v * q[k];
                    }
                }
            if (wsum > 0) {
                double e2 = 0;
                for (unsigned k = 0; k < 3; ++k) e2 += (cs[k] / wsum - hp[k]) * (cs[k] / wsum - hp[k]);
                o.centroid_cells = std::sqrt(e2) / cell;
            }
            o.drew = o.lit > 100;
        }
        stage_tier_source = wc::tier_by_station;
        for (unsigned i : {station, capital}) {
            const Result& o = res[i];
            const double ratio = res[cases[i].ref].lit ? double(o.lit) / double(res[cases[i].ref].lit) : 0.0;
            res[i].lit_ratio = ratio;
            std::printf("PLACEMENT case=%s surface=%s patches=%u lathes=%u no_profile=%u lit_pixels=%u sky_lit=%u "
                        "outside_lit=%u eligible=%u covered=%u coverage=%.3f centroid_error_cells=%.3f ref=%s "
                        "lit_ratio=%.3f dir=%d,%d,%d\n",
                        cases[i].name, i == station ? "hull" : "native", o.patches, o.lathes, o.no_profile, o.lit, o.sky_lit,
                        o.outside, o.eligible, o.covered, o.eligible ? double(o.covered) / double(o.eligible) : 0.0,
                        o.centroid_cells, cases[cases[i].ref].name, ratio, int(cases[i].dir[0]), int(cases[i].dir[1]),
                        int(cases[i].dir[2]));
        }
        auto on_hull = [&](unsigned i) {
            const Result& o = res[i];
            return o.drew && o.sky_lit == 0 && o.outside * 100 <= o.lit && o.eligible &&
                   double(o.covered) >= 0.9 * double(o.eligible) && o.centroid_cells <= 1.0 && o.lit_ratio >= 1.0 / 3.0 &&
                   o.lit_ratio <= 3.0;
        };
        // The normal from the lane (Run236: ddx / ddy against a 4-neighbourhood), on the CPU twin of three hull lanes (the
        // narrow hull's top, the flat fighter's top, the station's sphere): per hull pixel the analytic normal against
        // the 2x2-quad derivative (the hardware's coarse ddx / ddy) and the neighbourhood (per axis the neighbour with
        // the smaller depth step): the mean and 99th-percentile angle, and the share of pixels whose dominant axis (the
        // lattice of the largest weight) differs from the analytic one; silhouette pixels (a quad or neighbour on sky)
        // counted apart.
        for (unsigned i : {2u, 5u, 8u}) {
            const Placement& c = cases[i];
            stage_owner(c.z, c.radius, 1000);
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned j = 0; j < 3; ++j) put32(fake_node, wc::layout::node_rot + 16 * r + 4 * j, std::uint32_t(c.rows[r][j]));
            double pr = 0;
            for (unsigned k = 0; k < 3; ++k) pr += double(c.pos[k]) * double(c.pos[k]) * double(wc::world_scale) * double(wc::world_scale);
            pr = std::sqrt(pr);
            const double ext[3] = {c.ext[0] > 0.f ? double(c.ext[0]) : pr, c.ext[0] > 0.f ? double(c.ext[1]) : pr,
                                   c.ext[0] > 0.f ? double(c.ext[2]) : pr};
            const HullLane hull = staged_hull(ext);
            const UINT W = g.w, H = g.h;
            std::vector<double> pts(std::size_t(W) * H * 3);
            std::vector<unsigned char> on(std::size_t(W) * H);
            for (UINT y = 0; y < H; ++y)
                for (UINT x = 0; x < W; ++x) on[std::size_t(y) * W + x] = hull_point(g, hull, x, y, &pts[(std::size_t(y) * W + x) * 3]);
            auto P = [&](UINT x, UINT y) { return &pts[(std::size_t(y) * W + x) * 3]; };
            auto angle = [](const double* a, const double* b) {
                double d = 0, la = 0, lb = 0;
                for (unsigned k = 0; k < 3; ++k) { d += a[k] * b[k]; la += a[k] * a[k]; lb += b[k] * b[k]; }
                return std::acos(std::min(1.0, std::fabs(d) / std::sqrt(la * lb + 1e-300))) * 57.29578;
            };
            auto dominant = [](const double* n) {
                const double a[3] = {std::fabs(n[0]), std::fabs(n[1]), std::fabs(n[2])};
                return a[0] >= a[1] && a[0] >= a[2] ? 0 : a[1] >= a[2] ? 1 : 2;
            };
            std::vector<double> err_q, err_n;
            unsigned pixels = 0, edge = 0, wrong_q = 0, wrong_n = 0, wrong_q_edge = 0, wrong_n_edge = 0;
            for (UINT y = 1; y + 1 < H; ++y)
                for (UINT x = 1; x + 1 < W; ++x) {
                    if (!on[std::size_t(y) * W + x]) continue;
                    const double* q = P(x, y);
                    const double tn[3] = {q[0] / (ext[0] * ext[0]), q[1] / (ext[1] * ext[1]), q[2] / (ext[2] * ext[2])};
                    const UINT x0 = x & ~1u, y0 = y & ~1u;
                    const bool quad_on = on[std::size_t(y0) * W + x0] && on[std::size_t(y0) * W + x0 + 1] &&
                                         on[std::size_t(y0 + 1) * W + x0];
                    double nq[3] = {0, 0, 0}, nn[3] = {0, 0, 0};
                    if (quad_on) {
                        const double *a = P(x0, y0), *bx = P(x0 + 1, y0), *by = P(x0, y0 + 1);
                        const double dx[3] = {bx[0] - a[0], bx[1] - a[1], bx[2] - a[2]}, dy[3] = {by[0] - a[0], by[1] - a[1], by[2] - a[2]};
                        nq[0] = dx[1] * dy[2] - dx[2] * dy[1]; nq[1] = dx[2] * dy[0] - dx[0] * dy[2]; nq[2] = dx[0] * dy[1] - dx[1] * dy[0];
                    }
                    const bool l = on[std::size_t(y) * W + x - 1], r = on[std::size_t(y) * W + x + 1];
                    const bool u = on[std::size_t(y - 1) * W + x], d = on[std::size_t(y + 1) * W + x];
                    const bool nb_on = (l || r) && (u || d);
                    if (nb_on) {
                        const double *xa = r ? P(x + 1, y) : q, *xb = r ? q : P(x - 1, y);
                        const double *ya = d ? P(x, y + 1) : q, *yb = d ? q : P(x, y - 1);
                        const double dx[3] = {xa[0] - xb[0], xa[1] - xb[1], xa[2] - xb[2]}, dy[3] = {ya[0] - yb[0], ya[1] - yb[1], ya[2] - yb[2]};
                        nn[0] = dx[1] * dy[2] - dx[2] * dy[1]; nn[1] = dx[2] * dy[0] - dx[0] * dy[2]; nn[2] = dx[0] * dy[1] - dx[1] * dy[0];
                    }
                    const bool is_edge = !quad_on || !(l && r && u && d);
                    ++pixels;
                    edge += is_edge;
                    const bool bad_q = !quad_on || dominant(nq) != dominant(tn), bad_n = !nb_on || dominant(nn) != dominant(tn);
                    wrong_q += bad_q;
                    wrong_n += bad_n;
                    wrong_q_edge += bad_q && is_edge;
                    wrong_n_edge += bad_n && is_edge;
                    if (quad_on) err_q.push_back(angle(nq, tn));
                    if (nb_on) err_n.push_back(angle(nn, tn));
                }
            auto stats = [](std::vector<double>& v, double* mean, double* p99) {
                *mean = 0;
                for (double e : v) *mean += e;
                *mean = v.empty() ? 0 : *mean / double(v.size());
                std::sort(v.begin(), v.end());
                *p99 = v.empty() ? 0 : v[std::min(v.size() - 1, std::size_t(0.99 * double(v.size())))];
            };
            double mq, pq, mn, pn;
            stats(err_q, &mq, &pq);
            stats(err_n, &mn, &pn);
            std::printf("NORMALS case=%s pixels=%u silhouette_pixels=%u ddx_mean_deg=%.3f ddx_p99_deg=%.3f ddx_wrong_axis=%u "
                        "ddx_wrong_axis_silhouette=%u neighbour_mean_deg=%.3f neighbour_p99_deg=%.3f neighbour_wrong_axis=%u "
                        "neighbour_wrong_axis_silhouette=%u\n",
                        c.name, pixels, edge, mq, pq, wrong_q, wrong_q_edge, mn, pn, wrong_n, wrong_n_edge);
        }
        report("placement_station_hull_decal_at_the_hit", on_hull(station) && res[station].lathes == 0, "");
        report("placement_capital_ship_without_profile_draws_nothing",
               res[capital].patches == 0 && res[capital].lit == 0 && res[capital].lathes == 0 &&
                   res[capital].no_profile == 1, "");
        draw_lane(g, 0.f, -1.f);
    }
    // The face cull (stage::face_culled, core::face_cull_sin): a fighter (R 100 at view z 400) hit on its back (local
    // z = 60), the owner turned about y by theta: at 0 the +z face's box faces away from the eye at every corner
    // (culled, not drawn); turning it, the first theta whose box corners lie on both sides of the cut (the largest and
    // least dot(A, eye direction) across -face_cull_sin) draws the face.
    {
        rr::WeaponPatch patches[wc::max_patches];
        rr::WeaponDecal decals[wc::max_decals];
        struct Turn {
            unsigned culled = 0, has_z = 0, patches = 0;
            double dmin = 0, dmax = 0;
        };
        stage_tier_source = wc::tier_by_station; // the face cull is the hull decal's (a bubble draws no cull)
        auto turn = [&](double th) {
            Turn o;
            stage_owner(400.f, 100.f, 1000);
            const std::int32_t c = std::int32_t(std::cos(th) * 65536.0), s = std::int32_t(std::sin(th) * 65536.0);
            const std::int32_t rows[3][3] = {{c, 0, -s}, {0, 65536, 0}, {s, 0, c}};
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned j = 0; j < 3; ++j) put32(fake_node, wc::layout::node_rot + 16 * r + 4 * j, std::uint32_t(rows[r][j]));
            stage_ring.slots[0].pos[2] = 6000;
            for (unsigned i = 0; i < wc::ring_capacity; ++i)
                if (stage_ring.slots[i].sequence) stage_ring.slots[i].pos[2] = 6000;
            const ws::Counts n = ws::build(stage_ring, 1300, &fake_read, identity_camera(), ws::Tuning{}, patches,
                                           wc::max_patches, decals, wc::max_decals);
            o.culled = n.faces_culled;
            o.patches = n.patches;
            // The +z face's box as the stage builds it (fill_patch with the owner's frame), its corners' dots.
            ws::Owner own{};
            std::memcpy(own.pos, fake_node + wc::layout::node_pos, 12);
            std::memcpy(own.rows, fake_node + wc::layout::node_rot, 48);
            own.radius = 10000;
            float centre[3], frame[9], col[3] = {1.f, 1.f, 1.f};
            ws::owner_frame(own, identity_camera(), centre, frame);
            const float pt[1][3] = {{0.f, 0.f, 60.f}}, age[1] = {0.3f};
            rr::WeaponPatch z{};
            ws::fill_patch(z, centre, frame, wc::patch_fighter, 0, pt, age, 1, col, ws::Tuning{});
            o.dmin = 1e30;
            o.dmax = -1e30;
            for (unsigned k = 0; k < 8; ++k) {
                const float u = z.box[k & 1u ? 3 : 0], v = z.box[k & 2u ? 4 : 1], a = z.box[k & 4u ? 5 : 2];
                double e[3], len = 0, d = 0;
                for (unsigned j = 0; j < 3; ++j) {
                    e[j] = -(z.centre[j] + z.view_u[j] * u + z.view_v[j] * v + z.view_a[j] * a);
                    len += e[j] * e[j];
                }
                for (unsigned j = 0; j < 3; ++j) d += z.view_a[j] * e[j] / std::sqrt(len);
                o.dmin = std::min(o.dmin, d);
                o.dmax = std::max(o.dmax, d);
            }
            for (unsigned i = 0; i < n.patches; ++i) o.has_z += patches[i].seed == 0.f;
            return o;
        };
        const Turn away = turn(0.0);
        Turn across{};
        double across_deg = -1;
        for (int deg = 1; deg <= 80 && across_deg < 0; ++deg) {
            const Turn t = turn(double(deg) * 3.14159265 / 180.0);
            if (t.dmin < -double(wc::face_cull_sin) && t.dmax > -double(wc::face_cull_sin)) {
                across = t;
                across_deg = deg;
            }
        }
        std::printf("CULL case=away culled=%u has_z=%u patches=%u dot_min=%.4f dot_max=%.4f\n", away.culled, away.has_z,
                    away.patches, away.dmin, away.dmax);
        std::printf("CULL case=across theta_deg=%.0f culled=%u has_z=%u patches=%u dot_min=%.4f dot_max=%.4f cut=%.4f\n",
                    across_deg, across.culled, across.has_z, across.patches, across.dmin, across.dmax,
                    -double(wc::face_cull_sin));
        report("face_cull_away_culled_across_drawn", away.culled >= 1 && away.has_z == 0 &&
                                                         away.dmax < -double(wc::face_cull_sin) && across_deg > 0 &&
                                                         across.has_z == 1, "");
        stage_tier_source = wc::tier_by_station;
        stage_owner(400.f, 100.f, 1000);
    }
    // The own ship in the cockpit view (core::own_cockpit_gain, Run236 Flight C): one patch on the staged owner, drawn
    // after stage::dim_own_cockpit as the glue calls it: the own ship in the chase view (unchanged), the own ship in the
    // cockpit view (its luma sum at the gain), another ship in the cockpit view (unchanged), and no own ship known.
    {
        struct View {
            const char* name;
            std::uint32_t own; // the own-ship root node the glue passes (0: none resolved)
            bool cockpit;
        };
        stage_owner(400.f, 100.f, 1000);
        const View views[5] = {{"own_chase", addr(fake_node), false},
                               {"own_cockpit", addr(fake_node), true},
                               {"other_cockpit", addr(fake_node) + 0x1000u, true},
                               {"unknown_own_cockpit", 0u, true},
                               {"own_cockpit_stale", addr(fake_node), true}};
        double sums[5] = {};
        unsigned dimmed[5] = {}, drawn[5] = {}, built[5] = {};
        ws::CockpitClock clock;
        clock.cockpit(41, 1, 100); // the chase camera's last visit, frame 100
        for (unsigned v = 0; v < 5; ++v) {
            rr::WeaponPatch patches[wc::max_patches];
            rr::WeaponDecal decals[wc::max_decals];
            const ws::Counts n = ws::build(stage_ring, 1300, &fake_read, identity_camera(), ws::Tuning{}, patches,
                                           wc::max_patches, decals, wc::max_decals);
            // the stale case: the glue's view test with no visit since frame 100, now at frame 102 (review F2)
            const bool view = v == 4 ? clock.cockpit(41, 1, 102) : views[v].cockpit;
            dimmed[v] = ws::dim_own_cockpit(patches, n.patches, views[v].own, view);
            built[v] = n.patches;
            rr::WeaponEffectsFrame f = base_frame(g);
            f.patches = patches;
            f.patch_count = n.patches;
            const double sphere[3] = {100.0, 100.0, 100.0}; // the hull through the hit (the radius): the hull decal
            draw_hull_lane(g, staged_hull(sphere));
            const auto res = draw(pass, f);
            const std::vector<float> l = read_luma(g, g.scene_s.p);
            for (float x : l) sums[v] += x > 0.f ? double(x) : 0.0;
            drawn[v] = unsigned(SUCCEEDED(res.first) && res.second.drew);
            std::printf("COCKPIT case=%s own=%u cockpit=%u patches=%u dimmed=%u drew=%u lit_pixels=%u luma_sum=%.4f\n",
                        views[v].name, unsigned(views[v].own != 0), unsigned(views[v].cockpit), n.patches, dimmed[v],
                        drawn[v], lit_count(l), sums[v]);
        }
        const double ratio = sums[0] > 0 ? sums[1] / sums[0] : 0.0;
        std::printf("COCKPIT case=summary gain=%.3f own_cockpit_ratio=%.4f other_ratio=%.4f unknown_ratio=%.4f "
                    "stale_ratio=%.4f\n", double(wc::own_cockpit_gain), ratio, sums[0] > 0 ? sums[2] / sums[0] : 0.0,
                    sums[0] > 0 ? sums[3] / sums[0] : 0.0, sums[0] > 0 ? sums[4] / sums[0] : 0.0);
        report("cockpit_dims_only_the_own_ship_in_the_cockpit",
               drawn[0] && drawn[1] && drawn[2] && drawn[3] && drawn[4] && dimmed[0] == 0 && built[1] >= 1 &&
                   dimmed[1] == built[1] && dimmed[2] == 0 &&
                   dimmed[3] == 0 && dimmed[4] == 0 && std::fabs(ratio - double(wc::own_cockpit_gain)) < 0.01 &&
                   sums[2] == sums[0] && sums[3] == sums[0] && sums[4] == sums[0],
               "");
        draw_lane(g, 0.f, -1.f);
    }
    // The own ship in the cockpit on the eye sphere (Run240 Flight G, "cut at the screen's horizontal centre"): the
    // triage's scenario (verification/results/weapon-effects/cockpit-half/): a corvette bubble of half-extents (200, 150,
    // 632) (the production walk over its hull bounds), the eye at (0, 10, 300) in its frame, level and pitched +-10
    // degrees, 32:9 at the session's half-height tangent 0.5625, the sky lane; the Run240 own-ship hits on the nose, the
    // late set from below and the early set from above (owner frame, engine units). In the cockpit view the bubble is the
    // sphere about the eye (radius the bubble's smallest half-extent, 150): each hit alone lights a patch whose centroid
    // on the sphere (the lit pixels' directions weighted by luma x their solid angle) lies within one cell of the
    // eye-sphere point along the hit's direction from the eye; every lit pixel inside a draw's box; the set's light on
    // the side of the screen its hits lie on (the luma-weighted centroid row against the hits' mean row, where that is
    // off the centre line by more than 5 % of the height); the rows either side of the centre line reported (the cut).
    // The chase view of the same records draws the same image with the own ship known as without it (the hull bubble).
    {
        Target tt;
        const bool made = make_target(tt, 2048, 576);
        const float m11 = 1.f / 0.5625f, m00 = m11 * 576.f / 2048.f;
        const double eb[3] = {200.0, 150.0, 632.0}, E[3] = {0.0, 10.0, 300.0};
        const double ext[3] = {eb[0] / double(wc::bubble_scale), eb[1] / double(wc::bubble_scale), eb[2] / double(wc::bubble_scale)};
        const std::int32_t sets[2][8][3] = {
            {{1913, -5550, 53327}, {2868, -5163, 54400}, {-1309, -6181, 51396}, {-1045, -6423, 51372},
             {929, -3229, 54449}, {2973, -5993, 54227}, {-194, -3859, 53328}, {-1300, -2776, 55004}},
            {{-3805, 1282, 54322}, {-3129, 9, 54387}, {-2658, 4093, 45565}, {-4211, -224, 51521},
             {2811, 3610, 46291}, {-1087, 3106, 52998}, {-3639, 5391, 40472}, {-5937, 2107, 52798}}};
        const float cell = ws::metres_to_view(wc::cell_m[wc::patch_corvette]);
        const double pitches[3] = {0.0, 10.0, -10.0};
        double worst_cells = 0, radius_seen = 0;
        unsigned uncovered = 0, eye_draws = 0, chase_changed = 0, singles = 0, lit_singles = 0, side_ok = 0, side_checked = 0;
        // One build and draw of the staged owner at a pitch: hits `mask` of the set, the view, the own node; the lit
        // pixels outside every draw's box counted into *outside.
        auto run = [&](unsigned set, double pitch_deg, unsigned mask, bool cockpit_view, std::uint32_t own,
                       std::vector<float>* luma, double L[3][3], unsigned* eye_patches, unsigned* outside,
                       std::uint8_t tier = wc::patch_corvette, ws::Counts* counts = nullptr) {
            const double pr = pitch_deg * 3.14159265358979 / 180.0, cp = std::cos(pr), sp = std::sin(pr);
            const double axes[3][3] = {{1, 0, 0}, {0, cp, -sp}, {0, sp, cp}}; // owner axis k in view (= world)
            stage_owner(400.f, 550.f, 0);
            wc::ring_clear(stage_ring);
            for (unsigned k = 0; k < 3; ++k) {
                double c = 0;
                for (unsigned j = 0; j < 3; ++j) c -= E[j] * axes[j][k];
                put32(fake_node, wc::layout::node_pos + 4 * k, std::uint32_t(std::int32_t(std::lround(c / double(wc::world_scale)))));
                for (unsigned j = 0; j < 3; ++j)
                    put32(fake_node, wc::layout::node_rot + 16 * k + 4 * j, std::uint32_t(std::int32_t(std::lround(axes[k][j] * 65536.0))));
                for (unsigned j = 0; j < 3; ++j) L[k][j] = axes[k][j];
            }
            stage_box_profile(ext); // the profile's bounds: the bubble's half-extents / bubble_scale
            stage_tier_source = wc::tier_by_ship_class;
            for (unsigned h = 0; h < 8; ++h)
                if (mask & (1u << h)) add_stage_hit(1000 - 10 * h, sets[set][h], tier);
            stage_tier_source = wc::tier_by_station;
            ws::Tuning t = lathe_tuning();
            t.own_node = own;
            t.cockpit = cockpit_view;
            ws::Camera cam = identity_camera();
            cam.m00 = m00;
            cam.m11 = m11;
            rr::WeaponPatch patches[wc::max_patches];
            rr::WeaponDecal decals[wc::max_decals];
            const ws::Counts n = ws::build(stage_ring, 1500, &lathe_read, cam, t, patches, wc::max_patches, decals,
                                           wc::max_decals, nullptr, &fixture_lathes);
            ws::dim_own_cockpit(patches, n.patches, own, cockpit_view);
            if (counts) *counts = n;
            *eye_patches = 0;
            for (unsigned i = 0; i < n.patches; ++i) {
                *eye_patches += patches[i].eye_sphere;
                if (patches[i].eye_sphere) radius_seen = patches[i].bubble_e[0];
            }
            rr::WeaponEffectsFrame f = base_frame(tt);
            f.m00 = m00;
            f.m11 = m11;
            f.patches = patches;
            f.patch_count = n.patches;
            draw_lane(tt, 0.f, -1.f);
            draw_on(pass, tt, f);
            *luma = read_luma(tt, tt.scene_s.p);
            *outside = 0;
            for (std::size_t i = 0; i < luma->size(); ++i) {
                if (!((*luma)[i] > 1e-3f)) continue;
                const double dv[3] = {(2.0 * double(i % tt.w) / double(tt.w) - 1.0) / double(m00),
                                      (1.0 - 2.0 * double(i / tt.w) / double(tt.h)) / double(m11), 1.0};
                bool covered = false;
                for (unsigned q = 0; q < n.patches && !covered; ++q) { // the box's slabs along the ray from the eye
                    const rr::WeaponPatch& pt = patches[q];
                    const float* ax[3] = {pt.view_u, pt.view_v, pt.view_a};
                    double tin = -1e300, tout = 1e300;
                    for (unsigned k = 0; k < 3; ++k) {
                        const double o = -(double(pt.centre[0]) * ax[k][0] + double(pt.centre[1]) * ax[k][1] +
                                           double(pt.centre[2]) * ax[k][2]);
                        const double dd = dv[0] * ax[k][0] + dv[1] * ax[k][1] + dv[2] * ax[k][2];
                        const double lo = pt.box[k], hi = pt.box[3 + k];
                        if (std::fabs(dd) < 1e-12) {
                            if (o < lo || o > hi) tin = 1, tout = 0;
                            continue;
                        }
                        double t0 = (lo - o) / dd, t1 = (hi - o) / dd;
                        if (t0 > t1) std::swap(t0, t1);
                        tin = std::max(tin, t0);
                        tout = std::min(tout, t1);
                    }
                    covered = tout >= tin && tout > double(f.near_z) * 0.999;
                }
                *outside += !covered;
            }
        };
        auto ray = [&](double x, double y, double d[3]) {
            d[0] = (2.0 * x / double(tt.w) - 1.0) / double(m00);
            d[1] = (1.0 - 2.0 * y / double(tt.h)) / double(m11);
            d[2] = 1.0;
            const double n = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            for (unsigned k = 0; k < 3; ++k) d[k] /= n;
        };
        for (unsigned set = 0; set < 2 && made; ++set)
            for (unsigned pi = 0; pi < 3; ++pi) {
                std::vector<float> l;
                double L[3][3];
                unsigned eyes = 0, out = 0;
                run(set, pitches[pi], 0xffu, true, addr(fake_node), &l, L, &eyes, &out);
                eye_draws += eyes;
                uncovered += out;
                unsigned top = 0, bottom = 0, band_above = 0, band_below = 0;
                double row_w = 0, row_sum = 0;
                for (std::size_t i = 0; i < l.size(); ++i) {
                    if (!(l[i] > 1e-3f)) continue;
                    const unsigned y = unsigned(i / tt.w);
                    ++(y < tt.h / 2 ? top : bottom);
                    band_above += y < tt.h / 2 && y + 8 >= tt.h / 2;
                    band_below += y >= tt.h / 2 && y < tt.h / 2 + 8;
                    row_sum += double(l[i]) * double(y);
                    row_w += double(l[i]);
                }
                // the hits' mean screen row (their eye-sphere points' projections)
                double hits_row = 0;
                for (unsigned h = 0; h < 8; ++h) {
                    double hv[3] = {0, 0, 0};
                    for (unsigned j = 0; j < 3; ++j)
                        for (unsigned k = 0; k < 3; ++k) hv[j] += (double(sets[set][h][k]) * double(wc::world_scale) - E[k]) * L[k][j];
                    hits_row += (1.0 - double(m11) * hv[1] / hv[2]) * 0.5 * double(tt.h) / 8.0;
                }
                const double lit_row = row_w > 0 ? row_sum / row_w : -1, centre_row = 0.5 * double(tt.h);
                if (std::fabs(hits_row - centre_row) > 0.05 * double(tt.h)) {
                    ++side_checked;
                    side_ok += lit_row > 0 && (lit_row - centre_row) * (hits_row - centre_row) > 0;
                }
                std::printf("EYE_SPHERE set=%s pitch=%.0f lit_top=%u lit_bottom=%u lit_row=%.1f hits_row=%.1f centre_row=%.1f "
                            "rows_8_above=%u rows_8_below=%u outside_boxes=%u eye_draws=%u\n",
                            set ? "early" : "late", pitches[pi], top, bottom, lit_row, hits_row, centre_row, band_above,
                            band_below, out, eyes);
                // the chase view: the own ship known or not, the same image
                std::vector<float> c_own, c_none;
                unsigned e1 = 0, e2 = 0, o1 = 0, o2 = 0;
                run(set, pitches[pi], 0xffu, false, addr(fake_node), &c_own, L, &e1, &o1);
                run(set, pitches[pi], 0xffu, false, 0u, &c_none, L, &e2, &o2);
                chase_changed += c_own != c_none || e1 || e2 || lit_count(c_own) == 0;
                // each hit alone: its centroid on the sphere against its point
                for (unsigned h = 0; h < 8; ++h) {
                    run(set, pitches[pi], 1u << h, true, addr(fake_node), &l, L, &eyes, &out);
                    uncovered += out;
                    ++singles;
                    double acc[3] = {0, 0, 0}, wsum = 0, d[3];
                    for (std::size_t i = 0; i < l.size(); ++i) {
                        if (!(l[i] > 1e-3f)) continue;
                        ray(double(i % tt.w), double(i / tt.w), d);
                        const double w = double(l[i]) * d[2] * d[2] * d[2]; // luma x the pixel's solid angle
                        for (unsigned j = 0; j < 3; ++j) acc[j] += w * d[j];
                        wsum += w;
                    }
                    if (!(wsum > 0)) continue;
                    ++lit_singles;
                    double hv[3] = {0, 0, 0}, hn = 0, an = 0;
                    for (unsigned j = 0; j < 3; ++j) {
                        for (unsigned k = 0; k < 3; ++k)
                            hv[j] += (double(sets[set][h][k]) * double(wc::world_scale) - E[k]) * L[k][j];
                        hn += hv[j] * hv[j];
                        an += acc[j] * acc[j];
                    }
                    hn = std::sqrt(hn);
                    an = std::sqrt(an);
                    double err = 0;
                    for (unsigned j = 0; j < 3; ++j)
                        err += (radius_seen * (acc[j] / an - hv[j] / hn)) * (radius_seen * (acc[j] / an - hv[j] / hn));
                    worst_cells = std::max(worst_cells, std::sqrt(err) / double(cell));
                }
            }
        std::printf("EYE_SPHERE case=summary radius=%.1f cell=%.1f eye_draws=%u singles=%u lit_singles=%u "
                    "worst_centroid_cells=%.3f outside_boxes=%u side_ok=%u/%u chase_changed=%u\n",
                    radius_seen, double(cell), eye_draws, singles, lit_singles, worst_cells, uncovered, side_ok, side_checked,
                    chase_changed);
        report("cockpit_eye_sphere_hits_light_their_half", made && side_checked >= 4 && side_ok == side_checked, "");
        // one cell plus the hex snap (a footprint centred on its hit's cell, up to 1 / sqrt 3 cell off the hit) and the
        // triplanar lift's stretch: the luma centroid of a cap on a 5-cell sphere sits about a cell from its hit
        report("cockpit_eye_sphere_centroid_within_one_cell", made && lit_singles == singles && worst_cells <= 1.0 + 0.5774 &&
                                                              uncovered == 0 && std::fabs(radius_seen - 150.0) < 0.5, "");
        report("cockpit_eye_sphere_chase_unchanged", made && chase_changed == 0 && eye_draws > 0, "");
        // FL 67 A review F2: the own ship of capital tier in the cockpit keeps the eye sphere (radius its profile bounds'
        // smallest half-extent x bubble_scale, 150); in the chase view the same capital takes its lathe.
        {
            std::vector<float> l;
            double L[3][3];
            unsigned eyes = 0, out = 0, chase_eyes = 0, chase_out = 0;
            ws::Counts cn{}, chase{};
            radius_seen = 0;
            if (made) {
                run(0, 0.0, 0xffu, true, addr(fake_node), &l, L, &eyes, &out, wc::patch_capital, &cn);
                const unsigned lit = lit_count(l);
                const double cockpit_radius = radius_seen;
                run(0, 0.0, 0xffu, false, addr(fake_node), &l, L, &chase_eyes, &chase_out, wc::patch_capital, &chase);
                std::printf("EYE_SPHERE case=capital_own cockpit_patches=%u eye_draws=%u eye_spheres=%u lathes=%u "
                            "radius=%.1f lit_pixels=%u outside_boxes=%u chase_patches=%u chase_eye_draws=%u "
                            "chase_lathes=%u\n",
                            cn.patches, eyes, cn.eye_spheres, cn.lathes, cockpit_radius, lit, out, chase.patches,
                            chase_eyes, chase.lathes);
                report("cockpit_eye_sphere_capital_own_ship",
                       cn.patches > 0 && eyes == cn.patches && cn.eye_spheres == 1 && cn.lathes == 0 &&
                           std::fabs(cockpit_radius - 150.0) < 0.5 && lit > 0 && out == 0 && chase_eyes == 0 &&
                           chase.lathes == 1 && chase.eye_spheres == 0 && chase.patches > 0, "");
            } else {
                report("cockpit_eye_sphere_capital_own_ship", false, "");
            }
        }
        tt.release();
        fixture_lathes.clear(); // the box profile's row-11 mesh must not answer for the Titan's later
        stage_owner(400.f, 100.f, 0); // the staged owner back at view (0, 0, 400 m), radius 100, an empty ring
        wc::ring_clear(stage_ring);
        draw_lane(g, 0.f, -1.f);
    }
    // 12 hits per second on one owner through the production builder, frames of 1/60 s game time for 4 s (the cell
    // cells in steady state, older hits' cells fading on their own clocks), the uniform-level look: between consecutive
    // frames no pixel drops by more than one frame's fade step per lit cell over it (the fastest cell: cell_gain (1 +
    // stagger / 2) dt / (fade (1 - stagger / 2)); the cells over a pixel counted by the same draws at a constant level)
    // plus the FP16 step. The hits land on a flat face of the owner (its front at 0.6 R, square to
    // the -z axis: one region, one depth) and on the front of a sphere of radius R (every hit at its own depth; a capital
    // the hull decal: the staged hits are a station's), with
    // the production depth cache (a lit cell keeps the depth it was first lit at until it has faded out, so a re-light
    // never moves it). Fighter: hits within 0.15 rad of the face's centre as seen from the owner's (one draw). Capital
    // spread: a 400 m owner, hits within 35 degrees: its cells overflow one draw, so the region spills into further
    // draws and nothing is dropped. Witness, recorded and not gated: the same capital fire on the flat face with one draw
    // per region (Tuning::region_draws 1, the oldest clocks evicted).
    {
        struct NoPop {
            unsigned hits = 0, frames = 0, max_live = 0, max_patches = 0, max_cells = 0, max_region_cells = 0, popped = 0;
            unsigned spills = 0, cells_dropped = 0, hits_dropped = 0, dropped = 0, worst_frame = 0, worst_x = 0, worst_y = 0;
            double worst = 0, worst_rel = 0, allowed = 0;
            float worst_prev = 0.f, worst_cur = 0.f;
        };
        auto run_nopop = [&](const char* name, std::uint8_t tier, float owner_r, float owner_z, double spread_rad,
                             const ws::Tuning& tuning, bool flat) {
            NoPop o;
            stage_owner(owner_z, owner_r, 0);
            wc::ring_clear(stage_ring);
            rr::WeaponPatch patches[wc::max_patches];
            rr::WeaponDecal decals[wc::max_decals];
            // The surface: the flat face (a plane square to the view at 0.6 R in front of the centre) or the sphere. The
            // hits are the hull decal's (the staged tier source, a station's): the sphere is the hull through the hits (R).
            if (flat)
                draw_lane(g, owner_z - 0.6f * owner_r, 1.f);
            else {
                const double k = 1.0; (void)tier;
                const double r3[3] = {k * owner_r, k * owner_r, k * owner_r};
                draw_hull_lane(g, staged_hull(r3));
            }
            const double dt = 1.0 / 60.0, fade = double(wc::fade_ms) * 1e-3;
            o.allowed = double(wc::cell_gain) * (1.0 + 0.5 * wc::stagger) * dt / (fade * (1.0 - 0.5 * wc::stagger));
            const double r_engine = double(owner_r) / double(wc::world_scale);
            std::vector<float> prev;
            for (unsigned f = 0; f <= 240; ++f) {
                const std::uint32_t now = std::uint32_t(double(f) * dt * 1000.0 + 0.5);
                while (o.hits * 1000u / 12u <= now) {
                    const double th = spread_rad * std::sqrt(halton(o.hits + 1, 2)), a = 6.2831853 * halton(o.hits + 1, 3);
                    const double face = 0.6 * r_engine, lateral = flat ? face * std::tan(th) : r_engine * std::sin(th);
                    const std::int32_t pos[3] = {std::int32_t(lateral * std::cos(a)), std::int32_t(lateral * std::sin(a)),
                                                 -std::int32_t(flat ? face : r_engine * std::cos(th))};
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
                unsigned face_cells[6] = {}; // per face (seed) over its draws
                for (unsigned i = 0; i < n.patches; ++i) face_cells[unsigned(patches[i].seed) % 6u] += patches[i].cells;
                for (unsigned c : face_cells) o.max_region_cells = std::max(o.max_region_cells, c);
                (void)cells;
                o.max_patches = std::max(o.max_patches, n.patches);
                o.max_live = std::max(o.max_live, n.hits);
                o.spills += n.spills;
                o.cells_dropped += n.cells_dropped;
                o.hits_dropped += n.hits_dropped;
                o.dropped += n.dropped;
                rr::WeaponEffectsFrame fr = base_frame(g);
                fr.patches = patches;
                fr.patch_count = n.patches;
                // One cell per lattice over each pixel, the lattices' weights summing to one: a pixel fades at most
                // one cell's step per frame.
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
        const NoPop fighter_fire = run_nopop("fighter_12hps", wc::patch_fighter, 100.f, 400.f, 0.15, ws::Tuning{}, true);
        report("no_pop_at_12_hits_per_second", fighter_fire.frames == 240 && fighter_fire.worst_rel <= 0.0 &&
                                                   fighter_fire.max_live >= 30 && fighter_fire.max_patches <= 6 &&
                                                   fighter_fire.cells_dropped == 0, "");
        const NoPop capital_fire = run_nopop("capital_spread_12hps", wc::patch_capital, 2000.f, 4000.f, 0.61, ws::Tuning{}, true);
        report("no_pop_spread_capital_map_overflows", capital_fire.frames == 240 && capital_fire.worst_rel <= 0.0 &&
                                                          capital_fire.max_live >= 30 &&
                                                          capital_fire.max_region_cells > rr::weapon_patch_cells &&
                                                          capital_fire.spills > 0 && capital_fire.cells_dropped == 0 &&
                                                          capital_fire.dropped == 0, "");
        ws::Tuning one_draw{};
        one_draw.region_draws = 1;
        const NoPop witness = run_nopop("capital_spread_one_draw_witness", wc::patch_capital, 2000.f, 4000.f, 0.61, one_draw, true);
        report("one_draw_evicts_the_oldest_clocks", witness.cells_dropped > 0 && witness.spills == 0 &&
                                                        witness.max_cells == rr::weapon_patch_cells, "");
        const NoPop fighter_sphere = run_nopop("fighter_sphere_front_12hps", wc::patch_fighter, 100.f, 400.f, 0.15,
                                               ws::Tuning{}, false);
        const NoPop capital_sphere = run_nopop("capital_sphere_front_12hps", wc::patch_capital, 2000.f, 4000.f, 0.61,
                                               ws::Tuning{}, false);
        draw_lane(g, 0.f, -1.f);
        report("no_pop_sphere_front", fighter_sphere.frames == 240 && fighter_sphere.worst_rel <= 0.0 &&
                                                     capital_sphere.frames == 240 && capital_sphere.worst_rel <= 0.0 &&
                                                     capital_sphere.cells_dropped == 0, "");
        // The station's hull decal on its flat face (the decal path, unchanged by the bubble).
        stage_tier_source = wc::tier_by_station;
        const NoPop station_fire = run_nopop("station_flat_12hps", wc::patch_capital, 2000.f, 4000.f, 0.61, ws::Tuning{}, true);
        stage_tier_source = wc::tier_by_station;
        draw_lane(g, 0.f, -1.f);
        report("no_pop_station_hull_decal", station_fire.frames == 240 && station_fire.worst_rel <= 0.0 &&
                                                station_fire.cells_dropped == 0, "");
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
        std::vector<float> img_a, img_b;
        auto peak_of = [&](std::initializer_list<double> phis, unsigned* drawn, std::vector<float>* keep) {
            turned_owner();
            for (double phi : phis) hit_at(phi);
            const ws::Counts n = ws::build(stage_ring, 1500, &fake_read, identity_camera(), ws::Tuning{}, patches,
                                           wc::max_patches, decals, wc::max_decals);
            if (drawn) *drawn = n.patches;
            const double sphere[3] = {r, r, r}; // the hull through the hits: the hull decal
            const HullLane hull = staged_hull(sphere);
            const std::vector<float> l = render(pass, patches, n.patches, &cells_only, nullptr, &hull);
            if (keep) *keep = l;
            return l.empty() ? 0.0 : double(*std::max_element(l.begin(), l.end()));
        };
        unsigned pair_draws = 0, a_draws = 0;
        const double peak_a = peak_of({seam - half_cell}, &a_draws, &img_a);
        const double peak_b = peak_of({seam + half_cell}, nullptr, &img_b);
        const double peak_pair = peak_of({seam - half_cell, seam + half_cell}, &pair_draws, nullptr);
        double peak_added = 0; // the witness: the two singles' images added (what a sum of lattices would give)
        for (std::size_t i = 0; i < img_a.size() && i < img_b.size(); ++i)
            peak_added = std::max(peak_added, double(img_a[i]) + double(img_b[i]));
        const double single = std::max(peak_a, peak_b);
        std::printf("SEAM single_draws=%u pair_draws=%u peak_a=%.4f peak_b=%.4f peak_pair=%.4f peak_singles_added=%.4f\n",
                    a_draws, pair_draws, peak_a, peak_b, peak_pair, peak_added);
        report("seam_pair_never_above_one_hit", a_draws >= 1 && pair_draws >= a_draws && single > 0.0 &&
                                                    peak_pair <= single * (1.0 + 2e-3) + 1e-3, "");
        draw_lane(g, 0.f, -1.f);
    }
    // The lathe bubble (5.1.1 "Placement", user decision 2026-11-12: capitals get the bubble; FL 67 A: the box-fitted
    // ellipsoid misfit the Titan's sides). With the Argon Titan's offline profile (titan_profile) on a TShips row 11 of
    // class M2: (a) the units: the FL 67 walk's measured Titan box (half-extents 151433 x 166374 x 528422 engine units at
    // z +17329; the walk itself was removed on 2026-11-12, its box stays as constants) against the lathe's profile bounds,
    // 1:1 with no factor; (b) placement on the CPU (map_lathe against that box's ellipsoid's bubble_point: the FL 67
    // witness): the
    // profile's surface points in the side band |z| < 300k (titan_hit_mapping.py's) at six angles per slice, and the
    // nose and tail; each lathe point on the shell (lateral radius 1 in its slice) and within (bubble_scale - 1) of the
    // contact's offset from its slice centre (the sides) or of the half length (the domes); (c) routing through ws::build
    // (user decision 2026-11-12 on FL 69 A): with the profile one lathe region, no decal, every tier alike; without one,
    // with another class, a stale table or a refused type read, scale or profile nothing (the hook forwards such a hit
    // native), the refusal counted; a station the hull decal; the own ship in the cockpit the eye sphere from the
    // profile's bounds; a belly hit's copy across the seam; (d) the GPU: the Titan on its flank and
    // from above (its hull lane: the profile's own lathe, uninflated), a side hit drawn on the lathe and, as the witness, on
    // the FL 67 walk ellipsoid (fill_bubble): the lit centroid against the hit's lathe point and the contact (metres), and
    // from above the lit pixels over sky and their distance from the hull's outline (the hug); the judging frames under
    // dump-shield/. (e) The Titan's two-hit draws for the timing rows (the hull decal as a station's, the lathe).
    guard_next_section("lathe");
    {
        const wc::ShipProfile titan = titan_profile();
        const std::int32_t unit[3] = {0x10000, 0x10000, 0x10000};
        static ws::LatheMesh mesh;
        const bool built = ws::build_lathe(titan, unit, &mesh);
        const double s = double(wc::world_scale), metre = double(wc::engine_units_per_metre) * s, pi = 3.14159265358979;
        const double view_r = double(titan_radius) * s, owner_z = 12000.0;
        const std::int32_t side_rows[3][3] = {{0, 0, -65536}, {0, 65536, 0}, {65536, 0, 0}}; // local x -> view -z
        const std::int32_t top_rows[3][3] = {{0, -65536, 0}, {0, 0, -65536}, {65536, 0, 0}};  // local y -> view -z
        auto stage_titan = [&](const std::int32_t (*rows)[3], const double contact[3], std::uint8_t tier, bool profile,
                               std::int32_t class_id = titan_class) {
            stage_tier_source = wc::tier_by_ship_class;
            stage_owner(float(owner_z), float(view_r), 1000);
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned j = 0; j < 3; ++j) put32(fake_node, wc::layout::node_rot + 16 * r + 4 * j, std::uint32_t(rows[r][j]));
            put32(fake_obj, wc::layout::obj_radius, std::uint32_t(titan_radius));
            stage_ring.slots[0].patch_tier = tier;
            stage_ring.slots[0].radius = titan_radius;
            for (unsigned k = 0; k < 3; ++k) stage_ring.slots[0].pos[k] = std::int32_t(std::lround(contact[k]));
            stage_lathe_type(class_id, profile);
        };
        auto build_titan = [&](rr::WeaponPatch* out, const ws::Tuning& t) {
            static rr::WeaponDecal decals[wc::max_decals];
            return ws::build(stage_ring, 1500, &lathe_read, identity_camera(), t, out, wc::max_patches, decals,
                             wc::max_decals, nullptr, &fixture_lathes);
        };
        // (a) the units
        double side_contact[3];
        profile_point(titan, 9, 0.5 * pi, side_contact); // slice 9 (z -197k), its +x flank
        stage_titan(side_rows, side_contact, wc::patch_capital, true);
        // the FL 67 walk's box of the Titan's hull part (engine units): half-extents ext about off (view units)
        struct {
            double lo[3], hi[3];
        } wb;
        const double walk_ext[3] = {1514.33, 1663.74, 5284.22}, walk_off[3] = {0.0, 0.0, 173.29};
        for (unsigned k = 0; k < 3; ++k) {
            wb.lo[k] = (walk_off[k] - walk_ext[k]) / s;
            wb.hi[k] = (walk_off[k] + walk_ext[k]) / s;
        }
        const bool walked = true;
        double err[3][2], worst_xz = 0, worst_y = 0;
        for (unsigned k = 0; k < 3; ++k) {
            err[k][0] = (double(mesh.hull_lo[k]) - double(wb.lo[k]) * s) / metre;
            err[k][1] = (double(mesh.hull_hi[k]) - double(wb.hi[k]) * s) / metre;
            for (double e : err[k]) (k == 1 ? worst_y : worst_xz) = std::max(k == 1 ? worst_y : worst_xz, std::fabs(e));
        }
        std::printf("LATHE case=units built=%u walked=%u walk_lo_m=%.2f,%.2f,%.2f walk_hi_m=%.2f,%.2f,%.2f "
                    "profile_lo_m=%.2f,%.2f,%.2f profile_hi_m=%.2f,%.2f,%.2f err_x_m=%.3f,%.3f err_y_m=%.3f,%.3f "
                    "err_z_m=%.3f,%.3f factor=%.4f perimeter_m=%.1f dome_m=%.1f,%.1f\n",
                    unsigned(built), unsigned(walked), wb.lo[0] * s / metre, wb.lo[1] * s / metre, wb.lo[2] * s / metre,
                    wb.hi[0] * s / metre, wb.hi[1] * s / metre, wb.hi[2] * s / metre, mesh.hull_lo[0] / metre,
                    mesh.hull_lo[1] / metre, mesh.hull_lo[2] / metre, mesh.hull_hi[0] / metre, mesh.hull_hi[1] / metre,
                    mesh.hull_hi[2] / metre, err[0][0], err[0][1], err[1][0], err[1][1], err[2][0], err[2][1],
                    double(unit[0]) / 65536.0, mesh.perimeter / metre, mesh.dome_depth[0] / metre,
                    mesh.dome_depth[1] / metre);
        // x and z: the profile's widest slice and its ends are the walk's part box (1:1); y: the walk's LOD extent is the
        // symmetric max |y| of the body (166374) while the profile keeps the slices' true asymmetric span (2 % of it).
        report("lathe_profile_units_match_the_walk",
               built && walked && worst_xz < 0.5 && worst_y < 0.02 * 1663.74 / metre, "");
        {   // review F5: one Titan mesh's cold build (build_lathe from the profile into a cleared mesh, the cache's miss),
            // best and mean of 200 (us); the stage builds at most lathe_builds_per_frame a frame
            static ws::LatheMesh cold;
            LARGE_INTEGER freq, a, b;
            QueryPerformanceFrequency(&freq);
            double best = 1e30, sum = 0;
            bool ok = true;
            for (unsigned rep = 0; rep < 200; ++rep) {
                std::memset(static_cast<void*>(&cold), 0, sizeof cold);
                QueryPerformanceCounter(&a);
                ok = ws::build_lathe(titan, unit, &cold) && ok;
                QueryPerformanceCounter(&b);
                const double us = ticks_us(a, b, freq);
                best = std::min(best, us);
                sum += us;
            }
            std::printf("LATHE case=build_cost built=%u best_us=%.1f mean_us=%.1f builds_per_frame=%u frame_bound_us=%.1f\n",
                        unsigned(ok), best, sum / 200.0, ws::lathe_builds_per_frame,
                        sum / 200.0 * double(ws::lathe_builds_per_frame));
            report("lathe_build_cost_measured", ok && best > 0.0, "");
        }

        // (b) placement on the CPU
        // the walk's ellipsoid of that box (its centre, its half-extents x bubble_scale: under bubble_scale x the radius)
        ws::Bubble wbub;
        for (unsigned k = 0; k < 3; ++k) {
            wbub.c[k] = float(walk_off[k]);
            wbub.e[k] = float(walk_ext[k]) * wc::bubble_scale;
        }
        const bool wfit = true;
        std::vector<double> lathe_move, walk_move;
        double shell_err = 0, side_excess = -1e30;
        unsigned side_points = 0;
        for (int i = 0; i < 32; ++i) {
            const double zc = double(titan_z0) + (i + 0.5) * (double(titan_z1) - double(titan_z0)) / 32.0;
            if (std::fabs(zc) >= 300000.0) continue;
            for (double deg : {60.0, 90.0, 120.0, -60.0, -90.0, -120.0}) {
                double p[3];
                profile_point(titan, i, deg * pi / 180.0, p);
                const float pv[3] = {float(p[0] * s), float(p[1] * s), float(p[2] * s)};
                float B[3], chart[2], Bw[3], q[3];
                ws::map_lathe(mesh, pv, B, chart);
                ws::bubble_point(wbub, pv, Bw, q);
                const float* e = mesh.ring[ws::lathe_dome_rings + unsigned(i)];
                const double rx = (double(B[0]) - e[0]) / e[2], ry = (double(B[1]) - e[1]) / e[3];
                shell_err = std::max(shell_err, std::fabs(std::sqrt(rx * rx + ry * ry) - 1.0));
                double m = 0, w = 0, lat = 0;
                for (unsigned k = 0; k < 3; ++k) {
                    m += (double(B[k]) - pv[k]) * (double(B[k]) - pv[k]);
                    w += (double(Bw[k]) - pv[k]) * (double(Bw[k]) - pv[k]);
                }
                lat = std::hypot(pv[0] - double(titan.slice[i][0]) * s, pv[1] - double(titan.slice[i][1]) * s);
                lathe_move.push_back(std::sqrt(m) / metre);
                walk_move.push_back(std::sqrt(w) / metre);
                side_excess = std::max(side_excess, std::sqrt(m) - (double(wc::bubble_scale) - 1.0) * lat * 1.01 - 1e-3 * metre);
                ++side_points;
            }
        }
        auto median = [](std::vector<double> v) {
            std::sort(v.begin(), v.end());
            return v.empty() ? 0.0 : v[v.size() / 2];
        };
        auto vmax = [](const std::vector<double>& v) { return v.empty() ? 0.0 : *std::max_element(v.begin(), v.end()); };
        const double half_length_m = 0.5 * (double(titan_z1) - double(titan_z0)) * s / metre;
        double end_move[2] = {0, 0}, end_walk[2] = {0, 0};
        for (unsigned end = 0; end < 2; ++end) { // the tail and the nose on the axis of their end slices
            double p[3];
            profile_point(titan, end ? 32 : -1, 0.0, p);
            p[0] = titan.slice[end ? 31 : 0][0];
            p[1] = titan.slice[end ? 31 : 0][1];
            const float pv[3] = {float(p[0] * s), float(p[1] * s), float(p[2] * s)};
            float B[3], chart[2], Bw[3], q[3];
            ws::map_lathe(mesh, pv, B, chart);
            ws::bubble_point(wbub, pv, Bw, q);
            double m = 0, w = 0;
            for (unsigned k = 0; k < 3; ++k) {
                m += (double(B[k]) - pv[k]) * (double(B[k]) - pv[k]);
                w += (double(Bw[k]) - pv[k]) * (double(Bw[k]) - pv[k]);
            }
            end_move[end] = std::sqrt(m) / metre;
            end_walk[end] = std::sqrt(w) / metre;
        }
        const double end_allowed = (double(wc::bubble_scale) - 1.0) * half_length_m * 1.01 + 0.5;
        std::printf("LATHE case=placement side_points=%u lathe_median_m=%.1f lathe_max_m=%.1f walk_median_m=%.1f "
                    "walk_max_m=%.1f shell_err=%.6f side_excess_m=%.3f stern_m=%.1f bow_m=%.1f walk_stern_m=%.1f "
                    "walk_bow_m=%.1f end_allowed_m=%.1f walk_fit=%u walk_e_m=%.1f,%.1f,%.1f\n",
                    side_points, median(lathe_move), vmax(lathe_move), median(walk_move), vmax(walk_move), shell_err,
                    side_excess / metre, end_move[0], end_move[1], end_walk[0], end_walk[1], end_allowed, unsigned(wfit),
                    wbub.e[0] / metre, wbub.e[1] / metre, wbub.e[2] / metre);
        report("lathe_side_hits_on_the_shell", side_points >= 30 && shell_err < 1e-3 && side_excess <= 0.0 && wfit &&
                                                   median(lathe_move) * 4.0 < median(walk_move), "");
        report("lathe_bow_stern_on_the_domes", end_move[0] > 0.0 && end_move[0] <= end_allowed && end_move[1] > 0.0 &&
                                                   end_move[1] <= end_allowed, "");

        // (c) routing (user decision 2026-11-12 on FL 69 A): per tier and per refusal the expected surface (the lathe, the
        // hull decal, the eye sphere, or nothing: the hook forwards such a hit native) and the refusal counter that must
        // read 1.
        static rr::WeaponPatch out[wc::max_patches];
        enum Expect : std::uint8_t { lathe_surface, native_surface, decal_surface, eye_surface };
        enum Mod : std::uint8_t { no_mod, stale_rows, row_past_count, scale_zero, scale_over, degenerate, station };
        struct Route {
            const char* name;
            std::uint8_t tier;
            bool profile, cockpit;
            std::int32_t class_id;
            Mod mod;
            Expect expect;
            unsigned ws::Counts::*counter; // the refusal that must count 1 (nullptr: none)
        };
        const Route routes[] = {
            {"capital_profile", wc::patch_capital, true, false, titan_class, no_mod, lathe_surface, nullptr},
            {"corvette_profile", wc::patch_corvette, true, false, titan_class, no_mod, lathe_surface, nullptr},
            {"fighter_profile", wc::patch_fighter, true, false, titan_class, no_mod, lathe_surface, nullptr},
            {"capital_no_profile", wc::patch_capital, false, false, titan_class, no_mod, native_surface,
             &ws::Counts::lathe_no_profile},
            {"capital_class_mismatch", wc::patch_capital, true, false, 2023, no_mod, native_surface,
             &ws::Counts::lathe_mismatch},
            {"fighter_no_profile", wc::patch_fighter, false, false, titan_class, no_mod, native_surface,
             &ws::Counts::lathe_no_profile},
            {"cockpit_profile", wc::patch_capital, true, true, titan_class, no_mod, eye_surface, nullptr},
            {"capital_stale_table", wc::patch_capital, true, false, titan_class, stale_rows, native_surface,
             &ws::Counts::lathe_stale},
            {"capital_row_past_count", wc::patch_capital, true, false, titan_class, row_past_count, native_surface,
             &ws::Counts::lathe_unreadable},
            {"capital_axis_scale_zero", wc::patch_capital, true, false, titan_class, scale_zero, native_surface,
             &ws::Counts::lathe_scale},
            {"capital_axis_scale_257x", wc::patch_capital, true, false, titan_class, scale_over, native_surface,
             &ws::Counts::lathe_scale},
            {"capital_degenerate_profile", wc::patch_capital, true, false, titan_class, degenerate, native_surface,
             &ws::Counts::lathe_degenerate},
            {"station", wc::patch_capital, true, false, titan_class, station, decal_surface, nullptr}};
        constexpr unsigned route_count = sizeof routes / sizeof routes[0];
        bool route_ok[route_count] = {};
        for (unsigned r = 0; r < route_count; ++r) {
            const Route& c = routes[r];
            stage_titan(side_rows, side_contact, c.tier, c.profile, c.class_id);
            if (c.mod == stale_rows) lathe_profiles.tships_rows = type_rows + 1;
            if (c.mod == row_past_count) { // a table of 11 rows (recorded and live alike, not stale): row 11 is past it
                put32(fake_types, type_slots_at + 4, std::uint32_t(titan_row));
                lathe_profiles.tships_rows = std::uint32_t(titan_row);
            }
            if (c.mod == scale_zero) put32(fake_node, wc::layout::node_axis_scale + 4, 0);
            if (c.mod == scale_over) put32(fake_node, wc::layout::node_axis_scale + 8, 257u * 0x10000u);
            if (c.mod == degenerate) {
                for (auto& s : lathe_profiles.entries[0].slice) s[2] = s[3] = 0.f; // no width: build_lathe refuses
                fixture_lathes.clear(); // the Titan's cached mesh would answer the row otherwise
            }
            if (c.mod == station) stage_ring.slots[0].tier_source = wc::tier_by_station; // a dock or factory's hit
            ws::Tuning t = lathe_tuning();
            if (c.cockpit) {
                t.own_node = addr(fake_node);
                t.cockpit = true;
            }
            const ws::Counts n = build_titan(out, t);
            unsigned lathe_draws = 0, bubble_draws = 0, eye = 0;
            for (unsigned i = 0; i < n.patches; ++i) {
                lathe_draws += out[i].lathe != nullptr;
                bubble_draws += out[i].bubble;
                eye += out[i].eye_sphere;
            }
            const unsigned refusals = n.lathe_deferred + n.lathe_no_profile + n.lathe_mismatch + n.lathe_unreadable + n.lathe_scale +
                                      n.lathe_degenerate + n.lathe_cache + n.lathe_stale;
            const bool counted = c.counter ? n.*c.counter == 1 && refusals == 1 : refusals == 0;
            bool surface = false;
            switch (c.expect) {
            case lathe_surface:
                surface = n.lathes == 1 && lathe_draws == n.patches && n.patches >= 1 && !bubble_draws && !n.decals;
                break;
            case native_surface:
                surface = n.patches == 0 && n.decals == 0 && !n.lathes && !n.eye_spheres;
                break;
            case decal_surface:
                surface = n.patches && !lathe_draws && !bubble_draws && !n.lathes;
                break;
            case eye_surface:
                surface = n.eye_spheres == 1 && eye == n.patches && n.patches >= 1 && !n.lathes;
                break;
            }
            route_ok[r] = surface && counted;
            std::printf("LATHE case=route_%s patches=%u lathes=%u lathe_draws=%u bubble_draws=%u eye=%u "
                        "decals=%u mismatch=%u no_profile=%u unreadable=%u scale=%u degenerate=%u cache=%u stale=%u "
                        "ok=%u\n",
                        c.name, n.patches, n.lathes, lathe_draws, bubble_draws, eye, n.decals,
                        n.lathe_mismatch, n.lathe_no_profile, n.lathe_unreadable, n.lathe_scale, n.lathe_degenerate,
                        n.lathe_cache, n.lathe_stale, unsigned(route_ok[r]));
        }
        report("lathe_route_capital_takes_the_lathe", route_ok[0], "");
        report("lathe_route_fighter_corvette_take_the_lathe", route_ok[1] && route_ok[2], "");
        report("lathe_route_no_profile_draws_nothing", route_ok[3] && route_ok[5], "");
        report("lathe_route_class_mismatch_draws_nothing", route_ok[4], "");
        report("lathe_route_cockpit_keeps_eye_sphere", route_ok[6], "");
        report("lathe_route_stale_table_draws_nothing", route_ok[7], "");
        report("lathe_route_refusals_draw_nothing", route_ok[8] && route_ok[9] && route_ok[10] && route_ok[11], "");
        report("lathe_route_station_keeps_the_decal", route_ok[12], "");
        {   // LatheCache eviction: 32 types (rows 0..31, one mesh each) fill the cache in one stage call; a 33rd in the
            // same call is refused (every entry drawn this call); in the next call a type drawn again keeps its mesh
            // (same pointer, same row) and a new type evicts the least recently used of the others.
            static ws::LatheCache cache;
            const std::int32_t unit_axis[3] = {0x10000, 0x10000, 0x10000};
            const wc::ShipProfile base = titan_profile();
            static wc::ShipProfile profiles[ws::lathe_cache_entries + 2];
            const ws::LatheMesh* mesh[ws::lathe_cache_entries + 2] = {};
            auto type = [&](unsigned i) {
                profiles[i] = base;
                profiles[i].row = std::int32_t(i);
                ws::LatheType lt;
                lt.profile = &profiles[i];
                lt.row = std::int32_t(i);
                for (unsigned k = 0; k < 3; ++k) lt.axis[k] = unit_axis[k];
                return lt;
            };
            cache.frame();
            ws::LatheRefusal why = ws::LatheRefusal::none;
            unsigned filled = 0;
            for (unsigned i = 0; i < ws::lathe_cache_entries; ++i) filled += (mesh[i] = cache.get(type(i), &why)) != nullptr;
            ws::LatheRefusal full_why = ws::LatheRefusal::none;
            const ws::LatheMesh* full = cache.get(type(ws::lathe_cache_entries), &full_why);
            cache.frame();
            const ws::LatheMesh* again = cache.get(type(5), &why);
            const ws::LatheMesh* fresh = cache.get(type(ws::lathe_cache_entries + 1), &why);
            unsigned evicted_row = 9999, kept5 = 0;
            for (const auto& e : cache.entries) {
                if (&e.mesh == fresh) evicted_row = 0; // the slot reused: which row did it hold? (the LRU: row 0)
                if (e.valid && e.row == 5 && &e.mesh == mesh[5]) kept5 = 1;
            }
            const bool lru = fresh == mesh[0] && again == mesh[5];
            std::printf("LATHE case=cache_eviction filled=%u builds=%lu full_refused=%u full_why=%u again_same=%u kept5=%u "
                        "fresh_took_row0_slot=%u refused=%lu\n",
                        filled, static_cast<unsigned long>(cache.builds), unsigned(full == nullptr), unsigned(full_why),
                        unsigned(again == mesh[5]), kept5, unsigned(lru), static_cast<unsigned long>(cache.refused));
            report("lathe_cache_full_refuses_then_evicts_the_lru",
                   filled == ws::lathe_cache_entries && !full && full_why == ws::LatheRefusal::cache && lru && kept5 &&
                       evicted_row == 0 && cache.builds == ws::lathe_cache_entries + 1,
                   "");
        }
        {
            double belly[3];
            profile_point(titan, 12, pi, belly); // the seam's side (-y)
            stage_titan(side_rows, belly, wc::patch_capital, true);
            const ws::Counts n = build_titan(out, lathe_tuning());
            unsigned plus = 0, minus = 0;
            for (unsigned i = 0; i < n.patches; ++i) {
                const rr::WeaponPatch& p = out[i];
                for (unsigned k = 0; k < p.cells; ++k) {
                    const std::uint32_t packed = std::uint32_t(std::int32_t(p.cell_key[k]));
                    const double v = 0.8660254 * double(int(packed & 0xfffu) - rr::weapon_cell_key_bias) * p.cell;
                    plus += v > 0.45 * p.lathe_period;
                    minus += v < -0.45 * p.lathe_period;
                }
            }
            std::printf("LATHE case=seam patches=%u seam_copies=%u cells_plus=%u cells_minus=%u period_m=%.1f\n", n.patches,
                        n.lathe_seam, plus, minus, n.patches ? out[0].lathe_period / metre : 0.0);
            report("lathe_seam_hit_lights_both_sides", n.lathe_seam == 1 && plus > 0 && minus > 0, "");
        }

        // (d) the GPU
        const LatheHullLane lane_side = [&] {
            stage_titan(side_rows, side_contact, wc::patch_capital, true);
            return lathe_hull_lane(titan);
        }();
        rr::WeaponLook near_only = rr::default_weapon_look();
        near_only.bubble_far_gain = 0.f;
        struct View {
            const char* name;
            const std::int32_t (*rows)[3];
        } views[2] = {{"side", side_rows}, {"top", top_rows}};
        double offset_m[2][2] = {}, centroid_cells[2] = {1e30, 1e30}, gap_max_m[2][2] = {}, gap_mean_m[2][2] = {};
        unsigned lit[2][2] = {}, sky_lit[2][2] = {};
        for (unsigned vi = 0; vi < 2; ++vi) {
            stage_titan(views[vi].rows, side_contact, wc::patch_capital, true);
            const LatheHullLane lane = lathe_hull_lane(titan);
            draw_lathe_hull(g, lane);
            const std::vector<float> depth = read_lane_depth(g);
            // the contact, the lathe point and the walk point in view
            const float pv[3] = {float(side_contact[0] * s), float(side_contact[1] * s), float(side_contact[2] * s)};
            float B[3], chart[2], Bw[3], q[3];
            ws::map_lathe(mesh, pv, B, chart);
            ws::bubble_point(wbub, pv, Bw, q);
            double P[3], BL[3], BW[3];
            const double pd[3] = {pv[0], pv[1], pv[2]}, bd[3] = {B[0], B[1], B[2]}, wd[3] = {Bw[0], Bw[1], Bw[2]};
            owner_to_view(pd, P);
            owner_to_view(bd, BL);
            owner_to_view(wd, BW);
            const double m00 = 1.6, half_w = 0.5 * g.w, half_h = 0.5 * g.h, m11 = 1.6 * double(g.w) / double(g.h);
            auto screen = [&](const double v[3], double out2[2]) {
                out2[0] = half_w * (1.0 + m00 * v[0] / v[2]);
                out2[1] = half_h * (1.0 - m11 * v[1] / v[2]);
            };
            double sp[2], sb[2];
            screen(P, sp);
            screen(BL, sb);
            const double view_per_px = P[2] / (m00 * half_w);
            for (unsigned kind = 0; kind < 2; ++kind) { // 0 the lathe, 1 the FL 67 walk ellipsoid
                rr::WeaponPatch draws[wc::max_patches];
                unsigned count = 0;
                if (!kind) {
                    count = build_titan(draws, lathe_tuning()).patches;
                } else {
                    ws::Owner o;
                    std::memcpy(o.pos, fake_node + wc::layout::node_pos, 12);
                    std::memcpy(o.rows, fake_node + wc::layout::node_rot, 48);
                    o.radius = titan_radius;
                    float centre[3], frame[9], colour[3];
                    ws::owner_frame(o, identity_camera(), centre, frame);
                    wc::shield_colour(1.f, ws::Tuning{}.bands, colour);
                    const float hit[1][3] = {{pv[0], pv[1], pv[2]}}, age[1] = {0.5f};
                    unsigned left = ws::anchor_count;
                    ws::Counts nn{};
                    ws::fill_bubble(draws, wc::max_patches, &left, centre, frame, wc::patch_capital, wbub, hit, age, 1,
                                    colour, ws::Tuning{}, &nn);
                    count = nn.patches;
                    for (unsigned d = 0; d < count; ++d) draws[d].owner_radius = float(view_r);
                }
                std::vector<float> rgb;
                const std::vector<float> l = render(pass, draws, count, &near_only, nullptr, nullptr, true);
                render(pass, draws, count, nullptr, &rgb, nullptr, true);
                char name[64];
                std::snprintf(name, sizeof name, "titan_%s_%s", views[vi].name, kind ? "ellipsoid" : "lathe");
                dump_shield(name, rgb, depth, g.w, g.h);
                double wsum = 0, cx = 0, cy = 0, gsum = 0;
                unsigned gaps = 0;
                for (UINT y = 0; y < g.h; ++y)
                    for (UINT x = 0; x < g.w; ++x) {
                        const std::size_t i = std::size_t(y) * g.w + x;
                        const double v = l[i]; // the near side: the centroid
                        if (v > 1e-3) {
                            ++lit[vi][kind];
                            wsum += v;
                            cx += v * x;
                            cy += v * y;
                        }
                        // the whole look (both sides): the pixels over sky and their gap from the outline
                        if (rgb.size() != l.size() * 3 ||
                            !(std::max(rgb[3 * i], std::max(rgb[3 * i + 1], rgb[3 * i + 2])) > 1e-3f) || depth[i] > 0.f)
                            continue;
                        ++sky_lit[vi][kind];
                        // its distance from the hull's outline: the nearest hull pixel along the column (the top view
                        // puts the flank below the hull: up the column)
                        unsigned d = 1;
                        while (d <= 600 && y >= d && !(depth[i - std::size_t(d) * g.w] > 0.f)) ++d;
                        if (d <= 600 && y >= d) {
                            const double gm = d * view_per_px / metre;
                            gap_max_m[vi][kind] = std::max(gap_max_m[vi][kind], gm);
                            gsum += gm;
                            ++gaps;
                        }
                    }
                gap_mean_m[vi][kind] = gaps ? gsum / gaps : 0.0;
                if (wsum > 0) {
                    cx /= wsum;
                    cy /= wsum;
                    offset_m[vi][kind] = std::hypot(cx - sp[0], cy - sp[1]) * view_per_px / metre;
                    if (!kind) {
                        const double cell_px = double(ws::metres_to_view(wc::cell_m[wc::patch_capital])) / view_per_px;
                        centroid_cells[vi] = std::hypot(cx - sb[0], cy - sb[1]) / cell_px;
                    }
                }
                std::printf("LATHE case=gpu_%s_%s draws=%u lit_pixels=%u sky_lit=%u centroid_from_contact_m=%.1f "
                            "centroid_from_lathe_point_cells=%.3f sky_gap_max_m=%.1f sky_gap_mean_m=%.1f\n",
                            views[vi].name, kind ? "ellipsoid" : "lathe", count, lit[vi][kind], sky_lit[vi][kind],
                            offset_m[vi][kind], kind ? -1.0 : centroid_cells[vi], gap_max_m[vi][kind],
                            gap_mean_m[vi][kind]);
            }
        }
        double hx_max = 0;
        for (int i = 7; i <= 11; ++i) hx_max = std::max(hx_max, double(titan.slice[i][2]));
        const double hug_allowed_m = (double(wc::bubble_scale) - 1.0) * hx_max * s / metre * 1.1 + 2.0;
        std::printf("LATHE case=gpu_summary hug_allowed_m=%.1f\n", hug_allowed_m);
        // The side: the near side's luma centroid within 1.5 cells of the lathe point on screen (the footprint is centred
        // on the hit's cell, within half a cell of the hit, and foreshortened on the curved flank), far closer to the
        // contact than the walk ellipsoid's. From above the hit lies on the silhouette (its cells edge-on: few pixels):
        // every lathe pixel over sky within the lathe's inflation of the hull's outline, the walk ellipsoid's past it.
        report("lathe_gpu_side_hit_drawn_at_its_lathe_point",
               lit[0][0] > 100 && centroid_cells[0] <= 1.5 && offset_m[0][0] * 4.0 < offset_m[0][1], "");
        report("lathe_gpu_top_view_hugs_the_hull",
               sky_lit[1][0] > 50 && gap_max_m[1][0] <= hug_allowed_m && gap_max_m[1][1] > hug_allowed_m, "");
        {   // a judging frame of a hit on a smooth stretch (slice 6, z -301k: its neighbours' widths within 8 %)
            double smooth[3];
            profile_point(titan, 6, 0.5 * pi, smooth);
            stage_titan(side_rows, smooth, wc::patch_capital, true);
            draw_lathe_hull(g, lane_side);
            const std::vector<float> depth = read_lane_depth(g);
            rr::WeaponPatch draws[wc::max_patches];
            const unsigned count = build_titan(draws, lathe_tuning()).patches;
            std::vector<float> rgb;
            render(pass, draws, count, nullptr, &rgb, nullptr, true);
            dump_shield("titan_side_lathe_smooth", rgb, depth, g.w, g.h);
            std::printf("LATHE case=dump_smooth draws=%u\n", count);
            // The bow (2026-11-12 look fix: the cells' shrink across the hull clamped at lathe_min_width): a hit on the
            // narrow forward slice 30 at 60 degrees and one on the nose (the end slice's centre at z1), seen from the side
            // and from 35 degrees ahead of the beam (local z toward the eye: the bow's dome faces it).
            double bow_side[3], nose[3];
            profile_point(titan, 30, pi / 3.0, bow_side);
            profile_point(titan, 32, 0.0, nose);
            nose[0] = titan.slice[31][0];
            nose[1] = titan.slice[31][1];
            const double c35 = std::cos(35.0 * pi / 180.0), s35 = std::sin(35.0 * pi / 180.0);
            const std::int32_t ahead_rows[3][3] = {{std::int32_t(std::lround(-65536 * s35)), 0, std::int32_t(std::lround(-65536 * c35))},
                                                   {0, 65536, 0},
                                                   {std::int32_t(std::lround(65536 * c35)), 0, std::int32_t(std::lround(-65536 * s35))}};
            for (unsigned v = 0; v < 2; ++v) {
                stage_titan(v ? ahead_rows : side_rows, bow_side, wc::patch_capital, true);
                const std::int32_t at[3] = {std::int32_t(std::lround(nose[0])), std::int32_t(std::lround(nose[1])),
                                            std::int32_t(std::lround(nose[2]))};
                add_stage_hit(1200, at, wc::patch_capital);
                const LatheHullLane lane = lathe_hull_lane(titan);
                draw_lathe_hull(g, lane);
                const std::vector<float> bow_depth = read_lane_depth(g);
                const unsigned bow_count = build_titan(draws, lathe_tuning()).patches;
                std::vector<float> bow_rgb;
                render(pass, draws, bow_count, nullptr, &bow_rgb, nullptr, true);
                dump_shield(v ? "titan_bow_ahead_lathe" : "titan_bow_side_lathe", bow_rgb, bow_depth, g.w, g.h);
                std::printf("LATHE case=dump_bow view=%s draws=%u\n", v ? "ahead" : "side", bow_count);
            }
        }

        // (f) FL 69 A (2026-11-12, "the lathe darkened the whole hull"): the flight's geometry, the Titan's +x flank 446 m
        // from the eye (the hull past the screen's edges), 16 live hits spread over the flank at ages 0.1 to 2.5 s (the
        // flight's live=13..16 under sustained fire), the bright lit hull as the scene colour (draw_lathe_hull_colour).
        // A = the hull alone, B = the hull + the lathe draws, C = the lathe draws on black (the cells: any channel > 0).
        // Outside the cells B - A is 0 in every channel, alpha included; inside B - A >= 0 and B - A = C to fp16 rounding
        // (the pass adds its radiance and nothing else). The same through the production TemporalPass (the stage callback
        // in its bracket, the flown far / camera-gate configuration, 8 frames without jitter): stage on - off is 0 outside
        // the cells dilated by 3 px and nowhere negative. Last, the records past their life draw nothing.
        {
            const double flank_m = 446.0, half_width_m = double(mesh.hull_hi[0]) / metre;
            const unsigned now = 4000;
            stage_titan(side_rows, side_contact, wc::patch_capital, true);
            put32(fake_node, wc::layout::node_pos + 8,
                  std::uint32_t(std::int32_t(std::lround((flank_m + half_width_m) * metre / s))));
            stage_ring.slots[0].t0_game_ms = now - 2500;
            unsigned max_t0 = now - 2500, hits = 1;
            const int spread_slices[15] = {3, 5, 7, 8, 10, 11, 13, 14, 16, 18, 19, 21, 23, 25, 27};
            for (unsigned k = 0; k < 15; ++k) {
                double p[3];
                profile_point(titan, spread_slices[k], (50.0 + 80.0 * double((k * 7) % 15) / 14.0) * pi / 180.0, p);
                const std::int32_t at[3] = {std::int32_t(std::lround(p[0])), std::int32_t(std::lround(p[1])),
                                            std::int32_t(std::lround(p[2]))};
                const unsigned t0 = now - 100 - 160 * ((k * 4) % 15);
                add_stage_hit(t0, at, wc::patch_capital);
                max_t0 = std::max(max_t0, t0);
                ++hits;
            }
            titan_flight_lane = lathe_hull_lane(titan);
            draw_lathe_hull(g, titan_flight_lane);
            const std::vector<float> depth = read_lane_depth(g);
            static rr::WeaponDecal flight_decals[wc::max_decals];
            const ws::Counts n = ws::build(stage_ring, now, &lathe_read, identity_camera(), lathe_tuning(), titan_flight,
                                           wc::max_patches, flight_decals, wc::max_decals, nullptr, &fixture_lathes);
            titan_flight_count = n.patches;
            unsigned cells = 0, lathe_draws = 0, quads = 0;
            for (unsigned i = 0; i < n.patches; ++i) {
                const rr::WeaponPatch& p = titan_flight[i];
                cells += p.cells;
                lathe_draws += p.lathe != nullptr;
                quads += unsigned(p.lathe_r1 - p.lathe_r0) * unsigned(p.lathe_c1 - p.lathe_c0);
            }
            rr::WeaponEffectsFrame f = base_frame(g);
            f.patches = titan_flight;
            f.patch_count = n.patches;
            const auto on_black = draw(pass, f);
            const std::vector<float> C = read_rgba(g, g.scene_s.p);
            draw_lathe_hull_colour(g, titan_flight_lane);
            const std::vector<float> A = read_rgba(g, g.scene_s.p);
            draw_lathe_hull_colour(g, titan_flight_lane);
            const auto over = draw_over(pass, f);
            const std::vector<float> B = read_rgba(g, g.scene_s.p);
            const std::size_t px = std::size_t(g.w) * g.h;
            const bool read_ok = C.size() == px * 4 && A.size() == px * 4 && B.size() == px * 4 && depth.size() == px;
            std::vector<unsigned char> lit(px, 0), around(px, 0);
            unsigned lit_px = 0, hull_px = 0, lit_hull_px = 0, out_changed = 0, darkened = 0;
            double out_max = 0, alpha_max = 0, min_delta = 0, add_err = 0;
            for (std::size_t i = 0; read_ok && i < px; ++i) {
                lit[i] = C[4 * i] > 0.f || C[4 * i + 1] > 0.f || C[4 * i + 2] > 0.f;
                lit_px += lit[i];
                hull_px += depth[i] > 0.f;
                lit_hull_px += lit[i] && depth[i] > 0.f;
                bool changed = false;
                for (unsigned k = 0; k < 4; ++k) {
                    const double d = double(B[4 * i + k]) - double(A[4 * i + k]);
                    if (k == 3) alpha_max = std::max(alpha_max, std::fabs(d));
                    if (!lit[i]) {
                        out_max = std::max(out_max, std::fabs(d));
                        changed = changed || d != 0.0;
                    } else if (k < 3) {
                        min_delta = std::min(min_delta, d);
                        darkened += d < 0.0;
                        add_err = std::max(add_err, std::fabs(d - double(C[4 * i + k])) /
                                                        std::max(double(B[4 * i + k]), 1e-3));
                    }
                }
                out_changed += changed;
            }
            // the cells dilated by 3 px (the resolve's neighbourhood and history taps)
            for (UINT y = 0; read_ok && y < g.h; ++y)
                for (UINT x = 0; x < g.w; ++x) {
                    if (!lit[std::size_t(y) * g.w + x]) continue;
                    for (int dy = -3; dy <= 3; ++dy)
                        for (int dx = -3; dx <= 3; ++dx) {
                            const int xx = int(x) + dx, yy = int(y) + dy;
                            if (xx >= 0 && yy >= 0 && xx < int(g.w) && yy < int(g.h)) around[std::size_t(yy) * g.w + xx] = 1;
                        }
                }
            std::printf("LATHE case=flight_spread flank_m=%.0f hits=%u draws=%u lathe_draws=%u lathes=%u cells=%u "
                        "cells_per_hit=%.1f cells_dropped=%u band_quads=%u lit_px=%u hull_px=%u lit_hull_share=%.4f "
                        "on_black=%08lx over=%08lx\n",
                        flank_m, hits, n.patches, lathe_draws, n.lathes, cells, double(cells) / double(hits), n.cells_dropped,
                        quads, lit_px, hull_px, hull_px ? double(lit_hull_px) / double(hull_px) : 0.0,
                        static_cast<unsigned long>(on_black.first), static_cast<unsigned long>(over.first));
            std::printf("LATHE case=bright_hull read=%u out_changed_px=%u out_max=%.6g alpha_max=%.6g min_delta=%.6g "
                        "darkened=%u additive_rel_err=%.6g\n",
                        unsigned(read_ok), out_changed, out_max, alpha_max, min_delta, darkened, add_err);
            report("lathe_flight_spread_drawn", read_ok && on_black.first == S_OK && over.first == S_OK && n.lathes == 1 &&
                                                    lathe_draws == n.patches && n.patches >= 1 && lit_px > 1000, "");
            report("lathe_bright_hull_unchanged_outside_the_cells", read_ok && out_changed == 0 && alpha_max == 0.0, "");
            report("lathe_bright_hull_adds_only", read_ok && darkened == 0 && add_err <= 2e-3, "");
            // The production resolve over the same scene, stage off and on.
            std::vector<float> resolved[2];
            bool taa_ok = read_ok;
            Com<IDirect3DTexture9> motion;
            Com<IDirect3DSurface9> motion_s;
            taa_ok = taa_ok && SUCCEEDED(dev->CreateTexture(g.w, g.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F,
                                                            D3DPOOL_DEFAULT, &motion.p, nullptr)) &&
                     SUCCEEDED(motion.p->GetSurfaceLevel(0, &motion_s.p));
            for (unsigned on = 0; on < 2 && taa_ok; ++on) {
                rr::TemporalPass taa;
                HRESULT hr = taa.initialize(dev, nullptr, reinterpret_cast<const DWORD*>(rr::temporal_resolve_program()),
                                            nullptr, nullptr, reinterpret_cast<const DWORD*>(rr::hdr_writeback_program()));
                if (SUCCEEDED(hr)) hr = taa.configure_far();
                rr::Output o{};
                for (unsigned fr = 0; fr < 8 && SUCCEEDED(hr); ++fr) {
                    draw_lathe_hull_colour(g, titan_flight_lane);
                    const float quad[4][4] = {{-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
                    dev->BeginScene();
                    dev->SetRenderTarget(0, motion_s.p);
                    dev->SetVertexDeclaration(quad_decl);
                    dev->SetVertexShader(quad_vs);
                    dev->SetPixelShader(motion_ps);
                    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
                    dev->SetRenderTarget(0, g.scene_s.p);
                    dev->EndScene();
                    StageContext ctx{&pass, f, S_FALSE};
                    rr::FrameInputs in;
                    in.color = g.scene_tex.p;
                    in.current_depth = g.lane_tex.p;
                    in.width = g.w;
                    in.height = g.h;
                    in.epoch = 1;
                    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
                    std::copy(identity, identity + 16, in.clip_to_previous);
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
                    if (SUCCEEDED(hr) && on && (!taa.diagnostics().stage_ran || ctx.result != S_OK)) hr = E_FAIL;
                    if (SUCCEEDED(hr) && fr == 7) {
                        Com<IDirect3DSurface9> out_s;
                        if (!o.color || FAILED(o.color->GetSurfaceLevel(0, &out_s.p))) hr = E_FAIL;
                        else resolved[on] = read_rgba(g, out_s.p);
                    }
                }
                taa.shutdown();
                taa_ok = SUCCEEDED(hr) && resolved[on].size() == px * 4;
                if (!taa_ok) std::printf("LATHE case=bright_hull_taa_fail on=%u result=%08lx\n", on, static_cast<unsigned long>(hr));
            }
            unsigned taa_out_changed = 0, taa_darkened = 0;
            double taa_out_max = 0, taa_min = 0, taa_gain_num = 0, taa_gain_den = 0;
            for (std::size_t i = 0; taa_ok && i < px; ++i) {
                bool changed = false;
                for (unsigned k = 0; k < 3; ++k) {
                    const double d = double(resolved[1][4 * i + k]) - double(resolved[0][4 * i + k]);
                    taa_min = std::min(taa_min, d);
                    taa_darkened += d < 0.0;
                    if (!around[i]) {
                        taa_out_max = std::max(taa_out_max, std::fabs(d));
                        changed = changed || d != 0.0;
                    } else if (lit[i]) {
                        taa_gain_num += d;
                        taa_gain_den += double(C[4 * i + k]);
                    }
                }
                taa_out_changed += changed;
            }
            std::printf("LATHE case=bright_hull_taa ok=%u frames=8 out_changed_px=%u out_max=%.6g min_delta=%.6g "
                        "darkened=%u cells_gain=%.4f\n",
                        unsigned(taa_ok), taa_out_changed, taa_out_max, taa_min, taa_darkened,
                        taa_gain_den > 0 ? taa_gain_num / taa_gain_den : 0.0);
            report("lathe_bright_hull_taa_unchanged_outside_and_never_darker",
                   taa_ok && taa_out_changed == 0 && taa_darkened == 0, "");
            if (read_ok) {
                std::vector<float> rgb(px * 3), delta(px * 4, 0.f);
                for (std::size_t i = 0; i < px; ++i) {
                    for (unsigned k = 0; k < 3; ++k) rgb[3 * i + k] = C[4 * i + k];
                    bool negative = false;
                    for (unsigned k = 0; k < 3; ++k) {
                        const float d = B[4 * i + k] - A[4 * i + k];
                        negative = negative || d < 0.f;
                        delta[4 * i + k] = 4.f * std::max(d, 0.f);
                    }
                    if (negative) delta[4 * i] = 1.f, delta[4 * i + 1] = delta[4 * i + 2] = 0.f; // any darker pixel red
                }
                dump_shield("titan_flight_lathe", rgb, depth, g.w, g.h);
                dump_plain("titan_flight_hull", A, g.w, g.h);
                dump_plain("titan_flight_hull_lathe", B, g.w, g.h);
                dump_plain("titan_flight_delta_x4", delta, g.w, g.h);
                if (taa_ok) dump_plain("titan_flight_hull_lathe_taa", resolved[1], g.w, g.h);
            }
            // The owner's worst case for the timing rows: 48 more live hits over the flank (64, core::owner_hits).
            for (unsigned k = 0; k < 48; ++k) {
                double p[3];
                profile_point(titan, int(2 + (k * 11) % 28), (45.0 + 90.0 * double((k * 13) % 48) / 47.0) * pi / 180.0, p);
                const std::int32_t at[3] = {std::int32_t(std::lround(p[0])), std::int32_t(std::lround(p[1])),
                                            std::int32_t(std::lround(p[2]))};
                const unsigned t0 = now - 50 - 50 * ((k * 5) % 48);
                add_stage_hit(t0, at, wc::patch_capital);
                max_t0 = std::max(max_t0, t0);
            }
            const ws::Counts n64 = ws::build(stage_ring, now, &lathe_read, identity_camera(), lathe_tuning(), titan_flight64,
                                             wc::max_patches, flight_decals, wc::max_decals, nullptr, &fixture_lathes);
            titan_flight64_count = n64.patches;
            unsigned cells64 = 0;
            for (unsigned i = 0; i < n64.patches; ++i) cells64 += titan_flight64[i].cells;
            std::printf("LATHE case=flight_64_hits draws=%u lathes=%u cells=%u cells_dropped=%u area_dropped=%u\n", n64.patches,
                        n64.lathes, cells64, n64.cells_dropped, n64.area_dropped);
            if (read_ok) { // its judging frames: on the fixture's dark-grey hull and on the bright hull
                draw_lathe_hull(g, titan_flight_lane);
                std::vector<float> rgb64;
                render(pass, titan_flight64, n64.patches, nullptr, &rgb64, nullptr, true);
                dump_shield("titan_flight_64_lathe", rgb64, depth, g.w, g.h);
                rr::WeaponEffectsFrame f64 = f;
                f64.patches = titan_flight64;
                f64.patch_count = n64.patches;
                draw_lathe_hull_colour(g, titan_flight_lane);
                draw_over(pass, f64);
                dump_plain("titan_flight_64_hull_lathe", read_rgba(g, g.scene_s.p), g.w, g.h);
            }
            // FL 69 A follow-up (coordinator, 2026-11-13): the flight's own hits at 5120x1440, exact zero tests. The 13
            // shield hits the FL 69 log has on the Titan (owner b1907de0) before frame 94270 (owner-frame engine
            // positions, ages from the newest), the Titan seen from its +y side 446 m off the hull. (1) every cell of
            // the drawn band unlit (the draws' cells not lit yet, the ring hits 10 s old) and the hits at the end of
            // their fade (2.93 s, alive): the count and maximum of non-zero pixels, exact; (2) the live hits: the
            // non-zero pixels outside the map's cells (all of a draw's cells lit at once) dilated by 3 px; (3) the
            // band per draw (rings r0..r1 of 40, columns c0..c1 of 24, the chart box) for one flank hit and the 13.
            {
                static const struct { unsigned age_ms; std::int32_t pos[3]; } flight_hits[13] = {
                    {2637, {24894, 30591, -363698}}, {2391, {26749, 25916, -356338}}, {2169, {26528, 26464, -355931}},
                    {1933, {26885, 25486, -356140}}, {1782, {17197, 30179, -359655}}, {1656, {24838, 29594, -357541}},
                    {1411, {20459, 29553, -360432}}, {1134, {26452, 27626, -354914}}, {1016, {26808, 31044, -357402}},
                    {765, {36130, 34936, -356687}},  {504, {23100, 28045, -359726}},  {243, {20985, 29277, -359390}},
                    {0, {14339, 28126, -356151}}};
                const unsigned fnow = 20000; // game ms: every flight t0 stays positive
                Target big;
                const bool big_ok = make_target(big, 5120, 1440);
                auto stage_flight = [&](unsigned shift_ms, unsigned count) {
                    const double ctr[3] = {double(flight_hits[12].pos[0]), double(flight_hits[12].pos[1]),
                                           double(flight_hits[12].pos[2])};
                    stage_titan(top_rows, ctr, wc::patch_capital, true);
                    // top_rows: local x -> view -y, y -> view -z, z -> view +x; the newest hit straight ahead, its
                    // lateral offset flank_m from the eye
                    put32(fake_node, wc::layout::node_pos + 0, std::uint32_t(-flight_hits[12].pos[2]));
                    put32(fake_node, wc::layout::node_pos + 4, std::uint32_t(flight_hits[12].pos[0]));
                    put32(fake_node, wc::layout::node_pos + 8, std::uint32_t(std::int32_t(std::lround(
                                                                    flank_m * metre / s + double(flight_hits[12].pos[1])))));
                    stage_ring.slots[0].t0_game_ms = fnow - shift_ms;
                    for (unsigned k = 13 - count; k < 12; ++k)
                        add_stage_hit(fnow - shift_ms - flight_hits[k].age_ms, flight_hits[k].pos, wc::patch_capital);
                };
                auto bands = [&](const char* name, const rr::WeaponPatch* p, unsigned count) {
                    for (unsigned i = 0; i < count; ++i)
                        std::printf("LATHE case=band set=%s draw=%u cells=%u rings=%u..%u columns=%u..%u of=%u,%u "
                                    "box_u=%.1f..%.1f box_v=%.1f..%.1f period=%.1f wrap_rows=%.0f (metres)\n",
                                    name, i, p[i].cells, unsigned(p[i].lathe_r0), unsigned(p[i].lathe_r1),
                                    unsigned(p[i].lathe_c0), unsigned(p[i].lathe_c1), unsigned(rr::weapon_lathe_rings),
                                    unsigned(rr::weapon_lathe_segments), p[i].lathe_box[0] / metre, p[i].lathe_box[2] / metre,
                                    p[i].lathe_box[1] / metre, p[i].lathe_box[3] / metre, p[i].lathe_period / metre,
                                    double(p[i].lathe_wrap_rows));
                };
                static rr::WeaponPatch one[wc::max_patches], live[wc::max_patches], faded[wc::max_patches],
                    unlit[wc::max_patches], all_lit[wc::max_patches];
                stage_flight(0, 1);
                const unsigned n_one = ws::build(stage_ring, fnow, &lathe_read, identity_camera(), lathe_tuning(), one,
                                                 wc::max_patches, flight_decals, wc::max_decals, nullptr,
                                                 &fixture_lathes).patches;
                bands("one_hit", one, n_one);
                stage_flight(0, 13);
                const LatheHullLane top_lane = lathe_hull_lane(titan);
                const unsigned n_live = ws::build(stage_ring, fnow, &lathe_read, identity_camera(), lathe_tuning(), live,
                                                  wc::max_patches, flight_decals, wc::max_decals, nullptr,
                                                  &fixture_lathes).patches;
                bands("flight_13", live, n_live);
                stage_flight(2930, 13); // every hit 2.93 s or older: alive (patch_lifetime_ms), every cell faded out
                const unsigned n_faded = ws::build(stage_ring, fnow, &lathe_read, identity_camera(), lathe_tuning(), faded,
                                                   wc::max_patches, flight_decals, wc::max_decals, nullptr,
                                                   &fixture_lathes).patches;
                for (unsigned i = 0; i < n_live; ++i) {
                    unlit[i] = live[i];
                    all_lit[i] = live[i];
                    for (unsigned c = 0; c < live[i].cells; ++c) {
                        unlit[i].cell_since[c] = -1.f;  // not lit yet
                        all_lit[i].cell_since[c] = 0.f; // lit this instant
                    }
                    for (unsigned h = 0; h < rr::weapon_patch_hits; ++h) unlit[i].age_s[h] = 10.f, all_lit[i].age_s[h] = 10.f;
                }
                Com<IDirect3DQuery9> occ;
                dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, &occ.p);
                // One frame on black over the top lane: its RGBA and the pixels the draws rasterized.
                auto frame = [&](const rr::WeaponPatch* p, unsigned count, DWORD* raster) {
                    rr::WeaponEffectsFrame bf = base_frame(big);
                    bf.patches = p;
                    bf.patch_count = count;
                    dev->BeginScene();
                    dev->SetRenderTarget(0, big.scene_s.p);
                    dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
                    if (occ.p) occ.p->Issue(D3DISSUE_BEGIN);
                    rr::WeaponEffectsReport r{};
                    pass.run(bf, &r);
                    if (occ.p) occ.p->Issue(D3DISSUE_END);
                    dev->EndScene();
                    *raster = 0;
                    const DWORD start = GetTickCount();
                    while (occ.p && occ.p->GetData(raster, sizeof *raster, D3DGETDATA_FLUSH) == S_FALSE &&
                           GetTickCount() - start < 5000) {
                    }
                    return read_rgba(big, big.scene_s.p);
                };
                auto nonzero = [](const std::vector<float>& v, std::size_t i) {
                    return v[4 * i] != 0.f || v[4 * i + 1] != 0.f || v[4 * i + 2] != 0.f || v[4 * i + 3] != 0.f;
                };
                bool exact_ok = big_ok && n_live >= 1;
                if (big_ok) {
                    draw_lathe_hull(big, top_lane);
                    const std::size_t bpx = std::size_t(big.w) * big.h;
                    DWORD r_unlit = 0, r_faded = 0, r_live = 0, r_all = 0;
                    const std::vector<float> v_unlit = frame(unlit, n_live, &r_unlit);
                    const std::vector<float> v_faded = frame(faded, n_faded, &r_faded);
                    const std::vector<float> v_all = frame(all_lit, n_live, &r_all);
                    const std::vector<float> v_live = frame(live, n_live, &r_live);
                    exact_ok = exact_ok && v_unlit.size() == bpx * 4 && v_faded.size() == bpx * 4 &&
                               v_all.size() == bpx * 4 && v_live.size() == bpx * 4;
                    unsigned nz_unlit = 0, nz_faded = 0, nz_out = 0, nz_live = 0, cells_px = 0;
                    double max_unlit = 0, max_faded = 0, max_out = 0;
                    std::vector<unsigned char> mask(bpx, 0);
                    for (std::size_t i = 0; exact_ok && i < bpx; ++i) {
                        for (unsigned k = 0; k < 4; ++k) {
                            max_unlit = std::max(max_unlit, double(std::fabs(v_unlit[4 * i + k])));
                            max_faded = std::max(max_faded, double(std::fabs(v_faded[4 * i + k])));
                        }
                        nz_unlit += nonzero(v_unlit, i);
                        nz_faded += nonzero(v_faded, i);
                        nz_live += nonzero(v_live, i);
                        if (!nonzero(v_all, i)) continue;
                        ++cells_px;
                        const int x = int(i % big.w), y = int(i / big.w);
                        for (int dy = -3; dy <= 3; ++dy)
                            for (int dx = -3; dx <= 3; ++dx)
                                if (x + dx >= 0 && y + dy >= 0 && x + dx < int(big.w) && y + dy < int(big.h))
                                    mask[std::size_t(y + dy) * big.w + std::size_t(x + dx)] = 1;
                    }
                    for (std::size_t i = 0; exact_ok && i < bpx; ++i) {
                        if (mask[i] || !nonzero(v_live, i)) continue;
                        ++nz_out;
                        for (unsigned k = 0; k < 4; ++k) max_out = std::max(max_out, double(std::fabs(v_live[4 * i + k])));
                    }
                    std::printf("LATHE case=exact_zero width=5120 height=1440 draws=%u faded_draws=%u band_px=%lu "
                                "unlit_nonzero_px=%u unlit_max=%.9g faded_band_px=%lu faded_nonzero_px=%u faded_max=%.9g "
                                "map_cells_px=%u live_nonzero_px=%u live_outside_cells3_px=%u live_outside_max=%.9g\n",
                                n_live, n_faded, static_cast<unsigned long>(r_unlit), nz_unlit, max_unlit,
                                static_cast<unsigned long>(r_faded), nz_faded, max_faded, cells_px, nz_live, nz_out, max_out);
                    exact_ok = exact_ok && r_unlit > 0 && nz_unlit == 0 && nz_faded == 0 && nz_out == 0 && nz_live > 0;
                    std::vector<float> live_rgb(bpx * 3);
                    std::vector<float> top_depth(bpx, 0.f);
                    const std::vector<float> d = read_lane_depth(big);
                    for (std::size_t i = 0; i < bpx && exact_ok; ++i)
                        for (unsigned k = 0; k < 3; ++k) live_rgb[3 * i + k] = v_live[4 * i + k];
                    if (exact_ok && d.size() == bpx) dump_shield("titan_flight13_top_5k", live_rgb, d, big.w, big.h);
                }
                report("lathe_unlit_band_exactly_zero_5k", exact_ok, "");
                big.release();
            }
            // The records past their life (the latest hit's t0 + patch_lifetime_ms + 1): no draw stays. The ring holds
            // the follow-up's flight hits now, staged at 20000 game ms and earlier.
            max_t0 = std::max(max_t0, 20000u);
            const ws::Counts gone = ws::build(stage_ring, max_t0 + wc::patch_lifetime_ms + 1, &lathe_read, identity_camera(),
                                              lathe_tuning(), titan_flight + n.patches, wc::max_patches - n.patches,
                                              flight_decals, wc::max_decals, nullptr, &fixture_lathes);
            std::printf("LATHE case=past_life patches=%u lathes=%u decals=%u\n", gone.patches, gone.lathes, gone.decals);
            report("lathe_records_past_life_draw_nothing", gone.patches == 0 && gone.lathes == 0 && gone.decals == 0, "");
        }

        // (f) FL 70 A (2026-11-13) on the installed trimmed profile (titan_trimmed_profile; its own cache, so the row-11
        // meshes the timing rows point into stay). (1) The slice arithmetic with z0 != -z1: every body ring's points at
        // 70 and -110 degrees map onto themselves. (2) The logged contacts (owner frame, engine units; dir the projectile's
        // travel x 127): a nose-on bolt 189 m behind z1 near the axis (frame 36024), a stern one (45993), a flank one
        // (28204) enter the lathe on their line of flight (lathe_entry: on the shape, off the line < 0.05 m), the nose
        // one ahead of the contact toward the bow and the stern one toward the stern, where Run70's map_lathe threw them
        // onto their slice's rim (the witness, nose and stern: > 50 m off the line); a mast contact past z1 (outside) and a contact
        // without travel keep map_lathe (the mast on the bow dome's tip); a reversed travel (out of the hull) walks
        // past the hull's half-width and falls back to map_lathe (LatheEntry::beyond, counted lathe_entry_far). (3) The stage (build, its lathe memo): the
        // nose hit's draw holds the entry's chart u and not the contact's. (4) The fade on the production clock path:
        // the game clock word (layout::cfg_game_ms) of a fake cfg block advanced by the engine's step each frame (4 ms;
        // SETA 40 ms), three hits 30 ms apart stamped from it like the hook's t0, build at that clock and Present's
        // release (core::release_due): the draws stop after the cells' own fades, before the records retire (a live
        // record whose cells have all faded draws nothing and counts no lathe), each record retires within one step
        // past patch_lifetime_ms, no drawn cell is past its own fade, and the law is Run239's (fade 2.2 s, ripple
        // 3.333 /s, stagger 0.4, life 2941 ms).
        {
            static ws::LatheCache trimmed_lathes;
            static ws::LatheMesh tm;
            const wc::ShipProfile trimmed = titan_trimmed_profile();
            const bool tbuilt = ws::build_lathe(trimmed, unit, &tm);
            constexpr unsigned first = ws::lathe_dome_rings, last = ws::lathe_dome_rings + ws::lathe_body_rings - 1;
            double arith_worst = 0.0;
            for (unsigned r = first; tbuilt && r <= last; ++r)
                for (double deg : {70.0, -110.0}) {
                    const float* e = tm.ring[r];
                    const float q[3] = {e[0] + e[2] * float(std::sin(deg * pi / 180.0)),
                                        e[1] + e[3] * float(std::cos(deg * pi / 180.0)), tm.ring_z[r]};
                    float b[3], chart[2];
                    ws::map_lathe(tm, q, b, chart);
                    const double d = std::sqrt(double(b[0] - q[0]) * (b[0] - q[0]) + double(b[1] - q[1]) * (b[1] - q[1]) +
                                               double(b[2] - q[2]) * (b[2] - q[2])) / metre;
                    arith_worst = std::max(arith_worst, d);
                }
            std::printf("LATHE case=trimmed_profile built=%u z_first_m=%.1f z_last_m=%.1f dome_m=%.1f,%.1f march_m=%.2f "
                        "steps=%u ring_self_map_worst_m=%.4f\n",
                        unsigned(tbuilt), tm.ring_z[first] / metre, tm.ring_z[last] / metre, tm.dome_depth[0] / metre,
                        tm.dome_depth[1] / metre, tm.march / metre, tm.march_steps, arith_worst);
            report("lathe_trimmed_slice_arithmetic", tbuilt && arith_worst < 0.01 && tm.march_steps > 0 &&
                                                         tm.march_steps <= ws::lathe_march_max, "");
            struct Logged {
                const char* name;
                std::int32_t pos[3];
                std::int8_t dir[3];
            };
            const Logged logged[] = {{"nose", {-22289, -53823, 406290}, {20, 50, -115}},
                                     {"stern", {-20434, -34971, -409062}, {108, -4, 67}},
                                     {"flank", {45676, -50114, 180953}, {-126, 14, 13}},
                                     {"mast", {0, -33000, 530000}, {0, 0, -127}},
                                     {"reversed", {-22289, -53823, 406290}, {-20, -50, 115}},
                                     {"no_travel", {-22289, -53823, 406290}, {0, 0, 0}}};
            auto off_line = [](const float p[3], const float d[3], const float q[3]) {
                const double v[3] = {double(q[0]) - p[0], double(q[1]) - p[1], double(q[2]) - p[2]};
                const double n = std::sqrt(double(d[0]) * d[0] + double(d[1]) * d[1] + double(d[2]) * d[2]);
                const double t = (v[0] * d[0] + v[1] * d[1] + v[2] * d[2]) / n;
                const double l2 = v[0] * v[0] + v[1] * v[1] + v[2] * v[2] - t * t;
                return std::sqrt(l2 > 0.0 ? l2 : 0.0);
            };
            bool entry_ok = tbuilt;
            float nose_entry[3] = {}, nose_contact[3] = {};
            for (const Logged& h : logged) {
                const float c[3] = {float(double(h.pos[0]) * s), float(double(h.pos[1]) * s), float(double(h.pos[2]) * s)};
                const float d[3] = {float(h.dir[0]), float(h.dir[1]), float(h.dir[2])};
                float entry[3], run70[3], fixed[3], chart[2];
                ws::map_lathe(tm, c, run70, chart);
                const ws::LatheEntry walked = ws::lathe_entry(tm, c, d, entry);
                const bool entered = walked == ws::LatheEntry::entered;
                ws::map_lathe(tm, entry, fixed, chart);
                const double moved = std::sqrt(double(fixed[0] - entry[0]) * (fixed[0] - entry[0]) +
                                               double(fixed[1] - entry[1]) * (fixed[1] - entry[1]) +
                                               double(fixed[2] - entry[2]) * (fixed[2] - entry[2])) / metre;
                const bool travel = (h.dir[0] | h.dir[1] | h.dir[2]) != 0;
                const double run70_off = travel ? off_line(c, d, run70) / metre : -1.0;
                const double fix_off = travel ? off_line(c, d, fixed) / metre : -1.0;
                const float shape = ws::lathe_shape(tm, entry);
                std::printf("LATHE case=entry hit=%s inside=%u entered=%u contact_m=%.1f,%.1f,%.1f run70_m=%.1f,%.1f,%.1f "
                            "fix_m=%.1f,%.1f,%.1f run70_off_line_m=%.1f fix_off_line_m=%.3f entry_shape=%.5f map_moves_m=%.3f\n",
                            h.name, unsigned(ws::lathe_shape(tm, c) < 0.f), unsigned(entered), c[0] / metre, c[1] / metre,
                            c[2] / metre, run70[0] / metre, run70[1] / metre, run70[2] / metre, fixed[0] / metre,
                            fixed[1] / metre, fixed[2] / metre, run70_off, fix_off, double(shape), moved);
                auto is = [&](const char* k) { return std::strcmp(h.name, k) == 0; };
                if (is("nose") || is("stern") || is("flank")) {
                    entry_ok = entry_ok && entered && std::fabs(shape) < 2e-3f && fix_off < 0.05 && moved < 0.5 &&
                               (is("flank") || run70_off > 50.0); // the flank contact lies near its rim already
                    if (is("nose")) {
                        entry_ok = entry_ok && fixed[2] > c[2] + 50.f * float(metre);
                        std::memcpy(nose_entry, entry, sizeof entry);
                        std::memcpy(nose_contact, c, sizeof c);
                    }
                    if (is("stern")) entry_ok = entry_ok && fixed[2] < c[2] - 50.f * float(metre);
                    if (is("flank")) entry_ok = entry_ok && fixed[0] > c[0] && std::fabs(fixed[2] - c[2]) < 30.f * float(metre);
                } else if (is("reversed")) { // a travel out of the hull: the walk crosses it, past the half-width
                    entry_ok = entry_ok && walked == ws::LatheEntry::beyond && entry[0] == c[0] && entry[1] == c[1] &&
                               entry[2] == c[2] && fixed[2] == run70[2];
                } else if (is("mast")) { // outside: the radial dome, near its tip
                    entry_ok = entry_ok && !entered && fixed[2] > tm.ring_z[last] + 0.9f * tm.dome_depth[1];
                } else { // no travel: map_lathe of the contact itself
                    entry_ok = entry_ok && !entered && entry[0] == c[0] && entry[1] == c[1] && entry[2] == c[2] &&
                               fixed[0] == run70[0] && fixed[1] == run70[1] && fixed[2] == run70[2];
                }
            }
            report("lathe_hits_enter_on_their_line_of_flight", entry_ok, "");
            // (3) the stage: the nose hit through build (its lathe memo), the draw's chart box about the entry's u
            const double nose_contact_engine[3] = {-22289.0, -53823.0, 406290.0};
            stage_titan(side_rows, nose_contact_engine, wc::patch_capital, true);
            lathe_profiles.entries[0] = trimmed;
            trimmed_lathes.clear();
            stage_ring.slots[0].dir[0] = 20;
            stage_ring.slots[0].dir[1] = 50;
            stage_ring.slots[0].dir[2] = -115;
            static rr::WeaponPatch nose_draws[wc::max_patches];
            static rr::WeaponDecal nose_decals[wc::max_decals];
            const ws::Counts nn = ws::build(stage_ring, 1500, &lathe_read, identity_camera(), lathe_tuning(), nose_draws,
                                            wc::max_patches, nose_decals, wc::max_decals, nullptr, &trimmed_lathes);
            const float box_u0 = nose_draws[0].lathe_box[0], box_u1 = nose_draws[0].lathe_box[2];
            const ws::Counts again = ws::build(stage_ring, 1500, &lathe_read, identity_camera(), lathe_tuning(), nose_draws,
                                               wc::max_patches, nose_decals, wc::max_decals, nullptr, &trimmed_lathes);
            float b[3], entry_chart[2], contact_chart[2];
            ws::map_lathe(tm, nose_entry, b, entry_chart);
            ws::map_lathe(tm, nose_contact, b, contact_chart);
            const bool holds = nn.patches >= 1 && nose_draws[0].lathe && box_u0 <= entry_chart[0] && entry_chart[0] <= box_u1;
            const bool misses = nn.patches >= 1 && (contact_chart[0] < box_u0 || contact_chart[0] > box_u1);
            const bool memo_same = again.patches == nn.patches && nose_draws[0].lathe_box[0] == box_u0 &&
                                   nose_draws[0].lathe_box[2] == box_u1;
            std::printf("LATHE case=entry_stage patches=%u lathes=%u again_patches=%u box_u_m=%.1f..%.1f entry_u_m=%.1f "
                        "contact_u_m=%.1f memo_same=%u\n",
                        nn.patches, nn.lathes, again.patches, box_u0 / metre, box_u1 / metre, entry_chart[0] / metre,
                        contact_chart[0] / metre, unsigned(memo_same));
            // the same record with its travel reversed (out of the hull): the entry guard's fallback, counted once
            stage_ring.slots[0].dir[0] = -20;
            stage_ring.slots[0].dir[1] = -50;
            stage_ring.slots[0].dir[2] = 115;
            const ws::Counts rev = ws::build(stage_ring, 1500, &lathe_read, identity_camera(), lathe_tuning(), nose_draws,
                                             wc::max_patches, nose_decals, wc::max_decals, nullptr, &trimmed_lathes);
            const bool rev_holds = rev.patches >= 1 && nose_draws[0].lathe_box[0] <= contact_chart[0] &&
                                   contact_chart[0] <= nose_draws[0].lathe_box[2];
            const ws::Counts rev2 = ws::build(stage_ring, 1500, &lathe_read, identity_camera(), lathe_tuning(), nose_draws,
                                              wc::max_patches, nose_decals, wc::max_decals, nullptr, &trimmed_lathes);
            std::printf("LATHE case=entry_far patches=%u lathe_entry_far=%u again_far=%u holds_contact_u=%u\n", rev.patches,
                        rev.lathe_entry_far, rev2.lathe_entry_far, unsigned(rev_holds));
            report("lathe_stage_draws_the_entry", holds && misses && nn.lathes == 1 && memo_same && nn.lathe_entry_far == 0 &&
                                                      rev.lathe_entry_far == 1 && rev2.lathe_entry_far == 0 && rev_holds, "");
            // (4) the fade on the production clock path
            alignas(16) static unsigned char fake_cfg[0x800];
            auto clock_word = [&]() {
                std::uint32_t v = 0;
                std::memcpy(&v, fake_cfg + wc::layout::cfg_game_ms, 4);
                return v;
            };
            const ws::Tuning law = lathe_tuning();
            const bool run239_law = law.fade_s == 2.2f && law.ripple_speed == 3.333f && law.stagger == 0.4f &&
                                    wc::patch_lifetime_ms == 2941u;
            struct Run {
                double last_draw_s = -1.0, first_dark_s = -1.0;
                unsigned frames = 0, drawn_frames = 0, dark_live_frames = 0, released = 0, stray_lathes = 0;
                unsigned past_own_fade = 0, cells_max = 0;
                std::uint32_t release_age_min = 0xffffffffu, release_age_max = 0;
            };
            auto fly = [&](std::uint32_t step_ms) {
                Run run;
                std::uint32_t clock = 1267531u; // the FL 70 A log's first hit t0
                std::memcpy(fake_cfg + wc::layout::cfg_game_ms, &clock, 4);
                const double flank[3] = {45676.0, -50114.0, 180953.0};
                stage_titan(side_rows, flank, wc::patch_capital, true);
                lathe_profiles.entries[0] = trimmed;
                stage_ring.slots[0].dir[0] = -126;
                stage_ring.slots[0].dir[1] = 14;
                stage_ring.slots[0].dir[2] = 13;
                stage_ring.slots[0].t0_game_ms = clock_word(); // the hook's t0: cfg+0x718 (FL +0x708) at the hit
                const std::uint32_t first_t0 = clock_word();
                std::uint32_t newest = first_t0;
                unsigned added = 0;
                static rr::WeaponPatch draws[wc::max_patches];
                static rr::WeaponDecal decals[wc::max_decals];
                for (unsigned f = 1; f < 4000 && ring_live(stage_ring); ++f) {
                    clock += step_ms; // the engine's game-time step (SETA scales it)
                    std::memcpy(fake_cfg + wc::layout::cfg_game_ms, &clock, 4);
                    const std::uint32_t now = clock_word();
                    if (added < 2 && now - first_t0 >= 30u * (added + 1)) { // two more hits, 30 ms apart
                        ++added;
                        const std::int32_t at[3] = {45676 + 600 * std::int32_t(added), -50114,
                                                    180953 + 900 * std::int32_t(added)};
                        add_stage_hit(now, at, wc::patch_capital);
                        newest = now;
                    }
                    const ws::Counts n = ws::build(stage_ring, now, &lathe_read, identity_camera(), law, draws,
                                                   wc::max_patches, decals, wc::max_decals, nullptr, &trimmed_lathes);
                    ++run.frames;
                    const double since_newest = double(now - newest) * 1e-3;
                    if (n.patches) {
                        ++run.drawn_frames;
                        run.last_draw_s = since_newest;
                        for (unsigned d = 0; d < n.patches; ++d) {
                            const rr::WeaponPatch& p = draws[d];
                            const int rows = int(p.lathe_wrap_rows + 0.5f);
                            run.cells_max = std::max(run.cells_max, unsigned(p.cells));
                            for (unsigned c = 0; c < p.cells; ++c) {
                                const std::uint32_t packed = std::uint32_t(std::int32_t(p.cell_key[c]));
                                const int kx = int(packed >> 12) - rr::weapon_cell_key_bias;
                                int ky = int(packed & 0xfffu) - rr::weapon_cell_key_bias;
                                if (rows > 0) ky -= rows * int(std::floor(double(ky) / double(rows)));
                                if (p.cell_since[c] > ws::cell_fade_s(kx, ky, p.seed, law.fade_s, law.stagger) + 1e-4f)
                                    ++run.past_own_fade;
                            }
                        }
                    } else if (ring_live(stage_ring)) {
                        if (run.first_dark_s < 0.0) run.first_dark_s = since_newest;
                        ++run.dark_live_frames;
                        run.stray_lathes += n.lathes;
                    }
                    for (unsigned i = 0; i < wc::ring_capacity; ++i) { // Present: the release (weapon_effects_patch.cpp)
                        const wc::Event& e = stage_ring.slots[i];
                        if (!e.sequence || !wc::release_due(e, now, 0, 0)) continue;
                        const std::uint32_t age = now - e.t0_game_ms;
                        run.release_age_min = std::min(run.release_age_min, age);
                        run.release_age_max = std::max(run.release_age_max, age);
                        ++run.released;
                        wc::ring_release(stage_ring, i);
                    }
                }
                return run;
            };
            const Run normal = fly(4), seta = fly(40);
            for (const Run* r : {&normal, &seta})
                std::printf("LATHE case=fade_clock step_ms=%u frames=%u drawn_frames=%u last_draw_s=%.3f first_dark_s=%.3f "
                            "dark_live_frames=%u stray_lathes=%u released=%u release_age_ms=%lu..%lu life_ms=%u "
                            "past_own_fade_cells=%u cells_max=%u run239_law=%u\n",
                            r == &normal ? 4u : 40u, r->frames, r->drawn_frames, r->last_draw_s, r->first_dark_s,
                            r->dark_live_frames, r->stray_lathes, r->released,
                            static_cast<unsigned long>(r->release_age_min), static_cast<unsigned long>(r->release_age_max),
                            wc::patch_lifetime_ms, r->past_own_fade, r->cells_max, unsigned(run239_law));
            auto fade_ok = [&](const Run& r, std::uint32_t step) {
                const double life = double(wc::patch_lifetime_ms) * 1e-3;
                return r.drawn_frames > 0 && r.released == 3 && r.release_age_min > wc::patch_lifetime_ms &&
                       r.release_age_max <= wc::patch_lifetime_ms + step && r.last_draw_s >= 0.0 &&
                       r.last_draw_s < life && r.first_dark_s > r.last_draw_s && r.dark_live_frames > 0 &&
                       r.stray_lathes == 0 && r.past_own_fade == 0;
            };
            report("lathe_cells_fade_on_the_game_clock", run239_law && fade_ok(normal, 4) && fade_ok(seta, 40) &&
                                                             std::fabs(normal.last_draw_s - seta.last_draw_s) <= 0.08, "");
        }
        // (e) the timing rows' Titan: two hits on the flank (slice 9 at 90 degrees, slice 12 at 70), the decal and the lathe
        double second[3];
        profile_point(titan, 12, 70.0 * pi / 180.0, second);
        for (unsigned k = 0; k < 2; ++k) {
            stage_titan(side_rows, side_contact, wc::patch_capital, k == 1);
            const std::int32_t at[3] = {std::int32_t(std::lround(second[0])), std::int32_t(std::lround(second[1])),
                                        std::int32_t(std::lround(second[2]))};
            add_stage_hit(1200, at, wc::patch_capital);
            if (k == 0) // the hull decal (a station's hits; a ship without a profile draws nothing)
                for (auto& e : stage_ring.slots) e.tier_source = wc::tier_by_station;
            titan_timing_count[k] = build_titan(titan_timing[k], lathe_tuning()).patches;
        }
        titan_timing_lane = lane_side;
        std::printf("LATHE case=timing_draws decal_draws=%u lathe_draws=%u\n", titan_timing_count[0], titan_timing_count[1]);
        report("lathe_timing_draws_built", titan_timing_count[0] >= 1 && titan_timing_count[1] >= 1 &&
                                               titan_timing[1][0].lathe && !titan_timing[0][0].lathe, "");
        stage_tier_source = wc::tier_by_station;
        draw_lane(g, 0.f, -1.f);
    }
    // The stage's CPU cost in this i686 build (SSE2, the production flags): ws::build alone with the depth cache (the
    // production path), best and mean of 200 calls,
    // on the worst cases: 64 live hits on one capital owner spread over its front (the region spills), and 16 capital
    // owners with 16 spread hits each (the ring's 256), against the 0.2 ms stage_us target (which also holds the draws);
    // and one capital owner under 12 hits/s, 2.5 s in (31 live hits). The rows without a suffix draw the hull decal (the
    // hits a station's: since the walk's removal, 2026-11-12, a ship without a profile draws nothing), the _lathe rows
    // the same owners as ships with the Titan's profile.
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
            // a TShips row-11 ship with unit axis scales (read only by the lathe cases)
            const std::uint16_t cls = wc::layout::class_ship, sub = std::uint16_t(titan_row);
            std::memcpy(o.obj + wc::layout::obj_class, &cls, 2);
            std::memcpy(o.obj + wc::layout::obj_subtype, &sub, 2);
            for (unsigned i = 0; i < 3; ++i) put32(o.node, wc::layout::node_axis_scale + 4 * i, 0x10000u);
        };
        auto add_hit = [&](unsigned k, unsigned i, std::uint32_t t0, float radius, const std::int32_t* at = nullptr) {
            wc::Event* e = wc::ring_acquire(ring);
            if (!e) return;
            const double r_engine = double(radius) / double(wc::world_scale);
            const double th = 0.61 * std::sqrt(halton(i + 1, 2)), a = 6.2831853 * halton(i + 1, 3);
            e->role = wc::role_shield_hit;
            e->style = wc::shape_slug;
            e->flags = std::uint8_t(wc::flag_stage | wc::flag_forwarded | wc::flag_shield_read);
            e->shield_q = 0xffff;
            e->patch_tier = wc::patch_capital;
            e->tier_source = wc::tier_by_ship_class;
            e->dir[0] = e->dir[1] = e->dir[2] = 0;
            e->t0_game_ms = t0;
            e->lifetime_ms = std::uint16_t(wc::patch_lifetime_ms);
            e->owner = addr(owners[k].obj);
            e->node = addr(owners[k].node);
            e->handle = 0xe000u + k;
            e->pos[0] = std::int32_t(r_engine * std::sin(th) * std::cos(a));
            e->pos[1] = std::int32_t(r_engine * std::sin(th) * std::sin(a));
            e->pos[2] = -std::int32_t(r_engine * std::cos(th));
            if (at)
                for (unsigned c = 0; c < 3; ++c) e->pos[c] = at[c];
            e->radius = std::int32_t(engine(radius));
        };
        rr::WeaponPatch patches[wc::max_patches];
        rr::WeaponDecal decals[wc::max_decals];
        LARGE_INTEGER freq, a, b;
        QueryPerformanceFrequency(&freq);
        // The lathe cases (2026-11-12): every owner a TShips row-11 ship with the Titan's profile, its mesh cached (the
        // production steady state; the first call builds it); the reader also covers the fake type table.
        auto lathe_reader = [](std::uintptr_t a, void* out, std::size_t n) {
            const std::uintptr_t b = reinterpret_cast<std::uintptr_t>(&owners[0]), size = sizeof owners;
            if (a >= b && n <= size && a - b <= size - n) {
                std::memcpy(out, reinterpret_cast<const void*>(a), n);
                return true;
            }
            return lathe_read(a, out, n);
        };
        static ws::LatheCache cpu_lathes;
        auto time_build = [&](const char* name, std::uint32_t now, bool lathe = false) {
            double best = 1e30, sum = 0;
            ws::Counts n{};
            ws::Tuning tune{};
            if (lathe) {
                stage_lathe_type();
                tune.profiles = &lathe_profiles;
                tune.ship_table_slot = addr(fake_types) + type_slots_at;
                tune.ship_count_slot = addr(fake_types) + type_slots_at + 4;
            }
            for (auto& e : ring.slots) e.tier_source = lathe ? wc::tier_by_ship_class : wc::tier_by_station;
            for (unsigned rep = 0; rep < 200; ++rep) {
                QueryPerformanceCounter(&a);
                n = lathe ? ws::build(ring, now, lathe_reader, identity_camera(), tune, patches, wc::max_patches, decals,
                                      wc::max_decals, nullptr, &cpu_lathes)
                          : ws::build(ring, now, reader, identity_camera(), tune, patches, wc::max_patches, decals,
                                      wc::max_decals);
                QueryPerformanceCounter(&b);
                const double us = ticks_us(a, b, freq);
                best = std::min(best, us);
                sum += us;
            }
            unsigned cells = 0;
            for (unsigned i = 0; i < n.patches; ++i) cells += patches[i].cells;
            std::printf("STAGE_CPU case=%s owners=%u hits=%u patches=%u cells=%u spills=%u cells_dropped=%u dropped=%u lathes=%u no_profile=%u best_us=%.1f mean_us=%.1f target_us=200\n",
                        name, n.owners, n.hits, n.patches, cells, n.spills, n.cells_dropped, n.dropped, n.lathes,
                        n.lathe_no_profile, best, sum / 200.0);
            return lathe ? (n.lathes == n.owners ? sum / 200.0 : -1.0) : (n.patches ? sum / 200.0 : -1.0);
        };
        wc::ring_clear(ring);
        make_owner(0, 0.f, 4000.f, 2000.f);
        for (unsigned i = 0; i < wc::owner_hits; ++i) add_hit(0, i, 1000 + 40 * i, 2000.f); // 25 hits/s over 2.5 s
        const double one = time_build("one_capital_64_hits", 1000 + 40 * wc::owner_hits);
        const double one_lathe = time_build("one_capital_64_hits_lathe", 1000 + 40 * wc::owner_hits, true);
        wc::ring_clear(ring);
        for (unsigned k = 0; k < wc::max_owners; ++k) {
            make_owner(k, (float(k) - 7.5f) * 5000.f, 20000.f, 2000.f);
            for (unsigned i = 0; i < 16; ++i) add_hit(k, 16 * k + i, 1000 + 150 * i, 2000.f);
        }
        const double many = time_build("sixteen_capitals_16_hits", 1000 + 150 * 16);
        const double many_lathe = time_build("sixteen_capitals_16_hits_lathe", 1000 + 150 * 16, true);
        wc::ring_clear(ring);
        make_owner(0, 0.f, 4000.f, 2000.f);
        for (unsigned i = 0; i * 1000u / 12u <= 2500u; ++i) add_hit(0, i, 1000 + i * 1000u / 12u, 2000.f); // 12 hits/s
        const double at12 = time_build("one_capital_12hps", 3500);
        const double at12_lathe = time_build("one_capital_12hps_lathe", 3500, true);
        // The lathe (one chart region per owner) against the hull decal (six face regions) on the same hits: means (us).
        report("stage_cpu_lathe_not_above_decal", one_lathe > 0.0 && many_lathe > 0.0 && at12_lathe > 0.0 &&
                                                      many_lathe <= many && one_lathe <= one * 1.1 && at12_lathe <= at12 * 1.1,
               "");
        // The Flight B station (R 1304 m): 64 hits over its recorded contact cluster (x -131k..-110k, y 75k..126k, z
        // 353k..421k engine units: one +z region, |pos| / R 0.55 to 0.68), 25 hits/s.
        wc::ring_clear(ring);
        make_owner(0, 0.f, 20000.f, 6517.64f);
        for (unsigned i = 0; i < wc::owner_hits; ++i) {
            const std::int32_t at[3] = {std::int32_t(-131000 + 21000 * halton(i + 1, 2)),
                                        std::int32_t(75000 + 51000 * halton(i + 1, 3)),
                                        std::int32_t(353000 + 68000 * halton(i + 1, 5))};
            add_hit(0, i, 1000 + 40 * i, 6517.64f, at);
        }
        const double station = time_build("station_flight_b_cluster_64_hits", 1000 + 40 * wc::owner_hits);
        report("stage_cpu_measured", one > 0.0 && many > 0.0 && station > 0.0 && at12 > 0.0, "");
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
    // until the builder has nothing to draw: the last lit cell's game ms end after a sixth of them in wall time. The
    // last lit game ms is the builder's own (its cells' own fades since FL 70 A: game_end, found at 1 ms steps), within
    // the fastest and slowest cell's bounds.
    {
        stage_owner(400.f, 100.f, 0);
        std::uint32_t game_end = 0;
        {
            rr::WeaponPatch probe[wc::max_patches];
            rr::WeaponDecal probe_decals[wc::max_decals];
            for (std::uint32_t gm = 0; gm <= wc::patch_lifetime_ms + 1; ++gm)
                if (ws::build(stage_ring, gm, &fake_read, identity_camera(), ws::Tuning{}, probe, wc::max_patches,
                              probe_decals, wc::max_decals).patches)
                    game_end = gm;
        }
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
        const double lit_fast_ms = double(wc::fade_ms) * (1.0 - 0.5 * double(wc::stagger));
        const double expected = double(game_end + 1) / 6.0;
        std::printf("AGE lifetime_ms=%u lit_ms=%.1f lit_fast_ms=%.1f game_end_ms=%lu seta6_expected_wall_ms=%.2f "
                    "seta6_wall_ms=%.2f last_drawn_ms=%.2f frames=%u max_frame_ms=%.3f\n",
                    wc::patch_lifetime_ms, lit_ms, lit_fast_ms, static_cast<unsigned long>(game_end), expected, wall_ms,
                    last_drawn_ms, frames, max_frame_ms);
        report("age_law_seta6_measured", frames > 0 && last_drawn_ms <= expected + 0.2 && wall_ms >= expected &&
                                             wall_ms <= expected + 0.2 + max_frame_ms + 0.5 &&
                                             double(game_end) >= lit_fast_ms && double(game_end) <= lit_ms &&
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
        const double sphere[3] = {100.0, 100.0, 100.0}; // the hull through the hit: the hull decal
        draw_hull_lane(g, staged_hull(sphere));
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
        report("stage_build_live_owner_draws", live.patches >= 1 && live.lathes == 0 && drawn.second.drew && live_sum > 1.0, "");
        draw_lane(g, 0.f, -1.f);
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
    guard_next_section("step1");
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
    // Fault once, then recover; then Reset; a hull decal draw and a bubble's.
    {
        rr::WeaponEffectsFrame f = base_frame(g);
        rr::WeaponPatch both[9];
        both[0] = make_patch(fighter, {{0.f, 0.f, 0.f}});
        const rr::WeaponPatch& p = both[0];
        const unsigned drawn_both = 1 + bubble_patches(fighter, {{0.f, 0.f, 0.f}}, both + 1, 8);
        f.patches = both;
        f.patch_count = drawn_both;
        pass.set_faults(2);
        auto res = draw(pass, f);
        report("fault_reported", FAILED(res.first) && res.second.failed != rr::WeaponEffectsStep::None, "");
        pass.set_faults(0);
        auto res2 = draw(pass, f);
        report("recovers_after_fault", SUCCEEDED(res2.first) && res2.second.drew && res2.second.patches == drawn_both &&
                                           drawn_both >= 2, "");
        pass.before_reset();
        g.release();                       // only DEFAULT-pool targets are lost across Reset; shaders survive
        D3DPRESENT_PARAMETERS pp2 = pp;
        device_guard("before_reset"); // a loss before the Reset is seen, not reset away
        const HRESULT reset = dev->Reset(&pp2);
        pass.after_reset(reset);
        std::printf("RESET result=%08lx\n", reset);
        const bool mt = SUCCEEDED(reset) && make_target(g, 1920, 1080);
        std::printf("RESET make_target=%u\n", unsigned(mt));
        const HRESULT er = pass.ensure_resources();
        std::printf("RESET ensure=%08lx\n", er);
        bool drew_after = false;
        if (mt && SUCCEEDED(er)) {
            plane_lane(g, p);
            rr::WeaponEffectsFrame f2 = base_frame(g);
            f2.patches = both;
            f2.patch_count = drawn_both;
            auto after = draw(pass, f2);
            drew_after = SUCCEEDED(after.first) && after.second.drew && after.second.patches == drawn_both &&
                         total(read_luma(g, g.scene_s.p)) > 1.0;
        }
        report("reset_recovers", mt && SUCCEEDED(er) && pass.resources_ready() && drew_after, "");
        draw_lane(g, 0.f, -1.f);
    }
    // Timing, EVENT-fenced (best of 60), over a sky lane: an empty fence (the fence's own round trip); two hits on a
    // fighter (a 20 m owner at 100 m) two cells apart, once in one anchor region (one draw, one cell map) and once as two
    // regions (two draws); the same on a capital at close range (a 400 m owner, the hits 100 m from the camera, three
    // cells apart); the worst one owner can submit at once (that capital hit in four regions: four draws); a fighter
    // under 12 hits per second (36 live hits within two cells: the steady-state map), a capital map of all 128 cells
    // (16 disjoint footprints, the newest 128 cells), a 128-cell map of overlapping footprints 30 m from the eye so its
    // cells fill the frame, and the capital spread at 12 hits/s through the builder (its spill draws), at 1080p and
    // 5120x1440.
    // Each row carries lit_share (pixels the patches lit) and raster_share (pixels the draws rasterized, summed over
    // the draws: an occlusion query, Z off, so every pixel the cells cover counts).
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
        // The fill case: 16 overlapping footprints 2.5 cells apart, the central ones newest (the 128 newest cells a
        // contiguous patch about the axis), the plane 30 m from the eye so the cells fill the frame.
        const TierScene capital_fill{wc::patch_capital, 2000.f, 2150.f};
        Hit wide_fill[16];
        for (unsigned k = 0; k < 16; ++k) {
            const float x = (float(k % 4) - 1.5f) * 2.5f * cc, y = (float(k / 4) - 1.5f) * 2.5f * 0.8660254f * cc;
            wide_fill[k] = Hit{x, y, 0.3f + 0.1f * std::sqrt(x * x + y * y) / cc}; // a ring cell of a nearer hit is newer
        }
        std::sort(wide_fill, wide_fill + 16, [](const Hit& a, const Hit& b) { return a.age < b.age; });
        const rr::WeaponPatch capital_fill_map[1] = {make_patch(capital_fill, wide_fill, 16)};
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
        const double spread_r[3] = {2000.0, 2000.0, 2000.0};
        const HullLane spread_hull = staged_hull(spread_r); // the capital's hull through its hits (the hull decal)
        // The ship bubble's cases (5.1.1 "Placement"): the same hits on a facing owner's bubble (a sphere of the scene's
        // surface radius, the hull inside it at 0.87 of it as the lane), through the production fill_bubble; the
        // capital spread above is the hull decal through the builder (FL 67 A: a capital takes the decal). The
        // bubble_capital cases keep the capital scene's size as the bubble program's large-owner cost bound.
        rr::WeaponPatch bubble_fighter[8], bubble_capital[8], bubble_worst[32];
        const unsigned n_bf = bubble_patches(fighter_far, {{-cf, 0.f, 0.2f}, {cf, 0.f, 0.5f}}, bubble_fighter, 8);
        const unsigned n_bc = bubble_patches(capital_close, {{-1.5f * cc, 0.f, 0.2f}, {1.5f * cc, 0.f, 0.5f}}, bubble_capital, 8);
        unsigned n_bw = 0;
        for (unsigned k = 0; k < 4; ++k)
            n_bw += bubble_patches(capital_close, {{(k & 1 ? 1.5f : -1.5f) * cc, (k & 2 ? 1.5f : -1.5f) * cc, 0.1f + 0.1f * k}},
                                   bubble_worst + n_bw, 32 - n_bw);
        const HullLane fighter_hull = facing_hull(fighter_far, 0.87), capital_hull = facing_hull(capital_close, 0.87);
        // The eye inside the bubble (review F6), through the production fill_bubble on the sky lane (the eye sphere's draw):
        // a sphere R 20 m with the eye 10 m behind its centre, the hit on its front pole, and the own ship in the cockpit
        // view (the eye 1 m inside the sphere's back, eight hits around it, two behind the eye; the draws dimmed as the
        // glue dims them).
        rr::WeaponPatch inside_front[wc::max_patches], cockpit[wc::max_patches];
        unsigned n_inside = 0, n_cockpit = 0;
        {
            auto sphere_draws = [&](float z, const std::int32_t (*at)[3], const float* ages, unsigned count,
                                    rr::WeaponPatch* out) {
                const float centre[3] = {0.f, 0.f, z}, frame[9] = {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f};
                ws::Bubble sphere;
                for (unsigned k = 0; k < 3; ++k) sphere.e[k] = 100.f;
                float hits[8][3], colour[3];
                for (unsigned h = 0; h < count; ++h) ws::hit_local(at[h], hits[h]);
                stage_owner(z, 100.f, 1000);
                ws::patch_colour(stage_ring.slots[0], ws::Tuning{}, colour);
                ws::Counts n{};
                unsigned left = ws::anchor_count;
                ws::fill_bubble(out, wc::max_patches, &left, centre, frame, wc::patch_fighter, sphere, hits, ages, count,
                                colour, ws::Tuning{}, &n);
                for (unsigned d = 0; d < n.patches; ++d) {
                    out[d].owner = addr(fake_obj);
                    out[d].owner_node = addr(fake_node);
                    out[d].owner_radius = 100.f;
                    out[d].newest_s = ages[0];
                }
                return n.patches;
            };
            const std::int32_t front[1][3] = {{0, 0, 10000}};
            const float front_age[1] = {0.5f};
            n_inside = sphere_draws(50.f, front, front_age, 1, inside_front);
            const std::int32_t around[8][3] = {{7071, 0, 7071}, {-7071, 0, 7071}, {0, 7071, 7071}, {0, 0, 10000},
                                               {10000, 0, 0}, {0, -10000, 0}, {0, 2000, -9798}, {3000, 0, -9539}};
            const float around_age[8] = {0.04f, 0.08f, 0.12f, 0.16f, 0.2f, 0.24f, 0.28f, 0.32f}; // newest first
            n_cockpit = sphere_draws(95.f, around, around_age, 8, cockpit);
            ws::dim_own_cockpit(cockpit, n_cockpit, addr(fake_node), true);
            wc::ring_clear(stage_ring);
        }
        unsigned spread_cells = 0;
        for (unsigned i = 0; i < spread_count; ++i) spread_cells += spread[i].cells;
        std::printf("TIMING_MAPS fighter_12hps_cells=%u capital_full_map_cells=%u capital_fill_map_cells=%u capital_spread_draws=%u capital_spread_cells=%u\n",
                    fighter_burst[0].cells, capital_map[0].cells, capital_fill_map[0].cells, spread_count, spread_cells);
        struct Case {
            const char* name;
            const rr::WeaponPatch* patches;
            unsigned count;
            const HullLane* hull; // the surface (null: the first patch's plane)
            bool sky = false;     // the sky lane instead
            const LatheHullLane* lathe_lane = nullptr; // the Titan's hull lane instead
        } cases[] = {{"fence", nullptr, 0, nullptr},
                     // the lathe bubble (2026-11-12): the Titan's two flank hits as the hull decal (a station's) and on
                     // its lathe, on the Titan's hull lane, 12 km from the eye (the whole ship on screen)
                     {"titan_two_hits_decal", titan_timing[0], titan_timing_count[0], nullptr, false, &titan_timing_lane},
                     {"titan_two_hits_lathe", titan_timing[1], titan_timing_count[1], nullptr, false, &titan_timing_lane},
                     // FL 69 A: the flank 446 m from the eye under 16 spread live hits (the hull past the screen)
                     {"titan_flight_spread_lathe", titan_flight, titan_flight_count, nullptr, false, &titan_flight_lane},
                     {"titan_flight_64_hits_lathe", titan_flight64, titan_flight64_count, nullptr, false, &titan_flight_lane},
                     {"fighter_two_hits_one_region", fighter_one, 1, nullptr},
                     {"fighter_two_regions", fighter_regions, 2, nullptr},
                     {"capital_close_two_hits_one_region", capital_one, 1, nullptr},
                     {"capital_close_two_regions", capital_regions, 2, nullptr}, {"worst_four_regions", worst, 4, nullptr},
                     {"fighter_12hps_map", fighter_burst, 1, nullptr}, {"capital_close_full_map", capital_map, 1, nullptr},
                     {"capital_fill_full_map", capital_fill_map, 1, nullptr},
                     {"capital_spread_spill", spread, spread_count, &spread_hull},
                     {"bubble_fighter_two_hits", bubble_fighter, n_bf, &fighter_hull},
                     {"bubble_capital_close_two_hits", bubble_capital, n_bc, &capital_hull},
                     {"bubble_worst_one_owner_four_hits", bubble_worst, n_bw, &capital_hull},
                     {"bubble_eye_inside_front_hit", inside_front, n_inside, nullptr, true},
                     {"bubble_cockpit_eight_hits", cockpit, n_cockpit, nullptr, true}};
        Com<IDirect3DQuery9> ev, occlusion;
        dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev.p);
        dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, &occlusion.p);
        LARGE_INTEGER freq, a, b;
        QueryPerformanceFrequency(&freq);
        for (const Case& c : cases) {
            rr::WeaponEffectsFrame f = base_frame(tt);
            f.patches = c.patches;
            f.patch_count = c.count;
            if (c.lathe_lane)
                draw_lathe_hull(tt, *c.lathe_lane);
            else if (c.sky)
                draw_lane(tt, 0.f, -1.f);
            else if (c.hull)
                draw_hull_lane(tt, *c.hull);
            else if (c.count)
                plane_lane(tt, c.patches[0]);
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
            device_guard("timing");
            std::printf("TIMING width=%u height=%u case=%s patches=%u drawn=%u lit_share=%.4f raster_share=%.4f gpu_us=%.1f\n",
                        size.first, size.second, c.name, c.count, drawn,
                        l.empty() ? 0.0 : double(lit_count(l)) / double(l.size()),
                        double(rasterized) / (double(size.first) * double(size.second)), best);
        }
        tt.release();
    }

    pass.detach();
    guard_next_section("reshade");
    reshade_cases();
    reshade_texture_cases();
    guard_next_section("reshade_lone");
    lone_reshade_cases();
    guard_next_section("reshade_lone_classic");
    lone_classic_cases();
    guard_next_section("reshade_lone_classic_ap");
    lone_classic_ap_case();
    guard_next_section("muzzle_reshade");
    muzzle_reshade_cases();
    guard_next_section("end");
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
