#!/usr/bin/env python3
"""The Split Raptor's big3 / big2 geometry in run417 (Run 129 A): per frame with a fx_engine_xtc_red_big3 record, each
fx_engine_xtc_red_big2 on its axis (dot >= 0.95) within 2 x the big3's size: distance / big3 size, size ratio, and
whether the co-located layer rule (engine_plumes_core.h merge_layers: d <= v_large, d <= 1.5 v_small, 0.35 <= ratio
<= 0.75) unfloors it, with the census's radius and value_eff. Streams only "engine_draw device=1 " rows of the read-only log.

Usage: python3 run417_raptor_pairs.py [run417] > run417_raptor_pairs_out.txt
"""
import collections
import glob
import math
import sys

RUN = sys.argv[1] if len(sys.argv) > 1 else "run417"
LOG = glob.glob(f"/tmp/x3-bottleX3-{RUN}/session-*.log")[0]


def kv(line):
    out = {}
    for tok in line.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out.setdefault(k, v)
    return out


frames = collections.defaultdict(list)
with open(LOG, "rb") as f:
    for raw in f:
        if not raw.startswith(b"engine_draw device=1 ") or b"_big" not in raw:
            continue
        d = kv(raw.decode("ascii", "replace"))
        name = d.get("name", "").split("\\")[-1]
        if name not in ("fx_engine_xtc_red_big3", "fx_engine_xtc_red_big2"):
            continue
        frames[int(d["frame"])].append((name, float(d["size"]), tuple(float(x) for x in d["origin"].split(",")),
                                        tuple(float(x) for x in d["axis"].split(",")), float(d.get("radius", "nan")),
                                        float(d.get("value_eff", "nan"))))
print(f"# {LOG}")
print("# frame big3_size radius big3_value_eff | per big2: d/v d/v_small ratio unfloored value_eff")
summary = collections.Counter()
for fr in sorted(frames):
    recs = frames[fr]
    for b3 in (r for r in recs if r[0].endswith("big3")):
        row = []
        for b2 in (r for r in recs if r[0].endswith("big2")):
            if sum(x * y for x, y in zip(b3[3], b2[3])) < 0.95:
                continue
            dv = math.dist(b3[2], b2[2]) / b3[1]
            if dv > 2.0:
                continue
            q = b2[1] / b3[1]
            ds = math.dist(b3[2], b2[2]) / b2[1]
            un = dv <= 1.0 and ds <= 1.5 and 0.35 <= q <= 0.75
            summary["unfloored" if un else "floored"] += 1
            row.append(f"{dv:.3f} {ds:.3f} {q:.3f} {int(un)} {b2[5]:.1f}")
        if row:
            print(f"{fr} {b3[1]:.2f} {b3[4]:.2f} {b3[5]:.1f} | " + " | ".join(row))
print(f"# big2 beside a big3: unfloored {summary['unfloored']}, floored {summary['floored']}")
