# Effects modernisation: a proxy-owned effects stage

**Dropped (2026-09-25).** The user tried phase 1 and dropped it ("didn't like it; drop it"): the effect diversity and design stay the old game's, and tuning would take many hours. The phase-1 code (9375a1e7) is removed from production; this note stays as the record.

Design note, 2026-09-25 (Opus 5.5, high). **Ratified 2026-09-25 by the main session** as the architecture, with the second design (effects-modernisation-fable.md) merged in section 8. A second design was written
independently beside this one. Nothing here is built. It answers the user's request of 2026-09-25 ("replace /
modernize all weapon effects (hit shields etc), projectiles and engine trails ... make them look beautiful and
modern"; full replacement is allowed). That request supersedes the old "no soft particles" decision.
User preferences that still hold: cheap emitters above 1.0 over an exact blend law; 50-60 fps at 5120x1440;
decisions target 1920x1080; a little TAA ghosting is acceptable; documented D3D9 only.

Marks: **[M]** measured (the source is cited), **[I]** inferred, **[S]** static (code or archive). Costs are
for 5120x1440 (7.37 Mpx) unless a line says otherwise. Pixel sizes are given at 1080 lines and scale with
H/1080, except the TAA minimum core width, which is in render pixels.

## Outcome

Do not add a proxy-side effect in each game effect draw. Build **one effects stage**, owned by the proxy and
drawn once per frame at the scene end. It sits after the sun-shadow apply and the volumetric fog, before the
TAA resolve, on the FP16 scene target, with additive (ONE/ONE) blending.

- **Input.** Compact records. The proxy takes one from each recognised game effect draw and then **suppresses**
  that draw.
- **Data.** Current-frame lane depth (`.b` view depth) for soft intersections and occlusion. Proxy-side identity
  for age, trails and particles.
- **Geometry.** Analytic, from proxy-owned buffers: capsules for bolts, a Fresnel shell for shield hits, and
  later ribbons for trails, noise-sphere impostors for explosions and streaks for sparks.

The stage follows the dust-motes precedent: an unrouted, additive, RT2-occluded proxy draw at that slot. The
fixture measured **< 0.05 ms for 1,300 capsules [M]**, and HDR streaks at M = 2 kept **1.00 of their radiance
through the resolve [M]**.

**Phase 1** is projectiles/bolts and shield/impact hits: the best payoff per ms, and one combat flight settles
everything they need. That capture flight is in section 8.4 and the phase-1 implementation brief in section 9. The
second design was merged on 2026-09-25 (section 8). **Phase 2** is engine glows and ribbon trails, then
explosions with sparks and hit lights. **Phase 3** is beams, generic particles and opt-in heat distortion.

Expected cost (inferred):

- **Phase 1 GPU:** below 0.01 ms in a typical fight. The worst case is 0.07 ms, with a capital ship's shell
  filling the screen.
- **Phase 1 CPU:** net ≤ 0.1 ms, and likely a saving, because each suppressed game draw saves about 7.5 µs of
  Wine frontend time [M].
- **Stage fixed cost:** close to zero when the stage rides the render pass the fog or sun apply already has open.
- **Worst open cost:** if neither of those passes ran and the stage must open its own pass, up to about 0.36 ms
  [I, tile-GPU load and store of the FP16 target]. The phase-1 fixture must price this before a flight (section 2.2).

## 1. Facts the design stands on

