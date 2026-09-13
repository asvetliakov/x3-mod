# Authored alpha glow in the HDR bloom replacement

2026-09-13. Source/host correction reviewed; staged native shader compilation and the
standalone GPU corpus pass on X3. The nine GPU-matched shader artifacts have
been promoted without recompilation and verified in the installed DLL;
[combined qualification](../verification/combined-glow-materials.md) passes. Gameplay
appearance is now confirmed in run 27, but the user finds the glow too subtle;
gameplay frame cost remains unqualified. This changes extraction inside the existing
[BloomPass](bloom-pass-runtime.md), not the original-once lifetime/state boundary.
The installed build uses authored gain 0.35 and highlight gain 0.05. The separate
[exposure policy](space-exposure-policy.md) now defaults to Auto capped at +1.5 EV.

## Run 27 strength adjustment

The user confirms working bloom and accepts Auto capped at +1.5 EV. Increase
only authored gain from 0.10 to **0.35**, preserving highlight gain 0.05 and the
existing radius/scatter. The calibration below already evaluates this value:
at +1.5 EV the two source frames predict mean max-channel changes of 6.09–9.36
display codes on emitters, 9.45–15.89 on unmarked pixels within eight pixels of
strong emitters, and 0.176–0.226 farther than 32 pixels. At the prior installed 0.10,
the corresponding ranges are 1.95–2.98, 3.72–6.49 and 0.061–0.070.
These are bounded offline estimates, not final gameplay pixels or native parity.

