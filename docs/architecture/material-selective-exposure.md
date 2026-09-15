# Material base at EV0: single-scene implementation contract

**Core contract ratified by the parent — 2026-09-15.** This owns selective material exposure
and replaces the earlier paired-scene proposal. The parent ratified one compensated scene,
background-driven Auto, one exposed-linear TAA history stored as D/4, the exposed-destination
packed-screen law, explicit Q/Z fallback and bounded finite domain. Shader/ABI, finite-storage,
recovery and GPU evidence below remain open; detailed implementation changes not covered by
this contract remain **pending parent ratification**. No production code, build, Wine, game
or commit was performed for this design. Ordinary mode and the accepted Auto/fill defaults
retain their existing behavior.

## 1. Recommendation and intentional behavior

Use **one compensated scene and one TAA history**. Transform diffuse/point/fill source terms
before they mix with specular/emission. Meter the completed, uncompensated background at the
existing depth-only Clear that separates background and scene. Resolve in exposed linear light;
AgX/bloom consume that domain with no second application of scene exposure.

```
e = exp2(frame-latched scene EV)
B = albedo-weighted directional diffuse + point diffuse + fill
H = directional specular + reflection + material emission + lightmap/other emission
Q = B/e + H                         stored compatibility-encoded in the existing scene
D = e*Q = B + e*H                   exposed linear input to TAA / bloom / AgX
```

The user-facing mode is separate from Auto/manual exposure. Keep ordinary exposure mode
unchanged and retain **Auto ceiling +1.0 EV / fill .03**. Initial selective range is the
selected existing Auto range (default minimum -3, maximum +1) and manual EV0. A mode switch
at a closed frame boundary clears pending meter results, resets exposure to the defined
startup EV0 and invalidates/reseeds the single history. It need not preserve instant ordinary
same-frame rollback or the prior mode's Auto statistic/history.

Scope includes the current converted hull, asteroid and **opaque SM3 glass** families and
their admitted cutout/source-over routes, including XT DEFAULT/BUMPMAP/LOW. Unconverted
translucent material routes retain the converter's existing limitation. This is a real
selective exposure feature for those material contracts, not a full-object exemption or a
universal claim about all game writers. A valid depth sample is not a hull classification.

Deliberate new-mode differences to report:

- Auto measures background-stage content, not hull highlights, nearby point-lit surfaces or
  foreground explosions. Foreground effects still receive e; they do not set e.
- TAA operates in exposed linear light, rather than ordinary mode's engine-encoded history.
- Packed-screen effects evaluate their current nonlinear response against the exposed
  destination and scale the resulting effect delta by e.
- Mid-frame failures retain safe existing recovery; they cannot reconstruct an ordinary
  image after compensation has already been drawn.

No further user permission is inherently needed for these parent-authorized decisions.

## 2. Named source and RE facts

- `linear_material.cpp::transform`, `point_site`, `emissive_site`, `pixel_sites`: point RGB
  is decoded and gained separately before VS point accumulation, then added to separately
  sanitized/scaled material emissive. Full-precision COLOR1 currently carries **P+M**.
  Scaling that varying would exempt material emission incorrectly.
- `scene-linear-materials.md`, `linear-bump-materials.md`, `linear-hull-materials.md` and RE
  `remaining-hull-materials.md`: directional diffuse and specular mix **before** albedo.
  Shared half coefficients also serve reflection, and Split packs diffuse/angular values
  into scalar lanes before saturation. Change the proved diffuse consumer, not the shared
  DEF or the whole light-color vector. Terran fuses diffuse/specular in MAD instructions.
- `fill-light.md`: fill enters as one MAD using decoded LightDir_Color0 before albedo; it
  belongs in B even though it shares the sun color. Default .03 is retained.
