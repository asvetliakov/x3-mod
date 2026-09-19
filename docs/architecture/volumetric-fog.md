# Volumetric sun fog and sector haze

Design note, 2026-09-19, for ratification. Not implemented. Stage 0 (offline mock-up on real dumps) is done,
and was redone the same day on the first real fog-sector frames (run174, Argon Prime: section 2b, which
**revises the rule, the medium colour, the anisotropy and the reference strength** below). The pictures are the
go/no-go input for the user. Tool: `tools/analysis/fog_offline_mock.py`.
(M) = measured in this session, (D) = documented elsewhere in the repository, (I) = inference.

## Decision

**Two analytic terms driven by one engine value, with a single half-resolution march that returns only a
scalar "lit fraction"; composited once at the scene-end hook before TAA.**

- **A, sector haze (aerial perspective), geometry only.** `L <- L Ta + Sky_local (1 - Ta)`,
  `Ta = 2^-(d / (N s / S))`. Distant objects sink into the colour of the background behind them; sky
  pixels are untouched, so stars and the painted nebula never turn to soup.
- **B, sun medium local to the camera.** Density `sigma0 exp(-t / R)`, so optical depth is bounded:
  `tau(d) = tau_max (1 - exp(-d / R))`, sky `tau_max`. `L <- L Tb + albedo E_sun p_HG(cos) F (1 - Tb)`.
  `Tb` and the phase are analytic per full-resolution pixel; only `F = int sigma T V dt / (1 - Tb)` (V = cascade
  visibility) is marched, at half resolution, 16 jittered steps, one nearest tap in cascades 1-3.
- **Automatic rule (revised after run174)**: term B is keyed on the background record's `NumDustInstances` D
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
- **Stack with the engine's fog, do not replace or re-shade it.** The distance fade carries no colour, and the
  fog cards are a faint, never depth-tested screen-blend veil that the TAA already handles (section 2b).
  Enhancing the cards instead of adding a medium loses (section 6).

A sector-wide homogeneous medium (the brief's starting point) loses; the mock shows why (section 2).

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
