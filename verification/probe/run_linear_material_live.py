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
import math
import os
import re
import shutil
import subprocess
import tempfile
import time

import bottle
import linear_glass_live_reference as glass_live
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
PROGRAMS = Path('/tmp/x3-shader-sweep/programs')
PROGRAM_NAMES = ('vs_53a0a641107ed76c.bin', 'ps_8759c7838bbc86c2.bin', 'ps_3b94320087e81945.bin', 'ps_5f82ecacd39529cd.bin',
                 'vs_4944d81dfe531b37.bin', 'ps_ca6bfa4a6cca7e2a.bin', 'ps_5f82ecacd39529cd.bin')
PROGRAM_NAMES += ('vs_37c34a7478544c14.bin',)
FRAME_COUNT = 24
ELIGIBLE = {0, 1, 6, 8, 10, 11, 12, 14, 16, 17, 18, 20, 21, 22, 23}
BUMP_FRAMES = set(range(12, 24)) - {17, 23}
MATCHED = set(range(FRAME_COUNT)) - {0, 9, 10, 12, 17, 18, 19, 20, 21, 23}
PIXEL_PROGRAMS = ['8759c7838bbc86c2'] * FRAME_COUNT
VERTEX_PROGRAMS = ['4944d81dfe531b37' if frame in BUMP_FRAMES else '53a0a641107ed76c' for frame in range(FRAME_COUNT)]
for frame in BUMP_FRAMES: PIXEL_PROGRAMS[frame] = 'ca6bfa4a6cca7e2a'
PIXEL_PROGRAMS[19] = '5f82ecacd39529cd'
PIXEL_PROGRAMS[1] = '3b94320087e81945'
PIXEL_PROGRAMS[9] = '5f82ecacd39529cd'
VERTEX_PROGRAMS[9] = VERTEX_PROGRAMS[19] = '37c34a7478544c14'
# The exact retained XT control pair is now material-covered.
ELIGIBLE.update((9,19))
BUMP_FRAMES.update((9,19))

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
    ('4944d81dfe531b37', '1f26d41bcb7dac1e', True),
    ('4944d81dfe531b37', 'bdcdb3ab996ae4e0', True),
    ('19a246a56e9d9700', '78963cdc7c710e04', True),
    ('19a246a56e9d9700', '1ed1bf0fdec00e1a', True),
    ('19a246a56e9d9700', '2b04461d0dae038b', True),
    ('19a246a56e9d9700', 'acc83ed2509d84a1', True),
    ('44c4a41ca92ae2e3', '78963cdc7c710e04', True),
    ('44c4a41ca92ae2e3', '1ed1bf0fdec00e1a', True),
    ('44c4a41ca92ae2e3', '2b04461d0dae038b', True),
    ('44c4a41ca92ae2e3', 'acc83ed2509d84a1', True),
    ('4944d81dfe531b37', '3006f8030a467739', True),
    ('4944d81dfe531b37', 'd6e8bdde0e4c515f', True),
    ('19a246a56e9d9700', 'e5ea78b8b0b0fe07', True),
    ('19a246a56e9d9700', 'f42202faf57a3c89', True),
    ('19a246a56e9d9700', '769c3814fc0efba8', True),
    ('19a246a56e9d9700', '22cc5b05a55ef61e', True),
    ('44c4a41ca92ae2e3', 'e5ea78b8b0b0fe07', True),
    ('44c4a41ca92ae2e3', 'f42202faf57a3c89', True),
    ('44c4a41ca92ae2e3', '769c3814fc0efba8', True),
    ('44c4a41ca92ae2e3', '22cc5b05a55ef61e', True),
    ('53a0a641107ed76c', 'ef2bf556f207b8bd', False),
    ('53a0a641107ed76c', '91b6c09eb47f8555', False),
    ('719856ce0c213220', 'cc09f17db377fd9e', False),
    ('719856ce0c213220', '3755809bd40afc13', False),
    ('719856ce0c213220', '61418505e5d8f998', False),
    ('719856ce0c213220', 'b5f1d4145171026b', False),
    ('badefd5143b3024f', 'cc09f17db377fd9e', False),
    ('badefd5143b3024f', '3755809bd40afc13', False),
    ('badefd5143b3024f', '61418505e5d8f998', False),
    ('badefd5143b3024f', 'b5f1d4145171026b', False),
    ('4944d81dfe531b37', '3602b05ce11ca6ff', True),
    ('4944d81dfe531b37', '8e58ac79b59b02b1', True),
    ('19a246a56e9d9700', '042c9ae16f41feff', True),
    ('19a246a56e9d9700', '68f0dd6791fd7d3d', True),
    ('19a246a56e9d9700', '5c823b8507fa1442', True),
    ('19a246a56e9d9700', 'a6e1328c0bb3f401', True),
    ('44c4a41ca92ae2e3', '042c9ae16f41feff', True),
    ('44c4a41ca92ae2e3', '68f0dd6791fd7d3d', True),
    ('44c4a41ca92ae2e3', '5c823b8507fa1442', True),
    ('44c4a41ca92ae2e3', 'a6e1328c0bb3f401', True),
    ('b0602757fce6e870', '517540ae6d5e5410', False),
    ('0c223ad11bce02d5', '7a0c3388065bb08d', False),
    ('233d17d26ce0c1fc', '7a0c3388065bb08d', False),
    ('167eb2d5629ab9d3', 'd44db87778a43b61', True),
    ('330ceb9dd874ede2', '550c2a4d4d3ed70f', True),
    ('12b8a13f13fe8cfe', '550c2a4d4d3ed70f', True),
    ('29d7c575396ed280', '39eb3c2258a516e1', False),
    ('29d7c575396ed280', '57acf59d19c73791', False),
    ('2a560f246c90fa64', '43c9405568d2226f', True),
    ('2a560f246c90fa64', '5e056627e9ff3a8d', True),
    ('2a560f246c90fa64', '7e5e41276b3d7514', True),
    ('2a560f246c90fa64', 'fce465befff2f623', True),
    ('2e0254dd999841c2', '675f9077d8fd21c4', False),
    ('2e0254dd999841c2', 'c997a37560e266df', False),
    ('2e0254dd999841c2', 'ebf41e1ace7af45b', False),
    ('2e0254dd999841c2', 'f646f03be5a8708d', False),
    ('33388c8897d428a5', '188c5ab9dbb98393', True),
    ('33388c8897d428a5', '18d372968af4a480', True),
    ('37e6956afd8b8d76', '9d27e7ba242f3831', False),
    ('37e6956afd8b8d76', 'e1acf8a03850acaf', False),
    ('57392213f62fef19', '62c180abe017e239', True),
    ('57392213f62fef19', 'a910daef935891ce', True),
    ('5c17a381b149b3b9', 'ed44232013f67072', True),
    ('5c17a381b149b3b9', 'f286856c3f400377', True),
    ('a420a010b0271479', '77a5b2d62fb3be48', False),
    ('a420a010b0271479', 'f917d48ee826da1f', False),
    ('a7cddf2c98d61117', '675f9077d8fd21c4', False),
    ('a7cddf2c98d61117', 'c997a37560e266df', False),
    ('a7cddf2c98d61117', 'ebf41e1ace7af45b', False),
    ('a7cddf2c98d61117', 'f646f03be5a8708d', False),
    ('a804f173f693944a', 'ed44232013f67072', True),
    ('a804f173f693944a', 'f286856c3f400377', True),
    ('b4059ab6af8fc529', '43c9405568d2226f', True),
    ('b4059ab6af8fc529', '5e056627e9ff3a8d', True),
    ('b4059ab6af8fc529', '7e5e41276b3d7514', True),
    ('b4059ab6af8fc529', 'fce465befff2f623', True),
    ('ea3d15b287892410', '77a5b2d62fb3be48', False),
    ('ea3d15b287892410', 'f917d48ee826da1f', False),
)
IMPLEMENTED_PROGRAMS = {('vs', v) for v, _, _ in CORPUS_PAIRS} | {('ps', p) for _, p, _ in CORPUS_PAIRS}
LIVE_MATERIAL_PROGRAMS = IMPLEMENTED_PROGRAMS | {('vs','37c34a7478544c14'),('ps','5f82ecacd39529cd')}
# The corpus creates shared D, so its two pair-specific repaired VS objects
# exist even though the four corresponding DEFAULT PS are not in that mode.
LIVE_MATERIAL_OBJECTS = len(LIVE_MATERIAL_PROGRAMS) + 2
PROGRAM_NAMES += tuple(sorted(f'{stage}_{identifier}.bin' for stage, identifier in IMPLEMENTED_PROGRAMS
                              if f'{stage}_{identifier}.bin' not in PROGRAM_NAMES))
