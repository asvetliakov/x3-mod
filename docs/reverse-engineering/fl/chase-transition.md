# FL chase transition: cockpit tokens and supported view restoration

Bounded offline RE, 2026-10-29. **The existing fixed-table lifetime/update
policy can be shared, but the native register/frame bindings and VM call
policy cannot be transplanted from AP.** This note maps all current
`chase_transition.cpp` hook groups, the camera's two transition APIs, and the
update identity supplied to lead/HUD consumers. It does not implement or enable
FL restoration, and does not qualify lead/fire/steering hooks.

Current inputs are `src/engine/ap/chase_transition.{cpp,h}`,
`chase_transition_core.h`, `chase_transition_identity_core.h`, and
`chase_transition_restore_core.h`. Reuse the preceding
[chase-camera contract](chase-camera.md),
[own-ship/cockpit ownership](own-ship.md),
[HUD camera lifetime](ui-camera-lifetime.md),
[FL input transport](ui-input.md),
[FL named dispatch/completion](ui-refresh-completion.md), and
[whole-load ordering](ui-save-load-comparison.md).
These already establish native owner/callback closure, loaded-base use, hash
layout, and VM records; no new lifecycle or scheduling framework is justified.

## 1. What current production actually supports

There are **six mandatory witness sites**, **three optional mode/load/connect
observers**, and **four shared injection/cancellation sites** (measured source
inventory). Mandatory sites are installed only when the chase camera is wanted
and installed. The observers are requested for telemetry or view restore; the
four injection sites can also be held by one `InjectConsumer` (SETA restore).
Those shared sites are claimed once, with one prefilter bit per consumer. The
camera's complete initialization/dependency closure remains required; merely
rebinding these sites does not port its other dependencies.

The current restore policy is broader than the early sector-only design:

* A readable, complete cockpit visit at mode **258**, connect **0**, flag mask
  `0x4` clear and nonzero equal reference/view pointers can arm a ticket, after
  proving the active handle, registry-live ship/script ID and controller ID.
  `restore_observe_frame` is called **before** the camera pipeline's verdict;
  arming is not proof that this visit subsequently wrote a chase pose.
* A free-command destruction of that generation makes it pending. A fresh
  complete cockpit from the alloc command, at internal mode **1**, becomes due
  after **two** eligible visits and fresh active-control/ship/controller checks.
  **Ship equality and sector change are not requirements in current source**:
  ship switch/death respawn can qualify; the controller must remain the same.
  `+0x1fc/+0x54` sectors are log-only. Do not restore superseded same-ship or
  different-sector conditions from older architecture paragraphs.
* At input-worker entry, re-read the bound cockpit, require the actual native
  controlled object to equal its reference and pause mask bit 1 clear, then call
  the captured monitor's **`SelectMode(258)`** by name. Observe its real mode
  store on the bound generation and attempt thread. A successful observed 258
  store permits the captured manager's **`UpdateVisibility()`** call. This
  preserves native/script geometry and HUD behavior; a bare `+0x150` write
  does not satisfy the feature.
* Default retries **3**, cap **5**, spacing **60 cockpit updates**, pending or
  abandoned-call expiry **600 cockpit updates**. A refusal, explicit other mode,
  load/epoch change, destroyed bound lifetime or exhausted attempts cancels.
  A new constructor from the save-load chain cancels a pending ticket. These
  are source counts, not measured seconds or rendering frames.

## 2. Complete native seam inventory

All addresses are preferred VAs at `0x400000`; runtime addresses mean the
admitted FL loaded base plus RVA. Every span is half-open. The listed FL spans
are **proposed bindings**, not installed profiles. Instruction counts/edges are
reproduced by the producer below.

