# Engine light on the hull

Built 2026-10-03 (gap 8, phase 4 of [engine-exhaust-gap-analysis.md](engine-exhaust-gap-analysis.md)); not flown. Each
ship's brightest main engine lights the hull plates around its nozzles through the material route: a point light in
linear light, added inside the original-shading fill block of the converted hull programs, fed per hull node from the
previous frame's glow-jet records. Option `engine_light` (`X3M_ENGINE_LIGHT=on|off`, `--engine-light`), default on,
effective only with `engine_effects = plumes`. Ledger: [engine-light.md](../verification/engine-light.md).

## The law

Per ship (the jets' parent node+0x18, the root) the brightest main nozzle of the scene view, brightness I(s) x
value_eff, ties to the lower node handle (twin nozzles stay on one side frame after frame). RCS (`flag_steering`),
brake-pushed (`flag_brake`), geometry-less, other-view and parentless records never feed it.

| Quantity | Value |
| --- | --- |
| Position | nozzle + 0.5 x value_eff along the plume axis (into the exhaust) |
| Radius of influence R | 3 x value_eff, falloff saturate(1 - d^2 / R^2)^2 (zero at R) |
| Colour | the record's mean tint x I(s) x the preset's scale x 0.25 (I(s) = lerp(1.2, 4.0, s): 1.0 x tint at full throttle, 0.3 at idle) |
| Term | E = colour x saturate(N . l) x falloff, N the geometric world normal (v3, interpolated) |
| Cap | E' = min(E, saturate(1 - decode(sum))): the lit plate stays at or below 1 where the native lighting was |

value_eff is the plume floor's value (`engine_plumes::floored_value`). The term enters where the fill does: in the
fill block at the lobe-sum site, sum = encode(decode(sum) + K decode(C0) + E'), so the albedo multiply applies to it
(power law: encode(x) A = encode(x decode(A))) and the sun-share twin fill(sum) - fill(sum - S) keeps the light out of
the sun share (the shadow apply never darkens it). Constants (`colour_scale`, `behind`, `reach`, cap 1) are in
`engine_light_core.h` and `linear_engine_light_inc.h`; the eye decides them in flight.

## The pixel term

Every reviewed hull, palette, XT and glass pixel program (104; the four asteroid programs refuse) reads the eye vector
cam - P on TEXCOORD1 = v2 and the world normal on TEXCOORD2 = v3; the hull families and XT DEFAULT interpolate the eye
unnormalised, the palette families, XT BUMPMAP and glass normalise it first
([eye_normal_registers.py](../../verification/results/engine-light/eye_normal_registers.py), all 168 pairs
consistent). The term uses only the direction: the pixel's position relative to the camera is D = e w / (e . F),
e = nrm(v2), w the motion depth interpolator's clip w (`pixel_depth_input_register`.y; w = view z, P[11] = 1) and F the
camera's forward axis, exact for both eye forms up to the interpolated unit vector's bend (its triangle's angular size
squared) and independent of the partial-precision v2 magnitude. 18 instructions, 22 weighted slots, temporaries
r14/r15, plus three instructions per fill block; guards: e . F <= -2^-20, d^2 >= 2^-40, NaN/negative E to 0 (input
first in MAX/MIN).

| Register | Content |
| --- | --- |
| c199 (DEF) | (cap 1, -2^-20, 2^-40, 0) |
| c200 | (L - cam, R^2), world axes |
| c201 | (colour, 1 / R^2) |
| c202 | (F, 0) |

No original program reads c50 or above (corpus scan of all 429 ps_2+ programs), so the per-draw upload needs no
restore. Slots: +25 on the fill variant, +28 on the share producer (two fill blocks), +45 / +70 at K = 0 (the block is
emitted for the light alone), and on the four gained kinds 4 more for the nozzle plates (+29, +32 with the share);
largest twin 331 (measured 2026-10-04, all 104 x 8 option sets; 327 before the plates).

## Nozzle plates

Flight G (Run 124 A, run412, the own Split Scorpion in chase view): the white blob at the engine was mostly the hull.
The Scorpion's light map (`unique_split_m4_light`) is white (0.99-1.0) on 66 of the 5,997 LOD-0 faces, the recessed
rear nozzle plates (normals straight back), and the hull light-map gain 4 (`hull_lightmap_gain`, for lit windows and
markings) made them emit about 4 on every channel: plate minus the surrounding hull 3.7/4.3/4.3 in the F8 HDR capture
of frame 5437, the plume on top red ([run412_nozzle_split.py](../../verification/results/run412-engine-disc/run412_nozzle_split.py)
and its output).

Rule: in a twin that carries the light-map gain g, the light-map term is scaled by g - (g - 1) w instead of g, w the
plate weight around the light: 1 within r0 = 0.75 x value_eff of the light, 0 from r1 = 1.0 x value_eff, linear in
d^2 between (w = saturate((r1^2 - (d / v)^2) / (r1^2 - r0^2))). Near the nozzle the plate emits the texture's own value
(at most 1.0) and the plume's colour shows; windows and markings outside r1 keep the full gain; the lit diffuse and
the engine light term are untouched. The light sits 0.5 x value_eff behind the nozzle, so a nozzle plate lies about
0.4-0.55 x value_eff from it: the plain saturate(1 - d^2 / R_lm^2) would leave the plates at gain 1.33-1.57 with
R_lm = 1.25 x value_eff (2.0 still 1.13-1.22), hence the plateau.

Coverage on the Scorpion ([nozzle_plate_coverage.py](../../verification/results/engine-light/nozzle_plate_coverage.py),
[output](../../verification/results/engine-light/nozzle_plate_coverage_out.txt); the hull's world rows and the
`fx_engine_xtc_red_nor` origin of frame 5437, 1 record unit = 1,049.6 model units, value_eff 23.5 = 24,666 model units):
all 66 white faces at d/v 0.41-0.55 (farthest vertex 0.546), gain_eff 1.000; of the 315 other light-mapped faces
(light map >= 0.2) 30 are touched, 24 of them the nozzle surround at the stern (d/v < 0.6, gain_eff 1.00) and 6 dim
markings 3,400-9,400 model units forward of the nozzle (light map <= 0.39, gain_eff 1.00-3.75); the other 285,
including every face more than 2 x value_eff from the light (259, the windows and markings forward), keep gain 4.

Pixel program: the light block computes w from its q = saturate(1 - d^2 / R^2) with one MAD_SAT (A q + B, A =
9 / (r1^2 - r0^2), B = (r1^2 - 9) / (r1^2 - r0^2), R = 3 x value_eff; shader-local `def c198 = (A, B, 0, 0)`) and a MAX
(input first, NaN to 0) into r15.w; the gain MUL after the light-map fetch becomes `mad r15.w, -r15.w, g, r15.w`,
`add r15.w, r15.w, g`, `mul rL.xyz, rL, r15.w` (g = c223.x, or c217.w with the far fade). +4 instructions, +4 slots, +24
DWORDs; no new upload. Emitted only when the light's site precedes the light-map fetch and both sit outside flow
control (all 100 gained programs, measured); otherwise the twin keeps the plain gain. The emitted program re-proves
c198 (one DEF, three reads), r15 (the block's references plus six) and the block's position before the fetch. The
reach ratio 3 in the pixel program equals `engine_light::core::reach` (pinned by `test_engine_light.py`).

Limit: the suppression lives in the twin, so it acts only where the hull light does (`engine_effects = plumes`,
`engine_light` on, a ship in the light table, its routed hull draw). With `engine_light` off, or for a ship without a
recorded main jet, the plates keep the full gain; no second program set carries the weight.

## Variant pair, not a zero-light term

Each original-shading variant (plain motion, fill, gained, gained widened, share, share gained, share gained widened)
gets a twin at registration; a draw whose node carries a light binds the twin of the program the pair selection chose
and uploads c200-c202, every other draw binds today's program and uploads nothing. A zero-light term in every program
would cost the term on every hull pixel of every frame: measured +50.9 us per full-screen draw at 5120x1440 (0.497 ->
0.548 ms, +10 %, five EVENT-fenced batches of 20 additive draws) for the production BUMPMAP share program; at
1920x1080 the difference stayed inside the batches' warm-up noise (about 14 us by pixel count, inferred). The pair
costs that only on lit pixels (a chase-view own ship is a fraction of the screen) and one 34 ns lookup + constants +
upload per lit draw (3.7 ns per unlit routed draw, 2.2 ns per logged draw; i686 build under Wine/FEX). Creation: 7 twins
per reviewed program, 47.0 us per CreatePixelShader under Wine (measured, `create.us_each` of
[engine-light-gpu.json](../../verification/results/bottle-X3/engine-light-gpu.json)) plus ~19 us per host transform
(measured), about 48 ms over the 104 programs (728 twins, inferred). A twin is created only when its transform applied
the same share, light-map gain and widening as its base (`engine_light::core::twin_matches_base`): the share plan is
made with the fill block's site, so the light could change it; a twin that differs is refused (`mismatched` in the
`engine_light_variant` row) and its draws stay unlit. With the option off nothing is created and every
program is byte for byte today's.

## CPU path and latency

`engine_light_core.h` (portable, host-tested). At the frame boundary, before the ring is cleared: the ship table (at most
256 ships; beyond that a new ship replaces the dimmest entry, by I(s) x value_eff, when it is brighter, and always when
it is the own ship's (`Ring::own`); own-ship entries are never replaced, every light lost to the cap counts
`ships_dropped`; 16-entry block minima keep a newcomer at 32 compares, 23 us for 1,024 records in the worst order on the
arm64 host, measured) from the previous frame's records of the scene view, then the node table: each logged hull draw of a lit ship
(the node itself the root, or its parent the root) gets the light in its model space, L_local = W^-1 (L - t), from that
frame's world rows. A routed draw of the same node and handle places it with its own current world rows and view-inverse
rows (both shadowed from SetVertexShaderConstantF, resynchronised with the shadow): the light rides on the hull whatever
the ship did between the frames (no 300 m/s lag of 5 m per frame), and the camera is the one the program's eye vector
is in. The route logs only draws of ships already in the table, so a ship lights from the second frame its jets are
recorded in. VS layouts: world c28-30 / view inverse c34-36 (the 20 light-loop programs), c7-9 / c13-15 (the 9
single-light programs).

Per routed draw with a lit ship in the table: two ship-table probes (logging), one node-table probe; on a hit the
constants in double (about 30 multiply-adds) and one SetPixelShaderConstantF in the route's apply chain (a failure
rolls the route back to the native draw). Not on fade-arm draws, linear materials or outside the FP16 scene.

## Logging

`engine_light_mode` once per device (setting, status, reason, constants); `engine_light_variant` per reviewed program
(created/refused/mismatched/failed kinds); `--debug`: `engine_light_frame` per frame with a table or a candidate (ships, ships
drawn, nodes, candidates, draws lit, no_twin, no_rows, the record census, the log and node counts, twins). See
[logging-tiers.md](logging-tiers.md).

## Native Windows

ps_3_0 arithmetic and `SetPixelShaderConstantF` / `GetVertexShaderConstantF` only; no new resource or state. Runtime
unverified on Windows ([platform-portability.md](platform-portability.md)).

## Limits and open questions

- Nozzle plates keep the full light-map gain where no twin is bound (`engine_light` off, an unlit ship); see
  "Nozzle plates".

- Ships whose hull nodes hang deeper than one level under the root (turrets on sub-nodes) are not lit; the main hull is
  the root or its child in the ships studied ([engine-effects.md](../reverse-engineering/engine-effects.md) section 4).
- Twin nozzles: the light sits behind one of them (the brightest, ties to the lower handle), not at their centroid.
- The route-level path (registration, selection, upload, Reset) is exercised under Wine by the seam case
  `seam-engine-light` of `run_motion_output.py` (`verification/probe/motion_output_engine_light_seam_inc.h`): a glow-jet
  record of the effects pair under a synthetic root, the reviewed hull pair drawn as another ship's node and as a node
  under the root, the twin of the gained fill base bound and c200-c202 uploaded once, the image against the law, Reset.
- Amount, colour scale and reach need the user's eye (Run 123 or later).

## Not verified in flight

- Turrets and parts deeper than one level under the root stay unlit (the node table takes the root and its direct
  children only): on capitals a lit hull plate can meet an unlit turret or sub-part at a visible seam.
- The light switches off with a culled glow: a ship whose glow-jet draws are culled (the small-parts cull, the LOD
  switch, a jet off screen) records nothing that frame, so its hull light goes out with them.
- Far records (`flag_far`, the small-parts cull's copies) feed the ship table like any main jet, so a distant ship
  whose nozzles the cull removed can still light its routed hull draws; not looked at in a flight.
- Replay passes that reissue a routed hull draw with the twin bound: audited 2026-10-03 (read of the proxy's draw
  issue sites), none exists. The proxy's own draws are the shadow replay (`renderer/shadow_replay_pass.cpp`: its own
  depth programs, bound before every record), the invalid-share stamp (`sun_share_lane_inc.h` `sun_stamp_draw`: only
  for gate-3 refused draws, its own stamp program), the plume, ribbon, shimmer and fog passes (their own programs) and
  full-screen quads; the effect-pass replay was never built ([effect-pass-replay.md](effect-pass-replay.md), decision
  2026-09-17: stop at the native path). The twin is bound only inside a routed draw's apply chain and the route restores
  the application's program after it (c200-c202 are not restored: no original reads c50 or above).
