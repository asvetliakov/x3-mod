# Excluding application mutations during private replay

Design proposal, not an implemented synchronization guarantee. The current
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

The execution observer additionally requires serialized access to its plain
fields. `get_execution_view` takes the registry, but current query and scene
transition writers do not consistently take it. A concurrent snapshot cannot
be made safe merely by testing a revision afterward. The loader now also keeps
`Options::track_execution_state` false, so requesting motion does not activate
this observer through an unproven concurrent live path. Native component fixtures
enable it explicitly under their authored serialization contract.

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
