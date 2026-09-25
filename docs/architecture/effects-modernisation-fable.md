# Effects modernisation: weapons, impacts, explosions, engines (Fable design)

Design note, 2026-09-25, for the main session to ratify. No code, no capture, no
Wine run of its own; every figure is **[M]** measured in a cited ledger or
**[I]** inferred here. A parallel note by a second designer exists and was not
read.

**Outcome.** Build the effects on two proxy mechanisms that already exist in
part: (1) *in-place substitution* of the game's own effect draws keyed on the
exact program pair plus draw shape and material identity (as the bolt footprint
and the source-gain variants do today), and (2) a *deferred proxy effect queue*
drawn once per frame at step 0 of the scene-end hook, additive into the FP16
scene with the game's depth test still bound and the completed RT2 depth bound
as a texture (exact current-frame soft depth fade, no RESZ, no frame lag).
Phase 1 is projectiles (HDR core + halo, motion-stretched, proxy-generated
texture, extending the existing substitute vertex buffer) and shield/hull impacts
(the impact sprite replaced by an animated ripple plus spawned spark streaks);
together about **0.15 ms** per combat frame at 5120x1440 [I]. Everything is
documented D3D9 (ps_3_0, R32F sampling behind the existing MRT gate, dynamic
VBs, scissor) and builds for native Windows; nothing depends on RESZ or a
Wine-private layout.

## 0. Scope and constraints

User request (2026-09-25): replace or modernise weapon effects (shield hits
etc.), projectiles and engine trails; full replacement is allowed (overlay
catalogue assets, proxy-side substitute draws, new shaders). Standing
preferences: cheap emitters above 1.0 rather than an exact blend law; 50-60 fps
at 5120x1440 (every 0.1 ms counts); decisions target 1920x1080 as the common
case; a little TAA ghosting is accepted; native Windows is documented-D3D9-only
and unverified. The earlier "no soft particles" decision is superseded. Sun and
lens flares are out of scope (they have their own decisions: sun occlusion
default, flare overflow fix).

## 1. What the game draws today (grounded facts)

| Class | Draw path | Blend | Geometry / data | Source |
| --- | --- | --- | --- | --- |
| Bolts | bullet pair VS `5e484a06` (vs_1_1, VP only, c0-3) / PS `ec1f5c4a` (ps_1_1: one texture sample x COLOR0.a) | ADD ONE/INVSRCCOLOR, Z test on, Z write off, cull NONE | CPU-written world-space instances (stock bodies 8-78 faces, e.g. 72-vertex flamethrower/PlasmaBeam), stride 24 (POS, UV, D3DCOLOR), DISCARD lock of the whole buffer, non-indexed `DrawPrimitive(4, 0, count/3)`; one 512x512 DXT5 stage-0 texture; 2 draws/frame, 792-840 prims each with fire held in run271 [M] | effects-engine-remaining-emission.md "Bullet vertex buffer writer"; bolt-footprint.md section 1 |
| Beams | DEFAULT technique VS `d5e1c753` (`g_TexMatrix` UV) / PS `8360f422` (ps_2_0: sample, affine colour matrix c0-c2, x COLOR0.x fade) | ONE/INVSRCCOLOR (the 20 `bullet_*`/`fx_beams_diff` materials) | ordinary submesh dispatch `0x004c0150`; body meshes under `objects/effects/weapons/` | effect-shader-users.md blend census; effects-engine-remaining-emission.md "AF" row |
| Impact / shield-hit sprites | same DEFAULT pair | ONE/INVSRCCOLOR (`objects/v/00518, 10659, 12007, 12009`) | 10659 = one 2-face quad, diffuse `exp_PL_imp_diff`; 00518 = 70 faces, diffuse `fx_bullets2_diff`; 12007/12009 have both `.pbb` and `.pbd`, engine order unverified [M this session, `tools/analysis/body_materials.py`] | this note |
| Explosions | same DEFAULT pair | ONE/ONE (`expl.pbb`); `explosion_plane.pbd` / `exp_sparks.pbd` name no effect source | unknown draw shape and texture; no capture yet | effect-shader-users.md "Open unknowns" |
| Engine glows | same DEFAULT pair | 92 of 122 nozzle bodies ONE/INVSRCCOLOR, 30 ONE/ONE; 22 more nozzle materials are hull programs | separate scene objects at the nozzles, `objects/effects/engines/*`, TexAnim-animated textures `fx_engine_*` | effect-shader-users.md "Engine glow" |
| Generic particles | `0x004bf4c0` at `0x00472307`, gated on `view[0x270] & 0x4000`, after the sorted material queue of the view; VS `36f98d15` / PS `222bee0d` (SM1.1, `BASE_NoLight`) | ADD SRCCOLOR/INVSRCCOLOR, RGB only, Z test on, no Z write | view-space billboards: POS (centre) + TEXCOORD0 offset added in view space + UV + colour, stride 32, 21-61 quads per draw, one draw per frame, 256x256 X8R8G8B8 texture; c0-3 view, c4-7 projection | particle-motion-inputs.md; emission-draw-order.md; frame-loop-phases.md |