for pair, (vertex, pixel, bump) in enumerate(CORPUS_PAIRS[:116]):
    for repeat in range(2):
        frame = 24 + pair * 2 + repeat
        ELIGIBLE.add(frame)
        if bump: BUMP_FRAMES.add(frame)
        if repeat: MATCHED.add(frame)
        PIXEL_PROGRAMS.append(pixel)
        VERTEX_PROGRAMS.append(vertex)
ASTEROID_FRAMES = set(range(244, 256))
UNKNOWN_FRAMES = {256, 257}
UNKNOWN_PIXEL = 'fed278e46915d6da'
# Unknown PS draws have valid COLOR0 linkage but no motion/material row. They
# are last, so no later corpus correspondence depends on their missing history.
PIXEL_PROGRAMS.extend([UNKNOWN_PIXEL] * 2)
VERTEX_PROGRAMS.extend(['53a0a641107ed76c'] * 2)
# Append the new corpus after both prior unknown controls: old frame IDs stay stable.
for pair, (vertex, pixel, bump) in enumerate(CORPUS_PAIRS[116:]):
    for repeat in range(2):
        frame = 258 + pair * 2 + repeat
        ELIGIBLE.add(frame)
        if bump: BUMP_FRAMES.add(frame)
        if repeat: MATCHED.add(frame)
        PIXEL_PROGRAMS.append(pixel)
        VERTEX_PROGRAMS.append(vertex)
FRAME_COUNT = len(PIXEL_PROGRAMS)
MOTION_FRAMES = set(range(FRAME_COUNT)) - UNKNOWN_FRAMES


