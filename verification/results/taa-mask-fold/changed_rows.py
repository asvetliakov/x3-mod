#!/usr/bin/env python3
"""The kept lattice rows whose values changed with the mask fold (docs/verification/temporal-resolve.md, "Mask fold"):
old / new pairs of THIN_REGION_CAMERA_FORWARD, BOX_HALF_ORACLE, BOX_HALF_CONTAINMENT, THIN_REGION_HOLD_FADE_OWNER and the
main report's MOTE_STREAK, against git HEAD (or the revision given). Run from the repository root."""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
REVISION = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
for name, prefixes in (('temporal-lattice.txt', ('THIN_REGION_CAMERA_FORWARD ', 'BOX_HALF_ORACLE ', 'BOX_HALF_CONTAINMENT ', 'THIN_REGION_HOLD_FADE_OWNER ')),
                       ('temporal-pass.txt', ('MOTE_STREAK ',))):
    path = f'verification/results/bottle-X3/{name}'
    old = subprocess.run(['git', 'show', f'{REVISION}:{path}'], capture_output=True, text=True, cwd=ROOT, check=True).stdout.splitlines()
    new = (ROOT / path).read_text().splitlines()
    for prefix in prefixes:
        a = [l for l in old if l.startswith(prefix)]
        b = [l for l in new if l.startswith(prefix)]
        keyed = lambda rows: {' '.join(t for t in r.split() if t.startswith(('scene=', 'sky=', 'value=', 'dz=', 'frames='))) or r.split()[0]: r for r in rows}
        ka, kb = keyed(a), keyed(b)
        for key in ka:
            if key in kb and ka[key] != kb[key]:
                fa = dict(t.split('=', 1) for t in ka[key].split()[1:] if '=' in t)
                fb = dict(t.split('=', 1) for t in kb[key].split()[1:] if '=' in t)
                print(prefix.strip(), key, ' '.join(f'{k}={fa.get(k)}->{fb.get(k)}' for k in dict.fromkeys(list(fa) + list(fb)) if fa.get(k) != fb.get(k)))
            elif key not in kb:
                print(prefix.strip(), key, 'retired')
