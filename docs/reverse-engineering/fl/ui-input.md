# FL UI input modes, native coordinates and camera exclusion

Offline study, 2026-10-27. Extends the reviewed
[graph/coordinate study](ui-graphs.md) and
[engine anchors](engine.md); integration belongs to the
[FL port](../../architecture/farnhams-legacy-port.md). No production edits,
game/Wine execution, DLL build, installation or commit. Preferred executable
VAs throughout; KC addresses are file CODE offsets. `s` means UI projection
scale, not font/texture density.

## Outcome and correction to the earlier input candidates

The cursor path is **not** established by the two `MouseInput` readers
`0x4780d1/0x478113`. The same outer loop calls **`InputMouseDelta` earlier**,
at `0x477fed`, using different signed-word readers. The later `MouseInput`
events execute only for class IDs 103, 104 or 105. None of those classes, nor
a method named `MouseInput`, exists in the fixed FL story's CLAS table. Thus
there is no established `MouseInput → 150::InputMouseDelta` chain; treating
the former as the latter would miss the actual direct callback.

Two cursor-only native boundaries are established: the direct callback and
the post-mapping accumulated callback. Scaling the **outgoing argument copies**
there avoids changing native flight-axis deltas. The earlier four-reader AP
recipe must not be transplanted to FL. The older graph note's instruction
boundaries remain valid; its mode/reachability gap is resolved here to the
extent stated below, not by relabelling the raw readers as menu-only.

## 1. Dispatch, mode selection and exact coordinate flow

### Direct loop and legacy events

At `0x477ef9`, `[EBX+0x4d8]==1` selects the direct callback path. EBX is the
loop/VM owner, `[EBX+0x418]` its callback context and `[EBX+0x4dc]` the class
ID. The input context is `*0x5a8478`; `+0x414/+0x416` are signed 16-bit dx/dy.
The zero tests at `0x477fa9/0x477fb3` lead to signed Y/X loads at
`0x477fc6/0x477fce`, then `InputMouseDelta(dx,dy)` through `0x4bb5b0` at
`0x477fed` (eight caller-cleaned words). Its first six arguments are context,
0, class, method-name, 1, 2; the final two are dx,dy. The callback wrapper
copies arguments into five-byte VM records and enters KC synchronously through
`0x4bb470`; it is not a direct machine call to a fixed KC address.

The subsequent `Input` callback executes at `0x478015`. Only after that,
class tests at `0x478036/0x47803b/0x478040` admit 104/105/103 to `MouseInput`.
Those events carry `(0x118,button,button)`, `(0x119,button,0)`,
`(0x11d,dx,0)` and `(0x11e,dy,0)`. The axis sends are at
`0x478106/0x478146`, with nine caller-cleaned words each. No conversion from
these event codes to the class-150 method was found in this fixed story.
External/legacy classes or other story images remain a separate qualification
question; absence from this CLAS table is not proof the native branches never run.

KC `150::InputMouseDelta` (`0x17b71`, two arguments) calls
`502::MoveCursorRelative` at `0x17b7e`. That is the reviewed cursor path through
`0x17db00 → 0x17dec9`, clamping, cursor positioning, native cursor stores and
listener notifications. It consumes layout-pixel deltas after adaptation.

### Flight/control path and the previously uncovered fallback

When `[owner+0x4d8]!=1`, `0x478150` calls `0x478690`. Its normal path resolves
the active cockpit from `*0x5a82e4`, handle `registry+0x10`; it chooses
`cockpit+0x10` when `cockpit+0x150` has bit 4, otherwise `cockpit+0xc`.
The resulting controlled-object pointer (or zero) is passed in ECX to
`0x4cfd00` at `0x4787c8/0x4787dc`. One caller-cleaned stack word controls
additional processing: 0 for input result 2, otherwise 1 in these callers.
An early input-wait branch at `0x4786c6` and rejected-input return at
`0x478797` can bypass the control worker. Their full user-facing modal meaning
has not been assigned; they are not assumed to emit cursor motion.

