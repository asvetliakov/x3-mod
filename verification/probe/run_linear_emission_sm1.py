#!/usr/bin/env python3
"""Detached SM1 qualification; consumes an explicit prebuilt EXE, never a game/DLL build.

Invoke through wine_lock.py with X3M_FIXTURE_BOTTLE=X3. Completed measurements
can honestly reject one or both precision candidates without aborting the probe.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import bottle
from game_guard import game_running

ROOT=Path(__file__).resolve().parents[2]
PAIRS=(('0d44b36d48d24f7a','078494828322bcca',0),
       ('1b6863a088a177af','84d3de8887c963c5',2),
       ('21a2c13be7f989c3','d4a26efb7c603931',2),
       ('5e484a06672e28fb','ec1f5c4a2f4e1445',2),
       ('637dadcb5efa3288','078494828322bcca',1),
       ('6da1b1b6ed63ec82','2ea025492d370c8e',0),
       ('88620f88d6e0a00e','a5c3495e27270b4a',0),
       ('ed42e0742e47dca4','2ea025492d370c8e',1),
       ('f9755e1154244f58','a5c3495e27270b4a',1))
CASES=('unorm_point','unorm_linear','dxt_point','dxt_linear','fp16_point','fp16_linear',
       'mip_bias_point','mip_bias_linear','address_wrap','address_mirror','fog_on','fog_zero',
       'perspective','clip','flat','wrap0','alpha_ge128','alpha_gt128','rgb_zero','gain_zero',
       'screen_native','depth','hdr_boundary','fade_zero','alpha_zero','gain_2_5',
       'fog_fractional','fog_lit','ps1_range','screen_overlap','cap_before_fade_gain')
PROGRAMS=tuple(dict.fromkeys(f'{kind}_{pair[index]}.bin' for pair in PAIRS
                            for index,kind in ((0,'vs'),(1,'ps'))))

def fields(line):return dict(item.split('=',1) for item in line.split()[1:] if '=' in item)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def finite(value):
    value=float(value);assert math.isfinite(value) and value>=0
    return value

def validate(output):
    lines=output.splitlines()
    assert not any(x.startswith('SM1_ABORT') for x in lines),'native baseline/device probe aborted'
    caps=[fields(x) for x in lines if x.startswith('SM1_CAPS ')]
    assert len(caps)==1 and int(caps[0]['mrt'])>=3 and int(caps[0]['ps'],16)>=0xffff0200
    assert finite(caps[0]['ps1_max'])>0 and caps[0]['post_blend']=='1'
    created=[fields(x) for x in lines if x.startswith('SM1_CREATE ')]
    assert [(int(x['pair']),int(x['mode']),int(x['gain_index'])) for x in created]==[(p,m,g) for p in range(9) for m in range(6) for g in range(4)],'all promotion creations'
    creation={}
    for row in created:
        key=tuple(int(row[k]) for k in ('pair','mode','gain_index'))
        assert int(row['words'])>0;finite(row['ms'])
        creation[key]=not bool(int(row['hr'],16)&0x80000000)
    projected=[fields(x) for x in lines if x.startswith('SM1_PROJECTED ')]
    assert [int(x['pair']) for x in projected]==list(range(9))
    for row in projected:
        assert int(row['flags'],16)&0x100 and row['enhanced_draws']==row['admitted']=='0' and row['reason']=='undefined_source_w','projected submission cannot be promoted'
    rows=[fields(x) for x in lines if x.startswith('SM1_CASE ')]
    assert [(int(x['pair']),int(x['case']),int(x['mode'])) for x in rows]==[(p,c,m) for p in range(9) for c in range(len(CASES)) for m in range(6)],'complete all-nine case/mode corpus'
    modes=[dict(mode=m,precision='native_pp' if m>=3 else 'full',outputs=m%3+1,
                measured=0,unsupported=0,native_in_range_failures=0,native_boundary_failures=0,
                emission_failures=0,max_native_error=0.,max_energy_fraction=0.) for m in range(6)]
    parity_failures=emission_failures=unsupported=0
    for row in rows:
        p,c,m=(int(row[k]) for k in ('pair','case','mode'));mode=modes[m]
        assert row['name']==CASES[c] and int(row['boundary'])==int(c in (22,30))
        g=1 if c==19 else 2 if c==25 else 3 if c==30 else 0
        if not creation[p,m,g]:
            assert row['status']=='unsupported' and int(row['hr'],16)&0x80000000
            mode['unsupported']+=1;unsupported+=1;continue
        assert row['status']=='measured';mode['measured']+=1
        counts={k:int(row[k]) for k in ('rgb_diff','alpha_diff','e_diff','mask_diff','survived')}
        assert all(0<=v<=4096 for v in counts.values())
        assert counts['rgb_diff']<=3072 and counts['alpha_diff']<=1024 and counts['survived']<=1024
        assert m%3!=0 or counts['e_diff']==0
        if c not in (16,17):assert m%3==2 or counts['mask_diff']==0
        # Except explicit alpha tests every authored draw must have survivors;
        # zero RGB/fade/gain/alpha do not erase geometric coverage.
        if c not in (16,17):assert counts['survived']>0
        parity=bool(counts['rgb_diff'] or counts['alpha_diff']);emission=bool(counts['e_diff'] or counts['mask_diff'])
        parity_failures+=parity;emission_failures+=emission
        mode['native_boundary_failures' if c in (22,30) else 'native_in_range_failures']+=parity
        mode['emission_failures']+=emission
        mode['max_native_error']=max(mode['max_native_error'],finite(row['max_native']))
        mode['max_energy_fraction']=max(mode['max_energy_fraction'],finite(row['max_energy_fraction']))
    # These three draws share source, geometry and gain bank zero. Distinct
    # native survival proves both sides of the authored alpha=128 boundary.
    indexed={(int(row['pair']),int(row['case']),int(row['mode'])):row for row in rows}
    for p in range(9):
        for m in range(6):
            base,ge,gt=(indexed[p,c,m] for c in (0,16,17))
            if not creation[p,m,0]:
                assert all(row['status']=='unsupported' for row in (base,ge,gt))
                continue
            assert all(row['status']=='measured' for row in (base,ge,gt))
            assert 0<int(gt['survived'])<int(ge['survived'])<int(base['survived']), (p,m,'distinct nonzero native alpha-test boundary coverage')
    overlaps=[fields(x) for x in lines if x.startswith('SM1_OVERLAP ')]
    expected_overlap=[(p,m) for p in range(9) for m in range(6) if m%3 and creation[p,m,0]]
    assert [(int(x['pair']),int(x['mode'])) for x in overlaps]==expected_overlap,'same-DIP overlap cannot be excluded'
    for row in overlaps:
        assert (row['dip'],row['triangles'],row['layers'],row['q_policy_implemented'])==('1','4','2','0')
        for key in ('actual_e','shared_e','q_e','gap'):finite(row[key])
        assert abs(float(row['shared_e'])-float(row['q_e'])-float(row['gap']))<1e-7
        assert float(row['gap'])>.001,'shared MRT attenuation must expose the native-q policy gap'
    complete=[fields(x) for x in lines if x.startswith('SM1_COMPLETE ')]
    assert len(complete)==1
    assert {k:int(v) for k,v in complete[0].items()}==dict(pairs=9,cases=len(CASES),rows=len(rows),unsupported=unsupported,parity_failures=parity_failures,emission_failures=emission_failures,reset=1)
    assert lines.count('SM1_RESET passed=1')==1
    witnesses=[fields(x) for x in lines if x.startswith('SM1_WITNESS ')]
    assert len(witnesses)<=6 and len({int(x['mode']) for x in witnesses})==len(witnesses)
    failed_modes={m['mode'] for m in modes if m['native_in_range_failures'] or m['native_boundary_failures'] or m['emission_failures']}
    assert {int(x['mode']) for x in witnesses}==failed_modes,'first failing raw witness per measured mode required'
    for row in witnesses:
        assert row['prefix']==f"failure_p{row['pair']}_c{row['case']}_m{row['mode']}"
    for mode in modes:
        mode['native_in_range_exact']=mode['unsupported']==mode['native_in_range_failures']==0
        mode['boundary_exact']=mode['unsupported']==mode['native_boundary_failures']==0
        mode['qualified_in_range']=mode['native_in_range_exact'] and mode['emission_failures']==0
    return dict(completed=True,pairs=9,original_vs=9,original_ps=6,cases=len(CASES),rows=len(rows),
                caps=caps[0],gains=[1.,0.,2.5,.25],promoted_creations=216,modes=modes,all_modes_qualified_in_range=all(x['qualified_in_range'] for x in modes),
                unsupported=unsupported,parity_failures=parity_failures,emission_failures=emission_failures,
                projected_refusals=9,reset=1,witnesses=witnesses,case_results=rows,
                overlap_diagnostics=overlaps,creation_total_ms=sum(float(x['ms']) for x in created))

def compact_report(measured):
    """Raw stdout owns the full corpus; canonical evidence retains aggregates."""
    return {key:value for key,value in measured.items() if key!='case_results'}

def validate_witnesses(work,report):
    for row in report['overlap_diagnostics']:
        if int(row['pair'])==0:
            for kind in ('emission','native'):
                path=work/f"overlap_mode{row['mode']}_{kind}.rgba16f"
                assert path.is_file() and path.stat().st_size==8192,'same-DIP raw overlap diagnostic'
    for row in report['witnesses']:
        prefix=row['prefix'];outputs=int(row['mode'])%3+1
        names=[f'{prefix}_native.rgba16f']+[f'{prefix}_actual{i}.rgba16f' for i in range(outputs)]+[f'{prefix}_reference{i}.rgba16f' for i in range(3)]
        assert all((work/name).is_file() and (work/name).stat().st_size==32*32*8 for name in names),'complete local failure readbacks'

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--fixture',type=Path,required=True)
    p.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    p.add_argument('--result',type=Path,default=bottle.results_dir(ROOT,create=False)/'linear-emission-sm1-probe.json')
    args=p.parse_args();assert bottle.BOTTLE=='X3','new fixtures use X3 only';assert not game_running(),'game running'
    inputs=[args.fixture.resolve()]+[args.programs.resolve()/name for name in PROGRAMS]
    assert all(x.is_file() for x in inputs),'prebuilt fixture and all local originals required'
    provenance={str(x):sha(x) for x in inputs}
    sources=[ROOT/name for name in ('verification/probe/linear_emission_sm1_fixture.cpp',
             'src/renderer/linear_emission_sm1.cpp','src/renderer/linear_emission_sm1.h',
             'src/renderer/linear_emission.h','verification/probe/build_linear_emission_sm1.sh',
             'verification/probe/run_linear_emission_sm1.py')]
    source_hashes={str(x.relative_to(ROOT)):sha(x) for x in sources}
    work=Path(tempfile.mkdtemp(prefix='x3-emission-sm1-'))
    report=dict(completed=False,bottle=bottle.describe(),raw=str(work),inputs=provenance,source_sha256=source_hashes,game_launched=False,
                scope='Detached all-nine PS1/native-vs-PS2 promotion, three output counts, full and native-PP; no live/screen composition integration',
                limitations=['Native Windows and gameplay untested.',
                  'PROJECTED TSS exercises a fixture-only refusal, not a production admission implementation.',
                  'Exact native RGBA comparison is separated from beyond-PS1MaxValue finite HDR boundary results.',
                  'E uses independently measured full/PP TEXLD and raw multiplier, CPU decode/cap/gain, propagated FP16 half-ULP input intervals plus .003 relative/.00004 absolute shader arithmetic tolerance; exact zero E and E alpha are bitwise.',
                  'Screen cases qualify native B and actual auxiliary MRT behavior only. The overlapping-particle case submits four triangles in ONE DIP and tests E2+(1-E2)*FP16(E1), while reporting its gap from E2+(1-q2)*FP16(E1). The native-q composition policy is not implemented.',
                  'Creation timings are CPU-inclusive diagnostics, not GPU steady-state cost or game FPS.'])
    try:
        shutil.copy2(inputs[0],work/'fixture.exe');env={k:v for k,v in os.environ.items() if not k.startswith('X3M_')};env['WINEDLLOVERRIDES']='d3d9=b'
        command=[bottle.WINE,*bottle.wine_args(),'--workdir',str(work),str(work/'fixture.exe'),'Z:'+str(args.programs.resolve())]
        start=time.monotonic()
        with (work/'stdout.txt').open('w') as out,(work/'wine.log').open('w') as err:
            child=subprocess.run(command,env=env,stdout=out,stderr=err,timeout=900)
        report['seconds']=time.monotonic()-start;assert child.returncode==0,f'probe exit {child.returncode}; {work}'
        measured=validate((work/'stdout.txt').read_text());validate_witnesses(work,measured)
        assert provenance=={str(x):sha(x) for x in inputs},'prebuilt inputs changed'
        assert source_hashes=={str(x.relative_to(ROOT)):sha(x) for x in sources},'qualification sources changed'
        report.update(compact_report(measured))
    finally:
        target=args.result if report['completed'] else work/'failed-result.json';target.parent.mkdir(parents=True,exist_ok=True);target.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps({k:report[k] for k in ('completed','all_modes_qualified_in_range','rows','unsupported','parity_failures','emission_failures')}))

if __name__=='__main__':main()
