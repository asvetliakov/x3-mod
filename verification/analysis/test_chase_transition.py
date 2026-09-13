"""Compile/run the actual portable lifetime/provenance core, without Wine."""
import subprocess
import tempfile
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
class TransitionCore(unittest.TestCase):
 def test_production_state_machine(self):
  with tempfile.TemporaryDirectory(prefix='x3-chase-transition-') as d:
   exe=Path(d)/'fixture'
   subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(ROOT/'verification/probe/chase_transition_host.cpp'),'-o',str(exe)],check=True,capture_output=True,text=True)
   run=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
   self.assertIn('checks PASS',run.stdout)
if __name__=='__main__':unittest.main()
