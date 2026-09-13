# Remaining conventional SM3 hull material study

Read-only study, 2026-09-13. This follows the 49-original/70-pair
[Split and standard lighting group](../architecture/linear-standard-materials.md),
qualified at checkpoint `73f5c51`; the new group described here does not extend
the installed route.

The next conventional hull group contains **24 new PS, no new VS, 40 pairs and
168 archive pass occurrences**: shared Khaak/Teladi/Teladi_nodiff/Xenon BUMPMAP,
Split BUMPMAP, and Terran DEFAULT/BUMPMAP. All pairs already have the existing
class A or class B motion plan. The current two material register layouts fit
this group, including their fixed scratch registers and 13-entry RGB-site
capacity. No new allocator or shader-model capability is indicated by this study.
A completed implementation would bring this bounded SM3 material set to
73 originals/110 pairs; this is not complete archive or lower-quality coverage.

## Archive and pair boundary

Every group below contains the complete six-PS/ten-pair SM3 contract: base
(two-light affine), single-light affine, and single-light nonaffine, each with
ordinary and VFACE variants. Both toggle VS pair with all four single-light PS.
Each alias includes its ordinary, `2s`, `_0000` and `_0001` basenames and all
recorded toggle directories, rather than only the base effect file.
The shared race names overlap exactly: their ten pairs have 96 combined pass
occurrences. Split BUMPMAP and each Terran technique have 24 occurrences each.
These 168 occurrences are archive identities, not measured scene draws.

| Technique | Base VS | Both single-light VS | Motion/depth/material semantics |
| --- | --- | --- | --- |
| Terran DEFAULT | `53a0a641107ed76c` | `719856ce0c213220`, `badefd5143b3024f` | Class A: TEXCOORD4/5/6 |
| All three BUMPMAP groups | `4944d81dfe531b37` | `19a246a56e9d9700`, `44c4a41ca92ae2e3` | Class B: TEXCOORD5/6/7 |

The standard DEFAULT base depth-TEXCOORD7 exception does **not** occur in this
group. All six VS identities are already covered, so their point-light RGB,
scaled material emissive, geometry, fog, alpha and position programs stay shared.
Do not create family-specific variants of a VS already cached by original identity.

The following counts are original whole-program DWORDs and documented weighted
static instruction slots. They include cube TEXLD=4, DP2ADD/LRP=2 and POW/NRM=3;
they are not the decompiler's approximate count and are not transformed budgets.

| Family / technique | PS FNV-1a identity | Shape | Face | DWORDs | Original slots | RGB sites |
| --- | --- | --- | :-: | ---: | ---: | ---: |
| Shared BUMP | `1f26d41bcb7dac1e` | base affine | no | 1332 | 66 | 13 |
| Shared BUMP | `bdcdb3ab996ae4e0` | base affine | yes | 1358 | 71 | 13 |
| Shared BUMP | `78963cdc7c710e04` | single affine | no | 1255 | 52 | 7 |
| Shared BUMP | `1ed1bf0fdec00e1a` | single affine | yes | 1281 | 57 | 7 |
| Shared BUMP | `2b04461d0dae038b` | single nonaffine | no | 336 | 48 | 7 |
| Shared BUMP | `acc83ed2509d84a1` | single nonaffine | yes | 362 | 53 | 7 |
| Split BUMP | `3006f8030a467739` | base affine | no | 1312 | 65 | 12 |
| Split BUMP | `d6e8bdde0e4c515f` | base affine | yes | 1338 | 70 | 12 |
| Split BUMP | `e5ea78b8b0b0fe07` | single affine | no | 1243 | 51 | 6 |
| Split BUMP | `f42202faf57a3c89` | single affine | yes | 1269 | 56 | 6 |
| Split BUMP | `769c3814fc0efba8` | single nonaffine | no | 324 | 47 | 6 |
| Split BUMP | `22cc5b05a55ef61e` | single nonaffine | yes | 350 | 52 | 6 |
| Terran DEFAULT | `ef2bf556f207b8bd` | base affine | no | 1256 | 52 | 11 |
| Terran DEFAULT | `91b6c09eb47f8555` | base affine | yes | 1282 | 57 | 11 |
| Terran DEFAULT | `cc09f17db377fd9e` | single affine | no | 1180 | 38 | 6 |
| Terran DEFAULT | `3755809bd40afc13` | single affine | yes | 1206 | 43 | 6 |
| Terran DEFAULT | `61418505e5d8f998` | single nonaffine | no | 261 | 34 | 6 |
| Terran DEFAULT | `b5f1d4145171026b` | single nonaffine | yes | 287 | 39 | 6 |
| Terran BUMP | `3602b05ce11ca6ff` | base affine | no | 1324 | 64 | 11 |
| Terran BUMP | `8e58ac79b59b02b1` | base affine | yes | 1350 | 69 | 11 |
| Terran BUMP | `042c9ae16f41feff` | single affine | no | 1248 | 50 | 6 |
| Terran BUMP | `68f0dd6791fd7d3d` | single affine | yes | 1274 | 55 | 6 |
| Terran BUMP | `5c823b8507fa1442` | single nonaffine | no | 323 | 46 | 6 |
| Terran BUMP | `a6e1328c0bb3f401` | single nonaffine | yes | 355 | 51 | 6 |

