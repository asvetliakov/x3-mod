"""run381: the input_part=0 region stamps (cutevent/containers/sweep/region) against frame_phases per 300-frame window."""
import sys, re
kv = re.compile(rb'(\w+)=(-?[\w.]+)')
lp, fp, ft = {}, {}, {}
with open(sys.argv[1], 'rb') as f:
    for line in f:
        for prefix, store in ((b'loop_phases ', lp), (b'frame_phases ', fp), (b'frame_timing ', ft)):
            if line.startswith(prefix):
                d = dict(kv.findall(line)); store[int(d[b'frame'])] = d
g = lambda m, k: int(m.get(k.encode(), b'-1'))
print('frame  dt  pre_render | region cutevent containers sweep | sectorsum | max_owner region_max | draws  (ms)')
for fr in sorted(lp):
    d, p, t = lp[fr], fp.get(fr, {}), ft.get(fr, {})
    print(f"{fr:5d} {g(p,'dt_p50_us')/1000:5.1f} {g(p,'pre_render_p50_us')/1000:6.1f} | {g(d,'region_p50_us')/1000:6.2f} {g(d,'cutevent_p50_us')/1000:7.2f} "
          f"{g(d,'containers_p50_us')/1000:8.2f} {g(d,'sweep_p50_us')/1000:6.2f} | {g(d,'sum_p50_us')/1000:5.2f} | "
          f"{d.get(b'region_max_owner', b'-').decode():10s} {g(d,'region_max_us')/1000:6.1f} | {g(t,'draws_p50')}")
