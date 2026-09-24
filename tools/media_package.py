"""Offline local media import and recoverable app-local deployment (stdlib only).

Delivery digests bind these local artifacts; they are not renderer API admission.
No codec registration, remux, process launch, or original media writes occur here.
"""
from contextlib import contextmanager
import copy
import csv
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
import uuid
import xml.etree.ElementTree as ET

MEDIA = 'x3-modern-media'
INSTALL = 'x3-modern-install.json'
JOURNAL = 'x3-modern-transaction.json'
LOCK = 'x3-modern-installer.lock'
PROFILE = 'lav081-strict-mpeg1-rgb32-v1'
CLASSES = {'source': '{B98D13E7-55DB-4385-A33D-09FD1BA26338}',
           'video': '{EE30215D-164F-4A92-A4EB-9D4C13390F9F}'}
MODULES = {'LAVSplitter.ax': ('source', 'official'), 'LAVVideo.ax': ('video', 'official'),
           'avcodec-lav-62.dll': ('dependency', 'strict'), 'avformat-lav-62.dll': ('dependency', 'strict'),
           'avutil-lav-60.dll': ('dependency', 'strict'), 'swresample-lav-6.dll': ('dependency', 'strict'),
           'avfilter-lav-11.dll': ('dependency', 'official'), 'swscale-lav-9.dll': ('dependency', 'official'),
           'libbluray.dll': ('dependency', 'official')}
MANIFESTS = ('provider.manifest', 'LAVFilters.Dependencies.manifest')
SYSTEM = set('advapi32.dll bcrypt.dll comctl32.dll crypt32.dll d3d9.dll kernel32.dll msvcrt.dll ncrypt.dll ole32.dll oleaut32.dll shell32.dll shlwapi.dll user32.dll version.dll ws2_32.dll'.split())
# This command imports the already accepted record, not arbitrary new qualification.
ACCEPTED_GRAPH = 'cadf5fbf4cf41bae21282c06d97af26418fc3b38b331f6b32f16cfdcc55be00d'
ACCEPTED_DERIVED = '5420af9747904dde2c1e5300a898252dae9fcefa970f710606c55ed6e8033608'


class PackageError(ValueError):
    pass


def require(ok, message):
    if not ok:
        raise PackageError(message)


def sha256(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def encoded(value):
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + '\n').encode('utf-8')


def read_json(path):
    require(path.stat().st_size <= 1024 * 1024, f'oversize record: {path}')
    def unique(pairs):
        result = {}
        for k, v in pairs:
            require(k not in result, f'duplicate JSON key: {k}')
            result[k] = v
        return result
    return json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique)


def relative(value):
    require(isinstance(value, str) and value and '\\' not in value, 'invalid relative path')
    parts = value.split('/')
    for part in parts:
        require(part not in ('', '.', '..') and not re.search(r'[\x00-\x1f<>:"|?*]', part)
                and not part.endswith(('.', ' ')) and
                not re.fullmatch(r'(?i)(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?', part),
                f'unsafe path: {value}')
    return parts


def safe(root, value):
    """Reject Windows aliases, symlinks/reparse points and existing case aliases."""
    parts = relative(value)
    current = Path(root)
    for part in [''] + parts:
        if part:
            if current.exists():
                matches = [p.name for p in current.iterdir() if p.name.casefold() == part.casefold()]
                require(not matches or matches == [part], f'case collision: {current / part}')
            current = current / part
        if current.exists() or current.is_symlink():
            info = current.lstat()
            require(not stat.S_ISLNK(info.st_mode) and not getattr(info, 'st_file_attributes', 0) & 0x400,
                    f'link/reparse path: {current}')
    return current


def identity(path):
    require(path.is_file(), f'missing regular file: {path}')
    return {'bytes': path.stat().st_size, 'sha256': sha256(path)}


def verify(path, row):
    require(type(row.get('bytes')) is int and row['bytes'] >= 0 and
            re.fullmatch('[0-9a-f]{64}', str(row.get('sha256', ''))), 'invalid file identity')
    require(identity(path) == {k: row[k] for k in ('bytes', 'sha256')}, f'changed file: {path}')


