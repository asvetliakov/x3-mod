#!/usr/bin/env python3
"""Preserve one completed X3 session and its referenced local capture files.

No launcher, source deletion, directory-wide copy or manifest. --since-ns is a
pre-launch time.time_ns() boundary; no newly created log is a harmless no-op.
Explicit logs are allowed, but readbacks outside that log's write interval are
reported as stale. Shader dumps are reusable only when their FNV identity and
bounded size match the logged record. Invoke after the game exits.
"""
import argparse
import hashlib
import os
from pathlib import Path
import re
import stat
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'verification/probe'))
from game_guard import game_running

CAPTURES = Path.home() / 'Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures'
SESSION = re.compile(r'session-\d{8}-\d{6}-\d+\.log\Z')
# The launcher's teed terminal output (tools/manage.py launch), written next to
# the session log: preserved with it so a wall-clock burst from another
# component can be aligned with the proxy's frame clock.
LAUNCHER_STDERR = 'launcher-stderr.log'
# Current MotionOutput writers (including the earlier color/TAA readbacks) and
# the sun-lane shadow map dump. Each tag maps to its fixed basename prefix and
# the extensions its writer may emit: the depth readback is R32F normally and
# G32R32F (.r depth, .g sun share) while the sun lane is active.
READBACKS = {
    'motion_output_color_readback': ('color', ('bgra8',)),
    'motion_output_taa_readback': ('taa', ('rgba16f',)),
    'motion_output_taa_age_readback': ('taa_age', ('r32f',)),  # --taa-debug: the age / hold count target beside each taa dump
    'motion_output_present_readback': ('present', ('bgra8',)),
    'motion_output_readback': ('motion', ('rgba32f',)),
    'motion_output_depth_readback': ('depth', ('r32f', 'rg32f', 'rgba32f')),  # rgba32f: the receiver-depth option's wide RT2
    'hdr_readback': ('hdr', ('rgba16f',)),
    'shadow_replay_map_readback': ('shadow_map', ('r32f',)),
    'sun_lens_readback': ('lens', ('bgra8',)),  # --sun-occlusion-log: Present-time back buffer on F8 frames
}
# The sun lane dumps one map per cascade as shadow_map<k>_<device>_<frame>.r32f
# (k is the cascade index, glued to the prefix); the single-map name without an
# index stays valid. No other writer appends an index to its prefix.
CASCADED = {'shadow_replay_map_readback'}
CASCADES = 8
# Every accepted readback basename is device/frame digits plus a short
# extension, so this bound is far above any real name and below any path a
# malformed record could smuggle through.
MAX_BASENAME = 64
FIELDS = re.compile(r'(?:^|\s)(\w+)=([^\s]+)')


def created_ns(info):
    # macOS birth time rejects even an old log whose mtime was touched later.
    # Linux has no portable birth time; ctime is the conservative fallback.
    if hasattr(info, 'st_birthtime_ns'):
        return info.st_birthtime_ns
    if hasattr(info, 'st_birthtime'):
        return round(info.st_birthtime * 1_000_000_000)
    return info.st_ctime_ns


def require_idle():
    if game_running():
        raise RuntimeError('X3 is still running; exit it before preserving this session')


def select_log(directory_fd, since_ns):
    found = []
    for name in os.listdir(directory_fd):
        if not SESSION.fullmatch(name):
            continue
        info = os.stat(name, dir_fd=directory_fd, follow_symlinks=False)
        if not stat.S_ISREG(info.st_mode):
            continue
        if created_ns(info) >= since_ns and info.st_mtime_ns >= since_ns:
            found.append((created_ns(info), info.st_mtime_ns, name))
    return max(found)[2] if found else None


