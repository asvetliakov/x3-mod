# Hull emissive widening: verification ledger

Feature: `--hull-emissive-widening K,Q0,Q1` (`X3M_HULL_EMISSIVE_WIDENING`), design
[hull-emissive-widening.md](../architecture/hull-emissive-widening.md), problem
[thin-glow-lines.md](../architecture/thin-glow-lines.md). Default off; nothing installed.

## 2026-09-22: built and qualified on the fixture (WIP, worktree branch)

Source: transformer `linear_material.cpp` (`lightmap_widen_fetch`, `lightmap_widen_site`, opcodes 91/92/93 in
`body_shape`/`structure`), route `motion_output.cpp` (`configure_hull_emissive_widening`, c217.z in `evaluate_draw`,
selection in `bind_variant_pair`), law `fade_route_core.h::lightmap_widen_scale`, option `capture.cpp`, launcher
`tools/manage.py`. Bottle X3 (CrossOver Preview, arm64 Wine/FEX), fixture DLL `build/d3d9.dll` of this worktree
(sha256 `c471cc12…`, parent commit 6019d936 + the WIP diff).

### Host

- `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_hull_emissive_widening
  verification.analysis.test_hull_lightmap_gain`: 15 tests OK. The Python reference rebuilds all 600 widened variants
  (100 programs x 2 fills x 2 depth modes + 2 share) byte for byte from the gained ones; rG = highest temporary + 1
  (max r24, share variants); +12 DWORDs, +3 instructions, +7 weighted slots everywhere; the 8 programs without the
  term (glass, asteroid) unchanged; `widen` with G = 1 refused (InvalidConfig).
- Coverage ([hull-emissive-widening-coverage.json](../../verification/results/hull-emissive-widening-coverage.json)):
  101 ps_3_0 programs of the local archive declare `LightMapTexSampler`; 100 widened (44 at s2, 56 at s3), the moon
  program `6aaaa2cb27e92cc8` excluded by design. Per family (programs / max widened plain / share slots): argon 12 /
  138 / 215, boron 8 / 149 / 223, paranid 12 / 137 / 206, split 12 / 138 / 215, khaak+teladi+xenon 12 / 139 / 216,
  terran 12 / 137 / 213, standard_lighting 18 / 139 / 216, xt_standard_lighting(+damage) 8 / 192 / 271,
  xt_terraformer 6 / 172 / 253. Largest widened variant 271 of 512 slots (was 264).
- Slot growth by the driver's own SM3 table; confirmed on the device by D3DX below (+7 on every drawn program).
- `python3 verification/probe/check_no_x87.py build/d3d9.dll`: 620 reachable functions, no violations (the
  footprint law is SSE scalar).
- `/usr/bin/python3 verification/probe/run_host_suite.py`: see the report line of the run that produced this ledger.

### Fixture (`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py ... seam-lightmap-widen-{off,k2,k3,k4,programs}`)

Record: [hull-emissive-widening-seam.json](../../verification/results/bottle-X3/hull-emissive-widening-seam.json)
(untracked capture logs beside it). 256 x 256 target, gain 4, Q = 0.5, 2 units/px, a 0.5-px strip light map
(256 texels over a 128-px quad, full box mip chain) drifting 6.125 px/frame over 8 phases at w = 4 / 128 / 512
(footprint 0.04 / 1.25 / 5 units/px: k = 1 / 1 + (K-1)/2 / K). 180 checks per K case, 99 for the programs case.

| K | uploaded k (below / inside / above) | widened draws | per-phase peak ratio min/max (h = v) | design floor 1 - 0.5/T | strip width at half peak (px) | strip energy vs off | panel interior vs off |
|---|---|---|---|---|---|---|---|
| off | 0 / 0 / 0 | 0 | 0.500 | 0.5 (T = 1: level 1 at 2 texels/px) | 1.375 | 1 | 1 |
| 2 | 1 / 1.5 / 2 | inside, above | 0.750 | 0.75 | 2.125 | 1.00003 | 1.00002 |
| 3 | 1 / 2 / 3 | inside, above | 0.867 | 0.833 | 3.125 | 0.9966 | 1.00003 |
| 4 | 1 / 2.5 / 4 | inside, above | 0.875 | 0.875 | 3.875 | 0.9964 | 1.00007 |

