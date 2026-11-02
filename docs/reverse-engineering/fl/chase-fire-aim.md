# FL chase cursor-fire admission and read-only aim diagnostics

Bounded offline RE, 2026-10-29. **The current feature is one local admission
branch plus four diagnostic hooks; neither module rewrites the native ray or
adds finite camera-to-muzzle convergence.** FL retains the relevant native
cursor/unprojection/cone flow, but changes the fire ABI, cursor/cone cell order
and diagnostic stack layout. Keep policy and canonical diagnostic records
shared, with explicit FL extraction/address bindings. No production adapter,
execution, installation or visual acceptance is established here.

Inputs are `src/engine/ap/chase_fire.{cpp,h}` and `chase_aim_trace.{cpp,h}`.
Reuse accepted [camera](chase-camera.md),
[transition](chase-transition.md),
[lead/native aim publication](chase-lead.md),
[FL input/coordinate ownership](ui-input.md),
[UI command graphs](ui-graphs.md), and
[owner/callback exclusion](ui-render-exclusion.md).
The [AP cursor-fire note](../chase-mouse-fire.md) owns the original policy and
observations. Its AP gameplay numbers, compiled-script cursor condition and
old CPU-fixture results are not FL runtime evidence.

## 1. Feature and diagnostic policy, unchanged in scope

`chase_fire` is requested after successful chase-camera installation, independent
of telemetry and of transition/lead installation. It owns **one six-byte JZ**.
The original cursor-active comparison remains native. Only when that comparison
produces ZF=1 can the handler clear **saved ZF**, after proving an applied bound
chase context, current player/camera/view identities, main-gun group 0, flags
mask **0x22**, fresh same-thread QPC witness and valid native cursor/viewport.
It writes no cursor/steering state, fire arguments, ray, range, gun or camera.
A refusal preserves all saved flags and original branch behavior. An override
counter measures the local branch decision, not successful later ray admission.

The context policy is deliberately cross-update: same thread, nonzero monotonic
QPC, age at most **two wall-clock seconds**, same cockpit/ship/camera and ship
native ID, mode **258**, connect **0**, flag mask **0x4** clear, and equal
nonzero reference/view objects. It permits the last displayed pose before the
next camera update; requiring `current_update()` here would change that policy
because native fire can run outside the camera updater. Only a successfully
written **bound** chase pose publishes a mutating witness; unbound/refused visits
invalidate it. Unknown/inactive visits clear only the same previously accepted
cockpit address, as the current source does.

`chase_aim_trace` is separately telemetry-only and owns **four read-only sites**.
It retains native admission causes, the pre-cone ray, selected final direction
and the native cursor command. In particular, an effective extension override
must still report native active=0 / gate 3 at the early diagnostic site.
Downstream ray observation plus the separate override counter explains the
change; relabeling the original input as active would erase that evidence.
Internal/non-applied bound camera context is retained for A/B diagnostics.
Its two-second limit labels stale records and never controls native firing.

## 2. Actual FL fire ABI and native callers

All VAs are preferred addresses at image base `0x400000`; runtime means admitted
loaded base plus RVA. Native fire is **`[0x44e310,0x44ff46)`**. It receives
**ECX=ship**, then four callee-cleaned stack DWORDs:
**target, gun group, fire flags, mask**; its return is EAX and **RET 0x10** at
`0x44ff43`. AP's five contiguous stack arguments cannot be reused.

FL entry saves EBX and sets EBX=original entry ESP-4 at `0x44e311`, then creates
an aligned synthetic EBP frame and native SEH chain. Ship moves from ECX into
EDI at **0x44e34e**, then into **`[EBP-0x3c]` at 0x44e350**. The full-function
store census finds that as its sole writer, so the scalar ship identity is
available at all three diagnostic phases. Canonical extraction is:

