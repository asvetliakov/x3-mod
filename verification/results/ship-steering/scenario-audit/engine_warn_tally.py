#!/usr/bin/env python3
"""Scenario audit: who the engine's own warn named at a traced ship's first 0x12 row (ship_avoid_engine_warn), by
subject class (from that subject's ship_trace rows) and obstacle class/code; plus the order the subject was on in its
last trace row on an order other than 0 or 0x12 before the warn. One streaming pass over x3m.log."""
import re, sys
from collections import Counter, defaultdict
path = sys.argv[1] if len(sys.argv) > 1 else "/mnt/c/X3/x3m.log"
kv = re.compile(r"(\w+)=([^ \n]+)")
subj_class = {}
last_order = {}
rows = []
with open(path, "rb") as fh:
    for raw in fh:
        if raw.startswith(b"ship_trace frame="):
            d = dict(kv.findall(raw.decode("ascii", "replace")))
            o = d.get("obj")
            if o:
                subj_class[o] = d.get("class", "?")
                if d.get("order") not in ("0x12", "0x0"):
                    last_order[o] = (d.get("order"), d.get("tgt_class"))
        elif raw.startswith(b"ship_avoid_engine_warn "):
            d = dict(kv.findall(raw.decode("ascii", "replace")))
            o = d.get("obj")
            rows.append((o, subj_class.get(o, "?"), last_order.get(o, ("?", "?")), d.get("class"), d.get("code"), float(d.get("d_m", "nan"))))
print(f"log={path} rows={len(rows)}")
by = Counter((r[1], r[2][0], r[2][1], r[3], r[4]) for r in rows)
print("subject_class prev_order prev_tgt_class -> obstacle_class code : n")
for k, n in sorted(by.items(), key=lambda x: -x[1]):
    print(f"  {k[0]:4} {k[1]:5} tgt_class={k[2]:3} -> class={k[3]} code={k[4]} : {n}")
gate_rows = [r for r in rows if r[3] == "18"]
print(f"gate-named warns: {len(gate_rows)}; subjects {sorted(set(r[1] for r in gate_rows))}; d_m {sorted(round(r[5]) for r in gate_rows)}")

# Second pass: duration (game ms) of each traced 0x12 leg, keyed by the obstacle class the warn row named for it.
leg_named = {}
cur = {}  # obj -> (order, start_t, named_class)
legs = defaultdict(list)
with open(path, "rb") as fh:
    for raw in fh:
        if raw.startswith(b"ship_avoid_engine_warn "):
            d = dict(kv.findall(raw.decode("ascii", "replace")))
            leg_named[d.get("obj")] = (d.get("class"), subj_class.get(d.get("obj"), "?"))
        elif raw.startswith(b"ship_trace frame="):
            d = dict(kv.findall(raw.decode("ascii", "replace")))
            o, order, t = d.get("obj"), d.get("order"), int(d.get("t_ms", "0"))
            c = cur.get(o)
            if c is None or c[0] != order:
                if c is not None and c[0] == "0x12":
                    named = c[2] or ("none", subj_class.get(o, "?"))
                    legs[named].append(c[3] - c[1])
                cur[o] = [order, t, leg_named.pop(o, None) if order == "0x12" else None, t]
            else:
                c[3] = t
print("traced 0x12 legs by (obstacle class named, subject class): n, total s, median s")
for k, v in sorted(legs.items(), key=lambda x: -sum(x[1])):
    v = sorted(v)
    print(f"  {k}: n={len(v)} total={sum(v)/1000:.1f} s median={v[len(v)//2]/1000:.1f} s max={v[-1]/1000:.1f} s")
