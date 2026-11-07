# Ship and station death explosions: references for the explosion pass

Reference note for step 3 of the weapon-effects pass (`docs/architecture/weapon-look.md`
section 3.2). Sibling of `impact-effect-references.md` and `bolt-muzzle-references.md`;
same marking rules. Scope: how modern space games and films draw the death of a fighter,
a corvette, a capital ship, a station and a missile, what reads as modern, and which
parts fit a closed-form additive D3D9 renderer (ray-sphere and box impostors, analytic
streaks, sprites and glows, per-pixel programs in the low thousands of slots, FP16
additive over the scene plus a multiply pass for dark smoke, TAA, 5120x1440, no particle
state, no hand-painted flipbooks, every element a function of age and seed). The current
plan is the 3.2 law (flash, fireball impostor with two fbm octaves, shock shell, embers,
hull glow, secondaries; tiers fighter / corvette / capital / station / missile; life 1.6
to 6 game s; smoke off in the first flight); the failure it must not repeat is the one
`hull-burst-look.md` section 1 measured on the hull hit: a glow 4-7x the body, one smooth
octave, a flash disc in a halo, additive to white, which reads as a lamp. Written
2026-11-07 from web sources. Statements are **[sourced]** with a URL, **[sourced,
listing]** when the host was unreachable from here and only the search listing's text was
read (the Sources section names them), or **[inferred]** (game or film observation, or
reasoning). Published durations and sizes are rare; a quoted number is marked, every
other number is ours.

## 1. Games