`0x4cfd00` aligns ESP to 16 and saves incoming ECX at `[ESP+0x20]` before
pushing ESI/EDI; the same local is `[ESP+0x28]` thereafter. At `0x4d0734`,
EDI receives it. **Null object branches directly at `0x4d073a` to `0x4d2284`.**
Non-null object reaches the normal axis processing, including both
`0x4d1af1/0x4d1b10` readers. Even stack-mode 0 goes to `0x4d1af1`
(`0x4d0dc9`): these readers are not restricted to menus or the optional mode.

`0x4cfa10` takes five callee-cleaned stack words: axis index, controlled object,
signed delta, mapping-bank selector, opaque fifth word. The inspected body
does not consume the fifth word. It reads selector at entry ESP+16 and chooses
`0x5a6c68 + axis*4` when nonzero, `0x5a6c60 + axis*4` otherwise. The caller's
selector local is 1 when its active-cockpit lookup succeeds and `cockpit+0x1d8`
is positive (`0x4d0bd4..0x4d0bf6`), else zero (`0x4d0a47`). Do not infer a
menu/flight Boolean from that field without further semantic evidence.

Mapping values 1..16 enter the dispatch table: 6..8 perform no action and
11..12 only accumulate cursor motion. For example, 1/2/3 call
`0x4ce6f0/0x4ce9c0/0x4cec00`, and 13..16 call `0x4ce570`, whose unprojection
uses the **real-pixel cursor globals**. Values 11..16 also accumulate the
signed delta: 11/13/15 into `0x61c880`, 12/14/16 into `0x61c884`.
Thus scaling the input to this dispatcher would change native controls as
well as cursor movement, and could change the relation between raw-axis and
cursor-derived steering. KC `502::SetMouseMapping` (`0x17e095`) has twelve
`X2_SetControlMatrix` calls: branches configure 11/12, 13/14 with 15/16,
or 1/2 with 4/5 for axis codes `0x200/0x201`. These branches substantiate
multiple control contexts; the field naming is deliberately not guessed.

The null-object block reads the same raw words at `0x4d2289/0x4d22b2`, uses
the **base** mapping bank and the same 11..16 parity-to-accumulator tables.
It bypasses the normal axis readers entirely. Both blocks converge at
`0x4d22d6`; if either accumulated coordinate is nonzero, the worker sends
`150::InputMouseDelta(sumX,sumY)` at `0x4d2306` via `0x4bb500`, then clears
both cells at `0x4d230f/0x4d231e`. The six fixed callback arguments are
0,0,150,name,1,2. The two callback boundaries therefore cover both traced
cursor-producing modes without modifying either raw-reader branch.

| Context established by code | Scaling scope |
| --- | --- |
| Direct owner mode 1, including any current class receiving InputMouseDelta | Scale callback copies at 0x477fda; virtual screen queries and ordinary 0x200 projection remain paired |
| Non-null controlled object with flight mappings | Preserve raw axes; scale only the cursor sum callback at 0x4d22f8; convert KC cursor back to real pixels for native steering/fire |
| Null controlled object | Base-bank accumulation bypasses 0x4d1af1/0x4d1b10; the same 0x4d22f8 callback covers it |
| Legacy classes 103/104/105 | Independent MouseInput events remain unqualified; their actual UI users are not established by this story |
| Script absolute cursor movement/centering | Keep layout coordinates; `502::MoveCursorAbsolute` calls UpdateCursor at 0x17db34, bypassing delta adaptation and still reaching the native cursor boundary |

Controller-generated cursor motion, runtime remapping and arbitrary modded KC
method dispatch are not exhaustively inventoried. This is a native mouse-path
contract, not a claim that every device emits the same deltas. Scale changes
must also rebuild clipping/layout in virtual units; the studied SetClipping
method alone does not prove when every resize or mode change invokes it.

## 2. Camera classification and lifetime