Pipeline facts the design relies on:

- Effects draw into the owned FP16 scene (RT0) with RT1 (motion, 16 B) and RT2
  (depth, R32F today; the fade owner/camera gate keep it R32F or a 16 B lane)
  bound; unrouted draws leave the RT2 sentinel (-1). The scene-end hook at
  `0x004721b1` runs step 0 (state block, unbind RT1/RT2, depth off), then the
  TAA resolve, HDR bloom, AgX write-back, exposure (hdr-scene-path.md section 4).
  Fog composites before the resolve (fog-gpu-cost.md).
- TAA resolve: history clamped to the 3x3 box (mean +- 1.25 sigma, intersected
  with min/max), 0.9/0.1 blend; far pixels 0.985 weight + in-place 7x7 bound.
  A sub-3-px feature that is new each frame reaches the screen at about 0.1 of
  its scene value [I from resolve.hlsl, bolt-footprint.md section 1]; a feature
  with a >= 3 px lit interior survives at about its local minimum on its first
  frame; a vanished feature's history is clamped into the dark neighbourhood
  and is gone within one frame. Sentinel pixels are reprojected by the camera
  transform (policy 2), so unrouted effects do have history. Run332: 120/120
  sparkles clamped by the 3x3 [M].
- The proxy already: substitutes the stream-0 vertex buffer of the admitted
  bullet draw (chase view only, 60-100 us per draw, about 0.2 ms per firing
  frame at 30 bolts [M components, I total]); re-blends screen to additive with a
  gain for the nine SM1 bullet pairs (`--screen-emission-additive`, in place,
  before the resolve) and for the twenty SM2 effect pairs
  (`--emission-source-gain`, Screen substitution 2026-09-16, DESTBLEND ONE);
  shadows the bound textures per stage (`composition_textures_`) but does not
  fingerprint texture content; keeps the previous frame's R32F depth history
  (`TemporalPass::depths_[current_]`, returned in `Output`) alive across the
  next frame's scene [M source].
- Depth: D24X8 is not sampleable; RESZ works on Preview but samples through a
  comparison decoder (about 26 fetches) and is not guaranteed on native drivers
  (depth-resolve-backend.md, platform-portability.md). RT2 is exact and free
  once unbound.
- Overlay catalogue: the resolver `0x004e7590` takes a loose file first, else
  the highest catalogue holding the stem; the LOD baker already installs bodies
  and DDS atlases into `addon/05`/`06` (lod-overlay.md, body-format-bob1.md
  section 7). New textures and bodies therefore load natively with zero
  per-frame cost; effect *materials* (blend state, effect source) live in the
  body's MATERIAL6 record, so a replaced body can also change its blend.
- Full-screen reference costs at 5120x1440 under `--gpu-sync-timing` [M,
  fog-gpu-cost.md Run 77 C]: `fog_composite` 1.15 ms, TAA resolve 2.81 ms,
  the S1 copy 0.02 ms; each sub-pass carries a 0.264 ms floor. Per-slot rule of
  thumb: about 1 us per ps slot per full-screen pass (AGENTS.md).

## 2. Mechanisms

