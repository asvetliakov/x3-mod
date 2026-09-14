#!/usr/bin/env python3
"""Consume an approved prebuilt packed-screen EXE; never build or launch the game.

Run through wine_lock.py with X3M_FIXTURE_BOTTLE=X3. This is a detached
mathematical GPU prototype, not a live native-B/publication contract. Step E
(screen-emission-region.md): the source writes the red|blue plane lanes only,
the C assembly decodes the accumulated native lane once with the gain as a
literal, and at gain 1 every in-domain C equals the native B bit for bit;
schedule 3 accumulates an 8-layer overlapping chain in one DIP.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile
import time
import bottle
from game_guard import game_running

ROOT=Path(__file__).resolve().parents[2]
PAIRS=(('0d44b36d48d24f7a','078494828322bcca',0),('1b6863a088a177af','84d3de8887c963c5',2),
       ('21a2c13be7f989c3','d4a26efb7c603931',2),('5e484a06672e28fb','ec1f5c4a2f4e1445',2),
       ('637dadcb5efa3288','078494828322bcca',1),('6da1b1b6ed63ec82','2ea025492d370c8e',0),
       ('88620f88d6e0a00e','a5c3495e27270b4a',0),('ed42e0742e47dca4','2ea025492d370c8e',1),
       ('f9755e1154244f58','a5c3495e27270b4a',1))
CASES=('asymmetric','zero_channel','zero_rgb','q_one','gain_zero','gain_quarter','gain_2_5',
       'alpha_ge127_128','alpha_ge128_129','alpha_gt127_128','alpha_gt128_129','accepted_alpha_zero',
       'depth_one','depth_none','mask_seed','signed_q_boundary','q_gt1_boundary','hdr_boundary',
       'overflow_boundary','flat','clip','perspective','uv_flip','fog_layout')
BOUNDARIES=(15,16,17,18)
GAINS=(1.,0.,.25,2.5)
SCHEDULES=4          # 0 one DIP two particles, 1 two DIPs, 2 reversed order, 3 one DIP eight-layer chain
CHAIN_LAYERS=8
ERRORS=('b_diff','alpha_diff','c_diff','plane_diff','mask_diff','alpha_mask_diff',
        'unchanged_diff','init_diff','nonfinite','same_dip_diff','native_c_diff')
PROGRAMS=tuple(dict.fromkeys(f'{kind}_{pair[index]}.bin' for pair in PAIRS for index,kind in ((0,'vs'),(1,'ps'))))
PHASES=('native_dip','initialize','packed_dip','assemble_b','assemble_c')

def fields(line):return dict(x.split('=',1) for x in line.split()[1:] if '=' in x)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def gain_index(c):return 1 if c==4 else 2 if c in (5,17) else 3 if c==6 else 0
def finite(value):
    value=float(value);assert math.isfinite(value) and value>=0
    return value

def validate_caps(lines,width,height):
    rows=[fields(x) for x in lines if x.startswith('PACKED_CAPS ')]
    assert len(rows)==1;caps=rows[0]
    assert (int(caps['width']),int(caps['height']))==(width,height)
    assert int(caps['mrt'])>=4 and all(caps[k]=='1' for k in ('masks','post_blend','src_one','dst_invsrcalpha'))
    assert finite(caps['ps1_max'])>0
    return caps

def validate_helpers(lines):
    rows=[fields(x) for x in lines if x.startswith('PACKED_HELPER ')]
    expected=(('initialize',22,1,2,1,15,1),('assemble_b',5,4,5,4,1,0),('assemble_c',44,5,7,5,1,2))
    assert [(x['name'],*[int(x[k]) for k in ('alu','tex','temps','samplers','outputs','constants')]) for x in rows]==list(expected),'authored init/B/C resource budgets'
    assert all(int(x['words'])>0 for x in rows)
    return rows

def validate(output):
    lines=output.splitlines();assert not any(x.startswith('PACKED_ABORT') for x in lines),'fixture/native baseline aborted'
    caps=validate_caps(lines,32,32);helpers=validate_helpers(lines)
    creates=[fields(x) for x in lines if x.startswith('PACKED_CREATE ')]
    assert [(int(x['pair']),int(x['gain_index'])) for x in creates]==[(p,g) for p in range(9) for g in range(4)]
    creation={}
    for x in creates:
        assert int(x['words'])>0
        creation[int(x['pair']),int(x['gain_index'])]=not bool(int(x['hr'],16)&0x80000000)
    rows=[fields(x) for x in lines if x.startswith('PACKED_CASE ')]
    assert [(int(x['pair']),int(x['case']),int(x['schedule'])) for x in rows]==[(p,c,s) for p in range(9) for c in range(24) for s in range(SCHEDULES)],'all-nine ordered/overlapping/chain corpus required'
    aggregates={(p,s):dict(pair=p,schedule=s,measured=0,unsupported=0,in_domain_failures=0,boundary_failures=0,
                          max_fraction=0.,flag_zero_changed_boundary=0,max_layers=0,native_c_off_by_one=0,native_c_channels=0,**{key:0 for key in ERRORS}) for p in range(9) for s in range(SCHEDULES)}
    indexed={};unsupported=failures=boundary_failures=order_changed=0
    for row in rows:
        p,c,s=(int(row[k]) for k in ('pair','case','schedule'));agg=aggregates[p,s];indexed[p,c,s]=row
        assert row['name']==CASES[c] and int(row['boundary'])==int(c in BOUNDARIES)
        if not creation[p,gain_index(c)]:
            assert row['status']=='unsupported' and int(row['hr'],16)&0x80000000
            unsupported+=1;agg['unsupported']+=1;continue
        assert row['status']=='measured';agg['measured']+=1
        counts={key:int(row[key]) for key in ERRORS}
        assert all(0<=v<=100000 for v in counts.values())
        for key,value in counts.items():agg[key]+=value
        assert int(row['original_dips'])==int(row['packed_dips'])==(2 if s==1 else 1),'one-DIP overlap cannot be replaced by particle replay'
        layers=CHAIN_LAYERS if s==3 else 2
        assert int(row['layers'])==layers and 0<=int(row['max_layers'])<=layers
        agg['max_layers']=max(agg['max_layers'],int(row['max_layers']))
        assert 0<=int(row['overlap'])<=1024 and 0<=int(row['surviving'])<=1024*layers
        if c in (9,13):assert int(row['surviving'])==int(row['overlap'])==0
        else:assert int(row['surviving'])>0
        if c in (0,1,2,3,4,5,6,8,11,14):
            assert int(row['overlap'])>0
            # The chain schedule accumulates every layer on its middle strip.
            assert int(row['max_layers'])==layers,(p,c,s,'full overlap depth',row['max_layers'])
        # C equals the native B within one FP16 code at gain 1 in domain
        # (step E); the counters are only reported there, and the off-by-one
        # codes (the GPU POW round trip) stay a small fraction of the
        # surviving channel values.
        off=int(row['native_c_off_by_one'])
        if c in (4,5,6) or c in BOUNDARIES:assert counts['native_c_diff']==off==0
        else:
            agg['native_c_off_by_one']+=off;agg['native_c_channels']+=3*int(row['surviving'])
            assert off<=int(row['surviving'])*3//10,(p,c,s,'off-by-one codes bounded',off,row['surviving'])
        assert s==1 or counts['same_dip_diff']==0
        assert s==2 or int(row['order_changed'])==0
        if c in (1,2):assert int(row['unchanged_covered'])>0,'actual covered unchanged-channel witness'
        if c not in BOUNDARIES:assert int(row['flag_zero_changed'])==0,'unchanged-channel flag is only proved in-domain'
        else:agg['flag_zero_changed_boundary']+=int(row['flag_zero_changed'])
        failed=any(counts.values())
        if c in BOUNDARIES:boundary_failures+=failed;agg['boundary_failures']+=failed
        else:failures+=failed;agg['in_domain_failures']+=failed
        if c==0 and s==2:order_changed+=int(row['order_changed'])
        agg['max_fraction']=max(agg['max_fraction'],finite(row['max_fraction']))
    # Both accepted and rejected alpha/depth controls must actually exist; a
    # success with no surviving particles is not mathematical qualification.
    for p in range(9):
        for s in range(SCHEDULES):
            if not creation[p,0]:continue
            count=lambda c:int(indexed[p,c,s]['surviving'])
            assert 0<count(7)<count(8)==count(0)
            assert count(9)==0<count(10)<count(8)
            assert count(13)==0<count(12)<count(0)
            assert count(11)==count(2)==count(0),'alpha/RGB zero retains native geometry'
            assert int(indexed[p,0,2]['order_changed'])>0,'asymmetric sources must expose primitive-order dependence'
    complete=[fields(x) for x in lines if x.startswith('PACKED_COMPLETE ')]
    assert len(complete)==1
    expected=dict(pairs=9,cases=24,schedules=SCHEDULES,rows=24*9*SCHEDULES,unsupported=unsupported,failures=failures,
                  boundary_failures=boundary_failures,order_changed=order_changed,reset=1,
                  owned_targets=8,target_bytes=8*32*32*8,live_publication=0)
    assert {k:int(v) for k,v in complete[0].items()}==expected
    assert lines.count('PACKED_RESET passed=1')==1
    witnesses=[fields(x) for x in lines if x.startswith('PACKED_WITNESS ')]
    expected_classes=({0} if failures else set())|({1} if boundary_failures else set())
    assert len(witnesses)==len(expected_classes) and {int(x['boundary']) for x in witnesses}==expected_classes
    for row in witnesses:
        p,c,s=(int(row[k]) for k in ('pair','case','schedule'))
        assert int(row['boundary'])==int(c in BOUNDARIES)
        assert row['prefix']==f'failure_p{p}_c{c}_s{s}'
        assert indexed[p,c,s]['status']=='measured' and any(int(indexed[p,c,s][key]) for key in ERRORS)
    ranges=[fields(x) for x in lines if x.startswith('PACKED_RANGE_WITNESS ')]
    assert ranges==([dict(prefix='range_q2',q_clamped_by_fixture='0',qualification='0')] if creation[0,0] else [])
    off_by_one=sum(a['native_c_off_by_one'] for a in aggregates.values());channels=sum(a['native_c_channels'] for a in aggregates.values())
    return dict(completed=True,qualified_in_domain=failures==unsupported==0,live_publication=False,native_c_off_by_one=off_by_one,native_c_channels=channels,
                pairs=9,original_vs=9,original_ps=6,cases=24,schedules=SCHEDULES,rows=24*9*SCHEDULES,gains=GAINS,creations=36,chain_layers=CHAIN_LAYERS,
                unsupported=unsupported,failures=failures,boundary_failures=boundary_failures,
                order_changed=order_changed,reset=1,owned_targets=8,target_bytes=8*32*32*8,
                caps=caps,helpers=helpers,pair_schedule_results=list(aggregates.values()),witnesses=witnesses,range_witnesses=ranges)

def witness_names(prefix):
    return [f'{prefix}_{name}.rgba16f' for name in ('native','b','c','mask','initial_mask')]+[
        f'{prefix}_{name}{k}.rgba16f' for name in ('plane','initial_plane') for k in range(3)]+[
        f'{prefix}_ref{s}_{k}.rgba16f' for s in range(2) for k in range(3)]

def validate_witnesses(work,report):
    for row in report['witnesses']+report['range_witnesses']:
        for name in witness_names(row['prefix']):
            p=work/name;assert p.is_file() and p.stat().st_size==8192,'complete local first-failure/range witness'

def validate_timing(output):
    lines=output.splitlines();assert not any(x.startswith('PACKED_ABORT') for x in lines)
    caps=validate_caps(lines,1920,1080);helpers=validate_helpers(lines)
    refused=[fields(x) for x in lines if x.startswith('PACKED_BENCH_UNSUPPORTED ')]
    if refused:
        assert len(refused)==1 and int(refused[0]['hr'],16)&0x80000000
        assert not any(x.startswith(('PACKED_TIMING ','PACKED_BENCH_COMPLETE ')) for x in lines)
        return dict(supported=False,hr=refused[0]['hr'],caps=caps)
    rows=[fields(x) for x in lines if x.startswith('PACKED_TIMING ')]
    assert [(int(x['sample']),x['phase']) for x in rows]==[(i,p) for i in range(8) for p in PHASES]
    summaries=[fields(x) for x in lines if x.startswith('PACKED_BENCH_COMPLETE ')]
    assert len(summaries)==1
    assert {k:int(v) for k,v in summaries[0].items()}==dict(samples=8,warmups=4,width=1920,height=1080,
        original_dips=1,packed_dips=1,init_draws=1,b_draws=1,c_draws=1,layers=2,owned_targets=8,
        target_bytes=8*1920*1080*8,live_publication=0)
    phases={p:[finite(x['ms']) for x in rows if x['phase']==p] for p in PHASES}
    return dict(supported=True,caps=caps,helpers=helpers,samples=phases,median_ms={p:statistics.median(v) for p,v in phases.items()},
                owned_targets=8,target_bytes=8*1920*1080*8,
                scope='EVENT/QPC per phase at1920x1080. Source timings are one indexed DIP with two overlapping particles (Begin/End included). Init and each assembly include their owned bindings/fullscreen draw. Oracle readbacks, source setters, A/M/sentinel seeds and resource creation are excluded; separately synchronized CPU-inclusive diagnostic timings, not GPU-only cost or game FPS.',
                resources='Eight FP16 targets include immutable A, M, three planes, assembled B/C, and a separate native-baseline target. This diagnostic target count is not a finalized live allocation contract.')

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--result',type=Path,default=bottle.results_dir(ROOT,create=False)/'linear-emission-sm1-packed.json')
    args=parser.parse_args();assert bottle.BOTTLE=='X3','X3 bottle required';assert not game_running(),'game running'
    inputs=[args.fixture.resolve()]+[args.programs.resolve()/name for name in PROGRAMS]
    assert all(p.is_file() for p in inputs),'approved prebuilt fixture and local originals required'
    hashes={str(p):sha(p) for p in inputs}
    sources=[ROOT/name for name in ('verification/probe/linear_emission_sm1_packed_fixture.cpp',
        'verification/probe/build_linear_emission_sm1_packed.sh','verification/probe/run_linear_emission_sm1_packed.py',
        'src/renderer/linear_emission_sm1.cpp','src/renderer/linear_emission_sm1.h','src/renderer/linear_emission.h')]
    source_hashes={str(p.relative_to(ROOT)):sha(p) for p in sources};raw=Path(tempfile.mkdtemp(prefix='x3-emission-sm1-packed-'))
    report=dict(completed=False,raw=str(raw),bottle=bottle.describe(),inputs=hashes,source_sha256=source_hashes,
                game_launched=False,scope='Detached packed-channel mathematical GPU prototype; original native source and independent measurement draws are separate qualification baselines, not proposed live geometry replay.',
                limitations=['Native Windows/gameplay untested. No source/assembly/restore/publication failure contract or live route is qualified.',
                    'Only bounded q in[0,1] and representable accumulation are mathematical qualification candidates. Signed/>1q/HDR/overflow rows retain raw q and operational mismatches/finiteness separately; no clamp or extra blend domain is adopted.',
                    'Step E: C = encode(g decode(B) + (1 - g) decode(A)) on the stored lanes (.006 relative/.00008 absolute, boundary rows doubled); at gain 1 in domain C must equal the native B within one FP16 code (native_c_diff beyond one code, native_c_off_by_one reported: the GPU POW round trip). Native assembled B RGBA and unchanged-channel A copying are exact. The green lane is never written by the source (plane masks 5).',
                    'Assembly uses ordered exact-zero blue via ABS/CMP (the blue lane accumulates q unclamped: out-of-domain q can cancel the flag to 0, publishing A, or overflow it to non-finite; the bracket never refuses on it). Its negative-linear clamp is only an operational boundary diagnostic; negative/nonfinite accumulated light has no accepted domain or overflow policy.',
                    'Persistent native-B assembly failure and physical return/publication semantics remain unresolved; live_publication is always false.'])
    try:
        results={}
        for name in ('functional','benchmark'):
            assert not game_running(),'game running';work=raw/name;work.mkdir();shutil.copy2(inputs[0],work/'fixture.exe')
            env={k:v for k,v in os.environ.items() if not k.startswith('X3M_')};env['WINEDLLOVERRIDES']='d3d9=b'
            command=[bottle.WINE,*bottle.wine_args(),'--workdir',str(work),str(work/'fixture.exe'),'Z:'+str(args.programs.resolve())]
            if name=='benchmark':command.append('benchmark')
            start=time.monotonic()
            with (work/'stdout.txt').open('w') as out,(work/'wine.log').open('w') as err:child=subprocess.run(command,env=env,stdout=out,stderr=err,timeout=900)
            assert child.returncode==0,f'{name}: exit{child.returncode}; {work}'
            measured=validate((work/'stdout.txt').read_text()) if name=='functional' else validate_timing((work/'stdout.txt').read_text())
            if name=='functional':validate_witnesses(work,measured)
            measured['seconds']=time.monotonic()-start;results[name]=measured
        assert hashes=={str(p):sha(p) for p in inputs},'prebuilt inputs changed'
        assert source_hashes=={str(p.relative_to(ROOT)):sha(p) for p in sources},'frozen qualification source changed'
        report.update(results);report['completed']=True
    finally:
        target=args.result if report['completed'] else raw/'failed-result.json';target.parent.mkdir(parents=True,exist_ok=True);target.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(json.dumps(dict(completed=True,qualified_in_domain=results['functional']['qualified_in_domain'],live_publication=False)))

if __name__=='__main__':main()
