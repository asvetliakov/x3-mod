# FL gate warnings: real E paths and ordinary translation

2026-10-30. Bounded static follow-up to the
[avoidance E contract](ship-avoidance.md),
[source-copy lifetime](ship-source-lifetime.md), and
[ship profile](../../architecture/farnhams-legacy-ship-profile.md). No production
edit, build, PE/native execution, Wine, install or game launch occurred.

**Finding:** native warp order `0x21` is excluded from ordinary collision-pass
E by object flag `+40 & 400`. The first collision-active post-warp state is
ordinary command state after completion; it is not the remaining part of the
900 ms warp curve. E precedes command and integration writers, so its current
speed, basis and physical velocity do not define a fixed pending movement
segment. No useful ordinary 900 ms suppression authority is established here.
The concrete first candidate is the normal collision caller with current
order zero and nonzero speed/desired speed. Its future translation and result
consumption still require the bounded obligations in section 5.

## Reproduction and limits

Run `python3 verification/results/fl-gate-motion/verify.py` and
`python3 -m py_compile verification/results/fl-gate-motion/verify.py`.
The [producer](../../../verification/results/fl-gate-motion/verify.py) and
[receipt](../../../verification/results/fl-gate-motion/evidence.json) check
209 semantic instruction anchors, 17 completely decoded ranges, all three
encoded direct E calls, both encoded direct proximity calls, four order jump
table entries, and nine rejected mutations. Counts are measured static facts;
the small arithmetic counterexamples are inferred from the established
instructions, not flight samples. Hashes are provenance, not image admission.

Original `/mnt/c/X3FL-orig/X3FL.exe`: 2,804,736 bytes; SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
VAs below use preferred base `400000`. Production must retain loaded-base/RVA
binding. Read-only Ghidra `FLRegistry/X3FL.exe`, project analysis scripts, and
the project's PE/Capstone mapper corroborate the selected caller functions.
Raw listings/decompiler output remain untracked under
`~/.cache/x3m-port/fl-gate-motion/`. Encoded call censuses do not exclude
computed/private-interior or foreign calls. This is not all-order navigation
or complete native writer reconstruction.

## 1. Actual routes and the `400` lifetime

`466de0` resets warning bits `3000` at `466e20` and each ship's flight
`+1e0` count at `466e38`, then enumerates class pairs. Both outer and inner
objects must pass `flags+40 & 08200404 == 0` (`466f00/466fd1`) and
`flags2+44 & 20000 == 0` (`466f19/466fee`). The mask includes `400`.
The sector's `+148` mode additionally requires `flags2 & 400` on the
applicable branch; this is a different word and must not be confused with
object `flags & 400`. Pair processing reaches proximity at `46753c/46755d`
only for the corresponding class-7 ship with `flags & 800 == 0`.

Proximity `457cd0..457e10` then requires A's `flags2 & 10 == 0` and
`456c10(A,B)` true. That predicate admits gate class `12` for ordinary
class-7 ships subject to its type/order exclusions; it is not a gate-only
predicate. For B gate, the non-order-12 path additionally requires the supplied
distance to be less than `A.radius + 2625a0` (`457d13..27`). Order `1e`
has extra target-distance exclusions. These are native path conditions,
not a replacement for geometry/owner admission.

| E call | Proven route and result |
| --- | --- |
| `457db5` | Proximity with A's current order other than `12`; false skips `1000`, list append, `2000`, and count increment for this pair. |
| `457dd0` | Same proximity with current order `12`; false skips append/`2000`/count. True does not set `1000` here. |
| `460581` | Command order `12`, after `flags & 08000404 == 0`, its rescore deadline, and selection of a qualifying obstacle-list row. False clears that row at `4609d4`, decrements working count and affects the remaining command. |

Gate arrival setup `44d3b0`, called from post-pass `465030` at `4650af`,
sets order `21` at `44d6fb`, state `flight+218=1`, deadline
`flight+214=clock.now+900`, saves anchor `flight+240`, and sets `400` at
`44d73a`. The running command's `45f11a` reasserts `400`, writes the warp
pose at `45f096` and zeros desired/current speed at `45f09a/45f0a1`.

At expiry, `45f0c0..45f0e4` clears visual state/child state;
**`45f0f0` clears `400`**. `45f0fd` is the anchor load, not the flag clear;
`45f104` restores physical `+30` from flight `+240`. Shared completion
`45efb3..45efcc` sets order zero and `+218=0`, then calls `46a550(1)`.
That routine can invoke a registered callback at **`46a584`**, then clears
the callback fields. The assignment before this call does not prove that
the callback leaves the ship in order zero. The next E must read current
order/identity/pose under its own borrow.

Order `8` has its own `400` set/clear at `45eff0/45efac`; ordinary idle
cleanup can also clear it at `462a10`. Other generic/native writers exist.
Thus `400` is an exclusion bit with a demonstrated warp lifetime, not a
unique warp identity or a construction/readiness bit. No persistent arrival
certificate may be inferred from it becoming clear.

## 2. Current position and the next native translation

Let `R(n) = floor((n+32768)/65536)` and `T(n,d)` be signed division truncated
toward zero. Native dword additions wrap; an authorizing checked reader must
reject any proposed no-wrap interval whose additions/products cannot be
represented rather than substitute saturation for native arithmetic.

At E the current position is the freshly copied physical root
`P_E = *(A+70)+30..38`. Physical velocity is root `+100..108`, slip is
object `+60..68`, current/desired speed object `+10/+14`, and the ship's
flight forward row is `*(A+50)+890..898`. Those are different inputs.

The ordinary integrator (`463000`, called at `443b76`) reads clock root
`[5a83b0]`, time `+708`. Its per-object delta is the signed dword difference
`now - A.last_update(+a8)`: **negative or greater than 1000 becomes 1000**;
otherwise it is unchanged (`46319b..4631c3`). Zero remains zero. This is not
simply the command frame step `clock+704`, nor a real-time/render-frame delta.
The object's last-update time is written at `464d5a`.

For the ordinary undocked/nonattached branch, the relevant transition is:

1. Native angular integration composes and normalizes the root basis at
   `463de3/463df1`. With no flight angular offsets, it copies root basis to
   flight `+870/+880/+890` at `463e63..463e7d`; otherwise it composes and
   normalizes the flight basis at `463e98..463ebe`. Denote the resulting
   flight matrix by `B'` and the speed at this phase by `v'`.
2. `463ef7` saves old physical velocity `V`. `463f10..463f37` assigns
   **`V'[i] = R(v' * B'[2][i])`** to physical `+100..108`.
3. For class 7 with order other than 5 and nonzero `v'`, `463f6c` calls
   `50dfa0`: `L[j] = sum_i R(V[i] * B'[j][i])`. It then zeros `L[2]`
   (`463f74`) and `50e0c0` computes
   `C[i] = sum_j R(L[j] * B'[j][i])`. `463f90..463fce` adds that lateral
   carry to slip: `S'[i] = S[i] + C[i]`. For order 5 or zero speed,
   `S'=S` on this branch. The helpers round each product separately and use
   wrapping dword sums, not one floating dot product.
4. **`P'[i] = P_before_translation[i] + T((V'[i]+S'[i])*dt,1000)`**.
   `464020..464061` forms the velocity-plus-slip sum; `464075 -> 412d10`
   performs signed 64-bit multiply/divide; `4640d7` publishes the position.
5. Afterwards each nonzero slip component becomes
   `T(S'[i]*(2000-dt),2000)` (`4640e0..464108`). This decay does not reduce
   the slip already used by this translation.

