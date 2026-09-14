#!/usr/bin/env python3
"""Consume-only distance-fade runtime/coverage qualification and paired cost.

Root owns the matching candidate seam DLL and the Wine lease. No builds or game
launches are performed. The detached fixture owns the broad material equations;
this script reuses its oracle for seven bounded actual-route color witnesses
(six Asteroid pairs and the station BUMPMAP pair; linear-station-source-over.md).
"""
import argparse
import copy
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import statistics
import struct
import subprocess
import tempfile
import time

import bottle
from game_guard import game_running
import run_linear_distance_fade as component
import run_linear_material as material

ROOT=Path(__file__).resolve().parents[2]
BOOTSTRAP=('vs_53a0a641107ed76c.bin','ps_8759c7838bbc86c2.bin')
# Fixture pairs 0-5: the six Asteroid producers; pair 6: the station BUMPMAP
# hull pair (run_linear_material.PAIRS[51]) with its opaque sibling PS
# 0c1f3f0f440e4a0c drawn natively beside it (frames 14 and 15).
FADE_PAIRS=tuple(material.PAIRS[110:116])+(material.PAIRS[component.STATION_PAIR],)
STATION_PAIR=6
STATION_SIBLING_PS='0c1f3f0f440e4a0c'
STATION_SURVIVOR=(32,32)  # frame 15: composed pixel outside the overwrite, the frame's sample point
STATION_ALPHA=.068359375  # AlphaValue .625 x fog .25 x lrp(EnableGlow .25, Diffuse.a .5, LightMap.a .25)
STATION_FRAMES=(14,15,16,17)
EMISSION_PAIR=('d5e1c75351ed3f04','8360f422de08b5bd')
PROGRAMS=tuple(dict.fromkeys(BOOTSTRAP+tuple(f'vs_{v}.bin' for v,_ in FADE_PAIRS)+tuple(f'ps_{p}.bin' for _,p in FADE_PAIRS)+(f'ps_{STATION_SIBLING_PS}.bin',f'vs_{EMISSION_PAIR[0]}.bin',f'ps_{EMISSION_PAIR[1]}.bin')))
FRAMES=33
FAILED_SOURCES=(25,32)
RESET_FRAMES=(29,32)
PRESENT_FRAMES=tuple(range(2,14,2))
MIXED_FRAMES=tuple(range(18,22))
RESOLUTIONS=((1280,768),(1920,1080))
COUNTS=(1,4,16)
# Step 3 (docs/architecture/linear-distance-fade-region.md): admitted fade
# draws compose in place (policy 4) whenever the device reports
# D3DPRASTERCAPS_SCISSORTEST; emission keeps the exchange. The fixture's fade
# sources draw under an application scissor of a quarter of the viewport
# ([W/8,5W/8) x [H/4,3H/4) for the first source, shifted by W/4 for later
# ones), and the pass intersects the derived rectangle with it, so the region
# the pass actually backs up and composes is never larger than that scissor.
# Timing variants inject a bound-derived rectangle inside the scissor through
# the seam-only X3M_FIXTURE_FADE_RECT: side fractions of the viewport give the
# requested area fractions f (1 = no injection, the derived full viewport).
IN_PLACE_POLICY=4
TIMING_FRACTIONS=(('1',None),('0.06',.245),('0.01',.1))
STATUS_KEYS=30
# Fade-region witness (docs/architecture/linear-distance-fade-region.md, step 1):
# X3M_FADE_WITNESS=1 samples every fixture frame. The fixture has no seam scope,
# so its derived rectangles are the full viewport; X3M_FIXTURE_FADE_RECT (seam
# only) injects a rectangle instead. The positive rectangle contains both fade
# footprints ([8,40) and [24,56) by [16,48)); the control excludes the second
# footprint (a prepared fade source at plan index >= 1), so those sampled
# frames must report 16x32 covered pixels outside the union and the validator
# must fail for exactly that reason.
WITNESS_K=1
WITNESS_RECT=(8,16,56,48)
WITNESS_CONTROL_RECT=(8,16,40,48)
WITNESS_CONTROL_VIOLATIONS={16:512,19:512,21:512,23:512,27:512,31:512}
WITNESS_REASONS=('sampled','no_pass','no_fade','emission','mask_invalid')
WITNESS_BUCKETS=('f<=0.01','f<=0.02','f<=0.05','f<=0.1','f<=0.25','f<=0.5','f<1','f=1')
SCOPE=('Actual capture DIP / MotionOutput / shared additive-and-fade composition pool / '
       'Hdr owning exchange / supplemental TemporalPass. Six exact Asteroid pairs, the station '
       'BUMPMAP pair with its native opaque sibling, one existing additive pair, and two bootstrap '
       'programs; no broad detached equation rerun.')
LIMITATIONS=[
    'A fixture-only scene-owner admission seam replaces game owner-memory binding; actual capture, render-state admission, original DIP, HDR exchange, supplemental TAA and terminal publication remain exercised.',
    'The seven bounded linear-color witnesses reuse the detached Asteroid/standard BUMPMAP oracle and fixed observed X3 FP16 render-target round-toward-zero model. This store rule is not a native-Windows guarantee.',
    'The station opaque sibling and overwrite draws are native user-memory DIPs (gate 4 refuses them): the fixture RT1/RT2 oracle has no record for a routed sibling. Their rows, constants and textures are the station source\'s.',
    'Current/previous reactive coverage rejects invalid blended history; it does not implement layered transparent temporal accumulation or establish a shimmer fix.',
    'Real invalid-index-buffer sources establish exact native failure and no completed history. A failure before enhancement recovers next frame; earlier enhancement followed by rejected publication explicitly quarantines composition through Reset. Partial driver submission retains existing host/component evidence.',
    'Timing toggles only fade, with the same material/motion/TAA/emission settings. EVENT-fenced source and terminal windows exclude setup, the frame-level M clear, and readbacks; paired processes identify order effects but are not GPU timestamps or game FPS.',
    'The fixture has no seam scope: its derived rectangles are the full viewport and the in-place region is the source\'s quarter-viewport application scissor; the smaller timing fractions come from the seam-only rectangle injection, not from the bound table, and the source raster is the same at every fraction.',
    'Native Windows, installation, gameplay appearance and docking-port distance-transition causality remain unverified.',
]


def fields(line):
    return dict(re.findall(r'(\w+)=([^\s]+)',line))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def rgba(value):
    result=tuple(map(float,value.split(',')))
    assert len(result)==4 and all(math.isfinite(x) for x in result)
    return result


def sample_case(pair,zero=False):
    if pair==STATION_PAIR:
        # Detached case 61 (AlphaValue .625) with the live fixture's EnableGlow .25.
        case=copy.deepcopy(component.cases()[61]);case.update(glow=.25)
    else:case=copy.deepcopy(component.cases()[10*pair])
    case.update(pair=component.STATION_PAIR if pair==STATION_PAIR else 110+pair,fp16=0,gains=[1.,1.,1.],flags=material.FOG)
    if zero:case['diffuse'][3]=0.
    return case


def fade_alpha(pair):
    return STATION_ALPHA if pair==STATION_PAIR else .078125


def expected_composite(before,pair,zero=False,overlap=1):
    c=sample_case(pair,zero)
    linear=material.expected(component.oracle_case(c)).linear_rgb
    # The live Asteroid originals retain material alpha .625; the detached source
    # fixture deliberately overwrote it with one. The station case already carries
    # its AlphaValue. All factors here are binary-exact.
    alpha=(1. if pair==STATION_PAIR else .625)*component.source_alpha(c)
    assert alpha==fade_alpha(pair) or zero
    q=0.;energy=[0.]*3
    for _ in range(overlap):
        q=component.fp16_rt_store(alpha+(1-alpha)*q)
        energy=[component.fp16_rt_store(alpha*x+(1-alpha)*old) for x,old in zip(linear,energy)]
    return component.compose(before,energy,q)


def native_baselines(rows):
    assert [int(row['pair']) for row in rows]==list(range(len(FADE_PAIRS)))
    result={}
    for row in rows:
        before,after=rgba(row['before']),rgba(row['after'])
        assert before==(1.,1.,1.,1.) and after[3]==1.
        result[int(row['pair'])]=after
    return result


def expected_emission(before,enabled):
    if enabled:
        energy=[component.fp16_rt_store(x**2.2*.5) for x in (.5,.25,.125)]
        rgb=[component.fp16_rt_store((max(before[i],0)**2.2+energy[i])**(1/2.2)) for i in range(3)]
    else:rgb=[component.fp16_rt_store(a+.5*b) for a,b in zip(before,(.5,.25,.125))]
    return tuple(rgb)+(before[3]+.125,)


def validate_samples(rows,fade,emission,native):
    seen=set();maximum=0.;previous={}
    singles=(1,*PRESENT_FRAMES)
    expected_keys={(f,0,x,32) for f in singles for x in (16,32)}
    expected_keys|={(f,i,32,32) for f in STATION_FRAMES+MIXED_FRAMES for i in range(len(source_plan(f)))}
    for row in rows:
        frame,source,x,y=(int(row[k]) for k in ('frame','source','x','y'))
        key=(frame,source,x,y)
        assert key not in seen and key in expected_keys
        seen.add(key)
        kind,pair,_,_=source_plan(frame)[source]
        overlap=2 if frame==18 else 1
        assert int(row['pair'])==pair and row['kind']==kind and int(row['overlap'])==overlap
        assert float(row['alpha'])==(.125 if kind=='emission' else 0. if frame==1 else fade_alpha(pair))
        before,after=rgba(row['before']),rgba(row['after'])
        # Frame 14 composes over the native opaque sibling, never the unit background.
        if source==0 and frame==14:assert before[:3]!=(1.,1.,1.) and before[3]!=1.,(key,'station sibling drawn first')
        elif source==0:assert before[:3]==(1.,1.,1.),(key,'known ordinary background')
        else:assert before==previous[frame],(key,'ordered original source chain')
        previous[frame]=after
        if kind=='fade':assert before[3]==after[3],(key,'native target alpha')
        if frame==1:
            assert all(component.same_float(a,b) for a,b in zip(before,after)),(key,'zero-alpha raw A')
            continue
        if kind=='emission':wanted=expected_emission(before,emission)
        elif fade:wanted=expected_composite(before,pair,overlap=overlap)
        elif frame in PRESENT_FRAMES:
            assert all(component.same_float(a,b) for a,b in zip(after[:3],native[pair][:3])),(key,'native original-program FP16 twin')
            continue
        else:
            # Only the native mixed/overlap recurrence uses this calibrated
            # original shader result. Linear L is always the independent CPU
            # oracle. One FP16 calibration store is covered by RGB tolerance.
            alpha=fade_alpha(pair)
            original=[(v-(1-alpha))/alpha for v in native[pair][:3]]
            wanted=before
            for _ in range(overlap):wanted=tuple(component.fp16_rt_store(alpha*v+(1-alpha)*a) for a,v in zip(wanted,original))+(before[3],)
        for a,b in zip(after[:3],wanted[:3]):
            fraction=abs(a-b)/(.006*abs(b)+.00002)
            maximum=max(maximum,fraction)
            assert fraction<=1,(key,'actual source policy/order',a,b,fraction)
    assert seen==expected_keys,'zero, seven independent pair witnesses, station orderings, and overlap/mixed order'
    return dict(samples=len(seen),max_tolerance_fraction=maximum)


