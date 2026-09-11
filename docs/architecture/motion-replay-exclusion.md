# Excluding application mutations during private replay

Portable design proposal, not an implemented synchronization guarantee. Native
Windows/Direct3D is a required full-renderer target; Wine internals cannot be a
prerequisite for shared replay or resource-evidence interfaces. See
[platform portability](platform-portability.md). The current
production capture adapter keeps `motion_live_replay_available=false` and reports
`write_exclusion_unavailable`. Its standalone GPU fixtures supply serialized
calls explicitly. They do not prove that the game meets that condition.

## Observed gaps

The capture mutex surrounds intercepted device calls. Resource Lock/Unlock,
Query::Issue and StateBlock::Apply do not acquire that mutex. Many ordinary
device forwarders also dispatch directly. A native application call can therefore
change geometry, query scopes or device state between replay validation and
submission. Holding the ownership registry during inspection does not close
the gap: `buffer_lock` drops it before native Lock and records pending/revision
only after that call returns. The application's mapped writes occur between
Lock and Unlock, outside any wrapper critical section.

The execution observer additionally requires synchronized access and an in-flight
interval covering native dispatch through result publication. The initial
implementation used plain fields with incompletely synchronized writers; the
observer checkpoint `0e361c1` adds short synchronized bookends and refuses
snapshots during unfinished transitions. See [its verification](../verification/execution-state.md).
That snapshot still does not exclude a new transition after it is read. The loader keeps
`Options::track_execution_state` false, so requesting motion does not activate
this observer before the complete replay admission contract exists. Native
component fixtures enable it explicitly, including controlled concurrent
transition tests.

Native references solve allocation lifetime only. A static usage bit, initial
revision, same thread on previous uploads, or no observed worker in one capture
does not prove exclusion for a future replay.

## Candidate admission protocol

Prefer short monitor operations and explicit admission tickets over a new mutex
held around arbitrary native calls. The initial conservative design needs a
process-wide count of supported application native work, with a device-specific
exclusive replay token. A per-device counter alone misses shared backend locks:
another device's native Release may invoke a callback into the replayed device
while holding Wine's shared lock.

An application entry obtains admission before native dispatch and before taking
the ownership registry. Its ticket remains active through native work and the
associated metadata publication. The monitor mutex is released during all of
that work. Ordinary admitted calls may nest; nested callbacks inherit an active
application transaction, so replay cannot begin partway through one. Waiting
entrants must not retain the registry, a backend lock, or an application callback
dependency needed by the replay thread.

At a boundary, the capture thread tries to enter exclusive replay. It must
refuse immediately if application work is already admitted; it must never wait
for that work while holding the capture mutex. On success it prevents new
application native dispatch, then validates the complete device state, execution
view, selected surfaces and every retained geometry lease. Any already-pending
mapping refuses replay. No lock is held across the application's Lock-to-Unlock
interval: pending-map state records that interval, and replay does not wait for
the application to finish writing it.

Native Lock admission must begin before Lock itself, closing the current
pre-publication window. Successful Unlock must publish native completion and
finite/revision metadata before its ticket retires. ProcessVertices, query
creation/Issue/destruction, stateblock operations, scene transitions, state
setters, resource/private-data mutations, Reset/loss and relevant release paths
need the same ordering. Getters and resource creation cannot simply be classified
as harmless: their native work can take backend locks or return references whose
cleanup invokes callbacks. An implementation must inventory the generated
forwarders and handwritten native dispatch sites, rather than assume the small
current capture-hook list covers them.

The exclusive scope must begin before the first injected depth-copy or state
change, and continue through replay and full restoration. If it includes the
application's original Clear, that one original dispatch needs a narrow token
permission preserving ordinary result observation. A blanket thread-local
"internal call" bypass would also admit unintended reentrant application work.
Releasing exclusivity earlier requires a separate proof that state and selected
depth cannot change before original Clear. Neither ordering is implemented yet.

## Lock ordering and retirement

The capture hook may hold the capture mutex while trying admission and while
performing an exclusive replay. The admission attempt is nonblocking in this
direction. The short monitor mutex is never held while acquiring the registry,
calling native code, invoking a diagnostic consumer, or acquiring the capture
mutex. Ownership bookkeeping may take the registry after admission, but native
resource retirement retains its existing detach-under-registry,
Release-outside-registry discipline.

