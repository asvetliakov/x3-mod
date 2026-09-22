# Sun-share extraction in converted materials

Derived 2026-09-15 for the material-lane prerequisite of
[directional shadows](../architecture/directional-shadows.md). Route B remains
ratified. This is an implementation contract, not an implemented lane or GPU proof.
No engine hook, RT allocation, replay or cascade contract is established here.

## Result and evidence boundary

The converted shaders do **not** retain the sun separately at the fill insertion.
The `Pixel::color_source` entries identify colour operands, not isolated completed
lobes. In all 108 reviewed PS originals, the sun-colour consumers are MADs whose
addend already contains another contribution. Reading their destination after the
MAD includes that contribution. Subtracting the other contribution later loses
precision and is unnecessary.

A host pass used `tools/analysis/inspect_motion_output_profiles.py`'s complete
instruction walker and operand decoder, the 94 `Pixel` rows in
`src/renderer/linear_material.cpp`, and all 14 rows derived by
`tools/analysis/inspect_xt_materials.py`. Local inputs:
`/tmp/x3-shader-sweep/programs/ps_<fingerprint>.bin`; all 108 FNV identities checked.
Measured: **152 sun MAD consumers**, 64 one-consumer programs and 44 two-consumer
programs. A branch-aware RGB-dependency pass found **zero operations outside
MOV/ADD/MUL/MAD** on the sun-to-final-colour path. Its conservative carrier count
peaked at three original RGB registers; its static dependent-operation count was
2–12 per program (both branch arms counted). These are source-analysis counts,
not measured generated-program slots or a validated register allocator.

Local reproducible inspection command from the repository root:
`python3 /tmp/x3-sun-contract.py`. Derived per-program consumer offsets are in
`/tmp/x3-sun-contract-derived.json`; the helper and original instruction details
remain local and untracked. The helper checks identities, not numerical output.
The equations below also agree with the existing material, glass and XT host
reference implementations. No Wine, compilation, build or game launch was used.

## Extraction contract

Call the complete sun contribution **S**. For ordinary material laws this is
`A_effective * D0`, where D0 includes the authored diffuse **and gloss** response.
For XT, S also includes the authored occlusion attenuation below. Sun colour is
already decoded and scaled by `direct_gain` in PS `r12.xyz`; `r13.xyz` is D1.
These colour carriers are not angular lobes. New fill uses r12 too but is not sun.

Maintain a parallel RGB contribution for the reviewed original radiance chain:

1. Immediately **before** each original sun-colour MAD, evaluate its multiplicative
   term using the current scalar operand and converted `r12`. Ignore the MAD's
   additive operand for this seed. Use `color_source[0:2]` when nonzero, or XT
   `lights` entries with `value == 6`; do not infer sun from instruction order.
   Multiple seeds in a two-light program are diffuse and gloss contributions,
   not two interchangeable completed sun lobes.
2. Follow only the seed's RGB descendants. Copy/multiply/add the sun contributions
   in parallel with the original MOV/ADD/MUL/MAD, using the same currently live
   scalar and albedo operands. A source with no sun dependency contributes zero.
   A MAD's reflection or vertex-colour addend therefore contributes zero, while
   the other operand's sun contribution survives. Do not duplicate texture reads,
   normal reconstruction, powers, palette evaluation or angular calculations.
3. At every original instruction, evaluate the parallel operation before an
   original destination can overwrite an input needed by that operation. Use the
   converted albedo/colour operands, not the encoded originals. Match the actual
   converted precision at each site; never restore `_pp` on full-precision RGB.
   Scalar/angular work retains its existing precision and signs.
4. Do **not** seed from injected fill, D1, point/emissive vertex RGB, cube RGB or
   lightmap RGB. Propagate through their combinations only where the sun-bearing
   operand survives. Process original sites plus a reviewed edit map; a generic
   search for every read of r12 would incorrectly classify fill as sun.
