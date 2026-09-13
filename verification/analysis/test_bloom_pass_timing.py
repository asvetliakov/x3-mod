"""Strict host parser controls for the retained-CSO BloomPass timing runner."""
from pathlib import Path
import json
import tempfile
import unittest

import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
import run_bloom_pass_timing as timing


OLD = 'Z:/tmp/x3-bloom-authored-qualified/baseline'
NEW = 'Z:/tmp/x3-bloom-authored-qualified/new'


def valid_log():
    frequency = 1_000_000
    lines = [f'BUNDLE label=old directory={OLD}', f'BUNDLE label=new directory={NEW}',
             f'CAPS pixel_shader_version=ffff0300 max_ps30_instruction_slots=512 qpc_frequency={frequency}']
    lines += [f'CREATE_PS metric={metric} bundle={bundle} accepted=1 attach_hr=00000000 programs_hr=00000000'
              for _dimension in timing.DIMENSIONS for metric in ('full', 'extract')
              for bundle in ('old', 'new')]
    samples = {}
    for width, height in timing.DIMENSIONS:
        for metric_index, metric in enumerate(timing.METRICS):
            for pair in range(timing.PAIRS):
                old_ticks = 1000 + width % 10 + metric_index * 100 + pair * 10
                new_ticks = old_ticks + 100
                samples[metric, width, height, pair] = old_ticks, new_ticks
                lines.append(f'SAMPLE metric={metric} width={width} height={height} pair={pair} '
                             f'order={"old-new" if pair % 2 == 0 else "new-old"} '
                             f'old_ticks={old_ticks} old_ms={old_ticks/1000:.6f} '
                             f'new_ticks={new_ticks} new_ms={new_ticks/1000:.6f}')
            old_values = sorted(samples[metric, width, height, pair][0]
                                for pair in range(timing.PAIRS))
            new_values = sorted(samples[metric, width, height, pair][1]
                                for pair in range(timing.PAIRS))
            old_median, new_median = old_values[2], new_values[2]
            lines.append(f'SUMMARY metric={metric} width={width} height={height} '
                         f'warmup_pairs=3 measured_pairs=5 old_median_ms={old_median/1000:.6f} '
                         f'new_median_ms={new_median/1000:.6f} ratio={new_median/old_median:.6f}')
    lines.append('RESULT PASS dimensions=2 metrics=2 warmup_pairs=3 measured_pairs=5')
    return lines


class TimingAcceptanceTests(unittest.TestCase):
    def test_complete_log_recomputes_all_measurements(self):
        result = timing.parse_log('\n'.join(valid_log()), OLD, NEW)
        self.assertEqual(result['qpc_frequency'], 1_000_000)
        self.assertEqual(result['max_ps30_instruction_slots'], 512)
        self.assertEqual(result['create_ps_records'], 8)
        self.assertEqual(len(result['samples']), 20)
        self.assertEqual(len(result['summaries']), 4)

    def test_rejects_missing_duplicate_extra_and_order_tampering(self):
        original = valid_log()
        variants = [
            original[:-1],
            original + [original[-1]],
            original + ['unexpected'],
            [line for line in original if 'pair=4 order=old-new' not in line],
            original[:3] + [original[3]] + original[3:],
            [line.replace('pair=1 order=new-old', 'pair=1 order=old-new') for line in original],
            [line for line in original if not line.startswith('CREATE_PS metric=extract bundle=new')],
        ]
        for variant in variants:
            with self.subTest(lines=len(variant)), self.assertRaises(ValueError):
                timing.parse_log('\n'.join(variant), OLD, NEW)

    def test_rejects_nonpositive_nonfinite_and_inconsistent_arithmetic(self):
        original = valid_log()
        replacements = [
            ('old_ticks=1000 old_ms=1.000000', 'old_ticks=0 old_ms=0.000000'),
            ('old_ms=1.000000', 'old_ms=nan'),
            ('new_ms=1.100000', 'new_ms=9.100000'),
            ('old_median_ms=1.020000', 'old_median_ms=2.020000'),
            ('ratio=1.098039', 'ratio=-0.000000'),
            ('qpc_frequency=1000000', 'qpc_frequency=0'),
        ]
        for old, new in replacements:
            changed = [line.replace(old, new) for line in original]
            self.assertNotEqual(changed, original)
            with self.subTest(replacement=new), self.assertRaises(ValueError):
                timing.parse_log('\n'.join(changed), OLD, NEW)

    def test_artifact_inventory_requires_exactly_twelve_csos_per_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); executable = root / 'timing.exe'; executable.write_bytes(b'exe')
            for label in ('baseline', 'new'):
                target = root / label; target.mkdir()
                for name in timing.bloom_fixture.NAMES: (target / (name + '.cso')).write_bytes(name.encode())
            inventory = {str(path.resolve()): timing.filtering.digest(path)
                         for label in ('baseline', 'new') for path in (root / label).glob('*.cso')}
            record = dict(passed=True, inputs_stable=True, csos=inventory,
                          kernels=[dict(name=name[:-3]) for name in timing.bloom_fixture.NAMES[1:10]])
            record_path = root / 'artifacts.json'
            record_path.write_text(json.dumps(record))
            paths = timing.artifact_inputs(executable, root)
            self.assertEqual(len(paths), 26)
            self.assertIn(record_path.resolve(), paths)
            for fault in ('failed', 'unstable', 'promoted', 'duplicate', 'stale_hash'):
                changed = json.loads(json.dumps(record))
                if fault == 'failed': changed['passed'] = False
                if fault == 'unstable': changed['inputs_stable'] = False
                if fault == 'promoted': changed['promotion'] = {}
                if fault == 'duplicate': changed['kernels'][1] = changed['kernels'][0]
                if fault == 'stale_hash': changed['csos'][next(iter(inventory))] = 'bad'
                record_path.write_text(json.dumps(changed))
                with self.subTest(fault=fault), self.assertRaises(ValueError):
                    timing.artifact_inputs(executable, root)
            record_path.write_text(json.dumps(record))
            (root / 'new' / 'extra.cso').write_bytes(b'extra')
            with self.assertRaises(ValueError): timing.artifact_inputs(executable, root)


if __name__ == '__main__':
    unittest.main()
