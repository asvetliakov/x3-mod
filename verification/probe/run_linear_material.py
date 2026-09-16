#!/usr/bin/env python3
"""Detached GPU qualification; consumes an explicitly built EXE, never rebuilds.

Invoke under wine_lock.py with X3M_FIXTURE_BOTTLE=X3. Raw output/cases stay in
/tmp. The compact results record contains oracle tolerances and scoped coverage.
"""
from pathlib import Path
import argparse
from collections import Counter
import copy
import hashlib
import json
import math
import os
import re
import statistics
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bottle
from game_guard import game_running
import linear_material_reference as ref
import linear_xt_fixture_reference as xt_fixture
import linear_glass_fixture_reference as glass_fixture

ROOT = Path(__file__).resolve().parents[2]
PROGRAMS = Path('/tmp/x3-shader-sweep/programs')
EXE = ROOT / 'verification/probe/build/linear_material_fixture.exe'
CODE_INPUTS = ('src/renderer/quad_vertex_program.h', 'src/renderer/quad_vertex_program_inc.h', 'verification/probe/sun_share_material_inc.h', 'src/renderer/linear_material.cpp', 'src/renderer/linear_material.h',
               'src/renderer/linear_sun_share_inc.h', 'src/renderer/linear_xt_material_inc.h', 'src/renderer/linear_xt_profiles_inc.h',
               'src/renderer/material_motion.cpp', 'src/renderer/material_motion.h',
               'src/renderer/motion_output_profiles.h',
               'src/renderer/motion_output_profiles_inc.h',
               'docs/reverse-engineering/linear-material-profiles.json',
               'docs/reverse-engineering/xt-material-profiles.json',
               'verification/probe/linear_material_fixture.cpp',
               'verification/probe/linear_alpha_test_fixture_inc.h',
               'verification/probe/linear_material_reference.py',
               'verification/probe/linear_xt_fixture_reference.py',
               'verification/probe/linear_glass_fixture_reference.py',
               'verification/analysis/xt_material_reference.py',
               'verification/probe/run_linear_material.py')
PAIRS = [('53a0a641107ed76c', ps) for ps in ('63f96eba9eea7880', '8759c7838bbc86c2')]
PAIRS += [(vs, ps) for vs in ('719856ce0c213220', 'badefd5143b3024f') for ps in
          ('593e5dea9b3457d5', '7a0bb00a8070496a', '8d5b2ba0fb4d13bf', 'dab93928f26906f7')]
PAIRS += [('53a0a641107ed76c', ps) for ps in ('3b94320087e81945', 'e3b7acc16da9932d')]
PAIRS += [(vs, ps) for vs in ('719856ce0c213220', 'badefd5143b3024f') for ps in
          ('7a14d4dcb28f27e5', '8ab6188a40ca15ea', '8df6143d0e77d92e', 'e16a9806ee3544c3')]
PAIRS += [('4944d81dfe531b37', ps) for ps in ('ca6bfa4a6cca7e2a', '5e0a10fe752b6140')]
PAIRS += [(vs, ps) for vs in ('44c4a41ca92ae2e3', '19a246a56e9d9700') for ps in
          ('63379470db8d2a86', '68915563dd0aac9a', 'd086fde54698070c', 'f17fffd88d134b04')]
PAIRS += [('53a0a641107ed76c', ps) for ps in ('462342e3e5781384', '827d8d2d617bedce')]
PAIRS += [(vs, ps) for vs in ('719856ce0c213220', 'badefd5143b3024f') for ps in
          ('02606104fa59fb29', '1d638938d93421b3', 'bd4d51c08486c6e0', 'de2dd381fa64193d')]
PAIRS += [('494fe349b8bc12ec', ps) for ps in ('7c83ed50c9894e44', 'e70adc744a38ca59')]
PAIRS += [(vs, ps) for vs in ('719856ce0c213220', 'badefd5143b3024f') for ps in
          ('db644b73b68c0547', 'ff32b602a271c327', 'f6a501717c3e5ca8', '55826dc176afe464')]
PAIRS += [('4944d81dfe531b37', ps) for ps in ('0c1f3f0f440e4a0c', '64bac8bb307eb896')]
PAIRS += [(vs, ps) for vs in ('44c4a41ca92ae2e3', '19a246a56e9d9700') for ps in
          ('789449ffd931d23e', '4f052209611387f0', 'abf3c0fad53456d8', 'cf449bcb069aec4f')]
PAIRS += [('4944d81dfe531b37', ps) for ps in ('99153c144030c396', 'c1452981fd0bff64')]
PAIRS += [(vs, ps) for vs in ('44c4a41ca92ae2e3', '19a246a56e9d9700') for ps in
          ('b0f9313b77cc78ee', 'd514bf852d8a9c58', 'dff6a3d360603fa2', 'f1d14a7dbf7c6173')]
PAIRS += [('4944d81dfe531b37', ps) for ps in ('1f26d41bcb7dac1e', 'bdcdb3ab996ae4e0')]
PAIRS += [(vs, ps) for vs in ('44c4a41ca92ae2e3', '19a246a56e9d9700') for ps in
          ('78963cdc7c710e04', '1ed1bf0fdec00e1a', '2b04461d0dae038b', 'acc83ed2509d84a1')]
PAIRS += [('4944d81dfe531b37', ps) for ps in ('3006f8030a467739', 'd6e8bdde0e4c515f')]
PAIRS += [(vs, ps) for vs in ('44c4a41ca92ae2e3', '19a246a56e9d9700') for ps in
          ('e5ea78b8b0b0fe07', 'f42202faf57a3c89', '769c3814fc0efba8', '22cc5b05a55ef61e')]
PAIRS += [('53a0a641107ed76c', ps) for ps in ('ef2bf556f207b8bd', '91b6c09eb47f8555')]
PAIRS += [(vs, ps) for vs in ('719856ce0c213220', 'badefd5143b3024f') for ps in
          ('cc09f17db377fd9e', '3755809bd40afc13', '61418505e5d8f998', 'b5f1d4145171026b')]
PAIRS += [('4944d81dfe531b37', ps) for ps in ('3602b05ce11ca6ff', '8e58ac79b59b02b1')]
PAIRS += [(vs, ps) for vs in ('44c4a41ca92ae2e3', '19a246a56e9d9700') for ps in
          ('042c9ae16f41feff', '68f0dd6791fd7d3d', '5c823b8507fa1442', 'a6e1328c0bb3f401')]
ASTEROID_PAIRS = (
    ('b0602757fce6e870','517540ae6d5e5410'),
    ('0c223ad11bce02d5','7a0c3388065bb08d'),
    ('233d17d26ce0c1fc','7a0c3388065bb08d'),
    ('167eb2d5629ab9d3','d44db87778a43b61'),
    ('330ceb9dd874ede2','550c2a4d4d3ed70f'),
    ('12b8a13f13fe8cfe','550c2a4d4d3ed70f'))
PAIRS += list(ASTEROID_PAIRS)
FIXED_VERTICES = {'badefd5143b3024f','19a246a56e9d9700',
                  '233d17d26ce0c1fc','12b8a13f13fe8cfe'}
PALETTE_PAIRS = [('29d7c575396ed280',p) for p in ('39eb3c2258a516e1','57acf59d19c73791')]
PALETTE_PAIRS += [(v,p) for v in ('a420a010b0271479','ea3d15b287892410') for p in ('f917d48ee826da1f','77a5b2d62fb3be48')]
PALETTE_PAIRS += [('57392213f62fef19',p) for p in ('a910daef935891ce','62c180abe017e239')]
PALETTE_PAIRS += [(v,p) for v in ('5c17a381b149b3b9','a804f173f693944a') for p in ('ed44232013f67072','f286856c3f400377')]
PALETTE_PAIRS += [('37e6956afd8b8d76',p) for p in ('9d27e7ba242f3831','e1acf8a03850acaf')]
PALETTE_PAIRS += [(v,p) for v in ('2e0254dd999841c2','a7cddf2c98d61117') for p in ('f646f03be5a8708d','ebf41e1ace7af45b','c997a37560e266df','675f9077d8fd21c4')]
PALETTE_PAIRS += [('33388c8897d428a5',p) for p in ('18d372968af4a480','188c5ab9dbb98393')]
PALETTE_PAIRS += [(v,p) for v in ('b4059ab6af8fc529','2a560f246c90fa64') for p in ('7e5e41276b3d7514','43c9405568d2226f','5e056627e9ff3a8d','fce465befff2f623')]
PAIRS += PALETTE_PAIRS
PAIRS += xt_fixture.PAIRS
PAIRS += glass_fixture.PAIRS
FIXED_VERTICES |= {'ea3d15b287892410','a804f173f693944a','a7cddf2c98d61117','2a560f246c90fa64'}
TIMING_PAIRS = (0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 113, 116, 122, 128, 138, 162)
FAMILIES = ('Argon', 'shared DEFAULT', 'Argon BUMP', 'Split DEFAULT',
            'standard DEFAULT', 'standard BUMP', 'standard LOW', 'shared BUMP',
            'Split BUMP', 'Terran DEFAULT', 'Terran BUMP', 'Asteroid DEFAULT', 'Asteroid BUMP',
            'Boron DEFAULT', 'Boron BUMP', 'Paranid DEFAULT', 'Paranid BUMP')
FAMILIES += xt_fixture.FAMILIES + ('Glass',)
CUBE_PATTERN, FOG, BOUNDARY, DETAIL_PATTERN = 1, 2, 4, 8
PALETTE_GRADIENT, DIFFUSE_PATTERN, PALETTE_PERSPECTIVE = 16, 32, 64
CUBE_BANDS_U = (.125, .25, .25, .5)
CUBE_BANDS_V = (.125, .375, .375, .75)


