#!/usr/bin/env python3
"""The site-verifier summary the text-density ledger cites: runs verify_text_density_sites.py on the installed EXE and
writes site_verifier_summary.json (result, check count, failed checks, instructions decoded, other claims considered,
the branch/dword/raw-hit witnesses, the callers, the order-proof witnesses and the TexBltImage block's inbound branches and
return sequence). Run from anywhere; an optional first argument names the EXE (default: the verifier's, the Mac bottle;
the native Windows install is /mnt/c/X3/X3AP.exe from WSL2)."""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
command = [sys.executable, str(ROOT / 'verification/probe/verify_text_density_sites.py')]
if len(sys.argv) > 1:
    command += ['--exe', sys.argv[1]]
run = subprocess.run(command, capture_output=True, text=True, cwd=ROOT / 'verification/probe', timeout=600)
report = json.loads(run.stdout)
summary = {'result': report['result'], 'checks': len(report['checks']), 'failed': sorted(k for k, v in report['checks'].items() if not v),
           'instructions': report.get('instructions'), 'claims_considered': report.get('claims_considered'),
           'branches_into_spans': report.get('branches_into_spans'), 'raw_branch_hits_not_interior': report.get('raw_branch_hits_not_interior'),
           'dword_refs': report.get('dword_refs'), 'overlapping_claims': report.get('overlapping_claims'),
           'font_callers': report.get('font_callers'), 'materials_callers': report.get('materials_callers'),
           'cockpit_init_callers': report.get('cockpit_init_callers'), 'config_field_stores': report.get('config_field_stores'),
           'init_backward_branches': report.get('init_backward_branches'), 'font_after_span': report.get('font_after_span'),
           'blt_image_inbound': report.get('blt_image_inbound'), 'blt_image_return_sequence': report.get('blt_image_return_sequence'),
           'blt_image_following': report.get('blt_image_following'), 'identity': report.get('identity')}
(Path(__file__).resolve().parent / 'site_verifier_summary.json').write_text(json.dumps(summary, indent=2, sort_keys=True) + '\n')
print(json.dumps(summary, sort_keys=True))
raise SystemExit(report['result'] != 'PASS')
