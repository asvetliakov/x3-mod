# Screen-space ambient occlusion on the linear material path

Design note for goal 7 (GTAO/SSAO), written 2026-09-14 at `4ce8fc8` for ratification; steps 1,
1b and 2 are implemented (sections at the end). Roadmap row 7 gate: stable depth interpretation, temporal filtering, correct occlusion
and HUD separation.


Ratified by the main session 2026-09-14 as v1 (half-resolution GTAO, depth-reconstructed
normals, applied as one exact multiply before the TAA resolve, strength 0.5, default-off
behind a launcher option). Conditions: step 1 is the detached pass and fixture with the
analytic oracle; the scene-end hook placement waits for the run-14 capture query on Z-test-off
draws before the scene end and for the radius scale check; the sun-weighted v2 stays a
later design.
## Decision

Run a **half-resolution GTAO** chain (linearize → horizon search → two depth-aware blur passes →
bilateral upsample) at the route's **scene-end hook, immediately before the TAA resolve**, reading
the route's existing R32F depth target RT2 and reconstructing normals from depth. Apply it as **one
multiply into the owning scene target** through a `ZERO/SRCCOLOR` blend of
`pow(1 − s·(1 − ao), 1/2.2)`, which is exact for the gamma-2.2 compatibility encoding the converted
materials write. Temporal filtering is the existing resolve: the AO-darkened image is the resolve's
current sample, so the jitter-rotated GTAO noise integrates through the history at weight 0.9.
Everything is documented D3D9 (render-target textures, one pixel shader per pass, the embedded
vs_3_0 quad), fail-closed to no AO, released before Reset like RT1/RT2. Sun-aware (directional)
weighting is a v2 refinement gated on two bounded disassembly briefs (section 8).

## 1. Inputs

**Depth.** RT2 is `R32F` at main dimensions holding *device* depth `z/w` in [0,1] where a routed
draw covered the pixel and `-1` elsewhere (`live-motion-route.md` §"Implementation";
`temporal_pass.h:41`). The goals row says "view depth": it is device depth and the AO prepass
linearizes it. Writers are the 169 SM3 table rows (every opaque material family incl. the six glass
pairs); SM2 (466 pairs), SM1 (168), bloom, background/planet draws before the depth clear,
particles, stardust, overlays and GUI leave the sentinel. RT2 is rasterized with the same per-draw
jitter as the color, so AO and color share one raster. Sentinel policy: a sentinel pixel gets
`ao = 1` and as a horizon tap it is the far plane (never an occluder), so native-path draws are
neither darkened nor occluders. Linearization: `z_view = zn·m22/(m22 − d)` with the default
projection scratch `m22 = 1.0000030`, `m32 = −6.0000184` (zn = 6, zf = 2·10⁶;
`camera-state-and-frame-routine.md` §3–4) and `m00/m11/m20/m21` the route already reads at the
latching Clear (`camera_reprojection.h:29`). Float32 `d` near 1 gives `dz ≈ z²·10⁻⁸`, bounding the
radius at distance. **Normals.** Reconstructed from the half-res linear depth (min-difference of the
four neighbors, 5 taps), not emitted. An emitted lane means a fourth simultaneous target on every
routed draw (`NumSimultaneousRTs ≥ 4`, a new free varying per program, re-qualifying 169 variants
and the 3,923-case corpus) and collides with the four-target in-place brackets.

## 2. Algorithm

XeGTAO-style: per half-res pixel 2 slices × 2 sides × 4 steps = 16 depth taps, cosine weighted
horizon integral, radius `R` in view units with a screen-space cap (64 half-res px) and a distance
falloff so a fighter in front of a distant station casts no halo. Noise: 4×4 spatial hash per pixel,
rotated per frame by the route's `jitter_index` (period 8–16). Denoise: two separable 5-tap
depth-aware passes at half res, then a bilateral upsample (4 AO + 4 half-depth + 1 full-depth taps)
in the apply pass. Loops stay rolled: the X3 device advertises
`MaxPixelShader30InstructionSlots = 512` and the resolve already sits at 507
(`platform-portability.md`). Half resolution: at 1280×768 the chain is 245,760 px × ~24 taps +
983,040 px × 9 taps ≈ 15 M fetches (< 60 MB), GPU-negligible; full resolution quadruples the search
for no visible gain under a bilateral upsample (fixture toggle only). Temporal: multiply before the
resolve. With weight 0.9 and the 1.25σ variance clip (`resolve.hlsl:23-27,64-69`) a blurred residual
noise of ~10 % of the AO contrast leaves ~1 % per-frame ripple. A separate AO history (own
reprojection, own disocclusion) duplicates the resolve's rejection logic for one more pass and two
histories; it loses for v1.

