# Directional (sun) shadows and self-shadowing on ships and stations

Design note for user objective 8, written 2026-09-15 after runs 19–21 and the ratified
[ambient-occlusion-scale.md](ambient-occlusion-scale.md) (AO stays default-off; no sun-weighted
"AO v2"). Ratified route-B-first by the user on 2026-09-15. **Shader extraction is implemented and host-qualified; live lane/replay/shadow application remain unimplemented.** Owning
implementation notes when built: this file; ledger `../verification/directional-shadows.md`.

## Decision

Build **cascaded shadow maps (route B)** after the fill term. The required order is:
converted-material sun-lit-share lane, replay feasibility (Lock counter and one-cascade
depth replay of routed draws, without shading), then cascades and the scene-end apply pass.
**Screen-space sun shadows (route A) remain a fallback only if geometry replay proves
infeasible.** The route-A specification below is retained for that fallback and the shared
apply-pass design; it is not an implementation prerequisite.

Before shadows ship, RT2 grows from `R32F` to `G32R32F`: depth stays in `.r` and `.g`
records the sun's luminance share. This scalar approximation preserves zero-sun pixels;
it approximates the per-channel result when the other light terms differ in hue (§2).
Fill, point lights, emissive and lightmaps stay outside the numerator.

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

**Staged plan.** (1) Replay feasibility, without shading: count Locks on recorded buffers and
replay cascade 0 only, fitted to the own ship, casters = routed draws whose AABB
meets the slice (5–20 draws): proves record/lease/replay on a bounded list with a fixture that
replays synthetic geometry and checks the sun depth against an analytic projection.
(2) Cascades 1–2 with culling, the shared scene-end PCF apply, and the cost line per cascade.
Route A is reserved for infeasible replay, not a required combined pass.

## 5. Order, and what the other buys later

1. **Fill term**, qualified and offered for the user's appearance verdict.
2. **Lane** (`G32R32F` RT2, `oC2.g = f`), re-qualified through the existing corpus.
3. **Replay feasibility**: Lock counter and one-cascade depth replay of routed draws,
   no shading; validate leases, resource revisions, analytic depth and scoped cost.
4. **Cascades** with culling and the shared scene-end apply, after replay feasibility.
5. **Route A only as fallback** if replay proves infeasible.

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
- **B first (selected)**: the user chose geometry-based shadows despite the estimated
  draw-dependent cost. The replay-feasibility checkpoint measures that cost and resolves
  the admission contract before cascades and shading are implemented.
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

## 8. Receiver publication contract — ratified for the first lane checkpoint (2026-09-15)

**Status: ratified by the orchestrator for the first lane checkpoint.** This bounds the first
default-off lane deployment. It preserves the ratified lane → replay feasibility
→ cascades order; no screen-space shadow implementation is a prerequisite.
The receiver exclusions, unclipped FP16 radiance domain and point-sampled `.r`
history copy below are selected. Use `epsilon = 2^-20` (exactly representable). Earlier §2's five-slot estimate and
§7's glass uncertainty are superseded as evidence by the
[108-program extraction contract](../reverse-engineering/sun-share-material-contract.md).
That contract measured 152 sun MADs and requires parallel RGB propagation; it
does not yet prove generated shaders or framebuffer ownership.

### Recommendation: source extraction and receiver validity are separate

- Build and qualify extraction for **all 108 ordinary converted PS originals**:
  90 hull/asteroid/palette, four glass, 14 XT. S includes the authored sun diffuse
  and gloss, effective palette/albedo and XT occlusion; fill, D1, reflection,
  point light and emission contribute only to L. Glass is not a zero-sun family.
  Expose per-program extraction success independently of receiver admission,
  so a blended glass refusal cannot masquerade as successful zero extraction.
- Publish a receiver share only for ordinary unblended, depth-writing draws and
  the exact already-qualified alpha-tested cutout state. Opaque glass is eligible
  under the same state contract. The existing gate requires blend off and accepts
  only the exact cutout arm (`src/proxy/motion_output.cpp:3674`); the alpha test
  must discard color, depth and share together. Requalify its format-specific
  capability query when RT2 changes (`motion_output.cpp:783`).
- A receiver stores `(depth, f)`; invalid radiance/unsupported extraction with
  valid ordinary depth stores `(depth, -1)`, distinguished from proved `f=0`.
  The existing empty-depth clear remains `(-1,0)`. Validate `.r` and `.g` together
  before consuming. Preserve depth `.r`, alpha, motion and discard ordering;
  no later whole-register depth output may overwrite the share.
- **Blended glass, native source-over cutout, detached fade and the fused
  fade-band arm are not first-version receivers.** Native source-over cutout is
  deliberately exempt from a TAA miss (`src/proxy/linear_cutout.h:24`), which
  establishes no shadow-share validity. The fused fade arm masks RT2 entirely
  (`motion_output.cpp:426,445`); neither its retained background depth nor a
  source-only f describes the new owning color. Do not enable RT2 writes there
  or infer opaque coverage from the fade threshold.
- At scene end, exclude pixels covered by the **valid, same-frame composition
  coverage M**, even if their RT2 still describes an earlier opaque receiver.
  This conservatively excludes additive/screen composition too: their changes
  to L also stale the denominator. If a successful later scene color draw has
  no proved same-draw receiver update or conservative coverage (including native
  blended glass and refused composition), mark the **shadow frame unavailable**
  once an eligible receiver has been written. Do not guess overlap or reuse an
  earlier share. This flag alone must not invalidate ordinary TAA. Unknown
  coverage/state or failed publication also prevents shadow consumption.
  This conservative first boundary may exclude many live frames; report admitted
  receiver pixels and frame-refusal reasons before claiming useful coverage.

