# FL KC completion and bounded UI refresh dispatch

Scope update, 2026-10-28: the [AP/FL save-load comparison](ui-save-load-comparison.md)
finds no need for additional layout repair or KC completion hooks at unchanged
configured scale/resolution. The user excludes in-game resolution changes.
Earlier repair/changed-generation requirements below are conditional engineering
proposals, not active port prerequisites. Native borrow/ABI findings still apply.


Offline, 2026-10-28. Preferred-image native VAs and file CODE offsets for the
hash-bound FL image/story in
[verify_fl_ui_refresh_completion.py](../../../verification/probe/verify_fl_ui_refresh_completion.py).
Extends [saved layout](ui-saved-layout.md) and
[layout generation](ui-layout-generation.md). No production
patch, build, Wine/native execution, launch, install or commit.

## 1. Positive completion, pending and failure

**A selected task's normal root return is observable before its storage is
freed. AL1 and task disappearance are both insufficient.** There is also a
correction to the prior interpretation: `4be7d0` is a diagnostic stack scan,
not cleanup. Its complete `4be7d0..4be842` body has no calls, unlink or free.
The dispatcher calls it when its task remains registered, then returns AL1.

`4bb5b0` prepares a task through `4bb310`; the latter resolves the requested
receiver and named method, including superclass traversal at `4bb3bc`. It
allocates a **0x40-byte task** through `4be850`, writes entry CODE offset at
task+0x1c and receiver at +0x3c, then supplies the task and method descriptor to
the caller. `4bb65b` invokes `4bb470` with ECX=task. At that call site ESI=task,
EDI=task ID (`[task+8]`), EBX=VM, EBP=resolved method descriptor. This is a
concrete place to bind a selected dispatch to the actual allocated task, rather
than guessing from VM's current-task pointer after dispatch.

`4bb470` pushes the zero root-return sentinel and null object context, links the
task into the active list, sets VM+0x1434=current task, and calls `4bed90` with
ECX=VM and one callee-cleaned task word. It discards the interpreter's result
and returns AL1. The outer dispatcher also returns AL1 when the registered task
is absent, or still present after its diagnostic scan.

| Interpreter path | Exact evidence and meaning |
| --- | --- |
| Ordinary KC return, opcode83 | Jump-table entry selects `4c0809`. `[EDI+0xb]` is saved return CODE offset. Zero at `4c0817` selects **`4c1572 → call4c1592 → 4be900`**. ESI is the runtime CODE pointer just after the selected return opcode. The path stores that offset to task+0x1c and stack index to +0x18 before the call. Nonzero saved offset restores caller context/CODE and continues the same interpreter; a child method return is not a task completion. |
| Missing/dead object while unwinding | Return handling walks retained frames whose object+8 is null. Reaching the root through this path selects **`4c1538 → call4c156b → 4be900`**, supplying zero. It uses the same completion/free routine but is not the normal selected-method return edge. |
| Pending native result | After native-call opcode82, task word+0x20 must equal1 (`4c0793`). Otherwise `4c0798 → 4c149c` sets task word+0x3a=2, moves it to VM+0x12e8/+0x12ec pending list, saves CODE/stack positions and returns. No root-return event occurs. Completion callbacks can later make the task ready; see the existing callback note. |
| Native callback returns EAX0 | `4c078a → 4c1524` returns interpreter EAX0 without root completion or task destruction. The scheduler stops its current pass on zero (`4788ef`). The dispatcher still masks this with AL1. Treat it as interrupted/uncompleted, not successful return. |
| Invalid opcode or stack growth failure | Default `4c1599`, or failed growth via `4c1474`, reaches **`4c15b9 → 4beb00`**. This is the failure/disposal routine. Object deletion also calls it through `4bec00`. |

`4be900` delivers the native result callback, removes the task registry entry
at `4bea5a`, releases stack values/storage and frees the task at `4beae3`.
`4beb00` reports failure and performs equivalent removal/free at
`4beb59/4bebe2`. Thus a registry miss cannot distinguish success from failure.
The positive witness is all of:

1. Bind `(VM epoch, task pointer, task ID, resolved root entry, receiver ID,
   operation sequence)` at the selected `4bb65b` invocation. Nested unrelated
   dispatches receive different records; never use one global scratch task.
2. Observe **call4c1592**, with its stack task equal to that record and its
   stored CODE offset equal to an accepted return offset of that root method.
   Copy any needed scalar result from `task+0x14 + 5*[task+0x18]` now. Do not
   retain its address. Reject `4c156b` and failure/disposal for that task.
3. Forward the completion call and observe its normal return; only then seal
   the event. Observe normal return from the enclosing dispatch/interpreter
   scope as well. Unwind after the event must not publish readiness.

