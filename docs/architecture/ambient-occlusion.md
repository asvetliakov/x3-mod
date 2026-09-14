# Screen-space ambient occlusion on the linear material path

Design note for goal 7 (GTAO/SSAO), written 2026-09-14 at `4ce8fc8` for ratification. Not
implemented. Roadmap row 7 gate: stable depth interpretation, temporal filtering, correct occlusion
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
If a run objects, v2 moves the chain to a mid-scene bracket at the first blended draw.

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

- **Ambient term**: `g_LightAmbientIntensity` exists as an effect parameter
  (`effects-and-archives.md`) with no documented consumer or register. `disassemble`: which programs
  read it, its upload site in `0x004c0150`, captured values. Settles whether v2 can weight AO by the
  true ambient share instead of the D1 estimate above.
- **Sun direction**: `LightDir_Dir0` sits at c4 (7 programs), c5 (6), c0 (3), c13 (1) in
  `shader-registers.json`; its space (world or view) and update site (`0x004bdda0`) are unknown.
  Settles the v2 sun-weighted blend `lerp(ao, 1, sunlit share)` with the reconstructed normal, the
  directional-occlusion variant.
- **Per-view near/far regime**: when `view[0x270] & 0x800000` holds in gameplay and the value of
  `view[0x360]`; a Clear-hook capture of the globals or a bounded disassembly of the writers of
  `+0x360`. Settles the linearization constants (else the defaults).
- **View units**: read one known ship's fade-route AABB from a capture to pick `R`.
- **In-scene HUD**: whether any Z-test-off draw (target boxes, reticle) precedes the scene end; a
  capture query over the `motion_route` draw-state fields (`f4cd0a0`). Depth cannot separate them;
  phase does. If present, v2's mid-scene bracket is required.

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
