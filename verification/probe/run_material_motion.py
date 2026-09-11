#!/usr/bin/env python3
"""Fresh-build same-draw prototype; exact local shader inputs, no game launch."""
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
    'src/renderer/rigid_replay_program.h', 'src/renderer/rigid_replay_program.cpp',
    'src/renderer/rigid_position.h', 'src/renderer/rigid_position.cpp',
    'src/renderer/rigid_position_profiles_inc.h', 'src/renderer/position_path_profiles_inc.h',
    'src/renderer/pixel_coverage_profiles_inc.h', 'src/renderer/rigid_motion_pixel_program.h',
    'src/renderer/rigid_motion_pixel_program_inc.h', 'src/temporal/rigid_motion_ps.hlsl',
    'verification/probe/material_motion_fixture.cpp', 'verification/probe/build_material_motion.sh',
    'verification/probe/run_material_motion.py')
RAW = {
    Path('/tmp/x3-shader-sweep/programs/vs_53a0a641107ed76c.bin'): 'bc402d1c2bfbbcb9fedd98890db845dab2a24da8cfb5a88a74c4eafa40f7a50c',
    Path('/tmp/x3-shader-sweep/programs/ps_8759c7838bbc86c2.bin'): '9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0'}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def source_hashes():
    return {name: sha(ROOT / name) for name in SOURCES}

def validate_report(text):
    lines = text.splitlines()
    devices = re.findall(r'^DEVICE pure=(\d) mixed=(\d)$', text, re.M)
    assert len(devices) == 2 and [d[0] for d in devices] == ['0', '1'] and devices[0][1] == devices[1][1]
    mixed = devices[0][1] == '1'
    expected = (1182, 2952, 101318656, 164, 82) if mixed else (606, 1512, 100794368, 84, 42)
    terminal = 'RESULT PASS checks=%u numerical=%u color_components=%u depth_cases=%u configurations=%u devices=2' % expected
    assert lines[-1] == terminal and [s for s in lines if s.startswith('RESULT ')] == [terminal]
    assert 'FAIL' not in text
    checks = [s for s in lines if s.startswith('CHECK ')]
    assert len(checks) == expected[0] and all(s.endswith(' PASS') for s in checks)
    assert lines.count('RESET PASS') == 2
    cap = re.findall(r'^CAPS mrt=(\d+) misc=([0-9a-f]+) vs=([0-9a-f]+) ps=([0-9a-f]+) max_vs_const=(\d+) vs_slots=(\d+) ps_slots=(\d+)$', text, re.M)
    assert len(cap) == 1 and int(cap[0][0]) >= 2 and bool(int(cap[0][1],16) & 0x40000) == mixed
    assert len(re.findall(r'^FORMAT ', text, re.M)) == 3
    for name in ('d3d9.dll', 'wined3d.dll'):
        modules = re.findall(r'^MODULE name=' + re.escape(name) + r' path=(.+)$', text, re.M)
        assert len(modules) == 2 and all(p.lower().rstrip('\r') == 'c:\\windows\\system32\\' + name for p in modules)
    configs = re.findall(r'^CONFIG id=(\d+) width=(\d+) height=(\d+) format=(\d+) packed=(\d) perspective=(\d) lights=(\d+) valid=(\d) translation=([-\d.]+) jitter=([-\d.]+),([-\d.]+)$', text, re.M)
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
                max_replay_reference_error=max(float(r[3]) for r in reference),timings=timing_summary)

def no_game():
    p = subprocess.run(['pgrep','-ifl','[X]3AP[.]exe'],capture_output=True,text=True)
    assert p.returncode==1 and not p.stdout.strip(), 'X3AP running; no synthetic GPU run'

def main():
    result_path=RESULTS/'material-motion-summary.json'
    report_path=RESULTS/'material-motion.txt'
    result={'passed':False,'status':'RUNNING','game_launched':False,
            'scope':'One exact local shader pair, original synthetic geometry/textures; detached zero-origin opaque prototype, no production draw routing',
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
        subprocess.run(['sh',str(ROOT/'verification/probe/build_material_motion.sh')],check=True,cwd=ROOT)
        result['local_after_build']={str(p):sha(p) for p in RAW};assert result['local_after_build']==result['local_inputs']
        result['sources_after_build']=source_hashes();assert result['sources_after_build']==result['sources_before_build']
        result['native_after_build']=native_hashes();assert result['native_after_build']==result['native_before_build']
        result['executable_sha256']=sha(EXE)
        command=[str(wine),'--bottle','Steam','--no-update','--dll','d3d9=b',str(EXE)]+['Z:'+str(p) for p in RAW]
        result['command']=command;no_game()
        with report_path.open('w') as out,(RESULTS/'material-motion-wine.log').open('w') as err:
            process=subprocess.run(command,stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=180)
        result['exit_code']=process.returncode;assert process.returncode==0
        result.update(validate_report(report_path.read_text()))
        result['local_after_run']={str(p):sha(p) for p in RAW};assert result['local_after_run']==result['local_inputs']
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
