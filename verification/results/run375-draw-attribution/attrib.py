"""Run375 F8 burst draw attribution: joins draw/object_context/motion_route by index, names via cull_census body."""
import re,glob,collections,sys
f=sorted(glob.glob(sys.argv[1] if len(sys.argv)>1 else '/tmp/x3-bottleX3-run375/session-*.log'))[0]
KV=re.compile(r'(\w+)=(\S+)')
FR=set(range(int(sys.argv[2]),int(sys.argv[3])+1)) if len(sys.argv)>3 else set(range(4400,4408))
D=collections.defaultdict(dict); body={}; census=collections.defaultdict(collections.Counter); fe={}; marker={}; shadow={}
for l in open(f,errors='replace'):
    m=re.search(r' frame=(\d+) ',l) or re.search(r'^\S*\s*\S*frame=(\d+)',l)
    t=l.split(' ',1)[0].split()[-1] if l else ''
    k=l.split()[0] if l.split() else ''
    # row kind is the token before 'device=' or 'frame='
    mk=re.search(r'(\w+) (?:device=\d+ )?frame=(\d+)',l)
    if not mk: continue
    kind,fr=mk.group(1),int(mk.group(2))
    if kind=='cull_census':
        d=dict(KV.findall(l)); body[d.get('node')]=d.get('body')
        if fr in FR: census[fr][d.get('verdict')]+=1
    if fr not in FR: continue
    d=dict(KV.findall(l))
    if kind in('draw','object_context','motion_route','hull_emission_draws','hull_lightmap_widen_draw','object_bounds') and 'index' in d:
        D[(fr,int(d['index']))].update({kind[:6]+'_'+a:b for a,b in d.items()}); D[(fr,int(d['index']))]['has_'+kind]=1
    elif kind=='frame_end': fe[fr]=l.strip()[:200]
    elif kind=='scene_end_marker': marker[fr]=int(d['draw_index'])
    elif kind=='shadow_replay_depth': shadow[fr]=d.get('draws')
def fam(r):
    b=body.get(r.get('object_node'),'') or ''
    if not r.get('object_node'): return 'no_object_context',b
    for p,n in [('ships','ship'),('stations','station'),('effects/engines','engine_fx'),('effects/weapons','weapon'),('environments','sky/env'),('objects/v/','v_misc'),('cockpit','cockpit')]:
        if p in b.replace('\\','/'): return n,b
    return 'other:'+b.split('\\')[0],b
for fr in sorted(FR):
    rows=[(i,r) for (g,i),r in D.items() if g==fr]
    if not rows: continue
    mk=marker.get(fr,10**9)
    c=collections.Counter(); nodes=collections.defaultdict(collections.Counter); prims=collections.Counter()
    for i,r in rows:
        fa,b=fam(r); ph='post' if i>mk else 'scene'
        c[(ph,fa)]+=1; nodes[fa][(r.get('object_node'),b)]+=1
    print(f'frame {fr} draws={len(rows)} scene_end_marker={mk} shadow_replay_draws={shadow.get(fr)} census={dict(census[fr])}')
    print('  frame_end:',fe.get(fr))
    for k,n in sorted(c.items(),key=lambda x:-x[1]): print('  ',k,n)
    if fr==min(FR):
        for fa,cn in nodes.items():
            print('  family',fa,'distinct_nodes',len(cn),'draws',sum(cn.values()))
            bb=collections.Counter()
            for (nd,b),n in cn.items(): bb[(b,n)]+=1
            for (b,n),k in sorted(bb.items(),key=lambda x:-x[1]*x[0][1])[:12]: print('     ',k,'node(s) x',n,'draws',b)
        ps=collections.Counter((r.get('draw_ps'),fam(r)[0],'post' if i>mk else 'scene') for i,r in rows)
        for k,n in ps.most_common(20): print('   ps',k,n)
