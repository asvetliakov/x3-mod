#!/usr/bin/env python3
"""Reduce R7 whole-call windows. Unknown callers or health failures reject coverage.

Times are raw; self cost is a separate fixture-calibrated estimate, never a
subtraction or game-FPS measurement. Read logs as a stream.
"""
import argparse
import json
import statistics

CALLERS = ('cockpit', 'traversal', 'unknown')
HEADER = ('qpc', 'frame', 'frames', 'valid_frames', 'invalid_frames', 'stamps_p50', 'stamps_p95', 'self_p50_us',
          'self_p95_us', 'dispatch_cost_ns', 'self_calibrated')
HEALTH = ('nested', 'overflow', 'mismatch', 'unmatched', 'clock_failures',
          'clock_reversal', 'reentry', 'mode_refused', 'dropped', 'early', 'foreign')
FIELDS = HEADER + tuple(f'{c}_{f}' for c in CALLERS for f in
                        ('entries', 'calls', 'ticks', 'calls_p50', 'calls_p95', 'p50_us', 'p95_us')) + HEALTH
PREFIX = 'light_phases '


def parse_row(line):
    at = line.find(PREFIX)
    if at < 0:
        return None
    pairs = [word.split('=', 1) for word in line[at + len(PREFIX):].split() if '=' in word]
    if len(dict(pairs)) != len(pairs):
        return None
    try:
        fields = dict(pairs)
        row = {key: int(fields[key]) for key in FIELDS}
    except (KeyError, ValueError):
        return None
    if any(value < 0 for value in row.values()) or row['frames'] != 300:
        return None
    if row['valid_frames'] + row['invalid_frames'] != row['frames']:
        return None
    if row['self_calibrated'] != int(row['dispatch_cost_ns'] > 0):
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
    result = {'windows': len(rows), 'rejected': rejected, 'frames': sum(r['frames'] for r in rows)}
    if rows:
        result['median_window'] = {key: int(statistics.median_low(r[key] for r in rows))
                                   for key in FIELDS if key not in ('qpc', 'frame')}
        result['health'] = {key: sum(r[key] for r in rows) for key in HEALTH}
        result['complete_coverage'] = not rejected and all(
            not any(r[key] for key in HEALTH) and not r['unknown_entries'] and not r['invalid_frames']
            and all(r[f'{c}_entries'] == r[f'{c}_calls'] for c in CALLERS) for r in rows)
        result['self_calibrated'] = all(r['self_calibrated'] for r in rows)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log')
    args = parser.parse_args()
    with open(args.log, errors='replace') as handle:
        result = summarize(handle)
    print(json.dumps(result, indent=2))
    raise SystemExit(not result['windows'] or bool(result['rejected']))
