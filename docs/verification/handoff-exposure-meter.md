# Handoff: space-aware exposure meter (paused 2026-09-13, in progress)

Branch `worktree-agent-aea55d854948bd6a0` (worktree of `main` at `a40909a`).
Nothing here is committed to `main`; the branch compiles (CMake DLL and the
seam/fixture build) and the analysis suite passes. Continue from
"What remains" below.

## Why

Run 15 (bottle X3, 1280×768, log `/tmp/x3-bottleX3-run15/session-20260912-235347-212.log`):
132 metered frames, `luma_mean` median 0.0017 (min 0.00014, max 0.0103; the
1.0 on frame 0 is the unmetered initial state — the menu is never
redirected, so it was never metered), `ev_target` median +6.8, `ev` +6.85
with the +8 clamp reached: the whole-frame log mean sat at the meter floor
and the lit station was blown out. `meter_us` 8.4 ms was frame 0 only (first
use of the programs); the steady median was 167 µs.

## Design as built (all in the worktree)

Full text: [hdr-scene-path.md](../architecture/hdr-scene-path.md), stage 2,
"The space-aware meter". In short:

- **GPU chain** (`src/temporal/hdr_meter_level0_ps.hlsl`, `hdr_meter_reduce_ps.hlsl`,
  regenerated: 1996 / 462 words, manifests updated): every level writes
  `.r` = mean and `.g` = max of the log2 luminance; the reduction stops at
  the **tile image** (no axis above 128: 16×16 at 64×64, 80×48 at 1280×768,
  80×23 at 5120×1440) instead of 1×1. Levels, the two-slot ring and the
  readback surfaces are `G32R32F` (gate `chain_target`/`chain_sampling`;
  `A32B32G32R32F` fallback; `chain_format` on the `hdr_tonemap` line). The
  self test checks both channels (`meter_value`, `meter_max`).
- **Host statistic** (`src/renderer/exposure.{h,cpp}`, mirrored by
  `tools/analysis/exposure_reference.py`): `tile_weights` (raised cosine, 1 at
  the centre, `meter_edge_weight` 0.35 at the corners), `meter_statistics`
  (lit = tile mean ≥ log2(`meter_bg` 1/512); neutral when lit < `meter_min_lit`
  1 % of the tiles; the centre-weighted median of the lit tiles; the
  unweighted p99 tile maximum; `avg_log_l` kept for continuity; non-finite
  tiles read as the floor), `ev_key` (lift to the key in full, pull down ×
  `key_pull` 0.25), `ev_limit` (`log2(white_target 0.9 × 16.29) − p99`),
  `exposure_target` (min of the two, clamped to `ev_min/max` **−3..+2**),
  `apply_deadband` (`ev_deadband` 0.25 EV against the **held** target, so
  adaptation converges exactly; the first step takes the fresh target),
  `ExposureState::step(MeterStatistics, dt)`.
- **Readback** (`src/renderer/hdr_pass.{h,cpp}` `begin_frame`): the tile
  image is copied and locked a frame later as before, read into host arrays
  (allocated once per chain size), reduced, stepped. `chain_bytes` counts
  levels + ring + readbacks at 8/16 B per texel.
- **Env / flags**: `X3M_HDR_METER_BG`, `X3M_HDR_METER_MIN_LIT`,
  `X3M_HDR_WHITE_TARGET`, `X3M_HDR_KEY_PULL`, `X3M_HDR_EV_DEADBAND`,
  `X3M_HDR_METER_EDGE_WEIGHT` (`src/proxy/capture.cpp`); `tools/manage.py`
  `--hdr-meter-bg --hdr-white-target --hdr-key-pull --hdr-ev-deadband
  --hdr-edge-weight --hdr-ev-min --hdr-ev-max`.
- **Log**: `hdr_tonemap` gains the six parameters, `tile_max`, `chain_format`,
  `chain_target`, `chain_sampling` (the `r32f_*` fields are gone);
  `hdr_frame` gains `lit_fraction luma_lit luma_p99 ev_key ev_limit ev_fresh
  tiles lit` after `luma_mean` (`ev_target` is the held target). The fixture
  export `x3m_hdr_fixture_exposure` fills 18 floats.
