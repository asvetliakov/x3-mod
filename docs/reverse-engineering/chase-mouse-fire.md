# Chase camera: cursor fire admission, cone and finite aim

2026-09-13, read-only Ghidra analysis of the installed X3AP.exe, preferred base
`0x00400000`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
No game or Wine process was started. Raw listings and extracted game assets
remain local under `/tmp/x3-camera-study/`; only derived findings are recorded.

The user confirms right-mouse cursor fire works in first person with the same
setup, but chase view shoots straight. This is an external/chase-path symptom,
not evidence that the user selected the boresight-fire binding. The corrected
camera run's first-applied sample records mode 258, connect 0, flags `+0x1a0=0`,
aim-gun index 0 and matching reference/view-object identities. That sample does
not establish the input flags or cursor state when firing.

## Actual call and input paths

The four direct callers of fire control `0x00445170` are in engine command
execution `0x00460630`: `0x00461b04`, `0x00461b15`, `0x00461b54` and
`0x00461b66`. They pass the firing flags from a command argument. The earlier
claim of a direct call from `0x00416750` is incorrect. Static direct callers do
not establish whether a shot consumes the last displayed pose, a newly rebuilt
vanilla basis, or a chase pose; correlated runtime camera/fire events are needed.

Cursor state is also supplied by an engine command. `X2_UpdateCursorSteering`
is pointer-table entry `0x0057c96c` (string at `0x0054d130`) and case `0x1f`
of `0x00406de0`. Its parameters at `+1/+6/+0xb/+0x10` are written to
`0x00607c64` (steering state), `0x00607ce8` (cursor-fire active),
`0x00607cec` (x) and `0x00607cf0` (y). The active write at `0x004074de`
is the only direct write reference to that global. Thus the former description
of this as simply the mouse-input handler hid a script-controlled gate.

The latest archive `addon/04.cat:L/x3story.obj` was extracted locally to check
whether a script transition could be recovered directly. It has compiled CODE,
SYMB and CLAS sections; no readable cursor/fire function names were found.
No claim about a particular KC instruction or named source function follows
from that check. CODE and the numeric SYMB/CLAS records do not directly expose
source names or the four argument expressions. Runtime evidence below locates
the zero before native command dispatch without requiring a guessed bytecode
patch.

## Cursor branch admission

The branch beginning `0x00445a15` requires all of these:

- Fire argument byte has both `0x2` and `0x20` set.
- Cursor-active `*0x00607ce8 != 0`.
- Registry `*0x00608504` is nonnull; its handle at `+0x10` resolves through
  `0x0041cd20` to a cockpit.
- That cockpit's reference-view object `+0x10` equals the firing ship.
- Aim-gun index `cockpit+0x1d8` is nonnegative and below the ship's gun-view
  count (`0x00450e70`).
- Mapping that view index through `0x00450f50` equals the firing gun-group
  argument (0 for main guns).

There is no `cockpit+0x150 == 1` view-mode test in this branch. Other routines
have internal-view tests for turret handling and UI; those tests do not prove
that main-gun cursor fire is deliberately disabled externally. Likewise,
aim-gun 0 in one camera sample does not prove every gate above remains open
at each shot. The [third gameplay run](../verification/chase-third-run.md) now distinguishes
the failing gate: the engine command receives active=0 externally while its
cursor coordinates continue to update. The read-only camera/trace hooks do
not write that global. The precise compiled KC condition producing zero has
not been decoded; native engine dispatch itself performs no view test here.

`0x004257f0` checks whether the cursor lies within a roughly 35-pixel box around
the cockpit's stored aim icon (`+0x6ec/+0x6f0`, nonnegative). If so, the routine
can use tracked object `+0x1e0`; otherwise `0x00425410` picks through overlay
icons. This target selection affects range and convergence, independently of
cursor-branch admission.

## Range, cone and position

The initial cursor-ray depth comes from `0x00433e50`: bullet-type fields at
`+0x50` and `+0x54` multiply and divide by 1000 (speed/lifetime range). With a
picked target, it limits this depth by gun-group-origin-to-target distance,
then uses target size `+0xa4 / 4` as a floor bounded by the bullet range.
An absolute minimum of 5000 engine position units follows at `0x00445b25`.
These are engine position units, not an established metre conversion.

At `0x00445b4d`, `0x00489780` unprojects the cursor through sector-camera FOV
and viewport. Gun group 0 multiplies this vector by cockpit `+0xf0` at
`0x00445b63`. The cone is tested **after** that multiply:
`ray.z >= length(ray) * cos(max_angle)`. When outside, the routine restores
z to the chosen depth and rescales x/y to lie on the cone boundary. It does
not replace every out-of-cone ray with a straight vector.

