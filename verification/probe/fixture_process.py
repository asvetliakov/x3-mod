"""Fixture process lifetime under Wine: timeout cleanup and orphan detection.

A fixture .exe that crashes under Wine gets ``winedbg --auto`` attached by
Wine's unhandled-exception handler; both then sit at 0 % CPU. A runner whose
``subprocess.run(..., timeout=...)`` expires kills only its direct child (the
CrossOver ``wine`` launcher), so the fixture and its debugger survive with
PPID 1 and ``wine_lock.py``'s preflight refuses every later Wine command.

``run`` is a drop-in for ``subprocess.run(command, timeout=...)`` that, on a
timeout, also ends the fixture's descendants and any process whose command
line names the case's build directory (never anything else; never the game),
waits up to ``WAIT_SECONDS`` for them and raises ``FixtureTimeout`` carrying
the cleanup report. ``orphans`` identifies the leftovers of such a crash for
``wine_lock.py`` (report in the preflight, end with ``--clean-orphans``).

Wine rewrites a Windows process's argv to its Windows command line, so ps
shows the fixture as a DOS path on whichever drive maps its directory
(witness of the 2026-09-24 incident under CrossOver Preview, ps rows quoted in
``verification/analysis/test_wine_lock.py``: ``Y:\\x3-mod\\.claude\\worktrees\\
...\\verification\\probe\\build\\...\\motion_output_fixture.exe``; Y: presumably
maps the home directory) and the debugger as ``winedbg --auto <winpid> <event>``.
The ``--auto`` argument is a Windows PID, which ps cannot map to a Unix PID,
and the same witness showed both processes with PPID 1: the debugger is not a
Unix child of the fixture. A debugger is therefore ended only as a descendant
of an ended process, or as a PPID-1 ``winedbg --auto`` while no game runs
(the lock holder's crashed case is then its only possible debuggee); never by
name alone while the game is up.
"""
import os
import re
import signal
import subprocess
import time

from game_guard import WINE_COMMANDS, VALUE_OPTIONS, is_game_line

WAIT_SECONDS = 10.0   # after SIGTERM
KILL_WAIT_SECONDS = 2.0  # after the SIGKILL that follows
BUILD_SEGMENT = '/verification/probe/build/'


def process_table():
    """One ``ps`` snapshot as {pid: (ppid, args)}; RuntimeError when malformed."""
    inventory = subprocess.run(['ps', '-axww', '-o', 'pid=,ppid=,args='],
                               capture_output=True, text=True, check=True, timeout=10)
    rows = {}
    for line in inventory.stdout.splitlines():
        if not line.strip():
            continue
        fields = line.strip().split(None, 2)
        if len(fields) != 3 or not all(value.isdigit() for value in fields[:2]):
            raise RuntimeError('process inventory malformed')
        rows[int(fields[0])] = (int(fields[1]), fields[2])
    return rows


def unix_text(text):
    """Map a DOS ``D:\\a\\b`` spelling (any drive letter) to ``/a/b`` for path comparisons.

    Only Z: is the Unix root; for another drive the result is the path
    relative to that drive's (unknown) Unix directory, which ``_image_within``
    matches as a suffix.
    """
    text = text.replace('\\', '/')
    return re.sub(r'(?i)(^|[\s"\'=])[a-z]:/', r'\1/', text)


def _basename(token):
    return token.replace('\\', '/').rsplit('/', 1)[-1].lower()


def image(arguments):
    """The program a ps line executes, as a Unix-style path, or ''.

    A Windows process line starts with its (possibly quoted, possibly spaced)
    image path; a Wine launcher/preloader line names the program after its
    options and loader words. A spaced path is joined word by word, never past
    a word that starts another path, an option (``-``) or a relative path (``.``).
    """
    return _image(arguments)[0]


def wine_image(arguments):
    """The image of a Wine process line, or '' for a native process.

    Only a line whose raw first word (or quoted first token) is a drive-letter
    DOS path, or a Wine launcher/preloader line, names a Windows program: a
    native ``less``/``tail``/``python3`` given a fixture path never matches.
    """
    path, wine = _image(arguments)
    return path if wine else ''


