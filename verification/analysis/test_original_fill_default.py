"""Host tests of the --original-fill launcher default (X3M_ORIGINAL_FILL, docs/architecture/original-shading-critique.md 1a):
0.01 on every modded --hdr launch since 2026-09-25 (0.02 for a few hours) (user decision, accepted in flight) with X3M_ORIGINAL_FILL_DEFAULT=1; an
explicit value (0 = the opt-out, the byte-identical original programs) is sent with marker 0; no default and no marker under
--linear-materials (its --material-fill applies instead), without --hdr or under --vanilla, where the variable stays an explicit
0.0 against a stale shell value; an inherited value or marker never survives; the parser errors are unchanged. The DLL default
when the variable is unset stays 0, and the DLL logs the marker as default= on its original_fill_mode row.
No game, no Wine."""
import tempfile
import unittest

import test_taa_sky_history as sky
import test_taa_thin_vote as vote

ROOT = sky.ROOT
HDR = ('--motion-output', '--hdr')
LINEAR = ('--hdr-tonemap', '--linear-materials')
MARKER = 'X3M_ORIGINAL_FILL_DEFAULT'
STALE = {'X3M_ORIGINAL_FILL': '0.3', MARKER: '1'}


class OriginalFillDefaultLaunch(unittest.TestCase):
    # The thin vote's launcher harness (modded dry run, never launches; no --vanilla added), without inheriting its tests.
    launch = vote.ThinVoteLaunch.launch
    env = vote.ThinVoteLaunch.env

    def test_modded_hdr_launch_defaults_to_0_02_marked(self):
        with tempfile.TemporaryDirectory() as directory:
            for inherited in (None, {'X3M_ORIGINAL_FILL': '0.0', MARKER: '0'}):
                env = self.env(directory, *HDR, inherited=inherited)
                self.assertEqual((env['X3M_ORIGINAL_FILL'], env[MARKER]), ('0.01', '1'))

    def test_explicit_values_are_sent_with_marker_0(self):
        with tempfile.TemporaryDirectory() as directory:
            for value, sent in (('0.05', '0.05'), ('0', '0.0')):
                env = self.env(directory, *HDR, '--original-fill', value, inherited=STALE)
                self.assertEqual((env['X3M_ORIGINAL_FILL'], env[MARKER]), (sent, '0'), value)

    def test_linear_materials_keep_their_own_fill_and_send_no_default(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, *HDR, *LINEAR, inherited=STALE)
            self.assertEqual((env['X3M_ORIGINAL_FILL'], env['X3M_MATERIAL_FILL']), ('0.0', '0.05'))
            self.assertNotIn(MARKER, env)

    def test_without_hdr_no_default_is_sent(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, '--motion-output', inherited=STALE)
            self.assertEqual(env['X3M_ORIGINAL_FILL'], '0.0')
            self.assertNotIn(MARKER, env)

    def test_vanilla_sends_no_default(self):
        with tempfile.TemporaryDirectory() as directory:
            for args in (('--vanilla',), ('--vanilla', *HDR)):
                env = self.env(directory, *args, inherited=STALE)
                self.assertEqual(env['X3M_ORIGINAL_FILL'], '0.0', args)
                self.assertNotIn(MARKER, env)

    def test_parser_errors_are_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            for args, message in ((('--motion-output', '--original-fill', '0.05'), '--original-fill requires --hdr'),
                                  ((*HDR, *LINEAR, '--original-fill', '0'), '--original-fill excludes --linear-materials'),
                                  ((*HDR, '--original-fill', '0.51'), '--original-fill must be finite'),
                                  ((*HDR, '--original-fill', 'nan'), '--original-fill must be finite')):
                code, _, error = self.launch(directory, *args)
                self.assertEqual(code, 2, args)
                self.assertIn(message, error)


class OriginalFillDefaultSource(unittest.TestCase):
    def test_dll_reads_the_marker_and_logs_default(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        block = capture[capture.index('X3M_ORIGINAL_FILL=<k>'):][:3200]
        # The DLL default when unset stays 0 (fixtures unchanged).
        self.assertIn('{original_fill=0.f;bool fill_valid=true;float value=0.f;', block)
        # The marker counts only with an enabled fill and only as exactly "1".
        self.assertIn('const bool fill_default=original_fill!=0.f&&GetEnvironmentVariableW(L"X3M_ORIGINAL_FILL_DEFAULT",setting,32)==1&&setting[0]==L\'1\';', block)
        self.assertIn('fill_valid=%u default=%u%s",', block)
        self.assertIn('unsigned(fill_valid),unsigned(fill_default),', block)
        self.assertLess(block.index('if(!hdr_requested||excluded)original_fill=0.f;'), block.index('const bool fill_default='))

    def test_motion_runner_drops_an_inherited_marker(self):
        runner = (ROOT / 'verification/probe/run_motion_output.py').read_text()
        # the marker sits in the runner's inherited-marker pop list (the list grows with every launcher default)
        block = runner[runner.index("for marker in ('X3M_TAA_THIN_VOTE_DEFAULT'"):]
        block = block[:block.index("env.pop(marker, None)")]
        self.assertIn("'X3M_ORIGINAL_FILL_DEFAULT'", block)


if __name__ == '__main__':
    unittest.main()
