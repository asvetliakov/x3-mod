# Star and nebula rendering in space games

Research date: 2026-11-04. This note is a sourced survey for the X3 background
phase (painted star body plus painted nebula body per sector, drawn as a few
hundred SM1/SM2 draws into the FP16 HDR target; nebula fog cards already
replaced by volumetric fog on AP; 512 stardust streaks, planets and suns on
top). It collects evidence for a later design decision between a runtime
procedural star pass and an offline re-authoring of the painted nebula
textures. It does not make that decision.

Source quality is marked on every claim: **official** (developer or
first-party publication), **academic** (peer-reviewed or engine documentation
with stated method), **press** (journalist interview or coverage of a developer
post) and **community** (modders, forum users, fan wikis). Several primary pages
were unreachable from this host (Nexus Mods, ArtStation, Frontier forums, the
Valve developer wiki, Unity and Gaia Sky docs returned 402/403/DNS failures);
where only a search-engine summary of such a page was available this is
written as "search summary". Figures are marked measured (taken from the source
or from this repository) or inferred (derived here).

## 1. How the surveyed games render stars and nebulae

| Game | Stars | Nebulae / background | Source and quality |
| --- | --- | --- | --- |
| **Elite Dangerous** | Data-driven: about 160,000 catalogue stars plus Stellar Forge procedural stars; Braben: "Our night sky isn't drawn by an artist. It comes from raw data." Extra dust was added beyond measured values because the raw sky "looked very uniform and bright". Whether the sky is a per-system baked cubemap or live point rendering is **not documented** by Frontier; community sources describe a background generated from the data at jump time, with the galaxy background texture scaling 1024/2048/4096 with quality settings. | **Not documented** by Frontier. Community sources say nebulae are part of the generated background and that a config override can force higher-detail nebula rendering at the cost of stutter in witchspace. | [PCGamesN interview with David Braben, 2017](https://pcgamesn.com/elite-dangerous/every-single-star-in-our-night-sky-is-in-elite-dangerous-david-braben-on-re-creating-a-galaxy-all-over-again) (press); [Elite fan wiki Graphics Mods](https://elite-dangerous.fandom.com/wiki/Graphics_Mods) and [GameFAQs thread](https://gamefaqs.gamespot.com/boards/202752-elite-dangerous/75506318?page=1) (community, search summary) |
| **Star Citizen** | **Not documented.** The February 2021 monthly report says only that "the skybox and star-field for the Pyro system were further developed" (Art, Environment). | Authored skybox per system plus local volumetric gas clouds: the May 2021 report describes finalising "the look of the jump point gas cloud" and streaming support for gas clouds; the November and December 2025 report adds texture streaming for volumetric cloud and ground fog "including support for streaming 3D volume textures"; the Star Engine feature list names "Space Fog (Gas Clouds)". No source describes the skybox's construction. | [Monthly Report February 2021](https://robertsspaceindustries.com/en/comm-link/transmission/18018-Star-Citizen-Monthly-Report-February-2021) (official); [Monthly Report May 2021](https://robertsspaceindustries.com/en/comm-link/transmission/18167-Star-Citizen-Monthly-Report-May-2021) and [November and December 2025](https://starcitizen.tools/Comm-Link:Star_Citizen_Monthly_Report_-_November_&_December_2025) (official, search summaries); [Star Engine wiki](https://starcitizen.tools/Star_Engine) (community) |
| **No Man's Sky** | Hello Games' Prisms notes say "the variety, quality and number of stars in the sky has been significantly increased" but give no technique. A modder's changelog describes the stock sky as a star-field cube whose faces carry "coloured noise stars and glowing sprite stars", which the mod replaces with point stars. | Community only: nebulae are colour settings on the space sky (`SPACESKYCOLOURS.MBIN`) with background imagery in some systems; the Milky Way is a fixed texture that does not follow galactic position. **Not documented** officially. | [Prisms update](https://www.nomanssky.com/prisms-update/) (official); [Stellarum mod](https://www.nexusmods.com/nomanssky/mods/4556) and [Black Space mod](https://www.nexusmods.com/nomanssky/mods/1471) (community, search summary) |
| **X4: Foundations** | **Not documented.** | Pre-rendered from 3D fractal point clouds: Egosoft's Art Director Lino says early backgrounds were handmade (ink dropped in water and photographed), then the team adopted the IFS fractal renderer, where the fractal is "a three-dimensional thing made from point clouds", the artist can "fly around inside this world", "find a nice camera position and render it out". A developer comment relayed by a fan wiki says the Windfall sector's denser volumetric fog was dropped for performance. Modders replace the painted nebula textures with upscaled 8192x4096 versions. | [Interview with Lino, Egosoft news](https://www.egosoft.com/news/current_en.php) (official); [Windfall fan wiki](https://x4-foundations-wiki.fandom.com/wiki/Windfall) and [DeadAir's QHD Skybox Remaster](https://www.nexusmods.com/x4foundations/mods/888) (community, search summary) |
| **EVE Online** | **Not documented.** | Offline-rendered per-region backdrops: CCP t0rfifrans (2011-11-03) says the 68 unique backdrops are "inspired by real world imagery but they are all computer generated, painstakingly rendered using state of the art hardware and software", one per region, with ship surfaces reflecting the regional nebula. Engadget's reviewer adds that the nebulae are "real three-dimensional objects" whose view "is redrawn for each system and stored in the skybox". Fanfest 2022 announced higher-resolution nebulae; Houdini is credited for at least one system's skybox effect. | [Introducing New Nebulae into EVE](https://www.eveonline.com/news/view/introducing-new-nebulae-into-eve) (official); [EVE Evolved: Touring a galaxy reborn](https://www.engadget.com/2011-12-11-eve-evolved-touring-a-galaxy-reborn.html) (press); [Fanfest is here](https://www.eveonline.com/news/view/fanfest-is-here) and [André Bragason, ArtStation](https://www.artstation.com/artwork/182oW8) (official / community, search summary) |
| **EVERSPACE 2** | **Not documented** for EVERSPACE 2. For the first EVERSPACE a Kickstarter update describes a "skybox generator" in which sun size and overall brightness depend on distance to the star, that is a generated rather than single painted sky. | **Not documented.** The lead environment artist's credits list atmospherics and VFX for the Khaït Nebula; no construction detail is published. The UE5 tech blog covers GI, not backgrounds. | [EVERSPACE Kickstarter update](https://www.kickstarter.com/projects/rockfishgames/everspace/posts/1370594) (official, search summary); [Marco Unger, ArtStation](https://www.artstation.com/artwork/NyNeXN) (community, search summary) |
| **Homeworld 3** | **Not documented.** Precedent: Homeworld 2 backgrounds are vertex-coloured sphere meshes generated from a painting (vertex density follows contrast, poles painted as smooth gradients), with "the stars [as] single textures/billboards" on top. | Ray-marched generated volumes: the June 2023 development update (Technical Art Director Demetrius Apostolopoulos, Art Director Karl Gryc, Audio Director Dave Renn) says the nebulae are dynamically generated gas clouds from a dedicated system with density, movement and colour controls; the team started from Unreal's ray-marched cloud system, found it too limited, and tuned toward NASA imagery; ray marching samples the gas volume at several depths and density hides ships from sensors. Backgrounds behind the nebulae are not described. | [GamingTrend coverage of the dev diary](https://gamingtrend.com/news/massive-homeworld-3-dev-diary-showcases-huge-audio-and-visual-upgrades/), [Worthplaying](https://worthplaying.com/article/2023/6/30/news/138095-homeworld-3-reveals-details-about-visuals-audio-design-pushing-the-unreal-engine-4-and-more-screens-trailer/) (press, search summary; the [Steam post](https://store.steampowered.com/news/app/1840080/view/3665415870863542835) is the primary); [Homeworld 2 backgrounds, simonschreibt.de](https://simonschreibt.de/gat/homeworld-2-backgrounds/) (community analysis) |
| **Starfield** | **Not documented** by Bethesda. Modders report that "the pinpoint stars in the game are separate from the Milky Way skybox" and that INI values (`fStarfieldBackgroundScale`, `fStarIntensity`) scale the background and the local star glow. | Single replaceable Milky Way skybox texture (replacers ship 4k to 12k versions); per-system skyboxes are a modder wish, not a feature. **Not documented** officially. | [Enhanced Milky Way](https://www.nexusmods.com/starfield/mods/7846), [Majestic Milky Way Replacer](https://www.nexusmods.com/starfield/mods/4152?tab=posts), [Shadowstar's 12k Milky Way](https://www.nexusmods.com/starfield/mods/17048) (community, search summary) |
| **Freelancer** (2003) | Painted starspheres: the system INI `[Background]` block names three CMP models, `basic_stars`, `complex_stars` (drawn on top) and `nebulae` (drawn behind); `space_color` fills behind them. Modders describe starspheres as ordinary models whose star and nebula layers are indexed TGA MIP chains starting at 256x256, with the nebula images on flat planes. | Painted starsphere layer for distant nebulae; local nebula fields are INI zones with `[Fog]` near/distance falloff and exclusion zones (interior cloud puffs exist but the wiki page was unreachable). | [Starport wiki, system INI](https://the-starport.com/wiki/ini-editing/typed-inis/system) (community); [Starsphere Journey thread](https://the-starport.com/forums/topic/3005/starsphere-journey-documentation-of-the-journey) (community); nebula zone keys from [Starport exclusion-zone threads](https://the-starport.com/forums/topic/4964/exclusion-zones/1?topic_id=5105&lang=en-US) (community, search summary) |

Two reference renderers outside the list document what the games do not:
SpaceEngine's developer states that "real nebulae are still use old technology
(sprites)" while "raymarching ... is used for procedural ones" (official blog,
17 February 2019, [source](https://spaceengine.org/news/blog190217/)), and
Stellarium's sky drawer computes a point source's radius and luminance from
magnitude with twinkle only as an option tied to atmosphere
(`flagForcedTwinkle` is "an option for planetariums"; official docs,
[source](https://stellarium.org/doc/0.17/classStelSkyDrawer.html)), which
supports the premise that twinkling in space is a stylisation.

Pattern across the table: no surveyed game documents live catalogue or
procedural point rendering of stars as the shipping technique except Elite's
data-driven sky, and none documents nebula detail generated at runtime from a
painting. The documented nebula pipelines are offline 3D renders (EVE, X4),
painted layers (Freelancer, Homeworld 2 vertex colours, Starfield's single
texture) or fully volumetric ray marching (Homeworld 3, and Star Citizen's
local gas clouds on top of an authored skybox).

## 2. Keeping sub-pixel stars stable under TAA and jitter

The only peer-reviewed treatment found is Schneegans, Kreskowski and Gerndt,
"Smaller than Pixels: Rendering Millions of Stars in Real-Time", Eurographics
2025 short paper (academic; implemented in CosmoScout VR,
[project page](https://www.uni-weimar.de/de/medien/professuren/medieninformatik/vr/research/real-time-rendering/smaller-than-pixels-rendering-millions-of-stars-in-real-tim/),
[PDF](https://www.uni-weimar.de/fileadmin/user/fak/medien/professuren/Virtual_Reality/documents/publications/2025_Smaller_than_Pixels_Rendering_Millions_of_Stars_in_Real-Time.pdf)).
Its findings, with the paper's own wording:

- **Single-pixel points alias.** "Single-pixel stars promise a good performance
  but require a glare filter and produce artefacts when jumping between pixel
  positions. Small anti-aliased camera-aligned discs provide better visual
  quality but are more expensive to render."
- **Minimum disc footprint is a tuning knob with a flicker floor.** For discs
  "the solid angle A of the disc is a configurable parameter. Choosing the right
  size is difficult as smaller stars are more likely to create flickering but
  increase the drawing performance."
- **Energy-conserving splats.** Luminance is derived from the solid angle of the
  proxy: S = m_v + 2.5 log10(A) with A in arcsec², L = 10.8e4 × 10^(−0.4 S)
  cd/m². For point stars the per-pixel solid angle is used, so edge pixels get
  brighter; for discs the solid angle is constant, "hence, points and discs
  will contribute the same luminance to the render target regardless of their
  screen-space position."
- **Pre-filtered point rendering.** Their software rasterizer "distributes
  [the luminance] over a 2x2 pixel area, weighted by the distance of the star
  to the pixel centers. This gives a smooth transition when a star moves across
  the screen." They avoid hardware smooth points because "the implementation is
  vendor specific, so we cannot know how much luminance a smooth point will
  actually contribute to the render target."
- **Glare in post, not in the proxy.** Rendering stars "as small as possible
  and add[ing] a glare effect in a post-processing step scales best"; the glare
  is a sum of Gaussians with doubling sigma and halving weight, approximating
  the 1/θ² veiling-luminance law within 1 % for n = 10 (measured in the paper).
  Timings (measured, RTX 4070 Super): about 8 ms for 50 M catalogue stars at
  4K; screen-space glare 0.34 ms at 1080p and 0.74 ms at 4K.

The paper does not use TAA. The TAA-side evidence comes from engine
documentation and production code:

- **Luminance-weighted history blending.** Blender's EEVEE TAA cites slide 20
  of Karis, "High Quality Temporal Supersampling" (SIGGRAPH 2014) for a weight
  of the form 1/(bias + luma), "to reduce the flickering introduced by high
  energy pixels", and slides 43/45 for biasing toward history near the clamp
  box, "reduces flicker" ([Blender pull request](https://projects.blender.org/blender/blender/pulls/120867/files),
  code comment citing Karis; academic-grade secondary). Playdead's INSIDE TAA
  credits Lottes for "weighing by unbiased luminance diff" and Karis for YCoCg
  clipping ([repository](https://github.com/playdeadgames/temporal), official).
- **Flicker detection on luminance history.** Unreal's TSR documentation
  (official) says "the temporal analysis of flickering is only monitoring the
  luminance of pixels", exposes `r.TSR.ShadingRejection.Flickering` with a
  period in frames, and warns that TSR "can't solve the problem of transforming
  details that are 1 pixel thick that move only half a pixel in the frame";
  higher `r.TSR.History.SampleCount` gives "more stability on highlights on
  static images" at the cost of ghosting on "fireflies"
  ([source](https://dev.epicgames.com/documentation/unreal-engine/temporal-super-resolution-in-unreal-engine)).
  Unigine's TAA has a "fix flicker" option that uses the previous frame's
  brightness to remove bright pixels and is recommended for thin bright lines;
  its "preserve details" option is documented to add flicker
  ([docs](https://developer.unigine.com/en/docs/2.21/principles/render/antialiasing/taa), official, search summary).
- **History exclusion.** Unreal's spatial anti-aliaser ("responsive AA" in the
  older TAA) exists for pixels where history cannot be trusted; the TSR page
  states it "cannot create information it doesn't have" (official). No source
  documents excluding a star layer from TAA history specifically.
- **Pre-blurred star textures.** The community fix for flickering painted star
  textures is to blur the star texture and add it back additively, or to
  enlarge the stars ([Unity Answers](http://answers.unity3d.com/questions/1009567/star-skybox-and-rotating-camera.html),
  [GameDev.net](https://gamedev.net/forums/topic/485982-texture-flickering/4176513/), community).

Gaia Sky's documentation (official, search summary) adds the practical choice
between billboard quads whose size follows solid angle and brightness power,
and native points that "ignore perspective", with angle thresholds that switch
between the two
([docs](https://gaia.ari.uni-heidelberg.de/gaiasky/docs/3.7.3/Star-rendering.html)).

Summary for X3 (inferred from the above): the documented stable combination is
a disc or quad with a minimum footprint of about 2 px, luminance set by solid
angle so brightness does not depend on footprint or screen position, glare
added by the existing bloom chain rather than by enlarging the proxy, and
luminance-weighted (or history-biased) blending in TAA for the bright pixels.

## 3. Offline enhancement of painted low-resolution DXT1 sky textures

**Banding and hue drift from the format.** Eight-bit channels give 256 levels
per channel, which band over wide gradients; DXT1 endpoints are RGB565, so green
has 6 bits and red and blue 5, producing greenish or reddish shifts; the listed
fixes are dithering ("to ease the transition between two colors, without adding
any new colors"), keeping the special texture uncompressed, or storing colour
in geometry, which is how Homeworld's backgrounds and X Rebirth's lens flares
avoid it ([simonschreibt.de, colour banding](https://simonschreibt.de/gat/colorbanding/),
community-expert). The Homeworld 2 article also records that output colours
came out brighter than the painting and had to be compensated with a levels
adjustment, and that the left and right edges must match to avoid a seam
(community analysis).

**Debanding and dequantisation.** Tu, Lin, Wang, Adsumilli and Bovik,
"Adaptive Debanding Filter", IEEE Signal Processing Letters 2020, apply
"content-adaptive smoothing filtering followed by dithered quantization" as a
post-processing module that smooths banded regions while preserving edges
([arXiv 2009.10804](https://arxiv.org/abs/2009.10804), academic). Byun, Shim
and Kim, "BitNet: Learning-Based Bit-Depth Expansion", ACCV 2018, train a CNN
that can "effectively remove false contours and restore visual details at the
same time" ([arXiv 1910.04397](https://arxiv.org/abs/1910.04397), academic).
Neither is sky-specific; both treat the quantised painting as the signal and
reconstruct a higher-bit-depth plane before re-quantising with dither.

**DXT artefact restoration.** Valve's developer wiki documents an AI model
trained on original-versus-DXT pairs that "removes most of the blocky
artifacts, although fine-grain detail on more realistic textures will be
smoothed out", and warns that correctly decoded output "will result in a minor
hue shift" and that older decoders introduced a green tint
([Restoring Texture After DXT Compression](https://developer.valvesoftware.com/wiki/Restoring_Texture_After_DXT_Compression),
official wiki, search summary).

**Upscaling filters and learned super-resolution.** The ESRGAN author states
"ESRGAN actually has no ability to denoise", that the pretrained model is
"trained for bicubic downsampling kernel" and that inputs with other kernels or
no true high-resolution counterpart "may have noisy artifacts"
([BasicSR issue 69](https://github.com/xinntao/BasicSR/issues/69), author).
Texture-modding guides report model-specific colour shifts ("causes color
shift in the reds"), tint from DXT-decompression models, lines from noise,
over-sharpening and "smoothing / blurring / light to medium loss of detail"
from decompression passes ([STEP texture upscaling notes](https://stepmodifications.org/wiki/User:TechAngel85/Sandbox3),
community). A comparison of texture super-resolution found the best ESRGAN
results "not substantially better than bilinear scaling in realtime" with
"significant visual artifacting" in the worst case
([GDCorner](https://www.gdcorner.com/rainbowsix/gamedev/superresolution/rendering/2019/01/04/SuperResolutionTesting.html),
community, search summary), and a texture-generation paper notes Real-ESRGAN
degradations yield "over-smoothed textures, over-sharpened edges, and ringing"
([Meta 3D TextureGen](https://arxiv.org/pdf/2407.02430), academic, search summary).

**Guided detail synthesis.** The standard halo-free base/detail split is the
guided filter: an edge-preserving filter whose linear model keeps the output
gradient consistent with the guide and so avoids the gradient-reversal (halo)
artefacts of the bilateral filter (He, Sun, Tang, "Guided Image Filtering",
TPAMI 35(6) 2013, [summary](https://en.wikipedia.org/wiki/Guided_filter),
academic). Synthesising nebula detail as noise modulated by the painting's
luminance and gradient is the same idea applied with a procedural detail layer;
no published game pipeline doing this from a painted base was found.

**Documented nebula art pipelines.** None of X4, EVE or Homeworld generates
detail from a painting. X4 renders 3D IFS fractal point clouds from a chosen
camera (official, section 1); EVE renders 3D nebulae into per-system skyboxes
(official plus press); Homeworld 3 ray-marches generated volumes (press
coverage of the official update); Homeworld 2 converted a painting to vertex
colours, which smooths rather than adds detail (community analysis); SpaceEngine
keeps sprites for real nebulae and ray-marches only procedural ones (official).
Answer to the question as asked: **no documented pipeline found**.

**Failure modes collected above.** Noise look (ESRGAN on compressed or
mismatched-kernel input; aggressive artefact-removal models produce "too much
dithering (noise) to be useful"); haloing and gradient reversal (bilateral or
unsharp-mask style detail boosts; avoided by guided filtering); hue drift
(RGB565 endpoints on re-encode to DXT1, model colour shifts, decoder tint);
over-smoothing and loss of fine detail (DXT restoration, PSNR-trained models);
ringing and over-sharpening (super-resolution); seams at wrap edges and pole
stretching (Homeworld 2 notes; the same geometry applies to X3's part-split
nebula bodies).

## 4. Feasibility against the X3 constraints

Runtime constraints: Direct3D 9, shader model 3, no compute, no geometry
shader, no atomics; TAA with jitter; FP16 linear target with bloom; native
Windows and CrossOver/Wine with documented D3D9 APIs only. Offline constraints:
`tools/regenerate/x3m_regenerate.py` is a PyInstaller bundle of stdlib Python
plus `numpy` (`tools/analysis/lod_atlas.py` and `tools/analysis/fog_families.py`
import numpy; no Pillow, scipy or ML runtime is bundled; measured by grep on
2026-11-04); it already decodes DXT1/3/5 and encodes DXT1/DXT5 in numpy
(`lod_atlas.py` `--atlas-format dxt`), and derived textures are generated on
the user's machine.

| Technique | SM3 runtime pass | Python offline bake | Reason |
| --- | --- | --- | --- |
| Catalogue or procedural star quads with minimum 2 px footprint, luminance from solid angle (section 2) | **Feasible** | n/a | A static vertex buffer of N stars × 4 vertices with per-vertex offset and the SM3 VS doing the billboard; no geometry shader needed. D3D9 point sprites are an alternative but are screen-aligned squares clamped by `MaxPointSize` and `D3DRS_POINTSIZE_MIN`, and transformed vertices express size in pixels ([Microsoft Docs](https://learn.microsoft.com/en-us/windows/win32/direct3d9/point-sprites), official), so quads give more control. |
| 2x2 pre-filtered point splat (software rasterizer) | **Not feasible** as published; **feasible** as a 2x2-pixel quad with analytic coverage | n/a | The paper's rasterizer needs compute shaders and atomics; the same energy split can be done by a fixed 2 px quad whose PS evaluates distance-to-centre weights. |
| Glare pyramid (sum of Gaussians) | **Feasible** | n/a | X3's bloom chain is already a mip pyramid; view-space anisotropic glare is an extra pass and not needed for a flat screen. |
| Luminance-weighted TAA blending, history bias near clamp, star-layer mask | **Feasible** | n/a | The project owns the TAA resolve; a stencil or alpha channel written by the star pass can carry the mask. |
| Runtime dither/deband of the painted nebula at sample time | **Feasible** | **Feasible** | Ordered or blue-noise dither in the nebula PS is a few ALU; offline it is the AdaDeband recipe (smooth banded regions, dither, re-quantise) in numpy. |
| Bicubic/Catmull-Rom magnification of 2048x1024 parts at 5120x1440 (3-4x magnification, measured) | **Feasible** with cost | **Feasible** (resample to 4096x2048) | SM3 can do a 4-tap bicubic via bilinear taps; cost scales with the few hundred background draws. Offline, Lanczos/bicubic in numpy and re-encode; the engine loads 4096x2048 (one AP mod texture is that size, measured). Memory: 4096x2048 DXT1 is 4 MiB per part, uncompressed 32 MiB per part (inferred). |
| Guided detail synthesis (guided-filter base/detail, noise modulated by luminance and gradient) | **Feasible** but tuning-heavy | **Feasible** | Guided filter is box-filter based and runs in numpy; runtime needs a noise texture and extra ALU per nebula pixel and interacts with TAA. Failure modes in section 3 apply to both. |
| Learned super-resolution or DXT restoration (ESRGAN, BitNet, Valve model) | **Not feasible** | **Feasible with a native helper** | No inference runtime in D3D9/SM3; offline it needs an ONNX or PyTorch runtime and model weights outside the stdlib+numpy bundle, plus the colour-shift and noise caveats. |
| Re-rendering nebulae from 3D volumes (EVE, X4 pipelines) or ray-marched sky-filling nebulae (Homeworld 3) | **Feasible** only as a new art pass (SM3 ray marching exists in the fog work) with a full-screen cost at 5120x1440 | **Not feasible** as enhancement | There is no 3D source for X3's paintings; this is new art, not preservation of the painted look. |
| Vertex-colour backgrounds (Homeworld 2) | **Not feasible** | **Not feasible** | Would replace the engine's textured bodies; out of scope for a texture overlay or a star pass. |

## 5. Stars as HDR point sources in a bloom pipeline

**Physical range (measured values from the sources, ratios inferred).** Direct
sunlight is 32,000 to 100,000 lux, a magnitude-0 star about 2.08 microlux at
the surface, and the whole moonless starlit sky about 0.0001 lux
([Lux](https://en.wikipedia.org/wiki/Lux), reference). Each magnitude step is a
factor 10^0.4 = 2.512 in flux ([Apparent magnitude](https://en.wikipedia.org/wiki/Apparent_magnitude),
reference). A magnitude-0 star is therefore about 1.5e10 to 5e10 below the sun
and the brightest star (Sirius, −1.46) about four times above magnitude 0;
sixth magnitude is 250 times below magnitude 0 (inferred). No game in section 1
publishes its star radiance relative to its scene.

**How a physically based renderer places them.** Schneegans et al. write the
scene to a 32-bit float target, compute exposure from the average luminance
(Pattanaik et al. 2000) with filmic tone mapping, set each star's luminance
from its solid angle (section 2) so that a star's flux is fixed regardless of
footprint, let the glare carry a fraction of the star luminance (their figure
uses 20 %), and scale the glare luminance by the frame's maximum luminance so
it fits half floats (academic). The consequence stated in the paper is that
stars must be proxies with known solid angle, otherwise the per-pixel radiance
is arbitrary and bloom and exposure respond to the proxy size rather than the
star.

**How engines keep bright points out of exposure and bloom.** Unreal's
histogram auto exposure uses low and high percentiles ("a good starting range
is 70 to 80" and "80 to 95") so that "very bright pixels (like the sun) and
very dark pixels (like deep shadows) are not involved" in the estimate, plus an
optional metering mask that weights the screen centre
([Auto Exposure](https://dev.epicgames.com/documentation/unreal-engine/auto-exposure-in-unreal-engine),
official). Bloom implementations derived from Jimenez's Call of Duty: Advanced
Warfare talk apply a luminance-weighted "Karis average" (weight 1/(1+luma)) on
the first downsample so that single bright pixels ("fireflies") do not survive
into the blur ([Diligent bloom README](https://diligentgraphics.github.io/docs/df/de0/DiligentFX_PostProcess_Bloom_README.html),
[fgdata bloom shader](https://git.nayala.duckdns.org/fly/fgdata/src/branch/next/Shaders/HDR/bloom_downsample.frag),
production code, medium quality). Stellarium's drawer lowers the visible
magnitude limit with eye adaptation when a bright object is in view, that is
bright sources suppress faint stars rather than lifting exposure (official
docs). A Unity project note records the opposite failure: an 8-bit target
clamped HDR stars before bloom, so the live glow vanished in captures
([issue](https://github.com/amindell11/astronomical-home/issues/638), community).

Summary (inferred): the evidence supports stars that are energy-conserving
proxies whose integrated flux is a tiny fraction of a sun or lit hull, a bloom
first-downsample that is luma-weighted so isolated star pixels do not bloom
into blobs, and an exposure meter whose percentile window excludes sparse
bright pixels. X3's background-metered exposure (percentile of tile geometric
means) already has the percentile property; the open numbers are the star
radiance relative to the painted nebula body and the sun disc.

## What this means for X3

The surveyed games split cleanly: data-driven or procedural stars are
documented only for Elite (and the technique of record for sub-pixel stability
is the 2025 Eurographics paper, not a game), while nebulae are everywhere
either offline renders, painted layers or full volumetric systems, and none
synthesises detail from a painting. For the FL stock set (212 of 254 nebula
textures at 2048x1024 DXT1 without mips, magnified 3-4x at 5120x1440, measured)
the offline literature offers debanding, dequantisation, resampling and
guided-filter detail layers that run in the bake's existing numpy stack, with
hue drift on DXT1 re-encode, noise and haloing as the documented risks and a
4096x2048 ceiling the engine is known to load; learned upscalers need a native
helper and carry colour-shift and noise caveats. For stars, an SM3 quad pass
with a minimum footprint, solid-angle luminance, bloom-pyramid glare and
luma-weighted TAA blending is feasible with documented D3D9 APIs on both
targets, and the exposure meter's percentile window is the documented way to
keep such stars from driving exposure. The two paths are independent: the
painted star body and the painted nebula body are separate geometry in
TBackgrounds, so either can be replaced without the other.

## Unknowns the design must resolve

- No official source states how Elite, Star Citizen, No Man's Sky or Starfield
  draw their individual stars; the "noise plus sprites" and "pinpoint stars
  separate from the skybox" descriptions are modders' readings.
- No game publishes star radiance relative to its scene or its bloom threshold
  policy; the physical ratios above are the only measured anchors.
- No source quantifies a minimum star footprint under TAA; the paper reports
  the flicker-versus-size trade-off qualitatively.
- The guided-detail approach has no shipped precedent on painted nebulae; its
  look must be judged on X3 textures.
- The Freelancer nebula-zone and Valve DXT-restoration pages, the Homeworld 3
  Steam post and several Nexus pages were unreachable; their content is cited
  through secondary coverage or search summaries.

## Sources

- Schneegans, Kreskowski, Gerndt, [Smaller than Pixels: Rendering Millions of Stars in Real-Time](https://www.uni-weimar.de/fileadmin/user/fak/medien/professuren/Virtual_Reality/documents/publications/2025_Smaller_than_Pixels_Rendering_Millions_of_Stars_in_Real-Time.pdf), Eurographics 2025 short papers; [project page](https://www.uni-weimar.de/de/medien/professuren/medieninformatik/vr/research/real-time-rendering/smaller-than-pixels-rendering-millions-of-stars-in-real-tim/); code in [CosmoScout VR](https://github.com/cosmoscout/cosmoscout-vr).
- Epic Games, [Temporal Super Resolution](https://dev.epicgames.com/documentation/unreal-engine/temporal-super-resolution-in-unreal-engine) and [Auto Exposure](https://dev.epicgames.com/documentation/unreal-engine/auto-exposure-in-unreal-engine), Unreal Engine documentation.
- Karis, High Quality Temporal Supersampling, SIGGRAPH 2014 Advances course, as cited in [Blender EEVEE luma-weighted history blending](https://projects.blender.org/blender/blender/pulls/120867/files); Playdead, [temporal reprojection AA](https://github.com/playdeadgames/temporal).
- Unigine, [Temporal Anti-Aliasing](https://developer.unigine.com/en/docs/2.21/principles/render/antialiasing/taa) (search summary).
- Stellarium, [StelSkyDrawer class reference](https://stellarium.org/doc/0.17/classStelSkyDrawer.html); Gaia Sky, [Star rendering](https://gaia.ari.uni-heidelberg.de/gaiasky/docs/3.7.3/Star-rendering.html) (search summary).
- Microsoft, [Point Sprites (Direct3D 9)](https://learn.microsoft.com/en-us/windows/win32/direct3d9/point-sprites).
- Tu et al., [Adaptive Debanding Filter](https://arxiv.org/abs/2009.10804), IEEE SPL 2020; Byun et al., [BitNet: Learning-Based Bit-Depth Expansion](https://arxiv.org/abs/1910.04397), ACCV 2018; He, Sun, Tang, Guided Image Filtering, TPAMI 2013 ([summary](https://en.wikipedia.org/wiki/Guided_filter)).
- Simon Schreibt, [What is Color Banding?](https://simonschreibt.de/gat/colorbanding/) and [Homeworld 2 Backgrounds](https://simonschreibt.de/gat/homeworld-2-backgrounds/); [img2sky](https://github.com/r-lyeh-archived/img2sky).
- Valve Developer Community, [Restoring Texture After DXT Compression](https://developer.valvesoftware.com/wiki/Restoring_Texture_After_DXT_Compression) (search summary); xinntao, [BasicSR issue 69](https://github.com/xinntao/BasicSR/issues/69); STEP, [texture upscaling notes](https://stepmodifications.org/wiki/User:TechAngel85/Sandbox3); GDCorner, [Testing Texture Super Resolution Techniques](https://www.gdcorner.com/rainbowsix/gamedev/superresolution/rendering/2019/01/04/SuperResolutionTesting.html) (search summary).
- Diligent Engine, [Bloom README](https://diligentgraphics.github.io/docs/df/de0/DiligentFX_PostProcess_Bloom_README.html); FlightGear fgdata, [bloom_downsample.frag](https://git.nayala.duckdns.org/fly/fgdata/src/branch/next/Shaders/HDR/bloom_downsample.frag).
- Wikipedia, [Lux](https://en.wikipedia.org/wiki/Lux) and [Apparent magnitude](https://en.wikipedia.org/wiki/Apparent_magnitude).
- PCGamesN, [Braben on re-creating a galaxy](https://pcgamesn.com/elite-dangerous/every-single-star-in-our-night-sky-is-in-elite-dangerous-david-braben-on-re-creating-a-galaxy-all-over-again), 2017; Elite fan wiki, [Graphics Mods](https://elite-dangerous.fandom.com/wiki/Graphics_Mods).
- Cloud Imperium Games, [Monthly Report February 2021](https://robertsspaceindustries.com/en/comm-link/transmission/18018-Star-Citizen-Monthly-Report-February-2021) and [May 2021](https://robertsspaceindustries.com/en/comm-link/transmission/18167-Star-Citizen-Monthly-Report-May-2021); Star Citizen Wiki, [Star Engine](https://starcitizen.tools/Star_Engine).
- Hello Games, [Prisms update](https://www.nomanssky.com/prisms-update/); Nexus Mods, [Stellarum](https://www.nexusmods.com/nomanssky/mods/4556), [Black Space](https://www.nexusmods.com/nomanssky/mods/1471) (search summaries).
- Egosoft, [Interview with Lino, Art Director](https://www.egosoft.com/news/current_en.php); X4 fan wiki, [Windfall](https://x4-foundations-wiki.fandom.com/wiki/Windfall); Nexus Mods, [DeadAir's Factions QHD Skybox Remaster](https://www.nexusmods.com/x4foundations/mods/888) (search summaries).
- CCP Games, [Introducing New Nebulae into EVE](https://www.eveonline.com/news/view/introducing-new-nebulae-into-eve), 2011; Engadget, [EVE Evolved: Touring a galaxy reborn](https://www.engadget.com/2011-12-11-eve-evolved-touring-a-galaxy-reborn.html), 2011; CCP, [Fanfest is here](https://www.eveonline.com/news/view/fanfest-is-here).
- ROCKFISH Games, [EVERSPACE Kickstarter update](https://www.kickstarter.com/projects/rockfishgames/everspace/posts/1370594) (search summary); Marco Unger, [Khaït Nebula environments](https://www.artstation.com/artwork/NyNeXN).
- Blackbird Interactive, [Homeworld 3 developer update](https://store.steampowered.com/news/app/1840080/view/3665415870863542835), June 2023, via [GamingTrend](https://gamingtrend.com/news/massive-homeworld-3-dev-diary-showcases-huge-audio-and-visual-upgrades/) and [Worthplaying](https://worthplaying.com/article/2023/6/30/news/138095-homeworld-3-reveals-details-about-visuals-audio-design-pushing-the-unreal-engine-4-and-more-screens-trailer/).
- Nexus Mods, Starfield: [Enhanced Milky Way](https://www.nexusmods.com/starfield/mods/7846), [Majestic Milky Way Replacer](https://www.nexusmods.com/starfield/mods/4152?tab=posts), [Shadowstar's 12k Milky Way](https://www.nexusmods.com/starfield/mods/17048) (search summaries).
- The Starport, [system INI](https://the-starport.com/wiki/ini-editing/typed-inis/system), [Starsphere Journey](https://the-starport.com/forums/topic/3005/starsphere-journey-documentation-of-the-journey), [Exclusion Zones](https://the-starport.com/forums/topic/4964/exclusion-zones/1?topic_id=5105&lang=en-US).
- SpaceEngine, [Current work](https://spaceengine.org/news/blog190217/), 2019.
- Unity Answers, [Star skybox and rotating camera](http://answers.unity3d.com/questions/1009567/star-skybox-and-rotating-camera.html); GameDev.net, [Texture flickering](https://gamedev.net/forums/topic/485982-texture-flickering/4176513/); [astronomical-home issue 638](https://github.com/amindell11/astronomical-home/issues/638).
- Repository facts: `verification/results/sector-background-textures/nebula_texture_sizes.txt` (texture census, 2026-11-03); `tools/regenerate/x3m_regenerate.spec`, `tools/analysis/lod_atlas.py`, `tools/analysis/fog_families.py` (bake stack).
