# Finite upload evidence review

Historical checkpoint `c4f3d45`. Shared native/DLL result paths are subsequently
refreshed by [review 7](review-07.md); use that commit to inspect the original
18-case source and evidence described below.

This source checkpoint prepares the next consolidated diagnostic build. The
installed iteration-5 DLL remains unchanged; no gameplay launch or visual
HDR/TAA change is part of this work. The classification core, native mapping
qualifier and allocation observer have passed independent review. The broader
regressions, combined DLL matrix and forced native fallback all pass.

## Classification and publication

The finite-buffer core stores one nibble per aligned four-byte vertex cell and
whole-allocation index extrema. It classifies exponent bits with integer
operations, retains no payload pointer or copy, and publishes only after the
owner confirms successful native Unlock. Unknown cells, pending writes, failed
publication, stale revisions and incomplete index uploads cannot attest a range.

Independent review found an unsigned counter underflow for a tiny unaligned
upload containing no complete cell; the counter now advances only for a positive
cell interval. Review also required clearing untouched evidence after skipped
revision notifications. An independent byte/cell oracle passed 640,000 randomized
comparisons. The author's portable fixture passes 214,651 checks both normally
and under AddressSanitizer/UndefinedBehaviorSanitizer. Native mapping qualification,
allocation lifetime and global memory budgets are separate observer obligations.

## Draw acquisition and diagnostics

The reader qualifies the actual submitted VS before reusing its scratch storage
for the PS. Finite XYZ and index certificates remain separate from the existing
local proof mask. Indexed draws require observed extrema inside the widened API
min/count interval; the finite query conservatively covers that full interval
after signed base addition and actual VB size checks. Revisions and nonzero
owner generations must match. Nonindexed draws ignore the bound IB.

Independent review found no issue in this reader/loader/capture/launcher delta.
The code passes Win32 warning-as-error syntax checks. The full Python analysis
suite passes 279 tests, including a new scoped `motion_geometry` preservation
test. Mocked launcher checks verify both required switches and explicit clearing
of an inherited finite-position environment setting. The extended native reader
passes 219 checks and 63 caller-state comparisons, including the original seven
getter-failure controls. The final current-source reader run and all retained
hashes were independently checked. The combined DLL matrix also passes.

`motion_geometry` exposes independent source, finite and index evidence;
batched counters measure classification and report refusal reasons. A failed
draw may retain its pre-draw finite fact, but cannot acquire the separate
successful-submission proof. Deferred replay still needs revision revalidation,
object/camera correspondence and complete scene coverage.

## Native-state preservation finding

A strengthened qualifier control with nondefault FP settings and live x87 stack
values found that the initial FXSAVE/FXRSTOR guard did not preserve the tested
x87 tag state on this Preview backend. The ordinary default-state controls had
passed. The corrected qualifier uses FNSAVE with immediate FRSTOR for capture,
then FRSTOR plus LDMXCSR for restoration; these transport legacy state without
performing x87 arithmetic. Its strengthened native fixture passes 461 checks,
including nondefault computational state and a live ST sentinel. The observer's
native-versus-wrapped checks also pass. This does not establish game acceptance.

Review also identified a lock-order inversion: the registry lock could be held
across native private-data access, while native private-data destruction could
call a sidecar destructor that reacquired the registry lock. The observer now
uses nonblocking callback-time cleanup with bounded lock-free retirement and cleanup
at safe ownership entry points. The first native fixture verifies that callback
completion does not wait for the registry and that later cleanup returns the
reservation. Independent source and native-fixture review accepted the correction.

Review further found that aggregate reference-count changes cannot authenticate
the sidecar reference returned by GetPrivateData: an independently retained
IUnknown can change that count concurrently. The observer now uses a
scoped per-thread AddRef observation, with a passing deterministic concurrency control.
Borrowed-native replacement with a foreign IUnknown remains outside the mutation
contract. If encountered, the owner is permanently disabled and the returned
unknown pointer is never called; one foreign reference may remain retained in
that unsupported tamper case. The fixture verifies that repeated observations
do not grow that retention.

The final [observer fixture](../verification/finite-upload-observer.md) passes 385 checks.
Independent review verified every retained source, native-module, executable,
report and diagnostic-log hash against the final manifest. Reset/loss, failed
publication, tracker failure, budgets, allocation lifetime and state preservation
are covered within the documented boundary. No remaining observer finding is open.

## Existing behavior regressions

All affected standalone runners pass against the frozen ownership source:
buffer content 698 checks; ownership 370 native and 431 wrapped checks with
behavioral parity; copy-depth loss 357 checks across 33 cases; copied depth 634
checks and 32 numerical samples; scene capture 4,908 checks, 16 numerical samples
and 36 scenarios; loading hooks 68 plus mesh tracing 123 checks; and all six mesh
cache hook cases (12,781 checks). The draw-input run above also passes on this
final source. All retained repository source hashes match current files.

Independent runner review fixed stale derived PASS handling, current executable
hash consistency and incomplete terminal-report acceptance before these runs.
The new finite scheduling seams exist only in verification builds.

## Combined DLL and analysis acceptance

The fresh DLL passes all 18 integration cases, including finite opt-in with and
without ownership, independent revision tracking without object tracing,
unsupported-input refusal, and inherited-option clearing. Forced adoption failure
passes with all 16 production proxy/renderer objects and native unknown queries.
Production symbol inspection confirms that finite scheduling callbacks and the
synthetic rigid-program issuer are absent; the real qualifier is present.

Integration uses a distinct baseline fixture executable so its fresh build cannot
invalidate the standalone ownership proof. After separating the outputs, the
standalone ownership check and complete integration/fallback matrix were rerun;
both sets of source and binary manifests remain current.

The verified source DLL SHA-256 is
`6e21f29a57e97018753212759392ef0b79bd9fc120aa5121f30dd62f70ed3083`.
See the [build manifest](../../verification/results/ownership-integration-build.json),
[case verification](../../verification/results/ownership-integration-verification.json),
[fallback manifest](../../verification/results/ownership-integration-fallback.json)
and [symbol check](../../verification/results/ownership-integration-symbols.json).
The installed DLL was separately rehashed and remains iteration 5.

The [finite capture analyzer](../verification/finite-upload-capture.md) passes 30 focused tests.
Independent review corrected malformed primitive arguments/topology admission and
stale positive output on missing input. The complete Python analysis suite passes
279 tests. The actual completed iteration-5 snapshot supplies an additional
compatibility control: 28 complete frames, 12,957 successful draws, all new finite
evidence unknown, zero fabricated input candidates. No new user game run was
needed for this checkpoint.