- Below Q0 (k = 1) every frame hashes equal to the option-off run's (the un-widened gained variant binds; no
  `texldd(k=1) == texld` assumption in the route). F4 off drops gain and widening together (0 / 0 draws); after
  Reset the far frame binds the widened variant again with k = K.
- Wide panel: interior mean within 0.007 % of the un-widened one at every K (no rescale, no dimming).
- Oblique quad (perspective rows p = 0.6), light-map stage LINEAR against ANISOTROPIC 16: the image hash changes
  with the filter for the un-widened `texld` (w = 4) **and** for the widened `texldd` (w = 512, K = 2/3/4), so the
  backend applies anisotropic filtering to explicit-gradient fetches (peaks k3: 1.137 linear / 1.151 aniso16).
- Programs case (11 pairs, one per drivable family group: standard/argon/shared/split/terran DEFAULT, argon/standard
  BUMP, boron, paranid, XT default, XT terraformer): `CreatePixelShader` accepts every widened program (`dsx`/`dsy`
  sourcing `v1` directly); D3DX (`C:\X3\d3dx9_37.dll`) slot counts gained -> widened: 79->86, 78->85, 79->86,
  78->85, 77->84, 90->97, 91->98, 89->96, 77->84, 129->136, 109->116 (+7 each, matching the table). The widened
  variant forced at k = 1 is **bit-identical** to the gained one on every pair (0 FP16 codes differ over the whole
  target). At k = 3 the strip peak is 0.634 of the un-widened one on every pair with energy within 0.4 %.
- Route: per-draw work is one multiply-add and clamp on the footprint the far fade already computes, uploaded in
  the motion ABI's existing two-vector write (`c217.z`); the level-count gate reads the sampler shadow (the
  `SetTexture` hook now installs for the option as it does for the mip bias). Startup lines
  `hull_emissive_widening_mode` / `_configured`, per frame `hull_lightmap_widen_frame` (k range), at detach
  `hull_lightmap_widen_summary` (session k range and widened draw count).

