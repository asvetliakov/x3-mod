# Exposure in exterior-space games

Research date: 2026-09-13. This note is a bounded comparison for the X3:
Albion Prelude exterior-space camera. X3 is normally looking at black space,
stars or a bright authored nebula/planet background while ships, suns, lasers
and explosions occupy a changing fraction of the frame. It does not regularly
cross between a closed hangar and outdoors as X4 does.

The sources below are developer publications, official patch notes or official
settings documentation. They do **not** justify inferring a game's exposure
algorithm from its engine, HDR-output support, screenshots or photo mode. Most
studios publish the desired image and lighting tools, but not the meter,
percentiles, adaptation rates or current shipping constants.

## Comparison

| Game | What the developer actually documents | Classification and limits | Exterior-space lesson |
| --- | --- | --- | --- |
| **Star Citizen** | In June 2017, CIG said its exposure control was changed for space's extreme contrast: light in peripheral vision prevents the screen being overly brightened when the camera looks into space near a bright object. In September 2019, CIG described the then-current image/lens-light approach as forcing both a white room in dim light and a dark room in bright light toward the same grey. Its proposed replacement simulated a light meter using incoming lighting from all angles. Game code could also control camera exposure for mobiGlas legibility in bright light. A 2021 postmortem records an over-bright exposure bug when a planet streamed in. | **Lighting-aware direction, historically documented.** These sources describe work at different alpha stages; the September 2019 all-angle meter is new work, not proof of its current implementation. No public source found specifies present-day statistics or adaptation speed. | This is the closest documented match to X3. Black pixels must not by themselves request a large positive lift when a bright sun, planet, ship or effect is perceptually present. Scene/game context may override exposure for readability, and scene-streaming changes need discontinuity handling. |
| **Elite Dangerous** | The official Beyond Chapter Four 3.3 notes (11 December 2018) say the new lighting model used **dynamic exposure**, with colour grading applied to different situations. The same release added separate player-controlled Night Vision for the dark sides of planets, rings and other dark areas. | **Dynamic exposure; meter basis unknown.** The notes do not say whether it is image-, histogram-, lighting- or environment-metered, nor whether the current game retains the same policy. Night Vision is a visibility tool, not evidence that exposure lifts darkness. | A dynamic meter can coexist with deliberately dark navigation. Exposure need not make every shadowed exterior readable; a separate visibility mode can serve that need. Situation-specific grading also shows that one global neutral-grey target is not the only control. |
| **X4: Foundations** | Egosoft's January 2023 X TECH 5 description says the lighting engine targets natural-looking sunlight in space, higher-intensity suns, vibrant colours and a wider range of lighting moods. Reflection probes specifically improve cockpits, bridges and stations. | **Art and lighting goals documented; exposure unknown.** Egosoft does not disclose an exposure meter or adaptation behavior here. Cockpit/interior improvements should not be generalized to X3's exterior-only camera. | Preserve authored variation between black space, coloured nebulae and high-intensity suns. A meter that normalizes sectors toward one middle brightness would oppose the stated goal of varied lighting moods. |
| **No Man's Sky** | Hello Games' July 2024 Worlds Part I notes say night darkness varies over time and from planet to planet, and ambient colour follows the planet's environment. Earlier official notes record a bug in which planetary lighting settings were applied in space, showing that space and planet lighting contexts are distinct. The 2021 Prisms update deliberately increased the variety, quality and number of visible stars. | **Environment/art-directed lighting documented; exposure unknown.** These are lighting and content controls, not disclosure of a fixed or automatic exposure algorithm. Photo-mode bloom control is excluded from gameplay evidence. | Darkness, sky colour and star visibility are authored outputs to preserve. Space-specific settings are a useful model if X3 can expose reliable sector/lighting context; a whole-frame statistic alone cannot know that a bright nebula or dark sky is intentional. |
| **EVERSPACE 2** | ROCKFISH describes precomputed GI plus SSGI across its solar-system locations, later optionally Lumen, and explicitly calls some SSAO tuning an artistic choice despite reduced realism. In a developer post from April 2023 it also explained that UE4 HDR output was experimental and constrained by UI problems. | **Art-directed depth tuning; exposure unknown.** Neither source publishes gameplay exposure behavior. Engine migration and HDR display support do not establish default or custom auto exposure. | This comparison supplies no meter precedent. It does reinforce that an attractive space image can be strongly authored, and that HDR output, scene lighting and exposure are separate decisions. |

### Primary sources

