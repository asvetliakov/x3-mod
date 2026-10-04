#!/usr/bin/env python3
"""Run 125 A flight (run413): the log rows of the F8 frames (and each frame before), parsed once into a small JSON.

Reads /tmp/x3-bottleX3-run413/session-*.log (read-only, ~300 MB) by line prefix and keeps, for the frames of the four
F8 sets and the frame before each: camera_state (rows r, t, p00, p11), hdr_frame (ev_adapted), engine_draw (all
fields used), engine_stage counters, engine_light_frame, hull_lightmap_frame (the fields the run413 scripts use).
Usage: python3 run413_log.py   -> run413_log.json (beside this script)
"""
import glob, json, os
SESSION = "/tmp/x3-bottleX3-run413"
LOG = glob.glob(os.path.join(SESSION, "session-*.log"))[0]
SETS = [1288, 4197, 6044, 11208]
FRAMES = {f + k for f in SETS for k in range(-1, 8)}
KINDS = (b"camera_state device=1 ", b"hdr_frame device=1 ", b"engine_draw device=1 ", b"engine_stage device=1 ",
         b"engine_light_frame device=1 ", b"hull_lightmap_frame device=1 ")
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
        line = raw.decode("ascii", "replace")
        d = kv(line)
        try:
            fr = int(d.get("frame", -1))
        except ValueError:
            continue
        if fr not in FRAMES:
            continue
        kind = line.split(" ", 1)[0]
        slot = out.setdefault(str(fr), {})
        if kind == "engine_draw":
            d = {k: d[k] for k in ("index", "name", "z", "s", "size", "origin", "axis", "handle", "value_eff", "flags") if k in d}
            d["name"] = d["name"].split("\\")[-1]
            slot.setdefault(kind, []).append(d)
        elif kind == "camera_state":
            slot[kind] = {k: d[k] for k in d if k[:1] in "rtp" and k[1:2].isdigit() or k == "t"}
        elif kind == "hdr_frame":
            slot[kind] = {k: d[k] for k in ("ev", "ev_adapted", "ev_target")}
        elif kind == "engine_stage":
            slot[kind] = {k: d[k] for k in ("records", "nozzles", "discs", "capped", "faded", "culled_small", "culled_behind",
                                            "culled_idle", "skipped_other_view", "merged", "steering", "far", "floored")}
        elif kind == "hull_lightmap_frame":
            slot[kind] = {k: d[k] for k in ("gain", "admitted")}
        elif kind == "engine_light_frame":
            slot[kind] = {k: d[k] for k in ("ships", "ships_drawn", "records", "main", "other_view")}
json.dump(out, open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "run413_log.json"), "w"), separators=(",", ":"))
print("frames", len(out), "draws", sum(len(v.get("engine_draw", [])) for v in out.values()))
