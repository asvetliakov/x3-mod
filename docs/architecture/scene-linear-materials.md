# Scene-linear DEFAULT materials

Implementation qualification, 2026-09-13; **reviewed opt-in material slice installed; game acceptance pending**. This work
follows the installed bloom checkpoint. The current FP16 target contains the
game's gamma-space lighting; `material_radiance` only removes selected upper
clamps. Neither operation evaluates lighting in linear space.

The initial slice is the complete **Argon SM3 DEFAULT material contract**,
including two-sided and toggle variants: 10 archive pass pairings, 3 VS programs
and 6 PS programs. The reviewed [shared hull expansion](material-next-slice.md)
adds ten Khaak/Teladi/Teladi_nodiff/Xenon DEFAULT pairs with the same three VS
and six additional PS. Both contracts are qualified, reviewed and installed; gameplay acceptance
is pending. Evaluate this material's diffuse, existing
specular lobe, reflection and emissive contributions in an explicitly defined
linear working space, then compatibility-encode its RGB into the existing
engine-space FP16 scene. Keep the original alpha and live motion/depth outputs.
This achieves linear evaluation and HDR emissive range for that material slice;
it does **not** make the whole scene or its transparency blending linear, and
it does not claim PBR, clustered lighting or HDR display output.

## Initial Argon evidence and coverage

Inputs are the complete local archive programs/disassembly and the derived
[motion pair inventory](../../verification/results/motion-output-profiles.json),
not a list of captured shaders. The 10 pairs represent 24 archive pass
occurrences, all class A motion profiles. Two pairs appeared in the older
iteration-05 scene capture: 2,512 of 11,493 scene draws (21.86%). That draw share
is historical coverage metadata, not current coverage, pixel coverage or GPU
cost. The other eight pairs require no new game visit to inspect or exercise
synthetically. All six PS instruction bodies and all three VS bodies were
manually inspected for this plan.

| Vertex programs | Pixel programs / coverage |
| --- | --- |
| `53a0a641107ed76c` | `8759c7838bbc86c2`, `63f96eba9eea7880`: base and two-sided DEFAULT |
| `719856ce0c213220`, `badefd5143b3024f` | Each paired with `593e5dea9b3457d5`, `7a0bb00a8070496a`, `8d5b2ba0fb4d13bf`, `dab93928f26906f7`: the archive's `_0000`/`_0001` and color-toggle paths |

Coverage remains an exact **game program and effect-pair contract**, generated
from the archive inventory with reviewed input/output facts. Do not promote
other shaders based on names or a matching clamp motif. This is broader than
capture-only identification while retaining a bounded semantic proof. Material
coverage is separate from the existing 169-pair temporal registry; all other
reviewed motion pairs continue to work.

### Targeted shader findings

For these six PS programs the stable resource roles are:

| Input | Instruction-derived role | Linear treatment |
| --- | --- | --- |
| s0 RGB | Diffuse color, optionally through the game's affine hue/saturation/contrast transform | Keep the artistic transform in its authored code-value space, then decode RGB before lighting |
| s0 alpha | One source of output alpha | Preserve unchanged |
| s1 red | Scalar specular/reflection strength; original arithmetic multiplies specular strength by 3 | Treat as a data mask, not a color texture |
| s2 RGB | Added to the final material RGB without directional-light modulation | Decode as a separate emissive/lightmap contribution; apply a separate linear emissive scale |
| s2 alpha | Interpolated with diffuse alpha using `g_EnableGlow` | Preserve unchanged; the name does not make RGB conditional on glow |
| s3 RGB | Cubemap multiplied by diffuse color and the scalar specular mask | Decode cube color before multiplication; initially retain this legacy reflection model |
| COLOR0 RGB (original) | VS point-light accumulation plus material emissive | Decode point-light RGB before accumulation; preserve the already-scaled material emissive term; do not decode their sum |
| COLOR0 alpha | Material alpha multiplied by the VS fog factor when enabled | Preserve unchanged |

The base PS has two directional lights; toggle PS programs have one. The
one-sided and two-sided paths are separate contracts: the latter actually
consume VFACE and flip the normal. Color-transform toggles change register
layouts and whether the affine transform exists. Preserve their distinctions.
The material's other angular/specular saturations are response shaping, not
HDR radiance clamps; this slice does not blindly remove them.