A final child wrapper Release eventually calls
`parent->application->Release()`, which enters the capture Release hook. Its
application-native ticket must end after its native destruction and bookkeeping
but **before** that parent dispatch. The parent remains logically retained until
its Release, and enters the normal capture/admission path afresh. This avoids an
unexamined ticket-to-capture dependency. Final native destruction must remain
counted while callbacks can run; decrementing before native Release would
reintroduce the cross-device backend-lock race.

No application callback may be made to wait on its own exclusive replay. A
recursive mutex silently allowing it through would defeat exclusion. The
critical replay implementation must prove its native operations cannot invoke
arbitrary application callbacks, including through final references or foreign
private-IUnknown destructors. Move diagnostic consumers and callback-capable
retirement outside exclusivity, after state restoration, while retaining output
references until consumption completes. Exact pinned replay operations and
ownership of every temporary release need review before this is an admission
contract. Merely checking thread IDs when reentry happens cannot undo earlier
state changes or provide transparent native behavior.

Unobserved native entrypoints, Ex/native-fallback devices and application native
bypasses remain explicit coverage exclusions. Process-wide tickets for known
wrappers do not observe a native call that bypasses them. Any additional supported
route requires admission coverage or a positive noninterference proof. CPU
state/LastError preservation must include ticket acquisition, refusal, waiting,
bookkeeping and final teardown.

## Required deterministic controls

Before enabling live replay, verification needs controlled thread barriers for:

- Lock stopped before native return: replay refuses rather than seeing the old
  pending/revision state. A mapping held across the boundary also refuses.
- A new Lock, ProcessVertices, Query::Issue and StateBlock::Apply attempted after
  exclusive admission: no native dispatch occurs until restoration completes;
  original arguments, HRESULT, output pointers and CPU state survive.
- Existing query/scene transitions racing a snapshot: observer fields remain
  synchronized and uncertain scopes refuse replay.
- Child final Release entering parent capture Release, and another device's
  native destructor callback into the target: no lock-order cycle or premature
  admission. Native retirement remains outside the registry.
- Same-thread nested application callback during candidate native replay:
  demonstrate that the qualified critical segment cannot create it, or refuse
  that path before any injection. A timeout or recursive pass-through is not a
  successful result.
- Failed Lock/Unlock, Reset/loss, early return and native replay failure:
  admission counts, pending maps and exclusive tokens cannot become stuck or
  revive invalid evidence.

This is the next synchronization checkpoint. It does not resolve camera-cut
continuity, unsupported final-color contributions or temporal history policy.

## Backend-specific mutex research (not the shared design)

The user requires native Windows/Direct3D support as well as CrossOver. The
following Wine-specific route is research only and cannot become a prerequisite
for replay. The shared implementation must use portable application admission;
see [platform portability](platform-portability.md).

The pinned WineD3D binary already exports a process-wide recursive graphics
mutex. Its `wined3d_mutex_lock` and `wined3d_mutex_unlock` entries at RVAs
`c4260` and `c4280` pass the critical section at RVA `29a438` through imports
`29787c` and `2978f4`. The historical managed-buffer qualifier checks the D3D9
imports which lead to these exports. These addresses are findings for the exact
pinned binary, not a supported interface on arbitrary Wine releases.

A Wine-only experiment can test qualified **nonblocking** acquisition of that
native critical section. It cannot replace portable admission for the required
Windows renderer, or qualify resource memory on native Windows. Blocking on the
exported lock while holding the capture mutex would permit a callback deadlock.
After successful try-acquisition, a recursion count greater than one would mean
the thread inherited backend ownership; that boundary must refuse injection.
An already returned application buffer mapping is outside this mutex, so native
map-count and pending-write checks remain essential.

The ownership registry could then be acquired with `try_lock` for snapshot
validation and independent native-reference retention. A busy registry causes
immediate native-lock release and refusal. The registry must be released before
GPU commands and callback-capable cleanup: the backend command-stream thread can
destroy resources independently, and a callback waiting for the registry must not
block a command-stream operation awaited by the replay thread.

This route still needs proof of native mutator coverage, writes before the lock,
reentrant callbacks and resource lifetime through restoration. Execution calls
also need observation from **before** native dispatch through result publication:
otherwise a native query can finish before its wrapper updates the observer, and
the native mutex alone could expose a stale idle snapshot. The threaded observer
checkpoint addresses result publication. The native-mutex experiment was stopped
before implementation when native Windows became an explicit requirement. This
research does not justify removing the live replay gate.

