"""Host checks of the weapon-effects classifier and ring (src/proxy/weapon_effects_core.h), compiled on the host.

The classifier (style, calibre band, tint source) run over the stock AP census through the production header must give
the [M] table of docs/architecture/weapon-effects.md section 4.4, pinned against
verification/results/weapon-effects-census/classify_preview_out.txt; the ring/overflow/retire and the game-time age
law; the stage builder of src/proxy/weapon_effects_stage.h (owner validation, the dying bit, retirement, the game-time
age filter, decal routing, the four-hit insertion) over a fake engine memory; Present's ownership latch
(core::stage_owned) and the ring's scope (core::ScopeLatch). No Wine, no game. Builds a small harness against the
headers with a host C++ compiler (a host-only d3d9.h declares the few types weapon_effects_pass.h names).
"""
import json
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CENSUS = ROOT / 'verification/results/weapon-effects-census'
PREVIEW = CENSUS / 'classify_preview_out.txt'

D3D9_STUB = r'''
#pragma once
// Host-only declarations for the stage builder test (weapon_effects_pass.h's types), not a runtime substitute.
#include <cstdint>
typedef unsigned int UINT;
typedef long HRESULT;
typedef int D3DFORMAT;
#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
struct D3DCAPS9 {};
struct IDirect3DDevice9;
struct IDirect3DTexture9;
struct IDirect3DVertexShader9;
struct IDirect3DPixelShader9;
struct IDirect3DVertexDeclaration9;
'''

