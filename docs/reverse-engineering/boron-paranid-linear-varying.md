# Boron and Paranid BUMP linear RGB varying

The original bounded read-only study, 2026-09-13, used the isolated Asteroid
worktree from `10e447b`: complete Boron BUMPMAP six pairs and Paranid BUMPMAP ten
pairs, six distinct VS and ten distinct PS. That study made no production or
profile changes. Its declaration proposals and reasoning below are retained as
**historical pre-qualification analysis**, not the current recommendation.

**Current decision after synthetic X3 R1–R3:** same-register semantic packing
failed native-alpha preservation and is abandoned for the production route.
Separate physical COLOR1 RGB passed the bounded synthetic qualification while
retaining the whole original COLOR0/alpha declaration. The accepted
[compact R3 record](../../verification/results/bottle-X3/varying-split-separate-gpu.json)
preserves both earlier failures and distinguishes native parity from documented
FLAT conformance: this X3 stack reports FLAT state but interpolates native COLOR
as Gouraud. Native Windows remains unverified.

The Boron/Paranid candidate therefore needs a **separate physical COLOR1 RGB
varying**, with bounded scalar-varying relocation to recover a register where
necessary. This synthetic record proves neither that relocation nor actual game
programs. Their producer/consumer, precision, centroid, linkage and resource
proofs belong to their material qualification. Do not adopt mixed COLOR0.w PP /
COLOR1.xyz full precision merely because masks are legal or shader creation
succeeds. The earlier TEXCOORD9 packing proposal is historical and additionally
requires known GOURAUD and WRAP9=0.

## Original producer and consumer contract

All six VS declare o1 as COLOR0.xyzw with no destination PP or explicit centroid
flag. Exactly one instruction writes o1.xyz, and two mutually exclusive fog
branches write o1.w. All ten PS declare v0 as COLOR0.xyzw with PP and no explicit
centroid flag. Exactly two instructions read v0: an RGB-only saturated PP move,
and the final independent PP alpha multiply. The move's identity source swizzle
is xyzw, but its XYZ destination means it consumes only RGB. There are no other
v0 reads, relative accesses, mixed-lane color consumers or RGB-to-alpha links.

| VS | RGB producer DWORD | Fog alpha / plain alpha DWORDs |
| --- | ---: | --- |
| `57392213f62fef19` | 464 | 539 / 544 |
| `5c17a381b149b3b9` | 514 | 539 / 544 |
| `a804f173f693944a` | 465 | 491 / 496 |
| `33388c8897d428a5` | 464 | 539 / 544 |
| `b4059ab6af8fc529` | 464 | 539 / 544 |
| `2a560f246c90fa64` | 412 | 488 / 493 |

Loop VS RGB adds the accumulated point contribution to c40; fixed-point variants
MAD the point contribution with its response and c19. The new linear computation
must convert point colors at their individual sources and preserve the scaled
material-emissive contract. The existing native alpha writes remain untouched:
loop paths multiply a fog response by c39.x or move c39.x; fixed paths use c18.x.
The extra color-weight and basis outputs are not COLOR0 and remain native data.

| PS | RGB clamp DWORD / destination | Alpha multiply DWORD |
| --- | --- | ---: |
| `62c180abe017e239` | 410 / r4.xyz | 521 |
| `a910daef935891ce` | 384 / r4.xyz | 495 |
| `ed44232013f67072` | 290 / r3.xyz | 383 |
| `f286856c3f400377` | 316 / r3.xyz | 409 |
| `188c5ab9dbb98393` | 1230 / r3.xyz | 1342 |
| `18d372968af4a480` | 1204 / r3.xyz | 1316 |
| `43c9405568d2226f` | 1230 / r3.xyz | 1342 |
| `7e5e41276b3d7514` | 1204 / r3.xyz | 1316 |
| `5e056627e9ff3a8d` | 302 / r3.xyz | 397 |
| `fce465befff2f623` | 328 / r3.xyz | 423 |

Every listed alpha instruction writes only oC0.w from r2.w and v0.w. The sampled
alpha/interpolation producer still needs its ordinary full-family proof, but
there is no reason to route its vertex-alpha operand through a new register.
These masks/modifiers come from actual decoded tokens, not disassembler labels.

