#!/usr/bin/env python3
"""One-command release: build the DLL and x3m-regenerate from this checkout and package the player's zip.

    python3 tools/release/release.py --out DIR [--allow-dirty] [--regenerate-dir PATH | --skip-windows] [--no-strip]
                                     [--dry-run]

Steps, in order (each one stops the release on failure):
  1. tree     refuse when `git status --porcelain` shows tracked changes (the zip's README.txt records the source
              commit, so it must describe what was built); --allow-dirty builds anyway and records dirty=true
  2. config   tools/config/generate.py --check (the shipped x3m.ini template is current)
  3. dll      fresh CMake build in DIR/build-release (MinGW i686, RelWithDebInfo); any compiler, linker or CMake
              warning fails the release; then verification/probe/check_no_x87.py must report 0 violations and the
              X3M_SOURCE_COMMIT= marker must occur once and name the commit. Then the strip: the unstripped DLL is
              kept as DIR/unstripped/d3d9.dll; `objcopy --only-keep-debug` writes DIR/d3d9.debug and
              `objcopy --strip-debug --add-gnu-debuglink=DIR/d3d9.debug` writes DIR/d3d9.dll, the DLL that ships.
              The stripped DLL must carry the same loaded sections (name, RVA, virtual size, flags and bytes of every
              non-debug section, among them .text .rdata .data .reloc .idata .edata), the same entry point, image base
              and data directories, the same export names, the marker once and no .debug_* section, and must pass
              check_no_x87.py again. --no-strip (alias --keep-debug-in-zip) ships the unstripped build instead.
  4. regen    tools/regenerate/build.py --dist DIR/regenerate: the host build with its smoke test, then the
              Windows build with its smoke test (bottle X3M-Build under Wine, run through
              verification/probe/wine_lock.py after the game guard reports no game);
              --regenerate-dir PATH ships existing binaries instead (no build, no smoke test);
              --skip-windows builds and smoke-tests the host binary only and produces no zip (the zip requires
              x3m-regenerate.exe)
  5. package  tools/release/package.py, then the zip is listed, its entries must be exactly package.py's ZIP_ENTRIES
              in order: d3d9.dll, x3m.ini, x3m-regenerate.exe, the 16 density fonts f/<Name><S*d>.abc/.tga,
              OFL-NotoSans.txt, OFL-Exo2.txt and README.txt (no macOS binary, no CrossOver voice decoder); its
              d3d9.dll and x3m-regenerate.exe are re-hashed against steps 3 and 4 and its fonts against the committed
              assets/fonts/generated/F
  6. record   DIR/release-<version>.json (schema 2: commit, dirty, toolchain, hashes of the shipped and the unstripped
              DLL and of the debug file, the strip identity check, check results, wall times)

--dry-run prints the plan and the toolchain check and builds nothing. The version is project(VERSION) in
CMakeLists.txt (tools/config/generate.py version()); the DLL logs its major.minor.
"""
import argparse
import hashlib
import struct
import json
import os
import shutil
import subprocess
import sys
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/config'))
sys.path.insert(0, str(ROOT / 'verification/probe'))
sys.path.insert(0, str(ROOT / 'tools'))
import generate  # noqa: E402
import importlib.util  # noqa: E402

TOOLCHAIN = ROOT / 'cmake/mingw-i686.cmake'
BUILD_PY = ROOT / 'tools/regenerate/build.py'
PACKAGE_PY = ROOT / 'tools/release/package.py'
GENERATE_PY = ROOT / 'tools/config/generate.py'
X87_PY = ROOT / 'verification/probe/check_no_x87.py'
WINE_LOCK_PY = ROOT / 'verification/probe/wine_lock.py'
GCC = 'i686-w64-mingw32-gcc'
OBJCOPY = 'i686-w64-mingw32-objcopy'  # binutils of the cmake/mingw-i686.cmake toolchain (i686-w64-mingw32-g++)
SCHEMA = 2

def _load_package():
    spec = importlib.util.spec_from_file_location('x3m_release_package', PACKAGE_PY)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


