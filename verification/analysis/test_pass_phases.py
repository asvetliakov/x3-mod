"""Independent checks of the four effect-pass stamps (X3M_PASS_PHASES=1): the
site ledger against the installed EXE, refusals, the host accumulator/gate/
window probe, the production wiring and the launcher option. Twin of
test_game_phase_frame.py. No game, no Wine."""
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
import verify_pass_phase_sites as probe
from verification.analysis.test_chase_lead_sites import PatchedImage

ROOT = Path(__file__).resolve().parents[2]


class SourceAndReplay(unittest.TestCase):
    def test_exact_production_order_and_spec_fields(self):
        self.assertEqual(len(probe.SITES), 4)
        self.assertEqual([s.va for s in probe.SITES], [0x4c3ff0, 0x4c4000, 0x4c403e, 0x4c4049])
        self.assertEqual([len(s.expected) for s in probe.SITES], [6, 7, 8, 7])
        text = probe.SOURCE.read_text()
        self.assertTrue(probe.source_checks(text))
        # Every span is a plain copy: no ret_pop, no rel32 field.
        self.assertEqual(text.count('},6,0,0}') + text.count('},7,0,0}') + text.count('},8,0,0}'), 4)
        self.assertFalse(probe.source_checks(text.replace('},8,0,0}', '},8,0,1}', 1)))
        self.assertFalse(probe.source_checks(text.replace('0x8b,0x13,0x8b,0x82', '0x8b,0x13,0x8b,0x83', 1)))
        lines = text.splitlines()
        entries = [i for i, line in enumerate(lines) if '{"pass_phase_' in line]
        self.assertEqual(len(entries), 4)
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
        with tempfile.TemporaryDirectory(prefix='x3-pass-phases-') as temporary:
            exe = Path(temporary) / 'pass_phases_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/pass_phases_host.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertRegex(run.stdout, r'^pass_phases_host checks=\d+ failures=0 accumulator_bytes=\d+ window_bytes=\d+ dispatch_cost_ns=\d+\n$')

    def test_production_wiring(self):
        source = (ROOT / 'src/proxy/pass_phases.cpp').read_text()
        header = (ROOT / 'src/proxy/pass_phases.h').read_text()
        core = (ROOT / 'src/proxy/pass_phases_core.h').read_text()
        # Off = inert: the environment gate, the frame boundary behind one
        # relaxed load, the handler's first instruction an `active` test.
        self.assertIn('L"X3M_PASS_PHASES"', source)
        self.assertIn('if (active.load(std::memory_order_relaxed)) detail::frame_impl(frame_index, sampled, view_submit_us);', header)
        handler = source[source.index('x3m_pass_phase_enter(unsigned index)'):]
        handler = handler[:handler.index('\n}')]
        self.assertIn('if(!active.load(std::memory_order_relaxed))return;', handler)
        # Lean boundary and handler: LightCallBoundary, no PreserveCpuState,
        # one QueryPerformanceCounter, the owner check, no log.
        self.assertIn('x3m::LightCallBoundary cpu;', handler)
        self.assertNotIn('PreserveCpuState', handler)
        self.assertEqual(handler.count('QueryPerformanceCounter('), 1)
        self.assertIn('gate.owned(GetCurrentThreadId())', handler)
        self.assertNotIn('log(', handler)
        self.assertIn('__attribute__((force_align_arg_pointer))', source)
        # The lean stub: flags, EAX/ECX/EDX and XMM0-7 saved, then the handler,
        # then everything restored before `jmp [next]`; no x87 save.
        emit = source[source.index('void* emit(unsigned index,void*** next_out) {'):]
        emit = emit[:emit.index('\n}')]
        self.assertIn('e.byte(0x9c);e.byte(0x50);e.byte(0x51);e.byte(0x52);e.byte(0xfc);', emit)
        self.assertEqual(emit.count('e.byte(0x0f);e.byte(0x11);'), 1)
        self.assertEqual(emit.count('e.byte(0x0f);e.byte(0x10);'), 1)
        self.assertIn('e.rel32(reinterpret_cast<const void*>(&x3m_pass_phase_enter));', emit)
        self.assertIn('e.byte(0x81);e.byte(0xc4);e.dword(0x80);e.byte(0x5a);e.byte(0x59);e.byte(0x58);e.byte(0x9d);', emit)
        self.assertIn('e.byte(0xff);e.byte(0x25);e.dword(std::uint32_t(next));', emit)
        # Install transaction: window, preflight, in-order claim, reverse
        # rollback, activate last; the group needs the frame group.
        self.assertIn('status="install_window_closed"', source)
        self.assertIn('status="preflight_bytes"', source)
        self.assertIn('else status=patches[i].status;', source)
        self.assertIn('if(!restored)status="rollback_failed_inert";', source)
        self.assertIn('status="frame_phases_off"', source)
        self.assertIn('std::atomic<bool> active', header)
        # The window closes at the frame-phase boundary under its owner guard,
        # with the frame's view_submit joined; a frame without a sample is dropped.
        frame = (ROOT / 'src/proxy/frame_phases.cpp').read_text()
        impl = frame[frame.index('void frame_impl(std::uint64_t frame) noexcept {'):]
        impl = impl[:impl.index('\n}')]
        self.assertLess(impl.index('if(!owner(false))return;'), impl.index('pass_phases::frame(frame,taken,taken?last_sample.view_submit_us:0);'))
        self.assertLess(impl.index('pass_phases::frame('), impl.index('if(!taken)return;'))
        self.assertIn('if(!sampled){accumulator.discard();++dropped;return;}', source)
        self.assertIn('log("pass_phases frame=%llu frames=%u passes_p50=%llu apply_p50_us=%llu apply_p95_us=%llu draw_p50_us=%llu draw_p95_us=%llu end_p50_us=%llu end_p95_us=%llu sum_p50_us=%llu view_submit_p50_us=%llu self_p50_us=%llu', source)
        self.assertIn('out.self_us = std::uint64_t(passes) * site_count * dispatch_cost_ns / 1000;', core)
        cost = int(re.search(r'inline constexpr std::uint64_t dispatch_cost_ns = (\d+);', core).group(1))
        self.assertTrue(0 < cost <= 370, cost)  # 1.5 ms / 4,024 dispatches
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertLess(capture.index('frame_phases::initialize();'), capture.index('pass_phases::initialize();'))
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('src/proxy/pass_phases.cpp'), 2)
        build = (ROOT / 'verification/probe/build_game_phase_cpu.py').read_text()
        self.assertIn("('src/proxy/pass_phases.cpp','pass')", build)
        audit = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        self.assertIn("'_x3m_pass_phase_enter'", audit)