## Documented declaration rules and limits

SM3 PS inputs are ten floating-point v-registers; declaring one as color does
not impose the older shader models' [color clamp](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-3-0).
The existing explicit MOV_SAT is the clamp that must be replaced for linear RGB.

Microsoft permits multiple semantic declarations on disjoint masks of one
register, with matching VS/PS packing. A semantic itself must not be declared
repeatedly or straddle registers. This supports one COLOR0.w plus one distinct
TEXCOORD9.xyz, not two differently modified COLOR0 declarations. See the
[PS declaration contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dcl-usage---ps)
and its matching [VS packing example](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dcl-usage-outut-register---vs).

PP is permission to use reduced precision, not a required rounding operation;
implementations may ignore it. An unmodified texture-coordinate declaration
requires full-precision transfer. Therefore removing PP and inserting MOV_PP
later does not recreate the original interpolation precision. Color inputs also
receive centroid sampling implicitly; explicitly matching that sampling on the
new RGB semantic avoids introducing an unnecessary sampling change. These are
separate properties of the [precision and centroid contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-modifiers-ps-2-0).

## Historical TEXCOORD9 candidate ABI (state-restricted)

Derived declaration sketch, not extracted shader text:

```text
VS: COLOR0.w -> o1.w; TEXCOORD9.xyz -> o1.xyz
PS: COLOR0.w, PP -> v0.w; TEXCOORD9.xyz, centroid, full precision -> v0.xyz
```

In bytecode terms, narrow the original COLOR declaration mask to W while retaining
its existing flags; add the distinct XYZ semantic declaration to each stage.
Preserve the original alpha producer, fog branches, v0.w operand, final alpha
instruction and all native data declarations. Compute new linear RGB into o1.xyz,
then replace the sole original MOV_SAT_PP RGB consumer with a full-precision move
from v0.xyz. This changes the intended color path without moving alpha or changing
its semantic/precision. No extra VS output or PS input register is consumed.
TEXCOORD9 is unused by the original declarations and motion/depth semantics in
all 16 pairs; it is a semantic index, not an attempt to allocate PS v10.

Ordinary motion remains VS o9/PS v8, with TEXCOORD7 for Boron or TEXCOORD5 for
Paranid. Current depth remains VS o10/PS v9/TEXCOORD8. The candidate leaves both
fragments and all their operands unchanged in both depth modes. It also avoids
rewriting packed palette weights or geometric inputs. Material mode must still
publish the matched combined pair; an unconverted PS must never receive the new
VS declaration contract merely because it shares an original VS identity.

This construction requires known GOURAUD shading and WRAP9=0; the COLOR1 study
below explains why. The documented packing rules are not evidence that
our assembler/driver accepts differently modified disjoint declarations on one
physical register, nor that changing sibling lanes leaves native alpha bit-exact
on the current implementation. Qualification should bind that narrow uncertainty:
synthetic same-register COLOR.w PP + TEXCOORD.xyz full precision, ordinary versus
explicit centroid under no-MSAA, alpha/fog gradients near partial-precision
boundaries, and independent HDR RGB values above one. Require unchanged alpha
and RT1/RT2, expected full-precision RGB, exact preserved declarations/alpha
instructions, both depth modes and original-pair fallback. Native Windows
execution remains a separate unverified target even if X3/CrossOver passes.

## Historical comparison of the other constructions

Keeping the original full COLOR0_PP declaration and just writing linear RGB to
it retains reduced-precision transfer permission. Removing PP from the whole
register changes alpha's interpolation contract. Splitting COLOR0 itself into
two repeated semantic declarations contradicts the declaration restriction;
overlapping an unchanged COLOR0.xyzw with new TEXCOORD9.xyz also does not provide
a disjoint-mask construction.

Spare component capacity does exist elsewhere. Boron has v1.xy, v2–v6.xyz,
and v7.xy at base or v7.x in singles; Paranid has v1.xy, v2–v6.xyz and v7.x.
Unused ZW/W lanes can be proved by the original instruction dependencies, but
the existing declarations carry PP and some carry centroid. Reusing those lanes
would require the same mixed-semantic precision proof plus more writes, source
swizzles and family-specific packing. It offers no simplification over splitting
an already independently consumed RGB/alpha register.

