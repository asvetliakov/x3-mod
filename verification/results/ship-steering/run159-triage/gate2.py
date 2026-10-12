import sys,re,collections as C
L=open(sys.argv[1]).read().splitlines()
fr=lambda l:int(re.search(r' frame=(\d+)',l).group(1))
# Run159: window and capital set from the arguments / rows (Run158 used fixed ids and 60000-77000)
W=(int(sys.argv[2]),int(sys.argv[3])) if len(sys.argv)>3 else (64000,80100)
caps={re.search(r' obj=(\S+)',l).group(1) for l in L if (l.startswith('ship_trace frame=') and re.search(r' class=(M1|M2|TL|M7|M0) ',l)) or (l.startswith('ship_gate_approach ') and ' capital=1 ' in l)}
seta=C.Counter(); 
for l in L:
  if l.startswith('engine_stage') and 'seta=' in l:
    f=fr(l)
    if W[0]<=f<=W[1]: seta[re.search(r'seta=(\d)',l).group(1)]+=1
print('seta frames in window',dict(seta))
fe={fr(l):int(re.search(r'elapsed_ms=(\d+)',l).group(1)) for l in L if l.startswith('frame_end ')}
for f in sorted(int(re.search(r' frame=(\d+)',l).group(1)) for l in L if l.startswith('ship_gate_approach ') and ' capital=1 ' in l and 'cross_to_launch_ms=0 ' not in l): print('launch frame',f,'real s %.1f'%(fe.get(f,0)/1e3))
det=[dict(re.findall(r'(\w+)=(\S+)',l)) for l in L if l.startswith('ship_avoid_detour')]
d=[x for x in det if W[0]<=int(x['frame'])<=W[1] and (x['obj'] in caps or x['obstacle'] in caps)]
cc=[x for x in d if x['obj'] in caps and x['obstacle'] in caps]
print('detours in window touching these caps',len(d),'cap-vs-cap',len(cc),C.Counter((x['obj'],x['obstacle'],x['kind'],x['obstacle_order']) for x in cc))
print(' cap-vs-cap miss<C',sum(float(x['miss_m'])<float(x['C_m']) for x in cc),'C',sorted({x['C_m'] for x in cc})[:6],'miss min',min([float(x['miss_m']) for x in cc],default=None))
#print('0x3a2b1b30 as obstacle: orders',C.Counter((x['obstacle_order'],x['leader']) for x in det if x['obstacle']=='0x3a2b1b30'),'frames',min([int(x['frame']) for x in det if x['obstacle']=='0x3a2b1b30'],default=0),max([int(x['frame']) for x in det if x['obstacle']=='0x3a2b1b30'],default=0))
ev=[dict(re.findall(r'(\w+)=(\S+)',l)) for l in L if l.startswith('ship_avoid_e_verdict')]
e=[x for x in ev if W[0]<=int(x['frame'])<=W[1]]
print('E verdict rows in window',len(e),'B class',C.Counter(x['B_class'] for x in e),'B capitals (code M1/M2/...)',C.Counter((x['B'],x['B_code'],x['B_order'],x['verdict']) for x in e if x['B_code'] in('M1','M2','TL','M7','M0')).most_common(12))
S=[dict(re.findall(r'(\w+)=(\S+)',l)) for l in L if l.startswith('ship_steering_summary')]
for k in('unresolved','unresolved_episodes','unresolved_inside_turn','unresolved_behind','conflict_episodes','emergency','gate_p2_capital','gate_capital_aligned','gate_capital_kept','gate_capital_slowed','gate_capital_fallback_pred','gate_capital_not_kept_mode','join_slots'):
  a=[(int(x['frame']),float(x.get(k,0))) for x in S]
  for lo,hi,tag in ((0,30000,'gate1 f<30000'),(W[0],W[1],'gate2')):
    v=[y for f,y in a if lo<=f<=hi]
    print(k,tag,'sum',sum(v),'max',max(v) if v else '-')
