"""Float64 oracle for the first Argon linear material slice; never game code.

Equations are derived in docs/architecture/scene-linear-materials.md and
material-color-inputs.md. This is a numerical reference, not a SM3 interpreter.
It preserves the legacy angular/attenuation model and the separately scaled
material-emissive input. It does not simulate rasterization or temporal outputs.

The ordered color sanitizer's desired exceptional-value policy is NaN -> 0,
+Inf -> 65504, -Inf -> 0. D3D9 MIN/MAX ordering needs independent GPU
qualification; Python's implementation below does not establish that behavior.
Lighting geometry must be finite, with nonzero normal/view vectors and no
coincident active point light. Undefined legacy angular math is not redefined.

Optional half-source quantization models retained texture samples and original
PS varying endpoints only. It does not simulate every permitted legacy _pp
intermediate or interpolation rounding. New linear radiance remains float64
in this oracle. Half-target quantization models the encoded FP16 scene store.
"""
from dataclasses import dataclass
import math
from numbers import Real
import struct
from typing import Optional, Sequence, Tuple

Vec3 = Tuple[float, float, float]
Vec4 = Tuple[float, float, float, float]
CAP = 65504.0
DECODE_FLOOR = 1e-10
ENCODE_FLOOR = 1e-22
# Original shader-local float32 coefficient, evaluated in float64 by this oracle.
DIFFUSE_COEFFICIENT = 0.4000000059604645


def _real(value, name="value") -> float:
    if isinstance(value, bool) or not isinstance(value, Real):
        raise TypeError(name + " must be a real number, not a boolean")
    return float(value)


def _vector(values, length, name, finite=False):
    if isinstance(values, (str, bytes)):
        raise TypeError(name + " must be a numeric sequence")
    try:
        values = tuple(values)
    except TypeError as exc:
        raise TypeError(name + " must be a numeric sequence") from exc
    if len(values) != length:
        raise ValueError(name + " must have %d components" % length)
    result = tuple(_real(x, name) for x in values)
    if finite and not all(math.isfinite(x) for x in result):
        raise ValueError(name + " must be finite")
    return result


def sanitize(value: float) -> float:
    """Desired ordered MIN(MAX(value, 0), CAP), including exceptional inputs."""
    value = _real(value)
    if math.isnan(value) or value <= 0.0:
        return 0.0
    return min(value, CAP)


def decode(value: float) -> float:
    """Decode raw legacy code values; intentionally no post-decode cap."""
    value = sanitize(value)
    return max(value, DECODE_FLOOR) ** 2.2 if value > 0.0 else 0.0


def encode(value: float) -> float:
    """Sanitize final linear radiance and compatibility-encode; preserve black."""
    value = sanitize(value)
    return max(value, ENCODE_FLOOR) ** (1.0 / 2.2) if value > 0.0 else 0.0


