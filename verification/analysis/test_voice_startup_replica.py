import unittest
import unittest.mock
import run_voice_startup_replica as probe


def fixture(mode='game',hang_at=None,created=(True,True,True),play=True,run_fails=False):
    streams=1 if mode=='single' else 3;ds=mode in probe.GAME_DS_MODES
    rows=[f'REPLICA_HEADER schema=1 mode={mode} streams={streams} dwell_ms=0 watchdog_ms=15000 thread=9 audible=0'];seq=0
    def stage(stream,name,code='00000000',attempt=1):
        nonlocal seq
        rows.append(f'REPLICA_BEGIN seq={seq} stream={stream} name={name} attempt={attempt}')
        if hang_at==(stream,name):rows.append(f'REPLICA_HUNG step={name} stream={stream} elapsed_ms=15020 thread=9');return False
        rows.append(f'REPLICA_STAGE seq={seq} stream={stream} name={name} attempt={attempt} hr={code} wall_ms=1.5');seq+=1;return True
    stage(0,'co_initialize');stage(0,'directsound_create')
    if ds:stage(0,'directsound_caps')
    stage(0,'directsound_cooperative')
    rows.append('REPLICA_STARTUP ds_hr=00000000 coop_hr=00000000 ds_present=1 window=1')
    if ds:
        stage(0,'primary_create','88780032');stage(0,'primary_create','00000000',2);stage(0,'listener_qi');stage(0,'primary_play');stage(0,'primary_set_format');stage(0,'primary_get_volume')
        for name in ('listener_doppler','listener_distance','listener_rolloff','listener_position','listener_commit'):stage(0,name)
        rows.append('REPLICA_PRIMARY flags=11 create_hr=00000000 play_hr=00000000 format_hr=00000000 volume=0 listener=1 commit_hr=00000000')
    teardown=(['buffer_stop','control_stop','stream_stop','release_position','release_control','release_audio','release_media','release_graph',('buffer_stop',2),('stream_stop',2),'release_buffer','release_sample','release_data','release_multimedia'] if ds
              else ['buffer_stop','control_stop','stream_stop','release_position','release_control','release_buffer','release_sample','release_data','release_audio','release_media','release_graph','release_multimedia'])
    def tear(stream):
        for item in teardown:
            name,attempt=item if isinstance(item,tuple) else (item,1)
            if not stage(stream,name,'00000000',attempt):return False
        return True
    order=[1] if streams==1 else [2,3,1]
    for n,stream in enumerate(order):
        ok=created[n]
        if not stage(stream,'pump'):return '\n'.join(rows)+'\n'
        for name in ('activate_stream','initialize','add_audio','audio_qi_pre','set_pcm','get_graph'):
            if not stage(stream,name):return '\n'.join(rows)+'\n'
        if not ok:
            stage(stream,'open_file','80040217');stage(stream,'open_file','80040217',2)
            rows.append(f'REPLICA_FAILURE stream={stream} name=open_file hr=80040217')
            rows.append(f'REPLICA_STREAM stream={stream} created=0 role={"played" if stream==1 else "restored"} fatal=open_file hr=80040217 duration=0.000 early_update_hr=8000000a');continue
        names=['open_file','release_audio_pre','release_media_pre','get_audio','audio_qi_post','get_format','activate_audio_data','set_buffer','data_format','create_sample','create_dsound_buffer','position_qi','control_qi','get_duration','can_seek_forward','stream_run']
        if ds:names.remove('get_duration')
        if ds and run_fails and stream==1:
            for name in names[:-1]:stage(stream,name)
            stage(stream,'stream_run','80004005');rows.append('REPLICA_FAILURE stream=1 name=stream_run hr=80004005')
            if not tear(stream):return '\n'.join(rows)+'\n'
            rows.append('REPLICA_STREAM stream=1 created=0 role=played fatal=stream_run hr=80004005 duration=0.000 early_update_hr=8000000a');continue
        if mode=='early_update':names.append('early_update')
        if mode!='nopause':names.append('control_pause')
        for name in names:
            code='00040001' if name=='early_update' else '00000000'
            if not stage(stream,name,code):return '\n'.join(rows)+'\n'
        early='00040001' if mode=='early_update' else '8000000a'
        rows.append(f'REPLICA_STREAM stream={stream} created=1 role={"played" if stream==1 else "restored"} fatal=none hr=00000000 duration=44.304 early_update_hr={early}')
    if created[-1] and play and not run_fails:
        if not stage(1,'control_run'):return '\n'.join(rows)+'\n'
        rows.append('REPLICA_POLL cycle=1 state=2 outcome=queued hr=00040001 bytes=0 elapsed_ms=1')
        rows.append('REPLICA_POLL cycle=2 state=2 outcome=pending hr=00040001 bytes=0 elapsed_ms=7')
        rows.append('REPLICA_POLL cycle=40 state=4 outcome=completed hr=00000000 bytes=0 elapsed_ms=300')
        rows.append('REPLICA_POLL cycle=41 state=1 outcome=consumed hr=00000000 bytes=176400 elapsed_ms=306')
        rows.append('REPLICA_PLAY stream=1 cycles=210 completed=5 queued=5 pending_polls=190 eos=0 stuck=0 errors=0 bytes=882000 elapsed_ms=1600')
    for stream in reversed(order):
        if (ds and run_fails and stream==1) or not created[order.index(stream)]:continue
        if not tear(stream):return '\n'.join(rows)+'\n'
    if ds:stage(0,'primary_set_volume');stage(0,'release_listener');stage(0,'release_primary')
    stage(0,'release_directsound');stage(0,'co_uninitialize')
    rows.append(f'REPLICA_COMPLETE streams={streams} audible=0')
    return '\n'.join(rows)+'\n'


