"""Float64 oracle for bounded DEFAULT/BUMPMAP/BUMPMAP_LOW materials; never game code.

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
Bump reconstruction retains A->binormal/G->tangent and RSQ's absolute source.
The q=0 RSQ/RCP boundary, zero mixed normal/view and nonfinite geometry are
outside its analytical domain; GPU fixtures separately qualify these original
instructions. Nonzero near-zero q has no invented epsilon or fallback normal,
but float64 results do not establish legacy partial-precision boundary behavior.
BUMPMAP_LOW instead uses sampled XYZ signed data, without reconstructing Z.
Application lighting strengths are finite nonnegative scalar inputs, and their
power is finite positive. This analytical domain is not a runtime clamp or a
claim about undefined/nonfinite original POW behavior; zero/negative powers
require separate GPU qualification. Coefficients are never color-decoded.
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
    diffuse_coefficient: float = DIFFUSE_COEFFICIENT
    specular_power: int = 5
    cube_coefficient: float = 1.0
    bump_map: bool = False
    application_coefficients: bool = False
    normal_encoding: str = "ag"


@dataclass(frozen=True)
class LightingCoefficients:
    """Original standard-lighting PS scalar inputs and authored CTAB defaults.

    These are application material values, independent of our bounded Gains.
    No upper clamp is applied; float64 arithmetic/finite geometry scope remains.
    """
    diffuse: float = 1.0
    specular: float = 1.0
    reflection: float = 1.0
    power: float = 10.0

    def __post_init__(self):
        for name in ("diffuse", "specular", "reflection", "power"):
            value = _real(getattr(self, name), name)
            if not math.isfinite(value) or value < 0.0 or (name == "power" and value == 0.0):
                raise ValueError(name + " must be finite and " + ("positive" if name == "power" else "nonnegative"))
            object.__setattr__(self, name, value)


# Original game-program identities describe derived contracts, not raw code.
PROFILES = {
    "8759c7838bbc86c2": PixelProfile(2, True, False),
    "63f96eba9eea7880": PixelProfile(2, True, True),
    "593e5dea9b3457d5": PixelProfile(1, False, False),
    "7a0bb00a8070496a": PixelProfile(1, True, True),
    "8d5b2ba0fb4d13bf": PixelProfile(1, True, False),
    "dab93928f26906f7": PixelProfile(1, False, True),
    # Shared Khaak/Teladi/Teladi_nodiff/Xenon DEFAULT: offline proof only.
    "3b94320087e81945": PixelProfile(2, True, False, 0.5, 6, 0.5),
    "e3b7acc16da9932d": PixelProfile(2, True, True, 0.5, 6, 0.5),
    "7a14d4dcb28f27e5": PixelProfile(1, True, False, 0.5, 6, 0.5),
    "8ab6188a40ca15ea": PixelProfile(1, True, True, 0.5, 6, 0.5),
    "8df6143d0e77d92e": PixelProfile(1, False, False, 0.5, 6, 0.5),
    "e16a9806ee3544c3": PixelProfile(1, False, True, 0.5, 6, 0.5),
    "ca6bfa4a6cca7e2a": PixelProfile(2, True, False, bump_map=True),
    "5e0a10fe752b6140": PixelProfile(2, True, True, bump_map=True),
    "63379470db8d2a86": PixelProfile(1, True, False, bump_map=True),
    "68915563dd0aac9a": PixelProfile(1, False, False, bump_map=True),
    "d086fde54698070c": PixelProfile(1, True, True, bump_map=True),
    "f17fffd88d134b04": PixelProfile(1, False, True, bump_map=True),
    "462342e3e5781384": PixelProfile(2, True, False, 0.5, 10, 1.0),
    "827d8d2d617bedce": PixelProfile(2, True, True, 0.5, 10, 1.0),
    "02606104fa59fb29": PixelProfile(1, True, False, 0.5, 10, 1.0),
    "1d638938d93421b3": PixelProfile(1, True, True, 0.5, 10, 1.0),
    "bd4d51c08486c6e0": PixelProfile(1, False, False, 0.5, 10, 1.0),
    "de2dd381fa64193d": PixelProfile(1, False, True, 0.5, 10, 1.0),
    "7c83ed50c9894e44": PixelProfile(2, True, False, application_coefficients=True),
    "e70adc744a38ca59": PixelProfile(2, True, True, application_coefficients=True),
    "db644b73b68c0547": PixelProfile(1, True, False, application_coefficients=True),
    "ff32b602a271c327": PixelProfile(1, True, True, application_coefficients=True),
    "f6a501717c3e5ca8": PixelProfile(1, False, False, application_coefficients=True),
    "55826dc176afe464": PixelProfile(1, False, True, application_coefficients=True),
    "0c1f3f0f440e4a0c": PixelProfile(2, True, False, bump_map=True, application_coefficients=True),
    "64bac8bb307eb896": PixelProfile(2, True, True, bump_map=True, application_coefficients=True),
    "789449ffd931d23e": PixelProfile(1, True, False, bump_map=True, application_coefficients=True),
    "4f052209611387f0": PixelProfile(1, True, True, bump_map=True, application_coefficients=True),
    "abf3c0fad53456d8": PixelProfile(1, False, False, bump_map=True, application_coefficients=True),
    "cf449bcb069aec4f": PixelProfile(1, False, True, bump_map=True, application_coefficients=True),
    "99153c144030c396": PixelProfile(2, True, False, bump_map=True, application_coefficients=True, normal_encoding="xyz"),
    "c1452981fd0bff64": PixelProfile(2, True, True, bump_map=True, application_coefficients=True, normal_encoding="xyz"),
    "b0f9313b77cc78ee": PixelProfile(1, True, False, bump_map=True, application_coefficients=True, normal_encoding="xyz"),
    "d514bf852d8a9c58": PixelProfile(1, True, True, bump_map=True, application_coefficients=True, normal_encoding="xyz"),
    "dff6a3d360603fa2": PixelProfile(1, False, False, bump_map=True, application_coefficients=True, normal_encoding="xyz"),
    "f1d14a7dbf7c6173": PixelProfile(1, False, True, bump_map=True, application_coefficients=True, normal_encoding="xyz"),
    "1f26d41bcb7dac1e": PixelProfile(2, True, False, 0.5, 6, 0.5, bump_map=True),
    "bdcdb3ab996ae4e0": PixelProfile(2, True, True, 0.5, 6, 0.5, bump_map=True),
    "78963cdc7c710e04": PixelProfile(1, True, False, 0.5, 6, 0.5, bump_map=True),
    "1ed1bf0fdec00e1a": PixelProfile(1, True, True, 0.5, 6, 0.5, bump_map=True),
    "2b04461d0dae038b": PixelProfile(1, False, False, 0.5, 6, 0.5, bump_map=True),
    "acc83ed2509d84a1": PixelProfile(1, False, True, 0.5, 6, 0.5, bump_map=True),
    "3006f8030a467739": PixelProfile(2, True, False, 0.5, 10, 1.0, bump_map=True),
    "d6e8bdde0e4c515f": PixelProfile(2, True, True, 0.5, 10, 1.0, bump_map=True),
    "e5ea78b8b0b0fe07": PixelProfile(1, True, False, 0.5, 10, 1.0, bump_map=True),
    "f42202faf57a3c89": PixelProfile(1, True, True, 0.5, 10, 1.0, bump_map=True),
    "769c3814fc0efba8": PixelProfile(1, False, False, 0.5, 10, 1.0, bump_map=True),
    "22cc5b05a55ef61e": PixelProfile(1, False, True, 0.5, 10, 1.0, bump_map=True),
    "ef2bf556f207b8bd": PixelProfile(2, True, False, 1.0, 5, 1.0),
    "91b6c09eb47f8555": PixelProfile(2, True, True, 1.0, 5, 1.0),
    "cc09f17db377fd9e": PixelProfile(1, True, False, 1.0, 5, 1.0),
    "3755809bd40afc13": PixelProfile(1, True, True, 1.0, 5, 1.0),
    "61418505e5d8f998": PixelProfile(1, False, False, 1.0, 5, 1.0),
    "b5f1d4145171026b": PixelProfile(1, False, True, 1.0, 5, 1.0),
    "3602b05ce11ca6ff": PixelProfile(2, True, False, 1.0, 5, 1.0, bump_map=True),
    "8e58ac79b59b02b1": PixelProfile(2, True, True, 1.0, 5, 1.0, bump_map=True),
    "042c9ae16f41feff": PixelProfile(1, True, False, 1.0, 5, 1.0, bump_map=True),
    "68f0dd6791fd7d3d": PixelProfile(1, True, True, 1.0, 5, 1.0, bump_map=True),
    "5c823b8507fa1442": PixelProfile(1, False, False, 1.0, 5, 1.0, bump_map=True),
    "a6e1328c0bb3f401": PixelProfile(1, False, True, 1.0, 5, 1.0, bump_map=True),
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


@dataclass(frozen=True)
class BumpGeometry:
    normal: Vec3
    view: Vec3
    reflection: Vec3


def bump_geometry(normal_sample, tangent, binormal, geometric_normal, view, *,
                  two_sided=False, face=1.0, half_source=False, normal_encoding="ag") -> BumpGeometry:
    """Derive the BUMPMAP PS geometry from sampled RGBA and interpolated vectors.

    Inputs are already transformed/interpolated; do not normalize the basis.
    Quantization covers input endpoints only, not intermediate _pp arithmetic.
    The result has already been normalized and, where required, face-flipped.
    AG q=0 is reserved for independent GPU RSQ/RCP qualification, not redefined.
    XYZ uses X->binormal, G->tangent, Z->geometric normal; alpha is unused.
    """
    if not isinstance(two_sided, bool) or not isinstance(half_source, bool):
        raise TypeError("geometry switches must be boolean")
    quantize = half if half_source else float
    sample = tuple(quantize(x) for x in _vector(normal_sample, 4, "normal_sample"))
    if normal_encoding not in ("ag", "xyz"):
        raise ValueError("normal_encoding must be ag or xyz")
    used = (sample[1], sample[3]) if normal_encoding == "ag" else sample[:3]
    if not all(math.isfinite(x) and 0.0 <= x <= 1.0 for x in used):
        raise ValueError("used normal channels must be normalized finite data samples")
    def basis(values, name):
        return _vector(tuple(quantize(x) for x in _vector(values, 3, name)), 3, name, True)
    tangent = basis(tangent, "tangent")
    binormal = basis(binormal, "binormal")
    geometric_normal = basis(geometric_normal, "geometric_normal")
    view = _unit(basis(view, "view"), "view")
    if normal_encoding == "ag":
        x, y = 2.0 * sample[3] - 1.0, 2.0 * sample[1] - 1.0
        q = 1.0 - x*x - y*y
        if q == 0.0:
            raise ValueError("q=0 requires separate GPU reciprocal-chain qualification")
        z = math.sqrt(abs(q))
    else:
        x, y, z = (2.0 * s - 1.0 for s in sample[:3])
    normal = _unit(tuple(y*t + x*b + z*n
                         for t, b, n in zip(tangent, binormal, geometric_normal)), "mixed normal")
    if two_sided:
        face = _real(face, "face")
        if not math.isfinite(face) or face == 0.0:
            raise ValueError("face must be finite and nonzero")
        if face < 0.0:
            normal = tuple(-n for n in normal)
    reflection = tuple(2.0 * _dot(view, normal) * n - v for n, v in zip(normal, view))
    return BumpGeometry(normal, view, reflection)


def pixel(profile: str, varying: VertexResult, diffuse, specular_mask,
          lightmap, cubemap, directions: Sequence[DirectionalLight], *,
          affine=IDENTITY_AFFINE, face=1.0, glow=0.0, gains=Gains(),
          half_source=False, half_target=False, normal_sample=None,
          tangent=None, binormal=None, coefficients=None) -> PixelResult:
    """Evaluate one PS sample using explicit sampled inputs and VS varyings.

    DEFAULT takes already sampled cube RGB; vertex().reflection exposes its
    lookup geometry. BUMPMAP requires cubemap(direction)->sampled RGB, supplied
    by the caller, so sampling depends on the actual per-pixel reflection.
    That callback models a cube function, not filtering or rasterization. BUMP
    also requires normal_sample, tangent and binormal. varying.normal/view are
    the original geometric VS inputs; varying.linear_rgb keeps geometric point
    response, independently of the bumped PS normal.
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
    if coefficients is not None and not isinstance(coefficients, LightingCoefficients):
        raise TypeError("coefficients must be LightingCoefficients")
    if not contract.application_coefficients and coefficients is not None:
        raise ValueError("fixed lighting profile does not consume application coefficients")
    coefficients = coefficients or LightingCoefficients()
    diffuse_strength = coefficients.diffuse if contract.application_coefficients else contract.diffuse_coefficient
    specular_strength = coefficients.specular if contract.application_coefficients else 3.0
    reflection_strength = coefficients.reflection if contract.application_coefficients else contract.cube_coefficient
    power = coefficients.power if contract.application_coefficients else contract.specular_power
    directions = tuple(directions)
    if len(directions) != contract.directions:
        raise ValueError("directional-light count does not match profile")
    if not all(isinstance(light, DirectionalLight) for light in directions):
        raise TypeError("directions must contain DirectionalLight values")
    quantize = half if half_source else float
    source = lambda values, size, name: tuple(quantize(v) for v in _vector(values, size, name))
    diffuse = source(diffuse, 4, "diffuse")
    lightmap = source(lightmap, 4, "lightmap")
    mask = quantize(_real(specular_mask, "specular_mask"))
    if not math.isfinite(mask) or not 0.0 <= mask <= 1.0:
        raise ValueError("specular_mask must be a normalized finite data sample")
    if contract.bump_map:
        if not callable(cubemap):
            raise TypeError("BUMPMAP cubemap must sample RGB from its direction argument")
        geometry = bump_geometry(normal_sample, tangent, binormal, varying.normal, varying.view,
                                 two_sided=contract.two_sided, face=face, half_source=half_source,
                                 normal_encoding=contract.normal_encoding)
        normal, view = geometry.normal, geometry.view
        cubemap = source(cubemap(geometry.reflection), 3, "cubemap sample")
    else:
        cubemap = source(cubemap, 3, "cubemap")
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
        highlight = _sat(_dot(view, reflected)) ** power
        lobe = diffuse_strength * cosine + specular_strength * mask * _sat(3.0 * cosine) * highlight
        for i in range(3):
            directional[i] += lobe * decode(light.color[i]) * gains.direct
    vertex_rgb = _vector(varying.linear_rgb, 3, "linear vertex RGB")
    radiance = tuple(sanitize(albedo[i] * (vertex_rgb[i] + directional[i])
                              + decode(cubemap[i]) * mask * albedo[i] * reflection_strength
                              + decode(lightmap[i]) * gains.lightmap_emissive)
                     for i in range(3))
    glow = _real(glow, "glow")
    alpha_source = quantize(_real(varying.alpha, "vertex alpha"))
    alpha = (glow * lightmap[3] + (1.0 - glow) * diffuse[3]) * alpha_source
    rgba = tuple(encode(x) for x in radiance) + (alpha,)
    return PixelResult(radiance, tuple(half(x) for x in rgba) if half_target else rgba)