- `xt-materials.md`, `linear_xt_material_inc.h`: separate DiffuseStrength and
  SpecularStrength, then branch-dependent Tint and occlusion. Base diffuse compensation
  follows those same multipliers. Detail alpha affects specular; lightmap and Terraformer's
  final occlusion-texture RGB are emission, after occlusion, and stay H. Preserve repaired
  DEFAULT ordinary/linear pair transport and all branch predicates.
- `glass-materials.md`: six SM3 opaque pairs have albedo-weighted diffuse/point/specular,
  plus Fresnel cube. Only the diffuse/point/fill terms get compensation. The other 24 glass
  pairs are not current converted coverage. The filename is not an opacity contract.
- `linear-station-source-over.md`: `4944d81dfe531b37/64bac8bb307eb896` uses the existing
  distance-fade bracket; source-over is a real station population, not an optional theory.
- `linear_sun_share_inc.h::sun_plan` propagates directional-color seeds, including that
  light's specular. Its scalar luminance fraction is not B and omits point/fill.
- RE `material-color-inputs.md`, submission `0x004c0150`: material emissive is already
  strength-scaled and differs from uploaded light RGB. No new physical units are inferred.
- RE `emission-draw-order.md`: bucket-interleaved deferred drawing prevents assuming one
  final additive-only phase. Preserve composition at the original draw position.

Current code uses COLOR1 for material RGB; the early DEFAULT note's TEXCOORD6 plan is
superseded. This recommendation needs **no new varying or packed depth lanes**: the earlier
depth.zw/COLOR1.w idea was needed to retain two simultaneous radiance evaluations, not here.

## 3. Exposure and background-meter sequencing: source proof

`MotionOutput::begin_redirect` runs before the latching initial Clear. It binds HDR, then
calls `HdrPass::begin_frame`, which consumes the previous queued meter, steps exposure once,
advances the ring and calls `prepare_constants`. Material draws occur later. `write_back`
queues a meter but never steps exposure; bloom receives a retained display snapshot. Existing
comparison changes require a closed boundary and invalidate history.

`src/renderer/scene_boundary.h::SceneBoundarySelector::advance` provides the smaller meter
boundary directly:

1. Initial color+depth Clear latches the main target and enters **Background**.
2. Background draws occur on that latched pair.
3. The first successful full **depth-only Clear**, after at least one background draw, enters
   **Scene**. Another depth epoch in Scene is rejected. This Clear preserves background RGB.

Queue the new-mode meter immediately **after successful step 3**, before any following scene
draw, using the existing HDR texture and reduction chain. Use the same state-save/restore
contract as the existing meter but do not perform a display writeback at this boundary.
Sampling the source while it is bound as a target is forbidden: detach it while reducing,
then restore its exact bindings/viewport/depth/application state. A meter-only entry point
needs a complete owned state bracket; exposing `meter_chain` naked is insufficient.

At the next initial latch, consume this meter using existing dt/deadband/adaptation. Selective
writeback must **not enqueue a second, foreground-contaminated meter**. Ordinary mode keeps
its present post-resolve meter timing and algorithms. Keep the same tile/key/p99/edge-weight
parameters for selective background statistics, including neutral handling of sparse sky.

Latch `{mode, frame, generation, e, inv_e, finite_limits}` after `begin_frame`, and use that
immutable packet for VS, PS, composition, TAA and unresolved writeback. Repeated flushes must
not re-adapt. A mode change, Reset, scene ownership failure or exposure-token mismatch cannot
switch units midway through a submitted scene.

### Feedback stability and missing-boundary policy

The background is rendered before **any** selective producer is admitted, and is never
compensated. Thus its queued source `S_bg` is independent of e. For a static background,
existing space-aware target `T(S_bg)` is constant. Outside clamps/deadband changes, adaptation
is `EV_next=(1-a)*EV+a*T`, with `0<a<=1`; its error contracts by `1-a`, without the B/e feedback
of metering the compensated scene. Ring latency does not change a constant target.

