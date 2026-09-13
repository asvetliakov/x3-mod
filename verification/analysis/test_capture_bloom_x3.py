"""Strict parser checks for the real capture/COM fixture's acceptance evidence."""
import unittest
from capture_bloom_x3_run import CASES, parse_output


def success_output():
    lines = []
    for name in CASES:
        reset = name.startswith('reset')
        terminal = name in ('final_release', 'thread_final_release')
        row = dict(name=name, status='PASS', pre=1, post=int(name != 'escape'), cleanup=1,
                   abnormal=int(name == 'escape'), prepared=1,
                   committed=int(not reset and name != 'escape'), resets=int(reset),
                   defaults_clear=int(reset), map_absent=1, cpu_expired=1,
                   pin_kept=int(reset), active=0, prepare_hr='00000000', commit_hr='00000000',
                   original=1, caught=int(name == 'escape'), continued=int(name == 'continue'),
                   resumed=int(name == 'continue'), release=int(terminal), motion_refs=12, hdr=1, taa=1,
                   during_absent=0 if terminal else 999, during_expired=0 if terminal else 999,
                   reset_hr='8876086c' if name.endswith('_fail') else '00000000')
        lines.append('CASE ' + ' '.join(f'{k}={v}' for k, v in row.items()))
    lines.append('RESULT PASS checks=300 failures=0 cases=10 skipped=0')
    return '\n'.join(lines)


class CaptureBloomX3ParserTests(unittest.TestCase):
    def test_all_cases(self):
        parsed = parse_output(success_output())
        self.assertEqual(len(parsed['cases']), 10)
        self.assertEqual(parsed['checks'], 300)

    def test_missing_case(self):
        with self.assertRaises(ValueError):
            parse_output('\n'.join(success_output().splitlines()[1:]))

    def test_duplicate_case(self):
        text = success_output()
        with self.assertRaises(ValueError):
            parse_output(text + '\n' + text.splitlines()[0])

    def test_counter_failures(self):
        for old, new in [('prepared=1', 'prepared=0'), ('defaults_clear=1', 'defaults_clear=0'),
                         ('pin_kept=1', 'pin_kept=0'), ('cpu_expired=1', 'cpu_expired=0'),
                         ('active=0', 'active=1'), ('committed=1', 'committed=0'),
                         ('during_absent=0', 'during_absent=1'), ('continued=1', 'continued=0'),
                         ('motion_refs=12', 'motion_refs=0'), ('hdr=1', 'hdr=0'), ('taa=1', 'taa=0')]:
            with self.subTest(field=old), self.assertRaises(ValueError):
                parse_output(success_output().replace(old, new, 1))

    def test_hresult_failures(self):
        for old, new in [('prepare_hr=00000000', 'prepare_hr=80004005'),
                         ('commit_hr=00000000', 'commit_hr=80004005'),
                         ('reset_hr=8876086c', 'reset_hr=00000000')]:
            with self.subTest(field=old), self.assertRaises(ValueError):
                parse_output(success_output().replace(old, new, 1))

    def test_missing_field(self):
        with self.assertRaises(KeyError):
            parse_output(success_output().replace(' prepared=1', '', 1))

    def test_failed_check(self):
        with self.assertRaises(ValueError):
            parse_output('CHECK_FAIL case=normal name=bind\n' + success_output())

    def test_result_counts(self):
        with self.assertRaises(ValueError):
            parse_output(success_output().replace('cases=10', 'cases=9'))

    def test_only_ex_unavailable_can_skip(self):
        lines = success_output().splitlines()
        for i, line in enumerate(lines):
            if line.startswith('CASE name=reset_ex '):
                lines[i] = 'CASE name=reset_ex status=SKIP reason=create9ex_unavailable hr=8876086a'
        lines[-1] = 'RESULT PASS checks=280 failures=0 cases=9 skipped=1'
        self.assertEqual(parse_output('\n'.join(lines))['skipped'], 1)
        with self.assertRaises(ValueError):
            parse_output('\n'.join(lines).replace('reason=create9ex_unavailable', 'reason=bind_failed'))

if __name__ == '__main__':
    unittest.main()