Initialization `0x004333a0` reads tuning ID `0x5f`, default 30 degrees,
clamped to 0..89, converts it to binary angle at `0x00587b8c`, and computes
cosine `0x00587b88` and tangent `0x00587b90`. The installed
`addon/02.cat:addon/types/Globals.pck` explicitly sets
`SG_CURSORSTEERING_MAXFIREANGLE` to 30. Runtime globals should still be logged
because an archive setting is not a measurement of process state. The previous
18.65-degree centre-camera pitch, and proposed 20-degree downward pitch, each
fit this 30-degree cone; pitch alone therefore does not explain all cursor
positions producing straight fire.

After the cone, the vector passes through gun-angle, gun-mount and ship-node
basis matrices at `0x00445c48`, `0x00445c50`, `0x00445c5e`. The routine adds
the gun-group world origin at `0x00445c72`, producing an endpoint. That origin
is built from the gun-group record `+0x10` and ship-node `+0x40/+0x30` at
`0x00445997..0x004459bc`. It is **not** the sector-camera translation.
Final aiming subtracts the individual muzzle world position from that endpoint
and normalizes; the chosen world direction is at `[EBP-0x50]` on arrival at
`0x0044605a`. The following `0x004f0c00` converts it back through the ship basis
for projectile state.

Consequently writing camera basis and `+0xf0` together preserves angular input
consistency only. A camera boom requires finite camera-to-muzzle convergence
if cursor rays are to intersect the same visible point. That is a separate
correction from admission. Any future correction should preserve native range,
cone and gun-group constraints, and must validate the intermediate coordinate
frames and world origin with observed data first.

## Third-run finding and proposed scoped correction

The third run has 229 first-person cursor admissions and 180 external gate-3
rejections; every retained firing event has flags `0x2a`, a coherent player
cockpit/camera, and valid main-gun mapping. The linked writer coordinates equal
the firing coordinates even externally. All sampled external writer inputs
have active=0. Thus absent mouse coordinates, boresight input, bad camera
identity and the 30-degree cone are excluded as explanations for this run.
The cone code is not reached by those 180 external calls.

A fresh read of `0x4074cd..0x4074fa` confirms that the native command only
copies four pre-evaluated KC values: steering, cursor-fire active, x and y.
There is no native view-mode branch or host mouse-button query between the
argument load and global store. The sole direct read of cursor-active is the
fire admission comparison. This supports a deliberate chase extension to the
fire consumer; it does not justify globally declaring cursor steering active.

**Proposed implementation, not yet implemented:** claim the six-byte
`JZ 0x445cab` at `0x445a41`, bytes `0f 84 64 02 00 00`, immediately after the
original `CMP [0x607ce8],0`. The complete fire-function disassembly places
both ends on instruction boundaries and has no direct branch into the span.
Use the existing relocation field at offset 2 to retain the original JZ target.
An ABI-preserving callback may clear only saved EFLAGS.ZF when all of the
following hold; otherwise its saved flags remain byte-for-byte untouched:

- Chase installation succeeded and a recent active-player camera update
  actually applied the chase pose. This witness must exist without telemetry.
- Live resolved cockpit, ship and sector camera exactly match that witness;
  live mode is the observed external mode 258, connect mode is zero, and the
  current view has not switched or failed its camera update. Use the existing
  applied-context identity/age discipline, with a reported fail-closed refusal.
- The fire call refers to that player ship and main-gun group 0, and its native
  flags retain both fire bit `0x2` and cursor-fire bit `0x20`. Those checks already
  dominate this JZ, but rechecking them makes the extension's contract explicit.
- Native cursor-active is zero and the engine-provided cursor coordinates are
  readable/in the current viewport. Retain those coordinates unchanged.

This changes one admission decision for a genuine native cursor-fire command.
It does not synthesize a mouse press, change the cursor writer/steering state,
poll OS mouse input, change fire flags, or affect first-person/other external
views, NPCs or turret groups. All downstream native cockpit resolution,
aim-index/gun-group mapping, target picking, bullet range and cone checks still
execute. The next native flag-writing instruction overwrites the comparison flags
(`TEST EAX,EAX` after the registry load), so the intentional ZF change is local to this branch.

