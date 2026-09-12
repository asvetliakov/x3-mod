#!/usr/bin/env python3
"""Fresh-build production-module integration; standalone Preview only."""
from pathlib import Path
import hashlib,json,os,re,subprocess,tempfile
root=Path(__file__).resolve().parents[2]
results=root/'verification/results'
exe=root/'verification/probe/build/temporal_pass_fixture.exe'
paths=[root/name for name in ('src/renderer/temporal_pass.h','src/renderer/temporal_pass.cpp','src/temporal/resolve.h','src/temporal/resolve.hlsl','src/temporal/depth_decode.hlsl','verification/probe/temporal_pass_fixture.cpp','verification/probe/build_temporal_pass.sh','verification/probe/run_temporal_pass.py')]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
hashes=lambda:{str(p.relative_to(root)):sha(p) for p in paths}
d3dx=Path.home()/'Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/d3dx9_37.dll'
report={'passed':False,'sources_before_build':hashes(),'game_launched':False,'d3dx9_37_sha256':sha(d3dx)}
try:
    subprocess.run(['sh',str(root/'verification/probe/build_temporal_pass.sh')],check=True,cwd=root)
    assert hashes()==report['sources_before_build'],'Source changed during build'
    report['executable_sha256']=sha(exe)
    command=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine','--bottle','Steam','--no-update','--dll','d3d9=b','--workdir',str(exe.parent),str(exe),r'C:\X3\d3dx9_37.dll','Z:'+str(root/'src/temporal/depth_decode.hlsl'),'Z:'+str(root/'src/temporal/resolve.hlsl')]
    report['command']=command
    with (results/'temporal-pass.txt').open('w') as out,(results/'temporal-pass-wine.log').open('w') as err:
        run=subprocess.run(command,stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=300)
    report['exit_code']=run.returncode
    text=(results/'temporal-pass.txt').read_text()
    report['source_unchanged']=hashes()==report['sources_before_build']
    report['binary_unchanged']=sha(exe)==report['executable_sha256']
    report['compiler_unchanged']=sha(d3dx)==report['d3dx9_37_sha256']
    report['report_sha256']=sha(results/'temporal-pass.txt')
    report['samples']=sum(line.startswith('SAMPLE ') and line.endswith(' PASS') for line in text.splitlines())
    match=re.search(r'RESULT PASS numerical=(\d+) state_restorations=(\d+) generations=(\d+)',text)
    report['state_restorations']=int(match[2]) if match else 0
    report['generations']=int(match[3]) if match else 0
    assert run.returncode==0 and match and tuple(map(int,match.groups()))==(318,164,2) and report['samples']==292 and 'RESET PASS' in text and 'FAIL' not in text,text[-1500:]
    assert report['source_unchanged'] and report['binary_unchanged'] and report['compiler_unchanged'],'Provenance changed during run'
    # Negative controls for the jitter convention: the stationary scene must
    # reject the plausible wrong lookups. Each variant mutates the two history
    # lookup lines of resolve.hlsl in a temporary copy (the tree is untouched)
    # and runs the fixture's stationary-only mode, which prints every metric and
    # then fails on the first one (the one-step oracle) with exit code 1.
    source=(root/'src/temporal/resolve.hlsl').read_text()
    motion_tap='previousUV = motion.xy + sizeJitter.zw;'
    camera_tap='previousUV += 0.5 * sizeJitter.xy + sizeJitter.zw;'
    assert source.count(motion_tap)==1 and source.count(camera_tap)==1,'resolve.hlsl lookup lines changed; update the negative controls'
    variants={'previous-jitter':(motion_tap.replace('sizeJitter.zw','history.xy'),camera_tap.replace('+ sizeJitter.zw','+ history.xy')),
              'flipped-sign':(motion_tap.replace('+ sizeJitter.zw','- sizeJitter.zw'),camera_tap.replace('+ sizeJitter.zw','- sizeJitter.zw')),
              'no-jitter':(motion_tap.replace(' + sizeJitter.zw',''),camera_tap.replace(' + sizeJitter.zw',''))}
    report['negative_controls']={}
    with tempfile.TemporaryDirectory(prefix='x3-temporal-negative-') as directory:
        for name,(motion_line,camera_line) in variants.items():
            mutated=Path(directory)/f'resolve-{name}.hlsl'
            mutated.write_text(source.replace(motion_tap,motion_line).replace(camera_tap,camera_line))
            negative=command[:-1]+['Z:'+str(mutated),'stationary-only']
            out_path=results/f'temporal-stationary-negative-{name}.txt'
            with out_path.open('w') as out,(results/'temporal-pass-wine.log').open('a') as err:
                control=subprocess.run(negative,stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=90)
            text=out_path.read_text()
            stationary=re.search(r'STATIONARY .*oracle_error=([0-9.]+) .*drift_px=([0-9.]+)',text)
            entry={'motion_line':motion_line,'camera_line':camera_line,'exit_code':control.returncode,
                   'oracle_error':float(stationary[1]) if stationary else None,'drift_px':float(stationary[2]) if stationary else None,
                   'report':out_path.name,'report_sha256':sha(out_path)}
            report['negative_controls'][name]=entry
            assert control.returncode!=0 and stationary and 'RESULT FAIL stationary one-step oracle' in text and entry['oracle_error']>0.1,(name,text[-800:])
    assert hashes()==report['sources_before_build'],'Source changed during the negative controls'
    report['passed']=True
finally:
    (results/'temporal-pass-summary.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
