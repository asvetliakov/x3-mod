#!/usr/bin/env python3
# Agent Bash calls (UTC timestamps) from Claude transcripts between two ISO bounds; used to date host load against a flight.
# usage: host_activity_scan.py 2026-09-22T22:50:00 2026-09-22T22:53:00
import json,glob,os,sys
from datetime import datetime,timezone,timedelta
lo,hi=sys.argv[1],sys.argv[2]  # UTC ISO prefix bounds
for p in glob.glob('/Users/asvetl/.claude/projects/-Users-asvetl-x3-mod/*/subagents/*.jsonl')+glob.glob('/Users/asvetl/.claude/projects/-Users-asvetl-x3-mod/*.jsonl'):
    if os.path.getmtime(p) < datetime(2026,9,23,2,40).timestamp(): continue
    for l in open(p,errors='replace'):
        try: j=json.loads(l)
        except: continue
        ts=j.get('timestamp','')
        if not (lo<=ts<=hi): continue
        m=j.get('message',{})
        c=m.get('content') if isinstance(m,dict) else None
        if isinstance(c,list):
            for b in c:
                if isinstance(b,dict) and b.get('type')=='tool_use' and b.get('name')=='Bash':
                    print(ts[11:19],os.path.basename(p)[:22],b['input'].get('command','')[:160].replace('\n',' '))
