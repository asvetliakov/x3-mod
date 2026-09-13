# Remaining SM1 effects/engine conversion plan

2026-09-13. Offline plan; source promotion and screen composition are not
implemented. The current twenty-pair SM2 route remains separate. Native precision
parity and the screen attenuation policy must be resolved before integration.

## Complete scope and recommendation

Cover all **nine exact pairs / nine VS / six PS / 480 archive occurrences** in
rows 16–24 of the [remaining effects/engine ledger](../reverse-engineering/effects-engine-remaining-emission.md#count-and-exact-inventory):
six DEFAULT/INSTANCE scalar-fade pairs (192 occurrences) and three
INSTANCE_BULLETS pairs (288). Every actual token model is VS1.1/PS1.1; the bullet
technique remains SM1 even inside the highest quality effect directories. All
aliases/toggles in that ledger are part of the group. Do not use directory,
technique or normalized-body identity as runtime authority.

**Prepare a bounded PS1.1→PS2.0 promotion for all six PS identities, retain all
nine original VS byte-for-byte, and qualify the nine pairs together.** PS2 has
the needed color outputs and sufficient resources; PS3 supplies no needed
arithmetic feature and would require replacing the vertex side too. Microsoft's
[SM3 compatibility rule](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/shader-model-3)
forbids hardware SM3 stages mixed with older shader versions. Preserve the legacy
COLOR0/TEXCOORD0 interface with PS2 rather than introduce SM3 transport.

Promotion alone is **not complete live coverage**: the one observed bullet
population uses screen blending. The plan therefore has two acceptance axes:
all-nine source promotion/native parity, and ordered additive plus screen
composition/coverage. Admit by actual state, not by bullet versus scalar names.
An additive-only nine-entry registry would still miss the observed bullet draws.

## Original contracts checked locally

Targeted parsing used the existing `inspect_motion_output_profiles.instructions`
walker on all fifteen files in `/tmp/x3-shader-sweep/programs`; each whole-file
FNV identity and complete SM1 framing matched. Ignoring opaque comments yields
exactly three VS and two PS bodies. Targeted D3DX disassembly/CTAB in the local
`disassembly` directory establishes the following derived facts; no original
instruction tokens or game shader listings are tracked here.

| Body | Exact originals | DWORDs each | Actual executable shape |
| --- | --- | ---: | --- |
| VS DEFAULT | `0d44b36d48d24f7a`, `6da1b1b6ed63ec82`, `88620f88d6e0a00e` | 253 | 26 arithmetic slots; position/world/fog, UV affine |
| VS INSTANCE | `637dadcb5efa3288`, `ed42e0742e47dca4`, `f9755e1154244f58` | 219 | 23 arithmetic slots; position/world/fog, direct UV |
| VS bullet | `1b6863a088a177af`, `21a2c13be7f989c3`, `5e484a06672e28fb` | 101 | 7 arithmetic slots; position, direct UV, packed COLOR0 |
| PS scalar | `078494828322bcca`, `2ea025492d370c8e`, `a5c3495e27270b4a` | 59 | one sample, two arithmetic slots with co-issued alpha MOV |
| PS bullet | `84d3de8887c963c5`, `d4a26efb7c603931`, `ec1f5c4a2f4e1445` | 49 | one sample, one arithmetic slot with co-issued alpha MOV |

All six PS CTABs name a 2D diffuse sampler at stage 0. Scalar PS instruction
starts are DEF 39, TEX 45, DP3 47, RGB MUL 51, co-issued alpha MOV 55; bullet
starts are TEX 39, RGB MUL 41, co-issued alpha MOV 45. These are original DWORD
offsets, not replacement offsets. Neither body has affine RGB, another sampler,
relative addressing, kill/depth writes, branching or application PS constants.
Scalar c0 is shader-local `(1,0,0,0)`; the DP3 selects COLOR0.x. Define sampled
RGBA as `(T,a)` and the rasterized/clamped multiplier as `h`. Native source is
`q=T*h`, with output alpha **a**, not `a*h`. Scalar h comes from COLOR0.x;
bullet h comes from COLOR0.w. The co-issued alpha copy reads the same texture
sample and has no cross-instruction dependency on RGB. Preserve this distinction
in the promoted native branch and the new decoded branch.

The six fogged VS are **not the SM2 bool-b0 layout**. DEFAULT uses WVP c0–3,
world c4–6, camera translation c7.w/c8.w/c9.w, UV c10–11, float-register fog
flag c12.x, material alpha c13.x, and fog clip c14.xy. INSTANCE uses VP c0–3,
world/camera as above, fog flag c10.x, alpha c11.x, fog clip c12.xy, direct UV.
With `F=clamp(fog.x-distance*fog.y,0,1)`, native pre-raster COLOR0.x is
`alpha + enableFog*alpha*(F-1)` in its original MAD ordering. The source declares
the flag as bool but stores it in a float register; retain the actual arithmetic,
including exceptional distance behavior, rather than branch on a new b0.
The PS receives clamped/interpolated color. Bullet VS uses VP c0–3 and forwards
vertex v2 to complete COLOR0; it does not calculate fog. All nine write oT0.xyz
with z=0, and preserve native clip position. No new motion, depth, COLOR or WRAP
transport is needed for this producer.

## Promotion proof and limits

PS1 cannot acquire extra outputs by appending a tail: its result is r0. The
[SM1 register contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-1-x)
also permits historical fixed-point precision and device-dependent arithmetic
range. The [instruction-token format](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/instruction-token)
uses implicit SM1 arities/co-issue, versus explicit lengths and no co-issue for
PS2. Changing the version word is invalid. A small two-shape emitter must check
the complete original fingerprint/count, every opcode/operand/co-issue relation
and exact nine-pair membership before authoring a PS2 stream with declarations,
explicit sampling, preserved native RGB/alpha evaluation and oC0.

Use existing COLOR0 and TEXCOORD0 registers; both SM1 and PS2 color inputs are
clamped to [0,1] under the [D3D9 color-input contract](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-writing-shaders-9).
Preserve native COLOR interpolation (including flat shading), texture WRAP0,
filtering/mip bias/addressing and the bound texture. Require the actual 2D
sampler contract and hardware-sRGB-off. A new state-specific check is required:
[projected texture state](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dtexturetransformflags)
is honored by PS1.1–1.3 and must not silently become unprojected PS2 TEXLD.
These originals never write oT0.w, whereas the documented projected contract
requires all four coordinates. Therefore projection is not a defined source
contract for these nine originals: positively establish PROJECTED off using
cached successful TSS state; unknown/projected submissions retain native routing.
Do not change native state to make an otherwise unsupported draw eligible.

Keep the original sample alpha isolated throughout promotion. Reuse the current
source policy `E=S(cap(decode(S(T)))*h*gain)`, with decoded-result capping before
fade/gain, full-precision new math and +0 E alpha. Preserve the scalar DP3 in the
native branch initially instead of optimizing it to MOV. The original alpha
co-issue can be serialized because both instructions read independent live
sources. New native oC0 is an explicit complete output; original PS bytes remain
immutable for fallback, but **the promoted native body is not byte-identical**.
Opaque comments may be retained; CTAB/compiler metadata is not executable proof.

Neither `_pp` nor full precision promises exact historical SM1 rounding on all
D3D implementations. `_pp` can be ignored and does not specify an 8-bit fixed-
point pipeline; [PS1xMaxValue](https://learn.microsoft.com/en-us/windows/win32/api/d3d9caps/ns-d3d9caps-d3dcaps9)
exposes range, not a rounding emulator. The mandatory first probe is original
VS1/PS1 versus unchanged VS1/promoted PS2 on the same device: native B RGBA,
alpha-test boundary and interpolation parity, then B/E/coverage. Keep analytical
native-parity claims inside the reported SM1 arithmetic range; larger finite
FP16/HDR samples are separate operational boundary probes, not a presumed
well-defined historical SM1 result. Use documented
caps and an explicit functional capability result, not DLL hashes/private
backend structures. X3 success is not native Windows execution evidence.
Do not weaken native B's exact-fallback guarantee merely to pass this probe.

If promotion cannot preserve native output, the portable alternative is a
bounded synchronous auxiliary draw while the submitted buffers/resources/state
remain owned: execute the original PS once into native B, and a separately
qualified energy/coverage writer against the same unchanged VS and depth state.
This needs a new replay/rollback contract and doubles source geometry work; it
is **not approved by the current same-draw route**. It requires exact topology,
index/stream offsets/frequencies and clip/cull/alpha/depth coverage, exclusion of
active query/state-block/reentrant/conflicting writes, references until commands
are issued, native-B adoption on auxiliary failure, and unavailable supplemental
history after partial writes. No stable game-object identity is needed for an
immediate owned submission. If that contract cannot be established, fail before
enhancement and report the capability gap; do not mislabel fallback as support.

## Screen blend is a separate missing equation

The [existing runtime evidence](../reverse-engineering/effects-engine-remaining-emission.md#bounded-runtime-evidence)
contains 54 bullet draws with ADD/ONE/INVSRCCOLOR, including 19 alpha-tested draws.
For normalized native source `(q,a)` in [0,1] and encoded destination A, RGB is
`B=q+A*(1-q)` and non-separate alpha is `a+Aalpha*(1-a)` (with actual target/blend
precision). Out-of-range floating-point blend factors need their own documented
and actual-device qualification; do not extend this normalized equation by
assumption. This cannot use the additive compositor `encode(decode(A)+E)`.
Rejecting all screen or alpha-tested draws would leave this observed population
unconverted. Other pairs have no assigned blend class from this observation.

The orchestrator must select the desired linear screen law before implementation:
retaining native chromatic attenuation gives candidate
`C=encode((1-q)*decode(A)+E)`, while defining screen in the decoded domain changes
its attenuation as well. These differ at gain zero, HDR sources and overlap;
native bytecode proves neither radiometric interpretation. In either case,
retain original B and its raw alpha as the post-source fallback/alpha authority.
Do not infer source transmission from `B-A` after blending or assume sampled
alpha is the RGB attenuation: native q contains texture RGB times vertex fade.

Even after that decision, shared D3D9 MRT blend factors must be accounted for:
INVSRCCOLOR is evaluated against each RT's own source output. It cannot attenuate
an accumulated E buffer by native q while E outputs decoded RGB. A bounded
screen implementation should initially compose one submitted source at a time,
with separate q/transmission and E payloads cleared for that source, plus
persistent coverage. A four-RT B/E/q/M layout is one candidate, not a baseline
requirement; a three-RT alternative needs explicit scratch/persistent-mask
transport (for example a separately preserved alpha coverage lane), consumer
support and failure proofs. None of this is achieved by just enabling the
screen blend enum in current admission. Do not build a generic compositor graph.
Test source alpha-test with exactly the original sampled alpha in every output
path, preserving surviving/depth-tested geometry and raw B alpha. Once these
contracts are selected, qualify all nine pairs under every admitted blend class,
including the captured screen/alpha-test state; no per-alias user capture is a
prerequisite.

## Bounded implementation and qualification sequence

1. Extend the existing pure emission proof/tests with six SM1 identity rows,
   nine exact pairs and two authored promotion shapes. Keep all current SM2
   outputs byte-exact; validate comments/framing, alpha co-issue independence,
   malformed/mutated/aliased input failure and output nonmutation. No generic
   translator or stripped-body admission. Reuse existing numerical sanitizer/
   gain tests and add scalar-X versus vertex-W discrimination.
2. Run a small all-nine original/promoted/native-B/MRT device qualifier before
   integration. Include DXT/UNORM and FP16 inputs, non-dyadic filtered samples,
   mip/filter/bias/address, nonuniform vertex alpha, clipped/perspective/flat
   triangles, WRAP0, source RGB/fade/alpha/gain zero, finite HDR, color boundaries,
   exact alpha tests, all fog flag/clip layouts, and untouched-position checks.
   Select the native-parity result explicitly; retain a real failing witness if
   precision or legacy sampling prevents exact promotion.
3. Implement the chosen ordered screen law/transport, native recovery and
   alpha-test support alongside additive handling. Cache complete pair/model/
   multiplier/state metadata at creation/setters; invalidate successful and
   failed TSS setters, state-block Apply/unknown state and Reset appropriately.
   Keep one bounded pair check at refresh, no per-draw allocation/hash/translation.
4. Extend the existing detached/live runner once for the complete nine-pair
   group: actual untouched VS, accepted and disappearing masks, exact B alpha,
   ordered additive/screen/opaque overlap, query/state/refusal controls,
   failed-setter/source/composition ownership recovery and actual Reset. Reuse
   the qualified supplemental current/disappearing TAA consumer; do not invent
   object motion or write depth for the effect geometry.

For the explicit conservative native-body promotion, three-output estimates are
**28 ALU + one TEX for scalar** and **27 ALU + one TEX for bullet** (serialize
alpha MOV, complete oC0, plus the existing 24-slot prefade/decode/gain/coverage
addition). r0–r3 and c0/c30/c31 suffice; no more than four of the twelve baseline
PS2 temporaries, no flow control, no new interpolator. These are planning counts,
not a generated-program certificate; the implementation must independently count
weighted slots (POW costs three) and validate full output writes. Require PS2,
three FP16 MRTs and established post-pixel-blend/depth caps for the additive
candidate. Screen transport may add a fourth target or extra copy, and replay
adds a second geometry submission if selected; neither cost may be hidden in
this estimate. Measure creation separately, then paired source/composition/event
costs and steady allocation/lookups for the chosen path. No FPS conclusion is
available from this offline study.
