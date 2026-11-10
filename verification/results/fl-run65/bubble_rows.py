#!/usr/bin/env python3
"""FL run 65 (user flight 2026-11-10, Argon Titan): the shield bubble's hull-walk figures from x3m.log.

Streams the log (it is over 1 GB; only lines starting with `weapon_event ` or `weapon_effects_frame ` are parsed) and
prints, as one JSON document:

* the frame-row totals (bubble_walks, bubble_walk_failures, held, deferred, fallback, bubbles) over every
  `weapon_effects_frame` row;
* the frame transition: each frame row closes a period of frames (previous row's frame, this row's frame]; the
  shield-hit owners (weapon_event role=shield_hit stage=1) seen in a period are attributed to it, and for every owner
  the periods with its hits, the failing periods with its hits and the failing periods where it was the only owner;
* the per-owner pos_r split: hits, radius, patch tier, the largest |pos_x| / R, |pos_y| / R, |pos_z| / R and pos_r.

A frame number below the previous row's starts a new session (the periods do not span it).

    python3 verification/results/fl-run65/bubble_rows.py [--log /mnt/c/X3FL/x3m.log] [--owners 14]
"""
import argparse
import json
import re

FIELD = re.compile(r'(\w+)=(\S+)')


def fields(line):
    return dict(FIELD.findall(line))


def scan(path):
    totals = dict(rows=0, bubbles=0, bubble_fallback=0, bubble_walks=0, bubble_walk_failures=0, bubble_held=0,
                  bubble_deferred=0, bubble_clamped=0)
    owners = {}  # owner -> figures
    period = {}  # owner -> hits in the open period
    periods = dict(total=0, with_hits=0, failing=0, failing_with_hits=0)
    sessions = 1
    last_frame = -1
    with open(path, 'rb') as log:
        for raw in log:
            if raw.startswith(b'weapon_event '):
                f = fields(raw.decode('ascii', 'replace'))
                if f.get('role') != 'shield_hit' or f.get('stage') != '1':
                    continue
                frame = int(f['frame'])
                if frame < last_frame:  # a new session before its first frame row
                    period.clear()
                    sessions += 1
                    last_frame = -1
                owner, radius = f['owner'], int(f['radius'])
                pos = [int(v) for v in f['pos'].split(',')]
                o = owners.setdefault(owner, dict(radius=radius, tier=f.get('patch_tier'), hits=0, x=0.0, y=0.0,
                                                  z=0.0, pos_r=0.0, periods=0, failing=0, sole_failing=0))
                o['hits'] += 1
                if radius > 0:
                    for k, v in zip('xyz', pos):
                        o[k] = max(o[k], abs(v) / radius)
                o['pos_r'] = max(o['pos_r'], float(f.get('pos_r', 0)))
                period[owner] = period.get(owner, 0) + 1
            elif raw.startswith(b'weapon_effects_frame '):
                f = fields(raw.decode('ascii', 'replace'))
                frame = int(f['frame'])
                if frame < last_frame:
                    sessions += 1
                last_frame = frame
                totals['rows'] += 1
                for k in totals:
                    if k != 'rows':
                        totals[k] += int(f.get(k, 0))
                failures = int(f.get('bubble_walk_failures', 0))
                periods['total'] += 1
                periods['with_hits'] += bool(period)
                periods['failing'] += failures > 0
                periods['failing_with_hits'] += failures > 0 and bool(period)
                for owner in period:
                    owners[owner]['periods'] += 1
                    owners[owner]['failing'] += failures > 0
                    owners[owner]['sole_failing'] += failures > 0 and len(period) == 1
                period.clear()
    return totals, periods, owners, sessions


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--log', default='/mnt/c/X3FL/x3m.log')
    parser.add_argument('--owners', type=int, default=14, help='owners listed, by hits')
    args = parser.parse_args()
    totals, periods, owners, sessions = scan(args.log)
    failing_owners = sorted(o for o, v in owners.items() if v['failing'])
    listed = sorted(owners.items(), key=lambda kv: -kv[1]['hits'])[:args.owners]
    out = dict(log=args.log, owners_listed=args.owners, sessions=sessions, totals=totals, periods=periods, owners=len(owners),
               owners_in_failing_periods=len(failing_owners),
               owners_never_without_failure=[o for o in failing_owners if owners[o]['failing'] == owners[o]['periods']],
               by_owner={o: {k: round(v, 3) if isinstance(v, float) else v for k, v in d.items()} for o, d in listed})
    print(json.dumps(out, indent=1))


if __name__ == '__main__':
    main()
