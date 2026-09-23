#!/usr/bin/env python3
"""Stored-density fog fixtures: host build, Wine run, host check.

Two executables share one run: the shader numerics fixture (checkpoint 2) and the production
FogPass fixture (checkpoint 3: cache manager, worker, slab uploads, ramps, Reset, state).

  build  --output DIR [--asset-data DIR]  i686 MinGW builds; refuses stale shader fragments. The pass fixture
                                          links the legacy family packets; without --asset-data they are baked
                                          into DIR/fog_field by tools/build/bake_fog_fields.py
  run    --output DIR --reference REFDIR  only under X3M_FIXTURE_BOTTLE=X3 wine_lock.py (both executables, in turn)
  check  --output DIR --reference REFDIR  GPU readbacks versus tools/analysis/fog_density_shader_reference.py

--variant-reference VDIR (all three steps; docs/architecture/fog-gpu-cost.md step B): the 24-far-bin look. `build`
generates VDIR once with the exporter's --far-bins 24 from the same baked packets (an existing VDIR is kept and
must be a far_bins=24 reference of the same packets); `run` draws its cases with the *_look_far24 programs; `check`
gates them against VDIR and reports how far they move from the 40-bin images. The default gates are unchanged.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import numpy as np
import bottle
import fog_density_shader_slots as slots

ROOT = Path(__file__).resolve().parents[2]
PROGRAMS = ('fog-density-march', 'fog-density-composite', 'fog-density-repair', 'fog-density-march-exact')
LOOK_PROGRAMS = tuple(name.replace('_', '-') for name in slots.LOOK_PROGRAMS)
FAR24_PROGRAMS = tuple(name.replace('_', '-') for name in slots.FAR24_PROGRAMS)  # step B: the look with 24 far bins
GRID_PROGRAMS = tuple(name.replace('_', '-') for name in slots.GRID_PROGRAMS)  # the sun-visibility slice grid (X3M_FOG_SHADOW_PASS=1)
NOISE_MARGIN = 2e-3  # shaft lookup offset: pixels whose interleaved-gradient frac() argument is this close to a wrap are not compared
SOURCES = [ROOT / 'verification/probe/fog_density_shader_fixture.cpp', ROOT / 'src/fog/fog_density_generator.cpp']
PASS_SOURCES = [ROOT / 'verification/probe/fog_density_pass_fixture.cpp', ROOT / 'src/renderer/fog_pass.cpp', ROOT / 'src/fog/fog_density_cache.cpp',
                ROOT / 'src/fog/fog_density_generator.cpp', ROOT / 'src/renderer/fog_field_assets.cpp']
PASS_INPUTS = ['src/renderer/fog_pass.h', 'src/renderer/fog_look_math.h', 'src/renderer/fog_shadow_grid.h', 'src/fog/fog_density_cache.h', 'src/fog/fog_handover.h', 'src/fog/fog_density_generator.h', 'src/proxy/cpu_state.h', 'verification/probe/fog_density_cpu_march.h',
               'src/renderer/fog_march_program_inc.h', 'src/renderer/fog_composite_program_inc.h', 'src/renderer/quad_vertex_program.h',
               # Dust motes (fog-dust-motes.md): the stage's header, programs and the fixture's CPU twin.
               'src/renderer/fog_mote_math.h', 'verification/probe/fog_dust_motes_cpu.h', 'src/renderer/fog_dust_motes_vertex_program_inc.h',
               'src/renderer/fog_dust_motes_look_program_inc.h', 'src/renderer/fog_dust_motes_grid_program_inc.h']
MOTE_PROGRAMS = tuple(name.replace('_', '-') for name in list(slots.MOTE_PROGRAMS) + list(slots.MOTE_VERTEX))
# Pass off must stay the accepted look byte for byte: sha256 prefixes of the look-collapse acceptance
# (docs/verification/volumetric-fog.md, "Single look", 2026-09-22), same baked packets and reference poses.
ACCEPTED_LOOK_HASHES = {('A_look_sky', 'bilinear32'): '5a6ce47b291850ef', ('A_look_sky', 'bilinear16'): '8e3cbf876597046c',
                        ('A_look_depth3', 'bilinear32'): '5a6ce47b291850ef', ('A_look_depth3', 'bilinear16'): '8e3cbf876597046c',
                        ('B_look_sky', 'bilinear32'): '84d78267f42bfce8', ('B_look_sky', 'bilinear16'): 'e4fa96f9500cd8cf',
                        ('A_look_stripes', 'bilinear32'): '942e53e4023007fe', ('A_look_stripes', 'bilinear16'): 'f137782af8513f83',
                        ('A_look_stripes_held', 'bilinear32'): '297f4cdb1fa90556', ('A_look_stripes_held', 'bilinear16'): '2d5d71614d4519f7',
                        ('repair_shafts', 'full'): '88d6d32842e0a067'}
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-ffp-contract=off', '-msse2', '-mfpmath=sse',
         '-mstackrealign', '-mincoming-stack-boundary=2', '-static']
W, H = 128, 72
# Display-scaled gates (orchestrator ruling 2026-09-21): in-scatter S is gated like T. Implementation identity
# rests on the texel-exact march; the bilinear parity-S figure is reported, not gated.
GATE = dict(T_p99=.002, T_max=.003, parity_T_max=.001, S_p99=.002, S_max=.003, temporal=.003,
            # The visibility grid (fog-shadow-pass.md section 7): atlas twin 2/255 per texel, GPU march/repair reading the
            # GPU atlas against the host march reading the host atlas 5e-4 on S, the in-march law at least .003 away.
            grid_atlas_lsb=2, grid_S_max=5e-4, grid_T_max=1e-6)
PASS_CHECKS_MINIMUM = 40


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def shaders_current():
    """The provenance record of each fragment must match the sources, includes and header on disk."""
    records = {}
    for name in PROGRAMS + LOOK_PROGRAMS + FAR24_PROGRAMS + GRID_PROGRAMS + MOTE_PROGRAMS:
        record = json.loads((ROOT / f'verification/results/{name}-program.json').read_text())
        key = name.replace('-', '_'); header = slots.PROGRAMS.get(key) or slots.MOTE_VERTEX[key]
        if record['source_sha256'] != digest(ROOT / record['source']) or record['header_sha256'] != digest(header):
            raise ValueError(f'{name}: stale embedded shader; rerun tools/shaders/generate_rigid_motion_pixel.py')
        if any(digest(ROOT / p) != h for p, h in (record.get('includes') or {}).items()):
            raise ValueError(f'{name}: stale shader include')
        records[name] = dict(bytecode_sha256=record['bytecode_sha256'], **slots.count(slots.words_of(header), record.get('target', 'ps_3_0')))
    return records


def variant_reference(data, variant):
    """The 24-far-bin reference: generated once by the exporter (host only, never under Wine), then kept."""
    if not (variant / 'reference.json').exists():
        if variant.exists():
            raise ValueError(f'{variant}: exists without reference.json; remove it or choose another directory')
        subprocess.run([sys.executable, str(ROOT / 'tools/analysis/fog_density_shader_reference.py'), '--asset-data', str(data), '--output', str(variant), '--far-bins', '24'],
                       check=True, stdout=subprocess.DEVNULL)
    record = json.loads((variant / 'reference.json').read_text())
    if record.get('far_bins') != 24 or digest(variant / 'reference.npz') != record['reference_sha256'] or digest(variant / 'cases.txt') != record['cases_sha256']:
        raise ValueError(f'{variant}: not an intact far_bins=24 reference')
    if record['packet_sha256'] != digest(data / 'foggreenoutlands.fogbin') or record['manifest_sha256'] != digest(data / 'manifest.json'):
        raise ValueError(f'{variant}: made from other fog packets than this build')
    return record


def build(out, assets=None, variant=None):
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'fog_density_shader_fixture.exe'
    if exe.exists():
        raise ValueError('refuse overwrite of existing fixture executable; choose a new output directory')
    shaders = shaders_current()
    command = ['i686-w64-mingw32-g++', *FLAGS, *map(str, SOURCES), '-o', str(exe), '-ld3d9', '-luser32']
    subprocess.run(command, check=True)
    inputs = SOURCES + [ROOT / 'src/fog/fog_density_generator.h', ROOT / 'src/renderer/quad_vertex_program.h',
                        ROOT / 'src/renderer/quad_vertex_program_inc.h', ROOT / 'src/renderer/fog_look_math.h', ROOT / 'src/renderer/fog_shadow_grid.h', *slots.PROGRAMS.values()]
    data = assets or out / 'fog_field'
    if not assets:
        subprocess.run([sys.executable, str(ROOT / 'tools/build/bake_fog_fields.py'), '--output-dir', str(data)], check=True, stdout=subprocess.DEVNULL)
    text = (ROOT / 'cmake/fog_field_assets.rc.in').read_text()
    for name in ('bluewell', 'foggreenoutlands'):
        text = text.replace('@X3M_FOG_%s_BIN@' % name.upper(), (data / (name + '.fogbin')).resolve().as_posix())
    (out / 'fog-fields.rc').write_text(text)
    subprocess.run(['i686-w64-mingw32-windres', '-I', str(data), str(out / 'fog-fields.rc'), '-O', 'coff', '-o', str(out / 'fog-fields.o')], check=True)
    pass_exe = out / 'fog_density_pass_fixture.exe'
    pass_command = ['i686-w64-mingw32-g++', *FLAGS, '-Wno-cast-function-type', '-Wno-misleading-indentation', '-DX3M_FOG_PASS_FIXTURE', '-I' + str(data), *map(str, PASS_SOURCES),
                    str(out / 'fog-fields.o'), '-o', str(pass_exe), '-ld3d9', '-luser32']
    subprocess.run(pass_command, check=True)
    inputs += PASS_SOURCES + [ROOT / p for p in PASS_INPUTS]
    variant_record = variant_reference(data, variant) if variant else None
    record = dict(executable_sha256=digest(exe), pass_executable_sha256=digest(pass_exe), command=command, pass_command=pass_command, shaders=shaders, variant_reference=variant_record,
                  legacy_packets={name: digest(data / (name + '.fogbin')) for name in ('bluewell', 'foggreenoutlands')},
                  inputs={str(p.relative_to(ROOT)): digest(p) for p in dict.fromkeys(inputs)})
    (out / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def windows(path):
    return 'Z:' + str(Path(path).resolve()).replace('/', '\\')


def run(out, reference, variant=None):
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        raise ValueError('fixture requires X3M_FIXTURE_BOTTLE=X3')
    exe = out / 'fog_density_shader_fixture.exe'
    built = json.loads((out / 'build.json').read_text())
    if digest(exe) != built['executable_sha256'] or (out / 'stdout.txt').exists():
        raise ValueError('executable changed since build, or this output already holds a run')
    images = out / 'images'; images.mkdir()
    command = [bottle.WINE, *bottle.wine_args(), str(exe), windows(reference / 'cases.txt'), windows(images), *([windows(variant / 'cases.txt')] if variant else [])]
    start = time.monotonic()
    done = subprocess.run(command, capture_output=True, timeout=540, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
    (out / 'stdout.txt').write_bytes(done.stdout); (out / 'stderr.txt').write_bytes(done.stderr)
    seconds = time.monotonic() - start
    pass_exe = out / 'fog_density_pass_fixture.exe'
    if digest(pass_exe) != built['pass_executable_sha256']:
        raise ValueError('pass executable changed since build')
    pass_command = [bottle.WINE, *bottle.wine_args(), str(pass_exe), windows(reference / 'cases.txt')]
    start = time.monotonic()
    passed = subprocess.run(pass_command, capture_output=True, timeout=1500, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'))
    (out / 'pass_stdout.txt').write_bytes(passed.stdout); (out / 'pass_stderr.txt').write_bytes(passed.stderr)
    record = dict(command=command, returncode=done.returncode or passed.returncode, shader_returncode=done.returncode, pass_returncode=passed.returncode,
                  seconds=seconds, pass_seconds=time.monotonic() - start, pass_command=pass_command, bottle=bottle.describe(),
                  executable_sha256=built['executable_sha256'], pass_executable_sha256=built['pass_executable_sha256'], cases_sha256=digest(reference / 'cases.txt'),
                  variant_cases_sha256=digest(variant / 'cases.txt') if variant else None)
    (out / 'execution.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def metric(values):
    a = np.abs(np.asarray(values, np.float64)).ravel()
    return dict(count=int(a.size), p50=float(np.percentile(a, 50)), p99=float(np.percentile(a, 99)), max=float(a.max()))


def image(out, case, variant):
    data = np.fromfile(out / 'images' / f'{case}.{variant}.f32', '<f4')
    if data.size != W * H * 4 or not np.isfinite(data).all():
        raise ValueError(f'{case}.{variant}: readback extent or non-finite value')
    return data.reshape(H * W, 4)


def atlas(out, name):
    """The fixture's dump of a visibility grid atlas (A8R8G8B8 bytes) as (height, width, lane) uint8, lanes r,g,b,a."""
    data = np.fromfile(out / 'images' / f'{name}.atlas.rgba8', np.uint8)
    ref = importlib.util.spec_from_file_location('fog_density_shader_reference', ROOT / 'tools/analysis/fog_density_shader_reference.py')
    module = importlib.util.module_from_spec(ref); ref.loader.exec_module(module)
    gw, gh = module.grid_extent(2 * W), module.grid_extent(2 * H)
    bgra = data.reshape(4 * gh, 4 * gw, 4)
    return bgra[..., [2, 1, 0, 3]], module


