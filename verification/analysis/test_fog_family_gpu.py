"""Host tests for nontrivial GPU-family evidence, without D3D or Wine execution."""
from pathlib import Path
import tempfile
import unittest
import numpy as np
import fog_family_gpu as family

class FamilyGpuTests(unittest.TestCase):
    def log(self):
        lines=[f'CHECK {name} PASS' for name in sorted(family.REQUIRED)]
        lines += [f'FAMILY_GPU run={i} profile={profile} generation={i+1} nonempty=736 changed=2894 cpu_bytes=17846400 refs=10' for i,profile in enumerate(family.ORDER[:17])]
        return '\n'.join(lines+[family.SCOPE,f'RESULT production_fog checks={len(family.REQUIRED)} PASS'])+'\n'

    def test_complete_log_and_all_required_witnesses(self):
        text=self.log();self.assertEqual(family.validate_log(text),145)
        for name in family.REQUIRED:
            with self.subTest(name=name),self.assertRaises(ValueError):family.validate_log(text.replace(f'CHECK {name} PASS\n',''))

    def test_wrong_family_stale_generation_empty_or_multiple_atlases_fail(self):
        for old,new in [('run=2 profile=3','run=2 profile=2'),('generation=2 ','generation=1 '),('nonempty=736','nonempty=0'),('changed=2894','changed=0'),('cpu_bytes=17846400','cpu_bytes=35692800'),('refs=10','refs=11')]:
            with self.subTest(old=old),self.assertRaises(ValueError):family.validate_log(self.log().replace(old,new,1))
        for text in (self.log()+'RESULT FAIL error=x\n',self.log()+f'CHECK {next(iter(family.REQUIRED))} PASS\n',self.log().replace(family.SCOPE,'')):
            with self.assertRaises(ValueError):family.validate_log(text)

    def test_sampler_wraps_cell_centres_and_all_three_seams(self):
        atlas=np.zeros((1430,1560,4),'<f2')
        for z in (0,127):
            for y in (0,127):
                for x in (0,127):atlas[z//12*130+y+1,z%12*130+x+1]=[int(x==0)+2*int(y==0)+4*int(z==0)]*4
        points=np.array([[0,0,0],[32768,32768,32768],[-32768,-32768,-32768],[128,128,128],[32640,32640,32640]],np.float32)
        actual=family.sample(atlas,points)
        np.testing.assert_array_equal(actual,np.array([[3.5]*4]*3+[[7]*4,[0]*4],np.float32))

    def test_numerical_identity_cannot_pass(self):
        # A reference with visible fog cannot be satisfied by an empty GPU field.
        with tempfile.TemporaryDirectory() as tmp:
            out=Path(tmp);_,_,scene=family.fixed_view()
            st=np.zeros((24,32,4),'<f2');st[...,3]=1
            for profile in range(1,15):
                expected=st.copy();expected[...,:3]=.02;expected[...,3]=.9
                expected.tofile(out/f'reference-{profile}.st.rgba16f')
                (scene+.01).astype('<f2').tofile(out/f'reference-{profile}.composite.rgba16f')
            for run in range(18):
                st.tofile(out/f'family-{run}.st.rgba16f');scene.tofile(out/f'family-{run}.composite.rgba16f')
            result=family.analyze({'order':family.ORDER},out)
            self.assertFalse(result['passed']);self.assertTrue(all(row['nonempty']==0 and row['changed']==0 for row in result['cases']))
