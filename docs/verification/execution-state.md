# Observed execution-state prerequisite

The allocation-free `ObservedExecutionState` core records whether application
scene, state-block and query scopes permit injected replay. It does not render,
read GPU data, retain COM objects or prove geometry eligibility. The separate
`Options.track_execution_state` option defaults off. Tracking starts only with
successful creation of a pristine wrapped device; it cannot adopt an already
running native device. `get_execution_view` returning `S_OK` means a recognized
wrapper: consumers must additionally require `view.known`, no state-block
recording, and `view.queries_idle`. A known closed scene is allowed when the
renderer opens and closes its own scene.

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
must preserve execution scopes. The owner must serialize application calls, view
acquisition and replay, and report uncertain injected execution/restoration using
`invalidate_execution_state`; that permanent taint survives successful Reset.
This API does not automatically detect arbitrary native callers or hostile vtable
replacement. It therefore cannot establish safety for an uncooperative extension.

## Verification

`python3 verification/probe/run_execution_state.py` fresh-builds optimized and
ASan/UBSan host fixtures. Both pass **60,365 checks**, including 10,000 deterministic
legal interleaved intervals against a separate active-query model, strict failure
and contradictory transitions, all non-occlusion types, ignored capability-probe
semantics, cross-owner tokens, loss before/within an interval, reset generations,
disabled tracking and permanent native invalidation. These are CPU state-machine
checks; they are not native query semantics evidence. The implementation uses only
integer arithmetic and contains no floating-point arithmetic.

`verification/probe/execution_ownership_fixture.cpp` separately exercises actual
normal and pure native devices through the production wrappers, ordinary scope
transitions, real occlusion/event objects, Reset and explicit invalidation. Its
per-instance native HRESULT injections distinguish ordinary `E_FAIL` from
DEVICELOST/DEVICENOTRESET in typed/base outputs, GetContainer, buffer private-data
and ProcessVertices, checking native result preservation plus actual depth snapshot
and renderer resource retirement. The fresh native run passes **132 checks** across the exact 17 labeled
normal/pure scope and injected-failure cases, plus unlabeled default-disabled
and real-event-query controls. Failed scope/query transitions do not establish known execution; ordinary
unrelated `E_FAIL` preserves existing scope knowledge. Reproduce with
`python3 verification/probe/run_execution_ownership.py`.

Both runners replace retained success with a false record before any input read,
bound compilation/execution time, require exactly one terminal PASS, and verify
source/artifact hashes before and after execution. The native runner also checks
game absence before compilation and immediately before execution, and pins both
local d3d9/wined3d file hashes to the reviewed Preview runtime. Build products stay untracked;
only derived reports and hashes are retained. No game launch or installation is
part of this verification. The installed game still has no TAA enabled.
