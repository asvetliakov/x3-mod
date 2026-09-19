from pathlib import Path
import sys
import unittest

PROBE = Path(__file__).parents[1] / 'probe'
sys.path.insert(0, str(PROBE))
import verify_ownership as verifier  # noqa: E402


class OwnershipReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        results = Path(__file__).parents[1] / 'results/bottle-X3'
        cls.baseline = (results / 'ownership-baseline.txt').read_text()
        cls.wrapped = (results / 'ownership-wrapped.txt').read_text()

    def test_retained_inventory_matches_fixture(self):
        self.assertEqual(verifier.verify_report('baseline', self.baseline),
                         {'checks': 370, 'failures': 0})
        self.assertEqual(verifier.verify_report('wrapped', self.wrapped),
                         {'checks': 563, 'failures': 0})

    def test_malformed_terminal_is_rejected(self):
        bad = self.wrapped.replace('OWNERSHIP RESULT checks=563 failures=0',
                                   'OWNERSHIP RESULT checks=invalid failures=0')
        with self.assertRaisesRegex(AssertionError, 'incomplete check inventory'):
            verifier.verify_report('wrapped', bad)

    def test_truncated_report_is_rejected(self):
        bad = self.wrapped.rsplit('OWNERSHIP RESULT ', 1)[0]
        with self.assertRaisesRegex(AssertionError, 'incomplete check inventory'):
            verifier.verify_report('wrapped', bad)

    def test_duplicate_terminal_is_rejected(self):
        terminal = self.wrapped.rstrip().splitlines()[-1]
        bad = self.wrapped + terminal + '\n'
        with self.assertRaisesRegex(AssertionError, 'incomplete check inventory'):
            verifier.verify_report('wrapped', bad)

    def test_count_preserving_inventory_substitution_is_rejected(self):
        bad = self.wrapped.replace('CHECK prefix sentinel lock PASS',
                                   'CHECK unrelated replacement PASS', 1)
        with self.assertRaisesRegex(AssertionError, 'Step D inventory mismatch'):
            verifier.verify_report('wrapped', bad)


if __name__ == '__main__':
    unittest.main()
