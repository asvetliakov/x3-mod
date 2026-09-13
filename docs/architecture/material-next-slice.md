# Next material and emissive color-writer slice

Initial decision brief, 2026-09-13, after first Argon DEFAULT qualification
(source `a56e77e`, install checkpoint `b93eb6c`). Sections 4 and 5 record the
subsequent offline and runtime checkpoints. The shared DEFAULT expansion is now
reviewed and installed; see the [current install record](../../verification/results/linear-material-install.json).
Gameplay acceptance remains pending.

The subsequent [Split and standard lighting extension](linear-standard-materials.md)
records the complete 40-pair SM3 group, including all standard BUMP/LOW toggles,
its retained temporal ABI and pending GPU/live qualification.

**Selected and now implemented: shared Khaak/Teladi/Teladi_nodiff/Xenon DEFAULT.** It
extends real linear material/lightmap-emissive evaluation through the existing
opaque route. Engine/effects additive emission is the next architectural target;
it needs linear composition and temporal reactivity before production integration.
Encoding brighter individual sources into the current gamma-space blend target
would be an art-directed HDR transition, not correct linear HDR composition.

## 1. Shared opaque DEFAULT: selected next slice

The complete archive contract contains **10 pairs, the same three VS as Argon,
and six new PS**. The four family aliases overlap exactly; they are not four
independent coverage sets.

| Existing VS | New PS |
| --- | --- |
| `53a0a641107ed76c` | `3b94320087e81945`, `e3b7acc16da9932d` |
| `719856ce0c213220`, `badefd5143b3024f` | Each paired with `7a14d4dcb28f27e5`, `8ab6188a40ca15ea`, `8df6143d0e77d92e`, `e16a9806ee3544c3` |

All six PS instruction bodies were inspected. They retain the first slice's
four resource roles: diffuse/color transform, scalar specular mask, additive
lightmap RGB, and diffuse-tinted cubemap reflection. Alpha remains
`lerp(diffuse.a, lightmap.a, glow) * vertex_alpha`; material emissive remains
combined upstream with point lighting and must retain its native strength.

This is **mostly the same algebra**, with these exact shader-bound differences:

| PS | Directional lights | Affine color | VFACE | Local 0.5 coefficient |
| --- | ---: | :-: | :-: | --- |
| `3b94320087e81945` | 2 | yes | no | c8.w |
| `e3b7acc16da9932d` | 2 | yes | yes | c9.x |
| `7a14d4dcb28f27e5` | 1 | yes | no | c6.w |
| `8ab6188a40ca15ea` | 1 | yes | yes | c6.y |
| `8df6143d0e77d92e` | 1 | no | no | c3.y |
| `e16a9806ee3544c3` | 1 | no | yes | c4.y |

In every row that literal drives **both directional diffuse scaling and cube
scaling**. The specular dot's multiply chain forms x², x⁴, then x⁶; its result
is multiplied by the preserved `sat(3*NdotL)` factor and `3*specular_mask`.
Thus the family has diffuse 0.5, specular exponent 6 and cube gain 0.5, versus
Argon's exact float32 0.4, exponent 5 and cube gain 1. These are derived
instruction facts; no game shader text is copied here. The sixth-power result
is not an application-supplied gloss constant. Preserve original arithmetic
order instead of substituting Argon bytecode.

**No new resource ABI is expected.** All ten are existing class-A motion rows:
original PS v0–v4, motion v5/TEXCOORD4, current depth v6/TEXCOORD5. The qualified
radiance v7/TEXCOORD6 and the same VS o8 remain available; the three VS are
already exactly the qualified programs. Existing material-local constant ranges
and temporary choices still require generated per-program collision checks.
The shared VS variants must remain consistent for both old/new pairings; do
not add a second conflicting definition to a cache keyed by original VS.

