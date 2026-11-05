# Sector background: procedural stars and re-authored nebula textures

2026-11-04, design note (Fable). Survey: [space-backgrounds.md](../research/space-backgrounds.md).
Measurements: `verification/results/sector-background-textures/` (`nebula_texture_sizes.*`,
`sky_texture_stats.py`, `sky_texture_stats_fl.txt`). Nothing here is implemented.

**Outcome.** Ship (B) the offline nebula bake first, on FL, as a new `sky` row group inside the existing
LOD overlay catalogue (deband + 2x Catmull-Rom resample + dithered DXT1 re-encode of the 2048x1024 DXT1 parts,
exact zeros preserved, no mips, detail synthesis opt-in), and have the same bake stamp the painted star textures
with an `X3ML` header mark; then ship (A) the runtime star pass, which keys on that mark to find the star-body
draw, masks its colour writes exactly as the fog cards are masked, and draws one additive batch of 2 px
energy-conserving star quads in its place, seeded and sized from the mark alone, so A needs no engine-memory
read on either edition.

**Update 2026-11-04.** B, the nebula part bake, was rejected after the flight-level check (Decision B). The `sky` row
group now carries only the star-texture marks that A needs (Implementation).

**Update 2026-11-05.** A is implemented behind `sky_stars` (default off) on both editions and not flown
(Implementation A).

## Facts the decision rests on (measured unless marked)

- FL stock: 68 star textures `dds/nebula_<family>_stars_<colour>_diff.pck`, 1024x1024 DXT1 with 11 mips; 224
  nebula parts, 212 of them 2048x1024 DXT1 without mips; 79 families, 56 with four parts, 23 with none (fog-card
  families such as `foggreenoutlands`). AP+Mayhem: 322 star-named, 1106 parts, 861 of them 2048x1024 uncompressed
  (8 MiB each). One AP mod part is 4096x2048 DXT1, so that size loads (established by the brief).
- Every FL star texture has median luma 0: black plus stars, no painted haze (68 of 68). Bright peaks per
  texture: median 519, max 1145; lit texels (luma > 24) median 0.15 %, max 0.81 %.
- FL DXT1 parts: 0 transparent texels in 444.6 M; alpha is 1 everywhere, so the parts' `SRCALPHA/ONE` draws are
  plain additions and black (52 % of texels, median) must stay exactly 0 after any re-encode.
- Banding baseline on the 212 DXT1 parts: mean identical-luma run along x median 2.67 px, p90 4.29 (worst
  `avdustysol` 19.8); 8x8 non-black blocks with at most two colours median 27.9 %, p90 48.4 %. `bluewell`
  (Argon Prime, FL map 0,3 / 1,3): runs 2.0-2.4 px, plateau blocks 8-15 %.
- Bake stack timings (this host, one core): numpy DXT1 encode of 4096x2048 1.32 s, decode 0.30 s, Catmull-Rom
  2x upsample 0.27 s, five box filters r=16 0.33 s, gzip 0.10 s. The bundled encoder `lod_atlas._encode_color`
  is a PCA endpoint fit, always 4-colour mode.
- Runtime channels that already exist: the engine loads textures through `D3DXCreateTextureFromFileInMemoryEx`
  and the proxy parses `X3ML` in `dwReserved1[0..2]` into a per-texture mark with 8-bit slot/lane/K/payload fields
  (`src/proxy/lod_texture_mark.h`, `capture.cpp` loading_trace observer, sampler shadow per stage); the fog-card
  route masks a native draw's colour writes with `COLORWRITEENABLE` around the forwarded call
  (`motion_output_fog_inc.h` `fog_card_mask.begin/end`, `fog_card_match.h`); the sector reader exposes the row
  family name and `sector+0x140/+0x144` (`neb`/`stars`) on AP and FL (`farnhams-legacy-sector.md`).
- TAA: pixels no routed opaque draw wrote carry the RT2 sentinel and are reprojected at the far plane with the
  3x3 neighbourhood clip, luma-weighted HDR history and the strict sky term; weight 0.9. Exposure: the installed
  meter's p99 tile-maximum guard is star-sensitive (one luminance-64 star per tile on black gives -2.1 EV in the
  policy note's synthetic table; R2's tile geometric-mean percentile gives 0); lit tile threshold is 1/512 of
  white on the tile geometric mean. Bloom source clamp 1.0 decoded (ratified 2026-09-16).
- TBackgrounds row: `+0x48` count and `+0x4c[16]` star body ids (`nebula_<fam>_stars_%02d`), `+0x8c`/`+0x90[16]`
  nebula bodies; the sector's `<o t="2" s= neb= stars=>` picks indices (0 in every stock sector). The star body
  (`*_stars_01.pbb`) uses `effects.fx` blend materials (ONE/ONE, ONE/INVSRCCOLOR, SRCALPHA/INVSRCALPHA in the body
  census); the captured background pair `7b6393fe…/6109cf64…` shows 1 blend-off and ~3.3 `SRCALPHA/ONE` draws per
  frame. Which of these is the star body, and its order against the parts, is not proved (open invariant 1).

## Decision A: runtime star pass

**Chosen: replace in place.** When a draw binds a stage-0 texture whose mark says `payload = sky_stars`, forward
the native draw once with colour writes masked (the fog-card mechanism, actual HRESULT returned, Z/stencil
untouched) and immediately issue one additive draw of the procedural star batch with the same target, Z test
and write off, cull none, `ONE/ONE`, RGB mask 7. Whatever the engine draws after (nebula parts, planets, suns)
composes over our stars exactly as over the painted ones, so the native order is preserved without being known.
Suppression is lossless because the star textures carry nothing but stars (median luma 0, measured).

Options considered:
- *Draw over without masking.* Keeps the painted soft stars under the crisp ones: two star fields per sector.
  Loses on look; kept as the fallback if invariant 1 shows the star body is also drawing something else.
- *Identify the draw from engine memory (node model id against the row's `+0x4c` ids, the frame routine's
  `0x400000` branch).* Needs a per-edition layout proof and a per-draw engine read; the mark is read once at
  texture creation by code that already exists on both editions. Loses on cost and portability.
- *Point sprites (`D3DRS_POINTSIZE`).* Screen-aligned squares capped by `MaxPointSize`, size in pixels only for
  transformed vertices, vendor-dependent coverage (survey Q2, Q4). Loses on control of the footprint.
- *A full-sky star cube baked offline (B-style) at 4096x4096.* No runtime work, but a cube texel is still
  magnified to 2-3 px at 5120x1440, stars stay blobs and alias under jitter exactly like today. Loses on the
  goal.
- *Stars dimmed behind the nebula by a mask or `INVDESTCOLOR` blending.* The native parts add over stars
  (`SRCALPHA/ONE`, alpha 1); extinction would be new art and `INVDESTCOLOR` goes negative on an FP16 target
  with summed parts above 1. Left out (section 6).

## Decision B: offline nebula bake (rejected after flight-level check)

**Rejected 2026-11-04.** The recipe was implemented and passed its offline gate. At the size the screen shows
them, though, the baked parts looked worse than the stock parts magnified bilinearly by the GPU:

- Once each 4096 texel covers about 1.75 screen pixels, the ordered dither shows as a regular dot pattern.
- The 4096 DXT1 blocks quantise more than the GPU's bilinear interpolation of the 2048 blocks.
- On the bluewell part 01 preview at 4x crop, stock against bake, there were 9.8 against 4.9 mean colours per 8x8
  block and a plateau share of 3.4 % against 9.3 %.

The offline gate had measured the box-downsampled bake, which hid this. The part pipeline was removed from the
code.

The only remaining candidate is untested: an uncompressed 2048x1024 8-bit variant with deband, blue-noise ±0.5 LSB
dither and no upscale, at 8 MiB per part (AP Mayhem already ships parts in that form). The retained evidence is
listed in `verification/results/sector-background-textures/README.txt`.

The catalogue mechanism chosen for B stays, carrying only the star-texture marks:

- **Sky row group.** `tools/analysis/sky_textures.py` marks each star texture, and `lod_overlay.batch` adds the
  members to the same `addon/NN` (AP) or `addon2/NN` (FL) catalogues. The `.x3m-lod.json` manifest records them as
  `sky: {recipe_id, tool_sha256, options, rows: [{member, source, inputs_sha256, member_sha256, metrics}]}`.
- **Resolution.** The engine's resolver takes `dds/<basename>.pck` from the highest slot down, so a member with
  the stock name in the overlay slot wins. A selected mod package gets the members through the existing derived
  `<mod>-x3m-lod` copy.
- **Reuse.** Reuse is keyed on `inputs_sha256`. The sky tool files are deliberately not in `TOOL_FILES`, so a sky
  change does not rebake bodies and a body change does not re-mark textures.
- **Checks.** `lod_overlay_check` validates the slot by `overlay_sha256` as before.

Options considered:
- *A separate overlay slot with its own marker.* The LOD batch would see it as a mod catalogue (counted in
  `originals_sha256`, pushed or retired by slot moves) and every sky run would invalidate the LOD cache. Loses.
- *Runtime fix instead of a bake (bicubic taps and dither in the nebula PS).* The pair is SM1 and shared with
  `gui2d`; replacing it needs pass ownership that the material notes call unproved, and the cost scales with a
  few hundred draws every frame. The bake does it once. Loses on cost and risk.
- *Uncompressed 4096x2048 output.* 32 MiB per part, 128 MiB per sector. Loses on VRAM and load.
- *Learned super-resolution or DXT restoration.* Needs an inference runtime outside the stdlib+numpy bundle and
  carries documented colour shift and noise (survey Q3). Loses.

## Numbered answers

