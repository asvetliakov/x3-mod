# Collision query phases: verification ledger

`--collide-query-phases` / `X3M_COLLIDE_QUERY_PHASES=1` is an opt-in attribution
probe. It implies the existing memo; an explicit `--no-collide-memo` conflicts.
The engine contract and unresolved live workload are in
[sector-collide §14.10](../reverse-engineering/sector-collide.md#1410-moving-case-audit-the-remaining-cost-is-not-yet-attributed-2026-09-20).
It changes no collision answer or tree and stores no model pointers.

The memo's diagnostic thunk times miss, verify and ineligible engine dispatch
from after key/classification to store entry, before output reads. A separate
five-byte root-call redirect at `0x004e2956` brackets `0x004e2530`; none of the
four recursive calls is patched. An owner/busy gate protects each saved return
slot; the engine executes on its exact original stack. This preserves even
volatile ECX/EDX values pointing into descent locals. Foreign/reentered calls
tail-forward without modifying either slot and still execute the engine.

Each helper boundary saves flags, all GP registers, XMM0–7, x87 state, MXCSR
and LastError. FNSAVE/FRSTOR follows the project's qualified transport. Before
any FP control write, the thunk compares x87 and SSE rounding modes. Divergent
modes bypass instrumentation, preserve the engine's effective rounding and
invalidate the frame (`discard_cpu_mode`). This portable guard avoids the known
FEX shared-rounding hazard (§12.8); it uses no backend-private API. Equal-mode
helpers use integer arithmetic, documented QPC and thread APIs, with no allocation,
log or lock wait. The default memo thunk remains unchanged; disabled diagnostics
add predictable enabled tests on miss/store and exception paths, no clock or
additional site claim. Cache hits read no clock.

One row per 300 owner-thread Present frames carries `qpc`, `device`,
`first_frame`, `frame`, mode, valid/invalid frames, miss/verify/ineligible counts,
final engine visits/triangles/contacts, reason-specific discards, same-frame
query/descent/difference sums and p50/p95. Any invalid interval excludes that
entire frame's costs from sums and percentiles; counters still disclose attempts.
Reset, reentry, foreign work and abandoned intervals discard timing without
suppressing engine work. Empty frames are valid zero-cost samples.
`clock_self_ns` is the startup median of 129 consecutive QPC-pair deltas;
no self-cost is subtracted. The fixture measures full added bracket overhead.
These are diagnostic timings, not game FPS.

The diagnostic's frame mask/window can differ from `loop_phases` at startup or
on invalid frames. Frame IDs and timestamps disclose that difference; subtracting
independent window percentiles cannot establish a whole-collide residual. The
query-minus-descent metric is reduced from the same samples within this probe.

| Date | Check | Result |
| --- | --- | --- |
| 2026-09-20 | `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_collide_query_phases verification.analysis.test_collide_memo` | 12 tests passed. Accumulator faults, invalid-frame exclusion, paired quantiles, launcher dependency/inherited environment, log/provenance acceptance and existing memo checks. |
| 2026-09-20 | `python3 verification/probe/verify_collide_query_phases.py` | 13/13: whole call/windows, sole external plus four recursive targets, no interior references, five-word caller cleanup, pinned engine bodies and production windows. |
| 2026-09-20 | `python3 verification/probe/build_collide_query_phases.py` | i686 cross-compilation with SSE2/four-byte incoming-stack flags and `-Werror`; full GP/XMM/flags/FP envelope audit; original memo thunk still 28 instructions and memo module has no x87/MMX. Runtime qualification is a separate owner-run step. |

The dedicated fixture builds a genuinely unpatched reference by copying and
rebasing the caller, dispatch and query setup; its root call targets the original
descent directly. It tests four stack alignments, equal and divergent rounding
modes with live x87/XMM state and actual x87 division, moving queries, recursive
and contact cases, verify mode, real nested memo/root calls, clock faults, Reset,
foreign/unwind rejection, claim refusal and failed rollback/retry. All extracted
engine bytes remain untracked under `build/verification/`.

Owner-run command (no game):

```sh
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
  python3 verification/probe/run_collide_query_phases.py
```

The runner records bottle X3, WineArch and the two emulation environment lines
with its compact result in `verification/results/bottle-X3/collide-query-phases.json`.
Native Windows runtime and flight attribution remain unverified. This ledger
does not describe an installed build.

The first runtime qualification rejected argument repushing: 53 deep-query
comparisons differed only in ECX/EDX, each pointing 28 bytes lower on the shifted
stack. Results, preserved registers, x87/MXCSR, both global blocks, minimum and
LastError matched. A second compact witness confirmed the mechanism. The fix
uses guarded return substitution to preserve the original engine stack; parity
expectations were retained. Rejected records remain local as
`collide-query-phases-first-rejected.*` and `collide-query-phases-stack-witness.*`.

Initial exact-stack runtime pass (X3, arm64, `FEX_X87REDUCEDPRECISION=1`,
`WINEMSYNC=1`): 142 checks, 675 paired queries, zero failures/differences; two
300-frame windows, each with 300 valid frames and zero discards. First passing
benchmark: 322.5 ns/query with diagnostics off, 1,807.7 ns on, **+1,485.2 ns**,
median of seven 10,000-query trials per mode. QPC calibration: 100 ns. This is
about 0.28 ms of instrumentation at 187 executed queries/frame, an extrapolation
from the fixture rather than a flight measurement. Keep this first measurement;
subsequent acceptance runs need not retune or chase its variation.

That initial pass used `--no-build` without a pre-run executable hash and is not
the final artifact-bound acceptance record. The final runner builds by default,
hashes compiler-discovered fixture dependencies before/after compilation, records
the exact compiler/link commands and engine input hash, and binds the executable
before/after execution plus raw stdout SHA-256. `--no-build` requires this matching
build record and refuses changed inputs/artifacts. The owner completed that bound qualification on the integrated sources: **142 checks,
675 paired queries, zero failures/differences**, natural exit 0 in 5.723 s. All 22
compiler-discovered inputs, six build commands, executable pre/post/current hashes
and stdout hash were independently verified. The repeat measured 320.1 ns off,
1,744.8 ns on (**+1,424.8 ns/query**); this variation does not retune the probe.
The combined claim scan covers 152 claims including both R7 sites and finds no
collision-root overlap. Native Windows and flight attribution remain open.
