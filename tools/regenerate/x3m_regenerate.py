#!/usr/bin/env python3
"""x3m-regenerate: rebuild every mod-dependent x3-modern asset for the X3 install it sits in.

User flow (docs/user/regenerate.md): put the executable into the game directory (beside X3AP.exe),
double-click it. It starts at once, needs no answers, and runs, in order:

  0. recovery       = a write killed mid-way (addon/*.x3m-replaced left behind) is rolled back first: the
                      moved-aside previous files go back over any partial ones ("recovered interrupted write")
  1. fog families   = tools/analysis/fog_families.py --install --replace for that directory, with
                      --mod-cat addon/mods/<name>.cat when a package is selected (a family only the package
                      has is covered); <game>/x3m/fog-families.bin + .json, one .previous kept
  2. LOD overlay    = tools/analysis/lod_overlay.py --batch --sync --mod auto --install
                      (addon/NN.cat/.dat + marker, the derived addon/mods/<mod>-x3m-lod copy; bodies whose
                      inputs and tool settings are unchanged are reused, every other one is rebaked)
  3. fog check      = fog_families.py --check: step 2 changes the numbered catalogues, so the fog record's
                      launch fingerprint is refreshed (otherwise the launcher reports the fog file stale)

Every console line (timestamped) goes to <game>/x3m-regenerate.log too (rewritten each run); the tools'
detailed output (the LOD batch summary, the fog family table) goes to the log only, marked '|'. The run
ends with 'press any key' (--no-wait skips it) and exits 0 when every step succeeded, else 1. It refuses
when the directory holds no X3AP.exe or the game is running (tasklist on Windows, ps elsewhere).

The selected mod (the start menu's package, HKCU\\Software\\EGOSOFT\\X3AP\\ModName) is read through winreg on
Windows and from the CrossOver bottle's user.reg on macOS/Linux; when it cannot be read no package is baked.

Options are for scripted use only: --game-dir DIR (default: the executable's directory when frozen, else
the current directory), --jobs N (default CPU count - 1; the LOD bake additionally keeps its per-worker
memory cap, lod_overlay.default_jobs), --no-wait.
"""
from __future__ import annotations

import argparse
import contextlib
import csv
import io
import json
import multiprocessing
import os
import subprocess
import sys
import time
import traceback
from pathlib import Path

LOG_NAME = 'x3m-regenerate.log'
GAME_EXE = 'x3ap.exe'

if not getattr(sys, 'frozen', False):      # source checkout: the tools live beside this directory
    _ROOT = Path(__file__).resolve().parents[2]
    for _path in (_ROOT / 'tools' / 'analysis', _ROOT / 'tools', _ROOT / 'tools' / 'build',
                  _ROOT / 'verification' / 'probe'):
        if str(_path) not in sys.path:
            sys.path.insert(0, str(_path))


# --- console + log ------------------------------------------------------------------------------

class Log:
    """say() writes one timestamped line to the console and the log file; detail() to the log only."""

    def __init__(self, path, console):
        self.console = console
        self.path = path
        self.error = None
        try:
            self.file = open(path, 'w', encoding='utf-8', errors='replace', newline='\n')
        except OSError as exc:
            self.file, self.error = None, exc

    @staticmethod
    def _stamp():
        return time.strftime('%H:%M:%S')

    def say(self, text=''):
        for line in str(text).splitlines() or ['']:
            line = f'[{self._stamp()}] {line}'
            try:
                self.console.write(line + '\n')
                self.console.flush()
            except (OSError, ValueError):
                pass
            self._write(line)

    def detail(self, text):
        for line in str(text).splitlines():
            self._write(f'[{self._stamp()}]   | {line}')

    def _write(self, line):
        if self.file is not None:
            self.file.write(line + '\n')
            self.file.flush()

    def close(self):
        if self.file is not None:
            self.file.close()
            self.file = None