### Final color and the scalar approximation

First consumer domain: active FP16 owning target with the established gamma-2.2
material encoding, finite nonnegative L and S, componentwise `S <= L`, and no
output sanitizer clipping (`L <= 65504`). Compute S and L after the final material
RGB instruction, before transfer overwrites r11 (`linear_material.cpp:1210`;
`linear_xt_material_inc.h:282`). The sanitizer and encoder are explicit in
`linear_material.cpp:570`. Any violated domain gets invalid share, not a
sanitized numerator silently claimed to be exact. Eight-bit ownership and other
compatibility decode modes are initially consumer-disabled: target clamping is
another nonlinear operation not represented by the material ratio.

For `Y(L) >= epsilon`, use `f = Y(S)/Y(L)`. Then
`Y(L * (1 - f*(1-s))) = Y(L - (1-s)*S)` in real arithmetic. **Luminance is exact
in this domain; RGB hue is generally approximate.** Encoded FP16 storage and
blend rounding add numerical error, so this is not a bit-exact GPU claim. Exact
black with zero S is valid f=0; nonzero sub-epsilon L is invalid in the first
consumer policy. Saturation may guard roundoff but must not conceal an invalid
S/L decomposition. Multiplying the encoded owning color by the gamma-encoded
scalar preserves this identity only for the matching unclipped transfer.

Clipped/displayed-radiance approximation is an alternative for later ratification,
not the initial policy. Substituting sanitized L in the denominator does not
restore the unclipped shadow law. Exact per-channel sun removal needs more than
one scalar lane; a scalar with fixed f also cannot reproduce clipping transitions
for every shadow strength s.

### Fade ordering and a later blended-receiver alternative

The detached producer emits linear source L into oC1 and conservative coverage
into oC2, not the temporal RT2 contract (`linear_distance_fade.h:5`;
`linear_material.cpp:1212`). Its source-over law is
`Lnew = Q + (1-q)*decode(A)` (the owning
[fade note](linear-distance-fade.md), composition equation). A future exact
source attribution needs a matching `Snew = Qsun + (1-q)*Sbackground`, followed
by a new ratio against **Lnew**. Blending source f values cannot compute it;
the single existing depth also cannot represent separate shadow visibility of
transparent foreground and background. Extending that semantic/storage contract
is an alternative, not implied by proving glass's own S.

The current exchange path publishes only after `finish`, target exchange and
acknowledgement; the in-place path publishes its completed rectangle on successful
`finish` (`motion_output.cpp:3429,3450`). Only then may coverage/validity describe
the final owner. The proposed consumer runs after restored lazy bindings and all
these brackets, before AO/TAA, at `scene_end_hook` (`motion_output.cpp:1491`).
Never apply using an intermediate A/B/C, prepublication share or recovered-native
color with a previously valid share. First-version coverage exclusion avoids
changing this transaction or adding sun resources to the fade pool.

### RT2 capability boundary, ordinary TAA and native Windows

Keep the existing R32F path when the option is off or unavailable. Gate the
G32R32F enhancement independently at device/format boundaries: three MRTs,
`MRTINDEPENDENTBITDEPTHS`, render-target and point-sampling support through
`CheckDeviceFormat`, depth-stencil compatibility and an actual mixed-format
write/sample self-test beside the active RT0 and RGBA32F RT1. Cutout/masking needs
the documented MRT post-pixel-operation capabilities and per-format queries;
the existing HDR meter's isolated G32R32F allocation proves none of that MRT
combination. These are documented D3D9 contracts on both platforms
([MRT rules](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets),
[format query](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3d9-checkdeviceformat)).

Changing the allocation alone breaks shared consumers. TAA checks R32F explicitly
(`src/renderer/temporal_pass.cpp:269`) and copies current depth into R32F history
with StretchRect (`:310`); AO also rejects non-R32F input
(`src/renderer/ambient_occlusion_pass.cpp:291`). **Recommend a point-sampled `.r`
shader copy into the existing R32F history for enhanced RT2**, with format-aware
input validation. Keep ordinary history and the off-path copy unchanged. This is
a documented portable conversion, avoids requiring G32R32F→R32F StretchRect,
and reuses the existing R32F histories. Same-format G32R32F history is a valid
alternative but adds two enlarged histories. Driver-dependent conversion is not
a portable prerequisite
([StretchRect restrictions](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-stretchrect)).

Make allocation/variant selection transactional: unavailable G32R32F or failed
enhancement creation selects ordinary R32F and ordinary shader variants at a
safe frame boundary. No G32R32F-specific failure may permanently disable baseline
TAA. Reset/resize releases and rebuilds the selected resources; shared-state
restoration failure keeps the existing TAA safety response. Readbacks must use
the actual RT2 format/stride. Native Windows runtime remains unverified; future
implementation must record that gap in [platform-portability.md](platform-portability.md),
separately from documented-API source compatibility and cross-compilation.

### Cost, acceptance and remaining decisions

