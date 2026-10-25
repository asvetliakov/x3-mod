"""Derived numbers for docs/architecture/fog-visible-distance.md (inferred from the shipped constants;
no game, Wine or GPU involved). Units: render units, 5000 per km. python3 -I derive.py"""
import math
KM = 5000.0
S0, CAP0 = 65000.0, 112500.0            # fog_look_math.h taper_start / sky_cap (shipped)
RATE = 2.29e6                           # measured worker nodes/s, log_rows.txt (native, today's log)
ATLAS = 4260096                         # kAtlasBytes, measured in the volumetric_fog_range row
TROUGH = 164761600                      # min avail_virtual, Run184 memory rows (native, measured)
REACH_PAD = 1400 + 500 + 3000           # domain warp amplitude + sun-ward tap distance

def ss(a, b, x):
    t = min(max((x - a) / (b - a), 0.0), 1.0); return t * t * (3 - 2 * t)
def w_today(s): return 1.0 - ss(S0, CAP0, s)
def w_new(s, cap, p=2.0):
    return 1.0 if s <= S0 else (S0 / s) ** p * (1.0 - ss(0.75 * cap, cap, s))
def integral(w, cap, n=200000):
    h = cap / n; return sum(w((i + .5) * h) for i in range(n)) * h
def first_s(w, cap, level):            # farthest distance at which w >= level
    s = cap
    while s > 0 and w(s) < level: s -= 50
    return s

print("today: cap %.1f km, taper from %.1f km" % (CAP0 / KM, S0 / KM))
a, b = first_s(w_today, CAP0, .05), first_s(w_today, CAP0, .5)
print("  w>=.05 from %.2f km, w>=.5 from %.2f km: appearance band %.2f km; max |dw/ds| %.3f per km; equiv path %.2f km"
      % (a / KM, b / KM, (a - b) / KM, 1.5 / ((CAP0 - S0) / KM), integral(w_today, CAP0) / KM))
for k in (40 / 22.5, 3, 4, 5):
    cap = CAP0 * k
    for p in (2.0, 1.5):
        w = lambda s: w_new(s, cap, p)
        a, b = first_s(w, cap, .05), first_s(w, cap, .5)
        print("x%.2f cap %.1f km p=%.1f: w(22.5)=%.3f w(45)=%.3f w(67.5)=%.3f; w>=.05 from %.1f km, w>=.5 from %.1f km: band %.1f km; equiv path %.2f km"
              % (k, cap / KM, p, w(22.5 * KM), w(45 * KM), w(67.5 * KM), a / KM, b / KM, (a - b) / KM, integral(w, cap) / KM))
    # 40 geometric tail bins from today's cap to the new cap
    r = (cap / CAP0) ** (1 / 40) - 1
    print("   tail 40 geometric bins: ratio %.4f, ds first %.0f last %.0f units" % (r, CAP0 * r, cap * r / (1 + r)))
    for delta in (4096, 8192, 16384):
        reach = cap + REACH_PAD
        half = math.floor(reach / delta) + 1          # need box half extent incl. the trilinear corner
        slack = 64 - half - 1                         # window high side floor(c/d)+64, readiness guard 1
        nodes = (2 * half + 1) ** 3
        print("   delta %5d: need half %d nodes, retarget slack %d nodes (%.1f km), need box %.2f M nodes = %.2f s at %.2f M/s"
              % (delta, half, slack, max(slack, 0) * delta / KM, nodes / 1e6, nodes / RATE, RATE / 1e6))
print("window fill 128^3 = %.2f M nodes = %.2f s" % (128 ** 3 / 1e6, 128 ** 3 / RATE))
print("third level address space (CPU cache + SYSTEMMEM staging) %d B = %.1f%% of the Run184 trough; +DEFAULT %d B"
      % (2 * ATLAS, 200 * ATLAS / TROUGH, ATLAS))
