# FP16 HDR scene path, exposure and AgX

Design record, 2026-09-12. Roadmap stage 4, specified as an increment on the
live motion route, not a new renderer. It assumes the engine scene-end hook at
the `CALL 0x004c4750` callsite `0x004721b1` exists (shape and register contract
in [camera-state-and-frame-routine.md](../reverse-engineering/camera-state-and-frame-routine.md)
§8) and that the temporal resolve has moved there from the game's bloom
`StretchRect`. The motion ABI, history key, camera read and sentinel policy are
unchanged. Default off: with `X3M_HDR=0` no target is created, no call is
redirected, and the presented frame is bit-identical to today.

## 1. Target topology per frame

**Redirection point.** The route already latches the frame's main colour/depth
pair at the selector's first full colour+depth `Clear` (`scene_boundary.h`
step 1, issued by `0x004bb280` right after view activation, i.e. after
`BeginScene 0x004720c8`), creates RT1/RT2 there and advances the jitter. That
latch is also the right redirect point: inside the scene, before the 393
SM1/SM2 background draws, and the `Clear` then clears *our* target so no extra
clear draw is needed.

- At the latching `Clear`, before forwarding: create/reuse an owned
  `D3DFMT_A16B16G16R16F` render-target **texture** at the latched dimensions
  (`D3DPOOL_DEFAULT`, 1 level, no MSAA — every capture is single-sampled) and
  `SetRenderTarget(0, scene_surface)` through a native slot.
- **Depth/stencil stays the game's D24X8, untouched.** D3D9 constrains only
  size and multisample type, not format. RESZ, the depth epochs and the
  scene-depth machinery keep working because depth is never rebound.
- Application `SetRenderTarget(0, main)` during the scene phase is redirected
  the same way; binds of any *other* surface are forwarded verbatim, so the six
  env-map cube faces of `0x0047e820` between `EndScene 0x00472201` and
  `BeginScene 0x0047223d` are never redirected.
- Every consumer that models application state — `GetRenderTarget(0)`, the
  RT/constant shadow, `SceneBoundarySelector`, `describe_surface`, the state
  block path, the ownership wrapper — must keep seeing the **application's**
  logical binding (the latched A8R8G8B8 surface), exactly as the route's own
  calls are already kept out of the selector's event stream. Otherwise the
  selector rejects every frame on format.

**MRT slots** (D3D9 gives four; the scene phase now uses three):

| Slot | Content | Format | B/px |
| --- | --- | --- | ---: |
| RT0 | owned HDR scene | `A16B16G16R16F` | 8 |
| RT1 | motion (prev UV / prev z,w / validity) | `A32B32G32R32F` | 16 |
| RT2 | current device depth `z/w`, −1 sentinel | `R32F` | 4 |
| RT3 | free — normals, stage 7 | — | — |

RT1/RT2 binding, `COLORWRITEENABLE1/2` and restoration are unchanged. The
existing three-format MRT self test (`A8R8G8B8` + `A32B32G32R32F` + `R32F`,
passing on Preview) becomes a four-format one with FP16 in slot 0. Normals later
want RT3 *and* bandwidth, which is why §6 makes the compact motion encoding a
stage-7 prerequisite rather than a stage-4 one.

