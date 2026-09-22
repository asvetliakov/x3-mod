"""Body-level draw diff run255 f3351 vs run257 f3615 from the census outputs."""
import collections
def load(p, frame):
    out = collections.defaultdict(lambda: [0,0,0]); cur = None
    for l in open(p):
        if l.startswith('== frame '): cur = l.split()[2]
        elif l.startswith('B ') and cur == frame:
            p_ = l.split(None, 4); out[p_[4].strip()] = [int(p_[1]), int(p_[2]), int(p_[3])]
    return out
import sys; a = load(sys.argv[1], sys.argv[2]); b = load(sys.argv[3], sys.argv[4])
print('body  A(draws,alpha,nodes)  B(draws,alpha,nodes)  delta_draws')
for k in sorted(set(a) | set(b), key=lambda k: (b.get(k,[0])[0]-a.get(k,[0])[0])):
    x, y = a.get(k, [0,0,0]), b.get(k, [0,0,0])
    if x != y: print(f"{k:80s} {x} {y} {y[0]-x[0]}")
