# Linear Asteroid materials

The isolated Asteroid extension starts from the reviewed 110-pair source
checkpoint `10e447b`. It adds the complete SM3 Asteroid DEFAULT/BUMPMAP group:
**six VS, four PS and six exact pairs**, bringing the transformer to **83
originals / 116 pairs**. The pure source review is approved and the detached X3 qualification below now
passes. Live qualification and installation of these additions remain pending. This is a step toward complete material coverage; the
remaining 46 opaque SM3 pairs and lower shader models remain in scope.

The [original profile report](../reverse-engineering/linear-material-profiles.json)
records exact archive aliases/toggles, instruction and operand sites, complete
geometry/lighting/alpha chains and resource reservations. Names are inventory
evidence, not shader admission. Complete original fingerprints, lengths, sites
and existing ordinary-motion rows must agree before constructing a variant.

## Native equations and preserved inputs

Asteroid has separate base and detail textures. Decode each RGB source before
its original scalar multiplication, then preserve the original addition and
lighting product:

`albedo = decode(base.rgb) * base_weight + decode(detail.rgb) * detail_weight`.

The application supplies the detail weight at PS c4.x for base shaders and
c2.x for toggles. The original preshader supplies the complementary base value
at c5.x/c3.x. The transformer retains both actual scalar operands: it does not
decode, clamp or recompute weights, and keeps preshader comments opaque and
byte-exact. The numerical reference accepts the two scalar inputs separately;
its finite, nonnegative analytical domain is not a new runtime admission rule.

Directional lighting uses unit diffuse, a cubic specular response and the
original `sat(3*NdotL)` angular factor. Unlike the conventional hull shaders,
there is no outer specular factor of three. Specular texture red stays scalar
data. There is no reflection cube, lightmap or additive texture-emissive tail.
The base RGB receives lighting from converted directional colors and converted
VS point light plus the existing scaled material-emissive term. The latter
retains the established tint/strength limitation and gain policy.

DEFAULT uses three 2D samplers: base, specular and detail. BUMPMAP uses four:
base, AG normal, specular and detail. BUMP keeps the native A-to-binormal,
G-to-tangent reconstruction, RSQ/RCP behavior, normal and view normalization.
It has no VFACE or reflection output. The additional base-program UV varying
shifts its view/normal/tangent/binormal inputs; toggles pack detail UV into
the original UV varying's ZW. Original UV, basis, angular and scalar operations
retain their precision flags. Only proved radiance destinations lose PP.

Native alpha is base texture alpha times vertex alpha, written before final
RGB. All alpha instructions, source sample alpha lanes and VS fog/alpha
branches remain byte-exact. Authored texture conversions touch XYZ only.

## Explicit varying and position contracts

The new family uses four fixed layouts. No allocator or mixed-precision
declaration packing is introduced. Entries list VS output / PS input / semantic:

| Asteroid layout | Existing motion | Existing depth | New full-precision RGB | PS scratch |
| --- | --- | --- | --- | --- |
| DEFAULT base | o6 / v5 / TEX4 | o7 / v6 / TEX5 | o8 / v7 / COLOR1 | r9 |
| DEFAULT toggles | o6 / v5 / TEX4 | o5 / v4 / TEX3 | o8 / v7 / COLOR1 | r9 |
| BUMPMAP base | o8 / v7 / TEX6 | o9 / v8 / TEX7 | o10 / v9 / COLOR1 | r9 |
| BUMPMAP toggles | o7 / v6 / TEX5 | o8 / v7 / TEX6 | o9 / v8 / COLOR1 | r10 |

The lower DEFAULT-toggle depth register is intentional. Reservation checks use
the exact three register/semantic sets, rather than assuming contiguous ranges.
All four PS retain the existing temporal temporary base r5. Original VS r0–r6,
authored VS r7–r9, shader-local DEFs VS c248–249 and PS c212–213, and the
existing temporal constants remain distinct. Base BUMP uses the final legal
PS input v9. COLOR1 is free in all 83 originals and uses the same separate
physical RGB register as the preceding TEXCOORD candidate. The original
COLOR0 declaration and alpha precision are retained; no mixed-register
COLOR0.w/COLOR1.xyz packing is used. SM3 permits ten interpolated
four-component attributes.
See Microsoft's [SM3 linkage rules](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/shader-model-3)
and [VS output declarations](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dcl-usage-outut-register---vs).

