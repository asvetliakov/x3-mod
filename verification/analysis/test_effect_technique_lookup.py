"""Synthetic-output tests for run_effect_technique_lookup.py's parsing and validation; no game content."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'probe'))
from run_effect_technique_lookup import DRAWS_PER_FRAME, cache_saving, parse_output, summarize  # noqa: E402

MODULE = ('MODULE name=d3dx9_37 path=C:\\run\\d3dx9_37.dll image_size=3895296 stamp=47cdef5d '
          'exports=336 wine_builtin=0\n')
BUILTIN_MODULE = ('MODULE name=d3dx9_37 path=C:\\run\\d3dx9_37.dll image_size=585728 stamp=00000000 '
                  'exports=300 wine_builtin=1\n')
DEVICE = 'DEVICE vs=fffe0300 ps=ffff0300\n'
FIXTURE = ('FIXTURE effect=effect0.fb bytes=41004 parameters=54 techniques=2 engine_techniques=DEFAULT,BUMPMAP '
           'iterations=100000 repetitions=2 warmup=400 batch=100 frequency=10000000\n')
MEASURES = (('baseline', '-', 0.001), ('gtbn:DEFAULT', 'DEFAULT', 0.005), ('gtbn:BUMPMAP', 'BUMPMAP', 0.006),
            ('settech_same:DEFAULT', 'DEFAULT', 0.005), ('settech_alt:DEFAULT|BUMPMAP', 'DEFAULT|BUMPMAP', 0.005),
            ('beginend:DEFAULT', 'DEFAULT', 0.007))


def measure(name, detail, us, rep, iterations=100000):
    return (f'MEASURE measure={name} detail={detail} rep={rep} iterations={iterations} batch=100 batches=1000 '
            f'us_median={us:.4f} us_p90={us + 0.001:.4f} us_mean={us:.4f} us_p10={us:.4f} us_p99={us + 0.002:.4f}\n')


def output(module=MODULE, status='pass', iterations=100000, repetitions=2):
    text = module + DEVICE + FIXTURE
    for name, detail, us in MEASURES:
        for rep in range(repetitions):
            text += measure(name, detail, us, rep, iterations)
    return text + f'RESULT checks=90 status={status}\n'


class ParseOutput(unittest.TestCase):
    def test_records_module_fixture_and_measures(self):
        parsed = parse_output(output(), 100000)
        self.assertEqual(parsed['module']['image_size'], 3895296)
        self.assertEqual(parsed['module']['wine_builtin'], 0)
        self.assertEqual(parsed['fixture']['engine_techniques'], 'DEFAULT,BUMPMAP')
        self.assertEqual(parsed['fixture']['batch'], 100)
        self.assertEqual(len(parsed['measures']), len(MEASURES))
        self.assertEqual(len(parsed['measures']['baseline']), 2)
        self.assertEqual(parsed['checks'], 90)

    def test_rejects_failure_missing_baseline_and_short_runs(self):
        with self.assertRaises(ValueError):
            parse_output(output(status='fail'), 100000)
        text = ''.join(line + '\n' for line in output().splitlines()
                       if not line.startswith('MEASURE measure=baseline'))
        with self.assertRaises(ValueError):
            parse_output(text, 100000)
        with self.assertRaises(ValueError):
            parse_output(output(iterations=50000), 100000)

    def test_rejects_missing_repetition(self):
        text = output().replace(measure('gtbn:BUMPMAP', 'BUMPMAP', 0.006, 1), '')
        with self.assertRaises(ValueError):
            parse_output(text, 100000)


class Summarize(unittest.TestCase):
    def test_subtracts_the_baseline_and_scales_to_the_frame(self):
        summary = summarize(parse_output(output(), 100000))
        self.assertNotIn('net_us_median', summary['baseline'])
        self.assertAlmostEqual(summary['gtbn:DEFAULT']['net_us_median'], 0.004, places=4)
        self.assertAlmostEqual(summary['gtbn:DEFAULT']['net_us_p90'], 0.004, places=4)
        self.assertAlmostEqual(summary['beginend:DEFAULT']['ms_per_frame_median'],
                               0.006 * DRAWS_PER_FRAME / 1000.0, places=4)
        self.assertEqual(len(summary['gtbn:BUMPMAP']['rep_medians']), 2)

    def test_fails_closed_on_a_builtin_d3dx9(self):
        with self.assertRaises(ValueError):
            summarize(parse_output(output(module=BUILTIN_MODULE), 100000))


class CacheSaving(unittest.TestCase):
    def test_lookup_plus_redundant_settechnique(self):
        summary = summarize(parse_output(output(), 100000))
        saving = cache_saving(summary)
        self.assertAlmostEqual(saving['gtbn_us_median'], 0.0045, places=4)
        self.assertAlmostEqual(saving['settech_same_us_median'], 0.004, places=4)
        self.assertAlmostEqual(saving['saved_us_per_draw'], 0.0085, places=4)
        self.assertAlmostEqual(saving['saved_ms_per_frame'], round(0.0085 * DRAWS_PER_FRAME / 1000.0, 4), places=4)


if __name__ == '__main__':
    unittest.main()