**X4: Foundations (Egosoft).** 7.00 lists "improved explosion effects", 7.10 "improved
performance of explosion effects", and an earlier update "added some fire explosion
effects to the capital and station explosions" ([SteamDB](https://steamdb.info/patchnotes/15302949))
[sourced, listing]. Players are split between the vanilla look ("the new red one", "the
antimatter explosion style") and the X-Rebirth "burning hulk effect"; the mod restoring
the latter adds "burning and smoking wreckages and some fusion reactor explosions and
sparks", burning modules, randomised variants, and a user asked for a "critical explosion"
that does "the large blue explosion the instant it dies" ([Egosoft forum](https://forum.egosoft.com/viewtopic.php?p=4928343))
[sourced]. A capital death destroys the crates it dropped ([Steam](https://steamcommunity.com/app/392160/discussions/0/592891262117391391))
[sourced]; the computer warns "danger, explosion imminent" ([Steam](https://steamcommunity.com/app/392160/discussions/0/2283834583171933635))
[sourced, listing]. **[inferred]** (game observation): a fighter death is one orange burst
of about two ship lengths with a white core, a few sparks and a half-second dark puff; a
capital keeps its hull drifting for 5-10 s while module-sized fireballs pop along it at an
accelerating rate, then one large blue-white sphere with a thin ring swallows it and wreck
sections drift apart. As with its bolts, X4 is the floor: the two-phase structure is
right, the final body is a smooth sphere.

**Star Citizen (StarEngine).** The explosion assets are FumeFX simulations rendered to
flipbooks with optical-flow frame blending, lit from five directions for a diffuse and a
normal map, with a temperature map "coloured by a 32-bit gradient" so swapping the
gradient changes "the look of the overall effect from fire to plasma"; one 32-frame source
is stretched over 30x by the flow map; "you pay with shader complexity and texture memory"
([80.lv](https://80.lv/articles/vfx-of-star-citizen-working-on-explosions/)) [sourced].
The artist adds that smoke is composited over fire in the particle shader, the fire pass
is greyscale mapped to a black-body gradient, and the one requirement is "enough noise
and detail. Fire can bloom out texture detail" ([realtimevfx](https://realtimevfx.com/t/star-citizen-explosion-vfx-breakdown/632))
[sourced]. The Gamescom 2017 capital destruction was led by Michal Piatek over a part
hierarchy set up by tech art ([realtimevfx](https://realtimevfx.com/t/star-citizen-capital-ship-destruction-vfx/4183))
[sourced]. The explosions lead says the effects are "motivated through the components on
the ship": electrical feedback, small explosions, "then a wait, and then a big blast"
([PC Gamer](https://www.pcgamer.com/star-citizen-video-shows-spaceships-exploding-spectacularly/))
[sourced, listing]; the Idris video works from a mood board and "a timeline of the chain
of events through the ship" ([MassivelyOP](https://massivelyop.com/?p=294082)) [sourced,
listing]. When a part detaches "Squibs and Particles detonate along the edge of the
separation", revealing "the 'guts' of the ship", with "dramatic smoke, explosions and
sparks" and "space littered with the debris" ([starcitizen.tools](https://starcitizen.tools/Comm-Link:Design_Notes_-_New_Damage_System))
[sourced]. **[inferred]** (game observation): a fighter death is a white flash, a
fireball of 2-3 ship lengths with visible flow turbulence, a shell of sparks and tumbling
panels, and dark smoke lit by the embers for a few seconds; the capital sequence is
20-40 s of arcing, venting and internal bursts before the hull splits and the final
flash. Structure before volume: the fireball reads from its internal motion and its
debris, not its size.

**Elite Dangerous (Cobra).** Players: "ships start to explode a bit everywhere to finally
get a big explosion"; "the explosions themselves are mostly fine but the debris field
could be chunkier"; any revamp should avoid "flat expanding rings like those in the Death
Star" ([Frontier forum](https://forums.frontier.co.uk/threads/can-we-get-a-revamp-on-the-explosions-effects.637464/latest))
[sourced, listing]. **[inferred]** (game observation): a small ship goes in about 2 s: a
blue-white flash, an orange-yellow fireball of about 1.5 ship lengths with a hard noise
silhouette, blue-white sparks and dark chunks (the canopy among them) keeping the ship's
velocity, no smoke; a large ship chains three to six sub-bursts over 3-5 s first. The
cleanest game case of "sparks and chunks carry the motion, the fireball carries the light".

**Homeworld 3 (Blackbird, UE4).** A tool "realistically simulates how a ship's hull breaks
apart under the enormous pressure of an explosion" and "models interior rooms and
machinery, then warps and crinkles them so they bulge ... as if an explosion inside the
ship had blasted all its innards into space"; capital damage sockets run "light ... heavy
... critical ... 'it was a pleasure working with you, Captain'" with darker smoke, then
fire, breach visuals and sparks; "the battlefield is strewn with accurately modeled
debris"; fighters leave an "ink-like trail" ([DTF translation](https://dtf.ru/games/1724401-oruzhie-i-uron-yunitov-v-homeworld-3))
[sourced]. A player of the earlier games asked whether the ship is "meant to disappear
once the explosions have fully engulfed / surrounded the model" and wanted "many more
pieces" ([Homeworld forum](https://forums.sorcererking.com/175307/my-feedback-regarding-ship-damage-destruction-effects))
[sourced, listing]. **[inferred]**: HW3 capital deaths are 6-10 s of escalating socket
fires, a bulging hull, a white flash, a short orange fireball of about one hull length and
a persistent wreck; the wreck is the point.

**Everspace 2 (Rockfish, UE5).** "Enemy ships blow apart in convincingly fiery explosions
– actual physics of the vacuum of space be damned", the blasts "billow outward in clouds"
and leave scrap ([PCGamesN](https://pcgamesn.com/everspace-2/diablo), [Invision](https://invisioncommunity.co.uk/?p=310919))
[sourced, listing]; no developer breakdown ([ArtStation](https://magazine.artstation.com/2023/12/rockfish-games-everspace-2-art-blast/))
[sourced, listing]. **[inferred]** (game observation): the arcade maximum: a 2-3
ship-length flipbook fireball with a strong white core, a fast thin shock ring, 20-40
sparks, a few large spinning chunks, a smoke puff lit orange from inside, a bloom that
pulses the screen. Closest to what the user calls modern, and pure flipbook; our version
must get the same read from noise.

**Battlestar Galactica Deadlock (Black Lab, Unity).** The dev diaries cover "wear and
tear" and persistent damage, not deaths ([dev diary 4](https://www6.slitherine.com/news/battlestar-galactica-deadlock-dev-diary-4-wear-tear13-02-18),
[dev diary 5](https://www6.slitherine.com/news/bsg-deadlock-dev-diary-5-the-cost-of-war26-03-18))
[sourced, listing]. **[inferred]** (game observation): the show at game cost: a capital
burns from several points, breaks into two or three pieces with a short white flash at
each break, fire licks the edges for a few seconds; no big fireball, no ring. The TV
structure (break-up, not a sphere) survives a small budget.

**Star Wars Squadrons / Battlefront II (Motive, DICE; Frostbite).** As a Star Destroyer
weakens "the smooth grey portions of its hull will turn black and catch on fire"
([GGRecon](https://www.ggrecon.com/guides/star-wars-squadrons-how-to-destroy-star-destroyer/))
[sourced, listing]; a former Motive artist lists "CR90 Corvette Destruction" concept art
([ArtStation](https://benredekop.artstation.com/projects/AqNQVy)) [sourced, listing]. The
Battlefront II large explosion "might look like 1 flipbook put on 1 particle" but is many
volumetric particles, each duplicated with a smoke and a fire texture, two flipbooks whose
six RGB channels hold six lightmaps (a reader: a 6-point normal map with occlusion),
coloured by a LUT, from one 12288x8192 px atlas ([Simonschreibt](https://simonschreibt.de/?p=9364))
[sourced]. **[inferred]** (game observation): a Squadrons fighter death is the film
pyrotechnic at game speed: white flash, fast orange fireball of two ship lengths with
sharp dark holes, a fan of straight sparks, no ring, about a second; a capital is 10-15 s
of hull fires, a flash and a slow fireball with large debris; the hull lit by its own fire
is the modern part.

**EVE Online (CCP, Ascension).** The rebuilt explosions combine "mesh based effects,
motionparticles/optical flow and regular particles" and flares; each hull draws from a
weighted pool; small blasts spread across the hull "somewhat exponentially faster" into a
large final blast with glowing areas near the core; the goal was "a more space-y look than
just regular earthlike explosions"; the set fits "190 unique hulls, from bus-sized ships
to titans about 14 km long"; "Love those shockwaves!" ([realtimevfx](https://realtimevfx.com/t/eve-online-ascension-new-explosions/1269))
[sourced]. CCP: "destroy a ship in multiple stages, including a final, large explosion"
([EVE forum archive](https://forums-archive.eveonline.com/topic/386132)) [sourced,
listing]; wrecks remain "pretty anti-climactic, especially when killing larger ships"
([EVE forums](https://forums.eveonline.com/t/unique-ship-wrecks-and-explosions/510951))
[sourced]. **[inferred]**: EVE's accelerating cadence is the best-documented chain law:
geometric, not a fixed interval.

**FreeSpace 2 Open (code).** The one complete published anatomy with ratios to the hull:
`$Death Roll Time` defaults to 3000 ms (minimum 2, player minimum 1000, cargo a quarter);
a big ship whose explosion damage is at least 250 adds `3000 + 4 x damage + 4 x radius` ms
so neighbours can evade; a propagating explosion pops a fireball of 0.1 R at a random
surface point every 333-500 ms; in the last 500-100 ms five pre-death fireballs of 0.4 R
go off with the blast; at death a fireball of 1.75 R is created and the hull becomes
debris at one fifth of that fireball's life; a large ship splits along a clip plane with
fireballs of 2.1-2.4x the cross-section radius, capped at R, every 650-900 ms; a
fireball's life is frames over fps ([ship.cpp](https://raw.githubusercontent.com/scp-fs2open/fs2open.github.com/master/code/ship/ship.cpp),
[shiphit.cpp](https://raw.githubusercontent.com/scp-fs2open/fs2open.github.com/master/code/ship/shiphit.cpp),
[shipfx.cpp](https://raw.githubusercontent.com/scp-fs2open/fs2open.github.com/master/code/ship/shipfx.cpp),
[fireballs.cpp](https://raw.githubusercontent.com/scp-fs2open/fs2open.github.com/master/code/fireball/fireballs.cpp))
[sourced]. The largest ships went "boom, sending out a massive blue halo" ([PC Gamer via PressReader](https://www.pressreader.com/usa/pc-gamer-us/20210518/281685437725724))
[sourced, listing]. **[inferred]**: 3 s roll, 0.1 R pops at 2-3 Hz, a 0.4 R pre-death
cluster, 1.75 R final, break-up early in the fireball: the baseline to check the 3.2
table against.

**Gemini Wars, Halo 3, and the "too small" record.** Gemini Wars: "several explosions are
set off in key parts of the ship, like a chain of explosions. Then, just before the final
explosion the ship is broken into massive fragments" that "fade out nicely while moving"
([Camel 101](https://camel101.com/?p=2011)) [sourced]. Halo 3 built nearly every explosion
by scrolling layered sections of a tiling texture ("we scale and animate the UVs against a
tiling plate"), judged "not as successful at giving a cohesive sense of volume to a
fireball"; the Scarab death layers fireballs, light beams, smoke, then sparks and debris,
"the key thing here - lots and lots of sparks", 2 % of a 300-spark cloud colliding; a
256-effect cap and stacked transparency cut the lingering smoke ([Game Developer](https://www.gamedeveloper.com/game-platforms/gamefest-how-i-halo-3-i-s-lots-and-lots-of-sparks-got-made))
[sourced]. Gratuitous Space Battles' players asked for larger cruiser explosions
([Positech forum](https://forums.positech.co.uk/t/suggestion-larger-explosions-for-cruisers/3041))
and an X-Wing remake tracks "capital ships: when destroyed, the explosion itself is too
small" as a bug ([XWVM issue 196](https://bitbucket.org/xwvmteam/xwvm-lfs/issues/196/capital-ships-when-destroyed-the-explosion))
[sourced, listing]. **[inferred]**: three projects record the complaint the user made of
every first flown effect.

## 2. Films and TV

**Star Wars (1977) and the pyrotechnic tradition.** Joe Viskocil "insisted on almost every
explosion being shot straight up from below" so "the circumference of the explosion would
fill the entire frame, instead of dropping off to one side"; he demanded "some sort of
continuity, some sort of design to the explosions themselves" (TIE fighters died in green
flame); "no one has actually seen a space station or ship blown to smithereens"; the 1997
Special Edition added a ring shockwave ([Inverse](https://nc.inverse.com/entertainment/sci-fi-minature-explosions-star-wars-independence-day))
[sourced]. The camera ran "as fast as it would go" and titanium chips "that ignited
secondarily became one of the main ingredients" ([Zacuto](https://zacuto.com/blogs/blog/how-to-blow-up-the-death-star))
[sourced, listing]; larger elements were overlaid on smaller ones "to help give more
punch" (`impact-effect-references.md`). Independence Day's 5 x 14 ft White House carried
"30 to 40 explosive charges at key points", was filmed by nine cameras "at various speeds"
and played at normal speed, which "made the blast look larger and slower", with dollhouse
furniture inside so the debris read as objects ([DGA Quarterly](https://www.dga.org/craft/dgaq/issues/1404-fall-2014/shot-to-remember-independence-day))
[sourced]. **[inferred]** (film observation): the canonical ship death is a white flash of
one or two frames, a fireball of 2-4 ship lengths that expands fast and then hangs, dense
ragged silhouette, white-yellow interior, dark red rim, a shell of sparks and titanium
streaks flying straight, about 1.5 s at film speed; slow playback is what makes it read
as large, a timing rule we can copy: a big thing grows slowly.

**The Praxis ring.** First in Star Trek VI (1991) by ILM's CG division, scaled against a
wave hitting a liner; it "became a common feature in science fiction films"; Philip Plait:
"in reality a spherical wave would be more likely" ([Wikipedia](https://en.wikipedia.org/wiki/Praxis_effect))
[sourced]; Elite's players call the flat ring "something stupid" [sourced, listing, above];
EVE's viewers loved its shockwaves [sourced, above]. **[inferred]**: the flat disc reads
dated; the 3.2 thin limb-bright sphere shell is the modern form, kept on the two big
tiers only.

**Rogue One, The Last Jedi.** John Knoll wanted "to see inside the ships as they ripped
apart"; "normally such detail is lost in a vast explosion, but Knoll suggested not losing
the action in a fireball"; one continuous 30-40 s animation ([fxguide](https://www.fxguide.com/?p=189029))
[sourced]. The bomber was built "from the inside out ... with destruction in mind so that
during the explosion it would reveal the interior details"; the dreadnought explosion
"looks as real as an explosion in space could look"; Holdo's jump is "No sound. No
crashing explosion", "particulates slicing through the ships" in seven slow-motion shots;
Houdini rigid bodies and Clarisse debris ([CGW](https://www.cgw.com/Press-Center/In-Focus/2018/Whats-Bold-is-New-Again-Star-Wars.aspx),
[SlashFilm](https://www.slashfilm.com/556449/star-wars-the-last-jedi-visual-effects-featurette-ilm/))
[sourced]. **[inferred]**: the modern Star Wars capital death shows the ship, not the
fireball. We cannot split the X3 hull (the node dies), so the 3.2 secondaries inside the
hull box and lit fragment streaks are our stand-in for "seeing inside".

**Battlestar Galactica (2004).** Smaller ships "swarm over a new Cylon ship and literally
tear it apart"; explosion luminance is "piped" into Sapphire's Rack Defocus, which "blows
out the brightness and enlarges the explosions"; a "massive haze/smoke layer" is lit using
the explosion light as a matte; Feedback Bubble stretches the smoke for "a longer-lasting,
smoky fade-out look"; burning ships isolated by "a faint scattering of standing smoke"
looked larger ([CGW 2006](https://www.cgw.com/Publications/CGW/2006/Volume-29-Issue-5-May-2006-/Half-the-Battle.aspx))
[sourced]; fire is LightWave particles plus compositing ([Post Magazine](https://www.postmagazine.com/Publications/Post-Magazine/2008/November-1-2008/VISUAL-EFFECTS-BATTLESTAR-GALACTICA.aspx))
[sourced]; Zoic's nuclear ship kill was a 2006 feature ([Studio Daily](https://www.studiodaily.com/2006/07/whata%C2%A2aeas-my-flow-galactic-explosions/))
[sourced, listing]. **[inferred]** (episode observation): a Raider death is one hard
white flash and a short orange-white puff of one ship length with sparks and fragments,
under a second; a basestar is 10-20 s of internal bursts lighting the hull from inside,
sections shearing off, a final white flash that blooms the lens, then cooling fire on the
fragments; nothing billows. BSG's glow is a defocus of the bright pixels, i.e. the body's
own luminance; a separate larger glow primitive is the lamp.

**The Expanse.** The Canterbury "needed to demonstrate 'correct physics'"; the 1950s space
nuclear tests were the design basis, "combining the look of what you think of as a nuclear
explosion and the look of a nebula", in Houdini ([Post Magazine via PressReader](https://www.pressreader.com/australia/3d-world/20210223/283162906302874))
[sourced, listing]. Physics gives "a brief, intensely bright sphere of light", "no rolling
fireball, no churning black smoke and nothing that 'billows'", and "an expanding shell of
glowing droplets and shrapnel" that never slows ([ScienceABC](https://www.scienceabc.com/nature/universe/can-spaceships-actually-explode-like-they-do-in-movies.html))
[sourced]. Starfish Prime: "a brilliant white flash" became "an expanding green ball of
irradiance", white streamers, rings that "appeared frozen in place", a red glow; about
45 s for the first sequence, most effects gone by about 400 s ([Wikipedia](https://en.wikipedia.org/wiki/Starfish_Prime))
[sourced]. An air burst shows a double flash because "the dense shock front obscures the
fireball" ([Wikipedia](https://en.wikipedia.org/wiki/Effects_of_nuclear_explosions))
[sourced]. **[inferred]** (episode observation): a flash, a sphere of glowing droplets
expanding at constant speed and cooling white → orange → dark over 3-5 s, a gas shell gone
in a second, no fire. The ember shell is the most modern single element and it is
closed-form.

**Other.** Star Trek III used "a much smaller model" for the Enterprise blast ([WhatCulture](https://whatculture.com/film/20-things-you-didnt-know-about-star-trek-iii-the-search-for-spock-1984?page=2))
[sourced, listing]; The Orville styled a Krill death "in green flames instead of the usual
orange fire" ([Space.com](https://space.com/inside-the-orville-spectacular-space-battle.html))
[sourced, listing]; Babylon 5 was all LightWave CG ([Lurker's Guide](https://www.lysator.liu.se/lurk/www.midwinter.com/lurk/making/effects.html))
[sourced]. **[inferred]**: B5's smooth orange sphere with a flat ring is the "balloon"
the 3.2 kill criterion names.

**What reads as a modern ship death at a glance [inferred].** (1) Two phases for anything
above a fighter: internal bursts that light the hull from inside and accelerate, then one
final body. (2) A flash tiny in area and gone in two or three frames. (3) A fireball with
a ragged silhouette and a moving interior (flow, erosion, holes opening), hot core, dark
rim, fast rise then hang, dimmer than its own flash. (4) A shell of sparks, embers and
fragments that fly straight, inherit the hull's velocity, cool, and outlast the fireball.
(5) Light on the neighbours and the wreck. (6) Something left behind. (7) Scale told by
time: the big thing grows slowly and lasts long.

## 3. Techniques

- **Volumetric fireball impostors.** GPU Pro 6: volumetric explosions "look good from all
  view angles as they no longer depend on camera-facing billboards" and one body matches
  "thousands of particles without the cost of updating and sorting them"; the proxy is a
  hemisphere (NVIDIA: a cone capped with a hemisphere) shrunk to the current radius
  ([GPU Pro 6, Dunn](https://www.taylorfrancis.com/chapters/edit/10.1201/b18805-8/4-realistic-volumetric-explosions-games-alex-dunn),
  [NVIDIA PDF](https://developer.download.nvidia.com/assets/gamedev/docs/VolumetricExplosionEffects.pdf))
  [sourced, listing]; Perlin's density sphere is the ancestor (`impact-effect-references.md`
  3). A mesh variant: a cube-sphere displaced by 3-D value noise with a per-explosion
  random axis as seed, an ease-out expansion whose tail is cut "to prevent it coming to a
  full stop", an opaque material dissolved by `clip(0.15 + thickness - t)` against three
  octaves (weights 0.3, frequencies 2, 4, 6), swirl by rotating about three random axes
  with age, displaced peaks brightened as fake occlusion ([Quirkbit](https://quirkbit.itch.io/adrift-arena/devlog/122433/creating-stylised-explosions))
  [sourced]. For us: the 3.2 ray-sphere is the right primitive; against both references
  it lacks the erosion threshold (hull-burst-look 2.3), a low-frequency lobe term (2.4)
  and a directional shading term; the chord column alone is a balloon [inferred].
- **Six-way lightmap smoke, and why it matters without flipbooks.** Unity bakes "the
  lighting response of the smoke for light coming from six different directions" into two
  RGBA textures, emissive in a spare channel; "one downside to keep in mind is the flatness
  of this trick" ([Unity blog](https://unity.com/blog/engine-platform/realistic-smoke-with-6-way-lighting-in-vfx-graph))
  [sourced]; Battlefront II ships it [sourced, above]. The read it buys: "brighter on the
  top of the sprite and darker on the bottom"; a flat explosion has "a lot of high
  frequency detail, but lacks any low frequency detail", tested by blurring a frame
  ([realtimevfx 29528](https://realtimevfx.com/t/looking-for-feedback-on-explosion-vfx/29528))
  [sourced]. For us: a one-tap gradient of the density (or the lobe term's derivative)
  gives a normal; shade `0.6 + 0.4 n . l`, l toward the fireball's hottest point for the
  body and toward the sun for smoke; two slots per sample [inferred].
- **Flipbook vs procedural.** Star Citizen pays "shader complexity and texture memory"
  for flow-mapped flipbooks [sourced, above]; Halo 3's scrolled plates lost volume
  [sourced]; practitioners: flipbooks repeat ("the same fire flipbook scattered everywhere
  doesn't look realistic"), procedural "avoid[s] repetition", but procedural fire "never
  look[s] realistic enough" to some; a billboarded flipbook is fine until "flying around an
  extended explosion in a tight arc" ([realtimevfx fire tips](https://realtimevfx.com/t/looking-for-some-tips-for-creating-realistic-fire-effects-in-real-time-vfx/28143),
  [flight-sim thread](https://realtimevfx.com/t/explosions-for-a-flight-sim/29553))
  [sourced, listing; the flight-sim thread read]. For us the constraints decide; the
  generated noise atlas (hull-burst-look 2.8) replaces the octaves, not the erosion law,
  if the three-octave program misses its budget row [inferred].
- **The flash.** "The instant flash literally lasts about 2-3 frames"; the shockwave fires
  first "as background light for the flash"; layers staggered "by 2-3 number of frames"
  ([realtimevfx 1894](https://realtimevfx.com/t/stylized-frag-launcher-explosion-overwatch-inspiration-breakdown-posted/1894))
  [sourced]; "a large glow sprite is the wrong tool", use "a spiky muzzle-flash sprite for
  1-2 frames, plus a real light with a larger radius", desaturated; "an odd delay after the
  flash to when fireball comes out" kills it ([realtimevfx 29528](https://realtimevfx.com/t/looking-for-feedback-on-explosion-vfx/29528))
  [sourced]. For us: hull-burst-look 4's 40 ms star, no halo, arms 2-3x longer, the
  fireball born on the same frame [inferred].
- **Shock shell and refraction.** History and objection in section 2; FS2's linear law and
  the fast-then-slow rule in `impact-effect-references.md`; refraction is a masked UV
  offset into a colour copy ([GPU Gems 2 ch. 19](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-19-generic-refraction-simulation))
  [sourced], as in every hobby shader ([Godot](https://godotshaders.com/shader/distortion/))
  [sourced, listing]. For us: the 3.2 shell drawn as a second ray-sphere with density
  peaked at the limb (`exp(-((r - R)/w)^2)`), a sphere not a disc; refraction stays off
  [inferred].
- **Debris, embers and sparks.** "Lots and lots of sparks" [sourced]; a "chunkier" field
  [sourced, listing]; a droplet shell that never slows [sourced]; debris "ejected evenly
  from a sphere looks uniform", launch "clumps in cones with varied angles"; sparks "way
  too big and blobby"; children that "spawn in place and start falling" must inherit the
  parent velocity; "all the directionality should be gone within a few frames", the
  lingering part is a separate slow element ([realtimevfx 29528](https://realtimevfx.com/t/looking-for-feedback-on-explosion-vfx/29528),
  [4514](https://realtimevfx.com/t/explosion-feedback/4514)) [sourced]. For us: the 3.3
  streak (`p = p0 + v t`, length `|v| dt`, `exp(-t/tau)`, cooling colour) with v = hull
  velocity + a radial speed from a few seed cones, three lifetimes, and a dark thick
  variant with a hot edge as the fragment [inferred].
- **Chain detonations for capitals.** Star Citizen: small explosions, "a wait, and then a
  big blast" [sourced, listing]; EVE: "somewhat exponentially faster" micro-bursts
  [sourced]; Gemini Wars: fragments "just before the final explosion" [sourced]; FS2:
  0.1 R pops at 333-500 ms, five 0.4 R pops in the last 0.4 s, 1.75 R final, break-up at a
  fifth of its life [sourced]. The 3.2 secondaries come *after* the final body; the
  references put the chain before it. The native death cannot be delayed (the node dies at
  the site), but the record can carry a pre-roll: secondaries from t = 0 at a geometric
  cadence lighting the hull box, the final body born at its end [inferred].
- **Light on hulls.** Unreal particle lights cost "the number of lights times the pixels
  each one touches", so "a few large lights plus many small ones" ([Unreal docs](https://docs.unrealengine.com/4.27/RenderingAndGraphics/ParticleSystems/ParticleLights))
  [sourced, listing]; BSG lit its smoke by the explosion as a matte [sourced]. For us: the
  one glow primitive (weapon-look 2.8) on the hull depth, R 5 R_f, at a third of the
  body's peak and carrying its heat ramp (hull-burst-look 3), plus a small glow on the
  dying hull from t = 0 as the "burning before death" stage [inferred].
- **Exposure and bloom.** "Any luminosity value above 1.0 converge[s] towards white
  without tonemapping" ([realtimevfx](https://realtimevfx.com/t/vfx-graph-color-intensity-become-white-only/21679))
  [sourced, listing]; a bloom threshold of 1 so "only colors brighter than the display can
  produce glow" ([Catlike Coding](https://catlikecoding.com/unity/tutorials/custom-srp/hdr/))
  [sourced, listing]; auto-exposure has separate Speed Up / Speed Down and a Fixed mode
  ([Unity](https://docs.unity3d.com/Packages/com.unity.postprocessing@3.3/manual/Auto-Exposure.html))
  [sourced, listing], and Stingray's "Threshold Darks" excludes "visual effects that you
  don't want to be considered in light adaptation" ([Stingray help](https://help.autodesk.com/cloudhelp/ENU/Stingray-Help/stingray_help/lighting_rendering/shading_environment/adapt_scene_light_env.html))
  [sourced, listing]. For us: the 3.2 meter rule (≤ 0.5 EV, else body capped at E 2 and the
  small flash carries the brightness) is the Stingray threshold in our terms; the hot
  core sits in ≤ 15 % of the area at E 3-4 and the bulk at E 0.8-1.5 so the colour never
  clips to white [inferred].
- **Timing and scale.** "Faster rings would feel snappier", an effect can read "in slow
  motion", smoke "should probably live longer than the sparks", fireball lifetimes should
  be randomised [sourced, above]; "the fireball ... reads to me like a huge explosion, yet
  the overall size seems fairly small": size is the footprint, scale is behaviour at that
  footprint [sourced]; miniatures play slower the bigger they are meant to be [sourced];
  FS2 lengthens the roll with radius [sourced]. For us: the rise time is a fraction of a
  life that grows with the tier; the ember speed in m/s does not (big fragments are not
  faster, they fly longer) [inferred].

## 4. Recommended anatomy [inferred]

Units: R the object's bounding radius (3.2's R_obj), R_f = f x R the fireball's full
radius with the 3.2 clamps, L the tier life in game seconds; E linear radiance; every
duration scales with the game-time frame step. These start above the 3.2 table on purpose
(every flown first version was too small; three projects in section 1 record the same
complaint). Elements are the 3.2 set plus three additions marked (new): pre-roll
secondaries, fragment streaks, and the ember shell's constant-speed law.

Timeline (fractions of L, fighter values; the tier table rescales):

| Phase | Age | Element | Shape and law | Colour, radiance | Size |
| --- | --- | --- | --- | --- | --- |
| Flash | 0 to 40 ms (three-frame floor) | four-arm star, unequal arms, no halo (hull-burst-look 4) | linear fade | white, E 6 core | arms 0.4 R_f, core 0.08 R_f, capped at 0.25 H |
| Fireball rise | 0 to 0.15 L | ray-sphere impostor, erosion threshold rising with age and radius, lobed silhouette, two to three fbm octaves advected outward, gradient-shaded | R(t) = R_f (1 − e^(−t/0.15 L)), plus 0.1 R_f of slow growth over the rest of L | heat ramp T 1; core E 3-4 over ≤ 15 % of the area, bulk E 1.2 | to R_f |
| Shock | 0 to 0.3 L (corvette and up) | limb-bright sphere shell | r = 4 R_f (t/0.3 L)^0.5, alpha (1 − t/0.3 L)^2 | heat T 0.9, E 1.0 at the limb | shell width 0.06 r |
| Ember shell | 0 to 1.2 L | n streaks from 4-6 seed cones, constant radial speed v_e plus hull velocity, lifetimes 0.3 / 0.7 / 1.2 L | p = p0 + v t, length v dt, E e^(−t/τ) | white → orange → dark red by age | v_e = 1.5 R_f/s fighter; 60-120 m/s absolute for capital and station |
| Fragments (new) | 0 to 1.5 L | 5-10 % of the ember count, thick dark streaks with a hot leading edge, alpha not additive | 0.5 v_e, tumble by hashed phase | dark grey, edge T 0.6 | length 0.15 R_f, width 0.03 R_f |
| Hull glow | 0 to 0.3 L on neighbours, 0 to 0.5 L on the wreck sphere | surface light on the hull depth | E 1.5 → 0, carries the body's heat ramp | heat T 0.85 | R 5 R_f |
| Hang and cool | 0.15 L to 0.8 L | same impostor; holes open from 0.4 L, clumps go last | T 1 → 0.3, body E 1.2 → 0.3 | orange → dark red | R_f to 1.1 R_f |
| Smoke / afterglow | 0.5 L to 1.6 L (off in flight 1) | the impostor in the multiply pass, sun-side shaded | alpha up to 0.5 L then down | grey, warm inside while T > 0.2 | 1.1 R_f |
| Final fade | 0.8 L to L | body to zero, embers dark, glow gone | | | |

Per tier (floors for the first flown version, not ceilings):

| Tier | R_f = f x R, clamp (m) | L (game s) | Pre-roll secondaries (new) | Embers / fragments | Shock | Distinguishing read |
| --- | --- | --- | --- | --- | --- | --- |
| Fighter | 2.0, [12, 50] | 1.6 | none | 32 / 3 | none | one body that snaps to size; sparks carry the motion; gone under 2 s |
| Corvette / freighter | 1.5, [30, 160] | 2.4 | 2 bursts of 0.35 R_f at 0 and 0.15 L in the hull box, body born at 0.25 L | 64 / 6 | yes | two pops then one body; a short dark drift |
| Capital | 0.9, [80, 600] | 4.0 + 1.5 pre-roll | 6-8 bursts at a geometric cadence (0.6, 0.4, 0.25, 0.15 ... s) lighting the hull box from inside; the hull's own glow from t = 0; body born at the end of the pre-roll | 128 / 12 | yes, the largest | "wait, then big blast"; fragments with a hot edge outlast the body; neighbours light up |
| Station | 0.6, [200, 1,800] | 6.0 + 3 pre-roll | 12 bursts, same cadence, two of them 0.6 R_f | 160 / 16 | yes, slow (0.4 L) | the body rises over about a second and hangs; the view fills only with the eye inside |
| Missile | 3.5, [6, 25] | 0.8 | none | 12 / 0 | fast thin shell, 0.3 L | a flash and a puff; the shell is the read at distance |

Colour law: the 3.2 heat ramp, the core a lighter tint rather than clipped white, the rim
always darker than the interior so the silhouette reads against the glow; the glow is the
body's light (one third of its peak, its heat, its life). Secondaries use the same
impostor at 0.35 R_f at a third of the life. Everything is keyed by the death record's
seed: cone directions, lobe seed, noise offset, cadence jitter (±20 %).

Sizes and durations against the references: the final body is 1.75 R in FS2 [sourced],
2-4 ship lengths on film and in Everspace and Star Citizen for a fighter [inferred],
0.5-1 hull length for a capital in HW3 and BSG where the hull and its fragments are the
picture [inferred]; the roll is 3 s plus `4 x radius` ms for big ships in FS2 [sourced];
X4 capitals pre-roll 5-10 s and BSG capitals 10-20 s [inferred]; vanilla X3 rows end at
3.1 s / 9 s / 22 s [M, weapon-look 3.2]. The table's capital total (5.5 s) and station
total (9 s) sit between FS2 and X4 and under vanilla's 22 s; the user's "too small, too
short" history argues for erring long on the pre-roll, not on the body.

Budget: the pre-roll adds 6-12 small impostors over 1.5-3 s, each a 0.35 R_f sphere, well
under one close fireball's coverage; fragments are the ember program with another
profile; gradient shading is two slots per octave sample; the 3.2 octave LOD above 20 %
coverage and the shared budget (2.11) hold.

## 5. What to avoid (named failure modes)

- **The lamp.** Glow larger and brighter than the body, a flash disc in a halo, one
  smooth octave, additive to white (hull-burst-look 1, measured). Rule: glow ≤ 1.5 R_body
  and ≤ 1/3 of its peak, flash a star of ≤ 40 ms with no halo, body thresholded with two
  or more octaves, core radiance confined to a small area.
- **The balloon / soap bubble / beach ball.** "Its already round, which is a bit
  unnatural"; "it looks like a simple scaling motion" [sourced]; a Fresnel-rim sphere is
  the bubble; Babylon 5's smooth spheres [inferred]. Rule: lobed silhouette, erosion from
  the rim, holes mid-life, a dark rim, expansion that never fully stops, gradient shading.
- **The flat disc.** A camera-facing noise quad without a depth cue reads as a decal; the
  chord column and the soft cut through the hull plane (3.2 occlusion) make it a volume
  [inferred].
- **The Praxis disc.** A flat ring: "something stupid" [sourced, listing], Plait's
  objection [sourced]. Rule: a limb-bright sphere shell, two big tiers only, never the
  brightest element.
- **Uniform radial debris.** "Ejected evenly from a sphere looks uniform"; sparks "too big
  and blobby"; children spawning in place [sourced]. Rule: cones, three lifetimes, hull
  velocity inherited, thin streaks, tumbling fragments.
- **The delayed fireball.** "An odd delay after the flash to when fireball comes out"
  [sourced]. Rule: flash and body on the same frame; the pre-roll precedes the flash.
- **Slow motion at fighter scale, snap at station scale.** A slow ring reads as "slow
  motion", a uniform expansion as "mechanical" [sourced]; film plays the big ones slow
  [sourced]. Rule: rise time grows with the tier, ember speed does not.
- **The sun.** Twenty deaths or one station that pump the exposure; "above 1.0 converge[s]
  towards white without tonemapping" [sourced, listing]. Rule: the 3.2 meter gate and the
  E 2 body cap; the cluster scaling of hull-burst-look 3 applied to death glows.
- **Hiding the ship.** "Normally such detail is lost in a vast explosion" [sourced]. Rule:
  the capital body is 0.9 R, not engulfing; secondaries and fragments show where the hull
  was.
- **Nothing left.** Every reference leaves a wreck, a debris field or a cooling shell;
  EVE's and Homeworld's players complain when it is generic or absent [sourced]. Rule:
  embers and fragments outlast the body by 0.2-0.5 L; smoke is the flight-3 option.
- **The X4 floor.** A single smooth blue-white sphere with a ring after a long pre-roll
  [inferred]. Copy the pre-roll, not the body.

Open gaps: no publisher gives a death-explosion size or duration for X4, Elite, Star
Citizen, Everspace 2, Homeworld 3, Deadlock or Squadrons; the FS2 numbers are code
defaults, not what the MediaVPs ship; the Star Citizen chain timeline is press
paraphrase; the Expanse and BSG rules are episode observation plus two trade articles;
the GPU Pro 6 and NVIDIA volumetric chapters were read only as listings; section 4 is
entirely inference.

## Sources

Every URL is inline above. Fetched and read: Egosoft forum (burning wreckages, FireFX),
Steam discussion (K crates), 80.lv (Star Citizen explosions, UE4 explosion), realtimevfx
(Star Citizen breakdown, Star Citizen capital destruction, EVE Ascension, feedback 29528,
feedback 4514, flight-sim explosions, frag breakdown), starcitizen.tools, DTF (Homeworld
3), EVE forums (unique wrecks), fs2open raw sources (ship.cpp, shiphit.cpp, shipfx.cpp,
fireballs.cpp), Camel 101, Game Developer (Halo 3), Simonschreibt, Inverse via its nc
mirror, DGA Quarterly, Wikipedia (Praxis effect, Starfish Prime, Effects of nuclear
explosions, Joe Viskocil), fxguide, CGW (2006, 2018), Post Magazine 2008, SlashFilm,
ScienceABC, Unity blog (6-way lighting), Quirkbit, Lurker's Guide. Listing text only
(host unreachable, 403, 503, login wall or truncated): SteamDB, Nexus, Steam discussions
(explosion imminent), PC Gamer (Star Citizen, Homeworld 3, FreeSpace via PressReader),
MassivelyOP, GamingTrend, Bleeding Cool (no destruction text), Frontier forums,
GamesRadar, Homeworld forum (sorcererking), PCGamesN, Invision, ArtStation (Rockfish,
Redekop), Slitherine, Gamereactor, Gizmodo Japan, GGRecon, EVE forum archive, Positech,
Bitbucket, Zacuto, StarWars.com, WhatCulture, Space.com, Studio Daily, PressReader /
Post Magazine / Digital Production / VFX Voice (Expanse), Taylor & Francis (GPU Pro 6),
NVIDIA PDF (binary only), GitHub sample (README only), Godot shaders, Unreal docs, Unity
docs, Catlike Coding, Stingray help, realtimevfx (HDR emission, fire tips), TV Tropes,
ModDB, ILM.com.
