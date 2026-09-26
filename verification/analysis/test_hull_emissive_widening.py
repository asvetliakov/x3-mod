"""Host oracle of the hull emissive widening (--hull-emissive-widening K[,B];
docs/architecture/hull-emissive-widening.md 8.3 R1/R2).

Transformer (linear_material_hull_lightmap_gain_pixel_variant / the share entry
point with widen = {K, B}): the gained variant with its light-map
`texld rL, v1, s` replaced by the 28-instruction block (dsx/dsy of v1, the
per-pixel k = clamp(rho, 1, K) from the light map's own texel footprint through
the per-draw lanes c217.yz, the minification gate g = saturate(rho^2 / K - 1),
the widened texldd, the two axis-doubled coarse texldd, the larger axis luma
ratio's gate, the brightness floor b = saturate(32 L_k - 1) and the boost B)
plus three DEFs (c210 = (1/K, 2^-8, B-1, 1/0.15), c211 = (Rec.709 luma, 9),
c203 = (32, 1, 0, 0)), rG, rT = rG + 1 and rU = rG + 2 above the program's
highest temporary, nothing else changed (+131 DWORDs, +27 instructions, +35
weighted slots). The
byte-exact Python reference below rebuilds every widened variant from the
gained one and compares. The coverage test walks the local archive manifest:
every ps_3_0 program declaring LightMapTexSampler is widened or in the explicit
exclusion list (the moon), and the hash -> family -> stage -> transformed table
is written as tracked JSON. Launcher and DLL plumbing are checked by source
contract. No game assets bundled (the corpus is local, X3M_SHADER_PROGRAM_DIRECTORY).
"""
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/probe'))
import inspect_motion_output_profiles as shader
import run_linear_material as runner
from shader_constants import parse_ctab
from verification.analysis.test_capture_bloom_lifetime import extract_function
from verification.analysis.test_original_fill import families, launch
from verification.analysis.test_hull_lightmap_gain import FILLS, UNTOUCHED
from source_text import source_text

DSX, DSY, TEXLDD, TEXLD, MUL, DEF, DCL = 91, 92, 93, 66, 5, 81, 31
ADD, MAD, RCP, RSQ, DP3, MIN, MAX = 2, 4, 6, 7, 8, 10, 11
SAT = 1 << 20
WIDEN_K, WIDEN_B = 3.0, 3.0  # the driver's parameters
WIDEN_CONSTANT, WIDEN_LUMA_CONSTANT, WIDEN_GATE_CONSTANT = 210, 211, 203
PREREQUISITES = ['--motion-output', '--hdr']
COVERAGE = ROOT / 'verification/results/hull-emissive-widening-coverage.json'
# SM3 light-map programs outside the reviewed originals, by design (hull-emissive-widening.md 2.5).
EXCLUDED = {'ps_6aaaa2cb27e92cc8': 'moon (shader/3_0/moon_0000.fb, moon_0001.fb: coordinate v2, no motion row)'}
LIGHTMAP_SAMPLER = 'LightMapTexSampler'
WIDEN_SLOTS, WIDEN_WORDS, WIDEN_INSTRUCTIONS = 35, 131, 27


def swizzle(x, y, z, w):
    return (x | y << 2 | z << 4 | w << 6) << 16


def src(reg, sw=(0, 1, 2, 3)):
    return 0x80000000 | reg | swizzle(*sw)


def dst(reg, mask):
    return 0x80000000 | reg | mask << 16


