#!/usr/bin/env python3
"""Frame 5437 of run412: every draw with the engine effect pair (vs d5e1c753 / ps 8360f422) or logged blending on,
with its scoped model resolved to a body name through the session's cull_census rows (model -> body=), its
blend/Z-write (motion_route), node +0x130 flags (object_context) and screen bounds (object_bounds); then every
draw whose bounds contain the own nozzle (projected fx_engine_xtc_red_nor origin, run412_engine_disc.read_log).
Usage: python3 run412_notjet_5437.py > run412_notjet_5437_out.txt"""
import os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import run412_engine_disc as m

FR = 5437
names, rows = {}, {}
pat = re.compile(rb"^(draw|motion_route|object_context|object_bounds) device=1 frame=%d " % FR)
with open(m.LOG, "rb") as f:
    for raw in f:
        if raw.startswith(b"cull_census "):
            mm = re.search(rb"model=([0-9a-f]+) .* body=(\S+)", raw)
            if mm: names[mm.group(1).decode()] = mm.group(2).decode()
        elif pat.match(raw):
            t = raw.decode("ascii", "replace").split()
            d = dict(x.split("=", 1) for x in t[1:] if "=" in x)
            rows.setdefault(int(d["index"]), {})[t[0]] = d
m.FRAMES = [FR]
cams, evs, draws = m.read_log()
nx, ny, _ = next(m.project(cams[FR], p) for n, s, p in draws[FR] if n.endswith("_nor"))
print("# frame %d draws %d; own nozzle at (%.0f, %.0f)" % (FR, len(rows), nx, ny))
print("# index prims vs ps body zwrite blend src dst flags130 bounds")
hits = []
for i in sorted(rows):
    r = rows[i]; d = r["draw"]; mr = r.get("motion_route", {}); c = r.get("object_context", {}); b = r.get("object_bounds")
    body = names.get(c.get("model", ""), c.get("model", "-"))
    bb = "-" if not b else "%s,%s..%s,%s" % (b["sx0"], b["sy0"], b["sx1"], b["sy1"])
    if b and float(b["sx0"]) <= nx <= float(b["sx1"]) and float(b["sy0"]) <= ny <= float(b["sy1"]):
        hits.append((i, d["primitives"], d["ps"][:8], body, mr.get("zwrite"), mr.get("blend")))
    if d["vs"].startswith("d5e1c753") or mr.get("blend") == "1" or "motion_route" not in r:
        print(i, d["primitives"], d["vs"][:8], d["ps"][:8], body, mr.get("zwrite", "?"), mr.get("blend", "?"),
              mr.get("src", "?"), mr.get("dst", "?"), c.get("flags130", "-"), bb)
print("# draws whose bounds contain the own nozzle:", hits)
