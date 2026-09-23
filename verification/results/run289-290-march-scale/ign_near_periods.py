"""Distance to an integer of the shaft-offset noise increment 52.9829189*dot(v,(0.06711056,0.00583715)) for the lattice
vectors v (in march cells) found in band_lattice_run29x_out.txt: run290 (12,4),(-16,4),(-4,8) px / 4 = (3,1),(-4,1),(-1,2);
run289 (-8,8),(4,6),(-12,2) px / 2 = (-4,4),(2,3),(-6,1); (1,0),(0,1) as non-period controls. Small = near period of the
interleaved-gradient noise of fog_density_field_inc.h:246. usage: ign_near_periods.py"""
for v in [(3, 1), (-4, 1), (-1, 2), (-4, 4), (2, 3), (-6, 1), (1, 0), (0, 1)]:
    x = 52.9829189*(0.06711056*v[0] + 0.00583715*v[1]); f = x % 1
    print(v, round(min(f, 1 - f), 3))
