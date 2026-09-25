"""Run336 shadow pop triage: parse shadow rows into compact per-frame tables.
Usage: python3 parse_rows.py /tmp/x3-bottleX3-run336/session-*.log
Prints only summaries."""
import sys, re, collections, statistics
KINDS = ('shadow_replay_depth','shadow_retention_frame','shadow_replay_candidates','shadow_retention_summary','shadow_retention_flush','chase_camera')
rows = collections.defaultdict(list)
with open(sys.argv[1], errors='replace') as f:
    for line in f:
        k = line.split(' ',1)[0]
        if k in KINDS:
            d = dict(t.split('=',1) for t in line.split()[1:] if '=' in t)
            rows[k].append(d)
def num(v):
    try: return float(v)
    except: return None
for k in KINDS: print(k, len(rows[k]))
import json
out = sys.argv[2] if len(sys.argv) > 2 else None
if out:
    with open(out,'w') as o:
        json.dump({k: rows[k] for k in KINDS}, o)
