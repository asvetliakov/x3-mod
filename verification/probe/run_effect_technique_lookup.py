#!/usr/bin/env python3
"""Technique-lookup microbenchmark: what the engine's per-draw technique lookup costs; no game launch.

Per draw the engine calls `GetTechniqueByName` (0x004c0bfa) with a literal
material technique name (DEFAULT / BUMPMAP / BUMPMAP_LOW) and then
`SetTechnique` (0x004c0c34), whether or not the technique changed
(effect-pass-loop.md section 7, xt-materials.md). Both live inside the bundled
`prepare` bucket measured in game at 6.35-6.38 us per draw. This fixture prices
those two calls, plus the `End`/`Begin` bookends for reference, on the game's own
compiled effects under the game's own native `d3dx9_37.dll`, so the value of a
per-(effect, name) technique-handle cache can be computed before building one.

Measurements per effect, >=100k timed calls each after warm-up. The bottle's QPC
ticks at 0.1 us, coarser than these calls, so the calls are timed in batches of
`--batch` between one QPC pair and the median/p90 are taken over the per-batch
per-call means; an empty-body batch loop is the baseline and is subtracted:

  baseline, gtbn:<NAME> per declared engine name, settech_same:<NAME>,
  settech_alt:<A|B>, beginend:<NAME>

The effects are read out of the bottle's own CAT/DAT archives at run time into a
scratch directory outside the repository (deleted afterwards). No game bytes are
copied into the repository; only byte counts and SHA-256 are recorded. Which
d3dx9_37 answered is proven from the mapped PE image (`wine_builtin`), and a run
whose image is not the native redistributable fails closed.

These are diagnostic microbenchmark timings on an idle device, not game FPS.

Run it under the Wine lock:
  X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \\
      python3 verification/probe/run_effect_technique_lookup.py
"""
import argparse
import datetime
import hashlib
import json
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
from run_effect_beginpass import extract_effect  # noqa: E402  (same archive decode)

PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build'
SOURCE = PROBE / 'effect_technique_lookup_fixture.cpp'
EXE = BUILD / 'effect_technique_lookup_fixture.exe'
# The fixture toolchain of AGENTS.md "Code rules": MinGW i686, SSE2, the
# four-byte incoming stack contract (the effect_beginpass_fixture build).
BUILD_COMMAND = ['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                 '-static', '-static-libgcc', '-static-libstdc++',
                 '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
                 str(SOURCE), '-o', str(EXE), '-ldxguid', '-luser32']

# The busy Argon view's hull effect first, then its two-sided twin and two
# standard hull effects with a third technique (more names to search).
EFFECTS = ('shader/3_0/argon.fb', 'shader/3_0/argon2s.fb',
           'shader/3_0/standard_lighting.fb', 'shader/3_0/xt_standard_lighting.fb')
DRAWS_PER_FRAME = 825  # run 38 B (run113) busy-view draw count


def fields(line):
    return dict(token.split('=', 1) for token in line.split()[1:] if '=' in token)


def parse_output(text, iterations):
    parsed = {'module': None, 'fixture': None, 'device': None, 'measures': {}}
    for line in text.splitlines():
        if line.startswith('MODULE '):
            f = fields(line)
            parsed['module'] = {'path': f['path'], 'image_size': int(f['image_size']), 'stamp': f['stamp'],
                                'exports': int(f['exports']), 'wine_builtin': int(f['wine_builtin'])}
        elif line.startswith('DEVICE '):
            parsed['device'] = fields(line)
        elif line.startswith('FIXTURE '):
            f = fields(line)
            parsed['fixture'] = {'effect_bytes': int(f['bytes']), 'parameters': int(f['parameters']),
                                 'techniques': int(f['techniques']), 'engine_techniques': f['engine_techniques'],
                                 'iterations': int(f['iterations']), 'repetitions': int(f['repetitions']),
                                 'warmup': int(f['warmup']), 'batch': int(f['batch'])}
        elif line.startswith('MEASURE '):
            f = fields(line)
            if int(f['iterations']) < iterations:
                raise ValueError(f'{f["measure"]}: {f["iterations"]} iterations, expected {iterations}')
            parsed['measures'].setdefault(f['measure'], []).append(
                {'rep': int(f['rep']), 'detail': f['detail'], 'batches': int(f['batches']),
                 'us_median': float(f['us_median']), 'us_p90': float(f['us_p90']),
                 'us_mean': float(f['us_mean']), 'us_p10': float(f['us_p10']), 'us_p99': float(f['us_p99'])})
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
    if 'baseline' not in parsed['measures']:
        raise ValueError('no baseline measurement')
    for name, records in parsed['measures'].items():
        if len(records) != parsed['fixture']['repetitions']:
            raise ValueError(f'{name}: {len(records)} repetitions')
    return parsed


