import importlib.util
import unittest
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "fog_banks_replay", ROOT / "tools/analysis/fog_banks_replay.py")
replay = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(replay)


class FogBanksReplay(unittest.TestCase):
    def volume(self, rho=.5):
        result = np.empty((8, 8, 8, 4), np.float32)
        result[..., 3] = rho
        result[..., :3] = rho * np.array([.2, .5, .8], np.float32)
        return result

    def test_fixed_uint32_hash_and_centres(self):
        self.assertEqual(replay.mix32(0), 0)
        self.assertEqual(replay.mix32(0x58434647), 0xddae6da0)
        np.testing.assert_allclose(replay.bank_center((0, 0, 0)),
                                   [55980., 56853.5, 49541.], rtol=0, atol=0)
        np.testing.assert_allclose(replay.bank_center((-1, 2, -3)),
                                   [-56936.5, 238900.75, -241189.75], rtol=0, atol=0)

    def test_feather_is_bounded_with_exact_core_and_vacuum(self):
        values = replay.envelope(np.array([0., replay.CORE, (replay.CORE+replay.R)/2,
                                           replay.R, replay.R+1]))
        np.testing.assert_array_equal(values[[0, 1]], np.ones(2, np.float32))
        np.testing.assert_array_equal(values[[-2, -1]], np.zeros(2, np.float32))
        self.assertAlmostEqual(float(values[2]), .5, places=7)
        self.assertTrue(np.all((values >= 0) & (values <= 1)))

    def test_layout_offsets_are_bounded_and_sampled_spheres_disjoint(self):
        centres=[]
        for i in range(-2, 3):
            for j in range(-2, 3):
                for k in range(-2, 3):
                    centre=replay.bank_center((i,j,k)); nominal=replay.CELL*(np.array([i,j,k])+.5)
                    self.assertTrue(np.all(centre-nominal >= -replay.P/4))
                    self.assertTrue(np.all(centre-nominal < replay.P/4))
                    centres.append(centre)
        minimum=min(np.linalg.norm(a-b) for n,a in enumerate(centres) for b in centres[n+1:])
        self.assertGreater(minimum, 2*replay.R)

    def test_ray_sphere_clips_camera_depth_and_zero_length(self):
        origin=np.zeros(3); direction=np.array([[1.,0,0.],[0,1,0.]],np.float32)
        start,end,hit=replay.ray_sphere(origin,direction,np.array([30000.,0,0]),np.array([5000.,replay.fog.FAR]))
        self.assertFalse(hit[0])
        self.assertFalse(hit[1])
        start,end,hit=replay.ray_sphere(origin,direction[:1],np.array([10000.,0,0]),np.array([30000.]))
        self.assertTrue(hit[0]); self.assertEqual(float(start[0]),0.)  # camera lies inside R sphere
        self.assertEqual(float(end[0]),26384.)

    def test_true_pixel_centre_camera_rays(self):
        direction=replay.camera_rays(np.zeros(3),np.array([0.,0.,1.]),4,2)
        np.testing.assert_allclose(direction[0],[-.6396021,.2132007,.73854893],rtol=0,atol=1e-7)
        self.assertAlmostEqual(float(np.linalg.norm(direction[0])),1.,places=6)
        self.assertGreater(direction[0,1],0)

    def test_candidate_step_bound_and_ordered_composition(self):
        volume=self.volume(); centre=replay.bank_center((0,0,0)); origin=centre.copy()
        direction=np.array([[1.,0,0.],[0,0,1.]],np.float32); limit=np.full(2,replay.fog.FAR,np.float32)
        banks=replay.nearby_banks(origin)
        result=replay.integrate_banks(volume,origin,direction,limit,4e-6,500.,replay.SUN,banks,collect_cost=True)
        self.assertLessEqual(int(result["cost"]["sample_count"].max()),264)
        self.assertLessEqual(int(result["cost"]["sphere_hit_count"].max()),4)
        manual=replay.manual_composition(volume,origin,direction,limit,4e-6,500.,replay.SUN,banks)
        np.testing.assert_array_equal(result["S"],manual["S"])
        np.testing.assert_array_equal(result["T"],manual["T"])

    def test_empty_invalid_depth_and_source_alpha_identity(self):
        volume=self.volume(); centre=replay.bank_center((0,0,0)); direction=np.array([[1.,0,0.],[0,1,0.]],np.float32)
        result=replay.integrate_banks(volume,centre,direction,np.zeros(2,np.float32),4e-6,500.,replay.SUN)
        np.testing.assert_array_equal(result["S"],np.zeros((2,3),np.float32))
        np.testing.assert_array_equal(result["T"],np.ones(2,np.float32))
        source=np.array([[.1,.2,.3,0.],[.7,.8,.9,.73]],np.float32)
        composed=replay.composite_over_source(source,result)
        np.testing.assert_array_equal(composed[:,3],source[:,3])

    def test_reported_transport_laws_cover_nonempty_and_alpha(self):
        laws=replay.laws(self.volume(),4e-6)
        self.assertTrue(laws["nonempty_finite_bounded_transport"])
        self.assertTrue(laws["source_alpha_preserved_exact"])
        self.assertTrue(all(value for value in laws.values() if isinstance(value,bool)), laws)

    def test_bank0_support_is_identity_beyond_40km(self):
        volume=self.volume(); centre=replay.bank_center((0,0,0))
        origin=centre+np.array([0.,0.,200000+replay.R+1]); direction=np.array([[0.,0.,-1.]],np.float32)
        bank={"cell":(0,0,0),"center":centre,"distance":200000+replay.R+1}
        result=replay.integrate_banks(volume,origin,direction,np.array([replay.fog.FAR],np.float32),4e-6,500.,replay.SUN,[bank])
        np.testing.assert_array_equal(result["S"],np.zeros((1,3),np.float32))
        np.testing.assert_array_equal(result["T"],np.ones(1,np.float32))


if __name__ == "__main__":
    unittest.main()
