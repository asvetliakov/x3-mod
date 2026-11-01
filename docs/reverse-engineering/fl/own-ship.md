# FL own-ship root and ancestry for adaptive shadows

Bounded offline RE, 2026-10-28. **Reuse the existing owned material adapter;
no additional native detour is needed for the supported native render path.**
The established identity is the active control cockpit's **reference object**
and its render root, matching AP's adaptive-cascade policy. It is not a universal
player-character identifier independent of cockpit/script state. Root and
ancestry must be copied at submission time and published as owned values.
This note qualifies the native field/ordering contract. The adapter extension
is now implemented and independently reviewed with native synthetic evidence
([ledger](../../verification/farnhams-legacy-own-ship.md)); adaptive-shadow consumer
integration and game verification remain open.

All addresses are preferred VAs in clean FL, image base `0x400000`. Reuse the
admitted loaded-image base; never read AP's `0x608504` in FL. Owning feature:
[shadow retention](shadow-retention.md). Dependencies:
[material submission](material-submit.md),
[shared render admission](../../architecture/farnhams-legacy-shared-render-admission.md),
[owned lifetime accessor](../../verification/farnhams-legacy-lifetimes.md), and
[active cockpit evidence](ui-input.md).

## 1. Native root identity and initialization

| Link or operation | Established FL contract / operand evidence |
| --- | --- |
| Active cockpit registry | Image slot **`0x5a82e4`**, registry DWORD `+0` = map, DWORD `+0x10` = active handle. Setter loads the registry at `0x431fae`, validates the requested handle through `0x41e420`, normalizes failed lookup to zero, and stores at `0x431fd0`. |
| Cockpit resolver `0x41e420..0x41e456` | ECX=registry, `[entry ESP+4]`=handle; EAX=cockpit or zero; `RET 4`. It loads map `[registry]`, bucket array `[map]`, count `[map+4]`; index `(count-1)&handle`; link `{next,key,value}` at `+0/+4/+8`. Comparison `0x41e440`, value load `0x41e450`. No allocation/call; native chain itself is unbounded. |
| Cockpit `+0xc` | **Reference/follow object** pointer. Constructor initializes zero at `0x421164`. Native reference command resolves the nonzero object handle through `*0x5a82ec`, resolver `0x443d30`, then stores EAX at `0x430911`. A zero input or failed lookup stores zero. |
| Cockpit `+0x10` | Separate controlled/view object, initialized zero at `0x42116d`. Input mode may select it instead of `+0xc`; it must not replace the adaptive-shadow reference identity. Camera update also distinguishes the two pointers at `0x4224d7`. |
| Restored reference | Read branch in `0x41a400`: decoded nonzero object handle at `0x41a70b`, simulation registry load `0x41a70d`, resolver call `0x41a714`, zero branch `0x41a723`, cockpit `+0xc` publication **`0x41a727`**. Whole-load exclusion covers this fixup, not just the earlier B3D loader return. |
| Simulation object resolver `0x443d30..0x443d66` | ECX=simulation registry, stack handle, EAX=object or zero, `RET 4`; map is **registry `+0x14`**. Same `{next,key,value}` hash-node layout. Do not confuse this registry with the observed B3D render map at engine `+0xc`. The proposed copy does not need to traverse the simulation registry. |
| Reference object `+0x70` | **Render-root pointer**, not inline node data. Native camera update obtains ref from cockpit `+0xc` at **`0x42241c`**, reads root at **`0x422441`** or **`0x42246f`**, then consumes node base/world position and basis. This is independent FL consumer evidence, not an assumed AP offset. |
| Root initialization | Object initializer zeroes `+0x70/+0x74` at `0x449a2c/0x449a33`. One concrete type branch allocates a render node at `0x449bd4 → 0x4a2200`, publishes EAX at **`0x449bdf`**, then initializes its model at `0x449be2 → 0x4a35e0`. Publication alone is not initialization completion. |
| Render-node handle | DWORD `node+0x28`; birth/restore/removal and the owned observer contract are established in [FL object lifetimes](object-lifetimes.md). Root serial must be obtained from the owned B3D observer after the native copy. |

`0x42241c` is in camera update `0x421f50`: ECX=cockpit at entry, copied to
EBX at `0x421f5e`; ESI then holds the reference object in this branch. Internal,
external and scripted camera modes choose different pose calculations. They do
not redefine the stored reference pointer. AP's `object_capture::own_ship`
deliberately uses active cockpit `+0xc` regardless of connect/view mode; retain
that semantic policy. Do not add an external-view-only gate or label `+0x10`
as the own ship merely because the input dispatcher uses it in one mode.

