#!/usr/bin/env python3
"""Step C default flip (march spacing 4 default since Run 77 C2): the regrouped fog fixture records against the step C records
of the parent commit. The old default look (summary.json at spacing 2) must reappear as the s2_* set (s2.json), the old
q4 look (q4.json) as the default look (summary.json), the old far24 record as s2.json's far24; every old gate must map to
a new gate that passes. Usage: step_c_default_flip_identity.py NEW_DIR [OLD_REV]  (OLD_REV default HEAD)."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
new_dir = Path(sys.argv[1]); rev = sys.argv[2] if len(sys.argv) > 2 else 'HEAD'


def old(name):
    return json.loads(subprocess.run(['git', '-C', str(ROOT), 'show', f'{rev}:verification/results/fog-density-shader/{name}'], check=True, capture_output=True, text=True).stdout)


def new(name):
    return json.loads((new_dir / name).read_text())


def compare(a, b, path=''):
    """(equal leaves, differing leaf paths) of two JSON trees over the keys of `a`."""
    if isinstance(a, dict):
        eq, diff = 0, []
        for k in a:
            if not isinstance(b, dict) or k not in b:
                diff.append(f'{path}/{k} (missing)'); continue
            e, d = compare(a[k], b[k], f'{path}/{k}'); eq += e; diff += d
        return eq, diff
    return (1, []) if a == b else (0, [path])


os_, oq, of = old('summary.json'), old('q4.json'), old('far24.json')
ns, nq, nf, n2 = new('summary.json'), new('q4.json'), new('far24.json'), new('s2.json')
failed = False
rows = [('old default look (spacing 2) -> s2 look', os_['look_versus_host'], n2['look_versus_host']),
        ('old default repair_with_shafts.look -> s2 repair', {k: v for k, v in os_['repair_with_shafts']['look'].items() if k != 'compared'}, n2['repair_with_shafts']),
        ('old q4 look -> default look', oq['look_versus_host'], ns['look_versus_host']),
        ('old q4 repair -> default repair', oq['repair_with_shafts'], ns['repair_with_shafts']),
        ('old q4 deviation_from_scale_2 -> q4', oq['deviation_from_scale_2'], nq['deviation_from_scale_2']),
        ('old q4 depth_edges -> q4', oq['depth_edges'], nq['depth_edges']),
        ('old far24 (spacing 2) look -> s2 far24', of['look_versus_host'], n2['far24']['look_versus_host']),
        ('old far24 deviation -> s2 far24', of['deviation_from_40_bins'], n2['far24']['deviation_from_40_bins']),
        ('old far24 repair -> s2 far24', of['repair_with_shafts'], n2['far24']['repair_with_shafts']),
        ('old default versus_host (parity) -> default', os_['versus_host'], ns['versus_host']),
        ('old visibility grid (no hashes) -> default', {k: v for k, v in os_['visibility_grid'].items() if k != 'pass_off_hashes'}, ns['visibility_grid'])]
for label, a, b in rows:
    eq, diff = compare(a, b)
    print(f'{label}: {eq} equal leaves, {len(diff)} differ' + (f' {diff[:6]}' if diff else ''))
    failed |= bool(diff)
# The old scale-2 look hashes, now the s2 set's; the new default hashes are the old q4 images' (measured in this run).
print('s2 pass-off hashes equal the old default pins:', n2['pass_off_hashes']['measured'] == os_['visibility_grid']['pass_off_hashes']['expected'])
failed |= n2['pass_off_hashes']['measured'] != os_['visibility_grid']['pass_off_hashes']['expected']
# Gate mapping: every old gate has a passing successor.
rename = {'look_cases': 's2_look_cases', 'look_shaft_offset_exercised': 's2_look_shaft_offset_exercised', 'look_shadowed_coloured': 's2_look_shadowed_coloured',
          'repair_shaft_lookup': 's2_repair_shaft_lookup', 'pass_off_bit_identical': 's2_pass_off_bit_identical',
          'q4_look_cases': 'look_cases', 'q4_look_shaft_offset_exercised': 'look_shaft_offset_exercised', 'q4_look_shadowed_coloured': 'look_shadowed_coloured',
          'q4_repair_shaft_lookup': 'repair_shaft_lookup'}
rename.update({k: 's2_' + k for k in os_['gates'] if k.startswith('far24_')})
missing = [k for k in os_['gates'] if not ns['gates'].get(rename.get(k, k))]
new_only = sorted(set(ns['gates']) - {rename.get(k, k) for k in os_['gates']})
print(f"gates: old {len(os_['gates'])}, new {len(ns['gates'])} ({sum(ns['gates'].values())} pass); old gates without a passing successor: {missing}")
print(f'new gates without a predecessor: {new_only}')
failed |= bool(missing) or ns['result'] != 'PASS'
sys.exit(1 if failed else 0)
