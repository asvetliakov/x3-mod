#!/usr/bin/env python3
"""Restore the nine wall-clock timing rows of a fresh run_temporal_pass.py record to the committed values (the ledger's
convention since the section-10 build): LINE_TIMING, LINE_TIMING_CAMERA, LINE_TIMING_CAMERA_LANE and the six
FOLD_TIMING rows of verification/results/bottle-X3/temporal-lattice.txt, and their summary fields (pass_timing,
fold_timing, thin_region_camera.timing / timing_lane); lattice.report_sha256 becomes the restored file's hash and
records_note says so. Then checks that every other line of the lattice record outside the LUMA_LOCK rows and the lock's
budget row equals the committed one (the non-lock rows are bit-identical).

Run from the repository root after the fixture: python3 verification/results/taa-luminance-lock/restore_timing_rows.py
"""
import hashlib
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
LATTICE = 'verification/results/bottle-X3/temporal-lattice.txt'
SUMMARY = 'verification/results/bottle-X3/temporal-pass-summary.json'
TIMING = ('LINE_TIMING ', 'LINE_TIMING_CAMERA ', 'LINE_TIMING_CAMERA_LANE ', 'FOLD_TIMING build=')
NOTE = sys.argv[1] if len(sys.argv) > 1 else ''


def committed(path):
    # Bytes decoded without newline translation: the lattice record keeps the fixture's CRLF line endings.
    return subprocess.run(['git', 'show', 'HEAD:' + path], cwd=ROOT, check=True, capture_output=True).stdout.decode()


def timing(line):
    return line.startswith(TIMING)


old_lines = committed(LATTICE).splitlines(keepends=True)
new_path = ROOT / LATTICE
new_lines = new_path.read_bytes().decode().splitlines(keepends=True)
old_timing = [line for line in old_lines if timing(line)]
positions = [i for i, line in enumerate(new_lines) if timing(line)]
assert len(old_timing) == len(positions) == 9, (len(old_timing), len(positions))
for i, line in zip(positions, old_timing):
    new_lines[i] = line
new_path.write_bytes(''.join(new_lines).encode())

# Every line outside the lock's section (LUMA_LOCK_CASES .. LUMA_LOCK_BASE, its CHECK lines included) and the lock's budget
# row equals the committed record (the section grew, so compare the filtered sequences).
def outside_lock(lines):
    out, inside = [], False
    for line in lines:
        if line.startswith('LUMA_LOCK_CASES'):
            inside = True
        if not inside and not line.startswith('RESOLVE_BUDGET variant=embedded_far_camera_hold_lock ') \
                and not line.startswith('RESOLVE_BUDGET_DELTA'):
            out.append(line)
        if line.startswith('LUMA_LOCK_BASE'):
            inside = False
    return out


old_rest = outside_lock(old_lines)
new_rest = outside_lock(new_lines)
differing = [(a, b) for a, b in zip(old_rest, new_rest) if a != b]
print('non-lock lines', len(new_rest), 'committed', len(old_rest), 'differing', len(differing) + abs(len(old_rest) - len(new_rest)))
for a, b in differing[:5]:
    print('  committed:', a.rstrip()[:160])
    print('  new:      ', b.rstrip()[:160])

summary_path = ROOT / SUMMARY
summary = json.loads(summary_path.read_text())
old_summary = json.loads(committed(SUMMARY))
summary['pass_timing'] = old_summary['pass_timing']
summary['fold_timing'] = old_summary['fold_timing']
summary['thin_region_camera']['timing'] = old_summary['thin_region_camera']['timing']
summary['thin_region_camera']['timing_lane'] = old_summary['thin_region_camera']['timing_lane']
summary['lattice']['report_sha256'] = hashlib.sha256(new_path.read_bytes()).hexdigest()
if NOTE:
    summary['records_note'] = NOTE
summary_path.write_text(json.dumps(summary, indent=2) + '\n')
print('restored 9 timing rows; lattice sha256', summary['lattice']['report_sha256'])
