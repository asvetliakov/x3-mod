#!/usr/bin/env python3
"""Run one command under the machine-wide Wine runner lock.

Only one Wine-executing fixture or runner may run at a time (they share the
GPU, the bottle and its timing measurements). ``game_guard`` keeps the game
and the runners apart, but two runners or a runner and a hand-started fixture
executable could still overlap. This wrapper serialises them with an
exclusive advisory lock on ``/tmp/x3-wine-runner.lock``::

    python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py
    python3 verification/probe/wine_lock.py wine build/fixture.exe 1

After acquiring the lease, one shared preflight refuses a running game or
competing fixture/runner. No separate shell process scan is needed.
``--timings-json PATH`` optionally records lock wait and child elapsed seconds.

The wrapper waits for the lock (printing a note every 30 s with the holder's
description), runs the command, and exits with its status. ``--holder TEXT``
labels the holder for those notes; ``--timeout SECONDS`` gives up (exit 75)
instead of waiting forever. The lock is released when the wrapped command
exits, including on Ctrl-C.
"""
import argparse
import fcntl
import os
import json
import re
import subprocess
import sys
import time
from pathlib import Path

from game_guard import WINE_COMMANDS, VALUE_OPTIONS, is_game_line

LOCK_PATH = '/tmp/x3-wine-runner.lock'


def _basename(token):
    return token.replace('\\', '/').rsplit('/', 1)[-1].lower()


def _command(arguments):
    """Split ps's unquoted executable path, including CrossOver's space.

    Only the executable and its immediate script/loader argument are examined;
    a shell, editor or analysis command mentioning a fixture is not a runner.
    """
    words = arguments.split()
    for end in range(1, len(words) + 1):
        word = words[end - 1]
        if word.startswith('-'):
            break
        candidate = ' '.join(words[:end])
        name = _basename(candidate)
        if (name in WINE_COMMANDS or re.fullmatch(r'python(?:\d+(?:\.\d+)*)?', name)
                or name.endswith(('.exe', '.py'))):
            return name, words[end:]
        # A complete executable path or a bare command has no embedded spaces.
        if end == 1 and (not word.startswith('/') or Path(word).is_file()):
            break
    return '', []


def is_runner(arguments):
    """Identify a runner or fixture, without matching its waiting lock wrapper."""
    command, rest = _command(arguments)
    if command.startswith('python'):
        while rest and rest[0].startswith('-'):
            option = rest.pop(0)
            if option == '--':
                break
            if option in ('-c', '-m'):
                return False
            if option in ('-W', '-X') and rest:
                rest.pop(0)
        command = _basename(rest[0]) if rest else ''
    if command.endswith('.py'):
        return command in RUNNER_NAMES
    if command in WINE_COMMANDS:
        while rest:
            option = rest.pop(0)
            if option == '--':
                break
            if option in VALUE_OPTIONS:
                if rest:
                    rest.pop(0)
                continue
            if option.startswith('-') or _basename(option) in WINE_COMMANDS:
                continue
            rest.insert(0, option)
            break
        command, _ = _command(' '.join(rest))
    # Named fixtures also cover copies retained outside the normal build tree.
    # Other console probes use their source stem as the executable name.
    return command.endswith('.exe') and (
        'fixture' in command or command[:-4] in FIXTURE_STEMS)


# Inventory only this project's runner names: unrelated run_server.py services
# must not prevent fixtures. Basenames also recognize retained runner copies.
PROBE = Path(__file__).parent
RUNNER_NAMES = {p.name.lower() for pattern in ('run_*.py', '*_run.py') for p in PROBE.glob(pattern)} | {
    'check_bloom_shaders.py', 'check_bloom_composition.py', 'generate_bloom_programs.py'}
FIXTURE_STEMS = {p.stem.lower() for p in PROBE.glob('*.cpp')}


def preflight(pid=None):
    """Check one process snapshot after acquiring the lease; fail closed on ps errors.

    Ancestors can be a runner wrapping its own Wine child. Other wine_lock.py
    processes are waiting for our lease and are not competing executions. This
    advisory check cannot prevent an uncooperative process starting afterward.
    """
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
    current = os.getpid() if pid is None else pid
    if current not in rows:
        raise RuntimeError('process inventory missing this lock process')
    ancestors = set()
    while current in rows and current not in ancestors:
        ancestors.add(current)
        current = rows[current][0]
    conflicts = [f'{process} {arguments}' for process, (_, arguments) in rows.items()
                 if process not in ancestors and
                 (is_game_line(f'{process} {arguments}') or is_runner(arguments))]
    if conflicts:
        raise RuntimeError('game or competing Wine runner/fixture active: ' + '; '.join(conflicts))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--holder', default=None, help='label recorded while this command holds the lock')
    parser.add_argument('--timeout', type=float, default=None, help='seconds to wait for the lock before giving up')
    parser.add_argument('command', nargs=argparse.REMAINDER, help='command to run (prefix with -- if it starts with -)')
    parser.add_argument('--timings-json', type=Path, help='optional lock-wait and child elapsed time record')
    args = parser.parse_args(argv)
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('no command given')
    holder = args.holder or ' '.join(command)[:120]
    fd = os.open(LOCK_PATH, os.O_RDWR | os.O_CREAT, 0o666)
    started = time.monotonic()
    last_note = started
    while True:
        try:
            fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            break
        except BlockingIOError:
            now = time.monotonic()
            if args.timeout is not None and now - started > args.timeout:
                print(f'wine_lock: gave up after {now - started:.0f} s waiting for {LOCK_PATH}', file=sys.stderr)
                os.close(fd)
                return 75
            if now - last_note >= 30:
                try:
                    os.lseek(fd, 0, os.SEEK_SET)
                    owner = os.read(fd, 512).decode('utf-8', 'replace').strip()
                except OSError:
                    owner = ''
                print(f'wine_lock: waiting {now - started:.0f} s for the Wine runner lock (held by: {owner or "unknown"})',
                      file=sys.stderr, flush=True)
                last_note = now
            time.sleep(2)
    lock_wait = time.monotonic() - started
    child_started = None
    child_elapsed = None
    status = 75
    try:
        os.ftruncate(fd, 0)
        os.lseek(fd, 0, os.SEEK_SET)
        os.write(fd, f'{os.getpid()} {time.strftime("%H:%M:%S")} {holder}'.encode('utf-8', 'replace'))
        try:
            preflight()
        except (RuntimeError, OSError, subprocess.SubprocessError) as error:
            print(f'wine_lock: preflight refused: {error}', file=sys.stderr)
            return status
        try:
            child_started = time.monotonic()
            status = subprocess.call(command)
        except KeyboardInterrupt:
            status = 130
        finally:
            if child_started is not None:
                child_elapsed = time.monotonic() - child_started
        return status
    finally:
        try:
            os.ftruncate(fd, 0)
        except OSError:
            pass
        fcntl.flock(fd, fcntl.LOCK_UN)
        os.close(fd)
        if args.timings_json:
            try:
                args.timings_json.write_text(json.dumps({
                    'lock_wait_seconds': lock_wait, 'child_elapsed_seconds': child_elapsed,
                    'exit_code': status}, indent=2) + '\n')
            except OSError as error:
                print(f'wine_lock: could not write optional timings: {error}', file=sys.stderr)


if __name__ == '__main__':
    sys.exit(main())
