# Hull material work: visual value and allocation

2026-09-15 independent critique. The orchestrator adopts the next-step gate:
retain the reviewed selective shader checkpoint and defer its GPU/runtime
integration and blanket older-family expansion until a matched appearance
comparison. This is a scheduling gate, not cancellation of the material or
reflection goals. Existing shadow prerequisites and targeted emission work
remain the next priorities. Current installation is described only in
[status](../status.md).

## User scope clarification — 2026-09-15

“Consistent addition of multiple lights” means evaluating the existing sun and
point-light contributions in linear light. It adds no lights, changes no model,
and does not extend admission or falloff ranges. Dynamic light creation is a
separate feature and is not implied by this benefit. The user points out that
dynamic lights are sparse and overlapping illumination is uncommon in normal
play. Run-51 evidence includes sun plus a player-ship point light, but does not
establish a broadly visible multi-light benefit. This row carries low practical
weight for X3 and does not justify further hull conversion by itself.

The user excludes physically calibrated roughness/metalness requiring individual
material art review. A PBR conversion is **not planned** with the current assets;
no texture overhaul is available. A possible approximation derived from existing
textures/materials is only an unevaluated proposal, requiring a bounded automatic
prototype and demonstrated visual benefit before any commitment. Existing
normal/specular/cubemap response can still be calibrated globally or by family
without describing it as recovered PBR properties.

## Recommendation

Do **not** spend the next implementation budget converting every older hull family on the expectation of automatic visual improvement. First do one bounded, matched comparison of original hull shading and the current linear mode using existing controls. Keep the conversion available as an option. In priority order, spend subsequent effort on the user's brighter engine/weapon/light sources and geometric sun shadows; defer SSR and a replacement BRDF until a close/medium-distance witness makes their value concrete. Finish selective exposure only if this comparison supports keeping converted base shading or establishes a distinct user-valued exposure requirement. Its already reviewed shader work is no reason by itself to fund runtime integration. **Recommendation now: hold selective runtime/GPU integration until the matched A/B, retaining the reviewed source checkpoint.** The orchestrator adopts this comparison gate while retaining the earlier feature contract. Background metering, exposed-history units, packed-screen composition and finite-domain recovery are substantial integration obligations; the present evidence does not justify paying them merely to discover whether the converted base is preferred.

Most conversion so far is indeed the primary **SM3 hull/material families**: the total is 168 exact pairs / 137 original stages, including six asteroid pairs, 14 XT pairs, and six opaque-admitted SM3 glass pairs. The conventional 162-pair union is broader than hulls on one race or one ship and includes standard station materials. The separate emission producer has 20 SM2 pairs / 18 stages. The other 24 glass pairs, older hull profiles, backgrounds and transparent populations are separate contracts. Pair counts describe archive coverage, not visible pixels, materials needing artistic work, or the gain from converting more.

## What the existing work actually buys

Keep these concepts distinct:

- **FP16 storage:** retains values above one and more precision. It does not make the arithmetic linear or reconstruct already clipped energy.
- **Linear arithmetic:** decodes selected color inputs before lighting, then compatibility-encodes the result into the engine-space target. This is a declared gamma-2.2 adaptation of old assets, not recovery of known physically calibrated colors/intensities. The whole scene and all native blending are still not linear.
- **Source separation:** knows which operations contribute diffuse, point, specular, reflection and emission. This enables targeted gain, selective exposure and sun-only shadows. Separation is useful independently of deciding to replace the original diffuse look.
- **New BRDF/material model:** changes angular response, Fresnel, energy sharing or roughness behavior. This has not been supplied by linearization. More credible input assumptions and appearance calibration are needed.
- **Reflections:** X3 already samples cubemaps, using existing normals and scalar masks. SSR adds a visibility/radiance source and needs a fallback; neither FP16 nor linearization creates that source.