Historical iteration-05 coverage is **236 scene draws**, all from the first
pair. This is neither pixel coverage nor proof of a particular race/object in
the scene; the other nine pairings come from the complete archive pass inventory.
Continue the first slice's opaque, FP16-owner, no-MSAA, known-sampler and
matching-decode gates, with original alpha and motion/depth preserved.

The next checkpoint should generate six original-site profiles, parameterize
the numerical reference's exact lobe coefficients, and reuse the combined
transform/publication path. Qualify all ten pairs, faces/toggles, independent
colored lighting and lightmap gains, old/new shared-VS alternation, unavailable
combined-stage fallback, original alpha/RT1/RT2, Reset and instruction/register
budgets. Run affected checks and compare shader cost; no new scene-layer pass
is required. Do not infer correctness from one representative or hash aliases.

**Retain the negative coverage witness.** The current live runner
`verification/probe/run_linear_material_live.py` uses PS `3b94320087e81945`
with VS `53a0a641107ed76c` to prove that sharing a material VS does not authorize
an uncovered PS. It becomes covered in this slice. Replace that negative fixture
input with the still-uncovered, motion-reviewed Split pair
`53a0a641107ed76c` / `462342e3e5781384`, and keep the assertions proving ordinary
motion works while material routing refuses it. Do not delete the test.

For comparison, Split DEFAULT shares the roles but uses a tenth-power lobe;
standard_lighting makes several coefficients application constants. Boron PS
`39eb3c2258a516e1` is genuinely different: additional palette/color-weight
varyings and view-dependent mixing. Argon BUMPMAP offers more historical
coverage (10 pairs, 1,896 draws) but adds alpha/green normal reconstruction,
tangent/binormal mixing and per-pixel cube direction. Its reference pair
`4944d81dfe531b37` / `ca6bfa4a6cca7e2a` already occupies o8/TEXCOORD6 and v7
for depth, so it needs a separately validated radiance-varying assignment. A bounded
per-family ABI is sufficient; a general-purpose allocator is not a prerequisite.

## 2. Engine/effects: genuine additive emission needs a composition boundary

Highest-quality effects still bind VS2/PS2 for DEFAULT. A complete bounded
DEFAULT contract has these five pairings:

| VS | PS | Source RGB role |
| --- | --- | --- |
| `d5e1c75351ed3f04` | `8360f422de08b5bd` | Affine texture RGB × interpolated fade |
| `32e75459998d0388` | `9975b706e5a1c999`, `ff2473e73a6bdfa1` | Same fade path, affine present/absent |
| `089091aab2d5eb13` | `8559522220507d5e`, `875e780adb131b16` | Texture RGB, affine present/absent, no fade scalar |

INSTANCE reuses those five PS with VS `89193868c61c3846`,
`5b7a3ccd9e7df00a` and `a520be365951c9dc`, respectively; it is an explicit
follow-on. INSTANCE_BULLETS is a different SM1 contract: base
`5e484a06672e28fb` / `ec1f5c4a2f4e1445` multiplies texture RGB by vertex alpha.
Neither bullets nor particles are implicitly covered by PS2 work.

The scalar VS writes **g_AlphaValue × optional fog fade to COLOR0.x**. This
is a fade control, not recovered emissive intensity. PS2 alpha remains texture
alpha independently. Preserve the scalar's original SM2 interpolation/clamping;
new HDR range can be generated after it in the PS, without a new HDR varying.
Decode artistic texture color before applying a new linear source gain.
The base engine/effects programs are identical; those names cannot distinguish
exhaust, weapons, sun sprites or overlays at runtime.

A streamed query of the old iteration-05 capture gives decisive state evidence:

| Base pair population | Draws | RGB blend / depth |
| --- | ---: | --- |
| Scene additive | 32 | ADD/ONE/ONE; Z test on, Z writes off |
| Scene screen blend | 71 | ADD/ONE/INVSRCCOLOR; Z test on, Z writes off |
| Late overlay, exact same pair | 924 | ADD/ONE/ONE; Z test/write off |

