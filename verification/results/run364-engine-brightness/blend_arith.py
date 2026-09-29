#!/usr/bin/env python3
"""Q2 arithmetic, grey texels (rgb equal): engine-space result of the game's blend on the FP16 target (unclamped) vs
native A8R8G8B8 (clamped per blend), then display: native = clamp(e) as presented; HDR = AgX(decode_gamma22(e)) at EV 0
(run364 exposure) using the replay_common port of src/temporal/agx.hlsl."""
import sys, numpy as np
sys.path.insert(0, '/Users/asvetl/x3-mod/verification/results/run340-run91a-pan-replay')
from replay_common import agx
def show(label, e_hdr, e_nat):
    h = agx(np.array([[[e_hdr] * 3]], float), 0.0)[0, 0, 0]; n = min(max(e_nat, 0), 1)
    print(f'{label:52s} engine FP16 {e_hdr:.3f} (linear {max(e_hdr,0)**2.2:.3f}) native {n:.3f} | display HDR {h:.3f} native {n:.3f} ratio {h / n:.2f}')
s, d = 0.8, 0.05; show('screen ONE/INVSRCCOLOR s=0.8 over d=0.05', s + d * (1 - s), s + d * (1 - s))
s, d = 0.3, 0.05; show('screen s=0.3 over d=0.05 (disc fringe)', s + d * (1 - s), s + d * (1 - s))
s, d = 0.8, 3.0; show('screen s=0.8 over d=3.0 (disc over gained card)', s + d * (1 - s), s + 1.0 * (1 - s))
for s, d in ((0.2, 0.05), (0.4, 0.1), (1.0, 0.5), (1.0, 1.0)):
    show(f'hull card ONE/ONE gain 2 s={s} over d={d}', d + 2 * s, d + s)
    show(f'hull card ONE/ONE gain 1 s={s} over d={d}', d + s, d + s)
