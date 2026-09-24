#!/usr/bin/env python3
"""Join z_only prepass draws (VS c78b4c68a87fce74, null PS) to the object_context row of the same
frame/index and count node +0x130 bit 0x20000 (the prepass alias bit). Usage: zonly_join.py session.log ..."""
import collections, re, sys
ZONLY = 'c78b4c68a87fce74'
for path in sys.argv[1:]:
    draws, ctx = set(), {}
    with open(path, errors='replace') as f:
        for line in f:
            if line.startswith('draw ') and ZONLY in line:
                m = re.search(r'frame=(\d+) index=(\d+)', line); draws.add((m.group(1), m.group(2)))
            elif line.startswith('object_context '):
                m = re.search(r'frame=(\d+) index=(\d+).*?model=([0-9a-f]+).*?flags12c=([0-9a-f]+) flags130=([0-9a-f]+)', line)
                if m: ctx[(m.group(1), m.group(2))] = (m.group(3), int(m.group(4), 16), int(m.group(5), 16))
    joined = [ctx[k] for k in draws if k in ctx]
    bit = sum(1 for _, _, f130 in joined if f130 & 0x20000)
    models = collections.Counter(mo for mo, _, _ in joined)
    print(f'{path.split("/")[-2]}: z_only draws {len(draws)}, with object_context {len(joined)}, '
          f'flags130&0x20000 {bit}, distinct models {len(models)}, frames {len({k[0] for k in draws})}')
