# Material fill-light verification ledger

Final review PASS, 2026-09-15. This is evidence for the fill transform and its
explicit K=0 parity mode. A later user decision selects 0.03 as the production
default with linear materials; this ledger does not establish screenshot
acceptance for 0.03 or native-Windows behaviour.

## Provenance and scope

- Initial implementation: `294c155` (Constant hemispherical fill in the
  converted material law). Its follow-up corrections and candidate
  qualification are recorded in the contemporaneous handoff below.
- Reported host build: `cmake --build build -j4` passed with zero warnings
  ([`/tmp/x3-fill-qualification-build.log`](/tmp/x3-fill-qualification-build.log)).
  `build_motion_output.sh` also passed
  ([`/tmp/x3-fill-live-build.log`](/tmp/x3-fill-live-build.log)).
- All detached fixture results below used bottle **X3**, WineArch **arm64**,
  `FEX_X87REDUCEDPRECISION=1`, and `WINEMSYNC=1`; none launched the game.

## Host checks

Reported author checks passed: fill/live, 21 tests and the retained 20,490
seam checks; fade, 8 tests covering 130 originals in three exact
configurations; and the corrected reference/report suite, 115 tests in
59.591 s. The targeted duplicate-validator check passed one test in 4.893 s. MinGW
syntax compilation reported zero warnings. After the review corrections, the
affected live suite passed 17 tests and 20,495 checks.

Review corrections: Auto exposure is no longer compared with the fixed-EV0
run-51 baseline; failed insertion reports `fill_applied=0`; the luma metric is
the pre-RT Rec.709 scene-linear value decoded from the RGBA32F compatibility
output; and the report validator rejects missing or duplicate creation and
sample evidence. Final review passed with no blockers.

## Detached results

| Slice | Result | Evidence |
| --- | --- | --- |
| K=0 live route | PASS | 8 configurations, 32,516 checks. The corpus covers actual `evaluate_draw` execution across 148 exact pairs / 115 originals, material on/off, TAA on/off, ownership, Reset, state blocks, cached gains and shader retirement. [`linear-material-fill-live.json`](../../verification/results/bottle-X3/linear-material-fill-live.json) |
| K=0 detached parity | PASS | The new corpus has 4,177 cases and 37,593 samples across 168 pairs / 137 originals, including 14 XT and six SM3 glass pairs. Against the retained 3,923-case baseline, all 42,596 shared `SAMPLE`/`BASELINE`/`INVARIANT` rows are equal: 0 missing and 0 different. The 4,826 additions are the review-confirmed 254 glass cases × 19. [`linear-material-fill-k0-gpu.json`](../../verification/results/bottle-X3/linear-material-fill-k0-gpu.json), [`/tmp/x3-fill-k0-parity.json`](/tmp/x3-fill-k0-parity.json) |
| K=0.06 fill oracle | PASS | 23 sun-averted FP16 cases, 23 pairs, 207 FP16 samples and 207 RGBA32F twins. The 17 hull/asteroid/palette families, four XT standard/damage techniques, and glass agree with the one-FP16-code oracle: encoded FP16 RGB tolerance 1, observed maximum 1; alpha is exact. Pre-RT Rec.709 scene-linear luma has tolerance 1 and maximum 0. The separately reconstructed FP16 image has luma maximum 3; it does not strengthen the pre-RT image claim. The 54 ordinary-black samples are included. [`linear-material-fill-gpu.json`](../../verification/results/bottle-X3/linear-material-fill-gpu.json) |

The K=0 detached run took 28.184 s inside its child process (lock wait
0.000003 s); the corrected oracle run took 5.970 s (lock wait 0.000004 s).
[`/tmp/x3-fill-k0-timings.json`](/tmp/x3-fill-k0-timings.json) and
[`/tmp/x3-fill-oracle-corrected-timings.json`](/tmp/x3-fill-oracle-corrected-timings.json)
retain the measurements. These are fixture timings, not GPU timestamps or
gameplay FPS.

The hardened duplicate validator post-validated the retained GPU artifact and
its raw report without a GPU rerun: 23 filled pixel-shader creates, 207 sample
and 207 luma rows, with the retained artifact and runtime hashes bound to the
new validator. [`post-validation-2026-09-15.json`](/tmp/x3-linear-material-fill-gpu-corrected/post-validation-2026-09-15.json)
records that binding.

