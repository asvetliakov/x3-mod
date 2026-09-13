# Glass material conversion contracts

Derived archive review and source implementation, 2026-09-14. **Six SM3 glass
pairs now have a reviewed opaque material conversion in main source and pass
detached X3 GPU qualification. Focused live routing also passes; installation remains pending.
The other 24 pairs remain unimplemented.** Actual
blend state decides whether a draw instead needs ordered composition. The family
name does not establish transparency. The archive study covers uncaptured
profiles as well as the Run 27 pair; source evidence is recorded below.

## Evidence and complete boundary

Inputs are the existing local `/tmp/x3-shader-sweep/manifest.json`, `programs/`
and `disassembly/`, the tracked [motion inventory](../../verification/results/motion-output-profiles.json),
and a targeted reread of glass entries in the installed X3 CAT/DAT archives
using `tools/analysis/effect_passes.py`. The archive study ran no game or Wine command. Raw
shader bytes and listings remain local and untracked.

The reread confirms **144 DEFAULT pass occurrences, 128 distinct effect byte strings,
30 exact VS/PS pairs, 18 VS and 16 PS**. These reduce to 13 VS and 15 PS GPU
bodies; every body was inspected, with duplicate bodies checked by the sweep's
GPU-token digest. No glass program has a D3DX shader preshader. All six profile
directories, four toggle directories, and four aliases are included. There are
6 SM3 pairs, 12 SM2 pairs, 6 VS1.1/PS1.4 pairs and 6 VS1.1/PS1.1 pairs. No glass
BUMPMAP, INSTANCE or incomplete pass exists in this archive population.

The table enumerates every pair: each VS in a cell pairs with that row's PS.
`V` means base plus `hueshift_off`; `L` means `hue_lights_off` plus
`v_lights_off`. Base and `2s` aliases use all four toggle directories. For
`_0000/_0001`, a two-VS cell lists V first and L second. These are exact
complete-program FNV identities, suitable as game-program keys after structural
validation, not backend/DLL prerequisites.

| Directory | Alias | VS identities | PS identity |
| --- | --- | --- | --- |
| 3_0 | glass | `c30104cb0efb6675` | `a66fb1981ba755b2` |
| 3_0 | glass2s | `c30104cb0efb6675` | `ebc9b2b3f1564e9a` |
| 3_0 | glass_0000 | `e2ad860d5fbb3e59`, `74fdc00d802b4027` | `f31c9e2701c8eee4` |
| 3_0 | glass_0001 | `e2ad860d5fbb3e59`, `74fdc00d802b4027` | `9d49f288800f898d` |
| 2_b | glass | `cc2619826fd9b9f5` | `3c7c14b76b1bf8ef` |
| 2_b | glass2s | `cc2619826fd9b9f5` | `ef879a8c1d09a180` |
| 2_b | glass_0000 | `536967a7f4662f85`, `edddf585d48fa834` | `f8089d4363f51236` |
| 2_b | glass_0001 | `536967a7f4662f85`, `edddf585d48fa834` | `66cea7b0cf59a601` |
| 2_0, 2_a | glass | `cc2619826fd9b9f5` | `e6a26abf51f93e0b` |
| 2_0, 2_a | glass2s | `cc2619826fd9b9f5` | `bf897ecca953bc83` |
| 2_0, 2_a | glass_0000 | `536967a7f4662f85`, `edddf585d48fa834` | `37e6a5efda1d960a` |
| 2_0, 2_a | glass_0001 | `536967a7f4662f85`, `edddf585d48fa834` | `77a80ea5d6b097e4` |
| 1_4 | glass | `53dc165ea2c8ff8b` | `061889835cc5241f` |
| 1_4 | glass2s | `fe632f07da981d95` | `061889835cc5241f` |
| 1_4 | glass_0000 | `d43bb3c748076f13`, `6bd20a6a6e041250` | `2ff8848b2d0d08f9` |
| 1_4 | glass_0001 | `3a4221f5de4a031d`, `5f5f707f8d5edfe2` | `2ff8848b2d0d08f9` |
| 1_1 | glass | `a4dd2cc4645e6d71` | `e57a3bb1ed14fa5d` |
| 1_1 | glass2s | `b442f7323c4f690b` | `e57a3bb1ed14fa5d` |
| 1_1 | glass_0000 | `7395fa29a014ba89`, `e3c015f8fddced22` | `0e311fdab3c8ea49` |
| 1_1 | glass_0001 | `b6bc149c61d348c3`, `f8752119d5fec39c` | `0e311fdab3c8ea49` |

