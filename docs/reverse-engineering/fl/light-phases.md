# FL per-node light-selection timing boundaries

Offline instruction/ABI mapping, 2026-10-30. **The existing two-stamp light
recorder can be shared with an FL site pack and cold-bound caller addresses.**
The selector was already identified in
[sun/light ownership](sun-light.md#1-identification-fields-and-ranking);
this note adds its timing seam, native-frame tokens, callback closure and port
qualification. It does not establish live delivery, a lighting enhancement or
an FL bottleneck. No production source, executable build/execution, Wine/game
launch, installation or commit was performed.

Preferred addresses below use image base `0x400000`; resolve RVAs through the
recognized FL 1.3 loaded image. Preserve the AP executable gate and shared
recorder. Do not widen `object_trace::executable_verified()` to admit FL.

## Evidence and limits

```sh
python3 verification/results/fl-light-phases/produce.py
```

The [producer](../../../verification/results/fl-light-phases/produce.py) uses
project `compare_fl_engine`/`inspect_x3.PE32`, Capstone and the existing AP
`verify_light_phase_sites.py`. The compact
[mapping](../../../verification/results/fl-light-phases/mapping.json) retains
hashes, derived operands, counts, replay targets, failures and source provenance.
Raw local listings are `/tmp/fl-light-{phases,helper,crt}.local.txt`; no native
code listing or extracted binary is retained in this change.

Measured static result: **12 regions, 1,910 instructions, 66 operand anchors,
18 decoded tail-replay cases, eight authored stack-model cases and 14 semantic
mutation refusals**. Mutations bypass the executable digest and test the named
instruction/edge contract, including node/receiver/argument transport, stack
alignment/depth, exact exit registers/cleanup, comparator/stride, common exit,
external interior branch and CFG pointer. The finite named current-claim survey
covers **86 existing spans**, disjoint from both candidate spans. It reuses the
loop mapper's game/frame/pass/UI/music/material/point-light/scene inventory and
adds the ten loop stamps; it is not a generic enumeration of every private
hook. Installation must still preflight live bytes and reject actual claim
conflicts. The AP verifier passes **15 checks / 1,250 instructions** on the
clean AP image with its source table; its claim list was deliberately empty in
this bounded comparison, so that count is not a new AP overlap qualification.

Clean AP `/mnt/c/X3AP-orig/X3AP.exe`: SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Clean FL `/mnt/c/X3FL-orig/X3FL.exe`: **2,804,736 bytes**, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
These hashes identify offline evidence, not a proposed runtime whole-image gate.
Synthetic tails are decoded and the stack operation sequence is modeled;
neither is executed CPU-state preservation or runtime acceptance.

## 1. Selector, native ABI and caller categorization

| Contract | AP | FL |
| --- | --- | --- |
| Whole selector | `0x47d5e0..0x47d9b4` | `0x4964c0..0x4968bc` |
| Entry | `0x47d5e0`, six bytes | `0x4964c0`, same six bytes |
| Common exit stamp | `0x47d9ab`, five bytes | **`0x4968b4`, five different bytes** |
| Node | `[EBP+8]` | Same, loaded at `0x4964da` |
| Callee cleanup | `ret 4` | `0x4968b9: ret 4` |
| Cockpit CALL / native return | `0x42173b / 0x421740` | `0x423012 / 0x423017` |
| Traversal CALL / native return | `0x47dff1 / 0x47dff6` | `0x496fa7 / 0x496fac` |

At original native entry ESP=`E`, `[E]` is the native return and `[E+4]` is the
node. The selector pushes EBP, assigns EBP=`E-4`, aligns ESP down to 16,
subtracts `0x38`, then saves ESI/EDI. Write `A=align_down(E-4,16)`; the steady
selector ESP and exit-site ESP are **`A-0x40`**. FL does not push/pop EBX here.
Normal return restores caller EBP and reaches ESP=`E+8` after `ret 4`.

Incoming EAX/ECX/EDX are not selector arguments. It loads engine into EDX at
`0x4964c9`, then reset `0x496450` initializes EDX/ECX and conditionally EAX
before those values are used. The node is loaded independently from `[EBP+8]`.
The pushed ECX at `0x4964ea` is consumed by extent helper `0x4a39b0`'s `ret 4`;
that helper takes its node in ECX and does not read the pushed word. Do not
assign semantic significance to that scratch argument.

Cockpit helper `0x422fb0..0x423158` pushes
`[[cockpit+0x10]+0x70]` at `0x42300f`. On return it reloads EAX from the engine
global, clears EBP and tests selected count. This preserves the AP bucket's
meaning: cockpit light-display work, not a guarantee of cockpit-view drawing.
Traversal `0x4968c0..0x497720` pushes ESI=node at `0x496f9c`; independent geometry
loads intervene before CALL. Return immediately calls publication `0x4de530`,
which initializes ESI/EAX and, before use on the work path, EDX/ECX. Neither
known caller consumes the selector's volatile-register or arithmetic-flags
result. Still preserve every native output register/flag for unknown callers.
Traversal recursion at `0x497583/0x497594` occurs after this selector returns.
Return bucketing alone does not distinguish view, layer or camera origin.

An every-offset `.text` direct-encoding survey finds exactly these two entry
CALLs, no entry tail jump, and four direct branches to the common exit:
`0x4964e4`, `0x4964f5`, `0x496879`, `0x49689e`. Fall-through reaches it after
qsort cleanup. No actual decoded or unresolved raw edge enters either displaced
span interior. One raw short-branch-looking encoding at `0x496929` targets
`0x4968b6`; it starts in the displacement of the four-byte instruction
`0x496926: mov [esp+0x70],eax`, so is rejected as a noninstruction boundary.
Neither span has a HIGHLOW operand or any absolute dword encoding of a span
address anywhere in the file. This establishes conservative static direct-entry
coverage, not exclusion of computed private/third-party entries.

## 2. Exact spans, live state and native return ownership

| Role | Exact bytes / whole-instruction replay | Continuation and metadata |
| --- | --- | --- |
| Enter | `55 8b ec 83 e4 f0`: PUSH EBP; MOV EBP,ESP; AND ESP,-16 | `0x4964c6`; length 6, `ret_pop=0`, `rel32_offset=0` |
| Exit | **`5f 5e 8b e5 5d`**: POP EDI; POP ESI; MOV ESP,EBP; POP EBP | **`0x4968b9` native RET 4**; length 5, `ret_pop=0`, `rel32_offset=0` |

The exit span includes POP EBP to reach the required five whole bytes. AP's
`POP EDI; POP ESI; POP EBX; MOV ESP,EBP` is not reusable here. The tail must jump
to native RET 4; it must not append a return or include a synthetic `ret 4`
pop contract. The entry five-byte write/restoration fits within one aligned qword. The exit
starts at qword offset 4, so its five-byte write/restoration **crosses that
qword** and uses the plain-copy path. Both require the existing qualified
startup quiescence and rollback rules; entry atomicity does not remove that
requirement.

Reuse `lean_stub::emit_context`: preserve all GPRs/EFLAGS including DF and
XMM0–7, clear DF for the C++ callback, restore exact native ESP/context before
replay. At entry, flags are dead after the displaced AND, but incoming EBP,
nonvolatiles and native return/argument words must survive. At exit, native
EDI/ESI saves remain at `[A-0x40]` and `[A-0x3c]`; EBP=`E-4` is live, caller
EBP remains `[E-4]`, and the span changes no flags. Native SIMD and flags at
exit describe output, not necessarily entry state. Preserve them at that site.

The existing handler's entry token **`saved[SavedEsp]=E-4`** (PUSHAD records
ESP after PUSHFD) equals exit **`saved[SavedEbp]=E-4`**. Its sole live native
read, **`*(token+4)` at entry**, obtains `[E]`, the original caller return.
Bind the two bucket addresses once at cold initialization, either in immutable
recorder-owned fields or by mapping the two recognized FL returns to the
existing AP canonical bucket tokens. Do not do edition selection, image
recognition or another stack/native-global read per stamp. Native `[E]` stays
unchanged throughout: stamps JMP through the chain, and no return-address
replacement/wrapper is needed. The selector has no `[EBP+4]` return-address
inspection; preserve native stack placement anyway. The hook retains only
numeric token/return values and never retains or dereferences the node/light.

