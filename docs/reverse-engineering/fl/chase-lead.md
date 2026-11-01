# FL chase lead, central HUD and true-forward anchor contract

Bounded offline RE, 2026-10-29. **All nine current lead/HUD/timing seams have
FL counterparts, but the inline overlay layout, publication registers, solver
output slot and native hide ABI differ from AP.** Keep the portable projection,
identity/ticket policy and diagnostic timing state shared; supply explicit FL
bindings. This note does not implement an adapter or establish runtime/visual
acceptance. It covers `src/engine/ap/chase_lead.{cpp,h}`, `chase_lead_core.h`,
and `chase_native_timing_core.h`, without porting fire, aim tracing or steering.

Reuse the accepted [camera](chase-camera.md) and
[transition](chase-transition.md) contracts,
[FL HUD camera lifetime](ui-camera-lifetime.md),
[camera matrix/projection interpretation](camera-buffers.md),
[owner/callback exclusion](ui-render-exclusion.md), and
[resource callback lifetime](registry-value-lifetime.md).
The existing [lead architecture](../../architecture/chase-lead-marker.md) and
[central HUD/timing architecture](../../architecture/chase-central-hud.md) describe
AP policy; old installation/acceptance statements there are not FL evidence.
No additional lifecycle framework or per-draw registry observer is needed.

## 1. Supported behavior and owning native objects

Current source installs three required predictive-marker sites, a separate
one-site central-instrument transaction, and five additional timing sites only
with telemetry, a positive QPC frequency and the central endpoint installed.
Failure of the optional central transaction leaves lead independent; failure
of timing leaves both display features independent. Native ballistics, weapon
eligibility, icon selection and ordinary failure/hide paths remain native.
Only the internal-view-bit rejection is overridden after the existing scope
checks. No additional solver or text producer is called by the extension.

The predictive marker requires a successfully published **bound** chase pose,
active current cockpit/update/thread, view mode **258**, connect **0**, flags
mask `0x4` clear, main gun index **0**, tracking short **2 or 3**, and live
same-sector direct reference/target objects. Central instruments have no target,
tracking or solver-success requirement: they require the applied bound main
chase cockpit plus display, scene, cleanup and cached-texture availability.
These choices preserve the existing feature scope rather than widening it to
other gun groups, scripted views, nested targets or arbitrary cameras.

Both existing central-anchor choices must remain available:

* **`forward`** retains the ship-forward world direction derived from the
  successfully written camera basis/relative basis. At final FOV it projects
  that direction at infinity and adds one common pixel offset to up to five
  active native HUD nodes. Camera translation is deliberately absent.
* **`centre`** retains native central positions, with no anchor ticket,
  projection or node rewrite. Current setting policy chooses forward when the
  environment variable is absent or exactly `forward`; `centre`, empty and
  other present values select centre. Forward is the current source/launcher
  default. Preserve this policy in the edition adapter.

Native inline overlay **O = cockpit+`0x3bc`**, not AP's `+0x3ac`. This is the
object passed by updater `0x4229bc/0x4229c2` to **`0x42c330`**. Its entry takes
ECX=O and copies it to EBX at `0x42c33d`; the routine realigns ESP to 16, uses
an EBP frame and returns with plain RET at `0x42f019/0x42f025`. Its only surveyed
direct caller is `0x4229c2`. It is distinct from the separately allocated
**target-overlay scene record**, pointer O+`0x3e8` = cockpit+`0x7a4`, whose
lifetime is already documented in the HUD note. Lead/HUD nodes here belong to
**cockpit+4**, not that record's separate scene.

