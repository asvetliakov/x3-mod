#!/usr/bin/env python3
"""drift_compare.py <before> <after> [case ...]: the fireball drift law's look check (2026-11-07,
docs/verification/weapon-effects.md "Fireball drift law"). <before> / <after>: a native stage dump dir
(run_weapon_effects.py --host native --dump with X3M_WEAPON_EFFECTS_DUMP_CASES=energy_single,heavy_capital_64,
energy_fighter60_20,boom_fighter_600m and X3M_WEAPON_EFFECTS_DUMP_MS=40,120,296,696), its saved measure.py table
(<dir>/measure.txt once the raw files are gone) or that table's path. measure.py's figures per body part (-burst / -body
PFM): peak, lit pixels (10 % of the part's own peak), equivalent radius, blob count; the relative change of peak and
equivalent radius (bound 5 %) and the blob counts equal. Default case: energy_single."""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def rows(d):
    # a dump dir (measured here), its saved measure.py table (<dir>/measure.txt once the raw files are gone) or the table
    saved = Path(d) / 'measure.txt' if Path(d).is_dir() else Path(d)
    out = saved.read_text() if saved.is_file() else subprocess.run([sys.executable, str(HERE / 'measure.py'), d],
                                                                   capture_output=True, text=True, check=True).stdout
    table = {}
    for line in out.splitlines()[1:]:
        f = line.split()
        table[(f[0], int(f[1]), f[2])] = (float(f[3]), int(f[4]), float(f[5]), int(f[6]))
    return table


before, after = rows(sys.argv[1]), rows(sys.argv[2])
cases = sys.argv[3:] or ['energy_single']
worst, blobs_same = 0.0, True
print('case ms part peak_before peak_after lit_before lit_after eq_r_before eq_r_after blobs_before blobs_after '
      'peak_change eq_r_change')
for k in sorted(before):
    if k[0] not in cases or k[2] not in ('burst', 'body') or k not in after:
        continue
    b, a = before[k], after[k]
    dp = (a[0] - b[0]) / b[0] if b[0] else (0.0 if not a[0] else 1.0)
    dr = (a[2] - b[2]) / b[2] if b[2] else (0.0 if not a[2] else 1.0)
    worst = max(worst, abs(dp), abs(dr))
    blobs_same = blobs_same and a[3] == b[3]
    print(f'{k[0]} {k[1]} {k[2]} {b[0]:.4f} {a[0]:.4f} {b[1]} {a[1]} {b[2]:.1f} {a[2]:.1f} {b[3]} {a[3]} {dp:+.2%} {dr:+.2%}')
print(f'worst_relative_change {worst:.2%} bound 5% blobs_equal {int(blobs_same)} '
      f'{"PASS" if worst <= 0.05 and blobs_same else "FAIL"}')
