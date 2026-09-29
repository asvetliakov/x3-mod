# Callers (next X3AP frame) of the 0x004d6509 bucket and of rva0, slot 0, per window (run379).
import re,glob,collections
L=glob.glob('/tmp/x3-bottleX3-run379/session-*.log')[0]
kv=lambda s:dict(re.findall(r'(\w+)=(\S+)',s))
W={'group':range(16,21),'late_away':range(22,26),'early':range(8,10)}
i=-1;inc=False;P={k:collections.Counter() for k in W}
for line in open(L,errors='replace'):
    if line.startswith('profile_report scope=delta'): i+=1;inc=True
    elif line.startswith('profile_report scope=cumulative'): inc=False
    elif inc and line.startswith('profile_pair ') and ' slot=0 ' in line:
        d=kv(line);r=0x400000+int(d['rva'],16)
        for k,rg in W.items():
            if i in rg: P[k][(r,0x400000+int(d['caller'],16))]+=int(d['count'])
for k in W:
    c=P[k];tot=sum(v for (r,_),v in c.items() if r==0x4d6509)
    print(f'== {k}: pairs with rva 0x4d6509 total {tot}')
    for (r,cl),v in sorted(((x,v) for x,v in c.items() if x[0]==0x4d6509),key=lambda t:-t[1])[:12]:
        print(f'   caller 0x{cl:08x} {v}')
