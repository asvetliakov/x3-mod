# Excluding application mutations during private replay

Portable integration contract, not an implemented live synchronization guarantee.
The [application admission core](../verification/application-admission.md)
now implements root counting, nesting, promotion and permanent veto bookkeeping;
it is now wired to ownership, loader and installed capture/loading entrypoints
behind the off-by-default `X3M_ADMISSION=1` option. See
[entry coverage and its limits](../verification/proxy-application-admission.md).
Window callbacks, raw-native helper authority, mapping validation and the
exclusive GPU segment still need integration before replay can use it. Native
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

The current handwritten entry inventory adds 30 `WINAPI` capture hooks in
`src/proxy/capture.cpp` and 11 proxy exports in `src/proxy/loader.cpp` (including
the marker macro). Capture entry must precede `HookGuard`; loader entry must
precede backend initialization. Do not acquire blocking admission in `DllMain`.
The six direct graphics D3DX hooks in `loading_trace.cpp` and three mesh-method
templates (24 installed thunks) must enter before `Span`, cache preflight and
native dispatch. Remaining installed file/cursor/codec hooks need an explicit
coverage classification before enabling replay.

Public ownership helpers need two distinct call contracts: ordinary callers
obtain application admission, while reviewed renderer operations carry the
specific active replay authority. In particular, blindly adding application
entry inside `inspect_geometry_lease`, depth inspection/copy or native-pointer
access would classify intentional replay validation as an unexpected callback.
The finite sidecar's native `IUnknown` callbacks retain their existing bounded
retirement behavior; they must not enter an application wait from inside a native
callback. This inventory is a patch boundary, not implemented coverage.

The replay thread must not receive a general permission to call application
wrappers while exclusive. That would admit a same-thread native callback as if it
were intentional renderer work. Renderer native access and original-Clear dispatch
need narrow explicit permissions, and diagnostic callbacks belong outside the
exclusive scope.

## Portable callback contract for the first live producer

A permanent registration veto can close the asynchronous private-IUnknown gap
without depending on a backend's destruction timing. This is the proposed
activation contract, not evidence that the current live gate can be removed.
The supported process uses the normal D3D9 proxy/wrapper route from graphics
startup, documented native COM implementations, and the reviewed game/D3DX
call paths. Unannounced native access and third-party COM interception are outside
that contract. The public buffer implementation may accept a legitimate COM
forwarder for storage correctness; that does not certify arbitrary code in that
forwarder as safe inside an exclusive replay.

### Documented callback mechanisms

D3D9 resource private data flagged `D3DSPD_IUNKNOWN` retains an application
interface and calls it when the entry is replaced or destroyed. Retrieving the
interface through GetPrivateData also calls AddRef. The API does not provide a
portable completion fence for an arbitrary callback's lifetime, nor enumeration
of all private-data GUIDs. Therefore a scan of a resource or an empty current
registration count cannot establish a callback-free baseline. See Microsoft's
[SetPrivateData contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dresource9-setprivatedata)
and [GetPrivateData contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dresource9-getprivatedata).
The installed native destruction counterexample is retained in
[replay native callbacks](../reverse-engineering/replay-native-callbacks.md),
as evidence of the risk, not a required backend layout.

Window messages are a separate route. Microsoft identifies CreateDevice, Reset
and final device Release as operations that can cause mode-change messages while
runtime critical sections are held. Reset explicitly permits messages before it
returns. They must remain outside the replay segment. See
[Direct3D threading](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multithreading-issues)
and [Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset).
SendMessage can invoke a same-thread window procedure directly; cross-thread
sending waits and can process incoming messages. It is not a safe diagnostic
operation inside exclusion. See
[SendMessage](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendmessage).

Ordinary D3D9 state-setting and drawing methods do not expose a user callback
parameter. Treating the bounded sequence below as free of arbitrary application
callbacks is an inference from that API contract plus absence of registrations
and reviewed entry routes; it is not a Microsoft guarantee covering custom COM
interceptors, overlays, arbitrary drivers or every native implementation. Native
Windows execution remains a required validation target, not a completed test.

### Permanent veto and baseline

