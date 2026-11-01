# FL submit timing: native spans and shared-handler binding

Offline study, 2026-10-30. FL input `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`;
AP reference `/mnt/c/X3AP-orig/X3AP.exe`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Addresses are preferred VAs, base `0x400000`; bind an admitted loaded base plus
RVA. This is an implementation-ready static map, with one explicit existing
feature conflict. It neither enables FL submit diagnostics nor qualifies game
execution, runtime cost, native Windows or CrossOver behavior. No production
edit, build, Wine/native execution, game launch, install or commit was made.

The owning AP contract is [view-submit-hot-path](../view-submit-hot-path.md) and
the [submit timing ledger](../../verification/sampling-profiler.md).
Reuse the existing 22 recorder IDs, nine interval clocks, owner/frame gate,
300-frame reduction and shared transactional installer. The recorder is a
bounded diagnostic, not a universal native callback or lifetime observer.

## 1. Exact spans in existing recorder order

The table specifies `ret_pop=0` throughout. A continuation is VA + length.
Instruction operand witnesses, span hashes, exact continuations and replay
cases are in the [derived map](../../../verification/results/fl-submit-phases/mapping.json).
The two inverse imports have HIGHLOW fields at span **+2**; other spans have
none. Four spans carry a direct `CALL rel32` field at **+3**: the three sort
returns and walk miss. No span contains x87 instructions.

| ID / role | FL VA / RVA | Length / instruction sizes | Replay meaning |
| --- | --- | --- | --- |
| 0 sort enter | `0x4975c0` / `0x975c0` | 7 / 7 | TEST view EDX `+0x270`, mask `0x80`. |
| 1 sort return A | `0x48ba8b` / `0x8ba8b` | 7 / 2,5 | EDX=ESI; CALL queue drain `0x497680`. |
| 2 sort return B | `0x48bcb5` / `0x8bcb5` | 7 / 2,5 | Same; overlaps sun lens CALL, §4. |
| 3 sort return C | `0x4978a9` / `0x978a9` | 7 / 2,5 | Same, environment-map driver. |
| 4 walk begin | `0x497234` / `0x97234` | 10 / 6,4 | ECX=`[EDI+0x2a0]`; save ECX at ESP+0x10. |
| 5 walk miss | `0x497259` / `0x97259` | 7 / 2,5 | PUSH 0x70; CALL allocator `0x531395`. |
| 6 walk join | `0x497311` / `0x97311` | 6 / 6 | Overwrite ECX from node ESI `+0xf8`. |
| 7 technique begin | `0x4e1af8` / `0xe1af8` | 10 / 2,1,1,6 | Load effect vtable, push handle/EDI, load `+0xe8`. |
| 8 technique end | `0x4e1b04` / `0xe1b04` | 7 / 7 | CMP material ESI `+0x1a4`, 0. |
| 9 End begin | `0x4e606f` / `0xe606f` | 9 / 2,1,6 | Load effect vtable, push EDI, load `+0x10c`. |
| 10 End end | `0x4e607a` / `0xe607a` | 9 / 3,6 | Reload ESI/ECX from EBP−0x78/−0xac. |
| 11 block begin | `0x4e3d99` / `0xe3d99` | 10 / 2,1,1,6 | PUSH 1/count-address/effect; load Begin `+0xfc`. |
| 12 block end | `0x4e6005` / `0xe6005` | 6 / 6 | CMP pass count EBP−0xbc, ECX. |
| 13 inverse world begin | `0x4e4422` / `0xe4422` | 6 / 6 | CALL through inverse IAT `0x550338`. |
| 14 inverse world end | `0x4e4428` / `0xe4428` | 6 / 6 | LEA EAX, EBP−0x4b8. |
| 15 inverse view begin | `0x4e44d1` / `0xe44d1` | 6 / 6 | CALL through the same inverse IAT. |
| 16 inverse view end | `0x4e44d7` / `0xe44d7` | 8 / 2,6 | Load effect vtable; LEA ECX, EBP−0x538. |
| 17 material enter | `0x4e0f30` / `0xe0f30` | 6 / 1,2,3 | PUSH EBX; EBX=ESP; SUB ESP,8. |
| 18 material return | `0x4e714a` / `0xe714a` | 8 / 3,5 | Restore SEH predecessor ECX; EAX=1. |
| 19 world enter | `0x4de570` / `0xde570` | 6 / 1,2,3 | PUSH EBP; EBP=ESP; align ESP to 8. |
| 20 world return A | `0x496fbe` / `0x96fbe` | 6 / 6 | Load EDX from node ESI `+0x1ac`. |
| 21 world return B | `0x4976a5` / `0x976a5` | 6 / 3,3 | Load EDI from queue record ESI+0x10; ADD ESP,8. |

