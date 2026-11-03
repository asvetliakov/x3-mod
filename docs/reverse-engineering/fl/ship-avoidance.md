# FL ship avoidance: snapshot inputs and reactive-warn site E

2026-10-29, read-only study from source checkpoint `30957b41`. Selected group:
**`X3M_SHIP_AVOIDANCE`**, the existing lazy sector snapshot / target adjustment
at base site A and separately claimed suppression site E. The next adapter can
reuse that policy. This study adds no production code or activation, and does
not admit the gate-stop, gate-launch, traffic, lane or docking-approach groups.

The [base steering contract](ship-steering.md) owns the three
leaf entries, B clamp, clock/getter bindings and common object fields. Existing
AP behavior is documented in [steering architecture](../../architecture/ship-steering-and-avoidance.md),
its ratified amendments and the current source; the original §3 design prose
contains superseded capacities and station geometry. This note follows the
current sphere-part reader, not the older proposed oriented-box path.

## Evidence and reproducibility

Clean originals, measured: AP `/mnt/c/X3AP-orig/X3AP.exe`, 2,153,984 bytes,
SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`;
FL `/mnt/c/X3FL-orig/X3FL.exe`, 2,804,736 bytes,
SHA-256 `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
These hashes are provenance; they are not edition admission gates.

Run the bounded [producer](../../../verification/results/fl-ship-avoidance/produce.py):

```sh
python3 verification/results/fl-ship-avoidance/produce.py
```

The [derived result](../../../verification/results/fl-ship-avoidance/contract.json)
records AP/FL hook references and returns, 93 frozen instruction-semantic
anchors, decoded range hashes/counts, eight rejected mutations and a positive
FL relocation normalization at base +`0x130000`. All anchors must also be starts
of instructions in independently decoded named ranges. No raw native body or
listing is emitted. Encoded rel32 scans cover `.text`; abs32 scans cover every
backed section. Short-branch checks cover the complete warn and immediately preceding mesh
functions, which contain all native code within rel8 reach of either entry.
Computed targets and foreign modifications remain outside this census.

Ghidra 12.1.4 corroborated eight FL function extents, warning callers and
pseudocode, using the existing `FLRegistry/X3FL.exe` project read-only, without
analysis or project mutation. The bounded [script](../../../verification/results/fl-ship-avoidance/Avoidance.java)
accepts a local output pathname. Raw output/logs remain outside the worktree at
`~/.cache/x3m-port/fl-steering-avoidance-local/`. For example, run headless with
`-readOnly -noanalysis -postScript Avoidance.java /tmp/x3-fl-avoidance.local.c`.
No EXE was executed, built or installed; no native/Wine fixture or game ran.

All following VAs use preferred base `0x400000`; FL production bindings must use
the already admitted loaded base plus RVA. Existing base admission and its
portable Windows CPU/patch services remain applicable.

## 1. Exact E boundary and ABI

| Contract | AP | FL |
| --- | --- | --- |
| Native warn body, exclusive end | `0x44dd90..0x44e445` | **`0x457600..0x457ccb`** |
| Entry span | six bytes, lengths 1+2+3 | same six-byte span |
| Short hook bytes | `55 8b ec 83 e4 f0` | same |
| Replay | `push ebp; mov ebp,esp; and esp,-16` | same |
| Native tail continuation | `0x44dd96` | **`0x457606`** |
| A | `[entry ESP+4]` | **ECX** |
| B / distance / output | `[entry ESP+8/+12/+16]` | **`[entry ESP+4/+8/+12]`** |
| Result | integer EAX, false=0 | same |
| Cleanup | `ret 0x10` | **`ret 0x0c`** |

FL's native return instructions are `0x45773c`, `0x45776f`, `0x457ae1`,
`0x457c39`, `0x457cc8`; all pop 12 bytes. AP has four returns, all popping
16. The prologue contains no address operand or control transfer: plain replay,
`rel32_offset=0`. Both entry spans have zero interior encoded references and
zero absolute entry pointers. Exactly three encoded calls target each entry,
and Ghidra's FL references agree.

Incoming arithmetic flags are dead: `and esp,-16` overwrites them after only
push/mov. On pass-through restore the original GPR/XMM input image and exact
entry stack, then run the claim tail. On a suppression return EAX=0 and
`ret 12` for FL, without writing through output. Preserve EBX/EBP/ESI/EDI/ESP;
restoring incoming ECX/EDX is harmless and maintains the existing stub shape.
Every caller tests EAX immediately, so incoming/outgoing arithmetic flags are
not an E result. Native nonvolatile saves and all return epilogues are in the
recorded complete warn body.