The existing entry trace precedes this decision and should continue reporting
native active=0/gate3 honestly; downstream ray callbacks plus a small extension
admission counter identify the effective override. Relabeling the original
input as active would erase the causal evidence. A focused branch fixture must
exercise original taken/fallthrough and authorized override paths, unchanged
non-ZF CPU state, failed identity/view checks, and relative-JZ relocation.

This bounded admission change addresses the observed straight-fire symptom.
It does not establish camera-to-muzzle finite convergence or impact accuracy.
The native endpoint still starts at the gun-group origin; a later geometric
correction needs its own coordinate-frame derivation. Do not silently combine
an unvalidated camera-origin shift with this proven input-policy correction.

## Consolidated diagnostic implementation

The optional `chase_aim_trace` module uses four observation sites, enabled only
when chase installed successfully and telemetry is enabled. It makes no
weapon/input mutation. Source/host verification is available; runtime fixture
qualification and gameplay interpretation are separate steps. Exact bytes below were cross-checked against the file;
Ghidra's full fire-function instruction listing has no direct branch into any
span interior. The original prologue uses EBX as its argument-frame anchor and
realigns the separate EBP frame; ordinary C EBP argument assumptions are wrong.

| Site | Whole displaced instructions, bytes | Information available before replay |
| --- | --- | --- |
| `0x00445a15`, 5 bytes | `mov eax,[ebx+0x14]; mov edi,eax`, `8b43148bf8` | Original argument frame: ship `[EBX+8]`, target `+0xc`, gun group `+0x10`, flags `+0x14`, weapon mask `+0x18`; admission globals/cockpit state |
| `0x00445b70`, 6 bytes | `mov [ebp-0x1c],eax; mov eax,[ebp-0x1c]`, `8945e48b45e4` | Admitted ray `[EBP-0x30]`, its length EAX, selected depth `[EBP-0x14]`, cone globals, group origin `[EBP-0x130]` |
| `0x004074de`, 6 bytes | `mov [0x00607ce8],ecx`, `890de87c6000` | The sole cursor-active writer: ECX active, EDX x, EAX y; the observer runs before the original write and records supplied inputs |
| `0x0044605a`, 6 bytes | `mov edx,[ebx+8]; mov ecx,[edx+0x70]`, `8b53088b4a70` | Chosen world direction `[EBP-0x50]`, muzzle world position `[EBP-0xf0]`, cursor endpoint `[EBP-0x100]` only when cursor branch ran, cursor marker `[EBP-0x74]` |

All spans have no relative instruction. The byte-by-byte heuristic scanner
reports an apparent pre-clamp interior target from `0x00445b9a`, but this is
inside the immediate/displacement bytes of a real instruction at `0x00445b99`;
it is not an actual branch. The full instruction listing resolves this alias.

Record a monotonically increasing camera-event sequence for every active
cockpit visit, including internal pass-through, plus the last applied pose
state. Pair firing observations by argument-frame/frame pointers and an event
serial. Compare actual `+0xf0`, camera basis/position and ship basis to the
recorded pose at the fire event; do not assume fixed loop ordering. A bounded
player-only sample window should keep representative event records and count
omitted events. Aggregate output belongs on the existing report cadence; no
per-fire logging or heap allocations. Preserve flags, integer/SIMD/x87 state,
LastError and the four-byte incoming-stack ABI exactly as the camera hook does.

One user-managed run with first-person and chase cursor sweeps, allowing a
report window at each cursor region, can then resolve
which gate differs, whether the cursor moves in the script globals, whether
`+0xf0` still represents the displayed pose at firing, whether the cone clamps,
and whether the final muzzle direction changes. Existing logs lack these
fields. Forcing the cursor-active global, setting fire flags, disabling the
cone or changing gun state without that evidence is not justified.

## Local reproduction evidence

`chase-fire1.txt` covers direct fire callers, aim helpers and gun-view mapping;
`chase-fire2.txt` covers input/turret code and range;
`chase-fire3.txt` covers cursor-global references and cone initialization;
`chase-fire4.txt` covers command dispatch and the world endpoint add;
`chase-fire5.txt` contains the complete fire instruction listing and registry
resolver; `chase-fire-writer.txt` contains the complete cursor command listing. Each was generated through `X3CameraState.java` using the existing
`/tmp/x3-ghidra-research X3Render` project with `-noanalysis -readOnly`.

## Bounded records, lifetime and verification limits

Loader initialization follows a successful camera install; the four game spans
are claimed before `engine_patch` closes its install window. Any partial failure
attempts to restore every claimed span and leaves diagnostic collection disabled.
A restoration failure is logged explicitly; a leftover observer still returns
without touching game state. Successful sites remain for the process lifetime.
No additional export or backend-private mechanism is required.

