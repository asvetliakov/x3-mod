"""Host oracle of the original-shading share producer (legacy-sun-application.md 1, 3.1).

Transformer (linear_material_original_sun_share_pixel_variant): the fill
variant (K=0: the plain motion variant) of a reviewed original pixel program
retained verbatim and in order, plus `def c212`/`def c221`, eight carrier
initialisations, one seed MUL/MAD before each sun MAD, one parallel op per
sun-dependent RGB op on the original operands, the fill twin at K>0, the final
RGB instruction redirected into r11 (keeping _pp) and copied to oC0, the
converted producer's 21-instruction reduction and the sole `mov oC2.y` at END.
The float64 oracle executes the generated tail with the seeds intact and with
the seeds zeroed and checks s*Y(C) = Y(C) - Y(C_nosun) in code values. No game
assets are bundled; no GPU rounding, partial precision or native claim.
"""
import copy
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools/analysis'))
sys.path.insert(0, str(ROOT / 'verification/probe'))
import inspect_motion_output_profiles as shader
import run_linear_material as runner
from verification.analysis import xt_pixel_execution as vm

FILLS = (0., 0.05)
DEF, MOV, ADD, MAD, MUL = 81, 1, 2, 4, 5
DOMAIN_CONSTANT, LUMA_CONSTANT, FILL_CONSTANT = 212, 221, 215
TOTAL, CARRIER_BASE, CARRIER_FINAL = 11, 16, 23
REDUCTION_INSTRUCTIONS, INIT_INSTRUCTIONS, TWIN_INSTRUCTIONS, TWIN_SLOTS = 22, 8, 16, 34  # converted helper's 21 + the S <= L slack MAD
SLACK = 2. ** -16
ZERO = 0xa05500d4  # c212.yyyy, the variant's own zero literal


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def span(words, item):
    return tuple(words[item['dword']:item['dword'] + item['length'] + 1])


def unpack(code):
    words, items, _ = shader.instructions(code)
    return list(words), items


def pack(words):
    return struct.pack('<%dI' % len(words), *words)


def families():
    result = {}
    for index, (vs, ps) in enumerate(runner.PAIRS):
        family = ('hull' if index < 110 else 'asteroid' if index < 116 else 'palette' if index < 148
                  else 'xt' if index < 162 else 'glass')
        result.setdefault(f'vs_{vs}', family)
        result.setdefault(f'ps_{ps}', family)
    return result


def destination(item):
    return shader.register_of(item['words'][0]) if item['words'] and item['opcode'] not in (DEF, shader.DCL) else (-1, -1)


def redirected(variant_span, control_span):
    """The control's final RGB instruction with its destination moved to r11, _pp kept."""
    return (variant_span[0] == control_span[0] and variant_span[2:] == control_span[2:] and
            (shader.register_of(control_span[1]), shader.mask_of(control_span[1])) == ((8, 0), 'xyz') and
            (shader.register_of(variant_span[1]), shader.mask_of(variant_span[1])) == ((0, TOTAL), 'xyz') and
            variant_span[1] & 0x00f00000 == control_span[1] & 0x00f00000 == 0x00200000)


def split(variant, control):
    """(retained items, added items, redirected final item): the control matched in order as a subsequence."""
    words, items = unpack(variant)
    control_words, control_items = unpack(control)
    expected = [span(control_words, i) for i in control_items]
    retained, extra, final, j = [], [], None, 0
    for item in items:
        row = span(words, item)
        if j < len(expected) and row == expected[j]:
            retained.append(item); j += 1
        elif j < len(expected) and final is None and redirected(row, expected[j]):
            retained.append(item); final = item; j += 1
        else:
            extra.append(item)
    assert j == len(expected), 'control not retained in order'
    assert final is not None, 'no redirected final'
    return words, items, retained, extra, final


def initial(seed, branch):
    import random
    rng = random.Random(seed)
    registers = {f'{kind}{r}': [rng.uniform(.1, .3) for _ in range(4)]
                 for kind, count in [('r', 32), ('v', 10), ('c', 224)] for r in range(count)}
    for r in range(16, 24): registers[f'r{r}'] = [0.] * 4
    registers['c216'] = [.2, .3, .4, 1.]
    samplers = {r: (.43, .37, .29, .8) for r in range(16)}
    return vm.State(registers, {r: branch for r in range(16)}, samplers)


