"""Analytical checks for independently authored DEFAULT and BUMPMAP color equations."""
import math
import json
from pathlib import Path
import struct
import unittest

from linear_material_reference import (
    CAP, DIFFUSE_COEFFICIENT, PROFILES, DirectionalLight, Gains, LightingCoefficients, PointLight, decode, encode, half,
    bump_geometry, pixel, sanitize, vertex,
    ASTEROID_PROFILES, AsteroidWeights, asteroid_pixel,
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
            if row['id'].startswith('ps_') and not {'asteroid_default','asteroid_bump'} & set(row['families']):
                app = row['lobe_coefficients'].get('source') == 'application'
                if app:
                    self.assertEqual(row['lobe_coefficients'], {
                        'source': 'application', 'diffuse': 'g_MatDiffuseStrength',
                        'specular': 'g_MatSpecularStrength', 'specular_power': 'g_MatSpecularPower',
                        'cube': 'g_MatReflectionStrength',
                        'defaults': {'diffuse': 1.0, 'specular': 1.0, 'specular_power': 10.0, 'cube': 1.0}})
                expected[row['fnv1a64']] = (
                    len({source['name'] for source in row['directional_rgb_sources']}),
                    row['diffuse_affine_completion'] is not None,
                    row['two_sided'],
                    None if app else row['lobe_coefficients']['diffuse'],
                    None if app else row['lobe_coefficients']['specular_power'],
                    None if app else row['lobe_coefficients']['cube'],
                    bool({'argon_bump', 'standard_bump', 'standard_bump_low', 'shared_bump',
                          'split_bump', 'terran_bump'} & set(row['families'])),
                    app, 'xyz' if 'standard_bump_low' in row['families'] else 'ag',
                )
        self.assertEqual(len(expected), 66)
        self.assertEqual({key: (value.directions, value.affine_color, value.two_sided,
                               None if value.application_coefficients else value.diffuse_coefficient,
                               None if value.application_coefficients else value.specular_power,
                               None if value.application_coefficients else value.cube_coefficient,
                               value.bump_map, value.application_coefficients, value.normal_encoding)
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
            options.setdefault('normal_sample', (0.5, 0.5, 1, 0) if PROFILES[profile].normal_encoding == 'xyz'
                               else (0, 0.5, 0, 0.5))
            options.setdefault('tangent', (1, 0, 0))
            options.setdefault('binormal', (0, 1, 0))
            if not callable(options['cubemap']):
                sample = options['cubemap']
                options['cubemap'] = lambda direction: sample
        return pixel(profile, varying, **options)

    def test_all_contracts_retain_black_and_alpha(self):
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


class ExtendedFamilyTests(unittest.TestCase):
    # Order is independently pinned: 2-light affine front/back, then 1-light
    # affine front/back, then 1-light nonaffine front/back.
    SPLIT = ('462342e3e5781384', '827d8d2d617bedce', '02606104fa59fb29',
             '1d638938d93421b3', 'bd4d51c08486c6e0', 'de2dd381fa64193d')
    STANDARD = (
        ('7c83ed50c9894e44', 'e70adc744a38ca59', 'db644b73b68c0547',
         'ff32b602a271c327', 'f6a501717c3e5ca8', '55826dc176afe464'),
        ('0c1f3f0f440e4a0c', '64bac8bb307eb896', '789449ffd931d23e',
         '4f052209611387f0', 'abf3c0fad53456d8', 'cf449bcb069aec4f'),
        ('99153c144030c396', 'c1452981fd0bff64', 'b0f9313b77cc78ee',
         'd514bf852d8a9c58', 'dff6a3d360603fa2', 'f1d14a7dbf7c6173'))

    def assertRGB(self, actual, expected):
        for a, b in zip(actual, expected):
            self.assertAlmostEqual(a, b, delta=1e-12)

    def sample(self, profile, count=1, **kwargs):
        args = dict(diffuse=(1, 1, 1, .25), specular_mask=0,
                    lightmap=(0, 0, 0, .75), cubemap=(0, 0, 0),
                    directions=[DirectionalLight((0, 0, 1), (0, 0, 0))]*count)
        varying = kwargs.pop('varying', vertex((0, 0, 0), (0, 0, 1), (.6, 0, .8), (0, 0, 0)))
        args.update(kwargs)
        # Use the independent family lists, not PROFILES, to form valid inputs.
        if profile in self.STANDARD[1] + self.STANDARD[2]:
            args.setdefault('normal_sample', (.5, .5, 1, .5))
            args.setdefault('tangent', (1, 0, 0))
            args.setdefault('binormal', (0, 1, 0))
            if not callable(args['cubemap']):
                value = args['cubemap']
                args['cubemap'] = lambda direction: value
        return pixel(profile, varying, **args)

    def test_all_new_shapes_and_split_coefficients_independently(self):
        shapes = ((2, True, False), (2, True, True), (1, True, False),
                  (1, True, True), (1, False, False), (1, False, True))
        for family, ids in enumerate((self.SPLIT,) + self.STANDARD):
            for name, shape in zip(ids, shapes):
                p = PROFILES[name]
                self.assertEqual((p.directions, p.affine_color, p.two_sided), shape)
                self.assertEqual(p.application_coefficients, family != 0)
                self.assertEqual(p.bump_map, family >= 2)
                self.assertEqual(p.normal_encoding, 'xyz' if family == 3 else 'ag')
        for i, name in enumerate(self.SPLIT):
            count = 2 if i < 2 else 1
            with self.subTest(profile=name):
                lights = [DirectionalLight((0, 0, .2), (1, 1, 1))]*count
                result = self.sample(name, count, directions=lights, specular_mask=.7,
                                     cubemap=(.5, .25, .75))
                expected_lobe = .5*.2 + 3*.7*.6*(.16**10)
                self.assertRGB(result.linear_rgb,
                               [count*expected_lobe + .7*c**2.2 for c in (.5, .25, .75)])

    def test_all_standard_profiles_use_independent_application_scalars(self):
        for ids in self.STANDARD:
            for i, name in enumerate(ids):
                count = 2 if i < 2 else 1
                with self.subTest(profile=name):
                    lights = [DirectionalLight((0, 0, .2), (.2, .4, .8))]*count
                    args = dict(directions=lights, specular_mask=.7, diffuse=(.25, .5, .75, .25),
                                cubemap=(.5, .25, .75), lightmap=(.1, .2, .3, .75), glow=.25)
                    coeff = LightingCoefficients(2.5, .75, 1.25, 2.5)
                    result = self.sample(name, count, coefficients=coeff, **args)
                    lobe = 2.5*.2 + .75*.7*.6*(.16**2.5)
                    expected = [a**2.2*(count*lobe*c**2.2 + .7*1.25*r**2.2) + lm**2.2
                                for a, c, r, lm in zip((.25, .5, .75), (.2, .4, .8), (.5, .25, .75), (.1, .2, .3))]
                    self.assertRGB(result.linear_rgb, expected)
                    default = self.sample(name, count, **args)
                    self.assertEqual(default, self.sample(name, count, coefficients=LightingCoefficients(), **args))
                    self.assertEqual(result.encoded_rgba[3], default.encoded_rgba[3])
                    self.assertEqual(result.encoded_rgba[3], .375)

    def test_scalar_controls_are_separate_from_point_emissive_lightmap_and_our_gains(self):
        name = self.STANDARD[0][4]
        varying = vertex((0, 0, 0), (0, 0, 1), (0, 0, 1), (.25, .5, .75),
                         [PointLight((0, 0, 1), (.5, .25, .75), (1, 0, 0))])
        lights = [DirectionalLight((0, 0, 1), (1, 1, 1))]
        args = dict(varying=varying, directions=lights, specular_mask=.5,
                    cubemap=(1, 1, 1), lightmap=(.25, .5, .75, 0))
        dark = self.sample(name, coefficients=LightingCoefficients(0, 0, 0, 10), **args)
        self.assertRGB(dark.linear_rgb, [m+c**2.2+lm**2.2 for m,c,lm in
                                       zip((.25,.5,.75),(.5,.25,.75),(.25,.5,.75))])
        for coeff, addition in ((LightingCoefficients(2,0,0,10), 2),
                                (LightingCoefficients(0,2,0,10), 1),
                                (LightingCoefficients(0,0,2,10), 1)):
            got = self.sample(name, coefficients=coeff, **args)
            self.assertRGB(got.linear_rgb, [x+addition for x in dark.linear_rgb])
        got = self.sample(name, coefficients=LightingCoefficients(2,2,2,10), gains=Gains(direct=0), **args)
        self.assertRGB(got.linear_rgb, [x+1 for x in dark.linear_rgb])

    def test_new_affine_and_face_roles_preserve_original_alpha(self):
        varying = vertex((0,0,0), (0,0,1), (0,0,-1), (1,1,1), material_alpha=.5)
        affine = ((1,0,0,.1), (0,1,0,.2), (0,0,1,.3))
        for family, ids in enumerate((self.SPLIT,) + self.STANDARD):
            for i, name in enumerate(ids):
                count = 2 if i < 2 else 1
                two_sided, has_affine = i % 2 == 1, i < 4
                args = dict(varying=varying, directions=[DirectionalLight((0,0,-1),(1,1,1))]*count,
                            diffuse=(.2,.4,.6,.25), affine=affine, face=-1, glow=.25)
                if family: args['coefficients'] = LightingCoefficients(2,0,0,10)
                result = self.sample(name, count, **args)
                factor = 1 + ((2 if family else .5)*count if two_sided else 0)
                self.assertRGB(result.linear_rgb, [x**2.2*factor for x in
                                                  ((.3,.6,.9) if has_affine else (.2,.4,.6))])
                self.assertEqual(result.encoded_rgba[3], .1875)

    def test_low_normal_does_not_replace_geometric_point_light_response(self):
        varying = vertex((0,0,0), (0,0,1), (0,0,1), (0,0,0),
                         [PointLight((0,0,1), (.25,.5,.75), (1,0,0))])
        name = self.STANDARD[2][4]
        a = self.sample(name, varying=varying, normal_sample=(.5,.5,1,0))
        b = self.sample(name, varying=varying, normal_sample=(.5,1,.5,1))
        self.assertEqual(a, b)
        self.assertRGB(a.linear_rgb, [x**2.2 for x in (.25,.5,.75)])

    def test_coefficient_defaults_domain_and_wrong_types(self):
        c = LightingCoefficients()
        self.assertEqual((c.diffuse, c.specular, c.reflection, c.power), (1,1,1,10))
        self.assertEqual(LightingCoefficients(diffuse=1e6).diffuse, 1e6)  # No color cap or decode.
        for field in ('diffuse', 'specular', 'reflection', 'power'):
            for value in (math.nan, math.inf, -1):
                with self.assertRaises(ValueError): LightingCoefficients(**{field:value})
            for value in (True, '1', None):
                with self.assertRaises(TypeError): LightingCoefficients(**{field:value})
        with self.assertRaises(ValueError): LightingCoefficients(power=0)
        with self.assertRaises(AttributeError): c.power = 4
        with self.assertRaises(TypeError): self.sample(self.STANDARD[0][4], coefficients={})
        for name in ('593e5dea9b3457d5', self.SPLIT[4]):
            with self.assertRaises(ValueError): self.sample(name, coefficients=c)

    def test_low_xyz_asymmetric_basis_negative_z_and_unused_alpha(self):
        args = dict(normal_sample=(.7,.25,.4,math.nan), tangent=(2,1,0),
                    binormal=(0,3,1), geometric_normal=(1,0,4), view=(.3,.4,.5), normal_encoding='xyz')
        g = bump_geometry(**args)
        expected = tuple(x/math.sqrt(2.09) for x in (-1.2,.7,-.4))
        self.assertRGB(g.normal, expected)
        for a in (0, 1, -math.inf):
            self.assertEqual(g, bump_geometry(**dict(args, normal_sample=(.7,.25,.4,a))))
        back = bump_geometry(**args, two_sided=True, face=-1)
        self.assertRGB(back.normal, [-x for x in expected])
        self.assertRGB(back.reflection, g.reflection)
        mirrored = bump_geometry(**dict(args, tangent=(-2,-1,0)))
        self.assertRGB(mirrored.normal, [x/math.sqrt(.8*.8+1.7*1.7+.4*.4) for x in (.8,1.7,-.4)])

    def test_low_channel_endpoints_and_no_ag_reciprocal_boundary(self):
        args = dict(tangent=(1,0,0), binormal=(0,1,0), geometric_normal=(0,0,1), view=(0,0,1), normal_encoding='xyz')
        for sample, normal in (((1,.5,.5,1),(0,1,0)), ((.5,1,.5,1),(1,0,0)),
                               ((.5,.5,0,1),(0,0,-1)), ((.5,.5,1,1),(0,0,1))):
            self.assertEqual(bump_geometry(sample, **args).normal, normal)
        with self.assertRaises(ValueError): bump_geometry((.5,.5,.5,0), **args)
        # AG with this alpha/green has q=0; XYZ is valid and must not reconstruct Z.
        self.assertEqual(bump_geometry((1,.5,.5,1), **args).normal, (0,1,0))
        with self.assertRaises(ValueError): bump_geometry((1,.5,.5,1), **dict(args,normal_encoding='ag'))
        for sample in ((math.nan,.5,1,0), (.5,math.inf,1,0), (.5,.5,1.1,0)):
            with self.assertRaises(ValueError): bump_geometry(sample, **args)
        with self.assertRaises(ValueError): bump_geometry((.5,.5,1,0), **dict(args,normal_encoding='rgbx'))

    def test_low_quantized_endpoint_and_direction_dependent_cube(self):
        g = bump_geometry((.3333,.75,1,math.nan), (1,0,0), (0,1,0), (0,0,1), (0,0,1),
                          normal_encoding='xyz', half_source=True)
        mixed = (.5, 2*.333251953125-1, 1)
        length = math.sqrt(sum(x*x for x in mixed))
        self.assertRGB(g.normal, [x/length for x in mixed])
        seen = []
        def cube(direction):
            seen.append(direction)
            return tuple(.5+.25*x for x in direction)
        name = self.STANDARD[2][4]
        result = self.sample(name, normal_sample=(.5,.5,0,math.nan), cubemap=cube, specular_mask=1,
                             coefficients=LightingCoefficients(0,0,2,10))
        # N=-Z, V=(.6,0,.8): reflection=(-.6,0,.8), despite negative sampled Z.
        self.assertRGB(seen[0], (-.6,0,.8))
        self.assertRGB(result.linear_rgb, [2*x**2.2 for x in (.35,.5,.7)])


class RemainingHullTests(unittest.TestCase):
    # Independent original-ID expectations, ordered base front/back, single
    # affine front/back, single nonaffine front/back. No oracle profile lookup
    # supplies expected coefficients, shapes, or sample geometry below.
    GROUPS = (
        (('1f26d41bcb7dac1e','bdcdb3ab996ae4e0','78963cdc7c710e04',
          '1ed1bf0fdec00e1a','2b04461d0dae038b','acc83ed2509d84a1'), .5, 6, .5, True),
        (('3006f8030a467739','d6e8bdde0e4c515f','e5ea78b8b0b0fe07',
          'f42202faf57a3c89','769c3814fc0efba8','22cc5b05a55ef61e'), .5, 10, 1., True),
        (('ef2bf556f207b8bd','91b6c09eb47f8555','cc09f17db377fd9e',
          '3755809bd40afc13','61418505e5d8f998','b5f1d4145171026b'), 1., 5, 1., False),
        (('3602b05ce11ca6ff','8e58ac79b59b02b1','042c9ae16f41feff',
          '68f0dd6791fd7d3d','5c823b8507fa1442','a6e1328c0bb3f401'), 1., 5, 1., True))

    def assertRGB(self, actual, expected):
        for a, b in zip(actual, expected):
            self.assertAlmostEqual(a, b, delta=1e-12)

    def sample(self, name, index, bump, **kwargs):
        varying = kwargs.pop('varying', vertex((0,0,0),(0,0,1),(.6,0,.8),(0,0,0),material_alpha=.5))
        args = dict(diffuse=(1,1,1,.25), specular_mask=0,
                    lightmap=(0,0,0,.75), cubemap=(0,0,0),
                    directions=[DirectionalLight((0,0,1),(0,0,0))]*(2 if index<2 else 1))
        args.update(kwargs)
        if bump:
            args.setdefault('normal_sample',(.25,.5,.75,.5))
            args.setdefault('tangent',(1,0,0))
            args.setdefault('binormal',(0,1,0))
            if not callable(args['cubemap']):
                cube = args['cubemap']
                args['cubemap'] = lambda direction: cube
        return pixel(name, varying, **args)

    def test_all_twenty_four_shapes_and_fixed_coefficient_contracts(self):
        for names, diffuse, power, cube, bump in self.GROUPS:
            for i,name in enumerate(names):
                p=PROFILES[name]
                self.assertEqual((p.directions,p.affine_color,p.two_sided),
                                 (2 if i<2 else 1,i<4,i%2==1))
                self.assertEqual((p.diffuse_coefficient,p.specular_power,p.cube_coefficient),
                                 (diffuse,power,cube))
                self.assertEqual((p.bump_map,p.normal_encoding,p.application_coefficients),(bump,'ag',False))
                with self.assertRaises(ValueError):
                    self.sample(name,i,bump,coefficients=LightingCoefficients())

    def test_every_shape_independently_discriminates_diffuse_power_and_cube(self):
        for names,diffuse,power,cube,bump in self.GROUPS:
            for i,name in enumerate(names):
                with self.subTest(profile=name):
                    count=2 if i<2 else 1
                    colors=((.25,.5,.75),(.75,.25,.5))[:count]
                    lights=[DirectionalLight((0,0,1),c) for c in colors]
                    light_rgb=[sum(c[k]**2.2 for c in colors) for k in range(3)]
                    d=self.sample(name,i,bump,directions=lights)
                    s=self.sample(name,i,bump,directions=lights,specular_mask=.75)
                    self.assertRGB(d.linear_rgb,[diffuse*c for c in light_rgb])
                    self.assertRGB([a-b for a,b in zip(s.linear_rgb,d.linear_rgb)],
                                   [3*.75*(.8**power)*c for c in light_rgb])
                    reflection=self.sample(name,i,bump,specular_mask=.75,cubemap=(.25,.5,.75))
                    self.assertRGB(reflection.linear_rgb,[.75*cube*c**2.2 for c in (.25,.5,.75)])
                    # Packed single-light lanes must keep unsaturated diffuse
                    # d=.2 separate from the saturated inner specular factor .6.
                    angular=self.sample(name,i,bump,specular_mask=.75,
                                        directions=[DirectionalLight((0,0,.2),c) for c in colors])
                    self.assertRGB(angular.linear_rgb,
                                   [(diffuse*.2+3*.75*.6*(.16**power))*c for c in light_rgb])

    def test_every_shape_affine_face_and_alpha_remain_independent_of_gains(self):
        varying=vertex((0,0,0),(0,0,1),(0,0,-1),(.25,.5,.75),material_alpha=.5)
        affine=((1,0,0,.1),(0,1,0,.2),(0,0,1,.3))
        for names,diffuse,_,_,bump in self.GROUPS:
            for i,name in enumerate(names):
                count=2 if i<2 else 1
                args=dict(varying=varying,diffuse=(.2,.4,.6,.25),affine=affine,face=-1,glow=.25,
                          directions=[DirectionalLight((0,0,-1),(1,1,1))]*count)
                result=self.sample(name,i,bump,**args)
                albedo=(.3,.6,.9) if i<4 else (.2,.4,.6)
                self.assertRGB(result.linear_rgb,[a**2.2*(v+(count*diffuse if i%2 else 0))
                                                  for a,v in zip(albedo,(.25,.5,.75))])
                scaled=self.sample(name,i,bump,**args,gains=Gains(16,0,0))
                self.assertEqual(result.encoded_rgba[3],.1875)
                self.assertEqual(result.encoded_rgba[3],scaled.encoded_rgba[3])

    def test_new_ag_shapes_use_alpha_binormal_green_tangent_and_ignore_red_blue(self):
        for names,_,_,cube,bump in self.GROUPS:
            if not bump: continue
            for i,name in enumerate(names):
                # A=.75/G=.5 => N=(0,.5,sqrt(.75)); V=+Z therefore
                # reflection=(0,sqrt(.75),.5). An A/G swap changes cube green.
                seen=[]
                def lookup(direction):
                    seen.append(direction)
                    return tuple(.5+.25*x for x in direction)
                args=dict(varying=vertex((0,0,0),(0,0,1),(0,0,1),(0,0,0)),
                          normal_sample=(.25,.5,.75,.75),cubemap=lookup,specular_mask=1)
                result=self.sample(name,i,bump,**args)
                self.assertRGB(seen[-1],(0,math.sqrt(.75),.5))
                self.assertRGB(result.linear_rgb,[cube*c**2.2 for c in (.5,.5+.25*math.sqrt(.75),.625)])
                changed=self.sample(name,i,bump,**dict(args,normal_sample=(math.nan,.5,-math.inf,.75)))
                self.assertEqual(result,changed)

    def test_new_ag_normal_perturbation_keeps_geometric_point_response(self):
        varying=vertex((0,0,0),(0,0,1),(0,0,1),(0,0,0),
                       [PointLight((0,0,1),(.25,.5,.75),(1,0,0))])
        for names,_,_,_,bump in self.GROUPS:
            if not bump:continue
            for i,name in enumerate(names):
                a=self.sample(name,i,bump,varying=varying)
                b=self.sample(name,i,bump,varying=varying,normal_sample=(.25,.9375,.75,.9375))
                self.assertEqual(a,b)
                self.assertRGB(a.linear_rgb,[c**2.2 for c in (.25,.5,.75)])

    def test_new_shapes_retain_black_and_target_quantization(self):
        for names,_,_,_,bump in self.GROUPS:
            for i,name in enumerate(names):
                result=self.sample(name,i,bump,half_source=True,half_target=True)
                self.assertEqual(result.encoded_rgba,(0,0,0,.125))
                for x in result.encoded_rgba[:3]:self.assertEqual(math.copysign(1,x),1)


class AsteroidEvidenceTests(unittest.TestCase):
    def test_four_asteroid_profiles_match_independent_derived_evidence(self):
        path=Path(__file__).resolve().parents[2]/'docs/reverse-engineering/linear-material-profiles.json'
        evidence=json.loads(path.read_text())
        rows=[r for r in evidence['programs'] if r['id'].startswith('ps_')
              and {'asteroid_default','asteroid_bump'} & set(r['families'])]
        self.assertEqual(len(rows),4)
        expected={}
        for row in rows:
            count=len({s['name'] for s in row['directional_rgb_sources']})
            bump='asteroid_bump' in row['families']
            expected[row['fnv1a64']]=(count,bump)
            self.assertEqual(row['lobe_coefficients'],{'diffuse':1.0,'specular_power':3,
                             'specular_outer_scale':1.0,'grazing_scale':3.0,'cube':0.0})
            self.assertEqual(row['alpha_and_affine_proof']['alpha_model'],'base_alpha_times_vertex_alpha')
            self.assertEqual(row['alpha_and_affine_proof']['normal_encoding'],'ag' if bump else 'geometric')
            self.assertIsNone(row['diffuse_affine_completion'])
            self.assertFalse(row['two_sided'])
            weights=row['detail_weighting']
            self.assertEqual((weights['base']['register'],weights['detail']['register']),
                             ('c5','c4') if count==2 else ('c3','c2'))
            self.assertEqual((weights['base']['component'],weights['detail']['component']),('x','x'))
        self.assertEqual({name:(p.directions,p.bump_map) for name,p in ASTEROID_PROFILES.items()},expected)


class AsteroidTests(unittest.TestCase):
    CONTRACTS = {'517540ae6d5e5410': (2,False), '7a0c3388065bb08d': (1,False),
                 'd44db87778a43b61': (2,True), '550c2a4d4d3ed70f': (1,True)}

    def assertRGB(self, actual, expected):
        for a,b in zip(actual,expected): self.assertAlmostEqual(a,b,delta=1e-12)

    def sample(self, profile='7a0c3388065bb08d', **kwargs):
        varying=kwargs.pop('varying',vertex((0,0,0),(0,0,1),(.6,0,.8),(0,0,0),material_alpha=.5))
        count,bump=self.CONTRACTS[profile]
        args=dict(base=(.25,.5,.75,.25),detail=(.75,.25,.5,.75),specular_mask=0,
                  directions=[DirectionalLight((0,0,1),(0,0,0))]*count)
        args.update(kwargs)
        if bump:
            args.setdefault('normal_sample',(.25,.5,.75,.5))
            args.setdefault('tangent',(1,0,0));args.setdefault('binormal',(0,1,0))
        return asteroid_pixel(profile,varying,**args)

    def test_exact_four_contracts_and_native_default_weights(self):
        self.assertEqual({name:(p.directions,p.bump_map) for name,p in ASTEROID_PROFILES.items()},self.CONTRACTS)
        self.assertFalse(set(ASTEROID_PROFILES)&set(PROFILES))
        weights=AsteroidWeights()
        self.assertEqual((weights.base,weights.detail),(1,0))
        with self.assertRaises(AttributeError): weights.detail=1
        for name in self.CONTRACTS:
            a=self.sample(name,detail=(100,200,300,math.nan))
            self.assertEqual(a,self.sample(name,detail=(0,0,0,0)))
            self.assertEqual(a.encoded_rgba,(0,0,0,.125))

    def test_each_sample_decodes_before_independent_scalar_weights(self):
        v=vertex((0,0,0),(0,0,1),(0,0,1),(1,1,1))
        for name in self.CONTRACTS:
            for weights in (AsteroidWeights(1,0),AsteroidWeights(0,1),AsteroidWeights(.75,.25),AsteroidWeights(2,.5)):
                result=self.sample(name,varying=v,weights=weights)
                expected=[weights.base*b**2.2+weights.detail*d**2.2 for b,d in zip((.25,.5,.75),(.75,.25,.5))]
                self.assertRGB(result.linear_rgb,expected)
                if weights==AsteroidWeights(.75,.25):
                    wrong=[(.75*b+.25*d)**2.2 for b,d in zip((.25,.5,.75),(.75,.25,.5))]
                    self.assertTrue(all(abs(a-b)>.01 for a,b in zip(expected,wrong)))
            one=self.sample(name,varying=v,weights=AsteroidWeights(1,1))
            four=self.sample(name,varying=v,weights=AsteroidWeights(4,4))
            self.assertRGB(four.linear_rgb,[4*x for x in one.linear_rgb])

    def test_unit_diffuse_cubic_specular_inner_three_without_outer_three(self):
        for name,(count,_) in self.CONTRACTS.items():
            colors=((.25,.5,.75),(.75,.25,.5))[:count]
            light_rgb=[sum(c[k]**2.2 for c in colors) for k in range(3)]
            for cosine in (1.,.2):
                lights=[DirectionalLight((0,0,cosine),c) for c in colors]
                args=dict(base=(1,1,1,.25),directions=lights)
                d=self.sample(name,**args)
                s=self.sample(name,**args,specular_mask=.75)
                self.assertRGB(d.linear_rgb,[cosine*c for c in light_rgb])
                highlight=(.8*cosine)**3
                expected=[.75*min(1,3*cosine)*highlight*c for c in light_rgb]
                actual=[a-b for a,b in zip(s.linear_rgb,d.linear_rgb)]
                self.assertRGB(actual,expected)
                self.assertTrue(all(abs(a-3*b)>1e-5 for a,b in zip(actual,expected)))

    def test_base_alpha_only_independent_of_detail_weights_mask_and_gains(self):
        for name in self.CONTRACTS:
            for detail_alpha in (0.,1.,math.nan,math.inf):
                result=self.sample(name,detail=(.75,.25,.5,detail_alpha),weights=AsteroidWeights(0,4),
                                   gains=Gains(0,16,16),specular_mask=1)
                self.assertEqual(result.encoded_rgba[3],.125)
            v=vertex((0,0,0),(0,0,1),(0,0,2),(1,1,1),material_alpha=.5,fog_clip=(.75,.125))
            self.assertEqual(self.sample(name,varying=v).encoded_rgba[3],.0625)

    def test_geometric_point_and_scaled_emissive_multiply_detail_albedo(self):
        for name in self.CONTRACTS:
            for fixed,lights in ((False,0),(False,1),(False,8),(True,1)):
                gains=Gains(2,4,16)
                v=vertex((0,0,0),(0,0,1),(0,0,1),(.25,.5,.75),
                         [PointLight((0,0,1),(.5,.25,.75),(1,0,0))]*lights,
                         fixed_single=fixed,gains=gains)
                result=self.sample(name,varying=v,weights=AsteroidWeights(0,.5),gains=gains)
                self.assertRGB(result.linear_rgb,[.5*d**2.2*(4*m+2*lights*p**2.2)
                                                 for d,m,p in zip((.75,.25,.5),(.25,.5,.75),(.5,.25,.75))])

    def test_bump_ag_shifted_inputs_no_face_or_reflection_contribution(self):
        for name in ('d44db87778a43b61','550c2a4d4d3ed70f'):
            count=self.CONTRACTS[name][0]
            args=dict(base=(1,1,1,.25),normal_sample=(.25,.5,.75,.75),
                      directions=[DirectionalLight((0,1,0),(1,1,1))]*count)
            # Alpha .75 -> +binormal .5; tangent is X, binormal Y.
            self.assertRGB(self.sample(name,**args).linear_rgb,[.5*count]*3)
            self.assertEqual(self.sample(name,**args),self.sample(name,**dict(args,normal_sample=(math.nan,.5,-math.inf,.75))))
            swapped=self.sample(name,**dict(args,normal_sample=(.25,.75,.75,.5)))
            self.assertEqual(swapped.linear_rgb,(0,0,0))
            mirrored=self.sample(name,**args,binormal=(0,-1,0))
            self.assertEqual(mirrored.linear_rgb,(0,0,0))
            # Negative q retains sqrt(abs(q)); no new clamp or fallback normal.
            negative=self.sample(name,**dict(args,normal_sample=(.25,1,.75,1)))
            self.assertRGB(negative.linear_rgb,[count/math.sqrt(3)]*3)
            with self.assertRaises(ValueError):self.sample(name,normal_sample=(.25,.5,.75,1))
            point=vertex((0,0,0),(0,0,1),(0,0,1),(0,0,0),[PointLight((0,0,1),(.25,.5,.75),(1,0,0))])
            a=self.sample(name,varying=point)
            b=self.sample(name,varying=point,normal_sample=(.25,1,.75,1))
            self.assertEqual(a,b)

    def test_transfer_domain_endpoint_precision_and_unused_parameters(self):
        v=vertex((0,0,0),(0,0,1),(0,0,1),(1,1,1))
        for name in self.CONTRACTS:
            result=self.sample(name,varying=v,base=(.3333,0,math.inf,.25),half_source=True,half_target=True)
            self.assertEqual(result.encoded_rgba,(.333251953125,0,154.625,.25))
            black=self.sample(name,varying=v,base=(math.nan,-math.inf,-0.,.25))
            self.assertEqual(black.linear_rgb,(0,0,0))
            for x in black.encoded_rgba[:3]:self.assertEqual(math.copysign(1,x),1)
        for field in ('base','detail'):
            for value in (-1,math.nan,math.inf):
                with self.assertRaises(ValueError):AsteroidWeights(**{field:value})
            for value in ('1',True,None):
                with self.assertRaises(TypeError):AsteroidWeights(**{field:value})
        self.assertEqual(AsteroidWeights(1e6,2).base,1e6)
        with self.assertRaises(ValueError):asteroid_pixel('8759c7838bbc86c2',v,(1,1,1,1),(1,1,1,1),0,[])
        with self.assertRaises(ValueError):self.sample(directions=[])
        with self.assertRaises(TypeError):self.sample(weights=(1,0))
        with self.assertRaises(TypeError):self.sample(cubemap=(1,1,1))
        with self.assertRaises(TypeError):self.sample(half_source=1)


if __name__ == "__main__":
    unittest.main()