No per-draw shader transformation, allocations, constant upload or capability
queries. Reuse cached route state; validity adds bounded draw/frame bookkeeping.
The implemented extraction adds 32–45 weighted slots including copies, branch
handling and finite-domain checks (136–342 total). The earlier RE estimate of
2–12 dependency operations plus six reduction/output instructions omitted those
checks; §2's +5 estimate is superseded. GPU/register-pressure cost remains unmeasured.
RT2 adds 4 B/px (3.75 MiB at 1280×768); the recommended depth-history copy adds
one full-size draw in place of the existing copy, and the future consumer adds
a coverage read where needed. Measure these costs; no measured FPS claim exists.

Acceptance before release: all 108 source extractions and combined depth modes
must preserve color/alpha and isolate diffuse/gloss/fill/XT tails; mutation,
NaN/infinity, clipping, black/epsilon and output-order tests must refuse as above.
The owner's detached fixture must verify f and scalar **luminance** separately
from RGB hue error; opaque glass versus source-over glass; alpha-pass/reject
coverage; interleaved opaque/fade/emission with publication failures; and invalid
or absent M. Capability/allocation faults, option-off, resize and Reset must
retain ordinary TAA, with no cross-format StretchRect dependency. This note's
acceptance is factual/source/link review only; no build, Wine or game run occurred.

The conservative boundary is accepted for proving the lane, not as evidence that
shadows have useful gameplay coverage. Before cascades ship, measure live receiver
coverage and refusal reasons; zero useful coverage requires fixing publication
coverage, not claiming completion through fallback. Retain the existing user-run
shadow-footprint acceptance (§3) where applicable to the map consumer. A later blended
receiver contract, clipped/8-bit approximation and native runtime evidence remain
unresolved, rather than being advertised as supported by the extraction count.

## 9. Same-frame replay admission contract — ratified feasibility boundary (2026-09-15)

**Ratified by the orchestrator as the feasibility boundary, not permission to
enable live replay.** This addresses route B's first
Lock counter and one-cascade depth producer. It does not change the accepted
receiver lane. A quiet Lock trace is feasibility evidence, never permission to
remove `motion_live_replay_available=false` (`src/proxy/capture.cpp:137`).

### Selected minimum and evidence

Use the existing process-wide application-admission monitor plus frame-local
native geometry leases. Restrict the first producer to bounded, ordinary opaque
routed indexed draws with the already-supported **readable MANAGED, non-DYNAMIC**
VB/IB contracts, finite positions and exact index-range evidence. Exclude glass,
cutouts, fade, UP, instancing and mutable texture inputs from this prototype;
record their rejection counts. A partial diagnostic map is acceptable for replay
feasibility, not a complete shadow map eligible for scene-color application.

The portable qualifier already requires this buffer domain
(`src/ownership/portable_managed_upload.cpp:22`); write evidence accepts ordinary
flags 0/NOSYSLOCK and observes the mapping only before native Unlock
(`src/ownership/d3d9_ownership.cpp:902,929`). The lease validates owner/reset
generation, finite/index evidence and expected revisions, then supplies retained
native buffers (`:1599,1704`). Its native AddRefs preserve allocation lifetime,
not old contents. No extra lock/readback of application buffers is proposed.

### Counter first: observe attempts, completion and the draw-to-replay interval

Add optional VB/IB diagnostic bookends in `buffer_lock` **before native Lock**
(`d3d9_ownership.cpp:859`) and after result publication; pair them with Unlock
entry/completion (`:915`). Under the existing short registry synchronization,
maintain a saturating per-allocation attempt serial and in-flight call count,
plus successful pending-map state. Failed attempts remain visible; only native
successful writes advance the existing content revision. Keep the native call
outside the registry, and never hold a mutex across the application's mapping.
Current `record_buffer_event` advances pending/revision after native success
(`:835`), so it alone misses a Lock still executing at the boundary.

For the bounded candidate set record allocation identity/generation, byte range,
flags, thread, attempt serial and content revision at source capture and scene
end. Report aggregate ordinary/READONLY/DISCARD/NOOVERWRITE attempts, failures,
pending/in-flight maps, changed candidates and lease refusals; keep only bounded
failure witnesses. Include any Lock after successful source submission, even a
completed READONLY call, separately from actual content revision changes.
Do not allocate or log per Lock/draw, hash buffers, insert sentinel writes or
change returned pointers. A count of zero says only that the observed interval
was quiet. Coverage-unknown/native-escape vetoes must accompany the report.

### Source recording and exclusive scene-end replay

1. Preallocate a bounded draw-record array, cascade target/depth attachment,
   authored shader/declaration and retirement storage outside exclusivity.
   Acquire a native VB/IB lease while the source's actual application references
   are live; retain immutable submitted rows, declaration layout, stream/index
   ranges and cull state. Commit the record only after successful original draw.
   Reject a record if its Lock attempt serial/revision/pending state changed
   during capture/submission. Also bracket source state capture/submission with
   admission snapshots: require the sole root and unchanged `admitted_roots`
   counter, so another ordinary native caller cannot silently invalidate the
   source-state association. This is a source-record check, not replay exclusion.
2. The two actual boundary paths, `compositor_pre` (`capture.cpp:688`) and
   `scene_end_signal` (`:2213`), currently acquire the capture mutex without an
   application ticket. Establish an outer `ApplicationAdmissionAbi` **before**
   that mutex. Pass that specific boundary to the producer. After finishing lazy
   bindings/composition and before AO/TAA, try `ReplayAdmissionAbi` on the sole
   outer root; nested boundaries, other active roots, waiting callers or permanent
   vetoes refuse immediately. Do not wait for another application while holding
   the capture mutex. End exclusivity before ordinary scene-end consumers run.