Sort has exactly three direct callers, `0x48ba86`, `0x48bcb0`, `0x4978a4`;
the first two are the ordinary view and lens branches of frame `0x48b6d0`,
the third is environment rendering in `0x4977c0`. Its returns at `0x49762f`
and `0x49767f` have only three POPs plus RET, explaining caller-return closes.
World has exactly two direct callers `0x496fb9`, `0x4976a0`, the immediate
traversal and deferred queue paths. Material has one direct caller
`0x4e7142`, inside wrapper `0x4e6ef0`; its close is after the native
`ADD ESP,0x10` at `0x4e7147`. The skip edge `0x4e6f67 → 0x4e714a` makes that
close idle, so a span starting on the cleanup instruction would be invalid.
All six calls are retained with the native argument transport; changing
addresses alone would not reproduce FL's register ABI.

Walk enters through `0x49721f`, misses through `0x497242`, and hits through
`0x497247 → 0x497311`. EDI is the view; ESI is the node. ECX traverses the
records, comparing `[ECX+0xc]` against ESI. A hit leaves **ECX = hit record**
immediately before the join span overwrites it. This differs from AP's saved
ESI hit. A miss closes before either allocator/recovery call; the later join
is idle. The loop body is call-free and completes before recursive traversal
calls `0x497583/0x497594`, so normal traversal recursion cannot reopen walk.

The technique branch `0x4e1ad4` enters site 7. Site 11 follows the residual
setup span exactly: residual `[0x4e3d91,0x4e3d99)`, then submit begins at
`0x4e3d99`; effect Begin calls at `0x4e3da3`. Geometry-zero
`0x4e5ffe → 0x4e606f` bypasses site 12, so existing End-begin handling closes
block and increments `block_skipped`. Zero pass count
`0x4e600b → 0x4e606f` has already crossed site 12. Effect End calls at
`0x4e6078`. FL has **ten**, not AP's eleven, direct early-out edges to site
10; those are idle closes. No counter change is needed for their count.

The inverse IAT is named `d3dx9_43.dll!D3DXMatrixInverse` by the actual import
table. Source pointers are scratch `0x5aae60` and view `0x5aae68`; the first
retains AP's `inverse_world` recorder label but is **not** the separate world
matrix pointer `0x5aae64`. See [camera buffers](camera-buffers.md).
Outputs are EBP−0x4b8 and EBP−0x538, determinant pointers null. Preserve that
meaning rather than replacing either input with the similarly named world slot.

## 2. Stack, CPU state, lifetime, reentrancy and unwind

