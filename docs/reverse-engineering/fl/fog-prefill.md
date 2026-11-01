# FL loading-time fog prefill: transition order and finite resource callback copy

Offline targeted RE, 2026-10-30. **The existing AP R3 strategy is implementable
for FL gate/jump and ordinary new-game entry using the existing resource
wrappers, a finite owner-thread value copy, and the shared provisional-worker
policy. No new transition hook is indicated.** The offline study alone did not enable FL
prefill or qualify a compiled adapter; the subsequent implementation checkpoint is linked in §6. The remaining runtime questions are poll
availability/lead time and the eight-node tail's effectiveness, as for AP.

Scope: [AP transit ordering](../sector-transit-order.md) §§1–3,5–7;
[FL sector inputs](sector-fog.md) §5;
[fog_prefill.h](../../../src/proxy/fog_prefill.h), `capture.cpp::fog_prefill_poll`
and the post-original `create_texture`/`create_vb` wrappers; shared
`MotionOutput::volumetric_fog_prefill` and `fog_prefill_confirm` in
[motion_output_fog_inc.h](../../../src/proxy/motion_output_fog_inc.h).
This is separate from ordinary camera/fog activation. No production edit,
build, game/Wine/native execution, install or commit was performed.

## 1. Actual FL inputs and destination initialization

All VAs below are preferred VAs, image base `0x400000`. Resolve through the
recognized FL loaded base; the AP addresses are not FL evidence. Inspected FL
EXE SHA-256 is
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
The mounted stock FL story is **`addon2/03.cat:addon2/L/x3story.obj`**, decoded
SHA-256 `dbb732716e1acab6b8cc7f87f5d31324fb4764dfc22d1cec808749a4f45ec7b9`,
build `1636047691`, CODE **2,731,291 bytes**, **8,803 method entry offsets**
(measured). Root/AP catalogues also contain story objects; selecting one of
those by basename would analyze the wrong script. Hashes are provenance.

