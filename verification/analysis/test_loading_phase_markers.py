"""Synthetic `loading_phase` lines through the analyzer's extractor; no game content."""
import sys
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'/'analysis'))
from analyze_loading_phases import LOADING_PHASE_NAMES,extract_loading_phases
def marker(name,frame,elapsed,stall=0):
    return f'loading_phase name={name} frame={frame} elapsed_ms={elapsed} stall_ms={stall} device=1 qpc={1000+elapsed}\n'
LOG=('frame_end device=1 frame=0 draws=0 capture=0 present=00000000 elapsed_ms=1469 dt_ms=0 qpc=5\n'
     +marker('menu_shown',5,10586,6376)
     +'telemetry_summary device=1 frame=214 qpc=99 since_start_us=19.0\n'
     +marker('save_load_begin',214,14650)+marker('save_load_complete',215,33500,18850)
     +'frame_end device=1 frame=300 draws=265 capture=0 present=00000000 elapsed_ms=34899 dt_ms=33429 qpc=7\n')
class LoadingPhaseMarkers(unittest.TestCase):
    def test_three_markers_and_deltas(self):
        result=extract_loading_phases(LOG)
        self.assertEqual([m['name'] for m in result['markers']],list(LOADING_PHASE_NAMES))
        self.assertEqual([m['frame'] for m in result['markers']],[5,214,215])
        self.assertEqual(result['menu_ms'],10586)
        self.assertEqual(result['menu_to_save_ms'],14650-10586)
        self.assertEqual(result['save_load_ms'],18850)
        self.assertEqual(result['markers'][2]['stall_ms'],18850)
        self.assertEqual(result['rejected'],[])
    def test_missing_end_leaves_delta_unset(self):
        result=extract_loading_phases(marker('menu_shown',5,10586)+marker('save_load_begin',214,14650))
        self.assertEqual(result['menu_to_save_ms'],4064);self.assertIsNone(result['save_load_ms'])
        self.assertEqual(extract_loading_phases('')['markers'],[])
    def test_duplicate_unknown_and_malformed_rejected(self):
        text=marker('menu_shown',5,100)+marker('menu_shown',6,200)+marker('other',1,2)+'loading_phase name=save_load_begin frame=x elapsed_ms=3\n'
        result=extract_loading_phases(text)
        self.assertEqual(result['menu_ms'],100);self.assertEqual(len(result['markers']),1)
        self.assertEqual([r['line'] for r in result['rejected']],[2,3,4])
if __name__=='__main__':unittest.main()
