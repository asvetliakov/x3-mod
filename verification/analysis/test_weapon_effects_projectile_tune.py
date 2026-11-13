"""The dev tuning file's host-testable parts (docs/architecture/projectile-look.md "Dev tuning file"): the parser and
reload rule (src/proxy/weapon_projectile_tune_core.h) over the re-shades' nine keys (src/proxy/weapon_projectile_family.h),
compiled on the host. The removed custom projectile stage's keys (core_px .. hull_pulse_r, 2026-11-13) are unknown keys:
ignored and named for the reader's one row. No game tree, no Windows."""
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include "weapon_projectile_tune_core.h"
#include <cstdio>
#include <cstring>
#include <string>
namespace pt = x3m::weapon_effects::projectile_tune;
namespace pf = x3m::weapon_effects::projectile_family;
int main(int argc, char** argv) {
    std::string text;
    int c;
    while ((c = std::getchar()) != EOF) text.push_back(char(c));
    const std::string mode = argc > 1 ? argv[1] : "parse";
    if (mode == "parse") {
        pf::Tune tune = pf::baked;
        const pt::Result r = pt::parse(text.data(), text.size(), &tune);
        std::printf("{\"ok\": %d, \"seen\": %u, \"clamped\": %u, \"unknown\": %u, \"names\": [", int(r.ok), r.seen,
                    r.clamped, r.unknown);
        for (unsigned u = 0; u < r.unknown && u < pt::max_unknown; ++u) std::printf("%s\"%s\"", u ? ", " : "", r.unknown_name[u]);
        std::printf("], \"values\": [");
        for (unsigned k = 0; k < pf::tune_keys; ++k) std::printf("%s%.6g", k ? ", " : "", double(pf::tune_at(tune, k)));
        std::printf("]}\n");
        return 0;
    }
    if (mode == "clamp") { // clamp_key over one key in, two past the table: the value unchanged out of range
        std::printf("[%.6g, %.6g, %.6g, %.6g]\n", double(pf::clamp_key(9.f, pf::key_rs_gain)),
                    double(pf::clamp_key(-1.f, pf::key_rs_width)), double(pf::clamp_key(9.f, pf::tune_keys)),
                    double(pf::clamp_key(-7.f, 17)));
        return 0;
    }
    // poll: lines "tick exists write_time" -> the action per line
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
'''

DEFAULTS = [0.75, 0.4, 0.5,  # the bullet re-shade (bullet-draw-batch.md section 6; baked from FL 64 A, re-baked from FL 68 A)
            0.2, 0.0, 0.15, 0.1,  # the muzzle flash re-shade (muzzle-flash-look.md; baked from FL 65 A, size as drawn)
            1.0, 1.0]  # the bullet re-shade's geometric size (rs_head, rs_width; 1 = unchanged)
KEYS = ['rs_gain', 'rs_edge', 'rs_core', 'mz_size', 'mz_core', 'mz_gain', 'mz_smooth', 'rs_head', 'rs_width']
# The removed custom stage's keys (the FL run 72 file still carries them).
LEGACY = ['core_px', 'glow_ratio', 'glow_gain', 'hot_share', 'lod_dot_m', 'length_cap_m', 'stops', 'trail_ms',
          'head_gain', 'flash_core_mult', 'flash_len_m', 'flash_life_ms', 'flash_stops', 'hull_pulse_r']


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
        self.assertEqual((r['ok'], r['seen'], r['unknown']), (1, 0, 0))
        self.assertEqual(len(r['values']), len(KEYS))
        for got, want in zip(r['values'], DEFAULTS):
            self.assertAlmostEqual(got, want, places=5)

    def test_every_key_read(self):
        values = [0.6, 0.45, 0.5, 0.75, 1.5, 0.25, 0.3, 0.5, 0.6]
        text = json.dumps(dict(zip(KEYS, values)), indent=2)
        r = self.parse(text)
        self.assertEqual((r['ok'], r['seen'], r['clamped'], r['unknown']), (1, (1 << len(KEYS)) - 1, 0, 0))
        for got, want in zip(r['values'], values):
            self.assertAlmostEqual(got, want, places=4)

    def test_reshade_keys_bounds(self):
        # the bullet re-shade (bullet-draw-batch.md section 6): rs_gain 0..2, rs_edge 0.05..1 (the smoothstep's upper
        # edge stays above zero), rs_core 0..4
        low = self.parse('{"rs_gain": -1, "rs_edge": 0, "rs_core": -0.5}')
        self.assertEqual((low['ok'], low['clamped']), (1, 0x7))
        self.assertEqual([round(v, 6) for v in low['values'][0:3]], [0.0, 0.05, 0.0])
        high = self.parse('{"rs_gain": 3, "rs_edge": 2, "rs_core": 9}')
        self.assertEqual(high['clamped'], 0x7)
        self.assertEqual(high['values'][0:3], [2.0, 1.0, 4.0])
        inside = self.parse('{"rs_gain": 0.3, "rs_edge": 0.7, "rs_core": 1}')
        self.assertEqual((inside['ok'], inside['clamped'], inside['seen']), (1, 0, 0x7))
        self.assertEqual(inside['values'][3:], DEFAULTS[3:])

    def test_muzzle_reshade_keys_bounds(self):
        # the muzzle flash re-shade (muzzle-flash-look.md): mz_size 0.05..1 (the baked 0.1 inside), mz_core 0..4,
        # mz_gain 0..2, mz_smooth 0..1
        low = self.parse('{"mz_size": 0.01, "mz_core": -1, "mz_gain": -0.5, "mz_smooth": -2}')
        self.assertEqual((low['ok'], low['clamped']), (1, 0xf << 3))
        self.assertEqual([round(v, 6) for v in low['values'][3:7]], [0.05, 0.0, 0.0, 0.0])
        high = self.parse('{"mz_size": 3, "mz_core": 9, "mz_gain": 5, "mz_smooth": 1.5}')
        self.assertEqual(high['clamped'], 0xf << 3)
        self.assertEqual(high['values'][3:7], [1.0, 4.0, 2.0, 1.0])
        inside = self.parse('{"mz_size": 0.1, "mz_core": 4, "mz_gain": 2, "mz_smooth": 1}')
        self.assertEqual((inside['ok'], inside['clamped'], inside['seen']), (1, 0, 0xf << 3))
        self.assertEqual([round(v, 6) for v in inside['values'][:3]], DEFAULTS[:3])
        self.assertEqual(inside['values'][7:], DEFAULTS[7:])

    def test_reshade_size_keys_bounds(self):
        # the bullet re-shade's geometric size (projectile-look.md "Geometric size (2026-11-10)"): rs_head and rs_width
        # 0.2..1, default 1 (unchanged)
        low = self.parse('{"rs_head": 0.1, "rs_width": -1}')
        self.assertEqual((low['ok'], low['clamped']), (1, 0x3 << 7))
        self.assertEqual([round(v, 6) for v in low['values'][7:]], [0.2, 0.2])
        high = self.parse('{"rs_head": 1.5, "rs_width": 9}')
        self.assertEqual(high['clamped'], 0x3 << 7)
        self.assertEqual(high['values'][7:], [1.0, 1.0])
        inside = self.parse('{"rs_head": 0.5, "rs_width": 0.6}')
        self.assertEqual((inside['ok'], inside['clamped'], inside['seen']), (1, 0, 0x3 << 7))
        self.assertEqual([round(v, 6) for v in inside['values'][7:]], [0.5, 0.6])
        self.assertEqual([round(v, 6) for v in inside['values'][:7]], DEFAULTS[:7])

    def test_legacy_keys_are_unknown_named_and_ignored(self):
        # the FL run 72 file: the fourteen keys of the removed custom stage beside the kept ones; the legacy keys set
        # nothing, clamp nothing and are named (the reader logs each once), the kept keys apply
        legacy = dict(zip(LEGACY, [1.0, 2.0, 0.3, 0.4, 3000, 60, 2.0, 0, 1.5, 3.0, 5.0, 80, 2.0, 3.0]))
        kept = {'rs_gain': 0.6, 'mz_gain': 0.2}
        r = self.parse(json.dumps({**legacy, **kept}))
        self.assertEqual((r['ok'], r['clamped'], r['unknown']), (1, 0, len(LEGACY)))
        self.assertEqual(r['names'], LEGACY)
        self.assertEqual(r['seen'], (1 << 0) | (1 << 5))
        self.assertAlmostEqual(r['values'][0], 0.6, places=5)
        self.assertAlmostEqual(r['values'][5], 0.2, places=5)

    def test_unknown_keys_of_every_value_kind_ignored(self):
        r = self.parse('{"rs_core": 1, "note": "IRE flight 59", "flag": true, "x": null, "y": -1.5e2, '
                       '"stretch_k": 0.4}')
        self.assertEqual((r['ok'], r['seen'], r['unknown']), (1, 1 << 2, 5))
        self.assertEqual(r['names'], ['note', 'flag', 'x', 'y', 'stretch_k'])
        self.assertAlmostEqual(r['values'][2], 1.0)
        self.assertAlmostEqual(r['values'][0], DEFAULTS[0])

    def test_unknown_names_bounded(self):
        # past max_unknown (16) the count still rises, the names stop; a name past 23 bytes is kept truncated
        text = json.dumps({f'k{i:02d}': i for i in range(20)} | {'a' * 30: 1})
        r = self.parse(text)
        self.assertEqual((r['ok'], r['unknown']), (1, 21))
        self.assertEqual(r['names'], [f'k{i:02d}' for i in range(16)])
        r = self.parse(json.dumps({'a' * 30: 1}))
        self.assertEqual((r['ok'], r['unknown'], r['names']), (1, 1, ['a' * 23]))

    def test_out_of_bounds_clamped_and_named(self):
        r = self.parse('{"rs_gain": 40, "mz_smooth": -9, "rs_edge": 0.01}')
        self.assertEqual(r['ok'], 1)
        self.assertEqual(r['clamped'], (1 << 0) | (1 << 6) | (1 << 1))
        self.assertAlmostEqual(r['values'][0], 2.0)
        self.assertAlmostEqual(r['values'][6], 0.0)
        self.assertAlmostEqual(r['values'][1], 0.05, places=6)

    def test_malformed_refused_whole(self):
        for text in ('', 'rs_gain: 2', '{"rs_gain": "big"}', '{"rs_gain": 2', '{"rs_gain": 2} x', '{"a": {"b": 1}}',
                     '{"rs_gain": 2, ' + ' ' * 1100 + '}', '{"rs_gain": 2,}', '{"rs_gain": 2 "rs_core": 1}',
                     '{"rs_gain": +2}', '{"note": big}', '{,"rs_gain": 2}', '{"rs_gain": .5}', '{"rs_gain": 2,,"rs_core": 1}',
                     '{"x": [1]}'):
            self.assertEqual(self.parse(text)['ok'], 0, text[:40])
        # a legacy key is unknown now, so its string value no longer refuses the file
        self.assertEqual(self.parse('{"core_px": "legacy"}')['ok'], 1)

    def test_clamp_key_bounds_checks_its_index(self):
        # an index past the nine keys (a stale hard-coded index of the removed 23-key table) leaves the value unchanged
        self.assertEqual(self.run_mode('clamp', ''), [2.0, 0.2, 9.0, -7.0])

    def test_reload_rule(self):
        # (tick, exists, write_time): first query; within the second; a second later unchanged; changed; removed;
        # removed again; back; never present from the start is silent.
        lines = '0 1 100\n500 1 200\n1000 1 100\n2100 1 300\n3200 0 0\n4300 0 0\n5400 1 300\n'
        self.assertEqual(self.run_mode('poll', lines),
                         ['reload', 'skip', 'none', 'reload', 'absent', 'none', 'reload'])
        self.assertEqual(self.run_mode('poll', '0 0 0\n1000 0 0\n'), ['none', 'none'])


if __name__ == '__main__':
    unittest.main()
