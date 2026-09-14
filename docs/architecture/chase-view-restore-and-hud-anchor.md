# Chase view: automatic restore after a sector change, and the central HUD anchor

Design note, 2026-09-15, for two user requests on the installed chase camera
(`--camera chase`, 13° pitch-down, `offset_y` 0.45, distance 0.90). Nothing here
is implemented. Both items are behaviour changes and therefore **default off**
until the user accepts them in a run. Evidence base: the chase notes
([camera](chase-camera.md), [elevated geometry](elevated-chase-camera.md),
[lead marker](chase-lead-marker.md), [central HUD](chase-central-hud.md)), the
reverse-engineering studies ([external camera](../reverse-engineering/external-camera.md),
[view transition](../reverse-engineering/chase-view-transition.md),
[target indicator](../reverse-engineering/chase-target-indicator.md),
[lead reticle](../reverse-engineering/chase-lead-reticle.md),
[mouse fire](../reverse-engineering/chase-mouse-fire.md)), the source under
`src/proxy/chase_*`, and the run-22 (snapshot run51) telemetry log, which is the
first run with the transition diagnostics installed.

**Ratified 2026-09-15 (orchestrator):** both items as recommended, default-off (`--chase-view-restore`) and default `centre` (`--chase-hud-anchor forward|centre`). Before implementing item 1, one telemetry run with a gate jump and a jumpdrive (existing diagnostics) must settle the reset writer and its ordering; item 2 can be implemented directly.

## Item 1: restore the external view after a gate jump or jumpdrive

### What the engine does (established) and what it does not (unknown)

- The view mode is cockpit `+0x150`. Three writers exist in the EXE:
  constructor init to 0 (`0x0041fbe8`), save deserialization (`0x00419e06`) and
  the script command `INS_CockpitSetViewMode` (dispatcher `0x0042d340` case
  `0x30`, store at `0x0042e742`, EAX = cockpit, ECX = mode). The direct-displacement
  scan found **no view-mode store in the per-frame update `0x004205e0` nor in the
  sector-space setter path (case `0xb` → `0x00420360`)**. The reset on sector
  change is therefore a KC script decision executed through `0x0042e742`, not
  engine code; the script bytecode (`x3story.obj`) has no usable names, so the
  "engine code that resets the view" is a script method identified only by its
  runtime CODE offset (`task+0x1c`, logged as `next_pc`).
- Run 22 (run51 log) shows the shape of every view change through that writer.
  Each user view selection is one connect-mode request pair (`0x00422cd0`,
  requested 2 then 0) followed by **two** identical mode stores from two script
  PCs (`next_pc=0x000f0c63` then `0x000f0794`; the second PC appears in every
  mode write, so it is a common apply helper). The observed modes are 1
  (internal), 258 (`0x102`, the ordinary rear chase view) and 516 (`0x204`,
  target view, connect 3). Returning from 516 goes to 1, never to 258.
  `+0x1c0` was 0 before and after every 1↔258 change. The save load at the
  start of the run wrote mode 1 through `0x00419e06` (kind 7), so a load does
  reach the internal view through the deserializer, as the transition study
  predicted.
- Run 22 contains **no sector change** (`+0x1fc` = `0x19663cd0` in all 38
  update snapshots; camera snaps carry only reason 1). Run 18 saw the reset but
  had no writer instrumentation. So the reset's writer, its PC, its timing
  relative to the sector publication (`+0x1fc`, written at `0x004210d6` after the
  hook site) and whether it touches the view geometry are **unknown**. The
  installed transition diagnostics (`--telemetry`) answer all four in one run
  with a gate jump and a jumpdrive; no new disassembly is needed for that.
- Geometry caveat: the updater snapshots show cockpit `+0x130` = `(0,0,0)`
  in mode 258 as well as in mode 1 (`snapshot_valid=255`, so the read
  succeeded). The rear view's boom is therefore not carried by `+0x130` in this
  game state; the `+0x160` view-camera offset (case `0x36`, applied at
  `0x00420c3d..0x00420e03`) is the candidate, together with the target angles
  `+0xa8..+0xb0`. The restore must not assume which fields the reset script
  changes; it must measure them (below).

### Recommended option: one-use restore ticket, written from the existing hook

Write `+0x150` back to the armed external mode from the chase handler at
`0x00420e06` on the first frame that satisfies the completion predicate. This is
the same store the engine's own command performs (the study proved the command's
only state change is that store); there is no engine "set view" routine to call,
because the dispatcher case is inline and reachable only through the VM's
native-call op with a marshalled task and a five-byte-tagged argument block.
Fabricating that task is an invented ABI and is rejected (alternative A below).

