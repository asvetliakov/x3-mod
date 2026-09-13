# Chase view: native predictive lead marker

Read-only targeted analysis, 2026-09-13, installed X3AP.exe at preferred base
`0x00400000`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Ghidra 12.1.3, existing `/tmp/x3-ghidra-research X3Render`,
`-readOnly -noanalysis`, `tools/analysis/X3CameraState.java`. Local raw output is
`/tmp/chase-lead1.txt` through `chase-lead5.txt`; do not track it. No game,
Wine, build or installation was run. Branch bytes were also read directly from
the installed PE file. This is a static explanation and design input, not an
implemented or visually verified reticle correction.

## The missing marker has its own view gate

Target-overlay update `0x0042a2d0` runs after the sector-camera chase hook in
`0x004205e0`. Let `O = cockpit + 0x3ac`. The update receives `O` at `[EBP+8]`,
keeps it in EBX, and reads the cockpit through `O+0x324`.

The predictive marker is the overlay entry `O+0x3c` (entry active flag, node
pointer at `+4`, icon ID at `+8`). Its last screen position is stored at
`O+0x340/+0x344`, exactly `cockpit+0x6ec/+0x6f0`, which the cursor-near-lead
helper `0x004257f0` consumes. This dataflow, and the ballistic solver below,
identify this path independently of the surrounding bracket and crosshair UI.

Admission conditions, in native order:

| Condition | Evidence |
| --- | --- |
| Target bodies enabled: `O+0x320 != 0` | `0x0042a2e0`; command dispatcher case `0x35`, `INS_CockpitEnableTargetBodies`, calls setter `0x00429910`. Disabling it hides all 40 overlay entries. |
| `cockpit+0x10` view object exists, has owner `+0x54`, owner type short `+0x48 == 1`, view-object type short `+0x48 == 7` | `0x0042a2ef..0x0042a31b` |
| Tracking short `cockpit+0x1e4` is **2 or 3** | `0x0042a6e4..0x0042a6f1`; this is different from the 1/4 automatic-tracking predicate used by optional chase combat tightness |
| View mode has **bit 0 set** | `0x0042a6f7 TEST byte [ESI+0x150],1`; `0x0042a6fe JZ 0x0042aabc` |
| Tracked object `cockpit+0x1e0` exists | `0x0042a704..0x0042a70b` |
| Reject view-object pair type 7 / subtype `+0x4a == 0x10b` | call `0x004191c0` at `0x0042a71a`; exact subtype name not established |
| Length of tracked object's render-node vector `+0xf0` is below `0x21dfe0` | `0x0042a727..0x0042a73f`, helper `0x00469b80`; vector-domain/units not established here |
| `GetHUDInfo` result has either low bit set | callback at `0x0042a38a`, result read from registry `*0x00608504 + 0x35`, saved at `[ESP+0x10]`, tested with mask 3 at `0x0042a745`; names/meaning of these two settings bits are not established |
| Native gun-group lead solver succeeds | `0x0042a792` calls `0x004471b0`, failure branch at `0x0042a799` |
| Native transformed/projected depth is greater than 100 | `0x0042a89f..0x0042a8a6` |

Failure reaches `0x0042aabc`: hide `O+0x3c` through `0x00426280`, then write
both stored screen coordinates to `-1`. External mode 258 (`0x102`) fails the
view bit independently of successful cursor-fire admission. No test of
`0x00607ce8` appears in this lead block. The
[cursor-fire correction](chase-mouse-fire.md) therefore does not restore it.

This routine serves multiple cockpits. It does **not** itself require the
active-control registry handle or equality of view and reference objects for
this lead block. Any chase extension must impose its own existing applied
chase/active-owner predicate. Never change the global view mode or weaken all
`+0x150` tests: neighboring tests separately control crosshairs, bars, other
HUD entries and views.

## Native prediction and screen placement

