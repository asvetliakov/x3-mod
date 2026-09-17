# Sun shadows in shipped space games: a web-sourced survey

Written 2026-09-17 for the cascade plan in [shadow-cascades.md](shadow-cascades.md). Scope:
one dominant directional light, no ground plane, receivers from the own hull (metres) to
stations and capitals (tens of km). Every claim carries its URL; where a page could not be
fetched (Frontier's forum returns 403 to non-browser clients) the numbers come from search
excerpts and a community mirror, marked as such. Nothing here is verified against a running
build; it is reading, not measurement. Project units: 1 m = 5 units.

## 1. Per-game findings

### Elite Dangerous (Frontier, Cobra)

- **Technique.** Cascaded shadow maps configured per preset in `GraphicsConfiguration.xml`.
  Ultra ships six frustums plus a separate cockpit frustum ("six shadow frustums/cascades,
  seven if you count the cockpit"). Keys: `SliceSize`, `NumFrustums`, `FilterQuality`, `Fade`,
  `CrossFadeCascades`, `AdaptiveCascades`, `FrustumCockpit` (own `DepthBias`/`DepthSlopeBias`),
  and per-frustum `EndDistance`/`ShaderBias`.
  Sources: [Graphics Settings Beyond Ultra](https://forums.frontier.co.uk/threads/graphics-settings-beyond-ultra-shadows.507620/),
  [Tweaking shadows](https://forums.frontier.co.uk/threads/tweaking-shadows.537558/),
  [Shadow Quality](https://forums.frontier.co.uk/threads/shadow-quality.587551/).
- **Numbers (Ultra defaults, from a community override that lists the defaults inline;
  metres).** `SliceSize` 2048, `NumFrustums` 6, `FilterQuality` 3, `Fade` 0.01,
  `CrossFadeCascades` true, `AdaptiveCascades` false; `EndDistance` 50 / 250 / 600 / 1,325 /
  2,625 / 5,300; cockpit `DepthBias` 0.0001, `DepthSlopeBias` 1.5. Split ratios ≈ 5, 2.4, 2.2,
  2.0, 2.0: near-logarithmic after the first cascade. Modders raise `SliceSize` to 4096–8192
  and add frustums to 8–9 (4,096 / 8,192 m).
  [pastebin mirror](https://pastebin.com/QmifRmSC), search excerpt of the Frontier threads above.
- **Near range.** A dedicated cockpit frustum with its own bias: effectively a per-object
  map for the part of the own ship the player always sees. Hull self-shadow lives in
  cascade 0 (50 m).
- **Far range.** Last cascade ends at 5.3 km by default; beyond it nothing casts. Community
  reports: "beyond a few hundred meters, shadows can quickly turn into a mess of cloudiness
  and flicker" and ship shadows show "a transition cutting across them" between a fuzzy blob
  and a clear silhouette, attributed to "an insufficient number of shadow cascades for the
  distances involved".
  [Steam thread](https://steamcommunity.com/app/359320/discussions/8/6015206719894530446/),
  [Frontier flicker thread](https://forums.frontier.co.uk/threads/issue-with-extreme-shadow-flickering-and-shadow-contrast-on-planet-surfaces-post-patch-6-is-it-just-me.587916/).
- **Stabilisation.** `CrossFadeCascades` blends seams; `AdaptiveCascades` (off by default)
  presumably refits splits. No public statement on texel snapping.
- **Artefacts.** Long-standing flicker on/off with the star near the horizon and in rotating
  station bays (since U17–18, no config workaround documented).

### Star Citizen (Cloud Imperium, StarEngine from CryEngine 3)

- **Technique.** CryEngine lineage: sun GSM cascades with `e_GsmRange` (LOD-0 area, default
  3 m) and `e_GsmRangeStep` (each next cascade = previous × step, default 3.0), 0–5 LODs;
  cached far cascades (`r_ShadowsCache` = "cache cascade N and up", rendered once and kept,
  `e_ShadowsCacheUpdate`, `r_ShadowsCacheResolutions`), per-object shadow maps (≈ 0.25 ms for
  the Ryse protagonist on Xbox One) and shadow proxies.
  [CryEngine 3 shadows doc](https://www.cryengine.com/docs/static/engines/cryengine-3/categories/1638401/pages/1605703),
  [CryEngine cvars via Lumberyard](https://github.com/aws/lumberyard/blob/master/dev/Code/CryEngine/Cry3DEngine/cvars.cpp),
  [Godot per-object proposal quoting CryEngine cost](https://github.com/godotengine/godot-proposals/issues/5841).
- **StarEngine specifics.** "Major upgrade to shadow pool system: all lights share one giant
  pool for better dynamic resolution scaling, shadows can be cached between frames";
  camera-relative rendering keeps 32-bit render precision in a 64-bit world (CitizenCon 2953
  per the wiki). Players tune `r_ShadowsPoolSize` 64–512.
  [Star Engine wiki](https://starcitizen.tools/Star_Engine),
  [performance guide](https://referrals.eng.systems/en/guide-de-performance).
- **Recent.** Vulkan renderer enables ray-traced shadows ("softer and more realistic than the
  harsher rasterized versions"); GI adds screen-space shadows and directional occlusion.
  [Ray tracing wiki](https://starcitizen.tools/Ray_tracing),
  [PCGH May 2026 report](https://www.pcgameshardware.de/Star-Citizen-Spiel-3481/News/Graphics-Optimization-Improvements-May-2026-1544431/).
- **Limits.** No public cascade distances for SC itself; the geometric-step default (×3)
  and caching of far cascades are the documented CryEngine pattern.

### Everspace 1/2 (Rockfish, Unreal 4 → 5.3)

- **Technique.** Stock Unreal directional shadowing: CSM with `Dynamic Shadow Distance`,
  `Num Dynamic Shadow Cascades`, `Cascade Distribution Exponent` (1–4; higher packs
  resolution near the camera), `Cascade Transition Fraction`, `Shadow Distance Fadeout
  Fraction`, optional `Far Shadow Cascade Count/Distance` for tagged actors, `Inset Shadows
  for Movable Objects` (per-object maps), and screen-space `Contact Shadows`. Everspace 2
  moved to UE5.3 in 2024 (Lumen GI); no Rockfish statement on VSM versus CSM was found.
  [UE shadowing](https://dev.epicgames.com/documentation/en-us/unreal-engine/shadowing-in-unreal-engine),
  [UE dynamic scene shadows 4.27](https://dev.epicgames.com/documentation/en-us/unreal-engine/dynamic-scene-shadows?application_version=4.27),
  [Everspace 2 UE5 blog](https://www.unrealengine.com/en-US/tech-blog/everspace-2-sets-a-course-for-the-future-through-unreal-engine-5),
  [Revving the Engine](https://www.unrealengine.com/developer-interviews/revving-the-engine-everspace?lang=en-US).
- **Artefacts documented for the engine.** Visible quality drop at cascade boundaries
  ("blur appearing ~3 m away"), fixed by raising the distribution exponent at the cost of
  far-cascade density.
  [Unreal cascade trap](https://dev.to/adbhut/light-and-shadow-settings-unreal-engine-298p).

### X4: Foundations (Egosoft, XTECH 5, Vulkan)

- **Technique.** Not publicly documented. `config.xml` exposes `<shadows>` (quality tier) and
  `<softshadows>`; 7.00 lists "Enhanced shadows" as a feature; later engine work added
  parallax occlusion mapping and "stronger sunlight" lighting.
  [7.00 beta notes](https://steamdb.info/patchnotes/13948587/),
  [Steam config thread](https://steamcommunity.com/app/392160/discussions/3/1743355067115665243/),
  [GameRant interview](https://gamerant.com/x4-foundations-kingdom-end-expansion-interview/).
- **Artefacts (user reports at 3.0).** "Shadows get rough after a very short distance" and
  "flicker while flying"; "blurry shadow gets very sharp after a very short distance and gets
  blurry again": a cascade seam close to the ship, with the near cascade under-resolved.
  Only SSAA helped. [Egosoft thread](https://forum.egosoft.com/viewtopic.php?t=418177).
- **Relevance.** The direct successor shipped with the same failure mode this plan is trying
  to avoid: a coarse near cascade and a visible seam within a few hundred metres.

### No Man's Sky (Hello Games)

- **Technique.** "Cascaded shadow maps and screen space shadows"; lower settings reduce
  cascade count and resolution, higher tiers add filtering. Patch 5.0 (2024-07-17): "the
  shadow-rendering system has been reworked to take advantage of screenspace shadowing
  techniques, resulting in ... more accurate and more detailed shadows." Cascade count and
  distances unpublished.
  [settings guide](https://pcoptimizedsettings.com/no-mans-sky-voyagers-update-pc-optimization-best-settings-guide/),
  [patch 5.0](https://www.dsogaming.com/patches/no-mans-sky-patch-5-0-adds-volumetric-clouds-detailed-shadows-better-water-new-wind-system-performance-optimizations-and-support-for-nvidia-dlss-3/).
- **Pattern.** CSM for coverage, screen-space depth ray-march for near detail (grass, cockpit,
  hull panels): the two-lane approach that Unreal, Unity HDRP and UE5 all ship.

### Homeworld 3 (Blackbird, Unreal 4)

- Stock UE4 PBR with dynamic shadows and GI; maps "100 km in every direction" with
  megaliths; no cascade numbers published.
  [dev diary summary](https://gamingtrend.com/news/massive-homeworld-3-dev-diary-showcases-huge-audio-and-visual-upgrades/),
  [Worthplaying](https://worthplaying.com/article/2023/6/30/news/138095-homeworld-3-reveals-details-about-visuals-audio-design-pushing-the-unreal-engine-4-and-more-screens-trailer/).
  Homeworld 2 had no surface shadowing at all ("flatly lit from all angles").

### Freelancer (Digital Anvil, 2003, D3D8)

- The shipped engine has no shadow maps. A 2012 community hook added shadow mapping
  through an injected renderer: own ship only, PCF plus Gaussian blur, distance fade,
  "cascaded shadow mapping with variance shadow maps" planned for all casters "from small
  fighters over star destroyers to big planets". A d3d8to9 wrapper exposes a spurious ship
  shadow when the camera moves, i.e. a projected-blob remnant.
  [The Starport](https://the-starport.com/forums/topic/4565/freelancer-mod-news-freelancer-gets-true-shadows-to-its-graphics-engine/10?post_id=50534&lang=en-US),
  [dxwrapper issue](https://github.com/elishacloud/dxwrapper/issues/268).
- Relevance: the closest precedent to this project (shadows injected under a D3D8/9 game
  without source) started from one per-object map for the own ship.

### EVE Online (CCP, Trinity)

- **2011.** Per-ship shadow maps with variance shadow mapping; the light frustum fitted to
  the ship's own OBB instead of the bounding-sphere AABB ("tighter light focus without
  changing texture dimensions"); "fantastic in 90 % of cases, a little funky" otherwise.
  [Beauty hides in the shadows](https://www.eveonline.com/article/beauty-hides-in-the-shadows/).
- **2023.** Replaced by cascaded shadows; the old cap of 16 shadow casters in busy scenes
  removed; shadows always on above "disabled", filtering scaled by tier; MSAA → TAA.
  [Visual upgrades](https://www.eveonline.com/news/view/visual-upgrades-across-new-eden).
- **2025-08-18.** DXR ray-traced shadows (DX12, RT hardware) so a ship orbiting a station
  shadows it "based on the relative positions of objects".
  [RT shadows live](https://www.eveonline.com/news/view/upscaling-and-ray-traced-shadows-now-live).
- Trajectory: per-object → CSM → RT, in that order, for exactly this scene type.

### Kerbal Space Program (Squad, Unity)

- Unity CSM (2–4 cascades) with a shadow distance tied to quality; Unity fades shadows
  toward the far clip. Post-Unity-5 "flickering/sliding shadow effects ... in flight" were
  "the trickiest problem" (large-coordinate precision plus a floating origin), fixed with
  custom camera scripts and shader edits.
  [Enter the Shadows](https://forum.kerbalspaceprogram.com/developerarticles.html/enter-the-shadows-r205/),
  [Steam settings thread](https://steamcommunity.com/app/220200/discussions/0/35221584559456140/).
- Lesson: camera-relative sun-space and snapping are the difference between stable and
  sliding shadows at km scale; a stock CSM without them slides.

## 2. General techniques for huge directional scenes

- **Split scheme.** Logarithmic splits equalise perspective aliasing but put the first split
  at ~1 % of range (with 1000:1 near/far, PSSM(3) puts splits at 1 % and 10 %); the practical
  scheme `C_i = λ·C_log + (1−λ)·C_uni`, λ ≈ 0.5, is the shipped compromise; 3–4 splits are
  the usual trade-off.
  [GPU Gems 3 ch. 10](https://developer.nvidia.com/gpugems/gpugems3/part-ii-light-and-shadows/chapter-10-parallel-split-shadow-maps-programmable-gpus),
  [MJP survey](https://therealmjp.github.io/posts/shadow-maps/).
- **Stable cascades.** Bound each slice by a sphere so the ortho box does not change with
  rotation, snap the centre to texel increments; costs resolution (the sphere is wider than
  the slice) but removes crawling. Valient (ShaderX6) via
  [Engel](http://diaryofagraphicsprogrammer.blogspot.com/2008/06/stable-cascaded-shadow-maps.html),
  [Tardif](https://alextardif.com/shadowmapping.html),
  [MS CSM article](https://learn.microsoft.com/en-us/windows/win32/dxtecharts/cascaded-shadow-maps).
- **SDSM.** Read back the depth min/max (or histogram) and fit the splits per frame; better
  texel use than PSSM, requires a GPU reduction and trades away temporal stability.
  [Intel SDSM](https://www.intel.com/content/www/us/en/developer/articles/technical/sample-distribution-shadow-maps.html),
  [Lauritzen SIGGRAPH 2010](https://advances.realtimerendering.com/s2010/Lauritzen-SDSM(SIGGRAPH%202010%20Advanced%20RealTime%20Rendering%20Course).pdf).
- **Cached / reduced-rate far cascades.** CryEngine keeps cascades ≥ N rendered once until
  the camera leaves the cached zone; DigitalRune documents cascade locking and "updating
  distant cascades less frequently, distributing updates over several frames"; HDRP exposes
  per-cascade on-demand updates. The caveat everywhere: directional maps depend on camera
  position, so caches need a translation tolerance or camera-relative sampling.
  [DigitalRune shadow maps](https://digitalrune.github.io/DigitalRune-Documentation/html/4f8d2843-e46a-44cf-ba8d-c58fb8d9302d.htm),
  [HDRP shadows](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@12.1/manual/Shadows-in-HDRP.html),
  [Godot proposal 6948](https://github.com/godotengine/godot-proposals/issues/6948).
- **Per-object maps.** Unreal "inset shadows" and CryEngine per-object shadows render one
  bounding-box-fitted map for a chosen mesh (≈ 0.25 ms quoted for a hero character) and
  composite it with the cascades; EVE 2011 and the Freelancer hook did the same for ships.
  [Godot proposal 5841](https://github.com/godotengine/godot-proposals/issues/5841).
- **Screen-space contact shadows.** Per-pixel depth-buffer ray march toward the light over a
  short screen fraction (UE default off, typical length 0.1 of the screen; noise grows with
  length at a fixed sample count); HDRP's version is likewise a short ray march. Fixes hull
  detail the map's texel and bias cannot.
  [UE contact shadows](https://dev.epicgames.com/documentation/en-us/unreal-engine/contact-shadows-in-unreal-engine),
  [HDRP contact shadows](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@14.0/manual/Override-Contact-Shadows.html).
- **Softness.** HDRP: PCF tent 5×5 (9 taps) for directional at low/medium, PCSS at high with
  an `Angular Diameter` per directional light; PCSS "not recommended for consoles". The
  solar disc is ≈ 0.53°: penumbra width ≈ 0.0093 × blocker–receiver distance.
  [HDRP soften shadows](https://docs.unity3d.com/Packages/com.unity.render-pipelines.high-definition@17.4/manual/shadows-soften.html),
  [PCSS notes](https://wangkepfe.medium.com/area-light-percentage-closer-soft-shadows-pcss-9715ab0c9eb4).
- **Virtual / sparse shadow maps.** UE5 VSM: a 16k² virtual map per light in 128² pages, and
  for directional lights a clipmap stack (levels 6–22 by default, 64 cm to ≈ 40 km, each level
  16k over twice the radius), separate static/dynamic page caches, SMRT soft filtering by
  `Source Angle`; cost is page invalidation on motion and non-Nanite geometry.
  [UE VSM](https://dev.epicgames.com/documentation/unreal-engine/virtual-shadow-maps-in-unreal-engine),
  [Fortnite VSM](https://www.unrealengine.com/en-US/tech-blog/virtual-shadow-maps-in-fortnite-battle-royale-chapter-4).
- **Ray-traced and hybrid.** EVE and Star Citizen moved to DXR/Vulkan RT shadows; hybrid
  schemes ray-trace only the penumbra pixels found by the shadow map.
  [NVIDIA hybrid RT shadows](https://developer.nvidia.com/content/hybrid-ray-traced-shadows),
  [AMD FidelityFX Hybrid Shadows](https://gpuopen.com/fidelityfx-hybrid-shadows/).

## 3. Relation to this project's plan

Plan under review: four camera-centred, texel-snapped sun-space cubes at half-extents
250 / 1,500 / 7,500 / 25,000 units (50 / 300 / 1,500 / 5,000 m), all 4096² `R32F` on one
shared depth, per-pixel selection by sun-space extent with a 10 % blend band, the far
cascade on alternate frames, 3×3 rotated PCF with receiver-plane bias, and shadows applied
only to the per-pixel sun-share lane. Backend: D3D9 through wined3d on Metal, ps_3_0, no
compute, no depth readback, no RT.

**What the survey confirms.**

- Extents 250/1,500/7,500/25,000 are ratios 6, 5, 3.3: between Elite's default splits
  (5, 2.4, 2.2, 2, 2 over 50 m–5.3 km) and CryEngine's ×3 step. Elite's 5.3 km last cascade
  at 2048 and UE VSM's 40 km clipmap both cover the plan's 5 km far box; our far texel
  (12 units, 2.4 m) sits between Elite's 2.6 m (5,300 m / 2048) and VSM's level-22 texel.
  Not missing anything here; the split is already near-logarithmic.
- Camera-centred sun-space cubes with texel snapping are the Valient/Engel stable-cascade
  scheme (sphere-bound, snapped); KSP's flight flicker is what the plan avoids. Correct as is.
- Alternate-frame far cascade with retained basis is the CryEngine cached-cascade /
  DigitalRune locked-cascade pattern, made safe by composing the retained basis with the
  current camera. Shipped precedent; the one-frame stale mover is the known cost.
- Cascade blend band matches Elite's `CrossFadeCascades` and Unity's seam blending; the X4
  3.0 and Elite complaints are exactly the seam-without-blend and coarse-near-cascade cases.

**Candidates the survey suggests, each with a one-line cost/benefit for this backend.**

1. **Per-object map for the own ship, replacing or supplementing C0.** EVE 2011, Freelancer's
   hook, Elite's cockpit frustum, UE inset, CryEngine per-object all do it; OBB-fitted, the
   map's texel on a 10 m fighter would be ≈ 1.2 cm at 1024² versus C0's 12 cm at 4096². Cost:
   one extra small map and a fourth sampler in the quad plus a per-draw "own ship" test we do
   not have (the plan's cascade mask is by bounds, not identity); benefit: 4096² C0 (64 MiB and
   a full clear) could drop to 1024² with better hull resolution. Worth a design note once
   run 38 shows whether C0's clear/fill cost matters; not before.
2. **Screen-space contact shadows for the near band.** Would recover panel-scale hull
   self-shadow that a 12 cm texel plus bias loses, and the UE/HDRP/NMS precedent is strong.
   Cost: a ps_3_0 depth ray march over RT2's view position (8–16 taps per pixel of the sunlit
   lane, ≈ +0.1–0.3 ms on the quad at 768p, guess not measurement) and a second source of
   error for the twin; benefit: hull detail without more map resolution. Defer until the user
   judges the C0 hull result in run 38; it composes with the sun-share lane as `min(f, f_ss)`.
3. **Distance fade of the last cascade.** Elite (`Fade`), Unity, Unreal (`Shadow Distance
   Fadeout Fraction`) and the Freelancer hook all fade the far cascade to lit instead of
   cutting. The plan's "outside every cascade, lit" is a hard cut at 5 km; a linear fade over
   the last 10–15 % of C3's extent costs one `saturate` per pixel and removes a popping
   border when a station's shadow crosses the box. Cheap; add it to the apply quad.
4. **PCSS with the sun's 0.53° disc.** Precedent (HDRP high tier) and physically right, but
   a blocker search plus variable kernel on four cascades is 2–3× the taps and PCSS is the
   tier vendors exclude from fixed-hardware targets; at our texel sizes (12 cm–2.4 m) the
   penumbra (0.9 cm per metre of blocker distance) is sub-texel for hull self-shadow and only
   exceeds a texel for station-on-ship at > 100 m. Keep 3×3 PCF; if softness is wanted, scale
   the kernel by cascade index (already the case, kernel is in texels) rather than PCSS.
5. **Logarithmic splits / SDSM.** Both need per-frame refits, and SDSM needs a depth readback
   or compute; neither fits a snapped camera-centred cube whose reuse depends on a fixed
   extent. Rejected; the fixed near-log extents are the right choice for this backend.
6. **Cached far cascade keyed on a caster hash (CryEngine `r_ShadowsCache`).** The plan
   already lists "held centre plus caster-change hash" as deferred; the survey confirms it as
   the standard next step if C3 dominates at rest. No change to the plan now.
7. **Over-building check.** Four 4096² maps (256 MiB) exceed every shipped CSM here (Elite
   Ultra 6 × 2048², 24 MiB at `D16`); VSM reaches 16k virtual but pages sparsely. The
   `D24X8` shared attachment alone is Elite's whole budget. The plan's own fallback (C0 at
   1024², `D16`) is the right lever if run 38's clear/fill numbers bite; the survey adds the
   per-object own-ship map (item 1) as the way to keep hull quality while shrinking C0.
8. **Ray-traced or hybrid shadows.** EVE and Star Citizen's endpoint; unavailable under D3D9
   on wined3d and out of scope for native Windows support of this proxy. Not applicable.

**Net.** Nothing in the survey contradicts the ratified cascade layout. The two low-cost
additions are a last-cascade distance fade (item 3) and, after run 38, a decision between a
per-object own-ship map (item 1) and screen-space contact shadows (item 2) for the near
band; both are precedent-backed and neither changes the sun-share lane contract.
