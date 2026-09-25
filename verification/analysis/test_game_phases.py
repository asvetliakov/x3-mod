"""Actual fixed-state recorder: timing identity, lifecycle and bounded evidence,
plus the two diagnostic launcher options that feed it (--game-phase-threshold-ms,
--telemetry-draw). No game, no Wine."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest

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

    def test_segment_threshold_default_range_and_prerequisite(self):
        helper=self.helper()
        with tempfile.TemporaryDirectory() as directory:
            code,output,error=helper.launch(directory,'--perf','--game-phases')
            self.assertEqual(code,0,error)
            self.assertEqual(json.loads(output)['env']['X3M_GAME_PHASE_THRESHOLD_MS'],'20')
            code,output,error=helper.launch(directory,'--debug','--game-phases','--game-phase-threshold-ms','45')
            self.assertEqual(code,0,error)
            self.assertEqual(json.loads(output)['env']['X3M_GAME_PHASE_THRESHOLD_MS'],'45')
            code,_,error=helper.launch(directory,'--perf','--game-phase-threshold-ms','30')
            self.assertEqual(code,2)
            self.assertIn('--game-phase-threshold-ms requires --game-phases',error)
            code,_,error=helper.launch(directory,'--game-phases')
            self.assertEqual(code,2)
            self.assertIn('--game-phases requires --perf or --debug',error)
            for value in ('0','10001'):
                code,_,error=helper.launch(directory,'--perf','--game-phases','--game-phase-threshold-ms',value)
                self.assertEqual(code,2,value)
                self.assertIn('--game-phase-threshold-ms must be between 1 and 10000',error)
            # The threshold travels with --game-phases (the default written explicitly), so an inherited value cannot
            # change it; without --game-phases neither variable is sent.
            code,output,error=helper.launch(directory,'--perf','--game-phases',inherited={'X3M_GAME_PHASE_THRESHOLD_MS':'500'})
            self.assertEqual(code,0,error)
            self.assertEqual(json.loads(output)['env']['X3M_GAME_PHASE_THRESHOLD_MS'],'20')
            code,output,error=helper.launch(directory,'--perf',inherited={'X3M_GAME_PHASE_THRESHOLD_MS':'500','X3M_GAME_PHASES':'1'})
            self.assertEqual(code,0,error)
            env=json.loads(output)['env']
            self.assertNotIn('X3M_GAME_PHASE_THRESHOLD_MS',env);self.assertNotIn('X3M_GAME_PHASES',env)

    def test_telemetry_draw_requires_telemetry_and_resets_inherited_value(self):
        helper=self.helper()
        with tempfile.TemporaryDirectory() as directory:
            code,_,error=helper.launch(directory,'--telemetry-draw')
            self.assertEqual(code,2)
            self.assertIn('--telemetry-draw requires --perf or --debug',error)
            code,output,error=helper.launch(directory,'--perf','--telemetry-draw')
            self.assertEqual(code,0,error)
            self.assertEqual(json.loads(output)['env']['X3M_TELEMETRY_DRAW'],'1')
            code,output,error=helper.launch(directory,'--perf',inherited={'X3M_TELEMETRY_DRAW':'1'})
            self.assertEqual(code,0,error)
            self.assertNotIn('X3M_TELEMETRY_DRAW',json.loads(output)['env'])

    def test_production_wiring_of_both_variables(self):
        source=(ROOT/'src/proxy/game_phases.cpp').read_text()
        self.assertIn('L"X3M_GAME_PHASE_THRESHOLD_MS"',source)
        self.assertIn('core.frame_threshold=core.frequency*threshold_ms/1000;',source)
        self.assertIn('frame_threshold_ms=%u',source)
        core=(ROOT/'src/proxy/game_phases_core.h').read_text()
        # One branch per frame: zero keeps the built-in 50 ms and costs nothing.
        self.assertIn('if(frequency&&elapsed>=(frame_threshold?frame_threshold:frequency/20)){',core)
        telemetry=(ROOT/'src/proxy/telemetry.cpp').read_text()
        self.assertIn('L"X3M_TELEMETRY_DRAW"',telemetry)
        motion=(ROOT/'src/proxy/motion_output.cpp').read_text()
        for field in ('gate_us=%.1f','route_draw_us=%.1f','set_rt_us=%.1f'):
            self.assertIn(field,motion)

if __name__=='__main__':
    unittest.main()
