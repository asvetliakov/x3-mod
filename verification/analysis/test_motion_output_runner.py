"""Retained motion candidates cannot build or silently skip selected coverage."""
import contextlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

import run_motion_output as runner


class MotionOutputRunnerTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.inputs = {}
        for role in ('dll', 'seam', 'fixture'):
            path = self.root / ('retained-' + role)
            path.write_bytes(role.encode())
            self.inputs[role] = path
        self.args = [part for role, path in self.inputs.items() for part in ('--' + role, str(path))]

    def test_cli_rejects_incomplete_empty_unknown_or_missing_inputs(self):
        bad = [self.args, self.args + ['typo'], ['typo'],
               ['--dll', str(self.inputs['dll']), 'seam-burst-lazy-wrap'],
               self.args + ['--fixture', str(self.root / 'missing'), 'seam-on']]
        for argv in bad:
            with self.subTest(argv=argv), contextlib.redirect_stderr(io.StringIO()), patch.object(runner.subprocess, 'run') as run:
                with self.assertRaises(SystemExit) as error:
                    runner.main(argv)
                self.assertEqual(error.exception.code, 2)
                run.assert_not_called()

    def test_cli_preserves_normal_and_positional_selection(self):
        self.assertEqual(runner.arguments([]).cases, [])
        self.assertIsNone(runner.arguments(['seam-on']).dll)
        self.assertEqual(runner.arguments(self.args + ['seam-on']).cases, ['seam-on'])

    def test_exposure_auto_is_scoped(self):
        exposure = [c for c in runner.CASES if c['mode'] == 'hdrexposure']
        self.assertEqual(len(exposure), 3)
        self.assertTrue(all(c['hdr_env']['X3M_HDR_EXPOSURE'] == 'auto' for c in exposure))
        self.assertNotIn('X3M_HDR_EXPOSURE', runner.AGX)
        self.assertFalse(any(c['hdr_env'].get('X3M_FIXTURE_WRAP') == '1' for c in runner.CASES))

    def test_all_legacy_hdr_cases_own_their_intended_exposure(self):
        hdr = {c['name']: c['hdr_env'] for c in runner.CASES if c['hdr']}
        automatic = {'seam-hdr-exposure', 'seam-hdr-exposure-offset', 'seam-ownership-hdr-exposure',
                     'seam-hdr-tonemap-fault', 'seam-hdr-meter-selftest-unlock', 'seam-hdr-tonemap-shader-absent',
                     'seam-taa-hdr-tonemap-auto', 'seam-taa-hdr-tonemap-fault'}
        automatic |= {f'bench-{size}-hdr-tonemap-taa-{suffix}' for size in ('1280x768', '5120x1440')
                      for suffix in ('off', 'on', 'sharpen-on')}
        manual = {f'seam-hdr-ramp-{suffix}': '0' for suffix in ('none', 'golden', 'punchy', 'decode-none', 'decode-srgb', 'clamp4', 'identity')}
        manual.update({'seam-hdr-ramp-ev-minus2': '-2', 'seam-hdr-ramp-ev-plus1-punchy': '1', 'production-hdr-ramp-none': '0',
                       'seam-taa-hdr-tonemap-on': '0', 'seam-taa-hdr-tonemap-ev1': '1', 'seam-taa-hdr-tonemap-k0': '0',
                       'seam-ownership-taa-hdr-tonemap-on': '0', 'production-taa-hdr-tonemap-on': '0',
                       'seam-taa-hook-hdr-tonemap-on': '0', 'seam-taa-hdr-tonemap-sharpen-on': '0'})
        self.assertEqual({n for n, e in hdr.items() if e.get('X3M_HDR_EXPOSURE') == 'auto'}, automatic)
        self.assertEqual({n: e['X3M_HDR_EV_MANUAL'] for n, e in hdr.items() if e.get('X3M_HDR_EXPOSURE') == 'manual'}, manual)
        self.assertEqual((len(hdr), len(automatic), len(manual)), (48, 14, 17))
        for name, env in hdr.items():
            with self.subTest(case=name):
                if name in automatic:
                    self.assertNotIn('X3M_HDR_EV_MANUAL', env)
                elif name not in manual:
                    self.assertNotIn('X3M_HDR_TONEMAP', env)  # identity, runtime baseline fixed zero
                    self.assertNotIn('X3M_HDR_EXPOSURE', env)
                    self.assertNotIn('X3M_HDR_EV_MANUAL', env)
        self.assertEqual(hdr['seam-taa-hdr-tonemap-k0']['X3M_TAA_K'], '0')

    def test_render_state_resync_bound_includes_wrap_slots(self):
        self.assertEqual(runner.RS_SHADOW_STATES, 24)
        summary = dict(state_shadow='1', draws='12', rs_queries='48', rs_hits='24',
                       rs_gets=str(runner.RS_FILL_GETS + 24), rs_resyncs='1')
        runner.check_render_state('host', 7, summary, True, 1)
        for changed in ({'rs_hits': '23', 'rs_gets': str(runner.RS_FILL_GETS + 25)},
                        {'rs_gets': str(runner.RS_FILL_GETS + 23)}, {'rs_resyncs': '0'}):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                runner.check_render_state('host', 7, dict(summary, **changed), True, 1)
        # No resync still permits no shadow misses; shadow-off still requires
        # every query to be a native get, independent of the enlarged bound.
        runner.check_render_state('host', 0, dict(summary, rs_hits='48', rs_gets=str(runner.RS_FILL_GETS), rs_resyncs='0'), True, 0)
        runner.check_render_state('host', 0, dict(summary, state_shadow='0', rs_hits='0', rs_gets=str(runner.RS_FILL_GETS + 48)), False, 1)

    def run_selected(self, selectors, consume=True, mutate=False):
        build = self.root / 'build'
        build.mkdir(exist_ok=True)
        results = self.root / 'results'
        commands, validations = [], []

        def execute(command, **kwargs):
            commands.append(command)
            if command[0] != str(self.inputs['fixture']):
                self.assertFalse(consume, 'consume mode attempted a build')
                return subprocess.CompletedProcess(command, 0)
            directory = Path(command[command.index('--workdir') + 1])
            self.assertEqual((directory / self.inputs['fixture'].name).read_bytes(), b'fixture')
            expected_dll = b'dll' if 'production' in directory.name else b'seam'
            self.assertEqual((directory / 'd3d9.dll').read_bytes(), expected_dll)
            self.assertEqual(kwargs['env']['X3M_FIXTURE_WRAP'], '1' if directory.name.split('motion-output-')[1].startswith('seam-burst-lazy-wrap-') else '0')
            self.assertEqual(kwargs['env']['X3M_HDR_EXPOSURE'], 'fixed')
            self.assertEqual(kwargs['env']['X3M_HDR_EV_MANUAL'], '')
            captures = directory / 'x3-modern-captures'
            captures.mkdir()
            (captures / 'session-test.log').write_text('host dispatch test')
            if mutate:
                self.inputs['dll'].write_bytes(b'changed')
            return subprocess.CompletedProcess(command, 0, 'host dispatch test')

        def validate(*args, **kwargs):
            validations.append((args[0], kwargs['wrap']))
            return {'checks': 95 if kwargs['wrap'] else 86, 'set_rt_per_frame': {}}

        with contextlib.ExitStack() as stack:
            for name, value in {'ROOT': self.root, 'BUILD': build, 'RESULTS': results,
                                'EXE': self.inputs['fixture'], 'SEAM': self.inputs['seam'], 'DLL': self.inputs['dll'],
                                'WINE': self.inputs['fixture'], 'RAW': {}}.items():
                stack.enter_context(patch.object(runner, name, value))
            stack.enter_context(patch.object(runner, 'no_game'))
            stack.enter_context(patch.object(runner, 'sources', return_value={'source': 'stable'}))
            stack.enter_context(patch.object(runner.bottle, 'describe', return_value={'name': 'host mock'}))
            stack.enter_context(patch.object(runner.subprocess, 'run', side_effect=execute))
            stack.enter_context(patch.object(runner, 'validate_burst', side_effect=validate))
            with contextlib.redirect_stdout(io.StringIO()):
                if mutate:
                    with self.assertRaisesRegex(AssertionError, 'Binaries changed'):
                        runner.main(self.args + selectors)
                else:
                    runner.main((self.args if consume else []) + selectors)
        return json.loads((results / 'motion-output-partial.json').read_text()), commands, validations

    def test_consume_dispatches_only_selected_supplied_binaries_and_hashes(self):
        selected = ['production-burst-perdraw', 'seam-burst-lazy-wrap']
        with patch.dict(runner.os.environ, X3M_HDR_EXPOSURE='auto', X3M_HDR_EV_MANUAL='7'):
            result, commands, validations = self.run_selected(selected)
        self.assertEqual(len(commands), 2)
        self.assertEqual(result['build_commands'], [])
        self.assertFalse((self.root / 'results/motion-output-build.log').exists())
        self.assertEqual(result['binaries'], {str(p.resolve()): runner.sha(p) for p in self.inputs.values()})
        self.assertEqual(set(result['cases']), set(selected))
        self.assertEqual(validations, [(selected[0], False), (selected[1], True)])
        self.assertEqual(result['status'], 'PARTIAL')
        self.assertFalse(result['passed'])

    def test_retained_binary_mutation_fails(self):
        result, _, _ = self.run_selected(['seam-burst-lazy-wrap'], mutate=True)
        self.assertEqual(result['status'], 'FAIL')
        self.assertFalse(result['passed'])

    def test_normal_selected_run_keeps_all_four_build_commands(self):
        result, commands, _ = self.run_selected(['production-burst-perdraw'], consume=False)
        self.assertEqual(len(result['build_commands']), 4)
        self.assertEqual(commands[:4], result['build_commands'])
        self.assertEqual(len(commands), 5)

    def burst_output(self, wrap=True):
        """Small synthetic report; real burst validation still checks every oracle."""
        mode = dict(seam=1, enabled=1, jitter=0, jitter_samples=runner.JITTER_SAMPLES, taa=0, bench=0,
                    width=64, height=64, dll='test', burst=1, rt_mode='perdraw', camera=0, sentinel=0,
                    envmap=0, hook=0, state_shadow=1, hdr=0, hdrvalues=0, hdrfault=0, hdrramp=0,
                    hdrexposure=0, hdrtonemapfault=0, mipbias=0, mip_bias=0, sharpen=0, msaa=0)
        line = lambda label, values: label + ' ' + ' '.join(f'{k}={v}' for k, v in values.items())
        report = [line('MODE', mode)]
        trace = ['motion_output_mode rt_mode=perdraw frame_log=1 state_shadow=1',
                 'motion_output_device enabled=1 rt_mode=perdraw depth=1 state_shadow=1 scene_hook=0']
        if wrap:
            report.append('WRAP mode=hostile motion_texcoord=4 depth_texcoord=5 native_texcoord=0')
        captures = self.root / 'x3-modern-captures'
        captures.mkdir(exist_ok=True)
        for f in range(runner.BURST_FRAMES):
            if wrap:
                report.append('CHECK application reads exact WRAP4 after routed draw PASS')
            report += ['RESTORE label=fill differences=0', 'RESTORE label=burst differences=0',
                       f'COLOR frame={f} hash=test', f'STATE frame={f} hash=test',
                       f'MOTION_HASH frame={f} motion=test depth=test',
                       f'COVERAGE frame={f} mismatches=0 checked=4000',
                       'MOTION mismatches=0 depth_mismatches=0 matched=0 depth_written=2000']
            report += ['EXPECT matched=0 jittered=0'] * 8
            captured = f in runner.BURST_CAPTURE
            frame = dict(runner.BURST_EXPECT, frame=f, state_shadow=1, rs_queries=30, rs_hits=30,
                         rs_gets=runner.RS_FILL_GETS, rs_resyncs=0, scene_hook=0, hook_signals=0,
                         scene_end_source='none', scene_end_check=0, selector_state=9, latched=1, filled=1,
                         apply_failures=0, restore_failures=0, present='00000000', rt_mode='perdraw',
                         timing='cpu_qpc', jitter_writes=0, set_rt=20, lazy_flushes=0,
                         readbacks=2 if captured else 0, readback_us=1 if captured else 0,
                         gate_us=1, route_draw_us=1, set_rt_us=1, lazy_flush_us=0, fill_us=1)
            trace.append(line('motion_output_frame', frame))
            if captured:
                for i in range(7):
                    trace.append(f'motion_route frame={f} index={i} gate=5 routed={int(i < 5)} matched=0 result=00000000 depth={int(i < 5)}')
                for kind, prefix in [('motion', 'motion_output_readback'), ('depth', 'motion_output_depth_readback')]:
                    name = f'{kind}-{f}'
                    (captures / name).write_bytes(b'host report fixture')
                    trace.append(f'{prefix} frame={f} result=00000000 file={name}')
        trace.append('motion_output_release refs=0')
        report.append(f'RESULT PASS frames=9 restorations=18 checks={95 if wrap else 86} matched_pixels=0 depth_written=18000 coverage_pixels=36000')
        return '\n'.join(report), '\n'.join(trace)

    def test_real_burst_validator_requires_exact_hostile_wrap_evidence(self):
        text, trace = self.burst_output()
        result = runner.validate_burst('host', 'seam', False, text, trace, self.root, wrap=True)
        self.assertEqual(result['wrap_readback_checks'], 9)
        bad = [text.replace('WRAP mode=hostile', 'WRAP mode=benign'),
               text.replace('CHECK application reads exact WRAP4 after routed draw PASS\n', '', 1),
               text.replace('WRAP4 after routed draw PASS', 'WRAP5 after routed draw PASS', 1),
               text.replace('checks=95', 'checks=86'),
               text.replace('depth_mismatches=0', 'depth_mismatches=1', 1)]
        for output in bad:
            with self.subTest(output=output[-120:]), self.assertRaises(AssertionError):
                runner.validate_burst('host', 'seam', False, output, trace, self.root, wrap=True)
        with self.assertRaises(AssertionError):
            runner.validate_burst('host', 'seam', False, text, trace, self.root)
        text, trace = self.burst_output(wrap=False)
        self.assertEqual(runner.validate_burst('host', 'seam', False, text, trace, self.root)['checks'], 86)


if __name__ == '__main__':
    unittest.main()
