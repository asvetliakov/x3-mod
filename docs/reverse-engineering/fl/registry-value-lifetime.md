# Farnham's Legacy registry values: allocation, payload and snapshot lifetime

Offline bounded study, 2026-10-28. The named ordinary node/camera paths remove
membership before freeing the **value allocation**. Recovery
`0x485c00 → 0x4993b0` destroys a different resource owner, from engine `+0x14`,
not a value from the render registry at engine `+0xc`. However, **a render
registry entry can name a partially destroyed object whose attached storage has
already been freed**. Registry membership, even with complete birth observation,
is insufficient to admit renderer payload reads.

This note reuses the normal topology/alias closure in
[registry writers](registry-writers.md) and
[retained aliases](registry-retained-aliases.md), and the factory
failure/incarnation contract in [registry binding](registry-binding.md).
Factory observation is an implemented prerequisite supplied by the brief; this
study does not reopen it. The [object lifetime note](object-lifetimes.md)
remains the owner of map semantics. Architecture/admission decisions belong to
[engine profiles](../../architecture/farnhams-legacy-engine-profiles.md).
No production changes, game/Wine/native execution, builds, installs or commits.
All executable addresses are preferred VAs, base `0x400000`.

## Evidence and scope

```sh
python3 verification/probe/verify_fl_registry_value_lifetime.py
python3 -m py_compile verification/probe/verify_fl_registry_value_lifetime.py
```

