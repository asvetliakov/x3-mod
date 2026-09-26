#!/usr/bin/env python3
"""Settings file (docs/architecture/config-file.md, 2026-09-26): every X3M_* variable named in sections 1, 2, 3 and 5 of
docs/verification/launcher-options-inventory.md is a key of tools/config/schema.py; prints the counts per section, how
many are user-facing (in the template) or developer / environment-only, and exits 1 on a variable the schema lacks.
Section 4 (removed) is skipped. Brace forms (`X3M_SHADOW_CASCADE*`, `X3M_HDR_*`, `_STEP/_COLDFILL`) are family names, not
variables: only complete names are checked; a name no source reads (a stale inventory entry) is listed, not failed.

    python3 verification/results/config-file/inventory_keys.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/config'))
import schema  # noqa: E402

INVENTORY = ROOT / 'docs/verification/launcher-options-inventory.md'


def sections(text):
    current, found = None, {}
    for line in text.splitlines():
        match = re.match(r'## (\d)\. ', line)
        if match:
            current = match.group(1)
        elif current:
            found.setdefault(current, []).append(line)
    return found


def read_by_dll(name):
    return any(f'"{name}"' in path.read_text(errors='replace') for path in (ROOT / 'src').rglob('*') if path.suffix in ('.cpp', '.h'))


def main():
    parts = sections(INVENTORY.read_text())
    missing, report = [], {}
    for number in ('1', '2', '3', '5'):
        names = set(re.findall(r'X3M_[A-Z0-9_]*[A-Z0-9]\b(?![*<])', '\n'.join(parts.get(number, []))))
        names = {n for n in names if not n.endswith('_') and n not in ('X3M_',)}
        known = {n for n in names if n in schema.BY_ENV}
        family = {n for n in names - known if any(e.startswith(n + '_') for e in schema.BY_ENV)}  # a prefix such as X3M_HDR
        lacking = sorted(n for n in names - known - family if n != 'X3M_MOTION_OUTPUT_FIXTURE')  # a compile definition, not a variable
        stale = [n for n in lacking if not read_by_dll(n)]  # named by the inventory, read by no source: no key needed
        lacking = [n for n in lacking if n not in stale]
        if stale:
            print(f'section {number}: not read by the DLL (inventory entry only): {stale}')
        missing += lacking
        user = sum(not schema.BY_ENV[n]['developer'] for n in known)
        env_only = sum(schema.BY_ENV[n]['env_only'] for n in known)
        report[number] = (len(names), len(known), user, len(known) - user - env_only, env_only, sorted(family), lacking)
    for number, (names, known, user, developer, env_only, family, lacking) in report.items():
        print(f'section {number}: {names} names, {known} schema keys ({user} user-facing, {developer} developer, {env_only} environment-only), '
              f'family prefixes {family}, missing {lacking}')
    print('PASS' if not missing else 'FAIL', f'{len(schema.SETTINGS)} schema keys, {len(schema.user_facing())} user-facing')
    return 1 if missing else 0


if __name__ == '__main__':
    raise SystemExit(main())
