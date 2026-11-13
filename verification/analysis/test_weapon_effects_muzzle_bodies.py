"""The muzzle flash re-shade's body table (src/proxy/muzzle_reshade_core.h bodies[]; docs/architecture/
muzzle-flash-look.md): compiled on the host and checked against the census and the producing script's output
(verification/results/muzzle-reshade/muzzle_bodies_out.txt). No game tree, no Windows."""
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'verification/results/muzzle-reshade/muzzle_bodies_out.txt'
OUT_AP = ROOT / 'verification/results/muzzle-reshade/muzzle_bodies_ap_out.txt'
# The census's classic standard_lighting flash bodies and their atlases (fl_muzzle_flash_census_out.txt); 12009 (PRG)
# draws with effects.fx and stays unlisted.
ATLAS_FL = {445: 384, 446: 383, 444: 376, 450: 384, 447: 383, 314: 384, 10463: 380}
# AP Mayhem's (ap-bullet-batch-site.md section 3; muzzle_bodies.py --edition ap, 445 first as the reference); 12011 is
# MATERIAL6 effects.fx like 12009 and stays unlisted.
ATLAS_AP = {12012: 924, 12013: 925, 12014: 926, 12016: 931, 12017: 932, 12018: 933}
ATLAS = {**ATLAS_FL, **ATLAS_AP}

HARNESS = r'''
#include "muzzle_reshade_core.h"
#include <cstdio>
namespace mz = x3m::weapon_effects::muzzle_reshade;
int main() {
    std::printf("{\"count\": %u, \"bodies\": [", mz::body_count);
    for (unsigned i = 0; i < mz::body_count; ++i) {
        const mz::Body& b = mz::bodies[i];
        // a draw 30 m away along +z, world scale 0.001: the per-draw block must be written
        float rows[36] = {};
        rows[0] = rows[5] = rows[10] = 0.001f;
        rows[11] = 30.f;
        rows[24] = rows[29] = rows[34] = 1.f;
        float block[16];
        mz::constants(b, x3m::weapon_effects::projectile_family::baked, block);
        const bool drawn = mz::draw_constants(b, rows, block);
        std::printf("%s{\"id\": %u, \"index\": %u, \"lit_radius\": %.9g, \"mean\": %.9g, \"overlap\": [%.9g, %.9g], "
                    "\"hue\": [%.9g, %.9g, %.9g], \"n_side\": %.9g, \"n_axis\": %.9g, \"drawn\": %d, \"c192w\": %.9g}",
                    i ? ", " : "", b.id, mz::body_index(b.id), double(b.lit_radius), double(b.mean),
                    double(b.overlap[0]), double(b.overlap[1]), double(b.hue[0]), double(b.hue[1]), double(b.hue[2]),
                    double(mz::overlap_at(b, 0.f)), double(mz::overlap_at(b, 1.f)), int(drawn), double(block[3]));
    }
    std::printf("], \"miss\": [%u, %u, %u]}\n", mz::body_index(12009u), mz::body_index(0u), mz::body_index(0xffffffffu));
    return 0;
}
'''


class MuzzleBodies(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('g++') or shutil.which('c++') or shutil.which('clang++')
        if not compiler:
            raise unittest.SkipTest('a host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-muzzle-bodies-') as tmp:
            src, exe = Path(tmp) / 'h.cpp', Path(tmp) / 'h'
            src.write_text(HARNESS)
            subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                            str(src), '-o', str(exe)], check=True)
            cls.table = json.loads(subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout)

    def test_ids_unique_and_the_census_set(self):
        ids = [b['id'] for b in self.table['bodies']]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(set(ids), set(ATLAS))
        self.assertEqual(ids[0], 445)  # the fixture's and the dump's bodies[0]
        self.assertEqual([b['index'] for b in self.table['bodies']], list(range(len(ids))))
        self.assertEqual(self.table['miss'], [self.table['count']] * 3)  # 12009 (PRG) and others unlisted
        self.assertLess(self.table['count'], 256)  # MotionRoute::muzzle_body is a byte

    def test_constants_in_range(self):
        for b in self.table['bodies']:
            self.assertTrue(1e4 <= b['lit_radius'] <= 1e5, b)
            self.assertTrue(0.01 <= b['mean'] <= 10.0, b)
            self.assertTrue(1.0 <= b["overlap"][0] <= 128.0 and 0.0 <= b['overlap'][1] <= b['overlap'][0], b)
            self.assertLessEqual(b['n_side'], b['n_axis'])
            self.assertTrue(all(0.0 < h < 3.0 for h in b['hue']), b)
            # the hue at luminance 1, so mz_gain and mz_core stay luminances on every body
            self.assertAlmostEqual(0.2126 * b['hue'][0] + 0.7152 * b['hue'][1] + 0.0722 * b['hue'][2], 1.0, delta=0.005)
            self.assertEqual(b['drawn'], 1, b)
            self.assertGreater(b['c192w'], 0.0)

    @staticmethod
    def parse(path):
        text = path.read_text()
        resolved = {int(m.group(1)): int(m.group(2)) for m in
                    re.finditer(r'^=== body (\d+) \(\S+\), atlas tex/true/(\d+) \(\2-[0-9a-f]{8}\.png\)$', text, re.M)}
        rows = {}
        for m in re.finditer(r'^    \{(\d+)u, ([\d.]+)\.f, ([\d.]+)f, \{([\d.]+)f, ([\d.]+)f\}, '
                             r'\{([\d.]+)f, ([\d.]+)f, ([\d.]+)f\}\}, // tex/true/(\d+)$', text, re.M):
            rows[int(m.group(1))] = ([float(m.group(k)) for k in range(2, 9)], int(m.group(9)))
        return resolved, rows

    def test_rows_are_the_producing_scripts_and_atlases_resolved(self):
        resolved, rows = self.parse(OUT)
        self.assertEqual(resolved, ATLAS_FL)
        self.assertEqual(set(rows), set(ATLAS_FL))
        # the AP run measures 445 first, its reference: that row must equal the FL run's
        resolved_ap, rows_ap = self.parse(OUT_AP)
        self.assertEqual(resolved_ap, {445: 384, **ATLAS_AP})
        self.assertEqual(rows_ap.pop(445), rows[445])
        rows.update(rows_ap)
        self.assertEqual(set(rows), set(ATLAS))
        for b in self.table['bodies']:
            want, tex = rows[b['id']]
            self.assertEqual(tex, ATLAS[b['id']])
            got = [b['lit_radius'], b['mean'], *b['overlap'], *b['hue']]
            for g, w in zip(got, want):
                self.assertAlmostEqual(g, w, delta=5e-4 * max(1.0, abs(w)), msg=b['id'])


if __name__ == '__main__':
    unittest.main()
