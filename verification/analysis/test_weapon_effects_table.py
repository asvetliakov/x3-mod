"""tools/effects/weapon_effects_table.py: the generated <game>/x3m/weapon_effects.json and its DLL-side reader
(src/proxy/weapon_effects_table.h). The generator's shape helpers (shape_from_elongation, override resolution,
linear->sRGB), the overrides file's schema, and the reader compiled on the host round-tripping a generated table.
No game tree needed (the census-driven path is exercised by test_weapon_effects_core and the generator itself)."""
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/effects'))
for _p in ('tools/analysis', 'tools/regenerate', 'verification/analysis'):
    if str(ROOT / _p) not in sys.path:
        sys.path.insert(0, str(ROOT / _p))
import weapon_effects_table as wt  # noqa: E402

OVERRIDES = ROOT / 'tools/effects/weapon_overrides.json'

READER = r'''
#include "weapon_effects_table.h"
#include "bullet_reshade_core.h"
#include <cstdio>
#include <cstdlib>
#include <string>
namespace t = x3m::weapon_effects::table;
namespace core = x3m::weapon_effects::core;
int main(int argc, char** argv) {
    std::string text;
    int c;
    while ((c = std::getchar()) != EOF) text.push_back(char(c));
    core::Table table;
    t::Stats st;
    std::size_t fault = 0;
    const bool ok = t::parse(text.data(), text.size(), &table, &st, &fault);
    if (argc > 1 && std::string(argv[1]) == "bodymap") { // the geometric size's body map against a fake engine
        // TBullets at 0x1000 (count slot 0x10, table slot 0x14): row r's body id word at 0x1000 + r x 0xdb8; the reader
        // fails the slots until `ready` and the row reads while `failing`; argv[2] the engine's row count
        namespace rs = x3m::weapon_effects::reshade;
        const std::uint32_t engine_rows = argc > 2 ? std::uint32_t(std::atoi(argv[2])) : table.rows;
        bool ready = false;
        unsigned failing = 0;
        const auto read = [&](std::uintptr_t a, void* out, std::size_t n) {
            if (!ready) return false;
            if (a == 0x14 && n == 4) return *static_cast<std::uint32_t*>(out) = 0x1000u, true;
            if (a == 0x10 && n == 4) return *static_cast<std::uint32_t*>(out) = engine_rows, true;
            if (failing) return --failing, false;
            const std::uintptr_t r = (a - 0x1000) / core::layout::type_stride;
            if (n != 2 || a < 0x1000 || (a - 0x1000) % core::layout::type_stride) return false;
            *static_cast<std::uint16_t*>(out) = std::uint16_t(0x200 + r); // body id 0x200 + row
            return true;
        };
        rs::BodyMap m;
        const bool early = rs::build_body_map(m, ok ? &table : nullptr, 0x14, 0x10, read);
        ready = true;
        failing = 1;
        const bool after_fail = rs::build_body_map(m, ok ? &table : nullptr, 0x14, 0x10, read);
        const std::uint32_t failures_once = m.failures;
        const bool built = rs::build_body_map(m, ok ? &table : nullptr, 0x14, 0x10, read);
        const std::int32_t* row0 = rs::body_layout(m, &table, 0x200);
        const std::int32_t* unknown = rs::body_layout(m, &table, 0x7777);
        rs::BodyMap g; // every row read fails: final without layouts after body_map_retries builds
        unsigned builds = 0;
        failing = 1000000;
        while (!rs::build_body_map(g, ok ? &table : nullptr, 0x14, 0x10, read) && builds < 1000) ++builds;
        std::printf("early=%d after_fail=%d failures_once=%u built=%d state=%u reason=%s mapped=%u row0_head_u=%d "
                    "unknown=%d giveup_builds=%u giveup_state=%u giveup_reason=%s\n",
                    int(early), int(after_fail), failures_once, int(built), unsigned(m.state), m.reason, m.count,
                    row0 ? row0[1] : -1, unknown ? 1 : 0, builds + 1, unsigned(g.state), g.reason);
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "fnv") { // the bytes on stdin: the production texture hashes
        const auto* p = reinterpret_cast<const unsigned char*>(text.data());
        std::printf("head64=%016llx fnv1a64=%016llx\n",
                    (unsigned long long)x3m::weapon_effects::reshade::fnv1a64(p, text.size() < 4096 ? text.size() : 4096),
                    (unsigned long long)x3m::weapon_effects::reshade::fnv1a64(p, text.size()));
        return 0;
    }
    if (argc > 2 && std::string(argv[1]) == "profiles") { // the ship profiles block: the counts and row argv[2]'s entry
        static core::ShipProfiles pr;
        const bool pok = t::parse(text.data(), text.size(), &table, &st, &fault, &pr);
        const core::ShipProfile* p = pr.find(std::atoi(argv[2]));
        std::printf("ok=%d present=%u profiles=%u dropped=%u class_unknown=%u count=%u tships_rows=%u carries=%d found=%d "
                    "class=%d z0=%.1f z1=%.1f s16=%.3f,%.3f,%.3f,%.3f\n", int(pok), unsigned(st.profiles_present),
                    st.profiles, st.profiles_dropped, st.profiles_class_unknown, pr.count, unsigned(pr.tships_rows),
                    int(t::carries_ship_profiles(text.data(), text.size())), p ? 1 : 0, p ? int(p->class_id) : -9,
                    p ? double(p->z0) : 0.0, p ? double(p->z1) : 0.0, p ? double(p->slice[16][0]) : 0.0,
                    p ? double(p->slice[16][1]) : 0.0, p ? double(p->slice[16][2]) : 0.0, p ? double(p->slice[16][3]) : 0.0);
        return pok ? 0 : 1;
    }
    std::printf("ok=%d rows=%u missile_rows=%u entries=%u textures=%u overrides=%u schema=%u\n", int(ok), table.rows,
                table.missile_rows, st.entries, st.textures, st.overrides, st.schema);
    std::printf("bullet_textures=%u bullet_textures_dropped=%u tex0_bytes=%u tex0_head=%016llx tex0_hash=%016llx "
                "tex0_p99_q=%u\n", table.bullet_textures, st.bullet_textures_dropped, table.bullet_texture_bytes[0],
                (unsigned long long)table.bullet_texture_head[0], (unsigned long long)table.bullet_texture_hash[0],
                unsigned(table.bullet_texture_p99_q[0]));
    {   // the geometric size: entry 0's words and layout_constants at rs_head 0.5, rs_width 0.6 and at the defaults
        namespace rs = x3m::weapon_effects::reshade;
        auto tune = x3m::weapon_effects::projectile_family::families[0].tune;
        float shrunk[8], kept[8];
        rs::layout_constants(table.bullet_bodies ? table.bullet_body_layout[0] : nullptr, tune, kept);
        tune.rs_head = 0.5f;
        tune.rs_width = 0.6f;
        rs::layout_constants(table.bullet_bodies ? table.bullet_body_layout[0] : nullptr, tune, shrunk);
        std::printf("bullet_bodies=%u bullet_bodies_dropped=%u bullet_bodies_key=%u body0_hash=%08lx body0_words=",
                    table.bullet_bodies, st.bullet_bodies_dropped, unsigned(st.bullet_bodies_present),
                    (unsigned long)table.bullet_body_hash[0]);
        for (unsigned k = 0; k < 8; ++k) std::printf("%s%d", k ? "," : "", int(table.bullet_body_layout[0][k]));
        std::printf(" body0_shrunk=");
        for (unsigned k = 0; k < 8; ++k) std::printf("%s%.5g", k ? "," : "", double(shrunk[k]));
        std::printf(" body0_kept=");
        for (unsigned k = 0; k < 8; ++k) std::printf("%s%.5g", k ? "," : "", double(kept[k]));
        std::printf(" ire_hash=%08lx\n", (unsigned long)core::name_hash("objects/effects/weapons/bullet_ls_alpha"));
    }
    if (ok && argc > 1) {
        const unsigned row = unsigned(std::atoi(argv[1]));
        std::printf("row=%u bits=%u shape=%s texture=%06lx override=%06lx calibre_q=%u hull_class=%s\n", row,
                    table.bits[row], core::shape_name(table.shape[row]), (unsigned long)(table.texture_rgb[row] & 0xffffff),
                    (unsigned long)(table.override_rgb[row] & 0xffffff), table.calibre_q[row],
                    (table.bits[row] & core::row_hull_class) ? core::hull_class_name(table.hull_class[row]) : "-");
        std::printf("mount_tier=%u range_m=%u mount_keys=%u\n", table.mount_tier[row], table.range_m[row], st.mount_keys);
        std::printf("bolt_pin=%u e_body_q=%u w_orb_q=%u bolt_pins=%u bolt_rejected=%u present=%u\n", table.bolt_pin[row],
                    table.bolt_e_body_q[row], table.bolt_w_orb_q[row], st.bolt_pins, st.bolt_rejected,
                    unsigned(table.bits[row] & core::row_present));
    }
    return ok ? 0 : 1;
}
'''