This proves absence of the newly introduced material feedback, not camera-invariant Auto.
Looking from a bright planet to sparse space can still change the statistic; the existing
1% lit-tile gate may move the target toward neutral. `space-exposure-policy.md` already warns
that sky color is not incident illumination and that dim nebulae often request the ceiling.
Those are explicit properties of this new background-driven aesthetic, not hidden physics.

- No successful phase boundary, failed chain/readback, invalid ownership or unavailable
  meter: **hold the last valid e**; on startup that is EV0. Never meter completed Q as fallback.
- Clear any pending new-mode meter if its owning frame/boundary is invalidated. Label the
  held value and skip reason. Require a new valid boundary before queuing again.
- Arm selective material draws only in the positively established Scene phase. Missing
  boundaries therefore do not create an unmetered guessed scene phase. A clean meter failure
  after a valid boundary may still use the already latched held e.
- A failed state restore is a rendering-state failure, not a harmless missing statistic;
  follow the existing state-loss/block/recheck ladder.

No new full-resolution metering image, coverage mask or special depth classification is needed.

## 4. Exact shader compensation and finite domain

### Placement

Create separate cached selective combined variants from the same immutable originals.
Do not chain an already transformed stream into the original-hash motion transformer.

**Point:** for loop VS, multiply the completed point RGB accumulator by inv_e immediately
before its existing material-emissive ADD. All source sanitation, decode, gains, native light
response/attenuation and point accumulation stay ahead of this multiply. For fixed-light VS,
scale the proved point RGB factor after its input treatment and before the final point-response
MAD that adds M, retaining the original attenuation and M operand. Never scale COLOR1=P+M,
M, the material-emissive gain, an angular clamp or a shared light constant.

**Directional diffuse:** identify each actual diffuse scalar contribution after its native
angular saturation and diffuse-strength calculation, before it is combined with specular.
Multiply that contribution by inv_e. Fused forms need a proved split/replacement instruction
with original specular factors untouched. Do not scale the final accumulated lobe, decoded
r12/r13 light colors, or shared half-strength DEFs that also feed cube reflection.

**Fill:** use effective `fill*inv_e` only at the fill insertion; direct gain and sector tint
remain unchanged. The final albedo/palette/occlusion products naturally carry the compensation.
Material emission, directional specular, reflections, lightmaps and Terraformer additive RGB
remain unscaled in Q and acquire e at display conversion.

This is exact source-role compensation in real arithmetic. No final-pixel mask, guessed
fraction, CPU emissive upload, new point loop, RGB subtraction or new varying is needed.
Interpolation commutes with the per-frame scalar for each already independent point term;
original alpha and interpolation semantics are unchanged, including FLAT. Do not import the
paired-route's unnecessary GOURAUD-only restriction.

### Constants and hot path

Proposed runtime reservations: VS c250.x=inv_e; PS c222 contains inv_e and the frame's output/
scratch scale values. Prove absence in every selected original and combined form before use.
VS material DEF c248–249, motion c252–255, PS material/fill/fade c212–215, motion/depth
c216–220 and sun c221 retain their meanings.

Extend the existing VS c252–255 upload/restore range to c250–255 and PS c216–217 to the
needed c216–222 range, including complete application-value shadowing, successful-setter
tracking, state-block resync, Reset and rollback. Intermediate registers in an expanded
upload must carry known preserved application values; shader-local DEFs do not excuse
corrupting application state. Use separate old/new transactions if a profile does not qualify,
but do not add per-draw COM getters. The existing shadow currently tracks only the old ranges.
One existing setter per stage can carry the expanded block. No new per-draw allocation, lock,
bytecode scan or CPU pow. Shader creation and proof remain create-time work.

Respect ps_3_0's single distinct c# read port in every new instruction, including fill and
sanitizer code; stage one constant in a temporary when needed. The constant-port repair is a
mandatory affected regression, not just a documentation check.

### Clipping: do not distribute through a clamp

