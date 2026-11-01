# FL UI load readiness and scene-render admission

Offline study, 2026-10-27. Extends the reviewed
[scene lifetime contract](ui-camera-lifetime.md), reuses
[object lifetime](object-lifetimes.md) and
[startup fatal-exit](startup.md) evidence. Registry incarnation
belongs to the separate [registry binding study](registry-binding.md).
Addresses are preferred FL VAs; resolve against the loaded image base.
No production edits, game/Wine execution, builds, installation or commits.

## Result and scope

There is **no established native exclusion** preventing a render callback between
restored scene publication and cockpit ownership restoration. This study does
not find an actual intervening draw either. The normal parent CFG establishes
sequence, while calls remain opaque to callback/nonlocal/thread execution.

A bounded implementation can close this gap without changing UI coordinates:
hold **scene-render admission closed for the entire enclosing load**, gate the
complete native scene-render pass at `0x48b6d0`, and reopen only after normal
parent success **and observed successful cockpit loading**, with role tracking
complete. Keep the UI scale factor, screen queries, input/cursor conversions and
KC layout unchanged throughout. A nested render attempt during an admitted load
returns before entering the native render body. This suppresses a render pass;
it does not reinterpret an unknown scene as ordinary UI or reset UI layout.

This is a concrete static interception contract under the already-required
load/render thread admission condition, not proof that arbitrary concurrent or
reentrant engine entry is supported. In particular, a load starting inside an
already executing render needs separate exclusion/deferral qualification. The
three proposed seams below close **render-during-load**, not the inverse case.
The latter must not be hidden behind a private lookup-table lock or invented
failure return.

## 1. Enclosing load, successes and failures

`0x479430` takes its slot/index in ECX and no stack argument, and has ordinary
`ret` at `0x479951`. Its only surveyed direct caller is `0x477c52`; ECX is the
remainder of owner+0x4e0 divided by 100. The caller tests EAX at `0x477c57`.
Successful loading continues to the script callback at `0x477c8f`; the ordinary
frame later calls cockpit update `0x4782ca` and renderer `0x4782cf`.

Both calls belong to outer loop `0x477ba0`, whose only surveyed direct caller is
`0x477a9a` in startup/state dispatch. The full outer-loop normal CFG proves that
render continuation `0x4782d4` cannot reach load call `0x477c52` within the same
invocation; its backedge returns to `0x477e80`, after the loading branch. Conversely,
successful load continuation can reach the render call. Thus the normal native
path does not suspend an active render to load; a counterexample requires nested
entry, another thread, exception/nonlocal transfer or an unobserved alias, not
an ordinary loop iteration. Calls remain opaque in this CFG too.

| Stage | Evidence |
| --- | --- |
| Renderer subsection | `0x47984c → 0x493510`; scene-list publication at `0x49385d`. Parent tests EAX at `0x479851`; zero branches to failure `0x479965`. |
| Between renderer and cockpit | Calls `0x4b4a50`, `0x418510`, `0x448bb0` at `0x47985c/0x47986d/0x47987e`, with failure checks; allocation/constructor of cockpit registry follows. |
| Cockpit registry | `0x4798b2` writes `*0x5a82e4`. **Null at 0x4798b9 skips cockpit loading to 0x4798cc.** This path can still reach parent success. |
| Cockpit restoration | `0x4798bf → 0x420d30`, one callee-cleaned stream argument; parent tests **AL** at `0x4798c4`. The loader loops through `0x420e22 → 0x427800`, which calls `0x41a400` at `0x427868`; owner+4 role binding is `0x41a673`. |
| Cockpit completion | Each successful owner is inserted at `0x420e33`, followed by loop backedge `0x420e3f`. After all owners, the subsection end marker controls AL through `sete al` at `0x420ea1`; return is `ret 4` at `0x420eaf`. Empty owner count is valid; a null registry bypass is a different event. |
| Remaining parent work | `0x4798cf → 0x4b79f0`, final marker validation, filename/stream cleanup, `0x4a2890` and `0x4fff90`. Only then EAX=1 at `0x47993e` and normal return `0x479951`. |

