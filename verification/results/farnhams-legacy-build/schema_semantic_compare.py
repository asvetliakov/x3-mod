#!/usr/bin/env python3
"""Semantic compare of two generated src/config/config_schema_inc.h versions (default: HEAD vs working tree).

The table stores interval/element-range indices, so one inserted entry renumbers hundreds of lines; this
resolves every entry to its meaning (env, key, type, default, off value, the interval values, the per-element
ranges, count mask, choices, flags, the marker's target key) and reports added, removed and changed keys.
usage: schema_semantic_compare.py [OLD_REF_OR_PATH] [NEW_PATH]
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
HEADER = 'src/config/config_schema_inc.h'


def load(spec):
    path = Path(spec)
    if path.exists():
        return path.read_text()
    return subprocess.run(['git', '-C', str(ROOT), 'show', f'{spec}:{HEADER}'], capture_output=True, text=True, check=True).stdout


def block(text, name):
    body = re.search(rf'{name}\[[^\]]*\] = \{{\n(.*?)\n\}};', text, re.S).group(1)
    return [line.strip().rstrip(',') for line in body.split('\n') if line.strip().startswith('{')]


def resolve(text):
    intervals = [tuple(x.strip() for x in row.strip('{}').split(',')) for row in block(text, 'intervals')]
    elements = [tuple(int(x) for x in row.strip('{}').split(',')) for row in block(text, 'element_ranges')]
    rows = block(text, 'entries')
    keys = [re.match(r'\{"([^"]*)"', row).group(1) for row in rows]
    out = {}
    for row in rows:
        fields = re.findall(r'"(?:[^"\\]|\\.)*"|[^,{}\s][^,{}]*', row)
        env, key, type_, default, off = fields[0], fields[1], fields[2], fields[3], fields[4]
        range_first, range_count, element_first, element_count, count_mask = (int(f) for f in fields[5:10])
        choices, flags, marker_of = fields[10], int(fields[11]), int(fields[12])
        out[env] = dict(key=key, type=type_, default=default, off=off,
                        range=intervals[range_first:range_first + range_count],
                        elements=[intervals[f:f + c] for f, c in elements[element_first:element_first + element_count]],
                        count_mask=count_mask, choices=choices, flags=flags,
                        marker_of=keys[marker_of] if marker_of >= 0 else None)
    return out


def main():
    old_spec = sys.argv[1] if len(sys.argv) > 1 else 'HEAD'
    new_spec = sys.argv[2] if len(sys.argv) > 2 else str(ROOT / HEADER)
    old, new = resolve(load(old_spec)), resolve(load(new_spec))
    added = sorted(set(new) - set(old))
    removed = sorted(set(old) - set(new))
    changed = sorted(k for k in set(old) & set(new) if old[k] != new[k])
    print(f'entries {len(old)}->{len(new)} added={len(added)} removed={len(removed)} changed={len(changed)}')
    for k in added:
        print(f'  added {k} {new[k]}')
    for k in removed:
        print(f'  removed {k}')
    for k in changed:
        print(f'  changed {k}\n    old {old[k]}\n    new {new[k]}')
    return 1 if removed or changed else 0


if __name__ == '__main__':
    sys.exit(main())
