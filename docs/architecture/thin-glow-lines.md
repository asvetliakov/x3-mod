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

## 7. run227 A/B: `--taa-mip-bias 0.5`, 2026-09-22

Status: triage, no build, no fixture. Tags: **[M]** measured this session, **[I]** inferred. Inputs: `/tmp/x3-bottleX3-run227`
(Run63 DLL, `X3M_TAA_DEBUG=1`, two F8 bursts, frames 3943-3950 and 5383-5390, station model `00005428` confirmed in
both, 99 routed draws/frame vs run225's 36 — different pose/LOD, same model id). Baseline: run225 (§1-6), bias -0.5.
Compact numbers: `verification/results/thin-glow-lines-run227.json`. The original run225 scoring scripts (scratchpad)
are gone; this session's metrics are a fresh, partial re-implementation from §2's description, not the same code.

**(1) Bias applied [M].** `grep -a -o mip_bias=... session-*.log` on the `motion_output_mode` / `motion_output_device`
per-frame lines gives `mip_bias=0.5` at both burst starts (frame 3943 and 5383), against run225's `-0.5`. This is the
engine's applied per-frame state, not only the launch option. It is a single **global** sampler value (every stage),
the §5 recommended A/B form, not the production light-map-stage-only form.

**(2) Same strips, robust metrics [M].** Mask = HDR luma > 0.8, depth valid, view_z in a 5000-9500 unit band (a
z-band proxy for the station, not the note's exact spar box, which could not be recovered). Per-phase lit pixel count
and energy stay within +-3.4% of their mean at both bias settings (run225 2.7%, run227-A 2.3%, run227-B 3.4%): same
"jitter moves energy, does not lose it" behaviour as §2. Horizontal run-length histogram, 1-px-run share: run225
(bias -0.5) **0.379**, run227-A (bias +0.5) **0.296**, run227-B **0.326** — a real but modest ~15-22% relative drop
in the narrowest dashes in both independent bursts. Vertical 1-px-run share: run225 0.566, run227-A 0.577 (no
change), run227-B 0.504 (some widening). **Per-emitter tracked metrics (peak ratio, frame-to-frame change, post-TAA
change, gap fraction) failed a sanity check**: re-running this session's forward tracker on run225's own frames gives
a p50/p90 frame-to-frame change of 0.220/2.530 against the note's reported 0.049/0.481 — 4-5x off, almost certainly a
jitter/direction bug in the quick motion-vector decode (it omits the raster-jitter and jitter-UV terms that
`tools/analysis/analyze_motion_readback.py`'s `analyze_pixels` applies). Those numbers are recorded in the JSON for
completeness but are **not used** for the verdict below.

**(3) Verdict [M/I].** The bias did change the strips, by a small, measurable amount (horizontal narrowing of the
1-px-run share, not vertical, in both bursts), not by nothing and not by enough to read as "helped." This rules out
one of the three candidate explanations directly: the bias is confirmed global and applied to the light-map stage (it
is applied to every stage), so **it is not a routing/stage-reach problem**. The small, axis-inconsistent size of the
effect is consistent with the note's other two open explanations — anisotropic filtering already integrating across
the strip on this oblique hull, or the mip already landing on a texel near/above 1 px before the bias moves it the
rest of the way — and this A/B does not distinguish between them; both remain open, as in §6.

**(4) Bearing on option (a) vs (b') [I].** A full 1-stop global bias swing (twice the size of any plausible
stage-only change) produced a sub-threshold, partly inconsistent effect. A stage-only bias, a strict subset of this
A/B's effect, is very unlikely to do better. This is weak additional evidence for the note's §3 preference for (a),
the per-pixel analytic footprint, over further tuning of (b').

**Open issues.** (i) The tracked per-emitter metrics need a corrected, jitter-aware re-implementation (or the
original script, if it can be found) before per-emitter peak-ratio/flicker numbers can be trusted again. (ii) The
z-band ROI here is a proxy for the note's spar box, not the same region; a repeat with the same pose as run225 would
let lit-pixel/energy counts be compared directly instead of only their phase-stability. (iii) Distinguishing "aniso
already integrates" from "mip already >= 1 px" still needs the note's oblique-quad fixture (§3, option b' row);
nothing here settles it.

## 8. After run231, 2026-09-22: what gives bright, continuous, stable strips

Status: design note for ratification; nothing here is implemented. Tags: **[M]** measured (this session's source
reading or the ledger's numbers), **[C]** cited from the ledger / widening note, **[I]** inferred, **[A]** assumed.
Inputs: [hull-emissive-widening.md](hull-emissive-widening.md) (§1.3-1.5, "As built"), its ledger
[hull-emissive-widening.md](../verification/hull-emissive-widening.md) (run231 triage, follow-ups 1-2),
`fade_route_core.h::lightmap_widen_scale` (per-draw `k` from the origin's view z), `linear_material.cpp`
`lightmap_widen_fetch` (the 16-DWORD `dsx`/`dsy`/`mul`/`texldd` block, temp `rG`, constant `c217.z`),
`line_mask_ps.hlsl` (the 7-tap line test reads only depth `s1` and motion `s4`, no colour **[M]**; `s0`, `s2`,
`s3` unbound **[M]**), `temporal_pass.h` lines 90-150 (thin-region weight `min(n/(n+1), W)`, camera gate, sentinel).

### 8.1 Three distinct failures, three distinct levers

The Run64 flights separate what §2-3 lumped together as "tearing and flicker":

| symptom (user, run) | measured cause | which lever moves it |
|---|---|---|
| torn / gapped strips under pitch and yaw (run225/227/232) | a sub-pixel strip's history is a Catmull-Rom-resampled band whose peak follows the fractional camera step (§2 mechanism ii); W 0.9 -> 0.95 changed nothing **[C]** | only a wider **source** band: a 3 px band survives fractional resampling; that is the widening (`k`) and nothing on the TAA side |
| flicker at true rest, with and without widening (run231 0.229, run232 0.392 of peak pre-TAA **[C]**) | the 8-phase jitter samples the strip at 8 sub-texel positions; the resolve at W 0.9 leaks a fixed fraction of a period-8 input | only the **history weight** at rest: the widening's floor is already 0.83 at k = 3 and 0.875 at k = 4 (§1.4 of the widening note) — no source-side change buys another x3 |
| strips "almost disappeared" facing them; "appear / grow" under yaw (run231) | energy conservation: peak `I * w / k` (x 0.25 at k = 3 for w = 0.76 px) against the old x4-gained, 1-px, torn dot at up to `I` **[C]**; and `k_draw` is a function of the object origin's view z, which rises to its maximum for a centred (frontal) object and falls off-axis, with the frame's `k_min` swinging 1.7-3.0 under yaw **[C]** | a **brightness law** that is not energy-conserving for thin content (a choice of look, see 8.2), and a **per-pixel** `k` that does not depend on where the object origin sits in the frame |

Leak of a period-8 input through the resolve's IIR, `|H(w)| = (1 - W) / |1 - W e^{-iw}|` at the jitter fundamental
`w = 2 pi / 8`: W 0.9 -> **0.136**, W 0.97 -> **0.040** (x 3.4 lower; the 2nd and 4th harmonics 0.074 -> 0.022 and
0.053 -> 0.015) **[M, arithmetic]**. run231 measured a post/pre bright-pixel delta ratio of 0.20 at W 0.9 **[C]**;
at W 0.97 the same source would show about 0.06, i.e. a displayed swing near 1.4 % of peak for the k = 3 source
(0.229 x 0.06) instead of 4.6 %.

### 8.2 The brightness question is a look decision, not an estimation problem

Peak preservation for a strip narrower than a pixel means displaying `I` (or `I * w`, the coverage-exact value a
converged TAA shows) where the widened fetch returns `I * w / k`. Restoring it needs `w`, the
strip's screen width. **No phase-stable estimate of `w` exists below one pixel** **[I, from the law]**: any fetch at a
footprint <= 1 px carries the very phase ripple the widening removes (floor 0.29 at the installed -0.5 bias), so a
ratio `fine / coarse` re-imports it exactly (`coarse * (fine / coarse) = fine`); and two coarse fetches (footprints
`k`, `2k`) only tell *whether* the feature is narrower than `k` (ratio 2 for a line, 4 for a dot, 1 for a panel
interior, at most 1.33 at a panel edge `k / 2` px inside), not how much narrower. The general form "use the finest
footprint the ripple allows" collapses to choosing a smaller `k` (k = 2: peak `I * w / 2`, floor 0.75), which the
flights have effectively already bracketed (k = 3 too dim; k = 1 torn).

So the bright option is an explicit **thin-emitter boost** `B` applied where the coarse ratio says "thinner than
about `1.3 k` px", and `B` is the user's dial: `B = 1` is the energy-conserving law as built, `B = k` displays a
sub-pixel strip at `I * w` (the same peak a converged, un-widened TAA would show, in a `k` px band instead of 1 px).
Its known cost: a feature between 1 and `1.3 k` px wide (2-4 px dashes at k = 3) is over-brightened by up to `B`
(peak `I * min(w, k)` for `w <= k`), a 1-px dot by `B` short of its own area law. This matches the user's recorded
preference for cheap > 1.0 emitters over an exact law and reads as bloom-like (wider emitter, brighter core); it is
still a x3 in the base image and the fixture measures it. Panel interiors and edges are protected by the ratio gate
(8.3), and near hulls by `k = 1`.

### 8.3 Recommendation: three changes, two builds

**R1. Per-pixel `k` from the light map's own texel footprint (option b).** In the existing block, after `dsx`/`dsy`:
`rho_max^2 = max(dp2(rG.xy, rG.xy), dp2(rG.zw, rG.zw)) * (size * c)^2`; `k = clamp(sqrt(.), 1, K)` (`dp2` x2, `max`,
`mul`, `rsq`, `rcp`, `min`, `max`: **+8 slots**, no fetch), then the existing `mul rG, rG, k`. `(size * c)^2` is one
per-draw lane (`c217.y` is uploaded and unread **[M]**; `c216.zw` too: `pixel[2]`, `[3]`, `[5]` are 0 in
`motion_output.cpp:5505`); `size` = level-0 width of the bound light map, read once per texture with
`GetLevelDesc(0)` where the mip-bias shadow already reads the level count (`SetTexture` hook), `c` = the ramp
constant (`k = K` from `rho >= 1/c`; first value `c = K`, so a 1-texel strip is widened by `K` from the distance
where it is 1 px wide and not at all where it is `K` px wide). Effects: `k` is continuous per pixel and per
frame, the yaw pop and the frontal maximum of `k_draw` disappear (both come from `rows[15]`, not from the strip),
`Q0`/`Q1` and the per-draw seam go away, and the near band softens by `k * rho = c * rho^2` instead of a per-draw step.
The widened variant is then bound for every gain draw (no `k_draw = 1` fast path); near hulls read level 0 with
`k = 1`, which the fixture measured bit-identical to `texld` on 11 programs on this backend **[C]** (native: the
documented LOD law says the same; unverified). Cost at dock (hull fills the frame): +15 slots on programs of
34-71 original slots, no extra fetch.

**R2. Thin-emitter boost (option a, guarded).** After the widened fetch: a second `texldd` at gradients `2 * rG`
(`add rG2, rG, rG`; the block already holds `rG`), luma of both (`dp3` x2), `r = L_k / max(L_2k, eps)` (`max`,
`rcp`, `mul`), `t = saturate((r - 1.35) / 0.15)` (`mad_sat`), `g = 1 + (B - 1) * t` (`mad`), `rL.xyz *= g` (`mul`):
**+12 slots, +1 fetch**, one more temp, `B` and the thresholds in one `def` on a free constant (`constant_free`
check as for `c223`) or `B` per draw in `c216.z`. The gate band 1.35-1.5 sits between the panel-edge maximum (1.33)
and the strip minimum under the k = 3 ripple (2 x 0.83 / 1.0 = 1.66) **[M, arithmetic]**, so `t` is saturated on
strips (no ripple amplification) and 0 on panel interiors, smooth gradients and edges; DXT1 quantisation of the two
levels is the margin's enemy and the DXT1 fixture copy measures it. `B` is a launcher parameter
(`--hull-emissive-widening K,B`, `B` in `[1, K]`; the far fade `c217.w` composes multiplicatively as today). Luma
rather than per-channel keeps the boost hue-neutral on quantised mips. With R1 and R2 in one block: **+27 slots,
+1 fetch** per hull pixel of a widened draw; largest variant 271 + 20 = 291 of 512 **[C, from the as-built table]**.

**R3. Emissive vote in the stabiliser mask (option c).** In `line_mask_ps.hlsl` tests draw (`c7.z = 0`), `b` also
becomes 1 where the pixel is routed with valid depth (motion alpha 1, the routing the resolve already reads), its HDR
luma exceeds `E` (hull 0.16, strips 1.6-3.7 **[C]**: `E = 1.0`) and its 3x3 luma minimum is below `L / 3` (a local
peak, not a lit panel): 9 taps of the HDR scene through the unbound `s0`, `+~15 slots` on `line_mask_camera`'s 245
**[C]**; the resolve programs are untouched (the strength lands in `b` and follows the existing 11x11 grow, 17x17 speed
gate, camera gate and 7x7 box clip). Result: strips carry `min(n/(n+1), 0.97)` at rest and under camera pans, the
leak drops x 3.4 (8.1). Not for unrouted sentinel pixels (lasers, engine glows keep the sentinel law) and off at
`E = 0` (bit-identical mask). Specular glints on routed hulls qualify and gain 0.97 under pans; the 7x7 box clip
bounds their ghost, which the user has accepted for the lattice.

**Why all three.** Each addresses a failure the other two cannot: without R1 the yaw pop stays whatever `B` and `W`
are; without R2 the strips stay at `I * w / k` (the "disappeared" complaint); without R3 the rest flicker stays at
its k = 3 floor. R1 + R2 are one transform revision (they share `rG`, the constant lanes and the oracle) and R3 is an
independent file set (mask HLSL, `temporal_pass` option, launcher), so two implementers can run in parallel.

**Native Windows.** R1/R2: documented ps_3_0 opcodes (`dsx`, `dsy`, `texldd`, `dp2`/`dp3`, `rsq`, `rcp`, `mad_sat`),
one more fetch from the same declared sampler, constants the route already uploads; `GetLevelDesc` is a documented
texture method. R3: an owned FP16 target read by an owned program. Unverified natively, as before: the derivative
quad convention and anisotropy with explicit gradients (already on `platform-portability.md`'s list).

### 8.4 The alternatives and why they lose

- **(d) positive bias on the light-map stage only**: run227's global +0.5 narrowed horizontal 1-px runs 15-22 % and
  vertical not at all **[C]**, the anisotropy signature of a bias (minor axis only); a stage-only bias is a strict
  subset. Zero cost, no effect on the complaint. Rejected.
- **(e) 2-4 fine taps instead of a coarser mip**: four taps at +-0.25 px of the 0.71-px tent give an effective
  footprint of about 1.2-1.4 px (floor about 0.6-0.65, i.e. k about 1.4) for 4 fetches, where one `texldd` at k = 3
  gives 0.83; taps at the coarse level add nothing the mip chain has not already averaged. Superseded, as in §3.
- **Peak preservation from a fine fetch** (`g = clamp(F / C_k, 1, k)`): `min(F, I w)`, the torn strip with its peaks
  capped (8.2). Rejected on the law; the fixture's per-phase ripple would show it.
- **Uniform `x k_draw` on the gain (`c217.w`), CPU only, 1 h**: brightens panels by `k` in the ramp band and adds the
  brightness to the yaw pop. Kept only as the cheapest in-flight check that "disappeared" is the k dimming, if the
  ratification wants that answered before R2 is built.
- **(c) alone, no widening**: at rest it is exactly temporal supersampling (continuous, `I * w`, x 3.4 less leak),
  but under pans the 1-px history band's fractional resampling is untouched by W **[C]**; the tearing under motion stays.

### 8.5 Fixture before a flight, and the hours

Fixture: the existing `lightmapwiden` mode of `run_motion_output.py` (`seam-lightmap-widen-*`, synthetic 1024x1024
strip map, A8R8G8B8 and DXT1, 1/2/4-texel strips, 64-texel panel, rho sweep, 60/75 deg tilt, 6 px/frame drift over
8 phases) extended with: (i) R1: frames at `rho <= 1/c` hash-identical to the un-widened gained variant; `k` per rho
follows `clamp(c rho, 1, K)` read back through the strip width; the tilted case widens the major-axis strip;
(ii) R2: 1-texel strip peak at `rho >= 1` = `B x` the unboosted peak within 2 %, per-phase peak ratio unchanged from
k = 3 (0.867 **[C]**); panel interior within 1 FP16 code of the un-widened image and the edge rim <= 1.02 x interior
(the 1.35 gate against the 1.33 edge maximum); the 4-texel strip at rho 1.5 reports its over-brightening (expected
<= B); DXT1 copy: the gate's `t` distribution on the panel (any `t > 0` is a finding); `CreatePixelShader` accepts
all 100 programs with +27 slots. (iii) R3: `run_temporal_pass.py`'s thin-region case (`temporal_thin_region_inc.h`)
with a routed 0.5-px emissive strip at rest across 8 phases and under the camera path: post-resolve frame-to-frame
delta ratio 0.136 -> 0.040 within 0.02 of the IIR prediction at the fundamental, the mask's `b` = 1 on the strip and
0 on a 64-px lit panel and on the hull, and bit-identical masks at `E = 0`. Host: the transformer oracle
(`test_hull_lightmap_gain.py`) extended for the new block and slot totals, the coverage test's 100-row table, the
launcher tests for `K,B` and the vote option.

Hours, assuming the transform, its oracle and both fixture skeletons exist as built: R1 5 h (hook cache 1.5, block
1.5, oracle 1, fixture 1); R2 7 h (block 3, oracle and slot proof 2, fixture metrics 2); R3 5 h (HLSL 1.5, option and
launcher 1.5, fixture case 2). **17 h total, about 10 h on the transform path and 5-7 h on the TAA path in parallel**,
Wine queue time excluded. First flight, run225 station: `K = 3, B = 3` with the vote at `E = 1`, still and yawing,
`--taa-debug` bursts; A/B `B = 1` (energy-conserving, the run231 look with the yaw pop removed) and `E = 0`.

### 8.6 Unknown, and what settles it

- Whether the rest "flicker" the user reports is the ~4.6 % residual of 8.1 or its amplification by RCAS 0.75 and
  bloom: run231 carries `present_1` beside `taa_1` **[C]**; one triage of the bright-pixel delta on both settles it
  before R3 is built (if RCAS triples it, an RCAS exclusion on the emissive mask is the cheaper fix).
- The light-map UV density on real hulls (where `rho = 1` falls in metres, hence `c`, and whether every lit feature
  at 1-9 km is thinner than `1.3 k` px so that R2's over-brightening band is empty there): decode texture identity
  2353 and the UVs of frame 6516 draw 206 locally (numbers only), or fly `c` at two values.
- Whether the game's DDS light-map chains are normalised averages (R2's ratio gate assumes `C_2k >= C_k / 4` and
  panel `r = 1`): the same decode, per-level means (open since the widening note §6).
- Whether "almost disappeared" is entirely the `k` dimming or partly the frontal `k_draw` maximum: R1 + `B = 1`
  versus `B = 3` in the first flight separates them; the CPU-only `x k_draw` check answers it earlier if wanted.