@dataclass(frozen=True)
class AsteroidProfile:
    directions: int
    bump_map: bool


ASTEROID_PROFILES = {
    "517540ae6d5e5410": AsteroidProfile(2, False),
    "7a0c3388065bb08d": AsteroidProfile(1, False),
    "d44db87778a43b61": AsteroidProfile(2, True),
    "550c2a4d4d3ed70f": AsteroidProfile(1, True),
}


@dataclass(frozen=True)
class AsteroidWeights:
    """Actual PS base/detail scalar inputs, with the native default values.

    The original preshader normally supplies base=1-detail. Accept both inputs
    independently to model the uploaded constants, without forcing that relation
    or decoding/clamping strengths. Finite nonnegative weights are this oracle's
    analytical domain, not a new production admission or material-value policy.
    """
    base: float = 1.0
    detail: float = 0.0

    def __post_init__(self):
        for name in ("base", "detail"):
            value = _real(getattr(self, name), name)
            if not math.isfinite(value) or value < 0.0:
                raise ValueError(name + " weight must be finite and nonnegative")
            object.__setattr__(self, name, value)


def asteroid_pixel(profile, varying, base, detail, specular_mask, directions, *,
                   weights=AsteroidWeights(), gains=Gains(), half_source=False,
                   half_target=False, normal_sample=None, tangent=None, binormal=None) -> PixelResult:
    """Asteroid DEFAULT/BUMPMAP, from independently sampled base/detail RGBA.

    Sample UV packing/rasterization remains the fixture's responsibility. Decode
    each RGB sample before its native scalar multiply and add; the combined
    albedo multiplies geometric point/emissive plus directional radiance. There
    is no cube, lightmap, affine hue or VFACE operation. The lobe has unit diffuse
    and a cubic masked highlight with sat(3*NdotL), but no outer factor three.
    Detail alpha is unused; base alpha multiplies unchanged native vertex alpha.
    Shared AG normal helper preserves sqrt(abs(q)); q=0, zero normal/view and
    nonfinite geometry keep the same separate GPU qualification domain.
    A caller using vertex() supplies the same Gains to both stages.
    """
    if not isinstance(profile, str) or profile not in ASTEROID_PROFILES:
        raise ValueError("unknown asteroid pixel profile")
    if not isinstance(varying, VertexResult) or not isinstance(weights, AsteroidWeights) or not isinstance(gains, Gains):
        raise TypeError("expected VertexResult, AsteroidWeights and Gains")
    if not isinstance(half_source, bool) or not isinstance(half_target, bool):
        raise TypeError("quantization switches must be boolean")
    contract = ASTEROID_PROFILES[profile]
    directions = tuple(directions)
    if len(directions) != contract.directions:
        raise ValueError("directional-light count does not match asteroid profile")
    if not all(isinstance(light, DirectionalLight) for light in directions):
        raise TypeError("directions must contain DirectionalLight values")
    quantize = half if half_source else float
    source = lambda values, size, name: tuple(quantize(x) for x in _vector(values, size, name))
    base, detail = source(base, 4, "base"), source(detail, 4, "detail")
    mask = quantize(_real(specular_mask, "specular_mask"))
    if not math.isfinite(mask) or not 0.0 <= mask <= 1.0:
        raise ValueError("specular_mask must be a normalized finite data sample")
    if contract.bump_map:
        geometry = bump_geometry(normal_sample, tangent, binormal, varying.normal, varying.view,
                                 half_source=half_source)
        normal, view = geometry.normal, geometry.view
    else:
        normal = _unit(source(varying.normal, 3, "normal"), "normal")
        view = _unit(source(varying.view, 3, "view"), "view")
    directional = [0.0, 0.0, 0.0]
    for light in directions:
        cosine = _sat(_dot(normal, light.direction))
        reflected = tuple(2.0 * _dot(normal, light.direction) * n - l
                          for n, l in zip(normal, light.direction))
        highlight = _sat(_dot(view, reflected)) ** 3
        lobe = cosine + mask * _sat(3.0 * cosine) * highlight
        for i in range(3):
            directional[i] += lobe * decode(light.color[i]) * gains.direct
    vertex_rgb = _vector(varying.linear_rgb, 3, "linear vertex RGB")
    albedo = tuple(weights.base * decode(base[i]) + weights.detail * decode(detail[i]) for i in range(3))
    radiance = tuple(sanitize(albedo[i] * (vertex_rgb[i] + directional[i])) for i in range(3))
    alpha = base[3] * quantize(_real(varying.alpha, "vertex alpha"))
    rgba = tuple(encode(x) for x in radiance) + (alpha,)
    return PixelResult(radiance, tuple(half(x) for x in rgba) if half_target else rgba)