class DetailSink(io.TextIOBase):
    """File-like target for the tools' stdout/stderr: complete lines go to the log only."""

    def __init__(self, log):
        super().__init__()
        self.log, self.pending = log, ''

    def writable(self):
        return True

    def write(self, text):
        self.pending += text
        *lines, self.pending = self.pending.split('\n')
        for line in lines:
            self.log.detail(line)
        return len(text)

    def finish(self):
        if self.pending:
            self.log.detail(self.pending)
            self.pending = ''


@contextlib.contextmanager
def captured(log):
    sink = DetailSink(log)
    try:
        with contextlib.redirect_stdout(sink), contextlib.redirect_stderr(sink):
            yield
    finally:
        sink.finish()


# --- environment ----------------------------------------------------------------------------------

def default_game_dir():
    """The executable's directory when frozen (PyInstaller sets sys.frozen), else the current directory."""
    if getattr(sys, 'frozen', False):
        return Path(sys.executable).resolve().parent
    return Path.cwd()


def has_game_exe(game):
    try:
        return any(p.name.lower() == GAME_EXE and p.is_file() for p in Path(game).iterdir())
    except OSError:
        return False


def running_game():
    """Process lines of a running X3AP.exe; raises RuntimeError when the process table cannot be read.
    Windows: tasklist (CSV, no filter); elsewhere ps through game_guard, which also recognises the game
    started by a wine loader."""
    if sys.platform == 'win32':
        try:
            out = subprocess.run(['tasklist', '/FO', 'CSV', '/NH'], capture_output=True, text=True, timeout=20,
                                 creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        except (OSError, subprocess.SubprocessError) as exc:
            raise RuntimeError(f'tasklist unavailable: {exc}') from None
        if out.returncode != 0:
            raise RuntimeError(f'tasklist exit {out.returncode}')
        rows = [r for r in csv.reader(out.stdout.splitlines()) if r]
        return [','.join(r) for r in rows if r[0].strip().lower() == GAME_EXE]
    import game_guard
    return game_guard.game_running()


def default_jobs():
    return max(1, (os.cpu_count() or 2) - 1)


def host_memory_bytes():
    """Physical RAM: GlobalMemoryStatusEx on Windows (lod_overlay's sysconf query has no Windows form), else
    lod_overlay.host_memory_bytes; None when unknown."""
    if sys.platform != 'win32':
        import lod_overlay
        return lod_overlay.host_memory_bytes()
    import ctypes

    class MemoryStatusEx(ctypes.Structure):
        _fields_ = [('dwLength', ctypes.c_uint32), ('dwMemoryLoad', ctypes.c_uint32)] + \
            [(n, ctypes.c_uint64) for n in ('ullTotalPhys', 'ullAvailPhys', 'ullTotalPageFile', 'ullAvailPageFile',
                                           'ullTotalVirtual', 'ullAvailVirtual', 'ullAvailExtendedVirtual')]
    status = MemoryStatusEx()
    status.dwLength = ctypes.sizeof(status)
    try:
        return status.ullTotalPhys if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)) else None
    except (AttributeError, OSError):
        return None