The current AP save envelope occupies 140 bytes: saved XMM0–7 then EDX/ECX/EAX.
For a corresponding FL entry image, A is saved ECX at **`frame+0x84`**, B is
**`frame+0x90`**, distance `+0x94`, output `+0x98`, return `+0x8c`.
To call the existing cdecl `(A,B)` helper, a FL thunk can push B from
`[esp+0x90]`, then A from `[esp+0x88]` after that first push. These offsets
are arithmetic consequences of the existing envelope, not assembled-fixture
qualification. The AP thunk's two `[esp+0x94]` pushes and `ret 16` cannot be
reused unchanged.

## 2. Callers, output and native warning behavior

| Caller | AP call | FL call | FL result / false path |
| --- | --- | --- | --- |
| Proximity normal | `0x44e522` | `0x457db5` | `test eax,eax` `0x457dba`; false branches `0x457dbc -> 0x457e08` |
| Proximity order-`0x12` refresh | `0x44e540` | `0x457dd0` | test `0x457dd5`; false `0x457dd7 -> 0x457e08` |
| Native avoid order | `0x456c02` | `0x460581` | test `0x460586`; false `0x460588 -> 0x4609cd` |

FL proximity body `0x457cd0..0x457e10` receives A in ECX, B/distance on the
stack, and initializes its output local to zero at `0x457cd6`. It supplies that
local's address and never reads the local on either true or false path; a true
verdict appends the supplied B and distance. It reloads the flight pointer and
count after the call; ECX/EDX values returned by E are not consumed.
The avoid caller supplies A=EBX and B=ESI, reads output `[esp+0x80]` only on the
true path at `0x46058e`, then checks it for null. Its false path skips that
read. ECX/EDX are overwritten before subsequent native work. Therefore a false
short-circuit needs no output store at any of the three callers.

The native warn itself has early true paths which do not fill output, such as
`0x457732` and `0x457765`. Its mesh branches forward `[EBP+0x10]` at
`0x457ac8`, `0x457c19`, `0x457cab` to native mesh warn
**`0x4572c0..0x4575fb`** (AP `0x44d9c0`). That mesh routine remains native.
The new helper must not invoke it as a predictive narrow phase: its recursion
and collision services are not a new reentrancy capability.

A native true normal verdict ORs A flags `+0x40` with `0x1000`
(`0x457dbe`). Both true callers append while flight count `+0x1e0 < 5`:
B into **`+0x1e4 + 4*n`** (`0x457de7`), supplied centre distance into
**`+0x1f8 + 4*n`** (`0x457df1`), OR `0x2000` (`0x457dfb`), increment count
(`0x457e02`). All these layouts and meanings match AP's owning contract.
E returning false bypasses those native writes for the pair, as the existing
feature intends; an unresolved pair runs the original test unchanged.

## 3. Snapshot, native masks and station part layout

| Required input | Established FL evidence |
| --- | --- |
| Sector table pointer | `[sector+0x50]`, `0x466e10`; helper `0x457e2b` |
| Heads / stride | `[table+0xc*class]`; `0x457e28..0x457e2e`, L0 stride `0x466e49` |
| Object next / sentinel | next `[obj]`; continue only while `[node] != 0`, `0x466e42..47`; bucket helper `0x457e1f`, `0x457e35` |
| Object's sector | `obj+0x54`, independently consumed by bucket helper `0x457e1a` |
| Obstacle classes | existing six policy buckets 5/6/7/17/18/28; native E explicitly tests stations 5/6 at `0x457639/3f`, asteroid 17 at `0x457705`; ship/gate and shared class fields are in the base contract |
| Pair no-collision bit | **flags2 `obj+0x44 & 0x20000`**, both partners `0x466f16..1f`, `0x466feb..ff4` |
| Full native pair flags | **`obj+0x40 & 0x08200404`**, both partners `0x466f00`, `0x466fd1` |
| Warn reset | L0 `0x466e20` clears `+0x40` with `0xff9fcfff`, including `0x3000`; ships' `f+0x1e0=0` at `0x466e38` |
| Position, velocity, slip | base bindings establish phys position `+0x30..38` and obj slip `+0x60..68`; independently inspected integrator `0x464020..0x464054` combines phys velocity `+0x100..108` with that slip (details below) |
| Object collision radius | `obj+0xa4`; independently inspected warn reads B at `0x457694` and A at `0x45769a` |
| Capital record mask | native `0x4496e0` reads class-7 TShips `+0x14`; table `0x44971c` derives **`0x211d`**, matching shared policy |
| Station root | `obj+0x70`, the same scene/physics node used by native geometry and the base contract |
| Children / offsets / basis | head `node+0xc`, sibling `[child]`, zero-first-dword sentinel; child local `+0x30/+0x34/+0x38`, basis rows `+0x40`, stride `0x10` |
| Own / cached subtree radius | `+0xa0 / +0xa4`, getter `0x4a39b0` |
| Parent scale / scaled-child bit | parent 16.16 `+0x70`; child **`+0x12c & 0x40`**, `0x4a39ec..0x4a39fb` |

