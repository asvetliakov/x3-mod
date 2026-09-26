"""The shipped WMA speech decoder tree and its drop-in copy <game>/x3m/voice-decoder
(docs/architecture/voice-decoder-adapter.md, "Game-directory drop-in").

Used by `tools/manage.py voice-decoder` (install and check; the release zip does not carry the tree since
2026-09-27, it is a CrossOver developer path). The shipped set is every regular file of the source tree except registry/, GStreamer's
cache that the launcher creates next to it; artifact-sha256.txt names the binaries and patches, and one line of it
(the patched gst-libav source) describes a file that is not shipped.
"""
import hashlib
import os
from pathlib import Path

GAME_SUBDIR = 'x3m/voice-decoder'
HASH_LIST = 'artifact-sha256.txt'
SKIPPED_PARTS = {'registry', '__pycache__', '.DS_Store'}


class TreeError(ValueError):
    pass


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def shipped(source):
    """Sorted POSIX relative paths of the files to ship from SOURCE; TreeError on a symlink or a missing hash list."""
    source = Path(source)
    if not (source / HASH_LIST).is_file() or not (source / 'runtime/plugins/libgstlibav.dylib').is_file():
        raise TreeError(f'{source} is not a voice decoder tree ({HASH_LIST} or runtime/plugins/libgstlibav.dylib missing)')
    files = []
    for path in sorted(source.rglob('*')):
        relative = path.relative_to(source)
        if SKIPPED_PARTS & set(relative.parts):
            continue
        if path.is_symlink():
            raise TreeError(f'symlink in the voice decoder tree: {path}')
        if path.is_file():
            files.append(relative.as_posix())
    return files


def hash_list(root):
    """{relative path: sha256} of ROOT/artifact-sha256.txt (`<sha256>  <path>` lines)."""
    entries = {}
    for number, line in enumerate((Path(root) / HASH_LIST).read_text().splitlines(), 1):
        if not line.strip():
            continue
        parts = line.split(None, 1)
        if len(parts) != 2 or len(parts[0]) != 64 or any(c not in '0123456789abcdef' for c in parts[0]):
            raise TreeError(f'{HASH_LIST} line {number} is not "<sha256>  <path>"')
        entries[parts[1].strip()] = parts[0]
    return entries


def verify_hashes(root, files):
    """(verified, not_shipped, problems) of ROOT against its own artifact-sha256.txt; FILES is the shipped set."""
    verified, not_shipped, problems = 0, 0, []
    for relative, expected in hash_list(root).items():
        if relative not in files:
            not_shipped += 1
            continue
        path = Path(root) / relative
        if not path.is_file():
            problems.append(f'{relative} missing')
        elif digest(path) != expected:
            problems.append(f'{relative} sha256 mismatch')
        else:
            verified += 1
    return verified, not_shipped, problems


def status(source, dest):
    """('valid' | 'stale' | 'missing', detail): DEST compared file by file with SOURCE and against its hash list."""
    files = shipped(source)
    dest = Path(dest)
    if not dest.is_dir():
        return 'missing', f'{dest} does not exist'
    absent = [f for f in files if not (dest / f).is_file()]
    if len(absent) == len(files):
        return 'missing', f'{dest} holds none of the {len(files)} shipped files'
    differing = [f for f in files if f not in absent and digest(dest / f) != digest(Path(source) / f)]
    verified, not_shipped, problems = verify_hashes(dest, files) if HASH_LIST not in absent else (0, 0, [f'{HASH_LIST} missing'])
    if absent or differing or problems:
        named = [f'{f} missing' for f in absent] + [f'{f} differs' for f in differing] + problems
        return 'stale', f'{len(named)} problem(s): ' + ', '.join(sorted(set(named))[:6])
    return 'valid', f'{len(files)} files match {source}; {verified} artifact hashes verified, {not_shipped} listed but not shipped'


def install(source, dest, safe_path):
    """Copies the shipped files of SOURCE into DEST (identical files are left alone), creates DEST/registry and
    verifies DEST's hash list. SAFE_PATH(relative) returns the checked destination path of one relative name (no
    symlink, no case alias). Returns (copied, unchanged, verified, not_shipped); TreeError when SOURCE fails its own
    hash list before the copy or DEST fails it after."""
    files = shipped(source)
    verified, _, problems = verify_hashes(source, files)
    if problems or not verified:
        raise TreeError(f'source {source} fails its {HASH_LIST}: ' + (', '.join(problems) or 'no shipped entry'))
    copied = unchanged = 0
    for relative in files:
        data = (Path(source) / relative).read_bytes()
        target = safe_path(relative)
        if target.is_file() and target.read_bytes() == data:
            unchanged += 1
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        temporary = target.with_name(target.name + '.x3m-new')
        temporary.write_bytes(data)
        os.chmod(temporary, 0o644)
        os.replace(temporary, target)
        copied += 1
    registry = safe_path('registry')
    registry.mkdir(exist_ok=True)
    verified, not_shipped, problems = verify_hashes(dest, files)
    if problems:
        raise TreeError(f'{dest} fails {HASH_LIST} after the copy: ' + ', '.join(problems))
    return copied, unchanged, verified, not_shipped
