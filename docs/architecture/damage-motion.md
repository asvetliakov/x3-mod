# Bounded damage motion epilogue

The two damage BUMPMAP pixel programs now have a separate motion-transform
class, `BoundedDamageBranches`. Implementation and host verification are
complete in the isolated candidate, and the corrected detached X3 GPU fixture
passes R2. Independent source/fix and post-run evidence review are approved.
Installation, gameplay integration and native Windows execution remain pending. This extends motion
and optional current depth only. It does not change material RGB, fill missing
XT DEFAULT varyings, or solve the material RGB input-register limit.

## Ownership and proof

The generated table owns exactly VS `37c34a7478544c14` paired with PS
`31445adb0a62d134` (damage 2s) and `d51cf763125cb85a` (damage). The original
PS lengths are 1,746 and 1,720 DWORDs. Exact full-program fingerprints, lengths,
versions and instruction boundaries remain required. Runtime per-stage variant
creation stays independent; exact VS/PS pairing is enforced by the existing
pair lookup and generated rows. Class C retains its static-boolean-only rule.

[The validator](../../src/renderer/damage_motion_validation.h) walks the entire
program and checks a separate owned contract before the existing generic
header/register/reservation validator. It proves:

- Two top-level `if b0` / `if b1` blocks; only the latter has `else`.
- Exactly one top-level `if_ne r1.y, -r1.y`, with a single full-precision
  `mov r1.z, c25.<zero>` body and the next instruction its `endif`.
- The native c25 literals, unconditional MAD initialization of r1.z and CMP
  initialization of r1.y, and no intervening overwrite of those scalar lanes.
  For finite samples the weight is `1 - 2 * occlusion.red`; the branch clamps
  negative weights to zero. The original partial precision remains intact.
- The complete known executable opcode/arity inventory and seven flow sites;
  no extra flow, loop, call, early return, kill, predication or coissue.
- Both native oC0 writes occur after every join, before final END. No output
  is conditional. The generic walk also checks every branch-body operand
  against reserved registers, relative addressing and oDepth restrictions.

| Site, original DWORD index | Damage 2s | Damage |
| --- | ---: | ---: |
| IFC / body / join | 1436 / 1439 / 1442 | 1418 / 1421 / 1424 |
| Final join | 1701 | 1675 |
| RGB / alpha writes | 1736 / 1741 | 1710 / 1715 |
| END and epilogue insertion | 1745 | 1719 |

No original executable instruction is rewritten. The existing initialized
motion fragment is appended at END, then the optional current-depth fragment.
All added arithmetic executes after the joins and introduces no texture
sampling or derivatives. The original shader uses r0–r5 and v0–v7; motion
uses r6–r8 and v8/TEXCOORD7, with v9/TEXCOORD8 for current depth. Existing
VS variants already export the same allocation for other pairs sharing this
VS. All prior 169 table rows retain their exact fields; two rows are added.

## Host verification and performance review

[The compact host record](../../verification/results/damage-motion-host.json)
records the compiler, source identities and release/ASan/UBSan results:
171 transformed rows, 1,711 checks, 1,560,128 input fingerprint mutations,
4,446 row perturbations, 7,689 program perturbations and 1,026 alias layouts
per mode. Both modes pass. The two damage rows each exercise every bit of
34 critical words directly against the structural validator (1,088 mutants
per shader), including the literal, initializers, flow, one-body and output
contracts. This direct call retains the original owned identity so those
checks do not stop at a fingerprint mismatch. Additional forbidden-flow
mutations exercise refusal outside the owned sites. The Python generator's
independent full-token proof passes the same critical-word mutation sweep;
41 focused generator/contract tests pass; after the review fix, all 44
fixture-report tests pass after the R1 diagnostic correction (85 tests across
the affected modules).
The retained R2 MinGW fixture compiled with warnings as errors and the required
SSE2/stack-ABI flags, then passed the root-owned X3 run described below.

