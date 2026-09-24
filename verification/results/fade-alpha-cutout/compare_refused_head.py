#!/usr/bin/env python3
"""The -refused twin of the fade cutout script (owner off) is the pre-change behaviour: the same fixture executable run
once against the working-tree DLLs and once against DLLs built from the committed tree (HEAD before the change) must
produce byte-identical per-frame dumps (raw colour, RT1, lane RT2; presented frames; reference resolves), identical
FADE_CUTOUT lines, identical panel/hull motion_route records (pointer-free fields) and identical fade_route_frame
counters apart from the new fade_tested field. A second pair (-owner vs -refused of the new build) must agree on the
raw colour of every frame (the routed variant's oC0 is the native one).
usage: compare_refused_head.py <new-refused-dir> <head-refused-dir> [<new-owner-dir>]"""
import hashlib
import sys
from pathlib import Path


def fields(line):
    return dict(part.split('=', 1) for part in line.split() if '=' in part)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def dumps(directory):
    return {p.name: digest(p) for p in sorted(directory.iterdir())
            if p.suffix in ('.f32', '.bgra8', '.rgba16f')}


def trace(directory):
    logs = sorted((directory / 'x3-modern-captures').glob('session-*.log'))
    assert len(logs) == 1, (directory, logs)
    return logs[0].read_text(errors='replace').splitlines()


ROUTE_KEYS = ('frame', 'gate', 'routed', 'matched', 'depth', 'jittered', 'vs', 'ps', 'zwrite', 'blend', 'atest', 'mask',
              'fade_arm', 'fade_permille', 'unmatched', 'result', 'rows_hash', 'primitives')


def routes(lines):
    out = []
    for l in lines:
        if l.startswith('motion_route '):
            f = fields(l)
            if f.get('vs') == 'b0602757fce6e870':
                out.append(tuple(f.get(k) for k in ROUTE_KEYS))
    return out


def fade_frames(lines):
    return [tuple(sorted((k, v) for k, v in fields(l).items() if k != 'fade_tested'))
            for l in lines if l.startswith('fade_route_frame ')]


new, head = Path(sys.argv[1]), Path(sys.argv[2])
a, b = dumps(new), dumps(head)
assert sorted(a) == sorted(b), (sorted(set(a) ^ set(b)))
differing = sorted(k for k in a if a[k] != b[k])
cut = lambda d: [l for l in (d / 'fixture-stdout.txt').read_text().splitlines() if l.startswith(('FADE_CUTOUT', 'FADE_CUTOUT_CHECKS'))]
lines_equal = cut(new) == cut(head)
ta, tb = trace(new), trace(head)
routes_equal = routes(ta) == routes(tb)
frames_equal = fade_frames(ta) == fade_frames(tb)
print(f'dump_files={len(a)} differing={differing} fade_cutout_lines={len(cut(new))} lines_identical={lines_equal} '
      f'fade_pair_route_records={len(routes(ta))} records_identical={routes_equal} fade_route_frame_lines={len(fade_frames(ta))} '
      f'counters_identical_except_fade_tested={frames_equal}')
ok = not differing and lines_equal and routes_equal and frames_equal
if len(sys.argv) > 3:
    owner = Path(sys.argv[3])
    colour = [f'fade_route_color_{f}.f32' for f in range(12)]
    same = [digest(owner / c) == a[c] for c in colour]
    print(f'owner_vs_refused_raw_colour_identical_frames={sum(same)}/12')
    ok = ok and all(same)
print('PASS' if ok else 'FAIL')
sys.exit(0 if ok else 1)
