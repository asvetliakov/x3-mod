import importlib.util,tempfile,unittest
from pathlib import Path
from unittest import mock
import numpy as np
ROOT=Path(__file__).resolve().parents[2]; s=importlib.util.spec_from_file_location('density_runtime',ROOT/'tools/analysis/fog_density_runtime_screen.py'); m=importlib.util.module_from_spec(s); s.loader.exec_module(m)
class DensityRuntime(unittest.TestCase):
 def test_explicit_rotation_matches_scalar(self):
  x=np.array([[-3.,4.,5.],[1e6,-2e6,3e6]]); R=m.screen.R; scalar=np.array([[sum(v[j]*R[i,j] for j in range(3)) for i in range(3)] for v in x]); np.testing.assert_allclose(m.rotate(x,R),scalar,rtol=0,atol=1e-12)
 def test_field_is_frozen_refinement(self):
  ref=m.load('ref_test',m.HERE/'fog_mass_detail_refinement.py'); x=np.array([[-300000.,200000.,-100000.],[0,0,0],[65536,1,-2]],np.float64); np.testing.assert_array_equal(m.field(x),ref.refined_density(x))
 def test_candidate_has_24_plus_40_global_bins(self):
  lo,hi,d,ds,a,c=m.bins(np.array([200000.,12000.,1000.]),'candidate',0); np.testing.assert_array_equal(c,[64,24,24]); self.assertEqual(int(a[:,0].sum()),64); self.assertAlmostEqual(float(ds[:,0].sum()),200000)
 def test_pack_lane_and_group_transition(self):
  local=np.array([[0,0,z] for z in (0,1,2,3,4,127)]); x,y,l=m.pack_address(local); self.assertEqual(l.tolist(),[0,1,2,3,0,3]); self.assertEqual((x[3],y[3]),(0,0)); self.assertEqual((x[4],y[4]),(128,0)); self.assertEqual((x[-1],y[-1]),(896,384))
 def test_lazy_absolute_nodes_and_shift_identity(self):
  st=m.LazyStore(); p=np.array([[-100.,200.,-300.],[10.,20.,30.]]); a=st.sample_level('fine',p,np.zeros(3)); b=st.sample_level('fine',p,np.array([512.,0,0])); np.testing.assert_allclose(a,b,rtol=0,atol=2e-7)
 def test_missing_window_rejected(self):
  st=m.LazyStore()
  with self.assertRaises(ValueError): st.sample_level('fine',np.array([[1e9,0,0]]),np.zeros(3))
 def test_dense_counts_and_composition(self):
  old=m.field; m.field=lambda p:np.zeros(len(p),np.float32)
  try:
   pose=m.screen.pose_rows()[0]; d=m.rays(pose)[:2]; r=m.integrate(pose['origin'],d,np.array([200000.,1000.]),np.array([.2,1,.1],np.float32),m.analytic_eval,'dense',64.)
  finally: m.field=old
  self.assertEqual(r['_cost']['sample_counts'],[3125,16]); self.assertLessEqual(r['_cost']['composition_max'],2e-6); np.testing.assert_array_equal(r['full']['T'],[1,1])
 def test_population_is_saved_pixel_subset(self):
  p=m.populations(); self.assertEqual(len(p['stratified'][0]),576); self.assertEqual(len(p['central_crop'][0]),576); self.assertEqual((p['stratified'][0][0],p['stratified'][1][0]),(2,2)); self.assertEqual((p['central_crop'][0][0],p['central_crop'][1][0]),(48,27))
 def test_laws_hold_and_carry_no_vacuous_alpha_law(self):
  row=m.laws(m.LazyStore(),np.array([.2,1.,.1],np.float32)); self.assertNotIn('source_alpha_0_037_1_preserved_exact',row); self.assertFalse([k for k in row if 'alpha' in k]); self.assertTrue(all(row.values()),row)
 def test_dense_read_scan_finds_worst_case_between_coarse_points(self):
  limits=np.arange(0.,m.FAR+1.,100.); scan=m.candidate_reads(limits); worst=int(np.argmax(scan)); self.assertEqual(int(scan[worst]),172); self.assertEqual(float(limits[worst]),29300.)
  self.assertEqual(m.candidate_reads((m.FAR,)).tolist(),[132]); self.assertEqual(m.candidate_reads((1000.,11999.,12001.,150001.,200000.)).tolist(),[48,48,128,134,132]); self.assertEqual(int(m.candidate_reads((0.,))[0]),0)
 def test_density_reads_are_running_aggregates(self):
  st=m.LazyStore(); st.get=lambda name,keys:np.zeros(np.asarray(keys).shape[:-1],np.float16)
  pose=m.screen.pose_rows()[0]; d=m.rays(pose)[:2]; r=m.integrate(pose['origin'],d,np.full(2,m.FAR,np.float32),np.array([.2,1,.1],np.float32),m.filtered_eval(st),'candidate',0)
  cost=r['_cost']['density_reads']; self.assertEqual(cost['sampled_points'],128); self.assertEqual(cost['density_read_total'],264); self.assertEqual(cost['max_reads_per_sample'],4); self.assertEqual(cost['mean_reads_per_ray'],132.)
  self.assertEqual(r['_cost']['density_samples'],{'total':0,'max_per_ray':0,'rays':2})
 def test_plan_digest_is_checked_before_any_output(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp); bad=root/'plan.md'; bad.write_text('not the ratified plan\n'); out=root/'out'
   with self.assertRaises(ValueError) as e: m.run(root/'missing-assets',out,bad)
   self.assertIn('plan digest mismatch',str(e.exception)); self.assertFalse(out.exists())
   with self.assertRaises(ValueError) as e: m.run(root/'missing-assets',out,root/'no-such-plan.md')
   self.assertIn('plan file not found',str(e.exception)); self.assertFalse(out.exists())
 def test_existing_output_directory_is_refused(self):
  # Independent of the living ratified plan, whose status header changes after
  # a run: a temporary plan passes the startup digest check (patched expected
  # digest), so the refusal under test is the output directory's alone. The
  # pinned digest of the historical run stays as it is in the tool.
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp); plan=root/'plan.md'; plan.write_text('# stand-in plan\n'); out=root/'out'; out.mkdir()
   with mock.patch.object(m,'EXPECTED_PLAN',m.digest(plan)):
    with self.assertRaises(ValueError) as e: m.run(root/'missing-assets',out,plan)
   self.assertIn('already exists',str(e.exception)); self.assertNotIn('digest mismatch',str(e.exception))
 def test_default_plan_points_at_the_ratified_document(self):
  self.assertEqual(m.DEFAULT_PLAN.name,'fog-density-runtime-plan.md'); self.assertEqual(len(m.EXPECTED_PLAN),64)
if __name__=='__main__': unittest.main()