def _image(arguments):
    words = arguments.split()
    # A launcher path may contain a space (``CrossOver Preview.app``).
    loader = next((end for end in range(1, min(len(words), 8) + 1)
                   if _basename(' '.join(words[:end])) in WINE_COMMANDS), 0)
    if loader:
        rest = words[loader:]
        while rest:
            word = rest[0]
            if word == '--':
                rest.pop(0)
                break
            if word in VALUE_OPTIONS:
                del rest[:2]
                continue
            if word.startswith('-') or _basename(word) in WINE_COMMANDS:
                rest.pop(0)
                continue
            break
        # A preloader's loader argument may itself be a spaced path.
        loader_end = next((end for end in range(1, min(len(rest), 8) + 1)
                           if _basename(' '.join(rest[:end])) in WINE_COMMANDS), 0)
        words = rest[loader_end:]
    if not words:
        return '', False
    if words[0].startswith('"'):
        joined = ' '.join(words)[1:]
        end = joined.find('"')
        if end <= 0:
            return '', False
        return unix_text(joined[:end]), bool(loader or _DOS_START.match(joined))
    wine = bool(loader or _DOS_START.match(words[0]))
    if not _PATH_START.match(words[0]):
        return words[0], wine  # a bare command: its arguments are not its image
    for end in range(1, len(words) + 1):
        word = words[end - 1]
        if end > 1 and (_PATH_START.match(word) or word.startswith(('-', '.'))):
            break  # another path or an argument starts: the image had no .exe suffix
        candidate = ' '.join(words[:end])
        if candidate.lower().endswith('.exe'):
            return unix_text(candidate), wine
    return unix_text(words[0]), wine


_PATH_START = re.compile(r'^(/|[A-Za-z]:[\\/])')
_DOS_START = re.compile(r'^[A-Za-z]:[\\/]')


def is_auto_debugger(arguments):
    """``winedbg --auto`` (Wine's AeDebug debugger), by its image and option."""
    words = arguments.split()
    name = _basename(image(arguments))
    return name in ('winedbg.exe', 'winedbg') and '--auto' in words


def _is_game(pid, arguments):
    return is_game_line(f'{pid} {arguments}') or _basename(image(arguments)) == 'x3ap.exe'


def descendants(rows, roots):
    """Unix PIDs whose ancestry reaches one of ``roots`` (roots excluded)."""
    children = {}
    for pid, (ppid, _) in rows.items():
        children.setdefault(ppid, []).append(pid)
    found, stack = set(), list(roots)
    while stack:
        for child in children.get(stack.pop(), ()):
            if child not in found and child not in roots:
                found.add(child)
                stack.append(child)
    return found


def _within(arguments, directory):
    """True when the command line names a path inside ``directory``."""
    return directory.casefold() in unix_text(arguments).casefold()


def _image_within(arguments, directory):
    """True when the program a ps line runs lies inside ``directory`` (a Unix path).

    A Z: (or Unix) image must start with the directory. An image on another
    drive has lost the drive's Unix root, so under a ``verification/probe/build``
    tree it matches when its part after that segment starts with the
    directory's, and its part before the segment is a trailing part of the
    directory's (``/x3-mod/.claude/worktrees/w`` of ``/Users/u/x3-mod/.claude/worktrees/w``).
    """
    path, directory = wine_image(arguments).casefold(), directory.casefold()
    if not path:
        return False  # a native process is never this fixture
    if path.startswith(directory):
        return True
    if BUILD_SEGMENT not in path or BUILD_SEGMENT not in directory:
        return False
    path_root, path_rest = path.split(BUILD_SEGMENT, 1)
    dir_root, dir_rest = directory.split(BUILD_SEGMENT, 1)
    return (bool(path_root) and path_rest.startswith(dir_rest)
            and (dir_root + '/').endswith(path_root + '/'))


def game_running(rows):
    return any(_is_game(pid, arguments) for pid, (_, arguments) in rows.items())


def stray_debuggers(rows):
    """PPID-1 ``winedbg --auto`` processes: {pid: args}."""
    return {pid: arguments for pid, (ppid, arguments) in rows.items()
            if ppid == 1 and is_auto_debugger(arguments) and not _is_game(pid, arguments)}


