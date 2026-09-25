#!/usr/bin/env python3
"""Stored-density fog fixtures: host build, Wine run, host check.

Two executables share one run: the shader numerics fixture (checkpoint 2) and the production
FogPass fixture (checkpoint 3: cache manager, worker, slab uploads, ramps, Reset, state).

  build  --output DIR [--asset-data DIR]  i686 MinGW builds; refuses stale shader fragments. The pass fixture
                                          links the legacy family packets; without --asset-data they are baked
                                          into DIR/fog_field by tools/build/bake_fog_fields.py
  run    --output DIR --reference REFDIR --scale4-reference QDIR   only under X3M_FIXTURE_BOTTLE=X3 wine_lock.py
  check  --output DIR --reference REFDIR --scale4-reference QDIR   GPU readbacks versus tools/analysis/fog_density_shader_reference.py

The default march spacing is 4 (quarter resolution, docs/architecture/fog-gpu-cost.md step C, default since Run 77 C2);
spacing 2 is the opt-out. The default gate set is the base cases of REFDIR (unshaped parity, invalid and empty depth,
the dust motes and the FogPass fixture: programs without a spacing variant) plus the default look against QDIR; the
scale-2 opt-out is the s2_* variant set against REFDIR's look. (The 24-far-bin variants of step B and the visibility grid
went with --fog-far-bins and --fog-shadow-pass on 2026-09-25, and their gates with them.)

--reference REFDIR (required): the exporter's default (40 far bins, --march-scale 2) reference: the base cases and the
scale-2 look (s2_look_*, s2_repair_shaft_lookup, s2_pass_off_bit_identical).

--scale4-reference QDIR (required by run and check; docs/architecture/fog-gpu-cost.md step C): the default look, marched at
quarter resolution. `build` generates QDIR once with the exporter's --march-scale 4 (an existing QDIR is kept and must be a
march_scale=4 reference of the same packets); `run` draws its cases with the *_q4 programs into a 64x36 target, the look
repair split at spacing 4 and the depth-edge chain at both spacings; `check` gates them as the default look (look_*,
repair_shaft_lookup, pass_off_bit_identical) and as q4_* (programs, how far spacing 4 moves the image from spacing 2: the
look cases upsampled to the 256x144 screen by the composite's bilinear law, the depth-edge chain against its
full-resolution truth; record q4.json).
"""
import argparse
import hashlib
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
Q4_PROGRAMS = tuple(name.replace('_', '-') for name in slots.Q4_PROGRAMS)  # step C: the look marched at quarter resolution
CENSUS_PROGRAMS = tuple(name.replace('_', '-') for name in slots.CENSUS_PROGRAMS)  # step C: the --gpu-sync-timing needs-repair census
NOISE_MARGIN = 2e-3  # shaft lookup offset: pixels whose interleaved-gradient frac() argument is this close to a wrap are not compared
SOURCES = [ROOT / 'verification/probe/fog_density_shader_fixture.cpp', ROOT / 'src/fog/fog_density_generator.cpp']
PASS_SOURCES = [ROOT / 'verification/probe/fog_density_pass_fixture.cpp', ROOT / 'src/renderer/fog_pass.cpp', ROOT / 'src/fog/fog_density_cache.cpp',
                ROOT / 'src/fog/fog_density_generator.cpp', ROOT / 'src/renderer/fog_field_assets.cpp']
PASS_INPUTS = ['src/renderer/fog_pass.h', 'src/renderer/fog_look_math.h', 'src/fog/fog_density_cache.h', 'src/fog/fog_handover.h', 'src/fog/fog_density_generator.h', 'src/proxy/cpu_state.h', 'verification/probe/fog_density_cpu_march.h',
               'src/renderer/fog_march_program_inc.h', 'src/renderer/fog_composite_program_inc.h', 'src/renderer/quad_vertex_program.h',
               # Dust motes (fog-dust-motes.md): the stage's header, programs and the fixture's CPU twin.
               'src/renderer/fog_mote_math.h', 'verification/probe/fog_dust_motes_cpu.h', 'src/renderer/fog_dust_motes_vertex_program_inc.h',
               'src/renderer/fog_dust_motes_look_program_inc.h']