Use the existing material entry/SEH ABI from
[material submit](material-submit.md) and
[effect-pass mapping](pass-phases.md#3-native-esp-cpu-liveness-callbacks-and-exceptions).
Its ordinary frame base is **E = EBP−0x5c0**. Sites 7–12, 14 and 16 have ESP=E
on the normal native path. Technique's copied pushes leave E−8 before the
`SetTechnique` stdcall; End's copied push leaves E−4; block's three copied
pushes leave E−12 for Begin. Each normal COM return restores E. Inverse begin
sites 13/15 enter with **E−12**, three arguments already pushed; documented
D3DX stdcall cleanup returns to E at sites 14/16. A trampoline must preserve
those argument words and replay the indirect call only after restoring native
ESP. Failure/null return values do not bypass a closing stamp on normal return.

Material entry observes the original caller return/argument stack before its
alignment prologue. The close observes wrapper **W = EBP−0x40**, after the
caller's 16-byte cleanup; its direct skip reaches the same W. Both wrapper and
material retain their native SEH frames and native unlink/restore sequences.
World is register-aware: ECX=selected geometry/LOD, EDX=node, two caller-cleaned stack
words, plain RET. Both world returns observe the caller with **eight argument
bytes still pending**; site 21 itself replays their ADD ESP,8, whereas site 20
continues to that cleanup at `0x496fc4`. The whole world body
`[0x4de570,0x4deb8f)` is call-free with three plain returns; no asynchronous
lease or engine-pointer retention is introduced by its interval.

Sort is register-only (EDX=view), plain RET, three saved nonvolatile registers,
and has no calls. Its three closes enter at the caller's original stack depth;
the displaced drain CALL has no stack arguments. Native walk has ESP equal
to `align_down(EBP,16)−0xf0`, after 0xe8 local bytes and saved ESI/EDI. The
three walk stamps share that depth and add no pending argument until miss's
copied PUSH executes. The function's normal exit pops EDI/ESI and returns
`0xc`. These are native stack facts, not a claim that all injected helpers
have been executed under all stack residues.

At walk join ECX is live to the diagnostic; EDI/ESI must retain view/node
identity. At technique/block/End, EDI is live effect and ECX may be a handle
or count address. Inverse spans have live native arguments and SIMD locals;
none is callback scratch. Incoming flags at the load-only walk/join/return
sites have no next consumer before native arithmetic, but restoration remains
mandatory. Sort TEST, technique-end CMP, block-end CMP and world-entry AND
must establish exactly their native outgoing flags; site 21's ADD must do so
at the same stack depth. In particular site 8's flags survive the following
MOV until its branch. Preserve EFLAGS including DF, all registers, XMM0–7,
MXCSR and LastError through the existing context stub/helper contract. The
no-x87 handler callgraph audit remains required; this offline map only proves
that none of its 22 displaced spans needs x87 replay.

The two counters borrow the same healthy native lists the engine is about to
read: sort's root **`[0x5aa100]`**, head `+0x40`, sentinel `root+0x44`, links at
record+0; walk's view `+0x2a0`, links at record+0, terminal link null. The
sort's native loads at `0x4975c8/0x4975d0/0x4975e0/0x497634` establish those
fields. The existing 65,536 cap and one-in-16 walk sampling remain appropriate.
Bind the root global once, and use **saved ECX** for a hit, saved EDI for view.
Do not read AP's global or AP's ESI hit on FL. No engine pointer survives the
handler; queue counting is before sort's clock, walk recount after its close.
The extra pointer chase keeps the existing healthy-native diagnostic contract;
this does not establish malformed-pointer catch-and-continue behavior.

The documented [native render/callback admission](ui-render-exclusion.md)
and material study already delimit supported owner-thread rendering. The
normal sort/world/walk scopes above add no calls that could recursively open
their own interval. Material, effect, D3DX, allocator and private virtual calls
remain native callback/exception frontiers. The recorder's owner gate rejects
foreign threads; it cannot reject same-thread nested rendering. Its nine scalar
open clocks do not support arbitrary nesting. Reuse `reopened`, `idle` and
frame-discard behavior; do not expand this timing work into a universal callback
exclusion proof. A future demonstrated supported reentry through these exact
frontiers would be a concrete contract change, not justification to assume it
already happens.

An exception that unwinds past a close leaves its clock incomplete; preserve
the existing discard/reopened semantics and never manufacture a successful
close. The diagnostic owns no engine pointer, borrow cleanup or scoped native
mutation. An exception originating inside the injected helper/stub is not
qualified here; keep it nonthrowing and limited to existing owner/clock/list
counting/accumulation. The separate material observer's unwind cleanup remains
its own contract. No new native exception wrapper is proposed.

## 3. Reproducible offline evidence and limits

```sh
python3 verification/results/fl-submit-phases/produce.py
python3 -m py_compile verification/results/fl-submit-phases/produce.py
```

The [producer](../../../verification/results/fl-submit-phases/produce.py) reuses
the project PE32/Capstone tools and AP submit verifier. Measured: **8 complete
regions, 8,591 instructions, 56 additional semantic pins, 22 site operation
contracts, 132 replay cases** (22 × two loaded bases × three arena addresses),
**13 rejected semantic/edge mutations**, **100 existing/reserved claims**.
AP's unchanged source/native contract passes **15 checks**; its current
conflicting sun claim and unrelated historical fixed-claim source anchors are
excluded explicitly from that reference invocation. FL's one expected overlap
is reported, not accepted as simultaneous installation. Raw listings stayed
under `/tmp`; tracked output contains derived addresses, operands, counts and
hashes, never copied native instruction bytes.

Every region decodes gap-free from its known function entry to final return.
All spans and continuations are instruction boundaries and pairwise disjoint.
The `.text` scan examines every byte offset for direct rel8/rel32 call/jump
forms, then classifies candidates using complete region boundaries. **Five**
additional raw candidates are inside decoded operands and are disproved as
instruction edges; none has an unresolved source boundary. No actual edge
enters a span interior. The census includes direct calls to the three timed
entries and the start edges described in §1. A whole-file dword search finds
**zero** literal references to any span byte. All executable code is in the
surveyed `.text` section; arbitrary computed/private-address entry and third-party
patches remain outside this static proof.

The material body's one indexed jump `0x4e4f91` and five-entry table
`0x4e60cc` reuse the [pass study](pass-phases.md#2-instruction-incoming-edge-and-relocation-proof).
All targets are decoded boundaries outside the submit spans, and all table
fields/operand have HIGHLOW relocations. No other containing routine has an
indirect jump. The inverse IAT operands are the only submit span HIGHLOW
fields; the import slot itself is not dereferenced or frozen to a DLL address
by this map. Replays apply image relocation first, rebase the four direct CALL
fields modulo 32 bits, preserve all other loaded span bytes, then append the
shared continuation JMP. The indirect inverse CALL retains the **loaded IAT
slot address**, not its preferred VA and not a baked imported function pointer.
These are decoded replay tests, not CPU execution.

Negatives bypass exact image digests and exercise sort's view register/root,
cache head/hit register, technique slot, pass count, inverse IAT/source,
material cleanup, world node register, internal/external interior edges, and a
switch entry redirected into an operand. The external negative redirects the
independently decoded wrapper CALL at `0x4e7142`; it must fail the whole-text
incoming-edge check. The producer validates JSON counts before writing it.

## 4. Small cold binding, sun precedence and implementation acceptance

Prepare an immutable FL pack only after recognized FL identity/configuration,
in startup's exclusive Preparing window **before any material, sun, light,
LOD or diagnostic writes**. Existing complete material-body qualification can
serve both pass and submit diagnostics; it must be demandable when submit is
requested with pass disabled. Reuse its checked body/switch contract rather
than adding another persistent copy or late whole-body hash. The new sort,
walk, world and caller contexts are bounded by §3's region/operand evidence;
validate the needed immutable code once at this cold boundary, normalize each
known HIGHLOW by subtracting the admitted load delta, and retain the resolved
22 specs plus global pointer and hit-register selection. Zeroed-HIGHLOW hashes
in the report are analysis fingerprints, not a validator that may ignore
changed relocation values. No runtime graph/pointer validator is proposed.

At ordinary diagnostic initialization, consume that cold result and let the
unchanged `stamp::install_group` preflight all 22 live spans. Do not rehash the
original material body after light/LOD writes, or the original frame/queue
body after sun/camera writes. Keep AP's `object_trace::executable_verified()`
gate and AP table unchanged; choose the FL pack only from positively admitted
FL identity. A profile refusal disables this optional diagnostic and records
its reason without revoking unrelated supported features. The common core
roles, counters, reduction and handler remain shared; the only counter inputs
needing edition binding are the queue-global address and saved hit register.
The four direct call fields are `rel32_offset=3`; inverse `rel32_offset=0`
copies the already-relocated IAT operand. No hot validation, allocation, new
lock, logging, ownership registry or per-draw profile lookup is needed.

**Sun conflict and exact precedence.** FL sun owns
`[0x48bcb7,0x48bcbc)`, inside submit sort-return B
`[0x48bcb5,0x48bcbc)`. Moving the close past that CALL would include queue
drain time in sort, so it would change the diagnostic. AP
`sun_occlusion::initialize` checks explicit `X3M_SUBMIT_PHASES=1` after
establishing sun/bracket demand, refuses sun with `submit_phases_conflict`,
and writes neither sun site even if submit later fails its own gate. Preserve
that existing requested-flag precedence. Merely qualifying or retaining a
submit profile must never disable default sun. When the explicit submit flag
is absent, sun behavior remains its existing behavior.

FL currently returns `fl_startup::sun_occlusion_prepared()` before AP's
conflict test. Its own `request_startup`/`prepare_startup` do not check submit,
and startup installs sun before the common submit initializer. A future submit
binding therefore needs the **same explicit conflict refusal in FL sun's cold
request/prepare path before its first write**, reporting
`submit_phases_conflict`; adding a generic arbitration mechanism or silently
letting late submit preflight fail against sun would not match AP precedence.
This is an identified unsupported integration difference, not fixed by this
study. With both flags set, refuse sun and then independently qualify/install
submit. With no explicit submit demand, preserve sun even if the pack exists.

All other 99 inventoried current/reserved claims are disjoint, including
material CALL `[0x4e7142,0x4e7147)`, point-light `[0x4e4915,0x4e491b)`,
residual setup ending at submit block begin, the four pass stamps and the LOD
material byte. Eight first-five-byte windows cross qword boundaries: sort
return B, walk begin, technique end, End begin, block end, inverse-view end,
and both world returns. The shared patcher uses its existing quiescent
non-atomic write path there; this makes exclusive startup and late-install
refusal necessary, not a request for hot install support.

The smallest implementation acceptance is: retain AP behavior; exercise FL
pack rebasing (including both IAT operands), explicit sun precedence, absent
submit/default sun, disabled/foreign-thread gates, the ECX cache-hit count,
all normal/miss/idle/block-skip chains, reverse rollback at every site and
failed restore/inert state. Reuse the existing CPU envelope fixture with FL
spans and all four incoming stack residues, pending inverse/world arguments,
flags/MXCSR/LastError, normal and incomplete unwind chains, and the handler's
no-x87 callgraph check. Keep replayed native argument transport and loaded
operands exact. A bounded synthetic timing check can verify the existing
102 ns calibration convention; it cannot claim FL game cost or FPS. Preserve
fixture-only/default-off `X3M_SUBMIT_PHASES` exposure; the current draw-trace
group does not request submit because of its sun conflict. Queue/native/Wine
qualification belongs to the main owner. No unresolved native mapping blocker
remains for that bounded implementation; same-thread arbitrary callback
exclusion remains outside its established diagnostic contract.