The legacy highlight response is preserved in shader arithmetic, but its **displayed** width/contrast is not thereby preserved. In a simplified isolated term with unit colors, original displayed `w` becomes approximately `w^(1/2.2)` after linear evaluation plus compatibility encoding (before exposure/AgX). A half-strength term becomes about 0.73; a specular `q^p` becomes `q^(p/2.2)` in that simplified code-value comparison. This can lift broad diffuse response and broaden visible highlights. It is a mathematical explanation of why unchanged powers/masks need not retain the old metallic impression, **not a causal measurement of the user's unmatched screenshots**. AgX, multiple sources, light colors, fill and exposure also affect the result.

## Automatic benefit matrix

“Automatic” below means reusing assets across covered exact shader families after proving their channel roles. It does not mean skipping representative appearance checks. Effort is a relative engineering assessment, not a time or FPS measurement.

| Candidate benefit | Actually needs linear hull arithmetic? | Robustness using existing inputs | Authored data / tuning requirement | Visibility at distance | Effort, risk and recommendation |
| --- | --- | --- | --- | --- | --- |
| Brighter engine/weapon sprites with HDR bloom | **No.** Linear emission composition can operate over a decoded original-shaded destination. | Strong for qualified additive sources; screen/alpha blends need their own source/coverage law. | Global or effect-class gain is plausible; no per-hull authoring. Cannot infer desired physical brightness from texture alone. | High relative value: small intense sources can remain readable through bloom and temporal integration. | Existing additive route is independent of linear hulls. Packed screen route currently depends on them. **Prioritize, preserving qualified blend order.** |
| Brighter hull windows/lightmaps/material emission | No mathematical requirement to relight diffuse; does require component isolation before final mixing. | Good for proved lightmap and material-emissive roles; zero/absent emission data cannot create new emitters. | Small global/family gain calibration; special artistic content may need exceptions. Not inherently texture-by-texture work. | Medium/high if luminous features cover enough pixels; lost subpixel source energy is not recovered by gain alone. | Current controls require linear materials; original-base + emission hybrid needs scoped shader work. **Good candidate if original base wins.** |
| Better multi-light addition and attenuation in linear light | Yes for a physically coherent linear sum; not for merely making lights brighter. | Arithmetic is deterministic; physical/material accuracy is limited by legacy color/intensity convention and vertex point lighting. | Global/family calibration might yield a good style. No reliable universal automatic conversion to physical intensity. | Low demonstrated value in X3: few dynamic lights and uncommon overlap. Existing light admission limits illumination; no broad gameplay benefit established. | More coverage alone preserves existing light selection. **Keep only if matched appearance is preferred or required by a chosen lighting feature.** |
| Readable dark hulls / fill | No; fill can be authored in either shading contract. | Current fill is a sun-tinted constant floor, not recovered environment irradiance. | One global setting (.03 accepted); no per-material authoring, but too much can flatten contrasts. | Broad face illumination survives longer than fine details. | One ordinary PS MAD already implemented. **Use accepted value; do not justify ongoing conversion by fill alone.** |
| Base hull at EV0, exposed highlights/emission | No fundamental requirement for replacing original diffuse math; requires separate terms and coherent exposure/composition. | Exact for identified terms under the chosen contract; not an automatic cure for changed BRDF appearance at EV0. | Global policy, no new textures. User choice still matters. | Broad body brightness is visible; highlight benefit depends on size. | Runtime changes meter timing, history units, composition, fallback/Reset. **Defer allocation decision until existing EV0 comparison.** |
| Sun shadows and self-shadowing | **No fundamental requirement.** Need geometry, sun visibility and sun/non-sun separation; physically correct application is simplest in linear contributions. | Strong geometric basis with current sun directions and material contracts. Current sun-share lane is specifically derived from linear variants. | Global bias/cascade/filter settings plus representative geometry checks; no roughness/metalness maps or one-by-one tuning. | Strongest on large ships/stations at near/mid distance; large shadowed sections survive when fine normal detail does not. | High replay/lifetime/coverage work; current lane applies no shadow. Original-shading alternative needs a newly proved sun response and domain-correct apply law. **Priority hull enhancement; retain existing route unless comparison warrants redesign.** |
| Better use of existing normal maps | No; current bump shaders already use them for directional specular/diffuse and pixel cube reflection. | Known AG or XYZ encodings exist; DEFAULT lacks a sampled bump normal. | None to preserve them. Stronger normal response/detail is a style change; XT has authored strength/detail controls, conventional Argon has no consumed strength constant. | Fine detail mostly near; coarse curvature remains longer. | Linearization adds no new normal detail. **Do not sell it as a new automatic gain.** |
| Fresnel / more plausible highlight and energy response | Linear-light evaluation is the coherent basis; full all-family conversion is not required to prototype a supported slice. | Existing normals, view, mask and exponent allow a consistent heuristic. Glass and XT already have Fresnel-like behavior. Mask is not metalness or calibrated roughness. | A global/family roughness/exponent mapping is possible, without touching every texture, but is art direction and may misread painted metal, bare metal and glass. Reliable physical distinctions need authored data or exceptions. | Highlight glints can survive; most subtle roughness/readability gain is close/medium. | Medium/high semantic work and calibration; no guarantee it restores preferred original contrast. **One representative-family prototype only after demand, not blanket PBR.** |
| Roughness-aware cubemap reflections | No requirement to replace original diffuse; new reflection math should have a defined linear radiance contract. | Normals, cube coordinates and strength masks exist. No established universal roughness, calibrated environment radiance or correctly prefiltered roughness mip chain. | Global/family heuristic plus cube filtering may be acceptable; faithful mixed materials need new data. Ordinary texture mips are not automatically a physical roughness prefilter. | Strong reflections on large foreground ships; reduced value for tiny distant hulls. | Additional shader/filtering/resource work. **Defer pending a concrete reflection witness.** |
| SSR of nearby geometry | No requirement to change base diffuse; needs depth, normals, reflection weighting and scene radiance. | Existing depth helps; material normals/masks are not presently a general SSR output buffer. Offscreen/occluded geometry is unavailable to screen tracing. | Can use heuristic existing gloss masks, no mandatory repaint; polished/rough classification remains uncertain. Needs cubemap fallback and global trace/rejection tuning. | Lower expected value in sparse open space; better near stations/large ships. Fine reflected features disappear with distance. | High passes/bandwidth/history/edge-artifact cost, currently unimplemented. **Behind emission and shadows.** |
| Remove a proved brightness clamp (including possible nebula limits) | **No.** Preserve intended scalar/angular/coverage clamps. | Strong only where RE proves radiance loss and actual input exceeds the limit. A nonclipped source gains nothing. Nebula shares exact shader identities with UI. | No new material art; perhaps one source-class gain. Must establish ownership and compositing semantics first. | Background occupies many pixels, but current exposure already looks acceptable: expected marginal benefit unproved. | Small isolated clamp edit can be cheap; proving affected stage/blend may dominate. Existing material-radiance helper is not a generic nebula fix. **Inspect one visible clipping witness, not a broad campaign.** |
| Convert every remaining older hull/glass family | Yes by definition; not needed to improve already routed high-quality hulls. | Can extend consistent arithmetic after family-specific ABI/role proof. Does not add lighting, roughness, geometry or reflection information. | Engineering can be automatic per program family; appearance can still change and require global/family tuning. No promise of a universal untuned improvement. | Zero benefit where those programs are not submitted; useful if an actual fallback/LOD path causes an inconsistent visible object. | Significant lower-profile precision/interpolator/blend qualification. **Treat as coverage consistency and compatibility scope, not automatic appearance ROI.** |