| Id | Mechanism | Exists | Hot-path cost | Native Windows |
| --- | --- | --- | --- | --- |
| M1 | In-place program substitution of a game draw, keyed on the exact VS/PS pair + draw shape (`FogCardShape` style) + material identity | yes for pair + shape (bolts, fog cards, source gain); **material identity is new** (section 2.1) | one SetPixelShader/SetVertexShader pair per matched draw, restored after (the route's bracket) | documented; the variant is ps_3_0/vs_3_0 built at creation |
| M2 | Substitute stream-0 vertex buffer for a game draw | yes (`bolt_footprint_core.h`, proxy-owned dynamic VB) | CPU per instance; measured 27 us per 3,630-vertex projection | documented (dynamic VB, DISCARD) |
| M3 | New textures / bodies / materials through the overlay catalogue slot | yes (LOD baker, `lod_overlay.py`) | zero per frame | identical by construction |
| M4 | **Deferred proxy effect queue**: requests recorded during the scene, drawn at step 0 of the scene-end hook, ONE/ONE, depth test on against the still-bound D24X8, Z write off, RT2 bound at a sampler for soft fade, one dynamic VB, one or two programs | new; the hook, the state block and the RT2 unbind exist | one VB lock + N draws; additive blending is commutative so drawing after every game transparent is exact for the additive term (only the game's own later screen/alpha draws would have composited over ours: a small ordering error, accepted) | documented; RT2 sampling sits behind the existing `NumSimultaneousRTs >= 3` + R32F gate that TAA already requires |
| M5 | Proxy-side particle simulation spawned from game draw parameters (position/age from the impact sprite; nozzle position from the engine glow) | new, CPU side | O(particles) per frame; hundreds, not thousands | portable C++ |
| M6 | Screen-space post (heat distortion, depth-aware glow) after the resolve, before bloom, **scissored** to the effect's bounding rectangle | new; the post chain's brackets exist | proportional to area: full-screen about 1.0-1.2 ms [I from `fog_composite`], a 1000x1000 region about 0.15 ms [I] | documented (scissor test, StretchRect of the region) |
| M7 | Own-draw motion routing: a proxy draw writes RT1 (previous clip position) and RT2 (its depth) so the resolve reprojects it by its own motion | new; the MRT contract exists for routed hulls | free at draw time; disturbs the depth history under the particle for one frame | documented MRT |

Depth source decision for soft fade: **RT2 at the scene end (M4)**. It is exact,
current-frame, one tap, already unbound at step 0. For in-place substitutions
(M1) that must draw mid-scene, the previous frame's R32F history is the fallback
(one frame old; at 60 fps and 200 m/s a hull edge lags about 3 m, inside a
0.5-2 m fade band only when the camera is close; acceptable for glows, not for
the ripple, which therefore goes through M4). RESZ loses on both cost (26
fetches) and native guarantee. Unbinding RT2 mid-scene at the opaque/transparent
boundary loses because no such boundary exists: the sorted queue interleaves
immediate and deferred submissions without a blend partition
(emission-draw-order.md).

### 2.1 Material identity (the one new piece of plumbing)

Every DEFAULT-technique effect draw shares one pair, so the class (beam, impact,
explosion, engine) must come from the *material*. The proxy sees the bound
stage-0 texture pointer; it needs a registry `texture -> class` filled at
creation/upload: dimensions + format + a hash of level 0 at the first Unlock
(managed and dynamic uploads already pass through the portable upload observer)
or, for DEFAULT-pool textures filled by `UpdateTexture`/`UpdateSurface`, at that
call (documented). The reference hashes come from the archive textures (the
LOD baker's texture lookup already resolves names to bytes; hashes are
provenance and may be tracked, bytes may not). Fallback key when the hash is
unavailable: dimensions + format + draw shape (e.g. the 2-face impact quad).
This is also the key the overlay catalogue needs: a replaced texture gets a new
hash, so the registry must list both the vanilla and the overlay bytes. Cost:
one map lookup per DEFAULT-pair draw (a few hundred per frame at most), no
per-pixel cost. Unknown: whether the game fills effect textures through Lock or
UpdateSurface; the F8 capture's allocation records answer it.

## 3. Per class

Each block: reference look; mechanism; data available; cost at 5120x1440; TAA;
native Windows risk; rank (visual payoff per ms).

### 3.1 Projectiles / bolts

Reference: Everspace 2 and Star Citizen draw a bolt as a saturated HDR core
(3-8x display white, bloomed) inside a soft coloured halo, stretched along its
velocity by 2-4 lengths so it reads as a streak even at 1-2 px width, with a
faint additive trail. Elite's multicannon rounds are similar with a shorter
stretch; nobody uses the crossed-card mesh look.

Mechanism: M2 + M1, extending the built bolt footprint. Per admitted bullet
draw, replace each instance's crossed cards by two camera-facing quads built
from the instance's world-space axis (the footprint already computes the
area-weighted major axis and its projection): a core quad (W_core about 3 px,
length = max(native, L) plus a velocity stretch) and a halo quad (3x wider,
0.25 intensity). Velocity: match instances to the previous frame's by nearest
centroid along the axis (median displacement 3.8-4.6 px/frame in run271 [M]; a
new instance has no match and gets the native length). Texture: a proxy-generated
128x32 A8R8G8B8 core/halo profile created at device creation (no game bytes,
no overlay needed); the game's own DXT5 could stay bound for the colour.
Program: a small ps_3_0 (about 20 slots) sampling the profile, colour = the
instance's D3DCOLOR alpha lane x a per-weapon tint taken from the game's
texture sample at the instance UV centre (keeps the weapon's colour without a
table), core output G_core (5-8) above 1.0 for bloom, additive (the existing
DESTBLEND ONE substitution). First person: apply the same shape (the footprint
is chase-only today because first-person bolts were already long; the new look
should be the same in both, gated by option).

Data: everything is in the drawn prefix (world positions, UV period, colour)
and the shadowed c0-3 + viewport; nothing new to read.

Cost: CPU 60-100 us per draw today [M], plus matching over about 50 instances
(negligible); the substitute VB holds 12 vertices per instance (two quads as
triangle lists) instead of the body's 24-234, so it shrinks. GPU:
about 60 quads of 20x60 px = 72 kpx of overdraw, under 0.01 ms [I]. Total about
0.1 ms per firing frame [I].

TAA: the core is >= 3 px wide and >= 12 px long, so the interior survives the
3x3 clamp on its first frame; the halo (dim, wide) survives as a low-frequency
term; the 1-px rim is attenuated (accepted, bolt-footprint.md section 1). No
reactive mask needed; M7 is not required. Trail ghosting at the previous
position is removed within one frame by the clamp.

Native Windows: none beyond the existing route (dynamic VB, ps_3_0).

Rank: **1** (highest payoff per ms: plumbing exists, flight evidence exists,
combat is where the user looks).

### 3.2 Beams

Reference: a beam is a hot white-to-colour core (HDR 4-6x) with a soft glow
sleeve, slow scrolling noise along its length, a flare at the muzzle and a
hit flare at the far end; Elite's beam lasers add slight heat distortion along
the sleeve.

Mechanism: M1 on the DEFAULT pair keyed on the `fx_beams_diff` /
`bullet_*` material identity: a ps_3_0 variant (about 40 slots) that derives
the across-beam coordinate from the UV lane the beam texture uses across its
width (unknown which; capture), outputs core = G x profile above 1.0 and a
sleeve, scrolls a 1-D noise along the length from the shadowed frame counter,
and re-blends to additive (Screen substitution). The muzzle/hit flares come
from M4 at the beam's end points (the mesh's extreme vertices along its axis,
read from the managed VB through the existing readers).

