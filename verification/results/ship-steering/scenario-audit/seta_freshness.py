#!/usr/bin/env python3
"""Scenario audit (2026-10-12): does SETA push the game-time frame step past the module's freshness windows?

Reads one x3m.log (Run157 by default) in a single streaming pass and prints:
  * the distribution of the per-frame game step dt_ms from ship_trace rows (one value per frame);
  * per ship_steering_summary interval: the median trace dt_ms of the frames in it, and the site-E counters
    (e_calls, e_suppressed = resolved-set branch, e_suppressed_small, e_small_pass_stale), grouped by dt band.
Windows compared: kResolvedAgeMs 100 (resolved-set branch), kSteeredFreshMs / kClampMaxAgeMs 250.
"""
import re, sys, statistics
from collections import defaultdict

path = sys.argv[1] if len(sys.argv) > 1 else "/mnt/c/X3/x3m.log"
re_frame = re.compile(r"\bframe=(\d+)")
re_dt = re.compile(r"\bdt_ms=(\d+)")
re_seta = re.compile(r"\bseta=(\d+)")
fields = ("e_calls", "e_suppressed", "e_suppressed_small", "e_small_pass_stale", "e_small_pass_order")
re_f = {k: re.compile(r"\b" + k + r"=(\d+)") for k in fields}

frame_dt = {}
frame_seta = {}
summaries = []  # (frame, {field: n})
with open(path, "rb") as fh:
    for raw in fh:
        if raw.startswith(b"ship_trace frame="):
            line = raw.decode("ascii", "replace")
            fm, dm = re_frame.search(line), re_dt.search(line)
            if fm and dm:
                frame_dt.setdefault(int(fm.group(1)), int(dm.group(1)))
                sm = re_seta.search(line)
                if sm:
                    frame_seta.setdefault(int(fm.group(1)), int(sm.group(1)))
        elif raw.startswith(b"engine_stage ") and b" seta=" in raw:
            line = raw.decode("ascii", "replace")
            fm, sm = re_frame.search(line), re_seta.search(line)
            if fm and sm:
                frame_seta.setdefault(int(fm.group(1)), int(sm.group(1)))
        elif raw.startswith(b"ship_steering_summary "):
            line = raw.decode("ascii", "replace")
            fm = re_frame.search(line)
            if not fm:
                continue
            vals = {}
            for k, r in re_f.items():
                m = r.search(line)
                vals[k] = int(m.group(1)) if m else 0
            summaries.append((int(fm.group(1)), vals))

dts = sorted(frame_dt.values())
n = len(dts)
print(f"log={path}")
print(f"trace frames with dt_ms: {n}")
if n:
    q = lambda p: dts[min(n - 1, int(p * n))]
    print(f"dt_ms p10={q(.10)} p50={q(.50)} p90={q(.90)} p99={q(.99)} max={dts[-1]}")
    for lim in (50, 100, 167, 250, 333, 1000):
        print(f"  frames with dt_ms > {lim}: {sum(1 for d in dts if d > lim)} ({100.0 * sum(1 for d in dts if d > lim) / n:.1f} %)")
    if frame_seta:
        by = defaultdict(list)
        for f, d in frame_dt.items():
            if f in frame_seta:
                by[frame_seta[f]].append(d)
        for k in sorted(by):
            v = sorted(by[k])
            print(f"  seta={k}: frames {len(v)} dt_ms p50={v[len(v)//2]} p90={v[int(.9*len(v))]} max={v[-1]}")

bands = defaultdict(lambda: defaultdict(int))
prev = None
for fr, vals in summaries:
    if prev is not None:
        ds = [frame_dt[f] for f in range(prev + 1, fr + 1) if f in frame_dt]
        if ds:
            med = statistics.median(ds)
            band = "dt<=100" if med <= 100 else ("100<dt<=250" if med <= 250 else "dt>250")
            b = bands[band]
            b["intervals"] += 1
            for k in fields:
                b[k] += vals[k]
    prev = fr
print("site E per summary interval, grouped by the interval's median game dt per frame:")
for band in ("dt<=100", "100<dt<=250", "dt>250"):
    b = bands.get(band)
    if not b:
        print(f"  {band}: no intervals")
        continue
    calls = b["e_calls"] or 1
    print(f"  {band}: intervals {b['intervals']} e_calls {b['e_calls']} e_suppressed(resolved) {b['e_suppressed']} "
          f"({1e3 * b['e_suppressed'] / calls:.2f} per 1000 calls)")
if summaries:
    last = summaries[-1][1]
    print("  cumulative at the last summary (these three fields are running totals in the row): "
          f"e_suppressed_small {last['e_suppressed_small']} e_small_pass_stale {last['e_small_pass_stale']} "
          f"e_small_pass_order {last['e_small_pass_order']}")
