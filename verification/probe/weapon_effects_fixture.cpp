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
// 4-neighbourhood), 12 hits per second without a pop on a fighter's and a capital's flat face (the capital's cells
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
                          const HullLane* hull = nullptr) {
    if (hull)
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
    std::printf("ATTACH result=%08lx shell_vs=%u shell_ps=%u decal_vs=%u decal_ps=%u fp16=%08lx max_ps=%lu patch_vertices=%u patch_triangles=%u\n",
                attached, pass.caps().shell_vs_slots, pass.caps().shell_ps_slots, pass.caps().decal_vs_slots,
                pass.caps().decal_ps_slots, pass.caps().fp16_blending,
                static_cast<unsigned long>(caps.MaxPixelShader30InstructionSlots), rr::WeaponEffectsPass::patch_vertices,
                rr::WeaponEffectsPass::patch_triangles);
    if (!pass.caps().enabled || !lane_ps) {
        std::printf("RESULT FAIL checks=%u failures=%u\n", checks, failures + 1);
        return 1;
    }
    std::printf("ATTACH_SLOTS shell_vs=%u shell_ps=%u decal_vs=%u decal_ps=%u patch_bytes=%u frame_patches_bytes=%u\n",
                pass.caps().shell_vs_slots, pass.caps().shell_ps_slots, pass.caps().decal_vs_slots,
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
    report("pixel_program_low_thousands", pass.caps().shell_ps_slots < 2000 && pass.caps().decal_ps_slots < 2000, "");
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
    // Placement on the hull (the screen-space patch, Run236 Flight C): through the production builder with the identity
    // camera, one hit 0.5 s old (every footprint cell lit), the lane holding the case's hull (an ellipsoid of the
    // owner's frame: the modelled half-extents, else a sphere through the hit point). Per case: lit pixels over sky
    // (must be 0), lit pixels whose surface point (the hull lane's CPU twin) lies farther than sqrt 2 x the extent from
    // the hit (in the lattice's plane and along its axis each within it; at most 1 % of the lit, the GPU's float), the coverage of the surface within half the reach of the
    // hit (lit / eligible, at least 0.9: those points lie in footprint cells), the luma-weighted centroid of the lit
    // surface points against the hit (within one cell), the lit count against its hull's reference (the narrow hull's
    // mid flank, the flat fighter's flank: a third to three times, every approach direction; the flat flank is 2 m
    // high, so less of it lies within the footprint than of the top). Narrow: a 100 m corvette (1:1:6,
    // R its 50 m half-length) on its flank 9 % aft, its flank, top and bottom 40 % aft; flat: a fighter of
    // half-extents 6:1:4 (12 x 2 x 8 m) on its flank, top and bottom 0.3 of each half-extent off centre, and its top hit
    // by a bolt 42 degrees below its plane (the Run236 case: the bolt's direction plays no part); station: the Flight B
    // station (R 1304 m) at its recorded contact point (0.676 R); far side: a fighter hit on the face turned away from the
    // eye (its own hull hides the patch: nothing draws); eye inside: the camera 0.6 R from a flat fighter's centre,
    // inside its bounding sphere and outside its hull (6:1:4), the hit on the nose in front of it, 4 m away.
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
        const std::int32_t ident[3][3] = {{65536, 0, 0}, {0, 65536, 0}, {0, 0, 65536}};
        const std::int32_t side[3][3] = {{0, 0, -65536}, {0, 65536, 0}, {65536, 0, 0}}; // local x -> view -z (the flank)
        const std::int32_t facing[3][3] = {{65536, 0, 0}, {0, -65536, 0}, {0, 0, -65536}}; // local +z -> view -z
        const std::int32_t top[3][3] = {{65536, 0, 0}, {0, 0, -65536}, {0, 65536, 0}};    // local +y -> view -z
        const std::int32_t bottom[3][3] = {{65536, 0, 0}, {0, 0, 65536}, {0, -65536, 0}}; // local -y -> view -z
        const float a6 = 250.f / 6.f;
        const double s84 = std::sqrt(1.0 - 0.16), s82 = std::sqrt(1.0 - 0.18);
        Placement cases[12] = {
            {"narrow_mid_flank", wc::patch_corvette, 250.f, 600.f, {eng(a6 * std::sqrt(1.0 - 0.09 * 0.09)), 0, eng(0.09 * 250.0)}, {}, {a6, a6, 250.f}, {-127, 0, 0}, 0},
            {"aft_flank", wc::patch_corvette, 250.f, 600.f, {eng(a6 * s84), 0, eng(0.4 * 250.0)}, {}, {a6, a6, 250.f}, {-127, 0, 0}, 0},
            {"y_top_narrow", wc::patch_corvette, 250.f, 600.f, {0, eng(a6 * s84), eng(0.4 * 250.0)}, {}, {a6, a6, 250.f}, {0, -127, 0}, 0},
            {"y_bottom_narrow", wc::patch_corvette, 250.f, 600.f, {0, -eng(a6 * s84), eng(0.4 * 250.0)}, {}, {a6, a6, 250.f}, {0, 127, 0}, 0},
            {"flat_flank", wc::patch_fighter, 60.f, 400.f, {eng(60.0 * std::sqrt(1.0 - 0.09)), 0, eng(0.3 * 40.0)}, {}, {60.f, 10.f, 40.f}, {-127, 0, 0}, 4},
            {"y_top_flat", wc::patch_fighter, 60.f, 400.f, {eng(0.3 * 60.0), eng(10.0 * s82), eng(0.3 * 40.0)}, {}, {60.f, 10.f, 40.f}, {0, -127, 0}, 4},
            {"y_bottom_flat", wc::patch_fighter, 60.f, 400.f, {eng(0.3 * 60.0), -eng(10.0 * s82), eng(0.3 * 40.0)}, {}, {60.f, 10.f, 40.f}, {0, 127, 0}, 4},
            {"y_top_flat_42deg", wc::patch_fighter, 60.f, 400.f, {eng(0.3 * 60.0), eng(10.0 * s82), eng(0.3 * 40.0)}, {}, {60.f, 10.f, 40.f}, {0, -85, 95}, 4},
            {"station_flight_b_0p68R", wc::patch_capital, 6517.64f, 14000.f, {-114699, 85160, 416994}, {}, {}, {0, 0, -127}, 8},
            {"far_side", wc::patch_fighter, 100.f, 400.f, {0, 0, eng(60.0)}, {}, {}, {0, 0, -127}, 9},
            {"eye_inside_sphere", wc::patch_fighter, 100.f, 60.f, {0, 0, -eng(40.0)}, {}, {60.f, 10.f, 40.f}, {0, 0, 127}, 10},
            // review F1: the flat fighter's hull centred 2 cells (6 m) below the owner's origin, hit on its top (an upward
            // surface below the origin's plane): lights like the centred top
            {"y_top_flat_offcentre", wc::patch_fighter, 60.f, 400.f,
             {eng(0.3 * 60.0), eng(-30.0 + 10.0 * s82), eng(0.3 * 40.0)}, {}, {60.f, 10.f, 40.f}, {0, -127, 0}, 5,
             {0.f, -30.f, 0.f}}};
        const std::int32_t (*rows_of[12])[3] = {side, side, top, bottom, side, top, bottom, top, facing, ident, ident, top};
        for (unsigned k = 0; k < 12; ++k) std::memcpy(cases[k].rows, rows_of[k], sizeof ident);
        struct Result {
            unsigned lit = 0, sky_lit = 0, outside = 0, eligible = 0, covered = 0, patches = 0;
            double centroid_cells = 1e30, lit_ratio = 0;
            bool drew = false;
        } res[12];
        for (unsigned i = 0; i < 12; ++i) {
            const Placement& c = cases[i];
            rr::WeaponPatch patches[wc::max_patches];
            rr::WeaponDecal decals[wc::max_decals];
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
            const ws::Counts n = ws::build(stage_ring, 1500, &fake_read, identity_camera(), ws::Tuning{}, patches,
                                           wc::max_patches, decals, wc::max_decals);
            double hp[3], pr = 0;
            for (unsigned k = 0; k < 3; ++k) {
                hp[k] = double(c.pos[k]) * double(wc::world_scale);
                pr += hp[k] * hp[k];
            }
            pr = std::sqrt(pr);
            const double ext[3] = {c.ext[0] > 0.f ? double(c.ext[0]) : pr, c.ext[0] > 0.f ? double(c.ext[1]) : pr,
                                   c.ext[0] > 0.f ? double(c.ext[2]) : pr};
            const double off[3] = {c.off[0], c.off[1], c.off[2]};
            const HullLane hull = staged_hull(ext, off);
            const std::vector<float> l = render(pass, patches, n.patches, nullptr, nullptr, &hull);
            const double extent = n.patches ? double(patches[0].extent) : 0.0, cell = n.patches ? double(patches[0].cell) : 1.0;
            const double core = n.patches ? 0.5 * double(patches[0].reach) : 0.0; // surely inside the footprint's cells
            Result& o = res[i];
            o.patches = n.patches;
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
        for (unsigned i = 0; i < 12; ++i) {
            const Result& o = res[i];
            const double ratio = res[cases[i].ref].lit ? double(o.lit) / double(res[cases[i].ref].lit) : 0.0;
            res[i].lit_ratio = ratio;
            std::printf("PLACEMENT case=%s patches=%u lit_pixels=%u sky_lit=%u outside_lit=%u eligible=%u covered=%u "
                        "coverage=%.3f centroid_error_cells=%.3f ref=%s lit_ratio=%.3f dir=%d,%d,%d\n",
                        cases[i].name, o.patches, o.lit, o.sky_lit, o.outside, o.eligible, o.covered,
                        o.eligible ? double(o.covered) / double(o.eligible) : 0.0, o.centroid_cells, cases[cases[i].ref].name,
                        ratio, int(cases[i].dir[0]), int(cases[i].dir[1]), int(cases[i].dir[2]));
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
        report("placement_narrow_hull_flank_aft_top_bottom_on_the_hull", on_hull(0) && on_hull(1) && on_hull(2) && on_hull(3), "");
        report("placement_flat_fighter_flank_top_bottom_42deg_on_the_hull",
               on_hull(4) && on_hull(5) && on_hull(6) && on_hull(7) && res[7].lit == res[5].lit, "");
        report("placement_station_at_the_hit", on_hull(8), "");
        report("placement_far_side_hidden_by_its_hull", res[9].lit == 0 && res[9].eligible == 0 && res[9].patches >= 1, "");
        report("placement_eye_inside_sphere_draws", on_hull(10), "");
        report("placement_offcentre_origin_top_lights", on_hull(11) && res[11].lit_ratio >= 0.7, "");
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
        unsigned dimmed[5] = {}, drawn[5] = {};
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
            rr::WeaponEffectsFrame f = base_frame(g);
            f.patches = patches;
            f.patch_count = n.patches;
            const double sphere[3] = {100.0, 100.0, 100.0};
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
               drawn[0] && drawn[1] && drawn[2] && drawn[3] && drawn[4] && dimmed[0] == 0 && dimmed[1] == 5 && dimmed[2] == 0 &&
                   dimmed[3] == 0 && dimmed[4] == 0 && std::fabs(ratio - double(wc::own_cockpit_gain)) < 0.01 &&
                   sums[2] == sums[0] && sums[3] == sums[0] && sums[4] == sums[0],
               "");
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
            else {
                const double r3[3] = {owner_r, owner_r, owner_r};
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
            const double sphere[3] = {r, r, r};
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
        const double sphere[3] = {100.0, 100.0, 100.0};
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
        report("stage_build_live_owner_draws", live.patches == 5 && drawn.second.drew && live_sum > 1.0, "");
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
            plane_lane(g, p);
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
        const HullLane spread_hull = staged_hull(spread_r); // the capital's sphere: the hits' surface
        unsigned spread_cells = 0;
        for (unsigned i = 0; i < spread_count; ++i) spread_cells += spread[i].cells;
        std::printf("TIMING_MAPS fighter_12hps_cells=%u capital_full_map_cells=%u capital_fill_map_cells=%u capital_spread_draws=%u capital_spread_cells=%u\n",
                    fighter_burst[0].cells, capital_map[0].cells, capital_fill_map[0].cells, spread_count, spread_cells);
        struct Case {
            const char* name;
            const rr::WeaponPatch* patches;
            unsigned count;
            const HullLane* hull; // the surface (null: the first patch's plane)
        } cases[] = {{"fence", nullptr, 0, nullptr},
                     {"fighter_two_hits_one_region", fighter_one, 1, nullptr},
                     {"fighter_two_regions", fighter_regions, 2, nullptr},
                     {"capital_close_two_hits_one_region", capital_one, 1, nullptr},
                     {"capital_close_two_regions", capital_regions, 2, nullptr}, {"worst_four_regions", worst, 4, nullptr},
                     {"fighter_12hps_map", fighter_burst, 1, nullptr}, {"capital_close_full_map", capital_map, 1, nullptr},
                     {"capital_fill_full_map", capital_fill_map, 1, nullptr},
                     {"capital_spread_spill", spread, spread_count, &spread_hull}};
        Com<IDirect3DQuery9> ev, occlusion;
        dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev.p);
        dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, &occlusion.p);
        LARGE_INTEGER freq, a, b;
        QueryPerformanceFrequency(&freq);
        for (const Case& c : cases) {
            rr::WeaponEffectsFrame f = base_frame(tt);
            f.patches = c.patches;
            f.patch_count = c.count;
            if (c.hull)
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