Let C=65504, the current final material linear ceiling. The exact appearance identity is
qualified where original independent terms are finite/nonnegative, native angular input
conditions hold, and the **uncompensated final sum has not hit its cap**. Input-color clamps,
normal/data saturations and emissive sanitation stay at their original locations. Scaling
before one of those nonlinear operations is not interchangeable with scaling its result.

Early seed compensation changes the sum presented to the final sanitizer. Keep all input
sanitizers unchanged, but use a selective **output** ceiling
`C_Q=C/min(e,1)` for Q. Otherwise a valid base C at e=1/8 would be clipped to C before display
and incorrectly appear at C/8. Under the selected e<=2 and original unclipped domain,
`Q<=C_Q` and `D=eQ<=C*max(e,1)<=131008`. C_Q is float32; it need not fit linear FP16 because
the opaque scene stores its compatibility encoding. Finite ordered sanitation, safe powers,
zero signs and the adjusted ceiling require emitted-bytecode fixtures.

For unclipped original inputs these ceilings avoid adding compensation-induced clipping.
For original saturated/invalid/extreme inputs retain an explicit finite storage fallback;
**do not claim the exact base/highlight identity there**, since component attribution after
an original whole-sum cap is not uniquely determined. Fixtures must distinguish that domain
from ordinary in-range cases. Runtime e outside the qualified range disables selective mode
at a safe frame boundary; do not silently clamp only the compensation factor.

A user-requested non-default HDR clamp is also a nonlinear policy. Initially require the
current clamp-off configuration for selective mode, or specify and qualify its new exposed-
radiance meaning separately. Do not apply an unchanged pre-exposure clamp to Q and claim
base EV0. Ordinary mode's clamp behavior remains unchanged.

## 5. Every writer keeps an explicit composition meaning

The scene remains **compatibility-encoded Q**, not a globally linear target. Existing writer
order, rasterization, alpha/depth/cutout behavior, coverage and native recovery stay in force.

| Writer | Selective-mode contract |
|---|---|
| Background phase, nebula/sky | Unchanged, before compensation; meters the new Auto policy and receives e at display |
| Converted opaque hull/asteroid/glass/XT | Source Q from exact compensated VS+PS; native alpha, RT1 motion and RT2 depth remain |
| Admitted alpha-test cutout | Same oC0 alpha/comparator and same accepted samples across MRT; Q affects RGB only |
| Converted station/asteroid source-over fade | Emit linear Q into the existing ordered source-over bracket with the same source alpha; `a*Q+(1-a)*Qdst` gives correct fixed-base transmission |
| Converted additive emission | Add the same unexposed source emission to Q at its existing position; display applies e once |
| Qualified packed screen | Exposed-destination law below; same source draw and existing four-MRT pool |
| Unconverted/native opaque material | Remains ordinary-exposed native source under the converter's existing coverage boundary; no inferred base exemption |
| Unconverted source-over/translucent glass, native/refused screen/additive effects | Retain the current native fallback. Their encoded-domain interactions with compensated underlying surfaces are **not an exact layer-separation guarantee**; report this coverage limitation |
| Scene-color reads/copies or unknown content mutations | Preserve the logical Q image and its domain wherever ownership is proved; otherwise end/block enhancement under the existing ownership ladder, with no asserted ordinary reconstruction |

The last translucent row is a real limit: decoding an encoded source-over/additive result is
nonlinear, so native effects over compensated hulls can alter the apparent fixed-base result.
Do not hide it behind a material draw percentage. Counters must distinguish selective material
coverage, converted composition and native/refused overlap possibilities. Acceptance must
exercise the user's visible effects; if required effects fall into that row and visibly break
the requested appearance, extend those exact source/operator contracts before calling the
feature accepted. This is the converter's explicit existing coverage limit, not a reason to
build a second scene for every unknown program in advance.

### Existing scratch storage must be normalized