All six VS have independently proved position sources: r0, r1 or r2 with
matrix c0–3 or c24–27. Four interleave their position DP4 instructions with
unrelated work. The already-admitted ordinary-motion rows describe those exact
instruction sites and insertion boundaries. The combined transformation feeds
only immutable originals to that existing transformer, verifies copied spans,
and preserves every temporal insertion unchanged. It does not require a
contiguous position quad or alter the position constructor.

Exact sampler masks are DEFAULT `0x07` and BUMP `0x0f`. The public pair contract also reports the proved BUMP technique independently
of this mask, as described below; the previous sampler-mask helper remains
compatible. Live admission retains target, object/history and disabled-sRGB
requirements. The telemetry correction adds only a cached technique read to the
existing completed-route counter; it adds no per-draw pair lookup.

## Host qualification and remaining execution

Before edits, all **584** combined outputs from the 110-pair implementation were
captured over both depth modes and gains 0/1/4/16. The framed SHA-256 is
`8b15fc29c3f5bbc5b1389a317349457f9769b05098bbbd2b49d824c632ac34ff`.
The COLOR1 candidate intentionally changes one material RGB declaration
semantic token per program. Restoring only that known token for comparison
preserves byte-exact equality for all 584 outputs; the earlier 392/192/120/72
regressions use the same strict declaration-only normalization. No arithmetic,
register, write mask or precision flag is normalized away.

The transformer host module passes **12 tests**, constructing **664 variants**
and checking **13,224** driver assertions after the technique-contract extension.
It covers every pair and cross-pair,
configuration/input corruption, aliasing and failure rollback; independently
reconstructs original geometry, comments and alpha; checks temporal byte
identity and exact conversion boundaries; and checks legal register/constant
limits, full precision and signed-zero policy. The x86 core translation unit
cross-compiles with SSE2, the project's incoming-stack flags and `-Werror`.

The added cross-stage linkage audit checks **116 original pairs and 232
transformed pairs** (gain zero, both depth modes). Each stage has unique
semantics and disjoint masks in shared registers; every PS semantic and mask
is supplied by the VS; consumed semantic groups retain a mutually compatible
VS/PS register packing. These token checks establish declaration linkage,
not interpolation behavior under FLAT shading or nonzero texture-wrap state.

Maximum transformed weighted slots are VS **87/89** and PS **178/180** with
depth off/on; executable maxima are VS **76/78** and PS **133/135**. Asteroid's
own PS maxima are only **140/142** weighted slots. All are below the SM3
512-slot minimum, with non-unit flow, NRM, POW and DP2ADD costs counted.
A local create-time diagnostic measured 664 initial transformations in 8.93 ms
(about 13.4 microseconds each), and 184.3 ms including file I/O and negative
checks. This is host diagnostic time, not GPU timing or game FPS. The pair scan
adds six entries and is still consumed at cached shader-state changes.

The original-profile module has **42 passing focused checks**, including the
COLOR1 resource changes (41 passed in the module run; the affected historical
annotation comparison passed after its correction). Its Asteroid additions cover
440 executable original instructions with 3,364 direct opcode/operand/precision
mutations that bypass the fingerprint gate, plus 40 focused literal, relative
address, alias/toggle and ABI witnesses. The original arithmetic/site proofs
remain unchanged; COLOR1 availability and the selected material semantic are
explicit new resource annotations. Every original has a direct actual-declaration
COLOR1 collision witness that bypasses the fingerprint gate. Motion/depth
collision checks remain in place.

Seven new numerical reference tests pass, independently distinguishing
decode-before-weight, independent weights, cubic versus fifth-power response,
the absent outer factor of three, alpha/fog, point/material separation, shifted
AG axes, negative-q behavior and finite/half-precision endpoints. Both pending
reference-evidence crosschecks also pass: the four Asteroid contracts and the
unchanged 66 conventional-hull contracts. The seven numerical tests were not
repeated for that metadata check.

## Independent source review

The bounded review approves the six-pair pure transformer and its host proofs.
The four explicit ABIs preserve position, fog and alpha; decode base and detail
before their independent native weights; retain scalar specular and normal data;
and apply the proved directional, point-light and material-emissive equations.
All 584 preceding outputs remain byte-exact. Static instruction/register limits
remain legal, and transformation stays on cached shader creation rather than the
per-draw path.

One review finding was fixed. The class-C `494fe349b8bc12ec` /
`fffdabd910793aba` shaders are now described only as independent stage-local
transformation and material-rollback witnesses because their original VS/PS
linkage is invalid. The added linkage oracle passes all 116 original pairs and
232 combined depth variants, checking unique semantics, disjoint masks, PS-mask
inclusion and mutually compatible register packing.