The monitor needs a process-lifetime `coverage_unknown`/`foreign_callback_seen`
latch and an immutable first reason, in addition to active application roots.
These are refusal conditions, not faults in the application's original call:
forward that call with its original arguments/results after ordinary admission.

1. Start observation before returning the first application factory/device, with
   no previously exposed graphics objects. App-local proxy loading at process
   startup is the intended route. Attaching to an existing device or adopting an
   arbitrary native object cannot establish this baseline. The fixture may arm
   a clean process explicitly; a live loader must establish startup coverage.
2. Before dispatching any application SetPrivateData whose flags contain
   `D3DSPD_IUNKNOWN`, set the permanent process veto. Cover Texture, CubeTexture,
   VolumeTexture, Surface, Volume, VertexBuffer and IndexBuffer. Do this even
   when the call later fails or replaces a known entry, and before a possible
   native AddRef callback. No pointer dereference is needed to latch the veto.
3. An internal sidecar registration is exempt only through a separate private
   entrypoint with a positively owned object and reviewed nonblocking callback
   implementation. A matching GUID, interface address supplied by the caller,
   or general TLS internal-call flag is not authority. Current finite-sidecar
   callbacks must continue to avoid waiting on registry/backend/application
   locks; deferred retirement stays outside exclusive replay.
4. Never clear the veto on FreePrivateData, reset, object destruction, a failed
   registration, or a quiet interval. An earlier worker callback may still be
   queued. This removes the need to infer asynchronous callback completion.
5. Before returning an Ex interface, native fallback device, unknown successful
   QueryInterface escape, externally shared resource or late-adopted untracked
   object, permanently mark coverage unknown. Continue ordinary forwarding.
   A second device is allowed only through the same complete admission route.
   Any public raw-native escape needs a reviewed counted transaction; arbitrary
   application mutation invalidates this baseline, not just one buffer revision.
6. Snapshot the latch while atomically promoting the outer Clear root. New
   registration attempts cannot dispatch until exclusion ends; a registration
   admitted earlier prevents promotion through the active-root count. No separate
   unlocked check-then-promote sequence is sufficient.

The current executable has one static D3D import, Direct3DCreate9, but also
LoadLibrary/GetProcAddress imports. This is encouraging route evidence, not proof
that dynamic or bundled-library escapes never occur. The next bounded game audit
must identify resource registration and device-creation helpers in the EXE and
bundled D3DX, then cover their actual routes. Absence in a short trace alone is
not a clean-start certificate. See [executable inventory](../reverse-engineering/executable.md).
A startup route that cannot be established stays in forwarding/diagnostic mode;
there is no need for a backend DLL hash to express that refusal.

### Smallest exclusive execution segment

| Phase | Allowed work and required ownership |
| --- | --- |
| Before promotion | Prepare shaders, declarations, targets and immutable draw data; perform allocations and retire old objects. Hold a live device owner. Resolve game lifetime observations. These ordinary operations carry application admission. |
| Promote and validate | Require the only outer root, callback/coverage latches clear, no pending maps/DCs for inputs, current execution/lease/finite/lifetime identities and scene boundary. Read-only descriptor/GetDevice work may use retained native owners; temporary releases cannot destroy their parent. GetPrivateData is restricted to authenticated mod sidecars. Do not hold the registry during native dispatch. |
| Save | Capture a fresh stateblock and explicit RT/depth references before changing state. Do not recapture a populated stateblock whose old retained resources could be released. Preserve partial-save references for deferred retirement. |
| Inject and restore | Use the reviewed native bind/unbind, viewport/render/sampler/constant/shader/declaration setters, scene-compatible Clear/draw operations and StateBlock::Apply plus explicit RT/depth restoration. All original and injected resources remain retained. The current UP initialization may allocate internally but has no application callback parameter; moving it to a prepared triangle VB is a useful simplification, not a required claim of zero native allocation. |
| Original Clear | Issue only the intercepted original call under a narrow explicit token, preserving its complete CPU/LastError boundary and normal observation. Do not grant other wrapper dispatch a blanket same-thread exemption. |
| After exclusion | Publish diagnostics, call consumers and release the state/geometry/output retirement batches; perform loss/reset teardown only here. Failure paths use the same order. |

