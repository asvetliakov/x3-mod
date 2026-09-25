# TAA thin classification: which pixels get the thin treatment

Design note, 2026-09-25. **Ratified 2026-09-25 by the main session** (pixel tier adopted with the two amendments; user question "is our problem thin objects?" answered: same sampling problem, object thinness is one cause, footprint at depth another). Question: how the TAA should decide which pixels need
the "thin" treatment (the region hold: weight 0.97, clip against the half-resolution 7x7 box pair, camera gate) and which
take the plain path, so that the decision is robust instead of a list of special detectors. Trigger: the Run 84 A fog-band
plant sparkles (run327 / run329 / run332), which none of the three region sources sees. A bounded fix (far ramp 60/68 plus a
7x7 min/max clip on far pixels) is being implemented in parallel and is judged in section 6, not assumed.

Marks: **[M]** measured (the run329 / run327 result files, this note's census under
`verification/results/taa-thin-classification/`, the temporal-resolve ledger, the session logs), **[I]** inferred. Units:
view units as the dumps carry them (the ledger scripts print them as km at 1 unit = 1 m; at the brief's 0.2 m per unit the
plants sit at 18-27 km). Frame: 5120x1440, p00 0.4999979, so one pixel's footprint is `z / 1280` view units.

## Decision (recommended)

Classify by one inequality, evaluated twice: a feature needs the thin treatment when its size divided by the pixel's
footprint at its depth is below the jitter footprint (about one pixel). The **draw tier** evaluates it where the feature
size is known (the thin vote: the subset's triangle-height histogram against `log2` pixels per unit at the draw) and gives the
**region** treatment (an unclipped hold, bounded by the gates and the box under motion). The **pixel tier** evaluates it
where the feature size is not known and a calibrated smallest shading feature F stands in (the far ramp `farw`, whose
`far_f0 / far_f1` are footprints in view units per pixel: `farw = 1` once one pixel covers F units) and gives the **far**
treatment: weight 0.985 plus a 7x7 box bound, gated on the same camera openness as the weight. The emissive vote stays as
the image-side witness for painted / emissive strips (E = 1); the screen search stays the diagnostic for negative-space
features (gaps), whose fix is the baker. The bounded fix under implementation is the pixel tier; adopt it with two
amendments: gate the 7x7 on `farw * openC > 0` (a far mover keeps the 3x3 bound), and document `far_f0 / far_f1` as the
sampling-limit calibration (60 / 68 = a 12-14 m feature at 0.2 m per unit) rather than a line-fade distance. Do not lower the
emissive threshold ("mark edges as thin"): section 6 gives the numbers. Cost of the pixel tier: about 0.02-0.03 ms at
5120x1440 [I], no per-draw work, shader-only (portable).

## 1. What "needs the thin treatment" means

A pixel needs the thin treatment when the feature it integrates is narrower than the jitter footprint, so that the current
jittered sample lands on it in some phases only, and on a missed phase the pixel's current 3x3 neighbourhood does not contain
the value the history has converged to.

The mechanism as measured on the plants (`rest_pixels_327_2242_out.txt`, `rest_pixels_332_2826_out.txt` [M]): the hdr
sample at a sparkle pixel reads 79 / 168 / 79 / 79 / 158 / 79 / 127 / 140 codes over one jitter cycle (hit on 3-4 of 8
phases, a 0.4-px coverage [I]); the history is at age 64 on every plant pixel; on a hit phase the current sample enters at
`1 - keep` (keep 0.90-0.96 where `farw` is 0.0-0.66: 3.7-8.5 codes of an 85-code spike, the sparkle, margin 6 codes); on
the next dark phase the 3x3 clip pulls the history back to the dark neighbourhood (27 / 27 and 120 / 120 sparkles clamped
down, removed / rise median 1.00-1.02). The resolve model reproduces the actual output to 0.03 codes p99. So the treatment
has two independent parts, and both are needed: the **hold** (a keep high enough that a hit phase leaks below the margin:
at 0.985, 1.3 codes [I]) and the **bound** (a clip box wide enough that the converged history survives a missed phase). The
region gives hold 0.97 with no clip at rest (`old = lerp(clip3, old, a)` with `a = b = 1`); the far path gives hold 0.985
with the 3x3 clip. Neither alone removes the sparkle and keeps the line: replay `far1` (weight only) 0-1 sparkles but line
dimming p10 -9.7 to -11.5 codes on edge pixels (the clip still erases); `box7` (bound only, ramp 80/130) 13-22 sparkles;
`far1box7` 0 sparkles, p10 -1.9 to -2.5; `region` 0 sparkles, p10 -0.0 to -0.2 (`rest_sim_*_out.txt`, three bursts [M]).

## 2. Signals per pixel and per draw, and which see a missed feature

| signal | where | sees the feature on a frame the sample missed it | note |
| --- | --- | --- | --- |
| current colour | resolve | no, by definition | |
| current 3x3 min / max, mean, sigma | resolve (fetched) | no for an isolated sub-pixel line (the 3x3 max is the background) | the clip bound today |
| current 7x7 min / max | resolve, in place (40 taps, branch) | mostly: at rest, of the pixels whose history lies outside the 3x3 box by > 2 codes, 88-95 % of the far ones and 76-92 % of all lie inside the 7x7 box (census, run327 rest, 7 frames [M]) | the bound of the pixel tier |
| history (5-tap bilinear at the correspondence) | resolve | yes: it holds the converged coverage, until the clip erases it (then no) | the only per-pixel carrier of a missed feature |
| age | resolve (age target) | proves convergence (64 on all plant pixels [M]); says nothing about width | |
| region hold h, closure code | age fraction | bridges phases: a flag in any phase holds L = 8 frames | this is what makes any hit-phase detector work across the cycle |
| depth `.r`, view z `.b` | lane | yes: the pixel footprint `z / (p00 W / 2)`, independent of phase | the pixel tier's input |
| thin vote `.a` | lane, per draw | yes: independent of phase, known feature size | the draw tier's output |
| motion, alpha, camera path, dilation neighbour | resolve | prove static or camera-coherent (openC); no width information | the gates |
| previous-frame neighbourhood | not fetched | would show the feature's history-side edge at +9 taps per pixel; empty after an erase | rejected, section 8 |
| triangle-height histogram, `log2` px per unit, D, draw class | route, per draw | yes: geometry width in pixels at the draw's scale, per frame, no image needed | vote window 0.5-3 px, fraction >= 0.5 |
| texel footprint (c217.yz) | route, per draw | no: sub-pixel texture detail is mip-filtered and does not sparkle; the sparkling features are geometry / shading | |

Two facts follow. First, on a missed frame only three things carry the feature: the history (fragile), the depth footprint
and the draw-side footprint (both phase-independent). Second, a detector that fires on hit phases only is not disqualified:
the L-frame region hold carries it across the cycle. The main session's objection to "mark edges as thin" (no edge at the
pixel on the missed phase) is therefore not fatal in itself; the case against it is what it flags (section 6).

