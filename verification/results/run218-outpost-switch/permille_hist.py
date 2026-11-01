# Histogram of fade_permille on routed fade-arm motion_route rows, per VS (bins of BIN permille, default 50),
# with the per-VS minimum and the counts below 990 and 950; streams the log.
import sys, collections
log, bin_w = sys.argv[1], int(sys.argv[2]) if len(sys.argv) > 2 else 50
hist, lo = collections.defaultdict(collections.Counter), {}
with open(log, 'rb') as f:
    for line in f:
        if not line.startswith(b'motion_route ') or b'fade_arm=1' not in line or b'routed=1' not in line:
            continue
        kv = dict(t.split(b'=', 1) for t in line.split() if b'=' in t)
        vs, p = kv[b'vs'].decode(), int(kv[b'fade_permille'])
        hist[vs][p // bin_w * bin_w] += 1
        lo[vs] = min(lo.get(vs, 1000), p)
for vs, c in sorted(hist.items(), key=lambda kv: -sum(kv[1].values())):
    n = sum(c.values())
    print(vs, 'rows=%d min=%d below990=%d below950=%d' % (n, lo[vs], sum(v for b, v in c.items() if b < 990),
          sum(v for b, v in c.items() if b < 950)), ' '.join('%d:%d' % (b, v) for b, v in sorted(c.items())))
