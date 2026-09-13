#!/usr/bin/env python3
"""Run the retained-CSO BloomPass timing fixture under the shared Wine lock."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import subprocess
import time

import bottle
import run_bloom_filter as filtering
import run_bloom_pass as bloom_fixture
from game_guard import game_running

DIMENSIONS = ((1280, 768), (1279, 767))
METRICS = ('full', 'extract_first_draw')
PAIRS = 5
WARMUPS = 3
FLOAT_TOLERANCE = 5.1e-7  # six digits after the decimal point in fixture output


def artifact_inputs(executable: Path, artifacts: Path):
    executable = executable.resolve()
    if not executable.is_file():
        raise ValueError('Timing executable is missing')
    paths = [executable]
    expected = {name + '.cso' for name in bloom_fixture.NAMES}
    for label in ('baseline', 'new'):
        directory = (artifacts / label).resolve()
        actual = {path.name for path in directory.glob('*.cso') if path.is_file()}
        if actual != expected:
            raise ValueError(f'{label} CSO set mismatch')
        paths.extend(directory / name for name in sorted(expected))
    record_path = (artifacts / 'artifacts.json').resolve()
    record = json.loads(record_path.read_bytes())
    kernels = record.get('kernels', [])
    expected_kernels = {name[:-3] for name in bloom_fixture.NAMES[1:10]}
    if record.get('passed') is not True or record.get('inputs_stable') is not True \
            or 'promotion' in record or len(kernels) != 9 \
            or {item.get('name') for item in kernels} != expected_kernels:
        raise ValueError('Artifact record is not a complete passed stable stage')
    inventory = {str(path): filtering.digest(path) for path in paths[1:]}
    if record.get('csos') != inventory:
        raise ValueError('Artifact record CSO hashes differ from retained bundles')
    return [*paths, record_path]


def _near(actual, expected):
    return math.isfinite(actual) and abs(actual - expected) <= FLOAT_TOLERANCE


def parse_log(text: str, old_directory: str, new_directory: str):
    lines = text.splitlines()
    if not lines or any(not line for line in lines):
        raise ValueError('Empty or blank timing record')
    bundles = []
    caps = []
    creates = []
    samples = []
    summaries = []
    terminals = []
    bundle_pattern = re.compile(r'BUNDLE label=(old|new) directory=(.+)')
    caps_pattern = re.compile(
        r'CAPS pixel_shader_version=([0-9a-f]{8}) max_ps30_instruction_slots=(\d+) qpc_frequency=(\d+)')
    create_pattern = re.compile(
        r'CREATE_PS metric=(full|extract) bundle=(old|new) accepted=([01]) '
        r'attach_hr=([0-9a-f]{8}) programs_hr=([0-9a-f]{8})')
    sample_pattern = re.compile(
        r'SAMPLE metric=(full|extract_first_draw) width=(\d+) height=(\d+) pair=(\d+) '
        r'order=(old-new|new-old) old_ticks=(\d+) old_ms=([^ ]+) new_ticks=(\d+) new_ms=([^ ]+)')
    summary_pattern = re.compile(
        r'SUMMARY metric=(full|extract_first_draw) width=(\d+) height=(\d+) '
        r'warmup_pairs=(\d+) measured_pairs=(\d+) old_median_ms=([^ ]+) '
        r'new_median_ms=([^ ]+) ratio=([^ ]+)')
    terminal = 'RESULT PASS dimensions=2 metrics=2 warmup_pairs=3 measured_pairs=5'
    for line in lines:
        if match := bundle_pattern.fullmatch(line): bundles.append(match.groups()); continue
        if match := caps_pattern.fullmatch(line): caps.append(match.groups()); continue
        if match := create_pattern.fullmatch(line): creates.append(match.groups()); continue
        if match := sample_pattern.fullmatch(line): samples.append(match.groups()); continue
        if match := summary_pattern.fullmatch(line): summaries.append(match.groups()); continue
        if line.startswith('RESULT '): terminals.append(line); continue
        raise ValueError(f'Unexpected timing record: {line}')
    if bundles != [('old', old_directory), ('new', new_directory)]:
        raise ValueError('Bundle identity records mismatch')
    if len(caps) != 1:
        raise ValueError('Missing or duplicate caps record')
    version, slots, frequency = caps[0]
    frequency = int(frequency)
    if int(version, 16) < 0xffff0300 or int(slots) <= 0 or frequency <= 0:
        raise ValueError('Invalid timing capabilities')
    expected_creates = [('full', 'old'), ('full', 'new'), ('extract', 'old'), ('extract', 'new')] * 2
    if [(metric, bundle) for metric, bundle, *_ in creates] != expected_creates:
        raise ValueError('Shader creation records mismatch')
    if any(accepted != '1' or attach != '00000000' or programs != '00000000'
           for _, _, accepted, attach, programs in creates):
        raise ValueError('Retained shader creation failed')
    expected_keys = {(metric, width, height, pair)
                     for width, height in DIMENSIONS for metric in METRICS for pair in range(PAIRS)}
    parsed_samples = []
    ticks_by_key = {}
    for metric, width, height, pair, order, old_ticks, old_ms, new_ticks, new_ms in samples:
        key = (metric, int(width), int(height), int(pair))
        if key not in expected_keys or key in ticks_by_key:
            raise ValueError('Unexpected or duplicate sample')
        if order != ('old-new' if key[3] % 2 == 0 else 'new-old'):
            raise ValueError('Paired sample order mismatch')
        old_ticks, new_ticks = int(old_ticks), int(new_ticks)
        try: old_ms, new_ms = float(old_ms), float(new_ms)
        except ValueError as error: raise ValueError('Malformed sample milliseconds') from error
        if old_ticks <= 0 or new_ticks <= 0 or old_ms <= 0 or new_ms <= 0:
            raise ValueError('Nonpositive timing sample')
        if not _near(old_ms, old_ticks * 1000 / frequency) \
                or not _near(new_ms, new_ticks * 1000 / frequency):
            raise ValueError('QPC tick/millisecond mismatch')
        ticks_by_key[key] = (old_ticks, new_ticks)
        parsed_samples.append(dict(metric=metric, width=key[1], height=key[2], pair=key[3],
                                   order=order, old_ticks=old_ticks, new_ticks=new_ticks,
                                   old_ms=old_ms, new_ms=new_ms))
    if set(ticks_by_key) != expected_keys or len(samples) != len(expected_keys):
        raise ValueError('Incomplete timing samples')
    summary_by_key = {}
    for metric, width, height, warmups, pairs, old_ms, new_ms, ratio in summaries:
        key = (metric, int(width), int(height))
        if key not in {(m, w, h) for w, h in DIMENSIONS for m in METRICS} or key in summary_by_key:
            raise ValueError('Unexpected or duplicate timing summary')
        if int(warmups) != WARMUPS or int(pairs) != PAIRS:
            raise ValueError('Timing sample-count summary mismatch')
        try: old_ms, new_ms, ratio = map(float, (old_ms, new_ms, ratio))
        except ValueError as error: raise ValueError('Malformed timing summary') from error
        old_values = sorted(ticks_by_key[key + (pair,)][0] for pair in range(PAIRS))
        new_values = sorted(ticks_by_key[key + (pair,)][1] for pair in range(PAIRS))
        computed_old = old_values[PAIRS // 2] * 1000 / frequency
        computed_new = new_values[PAIRS // 2] * 1000 / frequency
        computed_ratio = new_values[PAIRS // 2] / old_values[PAIRS // 2]
        if old_ms <= 0 or new_ms <= 0 or ratio <= 0 or not _near(old_ms, computed_old) \
                or not _near(new_ms, computed_new) or not _near(ratio, computed_ratio):
            raise ValueError('Timing summary arithmetic mismatch')
        summary_by_key[key] = dict(metric=metric, width=key[1], height=key[2],
                                   old_median_ticks=old_values[PAIRS // 2],
                                   new_median_ticks=new_values[PAIRS // 2],
                                   old_median_ms=computed_old, new_median_ms=computed_new,
                                   ratio=computed_ratio)
    if len(summary_by_key) != len(DIMENSIONS) * len(METRICS):
        raise ValueError('Incomplete timing summaries')
    if terminals != [terminal] or lines[-1] != terminal:
        raise ValueError('Missing, duplicate, failed, or nonterminal result')
    return dict(pixel_shader_version=version, max_ps30_instruction_slots=int(slots),
                qpc_frequency=frequency, create_ps_records=len(creates),
                samples=parsed_samples, summaries=list(summary_by_key.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    if bottle.BOTTLE != 'X3':
        raise RuntimeError('Set X3M_FIXTURE_BOTTLE=X3 explicitly')
    filtering.require_runner_lock()
    if game_running():
        raise RuntimeError('Game running; timing fixture postponed')
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise ValueError('Output directory must be empty')
    inputs = artifact_inputs(args.exe, args.artifacts.resolve())
    before = {str(path): filtering.digest(path) for path in inputs}
    old = (args.artifacts.resolve() / 'baseline')
    new = (args.artifacts.resolve() / 'new')
    windows = lambda path: 'Z:' + str(path.resolve())
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(output),
               str(args.exe.resolve()), windows(old), windows(new)]
    report = dict(schema=1, passed=False, phase='running', bottle=bottle.describe(),
                  command=command, executable=str(args.exe.resolve()), artifacts=str(args.artifacts.resolve()),
                  timing_semantics='QPC wall time bounded by completed D3D EVENT fences; CPU + driver + GPU completion, not pure GPU time',
                  warmup_pairs=WARMUPS, measured_pairs=PAIRS, inputs_before=before)
    summary = output / 'summary.json'
    summary.write_text(json.dumps(report, indent=2) + '\n')
    started = time.monotonic()
    try:
        with (output / 'stdout.txt').open('w') as stdout, (output / 'stderr.txt').open('w') as stderr:
            result = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=180)
        report['exit_code'] = result.returncode
        log = (output / 'stdout.txt').read_text(errors='replace')
        if result.returncode:
            raise RuntimeError(f'Timing fixture exited {result.returncode}')
        report['measurements'] = parse_log(log, windows(old), windows(new))
        report.update(passed=True, phase='complete')
    except Exception as error:
        report.update(passed=False, phase='failed', error=str(error))
        raise
    finally:
        report['elapsed_seconds'] = time.monotonic() - started
        report['stdout_sha256'] = filtering.digest(output / 'stdout.txt') if (output / 'stdout.txt').exists() else None
        report['stderr_sha256'] = filtering.digest(output / 'stderr.txt') if (output / 'stderr.txt').exists() else None
        report['inputs_after'] = {str(path): filtering.digest(path) for path in inputs}
        report['inputs_unchanged'] = report['inputs_after'] == before
        if not report['inputs_unchanged']:
            report.update(passed=False, phase='failed', error='Timing inputs changed during execution')
        summary.write_text(json.dumps(report, indent=2, allow_nan=False) + '\n')
    if not report['inputs_unchanged']:
        raise RuntimeError('Timing inputs changed during execution')
    print(json.dumps(dict(passed=True, summary=str(summary))))


if __name__ == '__main__':
    main()
