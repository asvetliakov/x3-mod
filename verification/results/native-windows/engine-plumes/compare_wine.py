"""Native Windows record against the retained Wine record of run_engine_plumes.py: check verdicts that differ, the
flow rows of the failing checks, and the stage / build timings side by side. Prints only those rows."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
wine = json.loads((ROOT / 'verification/results/bottle-X3/engine-plumes/summary.json').read_text())
native = json.loads((Path(__file__).parent / 'summary.json').read_text())
for name, r in (('wine', wine), ('native', native)):
    print(name, 'passed', r['passed'], 'checks', r['report']['check_count'], 'failed', len(r['report']['failed_checks']),
          'commit', r['source']['commit'][:12])
w, n = dict(map(tuple, wine['report']['checks'])), dict(map(tuple, native['report']['checks']))
print('same labels', set(w) == set(n), 'verdicts differ', [k for k in w if n.get(k) != w[k]])
for a, b in zip(wine['report']['flow'], native['report']['flow']):
    print(f"flow {a['width']} value {a['value']}: ratio {a['ratio']} -> {b['ratio']}, peak_corr {a['peak_corr']} -> {b['peak_corr']}, "
          f"shift_px {a['shift_px']} -> {b['shift_px']}")
gw, gn = wine['gates_met'], native['gates_met']
for key in ('stage_gpu_ms', 'build_us', 'build_floor_us', 'build_merge_us'):
    for k in gw[key]:
        print(key, k, gw[key][k], '->', gn[key].get(k))
for key in ('core_survival_min', 'trail_dark_px_max', 'ps_slots', 'colour_error_max'):
    print(key, gw[key], '->', gn[key])
