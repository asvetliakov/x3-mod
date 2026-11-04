#!/usr/bin/env python3
"""Writes host_check.json beside this file: the output of verification/probe/sky_stars_check.cpp (compiled natively with
src/renderer/exposure.cpp by verification/analysis/test_sky_stars.py run_check): the mark parse, N, the vertex buffer
law, the kernel energy and the installed meter's replay on star-only frames. Usage: python3 -I host_check.py
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / 'verification/analysis'))
import test_sky_stars  # noqa: E402

rc, report = test_sky_stars.run_check()
(HERE / 'host_check.json').write_text(json.dumps(report, indent=1) + '\n')
print('ok' if report['ok'] else 'FAIL', 'checks=%d' % report['checks'], 'rc=%d' % rc)
sys.exit(rc)
