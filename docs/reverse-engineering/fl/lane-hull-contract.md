# FL lane hull: physical basis, flight nose and copied-width validity

2026-10-30. Targeted prerequisite to the existing lane controller's width
acquisition in `ship_gate_ctl_glue.cpp::width_ensure/ctl_commit/ctl_escort` and
`ship_gate_board_core.h::hull_from`. This does not admit that controller, add
an adapter or enable a feature. The [enclosure note](ship-enclosure.md)
owns the normalizer and finite body-motion bounds; [lane timing](gate-lane-timing.md)
owns deadline deferral. The stock arrival's 146-node/122-body supports use one
actual fixed root basis. They are not an established nose-relative turning
capsule.

The [producer](../../../verification/results/fl-lane-hull-contract/verify.py)
and [receipt](../../../verification/results/fl-lane-hull-contract/evidence.json)
check **43 operand anchors / 289 instructions / 5 rejected mutations**
(measured static counts). Input is original `/mnt/c/X3FL-orig/X3FL.exe`,
2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
The hash is provenance. Reproduce with:

```sh
python3 verification/results/fl-lane-hull-contract/verify.py
python3 -m py_compile verification/results/fl-lane-hull-contract/verify.py
```

The project's `compare_fl_engine` PE/Capstone reader inspected selected spans;
existing source/normalizer/tree receipts were reused. Raw listings are local
and untracked at `/tmp/fl-lane-basis.local.txt`. No PE code, native fixture,
Wine process or game was executed. No production source changed.

## 1. Exact physical/flight relation and the actual steering A entry

For class 7, physical local basis rows are root `+40/+50/+60`; flight rows
are flight `+870/+880/+890`. Thus `+890..898` is forward, `+880..888` is up;
`+870` starts the complete basis, not its up row. Steering A0 obtains flight
at `4564a7` and adds `870` at `4564aa`; it does not steer from root `+40`.
The existing [steering contract](ship-steering.md) covers A1/A2
and the native A/B CPU boundary.

Native integration publishes the following relation at `463e38..463ebe`:

| Current flight offsets | Native result |
| --- | --- |
| All of `+8a0/+8a4/+8a8` zero (`463e46..463e5e`) | `463e63..463e7d` copies all three physical root rows exactly to flight rows. |
| Any current offset nonzero | `463e98` builds Euler(offsets), `463ead` composes that matrix with root, and `463ebe` separately normalizes the resulting flight matrix. |

`50ebe0` uses ECX=left matrix, EDX=right matrix, one stack output pointer;
its fixed-point row product is `sum_k round16(left[i,k]*right[k,j])`.
Accordingly the latter relation is `flight=normalize(E(offsets)*physical)`
with native per-product rounding, rather than an exact orthogonal identity.
Current offsets are `+8a0/+8a4/+8a8`; corresponding targets are
`+8b8/+8bc/+8c0` (`4635d5..4635fd`). Equal current/target values skip their
slew. Target zero alone does not imply current zero. Root angular updates
compose at `463de3` and normalize at `463df1`; zero angular updates skip
to `463e38`, which still republishes flight. The nonzero-offset conjugation
work at `463d88..463dc7` precedes physical normalization and does not make
physical and flight bases interchangeable.

This is a **post-integration** relation, not an unconditional A-entry fact.
The scheduler runs ready tasks before the next command/integration driver,
and command A/B precede integration in that driver. SA case 48 resolves its
object at `46c20f`, then calls attitude setter `459430` at `46c227`.
On ordinary class 7 with flags2 `100000` clear, `459477` rebuilds physical
root `+40`, and `459486/491/49c` copies it into root world rows. The class-3
special branch is skipped by `4594c3/4594c8`. No class-7 flight basis,
object radius or physical-root pointer is changed on this route. Only the
Euler builder is called on that ordinary path. Thus a same-root/same-radius
class-7 ship can reach steering A with zero current offsets and **stale
flight rows**, even after an otherwise valid preceding integration. This
is an instruction-derived counterexample, not an executed setter fixture.

Minimal current axis guard: copy the actual physical and flight matrices in
the same admitted A/B/E read. If using the zero-offset relation, require the
three current offsets zero and compare actual rows; requiring targets zero
additionally closes their ordinary slew while those target inputs remain
unchanged. Otherwise reduce against the actual copied flight direction with
an independently qualified frame and residual. `make_frame` constructs an
orthonormal comparison frame, but its third row is derived from rows 0/1;
it is not exactly the native raw forward. Retain the axis residual if the
capsule consumer uses the raw flight forward. An unqualified frame guard,
Frobenius bound or zero-offset test alone is insufficient.

