#!/usr/bin/env python3
"""run374 frame-time and proxy-cost summary. Usage: perf_summary.py <session.log> [label]"""
import sys, re, statistics as st, collections
path=sys.argv[1]; label=sys.argv[2] if len(sys.argv)>2 else path
kv=re.compile(r'(\w+)=("[^"]*"|\S+)')
fam={'frame_end':['dt_ms','draws'],
 'motion_output_frame':['draws','routed','fill_us','taa_run_us','taa_draw_us','taa_capture_us','taa_apply_us','route_draw_us','gate_us','readback_us'],
 'hdr_frame':['redirect_us','writeback_us','meter_us','readback_us','readback_transfer_lock_us'],
 'shadow_retention_frame':['walk_us','journal_us','draw_us'],
 'shadow_replay_depth':['draws','apply_us','draws0','draws1','draws2','draws3','draws4'],
 'shadow_alpha_casters':['seen'],
 'volumetric_fog_frame':['applied','cpu_us','cards'],
 'hull_lightmap_frame':['admitted'],
 'thin_vote_frame':['draws']}
data=collections.defaultdict(lambda: collections.defaultdict(dict))
sectors={}; slow=[]; nbytes=0; nrows=collections.Counter(); bytes_by=collections.Counter()
with open(path,errors='replace') as fh:
    for line in fh:
        nbytes+=len(line); t=line.split(' ',1)[0]; nrows[t]+=1; bytes_by[t]+=len(line)
        if t in fam or t in('sector_background','frame_phases_slow'):
            d=dict(kv.findall(line))
            if d.get('device','1')!='1' and t!='frame_phases_slow': continue
            fr=int(d.get('frame','-1'))
            if t=='sector_background': sectors[fr]=(d.get('status'),d.get('name'))
            elif t=='frame_phases_slow': slow.append((fr,d))
            else:
                for k in fam[t]:
                    if k in d:
                        try: data[t][k][fr]=float(d[k])
                        except ValueError: pass
def pct(v,p): v=sorted(v); return v[min(len(v)-1,int(p/100*len(v)))]
dt={f:v for f,v in data['frame_end']['dt_ms'].items() if f>=100}
print(f'# {label}\nframes(dev1,>=100)={len(dt)} log_bytes={nbytes} bytes/frame={nbytes/max(1,max(dt)):.0f}')
v=list(dt.values())
for p in(50,90,99): x=pct(v,p); print(f'dt p{p}={x:.1f} ms fps={1000/x if x else 0:.1f}')
print(f'dt mean={st.mean(v):.2f} max={max(v)}')
# 300-frame windows
print('window  p50  p90  max  draws_p50 sector')
fs=sorted(dt); 
for w0 in range(fs[0]//300*300, fs[-1]+1, 300):
    ww=[dt[f] for f in fs if w0<=f<w0+300]
    if not ww: continue
    dr=[data['frame_end']['draws'].get(f,0) for f in fs if w0<=f<w0+300]
    sec=[s for f,s in sectors.items() if f<w0+300]
    print(f'{w0:5d} {pct(ww,50):5.1f} {pct(ww,90):5.1f} {max(ww):6.0f} {pct(dr,50):6.0f} {sec[-1] if sec else ""}')
print('\n# per-frame medians / means over frames>=100 (CPU qpc attribution)')
for t,ks in fam.items():
    if t=='frame_end': continue
    for k in ks:
        d={f:x for f,x in data[t][k].items() if f>=100}
        if d: print(f'{t}.{k}: n={len(d)} p50={pct(list(d.values()),50):.1f} mean={st.mean(d.values()):.1f} p99={pct(list(d.values()),99):.1f}')
print('\n# slow frames (frame_phases_slow), top 15 by dt')
for fr,d in sorted(slow,key=lambda x:-int(x[1]['dt_us']))[:15]:
    print(fr,' '.join(f'{k}={d[k]}' for k in('dt_us','pre_render_us','scene_update_us','views_us','view_submit_us','present_us','views') if k in d))
print('\n# log bytes by row family (top 15)')
for t,b in bytes_by.most_common(15): print(f'{t}: rows={nrows[t]} bytes={b}')
