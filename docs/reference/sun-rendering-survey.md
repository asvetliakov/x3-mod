# Sun rendering in space games: a survey

Web survey written 2026-11-02 as input for the own-sun design note. Each claim carries
its source and a one-word confidence: **documented** (developer text, engine docs, paper,
or game data files) or **inferred** (player reports, mod behaviour, screenshots). Every
figure is marked the same way. Several primary pages (Egosoft forum, Frontier forum,
CryEngine docs, the Hullin and Spencer PDFs) were unreachable from this host; where a
claim rests on a search snippet or on prior knowledge of a paper it is marked inferred.

## Per game

### Elite Dangerous (Frontier, Cobra engine)
- Star body is geometry (a shaded sphere with a procedural surface; supergiants are "the
  regular star model scaled up"), with a separate corona layer around it. Players report
  the corona rendering at "very low resolution" after the 2018/2021 lighting rework, which
  points to a low-resolution billboard or half-res post layer rather than full-res geometry.
  https://steamcommunity.com/app/359320/discussions/0/3111403360724686685 ; inferred.
- Glare is a full-screen layer that "reacts to bright light sources", separate from the lens
  dirt / canopy scratch texture; auto-exposure dims the whole sky when a star is in view
  ("the sky brightens when looking directly at a sun"). Same thread and
  https://elitepve.com/viewtopic.php?t=5099 ; inferred.
- Occlusion: the star is depth-tested geometry, so planets and ships occlude it naturally;
  no sprite pop-through reports found. Inferred.
- No Frontier developer talk on star rendering was located (open question).

### Star Citizen (CIG, Star Engine / CryEngine lineage)
- The sun is a physical light-emitting object in the scene (Chris Roberts, Gamers Nexus
  2016 via https://starcitizen.tools/Star_Engine); documented (secondary).
- Lens flares are image-based: "entire source image" drives "4 physically based lens
  distortions per color channel on up to 20 individual elements"; bloom is "physically
  based" with "a wide exponential kernel based purely on light intensity" (no threshold);
  exposure is a histogram-driven eye model. Same page; documented but the wiki flags
  missing citations.
- CryEngine's Optical Flare System (the ancestor of CIG's flare tooling) builds a sun flare
  from elements: Glow (circular polygon with gradient texture, "Focus Factor"), Ghost /
  Multi Ghost (scattering range, position factor, offset), Chromatic Ring ("array of
  textured spikes"), Iris/Image-space shafts; one light per level has `AttachToSun`.
  https://www.cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/36868680 ; documented (search snippet; page not fetched).
- Streaks/rays: thin rays appear only as the Chromatic-Ring spike element or as a starburst,
  never as the main sun body. Inferred.

### No Man's Sky (Hello Games)
- The sun is drawn with the game's lens-flare system ("used in a multitude of places ...
  particularly flood lights and sun effects"); community "No Bloom" mods strip bloom and
  "all lens-flare effects besides the sun's", which shows the sun flare is a separate asset
  from the generic flare set. https://stepmodifications.org/wiki/NoMansSky:No_Bloom_Effect ,
  https://www.nexusmods.com/nomanssky/mods/2315 ; inferred.
- Look: large soft glow dominating the disc, light rays and ghosts stylised; no official
  rendering description located. Inferred.

### Everspace 1 / 2 (Rockfish, Unreal Engine)
- EVERSPACE 1: the sun flare is a material-driven sprite; occlusion by "a line trace from
  the camera to the light source" whose result is passed to the lens-flare material (so the
  whole flare fades when the sun centre is hidden, no per-pixel test).
  https://www.everspace.game/de/?p=298 ("Behind the Scenes #1") ; documented (snippet).
- EVERSPACE 2: after feedback the team added an option to turn off all lens flares.
  https://www.everspace.game/de/?p=314 ; documented (snippet).
- Takeaway: a UE sprite flare with centre-point occlusion was judged intrusive enough that
  a kill switch was shipped. Inferred.

### X4: Foundations / X Rebirth (Egosoft, the successor engine)
- The sun is a glow plus a layered lens flare; players ask for a mod that "removes lens
  flares while keeping the sun's glow" because the flare "obscures dogfighting and menus"
  ("[MOD] No Suns", https://forum.egosoft.com/viewtopic.php?p=2590227). Inferred (forum
  not fetchable; snippet only).
- X Rebirth introduced "geometric lens flares" (per the engineered.at article title,
  https://engineered.at/articles/x-rebirth-geometric-lensflares, unreachable): flare
  elements are geometry drawn in the scene, not a screen-space post pass. Inferred.
- No official Egosoft note on sun sizes, flare counts or occlusion was found; the Starbright
  mod thread confirms sunlight intensity is a per-sector parameter. Inferred.

### Homeworld Remastered / Homeworld 3 (Gearbox / Blackbird)
- HW2/Remastered backgrounds are a vertex-coloured sphere mesh built from a painted image
  (HW2BGBuilder); stars and suns are "separate single textures or billboards" on that
  background, not lit geometry. https://simonschreibt.de/gat/homeworld-2-backgrounds/ ;
  documented.
- The sun is therefore a painted hot-spot in the background art with a billboard flare on
  top; no occlusion beyond the painted layer order. Inferred.
- HW3 (UE4) uses engine bloom and a sprite sun; no developer statement located. Inferred.

### Starfield (Bethesda, Creation Engine 2)
- The visible sun is a lens-flare / post element: on AMD GPUs "the local sun is often
  missing" until the October 2023 patch fixed "sun lens flares not appearing".
  https://www.tomshardware.com/news/starfield-fails-at-stars-on-amd-gpus ,
  https://www.techspot.com/news/100099-amd-gpu-users-reportedly-cant-see-sun-any.html ;
  documented (patch notes via press).
- Players report the sun flickering when looked at directly (temporal instability of the
  flare under upscaling/TAA), and that flare-dimming mods change little "because there is
  so much bloom anyway". Steam thread 3824174193411515237, Nexus mod 2893 ; inferred.

### Kerbal Space Program 2 (Intercept Games, Unity)
- Kerbol is a sprite flare: a bug report shows the "sun flare pass through horizon of
  celestial body while in low orbit", the classic failure of centre-point occlusion.
  https://forum.kerbalspaceprogram.com/topic/215392-... ; documented (bug report).
- Dev Insights #23 "Black Hole Sun" (Jon Cioletti, April 2024) covers eclipse rendering and
  the visible corona during totality; the corona is a dedicated effect only shown when the
  disc is covered. https://forum.kerbalspaceprogram.com/topic/224277-developer-insights-23-black-hole-sun/ ;
  inferred (page not fetchable).
- KSP1 community practice: Scatterer's "sunflare" replaces the stock sprite with a large
  soft glow plus ghosts; a corona look in KSP1 "came from scatterer sunflare". Inferred.

### EVE Online (CCP, Trinity)
- 2014 "Solar Flares" dev blog (CCP Nag, lead artist): thirteen flare variants by star type,
  rewritten by a graphics programmer but keeping "their original artistic feel", "avoided
  heavy physics effects", "limited flashy elements to preserve EVE's calm mood"; the
  notable detail is motion: "gradual colour changes and elements that grow larger and then
  fade out" as the camera moves. https://www.eveonline.com/news/view/solar-flares ;
  documented.
- A CCP post adds that the effects are "dynamic according to the position of the sun on the
  screen" and the camera-drone lens tint changes with it. https://forums-archive.eveonline.com/message/4594900/ ;
  documented (snippet).
- Later intensity complaints ("some dev ... went overboard with the intensity", "sun glare
  make you blind") show the glare level, not the structure, is what players fight.
  https://forums.eveonline.com/t/burning-eyes-bloody-stars/257132 ; inferred.

### Freelancer (Digital Anvil, 2003) and mods
- `DATA\SOLAR\stararch.ini` defines a star as named layers: `star_glow`, `star_center`,
  `lens_glow`, `spines` (a named ray set, e.g. `blue2_spines`), `lens_flare`
  (e.g. `hex_white_lens_flare`), `radius` (4000 for the Li02 sun), and
  `zone_occlusion_fade_in/out`, `intensity_fade_in/out`.
  https://the-starport.com/forums/post/5138 ; documented.
- Suns are TXM texture billboards ("just TXMs with Texture nodes"), flat textures that
  "look odd when the camera moves"; building a sun as a model "looks poor".
  https://the-starport.com/forums/topic/2325/editing-suns/6 ; documented.
- Advanced Renderer mod replaces fake lights with "real sphere geometry and PBR bloom",
  point-light range = 3x glow size. https://the-starport.com/forums/post/65502 ; documented.
- The X3 lineage (Freelancer-era billboard sun with a spines layer) is exactly the look the
  user rejects. Inferred.

## Generic rendering references
- Spencer, Shirley, Zimmerman, Greenberg 1995, "Physically-based glare effects": glare =
  bloom (scatter in cornea/lens/retina), flare lines / ciliary corona (lens fibres, radial
  streaks), lenticular halo (diffraction ring, wavelength-dependent, hence rainbow-tinted).
  The halo is "a function of wavelength as well as angle". Adding glare measurably
  increases perceived brightness. https://w1.graphics.cornell.edu/pubs/1995/SSZG95.pdf ;
  documented (abstract; PDF unreachable). From prior knowledge of the paper: the daytime
  PSF mixes a narrow Gaussian bloom with 1/theta^3 and 1/theta^2 tails, so most energy sits
  within ~1 degree and the tails extend tens of degrees; inferred.
- Hullin, Eisemann, Seidel, Lee 2011, "Physically-based real-time lens flare rendering":
  ghosts from every two-reflection path through the lens stack, starburst from the Fourier
  transform of the aperture (precomputed), chromatic effects from per-wavelength tracing;
  culling the 20 % weakest ghosts gives no visible change, 40 % still acceptable;
  29.8 ms vs 159 s ray-traced reference. https://resources.mpi-inf.mpg.de/lensflareRendering/pdf/flare.pdf ;
  documented (figures from a KAIST summary of the paper).
- Jimenez 2014, "Next generation post processing in Call of Duty: Advanced Warfare": bloom
  as a mip pyramid, first downsample weighted 1/(1+luma) per sample against fireflies,
  3x3 tent upsample, no bright-pass threshold, bloom blend about 0.04 so only HDR values
  bloom visibly; exposure applied after bloom. https://www.iryoku.com/next-generation-post-processing-in-call-of-duty-advanced-warfare/ ;
  documented.
- Kawase 2003 (GDC, "Frame Buffer Postprocessing Effects in DOUBLE-S.T.E.A.L"): streak
  filter smears a bright spot along a direction vector in 4 to 6 ping-pong passes, 4 taps
  per pass with doubling offsets and per-tap attenuation weights; Ehrath's 7-tap variant.
  https://www.slideshare.net/slideshow/05-post-fx/48338447 (Engel lecture) ; documented.
- Stride engine light streaks (Kawase-type): parameters Amount, Streak Count, Attenuation
  (0 immediate falloff, 1 none), Phase, Color Aberration Strength, Is Anamorphic; streaks
  take the bright-filter output like bloom. https://doc.stride3d.net/4.0/en/Manual/graphics/post-effects/light-streaks.html ;
  documented.
- Chapman 2017, screen-space pseudo lens flare: threshold, mirrored ghosts, fixed-length
  halo, chromatic sampling, generous blur; the author's own verdict: rely on bloom/glare
  for perceived brightness and use sprite-based flares "for hero effects like the sun";
  starburst offset must change continuously with camera rotation so it does not jump.
  https://john-chapman.github.io/2017/11/05/pseudo-lens-flare.html ; documented.
- Angular size: the real sun subtends about 0.53 degrees; Blender's sun default is 0.526
  and its developers call that disc "not terribly realistic" looking, users enlarge it;
  Unity HDRP exposes `angularDiameter`; Bevy's `SunDisk` docs say bloom is needed for the
  disc to glow into the sky. https://blenderartists.org/t/size-of-sun-lamp-in-2-8/1168614 ,
  https://doc.qu1x.dev/bevy_trackball/bevy/light/struct.SunDisk.html ; documented.
- Hillaire 2016 (Frostbite sky): sun disc rendered at physical luminance with limb
  darkening (sections 4.1.2-4.1.3); glare left to bloom and exposure.
  https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/s2016_pbs_frostbite_sky_clouds.pdf ;
  documented (table of contents; body not fetched).
- Procedural star tutorials: corona as a camera-facing billboard about 3x the star diameter
  with animated noise; lens glow sized from luminosity. https://www.moddb.com/news/procedural-star-rendering ;
  documented. SpaceEngine draws the corona on three orthogonal planes through the star
  centre with fractal noise. https://spaceengine.org/news/blog140201/ ; documented (snippet).
- TAA and thin bright lines: jitter samples "alternately on and off a high contrast edge",
  so sub-pixel bright lines flicker; Unigine's "Fix Flicker" is "recommended for bright thin
  ropes, wires, and lines" but "may remove small bright objects".
  https://developer.unigine.com/en/docs/2.17/principles/render/antialiasing/taa ; documented.

## Synthesis
- What makes a sun belong to the scene (consensus across Star Citizen, Frostbite, Jimenez,
  Chapman, Advanced Renderer): a true HDR emitter whose energy the bloom pyramid and the
  auto-exposure respond to, so the glow is produced by the same filter chain as every
  other highlight rather than painted on. The glow has a wide, monotonic falloff (Spencer's
  1/theta^2..3 tails, Star Citizen's "wide exponential kernel") and no visible edge.
  Documented.
- Hard rays are a minority element everywhere: Freelancer's `spines`, CryEngine's Chromatic
  Ring spikes, EVE's thirteen artistic flares; modern games (Elite, Star Citizen, Starfield)
  show rays only as a faint starburst or anamorphic streak below the glow. Inferred.
- Occlusion drives dimming of the whole glow: Everspace's line trace, Freelancer's
  zone-occlusion fades, KSP2's horizon bug all show a visibility factor scaling the entire
  flare; the per-pixel depth test belongs to the core disc, the glow fades by a fraction.
  Documented/inferred.
- Lens elements (ghosts, halo, dirt) are what tie the sun to the camera; EVE and Chapman
  both stress that they must move continuously with the sun's screen position and never
  pop. Documented.
- Typical size ratios (inferred from the sources above, no game publishes them): core disc
  1 : soft corona/glow 3-6 (the "3x diameter" corona billboard, Spencer's bloom falloff) :
  faint wide halo 10-20 : streak length 5-15 core radii, with streak brightness one to two
  orders below the core. Inferred.
- Thin rays look painted when: they are full-resolution hard edges (TAA flicker and
  aliasing), equal in length and brightness (no irregularity), fixed in orientation to the
  world rather than to the camera (a lens starburst rotates with roll, a corona does not),
  brighter than the glow they sit in, and when they survive partial occlusion unchanged.
  Rays that read as optical are blurred (several pixels wide at their base), fade
  exponentially, and are a small fraction of the glow energy. Inferred.

## Recommendations for this project
1. Make the glow the dominant element: a wide radial falloff with 1/theta^2-like tails
   several core radii across, written in HDR so bloom and exposure take it over.
2. Shrink the ray budget: four to eight soft streaks (Kawase-style, several pixels wide at
   the base, exponential attenuation) at roughly a tenth of the glow's peak, or none.
3. Scale the whole glow and streaks by a sampled visibility fraction of the disc (a few
   depth taps across the core), and depth-test only the hot core per pixel.
4. Keep every sub-pixel feature out of the pre-TAA image: blur rays to at least two pixels
   and let the halo be a low-resolution layer, so jitter cannot make them flicker.
5. Add one subtle lens tie-in (a faint chromatic ring or one or two ghosts that slide with
   the sun's screen position) instead of more rays; this is what EVE, Star Citizen and
   Chapman use to anchor the sun to the camera.
