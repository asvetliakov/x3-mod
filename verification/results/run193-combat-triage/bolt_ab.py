# bolt_footprint A/B: per 300-frame window dt p50/p99, frames over 33 ms, draws, bolt draws (screen_emission bolt_flag),
# views, view_submit_p50, residual prepare_p50 and bolt_footprint draws/instances/us. usage: python3 bolt_ab.py x3m.log
import re,sys,statistics as st,collections
F=collections.defaultdict(lambda: {'dt':[],'dr':[],'bolt':[]}); R=collections.defaultdict(dict)
for l in open(sys.argv[1],'rb'):
    s=l.decode('latin1'); k=s.split(' ',1)[0]
    if k=='frame_end':
        d=dict(re.findall(r'(\w+)=(\S+)',s)); f=int(d['frame']); w=f//300*300+300
        if int(d['dt_ms'])>0: F[w]['dt'].append(int(d['dt_ms'])); F[w]['dr'].append(int(d['draws']))
    elif k=='screen_emission_additive_frame':
        m=re.search(r' frame=(\d+) .*bolt_flag=(\d+)',s)
        if m: F[int(m.group(1))//300*300+300]['bolt'].append(int(m.group(2)))
    elif k in('frame_phases','residual_phases','bolt_footprint','pass_phases'):
        d=dict(re.findall(r'(\w+)=(\S+)',s)); R[(int(d['frame'])+150)//300*300][k]=d
tot=0
for w in sorted(F):
    v=F[w]; dt=sorted(v['dt'])
    if not dt: continue
    o=sum(x>33 for x in dt); tot+=o
    fp=R[w].get('frame_phases',{}); rp=R[w].get('residual_phases',{}); bf=R[w].get('bolt_footprint',{}); pp=R[w].get('pass_phases',{})
    print(w,f"dt_p50={st.median(dt)} dt_p99={dt[int(.99*(len(dt)-1))]} over33={o} draws={st.median(v['dr'])} bolt_draws={st.mean(v['bolt']) if v['bolt'] else 0:.1f}",
          f"views={fp.get('views_p50')} vsub={fp.get('view_submit_p50_us')} prepare={rp.get('prepare_p50_us')} apply={pp.get('apply_p50_us')}",
          f"bf_draws={bf.get('draws')} bf_inst={bf.get('instances')} bf_us={bf.get('us')}")
print('frames_over_33ms_total',tot)
