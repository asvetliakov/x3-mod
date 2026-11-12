#!/usr/bin/env python3
"""The walk's removal (docs/architecture/weapon-effects.md 5.1.1, placement decision 2026-11-12): the native fixture's
budget rows before (the D3D record at the base commit, git show) and after (the working record). Prints the stage CPU
rows (best / mean us of 200 builds), the muzzle-flash and projectile budgets and the hook's event cost; reads only
those fields.

  python3 verification/results/weapon-effects-nowalk/cost_rows.py [base_commit]
"""
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
REC = 'verification/results/native-windows/weapon-effects/'


def record(name, commit=None):
    if commit:
        text = subprocess.run(['git', '-C', str(ROOT), 'show', f'{commit}:{REC}{name}'], capture_output=True, text=True,
                              check=True).stdout
    else:
        text = (ROOT / REC / name).read_text()
    return json.loads(text)


def rows(r):
    m = r['metrics']
    out = {}
    for x in m['stage_cpu']:
        out['stage_cpu ' + x['case']] = (x['best_us'], x['mean_us'], x['patches'])
    for x in m['flash']:
        if str(x.get('case', '')).startswith('budget'):
            key = 'flash ' + x['case'] + (' ' + x['tune'] if 'tune' in x else '')
            out[key] = (x.get('cpu_us'), x.get('gpu_us'), None)
    for x in m['projectile']:
        if x.get('case') in ('budget', 'budget_worst'):
            out['projectile ' + x['case']] = (x.get('cpu_us'), x.get('gpu_us'), x.get('drawn'))
    return out


def event_cost(r):
    c, f = r.get('event_cost') or {}, r.get('event_cost_full') or {}
    return c.get('median_us'), f.get('median_us')


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else '89c534a1e'
    before, after = record('summary.json', base), record('summary.json')
    print(f'before={base} ({before["source"]["commit"]}) after={after["source"]["commit"]} passed={after["passed"]}')
    b, a = rows(before), rows(after)
    for k in sorted(set(b) | set(a)):
        print(f'{k:58s} before={b.get(k)} after={a.get(k)}  (stage_cpu: best, mean, patches; others: cpu, gpu, n)')
    hb, ha = record('hook-summary.json', base), record('hook-summary.json')
    print('hook event_cost median_us (50 events, full ring) before=%s after=%s' % (event_cost(hb), event_cost(ha)))
    print('hook handler_instructions/frame before=%s/%s after=%s/%s' % (
        hb['build']['audit']['handler_instructions']['_x3m_weapon_event'], hb['build']['audit']['handler_frame_bytes'],
        ha['build']['audit']['handler_instructions']['_x3m_weapon_event'], ha['build']['audit']['handler_frame_bytes']))


if __name__ == '__main__':
    main()