## Boundaries and open evidence

The K=0.06 oracle excludes the XT terraformer: its occlusion RGB also feeds an
additive-emission source, so it is outside this isolated fill-law comparison.
The K=0 live and detached checks do not prove a user-visible fill change.
Run 23 supplies limited CrossOver gameplay evidence below; gameplay performance
and native Windows runtime remain unverified. The current installed-build
description and compact install record are linked in the candidate handoff below.
The change adds no per-draw work; this evidence includes no benchmark or FPS
claim.


### Candidate handoff

The clean reviewed candidate has passed its host audits and X3 load check and
was installed by the main session. [Status](../status.md) is the authoritative
installed-build description; [the compact install record](../../verification/results/run23-candidate-install.json)
binds source, toolchain, hash, scoped results and rollback. Run23 later
completed; its evidence supports the fill law at explicit K=0.06. The later
0.03 default is the user's appearance decision, not a result claimed by this record.

### Run 23 / run54 gameplay evidence

The user completed Run 23 in `/tmp/x3-bottleX3-run54` with `--material-fill
0.06`. The analysis maps screenshots/readbacks as Auto far/near frames
3481/4887 and fixed-EV0 far/near frames 7060/7687. The user reports brighter
hulls and asked about 0.04; at that checkpoint **0.06 was retained provisionally**
and 0.04 was an optional preference bracket. The later user decision selects
0.03 as the production default; there is no 0.03 screenshot acceptance in this run.

The fixed-EV0 far frame passes the stated numeric thresholds: module p10
0.0672 (minimum 0.045), dark fraction 0.0115 (maximum 0.10), and cylinder mean
0.1519 (maximum 0.165). The approximate reprojected far-dark gain is 1.781,
which is supportive only: it has 172 dark samples and no root mask. The poses
differ, and unmatched dark-chroma difference is about 0.081, so that result is
neither a passing chroma check nor a controlled comparison. Auto exposure is a
global pre-tonemap multiplier: +1.5 EV gives 2.828×. The analysis result is
[`results.json`](/tmp/x3-run54-fill-analysis/results.json).

### Selected defaults checkpoint

The user selected fill 0.03 and Auto exposure ceiling +1.0 on 2026-09-15.
Source/default review passed; the three focused host modules passed 34 tests
(independent run 9.298 s), and capture syntax compilation passed with the
project ABI flags. Main's affected `./x3run --dry-run` emitted Auto, EV max 1.0
and fill 0.03 without explicit value options; vanilla dry-run also passed.
The current launcher applies these defaults to the existing DLL; its direct
initialization defaults update with the next candidate. Explicit fill 0 and
EV ceiling 1.5 remain supported. This preference choice does not establish
new 0.03 image/GPU acceptance or change the retained 0.06 law oracle.

### 2026-09-16 raised defaults (fill 0.05, Auto ceiling +1.3 EV)

The user raised the two appearance defaults: `X3M_MATERIAL_FILL` 0.03 → 0.05
and `X3M_HDR_EV_MAX` 1.0 → 1.3, in the launcher and in the DLL's direct
fallback (`src/proxy/capture.cpp`). Explicit `--material-fill 0` still disables
the term and keeps byte-identical programs; fill still requires
`--linear-materials`. The motion-output runner's mirror of the DLL ceiling
(`HDR_EV_MAX_DEFAULT`) moved with it so the reference adaptation still clamps
like the DLL. Host/default evidence only; this does not establish new image or
GPU acceptance at 0.05/+1.3.

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_linear_material_fill verification.analysis.test_linear_material_live verification.analysis.test_comparison_hotkeys verification.analysis.test_motion_output_runner verification.analysis.test_screen_emission_live   # OK
cmake -S . -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -B build && cmake --build build -j4   # 0 warnings
python3 verification/probe/check_no_x87.py build/d3d9.dll                        # result PASS, reachable_functions=225, violations {}
./x3run --camera chase --motion-output --object-trace --ownership --object-lifetime --taa --hdr --hdr-tonemap --linear-materials --dry-run   # X3M_HDR_EV_MAX=1.3, X3M_MATERIAL_FILL=0.05
```
