"""Amendment 16b (capital-gate-transit.md section 16.10): grid geometry behind the sequencing rule, arithmetic only.
For the scenario-M hulls (r_gate 1105 m, w = 0.45 r and w = r): the along-axis half-extent bound h = sqrt(r^2 + w^2) of a hull
inside the cylinder (radius w, half-length r), the sequencing target D_x = h_T + h_L + 100 m, the certificate threshold
h_T + h_L + 50 m, how far a parked neighbour's grid point A_N lies ahead of a joiner's crossing plane X_J along F
(0.75 r_gate + 2 (r_J - r_N)), and how far before its own X a joiner must be inside its corridor not to be held behind that
parked neighbour; then the scenario-M simultaneous starts (gate-frame s and q) with the route each pair takes at t = 0.
Usage: python3 -I seq_geometry.py > seq_geometry.txt"""
import math

RG = 1105.0
HULLS = {"Titan": 1092.0, "M1": 1022.0, "M2s": 755.0}


def h(r, wr):
    w = r * wr if wr > 0 else r
    return math.sqrt(r * r + w * w)


def depth(r):  # the grid point's distance in front of G along F (the KC's 2 (r_gate + r_ship))
    return 2.0 * (RG + r)


def main():
    for wr in (0.45, 0.25, -1.0):
        print(f"## w = {'r' if wr < 0 else f'{wr} r'}")
        print("# joiner J behind parked N | h_J h_N | D_x | cert | A_N - X_J | corridor needed before X_J")
        for jn, rj in HULLS.items():
            for nn, rn in HULLS.items():
                hj, hn = h(rj, wr), h(rn, wr)
                ahead = 0.75 * RG + 2.0 * (rj - rn)
                cert = hj + hn + 50.0
                print(f"{jn:5s} behind {nn:5s} | {hj:6.0f} {hn:6.0f} | {hj + hn + 100:6.0f} | {cert:6.0f} | {ahead:6.0f} | {cert - ahead:6.0f}")
    # scenario M simultaneous (verification/probe/ship_steering_host.cpp kCrowd): s from the ship's own X -> gate-frame s
    starts = [("Titan s0", 1092.0, 0.0, 0.0, -5.0 * RG), ("M1 s1", 1022.0, 666.0, 666.0, -5.0 * RG),
              ("M2s s3", 755.0, 1332.0, 666.0 + 2700.0, -5.5 * RG), ("M2s s2", 755.0, -666.0, 700.0, -8.0 * RG),
              ("Titan s4", 1092.0, -1332.0, -1332.0 - 1400.0, -1000.0)]
    print("## scenario M simultaneous at t = 0 (gate frame: s = (x - G).F, q = lateral R)")
    for name, r, lane, q, s_x in starts:
        print(f"{name:9s} r {r:5.0f} lane {lane:6.0f} q {q:6.0f} s_gate {-(depth(r) + 0.75 * RG) + s_x:7.0f}")
    print("# pair | ds (B - A) | h_A + h_B + 50 (w 0.45 r) | |dq| - (w_A + w_B) (lateral gap at theta 0, before margins)")
    sg = {n: -(depth(r) + 0.75 * RG) + sx for n, r, _, _, sx in starts}
    for i in range(len(starts)):
        for k in range(i + 1, len(starts)):
            a, b = starts[i], starts[k]
            ds = sg[b[0]] - sg[a[0]]
            print(f"{a[0]:9s} - {b[0]:9s} | {ds:7.0f} | {h(a[1], 0.45) + h(b[1], 0.45) + 50:6.0f} | {abs(a[3] - b[3]) - 0.45 * (a[1] + b[1]):6.0f}")


if __name__ == "__main__":
    main()
