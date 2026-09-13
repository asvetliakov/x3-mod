"""Analytical checks for independently authored DEFAULT and BUMPMAP color equations."""
import math
import json
from pathlib import Path
import struct
import unittest

from linear_material_reference import (
    CAP, DIFFUSE_COEFFICIENT, PROFILES, DirectionalLight, Gains, PointLight, decode, encode, half,
    bump_geometry, pixel, sanitize, vertex,
)


class TransferTests(unittest.TestCase):
    def test_original_float32_diffuse_coefficient(self):
        self.assertEqual(DIFFUSE_COEFFICIENT, struct.unpack('<f', bytes.fromhex('cdcccc3e'))[0])
        self.assertNotEqual(DIFFUSE_COEFFICIENT, 0.4)

    def test_black_extremes_and_postdecode_range(self):
        for value in (0.0, -0.0, -1.0, -math.inf, math.nan):
            self.assertEqual(sanitize(value), 0.0)
            self.assertEqual(decode(value), 0.0)
            self.assertEqual(encode(value), 0.0)
            self.assertEqual(math.copysign(1.0, encode(value)), 1.0)
            self.assertEqual(math.copysign(1.0, decode(value)), 1.0)
        self.assertEqual(sanitize(math.inf), CAP)
        self.assertEqual(sanitize(CAP * 2), CAP)
        self.assertGreater(decode(CAP), CAP)  # No premature source-radiance cap.
        self.assertLessEqual(encode(math.inf), 154.61)
        self.assertEqual(encode(math.inf), encode(CAP))

    def test_transfer_roundtrip_and_safe_positive_floors(self):
        for value in (0.001, 0.18, 1.0, 4.0, 16.0, 100.0):
            self.assertAlmostEqual(decode(encode(value)), value, delta=value * 1e-14)
        self.assertEqual(decode(1e-30), decode(1e-10))
        self.assertEqual(encode(1e-30), encode(1e-22))
        self.assertGreater(encode(1e-30), 0.0)
        self.assertEqual(half(encode(0.0)), 0.0)
        self.assertTrue(math.isinf(half(CAP * 2)))

    def test_gains_are_bounded_configuration_not_colors(self):
        self.assertEqual(Gains(0, 16, 1).material_emissive, 16.0)
        for value in (-0.1, 16.01, math.inf, math.nan):
            with self.assertRaises(ValueError):
                Gains(direct=value)
        for value in (True, "1", None):
            with self.assertRaises(TypeError):
                Gains(material_emissive=value)
        with self.assertRaises(TypeError):
            decode("0.5")


