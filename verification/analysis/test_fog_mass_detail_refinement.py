import hashlib,importlib.util,tempfile,unittest
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]; SPEC=importlib.util.spec_from_file_location('fog_mass_detail_refinement',ROOT/'tools/analysis/fog_mass_detail_refinement.py'); refinement=importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(refinement)
class FogMassDetailRefinement(unittest.TestCase):
 def test_exact_fixed_formula(self):
  points=np.array([[0.,0.,0.],[12345.,-6789.,2222.],[-30000.,40000.,-50000.]])
  row=refinement.screen.fields(points); expected=np.maximum(0,row['B']*(.50+.50*row['Dlo'])-.20*(1-row['B'])*row['Dhi']).astype(np.float32); np.testing.assert_array_equal(refinement.refined_density(points),expected); self.assertTrue(np.all((expected>=0)&(expected<=row['B'])))
  self.assertTrue(np.all(expected<=row['mass_plus_erosion']))
 def test_no_support_outside_mass_or_peak_boost(self):
  original=refinement.screen.fields
  def fixed(points):
   shape=np.asarray(points).shape[:-1]; B=np.array([0.,.25,1.],np.float32).reshape(shape); return {'B':B,'Dlo':np.ones(shape,np.float32),'Dhi':np.ones(shape,np.float32)}
  refinement.screen.fields=fixed
  try: rho=refinement.refined_density(np.zeros((3,3)))
  finally: refinement.screen.fields=original
  self.assertEqual(float(rho[0]),0.); self.assertLessEqual(float(rho.max()),1.)
 def test_constant_density_segment_composition(self):
  original=refinement.refined_density; refinement.refined_density=lambda points:np.full(np.asarray(points).shape[:-1],.5,np.float32)
  try: row=refinement.integrate(np.zeros(3),np.array([[1.,0,0]],np.float32),128.,np.array([.2,.5,.8],np.float32),refinement.screen.SIGMA)
  finally: refinement.refined_density=original
  self.assertAlmostEqual(float(-np.log(row['near']['T'][0])),refinement.screen.SIGMA*.5*refinement.fog.NEAR,places=6); self.assertAlmostEqual(float(-np.log(row['full']['T'][0])),refinement.screen.SIGMA*.5*175000,places=5)
 def test_saved_A_B_rays(self):
  self.assertEqual([p['name'] for p in refinement.screen.pose_rows()],['A','B'])
  for pose in refinement.screen.pose_rows(): self.assertEqual(refinement.connected.camera_rays(pose['origin'],pose['forward'],pose['up'],128,72).shape,(9216,3))
 def test_laws(self): self.assertTrue(all(refinement.laws(np.array([.2,.5,.8],np.float32)).values()),refinement.laws(np.array([.2,.5,.8],np.float32)))
 def test_exact_result_manifest(self):
  from PIL import Image
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp); expected={f'green-pose{p}-refined-detail-cloud-only.png':(2982,1098) for p in ('A','B')}; expected['green-poseB-refined-detail-area-witness.png']=(2342,1098); images={}
   for name,size in expected.items(): path=out/name; Image.new('RGB',size).save(path); images[name]=hashlib.sha256(path.read_bytes()).hexdigest()
   result={'schema':1,'images':images}; refinement.validate(result,out); victim=out/'green-poseA-refined-detail-cloud-only.png'; victim.write_bytes(b'x')
   with self.assertRaisesRegex(ValueError,'hash mismatch'): refinement.validate(result,out)
   Image.new('RGB',(2,2)).save(victim); result['images'][victim.name]=hashlib.sha256(victim.read_bytes()).hexdigest()
   with self.assertRaisesRegex(ValueError,'dimensions mismatch'): refinement.validate(result,out)
   wrong={'schema':1,'images':dict(result['images'])}; wrong['images']['wrong.png']=wrong['images'].pop(victim.name)
   with self.assertRaisesRegex(ValueError,'exact three-image manifest'): refinement.validate(wrong,out)
if __name__=='__main__': unittest.main()
