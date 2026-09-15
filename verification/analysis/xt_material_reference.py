"""Independent float64 oracle for the fourteen XT pixel-material contracts.

This module is deliberately a mathematical model rather than a shader-token
interpreter or a copy of the production transformer.  ``native_pixel`` keeps
the original encoded-color arithmetic.  ``linear_pixel`` keeps the native
affine/decal/damage result in that domain, decodes the completed base, decodes
each later color source separately, accumulates linear radiance, and encodes the
final RGB.  Scalar masks, weights, normals, face signs, and alpha are never
color-decoded.

The domain is finite inputs, nonzero view/normal vectors, and a positive full
BUMP reconstructed-Z radicand.  It does not simulate SM3 partial precision,
rasterization, texture filtering, or undefined POW/RSQ/nonfinite behavior.
DEFAULT linkage values are not native facts; their authored repair policy is
exposed separately by ``default_authored_varyings`` and ``default_secondary_uv``.
"""

from dataclasses import dataclass, field
import math
from numbers import Real
from typing import Callable, Mapping, Sequence, Tuple, Union


Vec2 = Tuple[float, float]
Vec3 = Tuple[float, float, float]
Vec4 = Tuple[float, float, float, float]
CubeSource = Union[Vec3, Callable[[Vec3], Sequence[float]]]

DEFAULT_HIGHLIGHT_POWER = 12
DEFAULT_FRESNEL_POWER = 2
DEFAULT_MIN_FRESNEL = 0.1
DEFAULT_FRESNEL_RANGE = 0.9
LUMA = (0.298999995, 0.587000012, 0.114)
LUMA_OFFSET = 0.449999988
CAP = 65504.0
DECODE_FLOOR = 1e-10
ENCODE_FLOOR = 1e-22


@dataclass(frozen=True)
class Contract:
    label: str
    family: str
    technique: str
    two_sided: bool

    @property
    def damaged_normal(self) -> bool:
        return self.family == "damage" and self.technique == "bump"

    @property
    def bump(self) -> bool:
        return self.technique in ("bump", "low")


CONTRACTS: Mapping[str, Contract] = {
    "fffdabd910793aba": Contract("standard/default", "standard", "default", False),
    "e6794b6ec37ff71a": Contract("standard/default/2s", "standard", "default", True),
    "5f82ecacd39529cd": Contract("standard/bump", "standard", "bump", False),
    "f1b0e820c7b488c3": Contract("standard/bump/2s", "standard", "bump", True),
    "6733b119142c8d42": Contract("standard+damage/low", "standard", "low", False),
    "496049cec2066ed3": Contract("standard+damage/low/2s", "standard", "low", True),
    "d51cf763125cb85a": Contract("damage/bump", "damage", "bump", False),
    "31445adb0a62d134": Contract("damage/bump/2s", "damage", "bump", True),
    "fd58e6b7e8cf969c": Contract("terraformer/default", "terraformer", "default", False),
    "dd87737d697c6764": Contract("terraformer/default/2s", "terraformer", "default", True),
    "d22f2ce2c740e6a7": Contract("terraformer/bump", "terraformer", "bump", False),
    "1de3d2dde345a7e3": Contract("terraformer/bump/2s", "terraformer", "bump", True),
    "75fb9c6b05e28ea2": Contract("terraformer/low", "terraformer", "low", False),
    "edaef099780fcafe": Contract("terraformer/low/2s", "terraformer", "low", True),
}


IDENTITY_AFFINE = (
    (1.0, 0.0, 0.0, 0.0),
    (0.0, 1.0, 0.0, 0.0),
    (0.0, 0.0, 1.0, 0.0),
)


@dataclass(frozen=True)
class Light:
    direction: Vec3
    color: Vec3


@dataclass(frozen=True)
class Palette:
    color1: Vec3 = (0.31, 0.30, 0.07)
    color2: Vec3 = (0.30, 0.28, 0.09)
    color3: Vec3 = (0.10, 0.28, 0.30)
    lines: Vec3 = (0.34, 0.29, 0.0)
    highlight: Vec3 = (0.60, 0.10, 0.10)
    weights: Vec3 = (0.0, 0.0, 0.0)
    highlight_weight: float = 0.0
    weighting: float = 0.0
    lines_power: float = 1.0


