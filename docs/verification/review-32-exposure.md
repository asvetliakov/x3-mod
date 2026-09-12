# Review 32: space-aware exposure meter

Independent source review on 2026-09-13 of branch
`worktree-agent-aea55d854948bd6a0`, starting at `ee7d2dc`, plus the owner's
review fixes. **Verdict: PASS for the branch checkpoint**, including the
Steam and X3 fixture suites and performance review. The orchestrator owns
merge, integration and installation. The space-aware policy still requires
the user's in-game visual acceptance; native Windows has not been tested.

## Findings and fixes

1. **Failed readback unlock still advanced adaptation.**
   `HdrPass::begin_frame` discarded `UnlockRect`'s result after copying the
   tile image. The frame could report a successful readback and publish a
   new exposure state despite an API failure. The fix preserves the HRESULT
   and steps only after complete success. The seam injects a returned failure
   after the real unlock, preserving mapping cleanup; the fixture checks the
   unchanged state and next-frame recovery. The meter attach self-test now
   also preserves its unlock failure in the capability verdict.
2. **Invalid meter settings could disable safeguards.**
   The six new environment controls accepted `wcstof`'s zero result for
   malformed strings: for example `X3M_HDR_WHITE_TARGET=garbage` disabled
   the highlight limit. Oversized environment values were not rejected
   before reusing the shared buffer. The fix rejects missing/truncated
   storage, incomplete conversion, non-finite and out-of-range values,
   retaining the previous setting; an explicit valid zero remains usable.
   This is configuration-time work, with no additional draw/frame cost.

Both production corrections were inspected independently, including the
attach-failure seam (`MeterTestUnlock=16`) and the frame failure/recovery
seam (`ReadbackUnlock=15`). The parser test exercises 21 controls in each
of its release and ASan/UBSan builds, including stale short storage with an
oversized getter return. Both unlock seams pass in the completed Steam and
X3 suites below.

## Correctness and portability inspection

- The GPU reduction carries separate mean and maximum log-luminance
  channels. Level zero always reduces once; subsequent levels stop with
  both axes at most 128. The host reference includes odd dimensions and
  duplicates edge taps consistently with the point/clamp shaders. Tile
  means and tile maxima serve different purposes and remain separate in
  readback and percentile selection.
- The statistic excludes dark tiles from the key calculation, uses a
  weighted median of lit tiles, and uses an unweighted lit fraction and
  percentile of tile maxima. Non-finite tile values become the meter floor
  before sorting/selection. The dead band is against the held target;
  adaptation continues towards that held target without a residual
  convergence error from reapplying the band against adapted EV.
- The highlight limit constrains the fresh target, not every displayed
  pixel: tile maxima are clipped for metering, the top percentile may be
  excluded, and adaptation, the dead band and EV bounds can exceed that
  limit. The updated architecture notes state these limitations. Actual
  space-scene tuning still needs the user's capture; fixture policy
  agreement does not establish its visual quality.
- The previous frame's ring image is copied before advancing the slot for
  the next meter. A failed candidate is not published. Target/chain release
  clears pending slots; a size change resets adaptation. Partial chain
  allocation releases resources and demotes the meter.
- The chain uses documented D3D9 float render-target/sampling capability
  checks, `GetRenderTargetData`, and system-memory `LockRect`; it prefers
  G32R32F with an RGBA32F capability fallback. No Wine-private export,
  layout, backend lock or DLL hash is introduced. A declaration/program
  mismatch during the eventual main merge must still be excluded by the
  integration suite. Native Windows has **not** been run.
- Host storage has a maximum of 16,384 tiles: three float arrays and one
  two-float scratch array, 320 KiB per meter at peak. Allocation and cosine
  weights occur when chain dimensions require them. The frame operation
  uses the cached arrays with no new allocation or production mutex.

## Performance inspection

The dense lit-tile sort is the main host statistic cost. An independent
standalone native-host C++17 `-O2` benchmark called the production
`meter_statistics` 300 times per case with deterministic varied values
and precomputed weights. These are diagnostic native-host CPU timings,
**not** Wine/FEX timings, GPU timings or game FPS:

| Tile image | 10% lit | All tiles lit |
| --- | ---: | ---: |
| 80×23 | 3.0 µs | 14.9 µs |
| 80×48 | 6.7 µs | 34.5 µs |
| 128×128 (maximum) | 35.1 µs | 568.4 µs |

The varied test values used a 32-bit LCG seeded with 123, multiplier
1,664,525 and increment 1,013,904,223. Lit log means were
`-8 + (state % 10000) / 1000`, dark tiles were -13.2877, and each maximum
was its mean plus 2. Sparse cases lit every tenth tile. This benchmark
does not include GPU download; uniform fixture images can understate
sorting cost. The implementation owner has been asked to include dense
coverage in the Wine performance assessment. Keep the bounded sort unless
target measurements justify a more complex weighted-selection algorithm.

The owner subsequently added a permanent original-data benchmark,
`verification/probe/exposure_statistics_benchmark.cpp` and
`run_exposure_statistics.py`. Independent review found no new blocker:
data, allocation and weights are outside timing; result comparisons remain
observable; source/compiler/executable provenance is checked across the
run; failures leave `passed=false`; and the runner requires exactly six
cases and one successful terminal. Run its Wine mode through `wine_lock.py`.
Its recorded host summary, `verification/results/exposure-statistics-host.json`,
passes all six cases and matches the current source hashes. Dense median
times are 37.25, 14.44 and 466.08 µs for 80×48, 80×23 and 128×128 respectively.
These differ from the exploratory benchmark above because the permanent
workload uses a different original data distribution and 41 batches of
40 calls after 20 warmups. Its p95 is of batch-average cost, not individual
frame latency. The completed target-runtime measurements are below.
The standalone runner was then reviewed with separate `native-host` and
`bottle-<name>` executable directories: a later MinGW build for another
bottle can no longer overwrite the binary required for an earlier hash
audit. Workload and compiler flags are unchanged. The refreshed host and
both target summaries pass, and their source/compiler/executable/output
hashes independently match the retained files.

Final dense median / p95 of batch-average statistic cost:

| Tile image | Steam (µs median) | X3/FEX (µs median / p95) |
| --- | ---: | ---: |
| 80×48 | 42.900 | 43.250 / 59.500 |
| 80×23 | 19.175 | 16.475 / 19.650 |
| 128×128 | 427.775 | 448.800 / 486.575 |

The maximum-size dense case is materially more expensive than sparse
space scenes, but remains bounded below half a millisecond at median in
this diagnostic. Keep the current sort; these measurements do not justify
adding a more complex selection algorithm at this checkpoint. The GPU
fixture's HDR+AgX+TAA boundary medians are 1.202/2.361 ms on Steam and
1.429/2.897 ms on X3 at 1280×768/5120×1440. These are synchronized synthetic
boundary costs, not live FPS or an isolated exposure increment. The
2/3-level chain uses 614,400/3,975,680 bytes at those sizes, and has no
additional per-draw allocation or CPU statistic work in game draws.

## Verification and verdict

Both full motion-output summaries report **PASS, 98 cases plus 16 benchmark
invocations** on each of Steam and X3 (26 benchmark dictionary entries
include ten derived comparisons). The independent reviewer verified all
196 saved case DLL, executable and trace hashes, plus the current source
maps of both full suites and their supporting suites. After both suites
completed, only the module docstring of `exposure_reference.py` changed
to correct claims about highlight and edge influence guarantees. The
reviewer independently verified its before/after hashes and identical
executable AST after removing the module docstring against
`verification/results/exposure-reference-doc-only.json`. This documented
source-provenance difference does not change the numerical reference and
does not require repeating GPU cases for wording alone.

The motion runner's recorded source map does **not** cover six imported
numerical/reference/helper modules. The final-checks artifact explicitly
records their post-run hashes; the reviewer matched all six to the current
files, but this is not retrospective before/after proof. Branch checkpoint
approval retains that limitation. Root's main integration run must include
those imports in its manifest; the existing suite maps have not been
rewritten to imply coverage they did not have.

