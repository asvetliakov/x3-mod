# Material fill-light verification ledger

Final review PASS, 2026-09-15. This is evidence for the default-off
`--material-fill` implementation; it does not change feature acceptance,
describe an installed build, or establish gameplay or native-Windows behaviour.

## Provenance and scope

- Initial implementation: `294c155` (Constant hemispherical fill in the
  converted material law). Follow-up corrections await their clean candidate
  build.
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
Gameplay appearance, gameplay performance, and native Windows runtime remain
unverified. The candidate awaits a clean Sol build and has not been installed.
The change adds no per-draw work; this evidence includes no benchmark or FPS
claim.


### Candidate handoff

The clean reviewed candidate has passed its host audits and X3 load check and
was installed by the main session. [Status](../status.md) is the authoritative
installed-build description; [the compact install record](../../verification/results/run23-candidate-install.json)
binds source, toolchain, hash, scoped results and rollback. Run23 is ready; no
game was launched and the fill default remains 0 pending the user's verdict.
