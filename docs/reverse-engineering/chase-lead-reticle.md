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

Remaining proof before implementation/acceptance: confirm point and camera
positions share the same native base/render domain (the solver starts from
target node `+0x30`, while the chase anchor has a native domain branch); resolve
FOV/zoom update timing and viewport centre mapping; qualify chosen trampoline
sites and per-invocation output storage; verify a selected moving target shows
the same native lead style, stationary finite targets project consistently,
first-person/other external views and disabled HUD are unchanged. No gameplay
run is requested by this note alone; consolidate any necessary diagnostics
with the next already planned user run.
