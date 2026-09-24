#!/usr/bin/env python3
"""S3 (5-tap bilinear Catmull-Rom history) against the committed temporal reports, row by row.

Run right after `run_temporal_pass.py` rewrote verification/results/bottle-X3/temporal-{pass,lattice}.txt, before they are
committed: compares them with the committed (HEAD, or --against REV) reports. A row is keyed by its tag (first token), its
label (SAMPLE / CHECK rows: the text before `actual=` or the verdict), its non-numeric key=value fields and its occurrence
index under that key. Prints, per report and tag, the rows compared / identical / differing and those only in one report;
then every differing row with the numeric fields that moved (old -> new, delta). CPU timing rows (LINE_TIMING*) are counted,
never listed. Exit status 0 when every CHECK row passes in the new reports, whatever moved.
"""
import argparse
from collections import Counter, OrderedDict
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RESULTS = ROOT / 'verification/results/bottle-X3'
FIELD = re.compile(r'(\w+)=(\S+)')
NUMBER = re.compile(r'^-?(\d+\.?\d*|\.\d+)(e[-+]?\d+)?$', re.I)


def committed(rev, name):
    return subprocess.run(['git', '-C', str(ROOT), 'show', f'{rev}:verification/results/bottle-X3/{name}'], check=True,
                          capture_output=True).stdout.decode()


def rows(text):
    keyed, seen = OrderedDict(), Counter()
    for line in text.splitlines():
        if not line.strip():
            continue
        tag = line.split()[0]
        if tag in ('SAMPLE', 'CHECK'):
            head = line[len(tag):].split(' actual=')[0].rsplit(' PASS', 1)[0].rsplit(' FAIL', 1)[0].strip()
            label, fields = head, dict(FIELD.findall(line[len(tag) + 1 + len(head):]))
        else:
            label, fields = '', dict(FIELD.findall(line))
        # A comma list of numbers (shift_px=0.064,0.000) is values k.0, k.1, not part of the key.
        numeric = lambda v: all(NUMBER.match(part) for part in v.split(','))
        ident = tuple(sorted((k, v) for k, v in fields.items() if not numeric(v)))
        values = {}
        for k, v in fields.items():
            if numeric(v):
                parts = v.split(',')
                values.update({k: float(v)} if len(parts) == 1 else {f'{k}.{i}': float(x) for i, x in enumerate(parts)})
        base = (tag, label, ident)
        keyed[base + (seen[base],)] = (line, values)
        seen[base] += 1
    return keyed


def compare(name, old_text, new_text, out):
    old, new = rows(old_text), rows(new_text)
    per_tag = OrderedDict()
    details = []
    for key in list(old) + [k for k in new if k not in old]:
        tag = key[0]
        stats = per_tag.setdefault(tag, Counter())
        if key not in new:
            stats['only_old'] += 1
            continue
        if key not in old:
            stats['only_new'] += 1
            continue
        (old_line, old_values), (new_line, new_values) = old[key], new[key]
        stats['compared'] += 1
        if old_line == new_line:
            stats['identical'] += 1
            continue
        stats['differing'] += 1
        if tag.startswith('LINE_TIMING'):
            continue
        moved = [f'{k} {old_values[k]:g}->{new_values[k]:g} ({new_values[k] - old_values[k]:+.6g})'
                 for k in new_values if k in old_values and old_values[k] != new_values[k]]
        label = f' {key[1]}' if key[1] else ''
        ident = ' '.join(f'{k}={v}' for k, v in key[2])
        details.append(f'  {tag}{label} {ident} #{key[3]}: ' + ('; '.join(moved) if moved else 'text changed'))
    print(f'== {name}: rows old={len(old)} new={len(new)}', file=out)
    for tag, stats in per_tag.items():
        if stats['differing'] or stats['only_old'] or stats['only_new']:
            print(f'{tag}: compared={stats["compared"]} identical={stats["identical"]} differing={stats["differing"]} '
                  f'only_old={stats["only_old"]} only_new={stats["only_new"]}', file=out)
    unchanged = [tag for tag, stats in per_tag.items() if not (stats['differing'] or stats['only_old'] or stats['only_new'])]
    print(f'unchanged tags ({len(unchanged)}): {" ".join(unchanged)}', file=out)
    print(f'-- {name}: differing rows (timing rows omitted): {len(details)}', file=out)
    for line in details:
        print(line, file=out)
    return all(' FAIL' not in line for line, _ in new.values() if line.startswith('CHECK '))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--against', default='HEAD')
    args = parser.parse_args()
    ok = True
    for name in ('temporal-pass.txt', 'temporal-lattice.txt'):
        ok = compare(name, committed(args.against, name), (RESULTS / name).read_text(), sys.stdout) and ok
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
