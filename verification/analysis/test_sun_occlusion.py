"""Host checks of the partial-sun-occlusion pure parts (docs/architecture/sun-partial-occlusion.md).

The probe override's decision table against a Python restatement of the vanilla prologue of
0x00488720 (three tests, in order, RE note section 16), the main-view guard and the readiness
fallback; the readiness state machine; record -> footprint; the blend classification; the
main-view registry walk over a fake image; the structural pixel wrap over hand-assembled
ps_2_0 / ps_3_0 programs. The C++ driver (verification/probe/sun_occlusion_host.cpp) uses the
production headers. No device, no Wine.
"""
import contextlib
import hashlib
import importlib.util
import io
import itertools
import json
import math
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
VISIBLE, HIDDEN, ORIGINAL = 0, 1, 2
FULL = (0, 0x10000, 0, 0x10000)  # top, bottom, left, right


def fields(line):
    return dict(re.findall(r'(\w+)=([^ ]+)', line))


def wrap32(v):
    v &= 0xffffffff
    return v - (1 << 32) if v & 0x80000000 else v


def vanilla_prologue(video, view_flags, x, y, rect):
    """0x00488742 / 0x00488763..0x004887a2 / 0x004887a8: 1, 0, 1, else the collision probe (None)."""
    top, bottom, left, right = rect
    if not video & 0x8000:
        return 1
    sx, sy = wrap32(x + 0x8000), wrap32(0x8000 - y)
    if sx < left or sx > right or sy < top or sy > bottom:
        return 0
    if view_flags & 0x8000000:
        return 1
    return None