This gives a stronger local halo without changing the unmarked highlight policy.
The sweep cannot qualify halo shape because radius was held fixed. If strength
is sufficient but spread remains too tight, evaluate radius separately. No shader
recompile, extra draw, resource, texture fetch or CPU work is introduced: the
existing constant carries the new gain. Gain 0.35 is installed; visual acceptance
is queued as [Run 9](../verification/user-runs.md#9-stronger-glow-and-selectionvoice-timing--ready).

## Observed failure and native meaning

Run 26 used installed checkpoint `75dbbed` and DLL
`3cbb350c3148e3e703677182cb9fb7b85e28af048f5b19c0dbe29b570ef9e42a`.
The captured creation flags are requested `0x52`, effective `0x42`; bloom
attached successfully. All 153 sampled prepare and 153 sampled commit records
reported success. Runtime ON/OFF records show effective contribution switching.
This excludes the previous pure-device attachment refusal as this run's cause.

The replacement nevertheless omitted a native glow term. The
[corrected shader study](../reverse-engineering/compositor-and-glow.md) proves
that native extraction stores `A * C` in RGB, independently of brightness,
and a thresholded white-highlight term multiplied by `1-saturate(A)` in alpha.
The observed final colored/white gains are 2 / 0.9, with threshold 0.87; the
25-tap blur has weight sum 1.2 per axis. Native composition uses a display-space
screen blend. These coefficients are not linear-light bloom coefficients.

The initial replacement used only a luminance threshold (1, half-width 0.5)
and final gain 0.05. In the two ON captures 11079 and 36969, no decoded pixel
exceeded luminance 1. Of the 2,571 / 8,858 pixels with alpha above 0.5,
992 / 7,163 had luminance at or below 0.5 and contributed exactly zero.
The existing reference predicts only 215 / 22 pixels with more than one
rounded 8-bit code of change out of 983,040, at fixed EV 0. These are offline
predictions, not final-main GPU measurements: the recorded `present_*.bgra8`
comes from ordinary HDR writeback before the original compositor and replacement.

The input is the retained FP16 scene after this frame's TAA resolve. Its alpha
is native scene glow bookkeeping, not opacity or a newly inferred material
mask. The resolve carries current alpha without temporal blending (NaN current
alpha is already canonicalized to 1 there); ordinary AgX carries that alpha.
HDR and resolved-TAA alpha were bit-identical across both complete sampled ON
frames. Bloom reads this retained alpha before the genuine compositor runs;
commit still preserves the genuine destination alpha with RGB-only writes.
FP16 alpha preserves more precision than native A8 writeback, so the correction
preserves authored intent rather than promising the native quantized mask.

## Bounded extraction policy

For decoded, clamped, exposed RGB `E`, original soft-knee weight `w(E)` and
ordered-clamped retained alpha `a`, extraction is:

```
G_authored * a * E + G_highlight * (1-a) * w(E) * E
```

`G_authored` starts at 0.10 and `G_highlight` at 0.05. The complementary term prevents counting the same
fully marked emitter again as an HDR highlight. Unmarked sources retain only
the original thresholded contribution; lowering the threshold globally is not
part of this change. Both terms share the existing normalized five-level,
scatter-0.7 pyramid. Final contribution is 1 when ON and 0 when OFF. OFF keeps
the same complete replacement path so native glow does not reappear; exposure
and sharpening use the exact ordinary-writeback snapshot.

Alpha policy at the extraction API is NaN/negative/minus-infinity to 0,
plus-infinity/values above 1 to 1, otherwise unchanged. A zero authored gain
selects the original RGB-only source arithmetic before consulting alpha, including
nonfinite alpha. This retains the existing standalone default and reference
cases. Positive authored gain opts into the new mode; gains are validated
finite, authored in [0,4] and highlight in [0,1]. The sum is clamped to 65,504
before its first FP16 store. No gain is exposed as a new launcher workflow.

The five-register bloom ABI stays c24–c28. Previously unused c27.w holds the
authored gain and c28.w the highlight gain; c28.xyz retain the decode ABI.
AgX c8–c21 and exact sharpen c23 are unchanged. The compiled programs remain straight-line SM3 within the minimum budget;
the GPU corpus exercises finite and nonfinite alpha behavior.

## Calibration and verification scope

Calibration uses matched source frames at EV 0 / +1.5 with authored gains
0.05, 0.1, 0.2, 0.35 and 0.5, keeping radius/scatter fixed. It separates marked
source change from unmarked dark-background spill. The rejected exploratory
gain 2 altered over one code in 9.2–20.3% of the EV-0 images; the native gain
2 therefore must not be transplanted as a linear default. Exposure remains
an independent art decision. At gain 0.10 / EV 0, mean max-channel display
change on alpha-positive pixels is 1.178 / 1.356 codes for frames 11079 / 36969.
On unmarked pixels within eight Chebyshev pixels of alpha>0.5 emitters it is
3.625 / 1.776 codes; farther than 32 pixels it is 0.0174 / 0.0278 codes. At
EV +1.5 those far means become 0.0610 / 0.0699 codes. These spatial bins
distinguish intended local halos from broad background veiling. The
[compact calibration result](../../verification/results/run26-bloom-authored-calibration.json)
retains all 20 cases, metric definitions and input/script provenance. Exact
native pixels are not emulated: address mode and down/up sampling plus native
partial-precision/UNORM behavior remain outside this bounded calibration.
CPU reference results omit intermediate FP16
rounding and are not native pixel parity or gameplay acceptance.

The source-only GPU corpus now has 24 unchanged legacy cases (canonical
CPU input/expected hashes pinned) and 12 authored cases (six ON/OFF pairs) across decode modes,
odd/even geometry and EV 0 / +1.5 / +2. Its host acceptance module passes
7 tests, including NaN/minus-infinity/plus-infinity alpha with finite RGB;
these do not qualify GPU execution or byte-exact legacy GPU output. Shared
shader recompilation is checked against the existing bounded three-code GPU
oracle, not an old/new byte-exact comparison. The final authored case reuses
the existing Reset/recreate image check.

The affected reference module passes 22 host tests. The capture handoff,
comparison, lifetime and composition modules pass 13 tests; the extracted
production retain callback passes 45 assertions in release and ASan/UBSan
runs. These cover exact display-block retention, final ON/OFF gain, retain-once
and refusal behavior. Existing bridge/Reset/state/recovery evidence applies
to unchanged ownership/control flow; the actual authored GPU images now pass the
focused fixture described below. No native Windows run is claimed.

Performance is bounded to extraction arithmetic and two constant components.
There are no additional texture fetches, shader passes, readbacks, resources,
locks, per-draw CPU hooks or meter/history work. The original compositor still
executes exactly once. The paired completion timings below measure the affected cost; the CPU image
reference itself is not a performance benchmark.


### Root-owned artifact qualification

`tools/shaders/stage_bloom_programs.py compile --directory <fresh-dir>` runs
under `wine_lock.py` with `X3M_FIXTURE_BOTTLE=X3`. It snapshots all shader,
include, tool, compiler and baseline-header inputs before any compilation,
then stages nine bloom programs and complete old/new twelve-program CSO
bundles. One `artifacts.json` records conservative static budgets, baseline
slot/DWORD deltas and input/artifact identities. A failed budget or changed
input refuses qualification; no embedded include is changed by compilation.

The existing `run_bloom_pass.py --output-dir <fresh-dir>` compiles only its
standalone fixture and checks the 36-case corpus on the root-owned Wine lease.
The separate `bloom_pass_timing_fixture_build.py` builds the retained-CSO timing
executable without running it. Its explicit old/new directory arguments select
1280×768 and 1279×767 paired measurements: three warmup pairs and five measured
pairs per full-pass/extraction metric, alternating order. Completed EVENT-query
fences bound QPC wall time, including CPU, driver, GPU completion and query
cost; these are neither pure GPU timestamps nor game FPS. The extraction span
uses only the copied native DrawPrimitiveUP slot in a separate batch, keeping
full-pass timing free of that synchronization. Actual caps and shader-creation
acceptance are printed. The bounded runtime timing result is recorded below.

After review accepts those results, `stage_bloom_programs.py promote
--directory <staged-dir> --gpu-summary <passed-summary>` validates stable inputs,
all retained CSOs and exact matching GPU-used shader hashes before writing the
nine canonical includes/records. It never recompiles or builds the DLL. The
focused staging tests cover whole-batch drift and successful/refused promotion
before any source write. The original historical promotion tool is unchanged.


### Compiled and GPU evidence

Root ran staged compilation under the X3 Wine lease. All nine programs pass
static qualification; the unchanged down/up/AgX programs retain their bytecode
identity. All extraction programs remain straight-line, without increased
texture fetch counts. The worst case is 425 of the minimum 512 SM3 slots.

| Extraction | Old slots | New slots | Old DWORDs | New DWORDs |
|---|---:|---:|---:|---:|
| Generic gamma 2.2 | 330 | 397 | 1,375 | 1,665 |
| Generic sRGB | 362 | 425 | 1,498 | 1,775 |
| Generic linear | 245 | 308 | 1,060 | 1,337 |
| Even gamma 2.2 | 145 | 176 | 658 | 799 |
| Even sRGB | 160 | 188 | 722 | 849 |
| Even linear | 108 | 136 | 518 | 651 |

The actual BloomPass fixture exits zero: 36 image cases, 16 controls, 52 checks
and one native Reset/recreate image pass. Every RGB comparison is within one
8-bit code (acceptance bound remains three); every native destination-alpha
comparison is exact. All nine GPU-used bloom shader hashes match both staged
compilation batches. The second staging batch only closes the durable GPU
promotion-record tooling; it does not change shader bytecode or require another
GPU run. The [existing canonical GPU summary](../../verification/results/bottle-X3/bloom-pass-summary.json)
retains the full result; its identical raw copy stays at
`/tmp/x3-bloom-authored-pass/summary.json`. The compact result below references
that record and keeps only aggregate correctness metrics, not another image or
readback/input inventory.

A timing-only reuse fix makes the standalone fixture entry configurable without
macro-renaming `main` throughout included headers. Removing the three-line entry
macro definition and expanding its default declaration reconstructs the exact
GPU-qualified fixture source hash `8fa88bf4218e8c70ee6261700337f3230df3f53fdf3c489646680a856d96edb6`.
The timing EXE cross-compiles with the project strict x86/SSE2/stack flags at
`/tmp/x3-bloom-authored-timing-build/bloom_pass_timing_fixture.exe`.
This fixture-only entry change does not change the qualified corpus behavior.


The [compact X3 result](../../verification/results/bottle-X3/bloom-authored-glow.json)
binds shader identities, correctness, raw timing paths and the following paired
measurements. The device advertises 512 PS3 slots; all eight old/new attachment
records succeed. Each group has three alternating warmup pairs and five measured
pairs; values below are milliseconds.

| Size / measured span | Old median | New median | Median paired difference |
|---|---:|---:|---:|
| 1280×768 complete prepare+commit | 1.0020 | 1.1738 | +0.2299 |
| 1280×768 first extraction draw | 0.3138 | 0.3235 | +0.0075 |
| 1279×767 complete prepare+commit | 1.0906 | 1.0140 | −0.0766 |
| 1279×767 first extraction draw | 0.3276 | 0.4342 | +0.1116 |

The generic extraction pays a measurable cost in this small sample, consistent
with its additional per-source-sample arithmetic. Full-pass paired differences
range from −0.2151 to +0.5374 ms for even dimensions and −0.4583 to +0.0397 ms
for odd dimensions. That overlap is measurement variability, not evidence that
the corrected full pass is faster. These completion-wall timings include query,
CPU and driver work and do not establish game FPS. The measured source change
adds no avoidable resource/pass/readback/lookup loop, and this result does not
justify repeating the benchmark without a new suspected hotspot.

The raw timing run was launched by root before the optional strict runner was
ready. Its retained output passes the same parser; all 24 CSO hashes still match
the passed/stable staging record. The EXE was frozen after its successful build,
but no EXE hash was taken before that run: the compact record explicitly labels
its EXE/source/parser hashes as after-run snapshots rather than claiming an
atomic before/after guard. The maintained runner now provides that guard,
including validation and hashing of the stage record, for future invocations.
No timing rerun was made solely to improve this provenance detail.
