// The projectile stage's look frames (docs/architecture/projectile-look.md, "Implementation (2026-11-08)"): a standalone
// native D3D9 program beside weapon_effects_fixture.cpp (whose record binds its source), built and run only by
// run_weapon_effects.py --host native --dump DIR. It creates a plain HAL device, attaches the production
// WeaponEffectsPass with the projectile programs and draws through the production builder and run_projectiles; each
// frame's FP16 scene is written as linear RGB PFM <dir>\<name>.pfm, which the runner encodes as PNG. No check, no record.
// Never launches the game.
#include <windows.h>
#include <d3d9.h>
#include "../../src/renderer/weapon_effects_pass.h"
#include "../../src/proxy/weapon_projectile_stage.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
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
            m.prev_t0 = born - 150;
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
    const int code = projectile_dump(dir);
    dev = nullptr;
    return code;
}
