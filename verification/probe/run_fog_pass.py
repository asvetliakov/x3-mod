#!/usr/bin/env python3
"""Detached volumetric fog pass: fresh fixture build, one Wine run, a compact record.

Pattern of run_ambient_occlusion.py. Builds verification/probe/build_fog_pass.sh
(the production pass with its embedded programs), runs the fixture in the
selected bottle with builtin D3D9, parses the CHECK / REFERENCE / ORACLE / SKY /
APPLY / TIMING lines and writes <results>/fog-pass-gpu1.json (bottle, hashes,
counts, tolerances, timing medians against the note's budget). Run through
wine_lock.py with X3M_FIXTURE_BOTTLE=X3. Never launches the game.
"""
from pathlib import Path
import hashlib
import json
import os
import re
import subprocess
import sys
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / 'verification/probe/build/fog-pass/fog_pass_fixture.exe'
SOURCES = ('src/renderer/fog_pass.h', 'src/renderer/fog_pass.cpp', 'src/renderer/fog_pass_math.h',
           'src/renderer/fog_march_program_inc.h', 'src/renderer/fog_composite_program_inc.h',
           'src/renderer/fog_sky_level0_program_inc.h', 'src/renderer/fog_sky_reduce_program_inc.h',
           'src/fog/fog_march_ps.hlsl', 'src/fog/fog_composite_ps.hlsl', 'src/fog/fog_sky_level0_ps.hlsl', 'src/fog/fog_sky_reduce_ps.hlsl',
           'verification/probe/fog_pass_fixture.cpp', 'verification/probe/fog_reference.h',
           'verification/probe/build_fog_pass.sh', 'verification/probe/run_fog_pass.py')
# docs/architecture/volumetric-fog.md section 4: +0.7 ms median CPU proposed, refuse above +1.0 ms
# (estimates, measured here detached: fenced windows are not game frame time).
BUDGET_MS = {'acceptance': 0.7, 'cap': 1.0}
# The lit fraction is k / 16: a sample on a shadow edge may flip between the
# GPU's float32 and the float64 twin, so the field is compared statistically.
REFERENCE_TOLERANCE = {'map_twin': {'mean_abs': 0.002, 'within_fraction': 0.998}, 'analytic': {'mean_abs': 0.01, 'within_fraction': 0.98},
                       'against_view_depth_lane': {'mean_abs': 0.002, 'within_fraction': 0.998}}
REFERENCE_SCENES = ['cascade0_absent', 'phase0', 'phase3', 'r32f']
TIMING_BLOCKS = [(1280, 768, 1), (1280, 768, 0), (1920, 1080, 1), (1920, 1080, 0), (1280, 768, 1), (1280, 768, 0)]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        try:
            out[key] = int(value) if re.fullmatch(r'-?\d+', value) else float(value)
        except ValueError:
            out[key] = value
    return out


def parse(text):
    """The fixture's report as one dictionary (also the host test's subject)."""
    report = {'checks': [], 'reference': [], 'oracles': [], 'sky': [], 'apply': {}, 'timing': [], 'timing_quads': [],
              'caps': None, 'attach': None, 'reset': None, 'result': None}
    for line in text.splitlines():
        if line.startswith('CHECK '):
            _, label, verdict = line.split(' ', 2)
            report['checks'].append([label, verdict.strip() == 'PASS'])
        elif line.startswith('REFERENCE '):
            entry = fields(line)
            entry['within_fraction'] = entry['within_one_step'] / entry['pixels'] if entry.get('pixels') else 0
            report['reference'].append(entry)
        elif line.startswith('ORACLE '):
            entry = fields(line)
            entry['passed'] = line.rstrip().endswith(' PASS')
            report['oracles'].append(entry)
        elif line.startswith('SKY '):
            report['sky'].append(fields(line))
        elif line.startswith('APPLY '):
            entry = fields(line)
            report['apply'][entry['scene']] = entry
        elif line.startswith('TIMING_QUADS '):
            report['timing_quads'].append(fields(line))
        elif line.startswith('TIMING '):
            entry = fields(line)
            entry['budget'] = {'acceptance_ms': BUDGET_MS['acceptance'], 'cap_ms': BUDGET_MS['cap'],
                               'within_acceptance': entry['chain_ms'] <= BUDGET_MS['acceptance'], 'within_cap': entry['chain_ms'] <= BUDGET_MS['cap']}
            report['timing'].append(entry)
        elif line.startswith('CAPS '):
            report['caps'] = fields(line)
        elif line.startswith('ATTACH '):
            report['attach'] = fields(line)
        elif line.startswith('RESET PASS'):
            report['reset'] = fields(line)
        elif line.startswith('RESULT '):
            report['result'] = fields(line)
            report['result']['verdict'] = line.split()[1]
    report['check_count'] = len(report['checks'])
    report['check_failures'] = sum(not passed for _, passed in report['checks'])
    report['failed_checks'] = [label for label, passed in report['checks'] if not passed]
    return report


