# Slot-0 (engine thread tid 220) frame table per report group, run379.
# Leaf PC is constant under FEX (module=65535 rva=0x10000), so the frame table
# (first X3AP.exe return address on the stack) is the only attribution.
import re,glob,collections,sys
L=glob.glob('/tmp/x3-bottleX3-run379/session-*.log')[0]
kv=lambda s:dict(re.findall(r'(\w+)=(\S+)',s))
reps=[];cur=None
for line in open(L,errors='replace'):
    if line.startswith('profile_report scope=delta'):
        cur={'h':kv(line),'frame':collections.Counter(),'pair':collections.Counter(),'thr':{}};reps.append(cur)
    elif line.startswith('profile_report scope=cumulative'):
        cur=None
    elif cur is None: continue
    elif line.startswith('profile_frame '):
        d=kv(line)
        if d['slot']=='0': cur['frame'][int(d['rva'],16)]+=int(d['count'])
    elif line.startswith('profile_pair '):
        d=kv(line)
        if d['slot']=='0': cur['pair'][(int(d['rva'],16),int(d['caller'],16))]+=int(d['count'])
    elif line.startswith('profile_thread '):
        d=kv(line); cur['thr'][d['slot']]=d
groups={'early_away R01':[1],'group R16-R20':[16,17,18,19,20],'late_away R22-R25':[22,23,24,25]}
out={}
for g,idx in groups.items():
    F=collections.Counter();P=collections.Counter();s0=0;tot=0;nt=0
    for i in idx:
        r=reps[i];F.update(r['frame']);P.update(r['pair']);s0+=int(r['thr']['0']['samples']);tot+=int(r['h']['samples'])
    ft=sum(F.values())
    out[g]=(F,P,s0)
    print(f"== {g}: reports={len(idx)} samples_all={tot} slot0_samples={s0} slot0_frame_rows_sum={ft} (table coverage {100*ft/s0:.1f}%) rva0={F[0]} ({100*F[0]/s0:.1f}%)")
    for rva,c in F.most_common(25):
        print(f"  0x{0x400000+rva:08x} {c:6d} {100*c/s0:5.1f}%")
    print('  top pairs:')
    for (a,b),c in P.most_common(15):
        print(f"  0x{0x400000+a:08x} <- 0x{0x400000+b:08x} {c:6d} {100*c/s0:5.1f}%")
import json
json.dump({g:{hex(0x400000+k):v for k,v in o[0].items()} for g,o in out.items()},open('/Users/asvetl/x3-mod/verification/results/run379-profile/frames.json','w'),indent=0)