Requiring zero current/target offsets does not freeze ordinary ship turns:
`4635fd` reaches `463d26`, whose nonzero angular branch still updates and
normalizes the physical root before the zero-offset copy. This is a useful
axis restriction independent of a zero-angular-rate arrival restriction;
actual stock autopilot offset inputs have not been captured here.

## 2. Turning capsule mathematics: established and sufficient alternatives

The existing root theorem is a Frobenius bound and normalized row-magnitude
bound; this work does not establish a quantitative orthogonality invariant.
For example rows `(Q,0,0),(Q,0,0),(0,0,Q)` pass Frobenius `<=2Q`, yet the
first two rows coincide. Native normalization is not claimed to produce this
example; it demonstrates what the reader's current magnitude predicate alone
does not prove. Exact physical/flight equality would not remove that gap.

With a genuinely qualified unit nose frame and outward supports
`|x|<=X, |y|<=Y, |z|<=Z`, one simple capsule has radius
`w=sqrt(X*X+Y*Y)`, axial segment halfspan `Z`, and **total half-length
`L=Z+w`**. The shared consumers use segment halfspan `L-w`. With `w=3,Z=10`,
`L=max(native_r,Z)=10` leaves corner `(3,0,10)` at distance `sqrt(18)>3`
from that segment. `L=13` contains it. Derived width must never be clamped
down to native radius: if a proven `w>native_r`, existing `hull_from`'s
`w<=r` eligibility refuses this narrow-hull path until the consumer contract
is explicitly changed. Native radius is not a geometric tightening proof.

Orthogonality is avoidable in a more conservative sufficient construction.
Suppose a complete independently qualified **root-local** shape has the
above X/Y/Z bounds, all physical row norms are at most `s`, and actual
flight forward equals physical row 2. For unit axis along that row, the
point `z*row2` is inside the axial segment of halfspan `s*Z`; the remaining
vector `x*row0+y*row1` has norm at most `s*(X+Y)`, or the also sufficient
`sqrt(2)*s*sqrt(X*X+Y*Y)`. Taking either outward bound as `w` and
**`L=s*Z+w`** therefore encloses the whole point in the segment's radius-w
neighborhood, even if the lateral rows have an axial component. This uses
no orthogonality theorem. The normalizer's `s=(Q+1)/Q` row bound persists after
ordinary angular updates; initial copied rows must satisfy the chosen guard.
This is a mathematical sufficient alternative, **not** a completed numeric
reducer: all-node fixed-point publication error, rotor/pivot recurrence and
raw-axis scaling must still be included. The arrival's fixed-root supports
are not automatically those root-local X/Y/Z inputs. Native-radius admission remains unqualified. The subsequent
[stock support-scale witness](../../../verification/results/fl-lane-hull-contract/support-scale.json)
uses the actual identity-root reader with 146 nodes, 122 tight bodies and the
516-record bank. Before additional publication allowance, the sufficient capsule
has inferred width 730.882 m and total half-length 1822.401 m against a modeled
native radius 1091.502 m. The
[producer](../../../verification/results/fl-lane-hull-contract/support_scale.py)
shows useful headroom, not completed arbitrary-root or lane acceptance.

## 3. Shape validity, available stamps and the 100 ms consumer interval

The existing [source qualification](ship-source.md) already
closes the useful finite normal-update subset: complete constant source keys,
qualified identity/no-gamma rotor reconstruction, no dynamic scale/axis
writers, no source events/target callbacks, and no eligible future scene
centering/transform. The [tree reconciliation](ship-tree-reconciliation.md)
joins the complete expected graph and all real bodies, including wrappers.
Those invariants can bound ordinary shape motion; pose samples alone cannot.
Explicit source replacement/reparenting/body assignment remains outside that
finite writer scope.

