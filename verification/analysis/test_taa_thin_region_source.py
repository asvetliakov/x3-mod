"""Host tests of the thin region's flag source (docs/architecture/taa-thin-geometry-alternatives.md section 3.2,
taa-mask-fold.md). --taa-thin-region-source and X3M_TAA_THIN_REGION_SOURCE(_DEFAULT) were removed on 2026-09-25 (user
decision, docs/verification/launcher-options-inventory.md "4. Removed"): the DLL configures the vote alone whenever
the thin vote and the thin region are on (both otherwise), marked default when the vote was the launcher's default. Covered:
the option is unknown and the variables are neither sent nor inherited; the runner's parser of the DLL's
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

    def test_the_option_is_unknown_and_the_variables_never_survive(self):
        inherited = {'X3M_TAA_THIN_REGION_SOURCE': 'both', 'X3M_TAA_THIN_REGION_SOURCE_DEFAULT': '0'}
        with tempfile.TemporaryDirectory() as directory:
            for args in ((*TAA, *LANE), (*TAA, *LANE, '--taa-thin-vote', 'off'), (*TAA, '--no-sun-shadow-lane'), ('--motion-output', '--no-taa')):
                for extra in (None, inherited):
                    env = self.env(directory, *args, inherited=extra)
                    self.assertNotIn('X3M_TAA_THIN_REGION_SOURCE', env, args)
                    self.assertNotIn('X3M_TAA_THIN_REGION_SOURCE_DEFAULT', env, args)
            code, output, error = self.launch(directory, '--vanilla', inherited=inherited)
            self.assertEqual(code, 0, error)
            self.assertNotIn('X3M_TAA_THIN_REGION_SOURCE', json.loads(output)['env'])
            for value in ('both', 'vote', 'screen'):
                code, _, error = self.launch(directory, *TAA, *LANE, '--taa-thin-region-source', value)
                self.assertEqual(code, 2, value)
                self.assertIn('unrecognized arguments', error)


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

    def test_one_vote_source_case(self):
        # The motion runner keeps the vote case (the DLL's own rule); its both / screen twins went with the variable.
        self.assertEqual(runner.THIN_VOTE_SOURCE_CASES, {'seam-thin-vote-far-on-source-vote': 'vote'})
        names = {entry['name']: entry for entry in runner.CASES}
        env = names['seam-thin-vote-far-on-source-vote']['hdr_env']
        self.assertEqual((names['seam-thin-vote-far-on-source-vote']['mode'], env['X3M_TAA_THIN_VOTE']), ('thinvote', 'on'))
        self.assertNotIn('X3M_TAA_THIN_REGION_SOURCE', env)
        for gone in ('seam-thin-vote-far-on-source-both', 'seam-thin-vote-far-on-source-screen'):
            self.assertNotIn(gone, names)


class ThinRegionSourceContract(unittest.TestCase):
    def test_dll_derives_resolves_and_logs(self):
        capture = (ROOT / 'src/proxy/capture.cpp').read_text()
        self.assertNotIn('GetEnvironmentVariableW(L"X3M_TAA_THIN_REGION_SOURCE', capture)
        self.assertIn('taa_thin_region_source_given=taa_thin_vote&&taa_thin_region[0]>0.f;', capture)
        self.assertIn('taa_thin_region_source=taa_thin_region_source_given?2u:0u;', capture)
        self.assertIn('taa_thin_region_source_default=taa_thin_region_source_given&&taa_thin_vote_default;', capture)
        self.assertIn('configure_thin_region_source(taa_thin_region_source,taa_thin_region_source_given,taa_thin_region_source_default)', capture)
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