| Kind / AP site | FL span and supplied native values |
| --- | --- |
| 0 construct `0x41f8d0` (7) | **`[0x420ec0,0x420ec6)`**, lengths 1+2+3: save EBX, establish its stack base, reserve 8. Incoming **ECX=cockpit**, original `[ESP]=caller`; AP reads the cockpit from stack+4. Constructor normal return is plain RET at `0x4215cd`. Do not read a partial owner's fields here. |
| 1 complete `0x41fe04` (5) | **`[0x4215c6,0x4215cc)`**, 1+2+1+2: pop ESI, restore ESP from EBP, pop EBP, restore ESP from EBX. **EAX=completed cockpit**, set at `0x421534` and preserved through the epilogue. All initialization, including conditional `+0x894`, has finished. Native FS registration was restored at `0x4215bf` before this seam. Replay stack-changing instructions exactly once, after restoring the original registers. |
| 2 destroy `0x41ffc0` (7) | **`[0x41e3f0,0x41e3f6)`**, 1+2+3: frame establishment/8-byte alignment. **ECX=cockpit**, original `[ESP]=caller`. This is FL's deleting wrapper, which unconditionally calls actual destructor **`0x421820`** at `0x41e3fa`, then frees `0x8c0` bytes; `RET 4` at `0x41e413`. AP hooked its directly called destructor instead. See provenance below. |
| 3 update begin `0x4205e0` (6) | **`[0x421f50,0x421f56)`**, 1+2+3. **ECX=cockpit**; AP uses stack+4. The token's frame witness is original entry ESP (return-word address). |
| 4 update internal exit `0x4216c3` (5) | **`[0x422f9b,0x422fa0)`**, 1+1+1+2: restore EDI/ESI/EBX/ESP. EBP still denotes the updater frame; its original entry ESP is **EBP+4**. Plain RET follows at `0x422fa1`. |
| 5 update ordinary exit `0x4216d3` (5) | **`[0x422fa9,0x422fae)`**, same lengths and frame relation; plain RET `0x422faf`. |
| 6 script mode `0x42e742` (6) | **`[0x431abc,0x431ac2)`**, one store **`[EAX+0x150]=ECX`**. EAX=cockpit, ECX=requested mode, but **EBX is the original dispatcher argument frame**, not EBP. The command table's case `0x30` at `0x432548` points to `0x431a9b`, which reaches this store. |
| 7 load mode `0x419e06` (6) | **`[0x41bf6e,0x41bf74)`**, one store **`[EDI+0x150]=EDX`**. AP used EBP as cockpit. Outgoing stream arguments are already on the native stack; preserve them. This observer cancels restore even without telemetry. |
| 8 connect `0x422cd0` (6) | **`[0x424570,0x424575)`**, 4+1: load EAX from stack+4, push ESI. Incoming **ECX=cockpit**, stack+4=mode; subsequent `0x424578` stores mode to cockpit+0x1c0. AP used ESI=cockpit/EAX=mode at entry. |
| Inject `0x410100` (6) | **`[0x4cfd00,0x4cfd06)`**, 1+2+3. **ECX=actual controlled object**, stack+4=processing flag, caller cleans that one word. AP's injection helper instead reads controlled object from stack+4. FL spills ECX at `0x4cfd0f`; normal RET is `0x4d232c`. |
| VM clear `0x49ea80` (8) | **`[0x4ba6b0,0x4ba6b5)`**, 3+1+1: reserve stack/save EBX/EBP. **ECX=VM**, preserved in EDI at `0x4ba6b7`; plain RET `0x4bb103`. Clear retires current task (`+0x1434` at `0x4ba881`) and class storage (`+0x20` at `0x4bab65`). AP received VM on stack. |
| VM load `0x4a0880` (7) | **`[0x4bcb10,0x4bcb15)`**, 1+2+2: frame/push -1, before native SEH registration. VM comes from image root **`0x5aa174`** at `0x4bcb32`; save stream is a native stack argument. Existing full-load admission owns the surrounding transaction. |
| Interpreter EH adapter `0x52f298` (5) | **`[0x54e80a,0x54e80f)`**, one MOV EAX of metadata pointer **`0x59e520`**; continuation tail-jumps to native handler `0x521368`. Interpreter registers this adapter at `0x4bed95`; its relocated pointer operand is **`0x4bed96`**. The stolen MOV itself has HIGHLOW operand **`0x54e80b`**, which must contain loaded-base-adjusted metadata. |

