"""Independent checks of the six per-sector update stamps (X3M_LOOP_PHASES=1):
the site ledger against the installed EXE, refusals, the host accumulator/gate/
window probe, the production wiring (shared lean stub and install transaction)
and the launcher option. Twin of test_pass_phases.py. No game, no Wine."""
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
import verify_loop_phase_sites as probe
from verification.analysis.test_chase_lead_sites import PatchedImage

ROOT = Path(__file__).resolve().parents[2]


class SourceAndReplay(unittest.TestCase):
    def test_exact_production_order_and_spec_fields(self):
        self.assertEqual(len(probe.SITES), 6)
        self.assertEqual([s.va for s in probe.SITES], [0x43a38e, 0x43a394, 0x43a39a, 0x43a3a0, 0x43a3be, 0x43a3ca])
        self.assertEqual([len(s.expected) for s in probe.SITES], [6, 6, 6, 5, 6, 5])
        text = probe.SOURCE.read_text()
        self.assertTrue(probe.source_checks(text))
        # Four displaced calls carry their rel32 at offset 2; the two pass ends are plain copies.
        self.assertEqual(text.count('},6,0,2}'), 4)
        self.assertEqual(text.count('},5,0,0}'), 2)
        self.assertFalse(probe.source_checks(text.replace('},5,0,0}', '},5,0,2}', 1)))
        self.assertFalse(probe.source_checks(text.replace('},6,0,2}', '},6,0,0}', 1)))
        self.assertFalse(probe.source_checks(text.replace('0x8b,0x36,0x83,0x3e,0x00', '0x8b,0x36,0x83,0x3e,0x01', 1)))
        lines = text.splitlines()
        entries = [i for i, line in enumerate(lines) if '{"loop_phase_' in line]
        self.assertEqual(len(entries), 6)
        swapped = list(lines)
        swapped[entries[3]], swapped[entries[5]] = swapped[entries[5]], swapped[entries[3]]
        self.assertFalse(probe.source_checks('\n'.join(swapped)))
        self.assertIsNone(probe.source_checks(None))

    def test_arena_replay_keeps_call_targets_and_plain_copies(self):
        for site in probe.SITES:
            offset, target = probe.REL32[site.va]
            for arena in probe.ARENAS:
                with self.subTest(site=site.name, arena=hex(arena)):
                    code = probe.relocated_bytes(site, arena)
                    if not offset:
                        self.assertEqual(code, site.expected)
                        continue
                    self.assertEqual(code[:offset], site.expected[:offset])
                    self.assertEqual((arena + offset + 4 + probe.struct.unpack_from('<i', code, offset)[0]) & 0xffffffff, target)

    def test_host_accumulator_gate_and_window_probe(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-loop-phases-') as temporary:
            exe = Path(temporary) / 'loop_phases_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/loop_phases_host.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertRegex(run.stdout, r'^loop_phases_host checks=\d+ failures=0 accumulator_bytes=\d+ window_bytes=\d+ summary_bytes=\d+ dispatch_cost_ns=\d+ slow_limit=64\n$')

    def test_production_wiring(self):
        source = (ROOT / 'src/proxy/loop_phases.cpp').read_text()
        header = (ROOT / 'src/proxy/loop_phases.h').read_text()
        core = (ROOT / 'src/proxy/loop_phases_core.h').read_text()
        # Off = inert: the environment gate, the frame boundary behind one
        # relaxed load, the handler's first instruction an `active` test.
        self.assertIn('L"X3M_LOOP_PHASES"', source)
        self.assertIn('if (active.load(std::memory_order_relaxed)) detail::frame_impl(frame_index, sampled, dt_us, pre_render_us);', header)
        handler = source[source.index('x3m_loop_phase_enter(unsigned index)'):]
        handler = handler[:handler.index('\n}')]
        self.assertIn('if(!active.load(std::memory_order_relaxed))return;', handler)
        self.assertIn('x3m::LightCallBoundary cpu;', handler)
        self.assertNotIn('PreserveCpuState', handler)
        self.assertEqual(handler.count('QueryPerformanceCounter('), 1)
        self.assertIn('gate.owned(GetCurrentThreadId())', handler)
        self.assertNotIn('log(', handler)
        self.assertIn('__attribute__((force_align_arg_pointer))', source)
        # The shared lean stub and the shared install transaction, the frame-group prerequisite.
        self.assertIn('return lean_stub::emit(reinterpret_cast<const void*>(&x3m_loop_phase_enter),index,next_out);', source)
        self.assertIn('return stamp::install_group(patches,specs,&emit,installed,status);', source)
        self.assertIn('return stamp::uninstall_group(patches);', source)
        self.assertIn('status="frame_phases_off"', source)
        self.assertIn('std::atomic<bool> active', header)
        # The accumulator: pass ends with nothing open are the container walk,
        # every other mismatch is an orphan; the largest interval keeps its owner.
        self.assertIn('inline constexpr bool walk[site_count] = {false, false, false, true, false, true};', core)
        self.assertIn('if (open != close) { if (open != none || !walk[index]) ++orphans; }', core)
        self.assertIn('if (delta > max_ticks) { max_ticks = delta; max_owner = close; }', core)
        self.assertIn('inline constexpr std::uint64_t slow_threshold_us = 50000;', core)
        self.assertIn('inline constexpr unsigned window_frames = 300, slow_limit = 64;', core)
        cost = int(re.search(r'inline constexpr std::uint64_t dispatch_cost_ns = (\d+);', core).group(1))
        self.assertTrue(0 < cost <= 200, cost)
        # The window closes at the frame-phase boundary under its owner guard
        # with dt and pre_render joined; the game-phase input phase replaces
        # pre_render when present; a frame without a sample is dropped.
        frame = (ROOT / 'src/proxy/frame_phases.cpp').read_text()
        impl = frame[frame.index('void frame_impl(std::uint64_t frame) noexcept {'):]
        impl = impl[:impl.index('\n}')]
        self.assertLess(impl.index('if(!owner(false))return;'), impl.index('loop_phases::frame(frame,taken,taken?last_sample.dt_us:0,taken?last_sample.phase_us[detail::pre_render]:0);'))
        self.assertLess(impl.index('loop_phases::frame('), impl.index('if(!taken)return;'))
        self.assertIn('if(!sampled){accumulator.discard();++dropped;return;}', source)
        self.assertIn('game_phases::last_input_us(&input_us);', source)
        phases = (ROOT / 'src/proxy/game_phases.cpp').read_text()
        self.assertIn('GetCurrentThreadId()!=owner_thread.load(std::memory_order_acquire))return false;', phases)
        phase_core = (ROOT / 'src/proxy/game_phases_core.h').read_text()
        self.assertIn('if(phase==6){input_last=input_ticks;input_valid=true;}', phase_core)
        self.assertIn('log("loop_phases frame=%llu frames=%u sectors_p50=%llu containers_p50=%llu collide_p50_us=%llu collide_p95_us=%llu simulate_p50_us=%llu simulate_p95_us=%llu post_p50_us=%llu post_p95_us=%llu passb_p50_us=%llu passb_p95_us=%llu sum_p50_us=%llu input_p50_us=%llu self_p50_us=%llu dispatch_cost_ns=%llu max_interval_us=%llu max_interval_owner=%s slow=%u orphans=%llu clock_errors=%llu clock_failures=%llu unmatched=%llu dropped=%llu early=%u foreign=%u"', source)
        self.assertIn('log("loop_phases_slow frame=%llu dt_us=%llu sectors=%u containers=%u collide_us=%llu simulate_us=%llu post_us=%llu passb_us=%llu sum_us=%llu input_us=%llu max_interval_us=%llu max_interval_owner=%s"', source)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertLess(capture.index('pass_phases::initialize();'), capture.index('loop_phases::initialize();'))
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('src/proxy/loop_phases.cpp'), 2)
        self.assertEqual(cmake.count('src/proxy/lean_stub.cpp'), 2)
        build = (ROOT / 'verification/probe/build_game_phase_cpu.py').read_text()
        self.assertIn("('src/proxy/loop_phases.cpp','loop')", build)
        self.assertIn("('src/proxy/lean_stub.cpp','lean')", build)
        audit = (ROOT / 'verification/probe/check_no_x87.py').read_text()
        self.assertIn("'_x3m_loop_phase_enter'", audit)
        # The CPU fixture arena is doubled for the fixture build only.
        engine = (ROOT / 'src/proxy/engine_patch.cpp').read_text()
        self.assertIn('#ifndef X3M_GAME_PHASE_FIXTURE\nconstexpr unsigned arena_size=16384;\n#else\nconstexpr unsigned arena_size=32768;\n#endif', engine)


