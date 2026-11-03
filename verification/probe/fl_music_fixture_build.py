"""Builds and runs the FL music family's native fixtures (fl_media_cue.cpp, fl_music_keep.cpp, fl_music_alert.cpp) with
the project's i686 MinGW flags, natively through WSL interop (no Wine). The installed X3FL.exe is passed by its
Windows path and mapped by the fixture at run time (fl_music_fixture.h); nothing of it is copied.

usage (manual): python3 verification/probe/fl_music_fixture_build.py media_cue|music_keep|music_alert [X3FL.exe]
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_EXE = Path('/mnt/c/X3FL/X3FL.exe')
COMMON = ['-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-cast-function-type', '-mstackrealign',
          '-mincoming-stack-boundary=2', '-DX3M_EDITION_FL=1', '-msse2', '-mfpmath=sse']
SHIM = ['-include', str(ROOT / 'verification/probe/fl_music_shim.h'), '-DX3M_FL_MUSIC_SHIM']
FEATURES = {
    'media_cue': ('X3M_MEDIA_CUE_FL_FIXTURE', 'src/engine/fl/media_cue_fl.cpp', []),
    'music_keep': ('X3M_MUSIC_KEEP_FL_FIXTURE', 'src/engine/fl/music_keep_fl.cpp', ['engine_memory']),
    'music_alert': ('X3M_MUSIC_ALERT_FL_FIXTURE', 'src/engine/fl/music_alert_fl.cpp', ['engine_memory']),
}


def available():
    return bool(shutil.which('i686-w64-mingw32-g++')) and Path('/proc/sys/fs/binfmt_misc/WSLInterop').exists()


def build(feature, tmp):
    """The fixture executable built from the production sources (CMake per-file flags); None without the toolchain."""
    if not available():
        return None
    compiler = shutil.which('i686-w64-mingw32-g++')
    define, module, extra = FEATURES[feature]
    tmp = Path(tmp)
    units = [(module, ['-fno-exceptions', f'-D{define}', *SHIM]), ('src/proxy/engine_patch.cpp', []),
             ('src/engine/fl/executable_identity_fl.cpp', []), ('verification/probe/fl_music_fixture_seam.cpp', [])]
    if 'engine_memory' in extra:
        units.append(('src/proxy/engine_memory.cpp', ['-mno-sse', '-mno-mmx', '-mfpmath=387']))
    objects = []
    for source, flags in units:
        obj = tmp / (Path(source).stem + '.o')
        subprocess.run([compiler, *COMMON, *flags, '-c', str(ROOT / source), '-o', str(obj)], check=True)
        objects.append(str(obj))
    exe = tmp / f'fl_{feature}.exe'
    subprocess.run([compiler, *COMMON, f'-D{define}', '-static', str(ROOT / f'verification/probe/fl_{feature}.cpp'),
                    *objects, '-o', str(exe)], check=True)
    return exe


def windows_path(path):
    return subprocess.run(['wslpath', '-w', str(path)], capture_output=True, text=True, check=True).stdout.strip()


def run(exe, x3fl=DEFAULT_EXE, *args, timeout=300):
    return subprocess.run([str(exe), windows_path(x3fl), *args], text=True, capture_output=True, timeout=timeout)


if __name__ == '__main__':
    feature = sys.argv[1]
    x3fl = Path(sys.argv[2]) if len(sys.argv) > 2 else DEFAULT_EXE
    with tempfile.TemporaryDirectory(prefix=f'x3-fl-{feature}-') as tmp:
        exe = build(feature, tmp)
        if exe is None:
            sys.exit('needs i686-w64-mingw32-g++ and WSL interop')
        result = run(exe, x3fl)
        print(result.stdout[-6000:], result.stderr[-2000:])
        sys.exit(result.returncode)