The active cockpit lookup is established independently by `0x4ce57e..0x4ce59d`:
registry `*0x5a82e4`, handle `+0x10`, resolver `0x41e420`, then cockpit `+8`.
Resolver ABI: ECX=registry, `[entry ESP+4]=handle`, EAX=result, `ret 4`.
Registry+0 points to `{bucket_array, bucket_count}`; index is
`handle & (count-1)`. Nodes contain `{next, handle, object}` at +0/+4/+8.
The native walk is unbounded and assumes valid table invariants. A diagnostic
or adapter needs bounded validated reads; this is not a reason to call the
native resolver from an arbitrary thread.

Cockpit initialization `0x421b60` stores a new scene at `+4`
(`0x421b65/0x421b6c`), a new camera at `+8` (`0x421b91/0x421b96`), and attaches
them through `0x4a5860` at `0x421bc1`. Attachment writes `camera+0x1c=scene`
at `0x4a5897`. This establishes the camera/scene pair; it is not a guessed
offset match. The projection routine tests `instance+0x130 & 0x200` at
`0x4de5af`; perspective instances branch to `0x4de96d` and never pass the
proposed scale seam. Screen-relative cockpit overlay calculations read real
display dimensions and the cockpit camera; this pair must remain in real
pixels while ordinary KC menu instances use virtual dimensions/projection.
Other cockpit cameras at +0x58/+0x5c/+0x60 are separately updated in
`0x422cb6..0x422cef`; they must not be equated with +8 merely by ownership.

**Lifetime qualification is still required.** Teardown detaches the +8 camera
at `0x421987`, destroys it at `0x42198f`, destroys the +4 scene at `0x421997`,
then clears **only +4** at `0x42199c` in this tail. Camera destructor
`0x4a4750` can invoke an indirect notification at `0x4a478b` before disposal.
A cached non-null +8 can therefore be stale, and destruction has a concrete
reentrancy route. Revalidation of scene/camera membership, invalidation around
lifecycle changes and the timing of publication relative to rendering must
be established for an FL adapter. A once-per-Present read plus pointer equality
is not proven sufficient. This study establishes active-cockpit ownership,
not completeness for every simultaneous cockpit/monitor or every flight HUD
component. A per-camera classification capture remains useful acceptance evidence.

## 3. Map, overlay and native cursor coordinates

`X2_UpdateCursorSteering` writes script x/y into native globals
`0x5aa180/0x5aa188` at `0x47bc50/0x47bc58`. Convert virtual→real **there**.
Native fire at `0x44ed2d..0x44ed3b` already passes those cells to
`0x426d70`; `0x4ce5b0..0x4ce5bc` passes them to camera unprojection
`0x4a50f0`. Do not scale those consumers a second time.

The independent script calls still need virtual→real conversion:
overlay pick `0x4320cf → 0x42bae0` and cursor aim
`0x4310e0 → 0x426d70`. Overlay this-pointer is **cockpit+0x3bc**;
the hit-test subtracts the real viewport centre computed from display signed
width/height and camera bounds (`0x42bafb..0x42bb61`), then tests the existing
native projected icons. Aim passes explicit nonnegative points through this
hit-test and camera unprojection (`0x426ddc/0x426e3c`); the negative-coordinate
fallback computes its own native centre (`0x426e87` onward).
KC `606::GetObjectAtScreenPos` (`0x18a9b4`) selects overlay or aim at
`0x18a9d6/0x18a9e8`. Scaling native callees themselves would also affect the
already-real fire route. The short-argument -1,-1 aim sentinel bypasses the
explicit-coordinate seam and must remain unchanged.

Galaxy-map callback `0x420179` passes `(column,row,x,y)` to
`WriteGalaxyMapSectorName`. The projected offsets are relative to the map
viewport centre, include the ring-radius label displacement and camera-roll
rotation, and are truncated to integer by SSE at `0x420158/0x42015d`.
Divide these final signed offsets by `s` before KC draws the labels. Pick
`0x431d08 → 0x41f6b0` takes `(scene,x,y,&outX,&outY)`, `ret 0x14`;
its half-viewport calculation reads real display dimensions at
`0x41f6f8/0x41f726`, and its bounds compare x/y at
`0x41f922/0x41f92a/0x41f92f`. Multiply the script offsets by `s` at the
dispatcher boundary. KC `2228::FindMapHoverObject` calls it at `0x1e94ab`
after obtaining `GetScene3D`. Label and pick must be adapted together. This
does not authorize scaling native perspective map geometry or changing its
camera; graph drawing and density remain owned by the separate graph note.