def sample_table():
    return {'schema': 1, 'tool': wt.TOOL, 'tables': {
        'bullets': {'rows': 45, 'sha256': 'be762076cdc38b03', 'entries': [
            {'row': 0, 'id': 'SS_BULLET_IRE', 'z_over_xy': 18.5, 'texture_rgb': [121, 82, 39], 'style': None,
             'tint': None, 'calibre': None},
            {'row': 1, 'id': 'SS_BULLET_X', 'z_over_xy': 2.0, 'texture_rgb': None, 'style': 'beam',
             'tint': [77, 255, 77], 'calibre': 0.5},
            {'row': 2, 'id': 'SS_BULLET_Y', 'z_over_xy': None, 'texture_rgb': None, 'style': None, 'tint': None,
             'calibre': None},
            {'row': 3, 'id': 'SS_BULLET_REPAIR', 'z_over_xy': None, 'texture_rgb': None, 'style': None, 'tint': None,
             'calibre': None, 'hull': 'repair'},
            {'row': 4, 'id': 'SS_BULLET_TUG', 'z_over_xy': None, 'texture_rgb': None, 'style': None, 'tint': None,
             'calibre': 0.1, 'hull': 'tug'},
            {'row': 5, 'id': 'SS_BULLET_Z', 'z_over_xy': None, 'texture_rgb': None, 'style': None, 'tint': None,
             'calibre': None, 'hull': 'unknown'},
            {'row': 6, 'id': 'SS_BULLET_MD', 'z_over_xy': None, 'texture_rgb': None, 'style': None, 'tint': None,
             'calibre': None, 'hull': 'ballistic'},
            {'row': 7, 'id': 'SS_BULLET_PBC', 'z_over_xy': None, 'texture_rgb': None, 'style': None, 'tint': None,
             'calibre': None, 'hull': 'beam'},
            {'row': 8, 'id': 'SS_BULLET_PPC', 'z_over_xy': None, 'texture_rgb': None, 'style': None, 'tint': None,
             'calibre': None, 'mount_tier': 'capital', 'range_m': 4957.4},
            {'row': 9, 'id': 'SS_BULLET_PSG', 'z_over_xy': None, 'texture_rgb': None, 'style': None, 'tint': None,
             'calibre': None, 'mount_tier': 'corvette', 'range_m': None},
            {'row': 10, 'id': 'SS_BULLET_MD', 'z_over_xy': None, 'texture_rgb': [66, 66, 66], 'style': 'slug', 'tint': None,
             'calibre': None, 'bolt_e_body': 0.6, 'bolt_halo': False, 'bolt_tint': 'texture'},
            {'row': 11, 'id': 'SS_BULLET_FBL', 'z_over_xy': None, 'texture_rgb': None, 'style': 'orb', 'tint': None,
             'calibre': None, 'bolt_w_orb': 2.0}]},
        'missiles': {'rows': 36}}}


