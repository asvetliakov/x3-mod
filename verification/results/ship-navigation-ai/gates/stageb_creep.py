#!/usr/bin/env python3
"""Where the order-5 misalignment test (0x00454897: nose . dir(f+0x240) < 0xf333) fires for a capital flying an
F-parallel final line at lateral offset l from the gate axis while f+0x240 is the gate origin (section 10.11, Q1).

The bearing to the origin is atan(l / z) at axial distance z, so the test fires for z < l * cot(acos(0xf333/65536)).
The trigger fires at z_t = sqrt(t^2 - l^2) with t = r_s + |v|/10 (tf 1, section 10.10). The creep zone is reached
before the trigger iff l > t * sin(acos(0xf333/65536)). Windows: gate-traffic-controller.md D.2.2 census row, with
t = l_max + 100 m (its own definition). Model numbers, [i].

    python3 -I verification/results/ship-navigation-ai/gates/stageb_creep.py > stageb_creep.txt
"""
import math
a = math.acos(0xf333 / 65536)
print('threshold %.4f deg, cot %.4f, sin %.4f' % (math.degrees(a), 1 / math.tan(a), math.sin(a)))
for name, lo, hi in (('Titan r 1092 w 583', 370, 1002), ('Colossus 1022/546', 331, 932),
                     ('M7 760/382', 162, 670), ('M7 778/352', 135, 688)):
    t = hi + 100
    print('%-20s t %4d m  creep iff l > %4.0f m' % (name, t, t * math.sin(a)))
    for l in (lo, (lo + hi) // 2, hi):
        zc, zt = l / math.tan(a), math.sqrt(t * t - l * l)
        print('    l %4d: test fires from z %5.0f m, trigger at z %5.0f m, creep distance %5.0f m' % (l, zc, zt, max(0.0, zc - zt)))
