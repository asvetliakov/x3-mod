# Specular anti-aliasing for the converted linear material shaders


Status (2026-09-14 evening): NOT ratified. The simulation in this note finds the uncorrected shader
brightens lit geometry with distance (+11 % at 4×) and darkens only off-peak geometry (≈1 %), which
contradicts the sign argument of the RE note; the detached fixture below (real pair, real textures, 1×/2×/4×)
decides the mechanism before any correction is implemented.

Design note, 2026-09-14 (read-only: no game, no Wine, no source edits). How the converted
bump-family pixel shaders (goal 6, [scene-linear-materials.md](scene-linear-materials.md))
correct the filtering error from normal-map minification, established for the docking-port
pair in [station-material-distance.md](../reverse-engineering/station-material-distance.md)
and for asteroids in [asteroid-specular-minification.md](../reverse-engineering/asteroid-specular-minification.md).

## Decision

**Option 2, precomputed variance channel.** At texture load the proxy builds, for each
known AG normal map, an 8-bit mip chain of the shortfall `1 − |N̄|` of the box-averaged
decoded unit normals; the converted PS samples it through one extra sampler at the
material's own UV and applies Toksvig to the native lobe (`f = |N̄| / (|N̄| + p(1 − |N̄|))`,
`p' = f·p`, peak `× f`). Diffuse and the cube term stay native in the first slice. It is
exactly the native program where the shortfall is 0 (all of mip 0), costs one 8-bit fetch
and about eight ALU slots per pixel, is static texture-space data (what TAA needs), and
uses only documented D3D9/D3DX calls the proxy already intercepts.

## Numbers

Texture evidence (whole 1024² `…windowedgrid_bump`, `p = 6`): mean `|N̄|` 0.9931 / 0.9839 /
0.9581 / 0.9452 at box levels 1 / 2 / 4 / 10; Toksvig peak factor 0.908 at the run-39 rect
(level 2.59), 0.847 at 2× (3.59), 0.802 at 4× (4.59); alpha covariance only 0.995 / 0.991 /
0.985. The factor is the *magnitude* of the lobe error, not the sign of the change.

Offline simulation for this note (pure Python on the local untracked DDS; flat quad,
captured light colours 112° apart, constant `m̄ = 0.596`, luma-weighted mean over the whole
map; reference = box-filtered mip-0 shading). Shader output over reference, uncorrected →
Toksvig-corrected:

| view, light 0 | spec share | 2.59 | 3.59 | 4.59 |
|---|---|---|---|---|
| head-on, light 30° | 0.59 | 1.003 → 0.997 | 1.029 → 1.015 | 1.069 → 1.043 |
| view 45°, light at mirror | 0.79 | 1.013 → 0.976 | 1.056 → 0.970 | 1.115 → 0.968 |
| view 60°, light 20° (off-peak) | 0.07 | 1.008 → 1.003 | 0.998 → 0.996 | 0.989 → 0.989 |

(a) The sign of the uncorrected error is geometry-dependent: the renormalised mean normal
sits on the highlight more often than the true texels, so lit ports get **brighter** with
distance (specular alone +22 % at level 6, mirror case) and only off-peak ports darken
(−1 %). The RE note's Jensen argument omitted the `1/|N̄|` boost renormalisation gives
both `N·L` and the lobe (diffuse alone +5.4 % at level 10 in every geometry). The reported
darkening is therefore not explained by this channel as simulated; the fixture decides,
and if it agrees the darkening needs another owner. (b) Toksvig removes most of the error
where specular dominates (1.115 → 0.968) but cannot invent wing energy where the mean
normal sees no highlight (off-peak specular 0.34 of reference at level 4; 0.7 % of
luminance). A diffuse `× |N̄|` term helps head-on (1.024 at 4.59) but hurts mirror (0.957)
and off-peak (0.945); it stays a fixture toggle, not part of the first slice.

Per-texel shortfall at level 1: mean 0.0069, p99 0.10, max 0.50; L8 stores
`min(1, 2·(1 − |N̄|))` (1/510 in `|N̄|`, ~1.2 % of peak per step at `p = 6`); texel 0 is identity.

## Options compared

1. **Runtime variance from mips** (`texldd` child taps or `texldl` at LOD±1). Every fetch
   is renormalised, so the estimator sees only the octave above the sampled level:
   simulated four-child-tap `|N̄|` is 0.986–0.996 where the truth is 0.947–0.958, giving
   1.081 instead of 0.968 at 4.59 (mirror). ~50 slots on the 208-slot dual PS plus an
   explicit `dsx/dsy` LOD, or magnified pixels read bilinear spread as roughness. Loses.
