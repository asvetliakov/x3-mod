#!/usr/bin/env python3
"""Fresh-build same-draw prototype; exact local shader inputs, no game launch.

The Argon pair runs the full configuration/timing inventory; every row of the
generated profile table then runs the color/motion/depth comparison (lights 0)
on a third device, reading its originals from the local sweep directory.
Class D rows are explicitly deferred to --damage, which supplies their native
XT ABI and can consume the retained fixture using --no-build.
"""
from pathlib import Path
import hashlib
import json
import math
import os
import re
import shutil
import statistics
import struct
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parent))
from game_guard import game_running  # noqa: E402
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory

ROOT = Path(__file__).resolve().parents[2]
RESULTS = bottle.results_dir(ROOT)
EXE = ROOT / 'verification/probe/build/material_motion_fixture.exe'
SOURCES = (
    'src/renderer/material_motion.h', 'src/renderer/material_motion.cpp',
    'src/renderer/damage_motion_validation.h',
    'src/renderer/motion_output_profiles.h', 'src/renderer/motion_output_profiles_inc.h',
    'src/renderer/rigid_replay_program.h', 'src/renderer/rigid_replay_program.cpp',
    'src/renderer/rigid_position.h', 'src/renderer/rigid_position.cpp',
    'src/renderer/rigid_position_profiles_inc.h', 'src/renderer/position_path_profiles_inc.h',
    'src/renderer/pixel_coverage_profiles_inc.h', 'src/renderer/rigid_motion_pixel_program.h',
    'src/renderer/rigid_motion_pixel_program_inc.h', 'src/temporal/rigid_motion_ps.hlsl',
    'src/renderer/current_depth_pixel_program.h', 'src/renderer/current_depth_pixel_program_inc.h',
    'src/temporal/current_depth_ps.hlsl',
    'verification/probe/material_motion_fixture.cpp', 'verification/probe/build_material_motion.sh',
    'verification/probe/run_material_motion.py')
PROGRAMS = Path('/tmp/x3-shader-sweep/programs')
RAW = {
    PROGRAMS / 'vs_53a0a641107ed76c.bin': 'bc402d1c2bfbbcb9fedd98890db845dab2a24da8cfb5a88a74c4eafa40f7a50c',
    PROGRAMS / 'ps_8759c7838bbc86c2.bin': '9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0'}
HEADER = ROOT / 'src/renderer/motion_output_profiles_inc.h'
PROFILES = RESULTS / 'motion-output-profiles.json'
ROW_PATTERN = re.compile(
    r'\{0x([0-9a-f]{16})ull, (\d+), 0xfffe0300u,\s*0x([0-9a-f]{16})ull, (\d+), 0xffff0300u,\s*'
    r'MotionOutputClass::(\w+),')
CLASS_LETTER = {'ReferenceRegisters': 'A', 'RelocatedRegisters': 'B', 'RelocatedRegistersWithBranches': 'C', 'BoundedDamageBranches': 'D'}
ROW_CHECKS_PER_CONFIG = 17  # same_draw, depth coverage, depth reference, replay reference, 9 covered samples, depth samples, 2 bilateral, changed depth
DEPTH_SAMPLES_PER_CONFIG = 9
ROW_SAMPLES_PER_CONFIG = 36
# Pixel boolean settings (b0 = bit 0, b1 = bit 1) per row class: class C repeats
# its configurations under all four, after a per-format control that the
# booleans change the original image.
BOOLEAN_COMBINATIONS = {'A': [0], 'B': [0], 'C': [0, 1, 2, 3], 'D': [0, 1, 2, 3]}
BOOLEAN_CONTROL = 'CHECK boolean branches change original material PASS'
ARGON = ('53a0a641107ed76c', '8759c7838bbc86c2')

def table_rows():
    """Rows of the generated header: (vs, vs_dwords, ps, ps_dwords, class letter)."""
    text = HEADER.read_text()
    rows = [(m[1], int(m[2]), m[3], int(m[4]), CLASS_LETTER[m[5]]) for m in ROW_PATTERN.finditer(text)]
    assert rows and text.count('{0x') == len(rows), 'generated header rows not parsed'
    return rows

def row_inputs(rows):
    """Local originals per row with SHA-256 cross-checked against the derived profile JSON; None when absent."""
    programs = json.loads(PROFILES.read_text())['programs']
    result = []
    for vs, vs_dwords, ps, ps_dwords, _ in rows:
        entry = {}
        for stage, fingerprint, dwords in (('vs', vs, vs_dwords), ('ps', ps, ps_dwords)):
            path = PROGRAMS / f'{stage}_{fingerprint}.bin'
            if path.exists():
                digest = sha(path)
                assert path.stat().st_size == dwords * 4 and programs[f'{stage}_{fingerprint}']['sha256'] == digest, path
                entry[stage] = {'path': str(path), 'sha256': digest, 'bytes': dwords * 4}
            else:
                entry[stage] = None
        result.append(entry)
    return result

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def source_hashes():
    return {name: sha(ROOT / name) for name in SOURCES}