The class-consumer audit covers generator classification/header emission,
header enum validity, per-stage supported-class and pixel-flow checks,
compile-time indices, unchanged fingerprint-based shader-cache keys, fixture
selection and report parsing. Linear-material class selection remains limited
to its existing exact materials; this class does not opt either shader into
linear RGB conversion.

The additional validation is one allocation-free linear walk at shader
creation. It adds no per-draw allocation, validation, locking or lookup pass.
The pair table continues to use the existing binary search. The GPU epilogue
has the same instruction sequence as existing motion producers; register
relocation changes indices only. Native programs have 100/95 arithmetic and
7 texture slots respectively under the inspector's existing slot model,
plus 25 motion arithmetic slots and optionally 2 current-depth arithmetic
slots. R2 passes driver shader creation and actual execution on X3. The
fixture's alternating event-completion timing measures submission, draw and
completion, not game FPS or isolated GPU time.

## Review fix

Independent review found that the inherited aggregate boolean sensitivity
control could pass when only b0 or b1 affected the supplied image. Damage mode
now requires both fixed-other-bit comparisons separately: b00 versus b01 and
b00 versus b10. The report parser requires distinct control labels and exact
counts. Four negative tests reject a missing b0/b1 control, a duplicated
single-bit control and substitution of the old aggregate control. The generic
historical fixture behavior is unchanged. Only the affected 36 report tests
were rerun; no build or Wine execution was performed for this fixture-only fix.
That pre-review executable was superseded by the reviewed R1 and R2 builds.

## First GPU attempt and texel-center correction

The root-owned X3 attempt R1 used fixture SHA-256
`25f2d4e19fe399f486a02e32711f86c7236d5ba969d1c06c7740ab0bd16cbf4b`.
Exact pair transformation and isolated b0/b1, alpha, occlusion and detail
sensitivity passed. The first native IFC witness then reported 8 mismatches
among 32 adjacent pixels, before any epilogue comparison. Immutable evidence
is preserved at `/tmp/x3-damage-motion-r1`; the compact host record records
this failed attempt. It is not a production shader failure or GPU acceptance.

