# FL traffic arrival boundary and resource-dependent clearance

2026-10-29, source `95e9e574`. The arrival hook and native layout below are
established static contracts. **The traffic group is not admitted:** its current
clearance policy assumes installed AP Mayhem gate geometry, which differs from
the clean FL resources. Mapping the hook and hull reader alone would activate
incorrect geometry assumptions. Lane and docking remain separate, unresolved
groups. No production code, executable, installed file or bottle was changed;
no PE, Wine command or game was executed.

The [base steering](ship-steering.md),
[avoidance](ship-avoidance.md) and
[gate stop/launch](gate-transit.md) notes own the already reviewed
hooks, dynamics getters, clock, simulation ordering and approach producers.
This note owns traffic's additional arrival seam, controller input audit and
hull-width layout. Existing policy is in
[gate traffic controller](../../architecture/gate-traffic-controller.md),
`ship_gate_ctl_glue.cpp/.h`, `ship_gate_arrival_core.h` and their shared cores.

## Evidence and receipt

```sh
python3 verification/results/fl-gate-traffic/verify.py
python3 -m py_compile verification/results/fl-gate-traffic/verify.py verification/results/fl-gate-traffic/facts.py
```

The [producer](../../../verification/results/fl-gate-traffic/verify.py),
[frozen operand anchors](../../../verification/results/fl-gate-traffic/facts.py)
and [compact result](../../../verification/results/fl-gate-traffic/verification.json)
record **2 spans, 198 FL operand anchors, 13 gap-free decoded regions / 10,204
instructions, 22 rejected structural/semantic/lookup witnesses**, and two synthetic
base maps with five checks each. Counts are measured static results. PASS
means that this RE contract reproduces; the result explicitly records
`traffic_admissible=false`. Image and resource hashes identify inputs, not a
production admission policy.

Inputs are clean AP `/mnt/c/X3AP-orig/X3AP.exe`, 2,153,984 bytes, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
and FL `/mnt/c/X3FL-orig/X3FL.exe`, 2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Ghidra 12.1.4, existing `FLRegistry/X3FL.exe`, read-only/no-analysis, independently
places the arrival seam in `FUN_0044d3b0`, 2,020-address body, sole direct
caller `0x4650af`; the postpass is `FUN_00465030`, 4,854-address body. It
corroborates the resource lookup/setter/child-transform/frustum extents too.
Raw disassembly/decompiler output and Ghidra logs stay local, outside the
worktree, at `~/.cache/x3m-port/fl-traffic-local/`. Reproduce the metadata query
with `tools/analysis/X3FunctionContext.java` for `44d729 465030 4a1a80
4a365d 4943a8 4e9287` against the existing main-checkout project.

All VAs below use preferred base `0x400000`. An eventual FL adapter binds loaded
base plus RVA, including the resource roots; it must not fall back to AP VAs.

| Receipt item | AP | FL |
| --- | --- | --- |
| Arrival launch body, exclusive end | `0x444170..0x444a52` | `0x44d3b0..0x44db94` |
| Native entry arguments | two stack DWORDs, `ret 8` | **ECX ship, one stack gate DWORD, `ret 4`** at `0x44db91` |
| Claim | `0x444606`, 6 B | **`0x44d729`**, RVA **`0x4d729`**, 6 B |
| Short span | `8b43708b4b50` | **`8b4f708b4750`** |
| Native overwritten work | EAX=ship phys; ECX=ship flight | **ECX=ship phys from EDI+0x70; EAX=ship flight from EDI+0x50** |
| Tail and active continuation | `0x44460c` | **`0x44d72f`**, RVA **`0x4d72f`**, identical on both paths |
| Patch metadata | plain-copy | length 6; instruction lengths 3+3; **ret_pop=0, rel32_offset=0** |