The ordinary opaque output is encoded, but the fade E scratch is **linear FP16**. It cannot
store Q above C. For selective fade, emit `min(e,1)*Q` into E and accumulate the usual coverage
q. At composition restore the accumulated RGB by dividing by that same frame scale before
adding `(1-q)*decode(A)`. This avoids Q overflow at negative EV with no new texture or draw.
Preserve exact q==0 raw-A copy and q==1 background independence. Normalize only E RGB, never
source alpha/coverage, and preserve the dual native output's original program/alpha.

Audit every existing composition sanitizer: final writes returning Q use C_Q; per-emission
input caps remain their source policy. Any scratch storing decoded destination must use an
explicit reversible scale. Do not raise an FP16 linear cap above representable range. All
scale constants come from the immutable frame packet, including recovery/publication paths.

### Packed screen: parent-ratified specialization

For the current native screen source S, define the existing nonlinear operator's delta:

```
D = e*decode(A_Q)
N = decode(native_screen(encode(D), S))
delta(D,S) = gain * (N - D)
D_out = D + e*delta(D,S)
Q_out = D_out/e
```

At e=1 this is the existing packed law. For a pure-base destination D=B, B is independent of
e and the screen delta receives e. With specular/emission already in D, screen's interaction
with that exposed destination is intentionally nonlinear; do not promise additive independent
H decomposition for an inherently destination-dependent effect.

Implement this by specializing the existing plane-init and composite quads while preserving
**exact native recovery against Q**. For each channel plane, retain red as the native-after
value seeded from raw encoded A_Q; use green for a second native-after value seeded from
**encode(D)**; keep blue as the existing accumulated modified flag. Enable green writes for
the existing producer, whose per-plane RGB already carries the same source channel and alpha.
The same geometry submission then advances both native-after values with identical factors.
The backup B already retains A_Q: recompute D=e*decode(B) in the composite rather than store
D linearly in a plane. Decode the green native-after value for N, form D_out/e in float32,
and return compatibility-encoded Q with its explicit finite policy. Red retains the exact
native-over-Q source result for the existing recovery assembler; M retains native alpha.

This layout is a required recovery-preserving proposal whose source/mask proof remains open.
Seeding red with encode(D) instead would destroy native-over-Q recovery. Reconstructing that
native result later from accumulated transmittance would not recover every intermediate
FP16 blend rounding, so it is not a substitute. Zero-modified channels copy raw A_Q. No extra
MRT, full-size image, geometry replay or state-capture bracket is needed. At e=1 select the
old plane-init/composite and old green mask exactly to preserve the existing storage/arithmetic
schedule; equality of the real-number equation alone does not prove old-law GPU parity.

Failed screen/fade publication must still recover the current native result relative to the
Q scene, not a supposed all-ordinary frame. A failed specialized plane-init before the source
is a clean native-source fallback; later failure follows the certified source-once ladder.

## 6. One display-linear TAA history, including finite storage

Modify the **current-color reads** of a selective resolve shader to decode Q and apply e
before temporal statistics, including center, all nine neighborhood taps and every current-
only/invalid-history return. History taps are already in the new exposed-linear storage
space and must not be decoded or multiplied by the new e again. Reactive snapshot branches
and alpha copying remain unchanged. Keep the same motion/depth/reprojection and mask policies.

A small fixed storage scale avoids adding a full-resolution FP32 image or narrowing the
existing high-radiance domain:

```
Z = D/4 = (e/4)*decode(Q)
current and history: linear Z, stored in the existing FP16 history pair
TAA luminance weighting k = 4       (so k*lum(Z) = lum(D))
AgX/bloom input: decode none, storage reconstruction multiplier 4
```

This is exposed linear light in fixed units, **not another scene exposure**. The factor 4 is
frame-invariant; changing Auto e does not rescale history. From the material domain above,
Z<=32752, safely below `resolve.h::prepare`'s existing **65000** validation ceiling (do not
raise that guard). Using D directly can exceed FP16, and D/2 can exceed that 65000 guard near
the existing source ceiling. Source/composition overflow remains subject to the finite-domain
contract; the temporal variance/unweight path retains its existing bounded failure handling.
Fixed scaling modestly changes subnormal/underflow limits; include tiny emissive fixtures.