The existing [HUD lifetime study](ui-camera-lifetime.md) proves
cockpit publication after initialization (`0x4305be`, then registry insertion
`0x4305cd`), restored-owner insertion after loading (`0x420e33`), and active
selector initialization/restoration (`0x41dff0/0x41dda8`). Cockpit removal at
`0x430605` removes the row before `0x43060d` destroys the owner. An old active
handle whose row is absent means no root; it is not permission to reuse an old
cockpit pointer. Cockpit `+8` is the HUD camera and is not a required equality
match for the material's world camera.

## 2. Descendants and retirement ordering

The ancestry relation is the B3D parent pointer **node `+0x18`**. It includes
render subassemblies without relying on model names, primitive counts or object
class bits. This is the same relation AP's `own_ship_descends` uses for turrets,
engines and other attached parts; independently detached nodes no longer belong
to that root even if they originated from the same simulation object.

Reparent `0x4a57d0` takes ECX=child and one callee-cleaned stack argument=parent
(`RET 4` at `0x4a5856`). Child is EDI; incoming parent is EBP after
`0x4a57e8`. It unlinks the child, inserts it into the parent's intrusive child
list (`0x4a5801/0x4a5806`) and stores **`child+0x18=parent` at `0x4a5809`**.
Scene attachment `0x4a56b0` takes ECX=node and one stack scene argument; it
clears **`node+0x18` at `0x4a56cd`**, inserts into the scene's top-level list and
stores scene at `+0x1c` (`0x4a56f6`). Node destruction recursively destroys the
child list at `0x4a33b0..0x4a33c4` before removing/freeing the parent.
These establish graph ownership and null-parent termination for a healthy
submitted node. They do not establish an engine-wide maximum ancestor depth.

**Parent writes do not retire/reinsert the node.** Registry revision and serial
can remain unchanged across reparenting or detachment. Therefore AP's
multi-frame `(pointer,handle)` ancestry cache cannot authorize FL results.
Copy the chain anew for every material invocation; the native graph can change
between submissions without changing the observer binding. A separate topology
hook/generation is unnecessary when no ancestry result crosses an invocation.

Reference invalidation is explicit: helper `0x427640` receives ECX=cockpit and
one stack object pointer (`RET 4`). It compares that pointer with cockpit `+0xc`
at `0x427648`, zeroes a match at **`0x42764d`**, and separately clears `+0x10`
at `0x427659`. All-cockpit invalidation `0x42fc70` takes ECX=object, saves it in
EBP, enumerates `*0x5a82e4` and calls the helper at **`0x42fcda`**.
Reference cleanup `0x454190` preserves its incoming object in EBX, forwards it
at **`0x454206/0x454208 → 0x42fc70`**, then continues other object-reference
cleanup. These are native lifecycle mechanisms, not new observer hooks.

A concrete unsafe sampling interval must remain visible: object destruction
`0x4491a0` reads root at **`0x4492f5`**, destroys it through **`0x449303 →
0x4a3300`**, and only then zeroes object `+0x70/+0x74` at
**`0x449308/0x44930f`**. Its call to reference cleanup is later,
**`0x44938e → 0x454190`**. There are callback/cleanup calls in the destructive
body; it is not a call-free lifetime envelope. A retained cockpit/reference
pointer or a successful old registry lookup cannot authorize copying through
this interval. A post-copy serial check would be too late to prevent the read.

## 3. Why the existing material seam can own this copy

This is a **derived extension of the already-qualified native admission
contract**, not a claim that the lifetime observer pins cockpit or simulation
storage. The precise supporting boundaries are:

1. **Owner and normal update order.** The fixed outer game loop runs native
   commands and simulation/cockpit update synchronously, then calls registry
   cockpit update at **`0x4782ca → 0x41e550`**, immediately followed by
   complete rendering at **`0x4782cf → 0x48b6d0`**. The material study's
   native-command scheduling closure establishes ready-task dispatch and
   method/interpreter transport on this same owner, not worker publication.
   Reference/active-cockpit setters belong to the synchronous native command
   dispatcher `0x430540`. Normal setters complete before a material copy.
2. **Whole-load and close exclusion.** The implemented common group covers
   complete load entry **`0x479430`**, complete render entry **`0x48b6d0`**
   and deferred close call **`0x4ec8ed → 0x476250`**. Whole load includes
   simulation/registry and cockpit restoration after the B3D subsection;
   its scope denies sampling until normal successful completion. Close/KC
   teardown runs only after native depths reach zero. Quarantine, drain,
   failed load/unwind or an absent owner scope grants no borrow.
3. **Actual callback closure before reaching the seam.**
   [Render exclusion](ui-render-exclusion.md) proves native
   window/input/audio callbacks publish input, next-location or ready results;
   they do not run another game loop or execute these setters. SC_CLOSE is
   the real synchronous KC exception and is deferred. The
   [render mutation study](ui-render-mutation.md) closes the
   registered allocation-recovery/D3D/D3DX/zlib frontier: it can reclaim
   resource payloads, but does not run cockpit/simulation native commands or
   reparent the graph. This conclusion relies on those actual registrations
   and interface provenance, not merely on a thread-ID comparison.
