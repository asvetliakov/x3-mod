#!/usr/bin/env python3
"""Run one command under the machine-wide Wine runner lock.

Only one Wine-executing fixture or runner may run at a time (they share the
GPU, the bottle and its timing measurements). ``game_guard`` keeps the game
and the runners apart, but two runners or a runner and a hand-started fixture
executable could still overlap. This wrapper serialises them with an
exclusive advisory lock on ``/tmp/x3-wine-runner.lock``::

    python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py
    python3 verification/probe/wine_lock.py wine build/fixture.exe 1

The wrapper waits for the lock (printing a note every 30 s with the holder's
description), runs the command, and exits with its status. ``--holder TEXT``
labels the holder for those notes; ``--timeout SECONDS`` gives up (exit 75)
instead of waiting forever. The lock is released when the wrapped command
exits, including on Ctrl-C.
"""
import argparse
import fcntl
import os
import subprocess
import sys
import time

LOCK_PATH = '/tmp/x3-wine-runner.lock'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--holder', default=None, help='label recorded while this command holds the lock')
    parser.add_argument('--timeout', type=float, default=None, help='seconds to wait for the lock before giving up')
    parser.add_argument('command', nargs=argparse.REMAINDER, help='command to run (prefix with -- if it starts with -)')
    args = parser.parse_args()
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
    try:
        os.ftruncate(fd, 0)
        os.lseek(fd, 0, os.SEEK_SET)
        os.write(fd, f'{os.getpid()} {time.strftime("%H:%M:%S")} {holder}'.encode('utf-8', 'replace'))
        try:
            return subprocess.call(command)
        except KeyboardInterrupt:
            return 130
    finally:
        try:
            os.ftruncate(fd, 0)
        except OSError:
            pass
        fcntl.flock(fd, fcntl.LOCK_UN)
        os.close(fd)


if __name__ == '__main__':
    sys.exit(main())
