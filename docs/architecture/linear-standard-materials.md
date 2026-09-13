# Split and standard lighting materials

Source and detached GPU qualification, 2026-09-13. This extension adds **40 exact SM3 pairs,
25 original programs (one VS and 24 PS), and 96 archive pass occurrences** to
[the installed DEFAULT](scene-linear-materials.md) and
[Argon BUMPMAP](linear-bump-materials.md) contracts. The pure transformer now
contains **70 pairs / 49 programs**. Detached GPU qualification passes. Live routing qualification and installation
of this extension are pending; the installed 30-pair evidence remains historical.
This is an intermediate group in the [complete coverage ledger](material-coverage.md),
not completion of all material families or shader models.

## Complete group and original algebra

Each row below contains ten pairs. Its first two PS use the base VS; each of
its remaining four PS pairs with both toggle VS. The complete archive proof
covers the base, `2s`, `_0000` and `_0001` aliases, all four toggle directories,
and the named technique. Actual VS/PS model 3.0 defines this group. SM1/SM2
programs, including those embedded in high-profile effects, remain in the
coverage ledger and gain no admission from these aliases.

| Family / technique | Base VS | Toggle VS | PS: base pair; four toggle programs |
| --- | --- | --- | --- |
| Split / DEFAULT | `53a0a641107ed76c` | `719856ce0c213220`, `badefd5143b3024f` | `462342e3e5781384`, `827d8d2d617bedce`; `02606104fa59fb29`, `1d638938d93421b3`, `bd4d51c08486c6e0`, `de2dd381fa64193d` |
| standard_lighting / DEFAULT | `494fe349b8bc12ec` | `719856ce0c213220`, `badefd5143b3024f` | `7c83ed50c9894e44`, `e70adc744a38ca59`; `db644b73b68c0547`, `ff32b602a271c327`, `f6a501717c3e5ca8`, `55826dc176afe464` |
| standard_lighting / BUMPMAP | `4944d81dfe531b37` | `19a246a56e9d9700`, `44c4a41ca92ae2e3` | `0c1f3f0f440e4a0c`, `64bac8bb307eb896`; `789449ffd931d23e`, `4f052209611387f0`, `abf3c0fad53456d8`, `cf449bcb069aec4f` |
| standard_lighting / BUMPMAP_LOW | `4944d81dfe531b37` | `19a246a56e9d9700`, `44c4a41ca92ae2e3` | `99153c144030c396`, `c1452981fd0bff64`; `b0f9313b77cc78ee`, `d514bf852d8a9c58`, `dff6a3d360603fa2`, `f1d14a7dbf7c6173` |

Split retains diffuse 0.5, outer specular strength 3, POW exponent 10 and cube
strength 1. Standard lighting preserves the application's diffuse, specular,
reflection and exponent scalars without decoding or adding a cap to them.
Their original CTAB defaults are 1, 1, 1 and 10 respectively; these are evidence
of the original effect defaults, not imposed values at draw time. Scalar
registers vary between base, affine single-light and non-affine single-light
programs. Exact original consumers are recorded in the
[derived profile](../reverse-engineering/linear-material-profiles.json).
Both retain the original saturated directional cosine and `sat(3*cosine)`
specular response. No angular instruction or original POW is replaced.

BUMPMAP retains alpha/green reconstruction: A feeds BINORMAL, G feeds TANGENT,
and the third component uses the original reciprocal-square-root sequence.
BUMPMAP_LOW instead uses signed XYZ (`2*sample.xyz-1`): X feeds BINORMAL, Y feeds
TANGENT and Z feeds the geometric normal; texture alpha is unused by this
normal path. Both retain normalization, VFACE handling and per-pixel reflection
coordinates. The numerical reference keeps the AG singular boundary distinct
from a valid XYZ normal; it does not substitute one encoding for the other.

The converted source roles and transfer policy stay unchanged: diffuse RGB
after any authored affine transform; raw directional and point RGB before
lighting; already strength-scaled material emissive without exponentiation;
lightmap RGB; and reflection cube RGB. Specular masks and normal maps remain
data. Every original sample retains partial precision. Added writes and linear
RGB consumers use XYZ and full precision; original sampled alpha, fog, geometry,
position, opacity and application scalar sources remain intact. The final RGB
sanitizer and compatibility encoding are the existing material policy.