## Evidence and constraints behind the matrix

1. `docs/architecture/material-coverage.md`, counting contract and all-family matrix: 168 pairs / 137 stages, distinct 20-pair emission registry, full older-profile inventory. Do not add overlapping family rows.
2. `docs/architecture/scene-linear-materials.md`, targeted findings and color contract; `src/renderer/linear_material.cpp::transfer`, `gain`, `fill_instruction`, `pixel_sites`: source decode/encode, one ordinary fill MAD, masks untouched. `transfer` emits three scalar POWs per RGB conversion plus sanitation; vertex point-color work scales with active lights and vertices. Conversion is not computationally free.
3. `docs/reverse-engineering/material-color-inputs.md`: EXE light producer `0x004bdbf0`, color span `0x004bdc3a–0x004bdc77`, signed channel/256; submission `0x004c0150` copies colors and has separately strength-scaled material-emission paths. No recovered physical calibration or exact asset transfer curve.
4. `docs/reverse-engineering/remaining-hull-materials.md`, lighting/scalar section: shared diffuse .5 / power 6 / cube .5; Split .5 / 10 / 1; Terran 1 / 5 / 1. `linear-standard-materials.md` has authored application scalars; they are not universal per-texel roughness.
5. `linear-bump-materials.md`, sample/normal contract: diffuse, AG normal, scalar specular red, lightmap, cube; A feeds binormal and G tangent. Vertex point light uses geometric normal with no added point specular. `xt-materials.md` has extra occlusion/detail roles and strength/exponent parameters. `glass-materials.md` already has vertex-generated Fresnel and no BUMPMAP archive technique. These differences rule out treating every specular map as one interchangeable PBR input.
6. `src/renderer/material_radiance.cpp::apply_radiance_profile` replaces only reviewed `MOV_SAT COLOR0.rgb` with lower-bounded MAX and retains unrelated instructions. `material_radiance.h` calls this vertex-radiance upper-clamp removal. It is not full-source linearization or arbitrary upper-limit removal.
7. `tools/manage.py:83–92,195–235`: additive `--linear-emissions` requires motion/TAA/HDR/tonemap/gamma2.2, not `--linear-materials`; `--screen-emission`, material gains and sun lane do require linear materials. `MotionOutput::configure_screen_emission` and `configure_linear_distance_fade` retain the material dependency. Therefore “original hulls + all enhanced weapons + shadows” is a viable proposal, **not a fully available launcher preset**.
8. `directional-shadows.md`, current appended lane contract and receiver boundary, plus `docs/status.md`: sun-share extraction/lane is available, replay/cascades unimplemented. Early top-of-note status/budget prose is historical; do not treat it as the latest implementation status. `material-selective-exposure.md` defines B/e+H and the new meter/history/composition domains; selective runtime is not implemented.

