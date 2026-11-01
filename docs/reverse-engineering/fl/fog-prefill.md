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