def family_name(pair):
    if pair >= 162:return 'Glass'
    if pair >= 148:return xt_fixture.family(pair)
    if pair < 110:return FAMILIES[pair // 10]
    if pair < 116:return FAMILIES[11 + (pair >= 113)]
    return FAMILIES[13 + (pair >= 122) + (pair >= 128) + (pair >= 138)]


def cube_location(direction):
    # D3D9 LookAtLH face basis, followed by viewport Y inversion:
    # https://learn.microsoft.com/en-us/windows/win32/direct3d9/creating-cubic-environment-map-surfaces
    x, y, z = direction
    magnitude = max(abs(x), abs(y), abs(z))
    if not math.isfinite(magnitude) or magnitude == 0:
        raise ValueError('analytical cube direction must be finite and nonzero')
    if abs(x) == magnitude:
        face, u, v = (0, -z, -y) if x > 0 else (1, z, -y)
    elif abs(y) == magnitude:
        face, u, v = (2, x, z) if y > 0 else (3, x, -z)
    else:
        face, u, v = (4, x, -y) if z > 0 else (5, -x, -y)
    return face, .5 * (u / magnitude + 1), .5 * (v / magnitude + 1)


def cube_sample(direction, require_margin=True):
    face, u, v = cube_location(direction)
    # The central 2x2 texels share a color, so exact axis directions do not
    # straddle a color discontinuity. Other samples stay clear of .25/.75.
    if require_margin:
        xyz = sorted(map(abs, direction))
        if xyz[1] / xyz[2] > .9 or min(abs(t-edge) for t in (u,v) for edge in (.25,.75)) < .025:
            raise ValueError('cube witness too close to face or color boundary')
    ix, iy = min(3, max(0, int(u * 4))), min(3, max(0, int(v * 4)))
    return ((face + 1) / 8., CUBE_BANDS_U[ix], CUBE_BANDS_V[iy])
AFFINE = ((.75, .125, 0, .0625), (0, .5, .25, .03125), (.125, 0, .875, -.03125))
# Retained _pp angular arithmetic may round intermediate lobes. This tolerance
# is independent of half-target storage and is not permission to clamp HDR.
RGB_REL_TOL = .006
RGB_ABS_TOL = 2e-5


def detail_sample(c):
    # Native Asteroid detail UV = 3*base UV, whether separate TEX1 or TEX0.zw.
    # Binary-exact constant UVs keep point samples away from texel boundaries.
    if not c['flags'] & DETAIL_PATTERN:
        return tuple(c['lightmap'])
    u,v = c['coefficients'][2:]
    ix,iy = (min(3,max(0,int(3*t*4))) for t in (u,v))
    return ((ix+1)/8.,(iy+1)/8.,(ix+iy+1)/16.,.875)


def fixture_cases():
    cases = []
    base = dict(pair=0, depth=1, lights=1, reverse=0, affine=0, valid=1, fp16=1,
                gains=[1., 1., 1.], diffuse=[.5, .25, .75, .75],
                lightmap=[.125, .25, .0625, .25], cube=[.25, .5, .125, 1.],
                mask=.25, material=[.25, .125, .0625], point=[.5, .25, .125],
                dir0=[.375, .25, .5], dir1=[.125, .5, .25], normal=[0., 0., 1.], glow=.25, flags=0,
                normal_sample=[.25, .5, .75, .5], binormal=[0., 1., 0.],
                tangent=[1., 0., 0.], camera=[0., 0., 4.], fog_clip=[.75, .125])

    def add(label, **kwargs):
        c = copy.deepcopy(base)
        c.update(copy.deepcopy(kwargs))
        c.update(id=len(cases), label=label)
        cases.append(c)

    for pair in range(10):
        for depth in (0, 1):
            for reverse in (0, 1):
                add('pair_depth_face', pair=pair, depth=depth, reverse=reverse)
    for pair in (0, 2, 6):
        for lights in ((0, 1, 8) if pair != 6 else (1,)):
            for gain in (1., 4., 16.):
                add('lights_gains', pair=pair, lights=lights, gains=[gain] * 3)
    for pair in range(6):
        add('affine_angular', pair=pair, affine=1, normal=[.6, 0., .8])
        for source in ('point', 'material', 'dir0', 'dir1', 'lightmap', 'cube'):
            isolated = {name: ([0.] * 3 if name not in ('lightmap', 'cube') else [0., 0., 0., 1.])
                        for name in ('point', 'material', 'dir0', 'dir1', 'lightmap', 'cube')}
            isolated[source] = base[source]
            add('isolated_' + source, pair=pair, **isolated)
    # Independent gains prevent a misplaced shared multiplier from passing.
    for gains in ([16., 1., 1.], [1., 16., 1.], [1., 1., 16.], [0., 0., 0.]):
        add('independent_gains', gains=gains)
    for amplitude in (0., 1., 4., 16., 256.):
        add('native_material_strength', pair=2, material=[amplitude, amplitude / 2, amplitude / 4],
            point=[0.] * 3, dir0=[0.] * 3, lightmap=[0., 0., 0., .25], cube=[0., 0., 0., 1.])
    # Isolate each converted input: NaN/Inf arithmetic in another legacy source
    # or affine 0*Inf is deliberately avoided so sanitizer behavior is observable.
    safety = (('black', 0.), ('minus_zero', -0.), ('negative', -1.), ('tiny', 1e-8),
              ('nan', math.nan), ('posinf', math.inf), ('neginf', -math.inf),
              ('cap', ref.CAP), ('overcap', 1e7))
    for source in ('diffuse', 'point', 'material', 'dir0', 'lightmap', 'cube'):
        for name, value in safety:
            isolated = dict(diffuse=[1., 1., 1., .75], point=[0.] * 3,
                            material=[0.] * 3, dir0=[0.] * 3, dir1=[0.] * 3,
                            lightmap=[0., 0., 0., .25], cube=[0., 0., 0., 1.], mask=1.)
            isolated[source] = [value] * 3 + (isolated[source][3:] if source in ('diffuse', 'lightmap', 'cube') else [])
            if source == 'diffuse': isolated['material'] = [1.] * 3
            add('safety_' + source + '_' + name, pair=2, fp16=0, **isolated)
    add('missing_history', valid=0)
    # Keep the first slice's 167 cases/IDs unchanged. The transfer implementation
    # is shared, so its 54 exceptional-source cases above run once, not per family.
    for pair in range(10, 20):
        for depth in (0, 1):
            for reverse in (0, 1):
                add('pair_depth_face', pair=pair, depth=depth, reverse=reverse)
    for pair in (10, 12, 16):
        for lights in ((1,) if pair == 16 else (0, 1, 8)):
            for gain in (1., 4., 16.):
                add('lights_gains', pair=pair, lights=lights, gains=[gain] * 3)
    for pair in range(10, 16):
        add('affine_angular', pair=pair, affine=1, normal=[.6, 0., .8])
        for source in ('point', 'material', 'dir0', 'dir1', 'lightmap', 'cube'):
            isolated = {name: ([0.] * 3 if name not in ('lightmap', 'cube') else [0., 0., 0., 1.])
                        for name in ('point', 'material', 'dir0', 'dir1', 'lightmap', 'cube')}
            isolated[source] = base[source]
            add('isolated_' + source, pair=pair, **isolated)
        dark = dict(point=[0.] * 3, material=[0.] * 3, dir1=[0.] * 3,
                    lightmap=[0., 0., 0., .25], cube=[0., 0., 0., 1.])
        add('shared_diffuse_coefficient', pair=pair, mask=0., **dark)
        # Reflected view cosine is about .8 here. This sixth-power witness has
        # enough specular energy to reject a retained fifth-power chain despite
        # the allowed legacy _pp lobe precision.
        add('shared_specular_power', pair=pair, mask=1.,
            normal=[math.sqrt(.1), 0., math.sqrt(.9)], **dark)
        dark['dir0'] = [0.] * 3
        dark['cube'] = base['cube']
        add('shared_cube_coefficient', pair=pair, mask=1., **dark)
        # New per-profile conversion sites independently carry finite HDR,
        # exact black and negative-source sanitation through the complete PS.
        dark.update(cube=[0., 0., 0., 1.], material=[1.] * 3)
        add('shared_finite_domain', pair=pair, fp16=0,
            diffuse=[ref.CAP, 0., -1., .75], **dark)
    for gains in ([16., 1., 1.], [1., 16., 1.], [1., 1., 16.], [0., 0., 0.]):
        add('independent_gains', pair=10, gains=gains)
    for pair in (0, 10, 1, 11, 0, 2, 12, 4, 14, 2, 6, 16, 8, 18, 6):
        add('shared_vertex_alternation', pair=pair)
    # All 313 DEFAULT cases above retain their original fields and order.
    base.update(pair=20, flags=CUBE_PATTERN)
    for pair in range(20, 30):
        for depth in (0, 1):
            for reverse in (0, 1):
                add('bump_pair_depth_face', pair=pair, depth=depth, reverse=reverse)
    bump_profiles = (20, 21, 22, 23, 24, 25)
    for pair in bump_profiles:
        for label, changes in (
            ('alpha_binormal', dict(normal_sample=[.25,.5,.75,.75])),
            ('green_tangent', dict(normal_sample=[.25,.75,.75,.5])),
            ('unused_red_blue', dict(normal_sample=[1.,.5,0.,.75])),
            ('negative_q', dict(normal_sample=[.25,.9375,.75,.9375])),
            ('safe_small_q', dict(normal_sample=[.25,.5,.75,.99609375])),
            ('nonunit_basis', dict(normal=[0.,0.,.5],tangent=[2.,0.,0.],binormal=[0.,.5,0.],normal_sample=[.25,.625,.75,.75])),
            ('nonorthogonal_basis', dict(tangent=[1.,.25,.125],binormal=[.125,1.,.25],normal_sample=[.25,.75,.75,.625])),
            ('mirrored_basis', dict(binormal=[0.,-1.,0.],normal_sample=[.25,.5,.75,.75]))):
            # Cube coordinates in these normal-response cases are allowed to
            # move across texels: a constant cube isolates normal/lobe algebra.
            add('bump_'+label, pair=pair, flags=0, **changes)
        add('bump_affine',pair=pair,affine=1,flags=0,normal_sample=[.25,.625,.75,.75])
        add('bump_mask_data',pair=pair,flags=0,mask=.75)
        add('bump_opacity_data',pair=pair,flags=0,diffuse=[.5,.25,.75,.25],lightmap=[.125,.25,.0625,.75],glow=.75)
        add('bump_finite_domain',pair=pair,flags=0,fp16=0,diffuse=[ref.CAP,0.,-1.,.75],
            material=[1.]*3,point=[0.]*3,dir0=[0.]*3,dir1=[0.]*3,cube=[0.,0.,0.,1.],lightmap=[0.,0.,0.,.25])
    for pair in (20,22,26):
        for lights in ((1,) if pair==26 else (0,1,8)):
            for gain in (1.,4.,16.):
                add('bump_lights_gains',pair=pair,lights=lights,gains=[gain]*3)
        for sample in ([.25,.5,.75,.5],[.25,.75,.75,.75]):
            add('bump_geometric_point',pair=pair,flags=0,normal_sample=sample,
                material=[0.]*3,dir0=[0.]*3,dir1=[0.]*3,cube=[0.,0.,0.,1.],lightmap=[0.,0.,0.,.25])
        for reverse in (0,1):
            add('bump_fog_alpha',pair=pair,reverse=reverse,flags=FOG)
    # Desired reflection directions: every face axis plus independent offcenter
    # U/V signs. With neutral N=+Z, camera=(-R.x,-R.y,R.z) reflects to R.
    directions=((1,0,0),(-1,0,0),(0,1,0),(0,-1,0),(0,0,1),(0,0,-1),
                (1,.75,-.75),(-1,-.75,.75),(.75,1,-.75),(-.75,-1,.75),(.75,-.75,1),(-.75,.75,-1))
    for i, direction in enumerate(directions):
        add('bump_cube_axis_uv',pair=bump_profiles[i%6],camera=[-direction[0],-direction[1],direction[2]],
            material=[0.]*3,point=[0.]*3,dir0=[0.]*3,dir1=[0.]*3,lightmap=[0.,0.,0.,.25],mask=1.)
    # Bumped coordinates, rather than the geometric VS normal, must select a
    # different cube face; A->B and G->T produce different colored faces.
    for pair in bump_profiles:
        for sample in ([.25,.5,.75,.875],[.25,.875,.75,.5]):
            add('bump_cube_perturbed',pair=pair,normal_sample=sample,
                material=[0.]*3,point=[0.]*3,dir0=[0.]*3,dir1=[0.]*3,lightmap=[0.,0.,0.,.25],mask=1.)
    # These deliberately exclude analytic RGB equivalence. Original normal ops
    # remain byte-exact by structural proof; GPU checks finite capped storage,
    # exact alpha, and unchanged temporal outputs, not an invented normal.
    for pair in (20,22,23):
        for label, changes in (
            ('q_zero',dict(normal_sample=[.25,.5,.75,1.])),
            ('near_q_positive',dict(normal_sample=[.25,.5,.75,1.-2**-23])),
            ('near_q_negative',dict(normal_sample=[.25,.5+2**-12,.75,1.])),
            ('zero_mixed_normal',dict(normal=[0.,0.,0.])),
            ('zero_view',dict(camera=[0.,0.,0.]))):
            add('bump_boundary_'+label,pair=pair,flags=BOUNDARY,fp16=0,**changes)
    for pair in (0,20,10,21,0,2,22,12,23,2,6,26,16,29,6):
        add('default_bump_alternation',pair=pair,flags=CUBE_PATTERN if pair>=20 else 0)
    # The original 512 cases retain all fields/order. Scalar tail is ignored by
    # fixed-coefficient shaders; only standard lighting consumes these values.
    for c in cases:
        c['coefficients'] = [1., 1., 1., 10.]  # diffuse, specular, reflection, power
    base.update(flags=0, coefficients=[1.,1.,1.,10.], normal_sample=[.5,.5,1.,.5])
    dark = dict(material=[0.]*3,point=[0.]*3,dir0=[0.]*3,dir1=[0.]*3,
                lightmap=[0.,0.,0.,.25],cube=[0.,0.,0.,1.])
    for pair in range(30,70):
        for depth in (0,1):
            for reverse in (0,1):
                add('extended_pair_depth_face',pair=pair,depth=depth,reverse=reverse)
        for gains in ([0.,0.,0.],[4.,1.,1.],[1.,16.,1.],[1.,1.,16.]):
            add('extended_independent_gains',pair=pair,gains=gains)
        add('extended_missing_history',pair=pair,valid=0)
    # Every distinct new PS conversion layout, not just captured program pairs.
    for start in (30,40,50,60):
        for pair in range(start,start+6):
            add('extended_affine_angular',pair=pair,affine=1,normal=[.6,0.,.8])
            for source in ('point','material','dir0','dir1','lightmap','cube'):
                isolated=copy.deepcopy(dark)
                isolated[source]=base[source]
                add('extended_isolated_'+source,pair=pair,**isolated)
            add('extended_finite_domain',pair=pair,fp16=0,diffuse=[ref.CAP,0.,-1.,.75],
                **dict(dark,material=[1.]*3))
            if start==30:
                add('split_diffuse_coefficient',pair=pair,mask=0.,**dict(dark,dir0=[1.]*3))
                add('split_specular_power',pair=pair,mask=1.,camera=[.6,0.,.8],**dict(dark,dir0=[1.]*3))
                add('split_cube_coefficient',pair=pair,mask=1.,**dict(dark,cube=[.5,.25,.75,1.]))
            else:
                # Independent controls detect swapped uploads, retained fixed
                # coefficients, and accidental exponentiation of scalar inputs.
                for label, coefficients in (
                    ('zero',[0.,0.,0.,10.]), ('diffuse',[2.5,0.,0.,10.]),
                    ('specular',[0.,2.5,0.,10.]), ('reflection',[0.,0.,2.5,10.]),
                    ('fractional_power',[0.,2.5,0.,2.5]),
                    ('mixed',[2.5,.75,1.25,2.5]), ('power_one',[0.,2.5,0.,1.])):
                    add('standard_coeff_'+label,pair=pair,coefficients=coefficients,
                        camera=[.6,0.,.8],mask=1.,material=[0.]*3,point=[0.]*3,
                        lightmap=[0.,0.,0.,.25],dir0=[1.,.5,.25],dir1=[0.]*3)
                add('standard_coeff_hdr',pair=pair,coefficients=[64.,0.,0.,10.],
                    mask=0.,**dict(dark,dir0=[1.]*3))
        for pair in (start,start+2,start+6):
            for lights in ((1,) if pair==start+6 else (0,1,8)):
                for gain in (1.,4.,16.):
                    add('extended_lights_gains',pair=pair,lights=lights,gains=[gain]*3)
            for reverse in (0,1):
                add('extended_fog_alpha',pair=pair,reverse=reverse,flags=FOG)
    for pair in range(50,66):
        if 56 <= pair < 60: continue  # distinct PS layouts already covered
        low=pair>=60
        for label,sample in (
            ('binormal',[.875,.5,.75,.5] if low else [.25,.5,.75,.875]),
            ('tangent',[.5,.875,.75,.5] if low else [.25,.875,.75,.5]),
            ('negative_z',[.625,.75,.125,.5] if low else [.25,.9375,.75,.9375]),
            ('unused',[.875,.5,.75,1.] if low else [1.,.5,0.,.875])):
            add('extended_normal_'+label,pair=pair,normal_sample=sample)
        for label,changes in (
            ('nonunit',dict(normal=[0.,0.,.5],tangent=[2.,0.,0.],binormal=[0.,.5,0.])),
            ('nonorthogonal',dict(tangent=[1.,.25,.125],binormal=[.125,1.,.25])),
            ('mirrored',dict(binormal=[0.,-1.,0.]))):
            add('extended_basis_'+label,pair=pair,normal_sample=[.75,.625,.75,.75],**changes)
        for sample in ([.875,.5,.875,.875],[.5,.875,.875,.5]):
            add('extended_cube_direction',pair=pair,flags=CUBE_PATTERN,normal_sample=sample,
                **dict(dark,cube=base['cube']),mask=1.)
        for sample in ([.5,.5,1.,.5],[.75,.625,.75,.75]):
            add('extended_geometric_point',pair=pair,normal_sample=sample,**dict(dark,point=base['point']))
    # Signed XYZ has valid endpoint normals with no AG reciprocal chain. Zero
    # mixed XYZ remains operational-only; no fallback normal is invented.
    for pair in range(60,66):
        for sample in ([1.,.5,.5,1.],[.5,1.,.5,0.],[.5,.5,0.,1.],[.5,.5,1.,0.]):
            add('low_signed_endpoint',pair=pair,normal_sample=sample)
        add('low_half_endpoint',pair=pair,normal_sample=[.3333,.75,1.,.5])
        add('low_boundary_zero_normal',pair=pair,normal_sample=[.5,.5,.5,.5],flags=BOUNDARY,fp16=0)
        add('low_boundary_zero_view',pair=pair,camera=[0.]*3,flags=BOUNDARY,fp16=0)
    for pair in (30,40,0,50,20,60,10,41,51,61,32,42,52,62,36,46,56,66,0):
        add('extended_family_alternation',pair=pair)
    # Preserve the accepted 1,527-case binary prefix verbatim. These families
    # reuse the existing scalar-free PS and AG geometry reference contracts.
    for pair in range(70,110):
        for depth in (0,1):
            for reverse in (0,1):
                add('hull_pair_depth_face',pair=pair,depth=depth,reverse=reverse)
        for gains in ([0.,0.,0.],[4.,1.,1.],[1.,16.,1.],[1.,1.,16.]):
            add('hull_independent_gains',pair=pair,gains=gains)
        add('hull_missing_history',pair=pair,valid=0)
    for start in (70,80,90,100):
        for pair in range(start,start+6):
            add('hull_affine_angular',pair=pair,affine=1,normal=[.6,0.,.8])
            for source in ('point','material','dir0','dir1','lightmap','cube'):
                isolated=copy.deepcopy(dark)
                isolated[source]=base[source]
                add('hull_isolated_'+source,pair=pair,**isolated)
            add('hull_diffuse_coefficient',pair=pair,mask=0.,**dict(dark,dir0=[1.]*3))
            add('hull_specular_power',pair=pair,mask=1.,camera=[.6,0.,.8],**dict(dark,dir0=[1.]*3))
            add('hull_cube_coefficient',pair=pair,mask=1.,**dict(dark,cube=[.5,.25,.75,1.]))
            add('hull_finite_domain',pair=pair,fp16=0,diffuse=[ref.CAP,0.,-1.,.75],
                **dict(dark,material=[1.]*3))
            # Normal cosine .2 makes sat(3*d)=.6, while 0.5*d is .1.
            # The non-unit basis is intentional and isolates the packed lanes.
            add('hull_packed_angular',pair=pair,normal=[math.sqrt(.96),0.,.2],mask=1.,
                camera=[.4,0.,-.916515138991168],**dict(dark,dir0=[1.]*3))
            if start!=90:
                for label,changes in (
                    ('alpha_binormal',dict(normal_sample=[.25,.5,.75,.75])),
                    ('green_tangent',dict(normal_sample=[.25,.75,.75,.5])),
                    ('unused_red_blue',dict(normal_sample=[1.,.5,0.,.75])),
                    ('negative_q',dict(normal_sample=[.25,.9375,.75,.9375])),
                    ('nonorthogonal',dict(tangent=[1.,.25,.125],binormal=[.125,1.,.25],normal_sample=[.25,.75,.75,.625])),
                    ('mirrored',dict(binormal=[0.,-1.,0.],normal_sample=[.25,.5,.75,.75]))):
                    add('hull_normal_'+label,pair=pair,**changes)
                for sample in ([.25,.5,.75,.875],[.25,.875,.75,.5]):
                    add('hull_cube_direction',pair=pair,flags=CUBE_PATTERN,normal_sample=sample,
                        **dict(dark,cube=base['cube']),mask=1.)
                for sample in ([.25,.5,.75,.5],[.25,.9375,.75,.9375]):
                    add('hull_geometric_point',pair=pair,normal_sample=sample,**dict(dark,point=base['point']))
        for pair in (start,start+2,start+6):
            for lights in ((1,) if pair==start+6 else (0,1,8)):
                for gain in (1.,4.,16.):
                    add('hull_lights_gains',pair=pair,lights=lights,gains=[gain]*3)
            for reverse in (0,1):
                add('hull_fog_alpha',pair=pair,reverse=reverse,flags=FOG)
    for pair in (70,71,72,73,74,75,80,81,82,83,84,85,100,101,102,103,104,105):
        add('hull_boundary_q_zero',pair=pair,normal_sample=[.25,.5,.75,1.],flags=BOUNDARY,fp16=0)
    for pair in (70,20,80,50,90,0,100,60,72,82,92,102,76,86,96,106,40):
        add('hull_family_alternation',pair=pair)
    # The 2,498 prior case payloads stay byte-exact. Asteroid reuses fields:
    # lightmap = detail RGBA; coefficients = base/detail weights and base UV.
    base.update(coefficients=[1.,.5,.0625,.1875])
    for pair in range(110,116):
        for depth in (0,1):
            for reverse in (0,1):
                add('asteroid_pair_depth_face',pair=pair,depth=depth,reverse=reverse)
        for weights in ((1.,0.),(0.,1.),(.75,.25),(2.,.5),(0.,0.)):
            add('asteroid_weights',pair=pair,coefficients=[*weights,.0625,.1875])
        for gains in ([0.,0.,0.],[4.,1.,1.],[1.,16.,1.],[1.,1.,16.]):
            add('asteroid_independent_gains',pair=pair,gains=gains)
        add('asteroid_missing_history',pair=pair,valid=0)
        for lights in ((1,) if PAIRS[pair][0] in FIXED_VERTICES else (0,1,8)):
            for gain in (1.,4.,16.):
                add('asteroid_lights_gains',pair=pair,lights=lights,gains=[gain]*3)
        for reverse in (0,1):
            add('asteroid_fog_alpha',pair=pair,reverse=reverse,flags=FOG)
        for source in ('point','material','dir0','dir1'):
            isolated={key:[0.]*3 for key in ('point','material','dir0','dir1')}
            isolated[source]=base[source]
            add('asteroid_isolated_'+source,pair=pair,**isolated)
        lightonly=dict(point=[0.]*3,material=[0.]*3,dir0=[1.]*3,dir1=[0.]*3)
        add('asteroid_second_direction',pair=pair,normal=[0.,0.,-1.],camera=[0.,0.,-4.],
            point=[0.]*3,material=[0.]*3,dir0=[0.]*3,dir1=[.5,.25,.75])
        add('asteroid_unit_diffuse',pair=pair,mask=0.,**lightonly)
        add('asteroid_cubic_specular',pair=pair,mask=1.,camera=[.6,0.,.8],**lightonly)
        add('asteroid_grazing_specular',pair=pair,mask=1.,normal=[.9797958971132712,0.,.2],
            camera=[.3919183588453085,0.,-.92],**lightonly)
        for uv in ((.0625,.1875),(.1875,.0625),(.3125,.1875)):
            add('asteroid_detail_uv',pair=pair,flags=DETAIL_PATTERN,coefficients=[.25,2.,*uv])
        add('asteroid_detail_alpha_ignored',pair=pair,lightmap=[.125,.25,.0625,0.])
        add('asteroid_base_alpha',pair=pair,diffuse=[.5,.25,.75,.125])
        add('asteroid_full_precision',pair=pair,fp16=0,diffuse=[.3333,.0625,1.25,.75])
        if pair>=113:
            for label,changes in (
                ('alpha_binormal',dict(normal_sample=[.25,.5,.75,.75])),
                ('green_tangent',dict(normal_sample=[.25,.75,.75,.5])),
                ('unused_red_blue',dict(normal_sample=[1.,.5,0.,.75])),
                ('negative_q',dict(normal_sample=[.25,.9375,.75,.9375])),
                ('nonorthogonal',dict(tangent=[1.,.25,.125],binormal=[.125,1.,.25],normal_sample=[.25,.75,.75,.625])),
                ('mirrored',dict(binormal=[0.,-1.,0.],normal_sample=[.25,.5,.75,.75]))):
                add('asteroid_normal_'+label,pair=pair,camera=[.6,0.,.8],mask=1.,**changes)
            for sample in ([.25,.5,.75,.5],[.25,.9375,.75,.9375]):
                add('asteroid_geometric_point',pair=pair,normal_sample=sample,
                    point=[.5,.25,.125],material=[0.]*3,dir0=[0.]*3,dir1=[0.]*3)
            add('asteroid_boundary_q_zero',pair=pair,normal_sample=[.25,.5,.75,1.],flags=BOUNDARY,fp16=0)
    for pair in (110,0,113,20,111,112,114,115,100,110):
        add('asteroid_family_alternation',pair=pair)
    # Keep all 2,757 prior records unchanged. Palette coefficients are unused by
    # native PS: use them only for world XY scale + normal XY slopes (gradient),
    # or constant UV in the last two slots (pattern). No authored interpolants.
    base.update(coefficients=[0.,0.,.0625,.4375])
    representatives={}
    vertices={}
    for pair in range(116,148):
        representatives.setdefault(PAIRS[pair][1],pair)
        vertices.setdefault(PAIRS[pair][0],pair)
        for depth in (0,1):
            for reverse in (0,1):
                add('palette_pair_depth_face',pair=pair,depth=depth,reverse=reverse)
        for gains in ([0.,0.,0.],[4.,1.,1.],[1.,16.,1.],[1.,1.,16.]):
            add('palette_independent_gains',pair=pair,gains=gains)
        add('palette_missing_history',pair=pair,valid=0)
        add('palette_fog_alpha',pair=pair,flags=FOG)
    for vs,pair in vertices.items():
        for lights in ((1,) if vs in FIXED_VERTICES else (0,1,8)):
            for gain in (1.,16.):
                add('palette_lights_gains',pair=pair,lights=lights,gains=[gain]*3)
    for ps,pair in representatives.items():
        for normal,camera in (([.4,.3,.7],[3.,0.,4.]),([.25,.5,1.5],[0.,0.,4.])):
            add('palette_unequal_weights',pair=pair,normal=normal,camera=camera,fp16=0)
        isolated={key:[0.]*3 for key in ('point','material','dir0','dir1')}
        isolated.update(lightmap=[0.,0.,0.,.25],cube=[0.,0.,0.,1.])
        for source in ('point','material','dir0','dir1','lightmap','cube'):
            args=copy.deepcopy(isolated);args[source]=base[source]
            if source=='dir1':args.update(normal=[0.,0.,-1.],camera=[0.,0.,-4.])
            add('palette_isolated_'+source,pair=pair,**args)
        add('palette_diffuse_coefficient',pair=pair,mask=0.,**dict(isolated,dir0=[1.]*3))
        add('palette_specular_power',pair=pair,mask=1.,camera=[.6,0.,.8],**dict(isolated,dir0=[1.]*3))
        add('palette_grazing_lobe',pair=pair,mask=1.,normal=[.9797958971132712,0.,.2],
            camera=[.3919183588453085,0.,-.92],**dict(isolated,dir0=[1.]*3))
        add('palette_affine_color',pair=pair,affine=1,normal=[.4,.3,.7])
        for camera in ([.8,0.,1.],[0.,.8,1.]):
            add('palette_cube_direction',pair=pair,flags=CUBE_PATTERN,camera=camera,
                **dict(isolated,cube=base['cube']),mask=1.)
        for uv in ((.0625,.4375),(.4375,.0625)):
            add('palette_diffuse_uv',pair=pair,flags=DIFFUSE_PATTERN,coefficients=[0.,0.,*uv])
        # Separately interpolate normalized VS view, reflection, palette RGB,
        # point RGB and native J/u^11; never reconstruct them at the sample.
        for reverse in (0,1):
            add('palette_interpolated_geometry',pair=pair,flags=PALETTE_GRADIENT,
                coefficients=[.25,.125,.0625,.03125],normal=[.25,.125,.875],reverse=reverse,fp16=0)
        if ref.PALETTE_PROFILES[ps].bump_map:
            for label,changes in (
                ('ag_axes',dict(normal_sample=[.25,.75,.75,.625],tangent=[1.,.25,.125],binormal=[.125,1.,.25])),
                ('negative_q',dict(normal_sample=[.25,.9375,.75,.9375])),
                ('mirrored',dict(binormal=[0.,-1.,0.],normal_sample=[.25,.5,.75,.75]))):
                add('palette_normal_'+label,pair=pair,camera=[.6,0.,.8],**changes)
            add('palette_boundary_q_zero',pair=pair,normal_sample=[.25,.5,.75,1.],flags=BOUNDARY,fp16=0)
    # Four native transport forms, two depth/winding states; no Cartesian bank.
    for pair in (116,122,128,138):
        for depth,reverse in ((0,0),(1,1)):
            add('palette_perspective_geometry',pair=pair,depth=depth,reverse=reverse,
                flags=PALETTE_GRADIENT|PALETTE_PERSPECTIVE,fp16=0,
                coefficients=[.25,.125,.0625,.03125],normal=[.25,.125,.875])
    for pair in (116,110,122,113,128,40,138,116):
        add('palette_family_alternation',pair=pair)
    return glass_fixture.append_cases(xt_fixture.append_cases(cases))


FILL = 0.06
FILL_PAIRS = (0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100,
              110, 113, 116, 122, 128, 138, 148, 150, 152, 154, 162, 164)


def fill_cases():
    """Small sun-averted oracle slice; the ordinary case bank stays frozen."""
    full = fixture_cases()
    result = []
    for pair in FILL_PAIRS:
        c = copy.deepcopy(next(row for row in full if row['pair'] == pair))
        fixed = PAIRS[pair][0] in FIXED_VERTICES
        c.update(id=len(result), label='fill_oracle', depth=len(result) & 1,
                 lights=1 if fixed else 0, reverse=0, affine=0, valid=1,
                 fp16=1, flags=0, gains=[2., 1., 1.],
                 diffuse=[.5, .25, .75, .75], lightmap=[0., 0., 0., .25],
                 cube=[0., 0., 0., 1.], mask=0., material=[0., 0., 0.],
                 point=[0., 0., 0.], dir0=[.375, .25, .5],
                 dir1=[0., 0., 0.], normal=[0., 0., -1.], glow=0.,
                 normal_sample=[.5, .5, 1., .5], binormal=[0., 1., 0.],
                 tangent=[1., 0., 0.], camera=[0., 0., -4.])
        result.append(c)
    return result


def fields(c):
    return (c['gains'] + c['diffuse'] + c['lightmap'] + c['cube'] + [c['mask']] +
            c['material'] + c['point'] + c['dir0'] + c['dir1'] + c['normal'] + [c['glow']] + c['normal_sample'] + c['binormal'] +
            c['tangent'] + c['camera'] + c['fog_clip'] + c['coefficients'])


def binary_cases(cases):
    data = bytearray(struct.pack('<I', len(cases)))
    for c in cases:
        data.extend(struct.pack('<9I51f', *(c[k] for k in ('id', 'pair', 'depth', 'lights', 'reverse', 'affine', 'valid', 'fp16', 'flags')), *fields(c)))
    return bytes(data)


def pattern_diffuse(c):
    u,v=c['coefficients'][2:]
    ix,iy=(min(3,max(0,int(t*4))) for t in (u,v))
    return ((ix+1)/8.,(iy+1)/8.,(ix+iy+1)/16.,c['diffuse'][3])


def palette_varying(c, sample=(8,8)):
    # D3D9 integer pixel centers give screen barycentrics
    # (1-x/32-y/32,x/32,y/32). Perspective cases scale object XY by
    # W=(1,2,4), supply W in object z, and project clip z=.5W. Thus their
    # screen triangle is unchanged; native varyings need reciprocal-W weights.
    # https://learn.microsoft.com/en-us/windows/win32/direct3d9/directly-mapping-texels-to-pixels
    from dataclasses import replace
    f32=lambda v:struct.unpack('<f',struct.pack('<f',v))[0]
    vector=lambda key:tuple(f32(v) for v in c[key])
    vs=PAIRS[c['pair']][0]
    lights=[ref.PointLight((0,0,2),vector('point'),(2,.25,.125))]*(1 if vs in FIXED_VERTICES else c['lights'])
    def at(x,y):
        position=(0.,0.,0.);normal=vector('normal')
        if c['flags']&PALETTE_GRADIENT:
            sx,sy,nx,ny=vector('coefficients')
            position=(f32(sx*x),f32(sy*y),0.)
            normal=(f32(normal[0]+f32(nx*x)),f32(normal[1]+f32(ny*y)),normal[2])
        return ref.palette_vertex(vs,position,normal,vector('camera'),vector('material'),lights,
                                  material_alpha=.625,gains=ref.Gains(*vector('gains')),
                                  fog_clip=vector('fog_clip') if c['flags']&FOG else None)
    if not c['flags']&PALETTE_GRADIENT:return at(0,0)
    if c['flags']&FOG:raise ValueError('gradient fog alpha requires separate native precision qualification')
    # Non-fog native alpha is constant at all vertices: retain it exactly,
    # rather than introducing float64 summation drift into the alpha oracle.
    clip_w=(1.,2.,4.) if c['flags']&PALETTE_PERSPECTIVE else (1.,1.,1.)
    a,b,d=(at(x*w,y*w) for (x,y),w in zip(((-1,1),(3,1),(-1,-3)),clip_w))
    bx,by=sample[0]/32.,sample[1]/32.
    weighted=tuple(bary/w for bary,w in zip((1-bx-by,bx,by),clip_w))
    weights=tuple(w/sum(weighted) for w in weighted)
    blend=lambda key:tuple(sum(w*getattr(v,key)[i] for w,v in zip(weights,(a,b,d))) for i in range(3))
    scalar=lambda key:sum(w*getattr(v,key) for w,v in zip(weights,(a,b,d)))
    return replace(a,normal=blend('normal'),view=blend('view'),reflection=blend('reflection'),
                   linear_rgb=blend('linear_rgb'),alpha=a.alpha,palette_weights=blend('palette_weights'),
                   reflection_weight=scalar('reflection_weight'),view_weight=scalar('view_weight'),
                   vertex_palette_rgb=blend('vertex_palette_rgb') if a.vertex_palette_rgb is not None else None)


def expected(c, half_source=False, sample=(8,8), flat_effective=False, fill=0.0):
    if c['pair'] >= 162:
        baseline = glass_fixture.expected(c,half_source,sample,cube_sampler=cube_sample,
                                          flat_color=flat_effective and bool(c['flags'] & 2048))
        if not fill:
            return baseline
        quantize = ref.half if half_source else float
        added = tuple(ref.decode(quantize(c['diffuse'][i])) * fill * ref.decode(c['dir0'][i]) * c['gains'][0]
                      for i in range(3))
        linear = tuple(ref.sanitize(a + b) for a, b in zip(baseline.linear_rgb, added))
        rgba = tuple(ref.encode(v) for v in linear) + (baseline.encoded_rgba[3],)
        return ref.PixelResult(linear, tuple(ref.half(v) for v in rgba) if c['fp16'] else rgba)
    if c['pair'] >= 148:
        baseline = xt_fixture.expected(c,half_source,sample,cube_sampler=cube_sample,
                                       flat_color=flat_effective and bool(c['flags'] & xt_fixture.FLAT))
        if not fill:
            return baseline
        values = xt_fixture.inputs(c, sample, linear=True, half_source=half_source,
                                   flat_color=flat_effective and bool(c['flags'] & xt_fixture.FLAT),
                                   cube_sampler=cube_sample)
        evaluated = xt_fixture.xt.linear_pixel(PAIRS[c['pair']][1], values)
        # The fill-mode fixture neutralizes XT's downstream occurrence to one,
        # leaving the requested k*decode(sun)*albedo term in isolation.
        added = tuple(evaluated.base_working[i] * fill * ref.decode(c['dir0'][i]) * c['gains'][0]
                      for i in range(3))
        linear = tuple(ref.sanitize(a + b) for a, b in zip(baseline.linear_rgb, added))
        rgba = tuple(ref.encode(v) for v in linear) + (baseline.encoded_rgba[3],)
        return ref.PixelResult(linear, tuple(ref.half(v) for v in rgba) if c['fp16'] else rgba)
    if c['flags'] & BOUNDARY:
        raise ValueError('operational BUMP boundary case excludes float64 RGB equivalence')
    # Inputs are uploaded as binary32, including .6/.8 angular control values.
    f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]
    vector = lambda key: tuple(f32(x) for x in c[key])
    gains = ref.Gains(*vector('gains'))
    fixed = PAIRS[c['pair']][0] in FIXED_VERTICES
    lights = [ref.PointLight((0, 0, 2), vector('point'), (2, .25, .125))] * (1 if fixed else c['lights'])
    varying = ref.vertex((0, 0, 0), vector('normal'), vector('camera'), vector('material'), lights,
                         fixed_single=fixed, material_alpha=.625, gains=gains,
                         fog_clip=vector('fog_clip') if c['flags'] & FOG else None)
    profile = PAIRS[c['pair']][1]
    directions = [ref.DirectionalLight((0, 0, 1), vector('dir0'))]
    contract = (ref.PALETTE_PROFILES[profile] if c['pair'] >= 116 else
                ref.ASTEROID_PROFILES[profile] if c['pair'] >= 110 else ref.PROFILES[profile])
    if contract.directions == 2:
        directions.append(ref.DirectionalLight((0, 0, -1), vector('dir1')))
    if 110 <= c['pair'] < 116:
        return ref.asteroid_pixel(profile,varying,vector('diffuse'),tuple(f32(v) for v in detail_sample(c)),
            f32(c['mask']),directions,weights=ref.AsteroidWeights(*vector('coefficients')[:2]),
            gains=gains,fill=fill,half_source=half_source,half_target=bool(c['fp16']),
            normal_sample=vector('normal_sample'),tangent=vector('tangent'),binormal=vector('binormal'))
    if c['pair'] >= 116:
        varying=palette_varying(c,sample)
        diffuse=pattern_diffuse(c) if c['flags'] & DIFFUSE_PATTERN else vector('diffuse')
        return ref.palette_pixel(profile,varying,diffuse,f32(c['mask']),vector('lightmap'),
            cube_sample if c['flags']&CUBE_PATTERN else lambda _:vector('cube')[:3],directions,
            affine=AFFINE if c['affine'] else ref.IDENTITY_AFFINE,face=-1 if c['reverse'] else 1,
            glow=f32(c['glow']),gains=gains,fill=fill,half_source=half_source,half_target=bool(c['fp16']),
            normal_sample=vector('normal_sample'),tangent=vector('tangent'),binormal=vector('binormal'))
    return ref.pixel(profile, varying, vector('diffuse'), f32(c['mask']), vector('lightmap'),
                     (cube_sample if c['flags'] & CUBE_PATTERN else lambda _:vector('cube')[:3])
                     if ref.PROFILES[profile].bump_map else vector('cube')[:3], directions, affine=AFFINE if c['affine'] else ref.IDENTITY_AFFINE,
                     face=-1 if c['reverse'] else 1, glow=f32(c['glow']), gains=gains,
                     fill=fill,
                     half_source=half_source, half_target=bool(c['fp16']), normal_sample=vector('normal_sample'),
                     tangent=vector('tangent'), binormal=vector('binormal'),
                     coefficients=ref.LightingCoefficients(*vector('coefficients'))
                     if ref.PROFILES[profile].application_coefficients else None)


