# Own sun, step 1c: the look

Design note, 2026-11-02, for one decision: what the mod's own sun looks like after Run219 (step 1, a 96-px disc:
too big, too bright, white) and Run220 (step 1b, a 17-px core with 16 rays: "like a painted circle with rays, not a
sun or something belonging to the scene; we probably want a big bright halo (x3 size?) with falloff and dimmer rays").
Inputs: [the survey](../reference/sun-rendering-survey.md) and its five recommendations, [own-sun-pass.md §14](own-sun-pass.md)
(step 1b as built), `src/effects/own_sun_ps.hlsl`, `src/proxy/own_sun_core.h`, the user's references (a small hot core
in a wide soft glow; a sunburst over a soft halo) and the vanilla FL sun (a soft glow with ghost rings, no visible rays
out of fog, faint radial rays in fog). Figures are [M] measured (offline script or code) or [I] inferred. Implemented as
step 1c on 2026-11-02 (not flown; [own-sun-pass.md §14](own-sun-pass.md), the ledger section "Own sun step 1c — host
evidence"): ps_3_0 184 slots, the glow maximum 0.781 [M].

## Decision

**One quad, one program, three elements: a 17-px core (unchanged), a glow that dominates (a Gaussian corona of
sigma 1.5 core radii at 0.6 plus a Lorentzian halo of half-brightness radius 6 core radii at 0.3, windowed at 24),
and six soft rays at 0.15 of engine radiance with a 3-px-minimum Gaussian width; the horizontal streak pair and the
faint 2-radius halo go. Settings and their defaults are unchanged (`own_sun`, `sun_disc_radius` 0.012,
`sun_disc_radiance` 2, `sun_rays` 1); every number below is a constant in `own_sun_core.h`.**

The glow is drawn by the stage, not by bloom (§5, option a). It is the survey's synthesis (rec. 1-4: glow dominant with
a 1/theta^2-like tail several core radii across, 4-8 soft streaks at a fraction of the glow, visibility fraction for the
glow and a per-pixel test for the core, nothing sub-pixel) and the user's "x3 halo, dimmer rays".

### Defaults and constants an implementer types

| Name | Step 1b | Step 1c | Unit / note |
| --- | --- | --- | --- |
| `sun_disc_radius` (setting) | 0.012 | **0.012** | of the viewport height: R = 17.3 px at 1440 rows, 9.2 px at 768 [M]; scales every length below |
| `sun_disc_radiance` (setting) | 2 | **2** | K, engine FP16; the glow and rays carry K / 2 as now |
| `sun_rays` (setting) | 1 | **1** | 0..1, scales the rays only (the streak term is gone) |
| `white_mix`, `core_inner` | 0.25, 0.6 | **0.25, 0.6** | core unchanged: `K lerp(c/m, 1, 0.25) m T`, edge 0.6 R -> 1 R |
| corona `corona_gain`, `corona_sigma` | - | **0.6, 1.5** | `0.6 exp(-(r / 1.5)^2)`, r in core radii |
| halo `halo_gain`, `halo_radius`, `glow_radius` | 0.2, 2, 16 | **0.3, 6, 24** | `0.3 / (1 + (r / 6)^2) W(r)`, `W = saturate(1 - (r / 24)^2)^2` |
| rays `ray_count`, `ray_gain`, `ray_length` | 16, 0.75, 15 | **6, 0.15, 14** | hashed lengths 3.50, 9.77, 11.17, 4.14, 12.92, 13.13 R; strengths 0.72, 0.63, 0.93, 0.83, 0.73, 0.63 [M] |
| ray width | 0.12 R tapering to 0.3 of it | **sigma = max(3 px, 0.2 R)**, no taper | 3.5 px at 1440, 3.0 px at 768 [M]; a CPU constant (c7.w), since R is known per frame |
| ray along-profile | `saturate(1 - r/L)^2` | **`exp(-r / L) saturate(1 - r / L)^2`** | exponential (survey rec. 2), still zero at L |
| `ray_offset` | 0.5 | **0.25** | spoke units; 0.25 keeps every spoke off the screen axes (with N = 6 and 0.5 two spokes sat on the vertical axis, a vertical cousin of the deleted streak pair: review of 13692fc8) |
| streaks `streak_*` | 0.6, 0.15, 8, 24 | **deleted** | the horizontal pair was the most "painted" element |
| quad | 48 x 32 R | **48 x 48 R** (half extent 24 both axes) | 829 x 829 px = 0.69 Mpx at 5120x1440, 442 px = 0.20 Mpx at 1280x768 [M] |
| per-pixel law | `lerp(glow, core, D) open (D + (1 - D) visible)` | **same** | glow = tint x (corona + halo + rays) |