Examples of accepted offsets (the byte immediately after opcode83): Restart
`0x16b16`, SetClipping `0x17db74`, SetMouseMapping `0x17e184`, base Resize
`0x18dc5b`, UpdateCursor `0x17e026`. The verifier enumerates every return in the
seven selected roots, including early monitor exits. Completion proves that
the selected KC invocation returned; it does not prove every optional child
effect succeeded. In particular, missing dynamic methods can return zero to
their caller. Required receiver/method resolution and the repaired scalar
postconditions remain separate checks.

## 2. Callable ABI, instances and lifetime

The existing named-method dispatcher is usable without installing a new VM
callback type. Its native entry is cdecl, returning AL, with words:

```
callback_type, callback_token, signed_receiver_id, method_name_pointer,
result_mode, argc, [argc integer payload words]
```

It is **six words for zero-argument operations**, seven for native Restart's
one argument, eight for MoveCursorAbsolute's two. Each argument becomes a
tag1/four-byte integer cell. The named lookup compares a native NUL-terminated
name (`4cd1d0`), not a CODE address or file STRG offset. Keep supplied name
storage immutable throughout the native call. Resolve the callee VA against
the loaded image, independently of rebasing heap pointers.

For mod-owned operations, `callback_type=32`, token0, result_mode1 gives a
specific callback-free route. On successful return, `4be9ee/4be9f1` reject
type>=32 before callback-table indexing. Missing-method/failure reporting
rejects the same type at `4bb248/4bb24b`. This retains ordinary task creation,
execution, pending handling and destruction. Type0 is **not** this contract:
the failure path can index callback slot0. Do not overwrite the game's
`owner+0x490/+0x498` result state or register a callback in an occupied slot.
Scalar getter results can instead be copied at the positive return witness.

Nonnegative receiver IDs select class singleton records from VM+0x20 with
count+0x1c. Negative IDs resolve the bitwise-complement key in VM+0x12d0's
dynamic registry (`4bb32f..4bb379`, independently `4bb1b0`). An object record
has class pointer+8 and five-byte member-array pointer+0xc; the class contains
member count+0x1c. Named dispatch walks the receiver's class hierarchy. There
is exactly **one Resize method** in this story, base2200 at0x18dc0d, so a live
2200 descendant's resolved named Resize can be checked against that entry;
its SpecialResize/Update remain virtual.

Task+0x3c is a borrowed current receiver, not a lifetime pin. Deletion removes
the dynamic registry entry, releases members, cancels tasks owned by that
object through `4bf983 → 4bec00`, writes object ID=-1 and class pointer=0 at
`4bf98f/4bf995`, and frees the object if its reference count is zero. Retained
object references can therefore keep a dead shell, which explains the missing
object return path above. A nonnull pointer is not a live instance witness.

Dispatch only on the admitted game/VM owner thread, outside render/load and
outside a running refresh. Re-resolve receiver IDs at each operation; check VM
epoch and class/entry before use. Do not keep member-array or instance pointers
across KC execution, window callbacks, yields or loads. A tiny scalar borrow
may copy/check fields without calling the engine; release it before dispatch.
Reload replaces story state even if VM/input addresses repeat. Bind operations
to the restored epoch, and invalidate pending records before a new load or VM
teardown. Observe task disposal (including deletion/cancellation), not only
the interpreter's explicit error call site, for a record that survives a yield.
No existing direct-read API is authorized by these offsets alone.

## 3. Injection boundaries and reentrancy

The following candidate spans are whole instructions; the verifier checks
image bytes, relocation operands and a `.text` direct-interior-edge census.
This census is not a proof about arbitrary indirect/foreign entry.