def expected_rgb(frame, material):
    if frame in UNKNOWN_FRAMES: return [.5, .25, .75]
    if frame in ASTEROID_FRAMES:
        base = [128/255, 64/255, 32/255]
        return [(4 * (.25 * value**2.2 + .75))**(1/2.2) if material else .25 * value + .75 for value in base]
    return [(4**(1/2.2) if material and frame in ELIGIBLE else 1.)] * 3


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
    variants, repairs = [], []
    for line in trace_lines:
        row = fields(line)
        if line.startswith('motion_output_frame '): frames[int(row['frame'])] = row
        elif line.startswith('linear_material_frame '): material_frames[int(row['frame'])] = row
        elif line.startswith('motion_output_release '): release.append(row)
        elif line.startswith('linear_material_variant '): variants.append(row)
        elif line.startswith('linear_material_xt_default_variant '): repairs.append(row)
    assert set(live) == set(motion) == set(frames) == set(range(FRAME_COUNT)), 'missing frame evidence'
    for frame, row in frames.items():
        assert int(row['routed']) == int(row['depth_routed']) == int(frame in MOTION_FRAMES), (frame, 'motion lost')
        assert int(row['matched']) == int(frame in MATCHED), (frame, 'history changed')
        assert int(row['gate3']) == int(frame in UNKNOWN_FRAMES), (frame, 'unknown PS pair refusal changed')
        assert int(row['apply_failures']) == int(row['restore_failures']) == 0, (frame, 'state failed')
        assert int(row['taa_resolved']) == int(taa), (frame, 'TAA changed')
        assert int(live[frame]['combined']) == int(material and frame in ELIGIBLE)
        expected_refusal = 4 if frame in MOTION_FRAMES-ELIGIBLE else 0
        assert int(live[frame]['refusal']) == expected_refusal, (frame, 'stdout refusal reason changed')
        assert live[frame]['ps'] == PIXEL_PROGRAMS[frame] and live[frame]['vs'] == VERTEX_PROGRAMS[frame], (frame, 'wrong material program pair')
    if material:
        assert set(material_frames) == set(range(FRAME_COUNT))
        assert len(variants) == len(LIVE_MATERIAL_PROGRAMS) and {(row['kind'], row['original']) for row in variants} == LIVE_MATERIAL_PROGRAMS, 'combined program inventory differs'
        assert len(repairs)==2 and {(r['kind'],r['original'],r['linear']) for r in repairs}=={('vs','494fe349b8bc12ec','0'),('vs','494fe349b8bc12ec','1')}
        assert all(int(row['transform']) == 0 and int(row['create'], 16) == 0 for row in variants+repairs)
        for frame, row in material_frames.items():
            assert int(row['routed']) == int(frame in ELIGIBLE), (frame, 'combined selection')
            assert int(row['refused']) == int(frame in MOTION_FRAMES and frame not in ELIGIBLE), (frame, 'material refusal')
            assert int(row['bind_failures']) == 0
            assert int(row['bump_routed']) == int(frame in ELIGIBLE and frame in BUMP_FRAMES), (frame, 'BUMP route witness')
    else:
        assert not material_frames and not variants and not repairs
    assert len(release) == 1 and int(release[0]['released']) == 1, 'owned objects were not retired'
    return dict(checks=int(summary['checks']), restorations=int(summary['restorations']), frames=FRAME_COUNT,
                motion_pixels=int(summary['motion_pixels']), matched_pixels=int(summary['matched_pixels']),
                depth_written=int(summary['depth_written']), taa_reference_frames=int(summary['taa_reference_frames']),
                held_references=int(release[0]['held']), pixel_programs=[live[i]['ps'] for i in range(FRAME_COUNT)],
                vertex_programs=[live[i]['vs'] for i in range(FRAME_COUNT)],
                rgba=[list(map(float, live[i]['rgba'].split(','))) for i in range(FRAME_COUNT)],
                native_hashes=[live[i]['native_hash'] for i in range(FRAME_COUNT)],
                temporal_hashes=[[motion[i]['motion'], motion[i]['depth']] for i in range(FRAME_COUNT)],
                combined_frames=sorted(ELIGIBLE) if material else [], refused_frames=sorted(MOTION_FRAMES-ELIGIBLE) if material else [])


def compare_cases(cases):
    for ownership in (0, 1):
        for taa in (0, 1):
            off, on = (cases[f'ownership{ownership}-taa{taa}-material{material}'] for material in (0, 1))
            assert on['temporal_hashes'] == off['temporal_hashes'], 'material route changed RT1/RT2'
            assert on['pixel_programs'] == off['pixel_programs'] == PIXEL_PROGRAMS, 'material positive/negative schedule changed'
            assert on['vertex_programs'] == off['vertex_programs'] == VERTEX_PROGRAMS, 'material vertex schedule changed'
            assert on['held_references'] == off['held_references'] + LIVE_MATERIAL_OBJECTS, 'additional live shader objects not reflected in actual device retirement'
            for frame in range(FRAME_COUNT):
                assert on['rgba'][frame][3] == off['rgba'][frame][3], 'alpha changed'
                if frame not in ELIGIBLE:
                    assert on['native_hashes'][frame] == off['native_hashes'][frame], 'refusal changed raw native FP16 output'
                    assert on['rgba'][frame] == off['rgba'][frame], 'refusal changed original material color'
                    assert all(math.isfinite(value) for value in on['rgba'][frame]), 'valid fallback must stay finite'
                else:
                    for case, material in ((off, False), (on, True)):
                        assert all(abs(actual-wanted)<.005 for actual,wanted in zip(case['rgba'][frame][:3],expected_rgb(frame,material))), 'combined FP16 witness absent'
    for taa in (0, 1):
        for material in (0, 1):
            a, b = (cases[f'ownership{o}-taa{taa}-material{material}'] for o in (0, 1))
            assert a['native_hashes'] == b['native_hashes'] and a['temporal_hashes'] == b['temporal_hashes'], 'ownership model changed outputs'


WRAP_REPRESENTATIVES = (
    ('57392213f62fef19', 'a910daef935891ce', 6, 7, 2),
    ('5c17a381b149b3b9', 'ed44232013f67072', 6, 7, 1),
    ('33388c8897d428a5', '18d372968af4a480', 7, 5, 1),
)