Alpha test is off and color mask is 15. Separate-alpha enable varies; complete
alpha factors are absent from this trace. Existing
[late-view disassembly](../reverse-engineering/bloom-late-view-state.md) proves
that ordinary view setup does not reset all blend state. A source override
therefore needs the actual scene owner, exact RGB blend gate and known alpha
contract; shader hashes alone would also change the 924 late draws.

For desired linear source E and decoded destination D, addition is D+E.
Adding encode(E) to the engine-space destination yields a different result:
two encoded unit inputs decode to about **4.59**, not 2. Screen blending also
uses source RGB as its destination attenuation factor; promoting that value
above one does not define an HDR opacity model. Leave screen blends unchanged
until radiance and coverage semantics are chosen explicitly.

A correct additive prototype could bracket **contiguous eligible draws**:
decode current scene RGB into linear FP16 scratch (copy alpha unchanged), draw
linear emissions there against the original depth with original alpha behavior,
then encode RGB/copy alpha back before any noneligible draw, target/clear change
or other relevant boundary. This preserves order and gives real additive energy
inside the bracket. It costs at least two full-scene transfers plus state,
rollback and ownership machinery; it is not a cheap per-shader extension.
The capture contains 16 such bursts of two adjacent draws across 16 frames,
exactly one burst per affected frame. That does not prove
an engine-wide emission phase. Deferring a separate emission layer until scene
end can put earlier emission over later occluders or after intervening blends.

TAA is another prerequisite: the current depth-sentinel policy does not detect
blended effects over routed opaque pixels, as
[temporal integration](temporal-integration.md) already records. Brightening
these sources needs qualified current/previous reactive coverage and a portable
mask producer; opaque depth or a zero velocity is not effect motion. The next
checkpoint for this contract should be a standalone ordered-composition fixture:
colored overlapping emitters, later occluder, intervening nonadditive blend,
inherited alpha state, shared late-overlay refusal, disappearing emission over
valid opaque depth, and failure of each transfer/restore. Use its correctness
and cost to choose production integration; do not relax the opaque gate first.

## 3. Particle billboards; sun identification still missing

Particles have two complete pairs:
`36f98d151fd6b0c6` / `222bee0defcb1852` and
`2eea471bc86935f2` / `da43623af4f72352`, both VS1.1/PS1.1 even in `3_0`.
The VS expands XY between view and projection and forwards packed vertex color.
PS RGB is texture × vertex RGB; alpha blends texture/vertex alpha using
`g_TextureAlpha`. The 20 old iteration-05 scene draws use **SRCCOLOR/INVSRCCOLOR**,
Z test on, Z writes off and RGB-only writes. Their RGB equation is
`Cs² + Cd*(1-Cs)` before target conversion; alpha is not RGB coverage.
A raw gain is especially poorly defined for this artistic blend.

First choose separate HDR radiance/coverage semantics, then qualify a suitable
shader-model output and reactivity. Stable previous particle identity/attributes
are still missing. Reuse the existing
[particle input study](../reverse-engineering/particle-motion-inputs.md), including
its DISCARD/layout and disappearance requirements; do not classify this as rigid
motion just because matrices are known.

No dedicated sun shader is established by the 28-family inventory. PS
`6109cf64c03529dd` is shared by GUI/nebula paths and `0a523f33ac47ae05` by
GUI/stardust paths; the capture places them in both background and late views.
The decisive sun evidence is an offline link from a sun model/material resource
to its actual view/draw and blend contract, including whether it precedes bloom.
A `TSuns` entry or bright sprite name alone cannot authorize a global gain.

The [sun resource follow-up](../reverse-engineering/sun-material-identity.md)
now connects TSuns lens groups to 36 bodies and the shared `effects` family.
Their dedicated lens scene is late, after any stock bloom invocation; explicit
materials include ordinary alpha blending. This does not establish a pre-bloom
sun emitter. The separate TPlanets sun-scene submesh/material and effective draw
mapping remains unresolved.

