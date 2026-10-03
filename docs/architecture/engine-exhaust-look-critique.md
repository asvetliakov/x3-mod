# Engine exhaust look: critique and redesign (2026-10-03)

Owning note for the plume look after the fixture's look images
(`verification/results/engine-effects/look-images/`, `README.md` there lists every band). Implemented on 2026-10-03 with
the deviations and measured gates of section 6; sections 1-5 are the design as written (section 1's images are the
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
| Profile, core | `0.08 + 0.92 exp(-(radial / 0.32)^2)`; hot core `exp(-(radial / (0.45 core))^2)`, `x (1 + 0.6 hot (1 - 0.5 smoothstep(0.2, 0.8, u)))` | The core's boost cools to half along the plume. Without it the cyan axis at u 0.5 on the uncapped 40 px plume read whiteness 0.137-0.149 through the fixture's AgX (gate 0.15) |
| Edge, tongues | `1 - smoothstep(0.45, 1, radial + 1.6 erode S2 (0.6 + 0.8 u))`; tail on `u + 0.614 erode S2` (0.35 at erode 0.57) | Tongues tied to `erode`, so the still look (erode 0) has none |
| Cells | `1 - a (1 - c)`, `c = (0.5 + 0.5 cos(2 pi u / period))^3`, `a = min(1, 1.7 shock) e^(-5 cfade u) smoothstep(0, period / 2, u) (1 - smoothstep(0.3, 0.9, radial)) s`: gap 0.85, first gap 0.33 of the crest, 0.74 by u 0.4 | Gap 0.85 instead of 0.8 and a half-period ramp. The written term measured lane depth 0.30-0.40 and gaps 0.69-0.78 (gates 0.35 and 0.6), because the full-period ramp and the fade leave the gap at u 0.24 at 0.61 of the crest. Gap 1.0 lets the carved white core dominate a red plume's high-passed luma (anisotropy 1.8-2.3) |
| Turbulence, heat, colour | as written; `head_colour` scale at least 0.75 | As written |
| Halo | e-fold `0.32 halo`, `exp(-4 u)`, reach 2.25 sigma | As written |
| Disc | kappa 3.33, halo gain 2.82, cap 1.5 x the side axis peak (per I: 1.20 revised, 1.46 previous), ring x 3 (`Look::disc_ring`); polar streaks with the direction on a circle of radius 2.5 in the noise and `4.5 rho - 1.6 phase` radially | kappa rises (1.8 -> 3.33) because the peaked profile's integral is smaller; the end-on/side energy stays 0.91 at s 1, L/n 4 (model). Halo 2.82 keeps the slab law's disc/side halo ratio (6.3). The ring needs 3x end-on to show between the integrated outer flame and the halo (model ring 1.29 cyan / 1.72 red at x3, 1.00 at x1). The circle replaces `atan2`, whose cut at +-pi would seam |
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

## Unknown, and what settles it

- Whether the body at 0.3-0.8 linear reads too dark at 5120x1440 with bloom on: the lab at the same constants is the
  first check, then one flight (own ship chase view, a fighter alongside, a capital). The presets are the lever.
- The far dots: the body energy falls to 0.37, and at 2-6 px the resolve integrates energy, so the dots may dim by a
  similar factor. `05` before and after settles it; if the 2-6 px dot luma falls under 0.7 of today's, raise `far_low`
  (0.15) so the dot end alone compensates; the law's shape stays.
- TAA on the finer striations through the three-frame dump; a ghost or a crawl on the 0.22 n across structure would
  show as anisotropy below 3 on the resolved images while the FP16 frame passes.
- The native slot count of the proposed program (unknown until a Windows build; the 800 gate is checked on both).
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