def validate_wrap_case(output, trace_lines, material, depth, rt_mode):
    lines=output.splitlines()
    summary=next((fields(line) for line in lines if line.startswith('RESULT PASS ')),None)
    assert summary and int(summary['frames'])==18 and int(summary['taa_reference_frames'])==0
    # Each frame has the existing sentinel-fill comparison and the added
    # post-DIP caller-state comparison: neither may disappear unnoticed.
    assert int(summary['restorations'])==36
    assert rt_mode in ('perdraw','lazy')
    assert not any(line.startswith('RESULT FAIL') for line in lines)
    assert (int(summary['depth_written'])>0)==depth
    observations=[fields(line) for line in lines if line.startswith('MATERIAL_WRAP ')]
    images={int(fields(line)['frame']):fields(line) for line in lines if line.startswith('MATERIAL_WRAP_IMAGE ')}
    motion={int(fields(line)['frame']):fields(line) for line in lines if line.startswith('MOTION_HASH ')}
    assert len(observations)==36 and set(images)==set(motion)==set(range(18))
    for index,row in enumerate(observations):
        frame,draw=divmod(index,2);representative,step=divmod(frame,6)
        _,_,source,temporal,scalars=WRAP_REPRESENTATIVES[representative]
        combined=material and step!=2
        assert int(row['valid'])==1 and int(row['result'],16)==0,'native observation failed'
        assert [int(row[key]) for key in ('frame','draw','sequence','representative','step','source','motion','scalars','combined')]==[frame,draw,index+1,representative,step,source,temporal,scalars,int(combined)]
        caller=[0]*16
        if step!=4:
            reverse=step in (1,3)
            caller[1],caller[2]=((11,6) if reverse else (5,10))
            caller[source]=2 if reverse else 1
            caller[temporal]=caller[8]=15
        expected=list(caller);expected[temporal]=0
        if depth:expected[8]=0
        if combined:
            expected[1]=(caller[1]&7)|(8 if caller[source]&1 else 0)
            if scalars==2:expected[2]=(caller[2]&7)|(8 if caller[source]&2 else 0)
        assert list(map(int,row['values'].split(',')))==expected,(frame,draw,'physical native WRAP')
    rows=[(line,fields(line)) for line in trace_lines]
    frames={int(row['frame']):row for line,row in rows if line.startswith('motion_output_frame ')}
    materials={int(row['frame']):row for line,row in rows if line.startswith('linear_material_frame ')}
    devices=[row for line,row in rows if line.startswith('motion_output_device ')]
    assert devices and all(int(row['depth'])==depth and row['rt_mode']==rt_mode for row in devices)
    if not depth:assert all(row['depth_reason']=='fixture_motion_only' for row in devices)
    assert set(frames)==set(range(18))
    for frame,row in frames.items():
        step=frame%6
        assert int(row['routed'])==2 and int(row['depth_routed'])==2*depth
        assert int(row['matched'])==(0 if step in (0,4) else 2)
        assert all(int(row[key])==0 for key in ('gate3','apply_failures','restore_failures','taa_resolved'))
        assert row['rt_mode']==rt_mode
    variants=[row for line,row in rows if line.startswith('linear_material_variant ')]
    repairs=[row for line,row in rows if line.startswith('linear_material_xt_default_variant ')]
    if material:
        assert set(materials)==set(range(18))
        assert len(variants)==len(LIVE_MATERIAL_PROGRAMS) and {(row['kind'],row['original']) for row in variants}==LIVE_MATERIAL_PROGRAMS
        assert len(repairs)==2 and {(r['kind'],r['original'],r['linear']) for r in repairs}=={('vs','494fe349b8bc12ec','0'),('vs','494fe349b8bc12ec','1')}
        assert all(int(row['transform'])==0 and int(row['create'],16)==0 for row in variants+repairs)
        for frame,row in materials.items():
            assert int(row['routed'])==int(row['bump_routed'])==(0 if frame%6==2 else 2)
            assert int(row['refused'])==(2 if frame%6==2 else 0) and int(row['bind_failures'])==0
    else:assert not variants and not repairs and not materials
    release=[row for line,row in rows if line.startswith('motion_output_release ')]
    assert len(release)==1 and int(release[0]['released'])==1
    assert all(all(math.isfinite(float(value)) for value in row['rgba'].split(',')) for row in images.values())
    if not depth:assert all(int(row['depth'],16)==0 for row in motion.values())
    return dict(checks=int(summary['checks']),restorations=int(summary['restorations']),frames=18,native_draw_observations=36,
                held_references=int(release[0]['held']),depth_enabled=bool(depth),fixture_motion_only_override=not depth,rt_mode=rt_mode,
                native_hashes=[images[i]['native_hash'] for i in range(18)],alpha_hashes=[images[i]['alpha_hash'] for i in range(18)],
                temporal_hashes=[[motion[i]['motion'],motion[i]['depth']] for i in range(18)])


