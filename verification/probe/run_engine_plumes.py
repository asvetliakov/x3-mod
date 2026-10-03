#!/usr/bin/env python3
"""Engine plumes fixture (X3M_ENGINE_EFFECTS=plumes, phase 2; docs/architecture/engine-effects-modern.md sections 3-6).

Builds verification/probe/engine_plumes_fixture.cpp with the production EnginePlumesPass, the record -> vertex builder
(src/proxy/engine_plumes_core.h) and the production TemporalPass (refusing stale embedded programs: each
engine-plume-*-program.json must match its source and header), runs it once in the selected bottle with builtin D3D9
and writes <results>/engine-plumes/summary.json: bottle, the checkout's commit and the production sources' hashes,
the build, every CHECK, and the numbers of each case (lengths, widths and the law against the CPU replica for
s = 0 / 0.5 / 1, core survival and trail through the real resolve at 0 / 4 / 8 px per frame over the dark and the
flickering sky, the occlusion cuts and rim widths (centred and off-centre at 90 % of the width), the chase cap and
fade, the presets, the temporal variation, the bulge and taper, the shock cells, after flight C the end-on energy at
0 / 30 / 60 / 90 degrees, after flight D the plume floor (k x the ship's radius) and the mouth against the body at three
throttles, Reset, the FP16 refusal, the EVENT-fenced stage cost at 30 / 100 nozzles and the CPU build with and without the
plume floor; after the gap analysis' phases 2 and 3 the idle floor's length at s = 0, the nozzle spill head-on behind a
plane, the flow's world displacement at value 100 / 600 / 1,500 / 10,000 and its lag-1 correlation, the two-tone colour at
u 0.1 / 0.5 / 0.9, the RCS puff and brake flare attack, the travel look at warp 6; ps slots gated at 800; after the revised
look law the structure gates of docs/architecture/engine-exhaust-look-critique.md section 5 on the FP16 readback: radial
contrast, the body lane's cells and dark gaps, the streaks' anisotropy, the whiteness, the end-on ring and hot centre).
--disc-ab runs the timing case alone with X3M_PLUMES_FIXTURE_DISC_AB=1: the stage cost with the end-on disc drawn and
not drawn, three interleaved rounds at 30 / 100 nozzles and both sizes, into
verification/results/engine-effects/plume_disc_ab.json (the summary record is not touched).
--dump-images DIR runs the fixture's look-image mode instead of the cases (the summary record is not touched): 1920x1080
frames of the production plume and ribbon stage through the real resolve after 30 frames of warm-up, tonemapped by a CPU
port of the write-back's AgX at EV 0 (no bloom), written as PNGs (png_writer.py) into DIR with a README.md listing each
file and its parameters, and contact_sheet.png when Pillow is importable.
The fixture's stdout stays under verification/probe/build/engine-plumes/. Run through wine_lock.py with
X3M_FIXTURE_BOTTLE=X3. Never launches the game.
"""
import statistics
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import png_writer
import bottle  # CrossOver bottle selection (X3M_FIXTURE_BOTTLE) and the per-bottle results directory
from game_guard import game_running

ROOT = Path(__file__).resolve().parents[2]
PROBE = ROOT / 'verification/probe'
BUILD = PROBE / 'build/engine-plumes'
EXE = BUILD / 'engine_plumes_fixture.exe'
FLAGS = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-msse2', '-mfpmath=sse',
         '-mstackrealign', '-mincoming-stack-boundary=2', '-static', '-static-libgcc', '-static-libstdc++',
         '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-DX3M_ENGINE_PLUMES_FIXTURE']
SOURCES = ('verification/probe/engine_plumes_fixture.cpp', 'src/renderer/engine_plumes_pass.cpp',
           'src/renderer/engine_ribbons_pass.cpp', 'src/renderer/temporal_pass.cpp')  # the ribbons: --dump-images only
