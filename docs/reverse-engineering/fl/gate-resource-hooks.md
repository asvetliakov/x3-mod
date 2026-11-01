# FL gate resource retirement and factory notification seams

2026-10-29. Targeted read-only reconstruction of clean FL 1.3, preferred base
`0x400000`, EXE SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
This implements no hook and grants no traffic admission. It narrows the
[ratified cold handoff](../../architecture/fl-gate-clearance-handoff.md), reusing
the reviewed [resource lifetime census](resource-lifetime.md).

```sh
python3 verification/results/fl-gate-resource-hooks/verify.py
python3 -m py_compile verification/results/fl-gate-resource-hooks/verify.py
```

The [producer](../../../verification/results/fl-gate-resource-hooks/verify.py) and
[compact evidence](../../../verification/results/fl-gate-resource-hooks/evidence.json)
record **92 operand anchors, 13 bounded regions / 303 instructions, eight bit
comparison witnesses, five CWD result paths, the consumed-scope exception
witness and two rejected span/target mutations** (measured host/static checks).
The prior 428814-instruction direct-reference census is reused, not rerun.
The small additional HIGHLOW target scan covers only named dispatch entries.
Native code, game, PE compilation, Wine, installation and commits were not run.
Raw listings remain local/untracked; these files contain derived findings only.

## 1. Mutation boundaries, stack and replay

| Seam | Exact displaced work and continuation | Boundary evidence |
| --- | --- | --- |
| `SE_ChangeDir` | `0x4caa38`: one five-byte relative CALL to `0x5319c7`; continuation `0x4caa3d` | Original argument was pushed at `0x4caa37`. Plain cdecl return; caller removes one DWORD at `0x4caa3f`. The five bytes fit an aligned qword. A copied relative CALL needs rel32 rebasing; a direct `claim_call` replacement instead validates the original target. |
| `P_SetSysFlags` | `0x4b3b7c`: one six-byte `mov [eax+0xac],ecx`; continuation `0x4b3b82` | EAX=G, ECX=the full new DWORD. No relative operand; a plain-copy tail can replay the store once and jump to `+6`. The span and its first five patch bytes cross a qword boundary. |

Both spans have zero direct interior edges and zero overlapping HIGHLOW fields
in the accepted lifetime census. The targeted HIGHLOW-value scan also finds
zero published pointers into their interiors. These are bounded executable
censuses, not proof against computed/foreign entries. Loaded addresses must be
the admitted FL base plus RVA, with no AP fallback. The system-flag MOV cannot
claim an atomic installation path: claim and rollback need the existing early,
exclusive startup window, exact bytes/target/protection checks, reverse recovery,
retained inert tails and pinned owner. Neither seam may be installed live.

At the CWD seam, the surrounding native dispatcher is `0x4c84a0`. Its EBP
frame is established after realignment and an FS:[0] handler is linked at
`0x4c84c4`. EDI is the VM object, ESI=1, EBX preserves the original stack base.
After the path push, **ESP=EBP-0x184**, `[ESP]=char* path`; the existing outer
SEH record is still active. ECX/EDX/XMM/x87 and flags cannot be declared dead
merely because a C call follows. EAX's returned CRT status is consumed at
`0x4caa42`; EBX/ESI/EDI/EBP and exact caller ESP must survive.

At the flags seam, let E be ESP at `0x4b3b00` entry. Native code has saved EBP
and ESI and queued the VM result arguments, so **ESP=E-16**:

| Offset at seam ESP | Existing word |
| --- | --- |
| `+0` | VM result-cell pointer |
| `+4` | New full flags DWORD |
| `+8/+12` | Saved ESI / EBP |
| `+16` | Original native caller return address |

EBP=1, used for the native transport success return at `0x4b3b8e`. EAX and ECX
must retain their values until the displaced MOV. The continuation reloads
ECX from `0x5aa174`, then calls result setter `0x4c1940`, which consumes the two
queued words (`ret 8`) and writes the new flags as the VM integer result.
The case restores ESI/EBP and uses plain `ret` at `0x4b3b91`. The common VM
caller removes its five arguments at `0x4c0785`. Do not invent callee cleanup
for this mid-function seam or pop the already queued result words.

The MOV changes no flags. In the inspected continuation arithmetic flags are
overwritten by result setter CMP `0x4c1952` before its first conditional reader;
the VM caller likewise executes ADD/TEST after native return. CWD's original
CRT entry overwrites arithmetic flags before reading them, and its continuation
starts with XOR ECX. Preserve exact flags/DF in the observer envelope anyway;
no new permission to alter calling state is needed for these rare commands.

## 2. CWD success/failure and minimal forwarding

`0x4caa32 -> 0x4c6330` converts the VM cell to a narrow string. The chosen
seam is **after** conversion and the argument push, so retirement need not
dereference the string or call VM code. The original thunk `0x5319c7` preserves
the input stack and tail-jumps to `0x53188a`; ordinary return is EAX int, plain
RET. The CRT calls **SetCurrentDirectoryA** at `0x5318c7`, not the wide API.

