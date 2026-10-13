"""Run158 triage: summary counters, small-ship gate rows, detours, slip, E verdicts, join rows. Usage: python3 -I summary.py <extracted rows>"""
import re,sys,math,statistics as St,collections as C
L=open(sys.argv[1],errors='replace').read().splitlines()
P=lambda l:dict(re.findall(r'(\w+)=(\S+)',l))
def q(v,p=1):
    v=sorted(v); f='n=%d min=%.{0}f p25=%.{0}f med=%.{0}f p75=%.{0}f p95=%.{0}f max=%.{0}f'.format(p)
    return f%(len(v),v[0],v[len(v)//4],St.median(v),v[3*len(v)//4],v[int(.95*(len(v)-1))],v[-1]) if v else 'n=0'
S=[P(l) for l in L if l.startswith('ship_steering_summary')]
CUM=[k for k in S[-1] if re.fullmatch(r'-?[\d.]+',S[-1][k]) and k!='frame' and all(float(b.get(k,0))>=float(a.get(k,0)) for a,b in zip(S,S[1:])) and float(S[-1][k])>0 and k.startswith(('e_s','e_small','member','avoid_excluded_member','gate_capital_join','gate_capital_slowed','gate_capital_fallback','gate_capital_not_kept','join_eval','join_pred','join_slots_'))]
print('== 1 intervals',len(S),'interval_ms sum',sum(float(d['interval_ms']) for d in S))
print('cumulative counters (last):',{k:S[-1][k] for k in CUM})
skip={'frame','interval_ms','claims','ships','stuck_total','handled_by_order_total','calls_per_ship_s','snapshot_max','join_slots'}|set(CUM)
tot={k:sum(float(d.get(k,0)) for d in S) for k in S[-1] if k not in skip and not k.startswith('handler_us') and re.fullmatch(r'-?[\d.]+',S[-1][k])}
print('per-interval sums:',{k:int(v) for k,v in tot.items() if v})
print('join_slots peak',max(int(d.get('join_slots',0)) for d in S),'stuck_total(last)',S[-1]['stuck_total'],'stuck rows',sum(l.startswith('ship_steering_stuck') for l in L))
print('handled_by_order_total(last)',S[-1]['handled_by_order_total'])
p99=[float(d['handler_us_p99']) for d in S];mx=[float(d['handler_us_max']) for d in S]
print('handler_us p50 med',St.median(float(d['handler_us_p50']) for d in S),'p99 med',St.median(p99),'p99 max',max(p99),'p99>5:',sum(x>5 for x in p99),'max_us',max(mx),'max>50:',sum(x>50 for x in mx))
print('refused rows (excl. summary counters)',sum(re.search(r'refused(?!=0)',l) is not None and not l.startswith('ship_steering_summary') for l in L))
# 4 small ships
tr=[P(l) for l in L if l.startswith('ship_gate_transition')];ap=[P(l) for l in L if l.startswith('ship_gate_approach')]
t2=[t for t in tr if t['capital']=='0'];a2=[a for a in ap if a['capital']=='0']
print('== 4 transitions',len(tr),'small',len(t2),'class',dict(C.Counter(t['class'] for t in tr)),'dock_blocked',dict(C.Counter(t['dock_blocked'] for t in tr)))
print(' err_B_deg',q([float(t['err_B_deg']) for t in t2]),'>18.2:',sum(float(t['err_B_deg'])>18.2 for t in t2))
print(' cross_heading_err',q([float(a['cross_heading_err_deg']) for a in a2]),'cross_v',q([float(a['cross_v_ms']) for a in a2]))
for cl in sorted({a['class'] for a in a2}): print('  %s cross_to_launch_s %s'%(cl,q([float(a['cross_to_launch_ms'])/1e3 for a in a2 if a['class']==cl])))
W=[P(l) for l in L if l.startswith('ship_avoid_engine_warn ')]
print(' engine warn rows',len(W),'other class 18 (gate)',sum(w['class']=='18' for w in W))
# 5 detours
det=[P(l) for l in L if l.startswith('ship_avoid_detour')]
cls={}
for l in L:
    if l.startswith('ship_trace frame='):
        m=re.search(r'obj=(\S+) class=(\S+)',l); cls[m.group(1)]=m.group(2)
caps={o for o,c in cls.items() if c in('M1','M2','TL','M7','M0')}
print('== 5 detours',len(det),'by kind',dict(C.Counter(d['kind'] for d in det)))
v=[d for d in det if d['obstacle'] in caps and d['obj'] not in caps]
print(' small-vs-capital detour rows',len(v),'miss<C',sum(float(d['miss_m'])<float(d['C_m']) for d in v),'miss',q([float(d['miss_m']) for d in v],0))
cc=[d for d in det if d['obstacle'] in caps and d['obj'] in caps]
print(' capital-vs-capital (traced capitals) rows',len(cc),'miss<C',sum(float(d['miss_m'])<float(d['C_m']) for d in cc))
# slip
sl=[];sl4t=[];f14=[]
for l in L:
    if not l.startswith('ship_trace frame='): continue
    m=re.search(r' order=(\S+) .*pos=(\S+) fwd=(\S+) .* v=(\d+) .*point=(\S+) .*slip=(\S+)',l)
    if not m: continue
    o,pos,fwd,vv,pt,s=m.groups(); vv=int(vv)
    if vv<=0: continue
    s=[int(x) for x in s.split(',')]; r=math.sqrt(sum(x*x for x in s))/vv
    if o=='0x4':
        sl.append(r); pos=[float(x) for x in pos.split(',')];pt=[float(x) for x in pt.split(',')];fw=[float(x) for x in fwd.split(',')]
        d=[a-b for a,b in zip(pt,pos)];n=math.sqrt(sum(x*x for x in d))
        if n and math.degrees(math.acos(max(-1,min(1,sum(a*b for a,b in zip(d,fw))/n))))>10: sl4t.append(r)
print(' slip present in trace rows with v>0, order 4: |slip|/v',q(sl,3)); print(' order-4 turning (aim>10 deg) |slip|/v',q(sl4t,3))
# E verdicts
E=[P(l) for l in L if l.startswith('ship_avoid_e_verdict')]
print('== 3 e_verdict rows',len(E),'verdicts',dict(C.Counter(e['verdict'] for e in E)))
print(' by A_order x verdict',C.Counter((e['A_order'],e['verdict']) for e in E).most_common(12))