| Canonical argument | FL native source |
| --- | --- |
| Ship | `[EBP-0x3c]`; source's early `field(EBX,8,ship)` would read target instead |
| Target | `[EBX+8]`; native picking updates this same stack argument at 0x44ed24/0x44ed40 |
| Gun group | `[EBX+0xc]`, main group 0 |
| Fire flags | `[EBX+0x10]`, loaded at 0x44ec82, masked with 0x22 at 0x44ec94 |
| Mask | `[EBX+0x14]` |

The mutating early filter needs ship plus group/flags, while diagnostics assemble
all five canonical values. They are **not** one contiguous five-DWORD FL read.
Every later phase must use the same ship extraction; otherwise an AI call whose
target happens to be the player could pass the AP-address filter incorrectly.

The complete surveyed direct caller list is **0x46bb81, 0x46bb9f, 0x46bbe4,
0x46bc03**, all in the native command dispatcher. Their pushes/ECX transport
independently establish four stack words plus ship ECX; omitted masks are -1.
The first pair supplies a resolved target, the second pair supplies zero target.
This is a native-command call boundary, not proof about which displayed camera
pose every shot uses. No new wrapper or engine call is needed for the extension.
Native calls, allocation/resource/VM work and native exceptions remain native.

## 3. Five proposed FL spans, instruction and relocation obligations

These are concrete counterparts of the current one feature/four diagnostic
roles; they are not installed profiles. Span boundaries are independently
compared with a full `.text` reference decode. Direct-edge and HIGHLOW censuses
find zero interior entries or relocated pointers into the spans. Arbitrary
computed/private-address or replacement-code entry remains outside this profile.

| Role / AP VA | FL half-open span | Live state and exact replay |
| --- | --- | --- |
| Cursor admission / 0x445a41 | **`[0x44ecad,0x44ecb3)`**, 6 | Near JE to **0x44ef9f**, rel32 field **+2**. It consumes native `CMP [0x5aa184],0` at **0x44eca6**. Change only saved ZF on qualification, replay JE with its loaded target. Native `TEST ECX,ECX` at **0x44ecb9** kills these flags after the registry load. |
| Diagnostic early gate / 0x445a15 | **`[0x44ec8b,0x44ec94)`**, 2+7 | `MOV EAX,ECX`, then `MOVAPS XMM0,[EBP-0x1c0]`. ECX contains flags, EBX the original argument frame and EBP the aligned native frame. Record canonical arguments/current native cursor before local admission; replay both whole instructions. The native AND at the continuation establishes its own flags. |
| Diagnostic pre-cone ray / 0x445b70 | **`[0x44ee56,0x44ee5c)`**, 6 | One signed `IMUL [0x6243c8]`, **HIGHLOW operand at 0x44ee58 (+2)**. At entry **EAX=ray length, ESI=chosen depth, ECX=raw ray z**; preserve XMM3/XMM4 and all other FP state. Observe before multiplying; replay against the loaded cosine cell exactly once. AP's store/load-length span has no direct FL equivalent. |
| Diagnostic chosen direction / 0x44605a | **`[0x44f405,0x44f40e)`**, 3+6 | Load EDX from `[EDI+0x70]`, form EAX=`EBP-0xf0`. EDI=ship; chosen world direction is already `[EBP-0xa0]`. Continuation pushes the supplied output and transforms through ship basis at 0x44f418. The formed `-0xf0` output is not the world muzzle position. |
| Diagnostic cursor command / 0x4074de | **`[0x47bc5d,0x47bc62)`**, 5 | Near JMP to **0x47b71e**, rel32 **+1**, immediately **after all three native cursor stores**. EAX is y, ECX the command-record array. Copy the just-written canonical cursor fields, preserve state, replay the unconditional jump. This changes observation timing from AP's before-active-store sample to FL's after-three-stores sample. |

Only the ray span contains a HIGHLOW operand. Rebase that loaded expected
absolute address and its relocated tail; it is not a relative displacement.
Both control transfers need newly encoded rel32 targets in a trampoline.
Synthetic checks cover image bases **0x100000** and **0x12000000**, without
executing loader relocation or native code. Five-byte qword-fit facts are in the
compact record; quiescent initial installation and exact byte ownership remain
required. None of these spans steals the native fire SEH prologue.

