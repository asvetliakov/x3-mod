"""Own sun, step 1 (docs/architecture/own-sun-pass.md, the AP minimal mode): host checks of the stage's CPU side through
the production header src/proxy/own_sun_core.h (driver verification/probe/own_sun_host.cpp): the projection of the polled
position with the frame camera against a double-precision restatement, the radius-in-pixels rule with its 1.5 px floor,
the fog's far-transmittance scalar, the radiance and the pixel law's centre and edge, the probe answer for eligible and
other records, the comparison hotkey's per-frame flip, the quad and the pixel constants (the knight kernel's documented
edge profile from the constants themselves) and the compiled program's register map; the wiring in the AP and FL sources;
the launcher option. No device, no Wine, no game.
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from source_text import source_text

ROOT = Path(__file__).resolve().parents[2]
SCALE = 0.01  # engine integers -> world units (own_sun_core.h context_scale)


def fields(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line))


def floats(text):
    return [float(v) for v in text.split(',')]


def rotation(a, b, c):
    """An orthonormal 3x3 (row-major r[i*3+j]) from three angles."""
    ca, sa, cb, sb, cc, sc = math.cos(a), math.sin(a), math.cos(b), math.sin(b), math.cos(c), math.sin(c)
    x = [[1, 0, 0], [0, ca, -sa], [0, sa, ca]]
    y = [[cb, 0, sb], [0, 1, 0], [-sb, 0, cb]]
    z = [[cc, -sc, 0], [sc, cc, 0], [0, 0, 1]]
    mul = lambda p, q: [[sum(p[i][k] * q[k][j] for k in range(3)) for j in range(3)] for i in range(3)]
    m = mul(mul(x, y), z)
    return [m[i][j] for i in range(3) for j in range(3)]


def f32(v):
    return struct.unpack('<f', struct.pack('<f', v))[0]


def reference_projection(p, r, t, m00, m11, m20, m21, w, h, radius, jx, jy):
    r = [f32(v) for v in r]
    t = [f32(v) for v in t]
    view = [t[j] + sum(p[i] * SCALE * r[i * 3 + j] for i in range(3)) for j in range(3)]
    x = view[0] * m00 / view[2] + m20
    y = view[1] * m11 / view[2] + m21
    rp = radius * h  # the radius is a fraction of the viewport height
    return dict(u=.5 + .5 * x, v=.5 - .5 * y, x=x + 2 * jx / w, y=y - 2 * jy / h, half_x=3 * 2 * rp / w, half_y=3 * 2 * rp / h,
                radius_px=rp, view_z=view[2])


def fog_reference(extinction, start, cap, k):
    depth = start + (cap - start) * .5  # D(cap): u = 1
    rgb = [math.exp(-ki * extinction * depth) for ki in k]
    return min(max(.2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2], 0.), 1.)


class OwnSunHost(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory(prefix='x3-own-sun-')
        cls.driver = Path(cls.dir.name) / 'own_sun_host'
        compiler = shutil.which('clang++') or shutil.which('c++')
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', str(ROOT / 'verification/probe/own_sun_host.cpp'),
                        '-o', str(cls.driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def run_driver(self, *args):
        return fields(subprocess.check_output([str(self.driver)] + [str(a) for a in args], text=True).strip())

    def project(self, p, r=(1, 0, 0, 0, 1, 0, 0, 0, 1), t=(0, 0, 0), m=(1, 1.7777778, 0, 0), size=(1280, 768), radius=.0667, jitter=(0, 0)):
        out = self.run_driver('project', *p, *r, *t, *m, *size, radius, *jitter)
        return {k: (v if k == 'skip' else float(v)) for k, v in out.items()}

    def test_projection_matches_the_double_reference(self):
        rng = random.Random(20261102)
        checked = 0
        for _ in range(60):
            r = rotation(rng.uniform(-3, 3), rng.uniform(-1.4, 1.4), rng.uniform(-3, 3))
            t = [rng.uniform(-50000, 50000) for _ in range(3)]
            m = (rng.uniform(.6, 1.5), rng.uniform(1, 2.5), rng.uniform(-.001, .001), rng.uniform(-.001, .001))
            size = rng.choice(((1280, 768), (1920, 1080), (5120, 1440)))
            jitter = (rng.uniform(-.5, .5), rng.uniform(-.5, .5))
            # A sun in front of this camera: view (vx, vy, vz) with |vx/vz| < 0.5, mapped back to world integers.
            vz = rng.uniform(3e6, 1.5e7)  # world integers stay inside int32 (the poll's words)
            view = [rng.uniform(-.4, .4) * vz, rng.uniform(-.3, .3) * vz, vz]
            world = [sum((view[j] - t[j]) * r[i * 3 + j] for j in range(3)) for i in range(3)]  # R orthonormal: R^-1 = R^T
            p = [int(round(v / SCALE)) for v in world]
            got = self.project(p, r, t, m, size, .0667, jitter)
            want = reference_projection(p, r, t, *m, *size, .0667, *jitter)
            self.assertEqual(got['skip'], 'none' if abs(want['x']) - want['half_x'] < 1 and abs(want['y']) - want['half_y'] < 1 else 'offscreen')
            for key in ('u', 'v', 'x', 'y', 'half_x', 'half_y', 'radius_px'):
                self.assertAlmostEqual(got[key], want[key], delta=2e-5 * max(1., abs(want[key])), msg=(key, p))
            self.assertAlmostEqual(got['view_z'], want['view_z'], delta=1e-3 * want['view_z'])
            checked += 1
        self.assertEqual(checked, 60)

    def test_sun_ahead_is_centred_and_jitter_moves_only_the_quad(self):
        d = self.project((0, 0, 10 ** 9))
        self.assertEqual(d['skip'], 'none')
        self.assertEqual((d['u'], d['v'], d['x'], d['y']), (.5, .5, 0., 0.))
        j = self.project((0, 0, 10 ** 9), jitter=(.5, -.25))
        self.assertEqual((j['u'], j['v']), (.5, .5))                       # the logged position is the unjittered one
        self.assertAlmostEqual(j['x'], 2 * .5 / 1280, places=7)            # +jx right: m20 + 2 jx / W
        self.assertAlmostEqual(j['y'], -2 * -.25 / 768, places=7)          # +jy down: m21 - 2 jy / H

    def test_behind_offscreen_and_camera_refusals(self):
        self.assertEqual(self.project((0, 0, -10 ** 9))['skip'], 'behind')
        self.assertEqual(self.project((0, 0, 0))['skip'], 'behind')        # at the camera
        self.assertEqual(self.project((3 * 10 ** 9, 0, 10 ** 9))['skip'], 'offscreen')
        # Centre just outside the right edge, the quad still reaches in: drawn (clipped by the hardware).
        edge = self.project((int(1.05 * 10 ** 9), 0, 10 ** 9))
        self.assertGreater(edge['x'], 1.)
        self.assertEqual(edge['skip'], 'none')
        self.assertEqual(self.project((0, 0, 10 ** 9), m=(0, 1, 0, 0))['skip'], 'camera')
        self.assertEqual(self.project((0, 0, 10 ** 9), m=(1, 1, float('nan'), 0))['skip'], 'camera')
        self.assertEqual(self.project((0, 0, 10 ** 9), size=(0, 720))['skip'], 'size')

    def test_radius_in_pixels_and_the_small_rule(self):
        # The radius is a fraction of the viewport height (the engine's vertical FOV): the same at every width.
        self.assertAlmostEqual(self.project((0, 0, 10 ** 9))['radius_px'], 51.2256, places=3)          # 0.0667 x 768
        self.assertAlmostEqual(self.project((0, 0, 10 ** 9), size=(5120, 1440))['radius_px'], 96.048, places=2)
        self.assertEqual(self.project((0, 0, 10 ** 9), size=(5120, 1440))['radius_px'],
                         self.project((0, 0, 10 ** 9), size=(2560, 1440))['radius_px'])
        d = self.project((0, 0, 10 ** 9), size=(1280, 720), radius=.1)
        self.assertAlmostEqual(d['half_x'], 3 * 2 * 72 / 1280, places=6)                               # square pixels: round
        self.assertAlmostEqual(d['half_y'], 3 * 2 * 72 / 720, places=6)
        self.assertAlmostEqual(d['half_x'] * 1280, d['half_y'] * 720, places=3)
        self.assertEqual(self.project((0, 0, 10 ** 9), size=(20, 22))['skip'], 'small')                # 1.467 px
        self.assertEqual(self.project((0, 0, 10 ** 9), size=(20, 23))['skip'], 'none')                 # 1.534 px
        self.assertAlmostEqual(self.project((0, 0, 10 ** 9), size=(1280, 720), radius=.005)['radius_px'], 3.6, places=5)
        for bad in (.001, .3, float('nan')):                                                           # out of range: the default
            self.assertAlmostEqual(self.project((0, 0, 10 ** 9), radius=bad)['radius_px'], 51.2256, places=3)

    def test_fog_scalar(self):
        self.assertEqual(float(self.run_driver('fog', 0, 1e-4, 65000, 112500, 1, 1, 1)['t']), 1.)      # no composite: exactly 1
        for e, k in ((1e-6, (1, 1, 1)), (5e-6, (1.2, 1, .9)), (2e-5, (1, 1.6, 1.6))):
            t = float(self.run_driver('fog', 1, e, 65000, 112500, *k)['t'])
            self.assertAlmostEqual(t, fog_reference(e, 65000, 112500, k), delta=2e-5, msg=(e, k))
        self.assertEqual(float(self.run_driver('fog', 1, 1e-2, 65000, 112500, 1, 1, 1)['t']), 0.)      # opaque column
        dense = self.run_driver('radiance', 255, 255, 255, 8, 0)
        self.assertEqual(dense['black'], '1')                                                          # T = 0 draws nothing

    def test_radiance_and_the_pixel_law(self):
        # disc = K lerp(c / m, 1, 0.6) m T, corona = 0.8 (K / 8) c T, m = max_ch(c).
        def want(words, k, fog):
            c = [w / 256 for w in words]
            m = max(c)
            return [k * (ci / m + (1 - ci / m) * .6) * m * fog for ci in c], [.8 * k / 8 * ci * fog for ci in c]
        for words, k, fog in (((256, 128, 0), 8, .5), ((50, 50, 50), 8, 1), ((70, 65, 85), 8, 1), ((200, 180, 255), 4, .7)):
            r = self.run_driver('radiance', *words, k, fog)
            self.assertEqual(r['black'], '0')
            disc, corona = want(words, k, fog)
            for got, value in zip(floats(r['disc']), disc):
                self.assertAlmostEqual(got, value, places=5, msg=words)
            for got, value in zip(floats(r['corona']), corona):
                self.assertAlmostEqual(got, value, places=6, msg=words)
        # A dark sun stays dark: #323232 (FL) is a grey disc at 0.195 x K, not a white one at 0.68 K.
        dark = floats(self.run_driver('radiance', 50, 50, 50, 8, 1)['disc'])
        self.assertAlmostEqual(dark[0], 8 * 50 / 256, places=5)
        # The hue survives the whitening at full brightness: a blue-white sun keeps blue the largest channel.
        blue = floats(self.run_driver('radiance', 100, 150, 255, 8, 1)['disc'])
        self.assertGreater(blue[2], blue[1])
        self.assertGreater(blue[1], blue[0])
        self.assertEqual(self.run_driver('radiance', 0, 0, 0, 8, 1)['black'], '1')                    # #000000: nothing
        self.assertEqual(self.run_driver('radiance', 255, 255, 255, 0, 1)['black'], '1')              # K = 0: nothing
        self.assertEqual(floats(self.run_driver('radiance', 4096, -5, 256, 8, 1)['colour']), [4., 0., 1.])  # clamped
        # Full sentinel lane: the centre texel is the disc radiance; the quad's edge (r = 3) is 0; open taps scale it.
        centre = floats(self.run_driver('pixel', 200, 100, 50, 8, 1, 0, 9, math.exp(3))['rgb'])
        for got, value in zip(centre, want((200, 100, 50), 8, 1)[0]):
            self.assertAlmostEqual(got, value, places=5)
        self.assertEqual(floats(self.run_driver('pixel', 200, 100, 50, 8, 1, 3, 9, math.exp(-6))['rgb']), [0., 0., 0.])
        corona = floats(self.run_driver('pixel', 256, 256, 256, 8, 1, 2, 9, math.exp(-3))['rgb'])
        self.assertAlmostEqual(corona[0], .8 * (math.exp(-3) - math.exp(-6)), places=6)
        seven = floats(self.run_driver('pixel', 200, 100, 50, 8, 1, 0, 7, math.exp(3))['rgb'])
        self.assertAlmostEqual(seven[0], centre[0] * 7 / 9, places=5)
        self.assertEqual(floats(self.run_driver('pixel', 200, 100, 50, 8, 1, 0, 0, math.exp(3))['rgb']), [0., 0., 0.])

    def test_probe_answer(self):
        table = {(a, e): int(self.run_driver('answer', a, e)['answer']) for a in (0, 1) for e in (0, 1)}
        self.assertEqual(table, {(0, 0): 2, (0, 1): 2, (1, 0): 2, (1, 1): 1})  # hidden only for the eligible record

    def test_hotkey_flips_per_frame(self):
        t = self.run_driver('toggle', '00011110001101')
        self.assertEqual(t['initial'], '1')
        # A held key is one press; release and press again flips back; the state holds between presses.
        self.assertEqual(t['on'], '11100000001110')
        self.assertEqual(t['edges'], '3')
        for configured, key, ready in ((a, b, c) for a in (0, 1) for b in (0, 1) for c in (0, 1)):
            self.assertEqual(self.run_driver('frame_active', configured, key, ready)['active'],
                             '1' if configured and key and ready else '0')

    def test_quad_strip(self):
        q = self.run_driver('quad', .25, -.5, .1, .2)
        vertices = [floats(q['v%d' % k]) for k in range(4)]
        for got, want in zip([v[:2] for v in vertices], [(.15, -.7), (.15, -.3), (.35, -.7), (.35, -.3)]):
            self.assertAlmostEqual(got[0], want[0], places=6)
            self.assertAlmostEqual(got[1], want[1], places=6)
        self.assertEqual([v[4:] for v in vertices], [[-3, -3], [-3, 3], [3, -3], [3, 3]])
        self.assertEqual({tuple(v[2:4]) for v in vertices}, {(0., 1.)})

    def test_constants_give_the_knight_kernel_and_its_edge_profile(self):
        w, h = 1280, 720
        c = floats(self.run_driver('constants', w, h, 1, 2, 3, 4, 5, 6)['c'])
        self.assertEqual(len(c), 20)
        self.assertAlmostEqual(c[0] * w, 1.)
        self.assertAlmostEqual(c[1] * h, 1.)
        self.assertEqual(c[12:15] + c[16:19], [1., 2., 3., 4., 5., 6.])
        d, wv = c[4:8], c[8:12]
        offsets = set()
        for vec in (d[0:2], d[2:4], wv[0:2], wv[2:4]):
            for sign in (1, -1):
                offsets.add((round(sign * vec[0] * w), round(sign * vec[1] * h)))
        self.assertEqual(offsets, {(dx, dy) for dx in (-2, -1, 1, 2) for dy in (-2, -1, 1, 2) if abs(dx) != abs(dy)})
        # A vertical silhouette (columns >= 0 covered): the open fraction across it, from these offsets and the centre.
        taps = list(offsets) + [(0, 0)]
        profile = [sum(1 for dx, _ in taps if p + dx < 0) / 9 for p in range(-3, 3)]
        self.assertEqual(profile, [1, 7 / 9, 5 / 9, 4 / 9, 2 / 9, 0])

    def test_compiled_programs_match_the_constant_layout(self):
        sun = load('own_sun_sun_occlusion_test', 'verification/analysis/test_sun_occlusion.py')
        words = lambda name: [int(v, 16) for v in re.findall(r'0x([0-9a-f]{8})u', (ROOT / 'src/renderer' / name).read_text())]
        ps, vs = words('own_sun_pixel_program_inc.h'), words('own_sun_vertex_program_inc.h')
        self.assertEqual((ps[0], ps[-1], vs[0], vs[-1]), (0xffff0300, 0xffff, 0xfffe0300, 0xffff))
        self.assertEqual(sun.constant_table(ps), {'texel': (2, 0), 'knight_d': (2, 1), 'knight_w': (2, 2), 'disc_rgb': (2, 3),
                                                  'corona_rgb': (2, 4), 'lane_sampler': (3, 0)})
        self.assertEqual(sun.constant_table(vs), {})                     # the vertex program takes no constant
        self.assertIn('constexpr unsigned pixel_registers = 5;', source_text(ROOT / 'src/proxy/own_sun_core.h'))
        native = json.loads((ROOT / 'verification/results/own-sun-pass/native/result.json').read_text())
        self.assertTrue(native['passed'])
        self.assertEqual({r['program']: (r['words'], r['end'], len(r['messages'])) for r in native['programs']},
                         {'own_sun_vs': (len(vs), '00000000', 0), 'own_sun_ps': (len(ps), '00000000', 0)})

    def test_cpu_side_cost(self):
        out = self.run_driver('bench', 200000)
        self.assertEqual(out['frames'], '200000')
        self.assertLess(float(out['ns_per_frame']), 20000.)  # a few microseconds at most (design section 9)


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class OwnSunWiring(unittest.TestCase):
    def test_ap_latch_and_fl_row(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        latch = capture.index('    if (own_sun_on) {\n        if (own_sun_key.step(own_sun_chord_down()))')
        self.assertLess(latch, capture.index('    sun_occlusion::present(ctx.frame + 1); // installed bracket only'))
        self.assertEqual(capture.count('own_sun_chord_down()'), 2)        # the definition and the one poll under own_sun_on
        self.assertIn('log("own_sun status=unavailable edition=fl requested=%u reason=fl_step_pending"', capture)
        self.assertIn('own_sun_on = own_sun_requested && motion_output_requested && own_sun_radiance > 0.f && sun_installed;', capture)
        fl_row = capture.index('log("own_sun status=unavailable edition=fl')
        self.assertEqual(capture.rindex('#else', 0, fl_row) > capture.rindex('#if !X3M_EDITION_FL', 0, fl_row), True)

    def test_stage_draws_only_after_a_hidden_answer(self):
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        wiring = motion[motion.index('if (own_sun_requested_ && own_sun_arm('):]
        wiring = wiring[:wiring.index('#endif')]
        self.assertLess(wiring.index('if (sun_occlusion::own_sun_hidden()) {'), wiring.index('in.stage_callback = &MotionOutput::own_sun_stage_callback;'))
        self.assertIn('} else\n                    own_sun_no_probe();', wiring)
        sun = (ROOT / 'src/proxy/sun_occlusion.cpp').read_text()
        self.assertIn('return patched_ && own_sun_hidden_seen_ && own_sun_hidden_frame_ == frame_;', sun)
        branch = sun[sun.index('++counters_.own_sun_hidden;'):]
        self.assertTrue(branch.startswith('++counters_.own_sun_hidden;\n        own_sun_hidden_seen_ = true;\n        own_sun_hidden_frame_ = frame;\n        return core::Hidden;'))
        stage = (ROOT / 'src/proxy/motion_output_own_sun_inc.h').read_text()
        arm = stage[stage.index('bool MotionOutput::own_sun_arm('):stage.index('// The stage callback while')]
        # F2: the poll decides the readiness at the arming (the next probe forwards when it is not Ok).
        self.assertLess(arm.index('note_own_sun_state(false, "poll");'), arm.index('own_sun_ready_ = true;'))
        self.assertIn('if (f.disc.skip == own_sun::core::Skip::Camera || f.disc.skip == own_sun::core::Skip::Size) own_sun_ready_ = false;', stage)
        # F5: radiance 0 requests nothing, so the chain is never hidden for it.
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('if (own_sun_requested && motion_output_requested && own_sun_radiance > 0.f) sun_occlusion::request_own_sun();', capture)
        self.assertIn('own_sun_on = own_sun_requested && motion_output_requested && own_sun_radiance > 0.f && sun_installed;', capture)

    def test_stage_is_ap_only(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        shared = cmake[cmake.index('set(X3M_SHARED_SOURCES'):cmake.index('set(X3M_AP_ENGINE_SOURCES')]
        self.assertNotIn('own_sun', shared)
        self.assertIn('src/renderer/own_sun_pass.cpp', cmake[cmake.index('set(X3M_AP_SOURCES'):cmake.index('set(X3M_SHARED_SOURCES')])
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        for needle in ('#include "motion_output_own_sun_inc.h"', 'release_own_sun();', 'own_sun_->before_reset()',
                       'own_sun_->after_reset(result)', 'in.stage_callback = &MotionOutput::own_sun_stage_callback;'):
            at = motion.index(needle)
            self.assertGreater(motion.rfind('#if !X3M_EDITION_FL', 0, at), motion.rfind('#endif', 0, at), needle)
        header = (ROOT / 'src/proxy/motion_output.h').read_text()
        at = header.index('bool own_sun_requested_ = false')
        self.assertGreater(header.rfind('#if !X3M_EDITION_FL', 0, at), header.rfind('#endif', 0, at))


class OwnSunLaunchOption(unittest.TestCase):
    NAMES = ('X3M_OWN_SUN', 'X3M_SUN_DISC_RADIUS', 'X3M_SUN_DISC_RADIANCE')

    def launch(self, *args, vanilla=False, inherited=None):
        module = load('own_sun_manage', 'tools/manage.py')
        with tempfile.TemporaryDirectory() as directory:
            game = Path(directory) / 'game'
            game.mkdir()
            (game / 'X3AP.exe').touch()
            (game / 'd3d9.dll').write_bytes(b'fixture')
            (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'fixture').hexdigest()}))
            wine = Path(directory) / 'wine'
            wine.touch()
            argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(game), *args]
            output, error = io.StringIO(), io.StringIO()
            with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                    mock.patch.object(module, 'VOICE_DECODER_REPO', None), mock.patch.dict(module.os.environ, inherited or {}), \
                    mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                    contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
                try:
                    module.main()
                except SystemExit as exit_error:
                    return exit_error.code, None, error.getvalue()
        return 0, {k: v for k, v in json.loads(output.getvalue())['env'].items() if k in self.NAMES}, error.getvalue()

    def test_default_off_and_explicit_values(self):
        self.assertEqual(self.launch()[1], {'X3M_OWN_SUN': '0'})
        self.assertEqual(self.launch(inherited={'X3M_OWN_SUN': '1', 'X3M_SUN_DISC_RADIUS': '0.2'})[1], {'X3M_OWN_SUN': '0'})
        self.assertEqual(self.launch('--own-sun', 'on')[1], {'X3M_OWN_SUN': '1'})
        self.assertEqual(self.launch('--own-sun', 'on', '--sun-disc-radius', '0.05', '--sun-disc-radiance', '6')[1],
                         {'X3M_OWN_SUN': '1', 'X3M_SUN_DISC_RADIUS': '0.05', 'X3M_SUN_DISC_RADIANCE': '6'})
        self.assertEqual(self.launch(vanilla=True)[1], {})

    def test_refusals(self):
        for args, vanilla, needle in ((('--own-sun', 'on'), True, 'cannot be combined with --vanilla'),
                                      (('--sun-disc-radius', '0.05'), False, 'requires --own-sun on'),
                                      (('--own-sun', 'off', '--sun-disc-radiance', '4'), False, 'requires --own-sun on'),
                                      (('--own-sun', 'on', '--sun-disc-radius', '0.3'), False, 'out of range'),
                                      (('--own-sun', 'on', '--sun-disc-radiance', 'nan'), False, 'out of range'),
                                      (('--own-sun', 'maybe'), False, 'invalid choice')):
            with self.subTest(args=args):
                code, _, error = self.launch(*args, vanilla=vanilla)
                self.assertEqual(code, 2)
                self.assertIn(needle, error)


if __name__ == '__main__':
    unittest.main()
