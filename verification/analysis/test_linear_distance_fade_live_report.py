"""Focused positive/negative evidence checks; never execute Wine or rebuild DLLs."""
import copy
import json
from pathlib import Path
import re
import struct
import tempfile
import unittest

import fade_refused_rect
import run_linear_distance_fade_live as live


def line(prefix, **values):
    return prefix+' '+' '.join(f'{k}={v}' for k,v in values.items())


def witness_trace(fade=1,emission=0,rect=None,outside=None,k=live.WITNESS_K):
    """fade_witness/fade_region session-log lines of one fixture process; outside maps
    frame -> covered pixels outside the union (the control's deliberate violation)."""
    trace=[]
    for f in range(live.FRAMES):
        wanted=live.expected_witness(f,fade,emission)
        rects=[rect or (0,0,64,64)]*wanted['rects']
        for i,r in enumerate(rects):
            area=(r[2]-r[0])*(r[3]-r[1]);permille=area*1000//4096
            trace.append(line('fade_region',device=1,frame=f,index=i,bound=int(rect is not None),reason=0 if rect else 3,status='no_scope',hit=0,poisoned=0,evicted=0,depth=0,
                              descriptor='00000000',part='00000000',aabb='0,0,0,0,0,0',vb=7,ib=9,vb_rev=3,ib_rev=1,jittered=1,rect=','.join(map(str,r)),
                              f_permille=permille,f_of='viewport',table_used=0,table_poisoned=0))
        if f%k:continue
        hist=[0]*8
        for r in rects:
            permille=(r[2]-r[0])*(r[3]-r[1])*1000//4096
            hist[0 if permille<=10 else 1 if permille<=20 else 2 if permille<=50 else 3 if permille<=100 else 4 if permille<=250 else 5 if permille<=500 else 6 if permille<1000 else 7]+=1
        sampled=wanted['reason']=='sampled'
        union=(rect[2]-rect[0])*(rect[3]-rect[1]) if rect else 4096
        o=(outside or {}).get(f,0)
        trace.append(line('fade_witness',device=1,frame=f,k=k,sampled=int(sampled),reason=wanted['reason'],result='00000000' if sampled else '00000001',
                          width=64 if sampled else 0,height=64 if sampled else 0,rects=wanted['rects'],rects_prepared=wanted['fade_prepared'],
                          rects_unprepared=wanted['rects']-wanted['fade_prepared'],overflow=0,lines_truncated=0,covered=(1024 if wanted['rects']<2 else 1536) if sampled else 0,
                          outside=o if sampled else 0,union=union if sampled else 0,fade_prepared=wanted['fade_prepared'],emission_prepared=wanted['emission_prepared'],
                          f_hist=','.join(map(str,hist))))
    return '\n'.join(trace)


def timing_trace(fade=1,width=1280,height=768,rect=None):
    """Per-frame linear_composition_frame lines of one timing process."""
    if not fade:return ''
    region=live.rect_area(live.intersect(rect or (0,0,width,height),live.source_scissor(0,width,height)))
    return '\n'.join(line('linear_composition_frame',device=1,frame=f,prepared=live.timing_count(f),linear=live.timing_count(f),native=0,incomplete=0,refused=0,
                          in_place=live.timing_count(f),in_place_linear=live.timing_count(f),in_place_incomplete=0,region_pixels=live.timing_count(f)*region) for f in range(18))