Not measured here: the post-resolve (TAA) peak change (the fixture runs TAA off; the design's mechanism (ii)),
DXT1 light maps (the fixture's maps are A8R8G8B8), flight cost. Native Windows: cross-compiled only
([platform-portability.md](../architecture/platform-portability.md)).

## 2026-09-22: Run 231 triage (flight, K=3,Q0=2,Q1=8; TAA on)

Inputs: `/tmp/x3-bottleX3-run231` (Run64 DLL e839dc7c, `--hull-emissive-widening 3,2,8`, `X3M_CAPTURE_FRAMES=32`,
`X3M_TAA_DEBUG=1`). Two F8 bursts identified from `color_1`/`taa_1` filenames: still = frames 6928-6959, pan (yaw) =
frames 9165-9196 (camera_rotation_deg ~3.1 deg/frame throughout, well under the 20 deg camera-cut gate — no cut fires).
Scripts: session scratchpad (untracked).

**A. Engagement.** Startup: `hull_emissive_widening_mode requested=1 enabled=1 valid=1 k=3 q0=2 q1=8 gain=4`,
`hull_emissive_widening_configured accepted=1 k=3 q0=2 q1=8` — no refusal/fallback. Per-frame `hull_lightmap_widen_frame`:
still burst is byte-for-byte constant every one of 32 frames (`admitted=328 widened=201 unity=127 k_min=2.10713
k_max=3`) — the per-draw ramp is stable at rest, as designed (c217.z uploaded per draw from a per-draw footprint, not
per pixel/per phase). Pan burst varies every frame: `widened` 182-254, `unity` 121-178, `k_min` swinging 1.72-3.0
(e.g. 9175 k_min=1.720, 9186 k_min=2.497, 9193 k_min=2.170) as different hull faces/parts enter/leave the Q0-Q1 band
under yaw — this directly produces "grow/shrink" of widened strips during the pan; nothing here is a bug, it is the
per-draw footprint law responding to a fast-changing view angle.

**B/C/D. Still vs pan, flicker at rest, jitter interaction.** ROI y[190:330] x[790:890] (densest bright cluster,
view_z 3000-9000, matching thin-glow-lines' station range) in the still burst: `color_1` (pre-TAA) is **bit-exact
every 8 frames** (max abs diff vs frame+8 = 0.0), matching `jitter_samples=8`; `jitter_x/jitter_y` alternate sign
frame to frame (e.g. f6928 jx=0.125, f6929 jx=-0.125, jitter_index cycles 0-7). Bright-pixel frame-to-frame |delta|
pre-TAA mean 0.096 (up to strip peaks ~1-3 in linear taa units), i.e. the strip itself changes substantially every
frame at rest, purely as a periodic function of jitter phase — the texldd gradient input (dsx/dsy of `v1`) is
computed from the jittered projected position, so the k=2.1-3 footprint samples a different sub-texel position each
of the 8 phases. `taa_1` (post-resolve) reduces the swing (bright-pixel frame delta mean 0.019, ~5x smaller;
per-pixel variance mean 0.00025 vs pre-TAA 0.0086 in the bright set) but is **not fully periodic** (max diff vs
frame+8 = 0.00195, not 0), consistent with history convergence damping but not erasing the jitter-driven wobble.
This is measured evidence for the mechanism the brief named: TAA jitter precedes the hull shader, so k>1 amplifies a
sub-pixel wobble that was already present at k=1, and the resolve only partly hides it — "flicker while standing
still" is real and pre-TAA, not a resolve-only artifact.

**E. Options confirmed active** from log: `X3M_TAA=1 X3M_TAA_DEBUG=1 X3M_TAA_THIN_REGION=0.97,1
X3M_TAA_THIN_REGION_GATE=camera X3M_TAA_SENTINEL_STABILISER=0.7 X3M_TAA_FAR_STABILISER=0.985,0,80,130,0.03,0.25
X3M_TAA_MIP_BIAS=-0.5`. Not distinguished by this capture: whether the thin-region camera gate opens differently on
pan vs still frames (no per-pixel gate-state log field found; would need a diagnostic dump of the thin-region mask).

**Not settled.** Whether the "almost disappeared when facing lines straight on" complaint is the k_draw ramp dropping
toward 1 for near-frontal draws (footprint falls below Q0) or a separate frontal-view effect; this run's still burst
was not confirmed frontal-vs-oblique in the ROI. A next capture should log, per tracked emitter, `k_draw`, view angle
to the surface normal and pre-/post-TAA peak in the same frame, across a slow pure-yaw sweep through frontal.

## 2026-09-22: follow-up 1 — pre-TAA flicker amplitude, with vs without widening at true rest

Additional inputs: `/tmp/x3-bottleX3-run232` (fog session, **no widening** — `X3M_HULL_EMISSIVE_WIDENING` not set in
this run's launcher line; check: `grep -a hull_emissive_widening` on its log returns nothing configured/enabled — the
option is off), `X3M_CAPTURE_FRAMES=8`, one F8 burst frames 5498-5505, **camera_rotation_deg=0.0000 on all 8
frames** (true rest, one full jitter cycle, jitter_index 1..7,0). `/tmp/x3-bottleX3-run225` (no widening, frames
6516-6523) and `/tmp/x3-bottleX3-run227` (mip bias +0.5, two bursts 3943-3950 / 5383-5390) are confirmed **not**
still: `camera_rotation_deg` rises 0.0 -> 0.62-1.14 deg/frame (run225) and 0.0 -> 0.54-0.66 / 0.0-0.25 deg/frame
(run227), matching their own docs (pitching). Only run232 and run231 are true-rest bursts; run225/227 are reported
for context only, not as a still baseline.

Readback availability: run232 and run225 carry only `hdr_1` + `depth_1` + `motion_1` (no `color_1`/`taa_1`/`present_1`);
run227 and run231 carry the full set. **Consequence: the post-TAA (displayed) comparison the question asks for cannot
be made for the no-widening still baseline (run232) — there is no `taa_1`/`present_1` dump in that run.** Only the
pre-TAA (`hdr_1`) comparison is possible against run231.

Method: same ROI-finding rule as the first triage (`hdr_1` luma > 0.8, view_z in 3000-9000, densest 40px-cell bright
cluster on frame 1 of each burst), all four runs draw the same station's gained hull program `ps=5f82ecacd39529cd`
with `model=00005428` present in every run's `motion_route` lines, confirming the shader/model is comparable across
runs (exact per-run camera framing differs, so the ROI box differs in screen position/size between runs; this is a
comparability caveat, not a control). Metric: pre-TAA (`hdr_1`) bright-pixel (>0.5) frame-to-frame |delta| over the
burst, and (run231 only, 32 frames) the 8-frame periodicity check.

| run | widening | rest? | ROI bright px | delta_mean | delta_mean / peak_mean | delta_p90 |
|---|---|---|---:|---:|---:|---:|
| run231 (frames 6928-59) | K=3,Q0=2,Q1=8 | yes (rotation=0) | 738 | 0.357 | 0.229 | 0.750 |
| run232 (frames 5498-05) | off | yes (rotation=0) | 197 | 0.644 | 0.392 | 1.547 |
| run225 (frames 6516-23) | off | no (pitching 0-1.1 deg/frame) | 2183 | 0.137 | 0.302 | 0.287 |
| run227-A (frames 3943-50) | bias +0.5, widen off | no (pitching 0-0.66) | 290 | 0.317 | 0.637 | 1.042 |
| run227-B (frames 5383-90) | bias +0.5, widen off | no (pitching 0-0.25) | 506 | 0.220 | 0.548 | 0.736 |

**Finding, revising the first triage's inference.** The measured pre-TAA jitter-driven frame-to-frame delta at true
rest is **larger without widening (run232, 0.644 mean, 0.392 of peak) than with widening (run231, 0.357 mean, 0.229 of
peak)**, both in absolute and peak-normalised terms. This is the opposite direction from the first triage's inferred
claim that "k amplifies the wobble," and is consistent with the architecture note's own design metric (§1.4: per-phase
peak floor rises from about 0.5 at bias -0.5/k=1 to 0.83 at k=3 — widening should *reduce* the phase-to-phase swing of
a sub-pixel strip, not increase it). **The amplitude claim in the first triage's report is not supported by this
comparison and should be treated as superseded.**

What remains unresolved: whether the *displayed* (post-TAA) flicker is nonetheless worse with widening cannot be
answered from these captures — run232 has no `taa_1`/`present_1`. A plausible alternative explanation, not measured
here: widening closes the tearing gaps (design intent), so a strip that was previously invisible on most phases (only
"there" 1 dash in some frames, per thin-glow-lines run225 numbers) becomes continuously visible at reduced but
non-zero peak on every phase; the same absolute pre-TAA wobble, now riding on a signal that is *always present*
instead of intermittently present, could read to the user as continuous "flicker" rather than intermittent "tearing" —
a perceptual reclassification, not a larger physical delta. This is **not measured**, only consistent with the
numbers above; it is an [I] hypothesis, not a finding.

**What would settle it.** One flight, same station, same pose, both with `--taa-debug`: (a) `--hull-emissive-widening
3,2,8` at rest (have: run231), (b) `--no-hull-emissive-widening` (or omit the option) at the same rest pose with
`--taa-debug` so `taa_1`/`present_1` exist. Compare post-TAA bright-pixel frame-to-frame delta and periodicity
directly; that number, not the pre-TAA one, is what the user sees.

## 2026-09-22: follow-up 2 — what the per-draw footprint (hence k) is a function of under yaw

Source: `src/proxy/fade_route_core.h::lightmap_widen_scale` (comment lines 100-107, code 108-115) — the widen scale
uses the **same footprint as the far fade**, `fade_route::lightmap_far_gain`'s `w` argument (lines 83-99 comment,
92-98 code): `footprint = 2 * w / (m00 * width)`, where **`w` is the object origin's view-space z (clip-space `w`,
`rows[15]` of the object's world-to-clip row-vector transform), not the Euclidean distance to the camera** (that is a
separate function, `origin_distance`, used only by the unrelated fade-band arm, and it is *not* what feeds the
widening or the far fade). `m00` is the camera's horizontal projection scale (a constant per camera, from the active
projection matrix) and `width` is the render width (1280). `motion_output.cpp`'s `evaluate_draw` calls this with the
draw's own `rows[15]` and writes the result into `pixel[6]` == `c217.z` (`hull_emissive_widening.md` §2.3).

