"""Native Windows fixture host for the D3D9 fixtures (no Wine; never launches the game).

On the WSL2 host (since 2026-10-11) a MinGW-built fixture .exe runs on the Windows side through WSL interop
(binfmt_misc `WSLInterop`): the real D3D9 runtime and GPU driver. The process sees Windows path semantics, so every
path handed to it is a Windows path (`windows_path`); its working directory is the Linux one translated by interop
(a `\\\\wsl.localhost\\...` UNC path for a directory under the Linux home); run_engine_plumes.py ran from
verification/probe/build/engine-plumes/ that way on 2026-11-05, so no Windows-local copy is needed).

Records go to `verification/results/native-windows/<fixture>/` (docs/verification/bottles.md: any host other than
bottle X3 writes under its own directory and records its identity). `identity()` builds the record's `host` block:
host name, Windows version, the adapter as the fixture's D3D9 runtime reports it through GetAdapterIdentifier (the
fixture's ADAPTER / ADAPTER_DESC / ADAPTER_DRIVER rows) and the provenance (Windows path, size, SHA-256, PE file
version) of the d3d9.dll and d3dx9_37.dll the fixture loaded (its D3D9_MODULE / D3DX_MODULE rows).
"""
import hashlib
import os
import re
import shutil
import struct
import subprocess
from pathlib import Path

HOST = 'native-windows'
INTEROP = Path('/proc/sys/fs/binfmt_misc/WSLInterop')
COMPILER = 'i686-w64-mingw32-g++'
GAME_DIR = Path(os.environ.get('X3_DIR') or '/mnt/c/X3')  # the native install (bottle.NATIVE_GAME_DIR)
GAME_IMAGES = ('X3AP.exe', 'X3FL.exe', 'X3TC.exe')
WINDOWS_ROOT = Path('/mnt/c')                               # a Windows-side cwd for cmd.exe / tasklist.exe


def missing(interop=INTEROP, which=shutil.which):
    """What the native host lacks (empty when it can build and run a fixture)."""
    out = []
    if not which(COMPILER):
        out.append(COMPILER + ' not found')
    if not Path(interop).exists():
        out.append(f'WSL interop not registered ({interop})')
    return out


def available(interop=INTEROP, which=shutil.which):
    return not missing(interop, which)


def results_dir(root, fixture, create=True):
    """verification/results/native-windows/<fixture>/."""
    path = Path(root) / 'verification/results' / HOST / fixture
    if create:
        path.mkdir(parents=True, exist_ok=True)
    return path


def windows_path(path):
    return subprocess.run(['wslpath', '-w', str(path)], capture_output=True, text=True, check=True).stdout.strip()


def linux_path(path):
    return subprocess.run(['wslpath', '-u', str(path)], capture_output=True, text=True, check=True).stdout.strip()


def parse_tasklist(text, images=GAME_IMAGES):
    """The game images present in `tasklist.exe /FO CSV /NH` output."""
    wanted = {name.lower() for name in images}
    found = []
    for line in text.splitlines():
        match = re.match(r'^"([^"]+)"', line.strip())
        if match and match.group(1).lower() in wanted:
            found.append(match.group(1))
    return found


def game_running_windows(timeout=30):
    """True when a game process runs on the Windows side (ps inside WSL does not see it). Fails closed: an
    unreadable task list counts as running."""
    try:
        done = subprocess.run(['tasklist.exe', '/FO', 'CSV', '/NH'], capture_output=True, cwd=WINDOWS_ROOT, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired):
        return True
    if done.returncode:
        return True
    return bool(parse_tasklist(done.stdout.decode('utf-8', 'replace')))


def parse_ver(text):
    """'Microsoft Windows [Version 10.0.26100.4652]' -> '10.0.26100.4652' (None when absent)."""
    match = re.search(r'\[Version ([0-9.]+)\]', text)
    return match.group(1) if match else None


def windows_version(timeout=30):
    try:
        done = subprocess.run(['cmd.exe', '/c', 'ver'], capture_output=True, cwd=WINDOWS_ROOT, timeout=timeout)
    except (OSError, subprocess.TimeoutExpired) as error:
        return {'version': None, 'error': repr(error)}
    text = done.stdout.decode('utf-8', 'replace').strip()
    return {'version': parse_ver(text), 'ver': text}


def pe_file_version(data):
    """The FileVersion of a PE's VS_FIXEDFILEINFO (a.b.c.d), None without one."""
    at = data.find(struct.pack('<I', 0xFEEF04BD))
    if at < 0 or at + 16 > len(data):
        return None
    ms, ls = struct.unpack_from('<II', data, at + 8)
    return f'{ms >> 16}.{ms & 0xffff}.{ls >> 16}.{ls & 0xffff}'


