# FL saved KC UI layout and restart refresh

Scope update, 2026-10-28: the [AP/FL save-load comparison](ui-save-load-comparison.md)
finds no need for additional layout repair or KC completion hooks at unchanged
configured scale/resolution. The user excludes in-game resolution changes.
Earlier repair/changed-generation requirements below are conditional engineering
proposals, not active port prerequisites. Native borrow/ABI findings still apply.


Offline, 2026-10-28. Preferred FL VAs and file CODE offsets; clean image/story
hashes are bound by the [verifier](../../../verification/probe/verify_fl_ui_saved_layout.py).
Extends [layout generation](ui-layout-generation.md) and
[load readiness](ui-load-readiness.md). No production changes,
execution under Wine/native Windows, game launch, build, install or commit.

## Result and correction

Saved cursor coordinates, clipping bounds, menu size limits and timed-message
positions **are serialized**, including when the next session selects a different
UI scale. They are not reconstructed by `502::Init` on a normal saved-game load.
There is a real every-load refresh: successful native load is followed by
`150::Restart → _Restart`. It refreshes clipping, input mapping, monitor positions
and the active menu. It does not alone establish complete coordinate consistency:
clipping refresh leaves the cursor position unchanged, dormant menu ancestors
retain their cached limits, and timer/message owners retain vertical positions.
These are concrete supported UI owners, not foreign private-address callers or
corrupted-state examples.

Two earlier negative conclusions were decoder errors. The fixed story has
**243 SYMB global functions**. SYMB is BE32 count followed by four BE32 words per
record: CODE entry, string offset, local count, argument count. CLAS method
boundaries alone accidentally attributed global functions following Patch7 to
that method. Opcode **0x84 is a direct global-function call**, omitted by the
old call census. `_Restart` is `0xd1bb..0xd3bb`, locals=3, argc=1;
`150::Restart` calls it at `0x16b04`, before X2_SetModified at `0x16b0e`.
The parent corrected the shared reader and previous note separately. The claim
that Restart only sets the modified flag, and the Patch7 attribution of
`0xd353/0xd385`, must not be reused.

## 1. Persistence and actual load ordering

The native save routine begins at `0x4bb920`. Its `OBJE` section at `0x4bc314`
writes all class records: VM=`*0x5aa174`, count=`VM+0x1c`, array=`VM+0x20`,
record stride **0x38**. Each record contributes its ID, member count at `+0x1c`,
and every member from the array at `+0xc`, stride **5 bytes**
(`0x4bc3d0..0x4bc3e5`). There is no UI-class exclusion in that loop.
A member is a one-byte tag followed by a four-byte payload. Integer tag 1
selects `0x4bb843`, which writes that payload through `0x5069f0` without any
coordinate conversion. Type values in CLAS are types/default metadata, not a
transient-field exclusion flag.

The load parent calls story-state loader `0x4bcb10` at `0x47983c`, before renderer
and cockpit restoration. It first reloads the story object through
`0x4bcc27 → 0x4b8e20`, then restores saved state over its defaults. The OBJE
reader resolves the class record and loads members at `record+0xc+5*index`
(`0x4bd619..0x4bd652`). Its older-build member selection compares version
metadata at `0x4bd60e/0x4bd612`; it is not a screen-layout migration. Scalar tag1
selects `0x4bc96a`, reads a DWORD, then uses `0x4c5590`; the latter stores the
payload at `+1` and tag1 at `+0` (`0x4c55a1/0x4c55a6`).

Dynamically created objects are restored separately through VM's `+0x12d0`
registry. The reader resolves their class/superclass, writes the class pointer
at object+8 (`0x4bd826`), allocates/publishes the five-byte member array at
object+0xc (`0x4bd8d0`), initializes metadata defaults, and restores members
through `0x4bd93e → 0x4bc8c0`. Thus a saved menu chain is not equivalent to
calling Create/CreateNext on newly constructed menus. Parent traversal and
member version checks preserve inherited state as well.

Relevant persisted integer fields are:

| Owner | Members and interpretation |
| --- | --- |
| 502 cursor singleton | 0/1=x/y; 8/9=clip minima; 10/11=clip maxima. Its listener array/table at13/14 and cursor instance at18 also belong to the restored owner; do not replace the singleton with a fresh Init. |
| 503 menu manager | 0=current menu object ID. Reload reads only this member for its Resize call. |
| 2200 menu base and descendants | 0=previous menu, 5=lifecycle state, 69=content-height limit, 71=chosen height, 80/81=cached maximum height/width. Dynamic graph/map instances retain these inherited members. |
| 632 short message / 633 timer | 4/5=position, 6/7=width/height; 0/1/2 hold scene/camera/instance ownership used by Show and Update. |

Native success from `0x479430` does **not** finish restart: its caller prepares
seven caller-cleaned words at `0x477c6a..0x477c8f`, then dispatches
`Restart` (string VA `0x58ba28`) for the restored current class via `0x4bb5b0`.
For the qualified class150 profile this reaches CODE `0x16af4 → 0xd1bb`.
Only after that dispatch does `0x477c97` jump to `0x477e6d`, preceding ordinary
input and scene-render work. Existing scene-role load readiness may finish at
native load return; **coordinate readiness cannot infer restart completion from
that earlier boundary**.