Glow sum, white sun at K = 2, outside the core (r >= 1 R): maximum 0.781 engine (a ray over the corona at the core's
edge); on the axis between spokes 0.676 at 1 R, 0.368 at 2 R, 0.244 at 3 R, 0.132 at 6 R, 0.068 at 9 R, 0.034 at 12 R,
0.011 at 16 R, 0.002 at 20 R [M, `verification/results/own-sun-pass/glow_meter.py`]. Through the gamma-2.2 decode and
AgX at the +1.3 EV cap that reads 0.98 (core), 0.82 at 1 R, 0.62 at 2 R, 0.46 at 3 R, 0.37 at 4 R, 0.23 at 6 R, 0.08 at
9 R, 0.02 at 12 R (display, 0.04 is the black sky); at EV 0: 0.69, 0.46, 0.31, 0.24, 0.13, 0.03 [I, the AgX table of
bloom-falloff.md interpolated]. So the glow is visible to about 9 R (156 px radius, a 310-px halo at 1440; 83 px at 768)
and has no edge anywhere: the core's edge sits inside display 0.82-0.98, where AgX has no contrast left. Step 1b's
visible halo ended at about 3 R. The rays are 1/5-1/7 of the glow at their base and show mainly beyond 4 R as soft spikes.

## Why these numbers

1. **Element set and ratios (brief Q1).** Core 1 : corona ~3 (sigma 1.5, 10 % at 2.3 R) : halo 6-20 (half at 6 R, tail
   to the 24-R window) : rays 3.5-13 R, at 0.15 of engine radiance, i.e. one order below the core in engine terms and
   nearly three in decoded luminance. This matches the survey's ratio table (core 1 : glow 3-6 : halo 10-20 : streaks
   5-15, streaks one to two orders below the core) and the user's "x3": the visible halo radius triples (3 R -> 9 R).
   Two glow terms, not one: a single Lorentzian wide enough to show at 9 R is flat out to 3 R (a bigger painted circle),
   a single Gaussian has no tail; Spencer's PSF is a narrow bloom plus 1/theta^2 tails, which is what the sum is.
2. **Radiance budget (Q2).** The core stays at K = 2 (decoded 4.6; K <= 2.25 leaves the +1.3 EV cap untouched; Run219's
   K = 8 at 96 px pulled `ev` to -1.0 [M, flight]). Every glow pixel stays under 1.0 engine (0.781 max [M]), so the
   core remains the one element above 1 and the meter's worst case is bounded by the core's own tile: a sun tile that
   becomes the p99 tile gives `ev_limit` +1.67 EV (core) or >= +3.87 EV (any glow tile), never below the cap [M, script].
   Tile counts at 5120x1440 (64-px tiles, 1,840, p99 needs 19), worst of 16 sub-tile placements: 0 tiles reach the
   K-2.25 luminance, 1-4 exceed engine 1 (the core), 2-4 exceed the 0.18 key, 20-24 become "lit" (mean log2 L above
   1/512; 1.1-1.3 % of all tiles); at 1280x768 (16-px tiles, 3,840): 0, 1-4, 6-9 and 96-100 (2.5-2.6 %) [M]. The lit
   tiles enter the key rule's lit median, which the tile model does not resolve against a real scene; the black sky
   itself is at ~0.0035 decoded and already lit, so the glow shifts the median by at most 1.3 percentile points at the
   user's resolution [I]. Envelope: with the sun in view, `ev_limit` may not fall below its value without the sun and
   `ev` may not drop by more than the dead band (0.25 EV); the flight's `own_sun_frame ev= ev_limit=` decides.
   Default `sun_disc_radiance` stays 2; the glow and rays are K / 2 times their gains, so one knob scales all.