Present, CreateDevice, Reset, TestCooperativeLevel, final device Release, additional
swapchain creation, cursor/window changes, message pumping, SendMessage,
alertable waits, shader compiler includes and arbitrary diagnostic consumers are
not part of this segment. Query Issue/GetData and waiting for GPU completion are
also excluded from the first producer. The existing RESZ depth-copy adapter needs
its own exact call inventory and deferred cleanup before being included; the
registration veto does not prove depth-copy restoration or portability.

The game-specific audit identifies WndProc `0x4d3620`, installed by window
initialization `0x4dac90`, and message pump `0x4d34b0`. The reviewed WndProc has no
direct D3D/Reset dispatch, but its deactivation and audio-message branches invoke
application callback tables. Therefore its whole body is not callback-free.
D3DX shader validation also registers an internal callback via a dynamically
resolved validator; compilation remains outside the exclusive segment. These
are bounded derived callsite findings; the
[game callback audit](../reverse-engineering/game-callback-registration.md)
records the evidence and remaining indirect targets.

A concrete window admission option is an outer window-procedure wrapper installed
on the focus/device windows before returning the device to the application. Its
application ticket surrounds CallWindowProc into the previous runtime/game chain,
with chain-change/lifetime validation. Installing admission only inside X3's
WndProc can be too late: Direct3D's own window hook precedes it. This outer route
makes ordinary external focus/message processing visible before entering that
chain, while messages caused by an already admitted CreateDevice/Reset retain the
existing root. It must be tested on native Windows, including correct window
thread installation and replacement; it does not imply that arbitrary internal
worker-thread message sends are universally safe. No window-hook implementation
or game thread-identity proof is claimed in this design note.

Window procedures on other threads may attempt ordinary wrapped D3D calls and
wait at entry without holding a graphics transaction; replay must not wait for
those threads or send them messages. A mode-changing call already in progress
has an active root, so promotion refuses. Same-thread window reentry requires a
callback-dispatching operation; the selected segment deliberately excludes the
documented sources above. Audit the game's WndProc and render-thread placement,
and test actual focus/mode transitions. Do not silently bypass admission on a
same-thread unexpected callback: that would make the state snapshot invalid.

`RigidMotionRetirement` already transfers seven explicit saved references without
allocation/AddRef, including partial failure, for release after exclusion.
MotionCapture must wire it, keep its device/pass/output owners alive, and order
geometry retirement similarly. See
[retirement verification](../verification/rigid-retirement.md).
This addresses explicit cleanup placement as well as the global registration
veto; neither mechanism substitutes for the other.

### Finite acceptance work for this contract

The next implementation can be accepted after these concrete controls, without
an open-ended audit of hypothetical driver callbacks:

- All seven registration wrappers latch before a synchronous test AddRef can
  reenter; failed registration also latches. Removal/reset never re-enable replay.
  Mod-sidecar registration remains eligible; caller GUID spoofing does not.
- A private-IUnknown destructor intentionally deferred until after its original
  CPU call returns cannot coexist with successful replay promotion because the
  earlier registration already vetoed the process. Include a second-device case.
- Late adoption, Ex/native fallback, unknown interface escape and incomplete
  startup each prevent promotion; clean wrapped startup and ordinary second
  wrapped device do not. Test the game/D3DX routes found by the bounded audit.
- Every candidate method, early return and partial-save failure retains resources
  until restoration/token release. Real private-IUnknown markers in a dedicated
  deliberately vetoed fixture verify retirement ordering without qualifying that
  fixture for live replay.
- A WndProc/barrier fixture issues wrapped calls from another thread while replay
  holds exclusion; no native dispatch occurs early, and replay never pumps or
  waits for the window thread. Nested Clear during ordinary Reset/message work
  refuses promotion. Native Windows and Preview require separate run evidence.

Together with entry inventory, execution-state synchronization and pending-map
checks, this is a bounded portable route to enabling the private motion producer.
It does not certify temporal continuity, scene coverage, HDR transfer, or TAA
consumption. The live gate remains closed until its implementation and controls
pass; the callback question no longer requires proving a universal backend
callback-free property.