## 2. What the native restart actually refreshes

The global restart's UI suffix is sequential:

| CODE call | Existing operation and coverage |
| --- | --- |
| `0xd353` | 502 SetClipping: stores minima0 and current screen-query width/height to members8..11. |
| `0xd367` | 502 SetMouseMapping: restores the native control matrix for current steering mode. |
| `0xd371` | 605 UpdatePositions: if initialized, calls UpdateViewportPos on monitor-array entries1/2 (`0x17ece0/0x17ecec`). |
| `0xd37b` | 605 UpdateVisibility, including UpdateOverlays. |
| `0xd385` | 503 Reload: abort630, stop640, reset savegame cache640, close641, then Resize **current menu only** at `0x28a89`. |
| `0xd38f` | 502 ResetFollowSteering, which may change steering mode and update native cursor steering. |

Base menu Resize (`0x18dc0d`) calls virtual GetMenuMaxHeight/Width, stores80/81,
recomputes71, calls virtual SpecialResize, and invokes Update for state2.
Graph2327 SpecialResize (`0x23d821`) repositions its graph bodies and calls
BuildGraphData at `0x23d865`. This is real graph reconstruction, not just a
native screen-width store.

The complete `_Restart` also restores fonts, initializes game databases,
executes setup scripts and performs save migration. Calling it wholesale after
a D3D Reset would repeat unrelated game operations. The identified UI suffix is
useful evidence for a new refresh transaction; it is not a qualified standalone
Reset callback.

### Cursor and native input state

SetClipping does not write members0/1. CenterCursor writes them but does not
update the native cursor or listeners. MoveCursorAbsolute (`0x17db22`) writes
0/1 and calls UpdateCursor at `0x17db34`. UpdateCursor clamps against8..11,
positions the cursor instance, calls UpdateCursorSteering at `0x17df33`, then
notifies listeners using a cloned listener array (including menu mouse-over
handling) and refreshes its icon. It is reentrant native/KC work, not a scalar
setter suitable for execution in the middle of an unfinished menu refresh.

`__SetSteeringMode` instead calls **UpdateCursorSteering directly** at
`0x17d98b`, without UpdateCursor's clamp. Therefore ResetFollowSteering does not
prove cursor repair. In mode2 its branch can skip even that direct update.
The full native instruction census finds only two MOV destinations for the
real-pixel cursor globals `0x5aa180/0x5aa188`: `0x47bc50/0x47bc58` in the
reviewed X2_UpdateCursorSteering native. Loading KC coordinates does not write
those globals. Merely clearing fractional carries cannot resynchronize them.

A supported changed-generation refresh needs the new clipping first, a chosen
cursor-position policy (clamp retained logical coordinates is the native
UpdateCursor policy), all affected layout state repaired, then the actual
UpdateCursor path to resynchronize scene position, native stores and listeners.
This also needs a new restored-UI epoch on every load: the session's native
input allocation survives and pointer/class/route comparison alone can retain
fractional residue from the pre-load cursor. Clear both route carries before
reopening input; synchronous callbacks must observe the new epoch.

### Dormant menu chain

Create/CreateNext refresh80/81 before Init (`0x18d6fa/0x18d704`,
`0x18d73e/0x18d748`). CreatePrevious (`0x18d768`) does **not**. When its state is
zero it calls virtual Init at `0x18d7cf`; its recursive previous-menu branch
also does not refresh limits. Disable (`0x18df4a`) sets state0, frees instances
and calls ExitMenu, preserving the menu object and these limits. Consequently,
restored inactive ancestors cannot be dismissed as new layouts: they have a real
later reactivation path that retains saved80/81.

For example, graph max width/height are clamp512..1024 of
`min(screenWidth-512, screenHeight-128)`. Saving at virtual2560x1440 gives1024;
reopening that ancestor at virtual1280x720 requires592. Reload of a different
current menu does not change the ancestor's1024. These numbers are **inferred
from bytecode formulas**, not measured visual output. A finite repair must cover
all retained ancestors or refresh cached limits on their reactivation **before
Init consumes them**. Calling SpecialResize on every dormant object is not
qualified: those objects may have no live scene and subclass callbacks may have
other effects. Reusing the pure getters/store sequence at reactivation avoids
assuming that dormant SpecialResize is safe, but its injection ABI is still to
be implemented and verified.

### Messages and timers are independent owners

632 Show (`0x195d1d`) computes x=(width-m6)/2 and y=height-150-m7/2 at
`0x195e56/0x195e6a`. Its Update (`0x1960d2`) redraws the texture and changes
neither position. 633 ShowTimer (`0x19634b`) uses y=height-250-m7/2 at
`0x19640a`. Normal RunTimer (`0x1967bc`) repeatedly calls Update; Update
(`0x19663a`) recomputes x at `0x196653` and repositions the instance using the
**saved y**, without querying height or assigning member5. For m7=64, changing
virtual height1440→720 leaves y1158 where the ShowTimer formula requires438.
No new ShowTimer call is intrinsic to restoring its saved task and fields.

