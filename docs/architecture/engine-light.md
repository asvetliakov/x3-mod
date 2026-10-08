# Engine light on the hull

Built 2026-10-03 (gap 8, phase 4 of [engine-exhaust-gap-analysis.md](engine-exhaust-gap-analysis.md)); not flown. Each
ship's main engines (up to 72 since 2026-10-08, "Plate cap"; one light per plate, "A light per plate") light the hull plates
around their nozzles through the material route: a point light in
linear light, added inside the original-shading fill block of the converted hull programs, fed per hull node from the
previous frame's glow-jet records. Option `engine_light` (`X3M_ENGINE_LIGHT=on|off`, `--engine-light`), default on,
effective only with `engine_effects = plumes`. Ledger: [engine-light.md](../verification/engine-light.md).

## The law

Per ship (the jets' parent node+0x18, the root) every main nozzle of the scene view up to 72 (the ship's plates,
"Nozzle plates", "Plate cap"), each with its own light; the brightest, brightness I(s) x value_eff, ties to the lower node handle, is
plate 0, the others follow in that order, and each hull pixel takes the light of one plate ("A light per plate"). RCS
(`flag_steering`), brake-pushed (`flag_brake`), geometry-less, other-view and parentless records never feed it.

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
| c52-c54 (DEF) | selecting twins: the tier thresholds 2 .. 10 ((2, 3, 4, 5), (6, 7, 8, 9), (10, 0, 0, 0)), "Plate cap" |
| c197 - 2i, i = 0..71 | slot i's nozzle plate ("Nozzle plates"): ((P_i - cam) / v_i, 1 / v_i); in the slots the twin runs without a plate a pad (slot 0's); slot 0's (2, 0, 0, 0) when its plate is not finite |
| c198 - 2i, i = 1..71 | plate i's light colour (colour_i, 1 / R_i^2), R_i = 3 v_i ("A light per plate", "Plate cap"; a pad: c201's); its light point is the plate's, L_i - cam = ((P_i - cam) / v_i) v_i |
| c198 (DEF) | plate twins: (-1 / (r1^2 - r0^2), r1^2 / (r1^2 - r0^2), 0, 4) |
| c199 (DEF) | (cap 1, -2^-20, 2^-40, 0) |
| c200 | plate 0's light (L - cam, R^2), world axes |
| c201 | plate 0's (colour, 1 / R^2) |
| c202 | (F, tier): .w the plate tier 0-11 (runs 1, 4, 8, 12, 16, 24, 32, 40, 48, 56, 64, 72 slots) |

