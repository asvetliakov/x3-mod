"""Focused refusal cases for the relocated cursor-admission branch."""
import struct
import sys
import unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'verification/probe'))
import verify_chase_fire_site as probe
from test_chase_aim_sites import synthetic_image

class Site(unittest.TestCase):
    def verify(self,changes=(),source=None):
        data=synthetic_image(extra=((probe.SPEC.va-len(probe.CMP),probe.CMP),(probe.SPEC.va,probe.SPEC.expected),*changes))
        return probe.verify(data,source)  # in-memory image bytes, no PE written out
    def test_valid_relative_branch(self):
        self.assertTrue(self.verify()['passed'])
    def test_compare_corruption(self):
        self.assertFalse(self.verify(((probe.SPEC.va-1,b'\x01'),))['passed'])
    def test_wrong_branch_target(self):
        self.assertFalse(self.verify(((probe.SPEC.va+2,b'\x65'),))['passed'])
    def test_interior_branch(self):
        at=probe.SPEC.va-0x20
        jump=b'\xe9'+struct.pack('<i',probe.SPEC.va+2-(at+5))
        self.assertFalse(self.verify(((at,jump),))['passed'])
    def test_source_relocation_refusal(self):
        self.assertFalse(self.verify(source=probe.SOURCE.read_text().replace('6,0,2','6,0,0'))['passed'])
    def test_source_span_refusal(self):
        self.assertFalse(self.verify(source=probe.SOURCE.read_text().replace('6,0,2','5,0,2'))['passed'])

if __name__=='__main__':unittest.main()