| Field / extent | FL native evidence and use |
| --- | --- |
| O+`0x3c0` / O+`0x3c4` | Target-body enable and cockpit backpointer; `0x42c345/0x42c352`. AP uses +0x320/+0x324. The latter resolves back to cockpit+0x780, also initialized by the cockpit constructor. |
| O+`0x3e0/+0x3e4` | Native cached lead x/y; successful stores `0x42cbe7/0x42cbed`, hide stores `0x42cc07/0x42cbfd`. They are cockpit+**0x79c/+0x7a0**; AP's +0x340/+0x344 and cockpit+0x6ec/+0x6f0 are wrong. The relevant inline extent ends at O+0x3e8; it is not the full 0x8c0-byte cockpit allocation. |
| O+`0x3c/+0x40` | Lead entry active flag / node pointer, preserved from AP. Successful native creation/update passes ECX=O+0x3c to `0x427b90` at `0x42cbb5`. Node x/y stores at `0x42cbc9/0x42cbcc`; flag `0x200` at `0x42cbdd`. |
| Central entries | Entry offsets **0, 0x208, 0x21c, 0x230, 0x294** remain unchanged. Active flag is entry+0, node pointer +4. Three additional entries **0x118, 0x140, 0x12c** must be inactive before admission; the native rejected branch cleans all eight at `0x42d019..0x42d068`. |
| Native central positions | Respectively **(0,9), (-70,-10), (70,-10), (0,40), (1,-25)**. Writes at `0x42ccc7/0x42ccce`, `0x42cdee/0x42cdf5`, `0x42ceb1/0x42ceb8`, `0x42cf74/0x42cf7b`, `0x42cd98/0x42cd9f` prove these match current shared anchor constants. Glyph hot spots are not established by node origins alone. |
| Cockpit display fields | Draw-2D enable **+0x244** tested at `0x424870`; display-body enable **+0x2b0** at `0x42489a`. AP's +0x234/+0x2a0 must change. HUD scene pointer +4, HUD camera +8 and world camera +0x58 retain their established roles. |
| Common scope fields | Reference/view +0xc/+0x10; mode +0x150; flags +0x1a0; connect +0x1c0; main gun +0x1d8; target +0x1e0; tracking short +0x1e4; sector +0x54. Ship/target native id +8, owner +0x54, type +0x48 match accepted camera/transition evidence. |
| Native node ownership | Node scene +0x1c must match current cockpit+4; Flags2 +0x130 must contain 0x200 and exclude 0xf000. Write current node x/y at +0x30/+0x34 only after fresh identity and writable-span checks. Node/scene pointers are borrowed labels, not leases. |

## 2. All nine seams and exact native operands

Preferred VAs below use image base `0x400000`; runtime means admitted FL loaded
base plus RVA. Each half-open span owns complete instructions. No span contains
a HIGHLOW operand, and the `.text`/HIGHLOW censuses find zero direct interior
edges or relocated pointers into the spans. This bounds the native profile;
it does not prove arbitrary computed/third-party entry absent.

