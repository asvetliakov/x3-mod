# Engine exhaust gap analysis: X4: Foundations and Everspace 2 against the proxy plume stage

Design note, 2026-10-03, after Run 122 A (run407/408). Question: what the reference exhausts consist of, what the
stage draws today, what is missing or different, and in what order to close the gaps. Investigation only: no
production edit, no launch, no Wine, no build. Inputs: [engine-effects-modern.md](engine-effects-modern.md)
(sections 3-5 and the look sections after flights B-D), `src/effects/engine_plume_ps.hlsl`,
`src/effects/engine_ribbon_ps.hlsl`, `src/proxy/engine_plumes_core.h` (`Look`), `src/proxy/engine_ribbons_core.h`,
`tools/effects/engine_exhaust_lab.html`, the ledger [engine-effects.md](../verification/engine-effects.md) (flights
A-E), the RE note [engine-effects.md](../reverse-engineering/engine-effects.md), the Run 122 triage
(`verification/results/run407-408-engine-plumes/README.md`), and the dropped effects design's section 3.8
([effects-modernisation-opus.md](effects-modernisation-opus.md)).

**In flight, not analysed here (read main at 8f90fa58):** an implementation agent is changing the plume files for
Run 123: far-jet records from the small-parts cull stub (the Run 122 finding: every far jet is removed by the proxy's
own cull at projected s < 3 before the engine draws it), a distance law, halo brightness 0.35 -> 0.20, and the floor
scale 0.5 as the default. Gaps 1 and 2 below are those changes; the plan starts after them.

Marks: [c] confident recollection of the reference game; [i] inferred from how such effects are commonly built or
from a less certain memory; [m] measured in this repository (ledger or fixture); [u] unknown. The reference
descriptions are from memory of the games, not from captures: nothing in them is verified here.

## Decision in one paragraph

The stage already has the hard part: a living flame body (turbulence, erosion, shock cells, pulse, hot core, halo,
ring, end-on disc) with the game's throttle law. What separates it from X4 and Everspace 2 is not the flame but its
context: (1) distant engines, which in both references are the brightest thing about a far ship and here were culled
(in flight); (2) the halo's weight and its spill onto the hull around the nozzle (the halo is in flight; the spill is a
two-instruction `glow_through` term that was designed and never built); (3) the turbulence's speed, which is the same
in nozzle widths for every size, so a capital's plume boils like a scaled-up fighter instead of crawling; (4) a single
tint, where the references go white -> saturated -> softer along the plume (the body's peak colour is already
computed and discarded on the CPU); (5) transients: strafe puffs, retro flare and a travel-mode (SETA) stretch, which
the game's rate-limited `z` smooths away. Hull light through the material route and a post-resolve heat shimmer are
the two real "modern" cues that cost a pass or a program family each; they come last. Sparks, smoke, lens glare and
particle trails are not copied.

## 1. Anatomy of the reference exhausts