Data: the DEFAULT VS constants give the world-view-projection of the beam body
(the exact register layout of `d5e1c753` is not documented here: capture typed
constants), the managed VB gives the mesh.

Cost: a handful of draws, small area: under 0.02 ms [I].

TAA: beams are attached to the ship and move little frame to frame; a distant
1-2 px beam is cut like a bolt. Optional VS-side widening across the axis
(the same rule as the bolt footprint, applied in the VS from a per-draw
constant) fixes it; deferred to phase 2.

Native Windows: none.

Rank: **4** (cheap but rarely on screen for the player's own ship classes;
depends on unmeasured UV conventions).

### 3.3 Shield hit

Reference: Elite, Star Citizen and Everspace 2 show the hit as a bright flash at
the impact point that spreads as a hexagonal or ripple pattern over a Fresnel
shell around the whole ship, fading in 0.3-0.6 s; the shell itself is visible
only near the rim (Fresnel) and at the ripple front. X3 draws a screen-blended
sprite (2-face quad, `exp_PL_imp_diff` on 10659) at the hit position that is
neither on the hull nor animated beyond the material fade.

Mechanism: M1 to suppress/replace the sprite draw (keyed on the pair, the
2-primitive quad shape and the `exp_*_imp` material identity), plus M4 to draw
a **ripple decal**: a camera-facing quad at the sprite's world position with a
ps_3_0 (about 60 slots) that draws 2-3 expanding rings + a hex cell pattern in
the quad's own UV, fades with age, and soft-fades against RT2 depth
(`saturate((depth_scene - depth_quad) / band)` on both sides, so the quad
"wraps" the hull instead of cutting it). Age: the proxy tracks live impacts by
(position, material) across frames; a sprite draw not within r of any live
impact starts a new one at age 0. Colour: the shield hue by ship race is a
small table (or the sprite's own texture tint via the identity registry). The
true Fresnel shell (re-drawing the target's hull with a shield PS) is phase 3
(section 5): it needs the hit-to-ship association and the hull mesh redraw.

Data: the sprite draw's WVP constants (position and size), its fade lane
(COLOR0.x from `g_AlphaValue`), RT2 at scene end. The hit *normal* is not
available; the decal is camera-facing and the depth fade supplies the
hull-conformance.

Cost: tens of quads per combat frame, 60-slot PS on about 200x200 px each: under
0.02 ms [I]; CPU tracking negligible.

TAA: the flash is >= 3 px on its first frame (interior survives), moves with the
target; rings are low-contrast on their trailing side (clamp tolerant). No
reactive mask.

Native Windows: RT2 sampling behind the MRT gate; the decal degrades to a hard
edge without RT2 (TAA off), no other dependency.

Rank: **2** (the effect the user named first; small cost; needs one capture to
pin the sprite identity and lifetime).

### 3.4 Hull hit / sparks

Reference: Everspace 2 and Homeworld 3 spawn 20-40 short-lived spark streaks
(velocity-stretched, HDR orange-white, 0.2-0.5 s, no gravity in space), a bright
point flash, a little glowing debris, and leave a dark scorch decal; Elite adds
a puff of grey vapour.

Mechanism: M5 + M4. The same impact tracker as 3.3 (keyed on the hull-hit sprite
material once the capture names it; if hull and shield hits share a sprite, the
tracker distinguishes them by the target's shield state, which the proxy cannot
read: then both get the ripple + sparks look, weighted by a launcher ratio)
spawns N sparks with random direction in the hemisphere facing the camera-side,
speed 20-60 m/s, lifetime 0.3 s; each frame the CPU integrates positions and
writes camera-facing streak quads (length = speed x dt x stretch, width >= 3 px)
into the deferred VB; PS about 20 slots, additive, HDR 4x. Scorch decal and
debris are phase 3.

Data: impact position and age (3.3); no target velocity (sparks are left behind
a moving target: accepted at these lifetimes).

Cost: 40 sparks x 5 live impacts = 200 quads x about 100 px: under 0.01 ms GPU;
CPU about 200 integrations: negligible. Total under 0.02 ms [I].

TAA: streaks are new each frame at a new position; with width >= 3 px and length
>= 12 px the interior survives (same argument as bolts); their rim flickers a
little (accepted). M7 (own motion) would make them perfect and is the phase-2
experiment.

Native Windows: none.

Rank: **3** (shares the tracker with 3.3, so its marginal cost is nearly zero).

### 3.5 Explosions

Reference: modern explosions are flipbook or 6-way-lit sprite sheets with an
HDR core that cools from white to orange (bloom does the rest), soft depth fade
where the volume intersects hulls, a fast expanding shockwave ring that
refracts the scene, spark streaks and debris, and lingering smoke.

Mechanism: M1 on the DEFAULT pair keyed on the explosion material identity
(`expl.pbb`'s texture; the flipbook stepping is the game's TexAnim, which the
substituted PS keeps) for the HDR core (colour ramp on luminance, gain above
1.0) and the soft depth fade against the previous-frame depth history (in-place
draw, one tap; the lag is invisible on a 5-20 m fade band); M4 for the
shockwave ring (expanding decal, 3.3's program with other constants) and sparks
(3.4's system, larger N); M6 for the heat/shockwave distortion, scissored to
the explosion's projected rectangle, applied after the resolve. M3 (overlay) to
replace the flipbook with a higher-resolution sheet is optional and needs an
authored asset; not proposed for the first pass.

Data: explosion draw constants (position, size), the flipbook frame (the
`g_TexMatrix` rows c10-c11 already observed for the gate), previous depth
history, RT2 at scene end.

Cost: a large explosion covering 2000x2000 px at 5120x1440 with 2 layers = 8 Mpx
through a 60-slot PS: about 0.1 ms; the scissored distortion post over the same
rectangle: region copy + quad about 0.5 ms [I, scaled from `fog_composite`
1.15 ms full-screen]. Total under 0.7 ms during a big explosion, 0 otherwise.
A full-screen explosion (the player's own death) is the ceiling: about 1.5 ms
for one frame sequence [I].

TAA: large and slow, clamp-friendly; the distortion runs after the resolve so it
is neither smeared nor ghosted. Sparks as 3.4.

Native Windows: scissor + StretchRect of a sub-rectangle are documented; RT2 as
above.

Rank: **5** (best-looking payoff per event but rare, unmeasured draw shape,
largest cost per frame when present).

### 3.6 Engine trails / glows

Reference: Elite and Everspace 2 give thrusters a white-hot HDR core with a
coloured outer cone, flicker, a ribbon trail whose length scales with speed and
fades over 0.5-2 s, heat distortion behind the nozzle; Homeworld 3's trails are
long, soft, additive ribbons.

Mechanism: (a) gain and additive re-blend of the nozzle glow bodies through the
existing Screen substitution (`--emission-source-gain`) restricted to the
engine material identity, so the nozzle core goes above 1.0 (today the gain is
global to the pair); (b) M5 + M4 **ribbon trail**: for each engine glow draw the
proxy takes the nozzle position (the draw's world matrix x the body's centroid,
which is fixed per body and can be read once from the managed VB) keyed by the
object identity the motion route already uses (motion-history-key.md), appends
it to a per-nozzle ring buffer of 16 samples, and draws a camera-facing ribbon
through the last k samples (k from speed = position delta / dt) with additive HDR
colour taken from the glow's tint and width tapering from the nozzle radius to
0; (c) M6 heat distortion behind the own ship's nozzles in chase view, scissored
(phase 3).

Data: per-draw WVP constants and object identity (routed rigid draws carry
both), managed VB for the centroid, TexAnim state not needed.

Cost: about 30 nozzles in view x 16 segments x 2 tris = 960 tris, 20-slot PS,
under 0.02 ms GPU [I]; CPU ring buffers negligible. (c) about 0.15 ms when on.

TAA: the ribbon is wide near the nozzle and moves with the ship; its tail is
world-static and reprojects with the camera term (sentinel policy 2): fine.
Distant ribbons thin out and are attenuated (accepted).

Native Windows: none new. Caveat: the 22 nozzle materials on hull programs
(`standard_lighting.fx` ONE/ZERO etc.) are outside this and stay as they are.

Rank: **3** ex aequo with sparks by payoff, but phase 2 because the engine
material identity and the nozzle-centroid convention need the capture.

### 3.7 Generic particles (`0x004bf4c0`)

Reference: the ambient dust that gives speed cues is drawn in Elite and Star
Citizen as motion-stretched soft streaks (length from screen velocity), fading
with distance; smoke and vapour are soft, depth-faded, lit billboards.

Mechanism: M1 on the exact pair `36f98d15` / `222bee0d`: a vs_3_0 variant that
stretches the corner offset (TEXCOORD0) along the screen-space velocity of the
centre, computed from the current c0-7 and the proxy-retained previous view
rows (the VS constant query succeeds for every particle draw [M]); a ps_3_0
variant that keeps the SRCCOLOR/INVSRCCOLOR law (the pass sets its own blend;
changing it needs a blend substitution as for Screen) or re-blends to additive
with a small gain if the dust reads too dark in HDR. Whether this pass is dust
or also smoke/trails is unknown (one draw per frame, 21-61 quads, 256x256
X8R8G8B8 texture: consistent with dust only).

Data: c0-7, the previous frame's c0-3 (retain 64 B per frame), vertex data as
declared.

Cost: one draw, under 0.005 ms [I].

TAA: 1-2 px motes are cut to about 0.1 by the clamp; the stretch (>= 12 px
along motion) restores the interior along the streak but not across it; M7 is
the real fix and not worth it for dust.

Native Windows: none.

Rank: **6** (cheap but low visibility; do it opportunistically with the vertex
shader work of 3.1).

## 4. Rank

| Rank | Class | Payoff | Cost/frame at 5120x1440 | Prerequisite |
| --- | --- | --- | --- | --- |
| 1 | Bolts (3.1) | high, always on screen in combat | about 0.1 ms firing [I] | none (flight evidence exists) |
| 2 | Shield hit (3.3) | high, the named request | under 0.02 ms [I] | sprite identity + lifetime from one capture |
| 3 | Sparks (3.4) | medium-high | under 0.02 ms [I] | shares 3.3's tracker |
| 3 | Engine trails (3.6) | medium-high, always on screen in chase view | under 0.02 ms [I] | engine material identity + centroid |
| 4 | Beams (3.2) | medium | under 0.02 ms [I] | UV convention |
| 5 | Explosions (3.5) | high per event, rare | 0.1-0.7 ms during an explosion [I] | draw shape + texture identity |
| 6 | Dust (3.7) | low | under 0.005 ms [I] | none |

Phase-1 budget: 3.1 + 3.3 + 3.4 about 0.15 ms per combat frame [I], below the
existing bolt footprint's 0.2 ms CPU which the new bolt path subsumes.

## 5. Phased plan

**Phase 1: bolts + impacts (shield ripple and sparks).** Builds M4 (deferred
queue, RT2 sampler), the impact tracker, the texture identity registry (2.1),
the bolt quad builder on top of `bolt_footprint_core.h`, and the two small
programs. Options: `--fx-bolts`, `--fx-impacts` (launcher defaults on for
modded launches after the flight; nothing under `--vanilla`), one
`fx_effects` telemetry row per 300 frames (bolts written, impacts live, queue
draws, CPU us). Fixtures before a flight:

- Host: `bolt_footprint_core` oracle extended with the quad builder (period
  detection unchanged, axis and stretch checks, matching of synthetic instance
  streams), impact tracker unit tests (spawn, age, merge within r, expiry),
  texture identity registry tests (hash at Unlock/UpdateSurface, fallback key).
- GPU (Wine, `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py
  python3 verification/probe/run_temporal_pass.py` style, a new
  `run_fx_effects.py`): a synthetic scene with a hull plane in RT2, a ripple quad
  intersecting it (soft-fade profile against a CPU reference within 1e-3), spark
  streaks at 3 px width, bolt quads; the same scene through the resolve with an
  8-phase jitter and a 4 px/frame pan proving interior survival (>= 0.8 of scene
  value from frame 1 for the core, as run332's method measures sparkles) and no
  ghost beyond 1 frame at the previous position.
- Existing rows unchanged: `run_temporal_pass`, `run_linear_emission_sm1_packed`
  (the additive bullet route), host suite.

**The one combat flight it needs (before phase 1 is built; the same launch
serves phase 2's questions).** Launch with `--telemetry --taa-debug` and the
current defaults (no `--gpu-sync-timing`; it halves the frame rate and is not
needed). Fly a fighter with two weapon types (a bolt weapon and a beam if
available) against a shielded M4/M5 in the chase view. Press F8 (capture burst)
five times, each after about one second of the situation:

1. firing bolts at the target with its shields up, target 300-600 m ahead
   (bolts in flight + shield-hit sprites in the same frames);
2. firing at the same target with its shields down (hull-hit sprites/sparks;
   whether the sprite differs from 1);
3. the target's death explosion, camera 200-400 m away, from the first flash
   (explosion draw shape, texture dims, flipbook stepping, sparks, planes);
4. the own ship's engines from behind under full throttle, then after a
   full-stop boost (engine glow draws, nozzle geometry, TexAnim, the particle
   pass under motion);
5. a beam weapon held on the target for one second (beam mesh, UV).

What the capture must answer (open questions): the texture allocation
(dims/format) bound by each class so the identity registry can be seeded from
the archive; whether the shield and hull hits use different sprites; the
impact sprite's lifetime in frames and its fade lane; the explosion's draw
shape and whether `explosion_plane`/`exp_sparks` reach the DEFAULT pair, the
particle pass or nothing; the DEFAULT VS typed constants (WVP layout) for
position extraction; whether effect textures are filled through Lock or
UpdateSurface; the pre-resolve versus post-resolve luminance of bolts and
flashes (`--taa-debug` readback) to confirm the 0.1 attenuation number.

**Phase 2: engine trails, beams, dust, and the M7 experiment.** Needs the
engine material identity and nozzle centroid (capture 4), the beam UV (capture
5). Fixtures: ribbon builder oracle (ring buffer, speed-length law), beam PS
profile against a CPU reference, particle VS stretch against a CPU reference;
GPU: M7 on spark streaks through the resolve, comparing the own-motion route
against the unrouted draw (ghost length, interior value); ship M7 only if it
wins on both and the depth-history disturbance stays under 0.1 % of pixels.

**Phase 3: explosions and the polish.** HDR core + soft fade on the explosion
draw, shockwave decal, scissored heat distortion after the resolve, scorch
decal; the Fresnel shield shell (hull redraw of the target with a shield PS,
association by the impact position against the routed hulls' bounds); nozzle
heat distortion. Fixtures: scissored post cost measured on the GPU fixture at
5120x1440 (region sizes 500, 1000, 2000 px), shell redraw cost per hull. Needs
capture 3 and a second flight for the shell.

## 6. Options considered and why they lose

- **Replace the effects through the overlay catalogue only (new bodies and
  textures, no proxy work).** Loses: the game's effect shader is a ps_2_0
  texture x fade with no HDR gain, no depth read, no motion, so the look stays
  a flat card; blend states could be authored but nothing above 1.0 exists.
  Kept as an optional asset channel (M3) for explosion sheets.
- **Enhance in place only (PS variants on the game draws, no substitute
  geometry, no proxy particles).** Loses for bolts (the crossed cards are the
  problem, bolt-footprint.md), for sparks and trails (the game draws nothing
  to enhance), for the ripple (a screen-blended quad cannot conform to a hull).
- **A full proxy particle system replacing the game's particle pass.** Loses:
  the game's particles are one draw of 21-61 dust quads; there is nothing to
  gain and no smoke to replace.
- **Soft particles from RESZ mid-scene.** Loses: 26-fetch decoder per pixel,
  a mid-scene RESZ resolve per transparent burst, no native guarantee. RT2 at
  the scene end is exact and free.
- **A mid-scene pass at the opaque/transparent boundary.** Loses: no such
  boundary exists in the sorted queue, and a full-viewport bracket mid-scene
  cost about 2.3 ms per DIP at 1080p when prototyped (screen-emission-region.md).
- **Routing all effects to the motion buffers (M7 for every transparent).**
  Loses now: writing RT2 under transparents corrupts the depth history the fold,
  thin classification and fog read; ghosting of additive layers with wrong
  motion. Kept as a phase-2 experiment for proxy-owned streaks only.
- **Heat distortion before the resolve.** Loses: 1-2 px shimmer is inside the
  3x3 clamp and is smoothed away, and it jitters with the sample offsets.
- **Full-screen distortion pass.** Loses on cost (about 1.0-1.2 ms at
  5120x1440 [I]) against a scissored region at 0.15-0.5 ms.
- **Keying the class on program hash alone.** Impossible: one pair serves every
  effect material (effect-shader-users.md).

## 7. Unknown, and what settles it

| Unknown | Settles it |
| --- | --- |
| Texture identity per class, Lock vs UpdateSurface fills | capture 1-5 allocation records + archive DDS hashes (`texture_lookup` tooling) |
| Shield vs hull sprite, lifetime, fade lane | captures 1-2 with `--telemetry` draw records |
| Explosion draw shape; `explosion_plane`/`exp_sparks` path | capture 3; if absent from the DEFAULT pair, a Ghidra look at the loader's default effect for text bodies without a source name |
| DEFAULT VS `d5e1c753`: the position matrix is c0-3 (row-dot form, rows 0-3, 29 slots, one `if/else`; texcoord outputs 1-7 and c252-255 free: `verification/results/motion-output-profiles.json` `sm2_pairs[0]` [M this session]); whether it is a full WVP over model-space vertices (then the sprite position is the matrix's translation) or a VP over pre-transformed vertices is open | typed constants plus vertex ranges of the impact draw in capture 1 |
| Engine glow body centroid convention and the `INSTANCE` pair's users | capture 4 |
| The 0.1 attenuation of sub-3-px effects under the resolve | `--taa-debug` readback in capture 1 (bolt-footprint.md section 7 method) |
| Native Windows: R32F sampling, MRT 3, scissor + sub-rect StretchRect | cross-compile check now; the `faderoute` RT2 parity fixture on a native machine when one exists |
| Cost figures above | the GPU fixture's timing rows at 5120x1440 before the flight; `--gpu-sync-timing` pass rows on the flight only as attribution |
