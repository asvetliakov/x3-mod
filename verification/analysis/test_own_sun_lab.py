"""Own sun tuning lab (tools/effects/own_sun_lab.html; docs/architecture/own-sun-look.md, "The tuning lab"): the page's
generated constants block matches src/proxy/own_sun_core.h (generate_own_sun_lab.py --check); the page is one file that
opens from disk (no module, fetch or external script); the GLSL carries the program's literal constants (hashes, tap
weights, arctangent); and the page's JS law, run under node, agrees with the C++ host reference (own_sun_core.h
reference_pixel through verification/probe/own_sun_host.cpp) on the glow at 1, 2, 3 and 6 core radii within 1e-3. A
Python port of the law from the generated block checks the same numbers where node is absent.
"""
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HTML = ROOT / 'tools/effects/own_sun_lab.html'
RADII = (1, 2, 3, 6)
ANGLES = (0., 20., 45., 75., 100., 225.)  # degrees, on and between spokes
SUNS = ((255, 255, 255, 2., 1., 1.), (255, 255, 200, 2., .8, .6))  # words, K, fog T, sun_rays


def fields(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line))


def section(text, begin, end):
    start = text.index(begin)
    return text[start:text.index(end, start) + len(end)]


def header_block(text):
    """The generated HEADER object as a dict (its keys are bare JS identifiers)."""
    body = section(text, 'const HEADER = {', '};')[len('const HEADER = '):-1]
    return json.loads(re.sub(r'^(\s*)(\w+):', r'\1"\2":', body, flags=re.M))


def python_law(h, words, k, fog, rays, px, py):
    """own_sun_core.h radiance() + pixel_constants() + reference_pixel() at 5120x1440 (the driver's frame), all taps open."""
    c = [min(max(w / 256., 0.), 4.) for w in words]
    m = max(c)
    glow = [(k / h['radiance_default']) * ci * fog for ci in c]
    radius_px = h['radius_default_u'] * 1440
    sigma = max(h['ray_sigma_min_px'] / radius_px, h['ray_sigma'])
    n = h['ray_count']
    share = rays
    r2 = px * px + py * py
    r = math.sqrt(r2)
    corona = h['corona_gain'] * math.exp(-r2 / h['corona_sigma'] ** 2)
    w = min(max(1 - r2 / h['glow_radius'] ** 2, 0.), 1.)
    halo = h['halo_gain'] / (1 + r2 / h['halo_radius'] ** 2) * w * w

    def spoke(index, across):
        kk = index - n * math.floor(index / n)
        h1, h2 = (kk * .6180340 + .3) % 1, (kk * .7548777 + .31) % 1
        along = max(r - h['ray_start'], 0.) / (h['ray_length'] * (.15 + .85 * h1))
        fade = min(max(1 - along, 0.), 1.)
        perpendicular = r * across * (2 * math.pi / n) / sigma
        return (.6 + .6 * h2) * math.exp(-perpendicular ** 2 - along) * fade * fade

    t = math.atan2(py, px) * n / (2 * math.pi) + h['ray_offset']
    nearest = math.floor(t + .5)
    across = t - nearest
    side = 1. if across >= 0 else -1.
    ray = h['ray_gain'] * share * (spoke(nearest, across) + spoke(nearest + side, across - side))
    return dict(corona=corona, halo=halo, rays=ray, glow=corona + halo + ray, tint=glow, m=m)


