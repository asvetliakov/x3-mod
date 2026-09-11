#!/usr/bin/env python3
"""Fresh-build same-draw prototype; exact local shader inputs, no game launch.

The Argon pair runs the full configuration/timing inventory; every row of the
generated profile table then runs the color/motion/depth comparison (lights 0)
on a third device, reading its originals from the local sweep directory.
"""
from pathlib import Path
import hashlib
import json
import math
import os
import re
import shutil
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]
RESULTS = ROOT / 'verification/results'
EXE = ROOT / 'verification/probe/build/material_motion_fixture.exe'
SOURCES = (
    'src/renderer/material_motion.h', 'src/renderer/material_motion.cpp',
    'src/renderer/motion_output_profiles.h', 'src/renderer/motion_output_profiles_inc.h',
    'src/renderer/rigid_replay_program.h', 'src/renderer/rigid_replay_program.cpp',
    'src/renderer/rigid_position.h', 'src/renderer/rigid_position.cpp',
    'src/renderer/rigid_position_profiles_inc.h', 'src/renderer/position_path_profiles_inc.h',
    'src/renderer/pixel_coverage_profiles_inc.h', 'src/renderer/rigid_motion_pixel_program.h',
    'src/renderer/rigid_motion_pixel_program_inc.h', 'src/temporal/rigid_motion_ps.hlsl',
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
CLASS_LETTER = {'ReferenceRegisters': 'A', 'RelocatedRegisters': 'B', 'RelocatedRegistersWithBranches': 'C'}
ROW_CHECKS_PER_CONFIG = 14  # same_draw, replay reference, 9 covered samples, 2 bilateral, changed depth
ROW_SAMPLES_PER_CONFIG = 36
# Pixel boolean settings (b0 = bit 0, b1 = bit 1) per row class: class C repeats
# its configurations under all four, after a per-format control that the
# booleans change the original image.
BOOLEAN_COMBINATIONS = {'A': [0], 'B': [0], 'C': [0, 1, 2, 3]}
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
        configs = re.findall(r'^CONFIG id=(\d+) width=32 height=32 format=(\d+) packed=(\d) perspective=(\d) lights=0 valid=(\d) translation=([-\d.]+) jitter=([-\d.]+),([-\d.]+) booleans=(\d)$', '\n'.join(block), re.M)
        formats = [116, 21] if mixed else [116]
        combinations = BOOLEAN_COMBINATIONS[letter]
        assert [int(c[0]) for c in configs] == list(range(config_id, config_id + 3 * len(formats) * len(combinations)))
        assert [int(c[1]) for c in configs] == [f for f in formats for _ in range(3 * len(combinations))]
        assert [(c[2], c[3], c[4]) for c in configs] == [('0', '0', '1'), ('1', '1', '1'), ('1', '1', '0')] * (len(formats) * len(combinations))
        assert [int(c[8]) for c in configs] == [b for _ in formats for b in combinations for _ in range(3)]
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
        checks = [l for l in block if l.startswith('CHECK ')]
        assert len(checks) == ROW_CHECKS_PER_CONFIG * len(configs) + len(controls) and all(l.endswith(' PASS') for l in checks)
        assert lines[end] == head + f'PASS configurations={len(configs)}', lines[end]
        assert len(block) == len(configs) + len(color) + len(samples) + len(reference) + len(checks), 'unexpected lines in row block'
        totals['checks'] += 1 + len(checks); totals['numerical'] += len(samples)
        totals['color_components'] += sum(int(c[2]) for c in color)
        totals['depth_cases'] += 2 * len(configs); totals['configurations'] += len(configs)
        results.append(dict(index=index, vs=vs, ps=ps, transformation_class=letter, status='PASS',
                            configurations=len(configs), boolean_combinations=combinations, boolean_controls=len(controls),
                            checks=1 + len(checks), samples=len(samples),
                            color_components=sum(int(c[2]) for c in color), min_covered=min(int(c[3]) for c in color),
                            max_analytic_uv_error_pixels=uv, max_analytic_previous_depth_error=depth,
                            max_replay_reference_error=max(float(r[2]) for r in reference)))
    assert at == len(lines), 'trailing row output'
    return results, totals

def validate_report(text):
    all_lines = text.splitlines()
    begin = [k for k, l in enumerate(all_lines) if l.startswith('ROWS_BEGIN ')]
    assert len(begin) == 1 and all_lines[-1].startswith('RESULT ')
    lines, row_lines, terminal_line = all_lines[:begin[0]], all_lines[begin[0] + 1:-1], all_lines[-1]
    head = '\n'.join(lines) + '\n'
    devices = re.findall(r'^DEVICE pure=(\d) mixed=(\d)$', head, re.M)
    assert len(devices) == 2 and [d[0] for d in devices] == ['0', '1'] and devices[0][1] == devices[1][1]
    mixed = devices[0][1] == '1'
    expected = (1182, 2952, 101318656, 164, 82) if mixed else (606, 1512, 100794368, 84, 42)
    assert all_lines[begin[0]] == 'ROWS_BEGIN checks=%u numerical=%u color_components=%u depth_cases=%u configurations=%u' % expected
    assert [s for s in all_lines if s.startswith('RESULT ')] == [terminal_line]
    assert 'FAIL' not in text
    table = table_rows()  # Not `rows`: the timing loop below reuses that name.
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
    assert len(re.findall(r'^FORMAT ', text, re.M)) == 3
    for name in ('d3d9.dll', 'wined3d.dll'):
        modules = re.findall(r'^MODULE name=' + re.escape(name) + r' path=(.+)$', text, re.M)
        assert len(modules) == 2 and all(p.lower().rstrip('\r') == 'c:\\windows\\system32\\' + name for p in modules)
    configs = re.findall(r'^CONFIG id=(\d+) width=(\d+) height=(\d+) format=(\d+) packed=(\d) perspective=(\d) lights=(\d+) valid=(\d) translation=([-\d.]+) jitter=([-\d.]+),([-\d.]+) booleans=0$', text, re.M)
    assert len(configs) == expected[4] and [int(c[0]) for c in configs] == list(range(1, expected[4]+1))
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
        ('same-draw output matches independent authored replay',expected[4])):
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
                rows=len(table),row_transformed=transformed,row_skipped=len(table)-transformed,
                row_configurations=row_totals['configurations'],row_checks=row_totals['checks'],
                row_samples=row_totals['numerical'],row_color_components=row_totals['color_components'],
                row_depth_cases=row_totals['depth_cases'],row_results=row_results)

