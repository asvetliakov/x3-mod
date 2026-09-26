#!/usr/bin/env python3
"""Builds the player's release zip (docs/architecture/config-file.md, section 6).

    python3 tools/release/package.py --dll build/d3d9.dll --out DIR [--regenerate-dir /tmp/x3-regenerate-dist]
                                     [--debug-file DIR/d3d9.debug]

Writes DIR/x3m-<version>.zip with exactly four entries: d3d9.dll (the given DLL), x3m.ini (the generated template
assets/x3m.ini), x3m-regenerate.exe from the regenerate directory (built by tools/regenerate/build.py --windows) and
README.txt (unpack, keep an edited x3m.ini on update, regenerate, edit x3m.ini, send x3m.log; with the DLL's SHA-256 and
the source commit, and with --debug-file a line saying the DLL is stripped and naming the kept debug file's SHA-256).
CrossOver/macOS-only files never ship: no host x3m-regenerate binary and no WMA voice decoder tree (the developer bottle
gets that through `tools/manage.py voice-decoder --install`). Refuses when `tools/config/generate.py --check` fails, so a
stale template never ships, or when an input is missing. Entries carry a fixed timestamp, so the same inputs give the same
zip. Prints the listing (name, bytes, SHA-256 prefix).
"""
import argparse
import hashlib
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/config'))
import generate  # noqa: E402

TEMPLATE = ROOT / 'assets/x3m.ini'
DEFAULT_REGENERATE = Path('/tmp/x3-regenerate-dist')
REGENERATE_NAME = 'x3m-regenerate.exe'  # the only regenerate binary that ships (required)
STAMP = (2026, 1, 1, 0, 0, 0)

README = """X3 Modern Renderer {version}
===========================

A renderer mod for X3: Albion Prelude: temporal anti-aliasing, HDR with bloom, sun shadows, volumetric
nebula fog, a chase camera and faster loading.

Install
-------
1. Quit the game. Unpack the zip into the game folder, the folder with X3AP.exe
   (with CrossOver: ~/Library/Application Support/CrossOver/Bottles/<bottle>/drive_c/X3).
   Updating: let d3d9.dll, x3m-regenerate.exe and README.txt overwrite the old ones, but keep an x3m.ini you
   edited (do not overwrite it) and compare it with the new one for new settings. An older x3m.ini keeps
   working: settings it lacks take their defaults, settings the new version no longer has are ignored
   and named in x3m.log.
2. Run x3m-regenerate.exe once from that folder (with CrossOver: in the game's bottle). It prepares the fog
   colours and the distant ship and station models for your game and mods, asks nothing and ends with
   "all done". Run it again after installing, updating or removing a mod.
3. Start the game as usual.

Settings
--------
x3m.ini holds every setting a player may want to change, each with a short explanation. Every line is
commented out (";" in front) and shows the value the mod uses anyway, so the mod works the same without
the file. To change a setting, delete the ";" at the start of its line and edit the value; put the ";"
back to return to the default. The mod only reads the file, never changes it.

Problems
--------
The mod writes x3m.log next to d3d9.dll (the previous session's log is x3m.prev.log). Its first lines
list which settings x3m.ini changed (config_file) and any line it could not use (config_key). For a bug report, set debug = 1 in
x3m.ini, reproduce the problem, quit, and send x3m.log.

Uninstall: delete d3d9.dll, x3m.ini, x3m-regenerate*, x3m-regenerate.log, x3m.log and x3m.prev.log, and what
x3m-regenerate wrote: the folder x3m (fog-families.bin and .json), the overlay catalogue addon/NN.cat/.dat
next to its addon/NN.x3m-lod.json marker (and the marker), and addon/mods/<mod>-x3m-lod.cat/.dat/.x3m-lod.json.
No need to run x3m-regenerate afterwards.

Build: d3d9.dll SHA-256 {sha256}, source commit {commit}.{debug}
"""


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def source_commit():
    try:
        commit = subprocess.run(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], capture_output=True, text=True, check=True).stdout.strip()
        dirty = subprocess.run(['git', '-C', str(ROOT), 'status', '--porcelain', '--', 'src', 'tools/config', 'assets', 'CMakeLists.txt'],
                               capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return 'unknown'
    return commit + ('+dirty' if dirty else '')


def manifest(dll, regenerate_dir, version, commit, debug_file=None):
    """[(name in the zip, bytes)] in zip order; ValueError when an input is missing or the template is stale."""
    stale = [path for path, text in generate.outputs().items() if not path.is_file() or path.read_text() != text]
    if stale:
        raise ValueError('generate.py --check fails (stale: ' + ', '.join(str(p.relative_to(ROOT)) for p in stale) + ')')
    dll = Path(dll)
    if not dll.is_file():
        raise ValueError(f'DLL missing: {dll}')
    regenerate = Path(regenerate_dir) / REGENERATE_NAME
    if not regenerate.is_file():
        raise ValueError(f'{REGENERATE_NAME} missing in {regenerate_dir} (tools/regenerate/build.py --windows)')
    debug = ''
    if debug_file is not None:
        if not Path(debug_file).is_file():
            raise ValueError(f'debug file missing: {debug_file}')
        debug = (f'\nd3d9.dll is stripped of debug information; the developer keeps the matching d3d9.debug '
                 f'(SHA-256 {sha256(Path(debug_file).read_bytes())}).')
    dll_bytes = dll.read_bytes()
    entries = [('d3d9.dll', dll_bytes), ('x3m.ini', TEMPLATE.read_bytes()), (REGENERATE_NAME, regenerate.read_bytes())]
    readme = README.format(version=version, sha256=sha256(dll_bytes), commit=commit, debug=debug).replace('\n', '\r\n')
    entries.append(('README.txt', readme.encode()))
    return entries


def write_zip(path, entries):
    with zipfile.ZipFile(path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in entries:
            info = zipfile.ZipInfo(name, STAMP)
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o755 if name == REGENERATE_NAME else 0o644) << 16
            archive.writestr(info, data)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--dll', required=True, help='the d3d9.dll to ship (a built candidate)')
    parser.add_argument('--out', required=True, help='directory for x3m-<version>.zip')
    parser.add_argument('--regenerate-dir', default=str(DEFAULT_REGENERATE), help='directory with x3m-regenerate.exe')
    parser.add_argument('--debug-file', help='the d3d9.debug split from a stripped --dll (README.txt names its SHA-256)')
    args = parser.parse_args(argv)
    version = generate.version()
    try:
        entries = manifest(args.dll, args.regenerate_dir, version, source_commit(), args.debug_file)
    except ValueError as error:
        print(f'package.py: refused: {error}', file=sys.stderr)
        return 1
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    path = out / f'x3m-{version}.zip'
    write_zip(path, entries)
    print(path)
    for name, data in entries:
        print(f'  {name:56s} {len(data):>10d}  {sha256(data)[:16]}')
    print(f'  zip {path.stat().st_size} bytes, sha256 {sha256(path.read_bytes())[:16]}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