SAMPLE='''Call graph:
    2986 Thread_101   DispatchQueue_1: com.apple.main-thread  (serial)
    + 2986 start  (in dyld) + 1234  [0x1]
    +   2986 __psynch_cvwait  (in libsystem_kernel.dylib) + 8  [0x2]
    2986 Thread_202
    + 2986 thread_start  (in libsystem_pthread.dylib) + 8  [0x3]
    +   2986 wg_parser_stream_get_buffer  (in winegstreamer.so) + 100  [0x4]
    +     2986 __psynch_cvwait  (in libsystem_kernel.dylib) + 8  [0x2]

Binary Images:
       0x1 -        0x2  dyld
'''


class ReplicaTests(unittest.TestCase):
    def test_completed_modes(self):
        for mode in probe.MODES:
            r=probe.validate(fixture(mode));self.assertTrue(r['completed']);self.assertIsNone(r['hung_step'])
            self.assertEqual(r['created'],1 if mode=='single' else 3);self.assertEqual(r['play']['completed'],'5')

    def test_game_ds_primary_row_and_run_failure_teardown(self):
        r=probe.validate(fixture('game-ds-stereo'));self.assertTrue(r['completed']);self.assertEqual(r['primary']['flags'],'11')
        self.assertNotIn('get_duration',[s['name'] for s in r['stages']])
        r=probe.validate(fixture('game-ds',run_fails=True));self.assertTrue(r['completed']);self.assertEqual(r['created'],2);self.assertIsNone(r['play'])
        runs=[k for k in r['key_steps'] if k['name']=='stream_run'];self.assertEqual([k['hr'] for k in runs],['00000000','00000000','80004005'])
        stops=[(k['name'],k['attempt']) for k in r['key_steps'] if k['stream']==1 and k['name'].endswith('_stop')]
        self.assertEqual(stops,[('buffer_stop',1),('control_stop',1),('stream_stop',1),('buffer_stop',2),('stream_stop',2)])
        r=probe.validate(fixture('game-ds',hang_at=(1,'control_stop'),run_fails=True));self.assertEqual((r['hung_step'],r['hung_stream']),('control_stop',1))
        for bad in (fixture('game-ds').replace('REPLICA_PRIMARY flags=11','REPLICA_PRIMARY flags=12'),fixture('game').replace('ds_present=1 window=1','ds_present=1 window=1\nREPLICA_PRIMARY flags=11 create_hr=00000000 play_hr=00000000 format_hr=00000000 volume=0 listener=1 commit_hr=00000000')):
            with self.assertRaises(AssertionError):probe.validate(bad)

    def test_hang_names_open_step_and_forbids_completion(self):
        r=probe.validate(fixture(hang_at=(1,'control_pause')))
        self.assertFalse(r['completed']);self.assertEqual((r['hung_step'],r['hung_stream']),('control_pause',1));self.assertEqual(r['created'],2)
        r=probe.validate(fixture(hang_at=(1,'control_run')));self.assertEqual(r['hung_step'],'control_run')
        good=fixture(hang_at=(3,'open_file'))
        for bad in (good.replace('REPLICA_HUNG step=open_file','REPLICA_HUNG step=control_pause'),good+'REPLICA_COMPLETE streams=3 audible=0\n',
                    good.replace('elapsed_ms=15020','elapsed_ms=200')):
            with self.assertRaises(AssertionError):probe.validate(bad)

    def test_failed_open_and_unterminated_step_rejected(self):
        r=probe.validate(fixture(created=(False,True,True)));self.assertTrue(r['completed']);self.assertEqual(r['created'],2)
        r=probe.validate(fixture(created=(True,True,False),play=False));self.assertIsNone(r['play'])
        good=fixture()
        for bad in (good.replace('REPLICA_STAGE seq=3','REPLICA_LOST seq=3'),good.replace('mode=game streams=3','mode=single streams=3'),
                    good.replace('REPLICA_STREAM stream=1 created=1','REPLICA_STREAM stream=1 created=0'),good.replace('completed=5 queued=5','completed=9 queued=5')):
            with self.assertRaises(AssertionError):probe.validate(bad)

    def test_sample_parser_keeps_leaf_paths_per_thread(self):
        threads=probe.parse_sample(SAMPLE)
        self.assertEqual([t['thread'] for t in threads],['Thread_101','Thread_202'])
        self.assertEqual(threads[1]['frames'][1],'wg_parser_stream_get_buffer (winegstreamer.so)')
        self.assertEqual(len(threads[0]['frames']),2)

