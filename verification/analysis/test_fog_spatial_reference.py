import unittest
import numpy as np
import fog_spatial_reference as ref

class Helper:
    @staticmethod
    def sample(volume,point):
        result=np.zeros(point.shape[:-1]+(4,),np.float32);result[...,3]=1;result[...,:3]=[.2,.4,.8];return result
    @staticmethod
    def metrics(values):
        return dict(p99=float(np.percentile(values,99)) if values.size else 0,max=float(np.max(values)) if values.size else 0)

class SpatialReference(unittest.TestCase):
    def inputs(self):
        c=np.zeros((8,4),np.float32);c[0]=[1,1,0,0];c[1]=[4,4,2,2];c[2,3]=1e-4;c[3]=[0,0,1,12000];c[4,0]=c[5,1]=c[6,2]=1
        d=np.zeros((4,4,4),np.float32);d[...,0]=.5;d[...,2]=1000
        scene=np.full((4,4,4),.25,'<f2');half=np.zeros((2,2,4),'<f2');half[...,3]=1
        return c,d,scene,half
    def test_actual_nonempty_is_not_rounded_reference_empty(self):
        c,d,scene,half=self.inputs();half[...,3]=np.nextafter(np.float16(1),np.float16(0))
        out,repair,empty,actual=ref.composite(Helper,None,c,d,scene,half)
        self.assertFalse(actual.any());self.assertFalse(empty.any())
    def test_exact_empty_and_alpha(self):
        c,d,scene,half=self.inputs();out,repair,empty,actual=ref.composite(Helper,None,c,d,scene,half)
        self.assertTrue(np.array_equal(out.view('u2'),scene.view('u2')));self.assertTrue(actual.all());self.assertFalse(repair.any())
    def test_invalid_guard(self):
        c,d,scene,half=self.inputs();d[...,2]=np.resize([0,-1,np.nan,np.inf],(4,4));half[...,:3]=1
        out,repair,empty,actual=ref.composite(Helper,None,c,d,scene,half)
        self.assertTrue(np.array_equal(out,scene));self.assertTrue(actual.all());self.assertFalse(repair.any())
    def test_invalid_neighbors_force_valid_pixel_repair(self):
        c,d,scene,half=self.inputs();d[::2,::2,2]=np.nan
        out,repair,empty,actual=ref.composite(Helper,None,c,d,scene,half)
        self.assertTrue(repair[1,1]);self.assertFalse(repair[0,0]);self.assertFalse(np.array_equal(out[1,1],scene[1,1]));self.assertTrue(np.array_equal(out[...,3],scene[...,3]))
    def test_zero_radiance_extinguishes_without_scattering(self):
        c,d,scene,half=self.inputs();y,x=np.mgrid[:2,:2];direction,limit,_=ref.rays(d[:2,:2],c,x,y)
        st=ref.march(Helper,None,c,direction,limit,radiance=(0,0,0));self.assertTrue((st[...,:3]==0).all());self.assertTrue((st[...,3]<1).all())
    def test_numeric_error_fails_independently(self):
        c,d,scene,half=self.inputs();changed=scene.copy();changed[...,:3]*=2
        groups=ref.numeric_groups(Helper,changed,scene,d,np.zeros((4,4),bool));self.assertFalse(groups['all']['passed'])
    def test_default_hg_and_gamma_controls(self):
        c,d,scene,half=self.inputs();y,x=np.mgrid[:2,:2];direction,limit,_=ref.rays(d[:2,:2],c,x,y)
        st=ref.march(Helper,None,c,direction,limit);isotropic=ref.march(Helper,None,c,direction,limit,g=0)
        self.assertTrue(np.array_equal(st[...,3],isotropic[...,3]));self.assertFalse(np.array_equal(st[...,:3],isotropic[...,:3]))
        st=st.astype('<f2');linear=ref.composite(Helper,None,c,d,scene,st,gamma=1)[0];encoded=ref.composite(Helper,None,c,d,scene,st)[0]
        self.assertFalse(np.array_equal(linear,encoded));self.assertTrue(np.array_equal(linear[...,3],encoded[...,3]))

class SupersededEntrypoints(unittest.TestCase):
    def test_historical_runner_never_starts_process(self):
        from unittest.mock import patch
        import run_fog_pass
        with patch.object(run_fog_pass.subprocess,'run',side_effect=AssertionError('process launched')):
            self.assertEqual(run_fog_pass.main(),2)
        self.assertTrue(callable(run_fog_pass.parse))


class ProvenanceGuards(unittest.TestCase):
    def test_case_route_mutation_rejected(self):
        import tempfile
        from pathlib import Path
        import fog_spatial_run as run
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);source=root/'data';source.mkdir();cases=root/'cases.txt'
            text='1974\n'+run.windows(source)+'\n1\n';cases.write_text(text)
            record=dict(cases=[dict(frame='1974',source=str(source),family='bluewell')],inputs={},cases_sha256=run.digest(cases))
            run.verify_inputs(record,cases)
            cases.write_text(text.replace('1974','1975'))
            with self.assertRaisesRegex(ValueError,'routing'):run.verify_inputs(record,cases)
            record['cases_sha256']=run.digest(cases)
            with self.assertRaisesRegex(ValueError,'routing'):run.verify_inputs(record,cases)
    def test_old_executable_or_input_execution_rejected(self):
        import tempfile
        from pathlib import Path
        import fog_spatial_run as run
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);paths=[root/name for name in ('inputs.json','cases.txt','build.json')]
            for i,path in enumerate(paths):path.write_text(str(i))
            build=dict(executable_sha256='new-executable')
            execution=dict(executable_sha256='new-executable',inputs_sha256=run.digest(paths[0]),cases_sha256=run.digest(paths[1]),build_sha256=run.digest(paths[2]))
            run.verify_execution(execution,build,*paths)
            for key in execution:
                changed=dict(execution);changed[key]='stale'
                with self.assertRaisesRegex(ValueError,key):run.verify_execution(changed,build,*paths)

if __name__=='__main__':unittest.main()
