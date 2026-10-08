"""Host tests of the engine light on the hull (docs/architecture/engine-light.md; gap 8 of
docs/architecture/engine-exhaust-gap-analysis.md).

- the option: schema entry engine_light (on|off, builtin on), the generated header and template, the launcher;
- the portable core (src/proxy/engine_light_core.h) through verification/probe/engine_light_host.cpp: the brightest main
  nozzle per ship (RCS, brake-pushed, other-view and parentless records ignored; ties to the lower handle), the
  256-ship cap (the dimmest entry gives way to a brighter ship and always to the own ship), the twin kinds' share /
  gain / widen match, the one-frame protocol with the motion compensation, degenerate rows, the hold (a ship without a
  record held on its moving hull for engine_light_hold frames by up to four anchors that follow its LOD, faded over the
  last third, dropped when undrawn or expired, re-bound at full strength by a record, competing at the cap with its
  faded brightness, the own ship's flag kept), the VS layouts, the host cost;
- the pixel twins (verification/probe/engine_light_structure.cpp over the local original corpus, skipped without it):
  every non-asteroid reviewed pixel program gets a twin in all eight option sets, the four asteroid programs refuse,
  and each twin is its base variant plus exactly the words re-derived here (one `def c199`, the tier thresholds
  `def c52-c54`, the light block with the per-plate light selection over up to 72 plates, one 3-instruction add per
  fill block), with the slot deltas; the light per plate (the Split Ocelot's two equal nozzles beyond one reach both
  lit; its ten main nozzles all plates, none dropped, all lit; the cap of 72; a one-nozzle ship's registers and
  modelled radiance unchanged); the transformers' slot budget (the device's cap, 32768 where it reports 512);
  the inputs and layouts the twin and the route rely on
  re-derived from the corpus (verification/results/engine-light/eye_normal_registers.py);
- the route's wiring (source checks) and the tracked Wine records (verification/results/bottle-X3/engine-light-gpu.json;
  the route-level seam case seam-engine-light-fixture.json, run_motion_output.py seam-engine-light).
"""
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/config'))
import schema  # noqa: E402
from verification.analysis.test_config_schema import hermetic_launcher, launch_env  # noqa: E402

PROGRAMS = Path(os.environ.get('X3M_SHADER_PROGRAM_DIRECTORY', '/tmp/x3-shader-sweep/programs'))
SETS = ('fill', 'fill0', 'gain', 'widen', 'share', 'share_gain', 'share_widen', 'share0')


def compiler():
    found = shutil.which('clang++') or shutil.which('c++')
    if not found:
        raise RuntimeError('host C++ compiler required')
    return found


# ---------------------------------------------------------------- SM3 tokens (independent of the C++ emitters)
TEMP, INPUT, CONST = 0, 1, 2
SAT = 0x100000


def reg(kind, number):
    return 0x80000000 | ((kind & 7) << 28) | ((kind & 24) << 8) | number


def dst(kind, number, lanes=7):
    return reg(kind, number) | (lanes << 16)


def src(kind, number, swizzle=0xe4, modifier=0):
    return reg(kind, number) | (swizzle << 16) | (modifier << 24)


def lane(kind, number, component, modifier=0):
    return src(kind, number, component * 0x55, modifier)


def op(code, *operands):
    return [(len(operands) << 24) | code, *operands]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


NRM, DP3, MIN, RCP, MUL, MAD, MAX, RSQ, ADD, MOV, CMP = 36, 8, 10, 6, 5, 4, 11, 7, 2, 1, 88


def engine_block(depth, plate=False, select=True):
    """The light block. With `select` (every twin of the corpus) a light per plate: r12 / r13 start as plate 0's light
    (MOV c200 / c201); eleven nested tier levels (if_ne tier, level - 1; level 1 before slot 1, slot 0 without plates,
    level j + 1 at slot runs[j]), the first moving slot 0's plate register c197 into r12; per further slot the squared
    distance u_i in value_eff units, the condition u_i - s in r14.y, CMP of r12 / r13 against the slot's plate register
    c197 - 2i and its colour c198 - 2i (cond >= 0 keeps the earlier), the running minimum s in r14.w (s = u_0 from slot
    0); before the first level closes the selected plate becomes a light (RCP r14.x = 1 / r12.w, r12.xyz *= r14.x, RCP
    r12.w = 1 / r13.w); the law then reads r12 / r13. Without `plate` slot 0 sits inside the first level and every slot
    forms D = e t (MUL); with `plate` the nozzle-plate form: D = e t once, slot 0 ahead of the branches, the weight's
    running minimum in r15.w (MIN, input first, from c198.w), the light from D (ADD) with 1 / d in r15.x, then
    w = saturate(A m + B) and max(w, 0) after q."""
    e, s, a, b, f, k = 14, 15, 200, 201, 202, 199
    la, lb = (12, 12 + 1) if select else (None, None)
    position = (lambda sw: src(TEMP, la, sw)) if select else (lambda sw: src(CONST, a, sw))
    colour = (lambda sw: src(TEMP, lb, sw)) if select else (lambda sw: src(CONST, b, sw))
    words = []
    weight = (op(MAD, dst(TEMP, s, 8) | SAT, lane(TEMP, s, 3), lane(CONST, 198, 0), lane(CONST, 198, 1)) +
              op(MAX, dst(TEMP, s, 8), lane(TEMP, s, 3), lane(CONST, 198, 2))) if plate else []
    to_light = op(MUL, dst(TEMP, s), src(TEMP, s), lane(TEMP, s, 3)) if plate else []
    if select:
        to_light += op(MOV, dst(TEMP, la, 15), src(CONST, a)) + op(MOV, dst(TEMP, lb, 15), src(CONST, b))
        level = 0
        for i in range(PLATE_SLOTS):
            # Uniform branches on the plate tier c202.w: if_ne = opcode 41 with the comparison 5 in bits 16-23, the
            # operands moved into r14.x / r14.y first (if_ne on two temporaries, the corpus's form).
            if level < TIER_LEVELS and i == (PLATE_RUNS[level] if level else (1 if plate else 0)):
                level += 1
                to_light += (op(MOV, dst(TEMP, e, 1), lane(CONST, f, 3)) + op(MOV, dst(TEMP, e, 2), threshold(level, plate)) +
                             [(2 << 24) | (5 << 16) | 41, lane(TEMP, e, 0), lane(TEMP, e, 1)])
                if level == 1:
                    to_light += op(MOV, dst(TEMP, la, 15), src(CONST, plate_register(0)))
            c = plate_register(i)
            if not plate:
                to_light += op(MUL, dst(TEMP, e), src(TEMP, s), lane(TEMP, s, 3))
            u = 3 if i == 0 else 0
            to_light += (op(MAD, dst(TEMP, e), src(TEMP, s if plate else e), lane(CONST, c, 3), src(CONST, c, 0xe4, 1)) +
                         op(DP3, dst(TEMP, e, 1 << u), src(TEMP, e), src(TEMP, e)))
            if plate:
                to_light += op(MIN, dst(TEMP, s, 8), lane(TEMP, e, u), lane(TEMP, s, 3) if i else lane(CONST, 198, 3))
            if i:
                to_light += (op(ADD, dst(TEMP, e, 2), lane(TEMP, e, 0), lane(TEMP, e, 3, 1)) +
                             op(CMP, dst(TEMP, la, 15), lane(TEMP, e, 1), src(TEMP, la), src(CONST, c)) +
                             op(CMP, dst(TEMP, lb, 15), lane(TEMP, e, 1), src(TEMP, lb), src(CONST, colour_register(i))) +
                             op(MIN, dst(TEMP, e, 8), lane(TEMP, e, 0), lane(TEMP, e, 3)))
        to_light += [43] * (level - 1)   # endif of the inner levels
        to_light += (op(RCP, dst(TEMP, e, 1), lane(TEMP, la, 3)) + op(MUL, dst(TEMP, la, 7), src(TEMP, la), lane(TEMP, e, 0)) +
                     op(RCP, dst(TEMP, la, 8), lane(TEMP, lb, 3)) + [43])
    if plate:
        to_light += op(ADD, dst(TEMP, e), position(0xe4), src(TEMP, s, 0xe4, 1))
        inverse = 0
    else:
        to_light += op(MAD, dst(TEMP, e), src(TEMP, s), lane(TEMP, s, 3, 1), position(0xe4))
        inverse = 3
    for w in (op(NRM, dst(TEMP, s), src(INPUT, 2)),
              op(DP3, dst(TEMP, s, 8), src(TEMP, s), src(CONST, f)),
              op(MIN, dst(TEMP, s, 8), lane(TEMP, s, 3), lane(CONST, k, 1)),
              op(RCP, dst(TEMP, s, 8), lane(TEMP, s, 3)),
              op(MUL, dst(TEMP, s, 8), lane(TEMP, s, 3), lane(INPUT, depth, 1)),
              to_light,
              op(DP3, dst(TEMP, e, 8), src(TEMP, e), src(TEMP, e)),
              op(MAX, dst(TEMP, e, 8), lane(TEMP, e, 3), lane(CONST, k, 2)),
              op(RSQ, dst(TEMP, s, 1 << inverse), lane(TEMP, e, 3)),
              op(MUL, dst(TEMP, e), src(TEMP, e), lane(TEMP, s, inverse)),
              op(NRM, dst(TEMP, s), src(INPUT, 3)),
              op(DP3, dst(TEMP, s, 1) | SAT, src(TEMP, s), src(TEMP, e)),
              op(ADD, dst(TEMP, e, 8), lane(TEMP, e, 3, 1), position(0xff)),
              op(MUL, dst(TEMP, e, 8) | SAT, lane(TEMP, e, 3), colour(0xff)),
              weight,
              op(MUL, dst(TEMP, e, 8), lane(TEMP, e, 3), lane(TEMP, e, 3)),
              op(MUL, dst(TEMP, e, 8), lane(TEMP, e, 3), lane(TEMP, s, 0)),
              op(MUL, dst(TEMP, e), lane(TEMP, e, 3), colour(0xe4)),
              op(MAX, dst(TEMP, e), src(TEMP, e), lane(CONST, k, 3))):
        words += w
    return words


