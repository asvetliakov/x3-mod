# SETA trailing smear: shedding the hull share a sky pixel took in the band

2026-09-22, design note (Fable, high). Decision: how a strict-sky pixel sheds the hull
colour it acquired while it sat in the sub-threshold 1 px band beside a slowly receding
silhouette, so the 3-12 px trail behind a SETA silhouette disappears without touching pans,
static edges or the flickering sky's ordinary accumulation. Predecessors:
`seta-motion.md` sections 3 (strict term) and 4 (band term); evidence
`verification/results/run249-band/` (Run67 DLL, strict + band 3 px, `/tmp/x3-bottleX3-run249`).
Status: **implemented 2026-09-22 (fixture-proven, awaiting review and commit; `docs/verification/temporal-resolve.md`
"2026-09-22 exit reset"), default off until flown.** Sections 1, 4, 6 and 7 describe the as-built form; section 5 is the
design-time comparison as written.

## 1. Decision (as built)

**Exit reset, carried as one bit in the age target.** A band pixel (unrouted far-plane pixel
a closer neighbour won the dilation for) that accepts history while its correspondence moves
at least `EXIT_PX` px/frame of translation parallax against the camera path (below the band
threshold, else it is already refused) writes its age *negated*. The next frame, a strict-sky
pixel (`nearest == 1`: no geometry in its 3x3, alpha not 1) under strict whose nearest
reprojected age texel is negative keeps no history that one frame: at the blend its weight is 0
(the output is the current sample exactly at k = 0, `x + 0 * (old - x)`; on the HDR route within
the weighting's rounding) and its count restarts at 1. The hull share leaves in one frame
instead of decaying at the history weight; the band itself, pans, static edges, routed pixels
and the loose path are untouched. Measured cost: +1 / +1 / 0 / +3 / +3 instruction slots on the
age / age_filter / age_line / far / far_camera programs after two exact rewrites paid for the
term (section 4), no new tap; nothing on the plain / thin / line variants.

## 2. Invariants every option must keep

1. Pans and static edges bit-identical to today's strict (fixture (g) static ring, (h) pan,
   (j) projective pan: `strict - loose == 0` on every pixel and frame). Both have zero
   translation parallax, so any new term must be gated on parallax measured while the pixel
   has a routed neighbour, i.e. while it is in the band. Once it is strict sky there is no
   parallax signal at all (no routed neighbour): the gate can only be *remembered*.
2. Loose path untouched: `options.z == 0` disables every new term, and the pass uploads the
   new threshold as off under loose so the age target's bytes are unchanged too.
3. No change when the band term is off: the term is read only through `band`, which is 0 for
   every non-band pixel and cleared by the alpha gate (`band *= step(alpha, 0.5)`).
4. Cost bounded per pixel; no new render target. The age pair (R32F, `ages_[2]`,
   `temporal_pass.cpp:287`, written as COLOR1 by every `X3M_AGE_WEIGHT` variant, read only by
   the resolve at s7 and by the `--taa-debug` readback in `motion_output.cpp:1731`) is the
   only per-pixel memory; the plain / thin / line variants have none.
5. The 1 px band keeps its anti-aliased accumulation (that share is correct while the edge is
   there); only what remains after the edge has left is the target.

## 3. Mechanism the design must undo (measured, run249)

