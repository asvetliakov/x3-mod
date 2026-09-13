#!/usr/bin/env python3
"""Consume one prebuilt synthetic interpolation EXE under wine_lock.py.

No production code/game bytes, implicit compilation, or automatic GPU retry.
The fixed CPU envelope is an experiment acceptance gate, not a universal D3D
interpolator accuracy claim. COLOR1 full precision is a combined SM3-contract
inference; separate COLOR1 parity alone is insufficient, so TEX+CPU are checked.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import time

import bottle
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
KINDS = ('native', 'split_centroid', 'split_plain', 'reference_centroid',
         'reference_plain', 'split_color', 'reference_color')
INPUTS = ('verification/probe/varying_split_fixture.cpp',
          'verification/probe/build_varying_split.sh',
          'verification/probe/run_varying_split.py',
          'verification/analysis/test_varying_split.py')
EPS = 64 * 2.**-23


def selected(flat, mode='qualification'):
    if mode == 'separate':
        return (0, 3, 6)
    return (0, 3, 5, 6) if flat else tuple(range(7))


def cases():
    return [dict(id=i, flat=f, depth=d, pattern=p, fog=g, perspective=w)
            for i, (f, d, p, g, w) in enumerate(
                (f, d, p, g, w) for f in range(2) for d in range(2)
                for p in range(4) for g in range(2) for w in range(2))]


def fields(line):
    return dict(token.split('=', 1) for token in line.split() if '=' in token)


def parse(text, mode="qualification"):
    assert 'FAIL' not in text and 'DIFF ' not in text, 'native failure'
    classifier_rows = []
    native_flat = True
    if mode == 'separate':
        classifier_rows = [fields(s) for s in text.splitlines() if s.startswith('CLASSIFIER ')]
        assert len(classifier_rows)==3
        for row,name,shade in zip(classifier_rows,('first_flat','gouraud','repeat_flat'),(1,2,1)):
            assert row['name']==name
            assert [int(row[k]) for k in ('requested','before','after')]==[shade]*3
            assert 1000<int(row['coverage'])<=4096
        classification = [fields(s) for s in text.splitlines() if s.startswith('CLASSIFICATION ')]
        assert len(classification)==1 and classification[0]['stable']=='1'
        assert classification[0]['native_flat_conformance'] in ('0','1')
        native_flat = classification[0]['native_flat_conformance']=='1'
        assert classification[0]['effective_flat']==('flat' if native_flat else 'gouraud')
    rows = [fields(s) for s in text.splitlines() if s.startswith('CASE ')]
    assert len(rows) == 64
    for actual, expected in zip(rows, cases()):
        for key, value in expected.items():
            assert int(actual[key]) == value, (key, actual, expected)
        assert int(actual['draws']) == len(selected(expected['flat'], mode))
        if mode == 'separate':
            assert int(actual['effective_flat']) == int(expected['flat'] and native_flat)
            assert [int(actual[k]) for k in ('shade_before','shade_after')]==[1 if expected['flat'] else 2]*2
        assert 1000 < int(actual['coverage']) <= 4096
        assert 700 < int(actual['analytic']) <= int(actual['coverage'])
        assert actual['rgb_exact'] == ('0' if mode == 'separate' else '1')
        for key in ('alpha_exact', 'motion_exact', 'depth_exact'):
            assert actual[key] == '1', key
        assert int(actual['centroid_exact']) == (0 if mode == 'separate' else 1 - expected['flat'])
        assert 0 <= float(actual['max_fraction']) <= 1
    creates = [fields(s) for s in text.splitlines() if s.startswith('CREATE ')]
    programs = [fields(s) for s in text.splitlines() if s.startswith('PROGRAM ')]
    active_programs = [(d,k) for d in range(2) for k in range(7) if mode != "separate" or k in (0,3,6)]
    assert len(creates) == len(programs) == len(active_programs)
    for position, (create, program) in enumerate(zip(creates, programs)):
        dep, kind = active_programs[position]
        for row in (create, program):
            assert int(row['depth']) == dep and row['kind'] == KINDS[kind]
        assert tuple(create[k] for k in ('vs', 'ps', 'inputs', 'outputs')) == ('1','1','10','11')
        subset = [r for r in rows if int(r['depth']) == dep and kind in selected(int(r['flat']), mode)]
        extra = classifier_rows if mode=='separate' and dep==0 and kind==0 else []
        assert int(program['draws']) == len(subset)+len(extra)
        assert int(program['coverage']) == sum(int(r['coverage']) for r in subset+extra)
    caps = [fields(s) for s in text.splitlines() if s.startswith('CAPS ')]
    assert len(caps) == 1 and int(caps[0]['mrt']) >= 3
    assert int(caps[0]['vs'], 16) >= 0xfffe0300
    assert int(caps[0]['ps'], 16) >= 0xffff0300
    assert caps[0]['msaa'] == '0' and caps[0]['format'] == '116'
    assert text.splitlines().count('CONTEXT shading=gouraud+flat wrap9=0 no_msaa=1') == 1
    results = [fields(s) for s in text.splitlines() if s.startswith('RESULT PASS ')]
    assert len(results) == 1
    result = results[0]
    assert (int(result['cases']), int(result['creates']), int(result['negatives'])) == (64, 12 if mode == "separate" else 28, 10)
    assert int(result['coverage']) == sum(int(r['coverage']) for r in rows)
    assert math.isclose(float(result['max_fraction']), max(float(r['max_fraction']) for r in rows), abs_tol=1e-8)
    return dict(native_flat_conformance=native_flat if mode=="separate" else None,
                requested_flat_cases=32, effective_flat_cases=32 if native_flat else 0,
                effective_gouraud_cases=32 if native_flat else 64, classifier_rows=classifier_rows,
                qualification_draws=sum(int(r["draws"]) for r in rows),
                classifier_draws=len(classifier_rows), cases=64, gouraud_cases=32, flat_cases=32, shader_creates=12 if mode == "separate" else 28,
                draws=sum(int(p['draws']) for p in programs), programs=programs,
                coverage=int(result['coverage']), max_fraction=float(result['max_fraction']),
                host_negative_contracts=10, rows=rows)


def f32(x):
    return struct.unpack('<f', struct.pack('<f', x))[0]


def inputs(case):
    colors = (
        ((1-2**-12,1+2**-12,.5),(1+2**-12,1-2**-12,.5+2**-13),(1,.75,.5-2**-13)),
        ((32-2**-7,64+2**-6,16),(32+2**-7,64-2**-6,16+2**-8),(32,64,16-2**-8)),
        ((2**-16,2**-14,2**-12),(2**-16+2**-24,2**-14+2**-22,2**-12+2**-20),
         (2**-16-2**-24,2**-14-2**-22,2**-12-2**-20)),
        ((.125,128,32768),(64,.0625,49152),(.5,4096,65504)))
    out = []
    for i, (x, y) in enumerate(((4.25,4.5),(59.5,7.25),(10.25,59.5))):
        w = .75 + .5*i if case['perspective'] else 1.
        cx, cy = f32(f32(2*x/64-1)*w), f32(f32(1-2*y/64)*w)
        out.append(((cx/w+1)*32, (1-cy/w)*32, w, colors[case['pattern']][i]))
    return out


def expected_rgb(case, vertices, x, y):
    # Independent signed-area barycentrics rather than the C++ closed form.
    def area(a, b, c):
        return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    full = area(*vertices)
    weights = [area((x,y),vertices[(i+1)%3],vertices[(i+2)%3])/full for i in range(3)]
    if min(weights) < .05:
        return None
    if case['flat']:
        return vertices[0][3]
    den = sum(a/v[2] for a,v in zip(weights,vertices))
    return tuple(sum(a*v[3][c]/v[2] for a,v in zip(weights,vertices))/den for c in range(3))


def classify_native(first, gouraud, repeated, covered):
    # Alpha bits alone choose the independent RGB oracle. Vertex0 fog=-.125
    # saturates to +0, hence true flat alpha is exactly +0 at any PP width.
    assert len(covered)>1000
    assert first==repeated, 'unstable native flat alpha'
    assert all(math.isfinite(struct.unpack('<f',x)[0]) for x in first+gouraud)
    assert len({gouraud[p] for p in covered})>1, 'ambiguous constant Gouraud control'
    flat = all(first[p]==bytes(4) for p in covered)
    smooth = all(first[p]==gouraud[p] for p in covered)
    assert flat != smooth, 'unclassified native alpha'
    return flat


def validate_classifier(folder, report):
    images = [[(folder/f'classifier_{name}_rt{t}_0.rgba32f').read_bytes() for t in range(3)]
              for name in ('first_flat','gouraud','repeat_flat')]
    assert all(len(x)==65536 for triple in images for x in triple)
    for t in (1,2):
        assert images[0][t]==images[1][t]==images[2][t]
    assert images[0][2]==bytes(65536)
    motion = struct.unpack('<16384f',images[0][1])
    covered = [p for p in range(4096) if motion[p*4+3]!=0]
    for row in report['classifier_rows']:
        assert int(row['coverage'])==len(covered)
    alpha = [[b[0][p*16+12:p*16+16] for p in range(4096)] for b in images]
    result = classify_native(*alpha,covered)
    assert result == report['native_flat_conformance']


def validate_pixels(folder, report, mode="qualification"):
    if mode == "separate":
        validate_classifier(folder,report)
    maximum = 0.
    checks = 0
    for case, row in zip(cases(), report['rows']):
        active = selected(case['flat'], mode)
        raw = {k: [(folder/f'{KINDS[k]}_rt{t}_{case["id"]}.rgba32f').read_bytes()
                   for t in range(3)] for k in active}
        assert all(len(b) == 64*64*16 for triple in raw.values() for b in triple)
        values = {k: [struct.unpack('<16384f', b) for b in triple] for k,triple in raw.items()}
        coverage = analytic = 0
        verts = inputs(case)
        for p in range(4096):
            offset = p*16
            for k in active:
                assert raw[k][0][offset+12:offset+16] == raw[0][0][offset+12:offset+16], ('alpha',case,p,k)
                for t in (1,2):
                    assert raw[k][t][offset:offset+16] == raw[0][t][offset:offset+16], ('temporal',case,p,k,t)
            pairs = [] if mode == 'separate' else [(5,6)] if case['flat'] else [(5,6),(1,3),(2,4),(1,2)]
            for a,b in pairs:
                assert raw[a][0][offset:offset+12] == raw[b][0][offset:offset+12], ('RGB parity',case,p,a,b)
            if not case['depth']:
                assert raw[0][2][offset:offset+16] == bytes(16), ('depth off',case,p)
            if values[0][1][p*4+3] == 0:
                continue
            coverage += 1
            oracle_case = dict(case,flat=int(row['effective_flat'])) if mode=='separate' else case
            expected = expected_rgb(oracle_case, verts, p%64, p//64)
            if expected is None:
                continue
            analytic += 1
            for k in active:
                if k == 0:
                    continue
                for channel, target in enumerate(expected):
                    actual = values[k][0][p*4+channel]
                    tolerance = 1e-12 + EPS*abs(target)
                    fraction = abs(actual-target)/tolerance
                    assert math.isfinite(actual) and fraction <= 1, ('CPU precision',case,p,k,channel,actual,target,tolerance)
                    maximum = max(maximum,fraction)
                    checks += 1
        assert coverage == int(row['coverage']) and analytic == int(row['analytic'])
    return dict(independent_cpu_checks=checks, independent_cpu_max_fraction=maximum,
                exact_alpha_pixels=report["qualification_draws"]*4096, exact_temporal_vectors=report["qualification_draws"]*4096*2)


DIAGNOSTIC_KINDS = ('native','separate','packed','vs_reverse','ps_reverse','both_reverse',
                    'packed_no_pp_decl','native_no_pp_decl','vs_full_write','ps_full_write',
                    'scalar_separate','packed_alpha_tap','separate_alpha_tap')


def diagnostic_report(folder, text):
    # Completion is not qualification: every failed comparison/nonfinite lane
    # remains in the record, independently counted from authoritative raw bits.
    assert 'FAIL' not in text
    caps = [fields(s) for s in text.splitlines() if s.startswith('CAPS ')]
    assert len(caps)==1 and int(caps[0]['mrt'])>=3
    assert int(caps[0]['vs'],16)>=0xfffe0300 and int(caps[0]['ps'],16)>=0xffff0300
    assert caps[0]['msaa']=='0' and caps[0]['format']=='116'
    assert text.splitlines().count('CONTEXT shading=gouraud+flat wrap9=0 no_msaa=1')==1
    rows = [fields(s) for s in text.splitlines() if s.startswith('OBSERVE ')]
    assert len(rows) == 52
    creates = [fields(s) for s in text.splitlines() if s.startswith('CREATE ')]
    programs = [fields(s) for s in text.splitlines() if s.startswith('PROGRAM ')]
    assert len(creates) == len(programs) == 26
    for n,(create,program) in enumerate(zip(creates,programs)):
        dep,k = divmod(n,13)
        for row in (create,program):
            assert int(row['depth']) == dep and row['kind'] == DIAGNOSTIC_KINDS[k]
        assert tuple(create[k] for k in ('vs','ps','inputs','outputs')) == ('1','1','10','11')
        assert int(program['draws']) == 2 and int(program['coverage']) > 2000
    all_observations = []
    for case in range(4):
        raw = [[(folder/f'{name}_rt{t}_{case}.rgba32f').read_bytes() for t in range(3)]
               for name in DIAGNOSTIC_KINDS]
        assert all(len(x)==65536 for triple in raw for x in triple)
        values = [[struct.unpack('<16384f',x) for x in triple] for triple in raw]
        covered = [p for p in range(4096) if values[0][1][p*4+3] != 0]
        assert len(covered)>1000
        for k,name in enumerate(DIAGNOSTIC_KINDS):
            row = rows[case*13+k]
            ref = 7 if k==6 else 12 if k in (11,12) else 0
            assert (int(row['id']),int(row['depth']),int(row['fog'])) == (case,case//2,case%2)
            assert row['kind']==name and row['reference']==DIAGNOSTIC_KINDS[ref]
            assert int(row['perspective'])==case//2
            counts = dict(coverage=len(covered),alpha_diff=0,alpha_nonfinite=0,rgb_diff=0,temporal_diff=0)
            for p in covered:
                off=p*16
                counts['alpha_diff'] += raw[k][0][off+12:off+16] != raw[ref][0][off+12:off+16]
                counts['alpha_nonfinite'] += not math.isfinite(values[k][0][p*4+3])
                counts['rgb_diff'] += raw[k][0][off:off+12] != raw[1][0][off:off+12]
                counts['temporal_diff'] += sum(raw[k][t][off:off+16] != raw[0][t][off:off+16] for t in (1,2))
            for key,value in counts.items():
                assert int(row[key])==value,(case,name,key)
            bits = next((struct.unpack('<I',raw[k][0][p*16+12:p*16+16])[0] for p in covered
                         if struct.unpack('<I',raw[k][0][p*16+12:p*16+16])[0]),0)
            assert int(row['first_alpha_bits'],16)==bits
            all_observations.append(dict(row, **counts))
    result = [fields(s) for s in text.splitlines() if s.startswith('RESULT DIAGNOSTIC ')]
    assert len(result)==1 and (int(result[0]['cases']),int(result[0]['creates']),int(result[0]['negatives'])) == (4,52,10)
    for dep in range(2):
        for k in range(13):
            assert int(programs[dep*13+k]['coverage']) == sum(o['coverage'] for o in all_observations if int(o['depth'])==dep and o['kind']==DIAGNOSTIC_KINDS[k])
    assert int(result[0]['coverage']) == sum(o['coverage'] for o in all_observations if o['kind']=='native')
    return dict(diagnostic_completed=True, qualification_pass=False, cases=4, shader_creates=52,
                draws=52, observations=all_observations,
                classification='Per-control failures retained; completion is not packing qualification.')


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--mode', choices=('qualification','separate','diagnostic'),default='qualification')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--result', type=Path)
    args = parser.parse_args()
    assert bottle.BOTTLE == 'X3', 'set X3M_FIXTURE_BOTTLE=X3'
    assert not game_running(), 'game is running'
    exe = args.exe.resolve(strict=True)
    folder = args.output_dir or Path(tempfile.mkdtemp(prefix='x3-varying-split-'))
    folder.mkdir(parents=True, exist_ok=True)
    assert not list(folder.iterdir()), 'raw output directory must be fresh'
    sources = {p:digest(ROOT/p) for p in INPUTS}
    executable = digest(exe)
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', str(exe), args.mode]
    started = time.monotonic()
    record = dict(pass_=False, mode=args.mode, command=command, raw_directory=str(folder),
                  exe=str(exe), exe_sha256=executable, source_sha256=sources,
                  bottle=bottle.describe())
    try:
        with (folder/'stdout.txt').open('wb') as out, (folder/'stderr.txt').open('wb') as err:
            process = subprocess.Popen(command, cwd=folder, stdout=out, stderr=err,
                                       env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
            while True:
                try:
                    process.wait(timeout=30)
                    break
                except subprocess.TimeoutExpired:
                    print('Varying fixture still running; raw:', folder, flush=True)
                    if time.monotonic()-started > 180:
                        process.kill()
                        process.wait()
                        raise RuntimeError('bounded fixture timeout; no retry')
        record.update(exit_code=process.returncode, wall_seconds=time.monotonic()-started)
        assert process.returncode == 0, 'native fixture failed'
        assert executable == digest(exe) and sources == {p:digest(ROOT/p) for p in INPUTS}
        text = (folder/'stdout.txt').read_text(errors='replace')
        if args.mode == 'diagnostic':
            record.update(diagnostic_report(folder,text))
            record.pop('pass_')
        else:
            report = parse(text,args.mode)
            record.update(validate_pixels(folder,report,args.mode))
            report.pop('rows')
            report.pop('classifier_rows')
            record.update(report, pass_=True)
        record['limitations'] = [
            'Synthetic finite positive RGB and no MSAA only; no game shader or live routing qualification.',
            'The diagnostic mode preserves classified failed comparisons and never qualifies packing.',
            'Separate mode deliberately excludes all packed variants; original R1 and strict-flat R2 failures remain unchanged.',
            'Separate mode selects effective flat/Gouraud CPU interpolation only from a fresh native-alpha FLAT/GOURAUD/FLAT classifier and verified Set/Get states; candidate RGB cannot select its oracle.',
            'native_flat_conformance=false records backend divergence from documented flat COLOR semantics, not a universal flat-conformance pass.',
            'Separate mode is resource-feasibility evidence and does not solve the ten-input Boron/Paranid originals; a zero filler is relocated for the authored reference.',
            'Any diagnostic declaration-order recovery is an observed X3-stack result, not a native-Windows or universal packing guarantee.',
            'TEX9 packed candidate is Gouraud with WRAP9=0; undefined mixed-semantic flat mode is never submitted.',
            'COLOR1 non-PP full precision is inferred from combined SM3 contracts and tested against dedicated TEX and CPU, not merely separate COLOR1.',
            'Flat full-TEX reference repeats first-vertex RGB while retaining original alpha/fog inputs and instruction words.',
            '64 float32-epsilon relative plus 1e-12 absolute is a fixed experimental envelope, not a universal hardware interpolation bound.',
            'PP may execute at full precision; no PP/full difference is required. Native Windows runtime remains unverified.',
            'Malformed declarations/context checks are host oracle refusals, not device validation claims.',
            'Diagnostic correctness fixture only; no gameplay performance claim.']
        record['raw_report_sha256'] = digest(folder/'stdout.txt')
        result = args.result or bottle.results_dir(ROOT)/('varying-split-gpu.json' if args.mode=='qualification' else f'varying-split-{args.mode}-gpu.json')
        assert not result.exists(), 'preserve any accepted result; choose a new result path'
        result.parent.mkdir(parents=True, exist_ok=True)
        result.write_text(json.dumps(record, indent=2)+'\n')
        print(json.dumps(dict(result=str(result),cases=record["cases"],creates=record["shader_creates"],wall_seconds=record['wall_seconds'])))
    except Exception as error:
        record.update(error=str(error),wall_seconds=time.monotonic()-started)
        (folder/'failed-result.json').write_text(json.dumps(record,indent=2)+'\n')
        raise


if __name__ == '__main__':
    main()
