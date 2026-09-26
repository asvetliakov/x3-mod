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


# CrossOver bottle environment (docs/architecture/voice-decoder-adapter.md, "Bottle environment delivery"): the two
# versioned GStreamer variables in [EnvironmentVariables] of <bottle>/cxbottle.conf, which CrossOver applies to every
# program started in the bottle, so a plain CrossOver launch finds the drop-in. Entries are `"NAME" = "value"` lines;
# `;;` starts a comment. Every byte outside the two entries is preserved (line endings included).
BOTTLE_CONF = 'cxbottle.conf'
BOTTLE_BACKUP = 'cxbottle.conf.x3m-bak'
BOTTLE_SECTION = b'[EnvironmentVariables]'
BOTTLE_VARIABLES = ('GST_PLUGIN_PATH_1_0', 'GST_REGISTRY_1_0')


def bottle_env_values(dest):
    """{name: host path} for the drop-in DEST, exactly what the launcher sets for it (tools/manage.py, voice_env)."""
    dest = Path(dest)
    return {'GST_PLUGIN_PATH_1_0': str(dest / 'runtime/plugins'), 'GST_REGISTRY_1_0': str(dest / 'registry' / 'x3-arm64.bin')}


def _entry(line):
    """(name, value) of a `"NAME" = "value"` line (line ending allowed), else None; comments never match."""
    text = line.strip()
    if not text.startswith(b'"'):
        return None
    close = text.find(b'"', 1)
    if close < 0:
        return None
    rest = text[close + 1:].strip()
    if not rest.startswith(b'='):
        return None
    value = rest[1:].strip()
    if len(value) < 2 or not value.startswith(b'"') or not value.endswith(b'"'):
        return None
    return text[1:close].decode('utf-8', 'replace'), value[1:-1].decode('utf-8', 'replace')


def _section(data):
    """(lines, start, end): LINES keep their endings; lines[start:end] is the body of [EnvironmentVariables].
    TreeError when the section is absent or repeated."""
    lines = data.splitlines(keepends=True)
    headers = [i for i, line in enumerate(lines) if line.strip() == BOTTLE_SECTION]
    if len(headers) != 1:
        raise TreeError(f'{BOTTLE_CONF} has {len(headers)} {BOTTLE_SECTION.decode()} sections, expected one')
    start = end = headers[0] + 1
    while end < len(lines) and not lines[end].lstrip().startswith(b'['):
        end += 1
    return lines, start, end


def bottle_env_state(data, values):
    """{name: 'present' | 'absent' | 'differs: <current value(s)>'} of VALUES in the bottle file bytes DATA."""
    lines, start, end = _section(data)
    found = {}
    for line in lines[start:end]:
        entry = _entry(line)
        if entry and entry[0] in values:
            found.setdefault(entry[0], []).append(entry[1])
    state = {}
    for name, value in values.items():
        current = found.get(name)
        state[name] = 'absent' if not current else 'present' if current == [value] else 'differs: ' + ' | '.join(current)
    return state


def bottle_env_edit(data, values, *, remove=False):
    """(new bytes, changes): DATA with each of VALUES set or, with REMOVE, every entry of those names deleted.
    An identical entry is left alone; a differing one is replaced in place (a later duplicate of the name is
    dropped); a missing one is added after the section's last non-blank line in the file's line ending."""
    for value in values.values():
        if any(c in value for c in '"\\\r\n'):
            raise TreeError(f'value {value!r} needs quoting the bottle file does not use')
    lines, start, end = _section(data)
    eol = next((b'\r\n' if line.endswith(b'\r\n') else b'\n' for line in lines if line.endswith(b'\n')), b'\n')
    changes, seen, out, body_end = [], set(), [], None
    for index, line in enumerate(lines):
        if index == end:
            body_end = len(out)
        entry = _entry(line) if start <= index < end else None
        if entry is None or entry[0] not in values:
            out.append(line)
            continue
        name, current = entry
        if remove:
            changes.append(f'{name}: removed "{current}"')
        elif name in seen:
            changes.append(f'{name}: duplicate "{current}" removed')
        else:
            seen.add(name)
            if current == values[name]:
                out.append(line)
            else:
                out.append(f'"{name}" = "{values[name]}"'.encode() + (line[len(line.rstrip(b'\r\n')):] or eol))
                changes.append(f'{name}: replaced "{current}"')
    missing = [] if remove else [name for name in values if name not in seen]
    if missing:
        at = len(out) if body_end is None else body_end
        while at > start and not out[at - 1].strip():
            at -= 1
        if not out[at - 1].endswith(b'\n'):
            out[at - 1] += eol
        out[at:at] = [f'"{name}" = "{values[name]}"'.encode() + eol for name in missing]
        changes += [f'{name}: added' for name in missing]
    return b''.join(out), changes


def write_bottle_conf(path, data):
    """Replaces PATH by DATA through a temporary file in the same directory and a rename, keeping PATH's mode. The
    first write also copies the original to cxbottle.conf.x3m-bak (an existing backup is never overwritten).
    Returns True when the backup was created by this call."""
    path = Path(path)
    mode = path.stat().st_mode & 0o777
    backup = path.with_name(BOTTLE_BACKUP)
    created = False
    for target, payload in (((backup, path.read_bytes()),) if not backup.exists() else ()) + ((path, data),):
        temporary = target.with_name(target.name + '.x3m-new')
        with open(temporary, 'wb') as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        os.replace(temporary, target)
        created = created or target == backup
    return created