5. After the original `final_rgb` instruction has been redirected into r11,
   capture S and complete L **before** `transfer(... encode=true)` overwrites r11.
   L includes fill, all non-sun lighting and material tails. Preserve output alpha.

This is forward contribution propagation, not a general shader derivative: reject
any product with two sun-dependent multiplicands, sun-dependent scalar/control/
texture coordinate, nonlinear RGB operation or unsupported swizzle. Such a case
requires additional material proof. No engine ABI reconstruction is needed for
this shader-only contract.

## Original DWORD sites and family specifics

All offsets below address original program DWORDs, not transformed byte offsets.
The existing profile's `final_rgb`, texture and colour-operand offsets bind each
variant; the examples are witnesses, not permission to match unprofiled motifs.

| Family / witness PS | Sun seed instruction sites | Required continuation and final L |
|---|---|---|
| Hull DEFAULT two-light `8759c7838bbc86c2` | 1170, 1183 | Separate gloss/diffuse carriers receive mask/coefficient at 1188/1192; effective albedo r3 at 1237; ignore cube addend; total at 1251. |
| Hull DEFAULT one-light `593e5dea9b3457d5` | 220 | Seed scalar already includes combined authored response; ignore vertex-colour addend, multiply decoded r1 albedo at 241; total at 255. |
| Hull BUMPMAP `ca6bfa4a6cca7e2a` | 1225, 1230 | Mask/coefficient at 1243/1251; decoded affine albedo r4 at 1305; total at 1319. |
| Glass two-light `a66fb1981ba755b2` | 245, 250 | Mask/coefficient at 267/271; diffuse r0 at final MAD 299; cube/Fresnel r2 addend is excluded. |
| XT DEFAULT `fffdabd910793aba` | 1497, 1510 | Coefficients/mask at 1506/1515/1519; branch albedos at 1587 or 1599; final MAD 1638 multiplies material+reflection by occlusion scalar r2.z, then adds lightmap. |
| XT terraformer BUMP `d22f2ce2c740e6a7` | 1500, 1513 | Coefficients/mask at 1509/1518/1522; branch albedos at 1615 or 1627; occlusion r3.w at 1665; final ADD 1675 includes separately decoded occlusion RGB. |

**Asteroids:** use the effective base/detail albedo product, including authored
weights. Their final colour is an albedo multiply without the hull cube/lightmap
tail. All six original PS rows were included in the 108-program pass.

**Boron/Paranid palette families:** use the actual effective palette-weighted
albedo factor at the existing material multiply, not simply the first decoded
texture. The cube reflection can itself be base/palette tinted; that does not
make it part of S. Existing references preserve family gloss powers and masks;
reuse live scalar factors rather than reconstructing a universal Lambert model.

**Glass:** all four PS originals (six pairs) admit extraction. Its sun response is
`decode(diffuse) * decode(Color0) * direct_gain *
(0.5*cosine + 3*mask*q^6*saturate(3*cosine))`.
The separate `cube_coefficient*mask*fresnel*decode(cube)` is excluded. Material
extraction does not decide whether blended glass is an eligible receiver or how
its share interacts with destination colour. A shader's own L is not the blended
framebuffer L; that integration question remains with the parent.

**XT (14 PS, including six terraformer):** propagate both palette branches.
S is `tint * sun_lit * pow(occlusion.a, occlusion_strength)`, where tint is base
or the actual palette-weighted base from the executed branch. The same occlusion
multiplier attenuates reflection in total L; reflection itself stays outside S.
Terraformer's additional decoded occlusion RGB and lightmap emission stay outside
S. Capturing before occlusion without applying that scalar would overestimate the
sun share. Damage/decal and two-sided paths retain their existing base and normal
selection; do not invent an independent branch decision for the numerator.

## Registers, boundaries and cost

