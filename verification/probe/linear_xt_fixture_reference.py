"""XT additions to the detached material fixture's stable 240-byte case ABI.

Geometry uses the existing independent point-light equations and the XT finite
pixel equations. This does not simulate PP arithmetic, filtering or rasterization.
The original DEFAULT producer is invalid: its ordinary reference is explicitly
the authored repaired pair. Raw shader programs remain external fixture inputs.
"""
from dataclasses import replace
from pathlib import Path
import copy
import math
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from verification.analysis import xt_material_reference as xt
import linear_material_reference as base

START = 148
PAIRS = [('37c34a7478544c14' if c.bump else '494fe349b8bc12ec', p)
         for p, c in xt.CONTRACTS.items()]
PALETTE, DECAL, ALT_PALETTE, ALT_DATA, FLAT = 128, 256, 512, 1024, 2048
THRESHOLD_MID, THRESHOLD_HIGH = 4096, 8192
SECONDARY_ALT, FLOAT2, CLIP = 16384, 32768, 65536
GRADIENT, UV_PATTERN, PERSPECTIVE = 16, 32, 64
FAMILIES = ('XT standard DEFAULT', 'XT standard BUMP', 'XT standard LOW',
            'XT damage BUMP', 'XT terraformer DEFAULT', 'XT terraformer BUMP',
            'XT terraformer LOW')


