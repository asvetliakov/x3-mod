"""Focused malformed-evidence witnesses for production transport extraction."""
import copy
import hashlib
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
import media_worker_clock_evidence as e
import media_worker_transport_tail_evidence as tail


def row(name,label,at,owner='worker',instance=0,session=1,**values):
    result=dict(name=name,label=label,begin=str(at),end=str(at),owner=owner,instance=str(instance),session=str(session),index='0',thread=str(instance+2 if owner=='worker' else 1),hr='00000000',a='0',b='0',c='0',d='0',e='0',f='0',handle_slot=str(instance),handle_generation=str(session),operation=str(101 if instance==0 else 202),epoch='1')
    result.update({k:str(v) for k,v in values.items()});return result


def shared_fixture(tail_mode=False):
    rows={'MC_HEADER':[dict(kind=e.EOF_KIND if tail_mode else e.KIND,transport='production_lav_worker')],'MC_EVENT':[],'MP_LAV_COHORT':[]}
    groups={('main',2):[]}
    for ident in ((0,) if tail_mode else (0,1)):
        main=groups[('main',2)];events=[]
        main.append(row('service','start',1,'main',ident,a=100+ident))
        for session in ((1,2) if tail_mode else (1,)):
            epoch=session;op=303 if tail_mode else 101 if ident==0 else 202;base=session*100
            identity=dict(epoch=epoch,operation=op,handle_generation=session)
            main.append(row('desired','publish',base,'main',ident,session,a=50+session,b=1,c=1,**identity))
            main.append(row('command','post',base+1,'main',ident,session,a=1,b=epoch,c=19393200000 if tail_mode else 0,d=50+session,e=2,f=1,**identity))
            events.append(row('command','take',base+2,instance=ident,session=session,a=1,b=epoch,c=19393200000 if tail_mode else 0,d=50+session,e=0 if tail_mode else 9 if ident else 6,f=1,**identity))
            if session==1:
                for offset,label,hr in ((3,'provider_module_snapshot','00000000'),(4,'provider_module_first','00000000'),(5,'provider_module_next','00000001'),(6,'provider_snapshot_close','00000000')):
                    events.append(row('call',label,base+offset,instance=ident,session=session,hr=hr,**identity))
                rows['MP_LAV_COHORT'].extend(dict(instance=str(ident),session='1',index=str(i),count='1',same='1',path=f'normalized/module{i}',expected=f'normalized/module{i}') for i in range(9))
            for offset,name,label,kw in ((7,'call','support_seeking',{}),(8,'call','positions_before_seek',{}),(9,'seek_position','before',dict(a=epoch,b=0,c=19395600000)),(10,'call','integer_seek',{}),(11,'call','positions_after_seek',{}),(12,'seek_position','after',dict(a=epoch,b=19393200000,c=19395600000)),(13,'seek_position','integer',dict(a=epoch,b=19393200000,c=19393200000)),(14,'call','stream_run',{})):
                events.append(row(name,label,base+offset,instance=ident,session=session,**identity,**kw))
        if not tail_mode and ident==0:
            events.append(row('source_error','published',400,instance=0,session=2,a=1,hr='80004005',handle_generation=2))
        main.append(row('worker_event','observed',500,'main',ident,2 if ident==0 else 1,a=26 if tail_mode else 28 if ident==0 else 24,b=2,c=0 if tail_mode or ident else -2147467259,d=1,e=1,f=1,operation=303 if tail_mode else 101 if ident==0 else 202,epoch=2 if tail_mode else 1))
        for index,r in enumerate(events):r['index']=str(index)
        groups[('worker',ident)]=events;rows['MC_EVENT'].extend(events)
    rows['MC_EVENT'].extend(groups[('main',2)]);return rows,groups


