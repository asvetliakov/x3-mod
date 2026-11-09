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
// fixed on a moved and rotated owner, the placement on hull lanes (a narrow 1:1:6 hull's flank, aft flank, top and
// bottom, a flat 6:1:4 fighter's flank, top, bottom and top from 42 degrees down, the Flight B station at 0.68 R, the
// far side hidden by its own hull, the eye inside the bounding sphere) with the normal's error (ddx / ddy against a
// 4-neighbourhood); since the ship bubble (5.1.1 "Placement", user decision after Run238 Flight E) the ships' cases
// draw on their bubble from the production hull walk over a fake body cache (stage_hull) and are measured against the
// bubble's CPU twin (the hit's bubble point B, the near side; the chase camera's nose hit on the far side around the
// outline with the hull hiding the rest; the eye inside the bubble drawing its far side only), the station keeps the
// hull decal; 12 hits per second without a pop on a fighter's and a capital's flat face (the capital's cells
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
#include "../../src/proxy/weapon_projectile_stage.h"
#include "../../src/proxy/engine_memory.h"
#include "../../src/renderer/temporal_pass.h"
#include "../../src/renderer/temporal_resolve_program.h"
#include "../../src/renderer/hdr_writeback_program.h"
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
alignas(16) unsigned char fake_obj[0x100], fake_node[0x200], fake_hull[0x14000];
bool fake_read(std::uintptr_t a, void* out, std::size_t n) {
    for (const unsigned char* base : {static_cast<const unsigned char*>(fake_obj), static_cast<const unsigned char*>(fake_node),
                                      static_cast<const unsigned char*>(fake_hull)}) {
        const std::uintptr_t b = reinterpret_cast<std::uintptr_t>(base);
        const std::size_t size = base == fake_obj ? sizeof fake_obj : base == fake_node ? sizeof fake_node : sizeof fake_hull;
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
// The staged records' TierSource: radius (a ship's bubble) unless a case sets the station's (the hull decal).
std::uint8_t stage_tier_source = wc::tier_by_radius;
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
// The ship bubble's inputs (5.1.1 "Placement"): the staged owner's hull as parts in a body cache the production walk
// reads (fake_hull: the slot, the cache root, its hash, one bucket, the chain entry, the model, its LOD array and record,
// then the part nodes, 0x270 each), so stage::build's bubble is the hull bounds x bubble_scale (hull_tuning points the
// walk at it); without stage_hull the staged root has no children and the walk falls back to the radius sphere.
constexpr unsigned hull_part_at = 0x400, hull_part_bytes = 0x270, hull_parts_max = (sizeof fake_hull - 0x400) / 0x270;
ws::Tuning hull_tuning() {
    ws::Tuning t{};
    t.body_cache_root = addr(fake_hull);
    return t;
}
// The hull: `parts` part nodes (body 7, one model whose single LOD record has the normalised extents of ext), the first
// at offset off (view units, owner frame), the others stepped along z by 2 ext_z / parts (the walk's budget case);
// half-extents ext (view units).
void stage_hull(const double ext[3], const double off[3] = nullptr, unsigned parts = 1) {
    std::memset(fake_hull, 0, sizeof fake_hull);
    const std::uint32_t h = addr(fake_hull);
    put32(fake_hull, 0x00, h + 0x10);        // the slot: the cache root
    put32(fake_hull, 0x10 + 0x14, h + 0x40); // its hash
    put32(fake_hull, 0x40, h + 0x80);        // buckets
    put32(fake_hull, 0x44, 1);               // one bucket
    put32(fake_hull, 0x80, h + 0xc0);        // the chain: one entry, key 7 + 1
    put32(fake_hull, 0xc0 + 4, 8);
    put32(fake_hull, 0xc0 + 8, h + 0x100);   // the model
    put32(fake_hull, 0x100 + 0x0c, h + 0x140);
    const std::uint16_t lods = 1;
    std::memcpy(fake_hull + 0x100 + 0x10, &lods, 2);
    put32(fake_hull, 0x140, h + 0x180);
    double e[3], m = 0;
    for (unsigned k = 0; k < 3; ++k) {
        e[k] = ext[k] / double(wc::world_scale);
        m = std::max(m, e[k]);
    }
    const std::int32_t s = std::int32_t(m + 0.5);
    for (unsigned k = 0; k < 3; ++k) put32(fake_hull, 0x180 + 0x20 + 4 * k, std::uint32_t(std::lround(65536.0 * e[k] / m)));
    parts = std::min(std::max(parts, 1u), hull_parts_max);
    for (unsigned j = 0; j < parts; ++j) {
        unsigned char* c = fake_hull + hull_part_at + hull_part_bytes * j;
        const std::uint32_t self = h + hull_part_at + hull_part_bytes * j;
        put32(c, 0, j + 1 < parts ? self + hull_part_bytes : addr(fake_node) + wc::layout::node_sentinel);
        put32(c, wc::layout::node_children, self + wc::layout::node_sentinel);
        double o[3] = {off ? off[0] : 0.0, off ? off[1] : 0.0, off ? off[2] : 0.0};
        if (parts > 1) o[2] += ext[2] * (2.0 * (double(j) + 0.5) / double(parts) - 1.0);
        for (unsigned k = 0; k < 3; ++k)
            put32(c, wc::layout::node_offset + 4 * k, std::uint32_t(std::int32_t(std::lround(o[k] / double(wc::world_scale)))));
        for (unsigned k = 0; k < 3; ++k) put32(c, wc::layout::node_basis + 0x10 * k + 4 * k, 0x10000);
        put32(c, wc::layout::node_scale, std::uint32_t(s));
        for (unsigned k = 0; k < 3; ++k) put32(c, wc::layout::node_axis_scale + 4 * k, 0x10000);
        put32(c, wc::layout::node_flags, 0x1000000);
        put32(c, wc::layout::node_body, 7);
    }
    put32(fake_node, wc::layout::node_children, h + hull_part_at);
    put32(fake_node, wc::layout::node_sentinel, 0);
    put32(fake_node, wc::layout::node_flags, 0);
    for (unsigned k = 0; k < 3; ++k) put32(fake_node, wc::layout::node_axis_scale + 4 * k, 0x10000);
}
// The bubble's CPU twin in double from a bubble draw: the owner's centre and axes in view, the bubble's centre and
// half-extents (owner frame).
struct BubbleTwin {
    double centre[3], axes[3][3], c[3], e[3];
};
BubbleTwin bubble_twin(const rr::WeaponPatch& p) {
    BubbleTwin b{};
    for (unsigned k = 0; k < 3; ++k) {
        b.centre[k] = p.centre[k];
        b.c[k] = p.bubble_c[k];
        b.e[k] = p.bubble_e[k];
        for (unsigned j = 0; j < 3; ++j) b.axes[k][j] = p.axes[k][j];
    }
    return b;
}
// The production mapping in double: the hull point p (owner frame) onto the bubble, B = c + e q / |q|.
void bubble_image(const BubbleTwin& b, const double p[3], double out[3], double q[3]) {
    double n = 0;
    for (unsigned k = 0; k < 3; ++k) {
        q[k] = (p[k] - b.c[k]) / b.e[k];
        n += q[k] * q[k];
    }
    n = std::sqrt(n);
    for (unsigned k = 0; k < 3; ++k) {
        q[k] /= n;
        out[k] = b.c[k] + b.e[k] * q[k];
    }
}
// The bubble's near and far points under pixel (x, y) (owner frame) and their view z; false where the ray misses it.
bool bubble_roots(const Target& t, const BubbleTwin& b, unsigned x, unsigned y, double pn[3], double pf[3], double* tn,
                  double* tf) {
    const double m00 = 1.6, m11 = 1.6 * double(t.w) / double(t.h);
    const double d[3] = {(2.0 * x / t.w - 1.0) / m00, (1.0 - 2.0 * y / t.h) / m11, 1.0};
    double od[3], dd[3], A = 0, Bq = 0;
    for (unsigned k = 0; k < 3; ++k) {
        const double* L = b.axes[k];
        od[k] = (-(b.centre[0] * L[0] + b.centre[1] * L[1] + b.centre[2] * L[2]) - b.c[k]) / b.e[k];
        dd[k] = (d[0] * L[0] + d[1] * L[1] + d[2] * L[2]) / b.e[k];
        A += dd[k] * dd[k];
        Bq += od[k] * dd[k];
    }
    const double tm = -Bq / A;
    double h2 = 0;
    for (unsigned k = 0; k < 3; ++k) h2 += (od[k] + tm * dd[k]) * (od[k] + tm * dd[k]);
    if (!(h2 < 1.0)) return false;
    const double half = std::sqrt((1.0 - h2) / A);
    *tn = tm - half;
    *tf = tm + half;
    for (unsigned k = 0; k < 3; ++k) {
        pn[k] = b.c[k] + b.e[k] * (od[k] + *tn * dd[k]);
        pf[k] = b.c[k] + b.e[k] * (od[k] + *tf * dd[k]);
    }
    return true;
}
// The hull lane's view depth under pixel (x, y) (hull_point's ray), 1e30 for sky.
double hull_depth(const Target& t, const HullLane& h, unsigned x, unsigned y) {
    const double m00 = 1.6, m11 = 1.6 * double(t.w) / double(t.h);
    const double d[3] = {(2.0 * x / t.w - 1.0) / m00, (1.0 - 2.0 * y / t.h) / m11, 1.0};
    double A = 0, B = 0, C = -1;
    for (unsigned k = 0; k < 3; ++k) {
        const double od = -(h.centre[0] * h.axes[k][0] + h.centre[1] * h.axes[k][1] + h.centre[2] * h.axes[k][2]);
        const double dd = d[0] * h.axes[k][0] + d[1] * h.axes[k][1] + d[2] * h.axes[k][2];
        const double w = 1.0 / (h.ext[k] * h.ext[k]);
        A += dd * dd * w;
        B += od * dd * w;
        C += od * od * w;
    }
    const double disc = B * B - A * C;
    if (disc < 0.0) return 1e30;
    double tt = (-B - std::sqrt(disc)) / A;
    if (tt <= 0.0) tt = (-B + std::sqrt(disc)) / A;
    return tt > 0.0 ? tt : 1e30;
}
// One side of the bubble against an image (side 0 near, 1 far), the twin deciding each pixel's point on that side and
// whether it shows (in front of the near plane, nearer than the hull lane): lit pixels, of them over sky (the hull
// lane), with no visible point on that side (hidden_lit: the GPU drew what the twin hides), farther than sqrt 2 x the
// extent from B; the points within `core` of B, visible (eligible, covered when lit) or hidden; the luma-weighted
// centroid's distance from B (view units).
struct SideMetric {
    unsigned lit = 0, sky_lit = 0, hidden_lit = 0, outside = 0, eligible = 0, covered = 0, hidden = 0;
    double centroid = 1e30, luma = 0;
};
SideMetric side_metric(const std::vector<float>& l, const BubbleTwin& b, const HullLane* hull, const double B[3],
                       double extent, double core, int side, double near_z) {
    SideMetric o;
    double wsum = 0, cs[3] = {0, 0, 0};
    for (UINT y = 0; y < g.h; ++y)
        for (UINT x = 0; x < g.w; ++x) {
            const double v = l[std::size_t(y) * g.w + x];
            const bool lit = v > 1e-3;
            double pn[3], pf[3], tn = 0, tf = 0;
            const bool hit = bubble_roots(g, b, x, y, pn, pf, &tn, &tf);
            const double hz = hull ? hull_depth(g, *hull, x, y) : 1e30;
            const double* p = side ? pf : pn;
            const double t = side ? tf : tn;
            const bool visible = hit && t > near_z && t < hz;
            const double d = hit ? std::sqrt((p[0] - B[0]) * (p[0] - B[0]) + (p[1] - B[1]) * (p[1] - B[1]) +
                                             (p[2] - B[2]) * (p[2] - B[2]))
                                 : 1e30;
            if (lit) {
                ++o.lit;
                o.luma += v;
                o.sky_lit += hz >= 1e29;
                if (!visible) {
                    ++o.hidden_lit;
                } else {
                    o.outside += d > extent * 1.4142 * 1.01;
                    wsum += v;
                    for (unsigned k = 0; k < 3; ++k) cs[k] += v * p[k];
                }
            }
            if (hit && d <= core) {
                if (visible) {
                    ++o.eligible;
                    o.covered += lit;
                } else {
                    ++o.hidden;
                }
            }
        }
    if (wsum > 0) {
        double e2 = 0;
        for (unsigned k = 0; k < 3; ++k) e2 += (cs[k] / wsum - B[k]) * (cs[k] / wsum - B[k]);
        o.centroid = std::sqrt(e2);
    }
    return o;
}
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
// The hull pulse's ghost through the same production resolve (muzzle-flash-look.md section 3; the pulse samples the lane,
// so it cannot write the lane's far flag as the bolts do, and enters the history): 12 frames of Halton jitter over a
// hull plane at the pulse's depth, the pulse drawn by run_projectiles (pulses alone) on frame 8 only, once with it and
// once without. ghost[j] = the largest resolved difference (with - without) on frame 8 + j over its largest on frame 8.
bool resolve_pulse_ghost(rr::WeaponEffectsPass& pass, Com<IDirect3DTexture9>& motion, Com<IDirect3DSurface9>& motion_s,
                         const rr::WeaponPulse& pulse, double ghost[3], double* peak_out) {
    constexpr unsigned frames = 12, lit_frame = 8;
    struct PulseStage {
        rr::WeaponEffectsPass* pass;
        rr::WeaponProjectileFrame frame;
        HRESULT result;
        static HRESULT callback(void* context, IDirect3DDevice9*) noexcept {
            auto* c = static_cast<PulseStage*>(context);
            rr::WeaponEffectsReport r{};
            c->result = c->pass->run_projectiles(c->frame, &r);
            return c->result;
        }
    };
    std::vector<float> res[2][3];
    for (unsigned on = 0; on < 2; ++on) {
        rr::TemporalPass taa;
        HRESULT hr = taa.initialize(dev, nullptr, reinterpret_cast<const DWORD*>(rr::temporal_resolve_program()), nullptr,
                                    nullptr, reinterpret_cast<const DWORD*>(rr::hdr_writeback_program()));
        if (SUCCEEDED(hr)) hr = taa.configure_far();
        if (FAILED(hr)) return false;
        rr::Output o{};
        float pjx = 0, pjy = 0;
        for (unsigned n = 0; n < frames; ++n) {
            const float jx = float(halton(n % 8 + 1, 2) - .5), jy = float(halton(n % 8 + 1, 3) - .5);
            draw_lane(g, pulse.centre[2], 1.f);
            const float quad[4][4] = {{-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
            dev->BeginScene();
            dev->SetRenderTarget(0, motion_s.p);
            dev->SetVertexDeclaration(quad_decl);
            dev->SetVertexShader(quad_vs);
            dev->SetPixelShader(motion_ps);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
            dev->SetRenderTarget(0, g.scene_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 13, 13, 13), 1.f, 0); // a dim hull, 0.051
            dev->EndScene();
            const rr::WeaponEffectsFrame bf = base_frame(g);
            PulseStage ctx{&pass, {}, S_FALSE};
            ctx.frame.width = g.w;
            ctx.frame.height = g.h;
            ctx.frame.pulses = &pulse;
            ctx.frame.pulse_count = 1;
            ctx.frame.lane_texture = g.lane_tex.p;
            ctx.frame.four_channel = true;
            ctx.frame.m00 = bf.m00;
            ctx.frame.m11 = bf.m11;
            ctx.frame.m20 = 2.f * jx / float(g.w);
            ctx.frame.m21 = -2.f * jy / float(g.h);
            ctx.frame.m22 = 1.f;
            ctx.frame.m32 = -1.f;
            ctx.frame.near_z = bf.near_z;
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
            if (on && n == lit_frame) {
                in.stage_callback = &PulseStage::callback;
                in.stage_context = &ctx;
            }
            dev->BeginScene();
            hr = taa.run(in, &o);
            dev->EndScene();
            if (FAILED(hr) || (on && n == lit_frame && (!taa.diagnostics().stage_ran || ctx.result != S_OK))) return false;
            pjx = jx;
            pjy = jy;
            if (n >= lit_frame && n < lit_frame + 3) {
                Com<IDirect3DSurface9> resolved;
                if (!o.color || FAILED(o.color->GetSurfaceLevel(0, &resolved.p))) return false;
                res[on][n - lit_frame] = read_luma(g, resolved.p);
            }
        }
        taa.shutdown();
    }
    double first = 0.0;
    for (unsigned j = 0; j < 3; ++j) {
        if (res[0][j].empty() || res[1][j].size() != res[0][j].size()) return false;
        double worst = 0.0;
        for (std::size_t i = 0; i < res[0][j].size(); ++i) worst = std::max(worst, double(res[1][j][i]) - double(res[0][j][i]));
        if (!j) first = worst;
        ghost[j] = first > 0.0 ? worst / first : 0.0;
    }
    *peak_out = first;
    return first > 0.0;
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

namespace {
// ------------------------------------------------------------------ projectiles (docs/architecture/projectile-look.md)
// The projectile stage's cases: a scene target (A16B16G16R16F), the four-lane lane (A32B32G32R32F, the far flag's
// RT1) and a D24S8 depth surface; bolts built by the production projectile_stage::bolt_vertices / build and drawn by
// the production WeaponEffectsPass::run_projectiles. The view is the identity rotation (view = world), the projection
// m00 (1.6 or a second FOV), m11 = m00 W / H, near 1 view unit (0.2 m), far 1e6.
namespace ps = x3m::weapon_effects::projectile_stage;
namespace pf = x3m::weapon_effects::projectile_family;
float scalar_len(float a, float b) { return std::sqrt(a * a + b * b); }
struct ProjTarget {
    UINT w = 0, h = 0;
    Com<IDirect3DTexture9> scene_tex, lane_tex;
    Com<IDirect3DSurface9> scene_s, lane_s, depth_s, scene_sys, lane_sys;
    void release() {
        scene_s.release(); lane_s.release(); depth_s.release(); scene_sys.release(); lane_sys.release();
        scene_tex.release(); lane_tex.release();
    }
};
bool make_proj_target(ProjTarget& t, UINT w, UINT h) {
    device_guard("make_proj_target");
    t.w = w;
    t.h = h;
    return SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A16B16G16R16F, D3DPOOL_DEFAULT, &t.scene_tex.p, nullptr)) &&
           SUCCEEDED(t.scene_tex.p->GetSurfaceLevel(0, &t.scene_s.p)) &&
           SUCCEEDED(dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &t.lane_tex.p, nullptr)) &&
           SUCCEEDED(t.lane_tex.p->GetSurfaceLevel(0, &t.lane_s.p)) &&
           SUCCEEDED(dev->CreateDepthStencilSurface(w, h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &t.depth_s.p, nullptr)) &&
           SUCCEEDED(dev->CreateOffscreenPlainSurface(w, h, D3DFMT_A16B16G16R16F, D3DPOOL_SYSTEMMEM, &t.scene_sys.p, nullptr)) &&
           SUCCEEDED(dev->CreateOffscreenPlainSurface(w, h, D3DFMT_A32B32G32R32F, D3DPOOL_SYSTEMMEM, &t.lane_sys.p, nullptr));
}
ps::View proj_view(const ProjTarget& t, float m00) {
    ps::View v;
    v.r[0] = v.r[4] = v.r[8] = 1.f;
    v.m00 = m00;
    v.m11 = m00 * float(t.w) / float(t.h);
    const float n = 1.f, f = 1e6f;
    v.m22 = f / (f - n);
    v.m32 = -n * f / (f - n);
    v.width = float(t.w);
    v.height = float(t.h);
    v.near_z = n;
    return v;
}
ps::BodyLook ire_look() {
    ps::BodyLook l;
    ps::tint_of(wc::pack_rgb(121, 82, 39), l.tint); // the IRE row's texture_rgb (weapon_effects.json row 0)
    l.valid = 1;
    return l;
}
// A first-frame bolt at view depth d_m metres moving along view +x at speed_mps (age long past): its streak (P - v dt to
// the head P) centred on the screen centre, so the head sits half the travel ahead of it.
ps::BoltIn bolt_at(float d_m, float speed_mps, float step_ms, float dir_y = 0.f) {
    ps::BoltIn in;
    in.p[2] = d_m * ps::view_units_per_metre;
    const float n = scalar_len(1.f, dir_y);
    in.v[0] = speed_mps * ps::view_units_per_metre / n;
    in.v[1] = speed_mps * ps::view_units_per_metre * dir_y / n;
    in.step_s = step_ms * 1e-3f;
    in.age_s = 10.f;
    in.p[0] = 0.5f * in.v[0] * in.step_s;
    in.p[1] = 0.5f * in.v[1] * in.step_s;
    return in;
}
// One pool bullet for the builder cases: the object, its node (live, handle, body 0x55, position and velocity in engine
// units) and its extension (expiry far ahead, born at 1 ms), registered by the object tap.
struct PoolBullet {
    alignas(16) unsigned char obj[0x80], node[0x150], ext[0x10];
};
void pool_bullet(wc::ProjectilePool& pool, PoolBullet& b, std::uint32_t handle, const std::int32_t pos[3],
                 const std::int32_t vel[3]) {
    std::memset(&b, 0, sizeof b);
    put32(b.obj, wc::layout::obj_node, std::uint32_t(reinterpret_cast<std::uintptr_t>(b.node)));
    put32(b.obj, wc::layout::obj_ext, std::uint32_t(reinterpret_cast<std::uintptr_t>(b.ext)));
    put32(b.node, wc::layout::node_live, 1);
    put32(b.node, wc::layout::node_handle, handle);
    put32(b.node, wc::layout::node_body, 0x55);
    for (unsigned k = 0; k < 3; ++k) {
        put32(b.node, wc::layout::node_pos + 4 * k, std::uint32_t(pos[k]));
        put32(b.node, wc::projectile::node_velocity + 4 * k, std::uint32_t(vel[k]));
    }
    put32(b.ext, wc::projectile::ext_expiry, 100000000);
    put32(b.ext, wc::projectile::ext_created, 1);
    wc::projectile_tap(reinterpret_cast<std::uintptr_t>(b.obj), pool);
}
// A pool with the one enabled body 0x55 and the game clock at cfg +0x708 (the tap's stamp).
void pool_reset(wc::ProjectilePool& pool, unsigned char* cfg, std::uint32_t* cfg_slot, std::uint32_t now) {
    wc::projectile_clear(pool);
    pool.armed = 1;
    pool.resolved = 1;
    pool.body_count = 1;
    pool.bodies[0].id = 0x55;
    pool.bits[0x55 >> 5] |= 1u << (0x55 & 31u);
    *cfg_slot = std::uint32_t(reinterpret_cast<std::uintptr_t>(cfg));
    pool.globals.cfg_slot = reinterpret_cast<std::uintptr_t>(cfg_slot);
    put32(cfg, wc::layout::cfg_game_ms, now);
}
HRESULT proj_draw(rr::WeaponEffectsPass& p, ProjTarget& t, const rr::WeaponProjectileVertex* v, unsigned quads,
                  float clear_z = 1.f, rr::WeaponEffectsReport* rep = nullptr) {
    dev->BeginScene();
    dev->SetDepthStencilSurface(t.depth_s.p);
    dev->SetRenderTarget(0, t.lane_s.p);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
    dev->SetRenderTarget(0, t.scene_s.p);
    dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, clear_z, 0);
    dev->SetDepthStencilSurface(nullptr); // the stage callback's state: the depth surface unbound
    rr::WeaponProjectileFrame f;
    f.width = t.w;
    f.height = t.h;
    f.vertices = v;
    f.quads = quads;
    f.lane = t.lane_s.p;
    f.depth = t.depth_s.p;
    rr::WeaponEffectsReport r{};
    const HRESULT hr = p.run_projectiles(f, &r);
    dev->EndScene();
    if (rep) *rep = r;
    return hr;
}
// The scene's max(r, g, b) per pixel, or the lane's four lanes per pixel.
std::vector<float> proj_scene(ProjTarget& t) {
    std::vector<float> out;
    if (FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) return out;
    D3DLOCKED_RECT lr{};
    if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return out;
    out.resize(std::size_t(t.w) * t.h);
    for (UINT y = 0; y < t.h; ++y) {
        const auto* row = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
        for (UINT x = 0; x < t.w; ++x)
            out[std::size_t(y) * t.w + x] =
                std::max(half_to_float(row[x * 4]), std::max(half_to_float(row[x * 4 + 1]), half_to_float(row[x * 4 + 2])));
    }
    t.scene_sys.p->UnlockRect();
    return out;
}
std::vector<float> proj_lane(ProjTarget& t) {
    std::vector<float> out;
    if (FAILED(dev->GetRenderTargetData(t.lane_s.p, t.lane_sys.p))) return out;
    D3DLOCKED_RECT lr{};
    if (FAILED(t.lane_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return out;
    out.resize(std::size_t(t.w) * t.h * 4);
    for (UINT y = 0; y < t.h; ++y)
        std::memcpy(&out[std::size_t(y) * t.w * 4], static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch, t.w * 16);
    t.lane_sys.p->UnlockRect();
    return out;
}
// The full width at half maximum through pixel (x, y) along (dx, dy), linear between the samples straddling half.
double fwhm_through(const std::vector<float>& l, UINT w, UINT h, int x, int y, int dx, int dy) {
    if (l.empty()) return -1.0;
    const double peak = l[std::size_t(y) * w + x];
    if (!(peak > 0.0)) return 0.0;
    double sum = 0.0;
    for (int s : {1, -1}) {
        double prev = peak;
        for (int k = 1;; ++k) {
            const int xx = x + s * k * dx, yy = y + s * k * dy;
            if (xx < 0 || yy < 0 || xx >= int(w) || yy >= int(h)) return -1.0;
            const double v = l[std::size_t(yy) * w + xx];
            if (v < 0.5 * peak) {
                sum += double(k - 1) + (prev - 0.5 * peak) / (prev - v);
                break;
            }
            prev = v;
        }
    }
    return sum;
}
// ------------------------------------------------------------------ muzzle flashes (docs/architecture/muzzle-flash-look.md)
// The extent at `level` through pixel (x, y) along (dx, dy): the distance between its two crossings of level. Each
// crossing between the samples k - 1 (above) and k (under) is the root of the parabola in log space through the samples
// k - 2, k - 1 and k (k - 2 = -1: the sample on the other side of the centre), exact for a Gaussian however few pixels it
// spans; log-linear between k - 1 and k when a sample is 0 or the parabola is not concave there. 0 when the pixel itself
// is under level, -1 off the target.
double extent_through(const std::vector<float>& l, UINT w, UINT h, int x, int y, int dx, int dy, double level) {
    if (l.empty()) return -1.0;
    const double centre = l[std::size_t(y) * w + x];
    if (!(centre > level)) return 0.0;
    const auto at = [&](int s, int k, double* v) {
        const int xx = x + s * k * dx, yy = y + s * k * dy;
        if (xx < 0 || yy < 0 || xx >= int(w) || yy >= int(h)) return false;
        *v = l[std::size_t(yy) * w + xx];
        return true;
    };
    const double ll = std::log(level);
    double sum = 0.0;
    for (int s : {1, -1}) {
        for (int k = 1;; ++k) {
            double v = 0, prev = 0, before = 0;
            if (!at(s, k, &v) || !at(s, k - 1, &prev)) return -1.0;
            if (v > level) continue;
            const double lp = std::log(prev), lv = std::log(std::max(v, 1e-30));
            double t = (lp - ll) / (lp - lv); // log-linear
            if (v > 0.0 && at(s, k - 2, &before) && before > 0.0) {
                // y(u) = a + b u + c u^2 through u = -1, 0, 1 (the samples k - 2, k - 1, k); the root in [0, 1]
                const double y0 = std::log(before), y1 = lp, y2 = lv;
                const double a = y1 - ll, b = 0.5 * (y2 - y0), c = 0.5 * (y2 + y0) - y1;
                const double disc = b * b - 4.0 * c * a;
                if (c < 0.0 && disc >= 0.0) {
                    const double root = (-b - std::sqrt(disc)) / (2.0 * c);
                    if (root >= 0.0 && root <= 1.0) t = root;
                }
            }
            sum += double(k - 1) + t;
            break;
        }
    }
    return sum;
}
std::size_t brightest(const std::vector<float>& l) {
    std::size_t best = 0;
    for (std::size_t i = 1; i < l.size(); ++i)
        if (l[i] > l[best]) best = i;
    return best;
}
// The projectile target's lane as a plane at view depth `depth` everywhere (lane_ps: .r 0.5 an occluder, .b the depth).
void fill_proj_lane(ProjTarget& t, float depth) {
    const float c0[4] = {float(t.w), depth, 1.f, 0.f};
    const float quad[4][4] = {{-1, 1, 0.5f, 1}, {1, 1, 0.5f, 1}, {-1, -1, 0.5f, 1}, {1, -1, 0.5f, 1}};
    dev->BeginScene();
    dev->SetRenderTarget(0, t.lane_s.p);
    dev->SetDepthStencilSurface(nullptr);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);
    dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev->SetVertexDeclaration(quad_decl);
    dev->SetVertexShader(quad_vs);
    dev->SetPixelShader(lane_ps);
    dev->SetPixelShaderConstantF(0, c0, 1);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(float) * 4);
    dev->EndScene();
}
// A flash's shooter and its bullet for the builder cases: the shooter (identity rows, its node at ship_pos moving at
// ship_vel, engine units) and the bullet born at `born` at the muzzle (ship-relative, engine units) moving at ship_vel
// + bullet_vel; one muzzle record of the shooter fired at `born` (ours or not, the previous shot gap_ms before, 0 a
// first shot). place(t) puts both at their positions at game ms t, the clock at t and taps the bullet.
struct FlashWorld {
    alignas(16) unsigned char ship[0x100], ship_node[0x200], obj[0x100], node[0x200], ext[0x80];
    double ship_pos[3]{}, ship_vel[3]{}, muzzle[3]{}, bullet_vel[3]{};
    std::uint32_t born = 0;
    unsigned char* cfg = nullptr;
    void make(wc::ProjectilePool& pool, std::uint32_t handle, std::uint32_t gap_ms, bool ours, unsigned slot) {
        std::memset(ship, 0, sizeof ship);
        std::memset(ship_node, 0, sizeof ship_node);
        std::memset(obj, 0, sizeof obj);
        std::memset(node, 0, sizeof node);
        std::memset(ext, 0, sizeof ext);
        put32(ship, wc::layout::obj_class, wc::layout::class_ship);
        put32(ship, wc::layout::obj_node, std::uint32_t(reinterpret_cast<std::uintptr_t>(ship_node)));
        put32(ship_node, wc::layout::node_live, 1);
        put32(ship_node, wc::layout::node_handle, 0x5000 + handle);
        for (unsigned k = 0; k < 3; ++k) put32(ship_node, wc::layout::node_rot + 16 * k + 4 * k, 0x10000);
        put32(obj, wc::layout::obj_node, std::uint32_t(reinterpret_cast<std::uintptr_t>(node)));
        put32(obj, wc::layout::obj_ext, std::uint32_t(reinterpret_cast<std::uintptr_t>(ext)));
        put32(node, wc::layout::node_live, 1);
        put32(node, wc::layout::node_handle, handle);
        put32(node, wc::layout::node_body, 0x55);
        put32(ext, wc::projectile::ext_expiry, born + 100000);
        put32(ext, wc::projectile::ext_owner, std::uint32_t(reinterpret_cast<std::uintptr_t>(ship)));
        put32(ext, wc::projectile::ext_created, born);
        // the shooter's world velocity as the engine keeps it: node +0x100 plus the slip obj +0x60 (a third of it here),
        // the bullet's the fire path's sum of both and its own (FL 0x04f88b..0x04f915)
        for (unsigned k = 0; k < 3; ++k) {
            const std::int32_t slip = std::int32_t(ship_vel[k] / 3.0);
            put32(ship_node, wc::projectile::node_velocity + 4 * k, std::uint32_t(std::int32_t(ship_vel[k]) - slip));
            put32(ship, 0x60 + 4 * k, std::uint32_t(slip));
            put32(node, wc::projectile::node_velocity + 4 * k, std::uint32_t(std::int32_t(ship_vel[k] + bullet_vel[k])));
        }
        wc::MuzzleRecord& m = pool.muzzles[slot];
        m = wc::MuzzleRecord{};
        m.ship = std::uint32_t(reinterpret_cast<std::uintptr_t>(ship));
        m.node = std::uint32_t(reinterpret_cast<std::uintptr_t>(ship_node));
        m.handle = 0x5000 + handle;
        m.t0 = born;
        m.prev_t0 = gap_ms ? born - gap_ms : 0;
        m.slot = std::uint16_t(slot);
        m.ours = ours;
        m.sequence = 1 + slot;
    }
    void place(wc::ProjectilePool& pool, std::uint32_t t, bool tap = true) {
        const double s = double(std::int32_t(t - born)) * 1e-3;
        put32(cfg, wc::layout::cfg_game_ms, t);
        for (unsigned k = 0; k < 3; ++k) {
            const double sp = ship_pos[k] + ship_vel[k] * s;
            put32(ship_node, wc::layout::node_pos + 4 * k, std::uint32_t(std::int32_t(std::lround(sp))));
            put32(node, wc::layout::node_pos + 4 * k,
                  std::uint32_t(std::int32_t(std::lround(ship_pos[k] + muzzle[k] + (ship_vel[k] + bullet_vel[k]) * s))));
        }
        if (tap) wc::projectile_tap(reinterpret_cast<std::uintptr_t>(obj), pool);
    }
};
// The comet through the production builder (FL 60 A, projectile-look.md section 2): one bullet at distance_m crossing
// the view along +x (581,000 engine units/s, world), `frames` consecutive stage frames (the node and the clock advanced
// by v x step_ms and step_ms, the render dt between them), the camera moving along +x at cam_mps (the eye at i x its
// travel per frame) or turning turn_deg per frame (yaw; accelerating: turn_deg x i (i + 1) / 2 at frame i, so the screen
// speed changes from frame to frame and the comet is drawn in segments); the last frame's head at ndc x head_ndc on the
// centre row. Each
// frame's bolt shape and counts; the last frame's quads drawn into t and the row y = h / 2 read back, with the age along
// it: piecewise linear through the knots (every frame's head at its age, the first frame's motion tail at its own).
struct CometRun {
    static constexpr unsigned max_frames = 8;
    ps::BoltShape shape[max_frames];
    ps::Counts counts[max_frames];
    unsigned frames = 0, drawn = 0, jumped = 0, statics = 0;
    std::vector<float> row;
    double knot_x[max_frames + 1]{}, knot_age_ms[max_frames + 1]{}; // newest first
    unsigned knots = 0;
    double age_at(double x) const {
        if (knots < 2) return 0.0;
        unsigned k = 0;
        while (k + 2 < knots && !((x - knot_x[k]) * (x - knot_x[k + 1]) <= 0.0)) ++k;
        const double dx = knot_x[k + 1] - knot_x[k];
        return std::fabs(dx) > 1e-9 ? knot_age_ms[k] + (x - knot_x[k]) / dx * (knot_age_ms[k + 1] - knot_age_ms[k])
                                    : knot_age_ms[k];
    }
};
CometRun comet_run(rr::WeaponEffectsPass& proj, ProjTarget& t, const pf::Tune& tune, float distance_m, unsigned frames,
                   std::uint32_t step_ms, double render_dt, double cam_mps, double turn_deg, double head_ndc,
                   std::uint32_t first_device_frame, bool accelerate = false) {
    static wc::ProjectilePool pool;
    alignas(16) static unsigned char cfg[0x800];
    static std::uint32_t cfg_slot;
    static PoolBullet bullet;
    static ps::Looks looks;
    static rr::WeaponProjectileVertex verts[rr::weapon_projectile_quads_per_bolt * 4];
    CometRun r;
    frames = std::max(1u, std::min(frames, CometRun::max_frames));
    r.frames = frames;
    const ps::View base = proj_view(t, 1.6f);
    const double pi = 3.14159265358979, z = double(distance_m) * 500.0, v = 581000.0, step_s = double(step_ms) * 1e-3;
    const double cam_step = cam_mps * 500.0 * render_dt;
    const auto turn_at = [&](unsigned i) {
        return turn_deg * (accelerate ? 0.5 * double(i) * double(i + 1) : double(i)) * pi / 180.0;
    };
    const double turn_last = turn_at(frames - 1), eye_last = cam_step * double(frames - 1);
    const double k = head_ndc / double(base.m00); // view_x / view_z of the last head
    const double x_last = turn_deg != 0.0
                              ? z * (k * std::cos(turn_last) - std::sin(turn_last)) / (std::cos(turn_last) + k * std::sin(turn_last))
                              : eye_last + k * z;
    const pf::Tune tunes[pf::family_count] = {tune};
    const std::int32_t vel[3] = {581000, 0, 0};
    pool_reset(pool, cfg, &cfg_slot, 5000);
    looks.look[0] = ire_look(); // the IRE row's look (no table here)
    looks.resolved = 1;
    for (unsigned i = 0; i < frames; ++i) {
        const std::uint32_t now = 5000u + i * step_ms;
        const std::int32_t pos[3] = {std::int32_t(std::lround(x_last - v * step_s * double(frames - 1 - i))), 0,
                                     std::int32_t(z)};
        put32(cfg, wc::layout::cfg_game_ms, now);
        if (!i) {
            pool_bullet(pool, bullet, 90, pos, vel);
        } else {
            put32(bullet.node, wc::layout::node_pos, std::uint32_t(pos[0]));
            wc::projectile_tap(reinterpret_cast<std::uintptr_t>(bullet.obj), pool);
        }
        ps::View view = base;
        const double turn = turn_at(i);
        view.r[0] = view.r[8] = float(std::cos(turn));
        view.r[6] = float(std::sin(turn));
        view.r[2] = -float(std::sin(turn));
        view.t[0] = float(-cam_step * double(i) * double(wc::world_scale)); // eye = -t / world_scale (identity basis)
        r.counts[i] = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, first_device_frame + i, false, now, step_ms,
                                float(render_dt), 1.f, view, tunes, verts, 1, rr::weapon_projectile_quads_per_bolt,
                                &r.shape[i]);
        r.drawn += r.counts[i].drawn;
        r.jumped += r.counts[i].jumped;
        r.statics += r.counts[i].statics;
    }
    proj_draw(proj, t, verts, r.counts[frames - 1].quads);
    const std::vector<float> l = proj_scene(t);
    if (!l.empty()) r.row.assign(l.begin() + std::ptrdiff_t(t.h / 2) * t.w, l.begin() + std::ptrdiff_t(t.h / 2 + 1) * t.w);
    for (unsigned i = frames; i-- > 0;) {
        r.knot_x[r.knots] = r.shape[i].head[0];
        r.knot_age_ms[r.knots++] = double(frames - 1 - i) * double(step_ms);
    }
    r.knot_x[r.knots] = r.shape[0].motion_tail[0];
    r.knot_age_ms[r.knots++] = double(frames - 1) * double(step_ms) + double(r.shape[0].tail_age_s) * 1e3;
    return r;
}
// The comet's profile along the last frame's row: C, the trail's start (the row 3 core widths behind the head over the
// law there); the row's maximum and where; the largest deviation from C F(age) where F >= 0.1 and more than three glow
// sigmas inside the drawn end (its end ramp, at trail_floor, is not the law) and the largest rise over
// the running minimum walking back from the head while F >= 0.02 (a seam's bump or a saw-tooth); the ages (game ms,
// interpolated) where the row falls under 50 % and 20 % of C; the row at the point of age trail_ms over C.
struct CometProfile {
    double c = 0, head_max = 0, head_max_x = 0, dev = 0, bump = 0, age50_ms = -1, age20_ms = -1, at_trail = 1;
};
double comet_law(double age_ms, double trail_ms) {
    const double f = (8.0 * std::pow(2.0, -3.0 * age_ms / trail_ms) - 1.0) / 7.0;
    return f < 0.0 ? 0.0 : f > 1.0 ? 1.0 : f;
}
CometProfile comet_profile(const CometRun& r, const pf::Tune& tune) {
    CometProfile p;
    if (r.row.empty() || !r.frames) return p;
    const ps::BoltShape& s = r.shape[r.frames - 1];
    const double w = s.core_px, head = s.head[0], trail = tune.trail_ms;
    const int n = int(r.row.size());
    const auto at = [&](int x) { return x >= 0 && x < n ? double(r.row[std::size_t(x)]) : 0.0; };
    const int x0 = int(std::lround(head - 3.0 * w));
    const double f0 = comet_law(r.age_at(x0), trail);
    p.c = f0 > 0.0 ? at(x0) / f0 : 0.0;
    if (!(p.c > 0.0)) return p;
    for (int x = int(std::floor(double(s.trail_end[0]))) - 4; x <= int(std::ceil(head + 3.0 * w)); ++x)
        if (at(x) > p.head_max) p.head_max = at(x), p.head_max_x = x;
    double runmin = 1e30, prev_v = at(x0), prev_a = r.age_at(x0);
    bool past_trail = false;
    for (int x = x0; x >= 0; --x) {
        const double a = r.age_at(x), f = comet_law(a, trail), v = at(x);
        if (!past_trail && a >= trail) {
            p.at_trail = v / p.c;
            past_trail = true;
        }
        if (p.age50_ms < 0.0 && v < 0.5 * p.c && prev_v >= 0.5 * p.c)
            p.age50_ms = prev_a + (prev_v - 0.5 * p.c) / (prev_v - v) * (a - prev_a);
        if (p.age20_ms < 0.0 && v < 0.2 * p.c && prev_v >= 0.2 * p.c)
            p.age20_ms = prev_a + (prev_v - 0.2 * p.c) / (prev_v - v) * (a - prev_a);
        if (f >= 0.02) {
            runmin = std::min(runmin, v);
            p.bump = std::max(p.bump, runmin > 0.0 ? v / runmin - 1.0 : 0.0);
            if (f >= 0.1 && std::fabs(double(x) - double(s.trail_end[0])) > 3.0 * double(s.glow_sigma_px))
                p.dev = std::max(p.dev, std::fabs(v / (p.c * f) - 1.0));
        }
        if (past_trail) break;
        prev_v = v;
        prev_a = a;
    }
    return p;
}
} // namespace

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
    std::printf("ATTACH_SLOTS shell_vs=%u shell_ps=%u bubble_ps=%u decal_vs=%u decal_ps=%u patch_bytes=%u frame_patches_bytes=%u\n",
                pass.caps().shell_vs_slots, pass.caps().shell_ps_slots, pass.caps().bubble_ps_slots, pass.caps().decal_vs_slots,
                pass.caps().decal_ps_slots, unsigned(sizeof(rr::WeaponPatch)),
                unsigned(sizeof(rr::WeaponPatch) * wc::max_patches));
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
    // Placement (5.1.1 "Placement", user decision after Run238 Flight E): through the production builder with the
    // identity camera, one hit 0.5 s old (every footprint cell lit), the lane holding the case's hull (an ellipsoid of
    // the owner's frame: the modelled half-extents, else a sphere through the hit point). Ships draw on their bubble:
    // the staged owner's hull is one part in the body cache the production walk reads (stage_hull), so the bubble is the
    // hull's half-extents x bubble_scale about its centre and the hit maps radially onto it (B). Per ship case the image
    // with the near side only (bubble_far_gain 0): the lit pixels whose near point (the bubble twin) lies hidden or off the
    // bubble (must be under 0.5 %), farther than sqrt 2 x the extent from B (the plane and the axis each within it; at
    // most 1 %: not on the tail), the coverage of the visible bubble within half the reach of B (at least 0.9), the
    // luma-weighted centroid of the lit near points against B (within one cell), the lit count against its hull's
    // reference (a third to three times), and the lit pixels over sky (the bubble reaches past the hull's outline:
    // recorded). Narrow: a 100 m corvette (1:1:6, R its 50 m half-length) on its flank 9 % aft, its flank, top and
    // bottom 40 % aft; flat: a fighter of half-extents 6:1:4 (12 x 2 x 8 m) on its flank, top and bottom 0.3 of each
    // half-extent off centre, and its top hit by a bolt 42 degrees below its plane (the bolt's direction plays no part:
    // the face-on count); the top with the hull centred 2 cells below the owner's origin. The station keeps the hull
    // decal (tier_by_station): the Flight B station (R 1304 m) at its recorded contact point (0.676 R), on the hull lane
    // (nothing on sky, nothing past the footprint). The chase camera and the eye inside the bubble follow.
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
        constexpr unsigned count = 10, station = 8;
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
             {0.f, -30.f, 0.f}}};
        const std::int32_t (*rows_of[count])[3] = {side, side, top, bottom, side, top, bottom, top, facing, top};
        for (unsigned k = 0; k < count; ++k) std::memcpy(cases[k].rows, rows_of[k], sizeof side);
        struct Result {
            unsigned lit = 0, sky_lit = 0, outside = 0, eligible = 0, covered = 0, patches = 0, bubbles = 0, off_bubble = 0;
            double centroid_cells = 1e30, lit_ratio = 0, b_shift_cells = 0;
            double cap_lo = 0, cap_hi = 0; // the bubble past the hull's tail and nose along z (view units; > 0: past)
            bool drew = false;
        } res[count];
        rr::WeaponLook near_only = rr::default_weapon_look();
        near_only.bubble_far_gain = 0.f;
        for (unsigned i = 0; i < count; ++i) {
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
            if (i != station) stage_hull(ext, off);
            const ws::Counts n = ws::build(stage_ring, 1500, &fake_read, identity_camera(), hull_tuning(), patches,
                                           wc::max_patches, decals, wc::max_decals);
            const HullLane hull = staged_hull(ext, off);
            const std::vector<float> l = render(pass, patches, n.patches, i == station ? nullptr : &near_only, nullptr, &hull);
            const double extent = n.patches ? double(patches[0].extent) : 0.0, cell = n.patches ? double(patches[0].cell) : 1.0;
            const double core = n.patches ? 0.5 * double(patches[0].reach) : 0.0; // surely inside the footprint's cells
            Result& o = res[i];
            o.patches = n.patches;
            o.bubbles = n.bubbles;
            if (i != station && n.patches) {
                const BubbleTwin bt = bubble_twin(patches[0]);
                double B[3], q[3];
                bubble_image(bt, hp, B, q);
                double shift = 0;
                for (unsigned k = 0; k < 3; ++k) shift += (B[k] - hp[k]) * (B[k] - hp[k]);
                o.b_shift_cells = std::sqrt(shift) / cell; // the bubble point's distance from the hull's contact point
                o.cap_hi = double(patches[0].bubble_c[2] + patches[0].bubble_e[2]) - (off[2] + ext[2]);
                o.cap_lo = (off[2] - ext[2]) - double(patches[0].bubble_c[2] - patches[0].bubble_e[2]);
                const SideMetric m = side_metric(l, bt, &hull, B, extent, core, 0, base_frame(g).near_z);
                o.lit = m.lit;
                o.sky_lit = m.sky_lit;
                o.outside = m.outside;
                o.eligible = m.eligible;
                o.covered = m.covered;
                o.off_bubble = m.hidden_lit;
                o.centroid_cells = m.centroid / cell;
                o.drew = o.lit > 100;
                continue;
            }
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
        stage_tier_source = wc::tier_by_radius;
        for (unsigned i = 0; i < count; ++i) {
            const Result& o = res[i];
            const double ratio = res[cases[i].ref].lit ? double(o.lit) / double(res[cases[i].ref].lit) : 0.0;
            res[i].lit_ratio = ratio;
            std::printf("PLACEMENT case=%s surface=%s patches=%u bubbles=%u lit_pixels=%u sky_lit=%u outside_lit=%u "
                        "off_bubble_lit=%u eligible=%u covered=%u coverage=%.3f centroid_error_cells=%.3f "
                        "bubble_shift_cells=%.3f cap_nose_m=%.2f cap_tail_m=%.2f ref=%s lit_ratio=%.3f dir=%d,%d,%d\n",
                        cases[i].name, i == station ? "hull" : "bubble", o.patches, o.bubbles, o.lit, o.sky_lit, o.outside,
                        o.off_bubble, o.eligible, o.covered, o.eligible ? double(o.covered) / double(o.eligible) : 0.0,
                        o.centroid_cells, o.b_shift_cells, o.cap_hi / 5.0, o.cap_lo / 5.0, cases[cases[i].ref].name, ratio,
                        int(cases[i].dir[0]), int(cases[i].dir[1]), int(cases[i].dir[2]));
        }
        auto on_bubble = [&](unsigned i) {
            const Result& o = res[i];
            return o.drew && o.bubbles == 1 && o.off_bubble * 200 <= o.lit && o.outside * 100 <= o.lit && o.eligible &&
                   double(o.covered) >= 0.9 * double(o.eligible) && o.centroid_cells <= 1.0 && o.lit_ratio >= 1.0 / 3.0 &&
                   o.lit_ratio <= 3.0;
        };
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
        report("placement_narrow_hull_flank_aft_top_bottom_on_the_bubble",
               on_bubble(0) && on_bubble(1) && on_bubble(2) && on_bubble(3), "");
        // the radius cut at bubble_scale x the radius: the narrow hull reaching its radius keeps the cap past its nose
        // and tail (at least 0.1 of the half-length; 1.15 x gives 0.15)
        report("bubble_cap_past_the_narrow_hull_nose_and_tail",
               res[1].cap_hi >= 0.1 * 250.0 && res[1].cap_lo >= 0.1 * 250.0, "");
        report("placement_flat_fighter_flank_top_bottom_42deg_on_the_bubble",
               on_bubble(4) && on_bubble(5) && on_bubble(6) && on_bubble(7) && res[7].lit == res[5].lit, "");
        report("placement_station_hull_decal_at_the_hit", on_hull(station) && res[station].bubbles == 0, "");
        report("placement_offcentre_origin_top_lights", on_bubble(9) && res[9].lit_ratio >= 0.7, "");
        draw_lane(g, 0.f, -1.f);
    }
    // A hit a hair either side of an axis plane (review F2): the flat fighter's top hit at its centre with x = +1, -1 and
    // 0 engine units (Q_x = +-eps): every face lit by each (no sign rule), so the three images agree (lit pixels within
    // 0.1 %, the peak within 0.2 %, the luma sum within 0.1 %) and no point is lit twice (the peak not above the zero
    // case's).
    {
        rr::WeaponPatch patches[wc::max_patches];
        rr::WeaponDecal decals[wc::max_decals];
        const std::int32_t top_rows[3][3] = {{65536, 0, 0}, {0, 0, -65536}, {0, 65536, 0}};
        const double ext[3] = {60.0, 10.0, 40.0};
        stage_tier_source = wc::tier_by_ship_class;
        double sum[3] = {}, peak[3] = {};
        unsigned lit[3] = {}, draws[3] = {};
        const std::int32_t xs[3] = {1, -1, 0};
        for (unsigned v = 0; v < 3; ++v) {
            stage_owner(400.f, 66.f, 1000);
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned j = 0; j < 3; ++j) put32(fake_node, wc::layout::node_rot + 16 * r + 4 * j, std::uint32_t(top_rows[r][j]));
            stage_ring.slots[0].pos[0] = xs[v];
            stage_ring.slots[0].pos[1] = 1000;
            stage_ring.slots[0].pos[2] = 0;
            stage_hull(ext);
            const ws::Counts n = ws::build(stage_ring, 1500, &fake_read, identity_camera(), hull_tuning(), patches,
                                           wc::max_patches, decals, wc::max_decals);
            draws[v] = n.patches;
            const HullLane hull = staged_hull(ext);
            const std::vector<float> l = render(pass, patches, n.patches, nullptr, nullptr, &hull);
            for (float x : l) {
                sum[v] += x;
                peak[v] = std::max(peak[v], double(x));
                lit[v] += x > 1e-3f;
            }
        }
        stage_tier_source = wc::tier_by_radius;
        std::printf("AXIS_PLANE draws_plus=%u draws_minus=%u draws_zero=%u lit_plus=%u lit_minus=%u lit_zero=%u "
                    "peak_plus=%.4f peak_minus=%.4f peak_zero=%.4f sum_plus=%.2f sum_minus=%.2f sum_zero=%.2f\n",
                    draws[0], draws[1], draws[2], lit[0], lit[1], lit[2], peak[0], peak[1], peak[2], sum[0], sum[1], sum[2]);
        bool agree = draws[0] == draws[1] && draws[0] == draws[2] && lit[2] > 100;
        for (unsigned v = 0; v < 2; ++v)
            agree = agree && std::fabs(double(lit[v]) - double(lit[2])) <= 1e-3 * double(lit[2]) &&
                    std::fabs(sum[v] - sum[2]) <= 1e-3 * sum[2] && peak[v] <= peak[2] * 1.002 + 1e-3 &&
                    peak[v] >= peak[2] * 0.998 - 1e-3;
        report("bubble_axis_plane_no_flip_no_double_light", agree, "");
        draw_lane(g, 0.f, -1.f);
    }
    // The bubble's two sides (5.1.1 "Placement"). Chase: the camera behind and above a fighter (a 4 x 1.6 x 12 m hull,
    // 24 m ahead, pitched 0.175 rad nose down: placement from the CPU sweep of the review, the near side unlit, the far
    // side's cap past the outline in view), a hit on its nose: the patch lies on the bubble's far side from the eye (its
    // outward normal points away); the far side draws around the hull's outline and the hull lane hides what lies behind
    // the hull. Images with one side each (the other's gain 0) and with both (the default gains):
    // per side the lit pixels, those the twin hides (hidden_lit: must be under 0.5 %), the bubble within half the reach
    // of B visible (eligible, its coverage) and hidden by the hull (hidden: their share is the occlusion), the centroid
    // against B. Eye inside: a sphere bubble (the fallback, R 20 m, no hull walk) with the eye 10 m behind its centre,
    // inside it, the sky lane, a hit on its front pole: the near point lies behind the eye, so nothing draws with the far
    // side's gain 0 and the far side alone draws the patch; the default image is the far side's at bubble_far_gain.
    {
        rr::WeaponPatch patches[wc::max_patches];
        rr::WeaponDecal decals[wc::max_decals];
        rr::WeaponLook near_only = rr::default_weapon_look(), far_only = near_only;
        near_only.bubble_far_gain = 0.f;
        far_only.bubble_near_gain = 0.f;
        far_only.bubble_far_gain = 1.f;
        const float near_z = base_frame(g).near_z;
        struct Side {
            const char* name = "";
            unsigned patches = 0;
            SideMetric near_side, far_side;
            double both_luma = 0, far_luma = 0, cell = 1, cap_nose = 0; // the bubble past the hull's nose (view units)
        };
        auto measure = [&](Side& out, const HullLane* hull, const double hp[3], const ws::Tuning& t) {
            const ws::Counts n = ws::build(stage_ring, 1500, &fake_read, identity_camera(), t, patches, wc::max_patches,
                                           decals, wc::max_decals);
            out.patches = n.patches;
            if (!n.patches) return;
            out.cap_nose = double(patches[0].bubble_c[2] + patches[0].bubble_e[2]);
            const BubbleTwin bt = bubble_twin(patches[0]);
            double B[3], q[3];
            bubble_image(bt, hp, B, q);
            const double extent = patches[0].extent, core = 0.5 * double(patches[0].reach);
            out.cell = patches[0].cell;
            const std::vector<float> ln = render(pass, patches, n.patches, &near_only, nullptr, hull, hull == nullptr);
            const std::vector<float> lf = render(pass, patches, n.patches, &far_only, nullptr, hull, hull == nullptr);
            const std::vector<float> lb = render(pass, patches, n.patches, nullptr, nullptr, hull, hull == nullptr);
            out.near_side = side_metric(ln, bt, hull, B, extent, core, 0, near_z);
            out.far_side = side_metric(lf, bt, hull, B, extent, core, 1, near_z);
            out.both_luma = total(lb);
            out.far_luma = total(lf);
        };
        // chase: the nose hit (0, 0, 60) on the hull of half-extents (20, 8, 60)
        Side chase, inside;
        chase.name = "chase_nose_from_behind";
        inside.name = "eye_inside_bubble";
        stage_tier_source = wc::tier_by_ship_class;
        {
            // the owner 24 m ahead, pitched 0.175 rad nose down (the camera above and behind it: the bubble's nose cap
            // beyond the hull's outline, the rays over the nose top entering the bubble outside the footprint), its
            // object radius 1.1 x the half-length (the bubble's z cut to it)
            stage_owner(120.f, 66.f, 1000);
            const double pc = std::cos(0.175), ps = std::sin(0.175);
            const std::int32_t rows[3][3] = {{65536, 0, 0},
                                             {0, std::int32_t(pc * 65536.0), std::int32_t(ps * 65536.0)},
                                             {0, -std::int32_t(ps * 65536.0), std::int32_t(pc * 65536.0)}};
            for (unsigned r = 0; r < 3; ++r)
                for (unsigned j = 0; j < 3; ++j) put32(fake_node, wc::layout::node_rot + 16 * r + 4 * j, std::uint32_t(rows[r][j]));
            const std::int32_t nose[3] = {0, 0, 6000};
            for (unsigned k = 0; k < 3; ++k) stage_ring.slots[0].pos[k] = nose[k];
            const double ext[3] = {20.0, 8.0, 60.0}, hp[3] = {0.0, 0.0, 60.0};
            stage_hull(ext);
            HullLane hull = staged_hull(ext);
            measure(chase, &hull, hp, hull_tuning());
            chase.cap_nose -= ext[2];
        }
        // eye inside: the fallback sphere R 100 view units about view z 50 (the eye 50 behind its centre), the hit on its
        // front pole (0, 0, 100), the sky lane
        {
            stage_owner(50.f, 100.f, 1000);
            stage_ring.slots[0].pos[2] = 10000;
            const double hp[3] = {0.0, 0.0, 100.0};
            draw_lane(g, 0.f, -1.f);
            measure(inside, nullptr, hp, ws::Tuning{});
        }
        stage_tier_source = wc::tier_by_radius;
        for (const Side* s : {&chase, &inside}) {
            for (int k = 0; k < 2; ++k) {
                const SideMetric& m = k ? s->far_side : s->near_side;
                std::printf("BUBBLE_SIDE case=%s side=%s patches=%u lit_pixels=%u hidden_lit=%u sky_lit=%u outside_lit=%u "
                            "eligible=%u covered=%u coverage=%.3f hidden=%u hidden_share=%.3f centroid_error_cells=%.3f "
                            "luma=%.3f\n",
                            s->name, k ? "far" : "near", s->patches, m.lit, m.hidden_lit, m.sky_lit, m.outside, m.eligible,
                            m.covered, m.eligible ? double(m.covered) / double(m.eligible) : 0.0, m.hidden,
                            m.eligible + m.hidden ? double(m.hidden) / double(m.eligible + m.hidden) : 0.0,
                            m.centroid / s->cell, m.luma);
            }
            std::printf("BUBBLE_SIDE case=%s side=both luma=%.3f far_luma=%.3f ratio=%.4f far_gain=%.2f cap_nose_m=%.2f\n",
                        s->name, s->both_luma, s->far_luma, s->far_luma > 0 ? s->both_luma / s->far_luma : 0.0,
                        double(wc::bubble_far_gain), s->cap_nose / 5.0);
        }
        const SideMetric& cf = chase.far_side;
        report("bubble_chase_far_side_around_the_outline",
               chase.near_side.lit == 0 && cf.lit >= 500 && cf.hidden_lit * 200 <= cf.lit && cf.hidden > 0 &&
                   chase.cap_nose >= 0.1 * 60.0 &&
                   cf.outside * 100 <= cf.lit &&
                   std::fabs(chase.both_luma - double(wc::bubble_far_gain) * chase.far_luma) <= 0.01 * chase.both_luma, "");
        const SideMetric& in = inside.far_side;
        report("bubble_eye_inside_far_side_only",
               inside.near_side.lit == 0 && in.lit > 100 && in.hidden_lit * 200 <= in.lit && in.eligible > 0 &&
                   double(in.covered) >= 0.9 * double(in.eligible) && in.centroid <= inside.cell &&
                   std::fabs(inside.both_luma - double(wc::bubble_far_gain) * inside.far_luma) <= 0.01 * inside.both_luma,
               "");
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
        stage_tier_source = wc::tier_by_radius;
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
            const double sphere[3] = {80.0, 80.0, 80.0}; // the hull inside the fallback bubble (the radius, 100)
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
                       std::vector<float>* luma, double L[3][3], unsigned* eye_patches, unsigned* outside) {
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
            stage_hull(ext);
            stage_tier_source = wc::tier_by_ship_class;
            for (unsigned h = 0; h < 8; ++h)
                if (mask & (1u << h)) add_stage_hit(1000 - 10 * h, sets[set][h], wc::patch_corvette);
            stage_tier_source = wc::tier_by_radius;
            ws::Tuning t = hull_tuning();
            t.own_node = own;
            t.cockpit = cockpit_view;
            ws::Camera cam = identity_camera();
            cam.m00 = m00;
            cam.m11 = m11;
            rr::WeaponPatch patches[wc::max_patches];
            rr::WeaponDecal decals[wc::max_decals];
            const ws::Counts n = ws::build(stage_ring, 1500, &fake_read, cam, t, patches, wc::max_patches, decals,
                                           wc::max_decals);
            ws::dim_own_cockpit(patches, n.patches, own, cockpit_view);
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
        tt.release();
        stage_owner(400.f, 100.f, 0); // the staged owner back at view (0, 0, 400 m), radius 100, an empty ring
        wc::ring_clear(stage_ring);
        draw_lane(g, 0.f, -1.f);
    }
    // 12 hits per second on one owner through the production builder, frames of 1/60 s game time for 4 s (the cell
    // cells in steady state, older hits' cells fading on their own clocks), the uniform-level look: between consecutive
    // frames no pixel drops by more than one frame's fade step per lit cell over it (the fastest cell: cell_gain (1 +
    // stagger / 2) dt / (fade (1 - stagger / 2)); the cells over a pixel counted by the same draws at a constant level)
    // plus the FP16 step. The hits land on a flat face of the owner (its front at 0.6 R, square to
    // the -z axis: one region, one depth) and on the front of a sphere of radius R (every hit at its own depth), with
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
            // The surface: the flat face (a plane square to the view at 0.6 R in front of the centre) or the sphere.
            if (flat)
                draw_lane(g, owner_z - 0.6f * owner_r, 1.f);
            else { // the hull inside the fallback bubble (the radius): the bubble draws on its own sphere
                const double r3[3] = {0.8 * owner_r, 0.8 * owner_r, 0.8 * owner_r};
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
        stage_tier_source = wc::tier_by_radius;
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
            const double sphere[3] = {0.8 * r, 0.8 * r, 0.8 * r}; // the hull inside the fallback bubble
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
    // The stage's CPU cost in this i686 build (SSE2, the production flags): ws::build alone with the depth cache (the
    // production path), best and mean of 200 calls,
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
                        name, n.owners, n.hits, n.patches, cells, n.spills, n.cells_dropped, n.dropped, best,
                        sum / 200.0);
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
        report("stage_cpu_measured", one > 0.0 && many > 0.0 && station > 0.0, "");
    }
    // The hull walk's cost (5.1.1 "Placement"; the per-owner cache): the production stage::hull_bounds over the fake body
    // cache with 127 parts (the walk's budget is 128 nodes), and the builder's frame for one capital with 64 spread hits
    // with the cache warm (the owner's bounds reused: the production steady state) and cold (rescoped before every call:
    // a walk in every frame, what the cache saves), best and mean of 200 calls (us).
    {
        stage_owner(4000.f, 2000.f, 0);
        wc::ring_clear(stage_ring);
        for (unsigned i = 0; i < wc::owner_hits; ++i) {
            const double th = 0.61 * std::sqrt(halton(i + 1, 2)), a = 6.2831853 * halton(i + 1, 3), re = 200000.0;
            const std::int32_t pos[3] = {std::int32_t(re * std::sin(th) * std::cos(a)),
                                         std::int32_t(re * std::sin(th) * std::sin(a)), -std::int32_t(re * std::cos(th))};
            add_stage_hit(1000 + 40 * i, pos, wc::patch_capital);
        }
        const double ext[3] = {600.0, 400.0, 2000.0};
        stage_hull(ext, nullptr, 127);
        LARGE_INTEGER freq, a, b;
        QueryPerformanceFrequency(&freq);
        ws::HullBox box;
        bool ok = true;
        double walk_best = 1e30, walk_sum = 0;
        for (unsigned rep = 0; rep < 200; ++rep) {
            QueryPerformanceCounter(&a);
            ok = ws::hull_bounds(&fake_read, addr(fake_hull), addr(fake_node), &box) && ok;
            QueryPerformanceCounter(&b);
            walk_best = std::min(walk_best, ticks_us(a, b, freq));
            walk_sum += ticks_us(a, b, freq);
        }
        // The production reader (engine_memory::read, src/proxy/engine_memory.cpp built as production builds it, without
        // SSE) over the same fixture blocks: cold, the region cache invalidated before each walk (next_frame: the walk's
        // first frame), and warm (the regions validated by the previous walk of the frame); the reads one walk makes.
        static unsigned walk_reads = 0;
        auto counted = [](std::uintptr_t at, void* out, std::size_t n) {
            ++walk_reads;
            return fake_read(at, out, n);
        };
        walk_reads = 0;
        ws::hull_bounds(counted, addr(fake_hull), addr(fake_node), &box);
        const unsigned reads = walk_reads;
        auto production = [](std::uintptr_t at, void* out, std::size_t n) { return x3m::engine_memory::read(at, out, n); };
        double em_best[2] = {1e30, 1e30}, em_sum[2] = {0, 0};
        bool em_ok = true;
        for (unsigned warm = 0; warm < 2; ++warm)
            for (unsigned rep = 0; rep < 200; ++rep) {
                if (!warm) x3m::engine_memory::next_frame();
                QueryPerformanceCounter(&a);
                em_ok = ws::hull_bounds(production, addr(fake_hull), addr(fake_node), &box) && em_ok;
                QueryPerformanceCounter(&b);
                em_best[warm] = std::min(em_best[warm], ticks_us(a, b, freq));
                em_sum[warm] += ticks_us(a, b, freq);
            }
        static rr::WeaponPatch patches[wc::max_patches];
        static rr::WeaponDecal decals[wc::max_decals];
        static ws::BubbleCache cache;
        double best[2] = {1e30, 1e30}, sum[2] = {0, 0};
        ws::Counts n{};
        for (unsigned cold = 0; cold < 2; ++cold) {
            cache.rescope(100 + cold);
            for (unsigned rep = 0; rep < 200; ++rep) {
                if (cold) cache.rescope(1000 + rep);
                QueryPerformanceCounter(&a);
                n = ws::build(stage_ring, 1000 + 40 * wc::owner_hits, &fake_read, identity_camera(), hull_tuning(), patches,
                              wc::max_patches, decals, wc::max_decals, &cache);
                QueryPerformanceCounter(&b);
                best[cold] = std::min(best[cold], ticks_us(a, b, freq));
                sum[cold] += ticks_us(a, b, freq);
            }
        }
        std::printf("HULL_WALK parts=%u nodes=%u ok=%u reads=%u walk_best_us=%.1f walk_mean_us=%.1f "
                    "engine_memory_ok=%u engine_memory_cold_best_us=%.1f engine_memory_cold_mean_us=%.1f "
                    "engine_memory_warm_best_us=%.1f engine_memory_warm_mean_us=%.1f warm_best_us=%.1f "
                    "warm_mean_us=%.1f cold_best_us=%.1f cold_mean_us=%.1f patches=%u bubbles=%u bubble_fallback=%u "
                    "cache_walks=%u cache_hits=%u\n",
                    box.parts, box.nodes, unsigned(ok), reads, walk_best, walk_sum / 200.0, unsigned(em_ok), em_best[0],
                    em_sum[0] / 200.0, em_best[1], em_sum[1] / 200.0, best[0], sum[0] / 200.0, best[1], sum[1] / 200.0,
                    n.patches, n.bubbles, n.bubble_fallback, cache.walks, cache.hits);
        report("hull_walk_measured", ok && em_ok && box.parts == 127 && box.nodes == 127 && n.bubbles == 1 &&
                                         n.bubble_fallback == 0 && cache.hits >= 199, "");
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
        const double sphere[3] = {80.0, 80.0, 80.0}; // the hull inside the fallback bubble
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
        report("stage_build_live_owner_draws", live.patches >= 1 && live.bubbles == 1 && drawn.second.drew && live_sum > 1.0, "");
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
        const double spread_r[3] = {0.87 * 2000.0, 0.87 * 2000.0, 0.87 * 2000.0};
        const HullLane spread_hull = staged_hull(spread_r); // the capital's hull inside its bubble (the fallback sphere)
        // The ship bubble's cases (5.1.1 "Placement"): the same hits on a facing owner's bubble (a sphere of the scene's
        // surface radius, the hull inside it at 0.87 of it as the lane), through the production fill_bubble; the
        // capital spread above is a bubble through the builder.
        rr::WeaponPatch bubble_fighter[8], bubble_capital[8], bubble_worst[32];
        const unsigned n_bf = bubble_patches(fighter_far, {{-cf, 0.f, 0.2f}, {cf, 0.f, 0.5f}}, bubble_fighter, 8);
        const unsigned n_bc = bubble_patches(capital_close, {{-1.5f * cc, 0.f, 0.2f}, {1.5f * cc, 0.f, 0.5f}}, bubble_capital, 8);
        unsigned n_bw = 0;
        for (unsigned k = 0; k < 4; ++k)
            n_bw += bubble_patches(capital_close, {{(k & 1 ? 1.5f : -1.5f) * cc, (k & 2 ? 1.5f : -1.5f) * cc, 0.1f + 0.1f * k}},
                                   bubble_worst + n_bw, 32 - n_bw);
        const HullLane fighter_hull = facing_hull(fighter_far, 0.87), capital_hull = facing_hull(capital_close, 0.87);
        // The eye inside the bubble (review F6), through the builder on the sky lane: the placement case (the fallback
        // sphere R 20 m, the eye 10 m behind its centre, the hit on its front pole), and the own ship in the cockpit view
        // (the eye 1 m inside the bubble's back, eight hits around it, two behind the eye; the draws dimmed as the glue
        // dims them).
        rr::WeaponPatch inside_front[wc::max_patches], cockpit[wc::max_patches];
        unsigned n_inside = 0, n_cockpit = 0;
        {
            rr::WeaponDecal decals[wc::max_decals];
            stage_owner(50.f, 100.f, 1000);
            stage_ring.slots[0].pos[2] = 10000;
            n_inside = ws::build(stage_ring, 1500, &fake_read, identity_camera(), ws::Tuning{}, inside_front,
                                 wc::max_patches, decals, wc::max_decals).patches;
            stage_owner(95.f, 100.f, 1000);
            wc::ring_clear(stage_ring);
            const std::int32_t around[8][3] = {{0, 0, 10000}, {7071, 0, 7071}, {-7071, 0, 7071}, {0, 7071, 7071},
                                               {10000, 0, 0}, {0, -10000, 0}, {0, 2000, -9798}, {3000, 0, -9539}};
            for (unsigned k = 0; k < 8; ++k) add_stage_hit(1000 + 40 * k, around[k]);
            n_cockpit = ws::build(stage_ring, 1400, &fake_read, identity_camera(), ws::Tuning{}, cockpit, wc::max_patches,
                                  decals, wc::max_decals).patches;
            ws::dim_own_cockpit(cockpit, n_cockpit, addr(fake_node), true);
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
        } cases[] = {{"fence", nullptr, 0, nullptr},
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
            if (c.sky)
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

    // ---- projectiles (docs/architecture/projectile-look.md "Implementation (2026-11-08)"): the production stage builder
    // and run_projectiles on a pass attached with the projectile programs (the shield pass above keeps its object set).
    guard_next_section("projectiles");
    {
        rr::WeaponEffectsPass refused;
        refused.configure_projectiles(true);
        refused.set_faults(16);
        refused.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format);
        report("projectile_caps_refusal_keeps_shields", refused.caps().enabled && !refused.caps().projectile_enabled &&
                                                            !refused.projectiles_ready(),
               refused.caps().projectile_reason);
        refused.detach();
    }
    rr::WeaponEffectsPass proj;
    proj.configure_projectiles(true);
    proj.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format);
    const rr::WeaponEffectsCaps& pc = proj.caps();
    std::printf("PROJECTILE attach=%u reason=%s vs_slots=%u ps_slots=%u lane32_blending=%u lane16_blending=%u mrt_bit_depths=%u "
                "num_rts=%lu\n",
                unsigned(pc.projectile_enabled), pc.projectile_reason, pc.projectile_vs_slots, pc.projectile_ps_slots,
                unsigned(pc.lane32_blending), unsigned(pc.lane16_blending), unsigned(pc.mrt_bit_depths),
                static_cast<unsigned long>(caps.NumSimultaneousRTs));
    report("projectile_attach", pc.enabled && pc.projectile_enabled && proj.projectiles_ready(), pc.projectile_reason);
    report("projectile_program_under_200_slots", pc.projectile_ps_slots > 0 && pc.projectile_ps_slots < 200 &&
                                                     pc.projectile_vs_slots > 0 && pc.projectile_vs_slots < 200, "");
    ProjTarget pt;
    report("projectile_target_1080", make_proj_target(pt, 1920, 1080));
    if (pc.projectile_enabled && pt.depth_s.p) {
        const ps::BodyLook look = ire_look();
        // The mechanism cases below run on the FL 60 A starting values with the comet on (trail_ms 40), the values their
        // thresholds were written against; the family's baked tune (FL 61 A: core 1 px, the comet off) is case (1b).
        const pf::Tune comet = {2.f, 3.f, 0.2f, 0.3f, 3000.f, 60.f, 2.f, 40.f, 1.5f};
        pf::Tune core_only = comet;
        core_only.glow_gain = 0.f; // the core's own width
        // the length and energy rules of the streak are the flat streak's (trail_ms 0, FL run 60's law); the comet's
        // falloff, head and ring are the cases (7f) to (7h)
        pf::Tune flat_core = core_only;
        flat_core.trail_ms = 0.f;
        pf::Tune flat = comet;
        flat.trail_ms = 0.f;
        // (1) the core width rule at 200 m / 500 m / 1 km / 3 km, at m00 1.6 and a second FOV (m00 1.0): the measured
        // full width at half maximum across the axis equals max(1, core_px 500 / d) within 0.5 px at both, and the
        // stretch's pixel length follows the live focal length (the ratio of the two m00).
        bool widths_ok = true, fov_ok = true;
        const int cx = int(pt.w / 2), cy = int(pt.h / 2);
        for (float m00 : {1.6f, 1.0f})
            for (float d : {200.f, 500.f, 1000.f, 3000.f}) {
                const ps::View view = proj_view(pt, m00);
                rr::WeaponProjectileVertex v[4];
                ps::BoltShape shape;
                const bool built = ps::bolt_vertices(view, core_only, look, bolt_at(d, 1162.f, 16.f), v, &shape);
                proj_draw(proj, pt, v, built ? 1u : 0u);
                const std::vector<float> l = proj_scene(pt);
                const double measured = fwhm_through(l, pt.w, pt.h, cx, cy, 0, 1);
                const double rule = std::max(1.0, double(core_only.core_px) * 500.0 / double(d));
                const bool ok = built && std::fabs(measured - rule) <= 0.5;
                widths_ok = widths_ok && ok;
                ps::BoltShape other;
                rr::WeaponProjectileVertex v2[4];
                ps::bolt_vertices(proj_view(pt, m00 == 1.6f ? 1.0f : 1.6f), core_only, look, bolt_at(d, 1162.f, 16.f), v2, &other);
                const double ratio = other.length_px > 0.f ? double(shape.length_px) / double(other.length_px) : 0.0;
                const double want = m00 == 1.6f ? 1.6 : 1.0 / 1.6;
                fov_ok = fov_ok && std::fabs(ratio / want - 1.0) < 0.01;
                std::printf("PROJECTILE case=width m00=%.2f distance_m=%.0f rule_px=%.3f measured_px=%.3f drawn_px=%.3f dim=%.3f "
                            "length_px=%.2f streak_m=%.2f\n",
                            double(m00), double(d), rule, measured, double(shape.core_px), double(shape.dim),
                            double(shape.length_px), double(shape.streak_m));
            }
        report("projectile_core_width_rule_both_fovs", widths_ok, "");
        report("projectile_length_follows_live_projection", fov_ok, "");
        // (1b) the baked tune (FL 61 A, 2026-11-09): the family's constants are the user's accepted file values, and at
        // 200 m / 500 m / 1 km it draws the flat streak (one quad, no trail, head gain 1) whose core (glow off) is
        // max(1, 1.0 x 500 / d) px wide within 0.5 px, and whose full look (glow on) is drawn at the centre.
        {
            const pf::Tune& baked = pf::families[pf::fighter_tracer].tune;
            const pf::Tune fl61a = {1.f, 2.f, 0.3f, 0.4f, 3000.f, 60.f, 2.f, 0.f, 1.5f};
            bool same = true;
            for (unsigned k = 0; k < pf::tune_keys; ++k) {
                pf::Tune a = baked, b = fl61a;
                same = same && pf::tune_at(a, k) == pf::tune_at(b, k);
            }
            pf::Tune baked_core = baked;
            baked_core.glow_gain = 0.f;
            bool ok_all = same;
            for (float d : {200.f, 500.f, 1000.f}) {
                const ps::View view = proj_view(pt, 1.6f);
                rr::WeaponProjectileVertex v[4];
                ps::BoltShape shape, full;
                const bool built = ps::bolt_vertices(view, baked_core, look, bolt_at(d, 1162.f, 16.f), v, &shape);
                proj_draw(proj, pt, v, built ? 1u : 0u);
                const double measured = fwhm_through(proj_scene(pt), pt.w, pt.h, cx, cy, 0, 1);
                const double rule = std::max(1.0, double(baked.core_px) * 500.0 / double(d));
                const bool built_full = ps::bolt_vertices(view, baked, look, bolt_at(d, 1162.f, 16.f), v, &full);
                proj_draw(proj, pt, v, built_full ? 1u : 0u);
                const std::vector<float> l = proj_scene(pt);
                const float centre = l.empty() ? 0.f : l[std::size_t(cy) * pt.w + cx];
                const bool ok = built && built_full && std::fabs(measured - rule) <= 0.5 && full.quads == 1u &&
                                full.trail_quads == 0u && full.head_gain == 1.f && centre > 1.f;
                ok_all = ok_all && ok;
                std::printf("PROJECTILE case=baked_fl61a distance_m=%.0f rule_px=%.3f measured_px=%.3f quads=%u ring=%u "
                            "head_gain=%.3f centre=%.4f energy=%.4f same=%u\n",
                            double(d), rule, measured, full.quads, full.trail_quads, double(full.head_gain), double(centre),
                            double(full.energy), unsigned(same));
            }
            report("projectile_baked_fl61a_flat_streak", ok_all, "");
        }
        // (2) SETA invariance and the bridged rule from the constants: at 1 km, first-frame steps 16 ms (1x) and 96 ms
        // (6x), the speed chosen so the 6x streak is 80 % of length_cap_m: the drawn length per game ms (the half-maximum
        // run along the axis; the end ramps' half maximum sits at the ends) is the same within 3 % and equals |v| step f /
        // d within 3 %. Past the cap (the IRE's 1162 m/s at 96 ms) the streak keeps its full length |v| step and its
        // radiance scales by cap / length (energy conserved): the run within 3 % of the rule and the centre's radiance
        // over an uncapped bolt's (16 ms) within 3 % of cap / length.
        double per_ms[2] = {0, 0};
        bool rule_ok = true;
        const float f_px = proj_view(pt, 1.6f).m11 * 0.5f * float(pt.h);
        const float seta_speed = 0.8f * flat_core.length_cap_m / 0.096f;
        for (unsigned k = 0; k < 2; ++k) {
            const float step = k ? 96.f : 16.f;
            rr::WeaponProjectileVertex v[4];
            ps::BoltShape shape;
            const bool built = ps::bolt_vertices(proj_view(pt, 1.6f), flat_core, look, bolt_at(1000.f, seta_speed, step), v, &shape);
            proj_draw(proj, pt, v, built ? 1u : 0u);
            const std::vector<float> l = proj_scene(pt);
            const double run = fwhm_through(l, pt.w, pt.h, cx, cy, 1, 0);
            per_ms[k] = run / double(step);
            const double rule_px = double(seta_speed) * step * 1e-3 * f_px / 1000.0;
            rule_ok = rule_ok && std::fabs(run / rule_px - 1.0) < 0.03 && shape.energy == 1.f;
            std::printf("PROJECTILE case=seta step_ms=%.0f speed_mps=%.1f streak_m=%.2f rule_px=%.2f length_px=%.2f run_px=%.2f "
                        "px_per_game_ms=%.4f energy=%.4f\n",
                        double(step), double(seta_speed), double(shape.streak_m), rule_px, double(shape.length_px), run,
                        per_ms[k], double(shape.energy));
        }
        {
            float centre[2] = {0.f, 0.f};
            double runs[2] = {0.0, 0.0};
            ps::BoltShape shapes[2];
            for (unsigned k = 0; k < 2; ++k) {
                rr::WeaponProjectileVertex v[4];
                const bool built = ps::bolt_vertices(proj_view(pt, 1.6f), flat_core, look, bolt_at(1000.f, 1162.f, k ? 96.f : 16.f),
                                                     v, &shapes[k]);
                proj_draw(proj, pt, v, built ? 1u : 0u);
                const std::vector<float> l = proj_scene(pt);
                centre[k] = l.empty() ? 0.f : l[std::size_t(cy) * pt.w + cx];
                runs[k] = fwhm_through(l, pt.w, pt.h, cx, cy, 1, 0);
            }
            const double length_m = 1162.0 * 0.096, want_energy = double(flat_core.length_cap_m) / length_m;
            const double rule_px = length_m * f_px / 1000.0;
            const double ratio = centre[0] > 0.f ? double(centre[1]) / double(centre[0]) : 0.0;
            rule_ok = rule_ok && std::fabs(shapes[1].streak_m / length_m - 1.0) < 0.01 &&
                      std::fabs(shapes[1].energy / want_energy - 1.0) < 0.01 && shapes[0].energy == 1.f &&
                      std::fabs(runs[1] / rule_px - 1.0) < 0.03 && std::fabs(ratio / want_energy - 1.0) < 0.03;
            std::printf("PROJECTILE case=cap step_ms=96 speed_mps=1162 streak_m=%.2f cap_m=%.1f energy=%.4f want_energy=%.4f "
                        "rule_px=%.2f run_px=%.2f centre_ratio=%.4f\n",
                        double(shapes[1].streak_m), double(flat_core.length_cap_m), double(shapes[1].energy), want_energy,
                        rule_px, runs[1], ratio);
        }
        report("projectile_seta_invariant_streak",
               per_ms[0] > 0.0 && std::fabs(per_ms[1] / per_ms[0] - 1.0) < 0.03 && rule_ok, "");
        // (3) the far flag: the lane's green lane carries 32 max(rgb) on the bolt (> 1 at its core), the other lanes and
        // every pixel away from it keep the cleared 0 (COLORWRITEENABLE1 = GREEN).
        {
            rr::WeaponProjectileVertex v[4];
            const bool built = ps::bolt_vertices(proj_view(pt, 1.6f), comet, look,
                                                 bolt_at(500.f, 1162.f, 16.f), v);
            proj_draw(proj, pt, v, built ? 1u : 0u);
            const std::vector<float> lane = proj_lane(pt);
            const std::vector<float> l = proj_scene(pt);
            bool others_zero = !lane.empty(), away_zero = !lane.empty();
            for (std::size_t i = 0; i < lane.size() / 4; ++i) {
                others_zero = others_zero && lane[4 * i] == 0.f && lane[4 * i + 2] == 0.f && lane[4 * i + 3] == 0.f;
                if (!l.empty() && l[i] == 0.f) away_zero = away_zero && lane[4 * i + 1] < 1e-3f;
            }
            const float g = lane.empty() ? 0.f : lane[(std::size_t(cy) * pt.w + cx) * 4 + 1];
            const float c = l.empty() ? 0.f : l[std::size_t(cy) * pt.w + cx];
            std::printf("PROJECTILE case=far_flag lane_g=%.3f scene_max=%.3f ratio=%.3f\n", double(g), double(c),
                        c > 0.f ? double(g / c) : 0.0);
            report("projectile_far_flag_written", g > 1.f && std::fabs(g / c - 32.f) < 0.5f && away_zero, "");
            report("projectile_far_flag_green_lane_only", others_zero, "");
        }
        // (4) the dot LOD: past lod_dot_m no streak (a spot of the core's width at the head), inside it the streak; the
        // spot keeps the streak's energy factor (at 96 ms both are past the cap: the same cap / length, no pop).
        {
            pf::Tune t = flat_core;
            double runs[2] = {0, 0};
            float energies[2] = {0.f, 0.f};
            bool dots[2] = {false, false};
            for (unsigned k = 0; k < 2; ++k) {
                const float d = k ? t.lod_dot_m * 1.1f : t.lod_dot_m * 0.9f;
                rr::WeaponProjectileVertex v[4];
                ps::BoltShape shape;
                ps::BoltIn in = bolt_at(d, 1162.f, 96.f);
                if (k) in.p[0] = in.p[1] = 0.f; // the dot sits at the head: on the centre
                const bool built = ps::bolt_vertices(proj_view(pt, 1.6f), t, look, in, v, &shape);
                proj_draw(proj, pt, v, built ? 1u : 0u);
                runs[k] = fwhm_through(proj_scene(pt), pt.w, pt.h, cx, cy, 1, 0);
                dots[k] = shape.dot && shape.drawn_px == shape.core_px;
                energies[k] = shape.energy;
                std::printf("PROJECTILE case=lod distance_m=%.0f dot=%u length_px=%.2f drawn_px=%.2f run_px=%.2f dim=%.3f "
                            "energy=%.4f\n",
                            double(d), unsigned(shape.dot), double(shape.length_px), double(shape.drawn_px), runs[k],
                            double(shape.dim), double(shape.energy));
            }
            // inside: the bridged rule's length (|v| step f / d, past the cap at full length) less a pixel of tolerance
            const double inside_px = double(1162.f * 0.096f) *
                                     double(proj_view(pt, 1.6f).m11 * 0.5f * float(pt.h)) / (double(t.lod_dot_m) * 0.9);
            const double want_energy = double(t.length_cap_m) / (1162.0 * 0.096);
            // the spot is a segment of the core's width (1 px here) with the logistic ramps: its half-maximum run along
            // the axis is under 2 core widths + 0.5 px (sampled on the pixel grid)
            report("projectile_dot_past_lod", dots[1] && !dots[0] && runs[1] <= 2.5 && runs[0] >= inside_px - 1.0 &&
                                                  std::fabs(energies[0] / want_energy - 1.0) < 0.01 &&
                                                  std::fabs(energies[1] / want_energy - 1.0) < 0.01, "");
        }
        // (5) nothing outside the bolt's quad: a diagonal streak with the glow; every lit pixel lies inside the
        // quad's four raster corners (1 px tolerance).
        {
            rr::WeaponProjectileVertex v[4];
            ps::BoltShape shape;
            const bool built = ps::bolt_vertices(proj_view(pt, 1.6f), comet, look,
                                                 bolt_at(300.f, 1162.f, 50.f, 0.6f), v, &shape);
            proj_draw(proj, pt, v, built ? 1u : 0u);
            const std::vector<float> l = proj_scene(pt);
            // corners 0, 1, 3, 2 in order around the quad
            const int order[4] = {0, 1, 3, 2};
            unsigned lit = 0, outside = 0;
            for (UINT y = 0; y < pt.h && !l.empty(); ++y)
                for (UINT x = 0; x < pt.w; ++x) {
                    if (!(l[std::size_t(y) * pt.w + x] > 0.f)) continue;
                    ++lit;
                    bool in = true;
                    double sign = 0.0;
                    for (int e = 0; e < 4; ++e) {
                        const float* a = shape.raster[order[e]];
                        const float* b = shape.raster[order[(e + 1) % 4]];
                        const double ex = b[0] - a[0], ey = b[1] - a[1], len = std::sqrt(ex * ex + ey * ey);
                        const double cross = (ex * (double(y) - a[1]) - ey * (double(x) - a[0])) / (len > 0 ? len : 1);
                        if (sign == 0.0) sign = cross >= 0 ? 1.0 : -1.0;
                        if (cross * sign < -1.0) in = false;
                    }
                    outside += !in;
                }
            std::printf("PROJECTILE case=quad lit=%u outside=%u length_px=%.2f ext_px=%.2f\n", lit, outside,
                        double(shape.length_px), double(shape.ext_px));
            report("projectile_no_pixels_outside_quad", built && lit > 50 && outside == 0, "");
        }
        // (6) the scene's depth occludes (LESSEQUAL against the depth surface, Z writes off): the depth cleared to the
        // projection's value at 400 m, a bolt at 300 m (left of centre) is drawn and one at 500 m (right) is hidden, so a
        // wrong depth law or ordering fails.
        {
            const ps::View view = proj_view(pt, 1.6f);
            rr::WeaponProjectileVertex v[8];
            ps::BoltIn near_in = bolt_at(300.f, 1162.f, 16.f), far_in = bolt_at(500.f, 1162.f, 16.f);
            near_in.p[0] += -0.3f * near_in.p[2] / view.m00; // the streak's centre at ndc x -0.3
            far_in.p[0] += 0.3f * far_in.p[2] / view.m00;    // ndc x +0.3
            const bool built = ps::bolt_vertices(view, comet, look, near_in, v) &&
                               ps::bolt_vertices(view, comet, look, far_in, v + 4);
            const float z400 = view.m22 + view.m32 / (400.f * ps::view_units_per_metre);
            rr::WeaponEffectsReport r{};
            const HRESULT hr = proj_draw(proj, pt, v, 2, z400, &r);
            const std::vector<float> l = proj_scene(pt);
            const int xn = int(0.35f * float(pt.w)), xf = int(0.65f * float(pt.w)); // ndc -0.3 / +0.3
            double near_sum = 0.0, far_sum = 0.0;
            for (UINT y = 0; y < pt.h && !l.empty(); ++y)
                for (UINT x = 0; x < pt.w; ++x) (int(x) < int(pt.w / 2) ? near_sum : far_sum) += l[std::size_t(y) * pt.w + x];
            const float near_c = l.empty() ? 0.f : l[std::size_t(cy) * pt.w + xn];
            const float far_c = l.empty() ? 0.f : l[std::size_t(cy) * pt.w + xf];
            std::printf("PROJECTILE case=depth z400=%.7f near_centre=%.3f far_centre=%.3f near_sum=%.1f far_sum=%.1f\n",
                        double(z400), double(near_c), double(far_c), near_sum, far_sum);
            report("projectile_depth_occludes",
                   built && hr == S_OK && r.quads == 2 && near_c > 1.f && far_c == 0.f && near_sum > 0.0 && far_sum == 0.0, "");
            // a refused frame: no depth surface
            rr::WeaponProjectileFrame f;
            f.width = pt.w;
            f.height = pt.h;
            f.vertices = v;
            f.quads = 1;
            f.lane = pt.lane_s.p;
            rr::WeaponEffectsReport rr2{};
            dev->BeginScene();
            const HRESULT no_depth = proj.run_projectiles(f, &rr2);
            dev->EndScene();
            report("projectile_refuses_without_depth", FAILED(no_depth) && rr2.refused && !std::strcmp(rr2.refused, "depth"), "");
        }
        // (7) the pool through the production builder and engine reader: a bullet registered by the object tap's
        // projectile half is drawn; recycled (handle changed), not tapped for longer than stale_ms (the game clock the
        // tap stamps), or expired, it is retired and drawn no more.
        {
            static wc::ProjectilePool pool;
            wc::projectile_clear(pool);
            alignas(16) static unsigned char cfg[0x800];
            static std::uint32_t cfg_slot;
            cfg_slot = std::uint32_t(reinterpret_cast<std::uintptr_t>(cfg));
            pool.globals.cfg_slot = reinterpret_cast<std::uintptr_t>(&cfg_slot);
            const auto clock = [&](std::uint32_t ms) { put32(cfg, wc::layout::cfg_game_ms, ms); };
            clock(1000);
            pool.armed = 1;
            pool.resolved = 1;
            pool.body_count = 1;
            pool.bodies[0].id = 0x55;
            pool.bits[0x55 >> 5] |= 1u << (0x55 & 31u);
            alignas(16) static unsigned char obj[0x100], node[0x200], ext[0x80];
            std::memset(obj, 0, sizeof obj);
            std::memset(node, 0, sizeof node);
            std::memset(ext, 0, sizeof ext);
            put32(obj, wc::layout::obj_node, std::uint32_t(reinterpret_cast<std::uintptr_t>(node)));
            put32(obj, wc::layout::obj_ext, std::uint32_t(reinterpret_cast<std::uintptr_t>(ext)));
            put32(node, wc::layout::node_live, 1);
            put32(node, wc::layout::node_handle, 9);
            put32(node, wc::layout::node_body, 0x55);
            put32(node, wc::layout::node_pos + 8, std::uint32_t(500 * 500)); // 500 m ahead (engine units)
            put32(node, wc::projectile::node_velocity, std::uint32_t(581000));
            put32(ext, wc::projectile::ext_expiry, 2000);
            put32(ext, wc::projectile::ext_created, 800);
            wc::projectile_tap(reinterpret_cast<std::uintptr_t>(obj), pool);
            static ps::Looks looks;
            looks.resolved = 0;
            static rr::WeaponProjectileVertex verts[rr::weapon_projectile_max_quads * 4];
            const pf::Tune tunes[pf::family_count] = {comet};
            const ps::Counts a = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, 1, false, 1000, 16, 0.f, 1.f,
                                           proj_view(pt, 1.6f), tunes, verts, rr::weapon_projectile_max_bolts,
                                           rr::weapon_projectile_max_quads);
            put32(node, wc::layout::node_handle, 10); // the node recycled
            const ps::Counts b = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, 2, false, 1016, 16, 0.f, 1.f,
                                           proj_view(pt, 1.6f), tunes, verts, rr::weapon_projectile_max_bolts,
                                           rr::weapon_projectile_max_quads);
            put32(node, wc::layout::node_handle, 9);
            clock(1016);
            wc::projectile_tap(reinterpret_cast<std::uintptr_t>(obj), pool);
            const ps::Counts c = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, 3, false, 1016 + wc::projectile::stale_ms, 16,
                                           0.f, 1.f, proj_view(pt, 1.6f), tunes, verts, rr::weapon_projectile_max_bolts,
                                           rr::weapon_projectile_max_quads);
            const ps::Counts d = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, 4, false, 1017 + wc::projectile::stale_ms, 16,
                                           0.f, 1.f, proj_view(pt, 1.6f), tunes, verts, rr::weapon_projectile_max_bolts,
                                           rr::weapon_projectile_max_quads);
            clock(1990);
            wc::projectile_tap(reinterpret_cast<std::uintptr_t>(obj), pool);
            const ps::Counts e = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, 5, false, 2000, 16, 0.f, 1.f,
                                           proj_view(pt, 1.6f), tunes, verts, rr::weapon_projectile_max_bolts,
                                           rr::weapon_projectile_max_quads);
            std::printf("PROJECTILE case=pool drawn=%u/%u/%u/%u/%u invalid=%u unseen=%u expired=%u count=%u\n", a.drawn,
                        b.drawn, c.drawn, d.drawn, e.drawn, b.invalid, d.expired, e.expired, pool.count);
            report("projectile_pool_draws_and_retires", a.drawn == 1 && b.drawn == 0 && b.invalid == 1 && c.drawn == 1 &&
                                                            d.drawn == 0 && d.expired == 1 && e.drawn == 0 &&
                                                            e.expired == 1 && pool.count == 0, "");
        }
        // (7b) the seam of the flat streak (FL 59 A; trail_ms 0, FL run 60's law): one bullet at 200 m crossing the view
        // along +x through the production builder, two consecutive stage frames at 60 fps and at 144 fps (the node
        // advanced by v dt between them, the game step the integer ms), with the baked tune and with the user's flown file
        // values (core_px 1, glow_ratio 2, glow_gain 2, hot_share 0.9, length_cap_m 5), and at 60 fps with the camera
        // moving along the bolt at 200 m/s (the bolt's velocity includes it, as a shot from the moving ship), and at 60 fps
        // with the camera turning 1 degree between the two frames (60 deg/s: a bridge in world or eye-relative space would
        // miss the seam by the turn's ~27 px here). Geometry: the second frame's tail is the first frame's head on screen
        // (|gap| <= 0.05 px). Light: both frames drawn into one target (the eye's integration of two frames) and read
        // along the axis row: uniform within 10 % ((max - min) / max) everywhere but within one core width of the
        // combined tail and head. trail_ms 0 reproduces FL run 60: each variant's lo, hi, seam and plateau within 2 % of
        // the run 60 record's (summary.json on 79f2d5e24, native, the same case).
        {
            // lo, hi, seam, plateau per variant (60 baked, 144 baked, 60 flown, 144 flown, 60 moving, 60 turning)
            static constexpr double run60[6][4] = {{4.082, 4.1992, 4.1953, 4.1992}, {4.1016, 4.1992, 4.1992, 4.1992},
                                                   {1.4551, 1.5195, 1.5195, 1.5186}, {3.5254, 3.6895, 3.6895, 3.6875},
                                                   {4.0938, 4.1992, 4.1992, 4.1992}, {4.082, 4.1992, 4.1992, 4.1992}};
            double run60_worst = 0.0;
            static wc::ProjectilePool pool;
            alignas(16) static unsigned char cfg[0x800];
            static std::uint32_t cfg_slot;
            static PoolBullet bullet;
            static ps::Looks looks;
            static rr::WeaponProjectileVertex verts[8];
            const ps::View view = proj_view(pt, 1.6f);
            pf::Tune flown = comet;
            flown.core_px = 1.f;
            flown.glow_ratio = 2.f;
            flown.glow_gain = 2.f;
            flown.hot_share = 0.9f;
            flown.length_cap_m = 5.f;
            flown.trail_ms = 0.f;
            bool seam_ok = true;
            for (unsigned variant = 0; variant < 6; ++variant) {
                const bool fast = variant == 1 || variant == 3, user = variant == 2 || variant == 3, moving = variant == 4;
                const bool turning = variant == 5;
                const double cam_units = moving ? 200.0 * 500.0 * (1.0 / 60.0) : 0.0; // the camera's travel per frame
                ps::View view2 = view;
                view2.t[0] = float(-cam_units * double(wc::world_scale)); // eye = -t / world_scale (identity basis)
                const double turn = turning ? 3.14159265358979 / 180.0 : 0.0; // view_x = x cos + z sin: the bolt moves right
                view2.r[0] = view2.r[8] = float(std::cos(turn));
                view2.r[6] = float(std::sin(turn));
                view2.r[2] = -float(std::sin(turn));
                const std::uint32_t frame = 100 + 10 * variant;
                const double dt = fast ? 1.0 / 144.0 : 1.0 / 60.0;
                const std::uint32_t step_ms = fast ? 7u : 17u;
                const pf::Tune tunes[pf::family_count] = {user ? flown : flat};
                const double travel = 581000.0 * dt; // engine units per frame
                // the first head half a (relative) frame left of the centre: the two streaks straddle it
                const std::int32_t p1[3] = {std::int32_t(-0.5 * (travel - cam_units)), 0, 200 * 500};
                const std::int32_t vel[3] = {581000, 0, 0};
                pool_reset(pool, cfg, &cfg_slot, 5000);
                pool_bullet(pool, bullet, 77, p1, vel);
                looks.resolved = 0;
                ps::BoltShape s1, s2;
                const ps::Counts a = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, frame, false, 5000, step_ms,
                                               float(dt), 1.f, view, tunes, verts, 1, 1, &s1);
                put32(bullet.node, wc::layout::node_pos, std::uint32_t(p1[0] + std::int32_t(travel + 0.5)));
                put32(cfg, wc::layout::cfg_game_ms, 5000 + step_ms);
                wc::projectile_tap(reinterpret_cast<std::uintptr_t>(bullet.obj), pool);
                const ps::Counts b = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, frame + 1, false,
                                               5000 + step_ms, step_ms, float(dt), 1.f, view2, tunes, verts + 4, 1, 1, &s2);
                const double gap = scalar_len(s2.tail[0] - s1.head[0], s2.tail[1] - s1.head[1]);
                // where the first head would sit under the second view (a bridge in world space starts there)
                const double hx = double(p1[0]), hz = 200.0 * 500.0;
                const double vx = hx * std::cos(turn) + hz * std::sin(turn) - cam_units, vz = -hx * std::sin(turn) + hz * std::cos(turn);
                const double world_miss = (double(view.m00) * vx / vz + 1.0) * 0.5 * double(pt.w) - double(s1.head[0]);
                proj_draw(proj, pt, verts, 2); // both frames in one target
                const std::vector<float> l = proj_scene(pt);
                const double w = double(s1.core_px);
                const int x0 = int(std::ceil(double(s1.tail[0]) + w)), x1 = int(std::floor(double(s2.head[0]) - w));
                double lo = 1e30, hi = 0.0, seam_v = 0.0, plateau = 0.0;
                for (int x = x0; x <= x1 && !l.empty() && x >= 0 && x < int(pt.w); ++x) {
                    const double v = l[std::size_t(cy) * pt.w + x];
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
                if (!l.empty()) {
                    seam_v = l[std::size_t(cy) * pt.w + int(std::lround(s1.head[0]))];
                    plateau = l[std::size_t(cy) * pt.w + int(std::lround(0.5 * (s1.tail[0] + s1.head[0])))];
                }
                const double spread = hi > 0.0 ? (hi - lo) / hi : 1.0;
                const bool ok = a.drawn == 1 && b.drawn == 1 && b.jumped == 0 && b.statics == 0 && std::fabs(gap) <= 0.05 &&
                                x1 - x0 > 50 && spread <= 0.10 && a.quads == 1 && b.quads == 1;
                seam_ok = seam_ok && ok;
                const double got[4] = {lo, hi, seam_v, plateau};
                for (unsigned k = 0; k < 4; ++k)
                    run60_worst = std::max(run60_worst, std::fabs(got[k] / run60[variant][k] - 1.0));
                std::printf("PROJECTILE case=seam fps=%u tune=%s camera_mps=%u turn_deg=%u distance_m=200 travel_m=%.3f "
                            "length_px=%.2f/%.2f core_px=%.2f gap_px=%.4f world_miss_px=%.2f scan_px=%d lo=%.4f hi=%.4f "
                            "spread=%.4f seam=%.4f plateau=%.4f energy=%.4f jumped=%u\n",
                            fast ? 144u : 60u, user ? "flown" : "baked", moving ? 200u : 0u, turning ? 1u : 0u,
                            travel / 500.0, double(s1.length_px), double(s2.length_px), w, gap, world_miss, x1 - x0 + 1, lo,
                            hi, spread, seam_v, plateau, double(s2.energy), b.jumped);
            }
            std::printf("PROJECTILE case=run60 trail_ms=0 worst_rel=%.4f\n", run60_worst);
            report("projectile_seam_uniform_60_144_moving_turning_camera", seam_ok, "");
            report("projectile_trail_zero_reproduces_run60", run60_worst <= 0.02, "");
        }
        // (7c) the static frame: the node not advanced for a frame (a logic tick slower than the render frame) draws the
        // head extrapolated by v x render dt x SETA rate from the read position, its tail at the previous head; the next
        // read, advanced by v x two frames, is no jump and bridges from the extrapolated head; under SETA 6 a static frame
        // holds the head at max_static_steps game steps; the next read behind that extrapolated head (within the jump
        // tolerance) holds the head on screen (never backwards); a teleport counts jumped and draws as a first frame.
        {
            static wc::ProjectilePool pool;
            alignas(16) static unsigned char cfg[0x800];
            static std::uint32_t cfg_slot;
            static PoolBullet bullet;
            static ps::Looks looks;
            static rr::WeaponProjectileVertex verts[4];
            const ps::View view = proj_view(pt, 1.6f);
            const pf::Tune tunes[pf::family_count] = {flat}; // the bridge's rules (the comet's are (7f) to (7j))
            const float dt = 1.f / 60.f;
            const std::int32_t x1 = -20000;
            const std::int32_t p1[3] = {x1, 0, 200 * 500};
            const std::int32_t vel[3] = {581000, 0, 0};
            pool_reset(pool, cfg, &cfg_slot, 5000);
            pool_bullet(pool, bullet, 78, p1, vel);
            looks.resolved = 0;
            const double z = 200.0 * 500.0; // engine units (view = world)
            const auto raster_x = [&](double engine_x) { return (double(view.m00) * engine_x / z + 1.0) * 0.5 * double(pt.w); };
            std::uint32_t now = 5000, device_frame = 200;
            const auto frame = [&](std::int32_t x, float seta, ps::BoltShape* shape) {
                put32(bullet.node, wc::layout::node_pos, std::uint32_t(x));
                put32(cfg, wc::layout::cfg_game_ms, now);
                wc::projectile_tap(reinterpret_cast<std::uintptr_t>(bullet.obj), pool);
                return ps::build(pool, &x3m::engine_memory::read, nullptr, looks, device_frame++, false, now, 17, dt, seta,
                                 view, tunes, verts, 1, 1, shape);
            };
            ps::BoltShape s1, s2, s3, s4, s4b, s5;
            const ps::Counts c1 = frame(x1, 1.f, &s1);
            now += 17;
            const ps::Counts c2 = frame(x1, 1.f, &s2); // static: the head at x1 + v dt
            now += 17;
            const std::int32_t x3 = x1 + std::int32_t(581000.0 * 2.0 * double(dt));
            const ps::Counts c3 = frame(x3, 1.f, &s3); // caught up: no jump
            now += 17;
            const ps::Counts c4 = frame(x3, 6.f, &s4); // static under SETA 6: 6 v dt = 100 ms, held at 2 x 17 ms
            // the clock not advanced: the read at its prediction (17 ms of game time) behind the head extrapolated to 34 ms
            const ps::Counts c4b = frame(x3 + std::int32_t(581000.0 * 0.017), 1.f, &s4b);
            now += 17;
            const ps::Counts c5 = frame(x3 + 60000, 1.f, &s5); // a teleport of 120 m: a first frame
            const double head2_err = double(s2.head[0]) - raster_x(x1 + 581000.0 * double(dt));
            const double tail2_err = double(s2.tail[0]) - double(s1.head[0]);
            const double seam3 = double(s3.tail[0]) - double(s2.head[0]);
            const double head4_err = double(s4.head[0]) - raster_x(x3 + 581000.0 * 0.034);
            const double want5 = 581000.0 * 0.017 * double(view.m00) * 0.5 * double(pt.w) / (z + 0.0); // px at depth z
            const double len5 = double(s5.length_px);
            const double held_err = double(s4b.head[0]) - double(s4.head[0]);
            std::printf("PROJECTILE case=static statics=%u/%u/%u/%u/%u/%u jumped=%u/%u/%u/%u/%u/%u head2_err_px=%.4f "
                        "tail2_err_px=%.4f seam3_px=%.4f head4_err_px=%.4f held=%u held_err_px=%.4f held_energy=%.4f "
                        "len5_px=%.2f want5_px=%.2f\n",
                        c1.statics, c2.statics, c3.statics, c4.statics, c4b.statics, c5.statics, c1.jumped, c2.jumped,
                        c3.jumped, c4.jumped, c4b.jumped, c5.jumped, head2_err, tail2_err, seam3, head4_err,
                        unsigned(s4b.held), held_err, double(s4b.energy), len5, want5);
            report("projectile_static_frame_extrapolates",
                   c1.drawn == 1 && c2.drawn == 1 && c3.drawn == 1 && c4.drawn == 1 && c1.statics == 0 && c2.statics == 1 &&
                       c3.statics == 0 && c3.jumped == 0 && c4.statics == 1 && c4b.statics == 0 && c4b.jumped == 0 &&
                       s4b.held && std::fabs(held_err) < 0.05 && s4b.energy == 1.f && c5.jumped == 1 &&
                       std::fabs(head2_err) < 0.05 &&
                       std::fabs(tail2_err) < 0.05 && std::fabs(seam3) < 0.05 && std::fabs(head4_err) < 0.05 &&
                       std::fabs(len5 / want5 - 1.0) < 0.01,
                   "");
        }
        // (7d) the cut: a bullet at 200 m drawn, then the camera turned 20 degrees in one frame. With the resolve's cut the
        // second frame is a first frame: its streak equals the single-bolt first-frame rule through the second view
        // (within 0.01 px) and is never longer; without the cut the bridge would draw the whole swing across the screen.
        {
            static wc::ProjectilePool pool;
            alignas(16) static unsigned char cfg[0x800];
            static std::uint32_t cfg_slot;
            static PoolBullet bullet;
            static ps::Looks looks;
            static rr::WeaponProjectileVertex verts[4];
            const pf::Tune tunes[pf::family_count] = {comet};
            const ps::View view = proj_view(pt, 1.6f);
            ps::View turned = view;
            const double turn = 20.0 * 3.14159265358979 / 180.0;
            turned.r[0] = turned.r[8] = float(std::cos(turn));
            turned.r[6] = float(std::sin(turn));
            turned.r[2] = -float(std::sin(turn));
            const std::int32_t vel[3] = {581000, 0, 0};
            double lengths[2] = {0.0, 0.0};
            ps::BoltShape second[2];
            for (unsigned with_cut = 0; with_cut < 2; ++with_cut) {
                const std::int32_t p1[3] = {-5000, 0, 200 * 500};
                pool_reset(pool, cfg, &cfg_slot, 5000);
                pool_bullet(pool, bullet, 79, p1, vel);
                looks.resolved = 0;
                ps::BoltShape first;
                ps::build(pool, &x3m::engine_memory::read, nullptr, looks, 300 + 10 * with_cut, false, 5000, 17, 1.f / 60.f,
                          1.f, view, tunes, verts, 1, 1, &first);
                put32(bullet.node, wc::layout::node_pos, std::uint32_t(p1[0] + 9683));
                put32(cfg, wc::layout::cfg_game_ms, 5017);
                wc::projectile_tap(reinterpret_cast<std::uintptr_t>(bullet.obj), pool);
                ps::build(pool, &x3m::engine_memory::read, nullptr, looks, 301 + 10 * with_cut, with_cut != 0, 5017, 17,
                          1.f / 60.f, 1.f, turned, tunes, verts, 1, 1, &second[with_cut]);
                lengths[with_cut] = second[with_cut].length_px;
            }
            // the first-frame rule through the turned view: the head and velocity in its view space, no tail
            ps::BoltIn in;
            const double hx = double(-5000 + 9683) * 0.01, hz = 200.0 * 500.0 * 0.01;
            in.p[0] = float(hx * std::cos(turn) + hz * std::sin(turn));
            in.p[2] = float(-hx * std::sin(turn) + hz * std::cos(turn));
            in.v[0] = float(5810.0 * std::cos(turn));
            in.v[2] = float(-5810.0 * std::sin(turn));
            in.step_s = 0.017f;
            in.age_s = 10.f;
            rr::WeaponProjectileVertex v[4];
            ps::BoltShape rule;
            ps::bolt_vertices(turned, tunes[0], ire_look(), in, v, &rule);
            std::printf("PROJECTILE case=cut turn_deg=20 cut_length_px=%.3f rule_px=%.3f bridged_length_px=%.2f\n", lengths[1],
                        double(rule.length_px), lengths[0]);
            report("projectile_cut_draws_a_first_frame",
                   std::fabs(lengths[1] - double(rule.length_px)) < 0.01 && lengths[1] <= double(rule.length_px) + 0.01 &&
                       lengths[0] > 2.0 * lengths[1],
                   "");
        }
        // (7e) the end-on, slow and paused bolt: a bolt flying along the view axis at 500 m (straight-ahead fire, its
        // on-screen motion 0 px) draws a spot of the core's width at full core radiance with the comet's tune (no trail, no
        // head): its centre pixel equals a flat (trail_ms 0) side-on IRE streak's at 500 m under the cap (within 2 %); an on-screen motion of 0.5 px and a paused game
        // (step 0) draw the same spot at factor 1 (the energy rule dims only a travel past length_cap_m).
        {
            const pf::Tune tune = comet;
            const ps::View view = proj_view(pt, 1.6f);
            const float f_px = view.m11 * 0.5f * float(pt.h);
            const float speed = 0.5f * 500.f / f_px / 0.016f; // m/s: 0.5 px in 16 ms at 500 m
            ps::BoltShape end_on, side, slow, paused;
            rr::WeaponProjectileVertex v[16];
            ps::BoltIn axis;
            axis.p[2] = 500.f * ps::view_units_per_metre;
            axis.v[2] = 1162.f * ps::view_units_per_metre; // receding along the view axis
            axis.step_s = 0.016f;
            axis.age_s = 10.f;
            const bool b0 = ps::bolt_vertices(view, tune, ire_look(), axis, v, &end_on);
            const bool b1 = ps::bolt_vertices(view, flat, ire_look(), bolt_at(500.f, 1162.f, 16.f), v + 4, &side);
            const bool b2 = ps::bolt_vertices(view, tune, ire_look(), bolt_at(500.f, speed, 16.f), v + 8, &slow);
            const bool b3 = ps::bolt_vertices(view, tune, ire_look(), bolt_at(500.f, speed, 0.f), v + 12, &paused);
            float centre[2] = {0.f, 0.f};
            for (unsigned k = 0; k < 2; ++k) {
                proj_draw(proj, pt, v + 4 * k, 1);
                const std::vector<float> l = proj_scene(pt);
                centre[k] = l.empty() ? 0.f : l[std::size_t(cy) * pt.w + cx];
            }
            const double ratio = centre[1] > 0.f ? double(centre[0]) / double(centre[1]) : 0.0;
            std::printf("PROJECTILE case=endon distance_m=500 endon_length_px=%.3f endon_drawn_px=%.3f endon_energy=%.4f "
                        "endon_centre=%.4f side_length_px=%.2f side_energy=%.4f side_centre=%.4f centre_ratio=%.4f "
                        "slow_length_px=%.3f slow_drawn_px=%.3f slow_energy=%.4f paused_drawn_px=%.3f paused_energy=%.4f "
                        "core_px=%.2f\n",
                        double(end_on.length_px), double(end_on.drawn_px), double(end_on.energy), double(centre[0]),
                        double(side.length_px), double(side.energy), double(centre[1]), ratio, double(slow.length_px),
                        double(slow.drawn_px), double(slow.energy), double(paused.drawn_px), double(paused.energy),
                        double(end_on.core_px));
            report("projectile_end_on_full_radiance",
                   b0 && b1 && b2 && b3 && end_on.length_px < 0.01f && end_on.drawn_px == end_on.core_px &&
                       end_on.energy == 1.f && side.energy == 1.f && std::fabs(ratio - 1.0) < 0.02 &&
                       slow.drawn_px == slow.core_px && slow.energy == 1.f && paused.length_px == 0.f &&
                       paused.drawn_px == paused.core_px && paused.energy == 1.f && end_on.head_gain == 1.f &&
                       slow.head_gain == 1.f && end_on.trail_age_s == 0.f,
                   "");
        }
        // (7f) the comet's seam (FL 60 A): the seam variants of (7b) with the comet (trail_ms 40, head_gain 1.5; the flown
        // values with the comet's defaults) over 5 frames at 60 fps and 7 at 144 fps (the ring full), the last frame read
        // along the axis row: from three core widths behind the head to the trail's end one monotonic falloff (no rise
        // above 5 % over the running minimum while F >= 0.02: no bump at a seam, no saw-tooth) on the law C F(age)
        // within 3 % where F >= 0.1, the age piecewise through the frames' heads; every frame bridged (no jump, no static)
        // and the trail drawn to F = 10 % (0.745 trail_ms: 1 ring segment at 60 fps, 4 at 144): in one quad where the
        // path is straight at a steady screen speed (merged), and with the camera's turn accelerating (1, 2, 3, 4 degrees
        // per frame) in segments (1 + 1 quads), so the seam between segments is read too.
        {
            pf::Tune flown = comet;
            flown.core_px = 1.f;
            flown.glow_ratio = 2.f;
            flown.glow_gain = 2.f;
            flown.hot_share = 0.9f;
            flown.length_cap_m = 5.f;
            bool ok_all = true;
            unsigned segmented = 0;
            for (unsigned variant = 0; variant < 7; ++variant) {
                const bool fast = variant == 1 || variant == 3, user = variant == 2 || variant == 3, moving = variant == 4;
                const bool turning = variant == 5 || variant == 6, accelerating = variant == 6;
                const pf::Tune tune = user ? flown : comet;
                const CometRun r = comet_run(proj, pt, tune, 200.f, fast ? 7u : 5u, fast ? 7u : 17u, fast ? 1.0 / 144.0 : 1.0 / 60.0,
                                             moving ? 200.0 : 0.0, turning ? 1.0 : 0.0, accelerating ? 0.3 : 0.5,
                                             400 + 20 * variant, accelerating);
                const CometProfile pr = comet_profile(r, tune);
                const ps::BoltShape& s = r.shape[r.frames - 1];
                const unsigned want_ring = fast ? 4u : 1u;
                const bool drawn_ok = s.trail_quads == want_ring && (s.merged ? s.quads == 1u : s.quads == 1u + want_ring) &&
                                      (!accelerating || !s.merged);
                segmented += !s.merged;
                const bool ok = r.drawn == r.frames && r.jumped == 0 && r.statics == 0 && drawn_ok && pr.c > 0.0 &&
                                pr.bump <= 0.05 && pr.dev <= 0.03;
                ok_all = ok_all && ok;
                std::printf("PROJECTILE case=comet_seam fps=%u tune=%s camera_mps=%u turn_deg=%s distance_m=200 frames=%u "
                            "quads=%u ring=%u merged=%u core_px=%.2f c=%.4f head_ratio=%.4f dev=%.4f bump=%.4f age50_ms=%.2f "
                            "age20_ms=%.2f trail_age_ms=%.2f at_trail=%.4f energy=%.4f\n",
                            fast ? 144u : 60u, user ? "flown" : "baked", moving ? 200u : 0u,
                            accelerating ? "1+2+3+4" : turning ? "1" : "0", r.frames, s.quads, s.trail_quads,
                            unsigned(s.merged), double(s.core_px), pr.c, pr.c > 0.0 ? pr.head_max / pr.c : 0.0, pr.dev,
                            pr.bump, pr.age50_ms, pr.age20_ms, double(s.trail_age_s) * 1e3, pr.at_trail, double(s.energy));
            }
            ok_all = ok_all && segmented >= 1;
            report("projectile_comet_seam_monotonic_60_144_moving_turning_camera", ok_all, "");
        }
        // (7g) the head and the trail: one IRE at 500 m, 60 fps, four frames, the baked tune: the row's maximum lies on the
        // head (within two core widths behind it) and the row at the point of age trail_ms is under 5 % of the trail's
        // start; the drawn comet ends where the law falls to trail_floor (0.745 trail_ms, within 1 ms). The core alone (glow_gain 0): the head's peak over the
        // trail's start is head_gain within 5 % (the glow, wider than the head, adds less there).
        {
            const pf::Tune tune = comet;
            const CometRun r = comet_run(proj, pt, tune, 500.f, 4, 17, 1.0 / 60.0, 0.0, 0.0, 0.3, 500);
            const CometProfile pr = comet_profile(r, tune);
            const ps::BoltShape& s = r.shape[r.frames - 1];
            const CometRun rc = comet_run(proj, pt, core_only, 500.f, 4, 17, 1.0 / 60.0, 0.0, 0.0, 0.3, 520);
            const CometProfile pc_ = comet_profile(rc, core_only);
            const double want = double(tune.head_gain);
            const double ratio = pr.c > 0.0 ? pr.head_max / pr.c : 0.0, core_ratio = pc_.c > 0.0 ? pc_.head_max / pc_.c : 0.0;
            const double behind = double(s.head[0]) - pr.head_max_x;
            std::printf("PROJECTILE case=comet_head distance_m=500 quads=%u ring=%u merged=%u core_px=%.2f c=%.4f head_max=%.4f "
                        "head_ratio=%.4f core_head_ratio=%.4f want_ratio=%.4f head_gain_written=%.4f head_behind_px=%.2f "
                        "at_trail=%.4f trail_age_ms=%.2f dev=%.4f bump=%.4f\n",
                        s.quads, s.trail_quads, unsigned(s.merged), double(s.core_px), pr.c, pr.head_max, ratio, core_ratio,
                        want, double(s.head_gain), behind, pr.at_trail, double(s.trail_age_s) * 1e3, pr.dev, pr.bump);
            report("projectile_comet_head_is_max_trail_under_5pct_at_trail_ms",
                   r.drawn == r.frames && s.trail_quads >= 1 && ratio > 1.2 && std::fabs(core_ratio / want - 1.0) < 0.05 &&
                       behind >= -0.5 && behind <= 2.0 * double(s.core_px) && pr.at_trail < 0.05 &&
                       std::fabs(double(s.trail_age_s) * 1e3 - double(ps::trail_drawn * tune.trail_ms)) < 1.0,
                   "");
        }
        // (7h) the trail in game time under SETA: the IRE at 500 m at 1x (17 ms steps, five frames) and at 23 ms steps
        // (SETA 1.4, the step still under trail_drawn x trail_ms / trail_cover) the trail is trail_ms: the game ms at which
        // the row falls under 50 % and 20 % of the trail's start agree within 5 % and with the law's (11.07 and 23.16 ms
        // at 40 ms) within 5 %. Under SETA 6 (100 ms steps, two frames) the trail is never shorter than the frame's
        // motion: the bolt's segment is drawn in full (its drawn length the motion within 0.5 %), the comet runs on to
        // trail_cover x the step (125 ms) into the previous segment with T = 125 / trail_drawn ms, the 50 % and 20 %
        // ages on that law within 5 %, and the energy factor is length_cap_m over the drawn world length |v| 125 ms
        // within 1 %.
        {
            const pf::Tune tune = comet;
            const CometRun one = comet_run(proj, pt, tune, 500.f, 5, 17, 1.0 / 60.0, 0.0, 0.0, 0.3, 600);
            const CometProfile p1 = comet_profile(one, tune);
            const CometRun mid = comet_run(proj, pt, tune, 500.f, 5, 23, 1.0 / 60.0, 0.0, 0.0, 0.3, 610);
            const CometProfile pm = comet_profile(mid, tune);
            const CometRun six = comet_run(proj, pt, tune, 500.f, 2, 100, 1.0 / 60.0, 0.0, 0.0, 0.3, 620);
            const ps::BoltShape& s6 = six.shape[six.frames - 1];
            pf::Tune long_tune = tune;
            long_tune.trail_ms = s6.trail_s * 1e3f; // the effective trail of the SETA 6 frame (the profile's law)
            const CometProfile p6 = comet_profile(six, long_tune);
            const auto law = [](double trail, double f) { return trail * std::log2(8.0 / (1.0 + 7.0 * f)) / 3.0; };
            const double law50 = law(tune.trail_ms, 0.5), law20 = law(tune.trail_ms, 0.2);
            const double law50_6 = law(long_tune.trail_ms, 0.5), law20_6 = law(long_tune.trail_ms, 0.2);
            const double want_energy = double(tune.length_cap_m) / (1162.0 * 0.125);
            std::printf("PROJECTILE case=comet_seta distance_m=500 age50_1x_ms=%.2f age50_1_4x_ms=%.2f age20_1x_ms=%.2f "
                        "age20_1_4x_ms=%.2f law50_ms=%.2f law20_ms=%.2f trail_6x_ms=%.2f drawn_6x_ms=%.2f age50_6x_ms=%.2f "
                        "age20_6x_ms=%.2f law50_6x_ms=%.2f law20_6x_ms=%.2f motion_6x_px=%.2f own_drawn_6x_px=%.2f "
                        "trail_age_6x_ms=%.2f energy_6x=%.4f want_energy_6x=%.4f drawn_6x_m=%.2f energy_1x=%.4f quads_6x=%u\n",
                        p1.age50_ms, pm.age50_ms, p1.age20_ms, pm.age20_ms, law50, law20, double(s6.trail_s) * 1e3,
                        double(s6.drawn_s) * 1e3, p6.age50_ms, p6.age20_ms, law50_6, law20_6, double(s6.length_px),
                        double(s6.own_px), double(s6.trail_age_s) * 1e3, double(s6.energy), want_energy,
                        double(s6.drawn_m), double(one.shape[one.frames - 1].energy), s6.quads);
            const auto within5 = [](double a, double b) { return a > 0.0 && b > 0.0 && std::fabs(a / b - 1.0) < 0.05; };
            report("projectile_comet_seta_trail_game_ms_never_under_motion",
                   one.drawn == one.frames && mid.drawn == mid.frames && six.drawn == six.frames &&
                       one.jumped + mid.jumped + six.jumped == 0 && within5(p1.age50_ms, pm.age50_ms) &&
                       within5(p1.age20_ms, pm.age20_ms) && within5(p1.age50_ms, law50) && within5(pm.age20_ms, law20) &&
                       std::fabs(double(s6.own_px) / double(s6.length_px) - 1.0) < 0.005 &&
                       std::fabs(double(s6.drawn_s) - 0.125) < 1e-4 && double(s6.trail_age_s) > 0.1 &&
                       within5(p6.age50_ms, law50_6) && within5(p6.age20_ms, law20_6) &&
                       std::fabs(double(s6.energy) / want_energy - 1.0) < 0.01,
                   "");
        }
        // (7i) the spot's hysteresis: one IRE at 500 m (core 2 px) whose whole drawn path (its motion and one ring
        // segment) alternates 1.1 and 0.9 core widths over four frames, the spot state carried from frame to frame; once
        // from a comet and once from a spot. The bolt keeps its mode in each run and the frame's brightest pixel (the
        // head, or the spot) changes by under 5 % from frame to frame.
        {
            const pf::Tune tune = comet;
            const ps::View view = proj_view(pt, 1.6f);
            const ps::ViewFrame vf = ps::view_frame(view, tune.core_px);
            const ps::BodyFrame body = ps::body_frame(tune, ire_look());
            double worst = 0.0;
            unsigned flips = 0;
            for (unsigned run = 0; run < 2; ++run) {
                unsigned state = 0;
                double prev = 0.0;
                bool prev_spot = false;
                for (unsigned f = 0; f < 4; ++f) {
                    const float w = 2.f, path = w * (((f + run) & 1u) ? 0.9f : 1.1f);
                    ps::BoltIn in;
                    in.p[2] = 500.f * ps::view_units_per_metre;
                    in.v[0] = 1.f;
                    in.step_s = 0.007f;
                    in.age_s = 10.f;
                    in.has_tail = true;
                    const float cx = 0.5f * float(pt.w), cy = 0.5f * float(pt.h);
                    in.tail_px[0] = cx - 0.3f * path, in.tail_px[1] = cy;
                    in.tail_age_s = 0.007f;
                    in.ring_count = 1;
                    in.ring_px[0][0] = cx - path, in.ring_px[0][1] = cy;
                    in.ring_span_s[0] = 0.007f;
                    in.spot_state = state;
                    rr::WeaponProjectileVertex v[rr::weapon_projectile_quads_per_bolt * 4];
                    ps::BoltShape shape;
                    const unsigned q = ps::bolt_quads(view, vf, tune, body, in, v, rr::weapon_projectile_quads_per_bolt, &shape);
                    proj_draw(proj, pt, v, q);
                    const std::vector<float> l = proj_scene(pt);
                    double peak = 0.0;
                    for (float x : l) peak = std::max(peak, double(x));
                    if (f) {
                        worst = std::max(worst, std::fabs(peak / prev - 1.0));
                        flips += shape.spot != prev_spot;
                    }
                    std::printf("PROJECTILE case=spot_hysteresis run=%u frame=%u path_px=%.2f spot=%u quads=%u peak=%.4f\n", run, f,
                                double(shape.path_px), unsigned(shape.spot), q, peak);
                    prev = peak;
                    prev_spot = shape.spot;
                    state = shape.spot ? 1u : 2u;
                }
            }
            report("projectile_spot_hysteresis_no_flip", flips == 0 && worst < 0.05, "");
        }
        // (7j) the merge's tolerance: an IRE at 200 m (core 5 px), its motion 150 px and one ring segment of 150 px at
        // 17 ms each, the motion's tail moved sideways by 0.5 and by 0.65 px (0.43 and 0.56 px, 0.086 and 0.112 core
        // widths, off the line from the drawn trail's end to the head: one quad, then two; the tolerance is 0.1 core
        // widths), the same ages: the column's light summed across the comet 200 px behind the head changes by under 3 %.
        {
            const pf::Tune tune = comet;
            const ps::View view = proj_view(pt, 1.6f);
            const ps::ViewFrame vf = ps::view_frame(view, tune.core_px);
            const ps::BodyFrame body = ps::body_frame(tune, ire_look());
            double sums[2] = {0.0, 0.0};
            unsigned quads[2] = {0, 0};
            bool merged[2] = {false, false};
            for (unsigned k = 0; k < 2; ++k) {
                ps::BoltIn in;
                in.p[2] = 200.f * ps::view_units_per_metre;
                in.p[0] = 150.f / (0.5f * float(pt.w) * view.m00) * in.p[2]; // the head 150 px right of the centre
                in.v[0] = 1162.f * ps::view_units_per_metre;
                in.step_s = 0.017f;
                in.age_s = 10.f;
                in.has_tail = true;
                const float cx = 0.5f * float(pt.w) + 150.f, cy = 0.5f * float(pt.h);
                in.tail_px[0] = cx - 150.f, in.tail_px[1] = cy + (k ? 0.65f : 0.5f);
                in.tail_age_s = 0.017f;
                in.ring_count = 1;
                in.ring_px[0][0] = cx - 300.f, in.ring_px[0][1] = cy;
                in.ring_span_s[0] = 0.017f;
                in.spot_state = 2;
                rr::WeaponProjectileVertex v[rr::weapon_projectile_quads_per_bolt * 4];
                ps::BoltShape shape;
                quads[k] = ps::bolt_quads(view, vf, tune, body, in, v, rr::weapon_projectile_quads_per_bolt, &shape);
                merged[k] = shape.merged;
                proj_draw(proj, pt, v, quads[k]);
                const std::vector<float> l = proj_scene(pt);
                const int xm = int(std::lround(cx - 200.0)); // in the ring segment's span
                for (UINT y = 0; y < pt.h && !l.empty(); ++y) sums[k] += l[std::size_t(y) * pt.w + xm];
            }
            const double change = sums[0] > 0.0 ? std::fabs(sums[1] / sums[0] - 1.0) : 1.0;
            std::printf("PROJECTILE case=merge_flip distance_m=200 quads=%u/%u merged=%u/%u column_sum=%.3f/%.3f change=%.4f\n",
                        quads[0], quads[1], unsigned(merged[0]), unsigned(merged[1]), sums[0], sums[1], change);
            report("projectile_merge_flip_under_3pct", merged[0] && !merged[1] && quads[0] == 1 && quads[1] == 2 &&
                                                           change < 0.03, "");
        }
        // (8) Reset: the programs go before it and come back after; the next frame draws again.
        {
            proj.before_reset();
            const bool gone = !proj.projectiles_ready();
            pt.release();
            g.release();
            D3DPRESENT_PARAMETERS pp3 = pp;
            device_guard("projectile_before_reset");
            const HRESULT reset = dev->Reset(&pp3);
            proj.after_reset(reset);
            pass.after_reset(reset);
            const bool remade = SUCCEEDED(reset) && make_proj_target(pt, 1920, 1080) && make_target(g, 1920, 1080);
            rr::WeaponProjectileVertex v[4];
            ps::bolt_vertices(proj_view(pt, 1.6f), comet, look, bolt_at(500.f, 1162.f, 16.f), v);
            rr::WeaponEffectsReport r{};
            const HRESULT hr = remade ? proj_draw(proj, pt, v, 1, 1.f, &r) : E_FAIL;
            const std::vector<float> l = remade ? proj_scene(pt) : std::vector<float>{};
            report("projectile_reset_recovers", gone && remade && hr == S_OK && r.drew && !l.empty() &&
                                                    l[std::size_t(cy) * pt.w + cx] > 1.f, "");
        }
    }
    // (9) the budget at 5120x1440: 512 bolts (32 near, 480 far, spread over the frame) through the production builder
    // (the pool walk, the engine reader's validation, the bridge, the comet's ring, the quads) and one draw, CPU best of
    // 40 and GPU fenced best of 60. Each timed build is a bridged moving frame with the comet's ring full: untimed, every
    // bullet is put back at its base position, tapped and built, then moved on by v x 16 ms (the clock 16 ms on) and
    // built twice more; the timed build is the fourth frame, 512 moving bullets (no static, no jump), each with its
    // segment and the trail's ring segments (the baked trail_ms 40 at 16 ms steps: 1 ring segment, in one quad when the path is straight).
    if (pc.projectile_enabled) {
        ProjTarget bt;
        const bool made = make_proj_target(bt, 5120, 1440);
        report("projectile_budget_target", made);
        if (made) {
            static wc::ProjectilePool pool;
            wc::projectile_clear(pool);
            pool.armed = 1;
            pool.resolved = 1;
            pool.body_count = 1;
            pool.bodies[0].id = 0x55;
            pool.bits[0x55 >> 5] |= 1u << (0x55 & 31u);
            alignas(16) static unsigned char cfg[0x800];
            static std::uint32_t cfg_slot;
            cfg_slot = std::uint32_t(reinterpret_cast<std::uintptr_t>(cfg));
            pool.globals.cfg_slot = reinterpret_cast<std::uintptr_t>(&cfg_slot);
            put32(cfg, wc::layout::cfg_game_ms, 5000);
            struct alignas(16) Bullet { unsigned char obj[0x80], node[0x150], ext[0x10]; };
            static std::vector<Bullet> bullets(rr::weapon_projectile_max_bolts);
            for (unsigned i = 0; i < bullets.size(); ++i) {
                Bullet& b = bullets[i];
                std::memset(&b, 0, sizeof b);
                put32(b.obj, wc::layout::obj_node, std::uint32_t(reinterpret_cast<std::uintptr_t>(b.node)));
                put32(b.obj, wc::layout::obj_ext, std::uint32_t(reinterpret_cast<std::uintptr_t>(b.ext)));
                put32(b.node, wc::layout::node_live, 1);
                put32(b.node, wc::layout::node_handle, 100 + i);
                put32(b.node, wc::layout::node_body, 0x55);
                const double d = i < 32 ? 200.0 + 10.0 * i : 600.0 + 5.0 * (i - 32); // metres
                const double ax = (halton(i + 1, 2) - 0.5) * 1.8, ay = (halton(i + 1, 3) - 0.5) * 1.8; // ndc
                put32(b.node, wc::layout::node_pos + 0, std::uint32_t(std::int32_t(ax / 1.6 * d * 500.0)));
                put32(b.node, wc::layout::node_pos + 4, std::uint32_t(std::int32_t(ay / (1.6 * 5120.0 / 1440.0) * d * 500.0)));
                put32(b.node, wc::layout::node_pos + 8, std::uint32_t(std::int32_t(d * 500.0)));
                put32(b.node, wc::projectile::node_velocity + 0, std::uint32_t(std::int32_t(581000.0 * 0.8)));
                put32(b.node, wc::projectile::node_velocity + 8, std::uint32_t(std::int32_t(581000.0 * 0.6)));
                put32(b.ext, wc::projectile::ext_expiry, 1000000);
                put32(b.ext, wc::projectile::ext_created, 1);
                wc::projectile_tap(reinterpret_cast<std::uintptr_t>(b.obj), pool);
            }
            static ps::Looks looks;
            looks.resolved = 0;
            static rr::WeaponProjectileVertex verts[rr::weapon_projectile_max_quads * 4];
            const pf::Tune comet = {2.f, 3.f, 0.2f, 0.3f, 3000.f, 60.f, 2.f, 40.f, 1.5f}; // the comet on: the costlier path
            const pf::Tune tunes[pf::family_count] = {comet};
            const ps::View view = proj_view(bt, 1.6f);
            LARGE_INTEGER freq{}, a{}, b{};
            QueryPerformanceFrequency(&freq);
            double cpu_best = 1e30;
            ps::Counts n;
            std::vector<std::int32_t> base(bullets.size() * 2);
            for (unsigned i = 0; i < bullets.size(); ++i) {
                std::memcpy(&base[2 * i], bullets[i].node + wc::layout::node_pos + 0, 4);
                std::memcpy(&base[2 * i + 1], bullets[i].node + wc::layout::node_pos + 8, 4);
            }
            const auto place = [&](std::uint32_t now, int steps) {
                put32(cfg, wc::layout::cfg_game_ms, now);
                for (unsigned i = 0; i < bullets.size(); ++i) {
                    Bullet& u = bullets[i];
                    put32(u.node, wc::layout::node_pos + 0, std::uint32_t(base[2 * i] + std::int32_t(581000.0 * 0.8 * 0.016 * steps)));
                    put32(u.node, wc::layout::node_pos + 8, std::uint32_t(base[2 * i + 1] + std::int32_t(581000.0 * 0.6 * 0.016 * steps)));
                    wc::projectile_tap(reinterpret_cast<std::uintptr_t>(u.obj), pool);
                }
            };
            unsigned bridged_statics = 0, bridged_jumped = 0;
            for (int rep = 0; rep < 40; ++rep) {
                const std::uint32_t now = 5000u + 64u * unsigned(rep), frame0 = 1000u + 4u * unsigned(rep);
                for (int k = 0; k < 3; ++k) {
                    place(now + 16u * unsigned(k), k);
                    x3m::engine_memory::next_frame();
                    ps::build(pool, &x3m::engine_memory::read, nullptr, looks, frame0 + unsigned(k), false, now + 16u * unsigned(k),
                              16, 0.016f, 1.f, view, tunes, verts, rr::weapon_projectile_max_bolts,
                              rr::weapon_projectile_max_quads);
                }
                place(now + 48, 3);
                x3m::engine_memory::next_frame();
                QueryPerformanceCounter(&a);
                n = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, frame0 + 3u, false, now + 48, 16, 0.016f, 1.f,
                              view, tunes, verts, rr::weapon_projectile_max_bolts, rr::weapon_projectile_max_quads);
                QueryPerformanceCounter(&b);
                cpu_best = std::min(cpu_best, ticks_us(a, b, freq));
                bridged_statics += n.statics;
                bridged_jumped += n.jumped;
            }
            Com<IDirect3DQuery9> ev;
            dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev.p);
            double gpu_best = 1e30;
            rr::WeaponProjectileFrame f;
            f.width = bt.w;
            f.height = bt.h;
            f.vertices = verts;
            f.quads = n.quads;
            f.lane = bt.lane_s.p;
            f.depth = bt.depth_s.p;
            bool drew = true;
            for (int rep = 0; rep < 60; ++rep) {
                dev->BeginScene();
                dev->SetDepthStencilSurface(bt.depth_s.p);
                dev->SetRenderTarget(0, bt.scene_s.p);
                dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0);
                dev->SetDepthStencilSurface(nullptr);
                fence(ev.p);
                QueryPerformanceCounter(&a);
                rr::WeaponEffectsReport r{};
                const HRESULT hr = proj.run_projectiles(f, &r);
                fence(ev.p);
                QueryPerformanceCounter(&b);
                dev->EndScene();
                drew = drew && hr == S_OK && r.quads == n.quads;
                gpu_best = std::min(gpu_best, ticks_us(a, b, freq));
            }
            double fence_best = 1e30; // the fence pair alone (the figure the wall clock carries)
            for (int rep = 0; rep < 60; ++rep) {
                dev->BeginScene();
                fence(ev.p);
                QueryPerformanceCounter(&a);
                fence(ev.p);
                QueryPerformanceCounter(&b);
                dev->EndScene();
                fence_best = std::min(fence_best, ticks_us(a, b, freq));
            }
            const std::vector<float> l = proj_scene(bt);
            unsigned lit = 0;
            for (float x : l) lit += x > 0.f;
            device_guard("projectile_timing");
            std::printf("PROJECTILE case=budget width=5120 height=1440 bolts=%u drawn=%u comets=%u dots=%u merged=%u quads=%u "
                        "trail_quads=%u cpu_us=%.1f gpu_us=%.1f fence_us=%.1f lit_share=%.4f vertex_bytes=%u statics=%u "
                        "jumped=%u\n",
                        pool.count, n.drawn, n.comets, n.dots, n.merged, n.quads, n.trail, cpu_best, gpu_best, fence_best,
                        l.empty() ? 0.0 : double(lit) / double(l.size()), unsigned(sizeof(rr::WeaponProjectileVertex)),
                        bridged_statics, bridged_jumped);
            report("projectile_budget_512_bolts", drew && n.drawn == rr::weapon_projectile_max_bolts && cpu_best < 100.0 &&
                                                      gpu_best < 50.0 && lit > 0 && bridged_statics == 0 &&
                                                      bridged_jumped == 0 && n.comets + n.dots == n.drawn &&
                                                      n.comets > 0, "");
            // (9b) the worst case: the same 512 bolts at 144 fps (7 ms steps) with the camera yawing faster every frame
            // (0.05 degrees x k at frame k: 43 degrees/s at the timed frame), so no comet's ages are linear on screen and
            // none merges: seven frames per repetition
            // (the ring full), the seventh timed, CPU best of 40 and GPU fenced best of 60. A frame after one whose bolts
            // wanted more than ring_trim_above quads redraws at most ring_trimmed_segments ring segments per comet.
            {
                const auto place7 = [&](std::uint32_t now, int steps) {
                    put32(cfg, wc::layout::cfg_game_ms, now);
                    for (unsigned i = 0; i < bullets.size(); ++i) {
                        Bullet& u = bullets[i];
                        put32(u.node, wc::layout::node_pos + 0, std::uint32_t(base[2 * i] + std::int32_t(581000.0 * 0.8 * 0.007 * steps)));
                        put32(u.node, wc::layout::node_pos + 8, std::uint32_t(base[2 * i + 1] + std::int32_t(581000.0 * 0.6 * 0.007 * steps)));
                        wc::projectile_tap(reinterpret_cast<std::uintptr_t>(u.obj), pool);
                    }
                };
                const auto yawed = [&](int k) {
                    ps::View v = view;
                    const double yaw = 0.05 * 0.5 * double(k) * double(k + 1) * 3.14159265358979 / 180.0;
                    v.r[0] = v.r[8] = float(std::cos(yaw));
                    v.r[6] = float(std::sin(yaw));
                    v.r[2] = -float(std::sin(yaw));
                    return v;
                };
                double cpu_w = 1e30;
                ps::Counts nw, before;
                unsigned w_statics = 0, w_jumped = 0;
                for (int rep = 0; rep < 40; ++rep) {
                    const std::uint32_t now = 20000u + 64u * unsigned(rep), frame0 = 3000u + 8u * unsigned(rep);
                    for (int k = 0; k < 6; ++k) {
                        place7(now + 7u * unsigned(k), k);
                        x3m::engine_memory::next_frame();
                        before = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, frame0 + unsigned(k), false,
                                           now + 7u * unsigned(k), 7, 1.f / 144.f, 1.f, yawed(k), tunes, verts,
                                           rr::weapon_projectile_max_bolts, rr::weapon_projectile_max_quads);
                    }
                    place7(now + 42, 6);
                    x3m::engine_memory::next_frame();
                    const ps::View last = yawed(6);
                    QueryPerformanceCounter(&a);
                    nw = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, frame0 + 6u, false, now + 42, 7, 1.f / 144.f,
                                   1.f, last, tunes, verts, rr::weapon_projectile_max_bolts, rr::weapon_projectile_max_quads);
                    QueryPerformanceCounter(&b);
                    cpu_w = std::min(cpu_w, ticks_us(a, b, freq));
                    w_statics += nw.statics;
                    w_jumped += nw.jumped;
                }
                double gpu_w = 1e30;
                f.quads = nw.quads;
                bool drew_w = true;
                for (int rep = 0; rep < 60; ++rep) {
                    dev->BeginScene();
                    dev->SetDepthStencilSurface(bt.depth_s.p);
                    dev->SetRenderTarget(0, bt.scene_s.p);
                    dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0);
                    dev->SetDepthStencilSurface(nullptr);
                    fence(ev.p);
                    QueryPerformanceCounter(&a);
                    rr::WeaponEffectsReport r{};
                    const HRESULT hr = proj.run_projectiles(f, &r);
                    fence(ev.p);
                    QueryPerformanceCounter(&b);
                    dev->EndScene();
                    drew_w = drew_w && hr == S_OK && r.quads == nw.quads;
                    gpu_w = std::min(gpu_w, ticks_us(a, b, freq));
                }
                const std::vector<float> lw = proj_scene(bt);
                unsigned lit_w = 0;
                for (float x : lw) lit_w += x > 0.f;
                device_guard("projectile_timing_worst");
                std::printf("PROJECTILE case=budget_worst width=5120 height=1440 step_ms=7 bolts=%u drawn=%u comets=%u merged=%u "
                            "quads=%u wanted=%u trimmed=%u previous_wanted=%u cpu_us=%.1f gpu_us=%.1f fence_us=%.1f lit_share=%.4f "
                            "statics=%u jumped=%u\n",
                            pool.count, nw.drawn, nw.comets, nw.merged, nw.quads, nw.wanted, nw.trimmed, before.wanted, cpu_w,
                            gpu_w, fence_best, lw.empty() ? 0.0 : double(lit_w) / double(lw.size()), w_statics, w_jumped);
                // a regression tripwire (coordinator, 2026-11-09): the rule's mechanics and CPU 150 us / GPU 100 us
                // fenced with the trim active; the flight budget stays the main row's 100 / 50 and the flight evidence is
                // the frame row's projectile_us_max (this case, 512 bolts on screen during a 43 degrees/s turn at 144
                // fps, is far beyond any flight); the row reports it against 100 / 50 too
                std::printf("PROJECTILE case=budget_worst_bounds cpu_within=%u gpu_within=%u\n", unsigned(cpu_w < 100.0),
                            unsigned(gpu_w < 50.0));
                report("projectile_budget_worst_trims_unmerged_144fps_turning",
                       drew_w && nw.drawn >= 480 && nw.merged * 50 <= nw.comets && nw.comets + nw.dots == nw.drawn &&
                           before.wanted > ps::ring_trim_above && nw.trimmed > 0 &&
                           nw.quads <= nw.drawn * (1u + ps::ring_trimmed_segments) && w_statics == 0 && w_jumped == 0 &&
                           cpu_w < 150.0 && gpu_w < 100.0,
                       "");
            }
        }
        bt.release();
    }
    // ---- the muzzle flash (docs/architecture/muzzle-flash-look.md section 6): the production flash_quad / build and
    // run_projectiles (the flash quads) and run (the pulses), at 2560x1440 (the design's 1440 rows) with the
    // fighter-tracer family's baked Tune and the IRE look; measured on the linear FP16 readback (max of r, g, b).
    guard_next_section("muzzle_flash");
    if (pc.projectile_enabled) {
        ProjTarget ft;
        const bool made = make_proj_target(ft, 2560, 1440);
        report("flash_target_1440", made);
        const ps::BodyLook look = ire_look();
        const pf::Tune baked = pf::families[pf::fighter_tracer].tune;
        pf::Tune core_only = baked;
        core_only.glow_gain = 0.f;
        const ps::View view = proj_view(ft, 1.6f);
        const float f_px = view.m11 * 0.5f * float(ft.h); // px per view unit at depth 1
        static rr::WeaponProjectileVertex fv[rr::weapon_projectile_max_quads * 4];
        // One flash at depth d (metres) on the screen centre, its axis `axis` (view), drawn alone: the readback, its
        // shape; the bolt core's measured white extent across a bolt at the same depth (glow off).
        struct Drawn {
            ps::FlashShape shape;
            std::vector<float> l;
            unsigned quads = 0;
            rr::WeaponPulse pulse;
            bool pulsed = false;
        };
        const auto draw_flash = [&](const pf::Tune& tune, float d, const float axis[3], float fade, D3DCOLOR clear = 0,
                                    const ps::View* other = nullptr) {
            Drawn r;
            const ps::View& fview = other ? *other : view;
            ps::FlashIn in;
            in.p[2] = d * ps::view_units_per_metre;
            for (unsigned k = 0; k < 3; ++k) in.axis[k] = axis[k];
            in.fade = fade;
            const ps::ViewFrame vf = ps::view_frame(fview, tune.core_px);
            const ps::BodyFrame bf = ps::body_frame(tune, look);
            r.quads = ps::flash_quad(fview, vf, tune, bf, look, in, fv, &r.pulse, &r.pulsed, &r.shape);
            dev->BeginScene();
            dev->SetDepthStencilSurface(ft.depth_s.p);
            dev->SetRenderTarget(0, ft.lane_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            dev->SetRenderTarget(0, ft.scene_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, clear, 1.f, 0);
            dev->SetDepthStencilSurface(nullptr);
            rr::WeaponProjectileFrame f;
            f.width = ft.w;
            f.height = ft.h;
            f.vertices = fv;
            f.quads = r.quads;
            f.lane = ft.lane_s.p;
            f.depth = ft.depth_s.p;
            rr::WeaponEffectsReport rep{};
            proj.run_projectiles(f, &rep);
            dev->EndScene();
            r.l = proj_scene(ft);
            return r;
        };
        const auto core_white = [&](float d) {
            rr::WeaponProjectileVertex v[4];
            ps::BoltShape shape;
            const bool built = ps::bolt_vertices(view, core_only, look, bolt_at(d, 1162.f, 16.f), v, &shape);
            proj_draw(proj, ft, v, built ? 1u : 0u);
            const std::vector<float> l = proj_scene(ft);
            const int cx = int(ft.w / 2), cy = int(ft.h / 2);
            return std::pair<double, double>(extent_through(l, ft.w, ft.h, cx, cy, 0, 1, 1.0),
                                             fwhm_through(l, ft.w, ft.h, cx, cy, 0, 1));
        };
        const auto white_of = [&](const Drawn& r, double* across, double* along, double* fwhm_across) {
            const std::size_t at = brightest(r.l);
            const int x = int(at % ft.w), y = int(at / ft.w);
            *along = extent_through(r.l, ft.w, ft.h, x, y, 1, 0, 1.0);
            *across = extent_through(r.l, ft.w, ft.h, x, y, 0, 1, 1.0);
            if (fwhm_across) *fwhm_across = fwhm_through(r.l, ft.w, ft.h, x, y, 0, 1);
            return std::max(*across, *along);
        };
        // the lit area at >= 80 % of the peak over the area at >= 10 % (a Gaussian's: 0.097), and the sum of the light
        const auto area_share = [&](const Drawn& r, double* total) {
            const float peak = r.l.empty() ? 0.f : r.l[brightest(r.l)];
            unsigned a80 = 0, a10 = 0;
            double sum = 0.0;
            for (float v : r.l) {
                a80 += v >= 0.8f * peak;
                a10 += v >= 0.1f * peak;
                sum += v;
            }
            if (total) *total = sum;
            return std::pair<double, unsigned>(a10 ? double(a80) / double(a10) : 1.0, a10);
        };
        static const float endon[3] = {0.f, 0.f, 1.f}, across_x[3] = {1.f, 0.f, 0.f};
        if (made) {
            // (F1) the size at scene white beside the bolt's core at the gun's depth (4 m cockpit, 27 m chase), 200 m and
            // 500 m: E = min(1.25 x the core's white extent, 48 px x 1440 / 1440), the cap binding at 4 m only; the
            // measured extent within 0.25 px of the rule, at most 1.25 x the measured core + 0.25 px and within 10 % of 1.25 x it. (F2) the anti-lamp
            // gate: at flash_stops +4 the white extent is still E (brightness cannot grow it); at flash_stops 0 the
            // Gaussian's FWHM is 1.25 x the core's (the sigma law's other end). The area share where it is resolvable.
            bool size_ok = true, cap_ok = true, lamp_ok = true, fwhm_ok = true, share_ok = true;
            for (float d : {4.f, 27.f, 200.f, 500.f}) {
                const std::pair<double, double> core = core_white(d);
                for (float stops : {2.f, 4.f, 0.f}) {
                    pf::Tune t = baked;
                    t.flash_stops = stops;
                    const Drawn r = draw_flash(t, d, endon, 1.f);
                    double across = 0, along = 0, fwhm = 0, total = 0;
                    const double white = white_of(r, &across, &along, &fwhm);
                    const std::pair<double, unsigned> share = area_share(r, &total);
                    const double rule = r.shape.white_px;
                    const bool capped = r.shape.capped;
                    const double gate = double(t.flash_core_mult) * core.first + 0.25;
                    const bool in_rule = std::fabs(white - rule) <= 0.25;
                    const double ratio = core.first > 0.0 ? white / core.first : 0.0;
                    const bool ratio_ok = std::fabs(ratio / double(t.flash_core_mult) - 1.0) <= 0.10;
                    if (stops == 2.f) {
                        size_ok = size_ok && r.quads == 1 && in_rule && (capped || (white <= gate && ratio_ok));
                        cap_ok = cap_ok && capped == (d == 4.f) && (!capped || std::fabs(white - 48.0) <= 0.5);
                    }
                    if (stops == 4.f) lamp_ok = lamp_ok && r.quads == 1 && in_rule && (capped ? white <= 48.25 : white <= gate);
                    if (stops == 0.f && !capped && d <= 27.f)
                        fwhm_ok = fwhm_ok && std::fabs(fwhm - double(t.flash_core_mult) * core.second) <= 0.5;
                    if (share.second >= 50) share_ok = share_ok && share.first <= 0.15;
                    std::printf("FLASH case=size distance_m=%.0f flash_stops=%.0f core_px=%.3f core_white_px=%.3f "
                                "core_white_measured_px=%.3f core_fwhm_measured_px=%.3f rule_white_px=%.3f white_px=%.3f "
                                "across_px=%.3f along_px=%.3f ratio=%.4f fwhm_px=%.3f sigma_px=%.3f peak=%.3f capped=%u "
                                "dot=%u share80_10=%.4f area10_px=%u\n",
                                double(d), double(stops), double(r.shape.core_px), double(r.shape.core_white_px), core.first,
                                core.second, rule, white, across, along, core.first > 0.0 ? white / core.first : 0.0, fwhm,
                                double(r.shape.sigma_px), double(r.shape.peak), unsigned(capped), unsigned(r.shape.dot),
                                share.first, share.second);
                }
            }
            report("flash_white_extent_rule_and_core_ratio", size_ok, "");
            report("flash_cap_binds_at_4m_only", cap_ok, "");
            report("flash_anti_lamp_plus4_stops", lamp_ok, "");
            report("flash_fwhm_law_at_zero_stops", fwhm_ok, "");
            report("flash_gaussian_area_share", share_ok, "");
            // over a lit hull (the scene cleared to 13/255 = 0.051): the flash adds, its own light the same
            {
                const Drawn black = draw_flash(baked, 27.f, endon, 1.f);
                const Drawn hull = draw_flash(baked, 27.f, endon, 1.f, D3DCOLOR_ARGB(0, 13, 13, 13));
                // FP16 rounds each sum (2^-11 relative): the difference within 2e-3 + 2e-3 x the flash's own light
                double worst = 0.0;
                const float base = hull.l.empty() ? 0.f : hull.l[0];
                for (std::size_t i = 0; i < black.l.size() && i < hull.l.size(); ++i)
                    worst = std::max(worst, std::fabs(double(hull.l[i] - base - black.l[i])) - 2e-3 * double(black.l[i]));
                std::printf("FLASH case=over_hull distance_m=27 hull=%.4f worst_excess=%.5f\n", double(base), worst);
                report("flash_additive_over_hull", base > 0.04f && base < 0.06f && worst < 2e-3, "");
            }
            // (F3) the streak across the view at 27 m and 500 m: its on-screen length flash_len_m x f / z within 1 px,
            // the white run along it within [length, length + E + 1]; folded into the point at 2 km
            bool streak_ok = true;
            for (float d : {27.f, 500.f, 2000.f}) {
                const Drawn r = draw_flash(baked, d, across_x, 1.f);
                const double rule = double(baked.flash_len_m) * ps::view_units_per_metre * f_px /
                                    (double(d) * ps::view_units_per_metre);
                const int y = int(ft.h / 2);
                int x0 = -1, x1 = -1;
                for (int x = 0; x < int(ft.w); ++x)
                    if (r.l[std::size_t(y) * ft.w + x] > 1.f) {
                        if (x0 < 0) x0 = x;
                        x1 = x;
                    }
                const double run = x0 < 0 ? 0.0 : double(x1 - x0 + 1);
                const bool ok = d == 2000.f ? r.shape.folded && r.shape.dot
                                            : !r.shape.folded && std::fabs(double(r.shape.length_px) - rule) <= 1.0 &&
                                                  run >= rule - 1.0 && run <= rule + double(r.shape.white_px) + 1.0;
                streak_ok = streak_ok && ok && r.quads == 1;
                std::printf("FLASH case=streak distance_m=%.0f rule_px=%.2f length_px=%.2f white_run_px=%.0f white_px=%.2f "
                            "folded=%u dot=%u\n",
                            double(d), rule, double(r.shape.length_px), run, double(r.shape.white_px),
                            unsigned(r.shape.folded), unsigned(r.shape.dot));
            }
            report("flash_streak_length_and_fold", streak_ok, "");
            // (F4) the LOD at 2 km and 3 km at the launcher's fov 90 (m00 1.0: the design's f of 1,280 px at 1440 rows): a
            // 1 px dot (no streak, no pulse: its radius under 3 px), its light no more than the 500 m flash's
            {
                const ps::View fov90 = proj_view(ft, 1.0f);
                double near_total = 0.0;
                area_share(draw_flash(baked, 500.f, endon, 1.f, 0, &fov90), &near_total);
                bool lod_ok = true;
                for (float d : {2000.f, 3000.f}) {
                    const Drawn r = draw_flash(baked, d, across_x, 1.f, 0, &fov90);
                    double total = 0.0;
                    area_share(r, &total);
                    lod_ok = lod_ok && r.quads == 1 && r.shape.dot && r.shape.folded && !r.pulsed && total <= near_total;
                    std::printf("FLASH case=lod distance_m=%.0f dot=%u folded=%u pulse=%u total=%.4f near_total=%.4f "
                                "peak=%.4f fwhm_px=%.3f\n",
                                double(d), unsigned(r.shape.dot), unsigned(r.shape.folded), unsigned(r.pulsed), total,
                                near_total, double(r.shape.peak), double(r.shape.fwhm_px));
                }
                report("flash_lod_dot_no_streak_no_pulse", lod_ok, "");
            }
        }
        // (F5) the life against the refire gap through the production tap and builder: a shooter at 27 m firing an IRE
        // whose muzzle record (ours) was relit gap_ms after the previous shot; stage frames at the game step until the
        // flash ends. Gap 150 ms: drawn at ages 0 and 17 at 17 ms steps (life 30 ms), 0 and 7 at 7 ms steps (life 14 ms:
        // two steps), once at 100 ms steps (SETA 6); a first shot once; a record not ours (the engine's flash shown)
        // never; a registration three steps late never (flash_stale). F = 1 on the first frame, 1 - age / life after.
        // The shooter moves 1.7 m along x between the two frames: the second flash rides it (its raster is the ship's
        // new position plus the muzzle offset).
        {
            static wc::ProjectilePool pool;
            alignas(16) static unsigned char cfg[0x800];
            static std::uint32_t cfg_slot;
            static ps::Looks looks;
            static FlashWorld w;
            static rr::WeaponPulse pulses[rr::weapon_max_pulses];
            const pf::Tune tunes[pf::family_count] = {baked};
            // (a record whose shooter's handle is not the shooter's now: another ship at that object, flash_lost)
            struct LifeCase { const char* name; std::uint32_t step, gap; bool ours; std::uint32_t tap_age; unsigned frames; bool other; };
            const LifeCase cases[7] = {{"gap150_step17", 17, 150, true, 0, 4, false}, {"gap150_step7", 7, 150, true, 0, 4, false},
                                       {"gap150_step100_seta6", 100, 150, true, 0, 3, false},
                                       {"first_shot", 17, 0, true, 0, 3, false}, {"not_ours", 17, 150, false, 0, 3, false},
                                       {"tapped_late", 17, 150, true, 51, 3, false},
                                       {"record_other_shooter", 17, 150, true, 0, 3, true}};
            const unsigned want_drawn[7] = {2, 2, 1, 1, 0, 0, 0};
            bool life_ok = true, fade_ok = true, ride_ok = true, axis_ok = true;
            for (unsigned c = 0; c < 7; ++c) {
                const LifeCase& lc = cases[c];
                pool_reset(pool, cfg, &cfg_slot, 10000);
                for (wc::MuzzleRecord& m : pool.muzzles) m = wc::MuzzleRecord{};
                put32(cfg, wc::projectile::cfg_step_ms, lc.step);
                looks.look[0] = look;
                looks.resolved = 1;
                w.cfg = cfg;
                w.born = 10000;
                const double ship[3] = {-2000.0, -1500.0, 13500.0}, vship[3] = {50000.0, 0.0, 0.0};
                const double muzzle[3] = {1600.0, 250.0, 1250.0}, vbullet[3] = {0.0, 0.0, 581000.0};
                for (unsigned k = 0; k < 3; ++k) {
                    w.ship_pos[k] = ship[k];
                    w.ship_vel[k] = vship[k];
                    w.muzzle[k] = muzzle[k];
                    w.bullet_vel[k] = vbullet[k];
                }
                w.make(pool, 700 + c, lc.gap, lc.ours, 3);
                if (lc.other) pool.muzzles[3].handle ^= 0x100;
                unsigned drawn = 0, lost = 0;
                double axis_err = 0.0; // degrees between the flash's axis and the shot's own direction (view +z here)
                float fades[4]{};
                float rasters[4][2]{};
                double expect[4][2]{};
                for (unsigned f = 0; f < lc.frames; ++f) {
                    const std::uint32_t now = 10000 + lc.tap_age + f * lc.step;
                    w.place(pool, now);
                    ps::FlashShape shapes[4];
                    ps::FlashOut out;
                    out.pulses = pulses;
                    out.max_pulses = rr::weapon_max_pulses;
                    out.max_quads = rr::weapon_projectile_max_flashes;
                    out.shapes = shapes;
                    x3m::engine_memory::next_frame();
                    const ps::Counts n = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, 500 + 10 * c + f, false, now,
                                                   lc.step, float(lc.step) * 1e-3f, 1.f, proj_view(pt, 1.6f), tunes, fv,
                                                   rr::weapon_projectile_max_bolts, rr::weapon_projectile_bolt_quads,
                                                   nullptr, &out);
                    lost += n.flash_lost;
                    if (n.flashes) {
                        const double cz = std::max(-1.0, std::min(1.0, double(shapes[0].axis[2])));
                        axis_err = std::max(axis_err, std::acos(cz) * 180.0 / 3.14159265358979);
                        if (drawn < 4) {
                            fades[drawn] = shapes[0].fade;
                            rasters[drawn][0] = shapes[0].raster[0];
                            rasters[drawn][1] = shapes[0].raster[1];
                            // the muzzle's birth point riding the shooter: ship(now) + the muzzle offset (view = world)
                            const double s = double(std::int32_t(now - w.born)) * 1e-3;
                            const ps::View pv = proj_view(pt, 1.6f);
                            const double vx = (ship[0] + vship[0] * s + muzzle[0]) * 0.01, vy = (ship[1] + muzzle[1]) * 0.01,
                                         vz = (ship[2] + muzzle[2]) * 0.01;
                            expect[drawn][0] = (double(pv.m00) * vx / vz + 1.0) * 0.5 * double(pt.w);
                            expect[drawn][1] = (1.0 - double(pv.m11) * vy / vz) * 0.5 * double(pt.h);
                        }
                        ++drawn;
                    }
                }
                const float life = ps::flash_life_ms(baked, lc.gap, lc.step);
                const bool fade_case = drawn == 2 && std::fabs(fades[0] - 1.f) < 1e-6f &&
                                       std::fabs(fades[1] - (1.f - float(lc.step) / life)) < 1e-4f;
                life_ok = life_ok && drawn == want_drawn[c] && (!lc.other || lost == 1);
                if (want_drawn[c] == 2) fade_ok = fade_ok && fade_case;
                double ride = 0.0;
                for (unsigned k = 0; k < drawn && k < 4; ++k)
                    ride = std::max(ride, std::max(std::fabs(rasters[k][0] - expect[k][0]), std::fabs(rasters[k][1] - expect[k][1])));
                ride_ok = ride_ok && ride < 0.1;
                axis_ok = axis_ok && axis_err < 0.05;
                std::printf("FLASH case=life name=%s step_ms=%u gap_ms=%u ours=%u tap_age_ms=%u life_ms=%.2f drawn=%u want=%u "
                            "fade0=%.4f fade1=%.4f ride_err_px=%.4f axis_err_deg=%.4f lost=%u matched=%lu unmatched=%lu "
                            "stale=%lu\n",
                            lc.name, lc.step, lc.gap, unsigned(lc.ours), lc.tap_age, double(life), drawn, want_drawn[c],
                            double(fades[0]), double(fades[1]), ride, axis_err, lost,
                            static_cast<unsigned long>(pool.counters.flash_matched),
                            static_cast<unsigned long>(pool.counters.flash_unmatched),
                            static_cast<unsigned long>(pool.counters.flash_stale));
            }
            report("flash_life_vs_refire_seta", life_ok, "");
            report("flash_fade_law", fade_ok, "");
            report("flash_rides_the_shooter", ride_ok, "");
            // the axis is v_bullet - (the slip + node velocity): the shot's own direction, the shooter's 100 m/s across
            // (a third of it in the slip) removed exactly
            report("flash_axis_is_the_shots_own_direction", axis_ok, "");
        }
        // (F6) the hull pulse through run_projectiles (pulses alone): a plane hull under the left half of the 1080p target, sky
        // on the right; the pulse of a 27 m own gun (radius 3 m) on the boundary, of a capital's hull at 500 m, faded to
        // 0.43; every pixel against the CPU twin (the plane's point under the pixel, light = 0.5 x tint x gain x (1 -
        // r / R)^2), the sky untouched, the peak I (1 + k) = 0.5 at full gain within 5 %; under 3 px (2 km) not built.
        {
            bool pulse_ok = true, sky_ok = true, peak_ok = true, drop_ok = true;
            for (float d : {27.f, 500.f, 2000.f}) {
                for (float gain : {1.f, 0.43f}) {
                    ps::FlashIn in;
                    const float z = d * ps::view_units_per_metre;
                    const ps::View pv = proj_view(pt, 1.6f);
                    // the muzzle about 0.9 m left of the boundary, on a pixel centre (D3D9: pixel x at ndc 2x / W - 1)
                    const double fx = double(pv.m00) * 0.5 * double(g.w);
                    const double px = std::floor(0.5 * double(g.w) - 0.9 * 5.0 * fx / double(z) + 0.5);
                    in.p[0] = float((2.0 * px / double(g.w) - 1.0) * double(z) / double(pv.m00));
                    in.p[2] = z;
                    in.axis[2] = 1.f;
                    in.fade = gain;
                    ps::FlashShape shape;
                    rr::WeaponPulse pulse;
                    bool pulsed = false;
                    ps::flash_quad(pv, ps::view_frame(pv, baked.core_px), baked, ps::body_frame(baked, look), look, in, fv,
                                   &pulse, &pulsed, &shape);
                    if (d == 2000.f) {
                        drop_ok = drop_ok && !pulsed;
                        continue;
                    }
                    draw_lane(g, z, 0.5f);
                    const rr::WeaponEffectsFrame f = base_frame(g);
                    rr::WeaponProjectileFrame pfr;
                    pfr.width = g.w;
                    pfr.height = g.h;
                    pfr.pulses = &pulse;
                    pfr.pulse_count = pulsed ? 1u : 0u;
                    pfr.lane_texture = g.lane_tex.p;
                    pfr.four_channel = true;
                    pfr.m00 = f.m00;
                    pfr.m11 = f.m11;
                    pfr.m22 = pv.m22;
                    pfr.m32 = pv.m32;
                    pfr.near_z = f.near_z;
                    rr::WeaponEffectsReport rep{};
                    dev->BeginScene();
                    dev->SetRenderTarget(0, g.scene_s.p);
                    dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
                    const HRESULT hr = proj.run_projectiles(pfr, &rep);
                    dev->EndScene();
                    const std::vector<float> l = read_luma(g, g.scene_s.p);
                    const float R = pulse.radius, m00 = f.m00, m11 = f.m11;
                    double worst = 0.0, peak = 0.0, peak_want = 0.0;
                    unsigned lit = 0, sky_lit = 0;
                    for (UINT y = 0; y < g.h && !l.empty(); ++y)
                        for (UINT x = 0; x < g.w; ++x) {
                            const float v = l[std::size_t(y) * g.w + x];
                            const bool hull = float(x) < 0.5f * float(g.w);
                            if (!hull) {
                                sky_lit += v != 0.f;
                                continue;
                            }
                            const double rx = (2.0 * x / g.w - 1.0) / m00, ry = (1.0 - 2.0 * y / g.h) / m11;
                            const double dx = rx * z - in.p[0], dy = ry * z - in.p[1];
                            const double r = std::sqrt(dx * dx + dy * dy) / R;
                            const double want = r < 1.0 ? 0.5 * double(gain) * (1.0 - r) * (1.0 - r) : 0.0;
                            worst = std::max(worst, std::fabs(double(v) - want));
                            lit += v > 0.f;
                            if (v > peak) peak = v, peak_want = 0.5 * double(gain);
                        }
                    const double radius_px = double(R) * double(m11) * 0.5 * double(g.h) / double(z);
                    pulse_ok = pulse_ok && SUCCEEDED(hr) && rep.pulses == 1 && pulsed && worst < 0.02 && lit > 0;
                    sky_ok = sky_ok && sky_lit == 0;
                    peak_ok = peak_ok && std::fabs(peak / peak_want - 1.0) <= 0.05;
                    std::printf("PULSE distance_m=%.0f gain=%.2f radius_m=%.2f radius_px=%.2f shape_px=%.2f half_over_r=%.4f "
                                "lit=%u sky_lit=%u peak=%.4f peak_want=%.4f worst=%.5f pulses=%u result=%08lx\n",
                                double(d), double(gain), double(R / ps::view_units_per_metre), radius_px,
                                double(shape.pulse_px), double(pulse.half / pulse.radius), lit, sky_lit, peak, peak_want,
                                worst, rep.pulses, static_cast<unsigned long>(hr));
                }
            }
            // S2 / N2: the screen cap (at most 0.15 of the rows in radius: the 27 m own gun at 1080 rows, 170.7 px wanted,
            // drawn within it), the eye within 1.5 radii (the own gun in first person, 3 m from the eye) and a culled flash
            // (off the target) draw no pulse
            {
                const ps::View pv = proj_view(pt, 1.6f);
                const auto pulse_of = [&](float x_m, float z_m, ps::FlashShape* shape, unsigned* quads) {
                    ps::FlashIn in;
                    in.p[0] = x_m * ps::view_units_per_metre;
                    in.p[1] = -0.5f * ps::view_units_per_metre;
                    in.p[2] = z_m * ps::view_units_per_metre;
                    in.axis[2] = 1.f;
                    rr::WeaponPulse p;
                    bool pulsed = false;
                    *quads = ps::flash_quad(pv, ps::view_frame(pv, baked.core_px), baked, ps::body_frame(baked, look), look,
                                            in, fv, &p, &pulsed, shape);
                    return pulsed;
                };
                ps::FlashShape s27, s3, soff;
                unsigned q27 = 0, q3 = 0, qoff = 0;
                const bool p27 = pulse_of(0.9f, 27.f, &s27, &q27), p3 = pulse_of(1.f, 3.f, &s3, &q3),
                           poff = pulse_of(400.f, 27.f, &soff, &qoff);
                const float cap_px = ps::pulse_max_rows * pv.height;
                std::printf("PULSE_CAP cap_px=%.2f own27_px=%.2f own27_clamped=%u own27_drawn=%u cockpit3_flash=%u "
                            "cockpit3_pulse=%u offscreen_flash=%u offscreen_pulse=%u\n",
                            double(cap_px), double(s27.pulse_px), unsigned(s27.pulse_clamped), unsigned(p27), q3, unsigned(p3),
                            qoff, unsigned(poff));
                report("pulse_clamped_to_the_screen_cap", p27 && q27 == 1 && s27.pulse_clamped && s27.pulse_px <= cap_px + 0.01f &&
                                                              s27.pulse_px > 0.9f * cap_px,
                       "");
                report("pulse_none_with_the_eye_within_1p5_radii", q3 == 1 && !p3, "");
                report("pulse_none_beside_a_culled_flash", qoff == 0 && !poff, "");
                // N6: the pulse enters the TAA history (it samples the lane, so it cannot flag the lane as the bolts do):
                // its ghost through the production resolve, the 27 m own-gun pulse on a hull plane for one frame; gone by
                // the second frame after it (at most one extra frame)
                ps::FlashIn in;
                in.p[0] = 0.9f * ps::view_units_per_metre;
                in.p[2] = 27.f * ps::view_units_per_metre;
                in.axis[2] = 1.f;
                rr::WeaponPulse p;
                bool pulsed = false;
                ps::flash_quad(pv, ps::view_frame(pv, baked.core_px), baked, ps::body_frame(baked, look), look, in, fv, &p,
                               &pulsed, nullptr);
                Com<IDirect3DTexture9> motion;
                Com<IDirect3DSurface9> motion_s;
                const bool made_m = SUCCEEDED(dev->CreateTexture(g.w, g.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A32B32G32R32F,
                                                                 D3DPOOL_DEFAULT, &motion.p, nullptr)) &&
                                    SUCCEEDED(motion.p->GetSurfaceLevel(0, &motion_s.p));
                double ghost[3] = {0, 0, 0}, ghost_peak = 0;
                const bool ran = made_m && pulsed && resolve_pulse_ghost(proj, motion, motion_s, p, ghost, &ghost_peak);
                std::printf("PULSE_GHOST ran=%u peak=%.4f ghost1=%.4f ghost2=%.4f\n", unsigned(ran), ghost_peak, ghost[1],
                            ghost[2]);
                report("pulse_taa_ghost_at_most_one_frame", ran && ghost_peak > 0.1 && ghost[2] <= 0.01, "");
                draw_lane(g, 0.f, -1.f);
            }
            report("pulse_lights_the_lane_surface_by_the_law", pulse_ok, "");
            report("pulse_sky_untouched", sky_ok, "");
            report("pulse_peak_at_the_mount", peak_ok, "");
            report("pulse_dropped_under_3px", drop_ok, "");
        }
        // (F7) the budget: 64 simultaneous flashes and 64 pulses at 5120x1440 (two own guns at 27 m, 62 spread from
        // 300 m to 1.5 km across the frame) through the production builder: CPU the build with the flashes less the same
        // build without (best of 40, every flash's first frame: anchored, re-posed and built), GPU fenced the one run_projectiles
        // of the flash quads and the pulses over a hull lane at the guns' depth (best of 60; each part alone reported).
        {
            ProjTarget bt;
            const bool made_b = make_proj_target(bt, 5120, 1440);
            static wc::ProjectilePool pool;
            alignas(16) static unsigned char cfg[0x800];
            static std::uint32_t cfg_slot;
            static ps::Looks looks;
            static FlashWorld worlds[rr::weapon_projectile_max_flashes];
            static rr::WeaponPulse pulses[rr::weapon_max_pulses];
            static ps::FlashShape shapes[rr::weapon_projectile_max_flashes];
            const pf::Tune tunes[pf::family_count] = {baked};
            const ps::View bv = proj_view(bt, 1.6f);
            pool_reset(pool, cfg, &cfg_slot, 20000);
            for (wc::MuzzleRecord& m : pool.muzzles) m = wc::MuzzleRecord{};
            put32(cfg, wc::projectile::cfg_step_ms, 17);
            looks.look[0] = look;
            looks.resolved = 1;
            for (unsigned i = 0; i < rr::weapon_projectile_max_flashes; ++i) {
                FlashWorld& w = worlds[i];
                w.cfg = cfg;
                w.born = 20000;
                const double d = i < 2 ? 27.0 : 300.0 + 20.0 * double(i - 2); // metres
                const double ax = i < 2 ? (i ? 0.08 : -0.08) : (halton(i + 1, 2) - 0.5) * 1.8;
                const double ay = i < 2 ? -0.2 : (halton(i + 1, 3) - 0.5) * 1.8;
                w.ship_pos[0] = ax / 1.6 * d * 500.0;
                w.ship_pos[1] = ay / (1.6 * 5120.0 / 1440.0) * d * 500.0;
                w.ship_pos[2] = d * 500.0;
                w.ship_vel[2] = 50000.0;
                w.muzzle[0] = 0.0;
                w.bullet_vel[0] = 581000.0 * 0.8;
                w.bullet_vel[2] = 581000.0 * 0.6;
                w.make(pool, 900 + i, 150, true, i);
                w.place(pool, 20000);
            }
            ps::FlashOut out;
            out.pulses = pulses;
            out.max_pulses = rr::weapon_max_pulses;
            out.max_quads = rr::weapon_projectile_max_flashes;
            out.shapes = shapes;
            LARGE_INTEGER freq{}, a{}, b{};
            QueryPerformanceFrequency(&freq);
            const auto rearm = [&] {
                for (unsigned i = 0; i < pool.count; ++i) {
                    pool.entries[i].flash = wc::projectile::flash_pending;
                    pool.entries[i].flash_frames = 0;
                }
            };
            double with_best = 1e30, without_best = 1e30;
            ps::Counts n;
            for (int rep = 0; rep < 40; ++rep) {
                rearm();
                x3m::engine_memory::next_frame();
                QueryPerformanceCounter(&a);
                // both builds first frames (no bridge: the frames 2 apart), the same bolts; only the flashes differ
                n = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, 3000 + 4 * rep, false, 20000, 17, 0.016f, 1.f,
                              bv, tunes, fv, rr::weapon_projectile_max_bolts, rr::weapon_projectile_bolt_quads, nullptr, &out);
                QueryPerformanceCounter(&b);
                with_best = std::min(with_best, ticks_us(a, b, freq));
                x3m::engine_memory::next_frame();
                QueryPerformanceCounter(&a);
                ps::build(pool, &x3m::engine_memory::read, nullptr, looks, 3002 + 4 * rep, false, 20000, 17, 0.016f, 1.f, bv,
                          tunes, fv, rr::weapon_projectile_max_bolts, rr::weapon_projectile_bolt_quads);
                QueryPerformanceCounter(&b);
                without_best = std::min(without_best, ticks_us(a, b, freq));
            }
            // the last timed build's flashes: their quads after its bolts' (rebuilt once more untimed)
            rearm();
            x3m::engine_memory::next_frame();
            n = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, 4000, false, 20000, 17, 0.016f, 1.f, bv, tunes, fv,
                          rr::weapon_projectile_max_bolts, rr::weapon_projectile_bolt_quads, nullptr, &out);
            Com<IDirect3DQuery9> ev;
            dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev.p);
            double gpu_best = 1e30, fence_best = 1e30, part_best[6] = {1e30, 1e30, 1e30, 1e30, 1e30, 1e30};
            bool drew = made_b;
            if (made_b) fill_proj_lane(bt, 27.f * ps::view_units_per_metre);
            for (int rep = 0; made_b && rep < 420; ++rep) {
                // both; the flash quads alone; the pulses alone; the 62 far pulses alone; the two own pulses alone (the
                // builder writes the entries' flashes last registered first: the own guns' are the last two); the 64
                // bullets' bolt quads alone; the bolts with the flash quads and the pulses (the production call)
                const int part = rep < 60 ? -1 : rep / 60 - 1;
                dev->BeginScene();
                dev->SetDepthStencilSurface(bt.depth_s.p);
                dev->SetRenderTarget(0, bt.scene_s.p);
                dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0);
                dev->SetDepthStencilSurface(nullptr);
                rr::WeaponProjectileFrame pf_frame;
                pf_frame.width = bt.w;
                pf_frame.height = bt.h;
                pf_frame.vertices = part >= 4 ? fv : fv + 4 * n.quads;
                pf_frame.quads = part == 4 ? n.quads : part == 5 ? n.quads + n.flashes : part >= 1 ? 0u : n.flashes;
                pf_frame.lane = bt.lane_s.p;
                pf_frame.depth = bt.depth_s.p;
                pf_frame.pulses = part == 3 ? pulses + (n.pulses - 2) : pulses;
                pf_frame.pulse_count = part == 0 || part == 4 ? 0u : part == 2 ? n.pulses - 2 : part == 3 ? 2u : n.pulses;
                pf_frame.lane_texture = bt.lane_tex.p;
                pf_frame.four_channel = true;
                pf_frame.m00 = bv.m00;
                pf_frame.m11 = bv.m11;
                pf_frame.m22 = bv.m22;
                pf_frame.m32 = bv.m32;
                pf_frame.near_z = bv.near_z;
                fence(ev.p);
                QueryPerformanceCounter(&a);
                rr::WeaponEffectsReport r1{};
                const HRESULT h1 = proj.run_projectiles(pf_frame, &r1);
                fence(ev.p);
                QueryPerformanceCounter(&b);
                dev->EndScene();
                if (part < 0) {
                    drew = drew && h1 == S_OK && r1.quads == n.flashes && r1.pulses == n.pulses;
                    gpu_best = std::min(gpu_best, ticks_us(a, b, freq));
                } else {
                    part_best[part] = std::min(part_best[part], ticks_us(a, b, freq));
                }
            }
            for (int rep = 0; made_b && rep < 60; ++rep) {
                dev->BeginScene();
                fence(ev.p);
                QueryPerformanceCounter(&a);
                fence(ev.p);
                QueryPerformanceCounter(&b);
                dev->EndScene();
                fence_best = std::min(fence_best, ticks_us(a, b, freq));
            }
            const double cpu = with_best - without_best, increment = part_best[5] - part_best[4];
            device_guard("flash_budget");
            std::printf("FLASH case=budget width=5120 height=1440 flashes=%u pulses=%u bolts=%u cpu_with_us=%.1f "
                        "cpu_without_us=%.1f cpu_us=%.1f gpu_us=%.1f gpu_net_us=%.1f gpu_flashes_us=%.1f gpu_pulses_us=%.1f "
                        "gpu_far_pulses_us=%.1f gpu_own_pulses_us=%.1f gpu_bolts_us=%.1f gpu_bolts_flashes_us=%.1f "
                        "gpu_increment_us=%.1f fence_us=%.1f lost=%u stale=%u dropped=%u\n",
                        n.flashes, n.pulses, n.drawn, with_best, without_best, cpu, gpu_best, gpu_best - fence_best,
                        part_best[0], part_best[1], part_best[2], part_best[3], part_best[4], part_best[5], increment,
                        fence_best, n.flash_lost, n.flash_stale, n.flash_dropped);
            // the gate (the production shape: the flashes and pulses ride the bolts' run_projectiles, its state and the
            // fence pair paid once): CPU under 20 us and the GPU fenced increment under 30 us; the flashes and pulses
            // alone (gpu_us, the fence pair included) are reported
            report("flash_budget_64_flashes_64_pulses",
                   made_b && drew && n.flashes == rr::weapon_projectile_max_flashes && n.pulses == rr::weapon_max_pulses &&
                       cpu < 20.0 && increment < 30.0,
                   "");
            bt.release();
        }
        // (F8) the combined frame (the design's budget row): 512 bolts, 64 of them just fired (each its flash's first
        // frame and its pulse; two own guns at 27 m, 62 from 300 m to 1.5 km), 448 others (32 from 200 m, 416 from 600 m,
        // across the frame), at 5120x1440, measured as one frame: the timed build is the fourth consecutive stage frame
        // (448 bolts bridged, the 64 shots born on it; the flashes off before), CPU best of 40, then its one run_projectiles (pulses,
        // bolts and flash quads) fenced, best of 60, over a hull lane at 27 m. Gated on the production tune (the baked
        // fighter tracer, comet off): GPU 60 us fenced and the flash work's CPU share 20 us, the frame's CPU reported against
        // the design's 100 us with its decomposition (variants 2 and 3); the comet tune (trail_ms 40) reported beside it.
        {
            ProjTarget ct;
            const bool made_c = make_proj_target(ct, 5120, 1440);
            static wc::ProjectilePool pool;
            alignas(16) static unsigned char cfg[0x800];
            static std::uint32_t cfg_slot;
            static ps::Looks looks;
            static FlashWorld worlds[rr::weapon_projectile_max_flashes];
            struct alignas(16) Plain { unsigned char obj[0x80], node[0x150], ext[0x10]; };
            constexpr unsigned plain_count = rr::weapon_projectile_max_bolts - rr::weapon_projectile_max_flashes;
            static Plain plain[plain_count];
            static rr::WeaponPulse pulses[rr::weapon_max_pulses];
            static rr::WeaponProjectileVertex cv[rr::weapon_projectile_max_quads * 4];
            const ps::View cview = proj_view(ct, 1.6f);
            const pf::Tune comet_tune = {2.f, 3.f, 0.2f, 0.3f, 3000.f, 60.f, 2.f, 40.f, 1.5f};
            if (made_c) fill_proj_lane(ct, 27.f * ps::view_units_per_metre);
            Com<IDirect3DQuery9> ev;
            dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev.p);
            LARGE_INTEGER freq{}, a{}, b{};
            QueryPerformanceFrequency(&freq);
            bool combined_ok = made_c;
            double cpu_of[4] = {1e30, 1e30, 1e30, 1e30}, gpu_combined = 1e30;
            for (unsigned variant = 0; variant < 4 && made_c; ++variant) {
                const pf::Tune tunes[pf::family_count] = {variant == 1 ? comet_tune : baked};
                // variant 2: the same frame, no flash work (FlashOut null); variant 3: without the 64 shots (448 bolts)
                double cpu_best = 1e30, gpu_best = 1e30;
                ps::Counts n;
                for (int rep = 0; rep < 40; ++rep) {
                    const std::uint32_t now0 = 40000u + 20000u * variant + 100u * unsigned(rep), born = now0 + 48u;
                    const unsigned frame0 = 6000u + 2000u * variant + 8u * unsigned(rep);
                    pool_reset(pool, cfg, &cfg_slot, now0);
                    for (wc::MuzzleRecord& m : pool.muzzles) m = wc::MuzzleRecord{};
                    put32(cfg, wc::projectile::cfg_step_ms, 16);
                    looks.look[0] = look;
                    looks.resolved = 1;
                    for (unsigned i = 0; i < rr::weapon_projectile_max_flashes; ++i) {
                        FlashWorld& w = worlds[i];
                        w.cfg = cfg;
                        w.born = born;
                        const double d = i < 2 ? 27.0 : 300.0 + 20.0 * double(i - 2);
                        const double ax = i < 2 ? (i ? 0.08 : -0.08) : (halton(i + 1, 2) - 0.5) * 1.8;
                        const double ay = i < 2 ? -0.2 : (halton(i + 1, 3) - 0.5) * 1.8;
                        w.ship_pos[0] = ax / 1.6 * d * 500.0;
                        w.ship_pos[1] = ay / (1.6 * 5120.0 / 1440.0) * d * 500.0;
                        w.ship_pos[2] = d * 500.0;
                        w.ship_vel[0] = w.ship_vel[1] = 0.0;
                        w.ship_vel[2] = 50000.0;
                        w.muzzle[0] = w.muzzle[1] = w.muzzle[2] = 0.0;
                        w.bullet_vel[0] = 581000.0 * 0.8;
                        w.bullet_vel[1] = 0.0;
                        w.bullet_vel[2] = 581000.0 * 0.6;
                        w.make(pool, 1200 + i, 150, true, i);
                    }
                    for (unsigned i = 0; i < plain_count; ++i) {
                        Plain& u = plain[i];
                        std::memset(&u, 0, sizeof u);
                        put32(u.obj, wc::layout::obj_node, std::uint32_t(reinterpret_cast<std::uintptr_t>(u.node)));
                        put32(u.obj, wc::layout::obj_ext, std::uint32_t(reinterpret_cast<std::uintptr_t>(u.ext)));
                        put32(u.node, wc::layout::node_live, 1);
                        put32(u.node, wc::layout::node_handle, 3000 + i);
                        put32(u.node, wc::layout::node_body, 0x55);
                        put32(u.node, wc::projectile::node_velocity + 0, std::uint32_t(std::int32_t(581000.0 * 0.8)));
                        put32(u.node, wc::projectile::node_velocity + 8, std::uint32_t(std::int32_t(581000.0 * 0.6)));
                        put32(u.ext, wc::projectile::ext_expiry, now0 + 100000u);
                        put32(u.ext, wc::projectile::ext_created, 1);
                    }
                    for (unsigned k = 0; k < 4; ++k) {
                        const std::uint32_t now = now0 + 16u * k;
                        // the 64 shots exist from the timed frame on (their birth): their bolts' first frame there
                        for (unsigned i = 0; k == 3 && variant != 3 && i < rr::weapon_projectile_max_flashes; ++i)
                            worlds[i].place(pool, now);
                        for (unsigned i = 0; i < plain_count; ++i) {
                            Plain& u = plain[i];
                            const double d = i < 32 ? 200.0 + 10.0 * i : 600.0 + 3.0 * (i - 32);
                            const double ax = (halton(i + 101, 2) - 0.5) * 1.8, ay = (halton(i + 101, 3) - 0.5) * 1.8;
                            const double s = 0.016 * double(k);
                            put32(u.node, wc::layout::node_pos + 0,
                                  std::uint32_t(std::int32_t(ax / 1.6 * d * 500.0 + 581000.0 * 0.8 * s)));
                            put32(u.node, wc::layout::node_pos + 4,
                                  std::uint32_t(std::int32_t(ay / (1.6 * 5120.0 / 1440.0) * d * 500.0)));
                            put32(u.node, wc::layout::node_pos + 8, std::uint32_t(std::int32_t(d * 500.0 + 581000.0 * 0.6 * s)));
                            wc::projectile_tap(reinterpret_cast<std::uintptr_t>(u.obj), pool);
                        }
                        ps::FlashOut out;
                        out.pulses = pulses;
                        out.max_pulses = rr::weapon_max_pulses;
                        out.max_quads = rr::weapon_projectile_max_flashes;
                        x3m::engine_memory::next_frame();
                        if (k < 3) {
                            ps::build(pool, &x3m::engine_memory::read, nullptr, looks, frame0 + k, false, now, 16, 0.016f, 1.f,
                                      cview, tunes, cv, rr::weapon_projectile_max_bolts, rr::weapon_projectile_bolt_quads);
                        } else {
                            QueryPerformanceCounter(&a);
                            n = ps::build(pool, &x3m::engine_memory::read, nullptr, looks, frame0 + k, false, now, 16, 0.016f,
                                          1.f, cview, tunes, cv, rr::weapon_projectile_max_bolts,
                                          rr::weapon_projectile_bolt_quads, nullptr, variant >= 2 ? nullptr : &out);
                            QueryPerformanceCounter(&b);
                            cpu_best = std::min(cpu_best, ticks_us(a, b, freq));
                        }
                    }
                }
                rr::WeaponProjectileFrame f;
                f.width = ct.w;
                f.height = ct.h;
                f.vertices = cv;
                f.quads = n.quads + n.flashes;
                f.lane = ct.lane_s.p;
                f.depth = ct.depth_s.p;
                f.pulses = pulses;
                f.pulse_count = n.pulses;
                f.lane_texture = ct.lane_tex.p;
                f.four_channel = true;
                f.m00 = cview.m00;
                f.m11 = cview.m11;
                f.m22 = cview.m22;
                f.m32 = cview.m32;
                f.near_z = cview.near_z;
                bool drew = true;
                for (int rep = 0; rep < 60; ++rep) {
                    dev->BeginScene();
                    dev->SetDepthStencilSurface(ct.depth_s.p);
                    dev->SetRenderTarget(0, ct.scene_s.p);
                    dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0);
                    dev->SetDepthStencilSurface(nullptr);
                    fence(ev.p);
                    QueryPerformanceCounter(&a);
                    rr::WeaponEffectsReport r{};
                    const HRESULT hr = proj.run_projectiles(f, &r);
                    fence(ev.p);
                    QueryPerformanceCounter(&b);
                    dev->EndScene();
                    drew = drew && hr == S_OK && r.quads == f.quads && r.pulses == n.pulses;
                    gpu_best = std::min(gpu_best, ticks_us(a, b, freq));
                }
                device_guard("flash_budget_combined");
                std::printf("FLASH case=budget_combined tune=%s width=5120 height=1440 bolts=%u drawn=%u comets=%u quads=%u "
                            "flashes=%u pulses=%u pulse_clamped=%u statics=%u jumped=%u cpu_us=%.1f gpu_us=%.1f\n",
                            variant == 1   ? "comet"
                            : variant == 2 ? "baked_no_flash_work"
                            : variant == 3 ? "baked_448_bolts"
                                           : "baked",
                            pool.count, n.drawn, n.comets, n.quads, n.flashes, n.pulses, n.pulse_clamped, n.statics, n.jumped,
                            cpu_best, gpu_best);
                cpu_of[variant] = cpu_best;
                if (!variant) {
                    gpu_combined = gpu_best;
                    combined_ok = combined_ok && drew && n.drawn == rr::weapon_projectile_max_bolts &&
                                  n.flashes == rr::weapon_projectile_max_flashes && n.pulses == rr::weapon_max_pulses &&
                                  n.statics == 0 && n.jumped == 0;
                }
            }
            // The gate: GPU fenced <= 60 us for the whole frame and the flash work's CPU share (the frame less the same
            // frame without it) <= 20 us. The frame's CPU against the design's 100 us is reported: the bolt stage alone
            // takes the frame past it (the 64 just-fired bolts' first frames), the flash work does not.
            const double flash_cpu = cpu_of[0] - cpu_of[2], newborn_cpu = cpu_of[2] - cpu_of[3];
            std::printf("FLASH case=budget_combined_bounds cpu_us=%.1f cpu_within_100=%u flash_work_cpu_us=%.1f "
                        "newborn_bolts_cpu_us=%.1f bolts_448_cpu_us=%.1f gpu_us=%.1f gpu_within_60=%u\n",
                        cpu_of[0], unsigned(cpu_of[0] <= 100.0), flash_cpu, newborn_cpu, cpu_of[3], gpu_combined,
                        unsigned(gpu_combined <= 60.0));
            // the frame's CPU is a regression tripwire at 150 us (coordinator, 2026-11-09: 64 shots born in one frame is a
            // synthetic extreme like the turning worst case; the flight evidence for CPU is projectile_us_max in the frame
            // row); cpu_within_100 above is informational
            report("flash_budget_combined_512_bolts_64_flashes_64_pulses",
                   combined_ok && gpu_combined <= 60.0 && flash_cpu <= 20.0 && cpu_of[0] <= 150.0, "");
            ct.release();
        }
        ft.release();
    }
    pt.release();
    proj.detach();
    pass.detach();
    guard_next_section("end");
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
