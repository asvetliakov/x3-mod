import re,sys
fn=sys.argv[1]; root=sys.argv[2] if len(sys.argv)>2 else None
d={}; cur=None
for l in open(fn):
    m=re.match(r'draw device=\S+ frame=\S+ index=(\d+) .*primitives=(\d+) vs=(\S+) ps=(\S+)',l)
    if m:
        cur=int(m.group(1)); d[cur]=dict(prims=m.group(2),vs=m.group(3)[:6],ps=m.group(4)[:6],anc=[],tex={})
        continue
    if cur is None: continue
    t=l.split()[0]
    if t=='object_context':
        kv=dict(x.split('=',1) for x in l.split()[1:] if '=' in x); d[cur].update(model=kv['model'][-4:],lod=kv['lod'][-1],node=kv['node'])
    elif t=='object_ancestor':
        kv=dict(x.split('=',1) for x in l.split()[1:] if '=' in x); d[cur]['anc'].append(kv['node'])
    elif t=='object_bounds':
        kv=dict(x.split('=',1) for x in l.split()[1:] if '=' in x); d[cur]['b']='%s-%s x %s-%s'%(kv['sx0'],kv['sx1'],kv['sy0'],kv['sy1'])
    elif t=='texture_desc':
        kv=dict(x.split('=',1) for x in l.split()[1:] if '=' in x); d[cur]['tex'][kv['stage']]='%sx%s/%s'%(kv['w'],kv['h'],{'894720068':'DXT5','827611204':'DXT1','21':'A8R8G8B8'}.get(kv['format'],kv['format']))
    elif t=='texture':
        kv=dict(x.split('=',1) for x in l.split()[1:] if '=' in x); d[cur].setdefault('tid',{})[kv['stage']]=kv['identity']
    elif t=='draw_args':
        kv=dict(x.split('=',1) for x in l.split()[1:] if '=' in x); d[cur]['nv']=kv.get('num_vertices')
# ancestry ids persist; propagate by node
for i,r in sorted(d.items()):
    if root and root not in r['anc'] and r.get('node')!=root:
        continue
    print(i,r.get('model'),'lod',r.get('lod'),'prims',r['prims'],'nv',r.get('nv'),r['vs'],r['ps'],r.get('b'),' '.join('%s:%s#%s'%(k,v,r.get('tid',{}).get(k)) for k,v in sorted(r['tex'].items())))
