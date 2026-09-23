"""D3D9 API-surface census of one proxy session log (row queries only; the log is never read whole).

Usage: python3 census.py /tmp/x3-bottleX3-run286/session-*.log > census-run286.txt
Counts, per captured draw, the render/sampler states, transforms, texture formats, buffer pools/usages,
vertex-element types, draw kinds, capture events, constant register ranges and distinct shader hashes.
Owning note: docs/architecture/d3d9-to-d3d11-translation.md section 1.
"""
import sys, re, collections
path = sys.argv[1]
C = collections.defaultdict(collections.Counter)
kv = re.compile(r'(\w+)=([^ ]*)')
want = {'state':('id','value'), 'sampler':('state','value'), 'transform':('state',), 'texture_desc':('format',),
        'texture':('type',), 'vertex_buffer':('usage','pool','fvf'), 'index_buffer':('usage','pool','format'),
        'resource':('type',), 'surface':('role','format','usage','msaa'), 'vertex_element':('type','usage','method'),
        'draw':('kind','topology'), 'capture_event':('op',), 'stream':('slot','frequency'), 'geometry':('source',),
        'constant':('kind','type'), 'constants':('kind','type','encoding'), 'viewport':('w','h'), 'buffer_content':('kind','flags')}
vs=set(); ps=set(); frames=set(); stateid=collections.Counter(); sampid=collections.Counter(); regs=collections.defaultdict(set)
with open(path,'rb') as f:
    for raw in f:
        line=raw.decode('latin1')
        k=line.split(' ',1)[0]
        if k not in want: continue
        d=dict(kv.findall(line))
        key=tuple(d.get(x,'?') for x in want[k])
        C[k][key]+=1
        if k=='draw':
            vs.add(d['vs']); ps.add(d['ps']); frames.add(d['frame'])
        if k=='state': stateid[d['id']]+=1
        if k=='sampler': sampid[d['state']]+=1
        if k=='constant': regs[(d['kind'],d['type'])].add(int(d['reg']))
for k in want:
    items=C[k].most_common()
    print(f'== {k}: {sum(C[k].values())} rows, {len(items)} distinct {want[k]}')
    for key,n in items[:40]: print('   ',n,key)
print('distinct vs',len(vs),'ps',len(ps),'frames',sorted(frames))
print('state ids', sorted(((int(i),n) for i,n in stateid.items())))
print('sampler states', sorted(((int(i),n) for i,n in sampid.items())))
for k,v in regs.items(): print('const regs',k,'min',min(v),'max',max(v),'count',len(v))