All thirteen spans have zero surveyed direct edges into their interiors and
zero relocated pointers into their interiors. The only relocated pointer to a
span start is the interpreter's registration of its EH adapter. That is expected
native publication, not an unexplained indirect edge. Apart from the EH MOV,
these spans contain no HIGHLOW operands or relative control flow. Replaying
FL's EH MOV requires rebasing its immediate; the source comment that all AP
spans are plain copies is not sufficient for FL. Reconstruct any new JMP/CALL
relative targets at their actual arena addresses. The compact record includes
five-byte qword-fit results; quiescent startup installation is required even
when a patch happens to fit. Keep exact span ownership and group rollback.

**Destructor caller selection is not a constant delta.** The real destructor
`0x421820` has one surveyed direct caller, `0x41e3fa`, and no HIGHLOW entry
publication. Hooking only that body would report `0x41e3ff` for every caller,
losing the free-command provenance. The wrapper has **13 surveyed direct
callers** and no HIGHLOW entry publication; its free-command caller is
**`0x43060d`, return `0x430612`**. Use that return address for the shared policy's
free origin. The constructor has two surveyed direct callers: command
**`0x4305a6`, return `0x4305ab`**, and load chain
**`0x420e0d`, return `0x420e12`**. Those are the FL alloc and load-constructor
origins. Other wrapper callers still revoke lifetime/tokens but must not open
this restore ticket. Native registry removal at `0x430605` precedes wrapper
entry, as established by the ownership note.

## 3. Task/dispatcher ABI and script-call contract

The current AP `restore_on_mode_store` and diagnostic `provenance` use EBP as
an argument frame. FL dispatcher **`0x430540`** constructs an aligned synthetic
EBP frame, retaining the original incoming stack in **EBX**: after its first
push, EBX=entry ESP-4; `0x43054d` reads `[EBX+4]` and `0x430564` loads the
command from `[EBX+0x10]`. Native dispatch comes from interpreter call
**`0x4c0783`**, return **`0x4c0785`**, after five caller-cleaned argument words.
Thus FL capture must use **EBX+4=return PC, EBX+0xc=task, EBX+0x10=command**.
Passing EBP from the AP observer can inspect unrelated aligned-frame locals.
The same preserved dispatcher EBX is available at free-wrapper entry for
optional destruction provenance. No extra native VM hook is needed to learn it.

Task layout is compatible: capacity `+0x10`, stack base `+0x14`, signed cell
index `+0x18`, next CODE offset `+0x1c`, current receiver `+0x3c`. FL interpreter
loads these at `0x4bedbc..0x4bedce` and publishes index/PC immediately before
native dispatch at **`0x4c0751/0x4c0759`**. The native callback argument table
is still VM+`(group+2)*24`; its userdata field is VM+`0x1454`
(`0x4c077a/0x4c0780`). Update the diagnostic provenance's expected dispatcher
to **`0x430540`**, expected native return to **`0x4c0785`**, VM root to
**`0x5aa174`**, and frame extraction to EBX. Never hardcode file CODE PCs as
runtime byte offsets or assume arbitrary readable bytes are valid CODE.

The bounded manager-return scanner can share its algorithm: FL return opcode
recognizes **tag 10 saved context immediately followed by tag 3 return PC** at
`0x4c0836/0x4c083a`, loads them at `0x4c0840/0x4c0843`, and treats zero return
as bottom (`0x4c084e`). Context class pointer remains +8; instance ID remains
+0. Retain only IDs/class IDs, never a task, member-array or context pointer
across a script call. The existing source reads at most **128 cells/640 bytes**
for this scanner, only on eligible mode stores. Runtime receiver lifetime and
VM epoch still govern dispatch, not the fact that a class/name was once seen.

FL named dispatcher **`0x4bb5b0`** has the already-established cdecl argument
shape `(callback_type, token, receiver_id, name, result_mode, argc, args...)`:
six caller-cleaned DWORDs for zero args, seven for one. Each argument becomes a
five-byte integer cell. **The FL callback-free selector is 32, not AP's 0.**
Both success (`0x4be9ee/0x4be9f1`) and failure (`0x4bb248/0x4bb24b`) reject
selector >=32 before indexing callbacks; failure can index selector zero.
A direct transplant of `restore_select_call`/`restore_refresh_call` or
`consumer_inject` would retain zero and violate that established FL contract.
The FL binding must supply type **32**, token **0**, result mode **1**, and the
same captured receiver/name/argc/value policy. This is an edition binding of the
engine call, not a reason to change the feature to simulated input or a native
mode write. Keep name storage immutable through the call.