@dataclass(frozen=True)
class PaletteProfile:
    family: str
    directions: int
    affine_color: bool
    two_sided: bool
    bump_map: bool = False
    vertex_palette: bool = False


@dataclass(frozen=True)
class PaletteVertexProfile:
    family: str
    fixed_single: bool = False
    vertex_palette: bool = False


# Independently classified original program schedules; not transformer metadata.
PALETTE_PROFILES = {
    '39eb3c2258a516e1': PaletteProfile('boron',2,False,False),
    '57acf59d19c73791': PaletteProfile('boron',2,False,True),
    'f917d48ee826da1f': PaletteProfile('boron',1,False,False,False,True),
    '77a5b2d62fb3be48': PaletteProfile('boron',1,False,True,False,True),
    'a910daef935891ce': PaletteProfile('boron',2,False,False,True),
    '62c180abe017e239': PaletteProfile('boron',2,False,True,True),
    'ed44232013f67072': PaletteProfile('boron',1,False,False,True,True),
    'f286856c3f400377': PaletteProfile('boron',1,False,True,True,True),
    '9d27e7ba242f3831': PaletteProfile('paranid',1,True,False),
    'e1acf8a03850acaf': PaletteProfile('paranid',1,True,True),
    'f646f03be5a8708d': PaletteProfile('paranid',1,True,False),
    'ebf41e1ace7af45b': PaletteProfile('paranid',1,True,True),
    'c997a37560e266df': PaletteProfile('paranid',1,False,False),
    '675f9077d8fd21c4': PaletteProfile('paranid',1,False,True),
    '18d372968af4a480': PaletteProfile('paranid',1,True,False,True),
    '188c5ab9dbb98393': PaletteProfile('paranid',1,True,True,True),
    '7e5e41276b3d7514': PaletteProfile('paranid',1,True,False,True),
    '43c9405568d2226f': PaletteProfile('paranid',1,True,True,True),
    '5e056627e9ff3a8d': PaletteProfile('paranid',1,False,False,True),
    'fce465befff2f623': PaletteProfile('paranid',1,False,True,True),
}
PALETTE_VERTEX_PROFILES = {
    '29d7c575396ed280': PaletteVertexProfile('boron'),
    'a420a010b0271479': PaletteVertexProfile('boron',False,True),
    'ea3d15b287892410': PaletteVertexProfile('boron',True,True),
    '57392213f62fef19': PaletteVertexProfile('boron'),
    '5c17a381b149b3b9': PaletteVertexProfile('boron',False,True),
    'a804f173f693944a': PaletteVertexProfile('boron',True,True),
    '37e6956afd8b8d76': PaletteVertexProfile('paranid'),
    '2e0254dd999841c2': PaletteVertexProfile('paranid'),
    'a7cddf2c98d61117': PaletteVertexProfile('paranid',True),
    '33388c8897d428a5': PaletteVertexProfile('paranid'),
    'b4059ab6af8fc529': PaletteVertexProfile('paranid'),
    '2a560f246c90fa64': PaletteVertexProfile('paranid',True),
}