| Layer | X4: Foundations | Everspace 2 | Confidence |
| --- | --- | --- | --- |
| Nozzle interior | The engine mesh has an emissive bowl: the cavity glows at idle in the faction colour, brighter under throttle; the glow sprite sits in it | Emissive nozzle ring/bowl on the ship mesh, glowing at idle; bright white at the throat under thrust | [c] both glow at idle; [i] the bowl brightness follows throttle in X4 |
| Camera-facing glow sprite | A soft round glow billboard at each nozzle, 1.5-2.5 nozzle diameters, additive; it is the dominant element head-on, at idle and at distance | The same, somewhat tighter, with a hotter centre; a lens-glare streak on the own ship's engines in some camera modes | [c] both have it; [i] the sizes |
| Hot core | White-to-tint core along the first third of the flame | White/cyan core, sharper, longer under boost | [c] |
| Flame body | Stacked additive cone planes with a scrolling noise/fire texture; smooth, slow structure; faint shock rings on larger engines | A fire-like scrolling noise body with eroded edges, higher-frequency flicker, visible shock-diamond rhythm under boost | [c] for the look, [i] for the construction (stacked planes vs a single textured cone) |
| Outer halo / haze | A wide soft sheath, dim, roughly the sprite's colour; much of the "halo" is bloom | A dim sheath plus heavy bloom | [c] |
| Heat distortion | Present behind large engines and in travel mode; subtle at fighter scale | Present at the nozzle and strong under boost (UE refraction material) | [i] for X4 (moderate), [c] for ES2 |
| Bloom | Wide, strong; engines bloom more than any other hull element; distant engines are bloom dots | Heavy, with a tight bright centre; distant ships are engine dots with bloom | [c] |
| Trail / contrail | A thin ribbon behind fighters that lengthens with speed and becomes a long streak in travel mode; capitals have none or a faint one | Long luminous ribbons, strongest under boost, fading in about a second | [c] |
| Sparks / particles | None at cruise; faint particle streaks in travel mode | Ember particles from the nozzle under boost | [c] X4 none at cruise; [i] ES2 embers |
| Colour | Faction colour: Argon blue-white, Teladi yellow-green, Paranid blue/violet, Split orange-red, Xenon red, Terran white-blue; white core -> tint -> a slightly darker, more saturated tint at the tail | Per-ship colour (cyan/blue default, orange/purple variants); cyan core -> blue -> violet tail on the default engines | [c] for the palettes; [i] for the exact tail gradient |
| Motion | Scroll away from the nozzle at a speed that reads as physical: slow on capitals, fast on fighters; flicker amplitude low (about 10 %), rate low; a slow breathing pulse | Faster scroll, visible flicker 10-20 % at 10-20 Hz, length pulse | [c] for X4 smooth / ES2 lively; [i] for the numbers |
| Throttle | Length and brightness follow the throttle with a lag of a few hundred ms; idle keeps a short glow | Same, with a sharper response | [c] |
| Boost / travel | Boost: a brief brighter, longer flame and a streak; travel mode: the plume elongates several times, turns whiter, and the trail becomes a long line | Boost: 2-4x length, brighter, more saturated, shock diamonds, shimmer, embers | [c] |
| Strafe / RCS | Short bright puffs at the thruster ports when strafing or rotating, 100-200 ms, with a fast attack | The same, brighter and with a few sparks | [c] both have puffs; [i] the durations |
| Braking | Forward-facing retro thrusters fire visibly when braking | Same | [c] X4; [i] ES2 |
| Shape | Fighters at cruise about 3-5 nozzle diameters long, slight mouth bulge, long soft taper, the tail fades rather than ends; travel mode 10+; capitals short relative to the nozzle (1-3 diameters) and wide | Cruise 2-4 diameters, boost 6-10; the tail wavers | [i] (ratios from memory) |
| Distance | Engines stay visible as bright dots with a glow sprite for many km, brighter than the hull; a far ship reads as "its engines" | The same; the sprites have a minimum screen size | [c] for the dot look; [i] for the minimum size |
| Hull | The sprite and bloom spill onto the engine housing; X4 engines also light nearby hull plates (an emissive mesh plus a local light); the own ship's rear is lit in chase view | A local light on the engine socket lights the rear hull plates; the plates nearest the nozzle glow | [i] for the local lights (moderate), [c] for the spill |
| Occlusion | The glow sprite fades softly at the hull edge and bleeds a little around the nozzle rim; the flame is depth-tested | The same (UE soft particles) | [c] for the soft edge; [i] for the bleed amount |
| Cutscene / photo mode | Identical to gameplay; photo mode only changes the camera | Identical | [c] |

## 2. Ours, layer by layer

Numbers are the `Look` constants on main (measured by reading the code); the in-flight values are noted.

