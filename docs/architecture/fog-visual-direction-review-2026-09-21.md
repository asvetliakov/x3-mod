# Volumetric fog: visual direction review (2026-09-21)

Owner note for the question raised after Run 60 session B (`/tmp/x3-bottleX3-run214`,
`screenshots/fog-smear.png`): the stored-density fog is technically sound but "something is
missing or wrong in the effect itself". Black smear near stations and distant-station flicker are
triaged separately as bugs and are not diagnosed here. Nothing in this note is implemented.

## Verdict

**The transport and storage design is fine and should stay. What is wrong is the medium and its
lighting model, and that is wrong in kind, not in tuning.** We built a numerically careful
single-scattering march through an *optically thin, single-hue, large-blob* medium lit by *one
term* (sun x HG x shadow). Every shipped space game that reads well does the opposite on each of
those axes: locally thick medium with voids, at least two colour terms, extinction that darkens
what is behind it, and something small near the camera that moves. Raising `--volumetric-fog`
strength will not fix it; it scales a flat veil.

Recommendation: keep the two-level stored field, the 24+40 march, the half-resolution
composite/repair and the cascade visibility; change (1) the density remap so the same stored
noise yields thick cores and true voids, (2) the lighting law (dual-lobe phase, sky ambient,
Beer-powder cheap self-shadow, separate albedo and extinction tints), (3) add sample jitter under
TAA, and (4) add a near-camera dust layer outside the march. All of 1-3 are edits inside
`march_depth` plus constants; none changes the atlas format, cache or upload path.

## 1. What shipped games do, and why it reads

| Title / technique | What it does | Why it reads |
| --- | --- | --- |
| Freelancer, X3 itself, Everspace 1 | Card/billboard puffs + per-zone distance fog colour + fogged skybox | Distance fog hides the backdrop and silhouettes objects; cards near the camera stream past. Cheap, but depth-cued. |
| Elite Dangerous | Nebulae are baked into the skybox from galaxy position; inside, screen-space tint + lightning flashes + dust motes streaming past the canopy | Almost no real volume. The *speed cue comes entirely from motes*, the mood from a colour-graded backdrop and local flashes. |
| EVE Online (2020+ volumetric clouds, gas sites) | Bounded cloud volumes, raymarched at reduced resolution, strongly authored colour ramps, emissive cores, lightning | Finite shapes with edges: you see a cloud *against* space. Two or three hues per cloud (core, rim, shadow side). |
| Star Citizen gas clouds | SDF/voxel-authored density, raymarch with temporal reprojection, deep-shadow-style light volume, local lights injected, ships silhouette and vanish | Thick medium: visibility drops to hundreds of metres; self-shadow gives cauliflower relief; ship lights bloom into the gas. |
| Everspace 2, Homeworld 3 / Deserts of Kharak | Art-directed cards and meshes plus analytic height/exponential fog and heavy grading; HW3 adds froxel lights in megalith scenes | Silhouette layering: far objects fade to fog colour in steps; fog colour differs from sun colour. |
| No Man's Sky | Analytic fog with sun-direction colour ramp (fog colour lerped toward sun colour by a phase-like term), planetary clouds Nubis-style | The "fog colour toward sun, other colour away" trick gives directionality for free. |
| X4 Foundations | Region-defined volumetric fog: bounded regions with density textures, raymarched half-res, lit by sun + ambient, plus the old card "fog wisps" and dust kept for near motion | Egosoft's own answer keeps near-field particles *in addition to* the volume. Known complaint: uniform regions look like a flat tint - the same failure we have. |
| Froxel volumetrics (Wronski 2014, Hillaire 2015) | View-aligned 3D grid of scatter/extinction, lights injected per froxel, temporally filtered, integrated front to back | Local lights and shadows inside fog; temporal jitter hides low sample counts. Needs 3D render targets or compute: not SM3. |
| Nubis / Horizon clouds (Schneider) | Low-freq shape noise remapped by coverage, eroded at edges by high-freq Worley; Beer + "powder" term; dual-lobe HG; few-sample light march toward sun; ambient by height | The canon for making noise look like *cloud* rather than noise: remap creates hard-ish edges and voids, erosion creates wisps, powder darkens sun-facing thin edges so cores look lit from inside. |
| Multiple scattering approximations (Wrenninge octaves; Hillaire 2016/2020) | Sum 2-3 octaves of single scatter with attenuated extinction and reduced anisotropy | Thick cores glow instead of going black; fixes the "dirty smoke" look of pure single scatter. |
| Blue-noise jitter + temporal reprojection | Offset ray start per pixel per frame; TAA/own history integrates | 16-32 samples look like 128+. Converts banding to fine noise TAA removes. |
| Depth-aware half-res upsample | Bilateral/nearest-depth | We already have this (composite + repair). |
| Dust motes / debris particles | A few hundred camera-local wrapped points or streaks | The single strongest sense-of-speed and scale cue in every title above. |
| Local light scattering | Engine glow, station lights, explosions add radiance to nearby medium | Ties objects into the medium; without it objects look pasted on top. |
| Albedo tint vs absorption tint | Scatter colour and extinction colour chosen separately (blue scatter, warm-removing extinction, or the reverse) | Objects seen through fog shift hue as well as brightness, which is what "blended" means perceptually. |

