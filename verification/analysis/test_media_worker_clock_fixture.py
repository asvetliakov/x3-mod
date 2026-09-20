"""Bounded host witnesses; synthetic test timestamps never drive native playback."""
import copy
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

import media_worker_clock_evidence as e

ROOT=Path(__file__).resolve().parents[2]


def event(name,label='',at=1,owner='main',instance=0,session=1,**values):
    r=dict(name=name,label=label,begin=at,end=at,owner=owner,instance=instance,session=session,thread=1,index=0,hr='00000000',**dict.fromkeys('abcdef',0));r.update(values)
    return {k:str(v) for k,v in r.items()}


class LeaseEvidence(unittest.TestCase):
    def witness(self):
        events=[];clocks=[];selected=[]
        for ident in (0,1):
            for label,at,owner in [('writing',10,'worker'),('ready',20,'worker'),('reading',30,'main'),('free',60,'main')]:
                events.append(event('slot',label,at,owner,ident,a=0,b=1,c=0,d=0 if owner=='worker' else 1,e=400000,f=e.FRAME_BYTES))
            events.append(event('graphics','texture_unlock',50,instance=ident,a=1,b=0,end=55))
            events.append(event('worker_exit','observed',70,instance=ident,a=0,b=1))
            events.extend(event('cpu_final','slot',80,instance=ident,a=i,b=0) for i in range(3))
            clocks.extend([dict(name='submit',instance=str(ident),a='1',b='1',c='0',d='400000',qpc='35',queued='1'),dict(name='update',instance=str(ident),qpc='40',consumed='1',queued='0')])
            selected.append(dict(instance=str(ident),token='1',qpc='40',tick='1'))
        return {'MC_EVENT':events,'MC_CLOCK':clocks},selected

    def tail_lease_witness(self):
        rows,selected=self.witness();rows['MC_CLOCK']=[];rows['MC_EVENT']=[r for r in rows['MC_EVENT'] if r['instance']=='0'];selected=selected[:1]
        rows['MC_HEADER']=[dict(kind=e.EOF_KIND,transport='production_lav_worker')]
        for r in rows['MC_EVENT']:r.update(handle_slot='0',handle_generation='1',operation='303',epoch='1')
        selected[0].update(handle_slot='0',handle_generation='1',operation='303',epoch='1',start='0',stop='400000',frame_generation='1',slot='0',session='1')
        return rows,selected
    def test_tail_selection_bound_to_real_ready_interval(self):
        rows,selected=self.tail_lease_witness();e.validate_leases(rows,selected)
    def test_tail_interval_swap_rejected(self):
        rows,selected=self.tail_lease_witness();selected[0].update(start='400000',stop='800000')
        with self.assertRaisesRegex(ValueError,'actual READY lease'):e.validate_leases(rows,selected)
    def test_tail_selection_cannot_precede_reading(self):
        rows,selected=self.tail_lease_witness();selected[0]['qpc']='25'
        with self.assertRaisesRegex(ValueError,'actual READY lease'):e.validate_leases(rows,selected)
    def test_real_leases_held_until_upload(self):
        rows,selected=self.witness();e.validate_leases(rows,selected)
    def test_free_before_queue_consumption(self):
        rows,selected=self.witness();selected=[]
        for r in rows['MC_EVENT']:
            if r['label']=='free':r['begin']=r['end']='37'
        with self.assertRaisesRegex(ValueError,'while metadata queued'):e.validate_leases(rows,selected)
    def test_free_before_upload(self):
        rows,selected=self.witness()
        for r in rows['MC_EVENT']:
            if r['label']=='free':r['begin']=r['end']='45'
        with self.assertRaisesRegex(ValueError,'before texture upload'):e.validate_leases(rows,selected)
    def test_cross_session_read_rejected(self):
        rows,selected=self.witness();next(r for r in rows['MC_EVENT'] if r['label']=='reading')['d']='2'
        with self.assertRaisesRegex(ValueError,'READING without READY'):e.validate_leases(rows,selected)
    def test_worker_cannot_free_reading(self):
        rows,selected=self.witness();next(r for r in rows['MC_EVENT'] if r['label']=='free')['owner']='worker'
        with self.assertRaises(ValueError):e.validate_leases(rows,selected)
    def test_final_requires_actual_worker_exit(self):
        rows,selected=self.witness();rows['MC_EVENT']=[r for r in rows['MC_EVENT'] if r['name']!='worker_exit']
        with self.assertRaisesRegex(ValueError,'exit safely'):e.validate_leases(rows,selected)
    def test_abandoned_sequence_reused_only_after_guard(self):
        rows,selected=self.witness()
        rows['MC_EVENT'][:0]=[event('slot','writing',1,'worker',a=0,b=0,c=0),event('retirement','seek_guard',2,'worker',a=1),event('slot','abandon',3,'worker',a=0,b=0,c=0)]
        e.validate_leases(rows,selected)
        rows['MC_EVENT']=[r for r in rows['MC_EVENT'] if r['name']!='retirement']
        with self.assertRaisesRegex(ValueError,'before actual retirement'):e.validate_leases(rows,selected)