3. Under successful promotion, revalidate the sealed records, exact owner/reset
   generation, pending/in-flight maps, expected revisions and synchronized
   scene/query/stateblock state. The monitor must exclude new native application
   dispatch through save, depth replay and restoration. Already-returned mappings
   cause immediate refusal; mapped writers are not stopped by the monitor.
   Invalid records can be omitted only while the output remains diagnostic with
   explicit partial-map status; state/generation/coverage uncertainty refuses the
   whole producer. No production shadow consumer may use that partial result.
4. Bind a mod-owned R32F color target and ordinary compatible depth-stencil,
   clear, and replay the admitted draws with the authored sun-depth pair. Capture
   fresh state and explicit RT/depth references; pin every original binding until
   restored. Use native entrypoints authorized for this exact segment, never a
   general TLS bypass of wrapper admission. No RESZ, sampled depth FOURCC,
   application shader compilation, resource allocation, Present/Reset, query
   wait, message pumping or diagnostic callback belongs inside this segment.
5. Restore fully before dropping the exclusive token. Then retire saved state,
   geometry and temporary references outside the ownership registry; publish
   diagnostics only after restoration. Reset attempt/loss/frame abort ends the
   frame reservation and invalidates its generation; no lease survives into the
   next frame. On restore failure use the existing state-loss safety response,
   never report a valid map. Successful native submission needs no GPU wait:
   later legal ordinary buffer Locks obey D3D resource-use synchronization.

The monitor core already atomically promotes only one root and makes new outer
entries wait without holding its mutex during native work
(`src/ownership/application_admission.cpp:34,68`). VB/IB Lock/Unlock wrappers
already enter it before native dispatch (`d3d9_forwarders_inc.h:1036,1093`), so
an in-flight Lock prevents promotion even before pending metadata is published.
This works only after the complete startup/callback/native-entry coverage in
[motion-replay-exclusion.md](motion-replay-exclusion.md) is established. The
loader still disables execution observation (`src/proxy/loader.cpp:197`).

### DISCARD/NOOVERWRITE, alternatives and Windows

Any writable Lock between source capture and replay invalidates the first
prototype's lease, irrespective of range; READONLY still refuses while pending.
DISCARD discards the entire VB/IB contents, so a retained COM pointer cannot
recover a previous backing generation. NOOVERWRITE is an application promise
about already-used ranges, not evidence that a later replay has retained the
original bytes. Keep both outside the first managed-buffer producer; do not infer
safety from equal pointers, static usage or a nonoverlapping offset alone.
These restrictions follow the documented
[Lock contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dvertexbuffer9-lock)
and [D3DLOCK semantics](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dlock).

A later immutable mod-owned geometry snapshot could admit rewritten/dynamic
inputs, but requires a proved copy boundary, payload storage, upload cost and
range/lifetime contract; it still does not exclude concurrent device-state
mutators. Revision-check-then-draw, the capture mutex alone and D3DCREATE_MULTITHREADED
do not make the multi-call replay transaction atomic. Wine-private locks/layouts
are excluded. Documented COM, D3D9 and portable C++ monitor primitives give the
same enhancement path on native Windows; runtime behavior is not verified there.

### Required coverage evidence before promotion can be enabled

Coverage must be established by entry inventory and observed ownership, not a
manually asserted `coverage_complete` flag. Inventory every generated normal-D3D9
method and handwritten dispatch: factories/device creation, QueryInterface and
returned interfaces, AddRef/Release and output adoption, buffers/textures/surfaces,
ProcessVertices, query Issue/GetData, stateblock Apply/Capture, all device setters,
scene transitions and Reset. Their root must precede capture/registry acquisition
and native dispatch and remain active through native result/metadata publication.
Exercise representative generic forwarders as well as hand-coded hooks with a
native-entry barrier. Entry instrumentation must start before the first factory
or application graphics object is returned. A successful unknown QI, Ex/native
fallback, untracked late adoption, shared/external resource or raw-native pointer
escape permanently vetoes the process before exposure. Prove the reviewed
game/D3DX pointer routes actually receive owned wrappers; an import list or absence
of escape events in a short trace is insufficient. Explicitly inventory mod-owned
raw-native helpers and grant authority only to their bounded counted operations.

All seven resource `SetPrivateData(D3DSPD_IUNKNOWN)` routes must latch a permanent
veto **before** native AddRef, including failed registration and a second device;
GUID spoofing cannot authorize the mod's private sidecar path. No FreePrivateData,
Reset or quiet interval clears it. Install/validate outer window-chain admission
before returning the device; game-WndProc-only wrapping does not cover a runtime
hook ahead of it. Same-thread callback reentry during replay is an invariant
failure, not permission to forward or synthesize an HRESULT. Qualified replay
operations must exclude its known sources. Defer callback-capable retirement and
diagnostics; child final Release ends its child ticket after native retirement
but before parent application Release reenters capture. Preserve incoming and
native-outgoing x87/MXCSR/LastError at each boundary. Never wait for application
roots, window threads or GPU completion under capture/registry/exclusion. A Reset
already active prevents promotion; one arriving later cannot dispatch until full
restoration and token release. These are independent deep-review obligations,
including failure/partial-save paths, before changing any live replay gate.

### Cost, acceptance and blocking integration facts