- Cloud Imperium Games, [Monthly Studio Report: June 2017](https://robertsspaceindustries.com/en/comm-link/transmission/16000-Monthly-Studio-Report-June-2017), especially Graphics; [Monthly Report: September 2019](https://robertsspaceindustries.com/en/comm-link/transmission/17266-Star-Citizen-Monthly-Report-September-2019), Graphics; [Monthly Studio Report: November 2017](https://robertsspaceindustries.com/en/comm-link/transmission/16294-Monthly-Studio-Report-November-2017), Foundry 42 UK Graphics; and [Alpha 3.12 Postmortem](https://robertsspaceindustries.com/en/comm-link/transmission/17991-Alpha-312-Postmortem), published 15 February 2021.
- Frontier Developments, [Elite Dangerous: Beyond Chapter Four 3.3 patch notes](https://forums.frontier.co.uk/threads/elite-dangerous-beyond-chapter-four-3-3-patch-notes.463020/), 11 December 2018. The original thread is currently behind Frontier's web challenge; its official text is also preserved in a [contemporaneous Frontier-hosted roadmap thread](https://forums.frontier.co.uk/threads/elite-dangerous-2018-roadmap-regularly-updated.397639/page-13).
- Egosoft, [Introducing X TECH 5 and kicking off 6.00 Public Beta](https://www.egosoft.com/x/xnews/202301_1_44News.html), January 2023.
- Hello Games, [Worlds Part I update](https://www.nomanssky.com/worlds-part-I-update/), July 2024; [Beyond patch 2.13](https://www.nomanssky.com/2019/10/beyond-patch-2-13/), October 2019; and [Prisms update](https://www.nomanssky.com/prisms-update/), June 2021.
- ROCKFISH Games developers Caspar Michel and Marco Unger, [EVERSPACE 2 sets a course for the future through Unreal Engine 5](https://www.unrealengine.com/en-US/tech-blog/everspace-2-sets-a-course-for-the-future-through-unreal-engine-5), 6 May 2024; ROCKFISH Games developer Michael Schade, [Dropping Official HDR Support](https://steamcommunity.com/app/1128920/discussions/0/3823034639982761837/?ctp=4), 11 April 2023.

An additional official control example is useful but does not reveal a meter:
EA documents separate 0–100 brightness and on/off HDR controls for
[Star Wars: Squadrons](https://www.ea.com/able/resources/star-wars/star-wars-squadrons/pc/video-settings).
This is why an HDR setting cannot be counted as evidence for auto exposure.

## Constraints for evaluating X3 policies

The comparison supports these constraints, not a final X3 policy choice:

1. **Do not grey-key black space.** The most specific space-game source calls
   out excessive brightening while looking into space near bright objects, and
   the later CIG work explicitly identifies grey-key ambiguity. A sparse-frame
   rule that sends X3 repeatedly to a positive EV cap has the same failure
   shape.
2. **Preserve authored sector mood.** X4 and No Man's Sky deliberately vary sun
   intensity, sky colour, darkness and starfields. Sector-to-sector
   normalization would erase information the original artists put into X3's
   backgrounds.
3. **Treat transient emitters as highlights, not exposure calibration.** Lasers,
   explosions, weapon flashes and a sun entering frame should retain contrast
   and bloom without pushing the whole scene brighter. Published sources do not
   supply useful adaptation rates; X3 testing must determine hold, attack and
   release behavior.
4. **Prefer reliable lighting/environment context when available.** CIG's
   documented direction uses incoming illumination rather than only reflected
   image brightness, while Elite and No Man's Sky use situation/environment
   controls alongside presentation. For X3 this only applies if reverse
   engineering finds a stable game-level sector/sun/lighting signal. Native RGB
   magnitudes are authored and gamma-decoded, so they must not be relabelled as
   physical luminance.
5. **Use a conservative fallback when context is absent.** Fixed EV 0 or a
   narrowly bounded correction around an authored baseline remains a valid
   candidate. The research found no primary-source precedent requiring a wide
   automatic lift for an exterior space view. Compare such candidates in the
   same sector against star visibility, nebula/planet colour, sun and hull
   highlight retention, and flashes/explosions.
6. **Keep visibility and exposure conceptually separate.** Elite's Night Vision
   handles intentionally dark navigation without forcing the exposure system to
   reveal everything. X3 may not need that feature now, but auto exposure should
   not silently take on the same job.

## Unknowns that evaluation must resolve

- No source above publishes enough detail to reproduce a current shipping
  game's meter, histogram masks, EV range or adaptation time constants.
- The Star Citizen all-angle light meter was announced in 2019; its current
  shipping status and exact implementation are unknown.
- Elite documents dynamic exposure but not its input statistic or treatment of
  stars, skyboxes and short-lived effects.
- X4, No Man's Sky and EVERSPACE 2 provide useful art-direction constraints but
  no evidence for classifying their exposure as fixed, image-metered or
  lighting-metered.
- Player brightness, gamma, HDR-output and photo-mode controls cannot answer
  those implementation questions.