AL from this dispatcher means accepted dispatch, **not completed KC execution**.
The state machine's real mode-store witness remains necessary, and pending,
failed/deleted tasks must not become a restored result merely because AL=1.
Reuse the existing FL completion/task-cancellation/interpreter admission
services when implementing the call boundary. No task pointer can survive
its disposal, and no lock, C++ guard or native borrowed pointer may span KC.
The injected engine call must run under the engine's saved x87/MXCSR/LastError
image; return bookkeeping re-establishes the helper's clean FP state before
finally restoring native state. A foreign exception propagates through the
injection frame with no destructor/cleanup from that frame, as intended by
current `x3m_chase_inject_enter`.

## 4. Roots, public APIs and shareable policy

| Binding / API | Required FL meaning |
| --- | --- |
| Cockpit registry | `*0x5a82e4`, active handle +0x10, map +0; native resolver `0x41e420`. Reuse known `{next,key,value}` rows. Current helper bounds buckets to 65,536, power-of-two count and chains to 32 links; nonzero active handle is required. No native resolver call is needed. |
| Simulation registry / ship | `*0x5a82ec`, map +0x14, native id ship+8; verify row value equals reference. Native `0x46a829 → 0x443d30` then `0x46a836` reads script ID **ship+0x94**, independently proving that field. |
| Excluded ship flag | Existing source refuses ship+0x40 mask **0x08000000**. FL native object traversal tests this exact mask at `0x465313` and skips marked objects. Its interpretation as deferred deletion is inherited AP terminology/inference; the FL operand and skip behavior are established. |
| Game/controller/pause | Root **`0x6243cc`**, controller +0x418, pause bits +0x4a0. Input transport loads this root at `0x4786ad`, reads pause at `0x4786bd`; worker loads root at `0x4cfd48`, passes +0x418 at `0x4cfd88` to its native by-name dispatch. AP's `0x57fc60` is wrong for FL. |
| VM / diagnostic identity | VM root **`0x5aa174`**. Existing FL notes establish singleton/class stride 0x38, classes/count +0x20/+0x1c, dynamic table +0x12d0, context descriptor +8 and member cells +0xc. Those layouts match `IdentityReader`; its embedded AP roots and optional story-cell assumptions must still be edition-bound. |
| `current_update(cockpit)` | Under the existing lock, return `{generation,serial,GetCurrentThreadId()}` only for this thread's active update and still-complete lifetime. Begin must use incoming ECX, entry ESP; both exits end with EBP+4. A nested/new begin permanently revokes the previous token rather than restoring it on return. Lead/HUD ownership compares all three scalar values and still independently validates camera/ship/registry identities. |
| `active_control_cockpit(cockpit)` | A complete nonzero lifetime generation, then an unlocked bounded current active-handle lookup matching this cockpit and nonzero handle. Called by the camera only for zero view-object visits. It does **not** itself require `current_update` or pin memory; owner-thread use inside the admitted cockpit update is essential. |
| `restore_observe_frame(frame)` | No new native read set for the supplied cockpit fields. Resolve its complete generation/constructor origin, then run the existing state machine with FL probes and epoch. Disabled costs one atomic load. Same-generation re-arm skips registry/ship/controller walks. It must not call the VM from the camera updater. |

The portable policy contains **64 lifetime rows, eight thread rows**, monotonic
64-bit generation/serial counters with exhaustion refusal, no eviction of active
thread tokens, and fail-closed unseen/partial/overflowed lifetimes. Constructor
entry revokes address reuse; only normal successful completion grants a
generation; destruction revokes all matching thread tokens. This policy and
fixed storage can be retained rather than adding a second lifecycle tracker.
Neither `generation` nor `current_update` independently proves a native object
is still live at an arbitrary call site.

The normal exit witnesses do not run on an abnormal updater unwind. Current
source's interpreter-EH epoch bump cancels restore, but does **not** clear the
`State` update table. Therefore no caller may use `current_update` outside a
positively admitted current native-update scope or after an abnormal unwind;
next begin/recreation revokes the prior token. An FL implementation must bind
that refusal to the existing owner/lifetime/exception admission services and
exercise it in a focused fixture. This is a precise gap in naked-token
qualification, not evidence that more whole-engine lifecycle hooks are needed.

