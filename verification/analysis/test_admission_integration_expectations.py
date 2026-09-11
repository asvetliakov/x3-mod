"""Original metadata controls for actual-DLL admission mode/final witnesses."""
import importlib.util
from pathlib import Path
import unittest

PATH = Path(__file__).parents[1] / 'probe/verify_ownership_integration.py'
SPEC = importlib.util.spec_from_file_location('admission_integration_expectations', PATH)
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


def witness(enabled=True, roots=12):
    return (f'application_admission_mode requested={int(enabled)} enabled={int(enabled)} live_replay=0 coverage_complete=0\n'
            f'application_admission_final phase=factory active_roots=0 waiting_roots=0 admitted_roots={roots if enabled else 0} promotions=0 vetoes=0 first_veto=0 enabled={int(enabled)}\n')


class AdmissionExpectations(unittest.TestCase):
    def test_enabled_and_disabled(self):
        for enabled in (False, True):
            result = VERIFY.verify_admission(witness(enabled), enabled, 1)
            self.assertEqual(result['enabled'], enabled)
            self.assertEqual(result['promotions'], 0)

    def test_serial_factories_increase_cumulative_roots(self):
        data = witness() + witness(roots=24).splitlines()[1] + '\n'
        self.assertEqual(VERIFY.verify_admission(data, True, 2)['admitted_roots'], 24)
        with self.assertRaises(AssertionError):
            VERIFY.verify_admission(data.replace('admitted_roots=24', 'admitted_roots=11'), True, 2)

    def test_nested_factory_requires_following_finished_device(self):
        data=witness().replace('active_roots=0', 'active_roots=1')
        finished=witness().splitlines()[1].replace('phase=factory', 'phase=device')+'\n'
        result=VERIFY.verify_admission(data+finished, True, 1, 1)
        self.assertEqual(result['nested_factory_witnesses'], 1)
        for changed in (finished+data, data+finished.replace('active_roots=0','active_roots=1'), data):
            with self.subTest(data=changed), self.assertRaises(AssertionError):
                VERIFY.verify_admission(changed, True, 1, 1)

    def test_permanent_veto_and_first_reason(self):
        first=witness().replace('vetoes=0 first_veto=0', 'vetoes=1 first_veto=1')
        second=witness(roots=24).splitlines()[1].replace('vetoes=0 first_veto=0', 'vetoes=3 first_veto=1')+'\n'
        self.assertEqual(VERIFY.verify_admission(first+second, True, 2)['final_vetoes'], 3)
        for replacement in ('vetoes=0 first_veto=0', 'vetoes=3 first_veto=2', 'vetoes=0 first_veto=1', 'vetoes=3 first_veto=3'):
            with self.subTest(value=replacement), self.assertRaises(AssertionError):
                VERIFY.verify_admission(first+second.replace('vetoes=3 first_veto=1', replacement), True, 2)

    def test_nonzero_roots_waiters_or_promotions_rejected(self):
        for field in ('active_roots', 'waiting_roots', 'promotions'):
            with self.subTest(field=field), self.assertRaises(AssertionError):
                VERIFY.verify_admission(witness().replace(field+'=0', field+'=1'), True, 1)

    def test_missing_duplicate_or_unknown_field_rejected(self):
        for data in (witness().replace('waiting_roots=0 ', ''),
                     witness().replace('active_roots=0', 'active_roots=7 active_roots=0'),
                     witness().replace('active_roots=0', 'active_roots=0 unexpected=0')):
            with self.subTest(data=data), self.assertRaises(AssertionError):
                VERIFY.verify_admission(data, True, 1)

    def test_duplicate_or_missing_mode_and_final_rejected(self):
        data=witness(); mode, final=data.splitlines()
        for changed in (final+'\n', mode+'\n'+data, data+final+'\n', mode+'\n'):
            with self.subTest(data=changed), self.assertRaises(AssertionError):
                VERIFY.verify_admission(changed, True, 1)

    def test_disabled_work_and_enabled_empty_rejected(self):
        for data, requested in ((witness(False).replace('admitted_roots=0', 'admitted_roots=1'), False),
                                (witness(False).replace('vetoes=0', 'vetoes=1'), False),
                                (witness(roots=0), True), (witness(False), True)):
            with self.subTest(data=data), self.assertRaises(AssertionError):
                VERIFY.verify_admission(data, requested, 1)

    def test_live_or_claimed_complete_coverage_rejected(self):
        for field in ('live_replay', 'coverage_complete'):
            with self.subTest(field=field), self.assertRaises(AssertionError):
                VERIFY.verify_admission(witness().replace(field+'=0', field+'=1'), True, 1)

    def test_invalid_or_overflow_counters_rejected(self):
        for value in ('-1', 'nan', str(1 << 64)):
            with self.subTest(value=value), self.assertRaises(AssertionError):
                VERIFY.verify_admission(witness().replace('admitted_roots=12', 'admitted_roots='+value), True, 1)


if __name__ == '__main__':
    unittest.main()
