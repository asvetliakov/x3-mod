# Sun and lens chain under partial occlusion

2026-09-22. Ratified design; **steps 1 and 2 are implemented behind `--sun-occlusion` (default off)**; step 1 flew
once (Run 223) without ever engaging for the sun, which the section "After Run 223" corrects; step 2 is not flown
(sections "Step 1 as built" and "After Run 223" carry the corrections to the text below; ledger:
[verification/sun-occlusion.md](../verification/sun-occlusion.md)). Owning RE note:
[lens-flare-visibility.md](../reverse-engineering/lens-flare-visibility.md) (mechanism),
[sun-material-identity.md](../reverse-engineering/sun-material-identity.md) (resources, late ordering).
Trigger: Run 61 user report, triaged in
[verification/volumetric-fog.md](../verification/volumetric-fog.md) (2026-09-22, Run220). This reopens the
"MINOR, dropped" call of goal 13 on the user's request; `docs/goals.md` is the orchestrator's to change.

## Decision

**Take the occlusion decision away from the engine's binary CPU probe and make it a GPU visibility
fraction `f` in a 1x1 texture, measured from the proxy's own scene depth (RT2 `.b`) over the sun's disc
footprint and multiplied into every draw of the `Lensflare Scene` in the pixel shader. No readback, no
query, no added latency.** Three pieces, one launcher switch (`--sun-occlusion`, default off until flown):

1. **Probe override** at the sole call `0x00471630` (`call 0x00488720`). Replacement keeps the cheap
   vanilla gates (**corrected: three, in vanilla order**: flare video option, the rect test, view flag;
   see "Step 1 as built") and otherwise answers "not occluded", so the chain stays
   instantiated while the sun is inside the engine's screen gate. Whenever the GPU side is not healthy it
   calls the original: vanilla behaviour, per frame.
2. **Visibility pass**, once per frame at the start of the lens bracket: 32 taps of RT2 over the disc
   footprint into a 1x1 FP16 target, temporally smoothed on the GPU (two 1x1 targets, ping-pong, lerp in
   the shader; the pattern of the 1x1 FP16 sky/exposure histories).
3. **Lens bracket** around the lens-scene traversal (`0x00472491`, `call 0x0047e6e0`). Draws inside it get
   a wrapped pixel shader that samples the 1x1 texture and scales colour or alpha by `f`.

Smallest first flyable step: exactly these three with a **uniform** `f` for every lens draw and
single-record frames only. Per-pixel clipping of the core disc sprite against RT2 (so the covered half of
the disc does not draw over the station at reduced strength) is step 2, because it needs a draw
classification that only a capture can give (section "Diagnostic").

## What the user's wish consists of

- **Sprite chain (this note).** Every installed TSuns row has model -1, so what the player sees as "the
  Sun" in an ordinary sector is the lens chain (glow, corona, streaks, ghosts) **plus, corrected, the
  in-scene card of fallback body 31** (`/Sonne`, RE note section 15), which is not in the lens scene. It is one record,
  one boolean (`record+0x30`), and the swept body-195 probe hides it on the first mesh hit, which for a
  probe aimed at the disc centre is "about half covered". This is the whole reported defect.
- **Fog shafts: already independent.** Inscatter and shafts come from the shadow cascades and the phase
  term, not from the sprite. Run220 burst 3 shows it: the chain is hidden for all 8 frames while the fog
  glow behind the station stays at mean 0.39 and max 0.61 to 0.65. Nothing in the fog path changes; the
  shafts "read wrong" only because the glare that anchors them is gone.
- **HDR, bloom, TAA: not involved, by static order.** The lens scene runs at `0x00472442`..`0x00472491`,
  after the scene-end hook `0x004721b1` where the mod does TAA, fog, bloom, AgX and the write-back
  ([hdr-scene-path.md](hdr-scene-path.md): post-boundary draws are not in the FP16 target). So the chain
  is a display-referred overlay in the 8-bit RT0: it is not a bloom source, `--bloom-source-clamp` does
  not touch it, it never enters TAA history, the exposure meter does not see it, and it writes neither
  RT1 nor RT2. `f` therefore scales display-referred values; one exponent constant
  (`--sun-occlusion-curve`, default 1) is the only tuning. Any in-scene sun (body 31 fallback, TPlanets
  sun scenes) is ordinary depth-tested geometry and already fades per pixel, bloom included. That the
  lens draws really land after the write-back is static evidence only; the diagnostic confirms it.
- **Sun shadow lane:** no interaction; it supplies an independent sun direction usable as a cross-check
  of the record's screen position in the diagnostic log.

## Why not the engine-side options

