# TAA lattice crawl and distant sub-pixel shimmer

Status: design note for ratification, 2026-09-19. Nothing here is implemented. Tags: **[M]** measured this
session on the dumps, **[I]** inferred, **[A]** assumed. Two problems with different tolerances:
(1) the crawl on the Terran solar power plant's panel lattice (must be fixed); (2) shimmer of distant
sub-pixel features (dimming accepted). Earlier notes ([taa-flicker-suppression.md](taa-flicker-suppression.md)
10-10.1, [source-antialiasing.md](source-antialiasing.md)) treated both as one per-pixel temporal problem; this
note looks at (1) spatially and in the lattice's own (motion-compensated) frame.

Inputs: `/tmp/x3-bottleX3-run148` (bias -0.5, drift), `run149` (bias 0, drift), `run142` (static plant, frames
6140-6171; distant station, frames 10840-10871), `run146` (#1 crawls 4241-4248, #2 clean 6289-6296), all
1280x768 `--taa-debug`. Scripts (untracked): session scratchpad `crawl/` (`lat.py`, `rope*.py`, `mc.py`, `st.py`,
`metrics.py`, `replay2.py` = the worktree's `taa_resolve_replay.py` plus mask options). Metric unit: display-relative
luma codes `255 kL/(1+kL)` unless stated.

## 1. Decision

1. The crawl is **not texture moire and not a resolve defect**. The lattice lines are about 0.7 px thick
   **geometry** over a blended glass face; the raw frame renders them binary. What crawls is **roping**: the
   reconstructed profile of a sub-pixel line depends on its sub-pixel phase (one bright pixel when centred, two
   half-bright pixels when straddling), the phase repeats along a slanted line every `1/slope` px (beads), and
   the beads slide along the line when the lattice drifts across it. It is the one-dimensional moire of a line
   lattice against the pixel rows. The resolve removes the fast alias of the raw frame but keeps the roping, the
   sharpen enlarges it by about 1.6x, mip bias and the clip do nothing.
2. Fix for (1), two resolve-side steps on a **lattice mask** our targets already define (3x3 contains a pixel
   with RT1 motion alpha 1 and RT2 depth sentinel, i.e. routed blended glass, and a valid-depth pixel):
   **(a) no sharpen on the mask; (b) Gaussian-weighted current sample on the mask** (`X3M_CURRENT_FILTER`
   restricted by the mask, A = 2 first, A = 1 if still visible). Replay on run148: crawl energy 6.60 -> 2.87
   (A = 2) or 2.49 (A = 1) codes with both steps; gradient energy off the mask x 0.9998. The mip bias stays -0.5.
3. For (2): do not detect in the resolve (replayed: -1 to -4 %). Fade **per node by projected size** in a band
   above the `--cull-small-parts` threshold, toward the background (alpha), not toward black. It covers compact
   small nodes only; long thin antennas and lit windows need the draw-id capture of section 6 before a design.

## 2. What the lattice is [M]

- **Two layers.** In panel interiors RT1 says routed (alpha 1) on every pixel, but RT2 depth is the -1 sentinel
  on the dark cell area and valid only on the bright line pixels and struts (depth histogram of a 50x30 patch:
  960 sentinel, 540 valid at 0.999+, nothing between). The plant (model `54b3`, 16 instances per material) draws
  16 blended `zwrite=0` faces (`ps 64bac8bb`, SRCALPHA/INVSRCALPHA, 4 752 prims each; the "station fade pair"
  of run125) over opaque geometry; 80 of its draws are alpha-tested (mostly `ps 5e0a10fe`; 91 392 prims, 10.5 %).
- **Lines are binary in the raw frame.** Cell 58 codes, line 123-127, no intermediate values; a line is a
  one-pixel staircase (runs of 3 px shifting one row). Line excess 0.305 over a 0.135 background (linear).
  Thickness from the 32-frame static jitter mean (run142): integrated excess 0.224 / 0.305 = **0.73 px**.
- **Mip bias does not touch them.** Line-pixel share of panel interiors 0.2944 +- 0.0045 (run148, bias -0.5)
  vs 0.2912 +- 0.0063 (run149, bias 0); roping ratio 0.68 vs 0.66; crawl energy 4.07 vs 4.32. A thin line in a
  minified alpha channel tested at ref 1 would widen with a blurrier mip; this one does not move. **[I]**
  geometry (the opaque body draw has 100 624 vertices for 40 258 prims, i.e. many separate quads), not an
  alpha-tested texture line. Which draw owns the lines is not decidable from the dumps (section 6).
- **Pitch and orientation** (dominant lattice peak of the raw 2-D spectrum per 40-px panel window, `lat.py`):

| capture | pitch px | lines off the nearest pixel axis | stair (bead) period px |
|---|---|---|---|
| run148 (crawls) | 2.1-4.8 | 18-19 deg (two panels 28-31) | 3.0 (1.7-1.9) |
| run146 #1 (crawls) | 2.4-4.7 (one 6.0) | 13-14 deg (two panels 25-27) | 4.1-4.4 (2.0-2.2) |
| run146 #2 (clean) | 4.7-8.3, mostly 7.5-8.3 | 22-40 deg | 1.2-2.5 |

  The peak picker can land on a comb harmonic, so pitch is +-1 harmonic **[I]**; the line direction is robust.

## 3. Diagnosis of the crawl

**Raw frame [M].** In the lattice frame (frames Fourier-shifted by the panel velocity from RT1, 0.43-0.47 /
0.068 px/frame) the raw deviation peaks sit at `n k0 - g` with `k0 = (0.088, -0.266)`, `g = (0, 1)`:
(0.164, 0.477) 9.0 codes, (0.250, 0.273) 7.3, (0.250, 0.227) 5.5. Their temporal frequency is 0.438-0.458
cycles/frame = the Halton base-3 jitter rate 3/8 plus `g.v` = 0.375 + 0.068. The exponential history (w = 0.9)
passes that at about 0.05, and these peaks are absent from the resolved deviation. The 2nd harmonic of the
lattice, (0.176, -0.53), lies beyond vertical Nyquist and is displayed folded at (0.164, 0.477) in both raw
and resolve: a 3.8-px lattice of 0.7-px lines is not representable at this resolution without loss.

**Resolve [M].** What remains is phase-dependent line shape, measured on line crossings in cell interiors by
sub-pixel centroid offset (display-linear, centred < 0.1 px vs straddling > 0.4 px):

| image | peak centred | peak straddling | ratio | integrated ratio |
|---|---|---|---|---|
| run148 resolve (drift) | 0.0566 | 0.0392 | 0.69 | 0.93 |
| run148 presented frame | 0.1054 | 0.0701 | 0.67 | 1.06 |
| run142 resolve (static) | 0.1051 | 0.0616 | 0.59 | 1.03 |
| run142 presented frame | 0.1665 | 0.1153 | 0.69 | 1.21 |

Energy is conserved across phases; the peak is not (a box reconstruction would give 0.50, the Catmull-Rom
history already softens it to 0.6-0.7). In the presented frames the beads are visible as a 3-px-period
modulation along each line, 89-123 codes peak to trough on strong frames, pulsing with the jitter phase
(kymograph, `kymo.py`). Static, the beads are stuck to the surface and read as texture. Under drift each
lattice-frame pixel cycles through the phases at `g.v`: 0.068 cycles/frame = a 15-frame period, exactly the
"slow band" no earlier option could move. Motion-compensated temporal rms on 16-px panel tiles (velocity
refined per tile; 40-px tiles over-read by 2x from rotation, so tile size matters):

| capture | total | period 2-4 | 4-8 | 8+ frames |
|---|---|---|---|---|
| run142 static, resolve | 1.81 | 1.21 | 1.31 | 0.26 |
| run148 drift, resolve | 4.07 | 1.50 | 1.58 | 3.43 |
| run149 drift (bias 0), resolve | 4.32 | 1.59 | 1.63 | 3.67 |
| run148 raw input | 26.3 | 21.2 | 10.9 | 12.7 |

The slow band rises 13x from static to a 0.07 px/frame perpendicular drift. Drift also lowers line contrast to
0.54 of static (peak 0.0566 vs 0.1051): the history resample blurs what the roping does not.

**Why angle-dependent [M geometry, I perception].** Bead period along the line is `1/tan(angle off axis)` and
the beads slip against the surface at `v_perp / tan(angle)`. At 13-19 deg the beads are 3-4.4 px long,
coherent across lines whose pitch is about the same (a 2-D bead lattice), and slip 3-4x faster than the drift:
a creeping pattern. At 22-40 deg they are 1.2-2.5 px, below what the eye separates, and slip at about the drift
speed; the clean view also has twice the pitch (lines then about 1.5 px thick **[I]**, which need no sub-pixel
reconstruction). The slip speed is geometry, not a measurement: a cross-correlation of a bead envelope
(`bead.py`) locked onto the struts and returned the surface velocity, so the beads' own velocity is unisolated.

**Contribution of each stage (run148 panel, replay; crawl energy = motion-compensated total rms):**

| stage | evidence | effect |
|---|---|---|
| mip bias -0.5 | run149 A/B, section 2 | none (4.07 vs 4.32, different pose) |
| variance clip | clip removed | none (4.06) |
| history weight | 0.80 / 0.90 / 0.95 on the mask | 5.94 / 4.07 / 3.19: more history helps, at a blur cost under drift |
| 8-phase jitter | raw alias rotates at 0.44 cycles/frame | suppressed about 20x by w = 0.9 **[I, filter gain]**; absent from the resolved spectrum **[M]** |
| history resample | cannot be removed offline | contrast x 0.54 under drift; softens roping 0.50 -> 0.69 |
| sharpen (RCAS after AgX) | presented vs resolve: bead amplitude 0.035 vs 0.017 display-linear **[M, two tone curves]**; grey RCAS proxy on the replay: crawl 4.07 -> 6.60 **[M, proxy]** | x 1.6-2 |
| dilation / closest depth | lattice motion is RT1 on every pixel, disocclusion rejects 0 | none |

Jitter/velocity resonance **[I]**: a `g = (1, 0)` alias rotates at the base-2 rate 0.5 minus `v_x`; at run148's
0.45 px/frame that is 0.05 cycles/frame, which w = 0.9 passes at 0.32. It does not show here because these
lines are near-horizontal (all top raw peaks are `g = (0, 1)`), but a near-vertical lattice drifting at about
0.5 px/frame horizontally, or a near-horizontal one at about 0.37 vertically, would alias slowly. Worth one
fixture case (section 5 step 4).

## 4. Fix candidates for (1), ranked by replay on the real frames

Mask `lattice` = 3x3 has (RT2 sentinel and RT1 alpha 1) and 3x3 has valid depth. `thin` = the existing
`sawV & sawS`. A = exponent of the current-sample Gaussian `exp(-A d^2)` about the jittered sample position
(A = 2: sigma 0.5 px; A = 1: 0.71). Crawl with sharpen = grey RCAS proxy, mask pixels excluded where stated.

| # | candidate | crawl, resolve | crawl, with sharpen | roping ratio / bead amplitude | line peak | off-mask gradient energy |
|---|---|---|---|---|---|---|
| 0 | installed | 4.07 | 6.60 | 0.68 / 0.0196 | 0.0613 | 1 |
| 1 | no sharpen on the mask | 4.07 | 4.15 | unchanged in the resolve | 0.0613 | 1 (proxy) |
| 2 | 1 + current filter A = 2 on the mask | 2.74 | 2.87 | 0.79 / 0.0102 | 0.0476 | 0.9998 |
| 3 | 1 + current filter A = 1 on the mask | 2.34 | 2.49 | 0.86 / 0.0051 | 0.0370 | 0.9997 |
| 4 | filter A = 1 on the mask, sharpen kept | 2.34 | 4.04 | 1.00 / 0.0001 after proxy | 0.0401 | 0.9997 |
| 5 | 3 + w = 0.95 on the mask | 2.48 | not run | 0.91 / 0.0031 | 0.0358 | 0.9997 |
| 6 | w = 0.95 on the mask alone | 3.19 | not run | 0.77 / 0.0116 | 0.0509 | 1 |
| - | filter A = 1 everywhere (rejected by the user) | 2.03 | not run | 0.86 / 0.0051 | 0.0370 | 0.51 |
| - | filter A = 1 on `thin` | as 3 on the lattice | | | | silhouette ring x 0.51, all geometry x 0.93 (dome crop) |

Static floor 1.81. run149 repeats the ranking (4.32 / 3.10 / 2.72); on the static run142 the masked filter
lowers the jitter ripple too (fast band 1.21 -> 0.63 at A = 2).

**Recommendation: 1 then 2.** Step 1 costs the lattice nothing in the resolve and removes the largest single
multiplier; step 2 halves what is left for a 22 % lower line peak (A = 1: 40 %). The cost is confined to pixels
next to routed blended glass: on the dome crop the mask is 2.5 % of pixels and the silhouette ring keeps 0.85
of its gradient energy against 0.51 for `thin`, because sky has RT1 alpha -1. The line softening on the
lattice is the price; there is no reconstruction of a 0.7-px line that is both one pixel sharp and
phase-independent. Candidate 5's extra history adds nothing over 3. Orientation-aware filtering (Gaussian
across the line only, direction from a 3x3 structure tensor) would spare the perpendicular struts inside the
mask; keep it as a refinement if step 2 looks soft in flight **[A]**.

Losers: **per-draw LOD bias** (the lines are not texture; measured no effect), **wider clip window** (the clip
is inert here), **history weight/age alone** (-22 % for a drift blur), **`thin`-masked filter** (softens every
silhouette), **MSAA/SSAA** (closed in source-antialiasing.md).

**Hot path.** No per-draw CPU work; one more resolve variant and one more sharpen variant chosen per frame.
Resolve: the nine current taps are already fetched for the clip; the nine Gaussian weights depend only on the
frame's jitter, so upload them as constants instead of nine `exp`; the mask adds up to eight RT1 fetches (the
centre is already read). Sharpen: five RT2 plus five RT1 fetches on the cross, or one fetch if the resolve
publishes the mask (its alpha use must be checked first). Instruction and sampler budget is far below ps_3_0
limits. GPU cost is unmeasured; the temporal fixture's pass timing decides.
**Native Windows.** `tex2Dlod`, constants and our own RT1/RT2 contract only; nothing Wine-specific. The mask
depends on the route's rule that a `zwrite=0` blended routed draw writes RT1 alpha 1 and leaves RT2; that rule
must be stated in `hdr-scene-path.md` and pinned by the fixture, because the mask silently vanishes if it changes.