Failure before/at that OS call returns -1. After OS success, the CRT obtains
the current directory (`0x5318f2`, possible larger-buffer allocation), then may
update the drive environment using SetEnvironmentVariableA at `0x5319a8`.
GetCurrentDirectory failure, allocation failure or drive-environment failure
can still produce **-1 after the process CWD already changed**. There is no
automatic restoration of the old directory. `SE_ChangeDir` compares only with
-1, publishes a VM Boolean and reaches the normal dispatcher tail `0x4cc11b`.
Its transport return remains 1, with `ret 0x14` from the inner dispatcher.

Consequently retire **before every attempt**, even null/invalid/same-directory
attempts, and never re-arm from the final Boolean or apparent restoration.
Preserve the native CRT's EAX, LastError and errno-related behavior rather than
manufacturing a success/error convention. A fixed retirement callback saves
and restores incoming LastError **before** forwarding; it must not restore that
old value after the real CRT call and erase the native result state.

The smallest forwarding shape is a five-byte `engine_patch::claim_call`
replacement: preserve full computational state, call the fixed gate owner to
retire, restore state/LastError/exact entry ESP, then **tail-JMP the original
`0x5319c7` target**. That preserves the original `0x4caa3d` return address and
one argument at native CRT entry, adds no post-call work, and needs no long-lived
observer frame across CRT execution. Do not hold a module/observer lock while
forwarding. An equivalent implementation still needs instruction/ABI fixtures.

The native CRT is not callback-free: null input routes through invalid-parameter
dispatcher `0x52b348`, which can call a thread/global handler at `0x52b383`, or
take fatal handling. Its cookie check can also fail. Those original exception,
callback and nonreturn paths must remain native; pre-retirement stays sticky.
The helper itself must be a bounded `noexcept` fixed-owner operation with no
engine/VM/COM call, file work, allocation or message pumping.

## 3. Full flags decode and cheap unrelated-bit path

`0x4b3b6b` obtains the fifth native argument cell; `0x4b3b6f` reads its unaligned
DWORD at cell+1 into ECX. `0x4b3b72` loads **resource/config owner G** from
`0x5a83b0`; it is not the engine pointer at `0x5aa100`. The store writes ECX
unchanged to G+0xac. This is arbitrary full-DWORD replacement, not a Boolean
setter and not a read-modify-write of bit 0.

The archive-enable change is exactly **`((old_flags ^ new_flags) & 1) != 0`**.
An aligned native DWORD read of the impending store's G+0xac and integer
XOR/TEST can cheaply skip retirement when only other bits change. Preserve the
comparison scratch register and original flags/state before replaying the MOV;
the no-helper branch need not touch FP/LastError. On a bit-0 change, call only
the fixed scalar retirement helper under a full envelope, then replay the MOV
once. The already-pushed new value must remain intact for the VM result.

The old-value read uses the same healthy native G-storage precondition as the
immediately following native write; readable-pointer checks do not create that
lifetime. A null/freed G is not a supported successful command path. A guard
failure must not admit/publish a table. Unobserved foreign writers remain outside
the supported generation contract. Restoring bit 0 later cannot undo model/type
caches loaded under a transient selection, so retirement remains sticky.

The known entry routes are explicit: `0x4b2e98` registers P handler `0x4b3b00`
(HIGHLOW operand `0x4b2e9b`), switch entry `0x4b3f4c` selects `0x4b3b6b`.
SE wrapper `0x4c8480` is published at HIGHLOW operand `0x4b8d3a`; it calls
`0x4c84a0`, whose index-117 table entry at `0x4cc30c` selects `0x4caa2f`.
The shared VM transport's indirect call is `0x4c0783`. `_chdir`'s reviewed
direct callers remain `0x4caa38` and pre-mount argv `-w` at `0x50a579`.
No wider imported-DLL or arbitrary computed-entry closure is asserted.

## 4. Existing factory completion, envelope and fixed abort

There is **no gate-owner factory notification in current production source**.
`x3m_lifetime_fl_leave` saves LastError, calls `finish(scope,true,result)`, then
restores LastError. `finish` holds `model_lock`, updates `fl_model` and epochs,
and releases its RAII lock on return. The existing internal type is `fl::Scope`.

`Coordinator::finish_factory` consumes `scope.entered` and decrements depth.
Successful completion requires a formerly entered Factory scope, normal return,
enabled/nonpoisoned construction, depth reduced to zero, no mutation/load depth,
coverage, a live nonzero provisional engine and **result==that engine**. It then
sets `construction_complete=true`. The factory thread was recorded at entry
and checked at provisional binding; a new notification should explicitly match
the current thread too. Missing ownership/observation, exhausted counters,
unbound/foreign scopes, mismatch or nested/poisoned construction must not notify.

