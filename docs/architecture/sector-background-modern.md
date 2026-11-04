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

## Decision B: offline nebula bake

**Chosen: a `sky` row group in the LOD overlay batch.** `tools/analysis/sky_textures.py` (numpy only) bakes each
eligible part and star texture; `lod_overlay.batch` adds its members to the same `addon/NN` (AP) or `addon2/NN`
(FL) catalogues and records them in the `.x3m-lod.json` manifest as `sky: {recipe_id, tool_sha256, rows:
[{member, source, inputs_sha256, member_sha256, metrics}]}`. The engine's resolver takes `dds/<basename>.pck` from
the highest slot down, so a member with the stock name in the overlay slot wins; a selected mod package gets the
members through the existing derived `<mod>-x3m-lod` copy. Reuse goes through `reuse_previous` by
`inputs_sha256 = sha256(decoded source bytes ‖ recipe_id ‖ options)`; the sky tool file is deliberately not in
`TOOL_FILES`, so a sky recipe change rebakes textures, not bodies, and a body change does not rebake textures.
`lod_overlay_check` validates the slot by `overlay_sha256` as today; the fog step reads the full catalogue view
(overlay slots included; see Implementation); the fog check already refreshes the launch fingerprint after the overlay write.

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

## Implementation (B, 2026-11-04): design facts that proved wrong

Measured on FL `bluewell` and `avdustysol` (8 parts, 2 star textures). Scripts and outputs are in
`verification/results/sector-background-textures/`: `sky_bake_metrics.py` with `sky_bake_metrics_fl.txt`, and
`sky_encoder_bound.py` with `sky_encoder_bound_fl.txt`.

- **The answer-4 banding limits measure 8-bit repetition, not banding.** The exact unbanded ramp, rounded to
  8 bits, has a mean identical-luma run of 8.0 px. The near-uniform dark red `avdustysol` has a source plateau
  share of 81 % without any banding. Meeting run ≤ 1.5 px and plateau ≤ 10 % needs noise that survives the 2x
  box. Even per-texel RGB565 with ordered dither (looser than DXT1) only reaches run 1.49 px at a hue shift of
  4.4°, and run 2.09 px at 1.98°. Run ≤ 1.5 px, plateau ≤ 10 % and hue < 1° cannot all hold on real parts.
- **Limits redefined (2026-11-04, orchestrator decision; B continues).** The identical-luma run is informational
  only. The plateau share passes at ≤ 10 % or at ≤ 90 % of the source's share. Hue shift (chroma > 8) is ≤ 2.0°.
  PSNR ≥ 40 dB, zero violations 0 and wrap-column luma change ≤ 0.5 are unchanged. The limits live in
  `sky_textures.THRESHOLDS` and `failures()`. Under them, 9 of 10 textures pass. The one failure is
  `avdustysol` part 03 at plateau 0.7261 against a cap of 0.7256 (0.9 × 0.8062); that part is a near-uniform
  colour, so its plateau share measures flatness, not banding. A follow-up decision gives near-uniform textures (source plateau share ≥ 0.5) their own branch (below);
  with the final tool (`tool_sha256` b66a663e…) the result is 10 of 10.
- **The fog step does not read a pinned stock view.** `fog_families.py` builds `sfc.Assets(game, mods=...)` (lines
  642 and 760), the full view including the x3m-lod overlay slots, so it sees the sky members. That is harmless
  today, because the fog step is skipped on FL and the sky group is off on AP. Once sky is on for AP, `--check`
  would report changed texture inputs for families that use `--background-palette`.
- **Near-uniform plateau rule tightened (review F1).** At a source plateau share ≥ 0.5 the bake passes only when
  its plateau share does not exceed the source's and its run is at most a third of the source's.
  `sky_metrics_noop_check.py` shows untouched and nearest-neighbour bakes fail, except bluewell part 01
  untouched, whose source share 0.081 already meets the ≤ 0.10 branch.
- **The sky tool hash covers lod_atlas.py (review F2).** The sky group's `tool_sha256` hashes
  `sky_textures.py` and then `lod_atlas.py`, so a decoder or encoder change rebakes the textures but no body.
- **Hue < 1° is out of DXT1's reach on dark low-chroma parts.** The bake reaches 1.31-1.62° on `bluewell`
  (chroma 9-32) and 0.41° on `avdustysol`. The answer-3 recipe as written gives 3.4-4.2°: a per-channel guided
  filter, then ±0.5 LSB dither, then the PCA encoder.
- **Stage 3 changed.** The guided filter runs on luma and is applied as a gain on RGB, so hue is locked. A
  per-channel filter alone costs 2.0° on a `bluewell` crop. The filter weight also falls to zero over the r texels
  next to each border, which keeps the wrap columns: within 0.06 luma on all eight parts.
- **Stage 6 changed.** An 8-bit ±0.5 LSB dither before the PCA fit is erased by 565 endpoint rounding: flat
  blocks collapse to one colour, and re-encoding the untouched `avdustysol` source alone costs 5.9°. The dither
  now moves the palette level (±0.5 palette step, ordered 8x8 Bayer). Low-spread blocks get bracketing endpoints:
  the 8x8 corners of the two enclosing 565 cells, chosen for the line closest to the block mean. Blocks with a
  luma spread above 16 keep `lod_atlas._encode_color`.
- **Invariant 8 holds without refinement.** PCA encoder PSNR is 47.6 dB on `bluewell` and 40.65 dB on
  `avdustysol`. One least-squares refinement iteration adds 0.01-0.4 dB for +0.35-0.85 s per part, so it is not
  used.
- **Overlay growth is about a tenth of the estimate.** Gzip DXT1 at 4096x2048 stores 0.19-0.79 MB per real part,
  not 3.4 MB. The 8 parts store 3.44 MB against 1.10 MB stock, so FL's 232 parts add roughly 0.1 GB (inferred),
  not 0.9 GB.
- **Time and memory are above the estimates.** Bake time is 2.5-4.4 s per part with 4 workers (measured), and
  the worker peak is 614 MiB RSS, not under 0.5 GB.
- **The reuse key covers the whole source DDS.** `inputs_sha256` hashes the whole unpacked source DDS (header and
  every mip), not the decoded level 0. A star texture's output is those bytes plus a header mark.