class LoopPhasesLaunchOption(unittest.TestCase):
    def test_launch_option_requires_telemetry_and_frame_phases_and_resets_inherited_value(self):
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        helper = LodScaleLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            for args in (('--loop-phases',), ('--loop-phases', '--telemetry')):
                code, _, error = helper.launch(directory, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('--loop-phases requires --telemetry and --frame-phases', error)
            code, _, error = helper.launch(directory, '--loop-phases', '--frame-phases')
            self.assertEqual(code, 2)
            self.assertIn('requires --telemetry', error)
            code, output, error = helper.launch(directory, '--loop-phases', '--frame-phases', '--telemetry')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_LOOP_PHASES'], '1')
            self.assertEqual(env['X3M_FRAME_PHASES'], '1')
            self.assertEqual(env['X3M_PASS_PHASES'], '0')
            code, output, error = helper.launch(directory, '--frame-phases', '--telemetry', inherited={'X3M_LOOP_PHASES': '1'})
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_LOOP_PHASES'], '0')


@unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed X3AP.exe unavailable')
class NativeSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = probe.DEFAULT_EXE.read_bytes()
        cls.image = probe.common.Image(cls.data)
        cls.decoded = probe.decode()
        cls.source = probe.SOURCE.read_text()
        cls.installed = probe.installed_spans(probe.INSTALLED.read_text())

    def report(self, image=None, decoded=None):
        return probe.inspect(image or self.image, decoded or self.decoded, self.source, self.data, self.installed)

    def test_actual_executable_and_all_six_spans(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report['checks'])
        self.assertTrue(report['source_present'])
        self.assertEqual(report['installed_sites_checked'], 47)
        self.assertEqual(len(report['sites']), 6)
        for row in report['sites']:
            self.assertEqual(row['incoming_sources'], sorted(f'{s:#010x}' for s in probe.INCOMING[int(row['va'], 16)]), row['name'])
            self.assertTrue(row['arena_replay_ok'] and row['arena_target_ok'] and row['in_loop_body'] and row['no_installed_conflict'], row['name'])
        # The two gate edges of each pass land on the pass-end span start only.
        self.assertEqual([len(row['incoming_sources']) for row in report['sites']], [0, 0, 0, 2, 0, 2])

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
                decoded = {k: list(v) for k, v in self.decoded.items()}
                decoded[probe.ROUTINE].append(probe.common.Instruction(0x100, b'\xe9\0\0\0\0', 'jmp', hex(site.va + 1)))
                row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                self.assertFalse(row['no_interior_branch'])
                decoded = {k: list(v) for k, v in self.decoded.items()}
                decoded[probe.ROUTINE].append(probe.common.Instruction(0x100, b'\xe9\0\0\0\0', 'jmp', hex(site.va)))
                row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                self.assertTrue(row['no_interior_branch'])
                self.assertFalse(row['incoming_ok'])
                self.assertFalse(row['ok'])

    def test_boundaries_back_edges_and_installed_conflicts_must_match(self):
        for site in probe.SITES:
            for boundary in ('start', 'end'):
                with self.subTest(site=site.name, boundary=boundary):
                    decoded = {k: list(v) for k, v in self.decoded.items()}
                    decoded[probe.ROUTINE] = [i for i in decoded[probe.ROUTINE]
                                              if (i.va != site.va if boundary == 'start' else i.end != site.end)]
                    row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                    self.assertFalse(row['whole_instructions'])
        decoded = {k: list(v) for k, v in self.decoded.items()}
        decoded[probe.ROUTINE] = [i for i in decoded[probe.ROUTINE] if i.va != 0x43a3a5]
        self.assertFalse(self.report(decoded=decoded)['checks']['back_edges'])
        overlapping = probe.inspect(self.image, self.decoded, self.source, self.data, [(0x43a3a2, 5)])
        self.assertFalse(next(row for row in overlapping['sites'] if row['name'] == 'loop_phase_sector_pass_a_end')['no_installed_conflict'])
        self.assertEqual(probe.inspect(self.image, {}, self.source, self.data, [])['result'], 'FAIL')


if __name__ == '__main__':
    unittest.main()
