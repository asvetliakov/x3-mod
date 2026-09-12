# Handoff: space-aware exposure meter (verified 2026-09-13)

## Completed verification (2026-09-13)

Resumed from committed branch state `ee7d2dc`, with no preexisting
uncommitted changes. The space-aware meter and review corrections now pass
both complete Steam and X3 motion-output suites: 98 validation cases plus
16 benchmark invocations per bottle. The three exposure cases each pass
245 checks / 120 frames; fault-15 readback failure holds the complete state
and next-frame success resumes adaptation (59 checks / 9 frames), while
fault-16 attach unlock failure refuses metering but retains AgX output
(23 checks / 3 frames).

Supporting verification passes in both bottles: TemporalPass 386 samples /
278 state restorations / two device generations; SceneCapture 4,908 checks /
16 samples; ownership integration 26 environments. Host analysis passes
814 tests, including production-parser controls built optimized and with
ASan/UBSan (21 controls each). Final static DLL audit passes 195 reachable
functions / no forbidden x87 arithmetic; the shader generator's `--check`
passes all ten programs. Durable audit:
`verification/results/exposure-final-checks.json`.

Independent [review 32](review-32-exposure.md) is closed. Its findings are
fixed: failed frame/self-test unlocks no longer publish successful meter
results, and malformed/truncated/non-finite meter settings cannot silently
disable safeguards. Runtime exposed three fixture-only issues, now reviewed:
visible-region color witnesses for overlaps, equal-precision target
comparison, and dead-band stimuli that escape the preceding clamp in both
offset variants. Production policy was unchanged by these fixture repairs.

The original-data statistic benchmark passes on native host, Steam/Rosetta
and X3/FEX, with construction/allocation/weights outside timing, 20 warmup
calls and 41 batches of 40 calls. Dense FEX median/p95 costs are
43.25/59.50 µs (80×48), 16.48/19.65 µs (80×23), and 448.80/486.58 µs
(maximum 128×128). Keep the bounded sort. Source/compiler/executable/output
provenance is retained in the three `exposure-statistics*.json` reports;
each bottle has its own local executable. Boundary and readback costs are
separated in [verification](hdr-scene-path.md); these are not game FPS.

All Wine commands were serialized with `wine_lock.py` after the orchestrator
released the slot, with no running game. No game launch or install occurred.
The slot has been returned to the orchestrator for reader verification.
The later `exposure_reference.py` wording correction changed only its module
docstring, after both complete motion suites; executable AST equality and
old/new hashes are recorded in `exposure-reference-doc-only.json`. No GPU
rerun was used to imply a numerical change from documentation.

The motion runner did not include imported numerical/helper modules in its
before/after source map. `exposure-final-checks.json` therefore labels their
hashes as post-run only; it does not claim those omitted files were frozen by
the suite. Root will extend the main integration manifest to include them.

Branch `worktree-agent-aea55d854948bd6a0` remains separate from main. The
orchestrator authorized its reviewed checkpoint commit and owns merge,
integration verification and installation. Recursive result attributes now
preserve nested X3 report bytes; staged blobs must equal their disk files.

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

## Historical paused-state verification

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

The orchestrator merges this reviewed branch checkpoint and verifies changes
affected by integration before installing. The new policy still needs actual
resolved game captures and user appearance acceptance against the preferred
fixed-EV-zero look. The [run-16 counterfactual](run16-exposure-baseline.md)
is derived from unresolved inputs, with all seven requests clipped to the
+2 cap; it does not validate the actual post-TAA meter or live tuning.
Keep EV −3…+2, weighted lit key, p99 ceiling, quarter-strength key darkening
and the held-target dead band unchanged pending that evidence. Native Windows
remains a required target whose runtime behavior has not been tested here.