There is no established scalar **all-shape mutation generation**. Body setter
`4a365d..4a36b1` writes node model `+144`, raw scale `+70`, axes and binding;
its `node+a4=-1` at `4a3695` is not `object+a4`. Source rebinding's call-free
prefix `4aa76d/77a/791` writes node scene/index/controls `+258/+25c/+260`.
The source-key writer then consumes these records, and its `4aa737` event
tail can call out unless the accepted event exclusion applies. Turret updates
change existing local attitudes without replacing the ship root. These
witnesses prevent treating root equality, object radius bits, unchanged
node count, type ID or copied name as proof of unchanged complete shape.

Current production `navigation::Copy` now implements the seam-local borrow
missing at the earlier source-lifetime checkpoint. It admits the completed
common owner, lifetime binding, engine, resource generation and accepted name
ticket, and compares lifetime epochs again at `finish()`. Resource generation
qualifies cold bodybank facts; name revision qualifies accepted name mapping;
lifetime epochs qualify the observed registry/load/factory domain. None is
documented as observing every in-place node/source/scale/pose writer above.
The reader retains exact source/key/node records in temporary owned scratch;
its public `Value` has identity/counts, not a persistent shape certificate.
Every later invocation obtains a new borrow and revalidates the relevant joins.

AP success caching (`width_ensure`) uses object radius bits and root pointer,
with type-only propagation for other instances. That contract is insufficient
for FL. A acquisition is not a lease until B: `ctl_observe` throttles live
member state to 100 ms while lane policy/B/E can consume cached hull values
on intervening updates. The clock threshold is not exclusion of the native
setters or proof that axis/shape stayed unchanged. A full source read at A
does not make a stale width safe before the next A or during a skipped B
state refresh. Invalidation/guarding must precede each dependent consumption.

**Established boundary:** a fresh complete synchronous copied hull can be
reduced against its actual copied flight axis and published only after the
existing successful borrow finish. A persistent w/L value is possible only
under an explicit finite shape-writer premise plus per-use current axis/
identity/resource/name guards, or bounded exact shape revalidation. Existing
scalar stamps alone do not establish that premise across arbitrary same-root
source changes. This study therefore does not authorize a persistent FL cache,
type-width fallback or unguarded 100 ms reuse. It also does not require a new
observer, a writer census or a general lifetime service: select the finite
supported writer boundary, then validate precisely what that boundary needs.

## 4. Hook, liveness and verification limits

No hook span changed. Reuse steering A's ECX object/stack point ABI and B's
EBX object envelope, exact instruction boundaries, GPR/ESP/XMM/MXCSR/LastError
preservation and four-byte incoming stack contract. Copied engine pointers
are invocation-local only; no retained root/node/source pointer authorizes a
later dereference. Finish the call-free bounded read before continuation,
exclude same-owner helper nesting, and introduce no native resolver, event,
allocation recovery or callback during it. The native A/B seams do not supply
an exception-unwind recovery contract. A failed copy/finish or observed loss
leaves output unchanged and must invalidate dependent publication before use.
This adds neither an SEH path nor an exception-safe engine lease.

Validation here is static operand/mutation checks and arithmetic counterexamples,
plus factual/link review. No full suite, build or native run was needed for
this documentation/producer change. Compatible documented Windows memory
transport is already used by the reader; this analysis supplies no new native
Windows execution evidence, live collision result or flight acceptance.

## 5. Pure canonical turning-capsule reducer (2026-10-30)

`arrival::capsule` now reduces complete copied values without changing arrival
policy or admitting the lane controller. Its [producer](../../../verification/results/fl-lane-hull-contract/capsule.py),
[receipt](../../../verification/results/fl-lane-hull-contract/capsule-evidence.json)
and [focused tests](../../../verification/analysis/test_fl_lane_capsule.py) consume
modeled stock joins and existing qualified ALL-LOD boxes. This is no new source
identity, live read, lifetime grant, persistent width cache or controller proof.
Every eligible A/B/E use still needs a fresh complete admitted read and successful
borrow finish. The output contains only integer game-unit bounds and copied
supports; the caller must also round subsequent consumer conversions outward.

**Canonical support theorem.** Reuse the unchanged `arrival::supports` with
identity root and identity comparison frame. Its initial `ship::enclose` checks
the complete profile, every intermediate/helper, writer flags, quaternion and
axis restrictions, matching scale/model/source records and every current native
publication; a final-node failure cannot publish a partial result. Constant
keys use the actual qualified loaded basis, with local Frobenius bound 2.
Identity keys compose exactly. The independently established no-gamma rotor
bound is 3 for every admitted angle, rather than only the current sampled angle.