## 3. The classification: one inequality, two tiers

`feature_px = feature_size / footprint(z) < ~1 px` (the jitter footprint; the vote's window 0.5-3 px is the same test with a
margin for lines that straddle two pixels).

**Draw tier (feature size known).** The thin vote as built: `h = 2 area / longest edge` per triangle, 8 log2 bins per subset,
shifted by `log2(m00 * scale * W / 2 / D)` at the draw, votes when at least half the subset's triangles fall in 0.5-3 px.
Treatment: the region (hold 0.97 unclipped at rest; under motion the gates close it and the box bounds it). It is the right
tool for strut lattices and masts: deterministic, engine-level, no per-pixel cost (about 448 draws x under 100 ns [I]),
resolution-aware through W. Its known limits stay: a subset mixing struts and panels below the 0.5 fraction does not vote;
the scale is taken at the object origin; negative-space features (gaps between panels) have no triangle to measure.

**Pixel tier (feature size unknown).** The far ramp, reinterpreted: `farw = saturate((d - d0) * inv)` where `d0 / d1` are the
device depths at which one pixel covers `far_f0 / far_f1` view units (`resolve.h:far_gate`, "the gate is a pixel footprint
in world units" [M]). With 60 / 68 that is 76,800-87,040 view units of depth here: from 87,040 on, every shading feature
smaller than 68 units (13.6 m at 0.2 m per unit) is sub-pixel, whatever draw it belongs to. The plants sparkle at 89,000-137,000
(footprint 70-107 units per pixel [M]); their features cover about 0.4 px, so 28-43 units [I]. Treatment: hold 0.985 (the far
weight, on `openC`) plus the bound: the in-place 7x7 min / max of the current frame where `farw * openC > 0` and the box
programs did not run. The bound is what distinguishes this tier from the region: a pixel classified by footprint alone has
no positive witness that a thin feature exists there, so its hold must stay bounded by what the neighbourhood shows this
frame; the region has that witness (the vote) and may hold unclipped. At 20-64k and 64-77k view units no pixel exists in the
three bursts (`far_px_counts_out.txt` [M]), so the calibration between 40 and 77k is untested (section 7).

**Image witness (feature seen, size unknown).** The emissive vote at E = 1 stays: a routed pixel above 1.0 luma whose 3x3
minimum is below a third of it is a strip seen this phase, and the hold bridges the rest. At this stand it flags 1,035-1,128
pixels per frame, all under 20k view units (the lit hull) [M].

**Diagnostic.** The screen search (depth class changes on 7-tap lines) is the only detector of negative space (the Terran
louvre gaps, 1 px at a 7.5 px pitch) and stays opt-in (`both`); that lattice's crawl is the coarse record's louvre gap and is
fixed in the baker, not in the TAA (mask-fold note, Decision).

### Behaviour on the four known cases

| case | draw tier | pixel tier | outcome | false positives / negatives |
| --- | --- | --- | --- | --- |
| Terran lattice (64 opaque cell draws, 1 px gaps at 7.5 px, near) | no vote (cells are panels; RT2 `.a` = 1 on 100 % of the region [M]) | farw 0 (near) | search only (72-80 % of the flickering pixels [M]); the baker fix removes the gap | FN by design: a gap is not a triangle; accepted, the fix is upstream |
| fog-band plants (plates at 89-137k units) | no vote (0 of 27 and 0 of 120 sparkle pixels; 2 of 4 opaque draws vote with `min_alpha` 0.2764, none of them far: `.a` < 1 on 0 of 29,134 far routed pixels [M]) | farw 1 from 87k with 60 / 68 | replay `far1box7`: 0 sparkles in 3 bursts, edge dimming p10 -1.9 / -2.4 / -2.5 codes, p50 +0.4 to +0.9 [M] | FP: every far pixel takes the bound (0.33-0.60 % of the frame [M]); harmless where the history is inside the 3x3 box (the clamp is then the identity) |
| hull masts under a pan | votes if the mast subset is at least half thin triangles at its scale; grouped with hull panels it does not (unknown here: the vote log carries only voted draws' fractions) | farw 0 | region with the camera gate (co-moving hull: openC 1) | FN when grouped below 0.5; a graded vote would trade region size for it (section 8) |
| moving ships (far or near) | struts vote; the gates close on the mover (camera-relative speed) | farw > 0 far; `openC` 0 on the mover and, through the dilation neighbour, on the background beside it | the base weight and the 3x3 bound on the mover; the plants beside it keep the far treatment only where their own and neighbour gates are open | with the 7x7 gated on `farw` alone (the fix as briefed) a far mover would take a 7-px ghost bound: the amendment gates it on `farw * openC` |

## 4. Cost on the hot path

Per pixel: `farw` and `openC` are already computed by the folded resolve; the 7x7 is the existing in-place loop
(`resolve.hlsl` lines 978-992) with one more `[branch]` condition. Taps: 40 per far pixel; 0.33-0.60 % of the frame at
5120x1440 is 24k-44k pixels, 1.0-1.8 M taps; the whole-frame 49-tap program costs 0.51-0.62 ms for 48 M taps at 1280x768
[M], i.e. about 11 ps per tap, so 0.01-0.02 ms [I], plus branch divergence on a 0.5 % set: call it 0.02-0.03 ms against the
3.88-4.05 ms TAA span [M]. Optional cost gate: take the loop only where the history lies outside the 3x3 box (6 compares on
values already in registers); at rest that is 1.0-1.1 % of far pixels (census [M]), output-identical because a history
inside the 3x3 box is inside the 7x7 box; worth having only if the timing row shows the branch at all. Per draw: nothing new.
Memory: nothing new. Region size, box pair, search: unchanged.

## 5. Native Windows

The pixel tier is shader arithmetic on inputs the resolve already binds (documented D3D9, ps_3_0, `[branch]` / `[loop]`), so
it behaves the same natively and needs no readable buffers: the plants are fixed on both targets. The draw tier depends on
the readable-MANAGED creation policy (WRITEONLY stripped at creation, a READONLY `Lock` of the system-memory copy: documented
usage, cross-compiled, not run natively; `platform-portability.md`, "TAA thin vote"). Without it (`--taa-thin-vote off`, or a
native refusal) the launcher does not send `vote`, the DLL default source is `both`, and the search (+0.3-0.4 ms in the fold fixture [M])
plus the emissive vote feed the region: struts fall back to the search's depth-fragmentation test, the far plants are
unaffected. Nothing here depends on wined3d.

## 6. Against the bounded fix and against "mark edges as thin"

| | bounded fix (60 / 68 + 7x7 on far pixels) = the pixel tier | region hold on far pixels ("a source that fires") | mark edges as thin (emissive E lowered to 0.5) |
| --- | --- | --- | --- |
| rest sparkles, 3 bursts (replay) | 0 / 0 / 0 [M] | 0 / 0 / 0 [M] | not replayed; would give the region variant on the pixels it flags [I] |
| edge dimming p10 / p50 (codes) | -1.9 to -2.5 / +0.4 to +0.9 [M] | -0.0 to -0.2 / +1.7 to +2.1 [M] | as the region on flagged pixels [I] |
| what it flags at this stand | far pixels: 24k-44k per frame [M] | the same | E 0.5: 3,195-3,289 routed pixels per frame against 1,035-1,128 at E 1, of which +1.9k on the near hull and 151-197 at 102-166k plus 43-64 beyond [M] |
| fires on the sparkle pixels | yes (footprint) | yes | on hit phases: sparkle pixels read 160-176 codes (luma 0.56-0.79 [I]) over a 3x3 minimum near 79 codes (0.08); the hold bridges the misses |
| ghost bound at rest | 7x7 of the current frame | none (the gates only) | none on the flagged set |
| exposure added | far world-static pixels only; a mover keeps the 3x3 (amendment) | far pixels unbounded: a far station's shading change lags with a 66-frame constant and nothing clips it | near hull highlights 0.5-1.0 luma: unclipped 0.97 hold (33-frame lag) on 2-3x the pixels the emissive vote holds today; detects contrast, not width, so the set grows with scene contrast, not with the sampling problem |
| cost | 0.02-0.03 ms [I] | box pair opens on far blocks (+0.02-0.05 ms [I]) | +9 taps already taken; region and box grow with the flagged set |

The bounded fix is the pixel tier; its 2 codes of dimming at p10 on far plant edges is the price of a bound on pixels
without a geometric witness, and at 8 bits it is below visibility [I]; the flight decides. "Mark edges as thin" is a lower
threshold on the image witness. It would work on the plants, and its objection is not the missed phase (the hold covers that)
but that contrast does not encode width: a 40-px hull edge scores like a 0.3-px bevel, the flagged set doubles or triples
on the near hull, and the region's unclipped hold lands on lit hull edges whose shading changes fast under a roll. If the
flight finds the 7x7's dimming visible, the principled fallback is not a lower E everywhere but the image witness inside
the pixel tier (E 0.5 where `farw = 1`, flagging 150-260 pixels per frame here [M]), which gives the region variant's 0.2 codes
on far strips while keeping the near set at E = 1.

## 7. Unknown, and what settles it

- **Pan.** The replays are rest steady states. run332's pan burst has 20 sparkles in the `farw = 1` bin (4.41 per 10k owned
  plant pixels, `cmp_332_pan_3631_exship8_out.txt` [M]) that the far weight alone does not explain. Settle: replay 3631-3638
  with the integer shifts of `sparkles.py` and the 7x7 bound, and a lattice row on the `FAR_CAMERA_PAN` strip with the bound
  on (yaw 10 / 10.5 / 8.25, the 0.4-px line): the sparkle margin at rest and yaw must stay at the rest level (14.20 codes on
  the 0.4-px line today [M]) with the 1-px line's peak not below 0.60 of rest.
- **The gap 40-77k view units.** No pixel in the three bursts lies between 20k and 64k or 64k and 77k [M]; a body with 5-m
  features at 50k would have `farw` 0 and keep 0.9. Settle: a fixture strip at `farw` 0.5 and 0 with 0.3-px lines, or one
  flight at a station 8-15 km out with an F8 burst; if it sparkles the calibration moves (f0 40) and the far fraction grows.
- **Do the plant draws almost vote?** The frame log carries the voted draws' minimum alpha only (0.2764 from 2 of 4 opaque
  draws at the plant stand [M]); the plants' own fraction is unknown. Settle: one telemetry field, the maximum fraction of
  the unvoted draws per frame (`thin_vote_frame`), no flight needed beyond the next one. It decides whether a graded vote
  (section 8) would have caught them; the recommendation does not depend on it.