def expected_alpha(c):
    alpha=.625
    if c['flags'] & FOG:
        alpha*=min(1.,max(0.,c['fog_clip'][0]-c['fog_clip'][1]*math.hypot(*c['camera'])))
    value=(c['diffuse'][3] if 110 <= c['pair'] < 116 or c['pair'] >= 162 else
           c['glow']*c['lightmap'][3]+(1-c['glow'])*c['diffuse'][3])*alpha
    return ref.half(value) if c['fp16'] else value


def validate_report(text, cases=None):
    cases = fixture_cases() if cases is None else cases
    by_id = {c['id']: c for c in cases}
    assert len(by_id) == len(cases), 'duplicate case ID'
    selected_pairs = {PAIRS[c['pair']] for c in cases}
    timing_pairs = tuple(p for p in TIMING_PAIRS if any(c['pair']==p for c in cases))
    xt_timing_pairs = tuple(p for p in (148,150) if any(c['pair']==p for c in cases))
    lines = text.splitlines()
    assert lines and lines[-1] == f'RESULT PASS cases={len(cases)}', 'missing final result'
    assert not any('FAIL' in line for line in lines), 'fixture reported failure'
    caps = [line for line in lines if line.startswith('CAPS ')]
    assert len(caps) == 1 and re.fullmatch(r'CAPS mrt=\d+ vs_slots=\d+ ps_slots=\d+', caps[0])
    cap = dict((k, int(v)) for k, v in re.findall(r'(\w+)=(\d+)', caps[0]))
    assert cap['mrt'] >= 3
    flat_rows = re.findall(r'^FLAT effective=([01]) gouraud=(\S+) requested_flat=(\S+)$', text, re.M)
    assert len(flat_rows) == 1, 'missing effective programmable COLOR interpolation witness'
    flat_effective = bool(int(flat_rows[0][0]))
    smooth, flat = map(float,flat_rows[0][1:])
    assert all(map(math.isfinite,(smooth,flat))) and abs(smooth-.25)<1e-6
    assert abs(flat-(.125 if flat_effective else .25))<1e-6, 'invalid FLAT classification'
    creates = re.findall(r'^CREATE stage=(vs|ps) key=(\S+) instructions=(\d+) words=(\d+) completed_ms=(\S+)$', text, re.M)
    assert len(creates) == len([l for l in lines if l.startswith('CREATE ')]) and creates
    assert len({(r[0], r[1]) for r in creates}) == len(creates), 'duplicate shader creation'
    required = {(stage, shader, str(depth)) for vs, ps in selected_pairs for stage, shader in [('vs', vs), ('ps', ps)] for depth in (0, 1)}
    observed = set()
    repaired_observed = set()
    for stage, key, count, words, ms in creates:
        shader, mode, depth, *_ = key.split('_')
        assert 0 < int(count) <= cap[stage + '_slots'] and int(words) > int(count)
        assert math.isfinite(float(ms)) and float(ms) >= 0
        if mode == '2': observed.add((stage, shader, depth))
        if key.endswith('_xt_repaired'):repaired_observed.add((stage,shader,depth,mode))
    assert required <= observed, 'missing combined program/depth creation'
    repaired_required={(stage,shader,str(depth),str(mode)) for vs,ps in selected_pairs
                       if (vs,ps) in xt_fixture.PAIRS and vs=='494fe349b8bc12ec' for stage,shader in (('vs',vs),('ps',ps))
                       for depth in (0,1) for mode in (1,2)}
    assert repaired_required<=repaired_observed, 'missing pair-local repaired ordinary/linear program'
    invariants = re.findall(r'^INVARIANT id=(\d+) pixels=256 alpha_bad=0 motion_bad=0 depth_bad=0 rgb_bad=0$', text, re.M)
    assert list(map(int, invariants)) == [c['id'] for c in cases], 'missing or failed invariant'
    samples = re.findall(r'^SAMPLE id=(\d+) x=(\d+) y=(\d+) rgba=(\S+)$', text, re.M)
    assert len(samples) == len(cases) * 9, 'missing numerical sample'
    seen, maximum_abs, maximum_scaled, black_samples, hdr_samples = set(), 0., 0., 0, 0
    failures = []
    family_results = {name:dict(cases=sum(family_name(c['pair']) == name for c in cases),
                               max_rgb_envelope_error=0., max_tolerance_fraction=0.)
                      for name in FAMILIES}
    for cid, x, y, rgba in samples:
        cid, x, y = int(cid), int(x), int(y)
        assert cid in by_id and x in (4, 8, 12) and y in (4, 8, 12)
        assert (cid, x, y) not in seen, 'duplicate sample'
        seen.add((cid, x, y))
        c = by_id[cid]
        family = family_results[family_name(c['pair'])]
        actual = tuple(map(float, rgba.split(',')))
        assert len(actual) == 4 and all(map(math.isfinite, actual)), (cid, 'nonfinite GPU output')
        assert actual[3] == expected_alpha(c), (cid, 'authored alpha')
        if c['flags'] & BOUNDARY:
            ceiling=ref.half(ref.encode(ref.CAP)) if c['fp16'] else ref.encode(ref.CAP)*(1+1e-5)
            assert all(0 <= v <= ceiling for v in actual[:3]), (cid, 'boundary storage outside finite encoded cap')
            continue
        ideal, quantized = expected(c,sample=(x,y),flat_effective=flat_effective), expected(c, half_source=True,sample=(x,y),flat_effective=flat_effective)
        for k, value in enumerate(actual[:3]):
            # Retained _pp samples may keep binary32 or narrow to binary16;
            # use the envelope, plus bounded legacy lobe/FP32 transfer error.
            lo = min(ideal.encoded_rgba[k], quantized.encoded_rgba[k])
            hi = max(ideal.encoded_rgba[k], quantized.encoded_rgba[k])
            error = max(lo - value, value - hi, 0.)
            absolute = 1e-12 if c['label'].endswith('_tiny') else RGB_ABS_TOL
            tolerance = absolute + RGB_REL_TOL * max(abs(lo), abs(hi))
            maximum_abs = max(maximum_abs, error)
            maximum_scaled = max(maximum_scaled, error / tolerance)
            family['max_rgb_envelope_error'] = max(family['max_rgb_envelope_error'], error)
            family['max_tolerance_fraction'] = max(family['max_tolerance_fraction'], error / tolerance)
            if error > tolerance: failures.append((cid, c['label'], k, value, lo, hi, tolerance))
            if not c['fp16'] and lo > 0:
                assert value > 0, (cid, 'positive full-range path collapsed to zero')
            if lo == hi == 0:
                assert value == 0 and math.copysign(1., value) == 1., (cid, 'black not exact +0')
                black_samples += 1
            if lo > 1: hdr_samples += 1
        # All fixture alpha inputs are exactly representable and the independent
        # whole-RT motion-only comparison also checks bit-for-bit alpha equality.
        assert actual[3] == ideal.encoded_rgba[3], (cid, 'authored alpha')
    assert not failures, ('RGB oracle mismatches', failures[:12], 'total', len(failures))
    baselines = re.findall(r'^BASELINE id=(\d+) x=(\d+) y=(\d+) rgba=(\S+)$',text,re.M)
    expected_baselines = {(c['id'],x,y) for c in cases if c['pair']>=148 for x in (4,8,12) for y in (4,8,12)}
    seen_baselines = set()
    baseline_max = 0.
    for cid,x,y,rgba in baselines:
        cid,x,y = int(cid),int(x),int(y)
        assert (cid,x,y) in expected_baselines and (cid,x,y) not in seen_baselines
        seen_baselines.add((cid,x,y))
        c=by_id[cid]
        actual=tuple(map(float,rgba.split(',')))
        assert len(actual)==4 and all(map(math.isfinite,actual))
        oracle=glass_fixture if c['pair']>=162 else xt_fixture
        wanted=[oracle.expected(c,half,(x,y),linear=False,cube_sampler=cube_sample,
                  flat_color=flat_effective and bool(c['flags']&xt_fixture.FLAT)).encoded_rgba for half in (False,True)]
        assert actual[3]==expected_alpha(c), 'ordinary alpha'
        for k,value in enumerate(actual[:3]):
            lo,hi=sorted(row[k] for row in wanted)
            fraction=max(lo-value,value-hi,0.)/(RGB_ABS_TOL+RGB_REL_TOL*max(abs(lo),abs(hi)))
            baseline_max=max(baseline_max,fraction)
            assert fraction<=1., ('ordinary/reference mismatch',cid,x,y,k,value,lo,hi)
    assert seen_baselines==expected_baselines, 'missing ordinary reference samples'
    timings = re.findall(r'^TIMING pair=(0|10|20|30|40|50|60|70|80|90|100|110|113|116|122|128|138|162) lights=(0|8) mode=([012]) iteration=(\d+) draws=4 vertices=98304 width=256 completed_ms=(\S+)$', text, re.M)
    assert len(timings) == 36 * len(timing_pairs)
    timing_summary = []
    for pair in timing_pairs:
        for lights in (0, 8):
            rows = [r for r in timings if int(r[0]) == pair and int(r[1]) == lights]
            assert [int(r[3]) for r in rows] == list(range(18))
            assert [int(r[2]) for r in rows] == [2-i%3 if (i//3)%2 else i%3 for i in range(18)]
            for mode in range(3):
                values = [float(r[4]) for r in rows if int(r[2]) == mode]
                assert len(values) == 6 and all(math.isfinite(v) and v >= 0 for v in values)
                timing_summary.append(dict(pair=pair, family=family_name(pair),
                                           lights=lights, mode=('original', 'motion', 'combined')[mode],
                                           samples=6, median_ms=statistics.median(values), min_ms=min(values), max_ms=max(values)))
    xt_timings = re.findall(r'^TIMING pair=(148|150) lights=([018]) mode=([012]) iteration=(\d+) draws=4 vertices=98304 width=256 completed_ms=(\S+) palette=(0|128) repaired=([01])$',text,re.M)
    assert len(xt_timings)==len(xt_timing_pairs)*3*2*18, 'missing XT matched timing windows'
    for pair in xt_timing_pairs:
        for lights in (0,1,8):
            for palette in (0,128):
                rows=[r for r in xt_timings if (int(r[0]),int(r[1]),int(r[5]))==(pair,lights,palette)]
                assert [int(r[3]) for r in rows]==list(range(18))
                assert [int(r[2]) for r in rows]==[2-i%3 if (i//3)%2 else i%3 for i in range(18)]
                assert all(int(r[6])==(pair==148) for r in rows)
                for mode in range(3):
                    values=[float(r[4]) for r in rows if int(r[2])==mode]
                    assert len(values)==6 and all(math.isfinite(v) and v>=0 for v in values)
                    timing_summary.append(dict(pair=pair,family=family_name(pair),lights=lights,
                        palette_enabled=bool(palette),mode=(('repaired ordinary','repaired motion','combined') if pair==148 else ('original','motion','combined'))[mode],
                        samples=6,median_ms=statistics.median(values),min_ms=min(values),max_ms=max(values)))
    recognized = 1 + len(flat_rows) + len(creates) + len(invariants) + len(samples) + len(baselines) + len(timings) + len(xt_timings) + 1
    assert len(lines) == recognized, 'unexpected output rows'
    return dict(cases=len(cases), pairs=len(selected_pairs), unique_originals=len({('vs',v) for v,p in selected_pairs}|{('ps',p) for v,p in selected_pairs}), shader_creations=len(creates),
                samples=len(samples), analytic_samples=9*sum(not bool(c["flags"] & BOUNDARY) for c in cases), families=family_results, original_case_prefix=3549 if all(i in by_id for i in range(3549)) else 0,
                xt_ordinary_samples=sum(148<=by_id[int(row[0])]['pair']<162 for row in baselines),
                glass_ordinary_samples=sum(by_id[int(row[0])]['pair']>=162 for row in baselines),
                ordinary_max_tolerance_fraction=baseline_max,
                effective_programmable_color_flat=flat_effective,
                boundary_cases=sum(bool(c["flags"] & BOUNDARY) for c in cases),
                boundary_limit="Finite capped RGB storage and alpha/temporal identity only; no float64 full-color equivalence", invariant_pixels=256*len(cases), max_rgb_envelope_error=maximum_abs,
                max_tolerance_fraction=maximum_scaled, exact_black_channels=black_samples,
                hdr_channels=hdr_samples, caps=cap, timings=timing_summary,
                max_executable_instructions={stage:max(int(r[2]) for r in creates if r[0]==stage) for stage in ('vs','ps')},
                create_total_ms=sum(float(r[4]) for r in creates))


def _half_code(value):
    return struct.unpack('<H', struct.pack('<e', value))[0]


def _linear_luma(rgb):
    return sum(weight * value for weight, value in zip((0.2126, 0.7152, 0.0722), rgb))


def validate_fill_report(text, cases=None):
    """Require the isolated K=.06 result within one binary16 code."""
    cases = fill_cases() if cases is None else cases
    assert cases == fill_cases(), 'fill case contract changed'
    lines = text.splitlines()
    assert lines and lines[-1] == f'RESULT PASS cases={len(cases)}'
    assert not any('FAIL' in line for line in lines)
    assert len(re.findall(r'^CAPS ', text, re.M)) == 1
    assert len(re.findall(r'^FLAT ', text, re.M)) == 1
    invariants = re.findall(r'^INVARIANT id=(\d+) pixels=256 alpha_bad=0 motion_bad=0 depth_bad=0 rgb_bad=0$', text, re.M)
    assert list(map(int, invariants)) == list(range(len(cases)))
    samples = re.findall(r'^SAMPLE id=(\d+) x=(\d+) y=(\d+) rgba=(\S+)$', text, re.M)
    assert len(samples) == 9 * len(cases)
    maximum_codes = 0
    fp16_luma_codes = 0
    seen = set()
    for cid, x, y, rgba in samples:
        cid, x, y = int(cid), int(x), int(y)
        assert (cid, x, y) not in seen and x in (4, 8, 12) and y in (4, 8, 12)
        seen.add((cid, x, y))
        actual = tuple(map(float, rgba.split(',')))
        wanted = expected(cases[cid], sample=(x, y), fill=FILL).encoded_rgba
        assert len(actual) == 4 and all(map(math.isfinite, actual))
        assert actual[3] == wanted[3]
        for got, want in zip(actual[:3], wanted[:3]):
            codes = abs(_half_code(got) - _half_code(want))
            maximum_codes = max(maximum_codes, codes)
            assert codes <= 1, (cid, x, y, got, want, codes)
        reconstructed = tuple(ref.decode(v) for v in actual[:3])
        luma_codes = abs(_half_code(_linear_luma(reconstructed)) -
                         _half_code(_linear_luma(expected(cases[cid], sample=(x, y), fill=FILL).linear_rgb)))
        fp16_luma_codes = max(fp16_luma_codes, luma_codes)
        assert luma_codes <= 3, (cid, x, y, 'FP16 reconstructed luma', luma_codes)
    samples32 = re.findall(r'^SAMPLE32 id=(\d+) x=(\d+) y=(\d+) rgba=(\S+)$', text, re.M)
    lumas = re.findall(r'^LUMA id=(\d+) x=(\d+) y=(\d+) value=(\S+)$', text, re.M)
    required_samples = {(c['id'], x, y) for c in cases for x in (4, 8, 12) for y in (4, 8, 12)}
    sample32_keys = [(int(cid), int(x), int(y)) for cid, x, y, _ in samples32]
    luma_keys = [(int(cid), int(x), int(y)) for cid, x, y, _ in lumas]
    assert len(samples32) == len(required_samples) and len(set(sample32_keys)) == len(samples32)
    assert len(lumas) == len(required_samples) and len(set(luma_keys)) == len(lumas)
    assert set(sample32_keys) == required_samples and set(luma_keys) == required_samples
    luma_values = {(int(cid), int(x), int(y)): float(value) for cid, x, y, value in lumas}
    pre_rt_luma_codes = 0
    for cid, x, y, rgba in samples32:
        cid, x, y = int(cid), int(x), int(y)
        actual = tuple(map(float, rgba.split(',')))
        assert len(actual) == 4 and all(math.isfinite(v) and v >= 0 for v in actual)
        wanted = expected(cases[cid], sample=(x, y), fill=FILL)
        decoded_luma = _linear_luma(tuple(ref.decode(v) for v in actual[:3]))
        reported_luma = luma_values[(cid, x, y)]
        assert math.isfinite(reported_luma) and reported_luma >= 0
        assert abs(reported_luma - decoded_luma) <= 2e-7 * max(1.0, decoded_luma)
        codes = abs(_half_code(reported_luma) - _half_code(_linear_luma(wanted.linear_rgb)))
        pre_rt_luma_codes = max(pre_rt_luma_codes, codes)
        assert codes <= 1, (cid, x, y, reported_luma, _linear_luma(wanted.linear_rgb), codes)
    creates = re.findall(r'^CREATE stage=(vs|ps) key=(\S+) instructions=(\d+) words=(\d+) completed_ms=(\S+)$', text, re.M)
    assert creates and len(creates) == len([line for line in lines if line.startswith('CREATE ')])
    filled_ps = [key for stage, key, *_ in creates
                 if stage == 'ps' and '_2_' in key and '_fill_' in key]
    required_filled_ps = set()
    for c in cases:
        suffix = '_xt_repaired' if c['pair'] in (148, 149, 156, 157) else ''
        required_filled_ps.add(f'{PAIRS[c["pair"]][1]}_2_{c["depth"]}_2_1_1_fill_0.0599999987{suffix}')
    assert len(filled_ps) == 23 and Counter(filled_ps) == Counter({key: 1 for key in required_filled_ps}), \
        'missing, duplicate or unexpected filled PS creation'
    baselines = re.findall(r'^BASELINE id=(\d+) x=(\d+) y=(\d+) rgba=(\S+)$', text, re.M)
    required_baselines = {(c['id'], x, y) for c in cases if c['pair'] >= 148
                          for x in (4, 8, 12) for y in (4, 8, 12)}
    assert {(int(cid), int(x), int(y)) for cid, x, y, _ in baselines} == required_baselines
    for cid, _, _, rgba in baselines:
        actual = tuple(map(float, rgba.split(',')))
        assert actual[:3] == (0.0, 0.0, 0.0), (cid, 'non-fill contribution in isolated baseline')
        assert actual[3] == expected_alpha(cases[int(cid)])
    allowed = ('CAPS ', 'FLAT ', 'CREATE ', 'INVARIANT ', 'SAMPLE ', 'SAMPLE32 ',
               'LUMA ', 'BASELINE ', 'RESULT PASS ')
    assert all(line.startswith(allowed) for line in lines), 'unexpected fill output row'
    return dict(cases=len(cases), pairs=len({c['pair'] for c in cases}), samples=len(samples),
                ordinary_black_samples=len(baselines),
                fp16_code_tolerance=1, max_fp16_code_error=maximum_codes,
                pre_rt_luma_fp16_code_tolerance=1,
                pre_rt_luma_max_fp16_code_error=pre_rt_luma_codes,
                fp16_image_reconstructed_luma_max_fp16_code_error=fp16_luma_codes,
                luma_contract='Rec.709 scene-linear luma decoded from RGBA32F compatibility output; pre-RT-quantization law',
                families=sorted({family_name(c['pair']) for c in cases}))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def selected_originals(cases):
    return {(stage,shader) for c in cases for stage,shader in
            (('vs',PAIRS[c['pair']][0]),('ps',PAIRS[c['pair']][1]))}


def original_provenance(cases, programs):
    """Validate only original programs selected by this fixture case set."""
    selected = selected_originals(cases)
    inputs = {f'{stage}_{shader}.bin': sha(programs/f'{stage}_{shader}.bin')
              for stage,shader in sorted(selected)}
    if any(c['pair'] < xt_fixture.START for c in cases):
        profiles = json.loads((ROOT/'docs/reverse-engineering/linear-material-profiles.json').read_text())
        prior = {p['id']+'.bin':p['sha256'] for p in profiles['programs']}
        required = {f'{stage}_{shader}.bin' for c in cases if c['pair'] < xt_fixture.START
                    for stage,shader in (('vs',PAIRS[c['pair']][0]),('ps',PAIRS[c['pair']][1]))}
        assert all(name in prior and inputs[name] == prior[name] for name in required), 'prior original input provenance'
    expected_words = {}
    if any(xt_fixture.START <= c['pair'] < glass_fixture.START for c in cases):
        profiles = json.loads((ROOT/'docs/reverse-engineering/xt-material-profiles.json').read_text())['programs']
        expected_words.update({('ps',p['ps']):p['words'] for p in profiles})
        expected_words.update({('vs','494fe349b8bc12ec'):526,('vs','37c34a7478544c14'):768})
    expected_words.update(glass_fixture.WORDS)
    for (stage,identity),words in expected_words.items():
        if (stage,identity) not in selected: continue
        code=(programs/f'{stage}_{identity}.bin').read_bytes()
        fingerprint=14695981039346656037
        for byte in code:fingerprint=((fingerprint^byte)*1099511628211)&0xffffffffffffffff
        assert len(code)==4*words and f'{fingerprint:016x}'==identity, 'additional original identity/size'
    return inputs


def alpha_cutout_cases():
    """Two captured exact pairs; scoped fields are consumed only in cutout mode."""
    base = fixture_cases()[0]
    # AlphaValue, Glow, filter/texture kind, selected mip, UV pixel jitter, fog.
    variants = ((1.,0.,0,0,0.,0), (1.,1.,1,1,.375,0),
                (.625,.375,2,0,-.25,0), (0.,.375,1,0,0.,0),
                (1.,0.,2,1,.25,2), (.625,1.,1,1,0.,2))
    cases=[]
    for pair in (0,21):
        for depth in (0,1):
            for reverse in (0,1):
                for variant,(alpha,glow,texture,mip,jitter,fog) in enumerate(variants):
                    c=copy.deepcopy(base)
                    c.update(id=len(cases),pair=pair,depth=depth,reverse=reverse,
                             label=f'cutout_{variant}',glow=glow,flags=fog,
                             coefficients=[alpha,jitter,texture,mip])
                    # UNORM and float textures have identical constant RGB.
                    for source in ('diffuse','lightmap'):
                        c[source][:3]=[round(v*255)/255 for v in c[source][:3]]
                    cases.append(c)
    return cases


def validate_cutout_report(text,cases=None):
    """Strict selected-mode framing plus existing independent material oracle."""
    cases=alpha_cutout_cases() if cases is None else cases
    assert cases==alpha_cutout_cases(), 'cutout case contract changed'
    lines=text.splitlines()
    assert lines and lines[-1]==f'RESULT PASS cases={len(cases)}', 'missing cutout completion'
    assert not any('FAIL' in line for line in lines), 'cutout fixture failed'
    caps=[l for l in lines if l.startswith('CAPS ')]
    assert len(caps)==1 and re.fullmatch(r'CAPS mrt=\d+ vs_slots=\d+ ps_slots=\d+',caps[0])
    assert int(re.search(r'mrt=(\d+)',caps[0])[1])>=3
    creates=re.findall(r'^CREATE stage=(vs|ps) key=(\S+) instructions=(\d+) words=(\d+) completed_ms=(\S+)$',text,re.M)
    assert len(creates)==len([l for l in lines if l.startswith('CREATE ')])
    required={(stage,shader,mode,depth) for pair in (0,21) for stage,shader in zip(('vs','ps'),PAIRS[pair])
              for mode in (0,1,2) for depth in ((0,) if mode==0 else (0,1))}
    observed=set()
    for stage,key,count,words,ms in creates:
        shader,mode,depth,*gains=key.split('_')
        record=(stage,shader,int(mode),int(depth))
        assert record not in observed and record in required, 'unexpected/duplicate creation'
        observed.add(record)
        assert gains==(['1','1','1'] if int(mode)==2 else ['0','0','0'])
        limit=int(re.search(stage+r'_slots=(\d+)',caps[0])[1])
        assert 0<int(count)<=limit and int(words)>int(count)
        assert math.isfinite(float(ms)) and float(ms)>=0
    assert observed==required, 'missing original/motion/combined depth shader'
    alpha=re.findall(r'^CUTOUT_ALPHA id=(\d+) bits=([0-9a-f,]+)$',text,re.M)
    rgb=re.findall(r'^CUTOUT_RGB id=(\d+) rgb=(\S+)$',text,re.M)
    coverage=re.findall(r'^CUTOUT_COVERAGE id=(\d+) passed=(\d+) rejected=(\d+) edges=(\d+) row=([01]{16})$',text,re.M)
    twins=re.findall(r'^CUTOUT_TWIN id=(\d+) scene=(\d+) mode=(\d+) pixels=256 bad=0$',text,re.M)
    for rows,label in ((alpha,'alpha'),(rgb,'rgb'),(coverage,'coverage')):
        assert [int(r[0]) for r in rows]==[c['id'] for c in cases], 'missing/duplicate '+label
    assert [tuple(map(int,r)) for r in twins]==[(c['id'],scene,mode) for c in cases for scene in range(6) for mode in range(3)], 'missing/duplicate cutout twin'
    expected_checks=0; max_scaled=0.; threshold_rows=[]; passed_total=0
    for c,(_,bits),(_,values),(_,passed,rejected,edges,row) in zip(cases,alpha,rgb,coverage):
        tokens=bits.split(',')
        assert len(tokens)==16 and all(re.fullmatch('[0-9a-f]{8}',v) for v in tokens)
        source_alpha=[struct.unpack('<f',bytes.fromhex(v)[::-1])[0] for v in tokens]
        assert all(math.isfinite(v) and 0<=v<=1 for v in source_alpha), 'invalid native alpha'
        passed,rejected,edges=map(int,(passed,rejected,edges))
        assert passed==row.count('1')*16 and rejected==256-passed
        assert edges==sum(a!=b for a,b in zip(row,row[1:]))*16
        if c['coefficients'][0]==0:
            assert passed==0 and source_alpha==[0.]*16, 'zero AlphaValue must reject'
        else:
            assert 0<passed<256 and edges>0, 'vacuous native coverage'
        if c['label']=='cutout_0':
            threshold=struct.unpack('<f',struct.pack('<f',1/255))[0]
            pattern=[0.,math.nextafter(threshold,0.),threshold,math.nextafter(threshold,1.),1/512,1/128,.5,1.]
            # The native _pp path may retain FP32 or narrow to binary16. Bind
            # sampled indices and near-threshold input; coverage stays native.
            for i,value in enumerate(source_alpha):
                expected_value=pattern[i%8]
                assert abs(value-expected_value)<=max(2e-9,abs(ref.half(expected_value)-expected_value)+2e-9), 'wrong sampled alpha/UV'
            assert row[0]==row[8]=='0' and row[6:8]==row[14:16]=='11'
            threshold_rows.append(dict(id=c['id'],alpha_bits=tokens[:8],native_coverage=row[:8]))
        actual=list(map(float,values.split(',')))
        assert len(actual)==3 and all(map(math.isfinite,actual))
        source=copy.deepcopy(c);source['fp16']=0
        lo_result,hi_result=expected(source),expected(source,half_source=True)
        for k,value in enumerate(actual):
            lo,hi=sorted((lo_result.encoded_rgba[k],hi_result.encoded_rgba[k]))
            scaled=max(lo-value,value-hi,0)/(RGB_ABS_TOL+RGB_REL_TOL*max(abs(lo),abs(hi)))
            assert scaled<=1, 'independent combined RGB reference'
            max_scaled=max(max_scaled,scaled)
        expected_checks+=51714+1280*c['depth']+18*passed
        passed_total+=passed
    final=re.findall(r'^CUTOUT_RESULT cases=(\d+) scenes=(\d+) twins=(\d+) checks=(\d+)$',text,re.M)
    assert len(final)==1 and tuple(map(int,final[0]))==(len(cases),len(cases)*6,len(cases)*18,expected_checks), 'cutout check accounting'
    allowed=('CAPS ','CREATE ','CUTOUT_ALPHA ','CUTOUT_RGB ','CUTOUT_COVERAGE ','CUTOUT_TWIN ','CUTOUT_RESULT ','RESULT PASS ')
    assert all(l.startswith(allowed) for l in lines), 'unexpected cutout output'
    assert len(lines)==2+len(creates)+len(cases)*21+1, 'malformed/extra cutout row'
    return dict(cases=len(cases),pairs=2,unique_originals=4,shader_creations=len(creates),
                scenes=len(cases)*6,twins=len(cases)*18,checks=expected_checks,
                alpha_samples=len(cases)*256,accepted_native_pixels=passed_total,
                max_tolerance_fraction=max_scaled,native_threshold_rows=threshold_rows)


def sun_share_cases():
    selected = {}
    for case in fixture_cases():
        pixel = PAIRS[case['pair']][1]
        if case['depth'] == 1 and case['fp16'] == 1 and pixel not in selected:
            selected[pixel] = case
    assert len(selected) == 108
    cases = []
    for case in selected.values():
        normal = copy.deepcopy(case)
        normal.update(id=len(cases), label='sun_source')
        cases.append(normal)
        zero = copy.deepcopy(case)
        zero.update(id=len(cases), label='sun_zero', dir0=[0., 0., 0.])
        cases.append(zero)
    return cases


def validate_sun_share_report(text, cases=None):
    cases = sun_share_cases() if cases is None else cases
    assert 'FAIL' not in text
    def records(tag):
        return [dict(re.findall(r'(\w+)=(\S+)', line)) for line in text.splitlines() if line.startswith(tag+' ')]
    clear = records('SUN_MATERIAL_CLEAR')
    assert len(clear) == 1 and int(clear[0]['pixels']) > 0, 'missing no-draw negative control'
    assert all(int(clear[0][key]) == 0 for key in ('drawn', 'valid', 'positive', 'zero')), 'clear pixels entered acceptance'
    summaries = records('SUN_MATERIAL_PASS')
    assert len(summaries) == 1, 'missing or duplicate sun producer summary'
    summary = summaries[0]
    count = int(summary['cases'])
    keys = ('drawn', 'valid', 'positive', 'zero', 'invalid')
    totals = {key: int(summary[key]) for key in keys}
    error = float(summary['max_error'])
    assert count == len(cases) and totals['positive'] > 0 and totals['zero'] > 0
    assert math.isfinite(error) and 0 <= error <= .01
    entries = records('SUN_MATERIAL')
    rows = {int(row['id']): row for row in entries}
    assert len(entries) == len(cases) and set(rows) == {case['id'] for case in cases}
    for case in cases:
        row = rows[case['id']]
        values = {key: int(row[key]) for key in keys}
        assert int(row['pair']) == case['pair'] and all(value >= 0 for value in values.values())
        assert values['drawn'] == values['valid']+values['invalid'] and values['valid'] > 0, (case['id'], 'no valid drawn samples')
        assert values['valid'] == values['positive']+values['zero']
        row_error = float(row['max_error'])
        assert math.isfinite(row_error) and 0 <= row_error <= .01
        if case['label'] == 'sun_zero':
            assert values['positive'] == 0 and values['zero'] == values['valid'], (case['id'], 'zero sun not proved on drawn samples')
    assert all(totals[key] == sum(int(row[key]) for row in entries) for key in keys)
    assert error == max(float(row['max_error']) for row in entries)
    return dict(cases=count, drawn_pixels=totals['drawn'], valid_pixels=totals['valid'], positive_pixels=totals['positive'],
                zero_pixels=totals['zero'], invalid_pixels=totals['invalid'], clear_control_pixels=int(clear[0]['pixels']),
                max_gpu_subtraction_error=error, shader_extraction=True, live_receiver_qualification=False)


ORIGINAL_FILLS = (0.03, 0.05)
ORIGINAL_FILL_EPSILON = 1e-22
# Fill law inside the original programs (original-shading-critique.md 1a):
# base32 == S * A_eff within this envelope proves the case's lobe sum S from
# the ORIGINAL program itself (A_eff measured on the pair's calibration case).
ORIGINAL_FILL_BASE_REL_TOL, ORIGINAL_FILL_BASE_ABS_TOL = 2e-5, 1e-6
ORIGINAL_FILL_RGBA32_REL_TOL = 1e-4  # pre-store law, FP32 POW on the device


def original_fill_cases():
    """Per pair of the fill slice: a calibration face (sun averted, vertex M=1,
    so the original writes A_eff), a black face (M=0), and code-0.10/0.40
    faces. Hull, asteroid, palette and glass carry the face in the vertex
    colour, which the original PS adds to its lobe sum before the albedo
    multiply; XT adds the vertex colour after the fill site, so its faces are
    sun-lit lobes: diffuse coefficient .5, N.L .8, C0 .25 / 1.0, no specular."""
    full = fixture_cases()
    result = []
    for pair in FILL_PAIRS:
        seed = copy.deepcopy(next(row for row in full if row['pair'] == pair))
        fixed = PAIRS[pair][0] in FIXED_VERTICES
        seed.update(pair=pair, lights=1 if fixed else 0, reverse=0, affine=0, valid=1,
                    fp16=1, flags=0, gains=[1., 1., 1.],
                    diffuse=[.5, .25, .75, .75], lightmap=[0., 0., 0., .25],
                    cube=[0., 0., 0., 1.], mask=0., material=[0., 0., 0.],
                    point=[0., 0., 0.], dir0=[.375, .25, .5],
                    dir1=[0., 0., 0.], normal=[0., 0., -1.], glow=0.,
                    normal_sample=[.5, .5, 1., .5], binormal=[0., 1., 0.],
                    tangent=[1., 0., 0.], camera=[0., 0., -4.])
        xt = 148 <= pair < 162
        def add(label, **changes):
            c = copy.deepcopy(seed)
            c.update(changes, id=len(result), label='original_fill_' + label, depth=len(result) & 1)
            result.append(c)
        add('calibration', material=[1., 1., 1.], sum=[1., 1., 1.])
        add('black', sum=[0., 0., 0.])
        if xt:
            for face, colour in ((.10, .25), (.40, 1.)):
                add('face_%.2f' % face, normal=[0., .6, .8], coefficients=[.5, 0., 0., 6.],
                    dir0=[colour] * 3, sum=[face] * 3)
        else:
            for face in (.10, .40):
                add('face_%.2f' % face, material=[face] * 3, sum=[face] * 3)
    return result


def original_fill_law(sum_code, light_code, fill):
    """encode(decode(max(S,eps)) + K*decode(max(C0,eps))) per channel, float64."""
    decode = lambda v: max(v, ORIGINAL_FILL_EPSILON) ** 2.2
    return tuple(max(decode(s) + fill * decode(c), ORIGINAL_FILL_EPSILON) ** (1 / 2.2)
                 for s, c in zip(sum_code, light_code))


def validate_original_fill_report(text, cases, fill):
    """The K variant against the law at the case's lobe sum, the K=0 variant bit-exact."""
    f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]
    lines = text.splitlines()
    assert lines and lines[-1] == f'RESULT PASS cases={len(cases)}'
    assert not any('FAIL' in line for line in lines)
    invariants = re.findall(r'^INVARIANT id=(\d+) pixels=256 alpha_bad=0 motion_bad=0 depth_bad=0 rgb_bad=0 k0_bad=(\d+)$', text, re.M)
    assert [int(cid) for cid, _ in invariants] == list(range(len(cases)))
    # Measured: the K=0 variant's FP16/motion/depth pixels differing from the baseline, summed over the cases.
    k0_bad_pixels = sum(int(bad) for _, bad in invariants)
    assert k0_bad_pixels == 0, ('K=0 variant not bit-exact', k0_bad_pixels)
    rows = re.findall(r'^OFILL id=(\d+) x=(\d+) y=(\d+) base32=(\S+) base16=(\S+) fill16=(\S+) fill32=(\S+)$', text, re.M)
    assert len(rows) == 9 * len(cases)
    parsed = {}
    for cid, x, y, b32, b16, f16, f32_ in rows:
        key = (int(cid), int(x), int(y))
        assert key not in parsed and int(x) in (4, 8, 12) and int(y) in (4, 8, 12)
        parsed[key] = tuple(tuple(map(float, v.split(','))) for v in (b32, b16, f16, f32_))
    calibration = {}
    for c in cases:
        if c['label'].endswith('calibration'):
            calibration[c['pair']] = {(x, y): parsed[(c['id'], x, y)][0][:3] for x in (4, 8, 12) for y in (4, 8, 12)}
            for value in calibration[c['pair']].values():
                assert all(math.isfinite(v) and v > 0 for v in value), (c['pair'], 'calibration face reads A_eff > 0')
    max_codes = 0; max_rel32 = 0.; max_base_rel = 0.; checked = 0; xt_sums = {}
    for c in cases:
        if c['label'].endswith('calibration'):
            continue
        sum_code = tuple(f32(v) for v in c['sum']); light = tuple(f32(v) for v in c['dir0'])
        xt = 148 <= c['pair'] < 162
        for x in (4, 8, 12):
            for y in (4, 8, 12):
                base32, base16, fill16, fill32 = parsed[(c['id'], x, y)]
                albedo = calibration[c['pair']][(x, y)]
                # The original writes S*A_eff. Hull-type faces are the vertex
                # colour exactly (strict); XT sun-lit faces are lobes whose
                # exact BUMP arithmetic is not modelled here: their site sum is
                # read back from the original itself (base32/A_eff) and only
                # required near the intended face.
                measured = tuple(base32[k] / albedo[k] for k in range(3))
                wanted_sum = original_fill_law(measured if xt else sum_code, light, f32(fill))
                for k in range(3):
                    expected_base = sum_code[k] * albedo[k]
                    if xt:
                        # Lit XT faces (damage/BUMP lobes differ per technique): a
                        # positive site sum of the intended order, recorded below.
                        assert (measured[k] > .25 * sum_code[k]) if sum_code[k] else (base32[k] == 0.0), (c['id'], c['label'], k, measured[k], sum_code[k])
                        xt_sums.setdefault((c['id'], k), measured[k])
                    else:
                        tolerance = ORIGINAL_FILL_BASE_REL_TOL * abs(expected_base) + ORIGINAL_FILL_BASE_ABS_TOL
                        assert abs(base32[k] - expected_base) <= tolerance, (c['id'], c['label'], k, base32[k], expected_base)
                        max_base_rel = max(max_base_rel, abs(base32[k] - expected_base) / max(abs(expected_base), 1e-3))
                    want = wanted_sum[k] * albedo[k]
                    rel32 = abs(fill32[k] - want) / max(abs(want), 1e-3)
                    max_rel32 = max(max_rel32, rel32)
                    assert rel32 <= ORIGINAL_FILL_RGBA32_REL_TOL, (c['id'], c['label'], k, fill32[k], want, rel32)
                    codes = abs(_half_code(fill16[k]) - _half_code(ref.half(want)))
                    max_codes = max(max_codes, codes)
                    assert codes <= 1, (c['id'], c['label'], k, fill16[k], want, codes)
                assert fill16[3] == base16[3] and fill32[3] == base32[3], (c['id'], 'alpha')
                checked += 1
    creates = re.findall(r'^CREATE stage=(vs|ps) key=(\S+) instructions=(\d+) words=(\d+) completed_ms=(\S+)$', text, re.M)
    filled = sorted(key for stage, key, *_ in creates if stage == 'ps' and '_5_' in key and '_ofill_' in key)
    zero = sorted(key for stage, key, *_ in creates if stage == 'ps' and '_6_' in key and '_ofill_0' in key)
    assert len(filled) == len(set(filled)) == len(zero) == len({c['pair'] for c in cases}) * 2, 'one K and one K=0 PS per pair and depth'
    allowed = ('CAPS ', 'CREATE ', 'INVARIANT ', 'OFILL ', 'RESULT PASS ')
    assert all(line.startswith(allowed) for line in lines), 'unexpected original-fill output row'
    return dict(cases=len(cases), pairs=len({c['pair'] for c in cases}), samples=checked, filled_ps=len(filled),
                fp16_code_tolerance=1, max_fp16_code_error=max_codes,
                rgba32_relative_tolerance=ORIGINAL_FILL_RGBA32_REL_TOL, max_rgba32_relative_error=max_rel32,
                base_relative_tolerance=ORIGINAL_FILL_BASE_REL_TOL, max_base_relative_error=max_base_rel,
                k0_bit_exact=k0_bad_pixels == 0, k0_compared_cases=len(invariants), k0_differing_pixels=k0_bad_pixels, alpha='exact',
                xt_measured_site_sums={'%d:%d' % key: value for key, value in sorted(xt_sums.items())})


def run_original_fill(args):
    cases = original_fill_cases()
    args.raw_dir.mkdir(parents=True, exist_ok=True)
    case_file = args.raw_dir / 'cases.bin'
    case_file.write_bytes(binary_cases(cases))
    result_path = bottle.results_dir(ROOT) / 'original-fill-gpu.json'
    inputs = original_provenance(cases, args.programs)
    result = dict(passed=False, bottle=bottle.describe(), game_launched=False,
                  render_contract=dict(sampler_indices=[0,1,2,3,4,5,6],sampler_srgb=False,srgb_write=False,msaa=False,targets=['RGBA16F/RGBA32F','RGBA32F','R32F']),
                  scope=('--original-fill (option C): %d pairs of the fill slice x 4 faces (calibration, black, 0.10, 0.40) x K %s; the original program\'s '
                         'plain motion PS as baseline, the K=0 variant bit-exact, the K variant against the exact law at the case lobe sum with A_eff measured on the calibration face. '
                         'Detached; no live route or native Windows proof.') % (len(FILL_PAIRS), ORIGINAL_FILLS),
                  timing_scope='No benchmark in the bounded original-fill correctness slice.',
                  fills=list(ORIGINAL_FILLS), original_sha256=inputs, executable_sha256=sha(args.exe),
                  code_sha256={name:sha(ROOT/name) for name in CODE_INPUTS}, runs={})
    wine=Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
    try:
        for fill in ORIGINAL_FILLS:
            report = args.raw_dir / ('report-%g.txt' % fill)
            command=[str(wine),'--bottle',bottle.BOTTLE,'--no-update','--dll','d3d9=b',str(args.exe),'Z:'+str(args.programs),'Z:'+str(case_file),'--original-fill',repr(fill)]
            with report.open('w') as out,(args.raw_dir/('wine-%g.log' % fill)).open('w') as err:
                process=subprocess.run(command,stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=1200)
            assert process.returncode==0, 'fixture failed; see '+str(report)
            result['runs'][repr(fill)] = dict(raw_report=str(report), exit_code=process.returncode,
                                              **validate_original_fill_report(report.read_text(), cases, fill))
        assert sha(args.exe)==result['executable_sha256'], 'executable changed'
        assert result['code_sha256']=={name:sha(ROOT/name) for name in CODE_INPUTS}, 'fixture/core/reference changed during run'
        assert all(sha(args.programs/name)==value for name,value in inputs.items()), 'original changed'
        result['passed']=True
    except BaseException as error:
        result['error']=repr(error)
        raise
    finally:
        destination=result_path if result['passed'] else args.raw_dir/'failed-result.json'
        destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:v for k,v in result.items() if k in ('passed','runs','error')}))