Keep one `TemporalPass` history allocation and an explicit history-domain tag. Toggle, Reset,
resize, camera cut and lost recovery invalidate it. New ordinary frames cannot sample old Z
as engine RGB. EV changes alone leave a stationary B-only source Z constant; H evolves with
scene exposure and receives ordinary temporal rejection/clipping for actual lighting changes.

All successful selective display consumers use `{decode=none, multiplier=4}` for Z, including
bloom extraction, its zero-strength candidate, no-bloom AgX and RCAS staging. Snapshot/logging
must distinguish **scene EV** from **storage reconstruction**. Do not overwrite scene EV with
+2 simply because the stored-image multiplier is 4. The bloom API already accepts none decode
and a positive multiplier; its retained source snapshot must name the actual resolved domain.

If resolve fails cleanly, the existing unresolved HDR source is still encoded Q: write it
through gamma decode, **scene multiplier e**, and C_Q. This current-only fallback can preserve
the requested appearance without TAA for that failed frame, as existing TAA recovery does;
invalidate history and retry. Do not use the Z/unity-or-storage snapshot on Q. Retain the
existing refusal to publish a resolved bloom candidate when the required resolve failed.
If AgX/shader restoration fails, the current safe identity/StretchRect ladder can display Q
without the intended transformation for that recovery frame. Record degradation and disable/
recheck as today; it is not ordinary-exposure equivalence. Preflight readiness before enabling
material compensation, but do not assert infallible post-submission presentation.

This folded resolve adds gamma decode and e/4 multiplication to current reads only, not to
history taps; it adds no full-size image, texture fetch or second temporal resolve. Prove
SM3 slots after compilation and measure this ALU cost. If decoding each neighborhood tap is
more expensive than a single conversion pass, a one-image preconversion (8 B/pixel, 15.8 MiB
at 1080p) is an optional measured tradeoff, not a prerequisite or a second history.

## 7. Failure scope, sun lane and Windows

A clean pre-draw selective bind failure restores every changed shader/constant/target state
and selects the existing ordinary material/motion fallback for that draw. Report incomplete
selective coverage; once earlier Q draws exist, this does not produce a wholly ordinary
frame. A persistent failure schedules mode disable at the next safe frame boundary and
invalidates history. State-loss/failed restoration must block enhanced routing and use the
existing explicit recovery ladder, not continue with guessed constants. No replay of
already-submitted geometry, new engine hooks or calling-ABI patch is required by this design.

Keep RT1 and RT2.r intact. The separately prepared sun runtime's .g fraction was qualified
against ordinary L; changing diffuse seeds changes its meaning. For the first selective
integration, write .g=-1 and publish a clear `selective_exposure_unqualified` reason while
retaining depth; ordinary-mode sun behavior remains. A later joint proof can define/extract
`sun_Q/Q` (diffuse/e plus ordinary sun specular) in the compensated domain, but it must not
silently label that value the old ordinary fraction. No shadows currently consume the lane;
there is no need to retain a twin scene for a future shadow policy. The parent ratified this
**temporary** invalidation only. It must be replaced by jointly qualified Q-domain extraction
before shadows acceptance: shadows must work with the user-selected selective exposure mode,
not require turning that mode off.

No new MRT count or format is required for material compensation. Existing screen policy
already needs four MRTs; materials retain their current temporal tuple. Native Windows uses
the same documented D3D9 shader/constants/target/stateblock/Reset APIs, with current format,
MRT post-pixel blending, masks, alpha-test and no-MSAA gates. Microsoft's
[MRT contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets)
and [SM3 register contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-3-0)
remain the platform basis. No Wine layout/export or DLL hash is a prerequisite. Native Windows
runtime is unverified; parent records that gap in `platform-portability.md`. Compatibility
source/cross-compilation is not native runtime acceptance.