The velocity and object-radius references above were independently inspected
in the original FL image during review; they are not additional frozen producer
anchors. In the integrator, `0x464020` loads phys from `[obj+0x70]` and
`0x464023` forms the slip address `obj+0x60`. The x sum reads phys `+0x100`
at `0x464032` and adds slip at `0x464038`; y reads slip at `0x464041` and
adds phys `+0x104` at `0x464044`; z reads slip at `0x464051` and adds phys
`+0x108` at `0x464054`. The shared snapshot reader adds velocity/slip in
int64, then saturates to int32 before SSE conversion.

The full native collision pass is `0x466de0..0x467e9b`. Its early filters retain
AP's pair masks. The snapshot intentionally uses the existing policy's
`deleted 0x08000000 / attached 0x200 / docked +0xf8 / flags2 0x20000`
filters; that is a narrower selection rule than copying every native mask.
This study does not introduce a new filter or claim that the two masks equal.
The native bucket helper `0x457e10..0x457e3f` takes sector in ECX, class and
optional previous object on the stack, `ret 8`; snapshot reads it directly and
does not call the helper. Names for the six class codes remain the inherited
AP/X3 policy names; this is not an FL asset population census.

FL cached subtree-radius getter **`0x4a39b0..0x4a3ad3`** receives node in ECX
and pops an additional unused stack word (`ret 4`). It returns `+0xa4`
immediately when nonnegative. Otherwise it seeds from own `+0xa0`, visits the
children and recursively obtains each child's radius. Under child bit `0x40`,
it scales child offsets by parent's `+0x70` with signed 64-bit multiply,
`+0x8000` and shift 16; otherwise offsets are unscaled. Each axis expands the
radius with `offset+child_radius` and `child_radius-offset`. It stores the
result at `0x4a3ac1`. This is the same per-axis metric the shared reader
replicates; retain its existing Euclidean validation slack.

Uniform-scale setter `0x4a3920..0x4a39ac` writes `+0x70`, resets axis scales
`+0x80/+0x84/+0x88` to `0x10000`, sets own `+0xa0`, and marks cache `+0xa4=-1`.
Axis-scale setter `0x4a3ae0..0x4a3b73` computes own radius from max axis times
uniform scale and marks cache dirty; it recursively updates children flagged
`0x40`. These producer/consumer contracts support the existing read-only
replication. **Do not call the native getter from the snapshot**, fill its
cache, or broaden this into gate-tree/hull-width admission.

## 4. Borrow, liveness, reentry and exception limits

Native sector driver `0x443b10` calls collision at `0x443b42`, command/steering
at `0x443b49`, then later sector integration. Therefore E in collision can see
the module's previous steering decision; A's first call constructs the new
snapshot after that collision pass. Preserve existing game-time freshness
windows rather than treating E as necessarily after A in the same frame.
The E order-`0x12` rescore also runs within the command pass.

At E, A/B are current native caller objects. Their flight/phys/type data are
borrowed synchronously; the injected helper reads them and copied module state,
and must introduce no native callback, wait, allocation or lock. At A, the
snapshot copies sector-list and node-graph fields within the existing admitted
simulation-thread scope, with no engine call while native pointers are live.
The current feature retains object addresses as scalar lookup identities,
not target-buffer pointers or leases. Snapshot contents are copied scalar
values; the target rewrite uses the existing four-slot module ring, preserving
the caller buffer and order point. FL's already qualified base boundary maps
its target redirection to the stack argument rather than EDX.