@dataclass(frozen=True)
class Inputs:
    diffuse: Vec4 = (1.0, 1.0, 1.0, 1.0)
    specular: Vec4 = (0.0, 0.0, 0.0, 0.0)
    lightmap: Vec4 = (0.0, 0.0, 0.0, 0.0)
    occlusion: Vec4 = (0.0, 0.0, 0.0, 1.0)
    bump_sample: Vec4 = (0.5, 0.5, 0.5, 0.5)
    detail: Vec4 = (0.5, 0.5, 0.0, 0.0)
    geometric_normal: Vec3 = (0.0, 0.0, 1.0)
    tangent_y: Vec3 = (0.0, 1.0, 0.0)
    tangent_x: Vec3 = (1.0, 0.0, 0.0)
    view: Vec3 = (0.0, 0.0, 1.0)
    lights: Tuple[Light, Light] = (
        Light((0.0, 0.0, 1.0), (0.0, 0.0, 0.0)),
        Light((0.0, 0.0, 1.0), (0.0, 0.0, 0.0)),
    )
    vertex_native_rgb: Vec3 = (0.0, 0.0, 0.0)
    vertex_linear_rgb: Vec3 = (0.0, 0.0, 0.0)
    vertex_alpha: float = 1.0
    palette: Palette = field(default_factory=Palette)
    decal: bool = False
    palette_enabled: bool = False
    face: float = 1.0
    diffuse_strength: float = 1.0
    specular_strength: float = 1.0
    specular_power: float = 10.0
    reflection_strength: float = 1.0
    fresnel: float = 0.0
    occlusion_strength: float = 1.0
    glow: float = 0.0
    bump_strength: float = 1.0
    detail_strength: float = 0.0
    affine: Tuple[Vec4, Vec4, Vec4] = IDENTITY_AFFINE
    cube: CubeSource = (0.0, 0.0, 0.0)
    direct_gain: float = 1.0
    lightmap_gain: float = 1.0
    fill: float = 0.0


@dataclass(frozen=True)
class Result:
    working_rgb: Vec3
    output_rgba: Vec4
    base_encoded: Vec3
    base_working: Vec3
    normal: Vec3
    lighting_normal: Vec3
    reflection: Vec3
    palette_rgb: Vec3
    alpha: float


@dataclass(frozen=True)
class DefaultAuthoredVaryings:
    palette_weights: Vec3
    highlight_weight: float
    fresnel_shape: float
    fresnel: float
    reflection: Vec3


def _real(value, name="value") -> float:
    if isinstance(value, bool) or not isinstance(value, Real):
        raise TypeError(name + " must be a real number")
    value = float(value)
    if not math.isfinite(value):
        raise ValueError(name + " must be finite")
    return value


def _vec(values, length, name) -> tuple:
    if isinstance(values, (str, bytes)):
        raise TypeError(name + " must be a numeric sequence")
    values = tuple(values)
    if len(values) != length:
        raise ValueError(name + " must have %d components" % length)
    return tuple(_real(value, name) for value in values)


def _dot(left, right) -> float:
    return sum(a * b for a, b in zip(left, right))


def _add(left, right) -> tuple:
    return tuple(a + b for a, b in zip(left, right))


def _mul(left, right) -> tuple:
    if isinstance(right, Real) and not isinstance(right, bool):
        return tuple(value * float(right) for value in left)
    return tuple(a * b for a, b in zip(left, right))


def _unit(vector, name) -> tuple:
    vector = _vec(vector, 3, name)
    length = math.sqrt(_dot(vector, vector))
    if length == 0.0:
        raise ValueError(name + " must be nonzero")
    return tuple(value / length for value in vector)


def saturate(value: float) -> float:
    return min(max(_real(value), 0.0), 1.0)


