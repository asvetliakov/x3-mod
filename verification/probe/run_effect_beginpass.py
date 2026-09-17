#!/usr/bin/env python3
"""Native-BeginPass attribution: time the game's own D3DX on the game's own effect; no game launch.

Six measurements: three setter regimes x {native d3dx9_37, Wine builtin d3dx9_37}.

  unchanged  every settable top-level parameter is written again with the value
             it already holds before each BeginPass
  changed    the same parameters are written with a value that differs every
             iteration
  none       no Set* between passes

Regime (unchanged) minus regime (none) is the answer to "does native D3DX dirty
a parameter on a same-value Set*": equal callback counts and equal cost mean it
compares values; higher counts/cost mean it does not, and a setter-dedup wrapper
has exactly that much to win per pass
(docs/architecture/effect-pass-replay.md, "Assessment after the classification
and environment experiments").

The effect is `shader/3_0/argon.fb`, the busy Argon view's hull effect, read out
of the bottle's own CAT/DAT archives at run time into a scratch directory
outside the repository (deleted afterwards). No game bytes are copied into the
repository; only the byte count and the SHA-256 are recorded.

`--dll d3dx9_37=n|b` selects which implementation answers; which one actually
loaded is proven from the mapped PE image (`image_size`, `stamp`,
`wine_builtin`), as `src/proxy/proxy_identity.cpp` does, and a case whose image
does not match its request fails.

Run it under the Wine lock:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \\
      python3 verification/probe/run_effect_beginpass.py
"""
import argparse
import datetime
import hashlib
import json
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(ROOT / 'tools/analysis'))
import bottle  # noqa: E402

PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build'
SOURCE = PROBE / 'effect_beginpass_fixture.cpp'
EXE = BUILD / 'effect_beginpass_fixture.exe'
# The fixture toolchain of AGENTS.md "Code rules": MinGW i686, SSE2, the
# four-byte incoming stack contract.
BUILD_COMMAND = ['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                 '-static', '-static-libgcc', '-static-libstdc++',
                 '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
                 str(SOURCE), '-o', str(EXE), '-ldxguid', '-luser32']

EFFECT = 'shader/3_0/argon.fb'
REGIMES = ('unchanged', 'changed', 'none')
# request -> the mapped image the case must report (fail closed on a mismatch)
CASES = (('native', 'n', 0), ('builtin', 'b', 1))
CALLBACKS = ('render_state', 'sampler_state', 'texture', 'texture_stage', 'vertex_shader',
             'pixel_shader', 'shader_constant', 'constant_registers', 'other', 'total')


def extract_effect(game, virtual_path=EFFECT):
    """The compiled effect bytes from the bottle's archives (the effect_passes.py decode)."""
    from inspect_x3 import read_catalogue
    catalogues = sorted([*game.glob('[0-9][0-9].cat'), *game.glob('addon/[0-9][0-9].cat')])
    found = None
    for cat in catalogues:  # ascending, addon last: the final occurrence is what the game loads
        for entry in read_catalogue(cat):
            if entry['path'] == virtual_path:
                found = (cat, entry)
    if not found:
        raise SystemExit(f'{virtual_path} not found in {game}')
    cat, entry = found
    with cat.with_suffix('.dat').open('rb') as stream:
        stream.seek(entry['offset'])
        data = bytes(value ^ 0x33 for value in stream.read(entry['size']))
    if data[:4] != b'\x01\x09\xff\xfe':
        raise SystemExit(f'{virtual_path} is not a compiled D3DX effect')
    return str(cat.relative_to(game)), data


def fields(line):
    return dict(token.split('=', 1) for token in line.split()[1:] if '=' in token)


