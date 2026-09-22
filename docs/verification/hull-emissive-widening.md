# Hull emissive widening: verification ledger

Feature: `--hull-emissive-widening K,Q0,Q1` (`X3M_HULL_EMISSIVE_WIDENING`), design
[hull-emissive-widening.md](../architecture/hull-emissive-widening.md), problem
[thin-glow-lines.md](../architecture/thin-glow-lines.md). Default off; nothing installed.

## 2026-09-22: built and qualified on the fixture (WIP, worktree branch)

Source: transformer `linear_material.cpp` (`lightmap_widen_fetch`, `lightmap_widen_site`, opcodes 91/92/93 in
`body_shape`/`structure`), route `motion_output.cpp` (`configure_hull_emissive_widening`, c217.z in `evaluate_draw`,
selection in `bind_variant_pair`), law `fade_route_core.h::lightmap_widen_scale`, option `capture.cpp`, launcher
`tools/manage.py`. Bottle X3 (CrossOver Preview, arm64 Wine/FEX), fixture DLL `build/d3d9.dll` of this worktree
(sha256 `c471cc12…`, parent commit 6019d936 + the WIP diff).

### Host

- `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_hull_emissive_widening
  verification.analysis.test_hull_lightmap_gain`: 15 tests OK. The Python reference rebuilds all 600 widened variants
  (100 programs x 2 fills x 2 depth modes + 2 share) byte for byte from the gained ones; rG = highest temporary + 1
  (max r24, share variants); +12 DWORDs, +3 instructions, +7 weighted slots everywhere; the 8 programs without the
  term (glass, asteroid) unchanged; `widen` with G = 1 refused (InvalidConfig).
- Coverage ([hull-emissive-widening-coverage.json](../../verification/results/hull-emissive-widening-coverage.json)):
  101 ps_3_0 programs of the local archive declare `LightMapTexSampler`; 100 widened (44 at s2, 56 at s3), the moon
  program `6aaaa2cb27e92cc8` excluded by design. Per family (programs / max widened plain / share slots): argon 12 /
  138 / 215, boron 8 / 149 / 223, paranid 12 / 137 / 206, split 12 / 138 / 215, khaak+teladi+xenon 12 / 139 / 216,
  terran 12 / 137 / 213, standard_lighting 18 / 139 / 216, xt_standard_lighting(+damage) 8 / 192 / 271,
  xt_terraformer 6 / 172 / 253. Largest widened variant 271 of 512 slots (was 264).
- Slot growth by the driver's own SM3 table; confirmed on the device by D3DX below (+7 on every drawn program).
- `python3 verification/probe/check_no_x87.py build/d3d9.dll`: 620 reachable functions, no violations (the
  footprint law is SSE scalar).
- `/usr/bin/python3 verification/probe/run_host_suite.py`: see the report line of the run that produced this ledger.

### Fixture (`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py ... seam-lightmap-widen-{off,k2,k3,k4,programs}`)

Record: [hull-emissive-widening-seam.json](../../verification/results/bottle-X3/hull-emissive-widening-seam.json)
(untracked capture logs beside it). 256 x 256 target, gain 4, Q = 0.5, 2 units/px, a 0.5-px strip light map
(256 texels over a 128-px quad, full box mip chain) drifting 6.125 px/frame over 8 phases at w = 4 / 128 / 512
(footprint 0.04 / 1.25 / 5 units/px: k = 1 / 1 + (K-1)/2 / K). 180 checks per K case, 99 for the programs case.

| K | uploaded k (below / inside / above) | widened draws | per-phase peak ratio min/max (h = v) | design floor 1 - 0.5/T | strip width at half peak (px) | strip energy vs off | panel interior vs off |
|---|---|---|---|---|---|---|---|
| off | 0 / 0 / 0 | 0 | 0.500 | 0.5 (T = 1: level 1 at 2 texels/px) | 1.375 | 1 | 1 |
| 2 | 1 / 1.5 / 2 | inside, above | 0.750 | 0.75 | 2.125 | 1.00003 | 1.00002 |
| 3 | 1 / 2 / 3 | inside, above | 0.867 | 0.833 | 3.125 | 0.9966 | 1.00003 |
| 4 | 1 / 2.5 / 4 | inside, above | 0.875 | 0.875 | 3.875 | 0.9964 | 1.00007 |

- Below Q0 (k = 1) every frame hashes equal to the option-off run's (the un-widened gained variant binds; no
  `texldd(k=1) == texld` assumption in the route). F4 off drops gain and widening together (0 / 0 draws); after
  Reset the far frame binds the widened variant again with k = K.
- Wide panel: interior mean within 0.007 % of the un-widened one at every K (no rescale, no dimming).
- Oblique quad (perspective rows p = 0.6), light-map stage LINEAR against ANISOTROPIC 16: the image hash changes
  with the filter for the un-widened `texld` (w = 4) **and** for the widened `texldd` (w = 512, K = 2/3/4), so the
  backend applies anisotropic filtering to explicit-gradient fetches (peaks k3: 1.137 linear / 1.151 aniso16).
- Programs case (11 pairs, one per drivable family group: standard/argon/shared/split/terran DEFAULT, argon/standard
  BUMP, boron, paranid, XT default, XT terraformer): `CreatePixelShader` accepts every widened program (`dsx`/`dsy`
  sourcing `v1` directly); D3DX (`C:\X3\d3dx9_37.dll`) slot counts gained -> widened: 79->86, 78->85, 79->86,
  78->85, 77->84, 90->97, 91->98, 89->96, 77->84, 129->136, 109->116 (+7 each, matching the table). The widened
  variant forced at k = 1 is **bit-identical** to the gained one on every pair (0 FP16 codes differ over the whole
  target). At k = 3 the strip peak is 0.634 of the un-widened one on every pair with energy within 0.4 %.
- Route: per-draw work is one multiply-add and clamp on the footprint the far fade already computes, uploaded in
  the motion ABI's existing two-vector write (`c217.z`); the level-count gate reads the sampler shadow (the
  `SetTexture` hook now installs for the option as it does for the mip bias). Startup lines
  `hull_emissive_widening_mode` / `_configured`, per frame `hull_lightmap_widen_frame` (k range), at detach
  `hull_lightmap_widen_summary` (session k range and widened draw count).

Not measured here: the post-resolve (TAA) peak change (the fixture runs TAA off; the design's mechanism (ii)),
DXT1 light maps (the fixture's maps are A8R8G8B8), flight cost. Native Windows: cross-compiled only
([platform-portability.md](../architecture/platform-portability.md)).
