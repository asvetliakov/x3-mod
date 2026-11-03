"""Host tests of the FL music keep (src/engine/fl/music_keep_fl.cpp, music_keep_sites_fl.h).

docs/reverse-engineering/fl/music-keep.md, "Port (2026-11-03)". (a) The native fixture verification/probe/fl_music_keep.cpp
(MinGW i686, natively through WSL interop) maps the installed X3FL.exe and runs the real stop-all, play, MOV_StopMovie,
pause, free-wrapper and retirement bytes over a synthetic arena with the production module installed: alt-tab
skip_all and the status gate (answered while inactive, forwarded with RunInBackground or active), pause keep_running
and the replay's skipped seek, the DirectSound pause, the load's orphan pause, the seek / Run thunks at their call sites,
the save pump through the import slots (gzwrite pumps on cadence, the save's own gzclose ends it, the replay skips the
Run), the combat resume (position remembered on the rising edge, the replay seeks to it), the track queue (defer
consumed with both completions, park consumed, another play ends it and the FL free retires the orphan with slot 32),
the docking keep (the id cell rewritten) and the KC patch through the load gate; callee-saved registers, ESP, DF,
MXCSR, x87 CW and LastError at every stub; every window change and each caller's rel32 refused; read-back / chain
rollback at three sites; the optional queue site's own rollback; shutdown; the late window. (b) The read-only site
verifier on the exe and on mutated copies. (c) Wiring.
"""
from pathlib import Path
import re
import tempfile
import unittest

import fl_music_fixture_build as fb
import fl_music_sites_common as c
import verify_music_keep_sites_fl as verifier

ROOT = Path(__file__).resolve().parents[2]
EXE = c.DEFAULT_EXE
SCENARIOS = ('alt_tab_skip_all', 'status_answered_while_inactive', 'status_forwarded_run_in_background',
             'pause_keep_running', 'pause_replay_resumes', 'pause_directsound_paused', 'load_pauses_orphaned_hold',
             'seek_skipped', 'seek_forwarded', 'save_keep_running_armed', 'gzwrite_pumps', 'gzwrite_cadence',
             'save_gzclose_ends_pumping', 'save_replay_skips_run', 'resume_remembered', 'resume_seeks',
             'queue_defer_consumed', 'queue_park_consumed', 'queue_end_other_play_frees', 'dock_keep_defers_same_id',
             'queue_end_load', 'kc_patch_applied', 'queue_optional_rolled_back', 'late_claim_wrote_nothing')


class FlMusicKeepNative(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix='x3-fl-music-keep-')
        cls.run_ = None
        if fb.available() and EXE.exists():
            cls.run_ = fb.run(fb.build('music_keep', cls.tmp.name), EXE)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def setUp(self):
        if self.run_ is None:
            self.skipTest('needs i686-w64-mingw32-g++, WSL interop and the installed X3FL.exe')

    def test_fixture_passes(self):
        out = self.run_.stdout
        self.assertEqual(self.run_.returncode, 0, '\n'.join(l for l in out.splitlines() if l.startswith('FAIL'))[:3000])
        self.assertGreaterEqual(int(re.search(r'RESULT PASS checks=(\d+) failures=0', out).group(1)), 55)
        cases, refused = map(int, re.search(r'FLIPS windows=22 cases=(\d+) refused=(\d+)', out).groups())
        self.assertEqual(cases, refused)
        self.assertIn('ARMED keep=1 gate=ok e=ok queue=ok pump=ok', out)
        self.assertIn('FAULTS taken=7', out)
        for label in SCENARIOS:
            self.assertNotIn(f'FAIL {label}', out)
        rows = re.findall(r'^LOG (music_\S+ .*)$', out, re.M)
        self.assertTrue(all('edition=fl' in r for r in rows), [r for r in rows if 'edition=fl' not in r][:3])
        self.assertTrue(any(r.startswith('music_queue frame=') and 'event=defer' in r for r in rows))
        self.assertTrue(any(r.startswith('music_queue_kc ') and 'action=applied' in r for r in rows))
        self.assertTrue(any(r.startswith('music_keep_resume ') and 'action=remember' in r and 'saved_ms=61500' in r
                            for r in rows))


class FlMusicKeepSites(unittest.TestCase):
    def setUp(self):
        if not EXE.exists():
            self.skipTest('needs the installed X3FL.exe')

    def test_verifier_passes(self):
        report = verifier.verify(EXE)
        self.assertEqual(report['result'], 'PASS', report['failed'])
        self.assertEqual(report['caller_counts'], {'stop_all': 6, 'play': 4, 'stop_movie': 2, 'free_wrapper': 1,
                                                   'seek': 3, 'status': 1, 'run': 2, 'pause': 1})
        self.assertFalse(report['a_jump_atomic'])  # Patch A crosses a qword: a plain write inside the window

    def test_mutations_refused(self):
        data = bytearray(EXE.read_bytes())
        im = c.Image(EXE)
        cases = {'a_span': 0x0b4240, 'free_contract': 0x0b467f, 'gzclose_call_through_slot': 0x10695e,
                 'status_no_look_exit': 0x0ff59a}
        for check, rva in cases.items():
            mutated = bytearray(data)
            mutated[im.pe.offset(rva)] ^= 0x01
            with tempfile.NamedTemporaryFile(suffix='.exe') as f:
                f.write(mutated)
                f.flush()
                report = verifier.verify(Path(f.name), sha256=None)
            self.assertEqual(report['result'], 'FAIL', check)
            self.assertIn(check, report['failed'])


class FlMusicKeepWiring(unittest.TestCase):
    def test_capture_and_sources(self):
        capture = re.sub(r'//.*', '', (ROOT / 'src/proxy/capture.cpp').read_text())
        fl_init = capture.rindex('music_keep::initialize();')
        self.assertLess(capture.rindex('media_cue::initialize();'), fl_init)
        present = capture.rindex('music_keep::present(ctx.id, ctx.frame, ctx.capture);')
        self.assertLess(capture.rindex('#else', 0, present), present)
        self.assertLess(present, capture.index('#endif', present))
        source = (ROOT / 'src/engine/fl/music_keep_fl.cpp').read_text()
        for needle in ('decide_stop(holds_, record, flags, id, media, role, status_gate_, save_pump_free)',
                       'queue_on_stop(queue_, v, queue_armed_, current_frame_)', 'queue_on_play(queue_, v, current_frame_)',
                       'kc_.settle(seen)', 'x3m_music_keep_fl_call_free', 'mov ecx, 32', 'mov dword ptr [esp], ecx',
                       'reason=ap_only edition=fl', 'W::first_mismatch(read_code, base_, W::keep_windows, W::keep_fixups)',
                       'InterlockedCompareExchange'):
            self.assertIn(needle, source)
        sites = (ROOT / 'src/engine/fl/music_keep_sites_fl.h').read_text()
        self.assertIn('core::stop_movie_native_return_va', sites)


if __name__ == '__main__':
    unittest.main()