class EventEvidence(unittest.TestCase):
    def witness(self,more=False,negative=False):
        groups={('main',2):[]};rows={'MC_EVENT':[]}
        for ident in (0,1):
            session=2 if negative and ident==0 else 1
            def w(name,label='',at=1,**kw):return event(name,label,at,'worker',ident,session,**kw)
            post=event('command','post',1,instance=ident,a=5 if session==2 else 1,b=1,d=50)
            groups[('main',2)].append(post)
            events=[w('command','take',2,a=5 if session==2 else 1,b=1,d=50)]
            if session==2:events.append(w('facts','partial',3,a=1,hr='80070002'))
            events.append(w('notify','before_drain',4,a=session,b=1,c=0))
            clock=5;count=0;batch=0
            batches=[32,0] if more else [1 if session==2 else 0]
            for amount in batches:
                batch+=1;begin=clock
                for i in range(amount):
                    count+=1;code=3 if session==2 else 10;p1=-2147024894 if session==2 else 33554944
                    events.extend([w('call','worker_event_poll',clock),w('provider_event','scalar',clock+1,a=1,b=count,c=code,d=p1),w('call','worker_event_free',clock+2)])
                    clock+=3
                    if session==2:events.append(w('source_error','attributable_event',clock,a=code,b=p1));clock+=1
                empty=amount<32
                if empty:events.append(w('call','worker_event_poll',clock,hr='80004004'));clock+=1
                events.append(w('event_batch','Empty' if empty else 'More',begin,end=clock,a=1,b=batch,c=amount,d=50,e=60));clock+=1
            events.append(w('drain','cleanup_stopped',5,end=clock,a=1,b=1,c=1,d=50,e=count,f=batch))
            for i,r in enumerate(events):
                r['index']=str(i)
                if r['name']=='event_batch':r['f']=str(131072-i)
            groups[('worker',ident)]=events;rows['MC_EVENT'].extend(events)
        rows['MC_EVENT'].extend(groups[('main',2)])
        return rows,groups
    def test_empty_means_observed_e_abort(self):
        rows,groups=self.witness();self.assertEqual(len(e.validate_events(rows,groups)),2)
    def test_more_is_fairness_not_capacity(self):
        rows,groups=self.witness(more=True);self.assertEqual(e.validate_events(rows,groups)[0]['events'],32)
    def test_expected_failed_load_event_freed_and_attributed(self):
        rows,groups=self.witness(negative=True);self.assertEqual(e.validate_events(rows,groups)[0]['events'],1)
    def test_expected_error_cannot_cross_instances(self):
        rows,groups=self.witness(negative=True);groups[('worker',0)],groups[('worker',1)]=groups[('worker',1)],groups[('worker',0)]
        with self.assertRaises(ValueError):e.validate_events(rows,groups)
    def test_failed_free_cannot_be_hidden(self):
        rows,groups=self.witness(negative=True);next(r for r in groups[('worker',0)] if r['label']=='worker_event_free')['hr']='80004005'
        with self.assertRaisesRegex(ValueError,'freed exactly once'):e.validate_events(rows,groups)
    def test_empty_without_e_abort_rejected(self):
        rows,groups=self.witness();next(r for r in groups[('worker',0)] if r['label']=='worker_event_poll')['hr']='00040001'
        with self.assertRaises(ValueError):e.validate_events(rows,groups)
    def test_deadline_anchor_cannot_reset(self):
        rows,groups=self.witness();next(r for r in groups[('worker',0)] if r['name']=='drain')['d']='51'
        with self.assertRaisesRegex(ValueError,'deadline reset'):e.validate_events(rows,groups)
    def test_command_deadline_payload_bound(self):
        rows,groups=self.witness();groups[('worker',0)][0]['d']='51'
        with self.assertRaisesRegex(ValueError,'mailbox payload changed'):e.validate_events(rows,groups)
    def test_notification_disabled_rejected(self):
        rows,groups=self.witness();next(r for r in groups[('worker',0)] if r['name']=='notify')['c']='1'
        with self.assertRaisesRegex(ValueError,'queue flags'):e.validate_events(rows,groups)
    def test_retirement_reserve_rejected(self):
        rows,groups=self.witness();next(r for r in groups[('worker',0)] if r['name']=='event_batch')['f']='511'
        with self.assertRaisesRegex(ValueError,'reserve'):e.validate_events(rows,groups)
    def test_nonload_provider_error_rejected(self):
        rows,groups=self.witness(negative=True);next(r for r in groups[('worker',0)] if r['name']=='provider_event')['d']='-2147467259'
        with self.assertRaisesRegex(ValueError,'unattributable'):e.validate_events(rows,groups)


