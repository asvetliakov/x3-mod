# Temporal resolve jitter-convention review: history at "pixel center minus velocity"

Independent review of the uncommitted change set that fixes the stationary
trembling and blur seen in gameplay with `X3M_TAA=1` (`src/temporal/resolve.hlsl`,
`resolve.h`, `rigid_motion_ps.hlsl`, `README.md`, `src/renderer/temporal_pass.h`,
the regenerated `temporal_resolve_program_inc.h`, the two fixtures, the
runners, the analyzer's `--jitter-from-log`, the design documents and the
stationary-regression evidence). The iteration-07 analysis files are another
agent's and were not reviewed. Every suite below ran on the final tree after
fresh `build/` and `build-ownership/` rebuilds; result files were queried
with scripts, never read whole.

## Derivation

Conventions: jitter `j` (raster pixels, +x right, +y down) shifts the
rasterized image by `+j`; output pixel `p` represents unjittered position
`p`; the current sample at `p` is scene content `p - j_cur`; the motion
producer at `p` reports the previous unjittered position `q` of that content;
the accumulated history lives on the unjittered grid. Standard TAA reads
history at the pixel center minus the velocity of the sample under it:
`p - ((p - j_cur) - q) = q + j_cur`. For static content `q = p - j_cur`, so the
tap is `p` itself (`f = 0`, no resampling) and the sub-pixel offset of the
sample is the supersampling. Reading at `q` verbatim would resample at a
fractional position every frame (blur); reading at `q + j_prev` (the old
shader) moved the tap by the jitter difference every frame (oscillation plus
blur), which is the gameplay defect.

The shader implements exactly this. Motion path: `previousUV = motion.xy +
sizeJitter.zw` (`resolve.hlsl:99`); `sizeJitter.zw = j_cur / size` in UV,
same axes as UV, so the sign is right, and RG already carries the half texel.
Camera path: `uv - 0.5 texel - j_cur` (`:81`) recovers the content's raw
raster position `p - j_cur`, the matrix maps it to `q`, and `+ 0.5 texel +
j_cur` (`:92`) restores the same jitter; the offset therefore transforms
through the matrix, which the zoom case checks (8 -> 9 -> 10 -> 9). The
previous jitter (`c5.xy`) is packed by `prepare` and never read. Producer:
`rigid_motion_ps.hlsl:18` emits `ndc -> UV + 0.5 texel - c0.zw`; interpolating
the previous unjittered rows across the current jittered raster yields the raw
previous raster position of the content at each jittered sample, so with
`c216.zw = 0` (`motion_output.cpp:1220`, the `pixel[]` literal) RG is `q`
with the texture half texel, exactly the ABI the resolve consumes; the
route's `apply_jitter` (`motion_output.cpp:1113`) moves the raster by
`(+jx, +jy)` and `resolve()` hands the same `jitter_` to the pass
(`motion_output.cpp:319`), so the sign and magnitude agree with `prepare`.
Bilinear footprint, depth rejection and reactive/sentinel logic are unchanged:
the 3x3 clamp is gathered around `uv` on the current jittered raster
(`:122-127`), the four history taps sample previous depth and previous
reactive at the corrected positions and compare against `motion.z` or the
reprojected `z/w` (`historyTap`), the sentinel/early-out order is untouched
(`:68-72`), and the only diff hunks are the two lookup lines and comments.

## Fixture oracles

The stationary scene's oracle is independent of the shader's convention: the
raster is displaced by `+j` analytically (`quad`: area edge `e` at `e - 0.5 +
j` in pre-transformed space), the correspondence is the analytic producer
definition `(p + 0.5 - j)/S` (not the shader's output), and the one-step model
reads the previous *output at the same pixel* with the 3x3 clamp and FP16
rounding, never `q + anything`; the phase-stability, DC-gain, analytic
coverage and centroid metrics do not use the lookup formula at all. The
detached cases are sign-sensitive: the motion case yields 0.5625 for the
current jitter against 0.40625 (previous), 0.40625 (flipped) and 0.484375
(none); the zoom case yields 0.5625 against 0.25 for every wrong convention;
the route cases separate own position (0.375), neighbor (0.5) and rejection
(0.25) with distinct history and current checkers. The 32-frame stripe
accumulation now asserts the pure per-pixel exponential average (CPU model
0.4536 / 0.5083 with FP16 rounding, reproduced by an independent script,
measured 0.4526 / 0.5073), which the old blend-the-neighbors convention
cannot produce; it is not sign-sensitive on its own (the stripe is
2-periodic), the motion and zoom cases are.

## Findings and fixes

1. **Low, fixed** - the negative proof (stationary scene against the
   previous-jitter shader) existed only as a manually produced pair of
   untracked result files from a checked-out commit; nothing in the tree
   reproduced it, and no automated case exercised the flipped-sign or
   no-jitter conventions on the stationary scene. `run_temporal_pass.py` now
   mutates the two lookup lines of `resolve.hlsl` in a temporary copy into
   `previous-jitter` (`+ history.xy`), `flipped-sign` (`- sizeJitter.zw`) and
   `no-jitter`, runs the fixture's `stationary-only` mode on each and requires
   exit code 1 with the one-step oracle as the failing metric and an oracle
   error above 0.1; the mutation asserts each pattern occurs exactly once so
   a later edit cannot make the control vacuous. Results:
   `temporal-stationary-negative-<variant>.txt`, numbers and hashes under
   `negative_controls` in `temporal-pass-summary.json`. The `previous-jitter`
   control reproduces the b9a1094 regression file bit for bit in its metrics
   (oracle 0.421387, drift 0.4638 px), so that file is now reproducible.
2. **Documentation, fixed** - `src/proxy/motion_output.cpp:1210-1216` still
   said the resolve "adds the previous raster jitter exactly once" when
   explaining why `c216.zw` is zero; restated for the current convention.
3. **Documentation, fixed** - the zoom-case comment
   (`verification/probe/temporal_resolve.cpp:229`) and the
   `temporal-resolve.md` table row claimed the old convention "would sample
   9.5 (0.40625)"; it samples 10.5 (previous instead of current after the
   projection), a flipped current sign 5 and no handling 8, all 0.25. The
   passing value is unaffected.