A consequential correction to existing terminology: the c0 clip-row family is
**loop-free, not necessarily light-free**. VS `badefd5143b3024f` evaluates one
fixed point light using c4 position, c5 RGB and c6 attenuation, then adds c19
material emissive. It has no light-count register or relative addressing.
VS `53a0a641107ed76c` and `719856ce0c213220` instead execute the eight-entry
c0–23 / i0 loop and add c40 emissive. All use the same inspected normal,
distance and attenuation model. Thus the temporal light-index guard describes
constant safety, not whether the shader produces light. This also explains why
suffixes/toggle-directory names must not determine lighting semantics.

### Targeted engine inputs and scaling

Read-only Ghidra decompilation of material submission `0x004c0150` confirms that
its `LightDir_Color0/1` effect writes copy three floats from the selected light
node's data at offsets +4/+8/+12. The point-light color writes copy the same
three fields. This upload stage applies no color transfer function or separate
intensity scale. The existing [constant upload study](../reverse-engineering/constant-uploads.md)
documents the effect/state-manager path and why temporary device state must be
restored even when the next game setter is suppressed by its cache.

The [upstream input study](../reverse-engineering/material-color-inputs.md)
traces light RGB to signed fixed-point channel values divided by 256, with
no separate RGB intensity multiplier on that path. Material emissive differs:
its uploaded constant can already contain authored strength multiplied by tint.
These findings do not establish an authored transfer curve or physical units. The decompilation contains uncertain
inferred C types; only the observed float-field copies are used here. Local
output is `/tmp/x3-scene-linear-material-parameters.txt`, with its headless log
alongside it. No game code, shader bytes or decompiler output belongs in Git.

## Color and material contract

Start with an explicit gamma-2.2 legacy input convention matching the current
HDR decoder. It is a declared conversion of legacy assets, not evidence that
the assets were authored to an exact transfer standard. Decode nonnegative
texture/color RGB **before** lighting multiplication and addition. Keep normal,
scalar specular, alpha, fog, positions and attenuation data unconverted. Use
separate linear gains for direct light and emissive; never use exposure as the
emissive source or exponentiate a new linear HDR gain.

Conceptually, retain the existing lobe for Argon. The shared hull family
retains its own diffuse coefficient 0.5, sixth-power specular lobe and cube
coefficient 0.5; it does not inherit Argon's exact float32 0.4 / fifth power /
cube coefficient 1. The [offline reference](../../verification/probe/linear_material_reference.py)
records these per-program differences. For Argon:

```
A = decode(legacy_color_transform(diffuse.rgb))
P = sum(legacy_point_response * decode(point_color))
M = legacy_scaled_material_emissive * material_emissive_gain
D = sum(legacy_directional_lobes * decode(directional_color))
R = decode(cubemap.rgb) * specular_mask * A
E = decode(lightmap.rgb) * lightmap_emissive_gain
L = A * (P + M + D) + R + E
```

These are derived role equations, not copied shader code. The exact per-profile
lobe factors and alpha dependency must be reproduced from the original program.
`M` remains the game's albedo-tinted material emissive; `E` remains independently
additive. Neither is inferred from final pixel brightness. Treat the already
scaled `M` constant as a legacy emissive amplitude/tint in the new working
space, preserving its native strength without exponentiation. This does not
recover the original tint colorimetry. A later scoped material-binding hook
could separate strength and normalized tint if that refinement is justified.
Our gains multiply the resulting working-space contributions, allowing
4/16/etc. without a 0–1 output clamp. The
initial lobe is intentionally the legacy response evaluated in linear space;
a modern roughness/Fresnel/GGX response is a subsequent material change with
its own tuning. Avoid changing transfer, lobe, shadows and light selection in
one first acceptance run.

The meaning of legacy light RGB components above one remains a declared
conversion policy, not recovered physical intensity. The input study finds
zero baked material-emissive defaults and zero active values in the historical
Argon capture; that does not make nonzero material emissive unsupported or
unimportant. Test it synthetically. Start all new linear gains at one and
qualify higher gains separately. Do not infer lightmap texel range from a
nonnull texture binding.

## Composition with the live temporal shaders

