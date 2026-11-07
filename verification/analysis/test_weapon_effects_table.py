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
import weapon_effects_table as wt  # noqa: E402

OVERRIDES = ROOT / 'tools/effects/weapon_overrides.json'

READER = r'''
#include "weapon_effects_table.h"
#include <cstdio>
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
    std::printf("ok=%d rows=%u missile_rows=%u entries=%u textures=%u overrides=%u schema=%u\n", int(ok), table.rows,
                table.missile_rows, st.entries, st.textures, st.overrides, st.schema);
    if (ok && argc > 1) {
        const unsigned row = unsigned(std::atoi(argv[1]));
        std::printf("row=%u bits=%u shape=%s texture=%06lx override=%06lx calibre_q=%u hull_class=%s\n", row,
                    table.bits[row], core::shape_name(table.shape[row]), (unsigned long)(table.texture_rgb[row] & 0xffffff),
                    (unsigned long)(table.override_rgb[row] & 0xffffff), table.calibre_q[row],
                    (table.bits[row] & core::row_hull_class) ? core::hull_class_name(table.hull_class[row]) : "-");
        std::printf("mount_tier=%u range_m=%u mount_keys=%u\n", table.mount_tier[row], table.range_m[row], st.mount_keys);
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
             'calibre': None, 'mount_tier': 'corvette', 'range_m': None}]},
        'missiles': {'rows': 36}}}


class WeaponEffectsTableGenerator(unittest.TestCase):
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
        # docs/architecture/bolt-muzzle-look.md section 1: the smallest mounting class decides; unmounted is a fighter
        self.assertEqual(wt.mount_tier_of({'M3': 4, 'M6': 2, 'M2': 1}), 'fighter')
        self.assertEqual(wt.mount_tier_of({'M6': 2, 'M2': 1}), 'corvette')
        self.assertEqual(wt.mount_tier_of({'M7': 1, 'TL': 2}), 'capital')
        self.assertEqual(wt.mount_tier_of({'TS': 1}), 'corvette')
        self.assertEqual(wt.mount_tier_of({}), 'fighter')
        self.assertEqual(wt.range_of({'speed': 500 * 1000, 'lifetime_ms': 2000}), 2000.0)   # 1000 m/s x 2 s
        self.assertIsNone(wt.range_of({'speed': None, 'lifetime_ms': 2000}))
        self.assertEqual(wt.SCHEMA, 2)
        # an unmounted row is marked and listed (the generator logs it: unknown 7's fallback)
        w = {'index': 3, 'weapon_id': 'SS_BULLET_X', 'body_key': None, 'speed': None, 'lifetime_ms': None}
        unmounted, mounted = wt.row_entry(w, None, None, {}), wt.row_entry(dict(w, index=4), None, None, {}, {'M2': 1})
        self.assertEqual((unmounted['mount_tier'], unmounted['mount_fallback']), ('fighter', True))
        self.assertEqual((mounted['mount_tier'], mounted['mount_fallback']), ('capital', False))
        table = {'tables': {'bullets': {'entries': [unmounted, mounted]}}}
        self.assertEqual([e['row'] for e in wt.mount_fallback_rows(table)], [3])

    def test_mount_tiers_match_the_catalogue(self):
        # the generator's rule over the tracked catalogue's used-by (both AP views): the size_derivation counts 30/19/12
        tiers = {}
        for view in ('stock_ap', 'mayhem3'):
            data = json.loads((ROOT / f'tools/effects/weapon_catalog/{view}.json').read_text())
            for e in data['weapons']:
                if e['style'].split('+')[0] == 'beam' or not e.get('lasers'):
                    continue
                u = set()
                for laser in e['lasers']:
                    u |= set((laser.get('used_by') or {}).keys())
                t = wt.mount_tier_of(u)
                tiers[t] = tiers.get(t, 0) + 1
        self.assertEqual((tiers['fighter'], tiers['corvette'], tiers['capital']), (30, 19, 12))

    def test_shipped_overrides_file_schema(self):
        data = json.loads(OVERRIDES.read_text())
        self.assertEqual(data['schema'], 2)
        # docs/architecture/weapon-look.md 3.1 (the variants) and hull-impact-classes.md section 2 (the class pins): the
        # by_id hull keys are exactly the census script's pin list, the tug's calibre beside it
        sys.path.insert(0, str(ROOT / 'verification/results/weapon-effects'))
        import hull_class_census as census  # noqa: E402
        self.assertEqual({k: v['hull'] for k, v in data['by_id'].items()}, census.PINS)
        self.assertEqual(data['by_id']['SS_BULLET_TUG'], {'hull': 'tug', 'calibre': 0.1})
        for entry in data['by_id'].values():
            self.assertIn(entry.get('hull'), wt.HULL_VARIANTS)
            self.assertNotEqual(entry.get('hull'), 'beam')
        self.assertEqual(set(data['by_table']),
                         {'be762076cdc38b03', '7b4ab0a6cea2cb07', 'f0cb79fd63c46318'})  # stock AP, Mayhem 3, FL
        self.assertIsInstance(wt.load_overrides(OVERRIDES), dict)


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
        self.assertEqual((f['ok'], f['rows'], f['missile_rows'], f['entries']), ('1', '45', '36', '10'))
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
        self.assertEqual(f['overrides'], '4')            # rows 1, 3, 4 (its calibre the same row) and 6; none for 5, 7
        # step 4: mount_tier (0 fighter, 1 corvette, 2 capital; absent = fighter) and range_m (metres; null = 0)
        _, f8 = self.read(sample_table(), row=8)
        _, f9 = self.read(sample_table(), row=9)
        self.assertEqual((f['mount_tier'], f['range_m'], f['mount_keys']), ('0', '0', '2'))
        self.assertEqual((f8['mount_tier'], f8['range_m']), ('2', '4957'))
        self.assertEqual((f9['mount_tier'], f9['range_m']), ('1', '0'))
        schema2 = sample_table()
        schema2['schema'] = 2
        self.assertEqual(self.read(schema2, row=8)[1]['mount_tier'], '2')

    def test_reader_rejects_a_foreign_or_malformed_table(self):
        code, f = self.read({'schema': 3, 'tables': {}})
        self.assertEqual(code, 1)   # schema must be 1 or 2
        code, f = self.read({'schema': 1, 'tables': {'bullets': {'rows': 45, 'entries': 'not a list'}}})
        self.assertEqual(code, 1)

    def read_text(self, text):
        out = subprocess.run([str(self.exe)], input=text, capture_output=True, text=True)
        return out.returncode, dict(kv.split('=') for kv in out.stdout.split())

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


if __name__ == '__main__':
    unittest.main()
