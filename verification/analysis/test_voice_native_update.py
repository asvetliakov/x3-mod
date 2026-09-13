import tempfile,unittest,wave
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

if __name__=='__main__':unittest.main()
