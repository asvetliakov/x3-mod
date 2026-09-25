#!/usr/bin/env python3
"""Compact record of the two apply cases of run_sun_share_live.py (single shadow map removed, 2026-09-25).

Run: X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_share_live.py
       --fixture verification/probe/build/motion_output_fixture.exe --dll verification/probe/build/motion-output-seam/d3d9.dll
       --case shadow_apply_cascades --case shadow_apply_no_cascades --result <scratch>/sun-share-live.json
then: python3 compact_sun_share_live.py <scratch>/sun-share-live.json  (writes sun-share-live.json beside this script)
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
report = json.loads(Path(sys.argv[1]).read_text())
keep = ('frames', 'exact_taa_frames', 'refusal_frames', 'apply_frames', 'apply_attempts', 'no_map', 'receiver_depth', 'rt2_format',
        'params_lines_linear', 'apply_skipped', 'shadowed_pixels_min', 'elapsed_seconds')
compact = {'passed': report['passed'], 'bottle': report['bottle'], 'inputs': report['inputs'],
           'cases': {name: {k: case[k] for k in keep if k in case} | ({'cascades': {k: v for k, v in case['cascades'].items() if not isinstance(v, (list, dict))}}
                                                                    if isinstance(case.get('cascades'), dict) else {})
                     for name, case in report['cases'].items()}}
(HERE / 'sun-share-live.json').write_text(json.dumps(compact, indent=1) + '\n')
print(json.dumps({n: {k: c.get(k) for k in ('exact_taa_frames', 'apply_frames', 'apply_attempts', 'no_map')} for n, c in compact['cases'].items()}), compact['passed'])