The source correction below replaces the material RGB TEXCOORD6/7/8 semantics
with separate-register COLOR1. This removes material RGB from `D3DRS_WRAPn`
state and retains color shading behavior under `D3DRS_SHADEMODE`. Existing
motion/depth TEXCOORD wrap handling is a separate state-preservation change.
Actual D3D creation and interpolation still require qualification before
installation of the 116-pair set.

Qualify all six pairs together using actual original VS/PS in the existing
detached and live fixture workflows. Include base/toggle UV packing and both
position matrix locations, per-pixel basis/view inputs, detail weights 0/1 and
an interior blend, independent colored base/detail, specular and point/material
isolation, both depth modes, original alpha, motion/depth image identity and
separately linkage-valid fallback controls. The retained class-C VS
`494fe349b8bc12ec` / PS `fffdabd910793aba` host witness proves only independent
stage-local motion transformation and material refusal/rollback: the original
pair is native-linkage-invalid and cannot establish portable or live fallback.
The full preceding corpus remains
a regression requirement. No GPU creation, interpolation, live state/Reset,
native Windows behavior or gameplay performance is claimed by host checks.

## Explicit BUMP telemetry contract

The telemetry correction derives a pair's BUMP flag at compile time from its
proved pixel profile, independently of its sampler mask. A single exact-pair
lookup returns both fields. Shader setters and registration cache and invalidate
both together; the draw counter reads the cached flag without another lookup.
The existing sampler-mask API and emitted shader bytes remain unchanged.

The complete family matrix checks both fields for all 116 pairs and unsupported
cross-pairs. The focused host seam executes the actual route-counter block and
distinguishes conventional DEFAULT mask `0x0f`, Asteroid BUMP mask `0x0f` and hull
BUMP mask `0x1f`, including a non-material fallback. Equal-mask technique changes,
state-block/Reset resync, failed getters, and bound-registration invalidation
before object release are covered. The eleven transformer tests and focused
live-control test pass; both affected production translation units compile for
x86 with SSE2 and `-Werror`. Independent review approves the cached technique
contract and its lifecycle evidence. This correction changes no RGB semantic
and makes no new GPU/live execution claim.

## Separate-register COLOR1 candidate

Following the interpolation review, every material variant now declares its
existing full-precision XYZ varying as COLOR1 instead of TEXCOORD6/7/8. All
physical output/input registers and masks stay unchanged. Original COLOR0,
including its native partial precision and alpha, is untouched. The shader
contains no new saturation or arithmetic. COLOR1 is chosen to retain native
color shading behavior under FLAT and avoid texture-coordinate wrap state;
actual shader creation and interpolation qualification remain required.

The pre-change 664-output framed digest is
`f38849e8c5dedde5674aacd881f8eaae87e8d2eb3d67342e19a111c7582df40b`.
The new golden check verifies that every one of these variants differs only
at its single material DCL semantic token. All previous golden digests remain
checked after restoring precisely that token for comparison. The 116 original
and 232 transformed pair linkage checks pass with COLOR1, and the x86 core
translation unit compiles cleanly. Shader instruction budgets are unchanged.
Independent review approves this declaration-only source change and its bounded
collision, linkage and byte-regression proofs. GPU behavior remains unqualified.

This is separate from the Boron/Paranid mixed-register proposal, whose
synthetic X3 test found an alpha failure. No result from that experiment is
claimed as qualification of the 116-pair shaders. Existing live and detached
numerical expectations are unchanged because they inspect rendered outputs,
not the previous RGB semantic number; their next actual run must bind the
COLOR1 candidate and retain native-alpha, temporal and fallback checks.


## Main source integration

Pure checkpoint `bf62f6e` is integrated after the motion/depth WRAP correction.
The sole merge conflict was a host-double declaration; both rollback and
material-counter methods are retained. Independent integration review confirms
the cached BUMP contract and WRAP shadow/Reset recovery remain independent.
The ten affected live-control/WRAP host tests pass, including 34,611 WRAP
assertions. Detached GPU qualification now passes as recorded below; live verification
remains pending; the installed build still has the previous 110-pair implementation.

## Detached 116-pair X3 qualification (R1)