def fixture_processes(rows, root_pid, build_dir, baseline=None):
    """Split the processes of one fixture case into (kill, skipped).

    Candidates: the runner's child ``root_pid``, its descendants, and any
    process whose command line names ``build_dir``. A candidate is killed only
    when the program it executes (its image, after Wine's DOS-to-Unix mapping,
    or the program a Wine launcher line runs) lies inside ``build_dir``, or it
    is a ``winedbg --auto`` descended from a killed process, or a PPID-1
    ``winedbg --auto`` absent from ``baseline`` (the stray debuggers before the
    case started; None: unknown, none ended) while no game runs. The game is
    never killed. A shell or editor that merely mentions the directory is skipped.
    """
    directory = unix_text(str(build_dir)).rstrip('/') + '/'
    candidates = ({root_pid} if root_pid in rows else set()) | descendants(rows, {root_pid})
    candidates |= {pid for pid, (_, arguments) in rows.items()
                   if _within(arguments, directory) or _image_within(arguments, directory)}
    if baseline is not None and not game_running(rows):
        candidates |= {pid for pid, arguments in stray_debuggers(rows).items() if baseline.get(pid) != arguments}
    kill = {pid for pid in candidates
            if (_image_within(rows[pid][1], directory) or
                (rows[pid][0] == 1 and is_auto_debugger(rows[pid][1]) and baseline is not None
                 and baseline.get(pid) != rows[pid][1] and not game_running(rows)))
            and not _is_game(pid, rows[pid][1])}
    while True:
        debuggers = {pid for pid in descendants(rows, kill) - kill
                     if is_auto_debugger(rows[pid][1]) and not _is_game(pid, rows[pid][1])}
        if not debuggers:
            break
        kill |= debuggers
    return kill, (candidates | descendants(rows, kill)) - kill


def orphans(rows):
    """Orphaned fixtures left by a crash: [(pid, args, [(debugger pid, args)])].

    Match rule: PPID 1; the line is a Wine process (raw first word a
    drive-letter DOS path, or a Wine launcher/preloader form); its image path
    (after Wine's DOS-to-Unix mapping) ends in ``.exe`` and lies under a ``verification/probe/build/``
    directory (any drive letter; a worktree's ``.claude/worktrees/<name>/``
    tree included); not the game. A ``winedbg --auto`` descendant is paired
    with it; PPID-1 debuggers are handled by ``orphan_debuggers``.
    """
    found = []
    for pid, (ppid, arguments) in sorted(rows.items()):
        path = wine_image(arguments)
        if (ppid != 1 or not path.lower().endswith('.exe') or BUILD_SEGMENT not in path.casefold()
                or _is_game(pid, arguments)):
            continue
        debuggers = [(child, rows[child][1]) for child in sorted(descendants(rows, {pid}))
                     if is_auto_debugger(rows[child][1]) and not _is_game(child, rows[child][1])]
        found.append((pid, arguments, debuggers))
    return found


def orphan_debuggers(rows, paired):
    """PPID-1 ``winedbg --auto`` not paired with a fixture, while no game runs.

    With the game up such a debugger may be the game's, so none is returned.
    """
    if game_running(rows):
        return []
    return [(pid, arguments) for pid, arguments in sorted(stray_debuggers(rows).items()) if pid not in paired]


def unpaired_debuggers(rows, paired):
    """``winedbg --auto`` processes that no orphan claims (reported, never killed)."""
    return [(pid, arguments) for pid, (_, arguments) in sorted(rows.items())
            if pid not in paired and is_auto_debugger(arguments)]


def protected(rows):
    """PIDs never signalled: <= 1, this process and its ancestors."""
    keep = {0, 1, os.getpid(), os.getppid()}
    current, seen = os.getpid(), set()
    while current in rows and current not in seen:  # the whole ancestor chain
        seen.add(current)
        current = rows[current][0]
        keep.add(current)
    return keep


def _wait_gone(pids, rows, table, wait):
    """Poll until ``pids`` are gone or ``wait`` s pass; return the survivors.

    A PID is considered alive while the table shows it with the same command
    line, so a reused PID is not mistaken for a survivor.
    """
    deadline = time.monotonic() + wait
    while True:
        try:
            current = table()
        except (RuntimeError, OSError, subprocess.SubprocessError):
            current = None
        if current is not None:
            alive = {pid for pid in pids if pid in current and current[pid][1] == rows[pid][1]}
            if not alive or time.monotonic() >= deadline:
                return alive
        elif time.monotonic() >= deadline:
            return set(pids)
        time.sleep(0.2)


def _signal(pids, number):
    for pid in sorted(pids):
        try:
            os.kill(pid, number)
        except ProcessLookupError:
            pass


