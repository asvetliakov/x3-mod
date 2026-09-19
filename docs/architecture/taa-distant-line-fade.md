# Distant thin-line shimmer ("problem 2"): node fade rejected, far-gated resolve recommended

Status: design note for ratification, 2026-09-19. Nothing here is implemented. Tags: **[M]** measured this
session, **[I]** inferred, **[A]** assumed. Supersedes section 5 of [taa-lattice-crawl.md](taa-lattice-crawl.md)
(per-node fade band on the cull trampoline). Context: [taa-flicker-suppression.md](taa-flicker-suppression.md)
(rejected levers, replay oracle), [cull-small-parts.md](../verification/cull-small-parts.md).

Inputs: `/tmp/x3-bottleX3-run153` frames 7741-7772 (baseline, ship stopped, distant station), `run154` frames
13111-13142 (same station, `--taa-thin-clip 0.75 --taa-adaptive-weight 0.97 --taa-alpha-history`), `run142`
frames 10840-10871 (busy sector, far stations, slow drift). 1280x768 `--taa-debug`. Scripts (untracked): session
scratchpad `p2/` (`survey.py`, `bands.py`, `hot.py`, `draws.py`, `met.py`, `metmc.py`) and `crawl/replay2.py`
(= `tools/analysis/taa_resolve_replay.py` plus a `crawl` mode that saves per-config frames). Unit: codes of
display-relative luma, presented 8-bit luma where stated.

## 1. Decision

1. **Do not build the per-node fade band for this problem.** The size metric at `0x0047d2a2` is per node, and the
   shimmering station is **one node**: run153 frame 7750, node `222dafe0`, model `53b8`, LOD 0, 34 draws,
   73 749 primitives, origin distance 78 759, about 200 x 220 px on screen (12 300 covered px) **[M, capture log `object_context` +
   `object_matrix` + `draw`]**. Its `s = r*640/D` is tens of pixels; no band near the 2 px cull threshold ever
   sees it. In run142 every far station is likewise one node (11-38 draws, 15 284-170 744 primitives, D 65 000-
   205 000) or a one-draw LOD 3 body **[M]**. The shimmering lines are sub-pixel facets inside a large node.
2. **Recommended: a far-gated stabiliser in the resolve and the sharpen** (section 4): on pixels whose view
   distance makes one pixel wider than a threshold in world units, (a) history weight up to 0.985 with a wide
   speed gate, (b) Gaussian current sample A = 1, (c) no sharpen. No per-draw work, no new hook, no diagnostic
   flight. Replay promise: static flicker x 0.15-0.19 (resolved), slow drift x 0.45-0.55; the sharpen removal
   comes on top (x 0.7 **[I]** from the measured 0.88 -> 1.24 station gain of RCAS).
3. If the flight still shows static shimmer, the next step is a **jitter-cycle box mean** for far pixels
   (section 6): the capture proves an 8-frame mean nulls the static ripple exactly.

## 2. What the shimmer is [M]

Run153 station crop (670,290)-(870,510), presented frames, 32 frames:

- The scene is exactly static and deterministic: routed motion median 0.000 px/frame, and raw HDR values of
  frame `f` and `f + 8` (same jitter phase) differ by 0.0000 relative. The 8-32 frame band is 0.00 codes in every
  pixel class. All flicker is the 8-phase jitter ripple passed by the exponential history.
- Where the energy is (share of presented temporal variance on geometry): flip px (depth validity toggles) 0.089,
  stable px with sky in the 3x3 0.145, **interior (3x3 geometry in all 32 frames) 0.765**. With the adaptive
  weight (run154) the interior share is 0.844. The `thin` mask and every sky/sentinel-based detector miss
  three quarters of it; "fade toward the sky" is the wrong target, because the background of these lines is
  the station's own hull.
- It is concentrated: 1.3 % of interior px hold 50 % of the interior energy, 10.8 % hold 90 %. The top 5 %
  (489 px) have presented luma 88 against a 5x5 median of 32.
- Those hot px are binary in the raw frame: per-phase HDR luma min 0.065, max 0.98 (median ratio 27), 42 % peak
  above 1.0; largest gap / range median 0.58. Resolved relative std 0.142 against raw 0.871: gain 0.16, the
  closed-form period-8 gain of w = 0.9 is 0.136. Relative view-z temporal std of hot px equals the interior's
  (0.0010 vs 0.0008): no depth discontinuity, so a depth-edge detector does not find them either.
