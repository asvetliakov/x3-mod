#!/usr/bin/env python3
"""Shadow-map window around a world point (default: the Titan caster origin 0,0,173.29) per frame and
cascade; writes a 600-texel crop PNG and prints the remote-station texel share (depth < 0.7) and the
share of 'hole' texels (not station) inside the crop. usage: titan_window.py CASCADE HALF FRAME..."""
import sys, subprocess, numpy as np
from PIL import Image
sys.path.insert(0, __file__.rsplit('/', 1)[0])
from project import rows, vec, CAP
P = np.array([0., 0., 173.29])
c, half = int(sys.argv[1]), int(sys.argv[2])
for f in sys.argv[3:]:
    cam, cas = rows(f); k = cas[c]
    r, u, ce = vec(k['right']), vec(k['up']), vec(k['center']); E = float(k['extent']); N = int(k['size'])
    q = P - ce; col = int((q @ r / E * .5 + .5) * N); row = int((-(q @ u) / E * .5 + .5) * N)
    m = np.array(np.memmap(CAP + f'shadow_map{c}_1_{f}.r32f', dtype='<f4', mode='r', shape=(N, N))[max(row-half,0):row+half, max(col-half,0):col+half])
    if m.size == 0:
        print(f, f'c{c} titan texel=({col},{row}) outside the map'); continue
    st = m < 0.7
    v = np.full(m.shape, 0, np.uint8); v[st] = 255; v[(~st) & (m < 0.99999)] = 90
    Image.fromarray(v).save(f'win_c{c}_{f}.png')
    print(f, f'c{c} titan texel=({col},{row}) crop={m.shape} station={st.mean():.3f} other_caster={((~st)&(m<0.99999)).mean():.3f} empty={(m>=0.99999).mean():.3f}')