The current motion varying uses four previous-clip components. Depth reads two
components (current clip Z/W) while the VS duplicates them as Z,W,Z,W. Depth's
unused PS ZW pair could carry two RGB components after coordinated export changes,
but not three; another lane or a new depth/motion encoding would still be needed.
Packing floating-point channels into one scalar would change range/precision.
Dropping depth in this family or replacing exact perspective depth with an
unproved ratio interpolation is not a completion of the existing TAA contract.
The same-register color split is the smallest candidate preserving both temporal
fragments; retain lane packing as a fallback if actual declaration validation
rejects the proposed mixed modifiers.

## Original source-study evidence scope

The local audit covers 6/6 original VS COLOR declarations/writes and 10/10 PS
COLOR declarations/reads across the complete 16 pair union. The original and
motion/depth semantic inventories prove TEXCOORD9 is free in both stages for
all 16 pairs. Derived audit data and the bounded query helper remain under
`/tmp/x3-linear-varying/`; original programs remain in the local archive corpus.
This note establishes a source-level candidate and its qualification boundary,
not implemented linear materials or measured performance.


## Historical COLOR1 packing proposal and primary contract

Follow-up requested by the root reviewer before fixture expansion. All original
COLOR declarations were enumerated again: each of the six VS and ten PS declares
only COLOR0, so **COLOR1 is free across all 16 pairs**. The temporal fragments
introduce TEXCOORD semantics only. Use the same disjoint masks and physical
packing in both stages:

```text
VS: COLOR0.w -> o1.w; COLOR1.xyz -> o1.xyz
PS: COLOR0.w, PP -> v0.w; COLOR1.xyz, no PP -> v0.xyz
```

The semantic names differ by usage index; this does not repeat COLOR0. Native
alpha still uses its original COLOR0 semantic, W component, PP permission,
producer and final instruction. RGB alone changes source math and drops the
explicit clamp/PP operation. No extra interpolator, temporal packing, render
state override or palette-weight relocation is required by this candidate.

The [SM3 shading rules](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/shader-model-3)
state that COLOR attributes are flat-shaded when SHADEMODE is FLAT. They warn
that non-COLOR components sharing a register with COLOR have undefined flat
interpolation. Both proposed semantics are COLOR, so that mixed-class warning
does not apply. The same page assigns WRAP0–15 behavior to TEXCOORD attributes;
COLOR1 introduces no WRAP9 dependency. Accordingly, the original TEXCOORD9
proposal was incomplete without the GOURAUD/WRAP9=0 boundary. COLOR1 is a better
candidate for preserving native interpolation behavior under both shading modes.

The [modifier documentation](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-modifiers-ps-2-0)
assigns implicit centroid to any color semantic, so COLOR0 and COLOR1 retain the
same color sampling class without adding explicit centroid to a non-COLOR input.
This does not establish MSAA acceptance for the renderer's current no-MSAA gate.

**Precision conclusion:** the SM3 register reference describes unified v-registers
as fully floating point without color clamping, and the SM3 precision section
makes full precision the default unless PP requests otherwise. These jointly
support interpreting a COLOR1 declaration without PP as full precision; that is
an inference from the model-wide rules, not a quoted COLOR1-specific guarantee.
The declaration-specific sentence that explicitly requires full-precision
transfer without PP names texture coordinates. Neither the sources inspected
nor their declaration examples explicitly settle mixed COLOR0_PP/COLOR1-full
precision on one physical register. Also, full floating-point range alone does
not prove mantissa precision. A successful HDR-above-one draw alone therefore
cannot close this question.

The pre-R1 study recommended COLOR1 as a **qualification candidate**, retaining the
state-restricted TEXCOORD9 comparison. Compare same-register COLOR0.w PP /
COLOR1.xyz full against an independently declared full COLOR1 input, and compare
alpha against the unmodified COLOR0_PP reference. Include GOURAUD and FLAT,
nonuniform vertex RGB/alpha, values between half-float steps, and HDR values
above one; exercise the PP-ignoring implementation possibility without claiming
that it demonstrates hardware which actually uses reduced interpolation.
Original alpha instructions/declaration flags and RT1/RT2 still require exact
checks. Root owns adoption; the fixture owner has been given this conclusion.
No Wine, fixture, production or shader-profile changes were made by this study.