- **(a) Multi-sample the engine probe, drive `record+0x10`.** Loses on three counts. `record+0x10` scales
  **size only** (`record+0x34 = size x acc / 200`, body scale from `+0x34`); colour and alpha are never
  touched, so a continuous value gives a shrinking sun, not a fading one. `0x00471660` re-applies +-100
  every frame, so holding a continuous value needs a second patch inside it. And each sample is a full
  candidate loop through the narrow phase `0x0048a890` on the render thread (N x the cost the RE note
  already lists as a weakness), with a probe whose thickness (body 195) is undecoded, still limited to
  255 near-field candidates and LOD state. Continuous intensity has to be applied in the proxy anyway.
- **D3D9 occlusion queries.** Documented and native, but asynchronous (1 to 3 frames), need `GetData`
  polling on the hot path, give a CPU number that must then be smoothed and uploaded, and their behaviour
  on the CrossOver D3DMetal/wined3d stack is unmeasured (the EXE never creates one, RE note section 1, so
  no evidence exists). CPU readback (`GetRenderTargetData`) is a pipeline stall. The 1x1 texture has
  neither problem and uses nothing beyond render-to-texture and a texture fetch.

## Hook sites and structures

Bytes below were re-read from the verified EXE (SHA-256 `fdbf3418...f8ab`, 2,153,984 bytes) in this
session; "RE" marks what the RE note already proves.

| Site | Bytes / fact | Status |
| --- | --- | --- |
| `0x00471630` | `e8 eb 70 01 00` -> `0x00488720`; preceded by `51 56` (`push ecx` = view from `[esp+0x14]`, `push esi` = record), followed by `83 c4 08 85 c0 74 03 89 6e 30` | Bytes verified here; `__cdecl (record, view)`, EAX result, single caller: RE |
| `0x00488720` gates | `*(*0x00606f34 + 0xfc) & 0x8000` clear -> 1; `view+0x270 & 0x8000000` -> 1 | RE section 3 |
| `0x00472491` | `e8 4a c2 00 00` -> `0x0047e6e0` (lens traversal); block skipped as a whole by `0f 84 ad 00 00 00` at `0x00472439` -> `0x004724ec` | Bytes verified here; callee ABI and draw synchrony **open (Q3)** |
| `0x00472442` | `e8 19 f2 ff ff` -> `0x00471660` | Bytes verified; not needed unless Q3 fails |
| record `+0x10`, `+0x30`, `+0x38`, `+0x28`, `+0x34` | accumulator, visible flag, lens group, depth const, size | RE |
| record `+0x20/+0x24` | screen position: **format and frame unknown (Q2)** | open |

Install follows the house pattern: `engine_patch::claim_call` (exact site bytes and expected target,
install window before the first Present, atomic write, rollback, `restore_call`), a naked thunk as in
`collide_memo.cpp` (no x87, MXCSR untouched, `LastError` preserved, callee-saved registers intact, SSE2
only, `-mstackrealign` handlers), exact-EXE identity gate, and a static verifier
`verification/probe/verify_sun_occlusion_sites.py` for boundaries and incoming edges. For `0x00472491`
reuse the pre/original/post transport of `compositor_bridge` so begin and end come from one site. Any
mismatch leaves both sites unpatched and the feature off: vanilla.

`disassemble` task, one brief, static only:

- **Q2.** In the collector `0x0047e315`..`0x0047e5b0` and its consumer in `0x00471660` (`node+0xb0/+0xb4`
  from screen coordinates x viewport size): the fixed-point format, origin, y direction and reference
  rectangle of `record+0x20/+0x24`, and what `+0x34` is in pixels. Deliver the formula record -> back-buffer UV.
- **Q3.** Are all lens-scene `DrawPrimitive`/`DrawIndexedPrimitive` calls issued inside the call tree of
  `0x0047e6e0` before it returns (nothing queued past `0x00472496`)? Stack contract of `0x0047e6e0`
  (the `83 c4 04` at `0x004724a2`), incoming edges into `0x0047248b`..`0x00472496`.
- **Q4.** Other readers of the probe's side effects: the candidate cache `view+0x37c`, the body-195 node
  at root `+0x6c`, and `record+0x30` outside `0x004715d0`/`0x00471660`/the collector. Skipping
  `0x00488720` must change nothing else.
- **Q5.** Which views reach `0x004715d0` (main 3D, monitors, cockpit) and the cheapest field that
  identifies the main view; the override applies to that view only, every other view keeps the original.
- **Q6 (step 2).** Lensflares 20-byte row semantics: is position factor 0 "on the sun", which rows are
  the core disc; and whether fallback body 31 draws a visible in-scene disc.

## Runtime behaviour