ADD_TRIPLET = (op(ADD, dst(TEMP, 13) | SAT, src(TEMP, 12, 0xe4, 1), lane(CONST, 199, 0)) +
               op(MIN, dst(TEMP, 13), src(TEMP, 14), src(TEMP, 13)) + op(ADD, dst(TEMP, 12), src(TEMP, 12), src(TEMP, 13)))
FILL_MAD = op(MAD, dst(TEMP, 12), src(TEMP, 13), lane(CONST, 215, 0), src(TEMP, 12))
DEF_C199 = [0x05000051, dst(CONST, 199, 15), bits(1.0), bits(-2.0 ** -20), bits(2.0 ** -40), bits(0.0)]
# Nozzle plates (docs/architecture/engine-light.md): full suppression to 0.75 x value_eff of each plate (the ship's main
# nozzles, up to 72, slot i's plate register at c197 - 2i), none from 1.0, linear in d^2 between; from the minimum m of
# (d / v_i)^2 over the slots (4 for an unused slot): w = saturate(A m + B), A = -1 / (r1^2 - r0^2), B = r1^2 / (...).
PLATE_FULL, PLATE_REACH, REACH = 0.75, 1.0, 3.0
PLATE_TOP, PLATE_SLOTS, PLATE_NONE = 197, 72, 4.0
# Plate cap (engine-light.md "Plate cap"): tier k runs PLATE_RUNS[k] slots; a light per plate (user decision
# 2026-10-08): plate 0's at c200-c201, plate i's colour (colour, 1 / R^2) at c198 - 2i beside its plate register; the
# tier thresholds 2 .. 10 in the shader-local c52-c54.
PLATE_RUNS = (1, 4, 8, 12, 16, 24, 32, 40, 48, 56, 64, 72)
TIER_LEVELS, TIER_FIRST = len(PLATE_RUNS) - 1, 52


def plate_register(i):
    return PLATE_TOP - 2 * i


def colour_register(i):
    return PLATE_TOP + 1 - 2 * i


