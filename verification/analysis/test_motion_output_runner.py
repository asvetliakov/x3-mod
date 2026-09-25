"""Retained motion candidates cannot build or silently skip selected coverage."""
import contextlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import fixture_process
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

    def test_exposure_reference_uses_runtime_ceiling_and_explicit_override(self):
        self.assertEqual(runner.hdr_env_params({})['ev_max'], 1.3)
        self.assertEqual(runner.hdr_env_params({'X3M_HDR_EV_MAX': '1.5'})['ev_max'], 1.5)

    def test_all_legacy_hdr_cases_own_their_intended_exposure(self):
        hdr = {c['name']: c['hdr_env'] for c in runner.CASES if c['hdr']}
        automatic = {'seam-hdr-exposure', 'seam-hdr-exposure-offset', 'seam-ownership-hdr-exposure',
                     'seam-hdr-tonemap-fault', 'seam-hdr-meter-selftest-unlock', 'seam-hdr-tonemap-shader-absent',
                     'seam-taa-hdr-tonemap-auto', 'seam-taa-hdr-tonemap-fault'}
        automatic |= {f'bench-{size}-hdr-tonemap-taa-{suffix}' for size in ('1280x768', '5120x1440')
                      for suffix in ('off', 'on', 'sharpen-on')}
        manual = {f'seam-hdr-ramp-{suffix}': '0' for suffix in ('none', 'golden', 'punchy', 'decode-none', 'decode-srgb', 'clamp4', 'identity',
                                                                'dither', 'identity-dither')}
        manual.update({'seam-hdr-ramp-ev-minus2': '-2', 'seam-hdr-ramp-ev-plus1-punchy': '1', 'production-hdr-ramp-none': '0',
                       'seam-taa-hdr-tonemap-on': '0', 'seam-taa-hdr-tonemap-ev1': '1', 'seam-taa-hdr-tonemap-k0': '0',
                       'seam-ownership-taa-hdr-tonemap-on': '0', 'production-taa-hdr-tonemap-on': '0',
                       'seam-taa-hook-hdr-tonemap-on': '0', 'seam-taa-hdr-tonemap-sharpen-on': '0', 'seam-taa-hdr-tonemap-sharpen-dither': '0',
                       'seam-taa-cutout-blended': '0', 'seam-taa-cutout-opaque': '0',
                       'seam-taa-fade-route-routed': '0', 'seam-taa-fade-route-routed-perdraw': '0', 'seam-taa-fade-route-masked': '0',
                       'seam-taa-fade-route-sentinel': '0', 'seam-taa-fade-route-hover': '0', 'seam-taa-fade-route-original': '0',
                       'seam-taa-fade-route-behind': '0', 'seam-taa-fade-route-overlay': '0', 'seam-taa-fade-route-foreign': '0',
                       'seam-taa-fade-route-overlay-lightmap': '0', 'seam-taa-fade-route-overlay-lightmap-far-fade': '0',  # --light-map-far-fade twins
                       'seam-taa-fade-route-hull': '0',  # X3M_FADE_RT2_OWNER (fade-rt2-ownership.md): the owner cases and the hull script
                       'seam-taa-fade-route-routed-owner-lane': '0', 'seam-taa-fade-route-hover-age': '0', 'seam-taa-fade-route-original-owner-age': '0',
                       'seam-taa-fade-route-original-age': '0',
                       'seam-taa-fade-route-zonly-owner': '0', 'seam-taa-fade-route-zonly-unjit-owner': '0',  # the prepass parity under the owner
                       'seam-taa-fade-route-cutout-owner': '0', 'seam-taa-fade-route-cutout-refused': '0',  # the alpha-tested cutout (fade-alpha-cutout-ownership.md)
                       'seam-taa-fade-route-cutout-order-owner': '0', 'seam-taa-fade-route-cutout-mip-owner': '0', 'seam-taa-fade-route-cutout-mip-nobias-owner': '0',
                       **{f'seam-taa-fade-route-{n}-owner': '0' for n in ('routed', 'routed-perdraw', 'sentinel', 'hover', 'original', 'behind', 'overlay', 'foreign', 'hull')},
                       'seam-taa-cutout-opaque-get': '0',
                       'seam-ownership-bolt-shape-prims': '0', 'seam-ownership-bolt-shape-decl': '0'})  # the bolt footprint's shape-refusal script
        self.assertEqual({n for n, e in hdr.items() if e.get('X3M_HDR_EXPOSURE') == 'auto'}, automatic)
        self.assertEqual({n: e['X3M_HDR_EV_MANUAL'] for n, e in hdr.items() if e.get('X3M_HDR_EXPOSURE') == 'manual'}, manual)
        self.assertEqual((len(hdr), len(automatic), len(manual)), (110, 14, 57))  # + seam-ownership-shadow-alpha-route{,-less} (the lane's FP16 scene, no exposure mode) + seam-thin-vote-far-on-owner (no exposure mode)  # 4 seam-*lightmap-far-fade*, 7 seam-lightmap-widen-* and 4 seam-thin-vote-* cases set no exposure mode (runtime default)
        for name, env in hdr.items():
            with self.subTest(case=name):
                if name in automatic:
                    self.assertNotIn('X3M_HDR_EV_MANUAL', env)
                elif name not in manual:
                    self.assertNotIn('X3M_HDR_TONEMAP', env)  # identity, runtime baseline fixed zero
                    self.assertNotIn('X3M_HDR_EXPOSURE', env)
                    self.assertNotIn('X3M_HDR_EV_MANUAL', env)
        self.assertEqual(hdr['seam-taa-hdr-tonemap-k0']['X3M_FIXTURE_TAA_K'], '0')

    def test_render_state_resync_bound_includes_wrap_slots(self):
        self.assertEqual(runner.RS_SHADOW_STATES, 32)
        summary = dict(state_shadow='1', draws='12', rs_queries='64', rs_hits='32',
                       rs_gets=str(runner.RS_FILL_GETS + 32), rs_resyncs='1')
        runner.check_render_state('host', 7, summary, True, 1)
        for changed in ({'rs_hits': '31', 'rs_gets': str(runner.RS_FILL_GETS + 33)},
                        {'rs_gets': str(runner.RS_FILL_GETS + 31)}, {'rs_resyncs': '0'}):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                runner.check_render_state('host', 7, dict(summary, **changed), True, 1)
        # A late failed setter invalidates an entry without a later query: it is
        # counted apart from resyncs, and a resync still requires a shadow miss.
        runner.check_render_state('host', 0, dict(summary, rs_hits='64', rs_gets=str(runner.RS_FILL_GETS),
                                                  rs_resyncs='0', rs_invalidations='1'), True, 0)
        for changed in ({'rs_hits': '64', 'rs_gets': str(runner.RS_FILL_GETS)},
                        {'rs_hits': '64', 'rs_gets': str(runner.RS_FILL_GETS), 'rs_invalidations': '1'},
                        {'rs_hits': '63', 'rs_gets': str(runner.RS_FILL_GETS + 1), 'rs_resyncs': '0',
                         'rs_invalidations': '0'}):
            with self.subTest(changed=changed), self.assertRaises(AssertionError):
                runner.check_render_state('host', 0, dict(summary, **changed), True,
                                          int(changed.get('rs_resyncs', summary['rs_resyncs'])))
        # No resync still permits no shadow misses; shadow-off still requires
        # every query to be a native get, independent of the enlarged bound.
        runner.check_render_state('host', 0, dict(summary, rs_hits='64', rs_gets=str(runner.RS_FILL_GETS), rs_resyncs='0'), True, 0)
        runner.check_render_state('host', 0, dict(summary, state_shadow='0', rs_hits='0', rs_gets=str(runner.RS_FILL_GETS + 64)), False, 1)

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
            def fixture(command, *, build_dir, timeout, **kwargs):
                self.assertEqual((Path(build_dir), timeout), (Path(command[command.index('--workdir') + 1]), 360))
                return execute(command, **kwargs)
            stack.enter_context(patch.object(runner.fixture_process, 'run', side_effect=fixture))
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
                 'motion_output_device enabled=1 rt_mode=perdraw depth=1 state_shadow=1 scene_hook=0',
                 'state_hooks device=1 installed=1 reason=explicit state_shadow=1 rs_mode=shadow']
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
                         timing='cpu_qpc', jitter_writes=0, set_rt=20, lazy_flushes=0, lazy_mask_writes=0,
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

    def zonly_output(self):
        """Synthetic zonly report and trace: nine frames, prepass then material, controls at frames 2, 4 and 6;
        the capture window (route records and readbacks) covers frames 1-3, the Reset after frame 3 closing it."""
        report = ['MODE seam=0 enabled=1 jitter=1 taa=0 msaa=0']
        trace = []
        for f in range(runner.ZONLY_FRAMES):
            _, jx, jy = runner.expected_jitter(f)
            control = f in runner.ZONLY_CONTROL_FRAMES
            report += ['RESTORE frame=%d label=fill differences=0' % f, 'RESTORE frame=%d label=prepass differences=0' % f,
                       'RESTORE frame=%d label=draw differences=0' % f,
                       f'EXPECT frame={f} index=2 object=A routed=0 matched=0 jittered=1 prepass=1',
                       f'EXPECT frame={f} index=3 object=A routed=0 matched=0 jittered=1',
                       f'ZONLY frame={f} control={int(control)} jx={jx:.6f} jy={jy:.6f} pixels=2000 holes={1800 if control else 0}']
            if not control:
                report.append(f'COVERAGE frame={f} jitter=1 checked=4000 mismatches=0 material=2000 background=1500')
            if f == 3:
                report.append('RESET PASS')
            capture = f in runner.ZONLY_CAPTURE_FRAMES
            trace.append(f'motion_output_frame device=1 frame={f} latched=1 draws=3 routed=0 gate2=1 gate3=1 gate4=1 apply_failures=0 '
                         f'restore_failures=0 readbacks={2 if capture else 0} jitter=1 jittered=2 unjittered_depth_writers=0')
            if capture:
                trace.append(f'motion_route device=1 frame={f} index=2 gate=3 routed=0 matched=0 depth=0 jittered=1 vs={runner.ZONLY_VS} ps={"0" * 16} result=00000000')
                trace.append(f'motion_route device=1 frame={f} index=3 gate=4 routed=0 matched=0 depth=0 jittered=1 vs=53a0a641107ed76c ps=8759c7838bbc86c2 result=00000000')
        report.append('RESULT PASS checks=80 restorations=27 frames=9')
        return '\n'.join(report), '\n'.join(trace)

    def test_zonly_cases_and_validator(self):
        zonly = [c for c in runner.CASES if c['mode'] == 'zonly']
        self.assertEqual([c['name'] for c in zonly], ['production-zonly', 'seam-zonly'])
        self.assertTrue(all(c['jitter'] and not c['taa'] and c['enabled'] == '1' for c in zonly))
        text, trace = self.zonly_output()
        result = runner.validate_zonly('host', text, trace)
        self.assertEqual(result['unjittered_depth_writers'], {f: 0 for f in range(9)})
        self.assertEqual(result['holes'], {f: (1800 if f in (2, 4, 6) else 0) for f in range(9)})
        self.assertEqual(result['routes'], 6)
        self.assertEqual(result['capture_frames'], [1, 2, 3])
        bad = [(text, trace.replace('unjittered_depth_writers=0', 'unjittered_depth_writers=1', 1)),
               (text, trace.replace('jittered=2 unjittered', 'jittered=1 unjittered', 1)),
               (text.replace('pixels=2000 holes=0', 'pixels=2000 holes=7', 1), trace),
               (text.replace('pixels=2000 holes=1800', 'pixels=2000 holes=900', 1), trace),
               (text, trace.replace('index=2 gate=3 routed=0 matched=0 depth=0 jittered=1', 'index=2 gate=3 routed=0 matched=0 depth=0 jittered=0', 1)),
               (text, trace.replace(f'vs={runner.ZONLY_VS}', 'vs=53a0a641107ed76c', 1)),
               (text.replace('mismatches=0', 'mismatches=3', 1), trace),
               # A route record outside the capture window, and a capture frame that logged no readbacks.
               (text, trace + '\nmotion_route device=1 frame=5 index=2 gate=3 routed=0 matched=0 depth=0 jittered=1 '
                              f'vs={runner.ZONLY_VS} ps={"0" * 16} result=00000000'),
               (text, trace.replace('readbacks=2', 'readbacks=0', 1))]
        for output, log in bad:
            with self.subTest(output=(output != text, log != trace)), self.assertRaises(AssertionError):
                runner.validate_zonly('host', output, log)

    def fade_route_output(self, script, directory, lazy=True, shift_resolved=None):
        """Synthetic fade-route report, trace and per-frame images: quad Q carries a ramp
        that moves with the jitter in the raw FP16 scene; the resolved FP16 image is
        stable after a routed frame or moves with the jitter after a bracketed one
        unless shift_resolved overrides it. The scripts' per-frame decisions follow
        the runner's tables (hover: the hysteresis sequence)."""
        import run_linear_distance_fade_live as live_fade
        frames = runner.FADE_ROUTE_FRAMES
        report = ['MODE seam=1 enabled=1 jitter=1 taa=1 msaa=0']
        trace = []
        case = live_fade.sample_case(0); case['diffuse'][3] = 1.
        linear = live_fade.material.expected(live_fade.component.oracle_case(case)).linear_rgb
        encoded = live_fade.component.compose((0., 0., 0., 1.), [live_fade.component.fp16_rt_store(v) for v in linear], 1.)[:3]
        before = (.3, .2, .1, .5)
        original = script == 'original'  # original shading: no composition (mask_valid 0), the original program's colour, no linear_material_frame line
        for f in range(frames):
            _, jx, jy = runner.expected_jitter(f)
            routed, held = runner.fade_route_routed(script, f), runner.fade_route_held(script, f)
            matched = routed and f > 0 and runner.fade_route_routed(script, f - 1)
            permille = runner.fade_route_permille(script, f)
            history = int(f not in (0, runner.FADE_ROUTE_CUT_FRAME))
            report.append(f'FADE_ROUTE frame={f} script={script} routed={int(routed)} matched={int(matched)} fade_routed={2 * routed} fade_refused={2 * (not routed)} overlay_routed=0 overlay_refused=0 '
                          f'fade_held={2 * held} prepared={0 if original else 2 * (not routed)} covered=800 own_motion={2 * 196 * routed} mask_set={0 if original else 2 * 196 * (not routed)} mask_valid={int(not original)} threshold=500 draws=2')
            for x, y in runner.FADE_ROUTE_SAMPLES:
                if original:
                    after = (.45, .35, .25, before[3])
                elif script == 'hover':
                    alpha = .125 * runner.FADE_ROUTE_HOVER_ALPHA[f]
                    after = tuple(live_fade.component.fp16_rt_store(e * alpha + b * (1 - alpha)) for e, b in zip(encoded, before)) + (before[3],) if routed else \
                        live_fade.expected_composite(before, 0, alpha_value=runner.FADE_ROUTE_HOVER_ALPHA[f])
                elif routed:
                    after = live_fade.component.compose(before, [live_fade.component.fp16_rt_store(v) for v in linear], 1.)
                else:
                    after = live_fade.expected_composite(before, 0)
                report.append(f'FADE_ROUTE_SAMPLE frame={f} x={x} y={y} before={",".join(map(repr, before))} after={",".join(map(repr, after))}')
            report.append(f'TAA frame={f} history={history} cut={int(f == runner.FADE_ROUTE_CUT_FRAME)} changed=100 policy=2 skipped=0')
            trace.append(f'motion_output_frame device=1 frame={f} rt_mode={"lazy" if lazy else "perdraw"} draws=4 routed={1 + 2 * routed} gate4={0 if routed else 2} '
                         f'apply_failures=0 restore_failures=0 taa_resolved=1 taa_history={history}')
            if original:
                trace.append(f'fade_route_frame device=1 frame={f} fade_routed={2 * routed} fade_refused={2 * (not routed)} fade_held={2 * held} fade_route=500 cutout_caps=1 overlay_routed=0 overlay_refused=0 fade_evicted=0 fade_owner=0 fade_owner_masked=0')
            else:
                trace.append(f'linear_material_frame device=1 frame={f} routed={1 + 2 * routed} bump_routed=0 refused=0 bind_failures=0 cutout_routed=0 cutout_missed=0 '
                             f'cutout_unavailable=0 cutout_caps=1 fade_routed={2 * routed} fade_refused={2 * (not routed)} fade_held={2 * held} fade_route=500')
            if f in runner.FADE_ROUTE_CAPTURE:
                trace.append(f'motion_route device=1 frame={f} index=2 gate=0 routed=1 matched=1 depth=1 jittered=1 vs=53a0a641107ed76c ps=8759c7838bbc86c2 result=00000000 '
                             'zwrite=1 blend=0 src=2 dst=1 atest=0 mask=15 sepalpha=0 fog=0 fade_arm=0 fade_permille=0 fade_held=0')
                for index in (3, 4):
                    trace.append(f'motion_route device=1 frame={f} index={index} gate={0 if routed else 4} routed={int(routed)} matched={int(matched)} depth={int(routed)} jittered=1 '
                                 f'vs={runner.FADE_ROUTE_PAIR[0]} ps={runner.FADE_ROUTE_PAIR[1]} result=00000000 zwrite=0 blend=1 src={-1 if original else 5} dst={-1 if original else 6} atest=0 mask=7 sepalpha={-1 if original else 0} fog=0 '
                                 f'fade_arm={int(routed)} fade_permille={permille} fade_held={int(held)} unmatched={"none" if matched else "history" if routed else "fade_threshold"}')
            # Images: the raw ramp moves with the jitter; the resolved one stays put
            # after a routed frame or moves with the jitter after a bracketed one.
            raw = []
            shown = []
            sx, sy = (0., 0.) if routed or original else (jx, jy)
            if shift_resolved is not None:
                sx, sy = shift_resolved(f)
            for y in range(64):
                for x in range(64):
                    raw += [.3 + .02 * (x - jx), .3 + .02 * (y - jy), .5, 1.]
                    shown += [.3 + .02 * (x - sx), .3 + .02 * (y - sy), .5, 1.]
            (directory / f'fade_route_color_{f}.f32').write_bytes(struct.pack('<%df' % len(raw), *raw))
            (directory / f'reference_taa_{f}.rgba16f').write_bytes(struct.pack('<%de' % len(shown), *shown))
        report.append(f'FADE_ROUTE_CHECKS frames={frames} script={script} quads=2')
        report.append(f'RESULT PASS checks=900 restorations=36 frames={frames}')
        trace.append('fade_rt2_owner_configured requested=0 enabled=0 default=0 motion_output=1 taa=1 hdr=1 fade_route=500 lane=0')  # the runner pins off
        return '\n'.join(report), '\n'.join(trace)

    def fade_zonly_output(self, script):
        """Synthetic zonly / zonly-unjit report and trace: twelve frames, the witness dropping the whole interior (392)
        on the frames with jx > 0."""
        unjit = script == 'zonly-unjit'
        report, trace = [], []
        for f in range(runner.FADE_ROUTE_FRAMES):
            _, jx, jy = runner.expected_jitter(f)
            holes = runner.FADE_ZONLY_PIXELS if unjit and jx > 0 else 0
            report.append(f'FADE_ZONLY frame={f} script={script} jx={jx:.6f} jy={jy:.6f} pixels=392 fill_before=392 color_holes={holes} rt2_holes={holes} '
                          f'mismatch=0 outside_changed=0 max_depth_error=2.9e-08 routed=2 fade_routed=2')
            trace.append(f'motion_output_frame device=1 frame={f} latched=1 draws=5 jitter=1 jittered={3 if unjit else 5} unjittered_depth_writers={2 if unjit else 0}')
        report += [f'FADE_ZONLY_CHECKS frames=12 script={script} quads=2', 'RESULT PASS checks=500 restorations=60 frames=12']
        return '\n'.join(report), '\n'.join(trace)

    def fade_cutout_output(self, owner, variant='', bias=None):
        """Synthetic cutout report and trace (fade-alpha-cutout-ownership.md section 2.4): twelve frames with the counts
        of fade_cutout_model, three capture frames of motion_route records in the variant's draw order."""
        o = int(owner)
        m = runner.fade_cutout_model(variant)
        report, trace = [], [f'fade_rt2_owner_configured requested={o} enabled={o} default=0 motion_output=1 taa=1 hdr=1 fade_route=500 lane=1 tested={o}']
        for f in range(runner.FADE_ROUTE_FRAMES):
            _, jx, jy = runner.expected_jitter(f)
            fill = m['final_fill'] + (1 - o) * (m['final_panel'] + m['final_hull'])
            report.append(f'FADE_CUTOUT frame={f} script=cutout variant={variant or "base"} owner={o} jx={jx:.6f} jy={jy:.6f} classified={m["classified"]} '
                          f'panel_pass={m["panel_pass"]} hull_pass={m["hull_pass"]} panel_owned={o * m["panel_pass"]} hull_owned={o * m["hull_pass"]} '
                          f'final_panel={o * m["final_panel"]} final_hull={o * m["final_hull"]} final_fill={fill} model_mismatch=0 final_mismatch=0 parity_mismatch=0 '
                          f'subset_violations=0 outside_changed=0 motion_mismatch=0 max_depth_error=1.2e-08 max_w_error=0 '
                          f'panel_routed={o} panel_tested={o} panel_refused=0 panel_gate4={1 - o} hull_routed=1 hull_tested=0 fade_routed={1 + o} fade_tested={o} fade_refused=0 fade_owner_masked=0')
            trace.append(f'fade_route_frame device=1 frame={f} fade_routed={1 + o} fade_refused=0 fade_held=0 fade_route=500 cutout_caps=1 overlay_routed=0 overlay_refused=0 '
                         f'fade_evicted=0 fade_owner={o} fade_owner_masked=0 fade_tested={o}')
            trace.append(f'motion_output_frame device=1 frame={f} latched=1 draws=5 jitter=1 jittered=5 unjittered_depth_writers=0 mip_bias={float(bias or 0):g}')
            if f in runner.FADE_ROUTE_CAPTURE:
                vs, ps = runner.FADE_ROUTE_PAIR
                panel = 'gate=0 routed=1 matched=1 depth=1 fade_arm=1 fade_permille=1000 unmatched=none' if owner else 'gate=4 routed=0 matched=0 depth=0 fade_arm=0 fade_permille=0 unmatched=no_zwrite'
                draws = [('1', panel), ('0', 'gate=0 routed=1 matched=1 depth=1 fade_arm=1 fade_permille=1000 unmatched=none')]
                for atest, record in (draws[::-1] if variant == 'order' else draws):
                    trace.append(f'motion_route device=1 frame={f} index=4 {record} jittered=1 vs={vs} ps={ps} zwrite=0 blend=1 src=-1 dst=-1 atest={atest} mask=7 sepalpha=-1 result=00000000')
        report += [f'FADE_CUTOUT_CHECKS frames=12 script=cutout owner={o} quads=2', 'RESULT PASS checks=900 restorations=60 frames=12']
        return '\n'.join(report), '\n'.join(trace)

    def test_fade_cutout_cases_and_validator(self):
        cases = {c['name']: c for c in runner.CASES if c['name'] in runner.FADE_CUTOUT_CASES}
        self.assertEqual(sorted(cases), sorted(runner.FADE_CUTOUT_CASES))
        for name, (owner, variant, bias) in runner.FADE_CUTOUT_CASES.items():
            env = cases[name]['hdr_env']
            self.assertEqual((env['X3M_FIXTURE_FADE_SCRIPT'], env['X3M_SUN_SHADOW_LANE'], env['X3M_LINEAR_MATERIALS'], env.get('X3M_FADE_RT2_OWNER'),
                              env.get('X3M_FIXTURE_FADE_CUTOUT', ''), env['X3M_TAA_MIP_BIAS']), ('cutout', '1', '0', 'on' if owner else None, variant, bias or '0'))  # FADE_ROUTE_ENV pins 0
        # The model (the fixture's classification written independently): base and mip share the hole; order adds the band.
        self.assertEqual(runner.fade_cutout_model(''), runner.fade_cutout_model('mip'))
        base, order = runner.fade_cutout_model(''), runner.fade_cutout_model('order')
        self.assertEqual((base['hull_pass'], base['final_hull'], order['hull_pass'], order['final_hull']), (36, 36, 72, 36))
        self.assertEqual(runner.FADE_CUTOUT_TWINS, {'seam-taa-fade-route-cutout-refused': ('seam-taa-fade-route-cutout-owner', 64),
                                                    'seam-taa-fade-route-cutout-mip-nobias-owner': ('seam-taa-fade-route-cutout-mip-owner', 32)})
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            for kind in ('color', 'motion', 'rt2'):
                for f in range(runner.FADE_ROUTE_FRAMES):
                    (directory / f'fade_route_{kind}_{f}.f32').write_bytes(bytes([f]))
            for name, (owner, variant, bias) in runner.FADE_CUTOUT_CASES.items():
                text, trace = self.fade_cutout_output(owner, variant, bias)
                result = runner.validate_fade_cutout(name, owner, variant, bias, text, trace, directory)
                self.assertEqual((result['fade_tested_per_frame'], result['panel_owned_per_frame'], result['variant']),
                                 (int(owner), int(owner) * result['model']['panel_pass'], variant or 'base'))
            text, trace = self.fade_cutout_output(True)
            refused_text, refused_trace = self.fade_cutout_output(False)
            order_text, order_trace = self.fade_cutout_output(True, 'order')
            mip_text, mip_trace = self.fade_cutout_output(True, 'mip', '-0.5')
            bad = [(True, '', None, text.replace('panel_owned=232', 'panel_owned=231', 1), trace),
                   (True, '', None, text.replace('model_mismatch=0', 'model_mismatch=1', 1), trace),
                   (True, '', None, text.replace('final_mismatch=0', 'final_mismatch=3', 1), trace),
                   (True, '', None, text.replace('parity_mismatch=0', 'parity_mismatch=1', 1), trace),
                   (True, '', None, text.replace('subset_violations=0', 'subset_violations=1', 1), trace),
                   (True, '', None, text.replace('max_depth_error=1.2e-08', 'max_depth_error=1e-04', 1), trace),
                   (True, '', None, text, trace.replace('fade_owner_masked=0 fade_tested=1', 'fade_owner_masked=1 fade_tested=1', 1)),
                   (True, '', None, text, trace.replace('fade_owner_masked=0 fade_tested=1', 'fade_owner_masked=0 fade_tested=0', 1)),
                   (True, '', None, text, trace.replace('tested=1', 'tested=0', 1)),
                   (True, '', None, text, trace.replace('unjittered_depth_writers=0', 'unjittered_depth_writers=1', 1)),
                   (True, '', None, text, trace.replace('unmatched=none jittered=1 vs=', 'unmatched=no_zwrite jittered=1 vs=', 1)),
                   (False, '', None, refused_text.replace('panel_gate4=1', 'panel_gate4=0', 1), refused_trace),
                   (False, '', None, refused_text, refused_trace.replace('fade_tested=0', 'fade_tested=1', 1)),
                   (False, '', None, text, trace),
                   (True, 'order', None, order_text.replace('hull_pass=72', 'hull_pass=36', 1), order_trace),
                   (True, 'order', None, text, trace),  # the base counts and draw order under the order variant
                   (True, 'mip', '-0.5', mip_text, mip_trace.replace('mip_bias=-0.5', 'mip_bias=0', 1))]
            for owner, variant, bias, output, log in bad:
                with self.subTest(owner=owner, variant=variant, output=output[:40]), self.assertRaises(AssertionError):
                    runner.validate_fade_cutout('host', owner, variant, bias, output, log, directory)

    def test_fade_zonly_cases_and_validator(self):
        cases = {c['name']: c for c in runner.CASES if c['name'] in runner.FADE_ZONLY_CASES}
        self.assertEqual(sorted(cases), sorted(runner.FADE_ZONLY_CASES))
        self.assertTrue(all(c['hdr_env']['X3M_FADE_RT2_OWNER'] == 'on' and c['hdr_env']['X3M_MOTION_FRAME_LOG'] == '1'
                            and 'X3M_SUN_SHADOW_LANE' not in c['hdr_env'] for c in cases.values()))
        text, trace = self.fade_zonly_output('zonly')
        result = runner.validate_fade_zonly('host', 'zonly', text, trace)
        self.assertEqual((result['hole_frames'], set(result['unjittered_depth_writers'].values())), ([], {0}))
        text, trace = self.fade_zonly_output('zonly-unjit')
        result = runner.validate_fade_zonly('host', 'zonly-unjit', text, trace)
        self.assertEqual(result['hole_frames'], result['positive_jx_frames'])
        self.assertTrue(result['hole_frames'])
        self.assertEqual(set(result['unjittered_depth_writers'].values()), {2})
        good = self.fade_zonly_output('zonly')
        witness = self.fade_zonly_output('zonly-unjit')
        bad = [('zonly', good[0].replace('color_holes=0 rt2_holes=0', 'color_holes=1 rt2_holes=1', 1), good[1]),
               ('zonly', good[0].replace('mismatch=0', 'mismatch=1', 1), good[1]),
               ('zonly', good[0].replace('color_holes=0 rt2_holes=0', 'color_holes=0 rt2_holes=3', 1), good[1]),
               ('zonly', good[0].replace('fill_before=392', 'fill_before=391', 1), good[1]),
               ('zonly', good[0].replace('max_depth_error=2.9e-08', 'max_depth_error=1e-04', 1), good[1]),
               ('zonly', good[0], good[1].replace('unjittered_depth_writers=0', 'unjittered_depth_writers=1', 1)),
               ('zonly', good[0], good[1].replace('jittered=5', 'jittered=4', 1)),
               ('zonly-unjit', witness[0], witness[1].replace('unjittered_depth_writers=2', 'unjittered_depth_writers=0', 1)),
               ('zonly-unjit', witness[0].replace('color_holes=392 rt2_holes=392', 'color_holes=100 rt2_holes=100', 1), witness[1])]
        for script, output, log in bad:
            with self.subTest(script=script, output=output != good[0]), self.assertRaises(AssertionError):
                runner.validate_fade_zonly('host', script, output, log)

    def test_fade_route_cases_and_validator(self):
        cases = [c for c in runner.CASES if c['mode'] == 'faderoute']
        self.assertEqual([(c['name'], c['lazy'], c['hdr_env']['X3M_FIXTURE_FADE_SCRIPT']) for c in cases],
                         [('seam-taa-fade-route-routed', True, 'routed'), ('seam-taa-fade-route-routed-perdraw', False, 'routed'), ('seam-taa-fade-route-masked', True, 'masked'),
                          ('seam-taa-fade-route-sentinel', True, 'sentinel'), ('seam-taa-fade-route-hover', True, 'hover'),
                          ('seam-taa-fade-route-original', True, 'original'), ('seam-taa-fade-route-behind', True, 'behind'),
                          ('seam-taa-fade-route-overlay', True, 'overlay'), ('seam-taa-fade-route-foreign', True, 'foreign'),
                          ('seam-taa-fade-route-overlay-lightmap', True, 'overlay'), ('seam-taa-fade-route-overlay-lightmap-far-fade', True, 'overlay'),
                          # X3M_FADE_RT2_OWNER (fade-rt2-ownership.md section 7)
                          ('seam-taa-fade-route-routed-owner', True, 'routed'), ('seam-taa-fade-route-routed-perdraw-owner', False, 'routed'),
                          ('seam-taa-fade-route-sentinel-owner', True, 'sentinel'), ('seam-taa-fade-route-hover-owner', True, 'hover'),
                          ('seam-taa-fade-route-original-owner', True, 'original'), ('seam-taa-fade-route-behind-owner', True, 'behind'),
                          ('seam-taa-fade-route-overlay-owner', True, 'overlay'), ('seam-taa-fade-route-foreign-owner', True, 'foreign'),
                          ('seam-taa-fade-route-hull-owner', True, 'hull'), ('seam-taa-fade-route-hull', True, 'hull'),
                          ('seam-taa-fade-route-routed-owner-lane', True, 'routed'), ('seam-taa-fade-route-hover-age', True, 'hover'),
                          ('seam-taa-fade-route-original-owner-age', True, 'original'), ('seam-taa-fade-route-original-age', True, 'original'),
                          ('seam-taa-fade-route-zonly-owner', True, 'zonly'), ('seam-taa-fade-route-zonly-unjit-owner', True, 'zonly-unjit'),
                          ('seam-taa-fade-route-cutout-owner', True, 'cutout'), ('seam-taa-fade-route-cutout-refused', True, 'cutout'),
                          ('seam-taa-fade-route-cutout-order-owner', True, 'cutout'), ('seam-taa-fade-route-cutout-mip-owner', True, 'cutout'),
                          ('seam-taa-fade-route-cutout-mip-nobias-owner', True, 'cutout')])
        owners = {c['name'] for c in cases if c['hdr_env'].get('X3M_FADE_RT2_OWNER') == 'on'}
        self.assertEqual(owners, {c['name'] for c in cases if '-owner' in c['name']})
        # The lane (four-channel RT2) on the original-shading owner cases only; the thin region on hover-owner only.
        self.assertEqual({c['name'] for c in cases if c['hdr_env'].get('X3M_SUN_SHADOW_LANE') == '1'},
                         {f'seam-taa-fade-route-{n}-owner' for n in ('original', 'behind', 'overlay', 'foreign', 'hull')}
                         | {'seam-taa-fade-route-routed-owner-lane', 'seam-taa-fade-route-original-owner-age', 'seam-taa-fade-route-original-age'}
                         | set(runner.FADE_CUTOUT_CASES))
        self.assertEqual({c['name'] for c in cases if 'X3M_TAA_THIN_REGION' in c['hdr_env']},
                         {'seam-taa-fade-route-hover-owner', 'seam-taa-fade-route-hover-age', 'seam-taa-fade-route-original-owner-age', 'seam-taa-fade-route-original-age'})
        self.assertEqual(set(runner.FADE_ROUTE_AGE_FRESH), {'seam-taa-fade-route-hover-owner'} | set(runner.FADE_ROUTE_AGE_CASES))
        # The original-shading cases (run 125; run 130's origin behind the camera and same-node overlay) turn linear materials and the fade bracket off; every other case keeps both on.
        self.assertTrue(all(c['jitter'] and c['taa'] and c['hdr'] and c['hdr_env']['X3M_FIXTURE_TAA_SENTINEL'] == '2'
                            and c['hdr_env']['X3M_FIXTURE_CAMERA'] == 'rotate' and 'X3M_FADE_ROUTE' not in c['hdr_env'] for c in cases))
        self.assertEqual({c['name']: (c['hdr_env']['X3M_LINEAR_MATERIALS'], c['hdr_env']['X3M_LINEAR_DISTANCE_FADE']) for c in cases},
                         {c['name']: (('0', '0') if c['hdr_env']['X3M_FIXTURE_FADE_SCRIPT'] in runner.FADE_ROUTE_ORIGINAL_SCRIPTS else ('1', '1')) for c in cases})
        self.assertEqual((runner.FADE_ROUTE_ENV['X3M_CAPTURE_START'], runner.FADE_ROUTE_ENV['X3M_CAPTURE_FRAMES']), ('2', '3'))
        # The hover tables: 507 arms, 449 is held, 390 disarms, 449 stays refused, 507 arms again.
        self.assertEqual(runner.FADE_ROUTE_HOVER_PERMILLE, (507, 449, 449, 390, 449, 449, 507, 449, 390, 507, 449, 449))
        self.assertEqual([runner.fade_route_routed('hover', f) for f in range(12)], [True, True, True, False, False, False, True, True, False, True, True, True])
        self.assertEqual([runner.fade_route_held('hover', f) for f in range(12)], [False, True, True, False, False, False, False, True, False, False, True, True])
        for script, lazy in (('routed', True), ('routed', False), ('masked', True), ('sentinel', True), ('hover', True), ('original', True)):
            with self.subTest(script=script, lazy=lazy):
                directory = self.root / f'fade-{script}-{int(lazy)}'; directory.mkdir()
                text, trace = self.fade_route_output(script, directory, lazy)
                result = runner.validate_fade_route('host', script, lazy, text, trace, directory)
                self.assertEqual((result['frames'], result['history_frames']), (12, 10))
                self.assertGreaterEqual(result['raw_evidence'], runner.FADE_ROUTE_MIN_EVIDENCE)
                self.assertGreaterEqual(result['resolved_evidence'], runner.FADE_ROUTE_MIN_EVIDENCE)
                self.assertLess(result['worst_raw_error_px'], .02)
                self.assertLess(result['worst_resolved_residual_px'], .02)
                self.assertLessEqual(result['max_code_error'], 1.0)
                self.assertLessEqual(result['max_tolerance_fraction'], 1.0)
                self.assertEqual(result['shifts'][1]['history'], 1)
                self.assertEqual((result['routed_frames'], result['held_frames']), {'routed': (12, 0), 'masked': (0, 0), 'sentinel': (12, 0), 'hover': (8, 5), 'original': (8, 5)}[script])
                if script == 'hover':
                    self.assertEqual(result['switch_step']['permille'], 449)
                    self.assertGreater(result['switch_step']['max_relative'], 0)  # the native mix and the bracket differ at .09 alpha
        # Wrong binding mode, a fade draw the arm did not route, an unrouted route
        # record, the wrong estimate, a masked composite off by one code, a
        # history drop, and the resolved image moving with the jitter (the run-49
        # trembling) or standing still for the masked bracket.
        directory = self.root / 'fade-bad'; directory.mkdir()
        text, trace = self.fade_route_output('routed', directory)
        bad = [(text, trace, False),
               (text.replace('fade_routed=2', 'fade_routed=1', 1), trace, True),
               (text.replace('fade_held=0', 'fade_held=2', 1), trace, True),
               (text, trace.replace('fade_permille=1000 fade_held=0', 'fade_permille=1000 fade_held=1', 1), True),
               (text, trace.replace('index=3 gate=0 routed=1', 'index=3 gate=4 routed=0', 1), True),
               (text, trace.replace('fade_permille=1000', 'fade_permille=999', 1), True),
               (text, trace.replace('fade_route=500', 'fade_route=400', 1), True),
               (text.replace('TAA frame=3 history=1', 'TAA frame=3 history=0', 1), trace, True),
               (text, trace.replace('frame=3 rt_mode=lazy draws=4 routed=3', 'frame=3 rt_mode=lazy draws=4 routed=2', 1), True),
               (text, trace.replace('fade_rt2_owner_configured requested=0 enabled=0 default=0', 'fade_rt2_owner_configured requested=0 enabled=0 default=1'), True),
               (text, '\n'.join(l for l in trace.split('\n') if not l.startswith('fade_rt2_owner_configured ')), True)]
        for output, log, lazy in bad:
            with self.subTest(output=(output != text, log != trace, lazy)), self.assertRaises(AssertionError):
                runner.validate_fade_route('host', 'routed', lazy, output, log, directory)
        trembling = self.root / 'fade-trembling'; trembling.mkdir()
        text, trace = self.fade_route_output('routed', trembling, shift_resolved=lambda f: runner.expected_jitter(f)[1:])
        with self.assertRaises(AssertionError):
            runner.validate_fade_route('host', 'routed', True, text, trace, trembling)
        still = self.root / 'fade-still'; still.mkdir()
        text, trace = self.fade_route_output('masked', still, shift_resolved=lambda f: (0., 0.))
        with self.assertRaises(AssertionError):
            runner.validate_fade_route('host', 'masked', True, text, trace, still)
        masked = self.root / 'fade-masked-bad'; masked.mkdir()
        text, trace = self.fade_route_output('masked', masked)
        sample = next(l for l in text.splitlines() if l.startswith('FADE_ROUTE_SAMPLE frame=2 '))
        after = sample.split(' after=')[1].split(',')
        wrong = sample.replace(' after=' + ','.join(after), ' after=' + ','.join([repr(float(after[0]) + .01)] + after[1:]))  # beyond the .6% oracle tolerance
        with self.assertRaises(AssertionError):
            runner.validate_fade_route('host', 'masked', True, text.replace(sample, wrong), trace, masked)