**Override (CPU, per live record per view, a handful per frame).** `ready` = feature on, both sites
claimed, route active with RT2 bound this frame, 1x1 targets exist, last frame's visibility pass ran, main
view, exactly one live record this frame. Not ready -> tail-call the original. Ready -> the two gates, else
return 0 and latch the record's UV, size and "new record" (`+0x10 == 0`) for the pass. Cost: a few loads
and compares; it **removes** the per-view candidate walk and narrow-phase loop whenever ready (saving
unmeasured; the collide hooks on `0x0048a890` simply see fewer queries).

**Visibility pass (GPU, once per frame).** One 1-pixel draw: 32 Vogel-disc taps of RT2 around the latched
UV, radius = disc radius (`--sun-occlusion-radius`, default taken from the capture; aspect corrected). A
tap is open when RT2 holds the sentinel/sky value, the sun being beyond every routed surface. Taps outside
the viewport are dropped from numerator and denominator; none valid -> keep history. Output
`lerp(history, open/valid, 1 - exp(-dt/80 ms))`, seeded without smoothing on a new record, after Reset
and after any frame the pass did not run. A dead band `saturate((f - 0.03) / 0.94)` on use guarantees an
exact 0 behind a full cover and absorbs the one-tap jitter flicker at edges (RT2 is jittered). About 15
device calls including state save and restore; RT0/RT1/RT2 bindings returned as the fog transaction does.

**Lens draws (GPU, about 10 per frame; 103 body occurrences over 9 groups).** Outside the bracket the hot
path pays one predictable flag test per draw. Inside: swap to the wrapped pixel shader (cached per
original shader pointer, built once), bind the 1x1 texture to a free sampler, one constant selecting the
channel: alpha when `SRCBLEND = SRCALPHA` (the authored MATERIAL6 state), rgb when `SRCBLEND = ONE`. If a
lens draw has a pixel shader the wrapper cannot transform (fixed function, ps_1_x without a free stage),
the draw is skipped while `f < 1` would matter; the diagnostic counts how many such draws exist.

**Failure direction.** If the override answered for a frame and the visibility pass then cannot run, the
lens draws of that frame are dropped (one hidden frame, never shine-through) and `ready` falls to false,
so the next frame is vanilla. Device loss and Reset release the two DEFAULT-pool targets; vanilla until
recreated and seeded.

**Temporal and edge cases.** Fade follows covered fraction with an 80 ms time constant; the engine's
boolean no longer flips on occluders, so its two-frame size step occurs only when the sun enters or leaves
the engine's own screen/FOV gate, as in vanilla (out of scope). Sun off-screen or sector without a sun: no
record, the override is never called, the pass is skipped, cost is the flag test. More than one live
record in a frame (multi-sun sectors, entries 10/11): step 1 is not ready -> vanilla for that frame;
step 2 may add one texel per record and assign ghosts to the nearest sun-to-centre line. Flare option off:
gate 1 returns 1, as vanilla. A fully covered sun keeps its sprites alive at `f = 0`: the blend fill the
game already pays for a visible sun, now also paid while hidden; accepted, revisit only if measured.
Cockpit geometry is not in RT2 and the lens scene draws after the cockpit view; vanilla also draws the
chain over the cockpit, and whether the probe ever saw cockpit meshes is unknown (Q5 covers it).

## Native Windows

Only documented D3D9: `CreateTexture` RENDERTARGET `A16B16G16R16F` 1x1 (the HDR path already requires
the format; without it the feature is off), `SetRenderTarget`, a ps_3_0 pass, a texture fetch in the
wrapped shader. No blending on FP16 (ping-pong instead), no query, no readback, no Wine export. The EXE
patches are byte-validated game-internal hooks, in scope on both platforms. Status after implementation:
Windows-compatible source and cross-compilation, **not** verified native behaviour; record the gap in
[platform-portability.md](platform-portability.md).

## Diagnostic and the flight

The proxy logs nothing about this path today (Run220: zero attributable draws). To avoid two load cycles,
the step-1 candidate carries the diagnostic: with `X3M_SUN_OCCLUSION_LOG=1` the override logs per call
`frame, view, record, +0x10, +0x20, +0x24, +0x30, +0x34, +0x38, original result` (it calls the original
while logging, feature on or off), and the bracket logs per draw the VS/PS hashes, stage-0 texture
identity and size, SRC/DEST blend, Z state, bound RT0 and primitive count. One flight, chase view, usual
Run 61 command plus `--sun-occlusion`, `X3M_MOTION_FRAME_LOG=1`, `--object-trace`, comparison hotkey for
the feature:

1. Sun fully clear, feature off: F8 burst (fingerprint: draw count, shaders, textures, RT, core size).
2. Sun about half behind a station edge, feature on: F8 burst; same place feature off: F8 burst.
3. Slow pass of the sun fully behind the station and out again, feature on: F8 burst at full cover.

