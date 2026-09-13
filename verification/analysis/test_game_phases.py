"""Actual fixed-state recorder: timing identity, lifecycle and bounded evidence."""
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

if __name__=='__main__':
    unittest.main()