PACKAGE = _load_package()
ZIP_ENTRIES = list(PACKAGE.ZIP_ENTRIES)  # package.py's order: d3d9.dll, x3m.ini, x3m-regenerate.exe, f/<16>, OFL x2, README.txt
MARKER = b'X3M_SOURCE_COMMIT='  # proxy_identity.cpp; tools/manage.py reads it at install
BOTTLE_SETUP = ('The Windows x3m-regenerate.exe is built in the CrossOver bottle X3M-Build. Set it up once with '
                '`python3 tools/regenerate/build.py --windows` (creates the bottle from the win10_64 template, installs '
                'Windows Python 3.12.10 and pip installs pyinstaller numpy==2.0.2 pillow; the steps are in that '
                "script's docstring), check it with `python3 tools/regenerate/build.py --windows --dry`, or pass "
                '--regenerate-dir PATH with an existing x3m-regenerate.exe.')
SKIP_WINDOWS_NOTE = ('NOTE: --skip-windows: only the host x3m-regenerate is built and smoke-tested. The zip requires '
                     'x3m-regenerate.exe and package.py refuses without it, so this run makes NO release zip.')


class ReleaseError(Exception):
    pass


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, 'rb') as handle:
        for block in iter(lambda: handle.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def pe_image(data):
    """(header fields, sections) of a PE32 image, parsed from its bytes; ReleaseError when it is not one.

    Section names longer than eight bytes (`/NNN`, the .debug_* sections MinGW ld writes) are resolved through the
    COFF string table."""
    try:
        if data[:2] != b'MZ':
            raise ValueError('no MZ header')
        pe = struct.unpack_from('<I', data, 0x3c)[0]
        if data[pe:pe + 4] != b'PE\0\0':
            raise ValueError('no PE signature')
        count, symbols, nsymbols, optional_size = (struct.unpack_from('<H', data, pe + 6)[0],
                                                   *struct.unpack_from('<II', data, pe + 12),
                                                   struct.unpack_from('<H', data, pe + 20)[0])
        optional = pe + 24
        if struct.unpack_from('<H', data, optional)[0] != 0x10b:
            raise ValueError('not a PE32 optional header')
        entry, = struct.unpack_from('<I', data, optional + 16)
        image_base, = struct.unpack_from('<I', data, optional + 28)
        ndirs, = struct.unpack_from('<I', data, optional + 92)
        directories = [struct.unpack_from('<II', data, optional + 96 + 8 * i) for i in range(min(ndirs, 16))]
        strings = symbols + 18 * nsymbols
        sections = []
        for i in range(count):
            at = optional + optional_size + 40 * i
            raw_name = data[at:at + 8].rstrip(b'\0')
            if raw_name.startswith(b'/') and symbols:
                offset = strings + int(raw_name[1:])
                raw_name = data[offset:data.index(b'\0', offset)]
            vsize, va, raw_size, raw_ptr = struct.unpack_from('<IIII', data, at + 8)
            flags, = struct.unpack_from('<I', data, at + 36)
            sections.append({'name': raw_name.decode('ascii', 'replace'), 'va': va, 'vsize': vsize, 'raw_size': raw_size,
                             'raw_ptr': raw_ptr, 'flags': flags})
    except (ValueError, struct.error, IndexError) as error:
        raise ReleaseError(f'not a PE32 image: {error}') from error
    return {'entry': entry, 'image_base': image_base, 'directories': directories}, sections


def section_bytes(data, section):
    """The initialised bytes a section loads (its raw data up to the virtual size; file-alignment padding excluded)."""
    return data[section['raw_ptr']:section['raw_ptr'] + min(section['vsize'], section['raw_size'])]


def pe_exports(data, header, sections):
    """The export names of a PE32 image, in export-table order."""
    rva, size = header['directories'][0] if header['directories'] else (0, 0)
    if not rva:
        return []

    def offset(address):
        for section in sections:
            if section['va'] <= address < section['va'] + max(section['vsize'], section['raw_size']):
                return section['raw_ptr'] + address - section['va']
        raise ReleaseError(f'export RVA {address:#x} outside every section')
    try:
        directory = offset(rva)
        count, = struct.unpack_from('<I', data, directory + 24)
        names = offset(struct.unpack_from('<I', data, directory + 32)[0])
        result = []
        for i in range(count):
            at = offset(struct.unpack_from('<I', data, names + 4 * i)[0])
            result.append(data[at:data.index(b'\0', at)].decode('ascii'))
    except (ValueError, struct.error) as error:
        raise ReleaseError(f'unreadable export table: {error}') from error
    return result


def is_debug_section(name):
    return name.startswith('.debug_') or name == '.gnu_debuglink'


def marker_values(data):
    """The X3M_SOURCE_COMMIT= marker values in a DLL's bytes (one expected)."""
    values, at = [], data.find(MARKER)
    while at >= 0:
        end = data.find(b'\0', at)
        values.append(data[at + len(MARKER):end].decode('ascii', 'replace'))
        at = data.find(MARKER, at + 1)
    return values


def strip_identity(full, stripped):
    """Proves the stripped DLL loads the same code and data as the unstripped build; returns the summary for the
    record, ReleaseError on any difference."""
    full_header, full_sections = pe_image(full)
    header, sections = pe_image(stripped)
    loaded = [s for s in full_sections if not is_debug_section(s['name'])]
    kept = [s for s in sections if not is_debug_section(s['name'])]
    problems = []
    names = [s['name'] for s in loaded]
    for required in ('.text', '.rdata', '.data', '.reloc', '.idata', '.edata'):
        if required not in names:
            problems.append(f'{required} missing in the unstripped build')
    if [s['name'] for s in kept] != names:
        problems.append(f'loaded sections differ: {names} -> {[s["name"] for s in kept]}')
    else:
        for before, after in zip(loaded, kept):
            for field in ('va', 'vsize', 'flags'):
                if before[field] != after[field]:
                    problems.append(f'{before["name"]} {field} {before[field]:#x} -> {after[field]:#x}')
            if section_bytes(full, before) != section_bytes(stripped, after):
                problems.append(f'{before["name"]} bytes differ')
    for field in ('entry', 'image_base', 'directories'):
        if full_header[field] != header[field]:
            problems.append(f'{field} differs')
    debug_left = [s['name'] for s in sections if s['name'].startswith('.debug_')]
    if debug_left:
        problems.append(f'debug sections left: {debug_left}')
    exports = pe_exports(full, full_header, full_sections)
    if not exports or pe_exports(stripped, header, sections) != exports:
        problems.append(f'export names differ ({len(exports)} in the unstripped build)')
    markers = marker_values(stripped)
    if len(markers) != 1 or markers != marker_values(full):
        problems.append(f'X3M_SOURCE_COMMIT marker: {len(markers)} in the stripped DLL, expected the unstripped one once')
    if problems:
        raise ReleaseError('the stripped DLL is not the unstripped build without debug sections: ' + '; '.join(problems))
    return {'result': 'PASS', 'sections_compared': names, 'exports': len(exports),
            'debug_sections_removed': [s['name'] for s in full_sections if is_debug_section(s['name'])],
            'debuglink': any(s['name'] == '.gnu_debuglink' for s in sections)}


def run(cmd, *, env=None, cwd=ROOT, echo=True):
    """Run a command, echo its output live and return (returncode, output text)."""
    cmd = [str(c) for c in cmd]
    print('+ ' + ' '.join(cmd), flush=True)
    process = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                               errors='replace')
    lines = []
    for line in process.stdout:
        lines.append(line)
        if echo:
            sys.stdout.write(line)
            sys.stdout.flush()
    return process.wait(), ''.join(lines)


