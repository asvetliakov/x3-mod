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