For a node whose ancestors have no rotor, checked integer composition gives
its canonical centre and basis. A rotor changes its own basis, not its pivot:
that centre is still the offset transformed through its parent's basis.
`column[j]` then bounds the varying column by `L*parent.column[j]+eM`, where
`L` is 1/2/3 for exact identity/constant nonidentity/rotor. Once an ancestor
moves, each centre coordinate interval expands by
`|offset|*parent.column[j]+eP`; each column follows the same recurrence. This
covers descendants of rotors, not just bodies located at a rotor pivot.
For stationary bodies, all eight transformed ALL-LOD box corners bound the
linear image of the box. Moving bodies use its origin-centred enclosing sphere.
Absent boxes retain the larger of the supplied radius and the signed16 sphere
`sqrt(12)*scale`, so an unrelated smaller cached profile radius cannot tighten it.
The union over every body gives canonical `|x|<=X,|y|<=Y,|z|<=Z` for every
admitted simultaneous local pose.

The reused support implementation adds its existing `1/64` outward allowance
at each floating operation group, and uses `2.6>eP` for centre publication. In
this identity-frame path all Q16 basis conversions, fixed corner products and
integer centres are exact binary64 inputs. The checked domains bound scalar
coordinates/intermediates below `2^30`, squared offset norms below `2^50`,
column norms below 13, offsets by `sqrt(3)*2^24`, and body coordinates by
`2*528422` per axis. A norm/dot/product/add group has fewer than 32 rounded
operations: even the conservative absolute bound `32*2^-22 < 1/64` covers
coordinate arithmetic, while correctly rounded SSE2 square root contributes
less than `2^-24` to an offset norm. Norm error times the largest admitted
column (13) remains below `1/64`. Repeated motion propagates the already
outward interval/column values; it does not silently accumulate an unreserved
per-level error. The helper refuses other MXCSR rounding/control modes.
This supplies the numeric justification for reusing those existing allowances;
the capsule adds no guessed geometric margin.

**Arbitrary-root publication theorem.** Let `A` be the copied physical root
matrix divided by Q. Exact integer row-square guards require every row length
`<=s=(Q+1)/Q`, and require nonzero row 2 plus exact equality of that row with
the actual copied flight forward. Thus `||A||_F<=R=113514/Q`, since
`3*(Q+1)^2<=113514^2`. Coincident/nonorthogonal rows are admitted. Let `Bc,Pc`
be an arbitrary conceptual native identity-root replay, using the *same* allowed
local matrices as an arbitrary-root native replay `Ba,Pa`; positions are relative
to the root. Bound `||Ba-Bc*A||_F` by `deltaB` and
`|Pa-Pc*A|` by `deltaP`. Both start at zero. Native composition rounds nine
matrix dot products and three position dot products, independently per product;
therefore each matrix error has Frobenius norm `<=eM=9/(2Q)` and each position
error has Euclidean norm `<=eP=3*sqrt(3)/2`. Submultiplicativity gives

```
deltaB_child <= L*deltaB_parent + eM*(1+R)
deltaP_child <= deltaP_parent + ceil(|offset|)*deltaB_parent + eP*(1+R)
body_error  <= deltaP_child + ceil(rho)*deltaB_child
```

Exact identity composition copies the parent basis, so it retains `deltaB`
without another matrix error, including a constant key that is exactly identity.
`rho` is the same ALL-LOD box sphere or independent signed16 fallback used above.
Take `E` as the maximum body error. This comparison works for the *moving*
canonical support union: it never assumes that the source identity basis is the
actual current rotor basis. Bodies on the root have zero publication error.

The implementation evaluates these recurrences as checked nonnegative Q32
integers. Reusing `sqrt(3)<=113514/Q`, the exact outward Q32 constants are
`eM*(1+R)=805725/2^32` and
`eP*(1+R)<=[3*113514*179050/2]/2^32`. Lengths/radii use the existing fixed
32-step integer ceiling square root. Additions/multiplications reject overflow;
intermediate positional and body errors also reject values above `2^26` units.
The shared helper's documented squared-input domain is `<=3*2^48`.

