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
- Optional `depth_<device>_<frame>.r32f` (`--depth-pattern`): row-major R32F
  device depth of frame N at the readback dimensions. **No current capture
  writes this file**; see [depth](#5-previous-depth-cross-check).

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

### 5. Previous-depth cross-check (`depth`)

Frame N+1's B channel is the depth the surface had in frame N. Comparing it
against frame N's device depth at the previous texel needs a depth image of
frame N at the readback dimensions. **Current captures provide none**: the
capture log records `scene_depth_copy`/`scene_depth_boundary` events for the
GPU-side D24X8 snapshot ([copied depth](copied-depth.md)) and the R32F
[decoder](depth-decode.md) is verified in fixtures, but neither the snapshot
nor a decoded image is read back to disk, and `draw`/`constant` records carry
no depth. The check therefore reports `unavailable`. Enabling it requires the
scene-depth adapter to decode its snapshot at the scene boundary and write
`depth_<device>_<frame>.r32f` (row-major R32F device depth in [0,1], MinZ 0 /
MaxZ 1 viewport) beside the motion file in requested capture frames; the
analyzer then reports the absolute error distribution and the fraction within
`--depth-tolerance` (1e-4 device-depth units, the resolve's default rejection
tolerance) and fails below `--depth-within-min` (0.99). Depth-1 (cleared)
texels at disoccluded positions count as errors, so the threshold must be
read together with the temporal coverage fraction.

## Results on the synthetic fixture

`verification/results/motion-readback-fixture-summary.json` /
`motion-readback-fixture.txt` come from the seam-on run
`verification/probe/build/motion-output-seam-on-20260912-031430-123134`
(log sha256 `5424c7da…e9632`, eight captured 64×64 frames, 6,082 valid
pixels): **PASS**. Integrity, counters and history pairing pass on all frames
(12 predictable draws, including the Reset after frame 8's decisions and the
duplicate-key cases). No fixture frame is static (frame 1's previous frame 0
is not captured; frames 4–8 move), so the static check is unavailable there
and is exercised by the unit fixtures instead. Row-pair consistency explains
3,402/3,402 sampled pixels in frames 4–8 with a maximum error of 0.0018 px,
matching the CPU oracle's 0.0016 px. Displacements are 0.7–4.1 px, none
suspicious. Temporal coverage: 1.0 for 4→5 and 6→7, 0.9 for 5→6 and 7→8 (the
seam alternates triangle depth/offset between frames, so 19 of 190 small
triangle pixels point at texels the previous frame did not cover), 0.0 for
3→4 where frame 3 matched nothing (informative). Depth: unavailable.

The unit tests (`verification/analysis/test_motion_readback.py`, 15 cases)
generate 16×16 logs and readbacks with an independent forward model (affine
and perspective rows) and cover the static test and its tolerance, displacement
statistics, row-pair consistency and its rejection of shifted previous UVs,
suspicious displacements, the temporal check with and without its criterion,
history-pairing disagreements, counter mismatches, the optional depth image,
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