The new maximum is **1358 DWORDs / 71 original PS slots**, so the current
1392-DWORD maximum input guard already accommodates every candidate. The shared
base face PS sets both maxima. The existing VS maximum for this group is 62
original static slots. Final combined-program budgets still require the actual
transformer; these original counts alone do not qualify its output.

## Lighting, scalar data and normal behavior

Using the already documented normal/view convention, let `d = sat(N·L)`,
`h = sat(reflect(-L,N)·normalize(V))`, and `m = specular_texture.r`.
The directional response is `D*d + 3*m*h^p*sat(3*d)`, with independently colored
light RGB applied in the original order. Reflection is tinted by `m*albedo` and
its separate cube coefficient. This summarizes the native algebra; implementation
must preserve original instruction order and partial precision for angular/data
work, including the native POW where present.

| Group | Directional diffuse D | Specular exponent p | Reflection coefficient | Native exponent evaluation |
| --- | ---: | ---: | ---: | --- |
| Shared BUMPMAP | 0.5 | 6 | 0.5 | x², x⁴, x⁶ multiply chain |
| Split BUMPMAP | 0.5 | 10 | 1 | Native POW with literal 10 |
| Terran DEFAULT/BUMPMAP | 1 | 5 | 1 | x², x⁴, x⁵ multiply chain |

All coefficients in this table are fixed shader facts. None of these 24 PS has
the standard-lighting application's diffuse/specular/power/reflection scalar
parameters. Do not add application-coefficient handling merely because Terran's
unit diffuse coefficient differs from Argon's 0.4. The outer specular coefficient
remains 3, and the inner saturated angular factor remains `sat(3*d)` throughout.

Shared BUMPMAP's half coefficient is `c9.y` in base, `c7.y` in affine-single,
and `c4.y` in nonaffine-single variants. Each is consumed by both directional
diffuse and cube tint. Its cube path adds a distinct RGB multiply relative to
Argon/Split/Terran BUMPMAP: retain that site and remove partial precision from
its linear RGB result with the other color-dependent operations.

Split's tenth-power literals are `c8.x` in base, `c7.x` in affine-single,
`c3.w` in ordinary nonaffine-single and `c4.x` in the face nonaffine-single.
Its diffuse halves are respectively `c8.z`, `c7.z`, `c4.y`, and `c4.z`.
Single-light packing combines `3*d` and `0.5*d` into two lanes before saturation;
the face nonaffine layout changes the packed constant swizzle. This is a data
operation, not an RGB vector. Keep its lane packing and native POW untouched.

Terran sums unit-weight diffuse with masked specular directly. Its base DEFAULT
and BUMPMAP layouts fuse the final specular scale into a MAD with diffuse, unlike
Argon's separate specular multiplication plus 0.4 diffuse MAD. The DEFAULT base
uses different live normal/view registers from the covered Argon/Split/standard
DEFAULT schedules. Its face variant moves normalized-view production after the
light-reflection MAD; both consume independent registers at that point. These
are semantic schedule differences despite the familiar source/output roles.

All 18 BUMPMAP PS reconstruct the same **AG** normal as the existing Argon
BUMPMAP path: sampled alpha becomes the BINORMAL `v5` coefficient, sampled green
becomes the TANGENT `v4` coefficient, and the native DP2ADD/RSQ/RCP chain obtains
the geometric-normal `v3` coefficient before normalization. Preserve the native
behavior for negative/zero reconstruction arguments; do not replace it with a
new clamp. Red and blue are unused normal data. No XYZ-normal LOW path occurs
in this group. VFACE normalization/sign handling and per-pixel reflection retain
the same geometric basis. The reflected cube coordinate uses the same live
reconstructed `r0.xyz` normal and normalized `r1.xyz` view; its producer is five
DWORDs before the sampler-4 fetch. A separate dot/doubling chain begins 21 DWORDs
before that fetch. Neither color conversion nor scratch use may clobber them.

Terran DEFAULT instead normalizes the geometric normal from `v3`, applies the
face sign where present, and samples the cube using `v4`. Its view comes from
`v2`; these geometric varying roles are not RGB sources.

