"""Structural acceptance and refusal tests for the seven view-restore sites."""
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
 def test_seven_sites_match_run60_proof_table(self):
  expected={0x4a3ffd:'03700c803e08',0x4a2260:'538b5c240c',0x4a2420:'538b5c240c',0x49ea80:'83ec08558b6c2410',0x49c9a0:'5333db895e04',0x4a0880:'6aff68c8005300',0x52f298:'b8f4e55600'}
  self.assertEqual({s.va:s.expected.hex() for s in probe.SITES},expected)
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
if __name__=='__main__':unittest.main()
