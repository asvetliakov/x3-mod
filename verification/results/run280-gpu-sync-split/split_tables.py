"""run280: TAA/fog split sums, undiagnosed frame per window from gpu_sync_timing rows. floor 0.264 ms."""
import re, sys, collections
F = 0.264
W = collections.defaultdict(dict); dt = {}; fr = {}
for line in open(sys.argv[1], errors='replace'):
    if not line.startswith('gpu_sync_timing window='): continue
    d = dict(kv.split('=', 1) for kv in line.split()[1:] if '=' in kv)
    w = int(d['window']); W[w][d['pass']] = (int(d['median_us'])/1e3, int(d['p90_us'])/1e3, int(d['wait_median_us'])/1e3, int(d['n']))
    dt[w] = int(d['dt_median_us'])/1e3; fr[w] = d['frames']
print('win frames dt scene present | taa copy mask box resolve sum taa-sum | fog_route march comp repair motes excl | npairs waits undiag_lo..hi')
for w in sorted(W):
    p = W[w]; g = lambda k: p.get(k, (0, 0, 0, 0))[0]
    taa_sub = sum(g(k) for k in ('taa_copy', 'taa_mask', 'taa_box', 'taa_resolve', 'taa_display'))
    fog_sub = sum(g(k) for k in ('fog_march', 'fog_composite', 'fog_repair', 'motes'))
    n = len(p); waits = sum(v[2] for v in p.values())
    print(f"W{w} {fr[w]} {dt[w]:.1f} {g('scene'):.2f} {g('present'):.2f} | {g('taa'):.2f} {g('taa_copy'):.2f} {g('taa_mask'):.2f} {g('taa_box'):.2f} {g('taa_resolve'):.2f} {taa_sub:.2f} {g('taa')-taa_sub:.2f} | "
          f"{g('fog_route'):.2f} {g('fog_march'):.2f} {g('fog_composite'):.2f} {g('fog_repair'):.2f} {g('motes'):.2f} {g('fog_route')-fog_sub:.2f} | {n} {waits:.2f} {dt[w]-waits-n*F:.1f}..{dt[w]-waits-F:.1f}  engine={g('engine'):.2f}")