The receipt freezes the transition consumers; the matrix helper formulas are
also checked in `50dfa0..50e1dc`. These equations describe
the selected ordinary branch, not attached/docked/special-class alternatives.
`flags2 & 02000000` skips the entire ordinary object integration, not only
angular slew. The test at `4632b3` branches at `4632ba` to `464d66`, which
advances `EBX` through the object list and returns to `463150` for the next
object. Thus basis, physical velocity, translation, slip decay and publication
are also bypassed. Earlier command/scalar-prefix effects are separate: do not
undo a speed write already performed by the command. The targeted
[static check](../../../verification/results/fl-arrival-entry/integration_skip.py)
and [receipt](../../../verification/results/fl-arrival-entry/integration-skip.json)
verify the two instruction-boundary spans against the original EXE. This is
static control-flow evidence, not a new native execution. A speed-only fixed
host control does not prove the skipped motion behavior; use nonzero rate/slip
when qualifying it. Other special branches remain outside this bounded policy.

AP `e_live_motion`'s floating `speed * flight_forward + slip` is **not** an
exact substitute for physical velocity plus slip at E: physical velocity is
from the prior integration; the speed/basis may have changed; native Q16
rounding differs even for `v=1,F=32768` (native 1, float 0.5); and the pending
integration can add lateral carry. A zero-speed post-command pose can still
translate from nonzero slip. The receipt's lateral-slip example moves 100 raw
units in a 100 ms update despite zero gate-forward x displacement. These
are falsifiers, not positive runtime qualification.

## 3. Gate forward and the existing warp Trajectory

Warp setup reads **the gate root's row 2**, gate `+70 -> +60/+64/+68`, at
`44d75e`. Each running order-21 update independently follows
`A.flight+224 -> gate+70 -> root+60` at `45f144..45f150`. It does not use
the ship flight forward row and does not save a permanent gate-forward copy.

For negative deadline difference, let `r=deadline-now`. The command writes
`P_warp[i] = anchor[i] + R(d(r)*F_gate[i])`, with
`d(r)=T(0x1967e8 * r*r,1000000)`. On the intended integer `r=0..900`
domain the scalar maximum is **1,348,650**. This explains the existing
`ship::Trajectory` interval. Its monotone endpoint proof additionally assumes
the same qualified gate forward for the whole curve and separately bounded
extra root motion; the scalar constant grants neither premise.

The ordinary root angular writer `463de3/463df1` is not ship-only; class-7
flight-basis publication branches occur afterwards. It can therefore update
a gate's physical basis on a qualifying moving/rotating object path. Other
pose setters/commands are outside this finite trace. No general immutable
gate-forward contract was found. A current copied gate pose is appropriate
for current geometry; retaining it for a future interval needs writer scope.

The stationary-gate E admission therefore checks both **current angular rates
`B+24/+28/+2c` and remaining angular commands `B+30/+34/+38`**, in addition to
translation. The [targeted stationary-input producer](../../../verification/results/fl-navigation-join/verify_stationary.py)
and [receipt](../../../verification/results/fl-navigation-join/stationary.json)
verify 21 instruction facts in the original FL image. Without autopilot,
`46397d/463997/4639b3` consume the three current rates. With autopilot, the
non-class-7 route at `4632e9` reaches the remaining-command consumers
`463331/463433/463536`; each uses its corresponding current rate and `444500`
step, before the common root composition/normalization. Nonship angular getters
select their own class/type record rather than hardcoding zero. A zero current
rate alone consequently does not qualify an idle pending angular command.
This is a current-input admission rule for the initial stationary-gate policy;
it does not prove that later callbacks cannot change that pose.

The current `Trajectory` API cannot stand for ordinary E travel merely by
passing current position and gate forward. `extra` is an already proven
motion bound, not a free slack knob. Passing `forward=0,extra=0` only bounds
the copied current root and establishes no warning-suppression horizon.

## 4. Scheduling, reevaluation and result lifetime

