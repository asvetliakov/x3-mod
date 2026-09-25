"""Independent checks of the twenty-two submit stamps (X3M_SUBMIT_PHASES=1): the
site ledger against the installed EXE, refusals, the host accumulator/window
probe, the production wiring (context lean stub, shared install transaction,
frame-boundary call, x87 audit root), the launcher option and the window-row
parser. Twin of test_residual_phases.py. No game, no Wine."""
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
import verify_submit_phase_sites as probe
from verification.analysis.test_chase_lead_sites import PatchedImage

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import summarize_submit_phases as rows  # noqa: E402


class SourceAndReplay(unittest.TestCase):
    def test_exact_production_order_and_spec_fields(self):
        self.assertEqual(len(probe.SITES), 22)
        self.assertEqual([s.va for s in probe.SITES][:7],
                         [0x47e620, 0x4722b4, 0x472490, 0x47e8f5, 0x47e264, 0x47e285, 0x47e315])
        text = probe.SOURCE.read_text()
        self.assertTrue(probe.source_checks(text))
        # A changed rel32 offset, a changed byte and a reordered table are all refused.
        self.assertFalse(probe.source_checks(text.replace('{0x56,0xe8,0x26,0xc4,0x00,0x00},6,0,2}', '{0x56,0xe8,0x26,0xc4,0x00,0x00},6,0,0}', 1)))
        self.assertFalse(probe.source_checks(text.replace('0xf6,0x80,0x70,0x02', '0xf6,0x80,0x70,0x03', 1)))
        lines = text.splitlines()
        entries = [i for i, line in enumerate(lines) if '{"submit_phase_' in line]
        self.assertEqual(len(entries), 22)
        swapped = list(lines)
        swapped[entries[0]], swapped[entries[1]] = swapped[entries[1]], swapped[entries[0]]
        self.assertFalse(probe.source_checks('\n'.join(swapped)))
        self.assertIsNone(probe.source_checks(None))
        # The role table of the core header has one role per site, in the same order.
        core = (ROOT / 'src/proxy/submit_phases_core.h').read_text()
        table = core[core.index('inline constexpr Role roles[site_count] = {'):]
        table = table[:table.index('};')]
        roles = re.findall(r'\{(\w+), (Open|Close)\}', table)
        self.assertEqual(len(roles), 22)
        names = [row[0] for row in probe.LEDGER]
        for (interval, action), name in zip(roles, names):
            opening = name.endswith(('_enter', '_begin'))
            self.assertEqual(action == 'Open', opening, name)
            self.assertTrue(name.startswith(re.sub(r'(?<!^)(?=[A-Z])', '_', interval).lower()), (interval, name))

    def test_rel32_replay_keeps_the_target_at_every_arena(self):
        self.assertEqual(sorted(probe.RELATIVE), [0x4722b4, 0x472490, 0x47e285, 0x47e8f5, 0x4c2251, 0x4c2316])
        for site in probe.SITES:
            for arena in probe.ARENAS:
                with self.subTest(site=site.name, arena=hex(arena)):
                    code = probe.relocated_bytes(site, arena)
                    if site.va in probe.RELATIVE:
                        offset, target = probe.RELATIVE[site.va]
                        self.assertEqual(code[:offset], site.expected[:offset])
                        self.assertEqual((arena + offset + 4 + struct.unpack_from('<i', code, offset)[0]) & 0xffffffff, target)
                    else:
                        self.assertEqual(code, site.expected)

    def test_host_accumulator_and_window_probe(self):
        compiler = shutil.which('clang++') or shutil.which('c++')
        self.assertIsNotNone(compiler, 'A host C++ compiler is required')
        with tempfile.TemporaryDirectory(prefix='x3-submit-phases-') as temporary:
            exe = Path(temporary) / 'submit_phases_host'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    str(ROOT / 'verification/probe/submit_phases_host.cpp'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            self.assertRegex(run.stdout, r'^submit_phases_host checks=\d+ failures=0 accumulator_bytes=\d+ window_bytes=\d+ dispatch_cost_ns=\d+\n$')

    def test_production_wiring(self):
        source = (ROOT / 'src/proxy/submit_phases.cpp').read_text()
        core = (ROOT / 'src/proxy/submit_phases_core.h').read_text()
        cost = int(re.search(r'inline constexpr std::uint64_t dispatch_cost_ns = (\d+);', core).group(1))
        self.assertTrue(0 < cost <= 370, cost)
        # The context stub, the shared transaction, the handler under LightCallBoundary and no x87 anywhere near it.
        self.assertIn('return lean_stub::emit_context(reinterpret_cast<const void*>(&x3m_submit_phase_enter),index,next_out);', source)
        self.assertIn('return stamp::install_group(patches,specs,&emit,installed,status);', source)
        handler = source[source.index('x3m_submit_phase_enter(unsigned index,const std::uint32_t* saved) {'):]
        handler = handler[:handler.index('\n}')]
        self.assertLess(handler.index('if(!active.load(std::memory_order_relaxed))return;'), handler.index('x3m::LightCallBoundary cpu;'))
        self.assertLess(handler.index('x3m::LightCallBoundary cpu;'), handler.index('if(!gate.owned(GetCurrentThreadId()))return;'))
        # The queue count precedes the sort's clock read; the walk count follows the walk's.
        self.assertLess(handler.index('accumulator.sorted(queue_length());'), handler.index('QueryPerformanceCounter(&v)'))
        self.assertLess(handler.index('accumulator.stamp(index,now);'), handler.index('accumulator.walked(walk_length('))
        self.assertNotIn('log(', handler)
        for word in ('float', 'double', 'long double'):
            self.assertNotRegex(source, r'\b' + word + r'\b')
        self.assertIn('constexpr std::uint32_t chase_cap=1u<<16;', source)
        stub = (ROOT / 'src/proxy/lean_stub.cpp').read_text()
        context = stub[stub.index('void* emit_context('):]
        self.assertIn('e.byte(0x9c);e.byte(0x60);e.byte(0xfc);', context)      # pushfd; pushad; cld
        self.assertIn('e.byte(0x61);e.byte(0x9d);', context)                  # popad; popfd
        self.assertNotRegex(context, r'0xdd|0xd9|0xdb|0xae')                  # no fnsave/frstor/fld/fxsave
        # Frame boundary under the frame group's owner guard; a frame without a sample is dropped.
        frame = (ROOT / 'src/proxy/frame_phases.cpp').read_text()
        impl = frame[frame.index('void frame_impl(std::uint64_t frame) noexcept {'):]
        impl = impl[:impl.index('\n}')]
        self.assertLess(impl.index('if(!owner(false))return;'), impl.index('submit_phases::frame(frame,taken);'))
        self.assertLess(impl.index('submit_phases::frame(frame,taken);'), impl.index('if(!taken)return;'))
        self.assertIn('if(!sampled){accumulator.discard();++dropped;return;}', source)
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertLess(capture.index('frame_phases::initialize();'), capture.index('submit_phases::initialize();'))
        self.assertLess(capture.index('submit_phases::initialize();'), capture.index('media_cue::initialize();'))
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertEqual(cmake.count('src/proxy/submit_phases.cpp'), 2)
        self.assertIn("('src/proxy/submit_phases.cpp','submit')", (ROOT / 'verification/probe/build_game_phase_cpu.py').read_text())
        self.assertIn("'_x3m_submit_phase_enter'", (ROOT / 'verification/probe/check_no_x87.py').read_text())
        fixture = (ROOT / 'verification/probe/submit_phase_cpu_fixture.cpp').read_text()
        for label in ('hit path closes each of the nine pairs exactly once and leaves none open',
                      'incoming x87 image (two live stack values, control word) survives every stamp',
                      'a skipped pass-loop guard closes the block at the End dispatch',
                      'the sampled hit counts the records up to the one in the saved ESI, through the saved EDI view',
                      'partial install rolled back to original bytes',
                      'install refused after the install window closed',
                      'documented submit dispatch cost within 2x of the measured cost'):
            self.assertIn(label, fixture)

    def test_x87_audit_pattern_catches_x87_and_ignores_hex_columns(self):
        import build_submit_phase_cpu as builder
        self.assertEqual(builder.X87.findall('   a:\td9 e8                \tfld1\n   c:\tf0 ff 00             \tlock inc DWORD PTR [eax]\n'), ['fld1'])
        self.assertEqual(builder.X87.findall('  10:\tdd 34 24             \tfnsave [esp]\n'), ['fnsave'])


class WindowRowParser(unittest.TestCase):
    def row(self, **overrides):
        values = {name: 0 for name in rows.FIELDS}
        values.update(frames=300, dispatch_cost_ns=95, walk_sample_period=16, stamps_p50=9000, self_p50_us=855,
                      sort_calls_p50=4, sort_p50_us=200, sort_p95_us=900, sort_nodes_p50=480, sort_nodes_max=700,
                      walk_calls_p50=900, walk_p50_us=450, walk_iterations_p50=405000, material_calls_p50=510,
                      material_p50_us=5100, material_net_p50_us=4500)
        values.update(overrides)
        return '[12.5] submit_phases ' + ' '.join(f'{k}={v}' for k, v in values.items())

    def test_format_string_and_parser_agree_on_the_field_list(self):
        source = (ROOT / 'src/proxy/submit_phases.cpp').read_text()
        call = source[source.index('log("submit_phases qpc='):source.index('emitted,s.frame,s.frames')]
        emitted = re.findall(r'(\w+)=%', ''.join(re.findall(r'"([^"]*)"', call)))
        # The parser reads by name, so only the set has to agree; no field is emitted twice.
        self.assertEqual(len(emitted), len(set(emitted)))
        self.assertEqual(sorted(emitted), sorted(rows.FIELDS))

    def test_rows_reduce_to_medians_and_derived_figures(self):
        text = [self.row(), self.row(sort_p50_us=100, sort_nodes_max=1200, idle=7), self.row(sort_p50_us=300, idle=5),
                'submit_phase_mode requested=1 enabled=1 status=ok', 'frame_phases qpc=1 frames=300']
        report = rows.summarize(text)
        self.assertEqual((report['windows'], report['rejected'], report['frames']), (3, 0, 900))
        self.assertEqual(report['pairs']['sort'], {'calls': 4, 'p50_us': 200, 'p95_us': 900, 'per_call_ns': 50000,
                                                   'nodes_p50': 480, 'nodes_max': 1200})
        self.assertEqual(report['pairs']['walk']['mean_walk_length'], 450.0)
        self.assertEqual(report['pairs']['material']['per_call_ns'], 10000)
        self.assertEqual(report['pairs']['material']['net_p50_us'], 4500)
        self.assertEqual(report['pairs']['end']['per_call_ns'], 0)
        self.assertEqual(report['health']['idle'], 12)
        self.assertEqual(report['self_p50_us'], 855)

    def test_malformed_rows_are_rejected_not_repaired(self):
        good = self.row()
        self.assertIsNotNone(rows.parse_row(good))
        self.assertIsNone(rows.parse_row(good.replace(' walk_p50_us=450', '')))
        self.assertIsNone(rows.parse_row(good.replace('walk_p50_us=450', 'walk_p50_us=nan')))
        self.assertIsNone(rows.parse_row(good.replace('walk_p50_us=450', 'walk_p50_us=-1')))
        self.assertIsNone(rows.parse_row(self.row(frames=0)))
        self.assertIsNone(rows.parse_row('residual_phases qpc=1'))
        report = rows.summarize([good, good.replace('sort_p50_us=200', 'sort_p50_us=x')])
        self.assertEqual((report['windows'], report['rejected']), (1, 1))
        self.assertEqual(rows.summarize(['nothing here']), {'windows': 0, 'rejected': 0, 'frames': 0})


class SubmitPhasesLaunchOption(unittest.TestCase):
    def test_launch_option_requires_telemetry_and_frame_phases(self):
        # Since the logging tiers (2026-09-26) the frame phases and telemetry come with --perf; this family pairs with them.
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        helper = LodScaleLaunchOption()
        with tempfile.TemporaryDirectory() as directory:
            for args in (('--submit-phases',), ('--submit-phases', '--debug')):
                code, _, error = helper.launch(directory, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('--submit-phases requires --perf', error)
            code, output, error = helper.launch(directory, '--submit-phases', '--perf')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_SUBMIT_PHASES'], env['X3M_PERF']), ('1', '1'))
            self.assertNotIn('X3M_FRAME_PHASES', env)  # the DLL's perf group turns the frame phases on
            # Independent of the pass and residual groups, and coexists with them.
            self.assertNotIn('X3M_PASS_PHASES', env); self.assertNotIn('X3M_RESIDUAL_PHASES', env)
            code, output, error = helper.launch(directory, '--submit-phases', '--residual-phases', '--perf')
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual((env['X3M_SUBMIT_PHASES'], env['X3M_RESIDUAL_PHASES'], env['X3M_PASS_PHASES']), ('1', '1', '1'))
            code, output, error = helper.launch(directory, '--perf', inherited={'X3M_SUBMIT_PHASES': '1'})
            self.assertEqual(code, 0, error)
            self.assertNotIn('X3M_SUBMIT_PHASES', json.loads(output)['env'])


@unittest.skipUnless(probe.DEFAULT_EXE.is_file(), 'installed X3AP.exe unavailable')
class NativeSites(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.data = probe.DEFAULT_EXE.read_bytes()
        cls.image = probe.common.Image(cls.data)
        cls.decoded = probe.decode()
        cls.source = probe.SOURCE.read_text()
        cls.claims, cls.anchored = probe.other_claims()

    def report(self, image=None, decoded=None, claims=None):
        return probe.inspect(image or self.image, decoded or self.decoded, self.source, self.data,
                             self.claims if claims is None else claims, self.anchored)

    def site(self, report, name):
        return next(row for row in report['sites'] if row['name'] == name)

    def test_actual_executable_and_all_spans(self):
        report = probe.verify()
        self.assertEqual(report['result'], 'PASS', report['checks'])
        self.assertTrue(report['source_present'])
        self.assertEqual(len(report['sites']), 22)
        self.assertGreaterEqual(report['other_claims_checked'], 33 + 10 + 4 + 2 + 6 + len(probe.FIXED_CLAIMS))
        self.assertEqual(report['raw_real_hits'], [])
        self.assertEqual(report['callers'], {'0x0047e620': ['0x004722af', '0x0047248b', '0x0047e8f0'],
                                             '0x004c0150': ['0x004c5228'], '0x004bdee0': ['0x0047e002', '0x0047e70c']})
        for row in report['sites']:
            self.assertTrue(row['no_x87'] and row['relative_ok'] and row['arena_replay_ok'] and row['no_claim_conflict'], row['name'])
        self.assertEqual(len(self.site(report, 'submit_phase_end_end')['incoming_sources']), 11)

    def test_corrupted_byte_refused_at_every_site(self):
        for site in probe.SITES:
            with self.subTest(site=site.name):
                image = PatchedImage(self.image, [(site.va, bytes([site.expected[0] ^ 1]))])
                row = self.site(self.report(image=image), site.name)
                self.assertFalse(row['bytes_ok'])
                self.assertFalse(row['ok'])

    def test_interior_or_unexpected_incoming_edge_refused(self):
        for site in probe.SITES:
            bounds = (site.function_start, site.function_end)
            with self.subTest(site=site.name):
                decoded = {k: list(v) for k, v in self.decoded.items()}
                decoded[bounds].append(probe.common.Instruction(0x100, b'\xe9\0\0\0\0', 'jmp', hex(site.va + 1)))
                self.assertFalse(self.site(self.report(decoded=decoded), site.name)['no_interior_branch'])
                decoded = {k: list(v) for k, v in self.decoded.items()}
                decoded[bounds].append(probe.common.Instruction(0x100, b'\xe9\0\0\0\0', 'jmp', hex(site.va)))
                row = self.site(self.report(decoded=decoded), site.name)
                self.assertFalse(row['incoming_ok'])
                self.assertFalse(row['ok'])

    def test_boundaries_conflicts_and_shape_checks_must_match(self):
        for site in probe.SITES:
            bounds = (site.function_start, site.function_end)
            with self.subTest(site=site.name):
                decoded = {k: list(v) for k, v in self.decoded.items()}
                decoded[bounds] = [i for i in decoded[bounds] if i.va != site.va]
                self.assertFalse(self.site(self.report(decoded=decoded), site.name)['whole_instructions'])
        # An overlap with any other claim is refused: the residual group's neighbour grown by one byte.
        overlapping = self.report(claims=self.claims + [(0x4c1eab, 0x4c1eb4)])
        self.assertFalse(self.site(overlapping, 'submit_phase_block_begin')['no_claim_conflict'])
        self.assertIn((0x4c1eab, 0x4c1eb3), self.claims)     # residual material_setup ends where block_begin starts
        self.assertIn((0x4c3ff0, 0x4c3ff6), self.claims)     # pass_begin starts where the guard's jbe ends
        # A relocated call target, a missing anchor and a lost caller all fail.
        image = PatchedImage(self.image, [(0x4c2252, b'\xb7')])
        self.assertFalse(self.site(self.report(image=image), 'submit_phase_inverse_world_begin')['bytes_ok'])
        decoded = {k: list(v) for k, v in self.decoded.items()}
        decoded[probe.TRAVERSAL] = [i for i in decoded[probe.TRAVERSAL] if i.va != 0x47e350]
        self.assertFalse(self.report(decoded=decoded)['checks']['anchors'])
        decoded = {k: list(v) for k, v in self.decoded.items()}
        decoded[probe.CALLER] = [i for i in decoded[probe.CALLER] if i.va != 0x4c5228]
        self.assertFalse(self.report(decoded=decoded)['checks']['return_sites_follow_calls'])
        self.assertEqual(probe.inspect(self.image, {}, self.source, self.data, [])['result'], 'FAIL')


if __name__ == '__main__':
    unittest.main()
