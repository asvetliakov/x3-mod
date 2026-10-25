import re,sys,struct
def f(b): return ','.join('%.4g'%struct.unpack('>f',bytes.fromhex(x))[0] for x in b.split(','))
def consts(fn,idx):
    out={};cur=None
    for l in open(fn):
        m=re.match(r'draw device=\S+ frame=\S+ index=(\d+) ',l)
        if m: cur=int(m.group(1)); continue
        if cur!=idx: continue
        kv=dict(re.findall(r'(\w+)=(\S+)',l))
        if l.startswith('constant '): out[(kv['kind'],kv['type'],int(kv['reg']))]=f(kv['bits']) if kv['type']=='f' else kv.get('bits')
        elif l.startswith('sampler '): out[('smp',int(kv['stage']),int(kv['state']))]=kv['value']
        elif l.startswith('state '): out[('rs',)+tuple(sorted(kv.items()))[:1]]=l.strip()
    return out
a=consts(sys.argv[1],int(sys.argv[2])); b=consts(sys.argv[3],int(sys.argv[4]))
skip=sys.argv[5:] 
for k in sorted(set(a)|set(b), key=str):
    if a.get(k)!=b.get(k) and k[0] not in skip: print(k, a.get(k), '|', b.get(k))
