"""Host checks of the partial-sun-occlusion pure parts (docs/architecture/sun-partial-occlusion.md).

The probe override's decision table against a Python restatement of the vanilla prologue of
0x00488720 (three tests, in order, RE note section 16), the eligibility guard (the main view's
re-probe of a background-view-owned record, run223) and the readiness fallback; the readiness state
machine; record -> footprint with the sun's saturated size; the body classification from the clip
rows; the blend classification; the main-view registry walk over a fake image; the structural
pixel wrap over hand-assembled ps_2_0 / ps_3_0 programs and the step-2 vertex / clip wraps. The C++ driver (verification/probe/sun_occlusion_host.cpp) uses the
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
import struct
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

    # Defaults: the main view (0x100) re-probes a record owned by the background view (0x200, +0x270 & 0x400000).
    def decide(self, ready=1, owner=0x200, view=0x100, main=0x100, owner_flags=0x400135, video=0x8000, view_flags=0, x=0, y=0, rect=FULL):
        return int(self.run_driver('decide', ready, owner, view, main, owner_flags, video, view_flags, x, y, *rect)['decision'])

    def test_eligibility_and_readiness_fall_back_to_the_original(self):
        self.assertEqual(self.decide(), VISIBLE)
        self.assertEqual(self.decide(ready=0), ORIGINAL)
        self.assertEqual(self.decide(owner=0x100), ORIGINAL)            # a record the main view owns (ship flares, group 25): run223
        self.assertEqual(self.decide(view=0x200), ORIGINAL)             # the owner's own probe (layer 15)
        self.assertEqual(self.decide(view=0x300), ORIGINAL)             # a later view's re-probe
        self.assertEqual(self.decide(owner_flags=0x135), ORIGINAL)      # a foreign record whose owner is not a background-regime view (a monitor)
        self.assertEqual(self.decide(owner_flags=0), ORIGINAL)          # owner flags unreadable
        self.assertEqual(self.decide(owner=0), ORIGINAL)
        self.assertEqual(self.decide(main=0), ORIGINAL)                 # main view unknown
        # The guard precedes the gates: a foreign view is the original's even with the flare option off.
        self.assertEqual(self.decide(view=0x300, video=0), ORIGINAL)
        self.assertEqual(self.decide(owner=0x100, video=0), ORIGINAL)
        # run223's measured case: owner 334fe888 layer 15 flags 0x00400135, main 334fe0f0 flags 0x0085492d.
        self.assertEqual(self.decide(owner=0x334fe888, view=0x334fe0f0, main=0x334fe0f0, owner_flags=0x00400135, view_flags=0x0085492d, x=-2802, y=9663), VISIBLE)

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
        self.assertEqual((f['valid'], f['known'], f['saturated']), ('1', '1', '0'))
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

    def body(self, rows, sun=(.4649, .3529), known=1, aspect=1280 / 768):
        out = self.run_driver('body', known, sun[0], sun[1], aspect, *rows)
        return int(out['body']), float(out['u']), float(out['v']), float(out['distance'])

    @staticmethod
    def rows_for(u, v, w=1.0):
        # dp4 oPos.x = r . c0 ... with the origin (0, 0, 0, 1): clip = (c0.w, c1.w, c2.w, c3.w).
        return [1, 0, 0, (u - .5) * 2 * w, 0, 1, 0, -(v - .5) * 2 * w, 0, 0, 1, .5 * w, 0, 0, 0, w]

    def test_body_classification(self):
        UNKNOWN, CORE, GHOST, OTHER = 0, 1, 2, 3
        sun = (.4649, .3529)
        self.assertEqual(self.body(self.rows_for(*sun))[0], CORE)
        self.assertEqual(self.body(self.rows_for(*sun, w=7.5))[0], CORE)                       # any w: the centre is a ratio
        body, u, v, d = self.body(self.rows_for(sun[0] + 3 / 1280, sun[1]))
        self.assertEqual(body, CORE); self.assertAlmostEqual(u, sun[0] + 3 / 1280, places=5); self.assertAlmostEqual(d, 3 / 1280, places=5)
        self.assertEqual(self.body(self.rows_for(sun[0] + 8 / 1280, sun[1]))[0], OTHER)        # past the tolerance (6.4 px), off the sun-centre line
        # Ghosts lie on the line through the sun and the screen centre: factor 0.3, -0.25, 1.6.
        for factor in (.3, -.25, 1.6):
            g = (.5 + (sun[0] - .5) * factor, .5 + (sun[1] - .5) * factor)
            self.assertEqual(self.body(self.rows_for(*g))[0], GHOST, factor)
        # A ship flare 20 px off the line: another record.
        self.assertEqual(self.body(self.rows_for(.5 + (sun[0] - .5) * .3 + 20 / 1280, .5 + (sun[1] - .5) * .3))[0], OTHER)
        self.assertEqual(self.body(self.rows_for(.9, .8))[0], OTHER)
        # Unknown: rows not known, the centre behind the camera, or not finite.
        self.assertEqual(self.body(self.rows_for(*sun), known=0)[0], UNKNOWN)
        self.assertEqual(self.body(self.rows_for(*sun, w=-1.0))[0], UNKNOWN)
        self.assertEqual(self.body(self.rows_for(*sun, w=0.0))[0], UNKNOWN)
        self.assertEqual(self.body(['nan'] + self.rows_for(*sun)[1:])[0], CORE)                # an unused element does not matter
        self.assertEqual(self.body(self.rows_for(*sun)[:3] + ['nan'] + self.rows_for(*sun)[4:])[0], UNKNOWN)
        # The sun on the screen centre: the core is classed by distance, nothing is a ghost.
        self.assertEqual(self.body(self.rows_for(.5, .5), sun=(.5, .5))[0], CORE)
        self.assertEqual(self.body(self.rows_for(.6, .6), sun=(.5, .5))[0], OTHER)

    # The lens scene's vertex program shape (vs_2_0, run223 fingerprint d5e1c753...): the origin (v0, 1), four dp4 of
    # r0 with c0..c3 in the order w, x, y, z, an oT0 and an oD0 output.
    VS20 = ['fffe0200', '0200001f', '80000000', '900f0000', '0200001f', '80000005', '900f0001',
            '02000001', '800f0000', '90e40000',                          # mov r0, v0
            '03000009', 'c0080000', '80e40000', 'a0e40003',              # dp4 oPos.w, r0, c3
            '03000009', 'c0010000', '80e40000', 'a0e40000',              # dp4 oPos.x, r0, c0
            '03000009', 'c0020000', '80e40000', 'a0e40001',              # dp4 oPos.y, r0, c1
            '03000009', 'c0040000', '80e40000', 'a0e40002',              # dp4 oPos.z, r0, c2
            '02000001', 'e0030000', '90e40001',                          # mov oT0.xy, v1
            '02000001', 'd00f0000', '90e40001',                          # mov oD0, v1
            '0000ffff']

    def test_free_texcoord_and_vertex_wrap(self):
        self.assertEqual(self.run_driver('texcoord', *self.VS20, '--', *self.PS20)['texcoord'], '7')
        ps_t7 = self.PS20[:7] + ['0200001f', '80000000', 'b00f0007'] + self.PS20[7:]                # the pixel side declares t7
        self.assertEqual(self.run_driver('texcoord', *self.VS20, '--', *ps_t7)['texcoord'], '6')
        vs_t7 = self.VS20[:-4] + ['02000001', 'e00f0007', '90e40001'] + self.VS20[-4:]              # the vertex side writes oT7
        self.assertEqual(self.run_driver('texcoord', *vs_t7, '--', *self.PS20)['texcoord'], '6')
        self.assertEqual(self.run_driver('texcoord', *(['fffe0300'] + self.VS20[1:]), '--', *self.PS20)['texcoord'], '8')  # vs_3_0: not this pair
        out = self.run_driver('vsvariant', 7, *self.VS20)
        words = out['words'].split(',')
        self.assertEqual((out['result'], out['matrix'], out['origin']), ('applied', '0', '1'))
        self.assertEqual(words[:10], self.VS20[:10])
        self.assertEqual(words[10:26], ['03000009', '8008000b', '80e40000', 'a0e40003', '03000009', '8001000b', '80e40000', 'a0e40000',
                                        '03000009', '8002000b', '80e40000', 'a0e40001', '03000009', '8004000b', '80e40000', 'a0e40002'])  # oPos -> r11
        self.assertEqual(words[26:32], self.VS20[26:32])
        self.assertEqual(words[-7:], ['02000001', 'c00f0000', '80e4000b', '02000001', 'e00f0007', '80e4000b', '0000ffff'])
        shifted = list(self.VS20)
        for i in (13, 17, 21, 25): shifted[i] = 'a0e4%04x' % (int(shifted[i][-4:], 16) + 24)      # c24..c27
        self.assertEqual(self.run_driver('vsvariant', 7, *shifted)['matrix'], '24')

    # The fingerprinted program's position source: def c14, 1, 0, 0, 0 / mad r0, v0.xyzx, c14.xxxy, c14.yyyx = (v0.xyz, 1).
    VS20_MAD = ['fffe0200', '05000051', 'a00f000e', '3f800000', '00000000', '00000000', '00000000',
                '0200001f', '80000000', '900f0000', '0200001f', '80000005', '900f0001',
                '04000004', '800f0000', '90240000', 'a040000e', 'a015000e',
                '03000009', 'c0080000', '80e40000', 'a0e40003', '03000009', 'c0010000', '80e40000', 'a0e40000',
                '04000004', '80070001', '90c40001', 'a0d0000e', 'a0c5000e',                                 # mad r1.xyz (another temporary) between the dp4s
                '03000009', 'c0020000', '80e40000', 'a0e40001', '03000009', 'c0040000', '80e40000', 'a0e40002',
                '02000001', 'e0030000', '90e40001', '0000ffff']

    def test_vertex_origin_validation(self):
        origin = lambda words: (self.run_driver('vsvariant', 7, *words)['result'], self.run_driver('vsvariant', 7, *words)['origin'])
        self.assertEqual(origin(self.VS20_MAD), ('applied', '1'))
        self.assertEqual(origin(self.VS20), ('applied', '1'))                                             # mov r0, v0 (a position stream carries w = 1)
        # The origin is not (v0.xyz, 1): the wrap still applies (the body is classified "other"), origin = 0.
        c14 = list(self.VS20_MAD); c14[3] = '40000000'                                                    # c14.x = 2: mul = (2, 2, 2, 0)
        self.assertEqual(origin(c14), ('applied', '0'))
        swz = list(self.VS20_MAD); swz[15] = '90e40000'                                                   # mad r0, v0.xyzw, ...: the w lane is still 0 * v0.w + 1
        self.assertEqual(origin(swz), ('applied', '1'))
        swapped = list(self.VS20_MAD); swapped[15] = '90210000'                                           # v0.yxzx: x and y exchanged
        self.assertEqual(origin(swapped), ('applied', '0'))
        other_input = list(self.VS20); other_input[9] = '90e40001'                                        # mov r0, v1 (not the position)
        self.assertEqual(origin(other_input), ('applied', '0'))
        scaled = self.VS20[:10] + ['03000005', '800f0000', '80e40000', 'a0e40004'] + self.VS20[10:]        # mul r0, r0, c4 after the mov: the last write is not the shape
        self.assertEqual(origin(scaled), ('applied', '0'))
        between = self.VS20[:14] + ['02000001', '80010000', 'a0000004'] + self.VS20[14:]                  # r0.x rewritten between the dp4s
        self.assertEqual(origin(between), ('applied', '0'))
        after = self.VS20[:26] + ['02000001', '800f0000', 'a0e40004'] + self.VS20[26:]                    # r0 rewritten after the last dp4: fine
        self.assertEqual(origin(after), ('applied', '1'))
        partial = self.VS20[:7] + ['02000001', '80070000', '90e40000'] + self.VS20[10:]                   # mov r0.xyz, v0 (w unset)
        self.assertEqual(origin(partial), ('applied', '0'))
        no_dcl = self.VS20[:1] + self.VS20[4:]                                                            # no dcl_position: the input is unknown
        self.assertEqual(origin(no_dcl), ('applied', '0'))

    def test_vertex_wrap_refusals(self):
        def refused(words, texcoord=7):
            out = self.run_driver('vsvariant', texcoord, *words)
            self.assertEqual(out['words'], 'deadbeef', out['result'])
            return out['result']
        self.assertEqual(refused(['fffe0300'] + self.VS20[1:]), 'unsupported_version')
        self.assertEqual(refused(self.VS20[:10] + self.VS20[14:]), 'no_output')                                       # three lanes only
        mixed = list(self.VS20); mixed[13] = 'a0e40004'                                                                # c3 -> c4: not K + lane
        self.assertEqual(refused(mixed), 'unsupported_shader')
        other = list(self.VS20); other[12] = '80e40001'                                                                # a different source temporary
        self.assertEqual(refused(other), 'unsupported_shader')
        mad = list(self.VS20); mad[10:14] = ['04000004', 'c0080000', '80e40000', 'a0e40003', 'a0e40004']              # oPos.w by mad
        self.assertEqual(refused(mad), 'unsupported_shader')
        inside = self.VS20[:10] + ['01000028', 'e0e40800'] + self.VS20[10:14] + ['0000002b'] + self.VS20[14:]          # if b0 around a dp4
        self.assertEqual(refused(inside), 'unsupported_shader')
        vs_t7 = self.VS20[:-4] + ['02000001', 'e00f0007', '90e40001'] + self.VS20[-4:]
        self.assertEqual(refused(vs_t7), 'resource_limit')
        self.assertEqual(refused(self.VS20, 8), 'invalid_input')
        temporaries = sum((['02000001', '800f00%02x' % i, '90e40000'] for i in range(1, 12)), [])
        self.assertEqual(refused(self.VS20[:10] + temporaries + self.VS20[10:]), 'resource_limit')

    def test_clip_wrap_of_a_ps_2_0_program(self):
        out = self.run_driver('clipvariant', 1, 7, 1.5 / 1280, 1.5 / 768, 0, *self.PS20)
        words = out['words'].split(',')
        self.assertEqual(out['result'], 'applied')
        self.assertEqual((out['sampler'], out['depth_sampler'], out['constant'], out['output'], out['fetch']), ('15', '14', '31', '11', '10'))
        self.assertEqual(words[:4], self.PS20[:4])
        head = words[4:37]
        le = lambda w: struct.unpack('<f', bytes.fromhex(w[6:8] + w[4:6] + w[2:4] + w[0:2]))[0]
        f32 = lambda v: struct.unpack('<f', struct.pack('<f', v))[0]
        self.assertEqual(head[:9], ['05000051', 'a00f001f', '3f000000', '3f000000', '00000000', '3de38e39', '0200001f', '90000000', 'a00f080f'])  # cK.w = 1/9, the tap weight
        self.assertEqual(head[9:13], ['05000051', 'a00f001e', '3f000000', 'bf000000'])
        self.assertEqual([le(head[13]), le(head[14])], [f32(.5 + .75 / 1280), f32(.5 + .75 / 768)])                                  # + half a texel: taps on texel centres
        self.assertEqual(head[15:17], ['05000051', 'a00f001d']); self.assertEqual([le(w) for w in head[17:21]], [f32(3 / 1280), f32(1.5 / 768), f32(1.5 / 1280), f32(3 / 768)])  # cD = 2dx, dy, dx, 2dy
        self.assertEqual(head[21:23], ['05000051', 'a00f001c']); self.assertEqual([le(w) for w in head[23:27]], [f32(-3 / 1280), f32(1.5 / 768), f32(-1.5 / 1280), f32(3 / 768)])  # cW = -2dx, dy, -dx, 2dy
        self.assertEqual(head[27:33], ['0200001f', '90000000', 'a00f080e', '0200001f', '80000000', 'b00f0007'])
        body = self.PS20[4:-1]; body[-2] = '800f000b'
        self.assertEqual(words[37:37 + len(body)], body)
        tail = words[37 + len(body):]
        self.assertEqual(tail[:7], ['02000001', '800f000a', 'a0e4001f', '03000042', '800f000a', '80e4000a', 'a0e4080f'])          # f
        self.assertEqual(tail[7:19], ['02000006', '80080009', 'b0ff0007', '03000005', '800f0009', 'b0e40007', '80ff0009',
                                      '04000004', '800f0009', '80e40009', 'a0a4001e', 'a0ae001e'])                                 # uv = t.xy / t.w * cC.xyzz + cC.zwzz
        self.assertEqual(tail[19:28], ['03000042', '800f0007', '80e40009', 'a0e4080e', '04000058', '800f0008', '80000007', 'a0aa001f', 'a0ff001f'])  # centre tap: rS = 1/9 where .r < 0
        # The eight knight moves, every one from the fragment's uv: +-(+2,+1) of cD.xy, +-(-2,+1) of cW.xy, +-(+1,+2) of cD.zw, +-(-1,+2) of cW.zw.
        offsets = ['a044001d', 'a144001d', 'a044001c', 'a144001c', 'a0ee001d', 'a1ee001d', 'a0ee001c', 'a1ee001c']
        for i, offset in enumerate(offsets):
            want = ['03000002', '800f0006', '80e40009', offset, '03000042', '800f0007', '80e40006', 'a0e4080e',
                    '04000058', '800f0007', '80000007', 'a0aa001f', 'a0ff001f', '03000002', '800f0008', '80e40008', '80e40007']
            self.assertEqual(tail[28 + 17 * i:28 + 17 * (i + 1)], want, offset)
        self.assertEqual(tail[28 + 136:], ['02000001', '8002000a', '80000008', '03000005', '8007000b', '80e4000b', '8055000a', '02000001', '800f0800', '80e4000b', '0000ffff'])  # rT.y = open_px
        with_f = self.run_driver('clipvariant', 1, 7, 1.5 / 1280, 1.5 / 768, 1, *self.PS20)['words'].split(',')
        self.assertEqual(with_f[-12:-8], ['03000005', '8002000a', '8055000a', '80000008'])                                          # core_f: rT.y = f * open_px
        # The kernel: a one-texel shift of a silhouette moves no pixel by more than 2/9 along an axis or a diagonal (the former cross: 5/9 / 2/9).
        taps = {(0, 0): 1 / 9, **{(sx * a, sy * b): 1 / 9 for a, b in ((2, 1), (1, 2)) for sx in (1, -1) for sy in (1, -1)}}
        self.assertAlmostEqual(sum(taps.values()), 1.)
        for key in (lambda p: p[0], lambda p: p[1], lambda p: p[0] + p[1], lambda p: p[0] - p[1]):
            lines = {}
            for p_, w in taps.items(): lines[key(p_)] = lines.get(key(p_), 0) + w
            self.assertLessEqual(max(lines.values()), 2 / 9 + 1e-9)
        profile = [sum(w for (dx, _), w in taps.items() if x + dx < 0) for x in range(-3, 3)]
        self.assertEqual([round(v, 5) for v in profile], [round(v, 5) for v in (1., 7 / 9, 5 / 9, 4 / 9, 2 / 9, 0.)])
        # ps_2_0 budget of the wrapped lens program: at most 64 arithmetic and 32 texture instructions, 12 temporaries.
        arithmetic = sum(1 for w in words[4:] if w[:2] in ('02', '03', '04') and w[-2:] not in ('42', '51', '1f') and w != '0000ffff')
        texture = sum(1 for w in words[4:] if w == '03000042')
        self.assertEqual(arithmetic, 33); self.assertEqual(texture, 11)  # 32 added: 40 for the lens scene's 8-arithmetic program
        self.assertEqual(self.run_driver('clipvariant', 2, 7, 1.5 / 1280, 1.5 / 768, 0, *self.PS20)['words'].split(',')[-7], '8008000b')
        self.assertEqual(self.run_driver('clipvariant', 3, 7, 1.5 / 1280, 1.5 / 768, 0, *self.PS20)['words'].split(',')[-7], '800f000b')

    def test_clip_wrap_refusals(self):
        def refused(words, texcoord=7, dx=1.5 / 1280, dy=1.5 / 768, scale=1):
            out = self.run_driver('clipvariant', scale, texcoord, dx, dy, 0, *words)
            self.assertEqual(out['words'], 'deadbeef', out['result'])
            return out['result']
        ps30 = ['ffff0300', '0200001f', '80000005', '900f0000', '02000001', '800f0800', '90e40000', '0000ffff']
        self.assertEqual(refused(ps30), 'unsupported_version')                                       # the clip pair is ps_2_x
        self.assertEqual(refused(self.PS20, texcoord=8), 'resource_limit')
        ps_t7 = self.PS20[:7] + ['0200001f', '80000000', 'b00f0007'] + self.PS20[7:]
        self.assertEqual(refused(ps_t7), 'resource_limit')                                           # t7 is the program's
        self.assertEqual(refused(self.PS20, dx=0), 'invalid_input')
        self.assertEqual(refused(self.PS20, dy=.5), 'invalid_input')
        samplers = sum((['0200001f', '90000000', 'a00f08%02x' % i] for i in range(1, 15)), [])
        self.assertEqual(refused(self.PS20[:10] + samplers + self.PS20[10:]), 'resource_limit')     # one free sampler, two needed
        constants = sum((['05000051', 'a00f00%02x' % i, '00000000', '00000000', '00000000', '00000000'] for i in range(0, 29)), [])
        self.assertEqual(refused(self.PS20[:10] + constants + self.PS20[10:]), 'resource_limit')    # c0..c28 named: three free, four needed
        self.assertEqual(self.run_driver('clipvariant', 1, 7, 1.5 / 1280, 1.5 / 768, 0, *(self.PS20[:10] + constants[:-6] + self.PS20[10:]))['result'], 'applied')  # c0..c27: exactly four (c28..c31)
        temporaries = sum((['02000001', '800f00%02x' % i, '80e40000'] for i in range(1, 7)), [])
        self.assertEqual(refused(self.PS20[:14] + temporaries + self.PS20[14:]), 'resource_limit')  # r0..r6 named: five free, six needed
        self.assertEqual(self.run_driver('clipvariant', 1, 7, 1.5 / 1280, 1.5 / 768, 0, *(self.PS20[:14] + temporaries[:-3] + self.PS20[14:]))['result'], 'applied')  # r0..r5: exactly six
        self.assertEqual(refused(['ffff0101', '0000ffff']), 'unsupported_version')
        # The counted ps_2_0 budget (native D3D9 enforces 64 arithmetic / 32 texture slots, wined3d does not): the clip wrap adds 32 + 10.
        mov = ['02000001', '800f0000', '80e40000']; texld = ['03000042', '800f0000', 'b0e40000', 'a0e40800']; pow_ = ['03000020', '800f0000', '80000000', '80550000']
        pad = lambda extra: self.PS20[:14] + extra + self.PS20[14:]                                   # after the program's own texld (1 arithmetic, 1 texture in all)
        applied = lambda words: self.run_driver('clipvariant', 1, 7, 1.5 / 1280, 1.5 / 768, 0, *words)['result']
        self.assertEqual(applied(pad(mov * 31)), 'applied')                                            # 32 + 32 = 64
        self.assertEqual(refused(pad(mov * 32)), 'resource_limit')                                    # 65
        self.assertEqual(applied(pad(mov * 28 + pow_)), 'applied')                                     # pow counts 3: 1 + 28 + 3 = 32
        self.assertEqual(refused(pad(mov * 29 + pow_)), 'resource_limit')                             # 33
        self.assertEqual(applied(pad(texld * 21)), 'applied')                                          # 22 + 10 = 32 texture
        self.assertEqual(refused(pad(texld * 22)), 'resource_limit')                                  # 33
        ghost = lambda words: self.run_driver('variant', 1, *words)['result']                        # the step-1 wrap adds 3 + 1
        self.assertEqual(ghost(pad(mov * 60)), 'applied'); self.assertEqual(ghost(pad(mov * 61)), 'resource_limit')
        self.assertEqual(ghost(pad(texld * 30)), 'applied'); self.assertEqual(ghost(pad(texld * 31)), 'resource_limit')

    def test_visibility_jitter_offset(self):
        # RT2 is on the jittered raster (+jx px right, +jy px down); the pass reads it at the unjittered tap + (jx / W, jy / H). Zero when the jitter is off.
        self.assertEqual(self.run_driver('jitter', 1, .375, -.4375, 1280, 768), {'u': '%.8f' % (.375 / 1280), 'v': '%.8f' % (-.4375 / 768)})
        self.assertEqual(self.run_driver('jitter', 1, -.25, .1667, 320, 180), {'u': '%.8f' % (-.25 / 320), 'v': '%.8f' % (.1667 / 180)})
        self.assertEqual(self.run_driver('jitter', 0, .375, -.4375, 1280, 768), {'u': '0.00000000', 'v': '0.00000000'})
        self.assertEqual(self.run_driver('jitter', 1, .375, -.4375, 0, 768), {'u': '0.00000000', 'v': '0.00000000'})
        # The compiled visibility program takes it on c2 (c0 the disc, c1 the smoothing), and the fixture's phases are the temporal pass's.
        words = [int(w, 16) for w in re.findall(r'0x([0-9a-f]{8})u', (ROOT / 'src/renderer/sun_visibility_program_inc.h').read_text())]
        self.assertEqual(constant_table(words), {'disc': (2, 0), 'control': (2, 1), 'jitter': (2, 2), 'sceneDepth': (3, 0), 'history': (3, 1)})
        fixture = load('run_sun_occlusion', 'verification/probe/run_sun_occlusion.py')
        motion = load('run_motion_output', 'verification/probe/run_motion_output.py')
        self.assertEqual(fixture.expected_phases(), {i: (round(motion.expected_jitter(i)[1], 4), round(motion.expected_jitter(i)[2], 4)) for i in range(8)})
        self.assertEqual(fixture.EXPECTED_GPU_CHECKS, 128)

    def test_site_constants(self):
        sites = self.run_driver('sites')
        self.assertEqual((sites['probe_site'], sites['probe_target'], sites['lens_site'], sites['lens_target']), ('0x471630', '0x488720', '0x472491', '0x47e6e0'))
        self.assertEqual((sites['gates'], sites['gates_length']), ('0xe5f40888a926996', '148'))


def constant_table(words):
    """{name: (register set, register index)} of a D3DX CTAB comment (2 = float constants, 3 = samplers)."""
    at = 1
    while at < len(words):
        token = words[at]
        if token & 0xffff == 0xfffe:
            length = (token >> 16) & 0x7fff
            blob = b''.join(struct.pack('<I', w) for w in words[at + 1:at + 1 + length])
            if blob[:4] == b'CTAB':
                table = blob[4:]
                _, _, _, constants, info, _, _ = struct.unpack('<7I', table[:28])
                out = {}
                for k in range(constants):
                    name, register_set, index, count, _, _, _ = struct.unpack('<IHHHHII', table[info + 20 * k:info + 20 * k + 20])
                    out[table[name:table.index(b'\0', name)].decode()] = (register_set, index)
                return out
            at += 1 + length
        else:
            at += 1 + ((token >> 24) & 15)
    return {}


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
    NAMES = ('X3M_SUN_OCCLUSION', 'X3M_SUN_OCCLUSION_LOG', 'X3M_SUN_OCCLUSION_RADIUS', 'X3M_SUN_OCCLUSION_CURVE', 'X3M_SUN_OCCLUSION_CORE_F')

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
            code, output, error = self.launch(directory, *self.ROUTE, '--sun-occlusion', '--sun-occlusion-log', '--sun-occlusion-radius', '0.05', '--sun-occlusion-curve', '2', '--sun-occlusion-core-f', 'on')
            self.assertEqual(code, 0, error)
            delivered = json.loads(output)
            self.assertEqual({k: v for k, v in delivered['env'].items() if k not in baseline['env']},
                             {'X3M_SUN_OCCLUSION': '1', 'X3M_SUN_OCCLUSION_LOG': '1', 'X3M_SUN_OCCLUSION_RADIUS': '0.0500', 'X3M_SUN_OCCLUSION_CURVE': '2.0000', 'X3M_SUN_OCCLUSION_CORE_F': '1'})
            code, output, error = self.launch(directory, '--vanilla', '--sun-occlusion-log')   # observe-only needs no route
            self.assertEqual(code, 0, error)
            self.assertEqual({k for k in json.loads(output)['env'] if k in self.NAMES}, {'X3M_SUN_OCCLUSION_LOG'})

    def test_core_f_defaults_to_on_and_takes_an_explicit_off(self):
        # run235 acceptance: the clipped core dims with f whenever --sun-occlusion is on; off restores clip-only.
        with tempfile.TemporaryDirectory() as directory:
            for args, want in ((('--sun-occlusion',), '1'), (('--sun-occlusion', '--sun-occlusion-core-f', 'on'), '1'),
                               (('--sun-occlusion', '--sun-occlusion-core-f', 'off'), '0')):
                code, output, error = self.launch(directory, *self.ROUTE, *args, inherited={'X3M_SUN_OCCLUSION_CORE_F': '0' if want == '1' else '1'})
                self.assertEqual(code, 0, error)
                self.assertEqual(json.loads(output)['env']['X3M_SUN_OCCLUSION_CORE_F'], want, args)   # the option wins over any shell value
            code, output, error = self.launch(directory, *self.ROUTE, inherited={'X3M_SUN_OCCLUSION_CORE_F': '1'})   # no override: nothing is patched, nothing is set
            self.assertEqual(code, 0, error)
            self.assertNotIn('X3M_SUN_OCCLUSION_CORE_F', json.loads(output)['env'])

    def test_dll_reads_the_variable_as_default_on(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('bool sun_occlusion_core_f = true;', capture)                                   # unset = on
        read = re.search(r'sun_occlusion_core_f=([^;]*);', capture).group(1)
        self.assertIn("L\"X3M_SUN_OCCLUSION_CORE_F\"", read)
        self.assertIn("value[0]==L'0'", read)                                                         # only an explicit "0" turns it off
        self.assertTrue(read.startswith('!('), read)
        self.assertIn('core_f=%u', capture)                                                           # the config line keeps the field

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, needle in ((('--sun-occlusion',), '--motion-output'), ((*self.ROUTE, '--sun-occlusion-radius', '0.02'), 'require --sun-occlusion'), ((*self.ROUTE, '--sun-occlusion-core-f', 'off'), 'require --sun-occlusion'), ((*self.ROUTE, '--sun-occlusion', '--sun-occlusion-core-f', 'maybe'), 'invalid choice'),
                                 ((*self.ROUTE, '--sun-occlusion', '--sun-occlusion-radius', '1.5'), 'out of range'), ((*self.ROUTE, '--sun-occlusion', '--sun-occlusion-radius', '0.001'), 'out of range'), ((*self.ROUTE, '--sun-occlusion', '--sun-occlusion-curve', '0.1'), 'out of range'),
                                 ((*self.ROUTE, '--telemetry', '--frame-phases', '--submit-phases', '--sun-occlusion'), '--submit-phases')):
                code, _, error = self.launch(directory, *args)
                self.assertNotEqual(code, 0, args)
                self.assertIn(needle, error, args)


if __name__ == '__main__':
    unittest.main()
