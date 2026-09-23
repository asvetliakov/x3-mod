# Sector transit order: when the destination sector becomes readable

Static analysis only, 2026-09-23, for the fog hand-over's R3
([fog-handover.md](../architecture/fog-handover.md) §3.A). X3AP.exe SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` (base `0x00400000`), Ghidra
12.1.3 `-readOnly -noanalysis` on `/tmp/x3-ghidra-research`, plus a decode of the story script
`addon/04.cat:L/x3story.obj` (decoded SHA-256 `ed5786a0…faff7a`, loader contract of
[selection-native-vm.md](selection-native-vm.md#recovering-actual-compiled-method-names)). No game,
Wine, build or install. Decompiler output stayed in the session scratchpad and is not committed.
Builds on [sector-fog.md](sector-fog.md) §11 (the cockpit → sector → TBackgrounds chain),
[external-camera.md](external-camera.md) §1–2 (the `INS_*` table, the cockpit object) and
[loading-orchestration.md](loading-orchestration.md) §3 (save loading).

**Outcome.** For a gate transit, a jumpdrive jump and the script `QuickWarp`, the destination's
engine sector object is **created and given its background index before any of the destination's
objects are activated**, i.e. before every destination body/mesh/texture creation, inside the same
synchronous script run that makes the stall. The cockpit's `+0x54` is written **last**, after all
destination loads, by the only `INS_CockpitSetSectorSpace` call site in the script, and the old
cockpit is freed at the start of the stall, so the §3.A cockpit route (including the "re-walk the
registry for the previous handle" fallback) gives no lead time. The destination is reachable
instead through the engine's **global object list** (`*0x0060850c`): a backward walk of at most
eight nodes from the list tail finds it (class 1, subtype 0, scene set, id different from the last
Ready sector), in at most 25 bounded reads from a resource-creation interception the proxy already
has. **No trampoline is needed** for gate/jump. For a **save load** the destination sector is
allocated only after the whole B3D/body/mesh restore, so the engine exposes it at the tail of the
stall and R3 gains nothing there (inferred; R1+R2 still apply).

## 1. Engine structures used

**Object manager** `M = *(u32*)0x0060850c`. Only the fields this note relies on:

| Offset | Meaning | Evidence |
| --- | --- | --- |
| `+0x08` / `+0x0c` / `+0x10` | Exec-style list header of **parentless** engine objects: `lh_Head`, `lh_Tail` (always 0), `lh_TailPred`. A node's `+0` is `next`, `+4` is `prev`; the first node's `prev` is `M+8`, the last node's `next` is `M+0xc` | AddTail at `0x0043f93b`–`0x0043f956` in the object constructor `0x0043f900`; same AddTail in the save-load post-pass `0x00442c31`–`0x00442c46`; walks at `0x0043a460` (`SA_CleanUpObjects`), `0x0043a4a0`, `0x0044aab0` |
| `+0x14` | id → object hash `{bucket array, bucket count}`, nodes `{next, id, object}` | `0x0043a560` (sector by id, returns 0 unless `*(i16*)(obj+0x48)==1`), `0x0043a4f0` |
| `+0x38` | the player ship (class 7) | written `0x0043f8be` (save load), cleared in the free path when that ship dies |

**Engine object** (all classes): `+0x08` id (from `0x004efcc0`, monotonically increasing within a
session), `+0x48` class (`i16`), `+0x4a` subtype (`i16`, both set by `0x0043ffa0`), `+0x50` class
data (for a sector: 32 child lists of 12 bytes, one per class), `+0x54` parent, `+0x9c` alive
marker `0xcafe` (constructor `0x0043ffa0`, restore `0x004421a0`; the free tail stamps `0xefac` at
`0x0043fc23` after the Remove at `0x0043fc0b`–`0x0043fc1f`, then zeroes `0x130` bytes).

**An object placed into a sector leaves the global list.** `SA_StartObjectInSpace`
(`0x00449510`) removes the node from its current list, AddTails it to the sector's child list for
its class, writes `[obj+0x54] = sector` at `0x004495fd` and only then attaches it to the sector's
scene (`CALL 0x00489da0` at `0x0044961e`, one of the attach paths that reach the mesh build
`0x004bd830`). Sectors are never placed, so **the global list holds the sector object(s) plus the
few objects that are allocated and not yet placed**. `SA_CleanUpObjects` (`0x0043a460`) kills every
non-sector parentless node, confirming the design.

**Sector object** (class 1, `0x180`-byte class data): constructor `0x004524d0` zeroes `+0x130..+0x174` (`+0x178 = -1`),
`+0x13c = 0` at `0x004524f6`, size `+0x14c = 10,000,000`, flags `+0x148 = 2` when subtype is 0.
Fields: `+0x130` sector scene (`SA_SetSpaceScene`, 0x128 → `0x00452570`, which also copies the
record's colour into the scene), `+0x134` galaxy scene, `+0x138` dust scene, `+0x13c` background
index (`SA_SetSectorBackgroundType`, case 0x133: `MOV [EAX+0x13c],ECX` at `0x004644b0`),
`+0x140`/`+0x144` stars/nebula index, `+0x148` flags, `+0x14c` size.

**Who allocates class 1.** Of the 28 `SA_AllocObject` sites in the script (the last pushed argument
is the main type, read at `args+1` in `0x00460630` case 0), exactly two push main type 1:
`2001::Activate` (the sector class) with **subtype 0**, and `621::Create` (cut-scene spaces for
`GetCutScene`/`GetCut2Scene`/`GetCut3Scene`/`GetComp2Scene`) with subtype `member14 ∈ {1,2,3,4,5}`
(initial `0xff`). `621::PlayThread` also writes a real sector's background index into such a space
(`0x0f386`). **Subtype 0 therefore distinguishes the flown sector from every cut-scene space.**
Four sites take the main type from a member or an expression (`2084`, `1652`, `2016` objects and
`2216::SpecialUpdate`); none is plausibly 1, not proven.

## 2. Gate transit, jumpdrive, QuickWarp: one synchronous script run

Gate (`2004::__FlyToNextSector` → `150::StartGateWarp`) and jumpdrive (`2004::JumpToSector` →
`StartGateWarp` or `StartSectorWarp`) both end in **`150::WarpToSector`**; `150::QuickWarp` (hotkey
input `2228::Input`, mission director `308::DoWarpPlayer`) runs the same sequence without the fade.
Script CODE offsets (class::method, native command), in execution order:

| # | Script site | What happens in the engine | D3D creations |
| --- | --- | --- | --- |
| 0 | `1660d` `TI_Delay` after `StartFade` | last yield before the stall: frames are presented during the fade | per frame |
| 1 | `16617` `605::StopAllMonitors` → `606::StopMonitor` → `INS_CockpitFree` (`f0085`) | **the cockpit is freed**; its registry row disappears | releases |
| 2 | `1662c` player(402)`.LeaveSector(old)`; `16665` `150::LeaveSector` → old `2001::Deactivate` | old objects deactivated; **old engine sector freed** (`SA_FreeObject` `8b513`, Remove at `0x0043fc0b`) and its three scenes freed | releases |
| 3 | `1666c` `SA_CleanUpObjects`, `16673` `SA_FreeAllBodies` (case 0x13b: `0x0048a060`, `0x00486920`, `0x0048a1e0`, `0x0043a4a0`, `0x004d1f80`) | all bodies freed, then **`0x0048a1e0` reloads bodies and rebuilds meshes for the instances left in enabled scenes** and `0x0043a4a0` → `0x00443280` re-requests the bodies of parentless objects (`0x0044336d` → `0x0047eb90` → `0x004863c0`) | **yes, not destination** |
| 4 | `16688` `150::EnterSector` → new `2001::Activate` (`8aa16`) | **`SA_AllocObject(1,0)` `8aa38`**: sector allocated, AddTail to the global list, `+0x13c = 0` | none (constructor only) |
| 5 | `8aa53` `SA_SetSectorBackgroundType` | **`+0x13c` = destination background index** | none |
| 6 | `8aa60`/`8aa6d` stars/nebula index; `8aa7d` scene `CreateAlloc`; `8aa93` `SA_SetSpaceScene` (`+0x130`); `8aaa0` size; `8ab01` galaxy scene | scene objects | none expected (B3D scene allocation) |
| 7 | `8ab3b`…`8af06` `Activate`/`Activate2` over every object table, `ActivateStartTasks`, `CreateSwarm`, `8b0f3` dust scene, `INS_InitDustScene` | each object: `SA_AllocObject` → `SA_StartObjectInSpace` → scene attach → body load and mesh build | **the destination's bodies, meshes, textures** |
| 8 | `166c3` player`.EnterSector(new)` (`402`, `AddTo` at `e3dda`) + `SetPos` `166f2`, **or** `16715` `WarpEnterSector` → ship `__LeaveHyperspace` (`AddTo` at `bd31b`) | **player ship's `+0x54` = new sector**, arrival position set | the player ship's bodies |
| 9 | `1671f` `605::RestartAllMonitors` → `OpenLayout` → `606::StartMonitor`: `INS_CockpitAlloc` `f01c5`, **`INS_CockpitSetSectorSpace` `f0206`** (the only call site in the script; engine write `MOV [ESI+0x54],EAX` at `0x0042d670`), then `SetActiveControl` `edca0` | **cockpit `+0x54` = new sector**, active-control handle | cockpit scene/bodies |
| 10 | `1672c` `ShowWarpOutScene` (its `Play` is forked) … next `TI_Delay` | stall ends; first presented frame | — |

No yielding native (`TI_Delay`, `TI_DelayRandom`, `TI_Wait*`) is reachable outside a fork between
rows 1 and 10 (a by-name over-approximation of every dynamic call to depth 3); `TI_Interrupt` only
signals (`0x004b1417`: `0x004b0bf0`, no suspend). The single exception is the carrier-hangar arrival
loop in `2004::__LeaveHyperspace` (`TI_Delay` at `bd2fa` after `SA_StartInHangar`), which can split
the stall after row 7. `150::LeaveSector`, the two natives of row 3 and `150::EnterSector` run only when the sector
coordinates change (test at `16632`–`16651`).

Consequences: the destination identity (row 5) exists **before every destination resource
creation** (row 7) and only D3D work of row 3 precedes it inside the stall; the cockpit write (row 9)
comes after all of them. Row 7 is the bulk of the stall (the bodies of every object), so the lead
time is most of the measured 4.4–6.2 s (inferred; the §3.A diagnostic flight measures it). The
arrival position (row 8) is known only at the end of the stall, so the prefill window must be centred
at the sector origin, as §3.A already proposes.

## 3. Save load: the sector appears after the bodies

`0x00404cc0` (called from the main loop at `0x004038ba`) restores, in order: timers; KC VM state
`0x004a0880` (`STOR`…`TASK`); **B3D world `0x0047a720`** (`INST`, `SCEN`, `CUT `, `TAKE`, then
`0x0048a1e0` at `0x0047b295`: body load `0x004863c0` and mesh build `0x004bd830` for every instance of
every enabled scene, i.e. the destination's geometry and textures); movies `0x00498ad0`; galaxy
`0x00417950`; **universe `0x0043f420` at `0x004050c9`**: teardown `0x00412d00`/`0x00439530`,
**type tables incl. TBackgrounds rebuilt** (`0x00434e40` at `0x0043f43d`), `SECT`: every engine
object pre-allocated by `malloc` + `memset` and registered in the id hash only (sectors: `0x180`
bytes, class word from the stream), `SOBJ`: `0x004421a0` per object (sector: `0x004304b0` reads
`+0x130..+0x178`: scenes `+0x130..+0x138` ending at `0x00430522`, index `+0x13c` at `0x0043052d`, only stream reads in between), post-pass `0x00442c00` at
`0x0043f83f` links the sector into the global list, player ship at `0x0043f8be`; then a **new cockpit
registry** (`0x0041c960` on a `0x458` allocation, `MOV [0x00608504],EAX` at `0x00405106`); cockpit restore `0x0041f720` →
`0x00425e20` → `0x00419430`, `[cockpit+0x54]` from the saved id at `0x004196e7`/`0x004196f0`; `SFX `
`0x0049b9d0`; `/XSA`.

So on a save load the destination sector is neither allocated nor in the list while its bodies and
meshes are built; it becomes readable (list and cockpit alike) only in the last part of the stall.
A prefill cannot start earlier from engine objects (inferred from the order; runtime not measured).
The TBackgrounds table is reallocated inside the same stall, before the sector is linked, which is the
§11.4 hazard window: the recipe below re-reads `0x00606fc0`/`0x00607040` on every sample and cannot
reach a sector before the post-pass, so it never pairs a new sector with the old table.

## 4. The first presented frame

For gate/jump, rows 1–10 complete before the frame after the stall is rendered, so statically the
cockpit and `+0x54` exist at that frame. run271 measured `no_cockpit` (registry handle 0) on that frame
and Ready one frame later (fog-handover §1). Consistent explanation, not proven: `606::SetActiveControl`
passes 0 to `INS_SetActiveControlCockpit` (`efd5d`–`efd5f`) when `CanGetActiveControl` (`efce5`)
refuses, and a later task sets it. Either way the cockpit route is not available before the stall ends.

## 5. Cheapest read side (question 2)

**No trampoline for gate/jump.** From the proxy's existing resource-creation wrappers
(`CreateTexture`/`CreateVertexBuffer`/`CreateIndexBuffer`, reached from `D3DXCreateMesh` inside the
mesh build and from the texture loader), on the engine's main thread, gated as in §3.A (the current
frame is older than `fog_density_gap_ms` and at least 250 ms since the last poll; zero work in an
ordinary frame):

1. `M = *(u32*)0x0060850c` — non-zero, 4-aligned.
2. `node = *(u32*)(M+0x10)` (`lh_TailPred`).
3. Up to **8** steps: stop when `node == M+8` (the head pseudo-node; inferred from the Exec layout,
   the list initialiser was not decompiled) or `node == 0`; read `w = *(u32*)(node+0x48)`; if
   `(w & 0xffff) == 1 && (w >> 16) == 0` take it; else `node = *(u32*)(node+4)`. Every node 4-aligned.
4. Candidate checks: `*(u16*)(node+0x9c) == 0xcafe`; one read of `0x20` bytes at `node+0x130`
   (scene, galaxy, dust, index, stars, neb, flags, size) with **scene `+0x130` ≠ 0** (set at row 6,
   after the index at row 5, so a sector caught between rows 4 and 5 with its constructor index 0 is
   refused); `id = *(u32*)(node+8)` **≠ the id of the last Ready sector** (the old sector is freed at row
   2, but an allocator may hand the same address to the new one; ids are not reused within a session).
5. The existing §11.4 recipe from hop 4 on: count `0x00607040`, table `0x00606fc0`, one `0x120`-byte
   row read, one 32-byte name read, then `fog_sector_placement(index, profile, recipe)`.

Worst case 2 + 16 + 3 + 4 = **25 reads**, all through `engine_memory::read` with `GetLastError`
preserved. A walk that reaches the bound without a candidate (many unplaced objects at the tail,
e.g. a large ship's turrets between its allocation and placement) returns "none" and the next poll
250 ms later tries again. The walk is backward because the destination is appended during the stall;
the steady-state list has the flown sector plus parentless leftovers, whose count is not bounded
statically.

Hook-site suitability of this read: no patch, no instruction boundary, no register or flag state
involved; it runs inside a D3D wrapper that already saves/restores `GetLastError`. At any D3D call the
engine is not inside a list mutation (the AddTail/Remove sequences above contain no calls), the
engine is single-threaded (sector-fog.md §11.3), and freed nodes are unlinked before `free`
(`0x0043fc0b`), so the walk never follows a dangling link; a stale page is still caught by the
committed-page read. Reentrancy: nested wrapper calls from `D3DXCreateMesh` only repeat a bounded
read; the hand-off to the density worker must not block the engine thread (post, never wait). Keep no
node, table or record pointer across polls. Windows: the same EXE offsets and reads; no Wine dependency.

**If a write hook were ever wanted** (not needed): the right site is `SA_SetSectorBackgroundType`
in `0x00460630` case 0x133, **`0x004644ad`** `8b 4e 06` `MOV ECX,[ESI+6]` + `89 88 3c 01 00 00`
`MOV [EAX+0x13c],ECX` (9 bytes, two position-independent instructions, no cross-reference into
`0x004644ad`–`0x004644b7`), EAX = sector (class-checked by `0x0043a560`), ECX = index, ESI = argument
cells, flags dead (the path continues `PUSH ECX; JMP 0x00462df2`, which reaches `CALL 0x004a47f0`
without reading flags). It also fires for cut-scene spaces (filter on subtype 0) and scripted
background changes (`2001::SetBackgroundType`, `0x8973f`). The brief's candidate `0x0042d670`
(`8b ce 89 46 54 e8 e8 2c ff ff` from `0x0042d66e`; the `CALL 0x00420360` is rel32 and would need
relocation) is useless for lead time: it runs at row 9.

## 6. Confirmation and the cost of a wrong read (question 3)

The prefill is not authority (fog-handover §3.A). It records `{id, sector, index, profile, recipe,
placement key}` and calls `configure`. At the detector's first `Ready` sample (cockpit → `+0x54` →
`Sample::sector`, `index`, family → `fog_sector_frame`):

- `Sample::sector == prefill.sector` and `*(u32*)(sector+8) == prefill.id` and the placement key
  (`index`, `profile`, `recipe`) equal → confirmed; `configure` is a no-op and R1 steps readiness once
  the real camera's need box is resident. Statically this is the expected outcome for every gate/jump:
  `INS_CockpitSetSectorSpace` resolves the same id through `0x0043a560` to the same object.
- Different object, same placement key (a sector sharing the background record) → keep the fill; the
  key, not the heap token, owns the field (`fog_sector_policy.h`).
- Different key → `configure` invalidates: today's behaviour.

What a wrong early read costs: at most one far-need-box fill on the worker thread during a stall the
engine spends anyway (about 0.55 s of one core at the measured 1.93 M nodes/s, fog-handover §1,
inferred), plus the CPU it takes from the single-threaded load on a machine with few cores. It cannot
draw: drawing stays gated by `fog_sector_.current(frame_)`, and nothing uploads before the first
frame. Sources of a wrong read and their guards: a cut-scene space (subtype ≠ 0), the old sector
(freed at row 2; id check), a half-initialised sector (scene check), a TBackgrounds rebuild on save load
(sector unreachable until after it, §3), a main-menu entry with no previous Ready id (a real
subtype-0 sector found there is accepted and corrected at Ready).

## 7. What remains unknown

- The lead time itself: the split of each stall into rows 1–3 and 7–10, and whether a resource
  creation (hence a poll) falls early in row 7. The §3.A diagnostic flight settles it: log the
  walk's status, steps, id, index and family per poll with ms until the stall ends.
- The steady-state and in-stall length of the parentless tail (the 8-node bound is a judgement).
- Why the first frame after a transit samples `no_cockpit` (§4).
- The main types pushed by the four member/expression `SA_AllocObject` sites (§1).
- The new-game / first-entry path (run271's first entry after 6 s of `no_cockpit` frames) was not traced.

## Reproduce

```sh
python3 verification/results/sector-transit-order/kc_transit_order.py      # script order, derived names only
python3 verification/results/sector-transit-order/verify_transit_sites.py  # EXE hash + byte witnesses
```

Outputs: `kc_transit_order_out.txt`, `verify_transit_sites_out.txt` beside the scripts. Ghidra
(`X3LoadingOrchestration.java`, same invocation as loading-orchestration.md, with `-readOnly`):
`decompile 0042d340 00420360 0043a560 0041cd20 00460630 004524d0 00452570 0043a460 0043a4a0
0043f900 0043ffa0 0043f990 00449510 0043f420 00404cc0 004421a0 004304b0 00442c00 0048a1e0
0048a060 00443280 004b1100 0041f720`, `listing 004b12d0 60`, `listing 00464496 10`, `listing
004495e0 40`, `listing 0043fbf0 40`, `xrefs 004644ad 0042d670`.
