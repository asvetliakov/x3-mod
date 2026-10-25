#!/usr/bin/env python3
"""Build the one-file x3m-regenerate executable (tools/regenerate/x3m_regenerate.spec) and smoke-test it.

  build.py                 host build (Linux/macOS): dist/x3m-regenerate with this Python's PyInstaller,
                           NumPy 2.x and Pillow (pip install --user pyinstaller), then the smoke test
  build.py --windows       dist/x3m-regenerate.exe built from WSL2 by a native Windows Python + PyInstaller,
                           then the same smoke test run natively on Windows
  build.py --windows --dry report the Windows Python, its version and the three package versions; change nothing
                           (exit 1 when something is missing, with the install instructions)
  build.py --smoke-only [--windows]   rerun the smoke test on the existing executable
  --dist DIR               write (or, with --smoke-only, test) the executable in DIR instead of <repo>/dist
                           (tools/release/release.py uses it)
  --windows-python PATH    the Windows python.exe (C:\\... or /mnt/c/... form); default $X3M_WINDOWS_PYTHON or
                           C:\\Python312\\python.exe

The smoke test builds the synthetic game root of verification/analysis/test_regenerate.py in a temporary
directory, runs the executable with --game-dir ROOT --no-wait and requires exit 0 and the per-item lines
in the console and in x3m-regenerate.log; it prints the bundle size, its sha256 and the start time (first run
of a fresh binary = cold, then warm) measured on a directory without X3AP.exe (the quickest complete run).

Windows build: only on a WSL2 host (/proc/sys/kernel/osrelease names microsoft) with Windows Python 3.12.10
and the pinned packages (WINDOWS_SETUP; build.py never downloads or installs anything). Paths are mapped with
wslpath in both directions. PyInstaller gets only drive paths, never the \\\\wsl.localhost\\... share (its
analysis and work tree on a UNC path are not supported), so the build stages its inputs on the Windows disk, in
%LOCALAPPDATA%\\x3m-build (a drive path; a UNC LOCALAPPDATA is refused before anything is created):
  0. x3m-build\\build.lock is taken exclusively (O_EXCL, '<pid> <time>'; a lock whose pid is gone is removed)
     for the whole build and smoke test, so two runs never wipe each other's staging; released at exit
  1. src\\ is wiped and refilled with the spec and the *.py files of the directories the spec searches
     (STAGE_DIRS, same relative layout, byte copies, so the bundled tool hashes equal the checkout's)
  2. dist\\x3m-regenerate.exe is deleted, then python.exe -m PyInstaller --noconfirm --clean --distpath dist
     --workpath work src\\tools\\regenerate\\x3m_regenerate.spec runs with the working directory on C:
  3. dist\\x3m-regenerate.exe is copied to --dist; the smoke test runs the staged copy (same sha256) on a
     synthetic root under %LOCALAPPDATA%\\x3m-build\\smoke, so the start times exclude reading the 30 MB
     executable over the WSL share; they do include the WSL-to-Windows process launch.
The synthetic root is written by the Windows Python itself (test_regenerate.py --make-root; the WSL python3 may
lack Pillow). That one script is run from its \\\\wsl.localhost\\... path: plain CPython imports from a UNC
sys.path entry without trouble (measured: the smoke tests passed this way), it only reads test sources there and
writes the root on C:, so it is not staged.
Nothing here runs under Wine and nothing touches the game directory, so no game guard or Wine lock applies (the
former macOS route through the CrossOver bottle X3M-Build is retired).
On a Windows PC without WSL: from the repository root, `C:\\Python312\\python.exe -m PyInstaller --noconfirm
--distpath dist --workpath build\\regenerate-win tools\\regenerate\\x3m_regenerate.spec` (docs/user/regenerate.md,
"Building").
"""
from __future__ import annotations