## 3. Placement and interactions

The chain runs in `MotionOutput::scene_end_hook` after the lazy-binding flush and before `taa_->run`
(`live-motion-route.md` §"Scene end", `motion_output.cpp:1352`), under the same `taa_call` reference
accounting. There RT2 is complete, every in-place bracket (fade policy 4, packed screen policy 8,
emission exchange) has finished, and compositing and HUD draws have not started (never routed; they
follow the signal). The resolve consumes the darkened target, the bloom copy sees the resolved
image, the HUD is untouched: HUD separation is by phase, not depth. The reactive mask M is not read
or written. Blended Z-write-off scene draws (7–21 per frame, `emission-draw-order.md`) over routed
surfaces are darkened by the same factor; the strength floor bounds it and the debug view shows it.
If a run objects, v2 moves the chain to a mid-scene bracket at the first blended draw. Rule
(step 2): the chain runs only on a frame the resolve will take, evaluated with the resolve's own
preconditions (TAA initialized through the same `ensure_taa`, the strict-sentinel transform skip, jitter active, RT2 filled, no state block recording, no active
application queries, no MSAA, the resolve not yet attempted this frame) plus a valid scene camera,
so a darkened sample is either temporally filtered or not presented at all. The bloom-copy fallback
(the route's scene end when the engine hook did not signal) runs the chain under the same contract,
before its resolve, when the hook did not run it that frame (`source=copy` on the frame line).

## 4. Application and what it must not touch

The converted material is `L = A·(P + M + D) + R + E`, written as `encode_gamma22(L)` to RT0
(`scene-linear-materials.md`); there is no separate ambient constant on the Argon path.
`encode(L·f) = encode(L)·f^(1/2.2)`, so the blend multiply is exact on FP16 and, with 8-bit
quantization, on the plain target. It is a **full multiply**, an approximation of ambient-only
occlusion: with the captured directional colors D0 = (170,200,150)/256 and D1 = (33,66,55)/256
(`material-color-inputs.md`) a sunlit crease has a fill share of 0.16–0.27 of its diffuse, so exact
ambient-only AO at `ao = 0.5` darkens it 8–13 % while a full multiply darkens 50 %; on the
sun-averted side (N·L0 ≤ 0) the full multiply is exact. Decision: strength `s = 0.5` (factor
`1 − s·(1 − ao)`, floor 0.5): 25 % in the sunlit case (2× over) and 25 % on the dark side (2×
under), tunable in the run. Not touched: native-path pixels (sentinel → factor 1), HUD/compositing
(phase), M. Emissive E, glass and cutout alpha are converted draws and *are* multiplied: emissive
windows in creases lose up to `s`; the v2 sun-weighted variant and an optional M-based exemption
address this if run evidence asks.

## 5. Lifetime, capability, native Windows

`renderer::AmbientOcclusionPass` follows `TemporalPass`/`LinearEmissionPass`: `attach` (device,
native table, caps), `ensure_targets`, `run`, `before_reset`/`after_reset`, `detach`; every call
through native slots; state saved in one owned `D3DSBT_ALL` block. Targets, all `D3DPOOL_DEFAULT`:
half-res R32F linear depth and two half-res R16F AO (ping-pong), ≈1.9 MiB at 1280×768. Gates at
attach: route producing depth (RT2 owned), `CheckDeviceFormat` R16F render target + point sampling,
post-pixel-shader blending on the owning format (already required by the FP16 path; else the
resolve's copy-through-scratch pattern); anything else → `ao=0 ao_reason=` on the device line. A
create/bind/draw failure mid-chain restores the block and skips the frame. Released after RT1/RT2 in
`before_reset`, re-created lazily after `after_reset(S_OK)`; shader objects survive Reset. Native
Windows: documented D3D9 only, cross-compiled with the SSE2/stack contract; runtime unverified (add
a bullet to `platform-portability.md`).

## 6. Budget and measurement

Run-11 median is 4.097 ms at 1280×768 (`screen-emission-region.md` §5). Estimate: five quads and one
save/restore ≈ 0.2–0.35 ms fixed CPU-side on this backend (the fade bracket floor is 0.10–0.16 ms
for 65 getters/285 setters plus two quads), GPU work under 0.1 ms. Acceptance: paired on/off windows
in game within **+0.5 ms** of the median (12 %); above +0.8 ms drop to one slice (8 taps) or refuse
by default. Order: detached fixture EVENT-fenced windows at 1280×768 and 1920×1080, then the live
route fixture's paired windows, then the user run's frame-line medians. Diagnostics: capture-frame
readback `ao_<device>_<frame>.r16f` beside the depth file (`motion_output.cpp:3939`), `ao_ms=` in
the frame line, a debug view (`X3M_AO_DEBUG`, comparison hotkey) writing the AO term as grayscale
instead of multiplying, and an on/off hotkey for A/B within one run.

## 7. Fixtures and acceptance

Detached `run_ambient_occlusion.py` (pattern `run_temporal_pass.py`): synthetic R32F device depth
for a sphere on a plane and a 90° corner with an analytic cosine-weighted visibility oracle
(contact-ring annuli within 0.05 mean, far plane and sentinel regions exactly 1, no halo beyond
`R`), a moving-camera sequence through the real resolve proving ripple < 2 % of contrast after 16
frames, Reset mid-chain, capability twins (no R16F, no blending), timing windows. Live
`run_ambient_occlusion_live.py` (pattern `run_linear_distance_fade_live.py`): route + TAA + AO on
synthetic geometry through the real hooks; witnesses: factor 1 bit-exact wherever RT2 is sentinel,
HUD-phase draws unchanged, four same-instance Resets, frame-line metrics. User run: station
approach, capital-ship hull, asteroid field, AO off/on by hotkey with debug-view screenshots and two
capture frames each; accept on contact darkening in struts and creases, no silhouette halo, no
flicker in motion, HUD unchanged, +0.5 ms median.

## 8. Unknowns and bounded briefs

- ~~**Ambient term**~~ **resolved** (`camera-state-and-frame-routine.md` §"Ambient occlusion
  inputs", round 2): `g_LightAmbientIntensity` has no register, no upload and no consumer — its
  handle at descriptor `+0x64` is never read in `0x004c0150`, 0 of the 751 archive programs declare
  any `Ambient` parameter, and the per-node `D3DLIGHT9.Ambient` is stored as zero at `0x004bdbf0`.
  v2 cannot weight by a true ambient share; the D1 estimate above stands.
- ~~**Sun direction**~~ **resolved** (same section): `LightDir_Dir0` is **world space**, recomputed
  per submitted node as `normalize(light+0xb0 − node+0xb0)` at `0x004c234d..0x004c245e`. Proven in
  the capture: the values are bit-identical across run-39 frames 1812/2071/2316 while the camera
  rotates 6.294°, and 615 frame-1812 draws fit one world light position to 0.0003° median residual.
  v2 must rotate it into view space itself and read the register each program's CTAB declares
  (507 of 751 programs declare it, at c4/c1/c22/c5/c0/c19/c7/c21/c18/c39/c13).
- ~~**Per-view near/far regime**~~ **resolved** (same section): gameplay does take the
  `view[0x270] & 0x800000` arm (run-39/40 sector camera `flags270=0x0085492d`), `view[0x360]` has no
  writer and stays 0, and every run-39 capture frame carries `m22 = 1.00000298`,
  `m32 = −6.00001812` (zn 6, zf 2·10⁶). The pass should not use `zn`/`zf` at all: linearize with
  `z_view = m32/(d − m22)` from `projection[10]`/`projection[14]`, already latched at the Clear.
- **View units**: read one known ship's fade-route AABB from a capture to pick `R`. Result (step 2):
  inconclusive. The fade-route AABB is a model-local box under a node scale, so it does not give the
  view-unit size of a known ship; the 0.2 m per view unit calibration of
  `camera-state-and-frame-routine.md` ("Ambient occlusion inputs") stays the working value and the
  radius is a launcher option (`--ao-radius <metres>`, default 2) to be tuned in the run.
- **In-scene HUD**: whether any Z-test-off draw (target boxes, reticle) precedes the scene end; a
  capture query over the `motion_route` draw-state fields (`f4cd0a0`). Depth cannot separate them;
  phase does. If present, v2's mid-scene bracket is required. Result (runs 39/40, all 12 capture
  frames): every Z-test-off draw is either among the first 1-5 draws of the frame, before the first
  scene-bound draw, or after the last main-target draw, where the engine switches to a 640x384 surface;
  no per-draw scene-end index existed in the capture, which is why step 2 logs the
  `scene_end_marker frame=F draw_index=N` line on capture frames (the frame's draw counter at the
  moment the hook sets `hook_scene_end`), so post-hook draws are identifiable by index in the next run.
  The scene-end placement stands; the mid-scene bracket stays a v2 option.

## Step 1 — implemented

Detached chain (2026-09-14): `src/renderer/ambient_occlusion_pass.{h,cpp}` (attach / prepare / execute /
before_reset / after_reset / detach, native slots, one owned `D3DSBT_ALL` block, gates in
`ambient_occlusion_caps.h`: ps/vs 3.0, conservative slot count ≤ `MaxPixelShader30InstructionSlots` (GTAO
program 483 of 512), R32F/R16F render targets, post-pixel-shader blending on the owning format, ZERO/SRCCOLOR
factors), programs `src/temporal/ao_{linearize,gtao,blur,apply}_ps.hlsl` compiled by
`tools/shaders/generate_rigid_motion_pixel.py`. Linked into the DLL, referenced by nothing yet. `prepare`
allocates the three half-resolution targets on the first frame and on a resolution change (rollback on a
partial failure); `execute` calls it. Radius is in metres (`radius_metres`, default 2) times
`units_per_metre` = 5 (`camera-state-and-frame-routine.md`, "Ambient occlusion inputs").

Three deviations from section 2, all choices made for the fixture's exact identities: (1) horizons are each
tap's elevation above the reconstructed tangent plane, not the tap's angle from the view vector (the float64
reference with XeGTAO's horizon on the fronto-parallel plane at 1280×768 reads mean 0.9969, minimum 0.8623 at
the borders, because texel-quantized taps leave the slice plane; `test_ambient_occlusion_reference.py`
records it); (2) each slice's arc is normalized by its unoccluded value `cos n + n sin n` and the slices are
averaged with the projected-normal weight, so a surface tilted by `n` in a slice loses the same fraction of
that slice for the same horizon as a fronto-parallel one (XeGTAO leaves the tilted slice above 1 and clamps
after averaging, under-counting partial occlusion there); (3) the R16F term stores occlusion `1 − visibility`,
so an unoccluded pixel is exactly 0 (the fixture's `FP16_STORE` probe shows this backend truncates FP16
render-target stores, which turned `1 − ε` into a lost ulp per pass).

Fixture `verification/probe/run_ambient_occlusion.py` (`verification/results/bottle-X3/ambient-occlusion-gpu1.json`,
99 checks): the float64 reference is a transliteration of the shaders (same taps; a fidelity check, max
difference 3.7·10⁻⁴ = fp16 quantization on the five scenes); the analytic oracles are the independent check
(fronto-parallel plane exactly 1; tilted plane 89 of 121,600 pixels below 1, none below 0.999, from the R32F
device-depth quantization; sentinel term 1 and target bit-identical; contact ring 0.957 mean; crease 0.92;
step far side 0.82; no halo beyond 1.3 R). The multiply law holds within one FP16 ulp: exact on the plane
under both rounding models, 100 % under round-to-nearest and 99.95 % under truncation on the tilted plane,
99.1 % / 99.9 % on the sphere, so the blend's product precision is not the store's and neither model is exact
everywhere. RT1/RT2, the auto depth surface and four vertex-sampler textures bound before every chain come
back by pointer (`D3DSBT_ALL` carries the vertex samplers). Fault ladder and Reset bit-identical.

Cost (EVENT-fenced, 7 pairs): 1280×768 submit 0.11 ms + 0.72 ms to GPU completion = 0.83 ms chain, over the
0.8 ms cap; 1920×1080 1.35 ms. Per-quad fenced in isolation (each includes its own render-pass flush and
completion): 768p linearize 0.35, gtao 0.46, blur 0.33 + 0.33, apply 0.47 (sum 1.94, so the chain overlaps
them); 1080p 0.22, 0.46, 0.20 + 0.20, 0.34. Without the two blurs the chain is 0.81 ms at 768p and 0.66 ms at
1080p. The numbers do not isolate one cause; the cheapest safe reduction they support is fewer passes: fold the
linearization into the GTAO quad (drops one pass and the half R32F target) and replace the two blurs by one
2D 5×5 depth-aware blur. Not tuned in step 1; the scene-end hook (step 2) waits on the run-14 query and this cost.

## Step 1b — cost reduction

Four quads instead of five (2026-09-14): one 2D depth-aware blur (sparse 5x5 quincunx, centre 4,
diagonals 2, axial +-2 weight 1) replaces the two separable 5-tap passes, the linearize quad stores the
scale-free depth `zs = z / |m32| = 1 / (m22 - d)` (the caller divides the radius and the falloff by
`|m32|`; every other test is relative), and the horizon search takes its half-pixel view ray, texel-centre
offset and last texel from folded constants in one `SetPixelShaderConstantF` of c0..c7. GTAO is 397 of
512 slots (was 483), linearize 8, blur 93, apply 70. Each slice now accumulates its *missing* arc
`cos n / 2 - (a0 - a1) sin n / 2 + (cos(2h0 - n) + cos(2h1 - n)) / 4`, which is exactly 0 in float when both
sides are unoccluded, so the flat-plane identity no longer depends on a cancellation.
Folding the linearization into the horizon search (the reduction this note proposed) was implemented and
measured first: it moves ~35 depth taps per half pixel to the full-resolution R32F and cost more than the
pass it saved (1280x768 chain 1.10-1.18 ms against step 1's 0.83; 1920x1080 improved to 1.07), so the
pass stays. Chain now: 1280x768 1.06 ms in the first timed block and 0.51 ms (floor 0.50) in a repeat
block at the end of the same run, 1920x1080 1.33 ms (floor 1.23); per-quad in the warm block linearize
0.20, gtao 0.30, blur 0.19, apply 0.30, each including its own ~0.2 ms fence and flush. The block-to-block
spread of this backend is larger than the reduction, so the fixture now measures 1280x768 twice and
reports both with the cheapest window of each block. Fidelity and oracles held: reference maximum
4.9e-4, contact ring 0.9567 (was 0.9569), crease 0.9199, step far side 0.8215 (was 0.82). The quincunx
offsets cover 7 of the 16 residue cells (mod 4) of the horizon search's 4x4 Bayer pattern, so a tap set
sees 7 of its 16 values where the separable pair saw all 16; that is not an averaging argument, the
justification is the measurement above (oracle means moved by at most 0.002).

## Step 2 — implemented

Live placement (2026-09-14). `MotionOutput::scene_end_hook` (`src/proxy/motion_output.cpp`) runs
`run_ambient_occlusion()` right after the lazy-binding flush (`restore_bindings`) and the
`hook_scene_end` latch, before `resolve_hdr` / `resolve` (`taa_->run`), so both routes' resolves
consume the darkened target: on the 8-bit route the owning target is the latched main surface, on
the FP16 route the HDR target while it is RT0 (`hdr_state_ == Active`), attach keyed on that format
(A8R8G8B8/X8R8G8B8 or A16B16G16R16F; re-attached when it changes). The pass, its targets and the
timing queries are created and released under `taa_call`, so `taa_references` accounts for them;
`before_reset` releases them after RT1/RT2, `after_reset(S_OK)` re-arms, `release_resources`
detaches. Gates per frame (`reason=`): `taa`, `state_lost`, `msaa`, `no_depth`, `recording`,
`queries`, `camera`, `target`, `attach` (the pass's capability reason on the one-time
`ambient_occlusion_device` line), `reset_pending`, `depth_container`, `failed` (the pass restored its
block and published nothing; up to eight `ambient_occlusion_failed` lines). Inputs: RT2's texture,
`camera_scene_.m00/m11/m20/m21` from the latch, the default scratch `m22 = 1.000003`,
`m32 = -6.0000184`, `jitter_index` from the frame counters, radius in metres times 5 units/m.
Launcher: `--ambient-occlusion` (requires `--motion-output --taa`; `X3M_AMBIENT_OCCLUSION=1`),
`--ao-radius <metres>` (0.1..100, default 2, `X3M_AO_RADIUS`), `--ao-strength` (0..1, default 0.5,
`X3M_AO_STRENGTH`), `--ao-debug` (`X3M_AO_DEBUG=1`: the apply quad writes the factor as grayscale,
blend off; implies timing), `--ao-timing` (`X3M_AO_TIMING=1`). Every switch is written explicitly
so an inherited value cannot enable it; default off.

Timing (`--ao-timing`/`--ao-debug`): one line per frame,
`ambient_occlusion_frame device= frame= attached= ran= reason= gpu_us= cpu_us= width= height= radius_px= gpu_frame= applied= result= restore= stage= debug=`.
`gpu_us` comes from documented D3D9 timestamp queries (`TIMESTAMPDISJOINT` begin/end around
`TIMESTAMP` end pairs, scaled by `TIMESTAMPFREQ`; two rotating sets polled with `D3DGETDATA_FLUSH`,
never blocking, so the value is the most recently completed pair and `gpu_frame` names its frame);
a `CreateQuery` refusal fails closed to CPU wall time (`ambient_occlusion_timing queries=unavailable`,
`gpu_us=-1`). A poll that fails (a lost device) releases the sets before anything is issued again
(`gpu_timing=lost`, one `queries=lost` line; CPU time until Reset). `cpu_us` is the QPC wall time of
the `execute` call. `radius_px` is the half-resolution screen radius at 20 m, capped at 64. The line
also carries `enabled=` (the hotkey state), `source=hook|copy` and
`gpu_timing=queries|unavailable|lost|pending`. Outside timing mode the per-frame cost is the chain
itself: one `GetRenderTarget`, one `GetContainer`, the four quads and the block capture/restore; no
per-draw work. Timestamp queries return `D3DERR_NOTAVAILABLE` on CrossOver Preview's D3D9, so the
frame line carries CPU wall time there; the GPU cost is bounded by the detached fixture's
event-query fencing (below) and measured in game by an off/on A/B on the same flight, which the
hotkey makes a same-run comparison.

Hotkey: **Ctrl+Shift+F11** (comparison-hotkeys.md; F9 exposure, F10 bloom) flips a per-frame
enable read at the scene end while `--ambient-occlusion` is on. The pass stays attached (no
re-attach cost); a disabled frame logs `reason=disabled enabled=0`; each press logs
`ambient_occlusion_toggle device= frame= enabled=`. The chord uses the comparison sampler (fresh
press, Ctrl+Shift armed in the previous foreground sample) and is polled only when the option is on;
it has no on-screen notice. Failure policy: `ao_failure_limit` (3) consecutive chain failures refuse
the device until Reset (`reason=failed_limit`); a target format that alternates with the redirect
state re-attaches at most once per 60 frames.

Live fixture `verification/probe/run_ambient_occlusion_live.py` (`aohook` script of
`motion_output_fixture.cpp`, 64x64, fixture-seam DLL, record
`verification/results/bottle-X3/ambient-occlusion-live1.json`). Scene: the flat frames of the hook
script (fronto-parallel planes: the factor is the exact identity, and the main target after the hook
equals the reference resolve byte for byte, i.e. the AO-off image) and a crease frame (object A drawn
twice with perspective terms p = +-1.5 so the Z test keeps the nearer plane on each side of ox = 0:
a concave crease with slope about 1.2, sentinel columns beyond |ndc x| > 2/3). On the frames without
history (the first after each Reset) the presented image is the multiplied scene, checked per pixel
against the main target read before the hook: sentinel pixels bit-identical, every channel within
[floor(b (1-s)^(1/2.2)) - 1, b], darkening present and concentrated in the centre band. Twins: ao-on
(1097/1148 pixels darkened on the two law frames, maximum drop 15/17 codes, centre-band mean 5.3/5.0
codes against 0 in the outer band, 1408 sentinel pixels unchanged, 8/8 frames `ran=1 applied=1`),
ao-off (bit-identical, no AO lines), ao-fault (`X3M_FIXTURE_AO_FAULT=attach` lowers the shader model:
`ambient_occlusion_device attached=0 reason=ps_3_0`, all frames `reason=attach`, crease frames
bit-identical), ao-debug (gray factor, sentinel 255, above the floor, 1360/1369 pixels below 255,
maximum 27/31 codes), ao-hdr (attach on format 113, identity on the flat frames, 8/8 ran), ao-toggle
(the export behind Ctrl+Shift+F11 flips the flag: frames 3-4 flat and byte-exact with the reference
resolve at `reason=disabled`, frames 5-6 crease darkened again, two toggle lines, one attach),
ao-pollfault (`X3M_FIXTURE_AO_FAULT=poll`: a timestamp set that polls as a lost device is released
before anything is issued; the chain still runs, `gpu_timing=lost`). Flat frame 1 of every non-debug
twin signals before the scene, so its chain runs at the bloom copy (`source=copy`). Two Resets per
twin; the fixture's state snapshot around the hook shows no difference in any frame. Fixture cost
at 64x64 on this backend: median `cpu_us` 292-652 per twin, minimum 116-184 (the frame after an
attach or Reset pays the target/block creation, 10-15 ms). The game-size cost is the detached
fixture's (below).

Detached fixture rerun after the debug-view flag (step 1b chain unchanged otherwise): 112 checks, 0
failures; 1280x768 0.78 ms first block / 0.59 ms repeat (floor 0.51), 1920x1080 1.39 ms.