def luma(rgb):
    return sum(x * w for x, w in zip(rgb, (.2126, .7152, .0722)))


class OriginalSunShareTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
        cls.families = families()
        if not all((cls.originals / f'{name}.bin').is_file() for name in cls.families):
            reason = f'local original corpus of the 137 reviewed programs unavailable under {cls.originals} (X3M_SHADER_PROGRAM_DIRECTORY)'
            if os.environ.get('X3M_REQUIRE_SHADER_CORPUS') == '1':
                raise AssertionError(reason)
            print('SKIP:', reason, file=sys.stderr)
            raise unittest.SkipTest(reason)
        compiler = shutil.which('clang++') or shutil.which('c++')
        if not compiler:
            raise RuntimeError('host C++ compiler required')
        temporary = tempfile.TemporaryDirectory(prefix='x3-original-sun-share-')
        cls.addClassCleanup(temporary.cleanup)
        cls.directory = Path(temporary.name)
        executable = cls.directory / 'structure'
        # The helper includes linear_material.cpp for the synthetic planner refusals.
        build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/original_sun_share_structure.cpp'),
                                str(ROOT / 'src/renderer/material_motion.cpp'), '-o', str(executable)], capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(executable), str(cls.originals), str(cls.directory)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.driver = json.loads(run.stdout)
        cls.rows = {row['name']: row for row in cls.driver['rows']}

    def covered(self):
        return sorted(name for name, family in self.families.items() if name.startswith('ps_'))

    def variant(self, name, f, depth):
        return (self.directory / f'{name}-oshare-{f}-{depth}.bin').read_bytes()

    def control(self, name, f, depth):
        return (self.directory / f'{name}-ofill-{f}-{depth}.bin').read_bytes()

    def test_coverage_is_every_reviewed_pixel_program_with_share_and_no_vertex_program(self):
        by_family = {}
        for name, family in self.families.items():
            row = self.rows[name]
            key = (family, name[:2])
            by_family.setdefault(key, [0, 0])
            by_family[key][0] += 1
            if name.startswith('ps_'):
                self.assertEqual(row['status'], 0, name)
                by_family[key][1] += row['share_applied']
                self.assertIn(len(row['seeds']), (1, 2), (name, 'one combined lobe or diffuse+gloss carriers'))
            else:
                self.assertEqual(row['status'], 3, (name, 'vertex programs are UnsupportedShader'))
        pixel = {family: tuple(counts) for (family, stage), counts in by_family.items() if stage == 'ps'}
        self.assertEqual(pixel, {'hull': (66, 66), 'asteroid': (4, 4), 'palette': (20, 20), 'xt': (14, 14), 'glass': (4, 4)})
        self.assertEqual((self.driver['supported'], self.driver['applied']), (108, 108))
        seed_counts = [len(self.rows[n]['seeds']) for n in self.covered()]
        self.assertEqual((sum(seed_counts), seed_counts.count(1), seed_counts.count(2)), (152, 64, 44), 'contract: 152 seeds, 64 one-seed and 44 two-seed programs')
        self.assertTrue(all(0 < seed < self.rows[n]['final_rgb'] for n in self.covered() for seed in self.rows[n]['seeds']), 'seed operands precede the final RGB instruction')
        self.assertTrue(all(self.rows[n]['light'] == (6 if self.families[n] == 'xt' else self.rows[n]['light']) and self.rows[n]['light'] in (1, 2, 5, 6) for n in self.covered()))
        self.assertEqual({n for n, r in self.rows.items() if r['status'] == 0}, set(self.covered()))
        self.assertEqual(self.driver['synthetic_refusals'], 13)  # 10 planner refusals + 3 reserved-range refusals

    def test_controls_retained_verbatim_in_order_with_one_redirected_final(self):
        for name in self.covered():
            original, original_items, _ = shader.instructions((self.originals / f'{name}.bin').read_bytes())
            original_spans = [span(original, o) for o in original_items]
            final_rgb = self.rows[name]['final_rgb']
            for depth in (0, 1):
                motion = (self.directory / f'{name}-motion-{depth}.bin').read_bytes()
                self.assertEqual(self.control(name, 0, depth), motion, (name, depth, 'K=0 control is the motion variant'))
                for f in range(2):
                    words, items, retained, extra, final = split(self.variant(name, f, depth), self.control(name, f, depth))
                    # Every original instruction is inside the retained control in order; the
                    # redirected final is the profile's final_rgb instruction.
                    retained_spans = [span(words, i) for i in retained]
                    k = 0
                    for original_item in original_items:
                        target = span(original, original_item)
                        if original_item['dword'] == final_rgb:
                            target = (target[0], (target[1] & ~0x7ff & ~0x1800 & ~0x70000000) | TOTAL, *target[2:])
                        while k < len(retained_spans) and retained_spans[k] != target: k += 1
                        self.assertLess(k, len(retained_spans), (name, depth, f, original_item['dword'], 'original instruction retained in order'))
                        k += 1
                    self.assertEqual(span(words, final), (words[final['dword']], *span(words, final)[1:]))
                    # The redirect copy follows the final immediately; oC2.y is the last instruction.
                    copy_item = items[items.index(final) + 1]
                    self.assertEqual((copy_item['opcode'], shader.register_of(copy_item['words'][0]), shader.mask_of(copy_item['words'][0]),
                                      shader.register_of(copy_item['words'][1]), shader.swizzle_of(copy_item['words'][1])), (MOV, (8, 0), 'xyz', (0, TOTAL), 'xyzw'))
                    self.assertEqual(copy_item['words'][0] & 0x00f00000, 0)
                    last = items[-1]
                    self.assertEqual((last['opcode'], shader.register_of(last['words'][0]), shader.mask_of(last['words'][0]),
                                      shader.register_of(last['words'][1]), shader.swizzle_of(last['words'][1])), (MOV, (8, 2), 'y', (0, CARRIER_FINAL), 'wwww'))
                    self.assertIn(copy_item, extra); self.assertIn(last, extra)

    def test_added_instruction_shape_constants_and_read_ports(self):
        for name in self.covered():
            row = self.rows[name]
            light = row['light']
            for depth in (0, 1):
                for f in range(2):
                    words, items, retained, extra, final = split(self.variant(name, f, depth), self.control(name, f, depth))
                    definitions = [i for i in extra if i['opcode'] == DEF]
                    self.assertEqual([shader.register_of(d['words'][0])[1] for d in definitions], [DOMAIN_CONSTANT, LUMA_CONSTANT], name)
                    self.assertEqual(struct.unpack('<4f', struct.pack('<4I', *definitions[0]['words'][1:])), (f32(2.2), 0., 65504., SLACK))
                    self.assertEqual(struct.unpack('<4f', struct.pack('<4I', *definitions[1]['words'][1:])), (f32(.2126), f32(.7152), f32(.0722), 2. ** -20))
                    first_dcl = next(i for i in items if i['opcode'] == shader.DCL)
                    self.assertTrue(all(d['dword'] < first_dcl['dword'] for d in definitions), name)
                    body = [i for i in extra if i['opcode'] != DEF]
                    # Initialisation: eight zero MOVs into r16..r23 before the first seed.
                    self.assertEqual([(i['opcode'], shader.register_of(i['words'][0]), i['words'][1]) for i in body[:INIT_INSTRUCTIONS]],
                                     [(MOV, (0, r), ZERO) for r in range(CARRIER_BASE, CARRIER_FINAL + 1)], name)
                    seeds = [i for i in body if i['opcode'] in (MUL, MAD) and CARRIER_BASE <= destination(i)[1] <= CARRIER_FINAL - 1
                             and any(shader.register_of(w) == (2, light) for w in i['words'][1:])]
                    self.assertEqual(len(seeds), len(row['seeds']), name)
                    for seed in seeds:
                        scalar = seed['words'][1]
                        self.assertEqual(shader.register_of(scalar)[0], 0)
                        self.assertIn(shader.swizzle_of(scalar), ('wwww', 'zzzz', 'yyyy', 'xxxx'), (name, 'scalar lobe times the sun colour'))
                    twin = [i for i in body if destination(i) in ((0, 12), (0, 13))]
                    self.assertEqual(len(twin), 11 if f else 0, (name, 'fill twin scratch only at K>0'))
                    for i in body:
                        self.assertEqual(i['words'][0] & 0x00200000, 0, (name, 'added instructions never carry _pp'))
                        constants = {shader.register_of(w)[1] for w in i['words'][1:] if shader.register_of(w)[0] == 2}
                        self.assertLessEqual(len(constants), 1, (name, i['dword'], 'one constant read port'))
                        if i['opcode'] == MAD and shader.register_of(i['words'][0]) == (0, TOTAL):
                            self.fail((name, 'a MAD into r11 is only the retained final'))
                    for i in retained:
                        registers = {shader.register_of(w) for w in (i['words'][1:] if i['opcode'] != DEF else i['words'][:1])}
                        self.assertFalse({(2, DOMAIN_CONSTANT), (2, LUMA_CONSTANT)} & registers, (name, 'c212/c221 collision'))
                        if i['opcode'] not in (DEF, shader.DCL):
                            # r12/r13 are the fill block's own scratch in the K>0 control.
                            private = {(0, TOTAL)} | {(0, r) for r in range(CARRIER_BASE, CARRIER_FINAL + 1)}
                            self.assertFalse(private & {shader.register_of(w) for w in i['words'][1:]}, (name, 'private temporaries read by the control'))
                    self.assertEqual(len(body), row['instructions'][depth][f] - self.control_instructions(name, f, depth), name)
                    # Accounting: init 8 + reduction 21 (right after the oC0 copy) + oC0 copy + oC2.y
                    # + twin 16 at K>0 + the parallel operations (seeds, propagation, kills, the final's).
                    copy_index = next(k for k, i in enumerate(body) if shader.register_of(i['words'][0]) == (8, 0))
                    reduction = body[copy_index + 1:copy_index + 1 + REDUCTION_INSTRUCTIONS]
                    self.assertEqual(len(reduction), REDUCTION_INSTRUCTIONS, name)
                    self.assertEqual(body[copy_index + 1 + REDUCTION_INSTRUCTIONS:], [body[-1]], (name, 'oC2.y is the only instruction after the reduction'))
                    negated = lambda i: any((w >> 24) & 15 == 1 for w in i['words'][1:])
                    twin_items = [i for i in body if destination(i) in ((0, 12), (0, 13)) or (i['opcode'] == 32 and destination(i) == (0, CARRIER_FINAL))
                                  or (i['opcode'] == ADD and negated(i) and i not in reduction)]
                    self.assertEqual(len(twin_items), TWIN_INSTRUCTIONS if f else 0, name)
                    parallel = [i for i in body[INIT_INSTRUCTIONS:copy_index] if i not in twin_items]
                    self.assertEqual(len(body), INIT_INSTRUCTIONS + len(parallel) + len(twin_items) + 1 + REDUCTION_INSTRUCTIONS + 1, name)
                    for i in parallel:
                        self.assertIn(i['opcode'], (MOV, ADD, MUL, MAD), (name, 'parallel operations are MOV/ADD/MUL/MAD'))
                        self.assertEqual(destination(i)[0], 0); self.assertTrue(CARRIER_BASE <= destination(i)[1] <= CARRIER_FINAL, name)
                        self.assertFalse(negated(i), name); self.assertEqual(i['words'][0] & 0x00f00000, 0, name)
                    self.assertGreaterEqual(len(parallel), len(seeds) + 1, (name, 'seeds and the final (asteroids propagate directly)'))
                    self.assertLessEqual(len(parallel), len(seeds) + 12 + 1 + 8, (name, 'contract: 2-12 dependent operations plus kills'))

    def control_instructions(self, name, f, depth):
        _, items = unpack(self.control(name, f, depth))
        return sum(1 for i in items if i['opcode'] not in (DEF, shader.DCL))

    def test_slot_budget_and_family_maxima(self):
        maxima = {}; added_slots = []
        for name in self.covered():
            row = self.rows[name]; family = self.families[name]
            for depth in (0, 1):
                for f in range(2):
                    self.assertLessEqual(row['slots'][depth][f], 512, name)
                    self.assertLessEqual(row['max_temp'][depth][f], CARRIER_FINAL, name)
                    maxima[family] = max(maxima.get(family, 0), row['slots'][depth][f])
                extra = row['slots'][depth][0] - row['control_slots'][depth][0]
                added_slots.append(extra)
                self.assertEqual(row['slots'][depth][1] - row['control_slots'][depth][1], extra + TWIN_SLOTS, (name, 'fill twin adds 14 instructions, 34 slots'))
                self.assertEqual(row['instructions'][depth][1] - row['instructions'][depth][0] - 14, TWIN_INSTRUCTIONS, name)
        self.assertEqual(self.driver['max_slots'], max(maxima.values()))
        self.assertLess(self.driver['max_slots'], 512)
        self.assertGreaterEqual(min(added_slots), INIT_INSTRUCTIONS + 1 + 1 + REDUCTION_INSTRUCTIONS + 1 + 1)
        print('Original sun-share host evidence: ' + json.dumps(dict(max_slots_per_family=maxima, added_slots=[min(added_slots), max(added_slots)],
                                                                    max_slots=self.driver['max_slots'], synthetic_refusals=self.driver['synthetic_refusals']), sort_keys=True))

    def tail(self, code, light, zero_sun):
        """Header DEFs plus everything from the first seed MUL (or seed MAD in a control) to END."""
        words, items = unpack(code)
        start = next(i for i in items if i['opcode'] in (MUL, MAD) and destination(i)[0] == 0 and destination(i)[1] <= CARRIER_FINAL - 1
                     and any(shader.register_of(w) == (2, light) and shader.swizzle_of(w) == 'xyzw' for w in i['words'][1:]))
        zeroed = 0
        if zero_sun:
            for i in items:
                if i['opcode'] in (MUL, MAD) and destination(i)[0] == 0 and destination(i)[1] <= CARRIER_FINAL - 1:
                    for q, w in enumerate(i['words'][1:], 1):
                        if shader.register_of(w) == (2, light) and shader.swizzle_of(w) == 'xyzw':
                            words[i['dword'] + q] = ZERO; zeroed += 1
        header = [words[0]]
        for i in items:
            if i['opcode'] == DEF: header += words[i['dword']:i['dword'] + i['length'] + 1]
        return pack(header + words[start['dword']:]), zeroed

    def test_float64_oracle_share_times_luma_equals_sun_luma_and_colour_identity(self):
        checked = 0
        for index, name in enumerate(self.covered()):
            row = self.rows[name]; light = row['light']
            for f in range(2):
                variant = self.variant(name, f, 1); control = self.control(name, f, 1)
                lit, _ = self.tail(variant, light, False)
                dark, zeroed = self.tail(variant, light, True)
                self.assertEqual(zeroed, 2 * len(row['seeds']), (name, 'original seed MADs and their parallel seeds'))
                reference, _ = self.tail(control, light, False)
                for branch in ((False, True) if row['xt'] else (False,)):
                    state = initial(index, branch)
                    a = vm.execute(lit, copy.deepcopy(state), stop_after_oc0=False).registers
                    b = vm.execute(dark, copy.deepcopy(state), stop_after_oc0=False).registers
                    c = vm.execute(reference, copy.deepcopy(state), stop_after_oc0=False).registers
                    self.assertEqual(a['oC0'], c['oC0'], (name, f, branch, 'colour and alpha are the control\'s'))
                    self.assertEqual(a['oC2'][0], c['oC2'][0], (name, 'depth is the control\'s'))
                    self.assertEqual(a['oC2'][2:], c['oC2'][2:], (name, 'the clip-w lanes (.b .a) survive the share write'))
                    total, without = luma(a['oC0'][:3]), luma(b['oC0'][:3])
                    share = a['oC2'][1]
                    self.assertGreater(total, without, (name, f, branch))
                    self.assertTrue(0. < share < 1., (name, f, branch, share))
                    self.assertAlmostEqual(share * total, total - without, delta=1e-9 * max(total, 1.), msg=str((name, f, branch)))
                    self.assertEqual(b['oC2'][1], 0., (name, 'zeroed seeds prove exact zero share'))
                    checked += 1
        self.assertEqual(checked, 2 * (94 + 2 * 14))


if __name__ == '__main__':
    unittest.main()
