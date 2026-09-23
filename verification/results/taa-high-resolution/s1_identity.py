#!/usr/bin/env python3
"""S1 (depth-copy fold) identity against the committed temporal reports.

Run right after `run_temporal_pass.py` rewrote verification/results/bottle-X3/temporal-{pass,lattice}.txt and before
they are restored: compares them with the committed (HEAD) reports. temporal-pass.txt must be byte-identical; the
lattice report must be identical line for line except the CPU timing rows (LINE_TIMING*), the instruction-slot rows
(RESOLVE_BUDGET and their CHECK rows; the slot rows are stale in the committed report since ba403af8) and the rows the
fold case appends (DEPTH_FOLD*, its CHECK rows, RESULT). Prints the differing counts, the four-channel-lane flight rows (the fold path with the lane term
on), the DEPTH_FOLD rows, the mask programs' slot rows and the lane timing row before / after.
"""
import difflib
import hashlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
RESULTS = ROOT / 'verification/results/bottle-X3'


def committed(name):
    return subprocess.run(['git', '-C', str(ROOT), 'show', f'HEAD:verification/results/bottle-X3/{name}'], check=True,
                          capture_output=True).stdout


def main():
    new_pass, old_pass = (RESULTS / 'temporal-pass.txt').read_bytes(), committed('temporal-pass.txt')
    print(f'temporal-pass.txt identical={int(new_pass == old_pass)} sha256={hashlib.sha256(new_pass).hexdigest()[:16]}')
    new, old = (RESULTS / 'temporal-lattice.txt').read_text().splitlines(), committed('temporal-lattice.txt').decode().splitlines()
    fold_start = next((i for i, line in enumerate(new) if line.startswith('DEPTH_FOLD_CASES')), len(new))
    skip = ('LINE_TIMING', 'RESOLVE_BUDGET', 'RESULT ', 'CHECK resolve instruction budget parsed')
    kept = lambda lines: [l for l in lines if not l.startswith(skip) and not l.endswith('within the guaranteed 512 slots PASS')]
    a, b = kept(old), kept(new[:fold_start])
    matcher = difflib.SequenceMatcher(None, a, b, autojunk=False)
    differing = sum(max(i2 - i1, j2 - j1) for tag, i1, i2, j1, j2 in matcher.get_opcodes() if tag != 'equal')
    print(f'lattice rows compared={len(a)}/{len(b)} differing={differing} (timing, slot and RESULT rows excluded; fold rows after them)')
    lane_new = [l for l in new if l.startswith('THIN_REGION_CAMERA_FLIGHT') and ' lane=1 ' in l]
    lane_old = [l for l in old if l.startswith('THIN_REGION_CAMERA_FLIGHT') and ' lane=1 ' in l]
    print(f'lane flight rows (fold path, lane term on) identical={int(lane_new == lane_old)} n={len(lane_new)}')
    for line in new[fold_start:]:
        if line.startswith(('DEPTH_FOLD ', 'DEPTH_FOLD_FAULT ', 'DEPTH_FOLD_RESET ', 'RESULT ')):
            print(line)
    for line in new:
        if line.startswith('RESOLVE_BUDGET') and 'line_mask' in line:
            print(line)
    for label, lines in (('before', old), ('after', new)):
        print(label, next((l for l in lines if l.startswith('LINE_TIMING_CAMERA_LANE')), 'missing'))
    return 0 if new_pass == old_pass and differing == 0 and lane_new == lane_old else 1


if __name__ == '__main__':
    sys.exit(main())
