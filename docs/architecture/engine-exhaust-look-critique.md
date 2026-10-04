# Engine exhaust look: critique and redesign (2026-10-03)

Owning note for the plume look after the fixture's look images
(`verification/results/engine-effects/look-images/`, `README.md` there lists every band). Implemented on 2026-10-03 with
the deviations and measured gates of section 6 (since its "One law" the blend with the slab law is one law
parameterised by the detail level, the 40 / 12 px energy gate 0.5 by decision); sections 1-5 are the design as written (section 1's images are the
previous law's, the look images now show the revised one). The law under critique is `src/effects/engine_plume_ps.hlsl` with `engine_plumes_core.h` `Look` (the user's lab
settings, [engine-effects-modern.md](engine-effects-modern.md) "Plume look redesign" and after), the references are X4 and
Everspace 2 as described in [engine-exhaust-gap-analysis.md](engine-exhaust-gap-analysis.md) section 1.

Measurements: `verification/results/engine-effects/plume_look_metrics.py` -> `plume_look_metrics_out.txt` (the images;
luma is Rec. 709 of the 8-bit display decoded with gamma 2.2, "display-decoded" below; AgX compresses the top, so these
ratios understate the linear ones) and `plume_look_proposal_model.py` -> `plume_look_proposal_model_out.txt` (a CPU model
of the current and the proposed side-view law through `tools/analysis/agx_reference.py`, one noise frame, no halo, ring,
TAA or bloom). Every figure below is from one of the two unless marked inferred.

## Decision in one paragraph

The plume does not need more light; it needs its light moved. Three changes carry the look: (1) a **peaked radial
profile** with a thin hot core instead of the present flat slab (the body between the axis and the edge drops to a
tenth of the core, out of the AgX shoulder where no contrast survives); (2) **shock cells that carve dark gaps** into the
first 0.4 L (crest 1, gap 0.2 at the mouth) instead of a +-50 % cosine on a slab; (3) an **anisotropic streak field**
(4.5 : 1 along the axis, advected by the existing flow phase) that drives the turbulence, the edge erosion and the tail
break-up, so the plume reads as streaks moving away from the nozzle rather than a boiling cloud. With them: a colour
gradient core -> tint -> darker tint (never tint -> white), the heat confined to the core and the first 0.3 L, a tighter
halo, and the end-on disc with a hot centre, the nozzle ring and radial spokes. The peak radiance does not rise (the
model's axis peak falls to 0.67-0.80 of today's), the body energy falls to 0.37 of today's, the mouth gate (0.85) and the
far-dot law stay; the shader keeps its two fbm evaluations, so the slot count stays near 734 + 40..65 (inferred), under
the fixture's 800 gate.

## 1. What the images show

Capped bands (150 px nozzle shrunk to 130 px and faded to 0.5 by the chase cap) are judged for shape; 05, 09 and 10 are
uncapped and judged for brightness.

| File | Reads right | Reads wrong (measured) |
| --- | --- | --- |
| `01b_side_fighter_..._argon-blue` (s = 0 / 0.5 / 1) | Length law (67 / 156 / 279 px), the mouth no longer a crest, the eroded outline at s = 1 | At s = 1 the whole first half is a white slab: axis RGB (221, 232, 232) at u 0.05, (230, 241, 242) at u 0.2, (163, 220, 227) at u 0.5; whiteness 0.06 at u 0.1. The radial profile at u 0.2 is flat: 0.87 0.86 0.86 0.73 0.53 0.55 0.60 0.65 0.54 0.34 0.13 (every 4 px from the axis): core / edge 1.4 at 0.6 of the half-width. Shock cells: depth 0.11 in u 0.05..0.4 (the axis profile 0.70 0.78 0.82 0.75 0.78 0.85 0.92 0.89 0.80 ... has no dark gap). Streak anisotropy 2.0: blobs, not streaks. The tail fades uniformly from u 0.6 |
| `01a_side_fighter_..._split-red` | The tail (u > 0.5) is saturated red and reads as fire; the whole plume reads better than the cyan one | The head is grey-pink, not hot: the head colour (the peak at the mean's luminance) is (0.39, 0.32, 0.32), darker in red than the mean (1, 0.15, 0.15); then heat 0.7 whitens it. Cells 0.16, core / edge 2.3 at u 0.2, aniso 2.6 |
| `02a_45deg_...` | The disc and the axial quad hand over without a seam; the halo sits at the mouth | The foreshortened body is a flat white blob with a soft cyan or red fog around it; no structure survives the view angle |
| `02b_end-on_...` | The halo is round and dim (hb 0.20 reads restrained) | A flat white-centred disc: the radial luma is 0.98 0.98 0.97 0.95 0.93 0.90 0.84 0.61 0.26 0.12 (every 4 px), max off-centre / centre 1.00, i.e. no ring, no hot centre, a plateau 24 px wide then a cliff. Centre RGB (244, 255, 255) blue, (253, 241, 240) red: the tint is gone |
| `03a_side_capital_...` (t0 / +100 ms) | The capital's crawl (flow 0.3) is visible as nearly the same structure 100 ms apart; the plume is wider and calmer than the fighter's `03c` | Same slab: core / edge 2.0 at u 0.2, 1.2 at u 0.5; cells 0.13; the structure is the fighter's cloud scaled up |
| `03c_side_fighter_...` (t0 / +100 ms) | The flow moves the structure (the blobs shift between frames) | Cells 0.18 / 0.15, core / edge 1.6, aniso 2.5 / 2.8: the motion is of blobs, so it reads as a cloud drifting, not fire streaming |
| `04_presets_...` | Restrained / default / strong scale as intended (peak 0.750 / 0.910 / 0.989 display-decoded) | Restrained is the only band with visible cells (0.32) because its body sits lower on the AgX curve; strong clips (0.04 whiteness at u 0.1, core / edge 1.6, cells 0.09). The look gets flatter the brighter the preset |
| `05_distance_..._n40-12-6-2px` | The 12 / 6 / 2 px plumes read as sparks, the distance law is right | The uncapped 40 px plume is clipped white over 1,201 pixels (any channel >= 250); whiteness 0.02 at u 0.1, 0.11 at u 0.5. At this size the body shows no tint at all except a thin cyan rim |
| `06a_hull_head-on_...` / `06b_hull_20deg_...` | The spill through the plate (0.15) is a soft disc, right in kind | 06a: the spill is a smooth radial gradient, which with the flat disc behind it is the only soft thing in the set; 06b: the body emerging past the edge is a white block cut by the lane |
| `07_rcs_steering_...` | Short plumes with the ramp; the puff (right) is wider than steady (left) | Both are small white wedges; the 1.4x attack one frame after its peak shows as display peak 0.987 against 0.935 and 1.07x the energy: the attack is lost in the AgX shoulder, as the cells are. At 60 px the cells and erosion are below the pixel scale |
| `08_seta_warp6_...` | The travel look doubles the length (330 -> 572 px) with the same outline | Anisotropy rises to 6.5 only because the body stretched; whiteness 0.06 at the head; the long body is a uniform pale tube, exactly where X4's travel plume shows long striations |
| `09_ribbon_moving8px_...` | The ribbon trails the nozzle correctly | 1,738 clipped pixels in the plume; the ribbon is fine, the plume it hangs on is a white bar |
| `10_crowd_30_mixed` | The crowd reads as engines of many sizes and hues; far dots are dots | 3,346 clipped pixels; every plume over about 20 px is a white core with a coloured fringe; the discs (purple, white) are flat circles |

Against the references: X4's and Everspace 2's plumes have a thin core that may clip to white, a saturated body a
stop or two darker, visible longitudinal striations that move away from the nozzle, shock cells with dark gaps on the
first third, and an outer flame darker and more transparent than the core. Ours has the clipped core, but it is the
whole body: the ratio axis / 0.6 half-width is 1.4-2.9 on every capped band where the references' is 4 or more
(inferred from the references, not measured here).

## 2. Causes in the law

Term by term, `engine_plume_ps.hlsl` side view, `Look` defaults, s = 1, I = 4:

1. **The radial profile is a slab.** `body = edge x tail x cells x turbulence x (1 + 0.6 core_mask)`, with
   `edge = 1 - smoothstep(0.55, 1, radial + erosion)` and `core_mask = 1 - smoothstep(0, 0.63, radial)`. Between the
   axis and radial 0.55 the only variation is the core boost 1.6 -> 1.0; the edge then falls over 0.55..1.0. Linear
   core / edge at 0.6 of the half-width is 1.4-1.9 (model). Every other defect sits on this one: with the body at
   4..6.4 linear everywhere inside radial 0.55, AgX at EV 0 maps 2 / 4 / 8 linear to display 0.874 / 0.934 / 0.971, so
   a 2x linear contrast becomes 0.06 display, and the tint's hue is gone (AgX desaturates towards white above about 2).
   The references' body sits at 0.3..1.5 linear where AgX still has slope (0.25 -> 0.556, 1 -> 0.787).
2. **Heat whitens the whole core radius and the first half.** `heat = 0.7 x core_mask x (1 - smoothstep(0, 0.55, u))`
   mixes 70 % white over 63 % of the local width out to u 0.55: measured whiteness 0.05-0.09 at u 0.1 on every band.
3. **The head colour is the peak at the mean's luminance.** For clusters whose peak is much brighter than the mean (red
   2.56x, darkblue 3.57x, orange 2.11x; `plume_two_tone_colours_out.txt`) the scaling makes the head *darker* in its
   own hue than the mean: red (0.39, 0.32, 0.32) against the mean (1, 0.15, 0.15). The head of the red plume is a
   grey-pink, then whitened; the saturated red appears only past u 0.3 where the lerp returns to the mean. For cyan the
   head (0.21, 0.71, 0.79) is simply a greyer cyan.
