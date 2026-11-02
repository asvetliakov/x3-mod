#!/usr/bin/env python3
"""Bolt row sums for FL run 23 vs AP ref. Usage: bolts.py LOG"""
import re,sys
from collections import Counter
KV=re.compile(r'(\w+)=(\S+)'); w=Counter(); fr=Counter(); fp=None; other=Counter(); ex={}
for line in open(sys.argv[1],errors='replace'):
    k=line.split(' ',1)[0]
    if k=='screen_emission_additive_refused_window':
        for a,b in KV.findall(line):
            if a not in('device','frame') : w[a]+=int(b)
    elif k=='screen_emission_additive_frame':
        for a,b in KV.findall(line):
            if a in('admitted','refused','bolt_flag','bolt_flag_refused'): fr[a]+=int(b)
    elif k=='bolt_footprint': fp=dict(KV.findall(line)); 
    elif k.startswith(('bolt_','screen_emission_additive')) and k!='bolt_footprint_hist':
        other[k]+=1; ex.setdefault(k,line.strip()[:400])
print("window sums nonzero:",{a:b for a,b in w.items() if b})
print("frame sums:",dict(fr))
print("bolt_footprint last session:",{a:b for a,b in (fp or {}).items() if a.startswith('session') or a=='buffer_bytes'})
for k,c in other.items(): print(c,ex[k])