- The PS shader-local lifetime runs from the first sun MAD through the last RGB
  operation and reduction. Original PS registers end at r6; motion relocation
  accepts only r0–r2 and shifts them by a base no higher than 7 (up to r9).
  Conversion uses r9/r10 scratch, r11 final radiance, r12/r13 light colours,
  and XT r14/r15 palette/default policy scratch. Do not reuse these carriers
  merely because one source variant currently appears to leave them dead.
- A simple initial allocation is **r16–r22**, one parallel RGB register per
  original r0–r6, plus **r23** for the final contribution/reduction. It is inside
  the 32-register ps_3_0 limit and requires no varying or sampler. Eight reserved
  registers is a conservative implementation budget, not the minimum. The three
  observed live carriers suggest subsequent compaction is possible, but require
  a real branch-aware liveness proof before lowering that budget.
- Reserve one checked-free shader-local constant, for example **c221** containing
  luminance weights in xyz and epsilon in w. c204–209 hold palette colours,
  c210 XT policy, c212–213 transfer/gains, c214 detached fade, c215 fill and
  c216–220 motion/depth. Check c221 across declarations, DEFs and operands in the
  actual combined program; being absent from the current emitters is not a
  replacement for ownership validation. No CPU upload is needed.
- A guarded scalar reduction requires two DP3s, MAX(epsilon), RCP, MUL_SAT and
  the lane write: **six instructions**, plus any required finite-domain checks.
  The extraction skeleton adds the observed 2–12 dependent operations before
  allocation/branch-copy overhead. The architecture's approximately five slots
  is therefore not a complete material-lane budget; measure every transformed
  variant against 512 slots, including motion/depth and fill.
- The output-side sanitizer currently clamps RGB to [0,65504] before encoding.
  Define denominator policy explicitly: for the supported nonsaturated finite
  domain, `f=saturate(luma(S)/max(luma(L),epsilon))`. If reproducing displayed
  luminance under clipping is required, use the same sanitized L as the encoder
  and retain saturation of f; the resulting factor no longer equals an exact
  unclipped shadowed-law ratio. NaN/overflow handling needs the same explicit
  finite-domain qualification as colour. The epsilon guard prevents zero/zero;
  it is missing from the five-operation count in the design note.
- Do not append the share after an unreviewed whole-register oC2 write: the future
  lane writer must preserve depth .r and ensure no later depth fragment clears
  .g. That output ordering is an integration acceptance requirement, not proved
  by this extraction pass. Detached distance-fade producers and fused branches
  likewise need explicit admission and lifetime review; do not silently broaden
  the lane to those variants from the ordinary-pair proof.
- Shader invocations are independent and scratch is invocation-local. No host
  shared state, callback, reentrancy or CPU/LastError boundary changes are needed.

## Next implementation boundary and refusals

Implement a hash-bound per-original parallel RGB edit plan, with original
instruction boundaries and converted operand mappings validated before emitting.
Track definite initialization and lane ownership across each IF/ELSE/ENDIF;
reject an uninitialized branch contribution, unknown input, unknown original,
ambiguous seed, changed instruction signature, ownership collision or resource
overflow. Preserve existing alpha/discard and all material colour outputs.
Do not silently report a valid zero share for an unproved material.

First acceptance should cover all 108 PS originals in both existing depth modes:
structural mutation refusals; combined temp/constant/slot accounting; zero-sun
identity with fill/D1/point/cube/emission independently nonzero; diffuse and gloss
sun isolation; palette branches; XT damage/occlusion/terraformer emission; black
and near-epsilon totals; unchanged colour/alpha. Follow with the owner's detached
GPU oracle. This note proves original dependency coverage and representative
sites, not the emitted bytecode, GPU numerical tolerance, blend semantics or
native Windows runtime behavior.

## Source extraction checkpoint (2026-09-15)

The explicit `linear_material_pixel_variant_sun_share` API now implements the
ordinary-program extraction above. It has no callers in the live renderer and
changes neither configuration nor resource allocation. Existing APIs remain
byte-identical. `extraction_applied` describes the program proof, published only
after successful output replacement; a proved shader can still emit invalid
share for an individual pixel. Unknown/malformed originals retain existing
transformation refusal and output rollback. A refused contribution plan on an
otherwise transformed known program publishes exactly -1 instead of false zero.
The fade APIs never select this path. Depth-off generation does not establish
valid `.r`; receiver admission still requires ordinary same-draw depth.

