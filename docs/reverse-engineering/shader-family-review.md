# Archive shader family cross-check

This is an independent exception review of the complete local archive sweep:
**751 unique complete programs**, comprising 495 pixel and 256 vertex shaders.
It combines an automated instruction/name screen over every D3DX disassembly
with selected manual dataflow inspection. It is **not** a manual proof of all
751 programs, a live-pass classification, or a replacement-shader specification.

The inputs are the local `/tmp/x3-shader-sweep/manifest.json`, `programs/`, and
`disassembly/` produced by the archive sweep. Copyrighted bytes and disassembly
stay outside the repository. IDs below are complete-program FNV-1a-64 hashes;
the sweep manifest supplies SHA-256 and every archive alias. Representative
paths in the table mean `shader/3_0/<family>.fb` in `addon/01.cat`, except where
explicitly stated. Identical bytes also occur in the root archive and other
profile/toggle directories. File names identify aliases, not runtime purpose.

## Complete family purpose table

The 28 family names below normalize only terminal `2s`, `_0000`, or `_0001`.
The meaning of those suffixes is not assumed. **Instruction** means the stated
arithmetic was inspected in a representative program or an already documented
material dataflow. **Pattern** means sampler/output/position screening plus the
common material evidence; it does not claim every variant was manually traced.
The association with a particular race, object type, or live pass remains a
**name hint** until correlated with a draw.

| Family | Representative program | Derived purpose and confidence |
| --- | --- | --- |
| `adeffects` | PS `03a16e5c63daa6e8` | Textured, RGB color-transformed surface with constant 0.4 alpha; **instruction**. Exact special-effect use is a name hint. |
| `argon` | PS `8759c7838bbc86c2` | Lit material combining diffuse/specular, lightmap and cubemap; **instruction**, detailed in position/material notes. Race assignment is a name hint. |
| `asteroid` | PS `517540ae6d5e5410` | Directionally lit diffuse/specular material with a second detail texture and vertex-light contribution; **instruction**. Asteroid assignment is a name hint. |
| `bloom` | PS `1c90e79667bdaddf`, `241c3fa33270f58e`, `b40d09effa812ec8` | Highlight extraction, separable blur and scene/glow composition; **instruction**. Original bloom alpha is auxiliary data. |
| `boron` | PS `39eb3c2258a516e1` | Lit material with additional varying/color terms, lightmap and cubemap reflection; **instruction**. Race assignment is a name hint. |
| `effects` | PS `8360f422de08b5bd`, `ec1f5c4a2f4e1445` | Textured RGB scaled by a vertex scalar, with separate texture alpha; **instruction**. Effect identity and blending require draw state. |
| `engine` | Same default programs as `effects` | Same textured/scalar paths; **instruction and exact shared hashes**. Engine/exhaust assignment is a name hint. |
| `glass` | PS `a66fb1981ba755b2` | Lit textured material plus cubemap reflection, separate texture × vertex alpha; **instruction**. Actual transparency also depends on blend state. |
| `gui2d` | PS `0a523f33ac47ae05`, `6109cf64c03529dd` | Texture RGB with vertex- or uniform-controlled alpha; matrix and direct-position VS alternatives; **instruction**. Programs are shared with scene families. |
| `khaak` | PS `3b94320087e81945` | Diffuse/specular lighting, lightmap and cubemap material; **instruction**. Same representative program as three other names. |
| `moon` | PS `d6f6ba4fee1cd53e` | Two vertex color inputs modulate diffuse/specular; separate lightmap and detail contributions; **instruction**. Older color-varying path. |
| `nebula` | PS `6109cf64c03529dd` | Texture RGB with uniform-scaled texture alpha; **instruction**, exact program shared with `gui2d`. No volumetric integration in this program. |
| `nebulafog` | PS `f7e0b6647a3bfa62` | Uniform-scaled texture RGB, unchanged texture alpha; **instruction**. Fog-volume meaning is a name hint. |
| `paranid` | PS `9d27e7ba242f3831` | Lit textured material with additional varying/color terms and view-dependent power response, lightmap/cubemap; **instruction**. Race assignment is a name hint. |
| `particles` | VS `36f98d151fd6b0c6`, PS `222bee0defcb1852` | View-space XY expansion between view and projection; textured vertex color with separate alpha interpolation; **instruction**. See motion exception below. |
| `planet_haze` | VS `be199829a9bb78db`, PS `cd6d6eb4b3d99443` | View/light-dependent lookup coordinates; three sampled RGB contributions averaged and scaled, constant 0.05 alpha; **instruction**. Atmospheric use is a name hint. |
| `planet_v` | VS `72f8dbb8567bbf88`, PS `00fcc903c7f085d5` | Vertex point/directional lighting plus texture and secondary texture contribution; **instruction**. VS/PS are SM2 despite directory name. |
| `split` | PS `462342e3e5781384` | Directional diffuse/specular power response, vertex lighting, lightmap/cubemap material; **instruction**. Race assignment is a name hint. |
| `standard_lighting` | PS `7c83ed50c9894e44` | Common diffuse/specular, vertex-light, lightmap and cubemap material; **instruction**, detailed in material notes. |
| `stardust` | VS `41c960621d22671f`, PS `0a523f33ac47ae05` | Matrix-transformed textured vertices with vertex alpha; **instruction**. PS is shared with GUI. |
| `teladi` | PS `3b94320087e81945` | Same representative lit/lightmap/cubemap program as `khaak`; **instruction and exact shared hash**. Race assignment is a name hint. |
| `teladi_nodiff` | PS `3b94320087e81945` | Same representative program as `teladi`; **instruction and exact shared hash**. The name does not prove absence of diffuse arithmetic. |
| `terran` | PS `ef2bf556f207b8bd` | Directional diffuse/specular, vertex lighting and lightmap/cubemap material; **instruction**. Race assignment is a name hint. |
| `xenon` | PS `3b94320087e81945` | Same representative lit/lightmap/cubemap program as `khaak`; **instruction and exact shared hash**. Race assignment is a name hint. |
| `xt_standard_lighting` | PS `5f82ecacd39529cd` | Extended material with bump, specular, lightmap, cubemap, occlusion and detail inputs; **instruction**, detailed in material notes. |
| `xt_standard_lighting_damage` | PS `d51cf763125cb85a` | Extended material with branch-dependent vertex-light saturation, reflection and occlusion modulation; **instruction**. Damage-specific texture/state meaning is a name hint. |
| `xt_terraformer` | PS `75fb9c6b05e28ea2` | Extended lit/reflected material, separate occlusion sampling and an additional sampled RGB contribution at output; **instruction**. Exact object assignment is a name hint. |
| `z_only` | VS `803ebfd17f79e413`, `c78b4c68a87fce74`; PS `652a7c5d1e9909a0` | Matrix position with optional UV; PS returns a texture sample; **instruction**. Depth-only, alpha-test and shadow behavior depend on external state. |

