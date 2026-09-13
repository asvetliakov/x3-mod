"""Host controls for bridge verdicts; no Wine or native-Windows execution."""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'verification/probe'))
spec=importlib.util.spec_from_file_location('bloom_bridge_runner',ROOT/'verification/probe/bloom_return_bridge_run.py')
runner=importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
GOOD='BLOOM RETURN BRIDGE RESULT checks=240 failures=0 normal=4 exceptional=16 continued=4 stack_alignments=4\n'


class BridgeReportTests(unittest.TestCase):
    def test_complete_inventory_passes(self):
        self.assertEqual(runner.parse_report(GOOD,0),{'passed':True,'checks':240})

    def test_under_and_over_counts_fail(self):
        for count in (0,239,241,2400):
            with self.subTest(count=count):
                self.assertFalse(runner.parse_report(GOOD.replace('checks=240',f'checks={count}'),0)['passed'])

    def test_nonzero_exit_fails(self):
        self.assertFalse(runner.parse_report(GOOD,1)['passed'])

    def test_fail_line_overrides_success_record(self):
        for prefix in ('FAIL original CPU preservation\n','FAIL\n'):
            with self.subTest(prefix=prefix):
                self.assertFalse(runner.parse_report(prefix+GOOD,0)['passed'])

    def test_duplicate_records_fail_even_when_second_malformed(self):
        for second in (GOOD,GOOD.replace('exceptional=16','exceptional=0')):
            with self.subTest(second=second):
                self.assertFalse(runner.parse_report(GOOD+second,0)['passed'])

    def test_missing_failed_or_different_case_inventory_fails(self):
        for output in ('',GOOD.replace('failures=0','failures=1'),GOOD.replace('normal=4','normal=3')):
            with self.subTest(output=output):
                self.assertFalse(runner.parse_report(output,0)['passed'])


if __name__=='__main__':unittest.main()