- **Docs done**: the architecture doc's stage-2 section (switches, files,
  gates, meter subsection with the rationale, the run-15 offline replay:
  menu −0.62 EV, space scene 0..+2 EV expected), README launch paragraph.

## Fixtures and suites run so far

- `python3 -m unittest discover -s verification/analysis` (PYTHONPATH=
  verification/probe): **813 tests OK** (36 of them `test_exposure_reference.py`
  + `test_exposure_port.py`, the latter compiling `exposure.cpp` natively and
  replaying the reference on the synthetic tile scenes unweighted and
  centre-weighted, the dead band, three frame loops).
- Generator: the two meter shaders recompiled under the Wine lock
  (`generate_rigid_motion_pixel.py --shader hdr_meter_level0 --shader
  hdr_meter_reduce`); `--check` over all programs not yet rerun.
- `run_motion_output.py seam-hdr-exposure` (Steam bottle, partial mode): the
  DLL ran the new 120-frame script through frame 109 with the expected
  numbers (`hdr_frame`, state after each scene): black sky + patch
  `ev_key=1.34946 ev_limit=7.697 lit=16`; white frame `ev_key=-0.61848
  ev_limit=3.874`; sparks `ev_key=2.970 ev_limit=-2.12593` (the limit wins);
  grey → `ev_fresh=+2` clamp; dead band: frames 80–99 held one target, frame
  100 (0.6 grey) moved it to `-0.21263`; `meter_us` 16–30 µs (one draw at
  64×64), `readback_us` 50–70 µs (copy + lock + statistic on 256 tiles).
  The fixture then **failed its own `require` on frame 110** (the emitter
  scene): the background sample was taken at pixel (0, 0), which the
  emitter patch covers. Fixed after the run (`bg_code` is now the last
  pixel); the seam/fixture build after the fix succeeds. **The runner's new
  `validate_hdrexposure` has therefore not yet been exercised end to end.**

## What remains

1. Rerun `python3 verification/probe/wine_lock.py --holder meter python3
   verification/probe/run_motion_output.py seam-hdr-exposure
   seam-hdr-exposure-offset seam-ownership-hdr-exposure`, fix whatever the
   new validator trips on (it asserts the DLL statistic against
   `exposure_ref.meter_image` per frame, the target terms on the DLL's own
   statistic ≤ 1e-4 EV, the dead-band hold/move, the emitter's weighted
   median at the object, presented codes ≤ 1 code, `chain_levels=1`,
   `chain_bytes = 4·256·texel`), then the full suite (Steam, then
   `X3M_FIXTURE_BOTTLE=X3`), `run_temporal_pass.py`, `run_scene_capture.py`,
   `run_ownership_integration.py`, generator `--check`, `check_no_x87.py
   build/d3d9.dll`. Check `seam-taa-hdr-tonemap-auto` and the
   `hdrtonemapfault` cases still validate (they read `stepped`/`ev`).
2. Bench at 1280×768 and 5120×1440 (`bench-*-hdr-tonemap-taa-*`): record
   `meter_us`, `readback_us` and the boundary increment against the stage-2
   table; the chain is now 2 / 3 draws instead of 6 / 7.
3. Docs: `docs/verification/hdr-scene-path.md` — rewrite the "Exposure
   (`hdrexposure`, 40 frames…)" section for the 120-frame script with the
   measured numbers and the bench, update the gate paragraph (`r32f_*` →
   `chain_*`, `meter_max`); `docs/status.md` bullet; then review and commit.
4. Design points to keep in mind: the dead band is measured against the held
   target (documented deviation from "against the adapted EV" — equal once
   settled); the key pull 0.25 is a deviation from the plain key rule so a
   white frame lands at −0.62 EV, not mid-grey; the lit count / neutral test
   / highlight limit are unweighted, only the median and the lit mean are
   centre-weighted.

Production DLL of this state: `build/d3d9.dll`, SHA-256 in the commit
message's report (not installed into any bottle).
