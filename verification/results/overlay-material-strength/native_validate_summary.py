#!/usr/bin/env python3
"""Fold a native_validate.exe transcript over the strength twins and the generator's twins.json into
native_validate_twins.json beside this script (names, sizes, verdicts and message counts only; the shader bytes stay
in the scratch dir). Usage: native_validate_summary.py SCRATCH  (SCRATCH holds native_twins.txt and twins.json, as
native_validate_twins.sh leaves them)."""
import collections, json, re, sys
from pathlib import Path
S = Path(sys.argv[1])
out_path = Path(__file__).with_name('native_validate_twins.json')
rows, cur = [], None
for line in open(S / 'native_twins.txt'):
    m = re.search(r'(ps_[0-9a-f]{16})-(\w+)-twin\.bin words=(\d+)', line)
    if m:
        cur = {'original': m.group(1), 'kind': m.group(2), 'words': int(m.group(3)), 'messages': []}
        rows.append(cur)
        continue
    m = re.search(r'msg instr=(-?\d+) id=(\d+): (.*)', line)
    if m:
        cur['messages'].append({'instruction_call': int(m.group(1)), 'id': int(m.group(2)), 'text': m.group(3).strip()})
    m = re.search(r'end=(\w+) messages=(\d+)', line)
    if m:
        cur['end'] = m.group(1)
gen = json.load(open(S / 'twins.json'))
tally = collections.Counter((r['kind'], r.get('end'), len(r['messages'])) for r in rows)
json.dump({'validator': 'SysWOW64 d3d9.dll Direct3DShaderValidatorCreate9 (native Windows host through WSL interop, no device)',
           'generator': 'material_strength_twin_structure: parents motion / fill 0.05 / gain 4 / widen K=B=3 dynamic / share / '
                        'share+gain / share+gain+widen and the engine-light twin of each (14 forms), depth on; twin = parent + add rT.w + join edit',
           'programs': gen['programs'], 'generated_rows': len(gen['rows']),
           'twins': sum(1 for r in gen['rows'] if r['status'] == 0),
           'refusals': dict(collections.Counter(r['refusal'] for r in gen['rows'] if r['refusal'] != 'none')),
           'lanes_read': dict(collections.Counter(str(r['lanes_read']) for r in gen['rows'])),
           'max_twin_slots': max(r.get('twin_slots', 0) for r in gen['rows']),
           'max_twin_words': max(r.get('twin_words', 0) for r in gen['rows']),
           'tally': [{'kind': k, 'end': e, 'messages': n, 'programs': c} for (k, e, n), c in sorted(tally.items())],
           'rows': [{k: v for k, v in r.items() if k != 'messages'} | {'messages': len(r['messages'])} for r in rows],
           'messages': [m for r in rows for m in r['messages']][:50]}, open(out_path, 'w'), indent=1)
for (k, e, n), c in sorted(tally.items()):
    print(c, k, 'end', e, 'messages', n)
print('validated', len(rows), 'twins of', gen['programs'], 'programs; refusals',
      dict(collections.Counter(r['refusal'] for r in gen['rows'] if r['refusal'] != 'none')))
