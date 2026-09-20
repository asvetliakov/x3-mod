#!/usr/bin/env python3
"""Validate real-device observer JSONL; callback absence is a valid negative result."""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path

BASE_OPERATIONS = [
    'vs.get', 'vs.size', 'vs.function', 'vs.release',
    'ps.get', 'ps.size', 'ps.function', 'ps.release',
    'decl.get', 'decl.description', 'decl.release',
    'stream.get', 'stream.identity', 'stream.frequency', 'stream.description', 'stream.release',
    'indices.get', 'indices.identity', 'indices.description', 'indices.release',
    *[f'{resource}.{action}' for resource in ('rt0', 'rt1', 'rt2', 'depth')
      for action in ('get', 'identity', 'description', 'container', 'release')],
    'container.identity', 'rt0.container_release',
    *[f'tex{stage}.{action}' for stage in (0, 3) for action in ('get', 'identity', 'description', 'release')],
    'effective', 'idle', 'idle',
]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(rows):
    require(rows and rows[-1] == dict(type='result', status='pass', cases=4), 'missing successful completion')
    def one(kind, selected=rows):
        found = [r for r in selected if r['type'] == kind]
        require(len(found) == 1, f'expected one {kind}')
        return found[0]
    device = one('device')
    require(device.get('schema') == 1 and device.get('real_d3d9') is True and
            device.get('selector_bypassed') is True and device.get('draws') == 0 and
            device.get('production_release_hook') is False and device.get('mrt', 0) >= 3,
            'device/scope contract')
    require(one('hook_control') == dict(type='hook_control', add=1, release=1), 'hook control')
    require(one('reset').get('hr') == '00000000', 'Reset failed')
    require(one('rollback').get('restored') is True, 'hook rollback failed')
    require(one('final_release').get('references') == 0, 'device reference leak')
    require({r['case'] for r in rows if 'case' in r} == set(range(4)), 'case coverage')
    output = []
    for case in range(4):
        selected = [r for r in rows if r.get('case') == case]
        begin, end = one('case', selected), one('case_end', selected)
        require(begin.get('cycle') == case // 2 and begin.get('ownership') == ('dropped_bound' if case % 2 else 'held'), 'ownership/cycle matrix')
        operations = [r for r in selected if r['type'] == 'operation']
        require(Counter(r['name'] for r in operations) == Counter(BASE_OPERATIONS), 'query coverage')
        require(one('effective_reached', selected).get('target_calls') == min(device['mrt'], 4), 'actual helper did not reach native MRT queries')
        expected_pointers = operations[0]['before']['pointers']
        samples_add = samples_release = 0
        for row in operations:
            # query_resource_id maps a missing private tag to S_FALSE; raw
            # D3DERR_NOTFOUND is not this helper's successful return contract.
            allowed_hr = {'00000000', '00000001'} if row['name'].endswith('.identity') else {'00000000'}
            require(row['hr'] in allowed_hr and row.get('cpu_preserved') is True, 'query failure or CPU envelope')
            for count in ('add', 'release'):
                require(type(row[count]) is int and row[count] >= 0, 'invalid callback count')
            if row['name'] == 'idle':
                require(row['add'] == row['release'] == 0, 'callback while idle')
            for label in ('before', 'after'):
                sample = row[label]
                require(sample['hr'] == ['00000000'] * 3 and len(sample['pointers']) == 3 and
                        all(type(p) is int and p > 0 for p in sample['pointers']) and
                        sample['pointers'] == expected_pointers, 'MRT changed or unavailable')
                for count in ('sample_add', 'sample_release'):
                    require(type(sample[count]) is int and sample[count] >= 0, 'invalid sample callback count')
                samples_add += sample['sample_add']
                samples_release += sample['sample_release']
        containers = [r for r in selected if r['type'] == 'container']
        require(len(containers) == 4 and {r['slot'] for r in containers} == set(range(4)), 'container coverage')
        for row in containers:
            require((row['slot'] == 0 and row['present'] is True and row['hr'] == '00000000') or
                    (row['slot'] != 0 and row['present'] is False and row['hr'] == '80004002'), 'surface container contract')
        callbacks = [r for r in selected if r['type'] == 'callback']
        require(end.get('overflow') == 0 and end.get('events') == len(callbacks), 'callback record loss')
        require(type(end.get('aliases_acquired')) is int and end['aliases_acquired'] > 0 and
                end['aliases_acquired'] == end.get('aliases_released'), 'explicit getter alias imbalance')
        counts = Counter((r['phase'], r['method']) for r in callbacks)
        require(all(r['method'] in ('addref', 'release') and type(r['result']) is int and r['result'] > 0 for r in callbacks), 'invalid callback or prematurely destroyed device')
        require(counts['mrt_sample', 'addref'] == samples_add and counts['mrt_sample', 'release'] == samples_release, 'sampling callbacks not separated')
        for phase in {r['name'] for r in operations}:
            for key, method in (('add', 'addref'), ('release', 'release')):
                require(counts[phase, method] == sum(r[key] for r in operations if r['name'] == phase), 'callback attribution mismatch')
        release_callbacks = {r['name']: r['release'] for r in operations
                             if (r['name'].endswith('.release') or r['name'].endswith('.container_release')) and r['release']}
        effective = next(r for r in operations if r['name'] == 'effective')
        output.append(dict(case=case, ownership=begin['ownership'], cycle=begin['cycle'],
                           resource_release_callbacks=release_callbacks,
                           effective_add=effective['add'], effective_release=effective['release'],
                           sampling_release=samples_release, explicit_aliases=end['aliases_acquired']))
    return dict(status='pass', cases=output, reset=True, hook_restored=True,
                nested_resource_release_observed=any(r['resource_release_callbacks'] for r in output),
                actual_helper_release_observed=any(r['effective_release'] for r in output),
                production_restoration_proved=False, actual_gpu_writes_tested=False,
                native_windows_runtime_verified=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('observations', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    require(args.observations.stat().st_size <= 4 * 1024 * 1024, 'oversize observations')
    rows = [json.loads(line) for line in args.observations.read_text().splitlines() if line.strip()]
    result = validate(rows)
    result['observations_sha256'] = hashlib.sha256(args.observations.read_bytes()).hexdigest()
    with args.output.open('x') as output:
        json.dump(result, output, indent=2, allow_nan=False)
        output.write('\n')
    print(json.dumps(result, allow_nan=False))


if __name__ == '__main__':
    main()
