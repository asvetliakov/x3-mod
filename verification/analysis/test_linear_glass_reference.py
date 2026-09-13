"""Independent glass equations and fixture coverage, without original shader bytes."""
from dataclasses import replace
import math
from pathlib import Path
import tempfile
from unittest.mock import patch
import unittest
import linear_material_reference as ref
import linear_glass_fixture_reference as fixture
import run_linear_material as runner

class GlassReferenceTests(unittest.TestCase):
    def vertex(self,normal=(0,0,1),camera=(.6,0,.8),**kwargs):
        return ref.glass_vertex((0,0,0),normal,camera,(0,0,0),**kwargs)

    def pixel(self,v=None,ps='a66fb1981ba755b2',diffuse=(.5,.25,.75,.75),mask=1.,cube=(0,0,0),colors=((1,1,1),(0,0,0)),**kwargs):
        v=v or self.vertex()
        directions=[ref.DirectionalLight(direction,color) for direction,color in zip(((0,0,1),(0,0,-1)),colors)]
        return ref.glass_pixel(ps,v,diffuse,mask,lambda _:cube,directions[:ref.GLASS_PROFILES[ps].directions],half_target=False,**kwargs)

    def test_native_gloss_power_six_is_diffuse_tinted(self):
        result=self.pixel(linear=False)
        lobe=.5+3*.8**6
        self.assertEqual(result.encoded_rgba[3],.75)
        for actual,diffuse in zip(result.linear_rgb,(.5,.25,.75)):
            self.assertAlmostEqual(actual,diffuse*lobe)
        self.assertNotAlmostEqual(result.linear_rgb[0],.5*(.5+3*.8**5))
        black=self.pixel(diffuse=(0,0,0,.5),linear=False)
        self.assertEqual(black.linear_rgb,(0,0,0))

    def test_cube_independent_of_diffuse_with_native_half_toggle_full_scale(self):
        v=self.vertex(camera=(1,0,0))
        for ps,k in (('a66fb1981ba755b2',.5),('f31c9e2701c8eee4',1.)):
            for linear in (False,True):
                result=self.pixel(v,ps,diffuse=(0,0,0,.75),cube=(.25,.5,.75),linear=linear)
                for actual,color in zip(result.linear_rgb,(.25,.5,.75)):
                    self.assertAlmostEqual(actual,k*(ref.decode(color) if linear else color))
                self.assertEqual(self.pixel(v,ps,mask=0.,diffuse=(0,0,0,.75),cube=(1,1,1),linear=linear).linear_rgb,(0,0,0))

    def test_fresnel_is_vertex_scalar_and_preserves_log_absolute_input(self):
        self.assertEqual(self.vertex(camera=(0,0,1)).fresnel,0.)
        self.assertEqual(self.vertex(camera=(1,0,0)).fresnel,1.)
        v=self.vertex(normal=(0,0,2.5),camera=(0,0,1))
        self.assertAlmostEqual(v.fresnel,1.5**ref.GLASS_FRESNEL_POWER)
        self.assertEqual(v.reflection,(0.,0.,11.5))
        a=self.vertex(camera=(0,0,1));b=self.vertex(camera=(1,0,0))
        midpoint=replace(a,view=(.5,0,.5),fresnel=.5)
        result=self.pixel(midpoint,diffuse=(0,0,0,1),cube=(1,1,1),linear=False)
        self.assertEqual(result.linear_rgb,(.25,.25,.25))
        recomputed=(1-1/math.sqrt(2))**ref.GLASS_FRESNEL_POWER
        self.assertGreater(abs(.5-recomputed),.2)

    def test_two_sided_only_changes_pixel_lighting(self):
        v=self.vertex()
        front=self.pixel(v,'ebc9b2b3f1564e9a',linear=False)
        back=self.pixel(v,'ebc9b2b3f1564e9a',face=-1,linear=False)
        self.assertGreater(front.linear_rgb[0],0.)
        self.assertEqual(back.linear_rgb,(0,0,0))
        for face in (-1,1):
            result=self.pixel(v,'ebc9b2b3f1564e9a',face=face,diffuse=(0,0,0,.75),cube=(1,1,1),linear=False)
            self.assertAlmostEqual(result.linear_rgb[0],.5*v.fresnel)

    def test_native_clamp_linear_unbounded_and_alpha_fog(self):
        v=replace(self.vertex(),linear_rgb=(3,2,1),alpha=.15625)
        colors=((0,0,0),(0,0,0))
        native=self.pixel(v,diffuse=(1,1,1,.75),colors=colors,linear=False)
        linear=self.pixel(v,diffuse=(1,1,1,.75),colors=colors,linear=True)
        self.assertEqual(native.linear_rgb,(1,1,1))
        self.assertEqual(linear.linear_rgb,(3,2,1))
        self.assertEqual(native.encoded_rgba[3],linear.encoded_rgba[3])
        self.assertEqual(self.vertex(camera=(0,0,4),material_alpha=.625,fog_clip=(.75,.125)).alpha,.15625)

    def test_scoped_original_dependencies_are_exactly_seven(self):
        cases=[c for c in runner.fixture_cases() if c['pair']>=fixture.START]
        self.assertEqual(runner.selected_originals(cases),set(fixture.WORDS))
        self.assertEqual(len(runner.selected_originals(cases)),7)

    @unittest.skipUnless(Path('/tmp/x3-shader-sweep/programs/vs_c30104cb0efb6675.bin').exists(),
                         'local original shader corpus absent')
    def test_scoped_provenance_with_only_seven_local_original_files(self):
        cases=[c for c in runner.fixture_cases() if c['pair']>=fixture.START]
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for stage,identity in fixture.WORDS:
                name=f'{stage}_{identity}.bin'
                (root/name).write_bytes((Path('/tmp/x3-shader-sweep/programs')/name).read_bytes())
            # No prior/XT proof files or unrelated originals exist in this root.
            with patch.object(runner,'ROOT',root):
                result=runner.original_provenance(cases,root)
                self.assertEqual(set(result),{f'{s}_{i}.bin' for s,i in fixture.WORDS})
                self.assertEqual(len(result),7)
                for name,digest in result.items():self.assertEqual(digest,runner.sha(root/name))
                witness=root/'ps_a66fb1981ba755b2.bin'
                data=bytearray(witness.read_bytes());data[-1]^=1;witness.write_bytes(data)
                with self.assertRaisesRegex(AssertionError,'identity/size'):
                    runner.original_provenance(cases,root)
                witness.unlink()
                with self.assertRaises(FileNotFoundError):runner.original_provenance(cases,root)

    def test_pairs_case_abi_interpolation_and_required_witnesses(self):
        cases=[c for c in runner.fixture_cases() if c['pair']>=fixture.START]
        self.assertEqual(len(cases),254)
        self.assertEqual(runner.PAIRS[162:],fixture.PAIRS)
        self.assertEqual(set(fixture.WORDS),{(stage,key) for v,p in fixture.PAIRS for stage,key in [('vs',v),('ps',p)]})
        self.assertEqual({(c['pair'],c['depth'],c['reverse']) for c in cases if c['label']=='glass_pair_depth_face'},
                         {(p,d,r) for p in range(162,168) for d in (0,1) for r in (0,1)})
        for label in ('over_one_point','tinted_gloss','untinted_cube','cube_only_mask','nonunit_normal','fog_alpha'):
            self.assertEqual({c['pair'] for c in cases if c['label']=='glass_'+label},set(range(162,168)))
        c=next(c for c in cases if c['flags']==16|2048)
        smooth=fixture.varying(c);flat=fixture.varying(c,flat_color=True)
        self.assertNotEqual(smooth.fresnel,flat.fresnel)
        self.assertNotEqual(smooth.linear_rgb,flat.linear_rgb)
        self.assertEqual(smooth.reflection,flat.reflection)
        for c in cases:
            for source_half in (False,True):
                for native in (False,True):
                    result=fixture.expected(c,source_half,linear=not native,cube_sampler=runner.cube_sample)
                    self.assertTrue(all(map(math.isfinite,result.encoded_rgba)))
                    self.assertEqual(result.encoded_rgba[3],runner.expected_alpha(c))

if __name__=='__main__': unittest.main()