def lod_jobs(jobs):
    """The LOD bake's worker count: jobs, capped by lod_overlay.default_jobs (CPU count - 2, at most 6) and by
    RAM // WORKER_BYTES - 1 (a worker on the biggest stations peaks near 7 GB), at least 1."""
    import lod_overlay
    n = min(jobs, lod_overlay.default_jobs())
    ram = host_memory_bytes()
    if ram:
        n = min(n, max(1, ram // lod_overlay.WORKER_BYTES - 1))
    return max(1, n)


def wait_for_key(prompt):
    """The console-stays-open wait: one key (msvcrt on Windows, a raw tty read elsewhere), else Enter."""
    try:
        sys.stdout.write(prompt + '\n')
        sys.stdout.flush()
        if sys.platform == 'win32':
            import msvcrt
            msvcrt.getch()
            return
        if sys.stdin is not None and sys.stdin.isatty():
            import termios
            import tty
            fd = sys.stdin.fileno()
            saved = termios.tcgetattr(fd)
            try:
                tty.setraw(fd)
                os.read(fd, 1)
            finally:
                termios.tcsetattr(fd, termios.TCSADRAIN, saved)
            return
        input()
    except (EOFError, OSError, KeyboardInterrupt, ImportError, ValueError, AttributeError):
        pass


# --- steps ----------------------------------------------------------------------------------------

class StepFailed(Exception):
    pass


def call_tool(log, main, argv):
    """Run a tool's main(argv) with its output in the log; a SystemExit with a message or a nonzero code
    becomes StepFailed (the tools refuse that way); other exceptions propagate."""
    try:
        with captured(log):
            code = main(argv)
    except SystemExit as exc:
        if exc.code in (0, None):
            return 0
        raise StepFailed(f'exit code {exc.code}' if isinstance(exc.code, int) else str(exc.code)) from None
    return code or 0


REPLACED_SUFFIX = '.x3m-replaced'   # lod_overlay.REPLACED_SUFFIX: a previous file moved aside during a write


def recover_interrupted(log, game):
    """Undo a write that was killed between moving the previous overlay aside and finishing (lod_overlay
    commit_outputs / commit_package): every addon/*.x3m-replaced and addon/mods/*.x3m-replaced file goes back
    over its original name, replacing a partial new file, which is exactly what their in-process rollback does.
    Returns the restored names; raises OSError when a file cannot be put back."""
    restored = []
    for folder in (game / 'addon', game / 'addon' / 'mods'):
        if not folder.is_dir():
            continue
        for aside in sorted(folder.glob('*' + REPLACED_SUFFIX)):
            target = aside.with_name(aside.name[:-len(REPLACED_SUFFIX)])
            os.replace(aside, target)
            restored.append(target)
    for cat in sorted({t.with_name(t.name.split('.')[0] + '.cat') for t in restored}):
        log.say(f'recovered interrupted write of {cat.relative_to(game).as_posix()}')
    return restored


def package_layer(game, name):
    """The selected package's catalogue as an extra fog layer: addon/mods/<name>.cat, or for a selected derived
    copy (<src>-x3m-lod) its source package named in the copy's marker; None when no such file exists."""
    import lod_overlay_check
    if not name:
        return None
    if name.endswith(lod_overlay_check.PACKAGE_SUFFIX):
        try:
            src = json.loads(lod_overlay_check.package_files(game, name)[2].read_text())['package']
            cat = game / 'addon' / 'mods' / f'{src}.cat'
            if cat.is_file() and cat.with_suffix('.dat').is_file():
                return cat
        except (OSError, ValueError, KeyError, TypeError):
            pass
    cat = game / 'addon' / 'mods' / f'{name}.cat'
    return cat if cat.is_file() and cat.with_suffix('.dat').is_file() else None


def fog_step(log, game, jobs, package_cat=None):
    import fog_families as ff
    log.say('fog families: reading TBackgrounds and the nebula catalogues'
            + (f' (with the selected package {package_cat.relative_to(game).as_posix()})' if package_cat else ''))

    def progress(name, done, total, result):
        what = ('refused: ' + result['refusal']) if 'refusal' in result else \
            ('baked' if 'packet' in result else 'palette checked (built-in profile)')
        log.say(f'processing fog {name} ({done}/{total}) {what}')
    ff.progress = progress
    try:
        code = call_tool(log, ff.main, ['--game', str(game), '--install', '--replace', '--jobs', str(jobs)]
                         + (['--mod-cat', str(package_cat)] if package_cat else []))
    finally:
        ff.progress = None
    if code:
        raise StepFailed(f'fog_families exit code {code}')
    record = json.loads((game / 'x3m' / ff.RECORD_NAME).read_text())
    log.say('fog layers: ' + ', '.join(record['catalogue_layers']))
    for fam in record['families']:
        if fam['status'] == 'refused':
            log.say(f"refused fog {fam['name']}: {fam['reason']}")
    counts = record['counts']
    refused = sum(n for k, n in counts.items() if k.startswith('refused'))
    f = record['file']
    log.say(f"fog summary: {f['families']} families added ({f['packets']} packets, {f['bytes']} B),"
            f" {counts.get('covered_by_build', 0)} covered by the built-in profiles, {refused} refused;"
            f" wrote x3m/{ff.FILE_NAME}")


def selected_mod(game):
    """(ModName, where): '' no package, None unknown (lod_overlay_check.read_mod_name: winreg on Windows,
    the bottle's user.reg under a CrossOver drive_c)."""
    import lod_overlay_check
    return lod_overlay_check.read_mod_name(None, game)


def lod_step(log, game, jobs, name, where):
    import lod_overlay
    if name is None:
        log.say(f'selected mod: unknown ({where}); the LOD overlay is baked without a mod package')
    elif not name:
        log.say(f'selected mod: none ({where})')
    else:
        log.say(f'selected mod: {name} ({where}); its changed bodies go into addon/mods/{name}-x3m-lod')
    n_jobs = lod_jobs(jobs)
    log.say(f'LOD overlay: scanning the ship and station bodies, then baking with {n_jobs} job(s)')

    def progress(body, done, total, package, result):
        log.say(f'processing model {body} ({done}/{total})' + (f' [mod {package}]' if package else ''))
        if 'refused' in result:
            first = (result['refused'].splitlines() or [''])[0][:160]
            log.say(f"refused model {body}: {lod_overlay.bake_reason(result['refused'])} ({first})")
    lod_overlay.progress = progress
    try:
        code = call_tool(log, lod_overlay.main, ['--game', str(game), '--batch', '--sync', '--mod', 'auto',
                                                 '--install', '--jobs', str(n_jobs)])
    finally:
        lod_overlay.progress = None
    if code:
        raise StepFailed(f'lod_overlay exit code {code}')
    record = json.loads((game / 'addon' / 'x3m-lod-batch.json').read_text())
    for b in record['bodies']:                 # bake refusals were printed as they happened
        before_bake = [r for r in b['refuse'] if not r.startswith('bake:')]
        if before_bake:
            log.say(f"refused model {b['name']}: {', '.join(before_bake)}")
    for note in record.get('notes', ()):
        if note.startswith(('--mod auto', 'package ', 'warning: addon/mods', 'previous overlay', 'orphaned marker')):
            log.say(note)
    slots = '; '.join(f"addon/{s['slot']:02d} {s['bodies']} bodies {s['bytes']} B" for s in record['slots'])
    slots += ''.join(f'; addon/{s:02d} retired (empty catalogue)' for s in record['retired_slots'])
    slots += ''.join(f'; addon/{s:02d} removed' for s in record['removed_slots'])
    log.say(f'slot plan: {slots}')
    c = record['counts']
    refused = sum(1 for b in record['bodies'] if b['refuse'])
    pkg = record.get('package')
    log.say(f"LOD summary: {c['enumerated']} bodies found, {c['eligible']} eligible, {c['built']} baked +"
            f" {c['reused']} unchanged = {c['overlay_bodies']} in the overlay, {refused} refused"
            + (f"; mod {pkg['name']}: {pkg['built']} baked + {pkg['reused']} unchanged" if pkg else ''))


def fog_check_step(log, game):
    import fog_families as ff
    try:
        code = call_tool(log, ff.main, ['--game', str(game), '--check'])
    except StepFailed as exc:
        code = str(exc)
    if code:
        log.say(f'warning: the fog families check after the LOD overlay did not pass ({code}); details in the log')
    else:
        log.say('fog families: check passed after the LOD overlay (launch fingerprint current)')


# --- driver ---------------------------------------------------------------------------------------

def run(game, jobs, log):
    """Every step; returns the names of the failed steps."""
    started = time.monotonic()
    log.say(f'x3m-regenerate: game directory {game}')
    log.say(f'log file {log.path}' + (f' (cannot be written: {log.error})' if log.error else ''))
    if not has_game_exe(game):
        log.say(f'refused: {game} holds no X3AP.exe; put x3m-regenerate next to X3AP.exe and start it there')
        return ['setup']
    try:
        lines = running_game()
    except RuntimeError as exc:
        log.say(f'refused: cannot tell whether the game is running ({exc})')
        return ['setup']
    if lines:
        log.say(f'refused: the game is running ({lines[0]}); quit X3 and start x3m-regenerate again')
        return ['setup']
    try:
        import fog_families
        import lod_overlay
    except Exception:
        log.say('failed: the bundled tools cannot be loaded')
        log.say(traceback.format_exc().rstrip())
        return ['setup']
    fog_families.running_game = running_game       # both tools check again right before they write
    lod_overlay.running_game = running_game
    failed = []
    try:
        recover_interrupted(log, game)
    except OSError as exc:                         # the LOD step then refuses with the tool's own message
        log.say(f'warning: could not recover an interrupted overlay write ({exc})')
    name, where = selected_mod(game)
    package_cat = package_layer(game, name)
    for label, step in (('fog families', lambda: fog_step(log, game, jobs, package_cat)),
                        ('LOD overlay', lambda: lod_step(log, game, jobs, name, where))):
        log.say(f'== {label}')
        t0 = time.monotonic()
        try:
            step()
            log.say(f'{label}: done in {time.monotonic() - t0:.0f} s')
        except StepFailed as exc:
            failed.append(label)
            log.say(f'{label}: FAILED: {exc}')
        except Exception:
            failed.append(label)
            log.say(f'{label}: FAILED with an unexpected error:')
            log.say(traceback.format_exc().rstrip())
    if not failed:
        log.say('== fog families check')
        fog_check_step(log, game)
    minutes = (time.monotonic() - started) / 60
    if failed:
        log.say(f'FAILED: {", ".join(failed)} after {minutes:.1f} min; a failed step keeps its previous results'
                f' (details above and in {LOG_NAME})')
    else:
        log.say(f'all done: fog families and LOD overlay regenerated in {minutes:.1f} min')
    return failed


def parse_args(argv):
    ap = argparse.ArgumentParser(prog='x3m-regenerate', description=__doc__.split('\n\n')[0])
    ap.add_argument('--game-dir', type=Path, help='game directory (default: this executable\'s directory when'
                    ' bundled, else the current directory)')
    ap.add_argument('--jobs', type=int, default=default_jobs(), help='parallel jobs (default: CPU count - 1)')
    ap.add_argument('--no-wait', action='store_true', help='do not wait for a key at the end')
    a = ap.parse_args(argv)
    if a.jobs < 1:
        ap.error('--jobs must be >= 1')
    return a


def main(argv=None):
    args = parse_args(sys.argv[1:] if argv is None else argv)
    game = (args.game_dir or default_game_dir()).resolve()
    console = sys.stdout
    with contextlib.suppress(AttributeError, ValueError, OSError):
        console.reconfigure(errors='replace')
    log = Log(game / LOG_NAME, console)
    try:
        failed = run(game, args.jobs, log)
    except BaseException:                     # KeyboardInterrupt included: still logged, still waits
        log.say('FAILED: interrupted or unexpected error:')
        log.say(traceback.format_exc().rstrip())
        failed = ['interrupted']
    finally:
        log.close()
    if not args.no_wait:
        wait_for_key('Done. Press any key to close this window.' if not failed else
                     f'Finished with errors ({", ".join(failed)}); see {LOG_NAME}. Press any key to close this window.')
    return 1 if failed else 0


if __name__ == '__main__':
    multiprocessing.freeze_support()       # frozen: spawned bake workers enter here and are dispatched
    sys.exit(main())