import argparse
import contextlib
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = ROOT / 'tools' / 'regenerate' / 'x3m_regenerate.spec'
DIST = ROOT / 'dist'
OSRELEASE = Path('/proc/sys/kernel/osrelease')
PY_VERSION = '3.12.10'
DEFAULT_WINDOWS_PYTHON = r'C:\Python312\python.exe'
# The versions recorded for the 2026-09-25 Windows build in the X3M-Build bottle (docs/verification/lod-overlay.md);
# the released 1.1.0 exe (bb3ba27b...) was built with unpinned pyinstaller and pillow, so these pins reproduce the
# recorded toolchain, not necessarily bb3ba27b's. NumPy stays 2.0.2: the fog_families bake has no pinned-hash gate.
WIN_PACKAGES = {'pyinstaller': '6.22.3', 'numpy': '2.0.2', 'pillow': '12.3.0'}
PIP_LINE = DEFAULT_WINDOWS_PYTHON + ' -m pip install ' + ' '.join(f'{n}=={v}' for n, v in WIN_PACKAGES.items())
WINDOWS_SETUP = (f'Install Windows Python {PY_VERSION} once: download the python.org 64-bit installer '
                 f'https://www.python.org/ftp/python/{PY_VERSION}/python-{PY_VERSION}-amd64.exe, run '
                 f'`python-{PY_VERSION}-amd64.exe /quiet InstallAllUsers=0 TargetDir=C:\\Python312 PrependPath=0`, '
                 f'then `{PIP_LINE}`; check with `python3 tools/regenerate/build.py --windows --dry`.')
STAGE_DIRS = ('tools', 'tools/regenerate', 'tools/analysis', 'tools/build', 'tools/effects', 'verification/probe')
DRIVE_PATH = re.compile(r'^[A-Za-z]:\\')
EXPECTED = ('processing fog zzpkg', 'fog layers:', 'processing model ships/x/good', 'refused model ships/x/badtext',
            'slot plan: addon/01', 'engine bodies summary:', 'fog families: check passed', 'all done')


def run(cmd, **kw):
    print('+ ' + ' '.join(str(c) for c in cmd), flush=True)
    return subprocess.run([str(c) for c in cmd], **kw)


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


# --- host build -----------------------------------------------------------------------------------

def host_build():
    import numpy
    if numpy.__version__.split('.', 1)[0] != '2':
        raise SystemExit(f'NumPy 2.x required (fog_families checks it at run time), found {numpy.__version__}')
    rc = run([sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--distpath', DIST,
              '--workpath', ROOT / 'build' / 'regenerate', SPEC]).returncode
    if rc:
        raise SystemExit(f'PyInstaller failed ({rc})')
    return DIST / 'x3m-regenerate'


# --- Windows build from WSL2 ----------------------------------------------------------------------

def is_wsl(osrelease=None):
    try:
        return 'microsoft' in Path(osrelease or OSRELEASE).read_text(encoding='ascii', errors='replace').lower()
    except OSError:
        return False