**So the footprint is a function of view-space depth along the camera's own forward axis, at the object's origin —
not of screen position, not of the projected size of a bounding sphere, and not of true 3D camera-to-object
distance.** Under pure yaw with the camera translating little or not at all, true distance `d` to a fixed point on
the station is roughly constant, but the view-space z is `d * cos(theta)` where `theta` is the angle between the
camera's forward vector and the direction to that point. As the camera yaws, `theta` shrinks toward 0 when the
station comes to the centre of the frame (frontal) and grows as it moves toward the screen edge. **A frontal, dead-
centre station therefore has the largest `w` (`cos(theta) ~= 1`) and hence the largest footprint and the largest
`k_draw`** (closest to full `K`); an off-axis station under the same yaw has a smaller `w`, smaller footprint, and
`k_draw` ramping back down toward 1 (possibly to exactly 1, unwidened, below `Q0`).

**Per-draw log evidence: not available in this capture.** `hull_lightmap_widen_frame` logs only the per-frame
aggregate (`k_min`/`k_max` over all admitted draws that frame); `motion_route`/`hull_lightmap_variant` lines carry no
per-draw footprint or `k_draw` field, and no line logs the draw's `rows[15]` for model `00005428`'s draws (unlike the
unrelated `sun_shadow_apply_params` cascade rows). So no single tracked draw's footprint/k series across the pan
burst frames can be extracted from this log; only the frame-level aggregate is available (already reported in the
first triage: `k_min` 1.72-3.0 swinging with `camera_rotation_deg` ~3.1 deg/frame over frames 9165-9196).