Common factor: **contrast at three scales** (kilometre silhouettes against voids, hundred-metre
wisps with relief, metre-scale motes) and **at least two colours** (lit side/sun-ward vs shadow
side/ambient). Accuracy of the integral is never what sells it.

## 2. Diagnosis of our effect

Read from `src/fog/fog_density_field_inc.h`, `src/fog/fog_density_generator.cpp`,
`src/renderer/fog_family_chroma_inc.h`, `src/renderer/fog_pass_math.h`, `fog_pass.cpp`, the
Run 214 session log and the screenshot. Scale: 12000 units = 2.4 km, so 5 units = 1 m.

1. **The medium is optically thin everywhere, so there is no extinction look and no shafts.**
   Session log: `sigma=2.5e-06`, `density_scale=1.5` (strength 0.03), rho <= 1. Peak optical depth
   through a 13 km core is about 3.75e-6 x 0.75 x 65000 = 0.18; the ledger's measured sky-ray
   mean opacity for this field family is 0.16-0.25 and earlier gates *capped* p99 opacity at
   0.05-0.14. With T >= 0.8 the composite `scene*T + S` is visually a pure additive veil. Shafts
   are compiled in and the cascades were bound (`shadow_maps=3`), but a station's shadow volume a
   few km long removes at most tau ~ 0.04 of inscatter from a veil that is itself a few percent
   of scene luminance: a 1-2 % luminance change, below visibility. **Shafts are invisible by
   construction, not by bug.** Visible shafts need locally thick medium around occluders.
2. **Density has no small or sharp structure.** `combine()` is four value-noise octaves with
   feature sizes 65536 / 32768 / 8192 / 2048 units = 13 km / 6.5 km / 1.6 km / 410 m, quintic
   interpolated, 8-point prefiltered, then trilinear in a 512-unit (102 m) grid. Value noise with
   quintic fade is the blobbiest noise there is; the remap is a wide smoothstep(.10,.40) and the
   detail term only modulates by 0.5-1.0 and subtracts up to 0.2 in the void side. Result: soft
   km-scale mounds whose line integrals over 40 km average toward a constant. There are no
   edges, no wisps, no cores. In the screenshot the fog reads as a uniform blue wash over the
   whole frame.