- Not texture minification **[M for one draw, I for the rest]**: the hull samplers of the station draw inspected (frame 7750, index 100) are `MINFILTER` anisotropic (3),
  `MIPFILTER` linear (2), `MAXANISOTROPY` 16 on 256x256 textures with 9 levels. **[I]** sub-pixel lit facets
  (rim rings, ribs) of a 73 749-triangle LOD 0 mesh drawn into about 12 300 px, brightened by the hull
  emission / light-map gains (authored-glow alpha on hot px 0.128 against 0.005 interior mean).
- The flown adaptive weight did what the replay said: presented interior std 2.83 -> 0.96 (x 0.34, run153 vs
  run154), replay w 0.97 x 0.37. The user still sees shimmer at that level, so the target is well below x 0.34.
  While moving, run154's speed gate (LO 0.1, HI 0.5 px/frame) returns the weight to 0.9: no effect, as reported.

## 3. Mechanism (1), the node fade band: why it loses

- Association and cost exist: the route samples the node of every routed draw (`MotionOutput::sample_scope`,
  `key.node` from `object_trace::current`), so a `(node, f)` table written by the stub could be probed per
  routed draw while non-empty; state changes only on faded draws. The stub would grow by a band compare and a
  table append (integer only, render thread).
- It cannot reach the target (section 1.1). What it would fade is compact 2-4 px parts: 31 nodes / 55 draws
  between 2 and 4 px and 11 / 21 between 4 and 8 px in the run131 census replay (97 / 128 / 139 nodes at 2 / 4 /
  8 px, cull-small-parts.md); run 43 B reported no visible pop-in from them.
- Opaque hull draws have no correct "toward background" by alpha: the background of an interior part is hull
  that may be drawn later (Z written by the faded part first), and RT1/RT2 would need
  `D3DPMISCCAPS_INDEPENDENTWRITEMASKS`. The stub also runs in shadow and env views with the main view's `m00`.
- Verdict: a per-draw probe and a blend-state path for a population that is not the complaint. Keep the pop;
  revisit only if pop-in is ever reported.

## 4. Recommended design: far-gated stabiliser

**Mask.** `farw = saturate((d - d0) * inv)` on the centre pixel's RT2 device depth `d`, 0 for the sentinel.
`d0`, `inv` are computed on the CPU each frame from the latched projection so that the gate is a pixel
footprint in world units: `z = -p32 / (p22 - d)`, footprint `= 2 z / (p00 * width)`; `farw` rises from 0 at
footprint `F0` to 1 at `F1`. Measured: projection rows give `p22 = 1.000003`, `p32 = -6.000018`; the inversion
puts the run153 station at z 65 710-76 706 (logged origin distance 78 759) and footprint 128-150 units/px; the
run142 station (model `53b8`, D 147 038) at 275; the near plant of run153 frame 4960 spans 46-148 **[M]**.
Float32 depth resolves about 60 units at z = 78 000 **[I]**, enough for a smooth gate. The footprint shrinks
with resolution, so at 3840 the same station gets a third of the treatment, which is the right direction.

**Action on `farw > 0`.**
- History weight `w = lerp(w_base, min(n / (n + 1), W_FAR), farw * (1 - saturate((speed - 0.5) / 1.5)))`,
  `W_FAR` 0.985 (the age target saturates at 64, i.e. 0.9846). Reuses the age pair of `--taa-adaptive-weight`.
- Current sample `lerp(centre, gaussian3x3(A = 1), farw)`; the nine taps are already fetched, the nine weights
  become per-frame constants.
- Sharpen lobe `*= 1 - farw`: one RT2 fetch at the centre in `taa_sharpen_ps` / `agx_sharpen_ps` and the same
  two constants.

**Replay on the real dumps** (whole crop = `farw 1`; resolved codes; frames 12-31 of a 31-frame free run seeded
with the installed history; "hot" = top 5 % interior px by baseline std):

