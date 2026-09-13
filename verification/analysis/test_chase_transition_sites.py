"""Structural acceptance and refusal tests for the nine transition sites."""
import dataclasses
import struct
import tempfile
import unittest
from pathlib import Path
import verify_chase_transition_sites as probe
from verification.analysis.test_chase_aim_sites import synthetic_image
class TransitionSites(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(prefix='x3-transition-sites-')
  cls.exe=Path(cls.tmp.name)/'test.exe'
  cls.data=synthetic_image(extra=tuple((s.va,s.expected) for s in probe.SITES))
  cls.exe.write_bytes(cls.data)
  cls.decoded=probe.decode(cls.exe)
  cls.image=probe.common.Image(cls.data)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_production_specs(self):
  self.assertTrue(probe.common.check_source_specs(probe.SOURCE.read_text(),probe.SITES)['ok'])
 def test_all_spans_are_whole_and_plain(self):
  for spec in probe.SITES:
   with self.subTest(site=spec.name):self.assertTrue(probe.common.inspect_site(self.image,spec,self.decoded[(spec.function_start,spec.function_end)])['ok'])
 def test_interior_branch_rejected_each_site(self):
  for spec in probe.SITES:
   with self.subTest(site=spec.name):
    incoming=probe.common.Instruction(spec.function_end-10,b'\xe9\x00\x00\x00\x00','jmp',hex(spec.va+1))
    listing=self.decoded[(spec.function_start,spec.function_end)]+[incoming]
    self.assertFalse(probe.common.inspect_site(self.image,spec,listing)['no_interior_branch'])
 def test_truncated_spans_rejected(self):
  for spec in probe.SITES:
   with self.subTest(site=spec.name):
    # Remove the last decoded instruction boundary, even for one-byte endings.
    listing=[i for i in self.decoded[(spec.function_start,spec.function_end)] if i.end!=spec.end]
    self.assertFalse(probe.common.inspect_site(self.image,spec,listing)['whole_instructions'])
 def test_changed_byte_rejected_each_site(self):
  for spec in probe.SITES:
   with self.subTest(site=spec.name):
    wrong=dataclasses.replace(spec,expected=bytes([spec.expected[0]^1])+spec.expected[1:])
    self.assertFalse(probe.common.inspect_site(self.image,wrong,self.decoded[(spec.function_start,spec.function_end)])['bytes_ok'])
 def test_updater_has_both_native_return_paths(self):
  ends={s.va for s in probe.SITES if s.name.startswith('chase_update_end')}
  self.assertEqual(ends,{0x4216c3,0x4216d3})
if __name__=='__main__':unittest.main()
