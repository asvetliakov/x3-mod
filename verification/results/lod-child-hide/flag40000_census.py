"""Count cull_census rows whose node flags (+0x12c at entry) carry 0x40000. Usage: LOG"""
import sys,re,collections
kv=re.compile(r'(\w+)=(\S+)')
c=collections.Counter(); mods=collections.Counter(); verd=collections.Counter(); bodies={}
with open(sys.argv[1], errors='replace') as fh:
    for line in fh:
        if not line.startswith('cull_census device='): continue
        d=dict(kv.findall(line)); c['rows']+=1
        f=int(d['flags_in'],16)
        if f & 0x40000:
            c['flag40000']+=1; mods[d['model']]+=1; verd[d['verdict']]+=1; bodies[d['model']]=d.get('body','-')
print(dict(c))
for m,n in mods.most_common(25): print(m,n,bodies[m])
print('verdicts',verd.most_common(10))