class PassPhasesLaunchOption(unittest.TestCase):
    def test_launch_option_requires_telemetry_and_frame_phases_and_resets_inherited_value(self):
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        helper = LodScaleLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            for args in (('--pass-phases',), ('--pass-phases', '--telemetry')):
                code, _, error = helper.launch(directory, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('--pass-phases requires --telemetry and --frame-phases', error)
            code, _, error = helper.launch(directory, '--pass-phases', '--frame-phases')
            self.assertEqual(code, 2)
            self.assertIn('requires --telemetry', error)
            code, output, error = helper.launch(directory, '--pass-phases', '--frame-phases', '--telemetry')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_PASS_PHASES'], '1')
            self.assertEqual(env['X3M_FRAME_PHASES'], '1')
            code, output, error = helper.launch(directory, '--frame-phases', '--telemetry', inherited={'X3M_PASS_PHASES': '1'})
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_PASS_PHASES'], '0')


@unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed X3AP.exe unavailable')
class NativeSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = probe.DEFAULT_EXE.read_bytes()
        cls.image = probe.common.Image(cls.data)
        cls.decoded = probe.decode()
        cls.source = probe.SOURCE.read_text()

    def report(self, image=None, decoded=None):
        return probe.inspect(image or self.image, decoded or self.decoded, self.source, self.data)

    def test_actual_executable_and_all_four_spans(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report['checks'])
        self.assertTrue(report['source_present'])
        self.assertEqual(len(report['sites']), 4)
        for row in report['sites']:
            self.assertEqual(row['incoming_sources'], sorted(f'{s:#010x}' for s in probe.INCOMING[int(row['va'], 16)]), row['name'])
            self.assertTrue(row['plain_copy'] and row['arena_replay_ok'] and row['in_loop_body'], row['name'])

    def test_corrupted_byte_refused_at_every_site(self):
        for site in probe.SITES:
            with self.subTest(site=site.name):
                image = PatchedImage(self.image, [(site.va, bytes([site.expected[0] ^ 1]))])
                row = next(row for row in self.report(image=image)['sites'] if row['name'] == site.name)
                self.assertFalse(row['bytes_ok'])
                self.assertFalse(row['ok'])

    def test_interior_or_unexpected_incoming_edge_refused(self):
        for site in probe.SITES:
            with self.subTest(site=site.name):
                decoded = {probe.ROUTINE: list(self.decoded[probe.ROUTINE])}
                decoded[probe.ROUTINE].append(probe.common.Instruction(0x100, b'\xe9\0\0\0\0', 'jmp', hex(site.va + 1)))
                row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                self.assertFalse(row['no_interior_branch'])
                decoded = {probe.ROUTINE: list(self.decoded[probe.ROUTINE])}
                decoded[probe.ROUTINE].append(probe.common.Instruction(0x100, b'\xe9\0\0\0\0', 'jmp', hex(site.va)))
                row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                self.assertTrue(row['no_interior_branch'])
                self.assertFalse(row['incoming_ok'])
                self.assertFalse(row['ok'])

    def test_boundaries_back_edge_and_routine_shape_must_match(self):
        for site in probe.SITES:
            for boundary in ('start', 'end'):
                with self.subTest(site=site.name, boundary=boundary):
                    decoded = {probe.ROUTINE: [i for i in self.decoded[probe.ROUTINE]
                                               if (i.va != site.va if boundary == 'start' else i.end != site.end)]}
                    row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                    self.assertFalse(row['whole_instructions'])
        decoded = {probe.ROUTINE: [i for i in self.decoded[probe.ROUTINE] if i.va != 0x4c405b]}
        self.assertFalse(self.report(decoded=decoded)['checks']['back_edge'])
        decoded = {probe.ROUTINE: [i for i in self.decoded[probe.ROUTINE] if i.end != probe.ROUTINE[1]]}
        self.assertFalse(self.report(decoded=decoded)['checks']['complete_routine'])
        decoded = {probe.ROUTINE: [i for i in self.decoded[probe.ROUTINE] if i.va != 0x4c4050]}
        self.assertFalse(self.report(decoded=decoded)['checks']['frame_anchors'])
        self.assertEqual(probe.inspect(self.image, {}, self.source, self.data)['result'], 'FAIL')


if __name__ == '__main__':
    unittest.main()