The chosen cursor trace span is intentionally **adjacent to, disjoint from** the
accepted UI coordinate span **`[0x47bc4d,0x47bc5d)`**. Observing the old active
store would not expose all three FL values in the AP registers; stealing the
last native y store would overlap that existing UI owner. The post-store JMP
preserves both existing responsibilities without introducing an owner framework.
When UI scaling is active, the observation must describe the resulting real
native pixels, not independently scale them again.

## 4. Cursor, cone, type and camera layouts

FL packs each triplet differently from AP:

| Canonical value | AP | FL and independent native operand |
| --- | --- | --- |
| Cursor active | 0x607ce8 | **0x5aa184**; writer **0x47bc48**, fire comparison **0x44eca6** |
| Cursor x | 0x607cec | **0x5aa180**; writer **0x47bc50**, fire push **0x44edf1** |
| Cursor y | 0x607cf0 | **0x5aa188**; writer **0x47bc58**, fire push **0x44edeb** |
| Cone cosine | 0x587b88 | **0x6243c8**; initialization **0x43d4b6**, fire multiply **0x44ee56** |
| Cone angle | 0x587b8c | **0x6243c4**; initialization **0x43d3cb** |
| Cone tangent | 0x587b90 | **0x6243c0**; initialization **0x43d587/0x43d5b5**, cone rescale **0x44eec9** |

A 12-byte read starting at FL active is wrong: x precedes active. Read the
bounded **x/active/y** block from 0x5aa180 and permute to canonical
**active/x/y**; similarly read **tan/angle/cos** at 0x6243c0 and permute to
**cos/angle/tan** for diagnostics. These are two small edition-bound copies,
not reasons to change the shared record format or repeatedly validate native
memory per draw. Native steering's separate cell **0x5aaa10** is written at
0x47bc40 and must not be changed by this feature.

The cursor command consumes five-byte records from its argument array:
steering at +1, active +6, x +0xb, y +0x10. Native stores have **no calls**
between active, x and y, so at the selected post-store seam these are one
completed native command's values. Existing FL UI evidence establishes
`X2_UpdateCursorSteering` and its real-pixel consumers; it does not prove that
FL's story emits active=0 in chase exactly as an AP capture did. User telemetry
must establish the actual FL input condition. The `.text` direct-reference
census finds active's only references at **0x44eca6 and 0x47bc48**; this is a
bounded direct-reference fact, not a proof against computed/foreign writes.

Other required bindings retain their accepted layouts:

* Cockpit registry root **0x5a82e4**, map +0, active handle +0x10, node
  `{next,key,value}` at +0/+4/+8; native resolver **0x41e420**. Current helpers
  bound 65,536 power-of-two buckets and 32 links. They do not explicitly refuse
  handle zero; FL admission must preserve the established nonzero active-control
  meaning instead of accepting a stale row zero as active.
* Camera/default root **0x5a82f8**, extra screen-record dereference at +0,
  signed dimensions +4/+6, default planes +0x28/+0x2c. Current camera viewport
  +0x288 retains normalized **y0,y1,x0,x1** order; FOV +0x298, planes
  +0x300/+0x304 and pose/basis fields match the camera note.
* Gun-view metadata root **0x5a83e4**, type/subtype shorts ship+0x48/+0x4a,
  type-row stride **0xdb8**, view count +0xd4 and mapping +0xdc with
  **0x18** stride. **0x45a930** receives ECX=ship and returns count EAX with
  plain RET; **0x45a9e0** receives ECX=ship and one callee-cleaned view-index
  word, returns mapping EAX with RET 4. The existing bounded diagnostic reader
  can reproduce these fields without calling either native helper.