def module_provenance(windows, to_linux=linux_path):
    """Windows path, size, SHA-256 and file version of a module the fixture loaded."""
    info = {'path': windows}
    try:
        data = Path(to_linux(windows)).read_bytes()
    except (OSError, subprocess.CalledProcessError) as error:
        info['error'] = repr(error)
        return info
    info.update(size=len(data), sha256=hashlib.sha256(data).hexdigest(), file_version=pe_file_version(data))
    return info


def fixture_rows(text):
    """The fixture's identity rows: ADAPTER fields, ADAPTER_DESC / ADAPTER_DRIVER text, D3D9_MODULE / D3DX_MODULE paths."""
    rows = {}
    for line in text.splitlines():
        head, _, rest = line.partition(' ')
        if head == 'ADAPTER' and 'adapter' not in rows:
            rows['adapter'] = dict(re.findall(r'(\w+)=(\S+)', rest))
        elif head in ('ADAPTER_DESC', 'ADAPTER_DRIVER', 'D3D9_MODULE', 'D3DX_MODULE') and head not in rows:
            rows[head] = rest.strip()
    return rows


def identity(text, version=None, to_linux=linux_path):
    """The record's host block from the fixture's stdout."""
    rows = fixture_rows(text)
    adapter = dict(rows.get('adapter', {}))
    adapter['description'] = rows.get('ADAPTER_DESC')
    adapter['driver'] = rows.get('ADAPTER_DRIVER')
    block = {'name': HOST, 'windows': version if version is not None else windows_version(), 'adapter': adapter,
             'interop': str(INTEROP), 'd3d9_dll': None, 'd3dx9_37_dll': None}
    if rows.get('D3D9_MODULE'):
        block['d3d9_dll'] = module_provenance(rows['D3D9_MODULE'], to_linux)
    if rows.get('D3DX_MODULE'):
        block['d3dx9_37_dll'] = module_provenance(rows['D3DX_MODULE'], to_linux)
    return block


def child_environment(extra, environ=None):
    """os.environ plus `extra`, each extra name forwarded to the Windows process through WSLENV."""
    env = dict(os.environ if environ is None else environ)
    env.update(extra)
    names = [n for n in (env.get('WSLENV') or '').split(':') if n]
    for name in extra:
        if name not in [n.split('/', 1)[0] for n in names]:
            names.append(name)
    if names:
        env['WSLENV'] = ':'.join(names)
    return env


# Variables every native fixture process gets unless its runner sets them itself. X3M_OWNERSHIP=1: since 2026-11-05 the
# proxy hooks only the ownership wrapper's device and forwards a native one unhooked (capture.cpp create_device;
# docs/verification/engine-effects.md, "Bare route refused"), so a runner that never set the variable keeps its device
# hooks here as it had them under Wine. A runner that passes X3M_OWNERSHIP (0 included) keeps its value.
NATIVE_DEFAULTS = {'X3M_OWNERSHIP': '1'}


def fixture_environment(extra):
    """The runner's variables over NATIVE_DEFAULTS."""
    return {**NATIVE_DEFAULTS, **extra}


def run_native(exe, args, workdir, timeout, env=None, log=None):
    """Runs a fixture .exe on the Windows side through interop (never the game). `args` are passed as given, so
    paths in them must already be Windows paths. `env` (forwarded through WSLENV) is applied over NATIVE_DEFAULTS. With
    `log` (a path stem under verification/probe/build/<fixture>/) stdout goes to <log>.log (CRLF folded) and stderr to
    <log>.stderr.txt. Returns (CompletedProcess, stdout text)."""
    if Path(exe).name.lower() in {name.lower() for name in GAME_IMAGES}:
        raise SystemExit(f'refusing to run a game image: {exe}')
    done = subprocess.run([str(exe), *args], capture_output=True, cwd=str(workdir),
                          env=child_environment(fixture_environment(env or {})), timeout=timeout)
    text = done.stdout.decode('utf-8', 'replace').replace('\r\n', '\n')
    if log is not None:
        log = Path(log)
        log.parent.mkdir(parents=True, exist_ok=True)
        log.with_name(log.name + '.log').write_text(text)
        log.with_name(log.name + '.stderr.txt').write_bytes(done.stderr)
    return done, text


if __name__ == '__main__':
    import json
    print(json.dumps({'host': HOST, 'missing': missing(), 'windows': windows_version(),
                      'game_running': game_running_windows()}, indent=2))