The verifier builds the parent CFG, allowing every ordinary call to return and
using the previously established normal termination of `0x4762d0`. Renderer
loading dominates the successful parent return. **Cockpit loading does not**:
removing the `0x4798bf` node leaves a success path through the null-registry
branch. Therefore neither renderer-subsection success nor parent EAX=1 alone
establishes role completeness. Observe a successful actual cockpit-loader call;
never infer it merely from the parent result or nonempty role set.

Explicit parse/subloader failure branches converge on `0x479965`. After cleanup,
`0x479a25` sets ECX=0x1d and `0x479a2a` calls `0x4762d0`, followed by INT3.
The reviewed fatal path reaches the CRT exit wrapper at `0x476ab7`, then normal
ExitProcess through IAT `0x5501b4`. There is no normal false return from this load
body. Treating its fatal path as nonreturning relies on the reviewed normal CRT
exit contract; exit callbacks and nonlocal transfers remain outside that CFG.

The enclosing native SEH record is installed at `0x479446`; its handler thunk
`0x54d8ba` supplies metadata `0x59cefc` to `0x521368`. Cleanup thunks close the
stream (`0x54d8a3`) or free a partially constructed cockpit registry
(`0x54d8b1`). These do not publish UI readiness. A mod wrapper must leave
readiness closed on foreign unwind or abnormal/nonlocal escape; do not implement
it with normal-return-only scope cleanup. A resumed exception that continues
native execution must not spuriously decrement an active scope.

## 2. Callback frontier and the correct render boundary

Concrete post-publication callback opportunities exist in renderer restoration:
allocation failure at `0x493e08` and `0x493fef` calls the function loaded from
`*0x5aae24`. The reviewed initialization binds that callback to `0x47b500`.
Its known engine/texture recovery path is not a renderer lock; texture recovery
also enters D3D. The reader `0x506700` has a `gzread` import call at `0x5067b0`
and CRT file-reading paths. None is asserted to pump a render; neither a linear
absence of render calls nor ordinary same-thread call sequencing proves that all
foreign callbacks and threads are excluded. No actual load-to-render callback
chain was established, and no such chain is fabricated as a positive witness.

The gate does not need that negative proof. It intercepts the scene-render
entry itself. The full direct caller census establishes this reverse cone:

| Target | All surveyed direct callers |
| --- | --- |
| `0x4de570` projection | `0x496fb9` in node traversal, `0x4976a0` in deferred traversal |
| `0x4968c0` node traversal | Recursion `0x497583/0x497594`; scene traversal `0x497aa1/0x497ac5` |
| `0x497680` deferred traversal | Main pass `0x48ba8d/0x48bcb7`; cubemap pass `0x4978ab` |
| `0x4978e0` scene traversal | Main pass `0x48ba73/0x48ba7f/0x48bca9`; cubemap pass `0x497892/0x49789d` |
| `0x4977c0` cubemap pass | `0x48b9f6` inside main render pass |
| `0x48b6d0` main render pass | `0x4782cf` |

There are zero absolute DWORD pointer literals for these six targets, the
enclosing load, renderer loader and outer loop in the surveyed image sections. This rules
out a literal static publication found by that search, not computed pointers,
unobserved indirect edges or another mod invoking an interior routine. Within
this fixed native direct-call cone, gating `0x48b6d0` covers both projection
callers and cubemap rendering with one entry hook. It also handles a callback
which reenters that same entry during loading, regardless of the callback's
source. Arbitrary direct/indirect entry below it is outside the admitted profile.

The pass has no stack arguments and returns with plain `ret` at `0x48be30`.
Its sole caller immediately compares a global at `0x4782d4`; it does not consume
a return value. Denying admission before the prologue avoids all of the pass's
engine reads, allocations, temporary camera-flag changes, D3D calls and paired
cleanup. Do not jump to its epilogue after executing a partial prologue. A denied
pass returns with caller state preserved and native stack shape intact.

