# FL destination Ready at SETA input entry

2026-10-29, offline reconstruction of the remaining
[SETA Ready dependency](seta.md#5-option-boundaries-and-remaining-ready-dependency).
**The clean FL input-entry phase can supply AP's bounded destination-Ready
predicate without a new hook or lifetime framework.** Bind the native list and
row reads, pair them with the existing common owner/load/close readiness
handoff, and perform a fresh call-free copy at the mapped shared input entry.
This closes the native contract needed by gate restore; it does not activate
FL SETA or qualify resource-creation fog prefill.

No production edit, PE build, native executable, Wine, game launch or runtime
capture is included. The producer performs PE/KC disassembly and memory-only
negative controls; its small Ready model executes no game code. All addresses
are preferred x86 VAs at base `0x400000`; use loaded-base bindings and HIGHLOW
expected operands, not preferred absolute addresses at runtime.

| Exact local input | Bytes / identity |
| --- | --- |
| `/mnt/c/X3AP-orig/X3AP.exe` | 2,153,984; SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| `/mnt/c/X3FL-orig/X3FL.exe` | 2,804,736; SHA-256 `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` |
| Clean FL decoded `L/x3story.obj` | Build1636047691; SHA-256 `dbb732716e1acab6b8cc7f87f5d31324fb4764dfc22d1cec808749a4f45ec7b9` |

[Producer](../../../verification/results/fl-port/prefill-ready-contract/produce.py),
[record](../../../verification/results/fl-port/prefill-ready-contract/contract.json):
**37 native regions / 1,274 instructions, 81 semantic anchors, nine complete
immediate caller sets, eight KC dominance witnesses, two rebases ×88 HIGHLOW
operands, 42 rejected image/edition controls, two rejected KC controls and
14 scalar Ready cases**, measured. The KC CFG is intraprocedural: calls may
fail, yield or invoke callbacks. It establishes the named local ordering, not
whole-task completion or a minimum prefill lead time. Raw bytes/listings stay
local and untracked. Digests bind evidence, not production admission.

```sh
python3 verification/results/fl-port/prefill-ready-contract/produce.py \
  --story /home/asvetl/x3-mod/verification/results/fl-port/ui-graphs/x3story.obj
```

## 1. What SETA actually requires

`seta_restore.cpp::EngineProbe::ready` uses only
`fog_prefill::walk.status == Found` and its `node`; it discards the sample.
The unchanged state machine then requires **player parent == node**, waits
**two presented frames**, checks the parent again and requires controlled
object == current ticket ship before StartFastForward. Preserve its existing
factor/VM/EH/expiry/player-rebind cancellation and result policy. Ready is not
a body-load, HUD-completion or sector-task-completion result.

The AP walk's success predicate is:

1. Read manager tail, find the first class1/subtype0 node while scanning
   backward at most8 nodes. Reach head/null/bound or malformed/read failure:
   defer. The first matching node decides; do not continue past an old/dead/
   incomplete matching sector to find a more convenient one.
2. Require marker `0xcafe`, sector scene `+0x130 != 0`, and ID `+8` different
   from the departure ID when that supplied ID is nonzero. Copy index+0x13c.
3. Fresh count/table, count1..4096, aligned/non-null table, index in range and
   checked 64-bit row arithmetic `table + index*0xdb8 + 0x44`.
4. Shared row validity: dust `R+0xf0` in0..64; fog `R+0x104/+0x108` both0, or
   `0 < near <= far <= 2,000,000,000`. Invalid values defer.

**Name/profile/recipe are not prerequisites.** `read_record` returns true after
valid row decoding even when its optional name read fails or `name_valid` is
false. SETA never examines that name. Therefore a SETA-only FL provider may
omit the native family-string dereference and still preserve every Ready
outcome. It needs no fog worker, placement cache, shader or camera sample.
The full fog sample's bounded NUL-stopping name contract remains unchanged for
its actual consumers. Do not mechanically call AP's full-span name reader.

The eight-node limit is a **refusal bound**, not a proven maximum occupancy or
promise to find a destination every frame. A ninth candidate defers; another
poll may find it later, otherwise the existing expiry ends the ticket. A newer
class1/subtype0 object that differs from the player parent also defers at the
state machine's parent comparison. No stronger candidate-selection policy is
introduced by this port.

## 2. FL publication and ownership order

### Normal gate, jumpdrive and QuickWarp

| Stage | Clean FL CODE / native effect |
| --- | --- |
| Last explicit fade delay before the principal sector-change block | `150::WarpToSector` entry **`0x19cfc`**, delay **`0x1a0cd`**. |
| Remove cockpit ownership | **`0x1a0d7`** StopAllMonitors; `606::StopMonitor` **`0x187538`** reaches the reviewed INS free/removal path. |
| Leave old sector and release its ownership | **`0x1a187`** `150::LeaveSector` → dynamic sector Deactivate. `2001::Deactivate` **`0x1080e6`** frees the engine sector **before** freeing its three scenes at `0x1080f6/0x108118/0x10813a`. |
| Clean parentless objects / reload surviving resource work | **`0x1a18e`** SA_CleanUpObjects; **`0x1a195`** SA_FreeAllBodies. These can create/reload resources belonging to surviving old/other scene instances, before destination publication. |
| Enter destination | **`0x1a1aa`** `150::EnterSector`, whose dynamic Activate reaches **`2001::Activate 0x1075d9`**. |
| Construct and register destination sector | **`0x1075fb`** SA_AllocObject with main1/subtype0. Native command **`0x46a73c → 0x45c110`**; list append and ID publication precede sector-specific initialization. |
| Publish actual background | **`0x107616`** SA_SetSectorBackgroundType → **`0x46eac5`** stores sector+0x13c. Stars/nebula indices follow at `0x107623/0x107630`. |
| Allocate sector scene and publish it | **`0x107640`** dynamic `600::CreateAlloc`; **`0x107656`** SA_SetSpaceScene → **`0x46e934 → 0x45c260`**, store **`0x45c264`** at sector+0x130. |
| Activate destination population | First object-table dynamic Activate **`0x1076fe`**, followed by the other Activate/Activate2/task/swarm/connection work. Dust-scene initialization is later at **`0x107cc6`**. |
| Reparent/arrive player, then rebuild monitor ownership | Parent call alternatives **`0x1a1ee`** EnterSector / **`0x1a240`** WarpEnterSector; **`0x1a24a`** RestartAllMonitors. `606::StartMonitor` allocates cockpit at **`0x187678`**, sets its sector at **`0x1876b9`**. |

Within sector Activate, native allocation dominates background publication,
background publication dominates scene publication, and scene publication
dominates the first population Activate. The entry's already-active return
cannot reach that population path. Within WarpToSector the StopAllMonitors,
LeaveSector, cleanup and FreeAllBodies calls dominate the destination
EnterSector call. The coordinate-equal path can skip sector replacement;
it is not forced to invent a new sector or bypass the unchanged departure-ID
check.

QuickWarp has the corresponding sequence at
**`0x1a76c`** StopAllMonitors, **`0x1a7ba`** LeaveSector,
**`0x1a7c1/0x1a7c8`** cleanup/FreeAllBodies, **`0x1a7dd`** EnterSector,
**`0x1a7f2`** player EnterSector and **`0x1a7fc`** RestartAllMonitors.
Gate and jumpdrive enter WarpToSector through StartGateWarp/StartSectorWarp,
as the SETA note records. Optional modified stories are not qualified here.

`402::EnterSector`'s delay `0x17a264` and `WarpEnterSector`'s delay `0x17a2de`
are inside fork children (`0x17a25a..0x17a274` and
`0x17a2d4..0x17a2ee`), not unconditional arrival-parent delays. This corrects
an apparent stall split that a call-name-only list would suggest. No blanket
claim that all dynamic activation or arrival methods cannot yield is needed:
input polling only samples the currently returned native state, requires
parent/control agreement and retains AP's observed predicate.

Scene allocation itself is **not call-free**: native
**`0x4b010c → 0x4a4a30`** can invoke allocation recovery at **`0x4a4a5c`**.
`600::CreateAlloc` calls B3D_SceneAlloc then its naming method; it does not
activate destination simulation population. A resource callback during this
allocation is therefore a different sampling phase from SETA's input entry.
A scene pointer is a nonzero scalar guard here; Ready never dereferences the
scene allocation or treats an observer publication as completed sector setup.

### Intrusive list and object lifetime

Manager is **`*0x5a82ec`**. Its constructor **`0x43dc30`** initializes the
header at M+8 exactly: **`0x43dc88..0x43dc95`** establishes head=M+0xc,
tail-marker next=0 and tail predecessor=M+8. This makes head-sentinel refusal
concrete rather than inheriting AP's earlier inferred initialization.

Sector constructor **`0x45c110`** calls common initializer `0x449910`, appends
with stores at **`0x45c158/15e/163/16a`**, registers its ID at
**`0x45c17c/181`**, then zeroes scene at **`0x45c18b`** and initializes index0
at **`0x45c199`**. Thus list/map membership can precede initialized sector
payload; neither alone is permission or Ready. The scene guard rejects the
normal constructor-to-background setup gap at the qualified input phase.
It cannot make arbitrary sampling *inside* construction safe.

Common objects append similarly in **`0x449110`**. Placement
**`0x452b70`** removes the old links at **`0x452bf8/0x452bff`**, links into the
sector's per-class child list, publishes **object+0x54 at `0x452c53`**, then
attaches its render root via **`0x452c78 → 0x4a56b0`**. These list mutations
have no call between their unlink/link stores. A successfully placed object
therefore leaves the parentless tail. No static count bounds every transient
unplaced ship/turret/object at that tail; retain the conservative8-step refusal.

Destruction **`0x4491a0`** may call cleanup before its tail. The tail unlinks
at **`0x44960e/0x449616`**, writes dead marker **`0x44961e`**, then zeroes the
object through **`0x449625`**. The old Deactivate order removes sector
ownership before subsequent scene destruction. The accepted phase must exclude
entry during destructive callbacks; an old alive marker/readable page cannot
pin an object. Sector ID is the native map label, not a permanent incarnation
counter; keep existing load invalidation and no-pointer-retention rules.

`SA_FreeAllBodies` native case **`0x46f8ce`** calls
`0x4a5940`, `0x4a1e60`, **`0x4a5b20`**, **`0x443d10`**, and `0x4fff90`.
`0x4a5b20` walks enabled scenes/cameras/instances and calls preparation
`0x4a2120`; parentless pass `0x443d10` calls **`0x44c5a0`**, which recursively
re-requests class bodies. Consequently resource/observer work before the
new sector exists is real, and cannot be labeled destination Ready. Ready
sampling needs no B3D mesh/texture/node observer borrow or resource pointer.

### Save load and table replacement

Whole-load **`0x479430`** restores VM at **`0x47983c`**, renderer/B3D world
at **`0x47984c → 0x493510`**, then simulation at
**`0x47987e → 0x448bb0`**, and cockpit owners at **`0x4798bf → 0x420d30`**.
Renderer restoration calls world preparation `0x4940fd → 0x4a5b20` before
simulation sectors are restored. It supplies no early sector-based prefill
window for those already restored bodies.

Simulation restore first cleans/rebuilds types
**`0x448bf2 → 0x442b80`**, **`0x448bf9 → 0x43e710`**. Object allocations
(`0x448d1f`) publish ID/class/subtype into the ID map at
`0x448d41/44/48/56`, without constructor list insertion. Per-object restore
**`0x448e3f → 0x44b6b0`** invokes sector reader
**`0x44b915 → 0x434d60`**, which writes scenes at
`0x4352a3/0x435315/0x435387` and background index at **`0x4353de`**.
Only afterward the postpass **`0x449014/0x449026 → 0x44be70`** appends
parentless objects at **`0x44bea0..0x44beb6`**. Player M+0x38 is restored
later at **`0x44909c`**. The common whole-load gate excludes this entire
interval, including cockpit fixups, failure and unwind; neither B3D return
nor sector map insertion reopens it.

TBackgrounds count/table are **`0x5a8338` / `0x5a83d0`**, row stride0xdb8
plus0x44. Native loader publishes the table at **`0x440637` before rows are
filled**, names at `0x4409e9`, fog at `0x440ac6/0x440ad1`. Runtime setter
**`0x472693`** may realloc at `0x4726be`, increment count at `0x4726c3`, then
publish the table at `0x4726c9`; cleanup frees at `0x442d11` before clearing
its slot at `0x442d25`. These are concrete reasons to sample only in the
returned native phase and reload all roots/row values each time. Positive
count, non-null storage or plausible zeroed fog cannot replace that phase.
Startup finishes type loading **`0x4778ae`** before it invokes the outer loop
at **`0x477a9a`**; its two surveyed loader callers are startup and whole-load
simulation restore. Existing [sector-fog lifetime evidence](sector-fog.md#3-initialization-publication-and-actual-payload-lifetime)
owns the full table mutation/cleanup closure and family allocation details.

## 3. Exact input read admission, callbacks and exceptions

The finite read is admitted by this conjunction, not by metadata alone:

1. **Actual native phase:** shared injection at **`0x4cfd00`**, original
   ECX=controlled object, before input clears counters. Its only surveyed direct
   callers are **`0x4787c8/0x4787dc`** in driver **`0x478690`**; that driver's
   only caller is **`0x478150`** in the native outer loop. Ring-reader `0x4786a8`
   dominates both input calls. KC Input `0x478768` is optional: no-dispatch
   paths also reach native input; when dispatched, its normal return
   `0x47876d` precedes this entry. The producer checks this distinction
   in the driver CFG. All surveyed entry targets have zero HIGHLOW function-pointer
   publications. Calls into private interiors/third-party replacements remain
   outside the fixed profile.
2. **Existing common owner/readiness:** require the same healthy admitted
   owner, prepared common group, nonzero incarnation, complete phase, no whole
   load, drain, pending close, quarantine or foreign thread. Use the shared
   **`ui_scale_fl::chase_native_identity`** handoff selected for chase-fire,
   with its incarnation/load generation, rather than adding another export or
   lifetime table. That handoff is integration work absent from base30ffa02e;
   this note relies on its stated common-state contract, not an already
   exported input-borrow service. `fl_render_admission::current()` requires
   live Render TLS and is unavailable here; do not fabricate a render scope.
3. **Native scheduling/callback closure:** simulation/table writers execute
   synchronously through registered dispatcher **`0x46a640`**, registration
   **`0x43dce0`**, interpreter native call **`0x4c0783`**. Any preceding task/command
   invocation has returned before this input phase; pending/suspended KC tasks cannot run
   concurrently on that owner. Reuse the reviewed [command/callback closure](material-submit.md#3-actual-borrow-readiness-writers-and-reentrancy)
   and [typed callback frontier](ui-render-mutation.md).
   Window/input/audio callbacks publish input/ready work; they do not recurse
   into this input driver. SC_CLOSE is routed through the existing deferred
   close owner. This is a supported native closure, not a whole-program proof
   against arbitrary computed private calls or foreign threads.
4. **One call-free native interval:** after obtaining current common identity,
   copy fresh manager/list/type/scene/index/id/table/row values using inline
   scalar loads and arithmetic guards. No engine helper/resolver, allocation,
   logging, page query, observer lock, D3D/COM call or shared-state callback
   belongs inside this interval. Complete before policy/logging or named
   dispatch, and recheck common identity before accepting the owned result.
   A later generation check cannot repair an earlier invalid read; phase and
   callback exclusion protect the read itself.

The native input driver and its returned command phase are the missing
positive read boundary. Common load/close state excludes destructive loading
and close; the call-free interval prevents synchronous sector/list/table
mutation once copying begins. This does not grant the same read from an
arbitrary resource interception, destructor, allocation recovery callback,
later renderer consumer or a retained ticket pointer.

Chase's priority call may run before the SETA consumer at the same injection.
Acquire the Ready read admission **after that call has returned**, and read
fresh roots; never carry a borrow or cached node through SelectMode or other
KC. The SETA module lock does not pin native storage and must not span its own
StartFastForward/StopFastForward dispatch. The Ready provider itself makes no
native call and publishes owned scalars only. The state machine's stored
`ready_sector` remains an equality label; it grants no later dereference.

Preserve the reviewed shared entry stub's instruction replay, ECX extraction,
GPR/EFLAGS/DF, x87/XMM/MXCSR, LastError and four-byte incoming-stack contract.
No new displaced span or continuation is proposed. Escaping native/KC
exceptions retain the existing VM/EH cancellation and foreign-unwind limits.
No reader grant survives unwind or failed/abandoned common load; do not add a
C++ destructor/return observer as an invented recovery guarantee. Emitted
call-free loads, identity refusal/recheck and fault/unwind behavior still need
the focused implementation checks. Corrupted/foreign-mutated native pointers
are not supported merely because structural bounds happened to pass.

## 4. Minimal adapter, option independence and limits

Bind manager `0x5a82ec`, count `0x5a8338`, table `0x5a83d0`; other required
object/sector/list offsets stay as above. Reuse shared walk/Ready bounds and
AP retry/retain/cancel policy. The FL branch must use admitted inline copies;
calling AP's `engine_memory::revalidate` / page-validating read loop is not the
existing FL finite native-copy contract. Keep AP's backend behavior unchanged.

For the first adapter, a fixed owned row can be passed to the existing row
validator, with no name dereference. A SETA-only copy may further restrict the
row to the three fields that affect Ready, provided the same shared bounds
remain authoritative; family/weights/stardust are not new gates. Do not force
fog profile admission or camera matching into gate restore. All roots/values
must be reacquired per eligible poll; no native pointer cache is needed.

Inferred cost: at most8 tail candidates, one bounded row copy and scalar
validation, at most once per presented frame while an armed ticket needs
Ready. A full0x120 row and eight-word sector block require at most101 native
scalar loads under the conservative copying recipe, before the separate
player/parent/pause probes; the minimal predicate needs only26. Neither figure
is measured native timing. There is no per-draw work, allocation, native call,
wait or new lock. Idle SETA retains its existing prefilter skip.

This Ready dependency belongs only to the gate ticket. Manual-flight partA,
partB and the key-stop latch do not consume it. Keep the current source's
separate fact that the latch is enabled only after `seta_restore` armed;
porting Ready does not alter that option relationship. Existing state-machine
refusals remain conservative when no candidate is found, an unrelated newest
sector is present, or the table/parent/control identity is not ready.

**No concrete clean-FL contradiction of the AP predicate was found.** Static
ordering supports using the existing phase; it does not establish a maximum
tail occupancy, first-Ready frame, stall duration, actual resource-prefill
lead time, every modified story, or final rendered sector/HUD completion.
Those are not prerequisites for the bounded SETA predicate. Native Windows
and CrossOver runtime behavior, loading cost and flight restoration remain
unverified. Resource-creation R3 prefill still needs its distinct interception
admission and measured lead-time qualification; this note does not reopen it
by borrowing the input-entry proof.

Implementation acceptance should target the relevant failures: no read on
foreign/load/drain/pending-close/quarantine identity; fresh post-chase roots;
constructor scene0 and old-ID refusal; cut-scene subtype exclusion; eighth
candidate success/ninth defer; wrong player parent/control refusal; row bounds
and address overflow; loaded-base profiles and group rollback; retained
scalar labels across VM/load cancellation; compiled call-free copy and CPU/
LastError preservation. No additional general lifecycle or asynchronous task
completion observer is justified by this evidence.

Independent consequential RE review reproduced the producer and accepted this
static contract. It also inspected the now-committed common owner/readiness
query, without treating that identity as a native borrow. Implementation must
still enforce fresh post-chase reads, bounds, identity refusal/recheck and the
compiled call-free/CPU envelope; runtime restoration remains unqualified.