Use one combined transformation plan validated against the **original complete
program**, its reviewed material profile and existing motion pair row. That
plan describes original-source edits plus the current motion/depth insertions
and produces one owned variant. Keep original fingerprint/count/structure
guards and immutable original programs for fallback. Do not feed a previously
material-patched stream into `material_motion_pixel_variant_for`: its original
fingerprint and offsets intentionally reject that operation. Do not remove
those checks to make chaining work.

The selected implementation reuses the unchanged row-explicit motion transformer
on the immutable original. An original-offset merge verifies every copied
original span in its result, preserves the temporal insertions, then applies
the reviewed material edits. One temporary motion vector at shader creation
avoids duplicating its validation logic. Publish the final output only after
merge and resource/framing checks succeed.

Retain current VS position instructions and temporal splice exactly. Add the
linear color work only where original light RGB and emissive are consumed;
prove that introduced temporaries/constants do not collide with original code
or the temporal fragments. Source color conversion in the VS avoids per-draw
CPU pow calls and uploads, but its cost scales with vertex count and active
point lights and must be measured. A CPU dirty-constant conversion cache is an
alternative only if that measurement justifies its additional state lifetime
and restore machinery.

SM3 COLOR semantics are not inherently clamped, but every selected original
PS declares COLOR0 with partial precision. Preserve that declaration and its
alpha path. Export the new linear RGB through a separate full-precision
TEXCOORD6 varying (VS o8, PS v7.xyz), proven free after both temporal exports;
redirect combined-PS RGB consumers to it. The combined VS need not recompute
unused legacy COLOR0 RGB, but must still write its original alpha. This avoids
changing the precision of COLOR0.w while carrying HDR RGB without the legacy
partial-precision declaration.

Keep original texture fetches, including their shared alpha lanes and allowed
partial precision. Their sampled code values are the retained legacy inputs;
conversion cannot recover precision lost there. All newly linear accumulation,
gain, decode, encode and HDR RGB output work uses full precision. This follows the documented
[SM3 input register contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-3-0)
and [partial-precision modifier](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-modifiers-ps-2-0). Numerical
fixtures must include the permitted source-sample precision in their tolerance,
and separately prove the full-range HDR path. No additional texture fetches are
introduced merely to preserve alpha. Keep the existing no-MSAA gate: replacing
a COLOR iterator with ordinary TEXCOORD interpolation must not silently change
multisample/centroid coverage semantics.

Reserve the existing VS c252–255, PS c216–220, oC1 motion and oC2 current-depth
ABI. Color edits never change their data or output masks. Keep `current_depth`
true/false variants. Shared VS/PS programs must have one consistent plan across
all pairs they serve; publish a material+motion pair only when both required
shader objects exist. A material failure retains the ordinary motion pair.
Use shader-local `DEF` constants for fixed transfer parameters and process-start
gains: reserve VS c248–249 and PS c212–213 after proving these free in all nine
originals and both temporal variants. Gains are finite, in [0,16], default one;
changing them requires a new configured variant. This slice adds no per-draw
material constant upload or save/restore range. The original application
constant state remains untouched.

Preparation, proof checking, allocation and `CreateShader` happen at shader
creation, not per draw. The hot path only selects cached objects and known
profile metadata under the existing pass gate.

### Existing live-route boundary

