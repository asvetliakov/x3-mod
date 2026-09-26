"""Reader of the per-frame sun trace (tools/analysis/shadow_sun_frame.py) and
its launcher option --shadow-sun-trace. Synthetic lines only; no game, no Wine."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'analysis'))
import shadow_sun_frame as reader
from source_text import source_text


def line(frame, source='point', reason='point', poll='ok', mask=0, carried=0, checks=2,
         disagreements=0, agreement=0.25, distance=1.57e7, cascades=4, device=1):
    return (f'shadow_sun_frame device={device} frame={frame} source={source} reason={reason} poll={poll} '
            f'rederived={bin(mask).count("1")} rederived_mask={mask} carried={carried} checks={checks} '
            f'disagreements={disagreements} agreement_deg={agreement:.6f} distance={distance:.9g} cascades={cascades}\n')


class ShadowSunFrameReader(unittest.TestCase):
    def test_parses_a_line_into_per_cascade_bits(self):
        row = reader.parse_line(line(7, mask=0b0101))
        self.assertEqual((row['device'], row['frame'], row['source'], row['reason']), (1, 7, 'point', 'point'))
        self.assertEqual(row['rederived'], 2)
        self.assertEqual(row['cascade_rederived'], [True, False, True, False])
        self.assertAlmostEqual(row['agreement_deg'], 0.25)

    def test_malformed_lines_are_refused(self):
        for text in ('shadow_sun_frame device=1 frame=2\n',                       # missing fields
                     line(1).replace('source=point', 'source=constant'),          # unknown source
                     line(1).replace('cascades=4', 'cascades=6'),                 # beyond shadow_cascade_max
                     line(1, mask=0b10000),                                       # a bit beyond the cascade count
                     line(1).replace('rederived=0', 'rederived=1'),               # count disagrees with the mask
                     line(1).replace('frame=1', 'frame=x')):                      # non-numeric
            with self.assertRaises(reader.MalformedLine, msg=text):
                reader.parse_line(text)

    def test_summary_gives_the_per_cascade_rederivation_rate(self):
        rows = reader.parse_lines(
            [line(1, mask=0b0001), line(2, mask=0b0011), line(3), line(4)]
            + [line(5, source='latch', reason='unchecked', poll='no_light', checks=0, agreement=-1.0, distance=0.0)])
        out = reader.summary(rows)
        self.assertEqual((out['frames'], out['cascades'], out['devices']), (5, 4, [1]))
        self.assertEqual(out['source'], {'point': 4, 'latch': 1})
        self.assertEqual(out['reason']['unchecked'], 1)
        self.assertEqual(out['rederived_frames'], 2)
        self.assertAlmostEqual(out['rederived_frame_rate'], 0.4)
        self.assertEqual(out['cascade_rederived'], [2, 1, 0, 0])
        self.assertAlmostEqual(out['cascade_rederived_rate'][0], 0.4)
        self.assertEqual(out['checked_frames'], 4)
        self.assertEqual(out['agreement_deg']['frames'], 4)  # the -1 of an unchecked frame is excluded
        self.assertAlmostEqual(out['distance']['median'], 1.57e7)
        self.assertEqual(reader.summary([]), {'frames': 0})

    def test_cli_reads_a_log_without_loading_it_whole(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'trace.log'
            path.write_text('frame_end device=1 frame=1 draws=2 capture=0\n' + line(1, mask=0b0001) + line(2))
            import contextlib
            import io
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(reader.main([str(path)]), 0)
            self.assertEqual(json.loads(output.getvalue())['rederived_frames'], 1)


class ShadowSunTraceLaunchOption(unittest.TestCase):
    """The launcher exports X3M_SHADOW_SUN_TRACE explicitly, so an inherited
    value can neither enable nor disable the trace."""

    def test_option_requires_the_cascades_and_cannot_be_inherited(self):
        # The per-frame sun trace is part of --debug since the logging tiers (2026-09-26): the DLL reads
        # X3M_SHADOW_SUN_TRACE or X3M_DEBUG (with the cascades on); --shadow-sun-trace is unknown and an inherited value dropped.
        from verification.analysis.test_shadow_cascades import launch
        base = ['--motion-output', '--ownership', '--shadow-replay-depth']
        with tempfile.TemporaryDirectory() as directory:
            code, _, error = launch(directory, *base, '--shadow-cascades', 'default', '--shadow-sun-trace')
            self.assertNotEqual(code, 0)
            self.assertIn('unrecognized arguments', error)
            code, output, error = launch(directory, *base, '--shadow-cascades', 'default', '--debug', inherited={'X3M_SHADOW_SUN_TRACE': '1'})
            self.assertEqual(code, 0, error)
            env = json.loads(output)['env']
            self.assertEqual(env['X3M_DEBUG'], '1')
            self.assertNotIn('X3M_SHADOW_SUN_TRACE', env)
            code, output, error = launch(directory, *base, inherited={'X3M_SHADOW_SUN_TRACE': '1'})
            self.assertEqual(code, 0, error)
            self.assertNotIn('X3M_SHADOW_SUN_TRACE', json.loads(output)['env'])
        self.assertIn('const bool trace_asked=log_tier::debug_flag(L"X3M_SHADOW_SUN_TRACE");', source_text(ROOT / 'src/proxy/capture.cpp'))


if __name__ == '__main__':
    unittest.main()