| Layer | Status | What we draw |
| --- | --- | --- |
| Nozzle interior | **partial** | The ring (0.3 x I(s)/4, radius 0.46 bulge, falloff 14 per nozzle width) and the disc's mouth; the hull's own nozzle cavity is the game's opaque material (the `grey` cluster). No emissive bowl. Flight D asked for the mouth below the body (mouth / body 0.60-0.77, gated 0.85 [m]). |
| Camera-facing glow | **present, different** | The end-on disc (weight smoothstep(0.3, 0.7, facing), the integrated body x kappa 1.8, halo x 3, soft cap 1.5 x the side peak) carries the head-on look; side-on there is no sprite, only the halo sheath along the body. The references' sprite is view-independent. |
| Hot core | present | heat 0.7, radius 0.45 of the local width, cooling over the first 0.55 L, x 1.6 radiance in the core mask. |
| Flame body | present | Bulge 1.15, taper 0.45, tail 0.7 (narrowing 0.6), 3-octave value noise: erosion 0.57, turbulence 0.6 (+-66 % modulation), shock 0.5 at period 0.16 L fading 0.6 and ramped in over one period, pulse 0.25 at 3/s, mouth ramp dip 0.5 over 0.3 L. L = 2 z nozzle widths (0.5 idle, 4 full). |
| Outer halo | present (in flight) | e-fold 0.55 nozzle widths at the nozzle, constant along the plume, exp(-2.2 u), hb 0.35 x I(s)/4; flight E: too strong; 0.20 in flight. No spill over the hull: SOFT 1.0 value, `glow_through` designed (section 4 of the owning note) but **not built** [m: no such term in the shader]. |
| Heat distortion | **missing** | Section 3.8 of the dropped design has the mechanism (post-resolve, scissored). |
| Bloom | present, unshaped | Emitters up to I_core 4.0 on the FP16 target bloom through the shared pyramid; per-source strength is separable, radius is not ([bloom-per-source-attenuation.md](bloom-per-source-attenuation.md)). Whether the plume's bloom reads too wide or flat is [u] until the user says so; flight E's "halo too strong" may be the halo, the bloom or both. |
| Trail | present, different | Ribbon 0.6 x the half-width, I lerp(0.2, 0.9, s) x (1 - u), T 0.5 / 0.8 / 1.2 s by value, 16 distance samples, 0.3 s fade. Always on at speed; the references' long trails belong to boost/travel. |
| Sparks | missing (not wanted) | |
| Colour | **partial** | One tint per record: the body's normalised mean (`engine_bodies.json`), white-hot core -> mean. `record_tint` also returns the body's peak colour and the builder **discards it** [m: `peak` is written at line 781 and never read]. No secondary tail colour. |
| Motion | **different** | Flow 2.625 nozzle widths/s at every size (`flow_rate`: constant in value units per nozzle width, i.e. a capital's plume scrolls as many of its own widths per second as a fighter's). Pulse 0.25 at 3/s on every nozzle. No fast flicker (removed with the first look). |
| Throttle | present | The game's z (0.25 + 1.75 s, 0.004/ms) for L; I(s) = lerp(1.2, 4.0, s); the mouth terms follow I(s)/4. |
| Boost / travel | **missing, no source** | The game has no boost term [m, RE note]. SETA is not known to the proxy [m: no SETA state in `motion_output.h`; the temporal code only reacts to motion]. |
| Strafe / RCS | partial | v/00566 jets drawn as the same quads, L = z value with z 0.01..1, weight z, no floor, the mouth ramp cuts up to 50 % at the nozzle. The game's rate limit makes a puff a 250 ms swell up and a 250 ms swell down, not a flash. |
| Braking | partial | `flag_brake` bodies (z > 2) draw as long plumes up to 6.25 value; no retro-facing flare distinct from the main plume; excluded from the floor. |
| Shape | present | Comparable to the references at cruise (0.5-4 nozzle widths); idle is shorter than the references' idle glow. |
| Distance | **missing** (in flight) | Minimum 3 px wide / 6 px long, cull below 1.5 px; and the proxy's small-parts cull removed every jet below projected 3 px before the stage saw it (run407/408 [m]). |
| Hull | **missing** | No light on the hull, no spill. |
| Occlusion | present | Core SOFT 0.15 value, halo SOFT 1.0, bias 0.5 value toward the camera; nothing draws through the hull. |
| Cutscene | present | The drawn view's records only (`ViewFilter`). |

## 3. Gap list, ranked by visual value per cost

Slot costs are inferred from the shader (the base program is 687 slots, measured); at 5120x1440 the stage covers a
few per cent of the screen, so +20 slots is on the order of 1-2 us [i, from the 1 us/slot/full-frame anchor].