def half(value: float) -> float:
    """Round to IEEE binary16; overflow becomes signed infinity as on narrowing."""
    value = _real(value)
    try:
        return struct.unpack("<e", struct.pack("<e", value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def _gain(value, name):
    value = _real(value, name)
    if not math.isfinite(value) or not 0.0 <= value <= 16.0:
        raise ValueError(name + " must be finite and in [0, 16]")
    return value


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def _unit(v, name):
    length = math.hypot(*v)
    if length == 0.0 or not math.isfinite(length):
        raise ValueError(name + " must have finite, nonzero length")
    return tuple(x / length for x in v)


def _sat(value):
    return min(max(value, 0.0), 1.0)


@dataclass(frozen=True)
class Gains:
    direct: float = 1.0
    material_emissive: float = 1.0
    lightmap_emissive: float = 1.0

    def __post_init__(self):
        for name in ("direct", "material_emissive", "lightmap_emissive"):
            object.__setattr__(self, name, _gain(getattr(self, name), name))


@dataclass(frozen=True)
class PointLight:
    position: Vec3
    color: Vec3
    attenuation: Vec3

    def __post_init__(self):
        object.__setattr__(self, "position", _vector(self.position, 3, "position", True))
        object.__setattr__(self, "color", _vector(self.color, 3, "color"))
        object.__setattr__(self, "attenuation", _vector(self.attenuation, 3, "attenuation", True))


@dataclass(frozen=True)
class DirectionalLight:
    direction: Vec3
    color: Vec3

    def __post_init__(self):
        # The original PS normalizes N and V, not the supplied direction.
        object.__setattr__(self, "direction", _vector(self.direction, 3, "direction", True))
        object.__setattr__(self, "color", _vector(self.color, 3, "color"))


@dataclass(frozen=True)
class PixelProfile:
    directions: int
    affine_color: bool
    two_sided: bool


# Original game-program identities describe six derived contracts, not raw code.
PROFILES = {
    "8759c7838bbc86c2": PixelProfile(2, True, False),
    "63f96eba9eea7880": PixelProfile(2, True, True),
    "593e5dea9b3457d5": PixelProfile(1, False, False),
    "7a0bb00a8070496a": PixelProfile(1, True, True),
    "8d5b2ba0fb4d13bf": PixelProfile(1, True, False),
    "dab93928f26906f7": PixelProfile(1, False, True),
}
IDENTITY_AFFINE = ((1.0, 0.0, 0.0, 0.0), (0.0, 1.0, 0.0, 0.0),
                   (0.0, 0.0, 1.0, 0.0))


@dataclass(frozen=True)
class VertexResult:
    normal: Vec3
    view: Vec3
    reflection: Vec3
    linear_rgb: Vec3
    alpha: float


def vertex(world_position, world_normal, camera_position, material_emissive,
           lights: Sequence[PointLight] = (), *, fixed_single=False,
           material_alpha=1.0, fog_clip: Optional[Tuple[float, float]] = None,
           gains=Gains()) -> VertexResult:
    """Evaluate lighting varyings from already-transformed world inputs.

    Loop VS contracts consume 0..8 active lights. The fixed-single VS contract
    consumes exactly one supplied light, without an integer-count condition.
    The transformed world normal remains unnormalized, as in the original VS.
    Emissive is already color times native strength: sanitize, gain, never pow.
    """
    if not isinstance(gains, Gains):
        raise TypeError("gains must be Gains")
    if not isinstance(fixed_single, bool):
        raise TypeError("fixed_single must be boolean")
    position = _vector(world_position, 3, "world_position", True)
    normal = _vector(world_normal, 3, "world_normal", True)
    camera = _vector(camera_position, 3, "camera_position", True)
    _unit(normal, "world_normal")
    view = tuple(c - p for c, p in zip(camera, position))
    _unit(view, "view")
    lights = tuple(lights)
    if (fixed_single and len(lights) != 1) or (not fixed_single and len(lights) > 8):
        raise ValueError("fixed VS requires one light; loop VS permits zero through eight")
    if not all(isinstance(light, PointLight) for light in lights):
        raise TypeError("lights must contain PointLight values")
    rgb = [sanitize(x) * gains.material_emissive
           for x in _vector(material_emissive, 3, "material_emissive")]
    for light in lights:
        offset = tuple(l - p for l, p in zip(light.position, position))
        direction = _unit(offset, "point-light displacement")
        distance = math.hypot(*offset)
        denominator = _dot(light.attenuation, (1.0, distance, distance * distance))
        if not math.isfinite(denominator) or denominator == 0.0:
            raise ValueError("point attenuation denominator must be finite and nonzero")
        response = _sat(_dot(normal, direction)) * _sat(1.0 / denominator)
        for i in range(3):
            rgb[i] += response * decode(light.color[i]) * gains.direct
    alpha = _real(material_alpha, "material_alpha")
    if not math.isfinite(alpha):
        raise ValueError("material_alpha must be finite")
    if fog_clip is not None:
        intercept, slope = _vector(fog_clip, 2, "fog_clip", True)
        alpha *= _sat(intercept - slope * math.hypot(*view))
    reflection = tuple(2.0 * _dot(view, normal) * n - v for n, v in zip(normal, view))
    return VertexResult(normal, view, reflection, tuple(rgb), alpha)


@dataclass(frozen=True)
class PixelResult:
    linear_rgb: Vec3
    encoded_rgba: Vec4


def pixel(profile: str, varying: VertexResult, diffuse, specular_mask,
          lightmap, cubemap, directions: Sequence[DirectionalLight], *,
          affine=IDENTITY_AFFINE, face=1.0, glow=0.0, gains=Gains(),
          half_source=False, half_target=False) -> PixelResult:
    """Evaluate one PS sample using explicit sampled inputs and VS varyings.

    Cube lookup geometry is exposed by vertex().reflection; this function takes
    an already sampled cube RGB because texture sampling is outside this oracle.
    RGB gains leave the original alpha interpolation untouched. A caller using
    vertex() must pass the same Gains to both stages.
    """
    if not isinstance(profile, str) or profile not in PROFILES:
        raise ValueError("unknown pixel profile")
    if not isinstance(varying, VertexResult) or not isinstance(gains, Gains):
        raise TypeError("expected VertexResult and Gains")
    if not isinstance(half_source, bool) or not isinstance(half_target, bool):
        raise TypeError("quantization switches must be boolean")
    contract = PROFILES[profile]
    directions = tuple(directions)
    if len(directions) != contract.directions:
        raise ValueError("directional-light count does not match profile")
    if not all(isinstance(light, DirectionalLight) for light in directions):
        raise TypeError("directions must contain DirectionalLight values")
    quantize = half if half_source else float
    source = lambda values, size, name: tuple(quantize(v) for v in _vector(values, size, name))
    diffuse = source(diffuse, 4, "diffuse")
    lightmap = source(lightmap, 4, "lightmap")
    cubemap = source(cubemap, 3, "cubemap")
    mask = quantize(_real(specular_mask, "specular_mask"))
    if not math.isfinite(mask) or not 0.0 <= mask <= 1.0:
        raise ValueError("specular_mask must be a normalized finite data sample")
    normal = _unit(source(varying.normal, 3, "normal"), "normal")
    view = _unit(source(varying.view, 3, "view"), "view")
    if contract.two_sided:
        face = _real(face, "face")
        if not math.isfinite(face) or face == 0.0:
            raise ValueError("face must be finite and nonzero")
        if face < 0.0:
            normal = tuple(-x for x in normal)
    albedo_code = diffuse[:3]
    if contract.affine_color:
        try:
            rows = tuple(_vector(row, 4, "affine row", True) for row in affine)
        except TypeError as exc:
            raise TypeError("affine must contain three numeric rows") from exc
        if len(rows) != 3:
            raise ValueError("affine must contain three rows")
        albedo_code = tuple(_dot(row, (*albedo_code, 1.0)) for row in rows)
    albedo = tuple(decode(x) for x in albedo_code)
    directional = [0.0, 0.0, 0.0]
    for light in directions:
        cosine = _sat(_dot(normal, light.direction))
        reflected = tuple(2.0 * _dot(normal, light.direction) * n - l
                          for n, l in zip(normal, light.direction))
        highlight = _sat(_dot(view, reflected)) ** 5
        lobe = DIFFUSE_COEFFICIENT * cosine + 3.0 * mask * _sat(3.0 * cosine) * highlight
        for i in range(3):
            directional[i] += lobe * decode(light.color[i]) * gains.direct
    vertex_rgb = _vector(varying.linear_rgb, 3, "linear vertex RGB")
    radiance = tuple(sanitize(albedo[i] * (vertex_rgb[i] + directional[i])
                              + decode(cubemap[i]) * mask * albedo[i]
                              + decode(lightmap[i]) * gains.lightmap_emissive)
                     for i in range(3))
    glow = _real(glow, "glow")
    alpha_source = quantize(_real(varying.alpha, "vertex alpha"))
    alpha = (glow * lightmap[3] + (1.0 - glow) * diffuse[3]) * alpha_source
    rgba = tuple(encode(x) for x in radiance) + (alpha,)
    return PixelResult(radiance, tuple(half(x) for x in rgba) if half_target else rgba)
