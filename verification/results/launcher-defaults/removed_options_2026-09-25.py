#!/usr/bin/env python3
"""Counts behind "Removed 2026-09-25" in docs/verification/launcher-options-inventory.md (host only, no Wine, no launch).

Parses the `launch` parser of tools/manage.py at BASE (ae16da06, before the removal) and in the working tree, and prints
the registered option strings, the suppressed refusal stubs, the removed names, and which of the launcher's
REMOVED_VARIABLES the DLL sources (src/proxy, src/ownership, src/renderer) still read.

    python3 verification/results/launcher-defaults/removed_options_2026-09-25.py
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
BASE = 'ae16da06'


def parser_of(source_text):
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / 'manage_snapshot.py'
        path.write_text(source_text)
        spec = importlib.util.spec_from_file_location('manage_snapshot', path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        captured = {}
        real = argparse.ArgumentParser.parse_args

        def grab(self, *args, **kwargs):
            captured['parser'] = self
            raise SystemExit(0)
        argparse.ArgumentParser.parse_args = grab
        argv = sys.argv
        try:
            sys.argv = ['manage.py', 'launch', '--dry-run']
            try:
                module.main()
            except SystemExit:
                pass
        finally:
            argparse.ArgumentParser.parse_args = real
            sys.argv = argv
    names, suppressed = set(), set()
    for action in captured['parser']._actions:
        for option in action.option_strings:
            if option.startswith('--'):
                names.add(option)
                if action.help == argparse.SUPPRESS:
                    suppressed.add(option)
    return module, names, suppressed


def main():
    os.chdir(ROOT)
    sys.path.insert(0, str(ROOT))
    base_text = subprocess.run(['git', 'show', f'{BASE}:tools/manage.py'], capture_output=True, text=True, check=True).stdout
    _, before, before_stubs = parser_of(base_text)
    module, after, after_stubs = parser_of((ROOT / 'tools/manage.py').read_text())
    text = ''
    for folder in ('src/proxy', 'src/ownership', 'src/renderer'):
        for path in sorted((ROOT / folder).iterdir()):
            if path.suffix in ('.cpp', '.h'):
                text += path.read_text(errors='replace')
    reads = set(re.findall(r'L"(X3M_[A-Z0-9_]+)"', text)) | set(re.findall(r'GetEnvironmentVariableA\("(X3M_[A-Z0-9_]+)"', text))
    removed = sorted(before - after)
    result = dict(base=BASE, registered_before=len(before), registered_after=len(after), suppressed_stubs_before=sorted(before_stubs),
                  suppressed_stubs_after=sorted(after_stubs), removed_count=len(removed), removed=removed, added=sorted(after - before),
                  removed_variables=len(module.REMOVED_VARIABLES),
                  removed_variables_still_read_by_dll=[v for v in module.REMOVED_VARIABLES if v in reads],
                  removed_variables_not_read=[v for v in module.REMOVED_VARIABLES if v not in reads],
                  alias_and_leftover_reads={v: v in reads for v in ('X3M_HDR_EV_OFFSET', 'X3M_VOLUMETRIC_FOG_ANISOTROPY')})
    print(json.dumps(result, indent=1))


if __name__ == '__main__':
    main()