| # | Gap | Why it matters | Mechanism | Cost | TAA | Windows | Judge |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | **Far engines as glow dots** (in flight) | Both references read a far ship by its engines; "plumes only on near ships" (flight E) is the largest difference | Far-jet records from the cull stub, a distance law | per the in-flight work | low (a 4-6 px dot above E survives at 1.00 [m, motes/bolts]) | low | in Run 123 |
| 2 | **Halo weight** (in flight, 0.35 -> 0.20) | Flight E | constant | 0 | - | - | in Run 123 |
| 3 | **Glow spill onto the hull at the nozzle** | The references' sprite bleeds around the nozzle rim and reads as the hull being lit; ours is cut cleanly at the lane, so the plume looks pasted on | `soft_halo = max(soft_halo, glow_through)` within the first `spill_reach` nozzle widths of the mouth (so the spill stays at the nozzle, not along the whole hull edge); `glow_through` 0.15, reach 1.0 nozzle width; the body and the ring unchanged | +4 ps slots [i], 0 CPU | none (static over frames) | none | needs the user's eye for the amount; the mechanism is clearly right |
| 4 | **Flow speed in world units** | A capital's turbulence boils at a fighter's rate: the one thing that makes big engines look like scaled-up small ones instead of slow and massive; the references' scroll is physical [c] | Phase per nozzle = world phase / n with a reference speed (e.g. the fighter class: 2.625 x 500 = 1,312 world units/s), clamped to [0.3, 1] x today's rate in nozzle widths so a tiny RCS jet does not strobe and a capital crawls at 0.3; the CPU computes the per-nozzle phase in double and writes it per vertex (the `shape.w` kind flag folds into the sign of `shape.z`, the bias, so the vertex stays 72 B) | 0 ps, +1 fmod per nozzle on the CPU | none (slower is safer) | none | clearly right in direction; the reference speed needs the eye |
| 5 | **Two-tone colour** | White -> saturated -> softer tint is what both references do; one tint is the flattest thing about ours | `colour = lerp(lerp(peak, mean, smoothstep(0.3, 1, u)), white, heat)`: the body's peak colour (already computed) in the first third, its mean at the tail; a second D3DCOLOR in the vertex (76 B) | +3 ps slots [i], +4 B/vertex | none | none | needs the eye (depends on how far the textures' peak and mean differ: the cluster tints are identical for both) |
| 6 | **RCS puff attack and retro flare** | X4/ES2 puffs are flashes; the game's rate limit gives a swell | Per-nozzle `last_z` in a serial-keyed map (the ribbon pool's shape, 512 slots); a rising z on a steering or brake body adds radiance x (1 + 0.5 x saturate(dz / 0.004 per ms)) decaying over 120 ms; no shape change | 0 ps, ~20 ns per RCS nozzle, one map | low: 120 ms is 7 frames at 60 Hz, above E it keeps 1.00 | none | needs the eye ("not distracting in fights": keep it at 1.5x and short) |
| 7 | **Travel look under SETA** | X4's travel drive elongates and whitens the plume; SETA is X3's travel mode and the user flies in it a lot | Detect SETA as game-time step / wall-clock step (the game's frame delta `*(0x00606f34)+0xcc` (the SETA factor, 16.16; `+0x718` is the absolute game clock, RE §8) is named in the RE note [m for the address, u for the read's safety]); ramp L x up to 2, I x 1.25, the ribbon's T x 2 over 0.5 s with hysteresis | one bounded read per frame, 0 ps | low | none | needs the eye and a disassembly confirmation of the read |
| 8 | **Engine light on the hull** | ES2 and X4 light the rear plates [i]; the own ship's lit tail in chase view is the strongest single "modern" cue | Material route: a second point light in the converted hull programs (the fill-light shape: one term in all 168 pairs via the bytecode editor), fed per object with the brightest main nozzle's position, colour and I from the previous frame's records (the jet's parent is the ship root, known to the recogniser) | +8-10 slots on every hull pixel (about 1 % of the hull pass [i]), one constant upload per ship draw, one map | none | low (documented API) | needs the eye; the largest task |
| 9 | **Heat shimmer** (built 2026-10-03, phase 5 below) | ES2's signature at the nozzle; visible only on the own ship in chase view at 5120x1440 | Section 3.8: post-resolve, scissored to the plume rects' union, `StretchRect` copy + one refraction draw with the plume's own noise; amplitude 1-2 px | 0.04 ms per 10 % rect + up to 0.36 ms pass boundary [i, 3.8] | none (after the resolve), but unaveraged: keep the amplitude low | low | opt-in; needs the eye |
| 10 | Idle glow | At idle the references keep a short soft glow; ours is 0.5 nozzle widths and I 1.2 | Raise the idle length floor to 1 nozzle width (L = max(2 z, 1)) | 0 | none | none | needs the eye; small |
| 11 | Bloom shaping | [u] whether the plume's bloom is too wide; the shared pyramid cannot give a per-source radius | Per-source strength through the alpha term if the eye asks | 0 | none | none | wait for the eye after 2 |

