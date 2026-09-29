# collide_census / collide_memo per 300-frame window vs frame_phases pre_render (run379).
import re,glob
L=glob.glob('/tmp/x3-bottleX3-run379/session-*.log')[0]
kv=lambda s:dict(re.findall(r'(\w+)=(\S+)',s))
C={};M={};P={}
for line in open(L,errors='replace'):
    if line.startswith('collide_census '): d=kv(line);C[int(d['frame'])//300]=d
    elif line.startswith('collide_memo device'): d=kv(line);M[int(d['frame'])//300]=d
    elif line.startswith('frame_phases '): d=kv(line);P[int(d['frame'])//300 -1]=d
print('win  pre_ms views_ms  p1_pairs_p50 p1_rej_p50 p2_cands_p50  memo_queries/frame memo_hits')
for k in sorted(set(C)|set(M)):
    c=C.get(k,{});m=M.get(k,{});p=P.get(k,{})
    mq=int(m.get('queries',0))/int(m.get('frames',1)) if m else 0
    print(f"{k*300:5d} {int(p.get('pre_render_p50_us',0))/1e3:6.1f} {int(p.get('views_p50_us',0))/1e3:6.1f} {c.get('p1_pairs_p50','-'):>12} {c.get('p1_rejected_p50','-'):>10} {c.get('p2_cands_p50','-'):>12} {mq:10.0f} {m.get('hits','-')}")