4. **The turbulence is a multiplicative +-50 % blob field on the slab.** `1 + 1.32 (n2 - 0.5)` with `n2 = fbm(p x 2.2)`
   and `p = ((x - phase) 1.6, 3 y, ...)`: features 0.63 nozzle widths along by 0.33 across, aspect 1.9 (model 2.0,
   images 2.0-3.5). It modulates a flat slab, so it reads as cotton. The same amplitude on the axis and at the edge
   means the core boils as much as the sheath.
5. **The erosion only bites the outline.** `erosion = 0.55 (n1 - 0.5)` enters the edge smoothstep, so it moves the 0.55..1
   boundary by +-0.27 w with the same blob field: a wavy outline, not tongues; and it is constant along u, so the tail
   stays as smooth as the mouth.
6. **The shock cells are a +-0.5 cosine on the slab, masked to radial 0.8.** `1 + 0.5 cos(2 pi u / 0.16) exp(-3u) ...`:
   linear depth 0.46 on the axis in the model, but the turbulence scrambles it and AgX flattens it to 0.23 (model) and
   0.11-0.18 (images, with TAA and the pulse). There are no dark gaps: the trough is 0.69 of the mean at u 0.16 on a
   body already in the AgX shoulder.
7. **The tail fades uniformly.** `tail = (1 - smoothstep(0.4, 1, u)) exp(-0.84 u) x ramp`: no noise enters it, so the end
   is a smooth taper (01b, 08 bottom).
8. **The halo is a uniform veil along the body.** `exp(-d / 0.55 n) exp(-2.2 u)` at hb x I(s) / 4 = 0.2 linear on the
   axis at the mouth (the body there is 2), 0.05 at the body edge, reaching 1.24 n: a faint sheath around the first third. At hb 0.20 it is dim and is not the
   main cause of the wash, but it fills the dark gap between the body edge and the sky that the references keep.
9. **The end-on disc saturates its cap.** The integrated body (kappa 1.8 x L / n = 7.2 x I) is soft-capped at 1.5 x the
   side peak, so the whole disc inside a 24 px radius is at the cap: a plateau, then the edge. The nozzle ring at 0.53 n (47 px
   at the capped size) is under the plateau; the shock rings at 0.8 w_k likewise.
10. **Tonemap.** AgX at EV 0 is part of every number above, and bloom (not in the images) will add a wider veil in
    flight. Nothing in the proposal raises the peak; it lowers the body to where the curve has slope.

## 3. The revised law

Side view, nozzle widths across and u = x / L along as today; `S1 = fbm(p) - 0.44`, `S2 = fbm(2.2 p + o) - 0.44` the two
existing fbm evaluations on new coordinates. Constants the user chose in the lab and keeps (same meaning): bulge 1.15,
taper 0.45, tail 0.7 (the width profile is untouched), flow 3 and the world-unit flow, pulse 0.25, period 0.16, cfade
0.6, heat 0.7, hb 0.20, ring 0.3. Constants the user chose whose *mapping* changes (the knob keeps its number and its
direction): turb 0.6, erode 0.57, shock 0.5, core 0.45, halo 1.1. Derived constants are marked (d).

