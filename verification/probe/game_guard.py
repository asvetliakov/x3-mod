"""Detect a running X3AP game process without tripping on analysis tooling.

The runners refuse to start a synthetic GPU fixture while the game is up. A
plain `pgrep -ifl X3AP.exe` also matches a Ghidra headless session
(`... AnalyzeHeadless ... -process X3AP.exe ...`), so this module inspects the
process table and keeps only real game processes:

- a process whose command is `X3AP.exe` or `X3AP` (Wine rewrites argv to the
  Windows command line, so the game shows as `C:\\X3\\X3AP.exe ...`), or
- a `wine` / `wine64` / `wine-preloader` / `wine64-preloader` /
  `winewrapper.exe` process whose first non-option argument ends in
  `X3AP.exe` (value-taking loader options such as `--bottle Steam` are
  consumed with their value; `--` ends the option list).

Lines mentioning `java`, `ghidra`, `AnalyzeHeadless` or `pgrep` are ignored.
"""
import subprocess

GAME_COMMANDS = ('x3ap.exe', 'x3ap')
WINE_COMMANDS = ('wine', 'wine64', 'wine-preloader', 'wine64-preloader', 'winewrapper.exe', 'wineloader')
VALUE_OPTIONS = ('--bottle', '--dll', '--workdir', '--cx-app', '--wl-app', '--ux-app', '--desktop', '--scope',
                 '--wait-child', '--cx-log', '--check-dll', '--enable-alt-loader')
IGNORED_WORDS = ('java', 'ghidra', 'analyzeheadless', 'pgrep')


def _basename(token):
    return token.replace('\\', '/').rsplit('/', 1)[-1].lower()


def is_game_line(line):
    """True when one `ps -o pid=,args=` line describes a real game process."""
    lowered = line.lower()
    if any(word in lowered for word in IGNORED_WORDS):
        return False
    fields = line.split()
    if len(fields) < 2:
        return False
    argv = fields[1:] if fields[0].isdigit() else fields
    # Command paths may contain spaces (`/Applications/CrossOver Preview.app/...`):
    # grow the command token by token until it names a known executable or an
    # option starts the argument list.
    for count in range(1, len(argv) + 1):
        if count > 1 and argv[count - 1].startswith('-'):
            return False
        command = _basename(' '.join(argv[:count]))
        if command in GAME_COMMANDS:
            return True
        if command in WINE_COMMANDS:
            break
    else:
        return False
    rest = iter(argv[count:])
    for token in rest:
        if token == '--':
            token = next(rest, None)
            return token is not None and _basename(token) in GAME_COMMANDS
        if token in VALUE_OPTIONS:
            next(rest, None)
            continue
        if token.startswith('-') or _basename(token) in WINE_COMMANDS:
            continue  # option, or the loader named by a preloader
        return _basename(token) in GAME_COMMANDS
    return False


def game_lines(text):
    return [line.strip() for line in text.splitlines() if line.strip() and is_game_line(line)]


def game_running():
    """Return the process-table lines of real game processes (empty when none).

    Raises RuntimeError when the process inventory cannot be read, so callers
    keep refusing on an unknown state.
    """
    try:
        inventory = subprocess.run(['ps', '-axww', '-o', 'pid=,args='], capture_output=True, text=True, timeout=10)
    except (OSError, subprocess.SubprocessError) as error:
        raise RuntimeError('process inventory unavailable: ' + str(error))
    if inventory.returncode != 0:
        raise RuntimeError('process inventory unavailable: ps exit ' + str(inventory.returncode))
    return game_lines(inventory.stdout)
