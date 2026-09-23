# Moving-hull softening under SETA: a parallax-gated history weight

Design note, 2026-09-23 (Fable). **Implemented 2026-09-23 (section 9 is the as-built record,
`docs/verification/temporal-resolve.md` "2026-09-23 motion history weight"); default 0.7,2,8 since 2026-09-23 after
Run 70 A, run262/run263** (launcher and DLL fallback, with an age program and a camera policy other than
`--taa-sentinel 1`; `--taa-motion-weight 0` opts out). Numbers marked [M] are Run 68 A measurements
(`verification/results/run254-exit/hull_sharp.py`, `hull_blurfit.py`, `hull_region_split.py`
and their `*_out*.txt`; ledger `docs/verification/temporal-resolve.md`, "Run 254"); [E] are estimates
from the model in section 3.

## 1. Decision

**Option (a), gated on the translation parallax the resolve already computes.** On the age variants
only, after the keep chain (`resolve.hlsl` line 637, before the alpha history), cap the history weight
by a floor that opens with the squared camera-relative displacement `dot(relative, relative)` of
line 401-402:

```
cap  = saturate(max(F, parallax2 * A + B))        // A = -(1-F)/(V1^2-V0^2), B = 1 + (1-F) V0^2/(V1^2-V0^2)
keep = min(keep, cap)                              // never raises a young pixel's n/(n+1)
```

