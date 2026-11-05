"""The vnoise lattice fix (2026-11-05): the native record at commit 0e90a8f46 (the tracked summary.json of 47764d79a, before the
fix, or argv[1]) against the current native record: check counts, the flow rows' ratio and peak correlation, the
structural gates and the ps slot count. Prints only those rows."""
import json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
REL = 'verification/results/native-windows/engine-plumes/summary.json'
rev = sys.argv[1] if len(sys.argv) > 1 else '47764d79a'
before = json.loads(subprocess.run(['git', '-C', str(ROOT), 'show', f'{rev}:{REL}'], check=True, capture_output=True, text=True).stdout)
after = json.loads((ROOT / REL).read_text())
for name, r in (('before', before), ('after', after)):
    print(name, 'passed', r['passed'], 'checks', r['report']['check_count'], 'failed', len(r['report']['failed_checks']),
          [c[0] if isinstance(c, list) else c for c in r['report']['failed_checks']][:8])
for a, b in zip(before['report']['flow'], after['report']['flow']):
    print(f"flow {a['width']} value {a['value']}: ratio {a['ratio']} -> {b['ratio']}, peak_corr {a['peak_corr']} -> {b['peak_corr']}")
for key in ('core_survival_min', 'trail_dark_px_max', 'ps_slots', 'colour_error_max'):
    print(key, before['gates_met'][key], '->', after['gates_met'][key])
