# Per-300-frame window: residual prepare_p50 vs per-frame scene counters (means over the window) and window rows;
# prints the table and Pearson r of each counter against prepare over the in-sector windows. usage: python3 prepare_drivers.py x3m.log
import re,sys,statistics as st,collections
PF={'screen_emission_additive_frame':['bolt_flag'],'hdr_frame':['lit_fraction'],'engine_frame':['candidates','not_jet'],
    'shadow_replay_candidates':['c0','c4'],'motion_output_frame':['draws','gate2','gate3','gate_us'],'camera_state':['r11','r12'],
    'occlusion_cull':['candidates'],'emission_source_gain_frame':['refused_other']}
WR={'collide_query_phases':['queries','engine_query_sum_us'],'scene_graph_census':['engine_nodes','inserts','tasks','unattached'],
    'cull_small_props_frame':['culled','no_scope','walks'],'light_phases':['traversal_calls_p50'],'residual_phases':['prepare_p50_us'],
    'submit_phases':['sort_nodes_p50','world_calls_p50','idle','walk_iterations_p50']}
acc=collections.defaultdict(lambda: collections.defaultdict(list)); win=collections.defaultdict(dict); cur=None
for l in open(sys.argv[1],'rb'):
    s=l.decode('latin1'); k=s.split(' ',1)[0]
    if k=='frame_end': cur=int(re.search(r' frame=(\d+)',s).group(1)); continue
    if k in PF or k in WR:
        m=re.search(r' frame=(\d+)',s); f=int(m.group(1)) if m else cur
        if f is None: continue
        d=dict(re.findall(r'(\w+)=(-?[\d.]+)(?=\s)',s+' '))
        if k in PF:
            for x in PF[k]:
                if x in d: acc[(f-1)//300*300+300][k+'.'+x].append(float(d[x]))
        else:
            w=(f+150)//300*300
            for x in WR[k]:
                if x in d: win[w][k+'.'+x]=float(d[x])
rows=[]
for w in sorted(win):
    if 'residual_phases.prepare_p50_us' not in win[w]: continue
    v=dict(win[w]); v.update({k:st.mean(x) for k,x in acc[w].items()}); rows.append((w,v))
keys=sorted({k for _,v in rows for k in v})
for w,v in rows: print(w,' '.join(f"{k.split('.',1)[1]}={v[k]:.2f}" if isinstance(v.get(k),float) else '' for k in keys if k in v))
sel=[(w,v) for w,v in rows if v.get('scene_graph_census.engine_nodes',0)>1000]
print('--- r vs prepare over',len(sel),'in-sector windows')
P=[v['residual_phases.prepare_p50_us'] for _,v in sel]
for k in keys:
    xs=[v.get(k) for _,v in sel]
    if None in xs or len(set(xs))<3: continue
    print(f'{st.correlation(xs,P):+.2f} {k}')
