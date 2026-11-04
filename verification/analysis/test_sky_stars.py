"""Sky stars, Decision A (docs/architecture/sector-background-modern.md, "Implementation A"): the runtime pass's pure part
src/proxy/sky_stars_core.h and the mark table of src/proxy/lod_texture_mark.h, compiled natively with the installed
exposure meter (src/renderer/exposure.cpp) through verification/probe/sky_stars_check.cpp: the payload-2 mark parse and
seed, N = clamp(K x 48 x density, 512, 16384), the settings' ranges, the static vertex buffer (count, 32-byte stride, the
2.5x-per-step power law, centred peak <= peak_cap 2.0, the size law monotonic in brightness, the first law reproduced at
min_size 1 / size 0 / gain 1, the twinkle lanes on about one star in four and the c251 wall-clock constant,
determinism), the kernel's energy at sub-pixel offsets, and the meter replay on star-only frames (invariant 5). Also the
baker's constants against the runtime's (tools/analysis/sky_textures.py). No D3D, no game bytes.
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
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
DRIVER = ROOT / 'verification/probe/sky_stars_check.cpp'


def run_check():
    compiler = shutil.which('g++') or shutil.which('clang++') or shutil.which('c++')
    if not compiler:
        raise RuntimeError('host C++ compiler required')
    with tempfile.TemporaryDirectory(prefix='x3-sky-stars-') as folder:
        exe = Path(folder) / 'check'
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(DRIVER),
                        str(ROOT / 'src/renderer/exposure.cpp'), '-o', str(exe)], check=True, capture_output=True, text=True)
        run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=300)
    return run.returncode, json.loads(run.stdout)


class SkyStarsCoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rc, cls.report = run_check()

    def test_all_checks_pass(self):
        self.assertEqual((self.rc, self.report['ok'], self.report['failed']), (0, True, ''), self.report)
        self.assertGreaterEqual(self.report['checks'], 49)

    def test_star_count(self):
        self.assertEqual(self.report['star_count'], {'k0': 512, 'k65': 3120, 'k143': 6864, 'k255': 12240,
                                                     'k65_d0.25': 780, 'k65_d4': 12480, 'k65_default': 6240,
                                                     'k255_default': 16384})

    def test_builder(self):
        b = self.report['build']
        self.assertEqual((b['stars'], b['bytes']), (6240, 6240 * 4 * 32))
        self.assertLessEqual(b['peak_max'], 2.0 + 1e-6)
        self.assertGreater(b['peak_max'], 1.96)
        self.assertAlmostEqual(b['median_decoded'], 0.05, delta=0.006)
        for ratio in b['bin_ratios']:
            self.assertAlmostEqual(ratio, 2.5, delta=0.45)
        # The defaults' footprints: half-width 1.5 px (dimmest) to 2.875 px (brightest, 1.5 + 1.4 on the 1/16 grid).
        self.assertEqual((b['support_min'], b['support_max']), (1.5, 2.875))
        self.assertLess(b['hot_support_max'], 8.0)

    def test_first_law_and_field(self):
        self.assertEqual(self.report['legacy'], {'mismatches': 0, 'field_mismatches': 0})

    def test_twinkle_lanes(self):
        t = self.report['twinkle']
        self.assertAlmostEqual(t['share'], 0.25, delta=0.03)
        self.assertAlmostEqual(t['bright_quarter_share'], 0.25, delta=0.06)
        self.assertEqual((t['rate_min'], t['rate_max'], t['bad_lanes']), (52, 192, 0))

    def test_kernel(self):
        k = self.report['kernel']
        self.assertLess(k['energy_rel_err'], 1e-9)
        self.assertLessEqual(k['peak_max'], 2.0 + 1e-6)
        self.assertEqual(k['outside'], 0)

    def test_meter_replay(self):
        for name in ('meter_k65_defaults', 'meter_k143_d4'):
            m = self.report[name]
            self.assertTrue(m['neutral'], m)
            self.assertEqual((m['ev_target'], m['lit']), (0.0, 0), m)
            self.assertGreaterEqual(m['ev_limit'], 1.3, m)
            self.assertLessEqual(m['frame_max'], 2.0 + 1e-3, m)
        self.assertIn('ev_limit', self.report['meter_k143_max_look'])  # reported, not asserted
        self.assertLess(self.report['meter_control_lum64']['ev_limit'], -2.0)


class SkyStarsConstantsTests(unittest.TestCase):
    def test_baker_and_runtime_agree(self):
        import sky_textures as st
        core = (ROOT / 'src/proxy/sky_stars_core.h').read_text()
        mark = (ROOT / 'src/proxy/lod_texture_mark.h').read_text()
        self.assertEqual(int(re.search(r'payload_sky_stars = (\d+);', mark).group(1)), st.PAYLOAD_SKY_STARS)
        self.assertEqual(int(re.search(r'lane_count = (\d+);', core).group(1)), len(st.STAR_LANES) + 1)
        self.assertEqual(st.LANE_OTHER, len(st.STAR_LANES))
        self.assertIn('stars_per_k = 48, stars_min = 512, stars_max = 16384', core)


if __name__ == '__main__':
    unittest.main()