def wslpath(flag, path):
    try:
        result = subprocess.run(['wslpath', flag, str(path)], capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise SystemExit(f'wslpath {flag} {path}: {error}')
    if result.returncode or not result.stdout.strip():
        raise SystemExit(f'wslpath {flag} {path} failed (exit {result.returncode}): {result.stderr.strip()}')
    return result.stdout.strip()


def to_wsl(path):
    """The WSL path of a Windows path (wslpath -u); a POSIX path is returned unchanged."""
    text = str(path)
    return Path(text) if text.startswith('/') else Path(wslpath('-u', text))


def to_windows(path):
    """The Windows drive path of a WSL path (wslpath -w); a path that maps to a UNC share is refused, because
    Windows programs here only get drive paths."""
    text = wslpath('-w', os.path.abspath(path))
    if not DRIVE_PATH.match(text):
        raise SystemExit(f'{path} is not on a Windows drive ({text})')
    return text


def windows_python_path(override=None):
    return to_wsl(override or os.environ.get('X3M_WINDOWS_PYTHON') or DEFAULT_WINDOWS_PYTHON)


def probe_windows_python(python):
    """(version, {package: version or None}, LOCALAPPDATA or None) as the Windows Python reports them; raises
    RuntimeError with the reason when it cannot be run."""
    code = ('import importlib.metadata as m, os, platform\n'
            'def v(n):\n'
            '    try: return m.version(n)\n'
            '    except m.PackageNotFoundError: return "-"\n'
            'print(platform.python_version() + " (" + platform.machine() + ")", os.environ.get("LOCALAPPDATA", "-"), '
            f'*[v(n) for n in {list(WIN_PACKAGES)!r}], sep="|")\n')
    try:
        result = subprocess.run([str(python), '-c', code], capture_output=True, text=True, errors='replace',
                                cwd=python.parent, timeout=120)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise RuntimeError(f'{python} could not be run: {error}')
    fields = result.stdout.strip().split('|')
    if result.returncode or len(fields) != 2 + len(WIN_PACKAGES):
        raise RuntimeError(f'{python} -c failed (exit {result.returncode}): {result.stderr.strip()[-400:]}')
    version, local, *packages = fields
    return version, {n: (None if p == '-' else p) for n, p in zip(WIN_PACKAGES, packages)}, (None if local == '-' else local)


def windows_check(override=None):
    """(ready, text, python, LOCALAPPDATA): a WSL2 host with Windows Python 3.12.10, the pinned packages and a
    drive-path LOCALAPPDATA. Never raises for a missing or broken Python; the reason is in text."""
    if not is_wsl():
        return False, 'not a WSL2 host (/proc/sys/kernel/osrelease does not name microsoft)', None, None
    try:
        python = windows_python_path(override)
    except SystemExit as error:
        return False, f'Windows Python path not mappable: {error}', None, None
    if not python.is_file():
        return False, f'Windows Python not found at {python}', python, None
    try:
        version, packages, local = probe_windows_python(python)
    except RuntimeError as error:
        return False, f'Windows Python not usable: {error}', python, None
    try:
        shown = to_windows(python)
    except SystemExit:
        shown = python
    text = f'Windows Python {shown} {version}; ' + ', '.join(f'{n} {packages[n] or "missing"}' for n in WIN_PACKAGES)
    wrong = [f'{n} {packages[n] or "missing"} (want {v})' for n, v in WIN_PACKAGES.items() if packages[n] != v]
    if not version.startswith(PY_VERSION + ' '):
        wrong.insert(0, f'Python {version} (want {PY_VERSION})')
    if local is None:
        wrong.append('no LOCALAPPDATA')
    elif not DRIVE_PATH.match(local):
        wrong.append(f'LOCALAPPDATA {local} is not a drive path (a UNC staging directory is not supported)')
    return not wrong, text + ('; wrong: ' + ', '.join(wrong) if wrong else ''), python, local


def windows_ready(override=None):
    """(ready, text) for tools/release/release.py."""
    ready, text, _, _ = windows_check(override)
    return ready, text


def windows_status(override=None):
    ready, text, _, _ = windows_check(override)
    print(text)
    exe = DIST / 'x3m-regenerate.exe'
    print(f'executable: {exe if exe.is_file() else "not built"}')
    if not ready:
        print(WINDOWS_SETUP)
    return ready


def require_windows(override=None):
    """(python, stage directory) or SystemExit with the reason and the install instructions; creates nothing."""
    ready, text, python, local = windows_check(override)
    print(text, flush=True)
    if not ready:
        raise SystemExit(text + '. ' + WINDOWS_SETUP)
    return python, to_wsl(local) / 'x3m-build'


def pid_alive(pid):
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


@contextlib.contextmanager
def stage_lock(stage):
    """Exclusive use of the shared staging directory: O_EXCL lock file '<pid> <time>', stale lock (dead pid or
    unreadable) removed once, released in finally. A live holder refuses the run instead of waiting."""
    stage.mkdir(parents=True, exist_ok=True)
    lock = stage / 'build.lock'
    for attempt in range(2):
        try:
            fd = os.open(lock, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o644)
            break
        except FileExistsError:
            try:
                holder = int(lock.read_text(encoding='ascii', errors='replace').split()[0])
            except (OSError, ValueError, IndexError):
                holder = None
            try:
                young = time.time() - lock.stat().st_mtime < 30  # an empty lock may be a holder between open and write
            except OSError:
                young = False
            if attempt or (pid_alive(holder) if holder is not None else young):
                raise SystemExit(f'{lock} is held by pid {holder} (another build.py --windows); retry when it ends')
            print(f'removing stale {lock} (pid {holder} is gone)', flush=True)
            lock.unlink(missing_ok=True)
    try:
        os.write(fd, f'{os.getpid()} {time.strftime("%Y-%m-%dT%H:%M:%S")}\n'.encode('ascii'))
        os.close(fd)
        yield
    finally:
        lock.unlink(missing_ok=True)


def stage_sources(stage):
    src = stage / 'src'
    if src.exists():
        shutil.rmtree(src)
    count = 0
    for rel in STAGE_DIRS:
        target = src / rel
        target.mkdir(parents=True, exist_ok=True)
        for path in sorted((ROOT / rel).glob('*.py')):
            shutil.copyfile(path, target / path.name)  # copyfile, not copy2: copystat can fail on DrvFs
            count += 1
    spec = src / 'tools' / 'regenerate' / SPEC.name
    shutil.copyfile(SPEC, spec)
    print(f'staged {count} sources and the spec in {to_windows(src)}', flush=True)
    return spec


def windows_build(python, stage):
    spec = stage_sources(stage)
    built = stage / 'dist' / 'x3m-regenerate.exe'
    built.unlink(missing_ok=True)  # a run that writes no exe must not ship the previous one
    t0 = time.monotonic()
    rc = run([python, '-m', 'PyInstaller', '--noconfirm', '--clean', '--distpath', to_windows(stage / 'dist'),
              '--workpath', to_windows(stage / 'work'), to_windows(spec)], cwd=stage).returncode
    print(f'  (PyInstaller exit {rc}, {time.monotonic() - t0:.1f} s)', flush=True)
    if rc:
        raise SystemExit(f'PyInstaller failed ({rc})')
    if not built.is_file():
        raise SystemExit(f'PyInstaller finished but {built} is missing')
    DIST.mkdir(parents=True, exist_ok=True)
    exe = DIST / 'x3m-regenerate.exe'
    shutil.copyfile(built, exe)
    return exe


# --- smoke test -----------------------------------------------------------------------------------

def windows_make_root(python):
    """make_root run by the Windows Python from the checkout's UNC path (module docstring)."""
    script = wslpath('-w', ROOT / 'verification' / 'analysis' / 'test_regenerate.py')

    def make_root(folder):
        folder = Path(folder)
        result = subprocess.run([str(python), script, '--make-root', to_windows(folder)], capture_output=True,
                                text=True, errors='replace', cwd=folder.parent)
        if result.returncode:
            raise SystemExit(f'make_root under Windows Python failed (exit {result.returncode}): {result.stderr[-1000:]}')
        return folder / 'drive_c' / 'X3'
    return make_root


def smoke(exe, windows=False, target=None):
    """target: (python, stage) of require_windows when windows is true."""
    digest = sha256(exe)
    print(f'bundle {exe}: {exe.stat().st_size} B ({exe.stat().st_size / 2**20:.1f} MiB), sha256 {digest}')
    base = None
    if not windows:
        sys.path.insert(0, str(ROOT / 'verification' / 'analysis'))
        sys.path.insert(0, str(ROOT / 'verification' / 'probe'))
        from test_regenerate import make_root
    else:
        python, stage = target
        make_root = windows_make_root(python)
        base = stage / 'smoke'
        base.mkdir(parents=True, exist_ok=True)
        staged = stage / 'dist' / 'x3m-regenerate.exe'
        if not staged.is_file() or sha256(staged) != digest:  # --smoke-only on another exe: run a copy on C:
            staged.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(exe, staged)
        print(f'runs the same bytes from {to_windows(staged)}')
        exe = staged

    def launch(game):
        args = ['--game-dir', to_windows(game) if windows else game, '--no-wait', '--jobs', '2']
        t0 = time.monotonic()
        result = run([exe, *args], capture_output=True, text=True, errors='replace', cwd=game.parent)
        return result, time.monotonic() - t0

    with tempfile.TemporaryDirectory(prefix='x3m-regen-smoke-', dir=base, ignore_cleanup_errors=True) as folder:
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
    global DIST
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--windows', action='store_true', help='build x3m-regenerate.exe with the native Windows Python (WSL2)')
    ap.add_argument('--dry', action='store_true', help='with --windows: report the Windows Python and packages only')
    ap.add_argument('--smoke-only', action='store_true', help='skip the build, test the existing executable')
    ap.add_argument('--dist', type=Path, default=None, help=f'output directory (default {DIST})')
    ap.add_argument('--windows-python', default=None,
                    help=f'Windows python.exe (default $X3M_WINDOWS_PYTHON or {DEFAULT_WINDOWS_PYTHON})')
    a = ap.parse_args(argv)
    if a.dist is not None:
        DIST = a.dist.resolve()
        DIST.mkdir(parents=True, exist_ok=True)
    if a.windows and a.dry:
        return 0 if windows_status(a.windows_python) else 1
    if not a.windows:
        smoke(DIST / 'x3m-regenerate' if a.smoke_only else host_build(), False)
        return 0
    target = require_windows(a.windows_python)  # probed and printed once per run
    with stage_lock(target[1]):
        exe = DIST / 'x3m-regenerate.exe' if a.smoke_only else windows_build(*target)
        smoke(exe, True, target)
    return 0


if __name__ == '__main__':
    sys.exit(main())