ORIGINAL_SUN_SHARE_FILLS = (0.0, 0.05)


def original_sun_share_cases():
    """Per reviewed pixel program (108): the lit calibration face (vertex M=1,
    so 0 < s < 1), the same face with the sun constant zeroed (s = 0) and the
    sun-only face (M=0, s > 0). Fixture-case inputs otherwise as the
    original-fill slice, with the normal turned towards the light."""
    selected = {}
    for case in fixture_cases():
        pixel = PAIRS[case['pair']][1]
        if case['depth'] == 1 and case['fp16'] == 1 and pixel not in selected:
            selected[pixel] = case
    assert len(selected) == 108
    result = []
    for case in selected.values():
        seed = copy.deepcopy(case)
        fixed = PAIRS[seed['pair']][0] in FIXED_VERTICES
        seed.update(lights=1 if fixed else 0, reverse=0, affine=0, valid=1, fp16=1, depth=1, flags=0,
                    gains=[1., 1., 1.], diffuse=[.5, .25, .75, .75], lightmap=[0., 0., 0., .25],
                    cube=[0., 0., 0., 1.], mask=0., material=[0., 0., 0.], point=[0., 0., 0.],
                    dir0=[.375, .25, .5], dir1=[0., 0., 0.], normal=[0., 0., 1.], glow=0.,
                    normal_sample=[.5, .5, 1., .5], binormal=[0., 1., 0.], tangent=[1., 0., 0.], camera=[0., 0., -4.])
        def add(label, **changes):
            c = copy.deepcopy(seed)
            c.update(changes, id=len(result), label='original_sun_share_' + label)
            result.append(c)
        add('calibration', material=[1., 1., 1.])
        add('zero_sun', material=[1., 1., 1.], dir0=[0., 0., 0.])
        add('sun_only')
    return result


