#!/usr/bin/env python3
"""A' only (dilated camera chain removed, 2026-09-24): the RESOLVE_BUDGET rows (D3DX dwords and instruction slots of every
embedded TAA program) of two temporal-lattice.txt reports side by side.

    python3 verification/results/aprime-only/slot_table.py BEFORE/temporal-lattice.txt AFTER/temporal-lattice.txt

BEFORE was the 5a4bbd52 tree's run_temporal_pass.py (lattice mode, bottle X3), AFTER this change's; see
docs/verification/temporal-resolve.md, "A' only: dilated chain removed"."""
import re
import sys


def rows(path):
    text = open(path).read()
    return {m.group(1): (int(m.group(2)), int(m.group(3)))
            for m in re.finditer(r'RESOLVE_BUDGET variant=(\S+) dwords=(\d+) instruction_slots=(\d+)', text)}


before, after = rows(sys.argv[1]), rows(sys.argv[2])
print(f"{'variant':34s} {'dwords':>6s} {'slots':>5s}   {'dwords':>6s} {'slots':>5s}")
for name in list(before) + [n for n in after if n not in before]:
    b = before.get(name)
    a = after.get(name)
    left = f'{b[0]:6d} {b[1]:5d}' if b else f"{'-':>6s} {'-':>5s}"
    right = f'{a[0]:6d} {a[1]:5d}' if a else f"{'removed':>12s}"
    print(f'{name:34s} {left}   {right}')
embedded = lambda r: [n for n in r if n.startswith('embedded_')]
print(f'embedded programs: {len(embedded(before))} -> {len(embedded(after))}; '
      f'embedded dwords {sum(before[n][0] for n in embedded(before))} -> {sum(after[n][0] for n in embedded(after))}')