| capture | config | hot px std | interior rms | interior px > 2 codes | hot mean luma |
|---|---|---|---|---|---|
| run153 static | base | 6.63 | 2.06 | 1415 | 83.7 |
| | w 0.97 | 2.28 (x 0.34) | 0.75 (x 0.37) | 232 | x 0.96 |
| | w 0.985 | 1.27 (x 0.19) | 0.48 (x 0.23) | 68 | x 0.95 |
| | A = 1 | 2.85 (x 0.43) | 1.21 (x 0.58) | 645 | x 0.90 |
| | A = 1 + w 0.97 | 1.50 (x 0.23) | 0.83 (x 0.40) | 359 | x 0.91 |
| | **A = 1 + w 0.985** | **0.97 (x 0.15)** | 0.60 (x 0.29) | 109 | x 0.92 |
| | A = 0.5 + w 0.985 | 1.12 (x 0.17) | 0.71 (x 0.34) | 243 | x 0.91 |
| run142 drift 0.039 px/frame, motion-compensated | base | 11.06 | 4.15 | 1155 | 63.4 |
| | w 0.97 | 7.35 (x 0.66) | 2.70 (x 0.65) | 641 | x 0.97 |
| | w 0.985 | 6.05 (x 0.55) | 2.42 (x 0.58) | 523 | x 0.96 |
| | A = 1 | 5.91 (x 0.53) | 2.32 (x 0.56) | 569 | x 0.95 |
| | **A = 1 + w 0.985** | **4.95 (x 0.45)** | 2.19 (x 0.53) | 465 | x 0.94 |

The weighted rows carry a start-up transient (the free run starts from a w = 0.9 history), so their mean-image
gradient figures are not reported; A = 1 alone keeps 0.43 of the interior gradient energy, the known cost of
that filter, here confined to `farw`. The run142 compensation is one global translation from the RT1 median
(sign checked: the other sign doubles the baseline); residual rotation inflates every row **[I]**, ratios are
the content. Under drift the remainder is phase-dependent line shape (roping, lattice note section 3), which no
weight removes; the filter is what acts there, so both terms are needed.

**Hot path.** CPU: nothing per draw; per frame two more constant registers and the nine Gaussian weights. GPU:
resolve +0 fetches, about +10 slots; sharpen +1 fetch. The age + filter variant is at 506 of 512 slots today:
the far variant must replace the adaptive weight's own LO/HI gate arithmetic with the far gate (one gate, not
two) to fit; the fixture's `RESOLVE_BUDGET` line decides. VRAM: the age pair, 8 B/px, as today's option.
**Native Windows.** ps_3_0 within 512 slots, constants, our RT2 contract, R32F target and
`D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`, all already prerequisites of the pass; nothing Wine-specific. The
projection comes from the documented camera latch; a frame without a valid projection uploads `inv = 0` (mask
off). Native behaviour stays unverified.

## 5. Alternatives considered

- **Node fade band**: section 3. Wrong granularity, measured.
- **General thin-feature detector in the resolve** (sky/sentinel `thin` mask, lattice mask, sky-neighbour
  attenuation): 76-84 % of the energy is interior without sentinel or depth step; the lattice note measured -1
  to -4 % for the attenuation. A colour-contrast detector is the biased `|current - history|` family already
  rejected. Distance is the only unbiased per-pixel signal our targets carry, hence the footprint gate.

## 6. Unknown, and what settles it

- Whether x 0.15 static / x 0.45 drifting reads as "gone": the flight. If static shimmer remains, a far-only
  **cycle box mean** (accumulate the eight phases into a second FP16 history and publish only completed
  cycles) is exact for a static scene, since the raw input repeats with period 8 to 0.0000 **[M]**; cost one
  more FP16 pair and a second reprojection, unmeasured under motion. Design only after the flight.
- LOD as a source-side lever: station `53b8` is drawn at LOD 0 (73 749 triangles) into about 12 300 px.
  `--lod-scale` accepts 1..4 only; a factor below 1 would bring LOD 1 in earlier. Whether LOD 1 drops the lit
  sub-pixel facets is unknown; a model-file read of `53b8` LOD 1 (draw and triangle counts, as in
  lod-selection.md "Cost") settles whether it is worth a range change.
- `F0` / `F1`: 90 / 150 units/px **[A]** puts the run153 station at `farw` 0.6-1 and the near half of the plant
  at 0; the far end of a large near structure is softened too. The replay with the real gate on the run153
  plant crop (frames 4948-4979) gives the cost before any shader work.
- Blinking lights on far stations: at w 0.985 a step reaches 31 % in 25 frames (53 % at 0.97) **[I]**; judged
  in the flight, remedy `W_FAR` 0.97.
- Slot count of the far variant and the GPU time of the pass: the temporal fixture.

## 7. Diagnostic capture