**1. Star pass.** *Hook:* the draw-call boundary in `capture.cpp`, inside the background phase (after the HDR
owner's initial Clear, before the depth-only Clear) and only while the HDR target is latched; condition: stage-0
sampler shadow lane = `sky_stars`. Action as in Decision A. *Seed and density:* the bake writes the mark
descriptor as `slot = fnv1a(family) & 0xff`, `lane = hue class from the texture's colour token` (white, blue, red,
orange, yellow, green, cyan, other), `K = bright-peak count / 8` (519/8 = 65 median, 1145/8 = 143 max), `payload
= 2`; the pass seeds its PRNG from `(slot, lane, K)` and draws `N = clamp(K × 48, 512, 8192)` stars (the ×48 stands
in for the unknown sphere tiling; a `sky_stars_density` multiplier is the only knob). Two sectors sharing a family
share a star field, as they share the painted texture today; the sector reader's family and `stars` index are
logged for correlation only. *Dimming behind the nebula:* by draw order, nothing else: the parts add over the
stars as they add over the painted ones. *Footprint and radiance:* a static VB of N × 4 vertices (position on the
unit sphere, corner, magnitude, tint; 28 B per vertex, 336 KB at N = 3,000 and 917 KB at the 8,192 cap, managed
pool) built once per seed on the render thread, the SM3
VS billboards each quad to `max(2 px, s(m))` on screen with the frame's TAA jitter applied as the routed draws
apply theirs, the PS writes a normalised Gaussian so the sum over the covered pixels equals the star's radiance
`R(m)` whatever the sub-pixel position (the survey's 2x2 pre-filtered splat as a quad, Q2); magnitudes follow a
power law so the count per magnitude rises 2.5x per step, the brightest star peaks at 1.0 decoded (the bloom
source clamp) and the median star at about 0.05, tinted by the lane class. Glare comes from the bloom pyramid, not
from a larger proxy. *TAA:* star pixels are sentinel pixels and get the far-plane camera reprojection, which is
exact for objects at infinity under pure rotation (also at SETA rates); the luma-weighted history and the 3x3
clip bound a 2 px star to the same firefly bound as today; when rotation exceeds the clip the pixel falls to
current-only, which is a jittered 2 px quad, not a smear. No new history mask. *Cost:* one draw, one VS/PS pair,
about twelve state sets and their restores, ~3,000 quads (K median) = 12 k vertices and ~15 k shaded pixels at
5120x1440, no per-frame allocation, no per-draw CPU work beyond the lane byte already read; inferred under 0.1 ms
GPU and under 5 µs CPU per frame; VB rebuild on a seed change about 0.1 ms once. *Exposure (Q5):* covered pixels
are at most ~0.3 % of the screen at peaks ≤ 1.0, so no 16x16 tile's geometric mean reaches the 1/512 lit
threshold and the R2-style meter is unmoved; the installed p99 tile-maximum guard reacts to a 64-luminance
pixel, so the peak cap 1.0 is a hard rule of the pass and the fixture replays the installed meter on a star-only
frame (open invariant 5 names which meter is installed).

**2. Star pass acceptance.** *Offline:* a host-suite test for the mark writer/parser round trip (payload 2, slot,
lane, K) and a D3D9 fixture under the Wine queue that renders the batch with a fixed seed at four sub-pixel
offsets and two rotations and checks per-star integrated energy within 1 %, peak ≤ 1.0, no pixel outside the
quad, then runs the resolve for 60 frames of a 0.5 px/frame pan and a 10 px/frame pan (SETA proxy) and checks
per-star peak-luminance variance below 10 % of the mean, and replays the exposure meter on the star-only frame
expecting EV 0. *Flown:* one FL flight in Argon Prime (bluewell) with `sky_stars=1`: rows `sky_stars
status=ready mark=slot/lane/K stars=N matched=1 masked=1 draw=ok` once per sector, pilot judgement: crisp stars,
no shimmer or trails at SETA 10x, no brightness step when turning from nebula to black sky, bloom unchanged.
*Kill criterion:* visible shimmer or trails under SETA rotation, or any exposure or bloom response to the stars,
not fixed by one footprint/radiance adjustment, removes the setting; it is not iterated a third time.

**3. Nebula bake.** Stages per part (defaults first): (1) read the winning member (mod layers honoured) and
decode to float RGB (`lod_atlas.decode_dds`, the `fog_families` 565 convention); (2) keep the exact-zero mask;
(3) deband: guided filter with the image as its own guide, r = 8 at source resolution, eps for an 8/255 edge
threshold, applied only where the local 17x17 range is below 12/255 (AdaDeband recipe, halo-free by the guided
filter's linear model); (4) resample x2 Catmull-Rom when the result stays within 4096x2048 texels (2048x1024 →
4096x2048; 2048x2048 keeps its size); (5) opt-in `--sky-detail`: value noise, 3 octaves, amplitude ≤ 4 %,
multiplied along the base colour (`out = base × (1 + k·n)`, hue locked, chroma unchanged) and weighted by local
luminance × gradient magnitude; (6) dither (ordered 8x8 Bayer, ±0.5 LSB) and DXT1 encode, zeros restored,
alpha 1; (7) no mips (stock has none; a mipped sky changes the sampler path, invariant 7); (8) `X3ML` mark
`payload = 3` (sky nebula) for the loading log, gzip, member under the stock name. Star textures: byte-identical
texels, header mark `payload = 2` only. Eligibility by default: DXT1 parts (FL 212+18+2 and AP 186); AP's 861
uncompressed parts are skipped by default (no DXT artefacts; a DXT1 re-encode would add hue drift and an
uncompressed upscale costs 32 MiB per part) and opt-in with `--sky-uncompressed=dxt1`. *Budgets:* FL sector 4 × 4
MiB = 16 MiB DXT1 (stock 4 MiB; AP stock uncompressed is already 32 MiB) plus the unchanged star texture; load
budget ≤ 150 ms added per sector change (inflate 4 × ~3.5 MB and upload 16 MiB; inferred, measured by the
loading-trace row at the flight). *Bake time (inferred from the measured stage timings):* about 2.5 s per
4096x2048 part on one core (decode 0.08 + guided 0.1 + resample 0.27 + dither/encode 1.4 + gzip 0.1, plus the
metrics), so FL 254 textures ≈ 640 core-s ≈ 1.2 min on 11 workers; AP default (186 DXT1) ≈ 1 min; AP with the
uncompressed opt-in (1106) ≈ 5 min; memory per worker under 0.5 GB. FL's overlay grows from 2.75 GB by about
0.9 GB (gzip DXT1 ≈ 3.4 MB per part measured on synthetic data; real parts compress less), which the multi-slot
layout absorbs under the 2 GB DAT cap. *Catalogue and cache:* Decision B.

**4. Nebula bake acceptance.** *Offline* (`verification/results/sector-background-textures/sky_bake_metrics.py`
and `sky_contact_sheet.py`, stdlib zlib PNG writer, no Pillow): per texture, on the bake box-downsampled to the
source size: plateau-block share ≤ 10 % and mean identical-luma run ≤ 1.5 px (baseline medians 27.9 % and 2.67
px); PSNR against the decoded source ≥ 40 dB; mean hue shift < 1° on texels with chroma > 8; zero violations of
the exact-zero mask; wrap-column difference equal to the source's; the contact sheet shows eight textures spanning
the baseline range (worst `avdustysol`, `bluewell`, median, best) as 256² crops at 4x nearest, source beside bake.
A host test runs the recipe on a synthetic banded gradient and asserts the same thresholds. *Flown:* one FL flight
in Argon Prime after one `x3m-regenerate` run: loading row `sky_textures family=bluewell parts=4 marked=4
4096x2048 DXT1`, sector load time within 150 ms of the previous run's row, pilot judgement: gradients smooth, no
noise, no halos, no colour change, no seam at the part edges. *Kill criterion:* visible noise, halos or a colour
change in the flown sector, or a load-time increase above 0.5 s, turns the sky group off by default (recipe kept
opt-in); if the fidelity and banding thresholds cannot both be met offline, the step is removed.

**5. Order of work.** B first, on FL: it is offline only (no hook, no runtime code except one log row), its proof
is mostly offline, FL stock is the banded DXT1 set where the gain is largest, and FL's regenerate flow already
writes `addon2` overlays; B also emits the marked star textures that A needs. Then A on FL (one flown step), then
both on AP (B's default scope there is the 186 DXT1 parts). The user: quit the game, run `x3m-regenerate` once in
`C:\X3FL` (adds about 1-2 min), start the game, fly in or jump to Argon Prime (bluewell), look at the sky
gradients and later, with `sky_stars=1`, at the stars while rotating under SETA; report the two log rows.

**6. Left out.** Twinkling: a stylisation tied to atmosphere (Stellarium's option), wrong in vacuum and a
per-frame luminance change that TAA reads as flicker. Procedural or ray-marched nebula volumes: new art without a
3D source and a full-screen cost at 5120x1440. ML upscaling or DXT restoration: no runtime in the bundle,
documented colour shift and noise. Nebula extinction of stars: the native composition adds, a mask or
`INVDESTCOLOR` is new behaviour with an FP16 sign hazard. Runtime bicubic/dither in the nebula pixel shader:
the SM1 pair is shared with `gui2d` and unowned. Mips on sky parts: changes the sampler path for no gain under
magnification.

## Open invariants for `implement-deep` before coding

1. **Star-body draw identity and order** (A). One capture frame's background phase with texture dimensions per
   draw: which pair and blend the `*_stars_01` body uses, where it sits against the parts, and the initial Clear
   colour (the masked draw leaves the Clear colour; it must be black). If the star body draws more than the star
   texture, fall back to draw-over without masking.
2. **Mark observer on FL and on non-atlas textures** (A, B). `lod_texture_mark` parse at
   `D3DXCreateTextureFromFileInMemoryEx` fires on X3FL.exe and for sky members; the sampler shadow gains a
   `sky_stars` lane without changing the strength lane; final-release bookkeeping (`live_marks`) holds.
3. **Match scope** (A). The star texture is bound only by the star-body draw in the background phase; menu,
   sector-map and loading views never hit the mask (guard on phase and HDR latch; count and log any other bind).
4. **Jitter and matrices at the background phase** (A). The route's per-frame jitter and the view rotation are
   available before the first routed draw; the pass uses the frame's view 3x3 (background and main scene share it
   in the captured cohort) with the far-plane projection constants, not a sampled per-submission `P[10]/P[14]`.
5. **Installed exposure meter** (A). Which of installed/R1/R2 is live; replay it on a star-only frame; if the
   p99 tile-maximum guard is live, the peak cap 1.0 is verified against the guard's 0.9 reference.
6. **Engine accepts 4096x2048 DXT1 on FL** (B). Proved by the loading row at the flight; the fixture-side check
   is the D3DX header path on the Wine queue.
7. **Sampler path of an unmipped 4096x2048 part** (B). Confirm `MIPFILTER NONE` and `LIN/LIN` magnification on
   the part draws so no mip chain is needed (a mipped bake would need a sampler proof first).
8. **Encoder quality** (B). The PCA endpoint fit without refinement must meet PSNR ≥ 40 dB on real parts; if not,
   add one endpoint-refinement iteration before the dither (cost measured, budget +1 s per part).
9. **Overlay growth** (B). FL overlay slot plan with ~0.9 GB more stays contiguous under the 2 GB DAT cap and the
   retire/replace rules; `--budget-bytes` interaction with the sky group defined (textures are never dropped
   for budget; bodies are).
10. **Native Windows** (both). All calls are documented D3D9/D3DX (`SetRenderState`, `DrawPrimitive`, static VB,
    header parse on the engine's own D3DX call); behaviour is Windows-compatible source until the user flies it on
    native D3D9, tracked in `platform-portability.md`.

## Implementation (2026-11-04): star-texture marks

What remains of the sky group is the mark that the star pass (A) needs.

- **Members and mark.** `tools/analysis/sky_textures.py` copies each winning
  `dds/nebula_<family>_stars_<colour>_diff` member byte for byte, texels and every mip, and writes the X3ML mark
  into `dwReserved1[0..2]`. The mark is version 1 with descriptor `slot = fnv1a32(family) & 0xff`,
  `lane = colour class`, `K = bright peaks // 8` and `payload = 2`. A host test round-trips it through
  `lod_texture_mark.h`'s own parser; `lane_of` stays 0, so the strength lane is untouched.
- **Switch and log.** `lod_overlay.py --batch --sky auto|on|off` (auto: on for FL, off for AP) carries the marks in
  the overlay slot, and the regenerate step logs one row per family.
- **Reuse.** `inputs_sha256` hashes the whole unpacked source DDS (header and every mip) together with the recipe
  id, because the output is those bytes plus the mark. The group's `tool_sha256` hashes `sky_textures.py` and then
  `lod_atlas.py`.
- **Budget, packages and candidate check.** Marked textures are never dropped by `--budget-bytes`. A package star
  texture is marked into the derived copy. `lod_overlay_check.candidate_state` counts the members as intended sky
  overrides.
- **Earlier part members.** A nebula part is no longer a member. A `--sync` run therefore drops the part members
  and their sky rows that an earlier version wrote (the FL run-49 overlay) while keeping the star members.
- **Fog step.** `fog_families.py` builds `sfc.Assets(game, mods=...)`, the full view including the overlay slots,
  not a pinned stock view, so it sees the star members. Their texels are identical to stock, so its colours are
  unaffected. Once the group is on for AP, `--check` may still report changed inputs for families that use
  `--background-palette`, because the member bytes differ from stock in the header.
- **Removed.** The part re-authoring pipeline, its options (`--sky-parts`, `--sky-uncompressed`, `--sky-detail`,
  `--sky-preview`), the previews, the part metrics and `failures()` were removed. Their findings (encoder bound,
  gate redefinitions) survive only in the retained `.txt` outputs listed in the results `README.txt`.

## Implementation A (2026-11-05): runtime star pass

Built, not flown. Evidence: `verification/results/sky-stars/` (`run_fixture.py` -> `fixture_out.txt`, `host_check.py` ->
`host_check.json`, `native/validate.py` -> `native/result.json`); host test `verification/analysis/test_sky_stars.py`.

**Pieces.** `src/proxy/sky_stars_core.h` (portable: seed, N, generator, kernel, vertex constants; no CRT math, no float
return), `src/renderer/sky_stars_pass.{h,cpp}` (programs, declaration, MANAGED VB of 8,192 quads rewritten per seed,
MANAGED 16-bit IB built once, save/set/draw/restore), `src/proxy/motion_output_sky_stars_inc.h` (match, mask, rows),
`src/effects/sky_stars_{vs,ps}.hlsl` compiled by `tools/shaders/generate_sky_stars_programs.py` (native D3DX, as the own
sun). Settings `sky_stars` (default off), `sky_stars_density` (0.25-4, default 1), developer `sky_stars_mask` (default 1).
The D3DX texture hook is patched when either `lod_material_strength` or `sky_stars` is on; payload-2 marks go to a
separate 1,024-entry table on the device (512 stored before refusals) (the table entry gained a `k` byte; the strength lane is unchanged).

**Where the hook sits and how it is guarded.** `MotionOutput::before_draw`, right after `evaluate_draw` (one bool test
with the setting off). The SetTexture hook keeps the packed seed of the stage-0 texture and a "bound since the last draw"
bit. A draw matches when: the seed is non-zero; the selector is in `Background` (after the latching Clear, before the
depth-only Clear); `hdr_state_ == Active` with the FP16 target and no MSAA (the latch); the texture was bound for this
draw, or the draw repeats the frame's first match's (vs, ps) and seed (a stale stage-0 binding on an untextured draw is
not the star body: this guard is not in the design); the bind bit is taken at the top of `before_draw` (`sky_stars_draw_begin`), before
any early return, so a draw that never reaches the classification (blocked, state lost, engine draw, UP) cannot
leave it for a later draw; only RT0 with a full viewport; no state block recording, no active
query; the route left the draw native; the background camera (`camera_background_`, read at the latching Clear) is
valid; the pass is attached and the seed's VB built. The pass and VB are prepared before the mask is set, so a refusal
leaves the painted stars. A marked texture bound for a draw outside the phase or the latch is counted (`outside=`), the
first four logged (`sky_stars_outside`), never masked. Mask mode forwards the native draw with `COLORWRITEENABLE 0`
(`FogCardMask`, the actual HRESULT returned, Z/stencil untouched); the batch is drawn once per frame after the first
matched draw (later matches of the same pair are masked only). `sky_stars_mask=0` draws over the unmasked draw and skips
the batch when a composition, additive or fog-card route took that draw.

**Batch.** Vertex = direction (12 B), corner px (8 B), energy (4 B), D3DCOLOR tint (4 B) = 28 B. VS: `c252-c254` are the
clip x/y/w rows of `r` (the background view's rotation) times `m00, m11, m20, m21` with the jitter folded in exactly as
`fade_region_math.h jitter_rows` (`+2 jx / W`, `-2 jy / H`, times w) when `jitter_active_`; `z = 0.99999 w`; the corner
is added in clip space as `corner x (2/W, 2/H) x w`. States: ONE/ONE ADD, `COLORWRITEENABLE 7`, Z and Z-write off, cull
none, alpha test, sRGB write, fog, stencil, scissor, user clip planes and `WRAP0/1` off, clipping on, solid fill; VS/PS,
declaration (or the application's FVF), stream 0 with its frequency, indices and `c252-c255` are read before the first
write and put back in reverse order; a failed restore latches `motion_state_lost_` and keeps the pass off until Reset.
Attach refuses a device whose getters fail (a pure device: `reason=getters`) before any star body is masked; three
failed batches in a row stop the matching (the painted stars return) until the next Reset. The mask uses the route's
own `fog_card_mask`, so composition, the additive route and the gain variants skip a masked star body exactly as a
masked fog card.

**Kernel and law (changed from the design).** The PS writes a separable Beta(3,3) kernel over [-a, a] integrated over
the pixel (`C(t(u+0.5)) - C(t(u-0.5))` per axis, C the quintic smootherstep): Gaussian-shaped (sigma 0.378 a) but with
compact support and a polynomial CDF, so the covered pixels sum to the energy exactly at any sub-pixel position; an
erf-based Gaussian could not be exact in the PS. The quad half extent is a + 0.5625 px. Brightness: decoded peak
`p = 1 / X`, X uniform in [1, 2.5^4], target peak `p^(1/2.2)` (the installed `hdr_decode` gamma 2.2), energy = target
peak / 0.6288 (the centred share at a = 1). The step is a flux ratio of 2.5 (not Pogson's 2.512), which makes the
2.5x-per-step count law exact without CRT `pow`. Under this law every star stays at the 2 px minimum footprint: s(m)
never exceeds it while the peak is capped at 1.0 (`half_support` grows a only if a caller raised the energy). Tint: the
lane class colour mixed with white by a per-star share in [0.25, 0.75], brightest channel 255. N = clamp(round(K x 48 x
density), 512, 8192). Caveat (review 2026-11-05), a flight observation to make: the energy is conserved in the
target's pre-decode values, not in linear light; after the gamma-2.2 decode a star's summed linear energy varies about
2x with its sub-pixel position (the peak cap and the brightness law hold for a centred star). TAA accumulates the
pre-decode values, so a still star is steady; watch for a faint pulse on slowly drifting stars.

**Exposure (invariant 5).** The live meter is the installed stage-2 space-aware meter (`src/renderer/exposure.cpp`:
lit tiles by geometric mean >= 1/512, key rule, p99 tile-maximum guard against 0.9 x 16.29); R1/R2 exist only in the
offline study. Replayed on star-only 5120x1440 frames (80x23 tiles of 64x64 px; gamma 2.2 decode, `hdr_ev_max` 1.3):
K 65 / density 1 (459 stars in view) EV 0, neutral, 0 lit tiles, guard `ev_limit` +5.91 (p99 tile max log2 -2.04);
K 143 / density 4 (8,192 stars, 1,213 in view) EV 0, `ev_limit` +5.25; control (one luminance-64 pixel per tile)
`ev_limit` -2.13. Measured by `host_check.py`.

**Fixture (native D3D9, RTX 5070 Ti, measured).** 653 isolated stars over 4 sub-pixel offsets x 2 rotations: worst
per-star energy error 0.17 %, worst spread across offsets 0.13 %, peak 0.9917, 0 written pixels outside the quads; state
restored on a clean run, an injected draw failure and an injected restore failure (0 mismatches); attach holds 5 objects,
detach 0, the device releases to 0 references. Cost at 5120x1440: one batch 3,120 stars
<= 1.5 us and 8,192 stars <= 2.4 us (CPU submission plus GPU, event-flushed over 200 batches: an upper bound),
`draw()` CPU 0.73-0.78 us (50 device calls, measured by the fixture's DRAW row), VB rebuild 0.22 ms (3,120 stars) and 0.46 ms (8,192) per seed
change, Lock to Unlock. One run used a plain D3D9 device with the production MANAGED buffers; two others (including
the retained `fixture_out.txt`) fell back to a D3D9Ex device with DEFAULT buffers (fixture-only `set_pool`) while another
process held exclusive fullscreen; the checks gave the same figures. Native shader validator: `sky_stars_vs` 116 words / 14 slots, `sky_stars_ps` 90 words / 11 slots,
End() S_OK, 0 messages.

**TAA.** The resolve replay (`tools/analysis/taa_resolve_replay.py`) is driven by `--taa-debug` capture dumps (1280x768
with their frame rows) and cannot take a synthetic star frame without fabricating a dump set, so the real resolve is
left for the flight. The fixture runs a model instead (history weight 0.9, exact bilinear reprojection, 3x3 min/max clip,
Halton(2,3) jitter, frames 20-59): per-star resolved peak coefficient of variation median 5.6 % / max 8.5 % at 0.5
px/frame (317 stars) and 3.9 % / 11.5 % at 10 px/frame (254 stars); current-only 16-22 %. Model figures, not the resolve.
Inferred: the native background draws are unrouted and so unjittered; the batch is jittered as the design asks.

**Rows (always tier).** `sky_stars_config requested= enabled= reason= density= mask= edition=` once; `sky_stars
status=off` when off; `sky_stars_texture ... mark=slot/lane/K stored=` for the first four marked star textures
(invariant 2); `sky_stars_device attached= reason= vs_slots= ps_slots= max_ps_slots=` at the first match;
`sky_stars_match mark= vs= ps= index= blend= src= dst= op= sepalpha= mask= z= zwrite= cull= atest= primitives= vertices=
indexed= texture=WxH levels= format= clear= target= outside= camera_p00.. scene_p00.. rotation_deg=` once per new seed
(invariant 1); `sky_stars status=<ready|no_mark|refused> mark= stars= matched= masked= draw=<ok|hr> reason= phase_draws=
frames= drawn_frames= outside= refused= rebuilds= table_full= density= mask_mode= draw_us= build_us= key=` at the depth-only
Clear when the status, seed, reason, table refusal count or sector key changed. Per-sector seed (2026-11-05, below):
`sky_stars_sector status=known|unknown reason= edition= S= id= kc= x= y= key= rebuilds= held= links= cached= sample=
check=` on every change of the read (status, reason, S, key; at most 1,024 per device), `sky_stars_sector_check status=
edition= base= windows= failed= vm_global= native_id_member=` once per process; `sky_stars_phase_draw field= index= star=
vs= ps= blend= src= dst= op= sepalpha= srca= dsta= mask= z= zwrite= texture=WxH identity= mark= primitives=` (the star
body's row adds `c0= c1= c2= c3=` and `background_valid= r=`) for at most 16 background-phase draws of the frame after a
field's first match, one-shot per field.

**Route test.** `verification/analysis/test_motion_sky_stars.py` (2 tests): the real fragment and `motion_output.h`
on the host with a scripted SetRenderState and pass (`verification/probe/motion_sky_stars_fixture.cpp`, 9 scenarios,
48 checks): mask, native, unmask, one batch in that order; a second call of the pair masked without a batch; stale
bind bit after an early return; outside the phase or the latch counted and unmasked; a second RT refused unmasked; a
refused attach unmasked and not retried; a failed restore quarantines until Reset; a failed mask restored at once;
three failed batches stop the matching; the table stores 512 marks (`Table<1024>`, AP+Mayhem has 322 star textures)
and its refusals reach the row as `table_full=`. It also checks the production call order in `motion_output.cpp`.

**Open.** Invariant 1 is answered by the first `sky_stars_match` row in flight (pair, blend, index, texture size, Clear
colour); `matched=2` or more per frame, or a pair that is not the star body, means falling back to `sky_stars_mask=0`.
Invariant 3 by `outside=`. Kill criterion as answer 2.

## Implementation A, look controls (2026-11-05)

Asked for after the first FL flight (run 50: 2,880 stars for Argon Prime, 2 px points read as too sparse, too dim and
too uniform). Built, not flown. Five settings, all `requires=sky_stars`, read once at process start; a value that does
not parse or is out of range keeps the default and the `sky_stars_config` row says `<name>_setting=default` (never a
refusal of the pass):

| Setting | Default | Range | Effect |
| --- | --- | --- | --- |
| `sky_stars_density` | 2 (was 1) | 0.25-4 | N = clamp(round(K x 48 x density), 512, 16384); `stars_max` 8,192 -> 16,384 (65,536 vertices, 16-bit indices still suffice) |
| `sky_stars_brightness` | 2 | 0.5-8 | gain on each star's energy |
| `sky_stars_size` | 1 | 0-3 | footprint growth with brightness |
| `sky_stars_min_size` | 1.5 px | 1-3 | half-width of the dimmest star (was the fixed `half_support_min = 1`) |
| `sky_stars_twinkle` | 0.3 | 0-1 | twinkle amplitude, 0 = steady |

**Law.** With `steps = log_2.5(x_max / X)` in [0, 4] (computed as 4 + log2(decoded) / log2(2.5), one logarithm per star):
`a = min_size + 0.35 x size x steps`, rounded to the 1/16 px grid; `energy = gain x target peak / centre_fraction(a)`,
so the centred peak is gain x target peak whatever a (a larger footprint adds light, it does not dim the star); then the
cap search grows a in 1/16 px steps while `energy x centre_fraction(a)` exceeds `peak_cap = 2.0` (target value, decoded
2^2.2 = 4.6). `half_support_max = 8` stays a hard bound: a star still above the cap there has its energy lowered to
`peak_cap / centre_fraction(8)`, so no star's centred peak exceeds 2.0 at any legal setting (host check `corners`: all
eight combinations of brightness, size and min_size at their limits, density 4, peak max 2.000, 1,316 stars at the
bound; before this rule brightness 8 / size 3 / min_size 3 left single stars at 6.47). At the defaults the half-width runs from 1.5 px (dimmest, a 3 px
soft point) to 2.875 px (brightest, 1.5 + 1.4 on the grid) and the cap search never runs (gain 2 x peak 1 = 2.0); at
brightness 8 with size 0 and min_size 1 it grows the brightest stars to 2.31 px. `min_size 1, size 0, brightness 1`
reproduces the first law's vertices byte for byte (host check `legacy`). The field (directions, tint) comes from the
seed's stream as before; the twinkle lanes from a second stream (`seed ^ 0x7417c1e5`), so the field is the same in
every look.

**Twinkle.** One generator word per star: the top two bits zero (one star in four, independent of brightness; host
check: 24.9 % of 6,240, 23.5 % of the brightest quarter) selects it; rate n in [52, 192] (n / 128 Hz, 0.41-1.5 Hz) and
a phase byte. The vertex gains a second D3DCOLOR (stride 28 -> 32 B: R = n, G = phase, B = 255 when on; 0 on steady
stars). The VS scales the energy by `1 + amplitude x sin(2 pi frac(rate t + phase) - pi)` (the half-cycle shift keeps
the sine argument in [-pi, pi]); energy only, the footprint does not change. The twinkle multiplies after the cap, so
a twinkling star's instantaneous peak reaches `peak_cap x (1 + twinkle)`: 2.6 at the default 0.3, 4.0 at 1. `c251 = (t, amplitude, 0, 0)` joins
c252-c255 in one five-register Get/Set/restore bracket (still 50 device calls per batch). **Why wall time:** t is
QueryPerformanceCounter seconds, never the game clock. The user flies with SETA at up to 10x; a twinkle driven by game
time would run at 4-15 Hz under SETA, which reads as flicker. Every rate is a multiple of 1/128 Hz, so t wraps at
128 s with no jump and stays below 128 in float (the brief's "modulo a day" would leave a 0.008 s float step at the
end of a day; 128 s divides a day). The tick conversion uses 31-bit signed halves (`cvtsi2sd`); `check_no_x87.py`
passes on both editions.

**Bloom and exposure (question 1 of the brief).** No code between the background phase and the HDR resolve special-cases
the sky or the background. The TAA resolve's luminance weighting is reversible and unclamped. The display path's
`min(v, exposure.y)` is `hdr_clamp` (developer setting, default 0 = off), so stars above 1.0 reach the tone curve. The
bloom extraction (`src/temporal/bloom_common.hlsl` `bloomExposed`) clamps the decoded value at `bloom_source_clamp`
(c27.y; user setting, default 1.0) before exposure, and only then applies the threshold (1.0, knee 0.5) in exposed
luminance. So the brightest stars bloom only as much as a decoded 1.0 pixel does. Brightness above 1.0 makes them
brighter on screen but does not make the glow bigger. Raising `bloom_source_clamp` would let them glow more, and it
would do the same for every other light.

**Evidence (measured).** Host check `host_check.json` (50 checks): K 65 at the defaults, 6,240 stars, centred peak max
1.9984, half-width 1.5-2.875 px, energy error 8e-16 at six sub-pixel offsets, legacy reproduction 0 mismatches. Meter replay
at 5120x1440: defaults EV 0, neutral, `ev_limit` +2.71; K 143 density 4 (16,384 stars) EV 0, `ev_limit` +2.16, so
both stay above `hdr_ev_max` 1.3 and the stars do not move the exposure. With every look setting at its maximum
(brightness 8, size 3, min_size 3, density 4) single stars stay at 2.0 and the image maximum is 4.23 where large quads
overlap; EV stays 0 and the guard's `ev_limit` drops to +0.59, below `hdr_ev_max`, so in that look a frame of stars
alone keeps the exposure from rising past +0.59. That case is reported, not asserted. Native fixture (`fixture_out.txt`, RTX 5070 Ti, plain D3D9, MANAGED): 945
isolated stars, energy error 0.15 %, spread across offsets 0.11 %, per-star peak 1.904 (image 2.242 where quads
overlap), twinkle at amplitude 1 (three times t) matches the modelled factor within 0.09 % of the star energy over 309 twinkling and 1,089 steady star samples
(steady stars move 0.13 %), state restored with c251-c255 under both injected faults. Cost at 5120x1440: one draw call per
frame, `draw()` CPU 0.75 us, batch upper bound 2.2 us (6,240 stars) and 6.9 us (16,384); the rebuild of a full
16,384-star buffer (2,097,152 B VB, Lock to Unlock) takes 1.04 ms (best of 5) per seed or look change, and no time per frame. No
allocation and no validation are added per frame. Programs: `sky_stars_vs` 162 words / 27 slots, `sky_stars_ps`
unchanged (90 / 11); the native validator accepts both with End() S_OK and 0 messages.

**Rows.** `sky_stars_config ... density= density_setting= brightness= brightness_setting= size= size_setting= min_size=
min_size_setting= twinkle= twinkle_setting= mask= edition=` (the setting tags are now `ok|default`, where they were
`ok|invalid_setting`); the `sky_stars` row gains `peak_max=` (the largest centred pixel), `support_max=` (px) and
`twinkling=`; `stars=` and `build_us=` follow the new count and law.

## Implementation A, clustering and brightness 1.5 (2026-11-05)

After FL run 51 (look controls accepted): `sky_stars_brightness` default 2 -> 1.5 (the brightest star's centred peak
is now 1.5, so the cap search does not run at the defaults), and the directions are no longer uniform. Built, not flown.

**Law.** New setting `sky_stars_clustering` = c (default 1, range 0-2, `requires=sky_stars`, same `ok|default` tag,
part of `Look`, so a change rebuilds the buffer). The directions follow a seeded density on the sphere:
`density(d) = min(bound, max(floor, 1 + sum_clusters c w S(d) - sum_voids c v S(d)))`, with
`S(d) = smootherstep((d.n - cos r) / (1 - cos r))` (1 at the cap centre, 0 at its rim). The cap stream (a third
generator stream, `seed ^ 0x3c6ef372`) draws 6-10 cluster caps (radius uniform 8-35 degrees, w uniform 1-4) and 2-4
void caps (radius `10 + 15 sqrt(u)` degrees, so 10-25 leaning large; v 2-3; each void centre redrawn up to 16 times
until it lies outside every cluster cap). At c = 1 a void sits on the floor over the inner part of its cap; c = 2
doubles every weight, so the clouds are twice as strong and the floor covers most of each void cap. `floor = 0.08 x (1 + sum c w (1 - cos r) / 4)` (the
void-free mean; the cap average of S is (1 - cos r) / 4), so a void keeps a few stars. `bound` = the largest density
at the cluster centres and at the weighted midpoints of every cluster pair. The law is the density clipped at that bound,
so rejection against it is exact for the clipped law; the clip only flattens a crest where overlapping clusters peak
between the probes. Each star's first candidate is the first law's uniform draw from the seed's stream. It is kept with
probability density / bound; otherwise the cap stream supplies a new uniform candidate. After 64 candidates the last
one is kept, so `build()` always ends. N is unchanged. Brightness, tint, twinkle and the footprint come from the first two streams as before, so
clustering moves only the directions. c = 0 skips the test entirely: the directions are the first law's, byte for byte
(host check `legacy`: 0 mismatches against the verbatim first law). The cap layout comes from the seed alone, so
moving the slider strengthens or weakens the same pattern.
CRT-free: `cos_bounded` (Taylor, |x| <= pi/2), `scalar::sqrt`, no atan2.

**Seed hook for a per-sector field (not used yet).** `generator_seed(mark_seed, sector_key)` folds
`seed64 = mark_seed ^ (sector_key x 0x9e3779b97f4a7c15)` to 32 bits (low ^ high). `build()` takes a trailing
`sector_key` (default 0), and key 0 returns the mark seed itself, so every field is unchanged today. A later change only
supplies the key (and adds it to the pass's rebuild key). Host check `generator_seed_fold`: key 0 identity, key 1 on
0x014101a7 = 0xe03c040b, and `sector_key_0_is_the_mark_field`.

**Evidence (measured).** Host check (`host_check.json`, 58 checks, 16 seeds x 16,384 stars, 48 equal-area bins = 6
bands of equal z x 8 longitude sectors of 45 degrees, binned independently with atan2): c = 0 chi-square at most 63.7
(47 dof), fullest/emptiest 1.20-1.36, no rejections; c = 1 fullest/emptiest 5.48-15.3, emptiest bin 74 stars, mean reject
rate 0.784, no candidate cap reached; c = 2 9.35-34.5, emptiest bin 47, reject rate 0.850, 32 of 262,144 stars kept
their 64th candidate. K 65 at the defaults: 6,240 stars, centred peak max 1.4988, cluster_max_min 10.1, reject rate
0.707. Native fixture (`fixture_out.txt`, RTX 5070 Ti) rebuild of a full 16,384-star buffer, best of 5: 1.15 ms at
c = 0, 3.05 ms at c = 1 (reject rate 0.804), 3.79 ms at c = 2 (0.861); before this change it was 1.04 ms. The
extra 0.11 ms at c = 0 is the 48-bin count of the row statistics (branch-free sector selection). The rebuild runs
once per seed or look change, not per frame. Meter replay at the new default: EV 0, neutral, `ev_limit` +4.01 (K 65)
and +2.99 (K 143, density 4). Programs unchanged.

**Rows.** `sky_stars_config` gains `clustering= clustering_setting=`. The `sky_stars` row gains `clustering=`,
`cluster_max_min=` (the fullest of the 48 bins over the emptiest; an empty bin counts as 1) and `reject_rate=`
(rejected candidates / all candidates).

## Implementation A, per-sector seed (2026-11-05)

Every sector now draws its own field, and the same one on every return. Built, not flown. The key comes from the
sector's universe map x/y ([sector-identity.md](../reverse-engineering/sector-identity.md) section 3, static analysis):
`key = (x + 1) * 0x10000 + (y + 1)`, never 0 for a known sector, so key 0 keeps meaning "no sector" and draws the
mark's own field (the field of every build before this one). The field seed is `generator_seed(mark_seed, key)`.

**Reader.** `src/proxy/sector_key_core.h` (portable, no CRT, no float) holds both editions' byte windows (the
verifier's 13 AP and 11 FL windows, 437 / 444 bytes, the 6 FL HIGHLOW fixups rebased from the module base), the VM
global (`0x006085e4` AP, `0x005aa174` FL) and the native-id member (m34 AP, m36 FL), and the bounded read of section 3,
steps 4 to 10. `src/proxy/sector_key.cpp` is the edition part, compiled in both DLLs. At the first sample it admits the
image (`object_trace::executable_verified()` on AP, `fl::verified()` and `fl::module_base()` on FL) and compares every
window over its whole length through `engine_patch::read_code`. A mismatch turns the key off for the process: every
read is then `unknown(disabled)` and the pass keeps drawing the key-0 field. It never refuses the pass. The chain is
read through `engine_memory::read`: one header span `[S+8, S+0x9e)` (class 1, `0xcafe`, id != 0, `[S+0x94]` < 0),
then, on a cache miss only, the VM global, `[VM+0x12d0]`, the map header (power-of-two capacity <= 2^22), at most 64
chain nodes, the object (`+0` == kc), its class record (2001), the m0/m1 cells (int, 0..255) and the native-id cell
(int, == `[S+8]`). The cache is keyed on `(S, [S+8], [S+0x94])`. Every refusal names its reason and clears the cache.
No hook, no patch, no engine call.

**Start.** The reader starts from the cockpit's sector `[cockpit+0x54]` of `sector_background::sample`, not from the
player root. That sample is already admitted on both editions (the FL anchors of `sector_background_fl_inc.h`, the AP
executable identity). It is the sector whose background is being drawn, so it is the right seed source in a remote
view. It is also already read once per frame (fog default on; `capture.cpp` `sector_background_context` now also runs
for `sky_stars`), so the start costs no new reads. The reader uses the sample's sector whenever its class word read
as 1. An empty background table (`Loading`) is `unknown(loading)` and drops the cache, as does a gap of 250 ms or more
between samples (a load stall), so a load walks the chain again even when `(S, id, kc)` repeat.

**Route.** `MotionOutput::sky_stars_sector_sample` (render thread, once per frame) applies the hold rule
(`sector_key::core::Hold`): a known key that differs from the held one becomes the held key, and `unknown` keeps it.
The matched star body calls `SkyStarsPass::prepare(seed, look, held key)` on the render thread, where the rebuild ran
before. Its cached-build check is now `sky_stars::core::same_build(seed, look, key, ...)`, so a key change rebuilds
exactly once and a return to a sector rebuilds that sector's field. Main menu and game load: the cockpit sample has no
class-1 sector (`no_sector`), or the table is empty (`loading`), or a chain step refuses. In every case the read is
`unknown` and the pass draws the held field whenever a marked star body matches: the field of the last known sector,
or the key-0 field before any sector is known. It never draws nothing because of the reader. Whether the menu scene has
a cockpit sector is not established (sector-identity.md section 5); the first `sky_stars_sector` rows answer it.

**Evidence (measured).** Host reader check (`verification/analysis/test_sector_key.py`,
`verification/probe/sector_key_check.cpp`, 53 checks): both editions' bindings, 30 refusal cases (5 header refusals
end a warm cache; edits past the header are seen only after a cache miss), object at chain node 64 found and at node 65
`kc_limit`, a cycle ends at the cap, a freed S (`0xefac`) then reused with a new id and KC id is walked again, the hold
rule, and the windows at the preferred base and rebased (an unrebased FL image refuses at `player_root`). The test also
compares the core's tables with the verifier's `WINDOWS` and with `summary.json`'s fixup offsets. Route
(`motion_sky_stars_fixture.cpp`, 12 scenarios, 106 checks): key 0 before a sector, a rebuild on a known key, the cached
build on the same key, the held field through `kc_missing`, `no_sector` and `loading` (one cache drop), a second sector
and the return each rebuild once (the stall drops the cache), one row per change, `key=` on the ready row, the phase
rows one-shot per field and capped at 16. Native fixture (`fixture_out.txt`, RTX 5070 Ti, plain D3D9): a keyed prepare
rebuilds once and matches `build()` with that key (twinkling 1,571, candidates 22,826); the drawn keyed buffer has all
489 isolated stars within 1 % of that build's energy; key 0 rebuilds the mark's field. Cost of the reader through the
production `engine_memory::read` (20,000 reps): cached check 36 ns warm (the region already validated in the frame, as
after the cockpit sample's reads of the same sector), 448 ns on the first touch after `next_frame()` (one VirtualQuery),
full walk 2.2 us on a sector change. A field rebuild stays 1.6 ms (6,240 stars) per sector change, not per frame.

**Phase rows.** The rows are read-only evidence for the open "stars dimmer under the nebula" question. A field's first
match arms the next background phase, which logs one `sky_stars_phase_draw` per draw (at most 16). The star body's row
adds its `c0-c3` rows as submitted (the background VS `7b6393fe` writes `oPos` from c0-c3,
[rigid-position-profiles.md](../reverse-engineering/rigid-position-profiles.md)) and the background camera's rotation
`r`. A ring of the last 64 logged fields keeps the rows one-shot per field. When idle the cost is one compare per draw
and one per matched draw.

## Decision C: nebula edge banding and stars under the nebula (2026-11-05)

Design note (Fable), after FL runs 52/53. Two pilot requests about the background phase: (1) the edge between black
space and the nebula shows bands; (2) the stars shine through the nebula at full strength. Nothing here is implemented.
Measurements: `verification/results/sector-background-textures/sky_dark_steps.py` -> `sky_dark_steps_fl.txt` (this
section), the `sky_stars_phase_draw` rows of `C:\X3FL\x3m.log` (run 52/53), and the facts above.

**Outcome.** (1) Deband the four nebula part draws at draw time in texture space: swap the engine's SM1.1 pair
`7b6393fe`/`6109cf64` for an own vs_3_0/ps_3_0 pair that reproduces its documented math and samples the DXT1 part
with a gradient-aware average (radius in texels, soft threshold, direction from a texel-indexed hash, no dither), writing
the smooth ramp into the FP16 target; recognised by pair, phase, blend and texture size, no copy, no header mark.
(2) Draw the star batch at the end of the background phase (in `before_clear` of the depth-only Clear, after
`restore_bindings()`), Z-tested at the far plane so planets still occlude it, and dim each star by the nebula radiance
read from a 1/8-size linear `StretchRect` of the HDR target taken just before the batch: `energy x (1 - k x
saturate(L / L0))`. Dimming off (`k = 0`) keeps today's draw position and takes no copy.

### Diagnosis (measured on the stock FL parts unless marked)

The phase in a four-part family (bluewell at frames 772 and 21296; two fields logged, each 5 draws): the star body
(`1024x1024`, blend off, 768 primitives), then four `2048x1024` parts with `SRCALPHA/ONE`, Z off, mask 7, 992
primitives each. The third logged field (frame 29693, a fog-card family) has no parts: the star body, two Z-tested
Z-writing `1024x1024` draws (a planet, blend off), a `64x64` additive draw and two fog cards (`5/6` and `3/2`). The
parts' PS returns the sampled RGB and `tex.a x c0.x` (`g_AlphaValue`, 0.996 in the AP captures;
[background-temporal-coverage.md](../reverse-engineering/background-temporal-coverage.md)); DXT1 alpha is 1, so each
part adds `0.996 x texel` to the target. The parts are a partition of the sphere (inferred from the 992-primitive pieces
and the wrap-column statistics; no overlap measured), so the additive blend sums one part per pixel and is not a banding
source except along the seams.

Where the step comes from. In the bluewell and reddawn parts (8 of 8) the first non-zero luma beside a black texel is 1
(median and p90; share >= 3 at most 2.1 %): the edge of the nebula is the smallest 8-bit step, not a 565 jump. Inside the
dark band (luma 1..24, 9-58 % of the texels) the per-channel values are the RGB565 ladder with its DXT1 thirds: R and B
take `3 5 8 11 13 16 19 21 22 24`, G takes `1 3 4 5 7 8 9 11 12 ...`, so a red or blue step is 2.7/255 where the painting
had 1/255. Identical-luma runs in the dark band are 2.3-3.0 texels. At the 2.5x magnification of 5120x1440 each run is
a 6-7 px plateau joined by a 2.5 px bilinear ramp, and the plateaus differ by 1-3/255 in the target's pre-decode domain
(the engine writes the texel as is; the proxy decodes with gamma 2.2 at display time). The exposure in run 52/53 sat
at EV +1.23 median, +1.3 p90 and cap (1,208 `hdr_frame` rows), a linear gain of 2.35 that lifts a pre-decode step by
about 1.47x on the display; the AgX curve then maps the shadows near-linearly. So the bands are the 8-bit source
quantisation, coarsened 2.7x in R/B by the 565 endpoints, made 6-7 px wide by the magnification and 1.5x taller by the
exposure. The FP16 target (10-bit mantissa) and the four-part sum contribute nothing; the 8-bit display store
re-quantises at 1/255 and is a separate, smaller contributor (the writeback dither `X3M_HDR_DITHER`, default off,
exists for it).

What a remedy must do: reconstruct the slow gradient across several plateaus (a filter radius of at least 3-4 plateaus,
so 8-16 texels or 20-40 screen px), keep real edges (the nebula filaments, a planet limb, a sun disc), and write the
result at a precision above 8 bits. A dither alone cannot fix it: the staircase is in the source, not in a
re-quantisation. Writing into FP16 needs no dither at all; the only re-quantisation left is the 8-bit display store.

### Options

- **(b) Texture-space deband at the part draws (chosen).** The filter sees the original texels and their steps, so
  the threshold is in texel units (the 565 ladder is at most 3/255 per step) and the radius in texels, independent of
  FOV and resolution; it touches only the four part draws, never stars, planets, suns or fog cards; no copy, no extra
  target traffic, 9 DXT1 taps per sky pixel. It needs a replacement program pair (D3D9 requires vs_3_0 with ps_3_0,
  so both) and a recognition rule for the draws.
- **(a) Screen-space deband at the phase end.** A copy of the FP16 target (5120x1440x8 B = 59 MB, not 29; read and
  write about 118 MB, inferred 0.15-0.3 ms) plus one 7.4 Mpx pass with 9-17 taps (inferred 0.3-0.5 ms); the radius must
  be 20-40 screen px to span the magnified plateaus and scales with resolution; it filters everything the phase drew
  (planet surfaces, sun halos, fog cards) under one threshold; the stars must be drawn after it (their dim tails,
  0.01-0.03 pre-decode at the kernel rim, sit under any useful threshold and would be averaged into the black). It is
  the fallback if the pair swap of (b) is refused by a runtime, and it shares nothing with (2) once (2) uses the
  1/8-size copy. Loses on cost, scope and resolution dependence.
- **(c) Offline uncompressed 8-bit re-encode with deband and dither.** Removes the 565 ladder but keeps 8-bit steps
  at 6-7 px, so it must dither, and Decision B showed a dither at texel scale magnified 1.75-2.5x reads as a dot
  pattern; 8 MiB per part (32 MiB per sector), a regenerate run for every user, and derived copies for mod parts.
  Loses on faithfulness and size.
- **(d) Combination.** (b) plus the existing display dither on (the 8-bit store), plus (2). This is the outcome; (a)
  is not combined with (b) because the plateaus are gone after (b).
- *For (2), keep the draw position and accept.* No cost, but the pilot asked. *`INVDESTCOLOR` blending.* No `k`, and
  negative on an FP16 sum above 1 (a sun disc under a star): rejected in Decision A, still rejected.
- *For (2), a same-size copy as the radiance source.* 59 MB per frame for a smooth signal; the 1/8-size linear
  `StretchRect` (640x180, 0.9 MB) averages over an 8x8 block, which is what the dimming wants (no speckle), reads the
  target once (inferred 0.1 ms) and costs nothing per star beyond four bilinear taps. The downscale is the "cheaper
  proxy".

### Chosen design

**C1, nebula deband.** `MotionOutput::before_draw` (the star route's hook, one bool test with the setting off): a
draw matches when the selector is in `Background`, the HDR latch holds (`hdr_state_ == Active`, FP16 target, no
MSAA), the shadow's pair is `7b6393fe2d3e1d85`/`6109cf64c03529dd` (the fog cards match by pair the same way), blend
on with `SRCALPHA/ONE`, Z test off, mask 7, only RT0 with a full viewport, stage-0 texture width >= 1024 and not a
star mark (payload 2). This admits FL's 2048x1024 and 2048x2048 parts and AP's 2048x1024 uncompressed, 2048x2048
and 1024x512 DXT5 parts, and excludes the 64x64 additive sun draw, the planet (blend off, Z on), the fog cards (other
pairs) and every UI use of the pair (outside the phase). On a match the route sets its own pair, sets `c255 =
(jitter x 2/W, -jitter y 2/H, 0, 0)` when `jitter_active_` else 0 (the batch's rule), forwards the draw, and restores
the pair and c255 (the batch's Get/Set/restore bracket; a failed restore latches `motion_state_lost_`). The engine's
constants c0-c5 (VS) and c0 (PS) are read as set; the stage-0 sampler state stays the engine's (LIN/LIN, no mip).

*Programs.* `sky_nebula_vs` reproduces the SM1.1 VS: `oPos = pos . c0-c3`, `oT0 = affine(uv, c4, c5)`, plus the
jitter term `oPos.xy += c255.xy x oPos.w`. `sky_nebula_ps`: centre `c = tex2D(s0, uv)`; for i in 1..2: direction `d`
from `hash(floor(uv x size) + i)` (texel-indexed, so static in texture space), radius `r_i = radius x i / 2` texels,
four taps at `uv + r_i x {d, -d, perp d, -perp d} / size`, `m` their mean, `diff = max over the four of
max_channel |tap - c|`, weight `w = saturate(1 - diff / threshold)`, `c = lerp(c, m, w)`; output `(c.rgb, tex.a x c0.x)`.
Threshold and radius are ps constants `c200` (a register the SM1 pair cannot touch), set once at attach and on a
setting change, not per draw. No grain: the result goes to FP16. The texel-indexed hash keeps the residual ripple
attached to the sky, so the far-plane TAA reprojection follows it (the jittered draw changes the UV per screen pixel
by less than a texel; `floor` keeps the direction fixed). A per-frame seed would instead leave 0.23x of its amplitude
as flicker through the 0.9 history (alpha / (2 - alpha)) and is not used. Expected residual: with radius 12 texels
and plateaus of 2.5, a step's ripple is about 2.5 / (2 x 12) = 0.1 of the step (inferred, fixture-measured).

*Jitter.* The native part draws are unjittered (inferred from the unrouted pair). The replacement VS applies the
frame's jitter like the batch so stars and nebula move together under TAA and the resolve supersamples the bilinear
magnification. Developer `sky_nebula_jitter=0` turns it off if the flight shows a sky wobble.

*Display store.* The flight runs with `X3M_HDR_DITHER=1` (existing static +-0.5 code display dither, post-TAA, no
temporal term). If the pilot accepts C1 with it, its default follows C1's.

**C2, stars under the nebula.** With `sky_stars_nebula_dim > 0` the matched star body is masked as today but the batch
is not drawn there. In `MotionOutput::before_clear`, after `restore_bindings()` (RT1/RT2 unbound, RT0 the FP16
target) and before the Clear is forwarded, the route predicts the phase end: `selector_.state() == Background`,
`flags & D3DCLEAR_ZBUFFER`, `!(flags & D3DCLEAR_TARGET)`, `count == 0`, `pending_.depth == main_depth_`, HDR latched,
a star body matched this frame and the batch not yet drawn. Then it issues `StretchRect(RT0 surface, nullptr,
dim surface, nullptr, D3DTEXF_LINEAR)` into the 1/8-size A16B16G16R16F render-target texture created at attach (and
after Reset; never per frame; `D3DTEXF_POINT` when `StretchRectFilterCaps` lacks `MINFLINEAR`, the copy skipped and
`k` forced to 0 when both fail), and draws the batch with `ZENABLE TRUE, ZFUNC LESSEQUAL, ZWRITE off` at `z =
0.99999 w` against the phase's depth (the latching Clear's 1.0, planets written by their Z-writing draws), everything
else as today. The selector's rule 2 makes a full-viewport depth-only Clear in `Background` either the phase end or a
rejected frame, so the prediction never draws the batch into a live mid-phase; a frame rejected after the draw is
unwound as a masked star body in a rejected frame is today. Because every draw between the star body and the phase end
is additive or opaque-and-Z-written, the deferred sum equals the in-place sum where the planets do not cover
(FP16 rounding aside) and loses the stars where they do, which is the native order's effect.

*Dimming.* The star PS gets the batch's clip-space centre from the VS (`centre.xy / w x (0.5, -0.5) + 0.5`), takes
four bilinear taps of the dim texture 1 texel apart (a 16 px neighbourhood on screen), pre-decode luma `L = dot(rgb,
(0.299, 0.587, 0.114))`, and multiplies the energy by `1 - k x saturate(L / L0)`. `k = sky_stars_nebula_dim`, `L0 =
sky_stars_nebula_dim_level` in `c250 = (k, 1 / L0, 0, 0)` (joins the c251-c255 bracket: 52 device calls per batch).
Over black the factor is 1; the brightness law, the peak cap and the twinkle are unchanged (the factor multiplies
the energy after the cap, so it only lowers peaks). The exposure meter cannot rise (less light). TAA: the factor
varies with the star's position over the nebula at the sky's own spatial frequency, so a drifting star changes
smoothly; no per-frame term.

### Invariants to validate

1. *Identity.* With `threshold = 0` the replacement pair's output equals a fixture-authored vs_1_1/ps_1_1 pair of the
   documented math (`oPos` from c0-c3, affine UV, RGB and `a x c0.x`) on a DXT1 texture at 2.5x magnification:
   0 differing pixels (native D3D9 fixture). The exact SM1 bytecode semantics (affine form of c4/c5, output
   registers) are confirmed by disassembling the local 512 B / 304 B programs before the VS is written.
2. *Banding removed, edges kept.* A synthetic DXT1 part with plateaus of 2-4 texels and 1/255 and 565 steps, rendered
   at 2.5x: ripple (max second difference along the gradient) <= 0.25/255 and error against the ideal ramp <= 0.5/255
   after C1; a 12/255 edge keeps its height within 1/255 over the whole radius (no halo); no texel farther than
   `radius` from a non-black texel becomes non-zero.
3. *Match scope.* Row `sky_nebula parts=4` per phase in bluewell, `parts=0` in a fog-card family, `outside=0`
   (pair bound outside the phase is never swapped); AP's uncompressed and 2048x2048 parts match.
4. *State restore.* The pair and c255 are back after a clean draw, an injected draw failure and an injected restore
   failure (the star route's `motion_sky_stars_fixture.cpp` pattern, 0 mismatches).
5. *Cost.* Full-sky frame at 5120x1440: C1 <= 0.5 ms GPU (inferred 0.2-0.3), the dim copy <= 0.2 ms (inferred 0.1),
   batch CPU <= 1 us as today; no per-frame allocation (attach holds the dim texture; detach releases to 0).
6. *Commutativity and occlusion.* The deferred batch over additive quads equals the in-place batch within 1 FP16 ulp;
   against a Z-written disc at depth 0.5 no star pixel is written inside the disc; in flight no stars on a planet disc.
7. *Prediction.* Row `sky_stars_defer predicted= confirmed=`: `confirmed / predicted >= 0.999` over a flight (the
   selector's `Background -> Scene` transition in `after_clear` confirms each prediction).
8. *Dimming law.* Over a flat quad at pre-decode luma L the star energy is `1 - k saturate(L / L0)` of the black-sky
   energy within 1 %; `k = 0` reproduces today's batch pixels bit for bit at the old draw position.
9. *TAA.* The fixture's resolve model (60 frames, 0.5 and 10 px/frame pans) over a debanded gradient: per-pixel
   temporal standard deviation <= 0.1/255; flight: no shimmer or crawling in the nebula under SETA rotation.
10. *Native.* Both pairs pass the native validator (ps_3_0 slots logged); every call is documented D3D9
    (`SetVertexShader/SetPixelShader`, `SetVertexShaderConstantF`, `StretchRect` RT to RT texture with
    `StretchRectFilterCaps` checked, render states); the FL log proves the phase end on FL, the selector note on AP;
    CrossOver runs the same calls; the user's native flight is the behavioural proof.

### Settings

| Setting | Default | Range | Effect |
| --- | --- | --- | --- |
| `sky_nebula_deband` | 1 (flown 2026-11-05, FL run 55) | 0/1 | C1 on; independent of `sky_stars` |
| `sky_nebula_deband_threshold` | 6 | 1-16 (1/255 units) | soft threshold on the max channel difference of a tap set |
| `sky_nebula_deband_radius` | 12 | 2-32 texels | outer radius; the inner iteration uses half |
| `sky_nebula_jitter` (developer) | 1 | 0/1 | TAA jitter on the replaced part draws |
| `sky_stars_nebula_dim` | 0 (flown 2026-11-05 at 0.5: not noticeable to the pilot; stays off) | 0-1 | `k`; 0 keeps today's draw position and takes no copy (`requires=sky_stars`) |
| `sky_stars_nebula_dim_level` (developer) | 0.5 | 0.1-2 | `L0`, pre-decode luma of full dimming |

### Cost (inferred unless marked)

C1 per frame: at most four extra `SetVertexShader/SetPixelShader` pairs, one constant Get/Set/restore bracket and the
match test; GPU 9 DXT1 bilinear taps per sky pixel (the DXT1 parts, 1 MiB each, sit in cache), 0.2-0.3 ms at
5120x1440 with the sky filling the screen, bounded by invariant 5. C2 per frame: one `StretchRect` reading 59 MB and
writing 0.9 MB (0.1 ms), four taps per star pixel (<= 100 k pixels at density 2: under 10 us), the batch as before
(measured 2.2 us upper bound, `draw()` CPU 0.75 us). No per-frame allocation in either. SETA has no bearing.

### Kill criteria

C1: visible halos, smeared filaments or a crawling pattern in the flown sector, not fixed by one threshold/radius
adjustment, or a fixture cost above 0.5 ms, turns the default off; a second failed adjustment removes the pair swap and
leaves (a) as the only recorded alternative. C2: stars visible on a planet disc, a brightness pulse of stars crossing
nebula structure under SETA rotation, or a prediction ratio below 0.999, not fixed by one law/L0 adjustment, sets
`sky_stars_nebula_dim` default 0 (the old draw position is the `k = 0` path); the dim texture and the deferral code go
with it.

### Implementation outline (files)

- New `src/proxy/motion_output_sky_nebula_inc.h` (match, swap, bracket, rows `sky_nebula_config`, `sky_nebula parts=
  outside= swapped= draw= restore=`), `src/renderer/sky_nebula_pass.{h,cpp}` (programs, attach/detach/Reset, constant
  bracket), `src/effects/sky_nebula_{vs,ps}.hlsl` and `tools/shaders/generate_sky_nebula_programs.py` (as the star
  generator; native D3DX compile, `*_inc.h` output).
- `src/proxy/motion_output_sky_stars_inc.h`: `sky_stars_phase_end_draw()` called from `before_clear` after
  `restore_bindings()` under the prediction; the dim copy; the `k = 0` path unchanged. `src/renderer/sky_stars_pass.{h,cpp}`:
  dim texture (1/8 size, FP16 RT, created at attach and after Reset), `StretchRect`, Z states, c250; `src/effects/
  sky_stars_{vs,ps}.hlsl`: centre passthrough and the dim taps. `src/proxy/motion_output.cpp`: one call in `before_clear`,
  the confirmation counter in `after_clear`; `motion_output.h`: members. `src/config/config_schema_inc.h` and `capture.cpp`:
  the six settings.
- Tests: `verification/analysis/test_sky_nebula.py` (hash static in texture space, settings parse), the route fixture
  `verification/probe/motion_sky_stars_fixture.cpp` (prediction, deferral, k = 0 path, restore faults), the native D3D9
  fixture under `verification/results/sky-nebula/` (invariants 1, 2, 4, 5, 6, 8, 9) and `native/validate.py` (10).
- Disassembly before coding: the local bytes of `7b6393fe` (512 B) and `6109cf64` (304 B) for invariant 1 (the
  `disassemble` agent; findings into `background-temporal-coverage.md`, no bytes tracked).
- Flight: one FL flight in Argon Prime with `sky_nebula_deband=1`, `X3M_HDR_DITHER=1`, `sky_stars_nebula_dim=0.5`;
  rows `sky_nebula parts=4`, `sky_stars_defer confirmed=`; pilot: smooth edge, no halos, stars dimmer in the bright
  nebula, none on a planet disc, no shimmer under SETA.

### Unknown

Whether the parts overlap anywhere (only the seam behaviour depends on it; the first flight's look answers it). The
exact SM1 program semantics beyond the coverage note (invariant 1's disassembly). Whether `StretchRect` with
`D3DTEXF_LINEAR` from an FP16 RT is granted on the user's driver (the caps check and the `copy=` row answer it; point
filtering is the fallback). The GPU figures are inferred until the native fixture measures them.

### Implementation C1 (2026-11-05): nebula part deband

Built, not flown; `sky_nebula_deband` default 0. Evidence: `verification/results/sky-nebula/` (`run_fixture.py` ->
`fixture_out.txt`, `native/validate.py` -> `native/result.json`); host test `verification/analysis/test_sky_nebula.py`
(`verification/probe/sky_nebula_check.cpp`, 13 checks). The pair's bytecode is recorded in
[background-temporal-coverage.md](../reverse-engineering/background-temporal-coverage.md) ("The background pair's
bytecode").

**Pieces.** `src/proxy/sky_nebula_core.h` (pair, settings, c200-c201/c255 packing, the tap pattern field),
`src/renderer/sky_nebula_pass.{h,cpp}` (programs; `begin` reads VS/PS, VS c255 and PS c200-c201 before the first write,
sets them and the pair, undoes a partial write at once; `end` restores in reverse order), `src/proxy/
motion_output_sky_nebula_inc.h` (match, swap, rows), `src/effects/sky_nebula_{vs,ps}.hlsl` compiled by
`tools/shaders/generate_sky_nebula_programs.py`. Settings `sky_nebula_deband`, `sky_nebula_deband_threshold`,
`sky_nebula_deband_radius`, developer `sky_nebula_jitter` and `sky_nebula_deband_hash` (default 0), read once at process start (a refused value keeps the
default, `<name>_setting=default` in `sky_nebula_config`).

**Match (changed from the design).** As designed, plus two refusals the bytecode made necessary: `FOGENABLE` on (ps_3_0
has no fixed-function fog) and stage 0 `D3DTTFF_PROJECTED` (honoured by an SM1 `tex`, not by `texld`); `outside=`
counts pair-and-state matches outside the phase or the latch, never swapped. The texture size is cached per pointer and
identity. PS c200-c201 are bracketed per draw (Get/Set/restore) instead of set once: the size differs between parts
(2048x1024, 2048x2048, 1024x512) and no other user of c200 is assumed. 12 device calls per part draw (8 in `begin`, 4 in
`end`), CPU 0.38 us per bracket, 1.53 us for four parts (measured, `CPU` row).

**Filter (changed from the design).** The note's formula (`diff` = largest tap-minus-centre difference, two sequential
rings, independent hash per ring) left the synthetic ramps' ripple unchanged (1.81 -> 1.83/255, measured): on a real
gradient every tap at radius 12 differs from the centre by the gradient times 12, so the outer ring never averages.
The implemented program is gradient-aware: `diff` is the largest channel difference between the midpoint of an
opposite tap pair and the centre (0 on any linear gradient, half the height across an edge), over the four pairs of the
same eight taps (ring 1 at R/2 along d and its perpendicular, ring 2 at R along d rotated 45 degrees); `w =
saturate(2 (1 - diff / threshold))`; output `lerp(c, m, w)`, m the mean of the centre and eight taps weighted by each sample's alpha (the centre when every alpha is 0; review 2026-11-05: AP's DXT5 and uncompressed parts must not average in the colour of transparent texels; DXT1 alpha is 1, the plain mean). The angle of d is a quarter
turn times the texel-indexed hash interpolated between the four nearest texel centres (static in texture space,
continuous in uv) when `sky_nebula_deband_hash = 1`; by default (0, `c200.w = 0`) the star is fixed, d = (1, 0), for
every texel (follow-up 2026-11-05: the hash's rotation is texel-scale grain, the pilot's visible residual). 120 ps_3_0
slots, 9 taps.

**Fixture (native D3D9, RTX 5070 Ti, plain D3D9, measured).** Synthetic 2048x1024 DXT1 part, 2.5x magnification:
- Invariant 1: the reference vs_1_1/ps_1_1 pair has the engine's instruction tokens; threshold 0 against it with a
  perspective c0-c3, an affine c4/c5 with rotation and offset and a non-black SRCALPHA/ONE target: 0 of 921,600 pixels
  differ (285,615 written).
- Invariant 2, radius 12 (ramp interiors, three rows, every channel, 1/255 units; before = the engine pair: rms 1.03,
  max 3.89, ripple 1.81):

  | Configuration | rms error | max error | ripple | height at +-radius | change beyond 1 texel of an edge | island spread |
  | --- | --- | --- | --- | --- | --- | --- |
  | threshold 6, hash off (default) | 0.456 | 2.49 | 1.09 | 0.000 | 1.26 | 9.3 texels, sum -0.03 % |
  | threshold 6, hash on | 0.428 | 2.36 | 1.98 | 0.000 | 0.67 | 12.9 texels, sum +0.96 % |
  | threshold 10, hash off | 0.439 | 1.62 | 0.78 | 0.000 | 3.04 | 9.3 texels |
  | threshold 16, hash off | 0.439 | 1.62 | 0.78 | 0.000 | 5.13 | 9.3 texels |

  Band means move by at most -0.025. Accepted shortfalls pending the flight (fixture `WARN` rows, `checks=21
  warnings=2 failures=0`): `edge_halo_beyond_1_texel` 1.26 vs the bound 1.0 (hard ceiling 2.0, a failure above it);
  `taa_sd_fast_pan` 0.662 vs 0.649 (ceiling 1.25 x the engine pair's, 0.772). A 256x256 DXT5 part with an alpha
  edge (transparent 16/255 beside opaque 8/255): output change against the engine pair 0.000 on the transparent side
  and 0.49 on the opaque side beyond one texel of the edge (bound 1/255). The
  hash-off default misses these two of the fixture's own bounds: the change beyond one
  texel of a 12/255 edge is 1.26 (bound 1.0; the fixed star's diagonal ring keeps a pair whose far tap lands in the
  edge's bilinear texel), and the fast-pan TAA figure below. The note's bounds (ripple <= 0.25, error <= 0.5) are not
  met in any configuration, and the ripple bound is not reachable with this tap set: the output is the centre plus
  eight bilinear taps, so every DXT1 step a tap crosses (up to 3.6/255 at a block boundary in G) moves the result by
  step / 9 = 0.4/255 over one texel (2.5 px), a second difference of about 0.16 per crossing; on a ramp several taps
  cross steps at neighbouring pixels and the measured floor at full weight is 0.78 (thresholds 10 and 16 agree).
  Reaching 0.25 needs about three times as many independent taps or a box prefilter, i.e. a different filter, not a
  setting. At threshold 6 the weight is partial on the staircase (diff up to about 3/255), which adds the switching
  between filtered and unfiltered pixels (1.09); the hash adds texel-to-texel pattern jumps (1.98).
- Invariant 4: programs, VS c255 and PS c200-c201 back after a clean bracket, an injected draw failure, an injected
  begin fault (undone inside `begin`) and an injected restore fault (reported, every restore still attempted): 0
  mismatches in each.
- Invariant 5: full-sky frame at 5120x1440 (four parts, each a quarter of the screen), event-flushed over 100 frames:
  engine pair 0.060 ms, deband pair 0.089 ms, extra 0.029 ms (bound 0.5 ms; the record of the last run).
- Invariant 9 (model: history 0.9, exact bilinear reprojection, 3x3 clip, Halton jitter through c255, ramp interiors):
  per-content-pixel temporal SD, hash off, at 0.5 px/frame 0.056 max (engine pair 0.123); at 10 px/frame 0.662 (engine
  pair 0.618, the model's own reprojection error; the fixture's bound 1.05 x the engine pair's, 0.649, is missed). With
  the hash on: 0.041 and 0.639.
- Invariant 10: `sky_nebula_vs` 118 words / 10 slots, `sky_nebula_ps` 546 words / 120 slots, native validator End()
  S_OK, 0 messages.

**Rows.** `sky_nebula_config requested= enabled= reason= threshold= threshold_setting= radius= radius_setting= jitter=`
once; `sky_nebula_device attached= reason= result= vs_slots= ps_slots= max_ps_slots= references=` at attach;
`sky_nebula_match index= texture=WxH format= fog= target= threshold= radius= jitter= result= c4= c5=` for the first four
swapped draws per device (c4/c5 when the engine-effects hook shadows them); `sky_nebula status=<none|swapped|partial|
refused> parts= swapped= reason= draw= restore= outside= frames= swapped_frames= refused= us= us_mean= us_max=
threshold= radius=` at the depth-only Clear when parts, swapped or the reason changed (at most 1,024 per device); the
`us` fields are the bracket's CPU cost (invariant 5's CPU side).

**Flight-only.** Invariant 3 (`parts=4` in bluewell, `parts=0` in a fog-card family, AP's parts matched), the look
(halos, smeared filaments, residual grain at 2.5x, seams), invariant 9 under SETA rotation, and native Windows.

### Implementation C2 (2026-11-05): phase-end star batch with nebula dimming

Built, not flown; `sky_stars_nebula_dim` default 0 until flown (review 2026-11-05; the flight's ini sets 0.5), like
`sky_nebula_deband`. Evidence:
`verification/results/sky-stars/` (`run_fixture.py` -> `fixture_out.txt`, section 5; `native/validate.py` ->
`native/result.json`, now four programs); route fixture `verification/probe/motion_sky_stars_fixture.cpp` (17
scenarios, 138 checks; `test_motion_sky_stars.py`).

**Pieces.** `src/effects/sky_stars_dim_{vs,ps}.hlsl` are one-line includes of the in-place sources with `SKY_STARS_DIM`
(the VS passes the centre's UV in the dim copy and the law; the PS takes four bilinear taps half a dim texel around it);
without the define the in-place programs compile to the same words as before (sha256 `40a8cb78...` / `9ac5e7fe...`,
unchanged). `SkyStarsPass` gains the dim side (`caps().dim`, checked at attach: `D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES`,
`StretchRectFilterCaps` MINFLINEAR else MINFPOINT, `D3DUSAGE_QUERY_FILTER` on A16B16G16R16F for linear sampling; a refusal
leaves the pass on and the route keeps the in-place batch), `prepare_dim` (a D3DPOOL_DEFAULT A16B16G16R16F render-target
texture of ceil(W/8) x ceil(H/8), created at the first deferred batch and again after Reset or a size change, released by
`before_reset` and `detach`), `copy_dim` (`StretchRect` of the HDR target) and `draw_deferred` (the batch with c250-c255,
`ZENABLE TRUE`, `ZFUNC LESSEQUAL`, `DEPTHBIAS` and `SLOPESCALEDEPTHBIAS` 0 appended to the touched states, stage-0
texture and six sampler states saved, set and put back). A dim texture creation that fails without a device loss
latches `dim_unavailable()` until Reset: no retry per phase end, the in-place batch meanwhile, one `sky_stars_defer
why=dim_unavailable` row. `draw()` keeps its call sequence (50 calls, measured). Settings `sky_stars_nebula_dim` (k, 0-1) and developer
`sky_stars_nebula_dim_level` (L0, 0.1-2), both `requires=sky_stars`, tagged in `sky_stars_config`.

**Route.** As designed: with k > 0 and the dim side the matched star body is masked and the batch pends; `before_clear`
(after `restore_bindings()` and the Clear's bindings) predicts the phase end with the selector's own rule
(`SceneBoundarySelector::predicts_phase_end`, the `depth_clear` that `advance()` applies: flags exactly
`D3DCLEAR_ZBUFFER`, no rectangles, z 1, a full 0..1 viewport on the latched pair, after a background draw), then requires
the phase's depth (`main_depth_`), the latch,
RT0 only and the matched viewport (else `reason=depth|latch|targets|queries|composition`, no batch). A failed copy draws
with k = 0 (still Z-tested); a dim texture that cannot be created draws the in-place programs there. Confirmation is
the Background -> Scene transition (`sky_stars_phase_end`); an open prediction at the next latching Clear is
`unconfirmed`, a pending batch never drawn `missed`. **Changed from the design:** `c250 = (k, 1/L0, 0.5/dim width,
0.5/dim height)` (the PS needs the tap offset and has no constant of its own); 72 device calls per deferred batch
(measured; the design said 52).

**Fixture (native D3D9, RTX 5070 Ti, plain D3D9, measured).**
- Invariant 8: over a flat copy at L = 0.251 and L = 0.502 (= L0) with k = 0.5, every one of 464 isolated stars carries
  `1 - k saturate(L / L0)` (0.749, 0.500) of its black-sky energy within 0.14 %. k = 0 in production is the in-place
  `draw()` itself; the deferred twin at k = 0 over black equals it on all 48,431 lit channels (0 differing).
- Invariant 6: the deferred batch after six overlapping additive quads against the in-place order: 3,011 channels
  differ, at most 2 FP16 ulp (the note asked 1; each ONE/ONE blend rounds, so a reordered sum of three or more terms can
  differ by one ulp per rounding). A disc written at depth 0.5: 0 star pixels inside, 11,237 outside.
- The copy filter: `D3DTEXF_LINEAR` at the 8x downscale (1024x512 -> 128x64) is a bilinear sample at each 8x8 block's centre, not
  an 8x8 average (one lit pixel per block at (3,3): every dim texel 0.250, the box mean is 0.0156). The dim factor so
  sees the central 2x2 of each block; small bright features alias (a flight watch item: a brightness pulse of stars
  over fine nebula structure under SETA rotation is the kill criterion).
- Invariant 4: stage-0 texture, the six sampler states, ZENABLE/ZFUNC and c250-c255 back after a clean batch, a draw
  fault and a restore fault: 0 mismatches.
- Invariant 5 at 5120x1440: the copy 0.0077 ms, the deferred batch 0.0041 ms (event-flushed upper bounds over 100),
  `draw_deferred` CPU 2.15 us (72 calls; above the note's 1 us); the dim texture 640x180 is created once; `before_reset` releases it (7 objects held, 9
  with the texture).
- Invariant 10: `sky_stars_dim_vs` 197 words / 33 slots, `sky_stars_dim_ps` 165 words / 23 slots, native validator
  End() S_OK, 0 messages.

**Rows.** `sky_stars_config` gains `nebula_dim= nebula_dim_setting= nebula_dim_level= nebula_dim_level_setting=`;
`sky_stars_defer why=<first|status|periodic|unconfirmed> predicted= confirmed= ratio= missed= unconfirmed= k= level= dim=
copy=<linear|point|failed|no_texture|none> copy_hr= dim_texture=WxH dim_linear= dim_reason= copy_us= draw_us=` at the
first deferral, with each `sky_stars` row, on an unconfirmed prediction and every 4,096 phase ends (at most 1,024 per
device); the `sky_stars` row's `reason=` names a phase-end refusal.

**Flight-only.** Invariant 7 (`ratio >= 0.999` over a flight), no stars on a planet disc in flight (a planet whose depth
lies beyond 0.99999 would let stars through), the look of the dimming and its stability under SETA rotation (the bilinear
copy above), FL and AP phase ends, and native Windows.
