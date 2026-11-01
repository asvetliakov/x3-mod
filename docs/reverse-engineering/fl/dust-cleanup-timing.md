# FL failed-newborn dust cleanup timing

2026-10-29; targeted static RE at source checkpoint `955eee56`. This note
answers the earlier-cleanup prerequisite in item 2 of the
[dust/light ledger](../../verification/farnhams-legacy-dust-light.md).
The [dust/light hook contract](dust-light.md) remains the owner
of the site, release ABI and staged transport. No production change, build,
native/Wine execution, game launch, installation or release execution occurred.
The existing production admission gates remain unchanged and closed.

## Result and limits

For the supported healthy native routes, a nonnull node returned at `0x420956`
remains a live, registered, never-bound, unattached newborn at each of the three
failure arrivals to `0x420ac4`. Releasing that node there does not invalidate
the updater's completed scene-list traversal, the outer cockpit-map iterator,
or the VM caller's argument/result state. The full native release is retained.
Its unconditional external-record scan still runs; on this newborn route the
matching-record recursion is not entered.

This is a **derived static timing contract**, using the established native
resource/recovery and callback contracts below. It is not a claim that release
has been executed or its cost measured. The producer's direct graph is
corroboration, not an indirect-call proof. Conclusions assume ordinary native
callee return, the qualified fixed callback publications, healthy intrusive
lists and existing serialized native ownership. Foreign callback replacement,
private-address entry, corrupted stale external records, concurrent registry
mutation or nonlocal continuation after fatal cleanup remain outside that
contract. No AP equivalence or cockpit-update render-pass admission is assumed.

The static evidence plus the already passing native adapter transport fixture
is sufficient input to review staged startup integration. It does not establish
actual FL release behavior or flight acceptance, and does not itself open
`Admission.real_release`. No broad 13-slot native-destructor harness is justified
by the graph present at this cleanup point.

## Reproduction and evidence

Input `/mnt/c/X3FL-orig/X3FL.exe`: 2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Preferred image base is `0x400000`; hashes are evidence provenance, not runtime
admission gates. Resolve production addresses through the admitted loaded base.

The [producer](../../../verification/results/fl-dust-timing/verify.py),
[Ghidra query](../../../verification/results/fl-dust-timing/DustTiming.java) and
[compact result](../../../verification/results/fl-dust-timing/verification.json)
retain 13 independently decoded regions, **2,646 instructions, 84 operand
assertions, three failure-prefix CFG checks and eleven negative controls**.
The prefix checks have 58/64/68 reachable instruction nodes, respectively;
these are static graph counts, not executed instructions. Eight negatives
redecode altered operand bytes without relying on a whole-region hash. Two
others substitute an earlier release/bind in the prefix graph and are rejected.
The final negative removes one TARGET row; the parser requires exactly the four
root/mode pairs and all thirteen target keys for each, rejecting duplicate,
unknown or missing records rather than treating absent evidence as absence.

Ghidra 12.1.4 used the existing `FLRegistry/X3FL.exe` database read-only.
Raw listings/decompilation stayed outside the checkout in
`~/.cache/x3m-port/fl-dust-timing-local/`. Reproduce from the main checkout,
substituting the isolated checkout's script directory when appropriate:

```sh
~/ghidra_12.1.4_PUBLIC/support/analyzeHeadless \
  "$PWD/verification/results/fl-port/registry-writers/ghidra" FLRegistry \
  -process X3FL.exe -readOnly -noanalysis \
  -scriptPath "$PWD/verification/results/fl-dust-timing" \
  -postScript DustTiming.java /tmp/fl-dust-cone.local.txt
python3 verification/results/fl-dust-timing/verify.py \
  --cone /tmp/fl-dust-cone.local.txt
```

The Ghidra query asks only whether named node birth/free/bind/attach/reparent,
render, collection, KC and teardown targets are reachable by direct calls from
body loading or recovery. CRT/external edges stop at `0x51df00`. With ordinary
fatal cleanup included, the body-loader graph has 493 functions and 1,584 direct
edges and reaches node operations through `0x49db00 → 0x4762d0`. Pruning that
already reviewed **normal nonreturning fatal path** leaves 204 functions,
712 direct edges and no queried node/lifecycle/render/KC target. Recovery has
107 functions, 309 direct edges and no queried target with or without pruning.
There are still 169/57 indirect CALL sites in those respective cones; their
existence is explicitly recorded. The typed callback proofs are reused, not
replaced by these counts or by a claim that all calls are direct.