def first_line(cmd):
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
    except (OSError, subprocess.SubprocessError) as error:
        return None, str(error)
    text = (result.stdout or result.stderr).strip().splitlines()
    return result.returncode, text[0] if text else ''


def git(*args):
    return subprocess.run(['git', '-C', str(ROOT), *args], capture_output=True, text=True, check=True).stdout


def tracked_changes(porcelain):
    """The `git status --porcelain` lines that concern tracked files (untracked '??' and ignored '!!' excluded)."""
    return [line for line in porcelain.splitlines() if line.strip() and not line.startswith(('??', '!!'))]


def toolchain():
    """{name: version line or None} for the host tools the release needs."""
    found = {}
    for name, cmd in (('mingw_gcc', [GCC, '--version']), ('mingw_objcopy', [OBJCOPY, '--version']),
                      ('cmake', ['cmake', '--version'])):
        code, line = first_line(cmd)
        found[name] = line if code == 0 else None
    found['python'] = sys.version.split()[0] + ' (' + sys.executable + ')'
    return found


def windows_state():
    """(ready, text) for the X3M-Build bottle and its Windows Python (tools/regenerate/build.py)."""
    sys.path.insert(0, str(ROOT / 'tools/regenerate'))
    import build as regenerate_build
    if not (regenerate_build.CX_BIN / 'wine').exists():
        return False, f'CrossOver Preview wine not found at {regenerate_build.CX_BIN}'
    if not regenerate_build.BOTTLE_DIR.is_dir():
        return False, f'bottle {regenerate_build.BOTTLE} absent ({regenerate_build.BOTTLE_DIR})'
    python = regenerate_build.windows_python()
    if python is None:
        return False, f'bottle {regenerate_build.BOTTLE} present but Windows Python is not installed'
    return True, f'bottle {regenerate_build.BOTTLE} with {python.relative_to(regenerate_build.BOTTLE_DIR)}'