Not in the table because the stage already has them: the shock cells, the pulse, the eroded edge, the soft lane
occlusion, the end-on disc, the throttle lag.

## 4. Recommended phase plan

Each phase is one `implement` task on a worktree with the fixture named, then one flight. Phase 1 is the in-flight
work and is not re-planned here.

| Phase | Changes | Fixture (acceptance) | Verdict type |
| --- | --- | --- | --- |
| 1 (in flight) | Far-jet records, distance law, halo 0.20, floor 0.5 | the in-flight agent's | Run 123, user's eye |
| 2 "physics and colour" (**built 2026-10-03**, section 5) | Gaps 4, 5, 3, 10 together: world-unit flow, two-tone colour, nozzle spill 0.15 over 1 nozzle width, idle floor | `run_engine_plumes.py` new cases: (a) two values 100 and 10,000 px: the noise field's displacement between frames in world units within 5 % of each other and of the reference speed (the CPU replica); (b) colour at u 0.1 / 0.5 / 0.9 against the replica within 1 %; (c) the occluded-mouth case: halo over the hull at the nozzle = 0.15 +- 0.02 of unoccluded, 0 past 1.5 nozzle widths; (d) idle length 1 nozzle width. Host: the per-vertex phase wrap over 10^4 s, the kind/sign packing round trip. Gates: ps slots <= 800, the fenced stage within noise, `test_engine_*` OK | 4 and 10 clearly right in direction; 3 and 5 by the eye. One launch |
| 3 "transients" (**built 2026-10-03**, section 5) | Gaps 6 and 7 | Plumes fixture: a steering record with z 0.01 -> 1 over three frames: radiance peak at frame 2 >= 1.3x and <= 1.5x steady, back within 150 ms; a brake body the same. SETA: a host test of the detector (ratio 6x, hysteresis, no false trigger on a 0.1 s stall); `run_engine_effects.py` counts the read; the read's safety by a `disassemble` task on `*(0x00606f34)+0xcc` (the SETA factor, 16.16; `+0x718` is the absolute game clock, RE §8) first | the user's eye, two launches (SETA on/off is the same launch) |
| 4 "hull light" | Gap 8 | The converted-material fixture with a synthetic engine light: the lit plate's radiance against the law within 1 %; byte-identical programs at light 0; per-draw cost by the hull pass's existing timing | `implement-deep`; the eye. **Built 2026-10-03** ([engine-light.md](engine-light.md)), not flown: twins of the 104 hull/palette/XT/glass programs, +25 slots (+28 on the share producer), not the +8-10 inferred above; lit radiance within 0.28 % of the law, unlit pixels bit-identical, light-off programs unchanged; 34 ns per lit draw, 3.7 ns per unlit routed draw; the term costs 50.9 us per full-screen hull draw at 5120x1440 and only on lit draws (all measured, [engine-light ledger](../verification/engine-light.md)) |
| 5 "shimmer" (**built 2026-10-03**, not flown) | Gap 9, on with the plumes at 1.5 px (`engine_shimmer`, `engine_shimmer_max` 4, load-time; the Ctrl+Alt+F7 toggle removed 2026-10-04) | A fixture over the resolved target: a known plume rect, measured offset <= 2 px, zero outside the rect, cost per rect size ([engine-effects.md](../verification/engine-effects.md), "Heat shimmer") | the eye; last |

Clearly right, do now (phase 2): the world-unit flow and the idle floor. Needs the user's eye: everything else.

## 5. Built: phases 2 and 3 (2026-10-03, worktree build on c6ca2842, not flown)