| Term | Today | Proposed | Why |
| --- | --- | --- | --- |
| Streak field `p` | `((x - phase) 1.6, 3 y, seed + 0.7 t)` | `((x - phase) 1.0, 4.5 y, seed + 0.7 t)` (d): features 1 n along, 0.22 n across, aspect 4.5; `S2` at 2.2x gives 0.45 x 0.1 n striations | Streaks advected by the same phase at the same speed; the brief's 4-8x stretch with a finer radial component. Model anisotropy 13 (images today 2.0-3.5). Cost 0 |
| Radial profile | `edge x (1 + 0.6 core_mask)`, flat to radial 0.55 | `profile = 0.08 + 0.92 exp(-(radial / 0.32)^2)` (d), `core = exp(-(radial / (0.45 core))^2)` = sigma 0.2 w (d, the `core` knob now sets the hot core's radius), `body x profile x (1 + 0.6 core)` | Core / edge 3.4 linear, 2.5-2.9 display-decoded at u 0.2 (model; today 1.4-1.9 / 1.2-1.4). The outer flame at 0.08-0.2 of the core sits at 0.3-0.8 linear where the tint shows. +4 slots (one exp, one mad) |
| Edge | `1 - smoothstep(0.55, 1, radial + 0.55 (n1 - 0.5))` | `1 - smoothstep(0.45, 1, radial + 0.91 S2 (0.6 + 0.8 u))` (d: erode x 1.6 x (0.6 + 0.8 u)) | Erosion from the fine striation field, growing along the plume: tongues lick the outline, the tail frays. +2 slots |
| Tail | `(1 - smoothstep(0.4, 1, u)) exp(-0.84 u) x ramp` | `(1 - smoothstep(0.4, 1, u + 0.35 S2)) exp(-0.84 u) x ramp` (d) | The end breaks into tongues +-0.35 L long instead of a uniform fade. +1 slot. The mouth ramp (dip 0.5 over 0.3 L) stays |
| Shock cells | `1 + 0.5 cos(2 pi u / 0.16) exp(-3u) ramp mask(0..0.8) s` | `c = (0.5 + 0.5 cos(2 pi u / 0.16))^3`; `a = 0.8 exp(-3u) smoothstep(0, 0.16, u) (1 - smoothstep(0.3, 0.9, radial)) s`; `cells = 1 - a (1 - c)` (d: gap depth 0.8 = min(1, 1.6 shock)) | Crests stay at 1, gaps fall to 0.2 at the mouth and to 0.76 by u 0.4: dark gaps between sharp cells across the core, fading with cfade as before. Nothing exceeds the body: the first crest is no brighter than today's mean. Body-lane cell depth 0.38 linear / 0.22 display-decoded (model; today 0.21 / 0.05). +4 slots (cube, mad) |
| Turbulence | `1 + 1.32 (n2 - 0.5)` everywhere | `1 + 1.32 S2 (0.4 + 0.6 min(radial, 1))` (d) | The core is steady (+-0.2), the sheath boils (+-0.5). +2 slots |
| Heat | `0.7 core_mask(0..0.63 w) (1 - smoothstep(0, 0.55, u))` | `0.7 core (1 - smoothstep(0.05, 0.3, u))` (d) | White only inside sigma 0.2 w and the first 0.3 L; the rest keeps its hue. 0 slots |
| Colour | `lerp(lerp(head, mean, ss(0.3, 1, u)), white, heat)` | `hot = lerp(head, body_tint, ss(0.2, 0.5, u))`; `body_tint = lerp(mean, 0.6 mean, ss(0.6, 1, u))`; `colour = lerp(hot, 0.5 mean, ss(0.25, 0.9, radial))` then `lerp(colour, white, heat)` | Core -> tint -> darker tint across, tint -> darker tint along: darker means more saturated after AgX. +6 slots (two lerps, one smoothstep). `head_colour`: the luminance scale k clamped to >= 0.75 (d) so the red head is (0.75, 0.61, 0.61), not (0.39, 0.32, 0.32); cyan unchanged (k 0.79) |
| Halo | `exp(-d / 0.55 n) exp(-2.2 u)`, reach 1.24 n | `exp(-d / 0.35 n) exp(-4 u)` (d: sigma = 0.32 halo n), reach 2.25 sigma = 0.79 n | A tight glow at the mouth (1/e by 0.25 L), the body edge meets dark sky. hb 0.20 kept. 0 slots; the quad narrows (CPU `halo_reach x sigma`), fewer pixels |
| Ring | unchanged | unchanged; if the mouth gate (0.85) fails against the thinner body lane, ring 0.3 -> 0.2 | |
| End-on disc | 8 samples of the slab law, cap 1.5 x side peak, planar noise | The same 8 samples of the new profile (hot centre sigma 0.2 w_k, body Gaussian, cells as rings 0.3..0.9 w_k); kappa re-derived by `plume_end_on_model.py` for the new profile (the side energy falls to 0.37, so kappa falls and the plateau goes); the noise in polar coordinates `(atan2(q.y, q.x) 2.5, 4.5 rho - 1.6 phase, seed + 0.7 t)` so the streaks are spokes scrolling outward (the flow along the line of sight) | A hot centre, the body glow, the nozzle ring at 0.53 n showing between body and halo, spokes instead of a flat plane. atan2 is about 20-25 slots inside the disc's dynamic branch (axial pixels do not pay) |
| Far-dot law | unchanged | unchanged | The dot's energy follows the body's (0.37): see Unknowns |

Slot estimate (inferred; the compile decides): 734 + about 20 axial + 25 disc = about 780, under the fixture's 800
gate and the brief's 900. The vertex program and the CPU builder change only in the halo reach and the head colour
clamp; the vertex stays 76 bytes. Hot-path cost: the stage's measured 0.26 ms at 5120x1440 for 300 nozzles (ledger)
scales with slots x pixels; +6 % slots against a halo quad 0.64x as wide, so no increase expected (inferred).

Native Windows: ps_3_0 with documented intrinsics only (`atan2`, `pow`, `exp`); the native compiler's slot count
differs from wined3d's (the shimmer program's 1,608 is a conservative count in the ledger), so the 800 gate is checked on both counts
when a Windows build is available ([platform-portability.md](platform-portability.md)).

TAA: the structure moves 2.625 n/s x 89 px = 3.9 px per frame at 60 fps on the capped fighter; the resolve's 3x3 clip
keeps moving high-contrast structure and the images are taken through it, so the gate sees the resolved result. The
across striations (0.22 n = 20 px capped, 2.7 px at the 12 px far plume) are above the pixel scale wherever the far law
has not already dimmed the plume.

### Energy and brightness

Model, s = 1, one frame: proposed / current axis peak 0.67 (cyan) and 0.80 (red); body energy 0.37; luma energy 0.34
(cyan) and 0.43 (red). The plume gets darker overall, which is the user's restraint and the brief's "no overall
brightening"; what rises is contrast. The presets (0.6 / 1 / 1.5) keep scaling I_core, I_halo and sigma; `strong`
remains the way to a brighter plume. The near cap's fade and the disc's chase floor are unchanged.

### The lab