`chase_transition` also calls `chase_lead::native_timing_invalidate` at
construction, destruction, update begin/end. That downstream implementation
must be FL-safe before the shared transition module can run. Generation/update
identity alone does not port lead's native timing or HUD hooks. Likewise the
shared `InjectConsumer`'s poll/result/vm/unwind callbacks and call target must
be edition-admitted; the transition module cannot bless an AP-bound SETA
consumer merely by sharing these four sites.

## 5. Fixed story evidence and remaining limits

The clean FL story has matching method identities and arities:
**606::SelectMode(1 argument)** at file CODE **`0x187c7f`**,
**606::StartMonitor(0)** at **`0x1875a1`**,
**605::ChangeViewForMonitor(2)** at **`0x185c24`**, and
**605::UpdateVisibility(0)** at **`0x184ec2`**. StartMonitor allocates a native
cockpit at **`0x187678`** and writes its mode at **`0x187c42`**. SelectMode
reaches the native mode setter at **`0x188111`**, after native geometry/connect
operations; ChangeViewForMonitor calls UpdateVisibility at **`0x185ddb`**.
UpdateVisibility calls Show at **`0x184f06`** and UpdateOverlays at
**`0x184f12`**. This establishes the same named-method/native-store relationship,
not a successful execution of SelectMode(258), exact FL rear-view framing,
absence of yields, or behavior under a replacement story.

The production capture selects the actual dynamic monitor and its caller at the
mode store; it does not require fixed class IDs for restore. Keep that property.
The optional diagnostic `IdentityReader` still assumes specific global/member
cells and class 0x96/0x25e. Matching a method name/ID is not proof of every
optional diagnostic cell's semantics under a different story; these cells are
not required by the current restore policy. The supported call path must
validate the currently resolved method/receiver and lifecycle epoch using
existing FL services. A missing method, dead receiver or unsupported pending
execution must refuse without a bare mode-write fallback. Exact flight behavior
and supported modified-story behavior remain implementation/user acceptance.

Preserve all GPRs, EFLAGS including DF, original stack/SEH chain, LastError,
x87 state/control, MXCSR and XMM0–7 around each observer. FL's EBX-based aligned
dispatcher and constructor epilogue make blanket AP stack/register assumptions
unsafe. The helper uses the project's four-byte incoming-stack/SSE2 contract,
then restores native state before instruction replay. VM EH cancellation must
remain lock-free: the native handler may be entered while another observer
holds its lock. An adapter invocation is a conservative cancel boundary, not
proof of positive script completion; preserve native Search/ContinueExecution
and unwind semantics. Quiescent install precedes first cockpit construction;
use existing FL initial-static-load admission, group rollback and process-life
ownership. Device Reset/release does not retire engine witness sites.

Cost follows current source bounds: one fixed-table begin/end visit per
registered cockpit update, O(64) lifetime and O(8) thread lookups under its SRW
lock, optional timing only when telemetry is enabled. Inactive restore is one
atomic load; an idle shared injection stub is one prefiltered memory compare
under saved flags. No registry walk on same-generation re-arm; at candidate,
injection or eligible mode capture use only bounded walks/640-byte stack copy.
Diagnostics keep first 16/last 32 events and suppress unchanged records before
extra geometry/identity reads. Logging occurs after releasing the module lock;
no lock survives an engine call. No per-draw engine reads or allocations are
required by this mapping. Runtime timing remains unmeasured.

## 6. Reproduction

```sh
python3 verification/results/fl-port/chase-transition-contract/produce.py
python3 -m py_compile verification/results/fl-port/chase-transition-contract/produce.py
```