## Exact conversion boundaries

Source roles remain the current contracts:

- DEFAULT: s0 diffuse, s1 scalar specular red, s2 lightmap/emissive RGB, s3 cube RGB.
- BUMPMAP: s0 diffuse, s1 AG normal data, s2 scalar specular red, s3 lightmap/emissive
  RGB, s4 cube RGB. Only s0/s3/s4 get RGB conversion.
- Affine diffuse completes in `r3.xyz` for DEFAULT or `r4.xyz` for BUMPMAP. Preserve
  the authored affine operation and convert after the final DP4. Nonaffine diffuse
  converts sampled `r1.xyz` directly. All texture instructions retain original PP.
- Directional RGB is c5/c7 for base, c5 for affine-single, c2 for nonaffine-single;
  every whole-vector source is a conversion input, including independent diffuse
  and specular consumers of each base light. Packed scalar constants are excluded.
- Every final RGB write is the independent sum into `oC0.xyz`. Alpha remains
  `lerp(diffuse.a, lightmap.a, glow)*v0.w`, with glow `c3.x` for affine or `c0.x`
  for nonaffine. Diffuse `r1.w`, lightmap `r0.w`, then interpolated `r2.w` remain
  live through their separate consumers. Preserve all native alpha precision.

Offsets below address instructions in the **original** whole stream. `S` lists
fetch offsets in sampler-index order. `A` is the final affine DP4, or absent;
conversion follows A+4, or S0+4 without affine. Lightmap/cube conversion follows
the corresponding fetch+4. `C` is the COLOR0 clamp to replace with the existing
full-precision RGB varying; `F` is the RGB output and alpha follows at F+4.

| PS | S offsets | A | C / register | Direct RGB consumers by c-register | F |
| --- | --- | ---: | --- | --- | ---: |
| `1f26d41bcb7dac1e` | 1272,1095,1235,1314,1268 | 1289 | 1260 / r6 | c5: 1225,1230; c7: 1185,1217 | 1323 |
| `bdcdb3ab996ae4e0` | 1298,1098,1261,1340,1294 | 1315 | 1286 / r6 | c5: 1251,1256; c7: 1211,1243 | 1349 |
| `78963cdc7c710e04` | 1194,1077,1161,1237,1190 | 1211 | 1182 / r5 | c5: 1223 | 1246 |
| `1ed1bf0fdec00e1a` | 1220,1080,1187,1263,1216 | 1237 | 1208 / r5 | c5: 1249 | 1272 |
| `2b04461d0dae038b` | 292,175,259,318,288 | — | 280 / r4 | c2: 304 | 327 |
| `acc83ed2509d84a1` | 318,178,285,344,314 | — | 306 / r4 | c2: 330 | 353 |
| `3006f8030a467739` | 1256,1095,1219,1294,1252 | 1273 | 1244 / r6 | c5: 1209,1214; c7: 1168,1201 | 1303 |
| `d6e8bdde0e4c515f` | 1282,1098,1245,1320,1278 | 1299 | 1270 / r6 | c5: 1235,1240; c7: 1194,1227 | 1329 |
| `e5ea78b8b0b0fe07` | 1186,1077,1153,1225,1182 | 1203 | 1174 / r5 | c5: 1211 | 1234 |
| `f42202faf57a3c89` | 1212,1080,1179,1251,1208 | 1229 | 1200 / r5 | c5: 1237 | 1260 |
| `769c3814fc0efba8` | 284,175,251,306,280 | — | 272 / r4 | c2: 292 | 315 |
| `22cc5b05a55ef61e` | 310,178,277,332,306 | — | 298 / r4 | c2: 318 | 341 |
| `ef2bf556f207b8bd` | 1193,1180,1238,1225 | 1213 | 1202 / r1 | c5: 1170,1175; c7: 1130,1162 | 1247 |
| `91b6c09eb47f8555` | 1219,1206,1264,1251 | 1239 | 1228 / r1 | c5: 1196,1201; c7: 1156,1188 | 1273 |
| `cc09f17db377fd9e` | 1116,1103,1162,1149 | 1136 | 1125 / r1 | c5: 1140 | 1171 |
| `3755809bd40afc13` | 1142,1129,1188,1175 | 1162 | 1151 / r1 | c5: 1166 | 1197 |
| `61418505e5d8f998` | 222,201,243,230 | — | 214 / r1 | c2: 217 | 252 |
| `b5f1d4145171026b` | 248,227,269,256 | — | 240 / r1 | c2: 243 | 278 |
| `3602b05ce11ca6ff` | 1268,1095,1235,1306,1264 | 1285 | 1256 / r6 | c5: 1225,1230; c7: 1185,1217 | 1315 |
| `8e58ac79b59b02b1` | 1294,1098,1261,1332,1290 | 1311 | 1282 / r6 | c5: 1251,1256; c7: 1211,1243 | 1341 |
| `042c9ae16f41feff` | 1191,1077,1158,1230,1187 | 1208 | 1179 / r5 | c5: 1216 | 1239 |
| `68f0dd6791fd7d3d` | 1217,1080,1184,1256,1213 | 1234 | 1205 / r5 | c5: 1242 | 1265 |
| `5c823b8507fa1442` | 283,169,250,305,279 | — | 271 / r4 | c2: 291 | 314 |
| `a6e1328c0bb3f401` | 315,178,282,337,311 | — | 303 / r4 | c2: 323 | 346 |

