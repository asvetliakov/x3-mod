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

## 8. Plan step 1 measured with the real operators (2026-09-19) [M]

`tools/analysis/taa_resolve_replay.py <dump> <box> lattice` now carries the lattice mask, the masked current filter,
`agx.hlsl` (gamma 2.2 decode, look none, the frame's logged `ev_adapted`) and `rcas.hlsl` (gain `2^-0.75`, the runs'
`X3M_TAA_SHARPEN`), with the sharpen exclusion as "mask pixel = tonemapped centre tap". Bloom is not modelled; the
replayed AgX + RCAS differs from the dumped `present_1_*` by 0.45 (run148) / 0.42 (run142) codes mean abs on the crop.
Boxes: run148 `740 360 880 490`; run142 `800 410 940 540`, `FR=6140-6171`. The resolve column reproduces section 4
exactly (4.07 / 2.74 / 2.34, peaks 0.0613 / 0.0476 / 0.0370, off-mask gradient x 0.9998 / 0.9997). Display columns are
`255 * luma(display RGB)`, a different unit from the resolve's `255 kL/(1+kL)`; compare within a column.

| run | resolve filter | resolve | AgX only | AgX + RCAS | AgX + RCAS, mask excluded |
|---|---|---|---|---|---|
| 148 drift | installed | 4.07 | 6.57 | 7.46 | 6.69 |
| 148 drift | lattice A = 2 | 2.74 | 4.41 | 5.08 | 4.60 |
| 148 drift | lattice A = 1 | 2.34 | 3.74 | 4.31 | 3.96 |
| 142 static | installed | 1.81 | 2.85 | 3.09 | 2.89 |
| 142 static | lattice A = 2 | 1.08 | 1.70 | 1.90 | 1.82 |
| 142 static | lattice A = 1 | 1.20 | 1.90 | 2.19 | 2.04 |

**The grey proxy is contradicted.** The real sharpen multiplies the crawl by 1.14 (run148) / 1.08 (run142), not 1.6;
the rest of the resolve-to-presented rise (4.07 -> 6.57) is the AgX tone curve's slope at this level, i.e. a unit change
that no sharpen mask removes. Excluding the mask from the sharpen is worth -10 % (7.46 -> 6.69), not -37 %; the masked
current filter keeps its full effect through the real operators (x 0.68 at A = 2, x 0.58 at A = 1; both steps x 0.62 /
x 0.53 of the installed presented crawl). RCAS also raises the straddling peak more than the centred one (roping ratio
0.85 -> 0.93 at A = 1), so it is not purely harmful on the mask. Section 4's "with sharpen" column and candidate 1's
ranking are superseded by this table. **Decision (orchestrator): the sharpen exclusion is rejected**: about 10 % for two
more sharpen variants and a mask hand-over through the bloom staging path (`bloom_agx_ps.hlsl` -> FP16 staging ->
`taa_sharpen_ps.hlsl` when `X3M_HDR_BLOOM=1`). Only the masked current filter goes ahead (section 9).

## 9. Implemented, unflown (2026-09-19): `--taa-line-filter A[,W]`, general line mask

**Mask choice [M].** The section-4 lattice mask needs routed blended glass behind the lines, so it never fires on trusses,
antennas or the distant station (coverage 0.0000 there). Compared in replay (`MASKS=<names> ... lattice`; masks are 3x3
dilations `x` of a per-pixel test; `line1`: valid depth whose two opposite neighbours at distance 1 along h, v or a
diagonal are both background = sentinel or farther, `(1 - q) * 1.1 < 1 - d`; `line2`: distance 1 or 2; `hv`: no
diagonals; `d`: plus the dual gap test). Presented crawl = AgX + RCAS column, A = 2 / A = 1:

| capture | installed | lattice | line1x | line2x | linehvx | line1dx |
|---|---|---|---|---|---|---|
| run148 plant, drift | 7.46 | 5.08 / 4.31 | 5.09 / 4.32 | 4.77 / 3.83 | 5.41 / 4.79 | 4.84 / 3.95 |
| run142 plant, static | 3.09 | 1.90 / 2.19 | 1.79 / 2.17 | | | |
| run153 plant 4948-4979, static, lines 2-3 px (box 1040 440 1180 570) | 1.71 | 0.94 / 1.17 | 1.54 / 1.55 | 0.90 / 1.15 | | |
| run142 station flip-px bands, A = 1 (box 100 280 920 700) | 3.44 / 4.18 / 16.10 | unchanged (mask empty) | 3.26 / 4.00 / 15.14 | 3.13 / 3.85 / 14.47 | 3.31 / 4.05 / 15.44 | 3.19 / 3.93 / 14.90 |
| station big-object edge gradient, A = 1 | 1 | 1 | 0.986 | 0.956 | 0.993 | 0.948 |
| station stable-geometry gradient, A = 1 | 1 | 1 | 0.996 | 0.983 | 0.998 | 0.995 |

line1x covers 93 / 94 % of the lattice mask on run148 / run142 with the same line peak (0.0476 / 0.0370 resolve) and
is within 1 % of it on the 0.7-px lattice; on the closer run153 view (lines 2-3 px, crawl already below the static floor)
it covers 26 % and only line2x matches. `thin` on the station: bands -14 % but edge gradient x 0.65. Dropping the
diagonals costs 6-11 % on the plant. Convex corners of big objects are line-like along the diagonal across them (the
1-5 % edge-gradient loss above). **Chosen: line1x by default, line2x selectable (`W = 2`)**; depth target only, so the
route rule "blended zwrite=0 routed draw writes motion alpha 1 and no depth" is not relied on and is not pinned.
run153's second capture (5345-5376) gave crawl 20 codes on my box (misregistered tiles, not comparable); run146 has no
`taa_1` dumps and run154 was captured with options the lattice mode does not replay; both skipped.

**Shader.** Inline the mask cost 73 slots (plain line variant 517, over 512), so it is its own program
`line_mask_ps.hlsl` (125 slots), drawn twice by `TemporalPass` into two owned A8R8G8B8 targets (line-like, then 3x3
maximum; 8 bytes/px, created on the first line-filtered run, released with the histories) and read by the resolve at
`s8` (one fetch). Variants `resolve_line` / `resolve_thin_line` / `resolve_age_line`: **446 / 483 / 509 slots**; A in
`c22.w`, width in the mask program's `c7.w`. The existing programs' bytecode hashes are unchanged (433 / 444 / 46 / 468 /
480 / 494 / 506). Refused beside `--taa-current-filter > 0`. Native Windows: `tex2Dlod`, A8R8G8B8 render targets, sampler
8, constants; nothing backend-specific.

**Fixture** (`temporal_line_inc.h`, lattice mode 255 numerical / 15 state): three 0.7-px lines 16 deg off axis at depth
0.99 drifting 0.3 px/frame, a static 8x8 square at 0.98, over the sentinel and over farther routed geometry (0.999).
Shader = 2-D CPU oracle within 0.0024 (0.0083 aged; bounds 0.006 / 0.02); mask on 65 % of the line region, 0 of 2688
square-silhouette px-frames masked and 0 differ from the unfiltered resolve (gradient ratio 1.000000); configured with
A = 0 bit-identical to the unconfigured pass; bead amplitude x 0.63 (A = 2) / x 0.45 (A = 1), line peak x 0.86 / x 0.76;
refusals, hostile state, failed draw, Reset. Pass time 1280x768, all geometry: 1.16 -> 1.56 ms (+0.40 ms CPU wall with
event-query drain, Wine).

**Flight.** `--taa-line-filter 1` (then `2` if soft, `1,2` if thicker lattices still crawl) at the plant, 13-19 deg,
slow drift; promise from the replay: presented crawl x 0.58 (A = 1) / x 0.68 (A = 2) on run148.

## 10. Flight verdict "no difference" (run157 / 158 / 159), diagnosis (2026-09-19) [M unless tagged]

Captures: run157 baseline, run158 `--taa-line-filter 1,2`, run159 `2,2`; 32 frames each, plant lattice, boxes
`714 236 854 366` / `693 232 833 362` / `696 353 836 483`. Tools: `taa_resolve_replay.py <dump> <box> remedy` (new mode) and
`lattice` with `INSTALLED="dict(filter=1., fmask='line2x')"`; scratch `crawl/` (`diag.py`, `engaged.py`, `st3d.py`, `beads.py`,
`creep.py`, `sim.py`).

**1. The filter engaged.** `motion_output_taa ... line_filter=1.000 line_width=2` (run158), `2.000 / 2` (run159); no
`motion_output_taa_line_filter` refusal and no `motion_output_taa_masks` fallback line. From the dumps: the replay WITH
`line2x` reproduces run158's dumped resolve to 0.041 codes (0.061 on the mask) and run159's to 0.059; the plain replay misses
them by 2.2 / 1.8 codes (4.3 / 2.8 on the mask); run157 the other way round (0.043 plain, 2.1 filtered). The mask (recomputed
from the dumped depth) covers the lattice: `line2x` 0.49-0.61 of the box, 71-74 % of all valid-depth px, the glass mask
0.40-0.50; vertical valid-depth runs are 1 px in 59-64 % of cases (same as run148); footprint 82-145 units/px. Zoomed, run158
is visibly softer than run157. Width, angle and the both-sides test are not the problem.

**2. The numbers dropped as predicted; the metric was the wrong one.** The ship hardly moved in these flights: routed median
0.022 / 0.016 / 0.067 px/frame (run148: 0.35). Within 32 frames: non-surface-locked spectral energy of the presented window
20.5 -> 12.2 (x 0.60, run157 -> run158), static bead contrast (centred vs straddling line peak in 8-frame cycle means) 0.182 ->
0.120 / 0.142; replay on run157 itself: bead amplitude x 0.32 (A = 1), x 0.67 (A = 2), temporal rms on the mask x 0.62 / x 0.72.
But at 0.02 px/frame the beads creep one period in 50+ frames: a 32-frame capture sees them as static, and every metric of
sections 3-9 measures the 8-frame ripple and the 8-32 frame band. The zoomed difference of the first and last cycle mean is
saturated over the whole lattice in both flights: what moves on screen is the lattice image changing shape as it slides by
a fraction of a pixel.

**3. Metric that tracks it: creep residual.** Cycle means A (frames 7-14) and B (last 8); per 28-px lattice tile the best
sub-pixel translation of A onto B (Fourier shift, 10-px pad); rms residual over the lattice contrast (std). 0 for a rigidly
translating image (run153 static plant: 0.000).

| image | run157 base | run158 A = 1 | run159 A = 2 |
|---|---|---|---|
| presented dump, shift 0.3-0.4 px (run159: 1.6 px) | 0.40 | 0.26 | 0.29 |
| dumped resolve | 0.38 | | 0.26 |
| raw 8-sample cycle mean (`hdr_1`) | 0.58 | | 0.47 |

A third of the lattice contrast is non-rigid after a 0.3-px slide; the raw 8-phase supersample is worse than the resolve.

**4. Remedies replayed on run157 (presented, against that metric).**

| candidate (on `line2x`) | creep residual, codes | of contrast | lattice contrast | bead amplitude | line peak |
|---|---|---|---|---|---|
| installed | 12.38 | 0.323 | 1 | 1 | 1 |
| A = 1 (flown) | x 0.64 | 0.264 | x 0.78 | x 0.32 | x 0.62 |
| A = 2 (flown) | x 0.70 | 0.262 | x 0.87 | x 0.67 | x 0.84 |
| 5x5 Gaussian A = 0.5 / 0.25 | x 0.69 / x 0.75 | 0.331 / 0.395 | x 0.67 / x 0.61 | x 0.06 / x 0.04 | x 0.41 / x 0.32 |
| along-line Gaussian, sigma 2.5 px (structure tensor of depth validity) | x 0.78 | 0.290 | x 0.87 | x 0.59 | x 0.80 |
| mask history weight 0.97 (31-frame transient from w 0.9) | x 0.74 | 0.244 | x 0.98 | x 0.91 | x 1.01 |
| A = 1 + weight 0.97 (transient) | x 0.66 | 0.248 | x 0.86 | x 0.66 | x 0.84 |
| dim 0.5 toward the 3x3 minimum / + A = 1 | x 0.82 / x 0.77 | 0.438 / 0.490 | x 0.61 / x 0.51 | x 0.47 / x 0.25 | x 0.51 / x 0.34 |
| A = 1, clip removed (bound, not shippable) | x 0.57 | 0.254 | x 0.73 | x 0.20 | x 0.55 |

Nothing beats the flown A = 1 by more than the no-clip bound's 7 points, and the user cannot see A = 1. Blurring harder
removes the beads entirely (x 0.04) without moving the creep: **the beads are not the visible term.** Dimming lowers the
residual only with the contrast (worse relative). A synthetic lattice with ideal reprojection and no clip (`sim.py`: 0.7-px
lines, pitch 3.8, 15 deg, same drift) gives 0.465 -> 0.117 with A = 1 (x 0.25), x 0.13 with w 0.97 on top, x 0.60 for w 0.97
alone, and 16 / 32 jitter phases add little (x 0.94 at w 0.9; x 0.52 at 32 phases, w 0.97): so the real resolve keeps a
floor of about 0.22-0.26 of the contrast that the model does not have. **Not explained [unknown]:** candidates are
perspective / rotation inside a 28-px tile (rigid-translation assumption), parallax of the struts behind the glass, shading
of the lit lines changing with the slide, and the clip + Catmull-Rom interplay under a 0.02 px/frame lookup; 32 frames at
this speed cannot separate them, and the weighted rows are transients.