class OwnSunLab(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = HTML.read_text()
        cls.dir = tempfile.TemporaryDirectory(prefix='x3-own-sun-lab-')
        cls.driver = Path(cls.dir.name) / 'own_sun_host'
        compiler = shutil.which('clang++') or shutil.which('c++')
        subprocess.run([compiler, '-std=c++17', '-O2', str(ROOT / 'verification/probe/own_sun_host.cpp'), '-o', str(cls.driver)],
                       check=True)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def run_driver(self, *args):
        return fields(subprocess.check_output([str(self.driver)] + [str(a) for a in args], text=True).strip())

    def reference(self, sun, px, py, open_taps=9, visible=1.):
        r, g, b, k, fog, rays = sun
        out = self.run_driver('pixel', r, g, b, k, fog, rays, px, py, open_taps, visible)
        return {key: ([float(v) for v in out[key].split(',')] if key == 'rgb' else float(out[key])) for key in out}

    def test_generated_block_is_current(self):
        done = subprocess.run([sys.executable, str(ROOT / 'tools/effects/generate_own_sun_lab.py'), '--check'],
                              capture_output=True, text=True)
        self.assertEqual(done.returncode, 0, done.stdout + done.stderr)
        self.assertIn('PASS', done.stdout)

    def test_single_file_that_opens_from_disk(self):
        self.assertEqual(self.text.count('<script'), 1)
        self.assertNotIn('<script src', self.text)
        for banned in ('type="module"', 'fetch(', 'import(', 'XMLHttpRequest', 'new Worker'):
            self.assertNotIn(banned, self.text)
        self.assertEqual(set(re.findall(r'https?://([^/"\s]+)', self.text)), {'fonts.googleapis.com'})
        self.assertIn('WebGL2 is not available', self.text)                 # no WebGL2: a note, not an exception
        self.assertIn('fsDirect', self.text)                                 # no float target: the scene without bloom

    def test_glsl_carries_the_programs_literals(self):
        hlsl = (ROOT / 'src/effects/own_sun_ps.hlsl').read_text()
        hlsl = hlsl[hlsl.index('sampler2D lane_sampler'):]
        literals = set(re.findall(r'(?<![\w.])-?\d+\.\d{4,}', hlsl)) | {'0.31', '0.85', '0.15'}
        glsl = section(self.text, 'const SCENE=`', '}`;')
        for literal in sorted(literals):
            self.assertIn(literal.lstrip('-'), glsl, literal)
        self.assertEqual(len(re.findall(r'vec3\(-?0\.\d{7},-?0\.\d{7},0\.07\d{5}\)', glsl)), 12)   # the 12 ring taps
        law = section(self.text, '/* @law_js */', '/* @end_law_js */')                # the CPU mirror: hashes, arctangent
        for literal in ('0.6180340', '0.7548777', '0.31', '0.85', '0.0134804', '0.0574773', '0.1212391', '0.1956359',
                        '0.3329946', '0.9999956', '1.57079633', '3.14159265'):
            self.assertIn(literal, law, literal)

    def test_python_port_of_the_block_matches_the_reference(self):
        h = header_block(self.text)
        for sun in SUNS:
            for radius in RADII:
                for angle in ANGLES:
                    a = math.radians(angle)
                    px, py = radius * math.cos(a), radius * math.sin(a)
                    want = self.reference(sun, px, py)
                    got = python_law(h, sun[:3], *sun[3:], px, py)
                    self.assertAlmostEqual(got['glow'], want['corona'] + want['halo'] + want['rays'], delta=1e-3,
                                           msg=(sun, radius, angle))

    @unittest.skipUnless(shutil.which('node'), 'node is not installed: the Python port above covers the constants')
    def test_page_js_law_matches_the_reference(self):
        points = [(sun, radius * math.cos(math.radians(a)), radius * math.sin(math.radians(a)), 9, 1.)
                  for sun in SUNS for radius in RADII for a in ANGLES]
        points += [(SUNS[1], .3, .1, 9, 1.), (SUNS[1], .8, .2, 7, .5), (SUNS[1], 2., 1., 4, .3)]   # core, edge, clipped
        script = '\n'.join([section(self.text, '/* @own_sun_constants */', '/* @end */'),
                            section(self.text, '/* @law_js */', '/* @end_law_js */'), '''
const points = %s, out = [];
const L = lookFromHeader(HEADER), R = HEADER.radius_default_u * 1440;
for (const [sun, px, py, open, visible] of points) {
  const rad = sunRadiance(sun.slice(0, 3).map(w => w / 256), sun[3], sun[4], L, HEADER);
  const c = pixelConstants(5120, 1440, {x: 0, y: 0, radius_px: R}, rad, sun[5], L, HEADER);
  out.push(lawPixel(c, px, py, open, visible));
}
const white = sunRadiance([255 / 256, 255 / 256, 255 / 256], 2, 1, L, HEADER);
const glowMax = glowMaxOutsideCore(pixelConstants(5120, 1440, {x: 0, y: 0, radius_px: R}, white, 1, L, HEADER), L.glow_radius);
console.log(JSON.stringify({out, glowMax}));
''' % json.dumps([[list(s), x, y, o, v] for s, x, y, o, v in points])])
        result = json.loads(subprocess.check_output(['node', '-e', script], text=True))
        worst = 0.
        for (sun, px, py, open_taps, visible), got in zip(points, result['out']):
            want = self.reference(sun, px, py, open_taps, visible)
            pairs = [(got[k], want[k]) for k in ('corona', 'halo', 'rays', 'core')]
            pairs += [(got['corona'] + got['halo'] + got['rays'], want['corona'] + want['halo'] + want['rays'])]
            pairs += list(zip(got['rgb'], want['rgb']))
            for a, b in pairs:
                worst = max(worst, abs(a - b))
                self.assertAlmostEqual(a, b, delta=1e-3, msg=(sun, px, py, open_taps, visible))
        self.assertLess(worst, 1e-4)  # in practice float vs double and the minimax arctangent only
        # The HUD's "glow max outside the core" against the reference's cartesian field scan at 0.02 core radii (its grid
        # can only miss the peak, which sits at the core's edge: the lab's polar scan reads at least as high, within 1 %).
        field = float(self.run_driver('field', 255, 255, 255, 2, 1, 1, .02)['glow_max'])
        self.assertGreaterEqual(result['glowMax'], field - 1e-4)
        self.assertLess(result['glowMax'], field * 1.01)


if __name__ == '__main__':
    unittest.main()