class MaterialTests(unittest.TestCase):
    def test_all_profile_contracts_match_independent_derived_evidence(self):
        path = Path(__file__).resolve().parents[2] / 'docs/reverse-engineering/linear-material-profiles.json'
        evidence = json.loads(path.read_text())
        expected = {}
        for row in evidence['programs']:
            if row['id'].startswith('ps_'):
                expected[row['fnv1a64']] = (
                    len({source['name'] for source in row['directional_rgb_sources']}),
                    row['diffuse_affine_completion'] is not None,
                    row['two_sided'],
                    row['lobe_coefficients']['diffuse'], row['lobe_coefficients']['specular_power'],
                    row['lobe_coefficients']['cube'],
                    'argon_bump' in row['families'],
                )
        self.assertEqual(len(expected), 18)
        self.assertEqual({key: (value.directions, value.affine_color, value.two_sided,
                               value.diffuse_coefficient, value.specular_power, value.cube_coefficient,
                               value.bump_map)
                          for key, value in PROFILES.items()}, expected)

    def assertRGB(self, actual, expected, tolerance=1e-12):
        for a, b in zip(actual, expected):
            self.assertAlmostEqual(a, b, delta=tolerance)

    @staticmethod
    def make_vertex(emissive=(0, 0, 0), lights=(), **kwargs):
        return vertex((0, 0, 0), (0, 0, 1), (0, 0, 2), emissive, lights, **kwargs)

    @staticmethod
    def dark_directions(profile):
        return [DirectionalLight((0, 0, 1), (0, 0, 0))
                for _ in range(PROFILES[profile].directions)]

    def sample(self, varying, profile="8759c7838bbc86c2", **kwargs):
        options = dict(diffuse=(1, 1, 1, 0.25), specular_mask=0,
                       lightmap=(0, 0, 0, 0.75), cubemap=(0, 0, 0),
                       directions=self.dark_directions(profile))
        options.update(kwargs)
        if PROFILES[profile].bump_map:
            options.setdefault('normal_sample', (0, 0.5, 0, 0.5))
            options.setdefault('tangent', (1, 0, 0))
            options.setdefault('binormal', (0, 1, 0))
            if not callable(options['cubemap']):
                sample = options['cubemap']
                options['cubemap'] = lambda direction: sample
        return pixel(profile, varying, **options)

    def test_all_eighteen_contracts_retain_black_and_alpha(self):
        for profile in PROFILES:
            with self.subTest(profile=profile):
                result = self.sample(self.make_vertex(material_alpha=0.5), profile,
                                     glow=0.25)
                self.assertEqual(result.linear_rgb, (0, 0, 0))
                self.assertEqual(result.encoded_rgba, (0, 0, 0, 0.1875))

    def test_colored_point_inputs_decode_before_addition(self):
        lights = [PointLight((0, 0, 1), (0.5, 0, 0), (1, 0, 0)),
                  PointLight((0, 0, 1), (0, 0.25, 0), (1, 0, 0))]
        output = self.make_vertex((0, 0, 2), lights)
        self.assertRGB(output.linear_rgb, (0.5 ** 2.2, 0.25 ** 2.2, 2))
        twice = self.make_vertex(lights=[lights[0], lights[0]])
        self.assertAlmostEqual(twice.linear_rgb[0], 2 * 0.5 ** 2.2)
        self.assertNotAlmostEqual(twice.linear_rgb[0], (0.5 + 0.5) ** 2.2)

    def test_zero_one_eight_and_fixed_single_light(self):
        light = PointLight((0, 0, 2), (1, 0.5, 0), (1, 0.5, 0.5))
        # Denominator is 1 + 0.5*2 + 0.5*4 = 4.
        for count in (0, 1, 8):
            actual = self.make_vertex(lights=[light] * count)
            self.assertRGB(actual.linear_rgb, (count / 4, count * 0.5 ** 2.2 / 4, 0))
        fixed = self.make_vertex(lights=[light], fixed_single=True)
        self.assertEqual(fixed, self.make_vertex(lights=[light]))
        for lights, fixed_single in (([], True), ([light] * 2, True), ([light] * 9, False)):
            with self.assertRaises(ValueError):
                self.make_vertex(lights=lights, fixed_single=fixed_single)

    def test_point_normal_is_not_normalized_and_reciprocal_is_saturated(self):
        light = PointLight((0, 0, 2), (1, 1, 1), (0.5, 0, 0))
        result = vertex((0, 0, 0), (0, 0, 0.25), (0, 0, 2), (0, 0, 0), [light])
        self.assertEqual(result.linear_rgb, (0.25, 0.25, 0.25))
        negative = PointLight((0, 0, 2), (1, 1, 1), (-1, 0, 0))
        self.assertEqual(self.make_vertex(lights=[negative]).linear_rgb, (0, 0, 0))

    def test_native_emissive_and_renderer_gain_are_linear(self):
        base = self.sample(self.make_vertex((0.125, 0.5, 2))).linear_rgb
        for scale in (1, 4, 16):
            gains = Gains(material_emissive=scale)
            actual = self.sample(self.make_vertex((0.125, 0.5, 2), gains=gains),
                                 gains=gains).linear_rgb
            self.assertRGB(actual, tuple(value * scale for value in base))
        doubled_native_strength = self.make_vertex((0.25, 1, 4))
        self.assertRGB(doubled_native_strength.linear_rgb, tuple(value * 2 for value in base))

    def test_each_shared_profile_has_independent_half_diffuse_sixth_power_and_half_cube(self):
        # Expectations here are analytical constants, independent of the
        # profile JSON and oracle coefficient fields under test.
        contracts = {
            '3b94320087e81945': (2, True, False), 'e3b7acc16da9932d': (2, True, True),
            '7a14d4dcb28f27e5': (1, True, False), '8ab6188a40ca15ea': (1, True, True),
            '8df6143d0e77d92e': (1, False, False), 'e16a9806ee3544c3': (1, False, True),
        }
        colors = (0.5, 0.25, 1.0)
        angled = vertex((0, 0, 0), (0, 0, 1), (math.sqrt(3), 0, 1), (0, 0, 0))
        for profile, (count, affine, two_sided) in contracts.items():
            with self.subTest(profile=profile):
                lights = [DirectionalLight((0, 0, 1), colors)] + [DirectionalLight((0, 0, 1), (0, 0, 0))] * (count - 1)
                diffuse = self.sample(self.make_vertex(), profile, directions=lights)
                self.assertRGB(diffuse.linear_rgb, tuple(0.5 * color ** 2.2 for color in colors))
                specular = self.sample(angled, profile, directions=lights, specular_mask=1)
                self.assertRGB(specular.linear_rgb, tuple((0.5 + 3 * 0.5 ** 6) * color ** 2.2 for color in colors))
                self.assertNotAlmostEqual(specular.linear_rgb[2], 0.5 + 3 * 0.5 ** 5)
                cube = self.sample(self.make_vertex(), profile, diffuse=(0.25, 0.5, 0.75, 0.25),
                                   specular_mask=0.5, cubemap=(0.5, 0.25, 1.0))
                self.assertRGB(cube.linear_rgb, tuple(0.25 * a ** 2.2 * c ** 2.2
                                                     for a, c in zip((0.25, 0.5, 0.75), (0.5, 0.25, 1))))
                shifted = self.sample(self.make_vertex((1, 1, 1)), profile, diffuse=(0.2, 0.4, 0.6, 0.25),
                                      affine=((0, 1, 0, 0), (1, 0, 0, 0), (0, 0, 0, 0.5)))
                expected = (0.4, 0.2, 0.5) if affine else (0.2, 0.4, 0.6)
                self.assertRGB(shifted.linear_rgb, tuple(x ** 2.2 for x in expected))
                back = vertex((0, 0, 0), (0, 0, 1), (0, 0, -1), (0, 0, 0))
                back_lights = [DirectionalLight((0, 0, -1), colors)] + [DirectionalLight((0, 0, -1), (0, 0, 0))] * (count - 1)
                faced = self.sample(back, profile, face=-1, directions=back_lights, specular_mask=1)
                self.assertRGB(faced.linear_rgb, tuple(3.5 * color ** 2.2 for color in colors) if two_sided else (0, 0, 0))

    def test_directional_lobe_has_fifth_power_and_independent_colors(self):
        varying = vertex((0, 0, 0), (0, 0, 1), (math.sqrt(3), 0, 1), (0, 0, 0))
        lights = [DirectionalLight((0, 0, 1), (0.5, 0, 0)),
                  DirectionalLight((0, 0, 1), (0, 0.25, 0))]
        result = self.sample(varying, specular_mask=1, directions=lights)
        original_diffuse = struct.unpack('<f', bytes.fromhex('cdcccc3e'))[0]
        lobe = original_diffuse + 3 * 0.5 ** 5
        self.assertRGB(result.linear_rgb, (lobe * 0.5 ** 2.2, lobe * 0.25 ** 2.2, 0))
        # Direction is retained as supplied, not normalized by the PS.
        low = self.sample(self.make_vertex(), "593e5dea9b3457d5", specular_mask=1,
                          directions=[DirectionalLight((0, 0, 0.1), (1, 1, 1))])
        self.assertRGB(low.linear_rgb, (original_diffuse * 0.1 + 3 * 0.3 * 0.1 ** 5,) * 3)

    def test_two_sided_normal_sign_is_profile_specific(self):
        varying = vertex((0, 0, 0), (0, 0, 1), (0, 0, -1), (0, 0, 0))
        lights = [DirectionalLight((0, 0, -1), (1, 1, 1))]
        front_only = self.sample(varying, "593e5dea9b3457d5", face=-1,
                                 directions=lights, specular_mask=1)
        two_sided = self.sample(varying, "dab93928f26906f7", face=-1,
                                directions=lights, specular_mask=1)
        self.assertEqual(front_only.linear_rgb, (0, 0, 0))
        self.assertRGB(two_sided.linear_rgb, (3.4000000059604645,) * 3)

    def test_face_is_irrelevant_to_one_sided_profiles(self):
        varying = self.make_vertex((1, 1, 1))
        for profile in ('8759c7838bbc86c2', '593e5dea9b3457d5', '8d5b2ba0fb4d13bf'):
            expected = self.sample(varying, profile)
            for face in (0, -0.0, math.nan, math.inf, -math.inf, None):
                with self.subTest(profile=profile, face=face):
                    self.assertEqual(self.sample(varying, profile, face=face), expected)
        for profile in ('63f96eba9eea7880', '7a0bb00a8070496a', 'dab93928f26906f7'):
            for face in (0, -0.0, math.nan, math.inf, -math.inf):
                with self.subTest(profile=profile, face=face), self.assertRaises(ValueError):
                    self.sample(varying, profile, face=face)

    def test_affine_artist_transform_precedes_color_decode(self):
        affine = ((0, 1, 0, 0.1), (1, 0, 0, -0.3), (0, 0, 0.5, 0))
        varying = self.make_vertex((1, 1, 1))
        transformed = self.sample(varying, "8d5b2ba0fb4d13bf",
                                  diffuse=(0.2, 0.4, 0.6, 0.25), affine=affine)
        self.assertRGB(transformed.linear_rgb, (0.5 ** 2.2, 0, 0.3 ** 2.2))
        untouched = self.sample(varying, "593e5dea9b3457d5",
                                diffuse=(0.2, 0.4, 0.6, 0.25), affine=affine)
        self.assertRGB(untouched.linear_rgb, (0.2 ** 2.2, 0.4 ** 2.2, 0.6 ** 2.2))

    def test_reflection_and_lightmap_are_separate_linear_contributions(self):
        result = self.sample(self.make_vertex(), diffuse=(0.5, 0.25, 1, 0.25),
                             specular_mask=0.5, cubemap=(0.25, 0.5, 0),
                             lightmap=(0, 0, 0.5, 0.75),
                             gains=Gains(lightmap_emissive=4))
        self.assertRGB(result.linear_rgb, (0.5 ** 2.2 * 0.25 ** 2.2 * 0.5,
                                           0.25 ** 2.2 * 0.5 ** 2.2 * 0.5,
                                           4 * 0.5 ** 2.2))
        self.assertEqual(self.make_vertex().reflection, (0, 0, 2))

    def test_all_gains_leave_original_fog_alpha_unaffected(self):
        # View length 2; fog factor = 1 - 0.25*2 = 0.5.
        for gains in (Gains(0, 0, 0), Gains(), Gains(16, 4, 16)):
            varying = self.make_vertex((2, 3, 4), material_alpha=0.8,
                                       fog_clip=(1, 0.25), gains=gains)
            output = self.sample(varying, gains=gains, glow=0.25)
            self.assertAlmostEqual(output.encoded_rgba[3], 0.15)

    def test_source_and_target_half_quantization_are_explicit(self):
        varying = self.make_vertex()
        code = 0.3333
        source = self.sample(varying, lightmap=(code, 0, 0, 0.75), half_source=True)
        self.assertEqual(half(code), 0.333251953125)
        self.assertAlmostEqual(source.linear_rgb[0], 0.333251953125 ** 2.2)
        self.assertAlmostEqual(source.encoded_rgba[0], 0.333251953125)
        target = self.sample(self.make_vertex((CAP, 0, 0)), half_target=True)
        self.assertEqual(target.encoded_rgba[0], 154.625)
        self.assertTrue(math.isfinite(target.encoded_rgba[0]))
        self.assertEqual(target.encoded_rgba[1:3], (0, 0))

    def test_analytic_geometry_domain_and_contract_validation(self):
        invalid = [PointLight((0, 0, 0), (1, 1, 1), (1, 0, 0)),
                   PointLight((0, 0, 1), (1, 1, 1), (0, 0, 0))]
        for light in invalid:
            with self.assertRaises(ValueError):
                self.make_vertex(lights=[light])
        with self.assertRaises(ValueError):
            vertex((0, 0, 0), (0, 0, 0), (0, 0, 1), (0, 0, 0))
        with self.assertRaises(TypeError):
            self.make_vertex(lights=[(0, 0, 1)])
        with self.assertRaises(ValueError):
            self.sample(self.make_vertex(), directions=[])
        with self.assertRaises(ValueError):
            pixel("unknown", self.make_vertex(), (1, 1, 1, 1), 0,
                  (0, 0, 0, 1), (0, 0, 0), [])