The selected warn/proximity/radius bodies have no indirect call or FS-based
SEH frame; FL's warn body itself has zero x87 instructions. This does **not**
make the entire native collision pass callback-free: it can issue KC/game
notifications and object updates. The new policy does not enter that pass or
native mesh queries. Keep the existing LastError/MXCSR guard, save XMM0–7,
leave x87 untouched, clear DF for compiled helper entry, realign C++ stack
from the four-byte incoming contract, and audit the built transitive helper
for x87. A foreign exception unwinding across a naked thunk is not a normal
false return and must not publish suppression state. Normal-path byte/ABI
checks do not qualify abnormal unwind or cross-thread invocation.

Two existing state assumptions need explicit implementation handling, not a
claim of universal lifetime proof. `ShipAvoid::resolved[]` stores B pointers,
not B native IDs; `PartCache` keys station pointer, root-radius bits and
position/basis stamp, not an owned node generation. The base FL adaptation
resets `core::Ship` when `table_id_` changes, but does not yet clear the parallel
avoidance state. Before enabling E, ensure an old A decision cannot authorize a
recycled A, and clear its avoidance state on that reset. Test B pointer reuse
within the freshness window and station/root replacement under the part-cache
key; current static evidence does not establish that either is impossible.
Membership memory already compares both current native IDs. Reuse known FL
simulation IDs/owned services if needed; no new generic lifetime machinery is
implied. These are source/contract limits, without a captured runtime failure.

## 5. Small implementation boundary and acceptance inputs

Retain the existing option behavior: avoidance needs the base, unset/empty means
on, `0` disables, invalid text refuses. E is separately byte-checked/claimed;
if it refuses, target adjustment may run while the original reactive loop stays
active. Publish `warn_live_` only after the correct edition thunk is chained;
a failed rollback must remain pass-through and retryable at shutdown. The
existing DLL pin and install-window constraints still apply.

At `30957b41`, opening the whole AP-only optional initializer block for FL
would activate unqualified later groups. Admit avoidance alone and leave the
other groups behind their existing qualification boundaries. Concrete inputs:

- Add FL E RVA **`0x57600`**, length six, stack cleanup **12**, and the thunk's
  A=ECX/B=stack normalization to the existing feature binding/install path.
- Pass the already bound **`native_.table`** explicitly into snapshot record
  reads. `ship_avoidance_walk_core.h::fill_snapshot` still embeds AP
  `kShipTypeTableSlot=0x606fd4`; `RawMem` is an ordinary reader and does not
  remap it. An address-translating reader is unnecessary.
- Replace AP-only TShips reads in `ship_steering_warn.cpp`'s capital/small/member
  verdict and warning/trace rows with the existing edition binding. The optional
  warn watcher in `_clamp.cpp` also uses `sites::kShipTypeTableSlot` when tracing;
  it must stay off until its reads are bound. Base `_decide.cpp` already uses
  `native_.table` and the correct clock. No new getter is required.
- Preserve current policy for resolved pairs, small noncapital partners and
  formation members, including freshness/ID checks and native fallback.
  Gate/dock/controller verdict branches remain inactive in this group.

Current source budgets (measured constants, not native maxima): four sector
snapshot slots; 2,048 obstacles per snapshot, 8,192 visited objects; 64 station
models, 64 spheres per model; 256 visited station children, radius recursion
depth three and budget 64; eight nearest candidates and eight resolved pairs.
One snapshot per sector/game-time epoch amortizes the walk. Station hits hash
position/basis and use fixed part-cache storage; no per-call allocation or lock.
E adds bounded ship-table/member lookups and resolved-list scans, not a sector
walk or mesh query. Keep signature/layout validation cold and formatting on the
existing frame path. The new native seam adds one thunk/argument normalization;
SSE save/restore traffic and five native warn return alternatives are static
facts, not dynamic cost measurements. No FL timing/FPS claim is made.

Minimum next acceptance: a focused synthetic fixture must cover A detour stack
redirection without caller/order-point mutation; FL E hit/miss/off/refusal with
all three native call shapes; output untouched on false; `ret 12`; complete
GPR/XMM/MXCSR/LastError/ESP preservation; rebase and failed install/restore paths;
snapshot table binding and scaled/dirty station children; native-list sentinel
and overflow fallback; pointer-reuse/lifetime cases above. Reuse unchanged base
and shared policy evidence. Actual runtime ordering, thread behavior, station
population, hot costs and user FL flight remain unmeasured.

