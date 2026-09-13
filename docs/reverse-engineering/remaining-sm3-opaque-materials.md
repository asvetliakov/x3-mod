# Remaining opaque SM3 material contracts

Read-only follow-up after the 110-pair source group, 2026-09-13. The complete
opaque matrix leaves **52 SM3 pairs / 19 new VS / 38 new PS**. These are coherent
full alias/toggle groups; the XT families overlap and must be unioned rather
than added. No new production/profile/test files were changed for this study.

| Complete next group | Pairs | Distinct VS / PS | Existing motion classification | Principal new contract |
| --- | ---: | ---: | --- | --- |
| Asteroid DEFAULT + BUMPMAP | 6 | 6 / 4 | 3 A, 3 B | Detail/base RGB weighting, alternate UV packing, new VS source positions and relocated base-BUMP varying |
| Boron DEFAULT + BUMPMAP | 12 | 6 / 8 | 12 B | Fixed palette RGB, view-dependent color weights, no spare PS input for BUMP material RGB |
| Paranid DEFAULT + BUMPMAP | 20 | 6 / 12 | 20 B | Fixed palette/view mixing, separate lobe coefficients, no spare PS input for BUMP material RGB |
| XT standard + damage + terraformer, all three techniques | 14 | 2 / 14 | 12 C, 2 unsupported | Static branch joins, decal/detail/occlusion roles, full BUMP input occupancy, two dynamic damage branches |

Only XT's DEFAULT VS `494fe349b8bc12ec` is already covered. The row counts above
are 20 referenced VS with that one overlap, hence 19 new identities. Across all
52 pairs the current motion inventory admits 50; the other two are damage BUMP
PS `31445adb0a62d134` and `d51cf763125cb85a` on VS `37c34a7478544c14`, refused for
`ps_control_flow_not_static_boolean_if`. Their sampled-data `IFC` is a real
control-flow extension, not a missing fingerprint or position-quad defect.

**Recommended next group: all six Asteroid pairs.** DEFAULT is base
`b0602757fce6e870`/`517540ae6d5e5410`, with both `0c223ad11bce02d5` and
`233d17d26ce0c1fc` using PS `7a0c3388065bb08d`. BUMP is base
`167eb2d5629ab9d3`/`d44db87778a43b61`, with both `12b8a13f13fe8cfe` and
`330ceb9dd874ede2` using PS `550c2a4d4d3ed70f`. The base programs use separate
base/detail UV varyings; toggles pack detail UV into the first varying's ZW.
They use unit diffuse, a cubic specular response and `sat(3*NdotL)`, with sampled
specular red as data; the conventional hull outer factor 3 is absent. Detail
and base texture RGB have separate scalar strengths and are added before the
lighting product. There is no reflection cube or additive lightmap. Alpha is
base texture alpha times native vertex alpha and is written before final RGB.
BUMP retains AG reconstruction with shifted normal/view/basis varying indices.
The four PS are at most 448 DWORDs / 47 original weighted slots. All six VS
require new point/emissive and geometry proofs; four have interleaved, but
already motion-admitted, position DP4s. Position source registers span r0/r1/r2
and matrices c0–3/c24–27. This needs exact per-VS sites rather than rejecting
noncontiguous quads or borrowing the current hull offsets.

The resource constraint is **physical PS register occupancy**, not merely an
unused TEXCOORD number. Boron DEFAULT and Paranid DEFAULT use motion v7,
depth v8, leaving v9 for an appended RGB varying. Their BUMP programs already
use v0–v7; motion takes v8 and current depth takes v9, leaving no complete input
register for RGB. XT BUMP/LOW has the same occupancy. Appending v10 is outside
the SM3 PS input register range. A bounded lane-packing or recomputation design,
or a separately proved upgrade of existing RGB interpolation that preserves
native alpha, is needed before claiming full DEFAULT/BUMP family coverage.
Asteroid BUMP base uses motion v7/TEXCOORD6 and depth v8/TEXCOORD7, leaving
v9; toggles use motion v6/TEXCOORD5 and depth v7/TEXCOORD6, leaving v8/v9.
Asteroid DEFAULT toggles use depth TEXCOORD3, while base uses TEXCOORD5.
These differences need explicit pair-local contracts, not a single BUMP flag.

This is not a claim that every component is occupied. Exact original masks show
Boron BUMP v1.xy, v2–v6.xyz, and v7.xy at base or v7.x in singles; Paranid BUMP
has v1.xy, v2–v6.xyz and v7.x; XT BUMP has v1.xyzw, v2–v6.xyz and v7.xy. Thus
scalar-only v7 in Boron singles/Paranid has three spare lanes, and other paths
have spare lanes distributed across existing inputs. However, these original
inputs are declared partial precision (some also centroid), so putting full-
precision RGB in the same declaration is not yet a valid precision contract.
Removing PP would also change the original scalar/geometry interpolation. The
current motion varying uses all four clip components; the current-depth PS
input reads two components, although its VS export duplicates those values
across XYZW. Depth's spare pair alone cannot hold RGB. A bounded repacking can
therefore be investigated, but requires exact lane reads, matching VS writes,
centroid/PP behavior, alpha preservation, and both depth modes to be proved.
The inventory's whole-register availability cannot resolve that decision alone.

Boron and Paranid's additional varyings are scalar palette weights and geometric
view inputs, not RGB to decode indiscriminately. Their fixed palette constants
are authored RGB sources; reflection tint, view-dependent highlights and
albedo mixing must remain separate from raw weights. Representative Boron
DEFAULT `39eb3c2258a516e1` has exponent 10 and 0.4 diffuse, plus a fifth-power
view term; Paranid `675f9077d8fd21c4` has exponent 10, 0.5 diffuse, outer specular
factor 6 and a ninth-power view term. These representative findings are not yet
a complete per-variant numerical proof. Some new VS contain ABS/EXP/LOG, which
also need documented cost entries in the bounded instruction-budget checker.

XT requires more than the existing standard-lighting scalar parameterization.
Its original programs combine optional decal/detail RGB, optional palette
mixing, native alpha-derived occlusion, additional texture sampling and static
boolean branch merges; damage adds a sampled-data branch. The largest PS is
1791 DWORDs, beyond the current material 1392 guard. Increasing that guard alone
would not supply the missing semantics. There is also a native DEFAULT linkage
question to resolve: the shared VS `494fe349b8bc12ec` declares TEXCOORD0.xy and
TEXCOORD1–3, while XT DEFAULT PS `fffdabd910793aba` reads TEXCOORD0.zw and declares
palette-weight TEXCOORD5/6. Determine the native branch/use boundary or intended
producer before inventing missing values. Existing class-C motion admission
and a successfully created original shader do not prove those material inputs.

The concise counts and linkage/resource results were queried from the complete
motion inventory and exact local program tokens. Local working summaries live
under `/tmp/x3-next-sm3/`; representative math came from targeted disassembly,
with token operands authoritative for precision. This continuation does not
qualify new transformed bytecode, native Windows behavior, or GPU performance.