Keep mode observation before any FP-control write, `ErrorGuard`, owner gate,
`LightCallBoundary`, four-byte incoming-stack realignment and the full no-x87
call-graph audit. FL selector itself uses SSE/integer arithmetic, but this is
not permission to weaken the envelope. Do not execute a formatter, allocate,
return a floating value or add x87 work in the handler. Preserve the existing
rounding-mode refusal and site-local LastError/MXCSR behavior; native output
must not be overwritten by a saved entry FP image.

## 3. Calls, lifetimes, reentry and nonlocal exits

Selector has exactly three direct calls: reset `0x496450`, extent `0x4a39b0`,
and CRT qsort `0x52b510`. Reset/comparator have no calls; extent's only call is
its own recursion at `0x4a39e7`. The native selector does not recurse or issue
an engine virtual callback. The qsort arguments are engine table `+0x628c`,
positive populated count, stride 12 and fixed integer comparator `0x496490`.
Zero populated count skips qsort. Its shortsort `0x52b430` and swap `0x52b4d0`
retain the same normal closure. The comparator is call-free, using only two
records' score words; no engine callback appears in that normal path.

FL CRT additionally calls through **CFG check slot `0x550410`** immediately
before its indirect comparator dispatches, including `0x52b5cc/0x52b5d2` and
shortsort `0x52b465/0x52b46b`. File slot points at call-free RET `0x417bf0`.
The loader can replace that security check; this note does not infer arbitrary
loaded-system callback absence from its file default. Security-cookie check
`0x51eaa7` normally returns and branches to failure `0x51f1ce` on mismatch.
Qsort's invalid-argument branch `0x52b54d` reaches errno and invalid-parameter
`0x52b3c3`; the native nonnull table, nonzero stride and fixed comparator exclude
that branch in the supported normal input. Corruption, external hooks,
asynchronous exceptions and hostile security callbacks remain outside it.
**The loaded CFG check is not a blocker to installing these entry/exit stamps.**
All native qsort/guard/comparator calls and their return words remain in place;
no such CALL is displaced, wrapped or redirected by this adapter. If a loaded
check reenters the selector, existing nested-entry poisoning rejects that
frame's elapsed evidence; dispatch reentry is separately counted/poisoned. If
it leaves nonlocally, unmatched/discard handling closes the stale interval at
the next frame boundary. The remaining limit affects a universal no-callback
claim and complete timing coverage. It requires no new whole-CRT validation,
lifetime framework or extra runtime read for this value-only recorder.