def _palette_code(values):
    # Actual DEF binary32 values, conveniently identified by byte-color numerators.
    return tuple(struct.unpack('<f',struct.pack('<f',n/255.0))[0] for n in values)


PALETTE_COLORS = {
    'boron': {name:_palette_code(rgb) for name,rgb in (
        ('x',(38,111,117)),('y',(190,103,25)),('z',(136,141,117)),
        ('view',(81,253,240)),('grazing',(151,187,74)),('reflection',(112,164,183)))},
    'paranid': {name:_palette_code(rgb) for name,rgb in (
        ('x',(137,151,177)),('y',(73,97,103)),('z',(110,59,25)),('grazing',(104,128,164)))},
}
# Authored immutable DEF policy: exact binary32 code and gamma exponent, then
# correctly rounded binary32 storage. Ordinary texture/light decode stays unchanged.
PALETTE_DECODE_EXPONENT = 2.200000047683716
PALETTE_LINEAR_COLORS = {
    family:{role:tuple(struct.unpack('<f',struct.pack('<f',v**PALETTE_DECODE_EXPONENT))[0]
                       for v in rgb) for role,rgb in colors.items()}
    for family,colors in PALETTE_COLORS.items()
}
PALETTE_VIEW_POWER = 1.2000000476837158
PALETTE_VIEW_OFFSET = 0.10000000149011612