The single frozen whole-group run passed on 2026-09-13 in **X3**, CrossOver
Preview arm64 Wine (`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`), under the shared
Wine lock. The [compact result](../../verification/results/bottle-X3/linear-material-gpu.json)
binds all 83 original shader hashes, scoped source hashes, the explicit fixture
EXE and raw `/tmp/x3-linear-material-116-r1/report.txt`. Its terminal result is
`PASS cases=2757`; exit code is zero. The fixture EXE is 11,196,870 bytes,
SHA-256 `9e4836ae0a0a763768a6955616556d4056c917802da5ef00569a4f4c2cb1df7b`,
built with GCC 16.2.0 using the existing standalone SSE2/incoming-stack build.
All recorded source inputs still match the completed run. No game was launched.

This qualifies the actual separate-register COLOR1 variants over **116 pairs**,
both depth modes and **697 shader creations**. The 2,498 previously accepted
case payloads remain byte-exact; 259 appended cases include 256 Asteroid cases
(115 DEFAULT, 141 BUMP) and three old-family alternation controls. All six
Asteroid pairs exercise winding, independent gains, missing history, native
loop zero/one/eight and fixed-single point lights, fog/base alpha, base/detail
weights including the deliberately noncomplementary `2/.5`, and a patterned
detail texture that distinguishes both axes of the original 3x UV mapping.
Cubic highlights, the absent outer factor three, the second directional light,
grazing response, AG channels/negative-q, and geometric-point versus bumped-PS
lighting have independent numerical witnesses.

The run produced **24,813 samples** and **705,792 whole-target invariant pixels**,
with no alpha, motion, depth or finite-storage mismatch. Of the samples, 24,381
have float64/retained-sample-envelope RGB comparisons; 48 operational boundary
cases retain only finite capped RGB and exact alpha/temporal requirements.
The unchanged tolerance is `2e-5 + 0.006*RGB` (tiny-source absolute tolerance
`1e-12`); maximum tolerance fraction was **0.1614341**, with maximum envelope
error `0.00390625`. Each Asteroid technique's maximum fraction was `0.1599756`
and maximum error approximately `0.000976568`. Exact black and HDR checks
covered 4,617 and 3,609 sampled channels respectively. No tolerance was widened.

Diagnostic timings fence setup, then measure QPC through EVENT completion for
four managed-buffer draws, 98,304 submitted vertices and a 256x256 target,
without readback inside the timed window. Each mode/light-count cell has six
post-warmup samples; mode order alternates. The two new representative families
have these batch medians in milliseconds:

| Asteroid technique | Point lights | Original | Motion | Combined | Combined observed range |
| --- | ---: | ---: | ---: | ---: | --- |
| DEFAULT | 0 | 0.60530 | 0.67275 | 0.78210 | 0.7768–0.7868 |
| DEFAULT | 8 | 0.56395 | 0.67050 | 0.78620 | 0.7761–0.7920 |
| BUMP | 0 | 0.67760 | 0.78340 | 0.78915 | 0.7721–1.3547 |
| BUMP | 8 | 0.67175 | 0.79455 | 0.80525 | 0.7774–0.9076 |

All 13 family timing groups remain in the compact record. These measurements
include CPU submission, backend scheduling and completion; they are neither
isolated GPU timestamps nor a game-FPS estimate. The BUMP zero-light outlier
makes median-only performance claims inappropriate. Shader creation totaled
42.1504 ms in this process, also diagnostic rather than gameplay loading time.

The earlier pending-GPU statements describe the pre-run checkpoint; this result
supersedes them **only for the detached case bank above**. It does not resolve
the separate mixed-register alpha failure, qualify every FLAT/perspective/wrap
interpolation state, or prove live route/state/Reset/fallback integration.
Those live checks remain separate, as do native Windows execution, installation
and gameplay image/performance acceptance. The original experiment history and
unchanged ordinary-motion/reference evidence remain intact.

## Combined live qualification and installation

Candidate `75dbbed` is installed with all 116 pairs / 83 originals. Eight actual
D3D cells (ownership, TAA and materials off/on) pass 2,064 frames / 25,988 checks
and 5,160 state restorations. Each enabled cell uses the combined route on 247
frames, refuses exactly frames 2/3/4/5/7/9/13/15/19, and retires all 83 additional
shader references. The [compact result](../../verification/results/bottle-X3/linear-material-live.json)
keeps counts and links the retained full report instead of duplicating per-frame
shader/color/hash arrays. The actual execution uses the fixture seam; game
owner-memory binding, native Windows and visual acceptance remain separate.
The production DLL itself passed a load check. Source/build/rollback are bound
by the [install record](../../verification/results/linear-material-install.json).