## 8. Practical cost and rejected alternatives

New persistent image storage: **zero** for the selected folded-resolve design. Keep existing
scene, TAA history, meter chain and composition pools. New costs are approximately one point
scaling MUL per vertex, a small number of diffuse/fill scalar operations per pixel, adjusted
finite-limit/encode handling, current-read gamma conversion inside TAA, and extra ALU in
existing screen/fade quads. Exact shader slots, load time and GPU time are not yet measured.
Expanded constant blocks add at most two VS and five PS registers to existing setter calls;
there is no new per-draw COM query, allocation or lock. Move, rather than duplicate, the Auto
meter chain; a separate state bracket occurs once per frame at the background boundary.

- **Paired L/Q scenes plus two histories:** preserves ordinary same-frame image and identical
  metering, but those are not user requirements. The earlier minimum 63.3 MiB extra color
  storage at 1080p, extra resolve and all-writer pairing are rejected as disproportionate.
- **Meter completed Q with unchanged Auto controller:** rejected; it creates exposure feedback
  from B/e and can drive a pure-base frame toward a clamp despite an invariant displayed B.
- **Final scalar sun/depth mask:** rejected; cannot distinguish colored diffuse, point, fill,
  specular, reflections and emission.
- **RGB base extraction/twin varying:** valuable as an independent numerical oracle, but
  unnecessary production work when scalar compensation is inserted at proved source seeds.
- **History in Q with no domain change:** rejected; a fixed base stored as B/e changes with
  Auto history and can pump/lag. Exposed-linear Z avoids this.

## 9. First concrete implementation milestone and exact ownership

**Milestone 1: complete create-time selective material variants plus independent source
oracles**, before any live feature enablement. This is implementable without new rendering
resources or the uncertain packed varying design.

Files: `src/renderer/linear_material.{cpp,h}`, `linear_xt_material_inc.h`, and a bounded new
`material_exposure_*_inc.h` profile/helper if needed. Add profile-specific diffuse-only
instruction seeds and final-output ceiling selection; point/fill specialization uses existing
proved sites. Ordinary mode must retain its old byte output; cache selective stages separately.
No live new mode is advertised until VS+PS seed proof, constant-port and all-family resource
checks pass. RE updates belong in the existing material-family notes, not raw shader listings.

Affected host commands, with reference extensions including runtime e constants:

```
PYTHONPATH=verification/probe python3 -m unittest \
  verification.analysis.test_linear_material_transformer \
  verification.analysis.test_xt_material_transformer \
  verification.analysis.test_linear_material_constant_port \
  verification.analysis.test_linear_material_reference \
  verification.analysis.test_xt_material_reference
```

Observable milestone results: every current selected family/branch has exact ordinary
regression, distinct point/diffuse/fill compensation, untouched specular/emission/reflection
inputs, old alpha and motion/depth, runtime inv_e constants without conflicts, and bounded
weighted SM3 slots. Test e=1/8,1/2,1,2; gains 0/1/4/16; fill 0/.03; loop counts 0/1/8 and fixed
light; both faces, shared cube half, Terran fused MAD, palette, glass Fresnel, XT damage/detail/
occlusion and Terraformer additive RGB. Compare with `B/e+H` from independently separated
reference components. Mutated seeds/clamp sites/constants, aliases and failed variants refuse
without publishing partial output. Include in-range near-cap B/e witnesses and explicitly
out-of-domain original-cap cases; checking only grey diffuse at EV1 is insufficient.

Subsequent bounded live integration files:

- `src/proxy/motion_output.{cpp,h}`: frame packet, selective caches/admission, constants shadow/
  rollback, phase-meter trigger, mode/history-domain handling and counters.
- `src/renderer/hdr_pass.{cpp,h}`: meter-only background transaction, no duplicate meter,
  resolved-Z versus unresolved-Q writeback/snapshot contract and recovery.