4. **Known in-pass writes and tail destruction.** Render traversal updates
   transform/LOD fields, and the known in-pass model initialization finishes
   before subsequent submission. They are synchronous, so the fresh copy
   sees their completed state. Playback destruction at `0x48bded` occurs
   after the last material submission; its continuation performs only array
   cleanup. It does not allow another sampler to dereference the former root
   after that destruction.
5. **No reentry within the copy.** Once the adapter has obtained the current
   borrow and owned binding, perform the complete bounded cockpit/root/parent
   read with no calls. No engine helper, map resolver, allocation, logging,
   page query, COM/D3D call or observer lock may occur inside it. The owner
   cannot enter a synchronous setter/destructor during this interval. The
   native healthy graph and completed reference publication establish entry
   readiness; the admission/closure establishes the finite read lifetime.

Thus **the common owner/load/close/callback protocol supplies dereference
permission; owned B3D serials supply identity validation afterward**. Neither
`object_lifetime::fl_owned_current` nor `VirtualQuery` substitutes for step 1–5.
This does not grant arbitrary follow-up reads through a material record's parent
or root token from a renderer method after forwarding.

No supported native mutation bypass of these boundaries was found in the named
paths. A new native callback that synchronously invokes simulation destruction,
a direct private render call from the destructive interval above, or foreign
thread/native pointer mutation falls outside the fixed healthy-native profile.
If any becomes supported, exclusion must move before that mutation; retaining
an old serial or merely refusing publication after the copy is not a repair.
The specific root-before-clear interval above is the failure witness, not an
unbounded demand for a new whole-engine audit. Existing normal/foreign-SEH
scope quarantine and callback rules remain required.

## 4. Minimal adapter and consumer contract

Extend `material_submit_fl::{Record,current}` and its existing entry helper,
with a small owned result, for example `OwnShip { status, root, root_handle,
root_serial, own }`. `root` is only an identity token. Reuse the record's full
borrow, engine/map, observer/load/registry/revision, camera/node serials and
submission serial; do not introduce a parallel global provider or second detour.
Propagate the owned result through `render_engine_state::DrawSample` into the
route. AP keeps its existing reader path.

At existing material CALL **`[0x4e7142,0x4e7147)`**:

- Obtain common admission and a fresh owned lifetime binding before native
  reads, as the existing adapter does. Require the FL profile's relocated
  cockpit slot. The existing entry node and camera are the material's current
  native borrow, not an observer-table discovery.
- During the same call-free interval, read slot → cockpit registry → map/header
  and active handle. Resolve only that bucket with a finite chain limit,
  pointer arithmetic/alignment guards, power-of-two capacity bound and cycle
  detection. Read matching cockpit's own handle at `+0` and require it to
  match; copy `+0xc`, reference object's `+0x70`, then root handle `+0x28`.
  Null registry/selector/reference/root yields a fresh explicit no-root result;
  malformed pointers/layout/cycles/limits yield unknown. Neither reuses a prior
  root. Do not call `0x41e420` or `0x443d30` from the adapter.
- Starting with the submitted node's already-copied handle and parent, copy
  ancestor `(pointer,handle,parent)` words until the root matches or null ends
  the chain. Bound the entire chain and reject cycles/overflow. AP policy is
  16 visited nodes and 32 hash links; these are usable initial adapter limits,
  **not measured FL structural maxima**. A limit refusal must be diagnosable.
  Copy fresh each invocation, including negative/non-own results.
- After the last native read, validate the root and each visited node against
  the same binding using owned metadata and the material camera. Require the
  material node serial to agree, and obtain root/ancestor serials. A bounded
  batch-owned accessor may avoid one mutex acquisition per ancestor; it must
  retain `fl_owned_current` semantics and perform no native reads. Recheck
  current borrow and full binding revision after validation before publication.
- Publish only the compact root identity/verdict. Keep chain scratch local to
  the entry helper, not as a long-lived native walk and not as a multi-frame
  ancestry cache. The normal material `current`/post-draw validation must also
  preserve root identity: unchanged binding protects its copied serial; if
  explicitly revalidated, compare root serial, not just root pointer/handle.
  Invalid/nested children shadow outer results under the existing TLS scheme.

