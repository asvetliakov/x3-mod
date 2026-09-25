#!/usr/bin/env python3
"""Counts behind the logging tiers in docs/verification/launcher-options-inventory.md, section 2 (host only, no Wine).

Parses the `launch` parser of tools/manage.py at BASE (9a668e81, before the tiers) and in the working tree (the parser
capture of ../launcher-defaults/removed_options_2026-09-25.py) and prints the registered option strings, the suppressed
refusal stubs, the removed and added names, and which of the launcher's TIERED_VARIABLES the DLL sources still read.

    python3 verification/results/logging-tiers/options_tiers.py
"""
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
BASE = '9a668e81'


def main():
    os.chdir(ROOT)
    sys.path.insert(0, str(ROOT))
    sys.path.insert(0, str(ROOT / 'tools'))
    spec = importlib.util.spec_from_file_location('removed_options', HERE.parent / 'launcher-defaults/removed_options_2026-09-25.py')
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    base_text = subprocess.run(['git', 'show', f'{BASE}:tools/manage.py'], capture_output=True, text=True, check=True).stdout
    _, before, before_stubs = helper.parser_of(base_text)
    module, after, after_stubs = helper.parser_of((ROOT / 'tools/manage.py').read_text())
    text = ''
    for folder in ('src/proxy', 'src/ownership', 'src/renderer'):
        for path in sorted((ROOT / folder).iterdir()):
            if path.suffix in ('.cpp', '.h'):
                text += path.read_text(errors='replace')
    reads = set(re.findall(r'L"(X3M_[A-Z0-9_]+)"', text))
    removed = sorted((before - after) | ((after_stubs - before_stubs) & before))
    result = dict(base=BASE, registered_before=len(before), registered_after=len(after), suppressed_stubs_after=sorted(after_stubs),
                  removed_or_stubbed=removed, removed_count=len(removed), added=sorted(after - before),
                  tiered_variables=len(module.TIERED_VARIABLES),
                  tiered_not_read_by_dll=[v for v in module.TIERED_VARIABLES if v not in reads])
    print(json.dumps(result, indent=1))


if __name__ == '__main__':
    main()