def tree_files(root):
    result = set()
    def visit(directory):
        folded = set()
        for p in directory.iterdir():
            require(p.name.casefold() not in folded, f'case collision: {p}')
            folded.add(p.name.casefold())
            rel = p.relative_to(root).as_posix()
            safe(root, rel)
            if p.is_dir():
                visit(p)
            else:
                require(p.is_file(), f'non-regular file: {p}')
                result.add(rel)
    visit(root)
    return result


def pe_info(path):
    """Bounded PE32 import/export inspection; never invokes a binary or loader."""
    data = path.read_bytes()
    def unpack(fmt, off):
        try:
            return struct.unpack_from(fmt, data, off)
        except struct.error as e:
            raise PackageError(f'truncated PE: {path}') from e
    require(data[:2] == b'MZ', f'not PE: {path}')
    pe, = unpack('<I', 0x3c)
    require(data[pe:pe + 4] == b'PE\0\0', f'bad PE signature: {path}')
    machine, count = unpack('<HH', pe + 4)
    size, = unpack('<H', pe + 20)
    opt = pe + 24
    require(machine == 0x14c and unpack('<H', opt)[0] == 0x10b and size >= 112 and count <= 96,
            f'not x86 PE32: {path}')
    sections = [unpack('<IIII', opt + size + 40 * n + 8) for n in range(count)]
    def offset(rva):
        for vs, va, rs, raw in sections:
            if va <= rva < va + rs:
                at = raw + rva - va
                require(at < len(data), 'PE RVA past file')
                return at
        raise PackageError(f'PE RVA outside file sections: {path}')
    def string(rva, skip=0):
        at = offset(rva) + skip
        end = data.find(b'\0', at, at + 4096)
        require(end >= at, 'unterminated PE name')
        return data[at:end].decode('ascii')
    imports = {}
    rva, length = unpack('<II', opt + 104)
    if rva:
        at = offset(rva)
        for n in range(min(length // 20 + 1, 4096)):
            orig, stamp, chain, name, thunk = unpack('<IIIII', at + n * 20)
            if not any((orig, stamp, chain, name, thunk)):
                break
            dll = string(name).lower()
            require(dll not in imports, 'duplicate PE import descriptor')
            entries = []
            at_thunk = offset(orig or thunk)
            for i in range(65536):
                value, = unpack('<I', at_thunk + 4 * i)
                if not value:
                    break
                entries.append('#' + str(value & 0xffff) if value & 0x80000000 else string(value, 2))
            else:
                raise PackageError('unterminated PE import thunk')
            imports[dll] = entries
        else:
            raise PackageError('unterminated PE imports')
    exports = set()
    er, es = unpack('<II', opt + 96)
    if er:
        fields = unpack('<IIHHIIIIIII', offset(er))
        base, nf, nn, funcs, names, ords = fields[5:]
        require(nf <= 100000 and nn <= nf, 'bad PE exports')
        for n in range(nf):
            if unpack('<I', offset(funcs) + n * 4)[0]:
                exports.add('#' + str(base + n))
        for n in range(nn):
            ordinal, = unpack('<H', offset(ords) + n * 2)
            require(ordinal < nf, 'bad export ordinal')
            exports.add(string(unpack('<I', offset(names) + n * 4)[0]))
    return imports, exports


def validate_provider(directory):
    modules = {name.lower(): pe_info(safe(directory, name)) for name in MODULES}
    for name, (imports, exports) in modules.items():
        for dependency, symbols in imports.items():
            require(dependency in modules or dependency in SYSTEM, f'unknown import {name}: {dependency}')
            if dependency in modules:
                require(set(symbols) <= modules[dependency][1], f'missing import/export: {name} -> {dependency}')
    ns = {'a': 'urn:schemas-microsoft-com:asm.v1'}
    for manifest in MANIFESTS:
        try:
            root = ET.parse(safe(directory, manifest)).getroot()
        except ET.ParseError as error:
            raise PackageError('invalid provider XML') from error
        rows = root.findall('a:file', ns)
        names = [r.get('name') for r in rows]
        expected = set(MODULES) if manifest == MANIFESTS[0] else set(MODULES) - {'LAVSplitter.ax', 'LAVVideo.ax'}
        require(len(names) == len(expected) and set(names) == expected, 'manifest module mismatch')
        if manifest == MANIFESTS[0]:
            assembly = root.find('a:assemblyIdentity', ns)
            require(assembly is not None, 'missing manifest assemblyIdentity')
            require(assembly.get('processorArchitecture') == 'x86', 'manifest architecture')
            classes = [(r.get('name'), c.get('clsid'), c.get('threadingModel'))
                       for r in rows for c in r.findall('a:comClass', ns)]
            require(set(classes) == {('LAVSplitter.ax', CLASSES['source'], 'Both'),
                                     ('LAVVideo.ax', CLASSES['video'], 'Both')} and len(classes) == 2,
                    'manifest COM class mismatch')


def validate_source(source, game, base, layout):
    require(source['id'] == 2 and source['effective_flags'] == 8 and source['codec'] == 'mpeg1video'
            and source['timeline'] == 'generated_timestamps' and source['original_relative'] == 'mov/00002.dat',
            'unsupported source profile')
    prefix = MEDIA + '/' if layout == 'installed' else ''
    require(source['asset'] == prefix + 'sources/' + source['original_sha256'] + '/00002.mkv', 'source asset path')
    verify(safe(game, source['original_relative']), {'bytes': source['original_bytes'], 'sha256': source['original_sha256']})
    verify(safe(base, source['asset']), {'bytes': source['asset_bytes'], 'sha256': source['asset_sha256']})


def validate_package(record, game, *, installed=False):
    p = read_json(record)
    layout = 'installed' if installed else 'staged'
    require(p['schema'] == 1 and p['kind'] == 'x3-owned-media-package' and p['layout'] == layout
            and p['path_base'] == ('media_root' if installed else 'package_root')
            and p['scope'] == 'local_qualification' and p['architecture'] == 'x86'
            and p['profile'] == PROFILE and p['distribution_qualified'] is False, 'unsupported package')
    require(len(relative(p['package_id'])) == 1, 'bad package ID')
    base = game / MEDIA if installed else record.parent
    directory = 'providers/' + p['package_id'] if installed else 'provider'
    provider = p['provider']
    require(provider['directory'] == directory and provider['manifest'] == directory + '/provider.manifest'
            and provider['source_clsid'] == CLASSES['source'] and provider['video_clsid'] == CLASSES['video'], 'bad provider selection')
    files = provider['files']
    require(set(files) == set(MODULES) | set(MANIFESTS) | {'notices/COPYING', 'notices/README.md', 'notices/FFmpeg-LICENSE.md'}, 'unknown/missing provider files')
    for name, row in files.items():
        require(row['path'] == directory + '/' + name, 'bad provider file path')
        role, origin = MODULES.get(name, ('manifest', 'official') if name in MANIFESTS else ('notice', 'notice'))
        require((row['role'], row['origin']) == (role, origin), 'bad module role/origin')
        verify(safe(base, row['path']), row)
    expected = set(files) | ({'package.json'} if installed else set())
    require(tree_files(safe(base, directory)) == expected, 'unknown provider runtime files')
    validate_provider(safe(base, directory))
    require(len(p['sources']) == 1, 'unsupported source count')
    validate_source(p['sources'][0], game, game if installed else base, layout)
    if not installed:
        require(tree_files(base) == {'package.json'} | {r['path'] for r in files.values()} | {s['asset'] for s in p['sources']}, 'unknown staged files')
    return p


def copy_verified(source, destination, row):
    destination.parent.mkdir(parents=True, exist_ok=True)
    require(not destination.exists(), f'copy collision: {destination}')
    shutil.copyfile(source, destination)
    verify(destination, row)


def prepare(graph_record, derived_record, game, output):
    require(sha256(graph_record) == ACCEPTED_GRAPH and sha256(derived_record) == ACCEPTED_DERIVED,
            'not the accepted local import records; new bytes require qualification')
    graph, derived = read_json(graph_record), read_json(derived_record)
    require(graph['architecture'] == 'x86' and set(graph['binaries']) == set(MODULES), 'graph module set')
    require(derived['payload']['original_sha256'] == derived['payload']['derived_sha256'] == derived['original']['sha256']
            and derived['payload']['bytes'] == derived['original']['bytes'], 'payload evidence mismatch')
    require(derived['codec'] == 'mpeg1video' and derived['timeline_semantics'] == 'generated_timestamps', 'timeline profile')
    for row in derived['decoded_ordinal_equivalence']:
        require(row['original']['sha256'] == row['derived']['sha256'], 'ordinal evidence mismatch')
    source = {'id': 2, 'effective_flags': 8, 'original_relative': 'mov/00002.dat',
              'original_sha256': derived['original']['sha256'], 'original_bytes': derived['original']['bytes'],
              'asset': 'sources/' + derived['original']['sha256'] + '/00002.mkv',
              'asset_sha256': derived['derived']['sha256'], 'asset_bytes': derived['derived']['bytes'],
              'codec': 'mpeg1video', 'timeline': 'generated_timestamps', 'derivation_record_sha256': sha256(derived_record)}
    verify(safe(game, source['original_relative']), derived['original'])
    verify(Path(derived['derived']['path']), derived['derived'])
    inputs = {}
    for name in ('official', 'strict'):
        row = graph['inputs'][name]
        path = Path(row['record_path'])
        require(sha256(path) == row['record_sha256'], 'changed provider provenance')
        inputs[name] = read_json(path)
    strict, official = inputs['strict'], inputs['official']
    for row in strict['records'].values():
        require(sha256(Path(row['path'])) == row['sha256'], 'changed strict build record')
    config = Path(strict['private_prefix']) / 'build/config.h'
    text = config.read_text()
    require(all(re.search(r'^#define ' + key + r' ' + value + r'$', text, re.M)
                for key, value in [('CONFIG_GPL', '1'), ('CONFIG_VERSION3', '1'), ('CONFIG_NONFREE', '0')]), 'strict license configuration')
    require(not output.exists(), 'output must be fresh')
    output.parent.mkdir(parents=True, exist_ok=True)
    require(shutil.disk_usage(output.parent).free > sum(r['bytes'] for r in graph['files'].values()) + source['asset_bytes'] + 1048576, 'insufficient staging space')
    stage = Path(tempfile.mkdtemp(prefix='.media-prepare-', dir=output.parent))
    try:
        files = {}
        for name in list(MODULES) + list(MANIFESTS) + ['COPYING', 'README.md']:
            original = safe(Path(graph['private_root']), name)
            row = graph['files'][name]
            verify(original, row)
            target = name if name in MODULES or name in MANIFESTS else 'notices/' + name
            role, origin = MODULES.get(name, ('manifest', 'official') if name in MANIFESTS else ('notice', 'notice'))
            files[target] = dict(path='provider/' + target, bytes=row['bytes'], sha256=row['sha256'], role=role, origin=origin)
            copy_verified(original, stage / files[target]['path'], row)
        license_path = Path(strict['source']['path']) / 'LICENSE.md'
        files['notices/FFmpeg-LICENSE.md'] = dict(path='provider/notices/FFmpeg-LICENSE.md', **identity(license_path), role='notice', origin='notice')
        copy_verified(license_path, stage / files['notices/FFmpeg-LICENSE.md']['path'], files['notices/FFmpeg-LICENSE.md'])
        copy_verified(Path(derived['derived']['path']), stage / source['asset'], derived['derived'])
        p = {'schema': 1, 'kind': 'x3-owned-media-package', 'layout': 'staged', 'path_base': 'package_root',
             'scope': 'local_qualification', 'package_id': 'lav081-strict-' + sha256(graph_record)[:16],
             'architecture': 'x86', 'profile': PROFILE, 'distribution_qualified': False,
             'provider': {'directory': 'provider', 'manifest': 'provider/provider.manifest',
                          'source_clsid': CLASSES['source'], 'video_clsid': CLASSES['video'], 'files': files},
             'sources': [source], 'provenance': {'graph_record_sha256': sha256(graph_record),
             'derived_record_sha256': sha256(derived_record), 'official_archive_sha256': official['archive_sha256'],
             'official_source_commit': official['source_commit'], 'strict_source_commit': strict['source']['commit'],
             'strict_patch_sha256': strict['patch']['patch_sha256'],
             'strict_records': {k: v['sha256'] for k, v in strict['records'].items()},
             'configuration_sha256': sha256(config), 'license': 'GPL enabled; version3 enabled; nonfree disabled',
             'public_source_bundle_complete': False,
             'accepted_scope': ['original31', 'strict31', 'integer-boundary', 'EOF'],
             'preparation': 'import accepted bytes; no rebuild/remux', 'remux_version': derived['preparation']['ffmpeg_version']}}
        (stage / 'package.json').write_bytes(encoded(p))
        validate_package(stage / 'package.json', game)
        os.rename(stage, output)
    except BaseException:
        shutil.rmtree(stage)
        raise
    return output / 'package.json'


def assert_game_closed():
    """Conservative cross-platform process check; enumeration failure is refusal.

    Native Windows matches the image name through tasklist. Elsewhere the
    shared detector verification/probe/game_guard.py (also used by wine_lock.py
    and the LOD overlay installer) matches the command token only: a process
    whose executable is X3AP.exe (Wine shows the game as C:\\X3\\X3AP.exe) or a
    wine loader whose program argument is X3AP.exe. A shell, python or grep
    whose arguments merely contain the text is not the game."""
    try:
        if os.name == 'nt':
            result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq X3AP.exe', '/FO', 'CSV', '/NH'],
                                    capture_output=True, text=True, check=True)
            rows = list(csv.reader(io.StringIO(result.stdout)))
            require(not any(row and row[0].casefold() == 'x3ap.exe' for row in rows), 'X3AP.exe is running')
        else:
            probe = str(Path(__file__).resolve().parents[1] / 'verification' / 'probe')
            if probe not in sys.path:
                sys.path.insert(0, probe)
            from game_guard import game_running
            try:
                lines = game_running()
            except RuntimeError as error:
                raise PackageError('process enumeration failed: ' + str(error)) from error
            require(not lines, 'X3AP.exe is running')
    except (OSError, ImportError, subprocess.CalledProcessError) as error:
        raise PackageError('cannot establish game is closed') from error


@contextmanager
def installer_lock(game, *, check_closed=True):
    path = safe(game, LOCK)
    with path.open('a+b') as stream:
        try:
            if os.name == 'nt':
                import msvcrt
                stream.seek(0)
                if not stream.read(1):
                    stream.write(b'0'); stream.flush()
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(stream, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise PackageError('another installer or launcher is active') from error
        try:
            if check_closed:
                assert_game_closed()
            yield
        finally:
            if os.name == 'nt':
                stream.seek(0)
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream, fcntl.LOCK_UN)


def atomic_bytes(path, value):
    fd, temporary = tempfile.mkstemp(prefix='.x3-modern-', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(value); stream.flush(); os.fsync(stream.fileno())
        os.replace(temporary, path)
        # Windows has no portable directory fsync; replace still uses its native API.
        if os.name != 'nt':
            fd = os.open(path.parent, os.O_RDONLY)
            try:
                os.fsync(fd)
            finally:
                os.close(fd)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def atomic_copy(source, destination, row):
    fd, name = tempfile.mkstemp(prefix='.x3-modern-', dir=destination.parent)
    os.close(fd)
    temp = Path(name)
    try:
        shutil.copyfile(source, temp)
        verify(temp, row)
        with temp.open('rb') as stream:
            os.fsync(stream.fileno())
        os.replace(temp, destination)
    finally:
        temp.unlink(missing_ok=True)


def no_journal(game):
    require(not safe(game, JOURNAL).exists(), 'unresolved transaction; run manage.py recover')


def selection_files(game, manifest, *, full=True):
    """Validate selection; return exact owned file identities for safe removal."""
    media = manifest.get('media')
    if media is None:
        return {}
    require(manifest.get('schema') == 2, 'media selection requires schema2')
    pid = media['package_id']
    require(len(relative(pid)) == 1, 'invalid selected package ID')
    path = MEDIA + '/providers/' + pid + '/package.json'
    require(media['package_record_relative'] == path, 'invalid package selection path')
    record = safe(game, path)
    require(sha256(record) == media['package_record_sha256'], 'changed package record')
    p = validate_package(record, game, installed=True) if full else read_json(record)
    require(p['package_id'] == pid and p['layout'] == 'installed' and p['path_base'] == 'media_root', 'selected package mismatch')
    result = {path: identity(record)}
    for row in p['provider']['files'].values():
        require(row['path'].startswith('providers/' + pid + '/'), 'provider path escape')
        result[MEDIA + '/' + row['path']] = {k: row[k] for k in ('bytes', 'sha256')}
    require(len(media['sources']) == len(p['sources']) == 1, 'invalid source selections')
    for row, source in zip(media['sources'], p['sources']):
        sp = MEDIA + '/sources/' + source['original_sha256'] + '/source.json'
        require(row['source_record_relative'] == sp and row['id'] == source['id'] == 2
                and row['effective_flags'] == source['effective_flags'] == 8, 'invalid source selection')
        record = safe(game, sp)
        require(sha256(record) == row['source_record_sha256'], 'changed source record')
        expected = dict(source, schema=1, kind='x3-owned-media-source', layout='installed', path_base='game_root')
        require(read_json(record) == expected, 'source record mismatch')
        require(source['asset'] == MEDIA + '/sources/' + source['original_sha256'] + '/00002.mkv', 'asset path escape')
        result[sp] = identity(record)
        result[source['asset']] = {'bytes': source['asset_bytes'], 'sha256': source['asset_sha256']}
    return result


def current(game, *, full=True):
    record, dll = safe(game, INSTALL), safe(game, 'd3d9.dll')
    if not record.exists():
        require(not dll.exists(), 'unowned d3d9.dll')
        return None
    manifest = read_json(record)
    require(manifest.get('project') == 'x3-modern-renderer', 'unowned install manifest')
    require(dll.is_file() and sha256(dll) == manifest['sha256'], 'installed DLL missing or changed')
    selection_files(game, manifest, full=full)
    return manifest


def snapshot_paths(game, previous):
    directory = previous['directory']
    require(re.fullmatch(re.escape(MEDIA) + r'/rollback/[0-9a-f]{32}', directory), 'invalid rollback path')
    root = safe(game, directory)
    require(tree_files(root) == {'d3d9.dll', 'install.json'}, 'unknown rollback files')
    record, dll = safe(root, 'install.json'), safe(root, 'd3d9.dll')
    require(sha256(record) == previous['manifest_sha256'] and sha256(dll) == previous['dll_sha256'], 'changed rollback pair')
    manifest = read_json(record)
    require(manifest.get('project') == 'x3-modern-renderer' and manifest['sha256'] == previous['dll_sha256'], 'mismatched rollback pair')
    return root, manifest


def publish_media(game, staged):
    p = validate_package(staged, game)
    require(shutil.disk_usage(game).free > sum(r['bytes'] for r in p['provider']['files'].values()) +
            sum(s['asset_bytes'] for s in p['sources'] if not safe(game, MEDIA + '/' + s['asset']).exists()) + 1048576,
            'insufficient installation staging space')
    installed = copy.deepcopy(p)
    installed.update(layout='installed', path_base='media_root')
    directory = 'providers/' + p['package_id']
    installed['provider']['directory'] = directory
    installed['provider']['manifest'] = directory + '/provider.manifest'
    for name, row in installed['provider']['files'].items():
        row['path'] = directory + '/' + name
    for s in installed['sources']:
        s['asset'] = MEDIA + '/' + s['asset']
    destination = safe(game, MEDIA + '/' + directory)
    if destination.exists():
        require(read_json(safe(destination, 'package.json')) == installed, 'same-ID package collision')
    else:
        parent = safe(game, MEDIA + '/providers')
        parent.mkdir(parents=True, exist_ok=True)
        temp = Path(tempfile.mkdtemp(prefix='.stage-', dir=parent))
        try:
            for name, row in p['provider']['files'].items():
                copy_verified(safe(staged.parent, row['path']), temp / name, row)
            (temp / 'package.json').write_bytes(encoded(installed))
            # immutable final directory; never merge or replace an existing cohort
            os.rename(temp, destination)
        finally:
            if temp.exists():
                shutil.rmtree(temp)
    selected = []
    for original, source in zip(p['sources'], installed['sources']):
        record = dict(source, schema=1, kind='x3-owned-media-source', layout='installed', path_base='game_root')
        asset = safe(game, source['asset'])
        target = asset.parent
        if target.exists():
            require(tree_files(target) == {'00002.mkv', 'source.json'} and read_json(safe(target, 'source.json')) == record,
                    'same-ID source cache collision')
            verify(asset, {'bytes': source['asset_bytes'], 'sha256': source['asset_sha256']})
        else:
            parent = safe(game, MEDIA + '/sources')
            parent.mkdir(parents=True, exist_ok=True)
            temp = Path(tempfile.mkdtemp(prefix='.stage-', dir=parent))
            try:
                copy_verified(safe(staged.parent, original['asset']), temp / '00002.mkv',
                              {'bytes': source['asset_bytes'], 'sha256': source['asset_sha256']})
                (temp / 'source.json').write_bytes(encoded(record))
                os.rename(temp, target)
            finally:
                if temp.exists():
                    shutil.rmtree(temp)
        selected.append({'id': source['id'], 'effective_flags': source['effective_flags'],
                         'source_record_relative': (target / 'source.json').relative_to(game).as_posix(),
                         'source_record_sha256': sha256(target / 'source.json')})
    validate_package(destination / 'package.json', game, installed=True)
    return {'package_id': p['package_id'], 'package_record_relative': (destination / 'package.json').relative_to(game).as_posix(),
            'package_record_sha256': sha256(destination / 'package.json'), 'sources': selected}


def remove_exact(game, files):
    retained = []
    for rel, row in files.items():
        try:
            path = safe(game, rel)
            if not path.exists():
                continue
            verify(path, row)
            path.unlink()
            parent = path.parent
            while parent != game:
                try:
                    parent.rmdir()
                except OSError:
                    break
                parent = parent.parent
        except (OSError, ValueError):
            retained.append(rel)
    return retained


def transaction(game, old, new, source, *, fault=lambda phase: None):
    """Journal has a verified old snapshot and a new snapshot; selection commits last.

    A fault leaves the journal in place. recover always restores the old pair,
    even when the last replace succeeded, making every interruption unambiguous.
    """
    no_journal(game)
    previous = None
    if old:
        directory = MEDIA + '/rollback/' + uuid.uuid4().hex
        root = safe(game, directory)
        root.mkdir(parents=True)
        copy_verified(game / 'd3d9.dll', root / 'd3d9.dll', identity(game / 'd3d9.dll'))
        # Retain the exact old manifest (including its previous pointer) for recovery.
        copy_verified(game / INSTALL, root / 'install.json', identity(game / INSTALL))
        previous = {'directory': directory, 'manifest_sha256': sha256(root / 'install.json'), 'dll_sha256': old['sha256']}
    if new is not None:
        new = copy.deepcopy(new)
        new.pop('previous', None)
        if previous:
            new['previous'] = previous
    # Save the desired pair as well: recovery accepts only old/new known identities.
    directory = MEDIA + '/transactions/' + uuid.uuid4().hex
    root = safe(game, directory)
    root.mkdir(parents=True)
    if new:
        copy_verified(source, root / 'd3d9.dll', {'bytes': source.stat().st_size, 'sha256': new['sha256']})
        (root / 'install.json').write_bytes(encoded(new))
    journal = {'schema': 1, 'phase': 'prepared', 'old': previous, 'new_directory': directory,
               'new_manifest_sha256': sha256(root / 'install.json') if new else None,
               'new_dll_sha256': new['sha256'] if new else None}
    atomic_bytes(safe(game, JOURNAL), encoded(journal))
    fault('prepared')
    if new:
        atomic_copy(root / 'd3d9.dll', safe(game, 'd3d9.dll'), identity(root / 'd3d9.dll'))
    else:
        safe(game, 'd3d9.dll').unlink()
    journal['phase'] = 'dll'; atomic_bytes(game / JOURNAL, encoded(journal)); fault('dll')
    if new:
        atomic_bytes(safe(game, INSTALL), encoded(new))
    else:
        safe(game, INSTALL).unlink()
    journal['phase'] = 'manifest'; atomic_bytes(game / JOURNAL, encoded(journal)); fault('manifest')
    if new:
        current(game)
    else:
        require(not (game / INSTALL).exists() and not (game / 'd3d9.dll').exists(), 'uninstall incomplete')
    safe(game, JOURNAL).unlink()
    # Cleanup only exact known snapshot files; immutable media is retained for rollback.
    remove_exact(game, {(directory + '/' + name): identity(root / name)
                        for name in ('d3d9.dll', 'install.json') if (root / name).exists()})
    if old and old.get('previous'):
        previous_root, _ = snapshot_paths(game, old['previous'])
        remove_exact(game, {str((previous_root / n).relative_to(game)): identity(previous_root / n)
                            for n in ('d3d9.dll', 'install.json')})
    if not new and previous:
        previous_root, _ = snapshot_paths(game, previous)
        remove_exact(game, {str((previous_root / n).relative_to(game)): identity(previous_root / n)
                            for n in ('d3d9.dll', 'install.json')})
    return new


def install(game, source, provenance, package=None, *, retire_media=False, fault=lambda phase: None):
    # Normal renderer installs retire the active playback selection. Keep its
    # immutable payload and exact old snapshot for the existing rollback path.
    with installer_lock(game):
        no_journal(game)
        old = current(game)
        if old and old.get('previous'):
            snapshot_paths(game, old['previous'])
        exe = identity(safe(game, 'X3AP.exe'))
        new = dict(provenance, schema=2, project='x3-modern-renderer', sha256=sha256(source))
        require(not (package and retire_media), 'cannot deploy and retire media together')
        if package:
            new['media'] = publish_media(game, package)
        elif old and 'media' in old and not retire_media:
            # Updating just the proxy preserves an existing managed media selection.
            new['media'] = old['media']
        result = transaction(game, old, new, source, fault=fault)
        verify(safe(game, 'X3AP.exe'), exe)
        return result


def rollback(game, *, fault=lambda phase: None):
    with installer_lock(game):
        no_journal(game)
        old = current(game)
        require(old and old.get('previous'), 'no previous installation')
        root, new = snapshot_paths(game, old['previous'])
        selection_files(game, new)
        return transaction(game, old, new, root / 'd3d9.dll', fault=fault)


def recover(game):
    with installer_lock(game):
        journal = read_json(safe(game, JOURNAL))
        require(journal['schema'] == 1 and journal['phase'] in ('prepared', 'dll', 'manifest'), 'invalid transaction journal')
        directory = journal['new_directory']
        require(re.fullmatch(re.escape(MEDIA) + r'/transactions/[0-9a-f]{32}', directory), 'invalid transaction directory')
        root = safe(game, directory)
        old_root = None
        if journal['old']:
            old_root, old = snapshot_paths(game, journal['old'])
            selection_files(game, old, full=False)
        expected_files = {'d3d9.dll', 'install.json'} if journal['new_dll_sha256'] else set()
        require(tree_files(root) == expected_files, 'unknown transaction files')
        if expected_files:
            require(sha256(safe(root, 'd3d9.dll')) == journal['new_dll_sha256'] and
                    sha256(safe(root, 'install.json')) == journal['new_manifest_sha256'], 'changed transaction snapshot')
        for name, old_key, new_key in [('d3d9.dll', 'dll_sha256', 'new_dll_sha256'),
                                       (INSTALL, 'manifest_sha256', 'new_manifest_sha256')]:
            path = safe(game, name)
            allowed = {journal[new_key]}
            if journal['old']:
                allowed.add(journal['old'][old_key])
            require(not path.exists() or sha256(path) in allowed, 'current pair changed since interruption')
        if old_root:
            atomic_copy(old_root / 'd3d9.dll', safe(game, 'd3d9.dll'), identity(old_root / 'd3d9.dll'))
            atomic_bytes(safe(game, INSTALL), (old_root / 'install.json').read_bytes())
            current(game, full=False)
        else:
            safe(game, 'd3d9.dll').unlink(missing_ok=True)
            safe(game, INSTALL).unlink(missing_ok=True)
        safe(game, JOURNAL).unlink()
        remove_exact(game, {directory + '/' + n: identity(root / n) for n in expected_files})
        if old_root:
            remove_exact(game, {str((old_root / n).relative_to(game)): identity(old_root / n) for n in ('d3d9.dll', 'install.json')})


def uninstall(game, *, fault=lambda phase: None):
    with installer_lock(game):
        no_journal(game)
        old = current(game, full=False)
        require(old, 'no owned installation')
        files = selection_files(game, old, full=False)
        if old.get('previous'):
            _, previous = snapshot_paths(game, old['previous'])
            files.update(selection_files(game, previous, full=False))
        # Report additions without treating them as owned deletion candidates.
        retained = []
        roots = {str(Path(name).parent) for name in files if name.endswith(('/package.json', '/source.json'))}
        for name in roots:
            root = safe(game, name)
            try:
                retained.extend(name + '/' + rel for rel in tree_files(root) if name + '/' + rel not in files)
            except PackageError:
                retained.append(name + ' (unsafe addition retained)')
        transaction(game, old, None, None, fault=fault)
        return sorted(set(retained + remove_exact(game, files)))


def guard_legacy(game, source_id):
    """Legacy original-file swapping must not invalidate an active managed source."""
    no_journal(game)
    path = safe(game, INSTALL)
    if path.exists():
        manifest = read_json(path)
        require(not any(row['id'] == source_id for row in manifest.get('media', {}).get('sources', [])),
                f'source {source_id} selected by managed media; uninstall managed selection first')