| Span | Adapter contract |
| --- | --- |
| `4bb65b..4bb660` CALL | Bind the selected task, then forward `4bb470` with ECX=task, no stack arguments. ESI/task and EDI/ID are live across the call. Preserve the original returned state and following `test al,al`. |
| `4c1592..4c1597` CALL | Positive root edge. Forward `4be900`, ECX=VM and **one callee-cleaned stack task word** (`ret4`). Read task fields only before forwarding; it is freed before return. |
| `4c156b..4c1570` CALL | Missing-object root path; same forwarding ABI, never a positive witness. |
| `4c15b9..4c15be` CALL | Explicit interpreter failure path, same ECX/one-word ABI to `4beb00`. Cancellation also reaches the routine from other sites; monitoring only this CALL is insufficient for asynchronous records. |
| `4beb00..4beb07` entry | Cancellation/failure lifecycle observer covering the other callers and tail entry from4becab. Incoming ECX=VM, `[ESP+4]`=task; seven bytes replay three saved-register pushes and the stack-relative task load. Copy identity before forwarding; never dereference after the routine frees it. |
| `4bed90..4bed95` entry | Interpreter scope covering initial execution and scheduler resume. Incoming ECX=VM, `[ESP+4]`=task, `ret4`. Five bytes replay frame establishment and `push -1`, preceding native SEH registration. Correlate normal return/unwind and EAX0 with a selected operation; do not turn foreign SEH into completion. |
| `477e6d..477e74` CMP | Concrete outer-loop transaction boundary before ordinary input. Seven bytes, one HIGHLOW operand at477e6f, crosses the aligned qword. Reproduce CMP against the relocated global after bookkeeping; the immediately following JNE consumes its flags. EBX is the live outer owner; preserve all other state. Install under qualified startup exclusion, not a presumed atomic seven-byte overwrite. |
| `4c0c10..4c0c16` two MOVs | Future dormant-menu seam in opcode87 handler. EBP is the interpreter frame, `[EBP-0x20]` current receiver, `[EBP-0x14]` VM, `[EBP+8]` task, ESI points to the operand. Exact replay sets ECX=receiver and EAX=receiver class. Gate by CODE offset, not every Init. |

The four CALL spans above fit their aligned qwords and contain no HIGHLOW
operands; reconstruct their relative target if moved. The opcode-handler span
and two entry spans also have no HIGHLOW operand and fit their aligned qwords.
All eight spans have zero surveyed direct interior
edges. Full GPR/flags, FP/XMM/MXCSR and LastError preservation is required for
bookkeeping; use the project's qualified envelope and four-byte incoming-stack
alignment. Neither native interpreter nor completion helper is a no-argument
C++ callback. No hook ABI is established merely by the Python requirement model.

Do not advance to another KC operation from the return observer: completion
still owns task/stack cleanup and may invoke native callbacks. The observer
only seals mod-owned data. The outer controller advances after native unwind
has completed normally. For the real Restart call477c8f, retain the earlier
seven-word forwarding envelope; bind its selected class150 task and require
return offset16b16 before treating the existing restart suffix as completed.

If an operation yields, keep its transaction/operation record outside a C++
stack frame and leave coordinates closed. Ordinary task scheduling through
`4788b0 → 4788e8 → 4bed90` must remain permitted so the selected task can resume;
ordinary coordinate consumers remain excluded. A later positive return seals
the same task identity, and the next outer boundary advances. Never pump the
whole scheduler recursively from the completion hook. The scheduler can execute
other KC tasks and window callbacks: revalidate the active menu/epoch before
publication, and defer destructive close throughout refresh just as during a
render borrow. A pending or interrupted task is not permission to publish, and
cannot be treated as a completed synchronous call because AL1 was returned.
Cancellation/deferred progression are implementation obligations, not measured
behavior of a new adapter. There is no proof here of a finite wall-clock bound
for arbitrary script/listener callbacks.

## 4. Concrete bounded refresh sequence

For a changed actual back buffer, the outer controller consumes the device/
dimension observation at477e6d; it does not execute KC inside D3D Reset. Its
transaction is epoch-bound, excludes ordinary coordinate consumers, and has a
candidate query generation visible to its admitted refresh work. Native CPU
screen dimensions must first be reconciled with actual surface dimensions;
the separate layout-generation note identifies that obligation. Publishing a
new scale before this state exists is not valid.

The minimal existing-method recipe is:

| Order | Dispatch/operation |
| --- | --- |
| 1 | `502::SetClipping`, argc0. Verify its normal root return and resulting members8..11. Clamp retained cursor members0/1 to those bounds under the same short scalar borrow **before any steering callback can consume them**. |
| 2 | `502::SetMouseMapping`, argc0; complete its selected task. This reproduces the current steering-mode mapping without rerunning global Restart. |
| 3 | `605::UpdatePositions`, then `605::UpdateVisibility`, argc0, each completed separately. Their existing initialized/empty exits are legitimate root returns. |
| 4 | Resolve current menu ID from503 member0. If nonzero, dispatch that instance's **Resize**, argc0, requiring resolved base2200 entry18dc0d and its positive return. A genuinely absent current menu requires no Resize; failed resolution of a nonzero ID is not the same case. Recheck current ID after callbacks. |
| 5 | Repair shown632/633 positions through the bounded seams below; ensure dormant2200 reactivation has the generation-aware repair seam installed. |
| 6 | Clear both input-route carries and establish the new restored/generation epoch, then `502::UpdateCursor`, argc0. Its normal return follows native cursor/instance positioning, listener calls and icon refresh. Only afterward publish coordinate readiness; synchronous listeners must already observe the new epoch and candidate coordinates. |

