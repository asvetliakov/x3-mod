# Weapon impact effects: references for the hull-hit pass

Reference note for the weapon-effects pass. Scope: how modern space games and films compose a weapon hit on a hull, the
element vocabulary that recurs, the techniques that fit our closed-form
additive renderer (analytic geometry, FP16 additive shaders over the hull
depth buffer, no particle state, every element a function of age), and a
recommended anatomy per weapon class. Written 2026-11-06 from web sources (GDC Vault, 80.lv, hard-light wiki,
Frontier forums and Fandom were unreachable from this host; those entries use
listing text, press or engine source); every claim carries a source or is marked **inference** (unsourced, from
memory of the game or from reasoning). Durations in the sources are rare;
where a number is given it is quoted, otherwise it is our inference.

## 1. How the references compose a hit

### Games

**Star Citizen (CryEngine/StarEngine).** The GDC 2015 talk "Advanced Visual
Effects with DirectX 11 & 12: Visual Effects in Star Citizen" (Alistair Brown)
covers the particle system, the ship damage system and shield rendering
([GDC Vault listing](https://gdcvault.com/play/1021768/Advanced-Visual-Effects-With-DirectX)).
Press coverage of the same talk: hull heat from shots is shown as light that
gradually fades, leaving a permanent scar; paint chips differently from the
metal beneath; each surface reacts per weapon
([Multiplayer.it](https://multiplayer.it/articoli/146111-star-citizen-astronavi-di-nuova-generazione.html)).
The damage system records four per-texel quantities in ship-wrapping damage
maps: temperature (current heat), burn (peak heat, drives scorching),
thickness (paint first, then metal), deformation; "lasers will cause your hull
to light up for several seconds", a powerful ballistic "might tear a hole
straight through your hull"; particles are simulated, spawned and rendered on
the GPU, 10-100x the CPU count at equal cost
([WCCFtech, PAX East 2015](https://wccftech.com/star-citizen-damage-technology-pax-east/)).
Weapon classes are ballistic, energy (laser repeaters/cannons) and
distortion; distortion repeaters have near-zero hull damage and a large
distortion radius ([starcitizen.tools](https://starcitizen.tools/Ship_weapons)).
**Inference** (game observation): laser hits are a short white-core flash with
a coloured halo and a glowing scorch that fades over seconds; ballistic hits
are a smaller flash plus a spray of yellow-white sparks and dark chips;
distortion hits are blue-white electrical arcs crawling over the hull for
~0.5 s with no scorch.

**Elite Dangerous (Cobra).** No Frontier VFX talk was found; Frontier VFX
artists describe only particle budgets (explosions can spend more particles
because their life is short; continuous effects are memory-heavy)
([Frontier forums, Meet the Team](https://forums.frontier.co.uk/goto/post?id=207446)).
Weapon damage classes are thermic (lasers), kinetic (multicannon), explosive
and the thermic-kinetic hybrids railgun and plasma accelerator
([Frontier forums](https://forums.frontier.co.uk/goto/post?id=3717187)). The
Scramble Spectrum experimental makes the pulse laser "cause electrical
discharges at the point of impact"
([Elite wiki](https://elite-dangerous.fandom.com/wiki/Scramble_Spectrum)); at
close range the point of impact snaps to the target
([Steam guide](https://steamcommunity.com/app/359320/discussions/0/1742216747647801936)).
**Inference** (game observation): laser hits are a sustained bright spot with a
soft orange glow halo and a heat-shimmer sprite, no sparks; multicannon hits
are brief sparks plus small grey debris chips and a faint puff, leaving dark
pits; plasma hits are a large violet-white bloom with a slow-fading globe of
plasma and a ring; railgun hits are a thin white flash at entry with a long
glowing hole and sparks ejected along the shot axis.

**X4: Foundations (Egosoft).** Bullet macros reference effects through an
`<effects>` block with `<impact ref="..."/>`; `Defaults.xml` adds
`<shieldedsurface ref="shield_surface_impact"/>` for hits on shields; a
beam-style bullet (`scale="1"`) does not render a custom impact while shields
are up ([Egosoft forum](https://forum.egosoft.com/viewtopic.php?p=5157934),
[Egosoft forum](https://forum.egosoft.com/viewtopic.php?p=4860344),
[Egosoft forum](https://forum.egosoft.com/viewtopic.php?p=5186989)). **Inference** (game observation): X4 hull hits are a small round flash plus a
few short sparks and a soft smoke puff that lasts about a second; shield hits
are a hexagon-cell ripple fading out over ~0.5 s; the hull keeps no scorch.

**Homeworld 3 (Blackbird).** Four damage types, kinetic, high explosive,
energy, ion, each with its own visual treatment; "Splat Damage" replaces
sticker decals with dynamic impact points that alter hull geometry and
properties at the hit, persisting between missions; each ship has a dynamic
damage texture that progresses from surface scars to breaches with secondary
hot spots and burn marks, prioritised for capitals; fighters use a sprite
puff at low speed and an ink-like ribbon trail at high speed; larger ships
have damage sockets with light/heavy/critical stages (light: thicker darker
smoke; later: fire, breach, sparks)
([Bleeding Cool](https://bleedingcool.com/games/homeworld-3-releases-new-blog-featuring-battle-details),
[DTF translation of the Steam post](https://dtf.ru/games/1724401-oruzhie-i-uron-yunitov-v-homeworld-3),
[PC Gamer](https://pcgamer.com/inside-homeworld-3-how-blackbird-interactive-aims-to-make-you-feel-attached-to-your-fleet-as-you-see-the-history-of-battles-through-the-whole-campaign)).
No durations are published.

**FreeSpace 2 Open / MediaVPs.** The classic table-driven model. weapons.tbl
has `$Impact Effect` (particle effect) with the legacy `$Impact Explosion` +
`$Impact Explosion Radius` (animated bitmap, radius default 1.0, beams get a
randomised radius 1.2-1.9x), `$Dinky Impact` for the low-cost variant,
`$Shield Impact Explosion Radius` (defaults to the average impact particle
radius), `$Piercing Impact` with velocity, splash velocity, variance, life and
particle count, `$Conditional Impact` selected by armor type/angle/health, and
a `$Shockwave` block (inner/outer radius, speed, rotation, model or bitmap,
`$Dinky shockwave`)
([fs2open weapons.cpp](https://raw.githubusercontent.com/scp-fs2open/fs2open.github.com/master/code/weapon/weapons.cpp)).
Shockwave law: `radius += frametime * speed`, clamped to outer radius,
`total_time = outer_radius / speed`; the 3D model fades as
`1 - radius/outer_radius`; the framebuffer distortion holds at full strength to
90 % of life then fades
([fs2open shockwave.cpp](https://raw.githubusercontent.com/scp-fs2open/fs2open.github.com/master/code/weapon/shockwave.cpp)).
MediaVPs ship "high-res weapon and weapon impact effects" and a 3D shockwave
([hyperlogos](https://www.hyperlogos.org/Freespace-2-MediaVPs-lllustrated)).
Particles can inherit the weapon's last velocity for an impact effect
([FS wiki Particles.tbl](https://wiki.hard-light.net/index.php/Particles.tbl)).

**Everspace 2, Star Wars Squadrons, BSG Deadlock, Dreadnought.** No developer
breakdowns of the impacts were found. Everspace 2 moved to UE5 in 2024 and
its weapon effects are Niagara-era
([Game Developer](https://gamedeveloper.com/business/rockfish-to-future-proof-everspace-2-with-unreal-engine-5-upgrade));
Squadrons is Frostbite ([Wikipedia](https://en.wikipedia.com/wiki/Star_Wars_Squadrons))
and EA's Battlefront II VFX note states the design rule that gameplay VFX exist
to "communicate gameplay mechanics and give feedback"
([EA community transmission](https://answers.ea.com/t5/Announcements/Community-Transmission-Visual-Effects-in-Star-Wars-Battlefront/td-p/9036396));
Deadlock is Unity ([Wikipedia](https://en.wikipedia.org/wiki/Battlestar_Galactica_Deadlock));
Dreadnought is UE4 with Maya-fluid flipbooks
([80.lv](https://80.lv/articles/an-incredible-ue4-demo-created-by-yager-artists/)).

### Films and TV

**Star Wars (original trilogy).** Space explosions were pyrotechnic elements
filmed against black for clean mattes, lined up on the ship element and
swapped in sync with the laser fire; larger explosions were overlaid on smaller
ones "to help give more punch"; the Death Star blast was shot from below so the
sparks fall toward camera, reading as zero-g
([fan analysis](https://sawfish-blackbird-kh6j.squarespace.com/spacebattlefromreturnofthejedi),
[Propstore](https://propstore.com/?p=7530)). Personal blaster deflections
used pyrotechnic squibs on the actor
([Star Wars Aficionado](https://starwarsaficionado.blogspot.com/2021/06/an-empire-at-40-shooting-vader.html));
the guns fired blanks and the bolt was animated in post
([Wikipedia](https://en.wikipedia.org/wiki/Blaster_(Star_Wars))). **Inference**:
the canonical blaster-on-metal hit is a squib: a hard white flash a few frames
long, a fan of hot sparks with gravity, a small dark smoke puff, a scorch.

**Battlestar Galactica (2004).** Zoic's documentary style, hand-held virtual
camera, avoiding Star Wars clichés; the writers spent time on vacuum physics
("what happens if a nuke goes off in space")
([CGW 2006](https://www.cgw.com/Publications/CGW/2006/Volume-29-Issue-5-May-2006-/Half-the-Battle.aspx));
fire effects were LightWave particles plus compositing
([Post Magazine 2008](https://www.postmagazine.com/Publications/Post-Magazine/2008/November-1-2008/VISUAL-EFFECTS-BATTLESTAR-GALACTICA.aspx)).

**The Expanse.** The production's rule is physics first ("the first thing an
animator has is a physics lesson"), aiming to show space as astronauts would
recognise it
([3D World via PressReader](https://www.pressreader.com/australia/3d-world/20200324/281608127508924));
showrunner Shankar wanted ships to "move like real ships" and rejected WWII
dogfighting, with Gravity as the reference, and the season 4 battle was fully
previsualised ([VFX Voice](https://vfxvoice.com/new-visual-vistas-propel-the-expanse-into-higher-orbit/)).
The CQB episode is held up as the most realistic TV space battle; PDC recoil is
countered on screen by burst thrusters
([The Companion](https://thecompanion.app/the-expanse-cqb-space-battle/)).
**Inference** (episode observation): PDC hits are a dense stream of tiny white
flashes, each a frame or two, with chips and vapour (venting atmosphere)
streaming out of the holes and no fireball; railgun hits are a single bright
entry flash, a through-hole and a jet of glowing debris out of the far side;
nothing burns, the glow of hot metal and the venting gas are the "post-hit".

## 2. Element vocabulary

Elements that recur across the references, with who uses them:

- **Initial flash.** A 2-3 frame concentrated bright core on an explosion
  breakdown ([realtimevfx frag breakdown](https://realtimevfx.com/t/stylized-frag-launcher-explosion-overwatch-inspiration-breakdown-posted/1894));
  a game-VFX guide suggests the core flash peaks in the first 0.05-0.1 s and a
  small hit lasts 0.2-0.4 s total
  ([gamineai guide](https://gamineai.com/blog/how-to-create-game-vfx-particle-systems-visual-effects)).
  Everyone has it; it is the element that, alone, reads as "flash".
- **Hot-gas puff / mini fireball.** The "burst core" pushes hot gas outward and
  the "burst ring" expands and dissolves from it
  ([realtimevfx](https://realtimevfx.com/t/stylized-frag-launcher-explosion-overwatch-inspiration-breakdown-posted/1894)).
  In vacuum settings (Expanse, BSG) it is replaced by vented gas and vapour;
  Star Wars and X4 keep a small fireball. **Inference**: energy and plasma
  weapons justify a puff (vaporised hull), ballistics do not.
- **Molten splash and sparks.** Two spark types, trailed and stretched,
  spawned with the core and dying just after the ring; reviewers asked for
  varied lifetimes so they do not all die at once
  ([realtimevfx](https://realtimevfx.com/t/stylized-frag-launcher-explosion-overwatch-inspiration-breakdown-posted/1894)).
  A bullet-impact recreation layers a quick dense burst, a few slower larger
  sparks delayed slightly, and a slow gravity set
  ([ArtStation](https://liamandrews1.artstation.com/projects/8BXOZQ)).
  In vacuum sparks keep straight lines (ILM shot the Death Star from below so
  sparks "fall toward camera"); **inference**: no gravity, but the spark
  decays by cooling (bright white to orange to dark) rather than by falling.
- **Ring / shockwave.** FS2 expands linearly and fades 1 - r/R; distortion
  overlay holds to 90 % of life ([shockwave.cpp](https://raw.githubusercontent.com/scp-fs2open/fs2open.github.com/master/code/weapon/shockwave.cpp)).
  A reviewer's rule: expand fast then ease over 0.2-0.3 s; a burst that
  "starts big and then shrinks" feels weak
  ([realtimevfx thread](https://realtimevfx.com/t/from-absolute-zero-to-vfx-junior-in-4-months-is-it-possible/8463/26)).
  Physically a blast wave needs a medium (Sedov-Taylor is derived for air,
  [Wikipedia](https://en.wikipedia.org/wiki/Blast_wave)); the ring in space is
  a convention for plasma/explosive hits only (**inference**).
- **Heat shimmer / refraction.** Screen-space UV perturbation by a bump normal
  with a mask, used in Far Cry for heat haze
  ([GPU Gems 2 ch. 19](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-19-generic-refraction-simulation));
- **Smoke / vapour.** Smoke starts at once and outlasts everything in the
  frag breakdown; in vacuum the references replace it with venting atmosphere
  (Expanse, **inference**) or an "ink-like" ribbon trail that only appears on a
  moving fighter (Homeworld 3). **Inference**: a short-lived vapour puff that
  inherits the hull's velocity reads as vacuum; a drifting cloud does not.
- **Lingering glow / scorch.** Star Citizen: hull "lights up for several
  seconds" after a laser, burn = peak temperature drives permanent scorching
  ([WCCFtech](https://wccftech.com/star-citizen-damage-technology-pax-east/));
  Homeworld 3: persistent per-ship damage texture with hot spots then burn
  marks. This is the "post-hit" element every modern reference has.

Which differ by class (sources plus inference): **energy (laser/pulse)**: flash
+ glow halo + scorch that cools; no sparks or ring (SC, Elite). **Ballistic**:
small flash, sparks, chips, pit, little glow (Elite multicannon, Expanse PDC,
SC ballistics). **Plasma**: large bloom, lingering plasma globe, ring, heavy
scorch (Elite PA, **inference**). **Beam**: continuous spot with shimmer and a
molten crawl along the hull; FS2 gives beams a randomised radius 1.2-1.9x.
**Railgun/mass driver**: thin entry flash, through-hole, sparks along the shot
axis, long-lived glowing hole (Elite, Expanse, **inference**). **Missile**:
fireball + ring + debris (FS2 shockwave block, X4 torpedo explosion refs).
**Distortion/ion**: arcs and flicker, no scorch (SC, Homeworld 3 ion type).

## 3. Techniques that fit a closed-form additive renderer

- **Impostor fireball with noise.** Perlin's raymarched density sphere (density
  1 at centre to 0 at edge, domain-warped turbulence, scaled over time) renders
  an explosion with no particles; a sphere-mask impostor with fBm is the cheap
  variant ([gamedev.net thread on Perlin's explosion](https://gamedev.net/forums/topic/470719/),
  [Moinet & Neyret sphere tracing](https://diglib.eg.org/items/366131fe-f2df-4841-89c3-f24c30f502f8/full)).
  For us: one camera-facing quad with a sphere-equation fake depth and
  age-driven fBm; the "puff" is a function of age, not a flipbook.
- **Flipbook-free puffs.** Flipbooks need motion-vector blending to avoid
  cross-fade ghosting ([80.lv tutorial](https://80.lv/articles/tutorial-motion-vectors-for-time-warping-image-sequences),
  [realtimevfx thread](https://realtimevfx.com/t/motion-vectors-for-animated-textures-how-do-you-generate-them/575)).
  Procedural noise sidesteps the whole problem; the frag breakdown's reviewers
  also preferred a stepped alpha gradient to a smooth falloff for punch.
- **Soft-particle depth fade.** `zFade = saturate(scale * (particleDepth -
  sceneDepth))`, multiplied into alpha, so a puff sinks into the hull instead
  of cutting ([GPU Gems 3 ch. 23](https://developer.nvidia.com/gpugems/gpugems3/part-iv-image-effects/chapter-23-high-speed-screen-particles),
  [NVIDIA SoftParticles 2007](https://developer.download.nvidia.com/whitepapers/2007/SDK10/SoftParticles_hi.pdf)).
  We already draw over the hull depth; this is one extra depth sample.
- **Motion-stretched sparks.** Stretch the sprite along velocity by length
  scale and speed; set a camera-stretch term to zero to keep sparks in world
  space; "freeform stretching" keeps them from thinning head-on
  ([Unity Renderer module](https://docs.unity3d.com/cn/2022.3/Manual/PartSysRendererModule.html));
  Niagara's Velocity Aligned mode rotates the sprite up-vector to velocity
  ([Epic docs](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/Niagara/ENiagaraSpriteAlignment)).
  For us: a spark is a closed-form segment `p(t) = p0 + v t` with length
  `|v| * dt_stretch`, drawn as a quad with a soft line profile; brightness
  `exp(-t/tau)` and colour temperature falling with age.
- **Ring expansion law.** FS2: linear radius, alpha `1 - r/R`. Reviewer rule:
  fast then slow. **Inference**: `r(t) = R (1 - exp(-t/tau_r))` or
  `R * (t/T)^0.4` (Sedov-like) with alpha `(1 - t/T)^2` reads as a shock; a
  linear ring reads as a UI circle.
- **Decal and glow fade.** Star Citizen stores temperature and burn: temperature
  decays, burn is monotone. **Inference** for a stateless renderer: a hit's
  glow is `G(t) = A exp(-t/tau_hot)` (~1-2 s) added to a scorch term
  `S(t) = B (1 - exp(-t/tau_cool))` that fades over 10-30 s; colour runs
  white -> orange -> dull red as `G` falls.
- **Refraction puff.** GPU Gems 2 ch. 19: perturb the backbuffer UV by a normal
  map, masked. For an additive renderer this is the one element that needs a
  copy of the colour buffer; FS2 ships it as an optional "framebuffer
  shockwave".
- **Light flash on the hull.** SC "hull lights up"; the frag breakdown uses the
  shockwave as background light for the flash. **Inference**: a short-range
  point light approximated as an additive radial gradient projected on the
  hull depth (we have no lighting pass) over 100-200 ms.

## 4. Recommended anatomy per weapon class (for our tiers)

Times are for the fighter tier; corvette x1.5, capital/station x2-2.5 in size
and about x1.3 in duration (**inference**, SC/HW3 prioritise bigger effects
on capitals). Frame-step scaling under SETA applies to every bound.

| Class | 0-50 ms | 50-300 ms | 0.3-2 s | post-hit (2-30 s) |
| --- | --- | --- | --- | --- |
| Energy (laser, pulse) | white core flash, 2-3 frames, hull-colour halo | halo cools to weapon colour, small vapour puff sinks into hull (depth fade) | glow spot `exp(-t/1.5 s)` | dark scorch fading over ~20 s |
| Ballistic (gatling, cannon) | pinpoint flash, 1-2 frames | 8-16 stretched sparks, two lifetimes (0.15 s and 0.5 s), 3-6 dark chips keeping velocity | faint pit glow 0.5 s | pit, no glow; dark mark ~10 s |
| Plasma | large bloom (2x laser), 2 frames | fireball impostor with fBm, ring fast-then-slow to 0.3 s, few slow sparks | plasma globe shrinks and reddens over 1 s | heavy scorch, hot-spot glow ~3 s |
| Beam | continuous spot + shimmer | molten crawl: sparks every 30-50 ms along the dwell path | glow trail along the path | scorch line, cools as the laser case |
| Railgun / mass driver | thin entry flash along shot axis | spark jet along the axis, exit-side jet on thin targets | hole glow `exp(-t/2 s)` | glowing hole 5 s, dark hole after |
| Missile | fireball flash | fireball + ring + debris, X4 delayed secondary at ~0.3 s | ember flickers 1-2 s | large scorch, vented vapour 3 s |
| Distortion / ion | blue-white flash | arcs crawling over the hull, 3-5 flickers over 0.5 s | nothing | nothing (no scorch) |

What makes it read as an impact rather than a flash (sources plus inference):

1. **Structure.** At least three layers with different shapes: a point (flash),
   a spray (sparks or chips), a surface mark (scorch/glow). The frag breakdown
   needed 18 layers staggered by "2-3 frames"; the reviewer rule is to stagger
   like a cartoon explosion
   ([realtimevfx](https://realtimevfx.com/t/stylized-frag-launcher-explosion-overwatch-inspiration-breakdown-posted/1894)).
2. **Asymmetry.** Sparks and chips follow the incoming shot direction mirrored
   about the hull normal (FS2 piercing impact has velocity and splash
   velocity; Expanse/Elite railguns spray along the axis). A radial-only burst
   reads as a flash.
3. **Motion.** Stretched sparks and chips that inherit hull velocity; FS2
   particles inherit the weapon's last velocity. Static sprites read as decals.
4. **Two-timescale decay.** A fast element gone within 0.3 s and a slow
   element (glow, scorch, vapour) outlasting it by 10x; every reference keeps
   something after the flash: SC seconds of hull glow, HW3 persistent scars,
   Expanse venting, X4 delayed distortion. The reviewer's "fast at the start,
   slow at the end" applies to the ring and to the whole effect.
5. **Readability test.** Check in greyscale; value contrast and silhouette
   before colour ([skill guide](https://playbooks.com/skills/omer-metin/skills-for-antigravity/vfx-realtime),
   community heuristic).

Open gaps: no public source gives Elite, X4, Everspace 2 or Squadrons element
sizes or lifetimes; Star Citizen's GDC slides are behind the Vault; the Expanse
vacuum rules are only paraphrased in press. Section 4 numbers are inference.
