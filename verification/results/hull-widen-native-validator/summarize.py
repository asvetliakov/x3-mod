#!/usr/bin/env python3
"""Fold native_validate.exe output files into result.json (names, verdicts, messages only).
usage: summarize.py out.json native_output.txt..."""
import collections, json, re, sys
rows = []
for path in sys.argv[2:]:
    cur = None
    for line in open(path):
        m = re.search(r'([0-9a-f]{16})-(\w+)\.bin words=(\d+)', line)
        if m:
            cur = {'original': m.group(1), 'kind': m.group(2), 'words': int(m.group(3)), 'messages': []}; rows.append(cur); continue
        m = re.search(r'msg instr=(-?\d+) id=(\d+): (.*)', line)
        if m: cur['messages'].append({'instruction_call': int(m.group(1)), 'id': int(m.group(2)), 'text': m.group(3).strip()})
        m = re.search(r'end=(\w+) messages=(\d+)', line)
        if m: cur['end'] = m.group(1)
tally = collections.Counter((r['kind'], r['end'], len(r['messages'])) for r in rows)
json.dump({'validator': 'SysWOW64 d3d9.dll Direct3DShaderValidatorCreate9 (native Windows host, no device)',
           'parameters': 'hull: fill 0.01, gain 2, depth 1, K 4, B 4 at HEAD d4ae9c5a; widen = far-fade dynamic gain, swiden = static gain; '
                         'fixed/sfixed = patch_one_port.py applied; headsgain/headswidenshare = sun-share gained/widened at HEAD; '
                         'wtswidenshare = sun-share widened from the uncommitted one-port fix in the checkout; '
                         'additive/flag/flagfixed = screen emission ec1f5c4a2f4e1445 gain 2 (flagfixed = patch_screen_flag_oc1.py)',
           'tally': [{'kind': k, 'end': e, 'messages': n, 'programs': c} for (k, e, n), c in sorted(tally.items())],
           'rows': rows}, open(sys.argv[1], 'w'), indent=1)
for (k, e, n), c in sorted(tally.items()): print(c, k, e, n)
