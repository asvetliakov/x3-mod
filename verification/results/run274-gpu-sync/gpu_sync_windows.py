#!/usr/bin/env python3
"""Summarise gpu_sync_timing window rows: per window, per pass median/p90 ms, n,
wait, and floor-subtracted median. Usage: gpu_sync_windows.py LOG [--floor-us 264] [--windows a-b]"""
import re, sys, argparse
ap = argparse.ArgumentParser(); ap.add_argument("log"); ap.add_argument("--floor-us", type=float, default=264.0)
ap.add_argument("--windows", default=None); ap.add_argument("--quiet", action="store_true")
a = ap.parse_args()
lo, hi = (map(int, a.windows.split("-")) if a.windows else (0, 1 << 30))
kv = re.compile(r"(\w+)=(\S+)")
rows = []
for line in open(a.log, errors="replace"):
    if line.startswith("gpu_sync_timing window="):
        d = dict(kv.findall(line)); w = int(d["window"])
        if lo <= w <= hi: rows.append(d)
    elif line.startswith("gpu_sync_timing") and not a.quiet:
        print("OTHER", line.strip()[:200])
wins = {}
for d in rows: wins.setdefault(int(d["window"]), []).append(d)
for w, ds in sorted(wins.items()):
    d0 = ds[0]
    print(f"W{w} frames={d0['frames']} dt_med={int(d0['dt_median_us'])/1e3:.1f} dt_p90={int(d0['dt_p90_us'])/1e3:.1f} dropped={d0['dropped']} unclosed={d0['unclosed']}")
    for d in sorted(ds, key=lambda d: -int(d["median_us"])):
        m, p, wt = int(d["median_us"]), int(d["p90_us"]), int(d["wait_median_us"])
        print(f"   {d['pass']:<14} n={d['n']:>4} med={m/1e3:7.3f} p90={p/1e3:7.3f} wait={wt/1e3:6.3f} med-floor={max(0,m-a.floor_us)/1e3:7.3f}")