2. **Precomputed shortfall chain** — chosen.
3. **LEAN/CLEAN moments.** Three channels (4 MB per 1024² map against 1.4 MB) and a
   Beckmann lobe on the projected half vector; the lobe changes everywhere including
   mip 0, against the coverage-ledger rule to exclude loss of native gloss before artistic
   tuning. Anisotropy is not needed at `p = 6`. Loses on identity.
4. **Screen-space `dsx/dsy` of the normal.** Zero on the port's six flat triangles;
   on the sampled normal it is the option-1 estimator with a jitter-dependent footprint,
   i.e. per-frame roughness under TAA. Loses on both counts.

## Design

- **Producer.** `src/proxy/loading_trace.cpp` already hooks
  `D3DXCreateTextureFromFileInMemoryEx` and sees the DDS bytes and the created texture.
  For a 2D DXT5 image in the role table it decodes mip 0 (AG → unit normal), box-filters
  the unit normals and creates an L8 texture of the same size and level count in
  `D3DPOOL_MANAGED` (non-Ex device per [renderer-device-creation.md](renderer-device-creation.md);
  survives Reset with no proxy work), filled via `LockRect` on the proxy's own texture.
  Load cost: one DXT5 decode plus ten box levels per map, measured by the existing load
  span; loading time only.
- **Role table.** The port bump's DXT5 R/B endpoints are not constant (30 values, mode
  45 %), so DXT5nm sniffing fails. Match a cheap hash of the in-memory DDS against
  content hashes of the 278 DXT5 `dds/*_bump.pck` images (derived, documentable). Bound if
  all were resident: 364 MB L8 (109 at 1024², 32 at 2048²), 182 MB as DXT1 grey; the first
  slice lists only maps bound by converted pairs in captures (the port map: 1.4 MB).
- **Binding.** At a routed bump-family draw the route looks the app's s1 pointer up in a
  small map (pointer → chain, invalidated on release), `SetTexture(free stage, chain)`
  plus stage-1 filter states, restored at the existing restore points (the mip-bias
  mechanism in `motion_output.cpp` is the template). The port's free stage is s5
  (declared s0..s4; s5/s6 bound but unused). Without a chain the route binds a 1×1 zero
  texture, so one variant serves all draws.
- **Shader.** `dcl_2d s5`; `texld` at `v1.xy`; `L = 1 − 0.5·s`; `f = L·rcp(L + p·(1 − L))`;
  `p' = f·p` feeds the existing `pow`; both `s_k` scaled by `f`. Native power, strength,
  cube and lightmap terms untouched; the edit sits inside the lobe before the linear
  gains. About +9 slots and one temporary on the 208-slot dual PS.
- **Hot path.** Per routed draw one lookup, one `SetTexture`, up to three sampler states
  and their restore, no allocation; per pixel one 8-bit fetch at the same LOD as s1.
- **Native Windows.** IAT hook of the game's D3DX import (already portable),
  `CheckDeviceFormat(D3DFMT_L8)` with A8R8G8B8 fallback, managed pool, `SetTexture`.
  Nothing Wine-specific; native behaviour unverified (the user cannot test Windows).

## Verification

Detached fixture: port VS/PS original and corrected dual PS on a quad with the real
untracked textures from `/tmp/x3-port-tex/` (hashes recorded; a synthetic bimodal AG map
for the tracked run), captured constants, the three geometries above, rect widths 1024
(mip 0), 170 (run 39), 85, 43 px; readback, luma mean over the rect, ratio to the
box-downsampled mip-0 render. Acceptance: corrected `|ratio − 1| ≤ 0.05` at every size and
geometry; uncorrected mirror ratio ≥ 1.08 at 43 px (power check); mip-0 render bit-exact
corrected vs uncorrected; s5 state restored after each draw; Reset twin. Live check:
fade-witness rect statistics at the run-39 port draws, correction on/off (expected −0.6 %
to −3.7 % at level 2.59), plus one user approach capture of one port node at ≥ 2× distance.

## Unknowns

- Whether every game texture load goes through the D3DX hook and whether the in-memory
  DDS equals the catalogue image: load-span count versus bound textures, and the hash of
  the `data` argument for the port's three loads against the RE note's SHA-256.
- Texture release observation for the pointer map (typed COM tracking or a `Release`
  wrapper on the returned texture).
- Real port geometry and UV bounds (whole-map averages here); the fixture and one
  approach capture decide whether the observed darkening is this channel at all.
