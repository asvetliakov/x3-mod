#!/usr/bin/env python3
"""Consume-only actual-D3D live material routing/Reset/lifetime qualification.

Run under wine_lock.py with X3M_FIXTURE_BOTTLE=X3. The explicit prebuilt EXE
and seam DLL are copied app-locally; no production or fixture build is implicit.
Raw output/readbacks stay in /tmp; one compact result records scoped evidence.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import time

import bottle
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
PROGRAMS = Path('/tmp/x3-shader-sweep/programs')
PROGRAM_NAMES = ('vs_53a0a641107ed76c.bin', 'ps_8759c7838bbc86c2.bin', 'ps_3b94320087e81945.bin', 'ps_ef2bf556f207b8bd.bin',
                 'vs_4944d81dfe531b37.bin', 'ps_ca6bfa4a6cca7e2a.bin', 'ps_3602b05ce11ca6ff.bin')
FRAME_COUNT = 24
ELIGIBLE = {0, 1, 6, 8, 10, 11, 12, 14, 16, 17, 18, 20, 21, 22, 23}
BUMP_FRAMES = set(range(12, 24)) - {17, 23}
MATCHED = set(range(FRAME_COUNT)) - {0, 10, 12, 17, 18, 21, 23}
PIXEL_PROGRAMS = ['8759c7838bbc86c2'] * FRAME_COUNT
VERTEX_PROGRAMS = ['4944d81dfe531b37' if frame in BUMP_FRAMES else '53a0a641107ed76c' for frame in range(FRAME_COUNT)]
for frame in BUMP_FRAMES: PIXEL_PROGRAMS[frame] = 'ca6bfa4a6cca7e2a'
PIXEL_PROGRAMS[19] = '3602b05ce11ca6ff'
PIXEL_PROGRAMS[1] = '3b94320087e81945'
PIXEL_PROGRAMS[9] = 'ef2bf556f207b8bd'

# Explicit source-qualified pair corpus; each pair appears twice, first unseen
# and then with valid prior history. This is independent of runtime admission.
CORPUS_PAIRS = (
    ('53a0a641107ed76c', '8759c7838bbc86c2', False),
    ('53a0a641107ed76c', '63f96eba9eea7880', False),
    ('719856ce0c213220', '593e5dea9b3457d5', False),
    ('719856ce0c213220', '7a0bb00a8070496a', False),
    ('719856ce0c213220', '8d5b2ba0fb4d13bf', False),
    ('719856ce0c213220', 'dab93928f26906f7', False),
    ('badefd5143b3024f', '593e5dea9b3457d5', False),
    ('badefd5143b3024f', '7a0bb00a8070496a', False),
    ('badefd5143b3024f', '8d5b2ba0fb4d13bf', False),
    ('badefd5143b3024f', 'dab93928f26906f7', False),
    ('53a0a641107ed76c', '3b94320087e81945', False),
    ('53a0a641107ed76c', 'e3b7acc16da9932d', False),
    ('719856ce0c213220', '7a14d4dcb28f27e5', False),
    ('719856ce0c213220', '8ab6188a40ca15ea', False),
    ('719856ce0c213220', '8df6143d0e77d92e', False),
    ('719856ce0c213220', 'e16a9806ee3544c3', False),
    ('badefd5143b3024f', '7a14d4dcb28f27e5', False),
    ('badefd5143b3024f', '8ab6188a40ca15ea', False),
    ('badefd5143b3024f', '8df6143d0e77d92e', False),
    ('badefd5143b3024f', 'e16a9806ee3544c3', False),
    ('4944d81dfe531b37', 'ca6bfa4a6cca7e2a', True),
    ('4944d81dfe531b37', '5e0a10fe752b6140', True),
    ('19a246a56e9d9700', '63379470db8d2a86', True),
    ('19a246a56e9d9700', '68915563dd0aac9a', True),
    ('19a246a56e9d9700', 'd086fde54698070c', True),
    ('19a246a56e9d9700', 'f17fffd88d134b04', True),
    ('44c4a41ca92ae2e3', '63379470db8d2a86', True),
    ('44c4a41ca92ae2e3', '68915563dd0aac9a', True),
    ('44c4a41ca92ae2e3', 'd086fde54698070c', True),
    ('44c4a41ca92ae2e3', 'f17fffd88d134b04', True),
    ('53a0a641107ed76c', '462342e3e5781384', False),
    ('53a0a641107ed76c', '827d8d2d617bedce', False),
    ('719856ce0c213220', '02606104fa59fb29', False),
    ('719856ce0c213220', '1d638938d93421b3', False),
    ('719856ce0c213220', 'bd4d51c08486c6e0', False),
    ('719856ce0c213220', 'de2dd381fa64193d', False),
    ('badefd5143b3024f', '02606104fa59fb29', False),
    ('badefd5143b3024f', '1d638938d93421b3', False),
    ('badefd5143b3024f', 'bd4d51c08486c6e0', False),
    ('badefd5143b3024f', 'de2dd381fa64193d', False),
    ('719856ce0c213220', 'db644b73b68c0547', False),
    ('719856ce0c213220', 'ff32b602a271c327', False),
    ('719856ce0c213220', 'f6a501717c3e5ca8', False),
    ('719856ce0c213220', '55826dc176afe464', False),
    ('badefd5143b3024f', 'db644b73b68c0547', False),
    ('badefd5143b3024f', 'ff32b602a271c327', False),
    ('badefd5143b3024f', 'f6a501717c3e5ca8', False),
    ('badefd5143b3024f', '55826dc176afe464', False),
    ('494fe349b8bc12ec', '7c83ed50c9894e44', False),
    ('494fe349b8bc12ec', 'e70adc744a38ca59', False),
    ('4944d81dfe531b37', '0c1f3f0f440e4a0c', True),
    ('4944d81dfe531b37', '64bac8bb307eb896', True),
    ('19a246a56e9d9700', '789449ffd931d23e', True),
    ('19a246a56e9d9700', '4f052209611387f0', True),
    ('19a246a56e9d9700', 'abf3c0fad53456d8', True),
    ('19a246a56e9d9700', 'cf449bcb069aec4f', True),
    ('44c4a41ca92ae2e3', '789449ffd931d23e', True),
    ('44c4a41ca92ae2e3', '4f052209611387f0', True),
    ('44c4a41ca92ae2e3', 'abf3c0fad53456d8', True),
    ('44c4a41ca92ae2e3', 'cf449bcb069aec4f', True),
    ('4944d81dfe531b37', '99153c144030c396', True),
    ('4944d81dfe531b37', 'c1452981fd0bff64', True),
    ('19a246a56e9d9700', 'b0f9313b77cc78ee', True),
    ('19a246a56e9d9700', 'd514bf852d8a9c58', True),
    ('19a246a56e9d9700', 'dff6a3d360603fa2', True),
    ('19a246a56e9d9700', 'f1d14a7dbf7c6173', True),
    ('44c4a41ca92ae2e3', 'b0f9313b77cc78ee', True),
    ('44c4a41ca92ae2e3', 'd514bf852d8a9c58', True),
    ('44c4a41ca92ae2e3', 'dff6a3d360603fa2', True),
    ('44c4a41ca92ae2e3', 'f1d14a7dbf7c6173', True),
 )
IMPLEMENTED_PROGRAMS = {('vs', v) for v, _, _ in CORPUS_PAIRS} | {('ps', p) for _, p, _ in CORPUS_PAIRS}
PROGRAM_NAMES += tuple(sorted(f'{stage}_{identifier}.bin' for stage, identifier in IMPLEMENTED_PROGRAMS
                              if f'{stage}_{identifier}.bin' not in PROGRAM_NAMES))
for pair, (vertex, pixel, bump) in enumerate(CORPUS_PAIRS):
    for repeat in range(2):
        frame = 24 + pair * 2 + repeat
        ELIGIBLE.add(frame)
        if bump: BUMP_FRAMES.add(frame)
        if repeat: MATCHED.add(frame)
        PIXEL_PROGRAMS.append(pixel)
        VERTEX_PROGRAMS.append(vertex)
FRAME_COUNT = len(PIXEL_PROGRAMS)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def validate_case(output, trace_lines, material, taa):
    lines = output.splitlines()
    summary = next((fields(line) for line in lines if line.startswith('RESULT PASS ')), None)
    assert summary and not any(line.startswith('RESULT FAIL') for line in lines), 'fixture did not pass'
    assert int(summary['frames']) == FRAME_COUNT and int(summary['depth_written']) > 0
    assert int(summary['taa_reference_frames']) == (FRAME_COUNT if taa else 0)
    live = {int(fields(line)['frame']): fields(line) for line in lines if line.startswith('LINEAR_LIVE ')}
    motion = {int(fields(line)['frame']): fields(line) for line in lines if line.startswith('MOTION_HASH ')}
    frames, material_frames, release = {}, {}, []
    variants = []
    for line in trace_lines:
        row = fields(line)
        if line.startswith('motion_output_frame '): frames[int(row['frame'])] = row
        elif line.startswith('linear_material_frame '): material_frames[int(row['frame'])] = row
        elif line.startswith('motion_output_release '): release.append(row)
        elif line.startswith('linear_material_variant '): variants.append(row)
    assert set(live) == set(motion) == set(frames) == set(range(FRAME_COUNT)), 'missing frame evidence'
    for frame, row in frames.items():
        assert int(row['routed']) == int(row['depth_routed']) == 1, (frame, 'motion lost')
        assert int(row['matched']) == int(frame in MATCHED), (frame, 'history changed')
        assert int(row['apply_failures']) == int(row['restore_failures']) == 0, (frame, 'state failed')
        assert int(row['taa_resolved']) == int(taa), (frame, 'TAA changed')
        assert int(live[frame]['combined']) == int(material and frame in ELIGIBLE)
        assert live[frame]['ps'] == PIXEL_PROGRAMS[frame] and live[frame]['vs'] == VERTEX_PROGRAMS[frame], (frame, 'wrong material program pair')
    if material:
        assert set(material_frames) == set(range(FRAME_COUNT))
        assert len(variants) == 49 and {(row['kind'], row['original']) for row in variants} == IMPLEMENTED_PROGRAMS, 'combined program inventory differs'
        assert all(int(row['transform']) == 0 and int(row['create'], 16) == 0 for row in variants)
        for frame, row in material_frames.items():
            assert int(row['routed']) == int(frame in ELIGIBLE), (frame, 'combined selection')
            assert int(row['refused']) == int(frame not in ELIGIBLE), (frame, 'material refusal')
            assert int(row['bind_failures']) == 0
            assert int(row['bump_routed']) == int(frame in ELIGIBLE and frame in BUMP_FRAMES), (frame, 'BUMP route witness')
    else:
        assert not material_frames and not variants
    assert len(release) == 1 and int(release[0]['released']) == 1, 'owned objects were not retired'
    return dict(checks=int(summary['checks']), restorations=int(summary['restorations']), frames=FRAME_COUNT,
                motion_pixels=int(summary['motion_pixels']), matched_pixels=int(summary['matched_pixels']),
                depth_written=int(summary['depth_written']), taa_reference_frames=int(summary['taa_reference_frames']),
                held_references=int(release[0]['held']), pixel_programs=[live[i]['ps'] for i in range(FRAME_COUNT)],
                vertex_programs=[live[i]['vs'] for i in range(FRAME_COUNT)],
                rgba=[list(map(float, live[i]['rgba'].split(','))) for i in range(FRAME_COUNT)],
                temporal_hashes=[[motion[i]['motion'], motion[i]['depth']] for i in range(FRAME_COUNT)],
                combined_frames=sorted(ELIGIBLE) if material else [], refused_frames=sorted(set(range(FRAME_COUNT))-ELIGIBLE) if material else [])


def compare_cases(cases):
    for ownership in (0, 1):
        for taa in (0, 1):
            off, on = (cases[f'ownership{ownership}-taa{taa}-material{material}'] for material in (0, 1))
            assert on['temporal_hashes'] == off['temporal_hashes'], 'material route changed RT1/RT2'
            assert on['pixel_programs'] == off['pixel_programs'] == PIXEL_PROGRAMS, 'material positive/negative schedule changed'
            assert on['vertex_programs'] == off['vertex_programs'] == VERTEX_PROGRAMS, 'material vertex schedule changed'
            assert on['held_references'] == off['held_references'] + 49, 'forty-nine additional shader objects not reflected in actual device retirement'
            for frame in range(FRAME_COUNT):
                assert on['rgba'][frame][3] == off['rgba'][frame][3], 'alpha changed'
                if frame not in ELIGIBLE: assert on['rgba'][frame] == off['rgba'][frame], 'refusal changed original material color'
                else: assert min(on['rgba'][frame][:3]) > 1.8 and max(off['rgba'][frame][:3]) == 1., 'combined FP16 witness absent'
    for taa in (0, 1):
        for material in (0, 1):
            a, b = (cases[f'ownership{o}-taa{taa}-material{material}'] for o in (0, 1))
            assert a['rgba'] == b['rgba'] and a['temporal_hashes'] == b['temporal_hashes'], 'ownership model changed outputs'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--programs', type=Path, default=PROGRAMS)
    parser.add_argument('--result', type=Path, default=bottle.results_dir(ROOT, create=False) / 'linear-material-live.json')
    args = parser.parse_args()
    fixture, dll = args.fixture.resolve(), args.dll.resolve()
    programs = [args.programs.resolve() / name for name in PROGRAM_NAMES]
    assert bottle.BOTTLE == 'X3', 'new verification requires X3 bottle'
    assert all(path.is_file() for path in [fixture, dll, *programs]), 'prebuilt inputs or local programs missing'
    raw = Path(tempfile.mkdtemp(prefix='x3-linear-material-live-'))
    report = dict(passed=False, game_launched=False, bottle=bottle.describe(), raw=str(raw),
                  scope='Actual live evaluate_draw across all 70 exact pairs / 49 originals, DEFAULT/BUMPMAP/LOW alternation and shared-VS negatives, five-sampler admission, FP16 color witness, unchanged RT1/RT2, stateblocks, Reset, cached gains and owned shader retirement; ownership 0/1 and TAA off/on. Native Windows untested.',
                  binaries={str(path): sha(path) for path in (fixture, dll)}, local_programs={path.name: sha(path) for path in programs}, cases={})
    args.result.parent.mkdir(parents=True, exist_ok=True)
    try:
        for ownership in (0, 1):
            for taa in (0, 1):
                for material in (0, 1):
                    assert not game_running(), 'game is running'
                    name = f'ownership{ownership}-taa{taa}-material{material}'
                    work = raw / name; work.mkdir()
                    shutil.copy2(fixture, work / 'fixture.exe'); shutil.copy2(dll, work / 'd3d9.dll')
                    env = {key: value for key, value in os.environ.items() if not key.startswith('X3M_')}
                    env.update(X3M_MOTION_OUTPUT='1', X3M_HDR='1', X3M_HDR_TONEMAP='agx', X3M_HDR_DECODE='gamma2.2',
                               X3M_HDR_EXPOSURE='manual', X3M_HDR_EV_MANUAL='0', X3M_HDR_CLAMP='0', X3M_HDR_BLOOM='0',
                               X3M_LINEAR_MATERIALS=str(material), X3M_MATERIAL_DIRECT_GAIN='1', X3M_MATERIAL_EMISSIVE_GAIN='1',
                               X3M_LIGHTMAP_EMISSIVE_GAIN='4', X3M_OWNERSHIP=str(ownership), X3M_TAA=str(taa),
                               X3M_TAA_SENTINEL='1', X3M_TAA_SHARPEN='0', X3M_TAA_MIP_BIAS='0', X3M_SCENE_HOOK='0',
                               X3M_TELEMETRY='1', X3M_MOTION_FRAME_LOG='1', X3M_CAPTURE_START='1', X3M_CAPTURE_FRAMES='0',
                               X3M_MOTION_RT_MODE='perdraw', X3M_STATE_SHADOW='1', WINEDLLOVERRIDES='d3d9=n,b')
                    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=n,b', '--workdir', str(work), str(work / 'fixture.exe'),
                               'Z:' + str(programs[0]), 'Z:' + str(programs[1]), 'linearmaterials', *['Z:' + str(path) for path in programs[2:7]]]
                    start = time.monotonic()
                    with (work / 'stdout.txt').open('w') as out, (work / 'wine.log').open('w') as error:
                        completed = subprocess.run(command, env=env, stdout=out, stderr=error, timeout=180)
                    assert completed.returncode == 0, f'{name}: exit {completed.returncode}; see {work}'
                    logs = list((work / 'x3-modern-captures').glob('session-*.log'))
                    assert len(logs) == 1, f'{name}: missing session log'
                    with logs[0].open() as trace:
                        result = validate_case((work / 'stdout.txt').read_text(), trace, bool(material), bool(taa))
                    result['seconds'] = round(time.monotonic() - start, 3)
                    report['cases'][name] = result
                    print(f'{name}: {result["checks"]} checks, {result["frames"]} frames, held={result["held_references"]}', flush=True)
        compare_cases(report['cases'])
        assert report['binaries'] == {str(path): sha(path) for path in (fixture, dll)}, 'prebuilt inputs changed during qualification'
        assert report['local_programs'] == {path.name: sha(path) for path in programs}, 'local programs changed during qualification'
        report['passed'] = True
        report['checks'] = sum(case['checks'] for case in report['cases'].values())
        report['limitations'] = ['Unknown sampler getter failure and combined creation/bind/restore failures are covered by scripted host control-flow checks, not injected into this GPU script.', 'Native Windows and gameplay appearance/performance remain unverified.']
    finally:
        destination = args.result if report['passed'] else raw / 'failed-result.json'
        destination.write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS cases={len(report["cases"])} checks={report["checks"]} result={args.result}', flush=True)


if __name__ == '__main__':
    main()