`MotionOutput::register_vertex_shader` and `register_pixel_shader` currently
create only motion variants. `evaluate_draw` selects an exact original pair
after establishing scene ownership, then applies opaque/depth/write-mask and
constant-safety gates. The material feature must keep separate cached combined
variants beside those motion-only objects, selecting both combined stages
atomically for an eligible pair. It must not replace the only fallback object.
The existing route checks sRGB **writes**, but does not establish texture
sampler sRGB decode state. Require known `D3DSAMP_SRGBTEXTURE=FALSE` for all
four used samplers, including the scalar mask sampler. Never silently force
them off. Extend the sampler hooks to material requests independently of mip
bias, update only on successful setters outside state-block recording, and
refresh/invalidate through attach, Reset and state-block application. Unknown
or incompatible state selects motion-only. Use cached flags at draw time,
not four new COM getters on every draw. Microsoft documents sampler decoding
separately from output encoding in [D3D9 gamma handling](https://learn.microsoft.com/en-us/windows/win32/direct3d9/gamma).
Material profile, decode, sampler or
combined-object refusal is not a temporal pair failure: preserve the existing
jitter, RT1/RT2 and missing-history sentinel path. For a combined-bind failure,
restore any partial setup and try the ordinary motion pair once; if that also
fails, preserve the existing original-draw fallback and log the failure.

## Transition and fallback

The current compositor, meter and bloom agree on one global engine-space input
encoding. For this first slice, output `encode_gamma22(L)` to oC0 RGB and leave
alpha untouched; existing downstream gamma-2.2 decode recovers `L` before
exposure/bloom/AgX. Restrict admission to matching decode mode until other inverse
pairs are explicitly qualified. FP16 stores values above one, but compatibility
encoding adds quantization error that must be included in numerical tolerances.

The first-slice numerical policy is explicit and lane-wise:

- Sanitize color-code inputs with ordered `MAX(x,+0)` then `MIN(result,65504)`,
  retaining the input as the first operand. This maps NaN/negative infinity
  to zero and positive infinity to the cap under the documented DX9 primitive
  comparisons. Apply the same sanitation to the scaled material emissive
  input, without a power. Extreme finite inputs are bounded by policy.
- Decode with exponent 2.2, a safe positive operand floor of 1e-10, and an
  explicit zero selection for nonpositive sanitized input. Do not cap each
  decoded source before lighting: attenuation can bring it back into range.
- Sanitize the final linear RGB sum to [0,65504], then encode with exponent
  1/2.2 and a safe operand floor of 1e-22. Select exact +0 for nonpositive
  input, including -0. An authored `CMP(-sanitized_x,+0,positive_result)` has
  the required select polarity; preserve it through bytecode generation.
- Keep all these new operations full precision. The largest decoded source
  is about 3.94e10; the encoded final ceiling is about 154.6011, safely inside
  FP16. Gains stay finite in [0,16]. GPU cases must include both signed zeros,
  negative finite values, tiny positives, NaN, both infinities and the caps.

The instruction ordering follows Microsoft's [DX9 MAX](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/max---ps)
and [CMP](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/cmp---ps)
contracts; actual emitted bytecode and backend behavior still need fixture
qualification. Do not substitute later Direct3D floating-point rules as proof.
Exact black stays zero in the encoded scene; the existing display decoder
retains its own negligible floor. This is a bounded rendering policy, not
physical intensity calibration or a guarantee for degenerate legacy geometry.

This is an explicit transition: uncovered opaque materials, background and
transparent/additive effects keep their current rendering. Covered opaque
surfaces obtain true linear lighting evaluation; effects blending over them
still occurs in legacy code-value space. Never call this target scene-linear.
Switching global decode to `none` is not a solution while those writers remain.
A whole-scene linear milestone needs an inventory and conversion policy for
**every** color writer, including shared GUI/scene shaders and low-model/fixed
function paths, or a proven separate-layer composition boundary.

Keep current scene ownership, target, depth, viewport, topology, blend and alpha
gates. Only compatible opaque draws enter this first slice. Unknown game
programs, unreviewed pairings, unavailable material variants or a mismatched
color contract retain original material color plus existing temporal behavior.
No backend-private export, DLL layout/hash or new graphics API is needed.
Native Windows source/API compatibility remains distinct from native Windows
runtime verification.

## Verification and the one later user run

Offline inspection can establish all 10 pairs' semantic roles, exact conversion
sites, resource/register budgets, original alpha/coverage invariance, branch and
shared-program consistency. The upstream color/default evidence is recorded
in the input study; replacement shader and numerical proof remain. Derived
profiles should be generated deterministically from the complete archive inputs. Negative cases must reject mutations, truncated
streams, changed definitions, missing conversion sites and collisions without
publishing partial variants.

Focused host and X3 GPU fixtures should compare the combined material+motion
variant against authored numerical references with independent point, material
emissive, lightmap, diffuse and cubemap contributions. Include zero/one/eight
loop lights and the fixed-single-light VS, both face signs, color-transform
variants, colored—not just grey—inputs, HDR gains 1/4/16 and the compatibility
encode/decode round trip. Verify original alpha exactly where representable,
RT1/RT2 against unchanged motion outputs, TAA off/on, current-depth off/on,
variant-creation failure fallback, state restoration and Reset recovery. Run
only the affected fixture chains under the shared Wine lock, X3 bottle only.
No game launch is required for these checks.

Performance evidence should compare original, motion-only and combined shaders
on representative geometry and light counts, plus create-time cost for all nine
unique programs. Inspect register pressure/instruction limits and absence of
new per-draw allocations, full-program hashing or repeated validation. GPU
fixture timings are diagnostic; they do not establish game FPS.

After a reviewed candidate exists, one user-launched capture can resolve actual
material usage and value ranges, whether the scene visibly exercises the
emissive inputs, image balance, covered/uncovered boundaries, temporal stability
and game frame cost. Use the consolidated diagnostics already available: creation results identify
original programs, per-frame material counters report routes/refusals/bind
failures, first refusals identify the original pair and reason, and F8 captures
provide per-draw shader identities and original constants for light-count and
range analysis. Existing exposure/bloom timings remain available. Use a hull with visible emissive panels, near/far views and a firing or
active-light event, with a fixed-EV A/B before automatic exposure/bloom tuning.
No claim that all 10 pairings were exercised should follow from visiting one
scene. Existing menu/window behavior is not changed by this slice.

## Offline design review

Independent Sol/high review identified shared-stage fallback, sampler decode,
COLOR0 partial precision, alpha-lane and domain hazards. The decisions above
resolve the routing, precision and constant-lifetime design points. The
[derived original-site profiles](../reverse-engineering/linear-material-profiles.json)
bind the nine source programs and available resources; by themselves they do
not prove a replacement shader. The bounded numerical policy has also been reviewed.
The emitted bytecode, alpha/temporal invariants, numerical behavior and GPU
cost are qualified separately below. The integration gate and installation are complete; the fixed-EV material
comparison is run 6 in the [brief queue](../verification/user-runs.md).

The offline checkpoint passed independent Sol/high review with no open
findings: 15 original-site proof tests and 18 analytical-reference tests,
33 total. The derived profiles regenerate exactly without raw game payload.
The [reference](../../verification/probe/linear_material_reference.py) models
all six pixel contracts and both point-light forms; its optional half-source
mode covers sampled/varying endpoints, not every legacy partial-precision
intermediate. Geometry singularities are outside its analytic domain. These
checks qualify offline preparation, not emitted shader execution or game
appearance. The implementation qualification below extends this evidence.


## Implementation and qualification

`linear_material.cpp` creates combined variants from immutable original game
programs. It first invokes the unchanged motion transformer, checks the copied
original spans, then inserts only the reviewed material edits while preserving
the temporal insertions. Failed validation leaves the output untouched. The
live route caches combined shaders alongside the existing motion-only objects;
it does not replace a shared vertex shader's ordinary temporal fallback.

The feature is opt-in with `--linear-materials`, requiring `--motion-output
--hdr --hdr-tonemap` and gamma-2.2 decode. Three independent process-start gains
are finite values in [0, 16], default 1:

| CLI setting | Environment | Meaning |
| --- | --- | --- |
| `--material-direct-gain` | `X3M_MATERIAL_DIRECT_GAIN` | Strength of decoded directional and point lights |
| `--material-emissive-gain` | `X3M_MATERIAL_EMISSIVE_GAIN` | Multiplier on the game's already-scaled material emissive |
| `--lightmap-emissive-gain` | `X3M_LIGHTMAP_EMISSIVE_GAIN` | Strength of the separate decoded s2 RGB contribution |
| `--material-fill` | `X3M_MATERIAL_FILL` | Constant hemispherical fill `k` added to the lobe sum before the albedo multiply, tinted by the decoded sun register; finite 0..0.5, **production default 0.03 with linear materials**; explicit 0 omits the fill instructions and preserves byte-identical parity ([fill-light.md](fill-light.md)) |

Malformed gains or incompatible configuration disable the material feature.
The gains are shader-local definitions, fixed for the device's lifetime; Reset
retains them. No extra application constant uploads are needed. Sampler decode
state is cached at attach, Reset and state-block boundaries and updated by
successful setters. Admission performs no new per-draw COM reads, allocation,
shader hashing or bytecode validation. Combined-stage bind failure restores the
original pair before one ordinary temporal retry; a later shared constants/MRT
failure retains the existing original-draw fallback.

The initial Argon structural fixture checked 72 depth/gain variants with 749 C++
assertions and four host tests, including byte-exact reconstruction of original
alpha/position and temporal edits. Maximum weighted instruction slots are VS 77
and PS 164 (SM3 limit 512). One host diagnostic created all 72 variants in
1.56 ms total; this is creation work, not a per-frame or gameplay measurement.

The initial Argon detached [D3D9 GPU fixture](../../verification/results/bottle-X3/linear-material-gpu.json)
passes 167 cases and 1,503 RGB samples across all ten pairings, both face signs,
zero/one/eight loop lights, the fixed point-light form, source isolation, gains,
HDR range and exceptional transfer inputs. Alpha and motion match unchanged
shaders exactly across 42,752 pixels; current depth also matches when enabled.
All 56 shader creations succeeded. Maximum RGB error consumes 15.9% of the
specified float/half-source envelope tolerance. The first attempt exposed a
fixture teardown hang; releasing D3D resources before its window/runtime and
printing the result only after cleanup produced a clean complete rerun.

Fenced X3/FEX diagnostics on a 98,304-vertex grid, four draws per sample, compare
original / motion-only / combined medians: 0 lights 0.611 / 0.740 / 0.762 ms;
8 lights 0.608 / 0.735 / 0.738 ms. These include submission and completion and
are neither isolated GPU timings nor a claim about game FPS. The [live integration fixture](../../verification/results/bottle-X3/linear-material-live.json)
then passes 1,220 checks in 96 frames across eight feature/ownership/TAA twins.
It executes the actual draw route with six admitted and six refused frames per
enabled case. Attach/Reset sampler refresh, recorded/applied state blocks,
shared-stage fallback, immutable gains, exact alpha and RT1/RT2 twins, and final
zero device/factory references pass. Combined shaders add exactly two owned
references in every corresponding case and are retired correctly. Unknown
sampler getter failure and shader creation/bind/restore failures are qualified
by scripted host control-flow tests, not fault-injected into this GPU script.
Native Windows execution and gameplay appearance remain unverified.


## Shared hull runtime extension

The expansion uses the same shader conversion and live policy, with exact
per-program sites and clamp destinations. The larger source bound is 1,296
DWORDs and the pair table explicitly lists all twenty valid combinations;
sharing a VS does not admit an unlisted PS. No new rendering pass or application
constant range is introduced. All 72 previous Argon generated variants remain
byte-identical to the pre-extension baseline.

The expanded structural fixture covers 120 variants with 1,313 checks and five
host tests. Maximum weighted instruction slots are VS 77 / PS 165. A host
creation diagnostic transformed all 120 in 1.857 ms; this is startup generation
work, not a per-frame or game measurement. Per-draw selection remains a bounded,
allocation-free twenty-pair check on cached shader identities.

The current detached GPU result extends the original 167-case prefix to 313
cases over all twenty pairs/fifteen originals: 2,817 RGB samples and 80,128
exact alpha/motion pixels, with current depth unchanged where enabled. All 89
shader creations succeed. Every new PS is independently discriminated from the
old diffuse/specular/cube coefficients. Maximum normalized tolerance use is
15.97%; the run exits cleanly. Representative shared-family fenced medians for
original / motion / combined are 0.607 / 0.738 / 0.738 ms with zero lights and
0.609 / 0.734 / 0.744 ms with eight lights (same diagnostic grid/sample method).
These are not game FPS or native-Windows runtime results.

The live fixture keeps the same twelve-frame lifecycle, alternates Argon and
shared DEFAULT on one VS, and uses the uncovered Split PS as its negative.
The fresh-candidate live result passes 1,228 checks across eight
feature/ownership/TAA twins and 96 frames. Both families activate on their
intended frames, Split retains ordinary motion, alpha/RT1/RT2 remain exact,
attach/Reset and state blocks recover, and the three extra combined shader
references retire to zero. Final review and installation are complete; gameplay acceptance is pending. The existing brief material run will cover the expanded candidate;
no separate gameplay session is being requested for each family.