def threshold(level, plate):
    """The tier threshold of level `level` (1-based): tier != level - 1."""
    k = level - 1
    if k == 0:
        return lane(CONST, 198, 2) if plate else lane(CONST, 199, 3)
    if k == 1:
        return lane(CONST, 199, 0)
    return lane(CONST, TIER_FIRST + (k - 2) // 4, (k - 2) % 4)


def tier(count):
    return next(k for k, run in enumerate(PLATE_RUNS) if run >= count or k == len(PLATE_RUNS) - 1)


DEF_TIERS = [w for r in range(3) for w in [0x05000051, dst(CONST, TIER_FIRST + r, 15)] +
             [bits(float(2 + 4 * r + l) if 2 + 4 * r + l < TIER_LEVELS else 0.0) for l in range(4)]]
PLATE_A = -1.0 / (PLATE_REACH ** 2 - PLATE_FULL ** 2)
PLATE_B = PLATE_REACH ** 2 / (PLATE_REACH ** 2 - PLATE_FULL ** 2)
DEF_C198 = [0x05000051, dst(CONST, 198, 15), bits(PLATE_A), bits(PLATE_B), bits(0.0), bits(PLATE_NONE)]
GAIN_DYNAMIC = lane(CONST, 217, 3)


def plate_gain(reg, gain=GAIN_DYNAMIC):
    return (op(MAD, dst(TEMP, 15, 8), lane(TEMP, 15, 3, 1), gain, lane(TEMP, 15, 3)) +
            op(ADD, dst(TEMP, 15, 8), lane(TEMP, 15, 3), gain) +
            op(MUL, dst(TEMP, reg), src(TEMP, reg), lane(TEMP, 15, 3)))


def plain_gain(reg, gain=GAIN_DYNAMIC):
    return op(MUL, dst(TEMP, reg), src(TEMP, reg), gain)


def find_all(words, pattern):
    hits, n = [], len(pattern)
    first = pattern[0]
    for i in range(len(words) - n + 1):
        if words[i] == first and words[i:i + n] == pattern:
            hits.append(i)
    return hits


def remove(words, at, n):
    return words[:at] + words[at + n:]


class OptionTests(unittest.TestCase):
    def test_schema_entry(self):
        e = schema.BY_KEY['engine_light']
        self.assertEqual((e['env'], e['type'], e['section'], e['default'], e['builtin'], e['launcher'], e['developer'], e['choices']),
                         ('X3M_ENGINE_LIGHT', 'enum', 'engine', None, 'on', '--engine-light', False, ('on', 'off')))
        self.assertIn('{"X3M_ENGINE_LIGHT", "engine_light", Type::Enum,', (ROOT / 'src/config/config_schema_inc.h').read_text())
        self.assertIn(';engine_light = on', (ROOT / 'assets/x3m.ini').read_text())
        check = subprocess.run([sys.executable, str(ROOT / 'tools/config/generate.py'), '--check'], capture_output=True, text=True)
        self.assertEqual(check.returncode, 0, check.stdout + check.stderr)

    def test_launcher(self):
        module, game, wine, directory = hermetic_launcher()
        with directory:
            self.assertNotIn('X3M_ENGINE_LIGHT', launch_env(module, game, wine))
            self.assertNotIn('X3M_ENGINE_LIGHT', launch_env(module, game, wine, '--engine-effects', 'plumes'))
            for value in ('on', 'off'):
                env = launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-light', value)
                self.assertEqual(env['X3M_ENGINE_LIGHT'], value)
            with self.assertRaises(SystemExit):
                launch_env(module, game, wine, '--engine-light', 'maybe')
            with self.assertRaises(SystemExit):
                launch_env(module, game, wine, '--vanilla', '--engine-light', 'on')

    def test_hold_schema_entry(self):
        e = schema.BY_KEY['engine_light_hold']
        self.assertEqual((e['env'], e['type'], e['section'], e['default'], e['builtin'], e['launcher'], e['developer']),
                         ('X3M_ENGINE_LIGHT_HOLD', 'int', 'engine', None, '60', '--engine-light-hold', False))
        self.assertEqual(e['range'], ((0.0, 600.0, False),))
        self.assertIn('{"X3M_ENGINE_LIGHT_HOLD", "engine_light_hold", Type::Int,', (ROOT / 'src/config/config_schema_inc.h').read_text())
        self.assertIn(';engine_light_hold = 60', (ROOT / 'assets/x3m.ini').read_text())

    def test_hold_launcher(self):
        module, game, wine, directory = hermetic_launcher()
        with directory:
            self.assertNotIn('X3M_ENGINE_LIGHT_HOLD', launch_env(module, game, wine))
            for value in ('0', '60', '600'):
                env = launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-light-hold', value)
                self.assertEqual(env['X3M_ENGINE_LIGHT_HOLD'], value)
            for value in ('-1', '601'):
                with self.assertRaises(SystemExit):
                    launch_env(module, game, wine, '--engine-light-hold', value)
            with self.assertRaises(SystemExit):
                launch_env(module, game, wine, '--vanilla', '--engine-light-hold', '60')

    def test_read_once_at_configure(self):
        inc = (ROOT / 'src/proxy/motion_output_engine_light_inc.h').read_text()
        self.assertEqual(inc.count('config::get(L"X3M_ENGINE_LIGHT"'), 1)
        self.assertEqual(inc.count('config::get(L"X3M_ENGINE_LIGHT_HOLD"'), 1)
        self.assertIn('hold=%u hold_setting=%s hold_status=%s', inc)
        self.assertIn('void MotionOutput::configure_engine_light(bool plumes)', inc)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        # After the material options the twins compose with.
        self.assertLess(capture.index('configure_hull_emissive_widening('), capture.index('configure_engine_light('))


class CoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix='x3-engine-light-')
        cls.addClassCleanup(temporary.cleanup)
        exe = Path(temporary.name) / 'host'
        build = subprocess.run([compiler(), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/engine_light_host.cpp'), '-o', str(exe)], capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.r = json.loads(run.stdout)

    def test_option(self):
        self.assertEqual(self.r['option'], dict(on=1, off=1, refused=1, default='on'))

    def test_brightest_main_nozzle(self):
        s = self.r['selection']
        self.assertEqual((s['ships'], s['found'], s['other_ship']), (1, 0, -1))
        self.assertEqual(s['handle'], 10)                                # the tie with handle 12 goes to the lower handle
        self.assertEqual(s['position'], [-5.0, 0.0, -15.0])               # 0.5 x value behind the nozzle along the axis
        self.assertEqual(s['colour'], [1.0, 1.0, 1.0])                    # white x I(1) 4 x preset 1 x 0.25
        self.assertEqual((s['radius'], s['brightness']), (30.0, 40.0))
        self.assertEqual((s['main'], s['rcs'], s['brake'], s['other_view'], s['orphan']), (3, 1, 1, 1, 1))

    def test_layer_plate_unfloored(self):
        # After Run 129 A: a smaller co-located layer takes no floor, so its plate sits at its natural value while the
        # larger record's plate keeps its floored value (the Split Scorpion's nor 10 + tiny 5.04 at R 67.3).
        q = self.r['layer_floor']
        self.assertEqual((q['count'], q['handles'], q['unfloored']), (2, [70, 71], 1))
        self.assertGreater(q['nor_floored'], 10.0)
        self.assertAlmostEqual(q['values'][0], q['nor_floored'], places=5)
        self.assertAlmostEqual(q['values'][1], 5.04, places=5)

    def test_nozzle_plates(self):
        # Every main nozzle of the ship, brightest first, one per handle (the brighter record), the smaller co-located
        # layer a plate at its natural value (unfloored, after Run 129 A), the RCS jet never; the ship's own light
        # fields are plate 0's (c200-c201), and every plate carries its own light (test_light_per_plate).
        p = self.r['plates']
        self.assertEqual((p['a']['handle'], p['a']['count'], p['a']['handles']), (50, 4, [50, 51, 52, 53]))
        self.assertEqual(p['a']['values'], [10.0, 8.0, 6.0, 5.0])
        self.assertEqual(p['a']['first'], [0.0, 0.0, -5.0])      # 0.5 x value behind the nozzle along -z, as the light
        self.assertEqual(p['a']['light'], p['a']['first'])
        # Nine nozzles: nine plates (values 18 .. 10), none dropped (the cap is 72; eight before 2026-10-08).
        self.assertEqual((p['b']['handle'], p['b']['count'], p['b']['handles']), (68, 9, list(range(68, 59, -1))))
        self.assertEqual((p['unfloored'], p['plates_dropped']), (1, 0))
        # Per-draw registers of the run (tier 1: four slots): ((P - cam) / v, 1 / v) slot 0 first, each further slot's
        # colour (colour, 1 / R^2), R = 3 v; slot 4 (beyond the run) not written.
        self.assertEqual((p['ok'], p['node_count'], p['tier'], p['beyond_untouched']), (1, 4, 1.0, 1))
        expected = [0, 0, -0.5, 0.1, 5, 0, -0.5, 0.125, 0, 40 / 6, -0.5, 1 / 6, 0, 0, -1.1, 0.2]
        colours = [1, 1, 1, 1 / 24 ** 2, 1, 1, 1, 1 / 18 ** 2, 1, 1, 1, 1 / 15 ** 2]
        self.assertEqual((len(p['registers']), len(p['colours'])), (len(expected), len(colours)))
        for got, want in zip(p['registers'] + p['colours'], expected + colours):
            self.assertAlmostEqual(got, want, places=6)
        # A non-finite plate in a slot the twin runs is a pad (slot 0's plate register, plate 0's colour: neither the
        # weight nor the light selection changes); a non-finite first plate runs slot 0 alone (tier 0) with the unused
        # plate (2, 0, 0, 0): plate 0's light, no plate weight.
        self.assertEqual(p['broken_pad'], 1)
        self.assertEqual(p['broken_first'], [1, 0.0, 2, 0, 0, 0])
        # The tiers by plate count 0 .. 73 and their runs: 1, then steps of four to 16 and of eight to 72.
        counts = range(0, 74)
        self.assertEqual(p['tiers'], [tier(c) for c in counts])
        self.assertEqual(p['runs'], [PLATE_RUNS[tier(c)] for c in counts])
        self.assertEqual(p['runs'][:18], [1, 1, 4, 4, 4, 8, 8, 8, 8, 12, 12, 12, 12, 16, 16, 16, 16, 24])
        self.assertEqual(p['runs'][66:], [72] * 8)
        core = (ROOT / 'src/proxy/engine_light_core.h').read_text()
        self.assertIn('constexpr float unused_plate[4] = {2.f, 0.f, 0.f, 0.f};', core)
        self.assertIn('constexpr unsigned plate_slots = 72;', core)
        self.assertIn('constexpr unsigned plate_runs[plate_tiers] = {1, 4, 8, 12, 16, 24, 32, 40, 48, 56, 64, 72};', core)
        inc = (ROOT / 'src/renderer/linear_engine_light_inc.h').read_text()
        self.assertIn('engine_light_plate_constant = 198, engine_light_plate_top = 197, engine_light_plate_slots = 72,', inc)
        self.assertIn('engine_light_plate_tiers = 12, engine_light_selected = 12;', inc)
        self.assertIn('nozzle_plate_none = 4.0f;', inc)
        abi = (ROOT / 'src/renderer/linear_material.h').read_text()
        self.assertIn('static constexpr unsigned plate_runs[tier_count] = {1, 4, 8, 12, 16, 24, 32, 40, 48, 56, 64, 72};', abi)

    def test_light_per_plate(self):
        # User decision 2026-10-08: the Split Ocelot's two equal secondary nozzles (value_eff 548.076, 2,411 apart, beyond
        # one reach 3 x 548.076 = 1,644; Run 134 A triage): both plates carry their own light, equal colour and R^2, the
        # tie's lower handle plate 0 (c200-c201), the other plate 1 (its plate register c195 and colour c196); slots 2-3
        # (run, tier 1) pads of slot 0, slot 4 on not written. The twin's selection modelled in float: a hull point 300
        # units beside each light is lit, while plate 0's light alone (the rule before) leaves the second nozzle dark.
        q = self.r['per_plate']
        self.assertEqual((q['ok'], q['count'], q['handles'], q['light_handle'], q['node_plates'], q['tier']),
                         (1, 2, [80, 81], 80, 2, 1.0))
        self.assertAlmostEqual(q['r2'][0], (3 * 548.076) ** 2, delta=1.0)
        self.assertAlmostEqual(q['r2'][0], q['r2'][1], delta=q['r2'][0] * 1e-6)   # 1 / (1 / R^2) in float
        self.assertAlmostEqual(q['distance'], 2411.0, places=2)
        self.assertGreater(q['distance'], REACH * 548.076)
        self.assertEqual((q['colour_equal'], q['keys_pad'], q['colours_pad'], q['skipped_untouched'], q['upload_run']),
                         (1, 1, 1, 1, 4))
        self.assertGreater(q['radiance'][0], 0.5)
        self.assertGreater(q['radiance'][1], 0.5)
        self.assertAlmostEqual(q['radiance'][0], q['radiance'][1], places=5)
        self.assertEqual(q['before'][1], 0.0)
        # One nozzle: c200-c202 and c197 exactly as draw_constants / plate_register_values give them, nothing else
        # written, the upload c197 alone below the light (four registers in two calls), and the modelled radiance with
        # the selection the single light's bit for bit.
        o = self.r['single']
        self.assertEqual((o['ok'], o['light_identical'], o['plates_identical'], o['lights_untouched'], o['upload_run'],
                          o['upload_first']), (1, 1, 1, 1, 1, 197))
        self.assertEqual(o['radiance_identical'], o['checked'])

    def test_many_nozzles(self):
        # Plate cap (engine-light.md "Plate cap"; Run 135 A triage): the Split Ocelot's ten main nozzles (two huge at
        # value_eff 939.2, eight big3 at 548.1, the big3 a brightness tie): ten plates, none dropped (the cap of eight
        # dropped 0x56e / 0x56f), tier 3 (run 12: two pads), every plate lit by its own light at a hull point beside it.
        m = self.r['many']
        o = m['ocelot']
        self.assertEqual((o['ok'], o['count'], o['plates_dropped'], o['node_plates'], o['tier'], o['run']),
                         (1, 10, 0, 10, 3.0, 12))
        self.assertEqual((o['lit'], o['own_light'], o['pads'], o['beyond_untouched']), (10, 10, 2, 1))
        self.assertEqual((o['upload_first'], o['upload_count']), (175, 23))
        self.assertEqual((o['first_handles'], o['last_handles']), ([0x564, 0x565], [0x56c, 0x56d]))
        self.assertGreater(o['least'], 0.5)
        self.assertGreaterEqual(o['dark_before'], 2)   # plate 0's light alone leaves the far plates dark
        # The cap: 72 nozzles all plates and lit (tier 11, run 72: c55-c197, 143 registers); 80: the 72 brightest.
        c = m['cap']
        self.assertEqual((c['ok'], c['count'], c['plates_dropped'], c['node_plates'], c['tier'], c['run'], c['lit'],
                          c['own_light'], c['pads']), (1, 72, 0, 72, 11.0, 72, 72, 72, 0))
        self.assertEqual((c['upload_first'], c['upload_count'], c['beyond_untouched']), (55, 143, 1))
        v = m['over']
        self.assertEqual((v['count'], v['plates_dropped'], v['lit'], v['first_handles'], v['last_handles']),
                         (72, 8, 72, [0x904f, 0x904e], [0x9009, 0x9008]))

    def test_cap(self):
        self.assertEqual(self.r['cap'], dict(ships=256, dropped=44, found=256))   # equal brightness: the incumbents stay

    def test_cap_replaces_the_dimmest_and_admits_the_own_ship(self):
        # 299 ships plus the own ship last and dimmest in ring order: in either brightness order the own ship is in, the
        # other 255 entries are the brightest (ranks 44..298), 44 lights dropped, every entry still found by its hash.
        for order in ('ascending', 'descending'):
            self.assertEqual(self.r['replace'][order], dict(ships=256, dropped=44, own=1, own_flag=1, others=255,
                                                            dimmest_rank=44, consistent=256), order)
        self.assertEqual(self.r['replace']['untagged_own'], 0)   # without the own tags the dimmest newcomer is refused

    def test_twin_kinds_match_their_base(self):
        # Each kind accepted with its base's share / gain / widen only; any flag flipped (or the light not applied) is
        # refused; the three share kinds refuse a twin whose share plan failed only with the light.
        self.assertEqual(self.r['twins'], dict(kinds=7, accepted=7, flipped_accepted=0, share_lost_refused=3, out_of_range=0))
        core = (ROOT / 'src/proxy/engine_light_core.h').read_text()
        table = core[core.index('constexpr TwinOptions twin_options'):core.index('inline bool twin_matches_base')]
        inc = (ROOT / 'src/proxy/motion_output_engine_light_inc.h').read_text()
        create = inc[inc.index('void MotionOutput::engine_light_create_twins'):inc.index('void MotionOutput::engine_light_release')]
        # The table agrees with the options the twins are built with (share 4-6, gain 2, 3, 5, 6, widen 3, 6).
        self.assertIn('for (unsigned k : {2u, 3u, 5u, 6u}) {', create)
        self.assertIn('for (unsigned k : {3u, 6u}) options[k].widen = &widen;', create)
        self.assertIn('for (unsigned k : {4u, 5u, 6u}) options[k].share = true;', create)
        flags = [tuple(v == 'true' for v in m) for m in re.findall(r'\{(true|false), (true|false), (true|false)\}', table)]
        self.assertEqual(flags, [(False, False, False), (False, False, False), (False, True, False), (False, True, True),
                                 (True, False, False), (True, True, False), (True, True, True)])
        self.assertIn('depth_enabled_, applied, &share,', create)
        self.assertIn('if (!engine_light::core::twin_matches_base(k, applied, share, gain, widened)) {', create)
        self.assertIn('mismatched=%02x', create)

    def test_one_frame_protocol_rides_on_the_hull(self):
        p = self.r['protocol']
        self.assertEqual((p['lit_before'], p['logged'], p['nodes'], p['ships'], p['found'], p['other'], p['wrong_handle'], p['placed']),
                         (0, 2, 1, 1, 1, 0, 0, 1))
        self.assertLess(p['error'], 0.02)    # float record origins at 1.5e5 world units (ulp 0.016)
        c = p['constants']
        self.assertEqual(c[3], 24.0 ** 2)    # R = 3 x 8
        self.assertEqual(c[8:12], [1.0, 0.0, 0.0, 0.0])
        d = self.r['degenerate']
        self.assertEqual((d['view'], d['singular_nodes'], d['singular']), (1, 0, 1))

    def test_hold(self):
        h = self.r['hold']
        self.assertEqual(h['window'], 60)
        # Bound with two plates; two anchors, the root's own draw (handle 4) first though logged after its child's.
        self.assertEqual(h['bound'], dict(ships=1, plates=2, anchors=2, anchor_root=1, anchor_handle=4, age=0, fade=1, own=1, node=1,
                                          node_plates=2))
        ages = {a['age']: a for a in h['ages']}
        self.assertEqual(sorted(ages), [1, 20, 40, 41, 50, 60])
        for age, a in ages.items():
            # Held while the hull is drawn, the own ship's flag kept, both plates in the child node's table.
            self.assertEqual((a['ships'], a['held'], a['found'], a['entry_age'], a['own'], a['node_plates']), (1, 1, 1, age, 1, 2), a)
            # The hull moved and turned (the stale world position is metres off) and the held plate rides on it: its
            # world position is the model point through this frame's rows and the child node keeps its model point.
            self.assertGreater(a['moved'], 5.0)
            self.assertLess(a['world_error'], 1e-3)
            self.assertLess(a['node_error'], 1e-3)
            # The fade: 1 through age 40, then (61 - age) / 21; colour, value_eff, the other plate's colour and the
            # radius all scaled by it.
            expect = 1.0 if age <= 40 else (61 - age) / 21
            self.assertAlmostEqual(a['fade'], expect, places=6)
            self.assertAlmostEqual(a['expect_fade'], expect, places=6)
            for k in ('colour', 'value', 'plate_colour', 'radius'):
                self.assertAlmostEqual(a[k], expect, places=5, msg=(age, k))
        self.assertEqual(ages[40]['fade'], 1.0)
        self.assertLess(ages[60]['fade'], 0.05)
        # Gone the frame after the window, counted expired; 60 held frames in all.
        self.assertEqual(h['expired'], dict(ships=0, held=0, hold_expired=1, nodes=0))
        self.assertEqual(h['held_frames'], 60)
        # Not drawn: dropped at once, not expired.
        self.assertEqual(h['undrawn'], dict(ships=0, held=0, hold_expired=0, nodes=0))
        # A fresh record re-binds at full strength.
        b = h['rebind']
        self.assertAlmostEqual(b['fade_before'], 11 / 21, places=6)
        self.assertEqual((b['ships'], b['held'], b['age'], b['fade'], b['own'], b['colour'], b['value']), (1, 0, 0, 1, 1, 1, 1))
        self.assertLess(b['world_error'], 1e-3)
        self.assertLess(b['node_error'], 1e-3)
        # Window 0: today's rule, no anchor taken.
        self.assertEqual(h['window0'], dict(anchor=0, ships=0, held=0, nodes=0))
        # Anchors across a LOD switch: the root's handle changes (held by the child), then the child's (held by the
        # root's new handle, the set having followed), then both (no anchor drawn: dropped); the plate stays on the hull.
        lod = h['lod']
        self.assertEqual((lod['bound_anchors'], lod['held'], lod['first_handle']), (2, [1, 1, 0], [5, 5, 0]))
        self.assertLess(max(lod['world_error'][:2]), 1e-3)
        self.assertGreaterEqual(min(lod['world_error'][:2]), 0.0)
        # A singular first anchor falls through to the next.
        self.assertEqual(h['singular_anchor']['held'], 1)
        self.assertLess(h['singular_anchor']['world_error'], 1e-3)
        # The cap: a held entry competes with brightness x fade (plate 0's value 8 against 256 records of 20, 1 or 4);
        # it enters a full table only in place of a dimmer evictable entry or as the own ship (dropped counted either
        # way). Value 4 loses to the held ship at fade 1 and beats it at age 55 (fade 6/21: 8 x 0.286 = 2.3).
        cap = h['cap']
        def row(held, own, fade):
            return dict(ships=256, held=held, dropped=1, found=held, own=own if held else -1, fade=fade)
        self.assertEqual(cap['brighter'], row(0, 0, 1))
        self.assertEqual(cap['dimmer'], row(1, 0, 1))
        self.assertEqual(cap['unfaded_wins'], row(1, 0, 1))
        self.assertEqual({k: v for k, v in cap['faded_loses'].items() if k != 'fade'}, {k: v for k, v in row(0, 0, 0).items() if k != 'fade'})
        self.assertAlmostEqual(cap['faded_loses']['fade'], 6 / 21, places=6)
        self.assertEqual({k: v for k, v in cap['own_faded'].items() if k != 'fade'}, {k: v for k, v in row(1, 1, 0).items() if k != 'fade'})
        self.assertEqual(h['parse'], dict(zero=1, max=1, refused=1))
        law = h['fade_law']
        self.assertEqual(law[:5], [0.5, 1.0, 0.5, 0.5, 1.0])
        self.assertAlmostEqual(law[5], 200 / 201, places=6)
        # Cost: the worst case (256 held ships of 72 plates) carries every entry; bounded by the table, no per-draw work.
        c = h['cost']
        self.assertEqual((c['held'], c['plates']), (256, 72))
        self.assertLess(c['hold_us'], 500.0)
        self.assertLess(c['anchor_us'], 100.0)

    def test_layouts(self):
        self.assertEqual(self.r['layout'], dict(loop=[28, 34], single=[7, 13], unknown=[0, 0]))

    def test_host_cost(self):
        c = self.r['cost']
        self.assertEqual((c['nodes'], c['ships']), (1024, 256))
        self.assertLess(abs(c['hits'] - c['rounds'] / 2), c['rounds'] / 100)   # half the probes hit
        self.assertLess(c['ns_per_draw'], 200.0)
        # Ships of eight main nozzles: every plate's register and colour per hit.
        self.assertEqual((c['hits8'], c['plates8']), (c['hits'], 8))
        self.assertLess(c['ns_per_draw8'], 200.0)
        # Ships of 72 (the cap): the per-draw work grows with the plates in use, bounded proportionally (200 ns per eight
        # plates); the node build over 1,024 logged draws per plate and node under 50 ns; the ship table over the ring's
        # 1,024 records (14 ships x 72: each record's sorted insertion moves up to 71 plates) within 72 / 8 of the
        # 128 x 8 build.
        self.assertEqual((c['hits72'], c['plates72']), (c['hits'], 72))
        self.assertLess(c['ns_per_draw72'], 200.0 * 72 / 8)
        self.assertLess(c['build_us72'] * 1e3 / (c['nodes'] * 72), 50.0)
        self.assertLess(c['ships_us72'], c['ships_us8'] * 72 / 8)

    def test_slot_budget(self):
        # AGENTS.md "Shader slot budget": the transformers check against the device's MaxPixelShader30InstructionSlots,
        # 32768 where it reports the 512 spec minimum (or less), never above 32768; no hard-coded 512 remains.
        self.assertEqual(self.r['slot_budget'], {'0': 32768, '512': 32768, '513': 513, '4096': 4096, '32768': 32768,
                                                 '65535': 32768, 'default': 32768})
        material = (ROOT / 'src/renderer/linear_material.cpp').read_text()
        emission = (ROOT / 'src/renderer/linear_emission.cpp').read_text()
        self.assertNotIn('slots <= 512', material)
        self.assertNotIn('sm3_pixel_slots', emission)
        self.assertIn('slots <= ps3_slot_budget()', material)
        self.assertIn('slots <= ps3_slot_budget();', emission)
        cpp = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('renderer::set_ps3_slot_budget(reported)', cpp)
        self.assertIn('ps3_slot_budget device=%llu ps30_slots=%lu budget=%lu rule=%s', cpp)
        inc = (ROOT / 'src/proxy/motion_output_engine_light_inc.h').read_text()
        self.assertIn('slots_max=%lu slot_budget=%lu', inc)


class TwinTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.derived = json.loads(subprocess.run([sys.executable, str(ROOT / 'verification/results/engine-light/eye_normal_registers.py')],
                                                capture_output=True, text=True, check=True).stdout)
        if not cls.derived.get('available'):
            reason = f'local original corpus unavailable under {PROGRAMS} (X3M_SHADER_PROGRAM_DIRECTORY)'
            if os.environ.get('X3M_REQUIRE_SHADER_CORPUS') == '1':
                raise AssertionError(reason)
            raise unittest.SkipTest(reason)
        temporary = tempfile.TemporaryDirectory(prefix='x3-engine-light-twins-')
        cls.addClassCleanup(temporary.cleanup)
        cls.out = Path(temporary.name)
        exe = cls.out / 'structure'
        build = subprocess.run([compiler(), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/engine_light_structure.cpp'), str(ROOT / 'src/renderer/linear_material.cpp'),
                                str(ROOT / 'src/renderer/material_motion.cpp'), '-o', str(exe)], capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(exe), str(PROGRAMS), str(cls.out)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.driver = json.loads(run.stdout)
        cls.rows = {row['name']: row for row in cls.driver['rows']}

    def reviewed(self):
        return {'ps_' + ps: row for ps, row in self.derived['rows'].items()}

    def test_inputs_and_layouts_rederived(self):
        d = self.derived
        self.assertEqual((d['pairs'], d['pixel_programs'], d['inconsistent']), (168, 108, []))
        for row in d['census']:
            if row['family'] != 'asteroid':
                self.assertEqual((row['eye_texcoord'], row['normal_texcoord'], row['eye_input'], row['normal_input']), (1, 2, 2, 3), row)
        self.assertEqual(sum(r['programs'] for r in d['census'] if r['family'] != 'asteroid'), 104)
        core = (ROOT / 'src/proxy/engine_light_core.h').read_text()
        for vs, layout in d['layouts'].items():
            expected = {(28, 29, 30, 34, 35, 36): (28, 34), (7, 8, 9, 13, 14, 15): (7, 13)}[tuple(layout['world'] + layout['view_inverse'])]
            block = core[core.index('static const std::uint64_t loop[]'):core.index('for (const auto h : loop)')]
            listed_loop = ('0x%sull' % vs) in block.split('single[]')[0]
            listed_single = ('0x%sull' % vs) in block.split('single[]')[1]
            self.assertEqual((listed_loop, listed_single), (expected == (28, 34), expected == (7, 13)), vs)
        self.assertEqual(len(d['layouts']), 29)

    def test_coverage(self):
        reviewed = self.reviewed()
        for name, row in self.rows.items():
            for set_name in SETS:
                entry = row['sets'][set_name]
                if name in reviewed and reviewed[name]['family'] != 'asteroid':
                    self.assertEqual((entry['status'], entry['engine']), (0, 1), (name, set_name))
                elif name in reviewed:
                    self.assertEqual(entry['engine'], 0, (name, set_name))
                else:
                    self.assertEqual(entry['engine'], 0, (name, set_name))
        self.assertEqual(self.driver['twins'], 104 * len(SETS))

    def test_twin_is_base_plus_the_engine_words(self):
        reviewed = self.reviewed()
        checked = plated = 0
        for name, row in reviewed.items():
            if row['family'] == 'asteroid':
                continue
            for set_name in SETS:
                entry = self.rows[name]['sets'][set_name]
                base = list(struct.unpack('<%dI' % ((self.out / f'{name}-{set_name}-base.bin').stat().st_size // 4),
                                          (self.out / f'{name}-{set_name}-base.bin').read_bytes()))
                twin = list(struct.unpack('<%dI' % ((self.out / f'{name}-{set_name}-twin.bin').stat().st_size // 4),
                                          (self.out / f'{name}-{set_name}-twin.bin').read_bytes()))
                share = set_name.startswith('share')
                fill0 = set_name.endswith('0')
                # A gained twin carries the nozzle-plate weight (every gained program: the light's site precedes the
                # light-map fetch, both at flow-control depth 0).
                plate = entry['gain'] == 1
                blocks = {depth: engine_block(depth, plate) for depth in range(10)}
                found = [(d, find_all(twin, b)) for d, b in blocks.items()]
                found = [(d, hits) for d, hits in found if hits]
                self.assertEqual(len(found), 1, (name, set_name))
                depth, hits = found[0]
                self.assertEqual(len(hits), 1, (name, set_name))
                self.assertIn(depth, (5, 6, 7, 8, 9), (name, set_name))   # the motion transformer's depth input
                self.assertEqual(len(find_all(twin, DEF_C199)), 1, (name, set_name))
                self.assertEqual(len(find_all(twin, DEF_C198)), 1 if plate else 0, (name, set_name))
                self.assertEqual(len(find_all(twin, DEF_TIERS)), 1, (name, set_name))   # every twin selects
                triplets = find_all(twin, ADD_TRIPLET)
                self.assertEqual(len(triplets), 2 if share else 1, (name, set_name))
                delta = entry['twin_slots'] - entry['base_slots']
                # A light per plate over up to 72 plates: the selecting block without plates 594 slots (572 more than
                # the single light's 22: two MOVs into r12 / r13, eleven levels of two MOVs, if_ne x 3 and endif, the
                # MOV of slot 0's plate register, slot 0 three, slots 1-71 seven each, the light from the selected plate
                # three); the plate form 597 (the same and D once, slot 0's MIN, the weight's two, slots without the MUL).
                if fill0:
                    self.assertEqual(delta, (70 if share else 45) + 572, (name, set_name))
                    continue
                # The plate form: 575 more block slots than the single light, the gain 3 for 1.
                self.assertEqual(delta, (28 if share else 25) + (577 if plate else 572), (name, set_name))
                for at in triplets:   # each add follows its fill block's MAD
                    self.assertEqual(twin[at - len(FILL_MAD):at], FILL_MAD, (name, set_name))
                stripped = twin
                for pattern in [blocks[depth], DEF_C199, DEF_TIERS] + [DEF_C198] * plate + [ADD_TRIPLET] * len(triplets):
                    at = find_all(stripped, pattern)[0]
                    stripped = remove(stripped, at, len(pattern))
                if plate:
                    # The plate gain sequence after the light-map fetch, in place of the base's gain MUL, after the block.
                    hits = [(reg, at) for reg in range(8) for at in find_all(stripped, plate_gain(reg))]
                    self.assertEqual(len(hits), 1, (name, set_name))
                    reg, at = hits[0]
                    self.assertLess(find_all(twin, blocks[depth])[0], find_all(twin, plate_gain(reg))[0], (name, set_name))
                    stripped = stripped[:at] + plain_gain(reg) + stripped[at + len(plate_gain(reg)):]
                    plated += 1
                if set_name.endswith('widen'):
                    # The widening's three temporaries sit one above the program's highest, which r14/r15 raise: the
                    # only other difference is that consistent renumbering (temporary registers only).
                    self.assertEqual(len(stripped), len(base), (name, set_name))
                    shifts = set()
                    for b, t in zip(base, stripped):
                        if b != t:
                            self.assertEqual((b & ~0x7ff, (b >> 28) & 7, (b >> 11) & 3), (t & ~0x7ff, 0, 0), (name, set_name))
                            shifts.add((t & 0x7ff) - (b & 0x7ff))
                    self.assertLessEqual(len(shifts), 1, (name, set_name))
                    renamed = {}
                    for b, t in zip(base, stripped):
                        if b != t:
                            renamed.setdefault(b & 0x7ff, t & 0x7ff)
                            self.assertEqual(renamed[b & 0x7ff], t & 0x7ff, (name, set_name))
                    self.assertTrue(all(t >= 16 for t in renamed.values()), (name, set_name))
                else:
                    self.assertEqual(stripped, base, (name, set_name))
                checked += 1
        self.assertEqual(checked, 104 * 6)
        self.assertEqual(plated, 100 * 4)   # every gained twin (the four glass programs have no light-map gain)

    def test_plate_constants_pinned(self):
        # The pixel program's reach ratio is the core's reach; the plate radii as documented.
        inc = (ROOT / 'src/renderer/linear_engine_light_inc.h').read_text()
        self.assertIn('constexpr float engine_light_reach_ratio = 3.0f, nozzle_plate_full = 0.75f, nozzle_plate_reach = 1.0f;', inc)
        core = (ROOT / 'src/proxy/engine_light_core.h').read_text()
        self.assertEqual(float(re.search(r'reach = ([0-9.]+)f', core).group(1)), REACH)


class RouteWiringTests(unittest.TestCase):
    def test_route(self):
        cpp = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('#include "motion_output_engine_light_inc.h"', cpp)
        # The tables are built from the previous frame's ring before engine_effects_frame_begin clears it.
        self.assertLess(cpp.index('if (engine_light_requested_) engine_light_frame();'), cpp.index('if (engine_hook_) engine_effects_frame_begin();'))
        # Per draw: decided before the pair is bound, the twin swapped in at the end of the selection, the constants uploaded
        # in the apply chain right after the motion ABI (a failure rolls the route back).
        prepare = cpp.index('engine_light_prepare(route, material);')
        self.assertLess(prepare, cpp.index('HRESULT hr = bind_variant_pair(route, material);'))
        self.assertIn('if (IDirect3DPixelShader9* twin = engine_light_twin(ps))', cpp)
        self.assertIn('route.engine_light = engine && SUCCEEDED(hr);', cpp)
        upload = cpp.index('if (SUCCEEDED(hr) && route.engine_light) hr = engine_light_upload();')
        self.assertLess(cpp.index('renderer::MaterialMotionAbi::pixel_coordinates_constant,'), upload)
        self.assertLess(upload, cpp.index('if (SUCCEEDED(hr)) hr = bind_targets(route);'))
        self.assertIn('engine_light_create_twins(entry, code, bytes, hash);', cpp)
        self.assertIn('engine_light_release(entry.second);', cpp)
        inc = (ROOT / 'src/proxy/motion_output_engine_light_inc.h').read_text()
        self.assertNotIn('new ', inc.split('void MotionOutput::engine_light_prepare')[1])   # no allocation per draw
        # Two uploads, the run of the draw's tier (its plates and colours, 2 run - 1 registers ending at c197) and the
        # light c200-c202: the API never writes c52-c54 or c198-c199 (the twins' DEFs).
        upload_body = inc[inc.index('HRESULT MotionOutput::engine_light_upload()'):inc.index('void MotionOutput::engine_light_prepare')]
        self.assertEqual(upload_body.count('direct_call<SetConstantsFFn>(SetPixelShaderConstantF,'), 2)
        self.assertIn('const unsigned run = el::block_upload_run(engine_light_->constants), first = Abi::upload_first(run);', upload_body)
        self.assertIn('engine_light_->constants + (first - Abi::light_constant) * 4,', upload_body)
        self.assertIn('Abi::upload_count(run));', upload_body)
        self.assertIn('Abi::pixel_constant,', upload_body)
        self.assertIn('Abi::pixel_constant_count);', upload_body)
        self.assertIn('el::same_runs(Abi::plate_runs)', upload_body)
        abi = (ROOT / 'src/renderer/linear_material.h').read_text()
        self.assertIn('static constexpr unsigned plate_constant = 197, plate_count = 72, tier_count = 12;', abi)
        self.assertIn('static constexpr unsigned tier_constant = 52, tier_constant_count = 3;', abi)
        self.assertIn('engine_light::core::block_constants(*light, world, view, s.constants)', inc)
        # The frame row names the nozzles carrying light (no per-pixel rows) and the ships above eight plates.
        self.assertIn('lights=%u own_lights=%s most_lights=%s', inc)
        self.assertIn('plates_more=%u plates_max=%u', inc)
        self.assertIn('hdr_state_ != HdrState::Active', inc)
        self.assertIn('preset_scale, s.ships, engine_ring_->own);', inc)   # the own-ship tags reach the ship table
        # The hold: the two ship tables swap at the boundary, the hold runs between the ship and the node table (only
        # with the plume stage attached), and the frame row carries its two counts.
        frame = inc[inc.index('void MotionOutput::engine_light_frame()'):inc.index('HRESULT MotionOutput::engine_light_upload()')]
        self.assertLess(frame.index('std::swap(s.ships, s.previous);'), frame.index('el::build_ships('))
        # Since 2026-10-09 the hold takes the node-sourced walk's roots (engine-nozzle-source.md section 7).
        self.assertLess(frame.index('el::build_ships('),
                        frame.index('if (attached) el::hold_ships(*s.previous, s.ships, s.log, s.hold, engine_node_walked_, engine_node_walked_count_);'))
        self.assertLess(frame.index('el::hold_ships('), frame.index('el::build_nodes(*s.ships, s.log, &s.nodes);'))
        self.assertIn('held=%u hold_expired=%u hold_walked=%u', frame)
        # The fixture scope carries node+0x18 like object_trace's (the seam case's lit node hangs under the root).
        self.assertIn('route.scope_parent = s.parent; // engine light: the synthetic node+0x18', cpp)


class WineRecordTests(unittest.TestCase):
    def test_tracked_record(self):
        path = ROOT / 'verification/results/bottle-X3/engine-light-gpu.json'
        record = json.loads(path.read_text())
        self.assertTrue(record['passed'])
        self.assertEqual(record['bottle']['name'], 'X3')
        self.assertEqual(len(record['cases']), 9)
        for case in record['cases'].values():
            self.assertTrue(case['passed'])
            self.assertLessEqual(case['stats']['max_relative'], 0.01)
            self.assertEqual(case['stats']['zero_not_identical'], 0)
        self.assertTrue(record['reset_ok'])
        self.assertTrue(record['teardown_ok'])
        self.assertIn('TEARDOWN device references=0', record['teardown'])
        self.assertLessEqual(record['cost']['hit_ns'], 200.0)
        # The per-draw path on ships of 8 and 72 nozzles (the run's plates computed and uploaded): <= 200 ns per eight.
        self.assertEqual(sorted(int(c['plates']) for c in record['cost_plates']), [8, 72])
        for c in record['cost_plates']:
            self.assertLessEqual(c['hit_ns'], 200.0 * max(1.0, c['plates'] / 8), c)
        # The full-screen term for ships of 1, 3, 8, 10 and 72 nozzles (the twin's uniform branches: 1, 4, 8, 12, 72
        # plate slots).
        self.assertEqual([(g['nozzles'], g['width'], g['height']) for g in record['gpu']],
                         [(n, w, h) for n in (1, 3, 8, 10, 72) for w, h in ((1920, 1080), (5120, 1440))])
        # A light per plate (DUAL): two nozzles apart beyond one reach and two overlapping, the Split Ocelot's ten and the
        # cap of 72, both kinds' forms, every pixel against the law with the selected plate's light; every plate lit
        # (none dropped), every further nozzle's foot lit, and dark under plate 0's light alone where the nozzles are
        # apart.
        self.assertEqual(sorted(record['dual']), ['0-apart-1', '0-apart-6', '0-cap-1', '0-ten-6', '1-cap-6',
                                                  '1-overlap-6', '2-overlap-1', '2-ten-1'])
        for name, dual in record['dual'].items():
            self.assertTrue(dual['passed'], name)
            self.assertTrue(dual['checks']['plates'], name)
            self.assertLessEqual(dual['stats']['max_relative'], 0.01, name)
            self.assertEqual(len(dual['stats']['lit']), 10 if 'ten' in name else 72 if 'cap' in name else 2, name)
            self.assertTrue(all(n > 0 for n in dual['stats']['lit']), name)
            self.assertTrue(all(f > 0.0 for f in dual['stats']['at_foot']), name)
            self.assertEqual(dual['stats']['zero_not_identical'], 0, name)
            if 'apart' in name:
                self.assertEqual(dual['stats']['before_at_foot'][1], 0.0, name)
        # Nozzle plates: the white light map's term at the light 1 x (<= 1.05), beyond the plate radius 4 x; gain 1 and the
        # fill-only twin 1 x everywhere; without the light (the base) 4 x everywhere; both layouts.
        plate = record['plate']
        self.assertTrue(plate['passed'])
        self.assertEqual(plate['radii'], dict(reach=REACH, full=PLATE_FULL, zero=PLATE_REACH))
        self.assertEqual(sorted(plate['modes']), ['%d-%s' % (p, m) for p in (0, 1)
                                                  for m in ('gain1', 'nogain', 'nolight', 'twin', 'twin10', 'twin3',
                                                            'twin72', 'twin8')])
        for name, mode in plate['modes'].items():
            s = mode['stats']
            self.assertLessEqual(s['register_error'], 1e-5, name)
            if name.endswith(('twin3', 'twin8', 'twin10', 'twin72')):
                # Every main nozzle of the ship at gain 1 (its plate point and its near samples), 4 beyond all of them.
                self.assertEqual(s['nozzles'], int(name.split('twin')[1]), name)
                for nozzle in s['per_nozzle']:
                    self.assertGreater(nozzle['near'], 0, name)
                    self.assertAlmostEqual(nozzle['at_point'], 1.0, places=5, msg=name)
                    self.assertLessEqual(nozzle['near_max'], 1.05, name)
                self.assertAlmostEqual(s['far_min'], 4.0, places=5, msg=name)
                self.assertAlmostEqual(s['far_max'], 4.0, places=5, msg=name)
                continue
            expected = dict(twin=(1.0, 4.0), gain1=(1.0, 1.0), nogain=(1.0, 1.0), nolight=(4.0, 4.0))[name.split('-')[1]]
            self.assertLessEqual(s['near_max'], 1.05 if expected[0] == 1.0 else 4.0 + 1e-5, name)
            self.assertAlmostEqual(s['centre'], expected[0], places=5, msg=name)
            self.assertAlmostEqual(s['far_min'], expected[1], places=5, msg=name)

    def test_tracked_seam_record(self):
        # The route-level path (run_motion_output.py seam-engine-light): twins at registration, the twin bound on the
        # lit draw and the base on the unlit one, one upload, the law within 5 %, Reset.
        record = json.loads((ROOT / 'verification/results/bottle-X3/seam-engine-light-fixture.json').read_text())
        self.assertEqual((record['case'], record['bottle']['name'], record['exit']), ('seam-engine-light', 'X3', 0))
        self.assertEqual([f['draws_lit'] for f in record['frames']], ['0', '0', '1', '1'] + [f['draws_lit'] for f in record['frames'][4:]])
        self.assertEqual(record['frames'][-1]['draws_lit'], '1')
        self.assertLessEqual(record['worst_relative'], 0.05)
        self.assertEqual(record['zero_differ'], 0)
        self.assertEqual(record['log_rows']['hull_variant']['created'], '07')
        # The hull twins above the old 512-slot refusal, created under the device's budget (wined3d reports 512: 32768).
        self.assertGreater(int(record['log_rows']['hull_variant']['slots_max']), 512)
        self.assertEqual(record['log_rows']['hull_variant']['slot_budget'], record['log_rows']['slot_budget'][-1]['budget'])
        for row in record['log_rows']['frame_rows']:
            if row['candidates'] != '0':
                self.assertEqual((row['draws_lit'], row['no_twin'], row['no_rows']), ('1', '0', '0'))


if __name__ == '__main__':
    unittest.main()
