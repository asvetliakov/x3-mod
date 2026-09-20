"""Independent checks of the two residual stamps (X3M_RESIDUAL_PHASES=1): the
site ledger against the installed EXE, refusals, the host accumulator/gate/
window probe, the production wiring (shared lean stub and install transaction,
the retained sibling clocks, the frame-boundary order) and the launcher
option. Twin of test_pass_phases.py. No game, no Wine."""
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
import verify_residual_phase_sites as probe
from verification.analysis.test_chase_lead_sites import PatchedImage

ROOT = Path(__file__).resolve().parents[2]


class SourceAndReplay(unittest.TestCase):
    def test_exact_production_order_and_spec_fields(self):
        self.assertEqual(len(probe.SITES), 2)
        self.assertEqual([s.va for s in probe.SITES], [0x4c1eab, 0x47230c])
        self.assertEqual([len(s.expected) for s in probe.SITES], [8, 9])
        text = probe.SOURCE.read_text()
        self.assertTrue(probe.source_checks(text))
        # Both spans are plain copies: no ret_pop, no rel32 field.
        self.assertEqual(text.count('},8,0,0}') + text.count('},9,0,0}'), 2)
        self.assertFalse(probe.source_checks(text.replace('},9,0,0}', '},9,0,2}', 1)))
        self.assertFalse(probe.source_checks(text.replace('0x8b,0x13,0x8b,0x8a', '0x8b,0x13,0x8b,0x8b', 1)))
        lines = text.splitlines()
        entries = [i for i, line in enumerate(lines) if '{"residual_phase_' in line]
        self.assertEqual(len(entries), 2)
        swapped = list(lines)
        swapped[entries[0]], swapped[entries[1]] = swapped[entries[1]], swapped[entries[0]]
        self.assertFalse(probe.source_checks('\n'.join(swapped)))
        self.assertIsNone(probe.source_checks(None))

    def test_plain_copy_replays_byte_identically_at_every_arena(self):
        for site in probe.SITES:
            for arena in probe.ARENAS:
                with self.subTest(site=site.name, arena=hex(arena)):
                    self.assertEqual(probe.relocated_bytes(site, arena), site.expected)

    def test_host_accumulator_gate_and_window_probe(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-residual-phases-') as temporary:
            exe = Path(temporary) / 'residual_phases_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/residual_phases_host.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertRegex(run.stdout, r'^residual_phases_host checks=\d+ failures=0 accumulator_bytes=\d+ window_bytes=\d+ dispatch_cost_ns=\d+\n$')

    def test_production_wiring(self):
        source = (ROOT / 'src/proxy/residual_phases.cpp').read_text()
        header = (ROOT / 'src/proxy/residual_phases.h').read_text()
        core = (ROOT / 'src/proxy/residual_phases_core.h').read_text()
        # Off = inert: the environment gate, the frame boundary behind one
        # relaxed load, the handler's first instruction an `active` test.
        self.assertIn('L"X3M_RESIDUAL_PHASES"', source)
        self.assertIn('if (active.load(std::memory_order_relaxed)) detail::frame_impl(frame_index, sampled, views_us, view_setup_us, view_submit_us, views);', header)
        handler = source[source.index('x3m_residual_phase_enter(unsigned index)'):]
        handler = handler[:handler.index('\n}')]
        self.assertIn('if(!active.load(std::memory_order_relaxed))return;', handler)
        self.assertIn('x3m::LightCallBoundary cpu;', handler)
        self.assertNotIn('PreserveCpuState', handler)
        self.assertEqual(handler.count('QueryPerformanceCounter('), 1)
        self.assertIn('gate.owned(GetCurrentThreadId())', handler)
        self.assertNotIn('log(', handler)
        self.assertIn('__attribute__((force_align_arg_pointer))', source)
        # The shared lean stub, the shared install transaction, the two
        # prerequisite groups and the sibling clocks they retain.
        self.assertIn('return lean_stub::emit(reinterpret_cast<const void*>(&x3m_residual_phase_enter),index,next_out);', source)
        self.assertIn('return stamp::install_group(patches,specs,&emit,installed,status);', source)
        self.assertIn('return stamp::uninstall_group(patches);', source)
        self.assertLess(source.index('status="frame_phases_off"'), source.index('status="pass_phases_off"'))
        self.assertIn('std::atomic<bool> active', header)
        self.assertIn('accumulator.material(now,pass_link->end_clock,pass_link->begin_clock,pass_link->begin_armed,frame_link->submission_ticks_at(now),pass_link->end_submission,pass_link->begin_submission,frame_link->live&&frame_link->submit_begin);', handler)
        self.assertIn('accumulator.view(now,frame_link->submit_end);', handler)
        pass_core = (ROOT / 'src/proxy/pass_phases_core.h').read_text()
        self.assertIn('if (index == 0 && begin_armed) { begin_armed = false; begin_clock = now; begin_submission = submission; }', pass_core)
        self.assertIn('if (index == site_count - 1) { ++passes; last = 0; end_clock = now; end_submission = submission;', pass_core)
        self.assertIn('end_clock = begin_clock = 0; begin_armed = false;', pass_core)
        self.assertIn('detail::Accumulator* shared_accumulator() noexcept;', (ROOT / 'src/proxy/pass_phases.h').read_text())
        frame_core = (ROOT / 'src/proxy/frame_phases_core.h').read_text()
        self.assertIn('submit_begin = 0; submit_end = qpc;', frame_core)
        self.assertIn('setup_begin = submit_begin = submit_end = 0;', frame_core)
        self.assertIn('const detail::Tracker* shared_tracker() noexcept;', (ROOT / 'src/proxy/frame_phases.h').read_text())
        # The accumulator pairs, never opens: the first material and a skipped
        # pass loop are counted, not accumulated; `other` saturates at zero.
        self.assertIn('if (end_clock && end_clock > p_clock) {', core)
        self.assertIn('if (!begin_armed && begin_clock >= p_clock) partition(1, 5, begin_clock - p_clock, p_submission, begin_submission);', core)
        self.assertIn('if (submit_end && submit_end != submit_end_seen) {', core)
        self.assertIn('if (views_us >= attributed) out.interval_us[3] = views_us - attributed; else ++other_underflow;', core)
        cost = int(re.search(r'inline constexpr std::uint64_t dispatch_cost_ns = (\d+);', core).group(1))
        self.assertTrue(0 < cost <= 370, cost)
        # The window closes at the frame-phase boundary under its owner guard,
        # ahead of the pass group's own reduction, with the views phase and the
        # view sums joined; a frame without a sample is dropped.
        frame = (ROOT / 'src/proxy/frame_phases.cpp').read_text()
        impl = frame[frame.index('void frame_impl(std::uint64_t frame) noexcept {'):]
        impl = impl[:impl.index('\n}')]
        self.assertLess(impl.index('if(!owner(false))return;'), impl.index('residual_phases::frame(frame,taken,taken?last_sample.phase_us[detail::views_phase]:0,taken?last_sample.view_setup_us:0,taken?last_sample.view_submit_us:0,taken?last_sample.views:0);'))
        self.assertLess(impl.index('residual_phases::frame('), impl.index('pass_phases::frame('))
        self.assertLess(impl.index('pass_phases::frame('), impl.index('if(!taken)return;'))
        self.assertIn('if(!sampled){accumulator.discard(pass_link->begin_armed);++dropped;return;}', source)
        self.assertIn('log("residual_phases qpc=%llu frame=%llu frames=%u materials_p50=%llu particle_views_p50=%llu passes_p50=%llu views_p50=%llu prepare_p50_us=%llu prepare_p95_us=%llu setup_p50_us=%llu setup_p95_us=%llu prepare_per_pass_p50_ns=%llu setup_per_pass_p50_ns=%llu particles_p50_us=%llu particles_p95_us=%llu other_p50_us=%llu other_p95_us=%llu self_p50_us=%llu dispatch_cost_ns=%llu prepare_skipped=%llu setup_skipped=%llu view_skipped=%llu other_underflow=%llu clock_errors=%llu clock_failures=%llu unmatched=%llu dropped=%llu early=%u foreign=%u"', source)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertLess(capture.index('pass_phases::initialize();'), capture.index('residual_phases::initialize();'))
        self.assertLess(capture.index('residual_phases::initialize();'), capture.index('loop_phases::initialize();'))
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('src/proxy/residual_phases.cpp'), 2)
        build = (ROOT / 'verification/probe/build_game_phase_cpu.py').read_text()
        self.assertIn("('src/proxy/residual_phases.cpp','residual')", build)
        audit = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        self.assertIn("'_x3m_residual_phase_enter'", audit)
        runner = (ROOT / 'verification/probe/run_game_phase_cpu.py').read_text()
        self.assertIn("'RESIDUAL PHASE BENCH'", runner)
        fixture = (ROOT / 'verification/probe/game_phase_cpu_fixture.cpp').read_text()
        for label in ('first material has no pass_end to pair with; its setup is pending',
                      'second material closes the first setup and pairs prepare with the last pass_end',
                      'skipped pass loop leaves its setup and the next prepare unpaired',
                      "the pass group's own sample closed after this group read it",
                      'residual partial install rolled back to original bytes',
                      'residual install refused after the install window closed',
                      'documented residual dispatch cost within 2x of the measured cost'):
            self.assertIn(label, fixture)
        self.assertIn('residual_sites=%u', fixture)