* Common cockpit reference/view +0xc/+0x10, sector camera +0x58, relative basis
  +0xf0, mode +0x150, flags +0x1a0, connect +0x1c0, aim gun +0x1d8 and
  tracking target +0x1e0 retain their accepted roles. Ship id +8 and root+0x70
  remain distinct from a full lifetime lease.

Native cone initialization searches tuning ID **0x5f** at 0x43d380, defaults to
**30 degrees** at 0x43d38f, clamps to **0..89** and converts to binary angle
before the stores above. These are measured native operands, not a measurement
of loaded FL configuration or current runtime tuning.

## 5. Diagnostic ray, muzzle and endpoint extraction

Keep native integer values as observations, with explicit validity bits:

| Event quantity | Required FL extraction / native evidence |
| --- | --- |
| Ray before cone | **`[EBP-0x60]`** three words; loaded by 0x44ee19/0x44ee1e/0x44ee23 after native unprojection and main-group relative transform. AP uses -0x30. |
| Ray length | **EAX** at 0x44ee56, from sqrt 0x44ee4d then conversion 0x44ee52. AP reads its phase register/local layout instead. |
| Selected depth | **ESI** at this seam; the minimum 0x1388 (5000 engine units) is imposed at 0x44edd5..0x44ede0. Native calls preserve ESI through unprojection/length calculation. |
| Gun-group origin | **`[EBP-0xb0/-0xac/-0xa8]`**; native post-cone endpoint additions at 0x44ef4d..0x44ef73 consume these exact words. AP uses -0x130. |
| Chosen world direction | **`[EBP-0xa0]`** at 0x44f405; normalized/chosen branches join before the native ship-basis transform at 0x44f418. AP reads -0x50. |
| Individual muzzle world position | Three **noncontiguous** words **`[EBP-0x50]`, `[EBP-0x2c]`, `[EBP-0x38]`**. Stores 0x44eb73/0x44eb76/0x44eb79 establish the triple; endpoint-minus-muzzle operations 0x44f349..0x44f36f establish its use. Copy scalar words at this phase; do not read a 12-byte AP muzzle block at -0xf0. |
| Cursor-selected endpoint valid flag | **`[EBP-0x68]`**, set to 1 at 0x44ef87. AP's -0x74 is a different FL scratch/high-product slot. |
| World endpoint | **`[EBP-0x160]`** three words, published from the group-origin addition block at 0x44ef80 and consumed at 0x44f349/0x44f35a/0x44f369. Copy only for an admitted cursor ray; AP uses -0x100. |
| Picked target | Reread **`[EBX+8]`** at the ray phase; the original input target can have been replaced by the native picker. AP rereads EBX+0xc. |

Source's `pose()` diagnostic snapshot reads ship node+0x30/+0x40, not the
native-anchor reader's selected base/render domain. Keep its labels honest:
these values are diagnostic base-node samples, not automatically the anchor
that native follow selected. The canonical camera pose and relative basis
still come from current camera/cockpit fields.

Native range helper **0x43d6a0**, ECX=bullet index, plain RET, uses type root
**0x5a83c8**, stride 0xdb8, fields +0x50/+0x54, and signed product/1000. Target
selection uses the accepted lead-pair helper **0x427170** at 0x44ed15 or picker
**0x426d70** at 0x44ed3b. It limits depth by gun-origin distance and target size
before the 5000-unit floor. No extension call duplicates these calculations.

## 6. Shared cursor-ray math and unresolved finite convergence

Native unprojection **`0x4a50f0`** takes **ECX=camera**, four callee-cleaned
stack words **x, y, depth, output pointer**, and RET 0x10. Fire calls it at
**0x44edf7** with cockpit+0x58, the existing real cursor cells and chosen depth.
It uses the accepted FOV/plane/default-screen/normalized-viewport layout.
For main group 0, **0x44ee11 → 0x50e0c0** multiplies the result by cockpit+0xf0
before the cone comparison. Native cosine multiplication/rounding at
0x44ee56..0x44ee68 and comparison 0x44ee6e preserve the same cone contract;
outside-cone paths rescale rather than universally replacing the ray with
boresight. Final basis transport and origin additions remain native.