| Source role / AP VA | FL span | Relocation and live contract |
| --- | --- | --- |
| Lead gate / `0x42a6fe` | **`[0x42c7b9,0x42c7bf)`**, 6 | Near JE to **0x42cbf5**, rel32 at +2. The immediately preceding `test byte [ESI+0x150],1` supplies ZF; **ESI=cockpit, EBX=O, EBP=overlay invocation frame**. Override only saved ZF when qualified, then replay branch. Native tracking 2/3 and following target/range/weapon tests remain intact. |
| Lead publish / `0x42aaae` | **`[0x42cbe7,0x42cbed)`**, 6 | One store **`[EBX+0x3e0]=ECX`**. **ECX=x, EDX=y**; next store is `[EBX+0x3e4]=EDX`. AP handler reads/rewrites ESI/EDI instead. Preserve EAX (native marker node) and all unrelated registers. Failure substitutes -1 into ECX/EDX only after owning-marker validation/hide. |
| Final FOV / `0x4213dd` | **`[0x422cf5,0x422cfb)`**, 6 | One compare **`[EBX+0x240],ESI`**; EBX=cockpit. Sector/dust FOV stores have completed at 0x422cc5/0x422cef; next conditional consumes this replayed compare's flags. It updates the cached FOV at 0x422cfd, then invokes distance/HUD producer parent at 0x422d05. |
| Central gate / `0x42aae0` | **`[0x42cc1e,0x42cc24)`**, 6 | Near JE to **0x42d019**, rel32 +2, consuming `test [EAX+0x150],1` at 0x42cc17. **EAX=cockpit, EBX=O**, EBP=same overlay frame. Qualified central admission clears only saved ZF. |
| Solver begin / `0x42a792` | **`[0x42c87b,0x42c880)`**, 5 | CALL **0x4505a0**, rel32 +1. Forward exactly once with original **ECX=ship**, five callee-cleaned stack words and original FP/error state. |
| Solver end / `0x42a797` | **`[0x42c880,0x42c888)`**, 2+6 | TEST EAX then JE **0x42cbf5**, rel32 +4. EAX is solver result, recorded as such by timing. The callback must preserve it and replay the TEST before its branch. |
| Distance begin / `0x423007` | **`[0x4248ad,0x4248b2)`**, 5 | CALL **0x4266f0**, rel32 +1. **ECX=cockpit, ESI=cockpit**, two callee-cleaned stack words already prepared. This call is retained, not reinvoked. |
| Distance end / `0x42300c` | **`[0x4248b2,0x4248b7)`**, 3+2 | Load EAX from `[ESI+4]`, TEST EAX; ESI remains cockpit and EBP remains this parent invocation's native caller frame. Replay once. |
| Central end / `0x42aed6` | **`[0x42d06d,0x42d073)`**, 6 | Load EAX from **`[EBX+0x3c4]`**. Both normal draw and hide branches converge; **EBX has been restored to O** after temporarily selecting individual entries. |

The producer proves four normal-return paths: lead gate to central gate,
solver-return test to central gate, central gate to central endpoint, and the
distance CALL to its continuation. Calls are assumed to return; exceptions and
nested native mutation are not manufactured into completed timing samples.
Instruction-boundary comparison is independent of slicing the named spans.

## 3. The solver point is a different FL stack slot

Let **P** be the stable native overlay ESP before the solver setup, after its
saved-register/local allocation. At `0x42c842`, the existing gun-position helper
receives P+0xa0; `0x44c2e0` returns with RET 8. Then:

1. `0x42c850` forms **P+0xe0**, the absolute world-point output.
2. After pushing it, `0x42c858` forms `[ESP+0xc4]` = **P+0xc0**, the direction
   output. After that push, `0x42c860` forms **P+0xa0**, gun position.
3. Target and gun index are pushed. `0x45a9e0` consumes the gun-index word
   (RET 4), returns the native gun flags/mask in EAX, which is pushed.
4. **`0x4505a0`** therefore receives ECX=ship and five stack words:
   mask, target, gun-position pointer, direction-output pointer,
   absolute-world-point-output pointer. It returns EAX success and **RET 0x14**.

The no-prediction fallback directly copies target root+0x30 into the **last**
output at `0x4506ce..0x4506df`; the other output is target minus gun position,
normalized via `0x432b30` at `0x450714`. The predictive branch forwards both
outputs to `0x44ff50` at `0x450737`. The main-gun native projection explicitly
reads P+**0xe0/+0xe4/+0xe8** at `0x42c89c/0x42c8b1/0x42c8c6` and subtracts
P+0xa0 gun origin. This distinguishes an absolute point from a direction and
avoids assuming AP's stack slot carries the same quantity.

At successful publication the native outgoing arguments have been cleaned and
P remains the overlay's local base. PUSHFD before PUSHAD saves **P-4** in the
pushad ESP slot. The FL adapter must consequently read the owned absolute
point at **saved ESP+0xe4**, not current AP's **saved ESP+0xc4**. Projecting the
latter as an absolute world position would be wrong. Keep only the three
copied integers across callbacks; no stack pointer or solver-owned reference
survives the overlay invocation. The extension uses the native solution once,
with no new ballistic solver, turret query or object-position correction.

The accepted camera note already proves that the post-pose loop derives target
node+0xf0 camera coordinates from node+0x30 minus current sector-camera+0x30.
The solver uses this same base domain for direct objects of the same type-1
sector. Renderer node+0xb0 is not an extra correction to this point.

