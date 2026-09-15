"""Compile/run the actual portable lifetime/provenance core, without Wine."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from verification.analysis.test_chase_lead import extract_named_function
ROOT=Path(__file__).resolve().parents[2]
class TransitionCore(unittest.TestCase):
 def test_production_state_machine(self):
  with tempfile.TemporaryDirectory(prefix='x3-chase-transition-') as d:
   exe=Path(d)/'fixture'
   subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(ROOT/'verification/probe/chase_transition_host.cpp'),'-o',str(exe)],check=True,capture_output=True,text=True)
   run=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
   self.assertIn('checks PASS',run.stdout)
 def test_fresh_identity_reader(self):
  with tempfile.TemporaryDirectory(prefix='x3-chase-identity-') as d:
   exe=Path(d)/'fixture'
   subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',str(ROOT/'verification/probe/chase_transition_identity_host.cpp'),'-o',str(exe)],check=True,capture_output=True,text=True)
   run=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
   self.assertIn('checks PASS',run.stdout)
 def test_bounded_event_supplement(self):
  source=(ROOT/'src/proxy/chase_transition.cpp').read_text()
  with tempfile.TemporaryDirectory(prefix='x3-transition-record-') as d:
   directory=Path(d);exe=directory/'fixture'
   start=source.index('using detail::Origin;');end=source.index('\n\nbool bytes(',start)
   (directory/'chase_transition_record_declarations_inc.h').write_text(source[start:end])
   (directory/'chase_transition_record_functions_inc.h').write_text('\n'.join(extract_named_function(source,name) for name in ('snapshot','same','record')))
   build=subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
      '-I',str(ROOT/'src/proxy'),'-I',str(directory),str(ROOT/'verification/probe/chase_transition_record_host.cpp'),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(build.returncode,0,build.stdout+build.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True)
   self.assertEqual(run.returncode,0,run.stdout+run.stderr)
   self.assertIn('checks PASS',run.stdout)
 def test_lifetime_callbacks_revoke_before_argument_reads(self):
  source=(ROOT/'src/proxy/chase_transition.cpp').read_text()
  with tempfile.TemporaryDirectory(prefix='x3-transition-handle-') as d:
   directory=Path(d);exe=directory/'fixture'
   (directory/'chase_transition_handle_under_test_inc.h').write_text(extract_named_function(source,'handle'))
   build=subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror',
      '-I',str(ROOT/'src/proxy'),'-I',str(directory),
      str(ROOT/'verification/probe/chase_transition_handle_host.cpp'),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(build.returncode,0,build.stdout+build.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True)
   self.assertEqual(run.returncode,0,run.stdout+run.stderr)
   self.assertEqual(run.stdout,'chase_transition_handle_host scenarios=7 checks=29 failures=0\n')
if __name__=='__main__':unittest.main()
