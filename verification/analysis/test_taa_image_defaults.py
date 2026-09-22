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
        with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
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
        # --taa-current-filter / --taa-history-weight (run 139 A/B): forwarded
        # only when given, a stale shell value dropped, ranges enforced.
        names = ('X3M_TAA_CURRENT_FILTER', 'X3M_TAA_HISTORY_WEIGHT')
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, inherited={'X3M_TAA_CURRENT_FILTER': '2', 'X3M_TAA_HISTORY_WEIGHT': '0.5'})
            for name in names:
                self.assertNotIn(name, env)
            env = self.env(directory, *TAA, '--taa-current-filter', '1.0', '--taa-history-weight', '0.95')
            self.assertEqual([env[name] for name in names], ['1.0', '0.95'])
            for option, value in (('--taa-current-filter', '4.5'), ('--taa-current-filter', 'nan'), ('--taa-current-filter', '-1'),
                                  ('--taa-history-weight', '0.99'), ('--taa-history-weight', '0.4'), ('--taa-history-weight', 'nan')):
                code, _, error = self.launch(directory, *TAA, option, value)
                self.assertEqual(code, 2, (option, value))
                self.assertIn(f'{option} must be within', error)
            for option in ('--taa-current-filter', '--taa-history-weight'):
                code, _, error = self.launch(directory, '--motion-output', option, '0.9')
                self.assertEqual(code, 2, option)
                self.assertIn(f'{option} requires --taa', error)

    def test_line_filter_is_absent_unless_given(self):
        # --taa-line-filter A[,W] (docs/architecture/taa-lattice-crawl.md section 9).
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_TAA_LINE_FILTER', self.env(directory, *TAA, inherited={'X3M_TAA_LINE_FILTER': '1'}))
            self.assertEqual(self.env(directory, *TAA, '--taa-line-filter', '1.0')['X3M_TAA_LINE_FILTER'], '1')
            self.assertEqual(self.env(directory, *TAA, '--taa-line-filter', '2,2')['X3M_TAA_LINE_FILTER'], '2,2')
            for value in ('4.5', 'nan', '-1', '1,3', '1,', 'x'):
                code, _, error = self.launch(directory, *TAA, '--taa-line-filter', value)
                self.assertEqual(code, 2, value)
                self.assertIn('--taa-line-filter takes A[,W]', error)
            code, _, error = self.launch(directory, *TAA, '--taa-line-filter', '1', '--taa-current-filter', '1')
            self.assertEqual(code, 2)
            self.assertIn('exclude each other', error)
            code, _, error = self.launch(directory, '--motion-output', '--taa-line-filter', '1')
            self.assertEqual(code, 2)
            self.assertIn('--taa-line-filter requires --taa', error)

    def test_far_stabiliser_is_absent_unless_given(self):
        # --taa-far-stabiliser W[,A[,F0,F1]] (docs/architecture/taa-distant-line-fade.md section 9): components separate.
        with tempfile.TemporaryDirectory() as directory:
            self.assertNotIn('X3M_TAA_FAR_STABILISER', self.env(directory, *TAA, inherited={'X3M_TAA_FAR_STABILISER': '0.985'}))
            for given, forwarded in (('0.985', '0.985,0,80,130,0.03,0.25'), ('0.985,1', '0.985,1,80,130,0.03,0.25'), ('0,1', '0,1,80,130,0.03,0.25'), ('0.97,0.5,100,160', '0.97,0.5,100,160,0.03,0.25'), ('0.985,0,80,130,0.5,2', '0.985,0,80,130,0.5,2')):
                self.assertEqual(self.env(directory, *TAA, '--taa-far-stabiliser', given)['X3M_TAA_FAR_STABILISER'], forwarded)
            self.assertEqual(self.env(directory, *TAA, '--taa-far-stabiliser', '0.985,1', '--taa-line-filter', '1')['X3M_TAA_FAR_STABILISER'], '0.985,1,80,130,0.03,0.25')
            for value in ('0.8', '0.995', 'nan', '0.985,5', '0.985,1,80', '0.985,1,130,80', '0.985,1,0,80', 'x', '0.985,1,80,130,1', '0.985,0,80,130,0.5,0.5', '0.985,0,80,130,-1,2', '0.985,0,80,130,0.1,65'):
                code, _, error = self.launch(directory, *TAA, '--taa-far-stabiliser', value)
                self.assertEqual(code, 2, value)
                self.assertIn('--taa-far-stabiliser', error)
            for extra in (('--taa-thin-clip', '0.75', '--taa-adaptive-weight', '0.97'), ('--taa-current-filter', '1'), ('--taa-line-filter', '2')):
                code, _, error = self.launch(directory, *TAA, '--taa-far-stabiliser', '0.985,1', *extra)
                self.assertEqual(code, 2, extra)
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
            self.assertEqual((env['X3M_TAA_THIN_REGION'], env['X3M_TAA_FAR_STABILISER']), ('0.97,1,0.05,0.5', '0.985,0,80,130,0.05,0.5'))
            env = self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-far-stabiliser', '0.985,0,80,130,0.05,0.5')
            self.assertEqual((env['X3M_TAA_THIN_REGION'], env['X3M_TAA_FAR_STABILISER']), ('0.97,1', '0.985,0,80,130,0.05,0.5'))
            env = self.env(directory, *TAA, '--taa-thin-region', '0.97,1,0.05,0.5', '--taa-far-stabiliser', '0.985,0,80,130,0.05,0.5')
            self.assertEqual(env['X3M_TAA_THIN_REGION'], '0.97,1,0.05,0.5')
            for value in ('0.8', '0.995', 'nan', '0.97,2', '0.97,1,0.5', '0.97,1,0.5,0.5', 'x', '0.97,1,0.03,0.25,1'):
                code, _, error = self.launch(directory, *TAA, '--taa-thin-region', value)
                self.assertEqual(code, 2, value)
                self.assertIn('--taa-thin-region', error)
            for extra in (('--taa-thin-clip', '0.75'), ('--taa-thin-clip', '0.75', '--taa-adaptive-weight', '0.97'), ('--taa-current-filter', '1'), ('--taa-far-stabiliser', '0.985,0,80,130,0.5,2')):
                code, _, error = self.launch(directory, *TAA, '--taa-thin-region', '0.97,1,0.03,0.25', *extra)
                self.assertEqual(code, 2, extra)
                self.assertIn('--taa-thin-region', error)
            code, _, error = self.launch(directory, '--motion-output', '--taa-thin-region', '0.97')
            self.assertEqual(code, 2)
            self.assertIn('--taa-thin-region requires --taa', error)

    def test_thin_region_gate_defaults_to_camera_when_the_region_is_on(self):
        # --taa-thin-region-gate screen|camera (docs/architecture/taa-lattice-crawl.md section 32.1). Run 59
        # (run207 screen / run208 camera) accepted the camera gate, so it is the default whenever the thin
        # region is active; the line filter (whose mask channel carries the only gate it can have) resolves
        # to screen silently, while an explicit camera plus the line filter is still refused.
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *TAA, '--taa-thin-region', '0.97')
            self.assertEqual((env['X3M_TAA_THIN_REGION'], env['X3M_TAA_THIN_REGION_GATE']), ('0.97,1', 'camera'))
            # A stale shell value cannot select a different gate than the resolved default.
            self.assertEqual(self.env(directory, *TAA, '--taa-thin-region', '0.97', inherited={'X3M_TAA_THIN_REGION_GATE': 'screen'})['X3M_TAA_THIN_REGION_GATE'], 'camera')
            self.assertEqual(self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-thin-region-gate', 'screen')['X3M_TAA_THIN_REGION_GATE'], 'screen')
            env = self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-thin-region-gate', 'camera')
            self.assertEqual(env['X3M_TAA_THIN_REGION_GATE'], 'camera')
            env = self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-far-stabiliser', '0.985', '--taa-thin-region-gate', 'camera', '--taa-line-filter', '0')
            self.assertEqual(env['X3M_TAA_THIN_REGION_GATE'], 'camera')
            # An active line filter silently keeps the screen gate; a zero A does not count as active.
            self.assertEqual(self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-line-filter', '1')['X3M_TAA_THIN_REGION_GATE'], 'screen')
            self.assertEqual(self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-line-filter', '2,2')['X3M_TAA_THIN_REGION_GATE'], 'screen')
            self.assertEqual(self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-line-filter', '0')['X3M_TAA_THIN_REGION_GATE'], 'camera')
            # Without the thin region (absent or W 0) no gate variable is emitted, inherited or not.
            for args in ((), ('--taa-thin-region', '0')):
                for inherited in (None, {'X3M_TAA_THIN_REGION_GATE': 'camera'}):
                    self.assertNotIn('X3M_TAA_THIN_REGION_GATE', self.env(directory, *TAA, *args, inherited=inherited))
            for args in (('--taa-thin-region-gate', 'camera'), ('--taa-thin-region', '0', '--taa-thin-region-gate', 'camera'),
                         ('--taa-thin-region', '0.97', '--taa-thin-region-gate', 'camera', '--taa-line-filter', '1'),
                         ('--taa-thin-region', '0.97', '--taa-thin-region-gate', 'wide')):
                code, _, error = self.launch(directory, *TAA, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('--taa-thin-region-gate', error)

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
            # It rides the screen gate as well as the camera one: the vote is in the mask's fragmentation channel.
            env = self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-thin-region-gate', 'screen', '--taa-thin-region-emissive', '1')
            self.assertEqual((env['X3M_TAA_THIN_REGION_GATE'], env['X3M_TAA_THIN_REGION_EMISSIVE']), ('screen', '1'))
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

    def test_sentinel_stabiliser_defaults_to_0_7_with_the_camera_gate(self):
        # --taa-sentinel-stabiliser S[,E] (docs/architecture/temporal-integration.md, "Distant unrouted
        # stations under a pan"). Run 61 (run216: lasers over sky clean) and Run 62 (run221: the
        # distant-station pan flicker fixed) accepted S = 0.7, so it is the default whenever the TAA
        # route runs with the thin-region camera gate; without that gate it resolves to off, not an
        # error, and "off"/"0" is the opt-out.
        with tempfile.TemporaryDirectory() as directory:
            for inherited in (None, {'X3M_TAA_SENTINEL_STABILISER': '0.2'}):
                env = self.env(directory, *TAA, '--taa-thin-region', '0.97', inherited=inherited)
                self.assertEqual((env['X3M_TAA_THIN_REGION_GATE'], env['X3M_TAA_SENTINEL_STABILISER']), ('camera', '0.7'))
            # No camera gate: the option resolves to off (the variable is dropped, inherited or not), never an error.
            for args in ((), ('--taa-thin-region', '0'), ('--taa-thin-region', '0.97', '--taa-thin-region-gate', 'screen'),
                         ('--taa-thin-region', '0.97', '--taa-line-filter', '1')):
                for inherited in (None, {'X3M_TAA_SENTINEL_STABILISER': '0.7'}):
                    self.assertNotIn('X3M_TAA_SENTINEL_STABILISER', self.env(directory, *TAA, *args, inherited=inherited), args)
            # Without --taa nothing is forwarded either (and no error, since nothing was asked for).
            self.assertNotIn('X3M_TAA_SENTINEL_STABILISER', self.env(directory, '--motion-output', inherited={'X3M_TAA_SENTINEL_STABILISER': '0.7'}))
            # The opt-out, by name and by number.
            for value in ('off', 'OFF', '0'):
                self.assertEqual(self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-sentinel-stabiliser', value)['X3M_TAA_SENTINEL_STABILISER'], '0', value)
            self.assertEqual(self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-sentinel-stabiliser', '0.7')['X3M_TAA_SENTINEL_STABILISER'], '0.7')
            self.assertEqual(self.env(directory, *TAA, '--taa-thin-region', '0.97', '--taa-sentinel-stabiliser', '0.7,0')['X3M_TAA_SENTINEL_STABILISER'], '0.7,0')
            self.assertEqual(self.env(directory, *TAA, '--taa-sentinel-stabiliser', '0')['X3M_TAA_SENTINEL_STABILISER'], '0')
            self.assertEqual(self.env(directory, *TAA, '--taa-sentinel-stabiliser', 'off')['X3M_TAA_SENTINEL_STABILISER'], '0')
            for value in ('0', '0.7', 'off'):
                code, _, error = self.launch(directory, '--motion-output', '--taa-sentinel-stabiliser', value)
                self.assertEqual(code, 2, value)
                self.assertIn('--taa-sentinel-stabiliser requires --taa', error)
            for args in (('--taa-sentinel-stabiliser', '0.7'), ('--taa-thin-region', '0.97', '--taa-thin-region-gate', 'screen', '--taa-sentinel-stabiliser', '0.7'),
                         ('--taa-thin-region', '0.97', '--taa-line-filter', '1', '--taa-sentinel-stabiliser', '0.7'),
                         ('--taa-thin-region', '0.97', '--taa-sentinel-stabiliser', '1.5'), ('--taa-thin-region', '0.97', '--taa-sentinel-stabiliser', '0.7,-1'),
                         ('--taa-thin-region', '0.97', '--taa-sentinel-stabiliser', '0.7,1,2'), ('--taa-thin-region', '0.97', '--taa-sentinel-stabiliser', 'nan')):
                code, _, error = self.launch(directory, *TAA, *args)
                self.assertEqual(code, 2, args)
                self.assertIn('--taa-sentinel-stabiliser', error)
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('float taa_sentinel[2] = {0.f, 1.f};', source)
        self.assertIn('taa_requested?GetEnvironmentVariableW(L"X3M_TAA_SENTINEL_STABILISER"', source)
        self.assertIn('sentinel_stabiliser=%.3f sentinel_emitter=%.3f', (ROOT / 'src/proxy/motion_output.cpp').read_text())
        # The native fallback mirrors the launcher: 0.7 (E at its initialiser 1) when the camera gate is
        # in effect, resolved after that gate and after the thin region and the line filter it depends on.
        self.assertIn('else if(taa_requested&&taa_thin_camera_gate)taa_sentinel[0]=.7f;', source)
        stabiliser = source.index('GetEnvironmentVariableW(L"X3M_TAA_SENTINEL_STABILISER"')
        self.assertLess(source.index('GetEnvironmentVariableW(L"X3M_TAA_THIN_REGION_GATE"'), stabiliser)
        # The resolved unmatched-static mode and the stabiliser pair are in the startup log line.
        self.assertIn('unmatched_static=%u sentinel_stabiliser=%.3f sentinel_emitter=%.3f', source)
        self.assertIn('taa_unmatched_static,double(taa_sentinel[0]),double(taa_sentinel[1])', source)
        # The motion-output runner pins the stabiliser off: its oracles model the pre-Run62 behaviour.
        self.assertIn("X3M_TAA_SENTINEL_STABILISER='0'", (ROOT / 'verification/probe/run_motion_output.py').read_text())

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
        for name in ('X3M_TAA_MIP_BIAS', 'X3M_TAA_SHARPEN', 'X3M_TAA_CURRENT_FILTER', 'X3M_TAA_HISTORY_WEIGHT'):
            line = next(l for l in source.splitlines() if f'GetEnvironmentVariableW(L"{name}"' in l)
            self.assertIn("*end==L'\\0'", line)

    def test_thin_region_gate_native_fallback_defaults_to_camera(self):
        # Absent X3M_TAA_THIN_REGION_GATE mirrors the launcher: camera when the thin region is on,
        # screen with the line filter, and untouched when the region is off or TAA is not requested.
        source = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('else if(taa_requested&&taa_thin_region[0]>0.f&&taa_line_filter<=0.f)taa_thin_camera_gate=true;', source)
        # The default is resolved after both values are parsed, so it sees the final settings.
        gate = source.index('GetEnvironmentVariableW(L"X3M_TAA_THIN_REGION_GATE"')
        self.assertLess(source.index('GetEnvironmentVariableW(L"X3M_TAA_THIN_REGION"'), gate)
        self.assertLess(source.index('GetEnvironmentVariableW(L"X3M_TAA_LINE_FILTER"'), gate)
        # An explicit value still decides, and the route's own fallbacks to the screen gate are unchanged:
        # the configure-time refusal and the box-allocation failure both leave the screen behaviour.
        self.assertIn('if(wcscmp(gate_setting,L"camera")==0)taa_thin_camera_gate=true;', source)
        self.assertIn('taa_thin_camera_gate_ = false;', (ROOT / 'src/proxy/motion_output.cpp').read_text())
        self.assertIn('camera_requested&&(!camera_gate_available()||lined)', (ROOT / 'src/renderer/temporal_pass.cpp').read_text())


if __name__ == '__main__':
    unittest.main()
