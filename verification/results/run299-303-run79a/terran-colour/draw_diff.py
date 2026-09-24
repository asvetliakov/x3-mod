"""Full state of selected draws (texture stages 0-7 with desc, PS/VS float constants, bools) and their differences.
Usage: python3 draw_diff.py LOG FRAME:DRAW FRAME:DRAW [...]"""
import sys, re
log, keys = sys.argv[1], [tuple(k.split(':')) for k in sys.argv[2:]]
want = set(keys); rows = {}; cur = None
with open(log, errors='replace') as fh:
    for line in fh:
        if line.startswith('draw '):
            d = dict(re.findall(r'(\w+)=(\S+)', line)); k = (d.get('frame'), d.get('index'))
            cur = rows.setdefault(k, {'draw': f"prims={d.get('primitives')} vs={d.get('vs')} ps={d.get('ps')}"}) if k in want else None
            if len(rows) == len(want) and cur is None and all(len(v) > 3 for v in rows.values()):
                break
            continue
        if cur is None: continue
        if line.startswith(('texture stage', 'texture_desc', 'constant kind', 'object_context', 'object_bounds')):
            d = dict(re.findall(r'(\w+)=(\S+)', line))
            if line.startswith('texture stage'): cur['tex%s' % d['stage']] = f"id={d['identity']} type={d['type']} levels={d['levels']}"
            elif line.startswith('texture_desc'): cur['tex%s' % d['stage']] += f" {d['w']}x{d['h']} fmt={d['format']}"
            elif line.startswith('constant kind'): cur[f"{d['kind']}{d['type']}{int(d['reg']):03d}"] = d.get('bits', d.get('values'))
            elif line.startswith('object_context'): cur['ctx'] = f"model={d['model']} lod={d['lod']}"
            else: cur['bounds'] = f"alpha_tested={d.get('alpha_tested')}"
for k in keys:
    print('==', ':'.join(k), rows.get(k, {}).get('draw'), rows.get(k, {}).get('ctx'), rows.get(k, {}).get('bounds'))
    for s in range(8):
        print('  tex%d' % s, rows.get(k, {}).get('tex%d' % s))
allk = sorted(set().union(*(set(v) for v in rows.values())) - {'draw', 'ctx', 'bounds'} - {'tex%d' % s for s in range(8)})
print('== constants that differ between the listed draws (missing = 0/unset)')
for c in allk:
    vals = [rows.get(k, {}).get(c, '-') for k in keys]
    if len(set(vals)) > 1:
        print(' ', c, ' | '.join(vals))