None needed. The draw-index target of the lattice note's step 6 is not required for this problem: capture
frames already log node, parent, model, LOD, world/view/projection and sampler state per draw, which answered
the ownership question at the granularity the cull site works at. The next flight is an acceptance flight.

## 8. Plan for one implement agent (Fable), default off until flown

1. **Replay first** (`tools/analysis/taa_resolve_replay.py`): option `far=(F0, F1, W_FAR, A)` with the depth
   inversion and the speed gate of section 4, hot-px / interior metrics of `met.py`; reproduce the table within
   5 %, then run the gated form on the run153 plant crop and report the interior gradient energy by `farw` bin.
2. **Shader**: `X3M_FAR_STABILIZE` variant of `resolve.hlsl` on the age variant (one gate), far term in the two
   sharpen programs; `TemporalPass::FrameInputs` gets `far_d0`, `far_inv`, `far_weight`, `far_filter`; the
   proxy computes `d0` / `inv` from the camera latch and the back-buffer width per frame.
3. **Fixture** (`run_temporal_pass.py`, lattice mode): new case "interior facets": 0.3-px lines of contrast 27
   over valid-depth geometry (no sentinel) at depth 0.99992, static and 0.04 px/frame, 256 frames. Gates:
   shader = CPU oracle within the FP16 bound; static hot ripple <= 0.25 x base; drifting motion-compensated
   <= 0.6 x base; every pixel with `d <= d0` bit-identical to the age variant, and with the option off to the
   plain program (hashes unchanged); `farw` monotone across a depth ramp; invalid projection -> mask off; slots
   <= 512; age/Reset/lifetime checks of the adaptive weight rerun unchanged; sharpen output bit-identical at
   `farw = 0`.
4. **Launcher**: `--taa-far-stabilize W[,A[,F0,F1]]` (`X3M_TAA_FAR_STABILIZE`), defaults `0.985,1,90,150`,
   implies the age target, values on the `motion_output_taa` line; `--taa-debug` needs nothing new (mask is a
   function of the depth dump).
5. **Flight**: the run153 station stopped, then approaching it and drifting past; off / on / on with A = 0;
   one 32-frame F8 each. Judged on shimmer, far blinking lights, softness of the plant's far end.

Risks: softened far ends of large near objects; slower far lights; a far ship crossing a far station ghosts
longer (both depths inside the far-plane tolerance, clamp-bounded); slot budget; the gate follows the main
view's projection only (the resolve runs on the main view, so shadow and env passes are not involved).

## Ratification (orchestrator, 2026-09-19)

Ratified with one constraint. The per-node fade band is dropped (the far station is one
node). The far-gated resolve stabiliser is accepted, but run 44 B had the user reject the
global current filter because it blurred distant stations, so the three components
(far history weight, far current filter A, far sharpen removal) must be separately
switchable from the launcher and the flight compares weight-only against weight + filter.
Implementation follows the lattice masked filter in the same worktree (one writer for the
resolve programs); plan step 1 (near-plant cost of the gate by replay) comes first. This
note supersedes section 5 and plan steps 5-6 of [taa-lattice-crawl.md](taa-lattice-crawl.md).

## 9. Implemented, unflown (2026-09-19): `--taa-far-stabiliser W[,A[,F0,F1]]`

(The fixed 0.5-2 px/frame speed gate of this section and of section 4 is superseded by section 10: `LO,HI` = 0.03,0.25 by
default and settable. The replay rows below were measured with the 0.5-2 gate.)

**Plan step 1, replay [M].** `tools/analysis/taa_resolve_replay.py <dump> <box> far` (`FAR=F0,F1`, `ONLY=<configs>`): the
shader-form gate `farw = saturate((d - d0) * inv)` from the footprint inversion (8-bit quantised as the mask stores it is
not modelled; p22 / p32 are the section-4 constants), the far weight with its 0.5-2 px/frame speed gate, the far filter,
and the presented stage through the real AgX + RCAS with the sharpen lobe scaled by `1 - farw`. Whole-crop gate (`FAR=0,0`)
on run153 station reproduces section 4: hot std x 0.19 / 0.45 / 0.14 (w 0.985 / A = 1 / both; note 0.19 / 0.43 / 0.15),
interior px > 2 codes 68 / 642 / 109 (note 68 / 645 / 109). Presented stage, frames 12-31, hot = top 5 % interior px,
gradient = per-frame squared x-gradient on interior px against the base:

| capture (box; footprint units/px) | gate | farw on interior | weight 0.985: hot / interior / gradient | filter A = 1 | weight + filter | sharpen off alone | all three |
|---|---|---|---|---|---|---|---|
| run153 station 7741-7772 (670 290 870 510; 128-230) | 90,150 | 0.5-1 | x 0.29 / 0.33 / 0.958 | x 0.46 / 0.56 / 0.503 | x 0.19 / 0.32 / 0.712 | x 0.99 / 0.99 / 0.971 | x 0.19 / 0.32 / 0.682 |
| | **80,130** | 93 % at 1 | x 0.19 / 0.25 / 0.957 | | x 0.14 / 0.29 / 0.748 | | x 0.14 / 0.28 / 0.715 |
| | 110,170 | 0-0.9 | x 0.54 / 0.55 / 0.969 | | x 0.35 / 0.42 / 0.733 | | x 0.35 / 0.41 / 0.713 |
| run142 station 10840-10871, drift 0.040 px/frame, motion-compensated (620 290 780 440; 180-340) | 90,150 | 80 % at 1 | x 0.55 / 0.61 / 0.797 | x 0.52 / 0.57 / 0.530 | x 0.43 / 0.54 / 0.682 | x 0.97 / 0.97 / 0.938 | x 0.42 / 0.51 / 0.630 |
| run153 plant, far end 4948-4979 (552 222 712 382; 107-155) | 90,150 | 0.3-1 | x 0.54 / 0.57 / 0.988 | x 0.59 / 0.64 / 0.622 | x 0.37 / 0.48 / 0.722 | x 0.98 / 0.98 / 0.975 | x 0.36 / 0.47 / 0.693 |
| | **80,130** | 0.5-1 | x 0.35 / 0.39 / 0.980 | | x 0.24 / 0.41 / 0.694 | | x 0.23 / 0.41 / 0.653 |
| | 110,170 | 0-0.5 | x 0.84 / 0.86 / 0.997 | | x 0.76 / 0.80 / 0.891 | | x 0.76 / 0.80 / 0.882 |
| run153 plant, near end (1100 336 1260 496; 25-34) | 90,150 | 0 | x 1.00 / 1.00 / 1.000 | | x 1.00 / 1.00 / 1.000 | | |
| run148 plant lattice, drift 0.45 px/frame (740 360 880 490; about 90-120) | 90,150 | 0-0.5 | x 1.00 / 1.00 / 0.985 | x 1.00 / 0.99 / 0.972 | x 1.00 / 0.99 / 0.967 | x 1.00 / 0.99 / 0.981 | x 1.00 / 0.99 / 0.952 |

Findings. (1) **Distance cannot separate the run153 station (128-150 units/px) from the far end of the near plant
(107-155)**: any gate that treats the station fully treats that end too. (2) The cost sits entirely in the filter: the
weight alone keeps 0.96-0.99 of the gradient energy everywhere (0.80 on the drifting run142 station, where the base
image itself is unstable), the filter 0.50-0.62 where `farw` is high. So the gate can be generous for the weight:
**F0, F1 = 80, 130** gives the station the whole-crop effect (x 0.19 weight only, x 0.14 with the filter) at a 2 % gradient
cost on the plant's far end; with the filter that end keeps 0.69. (3) **The far sharpen removal is worth 1-3 %** (x 0.97-0.99
alone, x 0.19 -> 0.19 and x 0.43 -> 0.42 on top of the others; section 1's x 0.7 [I] is contradicted by the real operators,
as the lattice note's section 8 found for the sharpen exclusion). It would need variants of three sharpen sites
(`TemporalPass`, `HdrPass`, the bloom staging sharpen) and a mask hand-over to two other modules; **not implemented**,
for the orchestrator to confirm. (4) At 0.45 px/frame (run148) neither component acts: the speed gate and low `farw`.