@dataclass(frozen=True)
class PaletteVertexResult(VertexResult):
    """Native interpolants; callers may supply separately interpolated fields.

    normal remains geometric; view was normalized at each vertex. reflection is
    the DEFAULT geometric reflection, NOT recomputed from interpolated N/V.
    J, u^11 and palette weights are independently interpolated native scalars.
    vertex_palette_rgb is already decoded/weighted Boron single-VS color.
    """
    palette_weights: Vec3
    reflection_weight: float
    view_weight: float
    vertex_palette_rgb: Optional[Vec3]


def palette_vertex(profile, world_position, world_normal, camera_position, material_emissive,
                   lights: Sequence[PointLight] = (), *, material_alpha=1.0,
                   fog_clip=None, gains=Gains()) -> PaletteVertexResult:
    """Boron/Paranid vertex math, from world inputs; no rasterizer emulation.

    Reuse point/fog math but normalize V before native reflection/palette terms.
    Float64 evaluates the ideal u^11 result for native POW or LOG/EXP schedules;
    intermediate precision/zero LOG behavior requires the retained GPU originals.
    Fixed-single contracts still require exactly one supplied point light.
    """
    if not isinstance(profile,str) or profile not in PALETTE_VERTEX_PROFILES:
        raise ValueError('unknown palette vertex profile')
    contract=PALETTE_VERTEX_PROFILES[profile]
    native=vertex(world_position,world_normal,camera_position,material_emissive,lights,
                  fixed_single=contract.fixed_single,material_alpha=material_alpha,
                  fog_clip=fog_clip,gains=gains)
    view=_unit(native.view,'view')
    reflection=tuple(2*_dot(view,native.normal)*n-v for n,v in zip(native.normal,view))
    reflection=_vector(reflection,3,'geometric reflection',True)
    weights=tuple(abs(v) for v in reflection)
    u=_sat(_dot(view,native.normal))
    j=(1-u)**PALETTE_VIEW_POWER+PALETTE_VIEW_OFFSET
    view_weight=u**11 if contract.family=='boron' else 0.0
    if contract.family=='boron': j+=j
    palette=None
    if contract.vertex_palette:
        colors=PALETTE_LINEAR_COLORS['boron']
        palette=tuple(weights[1]*colors['y'][i]+weights[0]*colors['x'][i]+
                      weights[2]*colors['z'][i]+view_weight*colors['view'][i]
                      for i in range(3))
    return PaletteVertexResult(native.normal,view,reflection,native.linear_rgb,native.alpha,
                               weights,j,view_weight,palette)