## 1. Newborn state and the three failure prefixes

Constructor `0x4a2200..0x4a23e8` zeroes the ordinary 0x270-byte allocation,
inserts it in the render registry `R+0xc` at `0x4a229b`, writes its handle at
node `+0x28`, initializes an empty child sentinel at `+0xc`, sets model ID
`+0x144=-1`, sets collection/control flags `+0x130=0`, and links it into the
**unattached-node list** rooted at `R+0x28` through the `+0x2c/+0x30`
sentinels (`0x4a23ac..be`). Failed registry insertion frees the allocation and
returns zero; it cannot reach the normal nonnull return. This reuses the
[birth/retirement contract](registry-value-lifetime.md).

Those registry and unattached-list publications are real pointer escapes.
They are not scene attachment, parent binding, a VM pointer publication, or an
external `record+0xc` reference. Successful constructor return establishes
scene `+0x1c=0`, parent `+0x18=0`, empty children and zero optional GPU/VM/array/
string fields. Its matrix initialization does not bind a body or scene.

From `0x420956` to the first bind, ESI retains the newborn. The complete
prefix has only five instructions mentioning ESI: capture, null test and
position writes to **`+0x30/+0x34/+0x38`**. No register alias or stack/global
slot receives the newborn pointer there. No call receives it as an argument;
ESI is a nonvolatile register preserved by the native callees.

| Arrival | Calls reachable before that branch | Ownership consequence |
| --- | --- | --- |
| Invalid ID, `0x420a19 JS → 0x420ac4` | Optional CRT random `0x532dcf` | No engine/node mutation call after birth; model, ownership and optional payload fields stay at newborn values. |
| Already failed body, `0x420a2f JNE → 0x420ac4` | Random, `0x486910` | Lookup takes **stack body ID EDI**, returns with `ret 4`. Its complete body is call-free: maps/bounds the ID and reads body-record failure word `+0x10`; it cannot free, attach, bind or retain the newborn. |
| Body load returned null, `0x420a3e JE → 0x420ac4` | Random, failed-word lookup, `0x4a1a40` | Loader takes **ECX=body ID EDI**, not the newborn; ESI is preserved. Its resource/recovery/callback distinction is covered next. |

Null allocation at `0x42095a` reaches the same tail with ESI=0 and is never
released. For the three nonnull failures, all normal control reaches the tail
before bind `0x420a47`. Reparent is absent. Scene attach is later at
`0x420abf`; its normal body unconditionally writes the valid supplied scene to
node `+0x1c` at `0x4a56f6`. Successful attachment therefore fails the staged
zero-scene guard. The source camera/scene inputs retain the native healthy
caller precondition; a null/corrupt scene is not a new pass-through contract.

IDs at least 100000 bypass the preload test and proceed to native bind/attach;
they are not another failed-newborn arrival. A bind's own failure result is
ignored by the original updater, but the subsequent normal scene attachment
still excludes it from this guard. Do not broaden the policy to arbitrary
unparented or partially initialized nodes.

## 2. Body loading and recovery do not acquire this node

