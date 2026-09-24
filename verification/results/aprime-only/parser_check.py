#!/usr/bin/env python3
"""A' only (2026-09-24): the Run 78 A / Run 79 A triage scripts still parse after the change.
1. Every script of verification/results/run299-303-run79a and run295-298-run78a that takes run numbers is run on run 299
   (/tmp/x3-bottleX3-run299, read-only) and its run299 lines are compared with the committed *_out.txt ones where present.
2. The two rows this change reformats (motion_output_taa without region_hold, motion_output_taa_history_taps with
   mask_targets) are written into a synthetic session log beside run299's first motion_output_frame rows
   (/tmp/x3-bottleX3-run99299, removed afterwards) and taa_frames.py, the one script that reads either row, is run on it.
No Wine, no game. usage: python3 verification/results/aprime-only/parser_check.py"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DIRS = {'run299-303-run79a': ('abnormal_rows', 'bolt_rows', 'dt_windows', 'exit_rows', 'gpu_taa', 'pan_windows', 'taa_frames'),
        'run295-298-run78a': ('abnormal_rows', 'bolt_rows', 'exit_rows')}
SOURCE = Path('/tmp/x3-bottleX3-run299')
failures = 0


def run(folder, script, *args):
    done = subprocess.run([sys.executable, f'{script}.py', *args], cwd=ROOT / 'verification/results' / folder, capture_output=True, text=True, timeout=900)
    return done.returncode, done.stdout, done.stderr


for folder, scripts in DIRS.items():
    for script in scripts:
        code, out, err = run(folder, script, '299')
        committed = ROOT / 'verification/results' / folder / f'{script}_out.txt'
        mine = [l for l in out.splitlines() if 'run299' in l or l.startswith('299')]
        theirs = [l for l in committed.read_text().splitlines() if 'run299' in l or l.startswith('299')] if committed.exists() else []
        verdict = 'no_run299_reference' if not theirs else ('identical' if mine == theirs else 'differs')
        failures += code != 0 or verdict == 'differs'
        print(f'{folder}/{script}.py 299: exit={code} lines={len(out.splitlines())} run299_lines={len(mine)} committed_run299_lines={len(theirs)} {verdict}'
              + (f' stderr={err.strip()[-200:]!r}' if code else ''))

# The new row formats, in a synthetic run.
log = sorted(SOURCE.glob('session-*.log'))[0]
target = Path('/tmp/x3-bottleX3-run99299')
target.mkdir(exist_ok=True)
try:
    frames = 0
    with open(log, errors='replace') as source, open(target / 'session-synthetic.log', 'w') as out:
        for line in source:
            if line.startswith('motion_output_taa '):
                line = re.sub(r' region_hold=\d+', '', line)
            elif line.startswith('motion_output_taa_history_taps '):
                line = line.rstrip('\n') + ' mask_targets=1\n'
            elif line.startswith('motion_output_frame'):
                frames += 1
                if frames > 5000:
                    break
            out.write(line)
    (target / 'launcher-stderr.log').write_text((SOURCE / 'launcher-stderr.log').read_text(errors='replace'))
    written = (target / 'session-synthetic.log').read_text(errors='replace')
    taa_rows = [l for l in written.splitlines() if l.startswith('motion_output_taa ') or l.startswith('motion_output_taa_history_taps ')]
    print(f'synthetic: frames={min(frames, 5000)} config_rows={len(taa_rows)} region_hold_in_taa_row={any("region_hold" in l for l in taa_rows if l.startswith("motion_output_taa "))} '
          f'mask_targets_rows={sum("mask_targets=1" in l for l in taa_rows)}')
    # The one script that reads a reformatted row (taa_frames.py keeps the history-taps row as text; the others read frame,
    # gpu-sync, abnormal, exit and bolt rows this change does not touch).
    for folder, scripts in {'run299-303-run79a': ('taa_frames',)}.items():
        for script in scripts:
            code, out, err = run(folder, script, '99299')
            failures += code != 0
            carried = 'mask_targets=1' in out
            print(f'{folder}/{script}.py 99299 (new rows): exit={code} lines={len(out.splitlines())} history_taps_row_carried={carried}'
                  + (f' stderr={err.strip()[-200:]!r}' if code else ''))
finally:
    shutil.rmtree(target)
print(f'RESULT {"PASS" if not failures else "FAIL"} failures={failures}')
sys.exit(1 if failures else 0)