def report(fade=1,emission=1,lazy=1,rect=None):
    required=emission+2*fade
    out=[f'RESULT PASS frames={live.FRAMES} checks=123 restorations=35 taa_reference_frames={live.FRAMES-2} taa_skipped_frames=2',
         'FADE_OPAQUE_RETURN frame=13 matched=1']
    for f in live.RESET_FRAMES:out += ['RESET PASS',line('FADE_RESET',frame=f,refs=3+bin(required).count('1') if required else 0,allocations=4 if required else 0,quarantine=int(bool(required) and f==live.RESET_FRAMES[1]),state_lost=0)]
    out += [line('FADE_REJECTED',frame=f,source_failed=1,taa=0,history_seeded=0,copy_exact=1) for f in live.FAILED_SOURCES]
    out += [line('FADE_EXPORT',frame=live.RESET_FRAMES[1],quarantine=int(bool(required)),state_lost=0)]
    native={i:(.95,.94,.96,1.) for i in range(7)}
    out += [line('FADE_NATIVE',pair=i,before='1,1,1,1',after=','.join(map(str,native[i]))) for i in range(7)]
    survivors={}
    trace=['motion_output_release held=123 released=1']
    for f in range(live.FRAMES):
        sources,stopped=live.expected_sources(f,fade,emission)
        s=[0]*live.STATUS_KEYS
        s[0]=int(bool(required));s[1]=int(bool(required and not stopped))
        for index,key in ((4,'prepared'),(5,'linear'),(6,'native'),(7,'incomplete')):
            s[index]=sum(r[key] for r in sources)
        completed=[r['hr'] for r in sources if r['prepared']]
        s[11]=completed[-1] if completed else 1;s[12]=len(sources)+live.routed_station_draws(f)
        s[13]=sum(r['kind']=='fade' for r in sources)*bool(fade)
        s[14]=sum(r['prepared'] for r in sources if r['kind']=='fade')
        s[15]=sum(r['linear'] for r in sources if r['kind']=='fade')
        s[16]=required;s[18]=s[19]=required|(live.IN_PLACE_POLICY*bool(fade));s[17]=int(stopped)
        s[20]=7+bin(required).count('1') if required else 0;s[21]=4 if required else 0
        s[27]=s[14];s[28]=s[15];s[10]=s[4]-s[27];s[29]=live.expected_region_pixels(f,fade,emission,rect)
        out.append(line('FADE_LIVE',frame=f,fade=fade,emission=emission,draws=len(sources),**{f's{i}':v for i,v in enumerate(s)},hash_alpha='a',hash_motion='m',hash_depth='d',hash_mask='mask'))
        prior=int(bool(required))
        for i,r in enumerate(sources):
            alpha=(.25 if i>=2 else .125) if r['kind']=='emission' else (0. if f==1 else live.fade_alpha(r['pair']))
            out.append(line('FADE_SOURCE',frame=f,source=i,kind=r['kind'],pair=r['pair'],alpha=alpha,overlap=2 if f==18 else 1,fault=r['fault'],hr=f'{r["hr"]:08x}',original_calls=1,prepared=r['prepared'],linear=r['linear'],native=r['native'],mask_before=prior,mask_after=int(r['mask_valid']),hash_mask_before='mask',hash_mask_after='mask'))
            prior=int(r['mask_valid'])
        if f in (1,*live.PRESENT_FRAMES,*live.STATION_FRAMES,*live.MIXED_FRAMES):
            before=(.9,.8,.7,.25) if f==14 else (1.,1.,1.,.5) # frame 14 composes over the native opaque sibling
            for source,(kind,pair,_,_) in enumerate(live.source_plan(f)):
                overlap=2 if f==18 else 1
                if f==1:after=before
                elif kind=='emission':after=live.expected_emission(before,emission)
                elif fade:after=live.expected_composite(before,pair,overlap=overlap)
                elif f in live.PRESENT_FRAMES:after=native[pair][:3]+(before[3],)
                else:
                    alpha=live.fade_alpha(pair);original=[(v-(1-alpha))/alpha for v in native[pair][:3]];after=before
                    for _ in range(overlap):after=tuple(live.component.fp16_rt_store(alpha*v+(1-alpha)*a) for a,v in zip(after,original))+(before[3],)
                for x in ((16,32) if f in (1,*live.PRESENT_FRAMES) else (32,)):
                    out.append(line('FADE_SAMPLE',frame=f,source=source,pair=pair,x=x,y=32,kind=kind,overlap=overlap,alpha=.125 if kind=='emission' else 0 if f==1 else live.fade_alpha(pair),before=','.join(map(str,before)),after=','.join(map(str,after))))
                if source==0:survivors[f]=after
                before=after
        for kind,box,routed in live.station_opaque_draws(f):
            survivor=survivors.get(f,(.9,.8,.7,.25)) if kind=='overwrite' else (1.,1.,1.,.5)
            out.append(line('FADE_STATION',frame=f,kind=kind,rect=','.join(map(str,box)),native=1,routed=routed,draw_index=3,hr='00000000',changed=0 if routed else (box[2]-box[0])*(box[3]-box[1]),outside_changed=0,
                            survivor_x=32,survivor_y=32,survivor_exact=1,survivor=','.join(map(str,survivor))))

        out.append(line('FADE_GEOMETRY',frame=f,ordinary_t=.03125*(f%3)))
        out.append(line('FADE_CAMERA',frame=f,view_translation=f'{.125*(f%5)},0,0'))
        routed=live.routed_station_draws(f);opaque=live.native_station_draws(f)
        trace.append(line('motion_output_frame',frame=f,rt_mode='lazy' if lazy else 'perdraw',draws=2+len(sources)+routed+opaque,routed=1+routed,gate4=opaque+sum(r['kind']=='fade' for r in sources),gate5=routed,apply_failures=0,restore_failures=0,taa_resolved=int(f not in live.FAILED_SOURCES),taa_history=int(f not in (0,25,26,30,32))))
        trace.append(line('linear_material_frame',device=1,frame=f,routed=1+routed,bump_routed=routed,refused=0))
        if f not in live.FAILED_SOURCES:trace.append(line('motion_output_taa_readback',frame=f,result='00000000'))
        if fade:
            # Composition frame/refusal lines (capture on): the station sources
            # are eligible, prepared and linear; the sibling/overwrite frames
            # refuse exactly one more non-producer pair draw.
            fades=[r for r in sources if r['kind']=='fade']
            trace.append(line('linear_composition_frame',device=1,frame=f,prepared=s[4],linear=s[5],native=s[6],incomplete=s[7],refused=3,suppressed=0,exports=0,quarantine=0,state_lost=0,mask_valid=1,
                              fade_eligible=len(fades),fade_prepared=sum(r['prepared'] for r in fades),fade_linear=sum(r['linear'] for r in fades),pool_traffic_estimate_bytes=0,in_place=s[27],in_place_linear=s[28],in_place_incomplete=0,region_pixels=s[29]))
            trace.append(line('linear_composition_refusals',device=1,frame=f,pair=1+live.native_station_draws(f)+(0 if emission else sum(r['kind']=='emission' for r in sources)),permission_scene=0,readiness=0,readers=0,frame_stop=0,preparation=0,prepare_failures=0,composition_failures=0,restore_failures=0,exchange_failures=0,ack_failures=0,recovery_failures=0,
                              last_prepare='00000000',last_prepare_restore='00000001',last_source='00000000',last_composition='00000000',last_restore='00000000',last_exchange='00000001',last_ack='00000001',last_recovery='00000001'))
    out.append(line('FADE_CHECKS',frames=live.FRAMES,submissions=sum(len(live.source_plan(f)) for f in range(live.FRAMES)),qualified=1,benchmark=0))
    return '\n'.join(out),'\n'.join(trace)