3. **One lighting term, one hue.** `S = lit x HG(g=0.3) x E_sun/pi x chroma`. No ambient/sky
   term, no self-shadow, no multiple-scatter lift. Family chroma is max-normalised and very
   saturated (bluewell 0.05, 0.27, 1.0; khaak 0.09, 0, 1.0), so inscatter is a single pure hue at
   every pixel; the only variation is scalar brightness. In bluewell the backdrop is also blue, so
   the veil is indistinguishable from a skybox tint. HG g=0.3 gives a forward/back ratio of 6.4
   but a lobe so broad that there is no sun-ward glow. The sky hue reduction shaders
   (`fog_sky_level0_ps.hlsl`, `fog_sky_reduce_ps.hlsl`) exist but are referenced by no `.cpp`:
   an ambient source was designed and is not wired.
4. **Extinction is grey.** `T` is scalar, so objects behind fog only dim slightly; they never
   shift toward the fog hue by losing their own colour. That is the missing "blended" feel
   together with point 1.
5. **No scale or motion cue.** Smallest density feature is 410 m and the fine grid is 102 m; a
   fighter is 20-30 m and flies 80-200 m/s. Nothing in the fog changes on a timescale the pilot
   perceives. Replacing the cards removed the only near-field parallax the vanilla game had.
6. **Deterministic mid-bin sampling.** `s = start + 0.5*ds`, no per-pixel or per-frame offset;
   c0 is de-jittered on purpose. Far bins are up to 4.7 km wide against 4096-unit (819 m) far
   cells, and the bin layout changes with each pixel's depth, so density edges would band and
   depth discontinuities change the whole sample set behind a silhouette. At today's thin
   density this is hidden; it will show as soon as density is raised (and is a plausible
   contributor to the station-adjacent artefacts - unverified, left to the triage).
7. **Fog is laid over the skybox at full 40 km column.** Sky pixels take `distance = horizon`.
   The authored nebula backdrop is therefore tinted by the same hue everywhere, flattening the
   one element of the frame that had painterly contrast. Shipped games either fog the backdrop
   deliberately to a *different* colour or leave it alone and let finite clouds occlude it.
8. **Exposure/bloom.** The veil adds a low, spatially flat radiance. Under auto-exposure it
   raises the metered average and pulls the whole frame down slightly while never crossing the
   bloom threshold itself, so it costs contrast and gives no glow back. Inference from the
   pipeline order (fog before TAA and before HDR resolve); not measured.
9. **Abrupt limits.** Not a problem: LOD blend 20-30k units and taper 150-200k units are smooth.
   The 90-frame ramp-in is fine.

## 3. Ranked changes

GPU costs are estimates for the half-resolution march at 1440p (about 0.92 M marched pixels x 64
bins) on Apple silicon through D3DMetal-class translation; none is measured. "Fits" means no
change to atlas format, cache, uploads or the composite/repair law.

