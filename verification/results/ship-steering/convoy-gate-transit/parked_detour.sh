#!/bin/sh
# Final review item 5 (convoy-gate-transit.md section 11): who flies the parked cast's turn-around. Seed 1, tf 1, the convoy leader
# (ship 0) every frame: the seconds in which the module's avoidance holds a detour (av 1) against the parked M7 (obst 9300), and the
# leader's lane law and nose (cos to F) at 10 s samples. Usage: sh parked_detour.sh <fixture binary> (from the repository root).
X3M_C_ONLY=parked X3M_C_TF=1 X3M_C_TRACE=1 X3M_C_SHIP=0 "$1" --lane 2>/dev/null | python3 -c '
import re, sys
av = {}; rows = {}
for l in sys.stdin:
    m = re.match(r"\s+tc parked tf 1 t=(\d+) ship 0 .* nose (\S+) .* law (\S+) on (\d) .* av (\d) obst (\S+)", l)
    if not m: continue
    t = int(m.group(1)); av.setdefault(t, set()).add((m.group(5), m.group(6)))
    rows.setdefault(t, (m.group(2), m.group(3), m.group(4)))
on = sorted(t for t, v in av.items() if ("1", "9300") in v and t <= 120)
print("leader seconds 0..120 with the avoidance detour against the parked M7 (obst 9300): %d..%d, %d of 121" % (on[0], on[-1], len(on)) if on else "no detour")
for t in sorted(rows):
    if t % 10 == 0 and t <= 120: print("t=%3d nose %s law %s on %s detour %s" % ((t,) + rows[t] + ("1" if t in on else "0",)))
'