A small family/global adjustment to the existing cube/specular response is more plausible as a first reflection experiment than SSR: it reuses real normal/view/cube inputs and can work without repainting each material. It still requires an explicit policy and representative calibration, and current general material-direct gain couples diffuse and specular rather than providing a dedicated reflection control. SSR cannot fill an empty or offscreen ray with missing ship geometry; the existing cubemap is the natural fallback, not evidence of a new dynamic reflection source.

## Hot-path cost and native Windows

Reuse original geometry, texture samples and normal/cube inputs where possible. Source variant construction is cached at shader creation; pair/sampler admission is bounded and cached at state changes. Broader archive coverage mainly increases creation/registry work until those shaders are submitted. Current linear decode/encode adds per-pixel ALU and per-vertex point-source work; existing small fixture timings do not establish game FPS or total-frame negligible cost. Emission composition adds region/full-surface copies and state brackets; shadows add draw capture/replay, maps and an apply pass; SSR adds screen tracing, buffers and temporal handling. Do not rank them by shader slot count alone.

`src/proxy/motion_output.cpp:1906–1943` uses documented D3D9 capability/format checks: SM3, 256 VS constants, mixed-depth MRT support, two targets for motion and three for depth, floating target/sampling support. `linear_material.cpp::body_shape/structure` checks transformed weighted instruction limits rather than assuming slot headroom. New material/reflection variants must recheck register, constant read-port and slot limits together; historical ≤180 PS counts are for earlier conventional groups, not a budget for every current combined option.

All recommended paths can use documented Windows/D3D APIs; none needs Wine internals. Original shading is native behavior, but combining it with new emission/shadow/SSR operations needs the same native driver creation, linkage, state preservation, Reset and composition checks as a linear route. Current CrossOver evidence and cross-compilation **do not verify native Windows execution**. Keep that gap in `docs/architecture/platform-portability.md`; no new runtime portability result is claimed here.

