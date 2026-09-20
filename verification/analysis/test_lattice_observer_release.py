"""Checker contract tests using synthetic records, never backend proof."""
import copy
import tempfile
import unittest
from pathlib import Path
from lattice_observer_release_report import BASE_OPERATIONS, validate
from lattice_observer_release_build import build


def observations(nested=False):
    rows = [dict(type='device', schema=1, real_d3d9=True, selector_bypassed=True,
                 draws=0, production_release_hook=False, mrt=4),
            dict(type='hook_control', add=1, release=1)]
    for case in range(4):
        if case == 2:
            rows.append(dict(type='reset', hr='00000000'))
        rows.append(dict(type='case', case=case, cycle=case // 2,
                         ownership='dropped_bound' if case % 2 else 'held'))
        callbacks = []
        sample = dict(hr=['00000000'] * 3, pointers=[123, 234, 345], sample_add=0, sample_release=0)
        for name in BASE_OPERATIONS:
            releases = int(nested and case % 2 and name in ('vs.release', 'effective'))
            adds = int(nested and case % 2 and name in ('vs.get', 'effective'))
            rows.append(dict(type='operation', case=case, name=name, hr='00000000',
                             cpu_preserved=True, add=adds, release=releases,
                             before=copy.deepcopy(sample), after=copy.deepcopy(sample)))
            for method, count in [('addref', adds), ('release', releases)]:
                callbacks.extend(dict(type='callback', case=case, phase=name, method=method, result=5)
                                 for _ in range(count))
        for slot in range(4):
            rows.append(dict(type='container', case=case, slot=slot,
                             hr='80004002' if slot else '00000000', present=slot == 0))
        rows.append(dict(type='effective_reached', case=case, target_calls=4))
        rows.extend(callbacks)
        rows.append(dict(type='case_end', case=case, aliases_acquired=321,
                         aliases_released=321, events=len(callbacks), overflow=0))
    rows += [dict(type='rollback', restored=True), dict(type='final_release', references=0),
             dict(type='result', status='pass', cases=4)]
    return rows


class ObserverReport(unittest.TestCase):
    def test_absence_is_valid_negative(self):
        report = validate(observations())
        self.assertFalse(report['nested_resource_release_observed'])
        self.assertFalse(report['actual_helper_release_observed'])
        self.assertFalse(report['production_restoration_proved'])

    def test_nested_callbacks_are_reported_not_promoted(self):
        report = validate(observations(True))
        self.assertTrue(report['nested_resource_release_observed'])
        self.assertTrue(report['actual_helper_release_observed'])
        self.assertFalse(report['production_restoration_proved'])
        self.assertEqual(report['cases'][1]['resource_release_callbacks'], {'vs.release': 1})

    def test_sampling_is_not_resource_release_evidence(self):
        rows = observations()
        row = next(r for r in rows if r['type'] == 'operation')
        row['before']['sample_release'] = 1
        rows.insert(-1, dict(type='callback', case=0, phase='mrt_sample', method='release', result=5))
        next(r for r in rows if r['type'] == 'case_end')['events'] = 1
        self.assertFalse(validate(rows)['nested_resource_release_observed'])

    def test_missing_private_ids_are_s_false(self):
        rows = observations(True)
        identities = [r for r in rows if r.get('name', '').endswith('.identity')]
        self.assertEqual(len(identities), 36)
        for row in identities:
            row['hr'] = '00000001'
        self.assertEqual(validate(rows)['status'], 'pass')

    def test_s_false_remains_invalid_outside_identity(self):
        for name in ('vs.get', 'rt0.get', 'vs.release', 'effective', 'idle'):
            with self.subTest(name=name):
                rows = observations()
                next(r for r in rows if r.get('name') == name)['hr'] = '00000001'
                with self.assertRaisesRegex(ValueError, 'query failure'):
                    validate(rows)

    def test_identity_failures_are_not_missing_tag_success(self):
        for hr in ('88760866', '80004005', '8876086c'):
            with self.subTest(hr=hr):
                rows = observations()
                next(r for r in rows if r.get('name') == 'stream.identity')['hr'] = hr
                with self.assertRaisesRegex(ValueError, 'query failure'):
                    validate(rows)

    def test_refuses_corrupt_evidence(self):
        mutations = [
            ('case_end', 'aliases_released', 320), ('case_end', 'overflow', 1),
            ('case_end', 'events', 100), ('effective_reached', 'target_calls', 0),
            ('operation', 'cpu_preserved', False), ('operation', 'release', 1),
            ('rollback', 'restored', False), ('final_release', 'references', 1),
            ('reset', 'hr', '8876086c'), ('device', 'selector_bypassed', False),
        ]
        for kind, key, value in mutations:
            with self.subTest(kind=kind, key=key):
                rows = observations()
                next(r for r in rows if r['type'] == kind)[key] = value
                with self.assertRaises(ValueError):
                    validate(rows)

    def test_refuses_missing_query_and_finish(self):
        rows = observations()
        del rows[next(i for i, r in enumerate(rows) if r['type'] == 'operation')]
        with self.assertRaises(ValueError):
            validate(rows)
        with self.assertRaises(ValueError):
            validate(observations()[:-1])

    def test_refuses_mrt_change(self):
        rows = observations()
        next(r for r in rows if r['type'] == 'operation')['after']['pointers'][1] = 0
        with self.assertRaisesRegex(ValueError, 'MRT'):
            validate(rows)

    def test_refuses_idle_callback(self):
        rows = observations()
        next(r for r in rows if r.get('name') == 'idle')['release'] = 1
        with self.assertRaisesRegex(ValueError, 'idle'):
            validate(rows)

    def test_build_cannot_overwrite_frozen_candidate(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root)
            with self.assertRaises(FileExistsError):
                build(path, path / 'missing-inputs')


if __name__ == '__main__':
    unittest.main()