At `0x0042a750..0x0042a75b`, `0x00443000` produces the selected gun-view origin
in caller local `[ESP+0x40]`. `0x00450f50` maps `cockpit+0x1d8` to a gun group;
`0x004471b0` receives the view object, tracked object and that origin, selecting
the native eligible weapon speed and firing mask. It calls `0x00446c00`, which
solves interception using target-minus-shooter velocity and native projectile
speed. Its output at caller `[ESP+0xc0]` is an **aim-equivalent point**: target
base position plus relative velocity times the solved time, not an assertion
about the target's absolute future world position. A second output at
`[ESP+0x80]` is the normalized aim direction. Native fallback with no eligible
speed uses the target base position and still succeeds. Preserve this behavior.

For main guns (`cockpit+0x1d8 == 0`), the caller subtracts the gun-view origin
from the point at `0x0042a7ae..0x0042a7de`. It then transforms the difference by
the inverse native gun-view basis (`0x004431a0`, inverse multiply at
`0x0042a841`) and inverse `cockpit+0xf0` (`0x0042a84a`). The vector is now in
`[ESP+0x40/+0x44/+0x48]`. **The sector-camera translation is never subtracted.**
Thus admission alone retains first-person gun-origin projection and cannot
correct finite-distance chase parallax.

Projection at `0x0042a84f..0x0042a8a6` uses cockpit `+0x230` for the angular
scale/depth. Pixel conversion at `0x0042a9a8..0x0042aa9c` uses cockpit-scene
camera `cockpit+8` viewport fields `+0x288..+0x294` and view-plane dimensions
`+0x300/+0x304` (global fallback). `+0x230` is updated later in cockpit update
at `0x004213dd/0x004213e5`; zoom/update ordering therefore needs attention in
any reuse. Pixel positions are relative to viewport centre.

Native range/alignment state chooses icon IDs `0x18a..0x18f`; that section
(`0x0042a8ac..0x0042a9a8`) uses `0x00444ce0` and retains the native flash/rotation
behavior. At `0x0042aaa9`, `0x00426230` updates the selected icon, writes its
node position as `(x_pixels,y_pixels,0)`, and sets node `+0x130 |= 0x200`.
The caller publishes the same coordinates to `O+0x340/+0x344` at
`0x0042aaae/0x0042aab4`.

The screen marker does not use cockpit-scene camera **basis** in this path.
Enabling `X3M_CHASE_SCENE_FIX` does not fix the view admission, finite origin,
or this explicit screen projection. Keep marker placement and cursor-near-lead
coordinates consistent; changing just the visible node would leave firing
range/target selection using a different position. The existing cursor-near
helper also rejects either negative relative coordinate; preserve or investigate
that existing native behavior separately, rather than silently expanding it.

## Candidate hook boundaries and a bounded chase extension

| Site | Decoded original operations | Purpose |
| --- | --- | --- |
| `0x0042a6fe`, 6 bytes | Near JZ to `0x0042aabc` | Change only this view-bit rejection for the active, currently applied chase cockpit/main guns. ESI is cockpit; EBX is overlay. Preserve native branch for every other context. |
| `0x0042a84f`, 8 bytes | `FLD1; MOV EDX,[EBX+0x324]` | Candidate projection-fork entry before the native x87 projection starts. Ballistic point remains at native caller ESP `+0xc0`; no need to rerun or replace the solver. |
| `0x0042a9a8`, 9 bytes | Load cockpit then cockpit-scene camera | Candidate native pixel-projection boundary. Has an incoming jump to its start from `0x0042a99f`. Preserve that entry and do not patch only the interior camera load. |

These are whole-instruction candidate spans, **not qualified hook claims**.
A production site probe still needs executable/ABI validation, all interior
branch-reference checks, relocation and rollback tests. Retain the full
register/flags/x87/SSE/LastError boundary and the four-byte incoming stack ABI.
Use SSE2 for any new arithmetic.

The smallest correct feature scope is the **one predictive marker in the
applied main-gun chase view**: retain all native enable/owner/tracking/weapon/
HUD gates and the native solver/icon logic, admit that view at the one branch,
and project the solver's aim-equivalent point through the actual sector
camera (`cockpit+0x58` position, basis, FOV, viewport). A projection fork around
the native depth/pixel calculations can preserve the intervening icon-state
block. Fail closed through the original hide path on an invalid point/camera;
keep the stored aim-icon coordinates identical to the visible pixels. Avoid
heap allocation, per-frame logging or repeated native solver calls. Reuse
existing validated current chase context, without admitting stale/other
cockpits merely because chase was applied previously.

