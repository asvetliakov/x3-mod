"""Gate traffic controller, level 1 (docs/architecture/gate-traffic-controller.md): the geometry numbers the design quotes.
Arithmetic only [i]; inputs are measured facts with their sources:
  r_gate 1104.5 m (installed ordinary gates), Terran 3207.5 m with hole h = 0.663 r (ship-navigation-ai.md 10.9) [m]
  order-5 trigger: centre within (|v|/10 + r_s)(1 + (tf - 1)/4) of the stored point, the gate origin for a capital (10.10) [m]
  KC grid: G - 2 (r_gate + r_s) F + 666 m (a R + b U), a = {0, 1, -1, 2, -2}[slot % 5], b = -(slot / 5) (10.2) [m]
  order-4 arrival sphere at a gate: 0.75 r_gate = 828 m (capital-gate-transit.md 0) [m]
  small arrival fan: upper 0.55 r arc, phi in [-45, 45] deg from +U (10.10) [m]; small departures: lower 0.55 r arc +-45 deg [m]
  capital arrival P: on the axis 22 m up, r_s in front; offset <= 0.6 r_gate carries through at 0x00444606 (10.10) [m]/[i]
  radii min/median/max per class (game-scale-and-ship-classes.md 4, installed) [m]; w/r 0.45-0.60 in flight (Run159, Run160
  census 0.45-0.53) [m]; l/r up to 1.40 (capital-widths) [m]; small ships r <= 140 m (M6 max) [m]
Usage: python3 -I design_numbers.py
"""
import math
RG = 1104.5
ARC = 0.55 * RG
FAN_LOW = ARC * math.cos(math.radians(45))      # lowest U of the upper small-arrival fan
R_SMALL = 140.0
M = 50.0                                         # clearance margin
E_LAT = 100.0                                    # lateral tracking allowance on the final straight
THETA = math.radians(3.0)                        # nose bound on the final straight
L_A = 0.6 * RG                                   # capital arrival offset (+U)
CLASSES = {'M7': (216, 560, 1431), 'M1': (227, 1170, 4583), 'M2': (307, 1337, 2998), 'TL': (745, 1135, 1837)}
CENSUS = {'Titan M2': (1092, 583), 'Colossus M1': (1022, 546), 'M7 a': (760, 382), 'M7 b': (778, 352)}

def rho(r, w, lr=1.0, theta=THETA):              # lateral reach of the capsule (half-length L = max(r, l)) with the nose theta off F
    L = max(r, lr * r)
    return w + (L - w) * math.sin(theta)

def trig(r, v=100.0, tf=1.0):                    # the order-5 trigger radius, m
    return (v / 10.0 + r) * (1 + (tf - 1) / 4)

print('== 1. Outgoing pass point Q at -U l_o: window [l_min, l_max] per class (tf 1, v 100 m/s)')
print('   l_max = trigger - %.0f m tracking; l_min = top of the hull footprint below the small-arrival fan (U %.0f m) by r_small + %.0f m' % (E_LAT, FAN_LOW, M))
print('   class  r m   | w/r 0.5: l_min..l_max  stop before plane at l_min | w/r 0.6: l_min..l_max | Terran ring (h 2126 m): l_o + w <= h - 50')
for c, rs in CLASSES.items():
    for r in rs:
        out = []
        for wr in (0.5, 0.6):
            w = wr * r
            lmin = max(0.0, rho(r, w) - (FAN_LOW - R_SMALL - M))
            lmax = trig(r) - E_LAT
            out.append((lmin, lmax))
        w5 = 0.5 * r
        t = trig(r)
        stop = math.sqrt(max(0.0, t * t - out[0][0] ** 2))
        terran = 0.663 * 3207.5 - 50 - 0.5 * r
        ok = lambda p: 'ok' if p[0] <= p[1] else 'NO'
        print('   %-5s %5d | %6.0f..%-6.0f %s  %6.0f m | %6.0f..%-6.0f %s | l_o <= %.0f' % (c, r, out[0][0], out[0][1], ok(out[0]), stop, out[1][0], out[1][1], ok(out[1]), terran))
print('   Run160 census hulls (measured w):')
for n, (r, w) in CENSUS.items():
    lmin = max(0.0, rho(r, w) - (FAN_LOW - R_SMALL - M)); lmax = trig(r) - E_LAT
    print('   %-12s r %4d w %3d (%.2f r): l_o %4.0f..%4.0f %s' % (n, r, w, w / r, lmin, lmax, 'ok' if lmin <= lmax else 'NO'))

