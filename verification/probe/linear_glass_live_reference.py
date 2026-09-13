"""Bounded evidence contract for the existing live fixture's six glass pairs.

Detached glass qualification owns the complete material equations. Here all
sampled color channels are endpoints and unclamped P is below one: the paired
ordinary working RGB therefore predicts encoded linear output directly.
"""
import math
import re
from linear_glass_fixture_reference import PAIRS

BOOTSTRAP = ('vs_53a0a641107ed76c.bin', 'ps_8759c7838bbc86c2.bin')
PROGRAM_NAMES = tuple(dict.fromkeys(BOOTSTRAP + tuple('vs_'+v+'.bin' for v,_ in PAIRS) + tuple('ps_'+p+'.bin' for _,p in PAIRS)))
MATERIAL_PROGRAMS = {tuple(name[:-4].split('_',1)) for name in PROGRAM_NAMES}
FRAME_COUNT = 27
SAMPLER_REFUSALS = {12:0, 14:1, 16:2}
GATED = {18,20}
UNMATCHED = set(range(0,12,2)) | {12,18,19,20,21,23,25,26}
SCOPE = ('Actual live six SM3 glass pairs; seven glass originals and two existing bootstrap programs. '
         'Material off/on, depth off/on, perdraw/lazy, all three sampler refusals, actual opaque '
         'COLORWRITE15 versus7 and blend gate, physical WRAP4/5 observation/restoration, '
         'StateBlock/Reset, alpha and temporal twins, two calibrated perspective RGB comparisons. '
         'No gameplay or native-Windows runtime claim.')
LIMITATIONS = [
    'Existing scripted host control-flow evidence owns driver creation/bind/getter/restore failure injection; this GPU mode does not inject driver failures.',
    'Detached 254-case glass qualification owns full material equations, both faces, over-one transport and term isolation. Live calibrated color inputs permit comparison against actual ordinary working RGB.',
    'X3 detached evidence reports effective programmable COLOR FLAT=false; effective native-Windows FLAT interpolation remains unverified.',
    'TAA resolve, unchanged earlier material families and broad performance chains are not rerun. Native alpha and motion/depth attachment outputs are checked directly.',
    'Native Windows and gameplay appearance/performance remain unverified; the docking-port distance transition is a separate investigation.',
]


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)', line))


def schedule(material):
    return [dict(plan=p, frame=p, pair=p//2 if p<12 else 4 if p==26 else 0,
                 step=p%2 if p<12 else p-12, transport=p>=25,
                 routed=p not in GATED, matched=p not in UNMATCHED,
                 combined=bool(material and p not in GATED and p not in SAMPLER_REFUSALS),
                 refusal=5 if p in GATED else 4 if p in SAMPLER_REFUSALS else 0)
            for p in range(FRAME_COUNT)]


def expected_wrap(plan, depth):
    values=[13,11,6,0,15,7]+[0]*10 if plan!=23 else [0]*16
    if plan not in GATED:
        values[4]=0
        if depth: values[5]=0
    return values