## 6. FL docking inputs against the current AP policy

2026-10-30, source checkpoint `58f0ab17`, bounded read-only extension. The
owning code is `ship_dock_glue.h`, `ship_avoidance_core.h::dock_container`,
`ship_steering_decide.cpp`, `_warn.cpp`, `_clamp.cpp` and `_install.cpp` under
`src/engine/ap/`. The present AP group defaults on, requires avoidance, runs
its own-container verdict at the already qualified E boundary, and keeps the
container's existing sphere parts in avoidance at A. It also uses carrier
velocity in the order-4 speed reference and B's live-point ramp. It adds no
hook. At this checkpoint FL explicitly sets docking off with
`dock_state_="unqualified_fl"` in the initializer's edition branch. None of
the findings here activates it or qualifies traffic/arrival behavior.

Run the [operand producer](../../../verification/results/fl-docking-inputs/produce.py)
with `python3 verification/results/fl-docking-inputs/produce.py`. The compact
[receipt](../../../verification/results/fl-docking-inputs/contract.json) records
**62 measured instruction anchors**, **four gap-free decoded ranges** and
**three SA table entries** from the same original FL image identified above.
Anchors are checked against instruction starts decoded from the enclosing
command pass, integrator and SA dispatcher entries, rather than decoded anew
at each anchor. Ghidra 12.1.4 corroborated the selected instructions and
integrator/matrix semantics in `FLRegistry/X3FL.exe`, `-readOnly -noanalysis`.
Raw output stays at `~/.cache/x3m-port/fl-docking-local/`, outside the worktree.
No build, fixture, native/Wine execution, game launch or timing measurement ran.

### 6.1 Minimal additional FL fields and native meaning

All addresses below are preferred-base VAs; use the existing FL RVA binding.
Position, velocity/slip, radius, class/flight/phys fields, E ABI and live-owner
borrow are reused from the base contract and sections 1–4, without another
getter, registry query or hook.

| Input | FL evidence and adapter contract |
| --- | --- |
| Target and order | SA case 10 resolves through `0x473dc4` to `0x46acdd`. It publishes order **4** as a word at `f+0x258` (`0x46ae5d/62`) and the container pointer at **`f+0x224`** (`0x46ae6f`). Case 12's explicit-point variant was already qualified in the gate-transit note. |
| Dock-capable bit | Case 10 reads container `+0x40`, shifts by 20 and tests bit 0 at **`0x46ad50..58`**: **`0x100000`**, exactly AP's predicate. With a nonnegative port and positive port count it calls the native approach-point service `0x45bae0` at `0x46ad81`. This is a field meaning witness; the adapter must not call that service. |
| Class scope | Current AP `dock_container` accepts order 4 to classes **5/6**, or class **7** only with the dock bit; order **0x1d** accepts classes **5/6** only. Other orders/classes keep the existing policy. FL native postpass independently combines the same bit and classes 5/6/7 at `0x465050..79`; the class-7 slew block at `0x462d79..dd9` requires combined flags `0x180000` (dock-capable + autopilot), then an approaching-ship bit or `f+0x180` deadline before zeroing desired speed and turn commands. |
| Idle target retention | SA case **0x1a**, entry **`0x46c8ff`**, writes order **0x1d** at `0x46c928/2d`, a current-position point and deadline, then jumps to the dispatcher tail at `0x46c97f`; the complete case has no `+0x224` operand. SA case **0x144**, entry **`0x470271`**, writes a class-7 ship's destination to `f+0x224` at `0x4702ea/ed`. Thus a preexisting station target can survive native idle. This establishes the inputs to the current conditional rule, not that every idle ship is in a docking queue. |
| Carrier rates | Signed int32 **`obj+0x24/+0x28/+0x2c` = yaw/pitch/roll**, binary angle per second. The integrator's servo paths read/write them at `0x463576/5a6`, `0x463473/4a0`, `0x463370/395`; its uncapped-rate path multiplies by elapsed milliseconds and divides by **1000** at `0x46397d..9c9`. Convert with the existing `2π/65536` factor. |
| Carrier angular frame | The same **`flight+0x870`**, three 16.16 rows at stride **0x10**. Integrator `0x463e60..7d` copies phys rows `+0x40/+0x50/+0x60` into `f+0x870/+0x880/+0x890` when flight attitude offsets are zero; the offset branch builds/composes that flight frame at `0x463e86..ebe`. It is the flight frame used by the AP policy, which can differ from the mesh frame under attitude offsets. |
| Rate signs and units | FL matrix builder **`0x50d400`** consumes yaw/pitch/roll binary increments. Its row-2 terms are `(-cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))`, and positive roll moves row 0 toward row 1, matching the AP study. Constants at `0x5919a8/0x591cf0/0x591d2c` are measured `2π/65536`, `65536`, `-65536`. The existing helper therefore uses `omega = -yaw*row1 - pitch*row0 + roll*row2`. The **0.5 rad/s cap** and nonfinite-to-zero behavior are current policy guards, not native limits. |
| Moving root and children | Carrier phys is the same `obj+0x70` scene root: integrator `0x463ddc..df1` composes/normalizes its `+0x40` basis. The already qualified generic node child/sibling/offset/scale/radius contracts in section 3 apply to that root; no separate ship-tree service or mutable radius getter is needed. A walk copies scalars synchronously and releases every borrowed child address before returning. |