class SunOcclusionHost(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.dir = tempfile.TemporaryDirectory(prefix='x3-sun-occlusion-')
        cls.driver = Path(cls.dir.name) / 'sun_occlusion_host'
        compiler = shutil.which('clang++') or shutil.which('c++')
        subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'verification/probe/sun_occlusion_host.cpp'), str(ROOT / 'src/renderer/lens_visibility_variant.cpp'),
                        '-o', str(cls.driver)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.dir.cleanup()

    def run_driver(self, *args):
        return fields(subprocess.check_output([str(self.driver)] + [str(a) for a in args], text=True).strip())

    def decide(self, ready=1, owner=0x100, view=0x100, main=0x100, video=0x8000, view_flags=0, x=0, y=0, rect=FULL):
        return int(self.run_driver('decide', ready, owner, view, main, video, view_flags, x, y, *rect)['decision'])

    def test_guard_and_readiness_fall_back_to_the_original(self):
        self.assertEqual(self.decide(), VISIBLE)
        self.assertEqual(self.decide(ready=0), ORIGINAL)
        self.assertEqual(self.decide(owner=0x200), ORIGINAL)          # a record of another view probed by the main view
        self.assertEqual(self.decide(view=0x200, owner=0x200), ORIGINAL)  # a monitor's own record
        self.assertEqual(self.decide(view=0x200), ORIGINAL)           # the main view's record re-probed by a later view
        self.assertEqual(self.decide(main=0), ORIGINAL)               # main view unknown
        # The guard precedes the gates: a foreign view is the original's even with the flare option off.
        self.assertEqual(self.decide(view=0x200, video=0), ORIGINAL)

    def test_gate_order_matches_the_vanilla_prologue(self):
        cases = itertools.product((0, 0x8000, 0xffff7fff), (0, 0x8000000, 0x8000001), (-0x9000, -0x8000, 0, 0x8000, 0x8001, 0x7fffffff),
                                  (-0x8001, -0x8000, 0, 0x8000, 0x9000, -0x80000000), (FULL, (0x2000, 0xc000, 0x1000, 0xf000)))
        checked = 0
        for video, view_flags, x, y, rect in cases:
            expected = vanilla_prologue(video, view_flags, x, y, rect)
            got = self.decide(video=video, view_flags=view_flags, x=x, y=y, rect=rect)
            self.assertEqual(got, VISIBLE if expected is None else expected, (hex(video), hex(view_flags), x, y, rect))
            checked += 1
        self.assertEqual(checked, 3 * 3 * 6 * 6 * 2)
        # The case the design's two-gate reading got wrong: outside the rect with 0x8000000 set is 0, not 1.
        self.assertEqual(self.decide(view_flags=0x8000000, x=0x9000), VISIBLE)
        self.assertEqual(self.decide(view_flags=0x8000000, x=0), HIDDEN)
        self.assertEqual(self.decide(video=0, view_flags=0, x=0x9000), HIDDEN)

    def test_readiness_needs_one_stable_record_and_a_pass(self):
        ready = lambda *script: self.run_driver('ready', *script)
        self.assertEqual(ready('b', 'p5')['answers'], '0')                                   # no previous frame
        self.assertEqual(ready('b', 'p5', 'k', 'b', 'p5')['answers'], '01')
        self.assertEqual(ready('b', 'p5', 'b', 'p5')['answers'], '00')                       # the pass did not run
        self.assertEqual(ready('b', 'p5', 'k', 'b', 'p6')['answers'], '00')                  # another record
        self.assertEqual(ready('b', 'p5', 'k', 'b', 'p5', 'p6', 'k', 'b', 'p5')['answers'], '0100')  # a second sun: vanilla, and the next frame too
        self.assertEqual(ready('b', 'p5', 'p6', 'k', 'b', 'p5', 'k', 'b', 'p5')['answers'], '0001')
        self.assertEqual(ready('b', 'p5', 'k', 'b', 'x', 'p5')['answers'], '00')             # an untransformable lens draw was seen
        self.assertEqual(ready('b', 'p5', 'k', 'b', 'x', 'p5', 'k', 'r', 'b', 'p5', 'k', 'b', 'p5')['answers'], '0000')  # and a device Reset does not lift the block
        self.assertEqual(ready('b', 'p5', 'k', 'b', 'd5')['answers'], '00')                  # feature off (log only)
        self.assertEqual(ready('b', 'p5', 'k', 'r', 'b', 'p5')['answers'], '00')             # device Reset
        self.assertEqual(ready('b', 'p5', 'p5')['single'], '1')                              # the same record twice is one record
        self.assertEqual(ready('b', 'p5', 'p6')['single'], '0')

    def test_a_transient_skip_holds_the_fraction_for_at_most_four_frames(self):
        usable = lambda *script: self.run_driver('hold', *script)['usable']
        self.assertEqual(usable('r', 's', 's', 's', 's', 's', 's'), '1111100')   # four held, then dropped until a pass runs again
        self.assertEqual(usable('r', 's', 's', 'r', 's', 's', 's', 's', 's'), '111111110')
        self.assertEqual(usable('r', 'f', 's'), '100')                           # a failed pass is never held
        self.assertEqual(usable('s', 's', 's', 's', 's'), '11110')

    def test_footprint(self):
        f = self.run_driver('footprint', 16384, -8192, 655, 200, 0x2aaa, 0x10000, 1920, 1080, 1)
        self.assertEqual((f['valid'], f['known']), ('1', '1'))
        self.assertAlmostEqual(float(f['u']), .75, places=6)
        self.assertAlmostEqual(float(f['v']), .625, places=6)       # y up in the record, v down on screen
        expected = 655 / 65536 / math.tan(math.radians(30)) / 2
        self.assertAlmostEqual(float(f['ru']), expected, delta=expected * 1e-3)
        self.assertAlmostEqual(float(f['rv']), expected * 1920 / 1080, delta=expected * 2e-3)
        half = self.run_driver('footprint', 0, 0, 328, 100, 0x2aaa, 0x10000, 1920, 1080, 1)   # accumulator 100: half the size, same disc
        self.assertAlmostEqual(float(half['ru']), expected, delta=expected * 5e-3)
        first = self.run_driver('footprint', 0, 0, 0, 0, 0x2aaa, 0x10000, 1920, 1080, 1)       # a record's first frame
        self.assertEqual((first['valid'], first['known']), ('1', '0'))
        tiny = self.run_driver('footprint', 0, 0, 1, 200, 0x2aaa, 0x10000, 1920, 1080, 1)
        self.assertAlmostEqual(float(tiny['ru']), 1.5 / 1920, places=6)                        # floor: 1.5 px
        self.assertEqual(self.run_driver('footprint', 0, 0, 655, 200, 0x2aaa, 0, 1920, 1080, 1)['known'], '0')  # no view scale
        self.assertAlmostEqual(float(self.run_driver('alpha', 0.016)['alpha']), 1 - math.exp(-0.016 / 0.08), places=6)
        self.assertAlmostEqual(float(self.run_driver('alpha', 5)['alpha']), 1 - math.exp(-0.25 / 0.08), places=6)
        self.assertEqual(float(self.run_driver('alpha', 0)['alpha']), 0)

    def test_blend_classification(self):
        ONE, INVSRCCOLOR, SRCALPHA, INVSRCALPHA, ZERO = 2, 4, 5, 6, 1
        scale = lambda *s: int(self.run_driver('blend', *s)['scale'])
        REFUSE, RGB, ALPHA, BOTH = 0, 1, 2, 3
        self.assertEqual(scale(1, SRCALPHA, INVSRCALPHA, 1, 0, 0, 0, 0, 0), ALPHA)
        self.assertEqual(scale(1, SRCALPHA, ONE, 1, 0, 0, 0, 0, 0), ALPHA)
        self.assertEqual(scale(1, ONE, ONE, 1, 0, 0, 0, 0, 0), RGB)
        self.assertEqual(scale(1, ONE, INVSRCCOLOR, 1, 0, 0, 0, 0, 0), RGB)
        self.assertEqual(scale(1, ONE, INVSRCALPHA, 1, 0, 0, 0, 0, 0), BOTH)
        self.assertEqual(scale(0, SRCALPHA, INVSRCALPHA, 1, 0, 0, 0, 0, 0), REFUSE)   # opaque
        self.assertEqual(scale(1, SRCALPHA, INVSRCALPHA, 3, 0, 0, 0, 0, 0), REFUSE)   # SUBTRACT
        self.assertEqual(scale(1, SRCALPHA, INVSRCALPHA, 1, 1, 0, 0, 0, 0), REFUSE)   # sRGB write
        self.assertEqual(scale(1, ZERO, ONE, 1, 0, 0, 0, 0, 0), REFUSE)
        self.assertEqual(scale(1, SRCALPHA, ZERO, 1, 0, 0, 0, 0, 0), REFUSE)
        self.assertEqual(scale(1, ONE, ONE, 1, 0, 1, 200, 5, 0), RGB)                 # rgb scaling never crosses an alpha test
        self.assertEqual(scale(1, SRCALPHA, ONE, 1, 0, 1, 8, 5, 0), ALPHA)
        self.assertEqual(scale(1, SRCALPHA, ONE, 1, 0, 1, 9, 5, 0), REFUSE)
        self.assertEqual(scale(1, SRCALPHA, ONE, 1, 0, 1, 0, 2, 0), REFUSE)           # LESS keeps low alpha
        self.assertEqual(scale(1, SRCALPHA, ONE, 1, 0, 1, 200, 8, 0), ALPHA)          # ALWAYS
        self.assertEqual(scale(1, SRCALPHA, ONE, 1, 0, 0, 0, 0, 1), REFUSE)           # fog is applied after the pixel shader
        self.assertEqual(scale(1, ONE, ONE, 1, 0, 0, 0, 0, 1), REFUSE)

    def test_main_view_walk(self):
        view = lambda case: int(self.run_driver('view', case)['view'], 16)
        self.assertEqual(view('ok'), 0x6000)
        for case in ('no_marker', 'no_handle', 'bad_count', 'missing', 'cycle', 'misaligned', 'unreadable'):
            self.assertEqual(view(case), 0, case)

    PS20 = ['ffff0200', '0002fffe', '11111111', '22222222',            # a leading comment stays in front of the def
            '0200001f', '80000000', 'b0030000', '0200001f', '90000000', 'a00f0800',
            '03000042', '800f0000', 'b0e40000', 'a0e40800', '02000001', '800f0800', '80e40000', '0000ffff']

    def variant(self, scale, words):
        out = self.run_driver('variant', scale, *words)
        return out['result'], out, out['words'].split(',')

    def test_wrap_of_a_ps_2_0_program(self):
        result, layout, words = self.variant(2, self.PS20)
        self.assertEqual(result, 'applied')
        self.assertEqual((layout['sampler'], layout['constant'], layout['output'], layout['fetch']), ('15', '31', '11', '10'))
        self.assertEqual(words[:4], self.PS20[:4])
        self.assertEqual(words[4:13], ['05000051', 'a00f001f', '3f000000', '3f000000', '00000000', '3f800000', '0200001f', '90000000', 'a00f080f'])
        body = self.PS20[4:-1]
        body[-2] = '800f000b'                                        # mov oC0, r0 -> mov r11, r0
        self.assertEqual(words[13:13 + len(body)], body)
        self.assertEqual(words[13 + len(body):], ['02000001', '800f000a', 'a0e4001f', '03000042', '800f000a', '80e4000a', 'a0e4080f',
                                                  '03000005', '8008000b', '80e4000b', '8055000a', '02000001', '800f0800', '80e4000b', '0000ffff'])
        self.assertEqual(self.variant(1, self.PS20)[2][-7], '8007000b')   # rgb
        self.assertEqual(self.variant(3, self.PS20)[2][-7], '800f000b')   # premultiplied: both

    def test_wrap_refusals_leave_the_output_untouched(self):
        def refused(words, scale=2):
            result, _, out = self.variant(scale, words)
            self.assertEqual(out, ['deadbeef'], result)
            return result
        self.assertEqual(refused(['ffff0101', '0000ffff']), 'unsupported_version')
        self.assertEqual(refused(['fffe0300', '0000ffff']), 'unsupported_version')
        self.assertEqual(refused(self.PS20, 0), 'invalid_input')
        self.assertEqual(refused(self.PS20[:-1]), 'invalid_input')                       # no END
        self.assertEqual(refused(self.PS20 + ['00000000']), 'invalid_input')             # words after END
        self.assertEqual(refused(self.PS20[:-4] + ['0000ffff']), 'no_output')
        partial = list(self.PS20); partial[-3] = '80070800'                               # only oC0.xyz
        self.assertEqual(refused(partial), 'no_output')
        relative = list(self.PS20); relative[-2] = '80e42000'
        self.assertEqual(refused(relative), 'unsupported_shader')
        predicated = list(self.PS20); predicated[-4] = '12000001'
        self.assertEqual(refused(predicated), 'unsupported_shader')
        self.assertEqual(refused(self.PS20[:4] + ['01000019', 'a0e41000'] + self.PS20[4:]), 'unsupported_shader')  # call l0
        samplers = sum((['0200001f', '90000000', 'a00f08%02x' % i] for i in range(1, 16)), [])
        self.assertEqual(refused(self.PS20[:10] + samplers + self.PS20[10:]), 'resource_limit')
        temporaries = sum((['02000001', '800f00%02x' % i, '80e40000'] for i in range(1, 11)), [])
        self.assertEqual(refused(self.PS20[:14] + temporaries + self.PS20[14:]), 'resource_limit')  # r0..r10 named: one free, two needed

    def test_wrap_of_a_ps_3_0_program_with_split_and_conditional_writes(self):
        head = ['ffff0300', '0200001f', '80000005', '900f0000']                           # dcl_texcoord v0
        split = head + ['02000001', '80070800', '90e40000', '02000001', '80180800', '90ff0000', '0000ffff']  # oC0.xyz, then oC0.w with _sat
        result, layout, words = self.variant(2, split)
        self.assertEqual(result, 'applied')
        self.assertEqual((layout['sampler'], layout['constant'], layout['output'], layout['fetch']), ('15', '223', '31', '30'))
        self.assertIn('8007001f', words); self.assertIn('8018001f', words)               # masks and _sat kept on r31
        self.assertEqual(words[-7:-4], ['8008001f', '80e4001f', '8055001e'])
        inside = head + ['01000028', '90000000', '02000001', '800f0800', '90e40000', '0000002b', '0000ffff']  # if v0.x / mov oC0 / endif
        self.assertEqual(self.variant(2, inside)[0], 'unsupported_shader')
        self.assertEqual(self.variant(2, head + ['0000002b', '02000001', '800f0800', '90e40000', '0000ffff'])[0], 'invalid_input')  # endif without if

    def test_site_constants(self):
        sites = self.run_driver('sites')
        self.assertEqual((sites['probe_site'], sites['probe_target'], sites['lens_site'], sites['lens_target']), ('0x471630', '0x488720', '0x472491', '0x47e6e0'))
        self.assertEqual((sites['gates'], sites['gates_length']), ('0xe5f40888a926996', '148'))


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class SunOcclusionSites(unittest.TestCase):
    def test_installed_executable_matches_every_site_claim(self):
        verifier = load('sun_occlusion_sites', 'verification/probe/verify_sun_occlusion_sites.py')
        if not verifier.DEFAULT_EXE.exists():
            self.skipTest('X3AP.exe is not installed')
        result = verifier.verify(verifier.DEFAULT_EXE)
        self.assertTrue(result['passed'], result['checks'])
        self.assertEqual(len(result['checks']), 16)
        self.assertEqual(result['conflicts'], ['submit_phase_sort_return_b'])