**5. Recommendation.** (a) Stop adding resolve-side spatial filters for the lattice: the flown one is at the measured bound
and invisible; keep `--taa-line-filter` as an option, not a default. (b) Before another remedy, one capture settles the
floor: the same view, ship drifting at the speed the user finds worst, **64 frames** (`--capture-frames 64`), baseline and
`--taa-line-filter 1,2 --taa-history-weight 0.97`; the creep residual over a 1-2 px slide with a converged history says
whether history weight on the mask (the far stabiliser's W with a line gate, replay x 0.74 without blur) is worth building.
(c) If the floor stays near 0.25, the lattice is below what this resolution can show (0.7-px lines at 2-4 px pitch) and the
remedy is source-side: fade the line draw by projected pitch (needs the draw-index capture of section 6, step 6) or a
supersampled scene target; the user should be asked what loss is acceptable on the lattice (dimmer lines, as accepted for
far stations, or a flat panel tint at distance).

## 11. Cause of the creep floor; what run172 / run173 contain (2026-09-19) [M unless tagged]

Captures: run172 baseline, run173 `--taa-line-filter 1,2 --taa-history-weight 0.97`, 64 frames each, boxes `814 353 984 503` /
`852 432 1022 582`. Scratch `crawl/`: `h1.py`, `ripple.py`, `sim2.py`; replay options `noclipmask`, `clipgamma`, `lanczos` in
`taa_resolve_replay.py ... remedy`.

**0. Both 64-frame captures are of a STATIONARY ship.** `camera_state` translation is identical to seven digits from 200
frames before the capture to 50 after it (run172 `-13171.99, 12890.15, -110179.3`; run173 likewise from frame 6062); routed
velocity on the lattice 0.0000 +- 0.0002 px/frame; every temporal component outside the harmonics of the 8-frame jitter
cycle is 0.00 codes in `hdr`, `taa` and `present`; first against last cycle mean 0.00 / 0.06 codes. (run148 / 157 / 159 did
move during their captures, so the capture does not freeze the game; the ship was simply at rest.) What these captures hold
is the static jitter ripple on the lattice, and the flown options remove it:

| presented, lattice px | contrast (std) | temporal rms | of contrast | period 8 / 4 / 2.67 / 2 |
|---|---|---|---|---|
| run172 baseline | 40.2 | 2.61 codes | 0.065 | 1.75 / 1.16 / 1.39 / 0.67 |
| run173 A = 1, W = 2, w 0.97 | 27.2 | 0.25 codes | 0.009 | 0.17 / 0.11 / 0.11 / 0.07 |

x 0.10 in codes, and the presented image of run173 is constant to a quarter of a code. "Still crawling, feels the same"
therefore cannot refer to what these two captures recorded: either the verdict was formed while moving (not captured), or
what crawls is added after our present (see 5).

**H1, reprojection error: not at rest, not decidable under motion.** At rest the motion the resolve uses averages exactly 0
on struts (0.418 of the box), glass cells (0.478) and glass cells with no valid depth in the 3x3 (0.234): medians
(-0.0002, -0.0001) / (0.0000, 0.0000) / (0.0000, 0.0000) px/frame (run153 static the same), so jitter handling and the
half-pixel convention are consistent. Glass cells are NOT reprojected as sky: they carry motion alpha 1 with a VALID
expected depth (`motion.z` median 0.999787 against the struts' 0.999785, valid share 1.000), so `resolve.hlsl` takes the
object-motion branch for them, dilated or not. Under motion (run159 0.065, run148 0.43 px/frame) struts and glass differ by
0.006-0.02 px/frame in the median with a within-frame spread of 0.03-0.12 (rotation / perspective across the box);
registering raw cycle means of a periodic lattice is too ambiguous to test that (tile sd 0.1-0.5 px, residual at the best
shift 0.4-1.2 of contrast), so a 0.02 px/frame error is neither shown nor excluded. **[unknown]**

**H2, variance clip: no.** Creep residual (presented, codes), replay on run159 (shift 1.0 px, 26 tiles) / run157: clip off on
the mask x 1.02 / x 1.06; min/max box only (gamma 9) x 1.02 / x 1.03; no clip anywhere x 1.01 / x 1.08; with A = 1, clip off on
the mask x 0.45 against x 0.46 with it. The x 0.57 of section 10 was 32-frame noise. On a binary lattice every 3x3 holds both
extremes; the clip never binds (the synthetic resolve with and without it is identical to four digits).

**H3, Catmull-Rom history resampling: no.** Lanczos-3 (36 taps) on the mask: x 0.97 / x 1.00; with clip off x 0.97 / x 1.05;
Lanczos + clip off + A = 1 x 0.45 / x 0.61 against x 0.46 / x 0.64 for A = 1 alone.

**H4, sampling / reconstruction: yes, this is the floor.** `sim2.py`: binary 0.7-px lines, pitch 3.8 px, 15 deg, through the
actual resolve ingredients (Catmull-Rom fetch at the true velocity, the clip, w, optional A), 224 frames, steady state:

| creep residual / contrast | 0.022 px/frame (run157) | 0.065 px/frame (run159) | real replay run157 / run159 |
|---|---|---|---|
| Halton 8, w 0.9 (installed) | 0.533 | 0.770 | 0.323 / 0.317 |
| + A = 1 | 0.161 (x 0.30) | 0.240 (x 0.31) | 0.264 / 0.182 |
| + A = 1, w 0.97 | 0.178 | 0.300 | |
| Halton 16 w 0.95 / 32 w 0.97 | 0.436 / 0.344 | 0.664 / 0.543 | |
| R2 8 / 16 (w 0.95) | 0.500 / 0.418 | 0.684 / 0.612 | |
| rotated grid 8 / 16 (w 0.95) | 0.496 / 0.419 | 0.730 / 0.622 | |
| Halton 16 or rotated grid 16, w 0.95, + A = 1 | 0.158 / 0.164 | 0.232 / 0.245 | |

The model with the real resolve has the same floor as the real frames once the filter is on (0.16-0.24 against 0.18-0.26):
section 10's "floor the model does not have" came from comparing against an ideal-reprojection model at one velocity and
is withdrawn. Sample count and pattern are not the limit: 16 / 32 samples or better 1-D projections buy x 0.8-0.95 without
the filter and nothing with it (0.158 against 0.161). The residual is the phase dependence of reconstructing a 0.7-px line
at 3.8-px pitch with a kernel about one pixel wide on a pixel grid: it is a property of the content at this resolution,
not of a resolve defect. Share of the floor: H4 all of it within the model's accuracy; H2 and H3 at most 3 %; H1 zero at
rest, undecided under motion.

**5. Recommendation.** No further resolve work on the lattice. (a) Ask the user two things before anything else: was the
crawl judged with the ship moving (then the 172 / 173 captures miss it and a moving 64-frame pair is needed), and at what
window / display scaling the game is viewed: the dumps are the 1280x768 back buffer, and a non-integer scale of a 2-4 px
lattice by the compositor produces its own moire that no present-side change can touch **[A]**. (b) If it is judged moving:
the only levers left act on the content: lower the lattice contrast with distance (fade the line draw or tint the panel
by projected pitch; needs the draw-index capture of section 6) or render the scene target at a higher resolution; predicted
creep for a fade to half contrast is x 0.5 in codes by construction, with the filter x 0.25-0.3 of today's. (c) Keep
`--taa-line-filter` optional; its measured gain stands (creep x 0.46-0.64 moving, static ripple x 0.10 with w 0.97).

## 12. Screen recording against the run175 dump at the same standstill (2026-09-19) [M unless tagged]

Inputs: `screenshots/lattice.mov` (ReplayKit, h264 yuv420p tv-range, 1280x768, 361 frames in 6.44 s on a 120 Hz timebase, frame
gaps 16.7 ms x 281, 25 ms x 56, 8.3 ms x 17, 33 ms x 6; 11.5 Mbit/s) and `/tmp/x3-bottleX3-run175` (64 frames 7485-7548, **baseline**:
`taa_current_filter=0`, `taa_history_weight=0.900`, sharpen 0.75, 8-sample Halton jitter; this build has no line-filter option in
`proxy_options`). Camera translation identical at 7485 and 7548. Scratch: `mov/` (`met.py`, `mot.py`, `lk.py`, `zf.py`, `thick.py`,
`fringe.py`, `rem*.py`). Output for the user: `build/lattice-compare/` (movie left, dump right, 4x nearest; GIFs and 8-phase strips).

**Scale.** The recording is the window content at 1:1: phase correlation of the movie mean against the dump mean peaks at (0, 0),
movie = 0.907 x dump + 2.9 codes (tv-range/codec), no resampling.

**The recording shows what we present.** Luma, pixels with 3x3 contrast > 25 codes:

| region (x0 y0 x1 y1) | source | temporal rms | p2p p50 / p90 / p99 / max | px > 20 / > 40 codes |
|---|---|---|---|---|
| edge-on orange arm 760 60 900 200 | dump | 15.5 | 15 / 48 / 151 / 158 | 2618 / 948 of 6952 |
| | movie | 14.7 | 23 / 57 / 153 / 165 | 3815 / 1161 |
| face-on panels 740 300 1000 440 | dump | 3.3 | 4 / 17 / 29 / 105 | 1414 / 60 of 25828 |
| | movie | 3.5 | 14 / 23 / 41 / 227 (HUD box) | 4231 / 260 |

Dump variance is 8-periodic to 1.000 (residual 0.01 codes). Movie spectrum on a 120 Hz hold grid: arm 0.64 of the energy in 6-9 Hz
(peak 7.5 Hz = 60 fps / 8) with harmonics at 14.9 and 22.5 Hz; nothing at other frequencies except < 1.2 Hz codec/HUD drift on the
panel crops. No beat with the display or recorder. The movie's larger p2p on the panels is codec noise over 361 samples.

**What crawls is not the section-11 lattice box but the near-edge-on arm** (and the other foreshortened arms, p2p map). Sections 10-11
measured face-on panels (2.6 codes rms) and missed it: there the ripple is small; on the arm hundreds of pixels swing by 100-160 codes
every cycle. 3819 arm pixels toggle between valid depth (orange panel, raw rgb 0.61 0.41 0.32) and sentinel depth (dark 0.04) across
the 8 jitter phases, covered in 1..7 of 8 phases; the dark regions are 1 px thick in the median but up to 4 (p90) / 7 px, and the
raw pattern anti-correlates between successive phases (r = -0.58). **[I]** moire of the foreshortened line lattice against the pixel
grid (pitch near 1 px), whose fringes jump several pixels for a sub-pixel jitter step; the shard-shaped fringes are the "triangles".
History is accepted on all these pixels (accept 1.00, disocc 0.00); the variance clip discards it, because a fringe is wider than
the 3x3 box, so the box holds only the current state. Only 0.22 of the > 80-code pixels are in `line2x`, 0.45 in `thin`.

**Motion.** No translation is measurable in either source: global Lucas-Kanade step 0.03-0.07 px/frame tracking the jitter delta at
about 10 % (= 1 - w), net movie dy +0.0025 px/frame on the arm; integer-shift correlation of successive presented frames peaks at
(0, 0) or is incoherent. The pattern is a cyclic 7.5 Hz shuffle of shards along a diagonal arm. "Upward" is perceptual and not
measured. **[unknown]** (the y jitter of Halton base 3 is a sawtooth, five +0.333 steps and three -0.556 resets per cycle, a
candidate cause).

**Replay on run175** (`rem*.py` exec the definitions of `taa_resolve_replay.py`; replayed present vs dumped 0.20 / 0.41 codes; last
24 of 56 frames, so w >= 0.97 rows still contain some convergence transient; "LK" = rms global step px/frame):

| config | arm rms / p99 / px > 40 / LK | panels rms / p99 / px > 40 / LK |
|---|---|---|
| installed | 14.8 / 151 / 1159 / 0.069 | 3.25 / 29 / 54 / 0.048 |
| jitter order randomised per cycle | 14.4 / 151 / 1651 / 0.075 | 4.43 / 45 / 662 / 0.048 (worse) |
| no RCAS on `line2x` | 14.8 / 151 / 1101 / 0.070 | 3.12 / 28 / 40 / 0.050 |
| w 0.97 on `line2x` | 14.0 / 151 / 824 / 0.057 | 1.57 / 19 / 7 / 0.023 |
| w 0.985 on `line2x` | 13.9 / 151 / 805 / 0.054 | 1.38 / 19 / 7 / 0.018 |
| A = 1 + w 0.97 on `line2x` (run173 options) | 13.9 / 151 / 809 / 0.056 | 1.65 / 19 / 7 / 0.016 |
| clip off on `thin` (3x3) + w 0.97 on it | 11.1 / 150 / 324 / 0.050 | 1.42 / 16 / 5 / 0.019 |
| no clip anywhere, w 0.9 (bound) | 5.9 / 41 / 192 / 0.039 | 3.02 / 26 / 17 / 0.046 |
| `thin` from a 5x5 depth-validity box: clip off + w 0.9 | 5.6 / 55 / 388 / 0.017 | not run |
| same, 5x5, w 0.97 | 2.9 / 28 / 25 / 0.009 | not run |
| same, 7x7, w 0.97 | 2.3 / 22 / 6 / 0.007 | not run |
| same, 9x9, w 0.97 | 2.0 / 21 / 1 / 0.006 | not run |

A 16-sample sequence cannot be replayed from an 8-phase dump. Reordering does not help (it spreads the same energy off the 8-frame
period and raises p2p). The flown `--taa-line-filter 1,2 --taa-history-weight 0.97` leaves the arm untouched, which explains "looks the same". What removes it at
standstill is a wider mixed-depth mask (valid and sentinel depth within 5x5 to 7x7) with the clip off and w 0.97 on it, speed-gated
like the far stabiliser. Not evaluated: the panels crop and other scenes for ghosting/collateral of the wider mask, behaviour under
motion, and whether the unjittered game shows a static moire on the arm (expected, not checked).

## 13. Implemented, unflown (2026-09-19): `--taa-thin-region W[,RELAX[,LO,HI]]`

Replay tool: `taa_resolve_replay.py <dump> <box> thin` (`GATES=LO:HI,...`, `WS=`, `GAMMAS=`, `GROW=`, `AGE0=`, `GHOST=1`, `SKIP=`);
mask survey `crawl/tmask.py`. Presented stage; "textured px" = 3-frame x-contrast > 25 codes, last 24 frames (12 of 31 on the
32-frame captures). All **[M]**.

**Mask.** A plain valid-and-sentinel 5x5 / 7x7 box holds every silhouette (1.000 of plain-silhouette px). Chosen instead:
a pixel is FRAGMENTED when one of the four 7-tap lines through it (h, v, two diagonals) changes depth class at least twice,
a class change being the line mask's test between consecutive taps (one valid, the other the sentinel or farther by
`(1 - q) * 1.1 < 1 - d`, so it also fires in front of distant geometry); the region is the 7x7 around fragmented pixels. A
straight silhouette crosses each line once: 0 of the fixture's square. On run175's arm the region holds 0.926 of the px with
p2p > 40 codes on average over the phases (every phase 0.63, any phase 1.000), 0.90 of those > 80; the undilated test 0.47; a
two-direction requirement 0.61. Frame share: 0.118 (run175), 0.21 (run153 plant), 0.03-0.04 (run153 / run160 / run174 / run142
station scenes), 0.078 (run148). Of "plain silhouette" px of real frames (3x3-mixed, next to 5x5-eroded geometry, no small
geometry within 4 px) it still takes 0.30-0.51, because station silhouettes carry detail within 6 px; what protects those is
the speed gate, not the mask.

**Action, by numbers (run175, arm `760 60 900 200`: installed rms 16.9 / p2p p99 151 / px > 40 827).**

| on the region | arm rms / p99 / px > 40 | panels `740 300 1000 440` |
|---|---|---|
| clip off, W 0.97 (exponential, age 64 from the start) | 3.30 / 23 / 6 | 1.52 / 15 / 4 (installed 4.51 / 36 / 51) |
| clip off, W 0.97, age restarted at 1: cumulative mean n/(n+1) until the cap binds (cap ~ 32) | 3.20 / 24 / 6 | |
| clip off, W 0.985 (cap 64): steady / from age 1 | 2.66 / 19 / 6 and 2.77 / 24 / 6 | 1.22 / 15 / 4 |
| clip off, W 0.9375 (cap 16) | 5.27 / 43 / 74 | |
| clip widened to mean +- 3 sigma, box dropped, W 0.97 | 12.5 / 145 / 211 | |
| clip off, W 0.985, region grown by 5 instead of 3 | 1.61 / 18 / 0 | |

Clip off is required (gamma 3 leaves 74 % of the ripple; a fringe is wider than any local statistic). The phase-balanced
accumulation of the blind review is what the age target already does: `min(n / (n + 1), W)` IS the cumulative mean of the
jitter cycle until n reaches W / (1 - W) (32 frames at 0.97, 64 at 0.985, the age target's limit), then an exponential of that
length; it measures the same as the exponential at equal cap (3.20 against 3.30), so no separate mode was built. The rest
(rms about 3) is not blend ripple but the mask toggling with the phase (region present in every phase on 0.63 of the hot px):
on a phase without the region the clip snaps the pixel back. Growing the region by 5 removes most of it (1.61, px > 40 = 0) for
0.51 instead of 0.42 of the crop; not taken yet (open issue). An exactly balanced window (equal weights over whole cycles)
would need a second history and is not needed at these levels.

**Ghosting under motion: the speed gate, and whose speed.** Added presented colour on masked BACKGROUND px against the
installed resolve (mean / p99 codes / share of px-frames > 8 codes), gradient energy on the region:

| capture (far-px speed) | no gate | gate 0.03-0.25, own speed | gate 0.03-0.25, fastest px within 6 px (chosen) | gate 0.1-0.5, same |
|---|---|---|---|---|
| run148 plant, 0.49 px/frame | 2.07 / 23.3 / 0.219, grad x 0.637 | 0.15 / 4.8 / 0.006 | 0.01 / 0.7 / 0.000, grad x 0.979 | |
| run161 station, 0.84 | -7.04 / 59 / 0.363, grad x 0.623 | -7.13 / 57 / 0.254 | region closed (mask share 0.000) | same |
| run159 plant, 0.068 | 2.31 / 27.8 / 0.195, grad x 0.729 | | 1.71 / 21.0 / 0.118, grad x 0.792 | 2.29 / 27.3 / 0.189 |
| run142 station, 0.045 | -0.17 / 13.9 / 0.035, grad x 0.993 | | -0.16 / 10.2 / 0.019, grad x 0.979 | -0.19 / 13.7 / 0.031 |
| run160 station, 0.087 | | | -0.14 / 7.2 / 0.007, grad x 0.857 | |

A per-pixel gate fails where it matters: the background pixel a moving edge has just uncovered carries the background's
speed (run161: a 57-code trail with the gate "on"). The gate is therefore closed by the fastest pixel within 6 px, the
reach of the region itself (the fixture found the 3-px version leaking at popping shards). Gate 0.03-0.25 as the far
stabiliser's; slow drift (0.04-0.09 px/frame) keeps most of the effect and pays 0.8-0.86 of the region's gradient energy
at the plant, nothing measurable at the stations. Static collateral: run153 station 5.31 / 57 / 74 -> 3.96 / 50 / 40; panels
above; neither regresses. Known residual: a body moving BEHIND a static lattice is not seen by the gate (the lattice and
its gaps are static); its trail through the gaps lasts about 1 / (1 - W) frames, bounded to the region **[I]**.

**Implementation.** `line_mask_ps.hlsl` (270 slots) gained the fragmented test (25 depth taps), the pixel's own speed closure
(routed motion, else the camera path `c0..c3` at its depth) and separable maxima: three draws when the line filter or the
thin region is on (tests -> [0], maxima along x -> [1], along y + composition -> [0]; 13 taps each), one draw for the far
stabiliser alone. Mask: r filter weight, g far gate, b thin-region strength (already speed-gated), a = b scaled to the
weight target. `resolve_far.hlsl` carries it: `soft = b * c24.x` replaces the 3x3 sentinel soft clip (not compiled in this
variant; `--taa-thin-clip` is refused beside the far stabiliser or the thin region) and
`keep += max(g * slow, a) * (min(n / (n + 1), c24.y) - keep)` with `c24.y = max(W_far, W_thin)` and each gate scaled to its own
target. **`resolve_far` 495 slots** (was 508: the sentinel tracking left), every other resolve program's bytecode unchanged;
only the mask program and `resolve_far` changed, both bound with these options alone, so option-off images are identical by
construction (seam cases unchanged: 164 / 140 / 59 checks). Proposed default set once flown: plain + mask program +
`resolve_far` (far stabiliser + thin region; line filter optional through the same mask); `resolve_age_line` and the other
line variants stay for the line filter without them. Pass time 1280x768, all geometry (CPU wall, event-query drained):
plain 0.61 ms, far stabiliser +0.19, line filter +0.40 (three draws in that build; it was +0.26 with two, restored by the review fixes below), thin region +0.79.
Launcher `--taa-thin-region W[,RELAX[,LO,HI]]` (`X3M_TAA_THIN_REGION`; RELAX default 1 = clip off; LO,HI shared with
`--taa-far-stabiliser`, a differing pair is refused); log fields `thin_region=`, `thin_relax=` on `motion_output_taa`.

**Fixture** (`temporal_thin_region_inc.h`; lattice mode 420 numerical / 19 state): shards 0.8 px at pitch 2.37 over the
sentinel plus a plain square. Static: shard ripple 16.3 -> 1.40 codes rms (x 0.086, p2p 99 -> 6) at W 0.97, x 0.044 at 0.985,
x 0.31 with RELAX 0.5, x 0.40 with the weight alone (the clip is the cause); shader = 2-D oracle (0.0083 at 0.97, bound 0.02),
age exact, published gate = oracle to 0 codes; the square and everything right of x = 20 bit-identical to the plain resolve
in all four channels for every config and speed (0 of 29 952 px-frames); drifting 0.12 px/frame x 0.61; at 0.30 px/frame the
whole image bit-identical to the plain resolve; motion starting after 64 static frames: equal to the installed resolve
within 0.0002 after 24 frames, uncovered background at most 0.029 above it from frame 8 (bound 0.04); refusals, hostile
c0..c7 / c22 / c24 / s4 / s8 / COLORWRITEENABLE1, failed second mask draw, Reset, mask-allocation fallback.

**Flight.** `--taa-thin-region 0.97` at the run175 standstill, then the same view drifting and turning; judged on the arm's
shuffle, trails behind the plant's edges when motion starts, and ships crossing behind lattices. If a residual twinkle
remains at rest: W 0.985, then the wider region (open issue).

### 13.1 Review fixes and the wider region (2026-09-19)

- **Region grown by 5 (11x11), closure within 8 px: default.** Replay, W 0.97: run175 arm 3.20 / 24 / 6 -> **2.34 / 22 / 0**
  (rms / p2p p99 / px > 40). Trails on masked background do not worsen (grow 3 -> 5, p99 codes / share > 8 codes): run148 0.7 / 0.000 ->
  0.7 / 0.000; run159 21.0 / 0.118 -> 20.5 / 0.110; run160 7.2 / 0.007 -> 6.2 / 0.005; run161 region closed in both. Frame share
  0.118 -> 0.135 (run175), 0.210 -> 0.236 (run153 plant), 0.042 -> 0.063 and 0.032 -> 0.046 (station scenes), 0.078 -> 0.089 (run148).
  Pass time of the thin region +0.67-0.69 ms against +0.79 measured with grow 3 (17 instead of 13 taps per separable draw; the
  difference is below the run-to-run spread of this timing, so no cost is measurable; mask program 305 slots).
- **Exact weights with both options on.** `resolve_far` now carries both targets: `c24.y` = W_far through `g` and the pixel's own
  speed gate, `c5.x` (never read by a resolve before) = W_thin through `b`; the history weight is the far blend, or the larger of
  the two where `b > 0`. No shared maximum, no scaled gate channel (mask `a` is 0). 496 slots. Fixture, far scene with
  `far 0.985 + thin 0.97`: oracle 0.0003, near pixels 0 of 39 936 differing, far-band ratio 0.154 / 0.192 / 0.577 / 1.000 as the
  far stabiliser alone; shard scene with both: ripple x 0.095 of the installed resolve, 1.10 x the thin region alone (pixels whose
  depth toggles with the phase alternate between the two targets), square bit-identical.
- **The far stabiliser alone is the program the user flew.** Fixture-only reference `temporal_resolve_far_reference_inc.h` (the
  `resolve_far` bytecode of commit dee6608c, run46 / run47 candidates) through `configure_far(reference)`: weight-only and
  weight + filter, static and at 0.04 px/frame, 96 frames each: bit-identical images.
- **Line filter alone is two mask draws again** (mode 4: 3x3 maximum + composition); the separable three-draw path runs only with
  the thin region. Line filter +0.22-0.23 ms (was +0.26 before the thin region, +0.40 in the first thin-region build).
- **`--taa-thin-clip` beside the far stabiliser or the thin region is refused, deliberately**: the far program compiles no 3x3
  sentinel soft clip (it measured no effect on any real capture, flicker note section 10.1); stated in both help texts.
- The route judges the two options separately: an out-of-range thin-region weight disables only the thin region
  (`motion_output_taa_thin_region ... reason=weight_range`), a bad far weight / filter only the far stabiliser; shared
  prerequisites (adaptive weight, current filter, thin clip, program, age caps) still take both, one log line.
- **One speed gate**: given on either option it applies to both (the launcher rewrites the far stabiliser's fields 5-6 when only
  the thin region names a gate); given on both, the pairs must be equal or the launch is refused; host test covers the three cases.
- **Frame border**: every tap clamps, so a line leaving the frame sees no further class change and a border pixel is exactly as
  fragmented as its inside neighbours make it (equivalent to "out of frame = the edge pixel's class"); documented in the shader.
- Seam cases rerun with the record kept: `verification/results/bottle-X3/motion-output-partial.json` (PARTIAL by design):
  `seam-taa-on` 164, `seam-taa-hdr-tonemap-on` 140, `production-taa-hdr-tonemap-on` 59 checks.
- `run_temporal_pass.py` exit 0: lattice mode 459 numerical / 19 state.



## 14. Run 48 A: stationary success, camera-rotation limit (2026-09-20)

**User report:** run176 baseline still crawls. With thin region 0.97, far stabiliser
0.985 and light-map fade 40,110, run177 fixes the stationary arm; crawl remains
while rotating the camera. This confirms the stationary benefit, not moving
quality or absence of trails behind independently moving objects.

**Real-dump gate reconstruction:** run177 stationary frames 5674–5705 and rotation
6392–6423. The inherited run175 crop mostly covers a spar in this changed view;
use the visually checked gridded-truss crop `900 50 1100 170`. Applying the existing
replay mask and motion-gate operators to the dumps gives:

| burst | fragmented-region pixel-frames | region speed p50 / p90 / p99 (px/frame) | gate active within region |
| --- | ---: | --- | ---: |
| stationary | 376,621 | 0.001 / 0.006 / 0.006 | 100% |
| camera rotating | 256,122 | 6.852 / 9.230 / 10.142 | 170 pixel-frames, 0.07% |

These are reconstructed gate measurements, not a readback of the shader's mask,
and the fixed crop does not follow one material point through the pan. They show
that the treatment is almost entirely gated off where the truss passes through
this crop during rotation. The user-visible residual is consistent with returning
to the ordinary resolve above HI=0.25; it is not evidence of a failed stationary
fix. No raw fixed-pixel variance is used as a moving crawl score.

**Decision:** keep 0.97 as the stationary-benefit candidate; leave camera-motion
quality open. Do not widen the shared gate into the measured 7–10 px/frame range:
it would retain unclipped history under substantial motion, contrary to the prior
blur rejection. A further tuning decision needs motion-compensated analysis of
the actual truss and trail measurements; a slow-pan capture near the 0.03–0.25
knee would test a local gate adjustment, whereas this rapid pan does not. No
additional flight is required before the already queued run 48 B/C.

Local preview and reconstruction results: `verification/results/run48a-lattice-triage/`.
Flight/environment provenance is in the [motion-output ledger](../verification/motion-output.md).

## 15. Moving-truss replay: quality gains and safety limits (2026-09-20)

**Decision:** neither approach below is a safe production fix or part of the
candidate. Existing dumps support useful investigation without another flight;
the remaining work is a renderer design and its failure tests, not a request to
wait for more captures. All numbers below are host replay measurements **[M]**.
No renderer edits, Wine, game launch or installation accompanied this work.

**Tracking and reference.** Run177 rotation frames 6392–6423, with frame 6392
seeding history and the last 16 resolved frames scored. Replay rectangle
`790 20 1140 335`; material points start in `905 55 1095 165`. Camera-only
registration drifts from the truss by median 1.644 px over the burst. Fitting and
composing the recorded routed motion instead gives per-frame median/p99 fit
errors 0.00105/0.00242 px. Scores use identical material support and AgX + RCAS,
without bloom. Ordinary replay versus dumped resolve, through those same display
operators, has mean absolute error 0.168 codes. Registration itself introduces
sampling error; the RMS is a comparison metric, not perceptual acceptance.

**Approach 1: retain history during coherent camera motion.** Subtract camera
motion from routed motion, retain the fastest-neighbor gate, and use W=0.97 with
Keys history parameter −0.65 only in the coherent fragmented region. A new
0.25–1 px/frame activation ramp preserves the existing stationary/slow path.
Tracked RMS falls **6.713 → 4.776 codes (28.9%)**, with gradient energy **1.023×**.
Without the sharper kernel, increased history loses detail; finite 8/16-frame
raw-history windows also failed to improve quality. This does not reverse the
prior slow-motion kernel rejection: the new branch is inactive there.

The gain is unsafe without an additional bound. In frame 6401, a synthetic 6×6
bright stale-history patch at previous coordinate approximately `(950,115)`,
with real current color/depth/motion, adds **236 codes** at background `(947,123)`;
the ordinary clipped resolve adds **47**. The existing 2% device-depth tolerance
rejects no pixel-frame in this replay. A tighter view-depth-relative proof on
its 2×2 depth footprint reduces the finite-foreground case to 75 codes, but
sentinel history still fails, and the proof does not cover the 4×4 color footprint.
Requiring all 4×4 history depths valid and compatible removes that sentinel fault
but reduces the quality gain to **2.4%**; same-depth contamination remains
indistinguishable. On unmodified frames, the new branch's eroded-background
absolute delta has p99/max **0.155/35.98 codes** against ordinary replay.

History classification would need to describe the **accumulated color**, including
unknown/depthless overlays and taint inherited through every contributing tap.
Current motion or raw depth alone cannot certify it. Existing reactive masks
represent composition coverage, not that history classification. A sampled
constant-motion kernel check also rejects Keys −0.75 at W=0.97 (feedback 1.027);
−0.65 reaches 0.993, so this 32-frame burst cannot establish long-term ringing.

**Approach 2: explicitly bound the enhancement against an independent ordinary
history.** Maintain separate ordinary and enhanced recurrences; project enhanced
HDR feedback toward the ordinary result, then clamp each final displayed RGB
channel after AgX + RCAS to ordinary ±C. Evaluating an “ordinary” result on the
enhanced history would invalidate the multi-frame bound.

| final RGB allowance C | tracked RMS codes | reduction | gradient energy / ordinary |
| --- | ---: | ---: | ---: |
| ordinary | 6.713 | — | 1.000 |
| ±2 codes | 6.325 | 5.8% | 0.985 |
| ±4 codes | 6.025 | 10.2% | 0.979 |
| ±8 codes | 5.522 | 17.7% | 0.977 |

Each allowance stays within its exact quantized RGB bound in the real-frame
stale-patch tests, including corruption injected only into enhanced history.
Each also equals its ordinary replay reference exactly on run177 stationary
5674–5705 (**10,253,250 RGB values per allowance**) and run159 slow 4789–4820
(**4,171,050 per allowance**). The run159 comparison uses identical replay options
for both chains; it does not reconstruct that flight's historical line-filter setup.

A separate **512-frame synthetic stress test**, not an extension of the flight,
uses a periodic moving lattice, an independently moving depthless bright overlay
at frames 64–95, and enhanced-only stale history injected at frame 128. All
quantized bounds hold. Last-cycle differences are 0.115/0.123/0.191 codes for
C=2/4/8, versus ordinary 0.116; the unbounded sharp-history experiment still
differs by 34.50. This establishes bounded additional error against ordinary
TAA, not clean history or absence of ordinary TAA's own ghosts.

**Costs and limits.** RCAS expands the earlier HDR/AgX guard to 5.16/9.54/16.30
codes on the real burst, so a final post-sharpen clamp is essential. The feedback
projection currently uses up to 16 AgX segment checks per pixel: an offline
oracle, not a practical shader patch. Production needs an independent ordinary
history/display chain (another FP16 history pair alone is **15 MiB at 1280×768**),
a cheaper feedback guard with remeasured quality, and a bound after the complete
actual display pipeline. Bloom is untested; metering/exposure feedback must not
let enhanced output contaminate the ordinary reference indirectly. Shader budget,
GPU cost, state/Reset/failure recovery and native Windows execution remain open.
The measured 6–18% improvement is not evidence that the user's crawl is fixed.

Local, untracked evidence and reproduction commands:
[coherent-motion investigation](/tmp/x3-motion-lattice-replay/verification/results/motion-lattice-replay/REPORT.md)
and [bounded-display investigation](/tmp/x3-motion-lattice-replay/verification/results/lattice-bounded-rotation/REPORT.md).
Their JSON records contain tracked metrics, background quantiles, stale-history
witnesses and exact comparisons; the synthetic record is in the neighboring
`lattice-bounded-loop/` directory. Replay helpers passed syntax and numerical
checks during the investigation; this documentation checkpoint adds no code tests.

## 16. Cheaper post-display history replay rejected (2026-09-20)

The next bounded experiment is complete, not waiting for another flight. It
keeps the ordinary HDR resolve independent and accumulates only display luma
in R16F, reprojected with recorded motion. A moving-only gate leaves the
stationary/slow path unchanged; final RGB stays within ordinary display ±C.
This removes the earlier iterative HDR-to-display projection, but is still an
offline AgX/RCAS model without bloom, not the complete game presentation.

Using §15's run177 rotation and material tracking, W=0.90 gives:

| History kernel / RGB allowance | Tracked RMS codes | Reduction | Gradient energy / ordinary |
| --- | ---: | ---: | ---: |
| Ordinary | 6.7132 | — | 1.0000 |
| Keys −0.65 / ±8 | 5.7587 | 14.2% | 0.9445 |
| Keys −0.65 / ±4 | 6.1556 | 8.3% | 0.9715 |
| Catmull–Rom / ±8 | 5.7000 | 15.1% | 0.8934 |

**Decision:** reject this recipe for production. The primary misses both the
predeclared ≥20% RMS improvement and ≥0.95 gradient ratio. Stronger smoothing
is not an established solution. RGB bounds hold in the evaluated replay;
stationary run177 and slow run159 are exactly unchanged across 10,253,250 and
4,171,050 RGB values per variant. These are comparisons to the same offline
ordinary reference, not proof of zero error against all historical game settings.

The proposal would require an R16F history pair plus display scratch, about
7.5 MiB at 1280×768 and two additional fullscreen draws. Neither GPU cost nor
state/Reset/native behavior was tested. Gamut clipping can change chroma; a
scalar history does not guarantee chroma preservation. No production edits,
new candidate or game launch accompanied the experiment. The moving-lattice
issue remains open, with this alternative excluded rather than queued to ship.

Local helper and results: `/tmp/x3-lattice-display-replay/tools/analysis/taa_lattice_display_replay.py`
and `/tmp/x3-lattice-display-replay/verification/results/lattice-display-replay/`
(`rotation`, `static`, `slow159` JSON records). No expanded synthetic stress
run was needed after failure of the bounded quality screen.

## 17. Moving-truss mesh ownership recovered (2026-09-20)

The captured model key `0x54b3` is a numeric resource-registry ID, not a content
hash or a filename. The three initially named S/M/L solarplant bodies fail the
captured fingerprint. Following power-factory aliases identifies
`objects/stations/x3tc/terran_spp_panel.pbb`, LOD0: all **22 subgroup triangle
counts** match (54,412 triangles/instance), 20/22 vertex counts match, and body
scale matches all 1,056 selected draw records. Two runtime groups have three
additional vertices in total; D3DX splitting is compatible with this difference,
but the exact split provenance is not established.

A material conversion correction is byte-proven: the point loader reads a
signed BE32 value, executes `SAR EAX,2` at `0x00482669/676/684`, and stores AX.
Later `MOVSX` loads at `0x004bc2d5/e0/f0` apply 2^-14 before the captured FP16
vertex representation. This is floor division for negative values, not C-style
truncation toward zero. The latter had created an artificial roughly 2% coverage
mismatch; it must not be attributed to alpha sampling or rasterization.

With corrected positions, captured transforms/culling/viewport and positive
screen-space jitter, geometry-only projection in the fixed truss crop matches
raw valid-depth support at IoU **0.99798 / 0.99712 / 0.99795** for frames
6392/6401/6423. Respective depth matches are **1,972 / 4,143 / 3,878** pixels.
An independent correct-floor reversed-jitter control falls to IoU0.567546.
The dominant owning groups are materials4/6/18 (support/grid, 5,684 triangles
per model), which use alpha-tested programs. That shader classification does
not itself establish alpha-texture aliasing: mesh projection already explains
almost all observed support, while UV/alpha execution remains unreconstructed.

**Decision:** ownership is established sufficiently for a current-frame coverage
experiment. It does **not** establish the cause of the displayed crawl or a
production fix. Next compare exact visible pixel-area coverage against binary
sampling on the same moving geometry and fixed material tracks, with independent
supersampling/depth-overlap witnesses. No current RGB fitting, new flight or
GPU-cost claim is needed for that bounded oracle. TAA remains required; a
coverage-only result is not complete TAA/display acceptance.

Local evidence: `/tmp/x3-lattice-ownership/REPORT.md`, `ownership_summary.json`
and reproducible helpers. Parser census validates 531 groups / 935,531 faces /
1,569,058 points; independent review confirmed the loader/conversion bytes,
corrected projections, and all 12 malformed-input/missing-frame/viewport guards
under optimized Python. Affected syntax checks pass. Exact D3DX half rounding,
residual pixels, alpha effects and the eventual partial-coverage/depth contract
remain open. No production edits, Wine, game, build or install accompanied this
ownership investigation.


## 18. Visible-area oracle is numerically unreliable (2026-09-20)

The bounded moving-geometry coverage experiment cannot support a quality
verdict. Source ownership and projection remain supported by §17, but the
GEOS/Shapely visible-polygon operations produced incorrect area while reporting
valid geometry. A small crop-wide mean error hid isolated large errors.

On frame6409 pixel(791,157), the first global visible-union calculation returned
0.436406 coverage versus independent 512× sampling 0.690758. Local clipping
and union recovered approximately 0.690775 there. That correction exposed a
second failure at (817,147): reported coverage1.0 versus approximately0.106
from independent dense sampling. Independent review traced the second failure
to a global polygon difference for triangle7626: the cutter covers the local
area, but the difference retains it. A geometry-validity flag is therefore
insufficient. Sequential subtraction agrees on that witness but is not a
validated replacement for all frames.

**Decision:** stop the bounded GEOS-based experiment; retain both failure
witnesses and invalidate affected integration gates. Do not infer that true
area coverage would improve or fail the moving-lattice quality thresholds.
No temporal-quality result or production implementation is accepted. A future
coverage experiment needs a numerically reliable construction with independent
local high-resolution checks, including maximum-error gates, before comparing
temporal behavior. No new user flight is needed to resolve this offline issue.

Local implementation/results: `/tmp/x3-lattice-coverage-oracle/`; compact
`verification/results/lattice-coverage-oracle/rejected-geos/FINAL.json` and
`rejected-geos/failure-witnesses.json` preserve the outcome and failure witnesses. The failed
experiment changes no production renderer, candidate or installed files.


## 19. Corrected source-coverage oracle passes (2026-09-20)

The replacement uses pixel-local convex clipping in float64, assigning each
target triangle only the region where it is the nearest primitive. Every
nearer original triangle is subtracted; deterministic equal-depth draw order
handles shared/duplicate surfaces. The resulting ownership regions are
disjoint, so their areas can be summed without the failed GEOS global union.
This is numerically checked analytic coverage, not symbolic exact arithmetic.
The rejected §18 artifacts remain preserved.

Independent review validated eleven synthetic cases, 120 deterministic random
cases and dense references for the earlier failure pixels. All 32 motion frames
6392–6423 pass integration and global outlier refinement; maximum discrepancy
is 0.003668 against dense integration witnesses. Values exceeding one only by
floating roundoff are retained; values beyond the explicit 1e-12 tolerance
fail rather than being clamped. The corrected run used 1,299.64 CPU seconds.

On the unchanged fixed material/lattice supports (2,154 / 1,406 support points),
material-tracked coverage RMS over frames 6408–6423 changes:

- Material: 0.19424159 → 0.04890944, a **74.82% reduction**.
- Lattice: 0.22745865 → 0.05620690, a **75.29% reduction**.

Mean-coverage errors versus the independent reference are about 6e-6/1.4e-5
relative; maximum profile-integral error is 1.974% and RMS-width error 0.904%.
Both retained 20% temporal-reduction gates and 5% reference/profile gates pass.
Image contrast decreases 6.13%/13.04%, a perceptual tradeoff, not a hidden pass.
The reviewer rebuilt both supports and all jitter-aware samples independently
and reproduced the metrics.

**Decision:** this supports a source-coverage/RGB experiment. It does not
establish displayed crawl dominance, solve the earlier 6.713-code RGB metric,
or qualify TAA, ghosts, alpha execution, unrelated occluders or GPU cost.
Actual observed radiance and occlusion must constrain the next RGB replay;
coverage alone cannot invent the material or hidden background colours.
No renderer implementation or additional user flight follows automatically.

Local evidence: `/tmp/x3-lattice-coverage-oracle/verification/results/lattice-coverage-oracle/convex/REPORT.md`, `FINAL.json`, `quality.json`,
phase images and line profiles. No production edits or Wine/game execution
accompanied the corrected oracle.


## 20. Current-HDR admission and prediction: bounded negative result (2026-09-20)

The run177 6392–6423 host test establishes **zero provable production admission**
on the fixed 2,154 material/1,406 lattice points and rejects the declared 5×5
two-color predictor on observed held-out RGB. No corrected-color TAA replay,
production change, build, Wine run or install followed. The accepted analytic
coverage oracle is unchanged; its 74.82%/75.29% geometry-RMS reductions do not
supply an actual-RGB or TAA result.

The new host helper retains node/material/original-face center IDs and uses
actual same-frame HDR, exact-face planar charts, fixed 1/(1+r²) weights, at least
three finite samples per class, and a global 20% spatial holdout excluded from
all training neighborhoods. Both nonempty observed foreground/background classes
must independently pass luma MAE<=1/p99<=4 display codes through existing
AgX+RCAS at captured exposure. Depth-compatible modeled opposite-face pairs fail:
foreground 309 observations 18.67/106.96; background 132 observations 14.62/92.13.
The separately labeled UNKNOWN sentinel-background proxy also fails:
foreground 5,824 observations 23.13/130.16; background 6,960 observations 9.20/53.71.
Last 16 fixed-support diagnostics fail as well. These are prediction residuals,
not temporal RMS, isolated radiance ownership, a numerical quality upper bound,
or a shaded subpixel RGB reference.

Independent review reproduced frame 6401 pixel (799,173): raw HDR
[.0747681,.0664673,.0651245] versus a prediction
[3.215877757,2.755744485,2.819967831] from three training samples on the same
node 491873968 / group 18 / face 47, yielding 237.07718733 displayed luma codes error.
This is variation within one model-assigned chart, not proof of a shared
hardware fragment owner. Subsequent reconstruction also places face 42 at the
dark center within the same depth tolerance; the modeled face-47 edge is only
0.00270 pixels away. Texture/alpha, raster ownership and compositing remain
unresolved. Reviewer required separate class gates; they
were fixed and all 32 results regenerated. Source/evidence and final report review
passed, including independent class-metric recomputation and synthetic checks.

Concrete missing evidence remains: (1) runtime-binding-matched texture contents
and complete shader/vertex-alpha evaluation over relevant UV/LOD footprints for
alpha-tested groups 4/6/18; (2) reconstructed geometry or conservative color/hazard
bounds/order for the 41–46 other scene draws per frame; (3) group 21 source-over
glass/depthless composite attribution, since it writes no depth; (4) established
background ownership and hidden-layer validity. A depth sentinel proves none of
these. Existing archives may supply textures once matched to captured resource
bindings; capture currently records texture identity/format/mips and buffer
revision/status, not texture/VB/IB content payload. These are precise input gaps,
not a request for another flight.

Latest complete 32-frame helper run 33.838554 host CPU seconds; recorded helper
compute 71.347974 s including pilot and superseded pre-class-gate pass, plus 3.25631 s
for a separate state audit, one worker. No GPU cost or game-FPS claim. Local
report and compact metrics:
[REPORT.md](/tmp/x3-lattice-coverage-oracle/verification/results/lattice-coverage-oracle/rgb-admission/REPORT.md),
[summary.json](/tmp/x3-lattice-coverage-oracle/verification/results/lattice-coverage-oracle/rgb-admission/summary.json).
Helper: `/tmp/x3-lattice-coverage-oracle/tools/analysis/lattice_rgb_admission.py`.
Reproduce with `python3 tools/analysis/lattice_rgb_admission.py`; generated data
remain under the existing ignored `verification/results/lattice-coverage-oracle/`
directory.

## 21. Conditional GPU owner/alpha/depth witness (2026-09-20)

A standalone D3D9 fixture now measures a seven-triangle source subset with the
captured vertex program/constants and its alpha dependency slice. It preserves
centroid/partial-precision modifiers and the production motion/depth export.
The installed run48 source `b698c32c` restores native mip bias for this exact
alpha-tested pair, supporting the fixture's bias 0 despite the global -0.5 option.
Source DDS contents/wrap and reconstructed FP16 vertex bytes remain conditional;
zero depth/slope bias, solid fill and disabled scissor/clip planes are explicit
uncaptured conditions. This is not complete original pixel-shader execution.

The owner run passes **48 tile observations**, three repeats before Reset and
one after, plus alpha-zero/one attachment controls. Independent source/evidence
review and seven host tests pass. Bottle X3/arm64 with both required emulation
environment values 1 took **4.888868 seconds**, lock wait 0.000004 seconds.
The [compact record](../../verification/results/lattice-gpu-witness.json) binds
inputs, source, executable, observations and limits; raw observations remain local.

Combined subset coverage has 14 pixels: 12 assigned to face47 and two to face42.
Face47 owns all four named centers, with auxiliary-depth residuals **[-3,0,0,0]
float32 ULPs** against the game capture. Its candidate alpha values are
[0.757353,0.317157,0.472549,0.302451], all above ref1/255; enabling alpha test
changes none of the candidate tiles. Face42 rendered alone exactly matches the
dark center's captured depth.

Thus neither raster exclusion nor candidate alpha rejection removes face47 at
the dark center under these fixture conditions. The fixture reproduces the three
bright-center depth values but **does not reproduce the dark-center depth**.
Face42's exact match is compatible evidence, not proof of runtime ownership.
Missing runtime payload/state parity remains consequential; no RGB explanation,
moving-crawl correction, TAA quality, performance or native Windows acceptance
follows. Further work must resolve those inputs rather than tune this conditional
fixture to the four observed colours.

## 22. Existing clip-W capture separates the face hypotheses (2026-09-20)

The existing run177 depth image also retains interpolated clip-W in `.b/.a`.
A fixture-only extension changes one operand DWORD in each diagnostic pixel
shader to export the existing interpolator into the former write-marker lane. It adds
no instructions, registers, resources or C++ rendering changes. All **40,320
previous non-W values remain bit-identical** across the 48 repeat/Reset observations.
Eleven host tests and independent source/runtime review pass; the X3/arm64 run
completed in 5.725503 seconds. The [compact W record](../../verification/results/lattice-gpu-clip-w.json)
binds the new inputs/output and prior observation hash.

At dark pixel (799,173), captured W is **59843.8828125**. Face42 rendered alone
matches both captured depth and W bit-for-bit. Face47 gives W **59782.48828125**,
a difference of **-61.39453125 / -15,717 float32 ULPs**, alongside the previous
-3 depth-ULP residual. The other three centers match face47's depth and W exactly.
Thus these channels strongly separate the conditional face hypotheses, rather
than grouping both under a broad device-depth tolerance.

The combined fixture still chooses face47 at the dark center. Correspondence
therefore favors face42 in the capture, but does not establish the game's actual
owner or explain why the capture differs from the combined fixture. Existing
run177 files contain shaders
and rendered outputs, not actual VB/IB/texture payloads or all effective post-route
states. A bounded actual-draw evidence design is being prepared; no additional
flight, production capture path, RGB correction or TAA acceptance follows yet.

## 23. Opt-in post-route state observation (2026-09-20)

The next bounded diagnostic captures **state only**, after `before_draw` and
before the unchanged original indexed draw. It adds no resource Lock/LockRect,
VB/IB/texture payload, writer ledger, render target, draw or readback. Its packet
always states `draw_input_coherence=unqualified` and
`payload_copy_valid=not_attempted`; equality with fixture state cannot establish
payload identity, fragment ownership, absence of later writers, RGB parity or a
crawl correction. The existing F8 image path is unchanged.

Enable with `--lattice-state run177_panel_position_v1 --object-trace` (equivalent
explicit DLL option `X3M_LATTICE_STATE=run177_panel_position_v1`, with the object
observer actually active). The launcher requires the scope option and clears
inherited lattice settings when the flag is absent. The DLL checks active scope
observation when arming, and refuses unavailable/failed scope reads. A rising F8
edge arms one frame per device, independent of the ordinary F8 multi-frame count;
automatic capture-start frames do not arm it. No new flight is requested by this
implementation.

The pinned selector uses original VS `4944d81dfe531b37` and PS
`5e0a10fe752b6140`, model `0x54b3`, LOD0, node-position raw words
`00bc6871,002dd083,ff415956`, and the six-element declaration (five FLOAT16_4
entries at offsets 0/8/16/24/32 plus END), stream0 stride40/offset0/frequency1.
Its two TRIANGLELIST argument signatures are `(base,min,count,start,primitives)`
`(0,0,9680,0,3784)` and `(0,0,1267,0,940)`. The position is identical in retained
frames 6392/6401/6423 and distinguishes all 16 panel instances in frame 6401.
Neither old draw indices nor a stale node pointer/resource ID selects a draw.
Observed session/node/handle equality only checks that the two same-frame matches
belong to one scope. A different save/object, changed shader/layout, unavailable
scope or duplicate/partial match does not fall back to another object. Publication
waits for the end of the entire frame; ambiguity invalidates the whole request.

`lattice_state_capture.{h,cpp}` keeps fixed CPU storage: two records, 256 fields
and 40000 DWORDs per record; effective shader programs are limited to 64 KiB each
(256 KiB total). At most 64 argument-signature candidates receive object-scope
selection reads; only two matches receive full effective-state queries. At most
16 stream slots, six clip planes, four MRTs plus depth, and 32 levels for each of
s0/s3 are described. There is no byte access to those resources. The x86 packet
allocation is 395928 bytes, including a separate 64 KiB selector scratch area
(compile-time ceiling 448 KiB), created only at an armed
F8 edge, and JSON is limited to 1 MiB. Per-query HRESULTs distinguish unavailable
values from zeros and unbound optional targets. Exact float bits, including
signed zero/NaN payloads, use eight-digit hexadecimal words.

The record includes original/effective shader linkage, raw effective shader
words, declaration, draw arguments, current object evidence, route flags/result,
full supported F/I/B constant banks, viewport/scissor/clip planes, raster and
MRT write/depth/bias/stencil/alpha/blend/multisample/wrap states, stream/index
binding descriptors, effective target/depth descriptions, and all 13 defined
sampler states plus texture type/LOD/level descriptors for s0/s3. Resource IDs
are read only if existing capture private data supplies them; this path never
assigns an ID and does not authenticate content. Pointer words are momentary
binding observations, not stable allocation or object identities.

Effective RT getters use the device's saved original slot 38. Calling the hooked
application getter here would restore lazy MRT bindings and return the logical
HDR target, changing the state being observed. All new draw-time getter work runs
inside `call_preserved`; the existing incoming/outgoing draw boundary and original
slot 82 call remain intact. Getter references are released within each observation.
Reset cancels the request; a shared CPU packet pin and a reentry guard prevent a
nested Present from publishing/freeing an active record. No COM object is retained
in the packet. The default-disabled draw path has one request-pointer branch;
compile-time null specialization eliminates the observation calls/envelopes.

Present writes `lattice-state-<pid>-<device>-<frame>-<generation>.json` through a
staging file, then logs its exact name, byte count and success. No file I/O or
formatting is added to the draw observer. `snapshot_x3_run.py` now preserves this
referenced file under the normal `/tmp/x3-bottleX3-runNN` workflow, including
explicitly refused observations, with basename/identity/size/session-window checks.
`verification/probe/lattice_state_packet.py --require-complete <file>` validates
selection, field coverage, bounds and successful submission before accepting a
complete **state observation**; it never promotes payload/coherence claims.
Query QPC ticks are reported separately from file output. Public API call duration
is not bounded by a cancellable timeout. Object tracing remains a session-long
prerequisite with its own hook/read cost; this is not wholly F8-only overhead.

The ratified [capture boundary](/tmp/x3-lattice-capture-boundary.md) and
[copy-serialization audit](/tmp/x3-lattice-copy-serialization.md) keep payload
integration stopped. In particular, LOD+0x3c is published before the builder;
final CloneMesh allocation linkage and callback/Reset access exclusion remain
unproved. Observed serials and readable-managed creation cannot replace the
missing copy-access authority. The state path requires neither the finite-upload
option nor new ownership/admission machinery.

Focused host checks cover selector uniqueness/refusals and capacity arithmetic,
packet completeness/raw-bit round trips, the extracted production draw hook's
ordering/unchanged dispatch and CPU/LastError model, explicit launcher prerequisites,
and normal collector success/stale/size/path refusal. The standalone
`lattice_state_fixture.cpp` also links the actual helper/read-only ID query and
extracted production draw hook against fixture-owned public COM endpoints. Its
12 cases exercise accepted/nonmatching/duplicate selectors, failed getters,
suppressed and failed draws, Reset, getter reentry, balanced resource/container
references, and the last shared packet pin dropping after nested publication
inside native dispatch. Actual `cpu_state.h` x87/MXCSR/LastError envelopes are
checked; captured selector shader inputs remain external local files. This is a
mock-interface fixture, with no device creation or rendering. The owner-run X3
fixture passed 250 checks in 4.091 s, with 12 packets (2 complete, 10 refused),
zero calls through the logical RT getter and zero forbidden resource operations.
The [runtime record](../../verification/results/lattice-state-observation-runtime.json)
binds the executable, inputs and results. Independent deep review cleared the
frozen source and runtime evidence; this does not establish live-driver behavior.
Cross-compilation covers the
helper, capture integration and read-only ID query with the project x86 SSE2 and
four-byte incoming-stack flags. The clean integration DLL at `d9ddca88` builds;
its linked no-x87 audit passes 95 roots / 539 reachable functions / zero violations.
The audit initially mistook generated nested thunks for the draw-hook root; the
reviewed symbol-selector correction preserves the call-graph walk and rejection
of real ambiguity. Native Windows runtime and live-game behavior are unverified. The first full discovery exposed a synthetic
Reset fixture missing the new diagnostic member; its reviewed compatibility fix
retains all prior checks and adds 12 cancellation assertions (42 scenarios,
218 checks). Full discovery completed 2,372 tests in 677.347 s with that sole
failure; the repaired affected test passes. A full rerun remains a candidate gate.

Affected command:
`PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_lattice_state_capture verification.analysis.test_snapshot_x3_run`.


## 24. Expanded retained-tile correspondence (2026-09-20)

A fresh host check compares all fourteen covered pixels in the retained 12x10
GPU tile against run177 frame6401. Twelve match combined depth and clip-W
bit-for-bit; two match face42 rendered alone. In addition to (799,173), the
new witness at (796,175) has captured W60085.44921875 versus combined/face47
W60014.0703125, a -71.37890625 difference and -4 float32 depth ULP. All eight
neighboring integer-coordinate shifts produce zero W matches out of fourteen.
This argues against a whole-pixel readback offset; it does not exclude fractional
coverage, runtime geometry or subsequent writers.

All 48 retained tiles agree on depth/W/conditional face correspondence across
arms, repeats and Reset. The candidate alpha-tested face47 survives both disputed
pixels (alpha0.7573529482/0.7664215565, threshold1/255), so simply adding that
already-tested alpha slice does not explain the discrepancy. Captured sun-share
is zero at the four face42-corresponding pixels and about0.948–0.995 at the ten
face47 controls. This is correlation, not a causal mask or proof of fragment
ownership.

Parent decision: retain the two discrepant pixels plus twelve exact controls
for interpreting §23's planned state packet. No additional capture field, new
flight, alpha threshold change or live payload copy is justified. The existing
packet covers the missing effective shaders/constants, raster/depth/stencil and
sampler addressing/LOD state. It cannot establish sampled runtime alpha, actual
VB/IB/texture bytes, coherent draw inputs or later writers. Equal state would
leave those alternatives open; RGB/TAA acceptance remains unchanged.

Reproduction: `python3 /tmp/x3-lattice-fresh-discrepancy.py` passes; its finite,
input-hash-bound result is `/tmp/x3-lattice-fresh-discrepancy.json`. Inputs are
`/tmp/x3-lattice-gpu-run-v5/observations.jsonl`, the v5 input manifest and
`/tmp/x3-bottleX3-run177/depth_1_6401.rgba32f`. The detailed state/alpha mapping is
[/tmp/x3-lattice-fresh-discrepancy.md](/tmp/x3-lattice-fresh-discrepancy.md).
No new Wine execution or production change was needed; native Windows remains
unverified.


## 25. Next-flight state decision and reopening gate (2026-09-20)

**Parent ratified:** §23's post-route state packet is sufficient for the next
bounded observation. Add no fields, payload copy, hook sites or flight sessions
before interpreting it. Keep the already queued at-rest, camera-rotation and
ship-translation F8 cases in [Run53](../verification/user-runs.md#53-spatial-fog-and-moving-lattice-state--ready-for-flight).
One F8 edge arms one state frame, independent of the 32-frame image burst.
Preserve §24's two discrepant pixels and twelve exact controls; the alpha-survival
and sun-share observations do not prove fragment ownership or justify a motion fix.

`capture.cpp::draw_indexed` calls `Capture::effective` after `before_draw` and
before saved slot82 submission; saved slot38 queries effective RT bindings.
The existing packet covers the effective shaders/constants, geometry descriptors,
raster/depth/stencil, viewport/clip and s0/s3 sampler/LOD assumptions that remain
missing from the conditional fixture. Validate each returned file with
`python3 verification/probe/lattice_state_packet.py --require-complete <file>`
and match its device/frame/generation and logged filename to the preserved
session. Both unique selected draws must submit successfully. Preserve an
unavailable, partial, ambiguous, Reset or nonmatching packet as a refusal;
do not substitute another object or treat it as state agreement.

If effective state differs, report exact differing words and their shader/state
use, then reproduce that difference in the conditional witness before proposing
a renderer change. If state agrees, runtime VB/IB/texture content, coherent input
lifetime, fractional coverage and subsequent writers remain open. Equality does
not establish payload identity, live face ownership or RGB/TAA parity. A payload
or writer diagnostic requires separate access/lifetime/coverage evidence and
parent ratification; the missing final CloneMesh linkage and copy-access authority
are not supplied by these state observations. Camera-rotation crawl remains open;
the accepted stationary improvement is unchanged.

No added hot-path cost follows from this decision. Existing cost remains the
request-pointer branch when unarmed, at most 64 candidates/two full observations
when armed, a 395928-byte packet and 1 MiB JSON cap; getters have no cancellable
wall-time bound. Query ticks exclude file output, and object tracing costs persist
outside F8, so this session does not measure production FPS. Existing documented
D3D9/Win32 queries and CPU/LastError boundaries remain; mock-interface qualification
and cross-compilation do not establish live native Windows execution. No new
fixture or flight is required for this documentation-only decision.

## 26. Run53 A received: state-packet validation (2026-09-20)

Run193 contains three 32-frame bursts (4558–4589, 5257–5288 and
8691–8722), with a state packet at each first frame. Each packet selected both
intended draws uniquely and records successful submission. The user has supplied
the capture path, without a new visual verdict; moving-crawl acceptance is unchanged.

The initial host checker rejected all three packets because the driver reports
`MaxUserClipPlanes=8`, while the producer deliberately records at most six plane
equations. All six selected draw records have `D3DRS_CLIPPLANEENABLE=0` and
successful equations for planes 0–5. The reviewed host-only correction preserves
the raw capability, requires every equation within `min(capacity,6)`, and rejects
any enabled plane outside that captured range. Capacity is bounded by the 32-bit
enable mask. This qualifies the observed effective clipping state without claiming
that uncaptured equations are known. No DLL change or repeat flight is needed for
this parser correction.

All three packets now pass `lattice_state_packet.py --require-complete`.
The affected host module passes 11 tests (4.963 s); the added capacity-32/33
boundary check passes in the focused method (0.016 s). Independent review cleared
source and actual packets. Effective-state comparison against the conditional
replay remains the next step; payload identity, writer ownership and RGB/TAA
benefit remain unqualified under §25.

The [receipt witness](../../verification/results/run53a-triage/state-witness.json)
reproduces all three packets. Camera log `t` is view-matrix translation, so its
change during rotation is not proof of ship translation; the later two bursts
remain motion-mode unclassified from those scalar differences alone.

The subsequent [source/input comparison](/tmp/x3-run193-state-interpret.md)
finds all six effective vertex programs byte-identical to the conditional fixture
and 108 explicit sampler words equal. The effective pixel shader preserves the
fixture's diffuse-alpha equation. View/jitter/history constants and RGB light-map
gain differences do not establish an alpha rejection change. The actionable
distinction is that all six packets report RT1/RT2 unbound through saved native
slot 38, while the effective pixel shader exports motion/depth to those targets.
`submission_error=8876086c` is an unused route default when submission is enabled,
and `rt_set=rt2_set=0` is normal in lazy mode; neither establishes a failed draw.

Parent-ratified next step: a standalone real-device probe of getter/resource-release
callbacks and MRT state before/after the observer. `release_device` can restore
lazy bindings during reference accounting; whether diagnostic references actually
trigger that path remains unproved. Count actual callbacks first, including bound
resources with and without an external application reference, before changing
production code. An injected mock callback alone cannot establish backend causation.
This is an offline diagnostic investigation, not a request for another flight or
authorization for live geometry copies. No moving-crawl fix is selected yet.

## 27. Bound observer reference callbacks and device lifetime (2026-09-20)

The real-device callback probe resolves the first part of §26's hypothesis.
Its original X3 run exited successfully; the host checker initially misclassified
36 missing private IDs because `query_resource_id` maps `D3DERR_NOTFOUND` to
`S_FALSE`. The narrowly corrected checker accepts that success code only for
identity queries. The original failed wrapper result is retained; offline
`/tmp/x3-lattice-observer-release-run-v1/checked-v2.json` validates the unchanged
executable and observations. With creation references retained, effective queries
produce zero device Release callbacks. With creation references dropped while
resources remain bound, the unmodified helper produces 11 AddRef/Release pairs,
both before and after Reset. Its own MRT sampling contributes another 290 Release
callbacks per dropped-reference case, explicitly separated from helper evidence.
Count-only hooks preserve MRTs; that probe does not establish production flushing.

The bounded production correction adds a capture-mutex-owned per-device observer
query nesting count around `Capture::original` and `Capture::effective`. The
existing full CPU envelope includes their complete getter/alias-release work.
`release_device` skips retention/reference inference and binding restoration
while this count is positive, but always forwards the real native Release and
retains ordinary destruction handling. The count ends before `before_draw`,
native submission and `after_draw`; the native draw is not enclosed in a new
observation scope.

Each candidate query acquires one saved-native-slot1 device reference and shared
CPU `Device` ownership. An optional holder declared before the outer draw lock
keeps both through submission, renderer cleanup, logging and `CallTimer`
destruction. It drops the native pin through normal `release_device`, then its
CPU owner, inside `call_preserved` after the outer lock. No subsequent statement
accesses `Device`. The native pin prevents reentrant last-application-reference
retirement during queries; CPU ownership alone would not. Normal final pin drop
can flush lazy targets after the draw, which is accepted diagnostic overhead.

Reentrant Present during an observer query invalidates the packet and returns
`D3DERR_INVALIDCALL` before media-root maintenance, `before_present`, native
Present or frame advance. Reset/ResetEx invalidates the packet and refuses at the
injected-operation boundary before renderer teardown or native Reset. Both use
the existing packet invalidation status `reset` as cancellation; refusal does not
mean a native Reset occurred. Ordinary Present/Reset outside queries retain their
existing behavior. The packet's independent CPU pin/reentry rules remain.

Unarmed draws acquire no shared/native references and allocate nothing. The
existing observer branch remains, plus the optional holder's null-pin destructor
check. Ordinary Present gains one exclusion read; only an already-held capture
scope enters the extra recursive lock check. An armed request can pin **65**
candidate draws: candidate 65 is pinned before `original` rejects the request's
64-read capacity. There remain at most 64 selector reads and two full state
observations. Each pin can incur a post-draw lazy flush. This is diagnostic cost,
not measured gameplay FPS; narrowing it requires a separately designed selector
split rather than assumptions about engine/resource lifetime.

The focused `latticeguard` mode in the real motion-output fixture links actual
capture, MotionOutput and Capture::effective implementations. Only fixture
selection/checkpoints and the explicitly labelled old-control guard bypass are
substituted. It compares observer off/old/guarded, lazy/per-draw routing, held/dropped
bound declaration references and ordinary Reset/recreation. MRT aliases remain
externally held through native submission, with any sampling-induced device
Release counted separately. Native arguments, result, CPU/LastError and MRT
checkpoints are measured; motion/depth readbacks test actual writes and an ordinary
subsequent draw tests recovery. Direct nested API calls are deliberate refusal
injections, distinct from real declaration Release callbacks. Getter failure and
unbound-index native failure test cleanup; two extra devices exercise last
application Release during queries with renderer references absent/present.
Another fixture device remains alive, so those cases qualify target retirement,
not process-last-device profiler shutdown. Extracted hook tests assert final pin
drop after the outer lock/timer and shared CPU ownership through native retirement.

The fixture uses its own full-size saved-dispatch table copy; it never patches
the backend's native vtable. Live-device disarm restores the borrowed pointer
before freeing the copy. Retired-device disarm occurs after the draw/pin/CPU owner
have returned. Early development failures are retained: v1 omitted the mode's
seam admission; v2 attempted a write to the borrowed native table; v3/v4 exposed
unequal predecessor history in the fixture. The recovery draw duplicates the
rigid key and poisons the next frame's history; v4 shows all 16,384 motion words
changing from valid history to sentinel while all 4,096 depth words agree.
One unique predecessor frame before each arm equalizes that input. Full auxiliary
comparison remains strict; no production history rule or numerical tolerance changed.
In v5, topology zero did not produce the assumed native failure. The final case
unbinds the index buffer and records the actual native `D3DERR_INVALIDCALL` while
preserving caller/native argument and result equality. All failed runs remain
local alongside the accepted v6 run; none were overwritten.

The [compact acceptance record](../../verification/results/lattice-observer-guard-2026-09-20.json)
binds the owner-run v6 results to the frozen fixture EXE/DLL and retained shader
hashes. Both per-draw and lazy runs pass 415 checks, 37 restoration comparisons
and 36 frames each: 24 matrix arms, 16 nested/failure cases and four final-release
cases total. In each old/dropped arm the real declaration-release callback causes
one production restoration; lazy RT1/RT2 disappear and center depth remains the
`-1` sentinel. Guarded/dropped arms retain that real callback but perform zero
query restorations, preserve MRTs at native submission and write depth `0.5`.
Observer-off/guarded full motion/depth readbacks and ordinary subsequent-draw
outputs agree. All matrix arms report zero sampling-induced device Releases.

The private seam uses clean support objects from `2b81d12c`, with the reviewed
guard capture delta based on `3e9b7076`; it is not an install candidate. The X3
bottle reports arm64 Wine with `FEX_X87REDUCEDPRECISION=1` and `WINEMSYNC=1`.
Runner time is 6.987 s; lock wait is 0.000003 s and child elapsed time 7.050 s.
The unchanged seam DLL's linked audit passes 95 roots / 548 reachable functions /
zero violations. Focused host checks pass 15 tests in 7.594 s, including the real
extracted draw (61 checks), lifetime fixture (46 scenarios / 246 checks) and
arm/disarm against a read-only native dispatch page. Independent deep review
found no remaining source/runtime blocker. These are diagnostic timings.

The fixture deliberately bypasses game selection. Direct nested API injections
exercise early cancellation before `effective` sets its busy flag; the existing
helper host tests separately cover busy packet storage. Windows-compatible
source/cross-compilation and X3/arm64 Wine evidence do not establish
[native Windows execution](platform-portability.md#2026-09-20-lattice-observer-query-reference-guard).
Neither the
callback mechanism nor this correction authenticates historical Run193 ownership
or its first destructive callback, Run177 payload/fragment correspondence, or a
moving-crawl/TAA improvement. No new flight is requested by this checkpoint.

## 28. External thin-geometry techniques and bounded next audit (2026-09-20)

The user requested an Astra/high design investigation of other engines. The
[parent-ratified report](/tmp/x3-thin-geometry-external-design.md) compares primary
AMD, Epic, Activision, Valve and NVIDIA sources against §§15–27 and inspected
shader code. AMD FSR2 protects thin-feature history with expiration and
visibility/shading invalidation; Epic TSR documents dense parallel subpixel lines
as a difficult case. These mechanisms do not reopen the failed stronger-history
recipes without new accumulated-history validity information. Source coverage
remains the leading local candidate, with conditional history protection second;
material filtering and authored content/LOD changes address separately established
contributors. No displayed-RGB benefit follows from the 75% geometry metric.

Parent selected one offline coverage-conditioned RGB/resolve attribution audit
using existing run177 data and fixed supports, preserving unknown ownership and
the two discrepant/twelve control pixels. It compares source HDR, ordinary resolve
and display variation and reports clamp/rejection contributions without fitting
another foreground/background predictor. The work is isolated under
`/tmp/x3-lattice-stage-attribution`; no production change, live payload copy or
extra flight is authorized by this experiment. Empty/unstable strata are a valid
negative outcome. Run54's corrected state capture remains the selected flight
step; neither it nor the offline audit promises a crawl fix.

## 29. Existing-capture stage attribution: bounded negative (2026-09-21)

The §28 audit is complete. Its [compact record](../../verification/results/lattice-stage-attribution-2026-09-21.json) binds the local scripts, reports and input manifest;
[method and limitations](/tmp/x3-lattice-stage-attribution/METHOD.md) remain local.
Ordinary replay reproduces crop-wide 31-frame dump MAE of 0.1678813085 codes.
Tracked RMS is 6.7132120046 codes on the unchanged 2,154/1,406 supports over
frames 6408–6423.
Comparable material-tracked AgX RMS is 29.845104 before resolve, 6.337852 after
resolve and 6.713212 after RCAS. TAA reduces this measured variation; these
metrics neither identify its remaining cause nor establish a shaded subpixel
reference. Per-step variance covariance terms are algebra, not causal ablations.

The loose material partition has 66 transition, zero stable-interior and
34,398 unknown point-frames out of 34,464; all 22,496 lattice point-frames are
unknown. The stricter partition is entirely unknown because its fixed 0.02
CPU clip-W screen rejects all twelve bit-exact GPU controls. Ten residuals are
0.05032–0.06690; two involve conditional face48/face42 differences. This is a
failed model qualifier, not evidence of runtime clip-W disagreement. Projected
edge margins also lack hardware qualification. Thresholds were not loosened.
The original fourteen-pixel witness retains twelve exact controls and the same
two discrepant pixels. Stationary/slow controls reproduce their own settings
but have no geometry oracle and remain unknown.

The checker passes 854 checks over 490 hash-bound input files. Final audit/check
CPU times are 55.959669/2.500942 s; independent reviewer checker CPU time is
2.451943 s. These are host analysis costs. Preserved host
matmul warnings coexist with zero full-crop/retained nonfinite values. Explicit
reactive-mask rejection is unavailable, so zero measured depth/other replay
rejections does not establish complete history validity. Review corrected the
same-face predicate to require all four taps in both frames; an independent
eight-tap reconstruction verifies it, and published partition counts are unchanged.

Close this bounded audit without a renderer patch or another RGB fit. Coverage
remains a candidate, not a demonstrated displayed-image fix. Unknown overlap and
background ownership prevent selecting a safe source-coverage or history-lock
implementation from this audit. Run54 B guarded state capture remains the intended next lattice evidence.
Run56 subsequently passed media stability by user report, releasing the crash
hold on this capture. It cannot retroactively authenticate Run177.

## 30. Run201 guarded state and bounded arithmetic qualifier (2026-09-21)

Run54 B returned three bursts starting at 9796, 10442 and 12010. The six
selected effective-state observations have RT1/RT2 bound (12/12), and all six
native submissions return S_OK. The prior missing-auxiliary-target symptom is
absent in these observations. The [checkpoint](../../verification/results/run201-lattice/checkpoint.json)
binds the host comparison and pilot; independent review reran both.

All six 586-DWORD VS programs match the retained fixture, as do 108 explicitly
set sampler values. Explicit raster state has no unexpected differences; the
fixture's RT0 mask15 versus live7 is intentional. The live PS retains the
conditional alpha slice, not full-RGB equivalence. Transform banks differ as
expected; the only PS-bank difference across the live packets is c217.w, the
distance-dependent RGB gain. Metadata does not authenticate texture or geometry
bytes. WRITEONLY requested usage does not establish readable native backing.

The packets still do not qualify a simultaneous input transaction or payload
access. Live observations do not independently sample attachments at native
submission, prove later-writer absence, or authenticate old Run177 ownership.
No production source or capture scope changes follow from this comparison.

A new pilot isolates arithmetic from full-model primitive selection using the
exact frozen seven-triangle payload and retained GPU primitive labels. Both
post-divide and fixture pre-DP4 jitter paths fail all twelve existing controls
at the unchanged absolute-W tolerance 0.02. Same-primitive comparison reduces
two earlier large errors to approximately 0.18/0.19; ten residuals remain
0.05–0.067. Supplying the GPU owner is a diagnostic condition, not an ownership
algorithm. Both historical GPU/capture exceptions remain separate unknowns.

The selected next action is a host-only shader/raster/interpolation qualifier
against the retained complete GPU tile, preserving negative pixels, the twelve
controls, both historical exceptions and all tolerances. Do not fit an offset or
loosen thresholds. No repeat flight or live resource copying is requested.

The [session triage](../../verification/results/run201-lattice/compact-report.md)
binds the session-header DLL/source identity and three strict-valid packets.
Owner/binding identities and observed lifetime bookends remain stable across
the selected records. The camera telemetry shows a stationary-orientation burst
and two bursts containing rotation; raw view-translation coordinates do not
by themselves establish ship displacement. RT1 RG stores previous UV, not
velocity, so raw RG magnitudes are not a movement measure.

### Frozen-subset qualifier result and next discriminator

The [independently reviewed qualifier](../../verification/results/run201-lattice/subset-qualifier.json)
returns UNKNOWN. Seven named arithmetic variants reproduce owner/support across
48 retained tiles, including 5,304 negative and 456 positive pixel instances per
variant. All positive depth comparisons pass the unchanged 2e-7 tolerance, but
every variant fails all twelve fixed W controls at 0.02. Maximum W residual is
0.1875–0.19140625. No offset, snapping grid or tolerance was fitted.

These are reconstructed frozen fixture bytes, not authenticated live buffers.
The two historical exceptions remain unchanged. The result does not distinguish
actual backend VS arithmetic from raster setup/varying interpolation, and cannot
reopen the rejected full-scene ownership or shaded-RGB models.

The selected next discriminator is a separate standalone GPU point probe: retain
the original VS bytecode, calibrate varying/MOV/readback transport using known
constant matrices, observe the frozen vertices, and bracket measurements with
the unchanged triangle cases including Reset recovery. Driver specialization
across primitive types remains a limitation. No game flight, live payload copy
or source-coverage correction is authorized by this measurement alone.

### Calibrated point probe completed; oracle still unqualified

The [independently reviewed point measurement](../../verification/results/run201-lattice/vertex-point-probe.json)
passes: 63 point records over 21 unique vertices, six exact calibrations, 48
baseline triangle tiles plus four post-Reset brackets. The standalone X3 fixture
exited successfully in 5.636 s under the shared Wine lock; no game was launched.
Four host checks pass. Independent review compares all 52 tiles, 6,240 pixels
and 49,920 FP32 lanes against the retained baseline, bit for bit.

The fused-DP4 model exactly matches all 126 exported vertex values. However,
substituting measured vertex Z/W still passes none of the twelve fixed W
controls (maximum absolute residual 0.1875 versus unchanged tolerance 0.02).
Owner/support agree and maximum depth error 1.78814e-7 remains below 2e-7.
Current clip X/Y are unobserved and remain modeled; point versus triangle
or pixel-shader linkage specialization is still possible. This narrows the
unresolved boundary to current XY, raster setup/interpolation or specialization;
it does not prove interpolation alone is responsible. No new arithmetic variants
or fitted thresholds are justified by this checkpoint.

The reviewed fixture and raw outputs remain local at the paths bound in the
compact result. Its legacy fixture dependency is not part of main. This is a
measurement checkpoint, not production code, live ownership qualification,
native Windows runtime evidence or a demonstrated crawl correction.

## 31. Close arithmetic exploration; investigate final upload observation

The two retained GPU/capture W fingerprints differ by 61.3945 and 71.3789,
far above the conditional model residual 0.1875. The old 0.02 qualifier remains
failed, but is not a prerequisite for distinguishing these finite hypotheses.
W alone supplies no bound on screen-edge displacement. The existing GPU data
cover only one 12×10 tile; repeating its fourteen comparisons cannot produce
ownership for the other motion frames.

A [bounded source-conversion pilot](../../verification/results/run201-lattice/rounding-pilot.json)
compares nearest/truncated half positions against the same frozen source.
It changes no owners among 120 tile pixels (14 positive), while disturbing ten
established controls. Face47's maximum projected vertex change is 0.00812 px;
face42/43 positions are unchanged. This is a conditional host screen, not proof
of D3DX conversion behavior. It does not justify another rounding Wine fixture.
The parent reproduced its assertions from the retained local script.

Selected next evidence path: investigate an opt-in mirror of final CloneMesh
VB/IB uploads, before game publication, rather than reading WRITEONLY buffers
at draw time. Target `0x004bcc2e` and handoff `0x004bc9c0`. Implementation is
blocked until targeted disassembly proves allocation linkage, producer-owned
readable mapped access, wrapper visibility, failure/unwind ordering and exact
ABI. Same-thread callbacks or matching revision bookends are not ownership
proof. Native bypass or absent exclusive access closes this proposed seam.

If qualified, a bounded startup-armed observer could retain bytes before original
Unlock, publish only after success, and attach them to F8 by actual allocation,
revision and generation. Later mutation invalidates the record. This would test
indices/order, splits, positions and UVs, without a per-draw copy or new Lock.
It would not by itself prove texture parity, a simultaneous draw-input
transaction, historical Run177 ownership or a visible crawl correction.

Public GPU attribute export is a possible alternative: WRITEONLY does not forbid
GPU reads. A diagnostic VS/ordinal stream can export decoded positions/UVs, but
not raw-byte identity or directly recover IB order; indexed ordinal attributes
are fetched through that same IB. It still requires an injected-draw ownership,
state/query and Reset contract. No live implementation or flight is selected yet.

### Prototype contract after targeted disassembly and review

[Final CloneMesh interval](../reverse-engineering/mesh-buffer-rewrite.md#final-clonemesh-private-upload-interval-2026-09-21)
establishes private fresh INDEX16 destinations before game publication. The
prototype retains opaque raw staging from original readable mappings; it adds
no buffer Lock/Unlock. This is safe-access evidence, not yet valid payload:
an internal attribute allocation may fail after both Locks and before index
stores, yet still reach destination Unlock. No interpretation, hashing, logging,
file output or publication may expose staging until CloneMesh, both original
Unlocks and final allocation/revision checks succeed. Failure wipes staging.
Legacy finite-value scanning must stay disabled for this mode.

A post-success READONLY-copy alternative also has private ownership, but its
additional mapping lifetime across reentrant/concurrent Reset is unqualified.
It is not selected. No blanket prohibition on locking lost MANAGED resources
is asserted. Implementation now targets the original mapping protocol in a
standalone actual-D3DX fixture, including failure, Reset, reentry and unwind
controls. The game callsite hook and F8 attachment remain later steps.

### Portable staging core checkpoint

The [CPU core](../../src/ownership/clone_upload_core.h) and its host fixture are
implemented and independently reviewed. [Results](../../verification/results/run201-lattice/upload-core.json):
39 cases / 596 assertions pass normally and under ASan/UBSan. Storage is a
preallocated 2 MiB arena plus 432 bytes of metadata. Exact-byte success, no
pre-publication exposure, private incomplete staging followed by failure wipe,
source/destination failures, stale identity/revision guards, duplicate/nested
scope refusal and a modeled CPU Reset barrier are covered. Failure wipe uses
non-elidable volatile stores.

The core requires authenticated facts and an externally held ownership registry
mutex; its guards do not prove those facts came from the game or public APIs.
It has no ownership-wrapper integration, CloneMesh/SEH adapter, EXE hook or F8
connection. Real COM/Reset, x86 state preservation and foreign unwind remain
mandatory next checks. No game snapshot, moving-crawl fix or native Windows
runtime acceptance follows from this host checkpoint.

### Readable-creation prerequisite qualified in the standalone fixture

The internal, default-off `prepare_readable_managed_uploads` option separates
readable MANAGED backing/immutable metadata from legacy finite-payload scanning.
It neither reserves the finite atlas nor runs its typed scanner, and rejects
conflicting finite/locked-prefix options. No CLI, game scope or live snapshot
is enabled by this prerequisite.

[Focused runtime evidence](../../verification/results/run201-lattice/readable-metadata.json):
157 checks pass on X3/arm64 under the shared Wine lock, in 8.036 s total
(7.890 s child, 0.00000375 s lock wait). Independent source/runtime review
verifies all 23 build dependencies, PAGE_NOACCESS forwarding without scanning,
exact native Lock/Unlock counts, application/native descriptor separation,
preserved payload bytes, four rollback CPU-state controls, option refusals,
Reset S_OK and clean reference release. No game was launched or DLL installed.

This closes the readable-metadata prerequisite on CrossOver only. Actual
CloneMesh event authentication, callback/reentry lifetimes and new-scope ABI
cleanup remain to implement. Existing wrappers promise ordinary COM returns;
foreign SEH escaping their admission TLS or callback-bearing registry sections
is not made safe by an outer snapshot cleanup handler. No global exception
retrofit or native Windows runtime qualification is claimed.

### Manual call-boundary checkpoint

The [x86 boundary](../../src/ownership/clone_upload_abi.cpp) is implemented and
independently reviewed with emitted code and a synthetic original-call fixture.
It captures incoming computational x87 state, MXCSR and LastError before observer
or exception bookkeeping, restores them before the original, and preserves its
outgoing state/HRESULT afterward. A no-EH shell and one separately compiled SJLJ
helper manage native registration and C++ propagation. The helper records the
compiler-owned opaque registration through an object-local alias and uses its
declared unregister API; no Wine-private structure or runtime layout is read.
This is a qualified GCC x86 SJLJ toolchain contract, not a general compiler ABI.

[Evidence](../../verification/results/run201-lattice/upload-abi.json): 158 runtime
checks pass, including 12 original calls, 13 preparations, 9 finishes, 4 aborts
and 2 native unwinds. The standalone X3/arm64 run took 4.319 s (4.161 s child,
4.083 µs lock wait); the saved source/emission/runtime checker passes 24 checks.
The boundary uses fixed stack storage with no allocation or per-draw work.

The production public CloneMesh dispatch is inspected in emitted code; the
runtime fixture uses a synthetic original. Actual CloneMesh observer integration
is still pending. Abort eligibility starts before preparation and relies on the
observer's readiness tag and explicit ownership of temporary references. Outer
cleanup does not repair foreign SEH escaping inherited wrapper admission or
registry sections. No native Windows execution, game hook or F8 capture is
qualified by this checkpoint; the new sources are not wired into the DLL build.

### Actual manual CloneMesh upload observation qualified

The ownership event observer now stages raw bytes from the original readable
destination mappings, with no additional Lock/Unlock or draw. It publishes only
after successful native CloneMesh, both original destination Unlocks, closed
source mappings, final allocation/revision linkage and completed qualifier
releases. Failures wipe the preallocated arena. Later writes, trusted native
mutation, retirement and Reset invalidate retained records. Qualifier callbacks
run outside the registry; the final guard/copy runs under the same registry
that excludes native Reset start. The mode remains default-off and manually
armed; no game hook or F8 connection is installed.

[Actual fixture evidence](../../verification/results/run201-lattice/manual-upload.json):
421 checks pass across 13 sessions with exact-S_OK arm/disarm. Four exact-byte
cases cover both selected mesh sizes with unchanged and FLOAT4-to-FLOAT16_4
declarations. Source Lock failures, an ignored original destination Unlock
failure, WRITEONLY fallback, Reset/reentry/nesting/refusal, final Release
invalidation and copy CPU-state preservation are covered. A MULTITHREADED
device tests Reset on its creation thread: native Reset waits for the guarded
copy, then invalidates the old scope before publication. Both original Clone
and Reset return S_OK. Production compilation without fixture hooks also passes.

The final X3/arm64 run takes 6.039 s (5.903 s child, 3.25 µs lock wait). The
selected app-local native D3DX module matches the disassembly provenance. Earlier
default-loader runs safely refused a different lock pattern with zero staged
bytes; their implementation identity was not logged, so they are not a proven
builtin-versus-native comparison. Explicit native selection fixes fixture scope
without relaxing production checks or adding a DLL-hash prerequisite.

Independent review covers source, emitted production code and saved execution.
Cost is bounded to a preallocated 2 MiB store, metadata validation, one copy per
destination and failure wipes; no per-draw payload work is added. Native
Windows execution, injected native bypass/slot faults, actual private allocator
OOM and inherited callback-SEH recovery remain unqualified. This manual
diagnostic establishes upload capture only, not a simultaneous draw-input
snapshot, game integration or a visible moving-lattice correction.

### Game callsite adapter qualified in isolation

The [adapter](../../src/proxy/lattice_upload_hook.cpp) claims the complete
`[0x004bcc2b,0x004bcc30)` MOV/CALL span and validates its 31-byte surrounding
context. Its armed path saves the dynamic public COM target before observer
callbacks and forwards that exact target with the original five arguments and
stdcall20 stack contract. The separate manual API retains normal public virtual
dispatch. The disabled path restores incoming flags and runs the displaced tail
once without observer calls or allocations. Patch writes use the existing
install window, atomic claim and rollback ownership rules. Quiescent rollback
is tested; live concurrent unpatching is not supported.

[Combined evidence](../../verification/results/run201-lattice/upload-hook.json):
389 hook checks pass, with 23 original calls, zero wrong targets, four aborts
and two native unwinds. The affected manual ABI regression passes 158 checks.
Actual 34-byte stub and 10-byte tail readbacks agree on the continuation. Four
stack alignments, CPU/LastError/nonvolatiles, target mutation during preparation,
disabling an active scope and patch/rollback failure paths are covered. Armed
outgoing EFLAGS are not preserved; the validated continuation overwrites them
before use. Saved-evidence checkers pass 21 and 31 checks. X3/arm64 execution
takes 4.360 s and 0.285 s, respectively; these are fixture times, not game FPS.

Independent deep review passes source, emitted code and runtime evidence. This
checkpoint does not install the patch or wire it into the DLL build/launcher.
The remaining integration must balance the observer device pin, defer closing
an active Clone scope without waiting, and attach one fully validated CPU pair
to F8 after guarded getter releases. Duplicate-shape refusal remains intact.
Native Windows runtime, inherited callback-SEH recovery and moving-crawl quality
remain outside this checkpoint.

### Ownership pin lifecycle and atomic pair qualified separately

The observer now exposes a CPU-only pin query, a unique arm token, deferred
close, and one guarded vertex/index pair copy. Close refuses new observations
and wipes storage immediately; an active Clone finishes releasing its completed
temporary references before detaching the pin under registry and releasing it
outside registry. Zero/exhausted device identities refuse before pin acquisition.
A stale arm cannot close a later arm. Reset invalidates retained payloads.

Pair copying rechecks both weak resource keys, allocation/revision identities,
generation, dispatch and capacities before either copy. Metadata becomes valid
only after both copies finish under the same registry guard. It adds no native
Lock/Unlock, COM call or allocation. Duplicate selected shapes still refuse.

[Combined evidence](../../verification/results/run201-lattice/upload-lifecycle.json):
386 actual-wrapper checks pass, including 12 clean device sessions, five detached
pin drains, three deferred closes, two exact paired-copy cases, C++/native
boundary aborts and creation-thread Reset exclusion. The affected manual Clone
regression passes 421 checks. The final production object has no fixture symbols;
the three API shells preserve CPU/LastError without outer exception registration.
Independent deep review passes source, emitted code and saved runtime evidence.

This qualifies ownership APIs only. The actual Capture Release/Reset accounting,
F8 attachment and publication still require integration. Native Windows runtime,
inherited foreign-callback SEH and moving-lattice visual quality remain unverified.

### F8 payload reader and collector prepared

Schema 2 preserves the state observation and adds an optional fixed 466,224-byte
sidecar for both selected vertex/index pairs. New allocation/revision/arm
identities are pointer-free. The reader validates exact ranges, identity, byte
count, SHA-256 and INDEX16 bounds; `--require-payload` is separate from existing
state completeness. Schema 1 reading remains supported.

The collector authorizes sidecars only from matching successful log records,
checks the copied JSON/binary together, and removes unqualified copied binary
files while preserving diagnostic JSON. Reads are bounded during I/O, and
malformed nested objects produce validation failures with cleanup. Terminal
packet status invalidates both attachments with its exact reason.

[Independent review and 47 focused host tests pass](../../verification/results/run201-lattice/upload-packet-reader.json).
The production writer and Capture lifecycle integration are still pending; this
checkpoint does not create new game captures or qualify draw-input coherence.

## 32. Camera-relative gate and wide-box clip: host replay (2026-09-21) [M]

Step 1 of the pivot in [the approach review](lattice-approach-review-2026-09-21.md) section 6, host
only: no Wine, no game, no build, no shader or production edit. Tool
`tools/analysis/taa_lattice_gate_replay.py` (patches section 15's resolve oracle with the two
candidate terms), test `verification/analysis/test_taa_lattice_gate_replay.py`, numbers
`verification/results/lattice-gate-replay-2026-09-21.json`, witness crops (untracked)
`/tmp/x3-lattice-gate-replay/`. Four variants, all plain Catmull-Rom and W 0.97: **installed**
(own speed, gate 0.03-0.25, clip off), **gate_open** (gate closed by camera-relative speed, clip
off), **gate_open_box7** / **gate_open_box11** (same gate, retained history clipped to a 7x7 / 11x11
current min/max box). The global history-cut heuristics are CPU-side whole-frame discards that this
replay does not model at all, so every variant runs with both off as the Run57 defaults have them;
`taa_history=1` on every replayed frame of all eight bursts, so the seeded history carries no cut
either. The baseline reproduces section 15 exactly: run177 rotation tracked rms **6.7132**, gradient
**643.9**, support 2154 px, routed fit 0.00105 / 0.00242 px.

| burst | region screen / camera-relative speed p50-p90 | gate share installed -> open | tracked rms ratio | gradient ratio |
|---|---|---|---|---|
| run177 rotation 6392-6423 | 5.58-8.67 / 0.03-0.08 | 0.000 -> 0.199 | **0.567** | **0.721** |
| run206 slow pan 5730-5761 | 3.88-5.01 / 0.09-0.19 | 0.014 -> 0.428 | 0.943 | 0.797 |
| run206 fast pan 6936-6967 | 5.47-71.8 / 0.03-2.02 | 0.160 -> 0.230 | n/a (see below) | n/a |
| run201 10442-10473 | 6.55-33.6 / 0.29-2.14 | 0.000 -> 0.177 | 0.996 | 0.979 |
| run201 12010-12041 | 5.18-14.5 / 3.43-6.09 | 0.000 -> 0.000 | 1.000 | 1.000 |

The three box variants differ from clip-off by under 0.02 % in rms and gradient on every burst: on a
lattice the wide box almost never binds on real history, exactly as H2 (section 11) predicted for the
3x3. It does bind on injected stale history. Section 15's 6x6 patch at previous coordinate (950,115),
read at background (947,123), frame 6401: installed **46.6** codes, gate_open **236.7**, box7
**74.4**, box11 **115.0**. So a 7x7 box answers section 7's open question affirmatively - it bounds
the ghost to 1.58 x the clipped resolve - and the 11x11 box is worse, because a wider neighbourhood
of a lattice contains brighter extremes.

Against the section 6 acceptance: tracked rms passes (0.567 <= 0.80) but **gradient energy fails in
every gate-open variant (0.721 < 0.90)**. Section 15's 1.023 x gradient came with Keys -0.65; with
the plain Catmull-Rom required here, retained history under a 5-9 px/frame pan loses detail. The
controls also fail: run177 stationary is bit-identical for gate_open (0 of 13,671,000 values) but not
for the box variants (209,436 and 127,322 values, max 0.64 HDR), and run159 slow is identical for
none (2.58-2.63 M values, max 0.0625 for clip-off) because at 0.03-0.11 px/frame the camera-relative
residual is slightly larger than the screen speed, closing the gate a little more (share 0.464 ->
0.454, rms x1.011). run161's background trail does not regress (p99 0.076 codes against plain for
every variant). run206's slow pan adds a small trail the installed region does not have (p99 0.083 ->
1.558 codes).

Two limits on the evidence. The camera-relative gate is inactive exactly where the ship translates:
in run201 12010-12041 the camera-relative speed is 3.4-6.1 px/frame and the gate never opens, and in
run201 10442-10473 it opens on 17.7 % of the crop for no measurable gain. run206's fast pan cannot be
scored by material tracking at all (no material point stays inside a 350x315 crop over the 16 scored
frames at 5-72 px/frame); only its gate shares and trails are reported. The replay's installed
variant carries the thin region but not the far stabiliser, matching section 15's baseline rather
than the full installed set.

**Verdict:** option A as specified does not clear the bar. The mechanism is confirmed (opening the
gate recovers 43 % of the tracked rms on the one burst where registration is exact), and a 7x7 box
is the right ghost bound, but the quality it returns is blurrier history rather than a restored
lattice, and it perturbs the accepted slow-drift path. Section 15's ghost verdict stands for
clip-off. If the user's acceptance of "a little ghosting" is to be spent, it should be spent on a
variant that also restores gradient energy, which needs the sharper history kernel section 15 used
and this brief excluded; that is a new decision, not a sweep of this one.

### 32.1 Accepted in Run 59, the default with the thin region (2026-09-21): `--taa-thin-region-gate camera`

Flown as run207 (`screen`) / run208 (`camera`) on DLL `b1bb05fb`; the user accepts the camera gate for pans and it is
now the launcher and native-fallback default whenever the thin region is active, `screen` being the opt-out, with
`--taa-line-filter` resolving silently to `screen`. The roll residual is measured in section 32.2 below
(rotation-only path p50/p90/p99 0.030 / 0.069 / 0.092 px: the roll alone keeps the gate open;
[ledger](../verification/temporal-resolve.md)). The gate is selected by `X3M_TAA_THIN_REGION_GATE`, log field
`thin_gate=` on `motion_output_taa`; `screen` is the pre-Run59 installed gate. Orchestrator decisions: gate speed `min(screen speed, camera-relative speed)`, so every pixel
the installed gate leaves open stays open; the 7x7 box only where the camera term opens what the screen gate would
have closed; nothing else changes.

**Mechanism.** Three new programs, bound only in this mode; the eleven existing programs that share the edited
sources (ten resolve variants and the line mask) recompile to the same bytecode, headers and word counts (every
change sits under `X3M_CAMERA_GATE`; only source and tool hashes moved in their provenance). `line_mask_camera_ps.hlsl`
(**223 slots**; the line-filter test is compiled out) carries the gates as OPENNESS `saturate(1 - (speed - LO) * W)`
(r = screen speed, a = the larger of it and the camera-relative one), takes the 17x17 minimum of both and publishes
**b = camera-gated strength, a = screen-gated strength** (a <= b). Openness, not closure, so that a non-finite
speed reads closed (`saturate(1 - inf) = 0`; a NaN reaching the UNORM target is written 0) without a comparison a
compiler may fold.
`thin_box_ps.hlsl` (**51 slots**) draws the weighted-domain 7x7 min / max of the current colour into two owned FP16
targets by MRT (15.7 MiB at 1280x768, allocated on first use), skipping pixels with b <= a. `resolve_far_camera.hlsl`
(**508 of 512 slots**; `resolve_far` stays 496) reads them at s9 / s10:
`old = lerp(clip3, old, a * RELAX) + (b - a) * RELAX * (box7(old) - clip3)`. With a = 0 this is the replay's
`gate_open_box7`; with a = b it is `resolve_far`'s expression plus `0 * finite`. The 48-fetch variant inside the
resolve does not fit; a first form with a branch and a division measured 513 slots and was replaced. The line
filter's mask channel carries the second gate, so the two exclude each other: the launcher refuses the pair; the
route keeps the line filter and falls back to the screen gate with one log line
(`motion_output_taa_thin_region ... camera_gate_unavailable=1 reason=line_filter`, likewise `camera_program` when the
programs are missing); the pass API itself refuses such a run (`E_INVALIDARG`), which the route never issues. A
box-target allocation failure that is not a lost device falls back to the screen gate for the session (re-armed
by Reset). The box pair lives only while the camera gate runs: the first run without it (fallback, option off)
releases it, once per configuration change, never per frame. Documented D3D9 only; native Windows is
source-compatible and unverified.

**Fixture** (lattice mode, 495 numerical / 21 state; was 459 / 19). Installed mode: the 18 `THIN_REGION` rows and the
motion-start row equal the previous record character for character. Camera mode with a static camera at rest and at
0.12 / 0.30 px/frame object drift: colour, alpha, age and gate bit-identical to the screen gate (3 of 3). Pan scene
(full-width shards, camera and shards 0.5 px/frame along x, columns 18..28): gate share **0.000 -> 1.000**, shard
ripple **16.34 -> 1.42 codes rms (x 0.087)**, p2p 99.4 -> 6.6; shader = 2-D oracle (0.0093, bound 0.02), age exact,
both published gates = oracle to 0 codes. This scene is uniform along x, so the fractional history fetch loses
nothing: it proves the gate and the clip, not moving-lattice quality (section 32's gradient x 0.721 stands). A
bright independent mover (value 4, 2 px/frame) closes the gate around itself: no excess over the scene maximum after
it left, either mode. Stale 6x6 patch of value 4 written into the pass's history under the open gate, one frame
later: installed **0.623**, camera **0.727 (x 1.17, bound 2)**, clip-off estimate 3.64; four frames later 0.373 /
0.664; the oracle with the same injection agrees to 0.0083. Refusals, hostile c0..c7 / c22 / c24 / s8..s10 /
COLORWRITEENABLE1, failed box draw, Reset, box-allocation fallback (= screen gate bit for bit). Box domain: the
same stale patch with k = 0.5 and one current pixel of 65504 (not finite for the resolve) inside it: shader = oracle
to 0.0103 (box weighed as the resolve, bad tap skipped; the pixel itself resolves black on 27 frames). Non-finite
speed: a routed 2x2 object at 1e30 px/frame (the speed overflows in the mask program) closes both gates within 8 px
(0 / 0, plain mask 0) and the output equals the screen gate's bit for bit. A NaN correspondence is reported, not
asserted: this backend runs shaders with fast-math semantics and reads the gate open in BOTH masks (plain 1.0,
camera 1.0), so no in-shader NaN rule is verifiable here; the resolve itself rejects such a pixel's history.

**Pass time** 1280x768, CPU wall with event-query drain, 6 rounds, from the record
(`verification/results/bottle-X3/temporal-pass-summary.json`): thin region **+0.78 ms** over plain; camera gate on top
**+0.16 ms** with no region on screen (the box pass skips every pixel) and **+0.59 ms** with every pixel fragmented
and reopened by a 1 px/frame pan (worst case; real region share is 0.03-0.24). Spread over the seven runs of this
session: thin region +0.47 .. +0.98, camera without a region -0.05 .. +0.26 (not distinguishable from zero), camera
worst case +0.37 .. +0.65 ms.

**Replay cross-check** (host, the shipped rule patched into `taa_lattice_gate_replay.py` without 8-bit mask
quantisation): run177 rotation: tracked rms x 0.5667, gradient x 0.7207, gate share 0.199, stale patch 74.4 codes (installed 46.6, clip-off 236.7): identical to `gate_open_box7` to every printed digit (3 of 13,671,000 values differ). run177 stationary: **0 of 13,671,000 values** differ from installed (box7-everywhere: 209,436). run159 slow (0.03-0.11 px/frame): 54,776 of 13,671,000 values differ, max 0.0127 HDR, rms x 1.000, gate share 0.4645 -> 0.4645 (pure camera gate: 2.63 M values, x 1.011); the residue is where the camera term is slightly slower than the screen speed and opens the region further, which decision 1 intends. Record: `/tmp/x3-taa-camera-gate-v1/replay-shader-rule.json` (untracked).

**Flight.** `--taa-thin-region 0.97 --taa-thin-region-gate camera` at the run177 position: slow and fast pan against
the installed gate, judged on crawl against blur and on trails behind ships crossing the lattice.

### 32.2 Camera path made depth- and translation-aware; the forward-flight answer (2026-09-21)

Host only (no Wine, no build, no production edit). Tool `tools/analysis/taa_resolve_replay.py`
(`camera_previous_ndc`, the camera path of the replay and of the gate experiment), new mode
`tools/analysis/taa_lattice_gate_replay.py --camera-check`, test
`verification/analysis/test_taa_camera_path.py` (7 cases), numbers
`verification/results/lattice-gate-replay-run209-forward.json`.

**Tool correction.** The replay's camera path built a rotation-only far-plane ray from the logged
`camera_state` R and P and ignored the logged translation `t`, so on a forward burst it reported a
camera-relative residual equal to the raw screen speed. The path now unprojects each pixel at its own
depth (`z_view = m32 / (d - m22)`, assumed m22 = 1.000003 / m32 = -6.000018; `camera_state` does not
log them), takes it through the current view inverse and the previous view and projection, and is the
default; `CAMPATH=rotation` (or `opt['campath']`) keeps the old path. The unit test flies a static
point cloud with a camera translating 136 units/frame AND yawing: the new path lands on the
analytically projected previous position to **< 0.01 px** (max over 4096 points, also with non-zero
m20/m21), the rotation-only path is off by the parallax (median 6.5 px, at least 8.7 px inside 400 units)
and the two agree at the far plane to < 0.05 px.

**The installed shader has the same gap, and that is the real finding.** The `clip_to_previous`
`temporal_pass.cpp` uploads is `camera_far_plane_reprojection` (`src/renderer/camera_reprojection.h`):
its z column is zero and the translation is dropped by construction, so although
`line_mask_camera_ps.hlsl` does fetch the pixel's depth into `clip.z`, the installed camera-relative
speed IS the full parallax whenever the ship translates. The old replay number was faithful to the
installed shader, not to the physics; run209-forward under `CAMPATH=rotation` reproduces it exactly
(region camera-relative speed p50/p90 **1.050 / 2.721** px against screen 1.041 / 2.708, gate share
0.0001 = the installed screen gate, rms and gradient ratios 1.000).

**Routed truth** (static routed geometry: motion RG with alpha 1 *is* the previous position), ROI
(860,10,1260,290), residual p50/p90/p99 px:

| burst | camera translation | rotation-only path | corrected path |
|---|---|---|---|
| run209 roll 7799-7830 | 0.5-7.0 units/frame | 0.0295 / 0.0686 / 0.0923 | 0.0189 / 0.0375 / 0.0577 |
| run209 forward 4421-4452 | 109-136 units/frame | 1.3949 / 2.8327 / 3.3125 | 0.0325 / 0.0531 / 0.0606 |

The roll row reproduces the previously reported 0.031 / 0.070 / 0.095 within noise (511,298 routed
pixel-frames), so the rewrite did not disturb the case where the old path was already right. On the
forward burst the rotation-only residual equals the screen speed (1.385 / 2.819 / 3.299) to 1 %,
while the corrected residual is 0.03-0.06 px: **the camera path, not the gate rule, was the problem.**

**Answer to the open question (forward burst 4421-4452, thin ROI, 624,066 routed pixel-frames).** With
a correct camera path the camera-relative gate is wide open during forward flight. Corrected residual
by radial bin (5 quintiles of radius from the frame centre, px): 0.023 / 0.028 / 0.034 / 0.043 / 0.053
p50 (rotation-only: 0.80 / 1.17 / 1.48 / 2.16 / 2.83 - the radial expansion the gate was closing on).
By view-z quartile (26-33 k / 33-39 k / 39-56 k / 56-70 k units): 0.052 / 0.040 / 0.026 / 0.024 p50
(rotation-only 2.74 / 1.92 / 1.05 / 0.92: the parallax falls with distance, as expected). Gate under
LO 0.03 / HI 0.25 with the 17x17 fastest-neighbour minimum, `min(screen, camera-relative)`: crop-wide
open share **0.641 -> 0.741**, mean openness 0.641 -> 0.734; thin-region-weighted share (the tool's
`trg`, the number the section 32 table carries) **0.0001 -> 0.3108**. The 26 % of the crop that stays
closed is not the truss: only **0.72 %** of crop pixels exceed HI and every one of them is a routed
pixel with a *sentinel* depth (a lattice cell the cutout route keys), for which no camera path exists
at all - the 17x17 window spreads those 0.72 % over 26 %. Material tracking (support 11,658 px, routed
homography fit 0.0066 / 0.1103 px): tracked rms **7.7709 -> 4.0359 (x0.5194)**, gradient
**850.3 -> 586.4 (x0.6896)** for `gate_open`, x0.5195 / x0.6895 for `gate_open_box7`; background trail
against the plain resolve p99 0.187 codes. So the gain on a forward burst matches the pan bursts of
section 32 in both directions: most of the crawl rms goes, about 31 % of the gradient energy goes with
it.

**SETA x N.** Per-frame camera translation multiplies by N. Measured floor: one R32F step of the
captured depth, reprojected with the translation scaled x1 / x2 / x6 / x10, moves the prediction by
0.0006 / 0.0012 / 0.0036 / 0.0059 px (p50) - depth quantisation is not what limits the gate. The
corrected residual's translation-proportional part is a *tool* calibration error, not physics: fitting
one multiplicative view-z scale per frame drives the forward-burst residual from 0.0318 to **0.0097 px
at scale 0.979** (2.1 % z, inside the +-4 % that camera-state-and-frame-routine.md gives for zf
recovered from m22), while the roll burst is insensitive to it (0.0257 -> 0.0252 at the grid edge). *Superseded by section 32.4:* the scale was the replay's transpose acting on |t| ~ 1.2e5;
with the exact inverse of the float rotation the best fit is 1.000 and the forward residual 0.0012 px.
A shader holding the engine's live m22/m32 carries no such term, so its residual under SETA x N stays
at the 0.01-0.02 px floor plus the 0.006 px depth term - the gate stays open (*inference* from the two
measured bursts and the depth-step measurement; SETA itself was not flown). Even taking the tool's
2 % error at face value and scaling it linearly, the residual reaches 0.046 / 0.10 / 0.155 px at
x2 / x6 / x10, still under HI = 0.25, i.e. partially open at worst. For comparison, the SETA-scaled
*installed* rotation-only residual (same world points, previous camera moved back N x) is
2.76 / 8.18 / 13.46 px p50 at x2 / x6 / x10: the installed gate closes harder the faster the ship
flies.

**Baseline.** Section 32's run177 numbers are reproduced by the flagged old path to every printed
digit: `gate_open_box7` rms **x0.566680**, gradient **x0.720736**, gate share 0.1988, stale patch
46.6 / 236.7 / 74.4 / 115.0 codes. Under the corrected path as default the same burst moves to rms
**x0.5214**, gradient **x0.6852**, share 0.2136 (rms 6.7132 -> 3.5001), because that burst also
translates a little; it is not a regression but a different, more accurate measurement, and the
section 32 table remains the record of what the *installed* rotation-only gate does. Two limits carry
over: routed pixels with a sentinel depth have no camera path in either mode, and the replay's
installed variant still carries the thin region without the far stabiliser.

### 32.3 Production: the camera mask's depth and translation term, c8 (2026-09-21)

Not installed; no flight yet. `clip_to_previous` (c0..c3, `camera_far_plane_reprojection`) and
`resolve.hlsl`'s sentinel policy are unchanged. Only the camera mask program gained a constant.

**Construction.** For a pixel with NDC (x, y) and device depth d, view z = m32 / (d - m22), and the
previous clip position divided by that z is

    far_plane(x, y) + D * (d - m22) / m32,    D = (t_prev - t_cur R_cur^-1 R_prev) * B_prev

i.e. the far-plane image of the pixel's direction (what c0..c3 already gives) plus the
camera-relative translation between the two views over the pixel's view z. `camera_depth_parallax()`
(`src/renderer/camera_reprojection.h`) builds `(DX, DY, DW) / m32` and `m22` in double from the two
`CameraState`s the far-plane matrix is built from; `temporal_pass.cpp` uploads them as **c8 of the
camera mask only** (`FrameInputs::camera_depth_parallax`, fed by `motion_output.cpp` only with the
camera gate on and a valid far-plane transform; non-finite -> zero). `line_mask_ps.hlsl`
(`X3M_CAMERA_GATE`) adds `c8.xyz * (depth - c8.w)` to `previous` on a valid depth; the sentinel keeps
the far-plane path, exact for it. Jitter handling, `min(screen, camera_relative)`, the 17x17 minimum,
the box clip and the resolve are untouched. c8.xyz = 0 (no translation, no depth law, policy 1) is
the far-plane path bit for bit.

**Depth.** RT2 `.r` is ordinary device depth z/w of the routed draw (`current_depth_ps.hlsl`), so
view z needs only the projection's m22 / m32. They come from the latched `CameraState`
(`projection[10]` / `[14]`, the pair `far_gate` already uses; defaults 1.000003 / -6.0000184 =
zn 6, zf 2e6 per camera-state-and-frame-routine.md), not from a fit; the term is refused unless
m22 > 1 and m32 < 0. `d - m22` is formed in the shader from the same float m22 the rasteriser's
projection held, an exact float subtraction for d in [0.5, 1], so the +-4 % uncertainty of `m22 - 1`
as a number never enters. `camera_state` log lines now end with `p22= p32=` so a flight can confirm
the latch (the pair is per-submission scratch in the engine; a latch taken under a different
view's near plane would bias z by zn'/zn, and only a capture can rule that out).

**Precision.** The matrix product is never formed from absolute coordinates in float: D is the
difference of the two translations in double. It uses the exact inverse of the float R_cur, not the
transpose: R^T R - I is ~1e-7 on engine views and |t| reaches 1e6, so the transpose leaves ~0.1
unit of false translation (0.04 px at view z 200 in the host test) even between identical views.
Host test (`verification/analysis/test_camera_reprojection.py`
`test_depth_parallax_against_full_reprojection`): camera at (3.1e5, -2.1e5, 8.0e5) flying 136
units/frame with yaw, pitch, roll and off-centre m20/m21, view z 200 .. 1.5e6; the float32 shader
form against the double unprojection/reprojection through the same float views: **worst 0.00013 px**
(1920x1080). Against the replay's `camera_previous_ndc` (section 32.2's rule) near the origin:
**0.00018 px**, so the shader's rule is numerically the replay's construction; the expected flight
behaviour is therefore section 32.2's (run209 forward: thin-region share 0.0001 -> 0.31, tracked
rms x0.52). The replay itself was not rerun. What remains is the engine's own float quantum of t
(0.06 unit at 1e6), shared by every static object drawn through the same view.

**Shader.** `temporal_line_mask_camera` 897 -> 920 words, **223 -> 226 slots** (bytecode
`cf7c1764...`). The other 60 generated headers are byte-identical (sha256 before/after);
`temporal_line_mask` was regenerated because the shared source changed: bytecode `a44bfebd...`
unchanged, only its manifest's `source_sha256` moved.

**Fixture** (`temporal_thin_region_inc.h`, scene "forward"; X3, `THIN_REGION_CAMERA_FORWARD`): camera
advancing 0.225 units/frame, no rotation (identity far-plane matrix, static sentinel background),
the 32 px window an off-axis crop (m20 = -100) so the expansion field is uniform to 1 %; static
routed shard rows at depth 0.99 (z 600, 0.600 px/frame) and 0.995 (z 1199, 0.300 px/frame), both
past HI. CPU residual of the c8 form against the analytic path 0.0002 / 0.0001 px, of the routed
rows 0.0058 / 0.0029 px (the crop's non-uniformity). Screen gate share 0, rotation-only camera gate
(c8 withheld) share 0 and output bit-identical to the screen gate's; with c8 the camera gate share
is **1.00 / 1.00** (minimum openness 1.0) at the two depths, screen channel 0; shard ripple
**16.34 -> 1.51 codes (x0.092)**, peak-to-peak 99.4 -> 7.8. A routed 2x2 object claiming 2 px/frame
against the static geometry at its depth closes the gate within 8 px (maximum 0) and leaves the
window 10 px away open. Existing camera cases are unchanged to the digit (pan x0.0866, three
at-rest bit-identities, overflow closes, stale patch 0.727 / 0.623). Pass time, 1280x768:
camera-minus-screen delta 0.164 -> 0.156 ms (no region) and 0.592 -> 0.576 ms (fragmented pan),
i.e. no measurable cost for the three extra instructions.

### 32.4 Depth-latch-free view z from the four-channel lane, general-flight fixture, replay inverse (2026-09-21)

Follow-up to the review of 32.3. Not installed.

**Depth-latch-free term.** c8 needs m22 / m32, which the engine keeps as per-submission scratch: a latch
taken under another view's near plane mis-scales the term by zn'/zn and `m22 > 1 && m32 < 0` cannot
see it (benign: the relative residual grows and `min(screen, relative)` is the screen gate again, but
the feature is silently off). RT2 of the four-channel lane already carries the linear view z (clip w)
in `.b` / `.a` (`current_depth_ps.hlsl`), and previous / z = far_plane + D / w needs no depth law.
`camera_lane_parallax()` returns `(DX, DY, DW, 1)`; `TemporalPass` uploads it as **c9** and binds
`FrameInputs::current_depth` at **s5** of the tests draw only when that texture is A32B32G32R32F
(c9 = 0 and s5 unbound otherwise). With `c9.w > 0.5` a valid-depth pixel takes `c9.xyz / .b` when
`.b > 0` and stays on the far plane otherwise; the c8 law of 32.3 is consulted only with c9.w = 0;
the sentinel is the far plane in every mode. The form is free of the DEPTH latch only: D and c0..c3
still use the latched m00 / m11 / m20 / m21, so a latch taken from a view with a different FOV is
not covered by it (nor by 32.3, nor by the far-plane matrix of 32.1). **Availability in the route:**
RT2 is A32B32G32R32F exactly when `sun_lane_active_` (`ensure_target`): `--sun-shadow-lane` requested,
the lane qualified on the device, the main depth format qualified and the lane not failed. HDR and
the motion RT mode do not enter. The Run 59 command line carries `--sun-shadow-lane`, so the user's
configuration takes the lane path; without the flag, on a device that does not qualify, or after a
lane failure RT2 is R32F and the c8 law with the latched m22 / m32 is the fallback, per frame, with
no history cut (the term only feeds the gate). G32R32F inputs carry no w and use c8.

**Mixed frames (policy).** The composition takes the 17x17 minimum of openness, so one valid-depth
pixel served by a wrong law would close a window the lane opens. With the lane bound the law is
therefore never used: a valid depth whose `.b` is not positive (none is expected: the producer
writes `.r` and `.b` in one draw and the fill is -1 in both) stays on the far-plane path. That closes
the gate within 8 px of such a pixel while the camera translates, deterministically and whatever the
latch, instead of depending on it.

**Unverified, low confidence.** A routed draw whose clip w is not a view z (a near-unit w, as a
pre-transformed or screen-space draw would produce) would write `.b` ~ 1 and the lane would read it
as geometry one unit from the camera: D / 1 is a huge parallax, the relative residual explodes and
the gate closes around it (the safe direction; never a false open). It is believed unreachable
because pre-transformed HUD and overlay draws are not routed (no transformed material program, no
RT2 write), and every routed projection is validated to P[11] = 1 (w = view z). The first flight's
log cannot show it: nothing logs per-pixel `.b`; only an F8 depth read-back (rgba32f) compared
against m32 / (r - m22) would.

**Shader.** `temporal_line_mask_camera` 920 -> 974 words, **226 -> 243 slots** (bytecode
`01111619...`; the first lane form with the per-pixel law fallback was 983 / 245); every other generated header byte-identical, `temporal_line_mask` bytecode
`a44bfebd...` unchanged (manifest source hash only).

**Fixture** (`THIN_REGION_CAMERA_FLIGHT`, 10 rows, 32 frames each). The published gates (b camera, a
screen) are compared with a CPU oracle built from the last frame's depth and motion read-backs: double
unprojection / reprojection with the exact inverse rotation, the shader's `max(w, 1e-6)`, 8-bit
openness, 17x17 minimum. Oracle error **0 codes** in all seven modelled rows and 0 on the screen
channel in all ten.
- *yaw + forward* (2.5e-6 rad/frame against 0.225 units/frame, off-axis crop; static rows 0.999 /
  0.700 px/frame, routed residual 0.015 px): share 1.00 / 1.00 through the law and through the lane;
  with the term withheld 0.00.
- *wrong latch* (CameraState m22 / m32 of zn = 106 while the scene is zn = 6; m00 / m11 / m20 / m21
  correct: the claim is scoped to the depth law): R32F law **share 0.00** (the reviewer's failure,
  reproduced), lane **share 1.00 / 1.00**.
- *wrong latch, mixed frame* (lane bound, columns x < 6 carry `.b` = -1 on valid depth): window
  share 1.00 / 1.00, gate maximum within 8 px of the hole 0, oracle error 0.
- *near geometry* (depth 0.5 = view z 12, camera +12 forward and 20 sideways per frame, so
  c8 * (d - m22) = DW / z = 1 and the previous w is twice the current; gate LO 2 / HI 10 so the radial
  0.5 px/px field grades the gate): window 0.004 .. 0.365, mean 0.117, identical through law and lane,
  0 with the term withheld.
- *behind* (camera 12.5 backward: the pixel was behind the previous camera, w < 0, the clamp decides;
  routed claim 12 px/frame): closed everywhere in both forms, no NaN-open pixel.
Existing rows unchanged (forward x0.0923, pan x0.0866, at-rest 3/3). Lattice mode 522 numerical
checks (501 + 21), 11 flight rows.

**Pass time** (`LINE_TIMING_CAMERA_LANE`, 1280x768, fragmented pan frame, every valid pixel on the
lane): A32B32G32R32F input with the law 1.934 ms, with the lane term 2.049 ms: **+0.115 ms** for the
extra fetch; the lane input itself against the R32F input -0.05 ms (noise). CPU wall time with an
event-query drain, not game FPS.

**Replay inverse.** `camera_previous_ndc` now inverts the logged float rotation exactly
(`INVERSE=transpose` keeps the old form). Routed-truth residual p50 / p90 / p99 px, same ROI and frames
as 32.2: run209 forward **0.0325 / 0.0531 / 0.0606 -> 0.0012 / 0.0023 / 0.0104**, run209 roll
0.0189 / 0.0375 / 0.0577 -> 0.0011 / 0.0019 / 0.0027. The best-fit view-z scale on the forward burst
moves from 0.979 to **1.000**: the "2 % z ambiguity" of 32.2 was the transpose acting on
|t| ~ 1.2e5, not the depth law, so the default m22 / m32 were right in that capture. Numbers:
`verification/results/lattice-gate-replay-run209-forward.json` key `camera_check_exact_inverse`.

### 32.5 Run 60 flight (run212): the gate is closed on the panels by their own glass (2026-09-21) [M]

Same flight, separate decision: run212 showed no approach flash with `--taa-unmatched-static node`
(22-draw unmatched groups filled on 36 approach frames), so node is now the default whenever TAA with
motion output is on, launcher and native fallback alike; `--taa-unmatched-static off` is the A/B opt-out
(see [temporal-integration.md](temporal-integration.md), "Unmatched draws: static-world previous rows").

Host only; no production edit. Tool `tools/analysis/taa_run60_crawl_replay.py` (the section 32.2 exact-inverse,
depth-aware camera path plus the installed Run60 rule: per-pixel `gateOpen()` as `line_mask_ps.hlsl` forms it,
17x17 minimum, a / b, 7x7 box on `b - a`, far stabiliser 0.985 with its own screen-speed gate, `keep = max`),
numbers `verification/results/lattice-crawl-run212-resolve-replay.json`. DLL 39c98242, `/tmp/x3-bottleX3-run212`,
forward burst 11995-12026 (136 units/frame, strut screen speed p50 / p90 0.89 / 1.28 px) and pan burst 16113-16144
(strut screen speed 2-40 px/frame, turnaround at 16133, camera also translating 3-49 units/frame).

**The triage ROI and its share.** ROI (860,10,1260,290) was inherited from run209 and holds no solar-panel lattice
in either run212 burst (forward: only the tip of one far arm, 4.4 % routed pixels; the panels sit at x 640-940,
y 380-560). `camera_check`'s gate-open share is crop-wide and counts every non-routed pixel as open, so 0.893 for
the rotation-only path is simply the share of that crop farther than 8 px from a routed pixel; it says nothing
about the truss. Replay crops used here: forward (760,385,945,565), track (790,410,920,545), support 8102 px,
routed fit 0.0026 / 0.034 px; pan (810,10,1270,758), track (815,565,1150,760), support 8461 px, routed fit
**0.57 / 2.87 px** (parallax across panels: the pan's tracked rms is registration-limited, so the pan is judged on
the frame-to-frame step of tracked points and on a pairwise measure through each strut pixel's own routed motion
vector, which carries one bilinear fetch and therefore favours blur slightly).

**1. Model fidelity (measured).** Replay seeded each frame from the dumped history, against the dumped TAA output,
display codes on strut pixels (thin region and valid depth), p50 / p90 / p99 / max: forward **0.028 / 0.073 /
0.105 / 0.16**, pan 0.025 / 0.077 / 0.215 / 19.4 (HDR p99 0.0005 / 0.0010). Free-running over 31 frames p99 0.58 /
0.80 codes. The same comparison with the gate opened (`sentfix_box7` below) misses the dump by 2.5 / 7.0 / 11.2
codes (forward): the dump is the closed-gate resolve, not the open one.

**2. Suppression (measured, codes).** Tracked rms / frame-step rms / gradient, scored tail of 16 frames:

| burst | jittered input | dumped TAA | present dump | plain w 0.90 replay |
|---|---|---|---|---|
| forward | 26.44 / 42.32 / 1875 | 4.20 / 5.00 / 457 | 4.34 / 5.22 / 469 | 4.24 / 5.04 / 464 |
| pan | 25.56 / 37.45 / 1414 | 9.86* / 8.00 / 359 | 9.96* / 8.13 / 365 | 10.09* / 8.34 / 387 |

(* registration-limited.) The resolve removes 84 % of the forward tracked rms, but **the installed thin region adds
nothing to the plain 0.90 resolve: x0.990 forward, x0.952 step in the pan.** Sections 32 / 32.2 had x0.52-0.57 for an
open gate (run177, run209 forward). Present against TAA output is +3 % / +2 %: nothing after the resolve (bloom,
tonemap, RCAS) creates the crawl, candidate (f) is out.

**3. Mechanism (measured).** Over strut pixels of the scored frames, installed model:

| | forward | pan |
|---|---|---|
| camera gate b mean (share > 0.5) | **0.081** | **0.233** |
| screen gate a | 0.000 | 0.000 |
| history weight p10 / p50 / mean | 0.900 / 0.900 / 0.906 | 0.900 / 0.900 / 0.910 |
| routed valid-depth camera-relative speed p50 / p99 | 0.0011 / 0.015 px | 0.0013 / 0.008 px |
| routed SENTINEL-depth pixels, share of crop | **25.7 %** | 14.3 % |
| their camera-relative speed p50 / p90 | **0.86 / 1.15 px** | **1.00 / 1.52 px** |
| thin mask on tracked support, mean / frame-to-frame change | 0.985 / 0.003 | 0.984 / 0.009 |
| 7x7 box binds where b > a | 0.07 % | 0.24 % |
| history accepted | 1.000 | 0.986 |

The panel interiors are routed blended glass: motion alpha 1, RT2 depth sentinel (section 2's "two layers"; the
lane `.b` is -1 there too, checked on the dump). `gateOpen()` gives a sentinel pixel the far-plane camera path with
no parallax term ("the sentinel has no geometry and stays at infinity") but still forms `relative` from its routed
motion against that path. The glass is at the panels' 16-100 k units, not at infinity, so under any camera translation its
relative speed is the whole parallax, 0.9-1.5 px > HI 0.25, and the 17x17 minimum spreads the closure over every
strut, all of which lie within 8 px of glass. Section 32.2 met the same pixels in run209 ("0.72 % of crop pixels
exceed HI, every one a routed pixel with a sentinel depth ... spreads over 26 %") and scored the rest; on the solar
plant panels they are a quarter of the crop. So: not (a) strut-edge selectivity (the whole region is closed), not
(b) (the box binds on < 0.3 %), not (d) (mask coverage 0.98, stable), not (e) (far stabiliser and thin region
combine by max; the far gate is closed by the 0.9 px screen speed, as designed), not (f). (c) is real but
secondary, see the pan below.

**4. Variants (measured; ratios against the installed model).** `sentfix` = a routed pixel with a sentinel depth
does not vote in the camera gate (`relative = 1`); it equals the forced-open gate to every printed digit forward and to three digits in the pan (b 0.999), so nothing else closes it. Background trail = p99 |variant - plain| on background 3 px clear of geometry
and glass, codes.

| variant | forward rms | forward gradient | forward trail | pan step | pan pairwise | pan gradient | pan trail |
|---|---|---|---|---|---|---|---|
| installed (Run60) | 4.198 | 457 | 0.10 | 7.94 | 14.15 | 355 | 0.07 |
| installed, W 0.94 | x1.003 | x1.007 | 0.08 | x1.018 | - | x1.036 | 0.07 |
| **sentfix, box7, W 0.97** | **x0.509** | x0.686 | 2.66 | **x0.823** | x0.945 | x0.760 | 1.06 |
| sentfix, clip off | x0.511 | x0.688 | 3.29 | x0.846 | x0.951 | x0.762 | 1.35 |
| sentfix, box5 / box11 | x0.507 / x0.510 | x0.685 / x0.688 | 2.21 / 2.84 | x0.818 / x0.834 | - | x0.763 / x0.762 | 0.95 / 1.39 |
| **sentfix, box7, W 0.94** | x0.710 | x0.840 | 1.52 | x0.902 | x0.967 | x0.906 | 0.88 |
| sentfix, box7, W 0.97, Keys -0.65 | x0.657 | **x1.164** | 2.09 | x0.971 | x1.067 | x1.184 | 3.79 |
| sentfix, box7, W 0.94, Keys -0.65 | x0.788 | x1.236 | 1.59 | x1.024 | - | x1.261 | 3.63 |

Ranking: (1) the sentinel vote, W 0.97, box7: half the forward crawl, the blur and trail section 32 already priced
(gradient x0.69, trail 2.7 codes p99 next to the struts); box size is immaterial for crawl, 7x7 or 5x5 is the better
ghost bound. (2) the same with W 0.94, the user's tentative preference: x0.71 crawl for x0.84 gradient and 1.5 codes
of trail - the middle of the trade, and only meaningful once the gate actually opens: **on the installed build W
0.94 against 0.97 changes nothing (x1.003), because the weight never applies.** Keys -0.65 restores the gradient
above the installed level in forward flight at x0.66 crawl, but in the pan it rings (trail 3.8 codes, step back to
x0.97): not recommended as a global kernel. Mask dilation / temporal hold was not run: the mask is already at 0.98
coverage with 0.3-0.9 % frame-to-frame change.

**Pan.** The same defect closes the gate (b 0.23; it opens only at the turnaround where the screen speed itself
falls). Opening it helps less: step x0.82, pairwise x0.95. With the gate open the weight is age-limited, not
W-limited (keep p10 / p50 / mean 0.90 / 0.95 / 0.943; 26-80 % of strut pixels younger than 19 frames per frame):
up to 15 % of the strut pixels per frame enter from outside the frame during the 35 px/frame legs (`oob`), and the
Catmull-Rom fetch at a new fractional offset every frame takes x0.76 of the gradient. That part is inherent to a
30-40 px/frame pan (*inference*: the age limit is measured, its attribution to off-screen entry rests on the
per-frame `oob` share and was not isolated further).

**Production change implied (not made).** `src/temporal/line_mask_ps.hlsl` `gateOpen()`, the line
`float relative = routed ? gateOpenness(...) : 1;` -> `routed && validDepth(depth) ? ... : 1`, which also makes the
function's own header comment true ("A routed pixel without a valid depth has no camera path and keeps its screen
speed" - today it keeps the far-plane path). Camera mask program only; one comparison, no new constant, fetch or
target; regenerate `temporal_line_mask_camera`. Fixture: a `THIN_REGION_CAMERA_FORWARD` row with routed
sentinel-depth cells between the shard rows (share today 0, expected 1). Risk to check there: a routed sentinel
pixel that really moves independently (glass on a passing ship) no longer closes the gate by itself; its
valid-depth hull within 8 px still does, and the box7 bound applies (*inference*, not replayed: no such mover in
run212). The alternative, giving routed glass a depth in RT2, touches the cutout route and the resolve's sentinel
policy and is not the cheap fix. W 0.94 is a launcher value (`--taa-thin-region 0.94`), no code.

#### 32.5 outcome: a routed sentinel pixel casts no vote (2026-09-21) [M fixture; not installed, no flight]

`gateOpen()` in `src/temporal/line_mask_ps.hlsl` now measures `relative` only on a valid depth; a routed pixel on the
depth sentinel contributes openness 1 to the camera gate (the value a non-routed pixel already contributes), and the
screen-speed channel is unchanged. The sentinel is read from s1 `.r`, which is the device depth with either source (R32F
or the four-channel lane), so one test serves both; a VALID depth whose lane `.b` is not positive still votes from the far
plane, as section 32.4's mixed-frame case requires. The no-vote is formed as `gateOpenness(screenSpeed * 1e-20)`, not a
constant 1, so a non-finite routed correspondence on glass still reads closed without a foldable comparison. Camera mask
program 243 -> 245 instruction slots (983 words), texldl count 10 -> 10 (no added fetch); the plain mask program is
byte-identical. Fixture (`temporal_thin_region_inc.h`, flight scene, forward flight 0.6 px/frame > HI): a routed
sentinel-depth quad under the shard rows (63.6 % of the window), R32F law and lane: gate open on both bands (share 1.000,
window minimum 1.000, oracle error 0); the same with a routed valid-depth 2 px/frame mover: window still open, gate 0
everywhere within 8 px of the mover. Fast SENTINEL-depth mover (pan scene, value 4, 2 px/frame over the shards, `THIN_REGION_GLASS_MOVER`): it casts no vote, camera
gate 1.000 within 8 px of it, screen channel 0, mask equals the depth-only oracle; the 7x7 box is then the only ghost bound:
excess over the scene maximum 0.000 five or more pixels behind the trailing edge and after the mover left, 2.57 (bound: the
mover's own value, 3.0 over the maximum) on the three pixels the box still reaches. That trail is the price of the no-vote for
bright fast glass; a valid-depth mover closes the gate instead. Non-finite routed motion on a sentinel pixel: 1e30 px/frame
reads closed on the GPU in both channels, output bit-identical to the screen gate (asserted); NaN reads OPEN on this backend,
with and without glass and in the plain mask too (fast-math shader compilation, reported only, as before this change).
`run_temporal_pass.py` on bottle X3: numerical 538 (was 522), 15 flight rows. The
host replays (`taa_lattice_gate_replay.py` `camera_check`) still count the sentinel vote; section 32.5's `sentfix` variant
is the model of this change.

#### 32.6 Run 61 verdict (2026-09-22)

run215/run216 on the Run61 DLL: the user reports the solar-plant crawl as fixed or nearly so in forward flight and pans. The thin-region weight stays at **0.97**: the user could not reliably separate 0.97 from `0.94,1` (0.94 perhaps slightly less blur in motion, slightly more crawl). A speed-eased weight is the fallback if motion blur becomes a complaint. Lasers over sky showed no trails with `--taa-sentinel-stabiliser 0.7`. Open: distant unrouted stations still flicker under fast pans (temporal-resolve ledger, run215 entries).