The original call ABI stays unchanged: ECX=descriptor, EDX=node; four stack
words camera, flags, light1, light2; ordinary RET and caller `add esp,0x10` at
`0x4e7147`. Preserve original input/output GPRs, EFLAGS, LastError, x87/MXCSR/XMM
state in the existing thunk. No register is newly free because this helper reads
more fields. Keep four-byte incoming-stack compatibility and the existing SEH
search/continue/unwind behavior. Review the thunk's **512-byte scope storage**
and matching `static_assert` if `Record` grows; local chain scratch belongs in
the C helper's aligned stack. A copy fault is not permission to resurrect an
outer value or forward through partially changed observer bookkeeping.

For `motion_output_shadow_adaptive_inc.h`, select the FL owned result instead
of `resolve_own_ship`/`object_capture::own_ship_descends`. Only successful,
main-scene-qualified candidate draws with known extents and current FL ticket
may feed the existing radius/hysteresis/cascade policy. Preserve own-root draws
and descendant draws equally. Do not require root itself to have rendered first.
A non-own draw can still supply the current valid root identity; it simply does
not contribute radius. Reset the frame accumulator on root identity change;
never mix radii from two roots in one frame. Root equality must include serial
and observer/load/registry/engine domain, so same-address replacement triggers
adaptive change. Preserve this semantic identity when calling the shared adaptive
helper, whose existing root key is only `uintptr_t`: resetting its prior identity
on a serial/domain change is one concrete option without changing geometry math.

At next-frame commit or delayed use, validate the selected root/camera from owned
metadata (fresh binding, matching full domain/serials) or discard the pending
radius. Do not retain a material invocation as a memory lease. Load, Reset,
scene/camera invalidation, loss of common admission, observer loss and foreign
unwind clear the pending owned result. A new successful sample can restore
operation; this is a working provider design, not a permanent refusal fallback.

## 5. Acceptance scope and bounded cost

Implementation tests must cover root itself; turret/engine descendants whose
root was not drawn; unrelated nodes; reparent-away and reparent-back with
**unchanged registry revision**; detachment; same-pointer/same-handle root and
ancestor reuse with different serials; cockpit selector/reference replacement;
reference absent after destruction; view changes with unchanged reference;
load/failed-load/Reset/unwind; stale binding/common stamp; nested material
success and invalid child; chain cycle/depth/collision limits; and two roots
appearing in one frame. Unsupported-admission fixtures must prove zero native
reads before admission and forbid copying in the root-before-clear witness.
Keep material sampling available when own-ship metadata alone is absent: an
unavailable optional result is not permission to invent a root or discard an
otherwise valid TAA sample.

Preserve successful alpha-tested own-ship candidates, existing resource leases,
far cascades and adaptive hysteresis; no change to alpha-retention policy is
implied. Test that original CPU/LastError/FP state and four stack words remain
identical, and inspect the expanded copy's compiled interval for calls. Native
synthetic adapter checks, focused host contracts and cross-compilation are
implementation-owner work; none was executed in this RE task. Game visual/FPS
qualification still needs user-supplied runs, including cockpit/ship/view/load
changes and descendant coverage.

Cost is bounded by one selected cockpit bucket and the visited ancestry chain
per material invocation. Do not allocate, walk the entire cockpit table, or
perform per-node OS/page queries. Start without cross-invocation ancestry
caching; if measurement warrants optimization, reuse an already-owned root
snapshot only within a proven identical pass/domain, while still copying fresh
parent links per invocation. Batch-owned identity validation avoids repeated
lock overhead. Measure actual material frequency and copy cost; static bounds
are not a timing result.

## Evidence and reproduction

Clean `/mnt/c/X3FL-orig/X3FL.exe`, **2,804,736 bytes**, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
AP behavior is taken from current `object_capture.h` and the previously checked
[AP/FL retention comparison](shadow-retention.md); no AP offset
was used to locate FL's fields. Existing callback/owner censuses were reused;
no full-executable sweep or new hook census was performed.

```sh
python3 verification/results/fl-port/own-ship-contract/verify.py
python3 -m py_compile verification/results/fl-port/own-ship-contract/verify.py
```

Measured static result: **18 selected regions, 409 instructions, 98 exact
operand anchors, 36 region-boundary edges, 98 altered-anchor negatives and
4 interior-boundary negatives**. The producer uses project `inspect_x3.PE32`
and Capstone, checks clean image identity, decodes from the earlier containing
function entry, and verifies each selected slice start/end. Altered-anchor
negatives bypass image hashes; they must fail operand/boundary checks.
`anchors.json` holds the reviewed expected operands; the verifier does not
regenerate expectations. Result JSON is parsed and validated by the producer.
All producers, compact results and raw exploration remain local/ignored beneath
`verification/results/fl-port/own-ship-contract/`. A read-only Ghidra attempt
found no FL program in the existing project; the findings rely on independently
decoded PE instructions and existing derived RE, not unavailable decompilation.
No production edits, game/Wine/native execution, build, install or commit.
