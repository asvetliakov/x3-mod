#!/usr/bin/env python3
"""Run 128 A flight (run416) and Run 126 flight (run414): the log rows of the F8 frames (and each frame before),
parsed once into small JSON (run416_log.json / run414_log.json beside this script). Session logs read-only by prefix.
Usage: python3 run416_log.py [run416|run414]
"""
import glob, json, os, sys
RUN = sys.argv[1] if len(sys.argv) > 1 else "run416"
SESSION = "/tmp/x3-bottleX3-" + RUN
LOG = glob.glob(os.path.join(SESSION, "session-*.log"))[0]
SETS = {"run416": [3788, 6265, 7304], "run414": [4048, 4832]}[RUN]
FRAMES = {f + k for f in SETS for k in range(-1, 8)}
KINDS = (b"camera_state device=1 ", b"hdr_frame device=1 ", b"engine_draw device=1 ", b"engine_stage device=1 ")
def kv(line):
    out = {}
    for tok in line.split()[1:]:
        if "=" in tok:
            k, v = tok.split("=", 1); out.setdefault(k, v)
    return out
out = {}
with open(LOG, "rb") as f:
    for raw in f:
        if not raw.startswith(KINDS):
            continue
        line = raw.decode("ascii", "replace"); d = kv(line)
        try:
            fr = int(d.get("frame", -1))
        except ValueError:
            continue
        if fr not in FRAMES:
            continue
        kind = line.split(" ", 1)[0]; slot = out.setdefault(str(fr), {})
        if kind == "engine_draw":
            d = {k: d[k] for k in ("index", "name", "z", "s", "size", "origin", "axis", "handle", "value_eff", "flags", "verdict", "cluster") if k in d}
            d["name"] = d["name"].split("\\")[-1]
            slot.setdefault(kind, []).append(d)
        elif kind == "camera_state":
            slot[kind] = {k: d[k] for k in d if (k[:1] in "rp" and k[1:2].isdigit()) or k == "t"}
        elif kind == "hdr_frame":
            slot[kind] = {k: d[k] for k in ("ev", "ev_adapted", "ev_target") if k in d}
        elif kind == "engine_stage":
            slot[kind] = {k: d[k] for k in ("records", "nozzles", "discs", "capped", "faded", "merged", "floored", "far",
                                            "far_jets", "culled_small", "view_rule") if k in d}
json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), RUN + "_log.json"), "w"), separators=(",", ":"))
print("frames", len(out), "draws", sum(len(v.get("engine_draw", [])) for v in out.values()))