## Exceptions relevant to TAA

### Particle expansion is not the ordinary mesh WVP path

The entire `particles` alias family contains two unique VS programs,
`36f98d151fd6b0c6` and `2eea471bc86935f2`, and two PS programs. The two VS
disassemblies have the same inspected arithmetic. They construct homogeneous
input position, transform it by c0–3, **add input TEXCOORD0 XY in that intermediate
space**, then transform by c4–7 to clip space. CTAB labels these matrices
`g_mView` and `g_mProj`, respectively. TEXCOORD1 supplies texture coordinates;
COLOR supplies vertex color.

Thus these are view-space expanded billboards, rather than a single ordinary
mesh WVP. Projection jitter belongs in c4/c5 relative to c7 after eligibility
is established. A motion producer needs the previous center and expansion data,
plus the previous view/projection; a rigid WVP-only assumption discards the
expansion contract. This is positional input dependence, **not evidence of a
time-driven GPU animation routine**. CPU particle updates remain unmeasured.

### Shared programs cannot identify scene/HUD eligibility

PS `6109cf64c03529dd` occurs in both `gui2d` and `nebula`; VS
`7b6393fe2d3e1d85` also occurs in `nebulafog`. PS `0a523f33ac47ae05` occurs in
both `gui2d` and `stardust`. The default `engine` and `effects` aliases contain
the same program hashes. These are exact byte identities, not just visually
similar code. Shader-only pass routing cannot reliably keep HUD unjittered or
separate scene-linear color from display/UI color.

Bloom VS `cbbf26102694c961` copies input position directly; its constants move
UVs. GUI VS `f36fc43f30b19d71` copies input XYZ and forces W=1. Neither has a
projection matrix to edit. The automated position-write screen found 18 VS with
a single MOV position write, two with a single MAD position write, and 236 with
four DP4 position writes. These counts describe output syntax, **not** a proof
that all 236 DP4 programs share their complete source-data dependencies.

### Relative addressing does not establish skinning

Across all 751 GPU instruction bodies, 67 programs use relative constants.
They are all vertex shaders, all use repetition, and their only relative
references are c0/c1/c2 indexed by a0.w. Manually inspected material, glass and
planet examples derive this index from three times a light-loop counter and
use the three registers as position, RGB, attenuation. This agrees with the
observed material light loops. The broad screen found no blend-weight or
blend-index declarations, VS texture instructions, `sincos`, or `frc`.

A register-name screen found no time, animation, bone, skin, or instance parameter
names; material detail-map **blend** weights are unrelated. This does not prove
the game has no animation, skinning, instancing or time dependence: buffers,
constants, texture data and draw submissions can all change on the CPU. It
does establish that generic relative-address detection must not label these
programs as bone-matrix shaders.

