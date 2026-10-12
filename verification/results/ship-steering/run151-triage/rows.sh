#!/bin/sh
# Run151 triage one-liners over C:\X3\x3m.log (native). Usage: sh rows.sh [log]
L=${1:-/mnt/c/X3/x3m.log}
echo "== sessions"; grep -c '^log_open' $L; grep -o '^proxy_identity sha256=[0-9a-f]\{8\}' $L; tac $L | grep -m1 -o '^hdr_frame device=1 frame=[0-9]*'
grep -m1 '^clock_anchor' $L | cut -c1-60; ls -l --time-style=full-iso /mnt/c/X3/X3Crash.log /mnt/c/X3/X3Crash.dmp | awk '{print $6,$7,$9}'
echo "== row kinds ship_*"; awk '{print $1}' $L | grep '^ship_' | sort | uniq -c
echo "== install"; grep -E '^ship_steering (installed)|^ship_steering_site|^ship_avoidance ' $L | cut -c1-140
echo "refused/late/mismatch:"; grep -cE 'ship_steering_refused|late_claim|bytes_mismatch' $L
echo "== summary aggregate"; grep '^ship_steering_summary' $L | python3 -c '
import sys
S={};N=0;mx={};vals={"p50":[],"p99":[],"cps":[],"ships":[]};bad=0
for l in sys.stdin:
  N+=1;d=dict(t.split("=",1) for t in l.split()[1:] if "=" in t)
  for k,v in d.items():
    if k.startswith("pass_") or k in("calls","handled","clamp_calls","clamp_applied","clamp_stale","redirected","avoid_calls","conflicts","detours","unresolved","snapshot_overflow","snapshots","emergency","e_calls","e_suppressed","part_models","part_fallback","part_invalid"): S[k]=S.get(k,0)+int(v)
  for k in("handler_us_max","snapshot_max","handler_us_p99","ships"): mx[k]=max(mx.get(k,0),float(d[k]))
  vals["p50"].append(float(d["handler_us_p50"]));vals["p99"].append(float(d["handler_us_p99"]));vals["cps"].append(float(d["calls_per_ship_s"]));vals["ships"].append(int(d["ships"]))
  if float(d["handler_us_p99"])>float(d["handler_us_max"]): bad+=1
  last=d
print("rows",N);print({k:v for k,v in S.items()})
print("max",mx)
import statistics as st
for k,v in vals.items(): print(k,"median",st.median(v),"min",min(v),"max",max(v))
print("rows_p99_gt_max",bad);print("last handled_by_order_total",last["handled_by_order_total"],"stuck_total",last["stuck_total"])'
echo "== stuck"; grep -c '^ship_steering_stuck' $L
echo "== census/station"; grep -E '^ship_avoid_(census|station)' $L | cut -c1-200
echo "== decision per class"; grep '^ship_steering_decision' $L | python3 -c '
import sys,collections
seen={};c=collections.Counter()
for l in sys.stdin:
  d=dict(t.split("=",1) for t in l.split()[1:] if "=" in t);k=d.get("class");c[k]+=1
  if k not in seen: seen[k]=l.strip()[:300]
print(dict(c))
for v in seen.values(): print(v)'
echo "== detour kinds/rate"; grep '^ship_avoid_detour' $L | python3 -c '
import sys,collections
kc=collections.Counter();fr=[]
for l in sys.stdin:
  d=dict(t.split("=",1) for t in l.split()[1:] if "=" in t);kc[d.get("kind")]+=1;fr.append(int(d["frame"]))
print(dict(kc),"frames",min(fr),max(fr))'
grep -m3 '^ship_avoid_detour' $L | cut -c1-260
echo "== exceptions"; grep -c 'exception=' $L; grep 'exception=' $L | cut -c1-200 | sort | uniq -c | head
echo "== roll sign flips per ship (e third component), large classes"; grep '^ship_steering_decision' $L | python3 -c '
import sys,collections
r=collections.defaultdict(list)
for l in sys.stdin:
  d=dict(t.split("=",1) for t in l.split()[1:] if "=" in t);r[(d["class"],d["obj"])].append(int(d["e"].split(",")[2]))
for (c,o),v in sorted(r.items()):
  nz=[x for x in v if x];fl=sum(1 for a,b in zip(nz,nz[1:]) if (a>0)!=(b>0))
  if c in("M1","M2","TL","M7","TP","TS") and len(v)>2: print(c,o,"n",len(v),"nonzero_roll",len(nz),"flips",fl,"max|roll|",max(map(abs,v)))'
echo "== detours per second (session 331.8 s; frames 57245)"; grep -c '^ship_avoid_detour' $L
echo "== warn/error/fail row kinds: this log vs x3m.prev.log"
for f in $L /mnt/c/X3/x3m.prev.log; do awk '{print $1}' $f | grep -iE 'warn|err|fail|refus|reject|invalid' | sort | uniq -c > /tmp/claude-1000/k_$(basename $f).txt; done
diff /tmp/claude-1000/k_x3m.prev.log.txt /tmp/claude-1000/k_x3m.log.txt
grep -o 'source_commit=[0-9a-f]\{8\}' /mnt/c/X3/x3m.prev.log | head -1
echo "== new kinds detail"; grep '^taa_invalidate' $L | grep -o 'reason=[a-z_]*' | sort | uniq -c; grep -m1 '^taa_invalidate' $L | cut -c1-200; grep -m1 '^screen_emission_additive_refused_window' $L | cut -c1-200
grep '^taa_invalidate' $L | awk '{print $4}' | sort | uniq -c
