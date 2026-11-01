# FL nozzle roots at the Scene boundary

Bounded offline native/source study, 2026-10-29. Input `/mnt/c/X3FL-orig/X3FL.exe`,
2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`;
preferred addresses use base `0x400000`. **[s]** = measured static operand/source
fact; **[i]** = inference from those facts and the named existing closure;
**[u]** = unestablished. No production edit, build, native/Wine execution or game
launch. Owning direction: [native nozzle placement](../../architecture/farnhams-legacy-engine-effects.md).

**The existing `0x48b929` pre-compositor callback is a permitted, call-free
root/list/base-field copy boundary for roots actually submitted by the accepted
main Scene on the healthy native path [i].** This includes undrawn direct
children. Finalize the owned ring there even if the plume GPU stage will not
run. Later native camera work cannot change that owned copy. The exact late-root
concern is resolved by the *complete* existing route and FL-camera gates, not by
`engine_node_gather` in isolation [s/i].

**A missing native seam remains a separate coverage case [u].** The native CFG
permits all sorted cameras to have layer <=17. An existing device EndScene
interception supplies a real pre-playback storage-read boundary, but does not
automatically retain main-camera admission or main-view basis equality. It is a
concrete fallback candidate with the limits in section 2, not permission to use
next Present or to reopen an expired camera record.

## Reproduction and evidence scope

```sh
python3 verification/results/fl-port/nozzle-scene-window/produce.py
python3 -m py_compile verification/results/fl-port/nozzle-scene-window/produce.py
```

[Producer](../../../verification/results/fl-port/nozzle-scene-window/produce.py)
and [derived contract](../../../verification/results/fl-port/nozzle-scene-window/contract.json):
**20 complete bounded regions, 4,368 instructions, 114 operand anchors, seven
rejected operand/edge mutations with all image/region hash gates bypassed, and
21 source clauses** [s]. Four camera-list models and four composed gather-gate
cases state the checked CFG/source consequences; they are not executed render
fixtures. The threshold comes from the actual decoded CMP operand. The producer
emits hashes, addresses, counts and conclusions, never executable bytes or full
listings. Its source hashes bind the reviewed consumer gates, not an activation
claim. Raw Capstone/Ghidra listings and the read-only research database remain
local under `~/.cache/x3m-port/nozzle-scene-window/` (Ghidra accessed it through
`/tmp/x3-nozzle-scene-window-20261029`). Project tools: `inspect_x3.PE32`, Capstone
5.0.7 and `X3DecompileFunctions.java` / Ghidra 12.1.4. Instruction operands take
precedence over inferred decompiler types.

Reuse, rather than reopen, the qualified [material owner/command closure](material-submit.md#3-actual-borrow-readiness-writers-and-reentrancy),
[own-ship graph closure](own-ship.md#3-why-the-existing-material-seam-can-own-this-copy),
[load exclusion](ui-load-readiness.md),
[deferred close](ui-render-exclusion.md), and
[typed recovery/playback continuation](ui-render-mutation.md).
This check extends their native graph borrow to the named later boundary. It
is neither a whole-program computed-call theorem nor support for foreign
threads, private-address calls, altered callbacks/vtables or corrupted lists.

## 1. Root, list and Raw readiness/lifetime at `0x48b929`

### Construction, ownership and retirement

| Native anchors [s] | Contract |
| --- | --- |
| `0x4a2203`, `0x4a2280` | Allocate and zero the complete 0x270-byte ordinary node. A failed allocation/map insertion returns zero; an actually submitted healthy node has completed successful construction. |
| `0x4a22a8` | Publish handle at node+0x28. This alone does not mean initialization completed. |
| `0x4a22ab..0x4a22b8` | For root R, initialize R+0xc=head=R+0x10, R+0x14=tail-link=R+0xc, and **DWORD[R+0x10]=0**. The embedded terminal element is R+0x10; only its next word is a list payload. |
| `0x4a57ec/0x4a57f3`, then `0x4a5801..0x4a5809` | Reparent unlinks child from old intrusive list, inserts it before new parent's R+0x10 sentinel using R+0x14 tail, then sets child+0x18=parent. It invalidates new parent radius at `0x4a580f`. ECX=child, stack argument=parent, RET 4 at `0x4a5856`. |
| `0x4a5797/0x4a579e`, `0x4a56cd/0x4a56f6` | Detach/scene attachment use the same next/previous links; scene attachment clears parent then sets scene. These synchronous operations can retain node serial. Serial equality is not topology immutability. |
| `0x4a33b0..0x4a33c4` | Destructor walks root+0xc, checks next before payload, saves successor before recursively destroying each child. |
| `0x4a34de/0x4a34e6`, `0x4a34ef`, `0x4a35b3` | Unlink this node, remove its handle from B3D map, then free ordinary-node storage. The embedded sentinel lasts exactly as long as its owner. Destructor has earlier callbacks/resource cleanup: its body is not a borrow interval. |
| `0x4a5472`, `0x4a54f0`, `0x4a556a` | Scene destructor destroys top-level node trees, then cameras and scene. A submitted node's scene owns its linked ancestry, siblings and their embedded child-list sentinels. |

For an actual submitted N, `R=N.parent ? N.parent : N` is a live immediate
parent or N itself, not a map-discovered allocation [i]. It need not be the
topmost ship ancestor. Apply the current root policy unchanged. Its direct
children are live members of that same healthy native ownership graph. No
native list node has been replaced by a separate heap sentinel. Inspect next
before any child flags/handle/Raw loads; never inspect the terminal as a node.

The relevant sorted path performs world preparation/activation/selection at
`0x48ba3a → 0x494b00`, `0x48ba43 → 0x495670`, and
`0x48ba4c → 0x497720`; immediate submissions are `0x48ba73/0x48ba7f →
0x4978e0`, deferred submissions finish at `0x48ba8d → 0x497680`.
The latter projects then calls material wrapper at `0x497702`; it follows a
separate engine command-record list, not the graph being sampled [s].

Between these submissions and the next sorted partition at `0x48b929`, the
named pass/traversal/deferred/camera-post paths do not unlink or destroy a
submitted main-scene root or its children [s/i]. Camera-post helper
`0x48aeb0 → 0x4a3fd0` is a screen/occlusion test, not node destruction.
The actual later node destruction at `0x48b516`, and eventual playback scene
destruction, are distinguished below. The already-qualified owner executes
native command/model/reparent/update work synchronously; window/audio/recovery
callbacks do not invoke that command stream, and SC_CLOSE teardown is deferred.
Thus no supported asynchronous graph writer can interleave a call-free copy
at this boundary [i]. Load/quarantine/unwind/non-owner/nested unsupported paths
grants no such entry borrow. Page readability or post-copy serials do not
supply this argument.

### Off-frustum children and the exact field set

The native whole-scene preparation **precedes every camera's frustum test**.
`0x48b87c → 0x4951f0` copies every scene top-level node's local position/basis
into world fields at `0x495226/0x495231/0x495247`, then calls recursive
`0x494370` at `0x49524e`. That recursive function walks every child from +0xc,
tests only the terminal next word, composes position from local +0x30 and parent
basis/scales, writes +0xb0, and either copies or composes the entire basis
+0xc0/+0xd0/+0xe0. Recursion at `0x494517` is unconditional for every linked
child; no frustum/renderable gate occurs there [s].

The later camera preparation `0x494b00 → 0x4947b0` also recurses over all
children (`0x494adc..0x494af1`) before `0x497720 → 0x495e50` selection.
Selection's failed frustum test (`0x495fd3`) clears renderable bit 2 at
`0x495fdc`, then still visits descendants via `0x495f27/0x495f37`.
Traversal `0x4968c0` skips the current node's material/billboard work on bit2=0
at `0x4968d4/0x4968db` and reaches child recursion at `0x497550` [s].
An off-frustum child therefore has prepared **base/world** pose, rather than
uninitialized pose just because it did not draw [i].

| Permitted copied fields | Readiness evidence / restriction |
| --- | --- |
| Root +0xc, root +0x28; optional root +0xa4 | Constructor/list/handle above. Radius starts -1 (`0x4a22f4`), is invalidated on updates/reparent and may be nonpositive. Copy only cached word; never call recursive radius writer `0x4a39b0`. |
| Each linked element +0 | Established intrusive successor; read terminal next only. Bounds/cycles/overflow remain refusals, not measured native maxima. |
| Actual child +0x18/+0x28 | Zero-initialized and reparent/handle-published; retain parent back-pointer and owned serial/domain validation. |
| JET child +0x70/+0x80/+0x84/+0x88 | +0x70 initially zero (`0x4a2308`), three scales initially 0x10000 (`0x4a2352/5c/66`). Model setter/jet drive complete synchronously before drawing; consume actual current scale, preserving existing guards. |
| JET child +0xb0/+0xb4/+0xb8 | All-child world preparation above; +0x130 bit0x400000 permits camera-relative position refresh in `0x4947b0`. Copy exactly three components; MOVUPS padding is not a fourth position value. |
| JET child basis +0xc0/+0xc4/+0xc8 and +0xe0/+0xe4/+0xe8 | Constructor initializes basis (`0x4a2335/0x4a234b`); whole-scene recursion refreshes it independently of visibility. Visible camera-facing nodes can subsequently overwrite it. It is initialized/live, not invariant across cameras. |
| Actual child +0x12c/+0x130; JET model +0x144 | Zero/default initialization at birth, +0x144=-1 (`0x4a22cc`). FL classifier/model/drive contract is [engine-effects sections 2–4](engine-effects.md#2-node-traversal-classification-and-throttle). Selection mutates visibility/LOD flags; retain JET/hidden/scale/parent policy. |

This read set follows no mesh, resource descriptor, script, simulation object,
camera or scene payload pointer. Context scale must already be an **owned**
matching main-camera value. After fresh common admission/owned binding and root
identity comparison, the entire finite root+children copy contains zero calls,
allocations, logging/timing/page queries, COM operations or observer locks.
Recheck admission/binding and validate copied identities through the existing
owned metadata API afterward. These checks validate identity; the preceding
healthy graph/owner argument supplies permission **before** the first load.

The existing compositor redirect is one complete five-byte CALL
`[0x48b929,0x48b92e)` with no stack arguments. Reuse its qualified x86 transport,
not another patch: preserve all GPRs/EFLAGS, FP/SIMD, LastError and four-byte
incoming-stack ABI; keep native call exactly once and foreign-SEH cleanup.
ESI remains next camera, EBX loop bound and EBP native SEH frame. Neither these
registers nor the native done byte is collector scratch. Copy before
`restore_bindings`, observer/logging callbacks or injected GPU operations.
On failed admission/copy/unwind, publish no invalid scratch and never resume a
walk over retired storage. Existing normal/outward-unwind quarantine applies;
this study adds no recovery catch or exception continuation.

## 2. Late subsets, no-stage and genuinely missing seam

### What can still be gathered after normal collection?

`engine_node_gather` alone checks Scene, recent/own root and remembered camera
handle. Unknown remembered view admits every handle. That is insufficient to
reason about actual call reachability [s]. For the minimal FL extension, retain
all existing successful routed-scope gates and insert only **after** the FL
owned-camera agreement; do not place FL gather before that test.

Actual `evaluate_draw` reaches routed `sample_scope` only after `scene_bound()`,
which explicitly requires `!hook_scene_end`. Its diagnostic hull-emitter
`sample_scope` call is reached through `prepare_hull_gain`, which requires
`route.scene`; that is also set only after the same gate. `sample_scope` then
requires FL `fl_motion_scope::agrees`: exact camera pointer, handle and serial,
complete domain and same native pass; unknown remembered engine view does not
bypass this [s]. FL gathering itself remains disabled in production at this
checkpoint; this is the behavior-preserving insertion condition, not a claim
that it has been enabled.

| Later native subset [s] | Root/field relevance and current gate result [i] |
| --- | --- |
| Sorted layer >17 | Runs ordinary preparation/activation/selection/submission, potentially for the same scene and roots. Distinct camera identity fails FL agreement; after an admitted seam `scene_bound()` already refuses. Native fields can change, but no further **routed eligible root** is admitted by these complete gates. |
| Auxiliary `0x48bbef` | The complete loop has three direct calls: preparation `0x48bbe6`, activation `0x48bbef`, tint `0x48bbfc`. It has **no `0x497720/0x4978e0/0x497680` material traversal**. Tint is XYZRHW/DIFFUSE DrawPrimitiveUP, not a native node submission. Preparation can refresh camera-relative child positions (0x130 bit0x400000) and selection flags, even for an already gathered root. Current correlation refuses auxiliary origin. |
| Special `0x48bca9` | Separate engine+0x64 scene/camera, excluded from ordinary camera-array collection. Initializer `0x48af40` reads source nodes through camera+0x2a0 rows but obtains each generated node from row+0x3c slots. Empty slot allocates at `0x48b09d`, sets model at `0x48b0a9`, **attaches to engine+0x64 scene at `0x48b0d9/0x48b0dc`**, then publishes at `0x48b0e8`. Existing slots are the same special nodes. Their writes/destruction (`0x48b516`) concern those proxy nodes, not source root row+0xc. Special camera cannot equal an accepted ordinary camera in a healthy intrusive scene list. Current correlation refuses special origin. |

Native camera-facing branches test **+0x12c**, not the JET classification word:
0x20 at `0x496908`, 0x4000 at `0x496cd3`, 0x10000000 at `0x496d33`, and
0x200 at `0x496dce`. Stores include `0x496f82/0x496f88`. Existing nozzle
guards do **not** exclude these flags: for example +0x130=0x4000001 and
+0x12c=0x200, unit x/y scales and valid z/parent satisfy them [s/i]. No asset
census establishes whether stock FL jets exercise this combination [u]. Do
not infer main-view basis equality from unchanged handle/serial, or extend the
base-world readiness proof into a claim that an undrawn billboard received a
camera-facing rewrite. The current node-source policy uses its prepared base
basis when its own material is absent.

A normal valid Scene hook therefore finalizes CPU records once, **independently
of stage attach, TAA/HDR resolve, GPU success or stock glow**. The next-frame
light-only fallback consumes that owned ring without native reads. This closes
the stage-skipped case; it does not require a later camera's fields to equal
those collected at the accepted main Scene [i].

### Missing seam and the real existing EndScene boundary

Native sort/loop uses layer +0x29c ascending; `0x48b920` compares with 17,
`0x48b927` skips the call on <=17, and only the first >17 entry triggers it.
There is no unconditional tail compositor call. A native list with layers
`{0,1}` can perform ordinary background Clear3/draw then main Clear2/draw and
never execute `0x48b929`; existing main-role selection does not require a later
layer. This is a **concrete native CFG possibility**, not a measured healthy
flight camera population [s/i/u]. It is enough to reject an unconditional
static guarantee that every initially accepted main Scene receives this seam.

The actual existing final device boundary is:

`0x48bd9e → 0x4e7170 → 0x4e7183 [device vtable+0xa8]`,
with native return PC **`0x4e7189`**. This is documented D3D9 **EndScene**,
confirmed by device provenance (`*0x5aaf94 → +0x18 → [0]`), explicit stack
this and slot 42. It executes after sorted, auxiliary and special submission,
before optional capture work, final scene flag cleanup and
`0x48bded → 0x48f570` playback destruction. The root graph from ordinary
submissions is still allocated under the same common closure [s/i].
`capture.cpp::end_scene` already intercepts this method; collection would have
to occur **before its current `restore_bindings`/`before_end_scene`**, which
can make injected device calls. No new native detour is necessary to recognize
this boundary.

A usable future fallback must discriminate **exact native return PC
0x4e7189**, current outer common pass/owner, selected actual device, unchanged
load/incarnation/registry/root identities, request/Reset/resource/frame/ring
state, and no already finalized ring. The earlier cube-transition EndScene at
`0x48b991` returns to **0x48b997**; injected EndScene calls and unrelated
application callers are not this boundary. Preserve the existing WINAPI
callee cleanup, HRESULT, CPU/LastError/FP state and exception transport. There
is no native catch in `0x4e7170` to convert outward failure into resumed
rendering. This is a CPU-copy boundary before forwarding EndScene, not a GPU
resolve signal or permission to drain closes.

**Limits that prevent claiming full fallback qualification now:**

* `camera_state_fl::read(main_key)` accepts only the latest activation. Late
  auxiliary/special publication replaces it. `fl_render_correlation::observe`
  denies any live nonordinary origin and requires exact current scene camera.
  Ordinary drawing itself sets special-scene active at `0x48bb3c`, before the
  late special activation. An initially accepted Scene may thus lose current
  correlation before final EndScene even without a new root or load. The
  fallback cannot reuse those *current* gates unchanged or repair them by a
  native camera dereference. Any retained context scale must be copied owned
  data from the earlier main Scene, with the actual invalidation reason and
  identities respected; no new receipt framework is proposed here.
* Later ordinary cameras may rewrite an eligible child's basis; auxiliary
  preparation may change camera-relative positions. EndScene storage liveness
  does not restore the main camera's values. AP's historical no-stage append
  also reads **late/current native fields** at the next frame boundary; it
  promises neither frozen main-view basis nor a current FL camera borrow.
  The normal FL Scene copy improves that placement. Calling final EndScene
  fallback equal to that main-view copy would add an unproved guarantee.

Thus **normal-seam storage permission and late-root coverage are established
under the named existing gates [i]; missing-seam full feature coverage remains
open [u]**. The exact existing EndScene location makes the next decision
bounded: choose and qualify the intended late/current-field fallback semantics
and owned main-context retention, or retain explicit missing-boundary refusal.
No new hook, camera reconstruction, all-root arena or ownership framework is
authorized by this result. Do not silently treat missing transport as a skipped
GPU stage or claim all healthy FL flights are covered.

## Cost and remaining acceptance

Native work stays once per admitted root, one fixed 256-child scratch, with
no per-draw walk, allocation, page query or native radius recursion. Existing
256-root/256-child, 1,024-record and 2,048-Seen caps and draw/far/node precedence
remain policy. Raw scratch is 18,432 bytes (256×72, inferred), not a 256×256
arena. Matching copied identities can reuse bounded owned batches outside the
read interval; no observer lock spans native loads. Optional final EndScene
recognition adds a constant caller/pass/device check per intercepted EndScene,
not another per-draw graph validation. None of these figures is an FL timing or
population measurement.

Required implementation evidence remains compiled zero-call native intervals,
proper root/child/sentinel bounds, zero reads on refused admission and later
light fallback, native synthetic unwind/Reset/reuse/overflow and caller/device
negative controls, and one consolidated user flight including off-frustum
nozzles and skipped-stage lighting. Native Windows runtime behavior is not
verified; game-private hooks remain x86 ABI qualified and the device boundary
uses public D3D9, with no Wine-specific dependency. Missing-seam policy and any
retained main-context semantics require parent decision before activation.
