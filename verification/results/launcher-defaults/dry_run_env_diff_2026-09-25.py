#!/usr/bin/env python3
"""X3M_* environment of the default modded and the vanilla dry run: tools/manage.py at BASE (ae16da06, the Run89 launcher
defaults) against the working tree, both loaded in-process from the same checkout (host only, never launches).

    python3 verification/results/launcher-defaults/dry_run_env_diff_2026-09-25.py

Runs `launch --bottle X3 --direct --dry-run` and `launch --bottle X3 --vanilla --dry-run` through both launchers with an
environment free of inherited X3M_* variables and prints, per launch, the variables only BASE sends, only the tree sends,
and those whose value differs.
"""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest import mock

ROOT = Path(__file__).resolve().parents[3]
BASE = 'ae16da06'
LAUNCHES = {'direct': ['launch', '--bottle', 'X3', '--direct', '--dry-run'], 'vanilla': ['launch', '--bottle', 'X3', '--vanilla', '--dry-run']}


def dry_run_env(path, argv):
    spec = importlib.util.spec_from_file_location('manage_dry_' + path.stem, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    clean = {k: v for k, v in os.environ.items() if not k.startswith('X3M_')}
    output = io.StringIO()
    with mock.patch.dict(os.environ, clean, clear=True), mock.patch.object(sys, 'argv', ['manage.py', *argv]), \
            mock.patch.object(module.subprocess, 'call', side_effect=AssertionError('must never launch')), contextlib.redirect_stdout(output):
        try:
            module.main()
        except SystemExit as exit:
            assert exit.code in (0, None), exit.code
    return {k: v for k, v in json.loads(output.getvalue())['env'].items() if k.startswith('X3M_')}


def main():
    os.chdir(ROOT)
    sys.path.insert(0, str(ROOT))
    base_text = subprocess.run(['git', 'show', f'{BASE}:tools/manage.py'], capture_output=True, text=True, check=True).stdout
    result = {}
    # The base copy goes to a temporary directory (never into the checkout); its ROOT line is rewritten to this checkout, so
    # every repository path it derives (voice decoder, fog families, overlay) is the same one the tree's launcher uses, and the
    # checkout's tools/ is on the import path for its sibling modules.
    root_line = 'ROOT = Path(__file__).resolve().parents[1]'
    assert base_text.count(root_line) == 1, 'the base launcher no longer derives ROOT from its path'
    base_text = base_text.replace(root_line, f'ROOT = Path({str(ROOT)!r})')
    sys.path.insert(0, str(ROOT / 'tools'))
    with tempfile.TemporaryDirectory() as directory:
        base_path = Path(directory) / 'manage_base_dry_run.py'
        base_path.write_text(base_text)
        for name, argv in LAUNCHES.items():
            before, after = dry_run_env(base_path, argv), dry_run_env(ROOT / 'tools/manage.py', argv)
            result[name] = dict(variables_before=len(before), variables_after=len(after),
                                only_before={k: before[k] for k in sorted(set(before) - set(after))},
                                only_after={k: after[k] for k in sorted(set(after) - set(before))},
                                changed={k: [before[k], after[k]] for k in sorted(set(before) & set(after)) if before[k] != after[k]})
    print(json.dumps(result, indent=1))


if __name__ == '__main__':
    main()