Measured static checks: **21 regions, 2,613 instructions, 13 seams, 65 operand
anchors, 68 independently compared region/seam edges, seven rejected
mutations**, four story method identities and six selected story edges. Source
hook inventory is checked separately. The producer uses project PE32, Capstone,
boundary/HIGHLOW, CAT and KC readers; only derived metadata/hashes appear in
`verification/results/fl-port/chase-transition-contract/contract.json`.
Mutations bypass image identity and reject changed constructor result,
dispatcher frame, mode owner, input-owner spill, callback bound, EH metadata and
an injected seam-interior branch. Original EXE/story files are read-only; hashes
bind offline provenance rather than production admission policy. The ignored
result tree should contribute only this producer and compact record; raw
listings stay local and untracked.

Finite implementation checks remain: supplied-register/frame adapters;
loaded-base-aware EH immediate and thirteen-span rollback; CPU/stack/LastError
and both update exits; early partial/reused/foreign/nested/unwound lifetime
refusals; callback-type32 success and missing-method failure; accepted versus
pending/failed/completed KC; actual captured mode result and visibility refresh;
shared injection-consumer ordering/cancellation. No production edits, build,
Wine/native/game execution, installation or commit was performed. This source
mapping does not establish native Windows runtime or FL gameplay acceptance.

## Verified 2026-11-02 against X3FL.exe

Decoded from the installed `C:\X3FL\X3FL.exe` (SHA-256 `e7f3aad8…`, read-only) with Capstone and the `.reloc`
census by `verification/probe/verify_chase_transition_sites_fl.py` (PASS; record
`verification/results/fl-chase-transition/verify_fl_sites.json`). All values below are RVAs (runtime = module base +
RVA) or field offsets; "anchor" means the exact Capstone text at that RVA is checked; "window" means the bytes are one
of the 26 identity windows the module compares at the loaded base before any write
(`src/engine/fl/chase_transition_sites_fl.h`, FNV-1a with relocated dwords zeroed, each fixup checked against base +
its target). Every span: exact bytes, whole instructions of the listed lengths, no relative control flow, no
HIGHLOW inside (except the EH immediate), no direct edge into an interior (72,230 direct edges surveyed), no relocated
pointer into an interior. Production binding: `src/proxy/chase_transition.cpp`, `#if X3M_EDITION_FL` blocks.

| Kind | RVA, lengths | Register ABI used by the handler | Inbound edges to start | Write | How verified |
| --- | --- | --- | --- | --- | --- |
| 0 construct | `0x020ec0` 1+2+3 | ECX = cockpit (`mov edi,ecx` `0x020ef1`), `[ESP]` = caller return | 2 (`0x020e0d`, `0x0305a6`, the only callers) | cmpxchg8b | bytes, anchors, caller census, window `construct` |
| 1 complete | `0x0215c6` 1+2+1+2 | EAX = cockpit (`mov eax,edi` `0x021534`; 21 instructions to the span, no EAX write, no edge from outside into `[0x021536,0x0215c6]`); plain RET `0x0215cd` | 0 | plain (qword crossing) | regs_access sweep, window `complete` |
| 2 destroy | `0x01e3f0` 1+2+3 | ECX = cockpit (`mov esi,ecx` `0x01e3f8`), `[ESP]` = caller; calls the destructor `0x021820` (its only caller), frees `0x8c0`, `ret 4` | 13 calls, no HIGHLOW | cmpxchg8b | anchors, caller census, window `destroy` |
| 3 update begin | `0x021f50` 1+2+3 | ECX = cockpit (`mov ebx,ecx` `0x021f5e`), frame = entry ESP | 1 (`0x01e5b0`, the only caller) | cmpxchg8b | anchors, window `update_begin` |
| 4 / 5 exits | `0x022f9b` / `0x022fa9` 1+1+1+2 | entry ESP = EBP + 4 (`pop ebp; ret` at `0x022fa0/1`, `0x022fae/f`) | 0 / 3 | cmpxchg8b | anchors, window `update_end` |
| 6 script mode store | `0x031abc` 6 | EAX = cockpit, ECX = mode, EBX = dispatcher argument frame (`mov esi,[ebx+0x18]` `0x031a9b`, `push [ebx+0xc]` `0x031ab9`) | 0 | plain | anchors, jump-table entry `0x032548` (HIGHLOW) = base + `0x031a9b` (case `0x30`), window `mode_script` |
| 7 load mode store | `0x01bf6e` 6 | EDI = cockpit, EDX = mode | 0 | plain | anchor, window `mode_load` |
| R0 inject | `0x0cfd00` 1+2+3 | ECX = controlled object (spill `0x0cfd0f`; both callers `0x0787c8/0x0787dc` set `mov ecx,esi` `0x0787b9`) | 2 | cmpxchg8b | anchors, caller census, windows `inject`, `inject_callers` |
| R1 VM clear | `0x0ba6b0` 3+1+1 | ECX = VM (`mov edi,ecx` `0x0ba6b7`); handler ignores registers | 3 | cmpxchg8b | callers `0x07861b`, `0x0b30cb`, `0x0b9aef`, window `vm_clear` |
| R2 VM load | `0x0bcb10` 1+2+2 | none read | 1 (`0x07983c`) | cmpxchg8b | window `vm_load` |
| R3 EH adapter | `0x14e80a` 5 | none read; `mov eax,<base+0x19e520>`: HIGHLOW at +1, the expected bytes are bound to the base, the tail copy is the live relocated instruction | 0; HIGHLOW publication `0x0bed96` only | cmpxchg8b | fixup census, anchor `jmp 0x521368`, window `eh_adapter` |