Reuse frame/Present owner admission and all existing bounded failure behavior:
ignore/count early and foreign hits; cap depth at eight; poison nested,
overflowing, reentered or mismatched frames; preserve the outer clock; invalidate
open records at frame/discard boundaries; surface unknown entries separately.
Unknown calls may accumulate in their bucket, but the existing summary must
not call that window complete coverage. Normal traversal recursion does not
remove the need for these controls. `dispatching` guards recorder dispatch;
it is not a proof against every asynchronous exception before/after that
region. No new native object lease is needed because the recorder reads no
native object data.

Selector installs no SEH frame. Memory access, signed division, qsort/security
failure and external exception paths can still leave without common exit.
Do not catch, swallow or replace native exceptions; leave the original return
word intact. The shared frame poison/unmatched logic prevents publishing stale
elapsed time after skipped exits/stack reuse. Generated-arena unwind behavior,
arbitrary callback/thread reentry and loaded-system security dispatch remain
runtime limitations. Synthetic balanced-RET tests cannot close them.

## 4. Smallest cold adapter and remaining acceptance

A bounded proposal is to qualify **1,078 bytes / three windows** once at the
existing recognized-FL/configured exclusive startup seam, before native
preparation and diagnostic writes:

| Window | Bytes | Reviewed HIGHLOW offsets from window start |
| --- | --- | --- |
| Selector `0x4964c0..0x4968bc` | 1,020 | `0xb`, `0x67`, `0x6f`, `0x77`, `0x3e1` |
| Cockpit call/return `0x42300c..0x423024` | 24 | `0xc` |
| Traversal argument/call/return `0x496f8f..0x496fb1` | 34 | none |