## 4. Projection, marker writes, HUD nodes and native aim publication

`chase_lead_core.h` is portable and retains the accepted row-vector convention:
point projection subtracts the camera's current integer position, transforms
through transposed fixed-point camera basis, rejects nonpositive depth, and
uses final FOV/plane/viewport values. Direction projection omits translation.
Integer viewport widths/heights retain the existing half-up extent rounding and
integer half extents; final signed pixels truncate toward zero. FL helper
**0x419e10** independently ends with signed IDIV at 0x419e34. SSE2/double
projection avoids the native intermediate fixed-point losses; reproducing x87
arithmetic is unnecessary.

The existing projection reader's camera fields match the accepted FL camera
contract: position +0x30, 16-byte-stride basis +0x40, binary-angle FOV +0x298,
view-plane +0x300/+0x304, native viewport pointer +0x278 and normalized viewport
+0x288. Replace the fallback root **0x606f38** with **FL 0x5a82f8**. Its +0
screen record supplies signed width/height words at +4/+6; plane fallback is
+0x28/+0x2c. Require equal sector/HUD normalized viewport, native viewport rect
and effective planes as current source does. Keep this native HUD in real
pixels through the existing FL UI role/exclusion boundary; menu scaling cannot
silently change only one side of this projection.

Source write protocol remains:

* Gate: clear any ticket with the same original EBP invocation frame; require
  current pose and scope; create one of eight fixed pending records; change
  only saved ZF. No successful native publication means no point to reuse.
* Publication: remove that frame's pending record, revalidate the same
  lifetime/update/ship/target/camera/scene/sector and current active marker,
  copy FL's world point, project and write **eight node bytes** at +0x30.
  Replace **ECX/EDX** so the native cached-pair stores publish the same pixels.
  Store the three point integers and owned projection values for final FOV.
* Final FOV: consume the matching cockpit/update ticket once; reject a changed
  identity or marker. If projection inputs are unchanged, skip reprojecting.
  Otherwise update the same node x/y **and O+0x3e0/+0x3e4** (16 bytes total).
  Failed projection hides only a still-owned marker; foreign/reused owners
  receive no extension write.

This is also native **aim-coordinate publication**. The cached pair is read by
**`0x427170`**, ECX=cockpit, at `0x427173/+0x79c` and `0x427195/+0x7a0`.
It takes two callee-cleaned stack words, returns **AL** as a Boolean and uses
RET 8; native early signed tests reject negative cached coordinates. Its sole
surveyed direct caller is **`0x44ed15`** in native fire control.
Normal load resets the pair to -1 at `0x41d5eb/0x41d5f5`; native failure does
likewise in the overlay. This establishes the exported native pair and one
consumer edge, not an FL fire/aim/steering port or universal ordering proof for
all script/native callbacks. The final-FOV seam precedes `0x422d05`'s native
notifications, so same-update notifications see the finalized publication;
normal rendering follows the cockpit registry walk. Fire/aim/steering admission
and native consumer semantics remain separately owned dependencies.

For true-forward HUD, derive ship basis from the successfully written pose as
`transpose(view_rel) * camera_basis`, normalize, and retain its third row as
three 16.16 integers. Central admission stores one of eight current-update
anchor tickets; final FOV consumes that exact ticket. Revalidate all active
nodes' current scene/flags/write spans, reject duplicate nodes and any integer
overflow **before the first group write**, then write at most **five times
eight bytes**. The five native relative positions above remain unchanged around
the common direction offset. Forward projection failure leaves the native
central positions produced earlier in this update; centre does no anchor work.
No texture/icon/ballistic producer is invoked by anchor projection.

## 5. Hide helper, texture availability and callback/lifetime boundaries