**Targeted Steam verification now passes:** three exposure variants each
complete 120 frames / 245 checks; the returned-unlock failure/recovery
case completes nine frames / 59 checks; and attach-unlock refusal completes
three frames / 23 checks. The independent reviewer checked all five saved
trace/DLL/executable hashes and their current source map against
`verification/results/motion-output-partial.json`. The summary correctly
remains `status=PARTIAL`, `passed=false`; these are five validated cases,
not the full-suite verdict.

Target arithmetic maximum error is 4.82e-6 EV; replayed adaptation maximum
error is 2.94e-6 EV; dead-band replay error is zero; presented exposure
RGB error is below 0.568 code. The failure trace confirms frame 2 returns
`80004005`, holds step count 1 and EV 0.07842, then frame 3 succeeds,
advances to step 2 and EV 0.15377. The attach failure leaves
`tonemap=1/ok`, `meter=0/self_test`, with presented reference errors below
0.5 code and exact alpha. The same controls pass in the full Steam and X3
suites.

The first resumed Steam exposure case stopped at frame 110 on the
fixture's uniformity assertion. The emitter patch's centre was covered by
the later central-object patch, so the fixture sampled the wrong region
as its emitter reference. The independently reviewed correction determines
the last covering rectangle geometrically, picks one visible witness per
region, requires every region to be visible, and compares every pixel with
that region's witness. The independent Python expected RGB/alpha checks
remain intact. The emitter scene has 1,536 background, 1,536 emitter and
1,024 central-object pixels. This is a fixture correction, with no
production change or relaxed numeric tolerance; the passing fresh runs
above include it.

That repaired fixture completed 120 frames. Offline validation then exposed
a second fixture-control error: a six-decimal held target was compared with
a five-decimal fresh target at a tighter-than-print-precision 1e-6
tolerance. The reviewed correction compares the same-record five-decimal
held/fresh fields for equality while retaining the six-decimal movement
greater than the dead band. Independent per-frame target, dead-band and
adaptation checks are unchanged. Offline validation and the fresh full
runs passed with the corrected validator provenance.

The default exposure run subsequently passed, while the +1 EV offset twin
revealed a stimulus problem: old level A (engine value 0.35) remained within
0.25 EV of the preceding held +2 clamp, so it never established the intended
new held target. Levels A/B/C were changed to 0.38/0.40/0.37, retaining
D=0.6. Independent arithmetic checks, including FP16 rounding and both
offsets, confirm A leaves the preceding band, B/C stay within A's band,
and D leaves it. This preserves the original assertions and production
policy; it corrects what the fixture actually exercises. Both variants
passed fresh runs with the revised stimulus.
The final reviewed validator also derives these stimulus margins from the
original scene images and case parameters, then requires the measured
initial-A transition to leave the previous held target and adopt its fresh
target. This prevents an offset/clamp combination from silently removing
the intended test transition.

Supporting suites pass on both bottles: temporal pass 386 samples /
278 restorations / two generations; scene capture 4,908 checks / 16 samples /
36 scenarios; ownership integration 26 environments. Generator `--check`
passes all ten authored programs, including 1,996-word level zero and
462-word reduction shaders; the reviewer inspected its retained result.
The owner's host result is 814 analysis tests passing, including the parser
negative controls; the production build and fixture build pass, and the
final no-x87 audit reports 195 reachable functions and zero violations. The
reviewer inspected the reported host log/audit and the new test source.
Durable evidence is `verification/results/exposure-final-checks.json`:
the final DLL SHA-256 is
`80d11b3e733038347927a0ea5dd6594b4031b07107e0f5a60feb60f9cc8716f4`.
The reviewer independently matched that DLL and all 32 generator/source/
header/manifest artifact hashes recorded there.
No source or fixture blocker remains. No game was launched and no Wine command was
executed by this reviewer. No merge, install or gameplay acceptance is
claimed here.
