# Iteration 9: camera reprojection in gameplay, and the blur question

First gameplay run of the camera-reprojection build (`1d36c29`). The user's
report is the acceptance test [iteration 8](iteration-08.md#anomalies) set for
the shimmer work — *no trembling or shimmering noticed any more* — plus a new
one: **objects look slightly blurry**.

The trembling verdict is now a user observation backed by the route's own
health numbers; the blur **cannot be measured from this run**, because it was
captured without `--taa-debug` and therefore has no colour and no resolved-image
readbacks. §4 says exactly what is missing, quantifies the resolve's low-pass
from the GPU fixture and from a model checked against it, and establishes the
sampler baseline a texture-LOD-bias decision needs. No gameplay was launched by
the agent and no `src/` change was made for this report.

## Provenance

| | run 1 |
| --- | --- |
| log | `session-20260912-160404-1632.log`, 154,442,312 B, 2,782,766 lines |
| sha256 | `ca149c414df62aabacbfecfb3ab0fdc586d182c8a23aec918f14293a169e0d47` |
| installed DLL | `db63e120afcbb38e1382f22dffb96fb6030bbab50d1c3faf2b5587e055ce7e3d` (`1d36c29`) |
| flags | `--direct --ownership --object-trace --object-lifetime --motion-output --taa --telemetry --profile` |
| mode witness | `taa=1 taa_debug=0 jitter=1 jitter_samples=8 cut_median_px=48.000 cut_missing=0.250 rt_mode=perdraw sentinel=auto camera_cut_deg=20.00 camera_log=300 state_shadow=1 scene_hook=0 hdr=0` |
| captured frames | 20 (five bursts of 4), 40 readbacks (20 motion RGBA32F + 20 depth R32F), 1280×768 |
| path | start → menu → load save → flight with a hard turn and a sector change |

Witnesses: one `motion_output_device ... enabled=1 reason=ok ...
history_available=1 history_capacity=4096 depth=1 depth_reason=ok jitter=1
jitter_samples=8 taa=1 taa_reason=ok taa_debug=0 rt_mode=perdraw camera=active`,
one `motion_output_taa device=1 initialize=00000000`, one
`motion_output_target device=1 width=1280 height=768 create=00000000
depth_create=00000000`, 56 `motion_output_variant`, `ownership_factory
mode=wrapped` (three factories, no fallback), `object_trace active=1
status=active`, `object_lifetime active=1 status=active_without_baseline`
(unchanged since iteration 5). Logs and readbacks stay untracked.

## Reproduction

```sh
L=/tmp/x3-iteration09-run1/session-20260912-160404-1632.log
C=/tmp/x3-iteration09-run1

# route/TAA health and the telemetry aggregation (iteration-8 code, unchanged;
# the image and flicker measurements need --taa-debug readbacks and are off)
python3 tools/analysis/analyze_iteration08_taa.py $L --captures $C \
    --no-images --no-flicker \
    --output verification/results/iteration-09-taa-summary.json \
    --text verification/results/iteration-09-taa-summary.txt

# the new evidence of this iteration
python3 tools/analysis/analyze_iteration09.py $L --no-camera-detail \
    --output verification/results/iteration-09-summary.json \
    --text verification/results/iteration-09-summary.txt

# camera read against the captured draw constants
python3 tools/analysis/analyze_camera_state.py $L \
    --metadata verification/results/shader-registers.json \
    --output verification/results/iteration-09-camera.json

# readbacks, both depth samplings
python3 tools/analysis/analyze_motion_readback.py $L --readback-dir $C \
    --label iteration09 --results-dir verification/results \
    --jitter-from-log --no-draw-details
python3 tools/analysis/analyze_motion_readback.py $L --readback-dir $C \
    --label iteration09-bilinear --results-dir verification/results \
    --jitter-from-log --no-draw-details --depth-sampling bilinear

python3 -m unittest verification/analysis/test_iteration09.py   # 18 cases
```

`analyze_iteration09.py` imports the record stream and the window statistics
from `analyze_iteration07_taa.py` rather than copying them; only the four things
no existing tool produces live in it (the regime split of a *single* session,
the camera field-of-view/rotation-floor derivation, the sampler census and the
resolve's low-pass model).

## 1. Route and TAA health

128 `motion_output_frame` records (every 60th frame plus the 20 captured ones).

| | frames |
| --- | ---: |
| `taa_attempted=1 taa_resolved=1 taa_result=00000000` | **108** |
| `taa_skip=2` (selector never reached the copy) | 20 |
| `taa_history=1` | 105 |
| resolved **without** history | 3 (frames 1965, 1966, 1967 — the cut, §1.2) |
| `taa_skip` 3 / 4 / 6 / 9 (no jitter, not filled, queries, camera state) | 0 |
| `motion_output_taa_failed`, `apply_failures`, `restore_failures` | 0 |
| `motion_output_reset`, `device_reset`, `motion_output_release`, `device_destroy` | 0 (no clean shutdown, fourth run in a row) |
| nonzero HRESULT that is a failure | 0 |

The only nonzero result codes in the whole log are `S_FALSE` (`00000001`) on the
20 skip frames — `fill_result`, `taa_result` and `taa_copy`, 20 each, all saying
the frame was not filled and not resolved — and 17,026 ×
`container_result=80004002` on `surface` records, which is `GetContainer` on a
plain render-target surface that has none. Everything else, 246,157 recorded
result codes, is `00000000`; all 40 readbacks report `result=00000000` at
1280×768.

**The 20 skip frames are menu frames, not a defect.** Every one has
`latched=0 routed=0 matched=0`, all 638–699 draws rejected at gate 2, `set_rt=0`,
`jitter_writes=0`, `camera_valid=0`, `scene_end_source=none`. They are frames
0–1080 (start → menu → the load) and frame 6420 (back to the menu at the end).
No scene frame was skipped.

Route counters over the 128 logged frames: 48,949 draws, 25,669 routed, 25,520
matched (**99.42%**), gates `gate1=0 gate2=15,231 gate3=1,483 gate4=6,566
gate5=0 gate6=149`. Over the 20 captured frames: 6,174 routed of 8,029, 6,018
matched (**97.5%**); the 8,029 per-draw `motion_route` records reproduce the
DLL's own counters on all 20 frames (`counter_consistency` pass), with the
per-draw gate histogram `{0: 6,118, 3: 380, 4: 1,475, 6: 56}`. The whole
matching shortfall is burst 1, the hard turn (1,854 of 2,007 = 92.4%); the other
four bursts match 100.0%, 99.8%, 100.0%, 100.0%. Gate 5 (`Scope`) still never
fires, and `object_lifetime` still runs `active_without_baseline`.

**`selector_state=9` on all 128 frames, including the 108 that resolved.** The
field is the selector's state *at the frame log*, which is after the boundary has
been consumed, so `Rejected` (9) is its terminal value on every frame and is
**not** an environment-map indicator here. The env-map signature of
[temporal-integration.md](../architecture/temporal-integration.md#environment-map-exclusion)
is the *conjunction* `routed=0 gate2=draws taa_skip=2 camera_valid=0`, which is
exactly the 20 menu frames above. A frame that both routed and reported
`selector_state=9` is the normal case, so this log gives **no evidence of
environment-map rendering during flight**; the field cannot answer the question
as logged, and a separate counter (or logging the *maximum* state reached) would
be needed to.

`rt_mode=perdraw` on all 128 frames, `lazy_flushes=0`.
`scene_end_source=stretchrect` with `scene_end_check=3` (`StretchOnly`) on all
108 routed frames and `none` on the 20 skips — as expected with
`scene_hook=0`: `hook_signals=0`, `draws_after_hook=0`, no
`motion_output_scene_hook_disagreement`. `bloom_copy_seen=1` on every routed
frame.

**State shadow.** 231,950 `rs_queries`, 231,950 `rs_hits`, **0 `rs_resyncs`**,
1,512 `rs_gets`. Every render-state query the route made over the logged frames
was answered from the shadow, and the shadow never disagreed with the device.

### 1.1 Burst classification

The camera translation and rotation the route read (§2) classify the bursts
directly; the medians are the motion readback's own per-pixel displacement
medians.

| burst | frames | camera | route `cut_median_px` | readback median displacement | verdict |
| --- | --- | --- | --- | ---: | --- |
| 0 | 1722–1725 | rotation and translation **bit-identical** across all four frames | 0.0000 ×4 | 0.0010–0.0011 px | **stationary** |
| 1 | 1964–1967 | hard turn, 2.20° then 6.23°/frame | 24.4, 78.2, 70.3, 71.0 | 18.8, 55.6, 55.5, 55.6 px | **turning** |
| 2 | 3090–3093 | rotation bit-identical, straight flight −172.5 units/frame | 0.86, 0.52, 0.52, 0.49 | 0.002 px (p95 2.7–5.1) | straight flight |
| 3 | 4801–4804 | rotation bit-identical, −172.5 units/frame | 0.23, 1.86, 1.88, 1.91 | 0.001 px (p95 3.3–3.4) | straight flight |
| 4 | 5752–5755 | rotation bit-identical, −172.5 units/frame, after the sector change | 0.013, 0.076, 0.076, 0.076 | 0.001 px (p99 18.5–23.1) | straight flight, near geometry |

Burst 0 is the strictest stationary burst captured so far: the camera buffer is
byte-identical across the burst and the route reports a zero median in all four
frames. Bursts 2–4 are straight-line flight at the same speed; their
displacement distributions are bimodal (a distant population that does not move
and a near population at 1–5 px, 18–23 px in burst 4), which is what a pure
forward translation produces.

### 1.2 Cut detector

`bound_px=48.000`, `bound_missing=0.250`. **Three frames report `cut=1`** —
1965, 1966, 1967 — with medians 78.2, 70.3, 71.0 px over 403–509 samples and a
missing fraction of 0.02–0.05. All three resolved with `taa_history=0`: the
history was rejected for the frame, which is the design. Frame 1964 (median
24.4 px) stayed under the bound. So the pixel cut detector fired exactly on the
hard turn and nowhere else in 128 logged frames.

`camera_cut=0` everywhere: the camera-rotation cut threshold is 20°/frame and the
worst rotation in the session is 6.23°, so the rotation cut never fired. The two
detectors are complementary and this run exercised only the pixel one.

**Gap: the sector change was not captured.** The camera translation jumps from
`(−75746, 12771, −124532)` (burst 3) to `(−51561, 83613, 360300)` (burst 4), so
the sector transition happened in one of the ~950 frames between them and none
of those is a logged frame. A sector change is a pure translation teleport:
`camera_cut` is a *rotation* test and would not catch it, and whether the pixel
detector did is unknown because the frame was not logged. The frames either side
are clean (`taa_history=1`, no failure), so nothing broke, but the transition
frame itself remains unevidenced.

### 1.3 Frame time

CPU wall clock only, never GPU time. Per-window statistics only: a one-second
window records count/total/min/max, so the per-window mean is the finest
statistic that exists and no session-wide median does. A **same-build
comparison is not available** — this is the only run of `1d36c29` and the
feature was on throughout — so the numbers below are medians per regime, not a
measurement of any feature.

The session is bimodal again (iteration 8 §6). 256 `frame_normal` windows, 6 of
them load/alt-tab stalls above 1 s (max 64 s):

| per-window mean | windows |
| --- | ---: |
| < 20 ms | 7 |
| 20–30 ms | 33 |
| 30–40 ms | **78** |
| 40–50 ms | **65** |
| 50–60 ms | 26 |
| 60–70 ms | 12 |
| 70–100 ms | 27 |
| ≥ 100 ms | 2 |

Split at 45 ms: **fast 165 windows, median 34.39 ms; slow 85 windows, median
59.17 ms.** By the analyzer's scene/other tag, the 200 scene windows have a
per-window mean median of 36.64 ms (p10 24.97, p90 71.80) and the 56 menu/load
windows 43.64 ms. Iteration 8's two regimes were 29.93 and 63.87 ms on a
different build and a different path through the save; the modes have moved but
the shape has not, and the pooled median still mostly reports how long the run
stayed where.

`present_normal` scene per-window mean median 36.0 µs, `draw_backend` 2.83 µs
over 2,438,084 calls, `lock_wait` 0.2 µs over 332 windows. The boundary
`StretchRect` bracket is now a real measurement rather than an upper bound,
because without `--taa-debug` it no longer contains two 31 MB readbacks:
**median 567 µs** over 20 samples (447–893 µs), against 22.6 ms in iteration 8.

### 1.4 Route CPU cost — new, and larger than expected

This build records the `route_*` and `taa_*` telemetry spans, so the route's own
CPU cost is measured for the first time in gameplay. These are CPU-inclusive QPC
spans of the proxy's own device calls, never GPU time. `route_gate`,
`route_draw`, `route_fill` and `route_lazy_flush` exclude each other and may be
added; `route_set_rt` and `route_jitter` nest inside them and may not.

| span | calls | mean | per resolved non-capture frame (88) |
| --- | ---: | ---: | ---: |
| `route_gate` | 2,438,085 | 8.15 µs | 3,719 µs |
| `route_draw` | 1,171,488 | 6.72 µs | 1,483 µs |
| `route_fill` | 5,268 | 70.2 µs | 70 µs |
| **exclusive route total** | | | **5,272 µs** |
| `route_set_rt` (nested) | 4,685,952 | 0.79 µs | 694 µs |
| `route_jitter` (nested) | 2,952,324 | 0.28 µs | 157 µs |
| `taa_run` (nests its five phases) | 5,268 | 362 µs | 351 µs |
| ↳ `taa_resolve_draw` | 5,268 | 322 µs | — |
| `route_readback` | 40 | 12.8 ms | 25.6 ms on capture frames only |

There is one `route_gate` per `draw_backend` call (2,438,085 against 2,438,084),
and the gate costs **2.9× the backend draw call it guards** (8.15 µs against
2.83 µs); over the session `route_gate` totals 19.9 s against `draw_backend`'s
6.9 s. At ~5.3 ms per routed frame the route is **13–15% of a 34–59 ms frame**,
and it is not the resolve: `taa_run` is 0.35 ms. `route_set_rt` alone is 0.69 ms
per frame across 4,685,952 calls, which is the `rt_mode=perdraw` policy paying
four `SetRenderTarget` calls per routed draw. `--motion-rt-mode lazy` exists
precisely for this and has never been run in gameplay. This is a performance
finding for the orchestrator, not a correctness one; it does not change any
verdict above.

## 2. Camera reprojection

43 `camera_state` lines: one activation line and **42 records** (the 300-frame
cadence plus the 20 captured frames).

| | |
| --- | ---: |
| records | 42 |
| `valid=1`, `policy=2` (`camera_path`) | **38** |
| `valid=0`, `policy=1`, `reason=1` (`switch_off`) | 4 — frames 0, 300, 600, 900 |
| `read_failure` / `failure` nonzero, `camera_state_failure` records | 0 |
| `background_valid=1` on every valid record, `background_fov_mismatch` | 38 / **0** |
| `camera_cut=1` | **0** |
| `history_view_valid=1` on every valid record | 38 |

**No frame read the camera while the scene was routed and failed.** The four
invalid records are frames 0–900, all inside the 20 menu frames of §1, and the
`motion_output_frame` view agrees exactly: `camera_valid=0` on precisely the 20
`taa_skip=2` frames and `camera_valid=1` on all 108 routed ones. The
analyzer's `camera_read_valid` check reports `fail` only because it compares
`valid` against the record count without excluding unlatched frames.
`routed_frames_without_camera` is empty.

### 2.1 Field of view

`p00 = 0.8` and `p11 = 1.333333` on all 38 valid records — one projection for
the whole session, menu to sector change.

* horizontal FOV `2·atan(1/0.8)` = **102.680°**
* vertical FOV `2·atan(1/1.333333)` = **73.740°**
* aspect `p11/p00` = 1.666666, i.e. 1280/768

This matches the 73.74° / 102.68° prediction of
[camera-state-and-frame-routine.md](../reverse-engineering/camera-state-and-frame-routine.md)
to every printed digit, from a live gameplay session, and the background pass
reports the same `p00`/`p11` on every frame (`background_fov_mismatch=0`).

### 2.2 `rotation_deg` has a noise floor of 0.31–0.70°

`camera_rotation_degrees` (camera_reprojection.h) is
`acos((tr(Rᵀ_a R_b) − 1)/2)`, which is the rotation angle **only if both
matrices are orthonormal**. The engine's view rows are floats and their norms
are short of one by up to 2.7e-5 (one record, frame 1200, by 2.25e-4), so
`tr(RᵀR) = Σ r²ᵢⱼ < 3` and the *same matrix against itself* returns a nonzero
angle: `acos(1 − δ/2) ≈ √δ`.

Evaluating the function on each record against itself gives a floor of
**0.307° to 1.645°** (median 0.479°). That is the whole explanation for the
suspicious constants in the log: bursts 0, 2, 3 and 4 report
`rotation_deg = 0.3627 / 0.5391 / 0.3073 / 0.4352` identically on all four
frames of their burst while their rotation rows are bit-identical across it, and
in every case the reported value equals that matrix's **self**-rotation to
0.0000–0.0004° (0.3629 / 0.5391 / 0.3069 / 0.4349). Bursts 2, 3 and 4 report the
same value on the preceding 300-frame cadence record too, because the matrix had
not changed since. The camera was not rotating at all in those bursts; the
reported degree is the floor.

Consequences, in order of importance:

1. **The 20° cut threshold is unaffected.** A floor of ≤ 1.7° cannot trip a 20°
   test, and `camera_cut=0` everywhere is correct.
2. **`rotation_deg` is not usable as a rotation measurement below about 1°**, and
   any future logic with a small rotation threshold (a reactive-history or
   sharpening heuristic, say) must not use it as-is. The fix is arithmetic, not
   a new read: normalize the rows (or use
   `acos((tr(RᵀR')−1)/2)` on row-normalized copies) before the angle.
3. The reprojection matrix itself is built from the raw rows by
   `camera_far_plane_reprojection` and is *not* affected by this: the
   normalization error is 3e-5, four orders of magnitude below the 0.5 px scale
   the reprojection works at, and §2.4 confirms the reprojection numerically.

### 2.3 The real rotation, and its screen displacement

The turn is the only real rotation in the session: **2.199° then 6.235°, 6.233°,
6.232° per frame** on frames 1964–1967, an order of magnitude above the floor.
A pure rotation `a` moves a far-plane pixel by `a·p00·W/2` horizontally and
`a·p11·H/2` vertically, which are equal here (`0.8·640 = 1.3333·384 = 512`):

| frame | rotation | predicted px | readback median displacement | error |
| --- | ---: | ---: | ---: | ---: |
| 1964 | 2.199° | 19.65 | 18.80 | 4.3% (the turn was accelerating) |
| 1965 | 6.235° | 55.72 | 55.57 | **0.3%** |
| 1966 | 6.233° | 55.70 | 55.47 | **0.4%** |
| 1967 | 6.232° | 55.69 | 55.64 | **0.1%** |

The camera read, the projection read and the per-pixel motion the shader wrote
agree to a few tenths of a percent on a 56-pixel displacement. This is the
strongest independent cross-check of the camera state in gameplay so far: the
route's own reprojection and the producer's per-draw rows are two different code
paths and they land on the same number.

### 2.4 Cross-check against the captured draw constants

`analyze_camera_state.py` compares, for every captured draw the shader-register
metadata can factor, the inverse of the uploaded `g_mViewInverse` (registers
c34–36) against the rotation and translation the route read from the engine's
view buffer, and the projection recovered as `P = WVP · W⁻¹ · C` against the
read `p00`/`p11`.

| | |
| --- | ---: |
| draws compared | 7,881 |
| agreeing (rotation ≤ 1e-6, projection ≤ 1e-4) | 7,841 (**99.49%**) |
| max projection deviation | 3.6e-4 (burst 3) |
| rotation / translation deviation, bursts 1–4 | ≤ 1.2e-7 / ≤ 8.0e-7 |
| `object_matrix role=view` rows compared / agreeing | 8,313 / 7,989 |

**All 40 disagreeing draws are outside the routed scene.** They are indices
307–316 of each of burst 0's four frames, all vertex shader
`d5e1c75351ed3f04` with no pixel shader, and burst 0's `motion_route` records
stop at index 291: these draws never reached the gate at all, because they are
issued after the scene ended (the post-scene HUD/overlay pass, drawn with an
identity-like view — hence a rotation deviation of 1.97 and a translation
deviation of 1.0 while the recovered *projection* still matches to 2e-7). Every
draw the route actually routed agrees. Verified directly on frame 1722: the
inverse of the uploaded `g_mViewInverse` reproduces the read rotation to
**4e-8** and the read translation (2162.99, 7341.814, 2640.031) to **1e-3
absolute, 5e-7 relative**.

The 324 disagreeing `object_matrix role=view` rows are 14–24 per frame out of
241–693, the same overlay/background population; the check has no per-scope
attribution, so this is an inference from the per-frame counts, not a proof.

The log carried everything the cross-check needs. Both analyzer checks report
`fail` on thresholds that do not exclude non-scene draws or unlatched frames;
neither indicates a defect in the camera read.

## 3. Motion and depth readbacks

`analyze_motion_readback.py --jitter-from-log`, 20 motion RGBA32F and 20 depth
R32F images. Jitter indices are the 8-phase Halton set the route logs and were
taken from the log, not assumed.

| check | result |
| --- | --- |
| `readback_integrity` | **pass** — 20/20 frames clean, no failure, unsupported, reset or shutdown record |
| `counter_consistency` | **pass** — 20/20 frames |
| `history_pairing` | **pass** — 4,583 predictable draws, 0 disagreements with the DLL's matched flag |
| `row_consistency` (the row-pair test) | **pass** — 15 frames, 547,267 sampled pixels, **0 unexplained**, max error **0.068 px** (tolerance 0.5) |
| `temporal_coverage` | **pass** — 15 pairs, worst covered fraction 0.9807, 13 pairs meeting the ≥ 0.99-matched criterion |
| `depth_image_integrity` | **pass** — 0 nonfinite, 0 out of range, `valid_motion_without_depth=0` on all 20 frames; written fraction 0.069–0.122, depth range 0.611–0.99997 |
| `static_consistency` | **unavailable** — no burst reuses bit-identical rows across frames (5 `unknown`, 15 `moving`), the same as iteration 7 |
| `displacement` | **fail** at the 64 px bound — 89,906 of 1,895,515 valid pixels (4.74%), max 151 px |
| `depth` (previous-depth cross-check) | **fail** at 1e-4 / 0.99 — worst within-fraction 0.287 nearest, **0.955 bilinear** |

Two verdicts need reading, and neither is a production defect.

**`displacement` is the bound, not the route.** Every suspicious pixel is in
burst 1: frames 1965–1967 have medians of 55.5–55.6 px and maxima of 127–151 px,
which §2.3 shows is *exactly* the 6.23°/frame turn. Bursts 0, 2, 3 and 4 report
`suspicious=0`. The 64 px bound was calibrated before any capture contained a
hard turn; a rotation of 6.2°/frame legitimately moves a 1280-wide frame by 56 px
at the centre and more at the edges. The route's own cut detector already treats
this as a cut.

**`depth` is a sampling artefact, reproducing iteration 7's finding.** With
nearest sampling the within-1e-4 fraction is 0.287–0.623; with
`--depth-sampling bilinear` it is **0.955–0.977** (per pair: 0.971, 0.977, 0.967,
0.971, 0.974, 0.967, 0.970, 0.977, 0.967, 0.961, 0.961, 0.970, 0.957, 0.955,
0.955) and the previous-sentinel count drops from 15,745 to 1,530. The
correlation with motion is exact: the stationary burst 0 has a *median* error of
4e-6–1e-5 while the flight bursts have 1e-4–2.8e-4, because nearest sampling of
the previous depth image at a fractional previous UV commits an error of up to
half a pixel of depth gradient, and the gradient is what forward motion exposes.
Iteration 7 measured 0.25–0.60 nearest / 0.945–0.972 bilinear; this run is
0.287–0.623 / 0.955–0.977 with a max error of **0.094 against iteration 7's
0.208**. The residual 4–5% sits at depth discontinuities where a bilinear tap
crosses a silhouette — which is what the resolve's per-tap point-sampled depth
test exists to reject. The 1e-4 / 0.99 criterion still needs restating for real
perspective depth at 1280×768, as
[iteration 7](iteration-07.md#anomalies) recorded.

Nothing else is anomalous. Notably the row-pair consistency test passes with
**zero** unexplained pixels and a 0.068 px maximum through a 6.2°/frame turn,
which means every valid pixel's previous UV and previous clip Z/W is explained
by `M_current · M_previous⁻¹` of some matched draw — the producer is correct
under rotation, not only under translation.

## 4. Blur: what can and cannot be said

**It cannot be measured from this run.** The measurement is a comparison of the
resolved image with a reference image, and run 1 wrote neither: without
`--taa-debug` there is no `color_<device>_<frame>.bgra8` (the pre-resolve 8-bit
target) and no `taa_<device>_<frame>.rgba16f` (the FP16 resolve output). The
analyzer says so itself — `taa_image: unavailable, no motion_output_taa_readback
line` — and the iteration-7/8 blur metrics (gradient-energy ratio,
high-frequency fraction) and the flicker classification are all unavailable for
the same reason. Any statement of the form "the blur is X%" from this run would
be fabricated. §5 is the run-2 plan that measures it.

What *can* be stated is what the resolve does that low-passes, and how much each
mechanism can cost, from the GPU fixture and from a model checked against it.

### 4.1 The four low-pass mechanisms

From `src/temporal/resolve.hlsl` and
[temporal-resolve.md](temporal-resolve.md):

1. **History weight 0.9 with a Catmull-Rom resample of the reprojected
   history.** Every frame the history is read at the previous position plus the
   current jitter. When that lands on a texel centre (a stationary camera under
   the fixed convention) the four-tap kernel degenerates to a single texel and
   loses nothing. When it lands at a fractional offset — which is what any
   camera or object motion produces — the kernel low-passes, and because the
   result is fed back at weight 0.9 the loss compounds geometrically. This is
   the dominant term under motion and is quantified in §4.2.
2. **The 3×3 variance clip.** The history is clipped to `mean ± 1.25σ` of the
   current 3×3 neighbourhood before blending. On a stationary surface this is
   what keeps the accumulation from drifting; on high-contrast detail it removes
   the part of the history that lies outside the current neighbourhood, which is
   a spatial low-pass with a 3×3 support. The fixture measures its bias
   indirectly: ring coverage against jitter-sampled coverage 0.033 (bound 0.05,
   "variance-clip bias").
3. **The closest-depth 3×3 dilation.** A pixel adopts the correspondence of the
   nearest of its nine neighbours. That is what makes silhouettes track, and it
   also means a pixel next to a nearer surface reads the history at that
   surface's velocity — a 1-pixel spatial smear at every depth edge. The
   fixture's corner measurement is why the shader uses the 3×3 and not a cross
   (a corner lost half its coverage with the cross: 0.257 against 0.500).
4. **The hard 1e-4 depth rejection.** Not a low-pass — the opposite: where it
   fires the pixel is the raw jittered sample with no filtering at all. Iteration
   8 measured that population at 6% of the screen carrying 78–89% of the
   residual flicker, and 75–80% of the screen is sentinel and untouched. It
   matters here because it means the blur is *not uniform*: the same frame
   carries over-filtered interiors and unfiltered edges, which is a plausible
   reading of "slightly blurry" on objects whose interiors the route covers.

### 4.2 How much the history resample can cost

The resolve's history lookup is a four-tap Catmull-Rom at fractional offset `t`.
Its transfer magnitude at spatial period `p` is
`|Σ w_k(t) e^{−i 2π (k−1)/p}|`, and feeding the output back at weight `w`
settles at `A = (1−w) / (1 − w·r)`. `analyze_iteration09.py` computes both and
`test_iteration09.py` pins the closed forms (weights sum to one, the Nyquist
null at a half-texel offset, the geometric series).

Anchoring against the GPU fixture's own measurement
([temporal-resolve.md](temporal-resolve.md), "Resampling blur": a period-8
sinusoid scrolling 0.25 px/frame so the history is resampled at a constant
0.75-texel offset, `w = 0.9`):

| | fixture | model |
| --- | ---: | ---: |
| Catmull-Rom amplitude ratio, period 8 | **0.930** | 0.959 |
| bilinear amplitude ratio, period 8 | **0.712** | 0.663 |
| stationary ramp, gradient energy | **0.9947** | 1.0 (zero offset is lossless) |

The model tracks the measurement to 0.03–0.05 in both directions, so it is fair
to extrapolate it across the frequency range the fixture did not test:

| spatial period | tap ratio (t = 0.75) | steady state | tap ratio (t = 0.5) | steady state |
| ---: | ---: | ---: | ---: | ---: |
| 2 px | 0.688 | **0.262** | 0.000 | **0.100** |
| 3 px | 0.848 | 0.421 | 0.688 | 0.262 |
| 4 px | 0.939 | **0.645** | 0.884 | 0.489 |
| 6 px | 0.986 | 0.887 | 0.974 | 0.812 |
| 8 px | 0.995 | 0.959 | 0.992 | 0.929 |
| 16 px | 1.000 | 0.997 | 0.999 | 0.995 |
| 32 px | 1.000 | 1.000 | 1.000 | 1.000 |

So: **under sustained fractional motion the resolve costs essentially nothing
above 16 px and 35–90% of the amplitude at 2–4 px**, with the exact figure set
by the fractional part of the per-pixel velocity each frame. A stationary camera
costs nothing at all (0.9947 of the gradient energy in the fixture). That shape
is consistent with the user's report *from run 1's own motion data*: bursts 2–4
are straight flight with per-pixel velocities of 1–5 px whose fractional parts
vary every frame, which is exactly the regime that resamples the history at a
new offset every frame; and the hard turn is not in the regime at all, because
the cut detector drops the history there (§1.2), so turns should look aliased
rather than soft.

Combining with the only in-game blur number that exists — iteration 8's
stationary gradient-energy ratio of **0.64–0.75** against the raw jittered
render, i.e. an amplitude ratio of 0.80–0.87, on bursts where the resampling
term was ~1 — gives a working expectation for run 2 in flight: the fine-detail
amplitude should read roughly `0.80–0.87 × 0.26–0.65` depending on the band,
i.e. a gradient-energy ratio well below iteration 8's 0.64–0.75. If run 2
measures a stationary ratio near 0.64–0.75 and a moving ratio far below it, the
resample is the cause and a sharpen is the remedy; if the stationary ratio is
also low, the variance clip and the dilation are implicated and the remedy is
different. That discrimination is the point of run 2.

### 4.3 The sampler baseline, and the one field the capture does not record

The sampler state the game binds, from the capture snapshots of the 20 captured
frames, attributed per draw and split by the route's verdict
(`analyze_iteration09.py`, `samplers`). 8,473 draw snapshots:

| class | draws |
| --- | ---: |
| routed and matched | 6,118 |
| routed, unmatched (gate 6) | 56 |
| rejected at gate 4 (draw state) | 1,475 |
| rejected at gate 3 (pair) | 380 |
| no `motion_route` record (outside the scene) | 444 |

**Every one of the 6,118 routed matched draws has the identical stage-0
signature**: `D3DSAMP_MINFILTER = ANISOTROPIC`, `MAGFILTER = LINEAR`,
`MIPFILTER = LINEAR`, `MAXANISOTROPY = 16`, `SRGBTEXTURE = 0`. The same holds
for the 56 gate-6 draws and the 1,475 gate-4 draws. Per stage, across all 6,118:

| stage | MINFILTER | MIPFILTER | MAXANISOTROPY |
| ---: | --- | --- | ---: |
| 0, 1, 5, 6 | ANISOTROPIC (6,118) | LINEAR (6,118) | 16 |
| 2 | ANISOTROPIC 5,986 / LINEAR 132 | LINEAR 5,986 / NONE 132 | 16 |
| 3 | LINEAR 4,096 / ANISOTROPIC 2,022 | NONE 4,096 / LINEAR 2,022 | 16 |
| 4 | LINEAR 4,364 / ANISOTROPIC 1,754 | NONE 4,364 / LINEAR 1,754 | 16 |
| 7–15 | POINT | NONE | 1 (the D3D9 default: unused) |

`SRGBTEXTURE = 0` on every stage of every routed draw — the game samples its
textures without hardware sRGB decode, which matters for the HDR path but not
for sharpness. Unrouted classes differ: the 444 out-of-scene draws are mostly
`POINT/POINT/NONE` (224, the HUD) or `LINEAR/LINEAR/POINT` at anisotropy 4 (80).

**`D3DSAMP_MIPMAPLODBIAS` is not in the capture.** `capture.cpp` enumerates
exactly five sampler states — `MINFILTER`, `MAGFILTER`, `MIPFILTER`,
`MAXANISOTROPY`, `SRGBTEXTURE` (135,568 records each, consistent across the
session) — and `MIPMAPLODBIAS` (state 8) and `MAXMIPLEVEL` (state 9) are not
among them. So **the game's baseline LOD bias is unknown from this log**: it
could be 0 or it could already be negative. The honest statement for the bias
decision is:

* the minification path is already as sharp as the hardware offers on every
  routed material draw (16× anisotropic with trilinear mips), so a negative bias
  would not be compensating a blurry sampler configuration — it would be adding
  detail above the mip chain's own Nyquist for the temporal filter to resolve;
* a typical −0.5 for native-resolution TAA is therefore a *sharpening* lever,
  not a correction, and its cost is aliasing wherever the resolve does not
  filter: the 6% rejected-edge population and the 75–80% sentinel population of
  iteration 8 would get the extra high-frequency energy with no temporal
  filtering at all;
* a post-resolve sharpen acts on the resolved image uniformly and does not need
  per-draw state changes, per-draw restore, or a decision about which stages to
  bias — six textured stages are live per routed draw;
* either way the baseline must be read first: **add `D3DSAMP_MIPMAPLODBIAS` and
  `D3DSAMP_MAXMIPLEVEL` (and, cheaply, `ADDRESSU`/`ADDRESSV`) to the capture's
  sampler enumeration before run 2.** That is a one-line change in
  `src/proxy/capture.cpp`, outside this report's scope.

## 5. What run 2 needs

To measure the blur rather than model it:

1. **Flags.** `--direct --ownership --object-trace --object-lifetime
   --motion-output --taa --taa-debug --telemetry`. `--taa-debug` is the whole
   point: it writes the pre-resolve 8-bit colour and the resolved FP16 image and
   re-enables every image metric of iterations 7 and 8. `--profile` can be
   dropped (the loading profile is answered) to cut log size; the boundary
   bracket will grow back to ~22 ms with the two extra readbacks, which is
   expected and is why §1.3's 567 µs figure should be recorded now.
2. **A TAA-off control run of the same save and scene**, `--motion-output
   --telemetry` with no `--taa`, no `--taa-debug`, no jitter. Iteration 8
   compared the resolved image with the *same frame's jittered* pre-resolve
   colour, which mixes the jitter's own blur into the ratio; an unjittered
   control is the reference a real MTF number needs, and it is also the
   controlled timing comparison §1.3 cannot make from one session.
3. **Bursts.** Five bursts of four frames again, chosen to cover the regimes the
   model separates: **one fully stationary** (as burst 0 — hold still for ≥ 2 s
   so the whole burst is stationary), **two in steady straight flight** at
   normal cruise (the 1–5 px/frame regime the user calls blurry), **one slow
   pan** at ~1°/frame (a fractional offset every frame but under the 48 px cut
   bound, the worst case for the resample), and **one hard turn** (to confirm the
   cut path). Keeping the ship in one state for a couple of seconds around each
   burst is what makes the burst interpretable.
4. **Sampler capture.** Add `D3DSAMP_MIPMAPLODBIAS` and `D3DSAMP_MAXMIPLEVEL`
   to `capture.cpp`'s enumeration first, so run 2 answers the bias baseline
   instead of leaving it open again.
5. **Optional second control: `--motion-rt-mode lazy`**, given §1.4. It is a
   pure A/B on the 0.69 ms/frame of `SetRenderTarget` and needs no new code.

## Anomalies

1. **The trembling is gone by the user's own report** and the health numbers are
   consistent with it: 108/108 scene frames resolved, 105 with history, the
   three history-free frames are the cut detector doing its job on a 6.2°/frame
   turn, zero failures, zero resyncs.
2. **The blur is unmeasured and cannot be measured from run 1.** No colour and
   no resolved-image readback exists. §4.2 bounds what the history resample can
   cost (26–65% of the amplitude at 2–4 px under sustained fractional motion,
   nothing above 16 px, nothing at all when stationary); §5 measures it.
3. **`camera_rotation_degrees` has a 0.31–0.70° noise floor** caused by the
   engine's non-orthonormal float view rows. Harmless for the 20° cut, wrong for
   any finer use, and the log's constant `rotation_deg` values across stationary
   bursts are entirely this artefact.
4. **`selector_state` cannot answer the environment-map question** as logged: it
   is the terminal state and reads 9 (`Rejected`) on all 128 frames, routed
   frames included. The env-map conjunction matched only the 20 menu frames, so
   there is no evidence of env-map rendering in flight, and no evidence against
   it either.
5. **The route costs ~5.3 ms of CPU per routed frame** (`route_gate` 3.7 ms,
   `route_draw` 1.5 ms), 13–15% of the frame, with the gate alone at 2.9× the
   backend draw call it guards. Measured for the first time; `taa_run` is
   0.35 ms, so this is the route, not the resolve.
6. **The sector change was not captured.** It is a pure translation teleport
   between bursts 3 and 4, `camera_cut` is a rotation test that cannot see it,
   and no logged frame covers the transition.
7. **The `displacement` and `depth` readback checks fail on thresholds, not
   production.** 64 px cannot survive a 6.2°/frame turn; the previous-depth
   check reads 0.955–0.977 with bilinear sampling against 0.287–0.623 with
   nearest, reproducing iteration 7 with a max error 2.2× smaller.
8. **The session did not shut down** (fourth run in a row): no
   `motion_output_release`, no `device_destroy`, no Reset. The gameplay
   witnesses for teardown and for `Reset` with the temporal pass allocated
   remain unobtained.
9. **`object_lifetime` still runs `active_without_baseline`** and gate 5 still
   never fires (0 of 25,669 routed draws).