def validate_original_sun_share_report(text, cases, fill):
    """Colour/alpha/motion/depth identical to the control on every pixel; the
    share against the zero-sun draw within one FP16 code; zero-sun faces
    exactly 0; calibration faces strictly below 1 and positive."""
    lines = text.splitlines()
    assert lines and lines[-1] == f'RESULT PASS cases={len(cases)}'
    assert not any('FAIL' in line for line in lines)
    rows = {int(row['id']): row for row in
            (dict(re.findall(r'(\w+)=(\S+)', line)) for line in lines if line.startswith('OSHARE '))}
    assert set(rows) == {c['id'] for c in cases}
    summaries = [dict(re.findall(r'(\w+)=(\S+)', line)) for line in lines if line.startswith('OSHARE_PASS ')]
    f32 = lambda x: struct.unpack('<f', struct.pack('<f', x))[0]
    assert len(summaries) == 1 and int(summaries[0]['cases']) == len(cases) and f32(float(summaries[0]['fill'])) == f32(fill)
    max_codes = 0.; max_share = 0.; invalid = 0; positive_pixels = 0; zero_pixels = 0
    for c in cases:
        row = rows[c['id']]
        pixels = int(row['pixels'])
        assert int(row['pair']) == c['pair'] and pixels == 256
        assert (row['color_bad'], row['motion_bad'], row['depth_bad']) == ('0', '0', '0'), (c['id'], c['label'])
        assert int(row['invalid']) == 0, (c['id'], c['label'], 'invalid share sentinel on a drawn pixel')
        codes = float(row['max_codes']); assert math.isfinite(codes) and codes <= 1., (c['id'], c['label'], codes)
        max_codes = max(max_codes, codes); max_share = max(max_share, float(row['max_share']))
        positive, zero, below_one = (int(row[k]) for k in ('positive', 'zero', 'below_one'))
        assert positive + zero == pixels
        if c['label'].endswith('zero_sun'):
            assert positive == 0 and zero == pixels, (c['id'], 'zero-sun face must read s = 0')
        elif c['label'].endswith('calibration'):
            assert below_one == pixels and positive == pixels, (c['id'], 'emissive face must read 0 < s < 1')
        else:
            assert positive == pixels, (c['id'], 'sun-only face must read s > 0')
        positive_pixels += positive; zero_pixels += zero; invalid += int(row['invalid'])
    creates = re.findall(r'^CREATE stage=(vs|ps) key=(\S+) instructions=(\d+) words=(\d+) completed_ms=(\S+)$', text, re.M)
    share = sorted(key for stage, key, *_ in creates if stage == 'ps' and '_8_' in key and '_oshare_' in key)
    assert len(share) == len(set(share)) == 108, 'one share PS per reviewed pixel program'
    instructions = {key: int(count) for stage, key, count, *_ in creates if stage == 'ps' and '_oshare_' in key}
    allowed = ('CAPS ', 'CREATE ', 'OSHARE ', 'OSHARE_PASS ', 'RESULT PASS ')
    assert all(line.startswith(allowed) for line in lines), 'unexpected original-sun-share output row'
    return dict(cases=len(cases), programs=108, pixels_per_case=256, invalid_pixels=invalid, positive_pixels=positive_pixels, zero_pixels=zero_pixels,
                fp16_code_tolerance=1, max_fp16_code_error=max_codes, max_share=max_share,
                colour_alpha_motion_depth='byte-identical to the control on every pixel (FP16 and RGBA32F)',
                max_share_ps_instructions=max(instructions.values()), share_ps=len(share))


