"""The projectile stage's host-testable parts (docs/architecture/projectile-look.md, "Implementation (2026-11-08)"):
the dev tuning file's parser and reload rule (src/proxy/weapon_projectile_tune_core.h) and the enabled-row resolution
of the families (src/proxy/weapon_projectile_family.h resolve_rows over the table reader weapon_effects_table.h),
compiled on the host. No game tree, no Windows."""
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include "weapon_projectile_tune_core.h"
#include "weapon_effects_table.h"
#include <cstdio>
#include <cstring>
#include <string>
namespace pt = x3m::weapon_effects::projectile_tune;
namespace pf = x3m::weapon_effects::projectile_family;
namespace t = x3m::weapon_effects::table;
namespace core = x3m::weapon_effects::core;
int main(int argc, char** argv) {
    std::string text;
    int c;
    while ((c = std::getchar()) != EOF) text.push_back(char(c));
    const std::string mode = argc > 1 ? argv[1] : "parse";
    if (mode == "parse") {
        pf::Tune tune = pf::families[pf::fighter_tracer].tune;
        const pt::Result r = pt::parse(text.data(), text.size(), &tune);
        std::printf("{\"ok\": %d, \"seen\": %u, \"clamped\": %u, \"values\": [", int(r.ok), r.seen, r.clamped);
        for (unsigned k = 0; k < pf::tune_keys; ++k) std::printf("%s%.6g", k ? ", " : "", double(pf::tune_at(tune, k)));
        std::printf("]}\n");
        return 0;
    }
    if (mode == "poll") { // lines: tick exists write_time -> the action per line
        pt::Poll p;
        unsigned tick = 0, exists = 0;
        unsigned long long written = 0;
        const char* s = text.c_str();
        int used = 0;
        std::printf("[");
        bool first = true;
        while (std::sscanf(s, "%u %u %llu%n", &tick, &exists, &written, &used) == 3) {
            s += used;
            const bool due = pt::due(p, tick);
            const char* a = "skip";
            if (due) {
                const pt::Action act = pt::decide(p, tick, exists != 0, written);
                a = act == pt::Action::reload ? "reload" : act == pt::Action::absent ? "absent" : "none";
            }
            std::printf("%s\"%s\"", first ? "" : ", ", a);
            first = false;
        }
        std::printf("]\n");
        return 0;
    }
    // rows: the table JSON on stdin -> the enabled rows and their families
    static core::Table table;
    t::Stats st;
    if (!t::parse(text.data(), text.size(), &table, &st)) {
        std::printf("{\"parsed\": 0}\n");
        return 0;
    }
    const pf::Resolution r = pf::resolve_rows(table);
    std::printf("{\"parsed\": 1, \"refused\": %u, \"shared\": %u, \"rows\": [", r.refused, r.shared);
    for (unsigned i = 0; i < r.count; ++i) std::printf("%s[%u, %u]", i ? ", " : "", unsigned(r.row[i]), unsigned(r.family[i]));
    std::printf("]}\n");
    return 0;
}
'''

DEFAULTS = [0.5, 2.0, 0.3, 0.4, 3000.0, 60.0, 2.0, 0.0, 1.5]  # weapon_projectile_family.h, fighter_tracer (FL 61 A)
KEYS = ['core_px', 'glow_ratio', 'glow_gain', 'hot_share', 'lod_dot_m', 'length_cap_m', 'stops', 'trail_ms', 'head_gain']


class ProjectileTune(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('g++') or shutil.which('c++') or shutil.which('clang++')
        if not compiler:
            raise unittest.SkipTest('a host C++ compiler is required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-projectile-tune-')
        src = Path(cls.tmp.name) / 'h.cpp'
        src.write_text(HARNESS)
        cls.exe = Path(cls.tmp.name) / 'h'
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                        str(src), '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_mode(self, mode, text):
        out = subprocess.run([str(self.exe), mode], input=text, capture_output=True, text=True, check=True)
        return json.loads(out.stdout)

    def parse(self, text):
        return self.run_mode('parse', text)

    def test_defaults_are_the_baked_values(self):
        r = self.parse('{}')
        self.assertEqual(r['ok'], 1)
        self.assertEqual(r['seen'], 0)
        for got, want in zip(r['values'], DEFAULTS):
            self.assertAlmostEqual(got, want, places=5)

    def test_every_key_read(self):
        values = [2.5, 4.0, 0.3, 0.5, 4000.0, 80.0, 2.5, 60.0, 2.0]
        text = json.dumps(dict(zip(KEYS, values)), indent=2)
        r = self.parse(text)
        self.assertEqual((r['ok'], r['seen'], r['clamped']), (1, 511, 0))
        for got, want in zip(r['values'], values):
            self.assertAlmostEqual(got, want, places=4)

    def test_missing_key_takes_the_default_and_unknown_keys_are_ignored(self):
        r = self.parse('{"core_px": 3, "note": "IRE flight 59", "flag": true, "x": null, "y": -1.5e2}')
        self.assertEqual((r['ok'], r['seen']), (1, 1))
        self.assertAlmostEqual(r['values'][0], 3.0)
        self.assertAlmostEqual(r['values'][3], DEFAULTS[3])

    def test_former_stretch_k_is_an_unknown_key(self):
        # FL 59 A: the streak is the bridged travel, no multiplier; a file still carrying stretch_k (the user's 0.4 and
        # 0) parses, sets nothing and clamps nothing (the reader logs no row for it)
        for text in ('{"stretch_k": 0.4, "core_px": 1, "glow_ratio": 2}', '{"stretch_k": 0}'):
            r = self.parse(text)
            self.assertEqual((r['ok'], r['clamped']), (1, 0), text)
            self.assertEqual(len(r['values']), 9)
        r = self.parse('{"stretch_k": 0.4, "core_px": 1, "glow_ratio": 2}')
        self.assertEqual(r['seen'], (1 << 0) | (1 << 1))
        self.assertEqual(r['values'][2:], DEFAULTS[2:])

    def test_comet_keys_bounds_and_flat_streak(self):
        # FL 60 A: trail_ms 0..200 (0 the flat streak of FL run 60), head_gain 1..4
        r = self.parse('{"trail_ms": 0, "head_gain": 1}')
        self.assertEqual((r['ok'], r['seen'], r['clamped']), (1, (1 << 7) | (1 << 8), 0))
        self.assertEqual(r['values'][7:], [0.0, 1.0])
        r = self.parse('{"trail_ms": 500, "head_gain": 0.5}')
        self.assertEqual(r['clamped'], (1 << 7) | (1 << 8))
        self.assertEqual(r['values'][7:], [200.0, 1.0])
        r = self.parse('{"trail_ms": -3, "head_gain": 9}')
        self.assertEqual(r['values'][7:], [0.0, 4.0])

    def test_out_of_bounds_clamped_and_named(self):
        r = self.parse('{"core_px": 40, "stops": -9, "glow_ratio": 0.2}')
        self.assertEqual(r['ok'], 1)
        self.assertEqual(r['clamped'], (1 << 0) | (1 << 6) | (1 << 1))
        self.assertAlmostEqual(r['values'][0], 8.0)
        self.assertAlmostEqual(r['values'][6], -2.0)
        self.assertAlmostEqual(r['values'][1], 1.0)

    def test_malformed_refused_whole(self):
        for text in ('', 'core_px: 2', '{"core_px": "big"}', '{"core_px": 2', '{"core_px": 2} x', '{"a": {"b": 1}}',
                     '{"core_px": 2, ' + ' ' * 1100 + '}', '{"core_px": 2,}', '{"core_px": 2 "stops": 1}',
                     '{"core_px": +2}', '{"note": big}', '{,"core_px": 2}', '{"core_px": .5}', '{"core_px": 2,,"stops": 1}',
                     '{"x": [1]}'):
            self.assertEqual(self.parse(text)['ok'], 0, text[:40])

    def test_reload_rule(self):
        # (tick, exists, write_time): first query; within the second; a second later unchanged; changed; removed;
        # removed again; back; never present from the start is silent.
        lines = '0 1 100\n500 1 200\n1000 1 100\n2100 1 300\n3200 0 0\n4300 0 0\n5400 1 300\n'
        self.assertEqual(self.run_mode('poll', lines),
                         ['reload', 'skip', 'none', 'reload', 'absent', 'none', 'reload'])
        self.assertEqual(self.run_mode('poll', '0 0 0\n1000 0 0\n'), ['none', 'none'])

    def test_enabled_rows_family_and_shared_body(self):
        def entry(row, ident, body, z, tier='fighter'):
            return {'row': row, 'id': ident, 'body': body, 'z_over_xy': z, 'texture_rgb': [121, 82, 39],
                    'style': None, 'tint': None, 'calibre': None, 'mount_tier': tier, 'range_m': 1000}
        ls = 'objects/effects/weapons/bullet_ls_alpha'
        table = {'schema': 2, 'tables': {'bullets': {'rows': 45, 'entries': [
            entry(0, 'SS_BULLET_IRE', ls, 18.5), entry(1, 'SS_BULLET_PAC', 'objects/effects/weapons/bullet_pr_alpha', 9.0),
            entry(37, 'SS_BULLET_FLAK_CLUSTER', ls, 18.5), entry(38, 'SS_BULLET_FLAK_CLUSTER2', ls, 18.5),
            entry(39, 'SS_BULLET_FLAK_ALPHA_FRAG', ls, 18.5)]}}}
        r = self.run_mode('rows', json.dumps(table))
        self.assertEqual(r['parsed'], 1)
        self.assertEqual(r['rows'], [[0, 0], [37, 0], [38, 0], [39, 0]])
        self.assertEqual((r['refused'], r['shared']), (0, 3))
        # the enabled id classified into another family (a slug) is refused, nothing drawn
        table['tables']['bullets']['entries'][0]['z_over_xy'] = 2.0
        r = self.run_mode('rows', json.dumps(table))
        self.assertEqual((r['rows'], r['refused']), ([], 1))
        # a corvette mount is another family too
        table['tables']['bullets']['entries'][0]['z_over_xy'] = 18.5
        table['tables']['bullets']['entries'][0]['mount_tier'] = 'corvette'
        self.assertEqual(self.run_mode('rows', json.dumps(table))['rows'], [])


if __name__ == '__main__':
    unittest.main()
