"""Host tests of --taa-thin-region-source (X3M_TAA_THIN_REGION_SOURCE, docs/architecture/taa-thin-geometry-alternatives.md
section 3.2, taa-mask-fold.md): vote by default on a modded --taa launch whose thin vote and thin region are on (marked
X3M_TAA_THIN_REGION_SOURCE_DEFAULT=1), not sent otherwise (the DLL default is both); both / screen / vote only, screen refused
with the camera gate (the default gate), requires --taa and the thin region (its 0.97 default counts), vote also the thin vote
(its default counts), refused under --vanilla, an inherited shell value never survives; the runner's parser of the DLL's
taa_thin_region_source row; the source contract of the DLL, the pass and the shaders (the folded resolve reads c10.y / c10.z;
the screen-gate chain's plain programs never read c10.y). No game, no Wine."""
import json
import tempfile
import unittest

import run_motion_output as runner
import test_taa_sky_history as sky
import test_taa_thin_vote as vote

ROOT, TAA = sky.ROOT, sky.TAA
LANE = vote.LANE


class ThinRegionSourceLaunch(unittest.TestCase):
    # The thin vote's launcher harness (dry run, never launches), without inheriting its tests.
    launch = vote.ThinVoteLaunch.launch
    env = vote.ThinVoteLaunch.env

    def test_default_vote_with_the_thin_vote_and_an_inherited_value_never_survives(self):
        with tempfile.TemporaryDirectory() as directory:
            inherited = {'X3M_TAA_THIN_REGION_SOURCE': 'both', 'X3M_TAA_THIN_REGION_SOURCE_DEFAULT': '0'}
            for extra in ({}, inherited):
                env = self.env(directory, *TAA, *LANE, inherited=extra)
                self.assertEqual((env['X3M_TAA_THIN_REGION_SOURCE'], env['X3M_TAA_THIN_REGION_SOURCE_DEFAULT'], env['X3M_TAA_THIN_VOTE']), ('vote', '1', 'on'))
            # Not sent without the thin vote (off, or no lane), without the thin region, or without --taa.
            for args in ((*TAA, *LANE, '--taa-thin-vote', 'off'), (*TAA,), (*TAA, *LANE, '--taa-thin-region', 'off'), ('--motion-output',)):
                env = self.env(directory, *args, inherited=inherited)
                self.assertNotIn('X3M_TAA_THIN_REGION_SOURCE', env, args)
                self.assertNotIn('X3M_TAA_THIN_REGION_SOURCE_DEFAULT', env, args)
            code, output, error = self.launch(directory, '--vanilla', inherited=inherited)
            self.assertEqual(code, 0, error)
            self.assertNotIn('X3M_TAA_THIN_REGION_SOURCE', json.loads(output)['env'])

    def test_values_are_forwarded_with_the_defaults(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('both', 'vote'):  # the thin region (0.97) and the vote (on) from their Run 81 defaults
                env = self.env(directory, *TAA, *LANE, '--taa-thin-region-source', value)
                self.assertEqual((env['X3M_TAA_THIN_REGION_SOURCE'], env['X3M_TAA_THIN_REGION_SOURCE_DEFAULT']), (value, '0'))
                self.assertEqual(env['X3M_TAA_THIN_VOTE'], 'on')
            # both needs no vote: the explicit off and the lane-less launch keep it; screen only with the screen gate.
            for args in ((*TAA, *LANE, '--taa-thin-vote', 'off'), (*TAA,)):
                self.assertEqual(self.env(directory, *args, '--taa-thin-region-source', 'both')['X3M_TAA_THIN_REGION_SOURCE'], 'both')
                self.assertEqual(self.env(directory, *args, '--taa-thin-region-gate', 'screen', '--taa-thin-region-source', 'screen')['X3M_TAA_THIN_REGION_SOURCE'], 'screen')
            env = self.env(directory, *TAA, *LANE, '--taa-thin-region', '0.95', '--taa-thin-vote', 'on', '--taa-thin-region-source', 'vote')
            self.assertEqual((env['X3M_TAA_THIN_REGION'], env['X3M_TAA_THIN_REGION_SOURCE']), ('0.95,1', 'vote'))

    def test_refusals(self):
        with tempfile.TemporaryDirectory() as directory:
            for value in ('Both', 'union', '1', ''):
                code, _, error = self.launch(directory, *TAA, *LANE, '--taa-thin-region-source', value)
                self.assertNotEqual(code, 0, value)
                self.assertIn('--taa-thin-region-source', error)
            cases = (
                (('--motion-output', '--taa-thin-region-source', 'both'), '--taa-thin-region-source requires --taa'),
                ((*TAA, *LANE, '--taa-thin-region', 'off', '--taa-thin-region-source', 'screen'), '--taa-thin-region-source requires --taa-thin-region with W > 0'),
                ((*TAA, *LANE, '--taa-thin-region', '0', '--taa-thin-region-source', 'both'), '--taa-thin-region-source requires --taa-thin-region with W > 0'),
                ((*TAA, *LANE, '--taa-thin-vote', 'off', '--taa-thin-region-source', 'vote'), '--taa-thin-region-source vote requires --taa-thin-vote on'),
                ((*TAA, '--taa-thin-region-source', 'vote'), '--taa-thin-region-source vote requires --taa-thin-vote on'),  # no lane: the vote's default is not sent
                (('--vanilla', '--taa-thin-region-source', 'both'), '--taa-thin-region-source cannot be combined with --vanilla'),
                ((*TAA, *LANE, '--taa-thin-region-source', 'screen'), '--taa-thin-region-source screen is refused with the camera gate'),
                ((*TAA, '--taa-thin-region-gate', 'camera', '--taa-thin-region-source', 'screen'), '--taa-thin-region-source screen is refused with the camera gate'))
            for args, message in cases:
                code, _, error = self.launch(directory, *args)
                self.assertNotEqual(code, 0, args)
                self.assertIn(message, error, args)


def row(**values):
    base = dict(device='1', requested='vote', configured='vote', reason='ok', thin_region='0.9700', thin_vote='1', twins='1', camera_gate='1', default='1')
    base.update(values)
    return 'taa_thin_region_source ' + ' '.join(f'{k}={v}' for k, v in base.items())


class ThinRegionSourceRow(unittest.TestCase):
    def test_rows_are_parsed_in_order(self):
        trace = '\n'.join(['motion_output_device device=1', row(), 'taa_thin_region_source_setting invalid=1',
                           row(device='2', requested='vote', configured='both', reason='thin_vote_off', thin_vote='0'),
                           row(device='3', requested='screen', configured='both', reason='thin_region_off', thin_region='0.0000'),
                           row(device='4', requested='screen', configured='both', reason='screen_refused_camera_gate', default='0')])
        rows = runner.thin_region_source_rows(trace)
        self.assertEqual([(r['device'], r['requested'], r['configured'], r['reason']) for r in rows],
                         [('1', 'vote', 'vote', 'ok'), ('2', 'vote', 'both', 'thin_vote_off'), ('3', 'screen', 'both', 'thin_region_off'),
                          ('4', 'screen', 'both', 'screen_refused_camera_gate')])
        self.assertEqual(runner.thin_region_source_rows('taa_thin_vote_configured requested=1 enabled=1'), [])

    def test_malformed_rows_fail(self):
        for bad in (row(requested='union'), row(configured='none'), row(reason='maybe'),
                    row(reason='program'),                            # a refusal that still configured the request
                    row(requested='screen', configured='vote'),       # ok with a different configuration
                    row(twins='2'), row(thin_region='x'), row(default='2'),
                    row(requested='screen', configured='screen', camera_gate='1')):  # screen is refused under the camera gate
            with self.subTest(bad=bad), self.assertRaises((AssertionError, ValueError)):
                runner.thin_region_source_rows(bad)
        text = row()
        self.assertEqual(len(runner.thin_region_source_rows(text.replace(' default=1', ' default=1 extra=7'))), 1)

    def test_one_case_per_source_value(self):
        # The motion runner's source cases run the camera gate: screen is its refusal case (configured both).
        self.assertEqual(sorted(runner.THIN_VOTE_SOURCE_CASES.values()), ['both', 'screen', 'vote'])
        names = {entry['name']: entry for entry in runner.CASES}
        for name, source in runner.THIN_VOTE_SOURCE_CASES.items():
            env = names[name]['hdr_env']
            self.assertEqual((names[name]['mode'], env['X3M_TAA_THIN_REGION_SOURCE'], env['X3M_TAA_THIN_VOTE']), ('thinvote', source, 'on'))


class ThinRegionSourceContract(unittest.TestCase):
    def test_dll_parses_resolves_and_logs(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertIn('GetEnvironmentVariableW(L"X3M_TAA_THIN_REGION_SOURCE",setting,32)', capture)
        self.assertIn('log("taa_thin_region_source_setting invalid=1");', capture)
        self.assertIn('configure_thin_region_source(taa_thin_region_source,taa_thin_region_source_given,taa_thin_region_source_default)', capture)
        self.assertIn('GetEnvironmentVariableW(L"X3M_TAA_THIN_REGION_SOURCE_DEFAULT",setting,32)==1', capture)
        motion = (ROOT / 'src/proxy/motion_output.cpp').read_text()
        self.assertIn('if (SUCCEEDED(hr) && taa_thin_source_given_) {', motion)  # logged only when given: the default log is unchanged
        self.assertIn('log("taa_thin_region_source device=%llu requested=%s configured=%s reason=%s ', motion)
        self.assertIn('taa_thin_source_ == 1 && taa_thin_camera_gate_ ? "screen_refused_camera_gate"', motion)
        self.assertIn('in.thin_region_source = static_cast<renderer::ThinRegionSource>(taa_thin_source_configured_);', motion)

    def test_pass_draws_the_plain_program_for_screen_and_sets_c10y_for_vote_only(self):
        source = (ROOT / 'src/renderer/temporal_pass.cpp').read_text()
        self.assertIn('plain_fold&&in.thin_vote&&thin_live&&!screen_source&&four_channel?line_mask_depth_thin_:nullptr', source)
        self.assertIn('const bool camera_vote=camera&&in.thin_vote&&thin_live&&four_channel;', source)
        self.assertIn('const bool vote_source=diagnostics_.thin_vote&&in.thin_region_source==ThinRegionSource::Vote;', source)
        self.assertIn('const float emissive_constants[4]={emissive_vote?in.thin_region_emissive:0.f,vote_source?1.f:0.f,camera_vote?1.f:0.f,0.f};', source)
        # Screen on the camera gate refuses the run (the folded resolve has no plain program).
        self.assertIn('!in.current_depth||in.thin_region_source==ThinRegionSource::Screen))||', source)
        header = (ROOT / 'src/renderer/temporal_pass.h').read_text()
        self.assertIn('enum class ThinRegionSource : unsigned { Both = 0, Screen = 1, Vote = 2 };', header)
        self.assertIn('ThinRegionSource thin_region_source = ThinRegionSource::Both;', header)

    def test_the_folded_resolve_reads_c10(self):
        shader = (ROOT / 'src/temporal/resolve.hlsl').read_text()
        self.assertIn('if (thinTests.z > 0.5 && thinValid(lane.r) && lane.a >= 0 && lane.a < 1) return 1;', shader)
        self.assertIn('[branch] if (!(thinTests.y > 0.5)) { if (fragmentedDepth(at)) return 1; }', shader)

    def test_only_the_twins_read_c10y(self):
        shader = (ROOT / 'src/temporal/line_mask_ps.hlsl').read_text()
        guarded = shader.split('#ifdef X3M_THIN_VOTE\n')
        self.assertEqual(sum('emissive.y' in part.split('#endif')[0] for part in guarded[1:]), 1)
        self.assertEqual(shader.count('emissive.y'), 1)
        self.assertIn('else [branch] if (!(emissive.y > 0.5))', shader)


if __name__ == '__main__':
    unittest.main()
