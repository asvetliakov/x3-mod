"""Portable negative controls for the private motion diagnostic report parser."""
import importlib.util
from pathlib import Path
import unittest
ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('motion_capture_runner', ROOT / 'verification/probe/run_motion_capture.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
STATE = 'CHECK pre-Clear replay preserves captured application state PASS'
END = 'RESULT PASS checks=253 samples=40 state_comparisons=28 devices=2'


def complete():
    return '\n'.join(['DEVICE pure=0', 'DEVICE pure=1'] + [STATE]*28 + ['CHECK boundary preserves full x87 MXCSR and LastError after injected work PASS']*28 +
                     ['CHECK other assertion PASS']*197 + ['SAMPLE component PASS']*40 + [END]) + '\n'


class ReportControls(unittest.TestCase):
    def test_complete(self):
        self.assertEqual(runner.validate_report(complete())['checks'], 253)

    def test_wrong_cpu_inventory(self):
        with self.assertRaises(RuntimeError):
            runner.validate_report(complete().replace("CHECK boundary preserves full x87 MXCSR and LastError after injected work PASS", "CHECK other assertion PASS", 1))

    def test_zero_checks(self):
        with self.assertRaises(RuntimeError):
            runner.validate_report('RESULT PASS checks=0 samples=0 state_comparisons=0 devices=2\n')

    def test_incomplete_check_inventory(self):
        with self.assertRaises(RuntimeError):
            runner.validate_report(complete().replace('CHECK other assertion PASS\n', '', 1))

    def test_missing_numeric(self):
        with self.assertRaises(RuntimeError):
            runner.validate_report(complete().replace('SAMPLE component PASS\n', '', 1))

    def test_wrong_state_inventory(self):
        with self.assertRaises(RuntimeError):
            runner.validate_report(complete().replace(STATE, 'CHECK other assertion PASS', 1))

    def test_missing_pure(self):
        with self.assertRaises(RuntimeError):
            runner.validate_report(complete().replace('DEVICE pure=1', 'DEVICE pure=0'))

    def test_duplicate_terminal(self):
        with self.assertRaises(RuntimeError):
            runner.validate_report(complete()+END+'\n')

    def test_trailing_output(self):
        with self.assertRaises(RuntimeError):
            runner.validate_report(complete()+'late output\n')

    def test_nonpassing_check(self):
        with self.assertRaises(RuntimeError):
            runner.validate_report(complete().replace('CHECK other assertion PASS', 'CHECK other assertion FAIL', 1))


if __name__ == '__main__':
    unittest.main()