- **Masts.** Whether a mast subset votes is unmeasured (Run 84 A item d has no ledger row). Settle: the same telemetry on
  the mast hull, plus the `taa_age` flag share on an F8 at the hull.
- **Native.** The readable-MANAGED policy and the 3-RT resolve are cross-compiled, not run.

## 8. Verification that proves the pixel tier

1. Replay (exists): `rest_sim.py` variant `far1box7` = the recommendation at rest, 0 sparkles and the dimming figures above
   on run327 2242, run329 5901, run332 2826 [M]; add the `farw * openC` gate to the model (identical at rest, openC = 1).
2. Lattice fixture: identity `farw = 0` -> output bit-identical to the installed program; `FOLD_FALLBACK`-style containment of
   the far 7x7 against the CPU box (0 differing); a new far rest row with 0.3-px lines at 60 / 68 (sparkle margin below 2
   codes, line peak within 0.9 of the region variant); `FAR_CAMERA_PAN` with the bound (section 7).
3. Timing row at 5120x1440: resolve delta at most 0.05 ms at rest and under the pan scene.
4. Flight: the plant stand at rest and a 3-9 px/frame pan, F8 bursts; the rate per 10k owned plant pixels (run327 rest 2.07,
   run332 rest 4.68, pan 3.28-5.10 [M]) must fall to at most 0.5 [I target], and edge dimming p10 must stay within -3 codes.

