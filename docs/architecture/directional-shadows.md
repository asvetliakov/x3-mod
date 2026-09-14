# Directional (sun) shadows and self-shadowing on ships and stations

Design note for user objective 8, written 2026-09-15 after runs 19–21 and the ratified
[ambient-occlusion-scale.md](ambient-occlusion-scale.md) (AO stays default-off; no sun-weighted
"AO v2"). For ratification by the main session. **Nothing here is implemented.** Owning
implementation notes when built: this file; ledger `../verification/directional-shadows.md`.

## Decision

Build **screen-space sun shadows (route A) first**, as one full-resolution quad at the route's
scene-end hook before the TAA resolve: a view-space ray march toward the sun over the routed
depth RT2, applied as an exact multiply of the sun-lit share of each pixel. Before either route
ships, the converted materials must **expose a sun-lit share lane**: RT2 grows from `R32F` to
`G32R32F` and every converted pair writes `oC2.g = lum(A·D0) / lum(L)` beside the depth, so the
shadow multiplies only the sun lobe and never the fill light, point lights, emissive or lightmap
terms. **Cascaded shadow maps (route B)** come later, reuse A's apply quad, and buy off-screen
and hidden casters and long shadows at a per-draw replay cost (0.7–2.6 ms CPU at run-48 draw
counts) that A does not pay.

## 1. What exists and what the numbers are

- **Depth.** RT2 is `R32F` device depth for the 169 routed SM3 rows (168 converted pairs plus
  the six glass pairs), `-1` sentinel elsewhere; the cutout contract has alpha-passing
  samples write depth ([alpha-tested-materials.md](alpha-tested-materials.md) §3; run-19/20
  accepted the cutout exemption, the per-sample depth is fixture evidence). Rasterized with the
  colour's per-draw jitter. Native-path draws (SM2/SM1, particles, background, HUD) leave the
  sentinel: they are neither casters nor receivers on either route.
- **Camera.** `CameraState` latched at the Clear: `m00/m11/m20/m21`, the view rotation `r[9]`
  and translation `t[3]` (`src/renderer/camera_reprojection.h:27-31`); `w_clip = z_view`, so
  `p_view = ((x_ndc − m20)/m00 · z, (y_ndc − m21)/m11 · z, z)` with
  `z = m32/(d − m22)`, `m22 = 1.000003`, `m32 = −6.0000184` (zn 6 = 1.2 m, zf 2·10⁶ = 400 km;
  1 m = 5 view units). Cockpit zoom moves zn to 54–104 units; the linearization must keep
  using the latched `projection[10]/[14]`, as AO does.
- **Sun.** `LightDir_Dir0` is a **world-space** object→light unit vector, per submitted node,
  bit-identical across frames while the camera rotates, fitted to one world point at 0.0003°
  median residual, 0.6° spread over a scene; declared by 507 of 751 programs at eleven
  registers (c4 153, c1 128, c22 64, c5 58, c0 38, c19 32, c7 14, c21 6, c18 6, c39 5, c13 3),
  so a consumer reads the pair's CTAB register
  ([camera-state-and-frame-routine.md](../reverse-engineering/camera-state-and-frame-routine.md),
  "Ambient occlusion inputs", round 2). The light record is also readable hook-free from
  engine globals (`+0xb0/b4/b8` world position). There is **no ambient term**: the converted
  law is `L = A·(P + M + D) + R + E` ([scene-linear-materials.md](scene-linear-materials.md)),
  where `D` is the sum of the two directional lobes (sun `D0` = (170,200,150)/256 and fill
  `D1` = (33,66,55)/256, diffuse plus the legacy specular power) and `R` is the environment
  cube reflection.
- **Hook and chain.** `MotionOutput::scene_end_hook` runs after the lazy-binding flush and
  before `taa_->run`; AO's four-quad chain lives there with a saved `D3DSBT_ALL` block, the
  `source=copy` fallback, per-frame gates and fail-closed reasons
  ([ambient-occlusion.md](ambient-occlusion.md) §3, step 2). Any new pass copies that shape.
