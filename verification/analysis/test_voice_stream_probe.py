import unittest
import run_voice_stream_probe as probe


def make_log(success=False, no_audio=False):
    lines=['VOICE_HEADER schema=1 source=144 wrapper=1 route=0 thread=8 repeats=2 cues=2 batches=2 native_flags=336 audible=0']
    seq=0
    def stage(name,repeat=-1,phase='startup',hr='00000000',required=1,attempt=1):
        nonlocal seq
        common=f'source=144 wrapper=1 route=0 repeat={repeat} seq={seq} phase={phase} name={name} attempt={attempt} required={required}'
        lines.extend(['VOICE_BEGIN '+common,'VOICE_STAGE '+common+f' hr={hr} wall_ms=1.0 cpu_ms=1.0 cpu_valid=1'])
        seq+=1
    stage('co_initialize')
    stage('directsound_create',required=0)
    lines.append('VOICE_STARTUP ds_hr=00000000 coop_hr=00000000 ds_present=1 window=1 primary_play=0')
    for r in (0,1):
        prefix=f'source=144 wrapper=1 route=0 repeat={r}'
        if not success:
            stage('activate_stream',r,'create',hr='80040154')
            lines.append('VOICE_FAILURE '+prefix+' phase=create name=activate_stream hr=80040154')
        else:
            for name in ('activate_stream','initialize','add_audio','audio_qi_pre','set_pcm','get_graph','open_file',
                         'get_audio','audio_qi_post','get_format','activate_audio_data','set_buffer_native','data_format',
                         'create_sample','create_dsound_buffer','position_qi','control_qi','probe_manual_sink_guard','stream_run','control_pause'):
                audio_names={'audio_qi_pre','set_pcm','get_audio','audio_qi_post','get_format','activate_audio_data','set_buffer_native','data_format','create_sample','create_dsound_buffer'}
                if no_audio and name=='add_audio':
                    stage(name,r,'create',hr='80004005',required=0,attempt=1)
                    stage(name,r,'create',hr='80004005',required=0,attempt=2)
                    lines.append('VOICE_DOWNGRADE '+prefix+' flags=344 no_audio=1 hr=80004005')
                elif not no_audio or name not in audio_names:
                    stage(name,r,'create')
            lines.append('VOICE_SINK '+prefix+f' terminals={0 if no_audio else 1} filters=3 manual_only=1 hr=00000000')
            if not no_audio:
                lines.append('VOICE_FORMAT '+prefix+' tag=1 rate=44100 channels=1 bits=16 align=2 avg=88200 extra=0')
            else:
                lines.append('VOICE_FAILURE '+prefix+' phase=decode name=native_no_audio hr=800700e8')
            for c in (() if no_audio else (0,1)):
                for b in (0,1):
                    start=(10 if c==0 else 60)*10000000+b*1000000
                    lines.append('VOICE_PCM '+prefix+f' cue={c} batch={b} requested_ms={10500 if c==0 else 60500} seek_ms={10000 if c==0 else 60000} actual=8820 start={start} end={start+1000000} current={start+1000000} nonzero=4400 peak=1000 energy=900000')
        stage('release_multimedia',r,'cleanup',required=0)
        lines.append('VOICE_CASE '+prefix+f' constructed={int(success)} decoded={int(success and not no_audio)} audio_enabled={int(not no_audio)} fatal={"native_no_audio" if no_audio else "none" if success else "activate_stream"} hr={"800700e8" if no_audio else "00000000" if success else "80040154"} duration={1000 if success else 0} cleanup=1')
    lines.append('VOICE_COMPLETE cases=2 owner_thread=1 audible=0')
    return '\n'.join(lines)+'\n'


