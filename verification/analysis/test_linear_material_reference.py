"""Analytical checks for the independently authored bounded DEFAULT color oracle."""
import math
import json
from pathlib import Path
import struct
import unittest

from linear_material_reference import (
    CAP, DIFFUSE_COEFFICIENT, PROFILES, DirectionalLight, Gains, PointLight, decode, encode, half,
    pixel, sanitize, vertex,
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
                )
        self.assertEqual(len(expected), 12)
        self.assertEqual({key: (value.directions, value.affine_color, value.two_sided,
                               value.diffuse_coefficient, value.specular_power, value.cube_coefficient)
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
        return pixel(profile, varying, **options)

    def test_all_twelve_contracts_retain_black_and_alpha(self):
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


if __name__ == "__main__":
    unittest.main()
