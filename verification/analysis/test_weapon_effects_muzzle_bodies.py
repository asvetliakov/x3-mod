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
# The census's classic standard_lighting flash bodies and their atlases (fl_muzzle_flash_census_out.txt).
ATLAS_FL = {445: 384, 446: 383, 444: 376, 450: 384, 447: 383, 314: 384, 10463: 380}
# The effects-pair body (12009, PRG: MATERIAL6 effects.fx), its diffuse texture and the DESTBLEND its material sets
# (muzzle_bodies.py effect_material: ONE / INVSRCCOLOR).
EFFECTS_FL = {12009: ('fx_bullets2_diff', 4)}
# AP Mayhem's (ap-bullet-batch-site.md section 3; muzzle_bodies.py --edition ap, 445 first as the reference); 12011 is
# MATERIAL6 effects.fx like 12009 and is not measured: unlisted.
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
        // the effects block from the same draw: the effects VS's rows c4..c9 are the same world and view-inverse rows
        float fx_rows[24], fx[mz::effects_floats];
        for (unsigned k = 0; k < 12; ++k) fx_rows[k] = rows[k], fx_rows[12 + k] = rows[24 + k];
        mz::effects_constants(b, x3m::weapon_effects::projectile_family::baked, fx);
        const bool fx_drawn = mz::effects_draw_constants(b, fx_rows, fx);
        // the effects-pair match on this id: under its own DESTBLEND and the other, unmapped and mapped
        const auto id_of = [&](std::uint32_t* out) { *out = b.id; return true; };
        bool candidate = false;
        const unsigned own = mz::effects_match(mz::effects_vertex_shader, true, true, 2u, b.dest_blend, 1u, id_of,
                                               [](std::uint32_t) { return false; }, &candidate);
        const unsigned other = mz::effects_match(mz::effects_vertex_shader, true, true, 2u, b.dest_blend == 4u ? 2u : 4u,
                                                 1u, id_of, [](std::uint32_t) { return false; }, &candidate);
        const unsigned mapped = mz::effects_match(mz::effects_vertex_shader, true, true, 2u, b.dest_blend, 1u, id_of,
                                                  [](std::uint32_t) { return true; }, &candidate);
        std::printf("%s{\"id\": %u, \"index\": %u, \"lit_radius\": %.9g, \"mean\": %.9g, \"overlap\": [%.9g, %.9g], "
                    "\"hue\": [%.9g, %.9g, %.9g], \"n_side\": %.9g, \"n_axis\": %.9g, \"drawn\": %d, \"c192w\": %.9g, "
                    "\"pair\": %u, \"dest_blend\": %u, \"fx_drawn\": %d, \"c29w\": %.9g, \"c30x\": %.9g, "
                    "\"fx_match\": [%u, %u, %u]}",
                    i ? ", " : "", b.id, mz::body_index(b.id), double(b.lit_radius), double(b.mean),
                    double(b.overlap[0]), double(b.overlap[1]), double(b.hue[0]), double(b.hue[1]), double(b.hue[2]),
                    double(mz::overlap_at(b, 0.f)), double(mz::overlap_at(b, 1.f)), int(drawn), double(block[3]),
                    unsigned(b.pair), unsigned(b.dest_blend), int(fx_drawn), double(fx[3]), double(fx[4]), own, other,
                    mapped);
    }
    std::printf("], \"miss\": [%u, %u, %u], \"dest_listed\": [%d, %d]}\n", mz::body_index(12011u), mz::body_index(0u),
                mz::body_index(0xffffffffu), int(mz::effects_dest_listed(4u)), int(mz::effects_dest_listed(2u)));
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
        self.assertEqual(set(ids), set(ATLAS) | set(EFFECTS_FL))
        self.assertEqual(ids[0], 445)  # the fixture's and the dump's bodies[0]
        self.assertEqual([b['index'] for b in self.table['bodies']], list(range(len(ids))))
        self.assertEqual(self.table['miss'], [self.table['count']] * 3)  # 12011 (AP Mayhem's PRG) and others unlisted
        self.assertLess(self.table['count'], 256)  # MotionRoute::muzzle_body is a byte

    def test_pairs_and_the_effects_match(self):
        count = self.table['count']
        self.assertEqual(self.table['dest_listed'], [1, 0])  # the effects pair admits INVSRCCOLOR only (12009's)
        for b in self.table['bodies']:
            if b['id'] in EFFECTS_FL:
                # the effects pair: its DESTBLEND, its block written, the match takes it on its blend only, never mapped
                self.assertEqual((b['pair'], b['dest_blend']), (1, EFFECTS_FL[b['id']][1]))
                self.assertEqual(b['fx_drawn'], 1)
                self.assertAlmostEqual(b['c29w'], 1 / (b['lit_radius'] * 0.2), delta=1e-9)  # 1 / (radius x mz_size)
                self.assertAlmostEqual(b['c30x'] * b['n_axis'], 0.15, delta=1e-5)  # mz_gain / N (the draw: axis-on)
                self.assertEqual(b['fx_match'], [b['index'], count, count])
            else:
                # a classic row: ONE / ONE, never the effects pair's
                self.assertEqual((b['pair'], b['dest_blend']), (0, 2))
                self.assertEqual(b['fx_match'], [count, count, count])
        # the effects rows after the FL classic seven (most frequent first) and before AP Mayhem's
        ids = [b['id'] for b in self.table['bodies']]
        self.assertEqual(ids.index(12009), len(ATLAS_FL))

    def test_constants_in_range(self):
        for b in self.table['bodies']:
            self.assertTrue(1e4 <= b['lit_radius'] <= 1e5, b)
            self.assertTrue(0.01 <= b['mean'] <= 10.0, b)
            self.assertTrue(1.0 <= b["overlap"][0] <= 128.0 and 0.0 <= b['overlap'][1] <= b['overlap'][0], b)
            self.assertLessEqual(b['n_side'], b['n_axis'])
            self.assertTrue(all(0.0 < h < 4.0 for h in b['hue']), b)  # 12009's red: 3.18
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

    @staticmethod
    def parse_effects(path):
        """The effects-pair bodies: the texture each resolved, the blend of the material as loaded, the printed rows."""
        text = path.read_text()
        resolved = {int(m.group(1)): m.group(2) for m in
                    re.finditer(r'^=== body (\d+) \(\S+\), effect texture (\w+) \(\2-[0-9a-f]{8}\.png\)$', text, re.M)}
        blends = {int(m.group(1)): (int(m.group(2)), int(m.group(3)), int(m.group(4))) for m in re.finditer(
            r'^=== body (\d+) .*\n  material as loaded: \S+ effects\.fx; g_AlphaBlendEnable 1 g_BlendOp (\d+) '
            r'g_SrcBlend (\d+) g_DestBlend (\d+) ', text, re.M)}
        rows = {}
        for m in re.finditer(r'^    \{(\d+)u, ([\d.]+)\.f, ([\d.]+)f, \{([\d.]+)f, ([\d.]+)f\}, '
                             r'\{([\d.]+)f, ([\d.]+)f, ([\d.]+)f\}, pair_effects, (\d+)u\}, // (\w+)$', text, re.M):
            rows[int(m.group(1))] = ([float(m.group(k)) for k in range(2, 9)], (m.group(10), int(m.group(9))))
        return resolved, blends, rows

    def test_effects_rows_are_the_producing_scripts(self):
        resolved, blends, rows = self.parse_effects(OUT)
        self.assertEqual(resolved, {k: v[0] for k, v in EFFECTS_FL.items()})
        self.assertEqual(blends, {k: (1, 2, v[1]) for k, v in EFFECTS_FL.items()})  # ADD, ONE, the row's DESTBLEND
        self.assertEqual({k: v[1] for k, v in rows.items()}, EFFECTS_FL)
        for b in self.table['bodies']:
            if b['id'] not in EFFECTS_FL:
                continue
            want, _ = rows[b['id']]
            got = [b['lit_radius'], b['mean'], *b['overlap'], *b['hue']]
            for g, w in zip(got, want):
                self.assertAlmostEqual(g, w, delta=5e-4 * max(1.0, abs(w)), msg=b['id'])

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
            if b['id'] in EFFECTS_FL:
                continue  # test_effects_rows_are_the_producing_scripts
            want, tex = rows[b['id']]
            self.assertEqual(tex, ATLAS[b['id']])
            got = [b['lit_radius'], b['mean'], *b['overlap'], *b['hue']]
            for g, w in zip(got, want):
                self.assertAlmostEqual(g, w, delta=5e-4 * max(1.0, abs(w)), msg=b['id'])


if __name__ == '__main__':
    unittest.main()
