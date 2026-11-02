#!/usr/bin/env python3
"""FL run 19 A/B timing comparison (engine light on vs off). Streams the logs."""
import re, sys, statistics as st
A="/home/asvetl/x3m-logs/fl-run19a-engine-light-on.log"; B="/home/asvetl/x3m-logs/fl-run19b-engine-light-off.log"
kv=re.compile(r'(\w+)=([^ ]+)')
def pct(v,p):
    v=sorted(v); return v[min(len(v)-1,int(p*len(v)))] if v else float('nan')
def scan(path):
    r=dict(last={},wins={},dt=[],draws=[],fog_cpu=[],fog_app=0,fog_n=0,sd_us=[],sd_dr=[],sd_apply=[],el_lit=[],el_frames=0,eng_c=[],eng_r=[],dtw=[])
    for line in open(path,errors='replace'):
        t=line.split(' ',1)[0]
        if t=='gpu_sync_timing' and ' window=' in line and ' pass=' in line:
            d=dict(kv.findall(line)); p=d['pass']; r['last'][p]=d; r['wins'].setdefault(p,[]).append(float(d['median_us']))
            if p=='scene': r['dtw'].append((float(d['dt_median_us']),float(d['dt_p90_us'])))
        elif t=='hdr_frame':
            m=re.search(r' dt_ms=([\d.]+)',line)
            if m and float(m.group(1))>0: r['dt'].append(float(m.group(1)))
        elif t=='frame_end':
            m=re.search(r' draws=(\d+)',line)
            if m: r['draws'].append(int(m.group(1)))
        elif t=='volumetric_fog_frame':
            d=dict(kv.findall(line)); r['fog_n']+=1; r['fog_app']+=d.get('applied')=='1'; r['fog_cpu'].append(float(d.get('cpu_us',0)))
        elif t=='shadow_replay_depth':
            d=dict(kv.findall(line))
            if 'us' in d: r['sd_us'].append(float(d['us'])); r['sd_dr'].append(int(d['draws'])); r['sd_apply'].append(float(d.get('apply_us',0)))
        elif t=='engine_light_frame':
            d=dict(kv.findall(line)); r['el_frames']+=1; r['el_lit'].append(int(d['draws_lit']))
        elif t=='engine_frame':
            d=dict(kv.findall(line)); r['eng_c'].append(int(d['candidates'])); r['eng_r'].append(int(d['records']))
    return r
def S(v): return f"p50={pct(v,.5):.1f} p90={pct(v,.9):.1f} mean={(sum(v)/len(v) if v else 0):.1f} n={len(v)}"
a,b=scan(A),scan(B); out=[]
P=print
passes=sorted(set(a['last'])|set(b['last']),key=lambda p:-float(a['last'].get(p,{}).get('session_median_us',0)))
P("(1) session GPU time per pass, us (last window's session_* fields; measured)")
P(f"{'pass':16}{'A_med':>8}{'A_p90':>8}{'A_n':>8}{'B_med':>8}{'B_p90':>8}{'B_n':>8}{'dMed':>8}{'A_winIQR':>10}")
sa=sb=0
for p in passes:
    x=a['last'].get(p,{}); y=b['last'].get(p,{})
    am=float(x.get('session_median_us',0)); bm=float(y.get('session_median_us',0)); sa+=am; sb+=bm
    w=a['wins'].get(p,[0]); iqr=pct(w,.75)-pct(w,.25)
    P(f"{p:16}{am:8.0f}{float(x.get('session_p90_us',0)):8.0f}{x.get('session_n','-'):>8}{bm:8.0f}{float(y.get('session_p90_us',0)):8.0f}{y.get('session_n','-'):>8}{am-bm:8.0f}{iqr:10.0f}")
P(f"sum of session medians: A={sa:.0f} B={sb:.0f}")
for n,r in(('A',a),('B',b)):
    P(f"{n} window dt_median_us: median-of-windows={st.median(x[0] for x in r['dtw']):.0f} dt_p90 median-of-windows={st.median(x[1] for x in r['dtw']):.0f} windows={len(r['dtw'])}")
P("(2) frame interval hdr_frame dt_ms; draws per frame (frame_end draws=)")
for n,r in(('A',a),('B',b)): P(f"{n} dt_ms {S(r['dt'])} | draws {S(r['draws'])}")
P("(4) shadow_replay_depth us (CPU) / draws / apply_us")
for n,r in(('A',a),('B',b)): P(f"{n} us {S(r['sd_us'])} | draws {S(r['sd_dr'])} | apply_us {S(r['sd_apply'])}")
P("(5) fog")
for n,r in(('A',a),('B',b)): P(f"{n} fog rows={r['fog_n']} applied=1 {r['fog_app']} ({100*r['fog_app']/max(1,r['fog_n']):.1f}%) cpu_us {S(r['fog_cpu'])}")
P("engine light / engine_frame")
for n,r in(('A',a),('B',b)):
    lit=r['el_lit']; P(f"{n} engine_light_frame rows={r['el_frames']} draws_lit {S(lit)} frames_with_lit={sum(1 for v in lit if v)} | engine_frame candidates {S(r['eng_c'])} records {S(r['eng_r'])}")
