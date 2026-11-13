"""The hull hit re-shade's core (src/proxy/hull_reshade_core.h; docs/architecture/hull-hit-look.md): the body table is
the one verification/results/fl-hull-hit-draw/hull_hit_bodies.py emitted, the match takes each listed body on its pair
and blend and leaves the bullet map's, the muzzle list's and the death bodies to the other routes (FL; nothing on AP),
and the CPU model's terms (fade, cooling, cell envelope, hot core) behave as the design states. The native fixture
checks the compiled programs against the same model (weapon_hull_reshade_cases.h)."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'src/proxy/hull_reshade_core.h'
TABLE = ROOT / 'verification/results/fl-hull-hit-draw/hull_hit_bodies_out.txt'

HARNESS = r'''
#include "hull_reshade_core.h"
#include <cstdio>
namespace hh = x3m::weapon_effects::hull_reshade;
namespace rs = x3m::weapon_effects::reshade;
int main() {
    const auto tune = x3m::weapon_effects::projectile_family::baked;
    std::uint32_t node = 0;
    const auto body_id = [&](std::uint32_t* id) { *id = node; return true; };
    const auto mapped = [](std::uint32_t id) { return id == 1207u; };
    const std::uint64_t sl = rs::classic_pixel_shader_pairs[rs::pair_fl][0];
    const auto take = [&](std::uint64_t vs, unsigned dst, std::uint32_t id, unsigned e) {
        node = id;
        const hh::Match m = hh::match(vs, true, 2u, dst, 1u, sl, true, true, body_id, mapped, e);
        return m.body < hh::table_count ? int(hh::bodies[m.body].id) : (m.candidate ? -1 : -2);
    };
    std::printf("{\"count\": %u, \"ap_count\": %u, ", hh::body_count(hh::edition_fl), hh::body_count(hh::edition_ap));
    std::printf("\"sparks_classic\": %d, \"sparks_effects\": %d, \"sparks_screen\": %d, \"sparks_ap\": %d, ",
                take(hh::classic_vertex_shader, 2u, 10663u, hh::edition_fl),
                take(hh::effects_vertex_shader, 2u, 10663u, hh::edition_fl),
                take(hh::classic_vertex_shader, 4u, 10663u, hh::edition_fl),
                take(hh::classic_vertex_shader, 2u, 10663u, hh::edition_ap));
    std::printf("\"hept_screen\": %d, \"blue_effects\": %d, \"muzzle\": %d, \"bullet\": %d, \"death\": %d, ",
                take(hh::effects_vertex_shader, 4u, 10659u, hh::edition_fl),
                take(hh::effects_vertex_shader, 2u, 12000u, hh::edition_fl),
                take(hh::classic_vertex_shader, 2u, 445u, hh::edition_fl),
                take(hh::classic_vertex_shader, 2u, 1207u, hh::edition_fl),
                take(hh::classic_vertex_shader, 2u, 10664u, hh::edition_fl));
    std::printf("\"other_vs\": %d, ", take(0x1234ull, 2u, 10663u, hh::edition_fl));
    float c[8];
    hh::constants(hh::bodies[hh::body_index(10663u, hh::edition_fl)], tune, c);
    std::printf("\"block\": [%g, %g, %g, %g, %g, %g, %g, %g], ", c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7]);
    const float hot[4] = {0.94f, 0.92f, 0.78f, 1.f}, ember[4] = {0.43f, 0.16f, 0.02f, 1.f};
    float a[4], b[4], e[4], z[4], h[4];
    hh::model(c, ember, 0.125f, 0.125f, 1.f, true, 0.f, 0.f, a);   // young ember, cell centre
    hh::model(c, ember, 0.125f, 0.125f, 0.1f, true, 0.f, 0.f, b);  // late ember
    hh::model(c, hot, 0.249f, 0.125f, 1.f, true, 0.f, 0.f, e);     // the cell's edge
    hh::model(c, hot, 0.125f, 0.125f, 1.f, true, 0.f, 0.f, h);     // hot texel, centre
    c[5] = 0.f;
    hh::model(c, hot, 0.249f, 0.125f, 1.f, true, 0.f, 0.f, z);     // no envelope
    std::printf("\"young\": [%g, %g, %g], \"late\": [%g, %g, %g], \"edge\": %g, \"plain_edge\": %g, \"hot\": [%g, %g, %g]}\n",
                a[0], a[1], a[2], b[0], b[1], b[2], e[0], z[0], h[0], h[1], h[2]);
    return 0;
}
'''


def header_rows():
    text = HEADER.read_text()
    block = text.split('constexpr Body bodies[] = {', 1)[1].split('};', 1)[0]
    return [line.rstrip() for line in block.splitlines() if line.strip().startswith('{')]


class HullReshadeTable(unittest.TestCase):
    def test_header_rows_are_the_generator_output(self):
        want = [line.rstrip() for line in TABLE.read_text().splitlines() if line.startswith('    {')]
        self.assertEqual(len(want), 24)
        self.assertEqual(header_rows(), want)

    def test_scope(self):
        ids = [int(re.match(r'\s*\{(\d+)u', r).group(1)) for r in header_rows()]
        self.assertEqual(len(set(ids)), len(ids))
        for death in (10656, 10664, 10670):  # the explosions family's
            self.assertNotIn(death, ids)
        for body in (587, 10663, 10658, 10661, 564, 12000):
            self.assertIn(body, ids)
        self.assertIn('excluded effects\\weapons\\bullet_ElectroSpike', TABLE.read_text())


class HullReshadeCore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('g++') or shutil.which('c++') or shutil.which('clang++')
        if not compiler:
            raise unittest.SkipTest('a host C++ compiler is required')
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-hull-reshade-')
        src = Path(cls.tmp.name) / 'h.cpp'
        src.write_text(HARNESS)
        exe = Path(cls.tmp.name) / 'h'
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT / 'src/proxy'),
                        str(src), '-o', str(exe)], check=True)
        cls.r = json.loads(subprocess.run([str(exe)], capture_output=True, text=True, check=True).stdout)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_match(self):
        r = self.r
        self.assertEqual((r['count'], r['ap_count']), (24, 0))
        self.assertEqual((r['sparks_classic'], r['hept_screen'], r['blue_effects']), (10663, 10659, 12000))
        # the wrong pair, the wrong blend, AP: candidates refused; a muzzle, a bullet-map and a death body: refused;
        # another VS: not even a candidate
        self.assertEqual((r['sparks_effects'], r['sparks_screen'], r['muzzle'], r['bullet'], r['death']),
                         (-1, -1, -1, -1, -1))
        self.assertEqual((r['sparks_ap'], r['other_vs']), (-2, -2))

    def test_block(self):
        # hh_gain, hh_edge, hh_core at the first step's values, the sparks' normaliser, hh_cool, 4 cells
        self.assertEqual([round(v, 4) for v in self.r['block']], [1.0, 0.4, 1.0, 3.0376, 0.3, 4.0, 0.0, 0.0])

    def test_model_terms(self):
        r = self.r
        young, late = r['young'], r['late']
        # fade: f 0.1 keeps at most a tenth; cooling: the late ember is redder (green and blue fall more than red)
        self.assertLessEqual(late[0], 0.1 * young[0] + 1e-6)
        self.assertLess(late[1] / late[0], young[1] / young[0])
        self.assertLess(late[2] / late[0] if late[0] else 0.0, young[2] / young[0] + 1e-9)
        # the cell envelope darkens the cell's edge; without it the edge keeps the centre's value
        self.assertLess(r['edge'], 0.05 * r['plain_edge'])
        # a hot texel at the defaults reaches scene white in red and green
        self.assertGreaterEqual(min(r['hot'][0], r['hot'][1]), 1.0)


if __name__ == '__main__':
    unittest.main()
