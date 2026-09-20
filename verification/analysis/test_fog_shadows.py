import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
import numpy as np
import fog_shadow_reference as shadow
import fog_spatial_reference as spatial
ROOT=Path(__file__).resolve().parents[2]
F=np.float32

class ShadowTests(unittest.TestCase):
    def cascade(self,depth=0,scale=1):
        rows=np.zeros((3,4),F);rows[0,0]=rows[1,1]=scale;rows[2,2]=.00001;rows[2,3]=.5
        return dict(rows=rows,map=np.full((64,64),depth,F),bias=1e-5,valid=True)
    def test_pcf_filters_comparisons_at_d3d9_centres(self):
        d=np.zeros((64,64),F);d[:,32:]=1
        p=np.array([[-1/64,0,.5],[0,0,.5],[1/64,0,.5]],F)
        np.testing.assert_array_equal(shadow.pcf(d,p,0),[.5,1,1])
    def test_thin_occluder_from_two_views(self):
        c=self.cascade(1);c['map'][:,32]=0
        p=np.array([[0,0,100],[1/32,0,100]],F)
        np.testing.assert_array_equal(shadow.visibility(p,[c]),[0,1])
        # Move camera/view transform but keep the same world occluder.
        moved=dict(c,rows=c['rows'].copy());moved['rows'][0,3]=.25
        p[:,0]-=.25
        np.testing.assert_array_equal(shadow.visibility(p,[moved]),[0,1])
    def test_edge_blends_to_coarser_or_lit_continuously(self):
        p=np.array([[.84999,0,10],[.85,0,10],[.9,0,10],[.95,0,10],[.95001,0,10]],F)
        v=shadow.visibility(p,[self.cascade(0),self.cascade(1,.5)])
        np.testing.assert_allclose(v,[0,0,.5,1,1],atol=1e-6)
        np.testing.assert_array_equal(v,shadow.visibility(p,[self.cascade(0)]))
        np.testing.assert_array_equal(shadow.visibility(p,[self.cascade(0),self.cascade(0,.5)]),np.zeros(5))
    def test_unavailable_and_outside_cascade_fallback(self):
        c=self.cascade();c['valid']=False;p=np.array([[0,0,10],[2,0,10]],F)
        np.testing.assert_array_equal(shadow.visibility(p,[c]),[1,1])
        np.testing.assert_array_equal(shadow.visibility(p,[c,self.cascade(0,.2)]),[0,0])
    def test_bias_changes_compare_not_coverage(self):
        c=self.cascade(.5);p=np.array([[0,0,1]],F)
        c['bias']=0;self.assertEqual(shadow.visibility(p,[c])[0],0)
        c['bias']=.001;self.assertEqual(shadow.visibility(p,[c])[0],1)
    def test_shadow_changes_scattering_only_and_empty_identity(self):
        class Helper:
            @staticmethod
            def sample(_,p):
                field=np.ones(p.shape[:-1]+(4,),F);field[0]=0;return field
        c=np.zeros((8,4),F);c[2,3]=1e-5;c[3]=[0,0,1,12000];c[4,0]=c[5,1]=c[6,2]=1
        direction=np.tile(np.array([0,0,1],F),(4,1));limit=np.full(4,12000,F)
        base=spatial.march(Helper,None,c,direction,limit)
        lit=spatial.march(Helper,None,c,direction,limit,visibility=shadow.world_visibility(c,[]))
        dark=spatial.march(Helper,None,c,direction,limit,visibility=shadow.world_visibility(c,[self.cascade(0,.00001)]))
        np.testing.assert_array_equal(base,lit);np.testing.assert_array_equal(base[:,3],dark[:,3]);np.testing.assert_array_equal(dark[:,:3],0)
        np.testing.assert_array_equal(dark[0],[0,0,0,1]);self.assertTrue((base[1:,:3]>0).all())
    def test_frame_and_rows_contract_native(self):
        compiler=shutil.which('clang++') or shutil.which('c++')
        with tempfile.TemporaryDirectory(prefix='fog-shafts-host-') as tmp:
            source=Path(tmp)/'test.cpp';exe=Path(tmp)/'test'
            source.write_text('''#include "fog_pass_math.h"
#include <limits>
using namespace x3m::renderer;
int main(){float r[12]={1,0,0,0,0,1,0,0,0,0,1,0};
if(!fog_shadow_current(0,0)||fog_shadow_current(4,3)||fog_shadow_current(3,4)||fog_shadow_current(~0ull,~0ull))return 1;
if(!fog_shadow_rows(r,0)||fog_shadow_rows(r,-1)||fog_shadow_rows(r,2))return 2;
r[0]=std::numeric_limits<float>::quiet_NaN();if(fog_shadow_rows(r,0))return 3;
r[0]=0;if(fog_shadow_rows(r,0))return 4;return 0;}''')
            subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-I'+str(ROOT/'src/renderer'),str(source),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
