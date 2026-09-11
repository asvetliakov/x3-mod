"""Portable stale/incomplete report controls; never invokes Wine."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('retirement_report', ROOT / 'verification/probe/run_rigid_retirement.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

class ReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = (ROOT / 'verification/results/rigid-retirement.txt').read_text()

    def test_current(self):
        self.assertEqual(module.validate_report(self.text)['checks'], 168)

    def reject(self, text):
        with self.assertRaises(AssertionError):
            module.validate_report(text)

    def test_zero_checks(self):
        self.reject('RESULT PASS checks=0 cases=0 callbacks=0 devices=2\n')

    def test_missing_check(self):
        self.reject(self.text.replace('CHECK batch initially reusable PASS\n', '', 1))

    def test_duplicate_terminal(self):
        self.reject(self.text + self.text.splitlines()[-1] + '\n')

    def test_trailing_output(self):
        self.reject(self.text + 'unexpected\n')

    def test_missing_pure(self):
        self.reject(self.text.replace('DEVICE pure=1', 'DEVICE pure=0'))

    def test_failed_check(self):
        self.reject(self.text.replace('CHECK batch initially reusable PASS', 'CHECK batch initially reusable FAIL', 1))

    def test_wrong_inventory(self):
        self.reject(self.text.replace('partial capture did not acquire failed depth output', 'unrelated control', 1))

    def test_missing_reset(self):
        self.reject(self.text.replace('RESET PASS\n', '', 1))

if __name__ == '__main__':
    unittest.main()
