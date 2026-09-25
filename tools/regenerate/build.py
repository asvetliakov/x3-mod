#!/usr/bin/env python3
"""Build the one-file x3m-regenerate executable (tools/regenerate/x3m_regenerate.spec) and smoke-test it.

  build.py                 host build (macOS/Linux): dist/x3m-regenerate with this Python's PyInstaller,
                           NumPy 2.0.2 and Pillow (pip install --user pyinstaller), then the smoke test
  build.py --windows       dist/x3m-regenerate.exe built by Windows Python + PyInstaller under Wine in the
                           dedicated CrossOver bottle X3M-Build (created on first use from the win10_64
                           template; never the game bottle), then the same smoke test under Wine
  build.py --windows --dry report what exists (bottle, Windows Python, packages); change nothing
  build.py --smoke-only [--windows]   rerun the smoke test on the existing executable

The smoke test builds the synthetic game root of verification/analysis/test_regenerate.py in a temporary
directory, runs the executable with --game-dir ROOT --no-wait and requires exit 0 and the per-item lines
in the console and in x3m-regenerate.log; it prints the bundle size and the start time (first run of a
fresh binary = cold, then warm) measured on a directory without X3AP.exe (the quickest complete run).

The bottle X3M-Build is the reusable build bottle: it holds only Windows Python 3.12.10 and the build
packages (never the game), stays in place between builds, and a later --windows run goes straight to the
pip and PyInstaller steps; the installer download is deleted once Python is installed (it is fetched
again only when the bottle is recreated).

Windows steps (each Wine command runs through verification/probe/wine_lock.py with
X3M_FIXTURE_BOTTLE=X3M-Build after the game guard reports no game):
  1. cxbottle --bottle X3M-Build --create --template win10_64
  2. download python-3.12.10-amd64.exe (python.org) to /tmp/x3m-build/ and run it
     /quiet InstallAllUsers=0 PrependPath=1 Include_test=0
  3. python.exe -m pip install pyinstaller numpy==2.0.2 pillow
  4. python.exe -m PyInstaller --noconfirm --distpath Z:<repo>/dist --workpath Z:<repo>/build/regenerate-win
     Z:<repo>/tools/regenerate/x3m_regenerate.spec
On a Windows PC the same steps are: install Python 3.12 (python.org, 64-bit), `py -m pip install pyinstaller
numpy==2.0.2 pillow`, then `py -m PyInstaller --noconfirm --distpath dist --workpath build\\regenerate-win
tools\\regenerate\\x3m_regenerate.spec` from the repository root (docs/user/regenerate.md, "Building").
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / 'tools' / 'regenerate' / 'x3m_regenerate.spec'
DIST = ROOT / 'dist'
CX_BIN = Path('/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin')
BOTTLE = 'X3M-Build'
BOTTLE_DIR = Path.home() / 'Library' / 'Application Support' / 'CrossOver' / 'Bottles' / BOTTLE
PY_VERSION = '3.12.10'
PY_URL = f'https://www.python.org/ftp/python/{PY_VERSION}/python-{PY_VERSION}-amd64.exe'
DOWNLOADS = Path('/tmp/x3m-build')
WIN_PACKAGES = ['pyinstaller', 'numpy==2.0.2', 'pillow']
EXPECTED = ('processing fog zzpkg', 'fog layers:', 'processing model ships/x/good', 'refused model ships/x/badtext',
            'slot plan: addon/01', 'fog families: check passed', 'all done')


def run(cmd, **kw):
    print('+ ' + ' '.join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], **kw)


# --- host build -----------------------------------------------------------------------------------

def host_build():
    import numpy
    if numpy.__version__ != '2.0.2':
        raise SystemExit(f'NumPy 2.0.2 required (fog_families checks it at run time), found {numpy.__version__}')
    rc = run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--distpath', DIST,
              '--workpath', ROOT / 'build' / 'regenerate', SPEC]).returncode
    if rc:
        raise SystemExit(f'PyInstaller failed ({rc})')
    return DIST / 'x3m-regenerate'


# --- Windows build in the X3M-Build bottle --------------------------------------------------------

def zpath(path):
    """Z: drive path of a host path (CrossOver maps / to Z:)."""
    return 'Z:' + str(Path(path).resolve()).replace('/', '\\')


def game_guard_clear():
    sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
    import game_guard
    lines = game_guard.game_running()
    if lines:
        raise SystemExit(f'the game is running ({lines[0]}); no Wine command is started')


def wine(args, check=True, **kw):
    """One Wine command in the build bottle, serialised by wine_lock (never two at once)."""
    game_guard_clear()
    env = dict(os.environ, X3M_FIXTURE_BOTTLE=BOTTLE)
    cmd = [sys.executable, ROOT / 'verification' / 'probe' / 'wine_lock.py', '--holder', f'x3m-regenerate build ({BOTTLE})',
           CX_BIN / 'wine', '--bottle', BOTTLE] + list(args)
    t0 = time.monotonic()
    result = run(cmd, env=env, **kw)
    print(f'  (exit {result.returncode}, {time.monotonic() - t0:.1f} s)', flush=True)
    if check and result.returncode:
        raise SystemExit(f'Wine command failed with exit {result.returncode}')
    return result


def windows_python():
    found = sorted(BOTTLE_DIR.glob('drive_c/users/*/AppData/Local/Programs/Python/Python3*/python.exe'))
    return found[-1] if found else None


def win_path(host):
    """C: path of a file inside the bottle's drive_c."""
    rel = Path(host).relative_to(BOTTLE_DIR / 'drive_c')
    return 'C:\\' + str(rel).replace('/', '\\')


