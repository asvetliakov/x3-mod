"""Loaded only in a project runner that wine_lock.py started (child_environment puts this directory on PYTHONPATH).

Every subprocess the runner starts with an argument list naming CrossOver's wine wrapper gets `winedbg.exe=d` in its
`--dll` value (wine_lock.with_winedbg_disabled): a faulting fixture then exits with a non-zero status instead of
starting `winedbg --auto`. Any other command, and a string command, is passed through unchanged. The wrapper deletes
PYTHONPATH before it starts Wine, so nothing here reaches the Windows side.
"""
import subprocess
import sys
from pathlib import Path

try:
    sys.path.append(str(Path(__file__).resolve().parents[1]))
    from wine_lock import with_winedbg_disabled
except Exception:  # noqa: BLE001 - never break the runner over a missing helper
    with_winedbg_disabled = None

if with_winedbg_disabled is not None:
    _original_init = subprocess.Popen.__init__

    def _init(self, args, *rest, **options):
        if isinstance(args, (list, tuple)):
            changed = with_winedbg_disabled(list(args))
            if changed != list(args):
                args = changed
        _original_init(self, args, *rest, **options)

    subprocess.Popen.__init__ = _init