| # | Change | Visual payoff | GPU cost | Risk here | Fits stored design |
| --- | --- | --- | --- | --- | --- |
| 1 | **Runtime density remap + higher sigma**: `rho' = saturate((rho - c)/(1-c))^p` with coverage c ~ 0.35-0.5, p ~ 1.5-2, and sigma raised 5-10x so cores reach tau 1-2 over a few km while 50-60 % of space becomes exactly empty | Largest single win: real voids, cloud edges, silhouettes that vanish into cores, and the precondition for visible shafts | ~3 ALU per sample; empty samples already skip shadow reads, so more voids makes the march *cheaper* | Low. Two constants. Breaks the opacity-ceiling gates on purpose; those were appearance choices, now reversed by the user's verdict | Yes |
| 2 | **Dual-lobe HG + sun-colour ramp**: `0.7*HG(0.75) + 0.3*HG(-0.15)`; per pixel, not per sample | Bright glow in the sun's half of the sky, cool dim fog away from it; gives the frame a light direction | ~10 ALU per pixel | Low; `fog_phase_constants` grows by one vec4 | Yes |
| 3 | **Ambient/sky term with its own colour**: `S += albedo * ambient * (1-T)` where ambient is the wired-up 1x1 sky mean, or initially a per-family constant of *different hue* from the sun term (e.g. bluewell: sun-lit pale cyan-white, ambient deep blue) | Two-colour fog: shadowed and anti-sun regions are coloured, not black; shafts become "bright on coloured", which is how they read in every reference | 1 fetch per pixel (or none) | Low for the constant; medium for wiring the orphaned sky reduction (2 small passes, Reset handling) | Yes |
| 4 | **Desaturate scatter, tint extinction**: scatter albedo = lerp(chroma, white, ~0.5); `T` becomes RGB with extinction tint = complement-weighted (blue fog removes red/green faster). Composite already carries rgb+a; needs T.rgb, so either a second target or pack luminance-T plus derive tint analytically `T_rgb = pow(T, k_rgb)` | Objects sink into the fog hue with distance ("blended"); highlights in thick fog go pale instead of neon | `pow(T,k)` in composite: ~6 ALU per full-res pixel | Low with the analytic pow form (exact for a homogeneous tint ratio); no new target | Yes |
| 5 | **Beer-powder + cheap self-shadow**: powder `1 - exp(-2*tau_local)`; self-shadow from 2 extra density taps toward the sun at +1 and +3 fine cells (far level only beyond 6 km) | Relief: sun-facing faces bright, far sides dark, cores look lit from within. This is what turns blobs into clouds | Powder free. 2 taps = 4 atlas fetches per non-empty sample, roughly +60-80 % of march texture cost in dense regions, nothing in voids | Medium: SM3 instruction/fetch budget inside the 64-iteration loop; check the compiled program fits and FXC does not unroll | Yes |
| 6 | **Multiple-scatter lift**: second octave `0.5 * S(sigma*0.5, g*0.5)` approximated per sample as `a2 = 1-exp(-0.5*sigma*rho*ds)` with isotropic phase | Thick cores glow instead of going muddy once #1 lands | 1 exp per sample | Low | Yes |
| 7 | **Per-pixel, per-frame sample offset**: interleaved-gradient or 4x4 Bayer noise, scaled by `ds`, frame index rotated with the TAA sequence | Removes banding that #1 will expose; makes 64 bins look like several hundred through TAA | ~6 ALU per pixel | Medium: fog is composited *before* TAA, which is what we want, but the project has a shimmer history - gate on the existing TAA shimmer fixture, and clamp jitter to the near 24 bins first | Yes |
| 8 | **Near-camera dust/mote layer** (not in the march): ~512-2048 camera-wrapped points in a 400 m cube, instanced quads or point sprites, velocity-stretched by camera motion, lit by the same phase/ambient, faded by local fine-level rho and by depth (soft) | The missing sense of speed and scale; present in ED, X4, Everspace, SC, vanilla X3 (cards) | One draw, <= 2k small additive quads: negligible fill | Medium: new draw in the scene-end bracket, needs state save/restore and motion-vector policy (write none; treat as transparent). SM3 has no instancing requirement - a static VB with shader-side wrap works on native D3D9 | Separate layer; complements |
| 9 | **Do not fog the skybox at full column**: cap sky-ray inscatter weight (e.g. sky pixels use taper end 60-80k) or blend sky S by 0.5 | Backdrop keeps its painted contrast; finite clouds read against it | None (constant) | Low | Yes |
| 10 | **Local light inscatter** (engine glow, station beacons): analytic point-light-in-homogeneous-medium integral (closed form, Sun et al. 2005) for the N brightest lights using rho at the light | Objects tied into the medium; strong at stations | ~25 ALU per light per pixel; 4 lights fine | High: needs a reliable list of light positions/colours from engine structures - a reverse-engineering task first | Additive to design |
| 11 | **Erosion octave at runtime**: one tiling 32^3 Worley-like volume (2D atlas) sampled at ~40-80 m scale, subtracting at cloud edges within 3 km | Wispy edges near the camera | +2 fetches per near sample | Medium; adds an asset and the erosion "swims" unless anchored (it is world-anchored, fine) | Extends field |
| 12 | Froxel grid / light volume | Proper local lights and cheap self-shadow | 3D targets unavailable in D3D9; emulation with 2D atlases and many passes | High | Replaces march; **not recommended** |