Preferred FNV1a64 values and the six exact fixup offsets are in `mapping.json`.
Normalize by subtracting the admitted load delta from each HIGHLOW DWORD,
retaining its exact preferred value; never mask/zero operands. Do not copy a
full `.text` hash, unrelated material body, global pointer or FL system-DLL
hash gate into this optional adapter. The fixed helper/CRT closure is offline
admitted-build evidence, not a new runtime helper census. This proposal follows
the existing pass/loop cold-profile pattern, not an implemented new validator.
An authored profile model admits three bases (preferred, rebased and lower)
and refuses **3,234 changed bytes, 18 changed relocated operands and nine short
reads**; these counts describe that model, not production binder tests.

Implementation plan:

1. Add an FL two-site profile with the exact spans above, plus immutable caller
   values resolved from RVAs `0x23017` and `0x96fac`. Require recognized FL 1.3,
   nonoverflowing/aligned loaded-image extent, exact local normalized windows
   and unchanged output on qualification failure. Cache only the two specs and
   two numeric bucket values. AP keeps its current source table, classifier and
   `object_trace` gate. Cold FL refusal disables only this optional diagnostic.
2. Reuse `stamp::install_group`, `emit_context`, accumulator, 300-frame window,
   log schema and frame delivery. Initialization consumes the qualified pack
   and preflights both actual live spans before any claim. Keep telemetry/frame
   prerequisites and `X3M_LIGHT_PHASES`/`X3M_DRAW_TRACE` request gating. No global
   `object_trace` change, new hot native read, per-stamp edition dispatch,
   allocation, lock, counter family or query/subobject stamp is required.
3. Extend the existing host/profile checks and light CPU fixture with an
   FL-shaped body: two saves, `sub 0x38`, new exit including POP EBP, original
   return/stack argument and four incoming exit edges. Exercise both caller
   buckets/unknown, all four incoming stack residues, skipped gates/no-qsort,
   populated qsort-shaped path, native input/output differences and exact
   native RET 4. Assert GPR/EFLAGS/DF, XMM, live x87/MXCSR and LastError against
   identical native baselines; test early/foreign, nesting/depth/reentry,
   mismatched token, skipped exit/frame discard, clock/mode refusal, all
   preflight/emission/rollback/late-install failures and disabled zero claims.
4. Run affected host checks, x86 compilation and no-x87 audit first. The parent
   owns any frozen-fixture native execution and Wine queue. Its receipt must
   retain actual measured check counts/source/binary provenance; this analysis
   supplies no invented native check total. Actual FL delivery and overhead
   need a later user-launched diagnostic capture. Existing **107 ns** dispatch
   estimate is AP/X3-bottle provenance, not FL calibration or game FPS.

The per-stamp performance contract stays the existing active/owner checks,
mode observation, one QPC and bounded value-only accumulation plus the already
present entry return-word read. All edition/profile/classifier binding work is
cold. No new policy or production blocker was found for this limited adapter;
executed CPU preservation, live native/FL delivery, exception unwinding and
probe-cost calibration remain open acceptance work.

## Adapter implementation evidence

The shared adapter and bounded host/x86 checks are recorded in the
[feature ledger](../../verification/farnhams-legacy-light-phases.md).
The offline counts above remain original mapping evidence; runtime acceptance
and its limitations are recorded separately there.