class ResidualPhasesLaunchOption(unittest.TestCase):
    def test_launch_option_requires_telemetry_and_implies_frame_and_pass_phases(self):
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        helper = LodScaleLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = helper.launch(directory, '--residual-phases')
            self.assertEqual(code, 2)
            self.assertIn('--residual-phases requires --telemetry', error)
            code, output, error = helper.launch(directory, '--residual-phases', '--telemetry')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_RESIDUAL_PHASES'], '1')
            self.assertEqual(env['X3M_FRAME_PHASES'], '1')
            self.assertEqual(env['X3M_PASS_PHASES'], '1')
            # The implication precedes the pass-phases check: no --frame-phases needed.
            code, output, error = helper.launch(directory, '--residual-phases', '--pass-phases', '--telemetry')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_RESIDUAL_PHASES'], env['X3M_FRAME_PHASES'], env['X3M_PASS_PHASES']), ('1', '1', '1'))
            code, output, error = helper.launch(directory, '--frame-phases', '--telemetry', inherited={'X3M_RESIDUAL_PHASES': '1'})
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_RESIDUAL_PHASES'], '0')
            self.assertEqual(env['X3M_PASS_PHASES'], '0')


@unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed X3AP.exe unavailable')
class NativeSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = probe.DEFAULT_EXE.read_bytes()
        cls.image = probe.common.Image(cls.data)
        cls.decoded = probe.decode()
        cls.source = probe.SOURCE.read_text()
        cls.installed = probe.installed_spans([path.read_text() for path in probe.INSTALLED])

    def report(self, image=None, decoded=None, installed=None):
        return probe.inspect(image or self.image, decoded or self.decoded, self.source, self.data,
                             self.installed if installed is None else installed)

    def test_actual_executable_and_both_spans(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report['checks'])
        self.assertTrue(report['source_present'])
        self.assertEqual(report['installed_sites_checked'], 47 + 10 + 4)
        self.assertEqual(len(report['sites']), 2)
        for row in report['sites']:
            self.assertEqual(row['incoming_sources'], sorted(f'{s:#010x}' for s in probe.INCOMING[int(row['va'], 16)]), row['name'])
            self.assertTrue(row['plain_copy'] and row['arena_replay_ok'] and row['in_loop_body'] and row['no_installed_conflict'], row['name'])
        self.assertEqual([len(row['incoming_sources']) for row in report['sites']], [4, 0])

    def test_corrupted_byte_refused_at_every_site(self):
        for site in probe.SITES:
            with self.subTest(site=site.name):
                image = PatchedImage(self.image, [(site.va, bytes([site.expected[0] ^ 1]))])
                row = next(row for row in self.report(image=image)['sites'] if row['name'] == site.name)
                self.assertFalse(row['bytes_ok'])
                self.assertFalse(row['ok'])

    def test_interior_or_unexpected_incoming_edge_refused(self):
        for site in probe.SITES:
            bounds = (site.function_start, site.function_end)
            with self.subTest(site=site.name):
                decoded = {k: list(v) for k, v in self.decoded.items()}
                decoded[bounds].append(probe.common.Instruction(0x100, b'\xe9\0\0\0\0', 'jmp', hex(site.va + 1)))
                row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                self.assertFalse(row['no_interior_branch'])
                decoded = {k: list(v) for k, v in self.decoded.items()}
                decoded[bounds].append(probe.common.Instruction(0x100, b'\xe9\0\0\0\0', 'jmp', hex(site.va)))
                row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                self.assertTrue(row['no_interior_branch'])
                self.assertFalse(row['incoming_ok'])
                self.assertFalse(row['ok'])

    def test_boundaries_anchors_and_installed_conflicts_must_match(self):
        for site in probe.SITES:
            bounds = (site.function_start, site.function_end)
            for boundary in ('start', 'end'):
                with self.subTest(site=site.name, boundary=boundary):
                    decoded = {k: list(v) for k, v in self.decoded.items()}
                    decoded[bounds] = [i for i in decoded[bounds]
                                       if (i.va != site.va if boundary == 'start' else i.end != site.end)]
                    row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                    self.assertFalse(row['whole_instructions'])
        decoded = {k: list(v) for k, v in self.decoded.items()}
        decoded[probe.MATERIAL] = [i for i in decoded[probe.MATERIAL] if i.va != 0x4c1eb5]
        self.assertFalse(self.report(decoded=decoded)['checks']['material_anchors'])
        decoded = {k: list(v) for k, v in self.decoded.items()}
        decoded[probe.FRAME] = [i for i in decoded[probe.FRAME] if i.va != 0x472301]
        checks = self.report(decoded=decoded)['checks']
        self.assertFalse(checks['frame_anchors'])
        self.assertFalse(checks['particles_skip_lands_on_span_end'])
        overlapping = self.report(installed=[(0x472310, 5)])
        self.assertFalse(next(row for row in overlapping['sites'] if row['name'] == 'residual_phase_view_particles')['no_installed_conflict'])
        self.assertEqual(probe.inspect(self.image, {}, self.source, self.data, [])['result'], 'FAIL')


if __name__ == '__main__':
    unittest.main()
