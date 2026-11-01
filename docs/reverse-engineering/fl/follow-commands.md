# FL native follow and escort commands

Static study, 2026-10-31, original `X3FL.exe` SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
No native execution or escort qualification is claimed. The source entry map
starts at `ship_steering_decide.cpp::decide` and `ship_gate_ctl_glue.cpp::ctl_escort`;
the latter still returns immediately for FL.

## Native point and leader contracts

The command switch at `45db2f..45db4d`, using tables `462fb4/462f5c`, selects
`461c9c` for order `14` hex and `4624e9` for order `15` hex. Both require a
nonzero `flight+224` target different from self, nonzero target `+54`, and
`WORD[owner+48]==1`, where `owner = DWORD[target+54]`. These branches do not establish leader identity,
deleted status, class 7, valid flight/root, or matching sector. The mod's
stronger live-leader contract cannot be inferred from native admission alone.

| Order | Point supplied to steering |
| --- | --- |
| `14` | `flight+240/+244/+248` is a **local formation offset**. At `461d14` the engine constructs the leader's yaw/pitch basis with zero roll; `461d31` transforms the offset and adds the leader's physical position. The temporary world point is at command stack `S+110`, sometimes advanced by `20 × follower radius × follower forward`. The stored local offset remains unchanged. |
| `15` | The leader's physical position is copied to `S+60`. `462691..462774` may shift this temporary point along the leader's yaw/pitch heading using native interpolation/scaling. It is distinct from stored `flight+240`. |

Order `14` calls A0 directly at `461fc4`, `462098`, `46235f`; dispatcher calls
are at `46213a`, `462230`, `462419`, `46246f`. Order `15` calls the dispatcher
at `46277b`. Dispatcher `456810..4569f8` selects existing A0/A1/A2 from target
alignment and type angular caps. The leaf ABI remains ECX=object, one stack
point pointer, `ret 4`, with nonvolatile GPRs preserved. No new hook boundary
is required. Temporary caller points are synchronous-call borrows, not fields
to retain in the controller or interpret as a gate completion point.

## Commands and callbacks before B

Due paths write `flight+210 = now + interval(flight+20c)`, invoke steering, then
calculate and write native desired speed. Order `14` also caps desired speed
with its maximum-speed getter. Consequently B sees native speed writes **after
A**, which must be included in the actual selected-speed test.

Not-due paths skip steering/speed updates but still inspect `flight+28c/+290`.
Expiry writes order zero and wakes reason `d`; invalid leader writes order zero
and wakes reason `3`. Neither path clears target or angular remainders. Order
`15` completes when native Chebyshev distance is below `flight+260`:
`4627e7 -> 4619c1` writes order zero and wakes reason `1`. Wake returns without
clearing `+80` when object `+7c` is zero. For a nonzero index, a present/enabled
callback table, signed index below `20` hex and nonzero function slot permit a
cdecl callback that may replace state before B. The nonzero-index path then
clears object `+7c/+80`, even if table/enable/slot checks prevented a callback.
There is no explicit native lower-bound check: fixture inputs must use valid
indices `1..31` for callback cases, with zero as the separate no-callback case.

Original basis selector `459fc0` must remain: order `14` relies on EDX surviving
it; order `15` retains its point in XMM1 across it. Both use the existing reload
at `462c6e`, B span `462c72..462c7b`, next-object store and arithmetic-flags
contract described in [angular motion](angular-motion.md).
Keep both objects, flights, physical roots and table storage live through the
invocation. No callback-crossing borrow, destruction, exception or nested-command
guarantee is established by this analysis.

## Candidate fixture closure

The [static inventory](../../../verification/results/fl-follow-command/static.json)
and [producer](../../../verification/results/fl-follow-command/closure.py) measure
13 spans, **4,093 bytes / 1,197 instructions**, and 27 classified external direct
edges. Relative to the existing `RANGES + DUE_RANGES + HELD_RANGES` union, the
additional bytes total **3,854 / 1,118 instructions**. New coverage consists of
`461c9c..4627ec`, missing dispatcher bytes in `456810..4569f8`, interpolation
`419fc0..41a0a5`, multiply/divide `412b50..412b7a`, Chebyshev distance
`419f40..419f7e`, and direction scaling `432bc0..432c59` (exclusive ends).

Reuse existing basis, square root, interval/RNG, wake/completion, Euler/vector
math, A leaves, slew/integration/publication and reload routines. External
edges resolve to existing A/math/getters or the controlled RNG-record bridge
at `537e72`. Direct-edge classification alone does not validate indirect calls
or runtime inputs. Excluding dispatcher routes permits a smaller direct-A
order-`14` witness but excludes every order-`15` steering path.

Native scratch reaches `S+7db`: use aligned **0x800 bytes plus guards**, EBX=object,
ESI=flight and `[S+24]`=owner. The held fixture's 0x620-byte scratch is too small.
The held B-return bridge can be reused without the incompatible due-exit detour.

