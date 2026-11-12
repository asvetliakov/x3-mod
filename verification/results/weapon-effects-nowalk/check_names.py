#!/usr/bin/env python3
"""The walk's removal (2026-11-12): the native fixture records' CHECK names removed and added against the base commit's
records (git show), per record (the D3D fixture, the AP and FL hook fixtures).

  python3 verification/results/weapon-effects-nowalk/check_names.py [base_commit]
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
REC = 'verification/results/native-windows/weapon-effects/'


def names(r):
    return {c[0] if isinstance(c, list) else c['name'] for c in r['checks']}


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else '89c534a1e'
    for f in ('summary.json', 'hook-summary.json', 'hook-fl-summary.json'):
        b = json.loads(subprocess.run(['git', '-C', str(ROOT), 'show', f'{base}:{REC}{f}'], capture_output=True,
                                      text=True, check=True).stdout)
        a = json.loads((ROOT / REC / f).read_text())
        nb, na = names(b), names(a)
        print(f'{f}: {len(nb)} -> {len(na)} checks, passed={a["passed"]}')
        print('  removed', sorted(nb - na))
        print('  added', sorted(na - nb))


if __name__ == '__main__':
    main()