class ProbeTests(unittest.TestCase):
    def test_completed_measurement_preserves_native_failure(self):
        r=probe.validate(make_log(),144,0,1)
        self.assertTrue(r['completed'])
        self.assertEqual([x['fatal'] for x in r['cases']],['activate_stream']*2)
        self.assertTrue(all(x['decoded']=='0' for x in r['cases']))

    def test_success_requires_pcm_native_stages_and_second_cue(self):
        r=probe.validate(make_log(True),144,0,1)
        self.assertEqual(len(r['cases'][0]['pcm']),4)
        for old,new in (('name=create_sample','name=omitted_sample'),('seek_ms=60000','seek_ms=10000'),
                        ('nonzero=4400','nonzero=0'),('manual_only=1','manual_only=0'),('start=600000000','start=100000000')):
            with self.subTest(old=old),self.assertRaises(AssertionError):
                probe.validate(make_log(True).replace(old,new),144,0,1)

    def test_negotiated_format_is_required_unique_and_aligned(self):
        good=make_log(True)
        lines=good.splitlines()
        missing='\n'.join(l for l in lines if not l.startswith('VOICE_FORMAT'))
        duplicate=good+'\n'+next(l for l in lines if l.startswith('VOICE_FORMAT'))
        for bad in (missing,duplicate,good.replace('tag=1 rate','tag=3 rate'),
                    good.replace('bits=16','bits=8'),good.replace('avg=88200','avg=88201'),
                    good.replace('align=2','align=4'),good.replace('VOICE_FORMAT source=144','VOICE_FORMAT source=244')):
            with self.assertRaises(AssertionError):probe.validate(bad,144,0,1)
        # A valid stereo format still requires frame alignment, not merely %2.
        stereo=good.replace('channels=1','channels=2').replace('align=2','align=4').replace('avg=88200','avg=176400')
        self.assertTrue(probe.validate(stereo,144,0,1)['completed'])
        with self.assertRaises(AssertionError):probe.validate(stereo.replace('actual=8820','actual=8818'),144,0,1)

    def test_native_no_audio_downgrade_retains_later_path(self):
        result=probe.validate(make_log(True,True),144,0,1)
        self.assertEqual([r['outcome_scope'] for r in result['cases']],['constructed_no_audio']*2)
        self.assertTrue(all(r['format'] is None and not r['pcm'] for r in result['cases']))
        with self.assertRaises(AssertionError):
            probe.validate(make_log(True,True).replace('name=stream_run','name=missing_late_run'),144,0,1)

    def test_hresult_failure_cannot_be_positive_or_lost(self):
        for content in (make_log().replace('80040154','00000001'),
                        '\n'.join(l for l in make_log().splitlines() if not l.startswith('VOICE_FAILURE'))):
            with self.assertRaises(AssertionError):probe.validate(content,144,0,1)

    def test_truncated_mismatched_or_nonfinite_measurement_rejected(self):
        variants=(make_log().replace('cpu_ms=1.0','cpu_ms=nan'),
                  make_log().replace('cpu_valid=1','cpu_valid=0'),
                  make_log().replace('VOICE_COMPLETE cases=2','VOICE_COMPLETE cases=1'),
                  make_log().replace('VOICE_STAGE source=144','VOICE_STAGE source=244'),
                  make_log().replace('VOICE_STAGE','IGNORED_STAGE',1))
        for value in variants:
            with self.assertRaises(AssertionError):probe.validate(value,144,0,1)

    def test_exact_missing_variant_selection(self):
        self.assertEqual(probe.select_variants(['244:2:0','244:2:1']),((244,2,1),(244,2,0)))
        self.assertEqual(probe.select_variants(None),probe.MATRIX)
        for invalid in (['244:2:1','244:2:1'],['244:3:0'],['145:2:0'],['244:2'],['244:2:2']):
            with self.assertRaises(ValueError):probe.select_variants(invalid)

    def test_matrix_covers_routes_wrapper_sources_without_aliases(self):
        self.assertEqual(len(probe.MATRIX),12)
        self.assertEqual(len(set(probe.MATRIX)),12)
        self.assertEqual(set(probe.MATRIX),{(s,r,w) for s in (144,244) for r in (0,1,2) for w in (0,1)})


if __name__=='__main__':unittest.main()