def parse_output(text):
    """The fixture's stdout as a record; raises ValueError on anything unusable."""
    parsed = {'module': None, 'fixture': None, 'device': None, 'regimes': {}, 'checks': None}
    for line in text.splitlines():
        if line.startswith('MODULE '):
            f = fields(line)
            parsed['module'] = {'path': f['path'], 'image_size': int(f['image_size']),
                                'stamp': f['stamp'], 'exports': int(f['exports']),
                                'wine_builtin': int(f['wine_builtin'])}
        elif line.startswith('DEVICE '):
            parsed['device'] = fields(line)
        elif line.startswith('FIXTURE '):
            f = fields(line)
            parsed['fixture'] = {'technique': f['technique'], 'technique_source': f['technique_source'],
                                 'pass': f['pass'], 'passes': int(f['passes']),
                                 'parameters': int(f['parameters']), 'settable': int(f['settable']),
                                 'skipped': int(f['skipped']), 'iterations': int(f['iterations']),
                                 'repetitions': int(f['repetitions']), 'warmup': int(f['warmup']),
                                 'effect_bytes': int(f['bytes'])}
        elif line.startswith('REGIME '):
            f = fields(line)
            record = {'rep': int(f['rep']), 'iterations': int(f['iterations']),
                      'us_median': float(f['us_median']), 'us_mean': float(f['us_mean']),
                      'us_p05': float(f['us_p05']), 'us_p95': float(f['us_p95']),
                      'setters_us_mean': float(f['setters_us_mean']),
                      'callbacks': {name: float(f['cb_' + name]) for name in CALLBACKS}}
            parsed['regimes'].setdefault(f['name'], []).append(record)
        elif line.startswith('RESULT '):
            f = fields(line)
            parsed['checks'] = int(f['checks'])
            parsed['status'] = f['status']
            if f['status'] != 'pass':
                raise ValueError('fixture failed: ' + f.get('reason', '?'))
    if parsed.get('status') != 'pass':
        raise ValueError('no RESULT status=pass line')
    for key in ('module', 'fixture'):
        if not parsed[key]:
            raise ValueError(f'no {key.upper()} line')
    missing = [name for name in REGIMES if name not in parsed['regimes']]
    if missing:
        raise ValueError('missing regimes: ' + ','.join(missing))
    for name, records in parsed['regimes'].items():
        if len(records) != parsed['fixture']['repetitions']:
            raise ValueError(f'{name}: {len(records)} repetitions, expected {parsed["fixture"]["repetitions"]}')
        if any(record['iterations'] < 10000 for record in records):
            raise ValueError(f'{name}: fewer than 10000 iterations')
    return parsed


def summarize(parsed, expected_builtin=None):
    """Median of the repetition medians per regime, with the callback counts.

    `expected_builtin` fails the case closed when the mapped image is not the
    implementation the DLL override asked for.
    """
    if expected_builtin is not None and parsed['module']['wine_builtin'] != expected_builtin:
        raise ValueError('wrong d3dx9_37 loaded: wine_builtin=%d, expected %d'
                         % (parsed['module']['wine_builtin'], expected_builtin))
    summary = {}
    for name in REGIMES:
        records = parsed['regimes'][name]
        callbacks = {key: round(statistics.median(r['callbacks'][key] for r in records), 3) for key in CALLBACKS}
        summary[name] = {
            'us_median': round(statistics.median(r['us_median'] for r in records), 4),
            'us_mean': round(statistics.median(r['us_mean'] for r in records), 4),
            'us_p05': round(statistics.median(r['us_p05'] for r in records), 4),
            'us_p95': round(statistics.median(r['us_p95'] for r in records), 4),
            'setters_us_mean': round(statistics.median(r['setters_us_mean'] for r in records), 4),
            'rep_medians': [round(r['us_median'], 4) for r in records],
            'callbacks': callbacks}
    if parsed['regimes']['changed'] and summary['changed']['callbacks']['total'] <= 0:
        raise ValueError('regime changed issued no state-manager callback')
    summary['deltas'] = {
        'unchanged_minus_none_us': round(summary['unchanged']['us_median'] - summary['none']['us_median'], 4),
        'changed_minus_none_us': round(summary['changed']['us_median'] - summary['none']['us_median'], 4),
        'unchanged_minus_none_callbacks': round(summary['unchanged']['callbacks']['total']
                                                - summary['none']['callbacks']['total'], 3),
        'same_value_set_dirties': summary['unchanged']['callbacks']['total'] > summary['none']['callbacks']['total'] + 0.5}
    return summary


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run_case(request, override, run_dir, effect_name, technique, iterations, repetitions, log):
    command = [bottle.WINE] + bottle.wine_args() + ['--workdir', str(run_dir), '--dll', f'd3dx9_37={override}',
                                                    str(run_dir / EXE.name), effect_name, technique,
                                                    str(iterations), str(repetitions)]
    log.write(f'==== {request} d3dx9_37={override}\n')
    log.flush()
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=log, text=True, timeout=3600)
    text = completed.stdout
    (run_dir / f'stdout-{request}.txt').write_text(text)
    if completed.returncode != 0:
        raise SystemExit(f'{request}: exit {completed.returncode}\n{text[-2000:]}')
    return parse_output(text)