class SunOcclusionLaunchOption(unittest.TestCase):
    ROUTE = ('--ownership', '--object-trace', '--object-lifetime', '--motion-output', '--taa', '--hdr')
    NAMES = ('X3M_SUN_OCCLUSION', 'X3M_SUN_OCCLUSION_LOG', 'X3M_SUN_OCCLUSION_RADIUS', 'X3M_SUN_OCCLUSION_CURVE')

    def launch(self, directory, *args, inherited=None):
        module = load('sun_occlusion_manage', 'tools/manage.py')
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'fixture')   # a modded dry run checks the installed DLL against its manifest
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'fixture').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def test_default_is_off_and_drops_inherited_variables(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = self.launch(directory, *self.ROUTE, inherited={name: '1' for name in self.NAMES})
            self.assertEqual(code, 0, error)
            self.assertFalse(set(self.NAMES) & set(json.loads(output)['env']))

    def test_dry_run_carries_the_options(self):
        with tempfile.TemporaryDirectory() as directory:
            baseline = json.loads(self.launch(directory, *self.ROUTE)[1])
            code, output, error = self.launch(directory, *self.ROUTE, '--sun-occlusion', '--sun-occlusion-log', '--sun-occlusion-radius', '1.5', '--sun-occlusion-curve', '2')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']},
                             {'X3M_SUN_OCCLUSION': '1', 'X3M_SUN_OCCLUSION_LOG': '1', 'X3M_SUN_OCCLUSION_RADIUS': '1.5000', 'X3M_SUN_OCCLUSION_CURVE': '2.0000'})
            code, output, error = self.launch(directory, '--vanilla', '--sun-occlusion-log')   # observe-only needs no route
            self.assertEqual(code, 0, error)
            self.assertEqual({k for k in json.loads(output)['env'] if k in self.NAMES}, {'X3M_SUN_OCCLUSION_LOG'})

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, needle in ((('--sun-occlusion',), '--motion-output'), ((*self.ROUTE, '--sun-occlusion-radius', '2'), 'require --sun-occlusion'),
                                 ((*self.ROUTE, '--sun-occlusion', '--sun-occlusion-radius', '9'), 'out of range'), ((*self.ROUTE, '--sun-occlusion', '--sun-occlusion-curve', '0.1'), 'out of range'),
                                 ((*self.ROUTE, '--telemetry', '--frame-phases', '--submit-phases', '--sun-occlusion'), '--submit-phases')):
                code, _, error = self.launch(directory, *args)
                self.assertNotEqual(code, 0, args)
                self.assertIn(needle, error, args)


if __name__ == '__main__':
    unittest.main()
