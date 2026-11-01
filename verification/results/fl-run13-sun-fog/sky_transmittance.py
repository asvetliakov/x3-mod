"""Mean-law transmittance toward a sky pixel (the sun) for FL run 13, The Hole (foggreenoutlands, profile 2).
Inputs from the run-13 log: sigma=6.25e-06 chroma=0.2007,1.0000,0.1207 (volumetric_fog_cache event=config),
density_scale=1 strength=0.02 (volumetric_fog_frame), distance=3 cap_km=67.5 horizon=1 (volumetric_fog_distance_mode).
Constants from source: sigma_scale 8, extinction_tint .6, sky_cap 112500, taper_start 65000 (renderer/fog_look_math.h),
occupancy .24 -> rho_mean .10980 (renderer/fog_transmittance.h). Law: renderer/fog_transmittance.h (plain look column)
and the FOG_DISTANCE weight w(s)=min(1,(S0/s)^2)(1-smoothstep(.75C,C,s)) (fog_look_math.h:109)."""
import numpy as np
sigma, scale, ready, sigma_scale, rho = 6.25e-6, 1.0, 1.0, 8.0, 0.10980
chroma, tint = np.array([0.2007, 1.0, 0.1207]), 0.6
e = sigma * scale * ready * sigma_scale * rho; k = 1 + tint * (1 - chroma)
start, cap0 = 65000.0, 112500.0
D_plain = start + (cap0 - start) * 0.5
C = 3 * cap0; s = np.linspace(0, C, 2_000_001)
t = np.clip((s - .75 * C) / (.25 * C), 0, 1); ss = t * t * (3 - 2 * t)
w = np.minimum(1, (start / np.maximum(s, 1e-9)) ** 2) * (1 - ss)
D_dist = np.trapezoid(w, s)
print(f"extinction per unit e={e:.4g}  k_rgb={k.round(3)}")
for name, D in (("plain look column (plumes law, cap 112500)", D_plain), ("distance mode 3 column (cap 337500)", D_dist)):
    T = np.exp(-k * e * D)
    print(f"{name}: D={D:.0f} units tau={e*D:.3f} T_rgb={T.round(3)} lum(T)={T@[.2126,.7152,.0722]:.3f}")
