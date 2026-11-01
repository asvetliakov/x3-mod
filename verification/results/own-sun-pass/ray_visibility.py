#!/usr/bin/env python3
"""Why Run221's step 1c showed "4 rays like an X" and what step 1d shows (docs/architecture/own-sun-look.md, "Step 1d"):
per spoke, the last radius (core radii) where the ray adds >= 10 % to the corona + halo on its axis, for the step 1c
constants (six spokes, profile from the centre) and the step 1d constants (eleven spokes, profile from the core's edge).
A contrast model, not a display measurement (inferred). White sun, K = 2, strengths and lengths as the program hashes them.
Usage: python3 ray_visibility.py
"""
import math

STEPS = {
    '1c': dict(n=6, offset=.25, l_max=14, start=0, corona=(.6, 1.5), halo=(.3, 6, 24),
               length=lambda k: (k * (.618034 + k * .1547)) % 1, square=True, strength=(.6, .4)),
    '1d': dict(n=11, offset=.125, l_max=13, start=1, corona=(.9, 1.05), halo=(.3, 4.2, 17),
               length=lambda k: (k * .618034 + .3) % 1, square=False, strength=(.6, .6)),
}
GAIN, THRESHOLD = .15, .1


def reaches(step):
    s = STEPS[step]
    (cg, cs), (hg, hr, gr) = s['corona'], s['halo']
    out = []
    for k in range(s['n']):
        h1 = s['length'](k)
        length = s['l_max'] * ((.25 + .75 * h1 * h1) if s['square'] else (.15 + .85 * h1))
        strength = s['strength'][0] + s['strength'][1] * ((k * .7548777 + .31) % 1)
        seen = 0.
        for i in range(100, 2000):
            r = i / 100
            a = max(r - s['start'], 0.) / length
            if a >= 1:
                break
            base = cg * math.exp(-(r / cs) ** 2) + hg / (1 + (r / hr) ** 2) * max(0., 1 - (r / gr) ** 2) ** 2
            if strength * GAIN * math.exp(-a) * (1 - a) ** 2 >= THRESHOLD * base:
                seen = r
        out.append(((k - s['offset']) * 360 / s['n'] % 360, length, strength, seen))
    return out


def main():
    for step in STEPS:
        rows = reaches(step)
        print('step %s: %d of %d rays visible; angle deg / length / strength / visible to R: %s' % (
            step, sum(1 for row in rows if row[3] > 0), len(rows), ', '.join('%.0f/%.2f/%.2f/%.1f' % row for row in rows)))


if __name__ == '__main__':
    main()