Every accepted active cockpit update records its handler-frame number, monotonic
sequence, QPC, view mode, applied/pass-through state and pose snapshot. Internal
view remains observable while the chase configuration is selected. An unreadable
cockpit invalidates a previous context only if its address matches the known
current cockpit; an unreadable unrelated monitor does not erase player context.
A known active cockpit whose required reads fail invalidates the context.

Fire events use a player-address filter before expensive reads and locking.
Four thread slots correlate entry, admitted ray and barrel outputs by thread,
EBX/EBP and a monotonically increasing entry serial. Every new accepted entry
invalidates the prior slot before reads or capacity checks. Eight first events
are retained per report; further entries increment omitted counts, and their
follow-up phases cannot append to an older event. The admitted ray and final
phase totals count all filtered observations; the cone-clamp count is over
retained ray samples. First/latest final directions and a barrel count belong
to one entry because later barrels bypass the shared gun-group admission code.
Reports invalidate all pending retained-slot references before releasing the lock.

Cursor-writer observations are global and therefore have no player-address
filter. They count writes, active/inactive writes, state changes and coordinate
changes, with first/latest samples for unknown, internal and external view.
Each record has thread, QPC and camera sequence; every retained fire event links
to the latest writer sample. This distinguishes no script update from an update
that explicitly disables cursor fire without another instrumented load.

All engine reads use bounded ranges and explicit validity masks. Pose records
include camera position/basis, relative basis, ship-node position/basis, FOV,
viewport, camera/default view plane and screen size. Gate 9 labels unavailable,
incoherent or older-than-two-seconds pose context (reported QPC age); that limit
is diagnostic only and does not affect native fire. Validity flags distinguish
unreadable zero-initialized storage from observed zero values.

There is no per-draw work or event allocation. Accepted player fire and the
cursor writer take one diagnostic lock; pose snapshots use the existing bounded
engine reader. Logs are emitted after releasing that lock on the existing
Present report cadence. `handler_ticks` includes accepted player-fire and cursor-writer observation
work after the first QPC, including lock wait. It excludes the stub, full CPU
boundary and first QPC; for fire observations it also excludes the early
AI/player filter. The global cursor writer has no player filter. These timings are diagnostic and are not total hook
cost or measured game FPS. The camera handler's existing inclusive timing also
includes its context-snapshot callback when this diagnostic is enabled.

The trace translation unit is compiled with `-fno-exceptions`, because MinGW
SJLJ registration before an RAII CPU save and unregistration after restore would
sit outside the injected boundary. The generated stub saves EFLAGS, GPRs and
XMM0–7; `PreserveCpuState` saves x87/MXCSR/LastError, and local arithmetic starts
with masked round-to-nearest state. `audit_object` in the fixture runner rejects
exception-runtime references and checks the callback's save/init/restore and
GetLastError/SetLastError instruction envelope. The camera callback's separate
build correction is tracked by the orchestrator, not established by this trace.

`verify_chase_aim_sites.py` validates the installed executable, exact C++ specs,
whole decoded instructions and decoded direct-branch targets. Eleven host tests
exercise corruption, boundaries, interior branches, source drift and the known
opcode-byte alias. The x86 `chase_aim_trace_fixture.cpp` reserves a zero-filled
PE data section at `0x401000..0x620000`, with its real code starting at
`0x630000`. The PE loader owns this section before Wine establishes process
heaps and mappings: late broad and targeted `VirtualAlloc` attempts collided
with existing private/mapped allocations and were rejected before any hook ran.
The fixture keeps the production site addresses unchanged and initializes
synthetic copies of the four short spans and executes the actual generated production stubs, with
native-outcome comparisons for GPR/flags, XMM, live x87 stack/control/status,
MXCSR and LastError. It also contains admission, identity/age, correlation,
capacity/report reset, writer input, bounds and partial-install rollback controls.
Before execution, the runner verifies the fixed x86 image base, disabled ASLR,
exact zero-filled/non-executable synthetic section, required site/global ranges,
entry location, section non-overlap and image extent. Runtime ownership checks
require each hook site to belong to the loaded fixture image, then make only
the hook pages executable. The six modes cover normal operation, rejection at
each of the four sites, and a closed installation window. The result summary
binds source and binary hashes before and after execution and rejects changes.
Cross-compilation is not execution: the coordinated X3-only runtime summary
establishes fixture behavior; gameplay and native Windows remain untested.
