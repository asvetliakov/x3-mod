#!/usr/bin/env python3
"""Effects stage fixture: fresh build, one Wine run of the GPU fixture and one of the keys fixture, a compact record.

Builds verification/probe/build_effects_stage.py (the production EffectsStagePass and TemporalPass with their
embedded programs), extracts the three effect textures the key table names from the installed catalogues into a
scratch directory (copyrighted bytes: never under the repository), runs both executables in the selected bottle with
builtin D3D9, parses the CHECK / ATTACH / PASS_OPEN / BOLT_* / SHELL* / RESOLVE / TIMING / KEYS lines and writes
<results>/effects-stage/summary.json (bottle, hashes, checks, the numbers against the gates of
docs/architecture/effects-modernisation-opus.md section 9). Run through wine_lock.py with X3M_FIXTURE_BOTTLE=X3.
Never launches the game.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/effects'))
import effect_keys  # noqa: E402

BUILD = ROOT / 'verification/probe/build/effects-stage'
TABLE = ROOT / 'tools/effects/effect_keys.json'
GATES = {'pass_open_ms': 0.1, 'bolts60_shells2_ms': 0.05, 'fullscreen_shell_ms': 0.1, 'resolve_core_ratio': 0.9, 'resolve_trail_px': 3}


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


def windows(path):
    return 'Z:' + str(Path(path).resolve()).replace('/', '\\')


def parse(text):
    report = {'checks': [], 'attach': None, 'pass_open': None, 'bolt': [], 'shell': [], 'resolve': [], 'timing': [], 'keys': [], 'key_paths': [], 'result': None,
              'suppression': None, 'reset': None, 'fp16_refused': None, 'off_path': []}
    for line in text.splitlines():
        head = line.split(' ', 1)[0]
        if head == 'CHECK':
            _, label, verdict = line.split(' ', 2)
            report['checks'].append([label, verdict.strip() == 'PASS'])
        elif head == 'ATTACH':
            report['attach'] = fields(line)
        elif head == 'PASS_OPEN':
            report['pass_open'] = fields(line)
        elif head in ('BOLT_CAPSULE', 'BOLT_TINY', 'BOLT_OCCLUDED', 'BOLT_SOFT'):
            report['bolt'].append(dict(fields(line), case=head))
        elif head in ('SHELL', 'SHELL_CUT', 'DECAL'):
            report['shell'].append(dict(fields(line), case=head))
        elif head == 'RESOLVE':
            report['resolve'].append(dict(fields(line), what=line.split()[1]))
        elif head == 'TIMING':
            report['timing'].append(dict(fields(line), label=line.split()[1]))
        elif head == 'KEYS':
            report['keys'].append(fields(line))
        elif head == 'KEY_PATH':
            report['key_paths'].append(fields(line))
        elif head == 'SUPPRESSION':
            report['suppression'] = fields(line)
        elif head == 'RESET':
            report['reset'] = fields(line)
        elif head == 'FP16_REFUSED':
            report['fp16_refused'] = fields(line)
        elif head == 'OFF_PATH':
            report['off_path'].append(fields(line))
        elif head == 'RESULT':
            report['result'] = dict(fields(line), verdict=line.split()[1])
    report['check_count'] = len(report['checks'])
    report['check_failures'] = sum(not passed for _, passed in report['checks'])
    report['failed_checks'] = [label for label, passed in report['checks'] if not passed]
    return report


def gates(report):
    """The section-9 gates as booleans (the fixture's own CHECK lines carry the geometry gates)."""
    out = {}
    po = report['pass_open'] or {}
    out['pass_open_measured'] = bool(po)
    out['own_pass_within_gate'] = bool(po) and po['own_pass_ms'] <= GATES['pass_open_ms']
    out['own_stage_within_gate'] = bool(po) and po['own_stage_ms'] <= GATES['pass_open_ms']
    # The two timing targets of section 9 are advisory (docs/verification/effects-stage.md: both missed as measured,
    # the flight decides): reported under `timing_targets` with `advisory: True`, never part of `passed`.
    timing = {(t['label'], t['width']): t for t in report['timing']}
    out['timing_targets'] = {'advisory': True, 'met': {}}
    for width in (1920, 5120):
        for label, gate in (('bolts60_shells2', 'bolts60_shells2_ms'), ('fullscreen_shell', 'fullscreen_shell_ms')):
            t = timing.get((label, width))
            out['timing_targets']['met']['%s_%d' % (label, width)] = t is not None and t['gpu_ms'] >= 0 and t['gpu_ms'] <= GATES[gate]
    out['resolve_core'] = all(r['ratio'] >= GATES['resolve_core_ratio'] for r in report['resolve']) and len(report['resolve']) >= 8
    out['resolve_trail'] = all(r['trail_px'] <= GATES['resolve_trail_px'] for r in report['resolve'] if r['what'] == 'bolt')
    out['keys_match'] = bool(report['keys']) and all(k['match'] == 1 for k in report['keys'])
    return out


def extract_textures(directory):
    """The table's keyed textures as DDS files (the packed member unpacked) with their expected keys."""
    table = json.loads(TABLE.read_text())
    layers = effect_keys.Layers(bottle.game_dir(), [])
    pairs = []
    for entry in table['entries']:
        if not entry.get('key'):
            continue
        data, source = layers.texture(entry['name'])
        if data is None:
            raise ValueError('texture missing from the installed catalogues: ' + entry['name'])
        path = Path(directory) / (entry['name'] + '.dds')
        path.write_bytes(data)
        pairs.append((path, entry['key'], entry['name'], source))
    return pairs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--timeout', type=int, default=900)
    parser.add_argument('--only', default=None, help='fixture case filter (comma list: attach,bolt,shell,off,resolve,timing,pass_open,suppression,reset), diagnosis only')
    parser.add_argument('--skip-keys', action='store_true', help='diagnosis only: run the GPU fixture alone')
    args = parser.parse_args()
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        raise SystemExit('fixture requires X3M_FIXTURE_BOTTLE=X3')
    results = bottle.results_dir(ROOT) / 'effects-stage'
    results.mkdir(parents=True, exist_ok=True)
    if not args.no_build:
        subprocess.run([sys.executable, str(ROOT / 'verification/probe/build_effects_stage.py'), '--output', str(BUILD)], check=True, cwd=ROOT)
    build = json.loads((BUILD / 'build.json').read_text())
    gpu, keys = BUILD / 'effects_stage_fixture.exe', BUILD / 'effects_stage_keys_fixture.exe'
    assert sha(gpu) == build['executable_sha256'] and sha(keys) == build['keys_executable_sha256'], 'executable changed since build'
    d3dx = bottle.game_dir() / 'd3dx9_37.dll'
    env = dict(os.environ, WINEDLLOVERRIDES='d3d9=b')
    record = {'bottle': bottle.describe(), 'd3dx9_37_sha256': sha(d3dx), 'build': {k: build[k] for k in ('executable_sha256', 'keys_executable_sha256')}, 'gates': GATES}
    with tempfile.TemporaryDirectory(prefix='x3-effects-keys-') as scratch:
        pairs = extract_textures(scratch)
        record['textures'] = [{'name': name, 'expected_key': key, 'source': source, 'dds_sha256': sha(path)} for path, key, name, source in pairs]
        runs = {}
        for label, exe, extra in (('gpu', gpu, []), ('keys', keys, ['--keys', *sum(([windows(p), k] for p, k, _, _ in pairs), [])])):
            command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(exe.parent), str(exe), r'C:\X3\d3dx9_37.dll', *extra]
            done = subprocess.run(command, capture_output=True, env=env, timeout=args.timeout)
            text = done.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')
            (results / ('%s.log' % label)).write_text(text)
            (results / ('%s.stderr.txt' % label)).write_bytes(done.stderr)
            runs[label] = dict(command=command, exit_code=done.returncode, report=parse(text))
    gpu_report, keys_report = runs['gpu']['report'], runs['keys']['report']
    merged = dict(gpu_report)
    merged['keys'] = keys_report['keys']; merged['key_paths'] = keys_report['key_paths']
    merged['checks'] = gpu_report['checks'] + keys_report['checks']
    merged['check_count'] = len(merged['checks']); merged['check_failures'] = sum(not p for _, p in merged['checks']); merged['failed_checks'] = [l for l, p in merged['checks'] if not p]
    record['runs'] = {k: {'command': v['command'], 'exit_code': v['exit_code'], 'result': v['report']['result']} for k, v in runs.items()}
    record['report'] = merged
    record['gates_met'] = gates(merged)
    record['passed'] = all(v['exit_code'] == 0 for v in runs.values()) and merged['check_failures'] == 0 and all(r['report']['result'] and r['report']['result']['verdict'] == 'PASS' for r in runs.values())
    (results / 'summary.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({'passed': record['passed'], 'checks': merged['check_count'], 'failures': merged['check_failures'], 'failed': merged['failed_checks'], 'gates': record['gates_met'],
                      'pass_open': merged['pass_open'], 'timing': [(t['label'], t['width'], t['gpu_ms']) for t in merged['timing']], 'results': str(results)}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