## Follow-up: coordinate domain and final-FOV proof

Additional read-only evidence is local in `/tmp/chase-lead-domain1.txt` and
`chase-lead-domain2.txt`, plus the existing
`/tmp/x3-camera-study/f4205e0.lst`. The scoped static domain question is resolved
for direct objects belonging to the cockpit's current sector. This does not
admit a tracked child object or a different sector by assumption.

`cockpit+0x54` is the sector-space object: dispatcher case `0xb`, named
`INS_CockpitSetSectorSpace`, stores the resolved object there and calls
`0x00420360`; that helper checks sector type short `+0x48 == 1`.
Immediately after the existing chase-pose hook, `0x00420e10..0x00421013`
walks 32 sector object lists from `*(cockpit+0x54)+0x50` (list stride `0xc`).
For every member object it computes:

```text
q.x = dot(object.node.position30 - sectorCamera.position30, sectorCamera.row40)
q.y = dot(object.node.position30 - sectorCamera.position30, sectorCamera.row50)
q.z = dot(object.node.position30 - sectorCamera.position30, sectorCamera.row60)
```

The fixed-point basis division is by 65536, with each product rounded before
summing. Writes at `0x00420ec8`, `0x00420f60`, and `0x00420ff8` put the result
in object node `+0xf0/+0xf4/+0xf8`. Thus the previously unnamed target `+0xf0`
vector in the lead-distance gate is **camera-space position computed for this
cockpit update**. In particular, the native engine itself subtracts precisely
the same node `+0x30` origin used by the ballistic solver from sector-camera
`+0x30`; inserting a node `+0xb0 - +0x30` correction would be wrong for this
scope. This is stronger evidence than comparing two coincident values in a
capture. Constrain the extension to matching nonnull
`target.owner54 == viewObject.owner54 == cockpit.sector54`, sector type 1,
in addition to the existing active applied chase/main-gun guards. A nested or
cross-sector target fails this scope check.

The renderer independently consumes camera `+0x30/+0x40` in `0x004be520`,
constructing the view from the same subtraction and transposed basis. The
context scale multiplies all three camera-space components equally and cancels
in perspective x/z and y/z. The ordinary object renderer's separate use of
node `+0xb0` does not change the native lead solver's coordinate contract.

FOV ordering is also resolved:

1. Pose and the above object camera-space loop finish.
2. `0x004210f0` updates the target overlay, including the native lead marker.
3. `0x00421144..0x00421389` selects base FOV and advances native zoom state.
4. `0x00421398` writes cockpit-scene FOV; `0x004213ad` writes sector-camera
   FOV. `0x004213c2/0x004213d7` update galaxy/dust FOVs.
5. `0x004213e5` writes the **same sector-FOV value** into `cockpit+0x230`.
6. `0x0042169d` is reached after the remaining dust/camera work and before
   the final internal-ship-visibility branch. Its next branch can return, but
   no remaining instruction changes the sector-camera pose or FOV.
7. The registry finishes the cockpit walk; the frame routine subsequently
   renders those camera values.

Consequently the native lead calculation reads the previous FOV during an
active zoom transition. Replacing only its early projection would inherit
that timing mismatch. Reuse the final camera FOV at the late seam instead of
copying or guessing the zoom interpolator.

## Revised minimal projection proposal

The projection-fork sites in the earlier table are no longer needed for this
bounded design. Retaining the native depth gate is intentional: the extension
repositions an otherwise native-admissible main-gun lead marker, preserving
native weapon/forward-depth suppression. It does not expand the set of
ballistic targets or weaken the depth threshold.

Three scoped seams suffice, with projection first at native publication and then
after final FOV if it changed:

| Site | Span / decoded operations | Proposed job |
| --- | --- | --- |
| `0x0042a6fe` | 6-byte near JZ | Start a fresh invocation witness; override only the view-bit rejection for the guarded applied main-gun chase cockpit. Native tracking 2/3 and all subsequent gates remain. |
| `0x0042aaae` | 6-byte `MOV [EBX+0x340],ESI` | Native successful marker publication: copy the three aim-point components from **original game ESP `+0xc0`**, project using the currently effective camera/FOV, rewrite the native node position and substitute ESI/EDI before the original two screen-coordinate stores. EBX is overlay. |
| `0x004213dd` | 6-byte `CMP [EBX+0x230],ESI` | EBX is cockpit. All camera FOV writes have finished; read sector-camera `+0x298` directly, since the cockpit copy is written just after this site. Consume the matching witness and reproject if the effective projection changed. This is before the native laser-notification callbacks. |

The earlier `0x0042169d` seam is geometrically late enough, but
`0x004213dd` is preferable because it precedes synchronous notification callbacks
(see below). It has incoming branches to its start from the conditional dust
FOV paths at `0x004213cd/0x004213d5`. The successful publication site has no
explicit incoming branch in the queried references. The native aim point at
original ESP `+0xc0` is not overwritten between solver return and publication:
main-gun subtraction reads it at `0x0042a7ae`; the temporary basis occupies
`+0x90..+0xbf`, and the intervening projection/icon code leaves `+0xc0` intact.
The screen helper returns with its argument stack removed before publication.
No native call lies between that return and the two coordinate stores.
Neither seam is an approved production patch: instruction-interior, relocation,
CPU-state and rollback checks still apply. No original engine bytes are stored
in this note.

A witness must be per invocation, including the current chase-handler serial,
shared cockpit-lifetime generation, thread, cockpit, ship identity, target
identity, sector, camera and overlay.
Clear/replace it at every applicable gate visit, capture only on a matching
successful native publication, and consume it once at the matching late site. Recheck pointers,
identities, active control, view/connect flags and active native marker before
writing. Monitor visits, skipped updates, failed solvers, a changed target,
loads and stale camera state must never reuse a prior point. A new gate visit
must invalidate a previous point even when a later read fails. No arbitrary
wall-clock freshness limit substitutes for this same-update correlation.

For the intended initial scope, require valid **matching sector and HUD
viewport bounds and matching effective integer viewport extents**, as well as
an ordinary centred marker (`node.flags130 & 0xf000 == 0`). This keeps the
implementation small and avoids introducing a separate monitor/edge-anchor
layout feature. If this common-main-view condition does not hold, retain the
native rejection rather than approximating the mapping.

With `P` the copied aim point, `C` the late sector-camera position, and `B`
its three fixed-point basis rows divided by 65536:

```text
v = (P - C) × transpose(B)
t = tan(pi * sectorCamera.fov298 / 65536)
W,H = positive camera plane300,304 / 65536, else the native global fallback
x_relative_pixels = round((viewport_width  / 2) * v.x / (v.z * t * W))
y_relative_pixels = round((viewport_height / 2) * -v.y / (v.z * t * H))
```

Use doubles/SSE2 for differences and projection; reject invalid or nonfinite
camera data, nonpositive depth, invalid projection denominators and integer
overflow. Bound binary FOV strictly to `0 < fov < 0x8000`, where the required
half-angle tangent is positive and finite; retain the native minimum FOV
`0x106` for the initial scope. Require positive plane dimensions, positive
screen/viewport extents within the native screen dimensions, normalized bounds
within `[0,65536]`, and the same existing orthonormality guard used by chase. Do not invert the basis with a different convention: both native
view construction and the object `+0xf0` producer use its transpose. Existing
chase basis validation bounds its orthonormal error.

Successful output writes marker node `+0x30/+0x34` and matching cockpit
`+0x6ec/+0x6f0`, retaining native z=0, icon, color, flash and rotation. The
`0x200` screen-node branch of `0x004bdee0` **reads base node `+0x30/+0x34`
directly**, replaces view with identity, and derives an alternate orthographic
projection from the HUD viewport. There is no render-ready position or dirty
transform buffer to update. Its established anchor convention includes
`x-0.25` and `-y-0.25` before clip conversion; retain this native quarter-pixel
behavior and ordinary integer pixel rounding rather than treating the glyph
origin as an independently calibrated geometric centre.