def copy_file(source_fd, target_fd, name, *, size=None, window=None, shader_id=None,
              expected_sha256=None, since_ns=None, created_before=None):
    """Pinned directories + no-follow leaf opens prevent path/symlink escapes."""
    read_fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK, dir_fd=source_fd)
    with os.fdopen(read_fd, 'rb') as source:
        before = os.fstat(source.fileno())
        if not stat.S_ISREG(before.st_mode):
            raise ValueError('not a regular file')
        if since_ns is not None and (created_ns(before) < since_ns or before.st_mtime_ns < since_ns):
            raise ValueError('selected log predates this launch')
        if size is not None and before.st_size != size:
            raise ValueError(f'size differs: expected {size}, found {before.st_size}')
        if window and not window[0] <= before.st_mtime_ns <= window[1]:
            raise ValueError('stale or overwritten outside this session')
        if created_before is not None and created_ns(before) > created_before:
            raise ValueError('created after this session log: it belongs to a later launch')
        if shader_id is not None and not 0 < before.st_size <= 4 * 1024 * 1024:
            raise ValueError('shader size exceeds the logged writer contract')
        write_fd = os.open(name, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600, dir_fd=target_fd)
        try:
            fingerprint = 14695981039346656037
            digest = hashlib.sha256() if expected_sha256 is not None else None
            with os.fdopen(write_fd, 'wb') as target:
                remaining = size
                while remaining is None or remaining:
                    chunk = source.read(1024 * 1024 if remaining is None else min(1024 * 1024, remaining))
                    if not chunk:
                        if remaining is not None:
                            raise ValueError('source truncated while copying')
                        break
                    target.write(chunk)
                    if digest is not None:
                        digest.update(chunk)
                    if shader_id is not None:
                        for byte in chunk:
                            fingerprint = ((fingerprint ^ byte) * 1099511628211) & 0xffffffffffffffff
                    if remaining is not None:
                        remaining -= len(chunk)
                if size is not None and source.read(1):
                    raise ValueError('source grew while copying')
            after = os.fstat(source.fileno())
            keys = ('st_dev', 'st_ino', 'st_size', 'st_mtime_ns', 'st_ctime_ns')
            if any(getattr(before, key) != getattr(after, key) for key in keys):
                raise ValueError('source changed while copying')
            if shader_id is not None and fingerprint != shader_id:
                raise ValueError('shader content does not match its logged identity')
            if digest is not None and digest.hexdigest() != expected_sha256:
                raise ValueError('content does not match its logged SHA-256')
            return before
        except BaseException:
            # Only this newly created destination is removed, never a source.
            os.unlink(name, dir_fd=target_fd)
            raise


def references(log):
    """Only recognized writer records authorize copying; last write wins."""
    wanted, issues = {}, []
    for line in log:
        tag = line.split(' ', 1)[0]
        if tag not in READBACKS and tag not in ('shader', 'mesh_adjacency_dump', 'loading_intervals_file'):
            continue
        row = dict(FIELDS.findall(line))
        try:
            if tag in READBACKS:
                prefix, extensions = READBACKS[tag]
                # Basename only: the shape below admits no separator, no '..'
                # and no absolute path, and the device/frame must be the ones
                # this record reports.
                index = f'[0-{CASCADES - 1}]?' if tag in CASCADED else ''
                shape = re.fullmatch(re.escape(prefix) + index + r'_(\d+)_(\d+)\.([a-z0-9]+)', row['file'])
                if (shape is None or len(row['file']) > MAX_BASENAME
                        or int(shape[1]) != int(row['device'])
                        or int(shape[2]) != int(row['frame']) or shape[3] not in extensions):
                    raise ValueError('unsafe or unexpected readback basename')
                name = row['file']
                if int(row['result'], 16) != 0 or int(row['bytes']) <= 0:
                    wanted.pop(name, None)
                    issues.append(f'{name}: writer did not report a successful readback')
                    continue
                wanted[name] = {'size': int(row['bytes']), 'timed': True}
            elif tag == 'loading_intervals_file':
                name = row['file']
                if not re.fullmatch(r'loading-intervals-\d+-\d+\.bin', name):
                    raise ValueError('unsafe interval basename')
                size = int(row['bytes'])
                if row['written'] != '1' or not 864 <= size <= 25166688:
                    wanted.pop(name, None)
                    issues.append(f'{name}: interval export incomplete or invalid size')
                    continue
                wanted[name] = {'size': size, 'timed': True}
            elif tag == 'shader':
                if row['kind'] not in ('vs', 'ps') or not re.fullmatch(r'[0-9a-f]{16}', row['id']):
                    raise ValueError('unsafe shader identity')
                name = f'{row["kind"]}_{row["id"]}.bin'
                if row['dumped'] != '1':
                    issues.append(f'{name}: shader dump was not successful')
                    continue
                size = int(row['bytes'])
                if not 0 < size <= 4 * 1024 * 1024:
                    raise ValueError('shader size exceeds the logged writer contract')
                wanted[name] = {'size': size, 'shader_id': int(row['id'], 16)}
            else:
                name = f'mesh-adjacency-{int(row["index"])}.bin'
                if row['path'] != name:
                    raise ValueError('unsafe or unexpected adjacency basename')
                if row['written'] != '1':
                    wanted.pop(name, None)
                    issues.append(f'{name}: adjacency dump was not successful')
                    continue
                wanted[name] = {'timed': True}
        except (KeyError, ValueError) as error:
            issues.append(f'{tag}: ignored invalid record ({error})')
    return wanted, issues