No original program reads c50 or above (corpus scan of all 429 ps_2+ programs), so the per-draw upload needs no
restore. A twin is refused when its original reads or defines any of c52-c202 or r12-r15 (c176-c202 from the light
per plate until the plate cap, c190-c202 before). The route uploads the run of the draw's tier, the 2 run - 1
registers ending at c197 (c197 alone for a one-plate ship), and the light c200-c202 in two SetPixelShaderConstantF
calls; the API never writes c52-c54 or c198-c199 (the twins' DEFs), so nothing relies on a DEF shadowing a later
upload. The block sizes and the twins' slot deltas since the plate cap are in "Plate cap"; the figures below are the
plates' (2026-10-04). Slots: +25
on the fill variant, +28 on the share producer (two fill blocks), +45 / +70 at K = 0 (the block is emitted for the
light alone), and on the four gained kinds 41 more for the nozzle plates (+66, +69 with the share; +4 with one plate
before Run 125); largest twin 368 weighted slots, 2,628 DWORDs
(`ps_f1b0e820c7b488c3`, share + gain + widening, base 299; measured 2026-10-04 over all 104 x 8 option sets with
[plate_slots.py](../../verification/results/engine-light/plate_slots.py); 331 with one plate, 327 before the plates).

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

All main nozzles (after Run 125: the Split Ocelot's stern nozzles at 2.4 km kept white rings, the plates of the
nozzles other than the light's): w is the maximum over the ship's plates, up to `engine_light::core::plate_slots` (8
until 2026-10-08, 72 since: "Plate cap").
A plate is each main nozzle's light point (0.5 x its value_eff behind it along its axis, where its light would sit)
with its own value_eff, so the window 0.75..1.0 x value_eff holds per nozzle and the light's own nozzle is plate 0 with
the Run 125 law. `build_ships` adds every main record of the scene view to its ship's list, brightest first (I(s) x
value_eff, ties to the lower handle, then the earlier record), one plate per node handle (the brighter record), the
dimmest giving way past `plate_slots` (`plates_dropped`). A record that `engine_plumes::merge_layers` marks as a smaller
co-located layer (within 1.5 x its own size of the larger) is, since Run 129 A (2026-10-04), a plate at its natural
value_eff with its light unfloored
(`unfloored`; until then it was dropped and added no plate, `merged`), so the plates follow the plumes' nozzles and
values. The Light's own fields are plate 0's, chosen over all main records (until 2026-10-08 the only light; each
plate now carries its own, "A light per plate"). `build_nodes` carries each plate into the node's model space with the
light; per draw `plate_constants` places them with the draw's world rows, relative to the camera, scaled by 1 / v_i.
The `engine_light_frame` row gains `plates=` (the ships by plate count 1..8; since the plate cap `plates_more=` the
ships above eight and `plates_max=` the largest count), `plates_none=`, `unfloored=` (`merged=` before Run 129 A) and
`plates_dropped=`; `engine_light_mode` reports `constants=c55-c202 plates_max=72` (`c176-c202 plates_max=8` from the
light per plate, `c190-c202` before).

Pixel program (gained twins; the registers and tiers as of 2026-10-04, eight slots c190-c197 and three tiers; since
the plate cap slot i reads c(197 - 2i) and eleven tier levels nest, "Plate cap"): after t = w / (e . F) the block
forms D = e t (MUL), then per plate slot i `mad r14.xyz, r15, c(190+i).w, -c(190+i)` ((D - P_i) / v_i), `dp3 r14.w, r14, r14` ((d_i / v_i)^2) and
`min r15.w, r14.w, r15.w` (input first: a NaN distance keeps the minimum; slot 0 starts from c198.w = 4). The light
then reads D (`add r14.xyz, c200, -r15` in place of the MAD) and keeps 1 / d in r15.x, so r15.w carries the minimum
m through it; after q, `mad_sat r15.w, r15.w, c198.x, c198.y` gives w = saturate((r1^2 - m) / (r1^2 - r0^2)) (w falls
with (d / v)^2, so the minimum distance is the maximum weight) and `max r15.w, r15.w, c198.z` maps a NaN to 0. An unused
slot uploads (2, 0, 0, 0): (d / v)^2 = 4, weight 0. A ship pays only its slots: slots 1-7 sit under an if_ne of the
tier against 0 (c198.z) and slots 4-7 under a nested if_ne of the tier against 1 (c199.x, the cap: a static_assert
holds `engine_light_cap` at 1), c202.w the tier the route uploads with the light (`plate_tier`: 0 for one plate, 1 for
two to four, 2 for five to eight), so a ship runs 1, 4 or 8 slots. Each branch first moves the tier and the threshold
into r14.x / r14.y (the per-slot scratch, free between slots) and compares the two temporaries, the form the corpus's
XT originals use (if_ne on two temporaries; no original compares constants). The operands come from constants, so the
branches are uniform (no divergence), and the twin's structure check admits them only for a plate twin (`structure(..., xt || plate_on)`; a hull original itself
has no flow control, an XT plate twin's block sits at its depth 0 by the plate condition). The gain site is unchanged:
`mad r15.w, -r15.w, g, r15.w`, `add r15.w, r15.w, g`, `mul rL.xyz, rL, r15.w` (g = c223.x, or c217.w with the far
fade). The plate block is 53 instructions, 61 weighted slots against the light's 18 / 22 (+39; with the gain's +2,
+41 per gained twin). Emitted only when the light's site precedes the light-map fetch and both sit outside flow control
(all 100 gained programs, measured); otherwise the twin keeps the plain gain. In each gained option set the other 4
of the 104 twins are the glass programs, whose transform applies no light-map gain at all (gain flag 0: nothing for the
plates to suppress; [gained_without_plates.py](../../verification/results/engine-light/gained_without_plates.py)). The emitted program re-proves c198 (one
DEF, five reads), c199 (one more read), c202 (three reads), c190-c197 (no DEF, two reads each), r15 (the block's
references plus six) and the block's position before the fetch. The reach ratio 3 in the pixel program equals
`engine_light::core::reach` (pinned by `test_engine_light.py`).

