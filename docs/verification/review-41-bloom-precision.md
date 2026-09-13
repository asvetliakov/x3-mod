# Review 41: retained bloom precision qualification

2026-09-13. Independent Sol high review approved the characterization, offline
analyzer and retained evidence after fixes. This qualifies the standalone
filter's retained 40 cases/540 images on the tested Steam backend. It does not
qualify native Windows, combined AgX/sharpen execution, production state and
recovery, or game integration. No production filter change was needed.

The original GPU terminal remains rejected: 38 reconstruction images exceeded
the original ideal oracle's fixed tolerance. Separate authored controls measured
FP16 stores toward zero, quantized sampler fractions and one-dimensional
sample-result rounding. Calibration never reads bloom output values. Both
whole-chain predictions from authored source uploads and local-stage predictions
from preceding GPU inputs match all 421,446 modeled channels exactly. Modeling
is restricted to the four affected one-dimensional cases.

The sampled-result nearest-up mode was added after training residual inspection;
12,288 unchanged held-out controls passed. An exact two-dimensional internal
rounding model was not established. A one-FP16-ULP envelope was introduced after
training inspection, then passed 12,288 unchanged held-out 2D controls. This is
a measured control envelope, not a universal D3D9 precision guarantee. The
content-independent UV maps constrain raster interpolation without fitting
output RGB. The original ideal tolerance was not widened.

| Review finding | Status |
| --- | --- |
| Historical pass flags could conceal a new ideal-oracle failure. | Fixed: recompute all 540 verdicts with the current oracle and require every unmodeled/2D image to pass; a negative control exercises concealment. The original 38 failures remain, with zero 2D/unmodeled failures. |
| Characterization wording obscured when model candidates were added. | Fixed: disclose post-training changes and unchanged held-out evaluation. |
| The prior 52-control rejection was described as bound without its identity in the final record. | Fixed: record its retained path and SHA-256. Both characterization rejection terminals remain preserved. |
| Status documents still described the completed diagnosis as pending. | Fixed: report scoped numerical qualification while retaining integration and platform limits. |

Eight host tests, isolated x86 cross-compilation, artifact/hash/count checks and
byte-identical offline report reproduction passed independent review. The
analyzer verifies input, runtime, shader and readback provenance before applying
its scoped gates. Hashes are evidence provenance, never renderer prerequisites.
This analysis performs no new GPU execution or performance measurement.

Evidence: [fixture and reproduction](bloom-filter-fixture.md),
[characterization](../../verification/results/bloom-filter-characterization-summary.json),
[precision result](../../verification/results/bloom-filter-precision-summary.json),
and [original rejection](../../verification/results/bloom-filter-first-gpu-failure.json).