The restart UI suffix above does not reconstruct either singleton. A finite
refresh must recompute their shown instances'4/5 from the established Show
formulas and reapply instance position, retaining their message content, timer
state and ownership. Re-running Show/ShowTimer is not equivalent: they hide or
reallocate scenes, establish delays/tasks and change timer/message lifetime.
The evidence identifies the exact stored values and safe semantic operation;
it does not yet qualify a production field-access/instance-dispatch adapter.

## 3. Concrete seams and transaction limits

`0x477c8f..0x477c94` is one five-byte relative CALL to `0x4bb5b0`, zero HIGHLOW
operands, no surveyed direct interior branch, and crosses its aligned qword.
The native cdecl words at wrapper entry are context, zero, class, name, mode1,
argc1, and owner+0x4e0 argument; copy all seven when forwarding, preserve their
original caller cleanup `add esp,0x1c`. EBX is the outer owner and EBP the outer
frame. A no-argument forwarding cast is invalid. Capture native returned state
before bookkeeping, preserve GPR/flags/FP/XMM/MXCSR/LastError using the qualified
wrapper envelope, and retain owner-thread admission across callbacks. This call
can own the **restart observation scope**, while the existing load scope owns
deserialization. Keep coordinate readiness closed between them.

Dispatcher normal return is not an actual KC completion witness. `0x4bb65b`
runs `0x4bb470 → 0x4bed90`; afterward, a remaining task is passed to the
diagnostic stack scan `0x4be7d0` at `0x4bb69c`, and the routine still returns AL1.
The earlier cleanup label was incorrect: this scan does not free/unlink tasks.
[Completion tracing](ui-refresh-completion.md) distinguishes
normal root return through `0x4c1592 → 0x4be900` from failure/disposal through
`0x4beb00`; task disappearance also cannot prove success. Missing class/method
returns AL0. The caller tests neither. A real transaction needs positive
observation that its selected KC refresh operations completed, not AL1 alone;
this study does not qualify a KC completion hook. SEH unwind from load/restart
must keep readiness closed and release only the wrapper's own scope, never
publish a completed generation or run listener callbacks during cleanup.

The finite semantic transaction identified here is:

1. On the admitted owner thread, exclude input/coordinate-consuming work and
   retain the old generation until an actual new screen/query state is ready.
2. Establish new query dimensions and clipping; repair saved/current cursor
   coordinates before native steering can consume them.
3. Complete the existing active-menu/monitor refresh, repair dormant menu limits
   before their next Init, and reposition shown632/633 without restarting tasks.
4. Run the cursor UpdateCursor path after layout completion, clear carries with
   the restored/generation epoch, then publish coordinate readiness.

For saved loads, steps2–4 must extend the actual post-load Restart path; doing
all of them at479430 return and then allowing Restart to repeat partial state
changes is not an established atomic implementation. For successful changed-size
Reset, native code still supplies no KC transaction or screen-record dimension
refresh. Reuse the bounded UI operations above through an explicit qualified
owner-boundary transaction, **not** the entire Restart or a migration callback.
Native saved-state reconstruction is now positively identified, but a complete
live refresh adapter is **not established by this study**. The remaining exact
seams are KC-operation completion, ordered cursor dispatch, dormant-menu getter
refresh and shown-message/timer position publication. Fixed-scale/frozen-auto,
identity or silent disable do not close these requirements.

## Verification and review

`verify_fl_ui_saved_layout.py --raw` passes **40 native instruction/operand
anchors, five regions / 428 instructions, 24 KC regions, four mutation negatives
and four logical counterexamples**. It checks scalar save/load dispatch,
class-member types, global-call boundaries, the native Restart call span and
cursor-store census. Mutations bypass the image hash and change the member
stride, call cleanup, global Restart target and clipping target. Counterexamples
show why clip-only, active-only, horizontal-only and pointer-only updates fail;
they are not native wrapper tests or measured gameplay.

```sh
python3 verification/probe/verify_fl_ui_saved_layout.py --raw
python3 -m py_compile verification/probe/verify_fl_ui_saved_layout.py
```

Ignored raw listings, producer, summary and exploration stay under
`verification/results/fl-port/ui-saved-layout/`. The verifier is the producing
script; `reproduce.sh` records its invocation there. No copyrighted bytes were
added to tracked fixtures. Static facts and formulas are image-bound; actual
completion, reentrancy/SEH adapters and native Windows/CrossOver behavior remain
unmeasured. No performance timing or activated UI support is claimed.

Independent bounded review of the parent's SYMB/parser correction found no
issue: the observed four-word format, 243 globals, absence of class/global
entry aliases in this image, new boundaries and opcode84 match the independent
section read. Existing reported check results were reused; no redundant suite,
Wine run or production review was performed.