The shareable production arithmetic here is the bounded integer cursor/viewport
policy: positive signed-16 dimensions, ordered 16.16 bounds in [0,65536],
on-screen x/y and normalized cursor within those bounds. Those checks bound
`pixel*65536` below INT32_MAX, so the existing 32-bit integer divisions are
eligible unchanged. The camera's shared row-vector identity
`camera_basis = view_rel * ship_basis` preserves angular orientation through
this native ray path. Source's diagnostic cone classification
`ray.z < (length*cos + 0x8000)>>16` also remains eligible after reordering the
FL cone fields and extracting the correct FL ray/length.

Neither module contains a replacement unprojection or a new ballistic ray
solver. **Finite convergence is not implemented by these modules in AP or FL.**
The native post-cone endpoint adds the gun-group world origin, then individual
muzzles aim at that endpoint. It does not add the raised camera translation.
The admitted angular identity alone is therefore not proof of identical visible
camera-ray and muzzle-ray intersection. Do not silently add a camera-origin
shift, new range/cone rule or boresight fallback while porting this local gate.
A future correction needs its own coordinate/frame and gameplay evidence.

## 7. Lifetime, reentrancy, emitted CPU boundary and performance

The mutating context stores scalar cockpit/ship/camera identity, native ship ID,
thread and QPC; it has **no transition generation/update token**. Fresh native
field/registry checks plus current view are essential at use. Equal pointers/id
within two seconds do not independently prove no address/ID reuse across a
load. Bind FL use to the existing native owner/load/close/readiness and lifetime
admission services; unsupported foreign/reentrant or stale-load use must refuse.
This is the precise integration gap, not a request for another lifecycle
framework or for a same-update-only guard that would remove supported firing.
No module lock pins native storage. Reads and publication must occur on the
supported owner/native invocation; do not schedule native reads on a worker.

Diagnostic records are mod-owned copies only: **eight events per report**,
four fixed thread-pending entries, and first/last cursor records for three
context buckets. Early player filtering precedes locks/QPC/native pose reads.
At each eligible gate, pending serial/frame identity is replaced before reading
the new record; matching later phases require the same EBX/EBP/thread/serial.
Nested calls do not restore an outer pending record. Exceptions can leave an
unmatched record until next gate/report, which is safe only because it grants
no game mutation. Report sets pending sample slots invalid; dropped/overflowed
records cannot append to old samples. Counts describe hook-phase executions,
not proven distinct fire invocations, muzzle count, frames or FPS.

Both generated stubs save flags, all GPRs and XMM0–7, then call a four-byte-
incoming-stack-aligned C helper under full x87/MXCSR/LastError preservation.
The helper establishes empty/masked private x87 state and MXCSR 0x1f80, restores
native state afterwards, and diagnostic callbacks must leave it all unchanged.
Only qualified fire-admission ZF may intentionally differ. FL ray's IMUL needs
native EAX/EDX/flags/FP restored before replay; the native MOVAPS gate keeps XMM
values live as well. Current fire/aim source emitters **do not emit CLD**; saving
DF alone does not prove hostile-DF C-helper correctness. The FL boundary must
honor the project's clear-DF C ABI while preserving incoming flags/DF on exit,
and its actual compiled/executed helper paths need the focused CPU fixture.
No build or runtime preservation result is claimed here.

The helpers do not call the engine. Existing native fire/picker/resource/VM
calls still may invoke callbacks or throw; preserve their original continuation
and exception semantics. Do not hold a new lock or native borrow across an
engine call. Use initial quiescent installation, exact loaded spans, per-group
rollback and process-lifetime stubs through device recreation. Diagnostic failure
must not disable the independently installed admission branch; disabled telemetry
must still retain cursor-fire enhancement. Aim trace has no explicit public
shutdown; it is process-lived. Fire shutdown is explicit/quiescent.

