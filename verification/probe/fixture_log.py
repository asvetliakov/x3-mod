"""The proxy's session log for fixture runs (docs/architecture/logging-tiers.md, "Log file policy").

Since the logging tiers the proxy writes <module dir>\\x3m.log; a fixture runner that reads the log from
<fixture dir>/x3-modern-captures/session-*.log sets X3M_LOG_FILE to a fresh session-named path there, so the
runner's reading and the snapshot naming stay as they were, and the production open path (CreateFileW on the
override) stays under test.
"""
import datetime
import os
from pathlib import Path


def session_log_env(directory):
    """{'X3M_LOG_FILE': 'Z:<directory>/x3-modern-captures/session-<stamp>-<pid>.log'} for one fixture process."""
    captures = Path(directory) / 'x3-modern-captures'
    captures.mkdir(parents=True, exist_ok=True)
    name = f'session-{datetime.datetime.now():%Y%m%d-%H%M%S}-{os.getpid()}.log'
    return {'X3M_LOG_FILE': 'Z:' + str(captures.resolve() / name)}