- **Route mechanics.** The route does not re-issue draws: it substitutes the pair's variant
  VS/PS on the engine's own `DrawIndexedPrimitive`, binds RT1/RT2, rewrites the submitted
  `c24–27` rows with the jitter and restores ([live-motion-route.md](live-motion-route.md)
  "Frame flow", "Jitter"). Replay of recorded draws exists only as reference infrastructure:
  `motion_live_replay_available=false`, because application `Lock/Unlock` runs outside the
  wrapper's mutex and the admission contract is not integrated
  ([motion-replay-exclusion.md](motion-replay-exclusion.md),
  [motion-resource-leases.md](motion-resource-leases.md) "Boundary and replay transaction").
- **Draw counts, run 48** (923 `motion_output_frame` lines with draws > 0): scene draws
  p10/p50/p90/max 48 / 143 / 363 / 1,108; routed 9 / 118 / 285 / 848 (bump p50 69, cutout
  p50 18, max 124). The emission-draw-order capture has 375 main-scene colour draws per frame
  over 24 frames.
- **Frame cost.** Run-11 at-rest median 4.10 ms at 1280×768; run-48 `frame_end` windows from
  frame 3,000 on give p50 8.7 ms per frame (loads and captures included, not an at-rest
  figure). AO's chain: 0.51–0.78 ms GPU-fenced at 768p and 1.33–1.39 ms at 1080p for four
  half-res quads (each quad 0.2–0.47 ms fenced, dominated by its flush), `cpu_us` median
  135 µs in game. GPU timestamp queries return `D3DERR_NOTAVAILABLE` on CrossOver Preview,
  so in-game GPU cost is only measurable by the hotkey A/B at rest.
- **Slot budget.** Converted pairs sit at ≤ 180 weighted PS slots
  ([linear-standard-materials.md](linear-standard-materials.md)); the resolve is 507/512;
  a scene-end quad has the full 512.

## 2. The shadow term in the material law (the material work, first)

A sun shadow `s ∈ [0,1]` belongs on the sun lobe only:
`L_s = A·(P + M + D1) + R + E + s·A·D0 = L·(1 − f·(1 − s))` with `f = A·D0 / L` per channel.
The environment reflection `R` is the cube map, not the sun, and stays; if a run shows sun
glare in the cube, a share `ρ·R` can be folded into `f` by one launcher option (default 0).

**Lane.** RT1 is fully used (motion `.xy`, expected depth `.z`, validity `.w`;
`src/temporal/resolve.hlsl:203-208`), and a fourth simultaneous target collides with the
in-place brackets and needs `NumSimultaneousRTs ≥ 4` (rejected in AO §1). RT2 instead becomes
`D3DFMT_G32R32F` (documented; already a render target in the HDR meter chain,
`src/temporal/hdr_meter_level0_ps.hlsl`): `oC2.r` stays `z/w`, `oC2.g = f_lum =
lum(A·D0)/max(lum(L), ε)`. The luminance share is one scalar; the per-channel form would need
three lanes, and the residual is a tint only where `E`, `M` or `P` differ in hue from the sunlit
term — acceptable, and the debug view shows it. The transformer already isolates the directional
lobes (`directional == (light1 ? 4 : 1)`, the specular and cube samplers;
`src/renderer/linear_material.cpp:770-844`) and inserts the depth output; the lane is one more
insertion after the lobe sum: two `dp3`, one `rcp`, one `mul`, one `mov` ≈ 5 slots per routed
pixel (≤ 185 of 512), +4 B/px of RT2 bandwidth (+3.75 MiB at 768p), nothing per draw on the
CPU. Every RT2 reader (resolve, AO linearize) reads `.r` and is unchanged; the sentinel fill
writes `(−1, 0)`. Re-qualification: the 168 pairs through the existing corpus and the detached
GPU fixture with the law `L_s` checked within one FP16 ulp, plus the mixed-format MRT self
test extended to `G32R32F` beside the main format.

**Application** stays the AO pattern: one `ZERO/SRCCOLOR` blend of `(1 − f·(1−s))^(1/2.2)` into
the owning target (8-bit or FP16, both gamma-2.2 encoded), exact by the encode identity. What
the full-pixel multiply AO used would do wrong here: a shadow is large and near-binary, so
emissive windows, lightmaps, engine glow and point-lit hull inside a shadow would go dark by
the whole `s`; AO hid the same error behind a 0.5 floor. The lane makes the multiply honest
for **both** routes, which is why it precedes them.

## 3. Route A — screen-space sun shadows

**Placement.** A `renderer::SunShadowPass` in `scene_end_hook` after `restore_bindings`, before
the AO chain (if on) and the resolve, under `taa_call`, with AO's gates and reason codes and
the `source=copy` fallback. It reads RT2 (`G32R32F`) and blends into the owning target: **one
quad, no intermediate target**.

