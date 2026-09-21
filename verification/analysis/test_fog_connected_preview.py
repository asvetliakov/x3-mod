import importlib.util
import unittest
from pathlib import Path

import numpy as np


ROOT=Path(__file__).resolve().parents[2]
SPEC=importlib.util.spec_from_file_location("fog_connected_preview",ROOT/"tools/analysis/fog_connected_preview.py")
preview=importlib.util.module_from_spec(SPEC); SPEC.loader.exec_module(preview)


class FogConnectedPreview(unittest.TestCase):
    def volume(self,rho=.5):
        result=np.empty((8,8,8,4),np.float32); result[...,3]=rho; result[...,:3]=rho*np.array([.2,.5,.8],np.float32)
        return result

    def test_fixed_hash_center_and_orientation(self):
        row=preview.region((0,0,0))
        self.assertEqual(row["hash"],0xddae6da0)
        self.assertEqual(row["axis_hashes"],[0xd01eeab0,0xd37ef856,0xff078614])
        np.testing.assert_array_equal(row["center"],[95576.,97323.,82698.])
        np.testing.assert_allclose(row["Q"],[[.46028039,.82808467,-.32002771],[-.01096809,.3657602,.93064449],
                                              [.88770584,-.42484732,.17743476]],rtol=0,atol=5e-9)
        np.testing.assert_allclose(row["Q"].T@row["Q"],np.eye(3),rtol=0,atol=2e-15)
        self.assertAlmostEqual(float(np.linalg.det(row["Q"])),1.,places=14)

    def test_pair_midpoint_has_positive_core_overlap(self):
        row=preview.region((0,0,0)); point=row["center"][None,:]
        values=[preview.lobe_envelope(point,row,center)[0] for center in row["lobes"]]
        np.testing.assert_array_equal(values,np.ones(2,np.float32))
        envelope,active=preview.union_envelope(point,[row])
        self.assertEqual(float(envelope[0]),1.)
        self.assertEqual(int(active[0]),2)

    def test_radial_harmonic_support_is_bounded(self):
        row=preview.region((0,0,0)); center=row["lobes"][0]
        # Along local +X, h=.925 and q=.925 is exact support boundary.
        point=center+(row["Q"]@np.array([.925*preview.AXES[0],0,0]))
        self.assertEqual(float(preview.lobe_envelope(point[None,:],row,center)[0]),0.)
        core=center+(row["Q"]@np.array([.75*.925*preview.AXES[0],0,0]))
        self.assertEqual(float(preview.lobe_envelope(core[None,:],row,center)[0]),1.)

    def test_union_uses_max_not_additive_density(self):
        row=preview.region((0,0,0)); point=row["center"][None,:]
        individual=[preview.lobe_envelope(point,row,center)[0] for center in row["lobes"]]
        union,_=preview.union_envelope(point,[row])
        self.assertEqual(float(union[0]),float(max(individual)))
        self.assertNotEqual(float(union[0]),float(sum(individual)))

    def test_true_pixel_center_camera_basis(self):
        row=preview.region((0,0,0)); direction=preview.camera_rays(row["center"],row["Q"][:,1],row["Q"][:,2],4,2)
        self.assertEqual(direction.shape,(8,3)); np.testing.assert_allclose(np.linalg.norm(direction,axis=1),1,rtol=0,atol=1e-7)
        self.assertLess(float(np.dot(direction[3],row["Q"][:,0])),0)  # right=cross(up,forward)=-Q local X
        self.assertGreater(float(np.dot(direction[0],row["Q"][:,2])),0)

    def test_merged_intervals_are_sorted_and_nonoverlapping(self):
        row=preview.region((0,0,0)); origin=row["center"]+150000*row["Q"][:,1]
        direction=preview.camera_rays(origin,-row["Q"][:,1],row["Q"][:,2],8,4); limit=np.full(len(direction),preview.fog.FAR)
        intervals,counts=preview.merged_intervals(origin,direction,limit,preview.nearby_regions(origin))
        self.assertTrue(any(counts["simultaneous_bound_overlaps"]>1))
        for parts in intervals:
            self.assertTrue(all(a<b for a,b in parts))
            self.assertTrue(all(parts[i][1]<parts[i+1][0] for i in range(len(parts)-1)))

    def test_union_integration_samples_detail_once_and_clips_depth(self):
        row=preview.region((0,0,0)); origin=row["center"]
        direction=np.array([row["Q"][:,1],-row["Q"][:,1]],np.float32); limit=np.array([20000.,0.],np.float32)
        original=preview.fog.sample_level; sampled=[0]
        def counted(volume,points):
            sampled[0]+=len(points)
            return original(volume,points)
        preview.fog.sample_level=counted
        try:
            result=preview.integrate_union(self.volume(),origin,direction,limit,4e-6,512.,preview.SUN,[row],collect=True)
        finally:
            preview.fog.sample_level=original
        self.assertEqual(sampled[0],int(result["cost"]["true_support_samples"].sum()))
        np.testing.assert_array_equal(result["S"][1],np.zeros(3,np.float32))
        self.assertEqual(float(result["T"][1]),1.)
        self.assertTrue(np.isfinite(result["S"]).all() and np.all((result["T"]>=0)&(result["T"]<=1)))

    def test_sampled_bound_intersections_are_scalar_distinct_counts(self):
        row=preview.region((0,0,0)); origin=row["center"]
        direction=np.array([row["Q"][:,1],-row["Q"][:,1]],np.float32); limit=np.full(2,preview.fog.FAR)
        regions,lobes=preview.sampled_bound_intersections(origin,direction,limit,[row])
        self.assertIsInstance(regions,int); self.assertIsInstance(lobes,int)
        self.assertEqual(regions,1); self.assertEqual(lobes,2)

    def test_reported_laws_pass(self):
        rows=preview.laws()
        self.assertTrue(rows["zero_and_negative_depth_operator_identity_exact"])
        self.assertTrue(rows["nonempty_operator_transport_finite_bounded"])
        self.assertTrue(rows["operator_composite_source_alpha_preserved_exact"])
        self.assertTrue(all(rows.values()),rows)


if __name__=="__main__": unittest.main()
