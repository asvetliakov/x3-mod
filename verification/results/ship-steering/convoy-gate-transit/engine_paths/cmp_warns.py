import re, sys
# usage: cmp_warns.py old.txt new.txt : per cast x tf (seed 1, lane) the engine-warn figures of both runs, side by side, only the rows that differ
pat = re.compile(r'^scenario C (\S+) lane tf (\d+) \d+ fps seed 1: .*?episodes (\d+) uncert (\d+) nonmember_warns (\d+) capsule (\S+) .*?'
                 r'gate warns (\d+) unsup (\d+) x12 (\d+) .*?obst warns (\d+) unsup (\d+) x12 (\d+) .*?avoidable (\d+) unsafe (\d+)')
keys = ('ep', 'unc', 'nonmem', 'caps', 'gw', 'gunsup', 'gx12', 'ow', 'ounsup', 'ox12', 'avoid', 'unsafe')


def load(p):
    out = {}
    for line in open(p):
        m = pat.match(line)
        if m:
            out[(m.group(1), int(m.group(2)))] = dict(zip(keys, m.groups()[2:]))
    return out


a, b = load(sys.argv[1]), load(sys.argv[2])
print('cast tf | ' + ' '.join(keys) + '   (old -> new where different)')
for k in sorted(set(a) | set(b)):
    x, y = a.get(k), b.get(k)
    if x is None or y is None:
        print(k, 'only in', 'new' if x is None else 'old')
        continue
    d = [f'{kk} {x[kk]}->{y[kk]}' for kk in keys if x[kk] != y[kk]]
    if d:
        print(f'{k[0]} tf{k[1]}: ' + ', '.join(d))
