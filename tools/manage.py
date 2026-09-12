#!/usr/bin/env python3
"""Install/remove the owned app-local proxy and launch through CrossOver Preview.

No registry or bottle-wide DLL override is changed. Refuse to overwrite an
unowned d3d9.dll or remove a file whose contents changed after installation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
GAME = Path.home() / 'Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3'
WINE = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['install', 'uninstall', 'launch', 'status'])
    parser.add_argument('--game-dir', type=Path, default=GAME)
    parser.add_argument('--bottle', default='Steam')
    parser.add_argument('--dll-source', type=Path, default=ROOT / 'build/d3d9.dll',
                        help='DLL to install (defaults to build/d3d9.dll; other actions do not use it)')
    parser.add_argument('--capture-start', type=int, default=120)
    parser.add_argument('--capture-frames', type=int, choices=range(0, 9), default=1)
    parser.add_argument('--direct', action='store_true', help='Skip launcher and intro using X3 command-line switches')
    parser.add_argument('--vanilla', action='store_true', help='Launch with builtin D3D9, ignoring the installed proxy')
    parser.add_argument('--telemetry', action='store_true', help='Enable bounded loading, presentation and cursor diagnostics')
    parser.add_argument('--ownership', action='store_true', help='Enable the experimental normal-D3D9 ownership wrapper')
    parser.add_argument('--depth-copy', action='store_true', help='Enable experimental original-preserving depth copy (requires --ownership)')
    parser.add_argument('--scene-depth-capture', action='store_true', help='Preserve identified scene depth in requested capture frames (requires --ownership --depth-copy)')
    parser.add_argument('--object-trace', action='store_true', help='Capture verified engine submission identity (exact executable only)')
    parser.add_argument('--object-lifetime', action='store_true', help='Observe verified render-registry lifetimes (requires --object-trace --ownership)')
    parser.add_argument('--mesh-cache', action='store_true', help='Enable experimental verified native adjacency reuse (requires --telemetry)')
    parser.add_argument('--profile', action='store_true', help='Run the in-process sampling profiler (X3M_PROFILE=1): one sampler thread, periodic profile_* reports in the session log; see docs/verification/sampling-profiler.md')
    parser.add_argument('--profile-interval-us', type=int, default=2000, help='Sampling interval in microseconds for --profile (100..1000000, default 2000)')
    parser.add_argument('--finite-positions', action='store_true', help='Validate positions from verified existing buffer uploads (requires --ownership --telemetry)')
    parser.add_argument('--motion-capture', action='store_true', help='Produce private rigid-motion diagnostics during capture (requires scene depth, finite positions and object lifetime)')
    parser.add_argument('--motion-output', action='store_true', help='Route the reviewed material pair through motion-output variants into a private RT1 (history needs --object-trace --object-lifetime; otherwise sentinel-only)')
    parser.add_argument('--motion-jitter', action='store_true', help='Per-draw sub-pixel jitter of every scene draw with a table VS (requires --motion-output; Halton 2,3 sequence, temporal step 1)')
    parser.add_argument('--taa', action='store_true', help='Run the temporal resolve at the bloom copy and present the resolved image (requires --motion-output; implies --motion-jitter; temporal step 3)')
    parser.add_argument('--taa-debug', action='store_true', help='Write the resolved FP16 image and the pre-resolve color in capture frames (requires --taa)')
    parser.add_argument('--motion-rt-mode', choices=['perdraw', 'lazy'], default='perdraw', help='RT1/RT2 binding policy of the route: perdraw (default) rebinds around every routed draw; lazy keeps the bindings across consecutive routed draws (A/B experiment, requires --motion-output)')
    parser.add_argument('--dry-run', action='store_true', help='launch only: validate the options and installation, print the command and X3M_* environment as JSON, and exit without launching')
    args = parser.parse_args()
    if args.dry_run and args.action != 'launch':
        parser.error('--dry-run applies to launch only.')
    if args.depth_copy and not args.ownership:
        parser.error('--depth-copy requires --ownership.')
    if args.scene_depth_capture and not (args.ownership and args.depth_copy):
        parser.error('--scene-depth-capture requires --ownership and --depth-copy.')
    if args.object_lifetime and not (args.object_trace and args.ownership):
        parser.error('--object-lifetime requires --object-trace and --ownership.')
    if args.mesh_cache and not args.telemetry:
        parser.error('--mesh-cache requires --telemetry.')
    if args.finite_positions and not (args.ownership and args.telemetry):
        parser.error('--finite-positions requires --ownership and --telemetry.')
    if args.motion_capture and not (args.scene_depth_capture and args.finite_positions and args.object_lifetime):
        parser.error('--motion-capture requires --scene-depth-capture, --finite-positions and --object-lifetime.')
    if args.motion_output and (args.object_trace != args.object_lifetime):
        parser.error('--motion-output history needs both --object-trace and --object-lifetime, or neither for sentinel-only mode.')
    if args.motion_jitter and not args.motion_output:
        parser.error('--motion-jitter requires --motion-output.')
    if args.taa and not args.motion_output:
        parser.error('--taa requires --motion-output.')
    if args.taa and not (args.object_trace and args.object_lifetime):
        parser.error('--taa requires --object-trace and --object-lifetime: without history every routed draw carries the sentinel, the resolve stays current-only and the jitter only moves the image.')
    if args.taa_debug and not args.taa:
        parser.error('--taa-debug requires --taa.')
    if args.motion_rt_mode != 'perdraw' and not args.motion_output:
        parser.error('--motion-rt-mode requires --motion-output.')
    if not 100 <= args.profile_interval_us <= 1000000:
        parser.error('--profile-interval-us must be between 100 and 1000000.')
    if args.taa:
        args.motion_jitter = True
    if args.motion_capture and args.capture_frames < 2:
        parser.error('--motion-capture requires --capture-frames between 2 and 8 for adjacent-frame correspondence.')
    game = args.game_dir.resolve()
    dll = game / 'd3d9.dll'
    manifest = game / 'x3-modern-install.json'
    if not (game / 'X3AP.exe').is_file():
        parser.error(f'X3AP.exe not found in {game}')
    owned = json.loads(manifest.read_text()) if manifest.exists() else None
    if args.action == 'status':
        print(json.dumps({'game': str(game), 'dll_present': dll.exists(),
                          'owned': bool(owned and dll.exists() and digest(dll) == owned['sha256']),
                          'installation': owned}, indent=2))
        return
    if args.action == 'install':
        source = args.dll_source.resolve()
        if not source.is_file():
            parser.error('Build the DLL first (see README.md).')
        if dll.exists() and (not owned or digest(dll) != owned['sha256']):
            parser.error('Existing d3d9.dll is unowned or changed; refusing to overwrite it.')
        temp = game / 'x3-modern-install.tmp'
        shutil.copy2(source, temp)
        os.replace(temp, dll)
        manifest.write_text(json.dumps({'project': 'x3-modern-renderer', 'sha256': digest(dll),
                                        'source': str(source)}, indent=2) + '\n')
        print(f'Installed {dll}; bottle configuration unchanged.')
    elif args.action == 'uninstall':
        if not owned:
            parser.error('No installation manifest; refusing to remove an unowned file.')
        if dll.exists() and digest(dll) != owned['sha256']:
            parser.error('Installed DLL changed; refusing to remove it.')
        if dll.exists():
            dll.unlink()
        manifest.unlink()
        print('Removed owned proxy and manifest; captures retained.')
    elif args.action == 'launch':
        if not WINE.is_file():
            parser.error(f'CrossOver Preview Wine not found: {WINE}')
        if not args.vanilla and (not owned or not dll.exists() or digest(dll) != owned['sha256']):
            parser.error('Install the proxy before launch, or use --vanilla.')
        env = os.environ.copy()
        env['X3M_CAPTURE_START'] = str(max(1, args.capture_start))
        env['X3M_CAPTURE_FRAMES'] = str(args.capture_frames)
        env['X3M_TELEMETRY'] = '1' if args.telemetry else '0'
        env['X3M_OWNERSHIP'] = '1' if args.ownership else '0'
        env['X3M_DEPTH_COPY'] = '1' if args.depth_copy else '0'
        env['X3M_SCENE_DEPTH_CAPTURE'] = '1' if args.scene_depth_capture else '0'
        env['X3M_OBJECT_TRACE'] = '1' if args.object_trace else '0'
        env['X3M_OBJECT_LIFETIME'] = '1' if args.object_lifetime else '0'
        env['X3M_MESH_CACHE'] = '1' if args.mesh_cache else '0'
        env['X3M_FINITE_POSITIONS'] = '1' if args.finite_positions else '0'
        env['X3M_MOTION_CAPTURE'] = '1' if args.motion_capture else '0'
        env['X3M_MOTION_OUTPUT'] = '1' if args.motion_output else '0'
        env['X3M_MOTION_JITTER'] = '1' if args.motion_jitter else '0'
        env['X3M_TAA'] = '1' if args.taa else '0'
        env['X3M_TAA_DEBUG'] = '1' if args.taa_debug else '0'
        env['X3M_MOTION_RT_MODE'] = args.motion_rt_mode
        env['X3M_PROFILE'] = '1' if args.profile else '0'
        env['X3M_PROFILE_INTERVAL_US'] = str(args.profile_interval_us)
        # --dll applies to this child only, preserving the user's other overrides.
        command = [str(WINE), '--bottle', args.bottle, '--no-update',
                   '--dll', 'd3d9=b' if args.vanilla else 'd3d9=n,b',
                   '--workdir', str(game), str(game / 'X3AP.exe')]
        if args.direct:
            command += ['-noabout', '-skipintro', '-runinbg']
        if args.dry_run:
            print(json.dumps({'command': command, 'cwd': str(game),
                              'env': {k: env[k] for k in sorted(env) if k.startswith('X3M_')}}, indent=2))
            return
        print('Launching X3AP through CrossOver Preview.', flush=True)
        raise SystemExit(subprocess.call(command, env=env, cwd=game))


if __name__ == '__main__':
    main()
