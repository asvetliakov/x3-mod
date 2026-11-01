# FL arrival expiry, stock reissue and gate clearing

2026-10-31. Bounded static reconstruction for the full arrival port. Read with
[gate motion](gate-motion.md#9-arrival-only-future-writers-and-the-continuous-native-warp-profile-2026-10-30),
[angular motion](angular-motion.md), and
[lane handover](lane-handover-contract.md#1-the-actual-completion-issuer-and-removal-sequence).
No production edits, build, native execution, Wine, installation or game launch.

**Finding:** FL has a stock script-mediated `21 -> 0 -> 1e -> 0` sequence,
conditional on the live task and script flags. Expiry does not issue order `1e`
inside its normal callback. Order `1e` can complete before writing speed;
when active it resets current speed on every command visit, followed by common
clamping and slew. Its completion does not itself brake. An unconditional
`WarpIn -> exit at 120/240 m/s -> brake` recurrence is therefore unsupported.

## Reproduction and evidence scope

Run `python3 verification/results/fl-arrival-entry/arrival_exit.py`, then
`python3 -m py_compile verification/results/fl-arrival-entry/arrival_exit.py`.
The [producer](../../../verification/results/fl-arrival-entry/arrival_exit.py) and
[receipt](../../../verification/results/fl-arrival-entry/arrival-exit.json) check
**21 whole-instruction regions, 746 instructions, 94 semantic anchors, three
SA name/dispatch bindings, two order dispatch entries, 36 stock method
statements with zero symbolic-depth conflicts, eight derived axis witnesses,
and six rejected instruction mutations**. Counts are measured static facts;
axis witnesses are inferred arithmetic, not game observations.

Original image `/mnt/c/X3FL-orig/X3FL.exe`: 2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
The original resource resolver selects `addon2/03.cat` member
`addon2/L/x3story.obj`, SHA-256
`dbb732716e1acab6b8cc7f87f5d31324fb4764dfc22d1cec808749a4f45ec7b9`.
Addresses use preferred base `400000`; production binding must remain RVA and
loaded-base aware. The project's PE/Capstone mapper, original SA name table
`5a4098`, dispatch table `473dc4`, and existing KC reader are reused. There
was no available live project-analysis connector in this session. Raw selected
listings and optional extracted original bytes stay outside the repository in
`/tmp/fl-arrival-exit-local/`; the receipt contains derived metadata only.

## 1. Actual registration, wake and script boundary

SA case `19` (hex), `SA_StartInHangar`, selects `46c876`. It calls
`44ccb0(ship,container,port)` at `46c8c3`; result 1 takes `46c8d4 -> 46acc9`,
saving requester type from `[dispatcher_entry_EBX+8]` into `ship+7c` and task
context from `[EBP-34]` into `ship+80`. On the established gate launch route,
`44d3b0` starts order `21`, performs sector insertion, then invokes the saved
requester callback at `44db72` and clears `+7c/+80` after it returns. This
launch wake resumes the waiting stock `__LeaveHyperspace` task.

Class 2004 `__LeaveHyperspace` calls `__LeaveHyperspaceWarpIn` at KC CODE
`147fa3` on its nonplayer branch. The latter's entry is **`148284`**. Its
bounded relevant sequence is:

| KC CODE | Effect |
| --- | --- |
| `1482dd` | `SA_SetSpeedLimit(m4,m92)` |
| `1482e8` | `SA_SetDesiredSpeed(m4,0)` |
| `1482ff` | Nonplayer calls `SA_WarpObjectIn(m4,p0)` and waits |
| `148308..148323` | Require live `m4` and `m13 & 400000 == 0` for clearing |
| `148331` | `SA_ClearObjectFromHangar(m4,p0,-1)` and waits |
| `148338` | Clear script intent `m44` |
| `14833f..14837d` | Conditional collision query and `SA_AvoidObjectCollision` |
| `148386..14839e` | Restore rotation acceleration and clear script stage |

`SA_WarpObjectIn` is case `21` (hex), body `4703a6`. Its test
`4703ef..4703ff` detects existing order `21` with the same target and jumps
straight to registration at `470504`: `ship+7c/+80` now hold the requester
and context waiting for **warp completion**. It preserves the launch's
anchor, deadline and pose on this path. The alternate body can start a new
warp; it is not the already-running arrival wait path.

The normal KC requester identity is established, not guessed from SA's own
callback: VM construction publishes **`4c1a40`** at
`VM+34+type*24` (`4b8d44`) and saves default type at `VM+1454` (`4b8d83`).
The interpreter pushes this default requester type at `4c077a` before calling
the selected dispatcher at `4c0783`. `4c1a40` resolves the task context,
writes result status `task+20=1`, integer tag/value `+28/+29`, and calls
`4c1880`. For a suspended task `+3a==2`, that helper transfers the result and
links the task into **VM+12d8 ready list**. Neither body calls the interpreter.
Typed old-value releases use the previously established release closure;
foreign requester callbacks and faults are outside this fixed stock proof.

Thus native expiry `45f0ad` clears visual state, clears `400` at `45f0f0`,
snaps physical `+30` from anchor `flight+240` at `45f104`, and jumps to
`45efb3`. Shared completion writes order zero at `45efba`, phase `+218=0`
at `45efc4`, then invokes `46a550(1)`. Normal wake queues the task and returns
with order zero through `45efd1 -> 462c6e`. It does **not** inline reissue `1e`.
Expiry does not zero either speed field; zero comes from an earlier running
warp visit or other proved writer. A first visit already past the deadline
must not assume that earlier running visit happened.

## 2. First nonwarp collision/E and actual B chronology

Outer loop calls ready scheduler `4788b0` at `477ebf` before sector driver
`443b10` at `477ed2`. The scheduler executes `4bed90` at `4788e8`, rereads
the ready-list head and continues until empty, unless interpreter returns
zero (`4788ef`). Reuse the dispatch-failure/interrupted-task limits of the
lane-handover note; no bounded ready-drain time follows.

Sector driver walks sectors once for **collision `466de0` (`443b42`), command
`45c810` (`443b49`), then post-pass `465030` (`443b50`)**. A second sector
walk integrates `463000` (`443b76`). Therefore:

1. The expiry iteration's collision pass precedes expiry, and normally still
   sees flag `400` and excludes the ship. Expiry/wake then leave order zero
   visible at that iteration's B and speed slew, before later integration.
2. The next healthy scheduler drain can resume `148305..148331` and issue
   order `1e` **before the next collision enumeration**. This is conditional
   on script guards and task survival; elapsed age alone does not prove it.
3. That collision reads the newly issued order and prior integrated speed;
   the current iteration's command reset and B have not run yet. A B sample
   from the expiry iteration does not identify this later command state.

There is also an actual native delay before proximity calls E during gate
clearing. `457d2d` tests order `1e`; for target class `12`,
`457d53 -> 419f40` computes **Chebyshev distance** between current physical
position and `flight+230`, the start saved by the clear issuer. Signed distance
**`<= 15ae0` (88,800 units, 177.6 m)** takes `457daa -> 457e08` before E.
This is checked irrespective of the current pair's B, after earlier proximity
predicates. Immediately after issue that distance is zero. Once displacement
exceeds the bound, normal proximity E can run before the same iteration's
active clear command/B; other native predicates still apply. If clearing
completes immediately, a later collision instead reads order zero. This
spatial native guard is neither a temporal forecast certificate nor a promise
that any particular pair will reach E.

## 3. Native order `1e`: issuer, point, speed and termination

SA name/dispatch tables bind case **`0d`** to `SA_ClearObjectFromHangar` at
`46b1ca`. The issuer resolves ship and container, requires class-7 ship and
container class 7/6/5/12, writes time `flight+210`, order `+258=1e`, target
`+224`, and current physical position to **`+230`**. It stores port at `+250`.
For the stock **port -1**, `46b27d -> 46acc9` registers the waiter without
computing a port exit point. `+240` is regenerated by the gate handler.
Nonnegative ports and nongate handlers are outside the finite gate mode.

Order tables select `46161c`. It refuses the command body when
`object+40 & 08000404 != 0`, rereads target `flight+224`, handles null through
`461c78` (order zero and wake reason 3), and selects the gate branch only for
target class `12`. Define `r` as gate object radius and
`dz = sum_i r16((ship.phys.pos_i-gate.phys.pos_i)*gate.F_i)`, where
`r16(x)=(signed64(x)+32768)>>16` with each component rounded separately and
native signed32 addition. Dot helper is `432ae0`; ordinary admitted operands
must exclude subtraction, abs(INT_MIN), sum and point-construction overflow.

Predicate **`449690`** accepts class-7 ship category indices
`{0,2,3,4,8,11,13,14,15}` (its own table `4496c4/4496cc`, not the separate
launch capital predicate `4496e0`). Let `large` denote that predicate.

* Threshold is `r` for large, otherwise signed truncation `r/4`. Signed
  **`abs(dz) > threshold`**, strictly greater, takes `4616e3 -> 45db62`.
  Equality remains active. This happens **before steering or either speed
  store**. Gate-clear duration can therefore be zero visits of forced speed.
* Otherwise set `q=dz+r` if `dz>=0`, or `q=dz-r` if `dz<0`, then write
  **`flight+240 = current ship physical position + r16(q*gate.F)`** and call
  `456450(ship,&flight+240)` at `461775`. The point stays on the ship's
  own gate-parallel line; it is not gate center plus q. At exact dz zero the
  chosen direction is **+F**. A presumed -F heading needs an actual sign proof.
* Desired speed becomes current `45a450(ship)` maximum. Current speed becomes
  **60,000 (120 m/s) if large, 120,000 (240 m/s) otherwise**, every active
  command visit, before common B. These are command stores, not necessarily
  the subsequently integrated speeds.
* Success `45db62` writes only order zero and wake reason 1, then reaches B.
  It preserves target, port, current speed, desired speed and angular commands.
  A later stock continuation may issue avoidance. It does not imply braking.

For example, `r=400,dz=-400,large` remains active and forces 60,000;
`dz=-401` completes without that store. A hypothetical P at -1348 m with
r=1104.5 m also completes immediately in the large branch. That receipt case
is a derived arithmetic example, not a measured FL ship/gate pose.

## 4. Supported recurrence, common integration and authority limit

For a finite supported active-clear step, first execute the native point/
steering/current-speed stores above, then the common second walk. It upper
clamps desired to maximum and nonnegative flight speed cap `+278`;
flag/timestamp suppression may zero desired and remaining angles. Shared
slew `462de0..462ec7` advances **from the newly forced current speed**, using
ship acceleration (including `flight+274`) and command dt `min(dt,1000)`.
Within the already admitted nonnegative, nonoverflowing arithmetic domain,
its step is `max(1,min(abs(desired-current),trunc(acceleration*dt/1000)))`
when speeds differ, directed toward desired. Repeated active visits reset
current before this slew, so integrating acceleration across visits from the
previous final speed would reconstruct a different law. Reuse the
[post-B prefix](gate-motion.md#shared-post-b-speed-prefix-and-finite-first-step-gap-2026-10-31)
and [angular/integration contract](angular-motion.md).
Object integration has its own dt selection; command dt is not automatically
that object dt. The integrated velocity uses post-slew speed, flight basis,
angular updates and slip, then root publication; a current E speed alone
cannot describe the pending translation.

An axis-parallel recurrence additionally needs fixed gate frame, stable
ship/flight/target/root identities, correctly aligned root/flight basis,
zero angular inputs and no transverse velocity/slip creation under the
existing continuous-warp and integration premises. Exact dz-zero and
immediate-completion branches must remain explicit. Generic steering,
explicit pose changes, script guards/task replacement, collision/avoidance
and new commands are native boundaries requiring fresh acquisition.

Later order-zero cleanup `462896..4628b9` zeros desired only when
`object+40 & 80000` and byte `object+44 & 4 == 0`; it clears target in that
flag branch. It is reached on a later order-zero command visit, not by the
clear-success jump. Therefore a brake phase needs an actual native writer
and observed order/flag premises. No new persistent history, VM observer or
per-frame scheduler tracking is required by this static finding; the next
bounded witness can join exact current native states at existing boundaries.

## 5. Fixture extraction, ABI, liveness and missing evidence

The producer exports `SPANS` with exclusive ends. Use optional
`--extract-local /tmp/fl-arrival-exit-local` to obtain a local-only original
header. The owner can reuse existing A0, getters, speed-prefix/slew,
integration and root-publication spans. Key additions are:

| Interval | Role | Bytes / instructions |
| --- | --- | --- |
| `45f0ad..45f10d` | Actual arrival expiry, including visual-child loop | 96 / 23 |
| `45efb3..45efd6` | Shared order-zero/phase-zero/wake/common exit | 35 / 9 |
| `46a550..46a59e` | Actual wake and indirect callback boundary | 78 / 24 |
| `46161c..4617af` | Complete gate-only `1e` handler | 403 / 101 |
| `45db62..45db79` | `1e` success/order-zero/wake | 23 / 6 |
| `461c78..461c9c` | Null-target completion | 36 / 8 |
| `46b1ca..46b2b2`, `46acc9..46acdd` | Real clear issuer and -1 registration tail | 232 / 60; 20 / 5 |
| `432ae0..432b2b`, `412c00..412c5f` | Original rounded dot/scale | 75 / 27; 95 / 36 |
| `449690..4496c3` and data `4496c4..4496dc` | Exact category predicate | 51 / 14 plus 24 data bytes |
| `457cd0..457e10`, `419f40..419f7e` | Proximity guard and Chebyshev helper | 320 / 96; 62 / 35 |

Interior command slices use **EBX=ship, ESI=flight, ESP=S**, original command
scratch contract `0x820` bytes with S 16-byte aligned, and sector reload
`[S+24]`. Keep the existing whole-command `0x824` prologue/frame contract when
entering the whole function. The gate slice uses scratch through the temporary
vector around `S+6c0`; `movaps` scratch operands require alignment. Account for
push-adjusted displacements around `412c00`: the gate F is at logical `S+2a0`.
Shared completion reloads flight through ship; the success slice uses live ESI.
No new displaced hook window is proposed. Copying these slices does not prove
hook entry/exit CPU flags, LastError or foreign exception safety.

Wake is **ECX=ship, one stack reason, ret 4**. It saves ESI, calls cdecl
`callback(context,reason)` and pops eight argument bytes; callback may clobber
volatile registers/flags/LastError, and wake clears replacement `+7c/+80`
after return. The fixed stock callback only queues; arbitrary reentry,
destruction or throwing callbacks are unproved. No copied-pointer helper
borrow may span wake. `432ae0` takes ECX/EDX vectors, returns EAX and preserves
ESI/EDI; `412c00` takes ECX vector, stack output/scalar pointers, returns output
in EAX, ret 8. The gate command may call native A0 and getters; those existing
closures remain prerequisites.

The issuer spans are interior to an SA dispatcher with SEH setup at
`46a656..46a664` and a larger aligned frame, not standalone cdecl functions.
For port -1, enter at `46b22f` only after providing the real resolved ship/
target and dispatcher EBX/EBP context, or execute the full issuer with proper
resolver bridges; terminate at its real common exit. Do not borrow the
command scratch ABI for this dispatcher. For the smallest expiry fixture,
use an empty, valid visual-child sentinel so `4a3bd0` is not replaced by a
made-up callee. Nonempty visual children need its actual existing closure.

The required next native witness is actual expiry -> callback records/queues
order-zero -> actual B/slew/integration -> explicit scheduler boundary ->
qualified actual issuer -> next command `1e`, with both immediate-completion
and active threshold-equality cases, both categories, and cap/slew differences.
Exercise the proximity displacement guard separately at 88,800 and 88,801
units and record actual E reachability before B. A controlled callback that
inline-reissues `1e` is a reentry control, not normal stock chronology. Existing
fresh-identity/order/forward pair-E results do not compare this arrival
forecast with the native sequence. No native boundary witness or complete
future arrival suppression authority is established by this static receipt.

## 6. Pending A0 steering and a uniform zero-angle continuation family

Follow-up, 2026-10-31. The [A0 producer](../../../verification/results/fl-arrival-entry/arrival_a0.py)
and [derived receipt](../../../verification/results/fl-arrival-entry/arrival-a0.json)
check the complete **947-byte / 266-instruction** native `456450..456803`
leaf, **45 instruction anchors**, five binary64 constants, its three output
stores and four direct math calls. Five instruction mutations are rejected.
Run `python3 verification/results/fl-arrival-entry/arrival_a0.py` and
`python3 -m py_compile verification/results/fl-arrival-entry/arrival_a0.py`.
These are static/host checks; this follow-up performs no native execution.
The accepted scalar integration law in
[angular motion §1](angular-motion.md#1-per-axis-cap-acceleration-time-and-scalar-update)
is reused; it is not rederived or replaced by a new servo.

### 6.1 Exact leaf input, local projection and output writes

A0 takes ECX=ship and one stack `int32[3]*` point, cleans it with `ret 4`,
saves EBX/EBP/ESI/EDI and writes only the ship's remaining yaw/pitch/roll at
`4567ec/4567ef/4567f2`. **Roll remaining is always overwritten with zero.**
It does not read old remaining commands, current angular rates, current speed,
flags or angular plant values. It leaves current rates unchanged for the
later integrator. The class-7 basis is the **actual flight basis `flight+870`**
(`4564a7/aa`), not root `phys+40`. Nonships use the root basis; the arrival
mode remains class 7.

Let `D=point-current physical position`. If every signed absolute component
is **strictly less than 100**, `45647c/488/494` selects the all-zero command.
Exclude signed subtraction overflow and abs(INT_MIN). Otherwise native local
coordinates are, for flight rows R/U/Z:

```text
x = sum_i R16(D_i*R_i)
y = sum_i R16(D_i*U_i)
z = sum_i R16(D_i*Z_i)
R16(p) = floor((signed64(p)+32768)/65536)
```

Each component product is rounded separately; additions are z-product then
y-product then x-product and are native signed32 additions. A real-valued dot
followed by one rounding is a different computation. All products, rounded
outputs, partial sums, coordinate additions and differences need the admitted
nonoverflowing domain.

### 6.2 General new remaining-angle law and its unresolved numerical boundary

For finite nonzero local vector, `45659a..45660f` forms binary64
`sqrt(x*x+y*y+z*z)`, in that add order, then scale
`1/(norm*(1/65536))`, and truncates each scaled coordinate with CVTTSD2SI.
Call that normalized integer vector `(X,Y,Z)`. If both X and Z are zero,
`45661b..45662c` writes yaw zero, pitch **+16384 if Y>0, otherwise -16384**,
roll zero. Even a degenerate all-zero local projection takes the negative
quarter-turn branch when the world near-point early return was not taken;
never infer zero remaining merely from a zero projection norm.

Otherwise:

1. `456631..456674` independently normalizes integer pair `(X,Z)` to Q16,
   again with binary64 sqrt/divide and truncation. `45668a -> 549afb` takes
   `atan2(-Xpair,Zpair)` in XMM0/XMM1. Negative radians get `+2*pi`; multiply
   by `65536/(2*pi)` and CVTTSD2SI. If the integer is at least 65536, subtract
   65536 once. Call this unsigned turn `u`.
2. Form `t=(-u)&65535`, then binary64 radians as
   `t*(pi/65536) + t*(pi/65536)` (`4566be..4566e0`). Native double cosine
   `54a1b1` and sine `54bcd6` supply
   `Zp=trunc(cos(t)*Z + sin(t)*X)` at `456722`.
3. Normalize pair `(Y,Zp)` when nonzero, then `45678e -> 549afb` computes
   `atan2(Ypair,Zppair)`. Apply the same unsigned-turn conversion to get v.
4. Signed yaw is **`u-65536` only when u>32768**, otherwise u
   (`4567be..4567ca`). Equality stays **+32768**, not -32768. Signed pitch
   uses the same strict wrap. Pitch is then forced to zero when signed yaw
   lies outside inclusive **[-16384,16384]** (`4567cd..4567e8`). Roll is zero.

All reference to sine/cosine in this sequence denotes the native binary64
callee, not the float32 Euler matrix helpers used by integration. Constants
at `591a00/591b60/591bd8/591c78/591a20` are respectively 1/65536, 1, 2*pi,
65536/(2*pi), and pi/65536. No angle lookup table is used in this leaf.

This establishes the operations and branches for general nonzero commands.
The receipt includes seven **host-libm illustrations**, explicitly inferred;
they do not qualify replacement atan/sin/cos at integer conversion boundaries.
A general numeric producer must reproduce this operation order and bound the
original CRT discrepancy, or compare its admitted finite nonzero packets with
actual copied A0 outputs. Simply calling the shared float `atan2_f` once, or
reusing old pre-A0 remainder after a first-step angular bound, is unsupported.
The exact backward-axis case also needs the positive +32768 boundary retained.

### 6.3 Useful sufficient zero and a whole active-interval proof

A sufficient integer test on **raw local** coordinates is:

```text
z > 0
65536*abs(x) < z
65536*abs(y) < z
```

Use widened checked products. The full Q16 normalization then truncates X and
Y to zero, with Z positive. Native yaw and pitch both evaluate atan2(0,positive)
and produce zero; roll is zero. The simpler raw x=y=0,z>0 case is included.
The strict integer gap is at least one unit: with signed32 z it exceeds
2^-31 relative, far larger than binary64 normalization rounding (even allowing
several directed-rounding ulps). It therefore does not depend on a host
near-one truncation coincidence. The producer checks 82 such boundary inputs.
This is sufficient rather than necessary; reject outside the test without
claiming that the native result must be nonzero.

For an entire active gate-clear interval, D is generated from the **current
ship position** by `D_i=R16(q*F_i)` under the no-overflow point-add/subtract
contract. Lateral position and lateral slip cancel from this D; they need
not be zero. With fixed gate F and flight rows, define exact widened products:

```text
C_j = sum_i F_i * flight_row_j_i
E_j = sum_i abs(flight_row_j_i)/(2*65536) + 3/2
local_j(q) is enclosed by q*C_j/65536^2 + [-E_j,+E_j]
```

The first term in E bounds vector-scale rounding propagated through the row;
3/2 bounds the three later per-product dot roundings. This follows directly
from two rounds each in [-1/2,+1/2]. For a supported q interval, take endpoint
extrema of the affine term and add E. If its entire z interval is positive
and `65536*maxabs(x_interval)<min(z_interval)` with the same y test, **every
native A0 in that interval writes three zero remaining commands**. The
producer evaluates this enclosure with exact rational arithmetic. A compiled
bound needs checked widened arithmetic or outward-rounded evaluations.

For the ordinary negative-side large-ship branch, active `dz in [-r,0)`
gives **q in [-2r,-r)**. The closed interval [-2r,-r] is a conservative
family. The sign premise matters: dz=0 selects +F and is a separate branch.
Flight/root pose, offsets, identities and source domain must remain under the
existing finite recurrence; arbitrary native pose changes are not covered.

Using source-derived gate-row-0 radius **681001 units** from the existing
stock-join values, the receipt checks 63 synthetic Q16 bases spanning yaw and
pitch rotations: **33 pass** this sufficient uniform-zero bound, and **1,155
sampled q values** in those passing families also yield the zero normalized
inputs and zero host angle result. The interval proof covers every admitted q;
the samples are arithmetic controls. These counts describe synthetic bases,
not a live stock orientation census. Rejected families are unresolved by this
bound, rather than evidence of unsafe motion. Selected examples and exact
basis values are retained in the compact receipt.

The same source geometry has modeled M2 anchor z=-552557 at radius681001;
the active exit distance is 128444 units (256.888 m). Gate row21 has modeled
anchor z=-552889 and radius714242, leaving 161353 units (322.706 m). These
are source-derived geometry, not measured live ship/gate states. Their active
interval can extend beyond the native 177.6 m proximity guard. Do not replace
these FL inputs with the earlier hypothetical radius1104.5 m counterexample.

On the ordinary nonplayer autopilot branch, zero remaining explicitly makes
integration clear the current axis rates and emit zero increments. Thus a
uniform-zero A0 family reuses the established no-turn recurrence, even if
pre-command angular rates were nonzero; stable equal flight offsets and all
other ordinary integration premises still apply. Side-slip translation can
then use the existing directional copied-value integration with its admitted
error bounds. The producer must still prove dz remains in the selected sign/
active interval, and handle actual completion, order-zero and subsequent
script commands at their native boundaries. This provides no lease through
those boundaries and introduces no persistent tracking.

### 6.4 Helper boundaries and next native witness

The existing [A/B ABI and lifetime contract](ship-steering.md#abi-cpu-state-lifetime-and-reentrancy)
remains controlling. A0's original entry consumes its flags immediately;
its four callees are math code, with no navigation/KC callback or recursive
edge in the qualified graph. These facts support a short copied-value
calculation, not a borrow through expiry wake or a cross-thread lifetime
promise. Keep the established XMM0-7, MXCSR, LastError, GPR/stack preservation
for any injected helper; no new hook is proposed here. A0 itself installs no
SEH frame and adds no foreign-unwind guarantee.

The next bounded native comparison should retain actual A0 and record its
three new remaining commands immediately after `461775`, including aligned
and rotated full-zero families, backward +32768, exact vertical and a few
nonzero near-quantization cases. Compare those with the numeric producer and
then consume their remainder through the **already accepted native scalar
law**, updating remaining by the native emitted increment. At each subsequent
active clear visit A0 rewrites remainder again. No E-time mutation of the
native leaf is needed or authorized by this finding.

### 6.5 Optional component extrema for ambiguous normalization (2026-10-31)

The generic interval normalizer may intersect its existing derivative-ball
cover with exact component extrema of its axis-aligned input box. This changes
neither the native operations in §6.2 nor the singleton nominal calculation.
It adds no new caller premise or persistent state. In particular the existing
nonzero/pole, helper-RN, native integer overflow and projected-world near-point
premises remain controlling. A projected family still represents native local
integer coordinates divided by **one common positive scale**.

For component x in [l,u], let S be the sum of the other components' squares.
The box supplies independent Smin/Smax: each squared interval's minimum is zero
when it crosses zero, otherwise the smaller endpoint square; its maximum is
the larger endpoint square. For nonzero vectors,

```text
f(x,S) = x / sqrt(x*x+S)
df/dx = S/(x*x+S)^(3/2) >= 0
df/dS = -x/(2*(x*x+S)^(3/2)).
lower = f(l, l>=0 ? Smax : Smin)
upper = f(u, u>=0 ? Smin : Smax).
```

The zero-x endpoint is exactly zero. The old derivative-ball test already
excludes a box containing the zero vector; the new calculation does not bypass
that test or any later pair-normalizer pole check. These are component bounds,
not a claim that their independent endpoints describe one realizable vector.
They may be propagated as a single box without forming trajectories.

`arrival_angles.h::component_bounds` evaluates squares, sums, square roots,
divisions and Q scaling outward in binary64. After each RN operation it takes
one adjacent representable value in the required direction. This encloses the
exact operation by correct rounding, including ties. Signed division uses an
upper denominator for a positive lower endpoint or negative upper endpoint,
and a lower denominator for the other two cases. Projected centre +/- radius
endpoints likewise move one adjacent value outward. The sign choice follows
the derivatives above; each subsequent operation is monotone in the selected
positive denominator. No rounding-mode change or CRT nextafter call is needed:
the adjacent value is formed with integer operations on the IEEE binary64 bits.

The optional calculation declines nonfinite intermediates, nonpositive
necessary denominators, nonzero endpoints whose square is below 2^-1020, and
quotients of magnitude below 2^-1020. These conservative exclusions keep its
arithmetic away from underflow, including callers with ambient FTZ/DAZ; the old
cover remains available. Exact zero terms remain exact. The normalized real
component interval is multiplied by Q=65536 and expanded by the **existing**
`normalization_error=2^-30` absolute Q16 allowance, which exceeds the qualified
native/helper eight-factor square/add/sqrt/reciprocal/product roundoff bound
in all four native MXCSR rounding directions. This allowance concerns integer
normalization, not the double CRT atan/trig budgets and not the float Euler
leaves. Intersect with the existing [-Q,Q] integer norm shell, apply monotone
CVTT, then intersect the old derivative interval. Exact singleton inputs keep
the old normalization arithmetic; the new intersection applies to ambiguous
2/3-component integer boxes and the initial real projected box. The later
atan, word conversion, wrap, positive +32768 and inclusive pitch-suppression
pipeline is textually unchanged.

The actual first broad covered-basis packet at t=.016 has centre
(-.009796142578125,.0050811767578125,.9999237060546875) and radii
(.0020656262498743192,.0020656228553028824,.0020656299693387315).
The [motion packet producer](../../../verification/results/fl-arrival-entry/motion_projected_witness.py)
retains this input and its source provenance. The compiled before/after witness changes its pitch interval from [-125,230]
to [30,75], retaining nominal yaw/pitch 102/52. This is a host calculation,
not fresh original-A0 execution or whole-motion acceptance. The producing
[packet/timing script](../../../verification/results/fl-arrival-entry/angle_component_witness.py)
compares the accepted 585c525a header with the current header and retains
[its receipt](../../../verification/results/fl-arrival-entry/angle-component-witness.json).
The focused [host producer](../../../verification/results/fl-arrival-entry/angle_host.py)
also retains its original input/pipeline source equality guards, allowing only
the explicit optional ambiguous-component additions to the old core. Its
expanded controls check all box corners with independent long-double arithmetic
and normalized integer results in all four rounding modes, in addition to the
existing A0 and projected-family corpus. Original-code qualification now includes the seventh broad packet: see the
[native checkpoint](../../verification/farnhams-legacy-gate-clearance.md#arrival-normalized-component-bounds--native-checkpoint-2026-10-31).
Previous native receipts are unchanged; this does not establish whole-motion acceptance.
