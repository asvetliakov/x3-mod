#!/usr/bin/env python3
"""`tools/manage.py launch --dry-run --ship-steering` on a host without the CrossOver Wine binary: the launcher's
WINE constant is patched to an empty file and a hermetic game directory is passed, exactly as
verification/analysis/test_launcher_defaults.py does (nothing is launched; subprocess.call is refused). Prints the
X3M_SHIP_STEERING* variables of the dry-run environment for the plain, the class-list and the default launch.

  python3 verification/results/ship-steering/dry_run.py
"""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import sys
import tempfile
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))          # `from tools import media_package`
sys.path.insert(0, str(ROOT / 'tools'))
spec = importlib.util.spec_from_file_location('manage_dry_run', ROOT / 'tools/manage.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def dry_run(directory, *args):
    game = Path(directory) / 'game'
    if not game.exists():
        game.mkdir()
        (game / 'X3AP.exe').touch()
        (game / 'd3d9.dll').write_bytes(b'proxy')
        (game / 'x3-modern-install.json').write_text(json.dumps({'sha256': hashlib.sha256(b'proxy').hexdigest()}))
    wine = Path(directory) / 'wine'
    wine.touch()
    argv = ['manage.py', 'launch', '--dry-run', '--game-dir', str(game), *args]
    environ = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
    out, err = io.StringIO(), io.StringIO()
    with mock.patch.object(sys, 'argv', argv), mock.patch.object(module, 'WINE', wine), \
            mock.patch.object(module, 'VOICE_DECODER_REPO', None), mock.patch.dict(module.os.environ, environ, clear=True), \
            mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), \
            contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            module.main()
        except SystemExit as e:
            return e.code, None, err.getvalue().strip().splitlines()[-1] if err.getvalue().strip() else ''
    env = json.loads(out.getvalue())['env']
    return 0, {k: v for k, v in env.items() if k.startswith('X3M_SHIP_STEERING')}, len([k for k in env if k.startswith('X3M_')])


with tempfile.TemporaryDirectory(prefix='x3-ship-steering-dry-') as d:
    for args in (('--ship-steering',), ('--ship-steering', '--ship-steering-classes', 'm1,M2,tl'), (),
                 ('--vanilla', '--ship-steering'), ('--ship-steering', '--ship-steering-classes', 'M9'), ('--ship-steering-classes', 'M1')):
        code, env, extra = dry_run(d, *args)
        print(f'launch --dry-run {" ".join(args):50s} exit={code} {env if code == 0 else extra} {"X3M_* count=" + str(extra) if code == 0 else ""}')