## Bounded ABI and implementation

The [pure core](../../src/renderer/linear_material.cpp) adds explicit identity,
count, source-offset and RGB-destination rows. Its transformation algorithm and
the ordinary motion transformer are unchanged. All 70 pairs are explicitly
published, so the new standard base VS cannot pair with an old DEFAULT base PS
merely because its body is similar. The new VS body matches the existing loop
VS; its four differing declaration centroid flags are retained exactly.

| Contract | Motion | Current depth | Full-precision material RGB | PS material scratch |
| --- | --- | --- | --- | --- |
| DEFAULT | o6 / v5 / TEX4 | o7 / v6 / TEX5 | o8 / v7 / TEX6 | r9, r11–r13 |
| Standard DEFAULT base exception | o6 / v5 / TEX4 | o7 / v6 / **TEX7** | o8 / v7 / TEX6 | r9, r11–r13 |
| BUMPMAP and BUMPMAP_LOW | o7 / v6 / TEX5 | o8 / v7 / TEX6 | o9 / v8 / TEX7 | r10–r13 |

The exception belongs only to PS `7c83ed50c9894e44` and `e70adc744a38ca59` with
VS `494fe349b8bc12ec`. It preserves the existing temporal registry's assignment,
validates the exact expected semantic and rejects collision with material RGB.
It does not allocate a new varying dynamically. Material constants remain
VS c248–c249 and PS c212–c213; motion/depth c252–c255 and c216–c220 remain separate.
Original and transformed resource checks include depth-off variants.

The largest original is PS `64bac8bb307eb896`, **1,392 DWORDs** including opaque
comments. The read guard refuses 1,393 before hashing; every admitted program
still requires its exact fingerprint and individual word count. The combined
builder gives the immutable original to the row-explicit motion transformer,
verifies every copied span, and preserves temporal insertions byte-for-byte.
No raw shader bytes or decompiler listing is tracked.

## Host evidence and cost

Focused verification passes without Wine or a production DLL build:

- `test_linear_material_profiles`: **30 tests**, 49 originals / 70 pairs /
  240 total archive occurrences. Exact semantic schedules and direct operand,
  destination, deletion, literal, coefficient and alias mutations cover the
  24 new PS; the new VS and depth-semantic exception are proved separately.
- `test_linear_material_transformer`: **8 tests**, **392 variants** across
  49 originals, both depth modes and gains 0/1/4/16. The host driver performs
  6,543 checks, including input/output aliasing, refusal rollback, all pair and
  cross-family combinations, original reconstruction and temporal identity.
- `test_linear_material_reference`: **37 tests**, including independent
  nondefault application coefficients, scalar zeroes and above-cap strengths,
  affine/face cases, signed XYZ basis asymmetry and AG/XYZ boundary distinctions.
  The 36 numerical tests and final generated-profile crosscheck ran separately.
- The core translation unit cross-compiles with i686 MinGW, `-O2 -Werror`, SSE2
  floating point and the project's four-byte incoming stack contract.

The transformer test freezes all **192 previously installed outputs** using a
framed SHA-256 over filenames, lengths and bytes:
`8c27bf32d6e0f006ab51f937b4321aecefa30073040dcbdac140b9e25dd4ea84`.
All compare exactly; the earlier 120- and 72-output regressions also pass.
Prior 24 program proof records remain exact except added shared-family annotations.

| Maximum static weighted slots | Depth off | Depth on |
| --- | ---: | ---: |
| DEFAULT VS / PS | 80 / 166 | 82 / 168 |
| BUMP/LOW VS / PS | 85 / 178 | 87 / 180 |

Counts include POW/NRM as three, cube TEXLD as four, DP2ADD/LRP as two and the
reviewed flow costs. Unknown opcode/forms refuse rather than receiving an
assumed unit cost. The maximum remains below SM3's 512-slot minimum guarantee.
A local diagnostic measured 392 initial transformations in 6.82 ms total
(about 17.4 microseconds each); file I/O and negative checks took 1.44 s together.
These host timings are not game FPS or GPU cost. Allocation and validation
remain create-time work. Exact pair lookup is an allocation-free scan bounded
at 70 rows, consumed by the existing shader-state cache lifecycle.