The existing [FL sector-input layout](sector-fog.md#1-identity-globals-and-record-layout)
applies: manager slot **`0x5a82ec`**, table slot **`0x5a83d0`**, count slot
**`0x5a8338`**, record `table + index*0xdb8 + 0x44`. This pass additionally
establishes the parentless list's initialization and callback boundaries:

| Native path | Established contract |
| --- | --- |
| Manager constructor `0x43dc30`, list initialization `0x43dc88..0x43dc9b` | Header is manager `+8/+0xc/+0x10`: head initially `M+0xc`, tail word zero, tail predecessor initially **`M+8`**. The backward-walk head sentinel is established, rather than inferred as in the AP note. |
| Sector constructor `0x45c110`, AddTail `0x45c14d..0x45c16d` | Node `+0/+4` are next/previous; appends to manager `+0x10`. This sequence contains no CALL. |
| Constructor continuation | Automatic-map insertion `0x45c17c → 0x50cfe0`, identifier publication `0x45c181`, **then** scene-zero `0x45c18b`, background-zero `0x45c199`, remaining sector initialization. Publication therefore precedes initialization. |
| Placement `0x44ccb0`, `0x44ce7b..0x44ceb4` | Unlinks old list, appends to destination class list and writes parent `+0x54` at `0x44ceb1`; this mutation interval contains no CALL. Placed objects leave the parentless tail. |
| Destruction `0x4491a0`, `0x449602..0x449625` | Unlinks both neighbors before marker `0xefac` at `0x44961e`, then clears the base at `0x449625`; unlink/marker interval contains no CALL. Object release follows the destruction path, not a retained list link. |
| Cleanup `0x443cd0` | Walks parentless list and schedules non-class-1, parentless objects for cleanup. It preserves sectors. |
| Background command `0x46eaac..0x46eadc` | Class-checked sector resolver precedes index store **`0x46eac5`**. |
| Scene setter `0x45c260..0x45c29c` | Writes scene **`0x45c264`**, then copies background color; **zero CALLs**, `RET 4`. No resource creation can interleave the store and its native return. |

The script's **fresh-activation** prefix is straight-line from CODE `0x1075f3`
through `0x1076c9`; no branch, fork, return or yielding native occurs there
(measured by the producer). Its already-active branch returns at `0x1075f2`
with the existing sector; it does not execute an alternative fresh setup.

| Order | FL CODE / operation | Consequence |
| --- | --- | --- |
| 1 | `2001::Activate`, `0x1075fb`: `SA_AllocObject(1,0)` | Fresh destination sector appended. Constructor's index 0 is insufficient. |
| 2 | `0x107616`: `SA_SetSectorBackgroundType`; `0x107623/30`: star/nebula selections | Destination index is written before scene publication. |
| 3 | `0x107640`: `600::CreateAlloc`; `0x107656`: `SA_SetSpaceScene`; `0x107663`: size | Destination sector scene becomes nonzero **after** index initialization. |
| 4 | `0x107673..0x1076c4`: galaxy scene allocation/configuration/publication | Still before destination object activation. |
| 5 | First object `Activate` at **`0x1076fe`**, followed by remaining object tables, `Activate2`, start tasks, swarm and dust | Bulk destination body/mesh/texture creation occurs after steps 1–3. |

`600::CreateAlloc` calls `B3D_SceneAlloc` at CODE `0x367d5`, then `Create2`
(which names the scene). Native scene allocator **`0x4a4a30..0x4a4b51`** creates
an empty `0x58`-byte scene, inserts its handle and initializes scalar/list
fields. Its only indirect call is the known allocation-recovery callback,
under guard `0x62de10`; it has no ordinary D3D creation. Thus the ordinary
resource-creation window starts after initialized scene/index, with recovery
handled separately below. The mesh/texture API identities reuse the
[typed resource-creation/recovery proof](ui-render-mutation.md#allocation-recovery-actual-objects-callbacks-and-finite-closure),
including native D3DXCreateMesh at `0x4dbbdb` and the documented texture APIs.

Thirty `SA_AllocObject` sites are measured; exactly two push literal main class
1: `2001::Activate` uses subtype 0; `621::Create` at `0x11aad` uses member14,
whose cut-scene cases assign nonzero subtypes (1,2,3,5; initial -1). Five sites
have nonliteral main types, including cut-scene `CopyObjects` and four
member/expression cases. This is not a proof that no dynamic allocation can
ever make another class-1/subtype-0 object. A mistaken speculative candidate
only spends bounded CPU work and is corrected by actual Ready confirmation.

## 2. Gate/jump, QuickWarp, first entry and save load

Gate `2004::FlyThroughGate`, hyperspeed and jumpdrive routes call
`150::StartGateWarp` or `StartSectorWarp`; both call **`150::WarpToSector`**
(entries `0x199b0`, `0x19af2`, `0x19cfc`). The measured critical spine is:

| FL CODE | Operation |
| --- | --- |
| `0x1a0cd` | Fade delay before teardown. |
| `0x1a0d7` | Stop all monitors; old cockpit is unavailable for early destination identification. |
| `0x1a135`, `0x1a187` | Player LeaveSector, then old sector LeaveSector/Deactivate when coordinates change. |
| `0x1a18e`, `0x1a195` | `SA_CleanUpObjects`, `SA_FreeAllBodies`; resource creations here can concern surviving old/global assets. |
| **`0x1a1aa`** | `150::EnterSector → 2001::Activate`, executing destination setup above before object loads. |
| `0x1a1ee`/`0x1a240` | Player EnterSector / WarpEnterSector; arrival position is late. |
| **`0x1a24a`** | Restart all monitors. The sole stock `INS_CockpitSetSectorSpace` site is later inside `606::StartMonitor`, **`0x1876b9`**. |

QuickWarp repeats cleanup/free-bodies/EnterSector at `0x1a7c1/0x1a7c8/0x1a7dd`,
then player entry `0x1a7f2` and monitor restart `0x1a7fc`. This supplies the same
initialization order without requiring a new background-write hook.

**Ordinary new-game first entry is now traced.** `150::Run` resolves gamestart
sector at `0x1655f`, selects/creates the player ship, then calls
`150::EnterSector` at **`0x16932`**, player EnterSector at `0x16947`, and
`605::StartMainMonitor` at `0x16984`. Thus its first destination also uses the
same initialized scene/index prefix before destination object loads and before
the cockpit route. Earlier startup/cut-scene/player construction may create
resources before this prefix; such polls can find nothing and are not promised
lead time. Scripted gamestart `700::RunScript` at `0x1666e` and arbitrary mods
are not assumed to obey one fixed startup script timeline.

No elapsed-time lower bound follows from script offsets. This study does not
claim a whole-transition no-yield theorem over every dynamic callee: FL
`2004::__LeaveHyperspace` contains a carrier-hangar wait at `0x147f0d` and
additional forked delays. A yield after activation does not invalidate a copied
prefill. Safety needs the finite resource callback interval, not a lock or
lease lasting from old-sector destruction to the first destination frame.

**Save load remains different.** Native `0x479430` calls B3D restore
**`0x47984c → 0x493510`**, simulation restore
**`0x47987e → 0x448bb0`**, then cockpit restore
**`0x4798bf → 0x420d30`**. Simulation restore cleans/reloads type tables at
`0x448bf2/0x448bf9` before restored sector fixup/linking. Destination body
restore therefore precedes a usable simulation sector, as in AP. Reusing the
common no-load filter simply refuses these polls. **`current_load()` identifies
only this save-load wrapper; it grants no gate/jump/new-game interval.**

## 3. Finite callback authority and its recovery exception

Use the existing post-original **CreateTexture / CreateVertexBuffer** wrappers.
`create_ib` currently has no prefill poll; no extra global interception is
needed to establish this contract. Preserve the existing >250 ms since Present
and at-most-one-per-250-ms poll policy, and require the saved Present thread to
equal the already published FL native owner. A raw thread-ID comparison alone
does not supply owner health or shutdown/load admission.

The existing **`ui_scale_fl::chase_native_identity()`** is a suitable owner/state
filter to reuse (or factor without changing its meaning): TLS healthy, calling
thread equals the startup-published owner, common installation prepared,
incarnation/complete valid, no save-load depth, no draining/pending close, and
the existing optional UI-readiness condition. It is available outside a render
pass and does not require adding a gate/new-game wrapper. Its returned identity
is a label; the following synchronous callback argument establishes the native
borrow, not that label by itself.

The [native-command scheduling closure](material-submit.md#native-command-scheduling-closure)
places normal KC task execution and complete render in the same native outer
loop **`0x477ba0`**; native simulation commands use registered dispatcher
`0x46a640` synchronously through interpreter CALL `0x4c0783`, with five stack
words removed at `0x4c0785`. The destination allocations above execute those
commands on that owner. At a resource callback the native owner is paused in
its synchronous D3D/D3DX call. The named list mutation regions have no CALL,
so none can be suspended halfway at this callback. Existing qualified
window/audio/close and typed D3D/D3DX callback behavior applies before the
post-original wrapper resumes; it adds no concurrent KC mutator during a
subsequent **call-free** copy. Foreign private-address calls/replaced callbacks
remain outside the native profile, as in the existing admission contracts.

**Refuse while rebased DWORD `0x62de10` is nonzero.** This is a concrete
reentrancy guard, not a new global-loading flag. Two actual partial-publication
hazards make this filter necessary:

1. Sector constructor appends the sector, then calls automatic map insertion
   **before** initializing `+0x130/+0x13c`. The intervening allocation can enter
   recovery; stale nonzero scene bytes cannot be treated as initialized data.
2. `SA_SetBgTypeData` calls family replacement at `0x472727`, publishing its
   returned name at `0x47272c`. Helper **`0x50b670` frees the old name at
   `0x50b68f`, then allocates at `0x50b6c1 → 0x4d8fc0`**. Until return, the row
   can still contain the freed old pointer. Allocation recovery can demand-load
   textures, so a CreateTexture callback can really occur in this interval.

Allocator `0x4d8fc0` sets the guard at `0x4d900c/0x4d905c` around indirect
recovery CALLs `0x4d901a/0x4d906a`, clearing it after return. Empty-scene
allocation uses the same guard at `0x4a4a4e..0x4a4a5e`. Registered callback
`0x47b500` calls engine reclamation `0x485c00` and texture reclamation
`0x512410`; its typed demand-loading frontier is already closed in the linked
recovery note. Denying recovery avoids these interrupted publications without
requiring a theorem that no resource callback ever occurs during allocation.

After owner/stall/recovery admission, copy fresh manager/list/sector/table/name
values inline into callback-local storage. **No page query, logging, allocator,
observer lock, engine helper or D3D/COM call inside that interval.** Existing
AP `engine_memory::read` cannot mechanically serve as FL's call-free primitive.
Recheck the same mod-owned owner/incarnation/load identity after the interval.
That identity does not include device Reset or camera invalidation: separately
check the existing `reset_active`/`reset_generation` and
`camera_state_fl::invalidation_epoch()` before and after the copy. Close, Reset,
foreign invalidation or abnormal unwind discards the scratch. The current
CreateTexture/CreateVertexBuffer wrappers use ordinary C++ automatic guards;
they are not already enclosed by foreign SEH. Adapt the FL path to the existing
compiler SEH facility with invocation-owned CPU/admission/lock cleanup that
survives outward unwind and propagates the exception. No failed copy may publish
a pending worker request or authorize a later retry from saved native pointers. An unsupported nested prefill callback refuses; it need not
introduce persistent nesting machinery. Consume the existing 250-ms poll slot
before copying/worker start, so immediate nested resource calls cannot start a
second poll.

No native instruction patch is introduced, hence no displaced instruction or
native register/flag replay is required. The existing WINAPI resource wrapper
must still forward every argument/result, satisfy the ordinary WINAPI calling
ABI and four-byte incoming stack contract, and preserve x87/MXCSR/LastError
through `CpuCallBoundary`. Volatile XMM registers follow the ordinary ABI; this
is not an instruction hook and requires no new full-register/flags shim. Do not put
worker start/logging inside the finite copy: native callbacks have resumed only
after that interval ends, and those operations consume owned values only.

## 4. Read policy, worker handoff and actual-frame closure

Retain the existing bounded AP policy with FL inputs:

1. Fresh `M=*manager_slot`; checked aligned/nonzero address; start at `M+0x10`.
   Walk backward at most **eight** nodes, stopping at zero or **`M+8`**. Validate
   arithmetic, alignment, type word class1/subtype0 and reject cycles/bound.
2. Candidate marker `0xcafe`, nonzero scene `+0x130`, background `+0x13c`, id
   `+8` different from last known Ready id. These are structural/selection
   checks after callback admission, not independent memory permissions. Never
   dereference the scene pointer. No camera or arrival-position read is needed.
3. Fresh count/table globals, count 1..4096, valid index, checked 64-bit
   row arithmetic, copy the existing `0x120`-byte row, and copy family bytes
   with the [FL exact-allocation name rule](sector-fog.md#4-minimal-edition-adapter-and-finite-read-boundary):
   volatile scalar unsigned-byte loads, stop immediately at NUL, maximum 32
   native loads. No unconditional 32-byte read or dword/vector lookahead.
4. After the native interval, apply existing family/fog/dust/profile/recipe and
   placement policy solely to owned values; center the speculative field at
   sector origin. The actual arrival camera is not yet known.

The scalar-copy upper bound for an AP-shaped eight-word sector block is
**134 native loads including the recovery scalar** (inferred implementation
bound: 2 manager/tail + 8 type + 7 link + 1 marker + 8 sector block + 1 id +
2 count/table + 72 row DWORDs + 32 name bytes + 1 recovery). This is a bounded
per-250-ms stall cost, not per-draw work or a measured FPS figure. The AP
25-span-read accounting must not be advertised as FL's scalar-load count.

Use `fog_prefill::plan/decide` and the existing nonblocking CPU density worker.
Keep only copied scalar recipe/key/name data and non-dereferenceable identity
labels. No native pointer, frame ticket or scene authority transfers to that
worker. The local walk's `Found`/sample status is **speculative**, even if its
internal AP-shaped sample calls that status Ready. It does not publish an FL
ordinary frame sample. Failed/clear/profile-refused candidates cannot arm fog.

Actual confirmation remains the first **independently admitted ordinary fog
Ready sample**, bound to its matched active cockpit camera and the exact
same-frame Scene/activation/load/invalidation ticket. Resource callbacks cannot
manufacture that ticket. Until confirmation, the prefill causes **no GPU
upload, drawing, card suppression, fog-latch readiness or stale-frame reuse**.
Equal placement key and recipe adopts the CPU fill; mismatch or unusable Ready
discards it using the existing common policy. Pointer/id equality is diagnostic,
not necessary for adoption: placement owns the field. FL automatic handles can
wrap and saved handles are restored, so ids are not permanent generations.
Retire the last-Ready/pending labels on owner/load invalidation; same-address
reuse or an equal id can at worst miss a speculative lead opportunity.

The eight-node limit is an effectiveness choice. Placed objects leave this
list, destination sector appends near the tail, and early activation normally
offers a nearby candidate. Unplaced objects/turrets or dynamic cut-scene
copies can bury it; no static tail-length maximum is established. Return Bound
and retry at the next due callback, without increasing the scan or installing
global allocation hooks by default.

## 5. Evidence and remaining acceptance

```sh
python3 verification/results/fl-fog-prefill/verify.py
python3 -m py_compile verification/results/fl-fog-prefill/verify.py
```

[Producer](../../../verification/results/fl-fog-prefill/verify.py) and
[compact record](../../../verification/results/fl-fog-prefill/record.json):
**15 whole-instruction regions / 437 instructions**, **49 native operand
witnesses**, **49 rejected changed-opcode witnesses** with no hash gate in the
validator; six HIGHLOW operands for the newly used recovery scalar and three
synthetic loaded bases (`0x10000000`, `0x60000000`, `0x70000000`); seven named
script methods, fresh-setup straight-line checks and
the full 30-site allocation census. These are measured offline checks,
not runtime qualification or an independent executable recognizer. Previously
qualified table layouts, name allocation and native callbacks are linked rather
than re-audited here.

Ghidra 12.1.4 used the existing analyzed project
`verification/results/fl-port/registry-writers/ghidra/FLRegistry`,
`-process X3FL.exe -readOnly -noanalysis -scriptPath tools/analysis`,
`X3CameraState.java` with `dec`/`ins` queries at `0x43dc30`, `0x45c110`,
`0x44ccb0`, `0x4491a0`, `0x443cd0`, `0x479430`, `0x448bb0`, `0x476bc0`,
`0x476ac0`, `0x4762d0`, `0x4a4a30`, `0x4d8fc0`; reference queries included
`0x5a82ec`, `0x45c110` and `0x477ba0`. Raw decompiler/listing outputs remain
only under `/tmp/fl-fog-prefill/*.local.txt`; no extracted bytecode is written.
Capstone completes dispatcher regions that the existing Ghidra project labels
as code without a containing function.

Implementation acceptance still needs compiled call-free/scalar-NUL-stop
inspection, owner/load/close/recovery/nested/fault refusal tests, foreign-SEH
scratch retirement, worker-only provisional behavior, same-frame confirmation
and AP policy parity. User flight evidence must measure first-entry/gate/jump
poll status/steps/id/index/key and confirmation lead time, including non-tail
refusal and same-key transits. A missing prior Present, a short stall or no
resource callback can legitimately give no prefill; R1/R2 and actual Ready
remain responsible for rendering. No Windows-specific private backend contract
is added, but native Windows execution and FL visual/lead-time acceptance remain
unmeasured.

## 6. Subsequent implementation checkpoint

The finite native copy is now implemented in
[`fog_prefill_fl.h`](../../../src/engine/fl/fog_prefill_fl.h), with the existing
resource wrappers providing compiler-SEH ownership and before/after
owner/load/Reset/request checks. The shared worker remains provisional until
the independently matched ordinary Scene sample.
[Implementation and scoped evidence](../../verification/volumetric-fog.md#2026-10-30--fl-loading-cpu-prefill-implementation-checkpoint)
record the host/code-generation checks and synthetic native wrapper witness;
actual game transition availability and lead time remain open.

## 7. Verified constants (2026-11-01)

Targeted static RE after FL run 13 (the vanilla cards stayed 3–4 s after arriving in The Hole; the log
has no `volumetric_fog_prefill` row, measured: the poll in `capture.cpp` reads AP's literal
`0x0060850c`/`0x00607040`/`0x00606fc0` and is not reached on FL). X3FL.exe SHA-256 `e7f3aad8…1dfbb8`
(provenance), side by side with `/mnt/c/X3AP-orig/X3AP.exe`; capstone over the file bytes and its base
relocations. **The AP walk applies to FL unchanged in shape: the same manager list, node and sector
layout, with the three image slots rebased.** §6's `fog_prefill_fl.h` is not in this tree (the Codex FL
work is archived on `fl-codex`); FL has no prefill code today.

Producer and checks (RVAs below; VA = module base + RVA, preferred base `0x400000`):

```sh
python3 tools/analysis/fl_fog_prefill_constants.py --check     # writes/compares src/engine/fl/fog_prefill_fl_inc.h
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fl_fog_prefill_constants
python3 verification/results/fl-fog-prefill/kc_transit_route.py   # story route, names and CODE offsets only
python3 verification/results/fl-fog-prefill/run13_stall.py         # runs 11–13 log: stalls > 200 ms, handover row
```

18 anchors / 159 whole instructions / 11 relocated operands / 66 operand witnesses (measured); every
anchor is at most 111 bytes and 3 operands, accepted at bases `0x400000`, `0x4d0000`, `0x10000000` and
refused for every single flipped byte (host test). Cross-checks: the shared slots and offsets equal
`fl_sector_background_constants.py`'s (which anchors the count slot); eax holds `0xcafe` from its load
at `0x49963` to the marker store at `0x49a20` (26 instructions, none writes eax); native command table
`0x73dc4` routes case 0 → `0x6a6a3`, `0x128` → `0x6e909`, `0x133` → `0x6eaac` (AP's numbering:
SA_AllocObject, SA_SetSpaceScene, SA_SetSectorBackgroundType).

### 7.1 AP inputs of `fog_prefill.h` and their FL counterparts

| AP (`fog_prefill.h`) | FL | Matched by | Status |
| --- | --- | --- | --- |
| `kManager` `0x0060850c` | slot RVA **`0x1a82ec`** | writer `0x778a9` stores the result of ctor `0x3dc30` (AP `0x403565`); cleanup walk `0x43cd0` = AP `0x43a460` instruction for instruction | verified |
| list header `M+8` (head), `M+0xc` (tail 0), `M+0x10` (tailpred) | same | ctor `0x3dc88..0x3dc95`: `[M+8]=M+0xc`, `[M+0xc]=0`, `[M+0x10]=M+8` (AP inferred this; FL measured) | verified |
| link prev `node+4`, next `node+0` | same | AddTail in sector ctor `0x5c14d..0x5c16a`, restore post-pass `0x4bea0..0x4beb6` (AP `0x442c31`), unlink `0x49602..0x49616` | verified |
| type dword `+0x48` = class 1, subtype 0 | class word `+0x48`, subtype word `+0x4a` | common init `0x4995f`/`0x499f2`; sector ctor tests `+0x4a` at `0x5c195` | verified |
| marker `+0x9c` == `0xcafe` | same; dead `0xefac` | `0x49963`/`0x49a20`; unlink then `0xefac` at `0x4961e`, then memset(obj, 0, 0x130) via `0x521920` | verified |
| block `+0x130` (scene) … `+0x13c` (index, dword 3) | same | ctor zeroes `+0x130` at `0x5c18b`, `+0x13c` at `0x5c199`; setter stores `+0x130` at `0x5c264`; case `0x133` stores `+0x13c` at `0x6eac5` | verified |
| id `+8` | same | ctor `0x5c181` stores the id-map insert result (`0x10cfe0`) | verified |
| count `0x00607040`, table `0x00606fc0` | **`0x1a8338`**, **`0x1a83d0`** | `sector_background_fl_inc.h` anchors; table also in setter `0x5c278` | verified |
| record recipe (`read_record`, stride `0xdb8` + `0x44`) | FL layout of `sector_background_fl` (`name_to_nul`) | unchanged | verified (sector-fog.md §6) |

Differences from AP: FL is relocatable (every slot is `module_base() + RVA`); the sector constructor
`0x5c110` inlines AP's common constructor AddTail (AP `0x43f93b`) and the sector initialisation (AP
`0x4524d0`) with the same order: common init `0x49910` (class, subtype, `0xcafe`), AddTail, id-map
insert (`CALL 0x10cfe0`, which allocates through the guarded allocator `0xd8fc0`), id store, then
`+0x130..+0x178` (flags `+0x148 = 2` for subtype 0, size `+0x14c = 10,000,000`, `+0x178 = -1`, as AP).

### 7.2 Answers

1. **Inputs.** Table 7.1; anchors and witnesses in the generated include.
2. **List.** Same Exec layout as AP: header at M+8 `{head, tail 0, tailpred}`, the backward walk's
   head sentinel is `M+8`, node = the engine object itself (`+0` next, `+4` prev). There is no native
   "pending destination" field: the destination is the newest class-1/subtype-0 node, appended at the
   tail by `0x5c110` when the script's `2001::Activate` allocates it (`SA_AllocObject(1,0)`, KC
   `0x1075fb`, args `push0; push1`), distinguished from the old sector by id `+8` and from cut-scene
   spaces (`621::Create`, subtype `member14`) by subtype 0. The two main-class-1 allocation sites of
   30 are those two (measured).
3. **Timing.** The outer loop's head is `0x77e80` (back edge `0x78591`); per iteration the KC ready-task
   runner `0x788b0` is called at `0x77ebf`, then the cockpit update at `0x782ca` and the render pass at
   `0x782cf` (BeginScene `call [ecx+0xa4]` at `0x8b859`). The whole transit runs synchronously inside
   one `0x77ebf` call: `150::WarpToSector` presents frames during the fade (`TI_Delay` `0x1a0cd`), then
   tears down (`0x1a0d7` StopAllMonitors … `0x1a195` FreeAllBodies) and calls `150::EnterSector`
   (`0x1a1aa`) → `2001::Activate`: alloc `0x1075fb`, index `0x107616`, scene `0x107656`; the sector is
   readable by the walk from the scene store on, **before** the first destination object activation
   at `0x1076fe` (bulk body/mesh/texture creation, the poll's opportunities). Cockpit `+0x54` is
   written only by `INS_CockpitSetSectorSpace` in `606::StartMonitor` (`0x1876b9`, the sole site),
   after `RestartAllMonitors` `0x1a24a`, still inside the same call; the arrival frame is the next
   `0x782ca`/`0x782cf`. The window is therefore from `0x107656` to the end of the stall. Measured in
   runs 11–13: the The Hole transit frame 23320 had `dt_ms=2667`; the handover armed at 23322 and
   needed 45 frames / 490 ms (`fill_ms=471`). AP reference (run273 D): found 7.1 s into a 12.6 s stall.
   The FL share of the 2.67 s after `0x107656` is not measured (needs FL poll rows); a lead above
   ~0.5 s would hide the cold fill (inferred from `fill_ms=471`).
4. **Codex notes.** Confirmed: slot `0x5a82ec`, ctor `0x43dc30` and header init, sector AddTail
   without a CALL, publication before initialisation, unlink/marker, cleanup `0x443cd0`, scene setter
   with no CALL, index store `0x46eac5`, recovery guard `0x62de10` (set `0x4d900c`/`0x4d905c`, cleared
   `0x4d901c`/`0x4d906c`), every KC offset in §§1–2 and the 30-site census. Settled: the id-map insert
   between AddTail and the `+0x130` zeroing does allocate (`0x50cfe0` → `0x50cf30` → `0x4d8fc0`), so
   a resource callback inside allocation recovery could see a linked, typed, `0xcafe` sector with an
   unset scene/index; refusing the poll while `[guard slot]` is nonzero closes it (one extra read; the
   AP poll has the same window unguarded). Corrected: §1's "placement `0x44ccb0`" is a second relink
   path (unlink, per-class child AddTail, parent at `0x44ceb1`); the counterpart of AP's
   `SA_StartObjectInSpace` `0x449510` is **`0x452b70`** (parent at `0x452c53`, scene attach
   `0x452c78 → 0x4a56b0`, as prefill-ready.md has it). The "134 scalar loads" bound and the
   SEH/finite-copy requirements of §3 are Codex design, not engine facts, and were not re-examined.
   `gate-transit.md` and `gate-bootstrap.md` carry no prefill input.
5. **Gate vs jump.** Same structure: `2004::FlyThroughGate` → `150::StartGateWarp`;
   `2004::JumpToSector` → `StartGateWarp` or `StartSectorWarp`; `2004::HyperspeedToSector` →
   `StartGateWarp`; both starts → `150::WarpToSector` → `150::EnterSector` → `Activate` (`0x19976`).
   `150::QuickWarp` calls `150::EnterSector` at `0x1a7dd` without the fade. All allocate through native
   case 0 → `0x6a71e` (`0x180` bytes) → `0x5c110`. `2001::Activate` returns the existing sector when
   its member 37 is positive (already active), so a transit into an active sector appends nothing.

Hook-site suitability: no patch and no instruction boundary; the walk runs in the existing
`CreateTexture`/`CreateVertexBuffer` wrappers on the render thread, as on AP. Every list mutation
named above (AddTail, unlink, relink) is CALL-free, so a callback never observes a half-linked node;
the only reentrant hazard is the allocation-recovery callback, covered by the guard read.

Unknown: the post-`0x107656` share of an FL stall and the tail occupancy at the first poll (needs
an FL build with the walk and a flight); whether 0x4d9120's allocation is zeroed (it decides what an
unguarded mid-construction read sees; irrelevant with the guard).