## 5. Problem (2): distant sub-pixel features

**Resolve-side detection loses [M].** Replay on the run142 station (3 927 flip px): attenuating a valid-depth
pixel toward the mean of its sky neighbours in proportion to the 3x3 valid count changes the flip-pixel bands
by -1 to -4 % (2.20 / 3.22 / 11.49 -> 2.12 / 3.13 / 11.35 at best). The flip pixels are mostly edges of larger
structure, not isolated lines. In that scene the sky (nebula, 62.7 codes) is **brighter** than the station
(41.1), so "dim toward black" would raise contrast: the fade has to go toward the background.

**Design: projected-size fade band on the cull trampoline.** `--cull-small-parts T` already has the node's
`s = r*640/D` at `0x0047d2a2` and sends `s < T` to the engine's cull, which pops. Add a band `T <= s < 2T` with
`f = (s - T) / T`: the trampoline writes `(node, f)` into a small per-frame table; the route, which already
knows the node of each draw, looks `f` up only when the table is non-empty and, for `f < 1`, binds the faded
variant: output alpha `f`, SRCALPHA/INVSRCALPHA, colour writes to RT1/RT2 masked off so the background's
motion and depth stand (`D3DPMISCCAPS_INDEPENDENTWRITEMASKS`; without the cap the node keeps today's pop).
Cost: one table probe per routed draw while a band node exists, state changes on faded draws only (a few
sub-4-px nodes per frame). Risks: an opaque faded part drawn before the hull behind it blends with sky and
still writes Z (sub-4-px error, **[A]** acceptable); the engine's own `+0x1d8` cull still pops; shadow and
env views must ignore `f`. Fixture: the census replay of cull-small-parts.md extended with band nodes (table
contents), and a seam case asserting alpha `f` and untouched RT1/RT2 under a faded draw.

**Not covered:** long antennas (bounding radius large, thickness sub-pixel) and sub-features of big nodes
(struts, lit windows). Lit windows are texture, so a selective LOD bias (0 or +0.5 on far nodes, by the same
`s`) is the natural lever, but no capture here isolates them **[A]**. Both need to know which draws own the
shimmering pixels first.

## 6. Plan

1. **Sharpen mask (offline + fixture).** Port AgX + RCAS to the replay to replace the grey proxy and confirm the
   1.6x on run148/run149; add the masked sharpen variant; temporal fixture case: lattice of 0.7-px lines at
   depth 0.999 over a routed no-depth face, 15 deg off axis, 0.07 px/frame perpendicular drift; assert bead
   amplitude and the motion-compensated slow band against the unmasked variant, and bit-identical output off
   the mask.
2. **Masked current filter, A = 2.** Same fixture; assert roping ratio >= 0.78 and off-mask output identical;
   record pass time. Replay numbers of section 4 are the expected values.
3. **Flight A:** steps 1 + 2 default on, A selectable; the plant at 13-19 deg, slow drift; judged on the crawl
   only. If visible, A = 1; if soft, the orientation-aware variant.
4. **Jitter resonance fixture:** near-vertical line lattice at 0.5 px/frame horizontal drift, slow band vs 0.3;
   only if it fails consider a longer or decorrelated sequence.
5. **Node fade band** (section 5) behind `--cull-small-parts`, fixture first, then a distant-station flight.
6. **The one capture to ask for:** a `--taa-debug` F8 with a **draw-index target** (routed draw index written
   to a spare float RT for the dumped frames) at the plant and at the distant station. It settles which draw
   owns the lattice lines (a per-draw fade of the lines by projected pitch becomes possible if they are their
   own draw) and which draws own the station's flip pixels, which the earlier ledger entry found undecidable.

## 7. Unknown

- AgX + RCAS exact gain on the beads: measured only through a grey proxy and through two different tone curves.
- Whether the lines are the opaque body draw or an alpha-tested draw: [I] geometry from bias invariance; step 6.
- Perceptual threshold: no number says at which crawl energy the user stops seeing it; 2.5-2.9 against a static
  1.8 is the replay's promise, the flight decides.
- The bead slip velocity is derived, not measured (section 3); the pulsing with jitter phase is measured.
- GPU cost of the two variants; alpha-channel use of the resolve output (mask hand-over to the sharpen).
- Motion compensation used a per-tile translation; rotation residue inflates every row of the crawl tables
  equally **[I]**, so ratios are safer than absolute values.
