#!/usr/bin/env python3
"""Co-located engine records in a flight: the census behind the plume merge rule (engine_plumes_core.h merge_layers).

Streams the engine_draw rows of /tmp/x3-bottleX3-<run>/session-*.log (read-only; only rows starting with
"engine_draw device=1 ") and, per frame, pairs every two records whose axes are parallel (dot >= 0.95; the rows carry
no parent, so a ship is approximated by the identical axis of its nozzles) and whose origins lie within 2 x the larger
record's size. Per pair: the distance over the larger size (d / v) and the smaller size over the larger (v2 / v1).
Prints a histogram over (body names, d / v bin, size ratio bin) for distinct (frame-free) name pairs. The size-ratio
bins follow the merge rule since Run 125: under 0.35 (a nozzle of its own: kept), 0.35..0.75 (a layer: merged when
d <= v_large), over 0.75 (twins: kept); the rule's two cases: the Split Scorpion's nor + tiny (0.50, run412, one nozzle,
merged) and the Split Ocelot's huge + big3 (0.20, run413: big3 has its own rim and plate geometry, kept). The last
lines count the pairs the rule merges (d <= v_large and 0.35 <= v2 / v1 <= 0.75) by body pair.

Usage: python3 run412_colocated_pairs.py [run412|run413] > <run>_colocated_pairs_out.txt  (default run412)
"""
import collections
import glob
import math
import sys

RUN = sys.argv[1] if len(sys.argv) > 1 else "run412"
LOG = glob.glob(f"/tmp/x3-bottleX3-{RUN}/session-*.log")[0]
MERGE_MIN, MERGE_MAX = 0.35, 0.75


def kv(line):
    out = {}
    for tok in line.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out.setdefault(k, v)
    return out


def main():
    frames = collections.defaultdict(list)
    with open(LOG, "rb") as f:
        for raw in f:
            if not raw.startswith(b"engine_draw device=1 "):
                continue
            d = kv(raw.decode("ascii", "replace"))
            try:
                frames[int(d["frame"])].append((d["name"].split("\\")[-1], float(d["size"]),
                                                tuple(float(x) for x in d["origin"].split(",")),
                                                tuple(float(x) for x in d["axis"].split(",")), d.get("handle")))
            except (KeyError, ValueError):
                continue
    hist = collections.Counter()
    frames_with = collections.Counter()
    merged = collections.Counter()
    for fr, recs in frames.items():
        seen = set()
        for i in range(len(recs)):
            for j in range(i + 1, len(recs)):
                a, b = recs[i], recs[j]
                if sum(x * y for x, y in zip(a[3], b[3])) < 0.95:
                    continue
                big, small = (a, b) if a[1] >= b[1] else (b, a)
                dist = math.dist(a[2], b[2])
                if dist > 2.0 * big[1]:
                    continue
                r = dist / big[1]
                q = small[1] / big[1]
                key = (big[0], small[0], "d/v<=0.5" if r <= 0.5 else "d/v<=1" if r <= 1.0 else "d/v<=2",
                       "v2/v1<0.35" if q < MERGE_MIN else "v2/v1<=0.75" if q <= MERGE_MAX else "v2/v1>0.75")
                if r <= 1.0 and MERGE_MIN <= q <= MERGE_MAX:
                    merged[(big[0], small[0], round(q, 3))] += 1
                hist[key] += 1
                if key not in seen:
                    frames_with[key] += 1
                    seen.add(key)
    print(f"# {LOG}; {len(frames)} frames with engine_draw rows")
    print("# larger body, smaller body, distance / larger size, size ratio: pairs (frames)")
    for key, n in sorted(hist.items(), key=lambda t: -t[1]):
        print(f"{key[0]:28s} {key[1]:28s} {key[2]:9s} {key[3]:12s} {n:7d} ({frames_with[key]})")
    print(f"# merged by the rule (d <= v_large, {MERGE_MIN} <= v2 / v1 <= {MERGE_MAX}): larger, smaller, size ratio: pairs")
    for key, n in sorted(merged.items(), key=lambda t: -t[1]):
        print(f"merged {key[0]:28s} {key[1]:28s} {key[2]:6.3f} {n:7d}")
    if not merged:
        print("merged none")


if __name__ == "__main__":
    main()
