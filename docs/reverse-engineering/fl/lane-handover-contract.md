# FL lane handover: current collision and stop obligations

2026-10-30. Bounded follow-up to the architecture note's
[handover correction](../../architecture/gate-traffic-controller.md#handover-scope-correction-2026-10-30).
This resolves the fixed stock completion/ready-task path and tests the existing
M2 capsule at finite handover states. It proposes the minimal A/B/E consumer
contract below for parent integration; no production policy, hook or activation
changes here. The [existing scope witness](../../../verification/results/fl-lane-handover/verify.py)
still owns the native contact/flag predicates and the complete pair forecast.

Run `python3 verification/results/fl-lane-handover/verify_contract.py`.
Its [receipt](../../../verification/results/fl-lane-handover/contract-evidence.json)
records **45 native operand anchors, 217 instructions in six gap-free ranges,
nine stock KC call anchors, zero KC stack-depth conflicts, five rejected
native mutations, and 28 geometry verdicts with zero failures** (measured
static/host counts). The geometry producer is
[contract_geometry.cpp](../../../verification/results/fl-lane-handover/contract_geometry.cpp).
The executable and stock story hashes are provenance. Original inputs are
`/mnt/c/X3FL-orig/X3FL.exe` and `addon2/03.cat:addon2/L/x3story.obj`, build
1636047691, 3,110,481 decoded bytes, SHA-256
`dbb732716e1acab6b8cc7f87f5d31324fb4764dfc22d1cec808749a4f45ec7b9`.
The root catalogue's `L/x3story.obj` is a different object and is not this input.
Read-only Ghidra `FLRegistry/X3FL.exe`, the project scripts
`X3DecompileFunctions`, `X3ListRange`, `X3FunctionContext` and `X3XrefsTo`,
plus the existing PE/Capstone and KC readers supplied the derived findings.
Raw outputs remain local at `/tmp/fl-handover-*.local.txt`; none is tracked.
No native Windows/Wine execution, game launch, build candidate or install ran.

## 1. The actual completion, issuer and removal sequence

FL stock class-2004 `__EnterHyperspace`, KC entry **0x147634**, decodes to
244 derived statements. Its relevant in-sector capital path is:

| Phase | KC CODE offset | Established effect |
| --- | --- | --- |
| Grid approach | `14799a` | `SA_FlyObjectBeforeContainerPos`, the explicit-point order-4 issuer qualified by the lane timing note. |
| Align | `147a4c` | `SA_RotateToAngles`, then state advances to the through-slot request. |
| Slot refusal | `1478da` | `TI_WaitRandomForInterrupt(1000,10000,this)` and retry; this is a wait in milliseconds, with no upper bound on the number of refusals. |
| Slot granted | `147ac4` | `SA_FlyObjectIntoContainer` issues order 5. |
| Order-5 success | `147ad7..147b0c` | Checks `HasDestination`; for a nonplayer, sets desired speed zero at `147aff`, then calls `SA_WarpObjectAway` at `147b0c`. |
| After warp completes | `147d17..147d49` | Releases the slot; success result 3 and nonplayer select `Remove()` at `147d49`. |

The **no-destination failure is real**: false `HasDestination` sets failure
result 8 and branches `147aeb -> 147d16`, bypassing order 8 and successful
removal. The player route also bypasses the nonplayer warp/removal path.
Interrupted/replaced tasks and native failure are not a guaranteed successful
transit. No fixed 900 ms or one-frame removal lease follows from these calls.

The callback token saved by a normal KC SA issue is not the SA group's own
callback index. VM construction publishes **0x4c1a40** at
`VM+0x34+type*24` (`4b8d44`) and saves its default requester type in
`VM+0x1454` (`4b8d83`). Interpreter `4c077a` pushes that requester type before
calling the selected SA dispatcher at `4c0783`. Order 5 saves it in
`ship+0x7c` at `46accc`; order 8 does so at `46b367`. Their corresponding
`ship+0x80` stores retain the task context. Wake **0x46a550** uses those fields
to select the requester result callback, rather than blindly calling the
callback registered beside SA's dispatcher.

`4c1a40` resolves the task by context, publishes status WORD `task+0x20=1`,
integer tag/value `+0x28/+0x29`, then calls **0x4c1880** at `4c1a9b`.
For a suspended task (`task+0x3a==2`), the latter transfers the result to its
stack and moves it to the VM's **+0x12d8 ready list**. These bodies do not
execute the interpreter. SA's separate requester callback **0x46a4c0**
(`43dced` registration) instead records a result in a simulation request;
that is not the normal stock KC task token described above.

Wake is ECX=ship, one stack reason argument, **ret 4**. Its indirect callback
at `46a584` is cdecl `(context,reason)`; it removes eight argument bytes at
`46a586`, then clears `ship+0x7c/+0x80`. It does not preserve a pending
callback installed by arbitrary reentry. The fixed normal result path adds no
such reentry. Reuse the value-release closure in
[render exclusion](ui-render-exclusion.md#activation-and-audio-completion-publish-results):
the integer result normally needs no old-value release; the typed release
branch does not dispatch KC. A foreign callback remains outside this proof.

## 2. Which gap is visible to collisions

The sector driver **0x443b10** runs collision/proximity **0x466de0** first
(`443b42`), then command processing **0x45c810** (`443b49`), and integration
**0x463000** in its later sector loop (`443b76`). The P4 and expiry order-zero
stores therefore occur **after that sector's collision enumeration**. Both
wakes return to the common B path (`45e485/45efd1 -> 462c6e`); B and speed
slew/integration can see order zero in that same simulation iteration.

The outer loop calls ready scheduler **0x4788b0** at `477ebf` before driver
`477ed2`. Its loop calls interpreter `4bed90` at `4788e8`, rereads the head,
and continues until the ready list is empty. It has no ordinary task-count or
elapsed-time budget. A zero interpreter result exits at `4788ef`; a concrete
source is failed native dispatch `4c078a -> 4c1524`. Therefore, **on the healthy
fixed-stock success path**, the woken task resumes and issues order 8, or
removes the completed departure, before the next ordinary collision pass.
This is an inferred scheduling consequence, not an observed frame trace.

That normal path is useful but does not erase current order-zero state or
cover the stock no-destination branch, task replacement/interruption, native
dispatch failure, changed callback token or a foreign script. The actual
consumer must reclassify freshly read order/flags/target at every A/B/E entry.
It cannot label a current order-zero ship collision-exempt because its board
stage or forecast says `dock`/`launch`, nor certify expiry from an imagined
`t=900` pose: the expiry branch clears `0x400` without evaluating P5, and a
large clock step can skip the last pre-expiry position sample altogether.

The live predicates remain exact: order **5 or 0x1e**, targeting this gate
with its container flag `0x100000`, exempts narrow contact in both operand
orders; proximity/E remains reachable. Live ship flag **0x400** excludes the
whole pair from both ordinary enumeration masks. A board stage, predicted
trigger, deadline alone, class-18 scene identity, or meshless scene component
is not one of those predicates. The central root remains in geometric queries.

## 3. Finite current M2 witness

The existing complete stock `SS_SH_A_M2` source graph supplies **146 nodes /
122 bodies**, capsule width **730.946 m**, half-length **1822.468 m** and
modeled aggregate object radius **1091.502 m**. This reuses the source graph,
ALL-LOD boxes, publication arithmetic and current capsule reducer; the radius
model is the existing scale/offset recurrence, not a live `ship+0xa4` capture.
Stock source gate rows 0 and 21 retain **both** root and scene envelopes.

For each gate, exact gate-F-aligned poses at lateral offsets 0, -100 and
-200 m give these measured pure-query verdicts:

| Supplied current pose | Row 0 | Row 21 |
| --- | --- | --- |
| Zero-speed native trigger radius, centre at `z=-r_ship` | 3/3 blocked | 3/3 blocked |
| Existing handover reserve, centre at `z=-(r_ship+300m)` | 3/3 blocked | 3/3 blocked |
| Native departure displacement at `t=899ms` from `z=-r_ship` | 3/3 blocked | 3/3 blocked |
| Same last sample with modeled P5 `v0=70m/s` | 3/3 blocked | 3/3 blocked |

These are **capsule refusals**, not proof that the actual mesh collides. No
full-through-plane tail was submitted. A zero-speed trigger is the native
radius case; a moving trigger adds `abs(v)/10` and time-factor inflation.
The `t=899` tests are last-legal-time controls, not a promise that native
motion reaches that sample or that current speed/pose equals the fixture.

There are positive controls: the same capsule clears in front of each gate at
the existing `end_distance` boundary plus one native unit. With the admitted
raw-nose maximum, 50 m margin and numerical reserve, these boundaries are
**2772.215809 m** (row 0) and **2596.543809 m** (row 21). An explicitly covered
straight stop also clears from farther out: illustrative 70 m/s, 5 m/s²,
1.1 s delay/step, 100 m tracking, 567 m run, 15.1 s horizon with its 10 m/s
drift margin charged. Those dynamics and the exact zero-turn cover are modeled
inputs, not qualified stock getters or native steering. This establishes a
useful finite stop query without tightening shapes or dropping the root.

## 4. Minimal producer/consumer contract for integration

**Supersession notice, parent-selected 2026-10-31:** the
[bounded-forecast decision](lane-command-transaction.md#bounded-forecast-adapter--parent-ratified-source-brief-2026-10-31)
replaces item 1's requirement that every physical request continue until the
actual command/completion ends, and corresponding later complete-command
certificate prerequisites. A checked AP-length forecast may end with positive
speed; its run-out is a geometric reserve, not an asserted future stop. Items
2–4 retain current-phase front/run-out protection, both gate components,
present hull, fresh actual eligibility and callback/ownership requirements.
The exact finite point and attainable progress interval in section 5 remain.
The list below is retained as history; predicted order 5 or launch still cannot
remove geometry or grant a live contact exemption. Source details await parent
ratification; no implementation or activation is implied.

1. Keep the entire pair-spacing forecast unchanged. Produce a separate owned
   geometry request for the **current admitted lane command and its refusal
   stop**, with live pose/nose/velocity, the copied gate's two components,
   each vertex's time, per-leg chord/raw-nose covers, and independent
   step/tracking/drift allowances. A future predicted order change authorizes
   no omitted leg. The request ends because its commanded approach/stop ends;
   it is not a forecast clipped at an order-5 prediction.
2. Before A point/stamp publication or B speed holding, check that command and
   its stop envelope against root **and** scene. For current ordinary order
   4/0/0x1f, hold outside the physical component-front stop boundary until
   freshly observed native order 5 changes eligibility. Apply this geometric
   bound alongside the existing trigger reserve; `lane_hand_bound`'s
   `r_ship+300m` reserve alone does not prove it. A failed/missing query makes
   no enhancement write. Full through-plane `lower_final` is not an admission
   prerequisite for this finite approach.
3. At E, distinguish current contact eligibility from warning suppression.
   Live order 5 may explain why native contact is skipped; it does not itself
   suppress E. A warning override needs the admitted finite command/stop
   geometry for its actual current phase. Current ordinary handover/zero/
   rotation, including an interrupted P4 or launch completion, uses the
   live-nose stopping sweep and present hull. If that fails or is unknown,
   leave native E unchanged. Do not reuse a pre-wake/board-stage exemption.
   Whole-pair `0x400` exclusion is recognized only from current native flags.
4. Keep P4/P5 and callback boundaries unchanged. A helper finishes its borrow
   before native wake, and rereads identity/order/target on a later entry;
   no pointer, ready-task ownership or result token becomes a cross-frame
   clearance lease. Current stop/turn covers remain explicit producer work:
   two endpoint noses and an 8-degree velocity window do not prove them.

The immediate implementation step is this finite approach/stop producer and
its A/B/E transaction, reusing `acquire_lane_gate`, `lane::query` and
`end_distance`, **with existing capsules**. Exercise the M2 finite refusal and
farther-out positive stop above in that actual transaction. Keep the M2's
unsafe near-state overrides refused; this evidence supplies no positive near-
gap M2 certificate. If useful operation subsequently requires accepting those
specific near states, tighten supports for those finite requests. The reason
would be their actual current stop/gap obligation, not the pair forecast tail.

Preserve the qualified [A/B envelope](ship-steering.md#abi-cpu-state-lifetime-and-reentrancy)
and [P2/P4/P5 envelope](gate-transit.md#cpu-stack-lifetime-and-callback-boundary):
exact ESP and nonvolatile/volatile GPR replay, XMM0–7, MXCSR and LastError,
SSE2 with four-byte incoming-stack support; no new native call, lock, wait or
callback while values are borrowed. The native helpers' normal paths are not
a foreign-unwind or fault recovery contract. Reuse current owner/identity and
finish checks rather than adding a global epoch or persistent shape cache.

Cost remains an integration acceptance item: keep cheap no-action/far guards,
acquire fresh hulls only after admission and deduplicate within the current
B/E invocation. Existing measured acquisition is about 313 us/ship; the pure
126-vertex query's 4.35 us ordinary / 64 us bounded fallback excludes that
cost. Native B/E repetition, physical steering covers, native Windows parity
and gameplay are unmeasured here; no new broad fixture or framework follows.

## 5. Progress and the finite point/Q seam

A farther hold must also move the **order-4 completion sphere**; otherwise
a safe speed cap could stop the ship before its current order can finish.
On the explicit-point path (`f+0x250=-1`), native **45e7bf** reads world point
`f+0x240`. For a class-18 target, **45e863..45e877** forms positive integer
`rho4 = floor(3*[gate+0xa4]/4)`. The comparison **45e8c6/45e8c8** completes
when point distance is at most rho4; **45e8de** clears order and **45e8ed**
wakes reason 1. The [timing note](gate-lane-timing.md) owns
ensuring that the rewritten field is installed before the first native test.

For an F-aligned final through lateral Q, publish the point
`L = G + Q_R*R + Q_U*U - (d_completion-rho4)*F`, in consistent native units.
The near edge of its arrival sphere is at axial `-d_completion`.
The finite physical request covers the approach to that edge and bounded
overshoot/stop there; the artificial point L is not an obligation for the
ship's hull to reach L itself. Charge native arrival-test cadence as well as
the current simulation step and tracking error. Do not cap speed to zero
strictly outside the completion sphere without moving that sphere.

The receipt includes **two inferred integer arithmetic progress witnesses**:
publish L from each measured front boundary plus 2 m; a centre one metre
past that nominal near edge is still one metre outside the front boundary
and is 500 native units inside the order-4 completion threshold. This is a
nonempty completion interval, not observed steering or a native execution.
For the stock success result, KC `1479c7` sets approach-complete state 1;
rotation success sets slot-request state at `147a5e`, grant sets state 2 at
`1478c9`, and `147ac4` issues order 5. There is no second KC distance-to-gate
wait between that grant and order-5 issue. The native capital/no-port issuer
already qualified in [gate transit](gate-transit.md) supplies
the gate position and order 5, rather than requiring arrival at a nearer
grid depth. Thus finite point publication permits the existing stock phase
sequence to proceed from a farther, safe P2 boundary.

This is a required source change at integration: existing
`lane_order_point` calls the **gate-wide** `lane_point_depth/lane_end`, while
other lane laws read `Member::d_end`. Merely changing that member value does
not move `f+0x240`. The chosen finite completion distance must drive the
published three DWORDs and their existing saved-point recognition/adoption,
as well as the approach law and stopping request. No new native completion
hook is needed; point/stamp publication retains the existing timing contract.

Existing `lane::lower_final` solves a **full axial final** around both root
and ring, and `adopt_final` calls `final_clear` over the full axial domain.
They remain useful full-final controls. Neither can be used as the mandatory
finite approach/Q gate: that would silently reinstate the M2 through-plane
prerequisite. The integration seam instead takes a policy candidate in the
existing lower-half Q window, joins the copied point to the exact chosen
finite order-4 field, and submits its actual finite approach/stop request
to `query`, retaining both components. Candidates with a clear finite
request can progress even when no full-final root/ring interval exists.
This note adds no new Q-selection algorithm or permission to ignore a body.

Leaving E unchanged at a refused near state preserves the native warning,
avoidance and issue paths. It **does not prove successful stock-M2 transit**:
native warning may still choose avoidance or evict the member before P4.
The finite contract proves a viable far order-4 completion and slot/issue
sequence; it gives no positive M2 near-state warning certificate. Activation
must not be described as useful M2 passage until the actual transaction and
near-E behavior are qualified. If the native fallback prevents progress in
that bounded case, the material unresolved question is those finite near
states' supports/clearance or their observed native eligibility, not an
invented guarantee that refusing enhancement ensures transit.

## 6. First finite aligned consumer checkpoint (2026-10-30)

The bounded production seam now supplies the finite field and stop request via
`navigation::acquire_aligned`; general approach, tracking, lateral rejoin,
escort and arbitrary-turn support remain required port work. Admission requires
one exact current Q, equal proper cardinal level ship/gate bases, zero angular
rates/remainders/slip, same-directed collinear physical velocity, no current
module detour and raw simulation step **1..2000 ms**. The raw step is retained
separately from the existing controller clamp. A same-Q convoy may share the
board; a different lateral refuses before A's table/board publication. There
is no stored geometry authority, lease or cross-invocation shape cache.

The [motion producer](../../../verification/results/fl-lane-finite/native_motion.py)
now checks the native acceleration getter's class-7 record/flight multiplier
against the slew operands: **30 anchors / 150 instructions in four gap-free
ranges**, six rejected mutations, and **10,000 integer deceleration-bound
cases** (measured static/host counts). For every admitted integer step,
`max(1,floor(a*dt/1000)) >= a*dt/2000`; the producer bounds `a*2000` against
signed overflow and uses effective `a/2`. Its finite stop horizon is
`raw_dt/1000 + 0.05 + 2*v_max/a`; run-out is
`v_max*(raw_dt/1000+0.05) + v_max*v_max/a`. It independently charges 100 m
tracking plus one native displacement unit per possible 1 ms update, the
existing 10 m/s drift, both component fronts and the query's margin/reserve.
The zero-turn invariant and future step restriction are conditional admission
domain obligations, not a new general native steering theorem.

A finishes fresh observation, gate and complete hull acquisition before table,
redirect or point publication; its commit consumes the same prepared result.
The native order-4 field uses exactly `d_completion-rho4`, and saved field
recognition uses that same depth. A also checks the actual order-4 leaf point
against the freshly copied field. B calls both getters before one fresh
admission, before either `clamp_apply` or `lane_clamp`; it reuses that owned
observation for its same-invocation controller update. Its ordinary handover
brakes at the finite completion edge as well as the existing native trigger
reserve. E calls its required getters before its final fresh admission. A
current native order 5/0x1e plus gate flag can report contact eligibility; it
never replaces either geometric query or suppresses warning by itself. The
entire existing pair forecast remains in place, including its stock tail.

The [joined host producer](../../../verification/results/fl-lane-finite/join.py)
executes actual acquisition/readers/capsule/query, verbatim A admission/commit/
publication, full B entry and both writers, and the full E policy/request/final
entry. It uses independently derived stock gate rows 0/21 and the complete
146-node/122-body M2 source graph, including a distinct second tree for the
same-Q convoy control. Native owner/getters, loaded records, bank/resource
publication and outer CPU envelopes are modeled. **The general A steering and
leaf-replay tail is outside this host transaction**. Existing reviewed hook
instruction/ABI/LastError/rollback contracts are unchanged; no new hook or
callback boundary is added. Native engine execution, joined native cost,
native Windows and useful near-gate M2 passage remain unqualified. Refusing
near E preserves native behavior and does not establish successful transit.

Review correction: the capital order-5 refresh originally omitted the optional
finite argument, so FL's admission guard refused that refresh despite the
prepared result. It now passes the same invocation's result, as order 4 does.
The joined producer extracts both actual production commit sites and executes
the guarded order-5 branch; it requires both explicit finite arguments and
rejects independent omissions at either site. Both source gate rows now check
order-5 commit/order advancement with no point write and near-state refusal
with unchanged payload, table and board. This closes the call-site/test gap;
the general A steering/leaf tail and native qualification limits above remain.


## Nonaligned order-five command extension (2026-10-31)

The [derived static receipt](../../../verification/results/fl-lane-handover/order5-command.json)
is reproduced by `python3 verification/results/fl-lane-handover/order5_command.py`.
It pins the original FL image and checks 32 instruction operands, 595 gap-free
instructions in the order-five handler and 21 in its issuer. Counts include
nongate/port branches; they are not executed instruction counts. No native run
or new hook qualification is implied.

The dispatch tables select `45dd7f` for order five. Each pass rereads target
`flight+224` and point `+240`; class-18 gates bypass the nongate `+230`
substitution. Before steering, `45e44a` compares the truncated floating-point
Euclidean distance with a speed/radius/SETA threshold. Within its nonoverflowing
integer domain, let `B=trunc(abs(current_speed)/10)+ship_radius` and
`T=trunc((requested_SETA_Q16-65536)/4)`. The threshold is
`B+((signed64(B*T)+32768)>>16)`, reducing to B at 1x. This is not the order-four
completion sphere. Native overflow behavior is not an admissible unbounded
adapter contract.

On completion, the existing P4 stop/order-zero/wake sequence runs before A.
Otherwise `45e4aa` calls A0 (`456450`) with the actual point; native alignment
then selects 500 native speed units (1 m/s) at `45e5ef` or the gate ramp ending
at `45e663`, before B. B must use the post-native values. The issuer
`46ac78..46acd8` writes destination, target, order, time and callback; a gate
update continues to use that destination. Keep the native gate-origin field
separate from any redirected controller carrot.

Current finite acquisition treats saved order five as an aligned phase, with
cardinal/equal bases, zero angular motion/slip, exact lateral Q and front-side
position. The command authorizer and retained-command binding also require
order four for ordinary moving commands. Removing those filters alone is
incorrect: the shared `Plan::line5` rollout does not export a current-phase
`LanePolicyStep` (speed remains -1), and `command_translation` currently adds
new lateral carry for every nonzero speed. Native order five skips that carry
at `463f4b..463f53`, while still translating and decaying existing slip.

The bounded extension should reuse current 3-D acquisition, shared order-five
`lane_steer`/`core::steer` and current/first/terminal geometry against both gate
components. Export the actual line-five policy, use order-five translation and
phase termination, and bind B/E to the fresh live command with existing identity,
age and atomic publication checks. Saved Q remains controller provenance;
reissued native points require a fresh join. Do not borrow a forecasted future
warp/removal as collision authority, or add a cache/token/hook for this change.

Acceptance needs both gate rows with off-Q/noncardinal approaches, nonzero
rates/remainders/slip, successive actual A/native-tail/B/motion/E visits, unchanged
native `+240`, useful steering and completion progress. Include SETA-sensitive
trigger boundaries, reissue/identity/point changes, stale commands, blocked
geometry and P4-to-order-zero transition. The current fixture's order-five
positive calls the generic dispatcher at `456997`; it does not execute this
full order-five handler. Existing aligned and AP behavior must remain covered.
Native behavior, useful near-capital geometry and live passage remain open.

### Order-five native speed and completion integer domain (2026-10-31)

The scoped [producer](../../../verification/results/fl-lane-handover/order5_speed_domain.py)
and [receipt](../../../verification/results/fl-lane-handover/order5-speed-domain.json)
pin the original image above, 70 instruction contracts and six gap-free regions.
They check 144250 ramp samples across all four helper branches and 22 explicit
range/rounding boundaries. These are measured static/arithmetic checks; the
all-value bounds below are inferred from the checked instruction equations.
No native fixture or game was executed for this receipt.

**Getter stability and chronology.** Reuse the existing
[complete getter graph](ship-steering.md#abi-cpu-state-lifetime-and-reentrancy),
which has no indirect calls or recursive edge, and its short simulation-thread
borrow. The stock class-18 gate route between A and the speed stores changes
steering remainders, computes alignment from the actual `+240` point, and sets
gate flags; it does not change the speed getter's inputs. The first call may
clear expired flight `+914/+918` at `45a543/45a550`, but the expired factor is
already ignored on that path, so its clear is idempotent for the returned
maximum. With the existing same-call raw getter capture and admitted native
borrow, both `45e61b` and `45e638` therefore return the same nonnegative M.
No extra callback snapshot or second speed-reader protocol is needed.
This remains a normal-path contract, with the existing owner/thread/lifetime
conditions, not a guarantee against foreign callbacks, faults or unwinds.

**Exact aligned gate ramp.** `45e620` loads gate radius R; `45e629` retrieves
the original native distance D from command-stack `+ec` (saved at `45df63`).
The signed constant multiply/shift computes `L=trunc(R/5)`; the other index is
`H=R`. Endpoint speeds are `a=trunc(M/2)` and `b=M`. The call to `419fc0` uses
ECX=L, EDX=H and three caller-cleaned stack arguments a,b,D; EAX returns the
desired speed stored at `45e663`. It is an integer interpolation:

```
D <= L: a
D >= H: b
L < D < H, D < 65536 and H-L < 65536:
    trunc((a+b)/2) + trunc((b-a)*(2*D-L-H)/(2*(H-L)))
otherwise, L < D < H:
    trunc((a+b)/2) + trunc((b-a)*(D-trunc((L+H)/2))/(H-L))
```

Here `trunc` is signed division toward zero, including midpoint division by
two. The small path calls `412b50` (`imul/idiv` at `412b72/74`); the large
path uses `41a08e/90`. Both products are signed 64-bit, but sums, differences,
division results and final stores have signed-32 obligations. The two forms
can round differently; replacing this with one floating lerp is not an exact
native speed law. Equal indices have a separate average branch; ordinary
geometry admission already requires R>0, hence H>L.

A sufficient domain for **every nonnegative distance** is `0<=M<=INTMAX`,
`R>0`, `M+floor(M/2)<=INTMAX`, `R+floor(R/5)<=INTMAX`, and `0<=D<=INTMAX`.
The exact all-distance scalar limits are M<=1431655765 and R<=1789569706.
The existing M<=10000000 bound implies the speed sum condition; use the
explicit radius-sum condition rather than unrelated escort/braking guards.
The endpoint clamps execute before interior arithmetic; some individually
clamped inputs outside this sufficient all-distance domain are safe, but are
not needed by this admission. Interior index widths are positive. The small
branch's doubled intermediates are bounded by its 65536 tests. The large
branch's midpoint and difference fit signed32 under the radius-sum check;
its product fits signed64 and quotient fits signed32. Both branches return
`a<=desired<=b` (centered integer truncation cannot cross an endpoint).

`45e5e7` uses signed alignment `>=0xf333`; its other branch writes **500**
at `45e5ef`, even when M<500. Thus the pre-B native tail desired is in
`[0,max(M,500)]`. B at `462c72` precedes the second bucket walk. That walk
upper-clamps desired to M at `462d65..76`, then to flight `+278` only when
that value is nonnegative (`462d83..92`), and may set it to zero at `462dc4`.
These preserve `[0,M]`. Given current speed in `[0,M]` and the existing
checked positive-acceleration/frame-product domain, `462e82..462ec7` slews
between current and clamped desired, preserving `[0,M]`. The physical speed
bound may therefore reuse M when native pre-B desired is 500>M. No claim is
made that desired zero implies current zero: P4's existing `cap_keep` helper
can preserve current speed, and that observed scalar remains authoritative.

**Completion arithmetic.** `45e053..7d` takes signed current speed s, forms
its dword absolute value, divides by ten toward zero and adds ship radius r.
Exclude s=INT_MIN; its absolute value cannot be represented by that sequence.
For the finite moving adapter s>=0 is already required. Compute all checks
in widened integers, with S the **requested** signed Q16 SETA at clock `+bc`:

```
B = trunc(abs(s)/10) + r                 # require 0 <= B <= INTMAX, r >= 0
delta = S - 65536                       # require INT_MIN <= delta <= INTMAX
T = trunc(delta/4)                      # cdq/and 3/add/sar at 45e088..8e
P = signed64(B)*T + 32768                # imul/add/adc at 45e091..98
Q = floor(P/65536)                      # shrd/sar, NOT truncation toward zero
threshold = B + Q                       # require Q signed32, threshold in [0,INTMAX]
```

The declared signed32 operands imply P fits signed64. The range check on Q
is necessary because native `shrd` retains only the low dword of the shifted
product; there is no saturation. The final `add` likewise wraps outside the
admitted domain. For physically nonnegative S in `[0,INTMAX]`, delta already
fits signed32, T>=-16384 and threshold is automatically nonnegative. In that
subdomain it suffices to check `B<=INTMAX` and widened `threshold<=INTMAX`;
no 1x minimum or arbitrary SETA multiplier cap is required. Keeping the
general explicit checks also describes any broader signed-S domain exactly.
Examples exercising the distinction: B=3,S=0 gives threshold=2; B=2,S=0
gives threshold=2; S=65535 gives T=0. At B=INTMAX, S=65536 fits whereas
S=65540 overflows the final threshold and must refuse.

**Distance conversion and predicate.** `45defc..df47` forms each point-minus-
current-physical-position component in signed dwords, converts each
component to float32, squares, then adds in x/y/z order. `412a86` is `sqrtss`;
`45df5c` converts its result with `cvttss2si`, ignoring MXCSR rounding for
that final truncation. Require each widened coordinate difference to fit
signed32 and the native float32 square/add/sqrt result f to satisfy
`0<=f<2147483648`. Otherwise `cvttss2si` can produce INT_MIN, which would
spuriously pass the later signed comparison. INTMAX itself rounds to 2^31
when converted to float under round-to-nearest, so checking only integer
coordinates/norm<=INTMAX is insufficient. Existing position reach guards
bound component differences by 2^27 and the norm below 2^28, safely inside
this conversion domain. Preserve the established MXCSR/float32 operation
contract; the receipt's distance examples model round-to-nearest host math.
The actual completion test is signed **D<=threshold**, inclusive, at
`45e44c/45e44e`, before A. An exact-double Euclidean norm or squared-distance
comparison can differ at a boundary and is not a byte-exact replacement.