The largest color-dependent site inventory is 13, in shared base BUMPMAP. Shared
single BUMPMAP has 7; Split BUMPMAP has 12 base/6 single; both Terran techniques
have 11 base/6 single. This fits the current fixed Pixel record. Original masks
are RGB-only at those sites; geometry and scalar packing retain native precision.
The shared cube-scale site's extra RGB operation must not be missed by borrowing
another family's shorter site list.

## Resource and admission implications

Class A retains VS o6/o7/o8 and PS v5/v6/v7 for motion/depth/material RGB.
Original Terran DEFAULT PS uses r0–r4 at base and r0–r3 in singles; motion occupies
r5–r7, leaving material scratch r9/r11/r12/r13 available. Class B retains VS
o7/o8/o9 and PS v6/v7/v8. Its original PS uses r0–r6 at base, r0–r5 in affine
singles and r0–r4 in nonaffine singles; motion correspondingly uses r7–r9,
r6–r8 and r5–r7. Material scratch r10/r11/r12/r13 remains available in every row.
The current VS material scratch r7/r8/r9 and the common c248/c249 VS,
c212/c213 PS definitions remain disjoint from original and temporal resources.
The original VS relative-constant proof still relies on the existing i0 count
bound [0,8]. Sampler masks stay 0x0f for DEFAULT and 0x1f for BUMPMAP.

This expansion consumes the current uncovered Terran DEFAULT witness
`53a0a641107ed76c`/`ef2bf556f207b8bd`, Split BUMPMAP witness
`4944d81dfe531b37`/`3006f8030a467739`, and Terran BUMPMAP witness
`19a246a56e9d9700`/`042c9ae16f41feff`. Preserve negative coverage checks when
implementing it. After these 40 pairs, the complete SM3 motion inventory has no
remaining class-A/B uncovered PS paired with any of the seven covered VS.
Four class-C static-branch pairs remain on VS `494fe349b8bc12ec`; one is
`fffdabd910793aba` (xt_standard_lighting/xt_standard_lighting_damage DEFAULT).
It is a candidate for a shared-VS negative only after checking the existing
class-C motion route required by that test. A deliberately unsupported cross-pair
is a different assertion and does not by itself replace a motion-success witness.

The subsequent [pure implementation](../architecture/linear-hull-materials.md)
records source proofs, reference profiles and transformed budgets. GPU/live
qualification and review of that group remain separate acceptance steps. The existing native Windows source contract is unaffected by
these shader findings; native Windows behavior has not been tested. This study
adds no per-draw work and makes no performance claim about the future transformer.

## Evidence and reproduction

The complete motion inventory was queried by technique and each complete alias,
and original programs were independently reclassified: all **40/40** reproduced
the recorded class, refusal-free classification and exact insertion plan. Selected
resource-budget checks passed for their 30 distinct programs (24 PS plus six
existing VS). All **24/24** PS passed targeted source-fetch, affine, raw-alpha
liveness and selected temporary checks; all 18 bump normal/refl chains were
checked by exact operands and intervening-lane-write predicates. Every face body
was compared with its ordinary counterpart after resolving DEF scalar lanes;
the only lighting-body differences were the harmless unused lanes in Split's
nonaffine packed coefficient and the Terran DEFAULT base view/reflection schedule
noted above. Full semantic certification is deliberately left to the next
implementation's generator and negative tests.

Inputs: local `/tmp/x3-shader-sweep/programs` and matching disassembly; the complete
`verification/results/motion-output-profiles.json` inventory SHA-256 is
`fa8b70e0fb897994bdb939a1810f6574eba59611b4391d92464fe0f86bf60f58`.
Session-local bounded extraction/check helpers and derived metadata are under
`/tmp/x3-remaining-hulls/`. Original shader bytes and disassembler output remain
local and untracked. No Wine execution, compilation, production changes or game
launch was performed for this study.

## Continuation: the remaining 52 opaque SM3 pairs

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
