# Thin glow lines (light-map window strips) at medium range: tearing and flicker

Status: design note for ratification, 2026-09-22. Nothing here is implemented. Tags: **[M]** measured this session,
**[I]** inferred, **[A]** assumed. Context: [taa-distant-line-fade.md](taa-distant-line-fade.md) §11-13 (light-map far
fade `80,220,1`, far stabiliser and its speed gate), [taa-lattice-crawl.md](taa-lattice-crawl.md) §13 / §32 (thin
region, camera gate, 7x7 box).

Inputs: `/tmp/x3-bottleX3-run225` (Run63 DLL `b0cde491`, `X3M_CAPTURE_FRAMES=8`, `X3M_TAA_DEBUG=0`: frames 6516-6523,
one full jitter cycle, `hdr_1` = the jittered FP16 scene before the resolve, `depth_1` four-channel lane, `motion_1`;
**no `taa_1` output dump**, so every post-TAA number below is a host replay of `resolve.hlsl`, not the game's output).
1280x768, options as installed (hull light-map gain 4, far fade 80,220,1, far stabiliser 0.985 / 0.03-0.25, thin
region 0.97 camera gate, `X3M_TAA_MIP_BIAS=-0.5`, sharpen 0.75). Camera pitching: routed motion median 5.7-6.6 px/frame,
the station translates 5-10 px/frame on screen. Compact numbers:
`verification/results/thin-glow-lines-run225.json`; scripts in the session scratchpad (untracked).

## 1. Decision

1. **The strips are sub-pixel emitters sampled below the pixel pitch in the current frame; the resolve does not
   restore them because a 1-px line cannot survive a fractional history resample, not because the clip rejects
   them.** The far fade is irrelevant at this range (footprint 13 units/px, fade starts at 80), the far stabiliser and
   the screen-gated thin region are closed by the 6 px/frame motion, and the emissive-aware clip (option d) changes
   nothing measurable (clip loss on strip pixels is 0.000). Section 2.
2. **Recommended first step, no build: fly `--taa-mip-bias 0` and `--taa-mip-bias 0.5` against the installed
   `-0.5` at the run225 station** (option b', section 3). It moves the light-map texture toward being pre-filtered at
   the pixel footprint, which is the source-side form of the energy-conserving widening (option a). The host
   emulation of that widening cuts the per-frame peak change of a tracked strip from 8.6 % to 4.3 % (median) and
   the tearing tail (p90) from 25 % to 22 % after the resolve, and from 48 % to 37 % before it, for an 11 % lower
   peak. If the A/B reads better, the production form is the same bias on the light-map stage only (stage 2 / 3 of
   the gained hull variants), a per-draw `SetSamplerState` the route already issues, no shader change.
3. Not recommended now: (c) forcing the strips into the thin region, (d) the emissive-aware clip, (e) supersampled
   emission. Section 3 gives the numbers. Raising the render resolution helps in proportion to the strip's coverage
   (section 4): 1080p turns the median strip here (0.76 px coverage proxy) into a whole pixel, not the p10 (0.52).

## 2. What the capture shows [M]

**The lines.** Station model `5428` (36 routed draws, 116 k primitives, hull program `5f82ecac…` with the light-map
gain variant, light-map texture stage 2 = 1024x1024 DXT1, 11 levels, anisotropic 16 / linear mip, game LOD bias 0,
route bias -0.5). View z on the strip pixels p10 / p50 / p90 = 5643 / 6635 / 8879 units (1.1-1.8 km), footprint 13
units/px: **six times nearer than the far fade's P0** and far below the far stabiliser's 80-130 band (`farw` 0 on every
strip pixel). Strip pixels (HDR luma > 0.8, routed, valid depth): 697-738 per frame; luma p50 / p90 / max 1.64 / 2.90
/ 3.66 against hull 0.164; strip / 5x5-median contrast p10 / p50 / p90 1.1 / 4.4 / 10.2. Geometry: **100 of 152 vertical
runs are 1 px tall**; horizontal runs 1-6 px (the spar's windows are authored dashes: 31 x 1 px, 55 x 2, 32 x 3, 29 x
4-6). The drum's vertical window columns read as dotted lines of 1-px dots.

**Per-phase presence.** Aggregated over the spar box, lit pixels per phase 383-406 and lit energy 721-749 (± 3 %): the
jitter moves the energy between pixels, it does not lose it. Per emitter (64 dashes tracked through the chained
motion field, 3x3 peak around the tracked centre): mean-over-phases / max-over-phases peak **p10 / p50 / p90 = 0.52 /
0.76 / 0.93**. Frame to frame (nearest raster pixel holding the same content, jitter accounted), the peak of a dash
changes by 4.9 % median and **48 % at p90**: one dash in ten loses or gains half its brightness every frame. That is
the "torn" line.

**What the resolve does with it (host replay, plain `resolve.hlsl` law, W 0.9, k per frame, frames 6519-6523
scored).** On strip pixels: history accepted 1.000, depth rejection 0.000, **clip loss 0.000** (median; the 3x3
contains the emitter, so the box holds it), current luma 1.65, **fetched history luma 0.88**. The history of a 1-px
line, Catmull-Rom resampled at a new fractional offset every frame (fy 0.31-0.90 across the burst), is a 2-3 px band
at about half the peak; the output is 0.9 of that plus 0.1 of the sharp current. Along the motion path the output's
peak changes 8.6 % median (input 4.9 %) and 25 % at p90 (input 48 %): the resolve halves the tearing tail and doubles
the median flicker, because the resampled peak depends on this frame's fractional camera step. W 0.95 changes neither
(8.4 % / 24 %). Nearest-pixel rms change over the dilated strip band: input 0.47, output 0.32 (x 0.69).

**Masks.** Thin-region membership (section 13 law reproduced offline: fragmented = two depth-class changes on a 7-tap
line, grown 7x7): **12.1 % of strip pixels**, 1.2 % fragmented themselves; the strips are hull interior. The screen
gate closes at 6 px/frame; the camera gate (a static station under camera rotation) would open on that 12 %. The
far gate is closed by depth and by speed. So at this station in this motion the installed resolve is the plain one.

**Options replayed (same frames, same scoring; "in" = before the resolve, "out" = after):**

| variant | in peak change p50 / p90 | out peak change p50 / p90 | out nn rms | out peak (median) |
|---|---|---|---|---|
| installed law | 0.049 / 0.481 | 0.086 / 0.254 | 0.323 | 2.06 |
| W 0.95 | same | 0.084 / 0.241 | 0.326 | 2.02 |
| (d) clip released where the 3x3 holds an emitter > 0.6 and history <= 1.1 x its max | same | 0.092 / 0.254 | 0.333 | 2.09 |
| (d) sigma term dropped where the 3x3 holds an emitter | same | 0.086 / 0.254 | 0.323 | 2.06 |
| **(a) emissive excess over 0.5 spread by a 3x3 tent, energy conserved** | **0.037 / 0.366** | **0.043 / 0.217** | **0.227** | 1.83 |
| (a) vertical [1 2 1]/4 only | 0.047 / 0.404 | 0.064 / 0.276 | 0.307 | 1.99 |
| (a) tent + W 0.95 | 0.037 / 0.366 | 0.044 / 0.224 | 0.232 | 1.82 |

The spread is a host emulation of a wider source footprint (a positive light-map mip step or an analytic-AA emitter);
it is not bit-exact for either. (d) is inert: the clip does not bind on these pixels. Tracked 5x5 box-energy CV (a
coarser metric, alignment-limited): installed 0.28 out / 0.30 in; tent 0.24 / 0.30; (d) 0.26-0.28.

## 3. Mechanism and options

**Mechanism.** (i) The window strip is narrower than a pixel at 1.3 km (coverage proxy 0.76 median, 0.52 p10). With
the route's -0.5 LOD bias the sampler picks a mip whose texel is about 0.7 px, so the strip is a band under one pixel
wide in the sampled texture and a pixel row can straddle it: the per-phase peak varies up to 2x, the jitter moves the
gaps every frame (tearing). (ii) The resolve accepts the history (no depth or clip rejection) but the history holds
the strip only as a resampled band at about half the peak; each new fractional camera step resamples it again, so
the displayed peak follows the fractional part of the camera motion (flicker under motion). This is §10's
resampling-blur mechanism, at 6 px/frame instead of 0.1. (iii) The stabilisers are all closed here by design. Not
supported by this capture: clip rejection (0.000 loss), depth rejection (0.000), geometric aliasing of thin quads
(the strips are texels of large hull triangles, not geometry).

| option | what the user sees | hot-path cost | slots | risk / fixture |
|---|---|---|---|---|
| **(b') light-map mip bias 0 or +0.5 instead of -0.5** (first: global `--taa-mip-bias`, no build; then stage 2 / 3 of gained hull variants only) | strips wider and dimmer at the same energy, fewer gaps; 11 % lower peak in the emulation; other hull textures unchanged in the stage-only form | per routed draw: the route already sets `MIPMAPLODBIAS` per eligible stage; a per-stage value adds at most one `SetSamplerState` when a stage's role changes between draws; zero per pixel | none (no shader) | documented D3D9 sampler state, identical on native Windows. Fixture: a routed quad with a 0.5-px emissive strip texture and full mip chain drifting 6 px/frame, sweep bias -0.5 / 0 / +0.5 / +1, measure per-phase peak ratio and the resolve's along-motion peak change; expect the emulation's ordering. Open: anisotropic LOD selection may already integrate across the strip on oblique hulls, so the gain could be smaller than the emulation |
| (a) analytic footprint in the hull pixel shader: emissive term = light-map sample spread to >= 1.5 px at fixed energy (a `tex2Dbias` / `tex2Dlod` of the light-map stage by the pixel's texel density, or a 3-tap blur of the light-map fetch) | as (b') but exact and per pixel, independent of aniso and mip chain | +2 fetches or one biased fetch per hull pixel of every gained draw (the light-map fetch is one of 3-4); stations cover 2-5 % of the frame at this range | gained variants 264 / 512, room for +4-10 | needs the ddx/ddy of the light-map UV (ps_3_0 `dsx/dsy` or `texldd`); the hull programs are transformed game bytecode (shader model unverified here), so this is a bytecode transform with proof, weeks not days. Take only if (b') proves the mechanism and its stage-only form is not enough |
| (c) put the strips in the thin region (add an emissive-contrast criterion: routed, valid depth, luma > T and > 3 x the 5x5 median, run <= 2 px) | W 0.97 with the camera gate open under pans; but the history is the same resampled band, so the peak stays about half and the per-step flicker (which W 0.95 did not move) stays | line mask +1 colour fetch per pixel of the 5x5 (25 taps) or a separate 3x3 pass | `line_mask_camera` 245, `resolve_far_camera` 508 / 512: the strength lands in the existing b channel, no resolve change; the mask program grows | the replay says W does not address this failure mode; a criterion on luma also admits sun glints and engine lights |
| (d) emissive-aware clip | nothing: measured 0.000 clip loss, output metrics within 0.01 of the installed law | `resolve_far_camera` +4-8 slots, none available | 508 / 512 | rejected on evidence |
| (e) 2x2 supersampled emissive term in the hull shader | exact per-pixel coverage of the strip: the input peak change would fall toward 0 and the resolve's history band would hold a stable 1-1.5 px line | 4 light-map fetches per hull pixel of gained draws; same transform difficulty as (a) with more slots | as (a) | superseded by (a): the same fetch budget buys an analytic footprint |
| (f) accept and document | as today | 0 | 0 | the user asked; not the recommendation |

**Native Windows.** (b') is `D3DSAMP_MIPMAPLODBIAS` on a documented stage, already applied by the route on Windows
and CrossOver alike; no capability boundary. (a) / (e) are shader transforms with the same bytecode proof rules as the
gain MUL. Nothing here reads Wine internals.

## 4. Resolution

Pixel pitch at 1920x1080 is 0.667 of 1280x720, so every strip's coverage grows x 1.5. From the run225 coverage proxy:
the median dash (0.76) becomes >= 1 px and stops tearing; the p10 (0.52) becomes 0.78 px and still tears, with the
peak-change tail roughly in proportion (p90 48 % -> about 30 % **[I]**, by the same coverage argument; the exact
tail needs a 1080p capture). The resolve's motion flicker (mechanism ii) is unchanged in relative terms for a line
that is still sub-pixel, and falls for one that reaches 1.5 px: the emulated 3x3 spread is roughly what 1080p does to
the median strip. Cost: TAA pass 1.18-1.33 ms at 1280x768 in the fixture record (`temporal-pass-summary.json`,
CPU wall with event-query drain; not game GPU time) -> about 2.5-2.8 ms at 1080p (x 2.11 pixels; x 2.25 against
720p). HDR write-back, bloom and the half-resolution fog scale by the same factor; no per-pass GPU time for those is
logged in run225 (the log carries CPU submit times only: `taa_run` 0.27 ms, `hdr_writeback` 0.14 ms CPU), so the
whole-frame cost at 1080p is unmeasured. Presenting 720p from an internal 1080p render is not a post-pass change:
the game's back buffer, viewport, scissor and pre-transformed (XYZRHW) HUD vertices are in pixel units, so the proxy
would have to scale every viewport and screen-space draw and downscale at present; that is a project on its own and
costs the full 2.25x of scene work, not only the passes. If the display allows it, the game's own 1920x1080 setting
is the cheap way to get the same effect. **[A]** the user's reason for 720p is not recorded; if it is performance,
(b') is the only option here that costs nothing.

## 5. Recommendation and what to capture

1. Fly, at the run225 station (spar and drum in view, 1-2 km), three A/Bs without a build: `--taa-mip-bias -0.5`
   (installed), `0`, `0.5`. Judge: gaps in the spar dashes and drum columns while still, and their flicker while
   pitching the camera as in run225. Expect softer hull textures at 0 / 0.5 (the bias is global in this form); that
   is the price of the A/B, not of the production change.
2. Capture each as an F8 burst with `--taa-debug` (32 frames, `color_1` / `taa_1` / `taa_mask` present) so the
   post-resolve numbers above can be measured on the game's output instead of a replay, once still and once pitching.
3. If 0 or 0.5 reads better: implement the light-map-stage-only bias (the gained hull variants know the stage) behind
   an option, with the strip fixture of section 3; if it reads the same: the mechanism is (ii) alone and the next
   candidate is (a) in its cheapest form (a single `tex2Dbias` on the light-map fetch of the gained variants, bias
   from a per-draw footprint constant, no derivatives), with the same fixture.

## 6. Unknown

- The light-map texel density on the spar (UV scale) and therefore the actual mip level: settle by dumping texture
  identity 2353 (frame 6516 draw 206) and the model's UVs, or by the strip fixture, which does not need it.
- Whether anisotropic filtering already integrates across the strip on this hull (it selects LOD from the footprint's
  minor axis): the fixture with an oblique quad answers it.
- The game's post-resolve tail (bloom, AgX, RCAS 0.75) on these strips: RCAS amplifies a 1-2 px band's contrast and
  may be the part the user names "flicker"; needs the `--taa-debug` capture of step 2 (the run225 burst has no
  `taa_1`).
- The 8-frame burst holds one jitter cycle under motion; every per-phase number is from one pass through the phases.
