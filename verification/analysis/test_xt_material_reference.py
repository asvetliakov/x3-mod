"""Focused analytical witnesses for the independent XT material oracle."""

from dataclasses import replace
import math
import unittest

from verification.analysis import xt_material_reference as ref


class XTReferenceTests(unittest.TestCase):
    def assertVec(self, actual, expected, tolerance=1e-12):
        self.assertEqual(len(actual), len(expected))
        for got, wanted in zip(actual, expected):
            self.assertAlmostEqual(got, wanted, delta=tolerance)

    def lit(self, **changes):
        base = ref.Inputs(
            diffuse=(0.62, 0.31, 0.18, 0.7),
            specular=(0.4, 0.8, 0.9, 0.6),
            lightmap=(0.13, 0.22, 0.37, 0.8),
            occlusion=(0.12, 0.27, 0.63, 0.75),
            bump_sample=(0.55, 0.46, 0.3, 0.56),
            detail=(0.61, 0.38, 0.2, 0.72),
            lights=(
                ref.Light((0.2, 0.1, 0.9), (0.75, 0.42, 0.18)),
                ref.Light((-0.3, 0.4, 0.8), (0.23, 0.66, 0.51)),
            ),
            vertex_native_rgb=(0.2, 0.4, 0.6),
            vertex_linear_rgb=(0.2 ** 2.2, 0.4 ** 2.2, 0.6 ** 2.2),
            vertex_alpha=0.6,
            palette=ref.Palette(
                color1=(0.18, 0.75, 0.33), color2=(0.83, 0.24, 0.58),
                color3=(0.41, 0.62, 0.16), lines=(0.9, 0.12, 0.47),
                highlight=(0.27, 0.55, 0.88), weights=(0.2, 0.7, 1.3),
                highlight_weight=0.4, weighting=0.65, lines_power=2.3),
            palette_enabled=True, diffuse_strength=0.8, specular_strength=0.7,
            specular_power=6.0, reflection_strength=0.6, fresnel=0.35,
            occlusion_strength=1.4, glow=0.25, bump_strength=0.55,
            detail_strength=0.18, cube=lambda r: (0.35 + 0.1 * r[0],
                                                  0.55 + 0.1 * r[1],
                                                  0.75 + 0.1 * r[2]))
        return replace(base, **changes)

    def test_inventory_is_exactly_fourteen_contracts(self):
        self.assertEqual(len(ref.CONTRACTS), 14)
        self.assertEqual(sum(c.two_sided for c in ref.CONTRACTS.values()), 7)
        self.assertEqual({c.technique for c in ref.CONTRACTS.values()},
                         {"default", "bump", "low"})
        self.assertEqual(sum(c.family == "damage" for c in ref.CONTRACTS.values()), 2)
        self.assertEqual(sum(c.family == "terraformer" for c in ref.CONTRACTS.values()), 6)
        self.assertEqual(sum(c.family == "standard" for c in ref.CONTRACTS.values()), 6)

    def test_transfer_is_finite_bounded_and_roundtrips_ordinary_values(self):
        self.assertEqual(ref.decode(-1), 0.0)
        self.assertEqual(ref.encode(-1), 0.0)
        self.assertEqual(ref.decode(ref.CAP * 2), ref.decode(ref.CAP))
        self.assertEqual(ref.encode(ref.CAP * 2), ref.encode(ref.CAP))
        for value in (0.01, 0.18, 1.0, 4.0, 16.0):
            self.assertAlmostEqual(ref.decode(ref.encode(value)), value,
                                   delta=value * 1e-13)

    def test_every_contract_evaluates_native_and_linear_with_shared_alpha(self):
        for shader, contract in ref.CONTRACTS.items():
            inputs = self.lit(face=-1.0 if contract.two_sided else 1.0,
                              decal=contract.family != "terraformer")
            with self.subTest(shader=shader):
                native = ref.native_pixel(shader, inputs)
                linear = ref.linear_pixel(shader, inputs)
                self.assertTrue(all(math.isfinite(x) for x in native.output_rgba))
                self.assertTrue(all(math.isfinite(x) for x in linear.output_rgba))
                self.assertEqual(native.alpha, linear.alpha)
                self.assertEqual(native.output_rgba[3], linear.output_rgba[3])

    def test_standard_decal_runs_on_completed_encoded_affine_diffuse(self):
        affine = ((0.8, 0.1, 0.0, 0.04), (0.0, 0.7, 0.2, 0.03),
                  (0.1, 0.0, 0.9, 0.02))
        inputs = self.lit(affine=affine, decal=True, palette_enabled=False,
                          vertex_native_rgb=(0, 0, 0), vertex_linear_rgb=(0, 0, 0),
                          lights=(ref.Light((0, 0, 1), (0, 0, 0)),) * 2,
                          lightmap=(0, 0, 0, 0), cube=(0, 0, 0))
        completed = ref.affine_diffuse(inputs.diffuse, affine)
        encoded_then_decode = ref.decode_rgb(
            ref.standard_decal(completed, inputs.occlusion[:3]))
        decoded_first = ref.standard_decal(ref.decode_rgb(completed), inputs.occlusion[:3])
        self.assertVec(ref.linear_pixel("fffdabd910793aba", inputs).base_working,
                       encoded_then_decode)
        self.assertGreater(max(abs(a - b) for a, b in zip(encoded_then_decode, decoded_first)), .02)

    def test_false_standard_decal_branch_ignores_occlusion_rgb(self):
        a = self.lit(decal=False, occlusion=(0.02, 0.3, 0.9, 0.7))
        b = replace(a, occlusion=(0.95, 0.01, 0.2, 0.7))
        for shader in ("fffdabd910793aba", "5f82ecacd39529cd", "6733b119142c8d42"):
            with self.subTest(shader=shader):
                self.assertEqual(ref.linear_pixel(shader, a).base_encoded,
                                 ref.linear_pixel(shader, b).base_encoded)

    def test_damage_raw_red_attenuates_detail_before_normalization(self):
        common = self.lit(decal=False, bump_sample=(0.5, 0.5, 0.5, 0.5),
                          detail=(1.0, 0.5, 0.0, 0.0), detail_strength=0.3,
                          occlusion=(0.1, 0.2, 1.0, 1.0), palette_enabled=False)
        live = ref.linear_pixel("d51cf763125cb85a", common).normal
        killed = ref.linear_pixel("d51cf763125cb85a",
                                  replace(common, occlusion=(0.6, 0.2, 1.0, 1.0))).normal
        self.assertGreater(live[0], 0.2)
        self.assertAlmostEqual(killed[0], 0.0)
        self.assertEqual(ref.decode(0.1), 0.1 ** 2.2)
        wrong_damage = max(1.0 - 2.0 * ref.decode(0.1), 0.0)
        self.assertNotAlmostEqual(wrong_damage, 0.8)

    def test_damage_raw_blue_scales_normal_without_renormalizing(self):
        inputs = self.lit(decal=False, occlusion=(0.2, 0.4, 0.37, 1.0),
                          detail_strength=0.0)
        result = ref.linear_pixel("d51cf763125cb85a", inputs)
        self.assertAlmostEqual(math.sqrt(sum(x * x for x in result.normal)), 0.37)
        self.assertAlmostEqual(math.sqrt(sum(x * x for x in result.lighting_normal)), 0.37)
        self.assertNotAlmostEqual(0.37, ref.decode(0.37))

    def test_occlusion_alpha_is_raw_scalar_in_both_paths(self):
        inputs = self.lit(occlusion=(0.1, 0.2, 0.3, 0.5),
                          lightmap=(0, 0, 0, 0), palette_enabled=False)
        full = replace(inputs, occlusion=(0.1, 0.2, 0.3, 1.0))
        for function in (ref.native_pixel, ref.linear_pixel):
            dim = function("5f82ecacd39529cd", inputs).working_rgb
            bright = function("5f82ecacd39529cd", full).working_rgb
            for got, wanted in zip(dim, bright):
                self.assertAlmostEqual(got / wanted, 0.5 ** inputs.occlusion_strength)
                self.assertNotAlmostEqual(got / wanted,
                                          ref.decode(0.5) ** inputs.occlusion_strength)

    def test_damage_decal_uses_raw_occlusion_rgb_then_decodes_base_once(self):
        inputs = self.lit(decal=True, occlusion=(0.2, 0.5, 0.8, 1.0))
        result = ref.linear_pixel("d51cf763125cb85a", inputs)
        diffuse = ref.affine_diffuse(inputs.diffuse, inputs.affine)
        expected_encoded = tuple(a * b for a, b in zip(diffuse, inputs.occlusion[:3]))
        self.assertVec(result.base_encoded, expected_encoded)
        self.assertVec(result.base_working, ref.decode_rgb(expected_encoded))
        self.assertNotEqual(result.base_working,
                            tuple(a * b for a, b in zip(ref.decode_rgb(diffuse),
                                                       ref.decode_rgb(inputs.occlusion[:3]))))

    def test_terraformer_additive_occlusion_is_independent_and_unoccluded(self):
        dark = ref.Inputs(diffuse=(0, 0, 0, 1), occlusion=(0.5, 0.25, 0.75, 0.0),
                          occlusion_strength=3.0)
        native = ref.native_pixel("fd58e6b7e8cf969c", dark)
        linear = ref.linear_pixel("fd58e6b7e8cf969c", dark)
        self.assertVec(native.output_rgba[:3], dark.occlusion[:3])
        self.assertVec(linear.working_rgb, ref.decode_rgb(dark.occlusion[:3]))
        self.assertVec(linear.output_rgba[:3], dark.occlusion[:3])

    def test_palette_decodes_each_runtime_rgb_before_unequal_weighting(self):
        inputs = self.lit(vertex_native_rgb=(1, 1, 1), vertex_linear_rgb=(1, 1, 1),
                          lights=(ref.Light((0, 0, 1), (0, 0, 0)),) * 2,
                          lightmap=(0, 0, 0, 0), specular=(0, 0, 0, 0),
                          cube=(0, 0, 0), occlusion=(0.1, 0.2, 0.3, 1))
        palette = inputs.palette
        sources = (palette.color1, palette.color2, palette.color3,
                   palette.highlight, palette.lines)
        view = ref._unit(inputs.view, "view")
        for shader in ref.CONTRACTS:
            candidate = replace(inputs, decal=ref.CONTRACTS[shader].family != "terraformer")
            result = ref.linear_pixel(shader, candidate)
            weights = (*palette.weights, palette.highlight_weight,
                       (1 - abs(ref._dot(view, result.reflection))) ** palette.lines_power)
            weighted_encoded = tuple(sum(color[channel] * weight
                                         for color, weight in zip(sources, weights))
                                     for channel in range(3))
            correct = tuple(sum(ref.decode(color[channel]) * weight
                                for color, weight in zip(sources, weights))
                            for channel in range(3))
            self.assertVec(result.palette_rgb, correct)
            self.assertGreater(max(abs(a - b) for a, b in
                                   zip(correct, ref.decode_rgb(weighted_encoded))), .05)

    def test_directional_colors_decode_independently_before_addition(self):
        inputs = ref.Inputs(diffuse=(1, 1, 1, 1),
                            lights=(ref.Light((0, 0, 1), (.5, .25, 0)),
                                    ref.Light((0, 0, 1), (.5, 0, .75))))
        result = ref.linear_pixel("fffdabd910793aba", inputs)
        self.assertVec(result.working_rgb,
                       (2 * .5 ** 2.2, .25 ** 2.2, .75 ** 2.2))
        self.assertNotAlmostEqual(result.working_rgb[0], ref.decode(.5 + .5))

    def test_cube_and_lightmap_are_separate_decoded_sources(self):
        inputs = ref.Inputs(diffuse=(.5, .25, .75, 1), specular=(.8, 0, 0, 0),
                            cube=(.4, .6, .2), lightmap=(.3, .7, .5, 0),
                            fresnel=.65, reflection_strength=.45,
                            occlusion=(0, 0, 0, .2), occlusion_strength=2)
        result = ref.linear_pixel("fffdabd910793aba", inputs)
        base = ref.decode_rgb(inputs.diffuse[:3])
        reflected = tuple(base[i] * ref.decode(inputs.cube[i]) * .65 * .45 * .8 * .04
                          for i in range(3))
        expected = tuple(reflected[i] + ref.decode(inputs.lightmap[i])
                         for i in range(3))
        self.assertVec(result.working_rgb, expected)
        wrong_lightmap = tuple((base[i] * ref.decode(inputs.cube[i]) * .65 * .45 * .8
                                + ref.decode(inputs.lightmap[i])) * .04
                               for i in range(3))
        self.assertNotEqual(result.working_rgb, wrong_lightmap)

    def test_full_bump_uses_alpha_green_while_low_uses_rgb(self):
        inputs = ref.Inputs(bump_sample=(.9, .55, .8, .6), bump_strength=.5,
                            occlusion=(0, 0, 1, 1))
        full = ref.native_pixel("5f82ecacd39529cd", inputs).normal
        full_red_changed = ref.native_pixel("5f82ecacd39529cd",
                                            replace(inputs, bump_sample=(.1, .55, .8, .6))).normal
        low = ref.native_pixel("6733b119142c8d42", inputs).normal
        low_alpha_changed = ref.native_pixel("6733b119142c8d42",
                                             replace(inputs, bump_sample=(.9, .55, .8, .1))).normal
        self.assertEqual(full, full_red_changed)
        self.assertEqual(low, low_alpha_changed)
        self.assertNotEqual(full, low)

    def test_palette_never_tints_cube_reflection(self):
        inputs = ref.Inputs(diffuse=(.6, .4, .2, 1), specular=(1, 0, 0, 0),
                            cube=(.7, .5, .3), fresnel=.8, reflection_strength=.9,
                            palette=ref.Palette(weights=(2, 3, 4),
                                                highlight_weight=5, weighting=.75))
        plain = ref.linear_pixel("5f82ecacd39529cd", inputs)
        tinted = ref.linear_pixel("5f82ecacd39529cd",
                                  replace(inputs, palette_enabled=True))
        self.assertEqual(plain.working_rgb, tinted.working_rgb)

    def test_bump_direct_mask_uses_detail_alpha_but_reflection_uses_specular_red(self):
        base = ref.Inputs(diffuse=(1, 1, 1, 1), specular=(0, 0, 0, 0),
                          detail=(0.5, 0.5, 0, 1),
                          lights=(ref.Light((0, 0, 1), (1, 1, 1)),) * 2,
                          cube=(1, 1, 1), fresnel=1)
        bump = ref.native_pixel("5f82ecacd39529cd", base)
        default = ref.native_pixel("fffdabd910793aba", base)
        self.assertGreater(bump.working_rgb[0], default.working_rgb[0])
        no_direct = replace(base, lights=(ref.Light((0, 0, 1), (0, 0, 0)),) * 2)
        self.assertEqual(ref.native_pixel("5f82ecacd39529cd", no_direct).working_rgb,
                         (0.0, 0.0, 0.0))

    def test_two_sided_face_flips_lighting_only_for_all_seven_pairs(self):
        cube = lambda r: (0.5 + 0.2 * r[0], 0.5 + 0.2 * r[1], 0.5 + 0.2 * r[2])
        inputs = ref.Inputs(diffuse=(1, 1, 1, 1), specular=(1, 0, 0, 0),
                            lights=(ref.Light((0, 0, 1), (1, 1, 1)),) * 2,
                            cube=cube, fresnel=1, reflection_strength=.7,
                            bump_sample=(.5, .5, .75, .5), occlusion=(0, 0, 1, 1))
        for shader, contract in ref.CONTRACTS.items():
            if not contract.two_sided:
                continue
            with self.subTest(shader=shader):
                front = ref.native_pixel(shader, replace(inputs, face=1))
                back = ref.native_pixel(shader, replace(inputs, face=-1))
                self.assertEqual(front.reflection, back.reflection)
                self.assertEqual(front.normal, back.normal)
                self.assertEqual(back.lighting_normal,
                                 tuple(-x for x in front.lighting_normal))
                self.assertNotEqual(front.working_rgb, back.working_rgb)

    def test_default_authored_policy_has_fixed_weights_and_fresnel(self):
        front = ref.default_authored_varyings((0, 0, 4), (0, 0, 7), 0.6)
        grazing = ref.default_authored_varyings((4, 0, 0), (0, 0, 7), 0.6)
        self.assertEqual(front.palette_weights, (0.0, 0.0, 1.0))
        self.assertEqual(front.highlight_weight, 1.0)
        self.assertEqual(front.fresnel_shape, 0.0)
        self.assertEqual(front.fresnel, 0.1)
        self.assertEqual(grazing.highlight_weight, 0.0)
        self.assertEqual(grazing.fresnel_shape, 1.0)
        self.assertAlmostEqual(grazing.fresnel, 0.64)
        self.assertEqual((ref.DEFAULT_HIGHLIGHT_POWER, ref.DEFAULT_FRESNEL_POWER,
                          ref.DEFAULT_MIN_FRESNEL), (12, 2, 0.1))

    def test_default_authored_reflection_strength_remains_double(self):
        dark = ref.Inputs(diffuse=(0.5, 0.5, 0.5, 1), specular=(1, 0, 0, 0),
                          cube=(1, 1, 1), occlusion=(0, 0, 0, 1))
        outputs = []
        for strength in (0.25, 0.5):
            authored = ref.default_authored_varyings((1, 0, 0), (0, 0, 1), strength)
            value = ref.native_pixel("fffdabd910793aba",
                                     replace(dark, fresnel=authored.fresnel,
                                             reflection_strength=strength)).working_rgb[0]
            # Subtract the fixed 0.1 Fresnel lobe, leaving the strength-squared term.
            variable = value - 0.5 * 0.1 * strength
            outputs.append(variable)
        self.assertAlmostEqual(outputs[1] / outputs[0], 4.0)

    def test_default_secondary_uv_uses_documented_api_expansion(self):
        self.assertEqual(ref.default_secondary_uv((0.2, 0.7)), (0.0, 1.0))
        self.assertEqual(ref.default_secondary_uv((0.2, 0.7, 0.3)), (0.3, 1.0))
        self.assertEqual(ref.default_secondary_uv((0.2, 0.7, 0.3, 0.8)), (0.3, 0.8))
        self.assertNotEqual(ref.default_secondary_uv((0.2, 0.7)), (0.2, 0.7))

    def test_alpha_is_numeric_glow_lerp_and_ignores_every_other_lane(self):
        inputs = self.lit(glow=1.4, vertex_alpha=0.6)
        expected = (1.4 * inputs.lightmap[3] + (1 - 1.4) * inputs.diffuse[3]) * 0.6
        variants = (
            inputs,
            replace(inputs, occlusion=(0.99, 0.01, 0.02, 0.17)),
            replace(inputs, specular=(0.9, 0.1, 0.2, 0.3), detail=(.1, .9, .8, .2)),
            replace(inputs, bump_sample=(.4, .6, .8, .3), cube=(.9, .1, .7)),
            replace(inputs, palette=replace(inputs.palette, weights=(7, 2, 9),
                                             highlight_weight=4)),
        )
        for shader, contract in ref.CONTRACTS.items():
            for variant in variants:
                candidate = replace(variant, decal=contract.family != "terraformer")
                self.assertAlmostEqual(ref.native_pixel(shader, candidate).alpha, expected)
                self.assertAlmostEqual(ref.linear_pixel(shader, candidate).alpha, expected)


