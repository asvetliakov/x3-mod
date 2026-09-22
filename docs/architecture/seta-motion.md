# SETA approach smear: diagnosis and the strict sky history

2026-09-22. Symptom (every build back to Run 60 at least, `--taa-sentinel-stabiliser 0`
without effect): flying fast toward a station under SETA leaves a black smear on the
bright nebula around the station. Evidence: run235 (`/tmp/x3-bottleX3-run235`, Run65 DLL
2d11aac4, burst 2 = frames 6967-6998, 1280x768, `color_`/`present_`/`taa_`/`motion_`/`depth_`
readbacks; session log grepped only). Scratch scripts stayed outside the repository.

## 1. The motion vectors are right; the ledger's "1.3 px" was a unit error

`motion_1_*.rgba32f` RG is the **previous unjittered absolute texture UV** of the content
at the jittered sample (src/temporal/README.md, "Per-pixel object motion"), not a
displacement. The Run 235 ledger section read `hypot(R, G)` as a velocity, which is why
"max 1.25-1.37 px, median 0.9" appeared: those are UV magnitudes of positions near the
screen centre. Decoded per the ABI, displacement in pixels is
`d = (p + 0.5 - j) - motion.xy * (W, H)` with `j` the frame's raster jitter from
`motion_output_frame`:

| check (run235 burst 2) | number |
| --- | --- |
| cockpit cluster (depth 0.938-0.963, fixed to the camera), decoded `d`, median x / y, every sampled frame | **0.000 / 0.000** px (n = 26.5k) |
| station bucket (depth 0.99-1.0), decoded `|d|` median, frames 6970 / 6978 / 6986 / 6990 | **3.14 / 4.31 / 7.55 / 8.75** px |
| station `|d|` max, same frames | **23.6 / 32.7 / 36.1 / 37.9** px |
| routed (alpha 1) fraction inside the station silhouette | **1.0000** |
| block match of the depth silhouette (48x48 windows, +-40 px search) against the median decoded `d` in the window, 9 windows over 6975-6990, displacements 0-14.8 px | error x **0.31 +- 0.88**, y **0.24 +- 0.63** px, max **2.36** (a window straddling two parts) |
| cut detector's own `cut_median_px` (object-origin displacement from the matched rows) | 0.24 at rest (6960-6967) -> **2.04-3.50** px during the approach |

So the same-draw motion output (question 1) is per rendered frame: the history rows are the
previous latched frame's submitted rows (`MotionRowHistory::commit` at Present,
`begin_frame` at the latching Clear), the camera pair is the previous successful resolve's
scene view, and there is no dt or velocity scaling anywhere; SETA changes only how far the
engine's rows move between two rendered frames. Matching (question 2) is by key equality
(lifetimes, node, mesh, geometry, pass); a large displacement cannot fail it. In burst 2 the
station draws were matched every frame (`matched == routed`, 425-461) except 6969
(387/425, 38 unmatched-static applied), 6973 (424/443, 17 applied + 2 object_unknown) and
6993-6996 (1-33 misses): LOD/mesh swaps as the station grows, the run212 case, not the
smear. No magnitude clamp exists (question 3): RGBA32F absolute UV, the resolve samples
wherever the UV lands, the far stabiliser gates weights, not vectors.

## 2. The cause: the far-plane disocclusion test accepts distant geometry as sky history

A sky pixel (current depth sentinel) under policy 2 reprojects at the far plane with
`expectedDepth = 1` and the test `previous >= expected - max(c6.x, c6.y * |expected|)` =
`previous >= 0.98`. The station's device depth during the approach is 0.9997 (p5-p95
0.99965-0.99995), so wherever the station's silhouette moved away this frame the previous
frame's hull depth proves "at or behind the far plane" and the dark hull colour becomes the
sky's history at weight 0.9 (0.97 inside the thin region, which is open around the
fragmented station), clipped only to the 3x3 box of a star field. Under normal flight the
trailing band is below one pixel and the closest-depth dilation owns it; under SETA it is
5-30 px wide.