**Shader / pass.** `resolve_far.hlsl` (`X3M_FAR_STABILIZE`, **508 slots**): the age variant whose LO / HI gate arithmetic
is replaced by `keep += g * (1 - saturate((speed - LO) / (HI - LO))) * (min(n / (n + 1), W) - keep)` and whose current sample
is `weighted += r * (filtered - weighted)`; `r` and `g` come from the mask at `s8`, so the gate costs the resolve no
depth arithmetic. `line_mask_ps.hlsl` (153 slots) now writes `r` = filter weight (dilated line mask and / or `farw`), `g` =
`farw` for the weight, each multiplied by its component switch (`c5`: d0, inv, filter on, weight on); far alone is one
mask draw (mode 2), with the line filter two. `farw` is stored in 8 bits (steps of 1/255). Thin clip and alpha history
work inside the far program; the adaptive weight and the global current filter are refused beside it, and a line filter
must use the same A. Every other program's bytecode hash is unchanged (only the mask program, bound with these options
alone, changed). The gate is computed per frame by `x3::temporal::far_gate` from the camera latch (`CameraState` gained
`m22`, `m32`) and the target width; no valid latch: `inv = 0`, mask off, program still bound (no history cut).
Review findings of the line filter: (a) the nine `exp()` taps cost nothing measurable (global-filter program against
plain, 1280x768: -0.07 ms, noise); the line filter's cost is its two mask draws (+0.18 ms; far stabiliser +0.26 ms with the
age target), left as is; (b) a mask-target creation failure that is not a lost device now turns the line filter and the
far stabiliser off for the session (`motion_output_taa_masks` log line, once), the run proceeds with the plain / thin
program and the history stays valid.

**Fixture** (`temporal_far_inc.h`; lattice mode 316 numerical / 17 state): near / mid / far bands (farw 0 / 0.4 / 1) of
static geometry with 0.3-px facets of contrast 27, static and 0.04 px/frame, 256 frames. Shader = 2-D CPU oracle within
0.00025 (bound 0.04 at w 0.985), age target exact, published mask = quantised gate; **near pixels (farw 0) bit-identical to
the plain resolve in all four channels, 0 of 39 936 px-frames differing, asserted for the four far configs without a line
filter** (weight, filter, both, weight + soft clip 0.75; the soft clip is inert on a scene without sentinel); the weight +
filter + line row reports `near_px=0` because the line filter legitimately changes the near band's facets, so identity is
not expected there and that row is held to the oracle only; static far ripple x 0.154
(oracle 0.154), mid x 0.68; `far_gate` against the closed form, monotone over a depth ramp, eight invalid inputs refused;
`inv = 0` and the mask-creation fault are the plain resolve bit for bit with the history kept; refusals, hostile
c5 / c22 / c24 / s8 / COLORWRITEENABLE1, failed draw, Reset. Not gated: the drifting motion-compensated ratio of plan
step 3 (the fixture reports fixed-pixel ripple only; the run142 replay above is the drift evidence), and the filter's
ripple in this scene (a 0.3-px facet toggles whole rows; the filter's evidence is the replay).

**Flight.** `--taa-far-stabiliser 0.985` (weight only) against `--taa-far-stabiliser 0.985,1` (weight + filter), stopped
at the run153 station and drifting past; watch far blinking lights (remedy W 0.97) and the plant's far end.

**Gate precision.** For 80,130 at 1280 px the band is d0 = 0.99985647 to d1 = 0.99991286, 5.6e-5 wide: about 950 float32
depth steps (5.96e-8 below 1), so `d - d0` is exact and the binding quantisation is the mask's 8 bits (255 levels, weight
steps of 0.4 % of `W - w`). Rounding d0 and d1 to float32 moves `inv` by up to 0.1 % (fixture: 17 734.9 against 17 749.3);
`far_gate` refuses footprints whose two depths collapse in float32 (tested at 9e5,1e6), and wider targets shorten the band
in depth steps proportionally (3840 px: about 320).

## Ratification amendment (2026-09-19)

The far sharpen removal is dropped. Replayed through the real AgX + RCAS it is worth x 0.97-0.99 alone and nothing on top
of the other two components (run153 station x 0.19 -> x 0.19, run142 drifting station x 0.43 -> x 0.42), against variants of
three sharpen sites and a mask hand-over to two modules. The stabiliser is the far history weight and the far current
filter, separately switchable. Also decided in review: a mask-target allocation failure is sticky within the session
and re-armed once per Reset; the age pair allocated before such a fallback is left in place (releasing it would cut the
history); the far stabiliser requires per-pixel motion and a history weight above 0.

## 10. Flight verdict "works, but blurry when the camera moves" (run160 / run161), 2026-09-19 [M unless tagged]

