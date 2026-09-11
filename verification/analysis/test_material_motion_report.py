"""Offline acceptance controls for the same-draw native report."""
from pathlib import Path
import importlib.util
import unittest
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('material_motion_report',ROOT/'verification/probe/run_material_motion.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class ReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.text=(ROOT/'verification/results/material-motion.txt').read_text()
    def reject(self,text):
        with self.assertRaises((AssertionError,KeyError,IndexError)):module.validate_report(text)
    def test_current(self):self.assertEqual(module.validate_report(self.text)['configurations'],82)
    def test_zero_checks(self):self.reject('RESULT PASS checks=0 numerical=0 color_components=0 depth_cases=0 configurations=0 devices=2\n')
    def test_missing_check(self):self.reject(self.text.replace('CHECK exact local shader pair transformed PASS\n','',1))
    def test_duplicate_terminal(self):self.reject(self.text+self.text.splitlines()[-1]+'\n')
    def test_trailing_output(self):self.reject(self.text+'extra\n')
    def test_wrong_device(self):self.reject(self.text.replace('DEVICE pure=1','DEVICE pure=0'))
    def test_wrong_module(self):self.reject(self.text.replace('path=C:\\windows\\system32\\d3d9.dll','path=C:\\X3\\d3d9.dll'))
    def test_missing_reset(self):self.reject(self.text.replace('RESET PASS\n','',1))
    def test_wrong_motion_inventory(self):self.reject(self.text.replace('SAMPLE config=1 x=8 y=8 channel=0','SAMPLE config=1 x=8 y=8 channel=1',1))
    def test_missing_timing(self):self.reject('\n'.join(s for s in self.text.splitlines() if not s.startswith('TIMING width=1280 height=768 format=21 iteration=0 '))+'\n')
    def test_wrong_light_control(self):self.reject(self.text.replace('native point-light count changes original material','unrelated control',1))
    def test_wrong_scene_pairs(self):self.reject(self.text.replace('scene_pairs=1','scene_pairs=2',1))
    def test_wrong_draw_count(self):self.reject(self.text.replace('scene_pairs=1 draws=1','scene_pairs=1 draws=2',1))
    def test_wrong_mixed_cap(self):self.reject(self.text.replace('DEVICE pure=0 mixed=1','DEVICE pure=0 mixed=0'))
if __name__=='__main__':unittest.main()
