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
    parser.add_argument('--capture-start', type=int, default=120)
    parser.add_argument('--capture-frames', type=int, choices=range(0, 9), default=1)
    parser.add_argument('--direct', action='store_true', help='Skip launcher and intro using X3 command-line switches')
    parser.add_argument('--vanilla', action='store_true', help='Launch with builtin D3D9, ignoring the installed proxy')
    parser.add_argument('--telemetry', action='store_true', help='Enable bounded loading, presentation and cursor diagnostics')
    args = parser.parse_args()
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
        source = ROOT / 'build/d3d9.dll'
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
        # --dll applies to this child only, preserving the user's other overrides.
        command = [str(WINE), '--bottle', args.bottle, '--no-update',
                   '--dll', 'd3d9=b' if args.vanilla else 'd3d9=n,b',
                   '--workdir', str(game), str(game / 'X3AP.exe')]
        if args.direct:
            command += ['-noabout', '-skipintro', '-runinbg']
        print('Launching X3AP through CrossOver Preview.', flush=True)
        raise SystemExit(subprocess.call(command, env=env, cwd=game))


if __name__ == '__main__':
    main()