Cost (the i686 fixture under Wine/FEX, `run_engine_light.py`; host arm64 figures in the ledger): the per-draw hit with
the plates and the two uploads (8 + 3 registers), the ship table over 1,024 records of 128 ships x 8 nozzles, the node build and
the full-screen twin term at 5120x1440 are in [engine-light.md](../verification/engine-light.md) (2026-10-04, "All
main nozzles"). The upload is 11 registers in two calls per lit draw (3 in one before).

Limits: the suppression lives in the twin, so it acts only where the hull light does (`engine_effects = plumes`,
`engine_light` on, a ship in the light table, its routed hull draw). With `engine_light` off, or for a ship without a
recorded main jet (unlit: no plates either), the plates keep the full gain; no second program set carries the weight.
A ship with more than `plate_slots` main nozzles (eight until 2026-10-08, 72 since) keeps gain 4 on its dimmest ones. When a ship's light unbinds (evicted at the
256-ship cap, no main-jet record that frame, the option off) its plates jump from about 1 back to 4 in one frame, no
fade; a far-fade far gain under 1 (`light_map_far_fade` third value, default 1) would raise a plate to 1 above the
faded hull around it, because g - (g - 1) w is not clamped at g >= 1. Plates of nozzles outside the node table's
reach (turrets and parts deeper than one level) are not drawn by a lit draw and keep the gain.

## A light per plate

User decision 2026-10-08, after Run 134 A (run12, [run134-dxvk-triage/engine-light](../../verification/results/run134-dxvk-triage/engine-light/)):
the Split Ocelot's two secondary `big3` nozzles have equal value_eff 548.076 and sit 2,411 apart, beyond one reach
(3 x 548.076 = 1,644); the one light per ship went to the tie's lower handle, so head-on the facing nozzle's hull got
no light while its plate still suppressed the light-map gain. Rule now: every plate carries its own nozzle's light
(position, value_eff and colour as the single light had them), the plates stayed capped at eight per ship (72 since
"Plate cap") and the plate weight is unchanged; each hull pixel takes the light of the plate nearest in units of its value_eff, u_i = (d_i / v_i)^2
the least (ties: the earlier, brighter plate), then evaluates that one light. Since R_i = 3 v_i for every plate, the
least u_i is the largest falloff saturate(1 - d_i^2 / R_i^2)^2: "nearest within reach" and "strongest falloff" are the
same plate, and outside every plate's reach the light is 0 whichever is taken. The selection does not weigh colour or
throttle (equal for equal nozzles; a brighter but farther plate loses to a nearer dimmer one inside its reach). One
falloff evaluation per pixel, as before.

Pixel program: the u_i are the plate block's own distances (c190-c197, the same MAD + DP3), so the selection adds to
each slot the twin runs only the condition `add r14.y, r14.x, -r14.w` (u_i - s), two `cmp` copying the slot's light
registers into r12 (position, R^2) and r13 (colour, 1 / R^2) when the condition is negative, and `min r14.w, r14.x,
r14.w` (s, from u_0 of slot 0). r12 / r13 start as `mov r12, c200` / `mov r13, c201` (plate 0); the light law then reads
r12 / r13 in place of c200 / c201. r12 / r13 are the fill block's scratch, written by it before it reads them, so they
are dead where the light block runs (the original never reaches r12: its temporaries are below 8). The twins without
light-map gain (fill, share producer, K = 0) carry the same selection behind the same tier branches, keeping e and t in
r15 and forming D = e t per slot (one MUL more); a gained twin whose block cannot carry the plate weight (not before the
light-map fetch at depth 0, none in the corpus) keeps plate 0's single light, so no branch sits between the light-map
fetch and the final where the gain's proof admits no flow control. Slot padding: a slot the twin runs (1, 4 or 8 by the
tier) without a plate, or with a non-finite one, is a copy of slot 0 (plate register and light), so its u equals u_0
and it changes neither the weight nor the selection; a NaN condition takes the slot, whose E is then NaN and cleared by
the final `max`.

