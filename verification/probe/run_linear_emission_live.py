#!/usr/bin/env python3
"""Consume an explicit prebuilt capture seam; never build or launch the game."""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import struct
import statistics
import subprocess
import tempfile
import time
import sys

import bottle
from game_guard import game_running
from run_linear_material_live import fields,sha

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/analysis'))
import agx_reference
PROGRAMS=('vs_53a0a641107ed76c.bin','ps_8759c7838bbc86c2.bin','vs_d5e1c75351ed3f04.bin','ps_8360f422de08b5bd.bin')
# Exact reviewed identities, in production table order; no shader-byte payloads.
VERTICES=('d5e1c75351ed3f04','32e75459998d0388','089091aab2d5eb13',
          '5b7a3ccd9e7df00a','6435a84d8ac5908e','89193868c61c3846',
          'a520be365951c9dc','cfb2c31707d545bc')
PIXELS=('8360f422de08b5bd','9975b706e5a1c999','ff2473e73a6bdfa1',
        '8559522220507d5e','875e780adb131b16','39f3b4d5b6a5aaed',
        '47e15e20d63b0e93','846c5c1a549f9491','c6dacb8f74b65c97',
        'f0c91793a75e1203')
PAIR_INDICES=((0,0),(1,1),(1,2),(2,3),(2,4),(3,1),(3,2),(4,5),(4,6),
              (4,7),(4,8),(5,0),(5,9),(6,3),(6,4),(7,5),(7,6),(7,7),(7,8),(0,9))
PAIRS=tuple((VERTICES[v],PIXELS[p]) for v,p in PAIR_INDICES)
CORPUS_PROGRAMS=tuple(f'{stage}_{value}.bin' for stage,values in
                      (('vs',VERTICES),('ps',PIXELS)) for value in values)
LEGACY_FRAMES=12
FRAMES=LEGACY_FRAMES+2*len(PAIRS)
REJECTED_FRAME=8
TAA_FRAMES=tuple(i for i in range(FRAMES) if i!=REJECTED_FRAME)
DRAWS=[0,1,2,0,1,1,1,1,1,1,1,0]+[1,0]*len(PAIRS)
FAULTS={5:3,6:6,7:101,9:7}