def no_game():
    p = subprocess.run(['pgrep','-ifl','[X]3AP[.]exe'],capture_output=True,text=True)
    assert p.returncode==1 and not p.stdout.strip(), 'X3AP running; no synthetic GPU run'

def main():
    result_path=RESULTS/'material-motion-summary.json'
    report_path=RESULTS/'material-motion.txt'
    result={'passed':False,'status':'RUNNING','game_launched':False,
            'scope':'Argon pair full inventory plus every profile-table row (lights 0) with the same original synthetic geometry/textures/constants; detached zero-origin opaque prototype, no production draw routing',
            'timing_scope':'QPC through EVENT completion, including clear/draw/switches; common setup fenced before QPC; one Begin/EndScene pair per workload; modes0color,1same-draw,2color+authoredGPUreplay; no timed readback or isolatedGPUduration',
            'cpu_baseline':'SSE2; stack realignment; four-byte incoming Win32 stack'}
    result_path.write_text(json.dumps(result,indent=2)+'\n')
    wine=Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
    windows=Path.home()/'Library/Application Support/CrossOver/Bottles/Steam/drive_c/windows/syswow64'
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
        command=[str(wine),'--bottle','Steam','--no-update','--dll','d3d9=b',str(EXE)]+['Z:'+str(p) for p in RAW]+['Z:'+str(PROGRAMS)]
        result['command']=command;no_game()
        with report_path.open('w') as out,(RESULTS/'material-motion-wine.log').open('w') as err:
            process=subprocess.run(command,stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=180)
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
if __name__=='__main__': main()