For the fixture's triangle-list FLAT oracle, use the first submitted vertex of
each triangle, not any constant color that happens to be returned. The D3D9
[triangle interpolation rule](https://learn.microsoft.com/en-us/windows/win32/direct3d9/triangle-interpolation)
uses the first vertex's color, and the archived Microsoft DirectX
[ShadeMode definition](https://learn.microsoft.com/en-us/previous-versions/ms858170%28v%3Dmsdn.10%29)
specifies triangle-list triangle i's first vertex as i*3. The single-triangle
fixture therefore uses submitted vertex zero. Preserve the independent native
COLOR0 alpha comparison rather than importing fixed-function fog/specular-alpha
exceptions into the SM3 color-semantic test.

## Synthetic qualification and retained failures

The authored fixture, runner and focused host tests are
[varying_split_fixture.cpp](../../verification/probe/varying_split_fixture.cpp),
[run_varying_split.py](../../verification/probe/run_varying_split.py) and
[test_varying_split.py](../../verification/analysis/test_varying_split.py).
Thirteen focused host tests pass. They check independent declaration mappings,
unchanged alpha/fog and temporal instruction words, rejected malformed contracts,
state/classifier ambiguity, and precision gradients that would reject two equally
half-rounded reference paths. Host refusals are not device-validation claims.
No extracted game shader bytes are tracked.

R1 successfully created all 28 authored shaders, then failed its first case:
all 1,512 covered pixels in every same-register packed variant had alpha bits
`0x7fc00000`. Dedicated references retained native alpha; full RGB and RT1/RT2
matched. The failure remains under `/tmp/x3-varying-split-r1`. The optional
13-program packing diagnostic was not executed because that production route
was abandoned.

R2 used separate COLOR1 and passed all 32 Gouraud cases. Its first requested-FLAT
case failed the documented first-vertex RGB oracle. Native COLOR0 and separate
COLOR1 RGBA were each bit-identical to their corresponding Gouraud draw, including
1,512 distinct alpha values. This was not a candidate-only interpolation change
or a different provoking vertex. The failed run remains under
`/tmp/x3-varying-separate-r2`; its tolerance and verdict were not changed.

R3 ran a native-only fresh FLAT → GOURAUD → FLAT classifier before the matrix.
Set/Get state values stayed 1/2/1 before and after each draw. The classifier uses
nonuniform native alpha with first-vertex fog below zero, for which documented
flat alpha is exactly positive zero at any permitted PP precision. It requires
either that exact zero reference or exact agreement with a nonconstant Gouraud
reference, plus exact repeated-FLAT stability; all other behavior fails. Candidate
RGB never selects the CPU oracle. The measured native alpha matched Gouraud in
all three draws, so the record says **`native_flat_conformance=false`**.

The separate-only matrix then passed 64 cases: 32 requested Gouraud and 32
requested FLAT, all 64 classified effective Gouraud. Both depth modes, fog/plain,
affine/perspective interpolation and four precision/HDR gradients are covered,
without MSAA. Twelve shader creations succeeded. The 192 qualification draws
plus three classifier draws produced 420,096 independent CPU RGB checks,
786,432 exact alpha comparisons and 1,572,864 exact RT1/RT2 vector comparisons.
Maximum error was 0.040770857 of the unchanged `1e-12 + 64 * FLT_EPSILON * |RGB|`
envelope. The envelope is a predeclared experiment bound, not a universal hardware
accuracy guarantee. Partial precision may execute at full precision; no PP/full
difference is required.

R3 exited zero in 4.563 seconds; this is fixture completion time, not a renderer
or gameplay performance measurement. Raw files remain under
`/tmp/x3-varying-separate-r3`. The same independent reviewer approved source,
oracle correction and final evidence. The compact record binds the executable,
scoped source hashes, raw report and prior failure witnesses.

This accepts separate COLOR1 parity and precision under the observed native
interpolation, not backend FLAT conformance, native Windows runtime, MSAA,
nonfinite inputs, scalar relocation, or complete material/live-route behavior.
The authored reference relocates a zero filler to retain ten occupied PS input
registers; it is not a solution for the ten-input Boron/Paranid originals.