def game_guard_clear():
    import game_guard
    lines = game_guard.game_running()
    if lines:
        raise ReleaseError(f'the game is running ({lines[0]}); no Wine command is started')


def plan(args, out, version):
    steps = ['1 tree     git status --porcelain: tracked changes ' + ('allowed (--allow-dirty)' if args.allow_dirty
                                                                       else 'refused'),
             '2 config   tools/config/generate.py --check',
             f'3 dll      cmake configure + build in {out / "build-release"} (0 warnings), check_no_x87.py (0 violations), '
             'X3M_SOURCE_COMMIT marker once']
    if args.no_strip:
        steps.append('           no strip (--no-strip): the zip ships the unstripped RelWithDebInfo DLL')
    else:
        steps.append(f'           strip: {out / "unstripped/d3d9.dll"} kept; objcopy --only-keep-debug -> {out / "d3d9.debug"}; '
                     f'objcopy --strip-debug --add-gnu-debuglink -> {out / "d3d9.dll"}; section/export/marker identity, '
                     'check_no_x87.py again')
    if args.regenerate_dir:
        steps.append(f'4 regen    reuse {args.regenerate_dir} (no build, no smoke test)')
    else:
        steps.append(f'4 regen    build.py --dist {out / "regenerate"} (host build + smoke test)')
        if not args.skip_windows:
            steps.append(f'           X3M_FIXTURE_BOTTLE=X3 wine_lock.py build.py --windows --dist {out / "regenerate"} '
                         '(bottle X3M-Build; Windows build + smoke test under Wine)')
    if args.skip_windows:
        steps.append('5 package  SKIPPED (--skip-windows: no x3m-regenerate.exe)')
    else:
        steps.append(f'5 package  package.py -> {out / f"x3m-{version}.zip"}; entries exactly {", ".join(ZIP_ENTRIES)}; '
                     're-hash d3d9.dll, x3m-regenerate.exe and the fonts')
    steps.append(f'6 record   {out / f"release-{version}.json"}')
    return steps