Required next evidence includes actual escort entry with unequal hulls,
leader loss/replacement/identity reuse, completion and expiry callbacks,
post-A speed writes, actual B/native motion/E and recovery. A fabricated escort
member or an ordinary gate-point fixture does not establish these contracts.


## Stock script handover to the follower's own gate approach (2026-10-31)

The selected FL follow template does have a route from following a leader to
issuing the follower's own gate approach. This is a script/task transition,
not a native order-14/15 conversion. Reproduce the derived evidence with
`python3 verification/results/fl-follow-command/verify_script_flow.py`;
[receipt](../../../verification/results/fl-follow-command/script-flow.json).
The producer checks eight selected script hashes/provenance records, five
command bindings, five KC methods and ten call anchors. The two central script
hashes and selected story hash are pinned. No game or native fixture executes.

In stock `!move.follow.template` version 15, the template reads the leader's
sector and uses timed escort commands on the same-sector path. On the selected
undocked different-sector path it calls `!move.movetonextsector`, passing that
sector and the leader as destination. These are source lines 49, 148/191 and
225 respectively (one-based, including blanks/comments); executable command
indices 108/141 bind command 562, while index 163 binds the next-sector script
call. This does not use the Mayhem 10 km follow-to-gate threshold.

The next-sector script permits player delegation and an autojump shortcut.
Its gate-route fallback selects the next sector and issues command 502 at
indices 44/48. In the selected story, this binds `__MoveToSector`, then
`__FlyToNextSector`, `FlyThroughGate` and `__EnterHyperspace`. The capital path
requests its own slot (`1477d1`), gets its own point (`14787f`), issues the
order-4 approach (`14799a`), aligns (`147a4c`) and issues order 5 (`147ac4`).
A usable route, gate/destination and permission remain prerequisites; collision
handling or interruption can defer progress.

`__EscortObject` detects a changed leader environment at `142e21` and returns
8 at `142e28`. Its native reason-13 path maps to result 12, and the follow
reason-1 path maps to result 10. None directly issues a gate flight: the task
must resume and the script must select the sector route. The KC tracker has
two later branch-depth conflicts in `FlyThroughGate`; the entry call is
independently operand-anchored, and the other four methods have none.

This establishes the stock handover route, not a guarantee that a follower
cannot cross the gate plane before script resumption. Native steering can
continue until the relevant wake/task transition. Escort hold/merge safety,
near-plane handover, timing and live progress still require qualification.
A fixture that merely replaces order 14/15 with order 4 would not prove this
script chronology.

## Native desired-speed domain (2026-10-31)

The [static producer](../../../verification/results/fl-follow-command/speed_domain.py)
and [derived receipt](../../../verification/results/fl-follow-command/speed-domain.json)
check 99 operands, seven desired-speed store sites, ten branch contracts and
nine arithmetic guard cases. They establish a sufficient raw-input domain for
native order-14/15 desired speed in `[0,M]`; they do not execute native code or
prove motion containment. The receipt also binds the in-progress same-call raw
getter capture, which requires acceptance with the escort implementation.

Use the actual maximum/acceleration getters `45a450` and `45a610` (existing
getter indices 3/4) before conversion to model floats. With nonnegative current
own speed v0, leader speed L, own/leader radii R/RL and distance D, sufficient
checked integer conditions are: `0<M<=10000000`, `A>0`, `v0<=M`,
`101*L<=INTMAX`, `20*R<=INTMAX`, `7*D<=INTMAX`, and
`q=(order14?v0*v0:M*M)/A<=INTMAX`. Let `b=q/2`; also require
`3*b+2*R<=INTMAX`, `2*D+3*b<=INTMAX`, and `R+RL+D<=INTMAX`.
Compute checks in widened arithmetic. Existing geometry/plant/frame-step and
acceleration-times-step requirements still apply.

The helper `419fc0` saturates outside its index interval; within it, checked
endpoint sums/differences and signed product bounds keep interpolation within
its endpoints. Order 14 uses nonnegative endpoint blends then caps at current
own maximum. Order 15 caps the leader-speed magnitude first and blends only
zero, that value and own maximum; it has no final maximum store clamp. Relevant
exact arithmetic includes a 1.3-times leader branch, an alternate 0.8-times
branch, and the rounded Q16 `0xe666*D` distance threshold. Do not approximate
these as 2.6, 1.2 or floating-point 0.9 when deriving guards.

Both order issuers reuse `46b526..46b52f` for the requester type and context,
joining the existing stock KC callback contract: the wake callback records
results and readies the task without executing the interpreter. Native
expiry/completion zero the order but preserve desired speed and remaining
angles. Thus the proved speed interval survives the supported stock tail;
B must still reclassify actual order/target/desired. Foreign callback mutation
is outside this contract. Native low/interior/high/zero speed samples, integer
rounding containment and integrated escort authorization remain required.
