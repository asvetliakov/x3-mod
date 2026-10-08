"""Host tests of the node-sourced engine nozzles (docs/architecture/engine-nozzle-source.md).

- the option: schema entry engine_nozzle_source (node|draw, builtin node), the generated header and template, the
  launcher flag;
- the portable core (src/proxy/engine_nozzle_walk_core.h) through verification/probe/engine_nozzle_walk_host.cpp over
  a synthetic node image: the walk finds the main jets of a ship root's child list, every guard rejects (back-pointer,
  scale, throttle range, the drive's hidden flag), the dedupe order draw > far > culled > node through
  engine_far_jets_core.h Seen, the undrawn jet's record (far_record with the live throttle, flag_node in place of
  flag_far), the 256 bound, an unreadable element or root, an empty list, a sentinel at the image's end, the root
  set's capacity and identity, the light's hold rule with a walked root (engine_light_core.h hold_ships), and the
  walk's cost (the ledger figure: per root of 108 parts, scattered and contiguous, per frame at 2 and 5 ships);
- the route's wiring (source checks): the gather at sample_scope, the append after the far append and at the light's
  boundary, the stage's view tally treating node records as far records, the handler's culled list, the stage row.
"""
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/config'))
import schema  # noqa: E402
from verification.analysis.test_config_schema import hermetic_launcher, launch_env  # noqa: E402


def compiler():
    found = shutil.which('clang++') or shutil.which('c++')
    if not found:
        raise RuntimeError('host C++ compiler required')
    return found


class OptionTests(unittest.TestCase):
    def test_schema_entry(self):
        e = schema.BY_KEY['engine_nozzle_source']
        self.assertEqual((e['type'], e['section'], e['builtin'], e['choices'], e['launcher'], e['env']),
                         ('enum', 'engine', 'node', ('node', 'draw'), '--engine-nozzle-source', 'X3M_ENGINE_NOZZLE_SOURCE'))
        header = (ROOT / 'src/config/config_schema_inc.h').read_text()
        self.assertIn('{"X3M_ENGINE_NOZZLE_SOURCE", "engine_nozzle_source", Type::Enum', header)
        self.assertIn('"node|draw"', header)
        template = (ROOT / 'assets/x3m.ini').read_text()
        self.assertIn(';engine_nozzle_source = node', template)

    def test_launcher_flag(self):
        module, game, wine, directory = hermetic_launcher()
        with directory:
            self.assertNotIn('X3M_ENGINE_NOZZLE_SOURCE', launch_env(module, game, wine))
            for value in ('node', 'draw'):
                env = launch_env(module, game, wine, '--engine-effects', 'plumes', '--engine-nozzle-source', value)
                self.assertEqual(env['X3M_ENGINE_NOZZLE_SOURCE'], value)
            with self.assertRaises(SystemExit):
                launch_env(module, game, wine, '--engine-nozzle-source', 'off')
            with self.assertRaises(SystemExit):
                launch_env(module, game, wine, '--vanilla', '--engine-nozzle-source', 'draw')


class CoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(prefix='x3-engine-nozzle-')
        cls.addClassCleanup(temporary.cleanup)
        exe = Path(temporary.name) / 'host'
        build = subprocess.run([compiler(), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                str(ROOT / 'verification/probe/engine_nozzle_walk_host.cpp'), '-o', str(exe)], capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.r = json.loads(run.stdout)

    def test_harness_checks(self):
        self.assertEqual(self.r['failures'], 0)
        self.assertGreaterEqual(self.r['checks'], 20)

    def test_option_parser(self):
        self.assertEqual(self.r['option'], dict(node=1, draw=1, refused=1, wide=1, default='node'))

    def test_walk_finds_the_jets_and_every_guard_rejects(self):
        w = self.r['walk']
        self.assertEqual((w['complete'], w['children'], w['jets']), (1, 11, 10))
        self.assertEqual((w['hidden'], w['guard_parent'], w['guard_scale'], w['guard_throttle']), (1, 1, 1, 2))
        # Emitted in list order: the drawn jet, the undrawn one, the RCS jet, the engine-culled and the far-copied one.
        self.assertEqual(w['order'], [0x5a, 0x5b, 0x5d, 0x61, 0x62])
        self.assertEqual((w['emitted'], w['duplicates'], w['records'], w['steering']), (5, 3, 1, 1))

    def test_dedupe_draw_far_culled_then_node(self):
        # Three of the five emitted jets were already in the set (draw 0x5a, far 0x62, culled 0x61): no record for them;
        # the RCS jet is refused by far_record; the undrawn main jet alone becomes the node record.
        w = self.r['walk']
        self.assertEqual(w['duplicates'], 3)
        self.assertEqual(w['record']['handle'], 0x5b)

    def test_record_carries_the_live_throttle_and_flag_node(self):
        rec = self.r['walk']['record']
        self.assertEqual((rec['s'], rec['z']), (1.0, 2.0))
        self.assertEqual(rec['origin'], [-0.3, 0.2, 0.2])
        self.assertAlmostEqual(rec['size'], 0.4, places=4)
        self.assertEqual((rec['flag_node'], rec['flag_far']), (1 << 10, 1 << 9))
        self.assertTrue(rec['flags'] & (1 << 10))
        self.assertFalse(rec['flags'] & (1 << 9))

    def test_hidden_flag_drops_the_record(self):
        self.assertEqual(self.r['walk']['hidden_again'], dict(hidden=2, records=0))

    def test_bound_and_unreadable(self):
        b = self.r['bound']
        self.assertEqual((b['complete'], b['overflow'], b['children'], b['emitted']), (0, 1, 256, 256))
        self.assertEqual(b['unreadable'], dict(complete=0, children=3, unreadable=1))
        self.assertEqual((b['next_unreadable_children'], b['empty_complete'], b['bad_root_unreadable'], b['tail_sentinel_complete']),
                         (2, 1, 1, 1))

    def test_root_set(self):
        self.assertEqual(self.r['roots'], dict(capacity=256, inserted=256, repeated=0, overflow=88, identity=1, fresh=1, ordered=1,
                                               ship_window=1, ship_extended=1, ship_latest=1))

    def test_hold_rule(self):
        self.assertEqual(self.r['hold'], dict(walked_skipped=1, held_unwalked=1, without_walked_held=1))

    def test_walk_cost(self):
        c = self.r['cost']
        self.assertEqual((c['roots'], c['children'], c['jets_per_root']), (256, 108, 4))
        # The ledger figure: a 108-part root scattered over the image under 10 us, five such ships under 50 us a frame;
        # the root set's probe well under the design's 20 ns per routed draw.
        self.assertLess(c['us_per_root'], 10.0)
        self.assertLess(c['us_per_frame_5_ships'], 50.0)
        self.assertLess(c['us_per_root_contiguous'], c['us_per_root'] * 2)
        self.assertLess(c['probe_ns_per_draw'], 20.0)


class ProductionReaderCostTests(unittest.TestCase):
    """The walk through the production reader (src/proxy/engine_memory.cpp on the host against the mock <windows.h> of
    test_engine_memory_shutdown.py; verification/probe/engine_nozzle_walk_memory_host.cpp): the per-part cost with
    engine_memory's region cache, span checks and tick sampling, one VirtualQuery per frame (review S1)."""
    @classmethod
    def setUpClass(cls):
        from verification.analysis import test_engine_memory_shutdown as mem
        from source_text import source_text
        text = source_text(ROOT / 'src/proxy/engine_memory.cpp')
        if text.count(mem.COPY) != 1:
            raise AssertionError('copy_bytes anchor moved; update test_engine_memory_shutdown')
        temporary = tempfile.TemporaryDirectory(prefix='x3-engine-nozzle-memory-')
        cls.addClassCleanup(temporary.cleanup)
        directory = Path(temporary.name)
        (directory / 'windows.h').write_text(mem.MOCK_WINDOWS)
        (directory / 'engine_memory_under_test_inc.h').write_text(text.replace(mem.COPY, 'host_copy(out, in, size);'))
        exe = directory / 'host'
        build = subprocess.run([compiler(), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(directory),
                                '-I', str(ROOT / 'src/proxy'), str(ROOT / 'verification/probe/engine_nozzle_walk_memory_host.cpp'),
                                '-o', str(exe)], capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stdout + build.stderr)
        run = subprocess.run([str(exe)], capture_output=True, text=True)
        if run.returncode:
            raise AssertionError(run.stdout + run.stderr)
        cls.r = json.loads(run.stdout)

    def test_walk_through_engine_memory(self):
        c = self.r
        self.assertEqual((c['ok'], c['roots'], c['children'], c['jets_per_root']), (1, 256, 108, 4))
        # Per root: the root's +0xc, two reads per part (next pointer, flag pair), a third per jet, the sentinel's next
        # pointer; one VirtualQuery per frame (the one region re-validated at its first touch), none per read.
        self.assertEqual(c['reads'], c['rounds'] * 256 * (1 + 108 * 2 + 4 + 1))
        self.assertEqual(c['copies'], c['reads'])
        self.assertEqual(c['vq_calls'], c['rounds'])
        self.assertLess(c['us_per_root'], 20.0)
        self.assertLess(c['us_per_frame_5_ships'], 100.0)


class WiringTests(unittest.TestCase):
    """Source checks of the route's wiring: the gather, the append points, the tally and the row."""
    def setUp(self):
        self.plumes = (ROOT / 'src/proxy/motion_output_engine_plumes_inc.h').read_text()
        self.light = (ROOT / 'src/proxy/motion_output_engine_light_inc.h').read_text()
        self.route = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.effects = (ROOT / 'src/proxy/motion_output_engine_effects_inc.h').read_text()
        self.handler = (ROOT / 'src/proxy/engine_far_jets.cpp').read_text()

    def test_gather_at_sample_scope(self):
        # Both paths of sample_scope (the fixture's synthetic scope and object_trace::current) hand the root to the set.
        self.assertEqual(self.route.count('engine_node_gather('), 2)
        body = self.route[self.route.index('bool MotionOutput::sample_scope('):]
        body = body[:body.index('\n}\n')]
        self.assertEqual(body.count('engine_node_gather('), 2)

    def test_append_after_far_append_and_at_the_light_boundary(self):
        run = self.plumes[self.plumes.index('HRESULT MotionOutput::run_engine_plumes()'):]
        self.assertLess(run.index('engine_far_append();'), run.index('engine_node_append();'))
        frame = self.light[self.light.index('void MotionOutput::engine_light_frame()'):]
        self.assertLess(frame.index('engine_node_append();'), frame.index('el::build_ships('))
        self.assertIn('el::hold_ships(*s.previous, s.ships, s.log, s.hold, engine_node_walked_, engine_node_walked_count_)', frame)
        self.assertIn('engine_node_appended_ = false;', self.effects)

    def test_dedupe_primed_from_ring_and_culled_list(self):
        append = self.plumes[self.plumes.index('void MotionOutput::engine_node_append()'):]
        append = append[:append.index('\n}\n')]
        self.assertLess(append.index('engine_node_seen_.begin();'), append.index('engine_far_jets::culled()'))
        self.assertLess(append.index('engine_far_jets::culled()'), append.index('nz::walk('))
        self.assertIn('engine_node_seen_.insert(engine_ring_->records[i].node_handle, engine_ring_->camera[i])', append)
        self.assertIn('| ee::flag_node', append)
        self.assertIn('object_lifetime::current(registry, raw.node, raw.handle, camera, view_handle, &life)', append)
        self.assertIn('engine_record_own(raw.node, raw.handle, true, root)', append)
        self.assertIn('SetLastError(error);', append)

    def test_view_tally_treats_node_records_as_far(self):
        self.assertIn('flag_far | engine_effects::core::flag_node', self.plumes)

    def test_gather_filters_ships_and_resets_the_view(self):
        gather = self.plumes[self.plumes.index('void MotionOutput::engine_node_gather('):]
        gather = gather[:gather.index('\n}\n')]
        self.assertIn('engine_ship_roots_.recent(root, frame_)', gather)
        self.assertIn('own_ship_node_', gather)
        self.assertIn('engine_scene_camera_hold', gather)
        append = self.plumes[self.plumes.index('void MotionOutput::engine_node_append()'):]
        append = append[:append.index('\n}\n')]
        self.assertIn('engine_ship_roots_.mark(engine_ring_->parent[i], engine_ring_frame_)', append)
        self.assertIn('engine_load_epoch_ != engine_node_epoch_', append)
        self.assertIn('seen_inserts >= engine_far_jets::core::Seen::slots', append)
        self.assertIn('engine_scene_camera_last_ = 0; // node-sourced nozzles', self.route)

    def test_handler_culled_list(self):
        self.assertIn('culled_[culled_count_].handle = word(node, core::handle_offset);', self.handler)
        # Integer only: the handler runs inside the engine's pass with no CPU-state boundary (comments aside).
        code = '\n'.join(line.split('//')[0] for line in self.handler.splitlines())
        self.assertNotRegex(code, r'\b(float|double)\b')

    def test_stage_row_fields(self):
        row = re.search(r'log\("engine_stage device=.*?"', self.plumes, re.S).group(0)
        for field in ('node_roots=', 'node_records=', 'node_dupes=', 'node_guard_rejected=', 'node_overflow=', 'node_hidden=',
                      'node_engine_culled=', 'node_root_overflow=', 'node_not_ship=', 'node_walk_us='):
            self.assertIn(field, row)
        self.assertIn('hold_walked=%u', self.light)


if __name__ == '__main__':
    unittest.main()
