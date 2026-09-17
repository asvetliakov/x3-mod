"""Synthetic-output tests for run_effect_beginpass.py's parsing and validation; no game content."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'probe'))
from run_effect_beginpass import CALLBACKS, parse_output, row, summarize  # noqa: E402

MODULE = ('MODULE name=d3dx9_37 path=C:\\run\\d3dx9_37.dll image_size=3895296 stamp=47cdef5d '
          'exports=381 wine_builtin=0\n')
BUILTIN_MODULE = ('MODULE name=d3dx9_37 path=C:\\run\\d3dx9_37.dll image_size=585728 stamp=00000000 '
                  'exports=300 wine_builtin=1\n')
DEVICE = 'DEVICE vs=fffe0300 ps=ffff0300\n'
FIXTURE = ('FIXTURE effect=effect.fb bytes=41888 parameters=44 settable=31 skipped=13 technique=DEFAULT '
           'technique_source=named passes=1 pass=P0 iterations=10000 repetitions=2 warmup=500 frequency=24000000\n')


def regime(name, rep, us, total, constants=12.0):
    counts = {'render_state': 13.0, 'sampler_state': 18.0, 'texture': 4.0, 'texture_stage': 0.0,
              'vertex_shader': 1.0, 'pixel_shader': 1.0, 'shader_constant': constants,
              'constant_registers': constants * 4, 'other': 0.0, 'total': total}
    text = ' '.join(f'cb_{key}={counts[key]:.4f}' for key in CALLBACKS)
    return (f'REGIME name={name} rep={rep} iterations=10000 us_median={us:.4f} us_mean={us + 0.2:.4f} '
            f'us_p05={us - 0.3:.4f} us_p95={us + 1.0:.4f} setters_us_mean=1.2500 {text}\n')


def output(module=MODULE, medians=((7.0, 7.4), (9.0, 9.2), (5.0, 5.2)), totals=(50.0, 50.0, 12.0), status='pass'):
    text = module + DEVICE + FIXTURE
    for name, pair, total in zip(('unchanged', 'changed', 'none'), medians, totals):
        for rep, value in enumerate(pair):
            text += regime(name, rep, value, total)
    return text + f'MANAGER references=1\nRESULT checks=120 status={status}\n'


class ParseOutput(unittest.TestCase):
    def test_records_module_fixture_and_regimes(self):
        parsed = parse_output(output())
        self.assertEqual(parsed['module']['image_size'], 3895296)
        self.assertEqual(parsed['module']['wine_builtin'], 0)
        self.assertEqual(parsed['fixture']['technique'], 'DEFAULT')
        self.assertEqual(parsed['fixture']['settable'], 31)
        self.assertEqual(sorted(parsed['regimes']), ['changed', 'none', 'unchanged'])
        self.assertEqual(len(parsed['regimes']['none']), 2)
        self.assertEqual(parsed['regimes']['none'][0]['callbacks']['total'], 12.0)
        self.assertEqual(parsed['checks'], 120)

    def test_failed_run_rejected(self):
        with self.assertRaises(ValueError):
            parse_output(output(status='fail'))

    def test_missing_result_rejected(self):
        with self.assertRaises(ValueError):
            parse_output(MODULE + DEVICE + FIXTURE)

    def test_missing_regime_rejected(self):
        text = output()
        text = '\n'.join(line for line in text.splitlines() if 'name=none' not in line) + '\n'
        with self.assertRaises(ValueError) as caught:
            parse_output(text)
        self.assertIn('none', str(caught.exception))

    def test_repetition_count_must_match_header(self):
        text = output().replace('repetitions=2', 'repetitions=3')
        with self.assertRaises(ValueError):
            parse_output(text)

    def test_short_run_rejected(self):
        text = output().replace('iterations=10000 us_median=5.0000', 'iterations=9000 us_median=5.0000')
        with self.assertRaises(ValueError):
            parse_output(text)


class Summarize(unittest.TestCase):
    def test_medians_and_deltas(self):
        summary = summarize(parse_output(output()), expected_builtin=0)
        self.assertAlmostEqual(summary['unchanged']['us_median'], 7.2)   # median of 7.0, 7.4
        self.assertAlmostEqual(summary['none']['us_median'], 5.1)
        self.assertAlmostEqual(summary['deltas']['unchanged_minus_none_us'], 2.1)
        self.assertAlmostEqual(summary['deltas']['changed_minus_none_us'], 4.0)
        self.assertAlmostEqual(summary['deltas']['unchanged_minus_none_callbacks'], 38.0)
        self.assertTrue(summary['deltas']['same_value_set_dirties'])
        self.assertEqual(summary['unchanged']['rep_medians'], [7.0, 7.4])

    def test_value_comparing_d3dx_reports_no_dirty(self):
        summary = summarize(parse_output(output(medians=((5.1, 5.0), (9.0, 9.2), (5.0, 5.1)),
                                                totals=(12.0, 50.0, 12.0))))
        self.assertFalse(summary['deltas']['same_value_set_dirties'])
        self.assertAlmostEqual(summary['deltas']['unchanged_minus_none_callbacks'], 0.0)

    def test_wrong_implementation_fails_closed(self):
        parsed = parse_output(output(module=BUILTIN_MODULE))
        with self.assertRaises(ValueError):
            summarize(parsed, expected_builtin=0)
        self.assertEqual(summarize(parsed, expected_builtin=1)['none']['callbacks']['total'], 12.0)

    def test_silent_changed_regime_rejected(self):
        with self.assertRaises(ValueError):
            summarize(parse_output(output(totals=(0.0, 0.0, 0.0))))

    def test_row_is_one_line_with_the_numbers(self):
        summary = summarize(parse_output(output()))
        line = row('native', 'unchanged', summary)
        self.assertNotIn('\n', line)
        self.assertIn('7.200', line)
        self.assertIn('total= 50.00', line)


if __name__ == '__main__':
    unittest.main()