def source_plan(frame):
    """Producer submissions; ordinary background/opaque draws are separate."""
    if frame==1:return [('fade',0,0,False)]
    if frame in PRESENT_FRAMES:return [('fade',(frame-2)//2,0,False)]
    # Station frames 14-17: after the native opaque sibling (and the frame's
    # last draw), before the opaque overwrite, after and before an emission source.
    plans={
        14:[('fade',STATION_PAIR,0,False)],15:[('fade',STATION_PAIR,0,False)],
        16:[('emission',0,0,False),('fade',STATION_PAIR,0,False)],
        17:[('fade',STATION_PAIR,0,False),('emission',0,0,False)],
        18:[('fade',0,0,False)],19:[('fade',0,0,False),('fade',0,0,False)],
        20:[('fade',0,0,False),('emission',0,0,False)],
        21:[('emission',0,0,False),('fade',0,0,False)],
        22:[('emission',0,0,False),('fade',0,3,False),('emission',0,0,False)],
        23:[('fade',0,0,False),('emission',0,3,False),('fade',0,0,False)],
        24:[('fade',0,6,False)],
        25:[('fade',0,0,True),('emission',0,0,False)],
        26:[('fade',0,0,False),('emission',0,0,False)],
        27:[('fade',0,0,False),('emission',0,7,False),('fade',0,0,False)],
        28:[('fade',0,0,False),('emission',0,0,False)],
        29:[('fade',0,0,False)],
        30:[('fade',0,0,False),('emission',0,0,False)],
        31:[('emission',0,0,False),('fade',0,0,False)],
        32:[('fade',0,0,False),('emission',0,0,False),('fade',0,0,True)],
    }
    return plans.get(frame,[])


def station_opaque_draws(frame):
    """Opaque station draws of a frame as (kind, rect, routed): frame 14 the
    depth-rejected routed sibling (ordinary converted route, no pixel) and the
    visible native sibling before the source; frame 15 the native overwrite of
    the left half after it."""
    return [('routed_sibling',(8,16,40,48),1),('sibling',(8,16,40,48),0)] if frame==14 else [('overwrite',(8,16,24,48),0)] if frame==15 else []


def routed_station_draws(frame):
    return sum(1 for _,_,routed in station_opaque_draws(frame) if routed)


def native_station_draws(frame):
    return sum(1 for _,_,routed in station_opaque_draws(frame) if not routed)


def validate_station(output,trace,fade,emission=1):
    """FADE_STATION lines (native sibling/overwrite confined to their scissor) and,
    with fade on, the per-frame composition refusal histogram of the station
    frames: every station source is eligible and prepared (admitted), no
    readiness/frame-stop/preparation refusal, and the pair (non-producer)
    refusals are the frame's baseline (frame 18: one Asteroid source, nothing
    else) plus the native opaque station draws plus, with emission off, the
    frame's emission sources."""
    rows=[fields(line) for line in output.splitlines() if line.startswith('FADE_STATION ')]
    assert [(int(r['frame']),r['kind'],tuple(map(int,r['rect'].split(','))),int(r['routed'])) for r in rows]==[(f,k,rect,routed) for f in STATION_FRAMES for k,rect,routed in station_opaque_draws(f)],'station opaque draws'
    samples={(int(r['frame']),int(r['source'])):r for r in (fields(line) for line in output.splitlines() if line.startswith('FADE_SAMPLE '))}
    for r in rows:
        assert int(r['native'])==1 and int(r['hr'],16)==0 and int(r['outside_changed'])==0,(r['frame'],'opaque station draw confined to its scissor')
        assert (int(r['survivor_x']),int(r['survivor_y']))==STATION_SURVIVOR
        if int(r['routed']):assert int(r['changed'])==0 and int(r['survivor_exact'])==1,(r['frame'],'routed opaque sibling is depth-rejected everywhere')
        else:assert int(r['changed'])>0,(r['frame'],'visible opaque station draw')
        if r['kind']=='overwrite':
            # The composed pixel outside the overwrite survives bit for bit: the
            # oracle-checked FADE_SAMPLE value of the frame's station source.
            assert int(r['survivor_exact'])==1 and rgba(r['survivor'])==rgba(samples[(int(r['frame']),0)]['after']),(r['frame'],'composed station pixel survives the opaque overwrite')
    result=dict(opaque_draws=[dict(frame=int(r['frame']),kind=r['kind'],rect=r['rect'],routed=int(r['routed']),changed=int(r['changed']),survivor_exact=int(r['survivor_exact'])) for r in rows])
    # The routed sibling takes the ordinary converted route: against frame 13
    # (the ordinary object alone) the frame routes one more draw at gate 5 (the
    # seam's unknown scope) and the linear material route converts one more
    # BUMP draw; the native siblings and the blended sources stop at gate 4.
    # No capture frame records a refused station rectangle.
    motion=indexed(trace.splitlines(),'motion_output_frame ','frame');material=indexed(trace.splitlines(),'linear_material_frame ','frame')
    base=motion[13];base_material=material[13]
    for frame in STATION_FRAMES:
        routed=routed_station_draws(frame);native=native_station_draws(frame);sources=sum(s[0]=='fade' for s in source_plan(frame)) # emission sources are not a motion pair (gate 3)
        row=motion[frame];mat=material[frame]
        assert int(row['routed'])==int(base['routed'])+routed and int(row['gate5'])==int(base['gate5'])+routed,(frame,'routed opaque station sibling through the ordinary route',row['routed'],row['gate5'])
        assert int(row['gate4'])==int(base['gate4'])+native+sources,(frame,'native siblings and blended sources refused at gate 4',row['gate4'])
        assert int(mat['routed'])==int(base_material['routed'])+routed and int(mat['bump_routed'])==int(base_material['bump_routed'])+routed and int(mat['refused'])==int(base_material['refused']),(frame,'routed sibling converted as an ordinary BUMP material draw',mat)
    assert not any(line.startswith('fade_refused_rect ') for line in trace.splitlines()),'admitted station sources never record a refused rectangle'
    if not fade:return result
    frames=indexed(trace.splitlines(),'linear_composition_frame ','frame')
    refusals=indexed(trace.splitlines(),'linear_composition_refusals ','frame')
    histogram={}
    for frame in STATION_FRAMES:
        sources=[s for s in source_plan(frame) if s[0]=='fade']
        row=frames[frame];ref=refusals[frame]
        assert int(row['fade_eligible'])==int(row['fade_prepared'])==int(row['fade_linear'])==len(sources),(frame,'station source admitted and composed',row)
        assert int(ref['readiness'])==int(ref['frame_stop'])==int(ref['preparation'])==0,(frame,'station refusal histogram',ref)
        histogram[frame]={k:int(ref[k]) for k in ('pair','permission_scene','readiness','readers','frame_stop','preparation')}
    baseline=int(refusals[18]['pair'])
    for frame in STATION_FRAMES:
        extra=native_station_draws(frame)+(0 if emission else sum(s[0]=='emission' for s in source_plan(frame)))
        assert histogram[frame]['pair']==baseline+extra,(frame,'only the native opaque station draws (and unrequested emission sources) are refused as non-producer pairs',histogram[frame]['pair'],baseline,extra)
    result.update(refusal_histogram=histogram,pair_refusal_baseline=baseline,admitted_sources=sum(len([s for s in source_plan(f) if s[0]=='fade']) for f in STATION_FRAMES))
    return result


def expected_sources(frame,fade,emission):
    stopped=False;scene_failed=False;result=[]
    for kind,pair,fault,failed in source_plan(frame):
        active=bool(fade if kind=='fade' else emission)
        applied_fault=fault if active else 0
        prepared=active and not stopped and not scene_failed and applied_fault!=3
        linear=prepared and not (failed or applied_fault in (6,7))
        # A composite fault after a successful source certifies the exchanged
        # native B for emission; the in-place fade recovers A|R from B|R
        # instead and is Incomplete (blocked), never Native.
        native=prepared and applied_fault==6 and not failed and kind=='emission'
        incomplete=prepared and (failed or applied_fault==7 or (applied_fault==6 and kind=='fade'))
        if active and (applied_fault==3 or incomplete):stopped=True
        scene_failed=scene_failed or failed
        result.append(dict(kind=kind,pair=pair,fault=applied_fault,hr=0x8876086c if failed else 0,
                           original_calls=1,prepared=int(prepared),linear=int(linear),native=int(native),
                           incomplete=int(incomplete),mask_valid=bool((fade or emission) and not stopped)))
    return result,stopped


def source_scissor(index,width=64,height=64):
    """Application scissor of the fixture's index-th producer source."""
    return ((3 if index else 1)*width//8,height//4,(7 if index else 5)*width//8,3*height//4)


def rect_area(rect):
    return max(0,rect[2]-rect[0])*max(0,rect[3]-rect[1])


def intersect(a,b):
    return (max(a[0],b[0]),max(a[1],b[1]),min(a[2],b[2]),min(a[3],b[3]))


def expected_region_pixels(frame,fade,emission,rect=None,width=64,height=64):
    """Sum of the rectangles the in-place brackets of one frame actually back
    up and compose: the derived (or injected) rectangle intersected with the
    source's application scissor, over every prepared fade source."""
    sources,_=expected_sources(frame,fade,emission)
    full=(0,0,width,height)
    return sum(rect_area(intersect(rect or full,source_scissor(i,width,height))) for i,s in enumerate(sources) if s['kind']=='fade' and s['prepared'])


class WitnessViolation(AssertionError):
    """Covered M pixels outside the union of the frame's derived rectangles."""
    def __init__(self,violations):
        super().__init__(f'covered M pixels outside the union of the derived fade rectangles: {violations}')
        self.violations=dict(violations)


def expected_witness(frame,fade,emission):
    """Witness verdict of one fixture frame: derived rectangles (admitted fade
    sources reach derive before prepare), prepared counts and the skip reason."""
    sources,stopped=expected_sources(frame,fade,emission)
    derived=0;stop=False;failed=False
    for (kind,_,_,fail),s in zip(source_plan(frame),sources):
        active=bool(fade if kind=='fade' else emission)
        if kind=='fade' and active and not stop and not failed:derived+=1
        if active and (s['fault']==3 or s['incomplete']):stop=True
        failed=failed or fail
    fade_prepared=sum(s['prepared'] for s in sources if s['kind']=='fade')
    emission_prepared=sum(s['prepared'] for s in sources if s['kind']=='emission')
    if not derived:reason='no_fade'
    elif emission_prepared:reason='emission'
    elif stopped:reason='mask_invalid'
    else:reason='sampled'
    return dict(reason=reason,rects=derived,fade_prepared=fade_prepared,emission_prepared=emission_prepared)


def validate_witness(trace,fade,emission,k=WITNESS_K,rect=None):
    """fade_witness/fade_region session-log lines: every sampled frame has zero
    covered pixels outside the union; reports the f histogram and the VB
    revision distribution. Raises WitnessViolation (an AssertionError) with the
    per-frame outside counts when the union is violated."""
    lines=trace.splitlines()
    rows=indexed(lines,'fade_witness ','frame')
    assert set(rows)=={f for f in range(FRAMES) if f%k==0},'one fade_witness line per k-th frame'
    regions={}
    for line in lines:
        if line.startswith('fade_region '):
            r=fields(line);regions.setdefault(int(r['frame']),[]).append(r)
    sampled=[];skipped={};hist=[0]*8;covered=union=0;violations=[];revisions={};overflow_frames=[]
    for frame,row in sorted(rows.items()):
        wanted=expected_witness(frame,fade,emission)
        assert row['reason'] in WITNESS_REASONS and row['reason']==wanted['reason'],(frame,row['reason'],wanted['reason'])
        assert int(row['k'])==k and int(row['sampled'])==int(row['reason']=='sampled')
        assert int(row['rects'])==wanted['rects'],(frame,'derived rectangles',row['rects'],wanted['rects'])
        assert int(row['rects_prepared'])==int(row['fade_prepared'])==wanted['fade_prepared'],(frame,'prepared rectangles')
        assert int(row['rects_unprepared'])==wanted['rects']-wanted['fade_prepared'],(frame,'unprepared rectangles')
        assert int(row['emission_prepared'])==wanted['emission_prepared'],(frame,'emission prepared')
        logged=len(regions.get(frame,[]));truncated=int(row['lines_truncated'])
        assert logged+truncated==wanted['rects'] and truncated==max(0,wanted['rects']-64),(frame,'per-DIP lines',logged,truncated)
        if int(row['overflow']):overflow_frames.append(frame)
        h=list(map(int,row['f_hist'].split(',')))
        assert len(h)==8 and sum(h)==wanted['rects'],(frame,'f histogram')
        for r in regions[frame] if frame in regions else ():
            l,t,rr,b=map(int,r['rect'].split(','))
            assert 0<=l<rr and 0<=t<b,(frame,'rectangle')
            if rect is not None:assert (l,t,rr,b)==rect and int(r['bound'])==1 and int(r['reason'])==0,(frame,'synthetic rectangle',r['rect'])
            revisions.setdefault(r['vb'],{});revisions[r['vb']][r['vb_rev']]=revisions[r['vb']].get(r['vb_rev'],0)+1
        if row['reason']!='sampled':
            skipped[row['reason']]=skipped.get(row['reason'],0)+1;continue
        assert row['result']=='00000000',(frame,'witness readback',row['result'])
        assert int(row['width'])==64 and int(row['height'])==64
        c,o,u=(int(row[key]) for key in ('covered','outside','union'))
        assert c>0 and u>=c-o,(frame,'sampled frame coverage')
        if int(row['overflow']):assert u==64*64,(frame,'an overflowing frame takes the whole target as its union')
        elif rect is not None:assert u==(rect[2]-rect[0])*(rect[3]-rect[1]),(frame,'union of the synthetic rectangle')
        hist=[a+b for a,b in zip(hist,h)]
        covered+=c;union+=u;sampled.append(frame)
        if o:violations.append((frame,o))
    if violations:raise WitnessViolation(violations)
    return dict(k=k,sampled_frames=sampled,skipped=skipped,overflow_frames=overflow_frames,covered_pixels=covered,union_area=union,outside_pixels=0,
                f_histogram=dict(zip(WITNESS_BUCKETS,hist)),region_lines=sum(len(v) for v in regions.values()),revisions_per_vb=revisions)


def compare_witness(cases):
    """The witness readback is read-only: the full and the containing rectangle
    reproduce the fade-on/emission-off baseline images and counters bit for
    bit. The control rectangle excludes the second footprint, which the
    in-place bracket therefore leaves native in A: alpha, M and the ordinary
    attachments are unchanged, the composed color differs on exactly the
    frames with covered pixels outside the union (and, through the TAA
    history, may differ afterwards), never before the first violation."""
    baseline=cases['lazy1-fade1-emission0']
    for name in ('witness-full','witness-rect'):
        for key in ('temporal_sha256','alpha_sha256','covered_pixels','frames_detail'):
            assert cases[name][key]==baseline[key],(name,'witness changed the composed result',key)
    control=cases['witness-control']
    for key in ('alpha_sha256','covered_pixels','frames_detail'):
        assert control[key]==baseline[key],('witness-control','native strip changed alpha, coverage or counters',key)
    first=min(WITNESS_CONTROL_VIOLATIONS)
    for frame,(a,b) in enumerate(zip(baseline['temporal_sha256'],control['temporal_sha256'])):
        if frame<first:assert a==b,(frame,'control differs before its first violation')
        elif frame in WITNESS_CONTROL_VIOLATIONS:assert a!=b,(frame,'the excluded footprint must stay native in A')


def indexed(lines,prefix,key):
    rows=[fields(line) for line in lines if line.startswith(prefix)]
    assert len(rows)==len({int(r[key]) for r in rows}),(prefix,'duplicate evidence')
    return {int(r[key]):r for r in rows}


def validate_functional(output,trace,fade,emission,lazy,rect=None):
    lines=output.splitlines();traces=trace.splitlines()
    terminal=[fields(line) for line in lines if line.startswith('RESULT PASS ')]
    assert len(terminal)==1 and not any(line.startswith('RESULT FAIL') for line in lines)
    summary=terminal[0]
    assert int(summary['frames'])==FRAMES and int(summary['checks'])>0 and int(summary['restorations'])>0
    assert int(summary['taa_reference_frames'])==FRAMES-len(FAILED_SOURCES) and int(summary['taa_skipped_frames'])==len(FAILED_SOURCES)
    live=indexed(lines,'FADE_LIVE ','frame')
    assert set(live)==set(range(FRAMES))
    source_rows=[fields(line) for line in lines if line.startswith('FADE_SOURCE ')]
    assert [(int(r['frame']),int(r['source'])) for r in source_rows]==[(f,i) for f in range(FRAMES) for i in range(len(source_plan(f)))]
    by_frame={f:[] for f in range(FRAMES)}
    for row in source_rows:by_frame[int(row['frame'])].append(row)
    required=int(emission)+2*int(fade);total_sources=0
    details=[]
    for frame,row in live.items():
        assert int(row['fade'])==fade and int(row['emission'])==emission
        status=[int(row[f's{i}']) for i in range(STATUS_KEYS)]
        expected,stopped=expected_sources(frame,fade,emission)
        assert status[0]==bool(required) and status[1]==bool(required and not stopped)
        assert status[16]==required,'required producers are the producer bits only'
        assert status[18]==status[19]==required|(IN_PLACE_POLICY*bool(fade)),'fade attaches the in-place policy beside the exchange fade'
        assert status[20]==(7+bin(required).count('1') if required else 0)
        assert status[21]==(4 if required else 0), 'one shared four-target pool per attach'
        assert status[2]==status[3]==status[9]==0,'recoverable controls must not retain lost/quarantined/suppressed state'
        assert status[4]==sum(s['prepared'] for s in expected)
        assert status[5]==sum(s['linear'] for s in expected)
        assert status[6]==sum(s['native'] for s in expected)
        assert status[7]==sum(s['incomplete'] for s in expected)
        assert status[27]==status[14],'every prepared fade source composes in place'
        assert status[10]==status[4]-status[27],'exchanged controls are the emission ones; in-place fade never exchanges'
        assert status[28]==status[15],'in-place Linear completions are the fade linear ones'
        assert status[29]==expected_region_pixels(frame,fade,emission,rect),(frame,'region pixels actually composed',status[29])
        # Key 12 counts every submitted indexed DIP: the sources plus frame 14's
        # routed (indexed) opaque sibling; the user-memory siblings are not DIPs.
        assert status[12]==len(expected)+routed_station_draws(frame) and int(row['draws'])==len(expected),'each original source DIP is submitted once'
        assert status[13]==sum(s['kind']=='fade' for s in expected)*bool(fade)
        completed=[s['hr'] for s in expected if s['prepared']]
        assert status[11]==(completed[-1] if completed else 1),'last composed source HRESULT; S_FALSE before any prepared source'
        assert status[14]==sum(s['prepared'] for s in expected if s['kind']=='fade')
        assert status[15]==sum(s['linear'] for s in expected if s['kind']=='fade')
        if required:assert status[17]==stopped
        prior_mask=bool(required)
        for source,(actual,wanted) in enumerate(zip(by_frame[frame],expected)):
            assert actual['kind']==wanted['kind'] and int(actual['pair'])==wanted['pair']
            assert int(actual['fault'])==wanted['fault'] and int(actual['hr'],16)==wanted['hr']
            assert int(actual['mask_before'])==prior_mask and int(actual['mask_after'])==wanted['mask_valid']
            prior_mask=wanted['mask_valid']
            assert int(actual['overlap'])==(2 if frame==18 else 1)
            alpha=(.25 if source>=2 else .125) if wanted['kind']=='emission' else (0. if frame==1 else fade_alpha(wanted['pair']))
            assert float(actual['alpha'])==alpha
            for key in ('original_calls','prepared','linear','native'):
                assert int(actual[key])==wanted[key],(frame,source,key,actual[key],wanted[key])
            if not wanted['prepared']:assert actual['hash_mask_before']==actual['hash_mask_after'],(frame,source,'unprepared source changed shared M bytes')
        total_sources+=len(expected)
        details.append(dict(mask_valid=bool(status[1]),prepared=status[4],linear=status[5],native=status[6],incomplete=status[7],
                            alpha=row['hash_alpha'],motion=row['hash_motion'],depth=row['hash_depth'],mask=row['hash_mask']))
    geometry=indexed(lines,'FADE_GEOMETRY ','frame')
    assert set(geometry)==set(range(FRAMES))
    assert all(float(row['ordinary_t'])==.03125*(f%3) for f,row in geometry.items())
    cameras=[fields(line) for line in lines if line.startswith('FADE_CAMERA ')]
    assert {int(row['frame']) for row in cameras}==set(range(FRAMES))
    translations=[tuple(map(float,row['view_translation'].split(','))) for row in cameras]
    assert all(len(v)==3 and all(math.isfinite(x) for x in v) for v in translations) and len(set(translations))>2
    assert [fields(line) for line in lines if line.startswith('FADE_OPAQUE_RETURN ')]==[dict(frame='13',matched='1')]
    assert [fields(line) for line in lines if line.startswith('FADE_REJECTED ')]==[dict(frame=str(f),source_failed='1',taa='0',history_seeded='0',copy_exact='1') for f in FAILED_SOURCES]
    assert [fields(line) for line in lines if line.startswith('FADE_EXPORT ')]==[dict(frame=str(RESET_FRAMES[1]),quarantine=str(int(bool(required))),state_lost='0')]
    checks=[fields(line) for line in lines if line.startswith('FADE_CHECKS ')]
    assert len(checks)==1 and int(checks[0]['frames'])==FRAMES and int(checks[0]['submissions'])==total_sources
    assert checks[0]['qualified']=='1' and checks[0]['benchmark']=='0'
    assert sum(line=='RESET PASS' for line in lines)==2
    resets=[fields(line) for line in lines if line.startswith('FADE_RESET ')]
    assert [int(row['frame']) for row in resets]==list(RESET_FRAMES)
    for row in resets:
        assert int(row['refs'])==(3+bin(required).count('1') if required else 0)
        assert int(row['allocations'])==(4 if required else 0)
        assert int(row['quarantine'])==int(bool(required) and int(row['frame'])==RESET_FRAMES[1])
        assert int(row['state_lost'])==0
    releases=[fields(line) for line in traces if line.startswith('motion_output_release ')]
    assert len(releases)==1 and int(releases[0]['released'])==1
    motion=indexed(traces,'motion_output_frame ','frame')
    assert set(motion)==set(range(FRAMES))
    for frame,row in motion.items():
        assert row['rt_mode']==('lazy' if lazy else 'perdraw')
        assert int(row['apply_failures'])==int(row['restore_failures'])==0
        assert int(row['taa_resolved'])==int(frame not in FAILED_SOURCES)
        if frame in FAILED_SOURCES:assert int(row['taa_history'])==0
    temporal=indexed(traces,'motion_output_taa_readback ','frame')
    assert set(temporal)==set(range(FRAMES))-set(FAILED_SOURCES)
    assert all(row['result']=='00000000' for row in temporal.values())
    native=native_baselines([fields(line) for line in lines if line.startswith('FADE_NATIVE ')])
    sample_result=validate_samples([fields(line) for line in lines if line.startswith('FADE_SAMPLE ')],fade,emission,native)
    station=validate_station(output,trace,fade,emission)
    return dict(frames=FRAMES,checks=int(summary['checks']),restorations=int(summary['restorations']),
                sources=total_sources,held_references=int(releases[0]['held']),frames_detail=details,station=station,**sample_result)


def validate_pixels(work,fade,emission):
    temporal=[];alpha_hashes=[];masks=[]
    for frame in range(FRAMES):
        actual_path=work/'x3-modern-captures'/f'taa_1_{frame}.rgba16f'
        reference_path=work/f'reference_taa_{frame}.rgba16f'
        if frame in FAILED_SOURCES:
            assert not actual_path.exists() and not reference_path.exists(),'failed native source must not publish or seed TAA'
            temporal.append(None)
        else:
            actual=actual_path.read_bytes();reference=reference_path.read_bytes()
            assert len(actual)==64*64*8 and actual==reference,(frame,'actual supplemental TAA differs from independent reference')
            temporal.append(hashlib.sha256(actual).hexdigest())
        color=(work/f'distance_fade_color_{frame}.rgba32f').read_bytes()
        mask=(work/f'distance_fade_mask_{frame}.rgba32f').read_bytes()
        assert len(color)==len(mask)==64*64*16
        colors=list(struct.iter_unpack('<4f',color));values=list(struct.iter_unpack('<4f',mask))
        assert all(math.isfinite(v) for p in colors for v in p)
        assert all(math.isfinite(v) and v>=0 for p in values for v in p[:3])
        expected=[False]*(64*64)
        sources,_=expected_sources(frame,fade,emission)
        for source,s in enumerate(sources):
            if not s['prepared'] or s['hr']:continue
            left,right=(24,56) if source else (8,40)
            for y in range(16,48):
                for x in range(left,right):expected[y*64+x]=True
        for index,(pixel,wanted) in enumerate(zip(values,expected)):
            assert all((v>0)==wanted for v in pixel[:3]),(frame,index,'actual shared M footprint, including invalid-but-retained bytes')
        masks.append(sum(expected))
        alpha_hashes.append(hashlib.sha256(b''.join(struct.pack('<f',p[3]) for p in colors)).hexdigest())
    return dict(temporal_sha256=temporal,alpha_sha256=alpha_hashes,covered_pixels=masks)


def validate_admission(output,trace,taa,hdr):
    lines=output.splitlines()
    terminal=[fields(line) for line in lines if line.startswith('RESULT PASS ')]
    assert len(terminal)==1 and not any(line.startswith('RESULT FAIL') for line in lines)
    assert int(terminal[0]['frames'])==4 and int(terminal[0]['checks'])>0
    live=indexed(lines,'FADE_LIVE ','frame');assert set(live)==set(range(4))
    for frame,row in live.items():
        status=[int(row[f's{i}']) for i in range(22)]
        assert int(row['fade'])==1 and int(row['emission'])==1
        assert all(status[k]==0 for k in (0,1,4,5,6,7,9,10,14,15,16,18,19,20,21)),(frame,'missing prerequisite must not enhance or claim supplemental availability')
        assert status[12]==int(frame!=0)
    source=[fields(line) for line in lines if line.startswith('FADE_SOURCE ')]
    assert [(int(r['frame']),int(r['source'])) for r in source]==[(f,0) for f in (1,2,3)]
    assert all(int(r['hr'],16)==0 and int(r['original_calls'])==1 and int(r['prepared'])==int(r['linear'])==0 for r in source)
    releases=[fields(line) for line in trace.splitlines() if line.startswith('motion_output_release ')]
    assert len(releases)==1 and int(releases[0]['released'])==1
    return dict(frames=4,checks=int(terminal[0]['checks']),taa=taa,hdr=hdr,sources=3,held_references=int(releases[0]['held']))


def timing_rect(width,height,side):
    """Injected rectangle of side fraction `side`, centred on the timed
    source's footprint and inside its application scissor."""
    if side is None:return None
    w=round(width*side);h=round(height*side);cx=3*width//8;cy=height//2
    rect=(cx-w//2,cy-h//2,cx-w//2+w,cy-h//2+h)
    assert intersect(rect,source_scissor(0,width,height))==rect
    return rect


def timing_count(frame):
    return 1 if frame<6 else 4 if frame<12 else 16


def validate_timing(output,trace,fade,width,height,rect=None,pair=0):
    lines=output.splitlines()
    terminal=[fields(line) for line in lines if line.startswith('RESULT PASS ')]
    assert len(terminal)==1 and not any(line.startswith('RESULT FAIL') for line in lines)
    assert int(terminal[0]['frames'])==18
    frames=indexed(trace.splitlines(),'linear_composition_frame ','frame')
    region=rect_area(intersect(rect or (0,0,width,height),source_scissor(0,width,height)))
    if fade:
        assert set(frames)==set(range(18)),'one composition frame line per timed frame'
        for frame,row in frames.items():
            count=timing_count(frame)
            assert int(row['prepared'])==int(row['in_place'])==int(row['in_place_linear'])==int(row['linear'])==count,(frame,'every timed source composes in place')
            assert int(row['in_place_incomplete'])==int(row['incomplete'])==int(row['native'])==0
            assert int(row['region_pixels'])==count*region,(frame,'region pixels',row['region_pixels'],count*region)
    else:assert not frames,'no composition producer requested'
    rows=[fields(line) for line in lines if line.startswith('FADE_TIMING ')]
    assert [(int(r['count']),int(r['sample'])) for r in rows]==[(c,s) for c in COUNTS for s in range(4)]
    result={}
    for row in rows:
        count=int(row['count'])
        assert int(row['width'])==width and int(row['height'])==height and int(row['fade'])==fade and int(row['emission'])==0 and int(row['pair'])==pair
        values={k:float(row[k+'_ms']) for k in ('source','terminal','total')}
        assert all(math.isfinite(v) and v>=0 for v in values.values())
        assert abs(values['total']-values['source']-values['terminal'])<=3e-9,'full cost must share the two fenced timestamps'
        result.setdefault(str(count),[]).append(values)
    checks=[fields(line) for line in lines if line.startswith('FADE_CHECKS ')]
    assert len(checks)==1 and int(checks[0]['frames'])==18 and int(checks[0]['benchmark'])==1
    assert int(checks[0]['submissions'])==6*sum(COUNTS)
    return dict(frames=18,warmups_per_count=2,samples_per_count=4,counts=result,rect=rect,pair=pair,
                region_pixels_per_bracket=region if fade else 0,region_fraction=region/(width*height) if fade else 0.)


def compare_functional(cases):
    both=cases['lazy1-fade1-emission1'];perdraw=cases['lazy0-fade1-emission1']
    for key in ('temporal_sha256','alpha_sha256','covered_pixels','frames_detail'):
        assert both[key]==perdraw[key],('lazy attachment mode changed result',key)
    for emission in (0,1):
        off,on=(cases[f'lazy1-fade{fade}-emission{emission}'] for fade in (0,1))
        assert off['alpha_sha256']==on['alpha_sha256'],'fade changed native target alpha'
        for a,b in zip(off['frames_detail'],on['frames_detail']):
            assert a['motion']==b['motion'] and a['depth']==b['depth'],'fade changed ordinary temporal attachments'


def timing_name(width,height,pair,fade,fraction='1',producer=0):
    return f'timing-{width}x{height}-'+('station-' if producer else '')+f'pair{pair}-fade{fade}'+('' if fraction=='1' else f'-f{fraction}')


def paired_cost(cases):
    result=[]
    for width,height in RESOLUTIONS:
        for producer,fractions in ((0,TIMING_FRACTIONS),(STATION_PAIR,TIMING_FRACTIONS[:1])):
            for fraction,_ in fractions:
                for count in COUNTS:
                    pairs=[]
                    for pair in (0,1):
                        off=cases[timing_name(width,height,pair,0,producer=producer)]['counts'][str(count)]
                        on_case=cases[timing_name(width,height,pair,1,fraction,producer)];on=on_case['counts'][str(count)]
                        deltas={k:[b[k]-a[k] for a,b in zip(off,on)] for k in ('source','terminal','total')}
                        pairs.append(dict(order='off/on' if pair==0 else 'on/off',
                                          off_median_ms={k:statistics.median(r[k] for r in off) for k in deltas},
                                          on_median_ms={k:statistics.median(r[k] for r in on) for k in deltas},
                                          paired_window_median_delta_ms={k:statistics.median(v) for k,v in deltas.items()}))
                    result.append(dict(width=width,height=height,producer='station' if producer else 'asteroid',requested_fraction=fraction,rect=on_case['rect'],
                                       region_fraction=on_case['region_fraction'],region_pixels_per_bracket=on_case['region_pixels_per_bracket'],
                                       ordered_dips=count,pairs=pairs))
    return result


def reusable_native(directory,hashes):
    """Reuse only the retained, unchanged first native process after revalidation."""
    directory=directory.resolve()
    assert directory.name=='lazy1-fade0-emission0','reuse is scoped to the native baseline only'
    prior=json.loads((directory.parent/'failed-result.json').read_text())
    assert prior['passed'] is False and Path(prior['raw']).resolve()==directory.parent,'marker must describe this stopped run'
    assert prior['inputs']==hashes,'retained native process must have the identical frozen fixture/DLL/program inputs'
    for suffix,name in (('.exe','fixture.exe'),('.dll','d3d9.dll')):
        expected=[value for path,value in hashes.items() if Path(path).suffix.lower()==suffix]
        assert len(expected)==1 and sha(directory/name)==expected[0],'retained executed binary differs from the frozen input'
    assert (directory/'stdout.txt').is_file()
    return directory


# ---- Step C: packed screen emission (docs/architecture/screen-emission-region.md) ----
# The `screenemission` fixture mode draws the row-19 bullet pair non-indexed
# from a DISCARD-locked dynamic buffer (the writer's layout) in the native
# screen state, mixed with the Asteroid pair-0 fade source and the additive
# emission source, through the same proxy seam DLL; `--screen-emission`
# selects these runs and writes screen-emission-live1.json.
SCREEN_PAIR=('5e484a06672e28fb','ec1f5c4a2f4e1445')
SCREEN_PROGRAMS=tuple(dict.fromkeys(BOOTSTRAP+(f'vs_{FADE_PAIRS[0][0]}.bin',f'ps_{FADE_PAIRS[0][1]}.bin',f'vs_{EMISSION_PAIR[0]}.bin',f'ps_{EMISSION_PAIR[1]}.bin',f'vs_{SCREEN_PAIR[0]}.bin',f'ps_{SCREEN_PAIR[1]}.bin')))
# Frame -> source kinds (s screen, f fade, e emission); the bullet buffer is
# created before frame 0 and recreated after the Reset of frame 10, so frames
# 1 and 11 are first draws (no scan yet: refused unbounded, native).
# p: PROJECTED on stage 0, g: sRGB on sampler 0 (readiness refusals), d: dither
# on (a different state, pair refusal); all bound, all native.
# Near-plane kinds (screen-emission-region.md, step B; rows x' = x, y' = y,
# z' = .1 (z - 1), w = z, the D3D near plane at w = 1): n a triangle with one
# vertex behind the camera (visible trapezoid NDC (0,.25),(.5,.25),(.75,.375),
# (0,.375)), x the quad with its near edge exactly on the plane, h the quad
# entirely behind (refused BehindNear, nothing rasterised), b a beam from the
# near plane to w = 1000; n, x and b are admitted in the screen state.
# c (frame 21, after the fade source that darkens its rectangle): the step E
# overlap chain, eight copies of the quad shifted 2 px along x from pixel 8
# in ONE DIP with a soft sprite (opaque alpha): the composed rectangle must
# equal the native twin (the off run) within one FP16 code at gain 1 and
# scale with the gain (screen-gain2 run).
SCREEN_PLAN=('','s','s','es','sf','fs','esf','s','s','s','s','s','s','se','p','g','d','n','x','h','b','fc')
SCREEN_KINDS='spgdnxhbc'
SCREEN_ADMITTED_KINDS='snxbc'  # the screen state with a bound: admitted when caps and readiness allow
SCREEN_NEAR_KINDS='nxhb'
SCREEN_FRAMES=len(SCREEN_PLAN)
SCREEN_CAPTURE_FRAMES=range(2,10) # X3M_CAPTURE_START=2, X3M_CAPTURE_FRAMES=8: the packed_sample diagnostic frames
SCREEN_OVERLAP={7:2}
SCREEN_FAULT={8:5,9:6}   # 5 SourceBind -> refusal 5, native; 6 Composite -> Incomplete, A|R recovered
SCREEN_UNBOUNDED_FRAMES=(1,11)
SCREEN_RESET_FRAME=10
SCREEN_STATUS_KEYS=50
SCREEN_QUAD=(32,24,48,40)           # pixels of the functional quad (64x64)
SCREEN_NEAR_TRAPEZOID=(32,20,56,24) # bounding box of the n footprint (84 pixels: rows 20-23, x from 32 below 48 + 2 (24 - y))
SCREEN_CHAIN_FRAME=21
SCREEN_CHAIN_QUADS=8;SCREEN_CHAIN_STEP_PX=2;SCREEN_CHAIN_SPRITE=16;SCREEN_CHAIN_SIGMA=3.;SCREEN_CHAIN_TINT=(.55,1.,.45)
SCREEN_CHAIN_ORIGIN=8
SCREEN_CHAIN_RECT=(SCREEN_CHAIN_ORIGIN,SCREEN_QUAD[1],SCREEN_CHAIN_ORIGIN+SCREEN_QUAD[2]-SCREEN_QUAD[0]+SCREEN_CHAIN_STEP_PX*(SCREEN_CHAIN_QUADS-1),SCREEN_QUAD[3])
SCREEN_KIND_RECT={'n':SCREEN_NEAR_TRAPEZOID,'h':(0,0,0,0),'c':SCREEN_CHAIN_RECT} # the SCREEN_SOURCE rect of the other kinds is SCREEN_QUAD
SCREEN_GAIN_RUN=2.
SCREEN_STRADDLE_RECT=(32,24,40,40)  # injected bound: the right half of the quad stays native
SCREEN_TEXEL=(.5,.25,.125,.5)       # bullet diffuse texel: q = rgb * h (h = 1), a = .5
SCREEN_GAIN=1.
SCREEN_SAMPLE_X=(36,44)             # inside the quad and the fade/emission scissor; inside the quad only
SCREEN_TIMING_SAMPLES=4


def screen_expected_sources(frame,screen=1,fade=1,emission=1,caps=1):
    """Per-source expectations of one functional frame: the packed
    admission (eligible, bounded, caps), preparation and completion, and the
    frame-stop after a composite fault."""
    result=[];stopped=False
    for source,kind in enumerate(SCREEN_PLAN[frame]):
        bullet=kind in SCREEN_KINDS
        fault=SCREEN_FAULT.get(frame,0) if kind=='s' else 0
        active=bool(screen if bullet else fade if kind=='f' else emission)
        applied=fault if active and (kind!='s' or caps) else 0 # the caps case queues no pass fault for a draw that never reaches the pass
        eligible=int(kind in 'spgc'+SCREEN_NEAR_KINDS and active) # d: dither on is a different state (pair refusal), never eligible
        unbounded=int(eligible and (frame in SCREEN_UNBOUNDED_FRAMES or kind=='h')) # h: the whole prefix behind the near plane
        caps_refused=int(eligible and not unbounded and not caps)
        readiness=int(eligible and not unbounded and not caps_refused and kind in 'pg') # PROJECTED stage / sRGB sampler
        admissible=active and not (bullet and (unbounded or caps_refused or kind=='d'))
        prepared=int(admissible and not stopped and applied!=5 and not readiness)
        incomplete=int(prepared and applied==6)
        linear=int(prepared and not incomplete)
        # Without the option the bullet pair is an ordinary non-producer pair
        # (histogram bit 0), as is the dither state with it; unbounded and
        # caps refusals are outside the histogram.
        refused=int((admissible and not prepared) or (bullet and (not active or kind=='d')))
        if incomplete:stopped=True
        admitted_kind=kind in SCREEN_ADMITTED_KINDS
        result.append(dict(kind=kind,fault=applied,overlap=SCREEN_OVERLAP.get(frame,1) if kind=='s' else SCREEN_CHAIN_QUADS if kind=='c' else 1,prepared=prepared,linear=linear,native=0,incomplete=incomplete,refused=refused,readiness=readiness,
                           packed_eligible=eligible,packed_admitted=int(admitted_kind and prepared),packed_linear=int(admitted_kind and linear),packed_incomplete=int(admitted_kind and incomplete),
                           packed_unbounded=unbounded,packed_caps=caps_refused,prefix_bound=int(bullet and screen and not unbounded),prefix_refused=unbounded,
                           witnessed=int(eligible and not unbounded and not caps_refused and not readiness), # reached the bracket's witness record
                           mask_valid=not stopped))
    return result,stopped


def screen_expected_witness(frame,screen=1,fade=1,emission=1,caps=1):
    sources,stopped=screen_expected_sources(frame,screen,fade,emission,caps)
    # A fade rectangle is recorded before admission; a packed rectangle only
    # for a bounded, caps-admitted screen draw that reaches the pass.
    rects=sum(1 for s in sources if (s['kind']=='f' and fade) or s['witnessed'])
    fade_prepared=sum(s['prepared'] for s in sources if s['kind']=='f');packed=sum(s['packed_admitted'] for s in sources)
    emission_prepared=sum(s['prepared'] for s in sources if s['kind']=='e')
    reason='no_fade' if not rects else 'emission' if emission_prepared else 'mask_invalid' if stopped else 'sampled'
    return dict(reason=reason,rects=rects,fade_prepared=fade_prepared,emission_prepared=emission_prepared,packed_prepared=packed)


def screen_decode(x):return max(x,1e-10)**2.2
def screen_encode(x):return max(x,0.)**(1/2.2) if x>0 else 0.


def screen_law(before,texel,h,overlap,gain=SCREEN_GAIN):
    """The step E composition of the note for `overlap` identical fragments:
    the native encoded value accumulates exactly as the game's blend does,
    is decoded ONCE at publication and the bolt's own contribution is scaled,
    C = encode(decode(A) + gain (decode(B_native) - decode(A))); alpha is the
    native a + (1 - a) A.alpha. Float64 restatement; FP16 storage is the
    tolerance. At gain 1 this is screen_native."""
    b=screen_native(before,texel,h,overlap)
    return tuple(screen_encode(screen_decode(before[c])+gain*(screen_decode(b[c])-screen_decode(before[c]))) for c in range(3))+(b[3],)


def screen_native(before,texel,h,overlap):
    q=[texel[c]*h for c in range(3)];a=texel[3];rgb=list(before[:3]);alpha=before[3]
    for _ in range(overlap):
        rgb=[q[c]+(1-q[c])*rgb[c] for c in range(3)];alpha=a+(1-a)*alpha
    return tuple(rgb)+(alpha,)


def screen_sample_expectation(row,sources,fade,emission,gain=SCREEN_GAIN):
    kind=row['kind'];source=int(row['source']);before=rgba(row['before']);wanted=sources[source]
    if kind in SCREEN_KINDS:
        texel=tuple(map(float,row['q'].split(',')))+(float(row['a']),)
        assert texel==SCREEN_TEXEL,(row['frame'],'bullet texel')
        assert not int(row['packed']) or kind in SCREEN_ADMITTED_KINDS,(row['frame'],'only the admitted state composes')
        if not int(row['covered']):return before
        if kind=='p':return None # projected coordinates: the native sample is undefined for the 1x1 texel too
        if kind=='c':return None # the soft sprite chain: the raw rectangle against the native twin is the oracle (validate_screen_chain)
        if wanted['incomplete']:return before # A|R recovered exactly
        if int(row['packed']):return screen_law(before,texel,1.,int(row['overlap']),gain)
        # A packed bracket composes only inside its rectangle: with the injected
        # straddling bound the quad outside it is missing, never native (the
        # C-shape failure of the note; the witness is the detector).
        if int(row['bracket']):return before
        return screen_native(before,texel,1.,int(row['overlap']))
    if not int(row['covered']):return before
    if kind=='e':return expected_emission(before,emission)
    return expected_composite(before,0) if fade else None


def validate_screen_samples(rows,expected_by_frame,fade,emission,gain=SCREEN_GAIN):
    """SCREEN_SAMPLE rows against the laws; native and packed screen alphas of
    the same destination alpha must agree bit for bit (the composite carries
    M.alpha, the native blend result)."""
    maximum=0.;count=0;native_alpha={};packed_alpha={};seen=set()
    for row in rows:
        frame,source,x=(int(row[k]) for k in ('frame','source','x'))
        key=(frame,source,x);assert key not in seen;seen.add(key)
        sources,_=expected_by_frame[frame]
        assert row['kind']==sources[source]['kind'] and int(row['overlap'])==sources[source]['overlap']
        before,after=rgba(row['before']),rgba(row['after'])
        wanted=screen_sample_expectation(row,sources,fade,emission,gain)
        if wanted is None:continue # fade off / projected stage: no CPU oracle
        for a,b in zip(after,wanted):
            fraction=abs(a-b)/(.006*abs(b)+.00002);maximum=max(maximum,fraction)
            assert fraction<=1,(key,'screen law / native / fade / emission',after,wanted,fraction)
        if row['kind'] in 'sgdxb' and int(row['covered']) and int(row['overlap'])==1 and not sources[source]['incomplete'] and int(row['packed'])==int(row['bracket']):
            (packed_alpha if int(row['packed']) else native_alpha).setdefault(before[3],set()).add(after[3])
        count+=1
    for alpha,values in packed_alpha.items():
        assert len(values)==1,('packed alpha must be deterministic',alpha,values)
        if alpha in native_alpha:assert values==native_alpha[alpha],('packed alpha equals the native blend result',alpha,values,native_alpha[alpha])
    assert not packed_alpha or any(alpha in native_alpha for alpha in packed_alpha),'at least one packed/native alpha pair at the same destination alpha'
    return dict(samples=count,max_tolerance_fraction=maximum,alpha_pairs=sum(alpha in native_alpha for alpha in packed_alpha))


def validate_screen_functional(output,trace,screen=1,fade=1,emission=1,caps=1,injected=None,gain=SCREEN_GAIN):
    lines=output.splitlines();traces=trace.splitlines()
    terminal=[fields(line) for line in lines if line.startswith('RESULT PASS ')]
    assert len(terminal)==1 and not any(line.startswith('RESULT FAIL') for line in lines)
    checks=[fields(line) for line in lines if line.startswith('SCREEN_CHECKS ')]
    assert len(checks)==1 and int(checks[0]['frames'])==SCREEN_FRAMES and checks[0]['qualified']=='1' and checks[0]['benchmark']=='0'
    assert tuple(map(int,checks[0]['quad'].split(',')))==SCREEN_QUAD and int(checks[0]['injected'])==int(injected is not None)
    live=indexed(lines,'SCREEN_LIVE ','frame');assert set(live)==set(range(SCREEN_FRAMES))
    source_rows=[fields(line) for line in lines if line.startswith('SCREEN_SOURCE ')]
    assert [(int(r['frame']),int(r['source'])) for r in source_rows]==[(f,i) for f in range(SCREEN_FRAMES) for i in range(len(SCREEN_PLAN[f]))]
    by_frame={f:[] for f in range(SCREEN_FRAMES)}
    for row in source_rows:by_frame[int(row['frame'])].append(row)
    expected_by_frame={f:screen_expected_sources(f,screen,fade,emission,caps) for f in range(SCREEN_FRAMES)}
    policies=(3|IN_PLACE_POLICY)|(8 if screen and caps else 0)
    totals=dict(packed_eligible=0,packed_admitted=0,packed_linear=0,packed_incomplete=0,packed_unbounded=0,packed_caps=0,packed_region_pixels=0)
    bracket_pixels={};total_sources=0 # kind -> region pixels of one packed bracket (one bound rectangle per kind)
    for frame,row in live.items():
        assert int(row['screen'])==screen and int(row['fade'])==fade and int(row['emission'])==emission
        status=[int(row[f's{i}']) for i in range(SCREEN_STATUS_KEYS)]
        expected,stopped=expected_by_frame[frame]
        assert int(row['draws'])==len(expected)
        assert status[16]==3,'required producers stay the fade/emission bits: policy 8 is never required'
        assert status[18]==status[19]==policies,(frame,'policy 8 attaches beside fade and emission only with the option and the caps',status[18])
        assert status[21]==(5 if screen and caps else 4),'one shared pool: five targets with the packed plane, four without'
        assert status[2]==status[3]==status[9]==0,'no lost/quarantined/suppressed state'
        assert status[1]==int(not stopped) and status[17]==int(stopped),(frame,'frame stop follows the composite fault only')
        for index,key in ((4,'prepared'),(5,'linear'),(6,'native'),(7,'incomplete')):
            assert status[index]==sum(s[key] for s in expected),(frame,key,status[index])
        for index,key in ((40,'packed_eligible'),(41,'packed_admitted'),(42,'packed_linear'),(43,'packed_incomplete'),(44,'packed_unbounded'),(45,'packed_caps'),(47,'prefix_bound'),(48,'prefix_refused')):
            assert status[index]==sum(s[key] for s in expected),(frame,key,status[index],sum(s[key] for s in expected))
        assert status[27]==status[14]+status[41] and status[28]==status[15]+status[42],'in-place brackets are the fade and the packed ones'
        assert status[10]==status[4]-status[27],'only emission exchanges'
        for actual,wanted in zip(by_frame[frame],expected):
            assert actual['kind']==wanted['kind'] and int(actual['overlap'])==wanted['overlap'] and int(actual['fault'])==wanted['fault'] and int(actual['hr'],16)==0
            assert int(actual['original_calls'])==1,(frame,'source once')
            for key in ('prepared','linear','native','incomplete','refused','packed_eligible','packed_admitted','packed_linear','packed_incomplete','packed_unbounded','packed_caps','prefix_bound','prefix_refused'):
                assert int(actual[key])==wanted[key],(frame,actual['source'],key,actual[key],wanted[key])
            rect=tuple(map(int,actual['rect'].split(',')))
            kind=wanted['kind']
            assert rect==(SCREEN_KIND_RECT.get(kind,SCREEN_QUAD) if kind in SCREEN_KINDS else source_scissor(0)),(frame,'source rectangle')
            pixels=int(actual['packed_region_pixels'])
            if wanted['packed_admitted']:
                assert pixels>0 and (bracket_pixels.get(kind,pixels)==pixels),(frame,'one bound rectangle per bracket',pixels)
                bracket_pixels[kind]=pixels
                if injected:assert pixels==rect_area(injected),(frame,'injected rectangle composed')
                else:
                    assert pixels>=rect_area(SCREEN_KIND_RECT.get(kind,SCREEN_QUAD)),(frame,'the conservative bound covers the footprint')
                    if kind in SCREEN_NEAR_KINDS:assert pixels<2048,(frame,kind,'a clipped bound stays well below the 64x64 viewport',pixels)
            else:assert pixels==0
            if not wanted['prepared']:
                # An unprepared source never touches the coverage lane; a bind
                # failure after the plane init leaves M.alpha|R seeded (the
                # bracket's scratch lane, step A deviations), nothing else.
                assert actual['hash_red_before']==actual['hash_red_after'],(frame,'unprepared source changed M coverage')
                assert wanted['fault']==5 or actual['hash_mask_before']==actual['hash_mask_after'],(frame,'unprepared source changed M')
            for key,value in totals.items():totals[key]=value+int(actual[key])
        total_sources+=len(expected)
    assert sum(line=='RESET PASS' for line in lines)==1
    resets=[fields(line) for line in lines if line.startswith('SCREEN_RESET ')]
    assert len(resets)==1 and int(resets[0]['frame'])==SCREEN_RESET_FRAME and int(resets[0]['allocations'])==(5 if screen and caps else 4)
    assert int(resets[0]['refs_before'])-int(resets[0]['refs_after'])==int(resets[0]['allocations']) and resets[0]['quarantine']=='0' and resets[0]['state_lost']=='0'
    releases=[fields(line) for line in traces if line.startswith('motion_output_release ')]
    assert len(releases)==1 and int(releases[0]['released'])==1,'every route object released at the final device Release'
    frames=indexed(traces,'linear_composition_frame ','frame');refusals=indexed(traces,'linear_composition_refusals ','frame')
    assert set(frames)==set(refusals)==set(range(SCREEN_FRAMES))
    for frame,row in frames.items():
        expected,_=expected_by_frame[frame];status=[int(live[frame][f's{i}']) for i in range(SCREEN_STATUS_KEYS)]
        for key,index in (('packed_eligible',40),('packed_admitted',41),('packed_linear',42),('packed_incomplete',43),('packed_unbounded_refused',44),('packed_caps_refused',45),('packed_region_pixels',46)):
            assert int(row[key])==status[index],(frame,'frame line',key,row[key],status[index])
        assert int(row['region_pixels'])==int(status[29]) and int(row['region_pixels'])>=int(row['packed_region_pixels'])
        ref=refusals[frame]
        assert int(ref['preparation'])==sum(s['refused'] for s in expected if s['fault']==5),(frame,'prepare failure refusal',ref['preparation'])
        assert int(ref['readiness'])==sum(s['readiness'] for s in expected),(frame,'readiness refusals: PROJECTED stage and sRGB sampler only',ref['readiness'])
        assert int(ref['readers'])==0,(frame,'no reader refusal',ref)
    variants=[fields(line) for line in traces if line.startswith('screen_emission_variant ')]
    assert [(v['original'],int(v['transform']),int(v['create'],16)) for v in variants]==([(SCREEN_PAIR[1],0,0)] if screen else []),'the packed producer is created once for the row-19 PS, only with the option'
    modes=[fields(line) for line in traces if line.startswith('screen_emission_mode ')]
    assert [(int(m['enabled']),float(m['gain']),int(m['gain_valid'])) for m in modes]==([(1,gain,1)] if screen else []),('the configured step E gain is read once',modes)
    chains=[fields(line) for line in lines if line.startswith('SCREEN_CHAIN ')]
    assert [(int(c['frame']),int(c['quads']),int(c['step_px']),int(c['sprite']),tuple(map(int,c['rect'].split(','))),int(c['packed'])) for c in chains]==[(SCREEN_CHAIN_FRAME,SCREEN_CHAIN_QUADS,SCREEN_CHAIN_STEP_PX,SCREEN_CHAIN_SPRITE,SCREEN_CHAIN_RECT,int(bool(screen and caps)))],('one eight-quad chain draw',chains)
    if injected:assert tuple(map(int,chains[0]['composed'].split(',')))==injected
    packed_regions=[fields(line) for line in traces if line.startswith('packed_region ')]
    # One line per screen draw that reached the bracket's witness record: eligible, bounded, caps present (prepared or refused there).
    witnessed=[(f,s['kind']) for f in range(SCREEN_FRAMES) for s in expected_by_frame[f][0] if s['witnessed']]
    assert [int(r['frame']) for r in packed_regions]==[f for f,_ in witnessed],'packed_region lines'
    near_rects={}
    for r,(frame,kind) in zip(packed_regions,witnessed):
        assert (r['vs'],r['ps'])==SCREEN_PAIR
        rect=tuple(map(int,r['rect'].split(',')))
        if injected:assert rect==injected,(frame,'injected rectangle')
        else:
            footprint=SCREEN_KIND_RECT.get(kind,SCREEN_QUAD)
            assert rect[0]<=footprint[0] and rect[1]<=footprint[1] and rect[2]>=footprint[2] and rect[3]>=footprint[3],(frame,kind,'the bound rectangle covers the visible footprint',rect,footprint)
            if kind in SCREEN_NEAR_KINDS:near_rects[kind]=dict(rect=rect,f_permille=int(r['f_permille']))
    if screen and caps and not injected:assert set(near_rects)=={'n','x','b'},('near-plane kinds bounded and witnessed',near_rects)
    sample_result=validate_screen_samples([fields(line) for line in lines if line.startswith('SCREEN_SAMPLE ')],expected_by_frame,fade,emission,gain)
    packed_samples=validate_packed_samples(traces,expected_by_frame,injected)
    return dict(frames=SCREEN_FRAMES,sources=total_sources,region_pixels_per_bracket=bracket_pixels.get('s'),bracket_pixels=bracket_pixels,near_rects=near_rects,totals=totals,held_references=int(releases[0]['held']),packed_samples=packed_samples,**sample_result)


def validate_packed_samples(traces,expected_by_frame,injected=None):
    """packed_sample lines (capture frames only, screen-emission-region.md
    step B grammar): one per admitted packed draw of a captured frame with
    both readbacks S_OK, finite values and the centre inside the rectangle;
    none outside the capture window."""
    rows=[fields(line) for line in traces if line.startswith('packed_sample ')]
    expected=[f for f in SCREEN_CAPTURE_FRAMES for s in expected_by_frame[f][0] if s['packed_admitted']]
    assert [int(r['frame']) for r in rows]==expected,('one packed_sample per admitted packed draw of a capture frame',[int(r['frame']) for r in rows],expected)
    frames=indexed(traces,'linear_composition_frame ','frame')
    assert all(int(frames[f]['packed_sample_skipped'])==0 for f in frames),'no capture frame exceeds the packed_sample cap (at most two admitted packed draws per frame)'
    changed=0
    for r in rows:
        assert r['pre_result']=='00000000' and r['post_result']=='00000000',(r['frame'],'sample readbacks',r)
        rect=tuple(map(int,r['rect'].split(',')));cx,cy=map(int,r['centre'].split(','))
        assert rect[0]<=cx<rect[2] and rect[1]<=cy<rect[3],(r['frame'],'centre inside the rectangle')
        if injected:assert tuple(map(int,r['composed'].split(',')))==injected
        pre=[float(v) for v in r['pre'].split(',')];post=[float(v) for v in r['post'].split(',')]
        assert all(math.isfinite(v) for v in pre+post) and math.isfinite(float(r['pre_y'])) and math.isfinite(float(r['post_y'])),(r['frame'],'finite samples')
        changed+=int(r['pre_y']!=r['post_y'])
    return dict(lines=len(rows),luminance_changed=changed)


def screen_chain_sprite(x,y):
    """The fixture's soft sprite texel (tint times a Gaussian of sigma 3 px
    about the sprite centre; float32 in the fixture, float64 here)."""
    centre=(SCREEN_CHAIN_SPRITE-1)/2;g=math.exp(-((x-centre)**2+(y-centre)**2)/(2*SCREEN_CHAIN_SIGMA**2))
    return tuple(t*g for t in SCREEN_CHAIN_TINT)


def screen_chain_fragments(x,y):
    """Sprite texels of the chain quads covering pixel (x, y) in draw order."""
    l,t,_,b=SCREEN_CHAIN_RECT;result=[]
    if not t<=y<b:return result
    for q in range(SCREEN_CHAIN_QUADS):
        left=l+q*SCREEN_CHAIN_STEP_PX
        if left<=x<left+SCREEN_CHAIN_SPRITE:result.append(screen_chain_sprite(x-left,y-t))
    return result


def fp16_code(value):
    return struct.unpack('<H',struct.pack('<e',value))[0]


def read_rgba32f(path,width=64,height=64):
    data=path.read_bytes();assert len(data)==width*height*16,(path,len(data))
    return list(struct.iter_unpack('<4f',data))


def validate_screen_chain(functional,native,gain_run,gain=SCREEN_GAIN_RUN,width=64,height=64):
    """Step E acceptance on the overlap chain (frame SCREEN_CHAIN_FRAME):
    `functional` and `native` (the off run: the same chain drawn natively)
    and `gain_run` (X3M_SCREEN_EMISSION_GAIN=gain) are work directories. The
    before images agree exactly; inside the chain rectangle the composed
    result equals the native twin within one FP16 code per channel with the
    alpha exact; outside it every run leaves A untouched; the gain run
    follows encode(decode(A) + gain (decode(B) - decode(A))) on the native
    twin within the FP16 tolerance and is brighter than native on every bolt
    channel. The withdrawn per-fragment law is shown to differ from native."""
    frame=SCREEN_CHAIN_FRAME
    images={}
    for name,work in (('functional',functional),('native',native),('gain',gain_run)):
        for stage in ('before','after'):images[name,stage]=read_rgba32f(work/f'screen_emission_chain_{stage}_{frame}.rgba32f',width,height)
    before=images['native','before']
    assert images['functional','before']==before and images['gain','before']==before,'the chain frame starts from the same A in every run'
    l,t,r,b=SCREEN_CHAIN_RECT;inside=[(x,y) for y in range(t,b) for x in range(l,r)]
    max_codes=0;alpha_exact=True;bolt=0;lifted=0;max_fraction=0.;withdrawn_max=0.;native_changed=0;max_layers=0
    for y in range(height):
        for x in range(width):
            i=y*width+x;n=images['native','after'][i];f=images['functional','after'][i];g=images['gain','after'][i];a=before[i]
            if not (l<=x<r and t<=y<b):
                assert f==a and n==a and g==a,(x,y,'A outside the chain rectangle untouched')
                continue
            for c in range(3):
                codes=abs(fp16_code(f[c])-fp16_code(n[c]));max_codes=max(max_codes,codes)
                assert codes<=1,(x,y,c,'composed chain within one FP16 code of the native twin',f[c],n[c])
            alpha_exact=alpha_exact and f[3]==n[3]
            if n[:3]!=a[:3]:native_changed+=1
            fragments=screen_chain_fragments(x,y);max_layers=max(max_layers,len(fragments))
            # The withdrawn step C law on the same fragments (float64 model).
            light=[screen_decode(a[c]) for c in range(3)]
            for q in fragments:light=[q[c]**2.2+(1-q[c])*light[c] for c in range(3)]
            withdrawn_max=max(withdrawn_max,max(abs(screen_encode(light[c])-n[c]) for c in range(3)))
            for c in range(3):
                wanted=screen_encode(screen_decode(a[c])+gain*(screen_decode(n[c])-screen_decode(a[c])))
                fraction=abs(g[c]-wanted)/(.006*abs(wanted)+.00002);max_fraction=max(max_fraction,fraction)
                assert fraction<=1,(x,y,c,'gain run follows the step E law on the native twin',g[c],wanted,fraction)
                if screen_decode(n[c])-screen_decode(a[c])>1e-3:
                    bolt+=1;lifted+=g[c]>n[c]
    assert alpha_exact,'chain alpha equals the native blend'
    assert native_changed>=len(inside)//2,('the chain touches its rectangle',native_changed)
    assert max_layers==SCREEN_CHAIN_QUADS,('eight overlapping fragments',max_layers)
    assert bolt>0 and lifted==bolt,('every bolt channel is lifted by the gain',bolt,lifted)
    assert withdrawn_max>.05,('the withdrawn per-fragment law would differ from native',withdrawn_max)
    return dict(frame=frame,rect=SCREEN_CHAIN_RECT,pixels=len(inside),native_changed=native_changed,max_layers=max_layers,max_codes=max_codes,alpha_exact=alpha_exact,gain=gain,gain_max_tolerance_fraction=max_fraction,bolt_channels=bolt,withdrawn_law_max_delta=withdrawn_max)


def screen_footprint(kind,x,y,width=64,height=64):
    """Whether the bullet source of `kind` rasterises pixel (x, y): the n
    trapezoid under the D3D9 convention (integer pixel centres, top-left fill
    rule: top/left edges inclusive, bottom/right exclusive), the rectangle else."""
    if kind!='n':
        l,t,r,b=SCREEN_KIND_RECT.get(kind,SCREEN_QUAD);return l<=x<r and t<=y<b
    u=x/(width/2)-1;v=1-y/(height/2)
    return .25<v<=.375 and 0<=u<.5+2*(v-.25)


def screen_footprint_area(kind,width=64,height=64):
    return sum(screen_footprint(kind,x,y,width,height) for y in range(height) for x in range(width))


def screen_expected_mask(frame,screen,fade,emission,caps,width=64,height=64):
    """M footprint after the frame: every prepared source's raster (the
    packed source writes M unscissored, so the whole quad even when a smaller
    rectangle is composed; a composite fault keeps the source's M writes)."""
    sources,_=screen_expected_sources(frame,screen,fade,emission,caps)
    expected=[False]*(width*height)
    for s in sources:
        if not s['prepared']:continue
        bullet=s['kind'] in SCREEN_KINDS
        l,t,r,b=(0,0,width,height) if bullet else source_scissor(0,width,height)
        for y in range(t,b):
            for x in range(l,r):
                if not bullet or screen_footprint(s['kind'],x,y,width,height):expected[y*width+x]=True
    return expected


def validate_screen_pixels(work,screen,fade,emission,caps):
    temporal=[]
    for frame in range(SCREEN_FRAMES):
        actual=(work/'x3-modern-captures'/f'taa_1_{frame}.rgba16f').read_bytes();reference=(work/f'reference_taa_{frame}.rgba16f').read_bytes()
        assert len(actual)==64*64*8 and actual==reference,(frame,'actual supplemental TAA differs from the independent reference')
        temporal.append(hashlib.sha256(actual).hexdigest())
        color=(work/f'screen_emission_color_{frame}.rgba32f').read_bytes();mask=(work/f'screen_emission_mask_{frame}.rgba32f').read_bytes()
        assert len(color)==len(mask)==64*64*16
        assert all(math.isfinite(v) for p in struct.iter_unpack('<4f',color) for v in p)
        values=list(struct.iter_unpack('<4f',mask));expected=screen_expected_mask(frame,screen,fade,emission,caps)
        for index,(pixel,wanted) in enumerate(zip(values,expected)):
            assert (pixel[0]>0)==wanted,(frame,index,'M red footprint')
    return dict(temporal_sha256=temporal)


def validate_screen_witness(trace,screen=1,fade=1,emission=1,caps=1,k=WITNESS_K,injected=None):
    """fade_witness lines with packed rectangles in the union: zero covered
    pixels outside on every sampled frame, or (injected straddling
    rectangle) violations on exactly the packed frames."""
    lines=trace.splitlines();rows=indexed(lines,'fade_witness ','frame')
    assert set(rows)=={f for f in range(SCREEN_FRAMES) if f%k==0}
    sampled=[];skipped={};hist=[0]*8;covered=union=0;violations=[]
    for frame,row in sorted(rows.items()):
        wanted=screen_expected_witness(frame,screen,fade,emission,caps)
        assert row['reason']==wanted['reason'],(frame,row['reason'],wanted['reason'])
        assert int(row['rects'])==wanted['rects'] and int(row['fade_prepared'])==wanted['fade_prepared'],(frame,'rects',row['rects'],wanted)
        assert int(row['packed_prepared'])==wanted['packed_prepared'] and int(row['emission_prepared'])==wanted['emission_prepared'],(frame,'prepared kinds',row)
        assert int(row['rects_prepared'])==wanted['fade_prepared']+wanted['packed_prepared'],(frame,'prepared rectangles')
        assert int(row['lines_truncated'])==0,(frame,'every rectangle logged')
        h=list(map(int,row['f_hist'].split(',')));assert sum(h)==wanted['rects']
        if row['reason']!='sampled':skipped[row['reason']]=skipped.get(row['reason'],0)+1;continue
        assert row['result']=='00000000' and int(row['width'])==64 and int(row['height'])==64
        c,o,u=(int(row[key]) for key in ('covered','outside','union'))
        assert u>=c-o
        if wanted['packed_prepared'] and injected:assert u<=rect_area(injected)+wanted['fade_prepared']*64*64,(frame,'injected union')
        hist=[a+b for a,b in zip(hist,h)];covered+=c;union+=u;sampled.append(frame)
        if o:violations.append((frame,o))
    if violations:raise WitnessViolation(violations)
    return dict(k=k,sampled_frames=sampled,skipped=skipped,covered_pixels=covered,union_area=union,outside_pixels=0,f_histogram=dict(zip(WITNESS_BUCKETS,hist)))


def screen_straddle_violations(width=64,height=64):
    """Frames where the injected half rectangle leaves covered bullet pixels
    outside the union: the packed frames sampled by the witness. Per kind:
    the footprint minus its overlap with the injected rectangle (the n
    trapezoid lies entirely outside it)."""
    def outside(kind):
        l,t,r,b=SCREEN_STRADDLE_RECT
        return sum(screen_footprint(kind,x,y,width,height) and not (l<=x<r and t<=y<b) for y in range(height) for x in range(width))
    # A prepared fade source's rectangle is the full viewport (no seam scope):
    # its union hides the straddle on the mixed frames.
    result={}
    for f in range(SCREEN_FRAMES):
        w=screen_expected_witness(f)
        if w['reason']!='sampled' or not w['packed_prepared'] or w['fade_prepared']:continue
        result[f]=sum(outside(s['kind']) for s in screen_expected_sources(f)[0] if s['packed_admitted'])
    return result


def validate_screen_timing(output,trace,screen,width,height):
    rows=[fields(line) for line in output.splitlines() if line.startswith('SCREEN_TIMING ')]
    assert [(int(r['count']),int(r['sample'])) for r in rows]==[(c,s) for c in COUNTS for s in range(SCREEN_TIMING_SAMPLES)]
    quad=None;per_bracket=None;counts={}
    for r in rows:
        assert (int(r['width']),int(r['height']))==(width,height) and int(r['screen'])==screen
        count=int(r['count'])
        assert int(r['admitted'])==count*screen and int(r['unbounded'])==0,(count,'every timed draw bound and admitted with the option')
        pixels=int(r['region_pixels']);quad=quad or tuple(map(int,r['quad'].split(',')))
        if screen:
            assert pixels%count==0 and (per_bracket in (None,pixels//count)),(count,'region pixels per bracket');per_bracket=pixels//count
            assert per_bracket>=rect_area(quad)
        else:assert pixels==0
        counts.setdefault(str(count),[]).append({k:float(r[f'{k}_ms']) for k in ('source','terminal','total')})
    checks=[fields(line) for line in output.splitlines() if line.startswith('SCREEN_CHECKS ')]
    assert len(checks)==1 and checks[0]['benchmark']=='1' and int(checks[0]['submissions'])==6*sum(COUNTS)
    frames=indexed(trace.splitlines(),'linear_composition_frame ','frame')
    assert all(int(row['packed_incomplete'])==0 and int(row['packed_caps_refused'])==0 for row in frames.values())
    return dict(width=width,height=height,screen=screen,quad=quad,region_pixels_per_bracket=per_bracket,counts=counts)


def screen_timing_name(width,height,pair,screen):
    return f'screen-timing-{width}x{height}-pair{pair}-screen{screen}'


def screen_paired_cost(cases):
    result=[]
    for width,height in RESOLUTIONS:
        for count in COUNTS:
            pairs=[]
            for pair in (0,1):
                off=cases[screen_timing_name(width,height,pair,0)]['counts'][str(count)];on_case=cases[screen_timing_name(width,height,pair,1)];on=on_case['counts'][str(count)]
                deltas={k:[b[k]-a[k] for a,b in zip(off,on)] for k in ('source','terminal','total')}
                pairs.append(dict(order='off/on' if pair==0 else 'on/off',off_median_ms={k:statistics.median(r[k] for r in off) for k in deltas},
                                  on_median_ms={k:statistics.median(r[k] for r in on) for k in deltas},paired_window_median_delta_ms={k:statistics.median(v) for k,v in deltas.items()}))
            on_case=cases[screen_timing_name(width,height,0,1)]
            result.append(dict(width=width,height=height,quad=on_case['quad'],region_pixels_per_bracket=on_case['region_pixels_per_bracket'],ordered_dips=count,
                               per_bracket_source_delta_ms=[round(p['paired_window_median_delta_ms']['source']/count,4) for p in pairs],pairs=pairs))
    return result


SCREEN_SCOPE=('Actual capture DrawPrimitive / MotionOutput admission / packed screen policy 8 in the shared pool / '
              'locked-prefix bound from the ownership Unlock scan / Hdr FP16 scene / supplemental TemporalPass. '
              'The row-19 bullet pair, one exact Asteroid fade pair, one additive emission pair and two bootstrap programs.')
SCREEN_LIMITATIONS=[
    'The bullet rows are the identity (positions are clip coordinates): the projection of the locked-prefix box is exercised, not a perspective camera; the fixture quad is axis-aligned.',
    'The packed-law oracle is a float64 restatement of the step E law with the FP16 store as tolerance (fraction <= 1 of .006|b|+.00002); the overlap chain is compared with its native twin from a separate process (the off run) within one FP16 code, and the gain run against the law on that twin; the detached packed corpus holds the bit-exact prototype comparison.',
    'Caps refusal is produced by withholding policy 8 from the attach request (seam only), the same route decision as a device without four targets or INVSRCALPHA.',
    'Timing pairs toggle the screen option (and with it the step B scan) with the same material/TAA/HDR settings; EVENT-fenced windows, not GPU timestamps or game FPS.',
    'Native Windows, installation and gameplay appearance remain unverified.',
]


def main_screen(args):
    inputs=[args.fixture.resolve(),args.dll.resolve(),*[args.programs.resolve()/name for name in SCREEN_PROGRAMS]]
    assert all(path.is_file() for path in inputs),'explicit prebuilt inputs and scoped original programs required'
    hashes={str(p):sha(p) for p in inputs}
    raw=Path(tempfile.mkdtemp(prefix='x3-screen-emission-live-'))
    result=dict(passed=False,bottle=bottle.describe(),game_launched=False,raw=str(raw),scope=SCREEN_SCOPE,inputs=hashes,cases={},limitations=SCREEN_LIMITATIONS)
    runs=[dict(name='screen-functional',screen=1,witness=True),dict(name='screen-off',screen=0,witness=True),
          dict(name='screen-gain2',screen=1,witness=True,gain=SCREEN_GAIN_RUN),
          dict(name='screen-straddle',screen=1,witness=True,rect=SCREEN_STRADDLE_RECT,expect_violations=screen_straddle_violations()),
          dict(name='screen-caps',screen=1,witness=True,caps=0)]
    for width,height in RESOLUTIONS:
        for pair in (0,1):
            for screen in ((0,1) if pair==0 else (1,0)):
                runs.append(dict(name=screen_timing_name(width,height,pair,screen),screen=screen,timing=True,width=width,height=height))
    try:
        for run in runs:
            assert not game_running(),'game running; no fixture launch'
            work=raw/run['name'];work.mkdir()
            shutil.copy2(inputs[0],work/'fixture.exe');shutil.copy2(inputs[1],work/'d3d9.dll')
            env={k:v for k,v in os.environ.items() if not k.startswith('X3M_')}
            env.update(X3M_MOTION_OUTPUT='1',X3M_HDR='1',X3M_HDR_TONEMAP='agx',X3M_HDR_DECODE='gamma2.2',
                       X3M_HDR_EXPOSURE='manual',X3M_HDR_EV_MANUAL='0',X3M_HDR_CLAMP='0',X3M_HDR_BLOOM='0',
                       X3M_LINEAR_MATERIALS='1',X3M_MATERIAL_DIRECT_GAIN='1',X3M_MATERIAL_EMISSIVE_GAIN='1',X3M_LIGHTMAP_EMISSIVE_GAIN='1',
                       X3M_LINEAR_DISTANCE_FADE='1',X3M_LINEAR_EMISSIONS='1',X3M_EMISSION_GAIN='1',
                       X3M_SCREEN_EMISSION=str(run['screen']),X3M_SCREEN_EMISSION_BOUND=str(run['screen']),X3M_SCREEN_EMISSION_GAIN=repr(run.get('gain',SCREEN_GAIN)),
                       X3M_OWNERSHIP='1',X3M_TAA='1',X3M_TAA_SENTINEL='2',X3M_TAA_SHARPEN='0',X3M_TAA_MIP_BIAS='-.5',
                       X3M_FIXTURE_CAMERA='rotate',X3M_SCENE_HOOK='0',X3M_TELEMETRY='1',X3M_MOTION_FRAME_LOG='1',X3M_STATE_SHADOW='1',
                       X3M_MOTION_RT_MODE='lazy',X3M_TAA_DEBUG='0' if run.get('timing') else '1',
                       X3M_CAPTURE_START='1000000' if run.get('timing') else str(SCREEN_CAPTURE_FRAMES.start),X3M_CAPTURE_FRAMES='0' if run.get('timing') else str(len(SCREEN_CAPTURE_FRAMES)),WINEDLLOVERRIDES='d3d9=n,b')
            if run.get('witness'):env['X3M_FADE_WITNESS']=str(WITNESS_K)
            if run.get('rect'):env['X3M_FIXTURE_SCREEN_RECT']=','.join(map(str,run['rect']))
            if run.get('caps')==0:env['X3M_FIXTURE_SCREEN_CAPS_FAULT']='1'
            mode='screenemissionbench' if run.get('timing') else 'screenemission'
            command=[bottle.WINE,*bottle.wine_args(),'--dll','d3d9=n,b','--workdir',str(work),str(work/'fixture.exe'),'Z:'+str(inputs[2]),'Z:'+str(inputs[3]),mode]
            if run.get('timing'):command.append(f"{run['width']}x{run['height']}")
            start=time.monotonic()
            with (work/'stdout.txt').open('w') as out,(work/'wine.log').open('w') as error:
                child=subprocess.run(command,env=env,stdout=out,stderr=error,timeout=180)
            assert child.returncode==0,f"{run['name']}: fixture failed; {work}"
            logs=list((work/'x3-modern-captures').glob('session-*.log'));assert len(logs)==1
            output=(work/'stdout.txt').read_text();trace=logs[0].read_text()
            caps=run.get('caps',1)
            if run.get('timing'):case=validate_screen_timing(output,trace,run['screen'],run['width'],run['height'])
            else:
                case=validate_screen_functional(output,trace,run['screen'],1,1,caps,run.get('rect'),run.get('gain',SCREEN_GAIN))
                case.update(validate_screen_pixels(work,run['screen'],1,1,caps))
                try:
                    case['witness']=validate_screen_witness(trace,run['screen'],1,1,caps,WITNESS_K,run.get('rect'))
                    assert not run.get('expect_violations'),f"{run['name']}: the straddling rectangle must fail the witness"
                except WitnessViolation as violation:
                    assert violation.violations==run.get('expect_violations'),(run['name'],'unexpected witness violations',violation.violations,run.get('expect_violations'))
                    case['witness']=dict(k=WITNESS_K,expected_failure=str(violation),violations=violation.violations)
            case['seconds']=round(time.monotonic()-start,3);case['raw']=str(work)
            result['cases'][run['name']]=case
        result['paired_cost']=screen_paired_cost(result['cases'])
        # Step E: the composed chain against its native twin (off run) and the gain run.
        result['chain']=validate_screen_chain(Path(result['cases']['screen-functional']['raw']),Path(result['cases']['screen-off']['raw']),Path(result['cases']['screen-gain2']['raw']))
        # The straddling and caps runs leave the sampled unaffected frames and
        # the native/fade/emission samples exactly as the functional run.
        functional=result['cases']['screen-functional']
        # Caps refusal precedes the readiness gates: every bounded eligible draw of the functional run is caps-refused.
        assert result['cases']['screen-off']['totals']['packed_eligible']==0
        assert result['cases']['screen-caps']['totals']['packed_caps']==sum(s['packed_caps'] for f in range(SCREEN_FRAMES) for s in screen_expected_sources(f,1,1,1,0)[0])==functional['totals']['packed_eligible']-functional['totals']['packed_unbounded']
        result['passed']=True
    except Exception as error:
        result['error']=f'{type(error).__name__}: {error}'
        raise
    finally:
        args.result.parent.mkdir(parents=True,exist_ok=True)
        args.result.write_text(json.dumps(result,indent=1)+'\n')
        print(json.dumps(dict(passed=result['passed'],result=str(args.result),raw=str(raw),cases=len(result['cases']),error=result.get('error'))))
    return 0 if result['passed'] else 1


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True);parser.add_argument('--dll',type=Path,required=True)
    parser.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--reuse-native',type=Path,help='retained lazy1-fade0-emission0 directory from a parser-stopped run; exact input hashes and every output are revalidated')
    parser.add_argument('--result',type=Path,default=None,help='default: linear-distance-fade-live.json, or screen-emission-live1.json with --screen-emission')
    parser.add_argument('--screen-emission',action='store_true',help='step C runs only (screenemission/screenemissionbench fixture modes): functional, off, straddling, caps refusal and paired timing')
    args=parser.parse_args();assert bottle.BOTTLE=='X3','new qualification requires X3'
    if args.result is None:args.result=bottle.results_dir(ROOT,create=False)/('screen-emission-live1.json' if args.screen_emission else 'linear-distance-fade-live.json')
    if args.screen_emission:return main_screen(args)
    inputs=[args.fixture.resolve(),args.dll.resolve(),*[args.programs.resolve()/name for name in PROGRAMS]]
    assert all(path.is_file() for path in inputs),'explicit prebuilt inputs and scoped original programs required'
    hashes={str(p):sha(p) for p in inputs}
    native_reuse=reusable_native(args.reuse_native,hashes) if args.reuse_native else None
    raw=Path(tempfile.mkdtemp(prefix='x3-distance-fade-live-'))
    result=dict(passed=False,bottle=bottle.describe(),game_launched=False,raw=str(raw),scope=SCOPE,inputs=hashes,cases={},limitations=LIMITATIONS)
    # Batch all three draw counts into each timing process; alternating process
    # order controls drift without multiplying startups for individual counts.
    runs=[dict(name=f'lazy1-fade{fade}-emission{emission}',fade=fade,emission=emission,lazy=1,taa=1,hdr=1) for fade in (0,1) for emission in (0,1)]
    runs.append(dict(name='lazy0-fade1-emission1',fade=1,emission=1,lazy=0,taa=1,hdr=1))
    runs += [dict(name=f'admission-taa{taa}-hdr{hdr}',fade=1,emission=1,lazy=1,taa=taa,hdr=hdr,admission=True) for taa,hdr in ((0,1),(1,0))]
    # Per resolution and pair order: fade off, then fade on at every requested
    # region fraction (pair 1 reverses the order), so each on-variant has an
    # off baseline in both orders.
    for width,height in RESOLUTIONS:
        for pair in (0,1):
            variants=[('0','1',None)]+[('1',fraction,side) for fraction,side in TIMING_FRACTIONS]
            for fade,fraction,side in (variants if pair==0 else variants[::-1]):
                runs.append(dict(name=timing_name(width,height,pair,int(fade),fraction),fade=int(fade),emission=0,lazy=1,taa=1,hdr=1,timing=True,
                                 width=width,height=height,rect=timing_rect(width,height,side)))
        # Station producer: paired windows at the derived full rectangle only.
        for pair in (0,1):
            for fade in ((0,1) if pair==0 else (1,0)):
                runs.append(dict(name=timing_name(width,height,pair,fade,producer=STATION_PAIR),fade=fade,emission=0,lazy=1,taa=1,hdr=1,timing=True,
                                 width=width,height=height,rect=None,producer=STATION_PAIR))
    runs.append(dict(name='witness-full',fade=1,emission=0,lazy=1,taa=1,hdr=1,witness=True))
    runs.append(dict(name='witness-rect',fade=1,emission=0,lazy=1,taa=1,hdr=1,witness=True,rect=WITNESS_RECT))
    runs.append(dict(name='witness-control',fade=1,emission=0,lazy=1,taa=1,hdr=1,witness=True,rect=WITNESS_CONTROL_RECT,expect_violations=WITNESS_CONTROL_VIOLATIONS))
    try:
        for run in runs:
            assert not game_running(),'game running; no fixture launch'
            reused=native_reuse is not None and run['name']=='lazy1-fade0-emission0'
            work=native_reuse if reused else raw/run['name']
            if not reused:
                work.mkdir()
                shutil.copy2(inputs[0],work/'fixture.exe');shutil.copy2(inputs[1],work/'d3d9.dll')
            env={k:v for k,v in os.environ.items() if not k.startswith('X3M_')}
            env.update(X3M_MOTION_OUTPUT='1',X3M_HDR=str(run['hdr']),X3M_HDR_TONEMAP='agx',X3M_HDR_DECODE='gamma2.2',
                       X3M_HDR_EXPOSURE='manual',X3M_HDR_EV_MANUAL='0',X3M_HDR_CLAMP='0',X3M_HDR_BLOOM='0',
                       X3M_LINEAR_MATERIALS='1',X3M_MATERIAL_DIRECT_GAIN='1',X3M_MATERIAL_EMISSIVE_GAIN='1',X3M_LIGHTMAP_EMISSIVE_GAIN='1',
                       X3M_LINEAR_DISTANCE_FADE=str(run['fade']),X3M_LINEAR_EMISSIONS=str(run['emission']),X3M_EMISSION_GAIN='1',
                       X3M_OWNERSHIP='1',X3M_TAA=str(run['taa']),X3M_TAA_SENTINEL='2',X3M_TAA_SHARPEN='0',X3M_TAA_MIP_BIAS='-.5',
                       X3M_FIXTURE_CAMERA='rotate',X3M_SCENE_HOOK='0',X3M_TELEMETRY='1',X3M_MOTION_FRAME_LOG='1',X3M_STATE_SHADOW='1',
                       X3M_MOTION_RT_MODE='lazy' if run['lazy'] else 'perdraw',X3M_TAA_DEBUG='0' if run.get('timing') else '1',
                       X3M_CAPTURE_START='1000000' if run.get('timing') else '1',X3M_CAPTURE_FRAMES='0',WINEDLLOVERRIDES='d3d9=n,b')
            if run.get('witness'):env['X3M_FADE_WITNESS']=str(WITNESS_K)
            if run.get('rect'):env['X3M_FIXTURE_FADE_RECT']=','.join(map(str,run['rect']))
            if run.get('producer'):env['X3M_FIXTURE_FADE_BENCH_PAIR']=str(run['producer'])
            mode='distancefadebench' if run.get('timing') else 'distancefade'
            command=[bottle.WINE,*bottle.wine_args(),'--dll','d3d9=n,b','--workdir',str(work),str(work/'fixture.exe'),'Z:'+str(inputs[2]),'Z:'+str(inputs[3]),mode]
            if run.get('timing'):command.append(f"{run['width']}x{run['height']}")
            start=time.monotonic()
            if not reused:
                with (work/'stdout.txt').open('w') as out,(work/'wine.log').open('w') as error:
                    child=subprocess.run(command,env=env,stdout=out,stderr=error,timeout=180)
                assert child.returncode==0,f"{run['name']}: fixture failed; {work}"
            logs=list((work/'x3-modern-captures').glob('session-*.log'));assert len(logs)==1
            output=(work/'stdout.txt').read_text();trace=logs[0].read_text()
            if run.get('timing'):case=validate_timing(output,trace,run['fade'],run['width'],run['height'],run.get('rect'),run.get('producer',0))
            elif run.get('admission'):case=validate_admission(output,trace,run['taa'],run['hdr'])
            else:
                case=validate_functional(output,trace,run['fade'],run['emission'],run['lazy'],run.get('rect'))
                case.update(validate_pixels(work,run['fade'],run['emission']))
            if run.get('witness'):
                try:
                    case['witness']=validate_witness(trace,run['fade'],run['emission'],WITNESS_K,run.get('rect'))
                    assert not run.get('expect_violations'),f"{run['name']}: the wrong rectangle must fail the witness"
                except WitnessViolation as violation:
                    assert violation.violations==run.get('expect_violations'),(run['name'],'unexpected witness violations',violation.violations)
                    case['witness']=dict(k=WITNESS_K,expected_failure=str(violation),violations=violation.violations)
            case['seconds']=round(time.monotonic()-start,3)
            case['raw']=str(work);case['retained_native_process']=reused
            result['cases'][run['name']]=case
            print(f"{run['name']}: passed, {case['frames']} frames, {case['seconds']} s",flush=True)
        compare_functional(result['cases'])
        compare_witness(result['cases'])
        result['paired_completion_cost']=paired_cost(result['cases'])
        assert hashes=={str(p):sha(p) for p in inputs},'prebuilt qualification inputs changed'
        timing_runs=len(RESOLUTIONS)*2*(1+len(TIMING_FRACTIONS)+2)
        result['functional_frames']=5*FRAMES;result['admission_frames']=8;result['timing_frames']=18*timing_runs;result['witness_frames']=3*FRAMES
        result['new_processes']=len(runs)-int(native_reuse is not None)
        result['retained_processes']=int(native_reuse is not None)
        result['passed']=True
    finally:
        path=args.result if result['passed'] else raw/'failed-result.json';path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text(json.dumps(result,indent=1)+'\n') # 34 processes: indent 1 keeps the tracked result under 200 KB
    print(f'PASS result={args.result}',flush=True)


if __name__=='__main__':main()