## 4. Concrete hook requirements and preservation

These are candidate contracts, **not an admitted install profile**. The ten
core sites below implement the traced coordinate crossings. The two legacy
MouseInput sites are conditional additional work pending their actual users;
the two raw-axis reader sites are negative witnesses, not recommended hooks.
Each patch must preserve all unlisted register/stack state. Arithmetic flags
may be changed only where the stated continuation kills them before use.

| Purpose; span [start,end) | Coordinate and ABI requirement | Liveness / continuation |
| --- | --- | --- |
| direct delta `0x477fda..0x477fdf` (5) | At entry `[ESP]=1`, `[ESP+4]=2`, `[ESP+8]=dx`, `[ESP+12]=dy`; divide outgoing signed values with fractional carry, replay method-name push | Preserve all GPRs, especially EBX owner and ESI pending input. Flags dead: pushes then callback's `sub esp,0xc`. Name immediate HIGHLOW field **0x477fdb**, value preferred 0x58ba3c |
| mapped delta `0x4d22f8..0x4d22fd` (5) | Same stack slots; scale copies only, leaving native accumulators and flight processing unchanged; replay method-name push | Preserve all GPRs and aligned worker frame; flags dead at `0x4bb500` entry. HIGHLOW field **0x4d22f9**. Callback then native zeroing proceeds normally |
| virtual width `0x4b23db..0x4b23e1` (6), height `0x4b2400..0x4b2406` (6) | Signed word `[EAX+4/+6]` → EAX, divide/round, push EAX then EDI | ECX already VM, EDI return record; preserve them. `0x4c1940` receives ECX and two stack words, `ret 8`; its first compare kills flags |
| projection `0x4de91b..0x4de920` (5) | ESI=P, EDI=instance, EBX=camera, EBP=aligned frame. Apply anchor transform only on 0x200 branch and outside qualified native-HUD exclusion; replay EAX=1 | Preserve GPRs, SIMD and flags conservatively. Suffix does not rewrite P[0/5/12/13]; returns without itself killing flags. Two known callers kill flags with `add esp,8`, but that is not universal caller proof |
| native cursor `0x47bc4d..0x47bc5d` (16) | ECX=5-byte-record array; x at +0xb, y at +0x10. Load/multiply/store each; EAX ends as scaled y | Preserve record/frame and other GPRs. Continuation `0x47b71e` reloads VM and calls flag-killing return helper. HIGHLOW fields **0x47bc51/0x47bc59** target x/y globals |
| overlay `0x4320c3..0x4320cf` (12) | ESI=record, EAX=cockpit. Push scaled y, set ECX=cockpit+0x3bc, push scaled x | Do not mutate shared record. Final stack top x,y; callee `ret 8`. Flags dead at callee's `sub esp,0x14` |
| explicit aim `0x4310c0..0x4310c6` (6) | x→EDX, y→EDI from ESI record, multiply both. EAX=cockpit, ECX=argc | Preserve ECX/ESI/EAX/EBX/EBP; next `cmp ecx,4` regenerates flags. Final call ECX=cockpit, stack x,y,flag, `ret 0xc`; argc<3 sentinel path bypasses seam |
| map label `0x420158..0x420162` (10) | Convert XMM1→integer y/push, XMM4→integer x/push, divide integer offsets with symmetric rounding | Preserve live SIMD and ESI/EDI/EBP/EBX; pushes must leave x,y at stack top. Flags dead at callback's `sub esp,0xc`; do not alter roll/projection intermediates |
| map pick `0x431d01..0x431d08` (7) | ESI record +6=x,+0xb=y; multiply outgoing copies, push y,x,EAX(scene) | Two output pointers already on stack, preserve them and EBP SEH frame. Flags dead at callee's alignment `and esp,-8`; `ret 0x14`. AP's flag-replay seam does not apply |