The laws and constants are in [engine-effects-modern.md](engine-effects-modern.md) ("After flight E, the gap analysis'
phases 2 and 3"); the checks in the ledger [engine-effects.md](../verification/engine-effects.md). Numbers are measured
(the X3 bottle, `run_engine_plumes.py` 178/178, both sizes identical unless given) unless marked.

| Gap | As built | Fixture result |
| --- | --- | --- |
| 4 flow | phase = accumulator x clamp(500 / value, 0.3, 1), per vertex | displacement over 0.2 s in world units / the law: 0.9999-1.0000 at value 100 / 600 / 1,500 / 10,000; 600 and 1,500 at 131.24 / 131.25 against 131.25 (656.25 per s), ratio 0.9999; lag-1 0.901 (100) and 0.946 (10,000) |
| 5 two-tone | head = the table's peak at the mean's luminance, mean towards the tail | 240 of 253 bodies' peak and mean differ by more than 5 % (median per cluster 0.08-0.66 of a channel; white identical), the peak 1.0-3.6x the mean's luminance (`verification/results/engine-effects/plume_two_tone_colours.py`): the table's peak is used, luminance-matched, no derived tail; colour against the replica at u 0.1 / 0.5 / 0.9, axis and 0.7 of the half-width: worst 0.00053 |
| 3 spill | 0.15 within 0.8 n, 0 at 1.0 n, only through an occluder within 2 value of the nozzle | head-on behind a plane at the nozzle: law 0.1501 (max 0.1503) in 0.65-0.8 n; the production soft cap 0.152 / 0.154 (1080p / 5120, max 0.155: the open halo is compressed more than the spilled one); 0 past 1.0 n; 0 through a plane 3 value in front |
| 10 idle floor | L = max(z, 0.5) value | L / value 0.5 at s = 0; mouth / body at s = 0 / 0.5 / 1: 0.752 / 0.645 / 0.580 (was 0.761 / 0.645 / 0.580; gate 0.85) |
| 6 attack | 512 slots, x (1 + 0.5 saturate(dz / (0.004 dt_game))), 120 ms linear decay | steering z 0.01 -> 0.505 -> 1: frame 2 at 1.500x steady, back at 133 ms; brake 2.2 -> 3.0 the same; a main jet 1.000 |
| 7 travel | one read per stage frame, ramp 0.5 s, hold 0.3 s; L x 2, I x 1.25, ribbon T x 2, flow x 1.5 | warp 6 through the production decode and ramp: weight 1; L 2.000x, I_core 1.250x, drawn length 2.010x, peak 1.2505x; the effects fixture's armed mode reads through the seam (rows read / engage at 6.000 / release; 44 reads, 0 refused in the merged build's record (23 in the first phase-3 run)), armed_refused fails closed (`site_mismatch`) |