## Exceptions relevant to HDR and bloom

### Five observed material programs do not cover the archive

Glass PS `a66fb1981ba755b2` explicitly clamps interpolated COLOR0 RGB before
combining it with directional lighting and a texture. Its VS
`c30104cb0efb6675` writes point-light accumulation plus emissive to COLOR0 under
SM3. Alpha is separately texture alpha times COLOR0 alpha; cubemap reflection
is another RGB contribution. This is an additional radiance-clamp candidate
outside the five exact profiles in [material radiance](material-radiance.md).
It needs its own guarded patch specification and pass policy.

Selected asteroid, Boron, Paranid, Split, Terran, shared Khaak/Teladi/Xenon,
damage and terraformer programs in the table also contain direct COLOR0 RGB
saturation. The broad inventory should identify every candidate, but matching
the arithmetic motif is not sufficient authority to patch an unverified
program. Specular/angular/fog/alpha saturations must remain distinct.

### Older color varyings are another clamp location

The `planet_v` representative is VS2/PS2. Its VS accumulates point-light RGB
and directional response into oD0, and the PS multiplies the corresponding v0
by the color-transformed diffuse texture, then adds a secondary sample scaled
by the second color input. There is no explicit PS SAT in that program to
remove. `moon` likewise receives two SM2 color varyings.

The existing [numeric varying probe](../verification/vertex-color-hdr.md)
demonstrates SM2 COLOR clipping on this backend. Therefore these programs need
separate numeric qualification and likely varying/model changes to preserve
over-one vertex lighting; an SM3-material SAT patch cannot cover them. The
particle shaders are SM1.1, another separate qualification target. The actual
program token version, not its effect directory, determines the contract.

### Original bloom alpha is not ordinary scene opacity

PS `1c90e79667bdaddf` computes a luminance threshold, saturates a scaled
threshold difference, and applies a cubic smoothstep. It writes sampled RGB
times **the original sampled alpha** to RGB, while its output alpha is that
threshold weight times one minus saturated sampled alpha. Removing these
saturations would change mask semantics rather than merely uncap radiance.
The older PS2 program `1db3179c324cf97d` has the same derived behavior.

The SM3 horizontal/vertical blur programs `f3172baa8dd19a40` and
`241c3fa33270f58e` accumulate **25 RGBA samples** using scalar weights. They
blur auxiliary alpha as well as RGB. An inspected older PS2 horizontal program
`5bb657a9f21d6833` uses 13 samples, so tap count also depends on the program.

Compositor `b40d09effa812ec8` combines a scene sample, glow RGB and a scalar
glow-alpha contribution, applies scalar multipliers including CTAB `Exposure`,
and forces output alpha to zero. `ff6eed5a5ddf3a3a` also combines glow RGB and
glow alpha and forces zero alpha. Neither inspected compositor performs a
nonlinear display tone curve. An `Exposure` parameter is not evidence of an
automatic exposure measurement/adaptation loop. These contracts matter when
replacing the old bloom with scene-linear HDR bloom and exposure.

## Fog, depth and shadow limits

The representative `nebulafog` PS is a single texture sample scaled in RGB by
a uniform; it preserves sample alpha. `planet_haze` is a lookup-based effect:
the VS derives view/light-dependent coordinates and the PS combines three
sampled RGB values with fixed output alpha 0.05. Neither inspected program
samples scene depth or integrates a volumetric ray. This is a precise statement
about those programs, not a claim about their scene geometry or blend state.

The archive has no effect basename containing `shadow`. The full GPU-body
screen found no explicit pixel depth output, `texkill`, projected texture
opcode, gradient texture opcode, derivative instruction, or vertex texture
fetch. These absences **do not prove no shadowing**: a shadow/depth pass may use
ordinary position output, depth state, alpha test, sampler state, or fixed
function machinery. In particular, the `z_only` representative PS simply
returns a texture sample; no discard or explicit depth write occurs inside it.
Its name alone cannot justify labeling every use a shadow map.

## Review boundary

Every program was screened after locating its actual `vs_*`/`ps_*` marker,
including D3DX's `2_x` spelling. The preshader and CTAB comment sections were
excluded from GPU-opcode counts. All 751 files were accounted for. The supplied
manifest groups them into 642 distinct GPU-token hashes, so comment/metadata
differences also create different complete-program identities.

Representative instruction paths were examined for every normalized family
through direct inspection, exact shared programs, and the prior material
inspection. Lower-profile and toggle variants received automated screening,
with selected particle and bloom variants manually cross-checked. No runtime
shader was modified, no game was launched, and no new numerical rendering
claim is made by this archive review.
