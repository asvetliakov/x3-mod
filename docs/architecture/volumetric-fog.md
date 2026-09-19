# Volumetric sun fog and sector haze

Design note, 2026-09-19, for ratification. Not implemented. Stage 0 (offline mock-up on real dumps) is done;
its pictures are the go/no-go input for the user. Tool: `tools/analysis/fog_offline_mock.py`.
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
- **Automatic rule**: `N s` is the engine's per-sector fog start, read once per frame from the sector camera.
  `tau_max = S x 0.03 x min(2, 250000 / (N s))`, haze half distance `N s / S`, `S` the global strength (default 1,
  0 = pass detached). Clear sectors (N s = 5,000,000) get `tau_max` 0.0015 and `Ta(100k units)` 0.986: near zero
  without a sector list.
- **Stack with the engine's fog, do not replace it.** The engine's "fog" is an alpha fade of distant nodes, not a
  colour fog (section 1), so there is nothing to double.

A sector-wide homogeneous medium (the brief's starting point) loses; the mock shows why (section 2).

## 1. How the game expresses fog (evidence)

| Question | Finding |
| --- | --- |
| Fixed-function fog | **Never enabled.** `state id=28` (D3DRS_FOGENABLE) snapshot per captured draw: 534,758 rows at 0 and 0 rows non-zero over 115 session logs under `/tmp/x3-bottleX3-run*` (M). The motion rows' `fog=-1` means the game never calls `SetRenderState(FOGENABLE)` in the recorded windows (the shadow reports -1 for a state never set). FOGCOLOR/START/END/DENSITY/TABLEMODE/VERTEXMODE are not in the snapshot list (`capture.cpp:707`); with FOGENABLE at its default they are inert. |
| Shader fog | `g_EnableFog` (b0), `g_FogClip` (c41), `g_AlphaValue` (c39): a per-node **distance fade to transparent** between N and F, `alpha = g_AlphaValue x saturate(g_FogClip.x - g_FogClip.y x distance)` (D, `docs/reverse-engineering/asteroid-fog-temporal.md`). It carries no colour. |
| Per-sector source | Sector/type record table `*0x00606fc0`, stride `0xdb8`, fields `+0x148`/`+0x14c` copied to the sector camera `+0x36c` (N) / `+0x370` (F), enable flag `+0x270 & 0x10000` (D, same note). With the configuration integer at 3 the engine raises F to 500,000,000 native, so **N is the datum that still differs per sector**. |
| Values seen | `object_fade` rows, all logs: (N, F) native = (25,000,000, 30,000,000) in 37 runs, (50,000,000, 55,000,000) in run48, (500,000,000, 500,000,000) in 21 runs; scale 0.01 (M). The 25M sector is the green-nebula sector of run153 f7741/run160, the 500M sector is the plain starfield of run157 (M, by dump). So N tracks "nebula sector" in the three cases available (I: three points only). |
| Nebula draws | Background is draw index 1, VS `7b6393fe2d3e1d85` / PS `6109cf64c03529dd` (the `nebula` program, shared with `gui2d`), 12 draws with that pair in run157 f5920 (M). The `nebulafog` PS `f7e0b6647a3bfa62` is drawn in **none** of the 115 logs (M). Neither program integrates a volume (D, `shader-family-review.md`). |
| Universe-map attribute | Not traced. That the `0xdb8` record is the background/sector type row and which map attribute fills `+0x148` is (I); the record's other fields (a fog or nebula colour, if any) are unknown. |

Needed from the user: one `--taa-debug` capture inside a sector with in-sector fog clouds around the ship (not
only a painted background). It settles whether `nebulafog` sprites and a smaller N appear there, and gives the
frame on which term B must not fight the sprite clouds. Disassembly that would settle the rest: the loader that
fills `*0x00606fc0` (which file column lands at `+0x148/+0x14c`, and whether a colour sits beside them).

Colour and anisotropy: no engine colour is known, so `albedo = lerp(white, hue of the mean sky radiance, 0.5)`,
sun colour from the tracked sun light, `g = 0.6` fixed. `Sky_local` is the sky-only image at about 1/16
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
  is heavy. Hence `tau_ref = 0.03`.
- One-frame noise of F against the 8-phase mean (IGN jitter), station frame: 8 steps mean 0.0146 / p99 0.067,
  16 steps 0.0070 / 0.037, 32 steps 0.0038 / 0.021.

The mock cannot show: temporal behaviour (shaft crawl under camera motion, TAA clamp response, the project's
shimmer history), bloom picking up the glow, RCAS, glows and lens flare in front of fog, the real sun colour,
8-bit F storage, the optional density noise, PCF vs the single tap, and any sector with sprite clouds.

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
values; sky reprojection is unchanged. HDR: all terms are finite, <= E_sun p_max (0.8 E_sun at g = 0.6).

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
light is untested (the mock derived it from the image); (5) sprite-cloud sectors unseen.

## 5. Stages

| Stage | Content | Proof |
| --- | --- | --- |
| 0 | This note, the mock, pictures | User judges `station_sheet.png` / `plant_sheet.png` and picks S |
| 1 | HLSL march + composite; host reference test: numpy march of the mock vs a software evaluation of the shader maths on a synthetic depth + one synthetic cascade (as `test_ambient_occlusion_reference.py`); sector-camera read with validation | host test; detached Wine fixture, EVENT-fenced timing at 768p/1080p, byte-identical scene at S = 0 |
| 2 | Temporal fixture: inject the term into `taa_resolve_replay.py` over the 32-frame run153/run160 windows | band/flicker metrics within the current installed build's limits |
| 3 | Live pass behind `--fog` (default off), rule on, S settable | paired on/off flight, +<= 0.7 ms median; user capture in a nebula and a clear sector, glow dimming <= 3 % |
| 4 | Optional: bloom-mip local glow, density noise | user A/B |

## 6. Alternatives that lose

- **Sector-wide homogeneous medium to a capped distance**: section 2; veil without shafts, or shafts with
  everything past 50k units erased.
- **Froxel volume**: D3D9 has no 3D render target writes; a 2D-atlas froxel grid costs several more quads on a
  CPU-bound frame for multi-light support this design does not need.
- **Blend-only composite (no scene copy)**: saves the copy and a sampler, wrong by up to 2.3x in linear where
  wash and scene are comparable, which is exactly the nebula sky.

## Unknowns

N for denser sectors and for sprite-cloud sectors (user capture); the record's colour fields and map attribute
(loader disassembly); GPU headroom (unmeasured); whether cascade 1 holds the own-ship casters; whether an
existing FP16 scratch and a colour-carrying meter chain can be reused; E_sun from the tracked sun light.
