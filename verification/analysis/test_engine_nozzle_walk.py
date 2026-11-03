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
import math
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


_HOST = {}


def host_result():
    """verification/probe/engine_nozzle_walk_host.cpp built and run once per process (CoreTests and AnchorTests)."""
    if 'r' not in _HOST:
        with tempfile.TemporaryDirectory(prefix='x3-engine-nozzle-') as temporary:
            exe = Path(temporary) / 'host'
            build = subprocess.run([compiler(), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/engine_nozzle_walk_host.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True)
            if build.returncode:
                raise AssertionError(build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            if run.returncode:
                raise AssertionError(run.stdout + run.stderr)
            _HOST['r'] = json.loads(run.stdout)
    return _HOST['r']


class CoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.r = host_result()

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


class AnchorTests(unittest.TestCase):
    """Hidden-emitter anchors (docs/architecture/plumes-no-glow-hulls.md step 1) through the same host driver: a hull
    whose only JET children are hidden fx_engine_emitter dummies, on both edition layouts with a decoy model at the
    other edition's offset; the record law; identity, throttle and completeness refusals; a visible-jet hull's records
    byte-identical to the walk before step 1; the probe table and its SETA-scaled retry; the cost."""
    @classmethod
    def setUpClass(cls):
        cls.r = host_result()

    def test_option(self):
        e = schema.BY_KEY['engine_plume_emitters']
        self.assertEqual((e['type'], e['section'], e['builtin'], e['launcher'], e['env']),
                         ('bool', 'engine', '1', None, 'X3M_ENGINE_PLUME_EMITTERS'))
        self.assertIn('{"X3M_ENGINE_PLUME_EMITTERS", "engine_plume_emitters", Type::Bool',
                      (ROOT / 'src/config/config_schema_inc.h').read_text())
        self.assertIn(';engine_plume_emitters = 1', (ROOT / 'assets/x3m.ini').read_text())

    def test_anchors_on_both_layouts(self):
        size_k = 0.5 * (0.35 + (0.25 - 0.35) * math.log(320 / 150) / math.log(500 / 150)) * 320
        flags = (1 << 10) | (1 << 11) | (11 << 12)  # flag_node | flag_anchor | cyan
        for edition, offset in (('ap', 0x140), ('fl', 0x144)):
            a = self.r['anchors'][edition]
            self.assertEqual(a['model_offset'], offset)
            # Four emitters anchored; the fifth hidden C = 0 child (another body) refused for its identity.
            self.assertEqual((a['complete'], a['anchors'], a['candidates'], a['not_emitter'], a['mode_reads']), (1, 4, 5, 1, 5))
            self.assertEqual(a['origin'], [-10.0, 2.0, -48.82])  # +0xb0 x 0.01
            self.assertEqual([abs(v) for v in a['axis']], [0.0, 0.0, 1.0])
            self.assertEqual(a['axis'][2], -1.0)
            self.assertEqual(a['axis_turned'][0], -1.0)
            self.assertAlmostEqual(a['size'], size_k, places=3)  # k(R) x R at R = 320, no cap
            self.assertAlmostEqual(a['size'], a['size_ref'], places=3)
            self.assertEqual((a['s'], a['z']), ([0.4286, 1.0], [1.0, 2.0]))  # the emitter's own +0x88
            self.assertEqual((a['flags'], a['body']), (flags, -1))
            # The other edition's model offset reads the decoy: nothing anchors.
            self.assertEqual((a['decoy_anchors'], a['decoy_not_emitter']), (0, 5))
            self.assertEqual((a['idle_s'], a['undriven_rejected'], a['cut_anchors'], a['old_hidden']), (0.0, 1, 0, 5))

    def test_visible_jet_hull_unchanged(self):
        p = self.r['anchors']['parity']
        self.assertEqual((p['records'], p['identical'], p['stats_same'], p['anchors'], p['glow']), (2, 1, 1, 0, 1))
        self.assertEqual((p['travel_anchors'], p['rcs_only_anchors'], p['probe_jets'], p['probe_anchors']), (0, 1, 0, 0))

    def test_probe_table(self):
        p = self.r['anchors']['probe']
        self.assertEqual((p['first_due'], p['after_walk_due'], p['previous'], p['cleared_due']), (1, 0, 1, 1))
        # 2 s of game time: 100 frames at 20 ms, 10 under SETA x10; 600 frames when the clock stalls.
        self.assertEqual((p['frames_to_retry_seta1'], p['frames_to_retry_seta10'], p['frames_to_retry_stalled']), (100, 10, 600))
        self.assertLess(p['ns_per_draw'], 20.0)
        # An anchored hull skipped one frame: its recent window lapses after frame 12 and the probe at 13 is due at once
        # (review fix); a glow root waits for retry_ms (not due within the 30 frames simulated).
        self.assertEqual((p['anchored_rewalk_frame'], p['glow_rewalk_frame']), (13, 0))

    def test_probe_walk_diagnostics(self):
        # FL run 34 (farnhams-legacy.md "Diagnostic build for run 34"): a station-like root (three parts, no JET), the
        # Heavy Centaur's emitter hull and a glow capital probed once each, the station again: four walks, the
        # subtotals of the window row, one engine_plumes_probe_root row per newly probed root (the station's second walk
        # adds none).
        p = self.r['anchors']['probe_rows']
        self.assertEqual({k: p[k] for k in ('walks', 'children', 'hidden', 'mode_reads', 'candidates', 'glow', 'none')},
                         dict(walks=4, children=12, hidden=5, mode_reads=5, candidates=5, glow=1, none=2))
        self.assertEqual((p['written'], p['dropped']), (3, 0))
        self.assertEqual([(r['root_index'], r['head'], r['row']) for r in p['rows']], [
            (0, 1, 'model=0x1234 verdict=none children=3 hidden=0 jets=0 candidates=0'),
            (1, 1, 'model=0x5678 verdict=anchored children=4 hidden=4 jets=4 candidates=4'),
            (2, 1, 'model=0x9abc verdict=glow children=2 hidden=1 jets=2 candidates=1')])
        # At most 8 rows per report window: 12 more fresh roots give 5 rows and 7 dropped; the next window reopens.
        self.assertEqual(p['cap'], dict(written=8, pending=5, dropped=7, reopened=1))

    def test_non_ship_gate(self):
        # Undriven hidden emitters (placement's 0x28f; only class 7 is driven): no anchor.
        self.assertEqual(self.r['anchors']['non_ship'], dict(anchors=0, rejected=2))

    def test_cost(self):
        c = self.r['cost']
        # The rule on a 108-part capital (four shown jets) and the anchors-only probe walk stay within the walk's budget.
        self.assertLess(c['us_per_root_rule_on'], 10.0)
        self.assertLess(c['us_per_root_probe_walk'], 10.0)


class AnchorWiringTests(unittest.TestCase):
    """Source checks of step 1's wiring: the gather's probe, the append's options and record law, the FL fence, rows."""
    def setUp(self):
        self.plumes = (ROOT / 'src/proxy/motion_output_engine_plumes_inc.h').read_text()
        self.header = (ROOT / 'src/proxy/motion_output.h').read_text()

    def body(self, signature):
        text = self.plumes[self.plumes.index(signature):]
        return text[:text.index('\n}\n')]

    def test_gather_probe(self):
        gather = self.body('void MotionOutput::engine_node_gather(')
        self.assertIn('engine_anchor_probes_.due(root, frame_, engine_anchor_clock_ms_)', gather)
        self.assertIn('engine_node_.probes >= engine_anchor_probe_cap', gather)
        self.assertIn('std::uint32_t(registry), probed, model)', gather)  # the draw's model for the probe row
        self.assertIn('static constexpr unsigned engine_anchor_probe_cap = 4;', self.header)

    def test_append_options_and_record(self):
        append = self.body('void MotionOutput::engine_node_append()')
        self.assertIn('walk_options.model_offset = engine_node_layout::edition.model;', append)
        self.assertIn('walk_options.jets = !probed;', append)
        self.assertIn('nz::anchor_record(k, radius, &record)', append)
        self.assertIn('engine_plumes::floor_ratio_at(plumes_look_, radius, &k)', append)
        self.assertIn('if (complete && !ring_full && !probed)', append)
        self.assertIn('engine_load_epoch_ != engine_anchor_epoch_', append)
        self.assertIn('nz::probe_step_ms(engine_clock_.last_step, seta_rate_)', append)
        core = (ROOT / 'src/proxy/engine_nozzle_walk_core.h').read_text()
        self.assertIn('double ms = step_seconds * 1000. * double(seta_rate);', core)
        # Recent glow ships never enter the probe table (review fix): only probe walks and non-glow outcomes are noted.
        self.assertIn('probed || outcome != nz::AnchorProbes::glow', append)
        core = (ROOT / 'src/proxy/engine_nozzle_walk_core.h').read_text()
        self.assertIn('if (i < 0 || verdict[i] == anchored) return true;', core)

    def test_fl_fence_and_rows(self):
        self.assertIn('engine_anchors_on_ = engine_anchors_setting_ && plumes_requested_ && node;', self.plumes)
        self.assertNotIn('fl_draw_only', self.plumes)  # the FL walk is honoured (fl-nozzle-walk-activation.md)
        self.assertIn(': "nozzle_source_draw",', self.plumes)
        for row in ('engine_plumes_hidden_emitters device=%llu frame=%llu event=configure',
                    'engine_plumes_hidden_emitters device=%llu frame=%llu event=window',
                    'engine_plumes_hidden_emitter_hull device=%llu'):
            self.assertIn(row, self.plumes)
        window = re.search(r'log\("engine_plumes_hidden_emitters device=%llu frame=%llu event=window.*?"', self.plumes, re.S).group(0)
        for field in ('anchors=%u', 'ships=%u', 'edition=%s', 'probes=%u', 'probe_deferred=%u', 'glow=%u', 'no_floor=%u'):
            self.assertIn(field, window)
        self.assertIn('engine_anchor_hull_rows_ < engine_anchor_hull_row_cap', self.plumes)
        # FL run 34: the probe walks' subtotals and the bounded per-root row (engine_nozzle_walk_core.h ProbeRootRows).
        self.assertIn('probe_walks=%u probe_children=%u probe_hidden=%u probe_mode_reads=%u probe_candidates=%u '
                      'probe_glow=%u probe_none=%u probe_rows=%u probe_rows_dropped=%u', window)
        core = (ROOT / 'src/proxy/engine_nozzle_walk_core.h').read_text()
        self.assertIn('"engine_plumes_probe_root device=%llu frame=%llu root=%08x model=0x%x verdict=%s children=%u '
                      'hidden=%u jets=%u "', core)
        self.assertIn('constexpr unsigned probe_root_row_cap = 8;', core)
        append = self.body('void MotionOutput::engine_node_append()')
        self.assertIn('engine_node_.probe_walks.add(st);', append)
        self.assertIn('if (engine_anchor_probes_.find(root) < 0)', append)
        self.assertIn('nz::probe_root_row(root, roots.model[slot], st, anchors_here)', append)
        self.assertIn('log(X3M_ENGINE_PLUMES_PROBE_ROOT_FORMAT,', append)
        self.assertIn('engine_probe_rows_.window();', self.body('void MotionOutput::engine_anchor_window_row('))
        stage = re.search(r'log\("engine_stage device=.*?"', self.plumes, re.S).group(0)
        self.assertIn('node_anchors=%u node_anchor_roots=%u node_probes=%u', stage)


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