PS_ROWS={
    1:(0,'Ss','/sbin/launchd'),
    900:(1,'S','/usr/bin/python3 verification/probe/run_voice_startup_replica.py --exe build/voice_startup_replica.exe'),
    950:(900,'S','/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine --bottle X3 Z:\\x3-mod\\voice_startup_replica.exe game-dmo'),
    960:(950,'R','C:\\windows\\system32\\start.exe voice_startup_replica.exe'),
    970:(960,'R','voice_startup_replica.exe'),
    980:(960,'R','C:\\x3\\helper.exe'),
    985:(1,'S','wineserver'),
    990:(1,'Z','voice_startup_replica.exe <defunct>'),
}


class FakeProc:
    """Popen stand-in: terminate() only stops the wrapper, as the real one does."""
    def __init__(self,pid,rows):self.pid=pid;self.rows=rows;self.returncode=None;self.waits=[]
    def terminate(self):self.rows.pop(self.pid,None);self.returncode=-15
    def kill(self):self.terminate();self.returncode=-9
    def wait(self,timeout=None):
        self.waits.append(timeout)
        if self.returncode is None:raise probe.subprocess.TimeoutExpired('wine',timeout)
        return self.returncode


class KillPathTests(unittest.TestCase):
    def test_replica_pids_by_name_and_parent_chain(self):
        pids=probe.replica_pids(rows=dict(PS_ROWS),wrapper=950)
        # 960/970 name the image, 980 is a PE descendant of the wrapper; the runner
        # (python), the wrapper itself, wineserver and the zombie are excluded.
        self.assertEqual(pids,[960,970,980])
        self.assertNotIn(990,probe.replica_pids(rows=dict(PS_ROWS)))
        # Without a wrapper anchor the wine wrapper itself also names the image.
        self.assertEqual(probe.replica_pids(rows=dict(PS_ROWS)),[950,960,970])

    def test_terminate_kills_pe_first_then_wrapper_and_reports_pids(self):
        rows=dict(PS_ROWS);signals=[]
        def fake_kill(pid,number):
            signals.append((pid,number))
            if number==probe.signal.SIGKILL:rows.pop(pid,None)
        proc=FakeProc(950,rows)
        with unittest.mock.patch.object(probe.os,'kill',fake_kill),unittest.mock.patch.object(probe,'process_rows',lambda:rows):
            result=probe.terminate(proc,grace=0.3)
        self.assertEqual([s for s in signals if s[1]==probe.signal.SIGTERM],[(p,probe.signal.SIGTERM) for p in (960,970,980)])
        self.assertEqual(result['killed_pids'],[960,970,980])
        self.assertEqual(result['survivors'],[])
        self.assertEqual(proc.returncode,-15)
        # The PE processes die before the wrapper is touched.
        self.assertNotIn(950,[p for p,_ in signals])

    def test_terminate_reports_survivors_when_the_pe_process_ignores_signals(self):
        rows=dict(PS_ROWS);proc=FakeProc(950,rows)
        with unittest.mock.patch.object(probe.os,'kill',lambda pid,number:None),unittest.mock.patch.object(probe,'process_rows',lambda:rows):
            result=probe.terminate(proc,grace=0.2)
        self.assertEqual(result['survivors'],[960,970,980])
        self.assertEqual(result['killed_pids'],[960,970,980])

if __name__=='__main__':unittest.main()
