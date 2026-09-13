# Chase view: central crosshair and target-distance instruments

Read-only follow-up to run 20A, 2026-09-13. The predictive lead marker is now
reported visible; the user separately reports a first-person crosshair/distance
display absent in chase. The strongest static match is the **central cockpit
instrument group**, whose distance texture remains updated externally while
its display nodes are hidden. The user subsequently confirmed that the missing
graphic stays near screen centre, consistent with this group rather than the
object-following brackets. Exact visual contents and the correction still need
gameplay verification.

Installed X3AP.exe preferred base `0x00400000`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Ghidra 12.1.3, existing `/tmp/x3-ghidra-research X3Render`, read-only/no-analysis;
`tools/analysis/X3CameraState.java`. Raw evidence stays local in
`/tmp/chase-target1.txt` through `chase-target4.txt`, `chase-target6.txt`, and
the earlier `/tmp/chase-lead3.txt`. No game, Wine, build or install was run.

## Separate ownership and view gate

Let `O = cockpit + 0x3ac`. Native overlay updater `0x0042a2d0` keeps `O` in
EBX and its cockpit backpointer at `O+0x324`. The separate
[predictive marker](chase-lead-reticle.md) uses `O+0x3c` and publishes its
screen coordinates at `0x0042aaae`. Immediately afterward, another test reads
view mode `cockpit+0x150`:

| Site | Length | Decoded operation / effect |
| --- | ---: | --- |
| `0x0042aad9` | 7 | Test view-mode low bit. |
| `0x0042aae0` | 6 | Conditional jump on zero to `0x0042ae82`, hiding central instruments. |
| `0x0042aae6` | — | Admitted native crosshair/style and text-node update path. |
| `0x0042ae80` | 2 | Skip hide block, continuing at `0x0042aed6`. |

Chase mode 258 (`0x102`) fails this bit test. This gate is independent of the
lead solver, its successful publication, tracking mode 2/3, and cursor-fire
admission. A failed predictive solution still reaches this instrument gate.

The updater's earlier common guards remain applicable: `O+0x320` target bodies
enabled; nonnull view object `cockpit+0x10`, type 7; nonnull object owner
`viewObject+0x54`, type 1. Failure hides the complete overlay. The routine serves
multiple cockpits and does not impose our active-chase ownership restriction.

## What the admitted group displays

Entries contain an active flag, node pointer at `+4`, and resource/icon state.
They attach to `cockpit+4`, the cockpit HUD scene. Coordinates below are native
screen-node coordinates relative to the HUD centre, not sector-world positions.

| Entry | Native screen position | Content / producer |
| --- | --- | --- |
| `O+0` | `(0,9,0)` | Central crosshair, helper `0x00426230` called at `0x0042ab7e`. Native icon groups `0x182..0x184`, `0x185..0x187`, or `0x1f3..0x1f5` depend on ship/gun/monitor state, cursor-steering global `0x00607c64`, and native target/style state. |
| `O+0x208` | `(-70,-10,0)` | Target-speed panel: texture 15, row `y=0..16`. |
| `O+0x21c` | `(70,-10,0)` | Own commanded-speed panel: texture 15, row `y=16..32`. |
| `O+0x230` | `(0,40,0)` | Tracked-target distance panel: texture 15, row `y=32..48`. |
| `O+0x294` | `(1,-25,0)` if admitted | Additional native gun/monitor status icon, independently hidden by its existing conditions. |

The three text panels use resource `registry+0x48`, native attachment helper
`0x004260c0`, and texture-coordinate matrices constructed by `0x004f53c0`.
Calls `0x00487b10` at `0x0042acec`, `0x0042adb7`, and `0x0042ae7b` bind the
respective 64-by-16 slices of the 64-by-64 texture. This helper retains a node
material/texture override and updates its matrix; it is not a new text-format
call each time. The nodes receive screen flag `node+0x130 |= 0x200`, also set by
crosshair helper `0x00426230`. Their placement does not consume target position,
sector-camera basis/position, or the lead projection coordinates.

The hidden path calls `0x00426280` for **eight** entries: `O+0`, `O+0x208`,
`O+0x21c`, `O+0x230`, `O+0x294`, `O+0x118`, `O+0x140`, and `O+0x12c`.
The last three have no direct field-offset producers in the targeted global
instruction sweep; that does not prove absence of indexed producers. Admitting
the complete group therefore also skips their native external-view cleanup.
A correction must account for this side effect instead of describing the
branch as a distance-only gate.

## Distance data, formatting, invalidation and update order