def compare_wrap_cases(cases):
    for depth in (0,1):
        for mode in ('perdraw','lazy'):
            off,on=(cases[f'depth{depth}-{mode}-material{material}'] for material in (0,1))
            assert on['held_references']==off['held_references']+LIVE_MATERIAL_OBJECTS
            assert on['alpha_hashes']==off['alpha_hashes'] and on['temporal_hashes']==off['temporal_hashes'],'WRAP changed native alpha/motion/depth'
            for frame in (2,8,14):assert on['native_hashes'][frame]==off['native_hashes'][frame],'refusal changed native palette output'
        for material in (0,1):
            a,b=(cases[f'depth{depth}-{mode}-material{material}'] for mode in ('perdraw','lazy'))
            assert all(a[key]==b[key] for key in ('native_hashes','alpha_hashes','temporal_hashes')),'lazy WRAP changed output'
    for mode in ('perdraw','lazy'):
        for material in (0,1):
            a,b=(cases[f'depth{depth}-{mode}-material{material}'] for depth in (0,1))
            assert a['alpha_hashes']==b['alpha_hashes'] and [v[0] for v in a['temporal_hashes']]==[v[0] for v in b['temporal_hashes']],'depth mode changed alpha/motion'


# A separate schedule keeps all earlier 148 corpus cases and their frame IDs.
XT_BUMP_VS = '37c34a7478544c14'
XT_DEFAULT_VS = '494fe349b8bc12ec'
XT_PIXELS = ('5f82ecacd39529cd','f1b0e820c7b488c3','6733b119142c8d42','496049cec2066ed3',
             'd51cf763125cb85a','31445adb0a62d134','d22f2ce2c740e6a7','1de3d2dde345a7e3',
             '75fb9c6b05e28ea2','edaef099780fcafe','fffdabd910793aba','e6794b6ec37ff71a',
             'fd58e6b7e8cf969c','dd87737d697c6764','7c83ed50c9894e44')
XT_PAIRS = tuple((XT_BUMP_VS if i<10 else XT_DEFAULT_VS,pixel) for i,pixel in enumerate(XT_PIXELS))
XT_PROGRAM_NAMES = tuple(dict.fromkeys(PROGRAM_NAMES[:2] + tuple('vs_'+v+'.bin' for v,_ in XT_PAIRS) + tuple('ps_'+p+'.bin' for _,p in XT_PAIRS)))
XT_MATERIAL_PROGRAMS = {tuple(name[:-4].split('_',1)) for name in XT_PROGRAM_NAMES}
XT_REPAIRED_PROGRAMS = {('vs',XT_DEFAULT_VS,linear) for linear in ('0','1')} | {('ps',p,'0') for p in XT_PIXELS[10:14]}
XT_MATERIAL_OBJECTS = len(XT_MATERIAL_PROGRAMS) + len(XT_REPAIRED_PROGRAMS)
XT_ALPHA = .625*(.25+.75*128/255)


def xt_schedule(material):
    schedule=[]
    frame=0
    for plan in range(92):
        pair=plan//6 if plan<84 else (10 if plan%2 else 14) if plan<88 else 15 if plan<90 else 0 if plan==90 else 10
        step=plan%6 if plan<84 else plan-84 if plan<88 else plan-88 if plan<90 else 6
        skip=not material and 10<=pair<14
        schedule.append(dict(plan=plan,pair=pair,step=step,transport=plan>=90,frame=None if skip else frame,
                             combined=bool(material and pair!=15 and not(plan<84 and step==2)),
                             matched=pair!=15 and plan<90 and not (plan<84 and step==0 or plan in (4,64,84))))
        frame+=not skip
    return schedule


def xt_expected_wrap(sample,depth,draw=1):
    pair,step,plan=sample['pair'],sample['step'],sample['plan']
    bump=pair<10
    values=[0]*16
    if plan not in (4,64):
        values[0]=13;values[1]=11 if step%2 else 5;values[2]=6 if step%2 else 10
        values[5]=7;values[6]=2 if sample['transport'] or step%2 else 1;values[7 if bump else 4]=15;values[8 if bump else 7]=15
    if pair!=15:
        values[7 if bump else 4]=0
        if depth:values[8 if bump else 7]=0
        if sample['combined'] and bump and (not sample['transport'] or draw):
            values[1]=(values[1]&7)|(8 if values[6]&1 else 0)
            values[2]=(values[2]&7)|(8 if values[6]&2 else 0)
    return values