If a point admitted by the extension later fails revalidation/projection,
hide it using the native hide semantics and set both stored aim coordinates
to -1. Merely clearing the overlay active word is insufficient: native
`0x00426280` also detaches the linked node through `0x00489e90`. A qualified
wrapper for that small hide helper, or a proven equivalent, is needed for the
failure path. Do not leave the admitted but incorrectly projected native icon
visible after an extension failure.

No additional solver call, per-draw work, allocation or per-frame log is
necessary. The only geometric work is one three-row camera transform and one
projection at successful publication, with a second projection only if the
final FOV/viewport differs. The point-to-camera vector can be retained when the
validated pose is unchanged. Native context ownership
and marker visibility must be revalidated with the usual writable-memory
checks before any late mutation.

## Consumers, submission order and bounded lifetime

The native cursor-near-lead helper `0x004257f0` has one direct caller:
`0x00445aa8`, in fire control `0x00445170`. The fire function itself has the
four previously documented engine-command dispatcher callers. A whole-program
instruction-displacement search finds no other reads of cockpit `+0x6ec/+0x6f0`;
overlay-relative `+0x340/+0x344` writes are constructor/reset and the two
native lead publication/hide paths. Other hits at the same displacement are
unrelated object or stack fields. This is static direct-reference evidence,
not proof against every computed pointer or mod-added callback.

The normal main-loop sequence is explicit: game-object/script update at
`0x00403f2a` calls `0x00416750`, cockpit registry update at `0x00403f2f` calls
`0x0041cde0`, then render at `0x00403f34` calls `0x00471f50`. The registry calls
the whole cockpit updater at `0x0041ce3e`. The marker update creates/attaches
scene nodes; its native screen helper writes base position without submitting
that marker to D3D. Actual scene submission uses the screen-node branch of
`0x004bdee0` during the later frame render. Thus `0x004213dd` precedes normal
same-frame marker submission. The next ordinary script/fire iteration sees the
finalized coordinates from the preceding displayed cockpit update.

However the updater can also enter script bytecode synchronously:
`0x004213ed` calls `0x00422fc0`, which can issue `NotifyLaserLow`; its helper
`0x00424e00` can issue `NotifyIntoLaserRange` / `NotifyOutofLaserRange`.
`0x0049f4c0` reaches `0x0049f430`, which calls the bytecode interpreter
`0x004a26a0` before returning. Treating those notifications as a deferred queue
would be incorrect. Finalizing at `0x004213dd` ensures these possible control
consumers see the correct final camera projection, and removes the need to
retain an aim-point witness across those callbacks.

The earlier successful-publication hook ensures the visible node and the
stored pair already agree with the camera/FOV effective at the original native
publication boundary. The final-FOV seam then updates both together before
notification callbacks. This distinguishes native in-update control semantics
from the later visual submission without inventing a universal fire ordering.
Native resource/icon calls before successful publication can therefore retain
the previously valid native publication until the new one is committed, as
in the original game; no provisional wrong screen pair is published by the
extension.

A bounded depth-12 direct-call survey of the post-solver overlay callees and
post-overlay HUD callees found script-callback paths through resource failure
handling. Targeted follow-up resolves those paths: `0x00401dd0` performs full
game shutdown and terminates via `_exit(0)`; it does not pump gameplay and return
to the overlay. Such a path cannot resume with a reclaimed object in this
invocation. The local call-path output remains
`/tmp/chase-lead-callpaths.txt`; consumer/failure follow-up is
`/tmp/chase-lead-consumers*.txt` and `/tmp/chase-lead-lifetime.txt`. Indirect
calls remain a static-analysis limit, so runtime guards are still mandatory.