def run_original_sun_share(args):
    cases = original_sun_share_cases()
    args.raw_dir.mkdir(parents=True, exist_ok=True)
    case_file = args.raw_dir / 'share-cases.bin'
    case_file.write_bytes(binary_cases(cases))
    result_path = bottle.results_dir(ROOT) / 'original-sun-share-gpu.json'
    inputs = original_provenance(cases, args.programs)
    result = dict(passed=False, bottle=bottle.describe(), game_launched=False,
                  render_contract=dict(sampler_indices=[0,1,2,3,4,5,6],sampler_srgb=False,srgb_write=False,msaa=False,targets=['RGBA16F/RGBA32F','RGBA32F','G32R32F']),
                  scope=('--original-sun-share (legacy-sun-application.md 3.2): 108 reviewed original PS x 3 faces (lit calibration M=1, zero sun, sun only) x K %s; '
                         'the share variant against the motion PS (K=0) or the fill PS (K>0): colour/alpha/motion/depth byte-identical, oC2.g against a zero-sun control '
                         'draw within one FP16 code of Y(C). Detached; no live route, lane or native Windows proof.') % (ORIGINAL_SUN_SHARE_FILLS,),
                  timing_scope='No benchmark in the bounded original-sun-share correctness slice.',
                  fills=list(ORIGINAL_SUN_SHARE_FILLS), original_sha256=inputs, executable_sha256=sha(args.exe),
                  code_sha256={name:sha(ROOT/name) for name in CODE_INPUTS}, runs={})
    wine=Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
    try:
        for fill in ORIGINAL_SUN_SHARE_FILLS:
            report = args.raw_dir / ('share-report-%g.txt' % fill)
            command=[str(wine),'--bottle',bottle.BOTTLE,'--no-update','--dll','d3d9=b',str(args.exe),'Z:'+str(args.programs),'Z:'+str(case_file),'--original-sun-share',repr(fill)]
            with report.open('w') as out,(args.raw_dir/('share-wine-%g.log' % fill)).open('w') as err:
                process=subprocess.run(command,stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=1800)
            assert process.returncode==0, 'fixture failed; see '+str(report)
            result['runs'][repr(fill)] = dict(raw_report=str(report), exit_code=process.returncode,
                                              **validate_original_sun_share_report(report.read_text(), cases, fill))
        assert sha(args.exe)==result['executable_sha256'], 'executable changed'
        assert result['code_sha256']=={name:sha(ROOT/name) for name in CODE_INPUTS}, 'fixture/core/reference changed during run'
        assert all(sha(args.programs/name)==value for name,value in inputs.items()), 'original changed'
        result['passed']=True
    except BaseException as error:
        result['error']=repr(error)
        raise
    finally:
        destination=result_path if result['passed'] else args.raw_dir/'share-failed-result.json'
        destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:v for k,v in result.items() if k in ('passed','runs','error')}))