This brief uses the complete [archive pair inventory](../reverse-engineering/motion-output-profiles.md),
local shader instruction inspection, existing targeted EXE findings and streamed
queries of the trace pinned in [material color inputs](../reverse-engineering/material-color-inputs.md).
The private blend helper is `/tmp/x3-next-material-states.py`. Counts are historical
only. No raw game payload, new manifest, Wine run, gameplay request or production
change was produced.

## 4. Offline proof checkpoint review

Independent review approves the offline extension with no open finding. The
derived profile now contains 15 programs and 20 pairs: the qualified nine-program,
ten-pair Argon set remains unchanged apart from family/coefficient annotations,
and the six new PS plus ten shared-family pairs are explicitly marked offline
only. The proof follows saturated diffuse cosine, the x²/x⁴/x⁶ specular chain,
sampled mask, exact 0.5 diffuse/cube coefficients and every final consumer for
all six new programs. It separately proves original alpha, relative operands,
resources and each Khaak/Teladi/Teladi_nodiff/Xenon alias's complete pair set.

Review found and resolved two evidence gaps: the first lobe proof checked
isolated coefficient sites without every intermediate producer/liveness link,
and the first archive check proved only the aliases' union. Direct structural
mutation tests now reject both classes. The two affected offline modules pass
37 tests, including independent per-profile numerical discriminators. The
production transformer fixture was explicitly filtered to its exact nine Argon
originals at this checkpoint; the 1,296-DWORD maximum was left for the future
production-bound change.
No production source, prior GPU result, build, Wine run, game launch or install
is part of this checkpoint.

## 5. Shared DEFAULT runtime checkpoint review

Independent review approves the runtime extension with no open finding. The
production transformer now accepts the proved 15 programs and only the 20
explicit pairs. It handles the 1,296-DWORD maximum while refusing 1,297 before
hashing, uses each program's proved clamp register and RGB sites, and leaves all
72 prior Argon combined outputs byte-identical. The full-precision material ABI,
alpha and temporal merge, sampler policy, failure fallback, cached gains and
live draw gate are unchanged. Per-draw selection is a bounded, allocation-free
scan of 20 cached identity pairs.

The structural fixture passes five tests over 120 variants and 1,313 checks;
maximum weighted slots are VS 77 and PS 165. The expanded
[GPU result](../../verification/results/bottle-X3/linear-material-gpu.json)
passes 313 cases, 2,817 samples and 80,128 exact alpha/motion pixels across all
20 pairs and 15 originals, including an unchanged 167-case Argon prefix. All 89
shader creations succeed, every new PS rejects the old Argon lobe coefficients,
and maximum normalized tolerance use is 15.97%. The clean run exits zero.

The [live result](../../verification/results/bottle-X3/linear-material-live.json)
passes 1,228 checks over 96 frames and eight feature/ownership/TAA twins. On one
shared VS, frame 1 admits the new `3b94320087e81945` PS and produces 1.87695312
RGB at lightmap gain 4, while frame 9 refuses Split `462342e3e5781384` and stays
exactly 1. Alpha and every RT1/RT2 hash match the feature-off twins. Combined
creation adds exactly three held shader references in each ownership/TAA case;
all device and factory references retire to zero. Attach/Reset sampler refresh
and the existing state-block cases also pass.

The retained clean candidate is 12,347,043 bytes with SHA-256
`24edac6c2c913e7994f2669f8d295ca0d56f79dd0bb9ef1b23f36fabf4efd724`.
Its build completed in 5.872 seconds, its import inventory remains 194 symbols
from 15 DLLs, the x87 audit found no violation in 211 reachable functions, and
the focused load check passes eight checks over 17 exports. I independently ran
the 11 GPU-result and two live-result parser tests; all 13 passed.

The evidence uses synthetic detached and live-route fixtures. Native Windows,
gameplay appearance, installed behavior and game FPS remain unverified; the
existing brief user material run covers the expanded candidate. This review
performed no build, Wine run, game launch, installation or commit.