class RenderEvidence(unittest.TestCase):
    def setUp(self):self.tmp=tempfile.TemporaryDirectory();self.output=Path(self.tmp.name)
    def tearDown(self):self.tmp.cleanup()
    def witness(self):
        events=[event('render_setup','point_1to1',a=1024,b=512,c=21,d=2)]
        captures=[];selected=[];expected={}
        for ident in (0,1):
            events.append(event('texture','identity',instance=ident,a=100+ident))
            tick=ident+1;start=ident*400000;raw=bytes([ident+1,20,30,255])*(e.FRAME_BYTES//4)
            name=f'clock-f{ident:02}.bgra';(self.output/name).write_bytes(raw)
            captures.append(dict(index=str(ident),file=name,bytes=str(len(raw)),written='1',instance=str(ident),tick=str(tick),generation='1',sequence='0',start=str(start),end=str(start+400000)))
            selected.append(dict(instance=str(ident),tick=str(tick),frame_generation='1',token='1',qpc=str(tick*100),start=str(start),stop=str(start+400000)))
            expected[(start,start+400000)]=dict(rgb_sha256=e.worker.rgb_digest(raw))
            for j,label in enumerate(('texture_lock','texture_rows','texture_unlock')):events.append(event('graphics',label,tick*100+1+j,instance=ident,a=tick,b=0))
        for tick in (1,2,3):
            for ident in range(min(tick,2)):
                events.append(event('draw','texture',tick*100+10,instance=ident,end=tick*100+11,a=tick,b=100+ident,c=ident))
                events.append(event('render_region','selection' if tick<3 else 'hold',tick*100+25,instance=ident,a=tick,b=ident,c=int(tick==ident+1),d=1))
            for j,label in enumerate(('end_scene','readback_transfer','readback_lock','snapshot_compare','readback_unlock','compose_present')):events.append(event('graphics',label,tick*100+20+j,a=tick))
            events.append(event('heartbeat','draw_present',tick*100+10,end=tick*100+30,a=tick,c=1))
        rows={'MC_EVENT':events,'MC_CAPTURE':captures,'MC_RESULT':[dict(captures='2',readbacks='3',transition_readbacks='1')]}
        return rows,selected,expected
    def tail_witness(self):
        rows,selected,expected=self.witness()
        rows['MC_HEADER']=[dict(kind=e.EOF_KIND,transport='production_lav_worker')]
        rows['MC_CAPTURE']=rows['MC_CAPTURE'][:1];selected=selected[:1]
        rows['MC_RESULT'][0]['captures']='1';rows['MC_RESULT'][0]['transition_readbacks']='2'
        rows['MC_EVENT']=[r for r in rows['MC_EVENT'] if not(r['instance']=='1' and r['name'] in ('graphics','draw','render_region'))]
        (self.output/'clock-f01.bgra').unlink()
        return rows,selected,expected
    def test_tail_actual_rendered_region_without_second_instance(self):
        rows,selected,expected=self.tail_witness()
        self.assertFalse(e.validate_render(rows,selected,self.output,expected)['actual_two_destinations_sampled'])
    def test_tail_missing_a_rejected(self):
        rows,selected,expected=self.tail_witness();rows['MC_CAPTURE']=[];rows['MC_RESULT'][0]['captures']='0'
        with self.assertRaises(ValueError):e.validate_render(rows,[],self.output,expected)
    def test_tail_extra_b_rejected(self):
        rows,selected,expected=self.witness();rows['MC_HEADER']=[dict(kind=e.EOF_KIND,transport='production_lav_worker')]
        with self.assertRaisesRegex(ValueError,'absent/extra instance'):e.validate_render(rows,selected,self.output,expected)
    def test_two_sampled_regions_and_held_peer(self):
        rows,selected,expected=self.witness();self.assertTrue(e.validate_render(rows,selected,self.output,expected)['actual_two_destinations_sampled'])
    def test_destination_swap_rejected(self):
        rows,selected,expected=self.witness();next(r for r in rows['MC_EVENT'] if r['name']=='draw')['b']='101'
        with self.assertRaisesRegex(ValueError,'sampled'):e.validate_render(rows,selected,self.output,expected)
    def test_held_texture_must_still_draw(self):
        rows,selected,expected=self.witness();rows['MC_EVENT']=[r for r in rows['MC_EVENT'] if not(r['name']=='draw' and r['a']=='3')]
        with self.assertRaisesRegex(ValueError,'retained/drawn'):e.validate_render(rows,selected,self.output,expected)
    def test_held_region_change_rejected(self):
        rows,selected,expected=self.witness();next(r for r in rows['MC_EVENT'] if r['name']=='render_region' and r['a']=='3')['d']='0'
        with self.assertRaisesRegex(ValueError,'held destination'):e.validate_render(rows,selected,self.output,expected)
    def test_exact_rgb_not_cpu_metadata(self):
        rows,selected,expected=self.witness();(self.output/'clock-f00.bgra').write_bytes(bytes([7,20,30,255])*(e.FRAME_BYTES//4))
        with self.assertRaisesRegex(ValueError,'exact RGB/alpha'):e.validate_render(rows,selected,self.output,expected)
    def test_opaque_alpha_required(self):
        rows,selected,expected=self.witness();(self.output/'clock-f00.bgra').write_bytes(bytes([1,20,30,0])*(e.FRAME_BYTES//4))
        with self.assertRaisesRegex(ValueError,'exact RGB/alpha'):e.validate_render(rows,selected,self.output,expected)
    def test_upload_after_draw_rejected(self):
        rows,selected,expected=self.witness();r=next(r for r in rows['MC_EVENT'] if r['label']=='texture_unlock');r['end']='112'
        with self.assertRaisesRegex(ValueError,'before draw'):e.validate_render(rows,selected,self.output,expected)
    def test_missing_actual_render_readback_rejected(self):
        rows,selected,expected=self.witness();rows['MC_EVENT']=[r for r in rows['MC_EVENT'] if not(r['label']=='readback_transfer' and r['a']=='1')]
        with self.assertRaises(ValueError):e.validate_render(rows,selected,self.output,expected)
    def test_wrong_generation_rejected(self):
        rows,selected,expected=self.witness();rows['MC_CAPTURE'][0]['generation']='2'
        with self.assertRaisesRegex(ValueError,'clock token'):e.validate_render(rows,selected,self.output,expected)
    def test_unbound_or_extra_capture_rejected(self):
        rows,selected,expected=self.witness();(self.output/'extra.bgra').write_bytes(b'x')
        with self.assertRaisesRegex(ValueError,'artifact'):e.validate_render(rows,selected,self.output,expected)


class FailedSourceEvidence(unittest.TestCase):
    def witness(self):
        def w(name,label,at,**kw):return event(name,label,at,'worker',0,2,**kw)
        return [w('call','lav_load',1,hr='80070002'),w('source_error','published',2,a=1,hr='80070002'),w('facts','cleanup',3,a=1,hr='80070002'),w('call','partial_stop',4),w('call','partial_stream_stop',5),w('call','partial_state',6),w('retirement','preconnection',7,a=1,d=0)]
    def test_real_load_failure_with_public_partial_retirement(self):e.validate_failed_source(self.witness())
    def test_load_hresult_mismatch_rejected(self):
        events=self.witness();events[0]['hr']='80004005'
        with self.assertRaisesRegex(ValueError,'actual Load'):e.validate_failed_source(events)
    def test_attempted_connection_cannot_use_partial_guard(self):
        events=self.witness();events[2]['b']='1'
        with self.assertRaisesRegex(ValueError,'preconnection'):e.validate_failed_source(events)
    def test_false_fact_cannot_hide_attempted_run(self):
        events=self.witness();events.append(event('call','run',3,'worker',0,2))
        with self.assertRaisesRegex(ValueError,'contradicts'):e.validate_failed_source(events)
    def test_s_false_stopped_observation_insufficient(self):
        events=self.witness();events[5]['hr']='00000001'
        with self.assertRaisesRegex(ValueError,'Stop/state'):e.validate_failed_source(events)
    def test_no_slot_for_failed_source(self):
        events=self.witness();events.append(event('slot','writing',3,'worker',0,2))
        with self.assertRaisesRegex(ValueError,'CPU storage'):e.validate_failed_source(events)
    def test_other_instance_failure_not_expected(self):
        events=self.witness();events[1]['instance']='1'
        with self.assertRaisesRegex(ValueError,'expected Load'):e.validate_failed_source(events)


class SourceWindows(unittest.TestCase):
    def witness(self):
        r=event('slot','ready',1,'worker',0,1,b=2,c=0,d=22000000,e=22400000,f=e.FRAME_BYTES)
        return {'MC_EVENT':[r]}, {(22000000,22400000):{}}
    def test_unselected_source_still_bound_to_exact_interval(self):
        rows,expected=self.witness();e.validate_source_windows(rows,expected)
    def test_unselected_late_frame_cannot_invent_interval(self):
        rows,expected=self.witness();rows['MC_EVENT'][0]['e']='22400001'
        with self.assertRaisesRegex(ValueError,'frozen window'):e.validate_source_windows(rows,expected)
    def test_stopped_seek_cannot_publish_content(self):
        rows,expected=self.witness();rows['MC_EVENT'][0]['b']='4'
        with self.assertRaisesRegex(ValueError,'allowed epoch'):e.validate_source_windows(rows,expected)
    def test_source_cannot_repeat_picture(self):
        rows,expected=self.witness();rows['MC_EVENT']*=2
        with self.assertRaisesRegex(ValueError,'ordered window'):e.validate_source_windows(rows,expected)



class ProtocolEvidence(unittest.TestCase):
    def witness(self):
        groups={('main',2):[],('worker',0):[],('worker',1):[]};clocks=[]
        inputs={0:[('begin',1,(101,0,280,0)),('pause',1,(0,0,0,0)),('seek',2,(22000000,2480,0,0)),('rate',2,(50000,0,0,0)),('resume',2,(0,0,0,0)),('rate',2,(200000,0,0,0)),('stop',3,(0,0,0,0)),('seek',4,(0,280,0,0))],1:[('begin',1,(202,100000000,10360,1)),('rate',1,(50000,0,0,0)),('update',1,(0,0,0,0)),('restart',2,(0,0,0,0)),('update',2,(0,0,0,0)),('restart',3,(0,0,0,0)),('stop',4,(0,0,0,0))]}
        plans={0:[(1,1,0,6,1),(2,2,22000000,7,1),(2,4,0,6,0),(5,4,0,0,0)],1:[(1,1,100000000,9,1),(2,2,100000000,9,1),(2,3,100000000,9,1),(3,4,0,0,0)]}
        for ident in (0,1):
            for i,(name,gen,args) in enumerate(inputs[ident]):clocks.append(dict(instance=str(ident),name=name,generation=str(gen),tick=str(i),qpc=str(i+1),end='1' if name=='update' else '0',**dict(zip('abcd',map(str,args)))))
            for i,(kind,gen,target,limit,running) in enumerate(plans[ident]):
                groups[('main',2)].append(event('command','post',20+i,instance=ident,a=kind,b=gen,c=target,e=limit,f=running))
                if kind in (1,2):groups[('worker',ident)].append(event('seek_position','integer',30+i,'worker',ident,a=gen,b=target,c=target))
        return {'MC_CLOCK':clocks},groups
    def test_fixed_phases_and_three_automatic_epochs(self):
        rows,groups=self.witness();e.validate_protocol(rows,groups)
    def test_loop_not_next_manager_pass_rejected(self):
        rows,groups=self.witness();next(r for r in rows['MC_CLOCK'] if r['name']=='restart')['tick']='50'
        with self.assertRaisesRegex(ValueError,'next manager pass'):e.validate_protocol(rows,groups)
    def test_source_cannot_run_ahead_of_epoch(self):
        rows,groups=self.witness();groups[('main',2)][1]['begin']='1'
        with self.assertRaisesRegex(ValueError,'preceded clock epoch'):e.validate_protocol(rows,groups)
    def test_b_cannot_add_or_extend_window(self):
        rows,groups=self.witness();groups[('main',2)][5]['e']='10'
        with self.assertRaisesRegex(ValueError,'command plan'):e.validate_protocol(rows,groups)
    def test_wrong_actual_seek_rejected(self):
        rows,groups=self.witness();groups[('worker',1)][1]['c']='100000001'
        with self.assertRaisesRegex(ValueError,'integer seeks'):e.validate_protocol(rows,groups)
    def test_changed_rate_rejected(self):
        rows,groups=self.witness();next(r for r in rows['MC_CLOCK'] if r['instance']=='1' and r['name']=='rate')['a']='100000'
        with self.assertRaisesRegex(ValueError,'protocol changed'):e.validate_protocol(rows,groups)
    def test_missing_fast_rate_is_not_relabeled_changed_protocol(self):
        rows,groups=self.witness();rows['MC_CLOCK']=[r for r in rows['MC_CLOCK'] if not(r['name']=='rate' and r['a']=='200000')];e.validate_protocol(rows,groups)


class ConfigurationEvidence(unittest.TestCase):
    def test_runner_mode_mismatch_fails_before_acceptance(self):
        rows={'MC_HEADER':[dict(qpc_frequency='10000000')]}
        result=e.finish(dict(kind='other',mode='worker-clock-two-textures'),'.',rows,{})
        self.assertFalse(result['worker_clock_accepted']);self.assertIn('configuration differs',result['validation_error'])
    def test_no_fictitious_gpu_or_no_stall_claim(self):
        result=e.costs({'MC_EVENT':[]},10000000)
        self.assertFalse(result['gpu_execution_time_measured']);self.assertFalse(result['universal_no_stall_claim']);self.assertIsNone(result['max_completion_gap_ms'])



class RetirementEvidence(unittest.TestCase):
    def witness(self,seek=False):
        names=('lav_transport_decommit','seek_stop','state_stopped','seek_abort','seek_settled') if seek else ('lav_transport_cleanup_decommit','cleanup_stop','cleanup_stream_stop','cleanup_state','cleanup_abort','cleanup_settled')
        rows=[event('call',name,i+1,'worker',index=i) for i,name in enumerate(names)]
        rows.append(event('retirement','seek_guard' if seek else 'guard',len(rows)+1,'worker',index=len(rows),a=1,c=1,d=0,e=0,f=0))
        return rows
    def test_full_guard_binds_actual_calls(self):e.validate_retirements(self.witness())
    def test_seek_guard_binds_actual_calls(self):e.validate_retirements(self.witness(True))
    def test_pending_actual_settled_cannot_hide_behind_scalar(self):
        for seek in (False,True):
            with self.subTest(seek=seek):
                rows=self.witness(seek);rows[-2]['hr']='00040001'
                with self.assertRaisesRegex(ValueError,'not terminal/paired'):e.validate_retirements(rows)
    def test_stop_s_false_rejected(self):
        rows=self.witness();rows[1]['hr']='00000001'
        with self.assertRaisesRegex(ValueError,'exact S_OK'):e.validate_retirements(rows)
    def test_decommit_missing_rejected(self):
        rows=self.witness()[1:]
        with self.assertRaisesRegex(ValueError,'call absent'):e.validate_retirements(rows)
    def test_valid_terminal_abort_matches_scalar(self):
        rows=self.witness();rows[-3]['hr']='80004004';rows[-1]['e']=str(-2147467260);e.validate_retirements(rows)
    def test_other_session_terminal_call_cannot_substitute(self):
        rows=self.witness();rows[-2]['session']='2'
        with self.assertRaisesRegex(ValueError,'call absent'):e.validate_retirements(rows)



class ServiceEvidence(unittest.TestCase):
    def witness(self):
        groups={}
        for ident in (0,1):
            groups[('worker',ident)]=[event('owner','worker',1,'worker',ident,thread=10+ident),event('service','ready',2,'worker',ident,a=100+ident),event('dd_identity','session',3,'worker',ident,b=100+ident,c=100+ident,d=1),event('cooperative','worker_window',2,'worker',ident,a=0x408,b=10+ident)]
        return groups
    def test_distinct_worker_dd_and_window_observations(self):e.validate_service_identities(self.witness())
    def test_shared_dd_rejected(self):
        groups=self.witness();groups[('worker',1)][1]['a']='100';groups[('worker',1)][2].update(b='100',c='100')
        with self.assertRaisesRegex(ValueError,'shared canonical'):e.validate_service_identities(groups)
    def test_normal_without_multithreaded_rejected(self):
        groups=self.witness();groups[('worker',0)][3]['a']='8'
        with self.assertRaisesRegex(ValueError,'cooperative'):e.validate_service_identities(groups)
    def test_foreign_window_thread_rejected(self):
        groups=self.witness();groups[('worker',0)][3]['b']='11'
        with self.assertRaisesRegex(ValueError,'cooperative'):e.validate_service_identities(groups)
    def test_noncanonical_comparison_rejected(self):
        groups=self.witness();groups[('worker',0)][2].update(b='200',c='200')
        with self.assertRaisesRegex(ValueError,'private DD'):e.validate_service_identities(groups)


class GraphClockEvidence(unittest.TestCase):
    def witness(self):
        return [event('call','set_sync_source',1),event('call','get_sync_source',2),event('clock','setup',3,a=0,b=1),event('seek_position','integer',4,a=1),event('call','get_sync_source',5),event('clock','after_seek',6,a=1,b=1),event('call','get_sync_source',7),event('clock','after_run',8,a=1,b=1)]
    def test_setup_and_each_actual_seek_run_observed(self):e.validate_graph_clocks(self.witness())
    def test_missing_postrun_clock_rejected(self):
        with self.assertRaisesRegex(ValueError,'incomplete'):e.validate_graph_clocks(self.witness()[:-1])
    def test_hidden_clock_pointer_rejected(self):
        rows=self.witness();rows[-1]['b']='0'
        with self.assertRaisesRegex(ValueError,'incomplete'):e.validate_graph_clocks(rows)
    def test_s_false_getter_rejected(self):
        rows=self.witness();rows[-2]['hr']='00000001'
        with self.assertRaisesRegex(ValueError,'not paired'):e.validate_graph_clocks(rows)



class CoverageEvidence(unittest.TestCase):
    def witness(self,two_before):
        def row(name,qpc,**kw):
            r=dict(name=name,qpc=str(qpc),instance='0',a='0',paused='1',intent='1',armed='1',queued='0',generation='1',numerator='00',end='0');r.update({k:str(v) for k,v in kw.items()});return r
        a=[row('pause',10),row('update',20)]
        if two_before:a.append(row('update',25))
        a.extend([row('seek',30,a=22000000),row('update',40,numerator='ff',generation=2),row('update',50,numerator='ff',generation=2)])
        b=[row('begin',1,instance=1,a=202),row('rate',2,instance=1,a=50000)]
        return {'MC_CLOCK':a+b,'MC_EVENT':[]},{('worker',0):[]}
    def test_postseek_updates_do_not_supply_preseek_pause_coverage(self):
        rows,groups=self.witness(False);r=e.coverage(rows,[],groups);self.assertIn('A_paused_position_unchanged',r['missing']);self.assertFalse(r['complete'])
    def test_two_distinct_preseek_updates_supply_pause_coverage(self):
        rows,groups=self.witness(True);r=e.coverage(rows,[],groups);self.assertNotIn('A_paused_position_unchanged',r['missing']);self.assertFalse(r['complete'])
    def test_peer_selection_after_seek_does_not_supply_paused_witness(self):
        rows,groups=self.witness(True)
        selected=[dict(instance='1',qpc='40',frame_generation='1',start='100000000')]
        result=e.coverage(rows,selected,groups)
        self.assertIn('B_fresh_while_A_paused',result['missing'])
    def test_real_peer_selection_between_pause_and_seek_supplies_witness(self):
        rows,groups=self.witness(True)
        selected=[dict(instance='1',qpc='26',frame_generation='1',start='100000000')]
        result=e.coverage(rows,selected,groups)
        self.assertNotIn('B_fresh_while_A_paused',result['missing'])
    def test_real_seek_overlap_not_invented_from_generic_progress(self):
        rows,groups=self.witness(True);r=e.coverage(rows,[],groups);self.assertIn('B_selection_during_A_actual_seek',r['missing'])


class TraceEvidence(unittest.TestCase):
    def witness(self):
        header=dict(kind=e.KIND,instances='2',slots_per_instance='3',capture_limit='40',readback_limit='52',clock_commit='0abe0a44',graph_clock='none_explicit',main_thread='1')
        events=[event('owner','main',1,instance=2,thread=1),event('owner','worker',1,'worker',0,thread=2),event('owner','worker',1,'worker',1,thread=3)]
        traces=[dict(owner=r['owner'],instance=r['instance'],count='1',overflow='0') for r in events]
        return {'MC_HEADER':[header],'MC_EVENT':events,'MC_TRACE':traces}
    def test_distinct_trace_owners(self):e.validate_trace(self.witness())
    def test_reordered_qpc_invalidates_anchor_assumption(self):
        rows=self.witness();rows['MC_EVENT'].append(event('call','x',2,end=2,owner='worker',instance=0,thread=2,index=1));rows['MC_EVENT'][1]['end']='3';rows['MC_TRACE'][1]['count']='2'
        with self.assertRaisesRegex(ValueError,'chronology'):e.validate_trace(rows)
    def test_worker_call_on_main_rejected(self):
        rows=self.witness();rows['MC_EVENT'][0]['name']='call'
        with self.assertRaisesRegex(ValueError,'graph call on main'):e.validate_trace(rows)
    def test_private_worker_thread_not_shared(self):
        rows=self.witness();rows['MC_EVENT'][2]['thread']='2'
        with self.assertRaisesRegex(ValueError,'not distinct'):e.validate_trace(rows)
    def test_buffered_helper_metadata_instance_binding(self):
        rows=e.parse('MC_META instance=1 session=1 MP_LAV_RGB valid=1\nMC_EVENT instance=0 owner=worker name=owner')
        self.assertEqual(rows['MP_LAV_RGB'][0],dict(instance='1',session='1',valid='1'))


if __name__=='__main__':unittest.main()