def validate_xt_case(output,trace_lines,material,depth,rt_mode):
    assert rt_mode in ('perdraw','lazy')
    lines=output.splitlines();schedule=xt_schedule(material)
    active={s['plan']:s for s in schedule if s['frame'] is not None}
    skipped={s['plan']:s for s in schedule if s['frame'] is None}
    def unique(prefix,key):
        rows=[fields(line) for line in lines if line.startswith(prefix)]
        assert len(rows)==len({int(r[key]) for r in rows}), (prefix,'duplicate evidence')
        return {int(r[key]):r for r in rows}
    live=unique('XT_LIVE ','plan');skips=unique('XT_SKIP ','plan');motion=unique('MOTION_HASH ','frame')
    wrap=[fields(line) for line in lines if line.startswith('XT_WRAP ')]
    summaries=[fields(line) for line in lines if line.startswith('RESULT PASS ')]
    assert len(summaries)==1 and not any(line.startswith('RESULT FAIL') for line in lines)
    summary=summaries[0];count=len(active)
    assert int(summary['frames'])==count and int(summary['taa_reference_frames'])==0
    assert int(summary['restorations'])==2*count and int(summary['checks'])>0
    assert (int(summary['depth_written'])>0)==bool(depth)
    assert set(live)==set(active) and set(skips)==set(skipped)
    assert set(motion)==set(range(count)) and len(wrap)==2*count
    assert sum(line=='RESET PASS' for line in lines)==2
    for plan,row in skips.items():
        assert row['reason']=='native_invalid_linkage' and int(row['pair'])==skipped[plan]['pair'] and int(row['step'])==skipped[plan]['step']
    rows=[(line,fields(line)) for line in trace_lines]
    frames={int(r['frame']):r for line,r in rows if line.startswith('motion_output_frame ')}
    materials={int(r['frame']):r for line,r in rows if line.startswith('linear_material_frame ')}
    devices=[r for line,r in rows if line.startswith('motion_output_device ')]
    variants=[r for line,r in rows if line.startswith('linear_material_variant ')]
    repaired=[r for line,r in rows if line.startswith('linear_material_xt_default_variant ')]
    releases=[r for line,r in rows if line.startswith('motion_output_release ')]
    assert devices and all(int(r['depth'])==depth and r['rt_mode']==rt_mode for r in devices)
    if not depth:assert all(r['depth_reason']=='fixture_motion_only' for r in devices)
    assert set(frames)==set(range(count))
    assert len(releases)==1 and int(releases[0]['released'])==1
    if material:
        assert set(materials)==set(frames)
        assert len(variants)==len(XT_MATERIAL_PROGRAMS) and {(r['kind'],r['original']) for r in variants}==XT_MATERIAL_PROGRAMS
        assert len(repaired)==len(XT_REPAIRED_PROGRAMS) and {(r['kind'],r['original'],r['linear']) for r in repaired}==XT_REPAIRED_PROGRAMS
        assert all(int(r['transform'])==0 and int(r['create'],16)==0 for r in variants+repaired)
        assert not any(line.startswith('linear_material_xt_default_unavailable ') for line,_ in rows)
    else:assert not variants and not repaired and not materials
    by_plan={}
    for index,row in enumerate(wrap):
        sample=schedule[int(row['plan'])]
        assert sample['frame'] is not None
        assert int(row['frame'])==sample['frame'] and int(row['sequence'])==index+1 and int(row['draw'])==index%2
        assert int(row['valid'])==1 and int(row['result'],16)==0
        assert list(map(int,row['values'].split(',')))==xt_expected_wrap(sample,depth,int(row['draw']))
        by_plan.setdefault(sample['plan'],[]).append(row)
    assert set(by_plan)==set(active) and all(len(rows)==2 for rows in by_plan.values())
    transport={}
    for line in lines:
        if not line.startswith('XT_TRANSPORT '):continue
        row=fields(line);plan,draw=int(row['plan']),int(row['draw'])
        assert plan in active and active[plan]['transport'] and draw in (0,1)
        assert (plan,draw) not in transport
        assert int(row['frame'])==active[plan]['frame'] and int(row['combined'])==int(material and draw==1)
        assert float(row['perspective'])==.125
        low,high=float(row['highlight_low']),float(row['highlight_high'])
        assert math.isclose(low,17**-6,rel_tol=1e-8) and high==1 and high-low>.5
        pixels,error,spread=int(row['pixels']),float(row['max_error']),float(row['rgb_range'])
        assert math.isfinite(error) and math.isfinite(spread)
        # materialxt keeps the fixture default 64x64 target through both Resets.
        assert (pixels>64*64//4 and 0<=error<=1 and spread>.001) if draw else (pixels==0 and error==spread==0)
        if not depth:assert int(row['depth'],16)==0
        transport[plan,draw]=row
    assert set(transport)=={(plan,draw) for plan,sample in active.items() if sample['transport'] for draw in (0,1)}
    result={}
    for plan,sample in active.items():
        row=live[plan];frame=sample['frame'];pair=sample['pair'];unknown=pair==15
        vertex,pixel=XT_PAIRS[pair] if not unknown else (XT_DEFAULT_VS,UNKNOWN_PIXEL)
        assert (row['vs'],row['ps'])==(vertex,pixel)
        assert int(row['frame'])==frame and int(row['pair'])==pair and int(row['step'])==sample['step']
        assert int(row['combined'])==sample['combined'] and int(row['matched'])==sample['matched']
        assert int(row['refusal'])==(1 if unknown else 4 if plan<84 and sample['step']==2 else 0)
        rgba=list(map(float,row['rgba'].split(',')));assert len(rgba)==4 and all(math.isfinite(v) for v in rgba)
        expected=[.5,.25,.75] if unknown else [4**(1/2.2) if sample['combined'] else 1]*3
        assert sample['transport'] or all(abs(got-wanted)<.005 for got,wanted in zip(rgba[:3],expected))
        assert abs(rgba[3]-(.625 if unknown else XT_ALPHA))<.001
        state=frames[frame];routed=0 if unknown else 2
        assert int(state['routed'])==routed and int(state['depth_routed'])==routed*depth and int(state['matched'])==2*sample['matched']
        assert int(state['gate3'])==(2 if unknown else 0)
        assert all(int(state[k])==0 for k in ('apply_failures','restore_failures','taa_resolved')) and state['rt_mode']==rt_mode
        if material:
            m=materials[frame];draws=1 if sample['transport'] else 2
            assert int(m['routed'])==draws*sample['combined'] and int(m['bump_routed'])==draws*(sample['combined'] and pair<10)
            assert int(m['refused'])==(1 if sample['transport'] else 2 if not unknown and not sample['combined'] else 0) and int(m['bind_failures'])==0
        if not depth:assert int(motion[frame]['depth'],16)==0
        result[str(plan)]=dict(rgba=rgba,alpha_hash=row['alpha_hash'],image_hash=row['image_hash'],temporal_hashes=[motion[frame]['motion'],motion[frame]['depth']])
        if sample['transport']:
            ordinary,linear=transport[plan,0],transport[plan,1]
            assert all(ordinary[key]==linear[key] for key in ('alpha','motion','depth'))
            assert linear['alpha']==row['alpha_hash'] and linear['motion']==motion[frame]['motion'] and linear['depth']==motion[frame]['depth']
            result[str(plan)]['transport']={key:linear[key] for key in ('pixels','max_error','rgb_range','highlight_low','highlight_high')}
    return dict(frames=count,checks=int(summary['checks']),restorations=int(summary['restorations']),native_draw_observations=len(wrap),
                held_references=int(releases[0]['held']),skipped_plans=sorted(skipped),samples=result)


def compare_xt_cases(cases):
    for depth in (0,1):
        for mode in ('perdraw','lazy'):
            off,on=(cases[f'depth{depth}-{mode}-material{m}'] for m in (0,1))
            assert on['held_references']==off['held_references']+XT_MATERIAL_OBJECTS
            for plan,row in off['samples'].items():
                twin=on['samples'][plan]
                assert row['alpha_hash']==twin['alpha_hash'] and row['temporal_hashes']==twin['temporal_hashes']
                if 88<=int(plan)<90 or int(plan)<84 and int(plan)%6==2:assert row['image_hash']==twin['image_hash']
            # DEFAULT step2 is matched ordinary; step3 is matched linear in the
            # same object/epoch/matrix state. No Reset or history normalization.
            for pair in range(10,14):
                ordinary,linear=(on['samples'][str(pair*6+s)] for s in (2,3))
                assert ordinary['alpha_hash']==linear['alpha_hash'] and ordinary['temporal_hashes']==linear['temporal_hashes']
        for material in (0,1):
            a,b=(cases[f'depth{depth}-{mode}-material{material}'] for mode in ('perdraw','lazy'))
            assert a['samples']==b['samples'],'lazy pair/state transaction changed results'
    for mode in ('perdraw','lazy'):
        for material in (0,1):
            a,b=(cases[f'depth{d}-{mode}-material{material}'] for d in (0,1))
            for plan,row in a['samples'].items():
                twin=b['samples'][plan]
                assert row['alpha_hash']==twin['alpha_hash'] and row['image_hash']==twin['image_hash'] and row['temporal_hashes'][0]==twin['temporal_hashes'][0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode',choices=('corpus','wrap','xt','glass'),default='corpus')
    parser.add_argument('--fixture', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--programs', type=Path, default=PROGRAMS)
    parser.add_argument('--result', type=Path)
    args = parser.parse_args()
    if args.result is None: args.result=bottle.results_dir(ROOT,create=False)/('linear-material-live-'+args.mode+'.json' if args.mode!='corpus' else 'linear-material-live.json')
    fixture, dll = args.fixture.resolve(), args.dll.resolve()
    programs = [args.programs.resolve() / name for name in (glass_live.PROGRAM_NAMES if args.mode=='glass' else XT_PROGRAM_NAMES if args.mode=='xt' else PROGRAM_NAMES)]
    assert bottle.BOTTLE == 'X3', 'new verification requires X3 bottle'
    assert all(path.is_file() for path in [fixture, dll, *programs]), 'prebuilt inputs or local programs missing'
    raw = Path(tempfile.mkdtemp(prefix='x3-linear-material-live-'))
    report = dict(passed=False, game_launched=False, bottle=bottle.describe(), raw=str(raw),
                  scope='Actual live evaluate_draw across all 148 exact pairs / 115 originals, DEFAULT/BUMPMAP/LOW/Asteroid alternation, newly covered retained XT BUMP control and valid covered-VS unknown-PS refusal, exact family sampler admission, FP16 color witness, unchanged RT1/RT2, stateblocks, Reset, cached gains and owned shader retirement; ownership 0/1 and TAA off/on. Native Windows untested.',
                  mode=args.mode,
                  binaries={str(path): sha(path) for path in (fixture, dll)}, local_programs={path.name: sha(path) for path in programs}, cases={})
    if args.mode=='glass': report['scope']=glass_live.SCOPE
    if args.mode=='xt': report['scope']='Actual live XT14 exact pairs, shared-D generic alternation, valid unknown mate, repaired DEFAULT ordinary fallback, sampler s5/s4 refusal, native WRAP/reserved-state restoration and Reset, plus two paired perspective RGB/alpha/temporal diagnostics with a physical highlight WRAP seam. Feature-off malformed DEFAULT is explicitly skipped; host seam owns its unavailable native-once path. Native Windows untested.'
    if args.mode=='wrap': report['scope']='Three actual native scalar WRAP carriers, two consecutive indexed submissions, native GetRenderState observation, full caller-state restoration, sampler refusal, StateBlock/Reset, depth on/motion-only fixture override, perdraw/lazy and material off/on; native alpha and RT1/RT2 twins. Detached qualification owns palette color math; no gameplay or native-Windows claim.'
    args.result.parent.mkdir(parents=True, exist_ok=True)
    try:
        specifications=([(owner,taa,material,1,'perdraw') for owner in (0,1) for taa in (0,1) for material in (0,1)] if args.mode=='corpus' else [(1,0,material,depth,mode) for depth in (0,1) for mode in ('perdraw','lazy') for material in (0,1)])
        for ownership,taa,material,depth,rt_mode in specifications:
            assert not game_running(), 'game is running'
            name = f'ownership{ownership}-taa{taa}-material{material}' if args.mode=='corpus' else f'depth{depth}-{rt_mode}-material{material}'
            work = raw / name; work.mkdir()
            shutil.copy2(fixture, work / 'fixture.exe'); shutil.copy2(dll, work / 'd3d9.dll')
            env = {key: value for key, value in os.environ.items() if not key.startswith('X3M_')}
            env.update(X3M_MOTION_OUTPUT='1', X3M_HDR='1', X3M_HDR_TONEMAP='agx', X3M_HDR_DECODE='gamma2.2',
                       X3M_HDR_EXPOSURE='manual', X3M_HDR_EV_MANUAL='0', X3M_HDR_CLAMP='0', X3M_HDR_BLOOM='0',
                       X3M_LINEAR_MATERIALS=str(material), X3M_MATERIAL_DIRECT_GAIN='1', X3M_MATERIAL_EMISSIVE_GAIN='4',
                       X3M_LIGHTMAP_EMISSIVE_GAIN='4', X3M_OWNERSHIP=str(ownership), X3M_TAA=str(taa),
                       X3M_FIXTURE_TAA_SENTINEL='1', X3M_TAA_SHARPEN='0', X3M_TAA_MIP_BIAS='0', X3M_SCENE_HOOK='0',
                       X3M_TELEMETRY='1', X3M_MOTION_FRAME_LOG='1', X3M_CAPTURE_START='1', X3M_CAPTURE_FRAMES='0',
                       X3M_MOTION_RT_MODE=rt_mode, X3M_STATE_SHADOW='1', WINEDLLOVERRIDES='d3d9=n,b')
            if args.mode=='glass': env['X3M_MATERIAL_EMISSIVE_GAIN']='1'
            if args.mode!='corpus': env['X3M_FIXTURE_MOTION_DEPTH']=str(depth)
            fixture_mode={'corpus':'linearmaterials','wrap':'materialwrap','xt':'materialxt','glass':'materialglass'}[args.mode]
            extra_programs=[] if args.mode in ('xt','glass') else ['Z:'+str(path) for path in programs[2:7]]
            command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=n,b', '--workdir', str(work), str(work / 'fixture.exe'),
                       'Z:' + str(programs[0]), 'Z:' + str(programs[1]), fixture_mode, *extra_programs]
            start = time.monotonic()
            with (work / 'stdout.txt').open('w') as out, (work / 'wine.log').open('w') as error:
                completed = subprocess.run(command, env=env, stdout=out, stderr=error, timeout=180)
            assert completed.returncode == 0, f'{name}: exit {completed.returncode}; see {work}'
            logs = list((work / 'x3-modern-captures').glob('session-*.log'))
            assert len(logs) == 1, f'{name}: missing session log'
            with logs[0].open() as trace:
                result = (glass_live.validate_case((work / 'stdout.txt').read_text(),trace,bool(material),bool(depth),rt_mode) if args.mode=='glass' else validate_xt_case((work / 'stdout.txt').read_text(),trace,bool(material),bool(depth),rt_mode) if args.mode=='xt' else validate_wrap_case((work / 'stdout.txt').read_text(),trace,bool(material),bool(depth),rt_mode) if args.mode=='wrap' else validate_case((work / 'stdout.txt').read_text(),trace,bool(material),bool(taa)))
            result['seconds'] = round(time.monotonic() - start, 3)
            report['cases'][name] = result
            print(f'{name}: {result["checks"]} checks, {result["frames"]} frames, held={result["held_references"]}', flush=True)
        (glass_live.compare_cases if args.mode=='glass' else compare_xt_cases if args.mode=='xt' else compare_wrap_cases if args.mode=='wrap' else compare_cases)(report['cases'])
        assert report['binaries'] == {str(path): sha(path) for path in (fixture, dll)}, 'prebuilt inputs changed during qualification'
        assert report['local_programs'] == {path.name: sha(path) for path in programs}, 'local programs changed during qualification'
        report['passed'] = True
        report['checks'] = sum(case['checks'] for case in report['cases'].values())
        report['limitations'] = ['Unknown sampler getter failure and combined creation/bind/restore failures are covered by scripted host control-flow checks, not injected into this GPU script.', 'The retained XT BUMP control now uses its own valid ordinary/linear linkage, constants, false native booleans and seven samplers; unknown-PS control has valid COLOR0 linkage and expects no motion route. Both preserve finite native output and exact FP16 twins.', 'Native Windows and gameplay appearance/performance remain unverified.']
        if args.mode=='glass': report['limitations']=glass_live.LIMITATIONS
        if args.mode=='xt': report['limitations']=['Malformed DEFAULT programs with material disabled are skipped before frame/draw submission; no native linkage qualification is claimed.', 'Actual driver creation/bind failure injection remains covered by the scripted host seam; this mode verifies successful GPU availability and sampler refusal.', 'The 84 unit-lightmap samples prove routing. Two appended perspective diagnostics compare encoded linear RGB against actual ordinary working RGB using endpoint color inputs, an active point light and a highlight WRAP seam; alpha and temporal hashes are exact twins. Detached qualification owns the full XT equations.', 'Native Windows and gameplay appearance/performance remain unverified.']
        if args.mode=='wrap': report['limitations']=['Fixture-only attach override selects motion-only variants; device caps are unchanged.', 'Native WRAP states, alpha and temporal outputs are exact witnesses; independent detached qualification owns palette RGB mathematics.', 'Native Windows and gameplay appearance/performance remain unverified.']
    finally:
        destination = args.result if report['passed'] else raw / 'failed-result.json'
        destination.write_text(json.dumps(report, indent=2) + '\n')
    print(f'PASS cases={len(report["cases"])} checks={report["checks"]} result={args.result}', flush=True)


if __name__ == '__main__':
    main()
