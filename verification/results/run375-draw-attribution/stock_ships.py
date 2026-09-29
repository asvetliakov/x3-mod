"""run356 (stock): per captured frame, ship hull/prop draws and distinct ship nodes."""
import re,glob,collections,sys
f=glob.glob(sys.argv[1])[0]
KV=re.compile(r'(\w+)=(\S+)')
body={};S=collections.defaultdict(lambda:collections.Counter());N=collections.defaultdict(set);T=collections.Counter()
for l in open(f,errors='replace'):
    if 'cull_census' in l:
        d=dict(KV.findall(l)); body[d.get('node')]=d.get('body','')
    elif 'object_context device' in l:
        d=dict(KV.findall(l)); fr=int(d['frame']); T[fr]+=1
        bn=body.get(d.get('node'),'') or ''
        if bn.startswith('ships'):
            S[fr]['hull' if '\\hull' in bn else 'prop']+=1
            if '\\hull' in bn: N[fr].add(d['node'])
best=sorted(S,key=lambda fr:-len(N[fr]))[:5]
for fr in best: print(fr,'draws',T[fr],'ship_hulls',len(N[fr]),dict(S[fr]))