def windows_status():
    py = windows_python()
    print(f'bottle {BOTTLE}: {"present" if BOTTLE_DIR.is_dir() else "absent"} ({BOTTLE_DIR})')
    print(f'Windows Python: {py or "not installed"}')
    site = sorted(BOTTLE_DIR.glob('drive_c/users/*/AppData/Local/Programs/Python/Python3*/Lib/site-packages/*.dist-info'))
    print('packages: ' + (', '.join(p.name[:-len('.dist-info')] for p in site
                                    if p.name.lower().startswith(('pyinstaller', 'numpy', 'pillow'))) or 'none'))
    exe = DIST / 'x3m-regenerate.exe'
    print(f'executable: {exe if exe.is_file() else "not built"}')


def windows_build():
    if not (CX_BIN / 'wine').exists():
        raise SystemExit(f'CrossOver Preview not found at {CX_BIN}')
    if not BOTTLE_DIR.is_dir():
        game_guard_clear()
        rc = run([CX_BIN / 'cxbottle', '--bottle', BOTTLE, '--create', '--template', 'win10_64',
                  '--description', 'x3-modern: Windows build of x3m-regenerate (no game)']).returncode
        if rc:
            raise SystemExit(f'cxbottle --create failed ({rc})')
    py = windows_python()
    if py is None:
        DOWNLOADS.mkdir(parents=True, exist_ok=True)
        installer = DOWNLOADS / PY_URL.rsplit('/', 1)[1]
        if not installer.is_file():
            print(f'downloading {PY_URL}', flush=True)
            urllib.request.urlretrieve(PY_URL, installer)
        wine([zpath(installer), '/quiet', 'InstallAllUsers=0', 'PrependPath=1', 'Include_test=0',
              'Include_launcher=0', 'Include_tcltk=0', 'Include_doc=0'])
        py = windows_python()
        if py is None:
            raise SystemExit('the Python installer finished but python.exe is not in the bottle')
        installer.unlink(missing_ok=True)
    wine([win_path(py), '-m', 'pip', 'install', '--disable-pip-version-check', *WIN_PACKAGES])
    wine([win_path(py), '-m', 'PyInstaller', '--noconfirm', '--clean', '--distpath', zpath(DIST),
          '--workpath', zpath(ROOT / 'build' / 'regenerate-win'), zpath(SPEC)])
    exe = DIST / 'x3m-regenerate.exe'
    if not exe.is_file():
        raise SystemExit('PyInstaller finished but dist/x3m-regenerate.exe is missing')
    return exe


# --- smoke test -----------------------------------------------------------------------------------

def smoke(exe, windows=False):
    sys.path.insert(0, str(ROOT / 'verification' / 'analysis'))
    sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
    from test_regenerate import make_root

    def launch(game):
        args = ['--game-dir', zpath(game) if windows else game, '--no-wait', '--jobs', '2']
        t0 = time.monotonic()
        if windows:
            result = wine([zpath(exe), *args], check=False, capture_output=True, text=True, errors='replace')
        else:
            result = run([exe, *args], capture_output=True, text=True)
        return result, time.monotonic() - t0
    print(f'bundle {exe}: {exe.stat().st_size} B ({exe.stat().st_size / 2**20:.1f} MiB)')
    with tempfile.TemporaryDirectory(prefix='x3m-regen-smoke-') as folder:
        empty = Path(folder) / 'empty'
        empty.mkdir()
        starts = []
        for _ in range(2):
            result, seconds = launch(empty)
            starts.append(seconds)
            if result.returncode != 1 or 'holds no X3AP.exe' not in result.stdout:
                raise SystemExit(f'start check: exit {result.returncode}, output {result.stdout[-400:]!r}')
        print(f'start (directory without X3AP.exe, exit 1 as designed): cold {starts[0]:.2f} s, warm {starts[1]:.2f} s')
        game = make_root(Path(folder) / 'root')
        result, seconds = launch(game)
        log = (game / 'x3m-regenerate.log').read_text(encoding='utf-8')
        missing = [e for e in EXPECTED if e not in result.stdout or e not in log]
        print(result.stdout)
        if result.returncode != 0 or missing:
            print(result.stderr[-2000:])
            raise SystemExit(f'smoke test failed: exit {result.returncode}, missing {missing}')
        print(f'smoke test PASS: exit 0 in {seconds:.1f} s, {sum(1 for l in log.splitlines() if "processing " in l)}'
              f' processing lines, log {len(log)} B')


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--windows', action='store_true', help='build x3m-regenerate.exe under Wine in bottle X3M-Build')
    ap.add_argument('--dry', action='store_true', help='with --windows: report the bottle state only')
    ap.add_argument('--smoke-only', action='store_true', help='skip the build, test the existing executable')
    a = ap.parse_args(argv)
    if a.windows and a.dry:
        windows_status()
        return 0
    if a.smoke_only:
        exe = DIST / ('x3m-regenerate.exe' if a.windows else 'x3m-regenerate')
    else:
        exe = windows_build() if a.windows else host_build()
    smoke(exe, a.windows)
    return 0


if __name__ == '__main__':
    sys.exit(main())
