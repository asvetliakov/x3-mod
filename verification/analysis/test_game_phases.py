"""Actual fixed-state recorder: timing identity, lifecycle and bounded evidence,
plus the launcher side of the variables that feed it: since 2026-09-26 --game-phases, --game-phase-threshold-ms and
--telemetry-draw are removed and X3M_GAME_PHASES / X3M_TELEMETRY_DRAW are members of --draw-trace (the DLL expands X3M_DRAW_TRACE=1;
the threshold stays at its 20 ms default unless a fixture sets it). No game, no Wine."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from source_text import source_text

ROOT=Path(__file__).resolve().parents[2]

class GamePhases(unittest.TestCase):
    def test_exact_timeline_lifecycle_and_bounded_records(self):
        with tempfile.TemporaryDirectory(prefix='x3-game-phases-host-') as temporary:
            exe=Path(temporary)/'fixture'
            build=subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'src/proxy'),
                                  str(ROOT/'verification/probe/game_phases_host.cpp'),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertRegex(run.stdout,r'^game_phases_host checks=\d+ failures=0 core_bytes=\d+\n$')

class DiagnosticLaunchOptions(unittest.TestCase):
    def helper(self):
        from verification.analysis.test_lod_scale_launch import LodScaleLaunchOption
        return LodScaleLaunchOption()

    def test_removed_options_and_draw_trace_membership(self):
        helper=self.helper()
        with tempfile.TemporaryDirectory() as directory:
            for args in (('--perf','--game-phases'),('--perf','--game-phase-threshold-ms','45'),('--perf','--telemetry-draw')):
                code,_,error=helper.launch(directory,*args)
                self.assertEqual(code,2,args)
                self.assertIn('unrecognized arguments',error)
            # --perf sends X3M_PERF only (the families are --draw-trace members, expanded by the DLL); an inherited value is dropped.
            code,output,error=helper.launch(directory,'--perf',inherited={'X3M_GAME_PHASE_THRESHOLD_MS':'500','X3M_GAME_PHASES':'1','X3M_TELEMETRY_DRAW':'1'})
            self.assertEqual(code,0,error)
            env=json.loads(output)['env']
            self.assertEqual(env['X3M_PERF'],'1')
            for name in ('X3M_GAME_PHASE_THRESHOLD_MS','X3M_GAME_PHASES','X3M_TELEMETRY_DRAW'):
                self.assertNotIn(name,env)
        self.assertIn('const bool wanted=log_tier::draw_trace_flag(L"X3M_GAME_PHASES");',source_text(ROOT/'src/proxy/game_phases.cpp'))
        self.assertIn('draw_active=log_tier::draw_trace_flag(L"X3M_TELEMETRY_DRAW");',source_text(ROOT/'src/proxy/telemetry.cpp'))

    def test_production_wiring_of_both_variables(self):
        source=source_text(ROOT/'src/proxy/game_phases.cpp')
        self.assertIn('L"X3M_GAME_PHASE_THRESHOLD_MS"',source)
        self.assertIn('core.frame_threshold=core.frequency*threshold_ms/1000;',source)
        self.assertIn('frame_threshold_ms=%u',source)
        core=source_text(ROOT/'src/proxy/game_phases_core.h')
        # One branch per frame: zero keeps the built-in 50 ms and costs nothing.
        self.assertIn('if(frequency&&elapsed>=(frame_threshold?frame_threshold:frequency/20)){',core)
        telemetry=source_text(ROOT/'src/proxy/telemetry.cpp')
        self.assertIn('L"X3M_TELEMETRY_DRAW"',telemetry)
        motion=source_text(ROOT/'src/proxy/motion_output.cpp')
        for field in ('gate_us=%.1f','route_draw_us=%.1f','set_rt_us=%.1f'):
            self.assertIn(field,motion)

if __name__=='__main__':
    unittest.main()
