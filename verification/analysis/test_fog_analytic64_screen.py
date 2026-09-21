import copy,importlib.util,unittest
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]; SPEC=importlib.util.spec_from_file_location('fog_analytic64_screen',ROOT/'tools/analysis/fog_analytic64_screen.py'); screen=importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(screen)
class FogAnalytic64Screen(unittest.TestCase):
 def test_subset_is_exact_from_128x72(self):
  pose=screen.screen.pose_rows()[0]; direction,indices,x,y=screen.subset_rays(pose); self.assertEqual(direction.shape,(576,3)); self.assertEqual((int(x[0]),int(y[0])),(2,2)); self.assertEqual((int(x[-1]),int(y[-1])),(126,70)); np.testing.assert_array_equal(indices,y*128+x)
 def test_one_global64_grid_recomposes_segments(self):
  original=screen.density_at; screen.density_at=lambda points:(np.ones(len(points),np.float32),np.full(len(points),.5,np.float32))
  try:
   pose=screen.screen.pose_rows()[0]; direction,_,_,_=screen.subset_rays(pose); row=screen.integrate_grid(pose['origin'],direction[:2],np.array([200000.,12001.],np.float32),np.array([.2,.5,.8],np.float32),'steps',64,True)
  finally: screen.density_at=original
  np.testing.assert_array_equal(row['cost']['stations'],[64,64]); self.assertLessEqual(row['composition_error']['T'].max(),2e-7); self.assertLessEqual(row['composition_error']['S'].max(),2e-7)
 def test_dense_global_grid_counts(self):
  original=screen.density_at; screen.density_at=lambda points:(np.ones(len(points),np.float32),np.zeros(len(points),np.float32))
  try:
   pose=screen.screen.pose_rows()[0]; direction,_,_,_=screen.subset_rays(pose); row=screen.integrate_grid(pose['origin'],direction[:3],np.array([1000.,12001.,200000.],np.float32),np.array([.2,.5,.8],np.float32),'spacing',64.,True)
  finally: screen.density_at=original
  np.testing.assert_array_equal(row['cost']['stations'],[16,188,3125])
 def test_exact_depth_witness_pixels_and_view_length(self):
  self.assertEqual(screen.WITNESS_PIXELS,((0,0),(31,0),(0,17),(31,17),(16,9))); pose=screen.screen.pose_rows()[0]; _,_,x,y=screen.subset_rays(pose); wi=screen.witness_indices(); lengths=screen.view_lengths(x[wi],y[wi]); self.assertTrue(np.isfinite(lengths).all() and np.all(lengths>0)); requested=np.tile(np.asarray(screen.DEPTH_LIMITS),len(wi)); reconstructed=np.minimum((requested/np.repeat(lengths,len(screen.DEPTH_LIMITS)))*np.repeat(lengths,len(screen.DEPTH_LIMITS)),screen.fog.FAR); np.testing.assert_allclose(reconstructed,np.minimum(requested,screen.fog.FAR),rtol=0,atol=1e-9)
 def test_invalid_zero_limit_and_alpha_laws(self):
  laws=screen.laws(np.array([.2,.5,.8],np.float32)); self.assertTrue(laws['zero_limit_identity']); self.assertTrue(laws['source_alpha_0_037_1_preserved_exact']); self.assertTrue(all(laws.values()),laws)
 def test_candidate_and_reference_thresholds(self):
  good={'T':{'p99':.001,'max':.003},'S_normalized_unit_radiance':[{'p99':.0005,'max':.002}]*3}; bad=copy.deepcopy(good); self.assertTrue(screen.candidate_pass(good)); bad['T']['max']=.0031; self.assertFalse(screen.candidate_pass(bad)); self.assertTrue(screen.reference_pass({'T':{'p99':.00025,'max':.00075}}))
  self.assertTrue(screen.composition_pass({'T':{'max':2e-6},'S':{'max':2e-6}})); self.assertFalse(screen.composition_pass({'T':{'max':2.1e-6},'S':{'max':0.}}))
 def test_branch_coherence_uses_fixed_2x2_step_groups(self):
  empty=np.zeros((64,18*32),bool); mixed=empty.copy(); mixed[:,0]=True; row=screen.branch_coherence_masks([empty,mixed]); self.assertEqual(row['total_2x2_step_groups'],2*64*9*16); self.assertEqual(row['mixed'],64); self.assertEqual(row['homogeneous_active'],0); self.assertEqual(row['homogeneous_empty'],row['total_2x2_step_groups']-64)
 def test_failure_witness_includes_nonsegment_gate(self):
  composition={'candidate64_total':{'T':{'max':0.},'S':{'max':0.}},'dense64':{'T':{'max':0.},'S':{'max':0.}},'dense128':{'T':{'max':3e-6},'S':{'max':0.}}}; segment={'candidate_passed':True,'reference_converged':True}; pop={'segments':{'near':segment,'shell':segment,'full':segment},'composition':composition}; poses={name:{'sky':pop,'geometry':pop,'invalid_depth_cases':[{'identity':True}]} for name in ('A','B')}; failed=screen.failed_gate_list(poses,True,{'example':False}); self.assertIn('A/sky/composition/dense128',failed); self.assertIn('law/example',failed)
if __name__=='__main__': unittest.main()