The angular expression is the current AP policy's instantaneous local-axis
approximation. The flight-offset branch above must not be described as a proof
that it is the exact derivative of every animated mesh node. This study does
not qualify port/path transforms for a new native navigation implementation;
the docking adapter consumes the engine's existing target and point.

### 6.2 Bounded moving-tree read and cache retirement

Measured source constants are unchanged: **eight** docking records, **64**
part spheres, **256** immediate-child steps, dirty-radius recursion depth
**three** and budget **64**. An unchanged successful carrier key reuses the
model until **250 ms**, copying the cached set and extrapolating each centre with stored
`w + omega × (part-C0)`. Failed walks wait **5000 ms**; absent/invalid/oversized
models retain the existing fallback. The **0.8 root-radius** carrier interior
disables this carrier row; E applies only on order 4, so the final order-5
docking phase does not need a new port-node reader. These are module budgets
and approximations, not measured FL model populations or performance.

The earlier section-4 cache warning describes its `30957b41` checkpoint. In
current `58f0ab17`, station `PartCache` already keys object pointer, native ID
**`obj+8`**, root pointer, root-radius bits and pose stamp, and invalidates on a
failed rebuild. A's existing native-ID change path also clears its avoidance
state. Reuse those qualified identity reads; no registry lookup belongs in
the docking hot path. Equal keys still do not prove descendant immutability.

The **carrier `DockModelRec` remains a concrete source-level gap** for FL
admission: `dock_rec` matches only `cont`; the 250-ms hit additionally compares
only root-radius float bits. Its 5000-ms failure backoff runs before either
station/carrier rebuild path and is also pointer-only. The receipt gives an
**inferred symbolic witness**, not an observed native lifetime event: a record
built at 1000 ms for `(cont=P, id=101, root=R)` is reused at 1100 ms for
`(cont=P, id=102, root=R2)` with unchanged radius bits. Both the successful
carrier-hit predicate and the failed-record retry predicate accept that alias.
Neither the live-owner borrow nor the station-cache fix retires this separate
carrier record. Current AP code has the same inherited behavior; this finding
does not authorize a shared-policy redesign.

A minimal later FL adapter needs to compare the currently borrowed container's
native ID and root against the record before either cached reuse or failure
backoff, and retire that record on a mismatch using existing lifecycle/identity
facilities. An equivalent already-owned retirement notification is acceptable
only with evidence that it reaches these records before reuse. Static evidence
here does **not** prove native pointer recycling impossible in 250/5000 ms or
close same-ID descendant edits; the latter remains the existing bounded model
approximation. No extra per-draw native query, lifetime framework or native
tree call is justified. A focused copied-row check of ID/root replacement,
failed-walk backoff and existing order/class fallbacks is the remaining
implementation acceptance input, alongside reuse of the qualified A/E/B ABI.

### 6.3 Boundary, liveness and exception limits