def validate(output,trace,enabled):
    lines=output.splitlines();traces=trace.splitlines()
    summary=[fields(x) for x in lines if x.startswith('RESULT PASS ')]
    assert len(summary)==1 and not any(x.startswith('RESULT FAIL') for x in lines),'native fixture completion'
    summary=summary[0]
    assert int(summary['frames'])==FRAMES and int(summary['taa_reference_frames'])==len(TAA_FRAMES) and int(summary['taa_skipped_frames'])==1
    assert int(summary['taa_frames'])==FRAMES-1 and int(summary['taa_history_frames'])==FRAMES-4
    checks=[fields(x) for x in lines if x.startswith('EMISSION_CHECKS ')]
    assert len(checks)==1 and int(checks[0]['frames'])==FRAMES and int(checks[0]['submissions'])==sum(DRAWS)
    assert int(checks[0]['reset'])==1 and int(checks[0]['numeric'])>0 and int(checks[0]['ordered_overwrites'])>0
    ratio=float(checks[0]['max_fraction']);assert math.isfinite(ratio) and 0<=ratio<=1
    rows=[fields(x) for x in lines if x.startswith('EMISSION_LIVE ')]
    assert [int(x['frame']) for x in rows]==list(range(FRAMES)),'all integration frames'
    assert [int(x['draws']) for x in rows]==DRAWS
    for i,row in enumerate(rows):
        assert int(row['enabled'])==int(enabled)
        assert int(row['fault'])==FAULTS.get(i,0)
        for key in ('prepared','linear','native','incomplete','exchanged','original_calls'):
            assert 0<=int(row[key])<=DRAWS[i],(i,key)
        if enabled:
            prepared=DRAWS[i] if i!=5 else 0
            assert int(row['prepared'])==prepared,(i,'prepared bracket count')
            assert int(row['exchanged'])==prepared,(i,'real owner exchange acknowledged')
            assert int(row['incomplete'])==int(i==8),(i,'failed source incomplete')
            assert int(row['linear'])==(DRAWS[i] if i not in (5,6,7,8,9) else 0),(i,'linear publication')
            assert int(row['native'])==int(i in (6,7,9)),(i,'native B fallback')
            assert int(row['mask_valid'])==int(i not in (8,9)),(i,'supplemental availability')
        else:
            assert all(int(row[k])==0 for k in ('prepared','linear','native','incomplete','exchanged','mask_valid'))
        # Requested-on counts the nonrouted flat background at pair refusal;
        # frame5 also refuses preparation, frame8 also refuses ordinary B.
        assert int(row['refused'])==(1+int(i in (5,8)) if enabled else 0)
        assert int(row['suppressed'])==0
        assert int(row['original_calls'])==DRAWS[i],(i,'actual original DIP invoked once, independently of prepared route')
        if enabled and i==8:assert int(row['source_hr'],16)&0x80000000
    corpus=[fields(x) for x in lines if x.startswith('EMISSION_CORPUS ')]
    assert [int(x['frame']) for x in corpus]==list(range(LEGACY_FRAMES,FRAMES)), 'complete accepted/disappearing pair corpus'
    for offset,row in enumerate(corpus):
        pair=offset//2;v,p=PAIR_INDICES[pair]
        expected=dict(frame=str(LEGACY_FRAMES+offset),pair=str(pair),vs=VERTICES[v],ps=PIXELS[p],
                      instance=str(int(3<=v<=6)),affine=str(int(p not in (2,4,5,6))),
                      fade=str(int(p not in (3,4))),fog=str(int(p not in (3,4))),
                      disappeared=str(offset%2))
        assert row==expected,(pair,'exact native pair/layout and disappeared schedule')
    rejected=[fields(x) for x in lines if x.startswith('EMISSION_REJECTED ')]
    assert len(rejected)==1
    rejected_hr=int(rejected[0].pop('native_hr'),16)
    assert rejected_hr&0x80000000 and (not enabled or int(rows[8]['source_hr'],16)==rejected_hr),'exact original failure HRESULT retained'
    assert rejected[0]==dict(frame='8',source_failed='1',original_b='1',b_routed='0',b_jittered='0',taa='0',history_seeded='0',copy_exact='1'),'explicit failed-frame native contract'
    assert output.count('RESET PASS')==1
    releases=[fields(x) for x in traces if x.startswith('motion_output_release ')]
    assert len(releases)==1 and int(releases[0]['released'])==1,'route references retired'
    motion={int(fields(x)['frame']):fields(x) for x in traces if x.startswith('motion_output_frame ')}
    assert set(motion)==set(range(FRAMES))
    for i,row in motion.items():
        draws=1 if i==REJECTED_FRAME else 2
        assert int(row['draws'])==3+DRAWS[i] and int(row['jittered'])==draws,(i,'actual background/A/source/B submissions and jitter')
        assert int(row['routed'])==int(row['depth_routed'])==draws,(i,'ordinary route after native failure')
        assert int(row['mip_bias_sets'])==int(row['mip_bias_restores'])==int(row['mip_bias_draws'])==draws,(i,'exact mip bias transitions')
        assert int(row['mip_bias_stages'],16)==1 and int(row['mip_bias_failures'])==int(row['mip_bias_biased_now'],16)==0,(i,'no mip bias failure/leak')
        resolved=i in TAA_FRAMES
        assert int(row['taa_attempted'])==int(row['taa_resolved'])==int(resolved)
        assert int(row['taa_skip'])==(0 if resolved else 2)
        assert row['scene_end_source']==('stretchrect' if resolved else 'none')
        assert int(row['taa_history'])==int(i not in (0,8,9,10))
        if i in (8,9):
            assert int(row['matched'])==1 and int(row['jittered'])==draws
            assert int(row['cut'])==int(i==9) and float(row['cut_missing'])==(0 if i==8 else .5)
    hdr={int(fields(x)['frame']):fields(x) for x in traces if x.startswith('hdr_frame ')}
    assert set(hdr)==set(range(FRAMES))
    assert all(x['end']==('present' if i==REJECTED_FRAME else 'bloom_copy') for i,x in hdr.items())
    assert all(int(x['writebacks'])==int(x['tonemapped'])==1 and x['writeback_source']=='shader' and x['tonemap']=='agx' and int(x['fallback'])==int(x['unwind'])==0 for x in hdr.values()),'one clean AgX writeback per frame'
    if enabled:
        emission={int(fields(x)['frame']):fields(x) for x in traces if x.startswith('linear_emission_frame ')}
        assert set(emission)==set(range(FRAMES))
        assert all(int(x['exports'])==int(x['quarantine'])==int(x['state_lost'])==0 for x in emission.values()),'no permanent quarantine or lost state'
    temporal={int(fields(x)['frame']):fields(x) for x in traces if x.startswith('motion_output_taa_readback ')}
    assert set(temporal)==set(TAA_FRAMES)
    assert all(x['result']=='00000000' for x in temporal.values())
    return dict(frames=FRAMES,checks=int(summary['checks']),restorations=int(summary['restorations']),submissions=sum(DRAWS),
                max_numeric_fraction=ratio,exact_temporal_frames=len(TAA_FRAMES),expected_temporal_frame_indices=TAA_FRAMES,rejected_frame=REJECTED_FRAME,rejected_source_hresult=f'{rejected_hr:08x}',reset=1,device_final_release=True,
                held_references=int(releases[0]['held']),frames_detail=rows,corpus_pairs=len(PAIRS),original_vs=len(VERTICES),original_ps=len(PIXELS),corpus_detail=corpus)