The user reports pop or fade, shine-through at full cover, and whether the half-strength disc over the
station edge is objectionable (that decides whether step 2 is needed).

## Fixtures (all Wine commands as `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py ...`)

- Host, no Wine: site verifier on the EXE; unit test of the gate table and `ready` state machine.
- Hook fixture (synthetic `E8` site, fake record/view): gate answers, fallback reaches the original when
  not ready, register/flags/`LastError`/MXCSR preservation, late-claim refusal, restore.
- GPU fixture: synthetic RT2 with a half-plane occluder at 0, 25, 50, 75, 100 % of the disc; fixture-only
  readback of the 1x1 expects `f` within 1/32, exact 0 and 1 at the ends after the dead band, convergence
  in the expected frame count, seeding on new record, off-viewport taps, Reset gap -> vanilla; a quad
  through the wrapped shader under both blend modes equals `f x` reference; surrounding state and
  RT1/RT2 bytes unchanged.

## Risks and unknowns

- Q3 false (draws deferred past the traversal) breaks the bracket; fallback is begin at `0x00472442`
  and end at the Text boundary `0x004724ec`, to be decided by the disassembly.
- Legacy MATERIAL3 flare bodies may use fixed function or SM1 pixel shaders; the wrapper coverage is
  unknown until flight step 1.
- Opaque draws that the route refuses do not write RT2 and will not occlude (fails towards visible; the
  vanilla probe had the opposite holes). Disc radius default is a guess until the fingerprint burst.
- Uniform `f` lets the core disc show over the occluder at reduced strength; step 2 (per-pixel RT2 test
  for draws classified as core via Q6 and the fingerprint) removes it.
- Cost figures above are counts, not timings; measure the pass and the bracket with the existing
  per-pass timing before the candidate is frozen.

## Step 1 as built (2026-09-22)

Evidence and commands: [verification/sun-occlusion.md](../verification/sun-occlusion.md). Static answers to
Q2-Q6: RE note sections 11-16.

**Corrections to the design above.**

1. **Three gates, not two.** `0x00488720` tests, in this order, the flare video option (-> 1), the record's
   position against the probing view's rect (-> 0 when outside) and `view+0x270 & 0x8000000` (-> 1). The
   override replicates all three in that order (`sun_occlusion_core.h`, `decide`): with two gates it would
   have answered "hidden" for a record outside the rect while `0x8000000` is set, where vanilla answers 0.
   The whole 148-byte prologue is compared (FNV-1a) before patching, because the override restates it.
2. **Body 31 is part of the visible sun and is not in the lens scene.** The `/Sonne` card is ordinary
   in-scene geometry of the main view; step 1 does not touch it. For step 2: it draws before the scene
   end, so it is in the FP16 target (bloom source, exposure, TAA) and is already depth-tested per pixel;
   what remains open is whether it writes RT2 (if it does, the visibility pass sees the sun occluding
   itself; the `f_raw` of the diagnostic answers that in the first burst with a clear sun) and whether its
   hard depth clip next to a faded chain is objectionable.

**What differs from the text above, and why.**

- *Eligible probe* (**superseded by "After Run 223"**, kept for the record: as first built, the override acted
  when `record+0x8 == view` and `view == *(cockpit+0x58)` of the active cockpit with `+0x270 & 0x10000`, which
  the sun's record never satisfies because the background view owns it). Now: the override acts on the **main
  view's re-probe of a record owned by a background-regime view** (`view == main_view`, `record+0x8 != view`,
  `owner+0x270 & 0x400000`; `core::eligible`). The main view is `*(cockpit+0x58)` of the active cockpit with
  `+0x270 & 0x10000` (the registry walk of `sector_background.h`, resolved once per frame through
  `engine_memory`); the owner's flags are one validated read per eligible-shaped probe. The owner's own probe
  (layer 15), later views' re-probes and every record the main view owns itself run the original and are not
  counted in `Ready::records`.
- *Readiness* (`core::Ready`): exactly one record of the main view in this frame, the same one as in the
  previous frame, and the visibility pass ran for it in the previous frame; not blocked; not after a Reset.
  A second sun is detected at its own probe call, so the first record of that one frame was already
  answered; from the next frame both are vanilla.
