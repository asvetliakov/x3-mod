# Application admission integration review

The proxy now accounts for ordinary application entrypoints through a single
process monitor before capture locks, ownership lookup, native dispatch and
output publication. It remains off by default (`X3M_ADMISSION=1` selects it once).
Live motion replay remains disabled: counted entrypoints alone do not prove
complete callback coverage or exclude mappings that stay open between API calls.

## Source and emitted code

Independent review covered the process getter, all eleven loader exports,
capture/loading scopes, generated ownership methods, and their build inputs.
The 297 emitted ownership method bodies contain admission entry/retirement and
no compiler exception bookends. Only the ABI adapter translation unit disables
exceptions globally for that file; generated forwarding definitions have a
separate scoped option. Handwritten helpers and the remaining production files
retain their exception policy. Ordinary return is the supported transport
contract; exceptional unwinding across the shells is not certified.

Review checked that private-IUnknown and shared-resource vetoes precede native
dispatch, foreign routing records its veto outside the registry mutex, and native
results remain unchanged. Final child Release retires its ticket before entering
the parent's application Release. Factory teardown can occur inside an outer
device transaction, so the final telemetry distinguishes those phases and checks
the subsequent device retirement instead of falsely requiring every nested
factory snapshot to be quiescent.

The process getter publishes one immutable pointer with acquire/release ordering.
Its cold Win32 configuration preserves CPU state; its hot path avoids Win32
calls. Review corrected the fixture runner to invalidate a previous PASS before
building, and limited the concurrency wording: eight simultaneous first-use
attempts do not prove that all eight execute the cold branch.

## Verification scope

The [ownership fixture](ownership-admission.md) passes 147 checks across twelve
fresh processes, with exact artifact and emitted-method hash verification. It
exercises actual callback registration/retirement for all seven resource
families, failed registrations, blocked dispatch, publication under admission,
and final child/parent handoff. Its fourteen timing samples use a fixed native
spy; they exclude real backend and outer capture work.

[Process configuration](process-admission.md),
[actual proxy integration](../../verification/probe/ownership_integration.md)
and [capture/loading hooks](proxy-application-admission.md) document their own
current-source evidence. Earlier standalone ABI timings at `127c3da` remain
historical evidence and are not relabeled as whole-hook cost.

All 26 actual-DLL cases and the current verifier pass with 23 linked production
objects. There are 24 complete native Clear argument/CPU-state witnesses:
twelve successful calls and twelve native-invalid-call results. The forced
fallback passes with twenty linked objects and the expected native escape veto.
Seventeen host parser controls cover admission and finite-observer expectations.
The production DLL is
`5a5f8a78d7c9a802d844368c7a68572c009edd1272b03e8dab306e6bcda39007`;
the fallback is
`1f6c966e920a24104b67aed7b366c669b53af9f9576f3d00addb3795bd816693`.
Neither is installed. Build, linked-object and case/report provenance is retained
in the integration manifests and `admission-integration-artifact-audit.json`.

The integration verifier now requires permanent veto bits and first reason to
remain stable, native factory escapes to record `UnobservedRoute`, and the final
serial outer transaction to retire. An unrelated stale finite-observer check
was corrected to require the new fixed 8 KiB empty-owner index exactly; it does
not accept arbitrary metadata growth. Legacy fixture environments explicitly
disable admission instead of inheriting the host's setting.

Performance review also required the benchmark to validate its loaded DLL path
and exact ownership mode, rejecting a silent native fallback. Otherwise a fast
native path could be mislabeled as wrapped overhead. The same four original
workloads run against retained and current DLLs; their checks are included in
the timed loop consistently. These measurements exclude replay draws and cannot
estimate GPU replay cost or gameplay frame time.

The final [actual-DLL benchmark](hook-admission-performance.md) passes seven
cases and 196 timing samples. With ownership enabled, admission adds about
134/137 ns to the tested getter/setter and 316/285 ns to the rejected
render-target/Clear calls that also cross capture. These are differences between
separate medians for the same workload, including native calls and fixture
validation. The retained old DLL separately exposes changes outside admission.
No game frame budget is inferred.

The loading hooks pass 75 import and 123 native mesh checks in each of explicit
admission off/on modes. The cache passes 12,905 checks across six cases in each
mode. Enabled witnesses confirm counted activity and balanced retirement;
disabled witnesses confirm zero activity. All sixteen loading/cache modes have
separate source-bound reports. Independent final review accepted these results,
the seven-case benchmark, ownership fixture, process getter and combined DLL
artifacts with no remaining scoped findings.

## Remaining limits

No installation, game launch or native-Windows execution is part of this
checkpoint. Source and tests use public Windows/D3D APIs, without runtime DLL
allowlists or Wine-private locks/layouts. The installed iteration-5 DLL remains
unchanged. No HDR, TAA, lighting or presentation feature is enabled by this work.

Before live replay, finish window/callback coverage, explicit authority for
trusted native helpers, validation of outstanding mappings, preparation outside
the exclusive interval, state restoration and deferred object retirement. The
game's private functions remain available for validated hooks and disassembly.
A clean admission snapshot is not sufficient permission to submit replay draws.