def decode(value: float) -> float:
    """Established finite gamma-2.2 decode policy for a color source."""
    value = min(max(_real(value), 0.0), CAP)
    return max(value, DECODE_FLOOR) ** 2.2 if value > 0.0 else 0.0


def encode(value: float) -> float:
    """Finite gamma-2.2 compatibility encode; negative radiance becomes black."""
    value = min(max(_real(value), 0.0), CAP)
    return max(value, ENCODE_FLOOR) ** (1.0 / 2.2) if value > 0.0 else 0.0


def decode_rgb(rgb: Sequence[float]) -> Vec3:
    return tuple(decode(value) for value in _vec(rgb, 3, "rgb"))


def encode_rgb(rgb: Sequence[float]) -> Vec3:
    return tuple(encode(value) for value in _vec(rgb, 3, "rgb"))


def affine_diffuse(diffuse: Sequence[float], affine=IDENTITY_AFFINE) -> Vec3:
    diffuse = _vec(diffuse, 4, "diffuse")
    rows = tuple(_vec(row, 4, "affine row") for row in affine)
    if len(rows) != 3:
        raise ValueError("affine must contain three rows")
    homogeneous = diffuse[:3] + (1.0,)
    return tuple(_dot(homogeneous, row) for row in rows)


def standard_decal(encoded_diffuse: Sequence[float], occlusion_rgb: Sequence[float]) -> Vec3:
    """Original componentwise standard decal schedule, in encoded color."""
    diffuse = _vec(encoded_diffuse, 3, "encoded_diffuse")
    occlusion = _vec(occlusion_rgb, 3, "occlusion_rgb")
    h = saturate(10.0 * (_dot(diffuse, LUMA) - LUMA_OFFSET))
    doubled_product = tuple(2.0 * d * o for d, o in zip(diffuse, occlusion))
    x = tuple(1.0 - 2.0 * (1.0 - o) * (1.0 - d) - p
              for d, o, p in zip(diffuse, occlusion, doubled_product))
    y = tuple(h * a + p for a, p in zip(x, doubled_product))
    t = saturate(2.0 * sum(occlusion))
    return tuple(d + t * (value - d) for d, value in zip(diffuse, y))


def encoded_base(contract: Contract, inputs: Inputs) -> Vec3:
    diffuse = affine_diffuse(inputs.diffuse, inputs.affine)
    occlusion = _vec(inputs.occlusion, 4, "occlusion")
    if contract.family == "terraformer":
        return diffuse
    if not inputs.decal:
        return diffuse
    if contract.damaged_normal:
        return _mul(diffuse, occlusion[:3])
    return standard_decal(diffuse, occlusion[:3])


def _normal(contract: Contract, inputs: Inputs) -> Vec3:
    geometric = _vec(inputs.geometric_normal, 3, "geometric_normal")
    if not contract.bump:
        return _unit(geometric, "geometric_normal")

    bump = _vec(inputs.bump_sample, 4, "bump_sample")
    detail = _vec(inputs.detail, 4, "detail")
    occlusion = _vec(inputs.occlusion, 4, "occlusion")
    damage = max(1.0 - 2.0 * occlusion[0], 0.0) if contract.damaged_normal else 1.0
    dx = (2.0 * detail[0] - 1.0) * inputs.detail_strength * damage
    dy = (2.0 * detail[1] - 1.0) * inputs.detail_strength * damage
    if contract.technique == "low":
        x = (2.0 * bump[0] - 1.0) * inputs.bump_strength + dx
        y = (2.0 * bump[1] - 1.0) * inputs.bump_strength + dy
        z = 2.0 * bump[2] - 1.0
    else:
        x = (2.0 * bump[3] - 1.0) * inputs.bump_strength + dx
        y = (2.0 * bump[1] - 1.0) * inputs.bump_strength + dy
        radicand = 1.0 - x * x - y * y
        if radicand <= 0.0:
            raise ValueError("full BUMP reconstructed-Z radicand must be positive")
        z = math.sqrt(radicand)
    mixed = _add(_add(_mul(inputs.tangent_y, y), _mul(inputs.tangent_x, x)),
                 _mul(geometric, z))
    normal = _unit(mixed, "mixed normal")
    return _mul(normal, occlusion[2]) if contract.damaged_normal else normal