The [verifier](../../../verification/probe/verify_fl_registry_value_lifetime.py)
checks the same 2,804,736-byte `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Measured: **27 regions, 2,600 instructions, 308 operand anchors, 54 independently
checked region edges, four candidate scope spans and nine rejected negatives**.
Six semantic negatives bypass all region hashes and operand anchors, including
an actual branch redirected around registry retirement. A separate direct-edge
negative targets the interior of the resource entry. Normal CFG dominance is
checked for all four final frees below; calls are assumed to return in this
intraprocedural check, not to exclude callbacks or exceptions.

The bounded linear census finds 65 direct entries to `0x4a3300`, five to
`0x4a4750`, ten to `0x4993b0`, and two to `0x485c00`; none targets a candidate
span interior, and no HIGHLOW pointer names those spans. These are measured
reference counts, not a computed-target proof. The lease model exercises 20
order-preserving reader/writer schedules: ten admit a copy, ten deny it, nine
make a competing writer wait for copy completion. These are **synthetic contract
checks**, not tests of the production observer or native transport.

Raw listings, JSON, producing scripts and `reproduce.sh` stay ignored under
`verification/results/fl-port/registry-value-lifetime/`. A read-only Ghidra
attempt found no `X3FL.exe` program in the local project; the evidence here comes
from the project's PE reader/Capstone tools. No decompiler inference is required.
Normal healthy object/list ownership is the bounded precondition. This is not
an audit of arbitrary corrupted aliases or foreign private-address calls.

## 1. What is freed, and when can a key remain?

| Path | Established order and exact free sites |
| --- | --- |
| Ordinary node destruction `0x4a3300` | Payload teardown begins at entry; removes handle at `0x4a34ef`. Camera-flag branch clears/frees `0x790` bytes at `0x4a3575/0x4a357e`; ordinary branch clears/frees `0x270` bytes at `0x4a35aa/0x4a35b3`. Remove dominates both final frees in the normal CFG. |
| Camera destruction `0x4a4750` | Payload teardown/callbacks precede removal at `0x4a48a7`; clears/frees `0x790` bytes at `0x4a48ec/0x4a48f2`. Remove dominates final free. |
| Failed ordinary/related/camera birth | `0x4a23c6`, `0x4a3eb1`, `0x4a4724` free a new allocation only on the automatic-key helper's zero-return branch. The success paths publish `+0x28` at `0x4a22a8`, `0x4a3e42`, `0x4a4606` and cannot reach those failure frees in the normal CFG. |
| Render-map replace `0x50cf78` | Overwrites hash-entry `+8`, returns zero at `0x50cf7b`, and does **not** free or retain the displaced value. Serialized restore calls insert at `0x4934a5`. Replacement is identity retirement/birth, not memory ownership transfer. |
| Map remove/destroy | Remove unlinks/frees only the 12-byte hash node (`0x50d08b`). Destroy frees entries, bucket array and map (`0x50d143/176/18d`), never values. |
| Engine teardown | Calls render-map destroy at `0x48abad`, engine destructor at `0x48ad6c`, engine allocation free at `0x48ad77`. Engine global remains stale. The factory unwind bypass is already covered by the enclosing factory protocol. |
| Resource destruction `0x4993b0` | Frees nested resource data first; removes key `resource+8+1` from engine `+0x14` at `0x499833`, unlinks at `0x49983f/847`, frees the 0x70-byte resource at `0x49984a`. It never directly calls the node/camera destructors. |

Thus the established **serialized, successful normal render-value free paths**
do not leave their own freed base allocation in the render map. This conclusion
is narrower than saying every reachable value is initialized or that concurrent
mutation is safe. The automatic-key helper searches for a vacant key and then
calls insert; insert replacement returns zero, and `0x50d029..02d` maps that
result to key zero. If an unsupported concurrent writer installs that key between
the vacancy search and insertion, replacement can publish the new value yet
return zero, causing its allocator to take a failure free. That is a conditional
race derivation, **not a demonstrated native concurrent writer** or a reason to
invent an extra topology hook. Reader admission needs the writer serialization
precondition already identified by the observer contract.

Positive membership also cannot extend allocation lifetime beyond a lookup:
a reader can validate `(key,value)`, release its gate, then race normal remove
and final free before its subsequent copy. The verifier retains this synthetic
counterexample. Rechecking a serial after that copy detects stale identity too
late to authorize the memory access.

## 2. Recovery destroys resources, including data beneath still-live owners

`0x485c00` receives the engine in ECX, preserves it in EDI, zeroes usage counts
in the 28-byte body-name records, and marks resources through root nodes and
scene nodes. Recursive marker `0x485b80` receives the node on the stack and
engine in ECX, reads node `+0x144`, increments the matching record `+0x18`, then
walks children at node `+0xc`; its only call is recursion. This is native usage
marking, not an external-reader reference count.

Recovery saves resource-next at `0x485d13` before cleanup. The zero usage test
is `0x485d5d/62`; a selected real resource reaches `0x485d66 → 0x4993b0`.
Sentinel guards are reused from the retained-alias note. This path's ownership
is distinct from ordinary render nodes even though both have a `+0xc` field.

`0x4993b0` takes resource in ECX and preserves it in EBP; its `+0xc` is an
array of mesh pointers, counted by signed word `+0x10`. Each mesh has a 16-bit
reference count at `+0x38`: values above one are decremented (`0x4993f3..3fa`),
otherwise mesh cleanup proceeds. This count belongs to the mesh; neither it nor
native usage marking supplies an atomic acquire/release protocol for a mod reader.

| Owned storage | Release evidence |
| --- | --- |
| Mesh arrays and nested data | CRT frees at `0x499428/455/49c/4d0/4f2`, `0x49957e/5a8/5d2/61d/632/670`, strings/table at `0x4996c1/6f7`, 0x60-byte mesh at `0x49970c`, resource mesh-pointer array at `0x49974a`. |
| GPU-side mesh wrapper | `0x499407 → 0x4de050`; releases per-material wrapper through `0x4dd180`, frees it at `0x4de0c2`, and frees the parent `+0x3c` wrapper at `0x4de104`, then clears that field. |
| Resource attachments | `0x49976d → 0x485a90 → 0x485a20`: matches a resource identifier in scene-owned attachment records; frees their 0x1c-byte child records at `0x485a47` and the 0x3c-byte attachment at `0x485a75`. These are not 0x270/0x790 render values. |
| Resource extension array | Entries of stride 0x60, pointer `+0x5c`, through `0x49978d → 0x48a470 → 0x48a520`; string/vector storage cleanup, 0x28-byte extension free at `0x499795`, array at `0x4997c6`. |
| Resource name/auxiliary owner | Name at `0x4997f7`; auxiliary `+0x5c` through `0x49981a → 0x51b0f0`, which frees buffers and its 0x18-byte owner. |

There is no direct KC invocation in these reviewed bodies. There **are**
external computed releases: attachment GPU cleanup `0x4e0ec8`, mesh GPU cleanup
inside `0x4dd180` (including `0x4dd26a/27e/292/2a5/2d2`), all through vtable
`+8` with an explicitly pushed receiver. Those shapes are consistent with COM
Release; this study does not reconstruct every object's interface/vtable.
`0x4dd180` also calls `0x512490` and `0x4da6b0`. Their transitive releases,
plus recovery's separate texture branch `0x512410`, are the **named callback
frontier**. A direct-call list is insufficient to claim recovery cannot dispatch
messages or reenter native code. No new HUD publication or synchronous KC call
is established by this note, but their absence across that frontier is unproved.

Resource cleanup has ten surveyed callers, including recovery, normal reset
`0x48a8b2`, teardown `0x48aab2`, and resource reset `0x4a1e92`. Observing only
recovery is insufficient for resource leases. Replacements/detachment of the
specific descriptor or camera buffer a consumer follows also require their own
writer contract; these outer resource frees do not cover every field mutation.

## 3. Native callbacks and pre-retirement destruction

The first ordinary-node payload free is `0x4a332b`, from `+0x1ac` with count
`+0x1a8`; fields are cleared later at `0x4a3346/3350`. GPU payload `+0x174`
is released by `0x4a335e → 0x4e7240`; its external vtable `+8` call at
`0x4e725b` happens before the wrapper free at `0x4e72e4`. Node detachment
`0x4a5740` also reaches that release independently of node destruction.
Camera cleanup frees `+0x378` at `0x4a4802`, clearing it at `0x4a4818`, and
releases the `+0x780` list before membership removal. Camera detachment
`0x4a58c0` separately frees `+0x278` at `0x4a58d8` while the camera remains live.

Both destructors issue an optional VM completion notification **before removal**:
`0x4a33a4` passes `(node+0x254,2)`, `0x4a478b` passes `(camera+0x254,3)`, as two
caller-cleaned stack words. The target is VM type record `+0x34`, selected using
object `+0x250` and stride 24. They do not pass the render-value pointer as an
explicit stack argument. The actual fixed native result targets and their
ready/result publication are already traced in
[UI render exclusion](ui-render-exclusion.md#2-window-callback-endpoints):
`0x4c1a40` and `0x478910`, with value release `0x4c5160` and ready insertion
`0x4c1880`. These callbacks do not synchronously run KC under that fixed contract.
A partial teardown remains observable regardless of whether the notification
itself runs KC; external GPU releases are another reentry boundary.

The native renderer **borrows**, rather than retains, cameras and nodes. Its
camera array at `0x48b8a3`, later load at `0x48b91b`, scene traversal
`0x48ba7f/8d`, and projection calls `0x496fb9/0x4976a0` are established by
[UI camera lifetime](ui-camera-lifetime.md#3-renderer-borrowing-and-the-projection-read).
Projection `0x4de570..0x4de920` has zero CALL instructions: camera is saved in
EBX at `0x4de57c`, instance in EDI at `0x4de581`, scene is already read at
`0x4de583`. That is a concrete short synchronous borrow, not permission to retain
those pointers until Present. The returning SC_CLOSE/KC destruction witness in
the UI exclusion note can invalidate a longer native render borrow. Its proposed
deferral and full-pass contract are dependencies, not proved by registry hooks.

## 4. Positive reader/snapshot contract

A useful admission contract is a **bounded copy into mod-owned storage**. It
must acquire permission before the first engine dereference, keep that permission
through the last copy, then drop it before native forwarding, D3D calls, allocation,
logging, waiting or other callback-capable work. Only scalar identity tokens and
copied values may escape; raw pointers in a copied block are still borrowed
addresses, not recursively owned storage. No engine reference count was found
that a reader can safely increment after an unprotected lookup.

There are two precise strengths of permission:

1. **Base-allocation identity copy.** Under the named serialized free paths,
   hold the same gate used by map-mutation begin through lookup and the complete
   fixed-size copy, reject active insert/remove/destroy/load/factory scopes, and
   compare bound incarnation, key and pointer without first dereferencing a stale
   global owner. Remove cannot pass its begin gate until this copy finishes;
   successful prior removal makes lookup fail. This establishes allocation
   lifetime for the copy, **not coherent initialized fields**: destructor code
   before remove can run concurrently and mutate/free attached payloads.
2. **Ready renderer snapshot.** Additionally require a qualified current native
   borrow at the exact sampling seam, or writer exclusion beginning before all
   relevant mutation/destruction. For destruction, begin at `0x4a3300` and
   `0x4a4750`, before their first effects; a resource-following snapshot also
   covers `0x4993b0` and independently reachable payload replacement/release.
   Begin publishes active/retiring state under the same gate before forwarding;
   a later reader denies without touching engine memory. A reader already copying
   finishes before a competing writer begins native work. Same-thread reentry
   never waits on itself: the copy is call-free, and callbacks during native
   destruction see the active scope and deny. Drop after the last read; no lock
   survives native forwarding. Ordinary update writers and birth completion
   need the exact fields' readiness contract too; destructor scopes alone are
   not universal field synchronization.

A same-owner-thread, call-free projection sample can satisfy the second form
without holding a mutex across a complete render pass, **provided** the native
borrow is valid on entry and foreign writer confinement is qualified. The current
study establishes the local no-call interval; it does not establish all-thread
confinement or qualify the broader material-submit seam. Thread-ID detection
alone notices a violation after it may already have started, so it cannot replace
that proof or actual exclusion.

Birth publication is early: handle stores, field initialization and list linking
follow map insertion; camera initialization continues even after list append at
`0x4a465d`. Load completion concerns a renderer subsection, not all scene/camera
fixups. Consumers must use a proven ready native seam or scope those construction
and load intervals through their actual final initialization, rather than making
insert-return a readiness event.

For TAA, copy the current node/camera identity and required transforms at the
qualified draw/sample boundary; history contains owned matrices plus incarnation,
load epoch and serial, and invalidates on mismatch. For shadows, capture the
required CPU transform/classification there and keep GPU resource ownership under
the independent documented D3D lease contract; a node token is not a vertex/index
buffer lease. For engine/body/material consumers, each pointer walk adds a typed
resource dependency whose free **and replacement** must be excluded for the whole
walk. These are positive consumer contracts, not a blanket renderer-enable flag.

## 5. Candidate scope ABI, unwind and implementation limits

| Entry span [start,end) | Displaced effects and live contract |
| --- | --- |
| Node `0x4a3300..0x4a3306`, 6 bytes | `push ebp; mov ebp,esp; and esp,-8`. ECX=value, no explicit stack arguments, ordinary RET. The aligned local frame is native-owned; enter before replay, never mistake its saved EBP for an argument. |
| Camera `0x4a4750..0x4a475b`, 11 bytes | Save EBX, EBX=ECX, save ESI, TEST camera `+0x1f4` bit 0x40. ECX=value, ordinary RET. ZF is live into JE at `0x4a475b`; all 11 bytes are needed for a five-byte entry detour with whole instructions. |
| Resource `0x4993b0..0x4993b5`, 5 bytes | `sub esp,0x14; push ebx; push ebp`. ECX=resource, ordinary RET. EBP becomes receiver at continuation. |
| Recovery `0x485c00..0x485c05`, 5 bytes | Five register pushes. ECX=engine, ordinary RET. A broad recovery scope still misses other resource destroy callers and independent payload release. |

All four spans are independently instruction-aligned and have no HIGHLOW operand.
Camera's 11-byte span exceeds one aligned qword; the others fit. Census exclusions
do not authorize live patching: retain the qualified quiescent installation and
rollback protocol. These are candidate scope boundaries, **not approved adapters**.
Preserve actual GPR/flags, incoming four-byte stack, LastError and FP/SIMD state;
no scratch permission follows from C ABI volatility. Camera displaced TEST must
reach its branch unchanged. Ordinary returns do not prove callback/unwind safety.

Destructors recurse (`0x4a33ba/0x4a348c`, camera children `0x4a47c6`), so use
per-invocation scope state and balanced nested counts. Store identity/address
values for exit; never read the destroyed object again. On foreign unwind,
keep readiness/coverage poisoned and drop only that invocation's active scope;
do not revive membership or assume its payload survived. These bodies establish
no local FS exception frame, and external calls can escape through enclosing game
handlers. Continued exceptions, nonlocal transfer and injected scope transport
require their own fixture qualification. The existing factory transport proof
does not automatically qualify four new entry stubs.

Missing evidence is now specific: complete current material-submit/camera sample
ABI and field-readiness; owner-thread or equivalent mutation exclusion; ordinary
field/replacement writers for each requested payload; callback closure for
`0x512410`, `0x512490`, `0x4da6b0` and the actual external release targets;
implementation/qualification of whichever additional scopes the owner selects.
No arbitrary whole-program corruption audit is required. The verifier deliberately
reports reader admission, payload readiness and new scope transport **false**.
Performance expectation is one gate/epoch check and bounded copy per semantic
sample, with no allocation or engine walk in the fast admission check; actual
cost must be measured with the eventual implementation, not inferred from this
offline verifier's approximately one-second run.

Orchestrator review reproduced the verifier (0.897 seconds reported), inspected
its normal-CFG dominance and birth-failure separation checks, and independently
decoded the node/camera payload frees and subsequent map removals from their
function entries. The resource removal selects engine `+0x14`, while node and
camera removal select `+0xc`, matching the stated distinction. No blocking
finding in this bounded study. The schedule model remains specification evidence;
it does not implement a gate or qualify the proposed scope adapters. Callback,
thread-confinement, readiness and renderer-consumer gaps above remain open.
