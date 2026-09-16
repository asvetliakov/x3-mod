"""Independent checks of the ten frame-phase stamps (X3M_FRAME_PHASES=1): the
site ledger against the installed EXE, refusals, the host tracker/window
probe, the production wiring and the launcher option. No game, no Wine."""
import dataclasses
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
import verify_frame_phase_sites as probe
from verification.analysis.test_chase_lead_sites import PatchedImage

ROOT = Path(__file__).resolve().parents[2]


class SourceAndReplay(unittest.TestCase):
    def test_exact_production_order_and_spec_fields(self):
        self.assertEqual(len(probe.SITES), 10)
        self.assertEqual([s.va for s in probe.SITES], [0x471f6c, 0x472044, 0x4720b5, 0x472186, 0x47238d,
                                                        0x4724ec, 0x472574, 0x47224c, 0x472270, 0x4722c8])
        text = probe.SOURCE.read_text()
        self.assertTrue(probe.source_checks(text))
        self.assertIn('},6,0,2}', text)  # view_setup_begin: push esi, then the call's rel32 at offset 2
        self.assertFalse(probe.source_checks(text.replace('},6,0,2}', '},6,0,1}', 1)))
        self.assertFalse(probe.source_checks(text.replace('},5,0,1}', '},5,1,0}', 1)))
        lines = text.splitlines()
        entries = [i for i, line in enumerate(lines) if '{"frame_phase_' in line]
        self.assertEqual(len(entries), 10)
        swapped = list(lines)
        swapped[entries[0]], swapped[entries[1]] = swapped[entries[1]], swapped[entries[0]]
        self.assertFalse(probe.source_checks('\n'.join(swapped)))

    def test_every_rel32_arena_replay_preserves_destination_and_opcode(self):
        for site in probe.SITES:
            for arena in probe.ARENAS:
                with self.subTest(site=site.name, arena=hex(arena)):
                    code = probe.relocated_bytes(site, arena)
                    self.assertEqual(len(code), len(site.expected))
                    if site.va in probe.TARGETS:
                        offset = probe.RELATIVE[site.va]
                        self.assertEqual(code[:offset], site.expected[:offset])
                        self.assertEqual(code[offset - 1], 0xe8)
                        target = (arena + offset + 4 + probe.struct.unpack_from('<i', code, offset)[0]) & 0xffffffff
                        self.assertEqual(target, probe.TARGETS[site.va])
                    else:
                        self.assertEqual(code, site.expected)

    def test_host_tracker_and_window_probe(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-frame-phases-') as temporary:
            exe = Path(temporary) / 'frame_phases_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/frame_phases_host.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertRegex(run.stdout, r'^frame_phases_host checks=\d+ failures=0 tracker_bytes=\d+ window_bytes=\d+\n$')

    def test_production_wiring(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        present = capture.split('const HRESULT hr=fn(d,a,b,w,r);')
        self.assertEqual(len(present), 2)
        # Same placement as frame_timing: begin ahead of before_original, end
        # after after_original; the frame sample beside frame_timing::frame.
        self.assertLess(present[0].rindex('frame_phases::present_begin();'), present[0].rindex('cpu.before_original();'))
        self.assertLess(present[1].index('cpu.after_original();'), present[1].index('frame_phases::present_end();'))
        self.assertLess(present[1].index('frame_phases::present_end();'), present[1].index('game_phases::present_endpoint('))
        self.assertIn('frame_timing::frame(ctx.frame,ctx.draws);', present[1])
        self.assertLess(present[1].index('frame_timing::frame(ctx.frame,ctx.draws);'), present[1].index('frame_phases::frame(ctx.frame);'))
        self.assertLess(present[1].index('frame_phases::frame(ctx.frame);'), present[1].index('++ctx.frame;'))
        self.assertLess(capture.index('game_phases::initialize();'), capture.index('frame_phases::initialize();'))
        # The shared stub enters the game-phase CPU boundary; indices at or
        # above the phase group's count are the frame stamps.
        phases = (ROOT / 'src/proxy/game_phases.cpp').read_text()
        boundary = phases[phases.index('x3m_game_phase_enter(unsigned index'):]
        boundary = boundary[:boundary.index('\n}')]
        self.assertIn('x3m::PreserveCpuState cpu;', boundary)
        self.assertIn('if(index>=x3m::game_phases::sites::Count){x3m::frame_phases::stamp(index-x3m::game_phases::sites::Count);return;}', boundary)
        self.assertIn('return index<unsigned(sites::Count)+unsigned(frame_phases::sites::Count)?emit(index,next):nullptr;', phases)
        # The tail runs at the game's exact ESP: the stub restores everything
        # before its indirect jump to the continuation.
        self.assertIn('e.byte(0x81);e.byte(0xc4);e.dword(0x80);e.byte(0x61);e.byte(0x9d);', phases)
        source = (ROOT / 'src/proxy/frame_phases.cpp').read_text()
        self.assertIn('X3M_FRAME_PHASES', source)
        self.assertIn('log("frame_phases qpc=%llu frame=%llu frames=%u incomplete=%u dt_p50_us=%llu dt_p95_us=%llu%s views_p50=%llu', source)
        self.assertIn('log("frame_phases_slow frame=%llu dt_us=%llu%s view_setup_us=%llu view_submit_us=%llu views=%u complete=%u"', source)
        self.assertIn('status="install_window_closed"', source)
        self.assertIn('status="preflight_bytes"', source)
        self.assertIn('else status=patches[i].status;', source)
        self.assertIn('" truncated=1"', source)
        self.assertIn('std::atomic<bool> active', (ROOT / 'src/proxy/frame_phases.h').read_text())
        self.assertIn('if(!restored)status="rollback_failed_inert";', source)
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('src/proxy/frame_phases.cpp'), 2)
        build = (ROOT / 'verification/probe/build_game_phase_cpu.py').read_text()
        self.assertIn("('src/proxy/frame_phases.cpp','frame')", build)


class FramePhasesLaunchOption(unittest.TestCase):
    def test_launch_option_requires_telemetry_and_resets_inherited_value(self):
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        helper = LodScaleLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = helper.launch(directory, '--frame-phases')
            self.assertEqual(code, 2)
            self.assertIn('--frame-phases requires --telemetry', error)
            code, output, error = helper.launch(directory, '--frame-phases', '--telemetry')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_FRAME_PHASES'], '1')
            self.assertEqual(env['X3M_GAME_PHASES'], '0')
            code, output, error = helper.launch(directory, inherited={'X3M_FRAME_PHASES': '1'})
            self.assertEqual(code, 0, error)
            self.assertEqual(json.loads(output)['env']['X3M_FRAME_PHASES'], '0')


@unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed X3AP.exe unavailable')
class NativeSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = probe.DEFAULT_EXE.read_bytes()
        cls.image = probe.common.Image(cls.data)
        cls.decoded = probe.decode()
        cls.source = probe.SOURCE.read_text()

    def report(self, image=None, decoded=None):
        return probe.inspect(image or self.image, decoded or self.decoded, self.source)

    def test_actual_executable_and_all_ten_spans(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report['checks'])
        self.assertEqual(len(report['sites']), 10)
        self.assertTrue(report['checks']['no_data_reference'])
        for row in report['sites']:
            self.assertEqual(row['incoming_sources'], sorted(f'{s:#010x}' for s in probe.INCOMING[int(row['va'], 16)]), row['name'])

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
                decoded = {probe.FRAME: list(self.decoded[probe.FRAME])}
                decoded[probe.FRAME].append(probe.common.Instruction(0x100, b'\xe9\0\0\0\0', 'jmp', hex(site.va + 1)))
                row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                self.assertFalse(row['no_interior_branch'])
                decoded = {probe.FRAME: list(self.decoded[probe.FRAME])}
                decoded[probe.FRAME].append(probe.common.Instruction(0x100, b'\xe9\0\0\0\0', 'jmp', hex(site.va)))
                row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                self.assertTrue(row['no_interior_branch'])
                self.assertFalse(row['incoming_ok'])
                self.assertFalse(row['ok'])

    def test_relative_destination_and_boundaries_must_match(self):
        for site in probe.SITES:
            with self.subTest(site=site.name):
                if site.va in probe.TARGETS:
                    decoded = {probe.FRAME: [dataclasses.replace(i, operands='0x400000')
                                             if site.va <= i.va < site.end and probe.common._is_direct_control(i) is not None else i
                                             for i in self.decoded[probe.FRAME]]}
                    row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                    self.assertFalse(row['relative_contract'])
                for boundary in ('start', 'end'):
                    decoded = {probe.FRAME: [i for i in self.decoded[probe.FRAME]
                                             if (i.va != site.va if boundary == 'start' else i.end != site.end)]}
                    row = next(row for row in self.report(decoded=decoded)['sites'] if row['name'] == site.name)
                    self.assertFalse(row['whole_instructions'])

    def test_scene_hook_overlap_flag_consumer_and_routine_shape_refused(self):
        self.assertTrue(all(s.end <= 0x4721b1 or s.va >= 0x4721b6 for s in probe.SITES))
        decoded = {probe.FRAME: [dataclasses.replace(i, mnemonic='jg') if i.va == 0x472393 else i for i in self.decoded[probe.FRAME]]}
        self.assertFalse(self.report(decoded=decoded)['checks']['overlays_flag_consumer'])
        decoded = {probe.FRAME: [i for i in self.decoded[probe.FRAME] if i.va != 0x47260b]}
        checks = self.report(decoded=decoded)['checks']
        self.assertFalse(checks['complete_routine'])
        self.assertFalse(checks['single_ret_no_indirect_jump'])
        self.assertEqual(probe.inspect(self.image, {}, self.source)['result'], 'FAIL')


if __name__ == '__main__':
    unittest.main()