HARNESS = r'''
#include "weapon_effects_core.h"
#include "weapon_effects_stage.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace core = x3m::weapon_effects::core;
namespace st = x3m::weapon_effects::stage;
static long I(const char* s) { return std::strtol(s, nullptr, 0); }
// A fake engine memory at 0x100000..0x110000 (32-bit addresses, as the records carry): object k at base + k * 0x400,
// its node at + 0x200. The reader refuses anything outside, as engine_memory::read refuses unreadable memory.
static unsigned char mem[0x10000];
constexpr std::uint32_t base = 0x100000;
static bool rd(std::uintptr_t a, void* out, std::size_t n) {
    if (a < base || a + n > base + sizeof mem) return false;
    std::memcpy(out, mem + (a - base), n);
    return true;
}
static void put(std::uint32_t a, std::uint32_t v) { std::memcpy(mem + (a - base), &v, 4); }
static std::uint32_t obj(unsigned k) { return base + k * 0x400; }
static std::uint32_t node(unsigned k) { return obj(k) + 0x200; }
// Owner k at engine (0, 0, z) with radius r (engine units), identity rows, handle 0xd000 + k.
static void owner(unsigned k, std::int32_t z, std::int32_t r) {
    std::memset(mem + (obj(k) - base), 0, 0x400);
    put(obj(k) + core::layout::obj_node, node(k));
    put(obj(k) + core::layout::obj_radius, std::uint32_t(r));
    put(node(k) + core::layout::node_handle, 0xd000 + k);
    put(node(k) + core::layout::node_pos + 8, std::uint32_t(z));
    for (unsigned i = 0; i < 3; ++i) put(node(k) + core::layout::node_rot + 16 * i + 4 * i, 0x10000);
}
static core::Event* hit(core::Ring& ring, unsigned k, std::uint32_t t0, std::int32_t px, std::uint32_t tint = 0x1000000u | 255u) {
    core::Event* e = core::ring_acquire(ring);
    e->role = core::role_shield_hit;
    e->flags = 0;
    e->t0_game_ms = t0;
    e->lifetime_ms = 600;
    e->damage = 1200;
    e->tint_rgb = tint;
    e->alt_rgb = 0;
    e->owner = obj(k);
    e->node = node(k);
    e->handle = 0xd000 + k;
    e->pos[0] = px;
    e->pos[1] = 0;
    e->pos[2] = -20000;
    e->radius = 20000;
    return e;
}
static unsigned live(const core::Ring& r) {
    unsigned n = 0;
    for (unsigned i = 0; i < core::ring_capacity; ++i) n += r.slots[i].sequence != 0;
    return n;
}
static int stage_cases() {
    static core::Ring ring;
    x3m::renderer::WeaponShell shells[core::max_shells];
    x3m::renderer::WeaponDecal decals[core::max_decals];
    st::Camera cam;
    cam.r[0] = cam.r[4] = cam.r[8] = 1.f;
    auto build = [&](std::uint32_t now) {
        return st::build(ring, now, &rd, cam, st::Tuning{}, shells, core::max_shells, decals, core::max_decals);
    };
    // 1 a live owner: one shell, one hit, its direction and tint.
    core::ring_clear(ring);
    owner(0, 40000, 20000);
    hit(ring, 0, 1000, 0);
    st::Counts n = build(1100);
    std::printf("live shells=%u hits=%u retired=%u decals=%u shell_hits=%u dir_z=%.3f age=%.3f centre_z=%.1f axis=%.1f tint_r=%.2f tint_g=%.2f\n",
                n.shells, n.hits, n.retired, n.decals, shells[0].hits, shells[0].dir[0][2], shells[0].age_s[0],
                shells[0].centre[2], shells[0].axes[0], shells[0].tint[0], shells[0].tint[1]);
    // 2..5 validation: a replaced node, a changed handle, the dying bit, an unreadable owner; each retires every record
    // of that owner (three hits) and leaves another owner's hit alone.
    const char* names[4] = {"node_mismatch", "handle_mismatch", "dying", "unreadable"};
    for (unsigned c = 0; c < 4; ++c) {
        core::ring_clear(ring);
        ring.retired = 0; // the running counter (ring_clear keeps it for the period rows)
        owner(0, 40000, 20000);
        owner(1, 40000, 20000);
        for (unsigned i = 0; i < 3; ++i) hit(ring, 0, 1000 + i, 0);
        hit(ring, 1, 1000, 0);
        if (c == 0) put(obj(0) + core::layout::obj_node, node(2));
        if (c == 1) put(node(0) + core::layout::node_handle, 0xbad);
        if (c == 2) put(obj(0) + core::layout::obj_flags, core::layout::obj_dying);
        if (c == 3)
            for (core::Event& e : ring.slots)
                if (e.sequence && e.owner == obj(0)) e.owner = 0x10;
        n = build(1100);
        std::printf("%s shells=%u retired=%u live=%u ring_retired=%u\n", names[c], n.shells, n.retired, live(ring), ring.retired);
    }
    // 6 the age filter in game time: 599 ms drawn, 601 ms skipped (left for Present), a future t0 (a load) skipped.
    core::ring_clear(ring);
    owner(0, 40000, 20000);
    hit(ring, 0, 1000, 0);
    const unsigned at599 = build(1599).shells, at601 = build(1601).shells, future = build(900).shells;
    std::printf("age at599=%u at601=%u future=%u live=%u\n", at599, at601, future, live(ring));
    // 7 decal routing: an owner above shell_radius_max_u gets a decal at the hit point, no shell.
    core::ring_clear(ring);
    owner(0, 400000, 200000);
    hit(ring, 0, 1000, 0)->pos[2] = -200000;
    n = build(1100);
    std::printf("decal shells=%u decals=%u centre_z=%.1f radius=%.2f\n", n.shells, n.decals, decals[0].centre[2], decals[0].radius);
    // 8 the four-hit insertion: five hits on one owner aged 300, 100, 500, 50, 200 ms (in ring order): the shell keeps
    // the newest four in age order, the newest hit's colour tints it.
    core::ring_clear(ring);
    owner(0, 40000, 20000);
    const std::uint32_t now = 2000;
    const std::uint32_t ages[5] = {300, 100, 500, 50, 200};
    for (unsigned i = 0; i < 5; ++i) hit(ring, 0, now - ages[i], std::int32_t(1000 * (i + 1)), i == 3 ? (0x1000000u | 255u << 8) : (0x1000000u | 255u));
    n = build(now);
    std::printf("four shells=%u hits=%u shell_hits=%u age0=%.3f age1=%.3f age2=%.3f age3=%.3f tint_r=%.2f tint_g=%.2f\n",
                n.shells, n.hits, shells[0].hits, shells[0].age_s[0], shells[0].age_s[1], shells[0].age_s[2],
                shells[0].age_s[3], shells[0].tint[0], shells[0].tint[1]);
    // 9 forwarded and hull-hit records never draw.
    core::ring_clear(ring);
    owner(0, 40000, 20000);
    hit(ring, 0, 1000, 0)->flags = core::flag_forwarded;
    hit(ring, 0, 1000, 0)->role = core::role_hull_hit;
    n = build(1100);
    std::printf("skipped shells=%u hits=%u live=%u\n", n.shells, n.hits, live(ring));
    return 0;
}
int main(int argc, char** argv) {
    if (!std::strcmp(argv[1], "classify")) {
        // argv: classify flags z_over_xy_milli(or "none") shield hull r g b
        const std::uint32_t flags = (std::uint32_t)I(argv[2]);
        const bool present = std::strcmp(argv[3], "none") != 0;
        const float e = present ? float(I(argv[3])) / 1000.f : 0.f;
        const std::int32_t shield = (std::int32_t)I(argv[4]), hull = (std::int32_t)I(argv[5]);
        const std::int32_t r = (std::int32_t)I(argv[6]), g = (std::int32_t)I(argv[7]), b = (std::int32_t)I(argv[8]);
        const std::uint8_t style = core::style_from(flags, core::shape_from_elongation(e, present));
        const std::int32_t d = shield > hull ? shield : hull;
        const char* band = core::tier_name(core::tier_from(d));
        const char* tint = core::light_saturated(r, g, b) ? "light" : "body_texture";
        std::printf("%s%s %s %s\n", core::shape_name(style & core::style_shape_mask),
                    (style & core::style_burst) ? "+burst" : "", band, tint);
        return 0;
    }
    if (!std::strcmp(argv[1], "ring")) {
        core::Ring ring;
        core::ring_clear(ring);
        for (unsigned i = 0; i < core::ring_capacity; ++i) {
            core::Event* ev = core::ring_acquire(ring);
            ev->role = (i == 3) ? core::role_shield_hit : core::role_hull_hit;
            ev->flags = (i == 3) ? 0 : core::flag_forwarded;
            ev->owner = 0x100 + (i & 7);
        }
        const std::uint32_t kept = ring.slots[3].sequence;
        core::ring_acquire(ring);
        bool survived = false;
        for (unsigned i = 0; i < core::ring_capacity; ++i) survived |= ring.slots[i].sequence == kept;
        core::ring_clear(ring);
        for (unsigned i = 0; i < 5; ++i) { core::Event* ev = core::ring_acquire(ring); ev->owner = i < 3 ? 0xaa : 0xbb; }
        unsigned retired = core::ring_retire_owner(ring, 0xaa);
        core::Event age{}; age.t0_game_ms = 1000; age.lifetime_ms = 600;
        std::printf("overflow=%u survived=%u retired=%u age=%u exp_now=%u exp_wrap=%u\n", ring.overflow,
                    unsigned(survived), retired, core::age_ms(1100, age.t0_game_ms), unsigned(core::expired(1700, age)),
                    unsigned(core::expired(500, age)));
        return 0;
    }
    if (!std::strcmp(argv[1], "stage")) return stage_cases();
    if (!std::strcmp(argv[1], "latch")) {
        core::ScopeLatch l;
        const bool first = l.changed(core::Scope{0x111, 0, false});    // the first known key: no change
        const bool unknown = l.changed(core::Scope{0, 0, false});      // an unknown read keeps the held key
        const bool sector = l.changed(core::Scope{0x222, 0, false});   // a different key
        const bool same = l.changed(core::Scope{0x222, 0, false});
        const bool loading = l.changed(core::Scope{0, 0, true});       // a load
        const bool epoch1 = l.changed(core::Scope{0x222, 5, false});   // the first known epoch
        const bool epoch2 = l.changed(core::Scope{0x222, 6, false});   // a new epoch
        std::printf("scope first=%u unknown=%u sector=%u same=%u loading=%u epoch1=%u epoch2=%u held=%llu\n", first, unknown,
                    sector, same, loading, epoch1, epoch2, (unsigned long long)l.sector_key);
        // stage_owned(armed, resolve_ok, callback_set, callback_ok)
        std::printf("owned drew=%u nothing_to_draw=%u callback_unset=%u failed_resolve=%u callback_failed=%u unarmed=%u\n",
                    core::stage_owned(true, true, true, true), core::stage_owned(true, true, false, false),
                    core::stage_owned(true, true, false, true), core::stage_owned(true, false, true, true),
                    core::stage_owned(true, true, true, false), core::stage_owned(false, true, true, true));
        return 0;
    }
    return 2;
}
'''