def validate_case(output, trace_lines, material, depth, rt_mode):
    assert rt_mode in ('perdraw','lazy')
    lines=output.splitlines(); samples=schedule(material)
    def unique(source,prefix,key):
        rows=[fields(line) for line in source if line.startswith(prefix)]
        assert len(rows)==len({int(r[key]) for r in rows}), (prefix,'duplicate evidence')
        return {int(r[key]):r for r in rows}
    live=unique(lines,'GLASS_LIVE ','plan')
    motion=unique(lines,'MOTION_HASH ','frame')
    rows=list(trace_lines)
    frames=unique(rows,'motion_output_frame ','frame')
    materials=unique(rows,'linear_material_frame ','frame')
    assert set(live)==set(motion)==set(frames)==set(range(FRAME_COUNT))
    summaries=[fields(line) for line in lines if line.startswith('RESULT PASS ')]
    assert len(summaries)==1 and not any(line.startswith('RESULT FAIL') for line in lines)
    summary=summaries[0]
    assert int(summary['frames'])==FRAME_COUNT and int(summary['taa_reference_frames'])==0
    assert int(summary['restorations'])==2*FRAME_COUNT and int(summary['checks'])>0
    assert (int(summary['depth_written'])>0)==bool(depth)
    assert sum(line=='RESET PASS' for line in lines)==1
    devices=[fields(line) for line in rows if line.startswith('motion_output_device ')]
    assert devices and all(int(r['depth'])==depth and r['rt_mode']==rt_mode for r in devices)
    if not depth: assert all(r['depth_reason']=='fixture_motion_only' for r in devices)
    releases=[fields(line) for line in rows if line.startswith('motion_output_release ')]
    assert len(releases)==1 and int(releases[0]['released'])==1
    variants=[fields(line) for line in rows if line.startswith('linear_material_variant ')]
    assert not any(line.startswith('linear_material_xt_default_') for line in rows)
    if material:
        assert set(materials)==set(frames)
        assert len(variants)==len(MATERIAL_PROGRAMS)
        assert {(r['kind'],r['original']) for r in variants}==MATERIAL_PROGRAMS
        assert all(int(r['transform'])==0 and int(r['create'],16)==0 for r in variants)
    else: assert not variants and not materials
    wraps=[fields(line) for line in lines if line.startswith('GLASS_WRAP ')]
    assert len(wraps)==2*FRAME_COUNT
    for index,row in enumerate(wraps):
        plan,draw=divmod(index,2)
        assert int(row['plan'])==int(row['frame'])==plan and int(row['draw'])==draw
        assert int(row['sequence'])==index+1 and int(row['valid'])==1 and int(row['result'],16)==0
        assert list(map(int,row['values'].split(',')))==expected_wrap(plan,depth)
    transports={}
    for row in (fields(line) for line in lines if line.startswith('GLASS_TRANSPORT ')):
        plan,draw=int(row['plan']),int(row['draw'])
        assert plan in (25,26) and draw in (0,1) and (plan,draw) not in transports
        assert int(row['frame'])==plan and int(row['combined'])==int(material and draw==1)
        assert float(row['perspective'])==.125
        pixels,error,spread=int(row['pixels']),float(row['max_error']),float(row['rgb_range'])
        assert math.isfinite(error) and math.isfinite(spread)
        assert (pixels>64*64//4 and 0<=error<=1 and spread>.001) if draw else (pixels==0 and error==spread==0)
        if not depth: assert int(row['depth'],16)==0
        transports[plan,draw]=row
    assert set(transports)=={(p,d) for p in (25,26) for d in (0,1)}
    rgb_samples={}
    for row in (fields(line) for line in lines if line.startswith('GLASS_SAMPLE ')):
        key=tuple(int(row[k]) for k in ('plan','draw','x','y'))
        assert key not in rgb_samples and int(row['frame'])==key[0]
        rgba=list(map(float,row['rgba'].split(',')))
        assert len(rgba)==4 and all(math.isfinite(v) for v in rgba)
        rgb_samples[key]=rgba
    expected_keys={(p,d,x,y) for p in (25,26) for d in (0,1) for x in (16,32,48) for y in (16,32,48)}
    assert set(rgb_samples)==expected_keys
    for plan in (25,26):
        for x in (16,32,48):
            for y in (16,32,48):
                ordinary,linear=(rgb_samples[plan,d,x,y] for d in (0,1))
                assert ordinary[3]==linear[3]
                for a,b in zip(ordinary[:3],linear[:3]):
                    assert a>=0
                    wanted=a**(1/2.2) if material else a
                    assert abs(b-wanted)<=.006*abs(wanted)+.00002
    result={}
    for sample in samples:
        plan=sample['plan'];row=live[plan];vertex,pixel=PAIRS[sample['pair']]
        assert int(row['frame'])==plan and int(row['pair'])==sample['pair'] and int(row['step'])==sample['step']
        assert (row['vs'],row['ps'])==(vertex,pixel)
        for key in ('combined','refusal','matched'): assert int(row[key])==sample[key], (plan,key,row[key],sample[key])
        rgba=list(map(float,row['rgba'].split(',')))
        assert len(rgba)==4 and all(math.isfinite(v) for v in rgba)
        assert abs(rgba[3]-(1. if plan==18 else .625*128/255))<.001
        state=frames[plan];routed=2*sample['routed']
        assert int(state['routed'])==routed and int(state['depth_routed'])==routed*depth
        assert int(state['matched'])==2*sample['matched'] and int(state['gate4'])==2*(plan in GATED)
        assert all(int(state[k])==0 for k in ('gate3','apply_failures','restore_failures','taa_resolved')) and state['rt_mode']==rt_mode
        if material:
            m=materials[plan]
            assert int(m['routed'])==(1 if sample['transport'] else 2)*sample['combined']
            assert int(m['refused'])==(1 if sample['transport'] else 2 if plan in SAMPLER_REFUSALS else 0)
            assert int(m['bind_failures'])==int(m['bump_routed'])==0
        if not depth: assert int(motion[plan]['depth'],16)==0
        result[str(plan)]=dict(rgba=rgba,alpha_hash=row['alpha_hash'],image_hash=row['image_hash'],temporal_hashes=[motion[plan]['motion'],motion[plan]['depth']])
        if sample['transport']:
            ordinary,linear=transports[plan,0],transports[plan,1]
            assert all(ordinary[k]==linear[k] for k in ('alpha','motion','depth'))
            assert linear['alpha']==row['alpha_hash'] and linear['motion']==motion[plan]['motion'] and linear['depth']==motion[plan]['depth']
            result[str(plan)]['transport']={k:linear[k] for k in ('pixels','max_error','rgb_range')}
    return dict(frames=FRAME_COUNT,checks=int(summary['checks']),restorations=int(summary['restorations']),
                native_draw_observations=len(wraps),held_references=int(releases[0]['held']),samples=result)


def compare_cases(cases):
    assert set(cases)=={f'depth{d}-{m}-material{v}' for d in (0,1) for m in ('perdraw','lazy') for v in (0,1)}
    for depth in (0,1):
        for mode in ('perdraw','lazy'):
            off,on=(cases[f'depth{depth}-{mode}-material{m}'] for m in (0,1))
            assert on['held_references']==off['held_references']+len(MATERIAL_PROGRAMS)
            for plan,row in off['samples'].items():
                twin=on['samples'][plan]
                assert row['alpha_hash']==twin['alpha_hash'] and row['temporal_hashes']==twin['temporal_hashes']
                if int(plan) in GATED or int(plan) in SAMPLER_REFUSALS: assert row['image_hash']==twin['image_hash']
        for material in (0,1):
            a,b=(cases[f'depth{depth}-{mode}-material{material}'] for mode in ('perdraw','lazy'))
            assert a['samples']==b['samples'], 'lazy transaction changed results'
    for mode in ('perdraw','lazy'):
        for material in (0,1):
            a,b=(cases[f'depth{d}-{mode}-material{material}'] for d in (0,1))
            for plan,row in a['samples'].items():
                twin=b['samples'][plan]
                assert row['alpha_hash']==twin['alpha_hash'] and row['image_hash']==twin['image_hash'] and row['temporal_hashes'][0]==twin['temporal_hashes'][0]