def end(pids, rows, table=process_table, wait=WAIT_SECONDS):
    """SIGTERM ``pids`` (their snapshot ``rows``), wait up to ``wait`` s, SIGKILL the rest.

    Returns (survivors, refused): PIDs still alive ``KILL_WAIT_SECONDS``
    after the SIGKILL, and PIDs refused before any signal (``protected``).
    """
    refused = set(pids) & protected(rows)
    pids = set(pids) - refused
    if not pids:
        return set(), refused
    _signal(pids, signal.SIGTERM)
    alive = _wait_gone(pids, rows, table, wait)
    if alive:
        _signal(alive, signal.SIGKILL)
        alive = _wait_gone(alive, rows, table, min(KILL_WAIT_SECONDS, wait))
    return alive, refused


class FixtureTimeout(subprocess.TimeoutExpired):
    """A per-case timeout whose message carries the process cleanup report."""

    def __init__(self, cmd, timeout, output=None, stderr=None, cleanup=''):
        super().__init__(cmd, timeout, output, stderr)
        self.cleanup = cleanup

    def __str__(self):
        return f'{super().__str__()} {self.cleanup}'

    def __repr__(self):
        return f'FixtureTimeout({self.timeout!r} s, {self.cleanup!r})'


def describe(pids, rows):
    return ', '.join(f'{pid} [{rows[pid][1][:160]}]' for pid in sorted(pids)) or 'none'


def cleanup_after_timeout(process, build_dir, table=process_table, wait=WAIT_SECONDS, baseline=None):
    """End one timed-out fixture case; return the one-paragraph cleanup report."""
    try:
        rows = table()
    except (RuntimeError, OSError, subprocess.SubprocessError) as error:
        process.kill()
        try:
            process.wait(timeout=wait)
        except subprocess.TimeoutExpired:
            pass
        return f'fixture cleanup: process table unavailable ({error}); killed only the runner child {process.pid}'
    kill, skipped = fixture_processes(rows, process.pid, build_dir, baseline)
    appeared = sorted(pid for pid in kill if rows[pid][0] == 1 and is_auto_debugger(rows[pid][1]))
    process.kill()  # the runner's own child (the Wine launcher), as subprocess.run does
    try:
        process.wait(timeout=wait)
    except subprocess.TimeoutExpired:
        pass
    kill.discard(process.pid)
    skipped.discard(process.pid)
    survivors, refused = end(kill, rows, table, wait) if kill else (set(), set())
    ended = kill - survivors - refused
    return (f'fixture cleanup after timeout (build dir {build_dir}): runner child {process.pid} killed; '
            f'ended {len(ended)}: {describe(ended, rows)}; still alive after SIGTERM, {wait:.0f} s and SIGKILL: '
            f'{describe(survivors, rows)}; refused (this process or an ancestor): {describe(refused, rows)}; '
            f'PPID-1 winedbg --auto that appeared during the case: {" ".join(map(str, appeared)) or "none"}'
            + ('' if baseline is not None else ' (no baseline snapshot: none ended)')
            + f'; not ours, left running: {describe(skipped, rows)}')


def run(command, *, build_dir, timeout, table=process_table, **kwargs):
    """``subprocess.run(command, timeout=timeout, **kwargs)`` with fixture cleanup.

    The command must name a path inside ``build_dir``, the directory holding
    this case's fixture; the cleanup guard matches command lines against it.
    """
    directory = unix_text(str(build_dir)).rstrip('/') + '/'
    if not directory.strip('/') or not any(_within(str(part), directory) for part in command):
        raise ValueError(f'fixture command does not name its build directory {build_dir}')
    try:
        baseline = stray_debuggers(table())  # one ps per case: debuggers already up are not this case's
    except (RuntimeError, OSError, subprocess.SubprocessError):
        baseline = None
    process = subprocess.Popen(command, **kwargs)
    try:
        try:
            stdout, stderr = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired as expired:
            cleanup = cleanup_after_timeout(process, build_dir, table, baseline=baseline)
            try:
                stdout, stderr = process.communicate(timeout=WAIT_SECONDS)
            except subprocess.TimeoutExpired:
                stdout, stderr = expired.output, expired.stderr  # a survivor still holds the pipe
            raise FixtureTimeout(command, timeout, stdout, stderr, cleanup) from None
        except BaseException:
            process.kill()  # KeyboardInterrupt, a decode error: as subprocess.run does
            raise
    finally:
        for stream in (process.stdin, process.stdout, process.stderr):
            if stream:
                try:
                    stream.close()
                except OSError:
                    pass
        try:
            process.wait(timeout=WAIT_SECONDS)  # bounded, unlike Popen.__exit__
        except subprocess.TimeoutExpired:
            pass
    return subprocess.CompletedProcess(process.args, process.returncode, stdout, stderr)
