"""Host tests of the TAA image defaults (user decision after run 27, 2026-09-16):
tools/manage.py always forwards X3M_TAA_MIP_BIAS=-0.5 and X3M_TAA_SHARPEN=0.75 in
TAA mode, an explicit 0 still disables either, both are dropped outside TAA mode
even when inherited, and the DLL falls back to the same pair only with TAA on.
No game, no Wine."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
TAA = ['--motion-output', '--ownership', '--object-trace', '--object-lifetime', '--taa']


def load_manage():
    spec = importlib.util.spec_from_file_location('taa_defaults_manage', ROOT / 'tools/manage.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class TaaImageDefaultsLaunch(unittest.TestCase):
    def launch(self, directory, *args, inherited=None):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', '--vanilla', '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def env(self, directory, *args, inherited=None):
        code, output, error = self.launch(directory, *args, inherited=inherited)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env']

    def test_taa_mode_forwards_both_defaults(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA)
            self.assertEqual(env['X3M_TAA_MIP_BIAS'], '-0.5')
            self.assertEqual(env['X3M_TAA_SHARPEN'], '0.75')

    def test_defaults_override_a_stale_shell_value(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, inherited={'X3M_TAA_MIP_BIAS': '-3', 'X3M_TAA_SHARPEN': '1'})
            self.assertEqual(env['X3M_TAA_MIP_BIAS'], '-0.5')
            self.assertEqual(env['X3M_TAA_SHARPEN'], '0.75')

    def test_explicit_values_win_and_zero_disables(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, '--taa-mip-bias', '0', '--taa-sharpen', '0')
            self.assertEqual(float(env['X3M_TAA_MIP_BIAS']), 0.0)
            self.assertEqual(float(env['X3M_TAA_SHARPEN']), 0.0)
            env = self.env(directory, *TAA, '--taa-mip-bias', '-1', '--taa-sharpen', '0.5')
            self.assertEqual(env['X3M_TAA_MIP_BIAS'], '-1.0')
            self.assertEqual(env['X3M_TAA_SHARPEN'], '0.5')

    def test_without_taa_both_are_dropped_even_when_inherited(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, '--motion-output', inherited={'X3M_TAA_MIP_BIAS': '-0.5', 'X3M_TAA_SHARPEN': '0.75'})
            self.assertNotIn('X3M_TAA_MIP_BIAS', env)
            self.assertNotIn('X3M_TAA_SHARPEN', env)

    def test_options_still_require_taa(self):
        with tempfile.TemporaryDirectory() as directory:
            for option in ('--taa-mip-bias', '--taa-sharpen'):
                code, _, error = self.launch(directory, '--motion-output', option, '0.5')
                self.assertEqual(code, 2, option)
                self.assertIn(f'{option} requires --taa', error)

    def test_resolve_options_are_absent_unless_given(self):
        # --taa-history-weight (run 139 A/B): forwarded only when given, a stale
        # shell value dropped, range enforced.
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_TAA_HISTORY_WEIGHT', self.env(directory, *TAA, inherited={'X3M_TAA_HISTORY_WEIGHT': '0.5'}))
            self.assertEqual(self.env(directory, *TAA, '--taa-history-weight', '0.95')['X3M_TAA_HISTORY_WEIGHT'], '0.95')
            for value in ('0.99', '0.4', 'nan'):
                code, _, error = self.launch(directory, *TAA, '--taa-history-weight', value)
                self.assertEqual(code, 2, value)
                self.assertIn('--taa-history-weight must be within', error)
            code, _, error = self.launch(directory, '--motion-output', '--taa-history-weight', '0.9')
            self.assertEqual(code, 2)
            self.assertIn('--taa-history-weight requires --taa', error)

    def test_retired_resolve_variants_are_refused_by_name(self):
        # Cleanup batch 6 (2026-09-23; docs/architecture/cleanup-inventory-2026-09-22.md): the four
        # rejected / superseded resolve variants were retired; their refusal stubs went on 2026-09-25, so they are plain
        # unknown arguments, and a stale shell value of their variables never reaches the DLL (which no longer reads them either).
        retired = {'--taa-current-filter': 'X3M_TAA_CURRENT_FILTER', '--taa-line-filter': 'X3M_TAA_LINE_FILTER',
                   '--taa-thin-clip': 'X3M_TAA_THIN_CLIP', '--taa-adaptive-weight': 'X3M_TAA_ADAPTIVE_WEIGHT'}
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, inherited={name: '1' for name in retired.values()})
            for name in retired.values():
                self.assertNotIn(name, env)
            for option in retired:
                for extra in ((), ('1',)):
                    code, _, error = self.launch(directory, *TAA, option, *extra)
                    self.assertEqual(code, 2, (option, extra))
                    self.assertIn('unrecognized arguments', error)
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        for name in retired.values():
            self.assertNotIn(f'L"{name}"', source)

    def test_sky_history_defaults_to_strict_with_the_exit_reset(self):
        # Run 68 A (2026-09-23, docs/architecture/seta-sky-hull-share-decay.md): with --taa the launcher resolves
        # X3M_TAA_SKY_HISTORY=strict and X3M_TAA_SKY_HISTORY_EXIT_PX=0.25 (with an age program, else 0), and the DLL falls
        # back to the same pair when the launcher passes nothing: strict with TAA on, 0.25 under strict only.
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, '--taa-far-stabiliser', '0.985', inherited={'X3M_TAA_SKY_HISTORY': 'loose', 'X3M_TAA_SKY_HISTORY_EXIT_PX': '0'})
            self.assertEqual((env['X3M_TAA_SKY_HISTORY'], env['X3M_TAA_SKY_HISTORY_EXIT_PX']), ('strict', '0.25'))
            self.assertNotIn('X3M_TAA_SKY_HISTORY_BAND_PX', env)  # the band threshold stays at the DLL default 3
            env = self.env(directory, *TAA, '--taa-far-stabiliser', '0.985', '--taa-sky-history', 'loose', '--taa-sky-history-exit-px', '0')
            self.assertEqual((env['X3M_TAA_SKY_HISTORY'], float(env['X3M_TAA_SKY_HISTORY_EXIT_PX'])), ('loose', 0.0))
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('float taa_sky_history_exit_px = 0.f;', capture)
        self.assertIn('float taa_sky_history_band_px = 3.f;', capture)
        self.assertIn('taa_sky_history_strict=taa_requested;', capture)
        self.assertIn('else if(taa_sky_history_strict)taa_sky_history_exit_px=.25f;', capture)
        self.assertIn('L"X3M_TAA_SKY_HISTORY_EXIT_PX"', capture)

    def test_motion_weight_defaults_to_0_7_with_an_age_program(self):
        # Run 70 A (2026-09-23, run262/run263, docs/architecture/taa-motion-history-weight.md): with --taa the launcher
        # resolves X3M_TAA_MOTION_WEIGHT=0.7,2,8 with an age program (the camera policy is always auto since
        # 2026-09-25), else 0, always forwarded; the DLL falls back to the same value when the launcher passes nothing.
        with tempfile.TemporaryDirectory() as directory:
            age = ('--taa-far-stabiliser', '0.985', '--taa-thin-region', '0.97')
            self.assertEqual(self.env(directory, *TAA, *age, inherited={'X3M_TAA_MOTION_WEIGHT': '0'})['X3M_TAA_MOTION_WEIGHT'], '0.7,2,8')
            self.assertEqual(self.env(directory, *TAA, *age, '--taa-motion-weight', '0')['X3M_TAA_MOTION_WEIGHT'], '0,2,8')
            self.assertEqual(self.env(directory, *TAA, inherited={'X3M_TAA_MOTION_WEIGHT': '0.7,2,8'})['X3M_TAA_MOTION_WEIGHT'], '0')
            self.assertEqual(self.env(directory, *TAA, *age, '--taa-motion-weight', '0.8,2,8')['X3M_TAA_MOTION_WEIGHT'], '0.8,2,8')
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('float taa_motion_weight[3] = {0.f, 2.f, 8.f};', capture)
        self.assertIn('taa_motion_weight[0]=.7f;', capture)

    def test_far_stabiliser_is_absent_unless_given(self):
        # --taa-far-stabiliser W[,A[,F0,F1]] (docs/architecture/taa-distant-line-fade.md section 9): components separate.
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_TAA_FAR_STABILISER', self.env(directory, *TAA, inherited={'X3M_TAA_FAR_STABILISER': '0.985'}))
            for given, forwarded in (('0.985', '0.985,0,60,68,0.03,0.25'), ('0.985,1', '0.985,1,60,68,0.03,0.25'), ('0,1', '0,1,60,68,0.03,0.25'), ('0.97,0.5,100,160', '0.97,0.5,100,160,0.03,0.25'), ('0.985,0,80,130,0.5,2', '0.985,0,80,130,0.5,2')):
                self.assertEqual(self.env(directory, *TAA, '--taa-far-stabiliser', given)['X3M_TAA_FAR_STABILISER'], forwarded)
            for value in ('0.8', '0.995', 'nan', '0.985,5', '0.985,1,80', '0.985,1,130,80', '0.985,1,0,80', 'x', '0.985,1,80,130,1', '0.985,0,80,130,0.5,0.5', '0.985,0,80,130,-1,2', '0.985,0,80,130,0.1,65'):
                code, _, error = self.launch(directory, *TAA, '--taa-far-stabiliser', value)
                self.assertEqual(code, 2, value)
                self.assertIn('--taa-far-stabiliser', error)
            code, _, error = self.launch(directory, '--motion-output', '--taa-far-stabiliser', '0.985')
            self.assertEqual(code, 2)
            self.assertIn('--taa-far-stabiliser requires --taa', error)

    def test_unmatched_static_defaults_to_node_with_taa(self):
        # --taa-unmatched-static off|node|all (docs/architecture/temporal-integration.md,
        # docs/architecture/taa-lattice-crawl.md): run212 (no approach flash, 22-draw unmatched groups filled
        # on 36 approach frames) made node the default whenever the TAA route is on; "off" is the A/B opt-out.
        with tempfile.TemporaryDirectory() as directory:
            self.assertEqual(self.env(directory, *TAA)['X3M_TAA_UNMATCHED_STATIC'], 'node')
            # A stale shell value can neither change the resolved default nor survive a launch without --taa.
            self.assertEqual(self.env(directory, *TAA, inherited={'X3M_TAA_UNMATCHED_STATIC': 'all'})['X3M_TAA_UNMATCHED_STATIC'], 'node')
            self.assertNotIn('X3M_TAA_UNMATCHED_STATIC', self.env(directory, '--motion-output', inherited={'X3M_TAA_UNMATCHED_STATIC': 'all'}))
            for value in ('off', 'node', 'all'):
                self.assertEqual(self.env(directory, *TAA, '--taa-unmatched-static', value)['X3M_TAA_UNMATCHED_STATIC'], value)
            code, _, error = self.launch(directory, *TAA, '--taa-unmatched-static', '1')
            self.assertEqual(code, 2)
            code, _, error = self.launch(directory, '--motion-output', '--taa-unmatched-static', 'node')
            self.assertEqual(code, 2)
            self.assertIn('--taa-unmatched-static requires --taa', error)
        # Native fallback: absent means node with the TAA route (which implies the motion route), "off"/"0" is
        # the explicit opt-out, and the route applies it only on the miss path.
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('unsigned taa_unmatched_static = 0;', capture)
        self.assertIn('if(!wcscmp(setting,L"node"))taa_unmatched_static=1;', capture)
        self.assertIn('else if(wcscmp(setting,L"0")!=0&&wcscmp(setting,L"off")!=0)log("taa_unmatched_static_setting invalid=1");', capture)
        self.assertIn('else if(taa_requested)taa_unmatched_static=1;', capture)
        # The default is resolved after X3M_TAA is parsed, so it sees the final route state.
        self.assertLess(capture.index('taa_requested=motion_output_requested &&'), capture.index('GetEnvironmentVariableW(L"X3M_TAA_UNMATCHED_STATIC"'))
        self.assertIn('hooked.motion_output.configure_unmatched_static(taa_unmatched_static);', capture)
        self.assertIn('unsigned unmatched_static_ = 0;', (ROOT / 'src/proxy/motion_output.h').read_text())
        self.assertIn('if (unmatched_static_) route.static_assumed = unmatched_static_rows(route, rows, previous);', (ROOT / 'src/proxy/motion_output.cpp').read_text())
        # The motion-output fixture runner pins the pre-run212 off value: its scripts' oracles model it.
        self.assertIn("X3M_TAA_UNMATCHED_STATIC='0',", (ROOT / 'verification/probe/run_motion_output.py').read_text())

    def test_thin_region_is_absent_unless_given(self):
        # --taa-thin-region W[,RELAX[,LO,HI]] (docs/architecture/taa-lattice-crawl.md section 13).
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_TAA_THIN_REGION', self.env(directory, *TAA, inherited={'X3M_TAA_THIN_REGION': '0.97'}))
            for given, forwarded in (('0.97', '0.97,1'), ('0.985,0.5', '0.985,0.5'), ('0.97,1,0.05,0.5', '0.97,1,0.05,0.5')):
                self.assertEqual(self.env(directory, *TAA, '--taa-thin-region', given)['X3M_TAA_THIN_REGION'], forwarded)
            self.assertEqual(self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-far-stabiliser', '0.985')['X3M_TAA_THIN_REGION'], '0.97,1')
            # One shared speed gate: given on either option it reaches both; given on both it must agree.
            env = self.env(directory, *TAA, '--taa-thin-region', '0.97,1,0.05,0.5', '--taa-far-stabiliser', '0.985')
            self.assertEqual((env['X3M_TAA_THIN_REGION'], env['X3M_TAA_FAR_STABILISER']), ('0.97,1,0.05,0.5', '0.985,0,60,68,0.05,0.5'))
            env = self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-far-stabiliser', '0.985,0,80,130,0.05,0.5')
            self.assertEqual((env['X3M_TAA_THIN_REGION'], env['X3M_TAA_FAR_STABILISER']), ('0.97,1', '0.985,0,80,130,0.05,0.5'))
            env = self.env(directory, *TAA, '--taa-thin-region', '0.97,1,0.05,0.5', '--taa-far-stabiliser', '0.985,0,80,130,0.05,0.5')
            self.assertEqual(env['X3M_TAA_THIN_REGION'], '0.97,1,0.05,0.5')
            for value in ('0.8', '0.995', 'nan', '0.97,2', '0.97,1,0.5', '0.97,1,0.5,0.5', 'x', '0.97,1,0.03,0.25,1'):
                code, _, error = self.launch(directory, *TAA, '--taa-thin-region', value)
                self.assertEqual(code, 2, value)
                self.assertIn('--taa-thin-region', error)
            code, _, error = self.launch(directory, *TAA, '--taa-thin-region', '0.97,1,0.03,0.25', '--taa-far-stabiliser', '0.985,0,80,130,0.5,2')
            self.assertEqual(code, 2)
            self.assertIn('--taa-thin-region', error)
            code, _, error = self.launch(directory, '--motion-output', '--taa-thin-region', '0.97')
            self.assertEqual(code, 2)
            self.assertIn('--taa-thin-region requires --taa', error)

    def test_thin_region_gate_is_not_a_launcher_variable(self):
        # The camera gate (docs/architecture/taa-lattice-crawl.md section 32.1, accepted in Run 59) is the only gate since
        # --taa-thin-region-gate and X3M_TAA_THIN_REGION_GATE were removed on 2026-09-25: the DLL gates whenever the thin
        # region is on. The variable is never sent and an inherited value is dropped; the option is unknown.
        with tempfile.TemporaryDirectory() as directory:
            for args in ((), ('--taa-thin-region', '0.97'), ('--taa-thin-region', '0')):
                for inherited in (None, {'X3M_TAA_THIN_REGION_GATE': 'screen'}):
                    self.assertNotIn('X3M_TAA_THIN_REGION_GATE', self.env(directory, *TAA, *args, inherited=inherited))
            code, _, error = self.launch(directory, *TAA, '--taa-thin-region', '0.97', '--taa-thin-region-gate', 'screen')
            self.assertEqual(code, 2)
            self.assertIn('unrecognized arguments', error)

    def test_thin_region_emissive_vote_defaults_to_one_on_the_hdr_route(self):
        # User-accepted run236/run237 default, 2026-09-22 (docs/architecture/taa-lattice-crawl.md section
        # 32.7): E = 1 whenever --taa runs with the thin region (W > 0) and --hdr. Without --hdr the scene
        # the resolve reads is display-referred and the vote cannot fire, so the launcher leaves it absent
        # there rather than exporting an inert value; an explicit 0 is the opt-out.
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, '--hdr', '--taa-thin-region', '0.97')
            self.assertEqual(env['X3M_TAA_THIN_REGION_EMISSIVE'], '1')
            # A stale shell value can neither change the resolved default nor survive the opt-out.
            self.assertEqual(self.env(directory, *TAA, '--hdr', '--taa-thin-region', '0.97',
                                      inherited={'X3M_TAA_THIN_REGION_EMISSIVE': '3.7'})['X3M_TAA_THIN_REGION_EMISSIVE'], '1')
            self.assertEqual(self.env(directory, *TAA, '--hdr', '--taa-thin-region', '0.97', '--taa-thin-region-emissive', '0',
                                      inherited={'X3M_TAA_THIN_REGION_EMISSIVE': '3.7'})['X3M_TAA_THIN_REGION_EMISSIVE'], '0')
            # An explicit value is kept as given.
            self.assertEqual(self.env(directory, *TAA, '--hdr', '--taa-thin-region', '0.97',
                                      '--taa-thin-region-emissive', '2.5')['X3M_TAA_THIN_REGION_EMISSIVE'], '2.5')
            # Missing prerequisite: no --hdr, or no thin region (absent or W 0). Never an error, just absent.
            for args in (('--taa-thin-region', '0.97'), ('--hdr',), ('--hdr', '--taa-thin-region', '0')):
                self.assertNotIn('X3M_TAA_THIN_REGION_EMISSIVE', self.env(directory, *TAA, *args), args)

    def test_thin_region_emissive_vote_is_absent_unless_given(self):
        # --taa-thin-region-emissive E (docs/architecture/thin-glow-lines.md 8.3 R3): the emissive vote in
        # the thin-region mask. Off unless given on the 8-bit route (E = 0 leaves the mask bit for bit; with
        # --hdr it resolves to 1, see above), and it has nowhere to land without the region, so it requires
        # --taa-thin-region with W > 0.
        with tempfile.TemporaryDirectory() as directory:
            for inherited in (None, {'X3M_TAA_THIN_REGION_EMISSIVE': '1'}):
                self.assertNotIn('X3M_TAA_THIN_REGION_EMISSIVE', self.env(directory, *TAA, inherited=inherited))
                self.assertNotIn('X3M_TAA_THIN_REGION_EMISSIVE',
                                 self.env(directory, *TAA, '--taa-thin-region', '0.97', inherited=inherited))
            for given, forwarded in (('1', '1'), ('1.0', '1'), ('0', '0'), ('0.5', '0.5'), ('3.7', '3.7')):
                env = self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-thin-region-emissive', given)
                self.assertEqual(env['X3M_TAA_THIN_REGION_EMISSIVE'], forwarded, given)
            for args in (('--taa-thin-region-emissive', '1'), ('--taa-thin-region', '0', '--taa-thin-region-emissive', '1'),
                         ('--taa-thin-region', '0.97', '--taa-thin-region-emissive', '-1'),
                         ('--taa-thin-region', '0.97', '--taa-thin-region-emissive', '65001'),
                         ('--taa-thin-region', '0.97', '--taa-thin-region-emissive', 'nan'),
                         ('--taa-thin-region', '0.97', '--taa-thin-region-emissive', 'x'),
                         ('--taa-thin-region', '0.97', '--taa-thin-region-emissive', '1,2')):
                code, _, error = self.launch(directory, *TAA, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('--taa-thin-region-emissive', error)
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('float taa_thin_emissive = 0.f;', source)
        self.assertIn('taa_requested?GetEnvironmentVariableW(L"X3M_TAA_THIN_REGION_EMISSIVE"', source)
        # The whole field must parse and stay within range; nothing else may turn it on.
        self.assertIn('wcstof(emissive_setting', source)
        self.assertIn("if(end!=emissive_setting&&*end==L'\\0'&&v>=0.f&&v<=65000.f)taa_thin_emissive=v;", source)
        # Route: off at initialisation without the thin region, then forwarded per frame, and in the log line.
        route = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('taa_thin_emissive_ > 0.f && taa_thin_weight_ <= 0.f', route)
        self.assertIn('in.thin_region_emissive = taa_thin_emissive_;', route)
        self.assertIn('thin_gate=%s thin_emissive=%.3f', route)
        # The pass refuses a non-finite or negative E while the region is on, and the mask uploads c10 on every draw.
        passcpp = (ROOT / 'src/renderer/temporal_pass.cpp').read_text()
        self.assertIn('thin_region&&(!std::isfinite(in.thin_region_emissive)||in.thin_region_emissive<0)', passcpp)
        self.assertIn('SetPixelShaderConstantF)(d,10,emissive_constants,1)', passcpp)

    def test_sentinel_stabiliser_is_retired(self):
        # --taa-sentinel-stabiliser was removed with the TAA mask fold (2026-09-25, docs/architecture/taa-mask-fold.md
        # section 7); its refusal stub went the same day, so any value is an unknown argument, and X3M_TAA_SENTINEL_STABILISER
        # is never sent or inherited, with or without --taa. The DLL neither reads the variable nor has a stabiliser field.
        with tempfile.TemporaryDirectory() as directory:
            for args in ((*TAA, '--taa-thin-region', '0.97', '--taa-sentinel-stabiliser', '0.7'), (*TAA, '--taa-sentinel-stabiliser', '0'),
                         (*TAA, '--taa-sentinel-stabiliser', 'off'), (*TAA, '--taa-sentinel-stabiliser'), ('--motion-output', '--taa-sentinel-stabiliser', '0.7')):
                code, _, error = self.launch(directory, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('unrecognized arguments', error)
            for args in ((*TAA,), (*TAA, '--taa-thin-region', '0.97'), ('--motion-output',)):
                env = self.env(directory, *args, inherited={'X3M_TAA_SENTINEL_STABILISER': '0.7', 'X3M_TAA_SENTINEL_STABILISER_DEFAULT': '1'})
                self.assertNotIn('X3M_TAA_SENTINEL_STABILISER', env, args)
                self.assertNotIn('X3M_TAA_SENTINEL_STABILISER_DEFAULT', env, args)
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertNotIn('float taa_sentinel[2]', source)
        self.assertNotIn('X3M_TAA_SENTINEL_STABILISER', source)
        self.assertNotIn('sentinel_stabiliser=%.3f', source)
        self.assertNotIn('sentinel_stabiliser=', (ROOT / 'src/proxy/motion_output.cpp').read_text())
        self.assertNotIn('sentinel_strength', (ROOT / 'src/renderer/temporal_pass.h').read_text())
        self.assertNotIn('X3M_TAA_SENTINEL_STABILISER', (ROOT / 'verification/probe/run_motion_output.py').read_text())

    def test_taa_debug_accepts_32_capture_frames(self):
        # Run 139: the resolved-frame spectrum needs more than one jitter period.
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, '--taa-debug', '--capture-frames', '32')
            self.assertEqual((env['X3M_TAA_DEBUG'], env['X3M_CAPTURE_FRAMES']), ('1', '32'))
            self.assertEqual(self.launch(directory, *TAA, '--capture-frames', '65')[0], 2)
        self.assertIn('if(capture_count>64) capture_count=64;', (ROOT / 'src/proxy/capture.cpp').read_text())


class TaaImageDefaultsDll(unittest.TestCase):
    """The DLL's own fallback (a direct WINEDLLOVERRIDES start without the launcher)."""

    def test_capture_defaults_are_gated_on_taa(self):
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('taa_mip_bias=taa_requested?-0.5f:0.f;', source)
        self.assertIn('taa_sharpen=taa_requested?0.75f:0.f;', source)
        # The env value is still parsed whole, so an explicit 0 disables either.
        for name in ('X3M_TAA_MIP_BIAS', 'X3M_TAA_SHARPEN', 'X3M_TAA_HISTORY_WEIGHT'):
            line = next(l for l in source.splitlines() if f'GetEnvironmentVariableW(L"{name}"' in l)
            self.assertIn("*end==L'\\0'", line)

    def test_no_filter_device_refuses_taa_at_attach_and_never_jitters(self):
        # Without FP16 / R32F history filtering (no fallback program set since 2026-09-25) TAA is refused at attach, before the
        # first latch, with one row naming the missing filter; the jitter (and the mip bias that follows it) runs only while the
        # resolve is available. The behaviour is the motion-output case seam-taa-no-filter-refused.
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('HRESULT filtering = renderer::TemporalPass::query_history_filtering(device_, native_, &filter);', motion)
        self.assertIn('taa_reason = "no_filter"; taa_enabled_ = false; taa_failed_ = true; jitter_active_ = false;', motion)
        self.assertIn('initialize=%08lx references=0 reason=%s effect=taa_off jitter=0 mip_bias=0', motion)
        self.assertIn('jitter_active_ = jitter_requested_ && (!taa_requested_ || (taa_enabled_ && !taa_failed_));', motion)
        self.assertIn('if (mip_bias_bits_ && jitter_active_)', motion)
        self.assertLess(motion.index('query_history_filtering(device_'), motion.index('jitter_active_ = jitter_requested_ &&'))

    def test_thin_region_gate_is_the_camera_gate_whenever_the_region_is_on(self):
        # The DLL reads no gate variable since 2026-09-25: camera whenever the thin region is on, off when the region is off
        # or TAA is not requested.
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('taa_thin_camera_gate=taa_requested&&taa_thin_region[0]>0.f;', source)
        self.assertNotIn('GetEnvironmentVariableW(L"X3M_TAA_THIN_REGION_GATE"', source)
        # The gate is resolved after the thin region is parsed, so it sees the final settings.
        self.assertLess(source.index('GetEnvironmentVariableW(L"X3M_TAA_THIN_REGION"'), source.index('taa_thin_camera_gate=taa_requested&&'))
        # A configure-time refusal of the camera-gate programs turns the thin region off (A' only since 2026-09-24: no fallback
        # program set), and so does a box-target allocation failure (in the pass).
        self.assertIn('taa_thin_camera_gate_ = false;', (ROOT / 'src/proxy/motion_output.cpp').read_text())
        self.assertIn('camera_requested&&(!camera_gate_available()', (ROOT / 'src/renderer/temporal_pass.cpp').read_text())
        self.assertIn('taa_thin_weight_ = 0.f; taa_thin_camera_gate_ = false;', (ROOT / 'src/proxy/motion_output.cpp').read_text())



