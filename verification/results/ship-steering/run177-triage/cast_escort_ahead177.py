# Run177 Q4: which host-fixture casts (CStart tables in verification/probe/ship_steering_host.cpp) start an escort ahead of its leader
# along F (escort z > leader z + 2 km; z < 0 out from the gate) or farther than 20 km out (kLaneOwnM). Usage: python3 -I cast_escort_ahead177.py HOST_CPP
import re, sys
s = open(sys.argv[1]).read(); n = e = 0
for m in re.finditer(r'static const CStart (k\w+)\[\d+\] = \{(.*?)\n\};', s, re.S):
    n += 1
    rows = re.findall(r'\{&(\w+),\s*([\d.]+),\s*(\w+),\s*(-?\d+),\s*(-?[\d.e]+),\s*(-?[\d.e]+),\s*(-?[\d.e]+)', m[2])
    for i, r in enumerate(rows):
        if r[2] != 'kEsc': continue
        e += 1; L = int(r[3]); ez, ex, ey = float(r[6]), float(r[4]), float(r[5])
        if 0 <= L < len(rows):
            lz = float(rows[L][6])
            if ez > lz + 2000: print('AHEAD', m[1], i, 'esc (x,y,z)', ex, ey, ez, '| lead z', lz, 'lead x', rows[L][4])
        if ez < -20000: print('FAR  ', m[1], i, 'esc (x,y,z)', ex, ey, ez)
print('tables', n, 'escort rows', e)