def validate_pixels(work,enabled=None):
    capture=work/'x3-modern-captures';hashes=[];masks=[]
    for frame in range(FRAMES):
        actual_path=capture/f'taa_1_{frame}.rgba16f';reference_path=work/f'reference_taa_{frame}.rgba16f'
        if frame in TAA_FRAMES:
            actual=actual_path.read_bytes();reference=reference_path.read_bytes()
            assert len(actual)==64*64*8 and actual==reference,(frame,'supplemental route differs from independent temporal reference')
            hashes.append(sha(actual_path))
        else:
            assert not actual_path.exists() and not reference_path.exists(),'failed frame must not publish or seed TAA'
            hashes.append(None)
        mask=(work/f'emission_mask_{frame}.rgba32f').read_bytes();assert len(mask)==64*64*16
        values=struct.unpack('<16384f',mask);assert all(math.isfinite(v) and v>=0 for i,v in enumerate(values) if i%4<3)
        masks.append(sum(v>0 for v in values[::4]))
        if enabled is not None and frame>=LEGACY_FRAMES:
            present=enabled and frame%2==0
            assert masks[-1]==(1024 if present else 0),(frame,'actual pair coverage or disappearing mask')
            for y in range(64):
                for x in range(64):
                    expected=float(present and 8<=x<40 and 16<=y<48)
                    assert all(values[(y*64+x)*4+k]==expected for k in range(3)),(frame,x,y,'exact supplemental footprint')
    raw=(work/'emission_color_8.rgba32f').read_bytes();display=(work/'presented_8.bgra8').read_bytes()
    assert len(raw)==64*64*16 and len(display)==64*64*4
    source=struct.unpack('<16384f',raw);maximum=0.
    for i in range(64*64):
        rgba=source[4*i:4*i+4];assert all(math.isfinite(x) for x in rgba)
        rgb=agx_reference.tonemap_engine(rgba[:3],exposure=1,decode_mode='gamma2.2')
        expected=(*reversed(rgb),min(max(rgba[3],0),1))
        for channel,wanted in enumerate(expected):
            error=abs(display[4*i+channel]-255*wanted);maximum=max(maximum,error)
            assert error<=1.,(8,i,channel,'unresolved native AgX display',display[4*i+channel],255*wanted)
    return dict(temporal_sha256=hashes,covered_pixels=masks,rejected_display_max_code_error=maximum)


