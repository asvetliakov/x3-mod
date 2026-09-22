"""Estimates behind docs/architecture/taa-motion-history-weight.md section 3 (not measurements).
cap(v) = max(F, 1 - (1-F)(v^2 - V0^2)/(V1^2 - V0^2)) clamped to 1; sigma model sigma^2 = s0^2 + c w/(1-w)
calibrated on the run254 points (w 0.9 -> sigma 1.0, rest 0.6); alias amplitude sqrt((1-w)/(1+w));
Laplacian energy kept by a Gaussian of sigma on a stripe of period P: exp(-4 pi^2 sigma^2 / P^2)."""
import math
F,V0,V1=.8,2.,8.
D=V1*V1-V0*V0;A=-(1-F)/D;B=1+(1-F)*V0*V0/D
cap=lambda v:min(1.,max(F,v*v*A+B))
print(f'A={A:.6f} B={B:.6f}; cap at 1/2/3/3.31/4/5/6/8/12:',[round(cap(v),3) for v in (1,2,3,3.31,4,5,6,8,12)])
print('cap reaches 0.97 at %.2f px, 0.9 at %.2f px'%(math.sqrt(V0*V0+(1-.97)/(1-F)*D),math.sqrt(V0*V0+(1-.9)/(1-F)*D)))
s0=.6;c=(1.0**2-s0**2)/(.9/.1)
sig=lambda w:math.sqrt(s0*s0+c*w/(1-w));al=lambda w:math.sqrt((1-w)/(1+w))
for w in (.97,.93,.9,.893,.8,.7):print(f'w={w}: sigma_est={sig(w):.2f} alias_amp={al(w):.3f}')
for s in (.6,.8,1.0,1.4):print(f'sigma={s}: energy kept on period-4 stripe={math.exp(-4*math.pi**2*s*s/16):.3f}')
