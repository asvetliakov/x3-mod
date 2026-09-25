# Legacy sun application: shadowing the original hull programs

Design note, 2026-09-16, pending orchestrator ratification. Question: how a per-pixel
sun shadow factor `f ∈ [0,1]` (later the one-cascade depth replay, `--shadow-replay-depth`,
today without a consumer) is applied to the output of the 108 original, unconverted pixel
programs the user plays with (no `--linear-materials`), in their native code-value space.
The existing sun-share lane and its linear law
([directional-shadows.md](directional-shadows.md) §2, §8) were designed for the converted
materials; this note gives original shading its own law and route. Inputs:
[sun-share-material-contract.md](../reverse-engineering/sun-share-material-contract.md),
[directional-shadows ledger](../verification/directional-shadows.md),
[shadow-replay-gates.md](shadow-replay-gates.md),
[original-shading-critique.md](original-shading-critique.md) §1a and its `--original-fill`
implementation (`d864246`), `src/renderer/linear_material.cpp` (`original_fill_transform`,
`linear_material_fill_sum`), `src/renderer/ambient_occlusion_pass.cpp` (the multiply-blend
apply precedent), run81 (`/tmp/x3-bottleX3-run81`). Host inspection of the 108 local
originals only; no Wine, build or game. Implemented so far: the share producer of 1,
`linear_material_original_sun_share_pixel_variant` in `src/renderer/linear_material.{h,cpp}`
with its host and detached GPU fixtures (3.1, 3.2; ledger
[directional-shadows.md](../verification/directional-shadows.md) "Original-program share
producer"), the apply pass of 2 (`src/renderer/sun_shadow_apply_pass.{h,cpp}`, 3.3), and the
wiring of 2 and 4: the lane latch without linear materials (capture gate, `qualify_sun_lane`,
gate 4's tested-opaque arm), the original share variant as the routed draw's bind pair,
the cutout pairs through the tested-opaque arm, the scene-end order replay → rows → apply →
AO → resolve with Reset plumbing, the F8 map dump, `--sun-shadow-apply`, and the live
`original_lane` / `shadow_apply` cases (3.4; ledger "Lane latch and apply wiring
(2026-09-17)"). Not yet: a user run with the lane and the quad on original shading (4.3).

## Decision

**Law:** the shadowed colour is the original program evaluated with its sun colour constant
scaled by `f` at every sun seed, `C_f = C − (1−f)·S_c`, where `S_c` is the sun's code-value
contribution to `oC0`; on all 108 programs this is exact (the sun path is MOV/ADD/MUL/MAD
with no saturate), `f = 1` is the native output and `f = 0` is the native night side.
**Route:** the lane, not injection: each original program writes the code-value share
`s = Y(S_c)/Y(C)` to `oC2.g` (the original-shading twin of the converted share producer),
and one scene-end quad multiplies RT0 by `1 − (1−f)·s` with `f` from the same frame's replay
map, before AO and the TAA resolve, with **no** `1/2.2` exponent because share and target are
both code values. **Before any shadow:** the lane must first become available on original
shading (run81: 0 of 2,123 frames, all vetoed by the two cutout pairs), which needs the
original share producer, a lane latch without the linear-material prerequisite and the
cutout pairs admitted through the tested-opaque arm.

## 1. The law in code-value space

### Facts about the 108 originals (host inspection, this session)

- Version token `0xffff0300` on all 108: every hull, asteroid, palette, glass and XT pixel
  program is ps_3_0. The ps_1_x (168 pairings) and ps_2_x (466 pairings) programs of the
  archive are effects, GUI, bloom and unrouted SM2 material variants; none is routed, none
  writes RT2, none is a receiver. The SM1 limitation in the brief therefore never reaches a
  hull: the question "can a ps_1_x program sample a mask" does not arise for the 108.
- 152 sun seeds, all `mad dst, rS.<w|z|y>, cSun, addend` (one scalar lobe times the sun
  colour constant plus an addend): `c5` ×74, `c2` ×38, `c1` ×12, `c6` ×28 (XT). 64 programs
  have one seed (combined diffuse+gloss lobe), 44 have two (diffuse and gloss carriers).
- Zero `_sat` result modifiers on any instruction of the sun-to-`oC0` path in any program;
  the final RGB instruction is `_pp` on all 108. The path is MOV/ADD/MUL/MAD only (contract).
- The two cutout programs `5e0a10fe752b6140` and `63f96eba9eea7880` are among the 108.

### Definition

Let `C` be the program's final `oC0.rgb` (gamma-encoded code values) and define `C_f` as
the same program with `cSun` replaced by `f·cSun` at every seed. Because every operation
from a seed to `oC0` is affine in the seed's product and nothing saturates, the sun's
contribution is a separable term `S_c` (the parallel carrier of the extraction contract,
evaluated on the *original* operands instead of the converted ones) and

    C_f = C − (1 − f)·S_c            (exact up to `_pp` rounding; f=1 ⇒ C, f=0 ⇒ C − S_c)

`f = 0` leaves exactly what the native program renders on a face the sun does not reach:
vertex point/emissive colour, D1 and its gloss (seeded from `Color1`, never scaled), the
cube reflection, lightmap/emissive, XT occlusion RGB and lightmap, and the `--original-fill`
term (`K·decode(C0)` is added at the lobe-sum site and does not pass through a seed). That
is the user's accepted floor at Auto +1.3 (critique §1), so a full shadow can never be
darker than a native night side.

**Fill interaction.** With `--original-fill K > 0` the lobe sum passes through
`encode(decode(sum) + K·decode(C0))`, which is not affine in the seed. `C_f` is still
defined by the seed scaling; its endpoints are `sum_f(1)` and `sum_f(0) = fill(sum − S_sum)`,
so the exact `S_c` for the lane is the carrier `A_c·(fill(sum) − fill(sum − S_sum))` through
the linear tail: a second evaluation of the 14-instruction fill block on the sun-free sum
(+32 weighted slots when `K > 0`, nothing at `K = 0`). Intermediate `f` interpolate
linearly between the two endpoints, which is what a PCF penumbra means anyway.

### Instruction shape

*Seed scaling (the reference form, used by the oracle and by the injection alternative):*
one `mul r13.xyz, cSun, rF.x` per program before the first seed and each seed MAD reads
`r13` instead of `cSun` — **one instruction per program**, regardless of seed count, one
constant read per instruction. `x·1.0` is exact, so `f = 1` is value-identical.

*Share producer (the lane form, recommended):* the ordinary motion/depth variant of the
original (the program the routed draw already binds, as `--original-fill` extends it) plus:

| Piece | Instructions | Where |
|---|---|---|
| Seed | `mul r16+k.xyz, rS.<w>, cSun` | immediately before each seed MAD (1–2 per program) |
| Propagation | one MOV/ADD/MUL/MAD per sun-dependent RGB operation, original operands, before the original destination write | 2–12 per program (contract counts; both branch arms in XT) |
| Fill twin | second fill block on `sum − S_sum`, carrier difference | only with `K > 0`: +14 |
| Redirect | final RGB instruction writes `r23` (keeps `_pp`), then `mov oC0, r23` | +1 |
| Reduction | `dp3` ×2 (`c221.xyz` luma), `max` ε, `rcp`, `mul_sat`, domain guards, `mov oC2.g` | 21 + 1 (the converted helper's count) |

Registers `r16–r22` shadow the originals' `r0–r6` (`r7` for terraformer), `r23` the total;
`c221` holds luma weights and `2^-20`, collision-checked like `c215`. Originals' motion
variants are ≤ 151 weighted slots and the converted producer adds 32–45, so the largest
original share variant is ≈ 200 of 512 (≈ 230 with the fill twin): a static count, not a
GPU measurement. Per family: hull 66, asteroid 4, palette 20, glass 4 (S excludes the
`cube_coefficient·mask·fresnel·cube` term), XT 14 (S includes `pow(occlusion.a, strength)`,
both palette branches, occlusion RGB and lightmap outside S). Lane off ⇒ byte-identical to
the motion variant, as `--original-fill 0`.

*Apply:* `C' = C·(1 − (1−f)·s)`. With `s = Y(S_c)/Y(C)` this equals `C − (1−f)·S_c` in
luminance exactly and per channel only where `S_c ∥ C`; the residual is a hue error where
cube, point or emissive terms differ in hue from the sunlit term — the same scalar
approximation the converted lane ratified (§8), shown by the debug view. Unlike the
converted lane there is **no `1/2.2` power**: the share was reduced from code values and RT0
holds code values, so the multiply is the identity `C − (1−f)·S_c` directly. One authored
apply program serves both lanes with the exponent as a per-frame constant (1 for original
shading, `1/2.2` for converted), or two `_inc.h` programs; nothing per pixel changes.

## 2. Where `f` comes from: the lane and one scene-end quad

**Placement.** `scene_end_hook` (`src/proxy/motion_output.cpp`), after the replay
transaction has produced this frame's map and before the AO chain and the resolve — the AO
pattern: one quad, `SRCBLEND = ZERO, DESTBLEND = SRCCOLOR` into the owning FP16 target
(`ambient_occlusion_pass.cpp:242–243`; the target format is qualified with
`D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING`, `:170`), no intermediate target. Same-frame map
and same-frame lane: no lag.

**Per pixel** (all inputs on the jittered raster): read RT2 `A32B32G32R32F` (`.r = z/w`,
`.g = s`, `.b = .a = the interpolated clip w`, written by the depth fragment's `mov oC2.zw`
before the share's `.g`; `docs/architecture/shadow-receiver-depth.md`, the only RT2 contract
of the lane since 2026-09-18 — the `G32R32F` z/w law and its option are gone, an R32F or
G32R32F RT2 skips the quad with `format`); skip when `s ≤ 0` or the sentinel (`.r = −1`);
the view depth is `.b` itself (`terms.x/.y = m22/m32` stay uploaded, unread);
NDC from the quad UV minus the frame's jitter; view position
`(x·w/m00, y·w/m11, w)`; `S = SunProj₀·SunView·V⁻¹` of the replay's own frame (the projection
`shadow_replay_projection.h` already builds on the CPU), 3 `dp4`; texel-snapped map UV;
receiver-plane bias `dsx/dsy` of sun depth plus a constant in normalized sun depth; 3×3
manual PCF, kernel rotated by `jitter_index`; output `1 − (1−f)·s`. ≈ 50 slots (§4 of the
architecture note: "A's quad with a 3×3 manual PCF"), two texture sources, once per screen
pixel, no overdraw.

**Cost per frame.** GPU: the producer's +32–45 slots per routed hull fragment (static; the
detached fixture measures it), RT2 `A32B32G32R32F` +12 B/px over the lane-off R32F
(+11.25 MiB at 768p), the apply quad
bounded above by the whole AO chain's measured 0.51–0.78 ms, the map 42.3 µs median at 8
replayed draws (run81, 2,106 of 2,123 frames replayed; 783 µs on the first frame). CPU: the
lane's existing bookkeeping per draw, ≈ 15 device calls and one stateblock per frame for the
quad. The acceptance number is the run's `frame_end` median delta with the apply on versus
off in the same sector.

**TAA.** The quad runs before the resolve, so the resolve sees shadowed current colour and
the history accumulates shadowed output; RT2 and RT0 share one jittered raster, the map is
rendered from pre-jitter rows and texel-snapped in sun space (stable), and the rotated PCF
kernel dithers the penumbra through the resolve as GTAO's noise does. A shadow edge is a
function of the depth surface and moves with the object: current-frame content, no reactive
mask change. There is no separate mask texture to jitter or to keep.

**Reset.** The apply pass owns only its two programs and a stateblock:
`before_reset` releases the block, `after_reset` clears `reset_pending_`, exactly
`ShadowReplayPass`/`AmbientOcclusionPass`. The map is rebuilt by the replay pass, RT2 by the
route. The quad runs only when, in the same frame, `sun_shadow_lane_frame available=1`,
`shadow_replay_depth replayed>0`, the owner is valid and HDR is active; any missing input
skips the quad, leaving the frame byte-identical. `sun_shadow_apply_frame … skip_reason=`
names the miss: `lane` (not published available), `replay` (no map / rows of this frame),
`owner`, `depth` (RT2 absent), `recording`, `queries`, `camera`, `target` (RT0 not the FP16
target), `attach`, `reset_pending`, `depth_container`, `bias` (the world-unit bias does not
resolve for the cascade), then the pass's own `input`, `params`, `format`, `device`, `detached`,
and `failed` (a device call failed; restored); `none` when applied.

**Run106 (first apply in game) finding.** The quad, the rows and the map agree to the
map's quantization and the HDR carries the twin's factor
([directional-shadows.md](../verification/directional-shadows.md), "Run 36 session B"); what
the design produces on the own ship is a short shadow behind each low protrusion, because
the visible receivers are the top hull, the sun in the run's views sat 31° off the view
axis ahead of the camera at 27° elevation, and a hull hump a few units tall casts a shadow
a few units long. The pass is not to be judged on a backlit view; the acceptance capture
needs the ship side-lit (sun 60–120° off the view axis), where the wing and fuselage
shadows span tens of units. The quad's NDC → view law now subtracts the route's raster
jitter through `m20/m21` (the latch carries none); capture frames log the pass inputs
(`sun_shadow_apply_params`) so the CPU twin runs on the run's data without assumptions.

**Cascade-0 coverage, resolution and the bias (2026-09-17).** (The single-map options below were
removed on 2026-09-25 with the single map; cascade 0 of `--shadow-cascades` is the near map:
[directional-shadows.md](directional-shadows.md), "Single map removed".) The map box is tunable:
`--shadow-replay-extent E` (half-extent in the sun basis, 50–4000, default 250),
`--shadow-replay-depth-half D` (128–8192, default 512) and `--shadow-replay-size N`
(64–4096, default 1024), read once at device creation; the candidate box test, the replay
projection and the apply quad take all three from the same `ShadowReplayCascade`. The world
texel is `2 E / N`: 0.49 units at 250 / 1024, 0.98 at 1000 / 2048, 0.73 at 1500 / 4096; map
memory is `4 N²` bytes plus the depth attachment (4 MiB at 1024², 16 MiB at 2048², 64 MiB at
4096²). A station-wide box (E 1000–1500) at the default N therefore quadruples to octuples the
texel and needs N 2048–4096 to keep it; replay cost grows with the admitted casters, not with
N. The bias is expressed in world units so this rescaling does not change it:
`--sun-shadow-bias-units B` (default 0.53571875), the compare subtracts `(B + texel) / 2 D`
and clamps the receiver-plane term (also the non-planar fallback) at
`--sun-shadow-bias-clamp-texels T` world texels (1–64, default 20.97152;
`sun_shadow_apply_bias`), which at the default cascade resolves to exactly the former
0.001 / 0.01; the texel term is what a receiver-plane fit expects to be off by across one
texel, and the clamp scales with the texel because the plane term extrapolates a slope over
at most ~1.9 texels. The cascade program additionally lowers every tap's plane term by
`--sun-shadow-bias-slope-texels S` texels of |dz/du| + |dz/dv| before the clamp (0–8, default
0.2; `directional-shadows.md`, "Run 40 B (run119) near flicker": a sun-grazing plane's plane
term is extrapolated from a sub-texel baseline and misses the taps by a tenth of the 27-u
texel slope). The 21-texel default is today's production value kept for identity; the
detached fixture's tuned literals correspond to 4 texels, and the wide fixture shows the large
fallback lighting a few silhouette pixels of a receiver's own faces (directional-shadows.md),
so `--sun-shadow-bias-clamp-texels 4` is the tuning run 38 can try without a rebuild. One cascade remains a compromise
between covering a complex and resolving the own ship: cascades (the architecture note's
cascade 1–2 with their own extents) are the real answer for a station complex, and this
tunable is the way to measure what the one map can carry before they exist.

**Known limitation.** Colour-only draws over a receiver (blended effects, no depth write)
never veto the lane and are multiplied by the receiver's factor at scene end, because the
quad runs on the composed RT0 after them. Accepted for the first look; judged on the run 36
F8; the fix, if needed, is a share-0 RT2 write from those draws so the quad leaves them alone.

**Native Windows.** Documented D3D9 only: `A32B32G32R32F`/`R32F` render targets, ps_3_0
`dsx/dsy/texld`, `ZERO/SRCCOLOR` blending on `A16B16G16R16F` behind the
post-pixel-shader-blending query, no Wine export or layout. Unverified on Windows like the
rest of the renderer; add the row to [platform-portability.md](platform-portability.md).

**Alternative A — inject `f` into every program (loses).** ps_3_0 makes it possible on all
108: `dcl vPos.xy` (or a second VS varying carrying the sun-space position), one sampler
(`s15`; the pairs declare at most `s0–s5`), `c221–c225` (screen→sun rows and map constants,
uploaded with the `c216–220` window), ≈ 12 slots for one tap or ≈ 50 with PCF per hull
*fragment* (×overdraw), plus the one seed MUL. It needs no lane and no frame-availability
veto, and it is per-channel exact. It loses because the map is replayed at scene end, so a
program in frame N can only sample frame N−1's map: a one-frame lag of `v·Δt` (3 m at
200 m/s, 60 fps) on every self-shadow of the own ship, the case the cascade is fitted to;
and because it plumbs a sampler, five constants and a varying through 168 pairs with the
vPos half-texel and s15-ownership questions unsettled. It stays the fallback: if the lane
cannot reach ≥ 90 % available frames on original shading after §4's fixes in one run,
switch to A and accept the lag.

**Alternative B — full-pixel multiply without the lane (AO's law).** Zero material work,
but a near-binary shadow darkens emissive windows, lightmaps and point-lit hull by the whole
factor; rejected by the architecture note and not revived. Useful only as a debug overlay
of the mask.

## 3. Acceptance fixture

Base: the extraction GPU slice (216 cases, 55,296 valid pixels, `run_linear_material.py`)
and the actual-renderer slice (`run_sun_share_live.py`, 15 cases).

1. **Host** (`verification/analysis/test_original_sun_share.py`, structure helper beside
   `original_fill_structure.cpp`): all 108 originals × both depth modes × fill 0/0.05: lane
   off ⇒ byte-identical to the motion variant (and to the fill variant with `K > 0`);
   original instruction sequence verbatim and in order; combined slots ≤ 512 with counts;
   refusals for the contract's structural cases; the float64 oracle executes the original
   tail twice (seeds intact, seeds zeroed) and checks `s·Y(C) = Y(C) − Y(C_nosun)`.
2. **Detached GPU** (`linear_material_fixture.cpp`, `--original-sun-share`): the 108 with
   the calibration faces; colour byte-identical to the motion variant on every pixel
   (`f = 1` identity, 216 cases); `oC2.g` against a second draw with the sun constant zeroed:
   `|s·Y(C) − (Y(C) − Y(C_nosun))| ≤ 1` FP16 code; zero-sun faces `s = 0`; emissive faces
   `s < 1`.
3. **Apply quad** (new fixture mode in `motion_output_fixture.cpp`): synthetic `G32R32F`
   RT2 and a synthetic map (box on a plane at three sun elevations, analytic shadow, the
   architecture's own case): map cleared to far ⇒ RT0 byte-identical; `f = 0` everywhere ⇒
   `C·(1−s)` within one FP16 code; general ⇒ `C·(1−(1−f)·s)` against the CPU projection of
   the same map within one FP16 code and one texel at edges.
4. **Actual renderer** (`run_sun_share_live.py`, a `shadow_apply` case): the positive
   six-frame case with the replay and the quad on, TAA output equal to the CPU-computed
   reference, a Reset at frame 4, one frame with the map unavailable and one lane-unavailable
   frame both byte-identical to the no-apply twin.

## 4. What the next user run must show before any shadow

**Run81 (linear session, `--sun-shadow-lane --shadow-replay-depth`):** 2,123 lane frames,
**0 available**; receivers 203 / median 230 / max 802 per frame; 113,630 `state` refusals
from exactly two writer signatures, the cutout pairs `4944d81dfe531b37/5e0a10fe752b6140`
and `53a0a641107ed76c/63f96eba9eea7880` (registered, `z=1 zwrite=1`, outside the exact
cutout state); every other bucket 0, `non_depth_writers` 3–4. The replay side is healthy:
2,106 frames `replayed=8` (`draws=8`, no lease/state/caps skips), 42.3 µs median.

**What the design needs from the lane** (all in `src/proxy/motion_output.cpp`):

1. A lane latch on original shading: `sun_lane_active_` and the tested-opaque arm currently
   require `linear_material_requested_`, and `tools/manage.py` refuses `--sun-shadow-lane`
   without `--linear-materials`. The original share variant becomes the routed draw's bind
   pair the way the fill variant does (composed with `--original-fill`).
2. The two cutout pairs admitted through the tested-opaque arm under original shading. They
   are excluded from it by identity because the *converted* cutout needs the exact arm; with
   no conversion in play the same argument that admitted the XT alpha-tested draws applies
   (alpha test discards before the depth and RT2 writes; the variant's `oC0.a` is the
   original's). They are 2 of the 108 and get their own share. Without this the lane is
   vetoed on every frame they draw — run81's only veto.
3. Then, in the run: `sun_shadow_lane_frame available=1` on ≥ 90 % of frames near a
   station with `state = 0`, receivers in run81's range; `shadow_replay_depth replayed>0`
   on the same frames with the §3 predicates of shadow-replay-gates; one F8 at the run-51
   spot with the RT2 readback, from which `analyze_sun_share_lane.py` reports eligible
   pixels and a share histogram (fraction of receiver pixels with `0 < s < 1`, invalid
   count). Shadows stay off (`shadows=0`; the field is 1 once `--sun-shadow-apply` is on).

**What it can do without the lane:** build and prove the producer (§3.1–3.2) and the quad
(§3.3–3.4) on fixtures; and judge the map itself offline — project the F8 capture's RT2 `.r`
into a dumped map on the CPU to render a would-be mask image (acne, peter-panning, cascade
fit, bias) with no lane at all. The F8 capture does not dump the map today
(`x3m_shadow_replay_fixture_readback`, `capture.cpp:2475`, is fixture-only); adding a
4 MiB map dump to the capture is the one small change that makes this possible.

## Files the implementation touches

- `src/renderer/linear_material.{h,cpp}`: `linear_material_original_sun_share_pixel_variant`
  (fill-transform skeleton, contract's parallel carriers on original operands, fill twin,
  `c221` reduction); `src/renderer/linear_sun_share_inc.h` if the reduction is shared.
- `src/renderer/sun_shadow_apply_pass.{h,cpp}`, `src/temporal/sun_shadow_apply_ps.hlsl` →
  `sun_shadow_apply_program_inc.h`; `src/renderer/shadow_replay_pass.h` exposes the map
  texture and the frame's `S` rows; `src/renderer/shadow_replay_projection.h` for the rows.
- `src/proxy/motion_output.{h,cpp}`: lane latch on original shading, cutout admission in
  the tested-opaque arm, bind-pair selection, scene-end order (replay → apply → AO →
  resolve), Reset plumbing, `sun_shadow_apply_frame` line; `src/proxy/capture.cpp`: env gate
  and the map dump at F8; `tools/manage.py`: `--sun-shadow-lane` without `--linear-materials`,
  `--sun-shadow-apply` (default off, requires the lane and `--shadow-replay-depth`).
- `verification/analysis/test_original_sun_share.py`, `verification/probe/
  original_sun_share_structure.cpp`, `linear_material_fixture.cpp`, `run_linear_material.py`,
  `motion_output_fixture.cpp`, `run_sun_share_live.py`, `tools/analysis/analyze_sun_share_lane.py`;
  ledger `docs/verification/directional-shadows.md`.

## Unknown, and what settles it

1. GPU cost of the original share variants and of the quad on this backend: unmeasured;
   §3.2/3.3 fixtures with `us` lines, then the run's `frame_end` delta.
2. Whether the camera latch carries the projection's `z` row for linearization: AO's
   linearize program already converts RT2 `z/w`; reuse its constants (check
   `ambient_occlusion_pass.cpp` constant upload before writing the quad).
3. Hue error of the scalar share on real hulls (nebula cube, headlight, windows): the debug
   view on the first shadowed F8; the per-channel form needs three lanes and is not funded.
4. Bias/acne at 1024² over a 250-unit half-extent: §3.3's analytic cases, then the offline
   would-be mask from the F8 dump.
5. For alternative A only: the ps_3_0 `vPos` half-texel convention and whether the game ever
   binds `s15` (a session count of `SetTexture(stage ≥ 8)`).
6. Native Windows execution of every piece: unverified, as for the whole renderer.
