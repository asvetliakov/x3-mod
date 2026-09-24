#!/usr/bin/env python3
"""X3M_TAA_THIN_REGION_SOURCE, motion-output evidence (docs/verification/temporal-resolve.md, "thin-region source A/B"):
from the partial summary of `run_motion_output.py seam-thin-vote-far-on-source-{both,screen,vote}` (bottle X3), per case
the checks, the DLL's taa_thin_region_source row, the struts' tests-target b per frame, the vote counters of the last
thin_vote_frame row and the unvoted flagged pixels per frame (the search's own flags; 0 required under vote)."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
summary = json.loads((ROOT / 'verification/results/bottle-X3/motion-output-partial.json').read_text())
for name in sorted(summary['cases']):
    case = summary['cases'][name]
    if not name.startswith('seam-thin-vote-far-on-source-'):
        continue
    rows = [(r['requested'], r['configured'], r['reason'], r['thin_vote'], r['twins'], r['camera_gate']) for r in case['source_rows']]
    struts = {frame: (m['S']['b_min'], m['S']['b_max']) for frame, m in sorted(case['masks'].items())}
    panel = {frame: m['P']['b_max'] for frame, m in sorted(case['masks'].items())}
    last = case['thin_vote_frame_last'] or {}
    print(f"{name}: exit={case['exit']} checks={case['checks']} source={case['source']} row={rows}")
    print(f"  strut_b(min,max)={struts} panel_b_max={panel}")
    print(f"  unvoted_flagged={dict(sorted(case['unvoted_flagged'].items()))} voted_last={last.get('voted')} reads={last.get('reads')} measured={last.get('measured')}")