`0x00422fc0` calls `0x00424e00` at `0x00423007` when the view object exists,
`cockpit+0x234` is nonzero (`INS_CockpitEnable2DDraw`, command `0x31`), and
`cockpit+0x2a0` is nonzero (`INS_CockpitEnableDisplayBody`, command `0x34`).
The producer further requires an active cockpit scene (`scene+0x18 & 1`) and
available texture 15. There is no external-view rejection around this call.

Its target is exactly `cockpit+0x1e0`. In view mode 1 it obtains the native
view origin through `0x00420400`; otherwise, at `0x00424e7b..0x00424e87`,
distance helper `0x0042f850` receives target render-node `+0x30` and view-object
render-node `+0x30`. Thus chase retains native **ship-to-target distance**,
not camera-to-target distance. The display is an instrument reading and does
not need a camera-parallax correction.

The producer preserves native unit selection (`registry+0x18`), integer and
fractional distance formats, localized unit strings (`0x004ab200`), font
(`registry+0x14`), and relation-dependent color (`0x00450890`). It clears the
distance row at `0x004251c4`, measures the formatted string with `0x0048b990`,
and renders centred into texture 15 at `0x00425203` through `0x0048b4b0`.
With no tracked target, calls at `0x004252ad` and `0x004252be` clear target-speed
and distance rows. The own-speed row is refreshed separately. Target speed is
suppressed for non-type-7 targets and the existing target flag `+0x44 & 0x10`.

This worker also performs native weapon-range testing (`0x00444ce0`) and can
send synchronous into/out-of-range notifications through `0x0049f4c0`.
Do not invoke it a second time merely to restore the hidden panel.

Within [cockpit update](external-camera.md), overlay-node placement is at
`0x004210f0`, camera/FOV convergence follows, and `0x00422fc0` runs at
`0x004213ed`. The normal main loop renders afterward. Therefore the existing
same-frame texture refresh precedes normal HUD submission even though node
placement comes first. Retaining native nodes and texture ownership needs no
pending target-point cache or late projection hook. When display/2D/scene
guards reject the producer, it does not promise to refresh stale texture data;
a new chase admission should require those guards rather than exposing a
disabled display's previous contents.

## Distinction from world-object brackets

`O+0x348` owns a separate manager created by `0x00427b00`, with named target
overlay scene, its own camera, and pooled `0x110`-byte object records. Caller
`0x0042a1d0` runs it for monitor number zero when `(viewMode & 3) != 0` and
the native sector flag permits it. **Mode 258 already passes.**

Updater `0x00427d50` selects candidates from cockpit `+0x260..+0x264`, prioritizes
the tracked/current object, sorts them, and renders object brackets, offscreen
arrows and status bars. Placement uses object-node camera-space fields
`+0xf0/+0xf4/+0xf8`, radius, HUD planes and cockpit projection scale `+0x230`.
There is no distance-string formatting in this worker. If the user means an
object-following bracket rather than the central instrument, changing
`0x0042aae0` alone does not establish that separate symptom is fixed. Its native
precomputed-coordinate/FOV timing would require a distinct investigation.

## Bounded correction proposal and remaining proof

The smallest candidate is a chase-only admission at `0x0042aae0`, preserving
the relocated native conditional branch outside the current applied main
chase context. At this site EAX is the cockpit, EBX is `O`; do not reuse the
lead-gate assumption that ESI is the cockpit. Qualify the current pose/lifetime
generation and exact cockpit/overlay ownership, existing active-control/main
view restrictions, and the display-producer guards above. Do not require a
successful lead solution or even a tracked target: the native centre crosshair
and own-speed instrument exist without one, while native code clears distance.

Preserve all native style/resource selection, texture refresh, target-null
clearing, and common enable/owner guards. For the otherwise-skipped three
cleanup entries, either establish their indexed lifetime fully or retain their
native hide calls for the chase extension. The existing helper ABI is documented
and checked by `verification/probe/verify_chase_lead_sites.py`.

This restores native **fixed-screen instruments**. It does not make the central
crosshair a gun-forward/world-target projection for the pitched, displaced
chase camera; the corrected predictive lead and mouse aiming remain separate.
No coordinate rewrite is indicated by this group's native dataflow.

The installed PE and independent full-function objdump decode agree that the
candidate is one six-byte conditional branch, target `0x0042ae82`, relative
displacement field offset 2, fallthrough `0x0042aae6`. Complete containing
function bounds are `[0x0042a2d0,0x0042c1de)`. No decoded direct control transfer
in that function enters the displaced interior; Ghidra also reports no
references to its five interior addresses. A production change still needs its
own source/site-parity probe and existing CPU-state/rollback checks. Visual
identity of the user's missing graphic and presentation with chase pitch remain
gameplay acceptance items. No production or installed files changed here.
