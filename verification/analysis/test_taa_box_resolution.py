"""Host tests of --taa-box-resolution (X3M_TAA_BOX_RESOLUTION, docs/architecture/taa-high-resolution.md S4): half by default
on every modded --taa launch since Run 82 with X3M_TAA_BOX_RESOLUTION_DEFAULT=1 (explicit full/half: marker 0; the DLL default
when unset stays full), full or half only, TAA mode only, nothing without --taa or under --vanilla (an explicit value refused),
an inherited shell value or marker never survives; the DLL reads both and hands half to TemporalPass::configure_box_resolution,
logging only when half is asked (default= on the creation row);
the two half-resolution programs are generated from their own sources.
No game, no Wine."""
import json
import tempfile
import unittest

import test_taa_sky_history as sky
import test_taa_thin_vote as vote

ROOT, TAA = sky.ROOT, sky.TAA


class BoxResolutionLaunch(unittest.TestCase):
    # The thin vote's launcher harness (dry run, never launches; no --vanilla added), without inheriting its tests.
    launch = vote.ThinVoteLaunch.launch
    env = vote.ThinVoteLaunch.env

    MARKER = 'X3M_TAA_BOX_RESOLUTION_DEFAULT'

    def test_default_half_with_taa_overrides_an_inherited_value(self):
        # Run 82: no option with --taa sends half, marked as the launcher default; a stale shell pair cannot change it.
        with tempfile.TemporaryDirectory() as directory:
            for inherited in (None, {'X3M_TAA_BOX_RESOLUTION': 'full', self.MARKER: '0'}):
                env = self.env(directory, *TAA, inherited=inherited)
                self.assertEqual((env['X3M_TAA_BOX_RESOLUTION'], env[self.MARKER]), ('half', '1'))

    def test_without_taa_nothing_is_sent_and_inherited_values_are_dropped(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, '--motion-output', inherited={'X3M_TAA_BOX_RESOLUTION': 'half', self.MARKER: '1'})
            self.assertNotIn('X3M_TAA_BOX_RESOLUTION', env)
            self.assertNotIn(self.MARKER, env)

    def test_vanilla_sends_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            env = self.env(directory, '--vanilla', inherited={'X3M_TAA_BOX_RESOLUTION': 'half', self.MARKER: '1'})
            self.assertNotIn('X3M_TAA_BOX_RESOLUTION', env)
            self.assertNotIn(self.MARKER, env)

    def test_explicit_full_and_half_are_forwarded_with_marker_0(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('full', 'half'):
                env = self.env(directory, *TAA, '--taa-box-resolution', value, inherited={self.MARKER: '1'})
                self.assertEqual((env['X3M_TAA_BOX_RESOLUTION'], env[self.MARKER]), (value, '0'))

    def test_other_values_missing_taa_and_vanilla_are_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('quarter', '2', 'HALF', ''):
                code, _, error = self.launch(directory, *TAA, '--taa-box-resolution', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-box-resolution', error)
            code, _, error = self.launch(directory, '--motion-output', '--taa-box-resolution', 'half')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-box-resolution requires --taa', error)
            code, _, error = self.launch(directory, '--vanilla', '--taa-box-resolution', 'half')
            self.assertNotEqual(code, 0)
            self.assertIn('--taa-box-resolution cannot be combined with --vanilla', error)


class BoxResolutionSource(unittest.TestCase):
    def test_dll_reads_the_setting_and_configures_the_pass(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('GetEnvironmentVariableW(L"X3M_TAA_BOX_RESOLUTION"', capture)
        self.assertIn('bool taa_box_half = false, taa_box_resolution_default = false;', capture)
        # The launcher's marker counts only with half and only as exactly "1".
        self.assertIn('taa_box_resolution_default=taa_box_half&&GetEnvironmentVariableW(L"X3M_TAA_BOX_RESOLUTION_DEFAULT",setting,32)==1&&setting[0]==L\'1\';', capture)
        self.assertIn('taa_box_resolution_setting invalid=1 reason=too_long length=%lu', capture)
        self.assertIn('configure_box_resolution(taa_box_half,taa_box_resolution_default)', capture)
        # A value other than full / half logs one row and stays full.
        self.assertIn('log("taa_box_resolution_setting invalid=1");', capture)
        self.assertIn('if(!wcscmp(setting,L"half"))taa_box_half=true;', capture)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('taa_->configure_box_resolution(2)', motion)
        # Logged only when half is requested: the default run's log is the pre-S4 log line for line.
        self.assertIn('if (SUCCEEDED(hr) && taa_box_half_) {', motion)
        self.assertIn('create=%08lx default=%u",', motion)
        self.assertIn('unsigned(taa_box_default_)', motion)
        self.assertIn('if (taa_box_half_ && taa_->box_resolution() == 2', motion)
        # The per-change row is capped: 8 rows, then one suppressed=1 row per attachment.
        self.assertIn('if (++taa_box_reason_rows_ <= 8)', motion)
        self.assertIn('suppressed=1 changes=9', motion)

    def test_half_programs_are_generated_from_their_own_sources(self):
        generator = (ROOT / 'tools/shaders/generate_rigid_motion_pixel.py').read_text()
        for name in ('rows', 'columns'):
            self.assertIn(f"'temporal_thin_box_{name}_half'", generator)
            record = json.loads((ROOT / f'verification/results/temporal-thin-box-{name}-half-program.json').read_text())
            self.assertEqual(record['source'], f'src/temporal/thin_box_{name}_half_ps.hlsl')
            self.assertEqual(record['target'], 'ps_3_0')
            self.assertIsNone(record['includes'])
            header = (ROOT / f'src/renderer/temporal_thin_box_{name}_half_program_inc.h').read_text()
            self.assertIn(f'thin_box_{name}_half_ps.hlsl', header)
        program = (ROOT / 'src/renderer/temporal_resolve_program.h').read_text()
        self.assertIn('temporal_thin_box_rows_half_program()', program)
        self.assertIn('temporal_thin_box_columns_half_program()', program)


class BoxWindowArithmetic(unittest.TestCase):
    """The index arithmetic of thin_box_rows_half_ps.hlsl / thin_box_columns_half_ps.hlsl and the resolve's point read, in
    double precision, against a brute-force union / intersection of the pixels' clamped 7x7 windows (per axis)."""
    SIZES = (32, 768, 1080, 1440, 5120)

    @staticmethod
    def texel(u, size):  # D3D9 POINT sampling with CLAMP: texel floor(u * size), clamped
        return min(max(int(u * size // 1), 0), size - 1)

    def test_resolve_reads_the_block_texel(self):
        for size in self.SIZES:
            for x in range(size):
                self.assertEqual(self.texel((x + .5) / size, size // 2), x >> 1, (size, x))

    def test_row_pairs_cover_exactly_the_union_and_the_common_rows(self):
        clamp = lambda v, size: min(max(v, 0), size - 1)
        for size in self.SIZES:
            half = size // 2
            pair_rows = []
            for g in range(half + 1):  # the rows draw: TEXCOORD0.y = (g + 1/2) / (H/2 + 1); rows 2g-1 and 2g
                v = (g + .5) / (half + 1)
                upper = self.texel(v + (2 * v - 1.5) / size, size)
                self.assertEqual(upper, clamp(2 * g - 1, size), (size, g))
                pair_rows.append((upper, clamp(2 * g, size)))
            for by in range(half):  # the columns draw: TEXCOORD0.y = (by + 1/2) / (H/2); pairs by-1..by+2
                t = (by + .5) / half
                pairs = [self.texel(t * (half / (half + 1)) + k / (half + 1), half + 1) for k in (-1, 0, 1, 2)]
                self.assertEqual(pairs, [min(max(by + k, 0), half) for k in (-1, 0, 1, 2)], (size, by))
                union = {clamp(2 * by + i + d, size) for i in (0, 1) for d in range(-3, 4)}
                common = {clamp(2 * by + d, size) for d in range(-3, 4)} & {clamp(2 * by + 1 + d, size) for d in range(-3, 4)}
                self.assertEqual({r for g in pairs for r in pair_rows[g]}, union, (size, by))
                # The emitter code's common rows: the lower row of pair by-1, both of by and by+1, the upper row of by+2.
                read = {pair_rows[pairs[0]][1]} | set(pair_rows[pairs[1]]) | set(pair_rows[pairs[2]]) | {pair_rows[pairs[3]][0]}
                self.assertEqual(read, common, (size, by))
                inner = {clamp(2 * by + d, size) for d in range(-1, 3)}
                self.assertEqual(inner, {clamp(2 * by + i + d, size) for i in (0, 1) for d in (-1, 0, 1)}, (size, by))


if __name__ == '__main__':
    unittest.main()