The legacy X/Y loads return ECX/EAX respectively, with EAX=input context for
the X zero branch; their following `test cx,cx`/`test ax,ax` kills flags.
Raw-axis X/Y return signed EAX, require ESI/EDI and outgoing stack preservation,
and dispatch with `ret 0x14`. Direct branches **target the X reader start**
from `0x4d0dc9/0x4d1a87`; moving a hook before the context reload would miss
those paths. None of this justifies scaling the raw readers.

All ten core spans are whole instructions. The first five bytes fit one aligned
qword at nine core sites; the 16-byte cursor span starts at qword offset 5 and
does **not**. Even for the others, a fitting first-five write does not authorize
non-atomic padding/tail writes. Installation at a proved quiet boundary,
rollback, relocated expected operands and all-or-none coordinate activation
need the actual FL patch implementation's admission checks. Use loaded module
base plus RVA for executable/global/string addresses; only the four HIGHLOW
operand fields listed above occur inside the ten core spans. External helper
addresses and pointer cells also require module-relative resolution.

Use per-invocation temporary storage, never a shared argument frame. Delta
fractional state needs separate X/Y and path identity, reset on scale/mode
changes, and a proved thread/reentrancy policy. Both callback wrappers can run
KC and native work synchronously; no claim of non-reentrancy follows from the
native call graph. Commit carry updates before invoking KC, rather than holding
a lock across callbacks. Wide intermediates and specified rounding/overflow
must be tested for arbitrary signed callback values; mouse source int16 does
not bound all map coordinates or accumulated values. Avoid allocations,
per-event registry walks and logging in arithmetic stubs. A C++ callout adds
stack-realignment, LastError/DF/CPU/SIMD preservation and exception obligations;
inline arithmetic avoids a new callout but not those state contracts.

## 5. Verification and remaining acceptance

[verify_fl_ui_input.py](../../../verification/probe/verify_fl_ui_input.py) uses the
project PE/CAT/KC readers and Capstone. Fixed input SHA-256 identities are the
reviewed EXE `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`
and story `dbb732716e1acab6b8cc7f87f5d31324fb4764dfc22d1cec808749a4f45ec7b9`.
It verifies **74 measured instruction-semantic checks**, dispatch/mode/ABI
witnesses, mapping tables, camera lifecycle,
native consumers, span boundaries from preceding instruction seeds and
HIGHLOW fields. A **measured** linear scan decodes 428,814 instructions;
no direct call/jump enters any of the **14 measured** span interiors. The
three direct edges to starts are retained in JSON. A truncated raw-axis load
is rejected; fixed-story MouseInput absence and the uncleared camera slot are
explicit negative witnesses. Linear decoding is not CFG/indirect-entry proof.

Reproduce without Wine:

```sh
python3 verification/probe/verify_fl_ui_input.py --raw
python3 -m py_compile verification/probe/verify_fl_ui_input.py
```

Local, ignored summary/listings and a reproduction wrapper live in
`verification/results/fl-port/ui-input/`. No raw resources/listings are tracked.
Outstanding bounded evidence: actual users of legacy classes 103/104/105;
camera publication/invalidation and simultaneous-monitor exclusion; indirect
entries and installer atomicity; callback/thread/exception admission; and user
acceptance in flight steering modes, no-controlled-object menus, graphs,
galaxy-map labels/picking, native overlay clicks, scale changes, Reset and
alt-tab cursor behavior. Native Windows and CrossOver execution remain separate
unverified targets here. These gaps do not require whole-engine reconstruction,
but they prevent claiming that static hook requirements implement or qualify
full FL UI scaling.

## Independent review

Bounded deep review found no blocking issue and corrected the mapping-table
wording above: 6..8 are no-ops, 11..12 only accumulate cursor motion. The reviewer
reproduced 74 semantic checks, 14 spans, 428,814 linear instructions, zero direct
interior edges and three edges to span starts. Targeted inspection confirmed
stack copies, coordinate conversions, flags/cleanup, four relocation operands
and the camera destruction limits. Compilation and documentation links passed.
This remains static contract evidence; no arithmetic stub or runtime admission
is qualified by these checks.