def _reflection(view: Vec3, normal: Vec3) -> Vec3:
    minus_view = _mul(view, -1.0)
    return _add(minus_view, _mul(normal, -2.0 * _dot(minus_view, normal)))


def _palette_value(palette: Palette, view: Vec3, reflection: Vec3, *, linear: bool) -> Vec3:
    colors = (palette.color1, palette.color2, palette.color3,
              palette.highlight, palette.lines)
    colors = tuple(decode_rgb(color) for color in colors) if linear else tuple(
        _vec(color, 3, "palette color") for color in colors)
    line = (1.0 - abs(_dot(view, reflection))) ** _real(
        palette.lines_power, "lines_power")
    weights = (*_vec(palette.weights, 3, "palette weights"),
               _real(palette.highlight_weight, "highlight_weight"), line)
    result = (0.0, 0.0, 0.0)
    for color, weight in zip(colors, weights):
        result = _add(result, _mul(color, weight))
    return result


def _cube_sample(source: CubeSource, reflection: Vec3) -> Vec3:
    value = source(reflection) if callable(source) else source
    return _vec(value, 3, "cube sample")


def _evaluate(contract: Contract, inputs: Inputs, *, linear: bool) -> Result:
    if not isinstance(contract, Contract):
        raise TypeError("contract must be Contract")
    if not isinstance(inputs, Inputs):
        raise TypeError("inputs must be Inputs")
    if contract.family == "terraformer" and inputs.decal:
        raise ValueError("terraformer b0 is palette control, not a decal flag")
    if len(inputs.lights) != 2 or not all(isinstance(light, Light) for light in inputs.lights):
        raise ValueError("XT contracts require exactly two Light values")

    base_encoded = encoded_base(contract, inputs)
    base = decode_rgb(base_encoded) if linear else base_encoded
    normal = _normal(contract, inputs)
    lighting_normal = _mul(normal, 1.0 if inputs.face >= 0.0 else -1.0) \
        if contract.two_sided else normal
    view = _unit(inputs.view, "view")
    reflection = _reflection(view, normal)

    diffuse_sum = (0.0, 0.0, 0.0)
    specular_sum = (0.0, 0.0, 0.0)
    for light in inputs.lights:
        direction = _vec(light.direction, 3, "light direction")
        color = _mul(decode_rgb(light.color), inputs.direct_gain) if linear else _vec(light.color, 3, "light color")
        diffuse_angle = saturate(_dot(lighting_normal, direction))
        minus_light = _mul(direction, -1.0)
        halfway = _add(minus_light,
                       _mul(lighting_normal, -2.0 * _dot(minus_light, lighting_normal)))
        highlight = saturate(_dot(halfway, view))
        specular_angle = highlight ** _real(inputs.specular_power, "specular_power")
        specular_angle *= saturate(3.0 * diffuse_angle)
        diffuse_sum = _add(diffuse_sum, _mul(color, diffuse_angle))
        specular_sum = _add(specular_sum, _mul(color, specular_angle))

    detail = _vec(inputs.detail, 4, "detail")
    specular = _vec(inputs.specular, 4, "specular")
    direct_mask = max(detail[3], specular[0]) if contract.bump else specular[0]
    lit = _add(_mul(diffuse_sum, inputs.diffuse_strength),
               _mul(specular_sum, inputs.specular_strength * direct_mask))

    if linear:
        lit = _add(lit, _mul(decode_rgb(inputs.lights[0].color), inputs.direct_gain * inputs.fill))
    palette_rgb = _palette_value(inputs.palette, view, reflection, linear=linear)
    if inputs.palette_enabled:
        weighting = _real(inputs.palette.weighting, "palette weighting")
        tint_factor = _add((1.0 - weighting,) * 3, _mul(palette_rgb, weighting))
        tint = _mul(base, tint_factor)
    else:
        tint = base
    vertex_rgb = (_vec(inputs.vertex_linear_rgb, 3, "vertex_linear_rgb") if linear
                  else tuple(saturate(value) for value in _vec(
                      inputs.vertex_native_rgb, 3, "vertex_native_rgb")))
    material = _mul(tint, _add(lit, vertex_rgb))

    cube = _cube_sample(inputs.cube, reflection)
    if linear:
        cube = decode_rgb(cube)
    reflected = _mul(_mul(base, cube),
                     inputs.fresnel * inputs.reflection_strength * specular[0])
    combined = _add(material, reflected)
    occlusion = _vec(inputs.occlusion, 4, "occlusion")
    if occlusion[3] < 0.0:
        raise ValueError("occlusion alpha must be nonnegative in the finite oracle")
    occurrence = occlusion[3] ** _real(inputs.occlusion_strength, "occlusion_strength")
    working = _mul(combined, occurrence)
    lightmap_rgb = _mul(decode_rgb(inputs.lightmap[:3]), inputs.lightmap_gain) if linear else _vec(
        inputs.lightmap[:3], 3, "lightmap rgb")
    working = _add(working, lightmap_rgb)
    if contract.family == "terraformer":
        additive = decode_rgb(occlusion[:3]) if linear else occlusion[:3]
        working = _add(working, additive)

    alpha = (_real(inputs.glow, "glow") * inputs.lightmap[3]
             + (1.0 - inputs.glow) * inputs.diffuse[3]) * inputs.vertex_alpha
    output_rgb = encode_rgb(working) if linear else working
    return Result(working, output_rgb + (alpha,), base_encoded, base, normal,
                  lighting_normal, reflection, palette_rgb, alpha)