Cost: ps 734 slots (687 before, gate 800); vertex 76 B; CPU build at 300 records 31.8 us (29.9 / 30.8 in the two runs
before; 1,024 records 111.6 against 108.3); stage GPU at 300 nozzles (250 far) 0.80 ms at 1080p, 0.28 ms at 5120x1440
(0.28-0.84 / 0.26-0.27 in the two runs before: within their spread) [m, fixture timings, not game FPS]. Deviations from the brief: the flow fixture's "value 100 and 10,000 within 5 % of each
other" cannot hold under the brief's own clamp (their world speeds differ 30x by design, 26.25 vs 787.4 per 0.2 s), so
the equal-speed check runs at 600 and 1,500 inside the clamp and 100 / 10,000 are checked against their law; the spill
gained the depth guard and a 0.8-1.0 n taper (both bounded, documented); the head colour is luminance-matched so the
plume does not brighten (red's head is 0.39x the raw peak). Needs the user's eye: the spill amount, the two-tone, the
attack's 1.5x and the travel look.

## Unknown, and what settles it

- Whether the bodies' peak and mean colours differ enough for gap 5 to show: `tools/effects/engine_bodies.py` output
  (`engine_bodies.json` mean_linear / peak_linear) for the Mayhem glow family, a five-line Python; the cluster tints
  are identical for both and would need a second table. *Settled 2026-10-03 (section 5): they differ.*
- Whether `*(0x00606f34)+0xcc` (the SETA factor, 16.16; `+0x718` is the absolute game clock, RE §8) is readable every frame as the game-time step and what it holds under pause and SETA:
  a `disassemble` task on the drive's reader (`0x004596e0`, RE note section 2) before gap 7. *Settled by the RE note's
  section 8 (the factor `+0xcc`, not a step; main-thread writers; the block lives to exit) and built in section 5.*
- Whether the hull programs can carry a second light without exceeding the ps_2_x/3_0 budget of the converted
  families and where the light position would be uploaded: the fill-light note's per-draw path and
  [hull-emissive-widening.md](hull-emissive-widening.md) section 2 settle the mechanism; a fixture settles the cost.
- Whether the plume's bloom reads too wide after the halo change: the user's eye in Run 123.

## Options considered and why they lose

- **Copy the references' camera-facing glow sprite as a third quad per nozzle.** The end-on disc already covers the
  head-on case and the halo the side; a view-independent sprite would stack with both at oblique angles (the hand-over
  problem of flight C again). The spill term (gap 3) gives the sprite's one visible benefit, the bleed at the rim.
- **An emissive nozzle bowl through the material route.** Needs per-ship texture work or a material-side mask the
  data does not carry; flight D already asked for a dimmer mouth. Not worth a program family.
- **Sparks, embers, particle trails.** Particles need a pool, a sort and a soft-particle term the user declined; ES2
  shows them only under boost, which X3 lacks. Too much for the restrained taste and the stage's one-draw shape.
- **Smoke or tail wisps on capitals.** Neither reference has smoke on ship engines in space; the capital's slow
  billow comes from gap 4 (flow speed), not from a new layer.
- **Lens glare streaks on the own ship's engines.** Distracting in fights; the user's sun flare policy is restraint.
- **"Boost" from s > 0.9.** Ships cruise at full throttle most of the time; a boost look there would be the normal
  look. SETA is the only travel-mode analogue (gap 7).
- **Long ribbons always on (the references' travel streak).** Already longer than the references at cruise; the SETA
  ramp in gap 7 is where the streak belongs.
- **Heat shimmer before the resolve.** Clipped by the 3x3 and jittered by the sample offsets (section 3.8); only the
  post-resolve, scissored form is viable, and only as opt-in.
- **Per-source bloom radius for the plumes.** Needs a second pyramid (the attenuation note); strength alone is
  separable and is enough if the eye asks.

## Phase 5 as built (2026-10-03, worktree build, not a candidate)

`src/proxy/engine_shimmer_core.h`, `src/renderer/engine_shimmer_pass.{h,cpp}`, `src/effects/engine_shimmer_ps.hlsl`,
`src/proxy/motion_output_engine_shimmer_inc.h`. After the temporal resolve on the FP16 route, on frames whose plume
stage drew, the scene view's records go through the plume builder itself (`build_nozzle`, so the rects follow its
cap, floor and pulse) to at most `engine_shimmer_max` rects (default 4, the own ship plus the nearest; up to 16) of nozzles of 24 px or more, ranked by projected width: from a quarter nozzle
width behind the nozzle to 1.5 L ahead along the projected axis, two nozzle widths wide (centred on the nozzle when
foreshortened). One `StretchRect` copies the resolved image over the union (grown by the amplitude) into a scratch; one
`DrawPrimitiveUP` of the rects' quads, scissored to the union, writes the refraction into the resolved image: per pixel
the sum over all rects of mask x the direction of the plume's value-noise gradient (one octave, cells of half a nozzle
width, scrolled by the nozzle's own flow phase as its plume (the stage's flow and SETA travel are passed to the builder), boiling with the stage's clock), clamped to unit length, times the amplitude
(1.5 px at 1440 rows by default, `engine_shimmer_px`). A scene depth nearer than the plume's nearest point less one nozzle
width takes none (a hull in front of its exhaust is not distorted). The resolved image is the TAA history, so the copy
goes back before Present: the next resolve never sees the shimmer (unaveraged, no accumulation). Measured on bottle X3:
ceiling 1.10 / 1.47 px against 1.125 / 1.5 px at 1080p / 5120x1440, byte-equal outside the rects and after the revert;
cost and the rest in the ledger section.

## End-on view: options deferred (2026-10-04, after Run 129 A)

The end-on view is the one place the plume is weaker than X4 / Everspace 2: the body strip foreshortens to nothing down
the axis and the integrated disc stands in for it, flat. Two options were costed and deferred by the user ("not worth for
now"):

- **Stacked cross-section discs**: three or four discs spaced along the axis inside the plume, each the law's radial
  profile at its depth, the radiance split so the total equals today's disc. Edge-on from the side (side views unchanged),
  parallax end-on. Builder change plus a fixture case; about a day with one flight. The cheap first step if the flat
  end-on disc ever bothers.
- **Volumetric exhaust**: a bounding cone per nozzle ray-marched through a 3D density field (the fire law x advected 3D
  noise), replacing strip and disc, right from every angle and intersecting hulls correctly. Per-pixel samples x coverage
  (a near capital's ten nozzles) is the first plume feature with a real frame-time risk; a few thousand slots; two to
  three days and several flights. Only if the stacked discs are still unsatisfying.
- Crossed fixed strips (Freelancer-era) were rejected: they overbrighten from the side where the strips overlap unless
  weighted and normalised by facing, and shimmer across the crossings as the camera moves.
