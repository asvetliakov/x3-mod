# Own sun pass: a procedural sun in the mod's stage, the engine's chain hidden

Design note, 2026-11-02, for the decision the user took in principle: draw the sun ourselves and delete the machinery
that bends the engine's lens draws (SM2/SM3 lens-body wraps, lens scan refusals, `classify_body`, the radius
calibration, `core::Deferral`, the probe answering "visible"). Offline only: catalogue decoding, the existing RE notes
and source; no build, no Wine, no game. [M] measured in this session, [S] from a disassembly or ledger note,
[I] inferred. Both editions (compile-time `X3M_EDITION`), native Windows D3D9 is the target.

**Decision.** One stage, `own_sun`, draws a procedural disc and corona into the FP16 scene at the scene-end hook,
in the slot the plumes and motes already use (after the fog pass, RT1/RT2 unbound, before the TAA resolve), lit by
the sun light poll's colour and position, clipped per pixel against the scene depth lane with a soft edge, and
attenuated by the fog's far transmittance. The engine's sun chain is suppressed by the probe hook that already
exists, now answering **hidden** for the sun's record (the engine then destroys the chain's bodies itself: zero
draws). Everything that wrapped, scanned, classified or deferred lens draws is deleted; the lens bracket stays only
as the carrier of the user's `lens_flare_gain` for engine halos. One setting, one kill criterion (section 6).

## 1. Fact check: are suns different per sector?

**Yes, in colour; barely, in shape.** Scripts and the full table: `verification/results/own-sun-pass/README.md`.

