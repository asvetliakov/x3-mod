# Linear Boron and Paranid materials

Pure implementation candidate, 2026-09-13, based on `bf62f6e`. The transformer
adds all 32 Boron/Paranid SM3 DEFAULT and BUMPMAP pairs described in the
[native material contract](../reverse-engineering/boron-paranid-materials.md).
The complete group adds 12 VS and 20 PS, reaching **115 originals / 148 exact
pairs**. This work is not installed. Independent source review, GPU qualification
and live state integration remain
required; a successful pure transformation alone does not admit a game draw.

Preserve the native palette, geometry, reflection, alpha and light schedules,
converting each actual color source before its weighted accumulation. In
particular, Boron single variants mix four palette colors in the vertex shader;
decoding the resulting sum in the pixel shader is insufficient. Reuse existing
gain and compatibility-encoding policy. The objective remains complete material
coverage and subsequently whole-scene linear lighting, not a permanent mixed
gamma/linear endpoint.

## Selected implementation candidate

Use separate full-precision COLOR1 RGB. DEFAULT has a spare whole register.
For BUMPMAP, relocate the native scalar J from o8.x/v7.x to the unused W lane
of view o3/v2. Boron base also relocates u^11 from o8.y/v7.y to the unused W
lane of geometric normal o4/v3. Verify these exact identities, original masks,
producer sites and every consumer against all originals before editing.
Extend the existing single TEXCOORD declarations; reuse the vacated o8/v7
as whole COLOR1. Native COLOR0 and its alpha interpolation remain untouched.
Do not use the shared-register COLOR0/COLOR1 packing that failed the X3 probe.
Native scalar precision/centroid flags and native XYZ math remain unchanged.

This is selected for implementation and qualification, not yet established as
an accepted driver path. Keep the existing motion/depth fragments intact and
prove final limits and cross-stage linkage for every pair and both depth modes.

## Live interpolation state prerequisite

Relocating a native TEXCOORD scalar changes which WRAP component applies to it.
The live route must preserve that application behavior: read the original
scalar semantic's wrap bits and the destination semantics' states, replace
only each destination W bit with the corresponding original scalar bit, and
restore every changed state after the draw. Preserve destination XYZ bits and
the logical application shadow. Do not simply zero native scalar wrapping or
reject all nonzero wrapping as the final implementation.

Publish a bounded, cached transport contract with each exact material pair;
derive indices from proved declarations, not physical register numbers. The
ordinary generated motion/depth semantics still require their own temporary
zero wrap states. Integrate both requirements into the existing bounded route
transaction, reading all inputs before mutations and retaining its rollback,
first-error and successful-Reset recovery rules. Material-to-motion fallback
must cancel the material relocation while retaining ordinary temporal rules.