Both spans have zero direct controls or unaligned `.text/.rdata/.data` DWORD
pointers into their interiors, and zero direct controls/pointers naming the
entry. Decoding from each complete launch entry independently establishes both
instruction boundaries. The global linear `.text` census does not establish
computed-target or foreign-patch safety. FL's first five bytes begin at qword
offset 1 and fit within that qword; the sixth overwritten byte remains part of
the six-byte receipt and restore. Keep the existing closed startup claim
transaction, reverse rollback, inert pass-through after failed restore, and DLL
pinning; this observation does not authorize live installation/removal.

## Inputs, liveness and native state publication

FL entry aligns ESP to 16 and reserves `0x78`, then saves ESI/EDI. At the seam,
**EDI=ship, `[EBP+8]=gate`, `[ESP+8]=ship**, EBP is the original frame and
`ESP=align_down(EBP,16)-0x80`. The matrix/Euler routine's two caller-cleaned
arguments are already removed at `0x44d6f8`; AP's outstanding-argument receipt
does not apply. EAX/ECX are overwritten by the claimed loads. EDX is overwritten
at `0x44d746`, ESI at `0x44d754`, before any subsequent incoming-value reader.
EDI and EBP remain live; preserve the normal helper nonvolatile contract,
including EBX even though this launch body does not use it. Do not infer the
AP thunk's `(EBX ship, EDI gate)` arguments from the matching six-byte length.
A corresponding cdecl helper call can push `[EBP+8]`, then EDI, after saving
the existing volatile GPR/XMM envelope. Restore exact ESP and run the claim tail.

Incoming arithmetic flags are dead: the continuation first uses moves, then
`or [EDI+0x40],0x400` at `0x44d73a` overwrites them before any flag reader.
That OR precedes later `imul/add/adc` chains. Preserve XMM0–7 and exact MXCSR
around the helper, plus LastError through the existing `Envelope`; there is
no permission to clobber native XMM merely because the next instruction loads
XMM0. Selected regions have zero x87 instructions and zero `std`; eventual
built-helper transitive SSE2/no-x87 and four-byte incoming-stack checks remain
required. Static analysis does not replace the assembled-boundary fixture.

The sector driver calls collision `0x443b42`, command `0x443b49`, then postpass
`0x443b50`. Postpass checks gate class 18 at `0x465082`, walks contained ships
from gate flight `+0x54` at `0x46509e`, tests ship flag `0x80` at `0x4650a6`,
pushes the gate, puts ship in ECX and calls the launch at `0x4650af`. Launch
class-18 and capital/small paths join before this seam; non-gate launch branches
to `0x44d80a` and bypasses it. A small ship missing its slot returns early.
Native scan uses **seven** slots at gate flight `+0x1dc+4*i`, limit 7 at
`0x44d4b8`; slot six is the existing player slot. Shared traffic scans six
slots after excluding the player object, so it must retain that player guard.

Before the seam, native code has written exit P into phys `+0x30..38` at
`0x44d6b5`, built attitude, published order `0x21` (`0x44d6fb`), flag/phase
`f+0x218=1` (`0x44d705`), and deadline `f+0x214=now+900` (`0x44d723`), using
clock `*0x5a83b0 +0x708`. The seam precedes the **16-byte** P-to-flight copy
at `0x44d72f/733`. The current helper changes only three position DWORDs;
the native fourth copied word remains native. The continuation forms initial
position `f+0x240 + 1,348,650*gate_forward/65536` at `0x44d7c3..7f8` and
publishes target gate to `f+0x224` at `0x44d7ff`. Gate launch speed becomes
120000, or 60000 for a capital (`0x44d9e1..9ee`); desired 20000 is clamped
by the already reviewed vmax getter `0x45a450`. Sector insertion is native
`0x452b70` at `0x44da0f`; re-plan time `f+0x210` and flag-0x80 clearing follow.

Order `0x21` resolves to `0x45f0ad` through the actual dispatcher table
`0x462f5c/462fb4`. It reads deadline, forms positive milliseconds left, squares
and scales by 1665000/1000000, multiplies gate row 2 and adds **the same flight
P** (`0x45f171/195/1a8`). Its common tail writes phys at `0x45f096` and zeros
desired/current speeds at `0x45f09a/0a1`. On completion it copies the full
flight point back to phys at `0x45f0fd/104`, clears flag `0x400`, order and
phase, and calls the already reviewed wake at `0x45efcc`. The P offset therefore
reaches the initial warp position, every order-0x21 frame and final snap.

Order `0x1e` resolves to `0x46161c`: gate path forms `(ship_pos-gate_pos) dot F`
through `0x432ae0`, compares absolute axial distance to gate radius for native
large predicate `0x449690`, or radius/4 otherwise (`0x4616df/e3`). Until done,
it constructs the target from **current ship position plus an F-only vector**
at `0x46171b..765`. Thus it preserves lateral placement, rather than rebuilding
a central gate point. Native speeds remain 60000/120000 at `0x46178f/7a3`.

## Controller readers and hull width

The source audit covers arrival, commit/escort, B-state refresh, E verdicts
and the frame worker. The five base getter ABIs and `read_dynamics` are reused;
no new native getter call is proposed. Engine objects handed to A/B/E/arrival
are synchronous simulation-thread borrows. Module boards/arrival rings retain
pointer values only as lookup identities plus native ID, copied scalar states
and timestamps. `ctl_frame` proves membership against the current walk/B live
set and reads no engine object through a stale board pointer. Preserve complete
snapshot overflow refusal, current-ID checks, clock/sector reset, freshness and
expiry; a pointer-only board match is insufficient. The launch happens after
command/B, so its first synthetic arrival state is intentional: native target
and final warp position are not yet published at the seam.

| Reader requirement | Established FL input / implementation implication |
| --- | --- |
| Gate/object frame, radius and player exclusion | Existing phys `+0x70`, position `+0x30`, basis rows `+0x40`, obj radius `+0xa4`, class/type `+0x48/+0x4a`, ID `+8`, flags `+0x40`; retain player bit 2 and manual-control/autopilot checks from base contracts |
| Member state | Existing speed/desire `+0x10/+0x14`, rates `+0x24..2c`, slip `+0x60..68`, flight order/target/point/basis `+0x258/+0x224/+0x240/+0x870`; deadline `+0x214` proved above |
| Type/code/rot acceleration | TShips root **`0x5a83e4`**, stride `0xdb8`, class `+0x14`, record rotational acceleration `+0xd0`; base/gate contracts; `ship_gate_ctl_glue.cpp` arrival and `.h` `ctl_read_state` still read AP `sites::kShipTypeTableSlot` and require edition binding |
| Hull model key | **node `+0x144`**, not AP `+0x140`; native setter `0x4a365d`, collision `0x4a5f99`, frustum `0x4e9254` |
| Resource cache root/hash | **`*0x5aa100 +0x14`** (`0x4a1a80/89`); hash buckets/size `+0/+4`, key `id+1`, bucket `(key & (size-1))*4`, entry next/key/model `+0/+4/+8` (`0x4a1a85..aee`) |
| Model and all-LOD extents | Array `model+0xc`, signed short count `+0x10`; `0x4a36dd/e3/f7`, `0x4a17eb..1849`; LOD extents `+0x20/+0x24/+0x28`, native consumer `0x4e9287/28e/2ab` |
| Extent meaning | `0x498560..8618` computes per-axis max of `abs(part_center)+part_half_extent`, part fields `+0x40..48/+0x50..58`; BOB store `0x49c2c7` and BOD store `0x4a183f` publish those three values to LOD `+0x20` |
| Scale and placement | Node uniform/axis scales `+0x70/+0x80..88`; body setter `0x4a366b..695` and frustum `0x4e927a..9329`. Child list `+0xc`, sibling `+0`, sentinel `[child]==0`; local position `+0x30`, local basis `+0x40` |
| Child transform and collision filter | `0x49439d/3a0` rotates child local offset, parent's flag `+0x12c &0x10000` bypasses axis scale (`0x4943a8`); otherwise axis fields scale it. Child `&0x40` applies parent uniform scale (`0x49440f/41c`). Native collision requires tree bit `0x1000000` without no-collision bit `0x8000000` (`0x4a5f7b..599`); retaining the existing conservative no-collision-only filter is consistent |

`capital_half_width` is reachable with **traffic alone**, not only lane:
`ctl_commit -> width_ensure` and arrival's default `X3M_LANE_W_ENTRY=1` path.
It reaches `wk::hull_width/hull_model`, whose AP root `0x608518` and model
offset `+0x140` must become edition inputs. The bounds, 128-node budget,
three-level recursion, 64-entry hash-chain limit, 16-LOD limit, finite/scale
guards, unknown-width fallback and retry generation remain shared policy.
Do not skip the width path or mark an unread width proven to admit FL.

The hull reader does **not** call native `0x4a1a40`: even a hit enters a mutable
loader, and a miss can allocate/load/recover/callback. Replicate only the
read-only hash walk and copy derived widths; retain no model/LOD pointer in
module state or across `read_dynamics` getter calls. Resource destruction
`0x4993b0` frees attached storage before removing its map key at `0x499833`
and freeing the resource at `0x49984a`, as established by
[resource lifetime](registry-value-lifetime.md). Map membership
is not a lease. The existing synchronous game-thread/no-engine-call read scope
is required; these static bytes do not prove concurrent resource-reader safety.

Native launch eventually invokes the wait callback at `0x44db72`, with
`(ship+0x80 context, reason 1)` and caller cleanup, then clears token/context
`+0x7c/+0x80`. The injected helper must have returned and released all local
engine/model borrows before resuming native work. No claim that the complete
launch/postpass is callback-free follows: postpass has three indirect calls.
No lock or new recursive-controller mechanism is authorized by this receipt.
The launch has no SEH registration; a foreign exception or synchronous fault
crossing an injected naked frame has no new unwind guarantee. Eventual CPU,
LastError, normal-return/refusal, rollback and nested-callback fixtures remain
implementation acceptance requirements.

## Measured resource mismatch and remaining design input

The producer resolves **complete exact keys** through clean root numbered
catalogues followed by addon/addon2 numbered catalogues, with no loose files,
namespace stripping or fallback. Its local `exact_body` adapter reads only
the requested stem's canonical binary/text keys through `ExactAssets.candidates`;
the generic AP `bob1.resolve_body` combines namespaces and is not used. Four
positive in-memory checks preserve explicit namespace, highest-CAT precedence
over format, format rank within that CAT and later duplicate selection. Three
negative checks reject an absent root body despite namespaced alternatives,
a wholly missing body and an unmodelled exact-key language variant. These
lookup checks do not read or write an executable or resource payload.
FL `addon2/types/TGates.pck` wins from
`addon2/04.cat`, 26 rows, decoded SHA-256
`49033a0009ee2da9317b9fffe1dd4bdf3347e24d7a62b9003d1f813ec4dd6363`.
Clean AP has 25 rows. All first **25 row-to-scene assignments, six unique
scene payloads and six unique resolved body payloads** are identical to clean
AP stock. These measured identities are fully enumerated with source/member
and hashes in the result. The non-resource numeric dummy 100000 in the Terran
scene is explicitly excluded from body comparison, not treated as mesh data.
New FL row 25 names `others\\hsap_scene` / `SS_WG_HYPERSPEED`; its geometry is
unqualified here.

Concrete witness: FL rows 0..3 use stock `others\\argon_gate_scene` (scene
SHA-256 `ac6229f348025aa0eac980d2a3da5d75877bc446664cb9a29e5275a50ea72c1f`)
and stock collision body `objects/others/argon_gate.pbb` (4,392,081 decoded
bytes, SHA-256 `02cffa97e652f2dcf80dc4fa9c72c78224d56df0b1b58c739e06858b36784fa2`).
This is not the installed AP Mayhem bubble without a ring. Existing measured
[AP gate geometry](../ship-navigation-ai.md#109-gate-hole-geometry) gives stock
Argon clear coaxial radius **0.605 r**, stock catapult rows 5..8 **0.127 r**,
and stock Terran **0.654 r**. Those figures are reused prior mesh measurements
on the identical payloads, not newly executed FL collision measurements. The
installed-Mayhem Terran value **0.663 r** belongs to a different body payload.

Affected shared policy is concrete:

- `ship_gate_geometry_core.h::gate_no_mesh(type<=20)` and
  `gate_terran(21..24)` choose the assumed family; both arrival placement and
  E suppression consume it.
- `ship_gate_arrival_core.h::capital_offset(r_gate,w,type)` chooses `0.6*r`
  without a width clearance for "no mesh", or a `0.663*r-w-50m` cap for Terran.
- `small_side_spawn` uses lateral `(±0.87*r,0.2*r)` by slot parity and capital
  width only; its lateral radius is approximately **0.893 r** (inferred
  arithmetic), outside the stock Argon projected hole. It has no gate-type or
  arriving-small-hull clearance input.
- `arrival_gate_row` suppresses native gate warning unconditionally for
  `gate_no_mesh` after its time/direction guards; otherwise it uses that same
  installed-Terran clearance. Hook success does not validate these verdicts.

The existing verified width is a conservative lateral disc radius; an unknown
width remains the object radius/fail result, not zero. Along with positive
gate radius, gate-local frame and resource-specific coaxial clearance, it can
support a stock-ring bound of `offset + width + margin <= clear_radius`.
This is a necessary geometric input for a future design, not an accepted new
offset law: axial profile, free-roll/hull reach, opposite traffic and the small
arrival's own radius still matter. Keep the installed AP behavior for its
established resource family. A minimal edition/resource-aware classification
and clearance design must resolve these inputs before traffic activation;
using stock AP identities here does not qualify arbitrary modified FL media.

Performance pass, inferred from current source: seam work runs once per gate
arrival; board/ring work has fixed storage and no allocation/lock. Width entry
walks are bounded but add real cold-path cost; cache only derived dimensions
under existing generation/retry limits. Preflight code/layout on installation,
not every B visit. Dynamic timing, callback schedules, FL gameplay and native
Windows behavior remain unmeasured. This contract does not admit lane, gate
tree reads, docking, arbitrary resource mutation or new native calls.

## Small-arrival coefficient initializer and bounded placement closure (2026-10-31)

The original gate initializer `44a528..44a567` reads signed gate subtype `+4a`,
tests `(subtype-5) <= 3` unsigned, and writes the gate flight displacement at
`+190/+194/+198`. The X/Y/Z coefficients are respectively zero,
`0xfffff334` (-3276) for subtypes 5..8 or `0xffff7334` (-36044) otherwise,
and `0xfffffd71` (-655). Thus stock gate rows 0 and 21 both use approximately
`(0,-0.55,-0.01)` in Q16. These are original immediates, rather than TGates
radius-derived or fixture-selected coefficients. The exact closed initializer
has 12 instructions, internal branches and no calls.

The [local extractor](../../../verification/probe/fl_arrival_occupancy_original.py)
validates the original FL image, complete instructions and all reachable edges
for this initializer, small placement `44d4a7..44d6b9` and rounded rotation
`50e0c0..50e1dd`: **244 instructions / 886 bytes**, including one eight-byte
constant. The only external edges are the slot-missing branch to `44db8c`
and call to the included rotation leaf. A fixture missing-slot return is an
explicit controlled no-seam exit; the original whole-function epilogue and
earlier classification are not claimed. Placement ends immediately after the
original 16-byte physical-position store at `44d6b5`, before attitude setup;
the existing installed `44d729` callback is invoked separately with its already
qualified modeled attitude/order seam.

Slot angular step at `591c58` is the source double `2730.666666666667`.
For slots 0..6 the folded sine/cosine reads are bounded to the quarter table
`5cb3f8..5db3f8`, inclusive. That table is runtime BSS, initialized through
`50d260` and native sine helpers. The occupancy fixture supplies an explicitly
modeled Q16 table and records its hash and every accessed index; original sine
initialization remains unqualified. Therefore these controls establish original
placement execution against admitted table inputs and exact fallback retention,
not exact game sine values or universal stock placement coordinates. Extracted
bytes and table/header data remain local.