def accept(report):
    """Raises AssertionError naming the first violated acceptance term."""
    assert report['result'] and report['result']['verdict'] == 'PASS' and report['check_failures'] == 0, report['failed_checks']
    assert report['result']['checks'] == report['check_count'], (report['result'], report['check_count'])
    assert report['attach'] and 0 < report['attach']['largest_program_slots'] <= 512, report['attach']
    assert sorted(set(e['scene'] for e in report['reference'])) == REFERENCE_SCENES, report['reference']
    for entry in report['reference']:
        tolerance = REFERENCE_TOLERANCE[entry['kind']]
        assert entry['mean_abs'] <= tolerance['mean_abs'] and entry['within_fraction'] >= tolerance['within_fraction'], entry
    assert len(report["oracles"]) >= 4 and all(o['passed'] for o in report['oracles']), report['oracles']
    assert sorted(report['apply']) == ['phase0', 'phase3'], report['apply']
    assert all(a['over'] == 0 and a['alpha_changed'] == 0 and a['bound_bad'] == 0 and a['darker'] == 0 for a in report['apply'].values()), report['apply']
    assert [(t['width'], t['height'], t['sky']) for t in report['timing']] == TIMING_BLOCKS, report['timing']
    assert len(report['timing_quads']) == 3 and report['reset'], (report['timing_quads'], report['reset'])


def main():
    results = bottle.results_dir(ROOT)
    record = {'passed': False, 'game_launched': False, 'bottle': bottle.describe(), 'sources': {s: sha(ROOT / s) for s in SOURCES},
              'budget_ms': BUDGET_MS, 'reference_tolerance': REFERENCE_TOLERANCE}
    out_path = results / 'fog-pass-gpu1.json'
    try:
        subprocess.run(['sh', str(ROOT / 'verification/probe/build_fog_pass.sh')], check=True, cwd=ROOT)
        record['executable_sha256'] = sha(EXE)
        command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(EXE.parent), str(EXE)]
        record['command'] = command
        text_path, log_path = results / 'fog-pass-gpu1.txt', results / 'fog-pass-gpu1-wine.log'
        with text_path.open('w') as out, log_path.open('w') as err:
            run = subprocess.run(command, stdout=out, stderr=err, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=900)
        record['exit_code'] = run.returncode
        text = text_path.read_text()
        record['report_sha256'] = sha(text_path)
        record['report'] = parse(text)
        assert run.returncode == 0, text[-2000:]
        accept(record['report'])
        assert record['sources'] == {s: sha(ROOT / s) for s in SOURCES} and sha(EXE) == record['executable_sha256'], 'Provenance changed during run'
        record['passed'] = True
    finally:
        out_path.write_text(json.dumps(record, indent=1) + '\n')
        summary = {k: record.get(k) for k in ('passed', 'exit_code', 'executable_sha256')}
        if 'report' in record:
            report = record['report']
            summary['checks'] = report['check_count']
            summary['failed_checks'] = report['failed_checks']
            summary['slots'] = report['attach'] and report['attach']['largest_program_slots']
            summary['reference'] = [(e['scene'], e['kind'], e['mean_abs'], e['max'], round(e['within_fraction'], 5)) for e in report['reference']]
            summary['oracles'] = [(o['scene'], o['label'], o['mean'], o['passed']) for o in report['oracles']]
            summary['apply'] = {s: (a['over'], a['max_rel'], a['bound_bad'], a['darker'], a['sky_cap_error']) for s, a in report['apply'].items()}
            summary['timing'] = [(t['width'], t['height'], t['sky'], t['submit_ms'], t['chain_ms'], t['gpu_ms'], t['gpu_timestamp_ms'], t['device_calls'], t['budget']['within_cap']) for t in report['timing']]
            summary['timing_quads'] = report['timing_quads']
        print(json.dumps(summary, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
