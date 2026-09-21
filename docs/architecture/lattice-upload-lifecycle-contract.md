# B: upload pin retirement and one guarded pair copy

**Removed 2026-09-22.** The default-off lattice state capture, geometry packet writer and upload-hook diagnostic described here is no longer in the tree; the crawl it was built to explain is fixed and accepted (`taa-lattice-crawl.md` §32.5–§32.6). The last commit that carries the code is main `59ad2649`. This note is kept as history.

Design/contract only against main `157aa489`, `taa-lattice-crawl.md` §31 and
`/tmp/x3-lattice-upload-flight-integration.md`. No source/build/Wine/game changes.
Parent ratified process-lifetime Store storage and the after-Release paired-copy
approach during this review. A's hook/ABI interface needs no new callback.

## Existing behavior that constrains the implementation

- `capture.cpp:825–902` (`release_device`) probes **the saved device dispatch**
  with AddRef/Release and counts motion/bloom references before forwarding the
  caller's original Release. On an ownership device these are wrapper counts,
  not an inferred native count. Its `accounting` gate excludes compositor,
  bloom operation, `lattice_query_depth` and other owned-reference operations.
- `d3d9_ownership.cpp:753–805`: `add_ref` increments `Node::refs` under registry;
  `release` decrements there, removes a zero-count node, then destroys backend
  and dispatches a child's parent Release through `parent->application->Release`
  **outside registry**. Therefore child-triggered final Release reaches capture.
- Current upload arm increments that same device wrapper count once. Current
  disarm clears upload globals under registry and Releases outside it, but
  refuses an active frame. Waiting for `device_destroy` to disarm is a cycle.
- `capture.cpp:1600–1713` retains a shared CPU Device owner plus an explicit
  device reference for the observed draw. The pin is dropped after the capture
  lock unwinds. `QueryScope` spans getters and their Releases; native submission
  runs with `lattice_query_depth==0`. Preserve both protections.
- `capture.cpp:1480–1544` invalidates the F8 packet even for a refused reentrant
  Reset; query-depth Reset returns INVALIDCALL before native dispatch. An
  ordinary Reset reaches ownership's registry invalidation and generation
  change. Its success does not restore old uploaded bytes.

## Minimal ownership API additions

Keep the accepted manual arm/disarm/copy APIs. Add these small typed operations
in `clone_upload_observer.h`; implement no-EH CPU/LastError shells over guarded
cores, as with the accepted control/query APIs. Suggested exact shape:

```cpp
struct CloneUploadArmToken {
    std::uint64_t serial = 0; // unique arm, never reused or wrapped
    std::uint64_t owner = 0;  // ownership device lease serial
};
struct CloneUploadPinView {
    CloneUploadArmToken arm;
    std::uint64_t generation = 0;
    unsigned references = 0; // exactly 0 or 1; only OUR live wrapper pin
    bool closing = false;
    bool scope_active = false;
};
HRESULT query_clone_upload_pin(IDirect3DDevice9*, CloneUploadPinView*) noexcept;

enum class CloneUploadClose { None, Deferred, Released };
HRESULT close_clone_upload(IDirect3DDevice9*, CloneUploadArmToken,
                          CloneUploadClose*) noexcept;

struct CloneUploadPairRequest {
    CloneUploadArmToken arm;
    unsigned slot = 0;
    std::uint64_t generation = 0;
    IDirect3DVertexBuffer9* vertex_key = nullptr; // weak lookup key only
    IDirect3DIndexBuffer9* index_key = nullptr;  // never dereference either key
    clone_upload::Identity vertex, index;       // observed allocation+revision
};
enum class CloneUploadPairStatus {
    None, Copied, Unarmed, Closing, ActiveScope, Selector, Missing, Duplicate,
    StaleArm, Device, Binding, Revision, OpenMapping, Dispatch, Capacity
};
struct CloneUploadPairResult {
    clone_upload::Record record{};
    CloneUploadPairStatus status = CloneUploadPairStatus::None;
    bool binding_revision_match_at_observation = false;
};
HRESULT copy_clone_upload_pair(IDirect3DDevice9*, const CloneUploadPairRequest&,
    void* vertex_bytes, std::size_t vertex_capacity,
    void* index_bytes, std::size_t index_capacity,
    CloneUploadPairResult*) noexcept;
```

`query`: CPU-only registry lookup, no AddRef/Release/getter/allocation. S_OK means
this exact recognized device has our pin (including Closing); S_FALSE means no
matching pin and zero output. Do not expose aggregate wrapper/native reference
counts. An arm serial distinguishes disarm/rearm on the same device; owner and
serial exhaustion refuse. Obtain this token after exact-S_OK arm, before enabling
A's route gate. Capture serializes coordinator arm/close decisions with its lock.

`close`: S_OK means the matching request was accepted; the output explicitly
says Deferred or Released. S_FALSE means no matching/stale token, no effect.
Invalid arguments/errors never assert Released. This API may perform one public
Release **after dropping registry**. It is not a CPU-only query. Repeated Closing
requests are idempotent. Existing manual disarm retains its ordinary semantics;
flight teardown uses close because S_FALSE-on-busy alone cannot schedule cleanup.