## Minimal comparison and allocation gate

Use existing installed controls, with the user launching. No new feature build is needed for the first decision.

1. Two loads of the same save and a reproducible stopped camera pose: original-material route and current `--linear-materials --material-fill .03`. Keep the same TAA/HDR/AgX, resolution, camera and supported additive-emission settings. Use fixed EV0 (`--hdr-exposure fixed`) and bloom off via the existing toggle for the diagnostic hull comparison. Do not accidentally leave `--screen-emission`, linear fade or sun lane requested in the original-material command; they depend on linear materials. Avoid a fade-band subject so that changing this route does not dominate the comparison. Use the main session's existing original-shading comparison if already prepared.
2. In each load, one identical near/medium hull/station pose and one typical distant-play pose are enough for this gate. Record original vs converted hull contrast, metal/gloss impression, readable form and any clipping; do not require every material to be individually inspected. A second material family is useful only if readily present in the same save. Capture at rest after histories settle.
3. In the same sessions, Ctrl+Shift+F9 supplies Auto (current +1 ceiling) vs EV0; Ctrl+Shift+F10 supplies bloom on/off. This separates exposure preference from conversion preference without implementing selective exposure. Original-vs-linear needs separate launch configuration; F9 is **not** a material toggle. The existing four EV screenshots have unmatched poses and cannot substitute for this pair.
4. Acceptance for continued hull-shading work: the user prefers the converted base at normal play distance, or names a specific visible defect that a **single bounded global/family calibration** can plausibly correct. If EV0 converted hulls still look bleak, selective EV0 base exposure cannot by itself restore original shading. If original wins and no specific automatic improvement is identified, retain it as the preferred base and scope emission/source separation separately; reassess the sun-share dependency before changing that architecture. If preferences are indistinguishable at normal distance, prioritize emission/shadows over more coverage or new BRDF work.

This is a decision experiment, not a claim of statistically measured user preference or measured GPU cost. If configurations materially change frame cost, collect matched at-rest timings once; no broad benchmark or extra feature construction is justified just to choose the next task.

## Unresolved questions

- Which current engine/weapon populations remain visibly too dim, and which use additive versus packed-screen composition? Existing 20-pair coverage does not establish visible-frame coverage or the desired gain.
- Does the preferred original look persist in a matched fixed-EV comparison, and is the complaint caused mainly by broad body lift, highlights, tone mapping or some combination? Current screenshots cannot isolate this.
- Would a small family-level response calibration satisfy the user without material-by-material work? It is feasible to try; source facts cannot predict preference.
- Which older-profile draws are actually necessary for the selected quality/LOD behavior? Converting unsubmitted profiles cannot improve that view.
- How much useful receiver/caster coverage and replay cost will the existing sun diagnostics establish? Native Windows rendering remains unverified.

## Critic reassessment after sparse-lighting clarification

The same independent critic strengthens the existing hold: its earlier weighting
of hypothetical multi-light and material-model gains was too generous for X3.
Linear hulls remain optional; no blanket conversion, selective runtime, PBR or
nebula-clamp work is justified without a demonstrated appearance need. The
orchestrator retains that hold. No new implementation or user run is queued.

The smallest proposed appearance experiment is existing targeted additive
emission gain on one representative effect, after identifying its blend route.
`--linear-emissions` already works independently of linear hulls; packed-screen
and material-emission paths currently do not. This proposal does not reopen the
accepted screen-emission gain 1 or authorize another gain bracket. Shadows remain
the strongest prospective automatic hull enhancement and retain their existing
route-B feasibility order.

If a matched comparison favors original hull shading, a narrowly scoped original
base plus emission/shadow path may be worth designing. Removing a gate alone is
insufficient: original code-value sun attenuation and the current linear sun-share
application use different laws, including clamp/blend handling. Existing inputs
do not recover physical roughness/metalness; automatic reflection/specular tuning
remains a conditional artistic experiment. The full read-only reassessment is
local at `/tmp/x3-material-critic-reassessment.md`; no production work followed.
