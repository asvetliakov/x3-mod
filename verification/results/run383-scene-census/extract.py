#!/usr/bin/env python3
"""Compact evidence for run383 (Run 108 A, scene_graph_census, Mayhem 3).
Usage: python3 extract.py [LOG] -> writes summary.json beside this script."""
import json, re, sys, statistics as st, os
LOG = sys.argv[1] if len(sys.argv) > 1 else "/tmp/x3-bottleX3-run383/session-20260929-101037-212.log"
KV = re.compile(r"(\S+?)=(\S+)")
def num(v):
    try: return int(v)
    except ValueError:
        try: return float(v)
        except ValueError: return v
census, loops, fends, olife = [], [], [], []
with open(LOG, errors="replace") as f:
    for line in f:
        tag = line.split(" ", 1)[0]
        if tag == "scene_graph_census": census.append(dict(KV.findall(line)))
        elif tag == "loop_phases": loops.append(dict(KV.findall(line)))
        elif tag == "frame_end": fends.append(dict(KV.findall(line)))
        elif tag.startswith("object_lifetime"): olife.append((tag, line.strip()[:400]))
rows = []
for c in census:
    r = {k: num(c.get(k, "-")) for k in ("frame","engine_nodes","registry_live","inserts","tasks","scenes","unattached","sampled_nodes","capped","walk_us")}
    r["bodies"] = {c[f"b{i}"].rsplit(":",1)[0]: int(c[f"b{i}"].rsplit(":",1)[1]) for i in range(8) if f"b{i}" in c}
    r["insert_callers"] = {c[f"i{i}"].rsplit(":",1)[0]: int(c[f"i{i}"].rsplit(":",1)[1]) for i in range(8) if f"i{i}" in c}
    rows.append(r)
PAIR = "00486d7d/0041f332"
deltas = []
for a, b in zip(rows, rows[1:]):
    if not isinstance(a["engine_nodes"], int) or not isinstance(b["engine_nodes"], int): continue
    df = b["frame"] - a["frame"]
    deltas.append({"frame": b["frame"], "d_engine_nodes": b["engine_nodes"]-a["engine_nodes"],
        "inserts_window": b["inserts"], "d_unattached": b["unattached"]-a["unattached"],
        "pair_inserts_window": b["insert_callers"].get(PAIR,0),
        "pair_rate_per_frame": b["insert_callers"].get(PAIR,0)/df})  # inserts and i* are per 300-frame window (reset each row)
lp = [{"frame": int(l["frame"]), "cutevent_p50_us": int(l["cutevent_p50_us"]), "cutevent_p95_us": int(l["cutevent_p95_us"]),
       "region_p50_us": int(l["region_p50_us"]), "input_p50_us": int(l["input_p50_us"]),
       "pre_render_us": num(l["pre_render_us"]) if "pre_render_us" in l else None} for l in loops]
ft = []
for s in range(0, len(fends), 300):
    w = [int(x["dt_ms"]) for x in fends[s:s+300] if x.get("dt_ms","0").isdigit() and int(x["dt_ms"])>0]
    if w:
        p = st.median(w); ft.append({"frame": int(fends[s]["frame"]), "dt_p50_ms": p, "fps_p50": round(1000/p,1), "dt_max_ms": max(w)})
en = {r["frame"]: r["engine_nodes"] for r in rows if isinstance(r["engine_nodes"], int)}
fr = sorted(en)
entry = next((b for a,b in zip(fr,fr[1:]) if en[b]-en[a] > 100), None)
left = next((f for f in fr if entry and f > entry and en[f] <= 210), None)
sector = [f for f in fr if entry and f >= entry and (left is None or f < left)]
growth = (en[sector[-1]]-en[sector[0]])/(sector[-1]-sector[0]) if len(sector)>1 else None
lpm = {x["frame"]: x["cutevent_p50_us"] for x in lp}
pts = [(en[f], lpm[f]) for f in sector if f in lpm]
slope = None
if len(pts) > 2:
    mx = st.mean(p[0] for p in pts); my = st.mean(p[1] for p in pts)
    slope = sum((x-mx)*(y-my) for x,y in pts)/sum((x-mx)**2 for x,_ in pts)
samp = sum(r["sampled_nodes"] for r in rows if isinstance(r["sampled_nodes"], int))
m1 = sum(v for r in rows for k,v in r["bodies"].items() if k == "model-1")
sd = [d for d in deltas if d["frame"] in sector[1:]]
pair_share = sum(d["pair_inserts_window"] for d in sd)/max(1,sum(d["inserts_window"] for d in sd))
diverge = [r["frame"] for r in rows if r["registry_live"] != r["engine_nodes"] and r["engine_nodes"] != "-"]
conc = {
 "sector_entry_frame": entry, "sector_left_frame": left,
 "net_node_growth_per_frame_in_sector": growth,
 "pair_rate_per_frame_in_sector_mean": st.mean(d["pair_rate_per_frame"] for d in sd) if sd else None,
 "pair_share_of_sector_inserts": pair_share,
 "registry_live_equals_engine_nodes_all_rows": not diverge, "diverging_frames": diverge,
 "model1_share_of_sampled_unattached": m1/samp if samp else None, "sampled_unattached_total": samp,
 "cutevent_us_per_node": slope, "cutevent_ns_per_node": slope*1000 if slope is not None else None,
 "negative_engine_node_deltas": [d for d in deltas if d["d_engine_nodes"] < 0],
 "object_lifetime_rows": [t for t,_ in olife],
}
out = {"log": os.path.basename(LOG), "census": rows, "deltas_300": deltas, "loop_phases": lp,
       "frame_time_300": ft, "object_lifetime_last": olife[-1][1] if olife else None, "conclusions": conc}
json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "summary.json"), "w"), indent=1)