def row(case, regime, summary):
    c = summary[regime]['callbacks']
    return (f'{case:<8} {regime:<10} {summary[regime]["us_median"]:7.3f} us/BeginPass   callbacks/pass '
            f'total={c["total"]:6.2f} rs={c["render_state"]:.2f} ss={c["sampler_state"]:.2f} tex={c["texture"]:.2f} '
            f'tss={c["texture_stage"]:.2f} vs={c["vertex_shader"]:.2f} ps={c["pixel_shader"]:.2f} '
            f'const={c["shader_constant"]:.2f} ({c["constant_registers"]:.1f} reg) other={c["other"]:.2f}')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--iterations', type=int, default=10000, help='timed BeginPass calls per regime and repetition')
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--technique', default='DEFAULT', help='technique to time (falls back to the first valid one)')
    parser.add_argument('--effect', default=EFFECT, help='archive path of the compiled effect')
    parser.add_argument('--out', type=Path, default=None, help='result file (the Wine log goes beside it as <stem>-wine.log)')
    parser.add_argument('--keep', action='store_true', help='keep the scratch run directory (it holds game bytes)')
    args = parser.parse_args()
    if args.iterations < 10000:
        parser.error('at least 10000 iterations')
    from game_guard import game_running
    assert not game_running(), 'the game is running'

    out = (args.out or (bottle.results_dir(ROOT) / 'effect-beginpass.json')).resolve()
    BUILD.mkdir(parents=True, exist_ok=True)
    subprocess.run(BUILD_COMMAND, check=True)
    game = bottle.game_dir()
    catalogue, blob = extract_effect(game, args.effect)
    dll = game / 'd3dx9_37.dll'
    if not dll.is_file():
        raise SystemExit(f'{dll} not found')

    # Scratch directory outside the repository: it holds the extracted effect
    # and a copy of the game's D3DX, neither of which may land in the tree.
    run_dir = Path(tempfile.mkdtemp(prefix='x3-effect-beginpass-'))
    result = {'generated': datetime.datetime.now().isoformat(timespec='seconds'),
              'bottle': bottle.describe(),
              'fixture': {'source': str(SOURCE.relative_to(ROOT)), 'exe_sha256': sha(EXE)},
              'effect': {'archive_path': args.effect, 'catalogue': catalogue, 'bytes': len(blob),
                         'sha256': hashlib.sha256(blob).hexdigest()},
              'd3dx9_37_game_file': {'path': str(dll), 'bytes': dll.stat().st_size, 'sha256': sha(dll)},
              'note': ('Median of the per-repetition medians; each repetition times `iterations` BeginPass calls with '
                       'QPC around the single call, after a 500-iteration warm-up. Callback counts are per BeginPass, '
                       'counted only inside the timed call, through a forwarding ID3DXEffectStateManager (the game '
                       'installs one too). Begin uses D3DXFX_DONOTSAVESTATE and CommitChanges is never called, as the '
                       'game does.'),
              'cases': {}}
    try:
        shutil.copy(EXE, run_dir / EXE.name)
        shutil.copy(dll, run_dir / 'd3dx9_37.dll')
        effect_name = 'effect.fb'
        (run_dir / effect_name).write_bytes(blob)
        with out.with_name(out.stem + '-wine.log').open('w') as log:
            for request, override, expected_builtin in CASES:
                parsed = run_case(request, override, run_dir, effect_name, args.technique,
                                  args.iterations, args.repetitions, log)
                summary = summarize(parsed, expected_builtin)
                result['cases'][request] = {'dll_override': f'd3dx9_37={override}', 'module': parsed['module'],
                                            'device': parsed['device'], 'run': parsed['fixture'], 'regimes': summary}
                out.write_text(json.dumps(result, indent=2) + '\n')
    finally:
        if args.keep:
            print('kept', run_dir)
        else:
            shutil.rmtree(run_dir, ignore_errors=True)
    run = result['cases']['native']['run']
    print(f'effect {args.effect} ({catalogue}, {len(blob)} bytes), technique {run["technique"]}/{run["pass"]} '
          f'[{run["technique_source"]}], {run["settable"]} of {run["parameters"]} parameters set per iteration, '
          f'{args.iterations} iterations x {args.repetitions}')
    for request, _, _ in CASES:
        for regime in REGIMES:
            print(row(request, regime, result['cases'][request]['regimes']))
    for request, _, _ in CASES:
        deltas = result['cases'][request]['regimes']['deltas']
        print(f'{request:<8} unchanged-none {deltas["unchanged_minus_none_us"]:+.3f} us, '
              f'{deltas["unchanged_minus_none_callbacks"]:+.2f} callbacks/pass; '
              f'same-value Set* dirties: {deltas["same_value_set_dirties"]}')
    print('wrote', out.relative_to(ROOT))


if __name__ == '__main__':
    main()
