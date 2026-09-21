# Sun and lens chain under partial occlusion

2026-09-22. Design note, nothing implemented. Owning RE note:
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

1. **Probe override** at the sole call `0x00471630` (`call 0x00488720`). Replacement keeps the two cheap
   vanilla gates (flare video option, view flag) and otherwise answers "not occluded", so the chain stays
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
  Sun" in an ordinary sector is the lens chain itself: glow, corona, streaks, ghosts. It is one record,
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