Docking remains inside existing A/E/B ownership and simulation-thread admission.
It adds no hook window or calling convention: retain the qualified entry/exit,
register/flags, stack alignment, LastError/MXCSR and XMM save contracts. Borrow
only live owner/target/flight/root/children synchronously; retain copied parts
and scalar identities, never child pointers. Make no native engine call,
wait, allocation or lock while borrowing. The engine's SA dispatcher has an SEH
frame and port/wake paths can call back; they are evidence producers, **not**
new helper reentrancy or exception-safe call capabilities. A raw memory fault
or foreign unwind across the naked hook is not a normal fallback. Existing
guarding and refusal behavior remains required; this static study adds no
runtime/native-Windows, threading, unwind or flight qualification.

## Port (2026-11-03)

`ship_avoidance` now runs on FL: `src/engine/fl/ship_avoidance_fl.cpp` with `ship_avoidance_sites_fl.h`, entered from the
FL steering (see [ship-steering.md](ship-steering.md), "Port (2026-11-03)"). The snapshot is `wk::fill_snapshot`, the
target adjustment `av::resolve`, E's verdict `av::site_e_verdict` (all shared); the FL glue is the AP `snapshot_for`,
`avoid_adjust` and `e_verdict` with the gate, traffic, lane and dock rows off (their groups are not ported; their state
is never set on FL). The walk core reads the TShips slot through its reader's `ship_table()` when the reader has one
(`ship_table_of`, `ship_avoidance_walk_core.h`); every AP reader keeps `kShipTypeTableSlot`, AP objects measured
identical. Docking inputs (section 6) are out of scope.

Install, after the steering's four claims: 6 part-2 windows (the collision reset and pair masks, the bucket helper, the
subtree-radius getter, the integrator's velocity) or the avoidance stays off (`ship_avoidance_refused
reason=window_mismatch`); then 10 part-3 windows (E's entry, class tests and radii, its five `ret 0xc`, the proximity and
avoid-order call sites) and E's claim at `base+0x057600` (six bytes, read back, `lock cmpxchg8b`), or E stays unclaimed
and the target adjustment runs with the native warn untouched. The E stub pushes B (`[entry esp+4]`) and A (saved ECX),
answers a suppressed pair with EAX = 0 and `ret 0xc`, and leaves every other register and the flags as they entered.

Section 4's lifetime limits are handled as the deliberate FL differences of the steering port: a recycled A is not
answered from the previous object's decision, and each resolved B carries its native id from the resolve (`+0x08`, read
while the snapshot's objects are live); a B whose id changed is not suppressed. The station part cache key is unchanged
(pointer, root-radius bits, pose stamp): a tree replaced behind the same pointer, radius and pose keeps the cached model
(measured: `STATION_REPLACED rebuilt=0`), the inherited AP behaviour.

Verified (host, measured 2026-11-03): `verify_ship_avoidance_sites_fl.py` PASS; the native fixture
`verification/probe/fl_ship_avoidance.cpp` runs the engine-model harness's avoidance unit cases (the head-on asteroid with
and without the clearing slip, the station fallback, the M5 head-on ship at 45–400 m, the capital/small-ship row) and an
asteroid sweep over lateral offset and slip, 46 cases, against the shared cores: snapshot, detour point and E verdict
46/46 equal; 19 resolved-row suppressions (1 table case, 18 sweep cases), each with the detour path keeping the hulls
apart (19/19); the walk check's station tree
gives the native getter's radius (125,000 units both); E through the three real callers' bytes 3/3; E CPU state 16/16;
16/16 window flips refused (6 turn the avoidance off, 10 E alone); recycled A and B; the E window under SETA steps 9/9.
Not verified: in game (not flown), native Windows game behaviour, station populations, runtime ordering and threading,
abnormal unwind across the stubs.

The "skipped only while the detour path keeps the hulls apart" property holds for the resolved row only. The small-ship
and formation-member rows of `site_e_verdict` (a capital A against a small or member B) skip the native warning with no
detour-path proof, the inherited AP rule (the small ship or the member avoids the capital itself); the fixture compares
their verdict with the core (the capital/small case: `suppress_small` through the FL TShips slot) but does not count them
for the property. Known cap, shared with AP's site E: `av::fresh_window_ms` caps the step at 250 ms (`ship_avoidance_core.h`), so no
window exceeds 1 s; at game-time frame steps above 1 s the E window, B clamp, port hold and leader lookahead are stale on the
next frame (the engine's own speed and leaf law then stand). The shared core is unchanged
(AP objects identical).
