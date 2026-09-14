#!/usr/bin/env python3
"""Detached ambient occlusion chain: fresh fixture build, one Wine run, a compact record.

Pattern of run_temporal_pass.py. Builds verification/probe/build_ambient_occlusion.sh
(the production pass with its embedded programs), runs the fixture in the
selected bottle with builtin D3D9, parses the CHECK / REFERENCE / ORACLE /
APPLY / TIMING lines and writes <results>/ambient-occlusion-gpu1.json (bottle,
hashes, counts, tolerances, timing medians against the note's budget). Run
through wine_lock.py with X3M_FIXTURE_BOTTLE=X3. Never launches the game.
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
EXE = ROOT / 'verification/probe/build/ambient-occlusion/ambient_occlusion_fixture.exe'
SOURCES = ('src/renderer/ambient_occlusion_pass.h', 'src/renderer/ambient_occlusion_pass.cpp', 'src/renderer/ambient_occlusion_caps.h',
           'src/renderer/ambient_occlusion_linearize_program_inc.h', 'src/renderer/ambient_occlusion_gtao_program_inc.h',
           'src/renderer/ambient_occlusion_blur_program_inc.h', 'src/renderer/ambient_occlusion_apply_program_inc.h',
           'src/temporal/ao_linearize_ps.hlsl', 'src/temporal/ao_gtao_ps.hlsl', 'src/temporal/ao_blur_ps.hlsl', 'src/temporal/ao_apply_ps.hlsl',
           'verification/probe/ambient_occlusion_fixture.cpp', 'verification/probe/ambient_occlusion_reference.h',
           'verification/probe/build_ambient_occlusion.sh', 'verification/probe/run_ambient_occlusion.py')
# docs/architecture/ambient-occlusion.md section 6: 0.2-0.35 ms fixed + < 0.1 ms
# GPU estimated; acceptance +0.5 ms, cap +0.8 ms (over the cap: report, do not tune).
BUDGET_MS = {'estimate_fixed': [0.2, 0.35], 'estimate_gpu': 0.1, 'acceptance': 0.5, 'cap': 0.8}
# The CPU reference tolerance: rare taps land on a different texel when a float32
# tap position sits within ~1e-5 px of a texel boundary (GPU sin/cos and
# products versus float64), so the term is compared statistically.
REFERENCE_TOLERANCE = {'mean_abs': 0.002, 'within_002_fraction': 0.999}


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
    lines = text.splitlines()
    report = {'checks': [], 'reference': {}, 'oracles': [], 'apply': {}, 'timing': [], 'timing_quads': [], 'fp16_store': None, 'caps': None, 'attach': None, 'reset': None, 'result': None}
    for line in lines:
        if line.startswith('CHECK '):
            _, label, verdict = line.split(' ', 2)
            report['checks'].append([label, verdict.strip() == 'PASS'])  # a list: labels repeat (recovery and Reset rerun a scene)
        elif line.startswith('REFERENCE '):
            entry = fields(line)
            report['reference'][entry['scene']] = entry
        elif line.startswith('ORACLE '):
            entry = fields(line)
            entry['passed'] = line.rstrip().endswith(' PASS')
            report['oracles'].append(entry)
        elif line.startswith('APPLY '):
            entry = fields(line)
            report['apply'][entry['scene']] = entry
        elif line.startswith('TIMING '):
            report['timing'].append(fields(line))
        elif line.startswith('TIMING_VARIANT '):
            report.setdefault('timing_variants', []).append(fields(line))
        elif line.startswith('TIMING_QUADS '):
            report['timing_quads'].append(fields(line))
        elif line.startswith('FP16_STORE '):
            report['fp16_store'] = fields(line)
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
    for entry in report['reference'].values():
        entry['within_002_fraction'] = entry['within_002'] / entry['pixels'] if entry.get('pixels') else 0
    for entry in report['timing']:
        entry['budget'] = {'cap_ms': BUDGET_MS['cap'], 'acceptance_ms': BUDGET_MS['acceptance'],
                           'within_acceptance': entry['chain_ms'] <= BUDGET_MS['acceptance'], 'within_cap': entry['chain_ms'] <= BUDGET_MS['cap']}
    return report


def main():
    results = bottle.results_dir(ROOT)
    record = {'passed': False, 'game_launched': False, 'bottle': bottle.describe(), 'sources': {s: sha(ROOT / s) for s in SOURCES},
              'budget_ms': BUDGET_MS, 'reference_tolerance': REFERENCE_TOLERANCE}
    out_path = results / 'ambient-occlusion-gpu1.json'
    try:
        subprocess.run(['sh', str(ROOT / 'verification/probe/build_ambient_occlusion.sh')], check=True, cwd=ROOT)
        record['executable_sha256'] = sha(EXE)
        command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(EXE.parent), str(EXE)]
        record['command'] = command
        text_path, log_path = results / 'ambient-occlusion-gpu1.txt', results / 'ambient-occlusion-gpu1-wine.log'
        with text_path.open('w') as out, log_path.open('w') as err:
            run = subprocess.run(command, stdout=out, stderr=err, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=900)
        record['exit_code'] = run.returncode
        text = text_path.read_text()
        record['report_sha256'] = sha(text_path)
        record['report'] = parse(text)
        report = record['report']
        assert run.returncode == 0 and report['result'] and report['result']['verdict'] == 'PASS' and report['check_failures'] == 0, text[-2000:]
        assert report['result']['checks'] == report['check_count'], (report['result'], report['check_count'])
        assert len(report['timing_quads']) == 2 and report['fp16_store'] and report['fp16_store']['mode'] in ('round_to_nearest', 'truncate'), (report['timing_quads'], report['fp16_store'])
        assert sorted(report['reference']) == ['corner', 'plane', 'sphere', 'step', 'tilted'], report['reference']
        for scene, entry in report['reference'].items():
            assert entry['mean_abs'] <= REFERENCE_TOLERANCE['mean_abs'] and entry['within_002_fraction'] >= REFERENCE_TOLERANCE['within_002_fraction'], (scene, entry)
        assert all(o['passed'] for o in report['oracles']) and len(report['oracles']) >= 10, report['oracles']
        assert all(a['over'] == 0 and a['alpha_changed'] == 0 for a in report['apply'].values()), report['apply']
        assert [(t['width'], t['height']) for t in report['timing']] == [(1280, 768), (1920, 1080)], report['timing']
        assert record['sources'] == {s: sha(ROOT / s) for s in SOURCES} and sha(EXE) == record['executable_sha256'], 'Provenance changed during run'
        record['passed'] = True
    finally:
        out_path.write_text(json.dumps(record, indent=1) + '\n')
        summary = {k: record.get(k) for k in ('passed', 'exit_code', 'executable_sha256')}
        if 'report' in record:
            summary['checks'] = record['report']['check_count']
            summary['check_failures'] = record['report']['check_failures']
            summary['reference'] = {s: (e['mean_abs'], e['p999'], e['max'], e['within_002_fraction']) for s, e in record['report']['reference'].items()}
            summary['apply'] = {s: (a['exact'], a['one_ulp'], a['over'], a['max_ulp']) for s, a in record['report']['apply'].items()}
            summary['timing'] = [(t['width'], t['height'], t['submit_ms'], t['chain_ms'], t['gpu_ms'], t['budget']['within_cap']) for t in record['report']['timing']]
            summary['timing_quads'] = record['report']['timing_quads']
            summary['fp16_store'] = record['report']['fp16_store']
        print(json.dumps(summary, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