Order of work: 1, 2, 3(constant form), 4, 6, 9 are one shader edit and a constants block - do
them together. Then 7 and 5 with fixtures. Then 8. 10-11 later; 12 never on this API.

### Native Windows behaviour

All of 1-9 are HLSL ps_3_0 arithmetic, `tex2Dlod` on RGBA16F/R32F and one extra ordinary draw:
documented D3D9 only, no Wine-specific capability. The one portability check is the ps_3_0
limits after #5 (512 instruction slots is not the constraint in SM3; dynamic-flow depth and the
FXC loop handling are) - the existing `fog-density-march-program.json` slot audit covers it.
Native behaviour remains cross-compiled and fixture-verified only, not flown.

### Hot-path cost

CPU per frame is unchanged for 1-7, 9 (a few more floats in the existing 25-vector upload; 8 adds
one draw and its state bracket, comparable to one game draw; the screenshot overlay shows 212 draws). GPU: #1
lowers cost by emptying samples; #5 is the only material increase and is confined to non-empty
samples. No allocation, locking or per-draw work is added.

## 4. Next flyable experiment

One build, stored mode, with a **look preset** cycled by a new hotkey (suggest Ctrl+Alt+F11) and
shown in the overlay next to `FOG`, so the user can A/B in the same place without restarting:

- **L0** current law (control).
- **L1** "shaped": remap (c=0.4, p=1.5), sigma x6, dual-lobe phase, constant two-colour ambient,
  desaturated albedo + tinted extinction, multi-scatter lift, sky column cap.
- **L2** L1 + Beer-powder + 2-tap self-shadow.
- **L3** L2 + per-frame sample jitter.

Dust motes are a separate toggle if they make the build; otherwise the following build.
Ctrl+Alt+F10 strength ladder stays, so each look can be tried thin and thick.

What the user should look for, in a bluewell or green sector with a station and the sun in view:
1. Flying toward the sun vs away from it: L1+ should show a clear bright side and a darker,
   *differently coloured* side. L0 looks the same both ways.
2. Are there now clear gaps where the skybox shows untouched, and cloud bodies with edges?
3. Fly so a station sits between you and the sun inside a cloud body: L1+ should show shadow
   shafts behind it; L2 should additionally show the cloud's own dark far side.
4. A distant ship or station entering a core should fade toward the fog colour and disappear,
   not just dim.
5. L3 vs L2 while strafing: banding/steps in cloud edges gone, and no new crawl or boiling at
   rest (shimmer history).
6. Frame time in the overlay at each preset.

Capture request: one F-key capture burst per preset from the same pose.

## Unknowns and what settles them

- **Actual per-pixel opacity in Run 214** was inferred from sigma and ledger statistics, not
  read back. A fixture readback of the half-res S/T target (mean and p99 of 1-T) settles it.
- **ps_3_0 fit and GPU time of L2** (self-shadow taps in a 64-bin loop): compile through the
  existing program audit and time with the fog pass's existing timing mode (`timing=` in `volumetric_fog_mode`) in flight; diagnostic timings
  are not game FPS.
- **Jitter vs TAA shimmer**: the existing TAA temporal fixture on a static pose with L3.
- **Whether the black smear/flicker share a cause with deterministic bins at depth edges**
  (diagnosis point 6): the running triage owns this; if confirmed, #7 moves up.
- **Light list for #10**: needs disassembly of the engine's per-frame light set beyond the sun
  node already polled (`sun_light_poll.h`); not started.
- **Ambient from the sky mean**: `fog_sky_level0/reduce` are compiled-source only and unwired;
  whether the 1x1 history is stable under card replacement needs a fixture before it replaces
  the per-family constant.