def summarize(parsed):
    """Median of the per-repetition medians (and of the p90s), baseline subtracted."""
    if parsed['module']['wine_builtin'] != 0:
        raise ValueError('a Wine builtin d3dx9_37 answered; the native DLL is required')
    raw = {}
    for name, records in parsed['measures'].items():
        raw[name] = {'detail': records[0]['detail'],
                     'us_median': round(statistics.median(r['us_median'] for r in records), 4),
                     'us_p90': round(statistics.median(r['us_p90'] for r in records), 4),
                     'us_mean': round(statistics.median(r['us_mean'] for r in records), 4),
                     'us_p99': round(statistics.median(r['us_p99'] for r in records), 4),
                     'rep_medians': [round(r['us_median'], 4) for r in records]}
    base_median = raw['baseline']['us_median']
    base_p90 = raw['baseline']['us_p90']
    for name, record in raw.items():
        if name == 'baseline':
            continue
        record['net_us_median'] = round(record['us_median'] - base_median, 4)
        record['net_us_p90'] = round(record['us_p90'] - base_p90, 4)
        record['ms_per_frame_median'] = round(record['net_us_median'] * DRAWS_PER_FRAME / 1000.0, 4)
    return raw


def cache_saving(raw):
    """What a perfect per-(effect, name) handle cache removes, per draw and per frame.

    GetTechniqueByName goes away entirely; SetTechnique goes away only in the
    redundant case (the handle already current). The lookup figure is the median
    over the effect's declared engine names.
    """
    lookups = [r['net_us_median'] for name, r in raw.items() if name.startswith('gtbn:')]
    same = [r['net_us_median'] for name, r in raw.items() if name.startswith('settech_same:')]
    per_draw = statistics.median(lookups) + (same[0] if same else 0.0)
    return {'gtbn_us_median': round(statistics.median(lookups), 4),
            'settech_same_us_median': round(same[0], 4) if same else None,
            'saved_us_per_draw': round(per_draw, 4),
            'saved_ms_per_frame': round(per_draw * DRAWS_PER_FRAME / 1000.0, 4)}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run_effect(run_dir, effect_file, iterations, repetitions, batch, log):
    command = [bottle.WINE] + bottle.wine_args() + ['--workdir', str(run_dir), '--dll', 'd3dx9_37=n',
                                                    str(run_dir / EXE.name), effect_file,
                                                    str(iterations), str(repetitions), str(batch)]
    log.write(f'==== {effect_file} d3dx9_37=n\n')
    log.flush()
    completed = subprocess.run(command, stdout=subprocess.PIPE, stderr=log, text=True, timeout=7200)
    text = completed.stdout
    (run_dir / f'stdout-{effect_file}.txt').write_text(text)
    if completed.returncode != 0:
        raise SystemExit(f'{effect_file}: exit {completed.returncode}\n{text[-2000:]}')
    return parse_output(text, iterations)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--iterations', type=int, default=100000, help='timed calls per measurement and repetition')
    parser.add_argument('--repetitions', type=int, default=3)
    parser.add_argument('--batch', type=int, default=100, help='calls per QPC pair (must divide --iterations)')
    parser.add_argument('--effects', nargs='+', default=list(EFFECTS), help='archive paths of the compiled effects')
    parser.add_argument('--out', type=Path, default=None, help='result file (the Wine log goes beside it as <stem>-wine.log)')
    parser.add_argument('--keep', action='store_true', help='keep the scratch run directory (it holds game bytes)')
    args = parser.parse_args()
    if args.iterations < 100000:
        parser.error('at least 100000 iterations')
    if args.batch < 2 or args.iterations % args.batch:
        parser.error('--batch must be >= 2 and divide --iterations')
    if len(args.effects) < 3:
        parser.error('at least three effects')
    from game_guard import game_running
    assert not game_running(), 'the game is running'

    out = (args.out or (bottle.results_dir(ROOT) / 'effect-technique-lookup.json')).resolve()
    BUILD.mkdir(parents=True, exist_ok=True)
    subprocess.run(BUILD_COMMAND, check=True)
    game = bottle.game_dir()
    dll = game / 'd3dx9_37.dll'
    if not dll.is_file():
        raise SystemExit(f'{dll} not found')

    run_dir = Path(tempfile.mkdtemp(prefix='x3-effect-technique-lookup-'))
    result = {'generated': datetime.datetime.now().isoformat(timespec='seconds'),
              'bottle': bottle.describe(),
              'fixture': {'source': str(SOURCE.relative_to(ROOT)), 'exe_sha256': sha(EXE),
                          'iterations': args.iterations, 'repetitions': args.repetitions, 'batch': args.batch},
              'd3dx9_37_game_file': {'path': str(dll), 'bytes': dll.stat().st_size, 'sha256': sha(dll)},
              'draws_per_frame': DRAWS_PER_FRAME,
              'note': ('Median of the per-repetition medians (and of the per-repetition p90s). Each repetition times '
                       '`iterations` calls in batches of `batch` between one QPC pair (the bottle QPC ticks at 0.1 us, '
                       'coarser than a single call) after a warm-up, and the per-repetition median/p90 are over the '
                       'per-batch per-call means. `net_*` subtracts the empty-body batch loop baseline. No state '
                       'manager is installed: none '
                       'of these calls reaches the device. Diagnostic microbenchmark timings on an idle synthetic '
                       'device, not game FPS.'),
              'effects': {}}
    try:
        shutil.copy(EXE, run_dir / EXE.name)
        shutil.copy(dll, run_dir / 'd3dx9_37.dll')
        with out.with_name(out.stem + '-wine.log').open('w') as log:
            for index, archive_path in enumerate(args.effects):
                catalogue, blob = extract_effect(game, archive_path)
                effect_file = f'effect{index}.fb'
                (run_dir / effect_file).write_bytes(blob)
                parsed = run_effect(run_dir, effect_file, args.iterations, args.repetitions, args.batch, log)
                raw = summarize(parsed)
                result['effects'][archive_path] = {
                    'catalogue': catalogue, 'bytes': len(blob), 'sha256': hashlib.sha256(blob).hexdigest(),
                    'module': parsed['module'], 'device': parsed['device'], 'run': parsed['fixture'],
                    'measures': raw, 'cache_saving': cache_saving(raw)}
                (run_dir / effect_file).unlink()
                out.write_text(json.dumps(result, indent=1) + '\n')
    finally:
        if args.keep:
            print('kept', run_dir)
        else:
            shutil.rmtree(run_dir, ignore_errors=True)

    for archive_path, record in result['effects'].items():
        print(f'{archive_path} ({record["catalogue"]}, {record["bytes"]} bytes, '
              f'{record["run"]["techniques"]} techniques: {record["run"]["engine_techniques"]})')
        base = record['measures']['baseline']
        print(f'  {"baseline":<28} {base["us_median"]:7.4f} us median  {base["us_p90"]:7.4f} p90  (empty batch loop)')
        for name, m in record['measures'].items():
            if name == 'baseline':
                continue
            print(f'  {name:<28} {m["net_us_median"]:7.3f} us net median  {m["net_us_p90"]:7.3f} net p90  '
                  f'{m["ms_per_frame_median"]:6.3f} ms/frame at {DRAWS_PER_FRAME} draws')
        saving = record['cache_saving']
        print(f'  perfect handle cache saves {saving["saved_us_per_draw"]:.3f} us/draw = '
              f'{saving["saved_ms_per_frame"]:.3f} ms/frame')
    print('wrote', out if ROOT not in out.parents else out.relative_to(ROOT))


if __name__ == '__main__':
    main()