The share enters in the band, not under cover: of the dark d3-12 pixels none had geometry at
its own position last frame (`mid_band_mech`: `A prev-depth geometry 0` on all three frames),
and 182 / 140 / 58 of 193 / 204 / 78 were already 3-12 px from the silhouette last frame. The
band pixel takes its routed neighbour's correspondence; below 3 px/frame the tap lands about on
its own previous texel, which alternates hull and sky under the jitter, so the pixel converges to
its coverage (the AA edge). When the edge recedes the pixel becomes strict sky; its tap is a
sentinel-depth texel (sky last frame) carrying that coverage, the loose depth proof accepts it,
the 3x3 clip of a star field (CV 0.37-0.40, `mid_band_series`) has `mean - 1.25 sigma` at about
half the mean and does not pull it, and the 0.9 blend decays it slowly while the sky flickers.
Where the share enters, by parallax of the band's nearest routed neighbour
(`band_parallax_fine.py`, measured, dark d1 pixels, share of the burst's total):

| parallax px/frame | < 0.25 | 0.25-0.5 | 0.5-1 | 1-2 | 2-3 | >= 3 (refused) |
| --- | --- | --- | --- | --- | --- | --- |
| burst 1 (3515+32) | 2.8 % | 13.9 % | 37.5 % | 25.2 % | 19.2 % | 1.5 % |
| burst 2 (5538+32) | 2.2 % | 8.4 % | 14.8 % | 28.8 % | 38.4 % | 7.3 % |

Under a pan the parallax is 0 exactly (fixtures (h), (j)); a static edge has no motion. A floor
of 0.25 px/frame therefore marks 97-98 % of the entering share and nothing under a pan.

## 4. Design as built: exit reset with a one-bit age memory

### Per-pixel maths (resolve.hlsl, `X3M_AGE_WEIGHT` variants only)

Write side, beside `refused`, read at the blend only (a refused or rejected pixel returns
current-only and writes age 1 as today):

```
// exit mark: a band pixel whose parallax reaches c25.x (EXIT_PX^2; 1e30 when off or not strict)
// but not the band threshold (a refused pixel never reaches the blend). 0 or 1, a select on band.
float exiting = dot(relative, relative) >= skyExit.x ? band : 0;
...
float aged = min(age + 1, 64);
return emit(..., -exiting >= 0 ? aged : -aged);   // one cmp with a negate modifier
```

Read side, at the blend where the age tap always was, after the depth proof and the history
taps:

```
float ageRaw = fetch(previousAge, tap + float2(f.x >= 0.5 ? 1 : 0, f.y >= 0.5 ? 1 : 0) * sizeJitter.xy).r;
float age = abs(ageRaw);
age = age <= 64 ? age : 1;                 // above 64 or NaN restarts; no |age| below 1 is readable
float keeping = max(tolerance, ageRaw);    // < 0 only on a strict-sky pixel under strict whose texel is marked
age = keeping >= 0 ? age : 0;              // adaptive variants: keep = min(0 / 1, ..) = 0, the write min(0 + 1, 64) = 1
keep = keeping >= 0 ? keep : 0;            // far variants (their keep does not pass through the age alone)
```

The strict-sky predicate is the sign of `tolerance`: the far-plane term (`tolerance -=
step(1, nearest) * c7.z`) takes 3 off it exactly on a strict-sky pixel under strict and nowhere
else, and every operand is finite, so `max(tolerance, ageRaw) >= 0` is "keep the history" without
a product; the texel is below 0 exactly when marked. A pixel still in the band, a routed pixel
and every geometry pixel read the mark as "keep" and continue the count through the sign
(`abs`): the far age ramp on a hull pixel that reads a marked texel is the count's, unchanged.
The `[1, 64]` range test became `abs(age) <= 64`: this program writes counts of 1..64 only, 0
never, and s7 is bound only behind a valid history every pixel of which this program wrote, so
the lower bound was unreachable; a NaN still fails the compare. The age write splats the count
to every lane (R32F stores .x only: same bytes, one instruction fewer).

**Why the blend, not `considered`.** The design-time form started the disocclusion sum at 3 for
a marked strict-sky pixel (current-only before the history taps). Reading the age tap before the
proof's verdict made the D3DX optimiser regroup the Catmull-Rom weight polynomials of the age
variants (a different mul / mad order), and the lattice fixture's "past the speed gate the
thin-region run is the plain resolve bit for bit" row failed on the drifting shards (fractional
footprint; the static square stayed identical). At the blend the instruction order of the
weights is the plain program's and every existing row is bit-identical. The price is that a
reset pixel still fetches its 16 history taps that frame (the band's exit edge only).

Why one bit and one frame suffice: after the reset the pixel's history *is* the current sky
sample; from then on its own texel is clean. The neighbour still in the band is marked
separately and resets when it leaves. Under the SETA leg (rotation 0, identity camera path,
`mid_band_chain`: far-sky shift (0, 0) on every frame) the footprint is the single texel
(`f == 0`), so no share is re-imported from marked neighbours; under rotation the 4x4 footprint
reaches the band texels (section 7, item 3: measured). The depth silhouette's jitter makes a
boundary pixel flip between band and strict for a few frames at 0.25-0.5 px/frame, so it may
reset more than once; each reset costs that pixel its flicker suppression until it
re-accumulates: on the far stabiliser the count restarts at 1 and the weight climbs back along
n / (n + 1) toward 0.985 over about 64 frames (the count saturates there), on the adaptive
weight toward 0.97 over about 32; on the 2 px ring beside a sub-threshold moving edge only.
That is the one look risk (section 7).

### Register, target, plumbing

- Constant: one new register `c25` (`kExitRegister`, x = `EXIT_PX^2`, yzw 0), uploaded with c24
  as one two-register block on the age programs (`aged ? 2 : 1` registers; no extra API call);
  every lane of c5, c6, c7, c22 and c24 is taken and c8-c21 / c23 belong to the AgX write-back
  and the sharpen. `prepare_exit` uploads P^2 only when the strict term is in effect
  (`constants.options[2] == 3`) and 1e30 otherwise (loose, policy 1, option off), so the age
  target's bytes are those of the option off there.
- Target: the existing age pair; values become `-64..-1, 1..64`. Only the resolve and the
  `--taa-debug` age readback read them: the readback's consumers must take `abs()`
  (`tools/analysis/taa_sentinel_pan_replay.py` and `taa_sentinel_pan_variants.py` seed their
  replay with `abs()` of the dump; nothing else reads it), and a negative count is a marked band
  pixel (a free diagnostic: the mark's footprint in flight).
- Capability boundary: requires an age program (`age_available()`: MRT with independent bit
  depths, R32F), i.e. `--taa-far-stabiliser`, `--taa-thin-region` (the far program) or
  `--taa-adaptive-weight`, the flown configuration being far_camera. Without one the pass logs
  `motion_output_taa_sky_history_exit ... unavailable=1 reason=no_age_program` once per device
  and runs without it, the far stabiliser's rule. Non-age programs' bytecode is unchanged (the
  term is under `#ifdef X3M_AGE_WEIGHT`).
- Launcher: `--taa-sky-history-exit-px P` (`X3M_TAA_SKY_HISTORY_EXIT_PX`; requires `--taa`; a
  value above 0 also requires `--taa-sky-history strict` and one of the three age options; 0 is
  the explicit off, accepted with `--taa` alone; else 0.125 <= P <= band px; **default off**).
  The DLL logs `sky_history_exit_px` in `motion_output_mode`, refuses a non-numeric or
  out-of-range value (`taa_sky_history_exit_px_setting invalid=1`) and a positive value without
  strict (`refused=1 reason=requires_strict`). `TemporalPass::run()` validates
  `FrameInputs::sky_history_exit_px` (`valid_sky_history_exit`: 0 or 0.125..band px,
  `E_INVALIDARG` otherwise). Recommended first flight value **0.25** (0.5 covers 83-89 % of the
  entering share, 0.25 covers 97-98 %, table above).

### Cost on the hot path (measured, `RESOLVE_BUDGET` of the fixture)

The prediction (4 slots) did not survive the compiler: the term as first written cost 11 slots
on the age program (every `step`, `?:` and the sign select compile to add + cmp pairs), and the
note's reserve, the age tap's rounding as `step(0.5, f)`, saved nothing (the same add + cmp).
The reserve applied instead is the two exact rewrites above (splat write, `abs(age) <= 64`),
proven bit-identical on the committed fixture before the term: -5 slots on every age variant.
Net, before -> after: age 494 -> 495, age_filter 506 -> 507, age_line 507 -> 507, far 495 ->
498, far_camera 507 -> 510 of 512 (age_line stays at 507 because its reserve and term cancel
exactly). The plain / thin / line variants: 0. GPU time: `LINE_TIMING_CAMERA` (1280x768, the
far-camera program) moved 1.4375 / 1.6160 -> 1.4712 / 1.6113 ms (no region) and 1.2554 / 1.7464
-> 1.3329 / 1.8870 ms (fragmented pan) in the retained fixture output, run-to-run noise of that
CPU-wall row.

Policy flip: a mark written on frame N is consumed only if frame N+1 still runs strict with the
sentinel policy 2; if the policy leaves 2 that frame the pass uploads 1e30, the mark is read as a
plain age and overwritten positive, so the reset is skipped for that pixel, not deferred. CPU: one
constant lane in a block already uploaded. VRAM: none.

### Native Windows behaviour

The same ps_3_0 program; the age pair's R32F MRT is a documented D3D9 capability the far
stabiliser already requires (`D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`). A negative float in an
R32F target and `abs` / `max` / `cmp` are exact on any conforming device; nothing depends on
Wine. Not verified natively (the user cannot test Windows): the fixture proves it under
CrossOver only.

## 5. Alternatives considered

**(A) Colour-side lower bound for strict sky history** (history luma clamped to a fraction of
the 3x3 current-sky mean, or of the pixel's own N-frame history mean). Loses because it cannot
tell the trail from a legitimately dark sky texel: a dark gap beside a star has a 3x3 mean
dominated by the star, so the clamp brightens the gap; the ordinary accumulation the far tail
shows (output 0.85-0.91 of own mean, lag-1 ~0, 16-18 % "below own mean" at 13+ px) is the same
population and would be touched too, i.e. an asymmetric clip on the whole sky under every camera
motion, breaking invariant 1 unless gated by the very memory of the chosen design (then it is
the chosen design with a slower decay). An own-mean over N frames needs N frames of storage the
age pair cannot hold. Cost: the weighted 3x3 mean exists (0 taps, ~3 ALU) but the failure is
semantic, not cost.

**(B) Memory plus a faster decay weight for K frames.** A countdown needs more than one bit
(fractional or offset encoding: `frc`, `floor`, decrement, re-encode, 8-10 slots) and overflows
age_line / far_camera by 4-6 slots; and a decay of `w` per frame for K frames leaves `w^K` of the
share (0.5 for K = 3 leaves 1/8). Its K = 1, `w = 0` corner is the chosen design. Kept as the
fallback if the flight shows the one-frame reset as sparkle: then the mark could lower `keep` to
`0.5` for the exit frame instead (same bit, same cost, replaces the `considered` mad by a `cmp`
on `keep`), at the price of two exit frames.

**(C) Sky pixel whose history tap was covered within the last K frames is current-only** (band
widened in time). Loses because "covered" is not where the share enters: a covered-then-uncovered
pixel's tap lands on a geometry texel and strict already rejects it (`A prev-depth geometry 0`
among the dark trail). Keyed on coverage it fires under pans: a rotation moves sky content out
from behind every world-static silhouette, and K current-only frames there is a sparkle trail
behind every hull under every pan (invariant 1). Also needs the countdown cost of (B).

**(D) Previous-depth 3x3 instead of a memory** ("band last frame" read from s3 around the tap).
Same semantics as the one bit, works on every variant, but 8-9 extra `texld` per pixel, over
budget everywhere and the most expensive thing the resolve does. Noted as the only route for a
non-age variant, should one ever need the term.

Lowering the band threshold and post-processing were ruled out by the brief's evidence (the
entering share is at 0.25-3 px/frame, below any usable threshold).

## 6. Verification (as run; `docs/verification/temporal-resolve.md`, "2026-09-22 exit reset")

**Fixture** (`run_temporal_pass.py`, edge case (l) "exit reset"): an 8x8 square of colour
(0, 0, 0.5) at depth 0.9997, routed, static for 16 frames, then +0.5 px/frame for 20 (motion =
the exact displacement, camera static: parallax 0.5) over a sky whose texel at (x, y) on frame n
is 1 when `(x + y + n) % 3 == 0` and 0 otherwise, colour only. Every 3x3 holds all three
residues, so the clip box is [0, 1] on every sky pixel and never pulls a dark history (the run249
mechanism), and a sky-only history keeps B == R exactly: any hull share shows as the "cast"
B - R > 0. Trail: a strict-sky pixel that was in the band on the previous moving frame,
followed while it stays strict sky. Programs: the age program (thin clip 0.7, WMAX 0.9), the
far program (0.985 under the far cases' gate) and far_camera (thin region 0.97, camera gate: the
flown configuration). Per generation, strict + band 3, exit 0.25 against strict alone:

| row | exit off | exit 0.25 | asserted |
| --- | --- | --- | --- |
| fresh trail pixels current-only on their first strict-sky frame (age program, straight) | 0 of 204 | **204 of 204** | == fresh count |
| trail cast max (age, straight) | 0.118408 | **0.000000** | 0 |
| negative ages: total / on static frames / off the band / band blend pixels left positive | 0 / 0 / 0 / 720 | **720** / 0 / 0 / **0** | static + off-band 0, positive 0 |
| hull pixels reading their own marked texel / continuing the count / unexplained | 0 | 162 / 162 / **0** | unexplained 0, own >= 1 |
| 0.125 px/frame (below the floor): negative ages | | **0** | 0 |
| yaw +0.3 px/frame (f = 0.7): fresh current-only, trail cast | 0 of 242, 0.112305 | **242 of 242**, 0.006714 | fresh == count |
| yaw -0.3 px/frame (f = 0.3): fresh current-only, trail cast | 48 of 205, 0.063965 | **205 of 205**, 0.051392 | fresh == count |
| far program (0.985 under the far cases' gate), straight / yaw +0.3 / yaw -0.3: fresh current-only, trail cast | (the off side is the age rows') | 204 of 204 / 242 of 242 / 205 of 205; **0.000000** / 0.018555 / 0.087769 | same assertions as the age rows |
| far_camera program (thin region 0.97, camera gate: the flown configuration), the same three | | 204 of 204 / 242 of 242 / 205 of 205; **0.000000** / 0.018188 / 0.086670; 720 marks each, hull unexplained 0 | same |
| control sky (8+ px from the square) | identical in both modes | | diff 0 |
| loose with exit 0.25 uploaded (pass forces 1e30) | | output diff 0, age diff 0, negatives 0 against loose without it | 0 |
| (f) slow sweep, (g) static ring, (h) pan, (i) fade-band strip, (j) projective pan, (k) huge camera path with the exit on | **not run**: these cases use the plain program (no age target), which does not compile the term; their off-path rows are asserted unchanged (all 416 baseline SAMPLE lines identical, `strict - loose == 0` on (g), (h), (i), (j), the band `adjacent_max` 0.000000) and the exit-on behaviour under a pan is covered by the yaw rows above (parallax 0 on the sky, 0.5 on the band) | | |
| `RESOLVE_BUDGET` | age 495, age_filter 507, age_line 507, far 498, far_camera 510 <= 512; the other variants' slots and bytes unchanged | | |

The fixture asserts each on row: fresh_current_only == fresh_px, negative_static +
negative_not_band 0, band_blend_positive 0, hull_mark_unexplained 0; trail cast 0 on the
straight rows; and the reserve-only run (committed fixture, HEAD shader plus the two rewrites)
was bit-identical to the committed reports on every stable line before the term went in.

**Flight** (a strict + band 3 + exit 0.25 run with a SETA approach burst and a pan burst):
`verification/results/run249-band/dark_vs_own_mean.py <dir> <first>`: d3-12 "below own mean"
share from 27 % / 34 % to the far tail's 16-18 %, d>=13 unchanged at 16-18 %;
`mid_band_series.py`: d>=13 CV / lag-1 / `taa/mean8` unchanged (ordinary accumulation);
`band_parallax_fine.py`: dark d1 counts unchanged (the band is not touched); the pan burst's
ring table (seta-motion.md section 4: current-only fraction 0.001 / 0.066 / 0.073 at distance
1 / 2 / 3, flicker std 0.72 / 1.08 / 0.93) unchanged within noise; and, new, the current-only
fraction at distance 2 beside sub-threshold moving edges during the SETA leg, the reset's own
footprint, which the user judges as sparkle or not. With `--taa-debug` the age readback's
negative count per frame is the mark's footprint directly.

## 7. Unknown, and what settles it

1. **Answered.** Exact slot cost: +1 / +1 / 0 / +3 / +3 on age / age_filter / age_line / far /
   far_camera (495 / 507 / 507 / 498 / 510 of 512) after the two exact rewrites of section 4;
   the `step(0.5, f)` reserve saved nothing with this D3DX.
2. Whether repeated resets on the 2 px ring beside a slowly moving edge read as sparkle in
   normal flight (nearby stations at 0.25-1 px/frame of parallax under translation are marked
   too): only the flight; the fallback is (B)'s `keep = 0.5` form with the same bit.
3. **Answered, with a residual.** The reset itself is unchanged under a 0.3 px/frame yaw (the
   nearest age texel is the pixel's own; every fresh trail pixel is current-only, both signs),
   but the fractional Catmull-Rom footprint re-imports the band's share: trail cast 0.0067
   against 0.112 without the exit when the footprint's -0.07 lobe reaches the band (content
   moving away from the trail, f = 0.7), **0.051 against 0.064** when its 0.29 lobe does
   (content moving toward the trail, f = 0.3); on the far and far_camera programs, whose sky
   weight is the base 0.9 without the adaptive ramp, 0.019 and **0.088** (against 0.112 / 0.064
   without the exit). The share enters through the neighbours' history,
   so (B)'s `keep = 0.5` does not address it; a per-tap mark read would cost 16 taps. The SETA
   leg itself has rotation 0 (run249 `mid_band_chain`), so the straight row is the flown case; a
   turning leg keeps a residual of up to 80 % of today's on the side facing the motion.
4. The floor (0.25 against 0.5): the fine histogram sizes it; the flight A/B is the launcher
   option itself.