def parse_arguments(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True, help='Previously built fixture EXE; this runner never builds')
    parser.add_argument('--programs', type=Path, default=PROGRAMS)
    parser.add_argument('--raw-dir', type=Path, default=Path('/tmp/x3-linear-material-gpu'))
    selection=parser.add_mutually_exclusive_group()
    selection.add_argument('--alpha-test-cutout',action='store_true',help='Only the two selected native cutout pairs; actual alpha/depth/stencil MRT twins')
    selection.add_argument('--glass-only', action='store_true', help='Run only new glass cases, retaining their original case IDs; skip unrelated timing passes')
    selection.add_argument('--sun-share', action='store_true', help='108 generated sun-share PS, normal and zero-sun cases; positive pixel evidence and exact color/alpha/motion/depth twins')
    selection.add_argument('--fill', action='store_true', help='Run the bounded K=0.06 sun-averted fill oracle slice')
    selection.add_argument('--original-fill', action='store_true', help='Run the original-shading fill (option C) slice at K 0.03 and 0.05: calibration/black/0.10/0.40 faces per fill pair, K=0 bit-exact')
    selection.add_argument('--original-sun-share', action='store_true', help='Run the original-shading share producer slice: 108 PS x lit/zero-sun/sun-only faces at K 0 and 0.05; colour twins and oC2.g against a zero-sun draw')
    return parser.parse_args(argv)