def grid_report(out, ref, execution_text):
    """The visibility grid: atlas twins, the march and repair reading the GPU atlas, the seam and the penumbra."""
    rows = {}; pixels = ref['A_look_pixels']; sx, sy = pixels % W, pixels // W
    same = {}
    for grid_name, look_name in (('A_grid_sky', 'A_look_sky'), ('A_grid_depth3', 'A_look_depth3'), ('B_grid_sky', 'B_look_sky')):
        same[grid_name] = all(np.array_equal(image(out, grid_name, v), image(out, look_name, v)) for v in ('bilinear32', 'bilinear16'))
    rows['no_cascade_identical_to_in_march'] = same
    twins = {}
    for name in ('A_grid_stripes', 'A_grid_stripes_held', 'A_grid_seam', 'A_grid_penumbra'):
        gpu, module = atlas(out, name); host = ref[name + '_atlas']
        delta = np.abs(gpu.astype(int) - host.astype(int))
        twins[name] = dict(max_lsb=int(delta.max()), texels_differing=int((delta > 0).sum()), texels=int(delta.size), gpu_mean=float(gpu.mean() / 255), host_mean=float(host.mean() / 255))
    gpu_repair_atlas, module = atlas(out, 'repair_grid_shafts'); delta = np.abs(gpu_repair_atlas.astype(int) - ref['A_repair_grid_atlas'].astype(int))
    twins['repair_grid_shafts'] = dict(max_lsb=int(delta.max()), texels_differing=int((delta > 0).sum()), texels=int(delta.size))
    rows['atlas_twin'] = twins
    # The march reading the atlas against the host march reading the host atlas; T unchanged from the in-march program.
    stripes = {}
    for label in ('A_grid_stripes',):
        for variant in ('bilinear32', 'bilinear16'):
            gpu = image(out, label, variant)[pixels]; off = image(out, 'A_look_stripes', variant)[pixels]
            stripes[variant] = dict(S=metric(gpu[:, :3] - ref[label + '_S']), T=metric(gpu[:, 3] - ref[label + '_T']), T_vs_in_march=metric(gpu[:, 3] - off[:, 3]),
                                    S_vs_in_march_law=metric(gpu[:, :3] - off[:, :3]), pixels_leaving_in_march_law=int((np.abs(gpu[:, :3] - off[:, :3]).max(1) > GATE['S_max']).sum()),
                                    fogged=int((ref[label + '_T'] < 1).sum()))
    rows['stripes'] = stripes
    # Repair: the grid repair's odd pixels against the host march of the repaired ray reading the host atlas; the
    # in-march bin-centre repair law (the `A_repair_shafts` reference) must be measurably elsewhere.
    scene = np.array([.25, .5, .75]); rp = ref['A_repair_pixels']
    gpu = np.fromfile(out / 'images' / 'repair_grid_shafts.full.f32', '<f4').reshape(-1, 4)[rp][:, :3]
    expect = {key: scene * ref[key + '_T'][:, None].astype(np.float64) ** ref['A_repair_extinction'][None, :] + ref[key + '_S'] for key in ('A_repair_grid_shafts', 'A_repair_shafts')}
    rows['repair'] = dict(versus_host=metric(gpu - expect['A_repair_grid_shafts']), in_march_law=metric(gpu - expect['A_repair_shafts']), reference_fogged=int((ref['A_repair_grid_shafts_T'] < 1).sum()),
                          pixels_leaving_in_march_law=int((np.abs(gpu - expect['A_repair_shafts']).max(1) > GATE['S_max']).sum()))
    # Seam: along every grid texel's column the largest jump between consecutive slices with the cross-fade, against the
    # hard switch (the two maps disagree by |vA - vB| = .5 at the hand-over) and the expected ramp increment.
    seam, module = atlas(out, 'A_grid_seam'); gw, gh = module.grid_extent(2 * W), module.grid_extent(2 * H)
    tx, ty, lane = module.grid_tile(np.arange(64))
    columns = np.stack([seam[ty[j] * gh:(ty[j] + 1) * gh, tx[j] * gw:(tx[j] + 1) * gw, lane[j]] for j in range(64)], 0).astype(np.float64) / 255.  # (64, gh, gw)
    jumps = np.abs(np.diff(columns, axis=0)); va, vb = .3, .8
    far = module.grid_far_width(module.TUNING['sky_cap']); expected_step = abs(vb - va) * far / (.1 * 1e5)  # band .85-.95 of x0 = 1e-5 z: 10 km along the centre ray
    # A slice's four strata sample the ramp at (k + xi) / 4 of the slice; consecutive slices in different tiles carry
    # different xi, so one step may exceed the ramp increment by a quarter of it. Near and far values from the centre
    # texel (edge rays, dir.z < 1, are still inside the band at the last slices).
    centre = columns[:, gh // 2, gw // 2]
    rows['seam'] = dict(max_slice_jump=float(jumps.max()), expected_ramp_step=float(expected_step), hard_switch_jump=abs(vb - va),
                        allowed=float(expected_step * 1.25 + GATE['grid_atlas_lsb'] / 255.), near_value=float(centre[:24].mean()), far_value=float(centre[-2:].mean()),
                        centre_column=[float(v) for v in centre])
    # Penumbra: slice 58 of the edge case; 10-90 % width of v across the light-space edge in map texels, per band of
    # blocker distance (columns), against 0.0093 d / texel (the full width of the 0.53 degree sun's penumbra).
    pen, module = atlas(out, 'A_grid_penumbra'); j = module.PENUMBRA_SLICE; t = module.grid_tile(j)
    v = pen[t[1] * gh:(t[1] + 1) * gh, t[0] * gw:(t[0] + 1) * gw, t[2]].astype(np.float64) / 255.
    qx, qy = np.meshgrid(np.arange(gw) + .5, np.arange(gh) + .5); full = np.stack((qx * 4 / (2 * W), qy * 4 / (2 * H)), -1)
    m00, m11, m20, m21 = H / (W * np.tan(np.radians(30))), 1 / np.tan(np.radians(30)), -.5 / W, .5 / H
    view = np.stack(((2 * full[..., 0] - 1 - m20) / m00, (1 - 2 * full[..., 1] - m21) / m11, np.ones_like(qx)), -1); direction = view / np.linalg.norm(view, axis=-1, keepdims=True)
    s = float(module.grid_slice_start(j, module.TUNING['sky_cap'])) + .5 * far; pos = direction * s
    x0 = 1e-5 * pos[..., 1] + 1e-6 * pos[..., 0]; gap = (module.PENUMBRA_Z0 + module.PENUMBRA_DZ * pos[..., 0] - .001 - module.PENUMBRA_SLAB) * module.PENUMBRA_RANGE
    bands = {}
    for d in (10000., 30000., 60000.):
        keep = np.abs(gap - d) < 4000.; xs = x0[keep] * 32.; vs = v[keep]  # x in map texels (N = 64: one texel = 2/64)
        order = np.argsort(xs); xs, vs = xs[order], vs[order]
        edges = np.arange(np.floor(xs.min()), np.ceil(xs.max()) + .25, .25); centres = .5 * (edges[1:] + edges[:-1])
        means = np.array([vs[(xs >= a) & (xs < b)].mean() if ((xs >= a) & (xs < b)).any() else np.nan for a, b in zip(edges[:-1], edges[1:])])
        ok = ~np.isnan(means); c, mn = centres[ok], means[ok]
        # v falls from 1 (lit, x < 0) to 0 (the slab): the first crossings of .9 and .1 from the lit side.
        def crossing(level):
            below = np.nonzero(mn <= level)[0]
            if not len(below) or below[0] == 0:
                return float('nan')
            i = below[0]; return float(c[i - 1] + (mn[i - 1] - level) / (mn[i - 1] - mn[i]) * (c[i] - c[i - 1]))
        width = crossing(.1) - crossing(.9); expected = 0.0093 * d / module.PENUMBRA_TEXEL
        bands[int(d)] = dict(texels=int(keep.sum()), width_texels=width, expected_texels=expected, ratio=width / expected if expected else float('nan'))
    rows['penumbra'] = dict(bands=bands, in_march_law_width_texels=1.0, slice=j, distance=s)
    rows['fetches_per_frame'] = {f'{w}x{h}': dict(pass_map_fetches=16 * 64 * module.grid_extent(w) * module.grid_extent(h), march_grid_fetches_ceiling=64 * ((w + 1) // 2) * ((h + 1) // 2),
                                                  in_march_map_fetches_ceiling=4 * 64 * ((w + 1) // 2) * ((h + 1) // 2), atlas_bytes=16 * module.grid_extent(w) * module.grid_extent(h) * 4)
                                 for w, h in ((1280, 768), (2560, 1440))}
    rows['fixture_checks'] = dict(grid_programs_created='CHECK grid_programs_created PASS' in execution_text, rgba8_target='CHECK format_rgba8_render_target PASS' in execution_text)
    pass_text = (out / 'pass_stdout.txt').read_text(errors='replace')
    rows['refused_grid_falls_back'] = all(f'CHECK {name} PASS' in pass_text for name in ('refused_grid_keeps_the_stored_path_available', 'refused_grid_reports_density_grid_target', 'refused_grid_draws_the_in_march_split_frame_byte_identical', 'refused_grid_stays_refused_until_detach'))
    return rows


def far24_report(out, ref, variant, text, shaders):
    """Step B (fog-gpu-cost.md): the 24-far-bin look against its own host reference under the default look gates, the
    repair split with its programs, and how far it moves from the 40-bin look: GPU far24 against GPU 40-bin images over
    every pixel of each 128x72 look case (both targets), and host far24 against host 40-bin law on the reference rays."""
    record = json.loads((variant / 'reference.json').read_text())
    if record.get('far_bins') != 24 or digest(variant / 'reference.npz') != record['reference_sha256']:
        raise ValueError('variant reference changed since the run')
    vref = np.load(variant / 'reference.npz')
    looks, deviation = {}, {}

    def moved(a, b):  # (N,4) S.rgb,T of two images: abs deltas over all pixels and over pixels fogged in either
        dT = np.abs(a[:, 3].astype(np.float64) - b[:, 3]); dS = np.abs(a[:, :3].astype(np.float64) - b[:, :3]); fog = (a[:, 3] < 1) | (b[:, 3] < 1)
        return dict(pixels=int(len(a)), fogged=int(fog.sum()), T_max=float(dT.max()), T_mean=float(dT.mean()), S_max=float(dS.max()), S_mean=float(dS.mean()),
                    T_mean_fogged=float(dT[fog].mean()) if fog.any() else 0., S_mean_fogged=float(dS[fog].mean()) if fog.any() else 0.,
                    pixels_past_gate=int(((dT > GATE['T_max']) | (dS.max(1) > GATE['S_max'])).sum()))
    for label in sorted(k[:-2] for k in vref.files if '_look' in k and k.endswith('_S')):
        pixels = vref[label[0] + '_look_pixels']; keep = np.ones(len(pixels), bool)
        if label + '_noise_margin' in vref.files:
            keep = vref[label + '_noise_margin'] > NOISE_MARGIN
        row = dict(compared=int(keep.sum()), left_out_near_noise_wrap=int((~keep).sum()), fogged=int((vref[label + '_T'][keep] < 1).sum()),
                   reference_min_T=float(vref[label + '_T'].min()), reference_max_S=float(vref[label + '_S'].max()))
        for name in ('bilinear32', 'bilinear16'):
            gpu = image(out, label, 'far24_' + name)[pixels][keep]
            row[name] = dict(T=metric(gpu[:, 3] - vref[label + '_T'][keep]), S=metric(gpu[:, :3] - vref[label + '_S'][keep]))
        if label.endswith('_shadowed'):
            gpu = image(out, label, 'far24_bilinear32')[pixels]; fog = gpu[:, 3] < 1
            row['shadowed_fogged_pixels'] = int(fog.sum()); row['shadowed_min_S'] = float(gpu[fog, :3].min()) if fog.any() else 0.
        if label + '_centre_delta' in vref.files:
            row['shaft_offset_moves_S_max'] = float(vref[label + '_centre_delta'][keep].max()); row['shaft_offset_moves_S_pixels'] = int((vref[label + '_centre_delta'][keep] > GATE['S_max']).sum())
        looks[label] = row
        same_rays = label + '_S' in ref.files and np.array_equal(ref[label[0] + '_look_pixels'], pixels)
        deviation[label] = dict(gpu_bilinear16=moved(image(out, label, 'far24_bilinear16'), image(out, label, 'bilinear16')),
                                gpu_bilinear32=moved(image(out, label, 'far24_bilinear32'), image(out, label, 'bilinear32')),
                                host=moved(np.column_stack((vref[label + '_S'], vref[label + '_T'])), np.column_stack((ref[label + '_S'], ref[label + '_T']))) if same_rays else None)
    repair = {}; scene = np.array([.25, .5, .75])
    found = re.search(r'^REPAIR_SHAFTS_FAR24 odd_pixels=(\d+) fogged=(\d+) changed=(\d+) worst_vs_bin_centre_march=(\S+)', text, re.M)
    if found:
        rp = vref['A_repair_pixels']; gpu = np.fromfile(out / 'images' / 'repair_shafts_far24.full.f32', '<f4').reshape(-1, 4)[rp][:, :3]
        expect = {key: scene * vref['A_repair_shafts' + key + '_T'][:, None].astype(np.float64) ** vref['A_repair_extinction'][None, :] + vref['A_repair_shafts' + key + '_S'] for key in ('', '_other')}
        forty = np.fromfile(out / 'images' / 'repair_shafts.full.f32', '<f4').reshape(-1, 4)[ref['A_repair_pixels']][:, :3]
        repair = dict(odd_pixels=int(found.group(1)), fogged=int(found.group(2)), changed=int(found.group(3)), worst_vs_bin_centre_march=float(found.group(4)),
                      reference_fogged=int((vref['A_repair_shafts_T'] < 1).sum()), versus_host=metric(gpu - expect['']), other_law=metric(gpu - expect['_other']),
                      moved_from_40_bins=metric(gpu - forty) if np.array_equal(rp, ref['A_repair_pixels']) else None)
    programs = {name: shaders[name] for name in FAR24_PROGRAMS}
    defaults = {name: shaders[name.replace('-far24', '')] for name in FAR24_PROGRAMS}
    pass_text = (out / 'pass_stdout.txt').read_text(errors='replace')
    pass_checks = dict(re.findall(r'^CHECK ((?:far24|far_bins)_\S+) (PASS|FAIL)\s*$', pass_text, re.M))
    shadowed = looks.get('A_look_shadowed', {}); stripes = looks.get('A_look_stripes', {})

    def within(row, channel):
        return row['p99'] <= GATE[channel + '_p99'] and row['max'] <= GATE[channel + '_max']
    gates = dict(
        far24_look_cases=len(looks) >= 6 and all(r['fogged'] > 50 and within(r[v]['T'], 'T') and within(r[v]['S'], 'S') for r in looks.values() for v in ('bilinear32', 'bilinear16')),
        # The offset lookup must be visible to the look gate: dropping it would move S by more than the gate plus this case's
        # GPU error (the default gate asks 2x the gate: the offset moves S by .0076 at 40 bins, by .0053 at 24).
        far24_look_shaft_offset_exercised=stripes.get('shaft_offset_moves_S_pixels', 0) >= 5 and bool(stripes)
        and stripes['shaft_offset_moves_S_max'] - max(stripes[v]['S']['max'] for v in ('bilinear32', 'bilinear16')) > GATE['S_max'],
        far24_look_shadowed_coloured=shadowed.get('shadowed_fogged_pixels', 0) > 50 and shadowed.get('shadowed_min_S', 0) > 0,
        far24_repair_shaft_lookup=bool(repair) and repair['reference_fogged'] > 50 and repair['versus_host']['max'] <= GATE['S_max'] and repair['other_law']['max'] > 2 * GATE['S_max'],
        far24_programs=all(r['slots'] < 512 and r['loops'] == 1 for r in programs.values())
        and [r['texture_instructions'] for r in programs.values()] == [r['texture_instructions'] for r in defaults.values()],
        far24_moves_the_look=any(d['gpu_bilinear32']['T_max'] > 0 for d in deviation.values()),
        far24_pass_fixture=len(pass_checks) >= 10 and all(s == 'PASS' for s in pass_checks.values()))
    return gates, dict(reference=record, look_versus_host=looks, deviation_from_40_bins=deviation, repair_with_shafts=repair, programs=programs,
                       default_programs=defaults, pass_fixture_checks=pass_checks)


def numbers(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        try:
            out[key] = float(value) if re.search(r'[.e]', value) else int(value)
        except ValueError:
            out[key] = value
    return out


def pass_report(out, execution):
    """The FogPass fixture's checks and measured budgets (render-thread CPU under the harness, not game FPS)."""
    text = (out / 'pass_stdout.txt').read_text(errors='replace')
    fixture_checks = re.findall(r'^CHECK (.+?) (PASS|FAIL)\s*$', text, re.M)  # a few labels contain spaces
    result = re.search(r'^RESULT PASS checks=(\d+) failures=0 state_restorations=(\d+)', text, re.M)
    rows = {}
    for tag in ('FILL', 'PASS_VS_CPU', 'FAR24', 'STEADY', 'RECENTRE', 'SEAM', 'SHAFTS', 'RESET_REUPLOAD', 'REPAIR', 'DETACH', 'DEVICE_REFERENCES', 'PREPARE_CPU', 'STATIC_GENERATION', 'HANDOVER', 'REFUSAL', 'GRID', 'GRID_REPORT', 'GRID_TOGGLE',
                'MOTES_POSES', 'MOTES_RESOURCES', 'MOTES_CALLS', 'MOTES_SKY', 'MOTES_STREAK', 'MOTES_CUT', 'MOTES_DEPTH', 'MOTES_WRAP', 'MOTES_RESET', 'MOTES_REFUSAL'):
        found = re.search(r'^%s (.*)$' % tag, text, re.M)
        rows[tag.lower()] = numbers(found.group(1)) if found else None
    rows['motes_shafts'] = [numbers(m) for m in re.findall(r'^MOTES_SHAFTS (.*)$', text, re.M)]
    # The dust motes' cases (fog-dust-motes.md section 5): the M_motes_* checks and the off-path identity.
    mote_checks = {n: s for n, s in fixture_checks if n.startswith('M_motes_') or n == 'motes_off_bit_identical'}
    rows['motes_checks'] = dict(count=len(mote_checks), failed=sorted(n for n, s in mote_checks.items() if s != 'PASS'))
    rows['seam_recentres'] = [numbers(m) for m in re.findall(r'^SEAM_RECENTRE (.*)$', text, re.M)]
    differing = [int(v) for v in re.findall(r'^ATLAS \S+ level=\d differing_bytes=(\d+)', text, re.M)]
    passed = bool(result) and execution.get('pass_returncode') == 0 and all(s == 'PASS' for _, s in fixture_checks) and len(fixture_checks) >= PASS_CHECKS_MINIMUM \
        and int(result.group(1)) == len(fixture_checks) and int(result.group(2)) >= 100 and differing and not any(differing) and 'STATE_DIFF' not in text
    return passed, dict(checks=len(fixture_checks), failed=[n for n, s in fixture_checks if s != 'PASS'], state_restorations=int(result.group(2)) if result else 0,
                        atlas_comparisons=len(differing), atlas_differing_bytes=sum(differing), executable_sha256=execution.get('pass_executable_sha256'),
                        seconds=execution.get('pass_seconds'), **rows)


def check(out, reference, far24_reference=None):
    record = json.loads((reference / 'reference.json').read_text())
    execution = json.loads((out / 'execution.json').read_text())
    if digest(reference / 'reference.npz') != record['reference_sha256'] or execution['cases_sha256'] != record['cases_sha256']:
        raise ValueError('reference changed since the run')
    if (execution.get('variant_cases_sha256') is not None) != bool(far24_reference) or (far24_reference and execution['variant_cases_sha256'] != digest(far24_reference / 'cases.txt')):
        raise ValueError('the run and --variant-reference disagree')
    text = (out / 'stdout.txt').read_text(errors='replace')
    ref = np.load(reference / 'reference.npz')
    groups = {}
    for pose in 'AB':
        groups[f'{pose}_sky'] = ref[f'{pose}_sky_pixels']
        for i in range(7):
            groups[f'{pose}_depth{i}'] = ref[f'{pose}_witness_pixels']
            if i != 3:
                groups[f'{pose}_shift{i}'] = ref[f'{pose}_witness_pixels']
    rows = {}
    for variant in ('bilinear32', 'exact32', 'bilinear16'):
        delta = {k: [] for k in ('cand_T', 'cand_S', 'dense_T', 'dense_S')}
        for case, pixels in groups.items():
            gpu = image(out, case, variant)[pixels]
            for key, name in (('cand', 'candidate'), ('dense', 'dense64')):
                delta[f'{key}_T'].append(gpu[:, 3] - ref[f'{case}_{name}_T']); delta[f'{key}_S'].append((gpu[:, :3] - ref[f'{case}_{name}_S']).ravel())
        rows[variant] = {k: metric(np.concatenate(v)) for k, v in delta.items()}
    # Hardware FP16 bilinear versus texel-exact fetches, every pixel of every fogged case.
    filtering = metric(np.concatenate([(image(out, c, 'bilinear32') - image(out, c, 'exact32'))[:, 3] for c in groups]))
    filtering_S = metric(np.concatenate([(image(out, c, 'bilinear32') - image(out, c, 'exact32'))[:, :3].ravel() for c in groups]))
    temporal = {'bilinear32': [], 'bilinear16': []}
    for pose in 'AB':
        names = [f'{pose}_sky' if i == 3 else f'{pose}_shift{i}' for i in range(7)]
        pixels = ref[f'{pose}_witness_pixels']
        def dense(name):
            t = ref[f'{name}_dense64_T']
            return t[np.searchsorted(ref[f'{pose}_sky_pixels'], pixels)] if name.endswith('_sky') else t
        for a, b in zip(names, names[1:]):
            for variant, rows_ in temporal.items():
                rows_.append((image(out, b, variant)[pixels, 3] - image(out, a, variant)[pixels, 3]) - (dense(b) - dense(a)))
    temporal16 = metric(np.concatenate(temporal['bilinear16']))
    temporal = metric(np.concatenate(temporal['bilinear32']))
    synced = {m.group(1): {k: float(v) for k, v in re.findall(r'(\w+_ms)=([0-9.]+)', m.group(0))}
              for m in re.finditer(r'^FIXTURE_SYNC_TIMING (\S+) .*$', text, re.M)}
    slope = {m.group(1): {k: float(v) for k, v in re.findall(r'(\w+_ms)=([0-9.]+)', m.group(0))}
             for m in re.finditer(r'^FIXTURE_SLOPE_TIMING (\S+) .*$', text, re.M)}
    names = [k[:-len('_candidate_S')] for k in ref.files if k.endswith('_candidate_S')]
    host = dict(T=metric(np.concatenate([ref[n + '_candidate_T'] - ref[n + '_dense64_T'] for n in names])),
                S=metric(np.concatenate([(ref[n + '_candidate_S'] - ref[n + '_dense64_S']).ravel() for n in names])))
    timing = {m.group(1): {k: float(v) for k, v in re.findall(r'(\w+_ms)=([0-9.]+)', m.group(0))}
              for m in re.finditer(r'^FIXTURE_TIMING (\S+) .*$', text, re.M)}
    repair = re.search(r'^REPAIR odd_pixels=(\d+) fogged=(\d+) changed=(\d+) worst_vs_full_march=(\S+)', text, re.M)
    # The repair program with the striped occluder bound: it keeps the bin centres while its march offsets the lookup.
    # GPU repair output = scene x T^k + S against the host look_march of the repaired ray; `other_law` is the distance of
    # the GPU result from the offset lookup, which must be well outside the gate.
    repair_shafts = {}; scene = np.array([.25, .5, .75])
    for g in re.findall(r'^REPAIR_SHAFTS odd_pixels=(\d+) fogged=(\d+) changed=(\d+) worst_vs_bin_centre_march=(\S+)', text, re.M):
        label = 'A_repair_shafts'; pixels = ref['A_repair_pixels']; keep = np.ones(len(pixels), bool)
        gpu = np.fromfile(out / 'images' / 'repair_shafts.full.f32', '<f4').reshape(-1, 4)[pixels][keep, :3]
        expect = {key: scene * ref[label + key + '_T'][keep, None].astype(np.float64) ** ref['A_repair_extinction'][None, :] + ref[label + key + '_S'][keep] for key in ('', '_other')}
        repair_shafts['look'] = dict(odd_pixels=int(g[0]), fogged=int(g[1]), changed=int(g[2]), worst_vs_bin_centre_march=float(g[3]), compared=int(keep.sum()), reference_fogged=int((ref[label + '_T'][keep] < 1).sum()),
                                     versus_host=metric(gpu - expect['']), other_law=metric(gpu - expect['_other']))
    generation = re.search(r'^GENERATION atlases=(\d+) seconds=(\S+) nodes_per_second=(\S+)', text, re.M)
    shaders = shaders_current()
    fixture_checks = re.findall(r'^CHECK (\S+) (PASS|FAIL)', text, re.M)
    # The look: GPU (S,T) of each look case against look_march of the host reference, same display-scaled gates.
    looks = {}
    for label in sorted(k[:-2] for k in ref.files if '_look' in k and k.endswith('_S')):
        pixels = ref[label[0] + '_look_pixels']; keep = np.ones(len(pixels), bool)
        if label + '_noise_margin' in ref.files:
            keep = ref[label + '_noise_margin'] > NOISE_MARGIN
        row = dict(compared=int(keep.sum()), left_out_near_noise_wrap=int((~keep).sum()), fogged=int((ref[label + '_T'][keep] < 1).sum()),
                   reference_min_T=float(ref[label + '_T'].min()), reference_max_S=float(ref[label + '_S'].max()))
        for variant in ('bilinear32', 'bilinear16'):
            gpu = image(out, label, variant)[pixels][keep]
            row[variant] = dict(T=metric(gpu[:, 3] - ref[label + '_T'][keep]), S=metric(gpu[:, :3] - ref[label + '_S'][keep]))
        if label.endswith('_shadowed'):
            gpu = image(out, label, 'bilinear32')[pixels]; fog = gpu[:, 3] < 1
            row['shadowed_fogged_pixels'] = int(fog.sum()); row['shadowed_min_S'] = float(gpu[fog, :3].min()) if fog.any() else 0.
        if label + '_centre_delta' in ref.files:  # striped occluder: how far the offset shaft lookup moves S from the bin-centre lookup
            row['shaft_offset_moves_S_max'] = float(ref[label + '_centre_delta'][keep].max()); row['shaft_offset_moves_S_pixels'] = int((ref[label + '_centre_delta'][keep] > GATE['S_max']).sum())
        looks[label] = row
    b = rows['bilinear32']
    e = rows['exact32']
    p16 = rows['bilinear16']  # the production RGBA16F (S,T) target
    pass_passed, pass_summary = pass_report(out, execution)
    grid = grid_report(out, ref, text)
    accepted = {f'{case}.{variant}': hashlib.sha256((out / 'images' / f'{case}.{variant}.f32').read_bytes()).hexdigest()[:16] for (case, variant) in ACCEPTED_LOOK_HASHES}
    grid['pass_off_hashes'] = dict(measured=accepted, expected={f'{c}.{v}': h for (c, v), h in ACCEPTED_LOOK_HASHES.items()})
    grid_bands = grid['penumbra']['bands']; widths = [grid_bands[k]['width_texels'] for k in sorted(grid_bands)]
    grid_pass_summary = pass_summary.get('grid') or {}

    def within(row, channel):
        return row['p99'] <= GATE[channel + '_p99'] and row['max'] <= GATE[channel + '_max']
    gates = dict(
        pass_fixture_passed=pass_passed,
        fixture_passed=execution['returncode'] == 0 and bool(re.search(r'^RESULT PASS', text, re.M)) and all(s == 'PASS' for _, s in fixture_checks),
        slots_below_512=all(s['slots'] < 512 for s in shaders.values()),
        march_loops_kept=all(shaders[name]['loops'] >= 1 and shaders[name]['texture_instructions'] <= 24 for name in shaders if 'march' in name or 'repair' in name),
        look_shaft_offset_exercised=looks['A_look_stripes'].get('shaft_offset_moves_S_max', 0) > 2 * GATE['S_max'] and looks['A_look_stripes']['shaft_offset_moves_S_pixels'] >= 5,
        repair_shaft_lookup=sorted(repair_shafts) == ['look'] and all(r['reference_fogged'] > 50 and r['versus_host']['max'] <= GATE['S_max'] and r['other_law']['max'] > 2 * GATE['S_max'] for r in repair_shafts.values()),
        look_cases=len(looks) >= 6 and all(r['fogged'] > 50 and within(r[v]['T'], 'T') and within(r[v]['S'], 'S') for r in looks.values() for v in ('bilinear32', 'bilinear16')),
        look_shadowed_coloured=looks['A_look_shadowed']['shadowed_fogged_pixels'] > 50 and looks['A_look_shadowed']['shadowed_min_S'] > 0,
        candidate_T=b['cand_T']['p99'] <= GATE['T_p99'] and b['cand_T']['max'] <= GATE['T_max'],
        parity_T=b['cand_T']['max'] <= GATE['parity_T_max'],
        dense64_T=within(b['dense_T'], 'T'), dense64_S=within(b['dense_S'], 'S'), candidate_S=within(b['cand_S'], 'S'),
        production_rgba16f_candidate=within(p16['cand_T'], 'T') and within(p16['cand_S'], 'S'),
        production_rgba16f_dense64=within(p16['dense_T'], 'T') and within(p16['dense_S'], 'S'),
        temporal=temporal['max'] <= GATE['temporal'], production_rgba16f_temporal=temporal16['max'] <= GATE['temporal'],
        # The visibility grid (fog-shadow-pass.md section 7, items 1-6).
        pass_off_bit_identical=all(accepted[f'{c}.{v}'] == h for (c, v), h in ACCEPTED_LOOK_HASHES.items()),
        grid_no_cascade_identical=all(grid['no_cascade_identical_to_in_march'].values()),
        grid_atlas_twin=all(t['max_lsb'] <= GATE['grid_atlas_lsb'] for t in grid['atlas_twin'].values()),
        grid_march_versus_host=all(r['S']['max'] <= GATE['grid_S_max'] and r['T']['max'] <= GATE['T_max'] and r['T_vs_in_march']['max'] <= GATE['grid_T_max'] and r['fogged'] > 50 for r in grid['stripes'].values()),
        grid_leaves_in_march_law=all(r['S_vs_in_march_law']['max'] > 2 * GATE['S_max'] and r['pixels_leaving_in_march_law'] >= 5 for r in grid['stripes'].values()),
        grid_repair_parity=grid['repair']['reference_fogged'] > 50 and grid['repair']['versus_host']['max'] <= GATE['grid_S_max'] and grid['repair']['in_march_law']['max'] > GATE['S_max'] and grid['repair']['pixels_leaving_in_march_law'] >= 5,
        grid_seam_cross_fade=grid['seam']['max_slice_jump'] <= grid['seam']['allowed'] and grid['seam']['max_slice_jump'] >= .05 and abs(grid['seam']['near_value'] - .3) <= 4 / 255 and abs(grid['seam']['far_value'] - .8) <= 4 / 255,
        grid_penumbra_widens=all(.5 <= grid_bands[k]['ratio'] <= 2 and grid_bands[k]['width_texels'] >= 1 for k in grid_bands) and widths == sorted(widths),
        grid_programs_created=all(grid['fixture_checks'].values()),
        grid_refused_falls_back_to_in_march=grid['refused_grid_falls_back'],
        grid_pass_fixture=bool(grid_pass_summary) and grid_pass_summary.get('fogged_pixels', 0) > 0 and grid_pass_summary.get('in_scatter_differs', 0) > 0,
        # The dust motes (fog-dust-motes.md section 5): every M_motes_* case against the twin, the off path the accepted frame.
        motes_cases=(pass_summary.get('motes_checks') or {}).get('count', 0) >= 29 and not pass_summary['motes_checks']['failed'],
        motes_programs_below_512=all(shaders[name]['slots'] < 512 for name in MOTE_PROGRAMS))
    far_bins_variant = None
    if far24_reference:
        far_gates, far_bins_variant = far24_report(out, ref, far24_reference, text, shaders)
        gates.update(far_gates)
    reported = dict(parity_S_bilinear_max=b['cand_S']['max'], parity_S_texel_exact_max=e['cand_S']['max'])  # not gated
    summary = dict(schema=2, result='PASS' if all(gates.values()) else 'FAIL', gates=gates, reported_not_gated=reported, host_candidate_vs_dense64=host, gate_values=GATE, versus_host=rows,
                   fp16_bilinear_vs_texel_exact=dict(T=filtering, S=filtering_S), temporal_residual_vs_dense64=temporal, production_rgba16f_temporal_residual_vs_dense64=temporal16,
                   pass_fixture=pass_summary, look_versus_host=looks, repair_with_shafts=repair_shafts, visibility_grid=grid,
                   shaders=shaders, fixture_checks=len(fixture_checks), fixture_timing_not_game_fps=timing, fixture_readback_synchronised_timing_not_game_fps=synced, fixture_march_slope_timing_not_game_fps=slope,
                   repair=dict(zip(('odd_pixels', 'fogged', 'changed'), map(int, repair.groups()[:3])), worst_vs_full_march=float(repair.group(4))) if repair else None,
                   generation=dict(atlases=int(generation.group(1)), seconds=float(generation.group(2)), nodes_per_second=float(generation.group(3))) if generation else None,
                   executable_sha256=execution['executable_sha256'], bottle=execution['bottle'], run_seconds=execution['seconds'], reference=record,
                   **({'far_bins_variant_file': 'far24.json'} if far_bins_variant else {}))
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    if far_bins_variant:  # its own file, so summary.json stays a small tracked record; the far24_* gates are in both
        far = dict(result=summary['result'], gates={k: v for k, v in gates.items() if k.startswith('far24_')}, bottle=execution['bottle'], **far_bins_variant)
        (out / 'far24.json').write_text(json.dumps(far, indent=2) + '\n')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('step', choices=('build', 'run', 'check'))
    parser.add_argument('--output', type=Path, required=True); parser.add_argument('--reference', type=Path)
    parser.add_argument('--asset-data', type=Path, help='baked legacy fog packets (CMake generated/fog_field); baked on demand when absent')
    parser.add_argument('--variant-reference', type=Path, help='the 24-far-bin reference (fog-gpu-cost.md step B); build generates it once when absent')
    a = parser.parse_args()
    if a.step != 'build' and not a.reference:
        parser.error('--reference is required')
    v = a.variant_reference
    result = build(a.output, a.asset_data, v) if a.step == 'build' else run(a.output, a.reference, v) if a.step == 'run' else check(a.output, a.reference, v)
    print(json.dumps({k: result[k] for k in ('executable_sha256', 'pass_executable_sha256', 'returncode', 'seconds', 'pass_seconds', 'result', 'gates') if k in result}))
    return 0 if result.get('returncode', 0) == 0 and result.get('result', 'PASS') == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