print('\n== 2. Capital arrival at +U %.0f m (0.6 r_gate): footprint vs the lower small-departure arc (U <= -%.0f m) and the departing capital' % (L_A, FAN_LOW))
for n, (r, w) in CENSUS.items():
    gap_small = (L_A - w) - (-FAN_LOW) - R_SMALL          # bottom of the arrival hull vs the top of the lower arc's ships
    print('   %-12s w %3d: bottom of the arrival hull U %+5.0f m; clearance to the lower arc (+r_small) %5.0f m' % (n, w, L_A - w, gap_small))
print('   arrival vs departing capital at l_o (both census Titans, w 583): need l_o + %.0f >= rho_o + w_a + %.0f -> l_o >= %.0f m' % (
    L_A, M, rho(1092, 583) + 583 + M - L_A))

print('\n== 3. Small arrivals while a capital arrival occupies the spawn zone: side spawn (+-0.87 r, +0.2 r) within 0.9 r of the axis')
side = (0.87 * RG, 0.2 * RG)
for n, (r, w) in CENSUS.items():
    d_fan0 = abs(ARC - L_A)                                # fan centre (0, 607) vs arrival centre (0, 663)
    d_side = math.hypot(side[0], side[1] - L_A)
    print('   %-12s w %3d: surface clearance (centre distance - w - r_small; must be >= %d m): fan centre %5.0f m, side spawn %5.0f m' % (
        n, w, M, d_fan0 - w - R_SMALL, d_side - w - R_SMALL))
print('   side spawn radius from the axis %.0f m = %.2f r_gate (visual bubble 0.910 r)' % (math.hypot(*side), math.hypot(*side) / RG))
for wr in (0.6, 0.7, 0.75):
    w = wr * 1337
    print('   M2 median r 1337 at w %.2f r = %.0f m: side spawn surface clearance %5.0f m' % (wr, w, math.hypot(side[0], side[1] - L_A) - w - R_SMALL))

print('\n== 4. KC grid slots: a final line through Q that also crosses the slot sphere (|line - A_lat| <= 0.75 r_gate - 150 = %.0f m)' % (0.75 * RG - 150))
r, w = 1092, 583
lmin = rho(r, w) - (FAN_LOW - R_SMALL - M); lmax = trig(r) - E_LAT
print('   Titan r %d w %d: admissible Q = disc |Q| <= %.0f with Q_U <= -%.0f' % (r, w, lmax, lmin))
def nearest_q(ax, ay):
    best = None
    for i in range(0, 721):
        a = math.radians(i * 0.5)
        for k in range(0, 101):
            rr = lmax * k / 100
            qx, qy = rr * math.cos(a), rr * math.sin(a)
            if qy > -lmin: continue
            d = math.hypot(qx - ax, qy - ay)
            if best is None or d < best[0]: best = (d, qx, qy)
    return best
single = 0
for s in range(20):
    a = [0, 1, -1, 2, -2][s % 5]; b = -(s // 5)
    ax, ay = 666.0 * a, 666.0 * b
    d, qx, qy = nearest_q(ax, ay)
    fits = d <= 0.75 * RG - 150
    single += fits
    print('   slot %2d A_lat (%+5.0f, %+5.0f): nearest Q (%+5.0f, %+5.0f), %5.0f m -> %s' % (s, ax, ay, qx, qy, d, 'one straight line' if fits else 'order-5 lateral S of %.0f m' % (d - (0.75 * RG - 150))))
print('   slots with one straight final line: %d of 20' % single)

print('\n== 5. Holding petals: a racetrack per holder in a plane containing F, on a ring of radius rho_ring around the funnel axis')
for wmax in (583, 700, 800):
    for dphi in (30, 36, 45):
        ring = (2 * wmax + 100) / math.radians(dphi)
        n = int(240 // dphi) + 1
        print('   w_max %3d m, petal spacing %2d deg: ring radius >= %5.0f m, petals per ring in the lower 240 deg: %d' % (wmax, dphi, ring, n))
for rh, vh in ((1000, 25), (1000, 40), (1500, 40), (1000, 60)):
    for sh in (0, 2000):
        lap = (2 * sh + 2 * math.pi * rh) / vh
        print('   racetrack R_h %4d m, straight %4d m at %2d m/s: lap %4.0f s, turn rate %.1f deg/s' % (rh, sh, vh, lap, math.degrees(vh / rh)))

print('\n== 6. Through-slot cadence (one capital per gate pair at a time)')
o5 = [15.3, 15.4, 15.5, 15.4, 15.5]   # Run160b cross_to_launch, kept crossings [m]
print('   kept crossings X -> launch (Run160b, measured): %s s; + KC hand-over and a 2 s gap -> one departure per %.0f-%.0f s' % (o5, min(o5) + 2, max(o5) + 3))
print('   after a capital arrival the KC refuses our side for 60 s (direction change; inferred from 10.2); arrivals measured 61-72 s apart')
