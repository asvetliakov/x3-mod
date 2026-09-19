#!/usr/bin/env python3
"""Reduce the `submit_phases` window rows of a session log (X3M_SUBMIT_PHASES=1).

One row per 300-frame window (src/proxy/submit_phases.cpp emit_window). The
reduction takes the median window per field and derives, per stamp pair, the
time per call and the share of the pair that is the stamps' own cost. Rows
with a missing or non-integer field are rejected, not repaired. Reads the log
line by line; never loads it whole.

    python3 tools/analysis/summarize_submit_phases.py <log> [--json]
"""
import argparse
import json
import statistics
import sys

PAIRS = ('sort', 'walk', 'technique', 'end', 'block', 'inverse_world', 'inverse_view', 'material', 'world')
HEADER = ('qpc', 'frame', 'frames', 'stamps_p50', 'stamps_p95', 'self_p50_us', 'dispatch_cost_ns')
EXTRA = ('sort_nodes_p50', 'sort_nodes_max', 'walk_misses_p50', 'walk_iterations_p50', 'walk_iterations_p95',
         'walk_sample_period', 'block_net_p50_us', 'material_net_p50_us')
HEALTH = ('block_skipped', 'reopened', 'idle', 'clock_errors', 'clock_failures', 'unmatched', 'dropped', 'early', 'foreign')
FIELDS = (HEADER + tuple(f'{p}_{s}' for p in PAIRS for s in ('calls_p50', 'p50_us', 'p95_us')) + EXTRA + HEALTH)
PREFIX = 'submit_phases '


def parse_row(line):
    """A dict of ints for one `submit_phases` row, or None when the row is malformed."""
    at = line.find(PREFIX)
    if at < 0:
        return None
    tokens = dict(token.split('=', 1) for token in line[at + len(PREFIX):].split() if '=' in token)
    try:
        row = {name: int(tokens[name]) for name in FIELDS}
    except (KeyError, ValueError):
        return None
    if any(value < 0 for value in row.values()) or not row['frames']:
        return None
    return row


def summarize(lines):
    rows, rejected = [], 0
    for line in lines:
        if PREFIX not in line:
            continue
        row = parse_row(line)
        if row is None:
            rejected += 1
        else:
            rows.append(row)
    report = {'windows': len(rows), 'rejected': rejected, 'frames': sum(r['frames'] for r in rows)}
    if not rows:
        return report
    median = {name: int(statistics.median_low(r[name] for r in rows)) for name in FIELDS if name not in ('qpc', 'frame')}
    report['median_window'] = median
    report['health'] = {name: sum(r[name] for r in rows) for name in HEALTH}
    cost = median['dispatch_cost_ns']
    pairs = {}
    for pair in PAIRS:
        calls, p50 = median[f'{pair}_calls_p50'], median[f'{pair}_p50_us']
        pairs[pair] = {'calls': calls, 'p50_us': p50, 'p95_us': median[f'{pair}_p95_us'],
                       'per_call_ns': p50 * 1000 // calls if calls else 0}
    pairs['material']['net_p50_us'] = median['material_net_p50_us']
    pairs['block']['net_p50_us'] = median['block_net_p50_us']
    pairs['sort']['nodes_p50'] = median['sort_nodes_p50']
    pairs['sort']['nodes_max'] = max(r['sort_nodes_max'] for r in rows)
    lookups = median['walk_calls_p50']
    pairs['walk'].update(misses_p50=median['walk_misses_p50'], iterations_p50=median['walk_iterations_p50'],
                         mean_walk_length=round(median['walk_iterations_p50'] / lookups, 1) if lookups else 0.0)
    report['pairs'] = pairs
    report['self_p50_us'] = median['self_p50_us']
    report['stamps_p50'] = median['stamps_p50']
    report['dispatch_cost_ns'] = cost
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log')
    parser.add_argument('--json', action='store_true')
    args = parser.parse_args()
    with open(args.log, errors='replace') as handle:
        result = summarize(handle)
    if args.json or 'pairs' not in result:
        print(json.dumps(result, indent=2))
    else:
        print(f"windows={result['windows']} frames={result['frames']} rejected={result['rejected']} "
              f"stamps_p50={result['stamps_p50']} self_p50_us={result['self_p50_us']}")
        for name, pair in result['pairs'].items():
            print(f'{name:14s} ' + ' '.join(f'{k}={v}' for k, v in pair.items()))
        print('health ' + ' '.join(f'{k}={v}' for k, v in result['health'].items()))
    sys.exit(0 if result['windows'] and not result['rejected'] else 1)