# The production sources the fixture exercises: their content hashes and the checkout's commit go into the record
# (test_engine_plumes compares them with the tree).
PRODUCTION_SOURCES = ('src/proxy/engine_plumes_core.h', 'src/proxy/engine_effects_core.h', 'src/renderer/fog_transmittance.h',
                      'src/renderer/engine_plumes_pass.h',
                      'src/renderer/engine_plumes_pass.cpp', 'src/effects/engine_plume_vs.hlsl', 'src/effects/engine_plume_ps.hlsl',
                      'src/renderer/engine_plume_vertex_program_inc.h', 'src/renderer/engine_plume_pixel_program_inc.h',
                      'src/renderer/temporal_pass.h', 'src/renderer/temporal_pass.cpp')
PROGRAMS = {'engine_plume_vs': ('verification/results/engine-plume-vertex-program.json', 'src/renderer/engine_plume_vertex_program_inc.h'),
            'engine_plume_ps': ('verification/results/engine-plume-pixel-program.json', 'src/renderer/engine_plume_pixel_program_inc.h')}
GATES = {'core_survival': 0.9, 'trail_dark_px': 3, 'stage_gpu_ms_advisory': 0.1, 'build_100_ms': 0.1, 'ps_slots': 800}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_binding():
    """The checkout's commit (-dirty when tracked files differ from it), whether the exercised production sources
    differ from it, and their content hashes."""
    def git(*args):
        return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, check=True).stdout
    changed = [line[3:] for line in git('status', '--porcelain', '--', *PRODUCTION_SOURCES).splitlines()]
    commit = git('rev-parse', 'HEAD').strip() + ('-dirty' if git('status', '--porcelain', '--untracked-files=no').strip() else '')
    return {'commit': commit, 'production_sources_dirty': bool(changed), 'dirty_paths': changed,
            'sha256': {path: sha(ROOT / path) for path in PRODUCTION_SOURCES}}


def programs_current():
    """Every embedded plume program matches its provenance record (source and header hashes)."""
    out = {}
    for name, (record_path, header) in PROGRAMS.items():
        record = json.loads((ROOT / record_path).read_text())
        if record['source_sha256'] != sha(ROOT / record['source']) or record['header_sha256'] != sha(ROOT / header):
            raise SystemExit(f'{name}: stale embedded program; run tools/shaders/generate_rigid_motion_pixel.py --shader {name}')
        out[name] = {k: record[k] for k in ('source', 'word_count', 'bytecode_sha256', 'target')}
    return out


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    command = ['i686-w64-mingw32-g++', *FLAGS, *SOURCES, '-o', str(EXE), '-luser32', '-ldxguid']
    done = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    (BUILD / 'build.log').write_text(done.stdout + done.stderr)
    if done.returncode:
        raise SystemExit('fixture build failed:\n' + done.stdout + done.stderr)
    return {'command': [str(c) for c in command], 'warnings': len(re.findall(r'\bwarning:', done.stdout + done.stderr)),
            'executable_sha256': sha(EXE)}


def fields(line):
    out = {}
    for key, value in re.findall(r'(\w+)=(\S+)', line):
        try:
            out[key] = int(value) if re.fullmatch(r'-?\d+', value) else float(value)
        except ValueError:
            out[key] = value
    return out


def parse(text):
    tags = ('ATTACH', 'FP16_REFUSED', 'RESOLVE_CONFIG', 'LENGTH', 'RESOLVE', 'OCCLUSION_HEADON', 'OCCLUSION_20DEG',
            'OCCLUSION_TAILON', 'OCCLUSION_OFFCENTRE', 'CHASE', 'CHASE_OWN', 'PRESETS', 'TEMPORAL', 'SHAPE', 'SHOCK', 'END_ON',
            'END_ON_NOZZLE', 'FLOOR', 'MOUTH', 'MOUTH_END_ON', 'DISTANCE', 'DISTANCE_DOT', 'OFF_PATH', 'FAULT', 'RESET', 'TIMING',
            'TIMING_DISC', 'BUILD', 'BUILD_FLOOR', 'IDLE', 'SPILL', 'SPILL_PROFILE', 'SPILL_NEAR', 'FLOW', 'FLOW_SAME', 'FLOW_LAG',
            'FLOW_KEYED', 'COLOUR', 'COLOUR_HEAD', 'ATTACK', 'ATTACK_CROSSING', 'TRAVEL', 'STRUCTURE', 'STRUCTURE_DISC')
    report = {tag.lower(): [] for tag in tags}
    report.update(checks=[], result=None)
    for line in text.splitlines():
        head = line.split(' ', 1)[0]
        if head == 'CHECK':
            _, label, verdict = line.split(' ', 2)
            report['checks'].append([label, verdict.strip() == 'PASS'])
        elif head in tags:
            report[head.lower()].append(fields(line))
        elif head == 'RESULT':
            report['result'] = dict(fields(line), verdict=line.split()[1])
    report['check_count'] = len(report['checks'])
    report['failed_checks'] = [label for label, passed in report['checks'] if not passed]
    return report