## 9. Options considered and why they lose

| option | loses because |
| --- | --- |
| region hold on far pixels (farw as a region source) | no bound at rest on pixels with no geometric witness: a far station's shading change ghosts with a 66-frame constant and nothing clips it; buys 2 codes at p10 |
| emissive E lowered everywhere ("mark edges as thin") | contrast is not width: +1.9k near hull pixels per frame at E 0.5 [M] into the unclipped hold, the set scales with scene contrast; the far strips it would catch are already caught by the footprint |
| 7x7 bound everywhere, or wherever the history leaves the 3x3 box (a depth-independent temporal witness) | output-identical to "7x7 everywhere": a 7-px ghost bound on every mover and disocclusion frame-wide; the census shows the witness is 7x denser far than near (1.0 % vs 0.14 % of pixels [M]), so the footprint gate loses almost nothing |
| far weight only (ramp 60 / 68, no bound) | 0-1 sparkles but the clip still erases the line each missed phase: p10 -9.7 to -11.5 codes [M], worse than today's -7.6 to -9.7 |
| gamma clip (wider sigma) | 156-238 sparkles, more than base [M] |
| graded vote (fraction below 0.5 votes with a strength) | puts far plates into the region: the unclipped hold without a witness (as row 1); region and box pair grow with every mixed subset; the plants' fraction is unknown; keep for the masts question only |
| history-side edge detection (a 3x3 of history taps) | +9 taps per pixel frame-wide; after an erase there is no history edge to detect, so it needs the bound first, which then makes it redundant |
| texel footprint per draw (c217) | sub-pixel texture detail is mip-filtered and does not sparkle; the features are geometry and shading |
| a per-draw "far" flag instead of per-pixel depth | the owned plant pixels span 92-221k view units (p5-p95 over the three bursts [M]), across the ramp; the per-pixel ramp already exists and is free |