A source and token audit found that the oversized triangle maps integer
screen pixel centers to `u=x/32`, exactly on POINT texture boundaries. The
native VS copies vertex TEXCOORD0.zw directly at DWORD 764; the 2s PS samples
s5 with those lanes at DWORD 1407. Microsoft's
[Direct3D 9 pixel mapping](https://learn.microsoft.com/en-us/windows/win32/direct3d9/directly-mapping-texels-to-pixels)
and [nearest-point sampling](https://learn.microsoft.com/en-us/windows/win32/direct3d9/nearest-point-sampling)
document why boundary rounding can select neighboring texels. R1 retained
aggregate failures only, so the exact values that failed remain unknown.

The consolidated fixture correction moves damage XYZW coordinates to texel
centers with `+0.5/width,+0.5/height` offsets in both vertex formats. At 32×32
these authored coordinates are exactly representable in FP16 and FP32.
The exact occlusion/weight/condition threshold oracle remains unchanged.
A second diagnostic FP32 target exposes the native sampled occlusion and
interpolated UVzw beside the existing pre/post-branch values. Bounded first-case
and failure traces record float values and raw bits to separate sampling
phase from branch arithmetic. This changes fixture inputs and diagnostics
only; the production transformation is unchanged. The consolidated fixture
compiles cleanly; all 44 affected report tests pass. Its source/artifact record
is `diagnostic_fixture_r2` in the compact evidence. The same reviewer approved
the correction before the root executed the successful R2 run.

## X3 GPU qualification R2

The existing detached fixture has a bounded `--damage` mode using the actual
local originals. Its XT input setup uses float4/half4 TEXCOORD0 (including
occlusion ZW), the native constant layout, and all seven native samplers. The
occlusion texture uses exact FP32 0.25/0.5/0.75 stripes in adjacent pixels of
one draw. A diagnostic variant checks native pre/post IFC scalars and vFace;
color equality alone is insufficient evidence of both branch outcomes.

R2 passes the mixed-format inventory of 192 cases / 576 configurations,
including static valid, moving valid and
moving invalid history in every case. The mode covers both exact PS, all
four b0/b1 combinations with separate native b0 and b1 sensitivity controls,
front/back winding for 2s, motion with and without RT2, clean and poisoned r6–r8, native
RGB/alpha twins, material alpha/occlusion/detail sensitivity, analytic and
replay motion/depth references, Reset and alternating completion timings.
Original-derived diagnostic bytecode is constructed in memory and remains
untracked. Generic all-row mode explicitly directs class D to this mode so
it cannot silently run with unrelated hull constants or vertex declarations.

The [canonical R2 summary](../../verification/results/bottle-X3/material-motion-damage-summary.json)
binds the exact local inputs, source hashes, retained executable and report.
It records **9,869 checks, 20,736 numerical samples, 7,077,888 exact color
component comparisons, 1,152 depth cases, one Reset and 36 timing samples**.
The run used bottle X3, arm64 Wine/FEX with `FEX_X87REDUCEDPRECISION=1` and
`WINEMSYNC=1`, and system32 D3D9. All 192 native IFC witnesses have zero bad
pixels; both face signs are exercised. Original and augmented RGB/alpha are
exact throughout the qualified cases. The immutable verbose report remains
local at `/tmp/x3-damage-motion-r2`.

The first 32-pixel raw trace confirms the intended sampled red pattern exactly:
0.25/0.5/0.75 (FP32 bits `3e800000`/`3f000000`/`3f400000`). Native pre-branch
weights are 0.5/0/-0.5, post-branch weights 0.5/0/0, and conditions 0/0/1.
There are 11 below-threshold, 11 exact-threshold and 10 above-threshold pixels.
The largest interpolated UV departure from its intended texel center is
`5.960464477539063e-8`, safely within the quarter-texel diagnostic margin.
This establishes the corrected fixture's sampling and branch contract. It
supports the boundary-phase explanation for R1, but cannot establish R1's
precise mismatch values because that run did not retain them.

### Paired completion timings

Each PS has six alternating three-mode groups at 1280×768/BGRA8. The paired
difference compares the augmented draw with the native draw within the same
group. These event-completion windows include clears, CPU submission and
completion waiting; the augmented mode also clears/writes its additional
motion and current-depth targets. They are neither GPU-only timings nor FPS,
and the small noisy sample does not establish gameplay cost.

| Pixel program | Native median, ms | Augmented median, ms | Paired delta median, ms | Paired delta range, ms |
| --- | ---: | ---: | ---: | ---: |
| Damage 2s `31445adb0a62d134` | 0.77970 | 0.91825 | +0.11175 | -0.91880 to +0.15150 |
| Damage `d51cf763125cb85a` | 1.15720 | 1.10235 | +0.11915 | -0.34560 to +1.34140 |

The paired means are -0.08788 ms and +0.20222 ms respectively, illustrating
the sensitivity to completion-window outliers. Native-plus-replay reference
medians are 1.04130 ms and 1.31840 ms. The source performance audit found no
new per-draw CPU work; no broader benchmark was repeated.

### Remaining limits

This is detached shader qualification on X3's hardware-processing device.
The functional cases use zero point lights; the completion-timing pass uses
eight. Existing unchanged VS light-loop and route state/lifetime evidence is
reused. R2 covers Reset, not a new ResetEx, PURE-device or live shader-cache
integration sweep. Native Windows/D3D behavior and gameplay integration of
these two new pairs remain unverified. No production DLL was built or
installed for this extension, and no game was launched by the agent. The
implementation adds neither linear RGB conversion nor general dynamic-flow
support, and it does not repair malformed XT DEFAULT linkage.