def validate_rows(lines, mixed, first_config, rows, inputs):
    """Per-row blocks after ROWS_BEGIN: exact row inventory, config/color/sample/reference/check counts."""
    assert lines[0] == 'DEVICE pure=0 mixed=%d' % mixed, lines[0]
    at, config_id, results = 1, first_config, []
    totals = dict(checks=0, numerical=0, color_components=0, depth_cases=0, configurations=0)
    for index, ((vs, _, ps, _, letter), files) in enumerate(zip(rows, inputs)):
        head = f'ROW index={index} vs={vs} ps={ps} class={letter} status='
        if letter == 'D':
            assert lines[at] == head + 'SKIP reason=dedicated_damage_fixture'
            results.append(dict(index=index, vs=vs, ps=ps, transformation_class=letter, status='SKIP', reason='dedicated_damage_fixture'))
            at += 1
            continue
        if files['vs'] is None or files['ps'] is None:
            assert lines[at] == head + 'SKIP reason=missing_local_program', lines[at]
            results.append(dict(index=index, vs=vs, ps=ps, transformation_class=letter, status='SKIP',
                                reason='missing_local_program'))
            at += 1
            continue
        assert lines[at] == head + 'BEGIN' and lines[at + 1] == 'CHECK row local shader pair transformed PASS', lines[at:at + 2]
        at += 2
        end = next(k for k in range(at, len(lines)) if lines[k].startswith('ROW '))
        block, at = lines[at:end], end + 1
        configs = re.findall(r'^CONFIG id=(\d+) width=32 height=32 format=(\d+) packed=(\d) perspective=(\d) lights=0 valid=(\d) translation=([-\d.]+) jitter=([-\d.]+),([-\d.]+) booleans=(\d) depth_slope=([-\d.]+)$', '\n'.join(block), re.M)
        formats = [116, 21] if mixed else [116]
        combinations = BOOLEAN_COMBINATIONS[letter]
        assert [int(c[0]) for c in configs] == list(range(config_id, config_id + 3 * len(formats) * len(combinations)))
        assert [int(c[1]) for c in configs] == [f for f in formats for _ in range(3 * len(combinations))]
        assert [(c[2], c[3], c[4]) for c in configs] == [('0', '0', '1'), ('1', '1', '1'), ('1', '1', '0')] * (len(formats) * len(combinations))
        assert [int(c[8]) for c in configs] == [b for _ in formats for b in combinations for _ in range(3)]
        # The perspective configurations tilt the current clip z (depth_slope .05) so RT2 varies over the image.
        assert [float(c[9]) for c in configs] == [0.0 if c[3] == '0' else 0.05 for c in configs]
        # Class C: the boolean control precedes each format's configurations.
        controls = [k for k, l in enumerate(block) if l == BOOLEAN_CONTROL]
        config_lines = [k for k, l in enumerate(block) if l.startswith('CONFIG ')]
        assert len(controls) == (len(formats) if letter == 'C' else 0)
        assert controls == [config_lines[3 * len(combinations) * f] - 1 for f in range(len(formats))] if controls else True
        config_id += len(configs)
        color = re.findall(r'^COLOR (\w+) width=32 height=32 format=(\d+) components=(\d+) covered=(\d+) mismatches=(\d+) maximum=([^\s]+)$', '\n'.join(block), re.M)
        # 32x32 targets: at least half the pixels must be covered original geometry for the identity to mean anything.
        assert len(color) == 3 * len(configs) and all(int(c[3]) >= 512 and int(c[4]) == 0 and float(c[5]) == 0 for c in color)
        assert sum(c[0] == 'bilateral_depth_equal' for c in color) == 2 * len(configs) and sum(c[0] == 'same_draw' for c in color) == len(configs)
        samples = re.findall(r'^SAMPLE config=(\d+) x=(\d+) y=(\d+) channel=(\d) actual=([^ ]+) expected=([^ ]+) error=([^ ]+) pixel_error=([^ ]+) PASS$', '\n'.join(block), re.M)
        assert len(samples) == ROW_SAMPLES_PER_CONFIG * len(configs)
        uv = depth = 0.0
        for _, x, y, channel, actual, want, error, pixel_error in samples:
            k = int(channel); values = list(map(float, (actual, want, error, pixel_error)))
            assert all(map(math.isfinite, values)) and abs(abs(values[0] - values[1]) - values[2]) <= 1e-8
            assert values[3] <= .005 if k < 2 else values[2] <= 2e-6
            if k < 2: uv = max(uv, values[3])
            if k == 2: depth = max(depth, values[2])
        reference = re.findall(r'^REFERENCE config=(\d+) components=4096 mismatches=(\d+) max=([^\s]+)$', '\n'.join(block), re.M)
        assert len(reference) == len(configs) and all(int(r[1]) == 0 and float(r[2]) <= 2e-6 for r in reference)
        depth_samples, depth_coverage, depth_reference, max_current_depth = validate_depth(block, len(configs))
        checks = [l for l in block if l.startswith('CHECK ')]
        assert len(checks) == ROW_CHECKS_PER_CONFIG * len(configs) + len(controls) and all(l.endswith(' PASS') for l in checks)
        assert lines[end] == head + f'PASS configurations={len(configs)}', lines[end]
        assert len(block) == len(configs) + len(color) + len(samples) + len(reference) + len(checks) + len(depth_samples) + len(depth_coverage) + len(depth_reference), 'unexpected lines in row block'
        totals['checks'] += 1 + len(checks); totals['numerical'] += len(samples)
        totals['color_components'] += sum(int(c[2]) for c in color)
        totals['depth_cases'] += 2 * len(configs); totals['configurations'] += len(configs)
        results.append(dict(index=index, vs=vs, ps=ps, transformation_class=letter, status='PASS',
                            configurations=len(configs), boolean_combinations=combinations, boolean_controls=len(controls),
                            checks=1 + len(checks), samples=len(samples),
                            color_components=sum(int(c[2]) for c in color), min_covered=min(int(c[3]) for c in color),
                            max_analytic_uv_error_pixels=uv, max_analytic_previous_depth_error=depth,
                            max_replay_reference_error=max(float(r[2]) for r in reference),
                            current_depth_samples=len(depth_samples), current_depth_reference_pixels=sum(int(r[1]) for r in depth_reference),
                            max_current_depth_error=max_current_depth))
    assert at == len(lines), 'trailing row output'
    return results, totals