Policy (portable, in a header beside `chase_camera_math.h`, fed by values the
handler already reads every frame from the 0x200-byte cockpit block plus the
transition module's `current_update(cockpit).generation`):

1. **Arm** on every applied frame (verdict 0, mode 258, connect 0, ref view
   object == ref object): remember mode, `+0x1c0`, `+0x130`, `+0x160`,
   `+0xa8..+0xb0` (three ints each), ref object, sector `+0x1fc`, lifetime
   generation. Re-arming every applied frame keeps the snapshot current with no
   allocation (eleven dwords copied).
2. **Open** the ticket when the observed mode changes 258 → 1 while armed.
   Count mode transitions since arming; the ticket requires exactly one.
3. **Consume** on the first later frame where all hold: mode still 1, connect 0,
   `+0x1a0 & 4` clear, same ref object, same lifetime generation, ref view ==
   ref (still the active control cockpit), `+0x54` sector space non-null,
   `+0x1fc` non-null and **different from the armed sector**, and at least two
   cockpit updates since the last observed mode write (with telemetry the
   writer events give this exactly; without it the mode value's frame-to-frame
   stability stands in). The write is `+0x150 = armed mode`, after the same
   `writable(cockpit, cockpit_block)` check the pose writes use. The next
   frame's update `0x004218b0` regenerates `+0xf0` from the current angles, the
   follow branch rebuilds the external pose, and the handler applies with a
   snap (the internal frames were refused, so the springs re-seat with reason
   1 and the TAA route sees exactly one cut; the teleport/sector coalescing of
   review A4 is not involved because those frames never reached the pipeline).
4. **Cancel** (ticket dropped, reason logged): a second mode transition since
   arming (the player pressed a view key during the transit, or the script
   re-asserted mode 1 after our write — we never fight the script), ref object
   change (ship change, eject, death), lifetime generation change (cockpit
   recreated, including the save-load chain `0x0041f720 → 0x0041f8d0`), a
   deserialization write (kind 7, telemetry only), any non-zero connect mode
   (docking and cinematic sequences use 3/4/5/6/8/9), `+0x1a0 & 4`, more than
   1200 cockpit updates since the ticket opened (counted in updates, not wall
   time, so a 45 s load cannot expire it; the bound is a cancel, not an
   authorization), or the write refused.
5. Fail-closed by construction: no ticket exists unless the chase pose was
   applied in mode 258 immediately before the reset, so a player who was in the
   cockpit view, docked, in a target view or in a menu before the jump gets
   nothing. Menus: whether the cockpit update runs inside menus is still
   unobserved; a restore during a menu is harmless (it is the same store the
   next key press would make) and the ticket's update-count bound still cancels.

Geometry: the first iteration writes **only `+0x150`** and logs, at the restore
frame, which of the armed geometry fields (`+0x130`, `+0x160`, `+0xa8..`) differ
from the current values (`geometry_delta=` bits). If the diagnostic run shows
the reset script rewrites any of them, the second iteration restores those from
the armed snapshot through the same validated fields (layouts proven by
dispatcher cases `0x2f`, `0x36`, `0x2b`). Restoring mode alone when the
geometry was untouched reproduces exactly the state the key path leaves, because
the key path's only other observed effect in run 22 was the connect 2→0 pair
which ends at the value we require anyway.

Residual behaviour the user must know: the KC script keeps its own view
variable and is not told about the restore. After a restore, the next press of
the internal-view key should still select mode 1 (absolute select); if the
script instead cycles, the first press may be a no-op. The acceptance run checks
this. A deliberate cockpit-view selection followed within ~20 s by a jump is
indistinguishable from the reset and would be restored; the diagnostic's
`next_pc` for the reset write, if it differs from the key path's
`0x000f0c63`, allows a stricter origin gate later.

Cost on the hot path: per handler invocation about a dozen integer compares and
an eleven-dword copy from the already-read cockpit block; on the restore frame
one `VirtualQuery` (already performed for the pose write) and one 4-byte store.
No allocation, no per-frame log. Native Windows: the store targets the same
non-relocatable image through `VirtualQuery`/`VirtualProtect` only; behaviour is
identical to CrossOver, but unverified there like the rest of the hook.

Option: `X3M_CHASE_VIEW_RESTORE=1` / `--chase-view-restore` (requires
`--camera chase`), default off. Install line adds `view_restore=<0|1>`.
Log line, once per ticket outcome (bounded, not per frame):
`chase_view_restore result=<written|cancelled|refused> reason=<sector_change>
cancel=<none|second_mode_change|ref_change|lifetime|deserialized|connect|flags|timeout|write>
mode_armed=258 mode_seen=1 sector_armed=0x… sector_now=0x… wait_updates=…
geometry_delta=0x… frame=…`.

### Verification

- Host fixture: the policy is a pure state machine; extend
  `verification/probe/chase_camera_host.cpp` with a `V` command (mode, connect,
  flags, ref, sector, generation, writer-kind event) and add cases to
  `test_chase_camera.py`: applied-258 → mode 1 → sector change → `written`
  after two stable updates; the negatives above each yield the expected
  `cancel=`; a ticket never opens without a prior applied frame; the restored
  value equals the armed value, not a constant. No new hook site, so the CPU
  fixture and site probes are unchanged; `check_no_x87.py` on the DLL as usual.
- Diagnostic prerequisite, one user run with `--telemetry`, no new build needed
  if the transition diagnostics are still installed: one gate jump and one
  jumpdrive, then read `chase_transition_event kind=6/7` around the sector
  change (`caller`, `requested`, `next_pc`, update serial), the kind-3
  snapshots' `+0x130`/`+0x1c0` before and after, and whether a constructor
  event (kind 0/1) occurs. That settles the writer, the order relative to
  `+0x1fc`, and the geometry question; it decides whether iteration two is
  needed before the option is offered.
- Acceptance, one run with `--chase-view-restore`: after each jump the log has
  `chase_view_restore result=written reason=sector_change`, the next
  `chase_camera` window shows `mode=258 verdict=0` with one snap, and the user
  reports the chase view back without a key press, plus the view keys behaving
  normally afterwards.

### Alternatives considered

- **A. Re-issue the engine's set-view path.** Rejected above: there is no
  callable routine, only the VM-dispatched command with a marshalled task.
  Calling `0x00422cd0` (ESI = cockpit, EAX = connect mode) is a real ABI but
  unnecessary while connect is already 0, and it can rewrite angles.
- **B. Veto the reset at the writer** (rewrite ECX in the `0x0042e742` stub so
  mode 1 is never stored). Zero internal frames, but it acts before the
  transition is complete, cannot distinguish the reset from a deliberate
  key press at that instant, and the mode-writer site is part of the
  telemetry-only diagnostic group. Loses to the post-transition ticket.
- **C. Synthesize the view key** (`SendInput` or the KC `Input` method through
  `0x0049f570`). Forges player input, depends on the key binding and the script's
  cycle state; rejected.

## Item 2: where the central crosshair belongs

### The numbers

Installed geometry, from `first_applied` lines: `fov298=0x4000`,
`half_vfov_tan=0.7500` (default plane height `0xc000`), mode 258, boom_local z
≈ −17,404 engine units, so the scaled boom is L ≈ 15,663 units (≈ 31 m if the
inferred 500 units/m holds; that conversion is not verified and only the
finite-distance rows depend on it). At 1280×768 the vertical scale is
384 px / 0.75 = 512 px per unit of tan.

The ship's forward axis in the settled camera frame is `(0, sin 13°, cos 13°)`;
the ship anchor sits at `(0, −0.3375, 1)·L/√1.114` (offset_y × tan = 0.3375).
Projecting `anchor + d·forward`:

| Point on the forward ray | Above screen centre |
| --- | ---: |
| ship anchor (d = 0) | −173 px (72.5 % height, by design) |
| d = L (≈ 31 m) | −25 px |
| 100 m | +50 px |
| 500 m | +101 px |
| 1 km | +110 px |
| 2 km (typical lead-marker range) | +114 px |
| infinity: tan 13° / 0.75 = 0.3078 of half height | **+118 px** |

With spring lag the actual written basis differs from the target by up to 8°:
the infinity point then lies between +45 px (camera 8° further up) and +197 px
(8° further down), returning within ~1 s. In the legacy geometry
(`--chase-pitch-down-deg 0`) the forward vanishing point coincides with the ship
anchor at −173 px. So the screen centre is the aim point in no framing except
the trivial one (offset 0, pitch 0); the native `(0, 9)` crosshair in chase is a
**false cue**, not a redundant one: a target centred on it is 13° (≈ 460 m at
2 km) below where boresight bolts go.

### What the existing elements already do

- The predictive lead marker projects the native solver's aim-equivalent point
  for the *tracked* target through the sector camera (`chase_lead`, seams
  `0x0042aaae` and `0x004213dd`). It is the true intercept for that target and
  only exists with tracking 2/3 and a successful solve. It does not tell the
  pilot where the ship points.
- The central group (`O+0` crosshair at `(0,9)`, panels at `(−70,−10)`,
  `(70,−10)`, `(0,40)`, status icon `(1,−25)`) is placed by native constants in
  cockpit-scene screen-node coordinates (node `+0x30/+0x34`, flag `0x200`,
  +y down) after the gate at `0x0042aae0`; nothing in that path reads the
  camera. The distance text is ship-to-target and needs no correction.
- Cursor fire unprojects the cursor through the sector camera's FOV, plane and
  viewport (`0x00489780`), multiplies by `+0xf0` and applies the cone; the
  proxy keeps `+0xf0 = B_cam × B_shipᵀ`, so a cursor at pixel p yields the
  camera-space direction of p. A cursor on the forward-ray pixel therefore
  gives exactly `(0,0,1)` in the ship frame: boresight. Bolts start at the
  gun-group origin, parallel to that direction, and converge on screen to the
  same vanishing point. The mouse-steering dead zone (`0x0040e8c0`) is
  measured from the **screen centre**, not from the reticle; that stays as is.

### Recommendation: anchor the group on the projected forward ray at infinity

Anchor the crosshair at the projection of the ship's forward **direction**
through the *written* sector camera basis and the *final* FOV, i.e. the
vanishing point of the guns. Use the direction, not a finite point: between
500 m and infinity the point moves 17 px, less than the reticle glyph, and the
lead marker already owns target-specific placement; tying the crosshair to the
tracked target would make it jump on target changes and vanish without one.
Keep the ship-silhouette anchor rejected: it shows where the ship is, not where
the bolts go. Move the whole admitted group (crosshair, three panels, status
icon) by the same integer offset so the native layout and the distance readout
under the reticle are preserved; crosshair-only is the fallback if the user
finds the moving text distracting.

Mechanics: in the existing final-FOV handler at `0x004213dd` (index 2, EBX =
cockpit, all FOV writes done, before the notification callbacks), when the
central gate admitted the group in this update and the chase pose was written:
`v = f_ship × B_camᵀ` (no translation), refuse if `v.z ≤ 0`, then the lead
module's pixel rule `x = trunc((w/2)·v.x/(v.z·t·W))`,
`y = trunc(−(h/2)·v.y/(v.z·t·H))` with the same `Projection` validation, and
write node `+0x30/+0x34` of each active group entry as native position plus
`(x, y)`, with the lead marker's ownership checks (entry active, node non-null,
node scene link == cockpit scene, flag `0x200`, writable). A refused projection
leaves the native centre placement. Because the base is the written camera
basis, the reticle follows the lag exactly as the scene does.