Counters add bounded metadata work at Lock/Unlock; source recording adds lease
work and two short admission snapshots per selected draw; promotion occurs once
per candidate scene. Measure those plus admission on ordinary calls, source
capture time, replay submission/restoration time and refusal rates. Reuse the
existing lease ceilings (4096/frame, 8192/process, 512 MiB charged allocations),
with a smaller prototype list; no new per-draw allocation or payload copies.
Report total routed draws, candidates, leased draws, admitted/replayed draws and
rejection reasons together. Zero admissible live draws does **not** demonstrate
replay feasibility, even when serialized synthetic geometry passes.

Before live activation, deterministic fixtures must stop Lock before native
return, hold a mapping across the boundary, race a new Lock/stateblock/query/Reset
after promotion, and exercise failed Unlock, DISCARD/NOOVERWRITE refusal, wrapper
recreation, final Release and partial restoration. Verify no early native
dispatch, no stuck ticket/lease, preserved HRESULT/output/CPU/LastError, original
state and analytic single-cascade depth. Tests run only when separately assigned
to the parent's Wine queue; this design task ran none.

**Blocking unknowns:** callback-free segment authority, outer window admission
and raw-native helper coverage are not implemented by this proposal. The existing
[game/D3DX audit](../reverse-engineering/game-callback-registration.md) follows
mesh allocation through device slots 26/27 at D3DX `0x591cb0/0x591a9d`, supporting
wrapped VB/IB delivery; it also identifies WndProc `0x4d3620` callback branches.
It does not certify all dynamic escapes or window-chain entry ordering. Finish
that bounded entry/fixture work before live promotion; a clean counter run cannot
substitute. Parent must ratify the managed-only diagnostic boundary, source-record
check and staged activation; useful admitted geometry and native Windows runtime
remain separate acceptance gaps tracked by [platform portability](platform-portability.md).

## 10. Diagnostic lane source checkpoint (2026-09-15)

`--sun-shadow-lane` / `X3M_SUN_SHADOW_LANE=1` is explicit and default off,
requiring motion output, TAA, HDR and linear materials. It applies no shadows
and records no replay geometry. Its off path retains ordinary shader objects,
R32F storage and the ordinary depth-history copy.

### Storage, cached variants and publication

The enhancement has its own documented D3D9 queries and self-test in
`src/proxy/sun_share_lane_inc.h`. It requires three MRTs, independent bit depths
and masks, post-pixel operations, per-format target/sampling/post-pixel queries
and depth-stencil matching. The self-test uses FP16 RT0, RGBA32F RT1 and G32R32F
RT2; reads ordinary and exact GE/ref1/RGB-mask cutout writes and rejection,
hardware depth rejection, independent RT2 masking, final invalid `.g`, two-lane
point sampling and point-copy `.r` into R32F. It captures a full state block plus
explicit targets/depth/viewport/scissor and restores after any mutation. A
restore failure uses the existing shared-state quarantine/TAA safety response.
There is no Wine-private prerequisite.

When a depth attachment exists at attach/Reset, qualify that format. Without
one, test the bounded D16/D24X8/D24S8 set and cache successes; a scene latch must
match an actually qualified format. No test runs inside a game scene. Failed
queries/tests select ordinary R32F. RT1/RT2 allocation is a transaction: failed
enhanced allocation discards its incomplete objects and retries R32F at the
latch, retaining ordinary shaders. Reset/resize release owned targets and
rebuild the selected storage; Reset repeats format qualification.

Each depth-producing PS has a separate cached invalid-share variant. Its sole
final `MOV oC2.g = -1` uses a collision-checked shader-local c221 DEF, adds one
slot and no temporary or host upload, and preserves all original instructions.
The converted variant instead selects the explicit extraction API. Ordinary
opaque/qualified cutout depth writers, including opaque glass, can publish a
receiver. Fused fade keeps its existing RT2 mask; detached/native blend draws
never become receivers. Empty clear is `(-1,0)`; invalid share is `-1`, distinct
from proved zero. Failed shader creation/bind immediately vetoes the shadow
frame and schedules R32F for the next frame boundary. The failed frame has **no
physical `.g` validity guarantee**; it is unavailable, even if bytes happen to
look nonnegative. This fallback never changes RT2 format mid-frame or disables
ordinary TAA solely because the lane failed.

`SunShareFrame` is a diagnostic snapshot at the pre-AO/TAA scene-end boundary,
not a later lease on the owning color. Successful in-place completion or
completed exchange/acknowledgement establishes same-frame conservative M.
Every pixel covered by M is excluded. A successful later owning scene color
writer without a proved depth/share update or completed conservative coverage
vetoes the frame after an eligible receiver. Unknown state, failed publication
and unavailable required M refuse the shadow snapshot. A plain depth writer
with explicit invalid `.g` safely replaces an earlier receiver. The lane veto
has no independent TAA-invalidation call.

