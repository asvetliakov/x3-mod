# Per-thread samples and leaf-kind split for chosen report groups (run379). Leaf PC is void under FEX
# (guest samples report module=65535 rva=0x10000 -> kind other); ntdll leaf = parked in a Wine wait/syscall.
import re,glob,collections
L=glob.glob('/tmp/x3-bottleX3-run379/session-*.log')[0]
kv=lambda s:dict(re.findall(r'(\w+)=(\S+)',s))
W={'early_away R01':[1],'group R16-R20':list(range(16,21)),'late_away R22-R25':list(range(22,26))}
i=-1;inc=False;T={k:collections.defaultdict(collections.Counter) for k in W};S={k:0 for k in W}
for line in open(L,errors='replace'):
    if line.startswith('profile_report scope=delta'):
        i+=1;inc=True;d=kv(line)
        for k,rg in W.items():
            if i in rg: S[k]+=int(d['samples'])
    elif line.startswith('profile_report scope=cumulative'): inc=False
    elif inc and line.startswith('profile_thread '):
        d=kv(line)
        for k,rg in W.items():
            if i in rg:
                key=(d['slot'],d['tid'],d['start_module'])
                for f in ('samples','leaf_x3ap','leaf_ntdll','leaf_wine','leaf_d3dx','leaf_proxy','leaf_other','suspend_failures'):
                    T[k][key][f]+=int(d.get(f,0))
for k in W:
    print(f'== {k}: all samples {S[k]}')
    for key,c in sorted(T[k].items(),key=lambda t:-t[1]['leaf_other'])[:8]:
        s=c['samples'] or 1
        print(f"  slot {key[0]:>2} tid {key[1]:>4} start_mod {key[2]:>2}: samples {c['samples']:6d} other(guest) {100*c['leaf_other']/s:5.1f}% ntdll {100*c['leaf_ntdll']/s:5.1f}% x3ap {c['leaf_x3ap']} proxy {c['leaf_proxy']} wine {c['leaf_wine']} d3dx {c['leaf_d3dx']}")