| uncovered sky (previous depth = station, current sentinel, no geometry in the current 3x3) vs control sky (sentinel both frames, none within 3 px) | f6972 | f6976 | f6980 | f6984 | f6988 |
| --- | --- | --- | --- | --- | --- |
| uncovered pixels | 3000 | 4383 | 4627 | 6040 | 6922 |
| previous depth p5 / p50 / p95 | .99975/.99981/.99995 | .99972/.99979/.99989 | .99966/.99977/.99985 | .99968/.99974/.99989 | .99965/.9997/.99985 |
| fraction within 0.02 of 1.0 (accepted by the test) | 0.999 | 1.000 | 0.999 | 1.000 | 1.000 |
| `present - colour` luma, p10: uncovered / control | -30.0 / -1.1 | -21.6 / -1.2 | -21.8 / -1.2 | -15.8 / -1.3 | -15.2 / -1.3 |
| fraction darker than colour by > 15 luma: uncovered / control | 0.198 / 0.005 | 0.142 / 0.006 | 0.133 / 0.005 | 0.105 / 0.006 | 0.101 / 0.006 |
| uncovered on bright nebula (colour luma > 60): `present - colour` median (control) | -29.8 (-0.4) | -12.2 (-0.4) | -12.5 (-0.4) | -8.7 (-0.4) | -13.9 (-0.6) |