Outer loop `477ba0` runs ready tasks at `477ebf -> 4788b0`, then
`477ed2 -> 443b10`. That driver first loops all active sectors with
**collision `443b42`, commands `443b49`, post-pass `443b50`**. Only after
that first sector loop does its second loop call integration `443b76`.
Outer follow-up is `477eed -> 464fd0`; the separately qualified input driver
is later at `478150`, as established by the
[input phase note](prefill-ready.md#3-exact-input-read-admission-callbacks-and-exceptions).
This E therefore precedes pending native movement; it is not a check of an
already finalized velocity segment. Commands in later sectors can also run
before the current ship's integration.

Concrete intervening writers include the command speed slew's
`462ec7` store, fed by desired speed, native max-speed getter and TShips
acceleration (`462d63..462e82`). Automatic order zero sets desired speed zero
at `4628a5` unless `flags2 & 4`; it does not forcibly zero a nonzero current
speed. Root/flight angular writers then run in integration. Native callbacks
include completion `46a584`; gate post-pass can initiate another pose/order
transition through `4650af -> 44d3b0`. A call-free **copy** borrow excludes
none of these writers after the helper returns. These are concrete missing
future-writer premises, not a recommendation for a new observer.

For proximity callers, a false E only omits this pair's warning publication.
The warning list/bits remain visible until the next native collision reset;
there is no stored false-pair lease. Consequently absence of a warning can
affect consumers during the rest of the update. The next reset/requery is a
scheduling event, not a demonstrated 900 ms or fixed-real-time deadline. This
trace does not reconstruct every VM consumer of the warning bits and must
not claim that omission has no persistent task consequences.

Order-12 command rescoring is specifically different: at `460334/46033a`
it skips work while `now < flight+210`; it publishes `now+200` at
`460355/46036f`. E false deletes an obstacle row at `4609d4`; the command
can complete through `460a6d -> 45db62 -> 46a550`, steer at `46152a`, and
write desired speed at `461558` and subsequent alternatives. Those values
persist through skipped rescores. A 200 game-ms deadline also does not mean
200 real ms or exactly one frame under SETA. **Keep both order-12 E paths
out of the first positive policy.**

## 5. Minimal next policy and explicit blockers

**Scope revision:** section 6 records the parent's later ratification of
ordinary E as a repeated engineering warning policy. The physical-future
requirements below remain requirements for claiming a physical trajectory
certificate; they no longer block that revised ordinary warning-policy scope.
The static writer/cadence findings above remain unchanged.

The useful native candidate is **`457db5`, collision-pass provenance,
current order zero, actual class-7 A and class-18 gate B**, with ordinary
motion/source/tree/owner admission. This includes ordinary coasting state;
it is not tied to a capital roster or an invented speed cap. Native E itself
returns false when both speed and desired speed are zero (`457611/457617`),
so a stationary-only positive control would not demonstrate the enhancement.
Completion's order-zero store provides an actual post-warp producer of the
state, but its callback means post-warp identity/order must be checked fresh.

**This candidate is not yet an authorized suppression interval.** The remaining
small decision is how long omission of this gate warning must be safe, and
which actual native command/callback scope fixes or bounds translation until
that reevaluation. The current E position/velocity tuple alone does not answer
it. Implementation obligations are:

1. Distinguish actual collision-pass `457db5` from both order-12 routes using
   qualified caller/scheduler provenance; preserve the already qualified E
   entry ABI (`ECX=A`, stacked B/distance/output, `ret 12`), flags/GPR/XMM,
   LastError/MXCSR and four-byte-stack envelope. No unproved return-address
   check alone may create future phase authority.
2. Perform a fresh finite call-free Copy for A and B; finish all payload copies
   before a decision. Keep no native pointer/lease across return. Reject nested
   helper use and active callback routes under the existing owner contract.
3. Supply a **derived ordinary root interval** for the actual warning-decision
   horizon. It must include intervening speed/heading/slip writers, the native
   per-object delta and rounding, gate motion, no-wrap checks, and task effects
   that outlive a pair-list reset. Reusing the warp curve or an arbitrary speed
   ceiling does not supply this. If that finite writer closure cannot be shown,
   preserve native warning for the individual pair.
4. Use the complete current gate/ship enclosures inside that interval. Test a
   real admitted moving order/caller positive, a true native warning that the
   new geometry suppresses, plus changed speed/heading/slip/dt, post-completion
   callback, order-12 rescore, new warp, gate pose change and overflow refusals.
   Synthetic geometry positives alone cannot qualify native motion authority.
5. Final gate-only authorization still belongs after prospective legacy
   suppression selection and before accounting/publication. Do not publish
   suppression then discover a motion/copy refusal. Foreign unwind is outside
   ordinary false-return behavior; inherited retirement rules remain required.

There is no new per-draw work or hook proposal. Future implementation should
reuse one bounded copied scalar set and checked integer motion arithmetic,
without a sector scan, allocation, logger/native callback, wait or new lock in
E. Static counts do not measure game cost or Windows/CrossOver execution.

### Scalar envelope extension: conditional, without an arbitrary speed cap

The final ordinary velocity publisher `463f1a` consumes signed current speed
directly; it does **not** clamp to the maximum-speed getter. The command
second pass at `462d65/462d76` clamps desired speed only from above, then
optionally to nonnegative flight `+278` at `462d83..462d92`. Negative desired
speed has no symmetric lower clamp in that pass. Its slew at
`462e82..462ec7` can leave existing overspeed above the getter while slowing
down. Native getter `45a450` itself includes existing speed factor `+274`
and timed FL factor `+914/+918`; reuse the
[qualified getter contract](ship-steering.md), not bare TShips
speed. The separate special `flags & 800000` integrator branch computes and
caps speed at ten times its getter result (`4631c7..4632ae`) but jumps to
`46412b`, skipping ordinary translation. It is no ordinary final speed cap.

There is nevertheless a useful **conditional** bound from actual scalars.
Assume a pending ordinary order-zero transition with no intervening writer
beyond the selected self command/integrator, no extra slip writer, no native
overflow, and a proven future flight-matrix component bound `|B'[j][i]|<=2Q`.
Let `W=sum_i abs(V_old[i])`. For automatic order zero with `flags2 & 4 == 0`,
take `C=abs(E.current_speed)`: desired becomes zero and the self slew stays
between that speed and zero. For manual order zero, a nonnegative getter and
the existing upper clamps do not increase the maximum magnitude beyond
`C=max(abs(E.current_speed),abs(E.desired_speed))`.

Product-wise rounding gives `abs(V'[i])<=2*C+1`. The lateral-carry transform
gives `abs(L[j])<=2*W+3`; after dropping `L[2]`, each transformed carry is
at most `8*W+14`. Hence a conservative ordinary pending-update bound is
`abs(U[i])<=2*C+abs(E.slip[i])+8*W+15`, and per-axis displacement is at most
`floor(bound[i]*dt/1000)`. This includes arbitrary future heading in the
admitted matrix domain and derives its speed bound from actual state.
Checked arithmetic must also prove the original native dword sum and all
trajectory guards cannot overflow. It must not silently saturate them.

This is an inferred arithmetic envelope, **not an established native E
authorization**: other objects/sectors can run command callbacks and gate
post-pass before integration, current order may change, and warning consumers
can retain decisions. A current copied matrix does not establish the required
future flight-matrix writer bound. Resolve those finite premises and the
decision horizon before using this formula as `Trajectory.extra` or a new
ordinary interval input. Its existence does not close the section-5 blockers.

### Shared post-B speed prefix and finite first-step gap (2026-10-31)

The order-five [static producer](../../../verification/results/fl-lane-handover/order5_speed_domain.py)
and [receipt](../../../verification/results/fl-lane-handover/order5-speed-domain.json)
also check 14 additional shared-prefix instruction contracts and 16 selection
boundaries. The hashed second-walk region includes these instructions. These
are measured static/arithmetic checks, not native execution. This prefix is
shared by class-seven ordinary, escort/follow and held commands; its selection
does not test the current order number.

The second bucket walk admits classes 7/19/10 at `462d26..3a`, then skips the
whole scalar update for own `obj+40 & 0x800200` (`462d40..47`) or own
`obj+44 & 0x02000000` (`462d4d..54`), branching to `462eca`. For the admitted
class-seven route, `462d5a` saves current speed before the following selection:

```
desired = min(selected_B_desired, native_maximum)  # 462d65..76
if signed(flight+278) >= 0:
    desired = min(desired, signed(flight+278))    # 462d83..92
if (own_flags_40 & 0x180000) == 0x180000:
    if (own_flags_40 & 0x10000000) != 0 or
       ((uint32(now - uint32(flight+180)) & 0x80000000) != 0):
        desired = 0                             # 462dc4
        remaining_yaw = remaining_pitch = remaining_roll = 0  # 462dcb..dd9
```

Both masks in the suppression predicate belong to **own obj+40**, loaded at
`462d95`; the `0x10000000` test at `462da6` does not read obj+44 or the gate.
The clock reads current game time from `5a83b0 -> +708`; `462dbc` subtracts
flight `+180` as a dword and `462dc2` uses JNS. Preserve that modular sign
test: widening the subtraction and comparing signed mathematical values is
different across wrap. Equality does not suppress. The subsequent scalar
slew at `462de0..462ec7` consumes the resulting desired value. Suppression
clears remaining commanded angles, not current speed or angular-rate fields.
The outer skip leaves current speed and remaining angles unchanged; it is not
equivalent to a zero desired speed.

**Observed source/fixture gap at discovery.** The finite path's first update
in `navigation_command_path.h` slewed `command.speed_intent` directly and used
the copied command remainders. Its live B intent was sampled before this
prefix. `observe_traffic` already copies own `obj+40/+44` within its 0x100-byte
object payload and publishes `+40` as `Observation::flags`, but discarded
`+44`. Its flight payload `+1e0..+25c` contains neither `+278` nor `+180`.
No downstream finite admission excluded own `obj+44 & 0x02000000`:
`acquire_hull` excludes own `+40 & 0x08800202`, while `ship_live::read` does
not supply an applicable obj+44 refusal. Thus the missing branches are not
eliminated by the existing ordinary hull qualification.

The old original-instruction `ls_slew` entry omitted `462d5a..462de0`.
Passing that narrowed fixture established its copied slew, not the complete
B-selected first step through native speed selection. The expanded fixture
is being integrated by its owner; no acceptance of its new prefix cases is
claimed by this static receipt. A derived scalar witness is current=1000,
selected desired=2000, acceleration=1000 and raw dt=100 ms: direct slew gives
1100, while optional cap=0 gives 900. With current=0, the corresponding
results are 100 versus zero, also selecting different lateral-carry branches.
The A desired-speed family can cover the scalar zero-to-M range, but a family
with fixed remainders does not by itself cover suppression's angular clear.

**Minimal shared correction.** Acquire finite-only optional cap and the
suppression inputs/derived boolean within the existing `Copy`; own flags2
is already transported in the object payload. Before first-step slew/angular
integration, apply the effective native desired and, when suppressed, the
zero remaining angles (including held-command remaining-angle state). Either
model the exact outer skip as unchanged current speed/remainders, or refuse
that specific route while it has no ordinary recurrence proof. Reuse the
same selection for ordinary, escort, held and order-five finite producers;
an order-five-only fix leaves the existing shared gap. This requires no new
hook, callback, observer, cache, lock or allocation. Copied scalars remain
invocation-local; keep the established helper borrow, native boundary and
exception limitations. Future conditional model steps retain their separate
policy obligations; this finding establishes the pending first-step inputs.

## 6. Native warning forecast and revised ordinary-E policy scope

The parent subsequently ratified ordinary E as **repeatedly evaluated warning
geometry matching the purpose of the existing AP policy**, not a theorem that
all subsequent command/callback motion stays inside its prediction. The 900 ms
physical warp curve remains arrival-only. No arbitrary future callback graph
or global speed cap is needed to describe this revised warning policy.

This subsection independently traces complete FL predicate
`457600..457ccb`, its proximity caller and recursive part-box routine
`4572c0..4575fb`, reusing the corrected AP comparison in
[navigation AI, section 10.14](../ship-navigation-ai.md#q1-the-part-box-test-0x0044d9c0-and-the-warn-test-around-it-m).
The updated producer checks the new instruction/constant anchors. Its complete
range hashes and reviewed operand traces support the derived formulas; the
receipt's interpretation fields are derived findings, not runtime observations.

### Native inputs, branch and return

At selected normal call `457db5`, ECX=A and the three stack arguments are
B, supplied centre distance `d`, output pointer. The caller forwards `d`
unchanged. In the collision path, `46723f..4672a0` calculates it from the
physical `+30..38` centre differences using single-precision squared length,
native square-root helper `412a80`, then `cvttss2si`. Thus `d` is an integer
centre distance with native floating/truncation behavior, not a path length,
time or independently copied target distance.

The predicate reads current speed `A+10`, desired speed `A+14`, acceleration
getter `45a610(A)`, radii `A/B+a4`, current order, physical centre positions,
and heading. **It does not forecast from A's physical velocity `+100..108`
or A's slip.** Desired speed only participates in the initial both-zero test.
B's speed and slip are used for its motion/behind tests. Targets `flight+224`
are read for attack/escort/follow relationships and the non-gate asteroid
exception; no `flight+240` target coordinates or module detour point enter
this predicate. A geometry policy that uses such coordinates is an explicit
module extension.

For A class 7, heading is flight `+870` row 2, i.e. `+890..898`
(`4577c9..4577d8`). Other classes use physical `+40` row 2. Acceleration for
class 7 is loaded TShips record `type*db8 + [5a83e4] +48`, multiplied with
Q16 rounding by `1 + flight+274` when that factor is nonzero
(`45a642..45a67b`). The predicate rejects only acceleration **zero**; the
enhancement should require its own positive, finite admitted acceleration
domain rather than silently interpreting a negative native result as valid
deceleration. The timed max-speed factor `+914/+918` is not read by this
acceleration getter.

Mesh-required B is exactly class 5/6, capital class-7 accepted by `4496e0`,
or class `14` with subtype `26..2a`, `2d..2f`, `7c`, `7d`
(`457635..45768c`). `4496e0` itself first requires class 7.
**Gate class `12` therefore has mesh-required=false.** It takes the close
sphere and cone rules below. Do not describe `abs(speed)+b` or the native
part-box call as the gate's actual branch.

Native result is EAX boolean. All FL returns pop 12 bytes. An early true
sphere result does not write output. Mesh hit writes the selected part to
output, recursively returning at the first hit; false leaves output untouched.
Normal proximity never reads its output local. The helper must not call the
native recursive part-box routine as a new predictive/reentrancy capability.

### Exact gate sphere/cone forecast in its ordinary non-wrapping domain

Use `R`/`T` from section 2. Let `rA`, `rB` be positive object radii and
`rr=rA+rB`. Define attacking state as order `10`, `11`, or order `12` with
`flight+264 > 0`. These are numeric native order classes, not inferred task
names. Let `vA=abs(A.speed)` in a domain excluding signed-absolute overflow.

1. If current **and** desired speed are zero, or acceleration `a==0`, return
   false (`457611..45762c`). A zero-current/nonzero-desired ship still runs
   the rest of the predicate, but desired magnitude adds no reach.
2. The close rule applies when `rB >= T(3*rA,4)`, B is station/class `11`,
   or `45a450(B)==0`. An attacking A against its actual target B returns
   true for `d<=rr`; otherwise a non-mesh B returns true for
   `d<=R(rr*0x13333)` (`4576e5..45776f`). The fixed multiplier is
   `1.1999969482421875`, the Q16 approximation to 1.2.
3. Start margin `M=clamp(rA + (attacking ? 0 : T(rB,10)) +
   (order==12 ? rA : 0),15000,500000) + rr` (`457776..4577c1`).
4. Let `delta=B.position-A.position`. Unless **all three** absolute
   components are at most 100, `432b30` normalizes delta to Q16 using
   double-precision square root, reciprocal scaling and truncation; in the
   near-centre case it substitutes A's heading row. The Q16 bearing cosine
   is `c=sum_i R(unit_delta[i]*headingA[i])` (`4577e0..4578b0`).
   **There is no subsequent clamp of c to Q.** Raw row norm and per-product
   rounding therefore need their actual checked bound; in the near-centre
   case this is a rounded row-norm-squared, not necessarily a unit cosine.
5. If `d>rr`, `c<=0`, B speed is zero and all B slip components are zero,
   return false (`4578b4..4578cf`). This precedes the later cone.
6. Initially `b=0`. When B speed is nonzero, its signed closing contribution
   is `w=-R(abs(B.speed)*sum_i R(unit_delta[i]*headingB[i]))`, but only
   when `c>0xcccc`, or A is not attacking, B is class 7, its `flags2 &10`
   is clear, and `c < -0xcccc`. `449690(B)` doubles positive w for its
   large-ship set (`4578d5..4579bd`). For an ordinary stationary gate this
   term is zero. The native scalar uses absolute B speed along its heading;
   it does not project B's physical velocity or slip.
7. If `c>0`, add `R(c*rA)` to M. If also `vA!=0`, set
   `b=T(T((w+R(vA*c))^2,a),2)`; otherwise the initialized contribution
   remains `b=w` (`4579c1..457a23`). Thus unusual zero-speed/moving-B or
   behind cases can leave a speed-valued w used as a distance. For positive
   a and stationary gate, this simplifies to projected stopping distance.
   Cap b from above at **300000 raw units** (`457a25`). Escort/follow
   relationship halving applies only against class-7 B, so it does not
   alter a gate query.
8. Reject if `d > b + M + rr` (`457ae4..457af0`). **rr appears twice**
   because M already contains rr. Preserve that observation when comparing
   against older shorthand formulas.
9. For non-mesh gate, `d<=b` returns true. Otherwise let the native float
   `cf=float(c)/65536` and `qf=float(rr+20000)/float(d-b)`. Return true
   exactly when `cf>=0` and `cf*cf > 1-qf*qf`; equality refuses
   (`457bf6..457c91`). The producer checks constants zero, one and Q16
   conversion at `591974/591aa0/591994`.

An outward cap on the native outer range in this admitted non-wrapping domain
is `800000 + 2*rr + max(0,R(c*rA))`: 300000 braking cap plus 500000 margin
cap and the two rr terms. Replacing its last term by rA assumes `c<=Q`,
which the predicate does not enforce. Use the exact checked copied current c
or a separately justified outward bound. Native products use signed 64-bit
multiply, Q16 rounding then the low dword; c/closing/margin additions are
wrapping dword additions. Braking squares the signed dword closing sum and
uses signed `idiv a`, followed by signed division by two. An authorizer must
reject absolute-value overflow (`INT_MIN`), rounded-product/sum/quotient
overflow and final range overflow, rather than saturate and claim native
equivalence. The gate predicate has no lower brake clamp or positive-a check
to rescue values outside that admitted domain.

Native units are 500 per metre. The above caps/margins are therefore braking
cap 600 m, cone radius padding 40 m, and pre-rr margin clamp 30..1000 m.
These are **distance** parameters. The gate predicate contains no fixed
one-second, one-frame, 200 ms or 900 ms forecast lease. Its use of braking
distance expresses warning intent, not enforcement of later motion.

### Part-box branch, for comparison and inflation provenance

For mesh-required classes, calls `457ad1/457c22/457cb4` use A in ECX,
stack `(B.root, abs(A.speed)+b, out, 0x13333, 0)`, `ret 20`.
`4572c0` first recursively visits every child (`+c` list/sentinel), then
requires model ID `node+144 >=0` and the current model from `4a1a40`.
It uses extents `e_i=+20/+24/+28` of the **first** model record
`[[model+c]]`, uniform node scale `s=+70`, and per-axis half extent
`H_i=R((R(e_i*s)+rA)*0x13333)` (`457322..4573cc`). This is per-axis
inflation of the box plus A's scalar radius; it is not a 1.2 multiplier of
an arbitrary ship enclosure or a native gate mesh test.

The current A root world centre `+b0` minus node world centre `+b0` is
transformed into the node frame using node world basis `+c0`
(`4573da..45741b`). Strictly inside all three slabs returns true; otherwise
flag zero checks single-precision squared distance to this expanded box
against reach squared (`45746c..4574d3`). It then constructs A's heading
times reach and transforms that into the same part frame. The compiled slab
step uses **integer division** (`457599`), producing per-axis intervals
`[-abs(q),+abs(q)]`; these all contain zero and cannot reject on nonzero
direction components. A component rounded to zero can reject when A lies
outside that slab. This corroborates the corrected AP interpretation: it is
primarily a distance-to-parts test, not a reliable forward segment cast.

### Policy implications and temporary deferrals

The existing AP module's gate policy is deliberately broader than that native
gate predicate. `ship_gate_lane_core.h::gate_ring_why` uses engineering run
`v * (1 + clamped_game_step_seconds + v/max(a_lin,0.1))`, direction from
current velocity (nose below 1 m/s), and a second ray toward the known target,
capped by target distance. Its glue's target is the current leader position
for order `14/15`, or flight `+240` for order 4 targeting this gate.
`e_live_motion` currently supplies the speed-times-flight-forward proxy plus
slip. These are **module policy inputs**, not native `457600` operands.

The ratified FL policy may use actual physical velocity plus slip (section 2)
and explicit target/detour rays as engineering prediction, with a documented
stopping/frame allowance. It should preserve or deliberately enlarge the
native warning intent described above; it must not claim a one-second native
gate lease or substitute the arrival-only 1,348,650-forward curve. Whether to
include both native scalar/heading and actual-motion rays, and the exact
allowance, remains the parent's policy choice. Fresh current enclosure and
identity/read-copy refusal still remain hard prerequisites. No copied state
may grant a later native read or cached physical safety certificate.

Order-12 paths remain a **later port requirement**, temporarily deferred from
the first implementation because their native list/rescore consequences
require their own acceptance; this is not a permanent feature exclusion.

## 9. Arrival-only future writers and the continuous native warp profile (2026-10-30)

This bounded follow-up starts at the already established `44d729` arrival
seam and ends at order-21's completion **position snap**, before its wake
callback. It preserves native axial P and changes no policy or production
code. The [operand producer](../../../verification/results/fl-arrival-policy/verify_future_writers.py),
[derived anchors](../../../verification/results/fl-arrival-policy/future_writer_facts.py)
and [compact receipt](../../../verification/results/fl-arrival-policy/future-writers.json)
check the original FL image: **112 operand facts, ten gap-free selected regions /
2,075 instructions, six changed-operand refusals, three dispatcher case mappings,
and 1,001 legal-dt zero-input arithmetic witnesses** (measured host/static counts).
Run `python3 verification/results/fl-arrival-policy/verify_future_writers.py`
and `python3 -m py_compile verification/results/fl-arrival-policy/verify_future_writers.py verification/results/fl-arrival-policy/future_writer_facts.py`.
The 1,001 cases check derived arithmetic, not native execution or arbitrary
callback schedules. Ghidra's existing read-only `FLRegistry/X3FL.exe` project
corroborated the reset, insertion, setters and publication function boundaries
using `X3DecompileFunctions.java` and `X3FunctionContext.java`. Raw output stays
untracked at `~/.cache/x3m-port/fl-arrival-future-writers-local/`.

### 9.1 Root attitude: no unconditional freeze, but a finite zero-input invariant

Distinguish gate **object angular rates `+24/+28/+2c`** from gate **physical
forward row `*(gate+70)+60/+64/+68`**. Neither denotes the ship's flight row.
The arrival body first selects flight attitude mode through
`44d455 -> 459fe0(0,1,0)`, then writes root local position at `44d6b5`, root
basis through `44d6b9 -> 50d400` and `44d6d2 -> 50ebe0`, and derived object
Euler angles through `44d6eb -> 50d8e0`. It does **not** clear ship object
angular rates `+24/+28/+2c`, slip `+60/+64/+68`, or old physical velocity
`root+100/+104/+108`.

`459fe0` writes flight target offsets `+8b8/+8bc/+8c0`, and, when its reset
branch runs, copies them to current offsets `+8a0/+8a4/+8a8` and clears offset
rates `+8ac/+8b0/+8b4`. However, `459ff6/45a003` returns early when the requested
mode already equals `flight+860` and the force argument is zero. Therefore the
seam must inspect the actual current/target values; a call with `(0,1,0)` is
not proof that offsets were reset or that object angular rates are zero.

Subsequent insertion `44da0f -> 452b70` clears object remaining angles
`+30/+34/+38` at `452c0e/15/1c`, and stamps last-update `+a8` at `452c29`,
**only when the old container differs from the destination sector**
(`452b83/85`). It leaves object angular rates and slip unchanged. An actual
container change is therefore part of the restricted first-step contract;
do not borrow its clearing/stamp effects from an early-return case.

Order 21's running block `45f10d..45f1c4` writes physical position and zeros
current/desired speed through `45f096/09a/0a1`; it does not write root basis
or angular inputs. The selected ordinary integrator does not exclude order
21 or flag `400` from angular integration. In a direct-rate branch
(`4632c0/cc`, or non-autopilot `4632d8/de`), the preserved rates feed
`46397d/997/9b3` and can produce a nonzero Euler increment, then root compose
`463de3` and normalization `463df1`. For example, a qualifying direct-rate
object with rate 16,384 and dt=1,000 produces angle 16,384 (derived exact
integer arithmetic). This is an ordinary branch counterexample to an
**unconditional** frozen hull; it does not assert that an autopilot path with
zero remaining angles retains that same rate. Autopilot's zero-remaining
branches explicitly clear rates at `463430/46352f/463779`.

A sufficient restricted invariant for a **nonplayer**, ordinary unattached
ship is: object rates and remaining angles all zero; flight current offsets
equal their three targets; stable route/offset inputs; no separate pose,
rate, offset or topology mutation. Direct-rate products stay zero for every
legal dt `0..1000`. Autopilot zero-remaining branches generate zero steps and
clear their rates. Equality tests `4635d5..4635fd` skip offset slewing; hence
all three effective root increments stay zero and `463d34` skips root
composition/normalization. Nonplayer admission avoids the player heading/bank
feedback branch `4637d0..46395e`. Equality need not mean a zero offset: the
root is unchanged on the no-increment path, though flight basis may differ.
The certificate must use the **actual new root matrix at this seam**.

This proof excludes special/attached/docked branches, route-changing flags,
external pose commands, and changed flight targets. It requires ordinary
non-special flags (`800000`/`8000000` object and `02000000` flags2 alternatives
are outside this recurrence), unattached/docked-link absence and the qualified
source/owner domain. It is a conditional native recurrence, not a new completed
bit or a lifetime lease lasting 900 ms.

### 9.2 Root position and gate-forward invariants

The same continuous profile needs more than zero angular inputs. Arrival
publishes speed 60,000/120,000 at `44d9ee`. With actual sector insertion,
last-update is the current game clock, making the **first** ordinary integration
dt zero if that clock is unchanged. Later running order-21 visits zero both
speeds, and the common speed slew preserves zero. However, dt zero alone does
not prevent slip creation: `463f6c -> 50dfa0`, zero local-z at `463f74`,
`463f8b -> 50e0c0`, then `463fce` adds old physical transverse velocity to
slip whenever initial current speed is nonzero. Identity flight basis and old
V=(1000,0,0) produce slip=(1000,0,0), even at dt zero; decay at dt zero leaves
it intact (derived exact witness).

A simple sufficient arrival input is **old physical velocity zero and object
slip zero**, actual sector transition, and first-integration dt zero. Then the
first lateral carry is zero despite launch speed, and every later running
order-21 update has current speed zero before integration. Velocity and slip
contribute no extra root translation. Without these premises, the segment
`P+r16(d*F)` does not enclose all intervening published roots; an individually
proved extra-motion bound or conservative refusal is required. Merely copying
P and F cannot supply that bound. The final position snap at `45f104` is in
scope; ensuing wake `45efcc -> 46a550`, callback `46a584`, and ordinary
post-completion motion are outside this arrival-only interval.

Initial placement reads gate F at `44d75e`; each running update independently
loads the **current target pointer and current gate root** at `45f144/147/14d`,
then F at `45f150`. Under unchanged ship/flight/target/gate/root identities,
unchanged P/deadline, and fixed F, all updates and the final snap use the
established `0..1348650` scalar interval and native rounding. For class-18 gate
on the ordinary unattached path, zero current angular rates **and** zero
remaining angles close the angular recurrence as above (there are no ship
flight-offset branches). Gate current speed zero and slip zero close ordinary
translation; old physical velocity is overwritten from zero speed and the
class-7 transverse-carry branch is absent. Gate class 18 is also outside the
command speed-slew class set 7/19/10 at `462d26..462d3a`.

Thus the supported **continuous native warp** mode may retain a fixed gate
frame and fixed ship root attitude under these input/writer restrictions.
No initial-zero test licenses later explicit reorientation, relocation,
retargeting, source replacement or topology mutation. Those mutations are an
explicit boundary of this finite mode, with native fallback for an unsupported
read; this study proposes no VM observer, 900 ms lease or cancellation system.

### 9.3 Supported native pose commands are the explicit external boundary

This boundary is concrete native behavior. Registered SA dispatcher `46a640`
uses table `473dc4`; **case 40 -> 46c193** resolves an object at `46c19f` and
calls position setter `4589c0` at `46c1b7`; **case 48 -> 46c203** resolves at
`46c20f` and calls attitude setter `459430` at `46c227`. Null resolution is
checked; there is no class-18 or order-21 refusal on either route.
`4589f5/fe/458a07` writes root local position and `458a11` copies it to root
world position. `459477 -> 50d400` changes root basis and `459486/491/49c`
publishes root world rows. Neither setter needs nonzero object angular rates.
Case 6 also reaches sector insertion `46c17b -> 452b70`.

The existing scheduling closure remains sufficient for **one call-free copy**:
ready tasks run synchronously at `477ebf -> 4788b0` before the next sector
driver `477ed2 -> 443b10`. Suspended tasks cannot interrupt that copy, but a
ready pose command can run between successive warp iterations. This is why a
fresh zero snapshot cannot prove arbitrary future scripts harmless. Such an
explicit native pose command is outside the continuous-warp profile, just as
native axial P itself cannot preserve its intended curve across an explicit
teleport. No whole-engine exclusion or foreign-call hypothesis is needed to
state that limit. Completion and launch wait callbacks are likewise native
continuation boundaries, not pointer-lease renewals; all helper borrows end
before returning to `44d72f`, long before the existing `44db72` callback.

### 9.4 The arrival seam has two different poses, not a coherent new world tree

After `44d6b5/44d6d2` and before `44d729`, only root **local** P/basis and object
Euler values have changed. The math leaves do not recurse through child lists;
no native root/child world publication occurs in this interval. The root world
`+b0..b8/+c0..e8` and descendants may still carry the preceding world pose.
Subsequent `452d7d/88/93` and `452da1` copy **only root** local pose into its
world fields, after the initial warp offset. They do not republish descendants.
The independent recursive publication is `4951f0`, which copies root local to
world at `495220..495247` then calls `494370` at `49524e`; that function performs
the accepted recursive child transforms. Ghidra gives its direct caller as
`48b87c`, not any launch instruction.

Consequently an ordinary valid arrival can fail the current E reader's exact
root-local/world and child-to-current-root checks. A simple derived witness is
old published root x=0, child local x=100/world x=100, then new root local
x=1000: the stored child is coherent with the **old world root**, not the new
local root. Copying that child world as its future displacement is incorrect.
The arrival reader must derive future supports from qualified **sourced local
hierarchy**, actual new root basis and the future P interval. Preserve an
independent complete construction/publication witness: a completed old world
root/subtree can be checked against its own copied published pose, with only
the recognized root-local transition treated separately. This is a proposed
seam-specific reader requirement; existing E `enclose`/`ship_reader` currently
require their original coherent-pose contract and cannot simply be called
with those checks disabled. A missing or inconsistent old publication, changed
local source, extra/missing body, or unexplained root difference must refuse.
This study does not claim that an owner token alone proves construction or
that every native arrival's old tree necessarily passes such a check.

The [stock enclosure](ship-enclosure.md) already bounds the
selected fixed source keys and arbitrary admitted turret rotors, including
quaternion mode, no-gamma rotor norm, optional event/target refusals, complete
hierarchy and every intermediate no-wrap guard. Those facts can be reused
conditionally with this frozen root; they do not freeze unsupported scale,
parent, key or copy-world modes. Existing per-composition allowances remain
`eM=9/(2*65536)` and position error `<2.6` raw units, with exact-identity
composition treated exactly. A six-support implementation still owes its
outward arithmetic derivation and useful stock controls; this receipt supplies
no new support coordinates or numerical-budget claim. The original 8,224
forward reserve is not blanket authority for a new projection calculation.

No new hook, native call, lock or borrow lifetime is introduced here. Existing
seam ABI/register/flags/LastError/MXCSR, normal-return and foreign-unwind limits
remain those in [traffic](gate-traffic.md). Reentrancy is admitted
only through the existing recognized owner/callback boundary; no model/node
pointer survives the helper. Static operand proof and arithmetic witnesses
are not assembled boundary, native collision, Windows or user-flight evidence.
The bounded reader and conditional support geometry remain implementation and
acceptance work; **arrival is not admitted by this receipt**.

### 9.5 Implementable first-step admission without a future dt-zero predicate

This follow-up **replaces the first-integration dt-zero requirement in §9.2**
with a bounded first-step proof. It adds no predictor, observer, hook or axial
relocation. The [first-step producer](../../../verification/results/fl-arrival-policy/verify_first_step.py),
[operand anchors](../../../verification/results/fl-arrival-policy/first_step_facts.py)
and [compact result](../../../verification/results/fl-arrival-policy/first-step.json)
check **44 facts, six gap-free regions / 124 instructions, 524,290 exhaustive
raw-F component/speed endpoint controls, and 2,002 noncardinal all-dt vector
controls** (measured static/host counts). Run
`python3 verification/results/fl-arrival-policy/verify_first_step.py` and
`python3 -m py_compile verification/results/fl-arrival-policy/verify_first_step.py verification/results/fl-arrival-policy/first_step_facts.py`.

**What the helper can know at the seam.** The actual post-pass walks class-18
objects at `465082`, takes their contained-ship list from gate flight `+54`
at `46509b/09e`, tests flag `80`, and calls arrival at `4650af`. The helper
can check **current `ship+54 == gate`**, and that **`gate+54` is the qualified
class-1 sector and differs from the gate**. These observable values ensure
that continuation `44da07/0c/0f` passes that sector to `452b70` and takes its
actual-container-change path, under the already required stable continuous
profile and native return/callback contract. No future boolean is needed to
predict which insertion branch executes. The inserted ship enters the same
sector whose command pass has just completed: driver `443b49` precedes
post-pass `443b50`, and the separate second sector loop integrates at `443b76`.
Thus its next self-command is not before this first integration.

Insertion stamps `ship+a8` from clock `+708` at `452c29`; integration loads
its per-sector current time at `46301c`, and forms/clamps the per-object
difference at `46319b..4631bf`. A zero first dt is naturally reachable when
those native samples agree. The driver does not itself establish that the
clock is frozen throughout all intervening native execution. Clock refresh
`4ffe30` advances `+708` at `4fff67`; frame-clock helper `50b600` invokes it
at `50b603`, and TI dispatcher also has a refresh call at `4ce193` and explicit
clock setter at `4ce158`. This study does not survey every intervening callback
to rule those out. In particular **the helper must not require an equality
between a current clock and a future integration sample**. The actual native
clamp always supplies an implementable bounded alternative, `0<=dt<=1000`,
including the native negative/oversized-difference cases that become 1000.

**Sufficient copied-value refinement.** Keep old physical velocity and object
slip zero and all the ordinary frozen-attitude/source checks of §9.1. For
this first-step profile, strengthen flight **current offsets and target offsets
to all zero**; and require the **actual new ship root forward row to equal
exactly the negation of copied raw gate F**. Check the three negations in wide
arithmetic; retain the existing `|F_i|<=2Q` and signed-value guards. Current
zero flight offsets select `463e63..463e7d`, which copies the root basis to
flight basis exactly. Frozen root and zero target/current offsets preserve
that equality. These are all seam-readable tests; no loader or native math
call belongs in the helper.

This mode has a concrete nonempty native construction witness. Arrival's
`44d6b9 -> 50d400` uses alpha `0x8000`, beta/gamma zero. Reusing the accepted
scalar CRT translation, nearest rounding gives cos=-1.0 and
sin=-8.742277657347586e-08; Q16 truncation gives -65536 and 0. Its matrix is
`diag(-Q,Q,-Q)`. Composition at `44d6d2` with the gate basis therefore gives
root row2 **exactly -F**, including for noncardinal integer gate rows. This
is a derived nearest-mode arithmetic witness, not native execution or a claim
for every MXCSR mode. Runtime admission checks the actual copied row rather
than assuming that witness. Failure to match is an individual fallback.

**Same segment, bounded extra rounding.** Put `D=1348650`, `Q=65536`,
`v in {60000,120000}`, `k=dt/1000`, and use native signed truncation `T`.
Launch speed is published at `44d9e1/e6/ee`. Because old V and slip are zero,
the first transverse-carry calculation is zero. For each root coordinate,
the initial warp point and first integration then produce exactly:

`X_i = P_i + r16(D*F_i) + T(r16(-v*F_i)*dt,1000)`.

The ideal point has coefficient `d=D-v*k`, which lies in
`[1228650,1348650]`, a subset of the **existing full 0..1348650 warp interval**.
The first step therefore needs no extended axial endpoint and no change to
native P. The difference between X and that ideal point is at most 1/2 per
axis for the initial warp rounding, at most `k/2` for velocity rounding,
and strictly less than one for signed translation truncation: each coordinate
error is `<2`, so Euclidean error is `<sqrt(12)<3.5` raw units. Later running
order-21 commands zero current/desired speed before integration; slip remains
zero, so their original warp-rounding proof still applies. Scope still ends
at the completion position snap, before wake and post-completion travel.

The original finite forward reserve remains sufficient **for this additional
native rounding term**: replace its former `sqrt(3)/2` warp term with `<3.5`,
so `2*4102 + 16 + 3.5 = 8223.5 < 8224`. The first-step term and ordinary
warp term are alternative time cases, not sums. This does not absorb the
separate support/projection arithmetic allowance or establish a universal
native SAT theorem; those retain their existing independent obligations.

The integer publication interval also stays unchanged, more strongly than
the floating segment argument alone. For every integer component F in the
admitted domain, `|r16(D*F)| >= 20*|F|`, while
`|r16(-v*F)| <= 2*|F|`, and nonzero velocity has the opposite sign. Signed
truncation is monotone in `dt in 0..1000`. Hence the complete first-step
coordinate lies between `P_i` and `P_i+r16(D*F_i)` exactly. The existing wide
endpoint plus every-intermediate-child no-wrap guard therefore encloses these
native integer roots without enlargement. The producer exhausts both speeds
and all **262,145** component values `-131072..131072` at the dt endpoints;
monotonicity covers all intermediate integer dt. Products and velocity+slip
sums fit the admitted integer ranges before root publication.

Without the actual -F alignment, zero old V/slip and zero angular updates do
not suffice: a perpendicular flight forward yields 120,000 raw units of
lateral first-step drift at speed 120,000 and dt 1000 (derived exact control).
Do not reuse the same segment for that mode. With the stated refinement,
the current pure geometry's full-segment/no-wrap inputs are sufficient for
this first native integration without a future clock predicate. This is an
implementable reader contract and reproducible arithmetic result; actual
reader wiring, compiled seam preservation, native collision and user flight
remain separate acceptance work.

## 10. Order-12 consumers, tombstones and copied-intent lifetime (2026-10-30)

This bounded follow-up resolves the native consumer question left by section 4;
it does not enable traffic or extend the qualified ordinary-E policy. The
[producer](../../../verification/results/fl-order12-consumers/verify.py),
[operand anchors](../../../verification/results/fl-order12-consumers/facts.py) and
[receipt](../../../verification/results/fl-order12-consumers/evidence.json) check
**141 semantic anchors, ten gap-free selected regions / 1,361 instructions,
and nine rejected operand/control mutations**. Run `python3
verification/results/fl-order12-consumers/verify.py` and `python3 -m py_compile
verification/results/fl-order12-consumers/verify.py
verification/results/fl-order12-consumers/facts.py`. Counts are measured static
facts. The list/timer examples below are inferred branch witnesses, not captured
runs. Original FL image provenance is unchanged from this note's reproduction
section. Read-only/no-analysis Ghidra `FLRegistry/X3FL.exe` places both `460334`
and `4609d4` inside `45c810` (26,350-address function body), with direct caller
`443b49`; `46a550` is a separate 78-address body. Raw decompiler/listings remain
untracked outside this checkout at `~/.cache/x3m-port/fl-order12-consumers-raw/`.
No production edit, build, native execution, Wine, install or game launch occurred.

### 10.1 Actual row routes: an ordinary gate bypasses rescore E

The rescore's `4604cc..460514` dispatches to E `460581` only for class 5/6,
a class-7 capital admitted by `4496e0`, class 20 type `2d..2f`, or **any**
class with signed type `26..2a`. `4496e0` explicitly returns false for a
non-class-7 object (`4496e0/4496e5 -> 449710`). Consequently class-18 gate
**types 0..25** (all 26 canonical FL gate rows) bypass E: `460516..460566`
forms the gate-centre delta and sum of object radii, then joins `4605eb`.
A modified gate with type `26..2a` would take E; class alone does not prove
bypass. No new gate-type production allowlist follows from this receipt.

Thus an ordinary listed own gate is **not** removed by returning false at the
command rescore E. It can be omitted earlier by proximity order-12 E `457dd0`,
before list publication. This distinction is needed when preserving the existing
own-gate warning-episode policy; treating every order-12 row as an E query is a
concrete unsupported adapter model.

### 10.2 Omission shifts append positions; command deletion leaves holes

Collision reset clears warning bits at `466e20` and ship count at `466e38`;
it does not clear each array cell. On order 12, proximity false at
`457dd7 -> 457e08` skips append, distance, `2000` and count writes. Later
accepted pairs therefore occupy the next lower available index; omission can
admit a pair that otherwise fell beyond the five-row cap. Derived example:
accepted candidates `[gate,B,C,D,E,F]` publish `[gate,B,C,D,E]`; omitting gate
publishes `[B,C,D,E,F]`. An index is not pair identity across collision passes.
The order-12 true branch does not set the normal branch's `1000` flag.

At a due command rescore, `460367/46037f` copies the native count into the
scan bound and working survivor count. E uses `ECX=EBX=A`, stacked `ESI=B`,
that slot's native distance (`46057b`) and scratch-output address (`46056b/57a`).
False `460588 -> 4609cd` **does not read output**. True with null output
`460597 -> 4609cd` has the same deletion consequence. `4609d4` writes only
zero to the pointer slot; `4609e1/e2` decrements the working survivor count.
It neither shifts pointers/distances nor stores a reduced native `flight+1e0`.
The original distance remains under the tombstone. An already-null slot also
subtracts one survivor (`4604b8 -> 4609da`). The loop still advances vector
scratch by 16 bytes and pointer by four (`4609e9/ee`), using the original scan
bound (`4609f1..460a00`). Later scoring explicitly skips nulls
(`4612b0..4612b4`) and advances the same indexed scratch (`461352/35a`).
No compaction is hidden in these selected consumers. A reader must validate
`count<=5`, inspect every occupied row, ignore tombstones and keep distance
paired with its original slot. Reading only row zero cannot prove "own gate
and nothing else". The shared `ctl_note_warn` already scans all five bounded
rows and rejects another nonzero pointer; its gate pointer must first be joined
to the current gate identity under the existing owner contract.

### 10.3 Result effects survive a skipped query and can invoke a callback

The deadline check is signed wrapped subtraction `now-*(flight+210)`, then
`46033a -> 462c6e` when negative. Due work publishes `now+200` at
`460355/36f`; this is game time, not a frame or real-time guarantee. The skip
jumps to the next object (`462c72`) and performs no order-12 steering/point/
desired-speed rewrite. The later shared speed slew/integration still run.
Previous native desired speed, angular command and flight point therefore
remain inputs even when the current collision pass has published zero rows.
Derived witness: deadline 1200, current time 1100, current count zero still
leaves the earlier order-12 command intent in force. A new empty list is not
immediate cancellation of the native avoid leg.

Due rescoring consumes survivors and scores to choose a detour, steer or finish.
It can replace `flight+240` (`460b7f`, `461497`, `4614fb`) and preserve a
native start point at `flight+230` (`4614a8`). Steering uses that flight point
through `456810` (`460c43`) or `456450` (`46152a`); the latter publishes
angular commands at `obj+30/+34/+38` (`4567ec..4567f2`). Desired-speed stores
include `461558/593/5c0/614`. Thus current `flight+240` is required native
intent for any future policy that authorizes suppression while order 12 runs;
a retained funnel target alone does not describe this consumer.

With zero survivors and the appropriate score state, `460a25..460a6d` compares
current distance from `flight+230` with `flight+260` (plus the selected
`flight+264`/speed allowance). A strict greater result takes `45db62`: order
zero is stored at `45db68`, then `45db6f -> 46a550`. That routine can dispatch
**indirect callback `46a584`**, passing object context `+80` and reason 1,
caller-cleaned by eight bytes at `46a586`; `46a550` itself is ECX-object,
`ret 4` at `46a59b`. It clears callback token/context `+7c/+80` after return.
No promise that the callback preserves order zero, pose, target, membership,
resource lifetime or module intent follows. This is a real callback boundary,
not a hypothetical foreign writer; no helper borrow may cross it.

### 10.4 Smallest shared-controller contract and remaining adapter boundary

Existing shared policy can retain an own-gate-only funnel/handover warning
episode for at most 60,000 game ms and preserve an evicted sequence for 30,000
ms (`keep_through_avoid`, `kEvictKeepMs`). These are **owned bookkeeping**, not
permission to suppress order-12 E: `sb_gate_why`, `e_certified` and
`lane_obst_why` refuse that live A order. Capital-pair avoidance exclusions
also refuse live A order 12 and B's **last copied** order 12. The latter is
not a current native B-order check: a B transition after its last observation
can coexist with a still-fresh board row. A stronger "neither current native
order is 12" claim needs an additional current copy; the existing engineering
policy instead retains the native E backstop: shared `ctl_e_fields` reads live
B order and `e_certified` refuses either live order 12, so that controller pair
reason leaves original E running. Do not promote a board timestamp to a native
writer/lifetime lease.

**AP parity means copying this actual policy:** retain qualified own-gate
order-12 episodes as bookkeeping while preserving the live-A order-12 E and
avoidance-exclusion refusals. It does not mean inventing order-12 false returns
or rebuilding the full controller state. Reuse the existing boards, member IDs,
warn/episode fields and invocation-local intent values; no future-borrow framework
is needed. Arrival placement remains its separate geometry/adapter task.

The adapter-ready boundary is therefore synchronous and value based:

1. Copy the current complete bounded warning rows under the recognized owner;
   join pointers to current IDs/membership before recording own-gate evidence.
   Store only owned booleans, stage, IDs and timestamps for the existing episode
   policy. Never retain a row index, native output node, list pointer or distance
   address as next-iteration authority.
2. For each decision, materialize required selected controller intent as owned
   points/path values, including its current lane/funnel/handover target and any
   active detour under that reason's existing freshness rules. Rejoin current
   A/B/root/flight/order/target/clock identities under a finite call-free `Copy`.
   Finish and expire that copy before returning a verdict. Current FL
   `e_gate_request` deliberately has no controller reason branch; its ordinary
   reason set is not a ready controller adapter.
3. Consume copied partner `(pointer,ID)` exclusions only in that synchronous
   `av::resolve` call against the current owned snapshot. Rebuild the exclusion
   list on the next decision; retain existing verdict/state age, load/clock/
   sector reset, overflow and identity refusals. A cached pair-clear result,
   episode retention or removed native row alone grants no later exclusion.
4. Preserve native order-12 E behavior and bookkeeping in the first controller
   adapter. Supporting suppression there is separate policy/acceptance because
   the native flight point can persist, be regenerated or finish into a callback.
   The present trace supplies no finite physical future-writer closure, and no
   fixed 200 ms safety lease. Recheck all current identities and required intent
   at the next helper invocation after native continuation/callbacks.

No additional seam is proposed. Reuse the qualified E entry ABI/whole-instruction
claim and envelope: false is EAX=0/`ret 12`, caller tests EAX, output untouched;
preserve nonvolatiles, exact ESP, XMM0–7, MXCSR and LastError. Existing owner/
HelperBorrow rejects nested helper use; do not call native rescore/mesh/callback
services from the payload or hold a borrow across native continuation. No new
SEH/unwind capability follows for a foreign exception through a naked hook.
Performance is bounded by the existing **five** warning rows and fixed member
arrays; no new sweep, observer, cache, allocation, lock or native call is needed
for this contract. Timing, actual controller adapter fixtures and native Windows/
user-flight behavior remain unmeasured here. **FL controller remains disabled.**