`cap` is 1 at or below V0 px/frame of parallax, F at or above V1, quadratic in px between (linear in
px^2: no sqrt). Launcher `--taa-motion-weight F[,V0,V1]` (`X3M_TAA_MOTION_WEIGHT`; requires `--taa`
and an age program, the exit reset's capability rule; 0 off; else 0.5 <= F < 1, 0 <= V0 < V1 <= 64).
**Default `0.7,2,8` since 2026-09-23 (Run 70 A: run262 at 0.8, run263 at 0.7; the user prefers 0.7).** Off uploads A = 0, B = 1,
F = 1: `cap` is 1 on every pixel and `min(keep, 1)` is `keep` bit for bit.

Why this discriminator: the mask's thin-region gate runs on `min(screen speed, camera-relative
speed)` against the *translation-aware* path (`line_mask_camera_ps.hlsl` line 49, taa-lattice-crawl.md
32.2-32.3), so a world-static station under SETA keeps b = 255 and the 0.97 ceiling at 12 px/frame
[M: b = 255 on 52-64 % of moving hull]. The resolve's `cameraUV` is the *rotation-only* far-plane path
(`resolve.hlsl` 351-359: "an approaching station under SETA: 3-38 px/frame, a pan: 0"), so
`relative` is exactly the quantity that separates SETA translation from a pan (roll/yaw residual
p50/p90/p99 0.030/0.069/0.092 px, 32.2 [M]) and from rest (0). No new input, no new tap.

## 2. What is being fixed [M]

Hull sharpness ratio E_taa/E_hdr: 0.44 / 0.46 / 0.35 below 0.5 px/frame, 0.25 / 0.38 / 0.12 at
0.5-1, 0.02-0.10 at >= 1 px/frame in all three bursts (normal speed included). Gaussian-equivalent
sigma 0.5-0.7 px at rest, 1.0-1.4 px at >= 1 px/frame; the blur fit explains 84-89 % of output minus
input: resample softening of long-lived history (median age 48-64), not ghosting. On moving hull
b = 255 gives sigma 1.4 (ceiling 0.97), b = 0 gives 1.0 (base 0.9). `taa_weight` 0.900, sharpen 0.75,
mip bias -0.5 constant across bins. Hull parallax p50/p90/p99: SETA 0.46 / 12.6 / 26.5, normal speed
0.38 / 0.99 / 1.64 px/frame; the fast SETA pixels sit mostly at >= 8 px/frame (4-8 bin 12-15 k px,
8-16 17-24 k, >= 16 19-21 k per burst).

## 3. Expected effect and its risk

Model (`verification/results/run254-exit/motion_weight_model.py`): sigma^2 = sigma_rest^2 + c * w/(1-w),
w/(1-w) being the mean number of Catmull-Rom resamples an accumulated sample has been through;
calibrated on two points (0.9 -> 1.0, rest 0.6) c = 0.071 px^2 per resample, which predicts 1.63 at
0.97 against the measured 1.4 bin (the sigma grid steps x1.4). Residual random-phase alias amplitude
after exponential averaging: sqrt((1-w)/(1+w)).

| parallax | cap (0.8,2,8) | keep b=0 / b=255 | sigma today [M] | sigma [E] | alias amp. today -> new |
| --- | --- | --- | --- | --- | --- |
| <= 2 (rest, pans, normal flight p99 1.64, run209 forward truss p99 3.31 -> cap 0.977) | 1 | 0.90 / 0.97 unchanged | 1.0 / 1.4 (0.5-0.7 at rest) | unchanged | unchanged |
| 1 | 1 | unchanged | 1.0 / 1.4 | 1.0 / 1.4 | 0.23 / 0.12 |
| 5 | 0.93 | 0.90 / 0.93 | 1.0 / 1.4 | 1.0 / 1.15 | 0.23 / 0.12 -> 0.23 / 0.19 |
| 8-38 (12) | 0.80 | 0.80 / 0.80 | 1.0 / 1.4 | 0.80 / 0.80 | 0.23 / 0.12 -> 0.33 / 0.33 |

The cap first touches the 0.97 pixels at 3.6 px/frame and the 0.9 base at 5.8, so nothing below
3.3 px/frame of translation parallax changes anywhere: rest, pans, the lattice on a normal forward
approach (32.2), the band (refused at >= 3 px), the SETA_EXIT rows (0.5 px/frame) and the sky
(`relative` = 0). The effect concentrates on the SETA leg by construction of the quadratic ramp.
Risk reintroduced: at >= 8 px/frame the hull's residual alias amplitude rises x1.45 over today's b = 0
pixels and x2.7 over the b = 255 ones, on a texture the -0.5 mip bias keeps above Nyquist, i.e.
sparkle on panel lines and specular texture during SETA; at 5 px/frame the b = 255 half rises to
0.19, still below today's b = 0 half. Only the flight rates the trade; F = 0.7 (sigma 0.73 [E], alias
0.42) is the second A/B value, and the lattice's clip relaxation (`b * RELAX`) is untouched at every
speed, so crawl suppression keeps its main term (section 13: "x 0.40 with the weight alone, the clip
is the cause"); only the accumulation length shortens.

## 4. Cost, register, variants

- **Instruction slots [E]:** `mad` (parallax2 * A + B), `max_sat`, `min` = +3 on each age variant
  if the compiler reuses the `dp2add` of line 402 (ps_3_0 has no plain dp2; a fresh dp2add makes it
  +4). Today: age 495, age_filter 507, age_line 507, far 498, **far_camera 510 of 512**
  (seta-sky-hull-share-decay.md section 4). far_camera therefore needs a **one-slot exact reserve**
  or the term does not fit the flown program. Candidates, to be proven bit-identical on the fixture
  before the term (the exit reset's method): (i) the far speed gate at line 632,
  `g * (1 - saturate(s))` as `g - g * saturate(s)` (a mad; exact where s is 0 or 1, i.e. at rest and at
  or above HI = 0.25 px/frame, which is every hull, pan and static row; last-ulp inside the
  0.03-0.25 window, where the 64 FAR_STABILISER drift rows decide); (ii) the camera-gate blend at
  line 582, `(b - a) * S` as `b * S - t` with `t = a * S` already computed (exact at a = b unless the
  backend fuses the mad). Unknown until `RESOLVE_BUDGET` is run (section 7).
- **Register:** no new register. c25.yzw are free lanes ("yzw 0", `resolve.h` line 40): y = A,
  z = B, w = F, uploaded with c24/c25 as the existing two-register block on age programs
  (`temporal_pass.cpp` 428-430, `prepare_exit` extended or a sibling `prepare_motion_weight`).
- **Variants:** the five `X3M_AGE_WEIGHT` programs (age, age_filter, age_line, far, far_camera), under
  the same `#ifdef`; far_camera is the flown one. Plain / thin / line variants read c25 nowhere and
  their bytecode stays unchanged; without an age program the pass logs `unavailable=1
  reason=no_age_program` once and runs without it, the exit reset's rule. Every hull-writing
  configuration the launcher ships with the thin region or the far stabiliser is an age program.