The ledger's "75-80 % of dark pixels 13+ px from any silhouette" are stars: the
`present < colour - 40` criterion on a sky of median luma 18 selects bright points the
resolve averages under jitter (900-1600 "new" dark pixels per frame that were sky the frame
before, scattered). The smear population is the 400-600 freshly uncovered pixels per frame
plus their persistence (mean age 6-7 frames by 6990, decaying at 3-10 %/frame under the
thin region's 0.97 weight and 7x7 box clip). The 1-px band beside the silhouette that takes
the station's velocity through the dilation is the documented AA-edge behaviour and stays.

## 3. Fix: the strict sky history term (`--taa-sky-history strict`)

`src/temporal/resolve.hlsl`: `c7.z` (0 off, 3 on; policy 2 only) is subtracted from the
disocclusion tolerance of an **unrouted** pixel (motion alpha not 1) where `nearest == 1`,
i.e. a far-plane pixel whose 3x3 holds no depth below 1 (nothing won the dilation). The
threshold then lies above every valid depth, so a history tap proves only when it is the
sentinel; a geometry tap of any depth rejects the lookup (current-only that frame; the
pixel's own sky history is sentinel-depth from then on and accumulates again), and so
does a history depth of exactly 1.0 that is not the sentinel, should a producer ever write
one. Dilated far-plane pixels (`nearest < 1`) and geometry pixels keep the unchanged test.
A routed pixel (alpha 1) is never strict: the fade-band draw (RT1 alpha 1 with its depth
target, RT2 masked, motion_output.cpp) over sky is `farPlane` with `expectedDepth =
motion.z`, and without this exclusion strict would have rejected an LOD cross-fade's own
history over sky (current-only flicker, a regression of the run212 approach behaviour);
the same exclusion makes a routed pixel rasterised at exactly device depth 1.0
unreachable for the term. Slot budget (`RESOLVE_BUDGET`, temporal-lattice.txt):
the term is `tolerance -= step(1, nearest) * c7.z` with `nearest *= step(alpha, 0.5)`
after the motion fetch (four slots), paid for by two exact rewrites elsewhere
(`nearest >= 1` for `all(dilate == 0)` in the fill-pair test, `saturate(2 - 0.5 speed)` in
the thin soft clip): age_line 509 -> **511**, far_camera 508 -> 509, age_filter 506 -> 508
of the guaranteed 512. A predicate on `farPlane && all(dilate == 0)` inside the tap loop
cost 9 slots and overflowed age_filter by 3, a flag cleared inside the alpha-1 branch 5.
The 512-slot class is created by `CreatePixelShader` like every embedded program
(`temporal_pass.cpp`, `make(temporal_resolve_age_line_program(), &age_line_)`) and a refusal
fails soft: the line filter with the age weight becomes unavailable, nothing crashes.
No new variant: the term rides in `ResolveConstants::options[2]` (the mask and snapshot
draws upload their own mode in that lane and the pass puts the term back),
`x3::temporal::prepare(..., sentinel_strict_sky)`, `FrameInputs::sentinel_strict_sky`,
`MotionOutput::configure_sky_history`, `X3M_TAA_SKY_HISTORY=strict|loose` (capture.cpp),
`tools/manage.py --taa-sky-history {loose,strict}` (requires `--taa`; not forwarded when
omitted, an inherited value is dropped). Default **loose**: the pre-existing behaviour bit
for bit, following the repository's rule that a resolve change becomes the default after the
user accepts it in flight (run212, run221). `c7.w` stays 2, so the mask programs
(`line_mask_ps.hlsl` reads `options.w` as `> 0.5` / `> 1.5`) see policy 2 unchanged. The twelve resolve program headers
were regenerated (`generate_rigid_motion_pixel.py`); the motion-output fixture's reference
resolve mirrors the env so the byte-for-byte oracle holds with the option set.

What strict rejects that loose accepted: geometry history at an unrouted sky pixel more
than one pixel from any current geometry, i.e. the trailing band of anything routed that
moved more than a pixel per frame across the sky (ships, stations, asteroids). The
mechanism is general: any sentinel pixel whose reprojected tap lands on last frame's
geometry is current-only for that frame under strict. Unrouted content itself (blended
distant stations, lasers, trails: sentinel depth in both frames) is not touched, so the
sentinel stabiliser's accepted behaviour (run216, run221) stands, but a thin unrouted
effect crossing a hull edge (a laser or trail leaving a ship's silhouette, a blended station
behind a moving hull) loses one frame of history on the sky pixels it enters that the hull
covered a frame earlier; the Run 60 unmatched-static path (`--taa-unmatched-static node`)
is a routed-draw rule (alpha 1) that the term never reaches.

Fixture: `verification/probe/temporal_pass_fixture.cpp`, edge case (d) "SETA sweep" in
`run_temporal_pass.py` (numbers in docs/verification/temporal-resolve.md): a black 8x8 square
at depth 0.9997, static 8 frames then +5 px/frame for 4 frames over a textured sky
(colour only, sentinel depth), motion the exact displacement; loose reproduces the smear on
the uncovered band, strict is current-only there and bit-identical to loose eight pixels and
more from the square. Edge case (e) "fade-band sweep": the same square routed and
depth-writing for 8 frames, then a fade-band draw (alpha 1, depth target 0.9997, RT2 masked)
moving over the sky: strict is bit-identical to loose on every pixel it routes and those
pixels keep using history. The routed-motion half of the acceptance check is the fixture's own
producer contract (synthetic rows); the live route's vectors are established by section 1.

Open: the run235 SETA capture also shows the engine's own star streaks (one-frame lines the
resolve removes); the 1-px dilation band beside a fast silhouette is section 4. Whether the
strict rule should become the default is a flight decision (Run 66).

## 4. The dilated band beside a fast silhouette: the band term

Run 244 (Run66, strict on, `/tmp/x3-bottleX3-run244`, ledger section "Run 244") cut the dark
sky pixels of the SETA burst by 42 % and the 1-2 px band by 63 %, and the user still saw a
little smearing. What strict left is the band the fixture reported as `adjacent_max 0.738`: a
far-plane pixel a valid neighbour won the dilation for takes that neighbour's correspondence
(`nearest < 1`, the geometry path), so the strict term (`nearest == 1`) never reaches it. For
a static or slow object that is the anti-aliased edge: the tap lands on this pixel's own
previous texel, which alternates hull and sky with the jitter, and the pixel converges to its
mean coverage. At 5-30 px/frame the tap lands on last frame's silhouette a whole pixel or
more away from this one (the previous edge pixel with its accumulated hull share, or the
hull interior when the vector is off by the block-match error of section 1), the 3x3 clip
admits it because the hull is in the neighbourhood, and the sky pixel beside the moving hull
is dark for that frame on both the trailing and the leading side.

**Term.** `resolve.hlsl`: `band = nearest < 1 ? farPlaneTerm : 0` is read after the dilation
loop, before the alpha scaling (an own-path far-plane pixel, the fade-band draw included,
has `nearest == 1` there and never enters the band), and cleared when the pixel's own motion
alpha is 1 (`band *= step(fetch(motionOverride, uv).w, 0.5)`, one fetch): a routed
sentinel-depth draw beside closer geometry (a fade-band square, an engine glow) keeps the
geometry path it takes today, the same exclusion as the far-plane term's. The reference is
`cameraUV`, the camera path at the dilated position as the resolve already computes it
before the motion override. What the route uploads there is `camera_far_plane_reprojection`
(zero z column, no translation), so whatever `nearest` is, `cameraUV` is the rotation-only
path of a direction at infinity, not the thin region's translation-aware gate (its c8 terms
live in the line mask, not here), and `relative = (previousUV - cameraUV) / sizeJitter.xy`
is the routed correspondence's **translation parallax** in px/frame: an approaching station
under SETA moves 3-38 px/frame that no rotation explains; a pan is rotation and gives 0.
`refused = dot(relative, relative) >= c5.y ? band : 0` with c5.y the band threshold squared
(`X3M_TAA_SKY_HISTORY_BAND_PX`, 1..16 px/frame, default **3**, uploaded by the pass in the
previous-jitter lane the resolve never read; `--taa-sky-history-band-px`, requires `--taa`),
and the disocclusion sum starts at `considered = refused * c7.z` instead of 0: under strict
(`c7.z = 3`) a refused pixel's `proven` (at most the footprint's weight, 1) never reaches
`considered - 0.001` and the lookup is refused, the pixel current-only that frame; under
loose (`c7.z = 0`), below the threshold, on a routed pixel and on every geometry pixel
nothing changes. Feeding translation-aware rows into the resolve's matrix would make the
reference follow a static silhouette under translation and silently disable the term (the
comment at the `cameraUV` site says so). Consequence to watch in the next strict flight:
any camera translation whose parallax at a nearby world-static silhouette exceeds the
threshold (a close flyby, a docking approach) puts that 1-px sky border on the current-only
path, which is the intended behaviour for uncovered sky but less anti-aliased than loose.
The threshold against the captures: in the SETA bursts the camera does not rotate
(`rotation_deg` 0 in run244 burst 1), so the routed displacement of the station bucket is
the parallax itself: run244 f3740 / f3750 / f3760 median 3.93 / 3.54 / 3.95 px/frame (p10
0.63-0.82, p90 14-25), run235 f6970 / f6978 / f6986 / f6990 median 3.14 / 4.31 / 7.55 /
8.75; the share of hull pixels below 3 px/frame is 0.40-0.43 (run244) and 0.34-0.49
(run235), below 2 px/frame 0.31-0.43 and 0.32-0.35. The default 3 refuses the band beside
the moving half of the hull and leaves the slow parts (the far side of a large station,
0.6-1 px/frame) accumulating; 2 px is about 2 sigma of the route's block-match error (0.3
+- 0.9 px/frame, section 1), which a world-static border could cross sporadically. Two
forms were measured and rejected on the way: lowering the tolerance so that only sentinel
taps prove (`adjacent_max` 0.738 -> 0.569: a fast band pixel's tap lands beside the previous
edge, and the sentinel-depth texel there carries that edge's accumulated hull share, as dark
as a hull tap) and a screen-speed gate, which would have made the border of every
world-static silhouette current-only during any pan faster than the threshold (edge crawl).