def validate_depth(text_or_lines, config_count):
    """RT2 evidence per configuration: nine analytic z/w samples (4e-6, the route
    oracle's tolerance), the covered/uncovered pattern and the ZFUNC EQUAL replay
    comparison (2e-6); returns the line groups and the largest analytic error."""
    text = '\n'.join(text_or_lines) if isinstance(text_or_lines, list) else text_or_lines
    samples = re.findall(r'^DEPTH_SAMPLE config=(\d+) x=(\d+) y=(\d+) actual=([^ ]+) expected=([^ ]+) error=([^ ]+) PASS$', text, re.M)
    assert len(samples) == DEPTH_SAMPLES_PER_CONFIG * config_count, (len(samples), config_count)
    for _, _, _, actual, want, error in samples:
        values = list(map(float, (actual, want, error)))
        assert all(map(math.isfinite, values)) and 0 <= values[0] <= 1 and abs(abs(values[0] - values[1]) - values[2]) <= 1e-8 and values[2] <= 4e-6
    coverage = re.findall(r'^DEPTH_COVERAGE config=(\d+) covered=(\d+) bad=(\d+)$', text, re.M)
    assert len(coverage) == config_count and all(int(c[1]) > 0 and int(c[2]) == 0 for c in coverage)
    reference = re.findall(r'^DEPTH_REFERENCE config=(\d+) compared=(\d+) mismatches=(\d+) max=([^\s]+)$', text, re.M)
    assert len(reference) == config_count and all(int(r[1]) > 0 and int(r[2]) == 0 and float(r[3]) <= 2e-6 for r in reference)
    return samples, coverage, reference, max(float(s[5]) for s in samples)