| Fact | Value | How verified |
| --- | --- | --- |
| Caller provenance (preferred VAs the core compares) | alloc `0x4305ab` (call `0x0305a6`), load chain `0x420e12` (call `0x020e0d`), free `0x430612` (wrapper call `0x03060d`, after the registry removal `call 0x50d040` `0x030605`) | anchors, windows `alloc_call`, `load_call`, `free_call` (static asserts tie each return to its window end) |
| Mode-store frame | `[EBX+4]` = native return RVA `0x0c0785` (after `call eax` `0x0c0783`, `add esp,0x14`), `[EBX+0xc]` = task, `[EBX+0x10]` = command `0x30`; dispatcher `0x030540` `push ebx; mov ebx,esp`, published only by HIGHLOW `0x01df32`, no direct caller | anchors, windows `dispatcher`, `native_call`, publication census |
| Task / context layout | task `+0x3c` context (`0x0bedbc`), `+0x18` signed index (`0x0bb620`), `+0x14` 5-byte cell base (`0x0bb62e`); context `+0` id, `+8` class descriptor (`0x0c0824`); return records tag 10 then tag 3 (`0x0c0836`, `0x0c083a`) | anchors. Task `+0x10` capacity is the offline note's value, not re-anchored: it bounds only the manager scan (HUD refresh), never the restore call |
| Named dispatcher | `0x0bb5b0` cdecl `(selector, token, key, name, kind, argc, args…)`, lookup `call 0x4bb310`, run `call 0x4bb470`; selector `32` = no callback (`cmp edx,0x20; jge` `0x0be9ee`, `cmp eax,0x20; jge` `0x0bb248`); the input worker itself calls it at `0x0cfd95` | anchors, windows `script_call`, `callback_bound_ok`, `callback_bound_fail`, `worker_call` |
| Game object | slot `0x2243cc`; controller `+0x418` (pushed by the worker `0x0cfd88`), pause bits `+0x4a0` bit 0 (`0x0786bd`, `0x0786c3`) | anchors, windows `worker_call`, `pause` |
| Cockpit registry | slot `0x1a82e4`; `+0x10` active handle, `+0` map `{buckets, count}`, row `{next, key +4, value +8}` (`0x0cf46f..0x0cf4a3`) | anchors, window `active_control` |
| Simulation registry / ship | slot `0x1a82ec`; `+0x14` object map keyed by object `+8` (`0x0495f1..0x0495fd`, the removal path); resolver `0x043d30` (map `+0x14`, value `+8`); script id `+0x94` (`0x06a836`); excluded flag `+0x40 & 0x08000000` (`0x065313`) | anchors, windows `sim_registry`, `ship_script_id`, `ship_flag` |
| Cockpit fields | `+0x150` mode, `+0x1c0` connect, `+0x1a0` flags, `+0xc` ref, `+0x10` view, `+0x1fc` / `+0x54` sectors | the chase-camera FL anchors (`fl/chase-camera.md`, "Verified 2026-11-02"), static-asserted in `chase_transition.cpp` |

