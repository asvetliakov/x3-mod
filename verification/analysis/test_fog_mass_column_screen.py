import importlib.util,json,unittest
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]; SPEC=importlib.util.spec_from_file_location('fog_mass_column_screen',ROOT/'tools/analysis/fog_mass_column_screen.py'); screen=importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(screen)
class FogMassColumnScreen(unittest.TestCase):
 def test_fixed_field_formula_and_support(self):
  points=np.array([[0.,0.,0.],[12345.,-6789.,2222.],[-30000.,40000.,-50000.]])
  row=screen.fields(points); manual=(2*screen.patches.value_noise(points/(2*screen.P),3)+screen.patches.value_noise((points@screen.R.T)/screen.P,4))/3
  np.testing.assert_allclose(row['f'],manual,rtol=0,atol=1e-7); np.testing.assert_array_equal(row['mass_only'],row['B']); self.assertTrue(np.all((row['mass_plus_erosion']>=0)&(row['mass_plus_erosion']<=row['B'])))
 def test_threshold_extremes_are_exact(self):
  original=screen.patches.value_noise
  try:
   screen.patches.value_noise=lambda points,octave: np.full(np.asarray(points).shape[:-1],-1 if octave in (3,4) else 0,np.float32)
   low=screen.fields(np.zeros((2,3))); self.assertTrue(np.all(low['B']==0) and np.all(low['mass_plus_erosion']==0))
   screen.patches.value_noise=lambda points,octave: np.full(np.asarray(points).shape[:-1],1,np.float32)
   high=screen.fields(np.zeros((2,3))); self.assertTrue(np.all(high['B']==1)); self.assertTrue(np.all(high['mass_plus_erosion']<=1))
  finally: screen.patches.value_noise=original
 def test_exact_bin_overlaps_partition_ranges(self):
  for n in (256,512):
   full=screen.bin_overlap(n,0,screen.fog.FAR); split=sum(screen.bin_overlap(n,a,b) for _,a,b in screen.RANGES[:3])
   np.testing.assert_array_equal(split,full); self.assertEqual(float(full.sum()),screen.fog.FAR); self.assertEqual(float(screen.bin_overlap(n,0,screen.fog.NEAR).sum()),screen.fog.NEAR)
 def test_constant_density_bound(self):
  n=512; distance=((np.arange(n)+.5)*(screen.fog.FAR/n))[:,None]; rho=np.ones((n,1),np.float32); row=screen.summarize_density(rho,distance,n,0,screen.fog.FAR)
  self.assertAlmostEqual(row['windowed_density_integral']['mean'],175000.,places=4); self.assertAlmostEqual(row['tau']['max'],1.640625,places=10); self.assertLessEqual(row['opacity']['max'],1-np.exp(-1.640625)+1e-12)
  self.assertEqual(row['midpoint_positive_chord_length_estimate']['count'],1); self.assertEqual(row['midpoint_positive_chord_length_estimate']['max'],screen.fog.FAR)
 def test_support_chords_split_at_exact_zero_samples(self):
  n=8; distance=((np.arange(n)+.5)*(screen.fog.FAR/n))[:,None]; rho=np.array([[1],[1],[0],[1],[1],[1],[0],[1]],np.float32); row=screen.summarize_density(rho,distance,n,0,screen.fog.FAR)
  self.assertEqual(row['midpoint_positive_chord_length_estimate']['count'],3); self.assertEqual(row['midpoint_positive_chord_length_estimate']['max'],75000.)
 def test_executed_extinction_bounds(self):
  full_tau=screen.SIGMA*175000; near_tau=screen.SIGMA*screen.fog.NEAR
  self.assertAlmostEqual(full_tau,1.640625,places=14); self.assertAlmostEqual(near_tau,.1125,places=14)
  self.assertAlmostEqual(float(np.exp(-full_tau)),.1938588426,places=9); self.assertAlmostEqual(float(-np.expm1(-full_tau)),.8061411574,places=9)
  self.assertAlmostEqual(float(np.exp(-near_tau)),.8935973471,places=9); self.assertAlmostEqual(float(-np.expm1(-near_tau)),.1064026529,places=9)
 def test_fixed_A_B_ray_grids(self):
  rows=screen.pose_rows(); self.assertEqual([x['name'] for x in rows],['A','B'])
  for row in rows:
   _,direction=screen.rays_for_pose(row); self.assertEqual(direction.shape,(144,3)); np.testing.assert_allclose(np.linalg.norm(direction,axis=1),1,atol=1e-7)
 def test_same_stations_feed_both_arms(self):
  row=screen.evaluate_pose(np.zeros(3),np.array([[1.,0,0]],np.float32),256); self.assertEqual(set(row),{'mass_only','mass_plus_erosion'}); self.assertEqual(row['mass_only']['ranges']['complete_0_40km']['rho_samples']['count'],256); self.assertEqual(row['mass_plus_erosion']['ranges']['complete_0_40km']['rho_samples']['count'],256)
 def test_laws_and_json_validation(self):
  self.assertTrue(all(screen.laws().values()),screen.laws())
  result={'schema':1,'operation_counts':{'unique_world_stations':221184},'poses':{p:{'arms':{a:{} for a in ('mass_only','mass_plus_erosion')}} for p in ('A','B')}}; screen.validate(result); json.dumps(result,allow_nan=False)
if __name__=='__main__': unittest.main()