`pair`: S_OK only for an entire copied pair; S_FALSE carries a precise refusal
and zero/invalid Record. E_POINTER/E_INVALIDARG cover malformed caller storage.
Caller owns initialized, nonoverlapping packet buffers with the stated capacities
for the whole call; no arbitrary-memory probing is implied. No COM operation,
allocation, logging, native Lock/Unlock or staging exposure is permitted.

## Pin state machine and exact Release accounting

The proxy coordinator owns one preallocated process-lifetime Store. Device
retirement wipes/reuses its contents; it does not destroy the arena. The ownership
observer owns its one wrapper device pin and active frame. Capture owns only an
arm token/weak identity and ordinary shared CPU Device context. Nothing releases
COM or waits from DllMain. A successful installed patch/module remains pinned.

States are Unarmed → Armed → Closing → Unarmed. Closing immediately rejects
prepare/copy, latches the current frame refused and wipes eligible retained/raw
storage under registry. It does not force-clear an active frame, wait for it,
or release its pin through a callback while that frame still owns temporaries.
Disable A's atomic route gate **before** accepting a matching close; a second
unrelated device's failed arm/close must not disable the first device's gate.
A call which already crossed the gate still encounters ownership's Closing
check and forwards the original target once without capture.

Capture `release_device` must:

1. Use the new CPU query only for contexts that attempted/own this diagnostic.
   Add its 0/1 pin to BOTH the retained-resource threshold and the final `held`
   total. In the current formulas these become respectively
   `motion_refs + bloom_refs + upload_pin + 1 + retained` and
   `motion_refs + bloom_refs + upload_pin`. Preserve the existing query-depth
   and renderer-operation exclusions and existing retained-resource cleanup.
2. When the balanced saved AddRef/Release probe says `after == held + 1`, the
   caller's not-yet-forwarded Release protects the device. Disable matching A
   routing, then call close with the observed arm token. Do not close a newer
   arm from an old Device context. Concurrent extra AddRef may reduce capture
   coverage, but actual reference operations still determine lifetime.
3. Around **immediate** pin release, use a stack-scoped capture retirement-busy
   guard. The recursive capture Release for the pin skips owned-resource
   accounting, forwards once, and returns its actual count. Ownership clears
   its armed state before invoking that Release. Continue existing renderer
   cleanup, then forward the original caller Release once and return exactly
   its returned count. Keep the CPU Device owner alive across callbacks.
4. If close says Deferred, skip the remaining renderer teardown on this stack
   and forward the caller Release once. Do not synthesize zero. The remaining
   pin (and any existing renderer refs) produces a real nonzero return.
   Persistent Closing MUST NOT permanently suppress reference accounting:
   only the stack-scoped immediate-drop guard does that.

At normal finish or abort, after every completed getter/authentication reference
has been released and the active frame unbound, a shared ownership helper may
finish Closing. Save the arm token in the frame when it binds; terminal drain
requires that exact token, and unbinding the frame plus detaching its closing
pin occurs in the same registry section. Preserve the ready tag at offset zero
and the existing 512-byte Context fit/trivial-lifetime assertions. Under registry
the drain verifies no active frame, wipes/clears the Store/control and transfers
the one pin to a local pointer. After unlocking it
calls that pointer's public Release once, with no later access to the old device,
Store binding or global arm. The pin's own hooked Release is now the caller's
pending reference, NOT an additional `held` pin. Existing renderer accounting
can therefore retire any remaining renderer resources and reach zero.

For example with no renderer refs: immediate final caller Release sees count2
(caller+pin), drops the pin to1, then forwards the caller to0 and returns0.
Deferred final caller Release returns1; eventual pin Release returns0 and retires
exactly once. With H renderer refs the deferred caller returns H+1; the later
pin Release flushes H through the existing guarded route, then returns0. Child
final destruction follows the same hook and count rules. No native count is
subtracted or guessed and no reference is silently consumed twice.

After detach, queries report no pin even during the physical Release callback;
that reference is now the callback's pending caller reference. Coordinator
storage remains alive without a completion callback. A newly created device may
query unarmed state and reuse the arena only after the old frame is unbound;
old-token cleanup cannot disable or modify a newer arm. A's interface is unchanged.

Normal Reset retains the arm/pin, invalidates the packet and Store, and advances
ownership generation. Failed Reset/loss refuses copies. Successful Reset permits
new uploads only; no recovery readback or restoration of old records. Closing
wins over Reset success and never re-enables routing.

Ready-tag handling and the existing native-unwind scope remain unchanged. The
deferred drain runs inside the accepted observer finish/abort CPU envelope but
outside registry. Arbitrary foreign SEH through inherited admission/registry
callbacks, or a COM callback that changes reference/output ownership then throws
without completing its contract, is not repaired by this mechanism. An observed
unsupported unwind stops qualification; no forced drain or invented cleanup.

## One pair after the last callback

