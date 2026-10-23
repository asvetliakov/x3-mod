# Per-window cost fields (_us/_ms/_ns) of the per-frame rows and log volume per frame, Run193 x3m.log.
# usage: python3 window_costs.py /mnt/c/X3/x3m.log
import re,sys,collections,statistics as st
W={'calmA':(600,1500),'slowA':(1600,2800),'calmB':(3000,3900),'slowB':(4000,4470)}
def win(f):
    for k,(a,b) in W.items():
        if a<=f<=b: return k
fre=re.compile(r' frame=(\d+)')
vals=collections.defaultdict(list)   # (win,row.field)->list
rows=collections.Counter(); byts=collections.Counter(); frames=collections.defaultdict(set)
cur=None
for l in open(sys.argv[1],'rb'):
    s=l.decode('latin1'); k=s.split(' ',1)[0]
    m=fre.search(s)
    if m and k in('frame_end',): cur=int(m.group(1))
    f=int(m.group(1)) if m else cur
    w=win(f) if f is not None else None
    if w is None: continue
    rows[(w,k)]+=1; byts[(w,k)]+=len(l); frames[w].add(f)
    if k in ('frame_phases','frame_timing','collide_query_phases'): continue
    for a,b in re.findall(r'(\w+_(?:us|ms|ns))=(-?[\d.]+)\b',s):
        try: vals[(w,k+'.'+a)].append(float(b))
        except: pass
nf={w:len(frames[w]) for w in W}
print('frames',nf)
keys=sorted({k for (_,k) in vals})
print('field', *W)
for k in keys:
    out=[]
    for w in W:
        v=vals.get((w,k),[])
        out.append(f'{st.mean(v):.1f}/{len(v)/max(nf[w],1):.2f}' if v else '-')
    if any(o!='-' and not o.startswith('0.0/') for o in out): print(k,*out)
print('--- rows/frame and bytes/frame')
tot={w:sum(b for (ww,_),b in byts.items() if ww==w) for w in W}
print('TOTAL bytes/frame',*[f'{tot[w]/nf[w]:.0f}' for w in W])
kk=sorted({k for (_,k) in rows}, key=lambda k:-sum(byts[(w,k)] for w in W))[:25]
for k in kk: print(k,*[f'{rows[(w,k)]/nf[w]:.2f}r/{byts[(w,k)]/nf[w]:.0f}B' for w in W])
