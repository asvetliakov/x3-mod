#!/usr/bin/env python3
"""Compile our original ps_3_0 fragments with a local native D3DX compiler.

Fifteen authored programs are embedded: the motion fragment
(src/temporal/rigid_motion_ps.hlsl -> src/renderer/rigid_motion_pixel_program_inc.h),
the current-depth fragment
(src/temporal/current_depth_ps.hlsl -> src/renderer/current_depth_pixel_program_inc.h),
the temporal resolve the live route runs (temporal step 3;
src/temporal/resolve.hlsl -> src/renderer/temporal_resolve_program_inc.h),
the HDR scene path's stage-1 identity write-back (src/temporal/hdr_writeback_ps.hlsl
-> src/renderer/hdr_writeback_program_inc.h; its display-dithered twin
src/temporal/hdr_writeback_dither_ps.hlsl -> hdr_writeback_dither_program_inc.h)
and its stage-2 AgX tonemap
(src/temporal/agx.hlsl -> src/renderer/hdr_tonemap_program_inc.h) and exposure
meter chain (src/temporal/hdr_meter_level0_ps.hlsl and hdr_meter_reduce_ps.hlsl
-> src/renderer/hdr_meter_level0_program_inc.h, hdr_meter_reduce_program_inc.h),
and the post-resolve sharpen: the RCAS program of the 8-bit route and the HDR
identity write-back (src/temporal/taa_sharpen_ps.hlsl -> src/renderer/
taa_sharpen_program_inc.h) and the AgX-then-RCAS write-back
(src/temporal/agx_sharpen_ps.hlsl -> src/renderer/hdr_tonemap_sharpen_program_inc.h);
both include src/temporal/rcas.hlsl, which this tool expands textually (the
provenance lists every include's hash), and the vs_3_0 pass-through of every
proxy quad (src/temporal/quad_vs.hlsl -> src/renderer/quad_vertex_program_inc.h;
the only entry with a `target` other than ps_3_0). `--shader` selects one (default: all). --check recompiles and compares the
checked-in artifacts without changing them. The compiler DLL is an external
local prerequisite, never redistributed. Only our authored shaders' compiled
programs and deterministic provenance are retained. No D3D device is created;
X3AP must nevertheless be stopped for this tool.
Run through verification/probe/wine_lock.py; X3M_FIXTURE_BOTTLE selects the bottle.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'verification/probe'))
from game_guard import game_running  # noqa: E402
import bottle  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
COMPILER_SOURCE = ROOT / 'tools/shaders/compile_rigid_motion_pixel.cpp'
SHADERS = {
    'rigid_motion': dict(source=ROOT / 'src/temporal/rigid_motion_ps.hlsl',
                         header=ROOT / 'src/renderer/rigid_motion_pixel_program_inc.h',
                         provenance=ROOT / 'verification/results/rigid-motion-pixel-program.json'),
    'current_depth': dict(source=ROOT / 'src/temporal/current_depth_ps.hlsl',
                          header=ROOT / 'src/renderer/current_depth_pixel_program_inc.h',
                          provenance=ROOT / 'verification/results/current-depth-pixel-program.json'),
    'temporal_resolve': dict(source=ROOT / 'src/temporal/resolve.hlsl',
                             header=ROOT / 'src/renderer/temporal_resolve_program_inc.h',
                             provenance=ROOT / 'verification/results/temporal-resolve-program.json'),
    # The reactive-mask snapshot modes, split out of the resolve (options.z).
    'temporal_resolve_snapshot': dict(source=ROOT / 'src/temporal/resolve_snapshot.hlsl',
                                      header=ROOT / 'src/renderer/temporal_resolve_snapshot_program_inc.h',
                                      provenance=ROOT / 'verification/results/temporal-resolve-snapshot-program.json'),
    # Flicker-suppression variants (docs/architecture/taa-flicker-suppression.md): defines plus an include of resolve.hlsl.
    'temporal_resolve_thin': dict(source=ROOT / 'src/temporal/resolve_thin.hlsl',
        header=ROOT / 'src/renderer/temporal_resolve_thin_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-resolve-thin-program.json'),
    'temporal_resolve_age': dict(source=ROOT / 'src/temporal/resolve_age.hlsl',
        header=ROOT / 'src/renderer/temporal_resolve_age_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-resolve-age-program.json'),
    'temporal_resolve_far': dict(source=ROOT / 'src/temporal/resolve_far.hlsl',
        header=ROOT / 'src/renderer/temporal_resolve_far_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-resolve-far-program.json'),
    'temporal_line_mask': dict(source=ROOT / 'src/temporal/line_mask_ps.hlsl',
        header=ROOT / 'src/renderer/temporal_line_mask_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-line-mask-program.json'),
    # Camera-relative thin-region gate (taa-lattice-crawl.md section 32.1): the mask with both gate strengths, the far
    # program with the 7x7 box clip, and the box pass itself. Defines plus includes; the plain programs' bytes are untouched.
    'temporal_line_mask_camera': dict(source=ROOT / 'src/temporal/line_mask_camera_ps.hlsl',
        header=ROOT / 'src/renderer/temporal_line_mask_camera_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-line-mask-camera-program.json'),
    # The two mask programs' first draw with the current depth copy folded in (docs/architecture/taa-high-resolution.md S1):
    # COLOR1 = the current-depth texel for the R32F history. Defines plus includes; the programs above keep their bytes.
    'temporal_line_mask_depth': dict(source=ROOT / 'src/temporal/line_mask_depth_ps.hlsl',
        header=ROOT / 'src/renderer/temporal_line_mask_depth_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-line-mask-depth-program.json'),
    'temporal_line_mask_camera_depth': dict(source=ROOT / 'src/temporal/line_mask_camera_depth_ps.hlsl',
        header=ROOT / 'src/renderer/temporal_line_mask_camera_depth_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-line-mask-camera-depth-program.json'),
    'temporal_resolve_far_camera': dict(source=ROOT / 'src/temporal/resolve_far_camera.hlsl',
        header=ROOT / 'src/renderer/temporal_resolve_far_camera_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-resolve-far-camera-program.json'),
    # S3 (docs/architecture/taa-high-resolution.md): the resolve programs above use the 5-tap bilinear Catmull-Rom history;
    # these five keep the 16-tap point form (X3M_HISTORY_TAPS16, bytecode of the earlier programs) for --taa-history-taps 16.
    'temporal_resolve_taps16': dict(source=ROOT / 'src/temporal/resolve_taps16.hlsl',
        header=ROOT / 'src/renderer/temporal_resolve_taps16_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-resolve-taps16-program.json'),
    'temporal_resolve_thin_taps16': dict(source=ROOT / 'src/temporal/resolve_thin_taps16.hlsl',
        header=ROOT / 'src/renderer/temporal_resolve_thin_taps16_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-resolve-thin-taps16-program.json'),
    'temporal_resolve_age_taps16': dict(source=ROOT / 'src/temporal/resolve_age_taps16.hlsl',
        header=ROOT / 'src/renderer/temporal_resolve_age_taps16_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-resolve-age-taps16-program.json'),
    'temporal_resolve_far_taps16': dict(source=ROOT / 'src/temporal/resolve_far_taps16.hlsl',
        header=ROOT / 'src/renderer/temporal_resolve_far_taps16_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-resolve-far-taps16-program.json'),
    'temporal_resolve_far_camera_taps16': dict(source=ROOT / 'src/temporal/resolve_far_camera_taps16.hlsl',
        header=ROOT / 'src/renderer/temporal_resolve_far_camera_taps16_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-resolve-far-camera-taps16-program.json'),
    'temporal_thin_box': dict(source=ROOT / 'src/temporal/thin_box_ps.hlsl',
        header=ROOT / 'src/renderer/temporal_thin_box_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-thin-box-program.json'),
    # Sentinel stabiliser (temporal-integration.md "Distant unrouted stations under a pan"): the separable box, bound only while it is on.
    'temporal_thin_box_rows': dict(source=ROOT / 'src/temporal/thin_box_rows_ps.hlsl',
        header=ROOT / 'src/renderer/temporal_thin_box_rows_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-thin-box-rows-program.json'),
    'temporal_thin_box_columns': dict(source=ROOT / 'src/temporal/thin_box_columns_ps.hlsl',
        header=ROOT / 'src/renderer/temporal_thin_box_columns_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-thin-box-columns-program.json'),
    # A' (docs/architecture/taa-plan-lifted-slot-cap.md step 1, --taa-region-hold on): the camera-gate resolve composing the
    # region from the mask's tests target with temporal holds, and the three box programs gated on that target.
    'temporal_resolve_far_camera_hold': dict(source=ROOT / 'src/temporal/resolve_far_camera_hold.hlsl',
        header=ROOT / 'src/renderer/temporal_resolve_far_camera_hold_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-resolve-far-camera-hold-program.json'),
    'temporal_thin_box_hold': dict(source=ROOT / 'src/temporal/thin_box_hold_ps.hlsl',
        header=ROOT / 'src/renderer/temporal_thin_box_hold_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-thin-box-hold-program.json'),
    'temporal_thin_box_rows_hold': dict(source=ROOT / 'src/temporal/thin_box_rows_hold_ps.hlsl',
        header=ROOT / 'src/renderer/temporal_thin_box_rows_hold_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-thin-box-rows-hold-program.json'),
    'temporal_thin_box_columns_hold': dict(source=ROOT / 'src/temporal/thin_box_columns_hold_ps.hlsl',
        header=ROOT / 'src/renderer/temporal_thin_box_columns_hold_program_inc.h',
        provenance=ROOT / 'verification/results/temporal-thin-box-columns-hold-program.json'),
    'hdr_writeback': dict(source=ROOT / 'src/temporal/hdr_writeback_ps.hlsl',
                          header=ROOT / 'src/renderer/hdr_writeback_program_inc.h',
                          provenance=ROOT / 'verification/results/hdr-writeback-program.json'),
    # The identity write-back with the static display dither (X3M_HDR_DITHER).
    'hdr_writeback_dither': dict(source=ROOT / 'src/temporal/hdr_writeback_dither_ps.hlsl',
                                 header=ROOT / 'src/renderer/hdr_writeback_dither_program_inc.h',
                                 provenance=ROOT / 'verification/results/hdr-writeback-dither-program.json'),
    'hdr_tonemap': dict(source=ROOT / 'src/temporal/agx.hlsl',
                        header=ROOT / 'src/renderer/hdr_tonemap_program_inc.h',
                        provenance=ROOT / 'verification/results/hdr-tonemap-program.json'),
    'hdr_meter_level0': dict(source=ROOT / 'src/temporal/hdr_meter_level0_ps.hlsl',
                             header=ROOT / 'src/renderer/hdr_meter_level0_program_inc.h',
                             provenance=ROOT / 'verification/results/hdr-meter-level0-program.json'),
    'hdr_meter_reduce': dict(source=ROOT / 'src/temporal/hdr_meter_reduce_ps.hlsl',
                             header=ROOT / 'src/renderer/hdr_meter_reduce_program_inc.h',
                             provenance=ROOT / 'verification/results/hdr-meter-reduce-program.json'),
    # Post-resolve sharpen (RCAS): the 8-bit route / identity write-back program
    # and the AgX-then-sharpen write-back; both include rcas.hlsl textually.
    'taa_sharpen': dict(source=ROOT / 'src/temporal/taa_sharpen_ps.hlsl',
                        header=ROOT / 'src/renderer/taa_sharpen_program_inc.h',
                        provenance=ROOT / 'verification/results/taa-sharpen-program.json'),
    'hdr_tonemap_sharpen': dict(source=ROOT / 'src/temporal/agx_sharpen_ps.hlsl',
                                header=ROOT / 'src/renderer/hdr_tonemap_sharpen_program_inc.h',
                                provenance=ROOT / 'verification/results/hdr-tonemap-sharpen-program.json'),
    # The vs_3_0 pass-through bound for every proxy full-screen quad (native
    # D3D9 pairs ps_3_0 with vs_3_0; the XYZRHW fixed-function path is not a
    # documented partner). Per-shader `target`; every other entry is ps_3_0.
    'quad_vertex': dict(source=ROOT / 'src/temporal/quad_vs.hlsl',
                        header=ROOT / 'src/renderer/quad_vertex_program_inc.h',
                        provenance=ROOT / 'verification/results/quad-vertex-program.json',
                        target='vs_3_0'),
    # The scene-end sun-shadow apply quad (docs/architecture/legacy-sun-application.md, section 2).
    'sun_shadow_apply': dict(source=ROOT / 'src/temporal/sun_shadow_apply_ps.hlsl',
                             header=ROOT / 'src/renderer/sun_shadow_apply_program_inc.h',
                             provenance=ROOT / 'verification/results/sun-shadow-apply-program.json'),
    # The same quad over up to five cascades (docs/architecture/shadow-cascades.md, section 2).
    'sun_shadow_cascade_apply': dict(source=ROOT / 'src/temporal/sun_shadow_cascade_apply_ps.hlsl',
                                     header=ROOT / 'src/renderer/sun_shadow_cascade_apply_program_inc.h',
                                     provenance=ROOT / 'verification/results/sun-shadow-cascade-apply-program.json'),
    # Partial sun occlusion, step 1 (docs/architecture/sun-partial-occlusion.md): the 1x1 visibility fraction.
    'sun_visibility': dict(source=ROOT / 'src/temporal/sun_visibility_ps.hlsl',
                           header=ROOT / 'src/renderer/sun_visibility_program_inc.h',
                           provenance=ROOT / 'verification/results/sun-visibility-program.json'),
    # The volumetric sun fog (docs/architecture/volumetric-fog.md, "Stage 1 implementation"):
    # the half-resolution lit-fraction march, the linear composite and the two sky-hue levels.
    'fog_march': dict(source=ROOT / 'src/fog/fog_march_ps.hlsl',
                      header=ROOT / 'src/renderer/fog_march_program_inc.h',
                      provenance=ROOT / 'verification/results/fog-march-program.json'),
    'fog_composite': dict(source=ROOT / 'src/fog/fog_composite_ps.hlsl',
                          header=ROOT / 'src/renderer/fog_composite_program_inc.h',
                          provenance=ROOT / 'verification/results/fog-composite-program.json'),
    'fog_sky_level0': dict(source=ROOT / 'src/fog/fog_sky_level0_ps.hlsl',
                           header=ROOT / 'src/renderer/fog_sky_level0_program_inc.h',
                           provenance=ROOT / 'verification/results/fog-sky-level0-program.json'),
    'fog_sky_reduce': dict(source=ROOT / 'src/fog/fog_sky_reduce_ps.hlsl',
                           header=ROOT / 'src/renderer/fog_sky_reduce_program_inc.h',
                           provenance=ROOT / 'verification/results/fog-sky-reduce-program.json'),
    # Stored-density fog (docs/architecture/fog-density-runtime-integration.md, checkpoint 2): the
    # 24+40 two-level march, the composite without in-line repair and the separate full-resolution
    # repair draw. The three `*_look` programs are what the renderer draws (the single look, FOG_LOOK);
    # the three unshaped ones and the texel-exact march are the shader fixture's parity reference only.
    'fog_density_march': dict(source=ROOT / 'src/fog/fog_density_march_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_march_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-march-program.json'),
    'fog_density_composite': dict(source=ROOT / 'src/fog/fog_density_composite_ps.hlsl',
                                  header=ROOT / 'src/renderer/fog_density_composite_program_inc.h',
                                  provenance=ROOT / 'verification/results/fog-density-composite-program.json'),
    'fog_density_repair': dict(source=ROOT / 'src/fog/fog_density_repair_ps.hlsl',
                               header=ROOT / 'src/renderer/fog_density_repair_program_inc.h',
                               provenance=ROOT / 'verification/results/fog-density-repair-program.json'),
    # The single look (FOG_LOOK): the production variants of the three programs above.
    'fog_density_march_look': dict(source=ROOT / 'src/fog/fog_density_march_look_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_march_look_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-march-look-program.json'),
    'fog_density_composite_look': dict(source=ROOT / 'src/fog/fog_density_composite_look_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_composite_look_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-composite-look-program.json'),
    'fog_density_repair_look': dict(source=ROOT / 'src/fog/fog_density_repair_look_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_repair_look_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-repair-look-program.json'),
    # Step B of docs/architecture/fog-gpu-cost.md (--fog-far-bins 24): the look's march and repair with 24 far bins.
    'fog_density_march_look_far24': dict(source=ROOT / 'src/fog/fog_density_march_look_far24_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_march_look_far24_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-march-look-far24-program.json'),
    'fog_density_repair_look_far24': dict(source=ROOT / 'src/fog/fog_density_repair_look_far24_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_repair_look_far24_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-repair-look-far24-program.json'),
    # Step C (--fog-march-scale 4): the look's march / repair / composite at the quarter-resolution sample spacing (both far-bin
    # counts; composite never marches), and the --gpu-sync-timing needs-repair census at either spacing.
    'fog_density_march_look_q4': dict(source=ROOT / 'src/fog/fog_density_march_look_q4_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_march_look_q4_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-march-look-q4-program.json'),
    'fog_density_march_look_far24_q4': dict(source=ROOT / 'src/fog/fog_density_march_look_far24_q4_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_march_look_far24_q4_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-march-look-far24-q4-program.json'),
    'fog_density_repair_look_q4': dict(source=ROOT / 'src/fog/fog_density_repair_look_q4_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_repair_look_q4_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-repair-look-q4-program.json'),
    'fog_density_repair_look_far24_q4': dict(source=ROOT / 'src/fog/fog_density_repair_look_far24_q4_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_repair_look_far24_q4_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-repair-look-far24-q4-program.json'),
    'fog_density_composite_look_q4': dict(source=ROOT / 'src/fog/fog_density_composite_look_q4_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_composite_look_q4_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-composite-look-q4-program.json'),
    'fog_density_needs_census': dict(source=ROOT / 'src/fog/fog_density_needs_census_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_needs_census_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-needs-census-program.json'),
    'fog_density_needs_census_q4': dict(source=ROOT / 'src/fog/fog_density_needs_census_q4_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_needs_census_q4_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-needs-census-q4-program.json'),
    # The sun-visibility slice grid (docs/architecture/fog-shadow-pass.md, X3M_FOG_SHADOW_PASS=1): the pass and the
    # look's march and repair reading it (FOG_SHADOW_PASS) instead of the in-march lookup; the *_look pair stays the control.
    'fog_density_visibility_grid': dict(source=ROOT / 'src/fog/fog_density_visibility_grid_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_visibility_grid_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-visibility-grid-program.json'),
    'fog_density_march_grid': dict(source=ROOT / 'src/fog/fog_density_march_grid_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_march_grid_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-march-grid-program.json'),
    'fog_density_repair_grid': dict(source=ROOT / 'src/fog/fog_density_repair_grid_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_density_repair_grid_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-density-repair-grid-program.json'),
    # Dust motes of the stored fog (docs/architecture/fog-dust-motes.md, X3M_FOG_DUST_MOTES): the capsule vertex
    # program and the pixel program in the in-march and grid variants, drawn after the repair.
    'fog_dust_motes_vertex': dict(source=ROOT / 'src/fog/fog_dust_motes_vs.hlsl',
                              header=ROOT / 'src/renderer/fog_dust_motes_vertex_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-dust-motes-vertex-program.json',
                              target='vs_3_0'),
    'fog_dust_motes_look': dict(source=ROOT / 'src/fog/fog_dust_motes_look_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_dust_motes_look_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-dust-motes-look-program.json'),
    'fog_dust_motes_grid': dict(source=ROOT / 'src/fog/fog_dust_motes_grid_ps.hlsl',
                              header=ROOT / 'src/renderer/fog_dust_motes_grid_program_inc.h',
                              provenance=ROOT / 'verification/results/fog-dust-motes-grid-program.json'),
    'fog_density_march_exact': dict(source=ROOT / 'verification/probe/fog_density_march_exact_ps.hlsl',
                                    header=ROOT / 'verification/probe/fog_density_march_exact_program_inc.h',
                                    provenance=ROOT / 'verification/results/fog-density-march-exact-program.json'),
}
VERSION_TOKENS = {'ps_3_0': 0xffff0300, 'vs_3_0': 0xfffe0300}
INCLUDE = re.compile(r'^#include "([^"]+)"\s*$')


def expand_includes(path, seen=None):
    """The source with every `#include "name"` line replaced by that file
    (relative to the including file, recursively; a cycle is an error).
    D3DXCompileShader is given no include handler, so the expansion is ours
    and the fixtures that compile the same sources do the same. Returns the
    text and the included files in order of first use."""
    seen = seen or []
    if path in seen:
        raise ValueError('include cycle at %s' % path)
    included = []
    lines = []
    for line in path.read_text().splitlines():
        match = INCLUDE.match(line)
        if not match:
            lines.append(line)
            continue
        target = (path.parent / match.group(1)).resolve()
        text, nested = expand_includes(target, seen + [path])
        included += [f for f in [target] + nested if f not in included]
        lines.append(text)
    return '\n'.join(lines) + '\n', included


def sha(data):
    return hashlib.sha256(data).hexdigest()


def validate(data, target='ps_3_0'):
    # Sanity bound only (ps_3_0 has no bytecode limit): the resolve with the
    # luminance weighting of HDR stage 3 is about 4,300 words.
    if len(data) % 4 or not 8 <= len(data) <= 32768:
        raise ValueError('Unexpected compiled program extent')
    words = struct.unpack('<' + 'I' * (len(data) // 4), data)
    if words[0] != VERSION_TOKENS[target] or words[-1] != 0x0000ffff:
        raise ValueError('Expected complete %s program' % target)
    offset = 1
    while offset < len(words) - 1:
        token = words[offset]
        if token == 0x0000ffff:
            raise ValueError('Early END')
        count = (token >> 16) & 0x7fff if token & 0xffff == 0xfffe else (token >> 24) & 15
        offset += 1 + count
    if offset != len(words) - 1:
        raise ValueError('Invalid instruction/comment framing')
    return words


def compile_one(name, args):
    shader = SHADERS[name]
    source, header, provenance = shader['source'], shader['header'], shader['provenance']
    target = shader.get('target', 'ps_3_0')
    expanded, included = expand_includes(source)
    compiler_path = args.d3dx.resolve()
    inputs = (source, COMPILER_SOURCE, Path(__file__).resolve(), compiler_path) + tuple(included)
    before = {path: sha(path.read_bytes()) for path in inputs}
    with tempfile.TemporaryDirectory(prefix='x3-original-motion-') as directory:
        work = Path(directory)
        exe, binary = work / 'compile.exe', work / 'program.bin'
        # The compiler reads one file: the include-expanded source when the
        # shader includes anything, the source itself otherwise (bit for bit).
        compiled = source
        if included:
            compiled = work / source.name
            compiled.write_text(expanded)
        subprocess.run(['i686-w64-mingw32-g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-msse2', '-mfpmath=sse', '-mstackrealign', '-mincoming-stack-boundary=2',
                        '-static', str(COMPILER_SOURCE), '-o', str(exe)], check=True)
        subprocess.run([bottle.WINE, *bottle.wine_args(), '--dll', 'd3dx9_37=n',
                        str(exe), 'Z:' + str(inputs[3]), 'Z:' + str(compiled), 'Z:' + str(binary), target],
                       check=True, timeout=60, env=dict(os.environ, WINEDLLOVERRIDES='d3dx9_37=n'))
        data = binary.read_bytes()
    if before != {path: sha(path.read_bytes()) for path in inputs}:
        raise RuntimeError('Compilation inputs changed')
    words = validate(data, target)
    text = '// Generated from our original %s. Do not edit.\n' % source.relative_to(ROOT)
    text += '// Reproduce: X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 tools/shaders/generate_rigid_motion_pixel.py --shader %s --check\n' % name
    text += ''.join('    ' + ', '.join(f'0x{v:08x}u' for v in words[i:i+6]) + ',\n'
                    for i in range(0, len(words), 6))
    record = dict(schema=1, source=str(source.relative_to(ROOT)), source_sha256=before[source],
                  compiler='native d3dx9_37.dll D3DXCompileShader', compiler_sha256=before[compiler_path],
                  entry='main', target=target, flags=32768, flags_name='D3DXSHADER_OPTIMIZATION_LEVEL3',
                  defines=None, includes={str(path.relative_to(ROOT)): before[path] for path in included} or None,
                  word_count=len(words), bytecode_sha256=sha(data),
                  header_sha256=sha(text.encode()),
                  tool_sources={str(path.relative_to(ROOT)): before[path] for path in inputs[1:3]},
                  copyright_scope='Original authored project shader; no game shader bytes',
                  creates_d3d_device=False)
    manifest = json.dumps(record, indent=2) + '\n'
    if args.check:
        if header.read_text() != text or provenance.read_text() != manifest:
            raise ValueError('%s: embedded shader/provenance differs from current native compilation' % name)
    else:
        header.write_text(text)
        provenance.write_text(manifest)
    return dict(shader=name, word_count=len(words), bytecode_sha256=sha(data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--shader', choices=sorted(SHADERS), action='append',
                        help='fragment to (re)compile; default: every fragment')
    parser.add_argument('--d3dx', type=Path, default=bottle.game_dir() / 'd3dx9_37.dll')
    args = parser.parse_args()
    if game_running():
        raise RuntimeError('X3AP running or process inventory failed; postpone compilation')
    results = [compile_one(name, args) for name in (args.shader or sorted(SHADERS))]
    print(json.dumps(dict(result='PASS', check=args.check, shaders=results)))


if __name__ == '__main__':
    main()