Extend `Capture::effective` to retain stream0 VB and IB getter references together
while collecting the existing declaration/ranges/state. All remain inside the
current QueryScope and observed-draw device/CPU owner pin. Allocate packet space
at the F8 edge, never here. Preserve existing unique-draw selection and policy.

After ALL other descriptor/getter/resource Release callbacks, use existing
`get_buffer_lock_view` on each still-owned VB/IB to capture allocation/revision
and ownership generation. Require known, quiet, nonsaturated views and equal
nonzero generations; retain the original weak keys and current arm token. Clear
owning slots BEFORE releasing the two getter refs, then finish those Releases
while query depth is still nonzero. These releases may invalidate the packet,
mutate a buffer, close the upload arm or destroy a wrapper.

Now call `copy_clone_upload_pair` once, with no subsequent observer COM call:
under the one registry guard it independently checks device/arm/Closing/health,
no active clone, exact generation, both application-node weak keys and kinds,
matching owner/allocation/revision, quiet maps, own Lock/Unlock dispatch and one
published Record matching BOTH resources and the selected slot. Missing nodes
refuse, even if a native binding still exists; NEVER adopt/resurrect a wrapper
from a dead key. Expected allocation IDs defeat pointer reuse. Do not derive
identity from a requested descriptor, shape match or equality of addresses.

After validating both capacities and every guard, copy VB and IB CPU bytes and
that same Record under that same lock. Publish the result flag/metadata only
after both memcpy operations complete. Do not call the two existing public
single-buffer helpers (two separate critical sections). A small Store pair
method may reuse their shared checks internally, with no callback/unlock between
parts. Expose a read-only duplicate-slot status for precise refusal; global last
refusal alone is not a per-slot reason. Reset/mutation/close uses the same mutex,
so it cannot split metadata/VB/IB. Bytes come from the admitted CPU Store, never
from a D3D map at F8.

A getter Release that mutates revision is caught by this LAST CPU guard. A
Release that destroys the wrapper yields Missing/Binding, not a second getter.
On refusal the entire packet pair is invalid/erased by its owner; no partial
valid pair or old successful header survives. Policy invalidation from Reset or
failed/suppressed original draw also invalidates the attachment; `result()` must
not restore an invalid packet. Keep `draw_input_coherence=unqualified`: this
proves the observed bindings' revisions at this CPU observation, not immutable
native draw inputs or later-writer/texture coherence.

## Concrete acceptance route, not a lifecycle model

Reuse the real capture fixture seams in `capture.cpp:3293–3328` and
`verification/probe/lattice_observer_guard_{state,seam,live}_inc.h`.
`lattice_observer_guard_live_inc.h:107–138` already exercises last application
Release inside a guarded query, routed/unrouted, and checks one actual native
draw, zero retirement inside the query, balanced pin and final retirement.
Its Reset action dispatches actual `reset_common`, and its dropped declaration
causes an actual native child Release callback. HOWEVER the existing
`lattice_observer_guard_run.py:90` explicitly sets `X3M_OWNERSHIP=0`; those saved
results DO NOT qualify the combined ownership/upload pin. Add an ownership-on
combined case using the actual wrapper, actual capture Release/Reset body and
native D3DX Clone fixture controls, not a copied reference-count simulation.

Required bounded matrix for B APIs/lifecycle:

- Arm before actual selected Clone; two complete pair copies with metadata and
  exact bytes; no added native Lock/Unlock; precise S_OK arm/close states.
- Direct and real child-triggered final device Release, with and without
  renderer-owned refs. Assert every actual return count, one final retirement,
  pin acquired/dropped once, no active scope/Store reference leak, and recreation.
- A test seam inside a real active Clone callback requests final/closing teardown:
  prove Deferred without waiting, original Release return unchanged, no further
  staging/publication, and normal/abort exit drops the pin outside registry.
  If the source/mesh's legitimate device refs prevent a true final-count case,
  label an explicit close-request control separately; do not fake a final count.
- Existing guarded last-app-release query control with upload pin enabled;
  QueryScope must still suppress teardown, and its draw pin's later Release
  must eventually close the upload pin and retire renderer/device once.
- Reentrant Reset in query is refused and invalidates packet. Ordinary real
  Reset/failed Reset preserves arm but prevents pre-Reset pair recovery. Use the
  already qualified creation-thread Reset barrier for a parked paired CPU copy.
- Mutation or wrapper destruction on the LAST VB/IB getter Release; weak-key
  address reuse with a different allocation; stale revision/generation/token;
  open mapping, duplicate slot, short second-buffer capacity and missing record.
  No first-buffer success can publish a partial pair. Check output canaries.
- One-device/busy, stale close after rearm, deferred close followed by recreation,
  nested pin Release, and no new observer query/allocation in the option-off path.
- Full CPU/x87/MXCSR/LastError boundary checks and emitted no-EH shells; retain
  ready-tag/scope-only unwind labeling. No broad CMake/CLI/packet-file wiring is
  required before these APIs and A are stable. Parent owns builds/Wine/review.

No tests were run for this contract. Existing 421-check upload and guarded-draw
results are reusable evidence only within the unchanged boundaries above.
