# Per-300-frame window: frame_phases, frame_timing, pass/residual phases (if present) and every submit_phases stamp
# (p50 us and calls p50 per frame). usage: python3 submit_split.py x3m.log
import re,sys
W={}
for l in open(sys.argv[1],'rb'):
    s=l.decode('latin1'); k=s.split(' ',1)[0]
    if k in('frame_phases','frame_timing','pass_phases','residual_phases','submit_phases','scene_graph_census'):
        d=dict(re.findall(r'(\w+)=(\S+)',s)); f=int(d['frame'])
        W.setdefault((f+150)//300*300,{})[k]=d
S=['sort','walk','technique','end','block','inverse_world','inverse_view','material','world']
for f in sorted(W):
    w=W[f]; fp=w.get('frame_phases',{}); ft=w.get('frame_timing',{}); p=w.get('pass_phases',{}); r=w.get('residual_phases',{}); sp=w.get('submit_phases',{}); sc=w.get('scene_graph_census',{})
    line=f"{f} dt={fp.get('dt_p50_us')} views={fp.get('views_p50')} vsub={fp.get('view_submit_p50_us')} draws={ft.get('draws_p50')} gapdd={ft.get('gap_draw_per_draw_us')} nodes={sc.get('engine_nodes')} inserts={sc.get('inserts')}"
    if p: line+=f" passes={p.get('passes_p50')} apply={p.get('apply_p50_us')} sum={p.get('sum_p50_us')} pself={p.get('self_p50_us')}"
    if r: line+=f" prepare={r.get('prepare_p50_us')} setup={r.get('setup_p50_us')}"
    print(line)
    if sp:
        print('   ', ' '.join(f"{n}={sp.get(n+'_p50_us')}us/{sp.get(n+'_calls_p50')}c" for n in S),
              f"block_net={sp.get('block_net_p50_us')} material_net={sp.get('material_net_p50_us')} sort_nodes={sp.get('sort_nodes_p50')}/{sp.get('sort_nodes_max')} walk_miss={sp.get('walk_misses_p50')} walk_it={sp.get('walk_iterations_p50')}/{sp.get('walk_iterations_p95')} stamps={sp.get('stamps_p50')} sself={sp.get('self_p50_us')} idle={sp.get('idle')} mat_p95={sp.get('material_p95_us')}")