def main():
    args = parse_arguments()
    assert bottle.BOTTLE == 'X3', 'new fixtures require X3M_FIXTURE_BOTTLE=X3'
    assert not game_running(), 'game running; fixture refused'
    if args.original_fill:
        return run_original_fill(args)
    if args.original_sun_share:
        return run_original_sun_share(args)
    cases = sun_share_cases() if args.sun_share else alpha_cutout_cases() if args.alpha_test_cutout else fill_cases() if args.fill else fixture_cases()
    if args.glass_only: cases = [c for c in cases if c['pair'] >= glass_fixture.START]
    args.raw_dir.mkdir(parents=True, exist_ok=True)
    case_file = args.raw_dir/'cases.bin'
    case_file.write_bytes(binary_cases(cases))
    report = args.raw_dir/'report.txt'
    result_path = bottle.results_dir(ROOT)/('sun-share-material-gpu.json' if args.sun_share else 'linear-alpha-test-gpu.json' if args.alpha_test_cutout else 'linear-material-fill-gpu.json' if args.fill else 'linear-glass-gpu.json' if args.glass_only else 'linear-material-gpu.json')
    inputs = original_provenance(cases, args.programs)
    result = dict(passed=False, bottle=bottle.describe(), game_launched=False,
                  render_contract=dict(sampler_indices=[0,1,2,3,4,5,6],sampler_srgb=False,srgb_write=False,msaa=False,targets=['RGBA16F/RGBA32F','RGBA32F','R32F']),
                  scope=('Six SM3 glass pairs: 254 new cases; earlier cases excluded; representative pair162 timing included. ' if args.glass_only else '168 pairs: prior148 plus all14XT and six SM3 glass, including four authored DEFAULT producer repairs. ') + 'Detached ordinary/combined numerics, alpha/motion/depth and interpolation. No live route or native Windows runtime proof.',
                  timing_scope='QPC through EVENT completion; 4 managed-buffer DrawPrimitive calls, 98,304 vertices, one Begin/EndScene, fenced setup, no readback; not GPU timestamps or game FPS. Glass-only selects pair162 with 0/8 point lights and native/motion/combined variants.',
                  tolerance=dict(rgb_relative=RGB_REL_TOL,rgb_absolute=RGB_ABS_TOL,tiny_rgb_absolute=1e-12,retained_sample_precision='float32/binary16 reference envelope',alpha='exact'),
                  original_sha256=inputs, executable_sha256=sha(args.exe), raw_report=str(report),
                  code_sha256={name:sha(ROOT/name) for name in CODE_INPUTS})
    if args.fill:
        result.update(fill=FILL,
          scope='23 sun-averted FP16 cases across 17 hull/asteroid/palette families, four XT standard/damage techniques and glass; point/material/reflection/lightmap/specular inputs zero. One-FP16-code oracle. No terraformer case because its occlusion RGB is also an additive emission source; no exhaustive program, live route or native Windows proof.',
          timing_scope='No benchmark in the bounded fill correctness slice.',
          tolerance=dict(fp16_code_distance=1, alpha='exact'))
    if args.sun_share:
        result['scope']='108 converted PS, normal and zero-sun paired cases. Detached positive valid share and independent D0 RGB subtraction, exact color/alpha/motion/depth. No live receiver/publication or native Windows runtime proof.'
        result['timing_scope']='No benchmark; correctness readbacks only.'
        result['render_contract']['targets']=['RGBA16F','RGBA32F','G32R32F']
    if args.alpha_test_cutout:
        result['scope']='Two captured Argon pairs, GE/ref1, mask7, blend off. Detached native/motion/combined coverage, retained alpha, hardware depth/stencil and poisoned MRT twins. No production gate/live TAA/native Windows qualification.'
        result['timing_scope']='No benchmark in this detached correctness mode; runtime route cost remains unmeasured.'
        result['render_contract'].update(alpha_test=True,alpha_function=7,alpha_reference=1,color_mask0=7,
          hardware_depth='D24S8',hardware_depth_functions=['LESSEQUAL','EQUAL probes'],stencil='off and diagnostic INCRSAT/DECRSAT twins',fixed_function_fog=False,dither=False)
    wine=Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')
    command=[str(wine),'--bottle',bottle.BOTTLE,'--no-update','--dll','d3d9=b',str(args.exe),'Z:'+str(args.programs),'Z:'+str(case_file)]
    if args.alpha_test_cutout: command.append('--alpha-test-cutout')
    if args.fill: command.extend(('--fill', str(FILL)))
    if args.sun_share: command.append('--sun-share')
    try:
        with report.open('w') as out,(args.raw_dir/'wine.log').open('w') as err:
            process=subprocess.run(command,stdout=out,stderr=err,env=dict(os.environ,WINEDLLOVERRIDES='d3d9=b'),timeout=1200)
        result['exit_code']=process.returncode
        assert process.returncode==0, 'fixture failed; see '+str(report)
        validator = validate_sun_share_report if args.sun_share else validate_cutout_report if args.alpha_test_cutout else validate_fill_report if args.fill else validate_report
        result.update(validator(report.read_text(),cases))
        assert sha(args.exe)==result['executable_sha256'], 'executable changed'
        assert result['code_sha256']=={name:sha(ROOT/name) for name in CODE_INPUTS}, 'fixture/core/reference changed during run'
        assert all(sha(args.programs/name)==value for name,value in inputs.items()), 'original changed'
        result['passed']=True
    except BaseException as error:
        result['error']=repr(error)
        raise
    finally:
        # Retain the checked-in accepted result when this run fails. Failure
        # diagnostics belong beside its local raw output until resolved.
        destination=result_path if result['passed'] else args.raw_dir/'failed-result.json'
        destination.parent.mkdir(parents=True,exist_ok=True)
        destination.write_text(json.dumps(result,indent=2)+'\n')
        print(json.dumps({k:v for k,v in result.items() if k in ('passed','cases','samples','error','max_tolerance_fraction')}))


if __name__ == '__main__':
    main()
