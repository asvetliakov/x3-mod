# Observed execution-state prerequisite

The `ObservedExecutionState` core records whether application
scene, state-block and query scopes permit injected replay. It does not render,
read GPU data, retain COM objects or prove geometry eligibility. The separate
`Options.track_execution_state` option defaults off. Tracking starts only with
successful creation of a pristine wrapped device; it cannot adopt an already
running native device. `get_execution_view` returning `S_OK` means a recognized
wrapper: consumers must additionally require `view.known`, no state-block
recording, and `view.queries_idle`. A known closed scene is allowed when the
renderer opens and closes its own scene. The live loader switch remains off.

Every observer mutation and snapshot uses the same short internal mutex. An
`ExecutionObservation` ticket marks a native scene/state-block/query/Reset call
in flight, including output adoption and cleanup. No observer mutex covers native
calls or COM Release. Snapshots return unknown with `NativeCallInFlight` while a
ticket remains live. Overlapping tickets permanently taint the device: reversed
native completion order cannot be mistaken for reliable operation order. Normal
completion must be explicitly acknowledged; an abandoned or exception-unwound
ticket permanently taints the observation. A loss observed after Reset begins
cannot be erased by that Reset subsequently returning success.

The per-query owner pointer is atomic and never changes during token lifetime.
A foreign observer rejects it before touching any non-atomic query fields. Those
fields are read and written only under their actual owner's mutex. Query/device
object lifetimes still require valid caller references. The separate legacy
recording-state flag is atomic; this does not make every other device/resource
field or arbitrary application D3D9 operation concurrently safe.

Successful observed BeginScene/EndScene and BeginStateBlock/EndStateBlock update
scope state. Failed or contradictory transitions make the entire view unknown
until a successful observed Reset. Recorded state-block Apply/Capture do not
change those execution scopes. Unknown, native and wrong-kind pointers are not
guessed to be wrappers. The getter does not issue native calls.

Each canonical wrapped occlusion query has a noncopyable token. Its successful
BEGIN opens an interval and successful END closes it; GPU completion/GetData is
not required to close the measured draw interval. Multiple overlapping queries
are counted. Failed, invalid or contradictory Issue, unknown tokens, and release
of an active query permanently invalidate query knowledge for that device.
Successful-looking Issue after an observed device failure cannot heal an interval:
lost devices may report success without executing the operation. An active query
at Reset, query ambiguity, or explicit native bypass remains unknown after Reset.
Idle query tokens can cross a successful Reset and synchronize to its generation.

A real query of any other type permanently disables this initial replay policy,
including EVENT, TIMESTAMP, TIMESTAMPDISJOINT, TIMESTAMPFREQ and diagnostic queries.
Null-output capability probes do not create a query. This deliberately avoids
claiming that an END-only timestamp means no timing observation spans the injected
work: separately issued timestamp objects can form an application-defined pair.
No undocumented type receives an idle default. This is conservative eligibility,
not an assertion that all these query types require BEGIN/END.

The ownership forwarders observe loss from HRESULT methods, including query
Issue/GetData, ordinary setters/getters and custom output/container/private-data/
ProcessVertices paths. Output cleanup/adoption happens before full loss retirement.
Device loss retires existing depth snapshots, geometry leases, finite evidence and
renderer-owned resources through the established ownership path. Query intervals
are observations, not native resources or additional application references.

The native device/resource bridge remains a **trusted closed-world boundary**.
All application scene, recording and query calls must pass through these wrappers;
no D3D9 getter discovers unobserved native query intervals. Native renderer work
must preserve execution scopes. The snapshot is **not a replay exclusion lease**. The renderer still needs to
exclude concurrent application work throughout validation and replay, and report
uncertain injected execution/restoration using
`invalidate_execution_state`; that permanent taint survives successful Reset.
This API does not automatically detect arbitrary native callers or hostile vtable
replacement. It therefore cannot establish safety for an uncooperative extension.