def f32(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def lane(reg, component):
    return src(reg, (component,) * 4)


def widen_block(destination, coordinate, sampler, g):
    """The 117-word block of hull-emissive-widening.md 8.3 (R1 + R2, axis-separated gate, minification gate, brightness floor) with rG = g, rT = g + 1, rU = g + 2."""
    t, u = g + 1, g + 2
    rl = destination & 0x7ff
    c217 = 0x80000000 | 0x20000000 | 217
    c210 = 0x80000000 | 0x20000000 | WIDEN_CONSTANT
    c211 = 0x80000000 | 0x20000000 | WIDEN_LUMA_CONSTANT
    c203 = 0x80000000 | 0x20000000 | WIDEN_GATE_CONSTANT
    return [2 << 24 | DSX, dst(g, 3), coordinate,
            2 << 24 | DSY, dst(g, 12), (coordinate & ~0xff0000) | swizzle(0, 1, 0, 1),
            3 << 24 | MUL, dst(t, 15), src(g), src(g),
            3 << 24 | MUL, dst(t, 15), src(t), src(c217, (1, 2, 1, 2)),
            3 << 24 | ADD, dst(t, 3), src(t, (0, 2, 0, 0)), src(t, (1, 3, 0, 0)),
            3 << 24 | MAX, dst(t, 1), lane(t, 0), lane(t, 1),
            4 << 24 | MAD, dst(u, 8) | SAT, lane(t, 0), lane(c210, 0), lane(c203, 1) | 1 << 24,
            2 << 24 | RSQ, dst(t, 1), lane(t, 0),
            3 << 24 | MAX, dst(t, 1) | SAT, lane(t, 0), lane(c210, 0),
            2 << 24 | RCP, dst(t, 1), lane(t, 0),
            3 << 24 | MUL, dst(g, 15), src(g), lane(t, 0),
            5 << 24 | TEXLDD, destination, coordinate, sampler, src(g, (0, 1, 1, 1)), src(g, (2, 3, 3, 3)),
            3 << 24 | ADD, dst(t, 15), src(g), src(g),
            5 << 24 | TEXLDD, dst(u, 7), coordinate, sampler, src(t, (0, 1, 1, 1)), src(g, (2, 3, 3, 3)),
            3 << 24 | DP3, dst(u, 1), src(u), src(c211),
            5 << 24 | TEXLDD, dst(t, 7), coordinate, sampler, src(g, (0, 1, 1, 1)), src(t, (2, 3, 3, 3)),
            3 << 24 | DP3, dst(t, 8), src(t), src(c211),
            3 << 24 | MIN, dst(t, 8), lane(t, 3), lane(u, 0),
            3 << 24 | DP3, dst(t, 1), src(rl), src(c211),
            4 << 24 | MAD, dst(t, 2) | SAT, lane(t, 0), lane(c203, 0), lane(c203, 1) | 1 << 24,
            3 << 24 | MAX, dst(t, 8), lane(t, 3), lane(c210, 1),
            2 << 24 | RCP, dst(t, 8), lane(t, 3),
            3 << 24 | MUL, dst(t, 1), lane(t, 0), lane(t, 3),
            4 << 24 | MAD, dst(t, 1) | SAT, lane(t, 0), lane(c210, 3), lane(c211, 3) | 1 << 24,
            3 << 24 | MUL, dst(t, 1), lane(t, 0), lane(t, 1),
            3 << 24 | MUL, dst(t, 1), lane(t, 0), lane(u, 3),
            3 << 24 | MUL, dst(t, 1), lane(t, 0), lane(c210, 2),
            4 << 24 | MAD, dst(rl, 7), src(rl), lane(t, 0), src(rl)]


def widen_definitions(k, b):
    return [5 << 24 | DEF, dst(0x20000000 | WIDEN_CONSTANT, 15), f32(1.0 / k), f32(2.0 ** -8), f32(b - 1.0), f32(1.0 / 0.15),
            5 << 24 | DEF, dst(0x20000000 | WIDEN_LUMA_CONSTANT, 15), f32(0.2126), f32(0.7152), f32(0.0722), f32(9.0),
            5 << 24 | DEF, dst(0x20000000 | WIDEN_GATE_CONSTANT, 15), f32(32.0), f32(1.0), 0, 0]


def expected_dynamic(words):
    """The far-fade form of a static gained variant: no `def c223`, the MUL reads c217.w."""
    out, dropped, replaced = [words[0]], False, 0
    i = 1
    while i < len(words):
        if not dropped and i + 5 < len(words) and words[i] == 0x05000051 and words[i + 1] & 0x70000000 == 0x20000000 and words[i + 1] & 0x7ff == 223:
            dropped = True; i += 6; continue
        if words[i] == 0xa00000df:
            out.append(0xa0ff00d9); replaced += 1
        else:
            out.append(words[i])
        i += 1
    assert dropped and replaced == 1
    return out


def reference_widened(gained, k=WIDEN_K, b=WIDEN_B):
    """The widened variant of a gained (dynamic) variant: the last texld replaced by the 117-word block, rG = max
    temp + 1 (rT = rG + 1, rU = rG + 2), the three DEFs before the first dcl."""
    words, items, _ = shader.instructions(struct.pack('<%dI' % len(gained), *gained))
    fetch = [item for item in items if item['opcode'] == TEXLD][-1]
    highest = -1
    for item in items:
        if item['opcode'] in (DCL, DEF):
            continue
        for word in item['words']:
            kind, number = shader.register_of(word)
            if kind == 0:
                highest = max(highest, number)
    g = highest + 1
    assert g + 2 < 32
    destination, coordinate, sampler = fetch['words']
    assert shader.register_of(coordinate)[0] == 1 and shader.swizzle_of(coordinate) == 'xyzw' and coordinate >> 24 & 0xf == 0
    block = widen_block(destination, coordinate, sampler, g)
    at = fetch['dword']
    # The transformer emits the DEFs at the original's first `dcl` (structure()'s first_declaration), after the
    # motion/fill/share DEFs keyed on the same position and after the original's own DEFs.
    first = next(item['dword'] for item in items if item['opcode'] == DCL)
    out = list(words[:first]) + widen_definitions(k, b) + list(words[first:at]) + block + list(words[at + 4:])
    return out, g


def program_words(path):
    data = path.read_bytes()
    return list(struct.unpack('<%dI' % (len(data) // 4), data))


class HullEmissiveWideningTransformerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        cls.families = families()
        if not all((cls.originals / f'{name}.bin').is_file() for name in cls.families):
            reason = f'local original corpus unavailable under {cls.originals} (X3M_SHADER_PROGRAM_DIRECTORY)'
            if os.environ.get('X3M_REQUIRE_SHADER_CORPUS') == '1':
                raise AssertionError(reason)
            print('SKIP:', reason, file=sys.stderr)
            raise unittest.SkipTest(reason)
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise RuntimeError('host C++ compiler required')
        temporary = tempfile.TemporaryDirectory(prefix='x3-hull-widen-')
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        executable = cls.directory / 'structure'
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/hull_lightmap_gain_structure.cpp'),
                                str(ROOT / 'src/renderer/linear_material.cpp'), str(ROOT / 'src/renderer/material_motion.cpp'),
                                '-o', str(executable)], capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(executable), str(cls.originals), str(cls.directory)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.driver = json.loads(run.stdout)
        cls.rows = {row['name']: row for row in cls.driver['rows']}

    def gained(self):
        return sorted(name for name, family in self.families.items() if name.startswith('ps_') and name not in UNTOUCHED)

    def test_python_reference_rebuilds_every_widened_variant_byte_for_byte(self):
        registers, checks = {}, 0
        for name in self.gained():
            for f in range(len(FILLS)):
                for depth in (0, 1):
                    gained = expected_dynamic(program_words(self.directory / f'{name}-hlgain-{f}-{depth}.bin'))
                    widened = program_words(self.directory / f'{name}-hlwiden-{f}-{depth}.bin')
                    expected, g = reference_widened(gained)
                    self.assertEqual(widened, expected, (name, f, depth))
                    registers.setdefault(g, 0); registers[g] += 1
                    checks += 1
                share = expected_dynamic(program_words(self.directory / f'{name}-hlsharegain-{f}.bin'))
                widened = program_words(self.directory / f'{name}-hlsharewiden-{f}.bin')
                expected, g = reference_widened(share)
                self.assertEqual(widened, expected, (name, f, 'share'))
                registers.setdefault(g, 0); registers[g] += 1
                checks += 1
        self.assertEqual(checks, 100 * 6)
        # rG sits above every register: the plain variants' motion bodies end at r8/r9 (XT r9/r10),
        # the share variants at r23 (its r11-r23 reduction), never at or beyond r32.
        self.assertTrue(all(g < 32 for g in registers), registers)
        self.assertEqual(sum(registers.values()), 600)
        self.assertEqual(max(registers), 24)  # rT = r25, rU = r26 in those share variants

    def test_free_lanes_and_constants(self):
        # The oracle's free-lane proof over every widened variant: c217 is read through .yzyz exactly once (the
        # block), otherwise only as replicated .x (the motion fragment) or .w (the far fade's gain); c210/c211 are
        # defined once and read only by the block (4 and 3 reads); the gained variant never touches c210/c211 or
        # c217.yz, so the lanes were free before the block took them.
        lanes, reads = {}, {}
        for name in self.gained():
            for variant in (f'{name}-hlwiden-0-1.bin', f'{name}-hlsharewiden-1.bin', f'{name}-hlgain-1-0.bin', f'{name}-hlsharegain-0.bin'):
                data = (self.directory / variant).read_bytes()
                if 'gain' in variant:  # the driver writes the static form; the widened variants build on the dynamic one
                    dynamic = expected_dynamic(list(struct.unpack('<%dI' % (len(data) // 4), data)))
                    data = struct.pack('<%dI' % len(dynamic), *dynamic)
                words, items, _ = shader.instructions(data)
                seen = {'yzyz': 0, 'xxxx': 0, 'wwww': 0, 'other': 0}
                defs = {WIDEN_CONSTANT: 0, WIDEN_LUMA_CONSTANT: 0, WIDEN_GATE_CONSTANT: 0}
                uses = {WIDEN_CONSTANT: 0, WIDEN_LUMA_CONSTANT: 0, WIDEN_GATE_CONSTANT: 0}
                for item in items:
                    if item['opcode'] == DEF:
                        number = shader.register_of(item['words'][0])
                        if number[0] == 2 and number[1] in defs:
                            defs[number[1]] += 1
                        continue
                    if item['opcode'] == DCL:
                        continue
                    for w in item['words'][1:]:
                        kind, number = shader.register_of(w)
                        if kind != 2:
                            continue
                        if number == 217:
                            sw = shader.swizzle_of(w)
                            seen[sw if sw in seen else 'other'] += 1
                        elif number in uses:
                            uses[number] += 1
                widened = 'widen' in variant
                self.assertEqual(seen['other'], 0, (variant, 'c217 read with a non-replicated swizzle'))
                self.assertEqual(seen['yzyz'], 1 if widened else 0, variant)
                self.assertEqual(seen['wwww'], 1, (variant, 'the dynamic gain reads c217.w once'))
                self.assertEqual(defs, {WIDEN_CONSTANT: 1 if widened else 0, WIDEN_LUMA_CONSTANT: 1 if widened else 0, WIDEN_GATE_CONSTANT: 1 if widened else 0}, variant)
                self.assertEqual(uses, {WIDEN_CONSTANT: 5 if widened else 0, WIDEN_LUMA_CONSTANT: 4 if widened else 0, WIDEN_GATE_CONSTANT: 3 if widened else 0}, variant)
                lanes[seen['xxxx']] = lanes.get(seen['xxxx'], 0) + 1
                reads[variant.split('-')[1]] = reads.get(variant.split('-')[1], 0) + 1
        self.assertEqual(sum(reads.values()), 400)
        self.assertTrue(all(count >= 1 for count in lanes), lanes)  # the motion fragment reads c217.x

    def test_untouched_programs_and_slot_growth(self):
        for name in sorted(UNTOUCHED):
            self.assertEqual(self.rows[name]['widen_applied'], 0, name)
            for f in range(len(FILLS)):
                for depth in (0, 1):
                    base = (self.directory / f'{name}-hlbase-{f}-{depth}.bin').read_bytes()
                    self.assertEqual((self.directory / f'{name}-hlwiden-{f}-{depth}.bin').read_bytes(), base, (name, f, depth))
                self.assertEqual((self.directory / f'{name}-hlsharewiden-{f}.bin').read_bytes(), (self.directory / f'{name}-hlshare-{f}.bin').read_bytes(), name)
        growth = {}
        for name in self.gained():
            row = self.rows[name]
            self.assertEqual(row['widen_applied'], 1, name)
            for f in range(len(FILLS)):
                for depth in (0, 1):
                    self.assertEqual(row['widen_slots'][f][depth] - row['variant_slots'][f][depth], WIDEN_SLOTS, name)
                    self.assertLessEqual(row['widen_slots'][f][depth], 512, name)
                self.assertEqual(row['share_widen_slots'][f] - row['share_variant_slots'][f], WIDEN_SLOTS, name)
                self.assertLessEqual(row['share_widen_slots'][f], 512, name)
            family = growth.setdefault(self.families[name], dict(programs=0, max_widen_slots=0, max_share_widen_slots=0))
            family['programs'] += 1
            family['max_widen_slots'] = max(family['max_widen_slots'], max(max(v) for v in row['widen_slots']))
            family['max_share_widen_slots'] = max(family['max_share_widen_slots'], max(row['share_widen_slots']))
        self.assertEqual({k: v['programs'] for k, v in growth.items()}, {'hull': 66, 'palette': 20, 'xt': 14})
        self.assertEqual(self.driver['max_widen_slots'], self.driver['max_variant_slots'] + WIDEN_SLOTS)
        self.assertLess(self.driver['max_widen_slots'], 512)

    def test_widened_block_shape(self):
        # The 28-instruction block in place of the texld (the first texldd keeps the texld's destination word, mask
        # and _pp; the coarse ones land in rU.xyz and rT.xyz), the gain MUL right after the block's final MAD, no DEF
        # of c217/c223, the block's opcodes and slot weights (dsx/dsy 2, texldd 3, the rest 1: 36 weighted, 35 over the texld).
        weights = {DSX: 2, DSY: 2, TEXLDD: 3}
        expected = [DSX, DSY, MUL, MUL, ADD, MAX, MAD, RSQ, MAX, RCP, MUL, TEXLDD, ADD, TEXLDD, DP3, TEXLDD, DP3, MIN, DP3, MAD, MAX, RCP, MUL, MAD, MUL, MUL, MUL, MAD]
        for name in self.gained():
            words, items, _ = shader.instructions((self.directory / f'{name}-hlwiden-0-1.bin').read_bytes())
            self.assertEqual([item['opcode'] for item in items if item['opcode'] == TEXLD][-1:], [TEXLD], name)  # earlier texture reads stay
            texldd = [item for item in items if item['opcode'] == TEXLDD]
            self.assertEqual(len(texldd), 3, name)
            at = items.index(texldd[0])
            block = items[at - 11:at + 17]
            self.assertEqual([item['opcode'] for item in block], expected, name)
            self.assertEqual(sum(weights.get(op, 1) for op in expected), WIDEN_SLOTS + 1)
            self.assertEqual(items[at + 17]['opcode'], MUL, name)
            self.assertEqual(shader.register_of(items[at + 17]['words'][2]), (2, 217))
            self.assertEqual(shader.swizzle_of(items[at + 17]['words'][2]), 'wwww')
            self.assertEqual(shader.swizzle_of(block[3]['words'][2]), 'yzyz')
            self.assertEqual(shader.register_of(block[3]['words'][2]), (2, 217))
            self.assertLess([item for item in items if item['opcode'] == TEXLD][-1]['dword'], texldd[0]['dword'], (name, 'the light-map fetch is the last texture read'))
            self.assertFalse(any(item['opcode'] == DEF and shader.register_of(item['words'][0]) in ((2, 217), (2, 223)) for item in items), name)
            # dsx/dsy source the coordinate register directly; the widened texldd keeps the texld's destination word.
            self.assertEqual(shader.register_of(block[0]['words'][1])[0], 1, name)
            self.assertEqual(block[0]['words'][1], texldd[0]['words'][1], name)
            self.assertEqual(shader.mask_of(texldd[0]['words'][0]), 'xyzw', name)
            g = shader.register_of(block[0]['words'][0])[1]
            self.assertEqual(shader.register_of(texldd[1]['words'][0]), (0, g + 2), name)  # x doubled: rU.xyz (rU.w holds g)
            self.assertEqual(shader.register_of(texldd[2]['words'][0]), (0, g + 1), name)  # y doubled: rT.xyz
            self.assertEqual((shader.register_of(texldd[1]['words'][3]), shader.register_of(texldd[1]['words'][4])), ((0, g + 1), (0, g)), name)
            self.assertEqual((shader.register_of(texldd[2]['words'][3]), shader.register_of(texldd[2]['words'][4])), ((0, g), (0, g + 1)), name)
            self.assertEqual((shader.mask_of(texldd[1]['words'][0]), shader.mask_of(texldd[2]['words'][0])), ('xyz', 'xyz'), name)
            self.assertEqual((shader.register_of(block[6]['words'][0]), shader.mask_of(block[6]['words'][0])), ((0, g + 2), 'w'), name)  # g = saturate(rho^2 / K - 1)
            self.assertEqual(shader.register_of(block[-1]['words'][0]), shader.register_of(texldd[0]['words'][0]), name)
            self.assertEqual(shader.mask_of(block[-1]['words'][0]), 'xyz', name)
            # Saturate on the 1/k clamp and the gate, the negated gate offset.
            self.assertTrue(block[6]['words'][0] & SAT and block[8]['words'][0] & SAT and block[19]['words'][0] & SAT and block[23]['words'][0] & SAT, name)
            self.assertEqual(block[23]['words'][3] >> 24 & 0xf, 1, name)
            self.assertEqual(block[19]['words'][3] >> 24 & 0xf, 1, name)  # b = saturate(32 L_k - 1)
            self.assertEqual(block[6]['words'][3] >> 24 & 0xf, 1, name)   # g = saturate(rho^2 / K - 1)
            # The DEFs carry K = 3, B = 3 (1/K, 2^-8, B - 1, 1/0.15; luma, 9).
            defs = {shader.register_of(item['words'][0])[1]: item['words'][1:] for item in items if item['opcode'] == DEF}
            self.assertEqual(list(defs[WIDEN_CONSTANT]), [f32(1 / 3), f32(2 ** -8), f32(2.0), f32(1 / 0.15)], name)
            self.assertEqual(list(defs[WIDEN_LUMA_CONSTANT]), [f32(0.2126), f32(0.7152), f32(0.0722), f32(9.0)], name)
            self.assertEqual(list(defs[WIDEN_GATE_CONSTANT]), [f32(32.0), f32(1.0), 0, 0], name)

    def test_flow_control_depth_refusal(self):
        # Behavioural: the driver runs linear_material_flow_control_depth on synthetic ps_3_0 programs and reports the
        # depths (a fetch inside if/rep/loop 1, nested 2, after the closing token or outside 0, after a label/ret or
        # call -2 = Subroutine, off-boundary and site-0 -1); the transform refuses depth != 0 (FlowControl) and -2
        # (Subroutine). Every widened corpus program passed the transform, so its pinned fetch is at depth 0 (the 14
        # XT programs close their rep/if blocks 26-30 DWORDs before it).
        self.assertEqual(self.driver['flow_control_checks'], 12)
        self.assertEqual(self.driver['flow_control'], dict(inside_if=1, after_endif=0, inside_rep=1, inside_loop=1, after_endloop=0, nested=2,
                                                            outside=0, after_label_ret=-2, after_call=-2, off_boundary=-1, site_zero=-1))
        header = source_text(ROOT / 'src/renderer/linear_material.h')
        self.assertIn('FlowControl, // hull emissive widening', header); self.assertIn('Subroutine   // hull emissive widening', header)

    def test_coverage_every_sm3_lightmap_program_is_widened_or_listed(self):
        manifest_path = self.originals.parent / 'manifest.json'
        if not manifest_path.is_file():
            self.skipTest(f'archive manifest missing beside the corpus: {manifest_path}')
        manifest = json.loads(source_text(manifest_path))
        effects = {}
        for effect in manifest['effects']:
            path = effect['path']
            if not path.startswith('shader/3_0/'):
                continue  # every 3_0 directory (hueshift_off, hue_lights_off included): the family is the effect's base name
            name = path.split('/')[-1].split('.')[0]
            for suffix in ('_0000', '_0001', '2s'):
                name = name.removesuffix(suffix) if hasattr(name, 'removesuffix') else (name[:-len(suffix)] if name.endswith(suffix) else name)
            for program in effect['program_occurrences']:
                effects.setdefault(program, set()).add(name)
        selected = {f'ps_{ps}' for _, ps in runner.PAIRS}
        rows, untransformed = [], []
        for program in manifest['programs']:
            if program['stage'] != 'ps' or program['model'] != '3_0':
                continue
            code = (self.originals / f"{program['id']}.bin").read_bytes()
            samplers = [c for c in parse_ctab(code) if c['name'] == LIGHTMAP_SAMPLER]
            if not samplers:
                continue
            name = program['id']
            transformed = name in selected and self.rows.get(name, {}).get('widen_applied') == 1
            row = dict(hash=name[3:], family=sorted(effects.get(name, ())), group=self.families.get(name), stage=samplers[0]['register'],
                       words=program['bytes'] // 4, transformed=transformed)
            if transformed:
                driver = self.rows[name]
                row['widened_slots'] = dict(plain=max(max(v) for v in driver['widen_slots']), share=max(driver['share_widen_slots']))
            elif name in EXCLUDED:
                row['excluded'] = EXCLUDED[name]
            else:
                untransformed.append(name)
            rows.append(row)
        rows.sort(key=lambda r: r['hash'])
        self.assertEqual(untransformed, [], 'SM3 light-map programs neither widened nor listed')
        self.assertEqual(len(rows), 101)
        self.assertEqual(sum(r['transformed'] for r in rows), 100)
        self.assertEqual({r['stage'] for r in rows if r['transformed']}, {2, 3})
        self.assertEqual(sum(1 for r in rows if r['transformed'] and r['stage'] == 2), 44)
        by_family = {}
        for r in rows:
            if r['transformed']:
                key = '+'.join(r['family']) or r['group']
                entry = by_family.setdefault(key, dict(programs=0, max_plain_slots=0, max_share_slots=0))
                entry['programs'] += 1
                entry['max_plain_slots'] = max(entry['max_plain_slots'], r['widened_slots']['plain'])
                entry['max_share_slots'] = max(entry['max_share_slots'], r['widened_slots']['share'])
        COVERAGE.write_text(json.dumps(dict(
            description='Every ps_3_0 program of the local archive declaring LightMapTexSampler: widened (hull emissive widening R1 + R2) or excluded by design. '
                        'Slots are the weighted SM3 totals of the widened variants (+35 over the gained ones); the moon program is out of scope.',
            source_manifest_sha256=manifest.get('index_sha256'), programs=len(rows), transformed=sum(r['transformed'] for r in rows),
            excluded={k[3:]: v for k, v in EXCLUDED.items()}, slot_growth_per_variant=WIDEN_SLOTS, per_family=by_family, rows=rows), indent=1) + '\n')


class LauncherAndProxyTests(unittest.TestCase):
    def test_launcher_option(self):
        with tempfile.TemporaryDirectory() as directory:
            code, output, error = launch(directory, *PREREQUISITES, '--hull-emissive-widening', 'off')
            self.assertEqual(code, 0, error)
            baseline = json.loads(output)['env']
            self.assertNotIn('X3M_HULL_EMISSIVE_WIDENING', baseline)  # "off" is the opt-out: the variable stays unset
            # Omitted with the light-map gain active: the user-accepted run236/run237 default, K = B = 4.
            code, output, error = launch(directory, *PREREQUISITES); self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env.pop('X3M_HULL_EMISSIVE_WIDENING'), '4,4')
            self.assertEqual(env, baseline)
            for spelling in ('off', 'OFF', ' off '):
                code, output, error = launch(directory, *PREREQUISITES, '--hull-emissive-widening', spelling)
                self.assertEqual(code, 0, error)
                self.assertNotIn('X3M_HULL_EMISSIVE_WIDENING', json.loads(output)['env'], spelling)
            # A stale shell value can neither survive the opt-out nor change the resolved default.
            with mock.patch.dict(os.environ, {'X3M_HULL_EMISSIVE_WIDENING': '3,3'}):
                code, output, error = launch(directory, *PREREQUISITES, '--hull-emissive-widening', 'off')
                self.assertEqual(code, 0, error)
                self.assertEqual(json.loads(output)['env'], baseline)  # an inherited value is dropped
                code, output, error = launch(directory, *PREREQUISITES); self.assertEqual(code, 0, error)
                self.assertEqual(json.loads(output)['env']['X3M_HULL_EMISSIVE_WIDENING'], '4,4')
            # The default never enables the route itself: without an active gain it resolves to off, not to an error.
            for extra in ([], ['--hull-lightmap-gain', '1']):
                args = (PREREQUISITES + extra) if extra else ['--motion-output']
                code, output, error = launch(directory, *args); self.assertEqual(code, 0, error)
                self.assertNotIn('X3M_HULL_EMISSIVE_WIDENING', json.loads(output)['env'], extra)
            # K[,B]: B defaults to K; B in [1, K].
            for value, expected in (('3', '3,3'), ('3,3', '3,3'), ('3,1', '3,1'), ('2,1.5', '2,1.5'), ('8,8', '8,8'), ('1.5', '1.5,1.5'), ('4,2.25', '4,2.25')):
                code, output, error = launch(directory, *PREREQUISITES, '--hull-emissive-widening', value); self.assertEqual(code, 0, error)
                env = json.loads(output)['env']
                self.assertEqual(env.pop('X3M_HULL_EMISSIVE_WIDENING'), expected)
                self.assertEqual(env, baseline)
            for value in ('', '3,3,1', '1', '1,1', '0.5', '8.5', '3,0.5', '3,3.5', '3,0', '3,-1', 'a', '3,b', 'nan', '3,nan', 'inf', '3,inf'):
                code, _, error = launch(directory, *PREREQUISITES, '--hull-emissive-widening', value)
                self.assertEqual(code, 2, value); self.assertIn('--hull-emissive-widening', error)
            for extra in (['--hull-lightmap-gain', '1'],):
                code, _, error = launch(directory, *PREREQUISITES, *extra, '--hull-emissive-widening', '3')
                self.assertEqual(code, 2, extra); self.assertIn('--hull-emissive-widening', error)
            code, _, error = launch(directory, '--motion-output', '--hull-emissive-widening', '3'); self.assertEqual(code, 2)
            # No --taa and no camera requirement: the footprint is the light map's own.
            code, output, error = launch(directory, *PREREQUISITES, '--no-light-map-far-fade', '--hull-emissive-widening', '3'); self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_HULL_EMISSIVE_WIDENING'], '3,3')

    def test_dll_plumbing(self):
        capture = source_text(ROOT / 'src/proxy/capture.cpp')
        block = capture[capture.index('// X3M_HULL_EMISSIVE_WIDENING=K[,B] (docs/architecture'):][:2800]
        self.assertIn('x3m::config::get(L"X3M_HULL_EMISSIVE_WIDENING",widen_setting,96)', block)
        self.assertIn('if(valid&&count==1)parsed[1]=parsed[0]; // B defaults to K', block)
        self.assertIn('valid=valid&&count>=1&&parsed[0]>1.f&&parsed[0]<=8.f&&parsed[1]>=1.f&&parsed[1]<=parsed[0];', block)
        self.assertIn('hull_emissive_widening_requested=valid&&gained;', block)
        self.assertNotIn('camera_state::request_consumer();', block)  # no camera latch: the footprint is the light map's
        self.assertIn('hull_emissive_widening_mode requested=1 enabled=%u valid=%u k=%g b=%g gain=%g%s', block)
        self.assertIn('configure_hull_emissive_widening(hull_emissive_widening[0],hull_emissive_widening[1])', capture)
        self.assertIn('hull_emissive_widening_configured accepted=%u k=%g b=%g', capture)
        # The level-0 size is read once per texture pointer change, inside the SetTexture hook beside GetLevelCount
        # (GetType + GetLevelDesc(0), documented), only with the widening on; never per draw.
        hook = extract_function(capture, 'HRESULT WINAPI set_texture(')
        self.assertIn('const bool size=query&&ctx.motion_output.texture_size_wanted();', hook)
        self.assertIn('if(SUCCEEDED(hr)&&size)ctx.motion_output.texture_level0_size(texture,width,height);', hook)
        self.assertIn('ctx.motion_output.set_texture(stage,texture,levels,query,reader,width,height,identity);', hook)
        # The size shadow is keyed by the proxy's resource identity as well as the pointer (a freed and reallocated
        # light map at the same address is re-read); the sampler hook installs for the widening (MINFILTER shadow).
        self.assertIn('const std::uint64_t identity=ctx.motion_output.texture_identity_wanted(stage,texture)?resource_id(texture):0;', hook)
        wanted_identity = extract_function(source_text(ROOT / 'src/proxy/motion_output.cpp'), 'bool MotionOutput::texture_identity_wanted(')
        self.assertIn('return lightmap_widen_ && texture && (stage == 2 || stage == 3) && (samplers_[stage].texture != texture || samplers_[stage].identity == 0);', wanted_identity)
        self.assertIn('const bool query=ctx.motion_output.texture_levels_wanted(stage,texture,identity);', hook)
        self.assertIn('hooked.motion_output.mip_bias_active()||hooked.motion_output.hull_emissive_widening()||hooked.motion_output.linear_materials_requested()', capture)
        header = source_text(ROOT / 'src/proxy/motion_output.h')
        motion = source_text(ROOT / 'src/proxy/motion_output.cpp')
        helper = extract_function(motion, 'void MotionOutput::texture_level0_size(')
        self.assertIn("texture->GetType() != D3DRTYPE_TEXTURE) return;", helper)
        self.assertIn('static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0, &desc)', helper)
        self.assertIn('bool texture_size_wanted() const noexcept { return lightmap_widen_; }', header)
        self.assertIn('DWORD width = 0, height = 0; // level-0 size of a 2D texture', header)
        self.assertEqual(motion.count('texture_level0_size('), 2)  # the definition and resync_samplers (the hook is in capture.cpp)
        self.assertEqual(capture.count('texture_level0_size('), 1)
        self.assertEqual(motion.count('GetLevelDesc'), 1)
        # The one other GetLevelDesc of the route: the alpha-tested casters' pool check (--shadow-alpha-casters,
        # default off) in the replay include. Per draw by design, but never on the off path: only an alpha-tested
        # draw that would be a managed candidate reaches it, and only with the option on and the pass holding its
        # alpha programs (alpha_casters_ready requires alpha_casters_requested_).
        replay = source_text(ROOT / 'src/proxy/motion_output_shadow_replay_inc.h')
        self.assertEqual(replay.count('GetLevelDesc'), 1)
        self.assertIn('GetLevelDesc(0, &desc)', extract_function(replay, 'bool MotionOutput::alpha_caster_source('))
        self.assertEqual(motion.count('alpha_caster_source('), 1)
        self.assertIn('if (route.alpha_tested && !alpha_excluded && zwrite && admitted && shadow_ok && managed) alpha_excluded = !alpha_caster_source(alpha_texture, alpha_threshold);', motion)
        self.assertIn('bool alpha_excluded = route.alpha_tested && !alpha_casters_ready();', motion)
        self.assertIn('bool alpha_casters_ready() const noexcept { return alpha_casters_requested_&&', header)
        setter = extract_function(motion, 'void MotionOutput::set_texture(')
        self.assertIn('if (queried) { s.levels = levels; s.width = width; s.height = height; s.identity = identity; }', setter)
        wanted = extract_function(motion, 'bool MotionOutput::texture_levels_wanted(')
        self.assertIn('(samplers_[stage].texture != texture || (identity != 0 && samplers_[stage].identity != identity))', wanted)
        # MINFILTER: shadowed by the sampler hook, raised to ANISOTROPIC for a widened draw of a stage shadowed at
        # anything else (read once when unknown, per widened draw with the hooks off), restored first in undo, a
        # failed raise keeps the gained variant; counted on the frame and session lines.
        ensure = extract_function(motion, 'bool MotionOutput::ensure_widen_filter(')
        for text in ('if (!state_hooks_) s.minfilter_known = false;', 'direct_call<GetSamplerStateFn>(GetSamplerState, stage, D3DSAMP_MINFILTER, &value)',
                     'if (s.minfilter == D3DTEXF_ANISOTROPIC) return true;', 'direct_call<SetSamplerStateFn>(SetSamplerState, stage, D3DSAMP_MINFILTER, D3DTEXF_ANISOTROPIC)',
                     'if (FAILED(hr)) { ++lightmap_widen_filter_failures_; return false; }', 'route.widen_filter_set = true; route.widen_filter_stage = std::uint8_t(stage); route.widen_filter_saved = s.minfilter;'):
            self.assertIn(text, ensure)
        undo = extract_function(motion, 'HRESULT MotionOutput::undo(')
        self.assertIn('if (route.widen_filter_set) { route.widen_filter_set = false; step(direct_call<SetSamplerStateFn>(SetSamplerState, route.widen_filter_stage, D3DSAMP_MINFILTER, route.widen_filter_saved)); }', undo)
        self.assertLess(undo.index('route.widen_filter_set'), undo.index('route.write2_set'))  # restored before the rest, right after the wrap states
        sampler = extract_function(motion, 'void MotionOutput::set_sampler_state(')
        self.assertIn('if (lightmap_widen_) { frame_timing::state_write(frame_timing::StateSet::SamplerState, unsigned(type), s.minfilter_known, s.minfilter == value); s.minfilter = value; s.minfilter_known = true; }', sampler)
        configure = extract_function(motion, 'bool MotionOutput::configure_hull_emissive_widening(')
        self.assertIn('if (device_) return lightmap_widen_;', configure)
        self.assertIn('lightmap_widen_ = hull_lightmap_gain_requested_ && std::isfinite(k) && std::isfinite(b)', configure)
        self.assertIn('&& k > 1.f && k <= 8.f && b >= 1.f && b <= k;', configure)
        # The per-draw footprint lanes ride the motion ABI's own two-vector upload (c217.yz): no extra constant write,
        # no getter, no allocation; (W K)^2 and (H K)^2 of the stage's shadowed size.
        draw = extract_function(motion, 'void MotionOutput::evaluate_draw(')
        self.assertIn('previous_rows ? 1.f : 0.f, lightmap_widen_draw_scale_[0], lightmap_widen_draw_scale_[1], lightmap_fade_gain_};', draw)
        self.assertEqual(draw.count('SetPixelShaderConstantF'), 1)
        self.assertIn('lightmap_widen_draw_scale_[0] = fade_route::lightmap_widen_scale(stage.width, lightmap_widen_k_);', draw)
        self.assertIn('lightmap_widen_draw_scale_[1] = fade_route::lightmap_widen_scale(stage.height, lightmap_widen_k_);', draw)
        self.assertNotIn('lightmap_widen_draw_k_', motion)
        core = source_text(ROOT / 'src/proxy/fade_route_core.h')
        law = extract_function(core, 'inline float lightmap_widen_scale(')
        for text in ('if (!size || !(k > 0.f)) return 0.f;', 'const float scaled = float(size) * k;', 'return scaled * scaled;'):
            self.assertIn(text, law)
        # Selection: the widened variant over every selected gain variant with a known size, not alpha tested, a mip
        # chain on the stage; no getter; the fixture may suppress it (the gained texld for the same-process baseline).
        bind = extract_function(motion, 'HRESULT MotionOutput::bind_variant_pair(')
        self.assertIn('bool widen_draw = lightmap_widen_draw_scale_[0] > 0.f && !widen_suppressed && !route.alpha_tested && shadow_.hull_lightmap_stage', bind)
        self.assertIn('&& samplers_[shadow_.hull_lightmap_stage].levels > 1;', bind)
        self.assertIn('if (widen_draw && shadow_.ps_hull_lightmap_widen && ensure_widen_filter(route)) { ps = shadow_.ps_hull_lightmap_widen; widen = true; }', bind)
        self.assertIn('const bool widened_original=gained_original&&widen_draw&&shadow_.ps_sun_original_lightmap_widen&&ensure_widen_filter(route);', bind)
        self.assertIn('if (widen && SUCCEEDED(hr)) route.hull_lightmap_widen = true;', bind)
        self.assertNotIn('GetRenderState', bind); self.assertNotIn('GetTexture', bind)
        # Created beside the gained variant only, both lanes, with K and B, the stage recorded.
        self.assertEqual(motion.count('const renderer::HullLightmapWiden parameters{lightmap_widen_k_, lightmap_widen_b_};'), 2)
        self.assertEqual(motion.count('&parameters, &widen_applied);'), 2)
        self.assertIn('if (lightmap_widen_ && entry.hull_lightmap_variant) {', motion)
        self.assertIn('if (lightmap_widen_ && entry.sun_original_lightmap_variant) {', motion)
        self.assertIn('hull_lightmap_widen_variant device=%llu original=%016llx transform=%u create=%08lx words=%u depth=%u k=%g b=%g stage=%u widen_applied=%u', motion)
        self.assertIn('renderer::linear_material_hull_lightmap_stage(hash, bytes / 4)', motion)
        # Level counts and sizes are read for the widening as the counts are for the mip bias; the far fade alone
        # latches the camera; frame, capture-frame draw and session lines without k ranges (k is per pixel).
        self.assertIn('return (mip_bias_bits_ || lightmap_widen_) && texture && stage < sampler_stage_count\n        && (samplers_[stage].texture != texture || (identity != 0 && samplers_[stage].identity != identity));', motion)
        self.assertIn('if (lightmap_far_fade_ && scene) lightmap_fade_m00_ = valid ? sample.state.m00 : 0.f;', motion)   # the scene read is shared with the small-parts cull hand-over (5a81df27)
        self.assertIn('hull_lightmap_widen_frame device=%llu frame=%llu admitted=%u widened=%u held=%u k=%g b=%g filter_sets=%u filter_reads=%u filter_failures=%u', motion)
        self.assertIn('hull_lightmap_widen_draw device=%llu frame=%llu index=%lu ps=%016llx stage=%u size=%lux%lu c=%g scale=%g,%g', motion)
        self.assertIn('hull_lightmap_widen_summary device=%llu k=%g b=%g variants=%lu widened_draws=%lu filter_sets=%lu', motion)
        for text in ('IDirect3DPixelShader9* hull_lightmap_widen_variant = nullptr;', 'IDirect3DPixelShader9* sun_original_lightmap_widen_variant = nullptr;',
                     'IDirect3DPixelShader9* ps_hull_lightmap_widen = nullptr;', 'std::uint8_t hull_lightmap_stage = 0;', 'bool hull_lightmap_widen = false;',
                     'float lightmap_widen_draw_scale_[2] = {0.f, 0.f};', 'bool widen_filter_set = false;', 'DWORD minfilter = 0; bool minfilter_known = false;', 'std::uint64_t identity = 0;'):
            self.assertIn(text, header)
        source = source_text(ROOT / 'src/renderer/linear_material.cpp')
        self.assertIn('constexpr unsigned dsx = 91, dsy = 92, texldd = 93;', source)
        self.assertIn('case dsx: case dsy: operands=2; slots=2; return true;', source)
        self.assertIn('case texldd: operands=5; slots=3; return true;', source)
        self.assertIn('if (op==texldd) { if (dimension!=2) return false; }', source)
        self.assertIn('constexpr unsigned lightmap_widen_words = 117, lightmap_widen_fetch_offset = 41,', source)
        self.assertIn('constexpr unsigned lightmap_widen_constant = 210, lightmap_widen_luma_constant = 211, lightmap_widen_gate_constant = 203;', source)
        self.assertIn('if (widen && (lightmap_gain==1.0f || !lightmap_widen_valid(*widen))) return LinearMaterialResult::InvalidConfig;', source)
        self.assertIn('return std::isfinite(w.k) && std::isfinite(w.b) && w.k>1.0f && w.k<=8.0f && w.b>=1.0f && w.b<=w.k;', source)
        self.assertIn("'dsx': 2, 'dsy': 2, 'texldd': 3", source_text(ROOT / 'tools/analysis/inspect_motion_output_profiles.py'))

if __name__ == '__main__':
    unittest.main()