class TaaAgeProgramDefaultsLaunch(unittest.TestCase):
    """Run 81 defaults on a modded launch: --taa-far-stabiliser 0.985 and --taa-thin-region 0.97 with --taa; off/0 turn
    them off; --vanilla and a launch without --taa send neither and print nothing about them. No game, no Wine."""
    NAMES = ('X3M_TAA_FAR_STABILISER', 'X3M_TAA_THIN_REGION')

    def launch(self, directory, *args, inherited=None, vanilla=False):
        module = load_manage()
        game = Path(directory) / 'game'
        game.mkdir(exist_ok=True)
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'fixture')
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': __import__('hashlib').sha256(b'fixture').hexdigest()}))
        wine = Path(directory) / 'wine'
        wine.touch()
        argv = ['manage.py', 'launch', '--dry-run', *(['--vanilla'] if vanilla else []), '--game-dir', str(game), *args]
        output, error = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), mock.patch.object(module, 'VOICE_DECODER_REPO', None), \
                mock.patch.dict(module.os.environ, inherited or {}), \
                mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
                contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
            try:
                module.main()
            except SystemExit as exit_error:
                return exit_error.code, output.getvalue(), error.getvalue()
        return 0, output.getvalue(), error.getvalue()

    def run_env(self, directory, *args, **kwargs):
        code, output, error = self.launch(directory, *args, **kwargs)
        self.assertEqual(code, 0, error)
        return json.loads(output)['env'], error

    def test_default_on_with_taa(self):
        with tempfile.TemporaryDirectory() as directory:
            env, _ = self.run_env(directory, *TAA, '--hdr', inherited={'X3M_TAA_FAR_STABILISER': '0', 'X3M_TAA_THIN_REGION': '0'})
            self.assertEqual((env['X3M_TAA_FAR_STABILISER'], env['X3M_TAA_THIN_REGION']), ('0.985,0,60,68,0.03,0.25', '0.97,1'))
            # The derived defaults see the defaulted thin region: emissive vote 1 (HDR; the camera gate follows the region in the DLL);
            # the sentinel stabiliser is retired.
            self.assertEqual(env['X3M_TAA_THIN_REGION_EMISSIVE'], '1')
            self.assertNotIn('X3M_TAA_THIN_REGION_GATE', env)
            self.assertNotIn('X3M_TAA_SENTINEL_STABILISER', env)
            # An explicit value still wins; the other keeps its default.
            env, _ = self.run_env(directory, *TAA, '--taa-thin-region', '0.95')
            self.assertEqual((env['X3M_TAA_FAR_STABILISER'], env['X3M_TAA_THIN_REGION']), ('0.985,0,60,68,0.03,0.25', '0.95,1'))

    def test_explicit_off(self):
        with tempfile.TemporaryDirectory() as directory:
            for off in ('off', '0', 'OFF'):
                env, _ = self.run_env(directory, *TAA, '--taa-far-stabiliser', off, '--taa-thin-region', off)
                self.assertEqual((env['X3M_TAA_FAR_STABILISER'], env['X3M_TAA_THIN_REGION']), ('0,0,60,68,0.03,0.25', '0,1'), off)
                # Neither age program in effect: the options keyed on them resolve off, as before the defaults.
                self.assertNotIn('X3M_TAA_THIN_REGION_GATE', env)
                self.assertEqual(env['X3M_TAA_MOTION_WEIGHT'], '0')

    def test_vanilla_and_no_taa_send_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            env, error = self.run_env(directory, *TAA, vanilla=True, inherited={name: '0.97' for name in self.NAMES})
            self.assertFalse(set(self.NAMES) & set(env))
            env, error = self.run_env(directory, '--motion-output', '--hdr', '--no-taa', inherited={name: '0.97' for name in self.NAMES})  # --taa: launcher default since 2026-09-25
            self.assertFalse(set(self.NAMES) & set(env))
            self.assertNotIn('stabiliser', error)
            self.assertNotIn('thin-region', error)

    def test_history_weight_above_a_default_keeps_it_off(self):
        with tempfile.TemporaryDirectory() as directory:
            env, _ = self.run_env(directory, *TAA, '--taa-history-weight', '0.98')
            self.assertNotIn('X3M_TAA_THIN_REGION', env)
            self.assertEqual(env['X3M_TAA_FAR_STABILISER'], '0.985,0,60,68,0.03,0.25')


if __name__ == '__main__':
    unittest.main()