`0x4a1a40` saves the numeric ID, looks in **resource map `R+0x14`**, consults
the failed-body table, obtains the effective body name, reads the payload through
`0x506d30`, and dispatches BOB/BOD parsing through `0x49ae00/0x49db00`.
Empty/failed resources are marked failed and may be destroyed through
**resource destructor `0x4993b0`**, not node destructor `0x4a3300`. Resource
map/table entries and mesh/GPU payloads are different owners from the newborn's
render-map value. Reuse [resource selection](resource-callers.md)
and [resource value lifetime](registry-value-lifetime.md#2-recovery-destroys-resources-including-data-beneath-still-live-owners).

Allocation failure can invoke the registered recovery callback
`*0x5aae24=0x47b500` under guard `0x62de10`. It calls `0x485c00` and
`0x512410`; the guard suppresses recursive **allocation recovery**, not arbitrary
native entry. `0x485c00` really visits the unattached list containing the newborn
(`0x485c42..5d`). It calls marker `0x485b80`, which reads model `+0x144`,
increments a body usage count when the mapped ID is valid, and recursively
visits children. It performs no node free, bind, attach, list insertion or
external-record publication. For this node, model=-1 fails its nonnegative
slot test and the child list is empty. Recovery does not convert registry
visibility into another owner of the node.

The later recovery loop destroys unused **resources**. Its mesh/effect/texture
operations and registered state-manager callbacks remain the typed frontier in
[render mutation](ui-render-mutation.md#allocation-recovery-actual-objects-callbacks-and-finite-closure).
That review identifies the D3D/D3DX interfaces and resource ownership, including
texture demand loading, rather than treating every vtable release as a node
destructor. Those callees receive resources, names, buffers or API interfaces;
they are not handed the newborn. Usage marking's only node entry does not
write/retain the node pointer.

There is another concrete callback path worth retaining: the optional loaded-
body branch calls **audio update `0x4b42e0` at `0x4a1d4b`**, before later
empty-mesh validation can still return null. Audio update calls VM result slot
`VM + type*24 + 0x34` at `0x4b4416/0x4b4462`. These are the already traced
fixed `0x4c1a40` / `0x478910` result/ready publications, not synchronous KC
execution; see [window/audio endpoints](ui-render-exclusion.md#2-window-callback-endpoints).
The task/token arguments are not this unexposed newborn pointer. Do not omit
this actual callback merely because the ordinary missing-file arm is shorter.

The parser also reaches fatal cleanup `0x4762d0`, whose normal ExitProcess path
does not return to the dust tail. Its node/scene frees therefore must not be
misclassified as earlier frees on a returning failed-load path. Exceptions or
foreign continuation which resume after that nonreturning contract are not
qualified by this analysis.

These conclusions do **not** borrow render-pass admission for cockpit update.
The ordinary cockpit call can occur outside any render scope. Existing window
callbacks publish input/next-location or ready state; SC_CLOSE's returning
cutscene flush can destroy scene-owned objects, but does not acquire this
unattached newborn through a scene. Full manager teardown is the nonreturning
fatal route above. No new message pump, media callback or native resource call
is added at the proposed cleanup point. Existing longer camera/cockpit borrows
are not made into leases by this note, and their native failure behavior is
not claimed to have been repaired by dust cleanup.

## 3. External records are not constructor registration

Native release always scans `R+0x18`, each scene's `+0x1c` camera list and each
camera's `+0x2a0` record chain. It compares **record `+0xc`** with the victim at
`0x4a3460`. A match unlinks the row, recursively releases thirteen pointer
slots at `row+0x3c`, and frees the row. Zero newborn payload fields alone do
not skip this scan, and the full native code must remain intact.

The relevant positive publication is **`0x4972df: row+0xc = ESI`** in node
traversal `0x4968c0`. The adjacent row `+8` is the camera, and the row is linked
into camera `+0x2a0/+0x2a8` at `0x4972f2..30b`. The source is the current
scene-traversed node, not the render registry's most recent insertion and not
the unattached-list head. Camera construction/restoration initializes the
record-list sentinel; it does not invent a record for every registry value.
The [special-scene study](nozzle-scene-window.md#2-late-subsets-no-stage-and-genuinely-missing-seam)
separately establishes that `row+0x3c` holds generated special-scene proxy nodes.

The newborn has no scene and no parent, and does not enter a scene traversal
on any returning failure prefix. Recovery's unattached-node marker does not
call that traversal or row publisher. Neither body-ID lookup/loading nor the
fixed returning callbacks receive, look up or attach this newborn for traversal.
Thus no matching row is introduced between its successful fresh allocation
and the seam. Healthy native retirement removes old matching rows before the
old allocation is freed; a pre-existing stale row containing a recycled address
would violate that existing ownership precondition, not establish a new valid
birth reference. No claim covers arbitrary malformed graph aliases.

For this restricted newborn, the native scan therefore takes only no-match
branches. Recursive row cleanup and its possible payload/VM callbacks remain
part of the general destructor but are not part of this added cleanup's
reachable healthy state. This is the reason the broad all-thirteen-slot fixture
is not the prerequisite for earlier cleanup. It is not permission to optimize
away the scan or broaden the guard.

## 4. Enclosing iterators, held state and continuation

The updater's existing scene-node loop saves its next node at `0x4207bb`,
advances it at `0x42092d..36`, and reaches fill only after it has exhausted that
loop. The newborn is then created on the separate unattached list. No live
iterator in that earlier scene walk points into the newly inserted node's
registry entry or unattached-list links.

The ordinary caller `0x421f50` finishes its own scene-node transform loop at
`0x422f60..65`, then pushes cockpit fields `+0x58/+0x54/+0x60` and calls dust
at `0x422f70`. Its continuation reloads cockpit `+0x10`, not the old node
iterator. The enclosing enumerator `0x41e550` uses **cockpit registry
`*0x5a82e4`**, calls the cockpit update at `0x41e5b0`, and reloads that map at
`0x41e5b5`. This is distinct from render registry `R+0xc`; releasing one newborn
and its render-map hash node cannot erase the enclosing cockpit entry.

The second direct caller is command **0x47** of native dispatcher `0x430540`
(table entry `0x4325a4 → 0x431f53`). It resolves two camera handles and a
sector object before passing the three dust arguments at `0x431f8e..91`.
After return it reloads VM `*0x5aa174`, uses the original dispatcher frame EBX
and posts its result through `0x4c1940`. It holds no newborn pointer or render-
map enumeration cursor. The dispatcher has its own SEH frame; a normal return
is not evidence that arbitrary foreign unwind can be resumed safely.

The unchanged release ABI is ECX=node, no stack argument, plain RET;
EBX/ESI/EDI/EBP survive its native epilogue, EAX/ECX/EDX are scratch. The staged
failure-only PUSHAD/aligned FP save and LastError wrapper protect the updater's
incoming state; only EBX's intentional fill-count output changes afterward.
The original CMP overwrites arithmetic flags before JL. The existing native
fixture covers that recording-release transport, not real native-release
exceptions or CrossOver FP restoration. A foreign unwind through the naked
frame is not qualified as a safe return to the tail.

Recovery's list/usage iterators have returned before `0x4a1a40` returns and
the dust seam runs. The marker itself has no callback apart from recursion;
the fixed recovery/audio callbacks do not synchronously reenter dust/KC to
suspend one of those iterators around the added release. The staged adapter
retains no global current-node pointer, observer scope, mutex or native lease
across release. Its only C++ release wrapper preserves LastError and calls the
native ECX-argument release. Existing render-map observer enter/leave bookkeeping
uses short internal locks and does not keep its lock over native forwarding.

On the restricted no-match newborn route, release bypasses optional payload,
notification, child, scene-detach, array and string paths. It unlinks this node,
removes its render-map hash entry, clears 0x270 bytes and frees the node. Those
remaining native operations introduce no resource-loader/recovery/VM callback.
Accordingly there is no new recursive cleanup callback which can invalidate
the caller's live cockpit/sector/camera objects or enclosing iterator. General
matched-record recursion is not silently assumed harmless; it is excluded by
the earlier ownership/publication facts for this specific node.

The staged stub then sets EBX to `[restored ESP+0x18]`, making the original
CMP/JL fall through to `0x420ace`. From there until ESI is redefined as an
integer at **`0x420b28`**, the complete continuation contains **no ESI use**.
No local alias of the newborn was published by the prefix. That native
`0x420ace..0x420b28` continuation is checked statically here. The existing native
transport fixture poisons the recording-release node and executes CMP/JL into
its capture tail; it checks one release/end per failure and the
register/stack/FP/LastError sentinels. It does not execute that native
continuation or demonstrate the actual native destructor's flight behavior.

## Integration prerequisite and performance

Review may use this bounded static contract to qualify the earlier-retirement
input to staged startup integration, together with the existing adapter fixture
and startup/rollback ownership requirements. Keep full native release, exact
FL profile/ABI and three-failure-only behavior. There is no newly identified
returning callback/newborn-alias witness requiring an extra reentry fixture.
Do not turn the direct-cone counts into blanket writer/exception qualification.

Actual release behavior and cost remain user-flight acceptance in ledger item 4.
The added release still scans every relevant scene/camera/record chain even
when there is no match. Its cost is inferred linear in that native population;
no timing is measured here. Failure-only cleanup ends fill for the invocation,
so it cannot release repeatedly while retrying the same missing body in that
fill loop. Collect actual release timing and unattached-node counts in the
already planned consolidated missing-resource diagnostic flight. No new
per-iteration allocation, registry traversal in mod code, lock, observer bus,
whole-image validation or general native-engine replay harness is needed.