## Verification

`python3 verification/probe/run_execution_state.py` fresh-builds optimized,
ASan/UBSan and ThreadSanitizer host fixtures. All pass **61,408 checks** with no
sanitizer diagnostics, including 10,000 deterministic
legal interleaved intervals against a separate active-query model, strict failure
and contradictory transitions, all non-occlusion types, ignored capability-probe
semantics, cross-owner tokens, loss before/within an interval, reset generations,
disabled tracking and permanent native invalidation. Deterministic thread gates
verify unavailable snapshots during an unfinished native ticket, reversed
completion order, concurrent Reset/loss, exception/abandoned tickets, and parallel
proper/foreign query token users. These are CPU state-machine
checks; they are not native query semantics evidence. The implementation uses only
integer arithmetic and contains no floating-point arithmetic.

`verification/probe/execution_ownership_fixture.cpp` separately exercises actual
normal and pure native devices through the production wrappers, ordinary scope
transitions, real occlusion/event objects, Reset and explicit invalidation. Its
per-instance native HRESULT injections distinguish ordinary `E_FAIL` from
DEVICELOST/DEVICENOTRESET in typed/base outputs, GetContainer, buffer private-data
and ProcessVertices, checking native result preservation plus actual depth snapshot
and renderer resource retirement. The fresh Windows executable run on Preview passes **184 checks** across the
exact 23 labeled cases, plus unlabeled default-disabled and real-event-query
controls. Win32 event barriers pause injected native BeginScene, BeginStateBlock
and query Issue callbacks. An independent reader must finish before the barrier
opens, proving no observer mutex remains held across native dispatch. A second
native callback completes in reverse order; both successful HRESULTs return
unchanged while the view remains unknown, including after actual Reset. These
controlled callback interleavings test wrapper bookkeeping, not an assertion that
arbitrary concurrent application D3D9 calls are valid. Failed scope/query transitions do not establish known execution; ordinary
unrelated `E_FAIL` preserves existing scope knowledge. Reproduce with
`python3 verification/probe/run_execution_ownership.py`.

Enabled scene/query-creation/Reset controls verify full incoming and native
outgoing x87 environment/register payload, MXCSR and Win32 LastError through the
new observer bookkeeping and ticket destruction. Getter/invalidation preserve
caller CPU state. The disabled BeginScene control verifies one native dispatch
and exact CPU-state parity; the immutable off option bypasses new ticket/FP
instrumentation. This does **not** claim whole-method Query::Release CPU parity:
its preexisting registry/delete/parent-release work lies outside the new helper.

Production synchronization uses standard C++ mutexes/atomics and existing Win32
CPU-environment helpers, with no Wine-private APIs. Host sanitizer execution was
on macOS; the x86 Windows executable ran on Preview. Native Windows execution
has not been performed, so these results are not native-Windows validation.

## Bounded dispatch cost

The retained optimized host run took 1,574,125 ns for one million successful
HRESULT observations, which bypass locking, and 273,708 ns for 10,000 completed
empty tickets. A Preview run of 1,000 empty scene pairs measured 828 native ticks
versus 6,032 observed ticks at 10,000,000 ticks/second: approximately 0.52 microseconds
added per BeginScene/EndScene pair. These tiny CPU dispatch measurements are not
a game frame-time prediction or a Windows performance result. The off option
avoids this new observation cost; rendering still remains disabled.

Both runners replace retained success with a false record before any input read,
bound compilation/execution time, require exactly one terminal PASS, and verify
source/artifact hashes before and after execution. The native runner also checks
game absence before compilation and immediately before execution. Actual local
d3d9/wined3d hashes are recorded before and after; verification requires the
observed files to remain stable during the test, with no expected DLL version or
digest allowlist. Missing provenance files are recorded as unavailable. Build products stay untracked;
only derived reports and hashes are retained. No game launch or installation is
part of this verification. The installed game still has no TAA enabled.