`tools/effects/engine_exhaust_lab.html` `plume()` takes the same edits (it is the port's reference): the coordinates
of `np`, `profile`, `core`, the carving `cells`, the weighted `turbI`, `eros` from `n2` growing with u, the tail's
`+ 0.35 S2`, the three-stop colour, the heat window, the halo's sigma and falloff, and `head` clamped at 0.75. The user
should see, side-on at s = 1 and nozzle 0.5: a thin white core 0.2 of the width, cyan around it, a darker blue rim; three
to four dark gaps in the first third moving with the flow; streaks running the length of the plume; a frayed tail with
tongues; the halo visible only at the mouth. At s = 0: one nozzle width of saturated tint with a small white core, no
cells. The split-red version: white core, pink head, red body, dark red rim. Add an end-on mode (the game-only disc is
documented as absent today; a lab version of the 8-sample integral with the polar spokes is about 30 lines of GLSL) so
the chase-view look is confirmed before a flight. Keep a "current law" toggle so the user compares A/B in the browser.

## 4. Mock-up acceptance

Before a flight the user confirms in the lab, with the sliders at the constants above: (a) the plume reads as fire with
depth, not fog, at nozzle 0.5 and s = 1; (b) the mouth is not brighter than the body at any throttle (the ramp stays);
(c) the halo reads as a mouth glow, not a sheath; (d) the end-on mode shows a hot centre, a ring and spokes; (e) the
red and the cyan tints both keep their hue outside the core. The lab's `Copy settings` line is the brief for the port.

## 5. Fixture gate for structure

A `structure` case in `run_engine_plumes.py`, on the `--dump-images` bands (through the resolve, AgX as today) and on
the FP16 readback of the same frames, side view, fighter 150 px capped and 40 px uncapped, s = 1, cyan and red, three
frames 100 ms apart (the pulse and the flow move the structure). Thresholds (the model's numbers are the design
targets; the images today are the failing baseline):

| Measure | Definition | Gate | Today (images) | Proposal (model) |
| --- | --- | --- | --- | --- |
| Radial contrast | axis luma / luma at 0.6 of the 10 % half-width, u 0.2; linear FP16 and display-decoded | >= 3.0 linear, >= 2.0 display | 1.4-2.9 display | 3.4 / 2.5-2.9 |
| Cell depth, body lane | (max - min) / (max + min) of the detrended luma along the row 0.35 w off the axis, u 0.05..0.4 | >= 0.35 linear, >= 0.2 display | 0.05-0.07 display (model of today) | 0.38 / 0.22 |
| Dark gaps | the minimum of the same lane in u 0.1..0.3 / its mean | <= 0.6 linear | 0.7-0.9 (inferred from the term and the axis rows) | 0.2-0.5 (inferred from the term) |
| Anisotropy | high-passed body luma (sigma 10 px), autocorrelation half-length along / across, u 0.1..0.8, within 0.6 of the half-width | >= 3 | 2.0-3.5 | 13 |
| Whiteness | 1 - min / max channel on the axis at u 0.5 and at radial 0.6, u 0.2 | axis u 0.5 >= 0.15; rim >= 0.5 | 0.11-0.44 / not measured | 0.20-0.25 / inferred >= 0.6 |
| Mouth | existing gate | <= 0.85 | 0.60-0.77 | to measure |
| Far dot | 2 / 6 px dot luma in `05` | within 0.7..1.3 of today | 1.0 | unknown |
| Disc | 02b radial profile: off-centre maximum / centre in 0.4..0.7 n (the ring) | >= 1.05 and centre > 0.9 of the peak | 1.00 | to measure |

The brief's 0.4 cell depth is reachable in linear FP16 (0.38 in the model) but not in display-decoded luma under AgX
at EV 0 unless the gaps go black; the display gate is set at 0.2 and the eye decides in flight. `plume_look_metrics.py`
already computes the radial, cell, whiteness and anisotropy columns on the dump images; the fixture case adds the FP16
columns and the three-frame repeat.

## 6. Implemented (2026-10-03, worktree build on e51872be, not a candidate)

Code: `src/effects/engine_plume_ps.hlsl` (797 slots by the production counter, gate 800; one fbm evaluation instead of
two), `engine_plume_vs.hlsl` (the tint passed as float4), `src/proxy/engine_plumes_core.h` (`Look`, `look_tables`,
`pixel_constants` c3..c18, the builder), the lab (`tools/effects/engine_exhaust_lab.html`: both laws, an A/B toggle, the
end-on disc). Models: `plume_end_on_model.py` (kappa, halo, the 02b ring), `plume_look_proposal_model.py` (the
"implemented" rows), `plume_slab_disc_fit.py`. Ledger: [engine-effects.md](../verification/engine-effects.md), "Revised
look law".

| Term | As built | Against section 3 |
| --- | --- | --- |
| Streaks | `S2 = fbm(2.2 p + (5, 2, 1)) - 0.4375`, `p = (x - phase, 4.5 y, seed + 0.7 t)` | As written. `S1` enters none of the table's terms, so the shader evaluates one fbm |
| Profile, core | `0.08 + 0.92 exp(-(radial / 0.32)^2)`, since the tuning pass plus the side view's outer sheath `4 outer m (1 - m)` (below); hot core `exp(-(radial / (0.45 core))^2)`, `x (1 + 0.6 hot (1 - 0.5 smoothstep(0.2, 0.8, u)))` | The core's boost cools to half along the plume. Without it the cyan axis at u 0.5 on the uncapped 40 px plume read whiteness 0.137-0.149 through the fixture's AgX (gate 0.15) |
| Edge, tongues | `1 - smoothstep(0.45, 1, radial + 1.6 erode S2 (0.6 + 0.8 u))`; tail on `u + 0.614 erode S2` (0.35 at erode 0.57) | Tongues tied to `erode`, so the still look (erode 0) has none |
| Cells | `1 - a (1 - c)`, `c = (0.5 + 0.5 cos(2 pi u / period))^3`, `a = min(1, 1.7 shock) e^(-5 cfade u) smoothstep(0, period / 2, u) (1 - smoothstep(0.3, 0.9, radial)) s`: gap 0.85, first gap 0.33 of the crest, 0.74 by u 0.4 | Gap 0.85 instead of 0.8 and a half-period ramp. The written term measured lane depth 0.30-0.40 and gaps 0.69-0.78 (gates 0.35 and 0.6), because the full-period ramp and the fade leave the gap at u 0.24 at 0.61 of the crest. Gap 1.0 lets the carved white core dominate a red plume's high-passed luma (anisotropy 1.8-2.3) |
| Turbulence, heat, colour | as written; `head_colour` scale at least 0.75 | As written |
| Halo | e-fold `0.32 halo`, `exp(-4 u)`, reach 2.25 sigma | As written |
| Disc | kappa 3.33 (4.13 since the tuning pass), halo gain 2.82, cap 1.5 x the side axis peak (per I: 1.20 revised, 1.46 previous), ring x 3 (`Look::disc_ring`; twice the side's sigma since the tuning pass); polar streaks with the direction on a circle of radius 2.5 in the noise and `4.5 rho - 1.6 phase` radially | kappa rises (1.8 -> 3.33) because the peaked profile's integral is smaller; the end-on/side energy stays 0.91 at s 1, L/n 4 (model). Halo 2.82 keeps the slab law's disc/side halo ratio (6.3). The ring needs 3x end-on to show between the integrated outer flame and the halo (model ring 1.29 cyan / 1.72 red at x3, 1.00 at x1). The circle replaces `atan2`, whose cut at +-pi would seam |
| Detail level (new) | `smoothstep(16, 40, drawn nozzle px)` in the tint's alpha; under 1 the body, cells and halo blend to the previous law's (slab edge 0.55..1 and core, cosine cells on the carving's envelope, e-fold 0.5 halo, `exp(-2.2 u)`, disc halo gain 3, cap on the previous peak, the disc's slab a fitted profile); streaks, tongues and the rim and tail darkening scale with it; turbulence stays | Not in the design. The resolve case (a 15 px nozzle moving 4 and 8 px a frame along its axis) kept 0.76-0.83 of its core with the revised law, 0.81-0.86 with streaks and cells off (the peaked profile alone), against the 0.9 gate; with the detail level the minimum is 0.957. It also settles the far-dot Unknown: `far_low` cannot (the 6 px weight 0.45 can rise at most to 1, which gives 0.67 of today on the 0.30 energy), so `far_low` stays 0.15 |
| Ring (side), far law, chase cap | unchanged (ring 0.3, `far_low` 0.15 at 2 x 4 px, cap and fade) | The mouth gate holds (0.65 / 0.67 / 0.76 at s 1 / 0.5 / 0) |

Gates, measured (bottle X3, `run_engine_plumes.py` PASS 236/236; display figures from `plume_look_metrics.py` on the
dumped images, before = the images at e51872be, `plume_look_metrics_before_out.txt`):

| Gate | Result |
| --- | --- |
| Radial contrast, FP16, u 0.2 (>= 3.0) | 3.02-4.49 over 24 frames (both sizes, cyan and red, 150 px capped and 40 px) |
| Body-lane cells, FP16 (>= 0.35); dark gaps (<= 0.6) | 0.43-0.57; 0.45-0.56 |
| Anisotropy, FP16 (>= 3) | 3.67-17.3 (red 40 px frame 0 the lowest) |
| Whiteness, display of the FP16 frame (axis u 0.5 >= 0.15; rim >= 0.5) | 0.157-0.53; 0.71-0.96 |
| Disc ring (>= 1.05) and hot centre (>= 0.9) | FP16 1.62 / 2.37 and 1.00; 02b image 1.80 / 2.47 and 1.00 (before 1.00 / 1.00) |
| Far dots, 05 image (0.7..1.3 of before) | 2 px 1.08, 6 px 1.01 (12 px 1.02; the 40 px plume 0.33, the revised law's restraint) |
| Mouth (<= 0.85 body) | 0.65 / 0.67 / 0.76 |
| Radial contrast, display (>= 2.0) | 1.77-2.47 on the s 1 bands, five of 13 at 1.77-1.99 (before 1.20-2.27, nine under 2.0) |
| Lane cells, display (>= 0.2) | 0.77-0.95 on the capped bands; 09 (40 px moving 8 px a frame) 0.16 (before 0.05-0.26) |
| Energy (model, detail 1, s 1) | body 0.33 of the slab law's, luma 0.31 cyan / 0.39 red, axis peak 0.67 / 0.80 |

The display radial contrast stays under its 2.0 on five of the capped bands (03c t0, the three presets, 08 normal;
1.77-1.99), through the resolve, the chase fade and AgX. The FP16 gate holds on the fixture's frames of the same setup.
Whether that reads as fire in flight is the eye's call, as section 5 anticipated for the display figures.

### Tuning pass (2026-10-03, worktree build on c5e04764, not a candidate)

Judged on the images above: the structure was right, two things overshot. The peaked profile left the outer flame
invisible (01b / 03a read as a needle about a third of the slab law's width), and the end-on ring read as a drawn
outline over a dark annulus (02a, 02b, 03b).

| Term | As built | Why |
| --- | --- | --- |
| Outer sheath (`Look::outer` 0.32, c18.y = 4 outer) | profile `0.08 + 0.92 exp(-(radial / 0.32)^2) + 4 outer m (1 - m)`, `m = smoothstep(0.25, 0.9, radial)` (the colour's darker-tint window): 0.32 at radial 0.575, 0.06 of it at the lane's 0.35, 0 on the axis; inside the eroded edge, carved and streaked like the body; side view only | A uniform floor (0.08 -> 0.15..0.45) widens the plume but the cells then band the floor in the anisotropy window: red anisotropy 3.0 at 0.15, 1.9-2.6 at 0.38 (gate 3). A sheath on the rim window alone (`outer m`) plateaus to the edge, so the 10 % width jumps from 0.5 to 0.85 of the slab law's between weights 0.5 and 1.0; `m (1 - m)` falls off before the edge and grades it |
| Disc kappa 4.13 | the disc's 8 samples keep the peaked profile without the sheath; kappa re-derived from the side energy (`plume_end_on_model.py`, law "tuned"); the detail-0 slab fit 0.448 x 1.8 / 4.13 = 0.1953 | The sheath in the disc's samples (floor 0.45, kappa 2.23) fills 0.3-0.6 n and buries the ring at any intensity (02b ring 1.00 / 1.00) |
| End-on ring (`Look::disc_ring_width` 2, `disc_ring` 3) | the Gaussian at twice the side's sigma (0.129 n), the end-on peak unchanged at x 3 (energy x 2); c18.z = 1 / width; the disc quad's half reaches 0.46 bulge + 3 x 0.129 | The written x 0.6 (x 1.8) at width x 2 leaves no bump on the cyan disc: FP16 gate 1.00 at 1920 and 5120, the halo at 0.53 n is as bright as the ring. On the resolved 02b image cyan needs x 3 at width 2 (x 2.7: 1.00; x 3: 1.11). A wider inner side (the Gaussian x 1.15-1.5 again inside the radius) drops cyan under 1.05 too; the annulus fills from the doubled width and the brighter disc body instead |

Measured (bottle X3; `run_engine_plumes.py` PASS 236/236, 800 slots; images re-dumped; before = c5e04764's images and
records, slab = e51872be's images; `plume_look_width_ratio.py` -> `plume_look_width_ratio_out.txt`):

| Gate | Before (c5e04764) | After |
| --- | --- | --- |
| 10 % half-width u 0.2 / the slab law's, s 1 capped bands (display) | 0.39-0.50 | 0.61-0.71 (01b 0.71, 03a 0.61 / 0.64, 03c 0.61 / 0.65, 04 default 0.64, 08 0.64 / 0.65); restrained preset 0.45; s 0.5 0.45 / 0.57, s 0 0.47 / 0.58 |
| Radial contrast, FP16, u 0.2 (>= 3.0) | 3.02-4.49 | 3.76-4.65 (0.6 of the wider half-width lands outside the sheath's peak) |
| Lane cells; dark gaps; anisotropy, FP16 | 0.43-0.57; 0.45-0.56; 3.67-17.3 | 0.40-0.57; 0.47-0.56; 7.5-17.3 |
| Whiteness axis u 0.5; rim (FP16 frame, display) | 0.157-0.53; 0.71-0.96 | 0.157-0.53; 0.53-0.88 (red 40 px the lowest: the sheath's red rim desaturates under AgX; outer 0.4 measured 0.48) |
| Disc ring FP16 cyan / red (>= 1.05); hot centre | 1.62 / 2.37-2.38; 1.00 | 1.14-1.17 / 1.43-1.48; 1.00 |
| Disc ring, 02b image cyan / red | 1.80 / 2.47 | 1.11 / 1.46; trough (min 0.2 n..ring, 4 px samples) 0.17 / 0.11 against 0.11 / 0.07 |
| Radial contrast, display, s 1 bands (>= 2.0) | 1.77-2.47, five under 2.0 | 2.46-6.65, none under 2.0 (09 moving 1.89, before 2.47) |
| Far dots 2 / 6 px of the slab law | 1.08 / 1.01 | 1.08 / 1.01 (the detail level keeps them); 40 px 0.40 (0.33) |
| Mouth; core survival; end-on 60 / 30 / 0 deg | 0.65 / 0.67 / 0.76; 0.957; 0.87-0.88 / 0.91 / 0.81 | unchanged; 0.957; 0.88-0.89 / 0.92 / 0.81 |
| Energy, model (detail 1, s 1, of the slab law) | body 0.33, luma 0.31 / 0.39, axis peak 0.67 / 0.80 | body 0.41 (`plume_look_proposal_model.py`; side 0.40 in `plume_end_on_model.py`), luma 0.37 / 0.45, axis peak unchanged; end-on / side energy 0.78 at L / n 4 (0.91: the brighter disc body meets the soft cap) [model; the fixture's end-on rows above are a 20 px nozzle, detail 0.074, the slab law: corrected from detail-1 measurements in "One law"] |
| Slots | 797 | 800 (the end-on ring's inner-side select dropped to make room) |

### One law: the review fixes (2026-10-04, worktree build on 8eb0514b, not a candidate)

The review of the tuning pass found that the revised law's lower energy dims a plume per pixel as it grows through the
detail blend (16 -> 40 px), that the end-on gate and the spill taper ran where the slab law draws, and that the own
ship's from-behind look lost most of its light. Three designs followed, each measured; the user's decision replaced the
blend with one law. Ledger: [engine-effects.md](../verification/engine-effects.md), "Revised law review fixes: one law".
Models: `plume_energy_compensation.py`, `plume_outer_flame_model.py`, `plume_one_law_model.py` (with their `_out.txt`).

1. A uniform gain g(s) = 1.91 + 0.45 s on the revised body met the energy gates (40 / 12 px per px^2 0.828 / 0.850,
   end-on 0.922 of the slab law) and failed the hue gate: the 40 px cyan axis at u 0.5 rose from 3.31 to 7.45 linear,
   whiteness 0.157 -> 0.024 (gate 0.15), the rims of the capped bands 0.43-0.49 (gate 0.5) [measured]. The coordinator
   kept the hue gate.
2. Moving the compensation into the outer flame (gain on radial > 0.25..0.3, the axis gain <= 1.3, a wider sheath):
   with the axis brightest and no bright band (a monotone profile, the hue, rim and contrast gates, the half-width
   0.8..1.0 of the slab law's) the energy at 40 px tops out at 0.58..0.61 of the slab law's; relaxing the rim gate to
   0.4 or letting the outer flame start late along the plume moves it to 0.61 [model, `plume_outer_flame_model_out.txt`].
   The contrast gate (the axis 3x the luma at 0.6 of the half-width) and the hue gate together bound it: the slab law is
   flat-topped, so its energy in a profile with a thin bright core needs a fat flame at core brightness, which AgX whitens.
3. The user's design change: one law, no blend and no gain, parameterised by the detail level.

The law as built (`src/effects/engine_plume_ps.hlsl`, `engine_plumes_core.h` `Look`):

| Term | As built |
| --- | --- |
| Detail level d | smoothstep(8, 40, drawn nozzle px) (was 16..40), in the tint's alpha |
| Core radius | the peaked profile's 0.32 and the hot core's 0.45 core x k(d) = 3.74 + (1 - 3.74) d (`Look::core_widen`, c18.x): at d 0 a smooth soft profile of the slab law's side energy (0.99..1.00 at s 0 / 0.5 / 1) and width (0.98) [model]; the edge 1 - smoothstep(0.55 - 0.1 d, 1, radial) |
| Structure x d | the cells' depth, the streak turbulence and erosion, the tongues (on max(S2, 0): the fade complete at the tip), the rim and tail darkening; the head colour from the mean tint at 0 |
| Outer sheath x d | 4 x 1.25 m (1 - m), m = smoothstep(0.1, 1.2, radial), inside its own edge 1 - smoothstep(0.65, 1.2, radial + erosion), carved by the cells, growing 0.6 + 0.4 smoothstep(0.1, 0.5, u), coloured 0.5 x tint^2 (scaled to the tint's largest channel) x the tail's darkening: the fat saturated flame around the thin hot core, no gain (refined in `plume_outer_flame_model.py`, NOGAIN) |
| End-on disc | the samples at the core's radius and d, plus the sheath as the annulus x 1.6 (`Look::disc_sheath`, c18.w) in its deep tint; kappa 1.84 (d 0, the slab law's 1.8 recovered) -> 3.0 (d 1) on the CPU (energy-matched 1.47 with the sheath; past 3.0 the soft cap saturates the centre); the ring x 1 + (max(1, 8 min(1, (L / n) / 2)) - 1) d at the side's width (the sheath fills what read as a dark annulus; the doubled width left no bump on the cyan disc); the cap 1.5 x the side's axis peak from a table by throttle and detail level (the cells x d enter the peak non-linearly); the slab-fit disc removed |
| Constants | c18 = (core_widen, 4 outer, 1 / disc_ring_width, disc_sheath); c19 and the gain removed; the slab branch removed |
| Unchanged | the halo's e-fold and fall by d, the far law (`far_low` 0.15 at 2..12 px), the 2 x 4 px floor, the mouth ramp, the side ring, the spill |

Measured (bottle X3; `run_engine_plumes.py` PASS 286/286; before = 8eb0514b with the new cases; images re-dumped, display
figures before -> after in `plume_look_metrics_compare_out.txt`):

| Gate | Before (8eb0514b) | After (one law) |
| --- | --- | --- |
| Side energy per px^2, 40 over 12 px (0.5..1.0; first 0.8), s 1 / 0.5 | 0.364 / 0.414 | 0.533 / 0.589; per px^2 over 12 / 20 / 28 / 34 / 40 / 60 px at s 1: 1.00 / 0.87 / 0.71 / 0.59 / 0.53 / 0.53; totals rise at both throttles |
| End-on total at d 1 over the d-0 law's at the same size (0.7..0.9, the user's relaxation) | 0.416 | 0.740 |
| End-on / side (0.7..1.5): 40 / 48 px, 60 / 30 / 0 deg; 20 px (d 0.32) | 0.98-1.01 / 1.04-1.06 / 0.91; 0.88 / 0.92 / 0.81 | 1.09-1.12 / 1.20-1.22 / 1.02; 0.87 / 0.84 / 0.71 |
| Nozzle 1.0 (40 px, L / n 2): end-on / side, without the ring, the ring's share | 1.375, 0.930, 0.33 | 1.74, 1.33, 0.24 (reported) |
| Own ship from behind over d 0: total / peak / extent, 1080 (34 px) and 1440 (45 px) | 0.476 / 0.865 / 0.731, 0.407 / 0.834 / 0.708 | 0.702 / 0.971 / 0.652, 0.695 / 0.972 / 0.648 |
| Core survival (>= 0.9): 15 px moving 4 / 8 px a frame; 40 px | 0.957-1.000; 0.979-1.011 | 0.984-0.997; 0.978-1.010; trail 0 on the dark sky |
| Hue: axis u 0.5 (>= 0.15); rim (>= 0.5) | 0.157-0.53; 0.53-0.88 | 0.169-0.551; 0.525-0.866 |
| Radial; lane; gaps; anisotropy (FP16) | 3.76-4.65; 0.40-0.57; 0.47-0.56; 7.5-17.3 | 3.62-6.65; 0.40-0.57; 0.45-0.54; 5.3-13.0 |
| Disc ring FP16 cyan / red (>= 1.05); hot centre | 1.14-1.17 / 1.43-1.48; 1.00 | 1.21 / 1.76; 1.00 |
| Mouth (<= 0.85) at s 1 / 0.5 / 0 | 0.65 / 0.67 / 0.76 | 0.69 / 0.70 / 0.76 |
| Spill 12 px: band median; taper mean / max | 0.1501; 0.0788 / 0.1456 | 0.1502; 0.0788 / 0.1457 (d 0.043, the window to 1.22 n) |
| Display (images): radial u 0.2 on the s 1 capped bands; body whiteness u 0.5; 10 % half-width / slab law's | 4.3-6.7; 0.36-0.38; 0.61-0.71 | 5.4-8.4; 0.35-0.38; 0.78-0.95 (the restrained preset 0.52) |
| Display: 02b ring cyan / red (>= 1.05); far dots 2 / 6 px of the slab law's (0.7..1.3); the 40 px plume | 1.11 / 1.46; 1.08 / 1.01; 0.40 | 1.04 FAIL / 1.38; 1.21 / 1.07; 0.55 |
| Slots (shared + disc + axial) | 800 (190 + 398 + 212) | 921 (195 + 496 + 230), advisory 1,024: the disc's annulus and the sheath add, the slab branch's removal does not pay for them |
| CPU build 30 / 100 / 1,024 records | 3.40 / 10.94 / 116.3 us | 3.75 / 12.09 / 128.3 us (the cap's lookup only for a drawn disc: 145.8 before that) |

The 40 / 12 px energy gate: 12 px is d 0.043 (the slab law's energy), 40 px is d 1, the thin-core law, whose energy the
hue and contrast gates bound near 0.6 of the slab law's (item 2); the single law measures 0.533 / 0.589, the model
0.532 / 0.583. Decision (the coordinator, 2026-10-04): the gate is 0.5..1.0 per px^2, and the total energy rising with
the size is the hard gate. The structured law carries about 0.55..0.6 of the slab law's energy per px^2, and the hue
and contrast gates win over the first 0.8, which was an orchestrator estimate, not a user requirement. Energy per px^2
falls smoothly from 12 to 40 px (no step) and the total rises with the size.

### Mouth whiteness (after flight F, 2026-10-04, worktree build on c256d9c2, not a candidate)

Flight F (Run 123 A, run409): the mouth read too white on the default preset and the user preferred the restrained
one, which is only I x 0.6 everywhere. Constants (`engine_plumes_core.h`, `Look` and `head_min`; no shader change):

| Constant | Before | After | Why |
| --- | --- | --- | --- |
| `heat` | 0.7 | 0.1 | the white lerp in the core's first 0.3 L; the core keeps its x 1.6 radiance, tinted |
| `head_min` | 0.75 | 0.42 | the red cluster's head (1, 0.81, 0.81) at 0.75 is 1.9x the mean's luminance and near white; its natural scale is 0.389 |
| `core_low`, `core_high` | 1.2, 4.0 | 1.248, 4.16 | I(s) x 1.04 returns the luminance the heat's white took; the mouth terms follow I / core_high and do not rise |
| `ring`, `mouth_dip`, `mouth_ramp` | 0.3, 0.5, 0.3 | unchanged | a dip of 0.6 over 0.4 L cost the far dots 13 % (it acts at every detail level) and the 0.2 ring failed the end-on ring gate (1.00) |

Metric (`verification/results/engine-effects/plume_mouth_whiteness.py`, `_out.txt`): the fixture's resolved FP16
frames before the tonemap (`run_engine_plumes.py --dump-images DIR --dump-linear [--preset restrained]`), the body
pixels within 0.3 L of the nozzle (the default-before mask, luma >= 0.25, applied to every state) and the end-on
disc's centre (0.3 n). Fraction with min(rgb) >= 1.0 (clipped white), default before / restrained before / default
after, s 1 side views: 01a red 0.119 / 0.001 / 0.000; 01b blue 0.026 / 0 / 0; 03a 0.036, 0.041 / 0 / 0; 03c 0.024,
0.032 / 0 / 0; 04 default band 0.025 / (0 in its restrained band) / 0; 05 at 40 px 0.102 / 0.034 / 0.006; 08 normal
0.036 / 0 / 0, warp 6 0.078 / 0 / 0; 09 at 40 px 0.115 / 0.019 / 0.011. Mean min / max on the s 1 bands 0.20-0.73 ->
0.15-0.66 (restrained 0.20-0.74: the restrained preset dims, it does not colour). Not met: the oblique and end-on red
(02a 45 deg 0.042 / 0 / 0.004; 02b centre 0.153 / 0 / 0.046), where the red head still clips. Radiance kept (summed
luma over default before): the whole plume 1.00-1.01 (blue; the red cluster 0.78-0.79, its head), the body past the
mouth 1.02-1.04, the far dots at 12 / 6 / 2 px 0.971 (restrained 0.58-0.60). Fixture: 40 / 12 px energy per px^2
0.533 -> 0.533 (s 1), 0.589 -> 0.590 (s 0.5); end-on over the side at 40 px 0.713 -> 0.706 (gate 0.7), at d 1
1.020 -> 1.013, over the slab law 0.740 -> 0.744; the 40 px cyan axis whiteness 0.169 -> 0.161 (gate 0.15), the
mouth over the body 0.694 -> 0.692 (gate 0.85), the end-on ring cyan 1.205 -> 1.183. No gate floor moved.

### End-on brightness (after flight G, 2026-10-04, worktree build on 3ee84cf3, not a candidate)

Flight G (Run 124, run412; screenshot `screenshots/engines3.png`): the own Split Scorpion seen from behind in chase
view at full throttle showed a clipped pink-white disc wider than its nozzle; the Ocelot's nozzles at about 6 km
read as pink-white dots. The side-view mouth change of flight F did not reach the end-on view.

Flight measurement (`verification/results/run412-engine-disc/run412_engine_disc.py`, `_out.txt`; the F8 HDR
captures in engine space, the tonemapper's input, and the depth captures; both frame sets at the default preset,
both own nozzles capped and faded, `capped=2 faded=2`). The AgX write-back shows a grey engine value white
(>= 0.98) from 2.11 at the second set's exposure (EV +0.65..+0.99) and 5.44 at the first's (EV -2.0..-2.1).

| | Own ship, frames 5437-5444 (EV +0.7..+1.0) | Own ship, 4480-4487 (EV -2.0) | Ocelot pair (z 7,300-11,300 units), 5437-5444 |
| --- | --- | --- | --- |
| Peak R / G / B (engine) | 19.0-20.3 / 15.5-16.8 / 15.5-16.8 | 15.5-18.3 / 13.5-16.3 / 13.5-16.2 | 3.7-12.5 / 3.9-11.1 / 2.7-4.1 |
| min(rgb) >= 1.0 | 2,581-2,696 px (59 px eq. diameter; the lit hull included) | 638-661 px (29 px) | 8-18 px (3-5 px) |
| AgX white | 386-522 px (22-26 px) | 17-45 px (5-8 px) | 3-11 px (2-4 px) |
| AgX R channel saturated | 6,622-7,693 px (92-99 px) | 1,054-1,225 px (37-40 px) | 3-23 px |
| R >= 1.0 | 7,893-8,511 px, 1,063-1,677 over sky | 7,176-7,419 px, 780-1,004 over sky | 12-29 px |
| Along the row through the main nozzle | R saturated 68-69 px, white 13-14 px; centre 10.5-11.3 / 6.1-6.3 / 6.1-6.3 | R saturated 13-14 px, white 0; centre 7.5-7.7 / 5.2-5.3 | not readable |

The main nozzle's opening in the depth capture (the recessed plateau under the projected centre) is 19-20 x 24-25 px
(the small nozzle 41 px above it 9-10 x 7-8 px). So the pink disc at EV +1 is the red-saturated area, 68-69 px across
along the row (3.5x the opening's width), and the white core is a column 13-14 px wide, inside the opening. The flight's
disc centre is about 3.5x the fixture's 02b red centre in R and 6x in G and B (inferred from the two tables:
10.5-11.3 / 6.1-6.3 against 3.25 / 1.07), so the flight disc stays above the AgX white point at EV +1 after this change
(about 0.67 of 6.2 is 4.1, against 2.11; inferred). Part of the difference is the second glow layer of the same
nozzle, drawn as a second plume and disc ("Co-located layers" below). The rest is not explained: the hull light lies
under the disc, and 15 non-jet engine draws at frame 5437 (`not_jet=15`) are forwarded natively.

Law: the near-camera fade took the disc to max(the body's fade, `chase_disc_floor` 0.6). Since the body's fade ends
at 0.5, any floor under 0.5 changes nothing (floor 0.4 alone: 02b energy 0.836, measured), so the floor is now the
disc's own fade target: the disc fades 1 -> `chase_disc_floor` over the same last 20 % before the cap
(`build_nozzle`: `disc_level = disc_weight x near_disc / near_weight`, `i_core` and `i_halo` carrying `near_weight`).
`chase_disc_floor` 0.6 -> 0.4. A disc under the band (every far, side or unfaded end-on disc) is unchanged. No shader
change (921 slots). `disc_cap` stays 1.5: at 1.0 it reached the clip target too (02b red 0, energy 0.68), but it dims
every unfaded disc. The fixture's end-on case at 40 px went 0.706 -> 0.565 of the side view, the detail-0 ratio
0.744 -> 0.677, the end-on rim's variation 0.032 -> 0.027 (`end_on_alive`, gate 0.03), and the head-on open peak at
1920 4.375 -> 2.46. `disc_kappa`, `disc_ring`, the head colour, the side-view mouth constants, `far_low`, the floor and
the presets are unchanged. The lab (`tools/effects/engine_exhaust_lab.html`) draws the unfaded disc and has no chase
fade, so it needs no change.

Fixture (`plume_disc_clip.py`, `_out.txt`; the resolved FP16 02b image, both 150 px nozzles capped and faded to
65.6 px, the own ship's chase regime), before -> after:

| 02b disc | centre (0.3 n) min(rgb) >= 1 | peak R / G / B | disc (4 n) min(rgb) >= 1 | tint channel >= 1 | energy |
| --- | --- | --- | --- | --- | --- |
| red | 0.046 -> 0.000 | 3.25 / 1.07 / 1.07 -> 2.17 / 0.71 / 0.72 | 56 px -> 0 | 4,491 -> 3,450 px | 0.671 |
| blue | 0.000 -> 0.000 | 0.88 / 3.09 / 3.93 -> 0.64 / 2.06 / 2.62 | 0 -> 0 | 4,447 -> 3,557 px | 0.670 |

Side views 01a, 01b, 03a, 03c, 04, 08, the distance dots 05, the RCS 07 and the ribbon 09 are bit-identical (the
largest per-pixel change is 0.0000). The frames with a near-camera disc change: 02a at 45 degrees 0.869 of its total, 03b at 30 degrees
0.864, 06b 0.988, 06a 0.999, the crowd 10 0.999. In the gated fixture, the hot centre (1.0) and the ring stay at
cyan 1.183 / red 1.726 (1.183 / 1.725 before; the ring is a ratio within the disc, which scales as a whole).
The 150 px disc's centre luma goes red 1.508 -> 1.006 and cyan 2.641 -> 1.761. The chase case's disc and drawn peak
are 0.400 of the unfaded (0.600 before). The end-on 40 px cases are unfaded and unchanged: 0.706 of the side, 1.013
at d 1, 0.744 over the slab. The own-ship look case (not faded) is unchanged at 0.706 / 0.698.

Gates: `headon_core_hidden` (1920) read the open peak against 0.9 x I. Its 192 px head-on value is past the cap at
1920, so the open peak follows the disc's fade, 4.375 -> 2.916 = 0.70 x I. The floor is now 0.6 x I; the cut
(inside <= 1e-3) is unchanged, and 5120 is in the band, 7.043 -> 6.992. `chase_disc_not_dim` now holds the disc at
`chase_disc_floor` within 1e-3 (CPU) and 0.01 (drawn), not only above it. The end-on gates (end-on over the side
0.7, over the slab 0.7..0.9, the ring 1.05) keep their floors: the law does not touch an unfaded disc, and the
user's earlier acceptance of 0.7-0.8 of the old end-on level is superseded only for the near-camera disc.

Co-located layers. At frame 5437 the own ship has two engine records, `fx_engine_xtc_red_nor` (size 10, origin
96411.5, -37509.1, 19210) and `fx_engine_xtc_red_tiny` (5.04; 96405.8, -37505.3, 19209). They share an axis and lie
6.9 units apart, so they are the two glow layers of one nozzle. The floor raised both (23.5, 20.2;
`floor_ratio_effects_out.txt`, own_m4), so two plumes and two end-on discs were drawn. `merge_layers`
(`engine_plumes_core.h`) runs in `build()` and in the ribbons' `update()` through `EnginePlumesFrame::parents`
(`Ring::parent`), so the plume, disc, ribbon and far paths drop the same record; far records sit in the same ring with
their parent. `engine_stage` reports `merged=N`. A record is dropped when another record of the same parent and kind
(steering and brake flags) is larger, it is at most 0.75 of that size, the axes are parallel (dot >= 0.95), and its
origin lies within the larger record's pre-floor size. The larger record keeps its floored value. Records are
bucketed by parent (2,048 hash heads), so pairs are tested only within one ship.

The distance limit is the size, not the plume's nozzle width (0.5 size). The glow body spans ±0.5 size
(`engine_bodies.csv`: nor ±500 at value 1,000), and the Scorpion pair is 0.69 size apart, so the nozzle width would
miss it. The 0.75 ratio keeps equal twins apart. Census over run412's 24 frames with `engine_draw` rows
(`run412-engine-disc/run412_colocated_pairs.py`, `_out.txt`; ships approximated by a shared axis, since the rows carry
no parent):

| Pair | Distance / larger size | Size ratio | Merged |
| --- | --- | --- | --- |
| nor + tiny (own Scorpion) | 0.5..1, 24 pairs in 24 frames | 0.50 | yes |
| huge + big3 (the capital at frame 4480) | <= 0.5, 32 pairs in 8 frames | 0.20 | yes (4 of its 8 big3 into its 2 huge) |
| huge + huge | 0.5..1, 16 pairs | 1.0 | no (ratio) |
| big3 + big3 | 1..2, 24 pairs | 1.0 | no |

Not established: whether the capital's huge + big3 pairs are layers of one nozzle or separate nozzles inside one
outer glow.

Fixture case `merge` (still look, floor scale 1, R 67.3, s 1, the nor's floored nozzle 40 px; 1920 and 5120):

- The pair draws 1 nozzle and 1 disc end-on (0 side-on), with merged 1 and value 23.56.
- Its frame total and end-on centre equal the nor alone (ratio 1.00000).
- Unmerged, as Run 124 drew it, the pair's total is 1.74x the nor alone end-on and 1.80x side-on. At the nor's
  centre the second layer adds only 1.106x end-on (1.009x side-on): its disc is centred 0.6 nozzle widths away.
- Two records 3 nozzle widths apart, or anti-parallel at one origin, are both kept.

Builder cost (`BUILD_MERGE`, floor and merge, ships of 8, median of 50 x 41 builds), against the floor alone:

| Records | Floor alone | Floor and merge |
| --- | --- | --- |
| 30 | 4.13 µs | 5.30 µs |
| 100 | 13.47 µs | 15.29 µs |
| 300 | 38.94 µs | 42.64 µs |
| 1,024 | 138.73 µs | 148.43 µs |
| 300 of one parent (worst case) | — | 123.13 µs |

The ribbons run the same merge once more per frame.

Re-estimate of the flight centre (inferred). The main nozzle's centre was measured at 10.5-11.3 / 6.1-6.3 / 6.1-6.3.
The disc fade gives x 0.667, and removing the second layer at that centre gives / 1.106 (fixture). That is about
6.3-6.8 / 3.7-3.8, still above the 2.11 white point at EV +1. The brightest spot of the old frame, the small layer's
own centre (peak 19-20 / 15.5-16.8 / 15.5-16.8), and the second halo (1.74x frame total) are gone. The remaining gap to
the fixture's 02b centre (about 3x) is not explained by the stacking.

## Unknown, and what settles it

- Whether the body at 0.3-0.8 linear reads too dark at 5120x1440 with bloom on: the lab at the same constants is the
  first check, then one flight (own ship chase view, a fighter alongside, a capital). The presets are the lever.
- The far dots: the body energy falls to 0.37, and at 2-6 px the resolve integrates energy, so the dots may dim by a
  similar factor. `05` before and after settles it; if the 2-6 px dot luma falls under 0.7 of today's, raise `far_low`
  (0.15) so the dot end alone compensates; the law's shape stays.
- TAA on the finer striations through the three-frame dump; a ghost or a crawl on the 0.22 n across structure would
  show as anisotropy below 3 on the resolved images while the FP16 frame passes.
- The native slot count of the proposed program (unknown until a Windows build; since the review fixes the count is
  logged against an advisory 1,024, not gated).
- The end-on kappa and cap for the new profile: `plume_end_on_model.py` rerun with the new radial law gives the number;
  the disc's chase floor (0.6) is unaffected.
- How the look moves: the images are single frames; the lab shows the flow. If the user finds the 4.5 : 1 streaks too
  linear in motion, the along scale 1.0 -> 1.3 (aspect 3.5) is the one knob to turn.

## Options considered and why they lose

- **Raise the tint saturation or add a saturation look in the tonemap.** Treats the symptom: at 4-8 linear AgX maps
  every hue to near-white; no saturation term recovers it without lifting the whole image. The fix is the body's level.
- **A darker exposure or a lower I_core for the body alone (I 4 -> 2).** Lowers the core too; the references keep the
  core at or above white. The peaked profile keeps the core and lowers the rest.
- **Keep the cosine cells with a larger amplitude (0.8).** Crests 1.8x the mean rebuild the bright head the user rejected
  after flight D, and the gaps stay above 0.5. Carving keeps crests at the body and makes the gaps dark.
- **A streak texture instead of the analytic field.** A texture lane would cost a sampler and a resource across Reset
  for a field the two existing fbm evaluations already provide at 0 extra slots on new coordinates.
- **Polar spokes on the axial quad too.** The side view's streaks are the advected field; spokes belong to the disc
  where the flow is along the line of sight.
- **Three-octave isotropic noise at a higher frequency ("more detail").** More cotton at a finer grain; the anisotropy
  and the radial profile are what read as fire, not the octave count (and the slot budget is better spent on the
  colour and cell terms).