Cost: one 3×3 transform, one tangent (already computed for the lead's late
projection when it ran; cache per update), two divides, up to five 8-byte
node writes plus their validation reads. No allocation, no solver call, no new
hook site (the final-FOV seam and central gate already exist). Native Windows:
documented memory API only, identical behaviour, unverified.

Option: `X3M_CHASE_HUD_ANCHOR=forward|centre` / `--chase-hud-anchor`, default
`centre` (native placement, no writes) until accepted. Install line adds
`hud_anchor=<centre|forward>`; the existing window report gains
`hud_anchor applied=… refused=… last_px=x,y`.

### Verification

- Host fixture (`test_chase_lead.py`, same synthetic memory as the lead
  tests): with `fov=0x4000`, plane `0x10000/0xc000`, screen 1280×768, full
  viewport and `B_cam = pitch_down(13°)`, the anchor is `(0, −118)`; it equals
  `project()` of a point 10⁷ units along forward within 1 px; with an extra
  ±8° pitch the anchor is `(0, −44)` / `(0, −196)`; with pitch 0 legacy
  geometry it lands on the ship anchor row (`+172`); a forward vector with
  `v.z ≤ 0` refuses and leaves the native `(0, 9)`; round trip with the
  documented unprojection model returns the forward direction within 1 px.