def native_pixel(contract: Union[str, Contract], inputs: Inputs = Inputs()) -> Result:
    """Evaluate the original finite pixel equation in its encoded working space."""
    return _evaluate(CONTRACTS[contract] if isinstance(contract, str) else contract,
                     inputs, linear=False)


def linear_pixel(contract: Union[str, Contract], inputs: Inputs = Inputs()) -> Result:
    """Evaluate the scene-linear extension and return compatibility-encoded RGBA."""
    return _evaluate(CONTRACTS[contract] if isinstance(contract, str) else contract,
                     inputs, linear=True)


def default_secondary_uv(texcoord0: Sequence[float]) -> Vec2:
    """Authored DEFAULT repair: copy API-expanded TEXCOORD0.zw.

    D3D's missing-component convention supplies zero through z and one for w;
    therefore a two-component declaration has secondary UV ``(0, 1)``.
    """
    values = tuple(texcoord0)
    if not 1 <= len(values) <= 4:
        raise ValueError("texcoord0 must contain one through four API components")
    values = tuple(_real(value, "texcoord0") for value in values)
    expanded = values + (0.0,) * max(0, 3 - len(values))
    expanded = expanded[:3] + ((values[3] if len(values) == 4 else 1.0),)
    return expanded[2], expanded[3]


def default_authored_varyings(world_view_delta: Sequence[float],
                              transformed_normal: Sequence[float],
                              reflection_strength: float) -> DefaultAuthoredVaryings:
    """Evaluate the explicit DEFAULT linkage policy, separate from native math."""
    view = _unit(world_view_delta, "world_view_delta")
    normal = _unit(transformed_normal, "transformed_normal")
    reflection = _reflection(view, normal)
    q = saturate(_dot(view, normal))
    shape = (1.0 - q) ** DEFAULT_FRESNEL_POWER
    strength = _real(reflection_strength, "reflection_strength")
    return DefaultAuthoredVaryings(
        tuple(abs(value) for value in reflection),
        q ** DEFAULT_HIGHLIGHT_POWER,
        shape,
        DEFAULT_MIN_FRESNEL + DEFAULT_FRESNEL_RANGE * shape * strength,
        reflection,
    )