**Sun direction per frame.** Extend the existing slot-109 `SetPixelShaderConstantF` hook (it
already watches `c216–217`) to copy 16 bytes when a write covers the bound pair's
`LightDir_Dir0` register — a column added to the pair table from each original PS's CTAB, which
the transformer parses. First routed draw of the frame wins (0.6° spread, irrelevant to a
march); rotate at scene end: `L_view = L_world · R` from the latch (row-vector convention).
Alternative: the light record via `engine_memory` (hook-free, private layout, same mechanism
as the object observers). Both are a per-frame 16-byte read, zero per-draw cost.

**March.** Per full-res pixel with `depth ≥ 0`: `p = p_view(uv, z)`; step
`p_k = p + (k + δ)·Δ·L_view`, `k = 0..15`, `δ` a 4×4 Bayer offset rotated by `jitter_index`;
`Δ` set so the projected march covers **N full-res pixels** (`N = 48` default,
`--sun-shadow-length`): world length `N·z/(m11·h/2)` = `z·N/512` at 768p, i.e. 0.6 m on the
own ship at 6 m, 47 m at 500 m, 280 m at 3 km. Each step projects (`uv_k = (x/z·m00 + m20,
y/z·m11 + m21)`, one `rcp`), taps the depth, and occludes when `z_k − z_tap ∈ (b, T)`:
`T = max(0.02·z, 2 units)` (a surface is assumed 2 % of its distance thick; run-tuned),
`b` = slope bias from the 5-tap depth normal plus twice the float quantization `z²·10⁻⁸`.
`s = 1 − (occluding steps)/16` gives a dithered penumbra; ≈ 8 instructions per step,
≈ 150 slots total. Marching in view space makes the "sun in front of the camera" case
converge to the sun's vanishing point correctly; within ~10° of the view axis (either sign)
the term fades to 1 (occluders behind the camera are unknowable).

**Why AO's scale problem does not recur.** AO integrates a *world-sized* hemisphere, so one
radius cannot serve 6 m and 3 km. A shadow is the projection of a visible caster onto a
visible receiver; its screen length is set in pixels and its world length scales with depth
by construction. The same strut at 500 m and at 3 km throws the same 48-pixel shadow, which
is what the eye expects. The run-20 depth captures put 53–67 % of the frame at 10 m–1.4 km
on the station approach, all inside that regime.

**Temporal stability.** RT2 and colour share one jittered raster; the march reads the same
surface the colour was shaded on. The dither rotates with `jitter_index` and integrates
through the resolve at 0.9 like GTAO's noise (AO §2: ~1 % ripple for a 10 % residual). A
shadow edge is a step function of the depth surface and moves with the object, so the
resolve treats it as current-frame content; if a run shows crawling, the fallback is AO's
shape (half-res march, quincunx blur, bilateral apply) at AO's measured 0.51–0.78 ms.

**What A cannot do.** Casters off screen or behind the visible depth layer (the thickness
heuristic guesses), shadows longer than `N` px (a station arm across its own kilometre of
hull at grazing sun is truncated; a coarse second march with growing steps extends `N` to
128 px for ~8 more taps), shadows onto or from native-path draws, and any shadow whose
caster the engine culled. Objective 8's "self-shadowing on ships and stations" is the
on-screen case; the rest is route B.