MOTE_PROGRAMS = tuple(name.replace('_', '-') for name in list(slots.MOTE_PROGRAMS) + list(slots.MOTE_VERTEX))
# Pass off must stay the accepted look byte for byte: sha256 prefixes of the look images, same baked packets and reference
# poses. The default is the quarter-resolution march (fog-gpu-cost.md step C), accepted by the user in Run 77 C2 (2026-09-24):
# its look images and repair split, pinned from the programs of that flight (bytecode unchanged since), gated as
# `pass_off_bit_identical`. The scale-2 opt-out keeps the look-collapse acceptance (docs/verification/volumetric-fog.md,
# "Single look", 2026-09-22), gated as `s2_pass_off_bit_identical`.
ACCEPTED_LOOK_HASHES = {('A_look_sky', 'q4_bilinear32'): '055d048320771171', ('A_look_sky', 'q4_bilinear16'): '48f422a86b2b67be',
                        ('A_look_depth3', 'q4_bilinear32'): '055d048320771171', ('A_look_depth3', 'q4_bilinear16'): '48f422a86b2b67be',
                        ('B_look_sky', 'q4_bilinear32'): 'a5b873a22846a8d2', ('B_look_sky', 'q4_bilinear16'): '0bc84507520c9dd0',
                        ('A_look_stripes', 'q4_bilinear32'): 'c26836c25ec04bbe', ('A_look_stripes', 'q4_bilinear16'): '14066247ef4ce898',
                        ('A_look_stripes_held', 'q4_bilinear32'): '72c2848d1ea50e1b', ('A_look_stripes_held', 'q4_bilinear16'): 'b3517b336b32afc1',
                        ('repair_shafts_q4', 'full'): 'a1413ec6ad1469b3'}