**Projection EAX=0 is not a draw gate.** Both projection callers continue without
testing its result; the next common helper `0x5139a0` overwrites EAX at
`0x5139af`, and deferred submission still calls `0x4e6ef0` at `0x497702`.
Suppressing only projection would leave submission using stale/incomplete
projection state. Gating the complete pass avoids that failure and avoids
inventing cleanup semantics for individual node/deferred loops.

## 3. Implementable load/readiness state contract

The added scope is independent of GPU history and the connected renderer:

1. Install the scene publication/retirement hooks and this admission group at
   the qualified early boundary, before any unobserved scene or load. Establish
   the normal initial role baseline. Initial UI scaling can be configured once;
   saved-game loading is not a scale-factor transition.
2. Before forwarding `0x479430`, obtain exclusive load admission while no scene
   render is active. Set phase=loading and reset this scope's cockpit-result
   witness. Retain existing scene roles until their real destruction; do not
   clear all roles merely because another load starts.
3. While loading, deny `0x48b6d0` before its native prologue. Continue observing
   new/restored scene bindings and scene retirement. No scene lookup is allowed
   to interpret a missing role as ordinary rendering during this interval.
4. Record AL!=0 from the actual `0x4798bf` cockpit call. Keep admission closed
   through all remaining parent work, not just this subsection return.
5. On normal parent return, reopen only if EAX=1, that cockpit call succeeded,
   and scene tracking remains complete. This precedes the caller's normal
   post-load script callback. The same protocol applies to every later load.
6. On absent cockpit witness, allocation/tracking failure or abnormal exit,
   keep scene rendering quarantined. Native fatal teardown may proceed; a
   foreign catch does not automatically make the partially restored world safe.
   Recovery/restart is an explicit owner decision, not a projection fallback.

This preserves existing KC cursor, clipping and layout because their coordinate
system never changes. The intended visible cost is omission of scene-render
passes attempted during loading; loading UI rendered by that pass would pause.
Other display/loading work is not promised to stop. Visual behavior and whether
any such pass actually occurs need user-run evidence, not speculation.

### Thread and reentrancy admission limit

Use the existing scene tracker thread-admission contract; bookkeeping locks must
not be held across native calls. A render entering after loading has exclusive
admission is simply denied. A cross-thread load must first exclude already
admitted renders, using a qualified mechanism. **A same-thread load nested inside
an admitted render cannot wait for itself.** This study does not supply a safe
deferral for that unsupported entry: the native direct caller is outside the
render cone, but arbitrary callback reentry into the outer loop is not disproved.

In particular, do not synthesize EAX=0 from the load wrapper to pretend it was
deferred. Its caller then sets `*0x5a84c4=1` and jumps to `0x4785a1`, whose script
callback and cleanup destroy cockpit/engine state (`0x4785e6`, `0x478639`). That
can invalidate an outer renderer. A supported implementation must qualify the
normal nonnested load/render admission condition or establish a safe earlier
deferral point before claiming this inverse case. The policy witness models a
refused admission decision, not an invented executable native-return policy.

The smallest enforceable protocol therefore has mutually exclusive load-writer
admission and render-reader admission, with both transitions checked atomically
before native forwarding. A reader admitted first must finish before any load
mutation begins; a load admitted first causes later render entries to return
without forwarding. A render token cannot simply be invalidated and then allowed
to resume after loading: its native locals may contain freed camera/node pointers.
Waiting for an active reader is only valid under a separately qualified
cross-thread progress contract, never for a same-thread reader. The normal CFG
supplies the intended nonnested route; it does not supply that foreign-entry
policy. Until the latter admission condition is qualified, these seams are a
reviewable implementation design, **not authorization to enable the feature**.

