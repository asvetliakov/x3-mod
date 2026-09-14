#!/usr/bin/env python3
"""Measure how the docking-port pair's luminance changes with distance.

Runs the detached `port_distance_fixture.exe` in the CrossOver bottle (no game
launch, no installation, no registry change) and records only derived numbers:
mean/max luminance and mean alpha of the quad at three camera distances, per
light/view configuration, with the real normal map and with a flat-normal
control. The original programs and the three original DDS images stay outside
the repository and are named by `--pair-dir` / `--texture-dir`; the runner
checks their SHA-256 against the identities recorded in
docs/reverse-engineering/station-material-distance.md before running.

Settles the sign contradiction between that note's minification analysis and
the simulation in docs/architecture/specular-antialiasing.md.
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and its results directory

PAIR_DIR = '/tmp/x3-bottleX3-run39'
TEXTURE_DIR = '/tmp/x3-port-tex'
PROGRAMS = {'vs': 'vs_4944d81dfe531b37.bin', 'ps': 'ps_64bac8bb307eb896.bin'}
# SHA-256 of the decompressed catalogue images identified for stages 0/1/2.
TEXTURES = {
    'diffuse': ('metal_argon_lattice_windowedgrid_diff.dds',
                '7d860ec4a8f2db0b650682876f4ee49a272c0e7e5878c967240904b88afb3b9a'),
    'bump': ('metal_argon_lattice_windowedgrid_bump.dds',
             '14f53a84b37b24b633c97565b2aa718c06b98e42920fb4a1510852ac3b263262'),
    'specular': ('metal_argon_lattice_windowedgrid_spec.dds',
                 '035f54f4722bcd03d905118bc3b923de920c58de10d534ef6ff2e16d9f51ef48'),
}
PS_SHA256 = '84eeded40acc811b109221fe5c7e1b5a898f3b9a34a1b4685937a24f6b1bc4a7'
# The three steps are the same camera geometry at 1x, 2x and 4x the run-39
# distance: on-screen widths 170 / 85 / 42.5 px, isotropic levels 2.59/3.59/4.59.
STEP_LABELS = {0: '1x', 1: '2x', 2: '4x'}
SIGN_EPSILON = 0.005  # below this a ratio counts as unchanged, not a sign


def _fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=([^\s]+)', line):
        try:
            out[key] = int(value)
        except ValueError:
            try:
                out[key] = float(value)
            except ValueError:
                out[key] = value
    return out


def parse(text):
    """Transcript to a report: caps, textures, configurations, measurements."""
    report = {'caps': None, 'programs': None, 'textures': [], 'configurations': [],
              'measurements': [], 'result': None, 'api_failures': []}
    for line in text.splitlines():
        if line.startswith('CAPS '):
            report['caps'] = _fields(line)
        elif line.startswith('PROGRAMS '):
            report['programs'] = _fields(line)
        elif line.startswith('TEXTURE '):
            report['textures'].append(_fields(line))
        elif line.startswith('CONFIG '):
            report['configurations'].append(_fields(line))
        elif line.startswith('MEASURE '):
            measurement = _fields(line)
            measurement['step_label'] = STEP_LABELS.get(measurement.get('step'))
            report['measurements'].append(measurement)
        elif line.startswith('API_FAIL '):
            report['api_failures'].append(line.split(None, 1)[1])
        elif line.startswith('RESULT '):
            parts = line.split()
            report['result'] = {'verdict': parts[1], **_fields(line)}
    return report


def _ratio(numerator, denominator):
    return numerator / denominator if denominator else None


def sign_of(ratio):
    if ratio is None:
        return 'unknown'
    if ratio > 1 + SIGN_EPSILON:
        return 'brighter'
    if ratio < 1 - SIGN_EPSILON:
        return 'darker'
    return 'unchanged'


def summarize(report):
    """Per configuration and normal mode, the 2x/1x and 4x/1x ratios and signs."""
    grouped = {}
    for measurement in report['measurements']:
        key = (measurement['config'], measurement['normal'])
        grouped.setdefault(key, {})[measurement['step']] = measurement
    summary = {}
    for (config, normal), steps in sorted(grouped.items()):
        base = steps.get(0)
        if base is None or not {1, 2} <= set(steps):
            continue
        entry = {'quad_px_w': [steps[s]['quad_px_w'] for s in (0, 1, 2)],
                 'inner_pixels': [steps[s]['inner'] for s in (0, 1, 2)],
                 'raw_mean_luma': [steps[s]['raw_mean_luma'] for s in (0, 1, 2)],
                 'raw_mean_alpha': [steps[s]['raw_mean_alpha'] for s in (0, 1, 2)],
                 'comp_mean_luma': [steps[s]['comp_mean_luma'] for s in (0, 1, 2)]}
        for field, label in (('raw_mean_luma', 'luma'), ('raw_max_luma', 'max_luma'),
                             ('raw_mean_alpha', 'alpha'), ('comp_mean_luma', 'composite'),
                             ('raw_all_mean_luma', 'luma_all_pixels')):
            for step in (1, 2):
                entry[f'{label}_{STEP_LABELS[step]}'] = _ratio(steps[step][field], base[field])
        entry['sign_luma_2x'] = sign_of(entry['luma_2x'])
        entry['sign_luma_4x'] = sign_of(entry['luma_4x'])
        entry['sign_composite_4x'] = sign_of(entry['composite_4x'])
        entry['sign_alpha_4x'] = sign_of(entry['alpha_4x'])
        summary.setdefault(config, {})[normal] = entry
    # real / flat isolates the normal-map channel from the diffuse, specular and
    # alpha channels, which minify in both modes.
    for modes in summary.values():
        if set(modes) != {'real', 'flat'}:
            continue
        for label in ('luma', 'composite', 'alpha'):
            for step in (1, 2):
                key = f'{label}_{STEP_LABELS[step]}'
                modes['real'][f'normal_channel_{key}'] = _ratio(modes['real'][key],
                                                                modes['flat'][key])
        modes['real']['sign_normal_channel_luma_4x'] = sign_of(
            modes['real']['normal_channel_luma_4x'])
    return summary


def conclusion(summary):
    """One line per configuration: the sign the real pair shows at 4x distance."""
    return {config: {'luma_4x': modes['real']['luma_4x'],
                     'sign': modes['real']['sign_luma_4x'],
                     'normal_channel_luma_4x': modes['real'].get('normal_channel_luma_4x'),
                     'composite_4x': modes['real']['composite_4x'],
                     'alpha_4x': modes['real']['alpha_4x'],
                     'alpha_sign': modes['real']['sign_alpha_4x']}
            for config, modes in summary.items() if 'real' in modes}


def verdict(report, summary):
    """The fixture is conclusive when every configuration produced three steps."""
    checks = {
        'fixture_passed': bool(report['result']) and report['result']['verdict'] == 'PASS',
        'no_api_failures': not report['api_failures'],
        'three_configurations': len(summary) == 3,
        'both_normal_modes': all(set(modes) == {'real', 'flat'} for modes in summary.values()),
        # The untilted quad hits the designed widths exactly; a tilted quad's
        # bounding box is wider than its centre width because its near edge is
        # closer to the camera, so only the halving between steps is checked.
        'head_on_widths_as_designed': all(
            abs(entry['quad_px_w'][0] - 170) <= 2 and abs(entry['quad_px_w'][1] - 85) <= 2
            and abs(entry['quad_px_w'][2] - 43) <= 2
            for entry in summary.get('head_on', {}).values()),
        'widths_halve_per_step': all(
            1.85 <= entry['quad_px_w'][step] / entry['quad_px_w'][step + 1] <= 2.35
            for modes in summary.values() for entry in modes.values() for step in (0, 1)),
        'alpha_present': all(entry['raw_mean_alpha'][0] > 0
                             for modes in summary.values() for entry in modes.values()),
    }
    return checks


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pair-dir', default=PAIR_DIR)
    parser.add_argument('--texture-dir', default=TEXTURE_DIR)
    parser.add_argument('--name', default='port-distance1')
    parser.add_argument('--timeout', type=int, default=300)
    options = parser.parse_args(argv)

    root = Path(__file__).resolve().parents[2]
    source = root / 'verification/probe/port_distance_fixture.cpp'
    executable = root / 'verification/probe/build/port_distance_fixture.exe'
    results = bottle.results_dir(root)
    pair_dir, texture_dir = Path(options.pair_dir), Path(options.texture_dir)

    inputs, missing = {}, []
    for label, name in PROGRAMS.items():
        path = pair_dir / name
        if not path.is_file():
            missing.append(str(path))
            continue
        inputs['program_' + label] = hashlib.sha256(path.read_bytes()).hexdigest()
    for label, (name, expected) in TEXTURES.items():
        path = texture_dir / name
        if not path.is_file():
            missing.append(str(path))
            continue
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        inputs['texture_' + label] = digest
        if digest != expected:
            missing.append(f'{path}: sha256 {digest} is not the recorded {expected}')
    if not executable.is_file():
        missing.append(f'{executable} (build with verification/probe/build_port_distance.sh)')
    if missing:
        print('missing or mismatched fixture inputs:\n  ' + '\n  '.join(missing), file=sys.stderr)
        return 2
    if inputs.get('program_ps') != PS_SHA256:
        print(f"pixel program sha256 {inputs['program_ps']} is not the recorded {PS_SHA256}",
              file=sys.stderr)
        return 2

    command = [bottle.WINE, *bottle.wine_args(), '--workdir', str(executable.parent),
               str(executable), '--pair-dir', str(pair_dir), '--texture-dir', str(texture_dir)]
    environment = os.environ.copy()
    environment['WINEDLLOVERRIDES'] = 'd3d9=b'
    metadata = {
        'started_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'bottle': bottle.describe(), 'command': command, 'process_local_override': 'd3d9=b',
        'timeout_seconds': options.timeout,
        'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
        'inputs_sha256': inputs,
        'input_directories': {'pair': str(pair_dir), 'textures': str(texture_dir)},
        'neutral_stages': {'s3_lightmap': 'black 1x1', 's4_cube': 'black 1x1 cube'},
    }
    transcript, log = results / f'{options.name}.txt', results / f'{options.name}-wine.log'
    with transcript.open('w') as out, log.open('w') as err:
        try:
            completed = subprocess.run(command, stdout=out, stderr=err, env=environment,
                                       timeout=options.timeout)
            metadata['exit_code'] = completed.returncode
        except subprocess.TimeoutExpired:
            metadata.update(exit_code=None, timed_out=True)
    report = parse(transcript.read_text())
    summary = summarize(report)
    checks = verdict(report, summary)
    metadata.update(caps=report['caps'], programs=report['programs'], textures=report['textures'],
                    configurations=report['configurations'], measurements=report['measurements'],
                    result=report['result'], api_failures=report['api_failures'],
                    ratios=summary, conclusion=conclusion(summary), checks=checks,
                    passed=metadata.get('exit_code') == 0 and all(checks.values()))
    (results / f'{options.name}.json').write_text(json.dumps(metadata, indent=2) + '\n')
    print(json.dumps({k: v for k, v in metadata.items() if k != 'measurements'}, indent=2))
    return 0 if metadata['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