class FadeLiveReportTests(unittest.TestCase):
    def test_scope_and_source_order(self):
        self.assertEqual(len(live.FADE_PAIRS),7)
        self.assertEqual(live.FADE_PAIRS[6],('4944d81dfe531b37','64bac8bb307eb896'))
        self.assertEqual(len(live.PROGRAMS),17)
        self.assertEqual(sum(len(live.source_plan(f)) for f in range(live.FRAMES)),44)
        self.assertEqual([live.source_plan(f) for f in live.STATION_FRAMES],
                         [[('fade',6,0,False)],[('fade',6,0,False)],[('emission',0,0,False),('fade',6,0,False)],[('fade',6,0,False),('emission',0,0,False)]])
        for fade in (0,1):
            for emission in (0,1):
                for lazy in (0,1):
                    result=live.validate_functional(*report(fade,emission,lazy),fade,emission,lazy)
                    self.assertEqual((result['frames'],result['sources'],result['samples']),(33,44,27))
                    station=result['station']
                    self.assertEqual([(d['kind'],d['routed']) for d in station['opaque_draws']],[('routed_sibling',1),('sibling',0),('overwrite',0)])
                    if fade:
                        self.assertEqual(station['admitted_sources'],4)
                        self.assertEqual({f:h['pair'] for f,h in station['refusal_histogram'].items()},{14:2,15:2,16:1,17:1} if emission else {14:2,15:2,16:2,17:2})
                        self.assertEqual(station['pair_refusal_baseline'],1)
                    else:self.assertNotIn('refusal_histogram',station)

    def test_station_witness_mutations_are_rejected(self):
        output,trace=report(1,0)
        for old,new in (('FADE_STATION frame=14 kind=sibling rect=8,16,40,48 native=1 routed=0 draw_index=3 hr=00000000 changed=1024 outside_changed=0','FADE_STATION frame=14 kind=sibling rect=8,16,40,48 native=1 routed=0 draw_index=3 hr=00000000 changed=1024 outside_changed=1'),
                        ('changed=512 outside_changed=0','changed=0 outside_changed=0'),('kind=overwrite','kind=sibling'),('kind=sibling','kind=routed_sibling'),
                        ('kind=routed_sibling rect=8,16,40,48 native=1 routed=1 draw_index=3 hr=00000000 changed=0','kind=routed_sibling rect=8,16,40,48 native=1 routed=1 draw_index=3 hr=00000000 changed=7')):
            with self.subTest(field=old):
                self.assertIn(old,output)
                with self.assertRaises(AssertionError):live.validate_functional(output.replace(old,new,1),trace,1,0,1)
        with self.assertRaises(AssertionError):live.validate_functional('\n'.join(r for r in output.splitlines() if not r.startswith('FADE_STATION frame=15 ')),trace,1,0,1)
        # The surviving composed pixel must be bit-exact and equal the oracle-checked sample.
        overwrite=next(r for r in output.splitlines() if r.startswith('FADE_STATION frame=15 kind=overwrite '))
        self.assertIn('survivor_exact=1 survivor=',overwrite)
        survivor=live.fields(overwrite)['survivor']
        for changed in (overwrite.replace('survivor_exact=1','survivor_exact=0'),overwrite.replace('survivor='+survivor,'survivor=1,1,1,0.5')):
            with self.assertRaises(AssertionError):live.validate_functional(output.replace(overwrite,changed),trace,1,0,1)
        frame14=next(r for r in trace.splitlines() if r.startswith('motion_output_frame frame=14 '))
        for old,new in (('routed=2','routed=1'),('gate5=1','gate5=0'),('gate4=2','gate4=1')):
            with self.subTest(field=old):
                self.assertIn(old,frame14)
                with self.assertRaises(AssertionError):live.validate_functional(output,trace.replace(frame14,frame14.replace(old,new)),1,0,1)
        material14=next(r for r in trace.splitlines() if r.startswith('linear_material_frame device=1 frame=14 '))
        with self.assertRaises(AssertionError):live.validate_functional(output,trace.replace(material14,material14.replace('bump_routed=1','bump_routed=0')),1,0,1)
        with self.assertRaises(AssertionError):live.validate_functional(output,trace+'\nfade_refused_rect device=1 frame=14 index=3 refusal=2',1,0,1)
        self.assertEqual(live.native_station_draws(14),1)
        station14=next(r for r in trace.splitlines() if r.startswith('linear_composition_frame device=1 frame=14 '))
        station16=next(r for r in trace.splitlines() if r.startswith('linear_composition_refusals device=1 frame=16 '))
        for base,old,new in ((station14,'fade_prepared=1','fade_prepared=0'),(station14,'fade_linear=1','fade_linear=0'),(station16,'readiness=0','readiness=1'),(station16,'pair=2 ','pair=3 '),(station16,'frame_stop=0','frame_stop=1')):
            with self.subTest(field=old):
                self.assertIn(old,base)
                with self.assertRaises(AssertionError):live.validate_functional(output,trace.replace(base,base.replace(old,new,1)),1,0,1)
        # The station sample composes over the sibling, never the unit background.
        sample=next(r for r in output.splitlines() if r.startswith('FADE_SAMPLE frame=14 '))
        with self.assertRaises(AssertionError):live.validate_functional(output.replace(sample,sample.replace('before=0.9,0.8,0.7,0.25','before=1.0,1.0,1.0,0.25')),trace,1,0,1)
        self.assertEqual(live.fade_alpha(6),.068359375)
        self.assertEqual(live.expected_composite((1.,1.,1.,.5),6)[3],.5)

    def test_missing_duplicate_or_corrupt_rows_refused(self):
        output,trace=report()
        for prefix in ('FADE_LIVE frame=0 ','FADE_SOURCE frame=22 source=2 ', 'FADE_SAMPLE frame=12 source=0 pair=5 x=16 ', 'FADE_SAMPLE frame=15 source=0 pair=6 x=32 ', 'FADE_GEOMETRY frame=13 ', 'FADE_CAMERA frame=4 ', 'FADE_OPAQUE_RETURN ', 'FADE_REJECTED ', 'FADE_RESET ', 'FADE_CHECKS ', 'FADE_NATIVE pair=6 ', 'FADE_STATION ', 'RESET PASS'):
            with self.subTest(prefix=prefix),self.assertRaises(AssertionError):
                live.validate_functional('\n'.join(r for r in output.splitlines() if not r.startswith(prefix)),trace,1,1,1)
        for prefix in ('FADE_LIVE frame=0 ','FADE_SOURCE frame=22 source=2 ','FADE_SAMPLE frame=12 source=0 pair=5 x=16 '):
            row=next(r for r in output.splitlines() if r.startswith(prefix))
            with self.subTest(duplicate=prefix),self.assertRaises(AssertionError):live.validate_functional(output+'\n'+row,trace,1,1,1)
        for old,new in (('s20=9','s20=10'),('s21=4','s21=8'),('original_calls=1','original_calls=2'),('s16=3','s16=1'),('s16=3','s16=7'),('s18=7','s18=3'),('s19=7','s19=3'),('ordinary_t=0.03125','ordinary_t=0'),('matched=1','matched=0'),('overlap=2','overlap=1'),('hr=8876086c','hr=00000000'),('mask_after=0','mask_after=1')):
            with self.subTest(field=old),self.assertRaises(AssertionError):live.validate_functional(output.replace(old,new,1),trace,1,1,1)
        # In place: fade never exchanges, every prepared fade source composes in
        # place, and the region pixels are the scissor-clipped rectangles.
        base=next(r for r in output.splitlines() if r.startswith('FADE_LIVE frame=19 '))
        for old,new in (('s10=0 ','s10=2 '),('s27=2 ','s27=1 '),('s28=2 ','s28=1 '),('s29=2048 ','s29=4096 ')):
            with self.subTest(field=old):
                self.assertIn(old,base)
                with self.assertRaises(AssertionError):live.validate_functional(output.replace(base,base.replace(old,new,1)),trace,1,1,1)
        boxed=next(r for r in output.splitlines() if r.startswith('FADE_LIVE frame=20 '))
        self.assertIn('s10=1 ',boxed);self.assertIn('s29=1024 ',boxed)
        control_output,control_trace=report(1,0,1,rect=live.WITNESS_CONTROL_RECT)
        self.assertIn('s29=1536 ',next(r for r in control_output.splitlines() if r.startswith('FADE_LIVE frame=19 ')))
        self.assertEqual(live.validate_functional(control_output,control_trace,1,0,1,live.WITNESS_CONTROL_RECT)['frames'],33)
        with self.assertRaises(AssertionError):live.validate_functional(control_output,control_trace,1,0,1)
        with self.assertRaises(AssertionError):live.validate_functional(output,trace.replace('taa_resolved=0','taa_resolved=1',1),1,1,1)

    def test_quarantine_export_and_reset_persist(self):
        output,trace=report(1,1)
        for old,new in (('FADE_EXPORT frame=32 quarantine=1','FADE_EXPORT frame=32 quarantine=0'),
                        ('FADE_RESET frame=32 refs=5 allocations=4 quarantine=1','FADE_RESET frame=32 refs=5 allocations=4 quarantine=0'),
                        ('FADE_RESET frame=32 refs=5','FADE_RESET frame=32 refs=9')):
            with self.subTest(field=old),self.assertRaises(AssertionError):live.validate_functional(output.replace(old,new,1),trace,1,1,1)

    def test_unprepared_hresult_is_s_false(self):
        output,trace=report(0,0)
        self.assertEqual(live.validate_functional(output,trace,0,0,1)['frames'],33)
        with self.assertRaises(AssertionError):live.validate_functional(output.replace('s11=1','s11=0',1),trace,0,0,1)
        output,trace=report(1,1)
        self.assertIn('s11=1',next(r for r in output.splitlines() if r.startswith('FADE_LIVE frame=0 ')))
        self.assertIn('s11=0',next(r for r in output.splitlines() if r.startswith('FADE_LIVE frame=1 ')))

    def test_no_healing_and_failed_source_contract(self):
        for frame in (22,23,27):
            rows,stopped=live.expected_sources(frame,1,1)
            self.assertTrue(stopped)
            self.assertEqual(rows[-1]['prepared'],0)
            self.assertFalse(rows[-1]['mask_valid'])
        # A composite fault on the in-place fade recovers A|R and blocks the
        # frame (Incomplete), never a certified native publication.
        rows,stopped=live.expected_sources(24,1,1)
        self.assertEqual((rows[0]['prepared'],rows[0]['native'],rows[0]['linear'],rows[0]['incomplete']),(1,0,0,1))
        self.assertFalse(rows[0]['mask_valid']);self.assertTrue(stopped)
        self.assertEqual(live.expected_region_pixels(24,1,1),1024)
        self.assertEqual(live.expected_region_pixels(19,1,0,live.WITNESS_CONTROL_RECT),1536)
        self.assertEqual(live.expected_region_pixels(25,1,1),1024)
        # Station frames: every station source is prepared and composes in place.
        for frame in live.STATION_FRAMES:
            rows,stopped=live.expected_sources(frame,1,1)
            self.assertFalse(stopped)
            self.assertTrue(all(r['prepared'] and r['linear'] for r in rows if r['kind']=='fade'))
        self.assertEqual(live.expected_region_pixels(16,1,1),1024)
        for fade in (0,1):
            rows,_=live.expected_sources(25,fade,1)
            self.assertEqual(rows[0]['hr'],0x8876086c)
            self.assertEqual(rows[0]['incomplete'],fade)
            self.assertEqual(rows[1]['prepared'],0)
            self.assertEqual(rows[1]['mask_valid'],not fade)

    def test_independent_linear_witness_rejects_native_or_wrong_alpha(self):
        output,_=report()
        samples=[live.fields(r) for r in output.splitlines() if r.startswith('FADE_SAMPLE ')]
        native=live.native_baselines([live.fields(r) for r in output.splitlines() if r.startswith('FADE_NATIVE ')])
        for field,value in (('after','1,1,1,.5'),('pair','5'),('alpha','.125')):
            changed=copy.deepcopy(samples);changed[2][field]=value
            with self.subTest(field=field),self.assertRaises(AssertionError):live.validate_samples(changed,True,True,native)
        changed=copy.deepcopy(samples);changed[0]['after']='1,1,1,.5001'
        with self.assertRaises(AssertionError):live.validate_samples(changed,False,True,native)
        off,_=report(0,1)
        native_samples=[live.fields(r) for r in off.splitlines() if r.startswith('FADE_SAMPLE ')]
        native_samples[2]['after']='.949,.94,.96,.5'
        with self.assertRaises(AssertionError):live.validate_samples(native_samples,False,True,native)
        # Independently pinned qualified values ensure the reused case is not a
        # zero-gain variant or a value inferred from actual composed A.
        self.assertEqual(live.expected_composite((1.,1.,1.,.5),0)[:3],(.96728515625,.9638671875,.97021484375))
        self.assertEqual(live.expected_composite((1.,1.,1.,.5),5)[:3],(.96728515625,.9638671875,.96923828125))

    def test_raw_temporal_and_mask_corruption_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            work=Path(tmp);(work/'x3-modern-captures').mkdir()
            for frame in range(live.FRAMES):
                if frame not in live.FAILED_SOURCES:
                    for path in (work/f'reference_taa_{frame}.rgba16f',work/'x3-modern-captures'/f'taa_1_{frame}.rgba16f'):path.write_bytes(bytes(64*64*8))
                (work/f'distance_fade_color_{frame}.rgba32f').write_bytes(struct.pack('<4f',1.,1.,1.,.5)*(64*64))
                (work/f'distance_fade_mask_{frame}.rgba32f').write_bytes(bytes(64*64*16))
            result=live.validate_pixels(work,0,0)
            self.assertEqual(sum(result['covered_pixels']),0)
            self.assertEqual(len(result['temporal_sha256']),live.FRAMES)
            for frame in live.FAILED_SOURCES:self.assertIsNone(result['temporal_sha256'][frame])
            path=work/'x3-modern-captures/taa_1_0.rgba16f';path.write_bytes(b'X'+path.read_bytes()[1:])
            with self.assertRaises(AssertionError):live.validate_pixels(work,0,0)
            path.write_bytes(bytes(64*64*8))
            path=work/'distance_fade_mask_0.rgba32f';path.write_bytes(struct.pack('<f',1.)+path.read_bytes()[4:])
            with self.assertRaises(AssertionError):live.validate_pixels(work,0,0)

    def test_retained_native_requires_identical_inputs(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);work=root/'lazy1-fade0-emission0';work.mkdir()
            (work/'stdout.txt').write_text('retained')
            (work/'fixture.exe').write_bytes(b'fixture');(work/'d3d9.dll').write_bytes(b'dll')
            hashes={'/frozen/fixture.exe':live.sha(work/'fixture.exe'),'/frozen/d3d9.dll':live.sha(work/'d3d9.dll')}
            marker={'inputs':hashes,'passed':False,'raw':str(root.resolve())}
            (root/'failed-result.json').write_text(json.dumps(marker))
            self.assertEqual(live.reusable_native(work,hashes),work.resolve())
            with self.assertRaises(AssertionError):live.reusable_native(work,dict(hashes,dll='changed'))
            with self.assertRaises(AssertionError):live.reusable_native(root/'lazy1-fade1-emission0',hashes)
            marker['passed']=True;(root/'failed-result.json').write_text(json.dumps(marker))
            with self.assertRaises(AssertionError):live.reusable_native(work,hashes)
            marker['passed']=False;(root/'failed-result.json').write_text(json.dumps(marker))
            (work/'d3d9.dll').write_bytes(b'changed executed DLL')
            with self.assertRaises(AssertionError):live.reusable_native(work,hashes)

    def test_timing_only_same_fenced_windows(self):
        out=['RESULT PASS frames=18 checks=1','FADE_CHECKS frames=18 submissions=126 benchmark=1']
        for count in live.COUNTS:
            for sample in range(4):out.append(line('FADE_TIMING',width=1280,height=768,count=count,sample=sample,fade=1,emission=0,pair=0,source_ms=1.,terminal_ms=2.,total_ms=3.))
        text='\n'.join(out);trace=timing_trace()
        result=live.validate_timing(text,trace,1,1280,768)
        self.assertEqual(set(result['counts']),{'1','4','16'})
        self.assertEqual((result['region_pixels_per_bracket'],result['region_fraction']),(640*384,.25))
        for old,new in (('total_ms=3.0','total_ms=4.0'),('source_ms=1.0','source_ms=nan'),('emission=0','emission=1'),('count=16','count=8'),('sample=0','sample=1')):
            with self.subTest(field=old),self.assertRaises(AssertionError):live.validate_timing(text.replace(old,new,1),trace,1,1280,768)
        for old,new in (('in_place=16','in_place=15'),('in_place_linear=4','in_place_linear=3'),('in_place_incomplete=0','in_place_incomplete=1'),('region_pixels=245760','region_pixels=245761'),('frame=17','frame=18')):
            with self.subTest(field=old):
                self.assertIn(old,trace)
                with self.assertRaises(AssertionError):live.validate_timing(text,trace.replace(old,new,1),1,1280,768)
        with self.assertRaises(AssertionError):live.validate_timing(text,'',1,1280,768)
        with self.assertRaises(AssertionError):live.validate_timing(text.replace('fade=1','fade=0'),trace,0,1280,768)
        self.assertEqual(live.validate_timing(text.replace('fade=1','fade=0'),'',0,1280,768)['region_fraction'],0.)
        # Injected fractions stay inside the timed source's scissor and hit the requested area.
        for (fraction,side),(w,h) in zip(live.TIMING_FRACTIONS,((1280,768),(1920,1080))):
            rect=live.timing_rect(w,h,side)
            if side is None:self.assertIsNone(rect);continue
            boxed=live.validate_timing(text.replace('1280','1920').replace('768','1080') if w==1920 else text,timing_trace(1,w,h,rect),1,w,h,rect)
            self.assertAlmostEqual(boxed['region_fraction'],float(fraction),delta=.0005)
        self.assertEqual(live.timing_name(1920,1080,1,1,'0.06'),'timing-1920x1080-pair1-fade1-f0.06')
        self.assertEqual(live.timing_name(1920,1080,1,0),'timing-1920x1080-pair1-fade0')
        self.assertEqual(live.timing_name(1280,768,0,1,producer=6),'timing-1280x768-station-pair0-fade1')
        # The station producer's windows are labelled and validated by pair.
        station=live.validate_timing(text.replace('pair=0','pair=6'),trace,1,1280,768,pair=6)
        self.assertEqual(station['pair'],6)
        with self.assertRaises(AssertionError):live.validate_timing(text,trace,1,1280,768,pair=6)

    def test_witness_control_native_strip_changes_only_the_composed_color(self):
        n=live.FRAMES
        base=dict(temporal_sha256=[None if f in live.FAILED_SOURCES else f'h{f}' for f in range(n)],alpha_sha256=['a']*n,covered_pixels=[1]*n,frames_detail=['d']*n)
        cases={name:copy.deepcopy(base) for name in ('lazy1-fade1-emission0','witness-full','witness-rect','witness-control')}
        with self.assertRaises(AssertionError):live.compare_witness(cases)
        for f in range(min(live.WITNESS_CONTROL_VIOLATIONS),n):
            if f not in live.FAILED_SOURCES:cases['witness-control']['temporal_sha256'][f]=f'native{f}'
        live.compare_witness(cases)
        broken=copy.deepcopy(cases);broken['witness-control']['temporal_sha256'][2]='native2'
        with self.assertRaises(AssertionError):live.compare_witness(broken)
        broken=copy.deepcopy(cases);broken['witness-control']['alpha_sha256'][16]='changed'
        with self.assertRaises(AssertionError):live.compare_witness(broken)
        broken=copy.deepcopy(cases);broken['witness-rect']['temporal_sha256'][16]='changed'
        with self.assertRaises(AssertionError):live.compare_witness(broken)

    def test_witness_positive_histogram_and_revisions(self):
        result=live.validate_witness(witness_trace(),1,0)
        self.assertEqual(result['outside_pixels'],0)
        self.assertEqual(set(result['sampled_frames']),{f for f in range(live.FRAMES) if live.expected_witness(f,1,0)['reason']=='sampled'})
        self.assertEqual(result['skipped'],{'no_fade':7,'mask_invalid':4}) # frame 14 now derives a station rectangle
        self.assertEqual(sum(result['f_histogram'].values()),len(result['sampled_frames'])+sum(1 for f in (19,23,27)))
        self.assertEqual(result['f_histogram']['f=1'],sum(result['f_histogram'].values()))
        self.assertEqual(result['revisions_per_vb'],{'7':{'3':result['region_lines']}})
        boxed=live.validate_witness(witness_trace(rect=live.WITNESS_RECT),1,0,rect=live.WITNESS_RECT)
        self.assertEqual(boxed['union_area'],1536*len(boxed['sampled_frames']))
        self.assertEqual(boxed['f_histogram']['f<=0.5'],sum(boxed['f_histogram'].values()))
        second=lambda f:any(i>=1 and s['kind']=='fade' and s['prepared'] for i,s in enumerate(live.expected_sources(f,1,0)[0]))
        self.assertEqual({f for f in range(live.FRAMES) if second(f) and live.expected_witness(f,1,0)['reason']=='sampled'},set(live.WITNESS_CONTROL_VIOLATIONS))

    def test_witness_control_fails_for_outside_pixels_only(self):
        trace=witness_trace(rect=live.WITNESS_CONTROL_RECT,outside=live.WITNESS_CONTROL_VIOLATIONS)
        with self.assertRaises(live.WitnessViolation) as raised:live.validate_witness(trace,1,0,rect=live.WITNESS_CONTROL_RECT)
        self.assertEqual(raised.exception.violations,live.WITNESS_CONTROL_VIOLATIONS)
        self.assertIn('outside the union',str(raised.exception))
        with self.assertRaises(live.WitnessViolation):live.validate_witness(witness_trace(outside={2:1}),1,0)

    def test_witness_hostile_lines_refused(self):
        trace=witness_trace(rect=live.WITNESS_RECT)
        base=next(r for r in trace.splitlines() if r.startswith('fade_witness device=1 frame=2 '))
        for old,new in (('sampled=1 reason=sampled','sampled=0 reason=sampled'),('result=00000000','result=8876086c'),('overflow=0','overflow=1'),
                        ('rects=1 ','rects=2 '),('rects_prepared=1','rects_prepared=0'),('rects_unprepared=0','rects_unprepared=1'),('lines_truncated=0','lines_truncated=1'),
                        ('fade_prepared=1','fade_prepared=0'),('width=64','width=32'),('union=1536','union=1535'),('k=1 ','k=2 ')):
            with self.subTest(field=old):
                self.assertIn(old,base)
                with self.assertRaises(AssertionError):live.validate_witness(trace.replace(base,base.replace(old,new,1)),1,0,rect=live.WITNESS_RECT)
        for old,new in (('reason=mask_invalid','reason=sampled'),('reason=no_fade','reason=emission')):
            with self.subTest(field=old),self.assertRaises(AssertionError):live.validate_witness(trace.replace(old,new,1),1,0,rect=live.WITNESS_RECT)
        with self.assertRaises(AssertionError):live.validate_witness(trace.replace('rect=8,16,56,48','rect=8,16,55,48',1),1,0,rect=live.WITNESS_RECT)
        with self.assertRaises(AssertionError):live.validate_witness('\n'.join(r for r in trace.splitlines() if r!=base),1,0,rect=live.WITNESS_RECT)
        with self.assertRaises(AssertionError):live.validate_witness('\n'.join(r for r in trace.splitlines() if not r.startswith('fade_region device=1 frame=2 ')),1,0,rect=live.WITNESS_RECT)
        with self.assertRaises(AssertionError):live.validate_witness(witness_trace(emission=1),1,0)

    def test_fade_refused_rect_lines_parse(self):
        # The exact capture-only line src/proxy/motion_output.cpp writes after
        # Present for a recognised source-over draw that admission refused
        # (docs/architecture/linear-station-source-over.md, section 4).
        def rect_line(frame,index,refusal=4,rect='412,236,701,455',bound=1,reason=0,status='bound',permille=64,f_of='viewport',total=1,node=1497592832,model='00001538',lod='00000002'):
            return (f'fade_refused_rect device=1 frame={frame} index={index} refusal={refusal} vs=4944d81dfe531b37 ps=64bac8bb307eb896'
                    f' node={node} model={model} lod={lod} bound={bound} reason={reason} status={status} rect={rect} f_permille={permille} f_of={f_of} refused_total={total}')
        lines=['fade_region device=1 frame=13681 index=40 bound=1',rect_line(13681,44),rect_line(13681,51,refusal=2,rect='0,0,1280,768',bound=0,reason=3,status='no_record',permille=1000,total=2),
               'motion_route device=1 frame=13681 index=44 gate=4 routed=0',rect_line(14601,17,total=1)]
        records=fade_refused_rect.parse(lines)
        self.assertEqual([(r.frame,r.index,r.refusal_name,r.rect,r.area) for r in records],
                         [(13681,44,'frame_stop',(412,236,701,455),289*219),(13681,51,'readiness',(0,0,1280,768),1280*768),(14601,17,'frame_stop',(412,236,701,455),289*219)])
        first=records[0]
        self.assertEqual((first.vs,first.ps,first.node,first.model,first.lod,first.bound,first.reason,first.status,first.f_permille,first.f_of,first.refused_total),
                         ('4944d81dfe531b37','64bac8bb307eb896',1497592832,0x1538,2,True,0,'bound',64,'viewport',1))
        self.assertFalse(records[1].bound)
        self.assertEqual(sorted(fade_refused_rect.by_frame(records)),[(1,13681),(1,14601)])
        self.assertEqual(len(fade_refused_rect.by_frame(records)[(1,13681)]),2)
        self.assertEqual(fade_refused_rect.parse(['shimmer_draw device=1 frame=3 index=1']),[])
        for bad in (rect_line(1,2,rect='10,10,10,20'),rect_line(1,2,rect='1,2,3'),rect_line(1,2,refusal=6),rect_line(1,2,f_of='screen'),rect_line(1,2,permille=1001)):
            with self.subTest(line=bad),self.assertRaises(ValueError):fade_refused_rect.parse([bad])
        with self.assertRaises(KeyError):fade_refused_rect.parse([rect_line(1,2).replace(' rect=412,236,701,455','')])
        # The production format string names every field the parser reads, in this order.
        source=(Path(__file__).resolve().parents[2]/'src/proxy/motion_output.cpp').read_text()
        self.assertIn('fade_refused_rect device=%llu frame=%llu index=%lu refusal=%u vs=%016llx ps=%016llx node=%llu model=%08lx lod=%08lx',source)
        self.assertIn(' bound=%u reason=%u status=%s rect=%ld,%ld,%ld,%ld f_permille=%lu f_of=%s refused_total=%u',source)
        self.assertIn('if (fade && capture_) record_fade_refused(route, refusal);',source)
        self.assertIn('if (fade_refused_count_) log_fade_refused();',source)

    def test_admission_refuses_false_availability(self):
        out=['RESULT PASS frames=4 checks=1']
        for frame in range(4):
            status={f's{i}':int(frame!=0) if i==12 else 0 for i in range(22)}
            out.append(line('FADE_LIVE',frame=frame,fade=1,emission=1,**status))
            if frame:out.append(line('FADE_SOURCE',frame=frame,source=0,hr='00000000',original_calls=1,prepared=0,linear=0))
        output='\n'.join(out);trace='motion_output_release held=10 released=1'
        for taa,hdr in ((0,1),(1,0)):
            self.assertEqual(live.validate_admission(output,trace,taa,hdr)['frames'],4)
            with self.assertRaises(AssertionError):live.validate_admission(output.replace('s16=0','s16=3',1),trace,taa,hdr)


if __name__=='__main__':unittest.main()