- *Open test:* RT2 `.r < -0.5` (the route's -1 sentinel; every RT2 format has `.r`), not `.b`.
- *Radius* (**superseded by "After Run 223"**): the sun's `record+0x34` saturates, so the radius is the
  configured absolute `--sun-occlusion-radius` (default 0.04 u); the derived value (`core::footprint`, RE note
  section 11) is logged where the size is not saturated.
- *Smoothing:* `1 - exp(-dt / 80 ms)` from QPC between passes; a seed (no smoothing) on a new record,
  after Reset, after a failed or skipped pass and after a gap over 0.5 s. The 1x1 holds `.r` smoothed,
  `.g` used = `pow(saturate((r - 0.03) / 0.94), curve)`, `.b` raw, `.a` valid / 32; the wraps read `.g`.
- *Lens draws:* a structural wrap of ps_2_0 / ps_2_x / ps_3_0 (`lens_visibility_variant.h`), built once
  per program and blend class, the fraction's texture on the highest sampler the program does not
  declare, no application constant touched (the wrap's constant is a `def`). Scale by blend law:
  SRCALPHA with ONE or INVSRCALPHA -> alpha; ONE with ONE or INVSRCCOLOR -> rgb; ONE / INVSRCALPHA ->
  both; alpha test admitted only for GREATER / GREATEREQUAL with ref <= 8 when alpha is scaled (error
  bounded by ref / 255); **`D3DRS_FOGENABLE` refused** (fixed-function fog is applied after the pixel
  shader, so `f = 0` would still show fog colour); everything else refused. The draw's blend, alpha-test
  and fog state and the bound program come from the route's shadow state, not from device getters; per
  wrapped draw the pass issues five calls (capture of a recorded block holding the application's shader,
  the wrap sampler's texture and six states; apply of ours; texture; shader; apply of the capture).
- *A draw that cannot carry the fraction* (fixed function, ps_1_x, an unknown hash, a refused blend law,
  fog, a refused wrap, a draw the route itself rebinds) *is dropped together with the rest of that
  bracket* (review 2026-09-22): `f` exists on the GPU only, so the frame cannot know that it is 1, and an
  unscaled element must never show through geometry. The refusal then **blocks the override for the
  process** (a Reset does not lift it: the refusal belongs to the program or the material, and lifting it
  would replay the dropped frame once per Reset); from the next frame the engine's probe decides (one
  `sun_occlusion_blocked` line). A transient refusal (shadow state unknown, a device error) costs that
  frame and the next, without the block. The chain therefore either fades as a whole or is vanilla.
- *Failure direction:* a pass that is skipped untouched (a query open, a block recording, the scene end
  late) keeps the last smoothed fraction of the same record in use for at most four consecutive frames
  (`core::Hold`) and the override keeps answering, so a periodic skip cannot make the flare flicker under
  open sky. After that, after a failed pass, or without a fraction, the lens draws of the frame are
  dropped (`D3D_OK`, not submitted) and the next frame is vanilla.
- *Bracket transport:* not `compositor_bridge` (it transports calls without stack arguments and has one
  global binding, which the scene hook owns). A cdecl thunk calls begin, the original with a copy of
  the view argument, and end; begin and end run under `PreserveCpuState`. Both thunks save EFLAGS and
  clear DF around the C handlers; the probe handler runs inside a `GetLastError` / `SetLastError`
  envelope (the frame's first probe reaches `engine_memory`'s `VirtualQuery`). The owner thread is fixed
  for the process; a probe from another thread runs the original and is logged once.
  `present()` closes a bracket an unwind left open.
- *Conflict:* `X3M_SUBMIT_PHASES`' `sort_return_b` stamp claims `0x00472490..0x00472495`. The feature is
  refused by name when that variable is set, and the launcher refuses the combination; either order of
  installation would also fail closed on bytes. No other claim under `src/proxy` touches either call
  (`verify_sun_occlusion_sites.py`; the scene hook is `0x004721b1`).
- *Not touched:* the size ramp (`record+0x10`, `0x00471660`), the 2D overlay call at `0x004724a7`
  (outside the bracket), body 31.

**Diagnostic (`--sun-occlusion-log`, with or without the feature).** `sun_probe` per probe call (frame,
view, record, owner, main view, `+0x10/+0x20/+0x24/+0x30/+0x34/+0x38`, view flags and layer, records so
far, ready, the vanilla result, the answer; the original always runs, once); `sun_visibility` per frame
(latch, footprint, radius and whether it was derived, weight, seed, skip reason, a synchronous 1x1
readback: smoothed, used, raw, valid taps); `sun_lens_draw` per draw inside the bracket (VS / PS hashes,
PS model, verdict, topology and counts, blend / z / alpha-test / colour-write state, stage 0 and 1
texture identity, size and format, RT0); `sun_lens_bracket` per frame (draws, wrapped, refused, dropped).

**For the flight** the three bursts of "Diagnostic and the flight" stand; add `--sun-occlusion-log` to
all of them. Read first: `sun_occlusion ... patched=1 reason=ok`; with a clear sun `f_raw` near 1 (else
RT2 is written where the sun shows, see correction 2); `sun_lens_draw verdict=` all `applied` (else the
refusal names what step 1 cannot wrap and the override is blocked); `radius_u` against the disc in the
screenshot.

## After Run 223 (2026-09-22): eligibility, radius, step 2

Measured in the first flight (ledger, "Run 223 triage"): the sun's lens record is owned by the layer-15
background-regime view (`+0x270 = 0x00400135`), the only probe that ever hid it was the main view's cross-view
re-probe, `record+0x34` is saturated (`0xffff x acc / 200`) in every frame of both suns, and the main view's own
records (ship flares, group 25) engaged the override and produced 24 dropped-bracket frames. Three corrections and
step 2, all behind the same switch, fixture-verified, not flown:

1. **Eligibility.** `core::eligible(record_owner, view, main_view, owner_flags)`: `view == main_view`,
   `record_owner != view`, `owner+0x270 & 0x400000`. Only such probes register in `Ready` (one sun = one
   background-owned record per frame; a second one is the multi-sun case). Main-view-owned records are the
   original's and no longer count, which removes the dropped brackets. The owner's flags are read through
   `engine_memory` (validated, cached per region and frame) only when the cheap pointer tests already hold. The
   `sun_probe` line carries `owner_flags270`, `owner_layer` and `eligible=` (formerly `own=`).
2. **Radius.** `core::footprint` reports `saturated` (`size >= 0xffff x acc / 200`) and derives nothing then. The
   pass always samples the configured absolute radius `X3M_SUN_OCCLUSION_RADIUS` (half-width as a fraction of the
   back-buffer width, the record's half-NDC unit; 0.005..0.25, default **0.04 = 51 px at 1280**), floor 1.5 px;
   `sun_visibility` logs `radius_u`, `radius_px`, `radius_derived_u` (0 when saturated or unknown) and
   `saturated=`. **Calibration** from the next flight's clear-sky burst: open `lens_<device>_<frame>.bgra8`
   (the Present-time back-buffer readback, below), measure the glow disc's half-width in pixels around
   (`u x width`, `v x height`) of the `sun_visibility` line and set `--sun-occlusion-radius` to that over the
   width; `f_raw` must read 1.0 in the clear burst and fall only once the station edge enters that disc.
3. **Log.** `sun_visibility` also carries the owner (`owner`, `owner_layer`, `owner_flags270`) and the sun
   lane's direction (the frame's validated `LightDir_Dir0`) projected with the scene camera as `lane_u` /
   `lane_v` beside the record's `u` / `v` (`lane=0` when no latch or camera, or the sun is behind the camera):
   the two must agree to a few pixels, which checks the record -> uv mapping against an independent source.

**Step 2 as built (per-pixel clip of the core bodies).** Fingerprint from Run 223: every lens draw of the sun is
`vs d5e1c75351ed3f04 / ps 8360f422de08b5bd`, vs_2_0 / ps_2_0, ONE/ONE additive, z off. Inside the bracket each
draw is classified on the CPU and wrapped accordingly:

- *Classification* (`core::classify_body`). The pair's vertex wrap identifies the clip rows `c[K..K+3]` (the
  program's four `dp4 oPos.{x,y,z,w}, r, c[K+lane]`; K = 0 for the fingerprinted program). The body's local
  origin lands at clip `(cK.w, cK+1.w, cK+2.w, cK+3.w)`, read from the route's clip-row shadow of that window
  (the same shadow the TAA jitter uses; `rows_known` required), and compared with the sun's uv: within
  `body_tolerance_u` = 0.005 (6.4 px at 1280) -> **Core** (position factor 1: glow, rays, streaks, cards);
  collinear with the sun and the screen centre within the same distance -> **Ghost**; anything else ->
  **Other** (another record's body: untouched, so the sun's `f` no longer scales ship flares). Assumption bound
  to the fingerprint: the program's position source is the homogeneous local position `(v0, 1)`, so the origin
  transform is the rows' `.w` column (true for `d5e1c753...`: `mad r0, v0.xyzx, c14.xxxy, c14.yyyx`). The
  classification is done on the CPU rather than in the pixel shader (the design's earlier sketch) because it needs
  no per-frame pixel constant and makes the class and the centre visible per draw in `sun_lens_draw`
  (`body=`, `centre_u/v`, `centre_dist_u`, `matrix_register`, `rows_known`).
- *Ghost*: the step-1 pixel wrap (`x f`), five device calls, as before.
- *Core*: the clip pair (`lens_visibility_variant.h`, part 2): the vertex program with every `oPos` write
  redirected to a free temporary and `oPos` / a free `oTn` written from it (n free in both programs, 7 for the
  fingerprinted pair), and the pixel program with the step-1 wrap plus `dcl tn`, uv = `(tn.xy / tn.w) x (0.5,
  -0.5) + (0.5 + dx/2, 0.5 + dy/2)` (under D3D9's pixel-centre rule a fragment's clip-derived uv is a texel
  edge; the half texel puts the taps on texel centres on any implementation), nine point taps of RT2 (the
  fragment and one and two whole pixels along +-x / +-y) and `open_px` = the ninths whose `.r < 0` (the route's
  sentinel: no routed surface in front of the far plane), then **`rO *= open_px`**: a core body is clipped, not
  scaled. Rationale: `f` is the disc's open fraction, which the per-pixel clip already realises exactly for the
  core (the glow's pixels behind geometry are removed, the open ones are what the eye sees at full strength);
  multiplying by `f` as well is a double attenuation the physical picture does not have. The ghosts and streaks,
  which have no geometry of their own to clip against, carry `f` (their brightness follows the amount of
  unoccluded disc). Flight (run235, Run 65 session A) preferred the product anyway - the clipped disc dimming with
  the covered fraction reads better in the game than the clip alone - so `rO *= f * open_px` is the **default**
  whenever the override is on (`X3M_SUN_OCCLUSION_CORE_F` unset or `1`, `--sun-occlusion-core-f on`), and
  `X3M_SUN_OCCLUSION_CORE_F=0` / `--sun-occlusion-core-f off` restores the clip-only form described above. The GPU
  path is unchanged; only which of the two wraps is built changed. The soft edge spans five columns
  (1, 7/9, 5/9, 4/9, 2/9, 0 across a vertical edge, fixture-measured: the knight-move kernel of "Jitter contract").
  The wrapped program stays **ps_2_0**: 40 arithmetic and 11 texture
  instructions, 9 temporaries, dependent-read depth 1 (limits 64 / 32 / 12 / 4), so no promotion; a vs_3_0 /
  ps_3_0 pair is refused for the clip (`unsupported_version`), as is a vertex program whose `oPos` is not the
  four-dp4 shape (the refusal blocks the override for the process, like every program-bound refusal). Seven
  device calls per core draw: capture of a recorded block (both shaders, both samplers' textures and six states
  each, and the four `def` registers of the wrap: on native D3D9 a `def` loads the device's constant file when
  the program is set, so the block also carries the application's values of those registers back; the step-1
  wrap's block does the same for its one), apply of ours, `SetTexture` x 2 (the 1x1 fraction and RT2),
  `SetPixelShader`, `SetVertexShader`, and one apply of the capture after the draw. The wrap is refused
  (`resource_limit`) when the original's counted ps_2_0 slots plus the wrap's (32 arithmetic + 10 texture; step 1:
  3 + 1) would exceed 64 / 32: wined3d does not enforce the limits, native D3D9 does. RT2 is referenced for the bracket (begin .. end, also in a held
  frame, whose RT2 is complete) and released at the bracket's end, before a Reset and at teardown. The pixel
  wraps bake one pixel of RT2 in uv and are rebuilt once when RT2's size changes; programs survive Reset,
  the blocks are re-recorded.
- *Classification before any build, fail closed towards "not ours".* `lens_prepare` only scans the vertex program
  (once per pair, no device object): a vertex program that is not the four-dp4 shape, whose position source is not
  `(position.xyz, 1)` (`origin_known`: `mov rS, vP` of the `dcl_position0` input, or the fingerprinted
  `mad rS, vP.xyzx, cA.xxxy, cA.yyyx` with `cA` a `def` evaluating to mul (1,1,1,0) / add (0,0,0,1); any other last
  write, or a write between the dp4s, fails it), or whose clip rows are in no shadowed window cannot be the
  fingerprinted sun program: its body is **Other** (untouched, no wrap built, never blocking), logged once per pair
  (`sun_lens_body_unclassifiable`). Other records' bodies and the log-only mode therefore never create a shader.
  A core body's vertex wrap is created on its first core draw. Only a transient state is a refusal, and a
  transient one: the rows not yet set in this device generation (`rows_unknown`), a centre behind the camera
  (`body_unknown`), a missing RT2. `block()` is reached only through a Core / Ghost body of the eligible record
  (its blend law, fog, a refused wrap).

**Diagnostic added for the next flight.** With `--sun-occlusion-log`, on F8 capture frames whose lens bracket
drew, `MotionOutput::before_present` reads the presented back buffer back as `lens_<device>_<frame>.bgra8`
(`sun_lens_readback` line; 32-bit non-multisampled back buffers only). Every other dump of the frame precedes the
lens block, so this is the only file that contains the chain. Read first in the next flight: `sun_probe ...
eligible=1 ready=1 answer=0` for the sun's record at `layer=16`; `sun_visibility ... lane=1` with `lane_u/v`
within a few pixels of `u/v` and `f_raw` = 1 under clear sky; `sun_lens_draw body=core clipped=1` for the glow
and `body=other` for ship flares; the `lens_*.bgra8` picture against `radius_px`. **Eligibility risk:** a second
view with `+0x270 & 0x400000` owning a lens record (a second sun's background view, or a nebula regime that keeps
its own record) makes two eligible records per frame and step 1 stays vanilla for that sector: watch
`multi_record_frames` (the `sun_occlusion` counters) and `sun_visibility single=0`.

## Jitter contract (2026-09-22)

RT2 is on the frame's **jittered** raster: every scene draw whose program has a table row gets
`MotionOutput::apply_jitter` (clip x += 2 jx / W · w, clip y += −2 jy / H · w: the image moves +jx px right,
+jy px down), while the lens bracket runs after `hook_scene_end`, where `scene_bound()` refuses and nothing is
jittered: the disc's fragments, the engine's record position (`u/v`) and the sun lane (`lane_u/v`, from the
unjittered `camera_scene_`) are all on the unjittered grid. The scene point at unjittered uv `p` therefore sits
at `p + (jx / W, jy / H)` in RT2. The visibility pass takes that offset on `c2.xy` (`sun_occlusion::core::jitter_uv`,
zero when the jitter is off; `SunVisibilityFrame::jitter_u/_v`, refused beyond ±0.05 uv) and reads each of the
32 taps at `p + c2.xy`, the viewport test staying on `p`: a tap farther than half a texel from a silhouette now
reads the same texel content in every phase (before, a tap within one texel of the edge on the wrong side flipped
with the phase: run228's 0.4688 / 0.5000 alternation). The `sun_visibility` line logs `jitter=… jitter_index=…
jitter_x=… jitter_y=…`. The clip pair is **not** offset: its nine taps are texel-centred point taps of the fragment's
own texel, and `floor(i + 0.5 + j) = i` for every |j| < 0.5, so the offset would select the same texel. The
clip's edge column is the jittered raster's silhouette column `ceil(e − 0.5 + jx)`, which moves by one texel
between phases whenever the silhouette's sub-pixel position is not on a texel boundary; a single jittered raster
holds one bit per texel and no per-frame reconstruction recovers the sub-texel edge (a de-jittered 2x2 coverage
estimate reduces the per-column phase range only from 5/9 to 0.49 in a model of a vertical edge). The resolve
averages that wobble away for the scene; nothing averages it for the disc. What the clip can do is bound the
step: its kernel is the fragment plus the eight knight moves (±2, ±1) / (±1, ±2), one ninth each, so that
every column, row and 45° diagonal of taps weighs at most 2/9 = 0.222, against 5/9 for the former ±1 / ±2
cross (an exhaustive search over symmetric integer-offset kernels of ≤ 17 taps finds no bound below 7/32 on a
1/64 weight grid, and the knight set is the only 9-tap shape reaching it; the uniform ninth keeps one weight in
`cK.w` and every tap addressed from the fragment's uv by one signed swizzle of `cD = (2dx, dy, dx, 2dy)` or
`cW = (−2dx, dy, −dx, 2dy)`, so no tap depends on the one before; 40 arithmetic / 11 texture as before, one more
`def` constant). A one-texel silhouette shift therefore moves any pixel of the clipped disc by at most 0.22 of
its value, spread over five columns (profile 1, 7/9, 5/9, 4/9, 2/9, 0), instead of 0.56 at one column. Removing
the residual wobble altogether would need temporal accumulation of the clip (a disc-relative mask ping-pong like
the 1x1 fraction).

*Limitation of the c2 correction.* The offset assumes every RT2 depth writer was jittered. A scene draw whose
program has a table row or is a reviewed prepass program but whose clip rows are in no shadowed window returns
early from `apply_jitter` (`!shadow_.rows_known[window]`, motion_output.cpp) and rasterizes unjittered, as does
any z-writing draw without a row at all; the latter are counted per frame in `unjittered_depth_writers` of the
`motion_output_frame` line (`counters_.unjittered_depth_writers`, incremented beside the `apply_jitter` call). An
unjittered silhouette in RT2 is then shifted by the offset by up to half a texel in the wrong direction: its taps
flip in the phases that would otherwise have been stable, i.e. the pre-fix behaviour for that occluder, not
worse. A flight that still sees `f_raw` alternating with `jitter_index` behind a specific occluder should read
`unjittered_depth_writers` in the same frames first.
