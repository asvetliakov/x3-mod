# Transient geometry retention for motion replay

Architecture of the transient ownership API and private `MotionCapture` producer
developed after checkpoint `c4f3d45`. In the serialized component fixture, the
producer invokes `RigidMotionPass` before the selected application's final depth
Clear; its output is diagnostic and is never consumed for temporal color.
Production live replay and loader execution tracking remain disabled until
mutation exclusion is enforced. Verification results are documented separately.

## Existing contracts

`MotionHistory` retains only keys and submitted matrices. Its two-phase collection
and sealing must finish before lookup: a later ineligible or conflicting duplicate
can invalidate an earlier observation. A matching pair proves neither current
buffer contents nor exclusive scene-color ownership. Current observations include
allocation revisions in their keys; a rewritten buffer conservatively loses
correspondence even if its bytes happen to be equal.

`RigidMotionPass` consumes borrowed native VB/IB pointers, an original current
depth surface and a caller-owned RGBA32F motion target. It checks actual native
device ownership and descriptors, restores state, and publishes output only on
operation/restoration success. Its finite, correspondence, scene and query flags
are caller obligations. Uncovered pixels remain invalid; this does not mask a
transparent or equal-depth unsupported contributor over a replayed pixel.

`retain_renderer_resource` already demonstrates the correct ownership split:
logical devices can own native references without owning application wrappers.
Its list is retired before native Reset/final device-root release. It is not a
suitable unchanged per-draw API: it has no individual or frame-scoped retirement,
opaque identity, content validation or hard per-frame slot bound.

## Ownership API

The API uses opaque `GeometryLeaseHandle` and `GeometryFrameHandle` values:

1. `begin_geometry_frame(device, frame_out)` starts a bounded reservation
   generation. An already active frame for this device is rejected; the caller
   must end it explicitly.
2. `acquire_geometry_lease(frame, wrapped_vb, optional_wrapped_ib, request,
   lease_out)` copies an immutable `GeometryLeaseRequest` and acquires native
   references atomically as one geometry reservation. The request contains owner
   generation, exact position query, indexedness and optional index query.
3. `inspect_geometry_lease(frame, lease, view_out)` revalidates those same stored
   requests and returns borrowed native pointers
   together with the finite/index views. Failure exposes no native pointer;
   diagnostic status/reason fields may still describe the refusal.
4. `release_geometry_lease(frame, lease)` frees an abandoned draw's reservation.
5. `end_geometry_frame(frame)` invalidates all frame handles and
   releases remaining native references.

Fixed tables cap the process at 64 frames, 8,192 leases and 512 MiB of conservatively
charged native allocation bytes; a frame admits at most 4,096 leases. Duplicate
reservations charge the full buffer sizes again. Refusal never evicts a prior
lease. The motion collector is independently capped at 4,096 observations, counts
ineligible records too, and invalidates history on collector overflow. A refused
lease remains an ineligible observation rather than silently disappearing from
duplicate/coverage analysis. Do not retain application VB/IB wrappers: their
logical parent references could keep the owning logical device alive, preventing
its final-retirement path from running. Native references hold the allocation
and its private sidecar alive without that logical cycle.

Frame and lease handles use a shared globally monotonic nonzero serial, so slot,
frame or device-address reuse cannot revive one. Exhaustion disables admission
rather than wrapping. Acquisition rejects a recognized wrapper whose parent
differs from the requested device and validates both allocations before retaining
either native reference. Null IB is explicitly nonindexed; a stale bound IB on
a nonindexed draw does not change that declaration.

Lease admission additionally attests native AddRef/Release endpoints. Each record
stores its certified native Release functions so retirement can balance the
original references even when a later slot replacement makes inspection refuse.
Any currently registered canonical wrapper must still have the expected owner,
kind and Lock/Unlock forwarding slots. A wrapper's absence is supported; a saved
dead wrapper address is never dereferenced.

`DrawInputReader` acquires a lease while its local references to the actual queried
VB/IB remain alive, when given an active geometry frame. Its result carries the
opaque handle, never a retained application interface. No pointer is reconstructed
from a resource ID later. An unsuccessful original draw remains an ineligible
observation for history poisoning; its bounded reservation is released with the
frame if not released earlier.

Inspection uses the held native allocation and its original owning device's
finite sidecar/metadata, without recreating an application wrapper. Reuse the
existing trusted native qualifier, finite query and index evidence. Require the
original expected revisions, no pending/ambiguous locks, unchanged owner/reset
generation, current qualified native endpoints and the exact position range.
The lease returns actual IB extrema; DrawInputReader and MotionCapture separately
check those extrema against declared min/count/base. Holding an allocation does not
freeze its contents. Unobserved native writes remain outside the established
observer contract; the lease must not imply otherwise.

Retire handles before releasing their native references, and release outside the
registry mutex. Detached references remain charged against the process count and
byte limits until native callbacks and CPU reference cleanup finish. End-of-frame,
failure, Reset attempt, device loss and final logical
device retirement all drain the reservations. The usual render/reset/write
serialization applies; returned native pointers are valid only until that frame
is retired and must not escape to application code.