def gates(r):
    out = {}
    out['core_survival_min'] = min((x['core_survival'] for x in r['resolve']), default=None)
    out['core_survival'] = bool(r['resolve']) and all(x['core_survival'] >= GATES['core_survival'] for x in r['resolve'])
    dark = [x for x in r['resolve'] if x['sky'] == 'dark']
    out['trail_dark_px_max'] = max((x['trail_px'] for x in dark), default=None)
    out['trail_dark'] = bool(dark) and all(x['trail_px'] <= GATES['trail_dark_px'] for x in dark)
    out['trail_flicker_px'] = {f"{x['width']}_{x['speed_px']:.0f}px": x['trail_px'] for x in r['resolve'] if x['sky'] == 'flicker'}
    out['stage_gpu_ms'] = {f"{x['width']}x{x['height']}_{x['nozzles']}" + (f"_far{x['far']}" if x.get('far') else ''): x['gpu_ms']
                           for x in r['timing']}
    out['distance_ratio_gpu'] = {f"{x['width']}_{x['nozzle_px']:.0f}px": x['ratio_gpu'] for x in r['distance']}
    out['stage_gpu_within_advisory'] = {k: v <= GATES['stage_gpu_ms_advisory'] for k, v in out['stage_gpu_ms'].items()}
    out['build_300_far250_us'] = next((x['median_us'] for x in r['build'] if x['records'] == 300), None)
    out['build_us'] = {str(x['records']): x['median_us'] for x in r['build']}
    out['build_floor_us'] = {str(x['records']): x['median_us'] for x in r['build_floor']}
    out['mouth_over_body'] = {f"{x['width']}_s{x['s']:.2f}": x['mouth_over_body'] for x in r['mouth']}
    out['build_100_within'] = any(x['records'] == 100 and x['median_us'] <= 1000 * GATES['build_100_ms'] for x in r['build'])
    # After the gap analysis (phases 2 and 3): the pixel program's slots, and each new case's numbers.
    out['ps_slots'] = r['attach'][0].get('ps_slots') if r['attach'] else None
    out['ps_slots_within'] = out['ps_slots'] is not None and out['ps_slots'] <= GATES['ps_slots']
    out['idle_L_over_value'] = {str(x['width']): x['L_over_value'] for x in r['idle']}
    out['spill'] = {f"{x['width']}_{x['lane']}": {k: x[k] for k in ('law_median', 'law_max', 'look_median', 'law_beyond_max', 'guard_max')}
                    for x in r['spill']}
    out['flow_world_over_law'] = {f"{x['width']}_{x['value']:.0f}": x['ratio'] for x in r['flow']}
    out['flow_600_over_1500'] = {str(x['width']): x['ratio'] for x in r['flow_same']}
    out['flow_lag1'] = {f"{x['width']}_{x['value']:.0f}": x['lag1'] for x in r['flow_lag']}
    # The review fixes: the per-nozzle phase under a value flip, the near plate's spill, the main-to-brake flare.
    out['flow_keyed'] = {str(x['width']): {k: x[k] for k in ('lag1_keyed', 'lag1_shared', 'factor_changes')} for x in r['flow_keyed']}
    out['spill_near'] = {f"{x['width']}_{x['lane']}_{x['value']:.0f}": {k: x[k] for k in ('core_ratio_max', 'rim_ratio_max', 'cut_max')}
                         for x in r['spill_near']}
    out['attack_crossing'] = {str(x['width']): {'frame1': x['frame1'], 'back_ms': x['back_ms']} for x in r['attack_crossing']}
    out['colour_error_max'] = max((x['error'] for x in r['colour']), default=None)
    out['attack'] = {f"{x['width']}_{x['kind']}": {'frame2': x['frame2'], 'back_ms': x['back_ms']} for x in r['attack']}
    out['travel'] = {str(x['width']): {k: x[k] for k in ('weight', 'L_ratio', 'I_ratio', 'drawn_ratio', 'peak_ratio')} for x in r['travel']}
    # The revised look law's structure (FP16, every frame gated in the fixture): the worst frame per size, tint and nozzle.
    structure = {}
    for x in r['structure']:
        key = f"{x['width']}_{x['tint']}_{x['asked_px']:.0f}"
        row = structure.setdefault(key, {'radial_min': 9e9, 'lane_depth_min': 9e9, 'gap_min_max': 0.0, 'aniso_min': 9e9,
                                         'white_axis_min': 9e9, 'white_rim_min': 9e9, 'nozzle_px': x['nozzle_px'], 'frames': 0})
        row['radial_min'] = min(row['radial_min'], x['radial'])
        row['lane_depth_min'] = min(row['lane_depth_min'], x['lane_depth'])
        row['gap_min_max'] = max(row['gap_min_max'], x['gap_min'])
        row['aniso_min'] = min(row['aniso_min'], x['aniso'])
        row['white_axis_min'] = min(row['white_axis_min'], x['white_axis_u05'])
        row['white_rim_min'] = min(row['white_rim_min'], x['white_rim_u02'])
        row['frames'] += 1
    out['structure'] = structure
    out['structure_disc'] = {f"{x['width']}_{x['tint']}": {k: x[k] for k in ('ring', 'ring_n', 'hot', 'ring_display', 'hot_display', 'nozzle_px')}
                             for x in r['structure_disc']}
    return out


