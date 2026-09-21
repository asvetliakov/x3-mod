# Volumetric sun fog and sector haze

Current direction: the [spatial production contract](#spatial-production-contract-2026-09-20)
supersedes the homogeneous algorithm and count-based scaling proposals below.
Source integration is in progress; production runtime and flight appearance are
not yet qualified. The earlier design and rejected experiments remain as history.

Original design note, 2026-09-19. Stage 1 was implemented default off; run 48 B
was flown and the user prefers strength 0.02. Card replacement is now ratified
(section "Card replacement design"); it supersedes the original stacking choice. Stage 0 (offline mock-up on real dumps) is done,
and was redone the same day on the first real fog-sector frames (run174, Argon Prime: section 2b, which
**revises the rule, the medium colour, the anisotropy and the reference strength** below). The pictures are the
go/no-go input for the user. Tool: `tools/analysis/fog_offline_mock.py`.
(M) = measured in this session, (D) = documented elsewhere in the repository, (I) = inference.

## Historical stage-1 decision

**Two analytic terms driven by one engine value, with a single half-resolution march that returns only a
scalar "lit fraction"; composited once at the scene-end hook before TAA.**

- **A, sector haze (aerial perspective), geometry only.** `L <- L Ta + Sky_local (1 - Ta)`,
  `Ta = 2^-(d / (N s / S))`. Distant objects sink into the colour of the background behind them; sky
  pixels are untouched, so stars and the painted nebula never turn to soup.
- **B, sun medium local to the camera.** Density `sigma0 exp(-t / R)`, so optical depth is bounded:
  `tau(d) = tau_max (1 - exp(-d / R))`, sky `tau_max`. `L <- L Tb + albedo E_sun p_HG(cos) F (1 - Tb)`.
  `Tb` and the phase are analytic per full-resolution pixel; only `F = int sigma T V dt / (1 - Tb)` (V = cascade
  visibility) is marched, at half resolution, 16 jittered steps, one nearest tap in cascades 1-3.
- **Historical automatic proposal (held after run180; not implemented)**: term B is keyed on the background record's `NumDustInstances` D
  (`sector-fog.md` section 3; 35 of 239 sectors have D > 0), not on `N s`: `N s` does not separate fog sectors
  from painted ones (Argon Prime, 8 cards, 180,000; the card-less green sectors 250,000; Uranus, 15 cards,
  5,000,000). `tau_max = S x 0.01 x w(D)`, `w(0) = 0.25`, `w(D > 0) = clamp(D / 8, 1, 2)`: Argon Prime 0.01,
  the 16-card sectors 0.02, clear sectors 0.0025 (settable to 0). Term A keeps `N s`: half distance `N s / S`,
  so it is near zero wherever the engine's own fade is outside the sector. `S` is the global strength
  (default 1, 0 = pass detached). D comes from `record(sector +0x13c) + 0x134` with the index checked against
  the count at `0x00607040`; if validation fails, a latched "PS `f7e0b6647a3bfa62` was drawn in this sector"
  stands in for D = 8, which needs no private layout.
- **Medium colour = the sector's hue; anisotropy g = 0.3 as the safe default, 0.3-0.6 left tunable** (revised):
  `albedo = hue of Sky_local` (full, not half), sun colour neutral in the mock. With the white-leaning albedo
  and g = 0.6 one sunward view of run174 turns into grey smog at `tau_max` 0.01 already; g = 0.3 cures that
  but also flattens the shaft glow in the best frame (section 2b), so g is settled in stage 1 once `E_sun` is
  calibrated.
- **Original stage-1 choice (superseded by the run 48 B card-replacement design): stack with the engine's fog.** The distance fade carries no colour, and the
  fog cards are a faint, never depth-tested screen-blend veil that the TAA already handles (section 2b).
  Enhancing the cards instead of adding a medium loses (section 6).

A sector-wide homogeneous medium (the brief's starting point) loses; the mock shows why (section 2).

**2026-09-20 scaling revision:** the user's 0.01 for Argon Prime and 0.05 for
Atreus' Clouds are visual targets, not a universal 8/16-instance mapping. A
read-only census of all 239 installed sector/background/cloud-body definitions is
complete and reviewed; the final policy is recorded at the end of this note.
The count-only draft is held. Existing stage-1 rendering still uses the selected
constant strength and card-presence rule; the committed engine-record reader is
diagnostic only until its live pointer chain is validated. FogNear/FogFar describe
the separate geometry distance fade; their influence on replacement density must
be justified by the census and engine consumers before adding another multiplier.

## 1. How the game expresses fog (evidence)

| Question | Finding |
| --- | --- |
| Fixed-function fog | **Never enabled.** `state id=28` (D3DRS_FOGENABLE) snapshot per captured draw: 534,758 rows at 0 and 0 rows non-zero over 115 session logs under `/tmp/x3-bottleX3-run*` (M). The motion rows' `fog=-1` means the game never calls `SetRenderState(FOGENABLE)` in the recorded windows (the shadow reports -1 for a state never set). FOGCOLOR/START/END/DENSITY/TABLEMODE/VERTEXMODE are not in the snapshot list (`capture.cpp:707`); with FOGENABLE at its default they are inert. |
| Shader fog | `g_EnableFog` (b0), `g_FogClip` (c41), `g_AlphaValue` (c39): a per-node **distance fade to transparent** between N and F, `alpha = g_AlphaValue x saturate(g_FogClip.x - g_FogClip.y x distance)` (D, `docs/reverse-engineering/asteroid-fog-temporal.md`). It carries no colour. |
| Per-sector source | Sector/type record table `*0x00606fc0`, stride `0xdb8`, fields `+0x148`/`+0x14c` copied to the sector camera `+0x36c` (N) / `+0x370` (F), enable flag `+0x270 & 0x10000` (D, same note). With the configuration integer at 3 the engine raises F to 500,000,000 native, so **N is the datum that still differs per sector**. |
| Values seen | `object_fade` rows, all logs: (N, F) native = (25,000,000, 30,000,000) in 37 runs, (50,000,000, 55,000,000) in run48, (500,000,000, 500,000,000) in 21 runs; scale 0.01 (M). The 25M sector is the green-nebula sector of run153 f7741/run160, the 500M sector is the plain starfield of run157 (M, by dump). So N tracks "nebula sector" in the three cases available (I: three points only). |
| Nebula draws | Background is draw index 1, VS `7b6393fe2d3e1d85` / PS `6109cf64c03529dd` (the `nebula` program, shared with `gui2d`), 12 draws with that pair in run157 f5920 (M). The `nebulafog` PS `f7e0b6647a3bfa62` is drawn in none of the first 115 logs and in every frame of run174 (M, section 2b). Neither program integrates a volume (D, `shader-family-review.md`). |
| Universe-map attribute | Closed by `docs/reverse-engineering/sector-fog.md`: the sector's background element indexes TBackgrounds; `+0x134` NumDustInstances, `+0x148/+0x14c` FogNear/FogFar; the record's two colour triples are constant (0,0,0 / 120,120,120) in the shipped file, so **the data holds no per-sector fog colour**. |

Colour and anisotropy: no engine colour exists, so `albedo = hue of Sky_local` (section 2b; was a half mix),
sun colour from the tracked sun light, `g = 0.3` fixed (was 0.6). `Sky_local` is the sky-only image at about 1/16
resolution (source in section 3).

## 2. Stage 0 mock-up (what the pictures show)

`fog_offline_mock.py` uses the dumped FP16 colour (`taa_1_*`, decoded ^2.2), RT2 (`.b` clip w), the projection
latch and **the five real cascade maps with the logged view->sun rows**, selected as
`sun_shadow_cascade_apply_ps.hlsl` does. The dumps contain the maps, so the screen-space depth march the brief
allowed as a stand-in was not needed. Sun direction = `-normalize(rows0 row 2)`. Sun colour is not logged:
(1, 0.95, 0.85) assumed; `E_sun = 2 pi x p90` lit-geometry luma (3.14 plant, 2.07 station). Display = the AgX port
with the frame's `ev_adapted`; no bloom, no RCAS.

Frames: **station** = run153 f7741 (25M sector, sun in front, view z +0.80, a station arm between camera and
sun); **plant** = run157 f5920 (500M sector, sun behind, view z -0.55, solar plant at 600 units).

- First attempt, homogeneous medium to a 150k cap (half distance 100k): sky transmittance 0.354, a flat grey
  veil over black space, lit fraction 0.992, i.e. no shafts. Shaft contrast is (shadow length)/(path length):
  a 3,000-unit shadow in a 150,000-unit path is 2 %. Density high enough for shafts erases everything beyond
  50k units (T geometry p5 = 0.05 at half distance 25k). Rejected on this evidence.
- Camera-local medium, R = 10,000: station frame F < 0.9 on 12.2 % of pixels (geometry mean 0.912), visible
  shafts from the arm and a forward glow toward the sun. Plant frame: F < 0.9 on 0.2 %; the term is only a
  faint veil, because the sun is behind the camera and the occluder is 600 units across. Expect the effect to
  be view-dependent: strong looking sunward past large structures, near nothing otherwise.
- Strength: `tau_max` 0.05-0.4 washed out the dark nebula sky (AgX lifts the toe; wash p50 0.0114 linear
  against a sky mean of 0.0056 already at 0.05). 0.01 is barely visible, **0.03 reads as light in dust**, 0.08
  is heavy. Hence `tau_ref = 0.03` (superseded: 0.01, section 2b).
- One-frame noise of F against the 8-phase mean (IGN jitter), station frame: 8 steps mean 0.0146 / p99 0.067,
  16 steps 0.0070 / 0.037, 32 steps 0.0038 / 0.021.

The mock cannot show: temporal behaviour (shaft crawl under camera motion, TAA clamp response, the project's
shimmer history), bloom picking up the glow, RCAS, glows and lens flare in front of fog, the real sun colour,
8-bit F storage, the optional density noise, PCF vs the single tap, and any sector with sprite clouds.

## 2b. Stage 0 redone on real fog frames: Argon Prime, run174

Evidence for the game's own fog is in `sector-fog.md` section 10 (M). What matters here:

- **How it is drawn.** 1-4 `nebulafog` billboards per frame, 340-2,500 units in front of a separate dust
  camera, each covering most of the screen; `ZENABLE 0`, no z write, screen blend `ONE / INVSRCCOLOR` in engine
  space, RGB = one DXT1 cloud texture x node alpha, no vertex colour, `FOGENABLE 0` on all 34,720 draws.
  They are the last scene draws (after hulls, glows and stardust) and come **before** the game's scene end,
  so the scene-end hook's TAA resolve, bloom and AgX take them as scene colour, and a fog composite at that
  hook lands on top of them.
- **TAA.** `routed=0 unmatched=unregistered`, only rt0 and depth bound: the cards write nothing into the
  motion or depth targets, so they cannot poison what is behind them. They are reprojected with the motion of
  the sky or hull behind; at 1,500 units and 150 units/s that is under 1 pixel per frame on a layer with no
  detail finer than the 5x-magnified 512 texture. No action needed.
- **How it looks, honestly.** A faint blue lift of the blacks with cloud structure that parallaxes as the ship
  moves: engine-space floor (0.0055, 0.017, 0.026) at full card alpha, identical on the own ship at 50 units,
  the station at 10,000-18,000 units and the sky, i.e. 7-39 % of the sky median. Most of the "fog" seen in
  Argon Prime is the painted `bluewell` background. There are **no hard intersections** with geometry, because
  there is no depth test at all: the classic soft-particle fault cannot occur; the inverse fault does (the veil
  also lies over the own ship), but at this weight it is invisible. No popping: node alpha ramps about 6/255 per
  frame. Banding/blockiness of the magnified DXT1 texture was not isolated (the layer cannot be separated from
  the background at pixel scale). **It does not interact with light at all**: no sun term, no shadow, no
  distance dependence.
- **Shadows were off in these frames** (sun lane refused by an unregistered opaque pair, `sector-fog.md`
  section 10), so the baselines lack sun shadows; the cascade maps were still replayed and dumped, and the mock
  rebuilds the view->sun rows from `shadow_replay_map_basis` and the logged scene camera (jitter ignored).

Sheets (one frame per capture, under `build/fog-mock/argon-prime/`, untracked): `sheet_c1_f23690.png`,
`sheet_c2_f25333.png`, `sheet_c3_f26429.png`, `sheet_c4_f28644.png` = baseline, cards removed (estimate),
haze only, variant a at `tau_max` 0.01/0.03/0.08 as first specified, variant b at card boost 1/2/4;
`sheet_c*_skyhue.png` = variant a with sector-hue albedo at 0.01/0.02/0.03; `sheet_c3_f26429_skyhue_g03.png`,
`sheet_c4_f28644_skyhue_g03.png` = sector hue, g = 0.3, 0.005/0.01/0.02.

| Frame | Sun (view z) | Geometry | F < 0.9 | What the sheet shows |
| --- | --- | --- | --- | --- |
| c1 f23690 | side, +0.08 | 8.8 %, far p95 52k | 0.5 % | veil only, no occluder toward the sun |
| c2 f25333 | behind, -0.73 | 6.6 % | 0.3 % | near nothing, as the plant frame |
| c3 f26429 | ahead, +0.89 | 9.0 %, far p95 38k | 2.5 % | **as first specified (g 0.6, half-white albedo) the whole sky is grey smog at 0.01**; sector hue + g 0.3 at 0.005 reads as blue dust |
| c4 f28644 | ahead, +0.91 | 15.9 %, station 10-18k backlit | 55.8 % (F < 0.5: 1.6 %) | the showcase: shafts hang below the station; grey at 0.03 as first specified, convincing blue lit fog with shaft glow at 0.01-0.02 with sector hue and g 0.6; at g 0.3 the same strengths are a plain blue veil with faint shafts |

- **Variant a (the two analytic terms).** Term A at the sector's 180,000-unit half distance: Ta 0.93-0.96 on
  the station, a mild, correct sink into the sky colour. Term B needs the three revisions in the Decision:
  sector hue, g = 0.3, `tau_ref` 0.01. The mock's `E_sun` estimator spread 2.6-4.9 over four frames of one
  sector (it follows the bright own ship), so the displayed strength is not comparable between frames; stage 1
  must take `E_sun` from the tracked sun light and the strength must be tuned on display images.
- **Variant b (enhance the game's cards).** Mocked as: invert the screen blend with the recoverable uniform
  card floor, re-apply it x boost x depth fade x sun colour x clipped phase x lit fraction over a 2,500-unit
  shell. Limitation: no with/without pair and no texture in the dump, so only the uniform part of the card
  layer is recoverable; its cloud structure stays baked into the image (measured once, from the fade-out in
  capture 2). Result: at x1-x2 it is indistinguishable from the baseline, because there is almost nothing to
  light; at x4 it is a pleasant blue glow toward the sun with barely visible shafts, which is term B with the
  card floor as its colour. The depth fade changes nothing visible (nothing to fix).
- **Which looks best:** variant a with sector-hue albedo: c4 at g 0.6, `tau_max` 0.01-0.02
  (`sheet_c4_f28644_skyhue.png`), c3 at g 0.3, 0.005 (`sheet_c3_f26429_skyhue_g03.png`). The two frames
  disagree on g; c3's `E_sun` is 1.9x c4's through the mock's estimator, so part of c3's smog is calibration,
  not phase. It is the only variant in which stations cast shafts, and it keeps the sector's colour. Variant b contributes one idea
  that is kept: the medium takes the sector's hue, not white.

## 3. Algorithm

Pass point: the scene-end hook (`hdr-scene-path.md` section 4), after the sun-shadow apply and AO, **before the
TAA resolve**, so the jittered march is accumulated by the 8-sample TAA and bloom/AgX see the fogged scene.

1. **March quad**, half resolution, one `A8R8G8B8` target (F in one channel; 8 bits suffice, the wash it scales
   is <= 0.05 linear). Inputs: RT2 (s0), cascade maps 1, 2, 3 (s1-s3). Per pixel: view ray from the projection
   latch, `d` from `.b`, sentinel -> infinity. 16 steps, equal weight in transmittance: `q = (k + ign) / 16`,
   `tau = -ln(1 - q (1 - Tb))`, `t = -R ln(1 - tau / tau_max)`; IGN offset by the TAA jitter index. Per step three
   `dp4` per cascade, first containing of cascades 1-3, one `texldl`, compare with `bias_max`. Outside cascade 3
   (84k units) lit; with R = 10k, 99.98 % of the density lies inside it, so cascade 4 and the range beyond 150k
   are not sampled. Cascade 0 is the own-ship map; its casters are assumed to be in cascade 1 as well
   (to confirm in stage 1 on the own-ship shadow in the plant frame).
2. **Composite quad**, full resolution, exact in linear light. The scene target is engine-space
   (`decode=gamma2.2`), so a blend-only composite is wrong for the additive term (where wash = scene it
   overshoots 2.3x in linear). Therefore: one `StretchRect` of the scene to an FP16 scratch, then a quad that
   decodes, applies A and B, encodes. It does the depth-aware upsample itself: 4 half-res F taps weighted
   bilinear x exp(-|dz| / 0.05 z) (as the mock). Samplers: scene copy, F, RT2, `Sky_local`.
3. **Sky and sentinel pixels**: term A skipped; term B with `tau_max`, F marched to infinity.
4. **Glows, engine trails, lens flare** are already in the scene and do not write RT2, so they take the fog of
   what lies behind them: over sky they lose `1 - exp(-tau_max)` = 3 % at the default, 8 % at S = 2.7. Accepted;
   a mid-scene pass at the opaque/transparent boundary costs a full-viewport bracket (about 2.3 ms per
   full-screen operation at 1080p, D `screen-emission-region.md`) and is not worth 3 %.
5. **Local glow** (later stage, optional): `+ albedo k (1 - Tb) x bloom mip 2-3 of frame n-1` in the composite;
   one more sampler, no per-light march. One frame of lag on a low-frequency term.
6. **Density noise** (later stage, optional): low-frequency 3D value noise multiplying sigma in the march and a
   second channel of the F target for the noisy `1 - Tb`. Default off; it is the main "soup/crawl" risk.

TAA/bloom/AgX: fog is part of the current colour, so the neighbourhood clamp and history see consistent
values; sky reprojection is unchanged. HDR: all terms are finite, <= E_sun p_max (0.21 E_sun at g = 0.3, 0.8 E_sun at g = 0.6).

Sky_local source: a 1/16 box reduction of the sky-masked scene (one small quad), or the exposure meter's tile
chain if it carries colour. Not checked in this session; stage 1 decides.

## 4. Cost, slots, memory (estimates; nothing here is measured for this pass)

| Item | Estimate | Basis |
| --- | --- | --- |
| CPU submit: march quad, StretchRect, composite quad (+ 1 tiny Sky_local quad) | 0.5-0.7 ms | about 0.2 ms floor per quad on this backend, AO chain 0.51-0.78 ms for 4 quads (D, `ambient-occlusion.md`) |
| GPU: 16 steps x 3 reads per half pixel = 48 reads + composite 7 reads per full pixel | 0.5-0.9 ms at 768p | GTAO 35 reads per half pixel about 0.3 ms (D); GPU headroom itself is unmeasured |
| Per-draw hot path | 0 | one read of three sector-camera fields per frame, no per-draw state |
| ps_3_0 slots, march | about 50 per loop body + 40 setup, well under 512 | `[loop]` body counted once; dynamic cascade choice = 3 `texldl` under branches or 3 unconditional reads |
| ps_3_0 slots, composite | about 90 | decode/encode 2 x pow, HG `pow 1.5`, two `exp`, 4-tap bilateral |
| Memory 768p / 1080p | F 0.94 / 1.98 MiB; scene scratch 7.5 / 15.8 MiB unless an existing FP16 scratch can be leased | sizes |

Budget proposal: +0.7 ms median CPU in a paired on/off flight; above +1.0 ms drop to 8 steps, then refuse.

Native Windows: ps_3_0, `texldl`, R32F and FP16 render-target textures, RT-to-RT `StretchRect`, state block
save/restore: documented D3D9 only, the same set the AO and shadow-apply passes use. Source-compatible;
unverified on Windows like those passes. The sector-camera read is game-private layout (allowed; validate the
pointer chain and refuse to S = 0 when validation fails).

Risks: (1) shimmer: F noise x wash = 0.037 x 0.045 = 0.0017 linear p99 per frame at 16 steps against a sky of
0.0056, before TAA's accumulation; this project has a shimmer history, so the stage 2 fixture gates on it;
(2) view dependence may read as the fog "switching" when turning through the sun; (3) veil over dark sky is
an AgX-toe effect, tune S on display images not linear numbers; (4) E_sun calibration from the tracked sun
light is untested (the mock's image estimator spread 1.9x inside one sector, section 2b); (5) only the
8-card `bluewell` sector is seen; the 16-card families and their `w(D)` = 2 are extrapolated; (6) the pass
needs the cascade maps in frames where the sun lane refuses the apply (all of run174): it must key on map
validity, not on the apply having run.

## 5. Stages

| Stage | Content | Proof |
| --- | --- | --- |
| 0 | This note, the mock, pictures | User judges `station_sheet.png` / `plant_sheet.png` and picks S |
| 1 | HLSL march + composite; host reference test: numpy march of the mock vs a software evaluation of the shader maths on a synthetic depth + one synthetic cascade (as `test_ambient_occlusion_reference.py`); sector-camera read with validation | host test; detached Wine fixture, EVENT-fenced timing at 768p/1080p, byte-identical scene at S = 0 |
| 2 | Temporal fixture: inject the term into `taa_resolve_replay.py` over the 32-frame run153/run160 windows | band/flicker metrics within the current installed build's limits |
| 3 | Live pass behind `--fog` (default off), rule on (D from the record, PS-presence fallback), S settable | paired on/off flight, +<= 0.7 ms median; user captures in Argon Prime (8 cards), one 16-card sector (Atreus' Clouds or Great Reef) and one clear sector; glow dimming <= 1 % at the revised default |
| 4 | Optional: bloom-mip local glow, density noise | user A/B |

## 6. Alternatives that lose

- **Sector-wide homogeneous medium to a capped distance**: section 2; veil without shafts, or shafts with
  everything past 50k units erased.
- **Froxel volume**: D3D9 has no 3D render target writes; a 2D-atlas froxel grid costs several more quads on a
  CPU-bound frame for multi-light support this design does not need.
- **Blend-only composite (no scene copy)**: saves the copy and a sampler, wrong by up to 2.3x in linear where
  wash and scene are comparable, which is exactly the nebula sky.

- **Enhance the game's fog cards (soft-particle fade, sun/shadow lighting, tint) instead of a medium**: mocked
  in section 2b. The cards are 7-39 % of the sky median, never depth tested (no intersections to soften), and
  camera-facing planes 340-2,500 units away: a shadow sampled on such a plane is a projection that swims as the
  billboard turns with the camera, not a shaft, and lighting a layer this faint shows nothing below a 4x boost,
  at which point it is term B. It would also need a replacement pixel shader and the cascade samplers bound on
  1-4 late scene draws, which exist in only 35 sectors. Cost is small (<= 4 draws); the result is the problem.

## Unknowns

Card weight and texture in the 16-card families (user capture in Atreus' Clouds or Great Reef); whether the
clear-sector floor `w(0)` = 0.25 is wanted at all (user taste); GPU headroom (unmeasured); whether cascade 1 holds the own-ship casters; whether an
existing FP16 scratch and a colour-carrying meter chain can be reused; E_sun from the tracked sun light.

## Ratification (orchestrator, 2026-09-19)

Ratified for stage 1 with the user's input on the Argon Prime sheets: the added sun-lit
medium with the sector hue, switched on automatically by the background's
`NumDustInstances`, **strength configurable** from the launcher (the user prefers the
0.02 panel; default 0.02 for the first flight, with a runtime hotkey to step the strength
and toggle the pass so several sectors can be judged in one session), anisotropy
configurable (0.3–0.6 unsettled), sun strength taken from the tracked sun light rather
than estimated from the image. Clear sectors get nothing in stage 1. Shafts require valid
cascade maps, not an applied shadow pass; the pass must work when the shadow apply was
refused. Default off until flown in several fog families (Argon Prime `bluewell`, one
16-card sector such as Atreus' Clouds or Great Reef).

## Stage 1 implementation (2026-09-19)

Status: **implemented and flown in run 48 B, default off; strength 0.02 preferred.** Term B only (the
ratified sun-lit medium). Term A (sector haze) is not built: it needs the sector camera's `N s`, which has
no production reader, and the ratification did not ask for it. Evidence: `docs/verification/volumetric-fog.md`.

**Files.** `src/renderer/fog_pass.{h,cpp}` (the transaction), `src/renderer/fog_pass_math.h` (d3d9-free:
ranges, strength ladder, sun radiance, capability gate, sector latch), `src/fog/fog_march_ps.hlsl`,
`fog_composite_ps.hlsl`, `fog_sky_level0_ps.hlsl`, `fog_sky_reduce_ps.hlsl` ->
`src/renderer/fog_*_program_inc.h` through `tools/shaders/generate_rigid_motion_pixel.py`
(`--shader fog_march|fog_composite|fog_sky_level0|fog_sky_reduce`, manifests
`verification/results/fog-*-program.json`), the live glue `src/proxy/motion_output_fog_inc.h`
(`MotionOutput::run_volumetric_fog`), the launcher options in `tools/manage.py`.

**Transaction** (one `D3DSBT_ALL` block plus saved target/depth/viewport/scissor, as `AmbientOcclusionPass`):

1. *March*, `ceil(W/2) x ceil(H/2)` `A8R8G8B8`: section 3 step 1 as written, 16 steps, IGN offset by
   `5.588238 x (jitter index mod 8)`. Cascades: the apply quad's slots **1-3** (slot 0, the own-ship map, is
   not marched; a single-cascade set uses slot 0), valid by the apply's own rule (replayed this frame, the far
   one also on the previous frame), the first containing **valid** cascade, one nearest tap at the texel
   centre, reference lowered by the apply's bias clamp. An invalid cascade is skipped (its samples fall
   through to the next one, then lit), so a frame whose replay was refused draws the veil without shafts
   instead of dropping the pass. `.b` of the `A32B32G32R32F` RT2 is the view depth; `R32F`/`G32R32F`, or
   `.b <= 0`, fall back to `m32 / (d - m22)`. 224 of 512 ps_3_0 slots (the largest of the four programs).
2. *Copy*: `StretchRect` of the FP16 scene target to an FP16 scratch (no existing scratch is leased: the
   AO and HDR passes own none of that format and size at this point).
3. *Sky hue*, every 32nd frame (weight 0.5 live; the ledger has the cost) and until seeded: scratch + RT2 -> 8x8 FP16 (12x12 point taps per texel =
   9,216 samples of the frame, sentinel pixels only, linear clamped at 4) -> a 1x1 FP16 history under
   `SRCALPHA/INVSRCALPHA` (the first update after creation or Reset writes unblended: a new
   target's contents are undefined; under 2 % sky coverage the weight is 0 and the history is kept). This is
   the note's `Sky_local` reduced to its mean: the mock took the albedo from the mean of all sky pixels too.
   Nothing is read back; `hue = clamp(sky / luma, 0, 4)`, white while the history is black.
4. *Composite*, full resolution into the scene target: decode (2.2, or 1 when the HDR path decodes nothing;
   sRGB is treated as 2.2), `L Tb + hue E_sun p_HG F (1 - Tb)`, encode, alpha carried; the 4-tap depth-aware
   upsample reads the half texels' depths from RT2 directly, so no half-resolution depth target exists.

Pass point: `scene_end_hook` after the sun apply and AO, before `resolve_hdr` (and the bloom-copy fallback
site, once per frame). Only the pass's own targets and the scene target are written; RT1.. and the depth
surface are unbound for the transaction and returned (fixture: RT1 and RT2 byte-identical after).

**Sun.** Direction: `-normalize(row 2)` of the first valid cascade's view rows, else of this frame's
would-be basis (`cascade_sun`), else the pass skips (`reason=sun`). Radiance: **tracked, not estimated**:
`sun_light_poll::Sample` now carries the chosen light node's colour words (`+0x150/152/154`, / 256 =
`LightDir_Color0`); `E_sun = pi x decode(Color0)` per channel (the hull programs shade
`albedo x Color0 x N.L`, i.e. a Lambert surface under irradiance `pi x Color0`), words clamped to 4 x 256.
When the poll is not `ok` (foreign executable, `X3M_SHADOW_SUN_POLL=0`) the pass uses Color0 = 1 (`E = pi`)
and logs one `volumetric_fog_sun source=fallback` line. The mock's image estimator gave 2.6-4.9 in Argon
Prime; a white sun gives 3.14 here. **Original stage-1 limitation:** actual colour words were not in the run174
dumps; run180 reports the tracked sun path (see the flight ledger).

**Automatic rule: PS presence, chosen over the record read.** The record read needs the current sector
object (`*(cockpit+0x54)+0x13c`), and no production reader reaches the cockpit; it would add a second
private pointer chain and an executable gate for a value (D) that stage 1 does not use (the ratified strength
is `tau_max` itself, without `w(D)`). The `nebulafog` program is instantiated only by a background with
`NumDustInstances > 0`, so its bind is the same fact, observed through documented D3D9 on any executable:
`set_pixel_shader` compares the bound hash with `f7e0b6647a3bfa62` (one compare per bind, only with the
option on) and stamps `FogSectorLatch`. The latch holds the sector foggy for 600 frames after the last
bind (1-4 of the 8-16 instances are on screen, each fading over ~40 frames) and ramps the medium's weight
over 90 frames both ways, so a card-free view, a gate jump or the first card does not pop.
A camera cut at the scene end (`counters_.cut`, the route's detector: displacement median or missing-key fraction over
its bound, as a gate jump, a load or a view switch produce) ends the hold at once unless a card was bound in that
same frame, so a clear sector sheds the medium in the 90-frame ramp instead of ~11 s; that the detector fires on
every gate jump is inferred, not flown. `--volumetric-fog-everywhere` forces the target to 1. Limits: the rule lags a sector change by the ramp, and
a fog sector viewed for over 600 frames with no card bound loses the medium (not observed in run174: every
frame bound at least one).

**Options.** `--volumetric-fog [S]` (`X3M_VOLUMETRIC_FOG=1`, `X3M_VOLUMETRIC_FOG_STRENGTH`, `S = tau_max`,
0..0.1, default 0.02, 0 = off; requires `--motion-output --taa --hdr --shadow-replay-depth
--shadow-cascades`), `--volumetric-fog-anisotropy G` (0..0.9, default 0.3),
`--volumetric-fog-everywhere`, `--volumetric-fog-timing` (one `volumetric_fog_frame` line per frame with
`cpu_us` and `calls`; otherwise one line per change of the skip reason, at most 64). Hotkeys, polled only with
the option: **Ctrl+Alt+F9** toggles the pass, **Ctrl+Alt+F10** steps the strength through
0.005/0.01/0.02/0.03/0.05 (a launcher value between steps moves to the next above); Shift must be up, so
the chords are disjoint from Ctrl+Shift+F9/F10 (exposure, bloom), as Ctrl+Alt+F7 is from the telemetry
marker. F1-F3 were not used: they are the engine's view keys and `GetAsyncKeyState` does not consume them.
Each press logs `volumetric_fog_toggle` / `volumetric_fog_strength`; with `--fps-overlay` the second line
reads `FOG 0.020`, `FOG 0.020 IDLE` (sector rule at zero) or `FOG OFF`.

**Off and failure paths.** Option off: `fog_requested_` guards the three call sites and the bind compare;
no object is created and no key polled. Any unmet precondition (`toggled_off`, `sector`, `taa`, `owner`,
`depth`, `recording`, `queries`, `camera`, `cascades`, `target`, `sun`, `reset_pending`) skips before any
device call. Failure policy (`fog_failure_action`): a lost device (any stage, target creation included) is neither
counted nor disabling, the frame after Reset retries; a target allocation that fails otherwise disables the pass
for the session with one `volumetric_fog_disabled ... session=1` line, as do three consecutive other failures
(cleared by a success or a Reset); a refused or failed attach, the adapter query included, skips with
`reason=attach` until the next Reset, like the AO and sun-apply passes. The proxy arms the option only with the
launcher's prerequisites present in the environment (`volumetric_fog_mode` logs each once). A failed
step before the composite leaves the scene untouched; a lost device stops restoration. `before_reset`
releases the four targets and the block (the sky history re-seeds), `after_reset` re-arms.

**Native Windows.** Documented D3D9 only: caps fields (`PixelShaderVersion`,
`MaxPixelShader30InstructionSlots`, blend caps, `D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES`),
`CheckDeviceFormat` (A8R8G8B8 target, FP16 target with post-pixel-shader blending, R32F texture), RT-to-RT
`StretchRect` of equal size and format with `D3DTEXF_NONE`, `texldl` in ps_3_0 loops and branches. The
rule needs no private layout; the sun colour read is game-private (not Wine-private) and has the portable
fallback above. Cross-compiled only; native execution unverified.

**Measured cost** (detached fixture, EVENT-fenced, CrossOver Preview; not game frame time): see the ledger.
The backend refuses `TIMESTAMP` queries, so GPU time is the fenced window minus the submit time.

**Open after stage 1.** `E_sun` calibration and g on display images (user flight); 16-card families; whether
the own-ship casters need slot 0 in the march (section 3 step 1's open question: not examined); the stage 2
temporal fixture (shimmer gate) has not been built; term A.

## Card replacement design — source qualified, candidate pending (2026-09-20)

The design below is implemented and source-reviewed; candidate and game-flight
qualification remain pending. Scoped evidence and limitations are in the
[verification ledger](../verification/volumetric-fog.md#card-replacement-source-qualification-2026-09-20). Session B/run180's
user preference is `tau_max = 0.02` and replacement of the vanilla card veil in
The Hole and Atreus Clouds. That preference supersedes the earlier artistic
choice to stack both layers. It does **not** authorize removing geometry's
`g_FogClip` distance fade, the painted background, stardust or glow draws.
The orchestrator ratified the bracket, acceptance contract and low-cost
one-frame-failure tradeoff below on 2026-09-20. Implementation and verification
remain pending.

**Recommendation.** Add explicit `--volumetric-fog-cards keep|replace`, default
`keep` for the unflown path; select `replace` explicitly for the next consolidated
candidate. Suppress only validated card **color writes**, while forwarding the
original native draw exactly once and returning its actual HRESULT. Do not turn
`MotionRoute::submit` off with synthetic success: that discards native draw
validation and any query/rasterization side effects. Keep strength 0.02 and the
current geometry fade and sector PS-presence rule.

**Evidence and exact interception.** `sector-fog.md` §10 records 422 card draws
in 128 frames, all with VS `7b6393fe2d3e1d85`, PS `f7e0b6647a3bfa62`, 2 primitives,
4 vertices, stride 24, Z disabled, Z-write disabled, alpha test disabled,
`ONE/INVSRCCOLOR/ADD`, no culling, RGB write mask 7. The cards are the last scene
color writers in that evidence; this order is not proved for all sectors/mods.
`capture.cpp::{draw_primitive,draw_indexed,draw_primitive_up,draw_indexed_up}`
forward through `MotionOutput::{before_draw,after_draw}`. Put admission in the
unrouted branch of `before_draw`, **after** successful
`restore_bindings_checked()`, exclusive of composition/source-gain brackets.
Use a small route flag and saved mask: native `SetRenderState(COLORWRITEENABLE,0)`
then the original draw, then checked restoration to the observed mask (7) in
`after_draw`, including failed-native-draw exits. Do not mutate the application's
state shadow. A failed set must recover known state before forwarding; failed
restoration uses the existing `motion_state_lost_`/submission-error path, never
pretends the next draw is safe. Preserve the LightCallBoundary/LastError contract.

Cache exact pair eligibility at shader binds/resync. Admission also requires
known matching render states, the validated declaration/stride and draw shape,
main FP16 scene ownership, no routed MRT writes, no concurrent composition,
non-MSAA, checked state getters or valid existing state-hook caches, no
state-block recording and no active query. Replacement does not enable global
state hooks. Read stream frequency afresh only after strict card gates; refuse
failed reads and frequency other than one. Only
admit draw forms demonstrated by the capture/fixture; unknown forms, state,
identity, foreign target or bind failure forward unchanged. Keep
`set_pixel_shader`'s existing `fog_latch_.card(frame_)` before any replacement
choice; stamp observed matching source draws as well so a reused binding still
counts. Replacement must never teach the sector detector that its cards vanished.
No background identity or geometry-fade shader is included in this allowlist.

**Readiness and fallback.** Cache frame eligibility after hotkeys in
`comparison_begin_frame`; Ctrl+Alt+F9 off and strength zero immediately leave all
cards native. Require a previous fully successful fog transaction *and* this
frame's known prerequisites/resources, not previous success alone. Prepare target
sizes/caps outside individual card brackets; a first warm-up frame after enabling
or Reset keeps vanilla and runs the medium once. Only a fully successful stacked
warm-up arms replacement for the following eligible frame; failed/refused warm-up
keeps vanilla. An ordinary F9 off/on repeats that warm-up, except that it cannot
clear a fault latch. Check current owner, camera/depth availability, TAA/jitter,
cascade/sun prerequisites and query/recording status before the first suppression;
recheck cheap mutable guards on later cards. `run_volumetric_fog` still performs
its full scene-end validation. Outside that explicit warm-up, if admission is unavailable, keep vanilla and skip
the replacement-mode medium that frame rather than silently stack both; frames
without visible cards may still run the medium under the existing latch.

**Material limitation, accepted design tradeoff.** No early predicate can
promise `FogPass::execute` will succeed later. It can fail at Targets, Block,
Capture, Normalize, March, Copy, Sky, Composite, EndScene or Restore; the scene-end
glue can also newly refuse queries, state, target or TAA. Once a card's color was
not written, this implementation cannot reconstruct it. The first such skip or
failure after suppression must latch *replacement and the medium* off until
Reset/session restart, independently of the current three-failure fog policy.
Thus there can be **one recoverable-failure frame without either fog layer**;
subsequent frames are vanilla. This is not a strict same-frame fail-open guarantee.
A device-loss/state-restoration failure uses existing recovery and cannot promise
a valid displayed frame. F9 off always forwards cards; F9 on must not clear the
fault latch. Log observed/suppressed counts, the per-frame refused boolean, readiness, current-frame pass
outcome and fault-latch reason, with bounded/change-only output. TAA history must
be invalidated on actual keep/replace/off/fault transitions so residual cards or
fog do not survive the hotkey comparison; not on every card or every frame.

**Alternatives.** Merely skipping the native call is cheaper but loses draw
semantics. Masking/replacing the PS adds shader ownership for no necessary benefit.
Prior-success-only suppression or retaining the existing three-failure retry
permits avoidable missing-fog frames. Strict same-frame fallback needs a retained
vanilla image, not merely resource preallocation. A feasible larger design copies
the pre-card scene once, lets all original cards draw into the vanilla owner,
then processes a clean candidate through sun apply, AO and fog before publishing
it; retain the vanilla owner until success. Any later non-card color writer must
invalidate that snapshot, or be mirrored with additional cost. On failure restore
the vanilla owner and handle sun/AO bookkeeping consistently. Existing
`HdrPass::exchange_target`/`publish_composition` demonstrate ownership exchange but
do not implement this transaction. Simply using a pre-card copy as FogPass input
**after** current sun/AO loses those passes: their order in `scene_end_hook` is
sun apply, AO, fog, TAA. This alternative needs ownership/recovery work and is not
recommended for the next low-cost optional layer.

**Hot-path cost and portability.** Non-card draws add one cached eligibility gate;
no new hashing, engine reads, allocation, lock or fog-specific Get* validation
on noncards. An eligible card uses at most 12 render-state getters, one fresh
stream-frequency getter and two colour-mask setters: at most 90–120 calls for
run180's sampled six to eight cards. Already-read per-draw state reduces the
incremental count. No new targets, copies or full-screen passes are added.
This is a checked call-count bound, not measured latency; original card
rasterization remains. The previous implementation forced global render-state
and sampler hooks; that behavior was removed before candidate qualification.
The retained-image alternative adds at least one FP16 full-size target (7.5 MiB
at 1280x768; 15.82 MiB at 1920x1080) and one copy (15/31.64 MiB read+write traffic),
plus binding/ownership checks and failure-path reprocessing. Native Windows uses
the same documented D3D9 render-state/draw/state-block/query contracts. No Wine
internals are needed; actual native execution remains unverified and must be
recorded as such in platform-portability.md when implemented.

**One consolidated next candidate / ownership.** The renderer change owns
`motion_output.{h,cpp}`, `motion_output_fog_inc.h`, the readiness helpers in
`renderer/fog_pass.{h,cpp}` if needed, and focused fixture cases. The launcher and
capture owner integrates the option/hotkey diagnostics and the independent
read-only `sector_background` diagnostic. Coordinate shared `capture.cpp` and
`tools/manage.py` rather than let both workstreams edit them. Include
`sector-fog.md` §11.5's once-per-second/change row in this same candidate, with
bounded validated reads and LastError preservation. The chain is still **static
RE only**: do not feed dust/index/near/far into replacement or a stage-2 sector
policy until a user flight establishes §11.5's named-sector, camera-fog, menu/load
and same-frame jump consistency checks. Diagnostic failure must never alter the
current card-presence rule or crash rendering.

**Acceptance before candidate publication (planned, not run here).** Extend the
focused host policy/launcher checks (`test_volumetric_fog.py` and relevant launcher
tests), then the owner runs the targeted motion-output/fog fixtures through the
Wine lock in bottle X3. Check exact card-only output suppression; keep/F9-off/
strength-zero/caps-refused byte equality to vanilla; one successful stacked
warm-up before first suppression; native draw count and
HRESULT parity; active-query and recording refusals; captured state-block Apply,
all relevant setters/resync, RT/depth/viewport and COLORWRITEENABLE restoration;
unchanged RT1/RT2; source detection while suppressed; rejection of sky/stardust/
distance-fade/unknown pairs; faults at admission, source draw, every fog failure
stage and restoration; one-frame-gap fault latch and Reset rearm. Record added
call counts and focused timings, plus x87/stack contract checks for changed draw
roots. User flight then compares .02 keep/replace/F9 off in The Hole and Atreus
Clouds, verifies clear-sector/jump/load behavior and validates §11.5. No agent
launch. No native-Windows verification may be inferred from CrossOver results.

**Unresolved.** Which run180 card draw forms/states extend §10's allowlist (the
separate triage owns that evidence); measured card-validation and mask cost; whether one-frame
TAA reseeding on mode changes is visually acceptable; native query/state parity;
stage-2 temporal fixture and record-reader flight qualification remain open.


## Earlier sector strength decision after the census — superseded (2026-09-20)

The [complete census](../reverse-engineering/sector-fog-census.md) supports
explicit artistic profiles, not an instance-count density formula. The planned
automatic policy is **bluewell 0.01** (Argon Prime) and **foggreenoutlands 0.05**
(Atreus' Clouds and The Hole). Extending each anchor to other sectors sharing
its background recipe is an inference. The other nine positive-card families
retain the explicitly uncalibrated selected global strength S: fogbluedistance,
fogcyancorner, fogdeepred, foggreeneye, fogparanid, fogred, uranus, uranus3 and
whitenexus. Equal body size alone does not transfer a calibrated strength.
D50 Veil of Delusion and the Uranus asset exceptions receive no count boost.

Keep R=10,000 view units and the existing independent geometry distance fade.
FogNear/FogFar, sector size, instance count and body area multiply neither
strength nor radius. The engine's fixed wrapping lattice and close-card fade
are not an absorption length; same-body size refresh is not a density signal.
The 2.99 geometric count-times-area ratio does not derive the user's chosen
fivefold strength difference.

**Not activated:** the next candidate first validates the engine-record chain
in §11.5. The existing card-presence rule and manual numeric strength/hotkeys
remain operative. Compare the 0.01/0.05 anchors manually with replacement-only
rendering; earlier preferences may have included vanilla cards. Once validated,
D=0 may turn the automatic medium off, while unreadable/changed recipes fall
back to the card-presence rule at S, never to a fabricated clear-sector verdict.
F9 off and S=0 remain overriding disables. Automatic/manual precedence and
transition smoothing must be specified before connecting these profiles to
rendering. No extra profile reader, GPU pass or preview logging is added here.


## Run49B: spatial cloud redesign required (2026-09-20)

**User verdict:** the uniform wash is not acceptable. The replacement must have
localized clouds, varying intensity and clear gaps, rather than bleaching an
entire heavy-fog view. The previous 0.01/0.05 family anchors are not accepted
replacement defaults; scaling a homogeneous field cannot meet this request.

The current density is camera-relative `sigma0*exp(-distance/R)`. Geometry
shortens its integration distance, and sun direction/shadows vary its light,
but sky rays all receive the same maximum optical depth. Composite hue comes
from a smoothed 1x1 mean sky color. Thus it is not literally a constant output
color, but it lacks spatial cloud occupancy and texture variation. Adding noise
to the lit-fraction channel alone cannot fix this: extinction/transmittance
must vary too.

The original engine cards are also not a physical sector fog-volume map.
[Sector-fog §12](../reverse-engineering/sector-fog.md#12-card-size-tiling-and-opacity-count-is-not-a-density-scalar-2026-09-20)
recovers camera-nearest periodic placement, individual textures, roll, size and
spatial alpha. They create patches and gaps which the uniform replacement
removed. Their appearance is useful reference; count, FogNear/FogFar and body
size alone do not define a density field.

**Next bounded experiment, ratified for offline evaluation only:** a fixed
world-anchored compact-cloud field with exactly zero density outside its clouds.
Integrate both transmission and in-scattering along the actual view ray,
clipped to opaque depth, rather than retaining a universal sky veil. Test
clear-ray occupancy, variation, low-step error and stability under camera
motion before any production shader/target/hook change. Sector families can
select artistic cloud recipes later; no runtime count-based strength rule is
being activated.

Run185's four F8 bursts all have fog enabled, at 0.01, 0.005, 0.05 and 0.05.
Their HDR/TAA dumps are already fogged: `run_volumetric_fog` precedes
`resolve_hdr` and `hdr_writeback`'s HDR readback. They are valid camera/depth/
shadow substrates and visual references, but cannot be presented as clean
fog-free backgrounds for an exact replacement composite. Initial offline
outputs therefore show cloud-only density/transmission beside the real frame;
no inverse-removal or fictitious final-game image is permitted. Existing older
fog-off captures may support a separately labeled composite, with their own
vanilla-card contamination and exposure limits.

Native Windows compatibility, ps_3_0 instruction/texture limits, GPU cost,
state/Reset recovery and moving-cloud temporal quality remain production
acceptance gates. The existing manual fog controls remain available, but this
visual result is not accepted as the final replacement.

### First bounded spatial-field replay: recipe rejected

The fixed-seed CPU prototype uses a periodic 64³ field, compact clouds and
zero density outside their support; occupied texels are 10.14%. On run185
frames 1974/9204/21901/26447, clear-sky-ray fractions are
68.83%/94.78%/91.55%/85.00%, while dense-ray fractions are
6.87%/0.23%/1.86%/2.39%. All pass the ≥25% clear requirement, but three
fail ≥5% dense coverage. Optical-depth p90–p10 spreads
0.01231/0.00027/0.00134/0.00534 all miss the >0.015 requirement.
The recipe is **rejected**, with no per-frame seed/strength tuning. The
24,000-unit horizon countercheck was not run; the 12,000-unit horizon remains
an unaccepted choice.

Camera/world anchoring, negative wrapping and periodic-coordinate checks pass.
The 24-step versus 128-step reference transmittance error has worst p99/max
0.00012/0.00021; four eight-frame pre-TAA comparisons also pass their numerical
error gate. Exact `(S,T)=(0,1)` on reference-empty rays fails, although no false
opacity exceeds 0.002. These sampling comparisons do not establish perfect
continuous-field integration or in-game temporal quality.

The density maps visibly contain gaps, but do not establish an attractive
replacement: three views lack enough cloud coverage, lighting is unshadowed,
and the original present images already contain the old fog. No final
replacement composite, GPU implementation or acceptance is claimed. Local
helper and validated report: `/tmp/x3-fog-patchy-replay/tools/analysis/fog_patchy_replay.py`
and `/tmp/x3-fog-patchy-replay/verification/results/fog-patchy-replay/report.json`.


### Fixed-field column/gain feasibility follow-up

The first rejection exposed two unsuitable diagnostic gates: optical-depth
p90–p10 implicitly demanded roughly 10% strong coverage despite the separate
5% dense-ray target, and a finite quadrature's zero samples did not prove an
exactly empty continuous ray. Neither failure establishes that spatial clouds
are invalid. Exact vacuum remains a zero-field/zero-length unit check; numerical
integration uses tolerances. These corrections do not retroactively accept the
original recipe's weak appearance.

Measured field mean density is 0.01975994 of peak, versus 0.10137177 occupied
texels. The compact soft profile explains much of the weak column density.
A closed follow-up retained the same field/seed and four cameras, considered
only 12,000/24,000 render-unit horizons, and solved a shared gain in [1,8]
from integrated columns. Artistic diagnostic constraints were ≥25% clear sky
(opacity ≤0.002), sky p99 opacity ≤0.10, and ≥5% dense sky (opacity ≥0.015)
in the two green-family views. Blue views may be mostly clear. Connected clear
and dense regions were checked separately; no per-frame seed/strength tuning.

The 12,000-unit shared feasible interval is **[3.337040784,4.220746008]**;
the smallest floating-point-safe gain, **3.337040784**, passes. Green frames
21901/26447 have dense fractions 5.00%/10.98%, largest dense components
4.48%/5.83%, and clear fractions 87.19%/80.83%. Across all views clear coverage
is at least 58.91%, with p99 opacity at most 7.99%. The sole selected lowest
gain at 24,000 fails the 2% dense-component gate (1.98% in frame21901); it was
not tuned further. This is a bounded recipe comparison, not a proof that all
longer-horizon configurations fail.

Selected eight-frame/window 24-step versus 64-step checks have worst p99/max
transmittance error 0.000384/0.000743 and zero false opacity above 0.002 on
reference-empty samples. Host execution took 12.47 seconds, not GPU time.
JSON/compilation checks pass; original rejected artifacts remain unchanged.
Local helper: `/tmp/x3-fog-patchy-replay/tools/analysis/fog_patchy_feasibility.py`;
report and fixed 0–0.10 opacity sheets: the neighboring
`verification/results/fog-patchy-feasibility/` directory.

**Parent decision:** retain numerical feasibility only. The inspected sheets
have coherent gaps but visibly smooth ellipsoid lobes; they are not accepted
as the requested cloud appearance. Next examine the native cloud texture's
internal intensity/colour variation before another appearance experiment.
No final replacement composite, fog-TAA motion proof, GPU qualification,
automatic sector-strength policy or production patch follows from these masks.


### Native-texture appearance experiment: not accepted

Read-only native texture inspection finds fully opaque alpha in both families;
RGB supplies the visible cavities and bright ridges. Bluewell's ≤0.01 encoded
luma fraction is 71.16%, versus green's 37.72%. These are texture statistics,
not physical optical density. A single artistic triplanar construction used
those textures to modulate the fixed macro-cloud field and derive colour;
it is not a recovered engine volume.

The 128³ blue/green bakes preserve mean density 0.01975994 using fixed family
normalization K=1.201995/1.111779, retaining unclamped floating maxima
1.375/1.257. There is no per-frame strength tuning. The four 24-versus-128-step
checks pass (worst p99/max transmittance error 0.002007/0.004909), but green
frame21901 has dense coverage **4.2408%**, below the retained 5% gate. The
reported outcome remains `FAIL_MORPHOLOGY`.

An appearance sandbox uses pre-feature run153 frame7741: 210 captured draws,
zero native fog-card shader hits in that frame, matching HDR/depth/camera,
fixed exposure and assumed white unshadowed illumination. It is explicitly an
old-scene comparison, not a prediction or reconstructed clean baseline for
run185. Root and independent review see family colour and some internal
variation, but still-smooth ellipsoid lobes. This is not the requested final
cloud appearance and is not promoted to production. The numerical coverage
miss is not the only reason for that decision.

Local evidence: `/tmp/x3-fog-appearance-assets/` and
`/tmp/x3-fog-patchy-replay/verification/results/fog-patchy-appearance/`;
helper `tools/analysis/fog_patchy_appearance.py` in that isolated checkout.
Host run 12.05 seconds; JSON, finite-value, syntax and independent factual/
visual checks pass. No game/Wine/build/install was used. Lighting, TAA motion,
shader budget, runtime cost, Reset and native execution remain unqualified.


## Spatial production contract (2026-09-20)

The parent ratified replacement of the rejected analytic medium with a fixed
world-space density field. The existing scene-end owner, camera/depth producers,
card mask bracket and TAA order remain the integration points. There is no old
uniform underlayer and no automatic uniform fallback. This is an artistic field
selected by validated engine family, not a recovered native three-dimensional
cloud map. The [verification ledger](../verification/volumetric-fog.md) separates
prototype, asset, production and eventual flight evidence.

**Recipe and control.** Period 32768, 128³ samples, 12000-unit horizon and 24 fixed
midpoint steps. The original profiles retain occupancy 0.12 / sigma 2.5e-6 for
bluewell and occupancy 0.24 / sigma 6.25e-6 for foggreenoutlands, with both decoded
atlases and packets byte-identical. The branch implementation extends coverage
to 14 asset-backed families: all 11 mapped positive-card families plus unused
fogblue, fogkhaak and khaakhive. Its twelve additions use a common provisional
artistic occupancy 0.12 / sigma 2.5e-6, not a measured native-density conversion
or accepted appearance. Four-stop palettes derive from native texture colour;
the procedural field does not reproduce native card positions or texture shapes.
Noise, cavities and voxel colour are baked deterministically by
`tools/build/bake_fog_fields.py`. The [family expansion checkpoint](../verification/volumetric-fog.md#asset-backed-family-expansion-host-checkpoint-2026-09-20)
records host-only evidence; candidate integration and actual-GPU coverage remain
pending, with the installed build described only in [status](../status.md).
The selected numeric strength S remains 0..0.1, with 0.02 meaning unity:
`sigma_effective = family_sigma * (S / 0.02)`. It changes density, not occupancy
or horizon. Thus the old 0.01/0.05 homogeneous anchors are superseded. F9 disables;
F10 keeps the existing strength ladder. Run53B records the user preference for
S=0.03 (density multiplier 1.5); this leaves the authored family sigma/occupancy
and unity basis unchanged. Non-unity settings are tuning options, not separately
accepted appearance. FogNear/FogFar, body size and card count add
no density multiplier; the engine's separate distance fade remains intact.

**Authority and fallback.** One copied engine record from the first successful
BeginScene governs the current frame independently of diagnostic logging.
In the expanded branch, ready, valid positive-dust records select one of the
14 profiles; D=0 is clear. Unused fogred, foggreenoutlands and foggreeneye records
share their family profile. Missing-asset families xtmgreenring (no dust bodies)
and earth (unresolved diffuse), unknown positive families, unreadable/mismatched
records and absent current-frame authority preserve native cards and run no
replacement field. Coverage is bounded by the stock asset inventory, not a
promise for arbitrary mod families. Present's diagnostic fallback never
authorizes next-frame suppression. `--volumetric-fog-everywhere` remains explicit
debug forcing of bluewell when a valid view lacks a known family; normal mode
never invents a profile. A sector identity change disarms replacement and requires a new matching
warmup even when the same family atlas can be reused.

**Storage and preparation.** The expanded branch packages 14 sparse RCDATA
resources totaling 34,142,200 bytes including headers. The original two packets
remain byte-identical; generated metadata and RC entries cover the full profile
inventory. There is still one active 17,846,400-byte (17.02 MiB) decoded CPU atlas
and one same-sized DEFAULT GPU atlas, plus half-resolution FP16 ST
and full-resolution FP16 scratch. Total GPU storage is about 26.40 MiB at
1280×768 or 36.80 MiB at 1920×1080, excluding driver copies/alignment. Preparation
may additionally use a transient SYSTEMMEM upload. Decode/validate/upload occurs
at the HDR owner latch before suppression, never in draw or execute. A changed
family cannot use the previous family's readiness. Reset releases DEFAULT
resources and lazily reuploads retained CPU data; option off allocates none.
First-use, switch and Reset preparation times are separate performance gates.

**Coordinates and light.** Camera position and rays use a checked inverse of the
existing camera rotation, world phase modulo 32768 and the existing projection/
jitter correction exactly once. Only the current qualified RGBA32F depth lane's
linear .b is admitted, including producer failure/publication checks. Invalid
geometry depth gives identity; sky uses the horizon. The spatial pass integrates
both scattering S and transmission T, preserves exact empty-pixel identity and
source alpha, and uses class-compatible upsampling with full-pixel edge repair.
Tracked sun radiance, anisotropy and HDR decode controls remain supported; the
qualified baseline is E=pi, g=0.3, gamma=2.2. This first spatial pass is unshadowed;
it does not claim volumetric shafts.

**Scene and failure contract.** Production uses the supplied native public D3D9
entries. For an open caller scene, state is captured, native EndScene permits
StretchRect, and native BeginScene reopens before fog draws and restoration.
A failed reopen permits one ordinary-error recovery attempt, no draws, and no
retry on loss. Unknown scene or failed restoration poisons the route and prevents
subsequent injected resolve/writeback. Closed scenes keep the corresponding
copy/Begin/draw/End sequence. Every stream's buffer, offset, stride and frequency
is explicitly restored alongside other state. Scene pixels are not rollback
protected after a composite write starts.

**Cards and history.** Replacement requires current profile/generation,
prepared targets, camera/depth/owner/idle admission and a successful matching
warmup. The existing single stacked warmup frame remains explicit for genuine
replacement readiness changes; keep mode is an intentional diagnostic comparison.
A camera/TAA history cut alone does not disarm successful replacement or require
another warmup: the spatial field uses the current camera/depth and has no
independent fog history. Repeated cuts must continue suppressing admitted native
cards while applying the current-frame volume. Sector/profile/generation changes,
toggles, Reset, missing authority and failures retain their existing readiness and
fallback rules; owner/camera/depth admission is still checked each frame. After suppression, refusal/failure latches
medium and replacement off until Reset; this can lose both layers for one frame
and cannot restore already suppressed cards. Native draw count/HRESULT remains
unchanged. Sector/profile, strength/mode, cut and Reset transitions invalidate
TAA once; there is no separate fog history. Finite fog translation against sky
rotation-only history remains a production-sequence and flight quality gate.

Production source reviews and cross-compilation do not substitute for the actual
pass and route fixtures, linked CPU audit, loading/timing measurements or native
Windows execution. The new scene transaction, supported control extremes,
invalid-depth behavior, state/recovery and source cleanliness must be tested
before a candidate. The detached prototype's roughly 1.1 ms timing is not a
measurement of this production integration.


### Space-game comparison and deferred experiments (2026-09-20)

Primary-source research supports separating distant nebula artwork from local
fly-through volumes. Star Citizen's [3.12 postmortem](https://robertsspaceindustries.com/en/comm-link/transmission/17991-Alpha-312-Postmortem)
reports its first gas-cloud release in the Persistent Universe and remaining noise/performance
work. EVE's [rendering update](https://www.eveonline.com/news/view/building-the-future-of-eve)
describes cubemap nebula backgrounds, so those images are not evidence for a
local volume renderer. No Man's Sky's [Worlds update](https://www.nomanssky.com/worlds-part-I-update/)
describes planetary volumetric clouds, without publishing its sampling or
reprojection implementation. These sources do not establish transferable cost
or an exact space-cloud TAA algorithm for X3.

Parent decision: finish the current production qualification and first spatial
flight unchanged. If translated clouds trail, investigate selective fog-aware
history confidence against the captured baseline; do not globally weaken scene
TAA. If spatial shape succeeds but colour is flat, evaluate one palette rebake
with density alpha preserved byte-for-byte. Both are conditional experiments,
not approved production changes or claims about another game's implementation.
The current 24 steps span up to 500 units per step against 256-unit voxels;
this identifies an undersampling risk, not an observed failure. Any added
ray jitter or reduced sampling waits on temporal evidence.

Detailed primary-source comparisons and implementation limits are retained in
[/tmp/x3-space-fog-research.md](/tmp/x3-space-fog-research.md). In particular,
Brown's accessible GDC2015 slides list gas clouds as future work; the session
abstract alone must not be cited as an implemented cloud algorithm.

### Spatial directional shafts (2026-09-20)

The requested next-flight extension modulates the directional incident-light
term at each occupied sample of the existing 24-step spatial march. It changes
`S += T * opacity * phase * chroma * visibility`; density, integration distance,
transmission and the empty-field identity law remain unchanged. There is no
uniform underlayer, screen-space beam mask, secondary light or density increase.
This section supersedes the unshadowed-only limitation of the initial spatial
production contract; flight appearance remains unaccepted.

The route supplies up to three active general-scene cascades in ladder order,
excluding cascade zero's own-ship-only map. A map requires successful replay
publication, an exact current-frame stamp, and rows composed from its retained
basis and the current camera. Surface-shadow application is not an admission
condition. In particular, the surface lane's permitted previous-frame far map
is unavailable to fog. Failed replay and Reset invalidate retained publication;
fog stores no map reference or lighting history between calls. Invalid/missing
maps independently fall back to a containing coarser current map or full light.
If every map is unavailable, the original unshadowed spatial field remains.

Each admitted map is a same-device square R32F texture with at least 64 texels.
The shader projects camera-relative **view** positions into these maps, keeping
the modulo-world position exclusively for density lookup. The D3D9 replay
convention places sample `(i,j)` at screen `(i,j)/N`; manual 2×2 bilinear PCF
filters four depth comparisons around this location through point samplers.
The depth comparison subtracts the existing computed **constant** bias:
`(configured_world_bias + 2*extent/N) / depth_range`. A volume sample has no
receiver surface plane, so neither slope correction nor the surface plane-fit
clamp (used by the older uniform fog fallback) applies.

Coverage requires normalized depth in `[0,1]` and maximum absolute XY at most
0.95. From XY 0.85 to 0.95, its weight decreases continuously into the next
containing available map, or full light. Coarser weights use the residual
weight, preserving a bounded `[0,1]` visibility even with overlapping bands or
holes in admission. Normal covered samples pay four map reads; overlap bands
can pay eight or twelve. Empty density samples skip all map projection/reads.
The same shared shader function runs in full-pixel edge repair, preventing a
lighting mismatch at depth/class boundaries.

Textures s4–s6 and constants c9–c21 are shared by march and composite. Existing
ALL-state-block capture/restore covers these textures, samplers and constants;
normalization explicitly initializes every touched sampler, both target binds
unbind all seven used pixel slots, and restoration unbinds all sixteen before
restoring caller state. Map validation precedes scene mutation, and loss during
validation aborts. No new GPU resource, allocation, Reset lease or per-draw work
is introduced. Map references remain borrowed for the serialized call; the
existing CPU/LastError guard, scene recovery and write-start/no-rollback contract
remain in force. Runtime qualification must exercise the additional samplers,
constants, actual comparisons, recovery and full transaction timing.

Native D3D9 compatibility uses only public texture/state APIs and ps_3_0. The
compiled march/composite use 315/506 instruction slots. Host compilation and
numerical witnesses do not establish native Windows execution. Captured
half-grid evidence finds very little overlap between these authored clouds and
existing caster shadows; this physically valid implementation therefore does
not promise visible shafts in arbitrary views. See the verification ledger.

### Engine camera precision admission correction (2026-09-21)

Run197's first-person fog disappearance is a camera precision refusal, not a
camera-mode restriction or the earlier cut-triggered warmup. The engine view
basis comes from fixed-point `/65536` values. Its small normalization drift can
exceed the fog helper's original `1e-4` row Gram-error tolerance while remaining
valid under `camera_state_from_matrices`' established `1e-3` near-rigid contract.
The helper now uses that same `1e-3` tolerance. The observed maximum is
`1.52630033e-4`; this measurement identifies the incompatible thresholds, but
is not a new bound derived from a single quantization operation.

The fog helper retains the true inverse, positive determinant range
`[0.999,1.001]`, finite-value guards and invalid-output behavior. It does not
normalize or reorthogonalize the view: doing so would alter agreement with the
engine's current depth and camera translation. The upstream projection/view
checks and current sector/resource authority remain required. At card
admission a malformed camera still forwards native cards without suppressing
color; a late failure after suppression retains the Reset-only fault latch.
There is no change to hook instructions, CPU/LastError boundaries, D3D state or
writes, resource lifetime, Reset/recovery or rollback behavior. The change is
one comparison constant: operation count, per-draw work, allocations and locks
are unchanged. Captured-camera and refusal evidence is in the
[verification ledger](../verification/volumetric-fog.md#run197-first-person-camera-precision-correction-2026-09-21).

### Finite cloud banks: offline prototype, not production

The next fixed preview changes spatial distribution rather than extending the
ubiquitous periodic field. Equal, disjoint world-space banks have radius P/2
(3.2768 km), a full-density core at 0.75R, and a cubic smooth feather to zero.
Centres occupy fixed 3P cells (19.6608 km) with deterministic, camera-independent
jitter bounded by P/4 per axis. The original family atlas is sampled in its
original global phase and scale inside each bank. The envelope multiplies both
density and premultiplied colour; no homogeneous layer or density compensation
fills the empty space. This preserves local appearance inside banks, not fog at
every historical camera position. All dimensions are design choices, not values
recovered from native fog cards.

Ray–sphere intersections bound integration to occupied intervals, clipped by
actual geometry depth and the smooth 30–40 km range window. Range culling uses
nearest support, not centre distance. The fixed candidate uses at most 500 render
units per midpoint step, compared against independent 128/64-unit references.
Equal-radius bank ordering must agree with explicit per-ray order. Empty space,
invalid depth and source alpha retain their identity laws.

Preview both tested families at fixed inside, boundary and outside poses, plus
10/30/38 km, then the original captured views without relocating banks to make
those views foggy. Reference appearance and candidate integration accuracy are
separate decisions. Show cloud-only fixed-scale images, not invented game
composites; include inside-cloud stress and smooth entry/exit. No seed, scale,
strength or sample-count search is bundled into this one experiment.

A possible portable runtime uses bounded interval marches and ordered S/T
composition into FP16 ping-pong targets. Per-bank fullscreen copies, visible
bank counts, depth repair, shader slots and Reset/state ownership need measured
qualification. Grouped bank passes or a documented format-blending capability
may reduce copy cost later, but are not selected implementations. Source
portability is not native Windows runtime verification. No game hook, candidate
build, installation or user flight is authorized by this offline preview alone.

The fixed preview was subsequently **not selected by the user**, who prefers
broader, connected clouds. Its converged reference is preserved as a comparison,
not the next production layout. The separate fixed 500-unit marcher also failed
its numerical gates. Further design must address organic connected regions and
clear pockets without turning support connectivity into an artificial street or
tube network, and without reintroducing a uniform sector-wide haze.

### Connected-region reference preview (2026-09-21)

The [offline reference](../../tools/analysis/fog_connected_preview.py) tests a
fixed paired-lobe region in each deterministic 5P cell, with P=32768 render
units. Each lobe has axes (1.5P,P,0.75P), fixed orientation and a smooth angularly
uneven boundary. The union uses the maximum envelope, multiplying the original
family density and premultiplied colour once; overlapping lobes do not add
extinction. Original detail scale/phase are retained, with no density floor.
Merged conservative ray intervals are clipped by depth and the 30–40 km taper.
These dimensions are authored choices, not recovered game fog parameters.

Six 128×72 reference views and two slices establish numerical convergence,
not production suitability. Nearby regions are broader, but distant views
still read as speckled ovals; appearance remains unselected. The largest
analytic cost is 1,558 atlas reads per ray at 128-unit spacing before lighting,
shadows or repair. There is no selected runtime integrator, GPU timing or new
flight build. See the [verification ledger](../verification/volumetric-fog.md#fixed-paired-lobe-connected-cloud-reference-preview).

User clarification after this preview: the interior is close to the desired
appearance, but the isolated distant bulb is rejected. The target is irregular
cloud patches extending through a sector or substantial parts of it, with
clearer gaps, rather than separately bounded cloud objects. Preserve the
interior as a visual reference; the paired-lobe distribution is not selected.

### Sector-wide macro modulation: negative fixed preview

A [fixed reference](../../tools/analysis/fog_sector_patches_preview.py) multiplies
the original family field by three-octave signed value noise at 8P/4P/2P, with
4:2:1 weights, fixed octave rotations and a smooth zero-to-one threshold. This
removes explicit lobe objects and varies density continuously; it is not selected.
The six comparison views converge numerically, but their long sightlines remain
cloudy almost everywhere. All A/C rays have positive macro support over the
complete 40 km; B averages 39.979 km. A 14.25% clear area in the separate sector
slice does not establish clear sightlines in those views. Original periodic fine
texture also remains visibly repeated. The user wants more clear space and
varied cloud density, size and shape, including internal variation.

Next design must distinguish broad cloud mass, internal detail and accumulated
opacity. Preserving the exact periodic atlas density is not an appearance
requirement; family colour/style and the liked nearby structure are the useful
references. The preview does not qualify angular antialiasing, a runtime marcher,
GPU cost or native behavior. No threshold/seed search followed this result.

### Mass/detail reference: selected visual direction, not production

The [fixed reference](../../tools/analysis/fog_mass_detail_preview.py) separates
world-stable cloud mass from smaller-scale internal modulation and erosion.
It replaces the old repeating alpha field as primary density, preserving a
density-weighted family mean colour for this comparison only. The resulting
A/B views remove the isolated bulb and diagonal fine-pattern carpet. They show
broad connected forms and visually dark/open-looking regions; internal mottling
is still subtle. The user finds this close and requests a little more patchiness
and density variation. Keep the mass layout and make one modest detail refinement.

Four base views and two B pixel-area witnesses pass reference checks. Along-ray
128/64 transmission error stays below 0.000003577; the area witness is one
comparison, not proof of angular convergence. Spatial family chroma, a bounded
runtime representation, GPU cost/state/Reset behavior and flight appearance
remain unresolved. No shader/baker or new production fog is selected.

The requested modest detail refinement preserves the mass field, scales and
world coordinates. It changes the density from
`max(0,B*(.65+.35*Dlo)-.15*(1-B)*Dhi)` to
`max(0,B*(.50+.50*Dlo)-.20*(1-B)*Dhi)`. This only removes density: it introduces
no peak boost, global gain or floor. The reviewed two-view comparison shows
stronger internal variation and weaker edges while retaining the broad layout.
It is an offline visual reference; runtime representation and flight acceptance
remain unresolved.