The implementation must own scalar point data only, never a retained game
object or renderer reference. At gate and publication validate the ship and
target pointer **and object `+8` identity**, current owner/sector equality,
active cockpit registry resolution, camera identity and applied-handler serial.
At final FOV consume only that exact same cockpit-update/thread witness and
revalidate those fields plus the native active marker's node/scene link.
Require its node context `+0x1c` to equal the cockpit scene `+4`, a valid screen
node flag, and fresh writable-range checks. A missing/changed object, target,
camera, scope or serial invalidates the witness. Never attempt to hide or write
an unrelated/reused cockpit on an identity failure; native baseline ownership
must still validate before touching its marker. The same-update scalar design
avoids promising that a handle is a process-lifetime generation identifier.

The [view-transition study](chase-view-transition.md) identifies cockpit
constructor `0x0041f8d0` (script allocation or load `0x0041f720`) and destructor
`0x0041ffc0` (direct script disposal or deleting destructor `0x0041ccf0`). If the
shared camera/transition integration already observes those paths, use its
monotonic lifetime generation and current-update sequence to clear pending
reticle state on every pose and constructor/destructor event; do not duplicate
those hook sites for this marker. Late publication requires the same lifetime
generation and no newer cockpit update. This strengthens cockpit reuse
handling without pretending that object pointers/handles alone prove lifetime.
A target-object destruction/reuse guarantee outside the documented synchronous
game flow is not established by that cockpit generation; keep target identity,
owner and native visibility checks and retain that explicit limit.

Do not re-show a marker the native code hid. Native failure paths before
publication retain their own hide behavior; a missing current publication must
never reuse an earlier point. For an extension failure at publication, hide
only the still-validated owning marker and substitute ESI=EDI=-1 so the original
stores publish invalid coordinates. For failure at final FOV, detach that same
validated owning marker and set the stored pair to -1; if its ownership no
longer validates, discard the witness without dereferencing the old pointer.
These requirements need focused state/lifetime tests, including a skipped
publication, failed solver, target switch, nested/mismatched update serial,
inactive native marker, scene change, scope refusal and late read/write failure.

## Implementability verdict

The coordinate domain, screen-node consumption, surviving native solver local,
FOV ordering and normal update-before-render sequence are sufficiently resolved
for the narrow three-seam implementation above. No additional coordinate/FOV
research is a blocker for that scope. Use the shared lifetime/update generation
when available, immediate native-publication correction, final-FOV revalidation,
and native hide-on-owned-failure behavior as the implementation policy. Do not
claim a universal absence of reentrancy or prove object lifetime from address
equality alone.

Remaining work is implementation and focused qualification of those policies:
site/ABI/CPU-state/rollback tests, current-update and lifetime invalidation,
late failure/visibility recovery, numerical projection checks, and one combined
user acceptance run. Unsupported owner/viewport/marker contexts retain native
behavior. Unexpected indirect reentry or unresolved identity must fail closed
and appear in bounded diagnostic counts, rather than silently broadening scope.

## Consolidated diagnostics and remaining acceptance

Static proof supports the above narrow implementation; runtime values and the
selected hook machinery still require verification. If the implementation is
deferred or a scope guard refuses, consolidate these bounded samples with the
planned view-mode trace, not another standalone load cycle:

- At lead gate: serial/thread/cockpit, view/connect/applied state, tracking,
  target-bodies flag, target/view/sector owner identities, aim index, and the
  native HUD mask. Count each admission/refusal reason.
- At successful native publication: one copied aim point, target node `+0x30/+0xf0`, current
  sector camera position/basis, early `+0x230` and both cameras' FOV fields.
  Aggregate maximum difference between recomputed target camera-space position
  and native `+0xf0`; per-product native integer rounding allows a small
  bounded numerical difference, not a domain-sized difference.
- At late finalization: same serial and identities, native marker active/node/
  flags, native pixel pair, final camera FOV, effective view-plane dimensions,
  both normalized viewports and their `D3DVIEWPORT9` rectangles, final proposed
  pair, camera-space depth and final verdict. Retain only a few representative
  samples plus counts/extrema on the existing cadence.

The relevant acceptance is a selected moving target in settled chase, finite
stationary targets at more than one range, zoom transitions, target loss and
view/sector changes, with first-person and non-chase HUD behavior unchanged.
Native Windows behavior and actual game marker alignment remain unverified.
No gameplay run is requested by this note alone.