if __name__ == "__main__":
    unittest.main()

class XtSelectiveExposureReferenceTests(unittest.TestCase):
    def test_independent_components_damage_detail_palette_occlusion_and_terra(self):
        from material_exposure_reference import evaluate
        count=0
        for shader,contract in ref.CONTRACTS.items():
            for gain in (0.,1.,4.,16.):
                for branch in (False,True):
                    for face in (-1.,1.):
                        for fill in (0.,.03):
                            point=(gain*.03,gain*.07,gain*.02);emission=(gain*.04,gain*.02,gain*.06)
                            inputs=ref.Inputs(diffuse=(.4,.5,.6,.7),specular=(.3,.1,.2,.8),lightmap=(.1,.3,.2,.7),
                                occlusion=(.2,.1,.4,.6),detail=(.4,.6,.3,.8),bump_sample=(.5,.5,.5,.5),
                                lights=(ref.Light((0.,0.,1.),(.3,.5,.7)),ref.Light((.2,.1,.9),(.5,.4,.2))),
                                cube=(.2,.3,.5),fresnel=.4,reflection_strength=.7,specular_strength=1.3,
                                diffuse_strength=.8,occlusion_strength=1.2,palette_enabled=branch,
                                decal=branch and contract.family!='terraformer',face=face,
                                palette=ref.Palette(weighting=.6,weights=(.2,.3,.1),highlight_weight=.2),
                                detail_strength=.1,direct_gain=gain,lightmap_gain=gain,fill=fill)
                            B=ref.linear_pixel(shader,replace(inputs,vertex_linear_rgb=point,specular_strength=0.,
                                reflection_strength=0.,lightmap=(0.,0.,0.,.7),
                                occlusion=(0.,0.,0.,.6) if contract.family=='terraformer' else inputs.occlusion))
                            H=ref.linear_pixel(shader,replace(inputs,vertex_linear_rgb=emission,diffuse_strength=0.,fill=0.))
                            for e in (.125,.5,1.,2.):
                                actual=ref.linear_pixel(shader,replace(inputs,diffuse_strength=inputs.diffuse_strength/e,fill=fill/e,
                                    vertex_linear_rgb=tuple(p/e+m for p,m in zip(point,emission))))
                                expected=evaluate(B.working_rgb,H.working_rgb,e)
                                self.assertTrue(expected.exact_domain)
                                for q,want in zip(actual.working_rgb,expected.q):self.assertAlmostEqual(q,want,delta=1e-10)
                                self.assertEqual(actual.alpha,H.alpha);count+=1
        print('Independent XT source component cases:',count)