Only one VS and one PS appeared in the sweep's historical runtime corpus. That
observation does not exclude the remaining 29 pairs from conversion scope.

## Actual opacity and effect state

[Run 27](../verification/run27-glow-selection.md#material-coverage-and-fallback)
records 1,700 sampled material refusals, all for
`c30104cb0efb6675/a66fb1981ba755b2`. Every detailed captured refused draw has
Z writes enabled and alpha blending disabled. Several nodes use the pair.
This establishes an unsupported **opaque** population; it does not identify
which on-screen object changed brightness or prove that glass caused the user's
gloss complaint. A captured node changes LOD while keeping the same pair and
native fallback, so these records do not demonstrate a route transition.

All 144 effect parameter sets default to alpha blending off, alpha test off,
Z writes on, source/destination ONE/ZERO, separate alpha off, ADD operations,
alpha source/destination ZERO/ZERO, color-write mask RGB (7), cull CCW (3),
and `g_AlphaValue=1`. These are authored defaults, not current draw state.
The pass containers include state entries for blending, depth writes, culling,
color masks and alpha testing. The compiled-effect operation numbering was
cross-checked against the [D3DX effect state table](https://github.com/wine-mirror/wine/blob/master/dlls/d3dx9_36/effect.c);
that is offline container interpretation, not a Wine runtime requirement.
Some aliases include literal ADD entries and others leave operation handling to
inherited/dynamic state, which is another reason to use the current D3D states.

Opacity is not inferred from `g_AlphaValue`, shader alpha or `glass2s` either.
The latter selects a two-sided lighting calculation, independently of blending.
The runtime gate must retain actual target/scene ownership, Z and stencil state,
alpha test, color-write masks, source/destination factors, RGB/alpha operations,
separate alpha and current sampler states.

## Native RGB and gloss equations

Use `D` for diffuse RGB, `S` for **specular texture red**, `C` for environment
cube RGB, and `P` for the vertex point-light plus material-emissive RGB. All
products below involving colors are componentwise. These equations describe the
shader arithmetic before target conversion/blending. Native interpolation,
partial precision and older-profile range limits remain part of equivalence;
the algebra alone does not erase them.

For SM3, normalize the interpolated view and geometric normal for pixel
lighting. Let `d_i=sat(dot(N,L_i))`, `q_i=sat(dot(reflect(-L_i,N),V))`, and
`h_i=q_i^6 * sat(3*d_i)`. The power-six response is the original multiply chain.
With directional RGB `K_i`, the complete output is:

`RGB = D * [sat(P) + sum_i K_i*(0.5*d_i + 3*S*h_i)] + k*S*F*C`.

Base `glass/glass2s` has two directional lights and `k=0.5`; `_0000/_0001`
has one directional light and `k=1`. All four PS contain exactly the relevant
explicit COLOR0 RGB clamp. Two-sided variants multiply the normalized pixel
normal by the native VFACE-derived sign. They do not replace the vertex
point-light normal or rebuild the interpolated cube direction.

`F` is a **vertex-generated, interpolated** COLOR1 scalar. For ordinary unit
transformed normals it is `(1-abs(dot(N_vertex,V_vertex)))^1.2`.
The cube direction is also vertex-generated, from reflection of the normalized
camera vector about the transformed vertex normal. That transformed normal is
not normalized in the VS. Preserve its original log/exp sequence and exceptional
input behavior; do not insert a normalization, clamp, analytic Fresnel curve or
per-pixel recomputation. SM2 uses LIT for the analogous scalar, which needs its
own range-boundary equivalence check rather than an assumed identity for
malformed/nonunit normals.

All SM2 PS have one directional light and the same output form, but receive
`P` through the older COLOR0 range contract. The 2_b programs normalize vectors
arithmetically and use power six. The 2_0/2_a programs sample a normalization
cube twice, remap sampled RGB from [0,1] to [-1,1], and replace `q^6` with
`POW_Sampler(q,0.00374999992).r`. Keep the actual lookup and sampler filtering;
its name does not prove an exact analytic exponent or interchangeable sampling.
Their base/2s cube scale is 0.5, while `_0000/_0001` is 1. Two-sided SM2 PS
flip the normal according to the native sign test of `dot(-V,N)`; they do not
use SM3 VFACE. Point-light accumulation remains based on the original VS normal.

PS1.4 is materially different. Its VS already puts half-scaled directional
diffuse into COLOR0, in addition to point lights and material emissive. It sends
`B=sat(3*sat(dot(N_vertex,L)))` in COLOR1.x. Its PS obtains normal/view through
normalization-cube samples, obtains the reflection-vector gloss response `Q`
from the same POW lookup row, and evaluates:

`RGB = D * [COLOR0.rgb + 2*Q*S*B*K] + k*S*C`.

The PS1.4 reflection input is the **positive PS c0** vector, whereas SM2/3
reflect the negative directional constant. Preserve that exact operand sign and
the original effect constant binding; a generic modern-light expression is not
an established replacement. Here base/2s has `k=0.5`; `_0000/_0001` has `k=1`.
There is **no Fresnel weight**
in the cube term. Two-sided normal selection occurs in the VS before its point
lights, directional diffuse, reflection direction and normalization-cube
coordinates are formed. Moving it into the PS would change interpolation.

PS1.1 instead uses a normalized half-vector constructed in the VS. The VS packs
normal and half-vector as `0.5*x+0.5`; the PS unpacks them, saturates their dot,
and squares twice. Its gloss weight is COLOR1.w and its equation is:

`RGB = D * [COLOR0.rgb + 2*S*B*sat(dot(N_unpacked,H_unpacked))^4*K] + C`.

The cube is **unweighted**: no multiplication by S, F or 0.5. The two complete
PS identities have identical GPU bodies. The half-vector is normalized before
interpolation, while the PS does not renormalize either unpacked vector. This
cannot be substituted with the SM3 reflection-vector response without changing
the authored look. SM1 two-sided VS variants flip the normal as described above.

All SM1 VS compute a fixed two point lights for base/2s and V-toggle variants,
or one point light for L-toggle variants. SM2/3 loop VS use the bounded runtime
point-light count (up to eight); their L-toggle VS evaluates one point light.
Positions are ordinary homogeneous world/WVP transforms, with normal, camera,
UV-matrix, lighting and fog dependencies. No glass GPU body samples a normal map,
scene color/depth, lightmap, occlusion map or animated displacement. This is
reflective lit geometry, not shader refraction or a screen blend operation.

## Sampling and alpha contracts

| Token profile | Diffuse | Numeric gloss mask | Environment RGB | Additional numeric lookup resources |
| --- | --- | --- | --- | --- |
| PS3.0 / arithmetic PS2.0 (directory 2_b) | s0 | s1.r | cube s2 | none |
| Lookup PS2.0 (directories 2_0/2_a) | s1 | s2.r | cube s3 | POW 2D s0; normalization cube s4 sampled twice |
| PS1.4 | s5 | s4.r | cube s2 | normalization cubes s0 and s1; POW 2D s3 |
| PS1.1 | s0 | s1.r | cube s2 | no additional texture; packed normal/half-vector varyings |

Decode diffuse and environment **RGB** under the established material source
transfer policy, preserving their original samples, filtering, cube direction,
LOD and all alpha/data components. Keep S, POW lookup values, normalization-cube
RGB, packed vectors, angular response and fog as numerical data. In particular,
enabling sRGB on every cube sampler would corrupt normalization cubes. There is
no hue/affine RGB transform in these glass GPU bodies.

Every PS writes the same authored alpha multiplication: diffuse sample alpha
times COLOR0 alpha. SM2/3 VS set COLOR0 alpha to `g_AlphaValue`, multiplied by
`sat(fogClip.x - distance*fogClip.y)` when fog is enabled. SM1 computes the
algebraic boolean-select form `alpha*(1 + enableFog*(fade-1))`. Preserve native
sample alpha, VS alpha range/interpolation and the original pixel multiplication
precision. It is neither specular strength nor environment reflectivity. Never
premultiply it twice. Native target alpha still depends on color-write mask and
separate-alpha blend state, even when RGB uses source-over.

## Implementation and remaining boundaries

1. **Implemented six-pair SM3 material producer.** Convert raw point/directional RGB before
   light accumulation, preserve already-strength-scaled emissive under the
   existing material policy, decode D and C, and replace only the P radiance
   clamp with a full-precision transported linear P. Preserve the entire gloss
   equation, original angular clamps/power, S, F, two-sided choice, fog and
   native alpha. The diffuse multiplication includes the specular contribution;
   splitting it into an untinted additive highlight would lose native behavior.
   Term isolation must prove the separate cube contribution survives as well.
2. **Implemented opaque admission.** Reuse the existing opaque scene/material path when its
   actual state gate passes. This directly addresses the captured unsupported
   opaque pair without requiring a transparent compositor first. Keep the
   original per-draw write mask and native alpha outcome; RGB-only effect
   defaults are not permission to force RGBA or weaken the opaque gate silently.
3. **Remaining source-over admission.** Reuse the [distance-fade composition contract](linear-distance-fade.md)
   only for actual SRCALPHA/INVSRCALPHA ADD draws with its full state, target,
   recovery and alpha requirements satisfied. Supply the glass native/linear
   RGB twins and exact native alpha at the original draw position. Blended
   layers need that shared reactive/coverage treatment; opacity does not make
   one blended motion vector valid. Other blend modes, alpha-test combinations,
   destination-alpha rules or depth-writing transparent states remain distinct
   admission cases until specified and checked. They are not automatically
   screen-emission passes.
4. **Remaining twelve-pair SM2 and twelve-pair SM1 producers.** Preserve each native
   equation above while promoting/rerouting radiance COLOR varyings into
   unclipped TEXCOORD transport and providing shader outputs usable by the
   shared material/temporal contracts. Establish older-profile native RGB/alpha
   precision and implicit range behavior before linear conversion. SM1 stage
   binding, phase/coissue/scale modifiers, coordinate range, numeric lookup
   samplers and partial COLOR writes need an explicit promotion proof. Use the
   actual SM2 POW resource in lookup variants; replacing it with an analytic
   power is a separate visual change. Absence from captures does not excuse
   these 24 pairs.

SM3 glass uses original outputs o0–o6, TEXCOORD0–3 and COLOR0/1; PS v0–v5 are
occupied. The existing motion registry gives all six pairs relocated motion
TEX4 (o7/v6), with current depth TEX5 (o8/v7). The selected material transport
reuses **COLOR1.xyz (o6/v5)** for full-precision linear P. It moves the native
Fresnel scalar from COLOR1.x to the vacated **COLOR0.x (o1/v0)**. Native alpha
remains COLOR0.w. The original COLOR0 declaration keeps its partial-precision
hint; only COLOR1 is enlarged to XYZ and made full precision. The original
Fresnel EXP and MUL_PP instructions retain their component masks and operands
apart from that register relocation. No extra rounding copy is inserted.

The original P producer no longer writes COLOR0.rgb, and its sole RGB consumer
(the radiance clamp) is redirected to COLOR1. A complete source-use check must
prove these are the only old COLOR0 RGB uses before reusing x. Both carriers
remain COLOR semantics, preserving FLAT/Gouraud interpolation without inheriting
TEXCOORD wrapping. Verify this with glass-specific GPU witnesses. This replaces
the preliminary TEX6 proposal, which would have required new WRAP6 state and a
FLAT-shading contract. No added WRAP transaction or Gouraud-only gate is planned.

Create and validate exact variants once at shader registration. Per-draw work
should use cached pair/sampler masks and tracked state; no texture readback,
shader disassembly, allocation or fresh program hashing belongs there.

These candidates use documented D3D9 shader, sampler, state and render-target
interfaces and capability checks. Required material/composition features need
native-Windows implementations; forwarding alone is not support. The existing
composition capability/rollback limits remain binding. No native-Windows runtime
or new GPU performance result is claimed by this study.

## Next evidence, proportional to the implementation

Use the existing material fixtures with a six-pair glass extension, not a new
global fixture workflow. For SM3, isolate point/emissive, each directional
light, diffuse-tinted gloss and cube terms; distinguish power six, 0.5/1 cube
scale, S=0/1, F near face-on/grazing, both faces, loop counts 0/1/8, fog and
nonunit-normal boundaries. Compare native/motion/converted twins with exact
alpha and temporal outputs, including registration/state refusal and rollback.
Reuse unchanged opaque/ordered-composition evidence; extend source-over checks
only for the material's changed producer dependencies. Measure any added draw
cost after implementation, separating diagnostic timing from game FPS.

For older profiles, the existing local shader bodies and archive pairing data
are sufficient to start host conversion specifications now. The remaining
needed evidence is native numeric behavior of their packed/lookup/SM1 range
contracts, not a capture of every alias. Bound actual POW/normalization resource
contents through the game's resource path or a supplied draw when their exact
numerical reference is needed; do not invent lookup equations. Any later scene
capture should correlate the selected object, node, draw and material state to
resolve the user's appearance complaint. No additional user run is requested
for this architecture checkpoint.

## Six-pair source candidate

The source implementation adds the four SM3 pixel and three vertex originals
to the existing material core, with the exact six pair keys and sampler mask
`0x07`. The opaque runtime gate and draw-state transaction are unchanged. The
[derived source contract](../reverse-engineering/glass-material-profiles.json)
and [host checker](../../verification/analysis/test_glass_material_transformer.py)
record the COLOR carrier relocation above. The 24 older pairs remain outside
this implementation; blended glass admission remains separate.

Host qualification builds 56 variants over all seven originals, both depth
modes and gains 0/1/4/16. It checks exact pair/cross-pair selection, unchanged
native instruction spans except listed RGB/carrier edits, exact texture/control
flow and temporal insertions, COLOR component/precision contracts, output
aliasing and refusal rollback. Five transformer and six independent glass
reference tests pass. The unchanged conventional 15-test and XT seven-test
transformer regressions also pass; the consumed glass negative is replaced by
an unconverted motion-qualified moon pair. The production translation unit
cross-compiles with strict warnings, SSE2 and the four-byte incoming stack flags.

The performance inspection adds no per-draw work beyond the existing cached
contract: new pair rows are appended, so previous lookup order is unchanged.
Create-time transformation of 56 variants measured 1.462 ms total (about 26.1
microseconds each), excluding file I/O; this is a host diagnostic, not GPU time
or game FPS. Maximum weighted static slots are VS 88/90 and PS 144/146 with
depth off/on, below the SM3 minimum limit of 512.

The existing detached fixture has 254 new glass cases (IDs 3923–4176), and its
`--glass-only` option runs those cases with one representative glass performance
window. Native/motion/combined comparisons cover all six pairs, full-precision
over-one P, Fresnel, diffuse-tinted gloss, cube direction/scale, masks, faces,
fog/alpha, FLAT/Gouraud and perspective interpolation. Earlier 3,923 case
payloads remain byte-identical. Missing-record controls and full/scoped report
checks pass. The root-owned detached GPU qualification below is complete;
the focused live route is qualified below. Gameplay and native-Windows behavior
remain unqualified.

Independent Sol/high review found no unresolved production or evidence issue.
The review's scoped-dependency finding was fixed: glass-only now loads and
records exactly seven originals, validated with an isolated seven-file witness;
it neither requires nor hashes unrelated prior/XT programs. Stale architecture
wording was also corrected. Focused live routing is recorded below.

Root integration passes 58 focused transformer/reference/report tests in 62.493 s.
The retained fixture for 254 glass cases is `/tmp/x3-glass-frozen/linear_material_fixture.exe`,
SHA-256 `7e71e01d72bfaa95553118dcdf4f8550f2f7302fbd0f856a8dc895da580eb2f7`.
Its first root-owned Wine attempt waited for the user's active Run 9 and timed
out before fixture execution. After the user exited, the same frozen EXE ran
successfully under the X3 Wine lease: **254 cases, 2,286 analytic/native samples,
63 shader creations and 65,024 invariant pixels pass**. Exact alpha and temporal
twins pass; maximum RGB tolerance fraction is 0.150877. The
[compact result](../../verification/results/bottle-X3/linear-glass-gpu.json)
binds the inputs and raw `/tmp/x3-linear-glass-gpu/report.txt`.

The backend's programmable COLOR interpolation remains effectively Gouraud
despite requested FLAT, as recorded by the native classifier; this is not proof
of native-Windows FLAT behavior. Representative completion-wall medians for
combined material are 0.736/0.787 ms with zero/eight point lights, versus
ordinary motion 0.795/0.796 ms over 98,304 vertices. These small event-fenced
samples do not establish gameplay speed or a speedup. Installed source remains
`d9413fc`; no gameplay change was installed.


## Focused live route qualification

The verification-only extension reuses the consume-only
`run_linear_material_live.py --mode glass` runner. It tests all six pairs with
seven glass originals and the existing two-program bootstrap. Eight configurations
cross material off/on, depth off/on and perdraw/lazy attachment; 27 frames each
exercise sampler refusals, actual opaque RGBA-mask admission versus RGB-only and
blended refusal, WRAP4/5 observation/restoration, StateBlock, Reset, native alpha
and RT1/RT2 twins, plus two calibrated perspective RGB comparisons. The detached
corpus owns the broader equation matrix; host control-flow evidence owns injected
driver failures. No broad TAA or unchanged-family rerun was needed.

Independent Sol/high review is clean after correcting the initial schedule,
keeping consecutive lazy draws free of getter barriers, and preventing fixture
setters from masking Reset sampler resynchronization. Twenty focused report tests
pass, including malformed/missing/duplicate evidence. The retained fixture is
`/tmp/x3-glass-live-frozen/motion_output_fixture.exe`, SHA-256
`1ff5efc7f02968f34f31936cc1013f4d630567d8bab88a410cf39952a849d6fb`.

Root built one clean candidate from reviewed source `44b4e34` and linked the
existing test seam from its retained objects. The X3 Wine run passes **216 frames,
432 native source observations and 4,258,208 checks**. The
[canonical live result](../../verification/results/bottle-X3/linear-material-live-glass.json)
binds the frozen binaries, scoped inputs, candidate/toolchain and raw results.
The production DLL also passes the linked x87 audit (218 reachable functions).
No new production per-draw work, broad benchmark or gameplay cost claim is
introduced by this fixture extension. The actual game remains on `d9413fc`;
glass is qualified for a future combined install, not installed or accepted in
user gameplay. This does not establish a fix for the docking-port transition.