Using active-instance Resize instead of503 Reload avoids Reload's unrelated
Abort630/Stop640/ResetSavegameCache640/Close641 operations. This is a semantic
selection based on the concrete Reload body, not a claim that those operations
are harmless. ResetFollowSteering is unnecessary for a dimension refresh:
SetMouseMapping preserves the selected mode and final UpdateCursor does the
actual clamp/native steering/listener work. The complete Restart remains the
game's own post-load path, not a Reset callback.

For a saved load, observe its actual Restart first, with coordinates closed.
The cursor scalar clamp must occur before Restart's ResetFollowSteering can
send stale coordinates; the existing `_Restart` SetClipping return at CODE
`0xd358` (call atd353) is the corresponding **in-task** ordering seam. A root
Restart completion observer alone is too late for this earlier consumption.
The native ordinary-return continuation is `4c08f0..4c0904`, with saved
destination CODE offset at `[EBP-0x34]`; gate on d358 **and the selected Restart
task**, then perform only the admitted scalar clamp before resumed KC runs.
This is a next bounded clamp adapter, not one of the qualified spans above.
After positive root completion, repair residual dormant/shown owners and run
final UpdateCursor. A new load epoch is required even for unchanged dimensions.
Do not blindly replay Restart or classify its AL1 as readiness.

## 5. Next finite repair seams

**Dormant2200:** gate opcode87's entry above by ESI=`VM.CODE+0x18d7d0`, the
operand of CreatePrevious's Init call at18d7cf. This is after the state0 test
and before Init. The receiver is the particular ancestor selected by the
recursive CreatePrevious path; repairing only the initially active object is
insufficient. Reuse the same virtual GetMenuMaxHeight/store80 and
GetMenuMaxWidth/store81 sequence that Create executes at18d6fa/18d704. Prefer
a same-task continuation so getter completion/yield has ordinary VM semantics;
do not run SpecialResize on a disabled scene. The exact added continuation,
stack capacity and loaded-CODE ownership/patch strategy still need a bounded
implementation review. This identifies one PC/receiver seam, not a request to
audit the whole story again.

**Shown632/633:** both IsShown methods test member2 (instance handle), at
195d0b/19631b. Hide frees this instance then clears2, frees/clears camera1 and
scene0, and changes message/timer state. A refresh must not call Hide or Show.
For a shown owner, read width6/height7 and recompute x=(screenWidth-width)/2,
y=screenHeight-150-height/2 (632) or -250-height/2 (633), write4/5, and apply the
existing four-argument B3D_InstSetPos pattern from **195f4b / 1964eb** using
member2 and those coordinates. The native-call ABI is the KC opcode82/native
table route, not a guessed direct C++ B3D function pointer. The bounded next
adapter is a KC continuation/helper containing only these query/formula/store/
position operations, or a separately qualified typed native adapter. Re-resolve
the owner/instance immediately before publication; no borrow survives a callback.
633 Update at19666a already applies x and saved y but also redraws the timer;
632 Update does not reposition. Neither Update alone closes the saved-y defect.
Do not reuse a saved scene/instance pointer after a yield or alter timer tasks,
delay/fade state, or message contents to achieve a resize.

These two repair adapters and the early saved-Restart clamp remain concrete
implementation work. The dispatch/completion contract above makes their order
and acceptance observable; it does not claim a completed FL UI feature.

## Verification and limits

`python3 verification/probe/verify_fl_ui_refresh_completion.py --raw` passes
**60 instruction/operand anchors, eight complete regions /729 instructions,
eight span checks, seven selected KC roots, four hash-bypassing mutations and twelve
requirement cases** (measured static counts). Mutations alter the root-return
branch, callback bound, success-to-failure callee, and SetClipping's return
opcode. Cases distinguish selected return from AL1, yield, wrong task/PC,
failure disappearance, missing owner, native abort, unwind, resumed selected
return and mismatched VM epoch. They are a
requirements oracle, not a native task simulator or hook fixture.

The verifier produces ignored `summary.json`, `reproduce.sh` and bounded raw
listings under `verification/results/fl-port/ui-refresh-completion/`. Raw
exploration remains there too. Python compilation and `git diff --check` are
the affected checks. No runtime performance, thread coverage, SEH wrapper,
changed-size Reset, native Windows or CrossOver behavior is measured. Static
evidence supports the proposed transaction; implementation must retain the
project's owner admission, load/render exclusion, rollback and exception rules.

## Independent review

2026-10-28: independent deep review reran the verifier and inspected native
dispatch arguments, both callback bounds, normal/dead-owner returns, task
destruction, scheduler resume and selected KC ordering/repair seams. No blocking
factual defect remained. The acceptance is the scoped completion/dispatch
contract, not implemented hooks or proof that optional child effects succeeded.
