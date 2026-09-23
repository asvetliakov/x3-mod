# Volumetric fog: near-camera dust motes

Design note, 2026-09-23 (Fable, high), for change 8 of
[fog-visual-direction-review-2026-09-21.md](fog-visual-direction-review-2026-09-21.md): the sense-of-speed
and scale cue the stored-density fog lacks. Built **default off** behind one launcher option on 2026-09-23;
what differs from the design below and the fixture numbers are in "As built" at the end. In the design
sections every cost is an estimate [I]; [M] marks numbers read from the notes or source. Units: 5 render units = 1 m, 5000 per km.
Owning runtime notes: [fog-density-runtime-integration.md](fog-density-runtime-integration.md) (the L2
look, rows c22-c41, the transaction), [fog-shadow-pass.md](fog-shadow-pass.md) (grid variant),
[seta-sky-hull-share-decay.md](seta-sky-hull-share-decay.md) and
[temporal-integration.md](temporal-integration.md) (what the resolve does to unrouted content).

## Decision

**A world-anchored periodic point lattice seen through a camera-centred window, drawn as one indexed draw of
N expanded quads (static VB, shader-side wrap and velocity streak) as a new last stage of the existing fog
transaction, after the repair draw, additively blended, occluded per pixel by RT2 depth, unrouted: no motion,
no depth write, no resolve mask.** TAA sees the motes as it sees every unrouted sentinel pixel (far-plane
history, 3x3 clip, sentinel stabiliser); they cannot write an exit mark because the mark is gated on a routed
neighbour's parallax and motes write neither RT1 nor RT2. Density, colour, phase, ambient and sun visibility
are the march's own functions evaluated once per mote at its position from the constants and samplers the
repair leaves bound, so a clear or idle sector draws none by construction (the transaction's zero-call path).

## 1. Representation

| Item | Value | Why |
| --- | --- | --- |
| Seeds | N per cube of side W = 2R, R = 1000 units (200 m), hashed from the index at creation (`SEED`), no RNG state | The review's 400 m cube; a fighter at 100-200 m/s crosses it in 5-10 s, under SETA in 0.5-1 s |
| Wrap | CPU (double): `c_m = cam mod W`; VS: `q = s_i - c_m`, `q -= W round(q/W)`, so `q` in [-R, R)^3 camera-relative, float32 exact to 1e-4 units. A sector jump or view cut shows another slice of the same lattice: no reseed | The pass's `FogWorldBasis::origin` (double, the run197-admitted camera at 1e-3 Gram; first-person included) is the only camera input: no new reader |
| Visibility volume | brightness x `saturate((R - |q|)/(.25 R))` x `saturate((|q| - NEAR)/NEAR)`, NEAR 25 units (5 m) | Spherical fade hides the cube faces where points enter; the near fade keeps a mote from filling the screen in first person, where no hull occludes the nose |
| On screen | N = 2048 default -> ~1070 in the sphere, ~120 in a 90x60 deg frustum [I: solid-angle fraction 11.5 %] | 512 gave ~30 on screen, too sparse; 8192 is the cap |
| Size | `size_px = clamp(K/|q|, SIZE, MAX_PX)`, SIZE 4 px, MAX_PX 12, K = SIZE x R | Sub-pixel points do not survive the 3x3 clip (section 3); everything at the wrap radius is at the minimum |
| Streak | screen segment from `P_prev(q + delta)` (previous basis, previous drift time) to `P(q)`, length clamped to STREAK px (128); radiance x `size/(size + L)` | Energy conservation: a long SETA streak is faint; the streak also overlaps its previous position, the TAA argument of section 3 |
| Drift | `q += DRIFT sin(w t + phi_i)` per axis, DRIFT 20 units, period 8 s, VS only | Motes move at rest (docked, idle); world-anchored, so no state |
| Geometry | static DEFAULT VB: N x 4 vertices (seed xyz, corner uv, 20 B: 160 KB) + IB N x 6 x 2 B (24 KB), `DrawIndexedPrimitive` once; created at `prepare_density` beside the grid target, released with `release_targets`, recreated after Reset by the next latch; counted in `allocations()` | Same lifetime rule as the grid; no MANAGED pool (a D3D9Ex-style device refuses it and the retention probe exists only because that is uncertain) |
| VS (vs_3_0) | wrap, drift, near/far fade, current and previous projection, capsule expansion (perpendicular +/- size/2, along the streak), degenerate (w = 0; superseded, see As built: one point outside the clip volume) when view z <= 0 or the fade is 0; ~11 constant rows, one upload (12 as built) | No VTF, no instancing, no point sprites |
| PS (ps_3_0), one source, two variants (`FOG_SHADOW_PASS`) like the march | capsule falloff (8 ALU); occlusion `vis = geometry(d) ? saturate((d.b - z)/(SOFT z)) : 1` from RT2 at s0 (1 fetch, `valid_geometry_depth` of the include; SOFT .02, 0 = hard clip); density `rho = lerp(far, fine, lambda)` at `cam_local + q` with the march's `level_sample` (4 fetches) and the look remap (coverage waves, warp: the same include, rows c22-c35); colour `albedo x (phase x E_sun/pi x visibility + ambient)` with the look's two-lobe phase on the mote direction, its two-colour ambient, and the sun visibility from `fog_look_visibility` (in-march variant, maps at s4-s5 as the repair binds them, ~90 slots, 4 fetches) or one grid fetch at slice `|q|/500` (grid variant, ~15 slots); x `density_scale` (carries the 90-frame far ramp) x GAIN x `rho'` (as built these scalars reach the PS through the VS, not a c42 row: superseded, see As built) | Everything is the fog's own law and constants, so a mote sits in a cloud body exactly where the march draws one and goes dark inside a shaft; slots [I] 230-260 in-march, 160-190 grid, a fresh 512 budget |
| Placement | after the repair draw, on `f.target` (full resolution, still bound: no `SetRenderTarget`), before `EndScene`/restore; the block restores VS, declaration, streams, indices, VS constants and the three blend states (`D3DSBT_ALL`) | Before the `StretchRect` copy the motes would enter the scratch scene and be extinguished by the whole column (a sky-pixel mote through 22.5 km of fog); the march cannot see them (no RT2). Over 200 m the fog's own transmittance is ~1 [I], so drawing them un-fogged is right |
| Blend | ONE/ONE on the FP16 target; requires `D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING` on A16B16G16R16F, already queried by the HDR pass (`caps_.fp16_blending`; superseded, see As built: FogPass makes its own `CheckDeviceFormat` at the first request and checks `D3DPMISCCAPS_BLENDOP` and `D3DPBLENDCAPS_ONE`); the 8-bit route's X8R8G8B8 blend is baseline | A refused blend capability refuses the mote stage only (`density_status().motes_refused`, one log line), never the fog, the grid's rule |
| Projection | the jittered projection a routed draw uses this frame (engine rows + the route's jitter), not the fog quad's corrected rows | The resolve removes the jitter from every current sample; an un-jittered mote would wobble +/- 0.5 px against the world. The RT2 depth it is tested against is on the same jittered grid |

Displacement at the wrap radius perpendicular to the flight path, 1280x768 (`p11` 1.3333 -> 512 px per unit tan
[I from the shadow-pass note's projection scalars]), 60 FPS; halve the frame rate and the numbers double:

| Speed | units/frame | px/frame at R (1000 u) | at 250 u (50 m) |
| --- | --- | --- | --- |
| 100 m/s | 8.3 | 4.3 | 17 |
| 200 m/s | 16.7 | 8.5 | 34 |
| SETA 10x, 100 m/s | 83 | 43 | 170 (clamped 128) |
| SETA 10x, 200 m/s | 167 | 85 | 340 (clamped 128) |

SETA needs no detection: the camera delta between consecutive fog frames is the displacement, whatever the
engine's time scale; `seta-motion.md`'s 3-38 px/frame hull parallax [M] is the same law at longer range. A
mote near the screen centre moves radially and slowly (parallax ∝ sin of its angle to the path).

## 2. Density and visibility law

Motes exist where the fog exists: `rho'` is the look's remapped density (coverage .35, exponent 2, waves,
warp) at the mote's position, exactly zero below the coverage, so voids show none and a cloud edge is a
mote edge. Readiness follows the march (`lambda` ramps fine in, `density_scale` ramps far in), a sector
whose rule holds the medium at zero (`FOG IDLE`, `density_scale == 0`) takes the transaction's strict
zero-device-call exit, and a refused camera or a filling far level draws no transaction at all: none of
this needs a mote-specific gate. The 13 -> 22.5 km fade does not apply (|q| <= 200 m). Sun visibility comes
free of new machinery in both variants (table above); the cascade at 200 m is the 7500 map in the in-march law
(the 1500 map is bound and unused there, as for the march) and the finest slice of the grid in the grid
variant. Strength (Ctrl+Alt+F10 ladder) scales `density_scale`, so motes follow the fog's strength; GAIN is
the mote-only multiplier.

**Known bias (as built, left for the flight to rate):** the scene target holds the encoded image (exponent 1/2.2
with the AgX decode), and ONE/ONE adds `pow(c, 1/2.2)` to it. That is exact over black, but over a lit pixel s it
over-adds: the result `s + c^(1/2.2)` exceeds the correct `(s^2.2 + c)^(1/2.2)`, increasingly so as s grows, so a
mote in front of a bright hull or a lit cloud reads brighter than its radiance. The blend is unchanged for now;
a decode-add-encode would need the target as a texture or a second pass.

## 3. Motion and TAA

**Unrouted, no mask, no motion written.** The resolve treats a mote pixel as sky content: RT2 keeps the
sentinel (no geometry), RT1 alpha stays the fill sentinel, history comes from the far-plane camera path
(policy 2; translation ignored, so under straight flight the history tap is the pixel itself), clipped to the
3x3 box, relaxed by the sentinel stabiliser (S .7), weighted by the far stabiliser's ramp on the flown
`far_camera` program. Consequences, all [I] until the fixture row below measures them:

- *No exit reset.* The mark is `exiting = parallax >= EXIT_PX ? band : 0` and `band` is non-zero only on an
  unrouted pixel whose 3x3 holds a routed (RT2 geometry) neighbour; the parallax is that neighbour's routed
  correspondence. A mote over sky has `band = 0`; a mote crossing a hull's 1-px band changes neither the
  band nor the hull's parallax. Motes therefore add zero negative ages; the `--taa-debug` age readback counts
  them in flight.
- *Ghost bound* is the sentinel stabiliser's: a trail at most 3 px behind the mote, no brighter than the
  brightest colour within 3 px, decaying 5 %/frame, cut by the box beyond 3 px (temporal-integration.md,
  "What can ghost"). Behind a streak that is a 3 px tail.
- *Dimming of new pixels.* A pixel the mote newly covers has sky history; the clip pulls it up only as far as
  the 3x3 (and, through S, the 7x7) statistics allow. One 1-px point in a 3x3 of sky: mean M/9, sigma .31 M,
  lower bound < 0, history unclamped, output .1 M at weight .9 (.015 M at .985): a sub-pixel mote vanishes.
  A pixel inside a uniform 5 px wide streak: 3x3 all M, lower bound M, output M; its 7x7 (35 of 49 at M)
  gives lower bound .14 M, so with S .7 the leading segment shows at roughly .4-.5 M and the overlapped
  segment (length >= displacement + size, the streak law) at M. Hence SIZE 4 px minimum and the overlap;
  GAIN corrects the residual once the fixture gives the number. Rotation (pans) reprojects sky history
  exactly, so a pan neither ghosts nor dims beyond the same rim effect.
- *TAA off* (`look_resolved` false): motes are drawn identically; only the resolve's averaging is absent.

Cost in the resolve: zero instructions, zero taps; `far_camera` stays at 510/512 [M], which is also why no
mask lane is possible (section 7).

## 4. Option, log row, hotkey

- `--fog-dust-motes N[,SIZE[,STREAK]]` -> `X3M_FOG_DUST_MOTES=N,SIZE,STREAK`; N 0 (explicit off) or 64..8192,
  SIZE 2..16 px (default 4), STREAK 0..512 px (default 128); requires `--volumetric-fog-range stored`
  (parser error otherwise, the `--fog-shadow-pass` rule). **Default off**: absent, the DLL creates nothing,
  polls no key, and the transaction is byte-identical to today.
- Tunables read once with the look tuning, `X3M_FOG_MOTES_<NAME>`: RADIUS (200..5000, 1000), NEAR (5..200,
  25), MAX_PX (SIZE..64, 12), GAIN (0..8, 1), SOFT (0..0.1, 0.02), DRIFT (0..200, 20), SEED (integer, 1);
  logged once as `volumetric_fog_motes_mode`.
- `volumetric_fog_frame` gains, with the option on: `motes=0|1` (drawn), `mote_count=N`, `mote_calls=<device
  calls of the stage>`, `mote_shift_px=<512 |delta| / R: the perpendicular displacement at the wrap radius
  this frame>` (superseded, see As built: H/2 x m11 in place of 512), `mote_streak=0|1` (previous basis valid: 0 on a cut, a Reset, a gap or |delta| > R),
  `mote_shadow=in_march|grid|none`, `mote_refused=<reason|none>`. Same budget rules as the grid fields.
- **Ctrl+Alt+F11** toggles the stage (free since the look cycle was retired on 2026-09-22; the Alt rule on
  F11's own raw latch, so Ctrl+Shift+F11 stays the shadow pass; polled only with the option). One
  `fog_dust_motes_toggle device= frame= enabled=0|1 refused=<none|not_requested> key=ctrl_alt_f11` line per
  press, no notice; the FPS overlay's fog line appends ` MOTES` while drawn. Fixture export
  `x3m_fog_dust_motes_fixture_toggle`.

## 5. Verification

1. **Density shader runner** (`fog_density_shader_run.py`, host twin in `fog_density_shader_reference.py`,
   which already owns the field, the look remap and the projection): `M_motes_sky` (32 seeds, camera in a
   dense region, sky depth): blob centroids within .5 px of the host projection, per-blob energy within 2 %
   of the host radiance, streak length within 1 px of the host for a given delta, and 0 length with the cut
   flag; `M_motes_void` (camera in a void): image hash equal to `A_look_sky`'s; `M_motes_depth3`: every mote
   behind the synthetic plane absent, in front present, occlusion consistent with the RT2 texel of its
   centre; `M_motes_wrap`: camera + W per axis gives the identical image, camera + W/2 does not; shaft
   variants: a mote inside the stripe shadow darker by the `.15` floor law in both variants.
2. **Off path bit-identical**: gate `motes_off_bit_identical`: the five accepted look images, the repair
   image and the grid images hash as in the ledger's tables; the ten existing programs' bytecode unchanged
   (`fog_density_field_inc.h` may gain a function but no drawn program's bytes); `calls` per frame unchanged;
   the pass fixture's hostile-state and reference-count cases extend by the new stage; `device_calls` with
   the option on minus off equals `mote_calls`.
3. **Route bridge** (`fog_route_density_inc.h`, `motes_ab_*`): launch on: frame row carries the fields; toggle
   off: one toggle row, the off frame byte-identical to the bridge's baseline frame, VB/IB/programs kept
   (`allocations()` equal); twenty alternating frames create nothing; Reset while on: VB/IB recreated exactly
   once at the next latch; option absent: no key polled, no allocation, rows absent.
4. **Temporal fixture** (`run_temporal_pass.py`, new edge case (m) "streak over sky"): a 5 px wide, 24 px
   long unrouted streak (sentinel depth, motion alpha -1) advancing 8 px/frame over case (l)'s flickering
   sky, `far_camera` with strict + band 3 + exit .25 and the stabiliser at .7 (the flown configuration).
   Asserted: negative ages 0 on every frame; trail beyond 3 px behind the tail <= .05 M; control sky
   identical. Reported (sizes GAIN, not gated): output/current on the overlapped segment's centre line
   (expected ~1) and on the leading segment (expected .4-.5). Second row: the same over a hull square with
   its band, asserting the hull's own mark count equals the run without the streak.
5. **Slots and provenance**: `fog_density_shader_slots.py` < 512 for both mote programs and the VS,
   generator `--check`, `test_fog_density_shaders`, `test_fog_look_reference`, `test_volumetric_fog`,
   `test_shader_compiler_provenance`, launcher parser tests, `test_comparison_hotkeys` for the Alt latch.
6. **Flight**: one build, motes on, the at-rest frame-time A/B with Ctrl+Alt+F11 (`frame_end` medians split
   by the toggle rows, the Run 256 method) at the run239 pose; a normal-speed leg, a SETA burst and a pan
   with `--taa-debug` (negative-age count per frame, F8 bursts); verdict on density, brightness, streak
   length and whether the near fade shows in first person.

## 6. Cost and native behaviour

Device calls per applied frame [I]: `SetVertexShader`, `SetVertexDeclaration`, `SetStreamSource`,
`SetIndices`, `SetPixelShader`, one `SetVertexShaderConstantF`, three `SetRenderState` (blend enable, src,
dst), `DrawIndexedPrimitive` = **10-11** (superseded, see As built: 12 measured, with `BLENDOP` and `CLIPPING`),
about 5 us at the ledger's .42 us per call, against 338 / 324 per
applied frame measured in runs 251 / 250 [M]. The PS reads the rows already uploaded (one new row c42
(GAIN, SOFT, R, NEAR) rides the existing 42-row upload; superseded, see As built: no c42) and the samplers the repair leaves bound. CPU
otherwise: the modulo, delta and drift time in double, one retained basis copy; no allocation, lock or
per-draw work. GPU [I]: 8192 vertices x ~40 instructions; fill ~120 motes x 4 px x (4 + L) px = ~5 k px at
normal speed, ~30 k px under SETA at 1280x768 (~250-slot pixels, 5-9 fetches), under 1 % of the march's
245,760 x 64 bin evaluations; **estimated well inside the 0.15 ms target**, but bottle X3 refuses timestamp
queries, so only the toggle A/B measures it. Memory: 184 KB VB/IB, one VS, two PS.

Native D3D9: vs_3_0/ps_3_0, a DEFAULT static VB/IB, `DrawIndexedPrimitive`, `tex2Dlod`, additive blending
on A16B16G16R16F behind the documented capability query. No VPOS, no vertex texture fetch, no point sprites,
no `SetStreamSourceFreq`, no MRT, nothing Wine-specific. Unverified natively like the rest of the fog; add
the row to `platform-portability.md`.

## 7. Alternatives considered

- **Routed draw writing RT1 motion and RT2 depth (same-draw motion output).** Exact motion vectors, but D3D9
  blends every bound target with one blend state, so a soft additive sprite would blend its motion and
  depth lanes with the sky's sentinel: the motes would have to be opaque or alpha-tested (hard 3-px
  squares), or two draws (opaque MRT core + blended colour). Worse, RT2 depth makes each mote geometry for
  the resolve: a 1-px band ring around every mote, marks at .25-3 px/frame of parallax, exit resets and
  sparkle rings under every SETA leg, the very thing the exit reset was built to confine to hulls. Loses.
- **Resolve mask lane ("current-only" class in RT2).** Needs a term in the resolve; `far_camera` is at
  510/512 [M] and the age variants at 495-507; and the pixel would still need RT2 written by a blended draw.
  Loses on slots before anything else.
- **Post-resolve draw.** On the HDR route the resolve's output *is* the next frame's history texture
  (ping-pong, no copy-back; temporal-integration.md "Stage 3"), so a post-resolve draw enters history
  anyway; the only clean site is after the AgX write-back, in display space, outside exposure and the HDR
  bloom, with a second state bracket (~30 calls). Kept as the fallback if the temporal row shows the
  leading-segment dimming is unacceptable and GAIN cannot cover it; then in display space with a fixed
  brightness.
- **Point sprites.** No streak, axis-aligned squares, `MaxPointSize` varies by driver (unqualified on this
  translation), a point whose centre leaves the screen is culled whole (edge popping), and
  `D3DRS_POINTSPRITEENABLE`/`POINTSIZE` are two more states the normalize resets. Loses.
- **Hardware instancing** (`SetStreamSourceFreq`, two streams). Saves 136 KB of VB for another state the
  bracket already resets to 1 and a capability question on the translation. Loses.
- **CPU density at the camera** (one amplitude per frame). Needs a C++ twin of the shader's remap, coverage
  waves and warp or diverges from the fog's own edges by up to 2 km; per-mote sampling in the PS uses the
  same include and constants and costs 4 fetches per mote pixel. Loses.
- **Extinguishing motes by the composite's T at the pixel** (one fetch of `lit`). T is the whole column's,
  not 200 m's: motes over a dense far column would vanish. Not adopted; noted as the knob if a flight shows
  motes too bright inside cores.
- **Drawing motes before the march** so the fog fogs them. They would enter the scratch scene and be
  extinguished by the pixel's full column; the march cannot place them (no RT2). Loses.

## 8. Unknown, and what settles it

1. Leading-segment brightness under the flown resolve (`far_camera`, S .7, exit .25): the temporal row (5.4);
   GAIN or a longer overlap (length = displacement + 2 x size) is the correction.
2. Slot counts of the two mote programs: `fog_density_shader_slots.py` at the first compile.
3. Whether ~120 on-screen motes at 4 px read as dust rather than snow, and the near fade in first person:
   the flight; N, SIZE, GAIN, NEAR are the knobs.
4. The mote-stage GPU time: unmeasurable on this bottle; the toggle A/B is the instrument, the branch GPU
   timer would measure it natively.
5. Fog transmittance over 200 m at the flown strength (assumed ~1): a one-line host computation from
   `sigma_eff` and the fine field along the camera's 1000-unit neighbourhood settles whether motes need an
   extinction term of their own.

## As built (2026-09-23)

Sources: `src/renderer/fog_mote_math.h` (option triple, `X3M_FOG_MOTES_<NAME>` table, seed lattice),
`src/fog/fog_dust_motes_vs.hlsl` and `fog_dust_motes_ps.hlsl` with the variants `_look_ps` (in-march
visibility) and `_grid_ps` (`FOG_SHADOW_PASS`), compiled to `src/renderer/fog_dust_motes_{vertex,look,grid}_program_inc.h`;
`FogPass` (`fog_pass.{h,cpp}`: resources in `density_resources`, `mote_constants`, the stage after the repair),
the proxy fragment (`motion_output_fog_inc.h`: toggle, drift clock, frame-row fields, refusal line), `capture.cpp`
(parse, F11 read shared with the shadow pass, overlay, fixture export), `comparison_controls.h`, `tools/manage.py`.

Where it differs from the design above:

- **12 device calls per applied frame** (measured; the estimate was 10-11): `SetVertexShader`,
  `SetVertexDeclaration`, `SetStreamSource`, `SetIndices`, `SetPixelShader`, one `SetVertexShaderConstantF`
  (12 rows), five `SetRenderState` and `DrawIndexedPrimitive`. The two extra states: `BLENDOP` `ADD` (normalize
  never sets the blend operation, so a caller's `MAX` or `REVSUBTRACT` would apply; the pass fixture's hostile state
  now sets `MAX`) and `CLIPPING` `TRUE` (normalize turns clipping off; capsules at the screen edge leave the viewport).
- **No pixel row c42.** GAIN x `density_scale` x far ramp and SOFT reach the pixel program through the vertex
  program's interpolators, so the march's 42-row upload and every call up to the repair are the off path's; on
  minus off is exactly the stage (measured 334 - 322 = 12).
- Vertex constants c0-c11: c0 the jittered projection routed draws use (FogParams' rows minus the quad pixel-centre
  term), c1-c3 this frame's world->view rotation (the double inverse of the fog's view->world rows), c4-c6 the previous
  drawn frame's, c7 camera mod W + streak flag, c8 camera delta + STREAK, c9 drift phases, DRIFT and brightness,
  c10 SIZE, MAX_PX, K, SOFT, c11 the viewport. Both streak ends use this frame's projection (no jitter streak at rest).
- Seeds are 24-bit unit values (lowbias32 of index and SEED) scaled by W in the vertex program; 20 B per vertex,
  VB N x 80 B, IB N x 12 B: 188,416 B at N 2048 (by construction), 753,664 B at 8192. A culled mote puts its four corners
  on one point outside the clip volume instead of w = 0.
- Streak rule: the previous drawn fog frame is `FogFrame::frame - 1`, the caller reports no cut
  (`FogFrame::mote_cut`, the proxy's cut-detector verdict `cut_finished_ && counters_.cut`, so a cockpit/external view
  switch or a roll-only cut inside the geometric bounds draws none), |delta| <= R and the view axis turned under
  30 degrees; anything else (cut, gap, Reset, toggle, a previous endpoint behind the camera) draws zero length.
- **Maintenance risk: the pixel program repeats the march's law inline** (the domain warp, the coverage shift of the
  three oblique waves, the taper smoothstep, the two-lobe phase and the ambient lerp of `march_depth` under `FOG_LOOK`
  in `fog_density_field_inc.h`), calling only the shared `level_sample`, `look_wave`, `look_density`, `geometry`,
  `valid_geometry_depth`, `fog_look_visibility`, `grid_slice` and `grid_tile`. Factoring those lines into shared
  functions would recompile the march and repair and change their accepted bytecode (repair sits at 510 of 512 slots),
  so they are duplicated; a change to the look's law must be made in both places (the pass fixture's twin catches a
  divergence only in the mote cases).
- Drift clock: `QueryPerformanceCounter` seconds since the first mote frame, read only with the option
  (`FogFrame::mote_seconds`); the 8 s phases are reduced on the CPU in double.
- Output `pow(colour, 1/gamma)`, alpha 0 (exact over black with the 2.2 encode; the scene alpha is untouched).
- Refusals (`density_status().motes_refused`, one `fog_dust_motes_refused` line, sticky until detach, the fog draws
  on): `mote_count`, `mote_blend_caps` (`D3DPMISCCAPS_BLENDOP`, `D3DPBLENDCAPS_ONE` source and destination,
  `MaxVertexShaderConst >= 12`), `mote_index_limits`, `mote_fp16_blending` (`RENDERTARGET |
  QUERY_POSTPIXELSHADER_BLENDING` on A16B16G16R16F, queried at the first request, not at attach), `mote_compiled_slots`,
  `mote_program_create`, `mote_buffers`, `mote_draw` (a draw that fails without a lost device; the fog frame stands).
  A lost device inside the stage takes the transaction's loss path (`FogStage::Motes`).
- The host twin is C++ (`verification/probe/fog_dust_motes_cpu.h` over `fog_density_cpu_march.h`'s texel-exact field)
  inside the production FogPass fixture (`fog_density_pass_fixture.cpp`, real VB/IB, blending and state
  restoration), reported by `fog_density_shader_run.py`; not the Python shader reference. The fixture picks its dense
  and void poses with the twin on a 1500-unit lattice of +-18 km around pose A (pose A itself has no fog within 200
  units). `mote_shift_px` uses H/2 x m11 (512 at 1280x768).

Measured on bottle X3 (compact record `verification/results/bottle-X3/fog-dust-motes.json`, written by
`verification/results/fog-dust-motes/summarize.py`; ledger `docs/verification/volumetric-fog.md`, "Dust motes"):

| Item | Result |
| --- | --- |
| Slots (ps look / ps grid / vs) | 311 / 257 / 101; texture instructions 13 / 6 / 0 |
| Device calls, applied frame | off 322, on 334: +12, equal to the stage's own count |
| Pass fixture | 117 checks (the 78 existing unchanged + 39 new), 1,141 state restorations, 30 / 30 gates, accepted look hashes equal, `GRID_REPORT` 20 / 15 and `GRID_TOGGLE` 323 / 338 / 15 as before |
| Off path | toggled-off frame byte-identical to a pass launched without the option, same call count |
| Sky (N 512, R 200, SIZE 4, STREAK 8) | 34 of 142 drawn motes in fog; every pixel within 0.41 of the tolerance (2e-3 + 3 %), worst 9.3e-4; 5 isolated blobs: centroid within 0.029 px of the projection, energy within 1.44 % |
| Streak (12 units right, next frame) | `mote_shift_px` 3.7412 = expected; per pixel 0.37 of tolerance with 8 STREAK-clamped capsules; length of the isolated streaks within 0.036 px; the gap frame draws zero length (0.31) |
| Caller cut (6 units, 2 degree yaw, `mote_cut`) | the same step streaks without the flag; with it zero length, the frame within 0.38 of the still twin's tolerance |
| Void pose | image byte-identical to the launch-off frame |
| Plane at view z 90 | 26 motes behind absent, 4 in front present; per pixel 0.25 |
| Wrap | camera + W on each axis puts every mote on the same pixel (13 blobs within 0.0038 px); + W/2 moves the lattice |
| Shafts | in-march dark map on the 0.15 floor law (0.25 of tolerance, energy 0.267 of the lit frame); grid variant lit 0.41, dark 0.25 |
| Reset | VB/IB released; +4 allocations at the next prepare (targets, two atlases, motes); frames byte-identical |
| Refusal (no `BLENDOP` cap) | `mote_blend_caps`; frame and call count equal to the launch-off frame |
| Route bridge | 36,333 checks; 15 `shadow_ab_*` and 13 `motes_ab_*` names; legacy images bit-identical to baseline `6f16dbf6`; row `motes=1 mote_count=8192 mote_calls=12`; Reset +5 allocations (family atlas, targets, two atlases, motes) |
| Temporal row (m) | negative ages 0 over the flickering sky and the dark sky (M 2 and 0.8); no trail beyond 3 px over the dark sky (both M); a moving hull's marks 648 with and without the streak; `run_temporal_pass.py` 744 / 546 on the rebased tree |

Reported, not gated (temporal row (m), 5 x 24 px streak at 8 px/frame, far_camera + strict + exit 0.25 + stabiliser
0.7): output/current on the centre row is 1.00 (overlapped) and 1.00 (leading) at M 2, above the stabiliser's emitter
bound E 1 (the box shrinks to the inner 3x3); at M 0.8, below it, 0.66 and 0.37: the 7x7 box of a 5-px streak holds the
dark sky, so each pixel climbs over its three covered frames. Over the flickering sky every 3x3 spans [0, 1], so the
clip cannot cut the history of M: the trail reaches 0.69 (34 % of M) and stays above 0.05 M for the whole remainder of
the sequence (the trail spans the screen) as it decays at the 0.97 weight. The 3 px trail gate therefore applies to
the dark sky only; the flickering-sky trail is reported, not gated.

Unknowns: 1 is measured above (a mote below E 1 shows at 0.37-0.66 of its radiance; GAIN about 1.5-2.7, or motes
above E, correct it; a sky whose local range spans the mote's value keeps a decaying trail); 2 is settled; 3-5 need
the flight.
