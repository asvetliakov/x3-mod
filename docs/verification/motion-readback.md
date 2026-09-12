# Motion readback analysis

`tools/analysis/analyze_motion_readback.py` checks the live route's RGBA32F
readbacks against the capture log without any game geometry, shader bytes or
renderer code. It exists so that the gameplay captures of the live route
([live motion route, Verification](../architecture/live-motion-route.md#verification))
can be judged offline before any temporal consumer reads the output.

```sh
python3 tools/analysis/analyze_motion_readback.py <captures>/session-*.log --label <label>
python3 -m unittest verification/analysis/test_motion_readback.py
```

Outputs: `verification/results/motion-readback-<label>-summary.json` and
`motion-readback-<label>.txt`. Exit status 0 when every check passes or is
unavailable, 1 when a check fails, 2 when the input cannot be analysed (no
`motion_output_readback` line). The log is streamed once; only bounded
per-draw metadata is kept, so gameplay logs of any size are acceptable. A
1280×768 frame with 300 routed draws takes about 2 s and 80 MB in pure Python.

## Inputs

- The capture session log. Used lines: `frame_begin`/`frame_end` (capture
  flag), `draw` (shader hashes), `viewport`, `constants`/`constant kind=vs
  type=f reg=24..27` (the submitted rows, exact bit patterns, sparse-zero
  handled as documented in [capture format](../architecture/capture-format.md)),
  `motion_route` (gate, routed, matched, key fields, `rows_hash`),
  `motion_output_readback` (file name, dimensions, HRESULT),
  `motion_output_frame` (counters, `committed`), `motion_output_reset` and
  `motion_output_device`.
- `motion_<device>_<frame>.rgba32f` beside the log (or `--readback-dir`):
  row-major `width × height` RGBA float32, exactly 16 bytes per pixel as
  `MotionOutput::readback` writes it.
- `depth_<device>_<frame>.r32f`, the route's RT2 readback of the same frame
  (row-major R32F device depth, -1 sentinel; logged as
  `motion_output_depth_readback`, `--depth-pattern` is the fallback name).
  Captures made before temporal step 1 have none; the depth checks then
  report `unavailable`. See [depth](#5-depth-image-and-previous-depth-cross-check-depth_image_integrity-depth).
- With `X3M_TAA_DEBUG` (temporal step 3): `taa_<device>_<frame>.rgba16f`, the
  resolved FP16 image the route copied back into the main target (row-major
  RGBA binary16, 8 bytes per pixel, logged as `motion_output_taa_readback`),
  and `color_<device>_<frame>.bgra8`, the 8-bit main target read before the
  resolve (row-major A8R8G8B8, logged as `motion_output_color_readback`).
  See [resolved image](#6-resolved-image-sanity-signal-taa_image).

## Conventions assumed

From `src/temporal/rigid_motion_ps.hlsl` and `src/temporal/README.md`:

- Alpha `1`: RG previous absolute texture UV including the half texel, B
  previous clip Z/W; alpha `-1`: sentinel; alpha `0`: camera-valid, which this
  producer never writes; anything else or nonfinite is an ABI violation.
- D3D9 raster samples are integers: pixel `(px, py)` has current NDC
  `(2·px/W − 1, 1 − 2·py/H)`. The shader writes
  `uv = ndc_prev.xy·(0.5, −0.5) + 0.5 + 0.5/size − prior_jitter`. With no
  jitter (checkpoint B1, `--jitter-uv 0 0`) a static object therefore reports
  exactly the texture-centre UV `((px + 0.5)/W, (py + 0.5)/H)` of its own
  pixel, and the pixel displacement is `(uv_prev − centre)·size`.
- **`--jitter-from-log` (required for `--motion-jitter`/`--taa` captures).**
  `prior_jitter` is zero in every build so far: the producer's UV is always
  **unjittered**. The *raster* is not — with jitter on, the route offsets the
  projection of every scene draw, so a static object is rasterized at `p + j`
  where `j` is that frame's `motion_output_frame jitter_x/jitter_y` (pixels,
  +X right, +Y down). Comparing the unjittered UV with the jittered pixel
  reports a constant `−j` displacement for a static scene, and pushes
  `row_consistency` towards its tolerance. `--jitter-from-log` takes each
  captured frame's own jitter from the log (ignored when that frame's
  `jitter=0`) and unjitters the raster pixel in the displacement, static and
  row-consistency comparisons, and offsets the previous-frame coverage lookup
  in `temporal_coverage` by `jitter_previous_x/y`. `depth` already applied
  `jitter_previous_x/y` unconditionally, because RT2 *is* a jittered raster.
  This is a different quantity from `--jitter-uv`, which models a producer-side
  `prior_jitter` and is one constant for the whole run; leave it at `0 0`.
  Measured effect on the iteration-7 capture: a stationary burst's median
  displacement falls from 0.31–0.38 px (exactly `−j`) to 0.0010 px, and
  `row_consistency`'s maximum error from 0.44 px to 0.0081 px.
- Rows `c24–27` are applied as `clip_i = dot(row_i, position)`; the translation
  column `(c24.w, c25.w, c26.w, c27.w)` is the clip position of the object
  origin. This is the convention the fixture rows follow (`c27 = (0.25,0,0,1)`
  gives `w = 0.25·x + 1`) and it reproduces the seam readback to 1.6e-6 px.
- History reconstruction follows `motion_row_history`: the previous table holds
  frame N−1's routed draws with gate 0 or 6, keyed by the 25 `motion_route`
  key fields (object/camera identity and epochs, geometry bindings, the
  position program hash with its declaration offset and type, and the draw
  arguments, the logged parts of the DLL's `RigidDrawKey`); a duplicate
  previous key poisons it, a duplicate current key is
  consumed once; a `motion_output_reset` empties the table for later lookups
  and for the next frame. Previous rows are known only when frame N−1 was
  captured (`frame_end capture=1`) and committed, so **gameplay captures must
  request runs of consecutive frames**; the first frame of each run has
  `static_state = unknown` and no row-pair check.

## Checks

### 1. Integrity and counters (`readback_integrity`, `counter_consistency`, `history_pairing`)

Per captured frame: file present, size `W·H·16`, HRESULT 0, no nonfinite
component, alpha in {1, 0, −1}; validity counts and fractions; valid-pixel UV
range, depth range and the number of previous UVs outside [0,1]. The
`motion_route` lines must reproduce the `motion_output_frame` counters
(`routed`, `matched`, `gate3..6` exactly; `gate1`/`gate2` may exceed the lines
because Feature/Scene rejections of non-scene draws write no line; the
difference is reported as `non_scene_draws_in_counters`). A frame with
`matched=0` must have no valid pixel. The log-reconstructed previous table must
agree with the DLL's `matched` flag for every predictable draw
(`history_pairing`). Routed draws whose viewport is not the full target are
noted because the pixel-centre convention assumes a full viewport.

### 2. Static consistency (`static_consistency`)

A frame is *static* when every matched draw's rows are bit-identical to the
previous rows it paired with (bits, or `rows_hash` when the constants were not
captured). Then every valid pixel must satisfy
`|uv_prev·size − (px + 0.5, py + 0.5)|∞ ≤ --static-tolerance-px` (default
0.01 px); the maximum error and violation count are reported. Frames are
labelled `static`, `moving`, `unknown` (previous rows unavailable) or
`no_matched`.

### 3. Displacement and row-pair consistency (`displacement`, `row_consistency`)

Displacement per valid pixel in pixels (previous minus current), with
min/median/p95/p99/max, a magnitude histogram and the count above
`--displacement-bound-px` (default 64); the check fails when the suspicious
fraction exceeds `--max-suspicious-fraction` (1%).

Footprint attribution without geometry is possible through the rows
themselves: for a matched draw with rows `M_cur`/`M_prev`, a pixel that belongs
to it satisfies `M_cur · inverse(M_prev) · (x', y', z', 1) ∝ (x, y, ·, 1)` where
`(x', y', z')` is the previous NDC recovered from its UV and depth and `(x, y)`
the pixel's own NDC. Draws sharing a map (static objects under one camera share
`P·V_cur·V_prev⁻¹·P⁻¹`) form one group. Every sampled valid pixel (uniform
stride, at most `--max-consistency-pixels`, default 50 000) is mapped through
every group; it is *explained* when some group lands within
`--consistency-tolerance-px` (0.5 px) and attributed to the best group. The
check fails when the unexplained fraction exceeds `--max-unexplained-fraction`
(1%). Per matched draw the report gives the expected screen displacement of the
projected object origin (translation columns divided by w, `None` when w ≤ 0)
next to the median displacement of the pixels attributed to its group; the two
differ legitimately under perspective or rotation (the seam's oversized
triangle: −1.60 px origin vs −1.87 px median), so this comparison is
informative while the per-pixel error is the criterion. Draws with unknown or
singular previous rows are listed under `skipped_draws`.

### 4. Temporal cross-check (`temporal_coverage`)

For consecutive captured frames N, N+1 of one device with equal dimensions:
each valid pixel of N+1 names a previous texel `floor(uv_prev·size)`; the
fraction of in-range texels that were valid (alpha 1) in frame N is reported,
with off-screen counts. Because the sentinel marks both unrouted draws and
routed-but-unmatched draws, the pass criterion (`--temporal-coverage-min`,
0.9) applies only when frame N matched at least
`--temporal-criterion-min-matched-ratio` (0.99) of its routed draws; other
pairs are reported as informative. A history pairing error (wrong previous
draw) shows up here as a low fraction even when checks 2–3 pass.

### 5. Depth image and previous-depth cross-check (`depth_image_integrity`, `depth`)

Since temporal step 1 the route reads RT2 back in capture frames as
`depth_<device>_<frame>.r32f` (row-major R32F device depth in [0,1] where a
routed draw covered the pixel, -1 elsewhere, MinZ 0 / MaxZ 1 viewport) and
logs it as `motion_output_depth_readback`; the analyzer takes the file named
there (falling back to `--depth-pattern`). Two checks use it:

- `depth_image_integrity`: every value finite and either the -1 sentinel or
  in [0,1]; the sentinel and written fractions, the written range and the
  number of motion-valid pixels whose depth is the sentinel (zero when every
  routed row carries the depth output) are reported per frame; a nonfinite or
  out-of-range value fails.
- `depth`: frame N+1's B channel (expected previous device depth) at each
  valid pixel against frame N's image at the previous UV. The producer's RG is
  the previous **unjittered** texture-centre UV while frame N was rasterized
  with its own jitter, so the sample position is RG plus frame N's jitter in
  UV units (`jitter_previous_x/y` of frame N+1's summary, zero without
  jitter). Sampling is nearest by default (`--depth-sampling bilinear`
  averages the four surrounding texel centres and drops sentinel taps,
  renormalizing); a sentinel or off-screen sample is counted, not compared.
  The absolute error distribution (min/median/p95/p99/max and a histogram
  with edges 1e-6..1e-1) and the fraction within `--depth-tolerance` (1e-4,
  the resolve's default rejection tolerance) are reported; the check fails
  below `--depth-within-min` (0.99). Disoccluded positions whose previous
  depth belongs to another surface count as errors, so the threshold must be
  read together with the temporal coverage fraction.

The cut detector's data (`motion_output_cut` and the summary's `cut`,
`cut_median_px`, `cut_missing`, `cut_samples`, jitter fields) is reported per
frame under `cut` without a verdict of its own; since step 3 the resolve
rejects history for a frame whose verdict is set.

### 6. Resolved image sanity signal (`taa_image`)

When a frame logs `motion_output_taa_readback`, the analyzer decodes the FP16
image, requires every RGB value to be finite (a nonfinite value fails the
check and the readback integrity) and compares each pixel against the
pre-resolve 8-bit color: the fraction of pixels whose RGB moved by more than
`--taa-threshold` (default 2/255) in any channel is reported per frame with
the maximum and mean difference. This is a sanity signal, not a quality
judgement: it says the resolve produced finite values and how much of the
image the history changed (zero for a current-only frame, the first frame,
a cut or a sentinel-only run; the first gameplay run will show what fraction
of a moving frame the history touches). A logged resolved image without its
color image is malformed.

## Results on the synthetic fixture

`verification/results/motion-readback-fixture-summary.json` /
`motion-readback-fixture.txt` come from the seam-on run of the temporal
step 1 suite (log sha256 `344fa4b7…`, eight captured 64×64 frames with
motion and depth readbacks, 6,082 valid pixels): **PASS**. Integrity,
counters and history pairing pass on all frames (12 predictable draws,
including the Reset after frame 8's decisions and the duplicate-key cases).
No fixture frame is static (frame 1's previous frame 0 is not captured;
frames 4–8 move), so the static check is unavailable there and is exercised
by the unit fixtures instead. Row-pair consistency explains every sampled
pixel in frames 4–8 with a maximum error of 0.0018 px, matching the CPU
oracle's 0.0016 px. Displacements are 0.7–4.1 px, none suspicious. Temporal
coverage: 1.0 for 4→5 and 6→7, 0.9 for 5→6 and 7→8 (the seam
alternates triangle depth/offset between frames, so a few small-triangle
pixels point at texels the previous frame did not cover), 0.0 for 3→4 where
frame 3 matched nothing (informative). **Depth image integrity** passes on
frames 1–8 (sentinel fractions 0.346–0.958, the
latter frame 7 where only the small triangle routes; no motion-valid pixel
without depth) and the **previous-depth comparison** evaluates frames
[4, 5, 6, 7, 8]: 3,343 pixels compared, 57 previous taps on the
sentinel (excluded), maximum error 0 (the fixture's stationary
depth 0.5/0.6 surfaces reproject exactly), within fraction 1.0.
The cut data is reported per frame (median 1.6 px, verdict 1 in the frames
whose keyed draws miss). `motion-readback-fixture-jitter-summary.json`
repeats this on the seam run with `X3M_MOTION_JITTER=1` (log
`6838da03…`): the comparison applies the logged previous jitter
([-0.375, -0.055556], [0.125, 0.277778], [-0.125, -0.277778], [0.375, 0.055556], [-0.4375, 0.388889] px for frames [4, 5, 6, 7, 8]), 3,380 pixels compared,
maximum error 0, within fraction 1.0. In that run the row-pair consistency
check reports a maximum error of 0.44 px: it maps pixels through the
unjittered rows while the raster is displaced by the current jitter, so a
jittered capture must be analyzed with `--jitter-uv` set to the frame's
current jitter in UV units (the analyzer has no per-frame value yet; an open
item for the gameplay run with jitter on).

The unit tests (`verification/analysis/test_motion_readback.py`, 19 cases)
generate 16×16 logs and readbacks with an independent forward model (affine
and perspective rows) and cover the static test and its tolerance, displacement
statistics, row-pair consistency and its rejection of shifted previous UVs,
suspicious displacements, the temporal check with and without its criterion,
history-pairing disagreements, counter mismatches, the depth image (sentinel
exclusion, integrity failures, the previous-jitter offset, nearest and
bilinear sampling, the logged readback line and the cut data),
truncated files, nonfinite and out-of-ABI values, logs without readbacks and
the CLI outputs.

## Acceptance criteria for the gameplay run

Before any temporal consumer reads RT1, one user-managed gameplay capture of
the live route (1280×768, requested frames in runs of at least three
consecutive frames, several runs including a stationary view) must satisfy,
with the default parameters:

1. `readback_integrity`, `counter_consistency` and `history_pairing` pass on
   every captured frame; zero nonfinite or out-of-ABI pixels; no valid pixel in
   a frame with `matched=0`.
2. `static_consistency` evaluated on at least one stationary burst. The
   iteration-6 run showed that gameplay never produces bit-identical rows even
   with the view held still: the engine recomputes the rows with floating-point
   noise every frame. The criterion is therefore stated in measured pixels: a
   burst whose median valid-pixel displacement is below 0.01 px counts as
   stationary, and in such a burst the row-pair check must explain every
   sampled pixel at 0.5 px with a maximum error below 0.2 px. Three bursts of
   the iteration-6 run met this (medians 0.001–0.002 px). The bit-identical
   form of the check remains in the analyzer for synthetic fixtures.
3. `row_consistency` pass over at least three moving frames and 50,000 sampled
   pixels: unexplained fraction ≤ 1% at 0.5 px, with the maximum explained
   error reported.
4. `displacement`: suspicious fraction ≤ 1% at 64 px; the per-frame median and
   p99 must be plausible for the recorded motion and reported in the summary.
5. `temporal_coverage` ≥ 0.9 on every pair where the criterion applies, and at
   least two such pairs.
6. Color bit-identity with routing disabled for the same scene is checked by
   the route's own capture comparison, not by this tool; it remains required.
7. `depth` stays unavailable until the depth readback above exists; enabling
   the consumer without it relies on the resolve's own depth rejection.

Any failing check blocks the consumer; the summary JSON and text report are
committed under `verification/results/` with the capture's log hash.