Capture a **one-shot successful transition** and the engine/incarnation/thread
values under the existing lock; notify one fixed gate owner after releasing it.
Do not poll `construction_complete` on unrelated leave paths, which could
duplicate notification. Do not create a generic callback bus or invoke hash/file
work while holding the registry observer lock.

The engine result has returned from `0x48a5a0`, but native global publication
**has not happened**: `0x477697` next writes EAX to `0x5aa100`, then TESTs it.
The callback must use the captured matching return token, not chase that still
unpublished engine global. Mounted resources come from the separate G owner.

`FL_DISPATCH` captures the factory's **output** GPR/flags/x87/XMM/MXCSR before
calling leave. Bookkeeping has DF=0, reset x87 and default MXCSR `0x1f80`;
the original native output is restored after leave. Its registered SEH record
remains linked throughout leave. Thus a synchronous fixed verifier after lock
release and before leave returns fits the existing computational/SEH envelope.
Four-byte incoming stack alignment, FNSAVE/FRSTOR and exact LastError preservation
remain required. This is source/envelope evidence, not an assembled adapter test.

There is a concrete **consumed-scope exception gap** for the new cold work:
`finish_factory` has already set `fl_scope.entered=false`. Existing
`factory_exception` requires entered=true; `finish(scope,false)` also returns
immediately for the consumed scope. A fault during newly added cold work will
therefore not automatically revoke a new gate table through that registry bit.

Use a gate-owner **active-cold-only abort** notification from the existing unwind
handler, independent of consumed `fl_scope.entered`. It must retire on exception
search as well as unwind, because search may resume execution. A fixed scalar
abort is idempotent, requires no native reads/lock/allocation, and must not throw.
Stage an unpublished candidate; publish only after the complete verifier returns
normally, the active cold token is unpoisoned and no resource retirement raced.
Check sticky retirement/generation before and after publication as required by
the owner's concurrency protocol. Leave a resumed/failing transaction retired.
An observed mutation during startup, before the cold callback, must likewise
block that publication; normal factory completion must not clear its sticky
retirement. Install the two observers in the ratified early startup window,
before custom VM commands can run, independently of UI/TAA feature demand.

That active-cold abort covers the notification transaction only. Once it has
completed normally and its active token is cleared, a later fault in native
caller work is not retroactively the cold-verifier failure. Later engine/lifetime
retirement and the two resolver mutation hooks remain the owning generation
boundaries; this note does not invent a whole-caller exception guarantee.

## 5. Concrete cold borrowing premise and implementation acceptance

The prior lifetime RE supplies a bounded **known-native writer exclusion** at
this normal startup point. Numbered/selected-mod array publications and catalogue
loaders are on the already completed startup paths; the last two name-suppression
calls have returned. Remaining known frees are shutdown/fatal/mount-failure
paths, not a healthy concurrent reload. The fixed callback is on the normal
exclusive factory owner before engine-global publication. With no engine/VM/COM
call or message pump in that callback, it does not reenter any known resource
writer. This is the concrete basis for asserting `ColdScope.exclusive_borrow`
in that supported flow; it is **not registry membership or model_lock authority**.

Enforce one active owner-thread cold token, the successful matching factory
transition, fixed trusted guarded-memory callbacks, preallocated scratch off the
game stack, and a current nonretired resource generation. Copy needed metadata
in the two finite guarded captures; do filesystem/hash work on owned values with
no observer lock held. Recheck G/descriptors/winners/CWD and retirement state
before publication. `ReadFile`/enumeration/crypto calls in the verifier source are
synchronous; there is no source engine/VM/COM callback, alertable wait or message
pump. This does not audit arbitrary third-party provider/API hooks or foreign
threads writing/freeing native resources. Those unobserved writers, arbitrary
computed array writes and externally edited live resources remain explicit
out-of-contract cases, not a missing unnamed engine lock to infer from pointers.

The metadata owner must not retain native addresses after the transaction.
Descriptor rereads detect inconsistency; they do not synchronize an unqualified
concurrent free. Native integration still needs to demonstrate these concrete
conditions with the real factory callback and its guarded memory transport.

The bounded implementation checks are: exact two-site CPU/LastError and stack
replay; unchanged-bit bypass and both bit-0 directions; attempted CWD success,
failure and successful-OS/failed-postprocessing; sticky repeated/nested retirement;
one-shot factory notification and rejection of mismatch/nesting/poison/foreign
thread; search/resume and unwind during active cold verification; no publication
on any such failure; and install/rollback/protection ownership. Reuse existing
FP/SEH envelope fixtures; the new observer's retirement work is fixed scalar
state, with no per-frame/resource scan or per-mutation I/O. Dynamic timing and
native joined-hook acceptance are still unmeasured. No general event bus, new
SEH framework, runtime KC qualification or production implementation is proposed.

Independent consequential review accepts this RE-only contract. The reviewer
reproduced all receipt fields except elapsed time, checked the two hook spans,
factory transition and consumed-scope exception gap against source, and verified
the four relative links. This does not replace the joined native acceptance
listed above or activate either injection site.
