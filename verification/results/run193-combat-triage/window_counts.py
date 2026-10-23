# Per-window mean of every numeric count field of the per-frame rows; prints fields that differ >2x between
# the slow windows (slowA 1600-2800, slowB 4000-4470) and the in-flight calm window (calmB 3000-3900).
import re,sys,collections,statistics as st
W={'slowA':(1600,2800),'calmB':(3000,3900),'slowB':(4000,4470)}
def win(f):
    for k,(a,b) in W.items():
        if a<=f<=b: return k
rowt=set(sys.argv[2].split(',')) if len(sys.argv)>2 else None
acc=collections.defaultdict(lambda: collections.defaultdict(list))
num=re.compile(r'(\w+)=(-?\d+(?:\.\d+)?)(?=\s|$)')
for l in open(sys.argv[1],'rb'):
    s=l.decode('latin1'); k=s.split(' ',1)[0]
    if rowt and k not in rowt: continue
    m=re.search(r' frame=(\d+)',s)
    if not m: continue
    w=win(int(m.group(1)))
    if not w: continue
    for a,b in num.findall(s):
        if a in('frame','device','qpc'): continue
        acc[k+'.'+a][w].append(float(b))
for key in sorted(acc):
    d=acc[key]; m={w:(st.mean(d[w]) if d[w] else 0) for w in W}
    c=m['calmB']; sa=m['slowA']; sb=m['slowB']
    if max(sa,sb)>2*max(c,0.5) or (c>0 and max(sa,sb)<0.5*c):
        print(f'{key} slowA={sa:.1f} calmB={c:.1f} slowB={sb:.1f}')
