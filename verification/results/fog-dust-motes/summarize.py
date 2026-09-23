#!/usr/bin/env python3
"""Compact record of the fog dust motes' fixture runs (docs/architecture/fog-dust-motes.md, "As built").

  summarize.py --pass-run DIR --bridge-run DIR [--temporal-output FILE] --output FILE

DIR of fog_density_shader_run.py (after `check`: summary.json, pass_stdout.txt), DIR of fog_route_bridge_run.py (after
`check --summary DIR/summary.json`: bridge.log, execution.json), and the temporal fixture's stdout with case (m).
Reads only the named rows; writes one small JSON (bottle-X3 by convention)."""
import argparse
import json
import re
from pathlib import Path


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        try:
            out[key] = float(value) if re.search(r'[.e]', value) else int(value)
        except ValueError:
            out[key] = value
    return out


def rows(text, tag):
    return [fields(m) for m in re.findall(r'^%s (.*)$' % tag, text, re.M)]


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('--pass-run', type=Path, required=True); p.add_argument('--bridge-run', type=Path, required=True)
    p.add_argument('--temporal-output', type=Path); p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    summary = json.loads((a.pass_run / 'summary.json').read_text())
    text = (a.pass_run / 'pass_stdout.txt').read_text(errors='replace')
    checks = re.findall(r'^CHECK (.+?) (PASS|FAIL)\s*$', text, re.M)
    new = [n for n, _ in checks if n.startswith('M_motes_') or n.startswith('motes')]
    pass_record = dict(result=summary['result'], gates_passed=sum(summary['gates'].values()), gates=len(summary['gates']),
                       checks=len(checks), failed=[n for n, s in checks if s != 'PASS'], pre_existing_checks=len(checks) - len(new), new_checks=len(new),
                       motes_named_checks=summary['pass_fixture']['motes_checks'], state_restorations=summary['pass_fixture']['state_restorations'],
                       accepted_look_hashes_equal=summary['visibility_grid']['pass_off_hashes']['measured'] == summary['visibility_grid']['pass_off_hashes']['expected'],
                       grid_report=summary['pass_fixture'].get('grid_report'), grid_toggle=summary['pass_fixture'].get('grid_toggle'),
                       slots={k: v['slots'] for k, v in summary['shaders'].items() if 'mote' in k},
                       executable_sha256=summary['pass_fixture']['executable_sha256'], bottle=summary['bottle'])
    for tag in ('MOTES_POSES', 'MOTES_RESOURCES', 'MOTES_CALLS', 'MOTES_SKY', 'MOTES_STREAK', 'MOTES_CUT', 'MOTES_DEPTH', 'MOTES_WRAP', 'MOTES_RESET', 'MOTES_REFUSAL'):
        found = rows(text, tag); pass_record[tag.lower()] = found[0] if found else None
    pass_record['motes_shafts'] = rows(text, 'MOTES_SHAFTS')
    log = (a.bridge_run / 'bridge.log').read_text(errors='replace')
    bridge_checks = re.findall(r'^CHECK (\S+) (PASS|FAIL)$', log, re.M)
    bridge = dict(result=(re.findall(r'^RESULT fog_route_bridge checks=(\d+) (\w+)$', log, re.M) or [None])[0],
                  shadow_ab_names=len({n for n, s in bridge_checks if n.startswith('shadow_ab_') and s == 'PASS'}),
                  motes_ab_names=sorted({n for n, s in bridge_checks if n.startswith('motes_ab_') and s == 'PASS'}),
                  motes_ab_rows=[m for m in re.findall(r'^MOTES_AB (on_frames=\S+ vb=\S+ visible=\S+|alternating\S* .*|reset_allocations=.*)$', log, re.M)],
                  steps=json.loads((a.bridge_run / 'execution.json').read_text())['steps'])
    summary_path = a.bridge_run / 'summary.json'
    if summary_path.exists():
        s = json.loads(summary_path.read_text()); bridge.update(legacy_bit_identical_to_baseline=s['density']['legacy_bit_identical_to_baseline'], exit_checks=s['exit_checks'])
    record = dict(schema=1, pass_fixture=pass_record, route_bridge=bridge)
    if a.temporal_output:
        t = a.temporal_output.read_text(errors='replace')
        record['temporal_streak'] = dict(rows=rows(t, 'MOTE_STREAK'), hull=rows(t, 'MOTE_STREAK_HULL'),
                                         samples=re.findall(r'^SAMPLE (mote streak.*?) actual=\S+ expected=\S+ tolerance=\S+ (PASS|FAIL)$', t, re.M),
                                         result=(re.findall(r'^RESULT (\w+) numerical=(\d+) state_restorations=(\d+) generations=(\d+)', t, re.M) or [None])[0])
    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(json.dumps(record, indent=1, sort_keys=True) + '\n')
    print(a.output)


if __name__ == '__main__':
    main()
