"""The proxy's session log for fixture runs (docs/architecture/logging-tiers.md, "Log file policy").

Since the logging tiers the proxy writes <module dir>\\x3m.log; a fixture runner that reads the log from
<fixture dir>/x3-modern-captures/session-*.log sets X3M_LOG_FILE to a fresh session-named path there, so the
runner's reading and the snapshot naming stay as they were, and the production open path (CreateFileW on the
override) stays under test.

Since the settings file (docs/architecture/config-file.md) the proxy's built-in defaults are the launcher's promoted
set; the fixtures run it with X3M_CONFIG=bare (no x3m.ini, no defaults: an absent variable is off, as before).
wine_lock.py sets it for every command under the lock; session_log_env returns it too, for the runners that start from
an environment stripped of X3M_* (a runner that tests the file itself sets its own X3M_CONFIG after this helper).
"""
import datetime
import os
from pathlib import Path

CONFIG_BARE = {'X3M_CONFIG': 'bare'}


def session_log_env(directory):
    """{'X3M_LOG_FILE': 'Z:<directory>/x3-modern-captures/session-<stamp>-<pid>.log', 'X3M_CONFIG': 'bare'} for one
    fixture process."""
    captures = Path(directory) / 'x3-modern-captures'
    captures.mkdir(parents=True, exist_ok=True)
    name = f'session-{datetime.datetime.now():%Y%m%d-%H%M%S}-{os.getpid()}.log'
    return {'X3M_LOG_FILE': 'Z:' + str(captures.resolve() / name), **CONFIG_BARE}
