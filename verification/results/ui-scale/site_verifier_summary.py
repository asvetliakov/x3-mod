#!/usr/bin/env python3
"""The site-verifier summary the ui-scale ledger cites: runs verify_ui_scale_sites.py on the installed EXE and writes
site_verifier_summary.json (result, check count, failed checks, instructions decoded, other claims considered, the
branch/dword/raw-hit witnesses, the SSE census count). Run from anywhere; paths are resolved from this file."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
run = subprocess.run([sys.executable, str(ROOT / 'verification/probe/verify_ui_scale_sites.py')], capture_output=True, text=True,
                     cwd=ROOT / 'verification/probe', timeout=600)
report = json.loads(run.stdout)
summary = {'result': report['result'], 'checks': len(report['checks']), 'failed': sorted(k for k, v in report['checks'].items() if not v),
           'instructions': report.get('instructions'), 'claims_considered': report.get('claims_considered'),
           'raw_branch_hits': report.get('raw_branch_hits'), 'branches_into_spans': report.get('branches_into_spans'),
           'dword_refs': report.get('dword_refs'), 'branches_to_site_starts': report.get('branches_to_site_starts'),
           'claims_in_windows': report.get('claims_in_windows'), 'xmm_text_census_count': report.get('xmm_text_census', {}).get('count'),
           'xmm_in_site_functions': report.get('xmm_in_site_functions'), 'identity': report.get('identity')}
(Path(__file__).resolve().parent / 'site_verifier_summary.json').write_text(json.dumps(summary, indent=2, sort_keys=True) + '\n')
print(json.dumps(summary, sort_keys=True))
raise SystemExit(report['result'] != 'PASS')
