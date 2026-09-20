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