**Capsule and no-wrap boundary.** For canonical point `(x,y,z)`, the root-mapped
point is `x*row0+y*row1+z*row2`; native publication adds a vector of norm at most
`E`. With the unit axis along row 2, the axial point `z*row2` lies in a segment
of halfspan `s*Z`. Triangle inequality gives radius `w=s*(X+Y)+E`.
The reducer separately ceilings X/Y/Z and E to integer units, evaluates each
multiply/divide with integer upward rounding, then publishes
`width=ceil(s*(ceil(X)+ceil(Y)))+ceil(E)` and
`half_length=ceil(s*ceil(Z))+width`. Both must be `<=2^26`. The radius is never
clamped to native radius. A consumer using raw row 2 and coefficient
`h=half_length-width` also contains the axial point, because `h>=Z`.
Its *physical* segment halfspan is `h*|row2|`; another consumer treating L as
physical total extent must retain that scaling. This reducer does not change
or qualify those downstream consumers.

The reused enclosure validates all current publication terms without narrowing
and proves the future local basis/relative centre domain under R, L and eM/eP.
Its all-node root-interval guards apply even to excluded/bodyless intermediates;
canonical checked composition also rejects term, sum and basis overflow.
The capsule invokes this with a stationary current root interval. It is a
relative turning-shape bound, not authorization for a translated root path:
the eventual caller must check its actual travel interval and consumer arithmetic
before use. Failed root-row/forward, complete-read, box or arithmetic checks leave
the entire capsule output unchanged; scratch/work are explicitly nontransactional.

**Measured controls and cost.** Seven focused host tests include 60 sampled
allowed rotor matrices with pivot descendants, exact-rational publication-error
and capsule containment, adversarial coincident/maximal/subunit root rows,
stale-flight refusal, 256-node identity depth, signed16 fallback/scale limits,
complete-read and late-box failures, distortion/position overflow and supported
MXCSR modes. Stock checks cover 146 nodes, 122 bodies, 122 tight boxes from the
516-record bank and 976 corner samples. Inferred from the modeled stock values,
integer width is **365473 units / 730.946 m**, total half-length **911234 units /
1822.468 m**, and publication error **30 units / 0.06 m**, against modeled native
radius **1091.502 m**. These are copied geometry results, not live measurements.

The producer records one 2000-call host timing (about 24 us per complete stock
reduction on this host; diagnostic CPU time, not game FPS). Work is bounded O(n):
one existing complete validation/support pass plus one error pass, fixed-step
integer roots, and eight corners only for stationary tight boxes (400 corners in
stock). It reuses caller-owned **40960-byte** scratch and its already-finished
validation bounds for the error pass. No new arrays, allocations, locks, native
calls, repeated name joins or retained pointers are introduced. The focused
capsule, enclosure and arrival-policy modules pass **32 tests** together; x86
MinGW compilation and instruction audit report **zero x87 instructions** in the
changed geometry object. Windows-compatible source/cross-compilation is checked;
no native Windows execution, Wine run, DLL build or install occurred.

Independent source/proof/evidence review accepts this pure helper with no
blocking findings. It independently recomputed the Q32 constants and final
stock dimensions and checked all eight recorded input hashes. The affected
command was `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_fl_lane_capsule verification.analysis.test_fl_ship_enclosure verification.analysis.test_fl_arrival_policy` (32 tests, 2.267 s, exit 0; tool-result provenance retained, no separate host log).
Actual A/B/E integration, outward consumer conversion, travel/no-wrap validation
and per-consumer performance remain open; this does not activate lane behavior.

## 6. Fresh acquisition and the inactive A width boundary (2026-10-30)

`navigation::acquire_hull` now joins the existing completed owner, `Copy` and
`ship_live::read` to the pure capsule. It owns no lifetime service, shape cache,
writer observer or hook. Callers must perform eligible-action/near-gate guards
before calling and already hold `HelperBorrow` and the saved CPU/LastError/MXCSR
envelope. The request carries object/id and optional expected root/flight tokens;
when omitted, both are acquired inside the admitted object copy. The A width
helper makes no pre-borrow root/flight reads merely to construct this request.
No clock, gate pointer, zero speed, zero physical rate or stationary arrival
premise is added to this relative shape operation. Deleted, attached, docked,
player and non-autopilot objects refuse before the full tree read.