The full FL hide helper is **`[0x427c20,0x427c3f)`**, 31 bytes, not AP's
`[0x426280,0x42629e)` 30-byte ESI helper. FL takes **ECX=entry**, saves its own
ESI, tests entry active/node, pushes node+0x1c and calls **0x4a5740** at
0x427c32, clears entry+0 at 0x427c37 and returns plain RET. An FL wrapper must
supply ECX rather than reuse AP's inline assembly ESI setup/absolute target.
Validate this helper's complete native contract at admitted loaded addresses.

Detachment is not a trivial no-callback store: `0x4a5740` can release node GPU
payload at `0x4a5758 → 0x4e7240`, recurse into children at 0x4a5773, and enter
node resource handling at 0x4a5787. The accepted resource note establishes the
external vtable release before wrapper free. Hold **no module lock** across
hide; remove the relevant pending ticket before calling, and revalidate complete
cockpit generation, scene and marker afterwards before cache writes. Foreign
exceptions propagate; no successful hide/completed timing may be inferred from
an interrupted call. If native code itself resumes an invalid destroyed owner,
this extension cannot certify the original continuation safe.

Texture **15** is used by the existing distance/speed producer
**0x4266f0**, ECX=cockpit, two stack words, RET 8. It checks current scene
activity, requests texture 15 through **0x5123a0** at 0x42671b and can return
early. Its parent **0x424860** has sole surveyed caller **0x422d05**;
0x4266f0 has sole surveyed caller **0x4248ad**. Native lazy cache creation must
remain there. Central admission only reads current availability:

| Availability operand | FL binding |
| --- | --- |
| Metadata signed count | short **0x61b410**, native compare 0x5123a9 |
| Metadata rows | pointer **0x61b418**, native load 0x5123bc; index stride **0x3c**, metadata short +0xc at 0x5123c1 |
| Combined texture counts | shorts **0x5fb404** and **0x61b40c**, loads 0x5123c9/0x5123d0 |
| Cached texture rows | pointer **0x5cb3f4**, native load 0x5123e0; stride **0x10**, surface +8 tested at 0x5123e9 and returned at 0x512400 |
| Lazy miss | `0x5123f2 → 0x5113b0`; do not call it from the gate |

A missing texture or active extra cleanup entry leaves native hiding in place;
a later ordinary producer/update can make admission succeed. Replace all five
AP texture globals explicitly; they do not share one AP-to-FL delta.

Use the accepted current **owner-thread cockpit invocation** and transition
`{generation,serial,thread}` binding, not the later renderer material/pass
borrow, to authorize this update-time work. Every pose-handler visit invalidates
its prior lead witness; only a successful bound camera write publishes one.
`camera_context` binds its first valid current thread once and rejects other
threads. It does not create an engine memory lease. Registry active-control,
ship id, target id/owner, current cameras and scene/marker identity are still
rechecked at use. Cockpit generation and object+8 equality do not independently
establish a target-lifetime serial across arbitrary callback-driven address/ID
reuse; the supported native callback closure remains a condition. The current
`active()` helper bounds 65,536 buckets/32 links
but lacks an explicit handle-zero refusal; an FL binding must honor the accepted
nonzero active-control contract rather than treating a stale matching row zero
as active control. This finding does not change production source here.

Native solver `0x4505a0` itself can synchronously call the VM through
`0x4505f1 → 0x4bb5b0` (gun-firing-mask query); original distance/HUD producers
can issue further notifications/resource work. Do not invent call-free ownership
across those native calls. The gate/publish/final checks and existing supported
callback/close/load admission are the actual boundary. The camera/transition
abnormal-update token limitation remains: a naked retained token after unwind
is not permission to access the engine. Reuse existing exclusion/exception
admission and refuse unsupported nested/foreign work; no new owner framework
is implied by this note.

Current anchor tickets actively reclaim stale generation/serial/thread records
at later central admissions and pose invalidations. Lead `pending[8]` records
instead clear by matching EBP at the next gate or matching cockpit/update at
final FOV. An interrupted update with a distinct frame can therefore retain a
slot until those paths reclaim it; capacity exhaustion refuses admission.
Do not claim automatic unwind reclamation for that array. Timing state is
separate: constructor/destructor/update boundaries revoke unfinished intervals;
normal endpoints require the same generation/update/thread/frame, and escaping
exceptions yield abandoned intervals rather than cross-update pairings.