class FixtureTimeoutCleanupTests(unittest.TestCase):
    """The per-case timeout ends only the fixture case's own processes."""
    CASE = '/Users/dev/x3-mod/verification/probe/build/motion-output-seam-on-1'
    WINE = '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine'

    def table(self):
        dos = 'Z:' + self.CASE.replace('/', '\\')
        return {1: (0, '/sbin/launchd'),
                10: (1, 'python3 verification/probe/run_motion_output.py'),
                100: (10, f'{self.WINE} --bottle X3 --no-update --dll d3d9=n,b --workdir {self.CASE} {self.CASE}/motion_output_fixture.exe Z:/tmp/a.fxo hook'),
                101: (100, f'{dos}\\motion_output_fixture.exe Z:\\tmp\\a.fxo hook'),
                102: (101, r'C:\windows\system32\winedbg.exe --auto 212 8820'),
                103: (100, '/usr/bin/some-helper --flag'),        # a descendant that is not this fixture
                104: (1, f'{dos}\\motion_output_fixture.exe'),     # the same case's fixture, reparented
                200: (1, r'C:\X3\X3AP.exe'),
                201: (200, r'C:\windows\system32\winedbg.exe --auto 300 99'),
                300: (1, f'{dos}-other\\motion_output_fixture.exe'),  # another case's directory (prefix)
                301: (1, f'tail -f {self.CASE}/wine.log'),           # mentions the directory only
                400: (1, r'C:\windows\system32\winedevice.exe')}

    def test_split_kills_only_the_case(self):
        kill, skipped = fixture_process.fixture_processes(self.table(), 100, self.CASE)
        self.assertEqual(kill, {100, 101, 102, 104})
        self.assertEqual(skipped, {103, 301})

    def test_cleanup_kills_descendants_waits_and_reports(self):
        state = self.table()
        killed = []

        class Child:
            pid = 100
            def kill(self):
                killed.append(100); state.pop(100, None)
            def wait(self, timeout=None):
                return -9

        def kill(pid, signal):
            killed.append(pid); state.pop(pid, None)

        with patch.object(fixture_process.os, 'kill', side_effect=kill):
            report = fixture_process.cleanup_after_timeout(Child(), self.CASE, table=lambda: dict(state), wait=1)
        self.assertEqual(sorted(killed), [100, 101, 102, 104])
        self.assertIn('ended 3: 101 ', report)
        self.assertIn('still alive after SIGTERM, 1 s and SIGKILL: none', report)
        self.assertRegex(report, r'not ours, left running: 103 .*301 ')
        self.assertTrue({200, 201, 300, 301, 103, 400} <= set(state))

    def test_witness_y_drive_fixture_and_new_ppid1_debugger_are_ended(self):
        case = ('/Users/dev/x3-mod/.claude/worktrees/agent-a/verification/probe/build/'
                'motion-output-seam-ownership-bolt-shape-prims-20260924-101010-000001')
        dos = 'Y:' + case[len('/Users/dev'):].replace('/', '\\')
        rows = {1: (0, '/sbin/launchd'),
                100: (10, f'{self.WINE} --bottle X3 --workdir {case} {case}/motion_output_fixture.exe Z:/tmp/a hook'),
                51295: (1, f'{dos}/motion_output_fixture.exe Z:'),                   # the witness's mixed separators
                51303: (1, 'winedbg --auto 204 212'),                                # verbatim witness debugger
                51400: (1, 'winedbg --auto 90 91'),                                  # already up before the case
                51500: (1, dos.replace('agent-a', 'agent-b') + r'\motion_output_fixture.exe'),  # another worktree
                51600: (1, dos + r'-other\motion_output_fixture.exe')}               # another case
        baseline = {51400: 'winedbg --auto 90 91'}
        kill, skipped = fixture_process.fixture_processes(rows, 100, case, baseline)
        self.assertEqual(kill, {100, 51295, 51303})
        self.assertNotIn(51400, kill | skipped)
        self.assertFalse({51500, 51600} & kill)
        # No baseline (ps failed before the case) or the game up: no PPID-1 debugger is ended.
        self.assertEqual(fixture_process.fixture_processes(rows, 100, case, None)[0], {100, 51295})
        rows[600] = (1, r'C:\X3\X3AP.exe')
        self.assertEqual(fixture_process.fixture_processes(rows, 100, case, baseline)[0], {100, 51295})
        del rows[600]
        state = dict(rows)

        class Child:
            pid = 100
            def kill(self):
                state.pop(100, None)
            def wait(self, timeout=None):
                return -9

        with patch.object(fixture_process.os, 'kill', side_effect=lambda pid, signal: state.pop(pid, None)):
            report = fixture_process.cleanup_after_timeout(Child(), case, table=lambda: dict(state), wait=1, baseline=baseline)
        self.assertIn('ended 2: 51295 ', report)
        self.assertIn('PPID-1 winedbg --auto that appeared during the case: 51303', report)
        self.assertEqual(set(state), {1, 51400, 51500, 51600})

    def test_command_must_name_its_build_directory(self):
        with self.assertRaises(ValueError), patch.object(fixture_process.subprocess, 'Popen') as popen:
            fixture_process.run(['wine', '/tmp/other/fixture.exe'], build_dir=self.CASE, timeout=1)
        popen.assert_not_called()

    def test_real_timeout_raises_with_cleanup(self):
        # A real child under a fake process table; os.kill may reach only that child.
        real_popen, real_kill = subprocess.Popen, fixture_process.os.kill
        children, faked = [], []

        def popen(*args, **kwargs):
            children.append(real_popen(*args, **kwargs))
            return children[-1]

        def kill(pid, number):
            if children and pid == children[0].pid:
                return real_kill(pid, number)
            faked.append(pid)
            state.pop(pid, None)

        state = {1: (0, '/sbin/launchd')}

        def table():
            if children and children[0].poll() is None:
                # The runner's child is the Wine launcher in production; show it in that form.
                state.setdefault(children[0].pid, (fixture_process.os.getpid(), f'/opt/wine/bin/wine --workdir {case} {command[-1]}'))
                state.setdefault(999001, (children[0].pid, r'C:\windows\system32\winedbg.exe --auto 1 2'))
            else:
                state.pop(children[0].pid if children else 0, None)
            return dict(state)

        with tempfile.TemporaryDirectory() as temporary:
            case = Path(temporary) / 'build' / 'case'
            command = [sys.executable, '-c', 'import time; time.sleep(30)', str(case / 'fixture.exe')]
            with patch.object(fixture_process.subprocess, 'Popen', side_effect=popen), \
                    patch.object(fixture_process.os, 'kill', side_effect=kill), \
                    self.assertRaises(fixture_process.FixtureTimeout) as error:
                fixture_process.run(command, build_dir=case, timeout=0.5, table=table, stdout=subprocess.PIPE, text=True)
        self.assertIsNotNone(children[0].poll())
        self.assertEqual(faked, [999001])
        self.assertIsInstance(error.exception, subprocess.TimeoutExpired)
        self.assertIn('ended 1: 999001 ', error.exception.cleanup)
        self.assertIn('fixture cleanup', repr(error.exception))

    def test_non_timeout_exception_kills_the_child(self):
        process = unittest.mock.MagicMock(pid=4242, stdin=None, stdout=None, stderr=None)
        process.communicate.side_effect = KeyboardInterrupt
        with patch.object(fixture_process.subprocess, 'Popen', return_value=process), \
                self.assertRaises(KeyboardInterrupt):
            fixture_process.run(['wine', self.CASE + '/x.exe'], build_dir=self.CASE, timeout=5, table=lambda: {})
        process.kill.assert_called_once_with()
        process.wait.assert_called_once_with(timeout=fixture_process.WAIT_SECONDS)

if __name__ == '__main__':
    unittest.main()