The create-time edit map uses actual converted operands at original sites.
All parallel operations precede their original destination write. Registers
r16–r22 follow original r0–r6, r23 retains final S/share; explicit initial zeros,
kill writes and IF/ELSE/ENDIF state joins preserve definite initialization.
Reduction reuses dead r16/r17, checks the ratified component domain and nonzero
sub-epsilon exclusion, then retains its result in r23.w until the sole final
`oC2.g` MOV at END. No later depth fragment can overwrite it. c221 holds the
luminance coefficients and exact 2^-20 epsilon, with declarations, definitions
and source operands checked for collisions. The helper uses 21 reduction
instructions plus the final output MOV; these include invalid-domain guards.
Exact -1 uses saturation of the existing 2.2 literal followed by negation,
without assuming exact reciprocal precision. Added instructions honor the
[documented ps_3_0 register read ports](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-3-0).

Focused host acceptance command:

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_linear_sun_share verification.analysis.test_linear_material_fill (deleted in cleanup batch 3, 2026-09-22; historical command: test_linear_material_fill)
```

Coverage: 108 originals / 152 sun MADs, both depth modes and fill 0/.06 produce
432 successfully extracted variants. Baseline instruction sequences remain
verbatim and in order; the separate frozen fill test retains 1,388 ordinary
outputs with SHA256 `3db6100189f38fa0b6300f6ff38c6871aa1a9adf59b5bd1bd91e93009c31dde8`.
Combined weighted slots are 136–342 of 512, adding 32–45 slots including the
21-instruction reduction, initialization/kills and output. Shader temporaries
remain within r0–r23; no sampler, varying, host upload or per-draw work is added.
Create-time latency, GPU execution cost and register-pressure cost are unmeasured.

The numerical oracle executes actual converted tails in the existing float64
XT interpreter. Ordinary output minus an independent ordinary execution with
only the original sun MAD terms zeroed supplies its numerator; the injected
c215 fill MAD retains r12. 432 depth-on tail comparisons cover two fill settings
and both boolean branch choices, preserve color/alpha/depth, and agree on share.
Additional tests execute 108 genuine zero-sun cases and 152 individually enabled
authored sun lobes, and compare fill on/off numerator invariance in both arms.
The 39 synthetic planner/resource checks include dependent swizzle/nonlinear
refusals, branch kills, missing/duplicate seed sites and temp/constant collisions.
Forced failure of every allocation before success covers 75 hull and 101 XT
allocation points, requiring unchanged output and a false extraction flag.
The authored reduction has 28 black/epsilon/component/NaN/infinity/clip cases
and eight scalar luminance-identity comparisons. Nonfinite cases relax only the
host interpreter's input adapter; they establish no GPU exceptional-value claim.
These are tail tests, not full angular/texture/rasterization or native execution.

**Known inherited native shader-validity blocker:** ordinary conversion's
`sanitize` emits a MAX of a source constant and c212.y, e.g. original shader
`02606104fa59fb29` decodes c5 through `MAX r12, c5, c212.y`. This reads two distinct
constant registers although ps_3_0 documents one constant read port. All 108
ordinary programs contain an inherited constant-port violation. The required
byte-identical off path is preserved here; accepting this host extraction
artifact does not establish native-valid combined shaders. The orchestrator
must track and repair this separately in
[platform-portability.md](../architecture/platform-portability.md) before native
shader validity can be claimed. Native D3D creation, G32R32F mixed MRT writes,
GPU precision/NaN semantics, discard and blend coverage, transactional resource
publication, Reset/recovery, consumer integration and game evidence remain
outside this checkpoint. No game, Wine, production DLL build or install ran.