class Release:
    def __init__(self, args):
        self.args = args
        self.out = Path(args.out).resolve()
        self.version = generate.version()
        self.times = {}
        self.record = {'schema': SCHEMA, 'version': self.version, 'complete': False}

    def step(self, name, function):
        print(f'\n=== {name} ===', flush=True)
        started = time.monotonic()
        try:
            return function()
        finally:
            self.times[name] = round(time.monotonic() - started, 1)
            print(f'=== {name}: {self.times[name]} s ===', flush=True)

    # 1
    def tree(self):
        commit = git('rev-parse', 'HEAD').strip()
        changes = tracked_changes(git('status', '--porcelain'))
        if changes and not self.args.allow_dirty:
            raise ReleaseError(f'{len(changes)} tracked change(s) in the tree (first: {changes[0].strip()}); the zip '
                               'records the source commit, so commit first or pass --allow-dirty')
        if changes:
            print(f'WARNING: building with {len(changes)} tracked change(s) (--allow-dirty); recorded as dirty')
        self.record.update(commit=commit, dirty=bool(changes), dirty_files=[line[3:] for line in changes])
        print(f'commit {commit}{" +dirty" if changes else ""}')

    # 2
    def config(self):
        code, _ = run([sys.executable, GENERATE_PY, '--check'])
        self.record['generate_check'] = 'PASS' if code == 0 else f'FAIL (exit {code})'
        if code:
            raise ReleaseError('tools/config/generate.py --check failed: run tools/config/generate.py and commit')

    # 3
    def dll(self):
        build = self.out / 'build-release'
        if build.exists():
            if not (build / 'CMakeCache.txt').is_file():
                raise ReleaseError(f'{build} exists and is not a CMake build directory; remove it or choose another --out')
            print(f'removing the previous {build} (a build directory is never reused)')
            shutil.rmtree(build)
        configure = ['cmake', '-S', ROOT, '-B', build, f'-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN}',
                     '-DCMAKE_BUILD_TYPE=RelWithDebInfo', f'-DPython3_EXECUTABLE={sys.executable}']
        code, configure_text = run(configure)
        if code:
            raise ReleaseError(f'cmake configure failed (exit {code})')
        code, build_text = run(['cmake', '--build', build, '-j4'])
        if code:
            raise ReleaseError(f'cmake build failed (exit {code})')
        warnings = [line.strip() for line in (configure_text + build_text).splitlines()
                    if 'warning:' in line.lower() or line.startswith('CMake Warning')]
        self.record['dll_warnings'] = len(warnings)
        if warnings:
            raise ReleaseError(f'{len(warnings)} build warning(s):\n  ' + '\n  '.join(warnings[:40]))
        dll = build / 'd3d9.dll'
        if not dll.is_file():
            raise ReleaseError(f'{dll} missing after the build')
        self.record['x87'] = self.x87_audit(dll)
        markers = marker_values(dll.read_bytes())
        commit = self.record.get('commit', '')
        if len(markers) != 1 or markers[0].removesuffix('-dirty') != commit:
            raise ReleaseError(f'X3M_SOURCE_COMMIT marker: {markers} in {dll}, expected one naming {commit}')
        self.record['source_commit_marker'] = markers[0]
        print(f'X3M_SOURCE_COMMIT={markers[0]} (once)')
        entry = {'path': str(dll), 'sha256': sha256_file(dll), 'bytes': dll.stat().st_size}
        print(f'unstripped d3d9.dll sha256 {entry["sha256"]}, {entry["bytes"]} bytes')
        if self.args.no_strip:
            self.dll_path = dll
            self.record.update(dll=entry, dll_unstripped=None, debug_file=None, strip=None)
            print('--no-strip: the zip ships the unstripped DLL')
            return
        self.strip(dll, entry)

    def x87_audit(self, dll):
        """check_no_x87.py on one DLL: its summary for the record, ReleaseError unless PASS with 0 violations."""
        code, text = run([sys.executable, X87_PY, dll], echo=False)
        try:
            summary = json.loads(text[text.index('{'):])
        except ValueError:
            summary = {'result': 'FAIL', 'error': 'unparsable output'}
        violations = len(summary.get('violations', {}))
        print(f'x87 audit {dll}: {summary.get("result")}, {violations} violation(s), '
              f'{summary.get("reachable_functions")} reachable functions')
        if code or summary.get('result') != 'PASS' or violations:
            raise ReleaseError(f'check_no_x87.py {dll}: {summary.get("error") or f"{violations} violation(s)"}')
        return {'result': summary.get('result'), 'violations': violations,
                'reachable_functions': summary.get('reachable_functions')}

    def strip(self, built, entry):
        """Keep the unstripped build in DIR/unstripped, split its DWARF into DIR/d3d9.debug and ship DIR/d3d9.dll
        stripped with a .gnu_debuglink to it; the stripped file must load the same image (strip_identity)."""
        unstripped = self.out / 'unstripped' / 'd3d9.dll'
        debug, shipped = self.out / 'd3d9.debug', self.out / 'd3d9.dll'
        for path in (unstripped, debug, shipped):
            if path.exists():
                path.unlink()
        unstripped.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(built, unstripped)
        if sha256_file(unstripped) != entry['sha256']:
            raise ReleaseError(f'{unstripped} does not match {built} after the copy')
        code, _ = run([OBJCOPY, '--only-keep-debug', unstripped, debug])
        if code or not debug.is_file():
            raise ReleaseError(f'objcopy --only-keep-debug failed (exit {code})')
        code, _ = run([OBJCOPY, '--strip-debug', f'--add-gnu-debuglink={debug}', unstripped, shipped])
        if code or not shipped.is_file():
            raise ReleaseError(f'objcopy --strip-debug failed (exit {code})')
        identity = strip_identity(unstripped.read_bytes(), shipped.read_bytes())
        print(f'strip identity: {len(identity["sections_compared"])} loaded sections identical '
              f'({" ".join(identity["sections_compared"])}), entry/image base/data directories identical, '
              f'{identity["exports"]} exports identical, marker once; removed {len(identity["debug_sections_removed"])} '
              'debug sections')
        identity['x87'] = self.x87_audit(shipped)
        self.dll_path = shipped
        self.record['strip'] = dict(identity, method=f'{OBJCOPY} --only-keep-debug; --strip-debug --add-gnu-debuglink')
        self.record['dll_unstripped'] = dict(entry, path=str(unstripped))
        self.record['debug_file'] = {'path': str(debug), 'sha256': sha256_file(debug), 'bytes': debug.stat().st_size}
        self.record['dll'] = {'path': str(shipped), 'sha256': sha256_file(shipped), 'bytes': shipped.stat().st_size}
        for name, key in (('stripped d3d9.dll', 'dll'), ('d3d9.debug', 'debug_file')):
            print(f'{name} sha256 {self.record[key]["sha256"]}, {self.record[key]["bytes"]} bytes')

    # 4
    def regenerate(self):
        if self.args.regenerate_dir:
            directory = Path(self.args.regenerate_dir).resolve()
            self.record['regenerate_source'] = 'reused'
            if not (directory / 'x3m-regenerate.exe').is_file():
                raise ReleaseError(f'x3m-regenerate.exe missing in {directory}. ' + BOTTLE_SETUP)
        else:
            directory = self.out / 'regenerate'
            if directory.exists():
                shutil.rmtree(directory)
            directory.mkdir(parents=True)
            self.record['regenerate_source'] = 'built'
            started = time.monotonic()
            code, text = run([sys.executable, BUILD_PY, '--dist', directory])
            self.times['regenerate_host'] = round(time.monotonic() - started, 1)
            if code or 'smoke test PASS' not in text:
                raise ReleaseError(f'host regenerate build or smoke test failed (exit {code})')
            if not self.args.skip_windows:
                ready, state = windows_state()
                if not ready:
                    raise ReleaseError(state + '. ' + BOTTLE_SETUP)
                game_guard_clear()
                timings = self.out / 'regenerate-windows-lock.json'
                env = dict(os.environ, X3M_FIXTURE_BOTTLE='X3')
                started = time.monotonic()
                code, text = run([sys.executable, WINE_LOCK_PY, '--holder', 'x3m release: x3m-regenerate --windows',
                                  '--timings-json', timings, sys.executable, BUILD_PY, '--windows', '--dist', directory],
                                 env=env)
                self.times['regenerate_windows'] = round(time.monotonic() - started, 1)
                try:
                    self.record['regenerate_windows_lock'] = json.loads(timings.read_text())
                    timings.unlink()
                except (OSError, ValueError):
                    pass
                if code or 'smoke test PASS' not in text:
                    raise ReleaseError(f'Windows regenerate build or smoke test failed (exit {code}). ' + BOTTLE_SETUP)
        self.regenerate_dir = directory
        self.record['regenerate'] = {path.name: {'sha256': sha256_file(path), 'bytes': path.stat().st_size}
                                     for path in sorted(directory.glob('x3m-regenerate*')) if path.is_file()
                                     and path.name in ('x3m-regenerate', 'x3m-regenerate.exe')}
        for name, entry in self.record['regenerate'].items():
            print(f'{name} sha256 {entry["sha256"]}, {entry["bytes"]} bytes')

    # 5
    def package(self):
        debug = ['--debug-file', self.record['debug_file']['path']] if self.record.get('debug_file') else []
        code, _ = run([sys.executable, PACKAGE_PY, '--dll', self.dll_path, '--out', self.out, '--regenerate-dir',
                       self.regenerate_dir, *debug])
        zip_path = self.out / f'x3m-{self.version}.zip'
        if code or not zip_path.is_file():
            raise ReleaseError(f'package.py failed (exit {code})')
        with zipfile.ZipFile(zip_path) as archive:
            if archive.testzip() is not None:
                raise ReleaseError(f'{zip_path}: CRC error')
            listing = [(info.filename, info.file_size, hashlib.sha256(archive.read(info.filename)).hexdigest())
                       for info in archive.infolist()]
        names = [name for name, _, _ in listing]
        if names != ZIP_ENTRIES:
            raise ReleaseError(f'the zip holds {names}, expected exactly {ZIP_ENTRIES}')
        hashes = {name: digest for name, _, digest in listing}
        if hashes['d3d9.dll'] != self.record['dll']['sha256']:
            raise ReleaseError('the zip\'s d3d9.dll does not match the built DLL')
        if hashes['x3m-regenerate.exe'] != self.record['regenerate'].get('x3m-regenerate.exe', {}).get('sha256'):
            raise ReleaseError(f'the zip\'s x3m-regenerate.exe does not match {self.regenerate_dir / "x3m-regenerate.exe"}')
        fonts = [name for name in PACKAGE.FONT_FILES if hashes[f'f/{name}'] != sha256_file(PACKAGE.FONTS_DIR / name)]
        if fonts:
            raise ReleaseError(f'the zip\'s fonts {fonts} do not match {PACKAGE.FONTS_DIR}')
        self.record['zip'] = {'path': str(zip_path), 'sha256': sha256_file(zip_path), 'bytes': zip_path.stat().st_size,
                              'entries': [{'name': n, 'bytes': b, 'sha256': d} for n, b, d in listing]}
        print(f'zip verified: exactly {", ".join(ZIP_ENTRIES)}; d3d9.dll and x3m-regenerate.exe match the built files, '
              f'the {len(PACKAGE.FONT_FILES)} fonts match {PACKAGE.FONTS_DIR.relative_to(ROOT)}')

    def execute(self):
        self.out.mkdir(parents=True, exist_ok=True)
        started = time.monotonic()
        self.record['toolchain'] = toolchain()
        missing = [name for name, value in self.record['toolchain'].items() if value is None]
        if missing:
            raise ReleaseError('toolchain missing: ' + ', '.join(missing))
        if self.args.skip_windows:
            print(SKIP_WINDOWS_NOTE)
        self.step('tree', self.tree)
        self.step('config', self.config)
        self.step('dll', self.dll)
        self.step('regenerate', self.regenerate)
        if self.args.skip_windows:
            self.record['zip'] = None
        else:
            self.step('package', self.package)
            self.record['complete'] = True
        self.times['total'] = round(time.monotonic() - started, 1)
        self.record['wall_seconds'] = self.times
        path = self.out / f'release-{self.version}.json'
        path.write_text(json.dumps(self.record, indent=2) + '\n')
        self.listing(path)

    def listing(self, record_path):
        record = self.record
        print(f'\n=== release {self.version} ===')
        print(f'commit             {record["commit"]}{" (dirty)" if record["dirty"] else ""}')
        print(f'toolchain          {record["toolchain"]["mingw_gcc"]}; {record["toolchain"]["cmake"]}')
        print(f'config             generate.py --check {record["generate_check"]}')
        print(f'x87                {record["x87"]["result"]}, {record["x87"]["violations"]} violations')
        print(f'd3d9.dll           {record["dll"]["sha256"]}  {record["dll"]["bytes"]} B'
              + ('  (stripped, shipped)' if record['strip'] else '  (unstripped, shipped: --no-strip)'))
        if record['strip']:
            print(f'  unstripped       {record["dll_unstripped"]["sha256"]}  {record["dll_unstripped"]["bytes"]} B  '
                  f'{record["dll_unstripped"]["path"]}')
            print(f'  d3d9.debug       {record["debug_file"]["sha256"]}  {record["debug_file"]["bytes"]} B  '
                  f'{record["debug_file"]["path"]}')
            print(f'  strip identity   {record["strip"]["result"]}: {len(record["strip"]["sections_compared"])} sections, '
                  f'{record["strip"]["exports"]} exports; x87 on the stripped DLL {record["strip"]["x87"]["result"]}')
        for name, entry in record['regenerate'].items():
            shipped = '' if name in ZIP_ENTRIES else ', smoke test only, not shipped'
            print(f'{name:18s} {entry["sha256"]}  {entry["bytes"]} B  ({record["regenerate_source"]}{shipped})')
        if record['zip']:
            print(f'zip                {record["zip"]["path"]}')
            print(f'                   {record["zip"]["sha256"]}  {record["zip"]["bytes"]} B')
            for entry in record['zip']['entries']:
                print(f'  {entry["name"]:22s} {entry["bytes"]:>10d}  {entry["sha256"][:16]}')
        else:
            print('zip                none: ' + SKIP_WINDOWS_NOTE)
        print('wall               ' + ', '.join(f'{name} {seconds} s' for name, seconds in record['wall_seconds'].items()))
        print(f'record             {record_path}')