def palette_pixel(profile, varying, diffuse, specular_mask, lightmap, cubemap,
                  directions: Sequence[DirectionalLight], *, affine=IDENTITY_AFFINE,
                  face=1.0, glow=0.0, gains=Gains(), half_source=False, half_target=False,
                  normal_sample=None, tangent=None, binormal=None) -> PixelResult:
    """Full native palette terms with independent color conversion boundaries.

    Both techniques require cubemap(direction)->RGB. DEFAULT consumes the
    actual interpolated reflection; BUMP derives it from the bumped PS normal.
    Neither recomputes the geometric VS weights/J from the PS normal or view.
    Half-source covers native sampled/data endpoints; new linear color varyings
    and converted DEF colors retain full precision. It is not an intermediate
    _pp/centroid/flat/wrap simulator. Nonfinite geometry or overflowing palette
    angular intermediates lie outside the float64 analytical domain.
    """
    if not isinstance(profile,str) or profile not in PALETTE_PROFILES:
        raise ValueError('unknown palette pixel profile')
    if not isinstance(varying,PaletteVertexResult) or not isinstance(gains,Gains):
        raise TypeError('expected PaletteVertexResult and Gains')
    if not isinstance(half_source,bool) or not isinstance(half_target,bool):
        raise TypeError('quantization switches must be boolean')
    if not callable(cubemap):
        raise TypeError('palette cubemap must sample RGB from its direction argument')
    contract=PALETTE_PROFILES[profile]
    if contract.vertex_palette != (varying.vertex_palette_rgb is not None):
        raise ValueError('palette producer does not match the PS contract')
    directions=tuple(directions)
    if len(directions)!=contract.directions:
        raise ValueError('directional-light count does not match palette profile')
    if not all(isinstance(light,DirectionalLight) for light in directions):
        raise TypeError('directions must contain DirectionalLight values')
    quantize=half if half_source else float
    source=lambda values,size,name:tuple(quantize(v) for v in _vector(values,size,name))
    diffuse=source(diffuse,4,'diffuse'); lightmap=source(lightmap,4,'lightmap')
    mask=quantize(_real(specular_mask,'specular_mask'))
    if not math.isfinite(mask) or not 0<=mask<=1:
        raise ValueError('specular_mask must be normalized finite data')
    if contract.bump_map:
        geometry=bump_geometry(normal_sample,tangent,binormal,varying.normal,varying.view,
                               two_sided=contract.two_sided,face=face,half_source=half_source)
        normal,view,reflection=geometry.normal,geometry.view,geometry.reflection
    else:
        normal=_unit(source(varying.normal,3,'normal'),'normal')
        view=_unit(source(varying.view,3,'view'),'view')
        reflection=_vector(source(varying.reflection,3,'reflection'),3,'reflection',True)
        if contract.two_sided:
            face=_real(face,'face')
            if not math.isfinite(face) or face==0:
                raise ValueError('face must be finite and nonzero')
            if face<0:normal=tuple(-n for n in normal)
    _unit(reflection,'cube direction')
    cube=source(cubemap(reflection),3,'cube sample')
    albedo_code=diffuse[:3]
    if contract.affine_color:
        rows=tuple(_vector(row,4,'affine row',True) for row in affine)
        if len(rows)!=3:raise ValueError('affine must contain three rows')
        albedo_code=tuple(_dot(row,(*albedo_code,1.0)) for row in rows)
    albedo=tuple(decode(v) for v in albedo_code)
    colors=PALETTE_LINEAR_COLORS[contract.family]
    if contract.vertex_palette:
        palette=_vector(varying.vertex_palette_rgb,3,'linear vertex palette',True)
    else:
        w=_vector(source(varying.palette_weights,3,'palette weights'),3,'palette weights',True)
        palette=tuple(w[1]*colors['y'][i]+w[0]*colors['x'][i]+
                      w[2]*colors['z'][i] for i in range(3))
        if contract.family=='boron':
            v=quantize(_real(varying.view_weight,'view weight'))
            if not math.isfinite(v):raise ValueError('view weight must be finite')
            palette=tuple(p+v*colors['view'][i] for i,p in enumerate(palette))
    grazing=1-abs(_dot(view,reflection))
    # Native Boron multiplication keeps the odd-power sign. Native Paranid POW
    # takes abs(src0), per https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/pow---ps.
    try:
        if contract.family=='boron':
            squared=grazing*grazing
            grazing=grazing*(squared*squared)
        else:grazing=abs(grazing)**9
    except OverflowError as exc:
        raise ValueError('palette angular term exceeds float64 domain') from exc
    if not math.isfinite(grazing):raise ValueError('palette angular term must be finite')
    palette=tuple(p+grazing*colors['grazing'][i] for i,p in enumerate(palette))
    effective=tuple(.5*d+.5*(d*p) for d,p in zip(albedo,palette))
    diffuse_coefficient=DIFFUSE_COEFFICIENT if contract.family=='boron' else .5
    specular_scale=3.0 if contract.family=='boron' else 6.0
    directional=[0.0,0.0,0.0]
    for light in directions:
        cosine=_sat(_dot(normal,light.direction))
        reflected=tuple(2*_dot(normal,light.direction)*n-l for n,l in zip(normal,light.direction))
        highlight=_sat(_dot(view,reflected))**10
        lobe=diffuse_coefficient*cosine+specular_scale*mask*_sat(3*cosine)*highlight
        for i in range(3):directional[i]+=lobe*decode(light.color[i])*gains.direct
    j=quantize(_real(varying.reflection_weight,'reflection weight'))
    if not math.isfinite(j):raise ValueError('reflection weight must be finite')
    reflection_rgb=tuple(decode(c)*mask*d*j for c,d in zip(cube,albedo))
    if contract.family=='boron':
        reflection_rgb=tuple(r*colors['reflection'][i] for i,r in enumerate(reflection_rgb))
    vertex_rgb=_vector(varying.linear_rgb,3,'linear vertex RGB')
    radiance=tuple(sanitize(a*(p+l)+r+decode(m)*gains.lightmap_emissive)
                   for a,p,l,r,m in zip(effective,vertex_rgb,directional,reflection_rgb,lightmap))
    glow=_real(glow,'glow')
    if not math.isfinite(glow):raise ValueError('glow must be finite')
    alpha=(glow*lightmap[3]+(1-glow)*diffuse[3])*quantize(_real(varying.alpha,'vertex alpha'))
    rgba=tuple(encode(v) for v in radiance)+(alpha,)
    return PixelResult(radiance,tuple(half(v) for v in rgba) if half_target else rgba)
