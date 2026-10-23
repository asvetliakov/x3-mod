# Camera orientation, lit fraction, visible bolts and near-cascade casters per window (Run193 x3m.log)
import re,sys,statistics as st
W={'slowA':(1600,2800),'calmB':(3000,3900),'slowB':(4000,4470)}
acc={w:{'r11':[],'r12':[],'lit_fraction':[],'bolt_flag':[],'c0':[]} for w in W}
cur=None
for l in open(sys.argv[1],'rb'):
    s=l.decode('latin1');k=s.split(' ',1)[0]
    if k=='frame_end': cur=int(re.search(r' frame=(\d+)',s).group(1)); continue
    if k not in('camera_state','hdr_frame','screen_emission_additive_frame','shadow_replay_candidates'): continue
    m=re.search(r' frame=(\d+)',s); f=int(m.group(1)) if m else cur
    if f is None: continue
    for w,(a,b) in W.items():
        if a<=f<=b:
            for kk in acc[w]:
                mm=re.search(r' '+kk+r'=(-?[\d.]+)',s)
                if mm: acc[w][kk].append(float(mm.group(1)))
for w in W: print(w,{k:round(st.mean(v),2) if v else None for k,v in acc[w].items()})