def validate_timing(output,enabled):
    rows=[fields(line) for line in output.splitlines() if line.startswith('EMISSION_TIMING ')]
    assert [int(x['sample']) for x in rows]==list(range(8))
    assert all(int(x['enabled'])==int(enabled) and int(x['width'])==1920 and int(x['height'])==1080 for x in rows)
    values=[float(x['completed_ms']) for x in rows]
    assert all(math.isfinite(x) and x>=0 for x in values)
    assert 'EMISSION_BENCH frames=12 samples=8 warmups=4 submissions=12 coverage=0.25' in output
    assert 'RESULT PASS ' in output and 'RESULT FAIL' not in output
    return dict(samples=values,median_ms=statistics.median(values),min_ms=min(values),max_ms=max(values),warmups=4,
                scope='QPC/EVENT completion: one25%-coverage original DIP, component transfers/real Hdr owner exchange and terminal supplemental TAA+AgX. Source setup, ordinary motion, frame M clear, Present and readback excluded. Off/on are separate processes, not paired GPU-only measurements or game FPS')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True);parser.add_argument('--dll',type=Path,required=True)
    parser.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--result',type=Path,default=bottle.results_dir(ROOT,create=False)/'linear-emission-live-gpu.json')
    args=parser.parse_args();assert bottle.BOTTLE=='X3','X3 fixture bottle required'
    inputs=[args.fixture.resolve(),args.dll.resolve(),*[args.programs.resolve()/x for x in dict.fromkeys(PROGRAMS+CORPUS_PROGRAMS)]]
    assert all(x.is_file() for x in inputs),'explicit prebuilt inputs and local originals required'
    hashes={str(x):sha(x) for x in inputs};raw=Path(tempfile.mkdtemp(prefix='x3-linear-emission-live-'))
    result=dict(passed=False,bottle=bottle.describe(),raw=str(raw),inputs=hashes,cases={},game_launched=False,
        scope='Actual capture DIP/MotionOutput/LinearEmissionPass/HdrPass owning exchange/supplemental TemporalPass; 20 exact original source pairs / 8 native VS / 10 native PS, including PS2.1, native DEFAULT UV and INSTANCE direct UV/fog/no-fade; prior 12 functional frames retained',
        limitations=['Native Windows and gameplay untested',
            'X3M_SCENE_HOOK=0 and a fixture-only compositor-owner admission override are used. Production game owner-memory binding at BeginScene is not exercised here; prior bloom owner/thread qualification is reused. Actual capture DIP, pass, Hdr exchange, supplemental TemporalPass and terminal StretchRect remain exercised.',
            'Failed source witness is a real invalid-index-buffer DIP; partial GPU submission semantics remain host/component-qualified only',
            'Fifty-two functional frames include the original twelve controls plus twenty accepted/disappearing exact pairs and one real failed-source rejection (frame8): fifty-one successful TAA frames resolve at StretchRect; frame8 performs an unrecognized native copy and unresolved HDR publication ending at Present, with no TAA/history seed. This is not a healthy Present-triggered TAA qualification.',
            'R1 used the wrong later-material declaration. R2 requested exact comparison with a nonrepresentable FP16 alpha sum. This fixture binds the correct declaration and uses an exactly representable .25 second source alpha; strict alpha equality and RGB tolerances are unchanged.',
            'Functional TAA readback uses a seam-only resolve-output flag independent of capture admission and persists across Reset. It is disabled for timing. Source snapshots and functional HDR readbacks explicitly get the logical RT before sampling, restoring borrowed MRT/mip state. R3 observed bias restoration during diagnostic readback; explicit getters now avoid relying on backend child-release callback timing. Immediate physical bias after each A/B draw and exact restores (two healthy, A-only one on the failed frame) are checked; consecutive lazy bias retention is not claimed here and retains its dedicated prior evidence.'])
    try:
        runs=[(lazy,enabled,False) for lazy in (0,1) for enabled in (0,1)]+[(1,enabled,True) for enabled in (0,1)]
        for lazy,enabled,benchmark in runs:
            assert not game_running(),'game running; no fixture launch'
            key=f'benchmark-emission{enabled}' if benchmark else f'lazy{lazy}-emission{enabled}';work=raw/key;work.mkdir()
            shutil.copy2(inputs[0],work/'fixture.exe');shutil.copy2(inputs[1],work/'d3d9.dll')
            env={k:v for k,v in os.environ.items() if not k.startswith('X3M_')}
            env.update(X3M_MOTION_OUTPUT='1',X3M_HDR='1',X3M_HDR_TONEMAP='agx',X3M_HDR_DECODE='gamma2.2',X3M_HDR_BLOOM='0',
                X3M_HDR_EXPOSURE='manual',X3M_HDR_EV_MANUAL='0',X3M_HDR_CLAMP='0',X3M_LINEAR_MATERIALS='0',
                X3M_LINEAR_EMISSIONS=str(enabled),X3M_EMISSION_GAIN='1',X3M_OWNERSHIP='1',X3M_TAA='1',X3M_FIXTURE_TAA_SENTINEL='1',
                X3M_TAA_SHARPEN='0',X3M_TAA_MIP_BIAS='-.5',X3M_SCENE_HOOK='0',X3M_TELEMETRY='1',X3M_MOTION_FRAME_LOG='1',
                X3M_TAA_DEBUG='0' if benchmark else '1',X3M_CAPTURE_START='1000000' if benchmark else '1',X3M_CAPTURE_FRAMES='0',X3M_MOTION_RT_MODE='lazy' if lazy else 'perdraw',X3M_STATE_SHADOW='1',WINEDLLOVERRIDES='d3d9=n,b')
            command=[bottle.WINE,*bottle.wine_args(),'--dll','d3d9=n,b','--workdir',str(work),str(work/'fixture.exe'),
                     'Z:'+str(inputs[2]),'Z:'+str(inputs[3]),'emissionsbench' if benchmark else 'emissions','Z:'+str(inputs[4]),'Z:'+str(inputs[5])]
            start=time.monotonic()
            with (work/'stdout.txt').open('w') as out,(work/'wine.log').open('w') as err:
                child=subprocess.run(command,env=env,stdout=out,stderr=err,timeout=180)
            assert child.returncode==0,f'{key}: fixture exit{child.returncode}; {work}'
            logs=list((work/'x3-modern-captures').glob('session-*.log'));assert len(logs)==1
            output=(work/'stdout.txt').read_text()
            if benchmark:case=validate_timing(output,bool(enabled))
            else:
                case=validate(output,logs[0].read_text(),bool(enabled));case.update(validate_pixels(work,bool(enabled)))
            case['seconds']=time.monotonic()-start
            result['cases'][key]=case
            print(f'{key}: completed',flush=True)
        for enabled in (0,1):
            a,b=(result['cases'][f'lazy{i}-emission{enabled}'] for i in (0,1))
            assert a['temporal_sha256']==b['temporal_sha256'] and a['covered_pixels']==b['covered_pixels'],'lazy mode changed output'
        native=result['cases']['benchmark-emission0'];enhanced=result['cases']['benchmark-emission1']
        result['completion_cost']=dict(native_median_ms=native['median_ms'],enabled_median_ms=enhanced['median_ms'],median_difference_ms=enhanced['median_ms']-native['median_ms'],paired=False)
        assert hashes=={str(x):sha(x) for x in inputs},'prebuilt inputs changed'
        result['passed']=True
    finally:
        target=args.result if result['passed'] else raw/'failed-result.json';target.parent.mkdir(parents=True,exist_ok=True);target.write_text(json.dumps(result,indent=2)+'\n')


if __name__=='__main__':main()