def preview_rows():
    """{weapon: {flags, e, dmg, style, band}} from classify_preview_out.txt's stock_ap table (the only tracked source;
    the census JSONs are untracked). The light RGB is not in the table, so tint source is covered by the hook fixture
    and the ring/tint unit checks, not re-derived here; style and calibre band are."""
    rows, in_stock = {}, False
    for line in PREVIEW.read_text().splitlines():
        if line.startswith('## stock_ap'):
            in_stock = True
            continue
        if line.startswith('## ') and in_stock:
            break
        if in_stock and line.startswith('| ') and '---' not in line and not line.startswith('| weapon'):
            cells = [c.strip() for c in line.strip().strip('|').split('|')]
            weapon, flags, zxy, dmg, style, band = cells[0], cells[1], cells[2], cells[5], cells[6], cells[7].split()[0]
            rows[weapon] = dict(flags=int(flags, 0), e=None if zxy == '-' else float(zxy), dmg=int(dmg),
                                style=style, band=band)
    return rows


class WeaponEffectsClassifier(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++') or shutil.which('c++') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('a host C++ compiler is required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-weapon-core-')
        src = Path(cls.tmp.name) / 'h.cpp'
        src.write_text(HARNESS)
        stub = Path(cls.tmp.name) / 'stub'
        stub.mkdir()
        (stub / 'd3d9.h').write_text(D3D9_STUB)
        cls.exe = Path(cls.tmp.name) / 'h'
        subprocess.run([compiler, '-std=c++17', '-O2', '-I', str(stub), '-I', str(ROOT / 'src/proxy'), str(src), '-o',
                        str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def classify(self, row):
        e = 'none' if row['e'] is None else str(int(round(row['e'] * 1000)))
        out = subprocess.run([str(self.exe), 'classify', str(row['flags']), e, str(row['dmg']), '0', '0', '0', '0'],
                             capture_output=True, text=True, check=True).stdout.strip().split()
        return out[0], out[1]  # style, band (tint needs the light RGB, not in the tracked table)

    def test_classifier_matches_section_4_4(self):
        preview = preview_rows()
        self.assertGreaterEqual(len(preview), 40)
        for weapon, want in preview.items():
            got = self.classify(want)
            self.assertEqual(got, (want['style'], want['band']),
                             f"{weapon}: core gave {got}, section 4.4 has {(want['style'], want['band'])}")

    def test_ring_overflow_retire_and_age_law(self):
        out = subprocess.run([str(self.exe), 'ring'], capture_output=True, text=True, check=True).stdout
        fields = dict(re.findall(r'(\w+)=(\d+)', out))
        self.assertEqual(fields['overflow'], '1')
        self.assertEqual(fields['survived'], '1')  # the stage-owned shield hit is never evicted first
        self.assertEqual(fields['retired'], '3')
        self.assertEqual(fields['age'], '100')     # game-time age
        self.assertEqual(fields['exp_now'], '1')
        self.assertEqual(fields['exp_wrap'], '1')   # a future t0 (a load) reads as expired

    def run_case(self, case):
        out = subprocess.run([str(self.exe), case], capture_output=True, text=True, check=True).stdout
        rows = {}
        for line in out.splitlines():
            name, _, rest = line.partition(' ')
            rows[name] = {k: float(v) for k, v in re.findall(r'(\w+)=(-?[0-9.]+)', rest)}
        return rows

    def test_stage_build_owner_validation_retirement_age_decal_and_slots(self):
        r = self.run_case('stage')
        live = r['live']
        self.assertEqual((live['shells'], live['hits'], live['retired'], live['decals'], live['shell_hits']), (1, 1, 0, 0, 1))
        self.assertAlmostEqual(live['dir_z'], -1.0, places=3)          # the hit direction on the unit sphere
        self.assertAlmostEqual(live['age'], 0.1, places=3)             # 100 game ms
        self.assertAlmostEqual(live['centre_z'], 400.0, places=1)      # engine 40000 x world_scale
        self.assertAlmostEqual(live['axis'], 200.0, places=1)          # radius 20000 x world_scale x the unit row
        self.assertEqual((live['tint_r'], live['tint_g']), (1.0, 0.0))
        for case in ('node_mismatch', 'handle_mismatch', 'dying', 'unreadable'):
            row = r[case]
            # every record of the bad owner leaves the ring (3), the other owner's hit draws and stays
            self.assertEqual((row['shells'], row['retired'], row['live'], row['ring_retired']), (1, 3, 1, 3), case)
        self.assertEqual((r['age']['at599'], r['age']['at601'], r['age']['future'], r['age']['live']), (1, 0, 0, 1))
        d = r['decal']
        self.assertEqual((d['shells'], d['decals']), (0, 1))
        self.assertAlmostEqual(d['centre_z'], 2000.0, places=1)       # the hit point: 400000 - 200000 engine units
        self.assertGreater(d['radius'], 0)
        f = r['four']
        self.assertEqual((f['shells'], f['hits'], f['shell_hits']), (1, 5, 4))
        self.assertEqual([f['age0'], f['age1'], f['age2'], f['age3']], [0.05, 0.1, 0.2, 0.3])  # newest first, 500 ms dropped
        self.assertEqual((f['tint_r'], f['tint_g']), (0.0, 1.0))       # the newest hit's colour
        self.assertEqual((r['skipped']['shells'], r['skipped']['hits'], r['skipped']['live']), (0, 0, 2))

    def test_present_latch_and_ring_scope(self):
        r = self.run_case('latch')
        scope = {k: int(v) for k, v in r['scope'].items()}
        # the first known key and an unknown read keep; a new key, a load and a new epoch clear; unknown keeps the key
        self.assertEqual((scope['first'], scope['unknown'], scope['sector'], scope['same'], scope['loading'],
                          scope['epoch1'], scope['epoch2'], scope['held']), (0, 0, 1, 0, 1, 0, 1, 0x222))
        owned = {k: int(v) for k, v in r['owned'].items()}
        # armed + resolve completed + (callback ran and drew / had nothing) owns; a failed resolve or callback does not
        self.assertEqual(owned, {'drew': 1, 'nothing_to_draw': 1, 'callback_unset': 1, 'failed_resolve': 0,
                                 'callback_failed': 0, 'unarmed': 0})


if __name__ == '__main__':
    unittest.main()