**Verdict on the mechanism, and its bearing on the two complaints.** The direction is consistent with both reported
symptoms: facing the lines straight on maximises `w` and therefore `k_draw`, which (§1.3's energy law, `peak = I *
min(1, w_strip/k)`) most strongly dims a sub-pixel strip's peak at exactly the pose the user calls "almost
disappeared"; panning away lowers `w` and `k_draw`, restoring more of the unwidened peak, matching "appear/grow." This
is an **inference from the law and the frame-aggregate numbers**, not a per-draw measurement — confirming it needs a
new diagnostic.

**What one new capture would need to record**, per draw for a chosen tracked node (e.g. `node_handle` of a station
5428 hull draw), each frame of a slow pure-yaw sweep through frontal: `rows[15]` (or the already-computed footprint),
`k_draw`, and the angle between camera forward and the object origin (or `camera_rotation_deg` plus a fixed
reference), alongside the existing pre-/post-TAA peak of one tracked strip pixel in that draw. That would let the
footprint-vs-angle relationship and the strip-peak-vs-k relationship both be read directly instead of inferred from
the per-frame aggregate.

## 2026-09-22: follow-up 3 — is displayed rest flicker amplified after the resolve?

Same ROI (y180-260,x780-860), still burst 6928-6959. Bright-pixel (>0.5) frame-to-frame |delta| normalised by ROI peak:
`taa_1` (resolved, linear) 713 px, peak 3.151, delta_mean 0.0192, **norm 0.00609**; `present_1` (displayed) 762 px,
peak 0.999 (clamped), delta_mean 0.00489, **norm 0.00489**. **Ratio present/taa = 0.80: below 1, not amplified.**
Lag-8 autocorrelation of the per-frame ROI mean luma: `taa_1` 0.99999970, `present_1` 0.99999894 — both essentially
perfectly jitter-periodic (matches the 8-sample jitter cycle exactly), confirming the displayed flicker is the same
periodic signal as the resolve's, only compressed further, not injected or amplified downstream.
`X3M_TAA_SHARPEN=0.75`, `X3M_HDR_TONEMAP=agx`, `X3M_HDR_BLOOM=1` are all active, but the numbers show their net effect
here is to *damp* the resolved wobble (likely display-range clamping near the AgX/white-target ceiling on this bright
strip), not to add gain. **Answer: the displayed rest flicker is the TAA residual, further reduced (not amplified) by
sharpen/bloom/tonemap; no post-resolve stage is the amplifier.**
