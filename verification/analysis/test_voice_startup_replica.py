import unittest
import run_voice_startup_replica as probe


def fixture(mode='game',hang_at=None,created=(True,True,True),play=True):
    streams=1 if mode=='single' else 3
    rows=[f'REPLICA_HEADER schema=1 mode={mode} streams={streams} dwell_ms=0 watchdog_ms=15000 thread=9 audible=0'];seq=0
    def stage(stream,name,code='00000000',attempt=1):
        nonlocal seq
        rows.append(f'REPLICA_BEGIN seq={seq} stream={stream} name={name} attempt={attempt}')
        if hang_at==(stream,name):rows.append(f'REPLICA_HUNG step={name} stream={stream} elapsed_ms=15020 thread=9');return False
        rows.append(f'REPLICA_STAGE seq={seq} stream={stream} name={name} attempt={attempt} hr={code} wall_ms=1.5');seq+=1;return True
    stage(0,'co_initialize');stage(0,'directsound_create');stage(0,'directsound_cooperative')
    rows.append('REPLICA_STARTUP ds_hr=00000000 coop_hr=00000000 ds_present=1 window=1')
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
        if mode=='early_update':names.append('early_update')
        if mode!='nopause':names.append('control_pause')
        for name in names:
            code='00040001' if name=='early_update' else '00000000'
            if not stage(stream,name,code):return '\n'.join(rows)+'\n'
        early='00040001' if mode=='early_update' else '8000000a'
        rows.append(f'REPLICA_STREAM stream={stream} created=1 role={"played" if stream==1 else "restored"} fatal=none hr=00000000 duration=44.304 early_update_hr={early}')
    if created[-1] and play:
        if not stage(1,'control_run'):return '\n'.join(rows)+'\n'
        rows.append('REPLICA_POLL cycle=1 state=2 outcome=queued hr=00040001 bytes=0 elapsed_ms=1')
        rows.append('REPLICA_POLL cycle=2 state=2 outcome=pending hr=00040001 bytes=0 elapsed_ms=7')
        rows.append('REPLICA_POLL cycle=40 state=4 outcome=completed hr=00000000 bytes=0 elapsed_ms=300')
        rows.append('REPLICA_POLL cycle=41 state=1 outcome=consumed hr=00000000 bytes=176400 elapsed_ms=306')
        rows.append('REPLICA_PLAY stream=1 cycles=210 completed=5 queued=5 pending_polls=190 eos=0 stuck=0 errors=0 bytes=882000 elapsed_ms=1600')
    for stream in reversed(order):
        for name in ('buffer_stop','control_stop','stream_stop','release_position','release_control','release_buffer','release_sample','release_data','release_audio','release_media','release_graph','release_multimedia'):
            if not stage(stream,name):return '\n'.join(rows)+'\n'
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

if __name__=='__main__':unittest.main()