def dry_run(args):
    out = Path(args.out).resolve()
    version = generate.version()
    print(f'release {version} into {out} (dry run: nothing is built)')
    for line in plan(args, out, version):
        print('  ' + line)
    tools = toolchain()
    print('toolchain:')
    for name, value in tools.items():
        print(f'  {name:10s} {value or "MISSING"}')
    ready = all(tools.values())
    if not args.regenerate_dir and not args.skip_windows:
        windows_ready, state = windows_state()
        print(f'  windows    {state}')
        if not windows_ready:
            print('  ' + BOTTLE_SETUP)
        ready = ready and windows_ready
    try:
        changes = tracked_changes(git('status', '--porcelain'))
    except (OSError, subprocess.CalledProcessError):
        changes = ['?? git unavailable']
    print(f'tree: {len(changes)} tracked change(s)' + ('' if not changes or args.allow_dirty else ' -> step 1 would refuse'))
    if args.skip_windows:
        print(SKIP_WINDOWS_NOTE)
    return 0 if ready else 1


def parse(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0],
                                     epilog=__doc__.split('\n\n', 1)[1], formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', required=True, help='output directory (build-release/, regenerate/, the zip, the record)')
    parser.add_argument('--allow-dirty', action='store_true',
                        help='build with tracked changes in the tree (the zip still names HEAD; recorded as dirty)')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--regenerate-dir', help='ship the x3m-regenerate binaries in this directory instead of building them')
    group.add_argument('--skip-windows', action='store_true',
                       help='host regenerate build and smoke test only; makes no zip (x3m-regenerate.exe is required)')
    parser.add_argument('--no-strip', '--keep-debug-in-zip', dest='no_strip', action='store_true',
                        help='ship the unstripped RelWithDebInfo DLL (no DIR/d3d9.debug, no strip identity check)')
    parser.add_argument('--dry-run', action='store_true', help='print the plan and the toolchain check; build nothing')
    return parser.parse_args(argv)


def main(argv=None):
    args = parse(argv)
    if args.dry_run:
        return dry_run(args)
    release = Release(args)
    try:
        release.execute()
    except ReleaseError as error:
        print(f'\nrelease.py: FAILED: {error}', file=sys.stderr)
        return 1
    if args.skip_windows:
        print('\n' + SKIP_WINDOWS_NOTE)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