View-mode writers: FL has no equivalent of AP's census of `+0x150` writers. A Capstone sweep of `.text` (`verification/results/fl-chase-transition/census_mode_writers.py`,
`.txt`) finds 34
dword writes at displacement `+0x150` (measured), 4 of them ESP-relative locals, so 30
register-relative writers; 3 are known cockpit writers (constructor zeroing `0x0212ab`, the load store `0x01bf6e`, the
script store `0x031abc`); the review checked 2 others as non-cockpit objects; 25 are unclassified (open). The restore
does not depend on that census (it acts on the observed script store and the fresh cockpit's frames), but an
unclassified engine writer of a cockpit's mode would show as `cancel=mode_left`, not as a restore failure.

Injection cadence: the input worker `0x0cfd00` is reached from `0x0787c8`/`0x0787dc` only. Pause bit 0 of
`[game+0x4a0]` branches past that path (`test cl,1; jne 0x4787ee` at `0x0786c3/0x0786c6`, anchored); the review reads
the `[game+0x499] == -1` exit of the command loop (`cmp edi,-1` at `0x07877b`, falls through to `xor eax,eax` at
`0x078797`) as a second skip (instruction anchors measured; the full CFG claim is the review's, not re-derived here).
A due ticket simply waits at such frames: the pending/due clock runs on cockpit updates, so the worst case is the
expiry at 600 updates (inferred, not measured in game).

Event window on FL (2026-11-03): with telemetry the FL install sets `diagnostics=1
diagnostics_reason=fl_verified_reads` and the AP event window runs on FL inputs only. Each `chase_transition_event`
row (first 16 and last 32 changes per report, identical consecutive witnesses of a cockpit suppressed, as on AP)
carries the witness kind (`kind_name=construct|complete|destroy|update|mode_script|mode_load`; the two mode-store
kinds 6/7 only with `chase_view_restore = 1`, because on FL their observer sites are claimed only with the restore), the
lifetime
generation and update serial, the normalized caller (alloc `0x4305ab`, load `0x420e12`, free `0x430612`; the mode
stores carry their own preferred VAs `0x431abc`, `0x41bf6e`), the requested mode, the active-control handle and a
snapshot of the verified cockpit fields only (`snapshot_valid` bits 1 mode `+0x150`, 2 connect `+0x1c0`, 4 ref `+0xc`,
8 view `+0x10`, 32 sector `+0x1fc`, 256 flags `+0x1a0`, 512 sector `+0x54`). A script mode store (kind 6) adds the
dispatcher frame chain the restore already reads (`frame=ok|refused`, task, context, `script_id`, `script_class`;
`fl_store_frame()`, one reader for both). A finished restore ticket adds one `kind=9 kind_name=restore` row with
`result`, `cancel`, `attempts`, `hud_refresh`, `mode_seen` and both generations, in sequence with the witnesses that
led to it. Still not ported, named in the install and window rows as `diagnostics_off=`: the connect observer (kind 8,
not claimed), the VM return-record and destructor provenance, `IdentityReader` and geometry (`+0xa8/+0x160/+0x120`),
whose FL contracts are unverified; the `+0x130` boom field (unverified on FL); and the `+0x58` world camera and
`+0x1e0` target, which are FL-verified ([chase-camera.md](chase-camera.md), [chase-lead.md](chase-lead.md) section 7)
but kept out of the row deliberately so it carries the restore's field set. No `chase_transition_detail` row on FL. The second injection consumer (SETA restore) is refused on FL (`inject_attach` →
`ap_only`).

Witnessed in FL run 25 (measured, `/home/asvetl/x3m-logs/fl-run25-restore-hud.log`): three
`chase_view_restore result=restored cancel=none attempts=1 hud_refresh=ok manager_scan=found mode_seen=258` rows.
`result=restored` requires the mode-store observer to have seen 258 stored into the bound cockpit on the calling
thread during the call (`on_call_result`), so `SelectMode(258)` does reach its native store inside the dispatcher's
`call 0x4bb470` on FL. `sector_from`/`sector_to` (`+0x1fc`) read 0 in all three rows (script-published, as under AP's
Mayhem); `+0x54` changed in two. Not verified: the FL story's behaviour under a modified story; the event window in
game (not flown).