- **GPU / CPU / VRAM:** no tap, no target, no branch; three ALU slots per pixel (below the
  `LINE_TIMING_CAMERA` run-to-run noise); three constant lanes in an upload that exists; none.
- **Placement:** after the far/adaptive `#endif` (line 637), before the alpha history so the alpha
  blend uses the same `keep` (line 643); after the age read (the exit reset's lesson: a read before
  the verdict regrouped the Catmull-Rom weights). The age write is unchanged (the count serves
  n/(n+1) and the exit mark); when the hull slows below 3.6 px/frame the 0.97 ceiling returns at once
  from a sharper history, no visible step. `min` never raises a weight (young pixels, the exit reset's
  0); `relative` is finite on the blend path (an overflowing correspondence fails `lookup` at line 417
  first), so 0 * parallax2 + 1 is exactly 1 with the option off and a NaN cannot reach `cap`.

## 5. Native Windows behaviour

The same ps_3_0 program; `mad`, `max`, `min` and `saturate` are exact on any conforming device;
the age pair's R32F MRT is the documented capability the far stabiliser already requires
(`D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`). Source-compatible, unverified natively (the user cannot
test Windows); the fixture proves it under CrossOver only.

## 6. Verification (fixture, `verification/probe/run_temporal_pass.py`; no flight needed to prove the mechanism)

Case (m), `MOTION_WEIGHT` rows in `temporal_pass_fixture.cpp` beside the `SETA_EXIT` case, parsed by the
runner like `seta_exit`: a routed 24x24 hull (alpha 1, depth 0.9997, exact motion vectors) textured
with a 4 px-period stripe, static 16 frames then translating at **1 / 5 / 12 px/frame** for 32 frames
with a static camera path (`relative` = the displacement), jitter on, on the far_camera program with
the flown settings (weight 0.9, thin 0.97 camera gate, far 0.985, strict + band 3 + exit 0.25) and on
the age program. Per row: E ratio (interior Laplacian energy of the resolve over the current jittered
sample, the triage's measure), best Gaussian sigma on the grid {0, .35, .5, .7, 1, 1.4, 2} against the
unjittered stripe, mean |age|, and the interior frame-to-frame rms after the transient (the flicker
witness). Asserted:

1. **Off path bit-identical:** option off, every existing row unchanged (stable-line comparison,
   0 removed / 0 added; the far-camera static `colour_identical=1 age_identical=1` rows; loose
   identical; non-age programs' bytes unchanged). The reserve rewrite is proven the same way first.
2. **1 px/frame row bit-identical to off** on colour and age (cap 1.01 > every keep); likewise a
   **pan row** (camera yaw 5 px/frame, hull world-static: `relative` ~ 0) and a rest row.
3. **12 px/frame:** E ratio >= 1.5x the off row's and sigma one grid step lower (a period-4 stripe
   at sigma 1.0 keeps 8.5 % of its Laplacian energy, at 0.8 21 % [E]); **5 px/frame:** E ratio >= the
   off row's, age identical; ripple at 12 <= 2x the off row's. Record the first run, then assert.
4. `RESOLVE_BUDGET` <= 512 on all five age programs, far_camera recorded; SETA_EXIT, THIN_REGION
   (18 rows) and FAR_STABILISER drift rows unchanged with the option on (all below V0).
5. Host tests (`test_taa_image_defaults` pattern): forwarding, range refusals (F, V0, V1, `--taa`,
   age program), DLL default 0; `motion_output_mode` logs `motion_weight=F,V0,V1`.

## 7. Alternatives considered

- **(b) History sharpening / unsharp of the Catmull-Rom sample by resample count.** The history sits
  inside the recurrence: any kernel with |K(f)| > 1/w at some frequency diverges, and at w 0.97 the
  admissible boost is 3 %; section 15 already rejected Keys -0.75 at W 0.97 (feedback 1.027) and
  could not clear -0.65 (0.993) for long-term ringing. Scaling by age does not help (steady state is
  age 64), and it costs either 9 more history taps or re-weighting the 16 (a different kernel, the
  same stability question) on a program with 2 free slots. Loses on stability and cost.
- **(c) Raise RCAS on moving pixels.** Display-only, five taps, lobe <= -0.1875, clamped to the
  ring's min/max: at 0.75 it recovers x1.5 in energy (E_present/E_color 0.053-0.086 against
  E_taa/E_hdr 0.035-0.062 at >= 1 px/frame [M]); s = 1 at most doubles that, 0.035 -> ~0.07 against
  0.44 at rest, and amplifies the residual alias alike. It also needs the motion texture and the
  rotation-only path in the write-back program. Loses on effect size; a possible complement later.