Inside one finite region, copy current flight rows and current/target offsets;
require those six offset words zero. The capsule compares actual flight forward
to copied physical root row 2 and checks all physical row norms. Ordinary angular
turns remain allowed. The existing reader requalifies complete source keys,
body/scale/name joins, all intermediates, current native publications and the
stationary-root no-wrap domain. Resolve the existing ALL-LOD bank once per owned
model name/scale/generation; a miss retains the independent signed16 fallback.
Reduce the capsule, finish the same `Copy`, erase native address arrays/raw
object copies/value tokens, and only then publish owned numeric results. A failed
finish or any earlier read leaves the public `HullValue` unchanged. No stores to
engine state occur in this helper. Its stationary shape checks do not authorize
a future translated root path or downstream controller arithmetic.

The float consumer gets width rounded outward in metres. To cover both a
physical extent interpretation and the existing raw-axis segment consumer,
first enlarge the integer segment halfspan `h` to `ceil(s*h)`. Construct float L
from the already rounded float width plus this halfspan. Check the consumer's
actual binary32 subtraction `(L-width)` against the integer halfspan using an
exact binary64 multiply by 500; advance L by one positive float if needed and
refuse if still insufficient. The width conversion similarly compares its
exact binary64 product by 500 to the integer bound. Thus independent width/L
rounding cannot silently shorten the consumed segment. No x87 return helper or
uint64-to-floating conversion is introduced.

FL `width_ensure` always acquires anew when explicitly invoked: no root/radius
success cache, retry timer, type fallback, width clamp or AP raw-body reader.
On failure it invalidates the planning width/length, rather than retaining an
older successful shape. On success it publishes only this invocation's planning
values and does not propagate them to the AP type table. These values are not a
lease for B/E or another A call. `ctl_commit` and `ctl_escort` explicitly refuse
FL before tree work or board creation, since qualified FL gate-Q and lane-stage
production still do not exist. Production traffic/lane admission remains off;
this checkpoint neither makes an inactive feature pay tree-read cost nor feeds
AP grid/aperture constants to an FL planner. The actual width helper is exercised
directly under a modeled A envelope; this is not an installed A-to-lane proof.
B/E pair validation, fresh self/partner fit to planning hulls, translated-travel
no-wrap checks and dependent write authorization remain prerequisites to activation.

The [acquisition producer](../../../verification/results/fl-lane-hull-contract/acquisition.py)
and [receipt](../../../verification/results/fl-lane-hull-contract/acquisition-host.json)
retain the actual command/output for 17 focused tests, 94 normal and 94
ASan/UBSan host checks, and strict x86 production/combined-fixture syntax.
Leak scanning alone is disabled because LeakSanitizer refuses this ptrace
host; no new allocations were added. Same-root modeled SetBody/Pos/Rot/Size/
Scale/attachment/SA48 mutations and changed offsets, recycled IDs, resource
loss and failed finish all refuse with unchanged engine bytes/copy-out. Recovery
reacquires successfully. A coherent supported source-position change at the
same root/radius publishes a larger new width, exercising successful fresh
replacement as well as refusals. These are synthetic records, not executions
of the engine's named mutation routines. The existing x86 object receipt covers
six affected units with zero x87 instructions and an explicit portable import/
helper relocation allowlist.

The opt-in `--arrival --lane-hull` native fixture reuses the existing 146-node /
122-body stock payload and 516-record bank. It converts the intentionally split
arrival root to coherent old-world local words and matching flight rows, then
checks all 122 tight bounds/eight model lookups, failed-finish copy-out, unchanged
engine bytes and recovery. Its 1/4/8-acquisition batches log total native copies,
bytes and QPC time over 32 iterations. These repeat the same modeled stock ship;
they are not distinct-fleet or self/partner-pair qualification. Source and fixture
syntax are checked here; the root owns the PE build, serialized native execution
and measured performance receipt. No DLL/PE build, Wine execution, install,
commit, new hook, B/E lane authorization or game launch was performed by this child.

The acquisition adds fixed **643344-byte** static owned scratch (x86 compiler
`sizeof`, retained by the acquisition producer) and reuses its arrays;
there is no allocation, lock or native getter during payload copying. Complete
read/source validation and the pure reducer's independent validation both run;
no validated-input bypass weakens the pure API. Bank lookup runs once per distinct
model; the bounded per-node model-index scan matches the existing arrival join.
This is eligible navigation work, never per-draw work. Full stock acquisition
cost, rather than the three-node correctness fixture cost, is the outstanding
native performance acceptance item.