There is no direct SETA API dependency in these two source modules. Their local
cursor policy neither stops/restores time warp nor ports SETA/manual flight.
SETA remains an external integration dependency through its own input/VM/native
adapters described by the transition contract; do not make reachable AP SETA
bindings appear FL-safe merely because chase fire or tracing is qualified.
Conversely, adding a SETA prerequisite to this gate would change its existing
telemetry-independent, camera-dependent behavior without evidence.

Source-derived hot-path bounds: active-native cursor calls leave the feature
handler after its enabled/ZF check; nonplayer/nonmain/noncursor calls perform
only the bounded argument extraction. Eligible inactive player calls use QPC,
one context/counter lock, at most 32 registry links and fresh bounded fields.
Camera publication adds ship-id sampling/thread/scalar copy. Tracing has four
full CPU boundaries when enabled and bounded stored event copies. It samples a
fire-entry pose on every eligible diagnostic gate even when the eight-record
window is full, plus an independent pose snapshot on each camera publication;
the storage cap does not cap those native reads. It adds no per-call log/file
I/O or allocation. Reports flush owned copies outside the lock. No per-draw work is
added. Timings exclude stated early filters/first QPC/full boundary and must not
be read as native fire cost or game FPS; actual FL timing remains unmeasured.

## 8. Reproduction and finite next checks

```sh
python3 verification/results/fl-port/chase-fire-aim-contract/produce.py
python3 -m py_compile verification/results/fl-port/chase-fire-aim-contract/produce.py
```

Measured offline result: **10 regions, 2,229 instructions, five seams, 108
operand anchors, 30 independent region/seam edges, nine rejected mutations,
two synthetic load bases**. The producer reuses project PE32/Capstone and FL
boundary/HIGHLOW tools; compact metadata is
`verification/results/fl-port/chase-fire-aim-contract/contract.json`. It checks
all four native fire callers, the sole saved-ship writer, cursor active reference
census, the nonoverlapping UI/trace spans and the ray HIGHLOW field. Mutations
bypass EXE identity and reject changed ship/argument ABI, cursor cell, ray/final
locals, writer/cone operand, cursor branch target and direct interior entry.
Original EXE is read-only; hashes bind offline provenance. Retain only producer
and compact record from the ignored result tree, with raw listings untracked.

Implementation acceptance remains finite: canonical FL argument/cursor/cone and
stack extraction; both admission branch destinations and relocated tails;
CPU/LastError/DF and live FP preservation; stale/foreign/load/address-reuse
refusals; honest native versus effective admission counters; diagnostic missing,
nested, orphaned and overflowed observations; compatibility with the adjacent
UI coordinate owner. User-only FL runs must compare internal/chase cursor fire,
cone behavior, actual writer inputs and marker/impact alignment. No production
edits, build, native/Wine/game run, install or commit was performed. Native
Windows behavior and finite-convergence accuracy remain unverified.

## 9. Verified 2026-11-03 against X3FL.exe

Re-checked on the installed `C:\X3FL\X3FL.exe` (SHA-256 `e7f3aad8…`, 38,327 HIGHLOW fixups, no other type; measured)
by `verification/probe/verify_chase_fire_sites_fl.py` (Capstone over the file, read-only; compact result
`verification/results/fl-chase-fire/verify_fl_sites.json`, PASS). Every claim of sections 2–4 that the admission branch
relies on holds; none had to be corrected. The `fl-port/chase-fire-aim-contract/` producer named in section 8 is not on
this branch; the verifier replaces it for this subset. Implementation: `src/engine/fl/chase_fire_fl.cpp` (it provides
`chase_fire.h` on FL in place of `src/proxy/chase_fire.cpp`, which stays AP-only with an unchanged object) with its
constants in `src/engine/fl/chase_fire_sites_fl.h`; the decision (witness freshness, identity, view, cursor/viewport) is
single-sourced in `src/proxy/chase_fire_core.h` and used by both editions. RVAs below (preferred VA = `0x400000` + RVA).