| Fact | Source |
| --- | --- |
| Almost every effect uses one pair: VS `d5e1c753…` (WVP c0-3, **world c4-6**, view inverse c7-9, `g_TexMatrix` c10-11), PS `8360f422…` (texture × colour × affine fade, PS 2.0). This covers engine glow, gate, explosions (`expl.pbb`), beams (`fx_beams_diff`), impact/shockwave/shield-hit sprites (`objects/v/00518, 10659, 12007, 12009`), flares and dock glows | [effect-shader-users.md](../reverse-engineering/effect-shader-users.md) 57-204 [S]; [camera-numerics.md](../reverse-engineering/camera-numerics.md) 45 [S]; [position-shaders.md](../reverse-engineering/position-shaders.md) 35 [S] |
| Blend is per material: screen ONE/INVSRCCOLOR (92 engine glows, all bolts/beams, the impact sprites); additive (gate, explosions, 30 glows, flares); alpha (lens flares) | effect-shader-users.md 88-113 [S] |
| Bolts use their own pair (`5e484a06…`/`ec1f5c4a…`, `INSTANCE_BULLETS`). The CPU writes world-space crossed-card bodies (24-234 vertices per instance) into a DISCARD VB of stride 24, drawn non-indexed with one 512² DXT5 texture. No per-bullet centre or bound is kept | [effects-engine-remaining-emission.md](../reverse-engineering/effects-engine-remaining-emission.md) 242-318 [S]; [bolt-footprint.md](bolt-footprint.md) 36-60 [M] |
| The proxy already copies the drawn bullet prefix at Unlock (10-41 µs per scan, ≈17 µs per lock [M]). It groups instances by UV period, computes each instance's area-weighted axis and pixel centroid, and binds a substitute VB (`bolt_footprint_core.h`). It re-blends the bullets additive with a gain (`--screen-emission-additive 2` in the current launch) | bolt-footprint.md 60-66, 260-330, 437-500 [M]; [emitter-plan.md](emitter-plan.md) |
| The bullet draws come twice per bullet frame. Draw 9 comes after the depth clear that follows the far pass and before the ships; draw 90 comes after them. Whether both carry the same instances is not proven | bolt-footprint.md 500-520 [M]/[I] |
| Under TAA, bolts carry no motion. A 1-2 px dot reaches the screen at ≈0.1 of its scene value | bolt-footprint.md 69-92 [I] |
| Particle renderer `0x004bf4c0` (`36f98d15…`/`222bee0d…`, SM1.1): view-space billboards, stride 32, one draw per frame of 42-122 triangles, SRCCOLOR/INVSRCCOLOR, a 256² texture, no scope identity | [particle-motion-inputs.md](../reverse-engineering/particle-motion-inputs.md) 48-118 [M] |
| `explosion_plane.pbd` and `exp_sparks.pbd` name no effect source | effect-shader-users.md 307-309 [S] |
| Scene RT0 is FP16. RT1 is motion (`A32B32G32R32F`). RT2 is the lane: `A32B32G32R32F` with `.r` = z/w and `.b` = view depth w when the sun lane is on (it is in the user's launch), else `R32F` z/w. RT1 and RT2 are bound during the scene, so they cannot be read mid-scene | [hdr-scene-path.md](hdr-scene-path.md) 38-47 [S]; [shadow-receiver-depth.md](shadow-receiver-depth.md) 19-21 [S]; `motion_output.h:530` [S] |
| The R32F depth history (`depths_[prev]`) holds the **previous** frame's lane `.r` (device z/w), written by the resolve. It can be read during the scene, but it is one frame old and non-linear | [taa-mask-fold.md](taa-mask-fold.md) 50-61 [S] |
| Scene-end order: sun lane publish → candidates → votes → **sun-shadow apply** → **volumetric fog** (motes last) → `resolve_hdr` (resolve, bloom, AgX, meter) | `motion_output.cpp:2046-2064` [S]; hdr-scene-path.md 364-392 [S] |
| Dust motes are drawn at the end of the fog transaction on the still-bound FP16 target: no `SetRenderTarget`, additive, occluded per pixel by RT2 `.b`, unrouted, jittered projection. Measured: < 0.05 ms for 1,300 capsules. A 5×24 px streak at 8 px/frame keeps 1.00 of its radiance at M 2, which is above the stabiliser's emitter bound E 1. At M 0.8 it keeps 0.37-0.66. Over a sky flickering in every 3x3 it leaves a trail of 34 % of M that decays at 0.97 per frame | [fog-dust-motes.md](fog-dust-motes.md) 14-41, 299-309 [M]; [fog-gpu-cost.md](fog-gpu-cost.md) 60 [M] |
| Cost anchors: about 1 µs per ps slot per full frame at 5120x1440; 0.1 ms per full-screen 16-byte read at 1080p (330 MB/ms); 0.035-0.04 ms fixed per full-screen draw; ≈7.5 of the 8.75 µs of each draw is Wine's D3D9 frontend (run95) | AGENTS.md [M]; [taa-high-resolution.md](taa-high-resolution.md) 37-38 [M]; [effect-pass-replay.md](effect-pass-replay.md) 17 [M] |
| An overlay catalogue at the highest addon slot can replace any body or texture member (a loose file wins, then the highest catalogue) | [texture-lookup.md](../reverse-engineering/texture-lookup.md) 15-25 [S]; [merged-lod-feasibility.md](merged-lod-feasibility.md) 1015-1030 |
| Object boxes (object-space extent plus object → world rows) exist for routed draws in the caster-candidate route (`--shadow-replay-candidates`, in the user's launch) | [engine-frame-time.md](engine-frame-time.md) 1577-1612 [S]; [shadow-cascades.md](shadow-cascades.md) 373 [S] |
| There is no dedicated capture of impacts, explosions, trails or particles in combat | brief; effect-shader-users.md "Open unknowns" |

## 2. The effects stage (shared by every class)

### 2.1 Records instead of draws

**Recognition, per draw, only on the effect pairs.** The draw path already knows the bound VS/PS identity. For
the effects pair `d5e1c753`/`89193868` + `8360f422` it adds a **texture key** for stage 0:

- **At upload, not at first bind** (merged decision, section 8.2). The ownership layer (`--ownership`, in the
  user's launch) already wraps textures and observes their level locks (`src/ownership/d3d9_ownership.cpp`) [S].
  At the first Unlock of level 0 (or at `UpdateTexture`/`UpdateSurface` for a DEFAULT-pool fill), it computes a
  sparse 64-bit content hash: the level-0 bytes at 16 fixed offsets (about 4 KB), plus width, height and format.
  The key is stored on the wrapper node, so it dies with the texture. The draw path reads it with no device call.
- **Fallback, only for a texture whose upload was not observed:** one `LockRect(0, D3DLOCK_READONLY)` at first
  bind, hashed the same way.
- Without `--ownership`, the stage recognises bolts only.

A shipped table maps key → effect class and parameters, including the body's local extent and axis. It is data,
not code: `effect_keys.json`, generated offline by a new tool from the installed catalogues and any mod trees. The
mod trees are read-only and go through copied synthetic roots, never symlinks.

- An unknown key (for example a mod's own texture) draws natively and unchanged.
- Bullets need no key: that pair is only `INSTANCE_BULLETS`.
- The generic particle pair is keyed the same way, in phase 3.

**Record contents (about 96 B).**

- The class.
- The shadowed VS constants: WVP c0-3, world c4-6, eye c7-9 and `g_TexMatrix` c10-11. From these come the hit or
  explosion origin (world `.w`), the scale (row norms), the axis, and the flipbook or scroll phase.
- The material alpha and colour (vertex colour or PS constant).
- The key.
- The object-scope node handle and serial (`--object-trace`), when the draw has a scope. This is the identity
  that gives each effect its age.
- For bolts: the instance list, which the footprint path already derives (world centroid, world axis, length,
  width, UV centroid, alpha).

**Suppression.** An admitted record returns `S_OK` without forwarding the draw. The engine does not check the
effect draw's result (`0x004c403c`, [emission-draw-order.md](../reverse-engineering/emission-draw-order.md)
"Scoped invocation") [S]. Each suppressed draw saves ≈7.5 µs of Wine frontend time [M-derived].

A record is admitted only when all of these hold:

- the stage is **armed** for the frame (resources valid, FP16 blend capability, HDR redirect active, not after a
  failed stage within the last 64 frames);
- the selector is in the **Scene** phase, after the latching depth clear. Far-pass effects stay native because
  the far pass's depth is cleared before the stage runs;
- there is room: 256 records per frame. Overflow draws natively and is counted.

**Fail path.** If the stage fails at scene end, that frame's recorded effects are lost (one frame without them).
The stage then disarms for 64 frames and logs one row. Nothing is ever both drawn natively and by the stage.

### 2.2 Placement and depth

**Where.** `run_effects_stage()` runs after `run_volumetric_fog()` at both scene-end sites (the hook and the
bloom-copy fallback, `motion_output.cpp:1969, 2055`), before `resolve_hdr`. This is the second design's "step 0 of
the scene-end hook" slot (section 8.3): the same position, the same completed RT2, the same pass-opening question.
Merged decision: the stage runs as the first act inside `resolve_hdr`'s step-0 state bracket, after RT1 and RT2
are unbound. It does not capture a `D3DSBT_ALL` block of its own.

**Which pass it rides.** Sun apply is on in the user's launch, and fog is on in fogged sectors. When either ran,
the FP16 RT0 is already bound without RT1 and RT2, so the stage adds draws to that pass with no attachment
change: the motes' "no `SetRenderTarget`" case.

**When neither ran,** the stage must unbind RT1 and RT2 itself to sample the lane. On a tile GPU that can cost a
load and a store of the FP16 target: 2 × 59 MB at 330 MB/ms ≈ 0.36 ms [I, upper bound]. The phase-1 fixture
measures it. If it is above 0.1 ms, phase 1 falls back per class (section 8.3):

- **Bolts:** the second design's in-place path. The existing substitute vertex buffer is extended to carry the HDR
  core + halo quads, motion-stretched, drawn by the game's own bullet draw in the scene.
- **Shells:** an in-scene variant that draws at the hook inside the scene pass. RT1 and RT2 stay bound with
  `COLORWRITEENABLE1/2 = 0`, the hardware depth test does occlusion, and the soft fade comes from the previous
  frame's depth history reprojected through the camera. That is exact for static geometry and off by the
  object's own motion at moving silhouettes. It comes from the same source with one define.

**Occlusion.** `ZENABLE` LESSEQUAL with Z-write off against the game's D24X8, when it is still attached in the
pass the stage rides. That depth holds the whole near pass, routed or not. In the shader, the lane `.b` gives the
**soft** term: `vis = saturate((z_scene − z_frag) / (SOFT · r_effect))`, where the sentinel means no occluder and
`SOFT · r_effect` is the effect's own soft radius in view units. Where depth is not attached, the lane alone
occludes, as for the motes. Unrouted near-pass opaque objects then do not occlude; the conventional opaque union
is covered ([material-coverage.md](material-coverage.md)), so the gap is small [I]. The shader's fallback for an
`R32F` lane is `m32 / (d − m22)`, the fog's rule.

**Projection.** The jittered projection that routed draws use this frame (as the motes do). The resolve removes
the jitter.

### 2.3 Blend, HDR, bloom, exposure

- **Blend.** Every stage draw is ONE/ONE on the FP16 target, in engine space, with emitters above 1.0 (the user's
  choice over an exact linear law), and alpha is never written. Separate alpha stays keep-destination, so the
  compositor's alpha-authored glow is untouched.
- **The two non-additive layers** keep premultiplied `ONE/INVSRCALPHA` and draw first within the stage, so the
  additive draws land on top of them: smoke in phase 2, and in phase 3 the moved particle draw with its native
  SRCCOLOR/INVSRCCOLOR law and the source clamped to [0, 1].
- **Halo.** The core is a few units above E 1 (the stabiliser bound). Every shader draws its own halo analytically,
  because `--bloom-source-clamp` limits what bloom receives. The look therefore does not depend on bloom settings;
  bloom and AgX add the bleed and the white-hot desaturation on top.
- **Exposure.** The meter reads the resolved image, so large explosions pull exposure down. That is a "flash"
  that may be desired or may pump. It is open question 8, measured in a fixture and in the flight.
- **Fog.** The stage runs after fog, so effects are not fogged (as accepted for glows in
  [volumetric-fog.md](volumetric-fog.md) 183-186).
- **Ordering.** The stage draws over every in-scene transparent layer (glass, lens flares, nebula sprites). For
  additive content only the unattenuated-behind-glass case differs, and it is minor [I].

### 2.4 TAA interaction

Stage pixels are unrouted, like the motes. **No reactive mask** is planned for phases 1-2.

**Why no mask is needed.**

- Cores above E 1 kept 1.00 of their radiance through the live resolve configuration (far camera, strict, exit
  0.25, stabiliser 0.7) [M, motes temporal row].
- Every core is kept at least 3 render px wide (the bolt-footprint argument: the centre pixel's 3x3 stays lit).
- Every moving element is streaked to cover its previous position, so the clip box holds the bright value.
- Shells and hit lights on a hull take the hull's own motion through the resolve's closest-depth dilation, so they
  accumulate as if attached.

**Residual risks.**

- Halos below E show at 0.37-0.66 [M]. Their gain accounts for that.
- A fast bright element crossing a sky that flickers in every 3x3 leaves a trail decaying at 0.97 per frame [M,
  synthetic worst case]. The flight checks it against real starfields.
- The escalation, only if a flight shows smears, is a phase-3 reactive mark (section 6, option H).

### 2.5 Programs, resources, lifetime

**Programs.**

- vs_3_0 and ps_3_0, created at the latch together with the fog and motes resources, not at first use. A first
  explosion must not hitch: a cold 4k-slot compile took 0.65 s [M, AGENTS.md].
- Each program stays within a few hundred slots. `MaxPixelShader30InstructionSlots` is logged as today.

**Buffers and textures.**

- One dynamic VB of 64 KB (`D3DUSAGE_DYNAMIC | WRITEONLY`, DEFAULT pool, one DISCARD lock per frame).
- One static icosphere VB and IB (320 triangles).
- One 64³ L8 noise volume and one 256² A8R8G8B8 tiling noise. The DLL generates both at creation (no files, no
  overlay). They are DEFAULT pool, filled via `UpdateTexture` from SYSTEMMEM, as the motes avoid MANAGED.

**Lifetime.** Released at Reset and recreated at the next latch (the motes' rule), and counted in
`allocations()`. The stage's state bracket is one `D3DSBT_ALL` block, the fog's pattern.

**Proxy-side state, host memory, O(visible effects).**

- An open-addressed map from node serial to {first frame, last frame, key, class, and the last world transform}.
- Per-nozzle ring buffers for trails (phase 2).
- A fixed pool of 2,048 CPU particles (phase 2).
- The previous frame's bolt instance list, used for velocity.
- An entry not seen for its fade time is dropped.
- Everything resets on the resolve's camera cut, SETA exit, sector change and load.

### 2.6 Native Windows

Everything is documented D3D9:

- FP16 post-pixel-shader blending, checked with `CheckDeviceFormat`;
- sampling the lane after it has been unbound;
- `LockRect(READONLY)` on MANAGED textures (the game uses a plain D3D9 device, not D3D9Ex);
- `SetPrivateData`/`GetPrivateData`;
- volume textures (`D3DPTEXTURECAPS_VOLUMEMAP`; the fallback is 2D tiled noise);
- `COLORWRITEENABLE1/2` (independent write masks, already required by the route).

The EXE seams it uses (object scope, object trace, the bullet writer's buffer) are validated at exact sites and
are the same on Windows. On an immediate-mode desktop GPU the "stage opens its own pass" cost is much smaller
than on a tile GPU [I]. None of this is verified on native Windows, and the gap belongs in
[platform-portability.md](platform-portability.md) when phase 1 lands.

## 3. Per effect class

Every cost is [I] from the anchors in section 1 unless marked. "f" is the screen fraction covered and "S" the
pixel-program slots; GPU ≈ f · overdraw · S µs.

### 3.1 Projectiles / bolts

**Reference look.** Tracers read as HDR capsules: a white-hot core a few pixels wide, a coloured halo, and a
motion-stretched tail, so they read at any distance with bloom on top (Everspace 2, Elite Dangerous). They never
shrink below a readable streak, and they fade softly where they end in a hull.

**Mechanism.** The bullet draws (both draw 9 and draw 90) are recorded and suppressed. Instances come from the
existing locked-prefix copy and period grouping.

- **Velocity.** Each instance is matched to the previous frame's instances on the same UV period. The match is
  the nearest centroid on the backward axis line `c − s·a`, with s in (0, s_max] and a perpendicular distance
  below ε; that gives v. The match is refused when two candidates are closer than 2|v|Δt, when a cut occurred,
  or when the draw has no period.
- **Why matching is unambiguous.** Streams are spaced 100-330 m apart (3-10 shots/s at ~1,000 m/s), against
  17 m of travel per frame at 60 fps [I].
- **Geometry.** The VS builds one camera-facing quad per instance along the projected axis. The length is
  `max(native length, L_min, |v|Δt·k_stretch)`; the width is `max(native width, W_min)` plus the halo. The
  footprint rule moves from the CPU rewrite into the VS: W_min = 3 render px, L_min = 12 px at 1080 lines.
- **Shading.** The PS evaluates a capsule SDF with analytic AA (`fwidth`). The core is white at I_core ≈ 6-10
  engine units; the halo is `exp(−d/σ)` × tint × I_halo.
- **Tint.** The native bullet texture sampled at the instance's UV centroid with `tex2Dlod` at mip 4, so mod
  bolt colours carry over.
- **Instance alpha.** The vertex alpha (`object+0x13c`).
- **Soft end.** Against the lane depth.
- **Unmatched instances** get a symmetric capsule with no tail.

**Data.** The locked-prefix copy (positions, UVs, alpha), the shadowed c0-3 and viewport, and the previous
frame's instance list.

**Cost.**

- GPU: about 60 instances × ~2,000 px ≈ 1.6 % of the frame at S ≈ 45 → < 0.001 ms, plus one draw.
- CPU: the existing scan (10-41 µs [M]) plus period and moments (existing), matching (n ≤ 64, ~10 µs) and a VB
  write of 12 KB. Net ≤ 0.1 ms per firing frame, minus two suppressed draws (−15 µs).
- This is cheaper than today's footprint, which rewrites every vertex.

**TAA.** The core is above E 1, at least 3 px wide, and streaked over its previous position, so it kept 1.00 of
its radiance in the motes' measurement. This replaces the ≈0.1 cut [I]. The halo shows at 0.37-0.66 and is
gained to compensate.

**Windows risk.** Low: it is the same substitute-buffer path as today.

**Open.** Chase view only, or first person too? The footprint deliberately left first person unchanged. A
modern look argues for both, with a per-view switch; **the user decides**.

### 3.2 Beams

**Reference look.** Continuous beams are a very bright thin core inside a wider coloured sheath. Energy noise
scrolls along the length, the width pulses slightly, and a flare burns where the beam meets its target
(Homeworld 3, Star Citizen).

**Mechanism.** Keyed on `fx_beams_diff`. The start point and axis come from world c4-6. The length comes from the
axis row norm times the body's local extent, taken from the key table. The draw is suppressed and replaced by
one camera-facing ribbon: capsule core, sheath, and scrolling noise whose phase comes from `g_TexMatrix` or time.
An end flare sits at the far end with a soft depth fade.

**Data.** World rows, the texture matrix, and the body extent (offline).

**Cost.** Below 0.001 ms per beam (≈50k px, S ≈ 60).

**TAA.** As for bolts. Beams are long and above E 1.

**Windows risk.** Low.

**Unknown.** Which weapons draw with `fx_beams_diff` (the capital-ship beams, Goner TL, Khaak lightning use
other materials), and whether the engine scales the body to the hit distance. It needs one F8 with a beam firing.

### 3.3 Shield hit

**Reference look.** A hit lights an animated ripple on a shield shell around the ship. The shell is invisible
until hit, has a Fresnel rim, and carries a hex or noise pattern. Rings expand from the impact point and fade in
about half a second, with a bright flash at the point (Elite Dangerous, Everspace 2).

**Mechanism.**

- **Keying.** On the shield-hit sprite key. Which of `00518/10659/12007/12009` is the shield hit is open; the
  offline texture names may settle it, and the combat census does.
- **Hit point P.** The sprite's world origin (c4-6 `.w`).
- **Owner ship.** The object box, from the caster-candidate route, whose inflated ellipsoid is nearest to P. The
  ellipsoid is the box half-extents × 1.2 through its object → world rows. The fallback is a sphere from the
  node's radius `+0xa0` ([lod-selection.md](../reverse-engineering/lod-selection.md)). When no owner lies within
  1.5 radii, the hit gets the second design's **ripple decal** instead of a shell: a camera-facing quad at P with
  rings and a hex pattern, soft-faded against the lane on both sides so it wraps the hull.
- **Age.** From the sprite's node serial: the first sighting is age 0. The ripple runs its own 0.6 s timeline,
  whatever the native sprite's lifetime.
- **Hit slots.** Up to 4 active hits per ship go into PS constants as the local direction plus the age.
- **Draw.** One draw per hit ship of the proxy icosphere (front faces).
- **Shading.** The PS computes:
  - the Fresnel rim, `pow(1 − |n·v|, 2.5)`;
  - per hit, a ring `exp(−((θ − c·age)/w)²) × (1 − age/T)` on the ellipsoid-normalised angle θ;
  - a hex/noise modulation from the tiling noise;
  - the tint (the native sprite texture's mip-5 average, so mods keep their colours);
  - a soft fade against the lane where the shell cuts the hull.
- **Flash.** A small HDR flash sprite at P for 0.1 s. The native sprite is suppressed.

**Data.** The sprite record (world rows, node serial, key), the ships' object boxes and rows, the lane `.b`.

**Cost.** GPU: a fighter at 300 m is ~0.6 % of the frame at S ≈ 110 → < 0.001 ms. A capital ship at close range is
60 % → 0.066 ms, only while hits are active. CPU: association ≈ 10 µs.

**TAA.** Where the shell lies over the hull it takes the hull's motion. The ripple is smooth, several px wide and
above E at its crest. The rim over sky takes the camera path.

**Windows risk.** Low for the shading. The caster-candidate route is also needed on Windows; without it, the
sphere fallback applies.

### 3.4 Hull hit and sparks

**Reference look.** An unshielded hit throws a spray of short, bright, velocity-stretched sparks. They cool from
white to orange. There is a brief flash, and a local light glows on the surrounding hull for a fraction of a
second, sometimes with a small smoke puff (Star Citizen, Everspace 2).

**Mechanism.**

- Keyed on the hull-hit sprite, which is suppressed.
- **Sparks.** 16-24 CPU particles spawn in the proxy's pool. The initial velocity is a cone around the local
  normal, approximated by the ellipsoid gradient at P, plus the ship's velocity from the node's frame delta.
  They have drag and live 0.25-0.5 s.
- **Drawing the sparks.** Streak quads from position to position − v·Δt·k, with a cooling ramp.
- **Hit light.** A **depth-aware glow**: one quad around P whose PS adds
  `L · exp(−|z_lane − z_P| / r) · falloff(|x − P|)` to pixels that have lane depth. No normals, so it is a soft
  omni light on the nearby hull. It lasts 0.15 s.
- **Flash.** An HDR core sprite for 0.08 s.

**Data.** As for 3.3, plus the node frame delta.

**Cost.** 240 streaks for 10 simultaneous hits: < 0.001 ms. Hit light: 10 × 2 % of the frame × S 25 → 0.005 ms.
CPU sim: ~10 µs.

**TAA.** Sparks are streaked, as for bolts. The hit light sits on the hull, so it takes the hull's motion.

**Windows risk.** Low.

### 3.5 Explosions

**Reference look.** A volumetric-looking fireball that brightens, boils and cools through a blackbody ramp into
dark smoke. It has a thin expanding shockwave ring, a spray of sparks and debris streaks, and a flash that lights
nearby hulls. It fades softly where it meets geometry (Everspace 2, Homeworld 3).

**Mechanism.**

- **Keying.** `expl.pbb` by texture key. `explosion_plane`/`exp_sparks` once the census shows how they draw.
- **Size and age.** Origin and size from world c4-6 and the body extent. Age from the node serial; the native
  flipbook phase in `g_TexMatrix` cross-checks the lifetime.
- **Replacement,** all in the stage:
  1. a camera-facing **noise-sphere impostor**: radial density plus 3-4 octaves of volume noise advected by age,
     a temperature → blackbody HDR ramp, and a soft lane fade (S ≈ 250-300);
  2. a **shockwave** ring (analytic, 0.4 s);
  3. 60-150 spark and debris streaks from the particle pool;
  4. the **depth-aware flash light** of 3.4, scaled up;
  5. optional smoke billboards (premultiplied, below E, drawn first).
- **Octave LOD** by projected radius bounds the fill cost.

**Cost.**

- A fighter's explosion at 500 m: 2 % × 2 layers × 300 → 0.012 ms.
- A capital ship's explosion at close range: 50 % × 3 layers × 300 → **0.45 ms** for 1-2 s. With the octave LOD
  above ~20 % coverage, about 0.22 ms.
- The phase-2 fixture decides whether large fireballs need a half-resolution offscreen variant (option E).

**TAA.** The fireball is large, smooth and above E. Smoke is below E and ghosts slightly over a flickering sky,
which is accepted.

**Windows risk.** Low. Volume-texture support is checked, with a 2D noise fallback.

### 3.6 Engine glows and trails

**Reference look.** Thrusters show a short, hot, throttle-dependent plume with a bright nozzle core. Ships leave
ribbon trails: thin, tapering, fading HDR strips that trace the flight path, sometimes with a heat shimmer
right behind the nozzle (Homeworld 3 trails, Elite Dangerous and Star Citizen plumes).

**Mechanism.**

- **Keying.** On the 122 `objects/effects/engines/*` material textures. The key table is generated offline, so
  no capture is needed for the keys.
- **Nozzle.** The world origin, axis and size from c4-6 of each glow draw.
- **Glow mesh.** Phase 2 keeps the native mesh in the scene, gained as today. It is the ship's authored nozzle
  shape, and the own ship is static on screen in chase view.
- **Additions,** in the stage:
  1. a **plume**: a cone impostor along −axis with an HDR core and noise flicker. Its length follows the glow's
     own scale or alpha if throttle drives them; open question 5.
  2. a **ribbon trail** from a per-node ring buffer of nozzle world positions. Samples are taken every
     max(1 frame, 1.5 m), 32 of them. The ribbon is camera-facing, its width tapers, the engine tint (texture mip
     average) fades out over 0.4-1.0 s, it fades near the camera so the chase view does not fill the screen, and
     it soft-fades against the lane.
- **Resets.** Node death fades the trail out. A cut or SETA exit clears the buffers.

**Data.** The glow records, node serials and the camera.

**Cost.** 30 nozzles × ~3,000 px of ribbon plus plume at S 40-60 → < 0.002 ms GPU. CPU ring buffers: ~20 µs.

**TAA.** Ribbons overlap themselves from frame to frame (the motes' streak argument). The plume sits on the
ship.

**Windows risk.** Low.

### 3.7 Generic particles

**Reference look.** Smoke and dust are soft, depth-faded volumes that never show a hard line where they cut
geometry. Emissive particles are HDR and additive; smoke is premultiplied and dims what lies behind it.

**Mechanism.**

- Copy the particle draw's prefix: 4-12 KB [M], through the locked-prefix scan used for bolts.
- Suppress the draw.
- Redraw it in the stage with the native VS layout, a soft-particle PS (lane `.b`) and the native
  SRCCOLOR/INVSRCCOLOR law with the source clamped.
- Once the census shows what it draws (missile smoke? dust?), reclassify by texture key.

**Data.** The VB prefix, c0-7 (view and projection), the 256² texture.

**Cost.** < 0.005 ms.

**TAA.** Unchanged from today.

**Windows risk.** Low.

### 3.8 Heat distortion (screen-space, cross-class)

**Reference look.** The air shimmers behind engines and in explosion shockwaves (Star Citizen, Everspace 2).

**Mechanism.**

Changed by the merge (section 8.5): distortion runs **after the resolve, before bloom**, scissored to the
producers' union rect. It does not belong in the pre-resolve stage: a 1-2 px shimmer before the resolve is
clipped by the 3x3 and jitters with the sample offsets.

1. A rect-limited `StretchRect` of the resolved output to a scratch copy.
2. One refraction draw over that rect. The UV offset is the gradient of animated noise times the producer's mask.

**Cost.** The copy is 16 B/px over the rect: a 10 % rect is 0.036 ms, plus the draw. The pass boundary on the
resolved output is up to about 0.36 ms on a tile GPU [I]. The second design scales it from `fog_composite` to
0.15-0.5 ms per region.

**Verdict.** Last in rank, opt-in, and only while a producer is on screen.

**Windows risk.** Low: `StretchRect` with rects between RT surfaces is documented.

## 4. Rank by visual payoff per ms

Payoff is the designer's 1-5 judgement of how much "modern" the class adds in normal play. Costs are GPU [I]
(CPU where it dominates).

| Rank | Class | Payoff | Typical | Worst | Needs a capture first |
| ---: | --- | ---: | --- | --- | --- |
| 1 | Projectiles / bolts | 5 | < 0.001 ms GPU, ≤ 0.1 ms CPU (net saving likely) | same | no (run271/273/279 suffice) |
| 2 | Shield hit | 5 | < 0.001 ms | 0.066 ms | sprite identity (the combat flight) |
| 3 | Engine trails and plumes | 4 | < 0.002 ms, 20 µs CPU | same | throttle behaviour only for the plume length |
| 4 | Hull hit, sparks, hit light | 3 | 0.005 ms | 0.01 ms | sprite identity (same flight) |
| 5 | Explosions | 5 | 0.012 ms | 0.22-0.45 ms for 1-2 s | yes: the draw path of `expl`/`explosion_plane`/`exp_sparks` |
| 6 | Beams | 3 | < 0.001 ms | same | which weapons use them |
| 7 | Generic particles | 2 | < 0.005 ms | same | what they are |
| 8 | Heat distortion | 2 | 0.04 ms | 0.4 ms | no, but it is a pass boundary |

## 5. Phased plan

### Phase 1: the stage, projectiles, shield hits

**Build.**

- The stage (2.1-2.5) with an `--effects-stage` option (launcher default off until the flight). It gets its own
  Ctrl+Shift hotkey that toggles the stage and restores native forwarding, for A/B within one fight.
- `tools/effects/effect_keys.py` and `effect_keys.json`.
- The runtime key: sparse hash plus private-data tag.
- Bolts (3.1).
- Shells (3.3), keyed on the offline candidate keys, behind `--effects-shields`.
- `--effects-census` telemetry, F8 frames plus per-frame counts:
  - `effects_stage_frame`: records, suppressed, forwarded, overflow, draws, the pass it rode, armed;
  - `effect_draw` on capture frames: class, key and name, pool/format/size, blend triple, node/serial/model,
    scoped, phase, world origin, row norms, `g_TexMatrix`, alpha, primitives;
  - `effect_node` at node death: key, first and last frame, max scale;
  - `bolt_match`: instances, matched, refused, the median of |v|, and a draw-9/draw-90 instance hash;
  - `shield_hit`: owner node, distance of P to the ellipsoid, slot.

**Fixtures before the flight.**

- **Host,** `PYTHONPATH=verification/probe` with the selected modules:
  - key-table determinism and mod precedence, run on copied synthetic roots;
  - bolt matching: synthetic streams recover v within 1 %, refuse the ambiguity case, and reset on a cut;
  - shell association and the 4-slot policy;
  - census parser.
- **GPU under Wine,** a new `run_effects_stage.py`, run as
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_effects_stage.py`,
  on a synthetic FP16 scene, lane, D24X8 and depth history, at 1920x1080 and 5120x1440:
  - **Bolt capsule.** Core width ≥ 3 px and length ≥ L at both resolutions. Energy relative to the native card
    within a set band. Occluded by lane or hull depth, and soft at the end.
  - **Moving bolt and shell through the real resolve,** 8 jitter phases at 0/4/8 px per frame. Core
    output/current ≥ 0.9. Ghosting ≤ 3 px over dark sky (the motes gate).
  - **Shell.** Rim, four rings, and the soft cut against the hull.
  - **Off path.** With the stage off or toggled off, the frame is byte-identical with the same call count, and
    the native draws are forwarded.
  - **Suppression witness.** Armed means not forwarded; disarmed or failed means forwarded.
  - **Recovery and refusal.** Reset releases and recreates. A missing FP16 blend capability refuses the stage
    and forwards natively.
  - **Timing** with `D3DQUERYTYPE_TIMESTAMP`, which is documented and used in the fixture only. Targets: 60 bolts
    plus 2 shells ≤ 0.05 ms; a full-screen shell ≤ 0.1 ms; the price of opening the stage's own pass, which
    decides the in-scene fallback of 2.2.
  - **Slot counts** logged.
- **Key fixture (Wine).** D3DX-load three effect `pck` textures through the ownership wrapper, and check that
  the upload-time sparse hash equals the table's. The same check covers the first-bind fallback.
- **Fog and route bridge.** Run them if the stage touches the fog transaction (the stage sits after it, but the
  memory rule requires the bridge for fog-adjacent changes).
- `run_temporal_pass` is not needed: the resolve is unchanged.
- **Cross-compile** the DLL with mingw: Windows-compatible source, not verified behaviour.

**Flights.** The capture flight (section 8.4) flies now, on the installed build, while phase 1 is built. It
decides the shield/hull keys, the explosion path and the throttle behaviour.

The phase-1 look flight flies after the candidate: one launch with `--effects-stage --effects-shields`, a
Ctrl+Shift hotkey A/B during a fight, and one F8 in first person with fire held (the view rule and the cockpit
glass ordering). The user reports:

- the look of bolts and shield ripples;
- the fps-overlay change under the hotkey (target: none visible, ≤ 0.1 ms);
- any trail over starfields;
- any exposure pumping.

### Phase 2: engine trails and plumes, explosions, hull hits and sparks

**Prerequisites from the flight.** The census gives:

- the sprite keys (shield, hull, shockwave);
- the explosion draw paths and lifetimes;
- whether explosion and impact draws have a scope, and what phase they draw in;
- the glow's throttle behaviour.

**Build.** The CPU particle pool, the trail ring buffers, the plume, the fireball impostor with octave LOD, the
shockwave, the depth-aware hit light, and the hull-hit sparks.

**Fixtures.** Host: sim determinism; trail sampling, cut and death; explosion timeline. GPU: fireball cost at 10,
30 and 60 % of 5120x1440; trails in chase view with the near fade; 60 frames through the resolve with the ghost
bound; the meter's response to a 2 s explosion covering 30 %.

**Decision point.** If the fireball at 30 % costs more than 0.2 ms, build option E for fireballs only.

**Flight.** One launch covering trails, explosions and hull hits.

### Phase 3: beams, generic particles, heat distortion; reactive mark only on evidence

Beams and particles use the phase-2 fixtures' pattern. Heat distortion is opt-in and priced by its fixture. The
reactive mark (option H) is built only if the phase-1 or phase-2 flights show trails over starfields.

## 6. Options considered and why they lose

**A. In-place shader substitution only.** Keep the game geometry and draw order. Swap the PS per texture key, and
take the soft fade from the previous-frame depth history.

- It loses because the history is last frame's device z/w: silhouettes are wrong under motion, and every pixel
  needs a reprojection.
- The geometry stays the native crossed cards and sprites, so there are no capsules, ribbons or shells to draw.
- The ≈0.1 TAA cut and the per-draw state cost stay.
- It remains the right tool for effects the stage does not replace (gate, flares, dock glows): today's gain path.

**B. Post-resolve effects layer** (crisp, outside TAA).

- It needs a render pass on the full-resolution FP16 output. At 5120x1440 that is ≈0.36 ms of load and store on a
  tile GPU [I].
- Alternatively it can be folded into the resolve's pass, but then it contaminates the next frame's history. That
  needs either a second "undo" rasterisation in a later pass on the output, or a resolve change that rejects
  marked history.
- It also loses TAA's smoothing of procedural noise.
- Pre-resolve with cores above E is measured to survive, so B buys little for its cost.

**C. Deferred replay of the game's own effect draws at the stage.** This retains the game's VB/IB and replays
constants.

- It keeps the native art but modernises nothing about shape.
- It reintroduces retained game resources across the frame, which the project direction retired ("replay/admission
  is reference only").

**D. A mid-scene depth copy at the opaque/transparent boundary,** for in-scene soft particles.

- There is a sorted list per bucket, so possibly several boundaries per frame.
- Each is a pass break plus a copy of the 118 MB `A32B32G32R32F` lane at 5120x1440, ≈0.36 ms or more [I].
- It pays that every frame for what the stage gets for free after the scene.

**E. Half-resolution offscreen effects target composited in bloom-extract and AgX** (GPU Gems 3 ch. 23).

- A fixed cost whenever it is active: a clear plus two reads of a 14.7 MB target, ≈0.1-0.2 ms [I].
- Six bloom-extract variants plus AgX gain a sampler.
- Thin cores blur.
- It is kept only as the phase-2 fallback for very large fireballs, if their measured fill cost is above 0.2 ms.

**F. Replacement art through the overlay catalogue** (new flipbook textures and bodies for effect objects).

- The effects stay flat sprites with the engine's timing and geometry.
- High-resolution flipbooks cost texture memory.
- Art has to be authored or generated.
- Mods that ship their own effect textures collide with the slot rules.
- Procedural shaders plus two generated noise textures give more for less. The overlay stays available if a
  specific art texture is ever needed, such as a hex pattern.

**G. GPU particle simulation.** D3D9 has no compute, and VTF/R2VB is format-limited and not portable. CPU
simulation of ≤ 2,048 particles costs tens of µs.

**H. A reactive mask for effect pixels** (bind RT3 through the scene, or mark the age target at the stage and
teach the resolve to reject marked history).

- Binding RT3 costs 29-59 MB of attachment bandwidth every frame.
- Marking the age target is a change to the most-tuned program in the renderer.
- It is not needed while cores stay above E and at least 3 px wide [M motes]. It is held in reserve for phase 3 on
  flight evidence only.

## 7. Unknowns, and what settles each

| # | Unknown | What settles it |
| ---: | --- | --- |
| 1 | Which sprite is the shield hit, the hull hit or the shockwave; their lifetime and animation (`g_TexMatrix` or scale). The body facts are measured (section 8.1): 10659 is a 2-face quad with `exp_PL_imp_diff`, 00518 is 70 faces with `fx_bullets2_diff`, and 12007/12009 have both `.pbb` and `.pbd` | Capture flight F8s 1-2 (section 8.4) |
| 2 | How `explosion_plane.pbd` and `exp_sparks.pbd` draw (which effect a sourceless `MATERIAL6` gets), and `expl.pbb`'s lifetime | Census at flight F8 4, plus a targeted disassembly of the body loader's default effect (`disassemble`) |
| 3 | What the particle renderer draws | Census and VB prefix at flight F8 5 |
| 4 | Whether bullet draws 9 and 90 carry the same instances (dedupe or draw both) | `bolt_match` instance hashes in the flight |
| 5 | Whether engine-glow scale or alpha tracks throttle | Flight F8 6 |
| 6 | Whether any replaced class draws in the far (Background) pass | Census `phase=` |
| 7 | Whether effect textures are MANAGED with level-0 bytes equal to the archive's; the cost of the first `LockRect` under Wine | Key fixture; census `pool=` |
| 8 | Exposure-meter response to bright transients (flash or pump) | Phase-2 meter fixture; the flight report |
| 9 | Whether effect draws carry an object scope (node serial) | Census `scoped=`. The fallback identity is spatial and temporal matching of records by key, position and scale |
| 10 | Whether hit sprites are parented to the ship, and how reliable the nearest-ellipsoid association is | `shield_hit` distance rows; optional disassembly of the impact spawn |
| 11 | Whether the D24X8 is still attached in the pass the stage rides, and the price of opening its own pass | Phase-1 GPU fixture timing |
| 12 | Trails of bright elements over real starfields | Flight report (motes' synthetic worst case: 34 % decaying at 0.97) |
| 13 | Native Windows behaviour of all of the above | Not verifiable by the user; track in platform-portability.md |

## 8. Merged from the second design (2026-09-25)

The main session ratified this note's architecture: the single proxy-owned effects stage. This section folds in
what the independent Fable design found ([effects-modernisation-fable.md](effects-modernisation-fable.md)). The
edits it forced in sections 2, 3 and 5 are marked there with "merged" or "changed by the merge".

### 8.1 Measured body facts and the DEFAULT effect VS

**Sprite bodies.** Measured by the second design with `tools/analysis/body_materials.py` [M]:

| Body | Faces | Diffuse | Consequence for the recogniser |
| --- | ---: | --- | --- |
| `objects/v/10659` | 2 (one quad) | `exp_PL_imp_diff` | An impact sprite. The draw-shape witness is 2 primitives, which is a second key beside the texture key |
| `objects/v/00518` | 70 | `fx_bullets2_diff` | Not a flat sprite. Its bolt-atlas texture suggests a muzzle or impact mesh of the bullet family; the class is open |
| `objects/v/12007`, `12009` | – | – | Both a `.pbb` and a `.pbd` exist. Which one the engine loads first is unverified, so the key table must list the textures of both |

The survey's four-way label "impact/shockwave/shield-hit" therefore reduces to one proven flat impact quad
(10659), one mesh (00518) and two bodies of unknown load order. Shield hit versus hull hit stays open (section 8.4,
F8s 1-2).

**The DEFAULT effect VS `d5e1c753`.** Read from `verification/results/motion-output-profiles.json`, `sm2_pairs[0]`
[M]; this session re-read the fields:

- position is a row-dot of c0-3 (`row_dot_quad`, rows 0-3, issue order wxyz);
- 29 instruction slots (33 with the motion fragment);
- texcoord outputs 1-7 and colour output 1 are free;
- the PS has 7 arithmetic + 1 texture slots, with texture inputs 1-7 free;
- per the second design, c252-255 are free;
- the motion route classes this pair `structurally_unsupported` (issue order), so it is never routed.

Together with the camera-numerics fit (WVP c0-3, world c4-6, view inverse c7-9, [camera-numerics.md](../reverse-engineering/camera-numerics.md)
45), a record takes the effect origin from the world c4-6 translation and its axes and scale from the row norms.
The second design flags as open whether the vertices are model-space under a full WVP, or pre-transformed under a
VP. The world rows in the fit favour model-space. F8 1 confirms it by comparing the c4-6 translation of the impact
draw with the target's `object_bounds` box.

For in-place variants (the phase-1 fallback of 8.3, and gate or flare work later), the free texcoord outputs and
c252-255 mean a variant can pass a camera-relative position or an age without disturbing the native outputs.

### 8.2 Texture identity: upload-time registry, sparse content hash as the key

The second design fills a `texture -> class` registry at upload: level-0 hash at the first Unlock through the
ownership layer's upload observer, or at `UpdateTexture`/`UpdateSurface`. Its fallback key is dimensions +
format + draw shape. Reference hashes come from the archive, and the capture's allocation records seed and
validate the table. This note's first version hashed at first bind with `LockRect(READONLY)` and tagged the
object with `SetPrivateData`.

**Decision: the second design's registry timing, with this note's sparse hash as the key function** (section
2.1 edited). Why:

1. **Load time, not combat time.** Hashing happens while the texture loads. A first-bind lock can fall on the first
   explosion of a fight, and under Wine a managed texture whose system copy was evicted may be downloaded from the
   GPU [I].
2. **Identity lives on the wrapper node.** The ownership layer already wraps textures (`d3d9_ownership.cpp`) [S],
   so a key cannot outlive its texture. Address reuse needs no private-data tag.
3. **The draw path pays nothing.** It reads a field on the node, with no device call.
4. **The sparse hash stays, because loading time is under investigation** (AGENTS.md). A full level-0 hash of
   every loaded texture would add megabytes of hashing to a load. About 4 KB per texture is enough to tell
   archive textures apart.
5. **The fallback key is refused as a primary key.** Dimensions + format + draw shape is not unique (many
   256² DXT5 effect textures). It remains the census's witness and a consistency check: a 2-primitive draw with
   the 10659 key.

The capture's allocation records (section 8.4) are used as the second design intends: they show whether effect
textures are filled by Lock or by `UpdateSurface`, and they seed the per-class dimensions the key tool checks. The
key itself is the content hash.

### 8.3 Bolt fallback and placement

**Placement.** The second design draws its deferred queue "at step 0 of the scene-end hook", with the completed
RT2 bound as a texture and the game's depth test still bound. In the code the order is sun apply → fog →
`resolve_hdr`, and step 0 (state block, RT1/RT2 unbind) is the start of `resolve_hdr`. Its slot is therefore
**the same position** as this note's: after fog, before the resolve, with the current-frame lane and the same
pass-opening question.

- **Differences:** none in position. Two refinements are adopted.
  - The stage runs inside step 0's existing state bracket. That saves one `D3DSBT_ALL` capture and apply.
  - The second design's assertion that the D24X8 stays attached there is **not verified**. It remains unknown 11,
    measured by the phase-1 fixture. Without it, occlusion falls back to the lane, the motes' rule.

**Phase-1 fallback if the pass-opening cost fails the 0.1 ms gate.** Bolts take the second design's in-place path.

- The existing substitute VB (`bolt_footprint_core.h`, measured 60-100 µs CPU per draw today) is extended.
- Each instance's crossed cards become two camera-facing quads on the instance's area-weighted world axis:
  - a core ≥ 3 px wide, with length max(native, L) plus a velocity stretch from nearest-centroid matching;
  - a halo 3× wider at 0.25 intensity.
- A proxy-generated 128x32 profile texture shapes them, and a 20-slot ps_3_0 draws them. The tint comes from the
  game texture at the instance's UV centre, and the output is gained above 1.0 under the existing DESTBLEND ONE
  substitution.
- It draws in the game's bullet draw, before the resolve, with hardware depth. It has no soft end, because the
  lane cannot be read mid-scene. The vertex count falls from 24-234 to 12 per instance.

Shells in that case take the in-scene variant of 2.2, or the ripple decal (3.3).

### 8.4 The capture flight: one plan for both designs

**When and on what build.** It flies **now, on the installed build**. No new DLL is needed: the F8 capture already
records, per draw:

- state, blend, bound textures with their allocation records (id, dimensions, format, pool; the fill path as far as the lock records show it, not verified for textures);
- VS constants and primitive counts;
- `object_context` (node, model), with `--object-trace`;
- `object_bounds`, with `--object-bounds-log`.

It runs in parallel with the phase-1 build, and its answers pin the shell keys before the look flight.

**Launch.** One launch at 5120x1440 in chase view with the current queued default command. That command already
carries `--telemetry --taa-debug --object-trace --object-bounds-log --shadow-replay-candidates` and the capture
flags. Do not add `--gpu-sync-timing`.

**Setup.** A fighter with a bolt weapon (plus a beam weapon if owned) and one missile, against a shielded M4/M5.
Press F8 about one second into each situation. Five F8s are required and the sixth is optional.

| F8 | Situation | Settles (unknowns: this note section 7, second design section 7) |
| ---: | --- | --- |
| 1 | Fire held at the shielded target 300-600 m ahead, hits landing, bolts in flight near and receding | shield-hit sprite: key via the allocation record, draw shape, fade lane, `g_TexMatrix`, scope, phase (7.1, 7.6, 7.9); the VS layout and the hit point (c4-6 translation vs the target's `object_bounds` box, 8.1, 7.10); bolt pre- vs post-resolve luminance with `--taa-debug` (the ≈0.1 cut); draws 9/90 counts and textures (7.4, by count only); texture fill path, Lock or `UpdateSurface` (7.7) |
| 2 | Same target, shields down, hull hits | hull-hit sprite vs F8 1 (7.1); whether sparks exist natively (particle pass or sprite) |
| 3 | The kill, 200-400 m away, from the first flash | the explosion's draw shape, textures and flipbook stepping; whether `explosion_plane`/`exp_sparks` reach the DEFAULT pair, the particle pass or nothing (7.2); peak scene luminance, the input to the meter question (7.8) |
| 4 | Missile just fired; own ship at full throttle, chase view from behind | the particle pass's content and the missile trail (7.3); engine-glow draws at full throttle, nozzle constants, the `INSTANCE` pair's users |
| 5 | Same view after a full stop (zero throttle) | glow scale or alpha against F8 4: the plume-length law (7.5) |
| 6 (optional) | A beam weapon held on the target for 1 s | beam mesh, UV across the width, scaling to the hit distance (3.2) |

**Left for later flights.**

- Exact lifetimes beyond an 8-frame burst (7.1, 7.2): the phase-1 census rows, in the look flight.
- The first-person view rule and cockpit glass: the phase-1 look flight.
- Starfield trails (7.12) and exposure pumping (7.8): the look flight.

### 8.5 Rejected options from the second design not listed in section 6

- **A full proxy particle system replacing the game's particle pass.** The pass is one draw of 21-61 quads, with
  nothing to gain and no smoke to replace. This note's 3.7 keeps the game's draw and only moves it and adds a
  soft fade.
- **Soft particles from RESZ mid-scene.** It needs a 26-fetch decoder per pixel and a mid-scene RESZ per
  transparent burst, and native drivers do not guarantee RESZ. The completed RT2 at the scene end is exact and
  free.
- **Routing every effect to the motion buffers (own-motion MRT writes for transparents).** Writing RT2 under
  transparents corrupts the depth history that the fold, thin classification and fog read, and additive layers
  would ghost along the wrong motion. Held in reserve, next to option H, only as a phase-2 experiment for
  proxy-owned streaks.
- **Heat distortion before the resolve.** A 1-2 px shimmer is clipped by the 3x3 and jitters with the sample
  offsets. This changed 3.8: distortion now runs after the resolve, scissored.
- **A full-screen distortion pass.** About 1.0-1.2 ms at 5120x1440 [I], against 0.15-0.5 ms for a scissored region.
- **Keying the effect class on the program hash alone.** Impossible: one pair serves every effect material.

### 8.6 What the merge changed in the decision

1. Texture keys are computed at upload on the ownership wrapper, not at first bind (2.1, 8.2).
2. The stage runs inside `resolve_hdr`'s step-0 bracket, at the same slot as before (2.2, 8.3).
3. The phase-1 fallback for bolts is the in-place substitute-VB path, if opening the pass costs more than 0.1 ms (2.2, 8.3).
4. Hits with no owner box get a ripple decal instead of a shell (3.3).
5. Heat distortion moves after the resolve, scissored (3.8).
6. The capture flight flies now on the installed build with five required F8s. The phase-1 look flight comes after the candidate (5, 8.4).

## 9. Phase-1 implementation brief (for `implement-deep`, Opus 5.5 high)

**Goal.** Build phase 1 of the effects stage, default off, behind launcher options:

- the stage skeleton with its pass-opening measurement;
- upload-time texture keys;
- projectiles as HDR capsules;
- shield-hit Fresnel shells.

A recognised game effect draw becomes a record and is suppressed. The stage draws it once per frame, as the first
act of `resolve_hdr`'s step-0 bracket, into the FP16 scene with ONE/ONE blending. The design is
`docs/architecture/effects-modernisation-opus.md`, sections 2, 3.1, 3.3 and 8. Read sections 2 and 8.3 before
coding.

**Work order.**

1. **Measure first.** The GPU fixture case `pass_open` prices the stage at 5120x1440 in two conditions: riding an
   RT0-only pass already opened by a sun-apply/fog-like quad, and opening its own pass after the RT1/RT2 unbind.
   It also reports whether the D24X8 is still attached in that bracket.
   - **Own-pass cost ≤ 0.1 ms:** build the stage as designed.
   - **Own-pass cost > 0.1 ms:** still build the stage, but run it only on frames where the fog or sun apply ran,
     and build the bolts' in-place fallback (8.3) for frames where they did not.
   - Record the number and the branch taken in the ledger. Do not stop to ask.
2. `tools/effects/effect_keys.py` and the upload-time key (8.2).
3. Bolts (3.1).
4. Shells, with the decal fallback (3.3).
5. Census rows (section 5).

**Acceptance.** Each must pass; the outputs go under `verification/results/effects-stage/`, with the producing
scripts beside them.

- **Host modules:**
  `PYTHONPATH=verification/probe /usr/bin/python3 -m unittest verification.analysis.test_effects_stage_core verification.analysis.test_effect_keys verification.analysis.test_bolt_footprint verification.analysis.test_screen_emission_live`.
  The result must be OK, with at least these new cases:
  - bolt matching recovers v within 1 % on synthetic streams, refuses spacing < 2|v|Δt, and resets on a cut;
  - shell association picks the right owner among 3 boxes, falls back to the sphere or decal beyond 1.5 radii,
    and applies the 4-slot policy;
  - the key tool is deterministic, and mod precedence holds on copied synthetic roots.
- **Build and audit:** `sh verification/results/effects-stage/acceptance.sh`, patterned on
  `verification/results/bolt-footprint/acceptance.sh`. It does a mingw `cmake --build … --target d3d9` with
  0 warnings, runs `check_no_x87.py` with no violations, and prints the ps/vs slot counts of the new programs
  (each ≤ a few hundred).
- **GPU fixture:** `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_effects_stage.py`
  at 1920x1080 and 5120x1440. All cases must pass:
  - `pass_open`: the measured number, reported against the gate;
  - bolt capsule: core ≥ 3 px, length ≥ L, occluded by lane depth, soft end;
  - moving bolt and shell through the real resolve, 8 jitter phases at 0/4/8 px per frame: core output/current
    ≥ 0.9, and a trail ≤ 3 px over dark sky;
  - shell: rim, 4 rings, soft hull cut, decal fallback;
  - off path: stage off or toggled off gives a byte-identical frame and call count, with native draws forwarded;
  - suppression witness: armed means not forwarded; disarmed or failed means forwarded, never both;
  - Reset: released and recreated;
  - missing FP16-blend capability: the stage is refused and draws go native;
  - timing with timestamp queries: 60 bolts + 2 shells ≤ 0.05 ms, a full-screen shell ≤ 0.1 ms;
  - `texture_keys`: the upload-time hash of three D3DX-loaded effect `pck` textures equals the table's.
- **Regressions,** because the stage sits in the fog-to-resolve span. Run one at a time, never two Wine commands
  at once:
  - `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_fog_pass.py`;
  - the fog route bridge, in the form the fog ledger uses;
  - `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_temporal_pass.py`,
    whose existing rows must be unchanged.
- **Launcher:** one `python3 tools/manage.py launch --dry-run … --effects-stage --effects-shields` shows the
  forwarded variables. Under `--vanilla`, nothing is forwarded.

**Files.**

- New:
  - `src/proxy/effects_stage_core.h`: header-only, no D3D types. Records, the node-age map, bolt matching, shell
    association and hit slots, key-table parsing.
  - `src/proxy/motion_output_effects_inc.h`: resources created at the latch, record and suppress, the stage run,
    Reset.
  - `src/effects/effects_{bolt,shell,decal}_{vs,ps}.hlsl` and `effects_soft_depth.hlsl`: the lane `.b` soft
    term, with the `R32F` fallback `m32/(d − m22)`.
  - Generated program headers `*_inc.h`, through a generator next to `tools/shaders/generate_bloom_programs.py`.
  - `tools/effects/effect_keys.py`, and the generated `effect_keys.json`, installed beside the DLL.
  - `verification/probe/run_effects_stage.py` with its fixture source and build script.
  - `verification/analysis/test_effects_stage_core.py` and `test_effect_keys.py`.
  - The ledger `docs/verification/effects-stage.md`.
- Existing:
  - `src/proxy/motion_output.cpp` and `.h`: the record/suppress hook beside `prepare_screen_additive`, and the call
    at the start of `resolve_hdr`'s step 0;
  - `src/proxy/capture.cpp`: the environment gates;
  - `src/proxy/loader.cpp`: the bullet prefix scan enabled by the stage;
  - `src/ownership/d3d9_ownership.cpp`: a narrow key callback on the existing level-0 lock observer;
  - `bolt_footprint_core.h`: reuse only;
  - `CMakeLists.txt`;
  - `tools/manage.py`: `--effects-stage`, `--effects-shields`, `--effects-census`, the Ctrl+Shift A/B hotkey, and
    `--effects-bolt-views chase|all` (default chase until the user decides).

**Constraints.** From AGENTS.md, binding:

- **Game and Wine.**
  - Never launch the game; `--dry-run` only.
  - Every Wine command runs as `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py <command…>`,
    never two at once, waiting in steps of at most 60 s.
  - Check that `X3AP` is not running before builds and fixtures.
- **Portability.**
  - Documented D3D9 only. No Wine-specific exports, layouts or hashes.
  - Capabilities are checked: FP16 post-pixel-shader blending, independent write masks, volume textures if used.
  - Native Windows remains unverified: add the gap to `platform-portability.md`.
- **CPU code.**
  - SSE2 with `-msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2`. No x87 and no fast-math.
  - The CPU/LastError preservation and rollback rules of every hook site apply.
- **Performance and resources.**
  - Performance pass: no per-draw allocation, and one bool on the non-effect draw path.
  - Programs are created at the latch, never at first use.
  - Shader programs stay in the low thousands of slots; do not split for 512.
  - Generated fragments are `*_inc.h`.
- **Stage behaviour.**
  - Default off; nothing under `--vanilla`.
  - Fail closed: a draw is never both suppressed and lost without a logged disarm, and never both native and staged. *(Phase 1 as built suppresses nothing: every recorded draw is native and staged; see "Implementation (phase 1)".)*
- **Evidence and files.**
  - Keep build products, captures and game bytes untracked. Hashes and names derived from the archive may be
    tracked; bytes may not.
  - Mod trees are read-only, through copied synthetic roots, never symlinks.
  - Work in an isolated worktree for source edits.
  - Do not commit, install or rebuild a frozen candidate DLL.
- **Report.** Outcome, Evidence (commands and numbers, marked measured or inferred), Files changed, Open issues;
  about 25 lines. The first message without a tool call ends the turn.

## Implementation (phase 1), 2026-09-25

Built as section 9 orders it; ledger `docs/verification/effects-stage.md`; fixture record
`verification/results/bottle-X3/effects-stage/summary.json`. Marks as in the header. Everything below is opt-in
(`--effects-stage`, `X3M_EFFECTS_STAGE=1`, refused under `--vanilla`, needs `--motion-output --hdr --taa --ownership`
and the additive bullets, implied at gain 1), default off until the flights.

**Work order 1, the pass-opening price [M].** `run_effects_stage.py` case `pass_open` at 5120x1440, the fenced frame
tail the stage rides in production (MRT quad with the depth surface attached → the RT1/RT2 and depth unbind the resolve's
`normalize` does → the stage → the resolve's first target change), stage on and off in alternating frames, the stage's
CPU submit subtracted: riding an RT0-only pass a fog-like quad opened 0.012 ms, opening its own pass right after the
unbind 0.021 ms, price of the own pass 0.008 ms (three runs: −0.002, 0.017, 0.008; noise about ±0.015 ms). This
backend answers no `D3DQUERYTYPE_TIMESTAMP` (0 samples, as in the fog fixture), so the numbers are EVENT-fenced wall
time differences, the fog ledger's law. **Verdict: below the 0.1 ms gate; the stage is built as designed, the in-place
bolt fallback of 8.3 is not built.** The 0.36 ms tile-GPU load/store of section 2.2 does not appear on this backend
[M]; a native tile driver stays unverified. Unknown 11: the D24X8 is attached when the resolve captures the caller's
state (`stage_depth_bound=1`) and is unbound by `normalize` before the stage runs, so the stage occludes by the lane
alone (the motes' rule) and sets `ZENABLE` off; the hardware depth test of 2.2 is not used.

**Where the stage runs.** `TemporalPass::FrameInputs::stage_callback` (+ `stage_context`): called once inside the
run's bracket after `normalize` and the scene open, before the first copy; the pass runs `normalize` again afterwards,
so the resolve sees the state it expects and a run with a null callback is bit for bit the old run (fixture: a
callback that draws nothing gives the run without the field byte for byte). `MotionOutput::resolve` sets the callback
only on an armed frame with the lane texture in hand; `run_effects_stage` draws through `renderer::EffectsStagePass`
(`src/renderer/effects_stage_pass.cpp`): programs, declaration, streams, indices, constants, the lane at s0, the game's
bullet atlas at s1 (the ownership layer's borrowed native pointer, AddRef'd for the frame), `CLIPPING`, `CULLMODE NONE`,
ONE/ONE, `DrawIndexedPrimitive`; no `SetRenderTarget`, no state block of its own (the resolve's bracket restores).

**Records, and why phase 1 suppresses nothing (2.1; review B1, 2026-09-25).** `record_effect_draw` runs in
`before_draw` for the pair `d5e1c75351ed3f04` / `89193868c61c3846` + `8360f422de08b5bd` only (one bool test for every
other pair): Scene phase after the latching clear, the stage armed, `--effects-shields`, the stage-0 texture's key
known and listed as `shield_hit`; then c4-6 read once (`GetVertexShaderConstantF`, one call per recorded draw), origin
and scale from the rows, the identity from the object scope's node serial (`--object-trace` / `--object-lifetime`) else
the key with the origin quantised to 8 units. Every refusal counts (`forwarded`, `unknown_keys`, `overflow`). Bolts:
`record_bolt_draw` runs inside `prepare_screen_additive` after its admission checks; the same shape and locked-prefix
rules as the footprint, `detect_period`, `derive_instances` (centroid, covariance axis, extents, UV centroid, colour
alpha); the second bullet draw of a frame with the same first/last vertex words adds no second record (unknown 4:
`bolt_sets` per window counts the distinct sets). **The recorded draw is not suppressed:** whether the stage will run is
not known at draw time (the resolve is decided at scene end: target bound, container, `resolve_allowed`; the state can
be lost mid-frame; the run can fail before the callback), and a suppressed sprite cannot be forwarded natively later
without retaining game resources (option a of the review: refused as not airtight for the sprites). So the game's
draw always goes through, the bullets through the additive route as before with only the `--bolt-footprint` rewrite
skipped for a recorded draw (the capsule is its footprint; `screen_emission_additive_refused_window effects_recorded=`),
the sprites unchanged, and the stage adds its capsules and shells on top (option b). A frame the stage does not reach
shows the game's effects, never nothing; the 7.5 us per suppressed draw of section 2.1 is not saved in phase 1, and
the native bolt card sits under the capsule (the capsule is far brighter). Suppression returns with phase 2 only with
a fixture-proven forward path. `--effects-bolt-views chase|all` is the footprint's view gate (default chase, marker
`_DEFAULT`). Arming (`effects_begin_frame`): the HDR redirect, the jittered TAA resolve, the lane, the attached pass
with its buffers, FP16 blending, and not within the 64-frame window after a failed stage (`effects_stage_failed` row,
`Arming::fail`); the decision is taken at the first draw after the latching clear (`effects_armed_now`: the HDR redirect, the depth surface and the jitter are latched inside the frame, not at Present). Ctrl+Alt+F5 (Shift up) toggles the stage (F3 is the game's target view). The end-to-end path
(recogniser, additive admission, native forward, the stage inside the resolve, the resolve-fault frame, Reset and
the buffers' return) is driven by the motion_output fixture's `seam-ownership-effects-stage` case (review B2; ledger
row "Review fixes": 10 drawn frames of 13 bullet draws, the fault frame kept its record with the native draw, the stage's
callback 116–173 us CPU per frame at the fixture's 1280x768).

**Bolts (3.1) [M in the fixture].** Four vertices per instance (`BoltVertex`, stride 64); the vertex program builds
the window-space streak from the previous position (centre − velocity, nearest-centroid match on the same UV period,
refused on ambiguity or a cut) to the current one, length max(native, L_min · H/1080) plus the streak, core radius
max(native, W_min/2), halo 3x; the pixel program is a capsule SDF with a one-pixel analytic edge, I_core 6 white plus
I_halo 1.5 tinted by `tex2Dlod(atlas, uv, mip 4)`, soft against the lane over SOFT 0.5 x max(half width, half length/4).
Fixture: a 0.8 px bolt drawn 3 px wide, 133 / 351 px long (native 130.6 / 348.2), a 0.5-unit bolt 15 / 19 px, the
hull-covered half 0.000, the end 2.5 units in front of a hull at 0.50 of the core; through the real resolve at 0 / 4 / 8
px per frame over 8 jitter phases the core keeps 1.000 with a 0-px trail over dark sky. **Timing [M]:** 60 bolts + 2
shells (204 / 544 px radius) cost 0.124 ms at 1080p and 0.162 ms at 5120x1440 in the tail, above the 0.05 ms target
of section 9; the two shells dominate (60 bolts alone: the pass_open stage at 0.012–0.021 ms).

**Shells and decals (3.3) [M in the fixture].** Owner boxes: every caster-candidate draw with a Known extent notes
(clip rows, object-space extent, node serial); per node the largest box of the frame; at the stage the object → world
rows come from the clip rows through P^-1 and the camera's world-from-view basis (`object_to_world_from_clip`, host
test within 2e-3). Association by ellipsoid-normalised distance (half-extents x 1.2), the nearest owner within 1.5
radii takes the hit into its four slots (oldest replaced), else the decal. The shell is the unit icosphere (162 / 320)
on the ellipsoid, Fresnel rim `pow(1 − |n·v|, 2.5)` x envelope of the newest hit, up to four Gaussian rings at
`ring_speed 3 rad/s x age`, a hex lattice on the rings, a 0.1 s flash, the native sprite's mean colour as tint (from the
key table), soft against the lane; back faces leave early. Fixture: rim 0.50 / 0.42 at 70° against 0.24 / 0.20 at 45°,
four rings 8.0/1.5, 7.8/2.1, 4.6/0.02, 5.9/0.04 (on / three widths off), the hull cut 0.000 / 8.23, the soft cut 2.80 of
8.60, the decal 3.33 on a hull at its depth and 0.000 over sky. **Timing [M]:** a shell projecting past every screen
edge 0.54 ms at 1080p and 0.77 ms at 5120x1440 (0.79 / 0.78 before the back-face early-out), above the 0.1 ms target;
the shell pixel program is 216 slots against the ~110 the estimate assumed, and both hemispheres are rasterised
(the early-out leaves the back one in the first instructions). Levers, not taken: a winding-based cull (halves the
raster work; the icosphere's winding would have to be pinned in the fixture), fewer ring slots, a coarser sphere
for large shells. In a fight the shells are far smaller than the screen; the flight's `effects_stage_frame`
`stage_us=` row measures the real cost.

**Texture keys (8.2) [M].** `ownership::Options::texture_upload_keys`: `Texture::LockRect` (level 0, writable, no
rect) remembers the mapping and `UnlockRect(0)` hashes it before forwarding; `GetSurfaceLevel(0)` links the surface
wrapper to its texture node (validated through the registry at use, the surface may outlive the wrapper) so
`Surface::LockRect/UnlockRect` keys the same way; `compute_texture_key_readonly` is the one-shot fallback (MANAGED /
SYSTEMMEM only; DEFAULT stays unknown). The key is `sparse_key`: FNV-1a 64 over width, height, the D3DFORMAT value and
16 runs of 256 bytes at rows (k·rows)/16 and columns (k·2654435761 mod 2^32) mod (row_bytes − 255), block rows for
DXT (`level0_layout`); `tools/effects/effect_keys.py` mirrors it on the archive DDS (DXT1/3/5, A8R8G8B8 / X8R8G8B8;
anything else is listed with no key and a reason). Fixture: D3DX loads the three catalogue textures through the
surface path (`source=2`) and every key equals the table's; the table (`tools/effects/effect_keys.json`, 3 entries:
`exp_PL_imp_diff` shield_hit `bef0465755af1985`, `fx_sphereshockwave_diff` shield_hit `1270112df32acd84`,
`fx_bullets2_diff` bolt `1d615ca9580bd5ac`, extents in body units from the LOD-0 points / 65536, tints = mean colour)
is read by the DLL from `<EXE dir>\effect_keys.json` or `X3M_EFFECTS_KEYS` (the launcher points the latter at the
repository's table until an install carries it). Which of the sprites is the shield hit stays open (unknown 1): the
classes are the table's guess; the census (`--effects-census`: `effect_draw` rows with `key=`, `verdict=`, size,
format, blend, scope, origin; `shield_hit` rows with the owner distance) pins them, and the flight may show the game
uploading differently from D3DX (unknown 7, the census `key_source=` / `uploads=` fields).

**Rows.** `effects_stage_config` (creation), `effects_stage_device` (attach, slots), `effects_stage_frame` (per frame
under `--telemetry`, else per 300 frames: armed, reason, recognised, recorded, forwarded, unknown_keys, overflow,
bolt_draws / bolt_sets, bolts, hits, shells, decals, matched, refused_ambiguous, median_speed, result, calls,
stage_us, depth_bound, enabled), `effects_stage_failed`, `effects_stage_toggle`, `effect_draw` and `shield_hit` on
capture frames with `--effects-census`. `effect_node` at node death (section 5) is not built: the age map expires
entries after 120 frames instead.

**Not built / open.** The `bolt_match` row's draw-9 / draw-90 instance hashes are folded into `bolt_sets`; hits with no
scope use the spatial identity; the meter's response (unknown 8) and starfield trails (unknown 12) are the flight's;
native Windows is cross-compiled only (platform-portability.md, "effects stage"). The two timing targets of section 9
are not met as measured (above); everything else in the acceptance list passes.
