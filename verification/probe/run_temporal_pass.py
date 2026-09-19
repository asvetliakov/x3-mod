#!/usr/bin/env python3
"""Fresh-build production-module integration; standalone Preview only."""
from pathlib import Path
import hashlib,json,os,re,subprocess,sys,tempfile
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
root=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/'tools/analysis'))
import analyze_iteration09_run2 as it09  # noqa: E402  the run-2 sharpness metrics (gradient energy, edge spread / MTF50)
results=bottle.results_dir(root)
exe=root/'verification/probe/build/temporal_pass_fixture.exe'
paths=[root/name for name in ('src/renderer/temporal_pass.h','src/renderer/temporal_pass.cpp','src/temporal/resolve.h','src/temporal/resolve.hlsl','src/temporal/resolve_filter.hlsl','src/temporal/resolve_snapshot.hlsl','src/renderer/temporal_resolve_snapshot_program_inc.h','src/renderer/temporal_resolve_program.h','src/temporal/resolve_thin.hlsl','src/renderer/temporal_resolve_thin_program_inc.h','src/temporal/resolve_thin_filter.hlsl','src/renderer/temporal_resolve_thin_filter_program_inc.h','src/temporal/resolve_age.hlsl','src/renderer/temporal_resolve_age_program_inc.h','src/temporal/resolve_age_filter.hlsl','src/renderer/temporal_resolve_age_filter_program_inc.h','verification/probe/temporal_flicker_inc.h','src/temporal/depth_decode.hlsl','src/temporal/sharpen.h','src/temporal/rcas.hlsl','src/temporal/taa_sharpen_ps.hlsl','verification/probe/temporal_pass_fixture.cpp','verification/probe/build_temporal_pass.sh','verification/probe/run_temporal_pass.py')]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
hashes=lambda:{str(p.relative_to(root)):sha(p) for p in paths}
d3dx=bottle.game_dir() / 'd3dx9_37.dll'
report={'passed':False,'sources_before_build':hashes(),'game_launched':False, 'bottle':bottle.describe(),'d3dx9_37_sha256':sha(d3dx)}
try:
    subprocess.run(['sh',str(root/'verification/probe/build_temporal_pass.sh')],check=True,cwd=root)
    assert hashes()==report['sources_before_build'],'Source changed during build'
    report['executable_sha256']=sha(exe)
    command=['/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine','--bottle',bottle.BOTTLE,'--no-update','--dll','d3d9=b','--workdir',str(exe.parent),str(exe),r'C:\X3\d3dx9_37.dll','Z:'+str(root/'src/temporal/depth_decode.hlsl'),'Z:'+str(root/'src/temporal/resolve.hlsl'),'Z:'+str(root/'src/temporal/taa_sharpen_ps.hlsl')]
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
    report['camera']={m.group(1):dict(drift_px=float(m.group(2)),error=float(m.group(3))) for m in re.finditer(r'CAMERA label=(\S+) frames=\d+ from=\d+ drift_px=([0-9.]+) error=([0-9.]+)',text)}
    # 508 / 278: the quad vertex program and copy mode twins (pre-review 28,
    # D1/D2 of the native-Windows audit) added 20 numerical and 25 state
    # checks per generation to the sharpen cases' 468 / 228 (review 23: 416 /
    # 164; stage 3: 448 / 204); the 386 samples are unchanged.
    assert run.returncode==0 and match and tuple(map(int,match.groups()))==(508,278,2) and report['samples']==386 and 'RESET PASS' in text and 'FAIL' not in text,text[-1500:]
    # The quad twins and the copy modes: byte-identical on this backend.
    report['quad_twins']=[dict(re.findall(r'(\w+)=(\S+)',line)) for line in text.splitlines() if line.startswith('QUAD_TWIN ')]
    report['copy_modes']=[dict(re.findall(r'(\w+)=(\S+)',line)) for line in text.splitlines() if line.startswith('COPY_MODE ')]
    assert len(report['quad_twins'])==6 and all(t['identical']=='1' for t in report['quad_twins']),report['quad_twins']
    assert len(report['copy_modes'])==2 and all(c['history_identical']=='1' and c['display_max_code_difference']=='0' for c in report['copy_modes']),report['copy_modes']
    # The sharpen cases' verdict lines (docs/verification/taa-sharpen.md).
    report['sharpen']={'verdicts':[dict((k,float(v)) for k,v in re.findall(r'(\w+)=([-0-9.e]+)',line)) for line in text.splitlines() if line.startswith('SHARPEN sharpness=')],
                       'nan':[dict((k,int(v)) for k,v in re.findall(r'(\w+)=(\d+)',line)) for line in text.splitlines() if line.startswith('SHARPEN_NAN ')],
                       'nonfinite':[line for line in text.splitlines() if line.startswith('SHARPEN_NONFINITE ')]}
    assert len(report['sharpen']['verdicts'])==4 and all(v['outside']==0 and v['alpha_diff']==0 and v['changed']>0 for v in report['sharpen']['verdicts']),report['sharpen']
    assert len(report['sharpen']['nan'])==2 and all(n['uniform']==1 for n in report['sharpen']['nan']),report['sharpen']
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
            negative=command[:-2]+['Z:'+str(mutated),command[-1],'stationary-only']
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
    # Sharpen measurement (docs/verification/taa-sharpen.md): the fixture's
    # 128x128 synthetic resolved-looking image through the pass at sharpness
    # 0 (the copy-back), 0.25, 0.5 and 1.0; the run-2 analysis functions give
    # the mean squared luma gradient (ratio against 0) and the slanted-edge
    # 10-90% rise and MTF50 of the strongest vertical edges (a comparison
    # between images of the same content, never an absolute MTF).
    measure_path=results/'temporal-sharpen-measure.txt'
    with measure_path.open('w') as out,(results/'temporal-pass-wine.log').open('a') as err:
        measure=subprocess.run(command+['sharpen-measure'],stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=120)
    measure_text=measure_path.read_text()
    assert measure.returncode==0 and 'RESULT PASS numerical=3 sharpen_measure=1' in measure_text and 'FAIL' not in measure_text,measure_text[-800:]
    images={}
    for line in measure_text.splitlines():
        if line.startswith('MEASURE '):
            entry=dict(re.findall(r'(\w+)=(\S+)',line))
            images[float(entry['sharpness'])]=(exe.parent/entry['file'],int(entry['width']),int(entry['height']))
    assert sorted(images)==[0.0,0.25,0.5,1.0],images
    def luma_image(path,width,height):
        data=path.read_bytes();assert len(data)==width*height*4,(path,len(data))
        return [(0.2126*data[i*4+2]+0.7152*data[i*4+1]+0.0722*data[i*4])/255.0 for i in range(width*height)]
    def metrics(path,width,height):
        image=luma_image(path,width,height);classes=[1]*(width*height)  # every pixel 'routed_interior' (it08.CLASSES index 1)
        gradient=it09.class_gradient_energy(image,classes,width,height)['all']['mean']
        edge=it09.edge_spread(image,classes,width,height,want=1)
        assert edge['status']=='evaluated',edge
        return {'gradient_energy':gradient,'rise_10_90_px':edge['rise_10_90_px'],'mtf50_cycles_per_px':edge['mtf50_cycles_per_px'],'edges':edge['edges'],'aligned':edge['aligned'],'sha256':sha(path)}
    base=metrics(*images[0.0])
    report['sharpen_measure']={'report':measure_path.name,'report_sha256':sha(measure_path),'off':base,'on':{}}
    for sharpness in (0.25,0.5,1.0):
        m=metrics(*images[sharpness]);m['gradient_energy_ratio']=m['gradient_energy']/base['gradient_energy']
        m['mtf50_ratio']=m['mtf50_cycles_per_px']/base['mtf50_cycles_per_px'];m['rise_ratio']=m['rise_10_90_px']/base['rise_10_90_px']
        report['sharpen_measure']['on'][str(sharpness)]=m
    ratios=[report['sharpen_measure']['on'][k]['gradient_energy_ratio'] for k in ('0.25','0.5','1.0')]
    assert ratios==sorted(ratios) and ratios[0]>1.0 and report['sharpen_measure']['on']['1.0']['rise_ratio']<1.0,report['sharpen_measure']
    assert hashes()==report['sources_before_build'],'Source changed during the measurement'
    # Run 139 (docs/verification/motion-output.md): the 1-px jittered lattice
    # under the filtered current sample (resolve_filter.hlsl, c22.y) and the
    # history weight. The fixture asserts: off path bit-identical to a pass
    # without the filtered program; shader = CPU definition of the filter;
    # 8-phase ripple ratios within 0.15 of the modelled 0.52 / 0.62 / 0.47;
    # flat regions within 1/255; the moving edge's bounds. Both programs'
    # instruction slots are recorded (the filtered one exceeds the 512 every
    # ps_3_0 device guarantees; the pass treats a refused creation as
    # 'filter unavailable').
    lattice_path=results/'temporal-lattice.txt'
    with lattice_path.open('w') as out,(results/'temporal-pass-wine.log').open('a') as err:
        lattice=subprocess.run(command+['lattice','Z:'+str(root/'src/temporal/resolve_filter.hlsl')],stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=600)
    lattice_text=lattice_path.read_text()
    fields=lambda prefix:[dict(re.findall(r'(\w+)=(\S+)',line)) for line in lattice_text.splitlines() if line.startswith(prefix)]
    number=lambda rows,key='config':{row.pop(key):{k:float(v) for k,v in row.items()} for row in rows}
    report['lattice']={'report':lattice_path.name,'report_sha256':sha(lattice_path),'exit_code':lattice.returncode,
                       'budget':number(fields('RESOLVE_BUDGET '),'variant'),'ripple':number(fields('LATTICE config=')),'oracle':number(fields('LATTICE_ORACLE ')),
                       'flat':number(fields('LATTICE_FLAT ')),'moving':number(fields('LATTICE_MOVING '))}
    # Flicker suppression (docs/architecture/taa-flicker-suppression.md, steps 0-3): the embedded programs' budgets, the
    # drifting-lattice table (per-pixel / 8x8-block band rms in codes, contrast, shader-vs-CPU-oracle error) and the step gates.
    report['flicker']={'caps':fields('FLICKER_CAPS '),'drift':fields('FLICKER_DRIFT '),'step1':fields('FLICKER_STEP1 '),'speed_gate':fields('FLICKER_SPEED_GATE '),
                       'near_depth':fields('FLICKER_NEAR_DEPTH '),'ghost':fields('FLICKER_GHOST '),'step2':fields('FLICKER_STEP2 ')+fields('FLICKER_STEP2_FAST '),'alpha':fields('FLICKER_ALPHA ')}
    # 28 / 9 are the run-139 lattice cases (LATTICE_BASE); the flicker cases add 182 numerical and 4 state checks.
    assert lattice.returncode==0 and 'LATTICE_BASE numerical=28 state_restorations=9' in lattice_text and 'RESULT PASS numerical=210 state_restorations=13 lattice=1' in lattice_text and 'FAIL' not in lattice_text,lattice_text[-1500:]
    assert len(report['flicker']['drift'])==64 and len(report['flicker']['near_depth'])==8 and all(float(v['instruction_slots'])<=512 for k,v in report['lattice']['budget'].items()),report['lattice']['budget']
    ripple=report['lattice']['ripple']
    assert len(ripple)==10 and ripple['off']==ripple['baseline'] and report['lattice']['budget']['plain']['instruction_slots']<=512,report['lattice']
    assert hashes()==report['sources_before_build'],'Source changed during the lattice cases'
    report['passed']=True
finally:
    (results/'temporal-pass-summary.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