def disc_ab_summary(rows):
    """Per size and nozzle count: the three rounds' gpu_ms with the disc on and off, their medians and the difference."""
    out = {}
    for row in rows:
        key = f"{row['width']}x{row['height']}_{row['nozzles']}"
        entry = out.setdefault(key, {'on': [], 'off': [], 'discs': row['discs'] if row['disc'] == 'on' else None})
        entry[row['disc']].append(row['gpu_ms'])
        if row['disc'] == 'on':
            entry['discs'] = row['discs']
    for entry in out.values():
        entry['median_on_ms'] = statistics.median(entry['on']) if entry['on'] else None
        entry['median_off_ms'] = statistics.median(entry['off']) if entry['off'] else None
        if entry['on'] and entry['off']:
            entry['disc_ms'] = round(entry['median_on_ms'] - entry['median_off_ms'], 4)
            entry['spread_on_ms'] = round(max(entry['on']) - min(entry['on']), 4)
            entry['spread_off_ms'] = round(max(entry['off']) - min(entry['off']), 4)
    return out


PNG_LIMIT = 500 * 1024


def dump_rows(text):
    """The fixture's DUMP rows: per image its description, bands and nozzles, in output order."""
    images = {}
    for line in text.splitlines():
        head = line.split(' ', 1)[0]
        if head == 'DUMP_DESC':
            _, name, desc = line.split(' ', 2)
            images[name] = {'name': name, 'desc': desc.strip(), 'panels': [], 'nozzles': [], 'written': False}
        elif head in ('DUMP_PANEL', 'DUMP_NOZZLE', 'DUMP'):
            row = fields(line)
            entry = images.setdefault(row['file'], {'name': row['file'], 'desc': '', 'panels': [], 'nozzles': [], 'written': False})
            if head == 'DUMP':
                entry.update(written=True, drew=row.get('drew'))
            else:
                entry['panels' if head == 'DUMP_PANEL' else 'nozzles'].append(row)
    return list(images.values())


