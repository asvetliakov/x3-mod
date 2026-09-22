#!/usr/bin/env python3
"""Exit reset (docs/architecture/seta-sky-hull-share-decay.md): the ledger's evidence rows from the fixture outputs.

Usage: compare.py <results-dir> [<baseline-git-ref>]  (default ref HEAD; the baseline is the committed
verification/results/bottle-X3/temporal-{pass,lattice}.txt of that ref, read with `git show`).
Prints: the SETA_EXIT rows, every RESOLVE_BUDGET row (before / after), the LOOP_TIMING_SUMMARY and
LINE_TIMING_CAMERA rows (before / after), and the whole-line diff of both fixture reports against the
baseline with the budget and timing lines set aside, so the off-path bit-identity is one number
(other_removed / other_added) and the SAMPLE-line identity another. Only the fields named are read.
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
VOLATILE = ('RESOLVE_BUDGET', 'LOOP_TIMING', 'LINE_TIMING', 'RESULT PASS', 'LATTICE_BASE', 'FLICKER_BASE', 'LINE_BASE', 'FAR_BASE', 'CHECK ', 'SETA_EXIT')


def baseline(ref, name):
    return subprocess.run(['git', 'show', f'{ref}:verification/results/bottle-X3/{name}'], capture_output=True, text=True, check=True, cwd=ROOT).stdout.splitlines()


def rows(lines, prefix):
    return [line for line in lines if line.startswith(prefix)]


def main():
    results = Path(sys.argv[1])
    ref = sys.argv[2] if len(sys.argv) > 2 else 'HEAD'
    for name in ('temporal-pass.txt', 'temporal-lattice.txt'):
        new = (results / name).read_text().splitlines()
        old = baseline(ref, name)
        print(f'== {name}: baseline {len(old)} lines, now {len(new)} lines')
        for prefix in ('RESOLVE_BUDGET variant=', 'LOOP_TIMING_SUMMARY', 'LINE_TIMING_CAMERA ', 'RESULT PASS'):
            for tag, lines in (('before', old), ('after', new)):
                for line in rows(lines, prefix):
                    print(f'{tag}: {line}')
        old_stable = [line for line in old if not line.startswith(VOLATILE)]
        new_stable = [line for line in new if not line.startswith(VOLATILE)]
        old_set, new_set = set(old_stable), set(new_stable)
        removed = [line for line in old_stable if line not in new_set]
        added = [line for line in new_stable if line not in old_set]
        old_samples = rows(old, 'SAMPLE ')
        new_samples = set(rows(new, 'SAMPLE '))
        missing = [line for line in old_samples if line not in new_samples]
        print(f'{name}: stable lines removed={len(removed)} added={len(added)}; baseline SAMPLE lines {len(old_samples)}, of them missing now {len(missing)}; SAMPLE lines now {len(new_samples)}')
        for line in removed[:20]:
            print('  removed:', line[:200])
        for line in added[:40]:
            print('  added:', line[:200])
        for line in rows(new, 'SETA_EXIT '):
            print(line)
    budget = {re.search(r'variant=(\S+)', line)[1]: int(re.search(r'instruction_slots=(\d+)', line)[1])
              for line in rows((results / 'temporal-lattice.txt').read_text().splitlines(), 'RESOLVE_BUDGET variant=')}
    old_budget = {re.search(r'variant=(\S+)', line)[1]: int(re.search(r'instruction_slots=(\d+)', line)[1])
                  for line in rows(baseline(ref, 'temporal-lattice.txt'), 'RESOLVE_BUDGET variant=')}
    print('slots (before -> after):', ', '.join(f'{k} {old_budget.get(k)} -> {v}' for k, v in budget.items()))


if __name__ == '__main__':
    main()