4. **Low, fixed** - `StationaryScene::correspondence` re-uploaded the 64x64
   ramp texture on every frame (a stray block on the same line as the motion
   unlock); moved to the constructor. Metrics unchanged.

Verified correct and left unchanged: both lookup lines and their sign, the
half-texel handling on the camera path, `prepare` (previous jitter packed,
unused), the producer contract and the zero `c216.zw` upload, the route's
jitter application and the pass inputs, the neighborhood clamp, depth and
reactive taps, the `stationary-only` entry point (`argc == 5`), the
analyzer's unjittering of the raster pixel and the previous-coverage lookup
(`analyze_motion_readback.py`, three new unit tests), and the bytecode
provenance: `generate_rigid_motion_pixel.py --check` recompiles
`resolve.hlsl` with the local native D3DX and matches the embedded header and
manifest (`a26c01c3...`, 1,695 words; only our authored shaders, no game
bytes).

Observations, not changed (design level):

- Silhouette edges against a surface at another depth stay current-only
  whenever their coverage flips (absolute tolerance 1e-4), so they do not
  accumulate a coverage fraction; the design document records it.
- The history depth is the previous jittered raster's depth while the
  expected depth is the content's; on one surface they agree, at depth edges
  the same limitation applies.
- `FrameInputs::previous_jitter` and `c5.xy` are dead inputs kept for ABI
  stability; harmless, and the docs say so.

## Results after the fixes

`build/` (RelWithDebInfo, `--clean-first`, by `run_motion_output.py`) and
`build-ownership/` (Release, `--clean-first`, `cmake --build`) were rebuilt on
the final tree; every suite below ran in sequence on it.

| Suite | Result |
| --- | --- |
| `generate_rigid_motion_pixel.py --check` | PASS: `temporal_resolve` 1,695 words, bytecode `a26c01c3...` equals the embedded header and manifest; `rigid_motion` 176 and `current_depth` 35 words unchanged; X3AP not running |
| `temporal_run.py` | PASS: 58/58 samples, 2 generations, RESET PASS; motion case 0.5625, zoom case 0.5625, stripe frames 30/31 0.4526 / 0.5073 against the model 0.4536 / 0.5083 |
| `run_temporal_pass.py` | PASS: 174 numerical / 162 state comparisons, 162 samples, 2 generations; stationary scene oracle error 0.000488, interior delta 0, edge delta 0.0591, ramp delta 0.0044, edge DC error 0.0040, analytic error 0.0551, ramp DC 0.0064, drift 0 px in both generations; negative controls all exit 1 on the one-step oracle: previous-jitter 0.4214 (drift 0.464 px), flipped-sign 0.4502 (0.747 px), no-jitter 0.3105 (0.404 px) |
| `run_motion_output.py` | PASS: 26 runs (22 cases + 4 bench), all 22 cases passed; production TAA 69 checks / 51 restorations, seam TAA 150 / 51, plain and through the wrapper; seam main target equals the reference resolve in all 12 frames, 555 pixels changed by history (frames 1, 4, 7, 10, 11; 374 under the old convention), `taa_image` differing fraction 0 in production and 0.0022 / 0.0229 / 0.0046 in seam frames 1 / 4 / 7; bench medians 0.336 / 1.008 ms off / on at 1280x768 and 0.609 / 2.680 ms at 5120x1440 |
| `check_no_x87.py build/d3d9.dll` | PASS: 125 reachable functions, 0 violations |
| `python3 -m unittest discover -s verification/analysis` | 462 tests OK (23 in `test_motion_readback.py` including the three `--jitter-from-log` tests; the count includes the untracked iteration-07 tests present in the tree) |

Verdict: go for installing with `X3M_TAA=1` on the documented command. The
convention is derived, implemented on both paths, proven by an oracle that
does not share the formula, and rejected under all three wrong conventions;
the stationary tremble should be gone, and the remaining visible limitation
is the current-only silhouette edge against a different depth.

