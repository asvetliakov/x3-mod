#!/usr/bin/env python3
"""Stored-density fog fixtures: host build, Wine run, host check.

Two executables share one run: the shader numerics fixture (checkpoint 2) and the production
FogPass fixture (checkpoint 3: cache manager, worker, slab uploads, ramps, Reset, state).

  build  --output DIR [--asset-data DIR]  i686 MinGW builds; refuses stale shader fragments. The pass fixture
                                          links the legacy family packets; without --asset-data they are baked
                                          into DIR/fog_field by tools/build/bake_fog_fields.py
  run    --output DIR --reference REFDIR  only under X3M_FIXTURE_BOTTLE=X3 wine_lock.py (both executables, in turn)
  check  --output DIR --reference REFDIR  GPU readbacks versus tools/analysis/fog_density_shader_reference.py
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
NOISE_MARGIN = 2e-3  # L3: pixels whose interleaved-gradient frac() argument is this close to a wrap are not compared
SOURCES = [ROOT / 'verification/probe/fog_density_shader_fixture.cpp', ROOT / 'src/fog/fog_density_generator.cpp']
PASS_SOURCES = [ROOT / 'verification/probe/fog_density_pass_fixture.cpp', ROOT / 'src/renderer/fog_pass.cpp', ROOT / 'src/fog/fog_density_cache.cpp',
                ROOT / 'src/fog/fog_density_generator.cpp', ROOT / 'src/renderer/fog_field_assets.cpp']
PASS_INPUTS = ['src/renderer/fog_pass.h', 'src/renderer/fog_look_math.h', 'src/fog/fog_density_cache.h', 'src/fog/fog_density_generator.h', 'src/proxy/cpu_state.h', 'verification/probe/fog_density_cpu_march.h',
               'src/renderer/fog_march_program_inc.h', 'src/renderer/fog_composite_program_inc.h', 'src/renderer/quad_vertex_program.h']
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
    for name in PROGRAMS + LOOK_PROGRAMS:
        record = json.loads((ROOT / f'verification/results/{name}-program.json').read_text())
        header = slots.PROGRAMS[name.replace('-', '_')]
        if record['source_sha256'] != digest(ROOT / record['source']) or record['header_sha256'] != digest(header):
            raise ValueError(f'{name}: stale embedded shader; rerun tools/shaders/generate_rigid_motion_pixel.py')
        if any(digest(ROOT / p) != h for p, h in (record.get('includes') or {}).items()):
            raise ValueError(f'{name}: stale shader include')
        records[name] = dict(bytecode_sha256=record['bytecode_sha256'], **slots.count(slots.words_of(header)))
    return records


def build(out, assets=None):
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
    record = dict(executable_sha256=digest(exe), pass_executable_sha256=digest(pass_exe), command=command, pass_command=pass_command, shaders=shaders,
                  legacy_packets={name: digest(data / (name + '.fogbin')) for name in ('bluewell', 'foggreenoutlands')},
                  inputs={str(p.relative_to(ROOT)): digest(p) for p in dict.fromkeys(inputs)})
    (out / 'build.json').write_text(json.dumps(record, indent=2) + '\n')
    return record


def windows(path):
    return 'Z:' + str(Path(path).resolve()).replace('/', '\\')


def run(out, reference):
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        raise ValueError('fixture requires X3M_FIXTURE_BOTTLE=X3')
    exe = out / 'fog_density_shader_fixture.exe'
    built = json.loads((out / 'build.json').read_text())
    if digest(exe) != built['executable_sha256'] or (out / 'stdout.txt').exists():
        raise ValueError('executable changed since build, or this output already holds a run')
    images = out / 'images'; images.mkdir()
    command = [bottle.WINE, *bottle.wine_args(), str(exe), windows(reference / 'cases.txt'), windows(images)]
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
                  executable_sha256=built['executable_sha256'], pass_executable_sha256=built['pass_executable_sha256'], cases_sha256=digest(reference / 'cases.txt'))
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
    for tag in ('FILL', 'PASS_VS_CPU', 'STEADY', 'RECENTRE', 'SEAM', 'SHAFTS', 'RESET_REUPLOAD', 'REPAIR', 'DETACH', 'DEVICE_REFERENCES', 'PREPARE_CPU', 'STATIC_GENERATION', 'REFUSAL'):
        found = re.search(r'^%s (.*)$' % tag, text, re.M)
        rows[tag.lower()] = numbers(found.group(1)) if found else None
    rows['seam_recentres'] = [numbers(m) for m in re.findall(r'^SEAM_RECENTRE (.*)$', text, re.M)]
    differing = [int(v) for v in re.findall(r'^ATLAS \S+ level=\d differing_bytes=(\d+)', text, re.M)]
    passed = bool(result) and execution.get('pass_returncode') == 0 and all(s == 'PASS' for _, s in fixture_checks) and len(fixture_checks) >= PASS_CHECKS_MINIMUM \
        and int(result.group(1)) == len(fixture_checks) and int(result.group(2)) >= 100 and differing and not any(differing) and 'STATE_DIFF' not in text
    return passed, dict(checks=len(fixture_checks), failed=[n for n, s in fixture_checks if s != 'PASS'], state_restorations=int(result.group(2)) if result else 0,
                        atlas_comparisons=len(differing), atlas_differing_bytes=sum(differing), executable_sha256=execution.get('pass_executable_sha256'),
                        seconds=execution.get('pass_seconds'), **rows)


def check(out, reference):
    record = json.loads((reference / 'reference.json').read_text())
    execution = json.loads((out / 'execution.json').read_text())
    if digest(reference / 'reference.npz') != record['reference_sha256'] or execution['cases_sha256'] != record['cases_sha256']:
        raise ValueError('reference changed since the run')
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
    # Look presets: GPU (S,T) of each look case against look_march of the host reference, same display-scaled gates.
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
        looks[label] = row
    shadowed_l0 = image(out, 'A_look0_shadowed', 'bilinear32'); l0_fog = shadowed_l0[:, 3] < 1
    b = rows['bilinear32']
    e = rows['exact32']
    p16 = rows['bilinear16']  # the production RGBA16F (S,T) target
    pass_passed, pass_summary = pass_report(out, execution)

    def within(row, channel):
        return row['p99'] <= GATE[channel + '_p99'] and row['max'] <= GATE[channel + '_max']
    gates = dict(
        pass_fixture_passed=pass_passed,
        fixture_passed=execution['returncode'] == 0 and bool(re.search(r'^RESULT PASS', text, re.M)) and all(s == 'PASS' for _, s in fixture_checks),
        slots_below_512=all(s['slots'] < 512 for s in shaders.values()),
        march_loops_kept=all(shaders[name]['loops'] >= 1 and shaders[name]['texture_instructions'] <= 24 for name in shaders if 'march' in name or 'repair' in name),
        look_cases=len(looks) >= 9 and all(r['fogged'] > 50 and within(r[v]['T'], 'T') and within(r[v]['S'], 'S') for r in looks.values() for v in ('bilinear32', 'bilinear16')),
        look0_shadowed_black=bool(l0_fog.sum() > 50 and not shadowed_l0[l0_fog, :3].any()),
        look1_shadowed_coloured=looks['A_look1_shadowed']['shadowed_fogged_pixels'] > 50 and looks['A_look1_shadowed']['shadowed_min_S'] > 0,
        candidate_T=b['cand_T']['p99'] <= GATE['T_p99'] and b['cand_T']['max'] <= GATE['T_max'],
        parity_T=b['cand_T']['max'] <= GATE['parity_T_max'],
        dense64_T=within(b['dense_T'], 'T'), dense64_S=within(b['dense_S'], 'S'), candidate_S=within(b['cand_S'], 'S'),
        production_rgba16f_candidate=within(p16['cand_T'], 'T') and within(p16['cand_S'], 'S'),
        production_rgba16f_dense64=within(p16['dense_T'], 'T') and within(p16['dense_S'], 'S'),
        temporal=temporal['max'] <= GATE['temporal'], production_rgba16f_temporal=temporal16['max'] <= GATE['temporal'])
    reported = dict(parity_S_bilinear_max=b['cand_S']['max'], parity_S_texel_exact_max=e['cand_S']['max'])  # not gated
    summary = dict(schema=2, result='PASS' if all(gates.values()) else 'FAIL', gates=gates, reported_not_gated=reported, host_candidate_vs_dense64=host, gate_values=GATE, versus_host=rows,
                   fp16_bilinear_vs_texel_exact=dict(T=filtering, S=filtering_S), temporal_residual_vs_dense64=temporal, production_rgba16f_temporal_residual_vs_dense64=temporal16,
                   pass_fixture=pass_summary, look_presets_versus_host=looks,
                   shaders=shaders, fixture_checks=len(fixture_checks), fixture_timing_not_game_fps=timing, fixture_readback_synchronised_timing_not_game_fps=synced, fixture_march_slope_timing_not_game_fps=slope,
                   repair=dict(zip(('odd_pixels', 'fogged', 'changed'), map(int, repair.groups()[:3])), worst_vs_full_march=float(repair.group(4))) if repair else None,
                   generation=dict(atlases=int(generation.group(1)), seconds=float(generation.group(2)), nodes_per_second=float(generation.group(3))) if generation else None,
                   executable_sha256=execution['executable_sha256'], bottle=execution['bottle'], run_seconds=execution['seconds'], reference=record)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('step', choices=('build', 'run', 'check'))
    parser.add_argument('--output', type=Path, required=True); parser.add_argument('--reference', type=Path)
    parser.add_argument('--asset-data', type=Path, help='baked legacy fog packets (CMake generated/fog_field); baked on demand when absent')
    a = parser.parse_args()
    if a.step != 'build' and not a.reference:
        parser.error('--reference is required')
    result = build(a.output, a.asset_data) if a.step == 'build' else run(a.output, a.reference) if a.step == 'run' else check(a.output, a.reference)
    print(json.dumps({k: result[k] for k in ('executable_sha256', 'pass_executable_sha256', 'returncode', 'seconds', 'pass_seconds', 'result', 'gates') if k in result}))
    return 0 if result.get('returncode', 0) == 0 and result.get('result', 'PASS') == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