SCALE2_LOOK_HASHES = {('A_look_sky', 'bilinear32'): '5a6ce47b291850ef', ('A_look_sky', 'bilinear16'): '8e3cbf876597046c',
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
GATE = dict(T_p99=.002, T_max=.003, parity_T_max=.001, S_p99=.002, S_max=.003, temporal=.003)
PASS_CHECKS_MINIMUM = 40


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def shaders_current():
    """The provenance record of each fragment must match the sources, includes and header on disk."""
    records = {}
    for name in PROGRAMS + LOOK_PROGRAMS + Q4_PROGRAMS + CENSUS_PROGRAMS + MOTE_PROGRAMS:
        record = json.loads((ROOT / f'verification/results/{name}-program.json').read_text())
        key = name.replace('-', '_'); header = slots.PROGRAMS.get(key) or slots.MOTE_VERTEX[key]
        if record['source_sha256'] != digest(ROOT / record['source']) or record['header_sha256'] != digest(header):
            raise ValueError(f'{name}: stale embedded shader; rerun tools/shaders/generate_rigid_motion_pixel.py')
        if any(digest(ROOT / p) != h for p, h in (record.get('includes') or {}).items()):
            raise ValueError(f'{name}: stale shader include')
        records[name] = dict(bytecode_sha256=record['bytecode_sha256'], **slots.count(slots.words_of(header), record.get('target', 'ps_3_0')))
    return records


def variant_reference(data, variant, march_scale=4):
    """The quarter-resolution reference: generated once by the exporter (host only, never under Wine), then kept."""
    if not (variant / 'reference.json').exists():
        if variant.exists():
            raise ValueError(f'{variant}: exists without reference.json; remove it or choose another directory')
        subprocess.run([sys.executable, str(ROOT / 'tools/analysis/fog_density_shader_reference.py'), '--asset-data', str(data), '--output', str(variant),
                        '--march-scale', str(march_scale)], check=True, stdout=subprocess.DEVNULL)
    record = json.loads((variant / 'reference.json').read_text())
    if record.get('far_bins', 40) != 40 or record.get('march_scale', 2) != march_scale or digest(variant / 'reference.npz') != record['reference_sha256'] \
            or digest(variant / 'cases.txt') != record['cases_sha256']:
        raise ValueError(f'{variant}: not an intact march_scale={march_scale} reference')
    if record['packet_sha256'] != digest(data / 'foggreenoutlands.fogbin') or record['manifest_sha256'] != digest(data / 'manifest.json'):
        raise ValueError(f'{variant}: made from other fog packets than this build')
    return record


def build(out, assets=None, scale4=None):
    out.mkdir(parents=True, exist_ok=True)
    exe = out / 'fog_density_shader_fixture.exe'
    if exe.exists():
        raise ValueError('refuse overwrite of existing fixture executable; choose a new output directory')
    shaders = shaders_current()
    command = ['i686-w64-mingw32-g++', *FLAGS, *map(str, SOURCES), '-o', str(exe), '-ld3d9', '-luser32']
    subprocess.run(command, check=True)
    inputs = SOURCES + [ROOT / 'src/fog/fog_density_generator.h', ROOT / 'src/renderer/quad_vertex_program.h',
                        ROOT / 'src/renderer/quad_vertex_program_inc.h', ROOT / 'src/renderer/fog_look_math.h', *slots.PROGRAMS.values()]
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
    scale4_record = variant_reference(data, scale4, 4) if scale4 else None
    record = dict(executable_sha256=digest(exe), pass_executable_sha256=digest(pass_exe), command=command, pass_command=pass_command, shaders=shaders,
                  scale4_reference=scale4_record,
                  legacy_packets={name: digest(data / (name + '.fogbin')) for name in ('bluewell', 'foggreenoutlands')},
                  inputs={str(p.relative_to(ROOT)): digest(p) for p in dict.fromkeys(inputs)})
    (out / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def windows(path):
    return 'Z:' + str(Path(path).resolve()).replace('/', '\\')


def run(out, reference, scale4=None):
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        raise ValueError('fixture requires X3M_FIXTURE_BOTTLE=X3')
    exe = out / 'fog_density_shader_fixture.exe'
    built = json.loads((out / 'build.json').read_text())
    if digest(exe) != built['executable_sha256'] or (out / 'stdout.txt').exists():
        raise ValueError('executable changed since build, or this output already holds a run')
    images = out / 'images'; images.mkdir()
    command = [bottle.WINE, *bottle.wine_args(), str(exe), windows(reference / 'cases.txt'), windows(images), *([windows(scale4 / 'cases.txt')] if scale4 else [])]
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
                  scale4_cases_sha256=digest(scale4 / 'cases.txt') if scale4 else None)
    (out / 'execution.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def metric(values):
    a = np.abs(np.asarray(values, np.float64)).ravel()
    return dict(count=int(a.size), p50=float(np.percentile(a, 50)), p99=float(np.percentile(a, 99)), max=float(a.max()))


def image(out, case, variant, w=W, h=H):
    data = np.fromfile(out / 'images' / f'{case}.{variant}.f32', '<f4')
    if data.size != w * h * 4 or not np.isfinite(data).all():
        raise ValueError(f'{case}.{variant}: readback extent or non-finite value')
    return data.reshape(h * w, 4)


def look_report(out, vref, tag, w, h, repair_line, repair_file):
    """The look cases of one reference drawn with one program set (images <case>.<tag><target>.f32 at w x h) against the
    host look_march, and the look repair split with shafts (fixture line `repair_line`, image `repair_file`) against the
    host march of the repaired rays (bin-centre lookup) and the offset-lookup control law (`_other`)."""
    looks = {}
    for label in sorted(k[:-2] for k in vref.files if '_look' in k and k.endswith('_S')):
        pixels = vref[label[0] + '_look_pixels']; keep = np.ones(len(pixels), bool)
        if label + '_noise_margin' in vref.files:
            keep = vref[label + '_noise_margin'] > NOISE_MARGIN
        row = dict(compared=int(keep.sum()), left_out_near_noise_wrap=int((~keep).sum()), fogged=int((vref[label + '_T'][keep] < 1).sum()),
                   reference_min_T=float(vref[label + '_T'].min()), reference_max_S=float(vref[label + '_S'].max()))
        for name in ('bilinear32', 'bilinear16'):
            gpu = image(out, label, tag + name, w, h)[pixels][keep]
            row[name] = dict(T=metric(gpu[:, 3] - vref[label + '_T'][keep]), S=metric(gpu[:, :3] - vref[label + '_S'][keep]))
        if label.endswith('_shadowed'):
            gpu = image(out, label, tag + 'bilinear32', w, h)[pixels]; fog = gpu[:, 3] < 1
            row['shadowed_fogged_pixels'] = int(fog.sum()); row['shadowed_min_S'] = float(gpu[fog, :3].min()) if fog.any() else 0.
        if label + '_centre_delta' in vref.files:  # striped occluder: how far the offset shaft lookup moves S from the bin-centre lookup
            row['shaft_offset_moves_S_max'] = float(vref[label + '_centre_delta'][keep].max()); row['shaft_offset_moves_S_pixels'] = int((vref[label + '_centre_delta'][keep] > GATE['S_max']).sum())
        looks[label] = row
    # The repair program with the striped occluder bound: it keeps the bin centres while its march offsets the lookup.
    # GPU repair output = scene x T^k + S against the host look_march of the repaired ray; `other_law` is the distance of
    # the GPU result from the offset lookup, which must be well outside the gate.
    repair = {}; scene = np.array([.25, .5, .75])
    found = re.search(r'^%s odd_pixels=(\d+) fogged=(\d+) changed=(\d+) worst_vs_bin_centre_march=(\S+)' % repair_line, (out / 'stdout.txt').read_text(errors='replace'), re.M)
    if found:
        rp = vref['A_repair_pixels']; gpu = np.fromfile(out / 'images' / f'{repair_file}.full.f32', '<f4').reshape(-1, 4)[rp][:, :3]
        expect = {key: scene * vref['A_repair_shafts' + key + '_T'][:, None].astype(np.float64) ** vref['A_repair_extinction'][None, :] + vref['A_repair_shafts' + key + '_S'] for key in ('', '_other')}
        repair = dict(odd_pixels=int(found.group(1)), fogged=int(found.group(2)), changed=int(found.group(3)), worst_vs_bin_centre_march=float(found.group(4)),
                      compared=int(len(rp)), reference_fogged=int((vref['A_repair_shafts_T'] < 1).sum()), versus_host=metric(gpu - expect['']), other_law=metric(gpu - expect['_other']))
    return looks, repair


def within(row, channel):
    return row['p99'] <= GATE[channel + '_p99'] and row['max'] <= GATE[channel + '_max']


def look_gates(prefix, looks, repair):
    """The look gates of one spacing: every case inside the display-scaled gates, the offset shaft lookup visible to them
    (it moves S by more than twice the gate on at least 5 pixels), the fully shadowed fog coloured, the repair split."""
    stripes = looks.get('A_look_stripes', {}); shadowed = looks.get('A_look_shadowed', {})
    return {prefix + 'look_cases': len(looks) >= 6 and all(r['fogged'] > 50 and within(r[v]['T'], 'T') and within(r[v]['S'], 'S') for r in looks.values() for v in ('bilinear32', 'bilinear16')),
            prefix + 'look_shaft_offset_exercised': stripes.get('shaft_offset_moves_S_max', 0) > 2 * GATE['S_max'] and stripes.get('shaft_offset_moves_S_pixels', 0) >= 5,
            prefix + 'look_shadowed_coloured': shadowed.get('shadowed_fogged_pixels', 0) > 50 and shadowed.get('shadowed_min_S', 0) > 0,
            prefix + 'repair_shaft_lookup': bool(repair) and repair['reference_fogged'] > 50 and repair['versus_host']['max'] <= GATE['S_max'] and repair['other_law']['max'] > 2 * GATE['S_max']}


def look_hashes(out, pinned):
    """sha256 prefixes of the pinned look images: the record (measured and expected) and whether all match."""
    measured = {f'{case}.{variant}': hashlib.sha256((out / 'images' / f'{case}.{variant}.f32').read_bytes()).hexdigest()[:16] for (case, variant) in pinned}
    return dict(measured=measured, expected={f'{c}.{v}': h for (c, v), h in pinned.items()}), all(measured[f'{c}.{v}'] == h for (c, v), h in pinned.items())


QW, QH = W // 2, H // 2  # the quarter-resolution march target of the 2W x 2H screen (step C)


def upsample(img, w, h, scale):
    """The composite's footprint law for one depth class: bilinear between march samples, sample p of the fixture's look
    cases sitting at full coordinate scale*p + scale/2 (its ray), read at every full pixel centre of the 2W x 2H screen,
    clamped at the edges. (S.rgb, T) as float64, shape (2H*2W, 4)."""
    a = img.reshape(h, w, 4).astype(np.float64)

    def axis(n, full):
        u = np.clip((np.arange(full) + .5 - scale / 2) / scale, 0, n - 1); i0 = np.floor(u).astype(int); i1 = np.minimum(i0 + 1, n - 1)
        return i0, i1, u - i0
    y0, y1, fy = axis(h, 2 * H); x0, x1, fx = axis(w, 2 * W)
    top = a[y0][:, x0] * (1 - fx)[None, :, None] + a[y0][:, x1] * fx[None, :, None]
    bottom = a[y1][:, x0] * (1 - fx)[None, :, None] + a[y1][:, x1] * fx[None, :, None]
    return (top * (1 - fy)[:, None, None] + bottom * fy[:, None, None]).reshape(-1, 4)


def moved_rows(a, b):
    """(N,4) S.rgb,T of two images: abs deltas over pixels fogged in either, and the count past the .003 gate."""
    dT = np.abs(a[:, 3] - b[:, 3]); dS = np.abs(a[:, :3] - b[:, :3]).max(1); fog = (a[:, 3] < 1) | (b[:, 3] < 1)
    past = int(((dT > GATE['T_max']) | (dS > GATE['S_max']))[fog].sum()) if fog.any() else 0
    return dict(pixels=int(len(a)), fogged=int(fog.sum()), T_max=float(dT[fog].max()) if fog.any() else 0., T_mean=float(dT[fog].mean()) if fog.any() else 0.,
                S_max=float(dS[fog].max()) if fog.any() else 0., S_mean=float(dS[fog].mean()) if fog.any() else 0., past_gate=past,
                past_gate_fraction=past / max(int(fog.sum()), 1), signed_T_mean=float((a[:, 3] - b[:, 3])[fog].mean()) if fog.any() else 0.)


def needs_repair_twin(depth, scale):
    """fog_density_field_inc.h needs_repair at march spacing `scale` over a (h, w, 4) depth image: a valid pixel class with no
    class-compatible sample among the 2x2 footprint taps (full pixels scale*q, upper-clamped) of nonzero bilinear weight."""
    h, w = depth.shape[:2]; r, b = depth[..., 0], depth[..., 2]
    with np.errstate(invalid='ignore'):
        geo = (r >= 0) & (r <= 1); valid = (b > 0) & (b <= 3.402823466e38)
    cls = np.where(geo, np.where(valid, 1, 2), 0)
    mw, mh = -(-w // scale), -(-h // scale)
    y, x = np.mgrid[0:h, 0:w]; hx, hy = x / scale, y / scale; bx, by = np.floor(hx).astype(int), np.floor(hy).astype(int); fx, fy = hx - bx, hy - by
    compatible = np.zeros((h, w), bool)
    for dx, dy, wt in ((0, 0, (1 - fx) * (1 - fy)), (1, 0, fx * (1 - fy)), (0, 1, (1 - fx) * fy), (1, 1, fx * fy)):
        qx, qy = np.minimum(bx + dx, mw - 1), np.minimum(by + dy, mh - 1)
        compatible |= (wt > 0) & (cls[qy * scale, qx * scale] == cls)
    return (cls < 2) & ~compatible, cls


def edge_report(out, k):
    """The depth-edge chain (fixture `edge.*`): spacing 2 and 4 against the full-resolution truth and against each other."""
    w, h = 2 * W, 2 * H
    depth = np.fromfile(out / 'images' / 'edge.depth.f32', '<f4').reshape(h, w, 4).astype(np.float64)
    truth = np.fromfile(out / 'images' / 'edge.truth.f32', '<f4').reshape(h, w, 4).astype(np.float64)
    truth_S, truth_Tk = truth[..., :3], np.power(np.maximum(truth[..., 3:], 1e-6), np.asarray(k, np.float64)[None, None, :])
    got = {}
    for s in (2, 4):
        o0 = np.fromfile(out / 'images' / f'edge.s{s}.scene0.f32', '<f4').reshape(h, w, 4).astype(np.float64)
        o1 = np.fromfile(out / 'images' / f'edge.s{s}.scene1.f32', '<f4').reshape(h, w, 4).astype(np.float64)
        got[s] = (o0[..., :3], o1[..., :3] - o0[..., :3])  # S, T^k per channel
    needs = {s: needs_repair_twin(depth, s)[0] for s in (2, 4)}; _, cls = needs_repair_twin(depth, 2)
    # Edge band: within 4 px (Chebyshev) of another depth class (class band), or, for geometry, of a geometry depth more than
    # 5 % away (depth band: the class law serves such a pixel from the other depth when no sample of its own depth is near).
    b = np.where(cls == 1, depth[..., 2], np.nan); class_band = np.zeros((h, w), bool); depth_band = np.zeros((h, w), bool)
    for dy in range(-4, 5):
        for dx in range(-4, 5):
            ys, xs = np.clip(np.arange(h) + dy, 0, h - 1), np.clip(np.arange(w) + dx, 0, w - 1)
            c2, b2 = cls[ys][:, xs], b[ys][:, xs]
            class_band |= c2 != cls
            with np.errstate(invalid='ignore'):
                depth_band |= (cls == 1) & (c2 == 1) & (np.abs(b2 - b) > .05 * np.minimum(b, b2))
    band = class_band | depth_band
    fog = (truth[..., 3] < 1)
    text = (out / 'stdout.txt').read_text(errors='replace'); found = re.search(r'^EDGE width=\d+ height=\d+ repaired_s2=(\d+) repaired_s4=(\d+)', text, re.M)

    def stats(mask, S, Tk, S_ref, Tk_ref):
        dS = np.abs(S - S_ref).max(-1)[mask]; dT = np.abs(Tk - Tk_ref).max(-1)[mask]
        if not dS.size:
            return dict(pixels=0)
        past = (dS > GATE['S_max']) | (dT > GATE['T_max'])
        return dict(pixels=int(mask.sum()), T_max=float(dT.max()), T_mean=float(dT.mean()), S_max=float(dS.max()), S_mean=float(dS.mean()),
                    past_gate=int(past.sum()), past_gate_fraction=float(past.mean()))
    rows = {}
    for label, mask in (('all_fogged', fog), ('edge_band', band & fog), ('class_edge_band', class_band & fog), ('geometry_depth_edge_band', depth_band & ~class_band & fog),
                        ('off_band', ~band & fog), ('needs_repair_s4', needs[4] & fog), ('served_s4_in_band', band & fog & ~needs[4])):
        rows[label] = dict(s2_vs_truth=stats(mask, *got[2], truth_S, truth_Tk), s4_vs_truth=stats(mask, *got[4], truth_S, truth_Tk),
                           s4_vs_s2=stats(mask, *got[4], *got[2]))
    return dict(width=w, height=h, geometry_pixels=int((cls == 1).sum()), sky_pixels=int((cls == 0).sum()), edge_band_pixels=int(band.sum()),
                class_band_pixels=int(class_band.sum()), depth_band_pixels=int((depth_band & ~class_band).sum()),
                needs_repair={s: int(needs[s].sum()) for s in (2, 4)}, repaired_gpu={2: int(found.group(1)), 4: int(found.group(2))} if found else None,
                needs_fraction={s: float(needs[s].mean()) for s in (2, 4)}, rows=rows)


def q4_report(out, qref, record, shaders):
    """Step C (fog-gpu-cost.md), the spacing itself: how far the default quarter-resolution march moves the image from the
    spacing-2 opt-out (look cases upsampled to the screen by the composite law; the depth-edge chain against its
    full-resolution truth), the quarter programs, and the pass fixture's spacing checks."""
    deviation = {}
    for label in sorted(k[:-2] for k in qref.files if '_look' in k and k.endswith('_S')):
        # The production FP16 march images of both spacings, upsampled to the 256x144 screen by the composite law.
        deviation[label] = moved_rows(upsample(image(out, label, 'q4_bilinear16', QW, QH), QW, QH, 4), upsample(image(out, label, 'bilinear16'), W, H, 2))
    programs = {name: shaders[name] for name in Q4_PROGRAMS + CENSUS_PROGRAMS}
    scale2 = {name: shaders[name.replace('-q4', '')] for name in Q4_PROGRAMS}
    edge = edge_report(out, qref['A_repair_extinction'])
    pass_text = (out / 'pass_stdout.txt').read_text(errors='replace')
    pass_checks = dict(re.findall(r'^CHECK ((?:q4|march_scale|needs_census)_\S+) (PASS|FAIL)\s*$', pass_text, re.M))
    gates = dict(
        q4_programs=all(r['slots'] < 512 for r in programs.values()) and all(programs[n]['loops'] == (0 if 'composite' in n else 1) for n in Q4_PROGRAMS)
        and all(programs[n]['texture_instructions'] == scale2[n]['texture_instructions'] for n in Q4_PROGRAMS),
        # The look moves (the quarter grid is a new sampling), and the depth-edge chain repairs at least as many pixels at 4 as
        # at 2 with the GPU never writing more than the host twin of needs_repair asks for.
        q4_moves_the_look=any(d['T_max'] > 0 for d in deviation.values()) and bool(edge['repaired_gpu'])
        and 0 < edge['repaired_gpu'][2] <= edge['needs_repair'][2] <= edge['needs_repair'][4] and 0 < edge['repaired_gpu'][4] <= edge['needs_repair'][4],
        # The quarter section of the pass fixture, including march_scale_default_is_quarter (FogDensityConfig's default).
        q4_pass_fixture=len(pass_checks) >= 10 and pass_checks.get('march_scale_default_is_quarter') == 'PASS' and all(s == 'PASS' for s in pass_checks.values()))
    return gates, dict(reference=record, deviation_from_scale_2=deviation, depth_edges=edge, programs=programs, scale2_programs=scale2, pass_fixture_checks=pass_checks)


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
    for tag in ('FILL', 'PASS_VS_CPU', 'Q4', 'Q4_ODD', 'NEEDS_CENSUS', 'STEADY', 'RECENTRE', 'SEAM', 'SHAFTS', 'RESET_REUPLOAD', 'REPAIR', 'DETACH', 'DEVICE_REFERENCES', 'PREPARE_CPU', 'STATIC_GENERATION', 'HANDOVER', 'REFUSAL',
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


def check(out, reference, scale4_reference=None):
    """The default gate set (march spacing 4 since Run 77 C2): the base cases of REFDIR (unshaped parity, invalid, empty,
    motes and the pass fixture; spacing-independent programs) and the default look against QDIR; the scale-2 opt-out as the
    s2_* variant set: REFDIR's look."""
    record = json.loads((reference / 'reference.json').read_text())
    execution = json.loads((out / 'execution.json').read_text())
    if digest(reference / 'reference.npz') != record['reference_sha256'] or execution['cases_sha256'] != record['cases_sha256']:
        raise ValueError('reference changed since the run')
    if (execution.get('scale4_cases_sha256') is not None) != bool(scale4_reference) or (scale4_reference and execution['scale4_cases_sha256'] != digest(scale4_reference / 'cases.txt')):
        raise ValueError('the run and --scale4-reference disagree')
    if record.get('march_scale', 2) != 2 or record.get('far_bins', 40) != 40:
        raise ValueError('--reference must be the base (scale 2, 40 far bins) reference')
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
    generation = re.search(r'^GENERATION atlases=(\d+) seconds=(\S+) nodes_per_second=(\S+)', text, re.M)
    shaders = shaders_current()
    fixture_checks = re.findall(r'^CHECK (\S+) (PASS|FAIL)', text, re.M)
    # The look of both spacings: GPU (S,T) of each look case against look_march of its host reference, same display-scaled
    # gates, and the look repair split with shafts. Scale 2 is REFDIR's look (the s2_* opt-out set); scale 4 is QDIR's, the
    # default (pinned by march_scale 4 and 40 far bins).
    s2_looks, s2_repair = look_report(out, ref, '', W, H, 'REPAIR_SHAFTS', 'repair_shafts')
    qrecord = json.loads((scale4_reference / 'reference.json').read_text())
    if (qrecord.get('march_scale'), qrecord.get('far_bins', 40)) != (4, 40) or digest(scale4_reference / 'reference.npz') != qrecord['reference_sha256']:
        raise ValueError('scale-4 reference changed since the run, or not a march_scale 4 / 40-bin reference')
    qref = np.load(scale4_reference / 'reference.npz')
    looks, look_repair = look_report(out, qref, 'q4_', QW, QH, 'REPAIR_SHAFTS_Q4', 'repair_shafts_q4')
    b = rows['bilinear32']
    e = rows['exact32']
    p16 = rows['bilinear16']  # the production RGBA16F (S,T) target
    pass_passed, pass_summary = pass_report(out, execution)
    pass_off_hashes, pass_off = look_hashes(out, ACCEPTED_LOOK_HASHES)
    s2_pass_off_hashes, s2_pass_off = look_hashes(out, SCALE2_LOOK_HASHES)
    gates = dict(
        pass_fixture_passed=pass_passed,
        fixture_passed=execution['returncode'] == 0 and bool(re.search(r'^RESULT PASS', text, re.M)) and all(s == 'PASS' for _, s in fixture_checks),
        slots_below_512=all(s['slots'] < 512 for s in shaders.values()),
        march_loops_kept=all(shaders[name]['loops'] >= 1 and shaders[name]['texture_instructions'] <= 24 for name in shaders if 'march' in name or 'repair' in name),
        candidate_T=b['cand_T']['p99'] <= GATE['T_p99'] and b['cand_T']['max'] <= GATE['T_max'],
        parity_T=b['cand_T']['max'] <= GATE['parity_T_max'],
        dense64_T=within(b['dense_T'], 'T'), dense64_S=within(b['dense_S'], 'S'), candidate_S=within(b['cand_S'], 'S'),
        production_rgba16f_candidate=within(p16['cand_T'], 'T') and within(p16['cand_S'], 'S'),
        production_rgba16f_dense64=within(p16['dense_T'], 'T') and within(p16['dense_S'], 'S'),
        temporal=temporal['max'] <= GATE['temporal'], production_rgba16f_temporal=temporal16['max'] <= GATE['temporal'],
        # The default look (spacing 4): cases, offset lookup, shadowed colour, repair split, and the accepted images byte for byte.
        **look_gates('', looks, look_repair), pass_off_bit_identical=pass_off,
        # The dust motes (fog-dust-motes.md section 5): every M_motes_* case against the twin, the off path the accepted frame.
        motes_cases=(pass_summary.get('motes_checks') or {}).get('count', 0) >= 26 and not pass_summary['motes_checks']['failed'],
        motes_programs_below_512=all(shaders[name]['slots'] < 512 for name in MOTE_PROGRAMS))
    # The default spacing's mechanics (q4_*) and the scale-2 opt-out (s2_*: its look), so both spacings keep the coverage
    # they had as default and variant.
    q4_gates, scale_record = q4_report(out, qref, qrecord, shaders)
    gates.update(q4_gates)
    gates.update(look_gates('s2_', s2_looks, s2_repair)); gates['s2_pass_off_bit_identical'] = s2_pass_off
    scale2_variant = dict(march_scale=2, reference=record, look_versus_host=s2_looks, repair_with_shafts=s2_repair, pass_off_hashes=s2_pass_off_hashes)
    reported = dict(parity_S_bilinear_max=b['cand_S']['max'], parity_S_texel_exact_max=e['cand_S']['max'])  # not gated
    summary = dict(schema=3, default_march_scale=4, result='PASS' if all(gates.values()) else 'FAIL', gates=gates, reported_not_gated=reported, host_candidate_vs_dense64=host, gate_values=GATE, versus_host=rows,
                   fp16_bilinear_vs_texel_exact=dict(T=filtering, S=filtering_S), temporal_residual_vs_dense64=temporal, production_rgba16f_temporal_residual_vs_dense64=temporal16,
                   pass_fixture=pass_summary, look_versus_host=looks, repair_with_shafts=look_repair, look_reference=qrecord, pass_off_hashes=pass_off_hashes,
                   shaders=shaders, fixture_checks=len(fixture_checks), fixture_timing_not_game_fps=timing, fixture_readback_synchronised_timing_not_game_fps=synced, fixture_march_slope_timing_not_game_fps=slope,
                   repair=dict(zip(('odd_pixels', 'fogged', 'changed'), map(int, repair.groups()[:3])), worst_vs_full_march=float(repair.group(4))) if repair else None,
                   generation=dict(atlases=int(generation.group(1)), seconds=float(generation.group(2)), nodes_per_second=float(generation.group(3))) if generation else None,
                   executable_sha256=execution['executable_sha256'], bottle=execution['bottle'], run_seconds=execution['seconds'], reference=record,
                   march_scale_record_file='q4.json', scale2_variant_file='s2.json')
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    # Each set in its own file, so summary.json stays a small tracked record; summary.json carries every gate.
    for name, prefix, body in (('q4.json', 'q4_', scale_record), ('s2.json', 's2_', scale2_variant)):
        if body:
            (out / name).write_text(json.dumps(dict(result=summary['result'], gates={k: v for k, v in gates.items() if k.startswith(prefix)}, bottle=execution['bottle'], **body), indent=2) + '\n')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('step', choices=('build', 'run', 'check'))
    parser.add_argument('--output', type=Path, required=True); parser.add_argument('--reference', type=Path)
    parser.add_argument('--asset-data', type=Path, help='baked legacy fog packets (CMake generated/fog_field); baked on demand when absent')
    parser.add_argument('--scale4-reference', type=Path, help='the default look: the quarter-resolution march reference (fog-gpu-cost.md step C); required by run and check; build generates it once when absent')
    a = parser.parse_args()
    if a.step != 'build' and not (a.reference and a.scale4_reference):
        parser.error('--reference and --scale4-reference are required')
    q = a.scale4_reference
    result = build(a.output, a.asset_data, q) if a.step == 'build' else run(a.output, a.reference, q) if a.step == 'run' else check(a.output, a.reference, q)
    print(json.dumps({k: result[k] for k in ('executable_sha256', 'pass_executable_sha256', 'returncode', 'seconds', 'pass_seconds', 'result', 'gates') if k in result}))
    return 0 if result.get('returncode', 0) == 0 and result.get('result', 'PASS') == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