The live caller has not yet established that write serialization. The capture
mutex covers device hooks, but VB/IB Lock/Unlock wrappers do not acquire it.
Their registry critical sections protect bookkeeping; native Lock and the
application's mapped writes happen outside those sections. A worker may therefore
start a writable mapping after lease inspection and before replay. Static usage
or a currently unchanged revision does not exclude that race. The standalone
fixtures provide serialization explicitly; their success does not establish it
for the game. Live GPU replay needs an additional admission/exclusion mechanism
before enabling this path. A viable mechanism must cover Lock attempts before
native dispatch, reject already-pending mappings, revalidate while exclusive,
and exclude new mutations through replay submission without waiting under the
ownership registry or a native backend lock. The production adapter currently
refuses live replay and the loader leaves execution tracking disabled. See the
[next exclusion design](motion-replay-exclusion.md) for admission, callback and
parent-Release ordering that must be established before enabling them.

## Boundary and replay transaction

This is the component transaction, exercised under fixture-authored serialization.
The production `motion_live_replay_available=false` gate prevents its GPU replay;
the loader also keeps execution tracking off.

`SceneCapture::before_clear` returns a typed candidate carrying the selector's
device, generation, frame, sequence and exact color/depth identities after a valid
depth copy. When invoked by a serialized caller, `MotionCapture` runs before the original Clear and verifies the
currently bound original depth/color still match. Copying depth through RESZ and
generating motion occur in this interval, with each operation fully
restoring native state and bypassing application-event observation.
The pre-clear adapter preserves incoming x87 state, MXCSR and LastError. The Clear
hook separately restores original-call input state immediately before dispatch
and preserves native outgoing state through all post-call bookkeeping and
destructors, using the qualified legacy state-transport helper.

Collect all main-scene observations from the selector's scene epoch, including
unsupported draws; do not mix background, bloom, overlay or another depth epoch.
Seal the complete collection before GPU submission. Revalidate leases and engine
lifetime snapshots immediately before replay: observer/load/registry epochs,
node/camera serials and a conservative mutation-revision policy must agree. The
draw's source contract must still be the token derived from its actual submitted
VS, and use the captured submitted rows rather than reconstructed transforms.

Pre-clear GPU output is staged, not committed history. Confirm the same candidate
only after the original Clear succeeds; a later selector rejection, loss, failed
Present or restoration failure invalidates the staged output/history. Release
geometry leases as soon as replay no longer needs them, with frame-end cleanup
as the backstop. Previous-frame matrix history needs no previous-frame COM refs.

The scene selector is a boundary recognizer, not a whole-color eligibility
proof: its scene phase permits unreviewed shaders and disabled depth writes.
Unsupported/equal-depth/transparent contributors require a conservative batch
rejection or an independently validated reactive/coverage mask before consuming
motion for temporal color. A selected boundary alone must not set this proof.

## Execution and temporal obligations

The ownership execution observer supplies `ExecutionView` from observed scene,
stateblock and query transitions. MotionCapture refuses unknown state, recording
stateblocks and non-idle queries before setting `RigidMotionInputs` flags. A
shader/Clear sequence is not used as proof of query idleness. Uncertain injected
execution taints that observer through `invalidate_execution_state`.

Camera lifetime serials distinguish allocation reuse, not camera cuts. Keep
continuity unknown explicit until a separate policy supplies it. MotionCapture
constructs `MotionHistory` with `DiagnosticStorageCorrespondence` and leaves
`MotionFrame::continuity` Unknown. Matching pairs retain that purpose/continuity
and cannot attest temporal continuity. A confirmed successful frame commits only
CPU matrix observations; its frame-local motion texture is already gone. The
default temporal history purpose refuses prior pairing on unknown continuity;
explicit discontinuity clears both purposes. See [motion history](../verification/motion-history.md).

`MotionCapture` uses a frame-local pass and output texture; its synchronous
diagnostic callback can inspect borrowed output only before `before_clear`
returns. This ensures native shaders/declarations/targets are gone before later
Reset or final device teardown. A future persistent pass would need an ownership
retirement callback because the capture Release hook learns final logical count
only after forwarding Release. Such a callback must be non-owning and run outside
the registry mutex before native resource/root teardown.

## Focused verification seams

Test real replay after all application VB/IB references are released and wrappers
are recreated; final device Release must still complete. Test changed bytes or
pending writes between draw and replay, cross-device inputs, stale handles after
Reset/device/slot reuse, partial acquisition and capacity failure, and independent
IB extrema failures. Verify references/budgets return on failed draw, missing
boundary, failed Clear, loss and final teardown. A synthetic two-frame GPU fixture
must exercise actual motion pixels, original-depth equality and restored caller
state, plus a later conflicting duplicate and an unsupported color contributor.
These tests complement, rather than replace, live-game boundary/coverage evidence.
