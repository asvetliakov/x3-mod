#!/usr/bin/env python3
"""Per-cell scan and counts of docs/verification/launcher-options-inventory.md (host only).

For every table row of sections 1-5: the cell count (split on `|` outside backticks; must equal the cell count of its
table's header row: 4 everywhere except the 3-column promoted-set sub-table of section 1), a `|` inside backticks (must be
absent), and per section the tables, the table rows and the distinct `--option` names in the first cells.

    python3 verification/results/logging-tiers/inventory_scan.py
"""
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[3]
DOC = ROOT / 'docs/verification/launcher-options-inventory.md'


def cells(line):
    out, cur, tick = [], '', False
    for ch in line.strip()[1:-1]:
        if ch == '`':
            tick = not tick
        if ch == '|' and not tick:
            out.append(cur)
            cur = ''
        else:
            cur += ch
    out.append(cur)
    return out


def main():
    section, result, width, previous = None, {}, None, ''
    for number, line in enumerate(DOC.read_text().splitlines(), 1):
        heading = re.match(r'## (\d)\. ', line)
        if heading:
            section = int(heading.group(1))
        elif line.startswith('## '):
            section = None
        header = line.startswith('|') and not previous.startswith('|')
        previous = line
        if section is None or not line.startswith('|') or line.startswith('|---'):
            continue
        entry = result.setdefault(section, {'tables': 0, 'rows': 0, 'names': set(), 'bad_cells': [], 'pipe_in_backticks': []})
        if header:
            width = len(cells(line))
            entry['tables'] += 1
            continue
        entry['rows'] += 1
        row = cells(line)
        if len(row) != width:
            entry['bad_cells'].append((number, len(row)))
        if any('|' in span for span in re.findall(r'`[^`]*`', line)):
            entry['pipe_in_backticks'].append(number)
        entry['names'] |= set(re.findall(r'--[a-z0-9][a-z0-9-]*', row[0]))
    print(json.dumps({f'section_{k}': {'tables': v['tables'], 'rows': v['rows'], 'option_names': len(v['names']), 'bad_cells': v['bad_cells'],
                                       'pipe_in_backticks': v['pipe_in_backticks']} for k, v in sorted(result.items())}, indent=1))


if __name__ == '__main__':
    main()
