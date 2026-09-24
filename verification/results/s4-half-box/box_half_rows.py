#!/usr/bin/env python3
"""S4 half-resolution box (docs/verification/temporal-resolve.md, "S4 half-resolution box"): the BOX_HALF_* rows and the
RESOLVE_BUDGET rows of the box programs from a run_temporal_pass.py lattice report, compacted to the fields the ledger cites.

    python3 verification/results/s4-half-box/box_half_rows.py [verification/results/bottle-X3/temporal-lattice.txt]"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'verification/results/bottle-X3/temporal-lattice.txt'
text = path.read_text()
fields = lambda line: dict(re.findall(r'(\w+)=(\S+)', line))
for m in re.finditer(r'RESOLVE_BUDGET variant=(embedded_thin_box\S*) dwords=(\d+) instruction_slots=(\d+)', text):
    print(f'slots {m.group(1):34s} dwords={m.group(2):>5s} slots={m.group(3):>4s}')
for line in text.splitlines():
    if line.startswith('BOX_HALF_CONTAINMENT '):
        f = fields(line)
        print('containment {scene:32s} frames={frames:>3s} compared_px={compared_px:>6s} violations={violations} half_only_px={half_only_px:>4s} '
              'half_only_violations={half_only_violations} nonfinite_skipped={nonfinite_skipped:>3s} output_identical={output_identical}'.format(**f))
    elif line.startswith('BOX_HALF_ORACLE '):
        f = fields(line)
        print('oracle      {scene:32s} oracle_error={oracle_error} age_oracle_error={age_oracle_error} tests_mask_error={tests_mask_error}'.format(**f))
    elif line.startswith(('BOX_HALF_STATE ', 'BOX_HALF_RIPPLE ', 'BOX_HALF_STALE ', 'BOX_HALF_PAN_STOP ', 'BOX_HALF_SENTINEL ', 'BOX_HALF_EMITTER ', 'BOX_HALF_TIMING ')):
        print(line)
for pattern in (r'REGION_HOLD_BASE numerical=\d+ state_restorations=\d+', r'RESULT \S+ numerical=\d+ state_restorations=\d+ lattice=1'):
    m = re.search(pattern, text)
    print(m.group(0) if m else f'missing: {pattern}')
