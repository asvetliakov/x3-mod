#!/usr/bin/env python3
"""Far-level nodes the held worker generates between an origin-centred prefill and the whole-atlas latch for the
run273 frame-24765 arrival, with next_slab growing to the window's edge (the code before the review) against the
far target only (fog_density_cache.cpp work_once, `want`). Pure geometry of fog_density_cache.cpp (need_box, window
origin, kFirstFillSlack 2, kReadinessGuard 1, next_slab order: urgent sides first, then x-, x+, y-, y+, z-, z+)."""
import math
DELTA, REACH, WINDOW, HALF, SLACK, GUARD = 4096.0, 200000.0, 128, 64, 2, 1
arrival = (153045.0, 168066.0, -16679.0)  # handover_gaps_out.txt: world camera at frame 24765
origin = (0.0, 0.0, 0.0)


def need(c): return [[math.floor((c[a] - REACH) / DELTA), math.floor((c[a] + REACH) / DELTA) + 1] for a in range(3)]
def grow(b, n): return [[lo - n, hi + n] for lo, hi in b]
def window(c): return [[math.floor(c[a] / DELTA) - HALF, math.floor(c[a] / DELTA) - HALF + WINDOW - 1] for a in range(3)]
def inter(a, b): return [[max(a[i][0], b[i][0]), min(a[i][1], b[i][1])] for i in range(3)]
def nodes(b): return math.prod(max(0, hi - lo + 1) for lo, hi in b)
def contains(outer, inner): return all(o[0] <= i[0] and i[1] <= o[1] for o, i in zip(outer, inner))


def next_slab(have, want, needb):
    for urgent_pass in (True, False):
        for a in range(3):
            for side in (0, 1):
                missing = have[a][1] < want[a][1] if side else have[a][0] > want[a][0]
                urgent = needb[a][1] > have[a][1] if side else needb[a][0] < have[a][0]
                if not missing or (urgent_pass and not urgent): continue
                slab = [list(x) for x in have]
                if side: slab[a] = [have[a][1] + 1, want[a][1]]
                else: slab[a] = [want[a][0], have[a][0] - 1]
                return slab
    return None


def grown(have, slab): return [[min(h[0], s[0]), max(h[1], s[1])] for h, s in zip(have, slab)]


def extension(bounded):
    prefill = inter(window(origin), grow(need(origin), SLACK))
    win = window(arrival); have = inter(prefill, win); target = grow(need(arrival), GUARD); total = 0
    while not contains(have, target):
        want = [[max(win[a][0], min(have[a][0], target[a][0])), min(win[a][1], max(have[a][1], target[a][1]))] for a in range(3)] if bounded else win
        slab = next_slab(have, want, target); total += nodes(slab); have = grown(have, slab)
    return nodes(prefill), total


first_fill = nodes(inter(window(arrival), grow(need(arrival), SLACK)))
p, edge = extension(False); _, bound = extension(True)
print('prefill_first_fill_nodes=%d arrival_first_fill_nodes=%d to_window_edge=%d to_far_target=%d' % (p, first_fill, edge, bound))