Captures: run160 `0.985` (weight only), run161 `0.985,1`; 32 frames each; far pixels (footprint > 80) move 0.01-0.15
px/frame in run160 (median 0.09) and 0.03-1.4 in run161 (median 0.8); neither holds a fast turn. Tool:
`taa_resolve_replay.py <dump> <box> far` with `GATES=LO:HI,...`, `WS=`, `NOCLIP=1`, `KEYS=a,...` (weight-only rows per speed
gate / weight / clip removed / Keys cubic parameter of the history fetch on far pixels). Boxes: run160 `1100 140 1270 380`,
run161 `1100 236 1270 420`. Presented stage, W = 0.985, gate 80,130; "shimmer" = motion-compensated hot-px std, "gradient"
= per-frame gradient energy on interior px, both against the base resolve of the same capture:

| capture, far-px speed | config | shimmer | gradient |
|---|---|---|---|
| run153 station, 0 | gate 0.5-2 = gate 0.03-0.25 | x 0.19 | x 0.957 |
| run142 station, 0.04 px/frame | gate 0.5-2 (flown) | x 0.53 | x 0.793 |
| | **gate 0.03-0.25** | x 0.60 | x 0.809 |
| | gate 0.02-0.15 | x 0.68 | x 0.834 |
| run160, 0.085 px/frame | gate 0.5-2 (flown) | x 0.55 | x 0.775 |
| | gate 0.05-0.5 | x 0.59 | x 0.796 |
| | **gate 0.03-0.25** | x 0.70 | x 0.843 |
| | gate 0.02-0.15 | x 0.85 | x 0.911 |
| | W 0.97 / 0.95, gate 0.5-2 | x 0.62 / x 0.75 | x 0.814 / x 0.873 |
| | clip removed (W 0.985 / 0.97 / 0.95) | x 0.54 / 0.61 / 0.74 | x 0.790 / 0.826 / 0.885 |
| | Keys cubic a = -0.75 / -1.0 on far px | x 1.00 / x 1.24 | x 0.860 / x 0.895 |
| run161, 0.8 px/frame | gate 0.5-2 (flown) | (metric blind at this speed) | x 0.971 |
| | gate 0.25-1 / 0.15-0.5 and narrower | | x 0.994 / x 1.000 |

**Mechanism.** (a) dominates, in its slow form: the history fetch is already Catmull-Rom (16 taps), but at W = 0.985 a far
pixel is the product of about 65 consecutive resamples, and at 0.04-0.15 px/frame (a hand-held camera, a slow pan, a
drifting ship) every one of them is fractional. A quarter of the far gradient energy goes at 0.085 px/frame, where the
flown 0.5-2 gate still gives the full weight; static it is x 0.957 (that part is the removed flicker itself). (b) the wide
gate is what exposes it: at 0.8 px/frame the flown gate costs only 3 %, below 0.5 px/frame it never relaxes. (c) the clip
is not it: removing it changes the gradient by 1.5 points. A sharper history kernel is not a remedy: the feedback
amplifies what the jitter injects and the shimmer returns in full (a = -0.75: x 1.00) for half of the sharpness.
Shimmer removal and resampling blur are the same N_eff = 1 / (1 - W) at a given speed; the only free choice is where on
the speed axis the weight lets go.

**Implemented.** The far weight's speed gate is now narrow and adjustable: `--taa-far-stabiliser W[,A[,F0,F1[,LO,HI]]]`,
default `LO,HI = 0.03,0.25` px/frame (was the fixed 0.5-2): full W only while the far content is practically still, the
base weight from 0.25 px/frame. Replay promise against the flown build: still unchanged (x 0.19); 0.04 px/frame x 0.53 -> x 0.60;
0.085 px/frame shimmer x 0.55 -> x 0.70 for gradient x 0.775 -> x 0.843; from 0.25 px/frame the far pixels are the plain resolve
bit for bit. Constants only (`c24.zw`): **no shader changed, `resolve_far` stays at 508 slots**, so the Catmull-Rom / filter
restructuring was not needed; the far filter A stays accepted (the user flies weight-only). `FrameInputs::far_speed_lo/hi`
validated `0 <= LO < HI <= 64`. Fixture (lattice mode 375 numerical / 17 state): refusals of five invalid gates; facets
drifting 0 / 0.04 / 0.14 / 0.30 px/frame, shader = oracle at each, far-band ripple ratio 0.154 / 0.192 / 0.577 / 1.000
(monotone, continuous), and at 0.30 px/frame every pixel of the weight-only run bit-identical to the plain resolve; near
pixels bit-identical at every speed.

