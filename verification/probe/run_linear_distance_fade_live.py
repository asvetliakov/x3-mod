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
    """Native opaque station draws of a frame: the sibling before the source (14),
    the overwrite of the left half after it (15)."""
    return [('sibling',(8,16,40,48))] if frame==14 else [('overwrite',(8,16,24,48))] if frame==15 else []


def validate_station(output,trace,fade,emission=1):
    """FADE_STATION lines (native sibling/overwrite confined to their scissor) and,
    with fade on, the per-frame composition refusal histogram of the station
    frames: every station source is eligible and prepared (admitted), no
    readiness/frame-stop/preparation refusal, and the pair (non-producer)
    refusals are the frame's baseline (frame 18: one Asteroid source, nothing
    else) plus the native opaque station draws plus, with emission off, the
    frame's emission sources."""
    rows=[fields(line) for line in output.splitlines() if line.startswith('FADE_STATION ')]
    assert [(int(r['frame']),r['kind'],tuple(map(int,r['rect'].split(',')))) for r in rows]==[(f,k,rect) for f in STATION_FRAMES for k,rect in station_opaque_draws(f)],'station opaque draws'
    for r in rows:
        assert int(r['native'])==1 and int(r['routed'])==0 and int(r['hr'],16)==0
        assert int(r['changed'])>0 and int(r['outside_changed'])==0,(r['frame'],'opaque station draw confined to its scissor')
    result=dict(opaque_draws=[dict(frame=int(r['frame']),kind=r['kind'],rect=r['rect'],changed=int(r['changed'])) for r in rows])
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
        extra=len(station_opaque_draws(frame))+(0 if emission else sum(s[0]=='emission' for s in source_plan(frame)))
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
        assert status[12]==int(row['draws'])==len(expected),'each original source DIP is submitted once'
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


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture',type=Path,required=True);parser.add_argument('--dll',type=Path,required=True)
    parser.add_argument('--programs',type=Path,default=Path('/tmp/x3-shader-sweep/programs'))
    parser.add_argument('--reuse-native',type=Path,help='retained lazy1-fade0-emission0 directory from a parser-stopped run; exact input hashes and every output are revalidated')
    parser.add_argument('--result',type=Path,default=bottle.results_dir(ROOT,create=False)/'linear-distance-fade-live.json')
    args=parser.parse_args();assert bottle.BOTTLE=='X3','new qualification requires X3'
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