## Concrete portable entry coverage

The checked-in generator currently emits 297 normal-D3D9 methods across 15
interfaces. Admission belongs at entry, before unwrapping inputs, taking the
registry, or evaluating a native call expression. Adding it only to
`observe_result` or `output` is too late: C++ evaluates the native argument first.
The generator is the authoritative mechanical inventory; handwritten helpers
remain responsible for their longer transactions and special retirement order.

| Entry family | Required interval or special case |
| --- | --- |
| All generated methods, including Factory, scalar and void methods | Before first native/registry operation through outgoing-result and output-adoption bookkeeping; not just HRESULT observation |
| QueryInterface/AddRef/GetDevice/GetContainer and COM output methods | Include registry lookup, canonical adoption, and redundant-reference cleanup; apparent getters can execute native COM callbacks |
| Release | Include native destruction and sidecar retirement, but explicitly end the child transaction before `parent->application->Release()` enters capture again |
| Capture hooks and factory/device setup | Ordinary outer admission must precede `HookGuard`/capture mutex; nested ownership dispatch inherits that admission |
| Clear replay boundary | Nonblocking promotion of its own outer admission, only when no other application root is active and the boundary is not nested inside a native callback |
| Public ownership depth-copy, finite/index query and geometry lease APIs | Their native inspection, AddRef/Release and cleanup need admission independently of generated methods; registry-only handle APIs still need consistent lifecycle ordering |
| D3DX loading and mesh hooks | Cover the outer native D3DX call and preflight/cleanup, including direct native buffers; counting only calls that happen to reach a wrapped mesh/buffer is incomplete |
| Raw native pointer APIs | Returning a borrowed pointer does not authorize uncounted mutation. Internal callers need an explicit renderer token; arbitrary application native bypass remains unsupported |

Surface/texture/cube LockRect, volume LockBox, GetDC/ReleaseDC, buffer Lock/Unlock
and ProcessVertices require separate content/mapping review. Tickets end when an
API call returns; successful mappings and DC access remain pending across calls.
The initial rigid producer needs this proof for every retained VB/IB and every
surface it reads. Extending the renderer to sample application textures extends
the required resource set. A static usage flag is not a replacement for mapping
state.

A minimal monitor can count outer application roots process-wide and keep nesting
depth in TLS. Nested calls inherit the root ticket, allowing ordinary native
callbacks during an already admitted application operation. At Clear, promotion
must require the calling root to be the only active root and the nesting depth to
match an ordinary outer boundary. Reentrant Clear inside a native callback must
refuse replay. New outer entrants wait only before capture, registry or native
locks. The monitor itself is held only for short transitions, never across calls.

This promotion requires capture entry coverage as well as wrapper coverage.
Acquiring the first ticket inside a wrapper after taking the capture mutex leaves
unreviewed lock-order edges. A normal generated RAII ticket spanning a child
Release's final parent capture dispatch also defeats the explicit handoff rule;
Release needs a dedicated helper-controlled end point.

The replay thread must not receive a general permission to call application
wrappers while exclusive. That would admit a same-thread native callback as if it
were intentional renderer work. Renderer native access and original-Clear dispatch
need narrow explicit permissions, and diagnostic callbacks belong outside the
exclusive scope.

## Remaining callback and cleanup proof

Counting completed CPU calls does not count asynchronous backend work. A native
operation may enqueue resource destruction, return, and later invoke a
private-IUnknown callback from a worker. Such a callback can arrive after the
application-root count reaches zero. Nested-ticket handling alone therefore does
not prove the critical replay segment cannot receive a callback. This must be
resolved for supported native Direct3D as well as Wine; one pinned backend's
mutex or worker behavior is not a portable proof.

The default `RigidMotionPass::run` path owns `SavedState` as a local object and
releases its native references before returning. On restoration failure these
may be the last references to application resources. The reviewed optional
`RigidMotionRetirement` now transfers its seven explicit references into a
caller-owned batch without allocation or AddRef, including failure paths. The
caller must release that batch after leaving exclusivity; MotionCapture has not
yet wired this API. See [retirement verification](../verification/rigid-retirement.md).
Geometry references, output consumption and other pass cleanup still require
explicit phase ordering. This component closes the local saved-state cleanup
placement gap; it does not eliminate callbacks from native replay operations or
establish portable replay exclusion.