**Cost.** GPU: 983,040 px × ≤ 16 taps × 8 B ≈ 126 MB of cached reads and ~150 slots per pixel;
against AO's per-quad fenced 0.2–0.47 ms (flush included) the estimate is **0.3–0.5 ms at
768p, 0.5–0.8 ms at 1080p**. CPU ≈ 100–150 µs (AO's 135 µs is four quads plus the block).
Per draw: the lane only. Memory: the RT2 growth, nothing else. **Budget: +0.5 ms on the
at-rest median at 768p, +0.8 ms at 1080p**; above it, 8 steps or half-res; above +1.0 ms,
refuse by default. Default off behind `--sun-shadows` (requires `--motion-output --taa`),
hotkey off/on in the comparison-hotkey family, `--sun-shadow-debug` grayscale of `s` and
of `f`, `--sun-shadow-timing` frame line.

**Native Windows.** `CreateTexture(G32R32F | R32F, RENDERTARGET)`, ps_3_0/vs_3_0, post-pixel-
shader blending on the owning format, `SetPixelShaderConstantF`: documented D3D9 only,
cross-compiled under the SSE2/stack contract, runtime unverified (bullet for
[platform-portability.md](platform-portability.md) when built).

**Acceptance.** *Fixture* (`run_sun_shadow.py`, pattern `run_ambient_occlusion.py`): synthetic
`G32R32F` depth+share of a box on a plane at three sun elevations with the analytic shadow
footprint (`s = 0` inside it within ±1 px, exactly 1 beyond and on the sentinel, exactly 1
on a plane facing the sun with no caster), an emissive strip (`f = 0`) crossing the shadow
bit-identical, the `L_s` law within one FP16 ulp on the converted-pair fixture, a moving
camera through the real resolve with < 2 % ripple after 16 frames, Reset mid-chain,
capability twins, EVENT-fenced timing at 768p and 1080p. *Live* (`aohook` script): sentinel
identity, HUD phase untouched, four Resets. *User run*: an Argon station at 300–800 m with the
sun grazing, a capital hull at 200–500 m, the own ship at rest; at each, F8 on, hotkey, two
seconds, F8 off (seconds apart, as run 21 required). Numbers: every enabled frame
`ran=1 applied=1 reason=ok`, `cpu_us` median ≤ 250 µs, in the station pair ≥ 5 % of routed
pixels with on/off luminance ratio < 0.7 (a shadow, not AO's 0.9), pixels with `f < 0.1`
changed by < 2 %, no darkening farther than `N` px from any depth discontinuity, the user
reports readable self-shadowing and no crawling, and the at-rest `frame_end` A/B within the
budget.

## 4. Route B — cascaded shadow maps from the sun

**What is missing.** The route never issues a draw of its own. B needs, per routed draw, a
record (the `RigidDrawKey` fields already read — VB/IB identities, declaration, stream
offset/stride, topology, index and vertex ranges — plus the unjittered `c24–27` rows, cull
mode, and for cutout rows the s0 texture, alpha function and reference), leases on the
VB/IB/declaration, and a **replay at scene end** into per-cascade `R32F` targets with a
depth-only pair: VS `clip = rows·pos` → `p_view = (x/m00, y/m11, w)` (exact, linear, valid
for vertices behind the camera) → `S_c = SunProj_c · SunView · V⁻¹` from the latch; PS writes
sun-space depth to colour (a sampled depth-stencil is vendor FOURCC, not documented D3D9).
Same-frame replay means no lag; the alternative, sampling last frame's map inside the
material shaders, lags moving ships by one frame (3 m at 200 m/s, 60 fps) and needs a
sampler plus ~30 slots in 168 pairs. B's apply is A's quad with a 3×3 manual PCF (≈ 50 slots)
instead of the march, or `min(s_A, s_B)` with both.

**Leases.** The record/replay window is the scene-draw-to-scene-end interval. The engine's
`Lock/Unlock` is outside the wrapper mutex; the leases note requires per-draw revision
revalidation immediately before replay and fail-closed skipping. B therefore integrates the
replay admission contract that the live route pivoted away from; whether X3 locks a mesh
buffer between a scene draw and scene end is unmeasured (a one-run counter of Locks on leased
buffers inside the scene phase settles it).

**Cascades.** The sun is world-fixed, so the cascade orientation is fixed and texel snapping
in sun space removes camera-translation swim. Three slices of the latched frustum:
6–250 units (the own ship, 1.2–50 m), 250–5,000 (50 m–1 km), 5,000–50,000 (1–10 km); casters
per cascade culled by the fade-route AABB (model-local box × node scale × rows). Resolution
2048² × 3 = 48 MiB `R32F` plus one 16 MiB `D24X8` (1024²: 12 + 4 MiB). Glass rows are excluded
as casters (table flag); cutouts alpha-test in the caster PS; blended particles are not routed.

**Cost.** CPU per replayed draw: 5–7 native calls (`SetStreamSource`, `SetIndices`,
`SetVertexDeclaration`, `SetVertexShaderConstantF` ×1, `DrawIndexedPrimitive`, cutout
texture) at ≈ 0.3–0.45 µs per call (the fade bracket: 350 calls in 100–160 µs), i.e. ≈ 2–3 µs
per draw per cascade. At run-48 counts: **p50 118 draws × 3 cascades ≈ 0.7–1.1 ms, p90 285
≈ 1.7–2.6 ms, max 848 ≈ 5–8 ms** before per-cascade culling, plus GPU vertex work ×(1 + cascades)
and three render-pass switches — 10–30 % of a 8.7 ms frame, 20–60 % of the 4.1 ms at-rest
median, against A's flat ≤ 0.5 ms. Recording adds per-draw work on the hot path (lease
AddRef/Release, ~100 bytes per record); the route today has no per-draw allocation.

**What B buys.** Casters off screen, back-facing and hidden behind the visible layer, long
shadows across a hull, true penumbra by PCF. It still cannot shadow from nodes the engine
culled (a station behind the camera): that needs the proxy to traverse the scene graph and
submit meshes itself, a private-structure step beyond this note.

**Staged plan.** (1) Cascade 0 only, fitted to the own ship, casters = routed draws whose AABB
meets the slice (5–20 draws): proves record/lease/replay on a bounded list with a fixture that
replays synthetic geometry and checks the sun depth against an analytic projection.
(2) Cascades 1–2 with culling and the cost line per cascade. (3) `min(s_A, s_B)`.

## 5. Order, and what the other buys later

1. **Lane** (`G32R32F` RT2, `oC2.g = f`), re-qualified through the existing corpus. Needed by
   both routes; no visible change on its own; per-pixel ≈ 5 slots.
2. **Route A** behind `--sun-shadows`, default off, with the debug view and hotkey; one user
   run on the acceptance above. Answers the appearance question objective 8 actually asks —
   do sun shadows read on X3's ships and stations — at a flat cost.
3. **Route B stage 1** only if the run asks for off-screen or long shadows; it inherits A's
   apply and lane and pays for a replay admission contract.

| Step | Per-draw hot path | Scene end | Memory | Budget |
| --- | --- | --- | --- | --- |
| Lane | +5 PS slots, +4 B/px RT2 | — | +3.75 MiB (768p) | no measurable change in the A/B |
| A | none | one quad, ≈ 0.3–0.5 ms (768p) | none | +0.5 ms median at 768p, +0.8 at 1080p |
| B (3 cascades) | record + leases | 0.7–2.6 ms CPU replay + PCF quad | 28–64 MiB | to be set by stage 1 |

## 6. Alternatives considered

- **Full-pixel multiply without the lane** (AO's law): zero material work, but darkens
  emissive windows, lightmaps and point-lit hull inside shadows; a preview only, not shippable.
- **Shadow-map sampling inside the converted pixel shaders**: a sampler, ~30 slots and a
  one-frame lag in 168 pairs, plus the replay it needs anyway; loses to B's scene-end apply.
- **B first**: physically complete, but its cost scales with draw count (up to 5–8 ms at
  run-48 peaks) and it reopens the replay admission contract before anyone has seen whether
  sun shadows read at X3 scale. Loses on cost and risk.
- **Sun-weighted GTAO** (the former "AO v2"): rejected by the user; it also stays bound to a
  world radius and would inherit AO's scale problem.

## 7. Unknowns and what settles them

- **Sun read path**: the PS-constant copy needs the per-pair register column (CTAB, offline);
  a capture check that the first routed draw's value matches the run-39 world fit settles it.
  The light-record path needs one Clear-hook read of `+0xb0/b4/b8` against the same fit.
- **`G32R32F` beside the main format as MRT**: `MRTINDEPENDENTBITDEPTHS` is already required;
  the mixed-format self test extended to `G32R32F` proves it on this backend; native Windows
  unverified.
- **Lane insertion per family**: the shared hull family retains its own lobes and the six
  glass pairs have no diffuse sun lobe (`f = 0` or the gloss term); the transformer's
  per-family proof decides; the SM1 emission path is not routed and gets no lane.
- **Thickness `T` and length `N` at X3 scale**: run-tuned; the fixture pins the law, not the
  constants.
- **In-game GPU cost**: unmeasurable through queries on this backend; the at-rest hotkey A/B
  is the instrument, as for AO.
- **B's lock window**: whether static mesh buffers are locked between a scene draw and scene
  end (a Lock counter over one run).
- **Far-pixel precision**: at 40 km (200,000 units) the depth quantization is 0.4 units
  (8 cm), far below `T`; at zf the march is meaningless and the term is 1 by the sentinel.
