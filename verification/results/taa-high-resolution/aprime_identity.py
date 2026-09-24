#!/usr/bin/env python3
"""A' re-baseline (docs/architecture/taa-plan-lifted-slot-cap.md step 1): the hold-off identity of a temporal fixture report
against a reference report (the ee3bbf88 temporal-pass.txt or temporal-lattice.txt). Every line of the reference is
matched against the new report as a multiset (exact text); lines of the rows A' added (REGION_HOLD*, THIN_REGION_HOLD*, HISTORY_TAPS_BASE and
their CHECK / SAMPLE lines after the REGION_HOLD_CASES marker) are skipped on the new side, and RESOLVE_BUDGET rows are
reported separately (their slots changed by construction). Prints the count of identical and differing lines and, per
row kind (the first token, plus config= / variant= / program= when present), the differing count.

usage: aprime_identity.py REFERENCE NEW [SHOW_N_REFERENCE_ONLY_LINES]
"""
import collections
import re
import sys


def kind(line):
    head = line.split(' ', 1)[0]
    m = re.search(r'\b(config|variant|program|row|mode|case|kind)=(\S+)', line)
    return head + (' ' + m.group(0) if m else '')


def rows(path):
    lines = open(path, encoding='utf-8', errors='replace').read().splitlines()
    out = []
    for line in lines:
        if line.startswith('REGION_HOLD_CASES'):
            break  # the A' block runs last in lattice mode
        out.append(line)
    return out


ref, new = rows(sys.argv[1]), rows(sys.argv[2])
budget = [l for l in new if l.startswith('RESOLVE_BUDGET')]
ref = [l for l in ref if not l.startswith('RESOLVE_BUDGET') and not l.startswith('MODULE ')]
new = [l for l in new if not l.startswith('RESOLVE_BUDGET') and not l.startswith('MODULE ') and not l.startswith('HISTORY_TAPS_BASE')]
available = collections.Counter(new)
differ, missing_lines = collections.Counter(), []
for line in ref:
    if available[line] > 0:
        available[line] -= 1
    else:
        differ[kind(line)] += 1
        missing_lines.append(line)
added = collections.Counter(kind(l) for l, n in available.items() for _ in range(n))
print('reference_lines=%d new_lines=%d identical=%d differing=%d new_only=%d' % (len(ref), len(new), len(ref) - sum(differ.values()), sum(differ.values()), sum(added.values())))
for k, v in sorted(differ.items()):
    print('reference_only %s count=%d' % (k, v))
for k, v in sorted(added.items()):
    print('new_only %s count=%d' % (k, v))
if len(sys.argv) > 3:
    for line in missing_lines[:int(sys.argv[3])]:
        print('REF', line[:400])