**Slot budget.** The term costs `add, cmp` (band), `texld, add, cmp, mul` (alpha gate),
`add, mul, dp2add, cmp` (parallax and gate; the reciprocal of the pixel size is shared with
the tap position) and `mul` (considered), 11-12 slots in every variant. Paid for by three
exact rewrites of shared code: (1) the disocclusion tap's proof, `validDepth(previous) &&
previous >= expected - tolerance` or the sentinel pair, became `threshold = max(expected -
tolerance, 0)` outside the loop and `proven += weight * dot(step(float2(threshold, -1e30),
previous), step(previous, float2(1, -0.5)))` inside it (the lower bound of validDepth folded
into the threshold, the two exclusive range pairs as one float2 step pair, a proof of exactly
0 or 1 scaling the weight; a NaN tap fails every step; 15 arithmetic slots to 9); (2) the
lookup's usability test, `!validDepth(expectedDepth) || any(previousUV < 0) ||
any(previousUV > 1)`, became `dot(step(0, lookup), step(lookup, 1))` with `lookup =
float3(previousUV, expectedDepth)`, 3 exactly when all hold (19 slots to 7 with the old
`valid` flag); (3) the `valid` flag itself became a count, `validity = dot(step(abs(clip),
1e20), 1) - step(clip.w, rejection.w)` on the camera path and `dot(step(abs(motion), 1e20),
1)` on the routed one, 4 exactly when the correspondence is finite, within 1e20 and in
front of the minimum W (a NaN w fails the magnitude step whatever the w step does), summed
with (2) into one `< 6.5` test. A vectorised Catmull-Rom weight chain was tried and
rejected: fxc split the zero components into 17 instructions against 11. Budgets: age_line
511 -> **507**, far_camera 509 -> **507**, age_filter 508 -> **506** (`RESOLVE_BUDGET`,
temporal-lattice.txt; the full table is in the ledger entry).

**Fixture.** `run_temporal_pass.py` edge cases: (d) SETA sweep gains the strict metric
`adjacent_max <= 0.10`; (f) slow sweep, the same square at 0.5 px/frame, strict bit-identical
to loose on every pixel and frame; (g) static ring, the square static and routed for two
periods, per-pixel temporal variance of the last period by Chebyshev distance 1 / 2 / 3 from
the union of the jittered footprints, strict bit-identical to loose (under a static camera an
own-path tap is the pixel's own previous texel, sentinel depth: the review's predicted ring
effect needs a fractional footprint, which the flight measurement below rules out as a
strict effect); (h) pan, the camera yawing 3 px/frame past the world-static square from
frame 0 (the sky through an NDC translation in `clip_to_previous`, the square through its
routed motion), strict bit-identical to loose on every pixel with the border accumulated
(the parallax is 0); (i) a fade-band strip beside a 5 px/frame depth-writing occluder: the
strip pixels beside it take the occluder's motion, so their history is not bit-identical
between the modes (their taps land on sky the band term refused a frame earlier) and the
oracle is the refusal itself: strict leaves none of them current-only that loose accumulates
(the alpha gate; without it every one would be); (j) the
projective pan, the route's far-plane matrix form of a yaw (P R P^-1, z row = w row x (1 -
2^-16)) through the projective divide at the band's depth, the square following the exact
inverse mapping of its centre, strict bit-identical to loose; (k) the camera path off the
scale (x row 1e15, the largest magnitude prepare accepts): the band refuses under strict
(fail closed, equal to the current sample), no output nonfinite; a NaN camera path is
unreachable (prepare bounds the rows, every operand is finite). Numbers in
docs/verification/temporal-resolve.md, "Band term".

**Ring question, flight numbers.** Sky pixels by Chebyshev distance from the station bucket
(depth >= 0.99), sky in both frames (not the freshly uncovered band), cockpit excluded;
"current-only" = `taa == hdr` exactly (a refused lookup). run244 burst 2 (strict, slow pan,
31 consecutive pairs) against run235 burst 2 (loose, SETA approach, 30 pairs):

| | run244 (strict) dist 1 / 2 / 3 / far sky | run235 (loose) dist 1 / 2 / 3 / far sky |
| --- | --- | --- |
| frame-to-frame std of the band's mean luma (8-bit present) | 0.72 / 1.08 / 0.93 / 0.35 | 1.27 / 0.82 / 0.79 / 0.08 |
| current-only fraction | 0.001 / 0.066 / 0.073 / 0.024 | 0.002 / 0.011 / 0.012 / 0.032 |
| of the band's pixels that had geometry within 1 px last frame and none in the 3x3 now (the only pixels the strict term can refuse), current-only | 0.000 / 0.000 / 0.001 | 0.000 / 0.002 / 0.002 |

The ring's luma flicker is the same order under strict and loose (its excess over the far-sky
control is smaller under strict: 0.6 against 0.7), and the pixels the strict term can refuse
are current-only at 0.0-0.1 %: the ring flicker is pre-existing (the jitter ripple of an
anti-aliased edge, (1 - w) times the hull/nebula contrast per phase) and the strict term is
not changed for it. The 6-8 % current-only fraction at distance 2-6 in run244 (1.1-1.5 % in
run235, 2.4-6.3 % on far sky in both) is not attributable to the strict term either (it sits
on pixels with no geometry within 1 px last frame); it is listed as an open observation.

## 5. What the band term leaves: the exit reset

Run 249 (`docs/verification/temporal-resolve.md`, "Run 249") showed the residual 3-12 px trail
enters the sky's history *below* the band threshold (97-98 % of it at 0.25-3 px/frame of parallax,
`verification/results/run249-band/band_parallax_fine_out.txt`) or while covered, and then survives
by ordinary accumulation outside the band. The band threshold is not the lever. The fix is the
**exit reset** of `docs/architecture/seta-sky-hull-share-decay.md`: on the age variants a band
pixel that accepts history at or above `--taa-sky-history-exit-px` (default off; 0.25 is the flown
candidate) writes its age negated into the existing R32F age target; the next frame a strict-sky
pixel whose nearest reprojected age texel is negative keeps no history once (`keep` 0 at the
blend, its count restarted: the output is the current sample, the note's `considered` form moved
to the blend because an age read before the depth verdict made the compiler regroup the
Catmull-Rom weights of the age variants), so the hull share leaves in one frame instead of
decaying at 0.9. The band, pans, static edges, routed pixels and loose are untouched by
construction: the mark is a select on `band` against a floor the pass uploads as 1e30 px^2
whenever the option is off or the strict term is not in effect, and the reset is gated by the
strict term's own `tolerance` (below 0 only on a strict-sky pixel under strict). Requires `--taa-sky-history strict`
and an age program (`--taa-far-stabiliser`, `--taa-thin-region` or `--taa-adaptive-weight`);
the fixture rows (case (l), straight, yaw +-0.3 px/frame, sub-floor and loose variants) are in
the ledger entry "2026-09-22 exit reset".
