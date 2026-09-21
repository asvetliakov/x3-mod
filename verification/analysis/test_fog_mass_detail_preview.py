import hashlib,importlib.util,tempfile,unittest
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2]; SPEC=importlib.util.spec_from_file_location('fog_mass_detail_preview',ROOT/'tools/analysis/fog_mass_detail_preview.py'); preview=importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(preview)
class FogMassDetailPreview(unittest.TestCase):
 def test_transport_composition(self):
  a={'S':np.array([[.1,.2,.3]],np.float32),'T':np.array([.8],np.float32)}; b={'S':np.array([[.05,.04,.03]],np.float32),'T':np.array([.7],np.float32)}; c=preview.compose(a,b)
  np.testing.assert_array_equal(c['S'],a['S']+a['T'][:,None]*b['S']); np.testing.assert_array_equal(c['T'],a['T']*b['T'])
 def test_area_average_averages_S_and_T_directly(self):
  S=np.arange(preview.HEIGHT*2*preview.WIDTH*2*3,dtype=np.float32).reshape(-1,3); T=np.arange(preview.HEIGHT*2*preview.WIDTH*2,dtype=np.float32); row=preview.downsample_area({'S':S,'T':T})
  expected=T.reshape(preview.HEIGHT,2,preview.WIDTH,2).mean(axis=(1,3)); np.testing.assert_allclose(row['T'].reshape(preview.HEIGHT,preview.WIDTH),expected)
 def test_area_average_constant_identity(self):
  n=preview.HEIGHT*2*preview.WIDTH*2; row=preview.downsample_area({'S':np.full((n,3),.25,np.float32),'T':np.full(n,.75,np.float32)}); np.testing.assert_array_equal(row['S'],np.full((preview.HEIGHT*preview.WIDTH,3),.25,np.float32)); np.testing.assert_array_equal(row['T'],np.full(preview.HEIGHT*preview.WIDTH,.75,np.float32))
 def test_convergence_gate(self):
  z={'S':np.zeros((4,3),np.float32),'T':np.ones(4,np.float32)}; good={'S':z['S'].copy(),'T':z['T']-0.0001}; bad={'S':z['S'].copy(),'T':z['T']-0.001}; self.assertTrue(preview.convergence_pass(preview.convergence(z,good))); self.assertFalse(preview.convergence_pass(preview.convergence(z,bad)))
 def test_exact_segment_composition_for_constant_density(self):
  original=preview.screen.fields
  def constant(points):
   shape=np.asarray(points).shape[:-1]; rho=np.full(shape,.5,np.float32); return {'mass_only':rho,'mass_plus_erosion':rho}
  preview.screen.fields=constant
  try: row=preview.integrate(np.zeros(3),np.array([[1.,0,0]],np.float32),128.,np.array([.2,.5,.8],np.float32),preview.screen.SIGMA)
  finally: preview.screen.fields=original
  for arm in ('mass_only','mass_plus_erosion'):
   self.assertAlmostEqual(float(-np.log(row[arm]['near']['T'][0])),preview.screen.SIGMA*.5*preview.fog.NEAR,places=6)
   self.assertAlmostEqual(float(-np.log(row[arm]['full']['T'][0])),preview.screen.SIGMA*.5*175000,places=5)
 def test_saved_A_B_true_pixel_ray_counts(self):
  for pose in preview.screen.pose_rows():
   low=preview.connected.camera_rays(pose['origin'],pose['forward'],pose['up'],128,72); high=preview.connected.camera_rays(pose['origin'],pose['forward'],pose['up'],256,144); self.assertEqual(low.shape,(9216,3)); self.assertEqual(high.shape,(36864,3))
 def test_laws_with_fixed_chroma(self): self.assertTrue(all(preview.laws(np.array([.2,.5,.8],np.float32)).values()))
 def test_premultiplied_density_chroma_construction(self):
  rho=np.array([0.,.25,1.],np.float32); chroma=np.array([.2,.5,.8],np.float32); rgba=preview.make_rgba(rho,chroma)
  np.testing.assert_array_equal(rgba[:,:3],rho[:,None]*chroma); np.testing.assert_array_equal(rgba[:,3],rho); np.testing.assert_array_equal(rgba[0],np.zeros(4,np.float32))
 def test_result_hash_validator(self):
  from PIL import Image
  with tempfile.TemporaryDirectory() as tmp:
   out=Path(tmp); images={}
   expected={f'green-pose{pose}-{arm}-cloud-only.png':(2982,1098) for pose in ('A','B') for arm in ('mass_only','mass_plus_erosion')}
   expected.update({f'green-poseB-{arm}-area-witness.png':(2342,1098) for arm in ('mass_only','mass_plus_erosion')})
   for name,size in expected.items():
    p=out/name; Image.new('RGB',size).save(p); images[p.name]=hashlib.sha256(p.read_bytes()).hexdigest()
   result={'schema':1,'poses':{'A':{},'B':{}},'images':images}; preview.validate(result,out)
   victim=out/'green-poseA-mass_only-cloud-only.png'; victim.write_bytes(b'x')
   with self.assertRaisesRegex(ValueError,'hash mismatch'): preview.validate(result,out)
   Image.new('RGB',(2,2)).save(victim); result['images'][victim.name]=hashlib.sha256(victim.read_bytes()).hexdigest()
   with self.assertRaisesRegex(ValueError,'dimensions mismatch'): preview.validate(result,out)
   wrong=dict(result); wrong['images']=dict(result['images']); wrong['images']['wrong.png']=wrong['images'].pop(victim.name)
   with self.assertRaisesRegex(ValueError,'exact six-image manifest'): preview.validate(wrong,out)
if __name__=='__main__': unittest.main()