- **(d) Scale b in the mask by parallax (0 resolve slots).** Also scales the clip relaxation (the
  crawl's main term returns on a moving lattice), leaves the 0.9 base (sigma 1.0), and the mask's
  camera path is the translation-aware one (it would need the rotation-only path as well). Loses.
- **(e) Lower the global weight or the 0.97 ceiling.** Touches rest and pans (ripple x0.086 at 0.97
  needs the long accumulation). Loses on the invariant.

## 8. Unknown, and what settles it

1. Exact slot cost and whether far_camera takes it: `RESOLVE_BUDGET` after the reserve rewrite; if
   neither candidate is bit-identical on the stable rows and no other exact reserve is found, the
   fallback is candidate (i) with its last-ulp change inside the 0.03-0.25 px/frame far-gate window
   accepted as a documented deviation, bounded by the drift rows.
2. Whether the sparkle at >= 8 px/frame reads worse than the blur: the flight A/B (`0.8,2,8` against
   off, then `0.7,2,8`); the fixture's ripple row bounds it but does not rate it.
3. The sigma model is a two-point calibration on a x1.4 grid; the 12 px/frame fixture row measures
   the real curve at 0.8 before any flight.
4. **Answered at review (co-moving objects).** An object that moves with the camera (the player's ship in
   the external view, an escort holding formation) has screen motion about 0 while its `relative` is the
   turn's displacement, so the section 1 gate alone would cap it above V1 px/frame of yaw. As built the
   gate is `min(dot(relative, relative), dot(screenPx, screenPx))`, `screenPx` the pixel's own lookup
   displacement (`previousUV - uv` in px, formed for the lookup anyway): a pan (parallax 0), a co-moving
   hull (screen motion 0) and rest keep 1, the SETA hull (both large) is capped. The fixture's co-moving
   row (camera yaw 12.5 px/frame, hull screen-static) is bit-identical to off; the min also bounds the
   gate for every input (the screen term is at most W^2 + H^2 px^2 inside the texture), so an overflowing
   parallax cannot reach the off path's `0 * p2 + 1`.

## 9. As built (2026-09-23)

**Term.** `resolve.hlsl`, `X3M_AGE_WEIGHT` variants only, computed beside the depth proof (the compiler sinks it to the
blend anyway and keeps `parallax2` live across the loops) and applied after the far / adaptive keep and before the alpha
history, exactly the section 1 form: `cap = saturate(max(F, dot(relative, relative) * A + B))`, `keep = min(keep, cap)`,
c25.yzw = A, B, F (`prepare_motion_weight`, `resolve.h`; `prepare_exit` now writes the off triple 0, 1, 1 into yzw and
`prepare_motion_weight` follows it). The age write is untouched: the cap changes the weight, never the count (fixture: the
age target identical to off on every row). Plumbing as the exit reset's: `FrameInputs::motion_weight, _v0, _v1` validated by
`run()` (`valid_motion_weight`: 0, or 0.5 <= F < 1 with 0 <= V0 < V1 <= 64), `X3M_TAA_MOTION_WEIGHT=F[,V0,V1]` parsed in
`capture.cpp` (1 or 3 fields, else `taa_motion_weight_setting invalid=1`), logged as `motion_weight=F,V0,V1` in
`motion_output_mode`, dropped per device without an age program (`motion_output_taa_motion_weight ... unavailable=1
reason=no_age_program`), and handed to the pass only under camera policy 2 (`relative` is the translation parallax against
the far-plane path there and the screen motion otherwise, i.e. a pan would be capped without the camera path).
`--taa-motion-weight F[,V0,V1]` (requires `--taa`; F > 0 requires an age program; 0 the explicit off; omitted: 0.7,2,8 with an
age program under a policy other than `--taa-sentinel 1`, else 0, always forwarded; the DLL's fallback for an absent variable
matches, an invalid or oversized value stays off, logged).

**Slots (measured, D3DXDisassembleShader through the generator and the fixture's `RESOLVE_BUDGET`).** The term as written
costs 4 slots on the far variants (a fresh `dp2add` for `parallax2`, `mad`, `max_sat`, `min`) and 6 / 6 / 9 on age /
age_filter / age_line: the extra live scalar reshuffles the register allocation of the adaptive variants (movs), and every
attempt to fold the cap into a value already live across the loops (the sign of `tolerance`, or reading the strict-sky
predicate from `threshold`) cost one more. Neither of the note's two candidates is exact (the far-gate fold `g - g s`
differs in the last ulp for `s` inside the 0.03..0.25 px/frame window, the camera-blend fold `b S - a S` for 0 < a < b) and
each saves one slot, which would not have covered the four. The reserve applied instead is two rewrites that are exact by
construction, on the age variants only (`#ifdef X3M_AGE_WEIGHT`; the plain / thin / line programs' bytes are unchanged):

- the closest-depth dilation fetches the centre texel with the eight neighbours instead of skipping it (one `texldl` more
  per pixel, the `kx != 0 || ky != 0` test and its branch gone: 5 to 7 slots): the centre cannot win (`neighbor < nearest`
  fails on its own depth, a far-plane pixel's texel is the sentinel and fails `neighbor >= 0`) and the soft-clip flags it
  would set are the ones the initialisers already set;
- the alpha history's range test `blended >= low && blended <= high` as `min(blended - low, high - blended) >= 0` (1 to
  3 slots): exact when `low`, `high` and `blended` are finite (the differences are the ones the compares formed); a NaN
  `blended` gives `min(NaN, NaN)`, which fails as before; a non-finite `low` / `high` (an FP16 scene alpha the game wrote
  as NaN or infinite, never seen; the range starts at the finite current alpha) is the one input where the backend's
  `min` with a NaN operand decides, where the original kept the current alpha. HDR route only (c22.z).

Reserve alone (before -> after): age 495 -> 487, age_filter 507 -> 499, age_line 507 -> 499, far 498 -> 488, far_camera
510 -> 500; proven bit-identical on the committed fixture and runner before the term went in (0 stable lines removed / 0
added on both reports, all 488 + 375 SAMPLE lines present, the SETA_EXIT rows identical). With the term: see the table
below. The centre tap's cost is one fetch per pixel on the age programs (28 -> 29 on far_camera). The GPU cost is
unmeasured: `LINE_TIMING_CAMERA` is a CPU-wall row whose run-to-run spread (1.33-2.01 ms across the three runs of this
change) exceeds anything one fetch and a few slots could add; the flight's `--taa-debug` timing is the only measure.

**With the term (measured, `RESOLVE_BUDGET` of the fixture and the generator's records; after cleanup batch 6 the age
variants are age, far and far_camera):** age 495 -> **494**, far 498 -> **493**, far_camera 510 -> **505** of 512 (net
-1 / -5 / -5 against the committed programs; the term with the co-moving gate of section 8 item 4 is +7 / +5 / +5 over the
reserve, the gate's `min` against the screen motion +1 on each). Measured before the batch-6 rebase, the section-1 form
without that gate was age 493, age_filter 505, age_line 508, far 492, far_camera 504. The plain / thin / snapshot programs:
bytes unchanged (their headers did not regenerate; the records' include hash moved with `resolve.hlsl`).

**Fixture (`verification/probe/run_temporal_pass.py`, case (m) `MOTION_WEIGHT`, `RESULT PASS numerical=712
state_restorations=278 generations=2`, 528 samples; the ledger has the full table).** Deviations from section 6: the hull is
a 512x16 strip (the 32-px edge scene holds a chain of only S / v frames at 12 px/frame), the stripe is a period-4 sinusoid
point-sampled at the jittered position (the Gaussian sigma of a sinusoid is its amplitude ratio, so a continuous
`sigma_fit` is reported beside the x1.4 grid), integer speeds were kept and half-texel ones added: at an integer speed the
history tap lands on the texel grid and nothing is resampled (E ratio 0.65 at 1, 5 and 12 px/frame, the same as at rest),
so the softening the option is for shows only at fractional speeds (E ratio 0.20 at 1.5, 5.5 and 12.5 px/frame, sigma_fit
0.81 against 0.43 at rest). The pan row yaws 12.5 px/frame (the note's 5 would be vacuous: the cap at 5 is above the
0.9 base); a co-moving row (camera yaw 12.5 px/frame, hull screen-static) proves the section 8 item 4 gate, and a 6.5
px/frame half-texel row proves the ramp at a binding point (cap 0.8725: within 0.002 of a run whose cap is that constant,
0.033 / 0.037 from off). The flicker witness compares jitter phases two frames apart (12.5 x 2 = 25 px). Off path: 0
stable lines removed / 40 added (the new metrics) on temporal-pass.txt, 0 / 0 on temporal-lattice.txt, all 488 + 331
SAMPLE lines present, SETA_EXIT rows identical.

| row (both programs; far_camera = the flown configuration, age = the adaptive one) | E ratio off -> on | sigma_fit off -> on (grid) | ripple rms off -> on | output / age diff vs off |
| --- | --- | --- | --- | --- |
| rest | 0.647 / 0.622 (fc) unchanged | 0.43 / 0.45 (0.35 / 0.5) | 0.0142 / 0.0089 | **0 / 0** |
| 1, 5, 12 px/frame (texel grid) | 0.649 -> 0.649, 0.649 -> 0.649, 0.647 -> **0.696** | 0.42, 0.42, 0.43 -> 0.39 | 0.0141, 0.0141, 0.0142 -> 0.0276 | 0 / 0, 0 / 0, 0.082 (0.087 fc) / **0** |
| 1.5, 5.5 px/frame (half texel) | 0.197 -> 0.197 both | 0.81 -> 0.81 | 0.0094 -> 0.0094 | **0 / 0** both |
| 6.5 px/frame (half texel, the ramp binds: cap 0.8725) | 0.197 -> **0.255** | 0.81 -> 0.75 | 0.0095 -> 0.0122 | 0.033 (0.037 fc) / **0**; 0 against the constant-cap run |
| **12.5 px/frame** | 0.197 -> **0.383 (x1.95)** | 0.81 -> **0.62** (0.7 -> 0.7) | 0.0095 -> 0.0195 (**x2.06**) | 0.093 (0.104 fc) / **0** |
| pan 12.5 px/frame (hull world-static) | 0.197 unchanged | 0.81 | 0.0095 | **0 / 0** |
| co-moving 12.5 px/frame (camera yaw, hull screen-static) | 0.647 / 0.622 (fc) unchanged | 0.43 / 0.45 | 0.0142 / 0.0089 | **0 / 0** |

The section 3 model said sigma 1.0 -> 0.8 (E x2.5) and alias x1.45 for 0.9 -> 0.8; the strip measures E x1.95 and a
two-frame ripple x2.06 at the half-texel resample, so the fixture's ripple bound is 2.5x (the first run's number, as
section 6 asked). Only the flight rates that trade (section 8, item 2). Everything below V0 and every pan is bit-identical
on both targets, and the age target never differs: the cap changes the weight, never the count.