## 6. Preservation, cost, edition bindings and reproduction

The port needs nine exact site bindings, FL hide/projection/texture roots and
layout/register adapters, with the existing camera and transition services
qualified first. The two display choices, predictor and native instrument
cleanup all remain required. `chase_lead` makes no direct fire/aim/SETA API call;
those consumers still need their separately admitted implementations. Transition
calls this module's `native_timing_invalidate`; that API must remain FL-safe
even when optional timing is disabled. The diagnostic native timing core can
be shared: eight thread slots, four phases, four first/four recent slow samples,
10 ms threshold. FL timing extraction uses ESI=cockpit at the lead gate and
distance endpoints, otherwise O+0x3c4, with EBX=O and current EBP frame. Preserve
native EAX solver result at the solver endpoint.

Preserve all GPRs, EFLAGS/DF, original stack, LastError, x87 state/control,
MXCSR and XMM0–7 around callbacks; establish clean private masked FP state for
shared SSE2/libm work and restore native state before replay. Only qualified
gate ZF or publication ECX/EDX may deliberately change. Replay original CALLs
once under native state; rebase their rel32 operands and both conditional
branches. The callbacks obey the project's four-byte incoming stack contract
regardless of native routine alignment. No hook steals a native SEH prologue.
Use the existing initial-static-load installation window, transactional rollback,
byte ownership and process-lifetime sites through device recreation; explicit
shutdown requires quiescence. All globals/helper targets use loaded base+RVA,
including scattered texture BSS slots. Neither AP literals nor file hashes
alone authorize a loaded FL patch.

Source-derived cost is per cockpit update/overlay invocation, with **no new
per-draw reads, allocations or native solver/producer duplication**. At most
eight fixed pending/anchor slots are scanned, up to five HUD nodes validated,
and at most 40 group-coordinate bytes written. Same-projection finalization
skips the extra lead projection. Same-update ownership and writable checks are
correctness boundaries, not permission caches to remove. Each installed seam
still pays the full CPU boundary; five timing seams are telemetry-only.
Timings include intervening hook overhead and do not establish gameplay FPS.
Admission exposes original native prediction/instrument work to chase views
where vanilla hid it, so runtime cost/loading behavior needs measurement on the
implemented adapter. Static evidence supplies no timing estimate.

```sh
python3 verification/results/fl-port/chase-lead-contract/produce.py
python3 -m py_compile verification/results/fl-port/chase-lead-contract/produce.py
```

Measured offline result: **14 regions, 3,707 instructions, nine seams, 113
operand anchors, 46 independent region/seam edges, four normal-return CFG
pairs, seven rejected mutations**. The producer uses project PE32/Capstone and
FL boundary/HIGHLOW utilities. Compact derived output is
`verification/results/fl-port/chase-lead-contract/contract.json`; original
EXE is read-only, raw listings remain local/untracked, and only producer/compact
record should be retained from the ignored result directory. Mutations bypass
image identity and reject wrong world-point slot, publication register, overlay
backpointer, hide ABI, texture root, cached-FOV offset and a direct branch into
a stolen instruction interior. The normal-CFG result explicitly assumes native
calls return; it is not a callback/exception implementation test.

Finite remaining checks are the actual register/stack/state adapters; loaded
bases/rollback; five-node atomic refusal and centre/forward choices; native
solver false/true outcomes and current absolute point; early/final FOV zoom;
stale/reused/nested/unwound tickets and capacity refusal; callback-capable hide
and resource cache failure; shared transition timing invalidation. User-only FL
flight acceptance must check marker/bolt alignment, settled forward HUD framing,
zoom, view changes, loads and resets. No production edits, build, Wine/native/
game execution, installation or commit was performed. Native Windows behavior
and FL visual acceptance remain unverified.