def snapshot(*, capture_dir=CAPTURES, since_ns=None, log=None, destination_root=Path('/tmp')):
    if log is None and (since_ns is None or since_ns <= 0):
        raise ValueError('provide an explicit log or positive pre-launch --since-ns boundary')
    require_idle()  # An unavailable process inventory also raises and refuses.
    capture_dir = Path(log).absolute().parent if log is not None else Path(capture_dir)
    if not capture_dir.exists():
        if log is not None:
            raise FileNotFoundError(capture_dir)
        return None, 0, []
    source_fd = os.open(capture_dir, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    with closing_fd(source_fd):
        name = Path(log).name if log is not None else select_log(source_fd, since_ns)
        if name is None:
            return None, 0, []
        if not SESSION.fullmatch(name):
            raise ValueError('explicit log must have a session timestamp/PID basename')
        existing = (re.fullmatch(r'x3-bottleX3-run(\d+)', path.name) for path in Path(destination_root).iterdir())
        number = 1 + max((int(match[1]) for match in existing if match), default=0)
        while True:
            destination = Path(destination_root) / f'x3-bottleX3-run{number}'
            try:
                destination.mkdir(mode=0o700)
                break
            except FileExistsError:
                number += 1
        target_fd = os.open(destination, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
        with closing_fd(target_fd):
            # Log first. All authorization is parsed from this immutable copy.
            info = copy_file(source_fd, target_fd, name, since_ns=since_ns if log is None else None)
            log_fd = os.open(name, os.O_RDONLY | os.O_NOFOLLOW, dir_fd=target_fd)
            with os.fdopen(log_fd, 'r', errors='replace') as copied_log:
                wanted, issues = references(copied_log)
            count = 0
            for filename, options in wanted.items():
                options = dict(options)
                if options.pop('timed', False):
                    options['window'] = (created_ns(info), info.st_mtime_ns)
                try:
                    copy_file(source_fd, target_fd, filename, **options)
                    count += 1
                except (OSError, ValueError) as error:
                    issues.append(f'{filename}: not preserved ({error})')
            # The launcher's own log is authorized by its name and this launch's
            # boundary, not by a record inside the session log; it is absent for
            # a session started outside tools/manage.py launch.
            # The launcher starts before the proxy creates its session log, so
            # this launch's file was created no later than the log: that
            # excludes a later launch's file even when --log names an older
            # session. --since-ns additionally excludes an earlier one. A file
            # never touched after the log was created is still preserved, with
            # its mtime relation recorded, because a quiet session writes
            # nothing after the header line.
            bounds = {'created_before': created_ns(info)}
            if since_ns is not None:
                bounds['since_ns'] = since_ns
            try:
                stamps = copy_file(source_fd, target_fd, LAUNCHER_STDERR, **bounds)
                count += 1
                if stamps.st_mtime_ns < created_ns(info):
                    issues.append(f'{LAUNCHER_STDERR}: not written after the session log was created '
                                  f'(mtime {stamps.st_mtime_ns} < log creation {created_ns(info)})')
            except FileNotFoundError:
                pass
            except (OSError, ValueError) as error:
                issues.append(f'{LAUNCHER_STDERR}: not preserved ({error})')
            require_idle()
            return destination, count, issues


class closing_fd:
    def __init__(self, fd): self.fd = fd
    def __enter__(self): return self.fd
    def __exit__(self, *_): os.close(self.fd)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument('--since-ns', type=int)
    selection.add_argument('--log', type=Path)
    parser.add_argument('--capture-dir', type=Path, default=CAPTURES)
    args = parser.parse_args()
    try:
        destination, count, issues = snapshot(capture_dir=args.capture_dir, since_ns=args.since_ns, log=args.log)
        if destination is None:
            print('No new X3 proxy session; no snapshot created (vanilla/dry-run is expected).')
            return 0
        print(f'X3 session preserved in {destination} ({count} referenced files).')
        for issue in issues:
            print(f'Snapshot incomplete: {issue}', file=sys.stderr)
        return 2 if issues else 0
    except (OSError, ValueError, RuntimeError) as error:
        print(f'X3 snapshot refused/failed: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
