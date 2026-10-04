"""Host tests of the engine light on the hull (docs/architecture/engine-light.md; gap 8 of
docs/architecture/engine-exhaust-gap-analysis.md).

- the option: schema entry engine_light (on|off, builtin on), the generated header and template, the launcher;
- the portable core (src/proxy/engine_light_core.h) through verification/probe/engine_light_host.cpp: the brightest main
  nozzle per ship (RCS, brake-pushed, other-view and parentless records ignored; ties to the lower handle), the
  256-ship cap (the dimmest entry gives way to a brighter ship and always to the own ship), the twin kinds' share /
  gain / widen match, the one-frame protocol with the motion compensation, degenerate rows, the VS layouts, the host
  cost;
- the pixel twins (verification/probe/engine_light_structure.cpp over the local original corpus, skipped without it):
  every non-asteroid reviewed pixel program gets a twin in all eight option sets, the four asteroid programs refuse,
  and each twin is its base variant plus exactly the words re-derived here (one `def c199`, the 18-instruction block,
  one 3-instruction add per fill block), with the slot deltas; the inputs and layouts the twin and the route rely on
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


NRM, DP3, MIN, RCP, MUL, MAD, MAX, RSQ, ADD = 36, 8, 10, 6, 5, 4, 11, 7, 2


def engine_block(depth):
    e, s, a, b, f, k = 14, 15, 200, 201, 202, 199
    words = []
    for w in (op(NRM, dst(TEMP, s), src(INPUT, 2)),
              op(DP3, dst(TEMP, s, 8), src(TEMP, s), src(CONST, f)),
              op(MIN, dst(TEMP, s, 8), lane(TEMP, s, 3), lane(CONST, k, 1)),
              op(RCP, dst(TEMP, s, 8), lane(TEMP, s, 3)),
              op(MUL, dst(TEMP, s, 8), lane(TEMP, s, 3), lane(INPUT, depth, 1)),
              op(MAD, dst(TEMP, e), src(TEMP, s), lane(TEMP, s, 3, 1), src(CONST, a)),
              op(DP3, dst(TEMP, e, 8), src(TEMP, e), src(TEMP, e)),
              op(MAX, dst(TEMP, e, 8), lane(TEMP, e, 3), lane(CONST, k, 2)),
              op(RSQ, dst(TEMP, s, 8), lane(TEMP, e, 3)),
              op(MUL, dst(TEMP, e), src(TEMP, e), lane(TEMP, s, 3)),
              op(NRM, dst(TEMP, s), src(INPUT, 3)),
              op(DP3, dst(TEMP, s, 1) | SAT, src(TEMP, s), src(TEMP, e)),
              op(ADD, dst(TEMP, e, 8), lane(TEMP, e, 3, 1), lane(CONST, a, 3)),
              op(MUL, dst(TEMP, e, 8) | SAT, lane(TEMP, e, 3), lane(CONST, b, 3)),
              op(MUL, dst(TEMP, e, 8), lane(TEMP, e, 3), lane(TEMP, e, 3)),
              op(MUL, dst(TEMP, e, 8), lane(TEMP, e, 3), lane(TEMP, s, 0)),
              op(MUL, dst(TEMP, e), lane(TEMP, e, 3), src(CONST, b)),
              op(MAX, dst(TEMP, e), src(TEMP, e), lane(CONST, k, 3))):
        words += w
    return words


ADD_TRIPLET = (op(ADD, dst(TEMP, 13) | SAT, src(TEMP, 12, 0xe4, 1), lane(CONST, 199, 0)) +
               op(MIN, dst(TEMP, 13), src(TEMP, 14), src(TEMP, 13)) + op(ADD, dst(TEMP, 12), src(TEMP, 12), src(TEMP, 13)))
FILL_MAD = op(MAD, dst(TEMP, 12), src(TEMP, 13), lane(CONST, 215, 0), src(TEMP, 12))
DEF_C199 = [0x05000051, dst(CONST, 199, 15), bits(1.0), bits(-2.0 ** -20), bits(2.0 ** -40), bits(0.0)]


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

    def test_read_once_at_configure(self):
        inc = (ROOT / 'src/proxy/motion_output_engine_light_inc.h').read_text()
        self.assertEqual(inc.count('config::get(L"X3M_ENGINE_LIGHT"'), 1)
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
        self.assertEqual(s['colour'], [1.04, 1.04, 1.04])                 # white x I(1) 4.16 (after flight F) x preset 1 x 0.25
        self.assertEqual(s['radius'], 30.0)
        self.assertAlmostEqual(s['brightness'], 41.6, places=4)            # 40 x 1.04 (I(1) 4.16 after flight F)
        self.assertEqual((s['main'], s['rcs'], s['brake'], s['other_view'], s['orphan']), (3, 1, 1, 1, 1))

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

    def test_layouts(self):
        self.assertEqual(self.r['layout'], dict(loop=[28, 34], single=[7, 13], unknown=[0, 0]))

    def test_host_cost(self):
        c = self.r['cost']
        self.assertEqual((c['nodes'], c['ships']), (1024, 256))
        self.assertLess(abs(c['hits'] - c['rounds'] / 2), c['rounds'] / 100)   # half the probes hit
        self.assertLess(c['ns_per_draw'], 200.0)


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
        blocks = {depth: engine_block(depth) for depth in range(10)}
        checked = 0
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
                found = [(d, find_all(twin, b)) for d, b in blocks.items()]
                found = [(d, hits) for d, hits in found if hits]
                self.assertEqual(len(found), 1, (name, set_name))
                depth, hits = found[0]
                self.assertEqual(len(hits), 1, (name, set_name))
                self.assertIn(depth, (5, 6, 7, 8, 9), (name, set_name))   # the motion transformer's depth input
                self.assertEqual(len(find_all(twin, DEF_C199)), 1, (name, set_name))
                triplets = find_all(twin, ADD_TRIPLET)
                self.assertEqual(len(triplets), 2 if share else 1, (name, set_name))
                delta = entry['twin_slots'] - entry['base_slots']
                if fill0:
                    self.assertEqual(delta, 70 if share else 45, (name, set_name))
                    continue
                self.assertEqual(delta, 28 if share else 25, (name, set_name))
                for at in triplets:   # each add follows its fill block's MAD
                    self.assertEqual(twin[at - len(FILL_MAD):at], FILL_MAD, (name, set_name))
                stripped = twin
                for pattern in [blocks[depth], DEF_C199] + [ADD_TRIPLET] * len(triplets):
                    at = find_all(stripped, pattern)[0]
                    stripped = remove(stripped, at, len(pattern))
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
        self.assertIn('hdr_state_ != HdrState::Active', inc)
        self.assertIn('preset_scale, &s.ships, engine_ring_->own);', inc)   # the own-ship tags reach the ship table
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
        self.assertEqual([(g['width'], g['height']) for g in record['gpu']], [(1920, 1080), (5120, 1440)])

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
        for row in record['log_rows']['frame_rows']:
            if row['candidates'] != '0':
                self.assertEqual((row['draws_lit'], row['no_twin'], row['no_rows']), ('1', '0', '0'))


if __name__ == '__main__':
    unittest.main()