| Edition | Sectors / sun objects | Lens groups referenced | Distinct `color` values | Distinct looks the engine can show |
| --- | --- | --- | --- | --- |
| AP stock | 239 / 250 [M] | 8 (groups 0-7; group 8 `SS_SUN_9` is unused) [M] | 132 [M] | 8 chain layouts x continuous colour |
| FL | 298 / 312 [M]; TSuns and Lensflares byte-identical to AP's [M] | 8 (0-7) [M] | 112 [M] | the same 8 layouts |
| Mayhem (`/mnt/c/X3`) | 256 / 263 [M]; TSuns 219 rows, Lensflares 182 groups [M] | 91 [M] | 236 [M] | 1 XRM single-card group (173) plus 90 stock-layout variants scaled by solar percent [I, from the table's own comments] |

What makes a sun look different in vanilla, from the tables and the RE notes:

- **Colour is per sun object**, the map's `color` attribute (24-bit), not per group: group 0 "blue" alone carries 32
  distinct colours on AP, from `#ffffff` to `#6496ff` [M]. FL has four `#000000` and seven `#323232` suns (dark
  sectors) [M]. The engine multiplies every chain body by the source colour and interpolates each body's size
  between the row's two sizes by `max_ch(colour)` ([lens-flare-visibility.md §11](../reverse-engineering/lens-flare-visibility.md), [S]).
  The same colour words feed the sun light (`LightDir_Color0 = colour / 256`, the fog's E_sun) [S].
- **The core disc is one texture for all eight stock groups.** Bodies 719-722 ("glow green/blue/yellow/red" by
  authored name) carry identical material lines: an untextured MATERIAL3 and MATERIAL3 texture 360
  (`tex/true/360.jpg`, 1024x1024) [M]. The hue of the glow is the sun colour, not the asset.
- **The group changes the chain layout and its size**: rows per group 10-13 (rays 752/753/754/760, streaks 761-766,
  ghosts 548/549/550/740/741/744/745, MATERIAL6 cards 11000-11011 for groups 1-3), core-disc size pairs 40-60
  (group 0), 60-100 (1), 28-54 (2), 45-73 (3), 40-80 (4), 25-80 and 50-80 (5), 25-80 (6), 40-120 (7) [M]. A factor
  of about two in disc size across groups, and a different set of ghosts.
- **Mayhem's sun is a different thing.** Its most-used group (173, 22 suns) is one XRM card (`1.00; 200; 250; 0`,
  body 996, MATERIAL6 `standard_lighting.fx`, 2048^2 texture; groups 174-181 are the other XRM colours, which no
  sector references) [M],
  the vs_3_0/ps_3_0 draw of run216 ([sun-occlusion ledger](../verification/sun-occlusion.md), 2026-11-01). Its
  pixel program is lit standard material, so the map colour does **not** tint it [S]; the other 90 referenced
  groups are stock layouts with the glow sizes scaled by solar percent [I].
- **Body 31 `/Sonne`** is drawn in-scene for every sun (every TSuns model is -1): a 4x4-quad card, texture 55
  (128x128) [M], depth-tested and in the FP16 target (blooms, meters, TAA) [S].
- TSuns field 9 (65536 for `SS_SUN_1`, 98304 for the rest; Mayhem 3275-163840 beside "5% .. 350%" comments) and the
  map's `f=1` flag on 23 AP / 33 FL / 253 Mayhem suns are **not decoded** here; neither is needed for the design.

So "different textures per sector" is not what vanilla does: one glow texture, tinted by a per-sun colour the
mod already polls every frame, in one of eight layouts. A procedural disc tinted by the polled colour reproduces
the per-sector variation the player actually sees; the layout (rays, ghosts) is the part we drop.

## 2. Where the sun is drawn (a)

The stage runs inside the scene-end hook (`0x004721b1`; FL: the scene hook flown in FL run 7), **after the fog pass and
beside the plume and mote stages**: RT1/RT2 unbound, the scene's jittered projection, the FP16 scene texture as
RT0, **before the TAA resolve, bloom and AgX** ([hdr-scene-path.md §4](hdr-scene-path.md),
[engine-effects-modern.md §3-5](engine-effects-modern.md)). Consequences, all by construction of that slot:

- The disc is a bloom source and enters the TAA history and the exposure meter like any in-scene emitter; the
  vanilla chain was a display-referred overlay after the write-back and never did [S].
- **No RT2 write.** The sun is at ~10^7 units (map positions ~10^9 engine integers / 100) [M], beyond the far
  plane and the fade ramp (76,800 / 87,040 units [S]); the lane stays sentinel under the disc, so fog, plumes,
  AO and the resolve keep treating those pixels as sky. Exactly right for an object at infinity.
- Blend ONE/ONE, Z test and Z write off (our own depth test in the pixel program), one `DrawPrimitive` of a
  camera-facing quad of side `2 x 3 r` at the projected sun; nothing drawn when the sun is behind the camera or
  the poll is not `Ok`. Fully off-screen suns still draw nothing (the quad is clipped).

**Radiance (FP16, engine space).** `c = colour / 256` (the poll's words, the same code values the engine uploads as
light colour [S]). Disc: `K · lerp(c, white, w) · D(r / r_disc)` with `K = 8` (`sun_disc_radiance`), `w = 0.6`
(the plume core's whitening), `D` a smoothstep edge of 0.1 r_disc. Corona: `K_h · c · exp(-3 (r - r_disc) / r_disc)`
with `K_h = 0.8` (sized under 1.0 engine; this sentence first cited a sentinel stabiliser's emitter bound E = 1, but
that stabiliser was retired on 2026-09-25, taa-mask-fold.md section 7: the bound is kept for the exposure meter and for
AgX contrast, see section 7 and own-sun-look.md). A black sun (`#000000`) draws nothing, which is what vanilla's colour-multiplied
chain does in those sectors [I]. **Exposure policy unchanged** ([space-exposure-policy.md](space-exposure-policy.md)):
Auto capped +1.3 EV multiplies by at most 2.46, so the disc saturates AgX white (far endpoint 16.29) as a sun
should. **Corrected 2026-11-02 (review of step 1):** the meter does react. Its highlight guard limits the EV to
`log2(14.66) - p99(tile max)` over the 16x16-px tiles (64-px at 5120x1440), and the disc's core reaches the top 1 % of
the tiles at 1280x768 (36-41 of 3,840 tiles, 0.94-1.07 % by placement; the p99 index needs 39) [M, offline,
`verification/results/own-sun-pass/meter_exposure.py`]. The tile maximum is the *decoded* luminance: under the default
`gamma2.2` decode a white disc of K = 8 meters at 97 (clipped to 64), so the guard caps the exposure at about -2.1 EV
while the disc holds p99 (+0.87 EV only under the identity decode); K <= 3.39 keeps it at or above 0 EV and K <= 2.25
leaves the Auto cap of +1.3 EV untouched [M, same script]. At 1920x1080 and at 5120x1440 the disc defined by the
height (step 1 as built, section 14) stays below the p99 index (0.81-0.85 % and 0.49-0.76 %), so other bright tiles
set the guard there; with the earlier width rule it covered 1.96-2.23 % at 5120x1440 [M]. The flight measures the
reaction (`ev=` / `ev_limit=` on `own_sun_frame`); the meter is not changed. If the flight shows pumping,
`sun_disc_radiance`, `bloom_source_clamp` and `hdr_ev_max` are the existing knobs.

## 3. Look parameters per sun (b)

**Recommended: colour and position from the sun light poll, fixed angular radius with one knob; the lens group
is not read.**

- Position and colour: `sun_light_poll::Sample.position` and `.colour` (AP `+0x150/152/154`, FL `+0x154/156/158`),
  polled on the render thread at the frame's first routed z-writing draw ([farnhams-legacy-sun-light.md](farnhams-legacy-sun-light.md));
  projected with the frame's unjittered scene camera exactly as `sun_visibility` already derives `lane_u/v`
  ([sun-partial-occlusion.md](sun-partial-occlusion.md), "After Run 223"), measured to agree with the engine's record
  to du 0.0008 / dv 0.0004 (run216 triage [S]). When the poll reports `NoDirectional` the stage draws nothing.
- Radius (as built: a fraction of the viewport height, default 0.0667, section 14): `sun_disc_radius`, half-width as a fraction of the back-buffer width, default 0.04 (the run223 default,
  51 px at 1280; the engine's own `record+0x34` saturates, so vanilla's disc size is the row size x colour term,
  not an angular quantity [S]). Aspect-corrected like the visibility pass.
- Not from the group. The lens group lives at `node+0x1a0` of the **second** sun node (the lens source, flag
  `0x20000000`), copied by the type-3 constructor at `0x00441712-0x004417c8` [S]; the poll reads the **first** node
  (the light), and whether that node also carries the group is unknown. The other hook-free source,
  `record+0x38` in the background view's record list `view+0x2a0`, exists only while the engine's collector admits
  the sun and the flare video option is on. Both are a disassembly task; neither is worth it for a 2x size spread
  the user can set with one knob. Revisit only if the user misses per-group variation.
- Mayhem: its XRM card is untinted and 4x the stock size [M]; with the default radius the procedural sun will be
  smaller and tinted by Mayhem's often dark colours (`#736464`, `#464155`, `#000000` [M]). That is the authored
  light colour of those sectors; if the user wants the XRM look back, the radius knob is the lever, not a texture.

## 4. Occlusion (c)

**Per-pixel depth test against the scene depth lane, soft edge, no probe.** The pixel program reads RT2 at the
fragment and the eight knight-move taps (`(+-2, +-1)`, `(+-1, +-2)`), `open = (taps with .r < 0) / 9` (the route's
-1 sentinel: no routed surface in front of the far plane; every RT2 format has `.r`), radiance `*= open`. This is
the clip kernel the current step-2 wrap already uses, with its measured profile across a vertical edge
(1, 7/9, 5/9, 4/9, 2/9, 0) and its bound of 2/9 per column under a one-texel silhouette shift
([sun-partial-occlusion.md](sun-partial-occlusion.md), "Jitter contract" [M there]). Two things change against
the lens-scene clip: the disc is now rasterised under the **same jitter** as the lane (the stage uses the routed
projection, as the plumes do), so no `c2` offset is needed; and the result goes **through the resolve**, whose
far-plane camera history and 3x3 clip average the residual wobble that nothing averaged for the post-resolve chain.
No 1x1 fraction, no temporal ping-pong, no readback, no query.

- **Fade band.** A fade-band draw above the alpha threshold is an RT2 owner (default on since Run 81,
  [fade-rt2-ownership.md](fade-rt2-ownership.md)) and occludes the sun as geometry; below it the hull is nearly
  transparent and the lane stays sentinel, so the sun shows through a hull that is itself almost gone. Consistent;
  no special case.
- **Fog.** The stage runs after the fog pass, so the pass does not fog the disc. The stage multiplies by the fog's
  analytic far transmittance `T(d -> far)` along the view (the one-constant law the plume phase 3 uses from the
  accepted 13 -> 22.5 km fade; on FL the "fog-transmittance lens gain" of step 9 is this same scalar). In a dense
  sector the sun is a dim disc, as the user asked for FL step 9. The fog's inscatter already carries E_sun from the
  same poll, so the glow around the sun and the disc agree in colour.
- **Holes.** Opaque draws the route refuses write no lane and do not occlude (fails towards visible; the vanilla
  probe's holes were the 255-candidate cap and LOD state). Cockpit geometry is not in the lane; whether the
  cockpit view draws over the stage is the plumes' open question too, not a new one.
- **Multiple suns.** The poll reports one node (the brightest directional). Sectors with 2-4 suns (10 AP, 11 FL [M])
  get one procedural sun; vanilla showed one chain per sun. Accepted for the minimal mode; a second sample
  (the poll's `second_score`) is the extension if the user notices.

## 5. Suppressing the engine's sun (d)

Two engine components draw "the sun": the lens chain and body 31.

**Lens chain: answer the probe "hidden" for the sun's record.** The probe hook (`0x00471630 -> 0x00488720`,
cdecl; FL RVA `0x8af0a -> 0xa3fd0`, ECX = record, EDX = view, both sites claimable, AP and FL offsets verified
[S]) and its eligibility test (`core::eligible`: main view re-probing a record owned by a background-regime
view, `owner+0x270 & 0x400000`) stay; the answer becomes a constant 1 for eligible records and the original for
every other probe (ship and station flares keep vanilla). Returning 1 is always safe: the engine's own first gate
returns it whenever the flare video option is off [S]. Downstream, `0x00471660` decrements the accumulator to 0
and destroys the record's bodies (`0x00487be0`) [S]: the sun's chain is never instantiated and costs no draw.
Nothing else reads the probe's result: `record+0x30` has five renderer sites and no HUD, AI or audio reader; the
candidate cache `view+0x37c` and the body-195 node are stateless between users
([lens-flare-visibility.md §13](../reverse-engineering/lens-flare-visibility.md) [S]). The three replicated gates
and the 148-byte prologue comparison are no longer needed by the override (an early 1 skips them) but the
prologue hash stays as the install identity check.

**Body 31.** Leave it in the minimal mode: it is depth-tested, blooms and resolves like any geometry, and its
weight on screen is not measured (the fingerprinted lens draws never included it). If the first flight shows a
visible second disc, skip the draw by identity through `object_trace` (model id 31 with constructor flags
`0x104000` [S]), the recogniser pattern the plumes use; no hash list.

**Why not a hash list in the lens bracket.** On stock AP under CrossOver the sun's bodies drew with
`d5e1c753/8360f422`, the pair every ship and station flare uses (run223 [S]); on Mayhem the sun is the SM3 pair but
bodies 753/754/778 are in both the sun groups and the engine groups [M]. No hash or body-id set separates the
sun's chain from engine halos; the record owner does.

## 6. Minimal mode, kill criterion, deletions (f)

**Minimal mode** = section 2's stage (disc, corona, lane clip, fog scalar) + section 5's hidden answer, behind one
setting `own_sun` (Bool, default on once flown, launcher `--own-sun on|off`) and one comparison hotkey that flips
both per frame (the probe answer and the stage are per-frame decisions, unlike the plumes' load-time redirects;
with the answer back to the original the engine re-instantiates the chain within two frames [S]). Tunables:
`sun_disc_radius`, `sun_disc_radiance`; nothing else in the first build.

**Kill criterion (the user's):** the sun looks worse than vanilla, or shows through a hull. Either -> `own_sun off`
restores vanilla in full (no answer, no stage) and the decision is reopened; no third rule is added in flight.

**Deleted, in this order, each a reviewed commit with its tests:**

1. After the first accepted flight: `src/renderer/lens_visibility_variant.{h,cpp}` (SM2/SM3 wraps and scan),
   `src/renderer/sun_occlusion_pass.{h,cpp}` (32-tap 1x1 visibility, ping-pong, jitter offset),
   `core::classify_body`, `core::footprint`, `core::Deferral`, `core::Hold`, `Ready` in `sun_occlusion_core.h`
   (keep `eligible`, the record and view offsets, the FL `+0xec` constant for the identity check), the lens-draw
   wrap and refusal paths of `motion_output_sun_occlusion_inc.h` (keep `apply_lens_gain`), the settings
   `sun_occlusion_core_f`, `sun_occlusion_curve`, `sun_occlusion_radius`, `sun_occlusion_default`, the
   `sun_lens_draw` / `sun_visibility` / `sun_lens_body_unclassifiable` rows and the `lens_*.bgra8` readback, the
   launcher references in `tools/manage.py` that name them (38 name the sun-occlusion or lens-gain settings [M]), `verification/probe/run_sun_occlusion.py`'s wrap
   and visibility cases, and the SM3 wrap plan of [sun-lens-sm3-body.md](../reverse-engineering/sun-lens-sm3-body.md)
   (the note stays as RE; the implementation is cancelled).
2. `sun_occlusion` and `sun_occlusion_log` are renamed into `own_sun` / `own_sun_log`; the probe hook, its static
   verifier, hook fixture and thunk tests stay under the new name.
3. Kept: the lens bracket claim (`0x00472491`; FL `0x8bcb7`) only for `lens_flare_gain` and its Law::skip, and the
   engine cull at gain 0 (`lens_flare_cull`), both independent of any classification and both the user's control
   over engine halos. If the user later drops the gain (plumes' redirect A already removes engine lens flares on
   AP), the bracket goes with it; that is a separate decision.

Lines: of the 2,723 in `lens_visibility_variant.cpp`, `sun_occlusion_pass.cpp`, `sun_occlusion_core.h`,
`motion_output_sun_occlusion_inc.h` and `sun_occlusion.cpp` [M], about 1,700 go [I], against about 400 added for the
stage [I]; `capture.cpp` has 42 references to the four features, most of them the diagnostic rows [M].

## 7. TAA (e)

- The sun is at infinity: its pixels are unrouted sentinel pixels in the resolve, which reprojects them with the
  far-plane camera history [S], the correct motion for a star under camera rotation; under translation it does not
  move. No motion vector is needed and none is written.
- The live resolve on the sun's pixels (corrected 2026-11-02, step 1c): the sentinel pixels reproject with the
  far-plane camera history and are clipped by the 3x3 mean +/- sigma box; the far stabiliser's gate is 0 on the
  sentinel (`resolve.hlsl` header), so a smooth static feature converges to its value at any radiance. The emitter
  bound E = 1 this item first cited belongs to the sentinel stabiliser retired on 2026-09-25 (taa-mask-fold.md
  section 7). The glow is still kept under 1.0 engine (0.781 at most outside the core at K = 2), for the exposure
  meter's guard and for AgX contrast at the core's edge, not for the resolve. The 3x3 clip cannot erase the core's
  edge: neighbours span 0..K, so the box is wide; the interior is uniform.
- Thin hold: not involved. The disc is not alpha-tested, not thin (>= 6 px: below 1.5 px of radius the stage draws
  nothing, the plumes' rule), writes no thin vote. The RT2 words are untouched (`.r` sentinel, `.g` share, `.b`
  view z, `.a` thin vote), so the far alpha-tested thin hold of FL run 8 sees the same lane as before.
- SETA: the stage has no time term; brightness and position come from this frame's poll and camera.
- Occluder edges: the silhouette in the lane is on the jittered raster and so is the disc; the knight kernel bounds
  the per-phase step to 2/9 and the resolve averages it. A flight that still sees edge crawl behind a specific
  occluder reads `unjittered_depth_writers` first, as the jitter contract says.

## 8. FL differences (g)

- The probe target takes ECX = record, EDX = view (register ABI, not cdecl), returns in EAX with a bare `ret`;
  the thunk saves ECX/EDX around the C decision and forwards with `popfd; jmp [target]`; the site `0x8af0a % 8 = 2`
  is written atomically [S]. The three early gates read the config word at **+0xec** (AP `+0xfc`); the hidden answer
  does not need them, but the install identity still compares the 147-byte prologue (FNV `0x1abcc319ce5e4445`) [S].
- Eligibility needs FL's main-view identity (FL sun-occlusion note §4) and the owner flag `+0x270 & 0x400000`
  (record and view offsets hold on FL [S]).
- The lens CALL `0x8bcb7` straddles a qword (non-atomic, install window only) [S]: the own sun does not touch it.
  It is claimed only if `lens_flare_gain` is ported to FL (section 6, item 3).
- Colour words `+0x154/156/158`, range `+0x15c` (`sun_light_poll_fl.cpp`, step 7, flown and accepted [S]). The
  stage itself is edition-neutral (it reads `Sample` and the frame camera).
- FL step 9 ("FL sun occlusion plus a fog-transmittance lens gain", status) is replaced by this design: the fog
  scalar is part of the stage; no FL port of the visibility pass or the wraps.

## 9. Cost on the hot path

- GPU [I, anchors measured for the plumes: 0.135 ns/px/slot, 0.035-0.04 ms fixed per full-screen draw, pass price
  0.008-0.016 ms]: one quad of side 6 r = 0.24 W, at 5120x1440 about 1.5 Mpx under a ps_3_0 of ~45 slots (nine lane
  taps, two falloffs, fog scalar) = 9 us, plus the fixed draw cost: **about 0.05 ms** per frame. Programs: vs ~10,
  ps ~45 slots, created at the latch; the pass logs `MaxPixelShader30InstructionSlots` and its own slot count once.
- CPU: one poll already taken (the fog and shadow lanes use it), one projection, six constants, one draw: a few
  microseconds. The probe hook's decision shrinks from 613 instructions to the eligibility compares plus a constant.
- Removed: the 32-tap visibility pass and its two 1x1 targets, 5-7 device calls per wrapped lens draw, the per-pair
  scan and classification, the vanilla probe's candidate walk and narrow phase for the sun's record (skipped by the
  early answer, saving unmeasured CPU against vanilla), and the sun's own chain draws (10 per frame on stock, 1 on
  Mayhem [S]).

## 10. Native Windows

Documented D3D9 only: a vs_3_0/ps_3_0 pair, one DEFAULT-pool vertex buffer or `DrawPrimitiveUP` of four vertices,
`SetTexture` of RT2 (already a texture), ONE/ONE blending into `A16B16G16R16F` (post-pixel-shader blending on the
FP16 format is what the plumes and bolts already rely on; the cap is logged once at device creation). No query,
readback, Wine export or private layout. The pixel program must pass the real runtime validator before a flight
(`verification/results/hull-widen-native-validator/`, the WSL harness; one constant register per instruction rule).
The probe patch is a byte-validated game-internal hook, in scope on both platforms. Status after implementation:
Windows-compatible source and cross-compilation, flown natively by the user (the game is native since 2026-10-11),
so native behaviour is verified by the flight itself; record the entry in
[platform-portability.md](platform-portability.md).

## 11. Verification that would prove it

- Host (no Wine): static site verifier unchanged (`verify_sun_occlusion_sites.py`, FL constants test); hook fixture
  (synthetic E8 site): eligible -> EAX 1 without running the original, others -> original, registers, EFLAGS,
  `LastError`, MXCSR preserved; schema and launcher tests for the renamed setting.
- GPU fixture (the motes/plumes fixture pattern, real device): synthetic lane with a half-plane occluder at 0, 25,
  50, 75, 100 % of the disc -> integrated radiance within 1/9 of the covered fraction and the edge profile
  (1, 7/9, 5/9, 4/9, 2/9, 0) across the silhouette; full sentinel lane -> centre texel `= K · lerp(c, 1, 0.6)`
  within FP16 epsilon; colour black -> no change to the target; fog scalar 0 -> no change; sun behind the camera ->
  no draw; RT1/RT2 bytes and the surrounding state block unchanged; a frame through the full chain (stage, resolve,
  write-back) shows the disc at >= 0.99 of its radiance over sky and the corona at 0.37-0.66.
- Native validator on the generated ps/vs (no messages).
- Flight (one, AP first): the comparison hotkey over a clear sun (colour and size against vanilla, screenshot), the
  sun crossing a station strut (no pop, no shine-through, edge quality), a fogged sector (dim disc), a dark-colour
  sector. Rows to read: `own_sun status=patched`, `own_sun_frame u= v= radius_px= colour= fog_t= drawn=1`, the
  probe counters (`eligible_answered`, `forwarded`), `unjittered_depth_writers` if the edge crawls.

## 12. Unknown, and what settles it

| Unknown | Settles it |
| --- | --- |
| Does the first sun node (the one the poll reads) also carry the lens group at `+0x1a0`, or any size field usable for per-group radius? | Disassembly of the type-3 constructor `0x0043ffa0` around `0x00441712` (which node gets `+0x1a0` and `+0x1b0`); FL equivalent from `sun-light.md` |
| Is the polled colour the same word the lens node is tinted by (both from the map `color`)? | Inferred from the constructor copying one record; a `sun_light_sample` row beside a vanilla screenshot of a strongly coloured sun confirms it |
| Visual weight of body 31 next to the procedural disc | The first flight with the chain hidden |
| Whether the stage draws over cockpit geometry | Same open question as the plumes; a cockpit-view screenshot |
| Exposure meter response to the disc | The frame's `hdr_exposure` rows in the flight; the analysis above says none |
| Multi-sun sectors | Accepted single sun; the poll's second candidate is the extension |

## 13. Options considered and why they lose

- **Copy the texture from the suppressed draw** (bind the engine's glow texture 360 or Mayhem's XRM card to our
  quad). Loses: the texture identity differs per install (stock glow, Mayhem card, FL untested), it needs the
  lens bracket to capture the texture, i.e. the draw interception we are deleting, and the stock glow is one
  texture anyway, tinted by a colour we already have. The procedural disc needs no asset and no bracket.
- **Keep the probe override "visible" and modulate the chain** (today's design, plus the SM3 wrap). Loses on the
  user's own evidence: three flights, four rules (uniform f, clip, clip x f, deferral), and the sun still showed
  through hulls on native AP because the wrap could not classify the body; every new install (Mayhem card, FL
  pairs) reopens the scan. The chain is post-resolve display-referred and can never bloom or resolve.
- **Visibility probe instead of the per-pixel depth test** (a 1x1 fraction, as step 1). Loses: the fraction is a
  disc-average and shows the covered half of the disc at reduced strength; the per-pixel test clips exactly and
  costs the same taps in the one draw we already issue. The user preferred clip x f in flight (run235); the
  clipped disc with the corona under E reproduces that reading without a second pass.
- **Hash or body-id list to drop the engine's sun draws.** Loses: no set separates the sun's bodies from engine
  halos on stock (`d5e1c753/8360f422` shared; 753/754/778 in both groups); the record owner does.
- **Suppress via the engine cull at gain 0 or the flare video flag.** Loses: both remove every lens flare, which
  is the user's separate halo decision, not the sun's.

## 14. Step 1 as built (2026-11-02, AP; not flown)

Sources: `src/proxy/own_sun_core.h` (pure: answer, hotkey, projection, radiance, fog scalar, quad, constants),
`src/renderer/own_sun_pass.{h,cpp}`, `src/effects/own_sun_{vs,ps}.hlsl` (tool `tools/shaders/generate_own_sun_programs.py`),
`src/proxy/motion_output_own_sun_inc.h`, the answer in `sun_occlusion.cpp`, the latch and hotkey in `capture.cpp`.
Evidence: [sun-occlusion ledger](../verification/sun-occlusion.md), "Own sun step 1". Where it differs from sections 2-11:

- **Answer gate.** The probe answers "hidden" only in a frame whose Present-time latch was set: setting on, hotkey on,
  and this device's stage armed in its last resolve (route, HDR/TAA FP16 path, camera, lane, attached pass, this
  frame's sun poll Ok). A device whose stage cannot draw keeps the engine's chain, so the sky is never without a sun.
  The answer precedes the readiness machine and the latch, so the step-1 visibility pass and the lens wraps see no
  record in an own-sun frame. A projection the camera refuses clears the readiness for the next frame too.
- **Draw gate (review F1).** The stage draws only in a frame whose main-view eligible probe the own sun answered
  "hidden" (`sun_occlusion::own_sun_hidden()`, set in the answer branch; the main view is probed before the scene-end
  hook, [lens-flare-visibility.md](../reverse-engineering/lens-flare-visibility.md) section 14). Planets and other
  background draws leave the lane at the -1 sentinel ([ambient-occlusion.md](ambient-occlusion.md)), so a lane clip
  alone would show the sun through a planet; the engine's own collector and probe still decide whether the sun is up
  there. With the eligibility failing (main view unresolved, another view) the engine's chain stays and nothing is
  drawn; with the flare video option off there is no sun at all (accepted). Such armed, active frames count `no_probe=`
  on `own_sun_frame`; the first writes one `own_sun_no_probe` row. If the main view were probed after the scene end,
  every active frame would count `no_probe` and draw nothing: the first flight shows it.
- **Draw.** `DrawPrimitiveUP` of four CPU-built clip-space vertices; the vertex program takes no constant, the pixel
  program five registers (texel, two knight-offset vectors, disc, corona). ps 70 slots, not ~45: nine `texldl` and
  their address arithmetic.
- **Colour (review F4, F5).** Disc `K lerp(c / m, 1, 0.6) m T` and corona `K_h (K / 8) c T`, `m = max_ch(c)`: the
  whitening keeps the hue, the brightness follows the colour as vanilla's colour-multiplied chain does (FL #323232 is a
  grey disc at 0.195 K, Mayhem #464155 a dim violet one), the corona scales with `sun_disc_radiance`. Black, T = 0 and
  K = 0 draw nothing; K = 0 also requests no probe answer, so the engine's sun stays.
- **Radius (review F6).** `sun_disc_radius` is a fraction of the viewport height (default 0.0667 = 51 px at 768 rows,
  the run223 size at 1280x768; 96 px at 1440 rows), because the engine keeps a fixed vertical field of view as far as is
  known; the disc stays round (square pixels).
- **Corona shape.** `lerp(corona h(r), disc, D(r))` with `h = max(saturate(exp(-3 (r - 1))) - exp(-6), 0)`: the corona
  fades in under the disc's edge (the centre texel is exactly the disc radiance) and reaches 0 at the quad's edge.
- **Exposure.** Section 2, corrected: the meter's guard does react to the disc; `own_sun_frame` carries `ev=` and
  `ev_limit=` for the flight.
- **Poll.** Taken at the arming: the shadow lane's poll of the frame when it ran (`poll_source=lane`); otherwise one
  poll at the scene end (`poll_source=stage`), an assumption that the array still holds the main view's lights there.
  Not Ok (no directional light, unavailable) leaves the stage unarmed (`own_sun_state reason=poll`), so the next
  frame's probe forwards to the engine (review F2).
- **Hotkey.** Ctrl+Shift+F9, polled only while the own sun is installed ([comparison-hotkeys.md](comparison-hotkeys.md),
  "Own sun"). Removed in step 2.
- **FL.** Inert in this step (one `own_sun status=unavailable edition=fl` row), although main has carried the FL probe
  hook since FL step 9; enabling it there is a separate step.
- **Not built.** Section 11's real-device fixture and a hook-fixture case for the answer (no Wine on the native host);
  the flight is the device test.

### Step 1b: the look after Run219 (2026-11-02, AP; not flown)

Run219 accepted the occlusion ("works and is better than vanilla") and rejected the look: about four times too big, too
bright, a flat disc instead of vanilla's rays, white where vanilla is yellowish (fogged sector, polled colour 0.88, 1.0,
0.78). The user's references are stars seen from space: a small hot point with thin rays of different lengths over a
faint halo. The same stage, quad, occlusion and fog scalar draw that instead (`src/effects/own_sun_ps.hlsl`; every
constant in `own_sun_core.h`, uploaded as c3..c9, so `test_own_sun` evaluates the law with the numbers the DLL draws with):

- **Core.** Radius 0.012 of the height (9 px at 768 rows, 17 px at 1440; step 1 drew 96), soft edge from 0.6 to 1 core
  radius, `K lerp(c / m, 1, 0.25) m T` with K = 2: decoded (gamma 2.2) 4.6, above the bloom threshold (exposed 1) and
  under the meter guard's +1.3 EV Auto cap (K <= 2.25). The core now covers 0.10-0.16 % of the meter tiles at 1280x768
  and 0.05-0.22 % at 5120x1440, far below the 1 % that sets p99, and even at p99 it would leave `ev_limit` at +1.67 EV
  [M, `meter_exposure.py`]. Run219 measured `ev_limit` -0.92 with the 96-px disc at 5120x1440 where the tile model
  predicted it below p99, so the model under-counts something there; the new defaults hold either way.
- **Glow, the polled colour exactly.** Halo, rays and streaks are `c T K / 2` times their intensity, no white mix
  (vanilla multiplies every body by the colour). Their sum peaks at 0.764 at K = 2, under 1.0 engine (the meter's
  guard and AgX contrast; the "stabiliser E = 1" first cited here was retired on 2026-09-25, section 7) [M].
- **Rays.** 16 spokes (offset half a spoke so none lies on the streaks), the nearest one per pixel from a minimax
  arctangent; per spoke a hash of its index sets the length (3.3-12.9 core radii: a few long, many short) and the
  strength (0.6-1.0 x 0.75); a Gaussian across the spoke (width 0.12 core radii, tapering outward) and
  `saturate(1 - r / L)^2` along it. Static in screen space. `sun_rays` (0..1, default 1) scales rays and streaks. Each
  pixel sums the two nearest spokes (review fix: the nearest alone jumped by 0.038 glow units at a spoke boundary, seam
  lines just outside the core; summed, the profile is continuous, `law_checks.py`).
- **Streaks.** The long horizontal pair: `0.6 exp(-(y / 0.15)^2 - |x| / 8)`, windowed to 24 core radii.
- **Halo.** Faint and wide: `0.2 / (1 + r^2 / 4)`, windowed to 16 core radii.
- **Occlusion.** The per-pixel knight clip over the whole quad (rays included) as before, and the glow multiplied by
  the core's visible fraction, so a covered core takes its rays with it; the core itself keeps the per-pixel clip. The
  fraction comes from 13 sentinel taps (the centre, six at 0.45 and six at 0.9 core radii, weighted `1 - 0.1 rho^2`,
  normalised): one tap moves the whole burst by at most 8.1 % (review fix: five equal taps moved it in 20 % steps, and a
  tap within half a pixel of a hull edge flipped per frame under the jitter). 17 taps cost 216 slots, over the budget.
- **Quad.** 48 x 32 core radii (0.46 Mpx at 5120x1440, 0.04 Mpx at 1280x768). ps_3_0 197 slots by the production
  counter after the review fixes (22 `texld`: plain `tex2D`, valid without flow control on the one-level
  point-sampled lane; `exp` folded into `exp2` through the uploaded constants); native validator 0 messages.
- **Meter, corrected (review F3).** "Below p99 has no effect" was wrong: tiles under the 1 % index still shift which
  tile is the p99 one. The new core adds 1-4 tiles at 5120x1440 (4-6 at 1280x768) and decodes to at most 4.6, so
  `ev_limit` stays at the scene's own value within a few tiles' shift of the p99 index.

### Step 1c: the glow-dominant look (2026-11-02, AP; not flown)

Run220 found step 1b "a painted circle with rays". Step 1c implements [own-sun-look.md](own-sun-look.md) as decided
there: the core unchanged (0.012 of the height, K = 2, white mix 0.25, edge 0.6-1 R); a Gaussian corona
`0.6 exp(-(r / 1.5)^2)` and a Lorentzian halo `0.3 / (1 + (r / 6)^2)` windowed at 24 R; six soft rays at 0.15 (hashed
lengths 3.50-13.13 R, offset 0.5, Gaussian width `max(3 px, 0.2 R)` without taper, `exp(-r / L) saturate(1 - r / L)^2`
along, the two nearest spokes summed); the horizontal streak pair and its constants deleted; the quad 48 x 48 R. The
13-tap visibility fraction on the glow, the per-pixel clip over the quad, the fog scalar on all, the screen-fixed rays and
the four settings with their defaults are unchanged. ps_3_0 184 slots, native validator 0 messages. The glow outside
the core peaks at 0.781 at K = 2 [M, host test and `glow_meter.py`, which now reads the header's constants by name].
Evidence: [sun-occlusion ledger](../verification/sun-occlusion.md), "Own sun step 1c — host evidence".

### Step 2 as built: the deletion (2026-11-02, both editions; not flown)

Run225 accepted the look (constants of `870a00fd6`); section 6's items 1-3 are done in one code commit after a pure
`git mv` commit, on both editions' shared code. Evidence: [sun-occlusion ledger](../verification/sun-occlusion.md),
"Own sun step 2 — deletion".

- **Deleted.** `src/renderer/lens_visibility_variant.{h,cpp}`, `src/renderer/sun_occlusion_pass.{h,cpp}`, the
  visibility program (`sun_visibility_ps.hlsl`, its `_program_inc.h`, `_taps_inc.h` and provenance record
  `verification/results/sun-visibility-program.json`). Its entry in `tools/shaders/generate_rigid_motion_pixel.py`
  (lines 158-160) is a documented leftover pointing at the deleted files: removing it changes the generator's digest,
  which 64 provenance records pin (`test_bloom_programs`), so it goes at the next generator re-record. In the core header `classify_body`, `footprint`, `Deferral`, `Hold`, `Ready`, `Latch`, `decide` (the
  replicated gates and the "visible" answer), `classify_blend`, the smoothing and jitter helpers; in the bracket inc the
  wrap, scan, classification, refusal/block and drop paths, `sun_lane_uv`, the RT2 reference per bracket; the settings
  `sun_occlusion_core_f`, `sun_occlusion_curve`, `sun_occlusion_radius`, the marker `sun_occlusion_default`; the rows
  `sun_lens_draw`, `sun_visibility`, `sun_lens_body_unclassifiable`, `sun_occlusion_blocked`, `sun_occlusion_released`,
  `sun_occlusion_config`, `sun_lens_bracket`, the `lens_*.bgra8` readback; the hook's `device_reset`, `frame_inputs`,
  `report_pass`, `block`, `lens_*` calls; the launcher options `--sun-occlusion`, `--no-sun-occlusion`,
  `--sun-occlusion-radius/-curve/-core-f` (their variables are in `REMOVED_VARIABLES`, dropped from any inherited
  shell); the GPU fixture of the pass and wraps. Its lens-flare gain cases moved into
  `verification/probe/lens_flare_gain_fixture.cpp` (8 checks).
- **Renamed.** `src/proxy/sun_occlusion{.h,.cpp,_core.h}` -> `own_sun_hook{.h,.cpp,_core.h}` (namespace
  `x3m::own_sun_hook`), `motion_output_sun_occlusion_inc.h` -> `motion_output_lens_bracket_inc.h`, the hook fixture, host
  double, site verifier, build/run scripts and `test_sun_occlusion` -> `own_sun_hook` names; the install row is
  `own_sun_hook own_sun= bracket= log= patched= reason=`. The FL file `sun_occlusion_fl.cpp`, its generated constants,
  `fl.sun_occlusion*` rows and `fl_sun_occlusion_log` keep their names. Settings: `own_sun` (Bool, schema and builtin
  default 1; the read site is on unless the value is "0"; launcher `--own-sun on|off`, sent explicitly, default on) and
  `own_sun_log` (`--own-sun-log`, developer). The ini keys `sun_occlusion` and `sun_occlusion_log` are aliases: the
  parser sets the new key and writes one `config_key key=<old> problem=renamed value=<new>` row; old and new key in one
  file are a duplicate, the last line wins.
- **The probe hook.** AP: installed when the own sun is requested (setting on, route on, radiance > 0) or the bracket is
  wanted (`lens_flare_gain` < 1 or the volumetric fog). In a frame whose latch is off and without the log, every probe
  is forwarded before any read (no main-view walk, no owner read); in an own-sun frame the eligible record is answered
  1 without the original, every other probe forwarded; the log (`own_sun_log`) runs the original itself once per
  logged probe and returns its answer (`sun_probe` row, the own sun's hidden answers are not logged). The 148-byte
  prologue hash stays the install identity. FL: installed for the bracket only; the probe forwards every call (with the
  log: runs it itself for the row); the own sun's FL answer is section 8's later step.
- **The lens bracket.** begin: the fog's share of the gain only (`lens_bracket_begin`); per draw: the gain only
  (`prepare_lens(route)`: one active test, the window count, the `plain` test, `apply_lens_gain`); no end listener.
  The engine cull at gain 0 (`lens_flare_cull`) is unchanged.
- **No hotkey (user decision, same day).** The Ctrl+Shift+F9 comparison chord, its `Toggle` state, the `own_sun_toggle`
  row and the `key=` field of `own_sun_frame` are deleted (a separate commit): `own_sun` in `x3m.ini` is the only switch,
  and the Present-time latch is `frame_active(setting, stage armed in the last resolve)`. F8 is the only in-game key.
- **`own_sun = 0`.** Nothing is answered and nothing drawn on either edition: the engine's chain is vanilla. The hook
  installs only for the gain or the fog, its probe then forwards every call without a read; the bracket scales the
  lens draws by G (and the fog's transmittance), as every other engine flare.
- **Kept beyond section 6's list.** The RT2 container acquire/release and `x3m_sun_occlusion_fixture_lens_depth`, now
  reached only by the retention fixture's case n (the run336 accounting regression; production never acquires); the
  runners' clearing of `X3M_SUN_OCCLUSION_DEFAULT`; the core header's layout and config constants (FL `+0xec`
  static_assert, the site verifier).
- **Cost.** Per lens draw the bracket's work is a subset of what it was (the gain path only; before: the state reads,
  the scan and the body classification whenever the override or its log was on). Per frame the visibility pass, the
  RT2 `GetContainer`/`Release` and the frame-inputs latch are gone; per probe, latch-off frames skip the main-view walk.
  FL native fixture: forward 42.6 ns over the native probe [M].

### Step 3 as built: the FL hidden answer (2026-11-02, FL; not flown)

The own sun draws on Farnham's Legacy (`X3M_EDITION=fl`) with the AP law. Evidence:
[FL ledger](../verification/farnhams-legacy.md), "Own sun step 3".

- **Answer.** `sun_occlusion_fl.cpp` `decide_probe()` mirrors AP: latch off and no log → forwarded before any read;
  otherwise the frame's main view is resolved once (cockpit registry walk), the owner's `+0x270` read only where the
  cheap tests hold, and `core::eligible` with the owner flag `0x400000`; in a latched frame the eligible record is
  answered 1 (EAX 1 through the thunk's `pop edx; pop ecx; popfd; ret`, the original not run) and
  `own_sun_hidden_seen_/own_sun_hidden_frame_` record it; every other probe forwards (`popfd; jmp [target]`) or, with
  `own_sun_log`, runs the original for its `sun_probe` row (hidden answers are not logged, as on AP). The owner's layer
  is read only with the log. `request_own_sun()`, `set_own_sun_frame()`, `own_sun_hidden()` are the shared
  `own_sun_hook.h` interface; the header fence is gone.
- **Install.** The probe call is claimed for the own sun or the bracket; the lens call (`0x8bcb7`, non-atomic) only
  for the bracket (`lens_flare_gain` < 1 or the volumetric fog), as section 8 asks: `install_at` with
  `lens_site = lens_target = 0` claims the probe only (`lens_write=none`; one of the two without the other is
  `invalid_site`). The `fl.sun_occlusion` row gains `own_sun=`.
- **Wiring.** `capture.cpp`: the request, the three settings, `own_sun_on` and `configure_own_sun` are unfenced;
  FL's Present block sets the latch (`frame_active(true, own_sun_ready())`) before its `own_sun_hook::present`, as AP's.
  One status row for both editions, `own_sun status=%s edition=<ap|fl> requested= reason= probe=` (the edition by
  string-literal concatenation, so the AP format string is unchanged); `probe=` is FL's `fl.sun_occlusion` status
  (`active`, `executable_mismatch`, `anchor_mismatch`, `late_claim`, ...), `reason=probe` when it did not install.
  The stage (`own_sun_pass.cpp`, `motion_output_own_sun_inc.h`, the resolve wiring, Reset) is unfenced and
  `own_sun_pass.cpp` moved into the shared source list. AP objects unchanged by this part (strip-debug compare).
- **Review fixes (same day).** `reason=main_view` when the hook installed without a main-view source
  (`own_sun_hook::main_view_known()`; FL: no admitted registry slot): the own sun stays off, the engine's sun is
  vanilla. `lens_flare_gain … bracket=probe_only` when only the probe is claimed (`bracket_installed()`).
- **No-probe fail-safe (both editions, shared).** `own_sun::core::ProbeGuard`: an armed resolve in an active frame
  without the main-view hidden answer is a miss (the `no_probe` path), a hidden answer a hit. The eighth consecutive
  miss withholds the readiness, so the latch stays off and the engine's sun returns (one `--debug` row
  `own_sun_state … reason=no_probe sun=engine misses=8 retry_frames=600`); every 600 frames one readiness is let
  through as a retry, and a hit re-arms for good; a missed retry waits another 600 frames. Reset and
  `configure_own_sun` clear the guard. `own_sun_frame` carries `starved=` and `retries=`. Cost: two integer tests per
  armed resolve. This changes AP's `motion_output.cpp`, `capture.cpp` and `own_sun_hook.cpp` objects (intended).
- **FL-specific risks for the first flight** (nothing here is flown). The main view must be probed before the
  scene-end hook, where the stage reads `own_sun_hidden()`: on FL the compositor call fires before the first view
  with layer > 17 and each view is probed after its draw ([FL sun occlusion](../reverse-engineering/fl/sun-occlusion.md),
  "Thread and order"), so this holds if the main view's layer is ≤ 17 (inferred, as on AP); otherwise every armed
  frame counts `no_probe`, `own_sun_no_probe` is written once and the fail-safe returns the engine's sun after 8
  frames (`starved=1` on `own_sun_frame`). The main view needs the registry slot that
  `sector_background_fl` admits (`fl.sun_occlusion … main_view=registry`; `none` makes nothing eligible: `own=0` in
  `fl.sun_occlusion_sample`). The sun colour is the FL poll (step 7), which needs the shadow cascades like AP's; not Ok
  leaves the stage unarmed (`own_sun_state reason=poll`).