class SharedContract(unittest.TestCase):
    def test_actual_shared_tuple_cohort_and_terminal(self):
        rows,groups=shared_fixture();e.validate_shared_contract(rows,groups)
    def test_each_fresh_graph_configured(self):
        rows,groups=shared_fixture(True);e.validate_shared_contract(rows,groups)
    def test_second_support_s_false_rejected(self):
        rows,groups=shared_fixture(True)
        next(r for r in groups[('worker',0)] if r['label']=='support_seeking' and r['session']=='2')['hr']='00000001'
        with self.assertRaisesRegex(ValueError,'SupportSeeking'):e.validate_shared_contract(rows,groups)
    def test_second_support_after_run_rejected(self):
        rows,groups=shared_fixture(True)
        r=next(r for r in groups[('worker',0)] if r['label']=='support_seeking' and r['session']=='2');r['begin']=r['end']='299'
        with self.assertRaisesRegex(ValueError,'SupportSeeking'):e.validate_shared_contract(rows,groups)
    def test_torn_operation_rejected(self):
        rows,groups=shared_fixture();groups[('worker',0)][0]['operation']='4294967297'
        with self.assertRaisesRegex(ValueError,'payload changed'):e.validate_shared_commands(rows,groups)
    def test_mailbox_busy_cannot_drop_post(self):
        rows,groups=shared_fixture();groups[('worker',1)].pop(0)
        with self.assertRaisesRegex(ValueError,'command lost'):e.validate_shared_commands(rows,groups)
    def test_desired_tuple_independent_of_command(self):
        rows,groups=shared_fixture();next(r for r in groups[('main',2)] if r['name']=='desired')['epoch']='2'
        with self.assertRaisesRegex(ValueError,'coherent desired'):e.validate_shared_commands(rows,groups)
    def test_duplicate_cohort_basename_rejected(self):
        rows,groups=shared_fixture();rows['MP_LAV_COHORT'][0]['count']='2'
        with self.assertRaisesRegex(ValueError,'cohort'):e.validate_shared_contract(rows,groups)
    def test_foreign_cohort_path_rejected(self):
        rows,groups=shared_fixture();rows['MP_LAV_COHORT'][0]['path']='foreign/module0'
        with self.assertRaisesRegex(ValueError,'cohort'):e.validate_shared_contract(rows,groups)
    def test_incomplete_module_enumeration_rejected(self):
        rows,groups=shared_fixture();next(r for r in groups[('worker',0)] if r['label']=='provider_module_next')['hr']='00000000'
        with self.assertRaisesRegex(ValueError,'snapshot incomplete'):e.validate_shared_contract(rows,groups)
    def test_shutdown_cannot_hide_source_error(self):
        rows,groups=shared_fixture();next(r for r in groups[('main',2)] if r['name']=='worker_event')['c']='0'
        with self.assertRaisesRegex(ValueError,'Load failure lost'):e.validate_shared_contract(rows,groups)
    def test_unsafe_retained_never_accepted(self):
        rows,groups=shared_fixture();next(r for r in groups[('main',2)] if r['name']=='worker_event')['a']='60'
        with self.assertRaisesRegex(ValueError,'unsafe'):e.validate_shared_contract(rows,groups)
    def test_getters_after_seek_cannot_prove_unchanged_stop(self):
        rows,groups=shared_fixture();r=next(r for r in groups[('worker',0)] if r['label']=='before');r['begin']=r['end']='113'
        with self.assertRaisesRegex(ValueError,'bracket'):e.validate_position_stop_pairs(groups[('worker',0)])


class SharedAnchors(unittest.TestCase):
    def test_cleanup_uses_post_envelope(self):
        main=[row('desired','shutdown',100,'main',a=50)]
        events=[row('deadline_anchor','shutdown',110,a=50,d=100)]
        e.validate_shared_anchors(events,main)
        events[0]['a']='60'
        with self.assertRaisesRegex(ValueError,'immutable actual post'):e.validate_shared_anchors(events,main)
    def test_no_fresh_pending_poll_deadline(self):
        events=[row('slot','writing',10,b=1,c=0),row('deadline_anchor','sample_request',11,a=50,b=1,c=0),row('call','completion',12)]
        for i,r in enumerate(events):r['index']=str(i)
        with self.assertRaisesRegex(ValueError,'pre-call cancellation'):e.validate_shared_anchors(events,[])
    @staticmethod
    def pre_call_cancellation():
        events=[row('slot','writing',10,b=1,c=0),row('deadline_anchor','sample_request',11,a=50,b=1,c=0),
                row('desired','cancelled_before_call',13),row('call','lav_allocator_decommit',14),row('retirement','cancel_guard',20,a=1)]
        for i,r in enumerate(events):r['index']=str(i)
        return events,[row('desired','publish',12,'main',0,b=1,c=1,epoch=2)]
    def test_cancel_after_reservation_before_actual_update(self):
        events,main=self.pre_call_cancellation();e.validate_shared_anchors(events,main)
    def test_pre_call_cancellation_requires_real_revocation(self):
        events,main=self.pre_call_cancellation();main[0]['epoch']='1'
        with self.assertRaisesRegex(ValueError,'pre-call cancellation'):e.validate_shared_anchors(events,main)
    def test_pre_call_cancellation_requires_safe_guard(self):
        events,main=self.pre_call_cancellation();events[-1]['a']='0'
        with self.assertRaisesRegex(ValueError,'safe retirement'):e.validate_shared_anchors(events,main)
    def test_frame_completion_cannot_invent_new_identity(self):
        events=[row('call','update',10),row('deadline_anchor','frame',11,a=50,b=1,c=0,epoch=2)]
        with self.assertRaisesRegex(ValueError,'actual successful completion'):e.validate_shared_anchors(events,[])


