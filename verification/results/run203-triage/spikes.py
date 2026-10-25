# top frame_end dt_ms spikes from pass1 json, with non-routine row kinds within +-2 frames
import json,sys,statistics
d=json.load(open(sys.argv[1]));dt={int(k):v for k,v in d['dt'].items()};pf={int(k):v for k,v in d['perframe'].items()}
routine={k for k,c in d['kinds'] if c>30000}
v=sorted(dt.values());n=len(v);print('frames',n,'median',v[n//2],'p99',v[int(n*.99)],'max',v[-1],'>=50ms',sum(x>=50 for x in v),'>=100ms',sum(x>=100 for x in v))
twin={764,4752,7926,9782,27489,35392}
for f,ms in sorted(dt.items(),key=lambda x:-x[1])[:int(sys.argv[2])]:
    ks={}
    for g in range(f-2,f+3):
        for k,c in pf.get(g,{}).items():
            if k not in routine and k not in('constant','sampler','state','camera_state'): ks[k]=ks.get(k,0)+c
    print(f,ms,'TWIN' if any(abs(f-t)<=2 for t in twin) else '',sorted(ks.items(),key=lambda x:-x[1])[:12])
