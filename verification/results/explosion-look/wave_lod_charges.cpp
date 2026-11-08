// wave_lod_charges.cpp (fire-material.md 13.5): the production stage's charges per frame for the close capital (720 m)
// and the wave gate views, CPU only (no device): the frame's charged screens, the wave's LOD level, key (the band's
// projected screens at the full law) and charge, the embers. The fixture's death_budget_capital_close_5120 stream
// reproduced row for row. Build and run (a Windows binary; the native host through the Wine queue):
//   i686-w64-mingw32-g++ -std=c++17 -O2 -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -static \
//     -static-libgcc -static-libstdc++ -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DX3M_WEAPON_EFFECTS_FIXTURE -Isrc/proxy \
//     verification/results/explosion-look/wave_lod_charges.cpp src/renderer/weapon_effects_pass.cpp \
//     src/renderer/temporal_pass.cpp src/renderer/exposure.cpp -o /tmp/wave_lod_charges.exe -luser32 -ldxguid
//   X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py /tmp/wave_lod_charges.exe
#include <windows.h>
#include <cstdio>
#include <cmath>
#include "weapon_effects_boom.h"
namespace wc = x3m::weapon_effects::core;
namespace ws = x3m::weapon_effects::stage;
namespace rr = x3m::renderer;
namespace bm = wc::boom;
static wc::Ring ring;
static ws::BoomState state;
static rr::WeaponSpriteVertex sprites[bm::max_quads * 4];
static rr::WeaponGlowVertex glows[rr::weapon_glow_boxes * 4], darks[rr::weapon_scorch_boxes * 4], halos[rr::weapon_halo_boxes * 4];
static rr::WeaponFireballVertex bodies[(bm::max_bodies + bm::max_secondaries) * 4], smokes[bm::max_bodies * 4],
    shocks[rr::weapon_shock_boxes * 4], waves[rr::weapon_wave_boxes * 4 * rr::weapon_wave_quads];
static x3m::engine_shimmer::WaveRequest wreq;
static rr::WeaponComposeConstants compose[bm::max_bodies];
static void run(double z, int above, unsigned stepa, int lo, unsigned W, unsigned H) {
    std::printf("== z=%.0f above=%d %ux%u\n", z, above, W, H);
    wc::ring_clear(ring);
    ws::boom_clear(state);
    wc::Event* e = wc::ring_acquire(ring);
    std::memset(e, 0, sizeof *e);
    e->sequence = 0x6a001u; e->role = wc::role_explosion; e->style = bm::capital; e->flags = wc::flag_stage; e->table = 7;
    e->patch_tier = wc::patch_capital; e->t0_game_ms = 1000; e->lifetime_ms = bm::record_life_ms[bm::capital];
    e->owner = 0x7000000u + e->sequence; e->node = 0x7100000u + e->sequence;
    e->pos[2] = std::int32_t(std::lround(z * 500.0)); e->radius = std::int32_t(std::lround(1288.0 * 500.0));
    e->dir[0] = 127; e->detail = above ? 0 : 127; e->calibre_q = above ? 127 : 0;
    const float P = bm::tier_preroll_ms[bm::capital];
    for (unsigned a = 0; a <= unsigned(P) + 2000; a += stepa) {
        ws::HullView v;
        v.cam.r[0] = v.cam.r[4] = v.cam.r[8] = 1.f;
        v.cam.m11 = 1.3333333f; v.cam.m00 = 1.3333333f * float(H) / float(W);
        v.width = float(W); v.height = float(H); v.near_z = 6.f; v.now = 1000 + a; v.step_ms = 16; v.budget_left = wc::screen_budget;
        ws::BoomTuning t;
        ws::BoomBuffers b;
        b.sprites = sprites; b.sprite_cap = bm::max_quads; b.glows = glows; b.glow_cap = rr::weapon_glow_boxes;
        b.fireballs = bodies; b.fireball_cap = bm::max_bodies + bm::max_secondaries; b.smokes = smokes; b.smoke_cap = bm::max_bodies;
        b.scorches = darks; b.scorch_cap = rr::weapon_scorch_boxes; b.shocks = shocks; b.shock_cap = rr::weapon_shock_boxes;
        b.halos = halos; b.halo_cap = rr::weapon_halo_boxes; b.waves = waves; b.wave_cap = rr::weapon_wave_boxes;
        b.wave_shimmer = &wreq; b.compose = compose;
        ws::BoomCounts n = ws::build_boom(ring, state, v, t, b, nullptr, nullptr);
        if (float(a) - P < float(lo)) continue;
        std::printf("%5d screens=%.3f body=%.3f waves=%u shed=%u wq=%u wscr=%.3f oct_shed=%u lod=%u key=%.3f charge=%.3f embers=%u quads=%u\n",
                    int(a) - int(P), n.screens, n.body_screens, n.waves, n.waves_shed, n.wave_quads, n.wave_screens,
                    n.wave_octaves_shed, n.wave_lod, n.wave_key, n.wave_charge, n.embers, b.sprite_quads);
    }
}
int main() {
    run(720, 0, 50, -100, 5120, 1440);
    run(5000, 0, 100, 0, 5120, 1440);
    run(5000, 0, 100, 0, 1920, 1080);
    run(1500, 0, 50, 0, 5120, 1440);
    run(1500, 1, 100, 0, 5120, 1440);
    run(1500, 0, 50, 0, 1920, 1080);
    return 0;
}