class TailAdmission(unittest.TestCase):
    def witness(self):
        return {'MC_EVENT':[row('tail_selection','actual_lease',i+1,'main',0,i//6+1,a=i+1,b=i,c=tail.TARGET+(i%6)*400000,d=tail.TARGET+(i%6+1)*400000,f=i//6+1,operation=303,epoch=i//6+1) for i in range(12)]}
    def test_twelve_actual_lease_admissions(self):self.assertEqual(len(tail.selections(self.witness())),12)
    def test_six_pictures_not_twelve_via_zip_truncation(self):
        rows=self.witness();rows['MC_EVENT']=rows['MC_EVENT'][:6]
        with self.assertRaisesRegex(ValueError,'twelve'):tail.selections(rows)
    def test_tail_cannot_claim_clock_transactions(self):
        rows=self.witness();rows['MC_CLOCK']=[{}]
        with self.assertRaisesRegex(ValueError,'fabricated'):tail.selections(rows)
    def test_old_session_lease_rejected(self):
        rows=self.witness();rows['MC_EVENT'][6]['handle_generation']='1'
        with self.assertRaisesRegex(ValueError,'identity'):tail.selections(rows)
    def test_runner_header_mode_mismatch_fails(self):
        result=tail.finish({'kind':e.KIND,'mode':'worker-clock-two-textures'},Path('/unused'),{'MC_HEADER':[dict(kind=e.KIND,qpc_frequency='10000000')],'MC_EVENT':[]},{})
        self.assertFalse(result['worker_transport_eof_accepted']);self.assertIn('mode mismatch',result['validation_error'])

class TailBoundaries(unittest.TestCase):
    def witness(self):
        events=[];main=[];clock=10
        expected={(tail.TARGET+i*400000,tail.TARGET+(i+1)*400000):{} for i in range(6)}
        for session in (1,2):
            identity=dict(operation=303,epoch=session)
            def w(name,label='',**kw):
                nonlocal clock
                clock+=2;r=row(name,label,clock,session=session,**identity,**kw);events.append(r);return r
            def m(name,label='',**kw):
                nonlocal clock
                clock+=2;r=row(name,label,clock,'main',0,session,**identity,**kw);main.append(r);return r
            m('command','post',a=1,b=session,c=tail.TARGET,e=2,f=1)
            w('session','begin',a=session)
            for i,(start,end) in enumerate(expected):
                sequence=(session-1)*6+i;index=sequence%3
                w('slot','writing',a=index,b=session,c=sequence)
                w('slot','ready',a=index,b=session,c=sequence,d=start,e=end)
                m('slot','reading',a=index,b=session,c=sequence,d=session)
                m('slot','free',a=index,b=session,c=sequence,d=session)
            w('slot','writing',a=0,b=session,c=session*6)
            w('call','update',hr='00040003')
            w('eof','eof_update',a=session,b=6,c=session*6,hr='00040003')
            w('provider_event','scalar',c=1)
            w('eof','graph_complete',a=session,b=6,c=1)
            w('drain','eof_wait',c=1)
            w('eof','paired_empty',a=session,b=6,c=1)
            w('retirement','guard',a=1,b=0,c=1)
            w('slot','abandon',a=0,b=session,c=session*6)
            w('seek_position','after',c=tail.TARGET+2400000)
            w('session','end',a=session)
            m('worker_event','observed',a=10)
            m('tail_phase','eof_six_free',a=session,b=6,c=3)
        for index,r in enumerate(events):r['index']=str(index)
        groups={('worker',0):events,('main',2):main}
        return {'MC_EVENT':events+main},groups,expected
    def test_real_terminal_twice_and_free_three(self):
        rows,groups,expected=self.witness();self.assertEqual(tail.boundaries(rows,groups,expected)['actual_sample_eos_sessions'],2)
    def test_six_frames_cannot_replace_sample_eos(self):
        rows,groups,expected=self.witness();next(r for r in groups[('worker',0)] if r['label']=='eof_update')['hr']='00000000'
        with self.assertRaisesRegex(ValueError,'actual terminal'):tail.boundaries(rows,groups,expected)
    def test_pending_public_origin_rejected(self):
        rows,groups,expected=self.witness();next(r for r in groups[('worker',0)] if r['name']=='call')['hr']='00040001'
        with self.assertRaisesRegex(ValueError,'public Update'):tail.boundaries(rows,groups,expected)
    def test_duplicate_complete_rejected(self):
        rows,groups,expected=self.witness();extra=copy.deepcopy(next(r for r in groups[('worker',0)] if r['label']=='graph_complete'));groups[('worker',0)].append(extra)
        with self.assertRaisesRegex(ValueError,'duplicated'):tail.boundaries(rows,groups,expected)
    def test_eos_writing_release_requires_guard(self):
        rows,groups,expected=self.witness();r=next(r for r in groups[('worker',0)] if r['label']=='abandon');r['begin']=r['end']='1'
        with self.assertRaisesRegex(ValueError,'physical guard'):tail.boundaries(rows,groups,expected)
    def test_early_second_graph_rejected(self):
        rows,groups,expected=self.witness();r=next(r for r in groups[('worker',0)] if r['name']=='session' and r['label']=='begin' and r['session']=='2');r['begin']=r['end']='1'
        with self.assertRaisesRegex(ValueError,'fresh graph began'):tail.boundaries(rows,groups,expected)
    def test_writing_slot_not_free_at_boundary(self):
        rows,groups,expected=self.witness();r=next(r for r in groups[('main',2)] if r['name']=='tail_phase');r['begin']=r['end']=str(int(r['begin'])-7)
        with self.assertRaises(ValueError):tail.boundaries(rows,groups,expected)

class RuntimeRelocation(unittest.TestCase):
    def test_actual_payload_binding_accepts_relocation_rejects_changed_bytes(self):
        from run_media_worker_clock_fixture import bind_runtime_assets, RUNTIME_FILES
        with TemporaryDirectory() as directory:
            root=Path(directory);local=root/'relocated';local.mkdir();reference=root/'original.mkv';reference.write_bytes(b'known-media');asset=local/'movie.mkv';asset.write_bytes(reference.read_bytes())
            files={}
            for name in RUNTIME_FILES:
                raw=('known-'+name).encode()
                (local/name).write_bytes(raw);files[name]=dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
            graph=dict(files=files);manifest=local/'provider.manifest'
            self.assertEqual(set(bind_runtime_assets(graph,manifest,asset,reference)),set(files))
            asset.write_bytes(b'other-media')
            with self.assertRaisesRegex(ValueError,'source bytes'):bind_runtime_assets(graph,manifest,asset,reference)
            asset.write_bytes(reference.read_bytes());(local/'LAVVideo.ax').write_bytes(b'foreign-codec')
            with self.assertRaisesRegex(ValueError,'cohort bytes'):bind_runtime_assets(graph,manifest,asset,reference)

    def test_installed_notices_and_excluded_sdk_provenance(self):
        from run_media_worker_clock_fixture import bind_runtime_assets, package_selection, RUNTIME_FILES
        import json
        with TemporaryDirectory() as directory:
            root=Path(directory).resolve();prefix='providers/profile';provider=root/'x3-modern-media'/prefix;provider.mkdir(parents=True)
            def blob(path,raw):
                path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw)
                return dict(bytes=len(raw),sha256=hashlib.sha256(raw).hexdigest())
            frozen={};installed={}
            for name in RUNTIME_FILES:
                frozen[name]=blob(provider/name,('known-'+name).encode());installed[name]=dict(frozen[name],path=prefix+'/'+name,role='manifest' if name.endswith('.manifest') else 'dependency')
            for name in ('COPYING','README.md','include/LAVVideoSettings.h','include/LAVSplitterSettings.h'):
                frozen[name]=dict(bytes=5,sha256=hashlib.sha256(b'other').hexdigest())
            for name in ('COPYING','README.md','FFmpeg-LICENSE.md'):
                key='notices/'+name;installed[key]=dict(blob(provider/key,('notice-'+name).encode()),path=prefix+'/'+key,role='notice')
            package={'provider':dict(manifest=prefix+'/provider.manifest',files=installed)}
            package_path=root/'x3-modern-media'/prefix/'package.json';package_meta=blob(package_path,json.dumps(package).encode())
            asset=root/'x3-modern-media/sources/source/movie.mkv';blob(asset,b'known-media');reference=root/'original.mkv';blob(reference,b'known-media')
            source_path=asset.with_name('source.json');source_meta=blob(source_path,json.dumps(dict(id=2,effective_flags=8,asset=str(asset.relative_to(root)))).encode())
            install=dict(media=dict(package_record_relative=str(package_path.relative_to(root)),package_record_sha256=package_meta['sha256'],sources=[dict(source_record_relative=str(source_path.relative_to(root)),source_record_sha256=source_meta['sha256'])]))
            blob(root/'x3-modern-install.json',json.dumps(install).encode())
            manifest,media,protected,record=package_selection(root/'fixture.exe')
            self.assertEqual(len(bind_runtime_assets(dict(files=frozen),manifest,media,reference)),11)
            self.assertEqual(len(protected),17);self.assertEqual(record['source_id'],2)
            self.assertFalse((provider/'include').exists());self.assertFalse((provider/'COPYING').exists())
            (provider/'notices/COPYING').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'notice bytes'):package_selection(root/'fixture.exe')

class InitialQuiescence(unittest.TestCase):
    @staticmethod
    def witness():
        groups={('main',2):[row('heartbeat','',25,'main',2),row('heartbeat','',55,'main',2)]}
        for i in (0,1):
            groups[('worker',i)]=[row('service','ready',5,instance=i,session=0),
                row('desired','assignment_quiescent',15,instance=i,session=0,a=1,d=3,handle_generation=1),
                row('desired','assignment_quiescent',45,instance=i,session=0,a=3,d=3,handle_generation=1)]
            for name,label,at,kw in [('desired','publish',10,dict(d=1)),('quiescence','observed',20,dict(a=1,d=3)),
                ('desired','publish',30,dict(b=1,d=2)),('quiescence','old_fact_refused',31,dict(a=2,b=1)),
                ('desired','publish',40,dict(d=3)),('quiescence','observed',50,dict(a=3,d=3)),
                ('desired','publish',60,dict(b=1,c=1,d=4)),('command','post',61,{})]:
                groups[('main',2)].append(row(name,label,at,'main',i,0,handle_generation=1,**kw))
        groups[('main',2)].sort(key=lambda r:int(r['end']));return groups
    def test_initial_no_graph_fact_twice_and_live_refusal(self):
        self.assertEqual(e.validate_initial_quiescence(self.witness())['canceled_facts'],4)
    def test_stale_serial_cannot_ack_new_cancellation(self):
        groups=self.witness();groups[('worker',1)][2]['a']='1'
        with self.assertRaisesRegex(ValueError,'canceled assignment'):e.validate_initial_quiescence(groups)
    def test_graph_work_before_idle_fact_rejected(self):
        groups=self.witness();groups[('worker',0)].append(row('session','begin',12))
        with self.assertRaisesRegex(ValueError,'already performed'):e.validate_initial_quiescence(groups)
    def test_old_fact_admitted_after_live_publication_rejected(self):
        groups=self.witness();next(r for r in groups[('main',2)] if r['label']=='old_fact_refused')['b']='0'
        with self.assertRaisesRegex(ValueError,'refusal absent'):e.validate_initial_quiescence(groups)

class PackageOwnership(unittest.TestCase):
    @staticmethod
    def witness():
        rows={'MC_HEADER':[dict(package_config='1',main_thread='1',qpc_frequency='1000')], 'MC_WATCHDOG':[dict(wait='0',qpc='95')],
              'MC_PACKAGE':[dict(kind='read',status='0',error='0',system='0',module_relative='1',module='123',owner='456',thread='9',begin='10',end='20'),
                dict(kind='paths',id='2',flags='8',manifest='z:%5Ctmp%5Cpackage%5Cprovider.manifest',media='z:%5Ctmp%5Cpackage%5Cmovie.mkv'),
                dict(kind='owners_transferred',count='2',expected='2',alive='1',qpc='40'),
                dict(kind='owners_released',count='0',expired='1',qpc='100')]}
        groups={('main',2):[row('heartbeat','',1,'main',2)]}
        for i in (0,1):
            groups[('main',2)].extend([row('service','start',30+i,'main',i,a=123,b=456),row('worker_exit','',90+i,'main',i)])
            groups[('worker',i)]=[row('service','ready',50+i,instance=i,b=456),row('cleanup','safe_complete',80+i,instance=i,b=456)]
        record=dict(package_config=dict(enabled=True,source_id=2,effective_flags=8),runtime_paths=dict(manifest='/tmp/package/provider.manifest',media='/tmp/package/movie.mkv'))
        return rows,groups,record
    def test_actual_reader_owner_transfer_retention_and_release(self):
        args=self.witness();self.assertEqual(e.validate_package(*args)['same_owner_services'],2)
    def test_actual_extended_drive_package_path_row(self):
        rows,groups,record=self.witness()
        # Exact spelling observed in the saved clock-v4 MC_PACKAGE paths row.
        paths=rows['MC_PACKAGE'][1]
        paths['manifest']=r'\\?\Z:\private\tmp\x3-media-package-playback-v1\Game%20relocated%20%CE%A9%E6%97%A5%20space\x3-modern-media\providers\lav081-strict-cadf5fbf4cf41bae\provider.manifest'
        paths['media']=r'\\?\Z:\private\tmp\x3-media-package-playback-v1\Game%20relocated%20%CE%A9%E6%97%A5%20space\x3-modern-media\sources\401e4192e150a848621d60666b2946e9bfa725a17c5a28e231b7cd55098e4c76\00002.mkv'
        record['runtime_paths']=dict(manifest='/private/tmp/x3-media-package-playback-v1/Game relocated Ω日 space/x3-modern-media/providers/lav081-strict-cadf5fbf4cf41bae/provider.manifest',media='/private/tmp/x3-media-package-playback-v1/Game relocated Ω日 space/x3-modern-media/sources/401e4192e150a848621d60666b2946e9bfa725a17c5a28e231b7cd55098e4c76/00002.mkv')
        self.assertTrue(e.validate_package(rows,groups,record)['actual_module_relative_reader'])
        paths['media']=paths['media'].replace('%E6%97%A5','%E6%9C%88')
        with self.assertRaisesRegex(ValueError,'byte-bound'):e.validate_package(rows,groups,record)
    def test_prefix_normalization_keeps_complete_path_and_namespace(self):
        self.assertEqual(e.normalized_drive_path(r'\\?\Z:\folder\literal%2520.mkv'),r'z:\folder\literal%20.mkv')
        self.assertNotEqual(e.normalized_drive_path(r'\\?\Z:\foreign\file.mkv'),e.normalized_drive_path(r'Z:\expected\file.mkv'))
        self.assertNotEqual(e.normalized_drive_path(r'\\?\UNC\host\folder\file.mkv'),e.normalized_drive_path(r'Z:\folder\file.mkv'))
        self.assertNotEqual(e.normalized_drive_path(r'\\.\Z:\folder\file.mkv'),e.normalized_drive_path(r'Z:\folder\file.mkv'))
    def test_reader_on_main_rejected(self):
        rows,groups,record=self.witness();rows['MC_PACKAGE'][0]['thread']='1'
        with self.assertRaisesRegex(ValueError,'off main'):e.validate_package(rows,groups,record)
    def test_dropped_worker_config_owner_rejected(self):
        rows,groups,record=self.witness();groups[('worker',1)][1]['b']='0'
        with self.assertRaisesRegex(ValueError,'dropped immutable'):e.validate_package(rows,groups,record)
    def test_fixture_local_owner_not_transferred_rejected(self):
        rows,groups,record=self.witness();rows['MC_PACKAGE'][2]['count']='3'
        with self.assertRaisesRegex(ValueError,'exclusively'):e.validate_package(rows,groups,record)
    def test_pins_released_before_thread_exit_rejected(self):
        rows,groups,record=self.witness();rows['MC_PACKAGE'][3]['qpc']='85';rows['MC_WATCHDOG'][0]['qpc']='80'
        with self.assertRaisesRegex(ValueError,'actual watchdog exit'):e.validate_package(rows,groups,record)
    def test_watchdog_timeout_cannot_release_package(self):
        rows,groups,record=self.witness();rows['MC_WATCHDOG'][0]['wait']='258'
        with self.assertRaisesRegex(ValueError,'actual watchdog exit'):e.validate_package(rows,groups,record)
    def test_release_before_watchdog_observation_rejected(self):
        rows,groups,record=self.witness();rows['MC_WATCHDOG'][0]['qpc']='101'
        with self.assertRaisesRegex(ValueError,'actual watchdog exit'):e.validate_package(rows,groups,record)
    def test_runtime_path_not_actual_selection_rejected(self):
        rows,groups,record=self.witness();record['runtime_paths']['media']='/tmp/other/movie.mkv'
        with self.assertRaisesRegex(ValueError,'byte-bound'):e.validate_package(rows,groups,record)

if __name__=='__main__':unittest.main()
