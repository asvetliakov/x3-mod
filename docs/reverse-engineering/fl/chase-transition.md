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
