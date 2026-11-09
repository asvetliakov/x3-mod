// The projectile stage's look frames (docs/architecture/projectile-look.md, "Implementation (2026-11-08)"): a standalone
// native D3D9 program beside weapon_effects_fixture.cpp (whose record binds its source), built and run only by
// run_weapon_effects.py --host native --dump DIR. It creates a plain HAL device, attaches the production
// WeaponEffectsPass with the projectile programs and draws through the production builder and run_projectiles; each
// frame's FP16 scene is written as linear RGB PFM <dir>\<name>.pfm, which the runner encodes as PNG. No check, no record.
// Never launches the game.
#include <windows.h>
#include <d3d9.h>
#include <d3dx9shader.h>
#include "../../src/renderer/weapon_effects_pass.h"
#include "../../src/proxy/weapon_projectile_stage.h"
#include "weapon_bullet_reshade_fixture.h"
#include "weapon_muzzle_reshade_fixture.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace rr = x3m::renderer;
namespace wc = x3m::weapon_effects::core;

namespace {
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
// float -> IEEE half (round to nearest; the dump's small positive radiances only, no NaN).
std::uint16_t float_to_half(float f) {
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    const std::uint32_t sign = (b >> 16) & 0x8000u;
    const int e = int((b >> 23) & 0xffu) - 127 + 15;
    std::uint32_t m = b & 0x7fffffu;
    if (e <= 0) return std::uint16_t(sign); // under the half's normal range: 0
    if (e >= 31) return std::uint16_t(sign | 0x7c00u);
    m += 0x1000u; // round
    if (m & 0x800000u) return std::uint16_t(sign | std::uint32_t(e + 1) << 10);
    return std::uint16_t(sign | std::uint32_t(e) << 10 | (m >> 13));
}
IDirect3DDevice9* dev = nullptr;
D3DCAPS9 caps{};
D3DFORMAT adapter_format = D3DFMT_X8R8G8B8;
void device_guard(const char*) {}
namespace ps = x3m::weapon_effects::projectile_stage;
namespace pf = x3m::weapon_effects::projectile_family;
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
// ------------------------------------------------------------------ projectile look dump (--dump <dir>)
// The look frames of the projectile stage (docs/architecture/projectile-look.md, "Implementation"): the production
// builder and run_projectiles at 1920x1080 and the fixture's projection (m00 1.6), the IRE row's look and the
// fighter-tracer family's baked Tune, on black with a dim hull panel (linear 0.02) in the lower right. Each frame's
// FP16 scene is written as linear RGB PFM <dir>\<name>.pfm (run_weapon_effects.py --dump encodes the PNGs); no check
// and no record. Each frame is the last of consecutive stage frames through the production builder (the pool, the
// bridge and the comet's ring, FL 60 A; a plain reader), so each bolt is its comet: the segment from the previous head
// and the trail behind it, past length_cap_m dimmed by cap / travel. Frames: near (IRE at 200 m, 500 m, 1 km, the
// fourth of 17 ms steps), seta (the same, the second of 100 ms steps: SETA 6), lod (3 km beside 300 m, 17 ms), comet-0
// to comet-2 (one IRE at 500 m, the third to fifth of 17 ms steps).
bool write_pfm(const char* path, ProjTarget& t) {
    if (FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) return false;
    D3DLOCKED_RECT lr{};
    if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
    std::FILE* f = std::fopen(path, "wb");
    bool ok = f != nullptr;
    if (ok) {
        std::fprintf(f, "PF\n%u %u\n-1.0\n", t.w, t.h);
        std::vector<float> row(std::size_t(t.w) * 3);
        for (UINT y = t.h; y-- > 0;) { // PFM rows run bottom to top
            const auto* src = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) + y * lr.Pitch);
            for (UINT x = 0; x < t.w; ++x)
                for (unsigned k = 0; k < 3; ++k) row[std::size_t(x) * 3 + k] = half_to_float(src[x * 4 + k]);
            ok = ok && std::fwrite(row.data(), sizeof(float), row.size(), f) == row.size();
        }
        std::fclose(f);
    }
    t.scene_sys.p->UnlockRect();
    return ok;
}
void put32(unsigned char* base, unsigned off, std::uint32_t v) { std::memcpy(base + off, &v, 4); }
bool plain_read(std::uintptr_t address, void* out, std::size_t size) {
    std::memcpy(out, reinterpret_cast<const void*>(address), size);
    return true;
}
struct alignas(16) DumpBullet {
    unsigned char obj[0x80], node[0x150], ext[0x10];
};
struct Bolt { float d_m, ndc_x, ndc_y; };
struct Frame {
    const char* name;
    std::uint32_t step_ms;
    unsigned frames, count, dumped; // stage frames run; bolts; the last `dumped` frames are written (name, name-1, ...)
    Bolt bolts[3];
};
int projectile_dump(const char* dir) {
    rr::WeaponEffectsPass proj;
    proj.configure_projectiles(true);
    proj.attach(dev, *reinterpret_cast<void* const* const*>(dev), caps, adapter_format);
    ProjTarget t;
    if (!proj.caps().projectile_enabled || !make_proj_target(t, 1920, 1080)) {
        std::printf("DUMP_FAIL reason=%s\nRESULT FAIL checks=1 failures=1\n", proj.caps().projectile_reason);
        return 1;
    }
    CreateDirectoryA(dir, nullptr);
    const ps::View view = proj_view(t, 1.6f);
    const pf::Tune tunes[pf::family_count] = {pf::families[pf::fighter_tracer].tune};
    // near, seta and lod: the last of 4 (60 fps) or 2 (SETA 6) frames; comet: one IRE at 500 m, the frames 3, 4 and 5 of
    // five (the ring full), each written
    const Frame frames[4] = {{"near", 17, 4, 3, 1, {{200.f, -0.35f, 0.25f}, {500.f, 0.f, 0.f}, {1000.f, 0.35f, -0.25f}}},
                             {"seta", 100, 2, 3, 1, {{200.f, -0.35f, 0.25f}, {500.f, 0.f, 0.f}, {1000.f, 0.35f, -0.25f}}},
                             {"lod", 17, 4, 2, 1, {{3000.f, -0.2f, 0.f}, {300.f, 0.2f, 0.f}}},
                             {"comet", 17, 5, 1, 3, {{500.f, 0.1f, 0.f}}}};
    static wc::ProjectilePool pool;
    static DumpBullet bullets[3];
    alignas(16) static unsigned char cfg[0x800];
    static std::uint32_t cfg_slot;
    static ps::Looks looks;
    static rr::WeaponProjectileVertex v[3 * rr::weapon_projectile_quads_per_bolt * 4];
    unsigned failures = 0, written = 0, device_frame = 1;
    for (const Frame& fr : frames) {
        wc::projectile_clear(pool);
        pool.armed = 1;
        pool.resolved = 1;
        pool.body_count = 1;
        pool.bodies[0].id = 0x55;
        pool.bits[0x55 >> 5] |= 1u << (0x55 & 31u);
        cfg_slot = std::uint32_t(reinterpret_cast<std::uintptr_t>(cfg));
        pool.globals.cfg_slot = reinterpret_cast<std::uintptr_t>(&cfg_slot);
        looks.look[0] = ire_look(); // the IRE row's look (no table here)
        looks.resolved = 1;
        // the IRE (581,000 engine units/s) mostly across the view, receding: 0.8 along +x, 0.6 along +z
        const double vx = 0.8 * 581000.0, vz = 0.6 * 581000.0, step_s = double(fr.step_ms) * 1e-3;
        for (unsigned f = 0; f < fr.frames; ++f) {
            const std::uint32_t now = 5000u + f * fr.step_ms;
            put32(cfg, wc::layout::cfg_game_ms, now);
            for (unsigned i = 0; i < fr.count; ++i) {
                const Bolt& b = fr.bolts[i];
                DumpBullet& u = bullets[i];
                const double z = double(b.d_m) * 500.0, back = step_s * double(fr.frames - 1 - f); // engine units, s
                const std::int32_t pos[3] = {std::int32_t(double(b.ndc_x) * z / double(view.m00) - vx * back),
                                             std::int32_t(double(b.ndc_y) * z / double(view.m11)),
                                             std::int32_t(z - vz * back)};
                if (!f) {
                    std::memset(&u, 0, sizeof u);
                    put32(u.obj, wc::layout::obj_node, std::uint32_t(reinterpret_cast<std::uintptr_t>(u.node)));
                    put32(u.obj, wc::layout::obj_ext, std::uint32_t(reinterpret_cast<std::uintptr_t>(u.ext)));
                    put32(u.node, wc::layout::node_live, 1);
                    put32(u.node, wc::layout::node_handle, 50 + i);
                    put32(u.node, wc::layout::node_body, 0x55);
                    put32(u.node, wc::projectile::node_velocity + 0, std::uint32_t(std::int32_t(vx)));
                    put32(u.node, wc::projectile::node_velocity + 8, std::uint32_t(std::int32_t(vz)));
                    put32(u.ext, wc::projectile::ext_expiry, 100000000);
                    put32(u.ext, wc::projectile::ext_created, 1);
                }
                for (unsigned k = 0; k < 3; ++k) put32(u.node, wc::layout::node_pos + 4 * k, std::uint32_t(pos[k]));
                wc::projectile_tap(reinterpret_cast<std::uintptr_t>(u.obj), pool);
            }
            ps::BoltShape shapes[3];
            const ps::Counts n = ps::build(pool, &plain_read, nullptr, looks, device_frame++, false, now, fr.step_ms,
                                           1.f / 60.f, 1.f, view, tunes, v, 3, 3 * rr::weapon_projectile_quads_per_bolt, shapes);
            const unsigned from_end = fr.frames - 1 - f;
            if (from_end >= fr.dumped) continue;
            for (unsigned i = 0; i < n.drawn; ++i)
                std::printf("DUMP_BOLT frame=%s stage_frame=%u distance_m=%.0f core_px=%.2f dim=%.3f length_px=%.1f drawn_px=%.1f "
                            "streak_m=%.1f energy=%.3f glow_sigma_px=%.2f reach_px=%.2f quads=%u trail_ms=%.1f head_gain=%.2f dot=%u\n",
                            fr.name, f, double(shapes[i].depth_m), double(shapes[i].core_px), double(shapes[i].dim),
                            double(shapes[i].length_px), double(shapes[i].drawn_px), double(shapes[i].streak_m),
                            double(shapes[i].energy), double(shapes[i].glow_sigma_px), double(shapes[i].ext_px),
                            shapes[i].quads, double(shapes[i].trail_age_s) * 1e3, double(shapes[i].head_gain),
                            unsigned(shapes[i].dot));
            dev->BeginScene();
            dev->SetDepthStencilSurface(t.depth_s.p);
            dev->SetRenderTarget(0, t.lane_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
            dev->SetRenderTarget(0, t.scene_s.p);
            dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0);
            const D3DRECT hull{LONG(t.w * 0.62f), LONG(t.h * 0.58f), LONG(t.w), LONG(t.h)};
            dev->Clear(1, &hull, D3DCLEAR_TARGET, D3DCOLOR_ARGB(0, 5, 5, 6), 1.f, 0); // a dim hull panel, linear ~0.02
            dev->SetDepthStencilSurface(nullptr);
            rr::WeaponProjectileFrame pf_frame;
            pf_frame.width = t.w;
            pf_frame.height = t.h;
            pf_frame.vertices = v;
            pf_frame.quads = n.quads;
            pf_frame.lane = t.lane_s.p;
            pf_frame.depth = t.depth_s.p;
            rr::WeaponEffectsReport r{};
            const HRESULT hr = proj.run_projectiles(pf_frame, &r);
            dev->EndScene();
            char name[64], path[MAX_PATH];
            if (fr.dumped > 1) std::snprintf(name, sizeof name, "%s-%u", fr.name, fr.dumped - 1 - from_end);
            else std::snprintf(name, sizeof name, "%s", fr.name);
            std::snprintf(path, sizeof path, "%s\\%s.pfm", dir, name);
            const bool ok = hr == S_OK && n.drawn == fr.count && r.quads == n.quads && write_pfm(path, t);
            failures += !ok;
            ++written;
            std::printf("DUMP name=%s bolts=%u quads=%u step_ms=%u result=%08lx written=%u\n", name, n.drawn, n.quads,
                        unsigned(fr.step_ms), static_cast<unsigned long>(hr), unsigned(ok));
        }
    }
    // ---- ire-muzzle (docs/architecture/muzzle-flash-look.md): the chase view of a fighter at rest from behind (the eye
    // 24 m behind and 5 m above its centre; the hull an ellipsoid of half-extents 6 x 1.5 x 9 m, in shadow, 0.03 to 0.08
    // linear, written into the scene and the lane on the CPU), its two IRE mounts 3.2 m out and 1.5 m behind the
    // centre on the hull's top firing forward together; the bullets first tapped one 17 ms step after the shot (the
    // design's unknown: the tap's age), so this stage frame is each flash's first (F = 1, at the birth point by the
    // back-extrapolation) beside its bolt's first segment, with the hull pulses (run_projectiles, before the bolts). The
    // depth surface stays clear (the hull does not occlude the bolts here).
    {
        wc::projectile_clear(pool);
        pool.armed = 1;
        pool.resolved = 1;
        pool.body_count = 1;
        pool.bodies[0].id = 0x55;
        pool.bits[0x55 >> 5] |= 1u << (0x55 & 31u);
        for (wc::MuzzleRecord& m : pool.muzzles) m = wc::MuzzleRecord{};
        cfg_slot = std::uint32_t(reinterpret_cast<std::uintptr_t>(cfg));
        pool.globals.cfg_slot = reinterpret_cast<std::uintptr_t>(&cfg_slot);
        looks.look[0] = ire_look();
        looks.resolved = 1;
        const std::uint32_t born = 8000, now = born + 17;
        put32(cfg, wc::layout::cfg_game_ms, now);
        put32(cfg, wc::projectile::cfg_step_ms, 17);
        alignas(16) static unsigned char ship[0x100], ship_node[0x200];
        std::memset(ship, 0, sizeof ship);
        std::memset(ship_node, 0, sizeof ship_node);
        put32(ship, wc::layout::obj_node, std::uint32_t(reinterpret_cast<std::uintptr_t>(ship_node)));
        put32(ship_node, wc::layout::node_live, 1);
        put32(ship_node, wc::layout::node_handle, 0x77);
        for (unsigned k = 0; k < 3; ++k) put32(ship_node, wc::layout::node_rot + 16 * k + 4 * k, 0x10000);
        const double centre[3] = {0.0, -5.0, 24.0}, half[3] = {6.0, 1.5, 9.0}; // metres (view = world here)
        for (unsigned k = 0; k < 3; ++k) put32(ship_node, wc::layout::node_pos + 4 * k, std::uint32_t(std::int32_t(centre[k] * 500.0)));
        const double speed = 581000.0, age_s = double(now - born) * 1e-3;
        for (unsigned i = 0; i < 2; ++i) {
            DumpBullet& u = bullets[i];
            std::memset(&u, 0, sizeof u);
            const double mount[3] = {i ? 3.2 : -3.2, 1.25, -1.5}; // on the top surface, in view of the chase eye
            put32(u.obj, wc::layout::obj_node, std::uint32_t(reinterpret_cast<std::uintptr_t>(u.node)));
            put32(u.obj, wc::layout::obj_ext, std::uint32_t(reinterpret_cast<std::uintptr_t>(u.ext)));
            put32(u.node, wc::layout::node_live, 1);
            put32(u.node, wc::layout::node_handle, 90 + i);
            put32(u.node, wc::layout::node_body, 0x55);
            put32(u.node, wc::projectile::node_velocity + 8, std::uint32_t(std::int32_t(speed)));
            for (unsigned k = 0; k < 3; ++k)
                put32(u.node, wc::layout::node_pos + 4 * k,
                      std::uint32_t(std::int32_t((centre[k] + mount[k]) * 500.0 + (k == 2 ? speed * age_s : 0.0))));
            put32(u.ext, wc::projectile::ext_expiry, born + 1200);
            put32(u.ext, wc::projectile::ext_owner, std::uint32_t(reinterpret_cast<std::uintptr_t>(ship)));
            put32(u.ext, wc::projectile::ext_created, born);
            wc::MuzzleRecord& m = pool.muzzles[i];
            m.ship = std::uint32_t(reinterpret_cast<std::uintptr_t>(ship));
            m.node = std::uint32_t(reinterpret_cast<std::uintptr_t>(ship_node));
            m.handle = 0x77;
            m.t0 = born;
            m.slot = std::uint16_t(i);
            m.ours = 1;
            m.sequence = 1 + i;
            wc::projectile_tap(reinterpret_cast<std::uintptr_t>(u.obj), pool);
        }
        // the hull: the lane's view depth and the scene's shading per pixel (the ray's near root; sky elsewhere)
        bool hull_ok = true;
        D3DLOCKED_RECT ll{}, sl{};
        if (SUCCEEDED(t.lane_sys.p->LockRect(&ll, nullptr, 0)) && SUCCEEDED(t.scene_sys.p->LockRect(&sl, nullptr, 0))) {
            const double l_raw[3] = {0.3, 1.0, -0.5}, ln = std::sqrt(0.09 + 1.0 + 0.25);
            for (UINT y = 0; y < t.h; ++y) {
                float* lane = reinterpret_cast<float*>(static_cast<unsigned char*>(ll.pBits) + y * ll.Pitch);
                std::uint16_t* scene = reinterpret_cast<std::uint16_t*>(static_cast<unsigned char*>(sl.pBits) + y * sl.Pitch);
                for (UINT x = 0; x < t.w; ++x) {
                    const double d[3] = {(2.0 * x / t.w - 1.0) / double(view.m00), (1.0 - 2.0 * y / t.h) / double(view.m11), 1.0};
                    double A = 0, B = 0, C = -1;
                    for (unsigned k = 0; k < 3; ++k) {
                        const double c = centre[k] * 5.0, e = half[k] * 5.0;
                        A += d[k] * d[k] / (e * e);
                        B += -d[k] * c / (e * e);
                        C += c * c / (e * e);
                    }
                    const double disc = B * B - A * C;
                    float shade = 0.f, depth = -1.f;
                    if (disc >= 0.0) {
                        const double tt = (-B - std::sqrt(disc)) / A;
                        if (tt > 0.0) {
                            double n[3], nn = 0.0, dot = 0.0;
                            for (unsigned k = 0; k < 3; ++k) {
                                const double e = half[k] * 5.0;
                                n[k] = (tt * d[k] - centre[k] * 5.0) / (e * e);
                                nn += n[k] * n[k];
                            }
                            for (unsigned k = 0; k < 3; ++k) dot += n[k] / std::sqrt(nn) * l_raw[k] / ln;
                            shade = float(0.03 + 0.05 * (dot > 0.0 ? dot : 0.0));
                            depth = float(tt);
                        }
                    }
                    lane[x * 4 + 0] = depth > 0.f ? 0.5f : -1.f;
                    lane[x * 4 + 1] = 0.f;
                    lane[x * 4 + 2] = depth > 0.f ? depth : 0.f;
                    lane[x * 4 + 3] = 0.f;
                    const std::uint16_t hs = float_to_half(shade), hb = float_to_half(shade * 1.08f);
                    scene[x * 4 + 0] = hs;
                    scene[x * 4 + 1] = hs;
                    scene[x * 4 + 2] = hb;
                    scene[x * 4 + 3] = 0;
                }
            }
            t.lane_sys.p->UnlockRect();
            t.scene_sys.p->UnlockRect();
            hull_ok = SUCCEEDED(dev->UpdateSurface(t.lane_sys.p, nullptr, t.lane_s.p, nullptr)) &&
                      SUCCEEDED(dev->UpdateSurface(t.scene_sys.p, nullptr, t.scene_s.p, nullptr));
        } else {
            hull_ok = false;
        }
        const pf::Tune tunes1[pf::family_count] = {pf::families[pf::fighter_tracer].tune};
        static rr::WeaponPulse pulses[rr::weapon_max_pulses];
        static ps::FlashShape fshapes[rr::weapon_projectile_max_flashes];
        static rr::WeaponProjectileVertex mv[(2 * rr::weapon_projectile_quads_per_bolt + rr::weapon_projectile_max_flashes) * 4];
        ps::FlashOut out;
        out.pulses = pulses;
        out.max_pulses = rr::weapon_max_pulses;
        out.max_quads = rr::weapon_projectile_max_flashes;
        out.shapes = fshapes;
        const ps::Counts n = ps::build(pool, &plain_read, nullptr, looks, device_frame += 5, false, now, 17, 1.f / 60.f, 1.f,
                                       view, tunes1, mv, 2, 2 * rr::weapon_projectile_quads_per_bolt, nullptr, &out);
        for (unsigned i = 0; i < n.flashes; ++i)
            std::printf("DUMP_FLASH frame=ire-muzzle index=%u depth_m=%.1f core_px=%.2f core_white_px=%.2f white_px=%.2f "
                        "sigma_px=%.2f peak=%.2f folded=%u length_px=%.2f pulse_px=%.1f raster=%.1f,%.1f\n",
                        i, double(fshapes[i].depth_m), double(fshapes[i].core_px), double(fshapes[i].core_white_px),
                        double(fshapes[i].white_px), double(fshapes[i].sigma_px), double(fshapes[i].peak),
                        unsigned(fshapes[i].folded), double(fshapes[i].length_px), double(fshapes[i].pulse_px),
                        double(fshapes[i].raster[0]), double(fshapes[i].raster[1]));
        dev->BeginScene();
        dev->SetDepthStencilSurface(t.depth_s.p);
        dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.f, 0);
        dev->SetRenderTarget(0, t.scene_s.p);
        dev->SetDepthStencilSurface(nullptr);
        // one run_projectiles: the pulses first (the lane a texture), then the bolts and flashes (the lane as RT1)
        rr::WeaponProjectileFrame pf_frame;
        pf_frame.width = t.w;
        pf_frame.height = t.h;
        pf_frame.vertices = mv;
        pf_frame.quads = n.quads + n.flashes;
        pf_frame.lane = t.lane_s.p;
        pf_frame.depth = t.depth_s.p;
        pf_frame.pulses = pulses;
        pf_frame.pulse_count = n.pulses;
        pf_frame.lane_texture = t.lane_tex.p;
        pf_frame.four_channel = true;
        pf_frame.m00 = view.m00;
        pf_frame.m11 = view.m11;
        pf_frame.m22 = view.m22;
        pf_frame.m32 = view.m32;
        pf_frame.near_z = view.near_z;
        rr::WeaponEffectsReport r{};
        const HRESULT hr = proj.run_projectiles(pf_frame, &r);
        dev->EndScene();
        char path[MAX_PATH];
        std::snprintf(path, sizeof path, "%s\\ire-muzzle.pfm", dir);
        const bool ok = hull_ok && hr == S_OK && n.drawn == 2 && n.flashes == 2 && n.pulses == 2 && r.pulses == 2 &&
                        write_pfm(path, t);
        failures += !ok;
        ++written;
        std::printf("DUMP name=ire-muzzle bolts=%u quads=%u flashes=%u pulses=%u drawn_pulses=%u result=%08lx hull=%u "
                    "written=%u\n",
                    n.drawn, n.quads, n.flashes, n.pulses, r.pulses, static_cast<unsigned long>(hr), unsigned(hull_ok),
                    unsigned(ok));
    }
    t.release();
    proj.detach();
    std::printf("RESULT %s checks=%u failures=%u\n", failures ? "FAIL" : "PASS", written, failures);
    return failures ? 1 : 0;
}
// ------------------------------------------------------------------ bullet re-shade dump (--dump DIR --dump-case reshade)
// The bullet re-shade's look evidence (bullet-draw-batch.md section 6; weapon_bullet_reshade_fixture.h): the IRE bolt
// drawn the way the game's INSTANCE_BULLETS pass draws it (stand-in vs_1_1 / ps_1_1 doing the game programs' work,
// ONE / INVSRCCOLOR, Z test, no Z write) into the 1920x1080 FP16 scene at the fixture's projection (m00 1.6), once
// vanilla and once with the production re-shade program swapped in, at 50 m (near) and 1500 m (far), its axis 25
// degrees out of the screen plane and its fins rolled 20 degrees, on the dark background (black) and a bright nebula
// stand-in (a flat linear 0.30 / 0.22 / 0.38). The mesh and texture are the game's IRE bolt and fx_bullets_diff when
// run_weapon_effects.py prepared them from the local lab assets (untracked), else the procedural stand-ins. Each
// output PFM <dir>\fixture-ire-{vanilla,reshade}-{near,far}.pfm is the two backgrounds' crops side by side (near
// 640x256, far 64x32 around the bolt). DUMP_RESHADE rows; no check, no record.
namespace rf = reshade_fx;
bool load_assets(const char* dir, rf::Mesh* mesh, std::vector<unsigned char>* rgba, unsigned* side,
                 float* p99 = nullptr) {
    if (!dir || !*dir) return false;
    char path[MAX_PATH];
    std::snprintf(path, sizeof path, "%s\\ire_mesh.txt", dir);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    rf::Mesh m;
    float q[5];
    while (m.n < 66 && std::fscanf(f, "%f %f %f %f %f", &q[0], &q[1], &q[2], &q[3], &q[4]) == 5) {
        for (unsigned k = 0; k < 5; ++k) m.p[m.n][k] = q[k];
        ++m.n;
    }
    std::fclose(f);
    std::snprintf(path, sizeof path, "%s\\ire_tex.rgba", dir);
    f = std::fopen(path, "rb");
    if (!f || m.n != 66) {
        if (f) std::fclose(f);
        return false;
    }
    std::vector<unsigned char> data(512u * 512u * 4u);
    const bool read = std::fread(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    if (!read) return false;
    *mesh = m;
    rgba->swap(data);
    *side = 512;
    if (p99) { // the texture's level-0 luminance p99 (0: none, the IRE's 2.75)
        *p99 = 0.f;
        std::snprintf(path, sizeof path, "%s\\ire_tex_p99.txt", dir);
        if ((f = std::fopen(path, "rb")) != nullptr) {
            if (std::fscanf(f, "%f", p99) != 1) *p99 = 0.f;
            std::fclose(f);
        }
    }
    return true;
}
// edge: an rs_edge override (null or empty: the baked tune); with it the re-shaded frames only, named
// fixture-ire-reshade-<range>-e<edge x 100, three digits> (the vanilla frames are the same as without it).
int reshade_dump(const char* dir, const char* d3dx_path, const char* assets, const char* edge = nullptr) {
    HMODULE d3dx = LoadLibraryA(d3dx_path && *d3dx_path ? d3dx_path : "d3dx9_37.dll");
    using CompileFn = HRESULT(WINAPI*)(LPCSTR, UINT, const void*, void*, LPCSTR, LPCSTR, DWORD, ID3DXBuffer**,
                                       ID3DXBuffer**, void*);
    const auto compile_fn = d3dx ? reinterpret_cast<CompileFn>(GetProcAddress(d3dx, "D3DXCompileShader")) : nullptr;
    const auto compile = [&](const char* src, const char* profile, ID3DXBuffer** out) {
        ID3DXBuffer* err = nullptr;
        const HRESULT hr = compile_fn(src, UINT(std::strlen(src)), nullptr, nullptr, "main", profile, 0, out, &err, nullptr);
        if (err) err->Release();
        return SUCCEEDED(hr) && *out;
    };
    ID3DXBuffer *vs_code = nullptr, *ps_code = nullptr;
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> vanilla, ours;
    Com<IDirect3DVertexDeclaration9> decl;
    const bool created = compile_fn && compile(rf::vs_source, "vs_1_1", &vs_code) &&
                         rf::assemble(d3dx, rf::vanilla_ps_asm, &ps_code) &&
                         SUCCEEDED(dev->CreateVertexShader(static_cast<const DWORD*>(vs_code->GetBufferPointer()), &vs.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(static_cast<const DWORD*>(ps_code->GetBufferPointer()), &vanilla.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(rf::reshade_program, &ours.p)) &&
                         SUCCEEDED(dev->CreateVertexDeclaration(rf::declaration(), &decl.p));
    if (vs_code) vs_code->Release();
    if (ps_code) ps_code->Release();
    ProjTarget t;
    if (!created || !make_proj_target(t, 1920, 1080)) {
        std::printf("DUMP_FAIL reason=reshade_programs\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    CreateDirectoryA(dir, nullptr);
    rf::Mesh mesh;
    std::vector<unsigned char> rgba;
    unsigned side = 0;
    float ire_p99 = 0.f;
    const bool real = load_assets(assets, &mesh, &rgba, &side, &ire_p99);
    if (!real) {
        mesh = rf::standin_mesh();
        rgba = rf::standin_texture(&side);
    }
    Com<IDirect3DTexture9> tex;
    tex.p = rf::make_texture(dev, rgba.data(), side);
    Com<IDirect3DVertexBuffer9> vb;
    if (!tex.p || FAILED(dev->CreateVertexBuffer(66 * sizeof(rf::Vertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                                 &vb.p, nullptr))) {
        std::printf("DUMP_FAIL reason=reshade_resources\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    const auto tune = pf::families[pf::fighter_tracer].tune;
    float constants[4];
    x3m::weapon_effects::reshade::constants(tune, constants);
    const bool edge_override = edge && *edge;
    char suffix[16] = "";
    if (edge_override) {
        auto t2 = tune;
        t2.rs_edge = std::strtof(edge, nullptr);
        x3m::weapon_effects::reshade::constants(t2, constants); // clamped like the tuning file
        std::snprintf(suffix, sizeof suffix, "-e%03d", int(constants[1] * 100.f + 0.5f));
    }
    const float app_c31[4] = {0.f, 0.f, 0.f, 0.f};
    // c31.w as production uploads it (bullet_reshade_core.h texture_scale of the bound texture's table p99; the
    // stand-in texture has no entry: the IRE constant)
    const auto p99_q = [](float p99) {
        return std::uint16_t(p99 > 0.f && p99 <= 1.f ? p99 * 65535.f + 0.5f : 0.f);
    };
    constants[3] = x3m::weapon_effects::reshade::texture_scale(p99_q(ire_p99));
    float rows[16];
    rf::clip_rows(1.6f, 1.6f * 1920.f / 1080.f, rows);
    std::printf("DUMP_RESHADE assets=%s mesh_vertices=%u texture=%u metres_per_body_unit=%.3f bolt_length_m=%.1f gain=%.3g "
                "edge=%.3g core=%.3g normaliser=%.4g texture_p99=%.4f\n",
                real ? "game" : "standin", mesh.n, side, double(rf::metres_per_body_unit),
                double(rf::metres_per_body_unit * 2.f * 0.2473f), double(constants[0]), double(constants[1]),
                double(constants[2]), double(constants[3]), double(ire_p99));
    dev->SetRenderTarget(0, t.scene_s.p);
    dev->SetDepthStencilSurface(t.depth_s.p);
    const struct { const char* name; float distance; UINT cw, ch; } ranges[2] = {{"near", 50.f, 640, 256}, {"far", 1500.f, 64, 32}};
    int code = 0;
    for (const auto& r : ranges) {
        void* p = nullptr;
        if (FAILED(vb.p->Lock(0, 0, &p, D3DLOCK_DISCARD))) return 1;
        const float centre[3] = {0.f, 0.f, r.distance};
        rf::place(mesh, centre, 0.4363f, 0.3491f, 255, static_cast<rf::Vertex*>(p));
        vb.p->Unlock();
        for (unsigned shade = edge_override ? 1u : 0u; shade < 2; ++shade) {
            const UINT ow = r.cw * 2, oh = r.ch;
            std::vector<float> out(std::size_t(ow) * oh * 3, 0.f);
            double sums[2]{};
            float peaks[2]{};
            for (unsigned bg = 0; bg < 2; ++bg) {
                dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                           bg ? D3DCOLOR_ARGB(0, 77, 56, 97) : D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
                dev->BeginScene();
                rf::bind_pass(dev, vs.p, decl.p, tex.p, rows, true);
                dev->SetStreamSource(0, vb.p, 0, sizeof(rf::Vertex));
                dev->SetPixelShader(vanilla.p);
                bool restored = true;
                const HRESULT hr = shade ? rf::reshaded_draw(dev, ours.p, vanilla.p, constants, app_c31, 22, &restored)
                                         : dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, 22);
                dev->EndScene();
                if (FAILED(hr) || !restored || FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) code = 1;
                D3DLOCKED_RECT lr{};
                if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return 1;
                const UINT x0 = 960 - r.cw / 2, y0 = 540 - r.ch / 2;
                const float base = bg ? 0.2126f * 77.f / 255.f + 0.7152f * 56.f / 255.f + 0.0722f * 97.f / 255.f : 0.f;
                for (UINT y = 0; y < r.ch; ++y) {
                    const auto* src = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) +
                                                                            (y0 + y) * lr.Pitch) + std::size_t(x0) * 4;
                    for (UINT x = 0; x < r.cw; ++x) {
                        float* d = &out[(std::size_t(y) * ow + bg * r.cw + x) * 3];
                        for (unsigned k = 0; k < 3; ++k) d[k] = half_to_float(src[x * 4 + k]);
                        const float l = 0.2126f * d[0] + 0.7152f * d[1] + 0.0722f * d[2] - base;
                        sums[bg] += l;
                        peaks[bg] = l > peaks[bg] ? l : peaks[bg];
                    }
                }
                t.scene_sys.p->UnlockRect();
            }
            char path[MAX_PATH];
            std::snprintf(path, sizeof path, "%s\\fixture-ire-%s-%s%s.pfm", dir, shade ? "reshade" : "vanilla", r.name,
                          suffix);
            std::FILE* f = std::fopen(path, "wb");
            bool written = f != nullptr;
            if (f) {
                std::fprintf(f, "PF\n%u %u\n-1.0\n", ow, oh);
                for (UINT y = oh; y-- > 0;)
                    written = written && std::fwrite(&out[std::size_t(y) * ow * 3], sizeof(float), std::size_t(ow) * 3, f) ==
                                             std::size_t(ow) * 3;
                std::fclose(f);
            }
            if (!written) code = 1;
            std::printf("DUMP_RESHADE name=fixture-ire-%s-%s%s distance_m=%.0f edge=%.3g crop=%ux%u dark_sum=%.3f "
                        "dark_peak=%.4f nebula_sum=%.3f nebula_peak=%.4f written=%u\n",
                        shade ? "reshade" : "vanilla", r.name, suffix, double(r.distance), double(constants[1]), r.cw, r.ch, sums[0], double(peaks[0]),
                        sums[1], double(peaks[1]), unsigned(written));
        }
    }
    // The other bodies the batch draws, at the same (baked) values, for judging whether one set serves every family:
    // <assets>\family_<name>.txt (a "vertices side p99" line, then x y z u v per vertex, body units as the IRE's) and
    // family_<name>.rgba (side x side RGBA), prepared by run_weapon_effects.py from the lab assets; 50 m, the IRE's pose,
    // dark background only, vanilla and re-shaded: fixture-<name>-{vanilla,reshade}-near.pfm (640x256). Absent files: no
    // frame and one DUMP_RESHADE family=<name> missing=1 row.
    if (!edge_override && assets && *assets) {
        static const char* const families[3] = {"pac", "hept", "ppc"};
        for (const char* name : families) {
            char path[MAX_PATH];
            std::snprintf(path, sizeof path, "%s\\family_%s.txt", assets, name);
            std::FILE* f = std::fopen(path, "rb");
            unsigned count = 0, fside = 0;
            float fp99 = 0.f;
            std::vector<float> pts;
            if (f && std::fscanf(f, "%u %u %f", &count, &fside, &fp99) == 3 && count && count <= 4096 && fside &&
                fside <= 2048) {
                pts.resize(std::size_t(count) * 5);
                for (unsigned i = 0; i < count * 5; ++i)
                    if (std::fscanf(f, "%f", &pts[i]) != 1) {
                        pts.clear();
                        break;
                    }
            }
            if (f) std::fclose(f);
            std::vector<unsigned char> frgba(std::size_t(fside) * fside * 4);
            std::snprintf(path, sizeof path, "%s\\family_%s.rgba", assets, name);
            f = std::fopen(path, "rb");
            const bool read = f && !pts.empty() && std::fread(frgba.data(), 1, frgba.size(), f) == frgba.size();
            if (f) std::fclose(f);
            if (!read) {
                std::printf("DUMP_RESHADE family=%s missing=1\n", name);
                continue;
            }
            Com<IDirect3DTexture9> ftex;
            ftex.p = rf::make_texture(dev, frgba.data(), fside);
            Com<IDirect3DVertexBuffer9> fvb;
            if (!ftex.p || FAILED(dev->CreateVertexBuffer(count * sizeof(rf::Vertex), D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                                          &fvb.p, nullptr))) {
                code = 1;
                continue;
            }
            void* p = nullptr;
            if (FAILED(fvb.p->Lock(0, 0, &p, 0))) return 1;
            {
                // rf::place's pose for any vertex count: the centre at 50 m, the axis yawed 25 degrees, rolled 20
                auto* v = static_cast<rf::Vertex*>(p);
                const float yaw = 0.4363f, roll = 0.3491f, s = rf::metres_per_body_unit;
                const float cy = std::cos(yaw), sy = std::sin(yaw), cr = std::cos(roll), sr = std::sin(roll);
                for (unsigned i = 0; i < count; ++i) {
                    const float* q = &pts[std::size_t(i) * 5];
                    const float x0 = q[0] * s, y0 = q[1] * s, z0 = q[2] * s;
                    const float x1 = cr * x0 - sr * y0, y1 = sr * x0 + cr * y0;
                    v[i] = rf::Vertex{z0 * cy - x1 * sy, y1, 50.f + z0 * sy + x1 * cy, q[3], q[4], 0xff000000u};
                }
            }
            fvb.p->Unlock();
            float fconstants[4] = {constants[0], constants[1], constants[2],
                                   x3m::weapon_effects::reshade::texture_scale(p99_q(fp99))};
            for (unsigned shade = 0; shade < 2; ++shade) {
                const UINT cw = 640, ch = 256;
                std::vector<float> out(std::size_t(cw) * ch * 3, 0.f);
                double sum = 0.0;
                float peak = 0.f;
                dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
                dev->BeginScene();
                rf::bind_pass(dev, vs.p, decl.p, ftex.p, rows, true);
                dev->SetStreamSource(0, fvb.p, 0, sizeof(rf::Vertex));
                dev->SetPixelShader(vanilla.p);
                bool restored = true;
                const HRESULT hr = shade ? rf::reshaded_draw(dev, ours.p, vanilla.p, fconstants, app_c31, count / 3, &restored)
                                         : dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, count / 3);
                dev->EndScene();
                if (FAILED(hr) || !restored || FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) code = 1;
                D3DLOCKED_RECT lr{};
                if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return 1;
                const UINT x0 = 960 - cw / 2, y0 = 540 - ch / 2;
                for (UINT y = 0; y < ch; ++y) {
                    const auto* src = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) +
                                                                            (y0 + y) * lr.Pitch) + std::size_t(x0) * 4;
                    for (UINT x = 0; x < cw; ++x) {
                        float* d = &out[(std::size_t(y) * cw + x) * 3];
                        for (unsigned k = 0; k < 3; ++k) d[k] = half_to_float(src[x * 4 + k]);
                        const float l = 0.2126f * d[0] + 0.7152f * d[1] + 0.0722f * d[2];
                        sum += l;
                        peak = l > peak ? l : peak;
                    }
                }
                t.scene_sys.p->UnlockRect();
                std::snprintf(path, sizeof path, "%s\\fixture-%s-%s-near.pfm", dir, name, shade ? "reshade" : "vanilla");
                f = std::fopen(path, "wb");
                bool written = f != nullptr;
                if (f) {
                    std::fprintf(f, "PF\n%u %u\n-1.0\n", cw, ch);
                    for (UINT y = ch; y-- > 0;)
                        written = written && std::fwrite(&out[std::size_t(y) * cw * 3], sizeof(float), std::size_t(cw) * 3, f) ==
                                                 std::size_t(cw) * 3;
                    std::fclose(f);
                }
                if (!written) code = 1;
                std::printf("DUMP_RESHADE name=fixture-%s-%s-near family=%s vertices=%u texture=%u texture_p99=%.4f "
                            "normaliser=%.4g distance_m=50 crop=%ux%u dark_sum=%.3f dark_peak=%.4f written=%u\n",
                            name, shade ? "reshade" : "vanilla", name, count, fside, double(fp99), double(fconstants[3]),
                            cw, ch, sum, double(peak), unsigned(written));
            }
        }
    }
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetTexture(0, nullptr);
    dev->SetStreamSource(0, nullptr, 0, 0);
    std::printf("RESULT %s checks=0 failures=%u\n", code ? "FAIL" : "PASS", unsigned(code != 0));
    return code;
}
// ------------------------------------------------------------------ muzzle flash re-shade dump (--dump-case muzzle)
// The muzzle re-shade's look evidence (muzzle-flash-look.md; weapon_muzzle_reshade_fixture.h): body 445 drawn the way the
// game draws it (stand-in vs_3_0 / ps_3_0 with the game pair's ABI and emissive law, ADD ONE / ONE, Z test, no Z write,
// the material's emissive 0.588) into the 1920x1080 FP16 scene at the fixture's projection (m00 1.6), once vanilla and
// once with the production program swapped in through the production sequence (plain path), its +z axis 25 degrees out
// of the screen plane and rolled 20 degrees, on the dark background (black) and a nebula stand-in (a flat linear
// 0.30 / 0.22 / 0.38). Frames: near (30 m) and far (300 m) at the scale peak (0.031, 75 ms), grow (30 m) at 20 % of it,
// and at 30 m and the peak the axis along the view ray (axis) and in the screen plane (side): the per-draw overlap's
// two ends.
// The mesh and texture are body 445 and tex/true/384 when run_weapon_effects.py prepared them (untracked), else the
// procedural stand-ins. Each PFM <dir>\fixture-muzzle-{vanilla,reshade}-{near,far,grow,axis,side}.pfm is the two
// backgrounds' crops side by side. DUMP_MUZZLE rows (energy, peak, the equivalent radii at scene white and at 0.1) and one
// DUMP_MUZZLE_GATE row per range (the design's gates: re-shaded energy <= mz_gain x vanilla's, the extent at scene white
// > 0 and <= mz_size x vanilla's; the 0.1 extent ratio is reported, not gated). c192 is the production draw_constants
// from the rows the placement gives (the gun's position and the world scale). keys: "size,core,gain,smooth" overrides
// the baked keys (clamped as the tuning file; the calibration sweep); no record.
namespace mf = muzzle_fx;
bool load_muzzle_assets(const char* dir, mf::Mesh* mesh, std::vector<unsigned char>* rgba) {
    if (!dir || !*dir) return false;
    char path[MAX_PATH];
    std::snprintf(path, sizeof path, "%s\\mz_mesh.txt", dir);
    std::FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    mf::Mesh m;
    float q[5];
    while (m.n < mf::max_vertices && std::fscanf(f, "%f %f %f %f %f", &q[0], &q[1], &q[2], &q[3], &q[4]) == 5) {
        for (unsigned k = 0; k < 5; ++k) m.p[m.n][k] = q[k];
        ++m.n;
    }
    std::fclose(f);
    std::snprintf(path, sizeof path, "%s\\mz_tex.rgba", dir);
    f = std::fopen(path, "rb");
    if (!f || m.n != mf::max_vertices) {
        if (f) std::fclose(f);
        return false;
    }
    std::vector<unsigned char> data(256u * 256u * 4u);
    const bool read = std::fread(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    if (!read) return false;
    *mesh = m;
    rgba->swap(data);
    return true;
}
int muzzle_dump(const char* dir, const char* d3dx_path, const char* assets, const char* keys = nullptr) {
    namespace mz = x3m::weapon_effects::muzzle_reshade;
    HMODULE d3dx = LoadLibraryA(d3dx_path && *d3dx_path ? d3dx_path : "d3dx9_37.dll");
    using CompileFn = HRESULT(WINAPI*)(LPCSTR, UINT, const void*, void*, LPCSTR, LPCSTR, DWORD, ID3DXBuffer**,
                                       ID3DXBuffer**, void*);
    const auto compile_fn = d3dx ? reinterpret_cast<CompileFn>(GetProcAddress(d3dx, "D3DXCompileShader")) : nullptr;
    const auto compile = [&](const char* src, const char* profile, ID3DXBuffer** out) {
        ID3DXBuffer* err = nullptr;
        const HRESULT hr = compile_fn(src, UINT(std::strlen(src)), nullptr, nullptr, "main", profile, 0, out, &err, nullptr);
        if (err) err->Release();
        return SUCCEEDED(hr) && *out;
    };
    ID3DXBuffer *vs_code = nullptr, *ps_code = nullptr;
    Com<IDirect3DVertexShader9> vs;
    Com<IDirect3DPixelShader9> vanilla, ours;
    Com<IDirect3DVertexDeclaration9> decl;
    const bool created = compile_fn && compile(mf::vs_source, "vs_3_0", &vs_code) &&
                         compile(mf::vanilla_ps_source, "ps_3_0", &ps_code) &&
                         SUCCEEDED(dev->CreateVertexShader(static_cast<const DWORD*>(vs_code->GetBufferPointer()), &vs.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(static_cast<const DWORD*>(ps_code->GetBufferPointer()), &vanilla.p)) &&
                         SUCCEEDED(dev->CreatePixelShader(mf::program, &ours.p)) &&
                         SUCCEEDED(dev->CreateVertexDeclaration(mf::declaration(), &decl.p));
    if (vs_code) vs_code->Release();
    if (ps_code) ps_code->Release();
    ProjTarget t;
    if (!created || !make_proj_target(t, 1920, 1080)) {
        std::printf("DUMP_FAIL reason=muzzle_programs\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    CreateDirectoryA(dir, nullptr);
    mf::Mesh mesh;
    std::vector<unsigned char> rgba;
    unsigned side = 256;
    const bool real = load_muzzle_assets(assets, &mesh, &rgba);
    if (!real) {
        mesh = mf::standin_mesh();
        rgba = mf::standin_texture(&side);
    }
    Com<IDirect3DTexture9> tex, light;
    tex.p = rf::make_texture(dev, rgba.data(), side);
    const unsigned char zero[4] = {0, 0, 0, 0};
    light.p = mf::flat_texture(dev, zero);
    Com<IDirect3DVertexBuffer9> vb;
    if (!tex.p || !light.p ||
        FAILED(dev->CreateVertexBuffer(mf::max_vertices * sizeof(mf::Vertex), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0,
                                       D3DPOOL_DEFAULT, &vb.p, nullptr))) {
        std::printf("DUMP_FAIL reason=muzzle_resources\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    auto tune = pf::families[pf::fighter_tracer].tune;
    if (keys && *keys)
        std::sscanf(keys, "%f,%f,%f,%f", &tune.mz_size, &tune.mz_core, &tune.mz_gain, &tune.mz_smooth);
    float base[16], app[16]{};
    mz::constants(mz::bodies[0], tune, base);
    const float size = mz::clamp_key(tune.mz_size, 17), gain = mz::clamp_key(tune.mz_gain, 19);
    float rows[16];
    rf::clip_rows(1.6f, 1.6f * 1920.f / 1080.f, rows);
    float mid[3];
    mf::mesh_centre(mesh, mid);
    std::printf("DUMP_MUZZLE assets=%s mesh_vertices=%u texture=%u metres_per_body_unit_at_peak=%.3g emissive=%.3f "
                "size=%.3g core=%.3g gain=%.3g smooth=%.3g centre_body_units=%.0f,%.0f,%.0f lit_radius_units=%.0f\n",
                real ? "game" : "standin", mesh.n, side, double(mf::peak_scale / mf::engine_units_per_metre),
                double(mf::emissive[0]), double(size), double(mz::clamp_key(tune.mz_core, 18)), double(gain), double(1.f - base[4]),
                double(mid[0]), double(mid[1]), double(mid[2]), double(mz::bodies[0].lit_radius));
    dev->SetRenderTarget(0, t.scene_s.p);
    dev->SetDepthStencilSurface(t.depth_s.p);
    const struct {
        const char* name;
        float distance, scale, yaw;
        UINT cw, ch;
    } ranges[5] = {{"near", 30.f, mf::peak_scale, 0.4363f, 640, 360},
                   {"far", 300.f, mf::peak_scale, 0.4363f, 96, 54},
                   {"grow", 30.f, 0.2f * mf::peak_scale, 0.4363f, 640, 360},
                   {"axis", 30.f, mf::peak_scale, 1.5707963f, 640, 360},
                   {"side", 30.f, mf::peak_scale, 0.f, 640, 360}};
    int code = 0;
    unsigned gate_failures = 0;
    for (const auto& r : ranges) {
        void* p = nullptr;
        if (FAILED(vb.p->Lock(0, 0, &p, D3DLOCK_DISCARD))) return 1;
        const float centre[3] = {0.f, 0.f, r.distance};
        float vs_rows[36], constants[16];
        mf::place(mesh, centre, r.scale, r.yaw, 0.3491f, static_cast<mf::Vertex*>(p), vs_rows);
        vb.p->Unlock();
        std::memcpy(constants, base, sizeof constants);
        if (!mz::draw_constants(mz::bodies[0], vs_rows, constants)) code = 1;
        mf::Figures dark[2];
        for (unsigned shade = 0; shade < 2; ++shade) {
            const UINT ow = r.cw * 2, oh = r.ch;
            std::vector<float> out(std::size_t(ow) * oh * 3, 0.f);
            mf::Figures figs[2];
            for (unsigned bg = 0; bg < 2; ++bg) {
                dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                           bg ? D3DCOLOR_ARGB(0, 77, 56, 97) : D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
                dev->BeginScene();
                mf::bind_pass(dev, vs.p, decl.p, tex.p, light.p, rows, true);
                dev->SetStreamSource(0, vb.p, 0, sizeof(mf::Vertex));
                dev->SetPixelShader(vanilla.p);
                bool restored = true;
                const HRESULT hr = shade ? mf::reshaded_draw(dev, ours.p, vanilla.p, constants, app, mesh.n / 3, &restored)
                                         : dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, mesh.n / 3);
                dev->EndScene();
                if (FAILED(hr) || !restored || FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) code = 1;
                D3DLOCKED_RECT lr{};
                if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return 1;
                const UINT x0 = 960 - r.cw / 2, y0 = 540 - r.ch / 2;
                std::vector<float> crop(std::size_t(r.cw) * r.ch * 3);
                for (UINT y = 0; y < r.ch; ++y) {
                    const auto* src = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) +
                                                                            (y0 + y) * lr.Pitch) + std::size_t(x0) * 4;
                    for (UINT x = 0; x < r.cw; ++x)
                        for (unsigned k = 0; k < 3; ++k) {
                            const float v = half_to_float(src[x * 4 + k]);
                            out[(std::size_t(y) * ow + bg * r.cw + x) * 3 + k] = v;
                            crop[(std::size_t(y) * r.cw + x) * 3 + k] = v;
                        }
                }
                t.scene_sys.p->UnlockRect();
                const float base = bg ? 0.2126f * 77.f / 255.f + 0.7152f * 56.f / 255.f + 0.0722f * 97.f / 255.f : 0.f;
                figs[bg] = mf::figures(crop.data(), r.cw, r.ch, base);
            }
            dark[shade] = figs[0];
            char path[MAX_PATH];
            std::snprintf(path, sizeof path, "%s\\fixture-muzzle-%s-%s.pfm", dir, shade ? "reshade" : "vanilla", r.name);
            std::FILE* f = std::fopen(path, "wb");
            bool written = f != nullptr;
            if (f) {
                std::fprintf(f, "PF\n%u %u\n-1.0\n", ow, oh);
                for (UINT y = oh; y-- > 0;)
                    written = written && std::fwrite(&out[std::size_t(y) * ow * 3], sizeof(float), std::size_t(ow) * 3, f) ==
                                             std::size_t(ow) * 3;
                std::fclose(f);
            }
            if (!written) code = 1;
            std::printf("DUMP_MUZZLE name=fixture-muzzle-%s-%s distance_m=%.0f scale=%.4f crop=%ux%u dark_sum=%.3f "
                        "dark_peak=%.4f dark_white_r=%.1f dark_tenth_r=%.1f nebula_sum=%.3f nebula_peak=%.4f written=%u\n",
                        shade ? "reshade" : "vanilla", r.name, double(r.distance), double(r.scale), r.cw, r.ch,
                        figs[0].sum, double(figs[0].peak), double(figs[0].white_r), double(figs[0].tenth_r), figs[1].sum,
                        double(figs[1].peak), unsigned(written));
        }
        const double energy = dark[0].sum > 0 ? dark[1].sum / dark[0].sum : 0.0;
        const bool energy_ok = dark[0].sum > 0 && energy <= double(gain);
        const bool white_ok = dark[1].white_r > 0.f && dark[1].white_r <= size * dark[0].white_r + 0.5f;
        const bool visible = dark[1].peak > 0.f;
        if (!energy_ok || !white_ok || !visible) ++gate_failures;
        std::printf("DUMP_MUZZLE_GATE range=%s yaw_deg=%.1f overlap=%.3f energy_ratio=%.3f gain=%.3g white_r=%.1f/%.1f "
                    "size=%.3g reshade_peak=%.4f vanilla_peak=%.4f energy=%s extent_white=%s visible=%s "
                    "tenth_r_ratio=%.3f\n",
                    r.name, double(r.yaw) * 57.29578, constants[8] > 0.f ? double(constants[10] / constants[8]) : 0.0,
                    energy, double(gain), double(dark[1].white_r), double(dark[0].white_r), double(size),
                    double(dark[1].peak), double(dark[0].peak), energy_ok ? "pass" : "FAIL", white_ok ? "pass" : "FAIL",
                    visible ? "pass" : "FAIL", dark[0].tenth_r > 0.f ? double(dark[1].tenth_r / dark[0].tenth_r) : 0.0);
    }
    // The view-angle sweep (no frame, dark background, 30 m, the scale peak): the flash axis yawed 0..90 degrees out of
    // the screen plane. One DUMP_MUZZLE_SWEEP row each with the draw's cos^2 and N, the energy ratio, the peaks and the
    // white radii; verification/results/muzzle-reshade/overlap_by_angle.py --rendered fits N(cos^2) from them.
    static const float sweep_deg[] = {0.f, 10.f, 20.f, 25.f, 30.f, 40.f, 50.f, 60.f, 70.f, 75.f, 80.f, 85.f, 88.f, 90.f};
    for (const float deg : sweep_deg) {
        void* p = nullptr;
        if (FAILED(vb.p->Lock(0, 0, &p, D3DLOCK_DISCARD))) return 1;
        const float centre[3] = {0.f, 0.f, 30.f};
        float vs_rows[36], constants[16];
        mf::place(mesh, centre, mf::peak_scale, deg * 0.01745329f, 0.3491f, static_cast<mf::Vertex*>(p), vs_rows);
        vb.p->Unlock();
        std::memcpy(constants, base, sizeof constants);
        if (!mz::draw_constants(mz::bodies[0], vs_rows, constants)) code = 1;
        const float vx = vs_rows[27] - vs_rows[3], vy = vs_rows[31] - vs_rows[7], vz = vs_rows[35] - vs_rows[11];
        const float zx = vs_rows[2], zy = vs_rows[6], zz = vs_rows[10], dz = vx * zx + vy * zy + vz * zz;
        const float c2 = dz * dz / ((vx * vx + vy * vy + vz * vz) * (zx * zx + zy * zy + zz * zz));
        mf::Figures figs[2];
        const UINT cw = 640, ch = 360;
        std::vector<float> crop(std::size_t(cw) * ch * 3);
        for (unsigned shade = 0; shade < 2; ++shade) {
            dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_ARGB(0, 0, 0, 0), 1.f, 0);
            dev->BeginScene();
            mf::bind_pass(dev, vs.p, decl.p, tex.p, light.p, rows, true);
            dev->SetStreamSource(0, vb.p, 0, sizeof(mf::Vertex));
            dev->SetPixelShader(vanilla.p);
            bool restored = true;
            const HRESULT hr = shade ? mf::reshaded_draw(dev, ours.p, vanilla.p, constants, app, mesh.n / 3, &restored)
                                     : dev->DrawPrimitive(D3DPT_TRIANGLELIST, 0, mesh.n / 3);
            dev->EndScene();
            if (FAILED(hr) || !restored || FAILED(dev->GetRenderTargetData(t.scene_s.p, t.scene_sys.p))) code = 1;
            D3DLOCKED_RECT lr{};
            if (FAILED(t.scene_sys.p->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return 1;
            const UINT x0 = 960 - cw / 2, y0 = 540 - ch / 2;
            for (UINT y = 0; y < ch; ++y) {
                const auto* src = reinterpret_cast<const std::uint16_t*>(static_cast<const unsigned char*>(lr.pBits) +
                                                                        (y0 + y) * lr.Pitch) + std::size_t(x0) * 4;
                for (UINT x = 0; x < cw; ++x)
                    for (unsigned k = 0; k < 3; ++k) crop[(std::size_t(y) * cw + x) * 3 + k] = half_to_float(src[x * 4 + k]);
            }
            t.scene_sys.p->UnlockRect();
            figs[shade] = mf::figures(crop.data(), cw, ch, 0.f);
        }
        std::printf("DUMP_MUZZLE_SWEEP yaw_deg=%.0f c2=%.5f overlap=%.4f energy_ratio=%.4f reshade_peak=%.4f "
                    "vanilla_peak=%.4f white_r=%.1f vanilla_white_r=%.1f\n",
                    double(deg), double(c2), double(constants[10] / constants[8]),
                    figs[0].sum > 0 ? figs[1].sum / figs[0].sum : 0.0, double(figs[1].peak), double(figs[0].peak),
                    double(figs[1].white_r), double(figs[0].white_r));
    }
    dev->SetPixelShader(nullptr);
    dev->SetVertexShader(nullptr);
    dev->SetTexture(0, nullptr);
    dev->SetTexture(2, nullptr);
    dev->SetStreamSource(0, nullptr, 0, 0);
    std::printf("RESULT %s checks=5 failures=%u\n", code || gate_failures ? "FAIL" : "PASS",
                unsigned(code != 0) + gate_failures);
    return code || gate_failures ? 1 : 0;
}
} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* dir = argc > 1 ? argv[1] : "dump";
    HMODULE runtime = LoadLibraryA("d3d9.dll");
    auto* create = runtime ? reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(runtime, "Direct3DCreate9")) : nullptr;
    Com<IDirect3D9> api;
    if (create) api.p = create(D3D_SDK_VERSION);
    if (!api.p) {
        std::printf("DUMP_FAIL reason=d3d\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "x3mprojdump";
    RegisterClassA(&wc);
    HWND window = CreateWindowA("x3mprojdump", "x3m", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
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
        std::printf("DUMP_FAIL reason=device\nRESULT FAIL checks=1 failures=1\n");
        return 1;
    }
    dev = device.p;
    dev->GetDeviceCaps(&caps);
    D3DDISPLAYMODE mode{};
    dev->GetDisplayMode(0, &mode);
    adapter_format = mode.Format;
    // argv: <dir> [reshade|muzzle <d3dx9_37.dll> <assets dir>] (run_weapon_effects.py --dump DIR [--dump-case ...])
    const bool reshade = argc > 2 && !std::strcmp(argv[2], "reshade");
    const bool muzzle = argc > 2 && !std::strcmp(argv[2], "muzzle");
    const int code = reshade ? reshade_dump(dir, argc > 3 ? argv[3] : nullptr, argc > 4 ? argv[4] : nullptr,
                                            argc > 5 ? argv[5] : nullptr)
                     : muzzle ? muzzle_dump(dir, argc > 3 ? argv[3] : nullptr, argc > 4 ? argv[4] : nullptr,
                                            argc > 5 ? argv[5] : nullptr)
                              : projectile_dump(dir);
    dev = nullptr;
    return code;
}