- `src/temporal/resolve.hlsl`, `resolve.h`, `src/renderer/temporal_pass.{cpp,h}` and generated
  resolve program: selective current-read conversion, one history, domain validation.
- `linear_emission_pass.{cpp,h}`, authored fade/screen generators and producers: scratch
  normalization, selective ceilings and exposed-destination screen law in the existing pools.
- `src/proxy/capture.cpp` / bloom input snapshots: propagate actual display source units;
  bloom algorithms need no second exposure or new extraction image.
- Launcher/comparison configuration and owning architecture/verification notes. Leave ordinary
  Auto defaults and fill defaults unchanged.

## 10. Acceptance, measurements and remaining proof

A small **host algebra sanity check**, not a shader/GPU fixture, evaluated five exposures
(1/8,1/4,1/2,1,2), colored B/H and one colored screen source: **10 cases / 30 RGB comparisons**,
maximum double error 0; e=1 old screen-law error 0. A constant-target adaptation check over
600 steps at 60 Hz and tau=1.2 s moved EV -3 toward +1 to 0.999038522, with per-step error
factor 0.986207117. These validate the written equations only; no rendering performance or
native precision claim follows. Named source paths and the five test modules were checked.

Parent's subsequent scoped X3 queue should extend the existing material, fade/screen,
`run_temporal_pass.py` and bloom fixtures. Every Wine command uses
`X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py ...`; none was run here.

Required integration witnesses:

- Boundary ordering: background meter sees no compensated draws, queues once, consumes next
  latch; missing/failed Clear, chain/readback failure and ownership change hold e or block
  safely. Ordinary mode's meter path is unchanged.
- Shader/component cases above, real alpha/cutout/occluder parity and all current converted
  families. Mode failures preserve application constants even when its next setter is cached.
- Colored overlapping screen sources and destinations, source-over self-overlap and q=0/1,
  additive-before/after material, screen-before/after fade, negative EV high-base scratch
  values, no extra geometry submission, native alpha, region witness and every recovery rung.
- TAA: static B-only at varying e stays constant; H-only follows e, mixed colored regions,
  motion/disocclusion/jitter/cuts, minuscule emission, near-cap values, all early-return paths,
  mode toggles/Reset and clean failed-resolve Q writeback. No history receives the wrong units.
- Bloom: correctly exposed B/effects threshold, resolved Z*4 versus unresolved Q*e snapshots,
  strength zero, no-bloom and sharpen order. Separate scene EV from storage gain in logs.
- Current unknown/native translucent routes: count and inspect their overlap with required
  user-visible features; qualify exact additional writers if they prevent the requested look.
- Measure shader creation/loading, per-draw CPU, meter bracket, TAA current-read ALU and
  scissored composition GPU cost once on the consolidated build. No broad benchmark by default.

One later user-run A/B: ordinary Auto and selective Auto at ceiling1/fill.03, plus manual EV0;
station hull/headlight/fill, moving specular, colored reflection, emissive lights, nebula,
explosions, station fade/port, opaque glass, XT and cutout edges. Require actual selective
coverage and stable base with still-exposed highlights/effects, active TAA and no cursor/
recovery regressions. The agent never launches the game.

Remaining targeted evidence: exact diffuse-only seeds for fused/packed family instructions;
all shader constant/slot proofs; specialized scratch finite behavior; meter state restoration
at the depth-only Clear; selective resolve compilation budget; coexistence policy for the sun
fraction; and measured translucent coverage at the user's scene. These are bounded source/
fixture tasks, not reasons to restore the rejected paired-scene requirement.

## 2026-09-15 implementation checkpoint

Milestone 1 create-time variants are implemented and independently reviewed;
the [feature ledger](../verification/material-exposure.md) distinguishes host
source/tail proof from pending whole-program GPU and runtime qualification.
No live selective-exposure mode is enabled by this checkpoint.
