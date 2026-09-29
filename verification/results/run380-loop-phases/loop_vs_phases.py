"""run380: loop_phases intervals (collide/simulate/post/passb, the input region) against frame_phases per 300-frame window."""
import sys, re
L = sys.argv[1]
kv = re.compile(rb'(\w+)=(-?[\w.]+)')
lp, fp, ft = {}, {}, {}
with open(L, 'rb') as f:
    for line in f:
        for prefix, store in ((b'loop_phases ', lp), (b'frame_phases ', fp), (b'frame_timing ', ft)):
            if line.startswith(prefix):
                d = dict(kv.findall(line)); store[int(d[b'frame'])] = d
g = lambda m, k: int(m.get(k.encode(), b'-1'))
print('frame dt_p50 pre_render views scene_end | collide simulate post passb sum input self | draws  (ms)')
for fr in sorted(lp):
    d, p, t = lp[fr], fp.get(fr, {}), ft.get(fr, {})
    print(f"{fr:5d} {g(p,'dt_p50_us')/1000:6.1f} {g(p,'pre_render_p50_us')/1000:8.1f} {g(p,'views_p50_us')/1000:6.1f} {g(p,'scene_end_p50_us')/1000:7.2f} | "
          f"{g(d,'collide_p50_us')/1000:6.2f} {g(d,'simulate_p50_us')/1000:7.2f} {g(d,'post_p50_us')/1000:5.2f} {g(d,'passb_p50_us')/1000:5.2f} {g(d,'sum_p50_us')/1000:5.2f} "
          f"{g(d,'input_p50_us')/1000:6.2f} {g(d,'self_p50_us')/1000:5.2f} | {g(t,'draws_p50')}")