**Unroutable depth writer (run 174).** A scene draw refused at gate 3 as
`unregistered` (a program outside the SM3 profile registry, e.g. the SM2
`adeffects` advertising signs of Argon Prime) no longer vetoes the frame when
it is an actual depth writer after a receiver. `sun_stamp_draw`
(`src/proxy/sun_share_lane_inc.h`) re-issues it once with the application's
own VS, geometry and raster state, `ZFUNC EQUAL`, no depth write, RT0/RT1
masked off and RT2 masked to `.g`, through a constant ps_2_0/ps_3_0 program
chosen by the bound programs' shader-model family: the pixels the draw won get
the explicit invalid share -1 and are excluded by the apply pass; every other
pixel keeps its lane bytes. Only that draw loses shadows. A stamp that cannot
run (stencil enabled, user-memory geometry, unknown program version, SM3 beside
a null stage, an original PS that writes oDepth) or fails keeps the veto; `pair`, gate-4 and xt-repair refusals
keep it too. Ledger: `docs/verification/directional-shadows.md`, "Run 174".

TAA recognizes G32R32F input and reuses its existing authored identity-copy PS
with point sampling, writing only `.r` into the existing R32F history. It never
requests G32R32F→R32F StretchRect. Copy failure restores state and leaves history
unpublished; depth-copy timings exclude those ticks from resolve-draw timing.
AO accepts either input format and samples `.r`. Capture/fixture readbacks and
`analyze_motion_readback.py` use actual 4/8-byte depth stride. Capture frames
with required valid M also write `sun_coverage_*.rgba16f`.
`tools/analysis/analyze_sun_share_lane.py` requires the same-frame publication,
G32 readback and required M before reporting actual eligible/zero-sun/excluded
pixels. Unavailable frames always report zero eligible pixels.

### Evidence and outstanding execution

Host checks cover 110 ordinary motion depth PS, 108 linear PS and four XT
repaired ordinary PS accepting invalid-share augmentation with original words
verbatim. The separate extraction suite retains 432 variants / 152 sun MADs,
137–349 weighted slots and 32–45 added slots on the repaired base. This lane checkpoint is now based on sanitizer repair
`5b3b5c3`; the extraction suite additionally requires zero inherited constant
read-port violations in the combined shaders. The repair's existing qualification
remains separate from the pending lane GPU/native-runtime evidence.
The synthetic C++ checks cover sentinel export, c221/relative-use/malformed
refusal, allocation rollback, frame ordering, invalid-depth updates, coverage
loss and frame veto; Python checks cover readback stride and fail-closed pixel
counts. After the zero-sun fix and sanitizer integration, the affected lane,
extraction and fill suites passed 19 host tests in 30.33 s; the 1,388-output
zero-fill digest matches the sanitizer repair. MinGW fixture/transformer syntax
checks use the required SSE2/four-byte incoming-stack flags.