class WeaponEffectsTableGenerator(unittest.TestCase):
    def test_texture_hash_and_luminance(self):
        # the bullet re-shade's texture keys and normaliser input (bullet_reshade_core.h): FNV-1a 64 (the published
        # vectors) and the luminance p99 / peak over every texel, Rec. 709 on value / 255
        import numpy as np
        self.assertEqual(wt.fnv1a64(b''), 0xcbf29ce484222325)
        self.assertEqual(wt.fnv1a64(b'a'), 0xaf63dc4c8601ec8c)
        self.assertEqual(wt.fnv1a64(b'foobar'), 0x85944171f73967e8)
        rgba = np.zeros((100, 100, 4), np.uint8)
        rgba[0, 0, :3] = 255                   # one white texel: the peak, not the p99
        rgba[1:, :, 0] = 100                   # 9900 texels of red 100: luminance 0.2126 x 100 / 255
        peak, p99 = wt.luminance_stats(rgba)
        self.assertEqual(peak, 1.0)
        self.assertAlmostEqual(p99, round(0.2126 * 100 / 255, 4), places=4)
        w = {'index': 3, 'weapon_id': 'SS_BULLET_PPC', 'body_key': 'objects/v/00518', 'speed': None, 'lifetime_ms': None}
        self.assertEqual(wt.row_entry(w, None, None, {}, texture='fx_bullets2_diff')['texture'], 'fx_bullets2_diff')
        self.assertIsNone(wt.row_entry(w, None, None, {})['texture'])

    def test_shape_from_elongation_matches_the_header_thresholds(self):
        self.assertEqual(wt.shape_from_elongation(18.5), 'tracer')
        self.assertEqual(wt.shape_from_elongation(4.0), 'tracer')
        self.assertEqual(wt.shape_from_elongation(3.99), 'slug')
        self.assertEqual(wt.shape_from_elongation(1.5), 'slug')
        self.assertEqual(wt.shape_from_elongation(1.46), 'orb')
        self.assertEqual(wt.shape_from_elongation(None), 'slug')

    def test_linear_to_srgb8(self):
        self.assertEqual(wt.linear_to_srgb8(0.0), 0)
        self.assertEqual(wt.linear_to_srgb8(1.0), 255)
        self.assertEqual(wt.linear_to_srgb8(2.0), 255)   # clamped

    def test_override_resolution_by_table_wins(self):
        overrides = {'schema': 1, 'by_id': {'SS_BULLET_IRE': {'style': 'orb'}},
                     'by_table': {'abc': {'SS_BULLET_IRE': {'style': 'tracer', 'tint': [1, 0, 0]}}}}
        self.assertEqual(wt.override_for(overrides, 'abc', 'SS_BULLET_IRE'), {'style': 'tracer', 'tint': [1, 0, 0]})
        self.assertEqual(wt.override_for(overrides, 'other', 'SS_BULLET_IRE'), {'style': 'orb'})
        self.assertEqual(wt.override_for(overrides, 'abc', 'SS_BULLET_Z'), {})
        hull = {'schema': 1, 'by_id': {'SS_BULLET_TUG': {'hull': 'tug', 'calibre': 0.1}}, 'by_table': {}}
        self.assertEqual(wt.override_for(hull, 'abc', 'SS_BULLET_TUG'), {'hull': 'tug', 'calibre': 0.1})

    def test_mount_tier_rule(self):
        # docs/architecture/bolt-muzzle-look.md section 1 (2026-11-08 #4): the catalogue's size, the smallest tier holding
        # 10 % of the mounting rows; unmounted is a fighter. One odd M3 row of twelve (the stock PSP) no longer decides.
        self.assertEqual(wt.mount_tier_of({'M3': 1, 'M2': 11}), 'capital')
        self.assertEqual(wt.mount_tier_of({'M3': 1, 'M6': 2, 'M2': 9}), 'corvette')
        self.assertEqual(wt.mount_tier_of({'M3': 4, 'M6': 2, 'M2': 1}), 'fighter')
        self.assertEqual(wt.mount_tier_of({'M6': 2, 'M2': 1}), 'corvette')
        self.assertEqual(wt.mount_tier_of({'M7': 1, 'TL': 2}), 'capital')
        self.assertEqual(wt.mount_tier_of({'TS': 1}), 'corvette')
        self.assertEqual(wt.mount_tier_of({}), 'fighter')
        self.assertEqual(wt.range_of({'speed': 500 * 1000, 'lifetime_ms': 2000}), 2000.0)   # 1000 m/s x 2 s
        self.assertIsNone(wt.range_of({'speed': None, 'lifetime_ms': 2000}))
        self.assertEqual(wt.SCHEMA, 3)   # 3: tables.ship_profiles
        # an unmounted row is marked and listed (the generator logs it: unknown 7's fallback)
        w = {'index': 3, 'weapon_id': 'SS_BULLET_X', 'body_key': None, 'speed': None, 'lifetime_ms': None}
        unmounted, mounted = wt.row_entry(w, None, None, {}), wt.row_entry(dict(w, index=4), None, None, {}, {'M2': 1})
        self.assertEqual((unmounted['mount_tier'], unmounted['mount_fallback']), ('fighter', True))
        self.assertEqual((mounted['mount_tier'], mounted['mount_fallback']), ('capital', False))
        table = {'tables': {'bullets': {'entries': [unmounted, mounted]}}}
        self.assertEqual([e['row'] for e in wt.mount_fallback_rows(table)], [3])

    def test_mount_tiers_match_the_catalogue(self):
        # the generator's rule over the tracked catalogue's used-by (both AP views): 29/15/17 by the size rule (2026-11-08;
        # the old any-fighter rule gave size_derivation's 30/19/12), and equal to the catalogue's own size column
        tiers = {}
        for view in ('stock_ap', 'mayhem3'):
            data = json.loads((ROOT / f'tools/effects/weapon_catalog/{view}.json').read_text())
            for e in data['weapons']:
                if e['style'].split('+')[0] == 'beam' or not e.get('lasers'):
                    continue
                t = wt.mount_tier_of(e['used_by'])
                self.assertEqual(t, wt.SIZE_TIERS.get(e['size'], 'fighter'), e['id'])
                tiers[t] = tiers.get(t, 0) + 1
        self.assertEqual((tiers['fighter'], tiers['corvette'], tiers['capital']), (29, 15, 17))

    def test_shipped_overrides_file_schema(self):
        data = json.loads(OVERRIDES.read_text())
        self.assertEqual(data['schema'], 2)
        # docs/architecture/weapon-look.md 3.1 (the variants) and hull-impact-classes.md section 2 (the class pins): the
        # by_id hull keys are exactly the census script's pin list, the tug's calibre beside it
        sys.path.insert(0, str(ROOT / 'verification/results/weapon-effects'))
        import hull_class_census as census  # noqa: E402
        self.assertEqual({k: v['hull'] for k, v in data['by_id'].items() if 'hull' in v}, census.PINS)
        self.assertEqual(data['by_id']['SS_BULLET_TUG'], {'hull': 'tug', 'calibre': 0.1})
        for entry in data['by_id'].values():
            if 'hull' in entry:
                self.assertIn(entry['hull'], wt.HULL_VARIANTS)
                self.assertNotEqual(entry['hull'], 'beam')
        # the bolt hand pins (bolt-muzzle-look.md 11.5): MASS/MD a dim haloless slug in the texture tint, HEPT a slug,
        # FBL an orb at 2.0 w; every other row carries none
        pins = {k: {f: v[f] for f in ('style',) + wt.BOLT_PINS if f in v} for k, v in data['by_id'].items()}
        pins = {k: v for k, v in pins.items() if v}
        dim = {'style': 'slug', 'bolt_e_body': 0.6, 'bolt_halo': False}
        # the stock MASS body has no decodable texture: its texture-tint pin would be a no-op (review 2026-11-08)
        self.assertEqual(pins, {'SS_BULLET_MD': dict(dim, bolt_tint='texture'), 'SS_BULLET_MASS': dim,
                                'SS_BULLET_HEPT': {'style': 'slug'},
                                'SS_BULLET_FBL': {'style': 'orb', 'bolt_w_orb': 2.0}})
        w = {'index': 0, 'weapon_id': 'SS_BULLET_MD', 'body_key': None, 'speed': None, 'lifetime_ms': None}
        e = wt.row_entry(w, None, None, wt.override_for(data, 'none', 'SS_BULLET_MD'))
        self.assertEqual((e['style'], e['bolt_e_body'], e['bolt_halo'], e['bolt_tint']), ('slug', 0.6, False, 'texture'))
        self.assertEqual(set(data['by_table']),
                         {'be762076cdc38b03', '7b4ab0a6cea2cb07', 'f0cb79fd63c46318'})  # stock AP, Mayhem 3, FL
        self.assertIsInstance(wt.load_overrides(OVERRIDES), dict)


    def test_uv_layout_finds_head_and_fins(self):
        # the geometric size (projectile-look.md "Geometric size (2026-11-10)"): the IRE's layout (bullet-draw-batch.md
        # section 6) rebuilt from synthetic triangles: eight half-fins from the axis (v 0.3519) to the rim (v 0.2792),
        # u tail -> head, and three crossed head quads each mapping the glow box u 0.7833..0.9831, v 0.7673..0.967
        def quad(p, uv):
            return [[p[0], p[1], p[2]], [p[0], p[2], p[3]]], [[uv[0], uv[1], uv[2]], [uv[0], uv[2], uv[3]]]
        tris = []

        def add(points, uvs):
            ps, us = quad(points, uvs)
            for pt, ut in zip(ps, us):
                tris.append([tuple(a) + tuple(b) for a, b in zip(pt, ut)])
        import math
        for k in range(8):
            x, y = 0.01337 * math.cos(k * math.pi / 4), 0.01337 * math.sin(k * math.pi / 4)
            add([(0, 0, -0.2473), (x, y, -0.2473), (x, y, 0.2473), (0, 0, 0.2473)],
                [(0.0131, 0.3519), (0.0131, 0.2792), (0.9767, 0.2792), (0.9767, 0.3519)])
        for k in range(3):
            a = k * math.pi / 3
            c, s = 0.005 * math.cos(a), 0.005 * math.sin(a)
            add([(-c, -s, 0.148), (c, s, 0.148), (c, s, 0.202), (-c, -s, 0.202)],
                [(0.7833, 0.7673), (0.9831, 0.7673), (0.9831, 0.967), (0.7833, 0.967)])
        got = wt.uv_layout(tris)
        self.assertEqual((got['groups'], got['head_reason'], got['fin_reason']), (2, 'ok', 'ok'))
        self.assertEqual(got['head'], dict(u=0.8832, v=0.86715, r=0.0999))  # the half of the wider side
        self.assertEqual(got['fin'], dict(axis_v=0.3519, rim_v=0.2792, u0=0.0131, u1=0.9767))
        # a third group past the fins' UV box but inside their band (the axis side): the fins are dropped (the shader
        # would shrink it too)
        add([(0, 0, 0), (0.01, 0, 0), (0.01, 0, 0.01), (0, 0, 0.01)], [(0.1, 0.355), (0.2, 0.355), (0.2, 0.365), (0.1, 0.365)])
        got = wt.uv_layout(tris)
        self.assertEqual((got['fin'], got['fin_reason'], got['head_reason']), (None, 'footprint_shared', 'ok'))
        # a mesh whose square UV cluster is not sprite quads (small triangles tiling the box): no head
        mesh = []
        for i in range(4):
            for j in range(4):
                u, v = i / 4, j / 4
                mesh.append([(0, 0, 0, u, v), (1, 0, 0, u + 0.25, v), (1, 1, 0, u + 0.25, v + 0.25)])
                mesh.append([(0, 0, 0, u, v), (1, 1, 0, u + 0.25, v + 0.25), (0, 1, 0, u, v + 0.25)])
        got = wt.uv_layout(mesh)
        self.assertEqual((got['head'], got['head_reason'], got['fin']), (None, 'none_square', None))


