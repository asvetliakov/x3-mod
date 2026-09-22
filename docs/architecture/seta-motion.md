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
resolve removes) and the 1-px dilation band beside a fast silhouette; neither is this fix's
scope. Whether the strict rule should become the default is a flight decision (Run 66).