Owner acceptance commands (not executed under Wine by this agent):

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_sun_share_lane verification.analysis.test_motion_readback verification.analysis.test_linear_sun_share verification.analysis.test_linear_material_report
sh verification/probe/build_linear_material.sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_linear_material.py --exe verification/probe/build/linear_material_fixture.exe --sun-share --raw-dir /tmp/x3-sun-share-material-gpu
sh verification/probe/build_temporal_pass.sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_share_temporal.py
python3 tools/analysis/analyze_sun_share_lane.py PATH_TO_CAPTURE_LOG --readback-dir PATH_TO_READBACKS
```

The detached `--sun-share` mode prepares 216 cases over all 108 PS originals,
normal/zero-sun twins, exact color/alpha/motion/depth comparisons, positive valid
pixel requirements and an independent paired GPU D0-subtraction oracle with
0.01 share tolerance for FP16 encode/decode. Before every material draw, an
authored constant PS through the shared quad ABI initializes RT2 to `(-1,0)`;
color and motion remain zero-cleared. A no-material-draw readback must contain
only that sentinel and report zero drawn/valid/positive/zero-sun samples. Each
case must then contain valid drawn samples; every zero-sun case requires
`zero == valid > 0` and no positive share. Report validation rejects missing or
duplicate rows, inconsistent count totals, clear pixels counted as zero-sun,
and zero-sun cases without drawn samples. It is not live receiver acceptance.
The temporal `sun-lane-only` mode prepares eight exact ordinary/enhanced history
twins, four first-copy failures/recoveries, two sizes across two Reset generations,
hostile-state comparisons, missing-copy refusal and an explicit cross-format
StretchRect rejection. Actual GPU results are pending. Runtime fixture seams
`X3M_FIXTURE_SUN_LANE_FAULT=caps|selftest|cutout_drop|alpha_mask|allocation|late_shader|bind` and status
keys 90–98 expose qualification, active format, publication and failure counts.
Their live route ordering, late-failure R32 fallback and positive gameplay pixel
coverage still require owner-run fixtures/captures; no game was launched here.

Cost pass: no new per-draw allocation, transformation, constant upload,
capability query or lock; cached pointer selection and bounded integer frame
bookkeeping only. The full corpus can add 222 cached PS objects plus one clear
PS. Steady storage adds 4 B/pixel (3.75 MiB at 1280×768), with unchanged R32F
histories and one full-size copy draw replacing StretchRect. Qualification
allocates five 4×4 textures, five readback surfaces, one depth surface and five
PS objects per tested depth format, all temporary, plus the retained clear PS.
A synthetic host bookkeeping loop measured about 1 ns/iteration including loop
and pseudorandom-input overhead; this is neither draw cost nor game FPS.
Startup qualification time, GPU register pressure/bandwidth and native Windows
execution remain unmeasured. No production DLL build, commit or install occurred.


### Review correction: nonvacuous cutout and actual MotionOutput fixture

The qualification positive cutout now writes RGB `(.5,.75,.25)`, motion
`(7,8,9,-1)`, and depth/share `(.125,.75)` over different opaque values.
Source alpha `.5` passes GE/ref1 while RGB-only writes must retain destination
alpha `1`. A third, distinct alpha-zero payload must leave those passing outputs
unchanged. Raster depth progresses `.5 → .25`; the subsequent `.375` draw must
fail, proving that the passing cutout updated hardware depth as well. The 18
readback comparisons remain; this correction adds one temporary three-slot
constant PS, no draw, allocation or query to the game hot path. Fixture-only
`cutout_drop` skips the passing draw and `alpha_mask` enables the forbidden alpha
write. Both must fail at `stage=cutout_pass` with successful restoration, then
run ordinary R32F TAA. Host payload execution rejects both broken outcomes.

The actual `motion_output_fixture.exe ... sunlane` mode uses the original
Argon material pair, real selector, native seam, lane qualification and cached
shader routing. Its 3,600 interior pixels are selected from known triangle
coverage, independently of observed depth/share. They must have exact `.r=.5`
and finite valid `.g`, with every pixel positive or (the explicit D0-zero frame)
exactly zero. Scene-end publication is asserted separately. Late shader
creation/bind failure follows an already observed positive receiver, leaves
G32 storage in that frame, vetoes publication, and chooses R32 on the next
frame. A separate untracked color writer must actually replace FP16 color while
retaining stale positive lane bytes, veto publication and keep TAA. Reset then
requalifies. Every frame also compares the complete FP16 TAA output against the
existing independent R32 reference, with four history frames per six-frame
process; no status-only TAA acceptance. Caller-state comparisons and final
zero device/factory references use the existing fixture checks.

`run_sun_share_live.py` consumes explicit prebuilt EXE/seam inputs and preserves
the detached material producer executable, inputs and result. Its eight scoped
cases prepare 48 frame/output comparisons, 32 history frames, eight Resets,
18 positive receiver-frame witnesses and four exact-zero receiver frames.
The cases are `positive`, `caps`, `cutout_drop`, `alpha_mask`, `allocation`,
`late_shader`, `bind`, and `untracked`; `--case` can select a bounded rerun.
The GPU/native behavior and measured startup/game cost still need owner
qualification; these are acceptance requirements, not claimed results.

```sh
# Owner only: build the seam against matching reviewed production objects.
sh verification/probe/build_motion_output.sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_share_live.py --fixture verification/probe/build/motion_output_fixture.exe --dll verification/probe/build/motion-output-seam/d3d9.dll
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_sun_share_lane
```

Review-fix host acceptance: all eight `test_sun_share_lane` tests passed in
5.31 s, including the existing 222-program invalid-export corpus, 24 C++ checks,
new authored-payload negative controls and live-report corruption witnesses.
Production MotionOutput, fixture-enabled MotionOutput and the actual live
fixture passed MinGW `-fsyntax-only` with `-Wall -Wextra -Werror`, SSE2 and the
required four-byte incoming-stack flags. No Wine or DLL build ran in this task.

### Review correction: actual composition-M coverage in the live lane fixture

Three additional `run_sun_share_live.py --case` selections use the real
unfaded additive emission pair `vs_089091aab2d5eb13` /
`ps_8559522220507d5e`, original shader registration, composition preparation,
owning-target exchange/acknowledgement and GPU coverage reads:

- `composition`: frame 1 excludes 768 covered pixels; frame 2 unions two
  rectangles to 1,536; frame 3 interleaves emission → full opaque receiver →
  emission and preserves both conservative exclusion rectangles. Frame 4
  follows Reset with zero M; frame 5 covers only the opposite 768-pixel region.
  All healthy M reads check the complete RGB mask against the authored scissor
  rectangles, with positive eligible receiver pixels outside their union.
- `composition_missing`: injects the actual pass's FrameClear operation failure
  before frame 2's latch, then submits a real additive source. Old physical M
  cannot become same-frame evidence: the frame reports zero eligible/excluded
  pixels and unavailable publication. Subsequent frames recover through the
  ordinary per-frame M clear.
- `composition_failed`: frame 2 first completes one real composition/exchange,
  then fails the second source's completion Restore, takes the existing native
  recovery/exchange path, and invalidates M. It proves that earlier successful
  composition in the same frame cannot authorize later incomplete coverage.

Every composition source must visibly increase owning FP16 RGB while leaving
RT2 receiver bytes unchanged. The interleaved opaque draw must restore original
material RGB before the second exchange. Actual linear/exchange/incomplete
counts accompany GPU M and eligible-pixel checks; report validation rejects
lost exclusions, missing exchange/interleave evidence and stale validity.
The missing/failed-M cases intentionally follow the **existing composition**
TAA contract: current-only resolve without history seeding, followed by a fresh
seed next frame. They require two history frames per process; healthy
composition requires four. This does not relax the lane-only fault requirement
that ordinary TAA history survives. Every process retains six full FP16 output
comparisons against the R32 reference with the matching supplemental-mask
policy. The full 11-case mode therefore requires 66 output comparisons,
40 history frames and 11 Resets. The new GPU composition cases remain
owner-qualified work, not claimed execution here.

The owner-built positive lane run completed 43,269 checks, 18 restorations and
six frames. Its original validator rejected nested `detail=stage=history_r`
parsing; the corrected leaf-field parser and nested-detail host regression now
accept the retained output, including all six exact TAA images, five positive
receiver frames and one zero-sun frame. Revalidation read existing artifacts
only. The affected host report test passed (0.58 s), and the modified live
fixture passed MinGW syntax checking with the required strict/SSE2/stack flags.
No production files, qualified material/temporal inputs, executables or results
were changed by this composition extension.

```sh
# Owner only; consumes the already built seam and fixture.
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_share_live.py --fixture verification/probe/build/motion_output_fixture.exe --dll verification/probe/build/motion-output-seam/d3d9.dll --case composition --case composition_missing --case composition_failed
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_sun_share_lane.SunShareLane.test_live_evidence_rejects_missing_draws_faults_and_taa
```

### GPU witness correction: unique motion keys and same-size fallback history

The first late-shader run reached frame 2 with valid receiver writes, lane
`available=0 failed=1`, and a successful TAA resolve, but failed the fixture's
history assertion. The fixture submitted the same object/motion key twice and
incorrectly marked both draws matched. `MotionRowHistory::lookup_and_record`
authorizes a previous key only once; the second draw misses, yielding a 1/2
missing fraction above the existing .25 cut threshold. The reference had been
fed a false no-cut verdict. Late-fault and interleaved-composition scripts now
warm two distinct object scopes every frame and use each exactly once. Added
`SUN_HISTORY` rows expose expected, reference and actual history separately.
No duplicate-key or camera-cut rule was weakened.

The first composition run changed native RGB but performed no owning exchange:
the fixture used non-indexed DrawPrimitive while the additive-emission gate
requires indexed DIP. Its source now uses a retained managed 16-bit index
buffer and actual DrawIndexedPrimitive, matching the existing emission fixture.
The index binding is explicitly released before fixture teardown. Neither
correction changes ordinary material/temporal shaders or qualified producer
inputs. Passed positive and early-capability output remains retained.

Inspection also found a real next-frame fallback issue: target allocation
unconditionally cleared motion-row correspondence, which would force a cut
when failed G32 storage changed to ordinary R32. The route now keeps the
unavailable old storage until the next scene latch can compare metadata, then
preserves row history **only after successful replacement**, when the previous
storage was an active failed lane with identical dimensions and the exact same
MotionOutput device/reset generation. Target retirement clears that generation;
Reset/loss/ordinary resize and allocation failures retain existing invalidation.
The ordinary selector still begins the row-history frame with its own exact
generation/size check. No target format changes within a scene.

This preservation is compatible with pixel history: the existing TemporalPass
history is already R32F containing the previous enhanced input's point-copied
`.r` (qualified by the temporal fixture). The new current RT1/RT2 receive the
ordinary sentinel fill before routing. Retained rows then supply the same
previous transforms against those compatible histories. No old current depth
pixels are copied or assumed valid. Allocation count and draw cost are
unchanged; the new metadata comparison runs only on target replacement, with
one retained generation word per device and no per-draw work.

Focused host acceptance executes the **actual extracted** `ensure_target` and
`release_target` methods with a documented-API failure double and the real
MotionRowHistory: 49 checks, 27 create calls, zero surviving mock surfaces.
It covers successful preservation, dimension/generation changes, prior target
loss, ordinary resize, failed R32 allocation, failed surface extraction and
consumed duplicate keys. This proves CPU decisions, not GPU lifetime or content.
The two affected host tests passed in 2.38 s; production/seam/live fixture
MinGW syntax passed. Parent-owned GPU reruns and deep review remain required
for this production change; no Wine or build was executed by this agent.

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_sun_share_lane.SunShareLane.test_actual_target_replacement_preserves_only_compatible_rows verification.analysis.test_sun_share_lane.SunShareLane.test_live_evidence_rejects_missing_draws_faults_and_taa
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_share_live.py --fixture verification/probe/build/motion_output_fixture.exe --dll verification/probe/build/motion-output-seam/d3d9.dll --case late_shader --case bind --case composition --case composition_missing --case composition_failed
```