class WeaponEffectsTableReader(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('clang++') or shutil.which('c++') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('a host C++ compiler is required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-weapon-table-')
        src = Path(cls.tmp.name) / 'r.cpp'
        src.write_text(READER)
        cls.exe = Path(cls.tmp.name) / 'r'
        subprocess.run([compiler, '-std=c++17', '-O2', '-I', str(ROOT / 'src/proxy'), str(src), '-o', str(cls.exe)],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def read(self, table_json, row=None):
        args = [str(self.exe)] + ([str(row)] if row is not None else [])
        out = subprocess.run(args, input=json.dumps(table_json), capture_output=True, text=True)
        fields = {}
        for line in out.stdout.splitlines():
            fields.update(dict(kv.split('=') for kv in line.split()))
        return out.returncode, fields

    def test_reader_round_trips_a_generated_table(self):
        code, f = self.read(sample_table(), row=0)
        self.assertEqual(code, 0)
        self.assertEqual((f['ok'], f['rows'], f['missile_rows'], f['entries']), ('1', '45', '36', '12'))
        self.assertEqual(f['shape'], 'tracer')          # row 0: from elongation 18.5
        self.assertEqual(f['texture'], '275279')        # 121,82,39 -> r|g<<8|b<<16
        _, f1 = self.read(sample_table(), row=1)
        self.assertEqual(f1['shape'], 'beam')           # row 1: style override
        self.assertEqual(int(f1['calibre_q']), round(0.5 * 255))
        self.assertTrue(int(f1['bits']) & 0x2)          # row_style_override
        self.assertTrue(int(f1['bits']) & 0x4)          # row_tint_override
        self.assertTrue(int(f1['bits']) & 0x8)          # row_calibre_override
        # the hull-hit variants (weapon-look.md 3.1): row_hull_repair 0x20, row_hull_tug 0x40; an unknown name pins none
        _, f3 = self.read(sample_table(), row=3)
        _, f4 = self.read(sample_table(), row=4)
        _, f5 = self.read(sample_table(), row=5)
        self.assertEqual(int(f3['bits']) & 0x60, 0x20)
        self.assertEqual(int(f4['bits']) & 0x60, 0x40)
        self.assertEqual(int(f4['calibre_q']), round(0.1 * 255))
        self.assertEqual(int(f5['bits']) & 0xe0, 0)
        # the class pins (hull-impact-classes.md section 2): row_hull_class 0x80 with the class; beam is not pinnable
        _, f6 = self.read(sample_table(), row=6)
        _, f7 = self.read(sample_table(), row=7)
        self.assertEqual((int(f6['bits']) & 0xe0, f6['hull_class']), (0x80, 'ballistic'))
        self.assertEqual((int(f7['bits']) & 0xe0, f7['hull_class']), (0, '-'))
        self.assertEqual(f['overrides'], '6')            # rows 1, 3, 4 (its calibre the same row), 6, 10, 11 (style); none for 5, 7
        # step 4: mount_tier (0 fighter, 1 corvette, 2 capital; absent = fighter) and range_m (metres; null = 0)
        _, f8 = self.read(sample_table(), row=8)
        _, f9 = self.read(sample_table(), row=9)
        self.assertEqual((f['mount_tier'], f['range_m'], f['mount_keys']), ('0', '0', '2'))
        self.assertEqual((f8['mount_tier'], f8['range_m']), ('2', '4957'))
        self.assertEqual((f9['mount_tier'], f9['range_m']), ('1', '0'))
        schema2 = sample_table()
        schema2['schema'] = 2
        self.assertEqual(self.read(schema2, row=8)[1]['mount_tier'], '2')
        # the bolt hand pins (bolt-muzzle-look.md 11.5): pin_halo_off 1, pin_tint_texture 2, pin_e_body 4, pin_w_orb 8
        _, f10 = self.read(sample_table(), row=10)
        _, f11 = self.read(sample_table(), row=11)
        self.assertEqual((f10['bolt_pin'], f10['e_body_q'], f10['w_orb_q']), ('7', '60', '0'))
        self.assertEqual((f11['bolt_pin'], f11['e_body_q'], f11['w_orb_q'], f11['shape']), ('8', '0', '200', 'orb'))
        self.assertEqual((f['bolt_pin'], f['bolt_pins']), ('0', '2'))

    def test_reader_handles_malformed_bolt_pins(self):
        # bolt-muzzle-look.md 11.5's pins on row 10 of the sample. A pin of the wrong JSON type makes the whole file
        # malformed (exit 1, the table stays empty); a number out of range or rounding to a stored 0, or another
        # bolt_tint name, drops that row alone (bolt_rejected counts it) and the rest of the table loads.
        def with_pin(**pin):
            t = sample_table()
            row = t['tables']['bullets']['entries'][10]
            for k in ('bolt_e_body', 'bolt_halo', 'bolt_tint'):
                row.pop(k, None)
            row.update(pin)
            return t
        for wrong_type in ({'bolt_e_body': '0.6'}, {'bolt_w_orb': [2.0]}, {'bolt_halo': 'no'}, {'bolt_tint': 1}):
            code, f = self.read(with_pin(**wrong_type), row=10)
            self.assertEqual((code, f['ok']), (1, '0'), wrong_type)
        for dropped in ({'bolt_e_body': 3.0}, {'bolt_e_body': -0.5}, {'bolt_w_orb': 9.0}, {'bolt_e_body': 0},
                        {'bolt_w_orb': 0.004}, {'bolt_w_orb': 0}, {'bolt_tint': 'light'}):
            code, f = self.read(with_pin(**dropped), row=10)
            self.assertEqual((code, f['ok'], f['entries'], f['bolt_rejected'], f['present'], f['bolt_pin']),
                             (0, '1', '11', '1', '0', '0'), dropped)
        code, f = self.read(with_pin(bolt_e_body=0.01, bolt_halo=True), row=10)   # the smallest stored E_b; true pins nothing
        self.assertEqual((code, f['bolt_rejected'], f['bolt_pin'], f['e_body_q']), (0, '0', '4', '1'))
        w = {'index': 0, 'weapon_id': 'SS_BULLET_X', 'body_key': None, 'speed': None, 'lifetime_ms': None}
        for bad in ({'bolt_e_body': 0.0}, {'bolt_w_orb': 0.004}, {'bolt_e_body': 2.6}, {'bolt_halo': True},
                    {'bolt_tint': 'light'}):
            with self.assertRaises(wt.GenerateError, msg=str(bad)):   # the generator never writes such a row
                wt.row_entry(w, None, None, bad)

    def test_reader_reads_bullet_textures(self):
        # tables.bullet_textures (the bullet re-shade's per-texture normaliser): kept entries in order, the p99 x 65535;
        # a bad byte count, hash or p99 drops the entry (counted); a wrong JSON type is malformed; the C++ hashes are
        # the generator's
        good = {'name': 'fx_bullets_diff', 'bytes': 349680, 'head64': 'd66d9c9af0d8ac47', 'fnv1a64': '34acf50c2603cdd5',
                'peak': 0.973, 'p99': 0.6169, 'rows': [0, 1]}
        t = sample_table()
        t['tables']['bullet_textures'] = [good, dict(good, bytes=0), dict(good, head64='xyz'), dict(good, p99=1.5),
                                          dict(good, fnv1a64='34ACF50C2603CDD5', bytes=1398256)]
        code, f = self.read(t)
        self.assertEqual((code, f['bullet_textures'], f['bullet_textures_dropped']), (0, '2', '3'))
        self.assertEqual((f['tex0_bytes'], f['tex0_head'], f['tex0_hash'], f['tex0_p99_q']),
                         ('349680', 'd66d9c9af0d8ac47', '34acf50c2603cdd5', str(round(0.6169 * 65535))))
        self.assertEqual(f['entries'], '12')      # the rows are untouched
        t['tables']['bullet_textures'] = [dict(good, bytes='349680')]
        self.assertEqual(self.read(t)[0], 1)
        t['tables']['bullet_textures'] = {'not': 'a list'}
        self.assertEqual(self.read(t)[0], 1)
        data = bytes(range(256)) * 20            # longer than the 4096-byte head
        out = subprocess.run([str(self.exe), 'fnv'], input=data, capture_output=True)
        got = dict(kv.split('=') for kv in out.stdout.decode().split())
        self.assertEqual(got, {'head64': f'{wt.fnv1a64(data[:wt.HEAD_BYTES]):016x}', 'fnv1a64': f'{wt.fnv1a64(data):016x}'})

    def test_reader_reads_bullet_bodies(self):
        # tables.bullet_bodies (the geometric size): 16.16 words in LayoutWord order (flags head 1 | fin 2), a null group
        # absent, a bad entry dropped (counted), a wrong JSON type malformed; no table: the identity constants
        ire = {'body': 'objects/effects/weapons/bullet_ls_alpha', 'head': {'u': 0.88319, 'v': 0.86715, 'r': 0.09988},
               'fin': {'axis_v': 0.35191, 'rim_v': 0.27924, 'u0': 0.01309, 'u1': 0.97672}, 'groups': 2}
        t = sample_table()
        t['tables']['bullet_bodies'] = [ire, dict(ire, body=None), dict(ire, head={'u': 0.5, 'v': 0.5, 'r': 0}),
                                        dict(ire, fin=dict(ire['fin'], rim_v=0.35191)),
                                        dict(ire, body='objects/v/00518', fin=None)]
        code, f = self.read(t)
        self.assertEqual((code, f['bullet_bodies'], f['bullet_bodies_dropped'], f['bullet_bodies_key']), (0, '2', '3', '1'))
        self.assertEqual(f['body0_hash'], f['ire_hash'])
        q = lambda x: round(x * 65536)  # noqa: E731
        self.assertEqual(f['body0_words'], ','.join(str(w) for w in (3, q(0.88319), q(0.86715), q(0.09988), q(0.35191),
                                                                     q(0.27924), q(0.01309), q(0.97672))))
        shrunk = [float(x) for x in f['body0_shrunk'].split(',')]
        kept = [float(x) for x in f['body0_kept'].split(',')]
        self.assertAlmostEqual(shrunk[0], 0.88319, places=4)
        self.assertAlmostEqual(shrunk[2], 1 / 0.09988, places=2)
        self.assertEqual((shrunk[3], shrunk[7]), (2.0, 0.5))                 # 1 / rs_head, the softness
        self.assertAlmostEqual(shrunk[5], 1 / (0.27924 - 0.35191), places=2)
        self.assertAlmostEqual(shrunk[6], 1 / 0.6, places=4)
        self.assertEqual((kept[3], kept[6]), (0.0, 0.0))                     # keys at 1: the falloffs' argument 0
        self.assertEqual(f['entries'], '12')
        code, f = self.read(sample_table())                                  # no table: tolerated, identity
        self.assertEqual((code, f['bullet_bodies'], f['bullet_bodies_key']), (0, '0', '0'))
        self.assertEqual(f['body0_kept'], '-100,-100,1,0,-100,1,0,0.5')
        t['tables']['bullet_bodies'] = [dict(ire, head={'u': '0.5', 'v': 0.5, 'r': 0.1})]
        self.assertEqual(self.read(t)[0], 1)
        t['tables']['bullet_bodies'] = {'not': 'a list'}
        self.assertEqual(self.read(t)[0], 1)

    def test_body_map(self):
        # bullet_reshade_core.h build_body_map / body_layout (weapon_effects_patch.cpp bullet_body_layout) against a fake
        # engine: TBullets not readable yet -> not final; a failed row read -> not final, retried next time, counted;
        # then built (row 0's body id maps to its entry, an unknown id gets none: the identity, MotionOutput logs it once);
        # every row read failing -> final without layouts after 64 builds; the engine's row count not the table's, no
        # table, or no bullet_bodies -> final, off
        t = sample_table()
        t['tables']['bullets']['entries'][0]['body'] = 'objects/effects/weapons/bullet_ls_alpha'
        t['tables']['bullet_bodies'] = [{'body': 'objects/effects/weapons/bullet_ls_alpha',
                                         'head': {'u': 0.88319, 'v': 0.86715, 'r': 0.09988}, 'fin': None}]

        def run(table, *args):
            out = subprocess.run([str(self.exe), 'bodymap', *args], input=json.dumps(table), capture_output=True, text=True)
            return dict(kv.split('=') for line in out.stdout.splitlines() for kv in line.split())
        f = run(t)
        self.assertEqual((f['early'], f['after_fail'], f['failures_once'], f['built']), ('0', '0', '1', '1'))
        self.assertEqual((f['state'], f['reason'], f['mapped'], f['row0_head_u'], f['unknown']),
                         ('1', 'ok', '1', str(round(0.88319 * 65536)), '0'))
        self.assertEqual((f['giveup_builds'], f['giveup_state'], f['giveup_reason']), ('64', '2', 'row_read_failed'))
        f = run(t, '46')                                       # the engine has 46 rows, the table 45: unbuilt and
        self.assertEqual((f['early'], f['built'], f['state'], f['reason'], f['row0_head_u']),   # retried, off only
                         ('0', '0', '0', 'unbuilt', '-1'))                                      # after 64 builds
        self.assertEqual((f['giveup_builds'], f['giveup_state'], f['giveup_reason']), ('64', '2', 'stale_rows'))
        del t['tables']['bullet_bodies']
        f = run(t)                                             # no layouts in the table: final at once, off
        self.assertEqual((f['early'], f['state'], f['reason']), ('1', '2', 'no_bullet_bodies'))
        f = run({'schema': 9})                                 # no table (a rejected file): final at once, off
        self.assertEqual((f['early'], f['state'], f['reason']), ('1', '2', 'no_table'))

    def test_reader_rejects_a_foreign_or_malformed_table(self):
        code, f = self.read({'schema': 4, 'tables': {}})
        self.assertEqual(code, 1)   # schema must be 1, 2 or 3 (3: ship_profiles)
        code, f = self.read({'schema': 1, 'tables': {'bullets': {'rows': 45, 'entries': 'not a list'}}})
        self.assertEqual(code, 1)

    def read_text(self, text):
        out = subprocess.run([str(self.exe)], input=text, capture_output=True, text=True)
        return out.returncode, dict(kv.split('=') for kv in out.stdout.split())

    def test_reader_reads_ship_profiles(self):
        """Schema 3 tables.ship_profiles (the lathe bubble, weapon_effects_lathe.h): kept entries keyed by their TShips
        index with the class token's engine id; a bad entry is dropped and counted, a wrong JSON type is malformed."""
        def entry(index, **kw):
            e = {'tships_index': index, 'scene': 'ships\\argon\\argon_M2_scene', 'class': 'OBJ_SHIP_M2', 'units': 'scene',
                 'axis': 'z', 'z0': -511093.0, 'z1': 545751.0, 'radius': 545994.2, 'points': 105399,
                 'slices': [[0.0, -1000.0 * i, 70000.0 + i, 60000.0] for i in range(32)]}
            e.update(kw)
            return e
        good = [entry(11), entry(3, **{'class': 2023}), entry(5, **{'class': 'class_id:2061'})]
        bad = [entry(11), entry(4096), entry(7, units='metres'), entry(8, axis='y'), entry(9, z1=-600000.0),
               entry(10, slices=[[0, 0, 1, 1]] * 31), entry(12, slices=[[0, 0, -1, 1]] * 32),
               entry(13, slices=[[0, 0, 0, 0]] * 32), entry(14, **{'class': 'OBJ_NOT_A_CLASS'})]
        table = dict(sample_table(), schema=3)
        table['tables'] = dict(table['tables'], ship_profiles=good + bad)

        def run(t, row):
            out = subprocess.run([str(self.exe), 'profiles', str(row)], input=json.dumps(t), capture_output=True,
                                 text=True)
            return out.returncode, dict(kv.split('=') for kv in out.stdout.split())
        code, f = run(table, 11)
        self.assertEqual(code, 0)
        # kept: 11, 3, 5 and 14 (an unknown class is kept, never matched); dropped: the second 11 and seven bad entries
        self.assertEqual((f['present'], f['profiles'], f['dropped'], f['class_unknown'], f['count']),
                         ('1', '4', '8', '1', '4'))
        self.assertEqual((f['found'], f['class'], f['z0'], f['z1']), ('1', '2022', '-511093.0', '545751.0'))
        self.assertEqual(f['s16'], '0.000,-16000.000,70016.000,60000.000')
        self.assertEqual(run(table, 3)[1]['class'], '2023')
        self.assertEqual(run(table, 5)[1]['class'], '2061')
        self.assertEqual(run(table, 14)[1]['class'], '-1')
        self.assertEqual(run(table, 7)[1]['found'], '0')
        broken = dict(table, tables=dict(table['tables'], ship_profiles=[entry(11, slices='none')]))
        code, f = run(broken, 11)                           # a wrong JSON type: the whole file is malformed
        self.assertEqual((code, f['ok'], f['count'], f['found']), (1, '0', '0', '0'))
        code, f = run(dict(sample_table(), schema=2), 11)  # an older table: no profiles, still read, nothing allocated
        self.assertEqual((code, f['present'], f['count'], f['carries']), (0, '0', '0', '0'))
        self.assertEqual((f['tships_rows'], run(table, 11)[1]['carries']), ('0', '1'))  # no generated_from: unverified
        # the generator's row count (generated_from.ship_profiles.rows): the stage's stale-table guard
        recorded = dict(table, generated_from={'tool': 'x', 'ship_profiles': {'rows': 426, 'profiles': 4}})
        self.assertEqual(run(recorded, 11)[1]['tships_rows'], '426')
        # past profile_capacity (1024): the first 1024 kept, the rest dropped and counted
        many = dict(table, tables=dict(table['tables'], ship_profiles=[entry(i) for i in range(1030)]))
        code, f = run(many, 1023)
        self.assertEqual((code, f['profiles'], f['dropped'], f['count'], f['found']), (0, '1024', '6', '1024', '1'))
        self.assertEqual(run(many, 1025)[1]['found'], '0')

    def test_reader_rejects_trailing_bytes_and_duplicate_rows(self):
        text = json.dumps(sample_table())
        self.assertEqual(self.read_text(text + '\n  \t\r\n')[0], 0)    # trailing whitespace is fine
        code, f = self.read_text(text + ' {}')                         # a second value after the object
        self.assertEqual((code, f['ok'], f['rows'], f['missile_rows']), (1, '0', '0', '0'))  # the table cleared
        self.assertEqual(self.read_text(text + 'x')[0], 1)
        duplicate = sample_table()
        duplicate['tables']['bullets']['entries'].append(dict(duplicate['tables']['bullets']['entries'][0]))
        code, f = self.read_text(json.dumps(duplicate))
        self.assertEqual((code, f['ok'], f['rows'], f['missile_rows']), (1, '0', '0', '0'))  # nothing kept


FL_GAME = Path('/mnt/c/X3FL')
LONG_SCENE = rb"""/ synthetic ship scene
VER: 3;
P 0; B ships\x\hull; C 1; N Bships\x\hull_1b; b
  { 0x2002;  0; 0; 1000;  0.000000; 0.000000; 0.000000; 0.000000;  -1;  -1; } // 0
P 1; B ships\x\pod; F 0; N pod;  // idx 1
  { 0x2002;  100; 0; 0;  0.250000; 0.000000; 0.000000; 1.000000;  -1;  -1; } // 0
P 2; B effects\engines\fx_t;
  { 0x2002;  0; 0; -99999;  0.000000; 0.000000; 0.000000; 0.000000;  -1;  -1; } // 0
P 3; B 19027;
  { 0x2012;  0.500000; 0.000000; 1.000000; 0.000000;  -1;  -1; } // 0
"""
FX_SCENE = rb"""P 0; B effects\engines\fx_t;
  { 0x2002;  0; 0; 0;  0.000000; 0.000000; 0.000000; 0.000000;  -1;  -1; } // 0
"""


def tships_row(scene, cls='OBJ_SHIP_M3', ident='SS_SH_X'):
    f = ['0'] * 55
    f[16], f[52], f[54] = scene, cls, ident
    return ';'.join(f) + ';'


def ship_game(folder):
    """An AP root whose 01.cat holds the weapon step's empty type tables, a TShips of six rows (short, scene 0, a
    missing scene, the long scene twice, an effects-only scene) and the scenes and bodies."""
    import test_engine_bodies as teb
    import test_regenerate
    from sector_fog_census import write_catalogue
    game = Path(folder) / 'X3'
    game.mkdir(parents=True)
    (game / 'X3AP.exe').write_bytes(b'MZ stub')
    rows = ['a;b;c;', tships_row('0'), tships_row('ships\\x\\gone_scene'), tships_row('ships\\x\\long_scene'),
            tships_row('ships\\x\\long_scene', 'OBJ_SHIP_M2', 'SS_SH_Y'), tships_row('ships\\x\\fx_scene')]
    tships = ('0;%d;\r\n' % len(rows) + '\r\n'.join(rows) + '\r\n').encode()
    mat = teb.legacy_material(b'tex_grey.dds')
    hull = teb.body(mat, 2000, [((-32768, 32768), (-16384, 16384), (-65536, 65536))])   # +-1000, +-500, +-2000
    pod = teb.body(mat, 100, [((0, 65536), (0, 0), (0, 65536))])                       # x 0..100, z 0..100
    members = [m for m in test_regenerate.weapon_members() if m[0] != 'types/TShips.txt'] + [
        ('types/TShips.txt', tships), ('objects/ships/x/long_scene.pbd', LONG_SCENE),
        ('objects/ships/x/fx_scene.pbd', FX_SCENE), ('objects/ships/x/hull.pbb', hull), ('objects/ships/x/pod.pbb', pod)]
    write_catalogue(game / '01.cat', members)
    return game


class ShipProfiles(unittest.TestCase):
    """tables.ship_profiles (schema 3, tools/effects/ship_profiles.py) on a synthetic tree."""

    def test_profile_parts_parents_rotation_and_skips(self):
        import engine_bodies
        import ship_profiles as sp
        from unittest import mock
        with tempfile.TemporaryDirectory() as folder:
            game = ship_game(folder)
            assets, _ = engine_bodies.load_assets(game, False, 'ap')
            with mock.patch.multiple(sp, PART_FRACTION=0.0, SMOOTH=False):  # the pod (50 of 1000 wide) kept, raw
                entries, skipped, prov = sp.ship_profiles(assets)
            default = sp.ship_profiles(assets)
        self.assertEqual([(s['tships_index'], s['reason']) for s in skipped],
                         [(0, 'short_row'), (1, 'no_scene'), (2, 'not_found'), (5, 'no_points')])
        self.assertEqual(skipped[2]['scene'], 'ships\\x\\gone_scene')
        self.assertEqual([e['tships_index'] for e in entries], [3, 4])
        a, b = entries
        self.assertEqual((a['class'], b['class'], a['units'], a['axis']), ('OBJ_SHIP_M3', 'OBJ_SHIP_M2', 'scene', 'z'))
        self.assertEqual({k: v for k, v in a.items() if k not in ('tships_index', 'class')},
                         {k: v for k, v in b.items() if k not in ('tships_index', 'class')})
        # hull: points x value / 65536 at z + 1000; the pod a child of part 0, a quarter turn about z then (100, 0, 0)
        self.assertEqual((a['z0'], a['z1'], a['points']), (-1000.0, 3000.0, 8))
        self.assertEqual(a['radius'], round((1000 ** 2 + 500 ** 2 + 3000 ** 2) ** 0.5, 3))
        self.assertEqual(len(a['slices']), sp.SLICES)
        self.assertEqual(a['slices'][0], [0.0, 0.0, 1000.0, 500.0])
        self.assertEqual(a['slices'][31], [0.0, 0.0, 1000.0, 500.0])
        self.assertEqual(a['slices'][16], [100.0, 50.0, 0.0, 50.0])          # the pod: x 100, y 0..100, z 1000..1100
        self.assertEqual(a['slices'][8], [50.0, 25.0, 500.0, 275.0])          # empty: halfway between 0 and 16
        self.assertEqual((prov['profiles'], prov['skipped'], prov['rows']), (2, 4, 6))
        # tallied over the distinct profiled scenes: long_scene's effects part and its absent v\19027
        self.assertEqual((prov['scene_parts'], prov['effect_parts_excluded'], prov['parts_unresolved']), (4, 1, 1))
        self.assertEqual((a['trim_lo'], a['trim_hi'], a['parts_excluded'], a['smoothed']), (0.0, 0.0, 0, False))
        # the defaults: the pod is under 6 % of the hull's 1000 (rule a), the hull's two end rings remain
        d = default[0][0]
        self.assertEqual((d['z0'], d['z1'], d['points'], d['parts_excluded'], d['smoothed']), (-1000.0, 3000.0, 4, 1, True))
        self.assertEqual(d['slices'], [[0.0, 0.0, 1000.0, 500.0]] * sp.SLICES)
        self.assertEqual((default[2]['thin_parts_excluded'], default[2]['part_fraction'], default[2]['trim_fraction'],
                          default[2]['smoothed']), (1, sp.PART_FRACTION, sp.TRIM_FRACTION, True))

    @staticmethod
    def box(x, y, z0, z1, step=100.0, cx=0.0):
        """The corners of a lateral box +-x, +-y at every z from z0 to z1 (inclusive) in steps."""
        import numpy as np
        zs = np.arange(z0, z1 + step / 2, step)
        return np.array([(cx + sx * x, sy * y, z) for z in zs for sx in (-1, 1) for sy in (-1, 1)], np.float64)

    def test_thin_parts_rule(self):
        import numpy as np
        import ship_profiles as sp
        hull = self.box(1000, 500, -2000, 2000, 4000)
        mast = self.box(10, 10, 2000, 4000)                  # 1 % of the hull's 1000: dropped
        turret = self.box(80, 40, 0, 100, cx=600)            # 8 %: kept
        boom = self.box(55, 59, -500, 500, cx=-900)          # 5.9 %: dropped (the limit is 60)
        pts, excluded = sp.hull_points([hull, mast, turret, boom])
        self.assertEqual(excluded, 2)
        self.assertEqual(len(pts), len(hull) + len(turret))
        self.assertEqual(pts[:, 2].max(), 2000.0)
        # a scene that is only thin geometry keeps it: one part, or parts each thin but wide together
        pts, excluded = sp.hull_points([mast])
        self.assertEqual((pts.tolist(), excluded), (mast.tolist(), 0))
        apart = [self.box(10, 10, 0, 100, cx=-1000), self.box(10, 10, 0, 100, cx=1000)]
        pts, excluded = sp.hull_points(apart)
        self.assertEqual((len(pts), excluded), (sum(len(c) for c in apart), 0))
        self.assertEqual(sp.hull_points([hull, mast], 0.0)[1], 0)
        self.assertTrue(np.array_equal(sp.hull_points([hull, mast], 0.0)[0], np.concatenate([hull, mast])))

    def test_thin_ends_rule(self):
        import numpy as np
        import ship_profiles as sp
        hull = self.box(1000, 500, 0, 3100)
        # a needle (5 x 5, 0.005 % of the hull's area) from 3200 to 6400: slices 16..31 trimmed, 32 slices re-sampled
        p = sp.profile(np.concatenate([hull, self.box(5, 5, 3200, 6400)]), smooth=False)
        self.assertEqual((p['z0'], p['z1'], p['trim_lo'], p['trim_hi'], p['points']), (0.0, 3100.0, 0.0, 3300.0, len(hull)))
        self.assertEqual((p['slices'][0], p['slices'][31]), ([0.0, 0.0, 1000.0, 500.0], [0.0, 0.0, 1000.0, 500.0]))
        # both ends; a lone tip point past empty slices is an end too (empty slices never count as wide)
        tail = self.box(5, 5, -1000, -800)
        p = sp.profile(np.concatenate([tail, hull, [[0.0, 0.0, 9000.0]]]), smooth=False)
        self.assertEqual((p['z0'], p['z1'], p['trim_lo'], p['trim_hi']), (0.0, 3100.0, 1000.0, 5900.0))
        # an internal neck between two hull sections is kept, and so is an end at the fraction
        neck = np.concatenate([self.box(1000, 500, 0, 1000), self.box(5, 5, 1100, 2000), self.box(1000, 500, 2100, 3100)])
        p = sp.profile(neck, smooth=False)
        self.assertEqual((p['z0'], p['z1'], p['trim_lo'], p['trim_hi']), (0.0, 3100.0, 0.0, 0.0))
        self.assertEqual(p['slices'][16][2:], [5.0, 5.0])
        edge = np.concatenate([hull, self.box(100, 50, 3200, 3300)])   # 1 % of the area: kept
        self.assertEqual(sp.profile(edge, smooth=False)['trim_hi'], 0.0)
        self.assertEqual(sp.profile(edge, fraction=0.011, smooth=False)['trim_hi'], 200.0)

    def test_width_easing(self):
        import numpy as np
        import ship_profiles as sp
        rows = [[float(i), 0.0, 1.0, 2.0] for i in range(10)]
        rows[5] = [5.0, 0.0, 10.0, 2.0]                       # a fin: hx steps 1 -> 10 -> 1
        eased = sp.smooth_widths(rows)
        self.assertEqual([r[2] for r in eased], [1, 1, 1, 4, 7, 10, 7, 4, 1, 1])
        self.assertEqual([r[3] for r in eased], [2.0] * 10)
        self.assertEqual([r[:2] for r in eased], [r[:2] for r in rows])           # centres unchanged
        self.assertTrue(all(e[k] >= r[k] for e, r in zip(eased, rows) for k in (2, 3)))
        ends = sp.smooth_widths([[0, 0, w, 1.0] for w in (1.0, 1.0, 1.0, 7.0)])
        self.assertEqual([r[2] for r in ends], [1.0, 3.0, 5.0, 7.0])
        blunt = sp.smooth_widths([[0, 0, w, 1.0] for w in (1.0, 7.0, 7.0, 7.0)])  # no running max at an end slice:
        self.assertEqual([r[2] for r in blunt], [3.0, 7.0, 7.0, 7.0])              # (2 x 1 + 7) / 3, not 7
        pts = np.concatenate([self.box(100, 50, 0, 3100), self.box(1000, 50, 1600, 1600)])
        raw, smooth = sp.profile(pts, smooth=False), sp.profile(pts)
        self.assertEqual((raw['smoothed'], smooth['smoothed']), (False, True))
        self.assertEqual({k: v for k, v in raw.items() if k not in ('slices', 'smoothed')},
                         {k: v for k, v in smooth.items() if k not in ('slices', 'smoothed')})
        self.assertEqual([round(s[2]) for s in smooth['slices'][14:19]], [400, 700, 1000, 700, 400])
        self.assertTrue(all(s[k] >= r[k] for s, r in zip(smooth['slices'], raw['slices']) for k in (2, 3)))

    def test_key_layouts(self):
        import numpy as np
        import ship_profiles as sp
        pos, rot = sp.parse_key(' 0x2012;  0.500000; 0.000000; 1.000000; 0.000000;  24966;  2; ')
        self.assertEqual(pos.tolist(), [0, 0, 0])                         # 0x10: a rotation-only key
        self.assertTrue(np.allclose(rot, np.diag([-1.0, 1.0, -1.0])))     # half a turn about y
        pos, rot = sp.parse_key(' 0xa002;  -5; -6469; -20489;  0.0; 1.0; 0.0; 0.0;  0.0; 0.0; 0.0; 0.0; 0.0;  3331; 1; ')
        self.assertEqual(pos.tolist(), [-5, -6469, -20489])
        self.assertTrue(np.allclose(rot, np.eye(3)))
        self.assertIsNone(sp.parse_key('junk; 1; 2'))
        self.assertEqual((sp.scene_name('4212'), sp.scene_name('0'), sp.scene_name(' ships\\a ')),
                         ('cut\\04212', None, 'ships\\a'))
        self.assertEqual((sp.part_body_name('940'), sp.part_body_name('-1')), ('v\\00940', None))

    def test_fast_lod0_reader_matches_bob1(self):
        import bob1
        import ship_profiles as sp
        import test_engine_bodies as teb
        mat = teb.legacy_material(b'tex_grey.dds')
        data = teb.body(mat, 2000, [((-32768, 32768), (-16384, 16384), (-65536, 65536)), ((0, 9), (0, 9), (0, 9))],
                        pin=65536)
        slow = sp.body_points(bob1.lods(bob1.parse(data, None))[0])
        fast = sp.visible_points(*sp.lod0_binary(data))
        self.assertEqual(fast.tolist(), slow.tolist())
        self.assertEqual(len(fast), 4)                       # the small quad and the pin sliver dropped
        tree = bob1.parse(data, None)
        lod = bob1.lods(tree)[0]
        lod['points'][0] = (3, *lod['points'][0][1:4], 0, 0)  # one point with a UV: mixed flags
        with self.assertRaises(sp.SlowPath):
            sp.lod0_binary(bob1.serialise(tree))

    def test_regenerate_writes_the_generator_table(self):
        # x3m-regenerate's weapon step (the bundle's code path) writes exactly weapon_effects_table.generate's table
        import x3m_regenerate as regen

        class Log:
            def __init__(self):
                self.lines = []

            def say(self, text=''):
                self.lines.append(text)
        with tempfile.TemporaryDirectory() as folder:
            game = ship_game(folder)
            regen.weapon_step(Log(), game)
            written = (game / 'x3m' / regen.WEAPON_EFFECTS_FILE).read_text()
            expected = wt.dumps(wt.generate(game))
        self.assertEqual(written, expected)
        table = json.loads(written)
        self.assertEqual(table['schema'], 3)
        self.assertEqual([e['tships_index'] for e in table['tables']['ship_profiles']], [3, 4])
        self.assertEqual([s['reason'] for s in table['generated_from']['ship_profiles_skipped']],
                         ['short_row', 'no_scene', 'not_found', 'no_points'])
        self.assertEqual((table['counts']['ship_profiles'], table['counts']['ship_profiles_skipped']), (2, 4))
        self.assertEqual(table['generated_from']['ship_profiles']['tships']['member'], 'types/TShips.txt')


@unittest.skipUnless((FL_GAME / 'X3FL.exe').exists() and (FL_GAME / 'addon2/04.cat').exists(), 'X3FL tree unavailable')
class ShipProfilesFlInstalled(unittest.TestCase):
    """The Argon Titan's profile on the live C:\\X3FL tree (verification/results/ship-profiles/hull_rules.py)."""

    @classmethod
    def setUpClass(cls):
        import engine_bodies
        import ship_profiles as sp
        assets, _ = engine_bodies.load_assets(FL_GAME, False, 'fl')
        cls.entries, cls.skipped, cls.prov = sp.ship_profiles(assets)

    def test_titan_profile_shape(self):
        titan = next(e for e in self.entries if e['tships_index'] == 11)
        self.assertEqual((titan['scene'], titan['class']), ('ships\\argon\\argon_M2_scene', 'OBJ_SHIP_M2'))
        self.assertEqual(len(titan['slices']), 32)
        self.assertLess(titan['z0'], titan['z1'])
        # the hull part at z 17329 with LOD-0 value 528422 (titan_scene.py): its normalised z +-65536 spans the ship;
        # the thin-end rule cuts the stern's first and the nose mast's last slice (0.43 % and 0.06 % of the widest)
        whole = (17329.0 - 528422, 17329.0 + 528422)
        self.assertEqual((titan['z0'] - titan['trim_lo'], titan['z1'] + titan['trim_hi']), whole)
        self.assertEqual((titan['z0'], titan['z1']), (-477889.237, 500605.816))
        self.assertEqual((titan['parts_excluded'], titan['smoothed']), (14, True))  # 8 small turret, 6 camera dummies
        self.assertTrue(all(s[2] > 0 and s[3] > 0 for s in titan['slices']))
        widest = max(range(32), key=lambda i: titan['slices'][i][2] * titan['slices'][i][3])
        self.assertTrue(8 <= widest < 24, widest)                                    # amidships
        self.assertAlmostEqual(max(s[2] for s in titan['slices']), 151433, delta=1)  # the stored x extent
        for end in (titan['slices'][0], titan['slices'][-1]):  # no needle dome (was 3757 x 2850), no blunt one either
            self.assertGreater(min(end[2], end[3]), 15000, end)
            self.assertLess(max(end[2], end[3]), 50000, end)
        self.assertGreaterEqual(titan['radius'], titan['z1'])
        self.assertEqual(self.prov['tships']['member'], 'addon2/types/TShips.pck')

    def test_skipped_rows_have_reasons(self):
        self.assertEqual(self.prov['profiles'] + self.prov['skipped'], self.prov['rows'])
        self.assertTrue(all(s['reason'] for s in self.skipped))
        self.assertIn('cut1_scene', {s['reason'] for s in self.skipped})


if __name__ == '__main__':
    unittest.main()
