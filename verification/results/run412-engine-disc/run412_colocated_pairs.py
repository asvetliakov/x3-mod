#!/usr/bin/env python3
"""Co-located engine records in the Run124 flight (run412): the census behind the plume merge rule.

Streams the engine_draw rows of /tmp/x3-bottleX3-run412/session-*.log (read-only; only rows starting with
"engine_draw device=1 ") and, per frame, pairs every two records whose axes are parallel (dot >= 0.95; the rows carry
no parent, so a ship is approximated by the identical axis of its nozzles) and whose origins lie within 2 x the larger
record's size. Per pair: the distance over the larger size (d / v) and the smaller size over the larger (v2 / v1).
Prints a histogram over (body names, d / v bin, size ratio bin) for distinct (frame-free) name pairs and the counts of
pairs within the merge rule's distance (d <= v_large) by the size-ratio rule (v_small <= 0.75 v_large).

Usage: python3 run412_colocated_pairs.py > run412_colocated_pairs_out.txt
"""
import collections
import glob
import math

LOG = glob.glob("/tmp/x3-bottleX3-run412/session-*.log")[0]


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
                       "v2/v1<=0.75" if q <= 0.75 else "v2/v1>0.75")
                hist[key] += 1
                if key not in seen:
                    frames_with[key] += 1
                    seen.add(key)
    print(f"# {LOG}; {len(frames)} frames with engine_draw rows")
    print("# larger body, smaller body, distance / larger size, size ratio: pairs (frames)")
    for key, n in sorted(hist.items(), key=lambda t: -t[1]):
        print(f"{key[0]:28s} {key[1]:28s} {key[2]:9s} {key[3]:12s} {n:7d} ({frames_with[key]})")


if __name__ == "__main__":
    main()