The owner rerun subsequently proved late-failure frame 2 and ordinary-R32
frame 3 both had `SUN_HISTORY expected=1 reference=1 actual=1`; all four
completed FP16 images (frames 0–3) were revalidated byte-exact from retained
files. It then reached a distinct Reset refusal: `qualified=0 reason=shader_cache`.
The orchestrator ratified this fail-closed distinction for the bounded lane:
**Reset may recover a transient bind failure with a complete cache; it cannot
recover a missing shader artifact.** The late-creation fixture now requires
R32/unavailable through frames 4–5 and successful ordinary TAA throughout.
The bind-only fixture still requires G32 requalification after Reset. Host
negative evidence rejects either falsely recovered creation failure or falsely
unrecoverable bind failure, and requires the exact persistent-cache reason.
That focused validator test passed in 0.56 s; fixture syntax passed again.

The shader registry owns generated PS references, not the original application
objects. It retains raw original addresses as lookup keys without dereferencing
them during qualification; no new dead-original access or recreation was added.
`release_resources` releases/nulls every generated sun PS; final device Release
erases its capture context, destroys MotionOutput and frees both registry maps.
A missing artifact adds no surviving PS reference and persists only with its
device's existing registry metadata. There is no fixed entry-count cap claimed:
registry size follows registered shader objects/addresses during that device
lifetime, as before this lane. An immutable-byte recovery design may improve
this separately; Reset does not invent replacement shader source.

Cascade layout (2026-09-17): see [shadow-cascades.md](shadow-cascades.md), ratified; the one-cascade replay and apply are in game since run 37 B.