def contact_sheet(out_dir, names):
    """Every image tiled 4 across at 480x270 with its file name; None without Pillow."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return None
    cols, tw, th, label = 4, 480, 270, 30
    rows = (len(names) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * tw, rows * (th + label)), (24, 24, 24))
    draw = ImageDraw.Draw(sheet)
    for i, name in enumerate(names):
        x, y = (i % cols) * tw, (i // cols) * (th + label)
        with Image.open(out_dir / f'{name}.png') as im:
            sheet.paste(im.convert('RGB').resize((tw, th), Image.LANCZOS), (x, y))
        text = name.replace('_agx-ev0', '')
        draw.text((x + 4, y + th + 2), text[:78], fill=(230, 230, 230))
        if len(text) > 78:
            draw.text((x + 4, y + th + 15), text[78:156], fill=(230, 230, 230))
    path = out_dir / 'contact_sheet.png'
    sheet.save(path, optimize=True)
    return path


def write_readme(out_dir, images, sheet, record):
    try:
        shown = out_dir.relative_to(ROOT)
    except ValueError:
        shown = out_dir
    lines = ['# Engine plume look images', '',
             'Written by `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_engine_plumes.py '
             f'--dump-images {shown}/` (the fixture\'s `--dump` mode: `verification/probe/engine_plumes_fixture.cpp`, namespace '
             '`dump`). Bottle X3, builtin d3d9; the game is not launched.', '',
             f"Source: commit `{record['source']['commit']}`; production sources dirty: {record['source']['production_sources_dirty']}; "
             f"fixture build warnings: {record.get('build', {}).get('warnings')}.", '',
             'Every image is 1920x1080: one or more horizontal bands, each a separate sequence of the production stage '
             '(EnginePlumesPass, then EngineRibbonsPass) inside the production TemporalPass resolve in the flown configuration, '
             '30 frames of warm-up at 60 fps (stage clock, flow accumulator x the travel flow, attack memory and SETA ramp advanced '
             'as the proxy advances them) and that band of the resolved frame named in the table (frame 30 = the 31st). Default '
             'plume look with the preset named. Background: a dark static starfield; the hull cases add a plain grey plate (encoded '
             '0.30) whose lane depth is the nozzle\'s. Tints are two-tone body colours (mean, peak; linear): "split-red" = the red '
             'cluster (1, 0.15, 0.15; 1, 0.81, 0.81), "argon-blue" = the cyan cluster (0.14, 0.71, 1; 0.27, 0.90, 1). The nozzle '
             'width in px is 0.5 x value x pixels per unit at the nozzle depth. Angles are from the line of sight to the nozzle '
             '(90 = side view, perpendicular to it wherever the nozzle sits on screen).', '',
             'Near-camera cap: at 1080p a 150 px nozzle is over the production cap (the body\'s width held to 0.12 H = 130 px by '
             'shrinking the plume about the nozzle, its radiance fading towards 0.5 in the last 20 %; `engine_plumes_core.h`, '
             'chase_cap). The bands where it acted say "near-cap N faded N" below: those plumes are shown smaller and dimmer than '
             'the law at 150 px, as the game would draw them at that size.', '',
             'Tonemap (`_agx-ev0` in each name): a CPU port of the write-back\'s AgX (`src/temporal/agx.hlsl` with the default '
             'constants: gamma 2.2 decode, look none) at EV 0 (exposure 1, the meter\'s neutral target over a dark sky), 8-bit '
             'display RGB. Not included: bloom, TAA sharpen, display dither, fog, the engine light on the hull and the heat shimmer.', '',
             '| File | Shows | Bands: rows, captured frame, preset, SETA weight, plate, drawn / nozzles, ribbons |', '| --- | --- | --- |']
    for im in images:
        bands = '; '.join(
            f"{p['rows']} f{p['capture_frame']} {p['preset']}" + (f" SETA {p['travel_weight']:.2f}" if p['seta'] else '') +
            (' plate' if p['plate'] else '') + f" drawn {p['drawn']}/{p['nozzles']}" +
            (f" near-cap {p['capped']} faded {p['faded']}" if p['capped'] or p['faded'] else '') + f" ribbons {p['ribbons']}" +
            (f" ({p['note']})" if p['note'] != '-' else '') for p in im['panels'])
        lines.append(f"| `{im['name']}.png` | {im['desc']} | {bands} |")
    lines += ['', 'Nozzles: screen position at the captured frame, projected width, value, view depth, angle, axis (view '
              'space), throttle, tint.', '']
    for im in images:
        for z in im['nozzles']:
            extra = ((' steering' if z['steering'] else '') + (f" fired at frame {z['fire_at']}" if z['fire_at'] >= 0 else '') +
                     (f" moving {z['speed_px']} px/frame" if z['speed_px'] else ''))
            lines.append(f"- `{im['name']}` band {z['band']}: ({z['x_px']}, {z['y_px']}) px, {z['nozzle_px']} px, value {z['value']}, "
                         f"depth {z['depth']}, {z['degrees']} deg from the line of sight ({z['facing']}), axis ({z['axis']}), "
                         f"s {z['s']}, {z['tone']}{extra}")
        if not im['nozzles']:
            lines.append(f"- `{im['name']}`: {sum(p['nozzles'] for p in im['panels'])} nozzles from a fixed LCG seed "
                         '(fixture `dump::images`), not listed one by one')
    lines += ['', 'Contact sheet: `contact_sheet.png` (every image at 480x270 with its name).' if sheet else
              'Contact sheet: skipped (Pillow not importable).', '']
    (out_dir / 'README.md').write_text('\n'.join(lines))


def dump_images(args, record):
    out_dir = Path(args.dump_images)
    out_dir = (out_dir if out_dir.is_absolute() else Path.cwd() / out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    raw = BUILD / 'dump'
    raw.mkdir(parents=True, exist_ok=True)
    for old in raw.glob('*.ppm'):
        old.unlink()
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(BUILD), str(EXE), r'C:\X3\d3dx9_37.dll',
               '--dump', 'dump']
    done = subprocess.run(command, capture_output=True, env=dict(os.environ, WINEDLLOVERRIDES='d3d9=b'), timeout=args.timeout)
    text = done.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')
    (BUILD / 'dump.log').write_text(text)
    (BUILD / 'dump.stderr.txt').write_bytes(done.stderr)
    report = parse(text)
    images = dump_rows(text)
    sizes = {}
    for im in images:
        ppm = raw / f"{im['name']}.ppm"
        if im['written'] and ppm.exists():
            sizes[im['name']] = png_writer.write_png(out_dir / f"{im['name']}.png", *png_writer.read_ppm(ppm))
            ppm.unlink()
    names = [im['name'] for im in images if im['name'] in sizes]
    sheet = contact_sheet(out_dir, names) if names else None
    if sheet:
        sizes['contact_sheet'] = sheet.stat().st_size
    write_readme(out_dir, images, sheet, record)
    over = {k: v for k, v in sizes.items() if v > PNG_LIMIT}
    ok = (done.returncode == 0 and report['result'] is not None and report['result']['verdict'] == 'PASS' and
          not report['failed_checks'] and images and len(names) == len(images) and all(im.get('drew') == 1 for im in images) and
          not over and record.get('build', {}).get('warnings', 0) == 0)
    print(json.dumps({'passed': bool(ok), 'exit': done.returncode, 'checks': report['check_count'], 'failed': report['failed_checks'],
                      'images': len(names), 'contact_sheet': bool(sheet), 'max_png_kb': round(max(sizes.values(), default=0) / 1024, 1),
                      'over_500kb': over, 'out': str(out_dir), 'log': str((BUILD / 'dump.log').relative_to(ROOT))}, indent=1))
    return 0 if ok else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--no-build', action='store_true')
    parser.add_argument('--timeout', type=int, default=1800)
    parser.add_argument('--only', default=None, help='fixture case filter (comma list), diagnosis only: the record is not passed')
    parser.add_argument('--disc-ab', action='store_true', help='the disc on/off stage-cost A/B alone (plume_disc_ab.json)')
    parser.add_argument('--dump-images', metavar='DIR', default=None,
                        help='look images (PNG, README, contact sheet) into DIR instead of the cases; the record is not touched')
    args = parser.parse_args()
    if args.disc_ab:
        args.only = 'timing'
    if os.environ.get('X3M_FIXTURE_BOTTLE') != 'X3':
        raise SystemExit('fixture requires X3M_FIXTURE_BOTTLE=X3')
    if game_running():
        raise SystemExit('the game is running')
    results = bottle.results_dir(ROOT) / 'engine-plumes'
    results.mkdir(parents=True, exist_ok=True)
    record = {'bottle': bottle.describe(), 'production_sources': list(PRODUCTION_SOURCES), 'source': source_binding(),
              'programs': programs_current(), 'gates': GATES, 'game_launched': False}
    if not args.no_build:
        record['build'] = build()
    d3dx = bottle.game_dir() / 'd3dx9_37.dll'
    record['d3dx9_37_sha256'] = sha(d3dx)
    if args.dump_images:
        return dump_images(args, record)
    command = [bottle.WINE, *bottle.wine_args(), '--dll', 'd3d9=b', '--workdir', str(BUILD), str(EXE), r'C:\X3\d3dx9_37.dll']
    if args.only:
        command += ['--only', args.only]
    env = dict(os.environ, WINEDLLOVERRIDES='d3d9=b')
    if args.disc_ab:
        env['X3M_PLUMES_FIXTURE_DISC_AB'] = '1'
    done = subprocess.run(command, capture_output=True, env=env, timeout=args.timeout)
    text = done.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')
    (BUILD / 'fixture.log').write_text(text)
    (BUILD / 'fixture.stderr.txt').write_bytes(done.stderr)
    report = parse(text)
    if args.disc_ab:
        ab = {'bottle': record['bottle'], 'source': record['source'], 'build_warnings': record.get('build', {}).get('warnings'),
              'method': 'tail EVENT-fenced, 60 on/off pairs per measurement; three rounds interleaved (on/off, off/on, on/off); '
                        'disc off = the facing band moved past 1 (Look disc_low 1.5, disc_high 2), same source and crowd',
              'exit_code': done.returncode, 'result': report['result'], 'rows': report['timing_disc'],
              'summary': disc_ab_summary(report['timing_disc']), 'game_launched': False}
        out_path = ROOT / 'verification/results/engine-effects/plume_disc_ab.json'
        out_path.write_text(json.dumps(ab, indent=2) + '\n')
        print(json.dumps({'exit': done.returncode, 'summary': {k: {x: v[x] for x in ('median_on_ms', 'median_off_ms', 'disc_ms', 'discs')}
                                                               for k, v in ab['summary'].items()},
                          'results': str(out_path.relative_to(ROOT))}, indent=1))
        return 0 if done.returncode == 0 and len(report['timing_disc']) == 24 else 1
    record['run'] = {'exit_code': done.returncode, 'result': report['result'], 'log': str((BUILD / 'fixture.log').relative_to(ROOT))}
    record['report'] = report
    record['gates_met'] = gates(report)
    record['passed'] = (done.returncode == 0 and not report['failed_checks'] and report['result'] is not None and
                        report['result']['verdict'] == 'PASS' and not args.only and record['gates_met']['core_survival'] and
                        record['gates_met']['trail_dark'] and record['gates_met']['build_100_within'] and
                        record['gates_met']['ps_slots_within'] and record.get('build', {}).get('warnings', 0) == 0)
    (results / 'summary.json').write_text(json.dumps(record, indent=2) + '\n')
    g = record['gates_met']
    print(json.dumps({'passed': record['passed'], 'checks': report['check_count'], 'failed': report['failed_checks'],
                      'core_survival_min': g['core_survival_min'], 'trail_dark_px_max': g['trail_dark_px_max'],
                      'trail_flicker_px': g['trail_flicker_px'], 'stage_gpu_ms': g['stage_gpu_ms'], 'build_us': g['build_us'],
                      'build_floor_us': g['build_floor_us'], 'mouth_over_body': g['mouth_over_body'],
                      'distance_ratio_gpu': g['distance_ratio_gpu'], 'ps_slots': g['ps_slots'],
                      'idle_L_over_value': g['idle_L_over_value'], 'spill': g['spill'],
                      'flow_world_over_law': g['flow_world_over_law'], 'flow_600_over_1500': g['flow_600_over_1500'],
                      'flow_lag1': g['flow_lag1'], 'flow_keyed': g['flow_keyed'], 'spill_near': g['spill_near'],
                      'colour_error_max': g['colour_error_max'], 'attack': g['attack'], 'attack_crossing': g['attack_crossing'],
                      'travel': g['travel'], 'structure': g['structure'], 'structure_disc': g['structure_disc'],
                      'results': str(results.relative_to(ROOT))}, indent=1))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
