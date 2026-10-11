"""Structural acceptance and refusal tests for the four engine-path view-restore sites."""
import dataclasses
import unittest
from pathlib import Path
import verify_chase_restore_sites as probe
from verification.analysis.test_chase_aim_sites import synthetic_image
class RestoreSites(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  # The image stays in memory (verify_chase_aim_sites.objdump_window): a
  # synthetic PE on disk is quarantined by Microsoft Defender for Endpoint.
  cls.data=synthetic_image(extra=tuple((s.va,s.expected) for s in probe.SITES),text_size=0x140000)
  cls.decoded=probe.decode(cls.data)
  cls.image=probe.common.Image(cls.data)
 def test_production_specs(self):
  self.assertTrue(probe.common.check_source_specs(probe.spec_table(probe.SOURCE.read_text(),'restore_specs'),probe.SITES)['ok'])
 def test_four_sites_match_design_table(self):
  expected={0x410100:'558bec83e4f0',0x49ea80:'83ec08558b6c2410',0x4a0880:'6aff68c8005300',0x52f298:'b8f4e55600'}
  self.assertEqual({s.va:s.expected.hex() for s in probe.SITES},expected)
  self.assertEqual(probe.INPUT_ROUTINE,(0x410100,0x412250))
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
    listing=[i for i in self.decoded[(spec.function_start,spec.function_end)] if i.end!=spec.end]
    self.assertFalse(probe.common.inspect_site(self.image,spec,listing)['whole_instructions'])
 def test_changed_byte_rejected_each_site(self):
  for spec in probe.SITES:
   with self.subTest(site=spec.name):
    wrong=dataclasses.replace(spec,expected=bytes([spec.expected[0]^1])+spec.expected[1:])
    self.assertFalse(probe.common.inspect_site(self.image,wrong,self.decoded[(spec.function_start,spec.function_end)])['bytes_ok'])
 def test_input_routine_extent_needs_ret_calls_and_no_escaping_jumps(self):
  I=probe.common.Instruction;start,end=probe.INPUT_ROUTINE
  call=lambda va:I(va,b'\xe8\x00\x00\x00\x00','call',hex(probe.SCRIPT_CALL))
  body=[I(start,b'\x55','push','ebp')]+[call(start+1+5*i) for i in range(13)]
  filler=I(start+66,b'\x90'*(end-start-67),'nop','')
  good=body+[filler,I(end-1,b'\xc3','ret','')]
  self.assertTrue(probe.input_routine_extent(good)['ok'])
  self.assertFalse(probe.input_routine_extent(good[:-1]+[I(end-1,b'\x90','nop','')])['ok'])
  self.assertFalse(probe.input_routine_extent(body[:-1]+[filler,I(end-6,b'\x90'*5,'nop',''),I(end-1,b'\xc3','ret','')])['ok'])
  escaping=body+[I(start+66,b'\xe9\x00\x00\x00\x00','jmp',hex(end+0x100)),I(start+71,b'\x90'*(end-start-72),'nop',''),I(end-1,b'\xc3','ret','')]
  self.assertFalse(probe.input_routine_extent(escaping)['ok'])
if __name__=='__main__':unittest.main()
