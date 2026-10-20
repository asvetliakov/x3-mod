#!/usr/bin/env python3
"""Rows for the piloted ship in an x3m.log written with debug=1 and ship_trace=1.
Usage: python3 -I player_rows.py /mnt/c/X3/x3m.log
The player's object id comes from the first ship_player_pos row (so the flight must pass within 15 km of a gate)."""
import re, sys, collections
path = sys.argv[1]
player = None; counts = collections.Counter(); orders = collections.Counter(); diff = collections.Counter()
trace = obst = 0; detours = []; flags = set()
with open(path, errors="replace") as f:
    for l in f:
        if player is None and l.startswith("ship_player_pos "):
            player = re.search(r"obj=(0x[0-9a-f]+)", l).group(1); continue
        if player is None or ("obj=" + player + " ") not in l: continue
        kind = l.split(" ", 1)[0]; counts[kind] += 1
        if kind == "ship_trace":
            trace += 1
            od = re.search(r"order=(0x[0-9a-f]+)", l).group(1); orders[od] += 1
            p = re.search(r" point=([-\d,]+) given=([-\d,]+)", l)
            if p and p.group(1) != p.group(2): diff[od] += 1
            if "obst=0x00000000" not in l: obst += 1
        elif kind == "ship_avoid_detour":
            detours.append((int(re.search(r"frame=(\d+)", l).group(1)), re.search(r"obstacle=(0x[0-9a-f]+) kind=(\d+)", l).groups(), float(re.search(r"miss_m=([\d.]+)", l).group(1))))
        elif kind == "ship_steering_decision":
            flags.add(re.search(r"flags=(0x[0-9a-f]+)", l).group(1))
print("player", player, "rows", dict(counts))
print("trace rows", trace, "by order", dict(orders), "point rewritten", dict(diff), "obst set", obst)
print("detours", len(detours), "first", detours[:1], "last", detours[-1:])
print("decision flags", sorted(flags))
