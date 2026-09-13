"""Execute the 14 XT pixel contracts against the independent equation oracle."""

from dataclasses import replace
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from verification.analysis import xt_material_reference as ref
from verification.analysis import xt_pixel_execution as shader


ROOT = Path(__file__).resolve().parents[2]


class SampleFeed:
    """Coordinate-checking texture feed; it performs no filtering simulation."""

    def __init__(self, inputs, bump, texcoord=(0.17, 0.29, 0.61, 0.73), tiling=1.7):
        self.inputs = inputs
        self.bump = bump
        self.texcoord = texcoord
        self.tiling = tiling
        self.seen = []

    def _two_d(self, role, expected, value):
        def sample(coordinate):
            self.seen.append((role, tuple(coordinate)))
            if any(abs(coordinate[index] - expected[index]) > 1e-10
                   for index in range(2)):
                raise AssertionError((role, coordinate, expected))
            return value
        return sample

    def bindings(self):
        uv = self.texcoord[:2]
        secondary = self.texcoord[2:]
        detail_uv = tuple(value * self.tiling for value in uv)
        cube = self._cube
        if not self.bump:
            return {
                0: self._two_d("diffuse", uv, self.inputs.diffuse),
                1: self._two_d("specular", uv, self.inputs.specular),
                2: self._two_d("lightmap", uv, self.inputs.lightmap),
                3: cube,
                4: self._two_d("occlusion", secondary, self.inputs.occlusion),
            }
        return {
            0: self._two_d("diffuse", uv, self.inputs.diffuse),
            1: self._two_d("bump", uv, self.inputs.bump_sample),
            2: self._two_d("specular", uv, self.inputs.specular),
            3: self._two_d("lightmap", uv, self.inputs.lightmap),
            4: cube,
            5: self._two_d("occlusion", secondary, self.inputs.occlusion),
            6: self._two_d("detail", detail_uv, self.inputs.detail),
        }

    def _cube(self, coordinate):
        self.seen.append(("cube", tuple(coordinate)))
        value = (self.inputs.cube(coordinate[:3]) if callable(self.inputs.cube)
                 else self.inputs.cube)
        return tuple(value) + (0.37,)


class XTPixelExecutionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.originals = Path(os.environ.get(
            "X3M_SHADER_PROGRAM_DIRECTORY", "/tmp/x3-shader-sweep/programs"))
        required = [cls.originals / f"ps_{hash_}.bin" for hash_ in ref.CONTRACTS]
        if not all(path.is_file() for path in required):
            raise unittest.SkipTest("local XT original shader programs are absent")
        compiler = shutil.which("clang++") or shutil.which("c++")
        if not compiler:
            raise unittest.SkipTest("host C++ compiler is absent")
        temporary = tempfile.TemporaryDirectory(prefix="x3-xt-pixel-execution-")
        cls.addClassCleanup(temporary.cleanup)
        cls.generated = Path(temporary.name)
        executable = cls.generated / "xt-material-structure"
        command = [
            compiler, "-std=c++17", "-O2", "-Wall", "-Wextra", "-Werror",
            str(ROOT / "verification/probe/xt_material_structure.cpp"),
            str(ROOT / "src/renderer/linear_material.cpp"),
            str(ROOT / "src/renderer/material_motion.cpp"),
            "-o", str(executable),
        ]
        result = subprocess.run(command, text=True, capture_output=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        result = subprocess.run([str(executable), str(cls.originals), str(cls.generated)],
                                text=True, capture_output=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def assertVectorClose(self, actual, expected, tolerance=3e-6):
        self.assertEqual(len(actual), len(expected))
        for lane, (got, wanted) in enumerate(zip(actual, expected)):
            self.assertTrue(math.isfinite(got), (lane, got))
            self.assertAlmostEqual(got, wanted, delta=tolerance,
                                   msg=f"lane {lane}: {got} != {wanted}")

    @staticmethod
    def fixture(**changes):
        inputs = ref.Inputs(
            diffuse=(0.62, 0.31, 0.18, 0.70),
            specular=(0.40, 0.08, 0.91, 0.64),
            lightmap=(0.13, 0.22, 0.37, 0.80),
            occlusion=(0.49, 0.27, 0.63, 0.75),
            bump_sample=(0.86, 0.46, 0.21, 0.56),
            detail=(0.61, 0.38, 0.19, 0.72),
            geometric_normal=(0.11, -0.07, 0.98),
            tangent_y=(0.02, 0.99, 0.04),
            tangent_x=(0.98, -0.03, 0.08),
            view=(0.23, -0.17, 0.94),
            lights=(
                ref.Light((0.20, 0.10, 0.90), (0.75, 0.42, 0.18)),
                ref.Light((-0.30, 0.40, 0.80), (0.23, 0.66, 0.51)),
            ),
            vertex_native_rgb=(0.20, 0.40, 0.60),
            vertex_linear_rgb=(0.20 ** 2.2, 0.40 ** 2.2, 0.60 ** 2.2),
            vertex_alpha=0.60,
            palette=ref.Palette(
                color1=(0.18, 0.75, 0.33), color2=(0.83, 0.24, 0.58),
                color3=(0.41, 0.62, 0.16), lines=(0.90, 0.12, 0.47),
                highlight=(0.27, 0.55, 0.88), weights=(0.20, 0.70, 1.30),
                highlight_weight=0.40, weighting=0.65, lines_power=2.30),
            palette_enabled=True, decal=False, diffuse_strength=0.80,
            specular_strength=0.70, specular_power=6.0,
            reflection_strength=0.60, fresnel=0.35,
            occlusion_strength=1.40, glow=0.25,
            bump_strength=0.55, detail_strength=0.18,
            affine=((0.80, 0.10, 0.00, 0.04),
                    (0.00, 0.70, 0.20, 0.03),
                    (0.10, 0.00, 0.90, 0.02)),
            cube=lambda direction: (0.35 + 0.10 * direction[0],
                                    0.55 + 0.10 * direction[1],
                                    0.75 + 0.10 * direction[2]),
        )
        return replace(inputs, **changes)

    @staticmethod
    def authored_default(inputs):
        varying = ref.default_authored_varyings(
            inputs.view, inputs.geometric_normal, inputs.reflection_strength)
        palette = replace(inputs.palette, weights=varying.palette_weights,
                          highlight_weight=varying.highlight_weight)
        return replace(inputs, palette=palette, fresnel=varying.fresnel), varying

    def state(self, contract, inputs, *, linear, authored_default=False):
        bump = contract.bump
        feed = SampleFeed(inputs, bump)
        texcoord = feed.texcoord
        registers = {
            "v0": (*((9.0, 8.0, 7.0) if linear else inputs.vertex_native_rgb),
                   inputs.vertex_alpha),
            "v1": texcoord,
            "v2": (*inputs.view, inputs.fresnel if bump and linear else 0.0),
            "v3": (*inputs.geometric_normal,
                   inputs.palette.highlight_weight if bump and linear else 0.0),
            "vFace": (inputs.face,) * 4,
        }
        if bump:
            registers.update({
                "v4": (*inputs.tangent_y, 0.0),
                "v5": (*inputs.tangent_x, 0.0),
                "v6": (*inputs.palette.weights, 0.0),
                "v7": (*inputs.vertex_linear_rgb, 0.0) if linear else
                      (inputs.fresnel, inputs.palette.highlight_weight, 0.0, 0.0),
            })
        else:
            if authored_default:
                inputs, varying = self.authored_default(inputs)
                registers["v4"] = (*varying.palette_weights, 0.0)
                registers["v5"] = (varying.fresnel_shape,
                                   varying.highlight_weight, 0.0, 0.0)
            else:
                registers["v4"] = (*inputs.palette.weights, 0.0)
                registers["v5"] = (inputs.fresnel,
                                   inputs.palette.highlight_weight, 0.0, 0.0)
            if linear:
                registers["v8"] = (*inputs.vertex_linear_rgb, 0.0)

        for index, row in enumerate(inputs.affine):
            registers[f"c{index}"] = row
        registers.update({
            "c3": (1.0 - inputs.palette.weighting, 0.0, 0.0, 0.0),
            "c4": (inputs.glow,) * 4,
            "c5": (*inputs.lights[0].direction, 0.0),
            "c6": (*inputs.lights[0].color, 0.0),
            "c7": (*inputs.lights[1].direction, 0.0),
            "c8": (*inputs.lights[1].color, 0.0),
            "c9": (inputs.specular_strength,) * 4,
            "c10": (inputs.specular_power,) * 4,
            "c11": (inputs.reflection_strength,) * 4,
            "c12": (inputs.diffuse_strength,) * 4,
        })
        if bump:
            registers.update({
                "c13": (inputs.bump_strength,) * 4,
                "c14": (inputs.occlusion_strength,) * 4,
                "c15": (feed.tiling,) * 4,
                "c16": (inputs.detail_strength,) * 4,
            })
            first = 17
        else:
            registers["c13"] = (inputs.occlusion_strength,) * 4
            first = 14
        colors = (inputs.palette.color1, inputs.palette.color2,
                  inputs.palette.color3, inputs.palette.lines,
                  inputs.palette.highlight)
        for register, color in enumerate(colors, first):
            registers[f"c{register}"] = (*color, 0.0)
        registers[f"c{first + 5}"] = (inputs.palette.lines_power,) * 4
        registers[f"c{first + 6}"] = (inputs.palette.weighting,) * 4
        booleans = ({0: inputs.palette_enabled} if contract.family == "terraformer"
                    else {0: inputs.decal, 1: inputs.palette_enabled})
        return shader.State(registers, booleans, feed.bindings()), feed, inputs

    def run_program(self, path, contract, inputs, *, linear,
                    authored_default=False):
        state, feed, adjusted = self.state(
            contract, inputs, linear=linear, authored_default=authored_default)
        shader.execute(path.read_bytes(), state)
        self.assertIn("oC0", state.registers)
        expected = (ref.linear_pixel(contract, adjusted) if linear else
                    ref.native_pixel(contract, adjusted))
        self.assertVectorClose(state.registers["oC0"], expected.output_rgba)
        expected_samples = ({"diffuse", "specular", "lightmap", "occlusion", "cube",
                             "bump", "detail"} if contract.bump else
                            {"diffuse", "specular", "lightmap", "occlusion", "cube"})
        self.assertEqual({role for role, _ in feed.seen}, expected_samples)
        cube_coordinate = next(coordinate for role, coordinate in feed.seen
                               if role == "cube")
        self.assertVectorClose(cube_coordinate[:3], expected.reflection)

    def cases(self, contract):
        base = self.fixture(face=-1.0 if contract.two_sided else 1.0)
        if contract.family == "terraformer":
            yield replace(base, palette_enabled=False)
            yield replace(base, palette_enabled=True, occlusion=(0.24, 0.57, 0.81, 0.38))
        elif contract.damaged_normal:
            for decal in (False, True):
                for palette in (False, True):
                    for red in (0.49, 0.50, 0.51):
                        yield replace(base, decal=decal, palette_enabled=palette,
                                      occlusion=(red, 0.27, 0.63, 0.42))
        else:
            yield replace(base, decal=False, palette_enabled=False)
            yield replace(base, decal=False, palette_enabled=True,
                          occlusion=(0.51, 0.27, 0.63, 0.75))
            yield replace(base, decal=True, palette_enabled=False,
                          occlusion=(0.49, 0.27, 0.63, 0.31))
            yield replace(base, decal=True, palette_enabled=True,
                          occlusion=(0.50, 0.27, 0.63, 0.88))
        if not contract.bump:
            yield replace(base, view=(0.0, 0.0, 4.0),
                          geometric_normal=(0.0, 0.0, 7.0),
                          reflection_strength=0.0)
            yield replace(base, view=(4.0, 0.0, 0.0),
                          geometric_normal=(0.0, 0.0, 7.0),
                          reflection_strength=0.6)
        if contract.two_sided:
            yield replace(base, face=1.0, palette_enabled=True)
            yield replace(base, face=-1.0, palette_enabled=True)

    def test_original_programs_match_native_equations_for_finite_inputs(self):
        """DEFAULT uses explicitly supplied hypothetical missing input lanes here."""
        for hash_, contract in ref.CONTRACTS.items():
            path = self.originals / f"ps_{hash_}.bin"
            for case, inputs in enumerate(self.cases(contract)):
                with self.subTest(shader=hash_, case=case):
                    self.run_program(path, contract, inputs, linear=False)

    def test_all_linear_transforms_match_per_source_linear_equations(self):
        for hash_, contract in ref.CONTRACTS.items():
            path = self.generated / f"ps_{hash_}-0-1-1.bin"
            self.assertTrue(path.is_file(), path)
            for case, inputs in enumerate(self.cases(contract)):
                authored = not contract.bump
                with self.subTest(shader=hash_, case=case):
                    self.run_program(path, contract, inputs, linear=True,
                                     authored_default=authored)

    def test_repaired_ordinary_default_matches_authored_encoded_reference(self):
        defaults = [(hash_, contract) for hash_, contract in ref.CONTRACTS.items()
                    if not contract.bump]
        for hash_, contract in defaults:
            path = self.generated / f"ps_{hash_}-0-0-1.bin"
            self.assertTrue(path.is_file(), path)
            for case, inputs in enumerate(self.cases(contract)):
                with self.subTest(shader=hash_, case=case):
                    self.run_program(path, contract, inputs, linear=False,
                                     authored_default=True)


if __name__ == "__main__":
    unittest.main()