3. **Radius (Q3).** 0.012 of the height, unchanged; every length is in core radii, so the knob scales core, glow and
   rays together (the proportions are the design, not a second knob). The one exception is the ray width floor of 3 px,
   which exists for TAA, not for the look.
4. **Rays under TAA (Q4).** Gaussian across, sigma = max(3 px, 0.2 R) with no taper (FWHM >= 7 px; step 1b tapered to
   0.6 px at the tip at 768 rows, below the raster), exponential along. The jitter moves the raster by <= 0.5 px, which
   changes a 3.5-px Gaussian at 0.15 engine by < 0.02 engine per pixel per frame [I]: inside any clip box. The glow's
   gradient is < 0.01 engine per px everywhere outside the core [M, profile above]; the core's 7-px edge is as flown
   and accepted. Nothing in the quad is sub-pixel. The resolve reprojects the sun's sentinel pixels with the far-plane
   camera history (own-sun-pass.md §7) and clips with the 3x3 mean +/- sigma box; the far stabiliser's gate is 0 on the
   sentinel (`resolve.hlsl` header), so a smooth static feature converges to its value at any radiance. Note: step 1b's
   "stabiliser emitter bound E = 1" names the sentinel stabiliser retired on 2026-09-25 (taa-mask-fold.md §7,
   temporal-integration.md); the <= 1 glow bound is kept here for the meter and for AgX contrast, not for the resolve.
5. **Roll (Q5).** Screen-fixed, as built. The rays are a lens/eye starburst, which is fixed to the sensor, and the
   engine's own chain under the comparison hotkey is screen-fixed too; a world-fixed (corona) pattern would turn with
   the ship's roll and read as a physical object. Cost of the alternative, if the user asks: the camera basis is
   available (`CameraState.r`, the world up axis in view space is `r[3..5]`; its normalised xy is the roll, one `sqrtss`,
   no CRT math) and the rotation is 2 slots; nothing else changes.
6. **Occlusion (Q6).** Keep: per-pixel knight clip over the whole quad (a hull in front darkens the glow where it is,
   as it darkens vanilla's fog inscatter) and the 13-tap core-visibility fraction scaling the glow and rays (max step
   8.1 % per tap [M, step 1b]). With the glow at 0.68 at the core's edge, an 8.1 % step is 0.055 engine, under the
   per-frame change the resolve already averages for the core; no widening. The user accepted the occlusion twice.
7. **Fog (Q7).** The far-transmittance scalar T multiplies core, glow and rays, as now (the constants carry it). In a
   fogged sector the sun dims as a whole while the fog pass's inscatter (same poll) carries the sky's glow.
8. **Settings (Q8).** `own_sun`, `sun_disc_radius`, `sun_disc_radiance`, `sun_rays`; nothing added. `sun_rays` keeps its
   0..1 range (the dim default is the maximum; a stronger ray is a constant change, not a knob).
9. **Kill criterion (Q9).** One flight, two F8 pairs (clear sector; fogged sector), Ctrl+Shift+F9 against the engine
   chain in each. The user's verdict decides: "better than the engine chain in both" accepts step 1c and freezes the
   look constants; "worse in either" sets `own_sun` default off (the engine chain stays the shipped sun) and closes the
   look iteration, no step 1d and no new setting. "Better but ..." allows at most one more constant-only pass against
   a reference the user names. Also a kill regardless of verdict: `ev` with the sun in view lower by > 0.25 EV than
   without it, or ray shimmer while parked.

## Cost on the hot path

One `DrawPrimitiveUP` of a 0.69-Mpx quad at 5120x1440 (0.20 Mpx at 1280x768) [M]. Slots: about 197 as built minus the
streak term (~8) plus one Gaussian (~4) and two exponentials along the rays (~4): about 200, under 250 [I; the native
validator measures]. GPU: 0.69 Mpx x 200 slots x 0.136 ps per slot-pixel (1 us per slot per full frame, the slot-budget
rule) = 19 us plus the fixed draw, well under 0.1 ms [I]. CPU: the same constant build as step 1b (36.7 ns per frame
[M, `cpu_cost.py`]) plus one `max` for the ray width. No allocation, no new target, no readback, one draw as before.

## Native Windows