The next proof is concrete native entry ownership, not support for arbitrary
foreign calls into private addresses. Start with the renderer's actual
scene-owned callback at **0x48bdd7**: ECX comes from scene+0x54, EAX is its
vtable, call slot +0x3c. Resolve its installed target(s) and whether they can
synchronously reach outer loop `0x477ba0` or load `0x479430`. The other bounded
frontier is the already identified window procedure **0x4ec790**, registered by
the display constructor at `0x4f3abc`, when dispatched during a renderer's
COM/D3D operation; trace any VM callback reached from these concrete paths only
far enough to distinguish queuing a later mode transition from invoking the
loop/load synchronously. Ordinary post-load VM call `0x477c8f` is already after
the load scope, so its existence is not an overlap witness. No synchronous
loop/load edge from these callback frontiers is established here. Unknown mods
or deliberate private-address calls are outside the native profile, not an
endless additional qualification requirement.

## 4. Exact hook seams, ABI and cost

| Purpose; displaced span | Contract |
| --- | --- |
| Load entry `0x479430..0x479435`, 5 bytes (1+4) | `push ebp; lea ebp,[esp-0x28]`. ECX=slot/index; no stack arguments. Preserve entry state around admission, then replay the exact prologue through a trampoline. Forward original once. Observe actual EAX and preserve post-native registers/flags, FP/SIMD and LastError on return; qualify native unwind handling. |
| Render entry `0x48b6d0..0x48b6d5`, 5 bytes (1+2+2) | `push ebp; mov ebp,esp; push -1`. No stack arguments or consumed result. Denied branch executes none of these and returns plainly with state preserved; admitted branch replays all, then releases reader admission after normal/qualified abnormal completion. |
| Cockpit-result call `0x4798bf..0x4798c4`, 5 bytes | Relative call to `0x420d30`; one stream pointer is already pushed. Native callee uses `ret 4`; any wrapper must forward one argument copy while reproducing that cleanup exactly. Observe **AL**, not unspecified upper EAX bits; preserve actual returned state before `test al,al`. |

All three spans have zero HIGHLOW operands. Both entry spans fit an aligned
qword; the cockpit call at qword offset seven crosses it. The call's relative
target must be reconstructed if relocated. The full-text census finds zero
direct interior edges. These facts do not authorize concurrent patching;
installation, rollback, callback ABI, LastError and four-byte incoming stack
qualification remain production acceptance work.

Added steady-state work is one small admission check per complete scene-render
pass, with no extra engine read, registry walk or allocation per draw. Load
scopes and one cockpit result are recorded per load. Existing per-scene role
allocation and per-pixel-projection scene lookup retain their prior costs.
No benchmark or game FPS claim is made.

Earlier role inference from serialized ownership is not needed for this bounded
gate. The renderer SCEN record is decoded before the later cockpit section;
mutable names/activity flags do not certify native HUD ownership. A validated
pre-parser would need stream positioning and ownership validation across earlier
sections, plus failure/unwind semantics. No such parser is qualified here.

## Verification and remaining boundaries

[verify_fl_ui_load_readiness.py](../../../verification/probe/verify_fl_ui_load_readiness.py)
binds the reviewed EXE SHA and checks **62 semantic instructions**, **10 regions /
4,033 instructions**, **3 hook spans**, the parent and outer-loop CFGs, **428,814** full-text
instructions and the complete surveyed direct reverse cone. Seven sequential
policy cases cover load/render denial, unchanged coordinates, delayed readiness,
subsequent loads, null-registry bypass, unwind/completeness failures and inverse
reentry refusal. They do not qualify native synchronization or exception frames.

```sh
python3 verification/probe/verify_fl_ui_load_readiness.py --raw
python3 -m py_compile verification/probe/verify_fl_ui_load_readiness.py
```

Ignored reproducer, parsed summary and raw bounded evidence live under
`verification/results/fl-port/ui-load/`. The remaining exact frontier is load
admission while a render is already active through the identified native callback
frontiers, native wrapper/foreign-unwind qualification, and actual visual
behavior during a load. The original restored-scene publication gap does not
require changing the scale group or reconstructing the whole renderer: under
qualified admission, the proposed gate prevents the consuming pass until role
completion is observed. Native no-draw exclusion itself remains unproved.
