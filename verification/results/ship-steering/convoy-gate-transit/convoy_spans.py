#!/usr/bin/env python3
"""Convoy gate-transit spans from a triage directory (game seconds).

For every leader in <triage>/escorts.txt: the moment the leader first comes within 15 km of
its gate (ship_gate_ctl_member rows, |p_m| is gate-relative), its launch, each escort's
launch (transits.txt), the span 15 km -> last escort launch, and the leader's straight-line
time 15 km -> its own launch distance at 100 m/s (the large cap).
Usage: python3 -I convoy_spans.py <triage dir>   (streams x3m.log through grep; prints a table)
"""
import math, re, subprocess, sys
from collections import defaultdict

tri = sys.argv[1]
trans = {}   # id -> (ptr, gate, launch game s or None)
for ln in open(f"{tri}/transits.txt"):
    m = re.match(r"(\d+) (0x[0-9a-f]+) \S+ (\w+) \|", ln)
    if not m:
        continue
    f = [x.strip() for x in ln.split("|")]
    launch = f[6].split("/")[0]
    trans[m.group(1)] = (m.group(2), m.group(3), None if launch == "-" else float(launch))
convoys = defaultdict(list)
for ln in open(f"{tri}/escorts.txt"):
    m = re.match(r"(\d+) (\d+) \|", ln)
    if m:
        convoys[m.group(2)].append(m.group(1))
ptrs = {trans[i][0] for L, es in convoys.items() for i in [L] + es if i in trans}
first15 = {}      # (ptr, gate) -> game s first |p| <= 15 km
track = defaultdict(list)  # ptr -> [(t, d, gate)] decimated to 1 row per 2 s
launch_d = {}     # ptr -> d_G of its last order-5 row (m)
rx = re.compile(r"t_ms=(\d+) gate=0x([0-9a-f]+) ship=(0x[0-9a-f]+) .*?order=(0x[0-9a-f]+) .*?p_m=(-?\d+),(-?\d+),(-?\d+)")
p = subprocess.Popen(["grep", "-F", "ship_gate_ctl_member", f"{tri}/x3m.log"], stdout=subprocess.PIPE, text=True)
for ln in p.stdout:
    m = rx.search(ln)
    if not m or m.group(3) not in ptrs:
        continue
    t = int(m.group(1)) / 1000.0
    d = math.sqrt(sum(float(m.group(k)) ** 2 for k in (5, 6, 7)))
    key = (m.group(3), m.group(2)[-4:])
    if d <= 15000 and key not in first15:
        first15[key] = t
    tr = track[m.group(3)]
    if not tr or t - tr[-1][0] >= 2.0:
        tr.append((t, d, m.group(2)[-4:]))
    if m.group(4) == "0x5":
        launch_d[m.group(3)] = d
print("leader gate | leader 15km | leader launch (15km->launch, straight@100) | escort launches +s after leader | 15km->last launch")
for L, es in convoys.items():
    if L not in trans:
        continue
    ptr, gate, tl = trans[L]
    t15 = first15.get((ptr, gate))
    dl = launch_d.get(ptr, float("nan"))
    straight = (15000 - dl) / 100.0
    el = [trans[e][2] for e in es if e in trans and trans[e][1] == gate]
    rel = ["%+.1f" % (x - tl) if (x is not None and tl) else "-" for x in el]
    last = max([x for x in el if x is not None] + [tl or 0])
    span = (last - t15) if (t15 and tl) else float("nan")
    lead = (tl - t15) if (t15 and tl) else float("nan")
    def d_at(e, t):
        rows = [r for r in track.get(trans[e][0], []) if r[0] <= t]
        return rows[-1][1] / 1000.0 if rows else float("nan")
    ext = "; ".join(f"{e}: 15km {first15.get((trans[e][0], gate))} d@L15 {d_at(e, t15 or 0):.1f} km" for e in es if e in trans)
    print(f"   escorts: {ext}")
    # Inferred lane floor: no waiting, each ship 15 km -> launch in T15 s (median unobstructed leader, measured),
    # launches in arrival order no closer than CAD s (capsule trail spacing 2.5 km at 100 m/s).
    T15, CAD = 134.0, 25.0
    if t15:
        arr = sorted([t15] + [first15[(trans[e][0], gate)] for e in es if e in trans and (trans[e][0], gate) in first15])
        fl, last_l = [], None
        for a in arr:
            x = a + T15 if last_l is None else max(a + T15, last_l + CAD)
            fl.append(x); last_l = x
        print(f"   lane floor (inferred): last launch {fl[-1]:.1f}, span {fl[-1] - t15:.1f} s, escort offsets {' '.join('%+.1f' % (x - fl[0]) for x in fl[1:])}")
    print(f"{L} {gate} | {t15} | {tl} ({lead:.1f}, {straight:.1f} at d_launch {dl:.0f} m) | {' '.join(rel)} (escorts {' '.join(es)}) | {span:.1f}")