class BumpTests(unittest.TestCase):
    # Explicit expectations independent of PROFILES and generated proof metadata.
    CONTRACTS = {
        'ca6bfa4a6cca7e2a': (2, True, False),
        '5e0a10fe752b6140': (2, True, True),
        '63379470db8d2a86': (1, True, False),
        '68915563dd0aac9a': (1, False, False),
        'd086fde54698070c': (1, True, True),
        'f17fffd88d134b04': (1, False, True),
    }

    def assertVector(self, actual, expected):
        self.assertEqual(len(actual), len(expected))
        for a, b in zip(actual, expected):
            self.assertAlmostEqual(a, b, delta=1e-12)

    @staticmethod
    def geometry(sample=(0, 0.5, 0, 0.5), **kwargs):
        args = dict(tangent=(1, 0, 0), binormal=(0, 1, 0),
                    geometric_normal=(0, 0, 1), view=(0, 0, 2))
        args.update(kwargs)
        return bump_geometry(sample, **args)

    def sample(self, profile='68915563dd0aac9a', varying=None, **kwargs):
        if varying is None:
            varying = vertex((0, 0, 0), (0, 0, 1), (0, 0, 2), (0, 0, 0))
        args = dict(diffuse=(1, 1, 1, 0.25), specular_mask=0,
                    lightmap=(0, 0, 0, 0.75), cubemap=lambda direction: (0, 0, 0),
                    directions=[DirectionalLight((0, 0, 1), (0, 0, 0))] * self.CONTRACTS[profile][0],
                    normal_sample=(0, 0.5, 0, 0.5), tangent=(1, 0, 0), binormal=(0, 1, 0))
        args.update(kwargs)
        return pixel(profile, varying, **args)

    def test_neutral_and_asymmetric_channels_use_actual_basis_order(self):
        self.assertEqual(self.geometry().normal, (0, 0, 1))
        self.assertEqual(self.geometry().view, (0, 0, 1))
        self.assertVector(self.geometry((0, 0.5, 0, 0.75)).normal, (0, 0.5, math.sqrt(3)/2))
        self.assertVector(self.geometry((0, 0.75, 0, 0.5)).normal, (0.5, 0, math.sqrt(3)/2))
        base = self.geometry((0, 0.625, 0, 0.75))
        self.assertVector(base.normal, (0.25, 0.5, math.sqrt(11)/4))
        for red, blue in ((1, 0), (0, 1), (-100, 100), (math.nan, math.inf)):
            self.assertEqual(self.geometry((red, 0.625, blue, 0.75)), base)

    def test_negative_q_retains_rsq_absolute_source_and_near_zero_has_no_epsilon(self):
        self.assertVector(self.geometry((0, 1, 0, 1)).normal, (1/math.sqrt(3),)*3)
        # x=1, y=2^-10 gives q=-2^-20 and z=2^-10, not zero.
        near = self.geometry((0, 0.5 + 2**-11, 0, 1)).normal
        length = math.sqrt(1 + 2**-19)
        self.assertVector(near, (2**-10/length, 1/length, 2**-10/length))
        x = 1 - 2**-20
        positive = self.geometry((0, 0.5, 0, 0.5 + x/2)).normal
        self.assertVector(positive, (0, x, math.sqrt(2**-19 - 2**-40)))

    def test_nonunit_nonorthogonal_and_mirrored_basis_is_not_repaired(self):
        z = math.sqrt(11)/4
        raw = (1, 1.5 + z, 4*z)
        length = math.sqrt(sum(x*x for x in raw))
        args = dict(tangent=(2, 0, 0), binormal=(1, 3, 0), geometric_normal=(0, 1, 4))
        self.assertVector(self.geometry((0, 0.625, 0, 0.75), **args).normal,
                          tuple(x/length for x in raw))
        # Mirroring tangent changes only its green-derived contribution.
        args['tangent'] = (-2, 0, 0)
        raw = (0, 1.5 + z, 4*z)
        length = math.sqrt(sum(x*x for x in raw))
        self.assertVector(self.geometry((0, 0.625, 0, 0.75), **args).normal,
                          tuple(x/length for x in raw))

    def test_face_applied_once_and_only_to_two_sided_normal(self):
        front = self.geometry((0, 0.5, 0, 0.75))
        back = self.geometry((0, 0.5, 0, 0.75), two_sided=True, face=-1)
        self.assertVector(back.normal, tuple(-x for x in front.normal))
        self.assertVector(back.reflection, front.reflection)
        for face in (0, math.nan, math.inf, None):
            self.assertEqual(self.geometry(face=face), self.geometry())
        for profile, (count, _, two_sided) in self.CONTRACTS.items():
            light = DirectionalLight((0, 0, -1), (1, 1, 1))
            varying = vertex((0, 0, 0), (0, 0, 1), (0, 0, -2), (0, 0, 0))
            result = self.sample(profile, varying, directions=[light]*count, face=-1, specular_mask=1)
            expected = count * (3 + 0.4000000059604645) if two_sided else 0
            self.assertVector(result.linear_rgb, (expected,)*3)

    def test_direction_dependent_cube_uses_bumped_normal_and_normalized_view(self):
        seen = []
        def cube(direction):
            seen.append(direction)
            return (0.5 + direction[0]/4, 0.5 + direction[1]/3, 0.5 + direction[2]/5)
        result = self.sample(normal_sample=(0, 0.5, 0, 0.75), specular_mask=0.5,
                             diffuse=(0.25, 0.5, 0.75, 0.25), cubemap=cube)
        self.assertEqual(len(seen), 1)
        self.assertVector(seen[0], (0, math.sqrt(3)/2, 0.5))
        expected_cube = (0.5, 0.5 + math.sqrt(3)/6, 0.6)
        self.assertVector(result.linear_rgb, tuple(0.5*a**2.2*c**2.2
                          for a, c in zip((0.25, 0.5, 0.75), expected_cube)))
        with self.assertRaises(TypeError):
            self.sample(cubemap=(0.5, 0.5, 0.5))

    def test_geometric_point_response_stays_separate_from_bumped_directional(self):
        point = PointLight((0, 0, 2), (0.5, 0, 0), (1, 0, 0))
        varying = vertex((0, 0, 0), (0, 0, 0.25), (0, 0, 2), (0, 0, 2), [point])
        light = DirectionalLight((0, 0, 1), (0, 1, 0))
        neutral = self.sample(varying=varying, directions=[light])
        perturbed = self.sample(varying=varying, directions=[light], normal_sample=(0, 1, 0, 1))
        self.assertVector(neutral.linear_rgb, (0.25*0.5**2.2, 0.4000000059604645, 2))
        # Mixed normal = (1,1,0.25); geometric point lighting does not change.
        self.assertVector(perturbed.linear_rgb,
                          (0.25*0.5**2.2, 0.4000000059604645/math.sqrt(33), 2))

    def test_each_bump_profile_retains_argon_lobe_affine_and_opacity(self):
        angled = vertex((0, 0, 0), (0, 0, 1), (math.sqrt(3), 0, 1), (0, 0, 0))
        for profile, (count, affine, _) in self.CONTRACTS.items():
            colors = (0.5, 0.25, 1)
            lights = [DirectionalLight((0, 0, 1), colors)] + [DirectionalLight((0, 0, 1), (0, 0, 0))]*(count-1)
            result = self.sample(profile, angled, directions=lights, specular_mask=1)
            self.assertVector(result.linear_rgb, tuple((0.4000000059604645+3*0.5**5)*c**2.2 for c in colors))
            for gains in (Gains(0, 0, 0), Gains(), Gains(16, 4, 16)):
                varying = vertex((0, 0, 0), (0, 0, 1), (0, 0, 2), (1, 1, 1),
                                 material_alpha=0.8, fog_clip=(1, 0.25), gains=gains)
                shifted = self.sample(profile, varying, diffuse=(0.2, 0.4, 0.6, 0.25),
                                      affine=((0, 1, 0, 0), (1, 0, 0, 0), (0, 0, 0, 0.5)),
                                      glow=0.25, gains=gains, normal_sample=(1, 1, 1, 1))
                codes = (0.4, 0.2, 0.5) if affine else (0.2, 0.4, 0.6)
                self.assertVector(shifted.linear_rgb, tuple(c**2.2*gains.material_emissive for c in codes))
                self.assertAlmostEqual(shifted.encoded_rgba[3], 0.15)

    def test_neutral_bump_preserves_existing_argon_color_results(self):
        counterparts = {
            'ca6bfa4a6cca7e2a': '8759c7838bbc86c2',
            '5e0a10fe752b6140': '63f96eba9eea7880',
            '63379470db8d2a86': '8d5b2ba0fb4d13bf',
            '68915563dd0aac9a': '593e5dea9b3457d5',
            'd086fde54698070c': '7a0bb00a8070496a',
            'f17fffd88d134b04': 'dab93928f26906f7',
        }
        gains = Gains(2, 3, 4)
        varying = vertex((0, 0, 0), (0, 0, 0.25), (1, 0, 2), (0.2, 0.3, 0.4),
                         [PointLight((0, 0, 3), (0.5, 0.25, 1), (1, 0.5, 0.25))], gains=gains)
        cube = (0.2, 0.6, 0.4)
        for profile, old in counterparts.items():
            common = dict(diffuse=(0.25, 0.5, 0.75, 0.3333), specular_mask=0.4,
                          lightmap=(0.1, 0.2, 0.3, 0.6), glow=0.25, gains=gains,
                          directions=[DirectionalLight((0.2, 0.1, 0.8), (0.25, 0.5, 0.75))]*self.CONTRACTS[profile][0])
            for quantization in ({}, {'half_source': True}, {'half_target': True}):
                new = self.sample(profile, varying, cubemap=lambda direction: cube, **common, **quantization)
                original = pixel(old, varying, cubemap=cube, **common, **quantization)
                self.assertEqual(new, original)

    def test_endpoint_half_scope_and_degenerate_domain_are_explicit(self):
        # .3333 green rounds to this independent binary16 endpoint.
        expected = self.geometry((0, 0.333251953125, 0, 0.5))
        self.assertEqual(self.geometry((0, 0.3333, 0, 0.5), half_source=True), expected)
        for sample, args in [((0, 0.5, 0, 1), {}),  # q=0 reciprocal chain
                             ((0, 0.5, 0, 0.5), {'geometric_normal': (0, 0, 0)}),
                             ((0, 0.5, 0, 0.5), {'view': (0, 0, 0)}),
                             ((0, math.nan, 0, 0.5), {}),
                             ((0, 0.5, 0, 1.1), {}),
                             ((0, 0.5, 0, 0.5), {'tangent': (math.inf, 0, 0)})]:
            with self.subTest(sample=sample, args=args), self.assertRaises(ValueError):
                self.geometry(sample, **args)
        for face in (0, math.nan, math.inf):
            with self.assertRaises(ValueError):
                self.geometry(two_sided=True, face=face)
        with self.assertRaises(TypeError):
            self.geometry(tangent='abc')
        with self.assertRaises(TypeError):
            self.geometry(half_source=1)


if __name__ == "__main__":
    unittest.main()