The registers and sizes as built 2026-10-08 (eight plates; superseded the same day by "Plate cap"):

| | Before (2026-10-04) | Light per plate (eight plates) |
| --- | --- | --- |
| Per-draw registers | c190-c202: 11 uploaded + 2 DEF (13) | c176-c202: 25 uploaded + 2 DEF (27); a one-plate ship uploads 11 (c190-c197, c200-c202) |
| Upload calls | 2 (8 + 3 registers) | 2 (22 + 3; 8 + 3 for one plate) |
| Block, gained twin | 53 instructions / 61 weighted slots | 83 / 91 |
| Block, other twins | 18 / 22 | 80 / 88 |
| Largest twin | 368 weighted slots | 398 (`ps_f1b0e820c7b488c3`, share + gain + widening, base 299); the transformer's own cap is 512 |
| Temporaries | r14, r15 | r12-r15 |

ps_3_0 has 224 float constants; the hull originals use none above c49. The slot figures are measured over all 104 x 8
option sets by `engine_light_structure.cpp` (`test_engine_light.py` re-derives every twin word for word: +66 slots on
the twins without gain, +71 on the gained ones over the single light's block plus the fill blocks). Executed per pixel:
a one-plate ship pays two MOVs more (without gain also the first branch's two MOVs and if_ne, which the gained twins
already ran); each further slot the tier runs costs four slots more on a gained twin (condition, two CMP, minimum) and
seven on the others (which ran no plate slots before).

A ship with one main nozzle gets bit-identical shader input in the registers it reads and a bit-identical image: the
GPU fixture's nine single-nozzle cases, the four single-nozzle plate modes on both pairs and the Reset witness have
equal SHA-256 over every sampled readback before and after the change on wined3d
([compare_reports.py](../../verification/results/engine-light-per-plate/compare_reports.py),
[single-nozzle-equivalence.json](../../verification/results/engine-light-per-plate/single-nozzle-equivalence.json)).

Limits: where two plates' reaches overlap, the pixel switches lights on the surface u_i = u_j, a visible edge where the
two lights differ in direction (N . l) or colour (the "overlap" fixture case: 8 samples of 16,384 within 1e-3 of the
switch; the Ocelot's equal nozzles 2,411 apart are both in reach in a lens around their midpoint, on the midplane up to
sqrt(1,644^2 - 1,205.5^2) = 1,118 units from the line between them, inferred). Not seen in a flight yet. The
suppression and the light still act only on lit draws (unchanged).

## Plate cap

User decision 2026-10-08 ("remove artificial limits"), after Run 135 A (run14,
[run135-dxvk-triage](../../verification/results/run135-dxvk-triage/)): the Split Ocelot has ten main nozzles (two
`huge` at value_eff 939.2, eight `big3` at 548.1, the big3 a brightness tie); with eight plates its two highest-handle
big3 (0x56e / 0x56f) lost their plate and light whenever all ten were drawn (5,077 frames with two drops) and regained
them when two others left the screen. `plate_slots` is now 72, chosen from the real maximum: main nozzles per ship scene
([nozzle_counts.py](../../verification/results/engine-light-plate-cap/nozzle_counts.py),
[output](../../verification/results/engine-light-plate-cap/nozzle_counts_out.json); scene parts on the SBTYPE_JET list or
under `effects/engines/`, role main by the C word; scene parts carry no parent, so all of a scene's jets hang under
the ship root) are at most 66 in both views: installed (516 ships with a text scene) 66 for the Boron Segaris (M7
drone carrier), then 40 (Argon Atlas), 36, 32, 28; 79 ships above 8, 19 above 16, 4 above 32, 1 above 48; stock (363)
66 for the Boron M7 drone carrier, then 14, 10. A ship beyond 72 (none today; a mod's) still drops its dimmest
nozzles (`plates_dropped`).

Registers: three per plate (plate register, light position, colour) would hold at most 49 plates between c50 and c202.
A further plate's light point is its plate point and R_i = 3 v_i, so its light needs only its colour: slot i's plate
register at c197 - 2i and, for i >= 1, its colour (colour_i, 1 / R_i^2) at c198 - 2i, directly above. 72 slots fill
c55-c197; the tier thresholds take c52-c54 (DEF); c198-c202 as before. The selection copies the winning slot's plate
register into r12 (starting from slot 0's inside the first branch) and its colour into r13 (starting from c201), and
before the first branch closes turns the plate back into a light once per pixel: `rcp r14.x, r12.w` (v),
`mul r12.xyz, r12, r14.x` (L - cam = ((P - cam) / v) v), `rcp r12.w, r13.w` (R^2). A one-plate ship runs no branch,
so r12 / r13 keep c200 / c201 and the law reads them exactly as before; on a multi-plate ship the reconstructed light
differs from the CPU's by float rounding (R^2: 2,703,485.75 against .76 in the host model, measured).

Tiers: c202.w = k runs slots 0 .. runs[k] - 1, runs = 1, 4, 8, 12, 16, 24, 32, 40, 48, 56, 64, 72
(`engine_light::core::plate_runs`, `EngineLightAbi::plate_runs`, asserted equal at the upload): one plate alone, then
steps of four to 16 (most ships: 8 or fewer, 437 of 516 installed) and of eight to 72, at most three pad slots below
16 plates and seven above (the Ocelot runs 12). Eleven nested uniform `if_ne` levels on the tier, level j comparing
with j - 1 (threshold 0 c198.z or c199.w, 1 c199.x, 2-10 c52-c54); ps_3_0 allows 24 levels. Per level the two MOVs of
the XT originals' form; a ship pays its levels and one failing comparison. Upload: the run's 2 runs[k] - 1 registers
ending at c197 (c197 alone for one plate: 4 registers in two calls with c200-c202, 11 before; 23 for the Ocelot; 143 for
72) and c200-c202; slots beyond the run are neither written nor uploaded (their stale values sit in branches the tier
skips). A ship whose first plate is not finite runs tier 0 (plate 0's light, no plate weight; before, its other plates
still ran). Twins refuse originals touching c52-c202 (none in the corpus: 832 of 832 created).

| | Light per plate (eight) | Plate cap (72) |
| --- | --- | --- |
| Per-draw registers | c176-c202: 25 uploaded + 2 DEF | c52-c202: up to 146 uploaded (2 run - 1 + 3) + 5 DEF |
| Upload, one plate / Ocelot / 72 | 8 + 3 / - / - | 1 + 3 / 23 + 3 / 143 + 3 |
| Block, gained twin | 83 instructions / 91 weighted slots | 597 weighted slots |
| Block, other twins | 80 / 88 | 594 |
| Twin minus single-light block | +71 gained, +66 others | +577, +572 |
| Largest twin | 398 slots, 2,760 DWORDs | 904 slots, 4,865 DWORDs (`ps_f1b0e820c7b488c3`, share + gain + widening, base 299) |
| CreatePixelShader (fixture, Wine) | 54.7 us | 80.2 us |

The twins now exceed 512 slots; the transformer checks the device's budget (platform-portability.md "Shader slot
budget": `MaxPixelShader30InstructionSlots`, 32768 where the device reports 512) instead of a fixed 512, and
`engine_light_variant` logs `slots_max=` (692 for the seam case's hull program on wined3d, `ps3_program_slots`
weights) and `slot_budget=`. Slot figures: [plate_slots.py](../../verification/results/engine-light-plate-cap/plate_slots.py)
over all 104 x 8 option sets (before: `plate_slots_before_d77c26dd.json`).

Cost (measured; the ledger's 2026-10-08 "Plate cap" row has the commands): CPU per lit draw on the i686 fixture under
Wine/FEX 63.5 / 160.2 / 806.3 ns for ships of 1 / 8 / 72 nozzles (74.3 ns for one before), host arm64 5.7 / 25.0 /
191.5 ns; the ship table over the ring's 1,024 records 43.3 us (128 ships x 8; 67.3 before: `LightFields` keeps a
record's light at 64 bytes instead of the 3.5 KB entry) and 105.4 us (14 x 72); the node table over 1,024 logged draws,
warm, 51 / 100 / 407 us under Wine/FEX (51 / 104 before for 1 / 8). GPU term per full-screen production BUMPMAP draw at
5120x1440 (wined3d): +90.7 / +184.8 / +300.2 / +427.2 / +2,415 us for 1 / 3 / 8 / 10 / 72 nozzles (before, same
session: +93.3 / +161.7 / +238.7 for 1 / 3 / 8; earlier ledger runs of unchanged code ranged 157-259 us at 8), at
1920x1080 +22.4 / +53.3 / +86.6 / +121.9 / +682.0. A 72-nozzle ship filling a 5120x1440 screen costs 2.4 ms per draw of
its hull.

A ship with one main nozzle is unchanged on the GPU: the fixture's nine single-nozzle cases, the four single-nozzle
plate modes on both pairs, all INVARIANT rows and the Reset witness have equal SHA-256 over every sampled readback
against a wined3d run of the d77c26dd build ([compare_reports.py](../../verification/results/engine-light-plate-cap/compare_reports.py),
[single-nozzle-equivalence.json](../../verification/results/engine-light-plate-cap/single-nozzle-equivalence.json)).

## Variant pair, not a zero-light term

Each original-shading variant (plain motion, fill, gained, gained widened, share, share gained, share gained widened)
gets a twin at registration; a draw whose node carries a light binds the twin of the program the pair selection chose
and uploads the run of its plates and colours (ending at c197) and the light c200-c202, every other draw binds today's program and uploads nothing. A zero-light term in every program
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
drawn, nodes, candidates, draws lit, no_twin, no_rows, the record census, the log and node counts, twins; since
2026-10-08 `lights=` the plates over all ships, each carrying its nozzle's light, and `own_lights=` / `most_lights=`
the own ship's and the ship of the most plates' root and plate node handles, brightest first, i.e. the plate slot
order, `-` when none: `most_lights=00663300:5a` in the seam case; since the plate cap `plates_more=` and
`plates_max=`). `ps3_slot_budget` once per device (`ps30_slots=` the device's MaxPixelShader30InstructionSlots,
`budget=` the transformers' limit, `rule=spec_minimum|device_cap`), and `engine_light_variant` carries `slots_max=`
(the largest created twin, `ps3_program_slots` weights) and `slot_budget=`. No per-pixel rows. See
[logging-tiers.md](logging-tiers.md).

## Native Windows

ps_3_0 arithmetic and `SetPixelShaderConstantF` / `GetVertexShaderConstantF` only; no new resource or state. Runtime
unverified on Windows ([platform-portability.md](platform-portability.md)).

## Limits and open questions

- Nozzle plates keep the full light-map gain where no twin is bound (`engine_light` off, an unlit ship); see
  "Nozzle plates".

- Ships whose hull nodes hang deeper than one level under the root (turrets on sub-nodes) are not lit; the main hull is
  the root or its child in the ships studied ([engine-effects.md](../reverse-engineering/engine-effects.md) section 4).
- Twin nozzles: each carries its own light (since 2026-10-08, "A light per plate"; before, one light behind the
  brightest, ties to the lower handle); a pixel takes one of them, so where their reaches overlap the hull shows the
  switch as an edge. Light and suppression cover up to 72 of the ship's main nozzles ("Plate cap"; every ship of the
  installed and the stock tree has at most 66).
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