| RVA / field | Fact | How verified |
| --- | --- | --- |
| fire `[0x04e310, 0x04ff46)` | 1,843 instructions decode exactly to the end; `ret 0x10` at `0x04ff43`; four direct callers `0x06bb81`, `0x06bb9f`, `0x06bbe4`, `0x06bc03` (dispatcher: mask, flags, group, target pushed; ECX = the ship resolved by `0x443d30` from command record +1: `0x06bb49` (kept across the target lookup, tested at `0x06bb68`) or `0x06bbe2`/`0x06bc01`); no direct edge into the function's interior from outside and no HIGHLOW dword pointing into it; the SEH handler the prologue pushes (`0x14d3b5`: `mov eax,0x59c8b8; jmp`) loads FuncInfo RVA `0x19c8b8` (magic `0x19930522`, maxState 1, nTryBlocks 0, no try map): no catch resumes inside the function | linear decode, `.text` direct-edge census (72,230 edges), `.reloc` census by value, FuncInfo read; anchors |
| EBX / EBP | written only at `0x04e311` (`mov ebx,esp`: EBX = entry ESP − 4), `0x04e31d`/`0x04e324` (aligned frame), `0x04ff3f`/`0x04ff42` (epilogue pops) | Capstone register-write sets over the whole function |
| stack words | `[EBX+8]` target, `[EBX+0xc]` gun group (`0x04e343`, compared with the gun mapping at `0x04ed07`), `[EBX+0x10]` fire flags (`0x04ec82`), `[EBX+0x14]` mask. Only `[EBX+8]` is ever stored (`0x04e3fd`, `0x04ed24`, `0x04ed40`, the picker); no `lea` of EBX | store census over the function |
| ship `[EBP−0x3c]` | sole writer `0x04e350` (`mov [ebp-0x3c],edi` after `mov edi,ecx` `0x04e34e`), straight-line prologue, no edge into `(0x04e310, 0x04e353]`; reloaded at `0x04ecc9` after the resolver | store census, edge census; window `prologue` `5347c1f5` |
| seam `0x04ecad` | `0f 84 ec 02 00 00` `je 0x44ef9f`, rel32 +2; preceded by `cmp dword [0x5aa184],0` (`0x04eca6`, 7 bytes) and the native flag test `and eax,0x22; cmp al,0x22; jne` (`0x04ec94..0x04eca0`); the next flag writer is `test ecx,ecx` `0x04ecb9`; no HIGHLOW in `[rva−3, rva+6)`; no direct edge to its start or interior; RVA mod 8 = 5: the five-byte jump crosses an aligned qword (plain copy inside the install window) | exact bytes, Capstone, `.reloc` census, edge census; window `admission` `56a65ba8` |
| branch target `0x04ef9f` | `mov edi,[ebp-0x3c]` then the boresight path; reached only from `0x04eca0` and the seam | edge census, anchor |
| cursor cells | x `0x1aa180`, active `0x1aa184`, y `0x1aa188` (one 12-byte block x/active/y; the handler reads it once and permutes to active/x/y); steering `0x1aaa10`. Writer `0x07bc3a..0x07bc5d` stores record +1 steering, +6 active, +0xb x, +0x10 y with no call between; the cursor ray pushes y (`0x04edeb`) then x (`0x04edf1`) | window `cursor_writer` `e849e23a` (four HIGHLOW dwords), window `unproject_call` `f6c4c372`; anchors |
| cursor-active references | exactly two HIGHLOW dwords hold base + `0x1aa184`: `0x04eca8` (the seam's compare) and `0x07bc49` (the writer) | `.reloc` census by value |
| registry | slot `0x1a82e4` loaded at `0x04ecb3`; `push [ecx+0x10]; call 0x41e420` (`0x04ecc1`); resolver map +0, count +4, link next +0/key +4/value +8 (`0x01e420..0x01e450`). The FL walk refuses handle 0 (section 4) | window `admission` (HIGHLOW), anchors |
| native checks after the seam (kept native, not duplicated) | `cmp [esi+0x10],edi` view object = ship (`0x04ecd9`); aim gun `[esi+0x1d8]` ≥ 0 and < `0x45a930`'s count (`0x04ece2..0x04ecf9`); mapping `0x45a9e0` = group (`0x04ed07`); lead-pair helper `0x427170` / picker `0x426d70`; range helper `0x43d6a0` (`0x04ed4c`); depth floor 5000 (`0x04edd5`); unprojection `0x4a50f0` with ECX = `[cockpit+0x58]` (`0x04ede7`); main-group relative transform (`0x04edfc`); cone `imul [0x6243c8]` (`0x04ee56`) | anchors; window `admission`, `unproject_call` |
| unprojection screen/viewport | `0x0a50f0`: ECX = camera (`0x0a50fd`), defaults slot `0x1a82f8` (`0x0a50f4`, `0x0a51ef`, `0x0a5222`); screen record `[[slot]]` width `+4` (`0x0a51fb`, signed `0x0a51ff`) with x0/x1 `+0x290/+0x294` (`0x0a522a/0x0a5230`), height `+6` (`0x0a5270`) with y0/y1 `+0x288/+0x28c` (`0x0a5299/0x0a529f`): normalized viewport order y0, y1, x0, x1 as on AP; offset = pixel·65536/dimension (signed IDIV) − centre | window `screen` `c013bc5c` (two HIGHLOW dwords = base + `0x1a82f8`); anchors |
| cockpit/ship fields | +0xc/+0x10 refs, +0x58 camera, +0x150 mode, +0x1a0 flags, +0x1c0 connect, ship +8 id: the chase camera and chase lead verifiers' anchors (`verify_chase_camera_site.py`, `verify_chase_lead_sites_fl.py`) | reused, unchanged |

Run-time identity: before any write, the five windows (481 bytes) must match at base + RVA with each of their eleven
relocated dwords equal to base + its target; refusals `executable_mismatch`, `chase_camera_unavailable`,
`prerequisite_failed`, `invalid_base`, `unreadable`, `window_mismatch`, `slot_mismatch` (the row names the window),
`late_claim`. Install is all or nothing: claim, read-back, stub chain; a read-back mismatch writes the original five
bytes back inside the install window and counts as restored only when all six read back original.

**Handler on FL:** the stub tests the one-byte enable flag and the saved ZF before the CPU envelope (a natively active
cursor or a disabled module costs one byte test; a natively active cursor also bumps the stub's `lock inc`
counter `native_active`). Then `[EBX+0xc] == 0`, `([EBX+0x10] & 0x22) == 0x22` and
`[EBP−0x3c] ==` the witness ship, before any lock or QPC; the shared decision follows. AP's `[EBX+8]` ship read would
read the target on FL; the native fixture places the player ship there as a decoy and the call is refused. The early
filters count by reason (`early_no_witness`, `early_group`, `early_flags`, `early_other_ship`, `early_read`) on the FL
`chase_fire_window` row only (AP's row is unchanged).

**Aim trace decision:** `chase_aim_trace` stays AP-only (`reason=diagnostic_not_ported`). Its four read-only sites
need FL-specific stack extraction (section 5) and add nothing the first flight needs: the fire row's
`native_inactive_override` count shows the branch decision, and the user sees where bolts go. Port it if the flight
shows bolts not following the cursor despite overrides (it then separates cone/range refusals from aim error).

Not verified: the seam in game (not flown); FL's native cursor-active value in the chase view (AP's capture showed 0;
if FL leaves it 1 the native path already fires at the cursor: `native_active > 0` with
`native_inactive_override = 0`; if instead `early_*`/`refused_*` rise, the calls were never admitted); finite
camera-to-muzzle convergence (section 6: native endpoint = gun origin + camera-ray direction, as on AP).
