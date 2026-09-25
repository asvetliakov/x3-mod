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
paths=[root/name for name in ('src/renderer/temporal_pass.h','src/renderer/temporal_pass.cpp','src/temporal/resolve.h','src/temporal/resolve.hlsl','src/temporal/resolve_snapshot.hlsl','src/renderer/temporal_resolve_snapshot_program_inc.h','src/renderer/temporal_resolve_program.h','src/temporal/resolve_thin.hlsl','src/renderer/temporal_resolve_thin_program_inc.h','src/temporal/resolve_age.hlsl','src/renderer/temporal_resolve_age_program_inc.h','verification/probe/temporal_flicker_inc.h','verification/probe/temporal_line_inc.h','verification/probe/temporal_far_inc.h','verification/probe/temporal_thin_region_inc.h','src/temporal/thin_box_ps.hlsl','src/temporal/resolve_far.hlsl','src/renderer/temporal_resolve_far_program_inc.h','src/temporal/line_mask_ps.hlsl','src/renderer/temporal_line_mask_program_inc.h','src/temporal/line_mask_depth_ps.hlsl','src/renderer/temporal_line_mask_depth_program_inc.h','verification/probe/temporal_depth_fold_inc.h','verification/probe/temporal_history_taps_inc.h',
    'src/temporal/resolve_far_camera_hold.hlsl','src/renderer/temporal_resolve_far_camera_hold_program_inc.h','src/temporal/thin_box_hold_ps.hlsl','src/renderer/temporal_thin_box_hold_program_inc.h',
    'verification/probe/temporal_fold_timing_inc.h',
    'verification/probe/temporal_far_camera_inc.h',
    'verification/probe/temporal_far_jitter_line_inc.h',
    'verification/probe/temporal_region_hold_inc.h','verification/probe/temporal_box_half_inc.h',
    'verification/probe/temporal_thin_source_inc.h','src/temporal/line_mask_depth_thin_ps.hlsl','src/renderer/temporal_line_mask_depth_thin_program_inc.h',
     'src/temporal/thin_box_rows_half_ps.hlsl','src/renderer/temporal_thin_box_rows_half_program_inc.h','src/temporal/thin_box_columns_half_ps.hlsl','src/renderer/temporal_thin_box_columns_half_program_inc.h','src/temporal/depth_decode.hlsl','src/temporal/sharpen.h','src/temporal/rcas.hlsl','src/temporal/taa_sharpen_ps.hlsl','verification/probe/temporal_pass_fixture.cpp','verification/probe/build_temporal_pass.sh','verification/probe/run_temporal_pass.py')]
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
    report['camera_pan']=[dict(f.split('=') for f in l.split()[1:]) for l in text.splitlines() if l.startswith('CAMERA_PAN ')]
    report['camera']={m.group(1):dict(drift_px=float(m.group(2)),error=float(m.group(3))) for m in re.finditer(r'CAMERA label=(\S+) frames=\d+ from=\d+ drift_px=([0-9.]+) error=([0-9.]+)',text)}
    # 508 / 278: the quad vertex program and copy mode twins (pre-review 28,
    # D1/D2 of the native-Windows audit) added 20 numerical and 25 state
    # checks per generation to the sharpen cases' 468 / 228 (review 23: 416 /
    # 164; stage 3: 448 / 204). 510 / 388: the camera pan case (run215, one
    # numerical check and sample per generation) on top of 508 / 386. 524 / 398:
    # the SETA sweep (seta-motion.md; two edge sequences with their motion
    # contract check and five metrics per generation) on top of 510 / 388;
    # 532 / 402: the fade-band sweep (two more sequences, two metrics). 546 / 408: the band term
    # (seta-motion.md section 4): one more SETA-sweep metric, the slow sweep and the static ring
    # (two sequences and one metric each). 552 / 410: the pan case (two sequences, one metric). 570 / 416: the
    # fade-band-beside-occluder, projective-pan and huge-camera-path cases (two sequences and one metric each). 616 / 444: the
    # exit reset (seta-sky-hull-share-decay.md section 6: nine age-program sequences and fourteen metrics per generation). 672 / 488: the
    # same rows on the far and far-camera programs (six sequences and eleven metrics per generation). 712 / 528: the motion
    # history weight (taa-motion-history-weight.md section 6: ten rows on two age programs, ten metrics per program and generation).
    # 744 / 546: the dust motes' streak over sky, case (m) of fog-dust-motes.md section 5.4 (seven sequences and nine metrics
    # per generation).
    assert run.returncode==0 and match and tuple(map(int,match.groups()))==(744,278,2) and report['samples']==546 and 'RESET PASS' in text and 'FAIL' not in text,text[-1500:]
    # Motion history weight rows (docs/architecture/taa-motion-history-weight.md): the age programs' keep weight capped by the
    # smaller of the translation parallax and the screen motion. Off path, every slow row, the pan and the co-moving hull
    # bit-identical; the age target never differs; the half-texel 12.5 px/frame row is sharper (E ratio) with the cap 0.8.
    report['motion_weight']=[dict(re.findall(r'(\w+)=(\S+)',line)) for line in text.splitlines() if line.startswith('MOTION_WEIGHT ')]
    weight_rows={(row['program'],row['row'],row['on']):row for row in report['motion_weight'][:42]} # two programs x (ten rows x off / on + the 6.5 bound run) per generation
    assert len(report['motion_weight'])==84 and len(weight_rows)==42,report['motion_weight']
    for program in ('age','far_camera'):
        for name in ('rest','1px','1.5px','pan12.5','comove12.5'):
            assert weight_rows[(program,name,'1')]['output_diff']=='0.000000' and weight_rows[(program,name,'1')]['age_diff']=='0.000000',weight_rows[(program,name,'1')]
        assert all(weight_rows[(program,name,'1')]['age_diff']=='0.000000' for name in ('5px','5.5px','6.5px','12px','12.5px')),program
        assert float(weight_rows[(program,'6.5px','2')]['output_diff'])<=.002<.01<=float(weight_rows[(program,'6.5px','1')]['output_diff']),(weight_rows[(program,'6.5px','1')],weight_rows[(program,'6.5px','2')])
        assert float(weight_rows[(program,'12.5px','1')]['e_ratio'])>=1.5*float(weight_rows[(program,'12.5px','0')]['e_ratio']) and float(weight_rows[(program,'12.5px','1')]['output_diff'])>0,(weight_rows[(program,'12.5px','0')],weight_rows[(program,'12.5px','1')])
    # Case (m): the unrouted streak writes no negative age and, over a dark sky, no trail beyond 3 px; the hull row's marks are
    # the hull's own. The flickering sky's trail and the segment brightness ratios are reported, not gated.
    report['mote_streak']=[dict(re.findall(r'(\w+)=(\S+)',line)) for line in text.splitlines() if line.startswith('MOTE_STREAK ')]
    streak_rows={(row['sky'],row['value']):row for row in report['mote_streak'][:4]}
    assert len(report['mote_streak'])==8 and sorted(streak_rows)==[('dark','0.80'),('dark','2.00'),('flicker','2.00'),('flicker_hull','2.00')],report['mote_streak']
    assert all(streak_rows[k]['negative_px']=='0' for k in (('dark','0.80'),('dark','2.00'),('flicker','2.00'))) and all(streak_rows[k]['trail_px']=='0' for k in (('dark','0.80'),('dark','2.00'))),report['mote_streak']
    # Exit reset rows (docs/architecture/seta-sky-hull-share-decay.md): strict alone leaves the hull share in the trail and no
    # negative age; with the floor every fresh trail pixel is current-only once and the trail is sky-only (B == R) afterwards.
    report['seta_exit']=[dict(re.findall(r'(\w+)=(\S+)',line)) for line in text.splitlines() if line.startswith('SETA_EXIT ')]
    exit_rows={row['mode']:row for row in report['seta_exit'][:14]} # fourteen rows per generation (loose_on's row carries its diff against loose without the floor)
    on_modes=['straight_on','yaw_plus_on','yaw_minus_on','far_straight_on','far_yaw_plus_on','far_yaw_minus_on','far_camera_straight_on','far_camera_yaw_plus_on','far_camera_yaw_minus_on']
    assert len(report['seta_exit'])==28 and sorted(exit_rows)==sorted(on_modes+['loose_on','slow_on','straight_off','yaw_minus_off','yaw_plus_off']),report['seta_exit']
    assert exit_rows['straight_off']['negative_px']=='0' and float(exit_rows['straight_off']['trail_cast_max'])>.05 and exit_rows['slow_on']['negative_px']=='0' and exit_rows['loose_on']['negative_px']=='0',exit_rows
    assert all(exit_rows[m]['trail_cast_max']=='0.000000' for m in ('straight_on','far_straight_on','far_camera_straight_on')),exit_rows
    assert all(r['fresh_current_only']==r['fresh_px'] and int(r['fresh_px'])>=20 and r['negative_not_band']=='0' and r['negative_static']=='0' and r['band_blend_positive']=='0' and r['hull_mark_unexplained']=='0' for r in (exit_rows[m] for m in on_modes)),exit_rows
    assert len(report['camera_pan'])==2 and all(r['w_below_current_only']=='0' and r['w_above_current_only']=='0' and int(r['w_below_px'])>4000 for r in report['camera_pan']),report['camera_pan']
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
    # under the history weight. The fixture asserts: shader = CPU model of the
    # resolve; the 8-phase ripple ratio within 0.15 of the modelled 0.62; flat
    # regions within 1/255; the moving edge's bounds. Every embedded program's
    # instruction slots are recorded. (The filtered current sample of that run,
    # resolve_filter.hlsl, and the line filter were removed 2026-09-23, cleanup
    # batch 6, with their cases.)
    lattice_path=results/'temporal-lattice.txt'
    with lattice_path.open('w') as out,(results/'temporal-pass-wine.log').open('a') as err:
        lattice=subprocess.run(command+['lattice'],stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=600)
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
    # Pass time of the plain, far-stabiliser and thin-region resolves (the line-filter cases went with the option, cleanup batch 6).
    report['pass_timing']=fields('LINE_TIMING ')
    # Far stabiliser (docs/architecture/taa-distant-line-fade.md section 9): the CPU gate, oracle error, near-pixel bit-identity, ripple ratios per band.
    report['far_stabiliser']={'gate':fields('FAR_GATE '),'speed_ramp':fields('FAR_SPEED_RAMP ')+fields('FAR_SPEED_GATE_CUSTOM '),'cases':fields('FAR_STABILISER '),'filter_effect':fields('FAR_FILTER_EFFECT ')}
    assert len(report['far_stabiliser']['cases'])==20 and all(c['near_differs']=='0' for c in report['far_stabiliser']['cases']),report['far_stabiliser']
    # Thin region (docs/architecture/taa-lattice-crawl.md section 13): oracle error, gate = oracle, shard ripple, plain-silhouette bit-identity, motion start.
    report['thin_region']={'cases':fields('THIN_REGION '),'motion_start':fields('THIN_REGION_MOTION_START '),'emissive':fields('THIN_REGION_EMISSIVE '),'emissive_nonfinite':fields('THIN_REGION_EMISSIVE_NONFINITE ')}
    # Emissive vote (docs/architecture/thin-glow-lines.md 8.3 R3): E = 0 bit-identical to the plain resolve, the vote on the
    # routed strip only, and the strip's rest leak down 3.4 x (the IIR prediction for 0.9 -> 0.97 at the jitter fundamental).
    assert len(report['thin_region']['emissive'])==1,report['thin_region']['emissive']
    emissive=report['thin_region']['emissive'][0]
    assert (float(emissive['e0_vs_plain_max_diff']),float(emissive['e0_mask_b_max']),float(emissive['e1_mask_oracle_error']))==(0.0,0.0,0.0),emissive
    assert (float(emissive['e1_strip_b_min']),float(emissive['e1_panel_core_b_max']),float(emissive['e1_unrouted_sentinel_b_max']))==(1.0,0.0,0.0),emissive
    assert 2.5<=float(emissive['delta_ratio'])<=4.5,emissive
    # Non-finite scene: a NaN pixel and one above the resolve's 65000 limit, and their eight neighbours, cast no vote.
    assert len(report['thin_region']['emissive_nonfinite'])==1,report['thin_region']['emissive_nonfinite']
    bad=report['thin_region']['emissive_nonfinite'][0]
    assert (bad['nan_present'],bad['overflow_present'])==('1','1'),bad
    assert (float(bad['control_b']),float(bad['nan_and_panel_core_b_max']),float(bad['overflow_b_max_3x3']),float(bad['mask_oracle_error']))==(1.0,0.0,0.0,0.0),bad
    # Camera-relative gate (section 32.1; A', its only path since 2026-09-24): the pan scene's tests target / ripple, the stale-history
    # bound, forward and general flight against the per-pixel oracle, non-finite motion, glass, the pass time (the screen gate's pan
    # oracle row beside it). The static-camera bit-identity with the screen gate was the dilated chain's and went with it.
    report['thin_region_camera']={'pan_oracle':fields('THIN_REGION_PAN '),'pan':fields('THIN_REGION_CAMERA '),'forward':fields('THIN_REGION_CAMERA_FORWARD '),'flight':fields('THIN_REGION_CAMERA_FLIGHT '),'stale':fields('THIN_REGION_STALE '),'box_domain':fields('THIN_REGION_BOX_DOMAIN '),'bad_motion':fields('THIN_REGION_BAD_MOTION '),'glass_mover':fields('THIN_REGION_GLASS_MOVER '),'timing':fields('LINE_TIMING_CAMERA '),'timing_lane':fields('LINE_TIMING_CAMERA_LANE ')}
    assert [(m['kind'],m['glass']) for m in report['thin_region_camera']['bad_motion']]==[('overflow_1e30','0'),('nan','0'),('overflow_1e30','1'),('nan','1')] and all(m['camera_gate_max_covered']=='0.0000' and m['camera_screen_channel_max_covered']=='0.0000' and m['camera_output_finite']=='1' and int(m['covered_px'])>0 for m in report['thin_region_camera']['bad_motion'] if m['asserted']=='1') and len(report['thin_region_camera']['glass_mover'])==1 and len(report['thin_region_camera']['box_domain'])==1 and len(report['thin_region_camera']['forward'])==1 and len(report['thin_region_camera']['flight'])==15 and [f['mover_max'] for f in report['thin_region_camera']['flight'] if f['mover']=='1']==['0.0000','0.0000'] and all(int(f['mover_px'])>0 for f in report['thin_region_camera']['flight'] if f['mover']=='1') and len(report['thin_region_camera']['pan_oracle'])==1 and len(report['thin_region_camera']['timing_lane'])==1,report['thin_region_camera']
    # The sentinel stabiliser's rows (SENTINEL_STABILISER, LINE_TIMING_SENTINEL) went with the stabiliser (docs/architecture/
    # taa-mask-fold.md section 7, 2026-09-25).
    assert not fields('SENTINEL_STABILISER ') and not fields('LINE_TIMING_SENTINEL '),'retired sentinel rows printed'
    # 18 THIN_REGION rows: six configurations x three drifts (the camera gate's are THIN_REGION_HOLD's since 2026-09-24).
    assert 'FAR_BASE numerical=295 state_restorations=6' in lattice_text and len(report['thin_region']['cases'])==18 and all(c['square_differs']=='0' for c in report['thin_region']['cases']),report['thin_region']
    assert len(report['pass_timing'])==1,report['pass_timing']
    # Cleanup batch 6 (2026-09-23): 10 / 0 are the run-139 history-weight cases (LATTICE_BASE; the filtered-sample rows and
    # their refusals, 18 and 9, went with --taa-current-filter); the flicker cases add 180 numerical and 4 state checks
    # (FLICKER_BASE; the filtered bit-identity pair went); the line block is timing only now (LINE_BASE adds 0 and 0; the
    # 45 and 2 line-filter checks went); the far-stabiliser cases add 109 and 2 (FAR_BASE; the line-filter row and the
    # line mask-failure check, 18, went); the thin-region cases 180 and 6 (7 and 0 of them the emissive vote,
    # thin-glow-lines.md 8.3 R3; the camera gate's, taa-lattice-crawl.md sections 32.1, 32.3, 32.4 and 32.5, and the sentinel
    # stabiliser's, temporal-integration.md, on A'. 2026-09-24, dilated chain removed: 21 fewer, the camera config's three
    # THIN_REGION rows (13, THIN_REGION_HOLD's), the three static identities (3), the camera pan-oracle row (3, THIN_REGION_HOLD_PAN's)
    # and the sentinel mover row (2); the glass, forward, flight and non-finite-motion checks rewritten per pixel on the tests
    # target (same count)). The depth-copy fold (taa-high-resolution.md S1) adds 8
    # and 77: four configurations (every twin byte-identical), the one-ulp negative control, the refused RT1 bind, the failed
    # fold draw and the device Reset; every run restores the hostile state (63 + 6 + 8 state checks).
    # The mask fold (taa-mask-fold.md): the camera gate folds the depth history into its resolve (RT2, reason resolve_mrt) from
    # every current-depth format, R32F included; the screen-gate chain keeps S1 (lane_mrt). Rows: camera 4 (lane, lane with the
    # screen chain's folding program refused, G32R32F against the R32F twin, and the one-ulp negative control), thin_screen and
    # far_only 3 each; faults: the screen chain's RT1 bind and fold draw, the camera gate's RT2 bind and resolve draw.
    report['depth_fold']=fields('DEPTH_FOLD ');report['depth_fold_fault']=fields('DEPTH_FOLD_FAULT ');report['depth_fold_reset']=fields('DEPTH_FOLD_RESET ')
    fold_rows={(r['case'],r['run']):r for r in report['depth_fold']}
    assert len(report['depth_fold'])==10 and len(fold_rows)==10,report['depth_fold']
    assert all(r['history_frames']=='2' and r['folded']==('0' if r['run']=='lane_copy' and r['case']!='camera' else '3') and
               r['reason']==('resolve_mrt' if r['case']=='camera' else 'program' if r['run']=='lane_copy' else 'lane_mrt') and
               r['against_reason']==('resolve_mrt' if r['case']=='camera' else 'r32f_depth') for r in report['depth_fold']),report['depth_fold']
    assert all(all(r[k]=='0' for k in ('color_bytes_differ','depth_bytes_differ','age_bytes_differ','mask_bytes_differ')) for r in report['depth_fold'] if r['run']!='negative_control'),report['depth_fold']
    assert int(fold_rows[('camera','negative_control')]['depth_bytes_differ'])>0,fold_rows[('camera','negative_control')]
    assert [r['kind'] for r in report['depth_fold_fault']]==['rt1_bind','fold_draw','rt2_bind','resolve_draw'] and all(r['hr']==r['operation']=='80004005' and r['reached']=='1' and r['folded']=='0' and r['target_unbound_after_bind']=='1' and r['recovered_hr']=='00000000' and r['recovered_folded']=='1' and r['recovered_history']=='0' for r in report['depth_fold_fault']),report['depth_fold_fault']
    assert len(report['depth_fold_reset'])==1 and report['depth_fold_reset'][0]['bytes_differ']=='0,0,0,0' and report['depth_fold_reset'][0]['after_folded']=='2',report['depth_fold_reset']
    # The mask fold (2026-09-25, taa-mask-fold.md; docs/verification/temporal-resolve.md "Mask fold"): 561 / 90 in all. The thin
    # region and fold block ends at 452 / 72 (487 / 89 before: the seven sentinel-stabiliser rows and their state checks retired,
    # the camera-gate checks rewritten for no mask and RT2, the depth-fold rows 15 -> 10 and faults 2 -> 4); A' adds 52 / 3 (the
    # identity rows 4 -> 2, the state row with the 2-RT refusal, RT2 and the stream offsets, FOLD_FALLBACK 4 rows x 3, the
    # sentinel rows retired); S4 adds 23 / 1 (the sentinel / emitter scenes and the timing retired); the thin source 13 / 14.
    # A' (taa-plan-lifted-slot-cap.md step 1): 46 numerical and 2 state checks on top of S3's 508 / 89 (the reference's identity
    # with the removed camera program and the four identity configurations, the state rows (the refused hold program, refused
    # box targets turning the thin region off, history and mask targets) and their two restorations, the stop-after-pan and box-open rows, 13 thin-region, 2 motion-start, 4 pan, 3 stale,
    # 2 box-domain, 6 sentinel and 5 fade-owner checks).
    # S3 (taa-high-resolution.md): 21 numerical checks on top of the fold's 487 / 89: the filter probe (8: every texel centre
    # exact at 32 / 1280 / 5120 and the fraction ramp, FP16 and R32F), the tap setting (1), four scenes (two each: the program
    # drawn and the rest / drift verdict; the far program on the screen gate: the camera gate has no 16-tap form), two
    # fallbacks, no camera-gate program without the filter query, and the Reset.
    # 2026-09-25: the 16-tap point programs were removed (--taa-history-taps went): the tap setting, the four 5 / 16 scenes and
    # the two 16-tap fallbacks went with them; a device without the filter query now refuses initialize (no fallback set).
    # Measured totals after it (2026-09-25): FAR_BASE 299 -> 295 (the far scenes' 16-tap reference identities), the history-taps
    # block 21 -> 10 numerical checks, RESULT 598 -> 583 / 90; the standalone filter query (HISTORY_FILTER_QUERY, MotionOutput's
    # attach-time decision) adds 1: 584 / 90.
    report['history_taps']={'filter_probe':fields('FILTER_PROBE '),'reset':fields('HISTORY_TAPS_RESET '),'no_filter':fields('HISTORY_TAPS_NO_FILTER '),'query':fields('HISTORY_FILTER_QUERY ')}
    taps=report['history_taps']
    assert [(r['bilinear'],r['reason'],r['initialize'],r['references']) for r in taps['no_filter']]==[('0','adapter_query','8876086a','0')],taps['no_filter']
    assert [(r['refused_reason'],r['passed'],r['passed_reason'],r['references']) for r in taps['query']]==[('adapter_query','00000000','ok','0')],taps['query']
    assert len(taps['filter_probe'])==8 and all(r['mismatched']=='0' for r in taps['filter_probe'] if r['mode']=='centre'),taps['filter_probe']
    assert len(taps['reset'])==1 and taps['reset'][0]['identical_to_fresh']=='1',taps
    # A' (taa-plan-lifted-slot-cap.md step 1; the camera gate's only path since 2026-09-24): the identity of the hold program with
    # holds reading 0 against the removed camera program (compiled from resolve.hlsl, word for word the removed embedded program) on
    # the 1x1 composition (colour bit for bit, age = count + encoded holds; four k / S configurations), the state rows, and the
    # thin-region rows with the hold against the CPU oracle extended by the holds.
    report['region_hold']={'reference':fields('REGION_HOLD_IDENTITY_REFERENCE '),'identity':fields('REGION_HOLD_IDENTITY '),'state':fields('REGION_HOLD_STATE '),'thin':fields('THIN_REGION_HOLD drift='),
                           'motion_start':fields('THIN_REGION_HOLD_MOTION_START '),'pan':fields('THIN_REGION_HOLD_PAN '),'stale':fields('THIN_REGION_HOLD_STALE '),
                           'box_domain':fields('THIN_REGION_HOLD_BOX_DOMAIN '),
                           'pan_stop':fields('THIN_REGION_HOLD_PAN_STOP '),'box_open':fields('THIN_REGION_HOLD_BOX_OPEN '),
                           'fade_owner':fields('THIN_REGION_HOLD_FADE_OWNER '),'fold_fallback':fields('FOLD_FALLBACK ')}
    hold=report['region_hold']
    # Fade owner (fade-rt2-ownership.md sections 4 and 7): a far routed square switching sentinel -> valid once (5 numerical checks).
    owner=hold['fade_owner']
    assert len(owner)==1 and owner[0]['pre_switch_differs']=='0' and int(owner[0]['max_openings_per_px'])<=1 and int(owner[0]['extra_open_frames'])<=int(owner[0]['hold_frames'])+1 \
        and owner[0]['late_hold_differs']=='0' and float(owner[0]['age_oracle_error'])==0,owner
    assert len(hold['reference'])==1 and hold['reference'][0]['identical']=='1' and hold['reference'][0]['words']=='2196',hold['reference']
    # The mask fold: the folded hold program against the removed camera program on the composition of its own tests (k = 0, 0.5):
    # colour, age and RT2 depth identical, the flag and the camera term exercised; the state row with RT2, the stream offsets from
    # a non-hostile start and the 2-RT refusal; FOLD_FALLBACK: the in-place 7x7 against the oracle on its pixels (full and half).
    assert len(hold['identity'])==2 and all(r['colour_differs']==r['age_differs']==r['count_differs']==r['depth_differs']=='0' and int(r['blended'])>256 and int(r['flagged'])>0 and int(r['camera_adds'])>0 for r in hold['identity']),hold['identity']
    assert len(hold['state'])==1 and all(hold['state'][0][k]=='1' for k in ('refused_path','two_targets_refused','rt2_restored','streams_restored','screen_restarts','screen_continues','camera_keeps','taps5_camera','box_refused_region_off','rearmed')),hold['state']
    assert [hold['state'][0][k] for k in ('masks_camera','masks_screen','masks_on','masks_at_reset','masks_after_reset','masks_box_refused')]==['0','2','0','0','0','0'],hold['state']
    assert len(hold['thin'])==3 and all(r['square_differs']=='0' and float(r['age_oracle_error'])==0 for r in hold['thin']) and len(hold['motion_start'])==1 and len(hold['pan'])==1 and len(hold['stale'])==1 and len(hold['box_domain'])==1 and len(hold['pan_stop'])==1 and len(hold['box_open'])==1,hold
    assert [(r['scene'],r['box']) for r in hold['fold_fallback']]==[(s,b) for s in ('pan_arm_bars','gap_lattice_7.5px') for b in ('full','half')] and all(int(r['fallback_px_frames'])>0 and float(r['error_in_place_7x7'])<=float(r['bound']) for r in hold['fold_fallback']),hold['fold_fallback']
    assert lattice.returncode==0 and 'LATTICE_BASE numerical=10 state_restorations=0' in lattice_text and 'FLICKER_BASE numerical=190 state_restorations=4' in lattice_text and 'LINE_BASE numerical=190 state_restorations=4' in lattice_text and 'DEPTH_FOLD_BASE numerical=448 state_restorations=72' in lattice_text and 'HISTORY_TAPS_BASE numerical=459 state_restorations=72' in lattice_text and 'REGION_HOLD_BASE numerical=511 state_restorations=75' in lattice_text and 'BOX_HALF_BASE numerical=534 state_restorations=76' in lattice_text and 'THIN_SOURCE_BASE numerical=547 state_restorations=90' in lattice_text and 'FOLD_TIMING_BASE numerical=547 state_restorations=90' in lattice_text and 'FAR_CAMERA_PAN_BASE numerical=567 state_restorations=90' in lattice_text and 'RESULT PASS numerical=584 state_restorations=90 lattice=1' in lattice_text and 'FAIL' not in lattice_text,lattice_text[-1500:]
    # The 2,048-slot ceiling per TAA program (docs/architecture/taa-plan-lifted-slot-cap.md section 2; AGENTS.md "Shader slot
    # budget": 512 is the spec minimum, not a limit). device_limit stays a record.
    assert len(report['flicker']['drift'])==64 and len(report['flicker']['near_depth'])==8 and all(float(v['instruction_slots'])<=2048 and v['within_ceiling_2048']==1 for k,v in report['lattice']['budget'].items()),report['lattice']['budget']
    assert {'embedded_far_camera_hold','embedded_thin_box_hold','embedded_thin_box_rows_half','embedded_thin_box_columns_half','fixture_fold_tests_out'}<=set(report['lattice']['budget']) and not {'embedded_line_mask_camera','embedded_line_mask_camera_depth','embedded_line_mask_camera_depth_thin','embedded_thin_box_rows_hold','embedded_thin_box_columns_hold'}&set(report['lattice']['budget']),report['lattice']['budget']
    # S4 (taa-high-resolution.md S4; X3M_TAA_BOX_RESOLUTION=half, opt-in): 40 numerical checks and one state restoration on top of
    # A''s 554 / 91 (REGION_HOLD_BASE): the state row (3), per-pixel containment of the full-resolution box in twelve scenes (12; the two static arm
    # scenes open no box at either resolution and are then identical bit for bit),
    # the half-resolution runs against the CPU oracle with the block box (9 scenes x 2; not the colour-only bar over geometry, which the oracle does not model at either resolution), the rest ripple, the stale patch (2),
    # the stop after a pan, the sentinel facets (2) and the emitter trail. The box stage timing rows are the fixture's wall clock at
    # 1280x768 and 5120x1440, reported, not gated.
    box={name:fields('BOX_HALF_'+name.upper()+' ') for name in ('state','containment','oracle','ripple','stale','pan_stop')}
    report['box_half']=box
    assert len(box['state'])==1 and all(v=='1' for v in box['state'][0].values()),box['state']
    # The mask fold: the sentinel scenes went with the stabiliser; at rest the fold's gate opens the box on the held region, where
    # the camera term adds nothing (b = a), so the arm scenes compare pixels and stay identical at both resolutions; half-only
    # pixels are held to the pixel's 7x7 (the resolve's in-place box at full resolution).
    scenes=['arm_rest','arm_drift_0.30','pan_0.50','pan_stale_patch','pan_box_domain_k0.5_nonfinite','pan_stop']
    assert [r['scene'] for r in box['containment']]==scenes and all(r['violations']=='0' and r['half_only_violations']=='0' and int(r['compared_px'])>0 and (r['scene'][:4]!='arm_' or r['output_identical']=='1') for r in box['containment']),box['containment']
    assert len(box['oracle'])==5 and all(float(r['oracle_error'])<=.0006/(1-.97) and float(r['age_oracle_error'])==0 for r in box['oracle']),box['oracle']
    assert len(box['ripple'])==1 and box['ripple'][0]['square_differs']=='0' and len(box['stale'])==1 and len(box['pan_stop'])==1,box
    assert not fields('BOX_HALF_SENTINEL ') and not fields('BOX_HALF_EMITTER ') and not fields('BOX_HALF_TIMING '),'retired box rows printed'
    # Thin-region source (X3M_TAA_THIN_REGION_SOURCE; taa-thin-geometry-alternatives.md section 3.2; temporal_thin_source_inc.h): 16 numerical
    # checks and 15 state restorations on top of S4's 594 / 92 (BOX_HALF_BASE). Per gate (camera = A', screen) and source the tests-target
    # flag and the kept history of five classes (S sky, P panel, U unvoted struts over sky, W voted struts over sky, V voted bars over a panel
    # at almost the same depth): both flags U W V, screen U W, vote W V; flagged keeps 0.97, unflagged 0. Identities with the plain run (no
    # vote): screen, and vote without its twin (no input; twins refused at creation). A source outside the enum refuses the run; the vote-only
    # tests target survives a Reset. The two timing rows (5120x1440, the tests draw per source) are the fixture's clock, reported only.
    thin_rows=fields('THIN_SOURCE gate=')
    report['thin_region_source']={'rows':thin_rows,'identity':fields('THIN_SOURCE_IDENTITY '),'refused':fields('THIN_SOURCE_REFUSED '),'invalid':fields('THIN_SOURCE_INVALID '),'reset':fields('THIN_SOURCE_RESET ')}
    source=report['thin_region_source']
    flags={'both':'uwv','screen':'uw','vote':'wv'}
    # The mask fold: the camera gate reads the vote in its resolve (twin=1 whenever the vote is cast) and refuses the screen source;
    # its flags are read from the age target's region hold (255 = flagged this frame).
    assert [(r['gate'],r['source'],r['drawn']) for r in thin_rows]==[('camera','both','both'),('camera','vote','vote')]+[('screen',v,v) for v in ('both','screen','vote')],thin_rows
    for r in thin_rows:
        for c in 'spuwv':
            low,high=(int(v) for v in r[c+'_b'].split('..'));keep=[float(v) for v in r[c+'_keep'].split('..')]
            assert int(r[c+'_px'])>0 and ((low>=254 and all(abs(k-.97)<=.002 for k in keep)) if c in flags[r['source']] else (high<=1 and all(abs(k)<=.002 for k in keep))),(r,c)
    assert [(r['gate'],r['kind'],r['drawn']) for r in source['identity']]==[('camera','vote_no_input_vs_plain','both')]+[('screen',k,v) for k,v in (('screen_vs_plain','screen'),('vote_no_input_vs_plain','both'),('vote_twin_refused_vs_plain','both'))] \
        and all(r[k]=='0' for r in source['identity'] for k in ('color_bytes_differ','depth_bytes_differ','age_bytes_differ','mask_bytes_differ')),source['identity']
    assert [(r['gate'],r['source'],r['hr']) for r in source['refused']]==[('camera','screen','80070057')],source['refused']
    assert [(r['hr'],r['published']) for r in source['invalid']]==[('80070057','0')] and [(r['drawn_before'],r['drawn_after'],r['age_bytes_differ']) for r in source['reset']]==[('vote','vote','0')],source
    # FOLD_TIMING (taa-mask-fold.md sections 3 and 8): the camera gate's sub-passes at 5120 x 1440 on the station content at rest
    # and under a pan, the launcher default (vote), both, and the thin vote off. No mask draw; reported, compared in the ledger
    # with the pre-fold baseline built from the same include (X3M_FOLD_BASELINE).
    report['fold_timing']=fields('FOLD_TIMING build=')
    assert [(r['motion'],r['config']) for r in report['fold_timing']]==[(m,c) for m in ('rest','pan') for c in ('vote','both','thin_vote_off')] and all(r['build']=='fold' and r['width']=='5120' and r['mask_ms']=='0.0000' and float(r['resolve_ms'])>0 and float(r['box_ms'])>0 for r in report['fold_timing']),report['fold_timing']
    # Far weight on the camera gate (docs/architecture/taa-mask-fold.md section 4.2 addendum; run327 sparkles): a far world-static
    # line strip (1 px and 0.4 px) under pure yaws of 10 / 10.5 / 8.25 px/frame, a far mover and a co-moving object, on the
    # camera-gate program (far 0.985 on X3M_TAA_FAR_GATE camera and screen, and far off) and the far program alone. The camera
    # gate's yaws keep the rest level of one-frame sparkles; the mover drops to the base weight bit for bit; the screen gate is
    # the far program bit for bit. Both behaviours are pinned (the 0.4 px line's sparkle margin in codes, the 1 px line's peak).
    report['far_camera_pan']=fields('FAR_CAMERA_PAN ')
    pan={(r['program'],r['width'],r['row']):r for r in report['far_camera_pan']}
    assert len(report['far_camera_pan'])==48 and len(pan)==48 and all(r['region_px']=='0' for r in report['far_camera_pan'] if r['program']!='far_screen'),report['far_camera_pan']
    for width in ('1.0','0.4'):
        rest=pan[('far_camera',width,'rest')]
        # The sparkle margin (spike_codes), not the count (432 at rest and at yaw10 on both gates): the integer yaw equals rest
        # within 0.1 code, the fractional yaws stay within rest + 1; the screen gate's integer yaw is the sparkling one.
        assert abs(float(pan[('far_camera',width,'yaw10')]['spike_codes'])-float(rest['spike_codes']))<=.1 and all(float(pan[('far_camera',width,row)]['spike_codes'])<=float(rest['spike_codes'])+1 for row in ('yaw10.5','yaw8.25')),(width,pan)
        screen=pan[('far_camera_screen_gate',width,'yaw10')]
        assert float(screen['spike_codes'])>40 if width=='0.4' else float(screen['spike_codes'])>=float(pan[('far_camera_screen_gate',width,'rest')]['spike_codes'])+.1,(width,screen)
        assert pan[('far_camera',width,'yaw10')]['rest_content_diff']=='0.000000' and pan[('far_camera',width,'mover10')]['off_diff']=='0.000000' and pan[('far_camera',width,'comove10')]['rest_content_diff']=='0.000000',(width,pan)
        assert all(r['screen_program_diff']=='0.000000' for r in report['far_camera_pan'] if r['program']=='far_camera_screen_gate' and r['width']==width),(width,pan)
    # program: (0.4 px margin at rest, yaw10, yaw10.5, yaw8.25), (1 px peak at yaw10.5, yaw8.25); the first run of each gate.
    pins={'far_camera':((14.202,14.202,6.427,14.202),(.603821,.661987)),'far_camera_screen_gate':((14.202,57.874,15.414,57.874),(1.004639,1.074005))}
    for program,(margins,peaks) in pins.items():
        assert all(abs(float(pan[(program,'0.4',row)]['spike_codes'])-v)<=.01 for row,v in zip(('rest','yaw10','yaw10.5','yaw8.25'),margins)),(program,pan)
        assert all(abs(float(pan[(program,'1.0',row)]['peak'])-v)<=1e-4 for row,v in zip(('yaw10.5','yaw8.25'),peaks)),(program,pan)
    # Far clip (docs/architecture/taa-mask-fold.md section 4.2 addendum "far clip"; run327 / run332 rest sparkles): slanted far
    # sub-pixel lines (1 px, 0.4 px) under the 8-phase jitter at rest and under yaws of 10 / 10.5 / 8.25 px/frame on the camera-gate
    # program with the far clip (7x7, the default) and without it (3x3). 7x7: every row's one-frame margin and the rest delta at
    # most 6 codes, the rest dimming within 3 codes; 3x3: the 0.4 px line sparkles at rest (margin and delta above 6 codes).
    report['far_jitter_line']=fields('FAR_JITTER_LINE ')
    jit={(r['clip'],r['gate'],r['width'],r['row']):r for r in report['far_jitter_line']}
    assert len(report['far_jitter_line'])==22 and len(jit)==22 and all(r['region_px']=='0' for r in report['far_jitter_line']),report['far_jitter_line']
    for width in ('1.0','0.4'):
        assert all(float(jit[('7x7','camera',width,row)]['spike_codes'])<=6 for row in ('rest','yaw10','yaw10.5','yaw8.25')),(width,jit)
        rest=jit[('7x7','camera',width,'rest')]
        assert float(rest['rest_delta_codes'])<=6 and max(-float(rest['dim_min']),float(rest['dim_max']))<=3 and int(rest['line_px'])>0,(width,rest)
    assert float(jit[('3x3','camera','0.4','rest')]['spike_codes'])>6 and float(jit[('3x3','camera','0.4','rest')]['rest_delta_codes'])>6,jit
    # The witness for the camera default of the far weight's gate: screen gate + 7x7, 0.4 px line, yaw 10.5 above the margin (the
    # other five screen rows are informational).
    assert float(jit[('7x7','screen','0.4','yaw10.5')]['spike_codes'])>6,jit
    ripple=report['lattice']['ripple']
    assert len(ripple)==4 and report['lattice']['budget']['plain']['instruction_slots']<=2048,report['lattice']
    assert hashes()==report['sources_before_build'],'Source changed during the lattice cases'
    report['passed']=True
finally:
    (results/'temporal-pass-summary.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