- Acceptance, one run with `--chase-hud-anchor forward`: ship settled
  (`lag_deg` < 0.5), a stationary distant target (≥ 1 km) under the reticle,
  fire without a tracked lead; screenshot must show the bolt streams passing
  through the reticle. Cursor fire with the cursor on the reticle must fire
  straight. The user also reports whether the moving panels are acceptable and
  whether mouse steering (if used) feels wrong with the reticle above the
  neutral centre.

### Alternatives considered

- **Leave at centre and re-aim the camera** so forward projects through the
  centre: requires offset 0 and pitch 0 (the ship would sit on the aim point),
  contradicting the accepted 13° top-down framing. Loses.
- **Anchor at the lead-marker distance**: duplicates the lead marker, depends on
  a tracked target and its solver, and moves ≤ 17 px from the infinity point at
  combat ranges. Loses.

## Open

- The reset writer, its PC, its timing relative to `+0x1fc`, and whether it
  changes `+0x160`/`+0xa8` (Item 1) come from the next telemetry run with a
  gate jump; no EXE disassembly settles them because the decision is in KC
  bytecode.
- Whether the cockpit update runs inside menus (affects only the ticket's
  timing, not its safety).
- Engine units per metre (affects only the finite-distance rows of the table).
- Mouse steering's centre-based dead zone versus a reticle 118 px above it; a
  separate change if the user steers with the mouse.
