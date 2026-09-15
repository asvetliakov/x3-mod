# Chase reticle placement: survey of third-person flight games and the option for this mod

**Ratified 2026-09-15 (orchestrator):** no code change. The next user run tries
`--chase-pitch-down-deg 0.5 --chase-offset-y 0.50` with the default `centre`
anchor as the first row, then the 5°/0.50/`forward` compromise if the top view
is missed; `--chase-lag-clamp-deg 4` only with `forward`.
**2026-09-16:** run 26 accepted that row and the user made it the default:
pitch-down 0.5° and `offset_y` 0.50 are now the compiled and launcher defaults
(`src/proxy/chase_camera_math.h`, `tools/manage.py`), with `centre` unchanged. Other-title claims are
recalled, not verified; the recommendation rests on measured mod geometry.

Design note, 2026-09-15. Question: the installed chase camera (13° pitch-down,
`offset_y` 0.45, distance 0.90, lag clamps 8°/0.10) puts the ship's forward
vanishing point 118 px above screen centre (768-row viewport). `--chase-hud-anchor
centre` (default, user's preference after run 25) leaves the native crosshair at
centre, which is a false aim cue; `forward` moves the group onto the true vanishing
point, which the user confirms is aligned but finds clunky in the upper-middle of
the screen. How do comparable games place an external camera and still give a
usable reticle, and what applies here? Evidence: [chase-camera.md](chase-camera.md),
[elevated-chase-camera.md](elevated-chase-camera.md),
[chase-view-restore-and-hud-anchor.md](chase-view-restore-and-hud-anchor.md) item 2,
[chase-central-hud.md](chase-central-hud.md), [chase-lead-marker.md](chase-lead-marker.md),
`src/proxy/chase_camera_math.h` (elevated construction), `tools/manage.py`, and the
run60 log. No code is changed by this note.

## The one formula that governs the problem

With the camera's optical axis pitched down by `p` relative to the ship's forward
axis, the forward direction projects `tan(p) / tan(half vfov)` half-heights above
centre, independent of distance, offset and lag. Measured in run60:
`half_vfov_tan=0.7500`, 768 rows, so 512 px per unit of tangent and
`anchor_last_px=0,-118` when settled (`chase_central_hud_window`, ten windows);
under lag the same field ranged x ±73 px, y −104..−155 px, and `lag_deg` reached
the 8° clamp. Where the ship appears on screen is a separate choice (`offset_y`
sets the anchor row at `(1+offset_y)/2`), and the elevated construction
(`chase_camera_math.h`, `pitch_down_deg != 0` branch) already realises any
`(p, offset_y)` pair: boom elevation seen from the ship is `p + atan(offset_y ·
tan(half vfov))`. So "see the ship from above" and "look down the ship's axis"
are two independent knobs, and the installed configuration spends 13° of the
31.7° top-view angle on the one knob that displaces the reticle.

The native game is the degenerate case: boom on the axis (`boom_local=0.3,0.4,-17403`
in run60), view parallel, crosshair constant `(0,9)`; ship, reticle and vanishing
point coincide at centre ([target indicator study](../reverse-engineering/chase-target-indicator.md)).

## How other games do it

Grouped by mechanism. Everything below about other titles is recalled from play
and public material, not verified against their code or documentation this
session; treat named games as examples of a pattern, not as measured facts.

**A. Elevated, parallel optical axis; ship in the lower half; reticle at centre.**
The camera sits above and behind and looks parallel to (or a few degrees below)
the ship's forward axis, so the forward vanishing point is at or near screen
centre and the crosshair is camera-fixed there. The ship's top is visible
because the line of sight *to the ship* slopes down, not because the axis does.
Inferred examples: Rogue Squadron, Star Fox, Ace Combat third-person (aircraft
lower-centre, gun pipper on the nose axis), Freelancer (default third-person,
centre crosshair), Everspace 1/2, Chorus, No Man's Sky ship view, Rebel Galaxy
Outlaw, Star Wars Squadrons third-person. This is the dominant design; a camera
tilted down to frame the ship is used for cinematic/vanity views, not for aiming.

**B. Camera-centre reticle with the ship slaved to it (mouse-aim).** The
reticle is fixed at camera centre; the player steers the camera and the ship
turns toward it, guns converge on the reticle, and the ship visibly swings under
the fixed reticle during turns. Inferred examples: Everspace, Rebel Galaxy
Outlaw, War Thunder mouse-aim (its "instructor"), Star Citizen third-person
gimbal aim. X3 already has this paradigm natively: cursor steering with a
screen-centre dead zone (`0x0040e8c0`) and cursor fire unprojected through the
sector camera ([mouse fire](../reverse-engineering/chase-mouse-fire.md)); the mod
keeps `+0xf0 = B_cam × B_shipᵀ` so the cursor is already a true free reticle.

**C. Dual reticle (nose pipper plus camera/aim reticle).** A second marker shows
where the guns actually point when the two differ (gimbals, convergence, lag).
Inferred examples: War Thunder (gunsight versus mouse cursor), Star Citizen
(ship centre versus gimbal reticle). Requires drawing a second element; out of
scope under the brief's constraint that the mod repositions, not redraws, the HUD.

**D. Leading indicator.** Universal; X3's own predictive lead marker is restored
in chase by `chase_lead` ([lead marker](chase-lead-marker.md)) and is target-specific,
so it does not need to be reinvented.

**E. Cinematic external views without aiming.** Elite Dangerous's external camera
suite is a filming tool rather than a combat view (recalled; unverified). Not a
model for this mod, whose chase view is the combat view.

Common denominators of the aiming designs: (1) optical axis parallel or within a
few degrees of the forward axis, so the reticle lives at centre; (2) the reticle
is camera-fixed and the *ship* moves on screen under lag, never the reticle;
(3) camera lag is small in combat; (4) FOV is wide; (5) distance is a few ship
lengths. Screen-space clamps and dynamic FOV are refinements of these, not
substitutes for the axis choice.

## Recommendation for this mod

Everything recommended is reachable with existing tunables; no hook, seam, node
write or hot-path work changes, and native Windows behaviour is identical to
CrossOver because only launch-time constants differ.

**Preferred: near-parallel elevated camera, native centre anchor (pattern A).**
`--chase-pitch-down-deg 0.5 --chase-offset-y 0.50` with the default
`--chase-hud-anchor centre`. `manage.py` parses the pitch as a float and the
handler treats any non-zero value as the elevated construction (`env_double`,
`pitch_down_deg == 0` selects legacy), so 0.5° is the parallel design with the
camera raised above the axis rather than the legacy on-axis tilt-up. Settled, the
forward vanishing point is 4.5 px above centre, inside the crosshair glyph; the
native `(0,9)` crosshair becomes a true boresight cue, the mouse-steering dead
zone, the reticle and the boresight coincide again as in the native view, and
`centre` (the user's preference) is no longer a false cue. The top-view angle
drops from 31.7° to 21.1° with the ship at 75 % of screen height; if the user
wants more top view, `--chase-offset-y 0.60` gives 24.7° with the ship at 80 %.
During turns the ship, not the reticle, swings (pattern B's look), with bolts
deviating by the lag angle; that is the behaviour every game in group A/B has.

**Compromise if the user wants both a stronger top view and a true reticle:**
`--chase-pitch-down-deg 5 --chase-offset-y 0.50 --chase-hud-anchor forward`.
Reticle 45 px above centre (just above the glyph's own height, no longer
"upper-middle"), top view 25.6°, ship at 75 %. `forward` keeps the whole native
group on the vanishing point, so the distance readout stays under the crosshair.

**Lag clamp:** with `forward` the reticle wanders with the lag, ±72 px
horizontally at the 8° clamp (run60 measured ±73). `--chase-lag-clamp-deg 4`
halves that to ±36 px horizontally and roughly −31..+40 px vertically at 0.5°
pitch (9..81 px at 5°). Applies only if the user chooses `forward`; with
`centre` the reticle never moves. Optional, and independent of the pitch choice.

Parameter table (768 rows, `half_vfov_tan` 0.75, formulas above):

| pitch | offset_y | vanishing point above centre | top-view angle | ship anchor row | anchor |
| ---: | ---: | ---: | ---: | ---: | --- |
| 13 (installed) | 0.45 | 118 px | 31.7° | 72.5 % | forward (true) / centre (false, 13° low) |
| 8 | 0.45 | 72 px | 26.6° | 72.5 % | forward |
| 5 | 0.50 | 45 px | 25.6° | 75 % | forward (compromise) |
| 3 | 0.55 | 27 px | 25.4° | 77.5 % | forward or centre (27 px error) |
| 0.5 | 0.50 | 4.5 px | 21.1° | 75 % | **centre** (default since 2026-09-16) |
| 0.5 | 0.60 | 4.5 px | 24.7° | 80 % | centre, more top view |

Finite-range convergence at 0.5°/0.50: the forward ray passes the ship's nose at
the anchor (−192 px), −93 px at one boom length ahead, about −10 px at 500 m and
−5 px at 1 km (using the unverified 500 units/m only for the metre labels), so
bolt streams visibly converge on the crosshair from the ship's nose, which is
the cue group-A games rely on.

Run 26 accepted the preferred row, and the constants edit in
`chase_camera_math.h` and `manage.py` (with the help text) landed on 2026-09-16,
covered by the existing geometry oracles.

## Verification

- Host, before the run: `test_chase_camera.py` already parameterises
  `pitch_down_deg`/`offset_y`; add one oracle case at 0.5°/0.50 asserting the
  elevated branch is taken (not legacy), the anchor row is 75 % and the
  forward projection lands within 5 px of centre; `test_chase_lead.py`'s anchor
  case at 0.5° expects `(0,-4)`/`(0,-5)`. Same command as item 2 of the anchor note.
- One user run per row tried, `--camera chase` with the row's flags, `centre`
  anchor: settled ship (`lag_deg` < 0.5), stationary target ≥ 1 km under the
  native crosshair, fire without a tracked lead; bolts must pass through the
  crosshair. Then the user judges top-view adequacy and whether the swinging
  ship under a fixed reticle reads well in turns. If they prefer more top view
  than 24.7°, run the 5°/0.50/`forward` row.
- Diagnostics needed: none new; `first_applied` (`half_vfov_tan`, `boom_local`),
  `chase_camera` window (`lag_deg`, `pos_lag`) and `chase_central_hud_window`
  (`anchor_last_px` with `forward`) already carry the numbers.

## Alternatives considered

- **Keep 13° and widen the FOV** (write sector camera `+0x298` at the final-FOV
  seam) to shrink the offset: at 90° vertical FOV 118 px becomes 88 px, still
  off-centre, and it distorts the scene and changes native zoom behaviour. Loses.
- **Anchor `forward` on the spring target basis instead of the written basis**
  so the reticle stays fixed while the ship swings: with a near-parallel axis
  the fixed point is screen centre anyway, so `centre` gives this for free.
  Only worth a code change if the user insists on ≥ 5° pitch *and* a fixed reticle.
- **Proxy-drawn second reticle (pattern C)**: needs a new draw in Present,
  glyph art, scaling across resolutions and TAA interaction; contradicts the
  reposition-only constraint. Loses.
- **Shorter distance** to bring the ship's nose closer to the vanishing point:
  the vanishing point does not move with distance; only finite-range parallax
  shrinks and the hull crops sooner. Loses.

## Unknown

- Whether the user accepts a 21–25° top view instead of 31.7°; only a run settles it.
- The crosshair glyph's visual hot spot relative to its node origin (existing
  open item); at 0.5° the residual 4.5 px is below any plausible calibration error.
- All statements about other titles are recalled, not verified; they motivate
  the pattern, and the mod's own geometry, not those recollections, carries the recommendation.
