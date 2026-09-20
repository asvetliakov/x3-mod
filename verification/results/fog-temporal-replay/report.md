# Spatial fog through existing TAA: bounded derived witness

This replay combines the 32 actual-production 120×72 `FogPass` baseline
composites with the actual run185 depth, motion, scene, camera and jitter
sequence. The production report passed 576 fixture checks across all 256
variants; this replay binds its report, inputs manifest, 32 variant-zero case
results and individual readback hashes. It executes the
reviewed `resolve` function from `tools/analysis/taa_resolve_replay.py` verbatim.
The frozen data-i manifest is `b09d222c…da72`, the production GPU report is
`d919e3ee…b147`, the executable is `70217095…c98d`, and the exact
resolve-function source is `0cc9ea56…d048`.
Run185 moves 81.45–81.70 world units per frame across all four eight-frame
bursts. The first two bursts have zero camera rotation; the latter two reach
0.42 degrees per frame.

The replay seeds frame zero as current history, matching the required one-time
history invalidation. It point-reduces captured motion and depth with the same
indices as data-i and scales captured jitter to the reduced raster. It runs a
scene-only control beside the fogged branch. Their difference isolates how TAA
changes the incremental production fog contribution without calling the captured
scene a clean no-fog reference.

Across 210,672 crop samples, every resolved value was finite. TAA changed the
incremental fog contribution by a mean 0.118 display-relative luma codes,
p95 0.472, p99 1.204 and maximum 11.650. On 181,742 stable-sky samples, where
history uses rotation only, the mean was 0.097 codes, p95 0.432, p99 0.836 and
maximum 8.193. The actual current-versus-rotation-history fog-effect mismatch
had mean 0.000197, p99 0.001647 and maximum 0.030133 in scene-linear RGB units.

Worst per-frame-region p99 incremental-fog changes were:

| Burst | Translation/frame | Rotation max | Stable sky | Geometry | Thin foreground |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1974 | 81.653 | 0.000° | 1.143 | 6.620 | 10.170 |
| 9204 | 81.646 | 0.000° | 0.795 | 0.537 | 1.093 |
| 21901 | 81.450 | 0.420° | 1.365 | 3.167 | 3.687 |
| 26447 | 81.648 | 0.276° | 0.602 | 0.000 | 0.037 |

These values are a numeric witness, not a quality threshold or publication
pass. The source scene already contains native fog, and reduced motion is
derived from the full-resolution capture. Geometry and thin-foreground figures are
especially resolution-limited. No replay can establish in-flight smear,
disocclusion quality, native Windows behavior or game acceptance.

The production and prototype summary quantiles are identical at the reported
precision. Their 32 composites differ in 19 of 1,105,920 half values across ten
frames, with maximum absolute difference 0.000122071. The prototype witness is
preserved separately and is not relabeled as production evidence.

The remaining exact gap is a post-`FogPass` TAA/current capture during translated
flight. That is required for a real smear, trailing-bank, disocclusion and thin
foreground verdict.

The deterministic machine-readable result is local at
`build/fog-temporal-replay-production/report.json` (34,353 bytes, SHA-256
`3a32d270…6adf`). The preserved prototype result is
`build/fog-temporal-replay/report.json` (SHA-256 `c36540f9…8f2b`).
