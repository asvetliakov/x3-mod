"""engine_draw rows (one per glow-jet record) for given frames: handle, node, name, size, value_eff, verdict, origin.
usage: engine_draw_rows.py LOG frame [frame ...]"""
import sys
log = sys.argv[1]; want = {f'frame={f}' for f in sys.argv[2:]}
cols = 'index handle node serial name cluster s size value_eff radius verdict flags origin'.split()
with open(log, errors='replace') as f:
    for line in f:
        if not line.startswith('engine_draw '): continue
        t = line.split()
        if t[2] not in want: continue
        d = dict(x.split('=', 1) for x in t[1:] if '=' in x)
        print(t[2], ' '.join(f'{c}={d.get(c)}' for c in cols))