This mapping follows the documented per-semantic/per-component
[SM3 wrapping rules](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/shader-model-3)
and [texture wrapping states](https://learn.microsoft.com/en-us/windows/win32/direct3d9/texture-wrapping).
Matching scalar values and interpolation flags is necessary; actual GPU parity
at gradients, clipping and partial-precision boundaries remains an independent
qualification requirement on the X3 stack. Native Windows remains untested.

## Verification scope

Retain all 664 preceding transformed outputs exactly. Add all 32 pairs together,
with an independent palette/reference model, scalar lane isolation, untouched
native alpha/control flow, combined resource limits and original fingerprint
guards. Prove fixed palette RGB conversion never changes shared scalar lanes.
Use direction-dependent cube samples and unequal palette weights to distinguish
incorrect reflection directions, gamma-space sums and swapped palette axes.

The later detached and live GPU batches must cover the actual original pairs,
GOURAUD/FLAT, perspective/affine gradients, fog alpha, both face signs, point
loop/fixed paths, HDR gains, motion/depth, fallback and state recovery. Include
hostile native WRAP bits and exact scalar/native-alpha comparisons. These
qualifications remain pending while the user performs the exposure/bloom runs;
no extra gameplay request is needed to implement or test the pure transformer.

## Pure source construction and resources

The complete original-fingerprint/count guards and original-site checks precede
all changes. The existing row-explicit motion transformer receives immutable
originals; the merge verifies its copied spans and retains every temporal
insertion. All new motion rows are class B, including DEFAULT. No motion table
or ordinary-motion implementation is changed. Original comments and DEF vectors
remain byte-exact, including the affine preshaders and shared scalar lanes.
The maximum admitted input remains 1,392 DWORDs; the new group's maximum is
1,347, so it does not widen the read guard.

| New family layout | Material RGB | Native motion | Native depth | Scalar relocation |
|---|---|---|---|---|
| Both DEFAULT | o10 / v9 COLOR1 | o8 / v7 TEX6 | o9 / v8 TEX7 | None |
| Boron BUMP | o8 / v7 COLOR1 | o9 / v8 TEX7 | o10 / v9 TEX8 | TEX6.X to TEX1.W; base also TEX6.Y to TEX2.W |
| Paranid BUMP | o8 / v7 COLOR1 | o9 / v8 TEX5 | o10 / v9 TEX8 | TEX7.X to TEX1.W |

Every new COLOR1 declaration uses XYZ without PP or saturation. The existing
whole COLOR0 declaration and native alpha are retained. BUMP extends the
existing noncentroid, PP scalar-compatible view/normal declarations, removes
the vacated scalar declaration and changes only the enumerated scalar
producer destinations and consumer operands. Boron single palette RGB remains
in its native XYZ-only varying (DEFAULT o6/v5 TEX4, BUMP o7/v6 TEX5), with only
the PS palette declaration's PP permission removed. Its alpha and geometric
scalar carriers remain separate.

Immutable source palette colors are checked against the exact native float32
DEF lanes. Offline derivation decodes each component with the float32 exponent 2.2 and
rounds the result to float32; creation emits those checked immutable bits as
new local DEFs without a runtime math-library call. Enumerated RGB
operands alone change to these constants; a shared exponent/factor lane in the
original constant never changes. VS c240–243 supply Px/Py/Pz/Pu where present;
PS c204–209 reserve Px/Py/Pz/Pu/Pf/Pc slots with absent roles omitted. This costs
no runtime GPU arithmetic slots and introduces no per-draw constant upload.

VS authored scratch remains r7–r9; all new PS use r10 as transfer scratch,
r11 as final-encode target and r12/r13 as decoded directional RGB. Native PS
motion uses a proved base of r5 or r6; the depth path reaches at most r9. Palette
DEFs remain disjoint from transfer VS c248–249 / PS c212–213 and temporal
VS c252–255 / PS c216–220. Original source registers are independently checked.

The preserved grazing schedules matter outside unit-length interpolation:
Boron uses the signed multiply-chain fifth power of `1-abs(dot(V,R))`, while
Paranid's native POW evaluates `abs(1-abs(dot(V,R)))^9`. No new SAT or
normalization is inserted into that scalar path. The numerical reference tests
this distinction and the geometric-versus-bumped reflection/palette split.

The pair helper still returns sampler mask and BUMP technique, and now appends
at most two `LinearMaterialScalarTransport` records. Each records the proved
source and destination TEXCOORD indices/components; inactive records are zero.
Exact-pair lookup performs no allocation. The later live integration must cache
these records and implement the wrap-component transaction above before
admitting the new pairs. The pure source does not mutate render state or grant
permission to bypass that integration.

## Host evidence and performance boundary

The retained 664 outputs from `bf62f6e` remain **byte-exact**, without semantic
normalization. Their filename/byte-length/byte-stream framed SHA-256 is
`de4f0b34bb486b5d036dbd36c0d364a70f40eb7f611f4a8d9a6fc2db869c1c13`.
The earlier normalized historical goldens remain separate checks.

The structural driver exercises 920 variants (115 originals, two depth modes,
four gain sets), the complete 148-pair and cross-family matrix, original
corruption, invalid gains, input/output aliasing and unchanged output on
failure. The scalar-transport matrix distinguishes one and two relocations,
Boron TEX6 versus Paranid TEX7, and every unsupported cross-pair. Independent
bytecode checks reconstruct original alpha, geometry and comments, enumerate
palette DEF/source changes, certify the scalar declaration masks/precision,
and retain original and transformed cross-stage linkage checks.

Current maximum transformed weighted slots are VS **106/108** and PS
**188/190** with depth off/on; executable instruction maxima are VS **89/91**
and PS **139/141**. LOG/EXP remain one slot; NRM/POW, flow control, DP2ADD/LRP
and cube sampling retain their documented non-unit costs. The 512-slot SM3
minimum remains satisfied.

The focused transformer module passes all 15 tests (14 in the complete run,
then the single corrected report-schema test in its affected rerun). Its host
structural driver passes 28,774 assertions. The core cross-compiles for x86 with SSE2, incoming
four-byte stack alignment and `-Werror`. A preliminary local diagnostic measured
about 14.0 ms for 920 initial transformations before replacing palette CPU POW
with immutable decoded bits; this is shader-creation CPU cost,
not game FPS or GPU time. Scalar relocation adds no shader arithmetic; decoded palette
DEF emission and original validation occur only at creation. No Wine or GPU
run, live state qualification, installation or native Windows execution is
claimed by this source checkpoint.

The original-profile module covers 48 tests: 47 passed in its complete run;
one pre-existing Asteroid-only metadata filter was narrowed and its affected
test then passed. The direct proofs reject 23,970 executable-operand mutations
across all 1,723 new native instructions, 608 DEF-lane mutations and 64
instruction deletion/insertion witnesses without relying on the fingerprint
gate. The report contains 536 archive pass occurrences; the preceding 83
program and 116 pair records remain unchanged as parsed records. All 32
new alias/toggle groups and all 115 original COLOR1 collision checks pass.

The numerical reference adds 14 passing analytical tests, plus one passing
independent generated-evidence crosscheck for all 20 PS / 12 VS. Prior numerical
functions and profile maps remain unchanged. The crosscheck independently
checks fixed palette float32 bits, decoded DEF values, source lanes, lobe and
grazing coefficients, geometry/face/affine shapes and all program identities.
Only that pending crosscheck was run after report generation; the 14 accepted
numerical tests were not repeated.

Focused commands: `PYTHONPATH=verification/probe python3 -m unittest
verification.analysis.test_linear_material_transformer`, followed only by
`verification.analysis.test_linear_material_transformer.LinearMaterialTransformerTests.test_palette_definitions_are_decoded_before_weighted_uses`
with the same unittest invocation for the corrected schema reader. The x86 core
compile uses `i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -Werror
-msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 -c
src/renderer/linear_material.cpp` with its object stored under `/tmp`.

## Main source integration

The reviewed pure transform and scalar WRAP transaction are integrated together
after the fixed-exposure/comparison-control checkpoint. Production sources
merged without conflicts; both sides of the WRAP documentation were retained.
Six focused tests pass, including 34,773 WRAP assertions, the actual material
bind/cache flow, and exposure handoff. `motion_output.cpp` and `capture.cpp`
cross-compile with x86 SSE2 and the four-byte incoming-stack contract.
The installed candidate remains `75dbbed` with 116 pairs. The 148-pair source
has not been built into or installed as a gameplay DLL; its expanded detached
fixture and live qualification remain separate acceptance work.

## Detached 148-pair fixture source gate

The independent reviewer approved the detached fixture and oracle after the
bounded perspective correction. The bank contains 3,549 cases for all 148
pairs / 115 originals, retaining the prior 2,757 serialized cases exactly
(SHA-256 `04413dc21403cffbfe6f6d0ef7c97cef27265d6319af1ceb3062e2831a28dd5e`)
and the 240-byte case ABI. Its 58 operational boundary cases retain their
existing finite-storage / exact-alpha / temporal invariants without a float64
RGB-equivalence claim. RGB tolerances are unchanged.

New cases distinguish native palette source decoding before mixing, independent
J and u^11 contributions, unequal and out-of-simplex weights, directional and
cube responses, texture sampling, geometric point lighting, normals, gain,
fog alpha, pair/depth/winding and shader toggles. Forty affine-gradient cases
evaluate the original VS independently at each vertex before interpolation.
Eight additional perspective cases cover Boron/Paranid × DEFAULT/BUMP base
transport with both depth/winding states: clip W is 1/2/4, projected geometry
covers the same triangle, clip z is .5W, and previous projected X differs by
-.125. The oracle uses reciprocal-W interpolation of the native VS outputs.
Host witnesses distinguish this from affine interpolation beyond the unchanged
RGB tolerance and check all nine sample positions. WRAP state is explicitly
zero here; hostile WRAP relocation remains the separate live-fixture gate.

All 38 focused report/case-generation tests are accounted for: 36 passed in
the initial module run; two mock-report failures exposed float64 summation
rounding of constant vertex alpha. Retaining that constant exactly fixed both,
and the affected report checks plus expanded perspective/prefix/alpha checks
passed. No unrelated suite was repeated. `git diff --check` passes.

One approved standalone build completed in 2.649 seconds using MinGW GCC
16.2.0 and `verification/probe/build_linear_material.sh` (SSE2, x86 incoming
stack realignment, no fast-math). The retained EXE is 11,219,410 bytes with
SHA-256 `8b114e50ec91ab4d4f77ad78e6943b33678a425585e54e69e3171bfa92460027`,
locally at `/tmp/x3-palette-gpu/verification/probe/build/linear_material_fixture.exe`.
Build metadata is local at `/tmp/x3-palette-gpu-build.json`. This is a source and
standalone-build checkpoint only: no 148-pair detached GPU result, native
Windows execution, or live-route approval is claimed. The runner requires an
explicit prebuilt EXE and does not rebuild; X3 execution stays with the shared
Wine queue owner. The installed 116-pair candidate is unaffected.