## Detached GPU result

The [X3 result](../../verification/results/bottle-X3/linear-material-gpu.json)
passes **1,527 cases / 13,743 samples**, all **70 pairs / 49 originals**, with
381 successful shader creations and clean exit 0. The original 512-case prefix
is retained exactly. There are 13,500 analytical RGB samples and 243 samples
from 27 operational boundary cases. Across 390,912 pixels, alpha and temporal
attachments remain exact; boundary cases claim finite capped RGB storage and
preservation, not float64 equivalence at singular or exceptional inputs.

The maximum RGB envelope error is 0.00390625 across the full retained corpus;
all four new family groups stay at or below 0.00097656. The largest error is
15.97% of the allowed tolerance. Independent cases cover application scalars,
LOW signed XYZ/basis/reflection behavior, affine and face variants, light counts,
gains and missing history. The result binds the exact core/proof/reference and
fixture inputs; native Windows execution is not established by this X3 run.

The four-draw, 98,304-vertex EVENT-fenced diagnostic gives these median
motion-only → combined times (milliseconds, zero/eight point lights): Split
0.6865/0.6855 → 0.7888/0.7893; standard DEFAULT 0.6708/0.6903 → 0.7913/0.8003;
standard BUMP 0.7939/0.7901 → 0.7971/0.7993; standard LOW 0.7993/0.7911 →
0.8030/0.7889. These are six-sample QPC windows including CPU/backend submission
and EVENT completion. They are neither GPU timestamps nor game FPS, and the
small BUMP/LOW differences do not establish a speedup.

## Complete-group qualification contract; live result pending

The existing `run_linear_material.py` detached fixture and independent oracle
now cover all **70 pairs / 49 originals** in one batch. The required contract is
to retain the 512
historical cases/IDs, append all 40 new pair/depth/winding combinations, and
exercise 0/1/8 loop lights, fixed-point variants and independent RGB gains.
For every new PS, include coefficient discriminators, colored directions and
sampled inputs that separate Split exponent 10 from prior exponents and standard
nondefault scalar strengths from defaults. Cover affine/non-affine and face
variants together. LOW cases must distinguish X from A, signed Z, unused alpha,
and reflected cube direction. Include operational exceptional/boundary cases
separately from the positive-exponent analytical oracle.

Compare every original alpha pixel and unchanged motion/depth attachment with
the original/motion-only controls; assert finite capped material output. Alternate
old and new shared VS programs and the new standard base VS, including both
depth modes, to expose varying/cache collisions. Use the existing controlled
original/motion/combined diagnostic timing windows for representative Split,
standard DEFAULT, AG and XYZ paths; do not infer frame-rate benefit from them.

Extend the existing `run_linear_material_live.py` qualification to the complete
70-pair publication, all three source gains, cached sampler masks 0x0f/0x1f,
shared-stage alternation, failed creation, unsupported combinations, Reset and
reference retirement. Retain a motion-reviewed uncovered witness: Terran
`53a0a641107ed76c` / `ef2bf556f207b8bd` currently supplies it. Replace the previous
Split witness now that it is covered. No live gate broadening or new sampler
API is needed. One owner builds and runs the complete group under the X3 Wine
lease after source review; no subgroup GPU or installation cycles are required.
The authored live script now contains **164 frames per configuration**: the
24 lifecycle frames followed by all 70 pairs twice, establishing first
appearance and valid prior history. It reuses existing objects to create exactly
49 covered original identities; the two negative originals remain motion-only.
The runner checks all 49 variant creations and their additional owned references,
all pair identities, alpha and motion/depth twins, and the isolated unit-lightmap
activation witness (startup gain four). The detached GPU oracle supplies the
independent direct/material/lightmap gain and detailed normal/lobe arithmetic
coverage. The live script's TAA reference remains its existing fixture comparison;
it does not claim an additional exact TAA readback file comparison. Its three
report tests and x86 translation-unit compilation pass; the actual run is pending.

Native Windows execution and gameplay appearance/performance remain unverified.