**Write-back.** At the scene-end hook, before the original `0x004c4750` runs,
one tonemap draw writes the display-encoded 8-bit result into the *game's* main
surface as RT0, RT1–3 unbound, depth off, blending off, `COLORWRITEENABLE=15`,
`SRGBWRITEENABLE=FALSE`. Output alpha is the **scene alpha, carried through
unchanged**: the original highlight program `1c90e79667bdaddf` multiplies
sampled RGB by the sampled *alpha* for colored glow and derives a separate
thresholded white-highlight mask multiplied by `1 − saturate(alpha)`, so
writing alpha 1 would silently change both terms. The exact equations and
observed constants are in [the compositor study](../reverse-engineering/compositor-and-glow.md).
(The resolve hit this exact bug; `temporal-integration.md`, "Findings while
wiring".)

Redirection is a **must-unwind** operation, unlike the resolve. Any failure
after the redirect still writes back, degrading in order: AgX with the last good
exposure → identity tonemap → `StretchRect(scene → main, POINT)` (which needs
the `CheckDeviceFormatConversion` gate the TAA copies already use). Reaching
`EndScene 0x00472574` or `Present` still redirected is a bug, not a degradation:
log it and disable the feature for the device.

**What the game's bloom sees.** *Stage 1*: `0x004c4750` runs unmodified, so its
`StretchRect` copies the **LDR, AgX-tonemapped, display-encoded** image and the
four bloom passes, GUI and text at `0x0047253f` behave exactly as today. Visible
consequence: tone compression changes the thresholded white-highlight term;
the independent alpha-authored colored term remains brightness-weighted and is
not governed by that threshold. This does not prove all glow is weaker than
vanilla. The original Stage 2 skip proposal is superseded by the
[original-once/RGB-replacement boundary](hdr-bloom-boundary.md). Run 26 showed
that its initial RGB-only replacement omitted the native alpha-authored glow;
the [authored-glow correction](bloom-authored-glow.md) records the bounded
replacement policy and qualification limits.

## 2. Shader implications

**Who writes colour in the scene** ([iteration-06.md](../verification/iteration-06.md),
20,076 main-scene draws / 68 frames): `vs_3_0/ps_3_0` 18,605 (92.7%) — the
169-row transformable population; `vs_1_1`/*null* 915 (`z_only.fb` depth
prepass, **no colour**); `vs_2_0/ps_2_0` 306 and `vs_1_1/ps_1_1` 250, all
transparent or additive (`effects`, `engine`, `adeffects`, `particles`,
`stardust`). Every sub-SM3 colour writer fails gate 4 (blend on or Z-write off),
which is why it carries the sentinel today; it will write ordinary 0..1 values
into the FP16 target through ordinary blending. Acceptable in stage 1. The
background phase (393 draws, all SM1/SM2) is in the FP16 target; the 948
post-bloom overlay draws are not (they run after the write-back).

**What an SM3 material pixel output means.**
[material-radiance.md](../reverse-engineering/material-radiance.md): the SM3
material VS accumulates the point-light loop RGB plus emissive into `COLOR0`,
and the PS clamps that interpolated value with `mov_sat_pp rN.xyz, v0` before
material mixing. There is **no saturate on `oC0`** — the clamp is on a temporary
(destination tokens `80370005`/`80370004`/`80370001`: temp register, `.xyz`
mask, `_SAT|_PP`), and nothing re-clamps the combined RGB afterwards. Alpha is
untouched by all eight sites.
[vertex-color-hdr.md](../verification/vertex-color-hdr.md) proves on this
backend that `vs_3_0 → ps_3_0 COLOR0` carries >1 through interpolation into FP16
while `vs_2_0 → ps_2_0 COLOR0` clips per vertex *before* interpolation: the SM3
population is the HDR population, SM2 needs a varying change (stage 6).

**Transformer extension.** The clamp removal already exists in production with
the same shape as the motion fragment: `src/renderer/material_radiance.{h,cpp}`
+ `material_radiance_profiles_inc.h`, five reviewed profiles, replacing the
three-DWORD `02000001 <dst> 90e40000` span with the four-DWORD
`0300000b <dst & ~0x00100000> 90e40000 <encoded local zero>`, i.e.
`mov_sat_pp t.xyz, v0` → `max_pp t.xyz, v0, <shader-local DEF zero>`; lower
bound and partial precision survive. GPU-verified (42 structural checks, 192
numeric samples across a Reset: 4 → 4.0, 16 → 16.0).

Stage 4 adds **coverage**, generated like the 169-row motion table: extend
`tools/analysis/inspect_material_radiance.py` into an archive-wide sweep over
the 108 pixel programs the motion table names, plus the candidates
`shader-family-review.md` flags by hand (glass `a66fb1981ba755b2`, asteroid,
Boron, Paranid, Split, Terran, the shared Khaak/Teladi/Xenon program, damage,
terraformer); emit a generated header with the pinned-digest discipline of
`generate_shader_profiles.py`; keep the registry's rejection rules (unknown
fingerprint, wrong word count, site inside a COMMENT/DEF payload, non-zero
literal, wrong write mask → reject, original kept). Two rules the sweep must
enforce: only the **interpolated-`COLOR0` RGB** clamp qualifies (the 36 other
saturations in the five reviewed programs, and the bloom threshold saturations,
are semantics, not range clamps); and a profile is a *shader* fact, not a *pass*
fact — application stays gated by the route's per-draw scene gates, because
program sharing is real (`0a523f33ac47ae05` is `gui2d` and `stardust`;
`6109cf64c03529dd` is `gui2d` and `nebula`). Switch `X3M_HDR_RADIANCE=1`,
independent of `X3M_HDR`, so the topology lands and proves bit-exact first.

**Blending in FP16.** Single-RT blending into `A16B16G16R16F` needs
`CheckDeviceFormat(..., D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING, ...)`; WineD3D
reports FP16 RT/filter/blend support ([platform.md](platform.md)). MRT blending
needs `D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING`, but only matters if RT1/RT2 are
ever bound across *blended* draws — gate 4 excludes them, so in `perdraw` mode a
blended draw has one target bound and only the single-RT cap applies. The
assessment's phase-level MRT binding would make the MRT cap load-bearing; gate
it there and fall back to `perdraw` without it. Additive (`ONE`/`ONE`) stacks
now accumulate past 1 instead of saturating — the point, but also a behaviour
change for *unmodified* shaders. `X3M_HDR_CLAMP=<float>` (default off) clamps
the tonemap input as a blunt firefly guard.

**sRGB — answered from capture evidence.** Queried over
`verification/results/game-turning-capture-summary.json`: `D3DRS_SRGBWRITEENABLE`
(state 194) = 0 on **762 of 762** draws; `D3DSAMP_SRGBTEXTURE` (sampler state
11) = 0 on **12,192 of 12,192** stage observations. The engine never asks for a
hardware sRGB decode or encode: textures are sampled as stored gamma-encoded
code values, lighting runs on those, the result is written raw to A8R8G8B8 and
presented. **X3 is a gamma-space renderer.** Both fields are already recorded by
`capture.cpp` (`state id=194`, `sampler stage=N state=11`), so no new capture
work is needed.

Working space, therefore: the FP16 target holds **engine space** — the game's
own values, unclamped and unquantized. The display transform is

```
linear  = decode(engine)          // X3M_HDR_DECODE: gamma2.2 (default) | srgb | none
graded  = AgX(exposure * linear)  // AgX consumes Rec.709 scene-linear
oC0.rgb = graded                  // already display-encoded, see §3
oC0.a   = engine.a
```

with `decode(x) = pow(max(x,0), 2.2)`, extended above 1 by the same power so it
stays monotone. Documented caveat, required in the shader header and the first
run report: **the game's blending happened in gamma space**, so decoding at the
tonemap input is not physically exact (a blend of two encoded values is not the
encoding of the blend). The exact fix — sampler `SRGBTEXTURE=TRUE` plus
linear-space material shaders — is stage 6. `X3M_HDR_DECODE=none` exists so the
A/B is measured, not argued.

## 3. Tonemap and exposure

**AgX.** Input Rec.709 scene-linear. The constants below are the "minimal AgX"
form (Troy Sobotka's AgX as reduced in Benjamin Wrensch's *Minimal AgX
Implementation*, iolite-engine.com, MIT). Blender 4.x and Godot 4.3 ship the
fuller Rec.2020-inset AgX with their own sigmoid fits and LUTs, not these
numbers; the minimal form is chosen because it fits `ps_3_0` without a LUT.
Wrensch writes the matrices as GLSL column-major `mat3` constructors; they are
printed here row-major acting on a column vector, and the reference
implementation below carries them at full published precision (15 digits).
They are reproduced from knowledge; the generator that emits the shader header
must pin them against the cited source, as
`tools/shaders/generate_rigid_motion_pixel.py` pins the motion and depth
fragments.

Input (inset) matrix, row-major, acting on a column vector; and its inverse
(outset). Both are row-stochastic to 1e-4, so white maps to white.

```
M_in                                            M_out
 0.842479062  0.078433600  0.079223745           1.196879005 -0.098020881 -0.099029744
 0.042328242  0.878468636  0.079166127          -0.052896852  1.151903130 -0.098961177
 0.042375655  0.078433600  0.879142974          -0.052971636 -0.098043450  1.151073673

min_ev = -12.47393 ;  max_ev = 4.026069

agx(v):   v = M_in * v
          v = clamp(log2(max(v, 1e-10)), min_ev, max_ev)
          v = (v - min_ev) / (max_ev - min_ev)     // 0..1 log encoding
          v = contrast(v)

contrast(x):  x2 = x*x ; x4 = x2*x2                // 6th-order sigmoid fit
          = 15.5*x4*x2 - 40.14*x4*x + 31.96*x4 - 6.868*x2*x
            + 0.4298*x2 + 0.1191*x - 0.00232

look(v):  luma = dot(v, (0.2126, 0.7152, 0.0722))  // default identity:
          v = pow(v*slope + offset, power)         //   offset 0 slope 1 power 1 sat 1
          v = luma + sat*(v - luma)

output(v): return saturate(M_out * v)              // ALREADY display-encoded;
                                                   // do NOT pow(v, 2.2) here
```

That last line is the standard integration bug and matters here: the target is a
plain A8R8G8B8 surface with `SRGBWRITEENABLE=FALSE`, presented as-is, so AgX's
output transform already produces the value to store. Looks are exposed as
`X3M_HDR_LOOK=none|golden|punchy` with Wrensch's published triples — `golden`:
slope (1.0, 0.9, 0.5), power 0.8, saturation 0.8; `punchy`: slope 1, power
1.35, saturation 1.4; `none`: slope 1, offset 0, power 1, saturation 1 — default
`none`; a black space background is the worst case for a punchy look. The CDL
base is clamped at zero before `pow` (`contrast(0) = -0.00232` would otherwise
put a negative under a fractional power), so `none` is the identity only above
zero; the outset plus `saturate` gives the same black either way.
One `ps_3_0` fragment embedded like `temporal_resolve_program_inc.h`; `log2`,
`exp2` and `pow` are single-slot SM3 instructions, well under 512 slots.
Matrices and coefficients live in constant registers so host reference and
shader consume identical numbers.

Reference values with the constants above, look `none`, EV 0: neutral 0.18 →
**0.4967** display (0.214 linear under a 2.2 display, i.e. mid-grey lands on
mid-grey), 1.0 → **0.7867**, 16 → 0.9978, 2^4.026 and above → 0.9986 (the
sigmoid's `contrast(1)`, one code below full white before `saturate`), 0.001 →
0.0156. The neutral axis spreads ≤ 1.5e-4 between channels (the matrices are
row-stochastic to 1.4e-4), under a quarter of an 8-bit code. The full ramp is
`verification/results/agx-ramp.json`.

**Reference implementation (stage 2 preparation, 2026-09-12).** Nothing in
this paragraph is compiled, embedded or wired into the renderer; the stage-1
build is untouched.

- `tools/analysis/agx_reference.py` — pure-Python double-precision oracle:
  `decode` (gamma2.2 / srgb / none, §2), `log_encode`/`log_decode`, `contrast`,
  `look`, `agx(rgb_linear, exposure_ev, look)`, `tonemap_engine(...)` (the full
  fragment including the `X3M_HDR_CLAMP` guard), and a CLI that prints the
  0.001…64 ramp and writes `verification/results/agx-ramp.json`. Every
  constant's provenance is in its docstring.
- `tools/analysis/exposure_reference.py` — the exposure model as pure functions
  with the names the C++ port will use: `meter_level0`, `reduce_mean`,
  `reduce_chain` (4× per axis, edge-clamped taps), `meter_image`, `ev_target`,
  `clamp_dt`, `adapt_rate`, `adapt`, `exposure_multiplier`, `resolve_ev`
  (auto/manual), `simulate`, `taa_k`, `luma_weight`, `weight_color`,
  `unweight_color`. Defaults: key 0.18, EV offset 0, τ_up 0.4 s, τ_down 1.2 s,
  `dt` clamp [1/240, 1/5] s, meter floor 1e-4, `Lmeter_clip` 64, and
  **`EV_min`/`EV_max` = −8/+8** (not numbered in the original text; a 256×
  range either way, well beyond the 1e-4…64 luminance span the meter clamps
  to). `exp2(-dt/(τ·ln2))` is `exp(-dt/τ)`, so τ is an ordinary first-order
  time constant (63.2 % after τ, 99.3 % after 5τ: a 3 EV sector change settles
  to 0.02 EV in 2 s at τ_up).
- `src/temporal/agx.hlsl` — the `ps_3_0` fragment (HLSL source only): sample
  FP16 scene, decode by mode constant, clamp, exposure multiply, inset as three
  `dp3`, log encode, polynomial, look, outset, `saturate`, alpha carried. No
  loops or dynamic branches; the decode mode is a constant-driven `lerp`
  select.
- `src/temporal/agx.h` — the constant layout as C++ (`AgxConstants`, 14
  registers **c8…c21**, deliberately clear of the resolve's c0…c7 so both
  programs can share one constant file), `set_look`, `set_decode`, `prepare`.
  `X3M_HDR_CLAMP` unset uploads 65504 so the shader always applies `min`.
- Tests: `verification/analysis/test_agx_reference.py` (monotone, neutral,
  mid-grey/white, clamps, log round trip, looks, decode modes, matrix inverse,
  ramp determinism and CLI, and a parse of `agx.h`/`agx.hlsl` that fails if any
  constant or register drifts from the reference) and
  `test_exposure_reference.py` (time constants, up/down asymmetry, monotone
  convergence, `dt` subdivision invariance to 1e-6, `dt` clamp across a 3 s
  hitch, EV clamps, 2 s settle, manual override, meter clamps, sun-disc bound,
  chain-vs-mean). The compiled-fragment-vs-reference ramp comparison of §7
  (≤ 1/512 code) is still owed to stage 2 proper, along with the generator,
  `src/renderer/exposure.h` and the reduction/adaptation draws.

**Exposure** is metered on the **resolved** HDR image (after TAA): that is what
is displayed, and TAA has already removed the per-frame sampling noise, so the
meter does not chase the jitter sequence. The tonemap of frame *n* consumes the
EV adapted at frame *n−1*, which removes the serial dependency between the
reduction chain and the tonemap draw at the cost of a lag that is invisible
against 0.4–1.2 s time constants.

Method: mip-style log-luminance reduction, **not** a histogram. D3D9 SM3 has no
compute and no scatter; a histogram would need CPU readback (a banned stall) or
vertex-texture-fetch point scatter. Chain, 4× per axis per level — at 1280×768
six draws (1280→320→80→20→5→2→1):

```
level 0:      L = dot(decode(scene.rgb), (0.2126, 0.7152, 0.0722))
            out = log2(clamp(L, 1e-4, Lmeter_clip))   // Lmeter_clip default 64
levels 1..n:  out = mean of the 16 taps        -> avgLogL in a 1x1 R32F
```

`Lmeter_clip` is the cheap substitute for a percentile: it stops a sun or a
weapon flash from dragging the geometric mean. A centre-weighted mask
(`X3M_HDR_METER_CENTER`) is a one-line extension of level 0.

```
EV_target   = clamp(log2(K) - avgLogL + EV_offset, EV_min, EV_max)  // K = 0.18
tau         = (EV_target < EV_adapted) ? tau_down : tau_up          // 1.2 s / 0.4 s
EV_adapted += (EV_target - EV_adapted) * (1 - exp2(-dt/(tau*ln2)))
exposure    = exp2(EV_adapted)
```

`dt` is QPC between consecutive boundary hits, clamped to [1/240 s, 1/5 s] so a
hitch or a loading pause cannot step the exposure. The adaptation state is an
`R32F` 1×1 ping-pong written by a 1×1 draw — nothing is read back per frame; a
4-byte lagged readback every `X3M_HDR_LOG` frames (default 300) feeds the
diagnostic line only. Switches: `X3M_HDR_EXPOSURE=auto|manual`, `X3M_HDR_EV`
(forces `EV_adapted` and skips the chain — deterministic, what the fixtures
use), `X3M_HDR_EV_OFFSET`, `X3M_HDR_KEY`, `X3M_HDR_EV_MIN/MAX`,
`X3M_HDR_ADAPT_UP/DOWN`. The adaptation step is a pure function of
`(EV_adapted, EV_target, dt, taus, clamps)` and belongs in
`src/renderer/exposure.h` as host-testable arithmetic mirrored by the shader,
exactly as `camera_reprojection.h` mirrors the resolve.

**TAA on HDR or LDR — recommendation: HDR, pre-tonemap, with Karis-style
reversible tonemap weighting inside the resolve.** Reasons specific to this
engine:

1. The pass is already FP16 end to end — history `A16B16G16R16F` ×2 plus `R32F`
   ×2, `FrameInputs::color` documented as a "scene-linear FP16 texture",
   `rejection[2] = 65000.f` already an HDR limit, and `temporal_pass.cpp:233`
   already taking the texture path with an `A16B16G16R16F` format check. Moving
   to HDR needs **no format change**, only handing `in.color` instead of
   `in.color_surface`.
2. It deletes the 8-bit→FP16 input `StretchRect` and its
   `CheckDeviceFormatConversion` gate — the single biggest unverified
   native-Windows dependency in the current resolve
   ([platform-portability.md](platform-portability.md)). `ensure_scratch` and
   the `ticks_copy_color` phase disappear.
3. TAA after AgX bakes the exposure of the moment into the history; adaptation
   then makes the whole history wrong by a multiplicative factor the
   neighbourhood clip cannot distinguish from motion. Keeping history in
   absolute engine-space radiance removes the problem: exposure is applied after
   the blend.
4. Stage 2 needs an HDR image *after* TAA to build bloom from; TAA on LDR would
   leave the only HDR image before the resolve.

The real cost is that variance clipping is scale-dependent — 1.25σ around a mean
of 40 is not the same tolerance as around 0.4 — and bright sub-pixel features
become fireflies the clip cannot suppress. Fix, preserving the tuning
[iteration-08.md](../verification/iteration-08.md) established: in
`src/temporal/resolve.hlsl` weight every current tap and the history tap by
`w = 1/(1 + k·luma)` before the 3×3 mean/σ and before the blend, and invert with
`c/(1 − k·luma)` after. `k` is uploaded as the adapted `exposure`, so the
weighting tracks scene brightness while the **stored history stays absolute** —
no rescaling on exposure change. Everything else in the resolve is unchanged
(mean ± 1.25σ within the min/max box, weight 0.9, closest-depth dilation,
one-sided depth disocclusion test, Catmull-Rom history, sentinel policy 2 and
the far-plane `clip_to_previous`). With `k = 0` the weighting is the identity,
which is also the migration test.

## 4. Pass order at the scene-end hook

Inside the hook at `0x004721b1`, between `BeginScene 0x004720c8` and
`EndScene 0x00472574`, so draws are legal; ESI/EDI/EBX/EBP preserved per the
callsite contract; all device calls through native slots so neither shadow nor
selector observes them.

```
 0. save state (the pass's cached D3DSBT_ALL block), unbind RT1/RT2, depth off
 1. [stage 7+] GTAO / SSR read RT2 (+ RESZ depth), modulate the FP16 scene
 2. TAA resolve   in.color = scene FP16 texture (no scratch copy)
                  in.current_depth = RT2, in.motion = RT1
                  clip_to_previous / sentinel_camera unchanged
                  -> Output::color, an FP16 history texture
 3. [stage 2] HDR bloom chain over Output::color into an FP16 scratch
 4. AgX tonemap draw: source = (3) or (2); exposure = EV_adapted from frame n-1
                  target = the GAME's main A8R8G8B8 surface as RT0
                  oC0.rgb = AgX(exposure * decode(scene)) ; oC0.a = scene.a
 5. exposure reduction chain over the same FP16 source -> 1x1 R32F adaptation
 6. restore the state block; the redirect is off for the rest of the frame
 7. return -> compositor 0x004c4750 (stage 1) or skipped (stage 2)
          -> game bloom / GUI / text 0x0047253f -> EndScene -> Present
```

**History format change: none.** `A16B16G16R16F` ×2 and `R32F` ×2 already. What
changes is content (unbounded instead of 8-bit-quantized `v/255`), the
disappearance of the input scratch copy, and the `k·luma` constant. The
sentinel/camera reprojection path, the far-plane `clip_to_previous` builder,
`camera_sentinel_policy`, the cut detector and `X3M_TAA_SENTINEL` are untouched.

## 5. Platform

**CrossOver Preview / WineD3D** (the path the game actually takes; DXMT is the
D3D11 implementation and belongs to the stage-5 presentation decision). FP16
render targets, filtering and blending are reported supported and FP16
allocation succeeds ([platform.md](platform.md)). The three-format MRT self test
(`A8R8G8B8` + `A32B32G32R32F` + `R32F`, `D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`)
passes on this backend (`color_errors=0 motion_errors=0 depth_errors=0
targets=3`, [motion-output.md](../verification/motion-output.md)). FP16 point
copies of the 8-bit main target already run every frame on the non-HDR TAA
route — `TemporalPass::run` does `StretchRect(color_surface → scratch,
D3DTEXF_POINT)` and the route copies back with `StretchRect(out.color_surface
→ main, POINT)` where the attach-time round trip proves the conversion
(`taa_copy=stretch`; otherwise same-format staging copy plus identity draws,
`taa_copy=draw`, D1 of the native-Windows audit); all 768 RGB codes land
within one FP16 ulp of `v/255` with no gamma curve
([temporal-resolve.md](../verification/temporal-resolve.md)). The HDR path
removes the first copy and turns the second into a draw. Every quad of this
pass (write-back, tonemap, meter chain, self tests) binds the embedded vs_3_0
pass-through and its declaration (`quad_vertex_program.h`), so no ps_3_0
program is drawn through the fixed-function vertex path (D2); the emergency
`StretchRect` rung stays gated by `CheckDeviceFormatConversion` plus the
self test's live 4×4 copy, which demotes the rung on failure. Backend quirk to carry
forward: a full `Clear` is deferred and its colour is encoded with the
sRGB-write state of the first following draw (found by the coverage oracle);
with an FP16 target the encoding question disappears, but the deferral means a
fixture must not assume residency immediately after `Clear`.

**Native Windows caps to check at runtime**, through the factory the proxy
already holds, cached at attach: `CheckDeviceFormat(..., D3DUSAGE_RENDERTARGET,
D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F)`; the same with
`D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING` (blending) and `D3DUSAGE_QUERY_FILTER`
(bloom upsample only); `caps.NumSimultaneousRTs >= 3` and
`D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`; `D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING`
only for phase-level MRT binding; `CheckDeviceFormatConversion(A16B16G16R16F →
A8R8G8B8/X8R8G8B8)` for the emergency unwind; plus the live four-format 4×4 self
test.

**Fail-closed.** Any failed cap or self test disables `X3M_HDR` for the device
with one `hdr_device` line giving the reason, and the frame proceeds exactly as
with `X3M_HDR=0`. `X3M_HDR_RADIANCE` without `X3M_HDR` is refused (unclamped
radiance into an 8-bit target is just clipping). Reset releases the scene target
with RT1/RT2 before the native call and recreates it lazily at the next latch; a
dimension change invalidates exposure state and TAA history. On native Windows
all of this compiles and is gated, and none of it is verified.

## 6. Cost model

Pixels: 1280×768 = 983,040; 5120×1440 = 7,372,800 (7.5×). Default-pool memory:

| Resource | B/px | 1280×768 | 5120×1440 |
| --- | ---: | ---: | ---: |
| motion RT1 `A32B32G32R32F` | 16 | 15.7 MB | 118 MB |
| depth RT2 `R32F` | 4 | 3.9 MB | 29.5 MB |
| **scene RT0 `A16B16G16R16F`** (new) | 8 | **7.9 MB** | **59 MB** |
| TAA colour history ×2 | 16 | 15.7 MB | 118 MB |
| TAA depth history ×2 | 8 | 7.9 MB | 59 MB |
| FP16 input scratch (**removed**) | 8 | −7.9 MB | −59 MB |
| stage-2 bloom chain (≈⅓ of RT0) | ~2.7 | +2.6 MB | +20 MB |
| **total, stage 1 / stage 2** | | 43.2 / 45.8 MB | **324 / 344 MB** |

Compact motion is the obvious lever: a *velocity delta* plus validity in
`A16B16G16R16F` instead of an absolute previous UV in `A32B32G32R32F` halves RT1
to 59 MB and halves its bandwidth. FP16 is adequate for a delta (±512 px resolves
to 0.5 px at the extreme, 0.03 px at 32 px of motion) but **not** for an absolute
UV (2⁻¹¹ relative ≈ 2.5 px at 5120 width), so the encoding change is mandatory
rather than cosmetic and it invalidates the resolve's RG convention and the
readback analyzer. **Not a prerequisite for stage 4** — 324 MB of default-pool
textures is tolerable in this 32-bit process. Make it a prerequisite for stage 7
(normals want RT3 plus another 29–59 MB) and re-evaluate if the first
5120×1440 HDR run shows allocation pressure.

Boundary work added per frame (scene draws also write 8 B/px instead of 4 into
RT0: +12 MB/frame at 1280×768 and +88 MB at 5120×1440 at ~3× overdraw):

| Pass | 1280×768 | 5120×1440 |
| --- | ---: | ---: |
| removed 8-bit→FP16 scratch copy | −11.8 MB | −88 MB |
| AgX tonemap (read 8, write 4) | 11.8 MB | 88 MB |
| exposure chain (≈1.33× level-0 read) | 10.5 MB | 78 MB |
| stage-2 bloom chain | 25 MB | 189 MB |

Against the measured boundary of **0.738 / 2.243 ms** (resolve on,
CPU-inclusive fixture bench, `motion-output.md` camera-reprojection rerun; the
resolve alone is 0.387 / 1.756 ms), stage 1 should add roughly **+0.2…0.4 ms at
1280×768 and +0.8…1.6 ms at 5120×1440** — a boundary of about 0.9–1.2 and
3.0–3.9 ms — partly offset by the removed input copy; stage 2's bloom roughly
doubles the increment at 5120×1440. These are estimates; the fixture's
`bench WxH` mode already produces exactly this number and must be re-run per
step. Nothing is added per draw except one `SetRenderTarget(0)` per frame.

## 7. Verification without the game

**Fixture cases** — extend the motion-output fixture (it already has the
hostile-state harness, coverage oracle, seam export, reference `TemporalPass` on
a second device, Reset, and the ownership-wrapper environments):

1. **Redirect + write-back identity.** `X3M_HDR=1`, `X3M_HDR_RADIANCE=0`,
   `X3M_HDR_EXPOSURE=manual`, `X3M_HDR_EV=0`, `X3M_HDR_DECODE=none`,
   `X3M_HDR_TONEMAP=identity`: the presented 8-bit main target must be
   **bit-identical** to the `X3M_HDR=0` run in every frame, alpha included,
   plain and through the wrapper. This single comparison proves redirection, the
   `Clear` retarget, state restoration, write-back and the alpha carry.
2. **HDR through blending.** `ONE`/`ONE` additive of a shader emitting 4.0 and
   16.0 → 20.0 in the FP16 target, not 1.0; repeat with
   `SRCALPHA`/`INVSRCALPHA` and after a Reset. Exercises the FP16 blend cap for
   real, not by HRESULT.
3. **Four-format MRT** on DXMT/Preview: `A16B16G16R16F` + `A32B32G32R32F` +
   `R32F` self test and a routed draw writing all three, against the CPU oracle;
   plus the cap forced absent (existing fault injection) to prove the feature
   disables itself rather than binding a partial set.
4. **Must-unwind.** Inject a failure at each step after the redirect and require
   a non-black, written-back main target every time, with exactly one log line.
5. **Radiance variants** in the live route against a redirected RT0: 4 and 16
   reach the FP16 target where the original clamped to 1, alpha unchanged, an
   unprofiled program refused.
6. **TAA on HDR.** The seam's byte-for-byte comparison against the reference
   `TemporalPass` repeated with `in.color` (FP16 texture): `k = 0` must
   reproduce the current LDR results exactly; `k > 0` gets new expectations.

**Host references.** `tools/analysis/agx_reference.py` plus a pytest: matrices,
log encoding, polynomial, look and output transform in double precision,
compared against the compiled `ps_3_0` fragment over a ramp (per channel
2⁻¹⁴…2⁶, the neutral axis and the primaries, ≥ 4,096 samples) through the
existing render-a-ramp-and-read-back pattern; tolerance 1/512 of an 8-bit code,
which bounds both the FP16 intermediate and SM3 `log2`/`exp2` precision. The
reference also emits the constant header the shader uses, so the two cannot
drift. Exposure unit tests over `src/renderer/exposure.h`: monotone convergence;
`tau_up`/`tau_down` asymmetry; invariance under `dt` subdivision (one 32 ms step
equals two 16 ms steps to 1e-6); `EV_min`/`EV_max` clamping; `dt` clamping
across a simulated 3 s hitch; manual override bypassing the chain; and the
reduction chain checked against a NumPy geometric mean on a synthetic image with
a saturated sun disc, proving `Lmeter_clip` bounds its influence.

**First gameplay run — capture fields.** A bounded `hdr_frame` line (capture
frames and every `X3M_HDR_LOG`): `hdr`, `hdr_redirected`, `hdr_written_back`,
`hdr_unwind`, `hdr_result`, `hdr_target_create`, `ev_target`, `ev_adapted`,
`avg_log_l`, `meter_clipped_fraction`, `scene_max_rgb`, `scene_p99_rgb` (from a
coarse reduction level, not a full readback), `radiance_variants`,
`radiance_draws`, `decode_mode`, `look`, `tonemap_ms`, `exposure_ms`,
`boundary_ms`. Acceptance: (a) `hdr_redirected == hdr_written_back` in 100% of
frames with `hdr_unwind = 0`; (b) with `X3M_HDR_RADIANCE=0`, fixed EV and an
identity tonemap, frames bit-identical to an `X3M_HDR=0` run of the same scene —
the criterion-6 colour-identity gate iteration 6 left unmet; (c)
`scene_max_rgb > 1` in at least one flight frame with `X3M_HDR_RADIANCE=1`, the
first actual evidence that the game has HDR content to preserve; (d)
`ev_adapted` settles within 2 s of a sector change without oscillating; (e)
boundary cost within the §6 estimate; (f) existing TAA acceptance unchanged.

## 8. Work breakdown

| # | Step | Switch | Files | Fixture | Review gate |
| --- | --- | --- | --- | --- | --- |
| 1 | **Topology**: own FP16 target, redirect at the latching `Clear`, logical-binding shim, write-back with identity tonemap, must-unwind ladder, four-format caps + self test | `X3M_HDR` | `src/proxy/motion_output.{h,cpp}`, `capture.cpp`, new `src/renderer/hdr_pass.{h,cpp}` | cases 1, 3, 4 | bit-identical presented frames, plain and wrapped; zero final device references; Reset/dimension change clean |
| 2 | **AgX + exposure**: tonemap fragment and generator, decode modes, reduction chain, 1×1 adaptation, `exposure.h` | `X3M_HDR_TONEMAP`, `X3M_HDR_EXPOSURE`, `X3M_HDR_EV*`, `X3M_HDR_DECODE`, `X3M_HDR_LOOK` | `src/temporal/agx.hlsl`, `src/renderer/{hdr_pass,exposure}.*`, `tools/shaders/`, `tools/analysis/agx_reference.py` | AgX ramp, exposure unit tests, case 4 | ≤ 1/512 shader-vs-reference; exposure tests green; manual EV deterministic; bench recorded |
| 3 | **TAA on HDR**: `in.color` texture path, drop the input scratch and its conversion gate, `k·luma` weighting, regenerate bytecode | `X3M_TAA` (unchanged) | `src/renderer/temporal_pass.{h,cpp}`, `src/temporal/resolve.hlsl`, `motion_output.cpp` | case 6, full temporal-resolve suite | `k = 0` reproduces every current expectation byte for byte; negative controls intact; seam reference re-run |
| 4 | **HDR radiance**: archive-wide clamp-site sweep, generated table, live application behind the route's gates | `X3M_HDR_RADIANCE` | `tools/analysis/inspect_material_radiance.py`, `src/renderer/material_radiance*`, `motion_output.cpp` | case 5 + the material-radiance GPU fixture over the new table | reversing each patch reproduces original bytes; mutated/unknown programs reject; no non-`COLOR0` saturation touched |
| 5 | **HDR bloom** replacing `0x004c4750`, gated on the glow option | `X3M_HDR_BLOOM` | `src/renderer/hdr_pass.*`, the scene-end hook | new bloom case + energy check | glow-off reproduces stage-1 output; skipping the original leaves no state residue; boundary cost measured |

Risks, ranked:

1. **The logical-binding shim.** Every consumer that queries RT0 must see the
   application's surface while the device holds ours — selector, RT/constant
   shadow, `describe_surface`, state blocks, `GetRenderTarget(Data)`, the
   ownership wrapper's accounting. Miss one and either the selector kills every
   frame or the wrapper's references drift. Case 1 catches the first, the
   wrapper environments the second.
2. **Must-unwind.** A frame that redirects and fails to write back is a black
   screen. This is the first feature whose failure mode is worse than "no
   feature"; case 4 must be in every step's gate, not only step 1.
3. **Blending semantics change even with unmodified shaders**: additive stacks
   that used to clip at white now accumulate, so the look differs from vanilla
   before a shader byte is touched. Mitigations: `X3M_HDR_CLAMP`, the
   `X3M_HDR_DECODE=none` A/B, and saying so in the run report.
4. **Gamma-space blending under a linear display transform** — documented
   caveat, not fixable at this stage; the reason mid-tones may look flatter than
   expected.
5. **Native Windows**: FP16 RT + FP16 blending + four-format independent-bit-depth
   MRT is a stack no native driver has been tested against here. Gated,
   fail-closed, unverified.
6. **5120×1440 memory**: 324 MB of default-pool textures before bloom, in a
   32-bit process. The compact motion encoding is the prepared lever.

## 9. Reverse-engineering gap

**Closed 2026-09-12** by
[compositor-and-glow.md](../reverse-engineering/compositor-and-glow.md): the
glow option is `VideoD3DFlags2` bit `0x80` (`*(*0x00606f34 + 0x100) & 0x80`,
tested at `0x004c4770` together with the device flag at `0x004c478a`); with it
clear the compositor issues no device call at all. With it set the compositor
does `GetRenderTarget(0)` (`0x004c4817`) and later
`StretchRect(saved_rt → sceneMap, LINEAR)` at `0x004c4c8c`; the HUD, the 2D
overlays and the text draw after it into RT0 (the only two `SetRenderTarget`
sites in the executable are the compositor's per-pass target selection and
the environment-map restore); no normal frame reads RT0 back. Consequences
for the redirect are in that document's section 7 and implemented below
(stage 1): the redirect is unwound before the compositor's `GetRenderTarget`,
and `GetRenderTarget(0)` answers with the game's own surface while redirected.
The paragraphs that follow are the original request, kept for the record.

One function, one pass: **`0x004c4750`** (original bloom/compositor, called from
`0x004721b1`). [ghidra-render-map.md](../reverse-engineering/ghidra-render-map.md)
records only that it "requires several non-null globals in the range
`0x00608a64` through `0x00608a74` and rendering option checks before running".
The option word is known from elsewhere —
[constant-uploads.md](../reverse-engineering/constant-uploads.md) shows the
per-view clear `0x004bb280` testing view flags "against render options at
`*0x00606f34 + 0xfc`" — but **which bit the compositor tests, and whether the
effect parameter `g_EnableGlow` is driven by the same bit, is not established**.
Stage 5 (skip the game's bloom, substitute an HDR one, only when the player has
glow enabled) cannot be gated correctly without it.

Requested: decompile `0x004c4750`'s prologue and the tests guarding its first
effect pass; identify the exact option load (offset within `*0x00606f34`, bit
mask) and whether any of `0x00608a64..0x00608a74` is the created bloom effect
rather than an option; record it in the render map. Read-only Ghidra pass on the
pinned image
(`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`) with the
existing `X3DecompileFunctions.java` recipe; no new tooling. Stages 1–4 above do
not depend on it.

## Stage 1 implementation (2026-09-12)

Delivered behind `X3M_HDR=1` (`tools/manage.py launch --hdr`, requires
`--motion-output`; default off: nothing is created and no call is redirected).
Synthetic evidence is in [hdr-scene-path verification](../verification/hdr-scene-path.md);
nothing here is gameplay-verified.

### Files and hooks

| File | Role |
| --- | --- |
| `src/renderer/hdr_pass.{h,cpp}` | `HdrPass`: the owned `A16B16G16R16F` target (level-0 surface only, one device reference in both reference models), the attach-time capability gate and four-format self test, the viewport/scissor-preserving `bind`, the write-back ladder, the recovery recheck; every device call through the route's native slots; fixture fault seam |
| `src/temporal/hdr_writeback_ps.hlsl` → `src/renderer/hdr_writeback_program{,_inc}.h` | the stage-1 identity tonemap (`ps_3_0`, `tex2D` at TEXCOORD0, alpha carried), compiled and pinned by `tools/shaders/generate_rigid_motion_pixel.py --shader hdr_writeback` (provenance `verification/results/hdr-writeback-program.json`) |
| `src/proxy/motion_output.{h,cpp}` | the redirect state machine (Off / Active / Suspended), the latch decision, the write-back policy at every consumer, the logical-binding shim answers, the `hdr_frame` telemetry and the capture-frame `hdr_<device>_<frame>.rgba16f` readback |
| `src/proxy/capture.cpp` | `X3M_HDR` parsing; slots 38 (`GetRenderTarget`) and 32 (`GetRenderTargetData`) hooked with the switch on; the substituted `SetRenderTarget(0)`; `ColorFill`/`UpdateSurface` destination checks; the `EndScene` flush |
| `src/proxy/telemetry.{h,cpp}` | metrics `hdr_redirect`, `hdr_writeback`, `hdr_writeback_draw`, `hdr_writeback_stretch`, `hdr_bind`, `hdr_recheck` |

**Gate (attach, after the route's own gate).** `ps_3_0`,
`NumSimultaneousRTs ≥ 2`, `MRTINDEPENDENTBITDEPTHS`; `CheckDeviceFormat`
for `A16B16G16R16F` as a render-target texture, with
`D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING`, and as a plain sampled texture
(the write-back samples it); `D3DUSAGE_QUERY_FILTER` and
`CheckDeviceFormatConversion(A16B16G16R16F → A8R8G8B8)` are recorded (the
filter gates stage 2's bloom, the conversion enables the emergency
`StretchRect` rung), `MRTPOSTPIXELSHADERBLENDING` is informational (RT1/RT2
are never bound across a blended draw). Then the self test on a 4×4 set: an
MRT draw writing `(2, 8, 0.5, 0.25)` into FP16 + `(1, 2, 3, −1)` into RGBA32F
+ `0.625` into R32F (three formats when the route produces depth, two
otherwise), an additive `ONE`/`ONE` draw into the FP16 target alone
(`(4, 16, 1, 0.5)`, the blend capability exercised for real), and the
write-back program copying the sum into a 4×4 A8R8G8B8 target
(`255, 255, 255, 128`: clamp and alpha carry), and — whenever the conversion
is granted — the emergency rung itself: the 8-bit target filled black, then
`StretchRect(FP16 → A8R8G8B8, POINT)` must reproduce the copy
(`stretch_errors`); a failure there demotes the rung (the ladder never
calls a copy known not to work) without refusing the feature, so both copy
rungs are proven at attach wherever they exist (review 22). The conversion
is queried for the back buffer's actual format (`main_format` on the
`hdr_device` line; A8R8G8B8 for the game). Any failure of the four checks:
`hdr_device enabled=0 reason=…`, no object kept, the frame proceeds as with
`X3M_HDR=0`.

**Topology per frame.**

1. `before_clear`: a copy of the selector is probed with the pending event;
   only the Clear that will latch (AwaitInitialClear → Background) redirects.
   MSAA on the latched pair refuses (`refused_msaa`; the selector never latches
   one anyway). The target is (re)created at the latched size; the game's RT0
   is fetched natively, checked against the shadow's description and held (one
   reference until the end); `SetRenderTarget(0, target)` with the viewport
   and scissor rectangle read before and written back after (D3D9 resets
   both). The game's Clear then clears the target. A failed Clear (the
   selector rejects) restores the binding at once without a write-back
   (`end=clear_failed`, `writebacks=0`: the never-cleared target is not the
   frame's content; the main target keeps what the failed Clear left).
2. Scene draws land in the target; RT1/RT2 bind beside it (the FP16 +
   RGBA32F + R32F MRT of §1). Every draw and every Clear with the target flag
   marks the content pending.
3. The end: at the engine scene-end hook (`end=hook`, before the compositor's
   `GetRenderTarget(0)`), else at the recognized bloom copy (`end=bloom_copy`,
   in `before_stretch` before the resolve), else at Present (`end=present`).
   `EndScene` **flushes** without ending (write-back, target stays bound), so
   a frame without a recognized scene end (glow off without the hook) is
   written back while draws are still legal, an environment-map excursion
   between `EndScene` and `BeginScene` keeps the redirect, and the terminal
   Present end normally finds nothing pending (`dirty_at_present=0`). The
   design's rule "reaching EndScene/Present redirected is a bug" is therefore
   refined: Present is the documented terminal end; content still pending
   there is the anomaly the counter records. Stage 1 order at the scene end:
   write-back, then the TAA resolve on the 8-bit target (stage 3 moves TAA
   onto the FP16 image).

**Logical-binding shim.** Every consumer keeps seeing the application's
A8R8G8B8 RT0:

| Consumer | How |
| --- | --- |
| `SceneBoundarySelector`, `scene_bound`, RT/constant shadow | fed from the application's calls only; `describe_binding(0, surface)` records the application's argument; `resync_shadow` maps a physical FP16 RT0 back to the held main surface |
| `describe_surface` | called on application arguments, never on the substituted target |
| state blocks | D3D9 state blocks do not capture render targets; `Apply` resynchronizes the shadow as before |
| application `GetRenderTarget(0)` (slot 38 hooked with the switch on): `snapshot()`, `clear_rt0`, `SceneCapture::bindings`, the game's compositor | returns the held main surface with one added reference while Active; other indices and other states forward |
| application `GetRenderTargetData(main, …)` and a `StretchRect` reading main that is not the bloom copy | a flush first (write-back, redirect continues); the read then sees the current scene |
| application writes into main (`StretchRect` destination, `ColorFill`, `UpdateSurface`) | the redirect ends first, so the write is never overwritten |
| ownership wrapper accounting | the FP16 texture is dropped after `GetSurfaceLevel`: one child = one logical device reference, counted by `device_references()` with the write-back shader; the held main reference is a child `AddRef`, released at every end and in `release_resources`; the wrapper's `unwrap` sees only wrapper pointers |
| lazy-mode restore, RT1/RT2 binding | slots 1/2, untouched; `restore_bindings` runs before every end |
| the sentinel fill, `TemporalPass::SavedState` | save and restore the *physical* RT0 (the target) with viewport and scissor; correct by construction |
| TAA resolve surface identity | the resolve runs after the end; `scene_end_hook` queries RT0 natively only after `end_redirect` |
| final device Release | `device_references()` includes the pass's two objects; `release_resources` drops the redirect without a write-back |

Not covered by the shim: `UpdateTexture` into a texture whose level is the
main target (the game's main target is the back buffer), `GetFrontBufferData`
(never called in a frame, §9 evidence), and an application `Lock` of RT0
(impossible on the back buffer).

**Write-back policy for a mid-scene RT0 switch (decided).** Application
`SetRenderTarget(0, other)` while Active: the pending content is written back
first (the main target holds the scene so far whether or not the application
ever rebinds it), the bind is forwarded verbatim and the redirect is
*suspended* (physical = logical = `other`; the FP16 target keeps its
content). `SetRenderTarget(0, main)` while suspended binds the target instead
(the application's expected viewport/scissor reset happens on the same
dimensions) and the redirect resumes without a copy: the content of the
excursion's target never touches the scene. `SetRenderTarget(0, main)` while
Active rebinds the target (the selector's Pattern rejection of a mid-scene
rebind is unchanged). Reset and the final device Release drop the redirect
without a write-back (`end=dropped`); before a Reset the held main surface
is bound back to RT0 first, so the FP16 texture is not kept alive by the
device's RT0 binding through the Reset and is not RT0 after a failed one
(the application's own pre-Reset `SetRenderTarget(0, main)` would be
substituted while Active; review 22).

**Must-unwind ladder** (`HdrPass::write_back`): (1) the identity copy draw
(explicit save of RT0–3, depth, viewport, scissor, FVF/declaration, VS, PS,
stream 0 and its frequency, texture 0, eight sampler states, the two stage-0
states, 23 render states; RT1.. unbound before RT0 changes, since every
bound target must match RT0's dimensions; restore in D3D9 order, the source
texture unbound before the target rebind; RT0 left at the caller's final
binding, so an end costs no extra bind; a scene bracket the pass opened is
always closed, lost device included, or the application's next `BeginScene`
would be refused); (2) on a failed draw, `StretchRect(target → main,
POINT)` when the conversion was granted, proven by the self test and the
device is not lost; (3) an explicit rebind of the final RT0 with the
viewport and scissor preserved whenever (1) did not end cleanly. Any rung taken past (1) logs one
`hdr_unwind=<draw|restore|stretch|lost|bind>` line and blocks the redirect:
the next latch runs the self test again (`hdr_recheck`, then every 60 latches
while it fails; Reset clears the block). Finding: rung 3 guarantees the
binding invariant (the compositor never sees the FP16 surface) but not an
image — with `D3DSWAPEFFECT_DISCARD` the main target's previous content is
undefined after Present (zeros on this backend), so both copy rungs failing on
a non-lost device is the one residual black-frame case (one frame, then
blocked). A lost device fails both rungs too, but its Present fails as well.

**Telemetry.** `hdr_device` once per device (gate verdict, every cap
HRESULT, the self-test detail); `hdr_target` per creation (size, bytes);
`hdr_frame` in capture frames and every `X3M_MOTION_FRAME_LOG` frames with
telemetry on: `redirected`, `end`, `writebacks`, `flushes`,
`writeback_source=shader|stretch|restore|none`, `unwind`, `unwind_reason`
and the four rung HRESULTs, `blocked`, `recheck`, `suspended`, `resumed`,
`dirty_at_present`, `refused_msaa`, `target_create`, `latch_bind`, `target`,
`target_bytes`, `caps`, `stretch_conversion` and the CPU-inclusive
`redirect_us`, `writeback_us`, `writeback_draw_us`, `writeback_stretch_us`,
`bind_us`, `recheck_us`.

**Evidence and deviations** (numbers in the verification record):

- Case 1 as specified — bit-identical presented frames — does **not** hold
  through an FP16 intermediate: an unquantized material output is rounded to
  11 significant bits before the 8-bit conversion, and a value near a code
  boundary lands one code away from the direct path (double rounding). The
  measured result on this backend is ~99% of pixels exact, the rest one code
  apart, only on lit material pixels, alpha exact, background and flat
  pixels exact; the runner accepts ≤ 1 code and ≥ 98% exact and records the
  numbers. Exactness would need an FP32 scene target (twice the bandwidth)
  and is not pursued.
- Case 3 holds: the RT1/RT2 readbacks of the routed draws through the
  FP16 + RGBA32F + R32F MRT are byte-identical to the non-HDR run, and the
  forced-absent capability and self test disable the feature without a
  redirect.
- Case 4 holds with the rung-3 finding above.
- HDR values: 2.0 plain plus 8.0 additive read back as 10.0 (alpha 1.5), 8.0
  plain as 8.0 (alpha 0.5), presented as 255/255/255 with alpha 255 and 128;
  the in-range `(0.75, 0.25, 0.375, 0.625)` (exact in FP16, non-integer 8-bit
  codes) presents as exactly `191, 64, 96, 159` on every pixel, so the
  one-code deviation above is the double rounding of unquantized values and
  not a bias or a sampling offset; a Reset with a dimension change
  re-creates the target at the new size; a Reset issued while the redirect
  is active succeeds with the main surface handed back first; a mid-scene
  switch to another target suspends and resumes with the frame intact.

## Stage 2 implementation (2026-09-12)

Delivered behind `X3M_HDR_TONEMAP=agx` (`tools/manage.py launch --hdr
--hdr-tonemap`, requires `X3M_HDR=1`) with its sub-switches; every default
reproduces stage 1 exactly (`X3M_HDR=1` alone is still the identity
write-back, bit-for-bit: the identity program, its constants and its draw are
untouched, and the stage-1 twins of the suite are rerun unchanged). Synthetic
evidence is in [hdr-scene-path verification](../verification/hdr-scene-path.md)
("Stage 2"); nothing here is gameplay-verified. Honest scope: with the tonemap
on, the presented image is the AgX transform of a **gamma-space** FP16 scene
decoded per §2 (the documented approximation: the game blended in gamma
space), and it is still LDR to the game's bloom, HUD and text, which draw
after the write-back exactly as in stage 1.

### Switches and defaults

The 2026-09-16 production/launcher default is Auto capped at +1.3 EV by user
choice. Run 27 exercised and visually accepted the earlier explicit +1.5-EV
configuration; see [current exposure policy](space-exposure-policy.md).
Standalone `ExposureParams` and its historical fixtures retain the +2 maximum.
Installation status is recorded separately in [status](../status.md).

| Switch | Values | Default | Note |
| --- | --- | --- | --- |
| `X3M_HDR_TONEMAP` | `identity` \| `agx` (`1`) | `identity` | The design does not say the tonemap defaults on with `X3M_HDR=1`, and §7 case 1 names `identity` explicitly, so stage 1 stays the default and the AgX path is opt-in |
| `X3M_HDR_DECODE` | `gamma2.2` (`pow22`) \| `srgb` \| `none` | `gamma2.2` | §2 default; applies to the tonemap input and to the meter's level 0 |
| `X3M_HDR_LOOK` | `none` \| `golden` \| `punchy` | `none` | §3 triples through `agx.h::set_look` |
| `X3M_HDR_CLAMP` | float > 0 | off (65504 uploaded) | §2 firefly guard, `min` on the decoded input |
| `X3M_HDR_EXPOSURE` | `auto` \| `manual` \| `fixed` | `auto` | manual without an EV is EV 0 |
| `X3M_HDR_EV_MANUAL` | EV in [−16, 16] | unset | forces `manual` with that EV, clamped to [`X3M_HDR_EV_MIN`, `X3M_HDR_EV_MAX`] (−3..+1.3 in the production/launcher default; standalone components retain +2) so `exp2(EV)` stays inside the constant block's range; the `hdr_tonemap` line prints the requested value, `hdr_frame … ev=` the effective one; the chain does not run (deterministic; the fixtures) |
| `X3M_HDR_EV` (alias `X3M_HDR_EV_OFFSET`) | EV in [−16, 16] | 0 | the offset added to the auto target. **Deviation from the §3 text**, where `X3M_HDR_EV` forced the EV: the orchestrator's stage-2 brief names `X3M_HDR_EV` as the offset and `X3M_HDR_EV_MANUAL` as the override, and that is what is implemented; the design's `X3M_HDR_EV_OFFSET` remains accepted as the alias |
| `X3M_HDR_KEY`, `X3M_HDR_EV_MIN/MAX`, `X3M_HDR_ADAPT_UP/DOWN` | floats | 0.18, **−3/+1.0**, 0.4 s/1.2 s | production defaults; `exposure_reference.py`/standalone components retain +2; explicit limits remain supported |
| `X3M_HDR_METER_BG` | scene-linear luminance in [1e-4, 64] | 1/512 | tiles whose geometric-mean luminance is below it are the black sky: excluded from the key rule (`--hdr-meter-bg`) |
| `X3M_HDR_METER_MIN_LIT` | fraction in [0, 1] | 0.01 | fewer lit tiles than this fraction of the tile image: the target is neutral (EV 0 plus the offset) |
| `X3M_HDR_WHITE_TARGET` | fraction in [0, 4] | 0.9 | the highlight limit: the brightest 1 % of tiles (the p99 tile maximum) may reach this fraction of the AgX white (`exp2(4.026069)` = 16.29 scene units); 0 disables the limit (`--hdr-white-target`) |
| `X3M_HDR_KEY_PULL` | fraction in [0, 1] | 0.25 | how much of the key rule applies when the lit median is *brighter* than the key: a white full frame is pulled down to −0.62 EV, not to mid-grey; 1 is the classic rule both ways (`--hdr-key-pull`) |
| `X3M_HDR_EV_DEADBAND` | EV in [0, 8] | 0.25 | the held target moves only when the freshly metered target differs from it by more than this; 0 disables the band (`--hdr-ev-deadband`) |
| `X3M_HDR_METER_EDGE_WEIGHT` | weight in [0, 1] | 0.35 | the lit statistic's tile weight at the frame corners, 1 at the centre (a raised cosine of the distance from the centre); 1 is unweighted (`--hdr-edge-weight`) |
| `X3M_HDR_DT_MS` | ms in (0, 1000] | 0 (QPC) | a fixed adaptation step; fixtures and A/B only |

### Files

| File | Role |
| --- | --- |
| `src/temporal/agx.hlsl` → `src/renderer/hdr_tonemap_program{,_inc}.h` | the AgX write-back (`ps_3_0`, s0 the FP16 scene, c8..c21 the `AgxConstants` block, alpha carried); the only change from the stage-2 preparation is a `1e-10` floor under the decode's `pow` (SM3 `log` of an exact zero), applied to the gamma and sRGB branches only so `decode=none` passes the raw value through, negatives included, as the reference does (review 23); compiled and pinned by `generate_rigid_motion_pixel.py --shader hdr_tonemap` (`verification/results/hdr-tonemap-program.json`, 414 words) |
| `src/temporal/hdr_meter_level0_ps.hlsl`, `hdr_meter_reduce_ps.hlsl` → `src/renderer/hdr_meter_program.h` + `hdr_meter_{level0,reduce}_program_inc.h` | the meter chain: level 0 folds `v = log2(clamp(luma(decode(scene)), 1e-4, 64))` into the first 4×4 reduction (one FP16 read per scene pixel, no full-resolution level-0 write) and writes `.r` = the mean of the 16 taps, `.g` = their maximum; `reduce` averages `.r` and maximises `.g` over 16 taps of the previous two-channel level; taps are clamped by the CLAMP sampler exactly as `exposure_reference.reduce_tiles` clamps its coordinates (1996 / 462 words since the space-aware meter) |
| `src/renderer/exposure.{h,cpp}` | the mechanical port of `exposure_reference.py`: `decode_channel`, `luma`, `meter_level0`, `meter_clipped`, `reduce_mean`, `reduce_chain` (both channels, to a tile image or 1×1), `tile_weights`, `meter_statistics`, `ev_key`, `ev_limit`, `exposure_target`, `apply_deadband`, `ev_target`, `clamp_dt`, `adapt_rate`, `adapt`, `exposure_multiplier`, `resolve_ev`, `taa_k`, `luma_weight`, `weight_color`, `unweight_color`, `ExposureParams` (the defaults), `MeterStatistics` and `ExposureState` (`step` = the loop body of `simulate`); no D3D types; `verification/analysis/test_exposure_port.py` compiles it natively and replays the reference on its output |
| `src/renderer/hdr_pass.{h,cpp}` | `HdrConfig`, the stage-2 gates in `attach` and the self test, the meter chain resources and draw (`ensure_chain`, `meter_chain`: levels down to the tile image, the tile-image ring and readback surfaces, the host tile copies and centre weights), the lagged readback, the host statistic and the adaptation at the latch (`begin_frame`), the constants (`prepare_constants`), the extended ladder in `write_back`, the c0..c21 save/restore, `references()` |
| `src/proxy/motion_output.{h,cpp}`, `capture.cpp`, `telemetry.{h,cpp}` | switch parsing (`HdrConfig`), `begin_frame` at the latch, the `hdr_tonemap` attach line, the `hdr_frame` fields, `hdr_tonemap_disabled`, the metrics `hdr_meter` and `hdr_meter_readback`, the exported `hdr_taa_k()` (stage 3 consumes it; nothing does yet), the fixture export `x3m_hdr_fixture_exposure` |

### Gates (attach, inside the enabled feature)

Neither the tonemap nor the meter can refuse `X3M_HDR`; each demotes itself
to the stage-1 behaviour with a reason on the `hdr_tonemap` line. Tonemap:
`CreatePixelShader(agx)` (`reason=shader`), then in the self test a tonemap
draw of the 4×4 additive sum must succeed with one code on every pixel and
alpha 0.5 carried (`reason=self_test`). Meter (auto exposure only): a
two-channel float render-target texture format that is also samplable —
`CheckDeviceFormat(RENDERTARGET, TEXTURE, …)` and usage 0 on `G32R32F`, else
on `A32B32G32R32F` (`chain_target`, `chain_sampling`, `chain_format` on the
`hdr_tonemap` line; the RGBA32F fallback is the self test's motion format,
so the feature already required it), the two meter programs (`shader`), then
in the self test a one-level chain on the 4×4 sum through a temporary 1×1
tile-image ring slot must read back the host's `meter_level0((4, 16, 1))` in
both channels (the mean and the maximum of a uniform block) to 1e-4 —
exactly 6.0 with the gamma or sRGB decode (322 clipped to 64), 3.628 with
`none` (`meter_errors`, `meter_value`, `meter_max`, `meter_expected` on the
self-test detail). The chain levels are created with the target at the
latched size (`hdr_target … chain_levels= chain_bytes=`); a failed creation
disables the meter for the device (`meter_reason=chain`). Manual exposure or
the identity tonemap never create a chain.

### Exposure: meter on the GPU, adaptation on the host (a documented deviation)

§3 keeps the adaptation state in a 1×1 R32F ping-pong written by a 1×1
draw with nothing read back per frame. Stage 2 runs the **chain** on the
GPU and the **statistic and adaptation** on the host: the last chain draw
lands in one of two **tile-image** ring targets (two-channel float, no axis
above 128 texels: 80×48 at 1280×768, 80×23 at 5120×1440, 16×16 in the
64×64 fixtures); **at the next frame's latch** (`HdrPass::begin_frame`)
`GetRenderTargetData` copies that target into its system-memory surface,
`LockRect` reads the tiles into two host arrays (8 or 16 bytes per texel by
the chain format; 30 KB at 1280×768), so the download waits on work
submitted a Present earlier, never on the current frame (measured for the
1×1 form: issuing the copy right after the chain, inside the write-back,
cost ~0.7 ms per frame on WineD3D at every size — the backend waits for the
queued frame there — whereas the deferred copy plus lock costs 30–80 µs);
`meter_statistics` reduces the tiles to the space-aware statistic (below)
and it feeds `ExposureState::step(statistic, dt)` with `dt` the QPC interval
between the two latches (clamped to [1/240, 1/5] s in the step;
`X3M_HDR_DT_MS` replaces it for the fixtures); `prepare_constants` uploads
`exp2(EV)` as c8.x for that frame's write-back. The ring keeps the tile
image intact while the current frame's chain writes the other slot. The
tonemap of frame *n* therefore consumes the EV adapted from frame *n−1*'s
meter, as §3 specifies. Reasons for the deviation: the prepared fragment
already takes the exposure as a constant (c8.x, pinned by the reference
test); the host state is what the `hdr_frame` line, the fixtures (`≤ 1e-3
EV` against `simulate`) and the stage-3 `k` upload need; and the statistic
(a weighted median and a percentile) has no cheap SM3 form, whereas on the
host it is a sort of at most 16 K floats per frame (`readback_us` on the
frame line covers the copy, the lock and the statistic). Pass order inside
one write-back bracket (§4 with the chain before the tonemap, one state
save/restore): unbind texture 0 and RT1.., depth off, fixed
FVF/sampler/render state, the chain (RT0 = level *i*, viewport, program,
c0..c3, texture = level *i−1*, the −0.5 quad; 64×64 → 16×16: one draw
straight into the ring slot; 1280×768 → 320×192 → 80×48: two; 5120×1440 →
1280×360 → 320×90 → 80×23: three, the last into the ring slot), then RT0 =
the game's main target, the AgX program with c8..c21, texture = the scene,
the quad; restore c0..c21 with the rest. A flush mid-frame runs the chain
too and its later end overwrites the same ring slot; the meter's failure is
reported (`meter=` HRESULT) and never fails the image. A dimension change
resets the state to EV 0 (§5); a Reset at the same size keeps it.

### The space-aware meter (2026-09-13)

**Why.** The first tonemapped game run (bottle X3, run 15, 1280×768: 133
redirected frames, 132 metered) showed the plain §3 rule — the geometric
mean of the whole frame mapped to the key — is wrong for space: the frame is
mostly black, its log mean sat at the meter floor (`luma_mean` median
0.0017, min 0.00014, max 0.0103 over the metered frames; the 1.0 on frame 0
is the unmetered initial state), `ev_target` median +6.8 and `ev` +6.85
with the +8 clamp reached, and the lit station, nebula and stars were blown
out while the menu (never metered: it is not redirected) looked fine. The
meter must follow lit content and limit the lift when broad bright regions
would approach the tonemapper's white.

**Statistic** (`exposure.h`, mirrored by `exposure_reference.py`), on the
tile image the chain leaves (each tile: the mean and the maximum of the
log2 luminance of its pixels, clamped to [1e-4, 64] per pixel):

```
weight      w = edge + (1 - edge) * (1 + cos(pi * r / r_corner)) / 2   // r: distance from the frame centre; edge 0.35
lit         tile.mean >= log2(meter_bg)                 // meter_bg 1/512: the black sky is excluded
neutral     lit < meter_min_lit * tiles                  // 1 %: nothing to meter, the target is EV 0 (+ offset)
d           = log2(key) - weighted median(lit tile means)
ev_key      = (d >= 0 ? d : d * key_pull) + ev_offset    // the lift in full, the pull down at a quarter
ev_limit    = log2(white_target * 16.29) - p99(tile max) // unweighted: limit broad highlights regardless of position
ev_fresh    = clamp(min(ev_key, ev_limit), ev_min, ev_max)   // -3 .. +2
ev_target   = |ev_fresh - ev_target| > ev_deadband ? ev_fresh : ev_target   // 0.25 EV; held otherwise
```

then the §3 adaptation (τ 0.4 s up / 1.2 s down, the dt clamp) towards the
held target, and `exp2(EV)` to the tonemap. The weighted median is the
first tile of the ascending order at which the cumulative weight reaches
half the lit weight; the p99 is the element at index `tiles·99/100` of the
ascending tile maxima; both are exact selections (a sort of the lit tiles,
`nth_element` of the maxima). The host port and reference use the same
selection rule; their floating-point weights and arithmetic are compared
within the fixture's tolerances. `avg_log_l`/`luma_mean` (the old statistic, the
mean of every tile mean) stay on the log line for continuity.

**Rationale, per term.** *Background exclusion:* the sky's floor tiles
carry no exposure information, and a log mean over them is a measure of how
much sky is in view, not of how bright the ship is. A tile counts as lit
when its geometric mean is above 1/512 of white — a tile half covered by a
hull at code 128 (decoded 0.22) qualifies; a tile with a few star pixels
does not. *Neutral fallback:* with under 1 % of the tiles lit (a distant
station, a few stars) there is nothing to expose for and the picture stays
as authored (EV 0), which is also what the game shows without HDR. *Key
rule on the median, not the mean:* the median of the lit tiles is robust
to a few very bright or very dark tiles (a sun sprite, an engine glow); the
lift towards the key is applied in full (a dark hull at decoded 0.05 is
lifted +1.85 EV), but the pull down is scaled by `key_pull` = 0.25: the
classic rule would take a white menu or a bright planet to mid-grey (−2.47
EV), which the orchestrator's acceptance for a bright full frame ("EV near
0 or slightly negative") rules out; at 0.25 the white frame lands at −0.62
EV and AgX still renders 1.0 × 0.65 as 0.73 display. *Highlight limit:*
the brightest 1 % of tiles may reach 0.9 of the AgX white (16.29 scene
units, where the sigmoid saturates); on this game's 8-bit-authored content
(decoded white = 1.0) the limit sits at +3.87 EV and only engages for
content above 3.7 (sun sprites through bloom, weapon flashes, the fixture's
100-valued blocks: the meter clip 64 then gives −2.13 EV), so it is the
guard, not the driver. This is a percentile limit on the fresh target, not
a hard bound on every pixel: the brightest tail may exceed it, a tile maximum
is not a pixel percentile, the meter clips at 64, and adaptation/deadband can
temporarily retain a higher exposure. *Original meter EV range −3..+2:* this
component/history calculation uses the earlier two-stop cap; later production
policy reduced it first to +1.5 and now to +1.0 without changing the meter equations. The earlier policy
allows a two-stop lift (a hull at decoded 0.045 reaches the key at +2) and
at most three stops of pull. Darker lit content can ask for more than two
stops and is deliberately capped; the old ±8 allowed a whole-frame mean
near the floor to drive a much larger lift. *Centre weighting* (orchestrator addition): a large emitter at the
edge — a sun, a planet limb, a nebula — should not drive the exposure down
and darken the ship the player looks at, so the lit tiles are weighted by a
raised cosine of their distance from the frame centre, 1 at the centre,
0.35 at the corners, 0.51 at the edge midpoints; the shape was chosen so a
centre object of a quarter of the frame (64 of 256 tiles, weight 56.3)
outweighs a white emitter covering the whole left half (96 tiles, weight
53.4) — a linear falloff would not (52.8 against 56.6). The lit count, the
neutral test and the highlight limit stay unweighted: percentile highlights
at the edge have the same influence as those at the centre. *Dead band* (orchestrator addition): small
scene changes while turning — a tile row of nebula entering, a star field
— must not drift the exposure, so the held target moves only when the fresh
one differs from it by more than 0.25 EV. The band is measured against the
**held target**, which equals the adapted EV once the state has settled;
measuring it against the adapted EV mid-adaptation would stop every
adaptation 0.25 EV short of its target, whereas the held target lets the
adaptation converge exactly and ignores the wobble around it afterwards. A
slow drift crosses the band eventually and steps the target once, smoothed
by τ. The first step after attach or a Reset takes the fresh target.

**Run 15 replayed offline.** The per-frame log carries only the whole-frame
mean, so the tile statistic cannot be rebuilt; what the rule would have
produced: the menu, had it been metered (`luma_mean` 1.0, every tile lit at
log2 0), gives `ev_key` = 0.25 × log2(0.18) = **−0.62 EV** and `ev_limit`
+3.87, so a target of −0.62 (the old rule: −2.47). The space scene: the
lit tiles are the station hull and the nebula — 8-bit codes 20–140 decode
to 0.0036–0.2, so the lit median gives `ev_key` between −0.04 and the +2
clamp, the expected range **0..+2 EV** against the +6.8..+8 the run logged;
with the p99 tile maximum at most 1.0 (no content above white in an
8-bit-authored frame without bloom) the limit stays at +3.87 and never
engages; frames with under 1 % of the tiles lit (a station far away) stay at
EV 0. The next game run records `lit_fraction`, `luma_lit`, `luma_p99`,
`ev_key`, `ev_limit` and `ev_fresh` per frame so the range can be checked.

**Cost.** The chain is now two draws at 1280×768 and three at 5120×1440
(against six and seven for the 1×1 chain: the reduction stops at the tile
image), the readback is a 30 KB (1280×768, 80×48 × 8 B) or 15 KB
(5120×1440, 80×23) copy plus a sort of the lit tiles; the levels are
two-channel (8 B per texel with `G32R32F`, 16 with the RGBA32F fallback),
so `chain_bytes` grows from 0.26 MB to 0.6 MB at 1280×768 and from 1.97 MB
to 3.9 MB at 5120×1440. Run 15's `meter_us` 8.4 ms was frame 0 only (the
first use of the programs); the steady median was 167 µs. The bench numbers
are in the verification record.

### Must-unwind ladder, extended

(1a) the AgX draw; on a failed draw with a clean restoration (1b) the
identity draw of stage 1, reported as an unwind with reason `tonemap`
(`hdr_unwind=tonemap`, `writeback_source=shader`, `fallback=1`, the block
and the recovery self test at the next latch exactly as for any other rung);
then the stage-1 rungs (2) `StretchRect` and (3) rebind. A tonemap draw
failure with a failed restoration or a lost device goes straight to the
stage-1 rungs. After `tonemap_failure_limit` = 3 failed tonemap draws the
tonemap is disabled for the device (`hdr_tonemap_disabled … reason=draw_failures`,
`tonemap=identity` on the frame line from then on); the meter stops with
it. The stage-1 self test still exercises the identity program, so the
fallback's rung is proven at attach whenever the tonemap is.

### Telemetry

`hdr_tonemap` once per device (both verdicts and every switch in force,
including the derived time constants, the meter's `meter_bg`,
`meter_min_lit`, `white_target`, `key_pull`, `ev_deadband`, `edge_weight`,
`tile_max` and the `chain_format` with its two gates); `hdr_target` gains
`chain_levels`, `chain_bytes`, `meter`, `meter_reason`; `hdr_frame` gains
`tonemap` (the program in force), `tonemapped` (the last write-back's image
is AgX), `look`, `decode`, `clamp`, `exposure`, `ev` (consumed),
`ev_adapted`, `ev_target` (the held target), `avg_log_l`, `luma_mean`
(`exp2(avg_log_l)`, the old whole-frame statistic), `lit_fraction`,
`luma_lit` (`exp2` of the weighted lit median), `luma_p99` (`exp2` of the
p99 tile maximum), `ev_key`, `ev_limit`, `ev_fresh` (this step's target
before the dead band), `tiles`, `lit`, `dt_ms`, `stepped`, `steps`, `meter`,
`readback`, `tonemap_draw`, `fallback`, `meter_us` (the chain inside the
draw bracket), `readback_us` (the copy, the lock and the statistic at the
latch), `k`, `chain_bytes`; metrics `hdr_meter`, `hdr_meter_readback` under
`X3M_TELEMETRY=1`.

### Verification (summary; numbers in the verification record)

`hdrramp` (64×65: 65 ramp rows × neutral/red/green/blue, alpha `row/64`,
drawn as constant quads, the FP16 input taken from the capture-frame
readback so the comparison isolates the program): the presented codes
against `agx_reference.tonemap_engine` on the exact FP16 inputs, per look,
decode (`gamma2.2`, `srgb`, `none`), clamp and manual EV, seam and
production DLL; gate ≤ 1 code max, ≤ 0.5 code mean per channel (§8), plus
the identity program on the same ramp. `hdrexposure` (40 frames: ten of
mid-grey, ten bright, ten mid-grey with a block at 100 far above the clip,
five with −1 / +Inf / −Inf blocks, five with a NaN block — the infinities and the NaN are unspecified on this backend and behave alike, white and metered at the floor, the host guard keeping the state finite; fixed `dt`): the chain against the reference geometric mean of the FP16
blocks (≤ 1%), the EV sequence against `simulate` replayed on the measured
meters (≤ 1e-3 EV; the end-to-end error recorded), the up/down direction,
the clip's effect on the sun frames, the presented blocks against the
reference at the consumed EV (≤ 1 code); once more with an EV offset and
other time constants, and once through the ownership wrapper (the chain's
references at teardown). Since the space-aware meter the script continues
with the synthetic space scenes (a black sky with a lit patch, a white
frame, mid-grey with super-bright sparks, uniform grey, the dead-band
sequence of small then large changes, a white edge emitter against a
centre object; 120 frames) and the DLL's tile statistic, its target terms
and its held target are checked against the reference on every
deterministic frame. `hdrtonemapfault`: the ladder above frame by frame
against the FP16 readback, and the program forced absent at attach (fault
12). Bench: the stage-1 bench with the tonemap and the meter on. Cost
finding: the AgX draw itself is free against the identity draw within the
bench's spread (manual EV: no chain); the auto-exposure chain costs about
+0.35 ms at 1280×768 (six small draws submitted at ~25 µs each plus the
deferred readback), the lever being a coarser chain (8× per axis, three
draws) if a gameplay profile shows it.

## Stage 3 implementation (2026-09-12)

Delivered behind the existing switches — `X3M_HDR=1` with `X3M_TAA=1`
(`tools/manage.py launch --hdr --taa`, both requiring `--motion-output`) —
with one new diagnostic switch, `X3M_TAA_K` (`--taa-k`). With `X3M_HDR=0`
the 8-bit route of temporal step 3 is untouched bit for bit; with
`X3M_TAA=0` stages 1 and 2 are unchanged.

* `X3M_TAA_SHARPEN=<0..1>` (`--taa-sharpen`, requires `--taa`; added after
  stage 3): the write-back that samples the resolved image runs the RCAS
  variant of its program — identity+RCAS, or AgX on each of the five taps
  followed by RCAS, i.e. sharpened after the tonemap — and never touches the
  published history; 0 or unset keeps the stage-3 programs bit for bit
  ([post-resolve sharpen](temporal-integration.md#post-resolve-sharpen-2026-09-12),
  [taa-sharpen.md](../verification/taa-sharpen.md)). The mechanism, the weighting and
the derivation of `k` are recorded in
[temporal-integration.md](temporal-integration.md#stage-3-of-the-hdr-scene-path-taa-on-hdr-2026-09-12);
the numbers in [hdr-scene-path verification](../verification/hdr-scene-path.md)
("Stage 3") and [temporal-resolve.md](../verification/temporal-resolve.md).
Nothing here is gameplay-verified.

### What changed against the §3/§4 text

| Item | Design | Implemented |
| --- | --- | --- |
| Resolve input | `in.color` = the FP16 scene texture, no scratch copy | As designed: the target's container (`GetContainer`, one reference per run); `ensure_scratch` and the `CheckDeviceFormatConversion` gate are not exercised on this path (they remain for the 8-bit input) |
| Resolve output → write-back | "Output::color, an FP16 history texture" consumed by the tonemap | As designed, by ping-pong: the write-back (`HdrPass::write_back(..., source)`) samples the pass's output texture; no copy back into the scene target. The emergency `StretchRect` rung copies the target, i.e. the unresolved scene (an image, never black) |
| Order at the scene end | scene FP16 → TAA → meter → AgX → compositor | As designed at both scene ends (`resolve_hdr` then `end_redirect` in `scene_end_hook` and `before_stretch`); stage 1's "write-back then resolve on the 8-bit RT0" survives only when the redirect is not active for the frame |
| Weighting | `w = 1/(1 + k·luma)` on every current tap and the history tap, inverse after | As designed, on the current pixel, the 3×3 statistics and each of the 16 Catmull-Rom taps, with the luma floored at 0 (review 24: a negative-luma pixel, possible from a subtractive blend into the FP16 scene, is the identity instead of a zero or negative weight); the inverse denominator is floored at 1/65504; `k = 0` is an exact identity by construction (a compare selects the constant 1.0), not by `1/(1+0)` |
| `k` | "uploaded as the adapted exposure" | `k = exp2(EV)` consumed by the same frame's AgX write-back (manual or adapted at the latch); **0 with the identity write-back** (no exposure model: the unweighted resolve keeps the stage-1 TAA twins within one code of their 8-bit twins); `X3M_TAA_K` overrides |
| Constant register | "a free constant register" | `c22` (`kLuminanceRegister`), leaving c8..c21 to the AgX block; `ResolveConstants` grew to nine registers, uploaded as c0..c7 plus c22 |
| Failure | — | A failed run leaves the write-back its unresolved source (`motion_output_taa_failed … hdr=1`, `taa_hdr=1`, the frame line's `taa_result`), the pass drops its history; every end of the redirect, `before_reset` and `release_resources` clear the borrowed output pointer; fixture fault `HdrFault::Resolve` (14) |
| Debug readbacks | — | `X3M_TAA_DEBUG` on the HDR path flushes the unresolved scene into the main target first so the 8-bit "pre-resolve colour" readback is meaningful; the FP16 `hdr_<device>_<frame>.rgba16f` readback is the unresolved scene, `taa_<device>_<frame>.rgba16f` the resolved one |

### Files

| File | Change |
| --- | --- |
| `src/temporal/resolve.hlsl`, `resolve.h` | `luminance` (c22), `weigh`/`unweigh`, the weighted statistics and blend; `prepare(..., luminance_k)` validates `0 ≤ k ≤ 65504`; `kResolveRegisterCount`, `kLuminanceRegister` |
| `src/renderer/temporal_resolve_program{,_inc}.h` | regenerated (4,487 words after review 24's luma floor, 4,375 before; `verification/results/temporal-resolve-program.json`); the generator's sanity bound raised from 16 KB to 32 KB of bytecode |
| `src/renderer/temporal_pass.{h,cpp}` | `FrameInputs::luminance_k`, the c22 upload, the documented FP16 input path |
| `src/renderer/hdr_pass.{h,cpp}` | `write_back(..., source)`: the sampled texture (resolved output or the target's container), the meter chain reads it too; `HdrFault::Resolve` |
| `src/proxy/motion_output.{h,cpp}` | `resolve(main, hdr_scene)`, `resolve_hdr`, the order at both scene ends, `hdr_resolved_`, `k` at the latch, `taa_hdr`/`taa_k` on the frame line, `configure_taa_k` |
| `src/proxy/capture.cpp` | `X3M_TAA_K` parsing, `taa_k` on the `motion_output_mode` line |
| `verification/probe/temporal_pass_fixture.cpp`, `run_temporal_pass.py` | `hdr_cases` (k validation, firefly, stationary gradient, saturated edge, negative channels); counts 448 / 204 / 386 |
| `verification/probe/temporal_resolve.cpp` | uploads c22 = 0 explicitly (78 samples unchanged) |
| `verification/probe/motion_output_fixture.cpp`, `run_motion_output.py` | the reference pass fed with the FP16 scene through the seam and the DLL's `k`; the presented comparison per write-back kind; the AgX + TAA cases, the `X3M_TAA_K=0` case, the hook, wrapper and production variants, the TAA fault script with fault 14; the coverage oracle skipped on tonemapped frames |