The flown configuration is native Windows (C:\X3). The stage uses documented D3D9 only (ps_3_0, `DrawPrimitiveUP`,
ONE/ONE onto `A16B16G16R16F` behind the `D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING` check, point-sampled lane reads);
nothing in this step adds a dependency. The program's validity is proven by the native validator harness
(`verification/results/own-sun-pass/native/`), 0 messages required; CrossOver runs the same path on wined3d.

## Verification that would prove it

- Host: `test_own_sun` updated for the law (glow maximum outside the core <= 0.79 at K = 2 white, hue kept, ray
  sigma >= 3 px at 768 and 1440 rows, six spokes with the hashed lengths above, no seam, the 13 taps unchanged);
  `law_checks.py` for N = 6; `glow_meter.py` re-run with the header's constants (no tile at the K-2.25 luminance,
  worst sun-tile `ev_limit` >= +1.67 EV at both resolutions).
- Program: `generate_own_sun_programs.py --check`, native validator 0 messages, slots <= 250.
- Flight (the device test; no own-sun fixture exists on the native host): the two F8 pairs and the hotkey comparison;
  `own_sun_frame ev=/ev_limit=` with and without the sun in view; a parked frame with a hull edge across the core.

## Options considered and why they lose

- **a. Bloom-driven glow with a per-pixel clamp exemption for the sun.** The bloom kernel is compact: 50 % at 2-3 px,
  1 % at 10-15 px, zero beyond ~120 px, and an extended source becomes a flat disk with an edge ([bloom-falloff.md](bloom-falloff.md)
  [M]); at strength 0.25 and source clamp 1.0 a clipped 17-px core adds at most ~0.09 of a plateau a few px wide. An
  exemption would need the prefilter to know which pixels are the sun (a mask or stencil across two passes), reopens
  the halo-on-star-fields complaint the clamp was added for, and still cannot produce a 1/theta^2 tail at 150 px.
- **b. A textured sprite** (vanilla's and Freelancer's billboard). An asset, a texture upload, filtering, fixed
  resolution (soft at 5120x1440), and the look the user rejected; the analytic law is resolution-independent and
  host-testable with the DLL's own constants.
- **c. A second low-resolution halo layer** (survey rec. 4). A target, a second draw and an upsample to prevent
  flicker that a smooth analytic gradient (< 0.01 engine per px) cannot produce; cost without benefit here.
- **d. Many rays** (16, thin, tapering, with the horizontal pair). The survey's list of what reads as painted: full-res
  hard edges, equal spacing, brighter than the glow, screen-fixed and unchanged under occlusion; Run220 confirmed it.
- **e. Rays that rotate with roll.** See Q5; 2 slots and the camera basis if ever wanted.
- **f. Two draws (glow quad + ray quad).** Not needed at ~200 slots; one quad keeps one occlusion read set.
- **g. A lens tie-in** (one ghost or chromatic ring sliding with the sun; survey rec. 5; vanilla's ghost rings). Off by
  default, not in this step: one more element and a slide law before the glow itself is accepted. The natural next
  step if the user misses vanilla's rings.
- **h. Brighter values (glow above 1, K above 2.25).** AgX gives 0.79-0.88 display for 1.0 engine already; brightness
  on screen is set by the exposure, and the meter guard bites above the K-2.25 luminance (Run219).

## Unknown, and what settles it

- The lit-median (key rule) effect of 20-24 lit tiles at 5120x1440 is unmodelled against a real scene; the flight's
  `ev=` with and without the sun settles it (kill rule above); `halo_gain` 0.3 -> 0.2 roughly halves the lit tiles.
- Run219's `ev_limit` -0.92 at 96 px where the tile model said "below p99" is still unexplained; the envelope here does
  not depend on which tile is p99 (every sun tile is bounded), only on the per-tile luminance, so it holds either way.
- The sky's alpha under the sun (authored term 0.375 vs highlight 0.05 in the bloom feed) decides the size of the small
  bloom tightening around the core; a `bloom_prepare` row or one scene-alpha readback settles it. Cosmetic.
- The display reads are from the AgX table interpolated, not from the tonemap program; a `run_hdr_pass` readback of a
  synthetic quad at the profile's values would pin them.
- The retired E = 1 bound is still cited in own-sun-pass.md §7 and the shader header; the step 1c implementation should
  reword both to the live resolve (far-plane camera history, 3x3 clip, far gate 0 on the sentinel).