def validate_report(text, expected_table=None):
    """Validate the current inventory by default; callers checking retained historical
    evidence can supply its explicitly bounded table without rewriting that evidence."""
    all_lines = text.splitlines()
    begin = [k for k, l in enumerate(all_lines) if l.startswith('ROWS_BEGIN ')]
    assert len(begin) == 1 and all_lines[-1].startswith('RESULT ')
    lines, row_lines, terminal_line = all_lines[:begin[0]], all_lines[begin[0] + 1:-1], all_lines[-1]
    head = '\n'.join(lines) + '\n'
    devices = re.findall(r'^DEVICE pure=(\d) mixed=(\d)$', head, re.M)
    assert len(devices) == 2 and [d[0] for d in devices] == ['0', '1'] and devices[0][1] == devices[1][1]
    mixed = devices[0][1] == '1'
    # Per configuration three more checks than checkpoint B1 (RT2 coverage,
    # RT2 replay reference, RT2 analytic samples): 82 configurations -> +246.
    expected = (1428, 2952, 101318656, 164, 82) if mixed else (732, 1512, 100794368, 84, 42)
    assert all_lines[begin[0]] == 'ROWS_BEGIN checks=%u numerical=%u color_components=%u depth_cases=%u configurations=%u' % expected
    assert [s for s in all_lines if s.startswith('RESULT ')] == [terminal_line]
    assert 'FAIL' not in text
    table = table_rows() if expected_table is None else expected_table
    # Not `rows`: the timing loop below reuses that name.
    row_results, row_totals = validate_rows(row_lines, mixed, expected[4] + 1, table, row_inputs(table))
    transformed = sum(r['status'] == 'PASS' for r in row_results)
    assert any(r['status'] == 'PASS' and (r['vs'], r['ps']) == ARGON for r in row_results), 'the Argon row must run'
    terminal = 'RESULT PASS checks=%u numerical=%u color_components=%u depth_cases=%u configurations=%u devices=3 rows=%u row_transformed=%u row_skipped=%u' % (
        expected[0] + row_totals['checks'], expected[1] + row_totals['numerical'], expected[2] + row_totals['color_components'],
        expected[3] + row_totals['depth_cases'], expected[4] + row_totals['configurations'], len(table), transformed, len(table) - transformed)
    assert terminal_line == terminal, (terminal_line, terminal)
    text = head
    checks = [s for s in lines if s.startswith('CHECK ')]
    assert len(checks) == expected[0] and all(s.endswith(' PASS') for s in checks)
    assert lines.count('RESET PASS') == 2
    cap = re.findall(r'^CAPS mrt=(\d+) misc=([0-9a-f]+) vs=([0-9a-f]+) ps=([0-9a-f]+) max_vs_const=(\d+) vs_slots=(\d+) ps_slots=(\d+)$', text, re.M)
    assert len(cap) == 1 and int(cap[0][0]) >= 2 and bool(int(cap[0][1],16) & 0x40000) == mixed
    assert len(re.findall(r'^FORMAT ', text, re.M)) == 4
    r32f = re.findall(r'^FORMAT value=114 rt=([0-9a-f]+) ', text, re.M)
    assert r32f == ['00000000'], 'R32F render target support'
    for name in ('d3d9.dll', 'wined3d.dll'):
        modules = re.findall(r'^MODULE name=' + re.escape(name) + r' path=(.+)$', text, re.M)
        assert len(modules) == 2 and all(p.lower().rstrip('\r') == 'c:\\windows\\system32\\' + name for p in modules)
    configs = re.findall(r'^CONFIG id=(\d+) width=(\d+) height=(\d+) format=(\d+) packed=(\d) perspective=(\d) lights=(\d+) valid=(\d) translation=([-\d.]+) jitter=([-\d.]+),([-\d.]+) booleans=0 depth_slope=([-\d.]+)$', text, re.M)
    assert len(configs) == expected[4] and [int(c[0]) for c in configs] == list(range(1, expected[4]+1))
    assert all(float(c[11]) == (0.05 if c[5] == '1' else 0.0) for c in configs), 'perspective configurations tilt the current depth'
    depth_samples, depth_coverage, depth_reference, max_current_depth = validate_depth(text, expected[4])
    assert {(int(c[1]), int(c[2])) for c in configs} == {(32,32),(1280,768),(5120,1440)}
    assert {int(c[6]) for c in configs} == {0,1,8} and {int(c[7]) for c in configs} == {0,1}
    samples = re.findall(r'^SAMPLE config=(\d+) x=(\d+) y=(\d+) channel=(\d) actual=([^ ]+) expected=([^ ]+) error=([^ ]+) pixel_error=([^ ]+) PASS$', text, re.M)
    assert len(samples) == expected[1]
    maximum_uv_pixels = maximum_depth = 0.0
    inventory = {}
    for cid,x,y,channel,actual,want,error,pixel_error in samples:
        cid,x,y,k = map(int,(cid,x,y,channel))
        w,h = map(int,configs[cid-1][1:3])
        values = list(map(float,(actual,want,error,pixel_error)))
        assert all(map(math.isfinite,values))
        assert 0 <= values[2] and 0 <= values[3]
        assert abs(abs(values[0]-values[1])-values[2]) <= 1e-8
        assert abs(values[3]-values[2]*(w if k==0 else h if k==1 else 1)) <= 1e-7
        assert values[3] <= .005 if k < 2 else values[2] <= 2e-6
        inventory.setdefault(cid, []).append((x,y,k))
        if k < 2:
            maximum_uv_pixels = max(maximum_uv_pixels, values[3])
        if k == 2:
            maximum_depth = max(maximum_depth, values[2])
    for cid, config in enumerate(configs, 1):
        w,h = map(int,config[1:3])
        assert sorted(inventory[cid]) == sorted((x,y,k) for x in (w//4,w//2,3*w//4) for y in (h//4,h//2,3*h//4) for k in range(4))
    color = re.findall(r'^COLOR (\w+) width=(\d+) height=(\d+) format=(\d+) components=(\d+) covered=(\d+) mismatches=(\d+) maximum=([^\s]+)$', text, re.M)
    groups = 8 if mixed else 4
    assert len(color) == expected[4]*3 + groups*2 and sum(int(c[4]) for c in color) == expected[2]
    assert all(int(c[5]) > 0 and int(c[6]) == 0 and float(c[7]) == 0 for c in color)
    assert sum(c[0]=='bilateral_depth_equal' for c in color) == expected[3]
    for label,count in (
        ('native point-light count changes original material',groups),
        ('native material constant changes original material',groups),
        ('changed depth rejects all tested fragments',expected[4]),
        ('same-draw output matches independent authored replay',expected[4]),
        ('current depth written exactly where the original covered',expected[4]),
        ('current depth matches the ZFUNC EQUAL replay of the rasterized depth',expected[4]),
        ('current depth samples match the analytic z/w',expected[4])):
        assert lines.count('CHECK ' + label + ' PASS') == count
    reference = re.findall(r'^REFERENCE config=(\d+) components=(\d+) mismatches=(\d+) max=([^\s]+)$',text,re.M)
    assert len(reference) == expected[4] and [int(r[0]) for r in reference] == list(range(1,expected[4]+1))
    assert all(int(r[2])==0 and math.isfinite(float(r[3])) and float(r[3])<=2e-6 for r in reference)
    timings = re.findall(r'^TIMING width=(\d+) height=(\d+) format=(\d+) iteration=(\d+) mode=(\d) completed_ms=([^ ]+) timed_readback=0 gpu_timestamp=0 scene_pairs=1 draws=([12])$', text,re.M)
    assert len(timings)==36
    assert all(int(t[6])==(2 if int(t[4])==2 else 1) for t in timings)
    timing_summary=[]
    for w,h in ((1280,768),(5120,1440)):
        rows=[t for t in timings if (int(t[0]),int(t[1]))==(w,h)]
        assert [int(t[3]) for t in rows]==list(range(18))
        assert [int(t[4]) for t in rows]==[2-i%3 if (i//3)%2 else i%3 for i in range(18)]
        for mode in range(3):
            values=[float(t[5]) for t in rows if int(t[4])==mode]
            assert len(values)==6 and all(math.isfinite(v) and v>=0 for v in values)
            timing_summary.append(dict(width=w,height=h,mode=mode,samples=6,mean_ms=statistics.mean(values),median_ms=statistics.median(values),minimum_ms=min(values),maximum_ms=max(values)))
    return dict(zip(('checks','numerical','color_components','depth_cases','configurations'),expected),mixed_bit_depth=mixed,
                max_analytic_uv_error_pixels=maximum_uv_pixels,max_analytic_previous_depth_error=maximum_depth,
                max_replay_reference_error=max(float(r[3]) for r in reference),timings=timing_summary,
                current_depth_samples=len(depth_samples),current_depth_reference_pixels=sum(int(r[1]) for r in depth_reference),
                max_current_depth_error=max_current_depth,
                rows=len(table),row_transformed=transformed,row_skipped=len(table)-transformed,
                row_configurations=row_totals['configurations'],row_checks=row_totals['checks'],
                row_samples=row_totals['numerical'],row_color_components=row_totals['color_components'],
                row_depth_cases=row_totals['depth_cases'],row_results=row_results)

DAMAGE_PS = ('31445adb0a62d134', 'd51cf763125cb85a')
DAMAGE_VS = '37c34a7478544c14'

def validate_damage_report(text):
    """Damage-only inventory: never infer branch coverage from color identity."""
    lines = text.splitlines()
    assert lines and lines[-1].startswith('DAMAGE_RESULT PASS ') and 'FAIL' not in text
    assert sum(l.startswith('DAMAGE_RESULT ') for l in lines) == 1
    begin = re.findall(r'^DAMAGE_BEGIN mixed=([01])$', text, re.M)
    assert len(begin) == 1
    formats = (116, 21) if begin == ['1'] else (116,)
    modules = re.findall(r'^DAMAGE_MODULE name=d3d9.dll path=(.+)$', text, re.M)
    assert len(modules) == 1 and modules[0].lower().rstrip('\r') == 'c:\\windows\\system32\\d3d9.dll'
    caps = re.findall(r'^DAMAGE_CAPS mrt=(\d+) misc=([0-9a-f]+) vs=([0-9a-f]+) ps=([0-9a-f]+) max_vs_const=(\d+) vs_slots=(\d+) ps_slots=(\d+)$', text, re.M)
    assert len(caps) == 1 and int(caps[0][0]) >= 3 and int(caps[0][4]) >= 256
    assert bool(int(caps[0][1],16)&0x40000) == (begin == ['1'])
    assert int(caps[0][2],16) >= 0xfffe0300 and int(caps[0][3],16) >= 0xffff0300
    assert int(caps[0][5]) >= 512 and int(caps[0][6]) >= 512
    headers = re.findall(r'^DAMAGE_CASE ps=([0-9a-f]{16}) generation=([01]) depth=([01]) poison=([01]) winding=([01]) format=(\d+) booleans=([0-3])$', text, re.M)
    expected = [(ps, str(g), str(d), str(p), str(w), str(f), str(b))
                for g in range(2) for ps in DAMAGE_PS for d in range(2) for p in range(2)
                for w in range(2 if ps == DAMAGE_PS[0] else 1) for f in formats for b in range(4)]
    assert headers == expected, 'damage case inventory'
    traces = re.findall(r'^DAMAGE_WITNESS_PIXEL x=(\d+) y=(\d+) expected=([^ ]+) expected_uv=([^ ]+) actual=([^ ]+) bits=([^ ]+) sampled_red_uv_face=([^ ]+) sampled_bits=([^ ]+)$', text, re.M)
    assert len(traces) == 32, 'one bounded first-case native input/branch trace'
    assert [(int(t[0]),int(t[1])) for t in traces] == [(x,16) for x in range(32)]
    trace_lines = [i for i,l in enumerate(lines) if l.startswith('DAMAGE_WITNESS_PIXEL ')]
    case_lines = [i for i,l in enumerate(lines) if l.startswith('DAMAGE_CASE ')]
    assert all(case_lines[0] < i < case_lines[1] for i in trace_lines), 'trace belongs to first case'
    for x, _, expected_values, expected_uv, actual_values, actual_bits, input_values, input_bits in traces:
        x=int(x);pre=.5-.5*(x%3)
        expected_values=list(map(float,expected_values.split(',')))
        assert expected_values == [max(pre,0.),pre,float(x%3==2)]
        actual_values=list(map(float,actual_values.split(',')))
        input_values=list(map(float,input_values.split(',')))
        for values,bits in ((actual_values,actual_bits),(input_values,input_bits)):
            bit_values=bits.split(',')
            assert len(values)==len(bit_values)==4 and all(map(math.isfinite,values))
            assert all(re.fullmatch(r'[0-9a-f]{8}',b) for b in bit_values)
            assert all(struct.pack('<f',v)==int(b,16).to_bytes(4,'little') for v,b in zip(values,bit_values)), 'trace floats and bits agree'
        assert actual_values[:3] == expected_values and actual_values[3] != 0
        assert input_values[0] == .25+.25*(x%3) and input_values[3] != 0 and (input_values[3]>0) == (actual_values[3]>0)
        # Center sampling must be safely inside the intended texel; retained
        # raw UV bits allow a stricter backend-specific precision investigation.
        assert list(map(float,expected_uv.split(','))) == [(x+.5)/32,16.5/32]
        assert abs(input_values[1]-(x+.5)/32) <= .25/32 and abs(input_values[2]-16.5/32) <= .25/32

    for label in ('damage native b0 isolated sensitivity', 'damage native b1 isolated sensitivity', 'damage native alpha sensitivity', 'damage native occlusion sensitivity', 'damage native detail sensitivity'):
        assert lines.count('CHECK '+label+' PASS') == len(headers)//4, label
    starts = [i for i, l in enumerate(lines) if l.startswith('DAMAGE_CASE ')]
    config_id, face_signs = 1, {}
    for case, start in zip(headers, starts):
        end = next(i for i in range(start + 1, len(lines)) if lines[i].startswith(('DAMAGE_CASE ', 'DAMAGE_RESET ', 'DAMAGE_TIMING ', 'CHECK damage exact original pair transformed', 'CHECK native alpha immediately', 'CHECK damage poison input framing', 'CHECK damage timing pair transformed')))
        block = '\n'.join(lines[start + 1:end])
        ps, generation, depth, poison, winding, fmt, booleans = case
        witness = re.findall(r'^DAMAGE_WITNESS booleans=([0-3]) winding=([01]) below=(\d+) equal=(\d+) above=(\d+) face_sign=(-?1) bad=(\d+)$', block, re.M)
        assert len(witness) == 1 and witness[0][:2] == (booleans, winding)
        assert witness[0][2:5] == ('11', '11', '10') and witness[0][6] == '0'
        assert block.count('CHECK adjacent damage IFC false threshold true native witness PASS') == 1
        key = (ps, generation, depth, poison, fmt, booleans)
        face_signs.setdefault(key, {})[winding] = int(witness[0][5])
        configs = re.findall(r'^CONFIG id=(\d+) width=32 height=32 format=(\d+) packed=([01]) perspective=([01]) lights=0 valid=([01]) translation=([^ ]+) jitter=([^ ]+) booleans=([0-3]) depth_slope=([^ ]+)$', block, re.M)
        assert len(configs) == 3
        assert [int(c[0]) for c in configs] == [config_id, config_id + 1, config_id + 2]
        assert all(c[1] == fmt and c[2] == str(int(booleans) & 1) and c[7] == booleans for c in configs)
        assert [(c[3], c[4], c[5], c[6], c[8]) for c in configs] == [('0', '1', '0.000000', '0.000000,0.000000', '0.000000'), ('1', '1', '0.125000', '0.250000,-0.375000', '0.050000'), ('1', '0', '0.125000', '0.250000,-0.375000', '0.050000')]
        colors = re.findall(r'^COLOR (\w+) width=32 height=32 format=(\d+) components=(\d+) covered=(\d+) mismatches=(\d+) maximum=([^\s]+)$', block, re.M)
        assert len(colors) == 9 and all(c[1] == fmt and c[2] == '4096' and int(c[3]) >= 512 and c[4:] == ('0', '0') for c in colors)
        assert [c[0] for c in colors] == ['same_draw', 'bilateral_depth_equal', 'bilateral_depth_equal'] * 3
        samples = re.findall(r'^SAMPLE config=(\d+) x=(\d+) y=(\d+) channel=(\d) actual=([^ ]+) expected=([^ ]+) error=([^ ]+) pixel_error=([^ ]+) PASS$', block, re.M)
        assert len(samples) == 108
        assert [(int(s[0]), int(s[1]), int(s[2]), int(s[3])) for s in samples] == [(cid, x, y, k) for cid in (config_id, config_id + 1, config_id + 2) for y in (8, 16, 24) for x in (8, 16, 24) for k in range(4)]
        for *_, channel, actual, want, error, pixels in samples:
            a, b, e, px = map(float, (actual, want, error, pixels))
            assert all(map(math.isfinite, (a, b, e, px))) and e >= 0 and px >= 0
            assert abs(abs(a-b)-e) <= 1e-8 and abs(px-e*(32 if int(channel)<2 else 1)) <= 1e-7
            assert px <= .005 if int(channel) < 2 else e <= 2e-6
        references = re.findall(r'^REFERENCE config=(\d+) components=(\d+) mismatches=(\d+) max=([^\s]+)$', block, re.M)
        assert len(references) == 3 and [int(r[0]) for r in references] == [config_id, config_id+1, config_id+2]
        assert all(r[1:3] == ('4096', '0') and math.isfinite(float(r[3])) and 0 <= float(r[3]) <= 2e-6 for r in references)
        if depth == '1':
            ds, dc, dr, _ = validate_depth(block, 3)
            assert [int(s[0]) for s in ds] == [config_id]*9 + [config_id+1]*9 + [config_id+2]*9
            assert [int(r[0]) for r in dc] == [config_id, config_id+1, config_id+2] and [int(r[0]) for r in dr] == [config_id, config_id+1, config_id+2]
        else:
            assert 'DEPTH_' not in block and block.count('CHECK motion-only depth target remains clear PASS') == 3
        assert block.count('CHECK changed depth rejects all tested fragments PASS') == 3
        config_id += 3
    assert all(v['0'] == -v['1'] for key, v in face_signs.items() if key[0] == DAMAGE_PS[0]), '2s opposite vFace signs'
    assert lines.count('DAMAGE_RESET PASS') == 1
    assert re.findall(r'^DAMAGE_TIMING ps=([0-9a-f]{16})$', text, re.M) == list(DAMAGE_PS)
    timing = re.findall(r'^TIMING width=1280 height=768 format=(\d+) iteration=(\d+) mode=([012]) completed_ms=([^ ]+) timed_readback=0 gpu_timestamp=0 scene_pairs=1 draws=([12])$', text, re.M)
    assert len(timing) == 36
    for rows in (timing[:18], timing[18:]):
        assert [int(t[1]) for t in rows] == list(range(18))
        assert [int(t[2]) for t in rows] == [2-i%3 if (i//3)%2 else i%3 for i in range(18)]
        assert all(math.isfinite(float(t[3])) and float(t[3]) >= 0 and int(t[4]) == (2 if t[2] == '2' else 1) for t in rows)
    terminal = re.fullmatch(r'DAMAGE_RESULT PASS checks=(\d+) numerical=(\d+) color_components=(\d+) depth_cases=(\d+) configurations=(\d+)', lines[-1])
    assert terminal
    configs = config_id-1
    assert tuple(map(int, terminal.groups())) == (sum(l.startswith('CHECK ') for l in lines), configs*36, configs*4096*3, configs*2, configs)
    assert all(l.endswith(' PASS') for l in lines if l.startswith('CHECK '))
    return dict(cases=len(headers), configurations=configs, checks=int(terminal[1]), numerical=configs*36,
                exact_color_components=configs*4096*3, depth_cases=configs*2,
                mixed_bit_depth=begin == ['1'], native_d3d9_path=modules[0], caps=caps[0], resets=1, timing_samples=len(timing),
                scope='two exact damage pairs; native RGB/alpha, adjacent IFC witness, b0/b1, windings, poisoned epilogue scratch, both depth modes, replay and analytic references')

def run_damage(no_build=False):
    """One scoped fixture artifact, no production DLL build or game launch."""
    RESULTS.mkdir(parents=True, exist_ok=True)
    report = RESULTS/'material-motion-damage.txt'
    summary = RESULTS/'material-motion-damage-summary.json'
    inputs = [PROGRAMS/f'vs_{DAMAGE_VS}.bin'] + [PROGRAMS/f'ps_{p}.bin' for p in DAMAGE_PS]
    result = dict(passed=False, game_launched=False, bottle=bottle.describe(), status='RUNNING')
    try:
        no_game()
        result['inputs'] = {str(p):sha(p) for p in inputs}
        result['source_commit'] = subprocess.check_output(['git','rev-parse','HEAD'], cwd=ROOT, text=True).strip()
        result['source_hashes'] = {name:sha(ROOT/name) for name in SOURCES}
        if not no_build:
            subprocess.run(['sh',str(ROOT/'verification/probe/build_material_motion.sh')],cwd=ROOT,check=True)
        result['executable_sha256'] = sha(EXE)
        wine = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
        command = [str(wine),'--bottle',bottle.BOTTLE,'--no-update','--dll','d3d9=b',str(EXE),'--damage','Z:'+str(PROGRAMS)]
        result['command'] = command
        no_game()
        with report.open('w') as out, (RESULTS/'material-motion-damage-wine.log').open('w') as err:
            process = subprocess.run(command, stdout=out, stderr=err, env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=900)
        result['exit_code'] = process.returncode
        assert process.returncode == 0
        result.update(validate_damage_report(report.read_text()))
        assert result['inputs'] == {str(p):sha(p) for p in inputs}
        assert result['executable_sha256'] == sha(EXE)
        assert result['source_hashes'] == {name:sha(ROOT/name) for name in SOURCES}
        result.update(passed=True,status='PASS',report_sha256=sha(report))
    except BaseException as error:
        result.update(status='FAIL',error=repr(error))
        raise
    finally:
        summary.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps(result,indent=2))


def no_game():
    assert not game_running(), 'X3AP running; no synthetic GPU run'

def main():
    result_path=RESULTS/'material-motion-summary.json'
    report_path=RESULTS/'material-motion.txt'
    result={'passed':False,'status':'RUNNING','game_launched':False, 'bottle':bottle.describe(),
            'scope':'Argon pair full inventory plus every profile-table row (lights 0) with the same original synthetic geometry/textures/constants, motion (RT1) and current depth (RT2) outputs; detached zero-origin opaque prototype, no production draw routing',
            'timing_scope':'QPC through EVENT completion, including clear/draw/switches; common setup fenced before QPC; one Begin/EndScene pair per workload; modes0color,1same-draw,2color+authoredGPUreplay; no timed readback or isolatedGPUduration',
            'cpu_baseline':'SSE2; stack realignment; four-byte incoming Win32 stack'}
    result_path.write_text(json.dumps(result,indent=2)+'\n')
    wine=Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
    windows=bottle.bottle_dir() / 'drive_c/windows/syswow64'
    native=[]
    def native_hashes(): return {str(p):sha(p) for p in native}
    try:
        native=[wine,windows/'d3d9.dll',windows/'wined3d.dll',Path(shutil.which('i686-w64-mingw32-g++')).resolve()]
        no_game()
        result['native_module_mapping']='32-bit process logical C:\\windows\\system32 maps host bottle windows/syswow64 PE32 files'
        for name in ('d3d9.dll','wined3d.dll'):
            data=(windows/name).read_bytes();offset=int.from_bytes(data[60:64],'little')
            assert data[offset:offset+6]==b'PE\0\0\x4c\x01', 'Expected actual x86 native module'
        result['sources_before_build']=source_hashes();result['native_before_build']=native_hashes()
        result['local_inputs']={str(p):sha(p) for p in RAW};assert all(sha(p)==h for p,h in RAW.items())
        rows=table_rows();result['programs_directory']=str(PROGRAMS);result['profiles_json_sha256']=sha(PROFILES)
        result['row_inputs']=row_inputs(rows)
        def rows_unchanged(): return row_inputs(rows)==result['row_inputs']
        subprocess.run(['sh',str(ROOT/'verification/probe/build_material_motion.sh')],check=True,cwd=ROOT)
        result['local_after_build']={str(p):sha(p) for p in RAW};assert result['local_after_build']==result['local_inputs'] and rows_unchanged()
        result['sources_after_build']=source_hashes();assert result['sources_after_build']==result['sources_before_build']
        result['native_after_build']=native_hashes();assert result['native_after_build']==result['native_before_build']
        result['executable_sha256']=sha(EXE)
        command=[str(wine),'--bottle',bottle.BOTTLE,'--no-update','--dll','d3d9=b',str(EXE)]+['Z:'+str(p) for p in RAW]+['Z:'+str(PROGRAMS)]
        result['command']=command;no_game()
        with report_path.open('w') as out,(RESULTS/'material-motion-wine.log').open('w') as err:
            process=subprocess.run(command,stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=1800)
        result['exit_code']=process.returncode;assert process.returncode==0
        result.update(validate_report(report_path.read_text()))
        result['local_after_run']={str(p):sha(p) for p in RAW};assert result['local_after_run']==result['local_inputs'] and rows_unchanged()
        result['sources_after_run']=source_hashes();result['native_after_run']=native_hashes()
        assert result['sources_after_run']==result['sources_before_build'] and result['native_after_run']==result['native_before_build']
        assert sha(EXE)==result['executable_sha256'] and all(sha(p)==h for p,h in RAW.items())
        result['wine_stderr_sha256']=sha(RESULTS/'material-motion-wine.log')
        result['report_sha256']=sha(report_path);result['passed']=True;result['status']='PASS'
    except BaseException as error:
        result['status']='FAIL';result['error']=repr(error);raise
    finally:
        result_path.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--damage', action='store_true', help='qualify only the two bounded damage pairs with native XT inputs')
    parser.add_argument('--no-build', action='store_true', help='consume the retained damage fixture executable')
    args=parser.parse_args()
    if args.no_build and not args.damage: parser.error('--no-build currently requires --damage')
    if args.damage: run_damage(args.no_build)
    else: main()
