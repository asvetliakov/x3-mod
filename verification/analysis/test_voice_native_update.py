import struct,tempfile,unittest,uuid,wave
from pathlib import Path
import run_voice_native_update as probe
import test_voice_stream_probe as test_probe

class NativeUpdateTests(unittest.TestCase):
    def test_native_mode_required_and_pcm_failures_not_hidden(self):
        # Reuse the accepted R1 report fixture/parser; only mode evidence differs.
        good=test_probe.make_log(success=True).replace('native_flags=336 audible=0','native_flags=336 audible=0 sample_event=0')
        good=good.replace('wrapper=1','wrapper=0')
        self.assertTrue(probe.check(good)['completed'])
        with self.assertRaises(AssertionError):probe.check(good.replace('sample_event=0','sample_event=1'))
        with self.assertRaises(AssertionError):probe.check(good.replace('nonzero=4400','nonzero=0'))

    def test_synthetic_control_format_and_cues(self):
        with tempfile.TemporaryDirectory() as d:
            valid=Path(d)/'synthetic.wav'
            with wave.open(str(valid),'wb') as w:
                w.setparams((1,2,44100,0,'NONE','not compressed'))
                for _ in range(70):w.writeframes(b'\x01\x00'*44100)
            probe.check_wave(valid)
            p=Path(d)/'wrong.wav'
            with wave.open(str(p),'wb') as w:
                w.setparams((1,2,22050,0,'NONE','not compressed'));w.writeframes(b'\x01\x00'*20)
            with self.assertRaises(AssertionError):probe.check_wave(p)


DECLARED={144:44304.046,244:3335.779}


def make_actual_log(window=20.0,tail=5.0):
    """Shape-accurate stdout for the actual-file mode; no game or Wine execution."""
    out=['VOICE_ACTUAL_HEADER schema=1 files=2 reopens=2 window_ms=%d tail_ms=%d thread=7 native_flags=336 audible=0 sample_event=0'%(window*1000,tail*1000)]
    def pre(kind,source,repeat=0):return '%s source=%d wrapper=0 route=0 repeat=%d'%(kind,source,repeat)
    for source in (144,244):
        declared=DECLARED[source];total=0
        for name,start,seconds,eos in (('head',0.,window,0),('seek',declared/2,window,0),('tail',declared-tail,tail,1)):
            size=int(seconds*88200);total+=size
            end=(start+seconds)*1e7 if not eos else declared*1e7
            out.append(pre('VOICE_READ',source)+' segment=%s index=0 hr=00000000 actual=%d start=%d end=%d current=0 nonzero=%d peak=9000'%(name,size,start*1e7,end,size//2))
            if eos:out.append(pre('VOICE_READ',source)+' segment=tail index=1 hr=00040003 actual=0 start=0 end=0 current=0 nonzero=0 peak=0')
            out.append(pre('VOICE_SEGMENT',source)+' name=%s start_ms=%d reads=1 bytes=%d decoded_ms=%.3f span_ms=%.3f first_start=%d last_end=%d nonzero=%d nonzero_reads=1 eos=%d monotonic=1 end_reason=%s post_eos_hr=%s'%(
                name,start*1000,size,seconds*1000,seconds*1000,start*1e7,end,size//2,eos,'endofstream' if eos else 'window','00040003' if eos else '8000000a'))
        out.append(pre('VOICE_FILE',source)+' created=1 complete=1 seeked=1 duration_s=%.6f requested_s=%.3f decoded_s=%.6f segments=3 reads=3 bytes=%d nonzero_reads=3 nonzero=%d eos=1 monotonic=1 rate=44100 channels=1 bits=16 fatal=none hr=00000000'%(
            declared,2*window+tail,total/88200.,total,total//2))
        out.append(pre('VOICE_RELEASE',source)+' name=release_multimedia refs=0')
        for attempt in (1,2):
            out.append(pre('VOICE_REOPEN',source,attempt)+' attempt=%d created=1 read=1 reads=1 bytes=88200 nonzero_reads=1 fatal=none hr=00000000'%attempt)
            out.append(pre('VOICE_RELEASE',source,attempt)+' name=release_multimedia refs=0')
    out.append('VOICE_CAPTURE bytes=1764000 written=1764000')
    out.append('VOICE_ACTUAL_COMPLETE files=2 reopens=2 audible=0')
    return '\n'.join(out)+'\n'


class ActualModeTests(unittest.TestCase):
    def check(self,text):
        return probe.check_actual(text,[144,244],20.0,5.0,DECLARED)

    def test_actual_log_accepted_and_summarised(self):
        result=self.check(make_actual_log())
        self.assertTrue(result['completed'])
        self.assertEqual([f['voice_id'] for f in result['files']],[144,244])
        for f in result['files']:
            self.assertAlmostEqual(f['decoded_seconds'],45.0,places=3)
            self.assertEqual(f['live_root_objects'],0)
            self.assertEqual([s['name'] for s in f['segments']],['head','seek','tail'])

    def test_missing_eos_live_object_and_short_window_rejected(self):
        good=make_actual_log()
        for broken in (good.replace('eos=1 monotonic=1 end_reason=endofstream','eos=0 monotonic=1 end_reason=window'),
                       good.replace('name=release_multimedia refs=0','name=release_multimedia refs=1',1),
                       good.replace('decoded_ms=20000.000','decoded_ms=17000.000',1),
                       good.replace('nonzero=44100 peak=9000','nonzero=0 peak=0').replace('nonzero_reads=1','nonzero_reads=0')):
            with self.assertRaises(AssertionError):self.check(broken)

    def test_declared_duration_from_asf_header(self):
        properties=uuid.UUID('8CABDCA1-A947-11CF-8EE4-00C00C205365').bytes_le
        body=b'\x00'*40+struct.pack('<QQQ',33373580000,33364620000,1579)+struct.pack('<IIII',0,2261,2261,48646)
        obj=properties+struct.pack('<Q',24+len(body))+body
        header=uuid.UUID('75B22630-668E-11CF-A6D9-00AA0062CE6C').bytes_le+struct.pack('<Q',30+len(obj))+struct.pack('<I',1)+b'\x01\x02'+obj
        with tempfile.TemporaryDirectory() as d:
            path=Path(d)/'voice.dat';path.write_bytes(header)
            self.assertAlmostEqual(probe.declared_seconds(path),3335.779,places=3)
            path.write_bytes(b'\x00'*128)
            with self.assertRaises(AssertionError):probe.declared_seconds(path)

if __name__=='__main__':unittest.main()
