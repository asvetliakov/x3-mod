# Bolts, tracers, orbs, flak and muzzle flashes: references for the bolt pass

Reference note for step 4 of the weapon-effects pass (`docs/archive/effects-look-2026-11-08/weapon-look.md`
section 3.3). Sibling of `impact-effect-references.md`; same marking rules. Scope: how
modern space games and films draw an energy bolt, a ballistic tracer, a plasma orb, a flak
shell and the muzzle event that launches them, and which techniques fit a closed-form
additive renderer (analytic capsules, discs, polylines and noise impostors, per-pixel
programs, FP16 additive over the scene, every element a function of age and velocity, no
particle state, a per-frame budget). Written 2026-11-07 from web sources. Statements are
**[sourced]** with a URL, **[sourced, listing]** when the host was unreachable from here
and only the search listing's text was read (the Sources section names them), or
**[inferred]** (game or film observation, or reasoning). Published durations and sizes
are rare; a quoted number is marked, every other number is ours.

## 1. Games

**X4: Foundations (Egosoft).** A bullet macro's `<effects>` block references separate
launch (muzzle) and impact effect definitions
([Egosoft forum tutorial](https://forum.egosoft.com/viewtopic.php?p=4786336)) [sourced].
Projectiles cannot carry a trail: particles added to a weapon's component move with the
projectile as a cloud rather than streaming behind it, and modders asked Egosoft for
trail emitters on projectiles
([Egosoft forum](https://forum.egosoft.com/viewtopic.php?p=4909291)) [sourced]. Players
describe the vanilla projectiles as "whitish/greyish", impact effects as rarely visible and
the effects as lagging behind the guns; the requested palette is orange pulse, red laser,
blue plasma, purple torpedo, yellow missile, green flak, with brightness rising from Mk1 to
Mk3 ([Egosoft forum, Colorful Weapons FX](https://forum.egosoft.com/viewtopic.php?p=4857432))
[sourced]. A forum explanation (not official) is that Egosoft toned projectile effects
down so large fights stay manageable
([Egosoft forum, Tracer World](https://forum.egosoft.com/viewtopic.php?p=4775603))
[sourced]. A visibility mod extends the weapon light range 4.5x and raises bullet
brightness ([Nexus mod 2145](https://www.nexusmods.com/x4foundations/mods/2145))
[sourced, listing]. The plasma cannon fires "plasma balls" at 1229 m/s (S) and 983 m/s (M),
called "very slow" ([Egosoft wiki](https://wiki.egosoft.com/X4%20Foundations%20Wiki/Manual%20and%20Guides/Objects%20in%20the%20Game%20Universe/Equipment/Ship%20Weapons/BeamGun%20Forward%20Weapons/Plasma%20Cannon))
[sourced]. **[inferred]** (game observation): X4 bolts are short pale capsules about 2-4 m
long with a faint tinted halo and a weak point light; the muzzle is a small disc flash of
one or two frames; bolt streams from an M-class gun read as a dotted line, not a stream.
X4 is the series' own answer and it is the look the user calls a downgrade: it is the
floor, not the target.

**Star Citizen (CryEngine/StarEngine).** Weapon classes are ballistic (solid projectiles,
limited ammo, bypass shields), energy (lasers, repeaters, cannons), distortion, scattergun,
gatling, mass driver ([RSI Vehicle Weapons Guide](https://api.star-citizen.wiki/comm-links/20032),
[starcitizen.tools](https://starcitizen.tools/Ship_weapons)) [sourced]. The design note on
damage wants damage types told apart by eye, lasers glowing the hull and ballistics tearing
holes ([RSI design notes](https://api.star-citizen.wiki/comm-links/14568)) [sourced]. The
GDC 2015 talk covers the particle and damage system
([GDC Vault](https://gdcvault.com/play/1021768/Advanced-Visual-Effects-With-DirectX))
[sourced, listing]. **[inferred]** (game observation): SC energy bolts are long (roughly
8-15 w) saturated capsules with a white core that stays visible at 2 km, with a soft
elongated halo and a bright muzzle glow that lights the gun barrel; ballistic rounds are
thin yellow-white tracers with a faint smoke puff at the muzzle and a visible shell
ejection; repeaters at high rate read as a continuous dashed line because each bolt is
longer than the gap between bolts.

**Elite Dangerous (Cobra).** The multi-cannon is a small-calibre machine gun firing
kinetic rounds at 1600 m/s; the plasma accelerator fires a "ball of high-energy plasma",
slow, fixed-mount ([Elite wiki, Multi-cannon](https://elitedangerous-archive.fandom.com/wiki/Multi-Cannon),
[Elite wiki, Plasma Accelerator](https://elitedangerous.gamepedia.com/Plasma_Accelerator))
[sourced]. Premium weapon colours recolour the muzzle flash and impact as well as the bolt
([Frontier forum](https://forums.frontier.co.uk/threads/do-the-premium-weapon-colors-affect-cannons.374537))
[sourced, listing]. Early testers found the cannon muzzle effects "a tad over bright"
([Frontier forum](https://forums.frontier.co.uk/goto/post?id=224152)) [sourced, listing].
**[inferred]** (game observation): Elite's pulse laser bolt is a short fat capsule (about
3 w) with a hard colour and a halo only at the head; the multi-cannon draws every round
as a thin white-yellow tracer with a very short tail, so a four-barrel stream reads as a
sparse dotted line; the plasma accelerator orb is a violet-white sphere with a trailing
wisp, roughly the size of a fighter cockpit, and a large white muzzle bloom that lights the
whole nose for about 0.2 s; the railgun is a thin white streak with a bright muzzle flare.

**Everspace 1/2 (UE4, UE5 Niagara since 2024)** ([Game Developer](https://gamedeveloper.com/business/rockfish-to-future-proof-everspace-2-with-unreal-engine-5-upgrade))
[sourced]. No developer breakdown of the weapon VFX was found; reviews list gatlings,
flak cannons "like shotguns", lasers and plasma guns, each with distinct sound
([TheXboxHub](https://www.thexboxhub.com/everspace-2-review/),
[Playtested](https://playtested.pages.dev/article/2023-04-11-one-of-the-best-space-combat-arcade-games-you-can-play-today-full-review-everspace-2))
[sourced]. **[inferred]** (game observation): Everspace 2 bolts are the arcade maximum:
long saturated capsules (10-20 w) with a wide additive halo, a short ribbon trail on
plasma and a bright muzzle flare that pulses the cockpit; gatling tracers are dense yellow
streaks with a fading tail; flak is a dark shell with a small orange glow that bursts into
a ring plus sparks; strongly bloomed.

**Homeworld 3 (Blackbird, UE4).** Four damage types, kinetic, high explosive, energy and
ion, each with "its own visual treatment"; the blog describes impacts and hull damage, not
the projectile in flight ([DTF translation](https://dtf.ru/games/1724401-oruzhie-i-uron-yunitov-v-homeworld-3),
[Bleeding Cool](https://bleedingcool.com/games/homeworld-3-releases-new-blog-featuring-battle-details))
[sourced]. **[inferred]** (game observation): kinetic fire is streams of thin yellow-white
tracers with a long faint tail, read at fleet scale as lines; ion is a blue beam; energy
bolts are short orange-red capsules; fighters show a tiny white muzzle twinkle.

**Freelancer (Digital Anvil, 2003).** The still-useful table-driven anatomy: a `BeamBolt`
has `tip_length`, `tail_length`, `core_length`, `head_width`, `core_width`,
`sec_core_width`, separate tip, core, outer, secondary-core and tail colours,
`head_brightness`, `trail_brightness`, `head_texture` (ball or star), `trail_texture` (wide
or thin) and `flash_size` for the creation flash
([Starport wiki, effects](https://the-starport.com/wiki/ini-editing/typed-inis/effects))
[sourced]. The projectile colour is a `BasicApp_Color` entry in the `.ale`
([Starport forum](https://the-starport.com/forums/post/34880)) [sourced]. Known weak
points: a long bolt effect shows nothing at short range, and an effect lifetime shorter
than the projectile range deletes the beam abruptly
([DiscoveryGC](https://discoverygc.com/forums/showthread.php?tid=64379),
[DiscoveryGC](https://discoverygc.com/forums/showthread.php?tid=135907)) [sourced].
**[inferred]**: the Freelancer decomposition (tip, core, head, tail, outer glow, creation
flash) is the closest published ancestor of our capsule + halo + tail + muzzle disc.

**Star Wars Squadrons / Battlefront (Motive, DICE; Frostbite).** EA's VFX note states the
rule that gameplay VFX exist to communicate mechanics and give feedback
([EA community transmission](https://answers.ea.com/t5/Announcements/Community-Transmission-Visual-Effects-in-Star-Wars-Battlefront/td-p/9036396))
[sourced, listing]; a developer said the 2015 bolts were meant to match the Rogue One VFX
([EA forums](https://forums.ea.com/discussions/star-wars-games-discussion-en/plasburst-vfx-issues/10642870/replies/10642871))
[sourced, listing]. A real-time Star Wars project's rule, from a VES-award write-up: laser
bolts must not look too saturated or luminous whatever the environment
([VFX Voice](https://vfxvoice.com/?p=13146768)) [sourced, listing; project not confirmed].
Squadrons has overcharged lasers and a channelled beam mod
([PC Gamer](https://pcgamer.com/drifting-engine-boosts-and-overcharged-lasers-how-combat-works-in-star-wars-squadrons))
[sourced, listing]. **[inferred]** (game observation): Squadrons bolts are the film capsule
at game speed: about 6-10 m long for a fighter cannon, a pale core inside a saturated red
or green edge, a soft halo of about 2 w, no tail, no bloom halo beyond the capsule, lit
on the hull as a coloured point light; the muzzle is a bright disc of the bolt colour for
one or two frames; incoming bolts end-on are a bright dot that grows to a disc.

**Halo (Bungie).** Covenant shots are "colored, light-up energy bolts" chosen because they
look more impressive coming towards the player, and bright visible incoming fire is a
readability rule: near-invisible shots would make every death feel like a drag
([Game Developer, Universal Truth 3](https://www.gamedeveloper.com/design/universal-truth-number-three-pt-3-))
[sourced]. The plasma rifle bolt is blue (the repeater's purple), muzzle velocity 126 m/s,
360-540 rpm, and the bolt loses velocity and cools with distance; Halo 3 gave it "a more
vivid look" ([Halopedia](https://www.halopedia.org/Okarda%27phaa-pattern_plasma_rifle))
[sourced]. **[inferred]** (game observation): the plasma bolt is a glowing teardrop about
0.3 m across with a white-blue core and a short comet tail, slow enough to see for its whole
flight; the human assault rifle shows a yellow muzzle star for one frame and sparse
tracers at roughly one round in four; that pairing (slow fat orb vs fast thin tracer) is
the clearest energy/ballistic contrast in games.

**Destiny 2, Mass Effect.** Destiny's particle systems are node graphs compiled to HLSL
([80.lv on the SIGGRAPH talk](https://80.lv/articles/siggraph-the-destiny-particle-architecture/))
[sourced] and a Bungie artist tunes the muzzle flash per barrel
([ArtStation](https://s3pirion.artstation.com/projects/Om2ldb)) [sourced, listing]; nothing
on Mass Effect's weapon VFX was found. **[inferred]** (game observation): both draw every
round as a white-yellow tracer with a short additive tail and a per-weapon muzzle shape
(star for kinetic, disc plus sparks for energy), and the first-person weapon is lit by
its own flash; ME's Reaper and Collector weapons are orange orbs with heat shimmer.

**EVE Online (CCP).** Turret fire has three types each with two subtypes, and ammo changes
the tracer colour ([EVE University, Identifying Visual Effects](https://wiki.eveuniversity.org/Identifying_Visual_Effects))
[sourced, listing]. Players called hybrid and projectile rounds "an ill-defined blurry round"
(2012), "barely visible at a distance" (2017) and turrets "pea shooters" next to a
battleship; one asked for a continuous autocannon tracer stream and a railgun smoke trail
([EVE forums archive](https://forums-archive.eveonline.com/message/758439),
[EVE forums](https://forums.eveonline.com/t/new-turret-firing-effects-feedback-issues-opinions/19428),
[EVE forums](https://forums.eveonline.com/t/make-weapon-effects-more-visible-and-awesomer/67672))
[sourced]. **[inferred]**: EVE is the cautionary case for distance: a bolt sized to the gun
vanishes at fleet range; the bolt must be sized to the screen.

**Rebel Galaxy Outlaw (Double Damage).** Look: "the punchy fireballs-and-oil aesthetic of
Mad Max"; beams were cut because they lacked "the kick of projectiles"; Red Devil pirates
use tracers and autocannons for a punchy feel
([Game Developer](https://gamedeveloper.com/design/designing-the-satisfying-space-combat-of-i-rebel-galaxy-outlaw-i-))
[sourced]. **[inferred]**: fat short orange bolts with a big soft halo and a long muzzle
plume; the muzzle is the feedback, not the bolt. **Starsector** (table contrast): a gun
projectile has `fringeColor` and `coreColor`, length 70 and width 11 px (about 6:1),
`fadeTime` 0.25 s; explosions add a white additive core over `explosionColor`
([Starsector wiki, .proj](https://starsector.wiki.gg/wiki/File_overview:_proj)) [sourced].

## 2. Films and TV

**Star Wars.** The bolts were hand animation and rotoscope; Adam Beckett led the team from
1975 and Peter Kuran made "the first perspective beams" after the first tests had no
perspective ([ILM](https://www.ilm.com/?p=40510)) [sourced]. Blanks fired on set produced
the muzzle flash and the bolt was added in post; Rebels and Empire fire red, TIEs green,
reversed in the prequels; MythBusters measured 130-135 mph from footage
([Wikipedia, Blaster](https://en.wikipedia.org/wiki/Blaster_(Star_Wars))) [sourced]. A
frame study got 15-35 m/s for ground shots and about 1,500 m in 0.08 s for a space shot
(the published 1.8e5 m/s figure does not reconcile with that) over 91 shots
([Wired analysis](https://wirednews.pages.dev/2012/05/star-wars-blaster-speed)) [sourced].
The fan reconstruction that matches the frames: a hand-weapon bolt is "a long and narrow
cylinder with tapered ends and a glowing texture" with no solid white core, only a lighter
shade of the base colour that reads almost white in close shots; spaceship bolts are
"much longer", more rounded and brighter at distance; a bolt is on screen about 14 frames
([theforce.net part 1](https://www.theforce.net/fanfilms/software/animaster/lasers/part1.asp),
[part 3](https://www.theforce.net/fanfilms/software/animaster/lasers/part3.asp)) [sourced].
Bolts "varied from shot to shot" in the original trilogy and ILM later wrote a style book
([fxguide](https://www.fxguide.com/fxpodcasts/john-knoll-and-the-handmade-star-wars-of-the-mandalorian-and-grogu/))
[sourced]. **[inferred]** (film observation): the canonical ship bolt is 15-25 bolt widths
long, about 1 m wide for a fighter cannon, a pale core of the tint (not white) with a
saturated edge and almost no halo, a short soft taper at both ends, flying a few ship
lengths per second so the whole capsule stays visible; it reads as a bolt because its
length, not its brightness, carries the direction.

**The Mandalorian.** Image Engine animated each bolt as a 3D object used as a mesh light on
the environment and called the effect "deceptively hard", starting with speed
([Image Engine case study](https://image-engine.com/case-studies/the-mandalorian-season-1/))
[sourced, listing]; Pixomondo created rifle bolts and tuned the timing in the edit
([AWN](https://www.awn.com/news/watch-pixomondos-mandalorian-season-1-vfx-showreel))
[sourced, listing]. **[inferred]**: the modern Star Wars bolt is a light source first, a
sprite second; the hull glow is what sells it at close range.

**Battlestar Galactica (2004).** Zoic's battle shots had "thousands of tracers" composited
with Sapphire plug-ins ([AWN](https://www.awn.com/vfxworld/battlestar-galactica-re-deux))
[sourced]. The glow is red for Colonials and blue for Cylons, from interspersed tracer
ammunition, and flak walls are cast under 6 km from the ship
([Battlestar wiki](https://en.battlestarwiki.org/Weapons_in_the_Re-imagined_Series))
[sourced, listing]; fans note the absence of the 1978 series' solid energy bolt
([GateWorld forum](https://forum.gateworld.net/sci-fi-and-fantasy-television/battlestar-galactica/18060-projectile-weapons-vs-directed-energy-weapons))
[sourced]. **[inferred]** (episode observation): a Viper tracer is a short, thin streak with
a bright head and a tail of 5-10 heads, every round traced, a yellow-white muzzle flash
of one frame per barrel with a visible shell; the flak field is a carpet of white-orange
bursts, each a bright point that blooms into a 20-40 m puff with a dark core, dozens a
second, with the shell itself invisible.

**The Expanse.** The authors proposed extending the railgun barrel with current-carrying
plasma and the VFX supervisor adopted it: gas vents from the muzzle before the barrel
lights ([Space.com](https://www.space.com/31437-the-expanse-crazy-sci-fi-tech.html))
[sourced, listing]; PDCs show short thruster bursts from the rear of the mount in time
with firing to cancel recoil ([The Companion](https://www.thecompanion.app/the-expanse-cqb-space-battle/))
[sourced, listing]. Rocket Science VFX produced most shots
([RSVFX](https://www.rsvfx.com/?p=1750)) [sourced, listing]. **[inferred]** (episode
observation): PDC rounds are a dense stream of dim orange tracers, every round visible,
each a thin streak maybe 3-5 m long with no halo, a muzzle flash that is a small orange
cone per barrel; the railgun shot is a single white streak with a long bright muzzle event
(0.3-0.5 s of barrel glow and venting) and no visible projectile after the first frames.

**Starship Troopers.** Bug plasma bursts were Dynamation particles lit with Imageworks'
Birps, previsualised, up to 200 elements in a space shot; Tippett developed the plasma,
SPI animated it in space ([ASC](https://theasc.com/articles/needs-pics-starship-maneuvers),
[Wikipedia](https://en.wikipedia.org/wiki/Special_effects_of_Starship_Troopers)) [sourced].
**[inferred]**: a slow blue-white fireball with a turbulent tail and a glow larger than the
core, the archetype of a heavy plasma shot.

**Guardians of the Galaxy / Marvel.** MPC built Xandar (857 shots), Method the Nova command
centre, Framestore 620+ shots on Vol. 2 ([AWN](https://www.awn.com/print/news/mpc-details-contributions-marvel-s-guardians-galaxy),
[Framestore](https://www.framestore.com/gotgvol2)) [sourced]; no bolt breakdown found.
**[inferred]**: short bright pulses (3-5 w), yellow or blue, a lens-glow halo wider than
the bolt, heavy bloom, a strong flash on the firing ship, streaked by motion blur.

**What makes them read as a bolt at a glance [inferred].** Length along the velocity (a dot
is a star); a hot pale core inside a saturated edge (a flat colour is a UI line); contrast
against the background, so a halo or dark edge where it is bright; a muzzle event bigger
than the bolt that lights the shooter; in streams the bolt-length to gap ratio (longer
than the gap reads as a dashed beam, shorter as tracers); and several frames on screen
(the film bolt is visible about 14).

## 3. Techniques

- **Velocity-stretched billboard.** Project the world velocity into view space and
  extrude the quad's vertices along it in the vertex shader, no geometry shader
  ([gamedev.net](https://gamedev.net/forums/topic/696694-stretched-billboard-projected-particles/5378582/))
  [sourced]. The degenerate cases are velocity near zero and velocity along the view axis;
  draw the line as a screen-space region and shade by point-to-segment distance
  ([Prideout](https://prideout.net/blog/old/blog/index.html@p=61.html)) [sourced, listing].
  Gaia Sky guards with a screen-velocity threshold and compensates brightness because a
  stretched object otherwise looks brighter
  ([Gaia Sky motion trails](https://tonisagrista.com/blog/2025/motion-trails/))
  [sourced, listing]. Unity's stretched billboard scales from the pivot
  ([Unity issue tracker](https://issuetracker-mig.prd.it.unity3d.com/issues/stretched-billboard-particles-improperly-offset-from-emitter))
  [sourced, listing]; flutter_scene defines the stretch as extra length per unit speed
  ([flutter_scene](https://pub.dev/documentation/flutter_scene/latest/scene/ParticleEmitterComponent/velocityStretch.html))
  [sourced]. For us: the capsule is a world-space segment (p0, p1) drawn as a screen quad
  with a signed-distance profile, and the streak rule extends p0 to the previous screen
  position, with energy conserved along the stretch [inferred].
- **End-on bolts.** Locking a sprite's axis to the travel direction loses the head-on view
  ([realtimevfx, align heads](https://realtimevfx.com/t/how-to-align-projectile-heads-with-a-target-solved/6727))
  [sourced, listing]. A segment's screen projection collapses to a disc of width w when
  end-on, so a capsule drawn by distance-to-segment degrades to a disc with no special
  case; add a camera-facing halo so the disc keeps its footprint [inferred].
- **Ribbons and when they are overkill.** Hitscan weapons are usually a beam; projectiles
  use ribbon trails with spawn-per-unit
  ([realtimevfx, Fortnite tracer](https://realtimevfx.com/t/line-tracer-as-fortnite/7414))
  [sourced]; a fast projectile's trail starts rendering too far from the spawn and a
  velocity-driven system attached to a projectile needs its position reset each tick
  ([UE forum](https://forums.unrealengine.com/t/fast-moving-projectile-with-niagara-trail-starts-rendering-too-far-away-from-the-spawn-location/818481),
  [UE forum](https://forums.unrealengine.com/t/issue-with-velocity-for-bullet-trace-effect-in-niagara/631255))
  [sourced]. A straight-flying projectile is best served by one velocity-aligned quad
  ([realtimevfx, align heads](https://realtimevfx.com/t/how-to-align-projectile-heads-with-a-target-solved/6727))
  [sourced, listing]. X3 bullets fly straight, so the tail is analytic; a ribbon is only
  for curving missiles [inferred].
- **Tracer every Nth round.** Belts are loaded four-to-one (every fifth round); NATO tracers
  burn red (strontium), Russian and Chinese green (barium); most modern tracers have a
  delay element so the trace appears some distance from the muzzle; WWII aircraft guns
  were loaded four-to-one across the magazine
  ([Wikipedia, Tracer ammunition](https://en.wikipedia.org/wiki/Tracer_ammunition))
  [sourced]. Fiction mostly traces every round (the trope "Every Bullet Is a Tracer",
  [TV Tropes](https://tvtropes.org/pmwiki/pmwiki.php/Main/EveryBulletIsATracer))
  [sourced, listing]. For us: X3 bullets are objects, each gets a bolt; the convention is
  a dimmer "slug" style for untraced rounds and a bright "tracer" for every Nth, selected
  by the bullet's creation stamp, if a stream ever reads too dense [inferred].
- **Hot core, cool edge.** Blackbody runs deep red near 1000 K, yellow at 4500 K, white
  near 6500 K, pale blue at 8000-9000 K
  ([devbytes](https://devbytes.co.in/news/use-black-body-radiation-for-physically-accurate-fire-colors-in-vfx))
  [sourced]; a glow sprite is a white core with a smooth falloff whose colour comes from a
  gradient ([nightshade textures](https://docs.rs/crate/nightshade/0.47.0/source/src/ecs/vfx/textures.rs))
  [sourced]; a dark colour set additive disappears, so the core needs HDR-range values
  ([realtimevfx](https://realtimevfx.com/t/beginner-need-help-w-blazblue-vfx/26380))
  [sourced, listing]. Freelancer separates core, outer and tail colours; Starsector core and
  fringe; the film bolt's core is a lighter tint, not white. For us: `rgb(r) = lerp(tint,
  white, k * exp(-r^2 / sigma_c^2))` with k from the weapon energy, so a heavy weapon has a
  white core and an IRE a pale one [inferred].
- **Bloom vs a halo drawn by hand.** Our tonemap has no bloom, so the halo is explicit: a
  Gaussian of sigma 1.5 w at 0.3-1.5 linear (weapon-look 3.3). Premultiplied alpha keeps
  colour over bright backgrounds where additive washes out, and a darker base or outline
  under a bright element keeps it readable over both
  ([realtimevfx, colour tips](https://realtimevfx.com/t/diving-into-color-tips-tricks-i-use-for-using-color/16220))
  [sourced, listing]. Over a lit station hull an additive-only bolt loses its edge; a
  thin darkening ring (alpha, not additive) around the core restores it [inferred].
- **Minimum pixel width and the streak.** No published width rule was found; the
  practitioner check is greyscale value contrast
  ([skill guide](https://playbooks.com/skills/omer-metin/skills-for-antigravity/vfx-realtime))
  [sourced, listing], and EVE is the counter-example. The 3 px / 12 px floors of weapon-look
  3.3 hold, clamped before the halo. The previous-position streak is the game form of a
  shutter smear: energy-conserved it reads as motion, otherwise as a longer bolt; under
  SETA the simple-mode cap of two rest lengths is the fallback [inferred].
- **Muzzle flash, flipbook vs analytic.** Practitioners put a real flash on screen for 1-2
  frames, keep the light bright only on those frames and otherwise dim with a small radius,
  pick random flipbook segments for variety, spawn sparks (unburnt powder) with the flash
  and smoke just after
  ([realtimevfx, muzzle flashes](https://realtimevfx.com/t/wondering-how-good-muzzle-flashes-are-made/13540),
  [realtimevfx, flipbook offset](https://realtimevfx.com/t/flipbook-frame-offset-on-muzzle-flashes-for-greater-variations/28301))
  [sourced, listing]. Film guides give 50-150 ms visible and a strobe of flashes on
  intermittent frames for automatic fire
  ([mycreativefx](https://mycreativefx.com/blog/muzzle-flash-vfx-realistic-gunfire-effects-for-action-films-production-guide),
  [Maxon Bang](https://help.maxon.net/rg/en-us/Content/html/02-Bang_presets-transform-keyframing.html))
  [sourced, listing]. On the Star Wars set the blanks made the real flash
  ([Wikipedia](https://en.wikipedia.org/wiki/Blaster_(Star_Wars))) [sourced]. For us,
  analytic beats flipbook: a disc (core), a forward cone (flame, length along the gun
  axis), a 4-6 point star (ballistic, rotated per shot by the creation stamp) and a
  camera-facing noise puff (smoke, 0.3 s, alpha not additive); variety from a per-shot
  seed, not frames; the life stays at 120 ms because X3 runs 3-90 shots a minute per gun
  and a one-frame flash is invisible at 4 shots/s [inferred].
- **Light on the shooter.** The Mandalorian bolts were mesh lights
  ([Image Engine](https://image-engine.com/case-studies/the-mandalorian-season-1/))
  [sourced, listing]; UE muzzle kits emit light on every unsuppressed flash
  ([Fab listing](https://www.fab.com/listings/fb9dafb0-8bf5-4f37-b1bc-b3cc79c7a6b8?lang=fr))
  [sourced, listing]; X4 players mod the weapon light range up 4.5x
  ([Nexus mod 2145](https://www.nexusmods.com/x4foundations/mods/2145)) [sourced, listing].
  We have no lighting pass, so the glow is a radial gradient on the hull depth around the
  muzzle, R about 6 w, 120 ms, and a bolt in flight lights nothing [inferred].
- **High-rate streams and strobing.** A continuous tracer stream is what players ask for
  when a fast gun fires in bursts ([EVE forums](https://forums.eveonline.com/t/make-weapon-effects-more-visible-and-awesomer/67672))
  [sourced]. Stream read = bolt length / (speed x fire interval); above 1 the stream is a
  dashed line. The muzzle re-light rule of weapon-look (reset, never add) prevents
  additive stacking; at 10 Hz under SETA 6 a 120 ms flash is always on, which reads as a
  steady lamp, so the flame length should breathe with the per-shot seed (0.7-1.3) to keep
  it alive [inferred].
- **Heat distortion for plasma.** Screen-space refraction confined to a mesh with a grab
  pass, not full-screen ([gamedev.net shaderlab](https://gamedev.net/shaderlab/38-heat-haze-distortion/))
  [sourced, listing]; normal map plus mask ([GPU Gems 2 ch. 19](https://developer.nvidia.com/gpugems/gpugems2/part-ii-shading-lighting-and-shadows/chapter-19-generic-refraction-simulation))
  [sourced]. Needs a colour-buffer copy: the one optional orb element, heavy plasma only
  [inferred].
- **Readability against bright stations and dark space.** Halo: incoming fire must be
  visible or deaths feel unfair ([Game Developer](https://www.gamedeveloper.com/design/universal-truth-number-three-pt-3-))
  [sourced]; a real-time Star Wars rule: bolts not too saturated or luminous in any
  environment ([VFX Voice](https://vfxvoice.com/?p=13146768)) [sourced, listing]. The far
  flag (`RT2.g`) protects the core through the resolve; a bright hull behind a pale core
  is answered by the dark ring above [inferred].

## 4. Recommended anatomy [inferred]

Unit w is the bolt width from weapon-look 3.3 (w = 0.5 m x (1 + 2 k)); these sizes start
above the design's rest lengths on purpose, because every flown first version so far was
too small. Everything is tint-coloured unless named white; E is linear radiance.

| Style | Core (white -> tint) | Halo | Tail | Streak | Muzzle |
| --- | --- | --- | --- | --- | --- |
| Tracer (pulse, laser, IRE, PAC) | capsule 15 w x 1 w, k 0.6 at the head, tapered both ends over 2 w | Gaussian sigma 1.5 w along the whole capsule, E 1.0 | 10 w at E 0.4 linear to 0 | previous position, energy-conserved | energy |
| Slug (ballistic, untraced or heavy kinetic) | capsule 4 w x 1 w, k 0.9 (white-hot head) | sigma 1 w, E 0.5, head only | 25 w thin (0.6 w) at E 0.25, the "tracer tail" | same | ballistic |
| Orb (plasma, PSP, Halo-style) | sphere 2.5 w with sphere-equation depth, k 0.7, fBm boil at 3 Hz | sigma 3 w, E 1.5, slightly elongated along travel (1.3:1) | comet wisp 8 w, noise-broken, E 0.3 | capped at 1 rest length | plasma |
| Wave / flak shell | crescent across the travel, width col 18, depth 0.5 w, k 0.3 | sigma 1 w, E 0.6 | none | none | flak |
| Heavy (PPC, capital cannon) | capsule 8 w x 1.5 w, k 1.0 | sigma 2.5 w, E 2.0 | 40 w tapered, E 0.5 | same | heavy |

Colour law per style: tracer and heavy hold the tint to the tail end; the slug cools along
the tail (white -> orange -> tint x 0.5) as a burning tracer does; the orb's core breathes
between k 0.5 and 0.9 with the fBm. The end-on case is free: the capsule's projection
collapses to a disc of 1 w plus its halo, so an incoming bolt is a bright dot that grows;
for the orb nothing changes.

Muzzle anatomy per class (life 120 ms unless named; every element re-lit by reset):

| Class | Core | Flame / shape | Secondary | Hull glow |
| --- | --- | --- | --- | --- |
| Energy (laser, pulse) | disc 3 w, k 0.8 | forward cone 6 w long, half-angle 15 deg, tint | none | R 8 w, E 0.8, 120 ms |
| Ballistic (gatling, mass driver) | disc 2 w, white-yellow, 2 frames min | 5-point star 5 w, rotated per shot, plus cone 4 w | smoke puff 3 w, alpha, 300 ms, inherits ship velocity | R 5 w, E 0.6, 60 ms |
| Plasma | disc 4 w, k 0.6 | sphere bloom 6 w, fBm, 180 ms | shimmer ring if heat distortion is on | R 10 w, E 1.0, 180 ms |
| Heavy (PPC, capital) | disc 5 w, k 1.0 | cone 12 w, half-angle 10 deg, 250 ms with a 60 ms pre-glow on the barrel (the Expanse railgun) | vent streaks 2 x 8 w sideways, 150 ms | R 15 w, E 1.2, 250 ms |
| Flak (turret) | disc 2 w, orange-white | short star 3 w | smoke puff 4 w, 400 ms | R 4 w, E 0.5, 80 ms |
| Missile launch | none | exhaust cone 8 w tint orange, 300 ms, with the missile's own trail | smoke ring 5 w expanding to 10 w, alpha, 600 ms | R 6 w, E 0.4, 300 ms |

Stream rule: for a gun whose bolt length exceeds speed x fire interval, draw the tracer
style with the tail trimmed to the gap so bolts do not merge into a beam; for a gun below
that ratio, keep the full tail. Kill criteria and the simple-mode fallbacks are those of
weapon-look 3.3; the sizes above are the starting point for the first flown version, not
a ceiling.

Open gaps: no publisher gives bolt or muzzle sizes in metres or frames for any game
listed; the Star Wars proportion is a fan reconstruction plus a 14-frame count; the BSG
and Expanse tracer rules are episode observation; the muzzle frame numbers are listing
text. Section 4 is entirely inference.

## Sources

Every URL is inline above. Fetched and read: ILM animation history, Wikipedia (Blaster,
Tracer ammunition, Starship Troopers effects), Wired blaster analysis, theforce.net laser
tutorial, fxguide Knoll podcast, AWN (Zoic, MPC), GateWorld, ASC, Framestore, Egosoft forum
and wiki, Starport wiki and forum, DiscoveryGC, Game Developer (Halo, Outlaw, Everspace),
Halopedia, 80.lv Destiny, EVE forums, Starsector wiki, gamedev.net billboards,
flutter_scene, UE forums, devbytes, nightshade, GPU Gems 2, Elite wikis, RSI comm-links,
starcitizen.tools, DTF, Bleeding Cool. Listing text only (host unreachable or 403): Image
Engine, Battlestar wiki, Space.com, The Companion, RSVFX, Nexus, Frontier forums, EA
forums, VFX Voice, PC Gamer, ArtStation, EVE University, Prideout, Gaia Sky, Unity issue
tracker, realtimevfx, TV Tropes, mycreativefx, Maxon, Fab, GDC Vault, playbooks.