**Not done / unknown.** No capture of a real camera turn exists, so the speed at which the user sees the blur is inferred
from the replay's slow captures; if `0.03,0.25` still reads soft, `0.02,0.15` is the next step (x 0.85 shimmer at 0.085
px/frame), if shimmer returns while drifting, `0.05,0.5`. A lossless-enough resample (6-tap Lanczos on gated pixels, 36
taps) would move the whole curve but needs its own pass; not designed.


## 11. Light-map far fade: `--light-map-far-fade P0,P1[,G]` (2026-09-19, implemented, unflown, default off)

**Evidence (run168, `--taa-far-stabiliser 0.985`) [user report].** The thin-line shimmer of the distant station is almost
gone; what still shimmers is the glowing light-map windows, and Ctrl+Shift+F4 (light-map gain off) makes the station
"almost ideal". A window of a distant station is a sub-pixel emissive texel; at `--hull-lightmap-gain 4` it is a x4 HDR
point sample that the jitter switches on and off, and the far gate's variance clip cannot hold a value that is alone in
its neighbourhood.

**Law.** Per routed draw, `gain_eff = gain` for footprint `<= P0`, `G` for `>= P1`, linear in the footprint between
(`fade_route::lightmap_far_gain`, `src/proxy/fade_route_core.h`; continuous in distance, both ends returned exactly). The
footprint is section 4's measure, `2 z / (p00 * width)` world units per pixel, with `z` the object origin's view depth (the
clip `w` of the draw's own WVP rows, `rows[15]`), `p00` from the latched scene camera and `width` the route's target width.
`G` defaults to 1 (the game's own brightness) and may be anything in `[0, gain]`. No camera, or an origin at or behind the
camera plane (a large object around the viewer), keeps the configured gain. The option is its own camera-latch consumer
(`camera_state::initialize`, `MotionOutput::read_camera`), so it does not depend on `--taa`.

**Mechanism.** The gain was a shader-local `def c223` in the gained variants, which no upload can override. With the
option on, both gained variants (plain and sun-share; `linear_material.cpp`, `lightmap_dynamic`) are built without the DEF
and their one MUL reads `c217.w`, a lane of the motion ABI's per-draw mode vector that the motion fragment never reads
(it reads `c217.x` only). The route already uploads c216-c217 on every routed draw and restores them after it, so the
per-draw gain costs **no additional `SetPixelShaderConstantF`, no `Get*`, no allocation**: one division and a few
multiplies on draws whose pair has a gain variant, zero otherwise. With the option off the variants and the upload are
byte for byte the previous ones (`c217.w = 0`, DEF present). F4 selects or deselects the same variant as before; the
fade-band and overlay arms never bind a gained variant (unchanged); cutout pairs of the lane use the gained share variant
and get the same constant; Reset re-creates the variants through the same registration path; documented D3D9 only.

**Per draw, not per pixel.** One object gets one gain, from its origin. Consequences: (a) a big station seen close has
its origin at a small footprint (at width 1280 and p00 0.8, footprint 40 is z = 20.5 km), so it keeps the full gain over
its whole body; (b) the gain does not vary across a body that spans a depth range: a part 2 km deeper than its origin
differs from the per-pixel law by `3.9 / (P1 - P0) * (gain - G)`, 0.17 of 4 for `40,110,1`, invisible; (c) separately
drawn parts of one station step by the same small amount, never a seam inside a draw. A per-pixel version is affordable
in slots (largest gained variant 264 of 512 static slots; about +4 instructions and one more constant) but needs view
depth in the pixel program, which only the depth-exporting rows interpolate, and buys nothing at these distances.
**Not built.**

**Energy.** Only the light-map (self-illumination) term changes; lit hull, guide lights (F6 gain) and effects do not.
Beyond `P1` a lit station's windows are `G / gain` of their boosted radiance: 1/4 at the production gain 4 with `G = 1`
(two stops down, exactly the unmodded game's value), and their bloom feed falls by the same factor.

**Suggested first flight: `--light-map-far-fade 40,110`** [A]. The run153/run168 station sits at 128-150 units/px, past
`P1`, so its windows return to the game's brightness; the fade starts at 40 units/px where a 10-20 unit window is already
well under a pixel. Both numbers scale with resolution by construction (a wider target halves the footprint). Unknown
until flown: whether mid-distance stations (40-110) read too dim, and whether `G = 1` windows still shimmer under the
stabiliser (then try `G = 0.5`).

Verification: `docs/verification/motion-output.md`, "Light-map far fade".