def family(pair):
    return FAMILIES[(pair - START) // 2]


def append_cases(cases):
    seed = copy.deepcopy(cases[0])
    seed.update(flags=0, affine=0, fp16=1, coefficients=[.8, .7, .6, 6.],
                normal_sample=[.25, .5, .75, .5], normal=[0., 0., 1.],
                camera=[.6, .3, 4.], mask=.25, lights=1)
    def add(label, pair, **changes):
        c = copy.deepcopy(seed)
        c.update(changes, pair=pair, id=len(cases), label='xt_' + label)
        cases.append(c)
    for pair in range(START, START + len(PAIRS)):
        contract = xt.CONTRACTS[PAIRS[pair - START][1]]
        for depth in (0, 1):
            for reverse in (0, 1):
                add('pair_depth_face', pair, depth=depth, reverse=reverse)
        for flags in (PALETTE, PALETTE | ALT_PALETTE):
            add('runtime_palette', pair, flags=flags)
        if contract.family != 'terraformer':
            for flags in (DECAL, DECAL | PALETTE):
                add('decal_palette', pair, flags=flags)
        for gains in ([0., 0., 0.], [2., .5, 4.], [.5, 4., 2.], [16., 16., 16.]):
            add('independent_gains', pair, gains=gains, flags=PALETTE)
        for lights in (0, 8):
            add('point_count', pair, lights=lights, flags=PALETTE)
        add('missing_history', pair, valid=0)
        add('fog_alpha', pair, flags=2 | PALETTE, camera=[0.,0.,4.])
        add('affine', pair, affine=1, flags=PALETTE | DECAL if contract.family != 'terraformer' else PALETTE)
        add('sample_data', pair, flags=ALT_DATA | PALETTE)
        for flags in (UV_PATTERN, UV_PATTERN | SECONDARY_ALT):
            add('distinct_uv', pair, flags=flags)
        if contract.damaged_normal:
            for flags in (0, THRESHOLD_MID, THRESHOLD_HIGH):
                add('damage_threshold', pair, flags=flags | DECAL)
        if not contract.bump:
            add('expanded_float2', pair, flags=UV_PATTERN | FLOAT2)
        for flags in (GRADIENT | PALETTE, GRADIENT | PERSPECTIVE | PALETTE,
                      GRADIENT | PERSPECTIVE | CLIP | PALETTE):
            for reverse in (0, 1):
                add('interpolation', pair, flags=flags, reverse=reverse, fp16=0)
        if pair in (148, 151, 154, 157):
            for reverse in (0, 1):
                add('flat_interpolation', pair, flags=GRADIENT | PALETTE | FLAT,
                    reverse=reverse, fp16=0)
        if pair in (148, 151, 154, 157):
            add('cube_direction', pair, flags=1, camera=[0.,0.,4.], fp16=0)
    return cases


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def _unit(v):
    length = math.hypot(*v)
    if not length:
        raise ValueError('zero fixture vector')
    return tuple(x / length for x in v)


def _dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def varyings(c, sample=(8, 8), *, linear=True, flat_color=False):
    """Interpolate each native producer value; never recompute vertex weights in PS."""
    contract = xt.CONTRACTS[PAIRS[c['pair'] - START][1]]
    vector = lambda key: tuple(map(f32, c[key]))
    lights = [base.PointLight((0., 0., 2.), vector('point'), (2., .25, .125))] * c['lights']
    gains = base.Gains(*vector('gains'))
    def at(x, y):
        normal = vector('normal')
        position = (0., 0., 0.)
        if c['flags'] & GRADIENT:
            position = (f32(.125 * x), f32(.0625 * y), 0.)
            normal = (f32(normal[0] + f32(.0625 * x)),
                      f32(normal[1] + f32(.03125 * y)), normal[2])
        v = base.vertex(position, normal, vector('camera'), vector('material'), lights,
                        material_alpha=.625, gains=gains,
                        fog_clip=vector('fog_clip') if c['flags'] & 2 else None)
        native_rgb = list(vector('material'))
        for light in lights:
            delta = tuple(a-b for a,b in zip(light.position, position))
            distance = math.hypot(*delta)
            response = min(1., max(0., _dot(normal, _unit(delta))))
            response *= min(1., 1. / (2. + .25*distance + .125*distance*distance))
            native_rgb = [a + response*b for a,b in zip(native_rgb, light.color)]
        view = _unit(v.view)
        if contract.bump:
            q = min(1., max(0., _dot(view, normal)))
            reflected = tuple(2.*_dot(view, normal)*n-a for n,a in zip(normal, view))
            weights, highlight = tuple(map(abs, reflected)), q**12
            shape = (1.-q)**2
            fresnel = f32(.1) + f32(.9)*shape*f32(c['coefficients'][2])
        else:
            authored = xt.default_authored_varyings(v.view, normal, f32(c['coefficients'][2]))
            weights, highlight, fresnel = authored.palette_weights, authored.highlight_weight, authored.fresnel
            shape = authored.fresnel_shape
            fresnel = f32(.1) + f32(.9)*shape*f32(c['coefficients'][2])
            # DEFAULT exports raw world view; its PS normalizes after interpolation.
            view = v.view
        return dict(normal=normal, view=view, weights=weights, highlight=highlight,
                    fresnel=fresnel, shape=shape, rgb=v.linear_rgb if linear else tuple(native_rgb), alpha=v.alpha)
    if not c['flags'] & GRADIENT:
        return at(0., 0.)
    ws = (1., 2., 4.) if c['flags'] & PERSPECTIVE else (1., 1., 1.)
    vertices = [at(x*w, y*w) for (x,y),w in zip(((-1.,1.),(3.,1.),(-1.,-3.)),ws)]
    bx, by = sample[0]/32., sample[1]/32.
    weights = [(b/w) for b,w in zip((1.-bx-by,bx,by),ws)]
    weights = [w/sum(weights) for w in weights]
    result = {}
    for key, value in vertices[0].items():
        if key == 'alpha': result[key] = value
        elif key == 'rgb' and flat_color: result[key] = value
        elif isinstance(value, tuple):
            result[key] = tuple(sum(w*v[key][i] for w,v in zip(weights,vertices)) for i in range(len(value)))
        else: result[key] = sum(w*v[key] for w,v in zip(weights,vertices))
    return result


def inputs(c, sample=(8, 8), *, linear=True, half_source=False, flat_color=False,
           cube_sampler=None):
    contract = xt.CONTRACTS[PAIRS[c['pair'] - START][1]]
    varying = varyings(c, sample, linear=linear, flat_color=flat_color)
    q = base.half if half_source else float
    vec = lambda key: tuple(map(f32, c[key]))
    sampled = lambda value: tuple(q(f32(x)) for x in value)
    ws = (1., 2., 4.) if c['flags'] & PERSPECTIVE else (1., 1., 1.)
    bx, by = sample[0]/32., sample[1]/32.
    denominator = (1.-bx-by)/ws[0] + bx/ws[1] + by/ws[2]
    uv = (.125, .625) if c['flags'] & UV_PATTERN else (bx/ws[1]/denominator, by/ws[2]/denominator)
    secondary = ((0., 1.) if c['flags'] & FLOAT2 else
                 (.625, .375) if c['flags'] & SECONDARY_ALT else (.375, .625))
    texel = lambda coordinate: tuple(min(3, max(0, int(q(x)*4))) for x in coordinate)
    ox, oy = texel(secondary)
    dx, dy = texel(tuple(q(x)*f32(1.7) for x in uv))
    alternate = bool(c['flags'] & ALT_DATA)
    threshold = .51 if c['flags'] & THRESHOLD_HIGH else .5 if c['flags'] & THRESHOLD_MID else .49
    occlusion = sampled((threshold, .35, f32(.2 if alternate else .6)+ox/32.,
                         f32(.4 if alternate else .8)-oy/32.))
    detail = sampled((f32(.65 if alternate else .55)+dx/64.,
                      f32(.35 if alternate else .45)+dy/64.,
                      .8 if alternate else .2, .6 if alternate else .3))
    diffuse = sampled(c['diffuse'])
    if c['flags'] & UV_PATTERN:
        x, y = texel(uv)
        diffuse = sampled(((x+1)/8., (y+1)/8., (x+y+1)/16., c['diffuse'][3]))
    colors = ((.18,.75,.33),(.83,.24,.58),(.41,.62,.16),(.90,.12,.47),(.27,.55,.88))
    if c['flags'] & ALT_PALETTE:
        colors = tuple((g,b,r) for r,g,b in colors)
    colors = tuple(tuple(map(f32, color)) for color in colors)
    palette = xt.Palette(color1=colors[0], color2=colors[1], color3=colors[2],
                         lines=colors[3], highlight=colors[4],
                         weights=tuple(q(x) for x in varying['weights']),
                         highlight_weight=q(varying['highlight']), weighting=f32(.65),
                         lines_power=f32(2.3))
    diffuse_strength, specular_strength, reflection_strength, power = vec('coefficients')
    return xt.Inputs(diffuse=diffuse, specular=sampled((c['mask'],.08,.91,.64)),
        lightmap=sampled(c['lightmap']), occlusion=occlusion,
        bump_sample=sampled(c['normal_sample']), detail=detail,
        geometric_normal=tuple(q(x) for x in varying['normal']),
        tangent_y=tuple(q(x) for x in vec('tangent')),
        tangent_x=tuple(q(x) for x in vec('binormal')),
        view=tuple(q(x) for x in varying['view']),
        lights=(xt.Light((0.,0.,1.),vec('dir0')),xt.Light((0.,0.,-1.),vec('dir1'))),
        vertex_native_rgb=varying['rgb'], vertex_linear_rgb=varying['rgb'],
        vertex_alpha=varying['alpha'], palette=palette,
        decal=bool(c['flags'] & DECAL) and contract.family != 'terraformer',
        palette_enabled=bool(c['flags'] & PALETTE), face=-1. if c['reverse'] else 1.,
        diffuse_strength=diffuse_strength, specular_strength=specular_strength,
        specular_power=power, reflection_strength=reflection_strength,
        fresnel=q(varying['fresnel']) if contract.bump else
            f32(.1)+f32(.9)*q(varying['shape'])*reflection_strength,
        occlusion_strength=f32(1.4), glow=f32(c['glow']),
        bump_strength=f32(.55), detail_strength=f32(.18),
        affine=tuple(tuple(map(f32,row)) for row in
                     (((.75,.125,0.,.0625),(0.,.5,.25,.03125),(.125,0.,.875,-.03125))
                      if c['affine'] else xt.IDENTITY_AFFINE)),
        cube=(lambda direction: sampled(cube_sampler(direction))) if c['flags'] & 1 else sampled(c['cube'])[:3],
        direct_gain=f32(c['gains'][0]), lightmap_gain=f32(c['gains'][2]))


def expected(c, half_source=False, sample=(8,8), *, linear=True,
             flat_color=False, cube_sampler=None):
    values = inputs(c, sample, linear=linear, half_source=half_source,
                    flat_color=flat_color, cube_sampler=cube_sampler)
    contract = PAIRS[c['pair'] - START][1]
    result = (xt.linear_pixel if linear else xt.native_pixel)(contract, values)
    rgba = tuple(base.half(x) for x in result.output_rgba) if c['fp16'] else result.output_rgba
    return base.PixelResult(result.working_rgb, rgba)
