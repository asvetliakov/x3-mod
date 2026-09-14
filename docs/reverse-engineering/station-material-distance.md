# Station material distance changes: shared producer and Run 28 limits

Native/shader research and capture-only diagnostic implementation, 2026-09-14. The user's reproduction is the red-marked docking-port structure on Federal Argon Shipyard; similar changes were seen elsewhere. Neither that description nor a projected node center identifies its shader or mesh. The capture-only diagnostic extension is now installed in combined source `8442f43`; see the [install record](../../verification/results/linear-material-install.json). New target-to-node association evidence still requires a gameplay capture.

The same ordinary native distance-fade producer can supply BUMP, DEFAULT and XT effects. It is not an asteroid-specific branch. This establishes a conditional material contract, not the cause of the photographed port transition. Run 28's four captured frames have shader fog disabled on every inspected relevant material draw.

## Native producer

Installed X3AP.exe SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`; addresses are preferred VAs. Existing material-submission and asteroid notes were checked against new targeted Ghidra instruction exports and on-disk PE bytes. Raw output remains in `/tmp/x3-station-fade/{material-asm,native,parent}.txt`.

`4c0150` processes each material subset and resolves optional effect parameters by name. The fog block does not test station, asteroid, technique name, shader hash or model identity. Its relevant guards and data are:

| Site / field | Contract |
| --- | --- |
| `4c219d..4c2216` | Node `+130 & 20000` selects a separate path ending at `4c2f4e`, bypassing the ordinary per-material lighting/fog block. |
| `4c2b46` | Camera `+270 & 10000` must be set. |
| `4c2b59` | Node `+12c & 02000000` must be clear. |
| `4c2bbf..4c2c15` | Distance D is computed from signed node translation `+b0/b4/b8` minus camera `+30/34/38`, float squares/sum, native square-root and integer conversion. |
| `4c2c63..4c2c6f` | Compare `F-D <= F-N`; with ordinary finite, nonoverflow values this is `D >= N`. N is camera `+36c`. |
| F | Camera `+370`, clamped to at least 100,000,000 native units when configuration `*606f34+768 == 2`, or 500,000,000 when it is >= 3; lower configuration uses camera F. |
| `4c2cea / 4c2cff` | Fog-on sets optional `g_ZWriteEnable=0` and `g_AlphaBlendEnable=1`. |
| `4c2d01..4c2d3c` | Only a previously unblended material with descriptor `+1a4 == 0` gets forced SrcBlend=5 / DestBlend=6. Existing transparency is not universally normalized. |
| `4c2d43..4c2df2` | With context scale s, compute N*s and F*s on the native x87 path, then emit float FogClip `(F*s/(F*s-N*s), 1/(F*s-N*s), 0, 0)`. Preserve operation/rounding order in any exact oracle. |
| `4c2e04..4c2e59` | EnableFog true for the fade branch; bypass writes FogClip `(1,0,0,0)` and false. |

Cached parameter blocks restore baseline state before per-draw overrides. Independent nonzero node `+13c` sets AlphaValue to value/255 and enables blending (`4c2a72..4c2b41`); its blending fallback can differ from distance fog. A later `node+130 & 08000000` alpha-override path also exists. A blended draw is therefore not sufficient evidence of distance fog.

After the fog block, `4c300e/4c3022` read current effect blend/test values. Under the ordinary no-glow path and the configuration/capability conditions documented in `asteroid-fog-temporal.md`, `4c379a..4c3818` chooses RGB mask 7 for ordinary blend or alpha-test cases, RGBA mask 15 for both disabled, and sets AlphaValue=0 for the opaque case without node override/special descriptor. The zero alpha is native glow bookkeeping when blending is off. Other glow/special paths have different rules. Shader alpha, alpha testing, blend factors and output mask must be considered together.

The decision uses submitted node origins. Shader fade uses vertices. Each child can cross independently, and a large mesh can straddle the threshold. The HUD distance path `424e7b..424e87` passes selected/view object `+70` node `+30` positions to its native distance helper. It is not the submitted child's `+b0` camera distance, so 690 m / 666 m cannot establish N, D or a shader fade value.

## Shader and effect contracts

The bounded archive scan covers 88 SM3 Argon, `standard_lighting` and XT effects, 240 P0 pass occurrences and 64 exact pairs (20 Argon, 30 standard, 14 XT). Eight VS and 44 PS establish:

```
vertexAlpha = AlphaValue * (EnableFog ? saturate(FogClip.x - FogClip.y * vertexDistance) : 1)
sourceAlpha = interpolated(vertexAlpha) *
              (EnableGlow * LightMap.a + (1 - EnableGlow) * Diffuse.a)
```

The pixel COLOR0 input, LRP and final multiply use native partial precision. No additional clamp is inserted around EnableGlow: 0 selects diffuse alpha, 1 lightmap alpha. DEFAULT uses diffuse/lightmap samplers s0/s2; BUMP and LOW use s0/s3. All 44 PS have exactly one alpha output, no TEXKILL, and their alpha/sample path is outside the XT static/dynamic branches. The photographed pixels' sampled alpha and glow control remain unproved. This differs materially from the asteroid's simpler diffuse-alpha contract.

Common Argon/standard VS use b0 / c39.x / c41.xy for fog enable / alpha / clip; two compact layouts use b0 / c18.x / c20.xy. XT BUMP/LOW `37c34a7478544c14` uses b0 / c40.x / c44.xy, while c39 is reflection strength. Never reuse asteroid register assumptions for XT. The four original XT DEFAULT pairings with VS `494fe349b8bc12ec` remain malformed in unrelated varying lanes. The runtime pair-bound repair preserves DEFAULT alpha instructions; this is an authored repaired producer, not portable native parity with the malformed archive pair.

All 240 passes bind the host Z-write, alpha-test/blend, blend-factor, separate-alpha and color-mask parameters through verified scalar identity expressions. The effects default fog off, AlphaValue 1, FogClip (1,0), EnableGlow 0, Z/Z-write on, blend/test off, ONE/ZERO and RGB mask 7. Native submission overrides these defaults. Two hundred passes set ADD and GREATEREQUAL/ref 1; 40 suffixed passes omit those fixed states. Exact derived inventory and reproducible alpha checks remain in `/tmp/x3-station-fade/{inspect.py,analyze_alpha.py,summary.json,alpha-dataflow.json}`; raw archive programs remain local.

## Actual captured boundary

A bounded stream query of `/tmp/x3-bottleX3-run28/session-20260914-013301-216.log` isolates frames 4052–4055; local reductions are `run28-state-slice.json` and `run28-fog-slice.json`. All 821 inspected draws on common Argon/standard/Asteroid fog VS have b0=0 (200, 207, 207, 207 by frame). This is one short burst, not a before/after approach pair.

Frame 4052 has:

- 20 Argon BUMP alpha-tested draws: `4944d81dfe531b37/5e0a10fe752b6140`, Z-write on, blend off, RGB mask 7, motion gate 4.
- 12 Argon DEFAULT alpha-tested draws: `53a0a641107ed76c/63f96eba9eea7880`, same state gate.
- One reviewed source-over draw: `4944d81dfe531b37/64bac8bb307eb896`, node `1ae7a178`, handle 52029, model key `543f`, LOD 2, six triangles. Z-write off, SRCALPHA/INVSRCALPHA ADD, alpha-test off, separate alpha off, RGB mask 7, gate 4, jitter active. Its b0=0 and identity FogClip rule out the shader distance-fog branch for this draw at this time.
- 167 recognized material draws pass the opaque motion route; two additional routed glass draws are outside the installed material table. These are draw records, not identified visible port pixels.

`MotionOutput::evaluate_draw` applies reviewed jitter before gate 4; gate 4 rejects depth-write off, blend on, alpha-test on or non-RGBA mask before linear-material availability is considered. Thus the general source-domain discontinuity remains real: eligible opaque material uses converted lighting; rejected transparency/cutout uses native lighting. This capture establishes coexisting policies but no same-node state, pair or LOD transition, and no pixel-level magnitude.

## Capture-only association extension

Native `489f20` attaches child EAX to parent EDI; exact store `489f54: MOV [ESI+18],EDI` establishes the node parent link. `43d1d0` uses this at `43d268` and `43d3e9`, including recursively built model parts. Target object `+70` supplies its render-node anchor, not every independently drawn child.

The implementation uses the existing object scope and capture readers, without another game hook or broad tracer:

1. Once per capture frame, snapshot active target pointer/id and its readable `+70` node pointer/handle, plus camera identity. Keep explicit validity and epoch; never infer association from proximity.
2. Include current node `+18` and `+13c` from the already-read `0..14f` block. For capture-only target association, walk at most 16 nodes including the submitted node, record raw pointer/handle pairs, stop on null, cycle, invalid read or bound. Reaching the selected object's node is positive descendant evidence; a truncated chain is unknown. Do not use this diagnostic chain as a lifetime identity.
3. Capture camera flags `+270`, N/F `+36c/+370`, scale `*(camera+1c)+2c`, and the relevant configuration value once per distinct capture camera. Node coordinates, model/LOD, shader pair, textures, fog constants, b0, state and motion gate already exist.
4. Two bounded captures on the same approach—before and after the visible change—then discriminate source-domain/state change, native fog coverage change, material/LOD change and unchanged-state minification. Do not add ordinary per-draw logging outside capture or resubmit native work.

The shared fade prototype remains justified by prior asteroid evidence. Extending it to these families requires their exact native/linear alpha contracts and real state admission; it should not be sold as a demonstrated docking-port repair. With unchanged native state, pair, textures and geometry, normal/specular or texture minification and ordinary lighting/view dependence remain candidate explanations, not established causes.


## Implementation bounds and validation

`object_capture.h` is a capture-only pure reader/cache. `Device` owns one fixed cache under the existing capture mutex: 512 unique `(node, handle, parent)` keys and 16 `(camera, handle)` keys. Node cache exhaustion emits id 0 with `ancestry_capacity=1`; camera exhaustion uses `fade_capacity=1`. Repeated keys do no additional diagnostic game reads. New node records walk at most 15 ancestors beyond the submitted node already read by `object_trace`; zero parent, matched anchor, cycle, unreadable data and limit have distinct terminal statuses. A match requires pointer **and** node handle equality. No ancestry or target cache affects temporal/motion identity. Each ancestry record is a sample at its logged first draw index; later references do not prove that every ancestor remained unchanged within the frame.

Active-cockpit lookup reuses the established registry layout at `608504`: current handle at registry +10, table bucket header and at most 32 chain rows, with malformed table, cycle, missing entry and read failure separate from no target. It is evaluated once at the first scoped draw of each capture frame; `object_target` records the sample's draw index. It is a sampled identity, not proof that target/lifetime remained unchanged later in the frame. No chase hook is required. Camera fields are sampled once per unique camera/handle; a later change within the same frame is outside this bounded snapshot contract.

`object_evidence` refers to once-emitted `object_ancestry` / `object_ancestor` and `object_fade` rows. Join on device, frame, reset generation **and diagnostic epoch**, then record id. Every Reset entry, including refusal, invalidates the cache; the next diagnostic sample advances its own epoch even if the production Reset generation did not change. Present frame changes also discard entries lazily at the next capture sample. New Device ownership creates a separate cache. The summary parser preserves raw rows and rejects mismatched draw coordinates without inferring missing associations.

The existing node read remains 0x150 bytes; parent +18 and raw alpha override +13c are copied only when `matrices=true` (the capture path). Normal motion-route reads, locking and game hooks are unchanged. The state snapshot now requests documented D3DRS_FOGENABLE (28) independently of shader b0; absent state rows remain unknown if the native query fails. All new arithmetic is integer address/bounds handling; camera scale and coordinates are printed as raw bits. `object_context` preserves LastError across checked reads and logging, including early return.

Performance inspection: no dynamic allocation, lock acquisition, game callback or native submission is added by these readers. The fixed cache occupies under 7 KiB per Device. Its worst-case key search is 512 small comparisons on capture draws only; no live noncapture draw enters it. Per distinct capture node the new ancestry work is at most 15 checked 44-byte reads and 17 small records; per distinct camera it is one 0x374-byte read plus scale/config fields. Capture cost is intentionally not presented as game FPS. Extra work remains inside the existing capture timing metric.

Focused validation covers the actual pure reader/cache with synthetic memory and brace-extracts the production logging endpoint to exercise LastError, first-sample emission, reuse and failure. It includes malformed and cyclic active registries, ancestor read failure/cycle/depth limit, same pointer with changed handle or parent, target clearing, changed frame/Reset/diagnostic epoch, bounded cache exhaustion, and exact preservation of hostile raw bits. Windows-compatible source is syntax-checked with the project's x86/SSE2/stack flags; no native Windows or Wine behavior is claimed. Independent Sol/high source review approved with no findings. All 24 focused unittest methods pass; the reviewer reproduced them in 2.495 seconds. Full capture.cpp and object_trace.cpp x86 syntax-only compilation passes with SSE2, four-byte incoming stack and warnings-as-errors. No Wine qualification is required to claim these host/source results, and no runtime result is implied.

## Run 11 captures: no fog transition; coexisting lighting on every station model

Triage of the five run-11 F8 frames (`/tmp/x3-bottleX3-run36/`, frames 1476, 1699,
1917, 13681, 14601; the last two are the station approach) on installed `3f06979`:
shader `b0` and `D3DRS_FOGENABLE` are 0 on every inspected BUMP/DEFAULT/port draw
(port draws per frame 2, 2, 3, 2, 1; BUMP 32, 32, 37, 15, 8; DEFAULT 53, 53, 73, 14,
14), so no native distance-fog transition exists in any capture. `object_fade` rows
are per camera only (N/F `0x017d7840/0x01c9c380` early, `0x02faf080/0x03473bc0` at the
station); ancestry rows show `alpha13c=0` for the port draws. Between 13681 and
14601 the surviving station node `0e9d37b8` (model `5471`, LOD 2) repeats an identical
16-draw sequence; node `25180e80` (model `5436`) is present at 13681 and absent at
14601 (visibility, not a state flip). Every station model key mixes converted and
native lighting (routed/refused: `543f` 42/3, `542a` 42/3, `5427` 15/1, `5436` 12/1,
`5471` 30/2); the refused remainder is the source-over port pair
`4944d81dfe531b37/64bac8bb307eb896` (Z-write off, SRCALPHA/INVSRCALPHA, gate 4). The
state precondition for the coexisting-lighting explanation therefore exists on every
station; pixel-level proof and the remedy are the subject of
`docs/architecture/linear-station-source-over.md`.

## Port draw blend state (2026-09-14)

The source-over state of the docking-port draws (`4944d81dfe531b37/64bac8bb307eb896`) is a **fixed material property read from the asset**, not distance fog, not a node override and not the effect's pass defaults. Addresses from the existing `4c0150` export; slices in `/tmp/x3-station-blend/`.

`4c0150` caches the by-name effect handles on the per-subset descriptor (`[[EBP+8]+0xc] + i*0x1a8`, loop head `4c0223..4c023d`; pushes `4c1c51..4c1e0a`, each store follows the next push): `+140` g_FogClip, `+144` g_ZEnable, `+148` g_ZWriteEnable, `+158` g_EnableFog, `+170` g_AlphaBlendEnable, `+174` g_ALPHATESTENABLE, `+178` g_SrcBlend, `+17c` g_DestBlend, `+188` g_AlphaValue, `+18c` g_ColorWriteEnable. `4b8f70`/`4b9010` are the by-name int/float setters (GetParameterByName at vtable `+24`, then `+68`/`+78`).

The material id is the subset descriptor dword at `+00` (`4c0236`), replaced by the node override table `node+1a8`/`node+1ac` (stride `0x80`, id word `+70`, `4c02b2..4c02f1`) when negative. Record = `*(0x608db0) + id*0x3c`; **flags dword at record `+10`**, written once by the loader `4f44a0` (store `4f4723`, stride `4f488f`, count `0x608dac`, base writes only `4f4571`/`4f4586`, freed `4f48e4`). Its parser name table at `0x54dbe0` is the `MPF_*` bit list: `01 ALPHATEST, 02 DESTINATIONBLEND, 04 ALPHABLEND, 08 WIREFRAME, 10 2SIDED, 40 MULTIPLY2X, 80 MULTIPLY, 100 NOFILTERING, 400 SRCCOLOR`.

`4c1779` tests `flags & 0x4c7`; if set it writes `descriptor+1a4 = 1` (`4c178e`), `g_ZWriteEnable=0` (`4c1781..4c1798`), `g_AlphaBlendEnable=1` (`4c17a0..4c17a7`), then factors: MULTIPLY `4c17b6` 9/1, MULTIPLY2X `4c17f0`+`4c1801` 9/3, SRCCOLOR `4c180d` 3/4, **ALPHABLEND `4c1827`+`4c1838` SRCALPHA(5)/INVSRCALPHA(6)**, DESTINATIONBLEND alone `4c187f` 2/2, ALPHATEST alone `4c1864`. Every branch ends at `4c17c7` (DestBlend) and `4c17d3` (`g_AlphaValue=1.0`). `4c18b2..4c18fe` then forces ALPHATEST-without-DESTINATIONBLEND back to opaque (ZWrite 1, ONE/ZERO, alpha test on, blend off) — the sibling BUMP/DEFAULT state. `flags & 0x4c7 == 0` takes `4c1897` (opaque, `+1a4 = 0` from `4c0e73`). `4c172c` derives g_CullMode from `MPF_2SIDED`. The captured port state therefore means exactly `MPF_ALPHABLEND` set and `MPF_ALPHATEST` clear, and `g_AlphaValue=1.0` is established rather than assumed.

These are the only by-name writes of the blend/Z/test parameters in the whole `4c0150` body. Everything after writes through the cached handles and only ever *enables* blending: fog `4c2cdd..4c2d3c`, node `+13c` `4c2a8b..4c2b41`, node `+130 & 08000000` `4c2f79..4c2fd5`. The first two gate their SrcBlend/DestBlend writes on `+1a4 == 0` and blend currently off (`4c2abe`/`4c2ac6`, `4c2d01`/`4c2d0a`); the third writes the same 5/6. With `+1a4 == 1` the port's factors are unreachable by any per-draw override. `4c300e`/`4c3022` read the result back into `[ESP+78]`/`[ESP+7c]`, `4c379a` picks mask 7 for blend-on, and the `AlphaValue=0` write at `4c3801` is skipped because `+1a4 != 0`.

Admission by captured draw state is therefore **stable** for these draws: Z-write off, blend on, SRCALPHA/INVSRCALPHA, alpha-test off, mask 7, AlphaValue 1 all follow from one asset-constant flags word and cannot change with camera distance, fog or a node alpha override. Two residual variations keep the same admissible state: a node `+13c != 0` rescales `g_AlphaValue` (`4c2a87..4c2b41`), and the fog block can still set `g_EnableFog=1`/`g_FogClip` at distance (`4c2e04`, no `+1a4` gate), so a `b0 = 1` port draw would pass the same check. A different LOD or subset can name a different material id and then fails the state check (fail-closed). `4c0c36` reads `+1a4` from the previous submission, before this call writes it.

Unresolved: which of the two flag providers the port uses — the global table above or the per-subset material at `[[ESP+40]+0x28]` taken when mesh flag `[[ESP+6c]+0x50] & 0x10` is set (`4c027e`/`4c028a`, `4c1393`, `4c150f`); both are loaded asset data. The port material's actual MPF value was not read (no asset parse, no game run).

## Docking-port pair: minification analysis

Derived read-only bytecode study, 2026-09-14. No game run and no Wine command.
The programs were disassembled earlier through the existing D3DX route
(`tools/analysis/disassemble_shaders.cpp`, see `shader-sweep.md`); the cached
output is byte-identical input to run 39's copy — `ps_64bac8bb307eb896.bin`
SHA-256 `84eeded40acc811b109221fe5c7e1b5a898f3b9a34a1b4685937a24f6b1bc4a7` in both
`/tmp/x3-shader-sweep/programs/` and `/tmp/x3-bottleX3-run39/`. Raw disassembly
and the extracted draw block stay in `/tmp/x3-port-ps/`.

### Interpolants

VS `4944d81dfe531b37` (vs_3_0, 62 slots) writes, in PS register terms:

| PS reg | VS output | Content |
|---|---|---|
| `v0.xyz` | `o1.xyz` | `sum_{i<i0} sat(Ng·dir_i)*color_i*rcp_sat(atten_i·(1,d,d²)) + g_MatEmissiveColor` |
| `v0.w` | `o1.w` | `b0 ? sat(FogClip.x - FogClip.y*|cam-pos|)*g_AlphaValue : g_AlphaValue` |
| `v1.xy` | `o2.xy` | `g_TexMatrix * (u,v)` — the only UV, shared by s0..s3 |
| `v2.xyz` | `o3.xyz` | camera position (`g_mViewInverse` column 3) minus world position: eye vector, unnormalised |
| `v3.xyz` | `o4.xyz` | world normal via `g_mWorldIT` |
| `v4.xyz` | `o5.xyz` | world **tangent** via `g_mWorldIT` |
| `v5.xyz` | `o6.xyz` | world **binormal** via `g_mWorldIT` |

### Texture stage roles

PS `64bac8bb307eb896` (ps_3_0, 67 slots, 5 texture) declares only s0..s4. Its CTAB
names them and the arithmetic confirms each role. The capture binds seven stages;
**stages 5 and 6 are never sampled by this program**.

| stage | identity | CTAB name | format / size / levels | use in the bytecode |
|---|---|---|---|---|
| s0 | 1850 | `DiffuseTexSampler` | DXT5 1024² / 11 | `.rgb` albedo through the grading matrix; `.a` is the **only** alpha output |
| s1 | 1851 | `BumpTexSampler` | DXT5 1024² / 11 | AG normal: `x = 2*S.a-1`, `y = 2*S.g-1` |
| s2 | 1852 | `SpecularTexSampler` | DXT1 1024² / 11 | scalar red: gates both the specular sum and the cube reflection |
| s3 | 1706 | `LightMapTexSampler` | DXT5 32² / **1** | `.rgb` added last; `.a` selected only when `g_EnableGlow`≠0 |
| s4 | 1708 | `CubeMapTexSampler` | cube / **1** | environment lookup at `reflect(-V,N)` |
| s5 | 1854 | — | DXT5 32² / 6 | bound, not sampled |
| s6 | 1710 | — | A8R8G8B8 32² / 1 | bound, not sampled |

### Arithmetic

With `S` = s1 sample, `T`=`v4`, `B`=`v5`, `Ng`=`v3`, `V`=`nrm(v2)`, `m`=s2`.x`,
`A`=s0`.rgb` after the `dp4` grading, `f` = ±1 from the `vFace` `cmp` pair:

```
x = 2*S.a - 1 ;  y = 2*S.g - 1 ;  z = sqrt(abs(1 - x*x - y*y))   // rsq then rcp
N = f * normalize(y*T + x*B + z*Ng)
d_k = saturate(N·L_k)
R_k = 2*(N·L_k)*N - L_k                        // mad r2, r0, -r0.w, -c_k
h_k = saturate(R_k·V)
s_k = pow(h_k, g_MatSpecularPower) * saturate(3*d_k)
direct = g_MatDiffuseStrength*(d_0*C_0 + d_1*C_1) + m*g_MatSpecularStrength*(s_0*C_0 + s_1*C_1)
env    = cube(reflect(-V,N)) * m * A * g_MatReflectionStrength
oC0.rgb = (direct + saturate(v0.rgb)) * A + env + lightmap.rgb
oC0.a   = lerp(s0.a, lightmap.a, g_EnableGlow) * v0.w
```

The normal decode, the `sqrt(abs(q))` fold and the reflection construction are the
**same expressions** as the asteroid BUMP pair in
[asteroid-specular-minification.md](asteroid-specular-minification.md); this shader
adds the cube reflection, the `g_Mat*Strength/Power` weights, the additive lightmap,
the `vFace` two-sided flip and an identity brightness/contrast/saturation/hue
preshader matrix in `c0..c2`. There is **no Fresnel term** anywhere.

Normal-dependent: `d_0`, `d_1`, `s_0`, `s_1` and the cube lookup direction.
Sampled scalars, normal-independent: `m` (s2 red), `A` (s0 rgb), the lightmap rgb,
and the alpha.

### Captured constants, draw index 216 (frames 1812 / 2071 / 2316 of run 39)

All 34 non-zero PS float registers, all 32 render states, all 112 sampler rows and
all 13 texture/desc rows are **byte-identical across the three frames** (SHA-256 of
the row sets: `b6d6a065741a`, `9cddc712bf68`, `5cda9ec7d61e`, `2b36ce3fd993`). The
capture writes constants sparse-zero (`src/proxy/capture_state.cpp:57`), so an absent
register is exactly zero.

| register | parameter | value |
|---|---|---|
| `c0..c2` | grading matrix (preshader) | identity, zero fourth column — a no-op |
| `c3` | `g_EnableGlow` | **absent ⇒ 0** |
| `c4` | `LightDir_Dir0` | `(-0.304886, 0.455627, -0.836319)`, unit |
| `c5` | `LightDir_Color0` | `(0.664062, 0.781250, 0.585938)`, Rec.709 luma 0.742 |
| `c6` | `LightDir_Dir1` | `(0.945480, 0.230774, 0.229752)`, unit |
| `c7` | `LightDir_Color1` | `(0.128906, 0.257812, 0.214844)`, luma 0.227 |
| `c8` | `g_MatSpecularStrength` | **3** |
| `c9` | `g_MatSpecularPower` | **6** |
| `c10` | `g_MatReflectionStrength` | 1 |
| `c11` | `g_MatDiffuseStrength` | **0.5** |

`c12`/`c13` are `def`-ed inside the program (`3,0,0,0` and `1,-1,0,2`); the host values
logged at those registers belong to another effect and are shadowed. VS `b0 = false`,
`i0 = (0,0,1,0)` so `g_nNumLightPoint = 0`, `c40` (`g_MatEmissiveColor`) absent ⇒ 0,
`c39` (`g_AlphaValue`) = 1, `c41` (`g_FogClip`) = `(1,0)`, `c37/c38` identity.

Two consequences follow directly. First, `v0.xyz = 0`: the point-light loop runs zero
times and emissive is black, so **this material has no ambient or emissive floor in
`oC0.rgb`** — apart from the additive 32² lightmap, every RGB term is a product of the
sampled normal's response with the albedo. Second, `v0.w = 1`, so `oC0.a = s0.a`
exactly.

Relative weights at these constants: per light the specular peak is `3*m` against a
diffuse peak of `0.5`, i.e. 6× at `m = 1` and equal at `m = 1/6`; light 0 outweighs
light 1 by 3.3× in luma; `dot(c4,c6) = -0.375`, so the two lights are 112° apart and a
single flat normal can satisfy `N·L > 0` for at most one of them. Exponent 6 gives a
half-power half-angle of 27° — broad, the same caveat the asteroid note makes for its
exponent 3.

### Sign of the change under minification

The decode rebuilds `z` from `x,y`, so the sampled normal is **unit by construction**:
mip averaging shortens `(x,y)` toward `(0,0)`, `z` is restored to 1, and the shader
shades with a *flattened but still unit* normal. The Toksvig length signal is
destroyed before the shader can see it, exactly as recorded for the asteroid pair.
The shader therefore evaluates `g(mean N)` where correct filtering wants `mean g(N)`.

- `d_k = max(0, N·L_k)` is convex, so `mean d_k ≥ d_k(mean N)` (Jensen). The diffuse
  term can only **lose** energy as the mip level rises, never gain. The common
  heuristic that "diffuse N·L rises toward the geometric normal" holds for the
  *unclamped* dot only; the `saturate` reverses it for any footprint that straddles a
  terminator, and with the two lights 112° apart every footprint straddles one.
- `s_k = h_k^6 * saturate(3*d_k)`. Where the surface is meaningfully lit (`d_k ≥ 1/3`)
  the gate is a constant 1 and `h^6` is convex on `[0,1]`, so the same inequality holds;
  in the terminator band the gate is linear in `d_k` and the product still falls with
  flattening. This is the dominant term at the captured constants (weight `3*m*C_0`
  against `0.5*C_0`).
- The cube lookup is not convex in `N`; flattening merely converges it to the
  geometric mirror direction. Stage 4 has one level and `MIPFILTER = NONE`, so the
  cube itself never minifies; its distance dependence comes only through the direction
  and through `m`.
- `m` and `A` are ordinary scalars/colours: their mips converge to the local mean with
  no systematic sign.

So every normal-driven term in this shader is one that **falls** under minification,
the dominant one is specular, and there is no ambient floor to hold the result up. The
bytecode's expected sign of the luminance change with distance is negative, consistent
with the reported darkening.

### Alpha path

`lrp_pp r2.w, c3.x, r0.w, r1.w` selects `s0.a` because `c3 = 0`; `r1.w` at that point
holds `s0.a` from `texld_pp r1, v1, s0` (the earlier `d_0` in `r1.w` is overwritten by
that fetch). `mul_pp oC0.w, r2.w, v0.w` with `v0.w = 1` leaves `oC0.a = s0.a`. The
alpha is therefore a **sampled scalar, not a normal term** — but it is a scalar from a
DXT5 1024² texture with a full 11-level mip chain, so it minifies. Under the fixed
SRCALPHA/INVSRCALPHA source-over this multiplies the *entire* `oC0.rgb`, including the
non-minifiable additive lightmap. If the port's diffuse alpha has any sub-unit texels
inside the growing footprint (cut-out grille, decal border), the whole port — glow
included — fades toward the background as distance grows. This is a second darkening
path, independent of the normal, and at these constants it is the stronger lever
because it scales every term at once. Whether the mip-0 alpha over these pixels is
actually below 1 is **unknown**: the capture holds no texels.

### Mip state at these draws

Stages 0–3, 5, 6: `MAGFILTER = LINEAR(2)`, `MINFILTER = ANISOTROPIC(3)`,
`MIPFILTER = LINEAR(2)`, `MIPMAPLODBIAS = 0`, `MAXMIPLEVEL = 0`,
`MAXANISOTROPY = 16`, `SRGBTEXTURE = 0`. Stage 4 (cube): `MINFILTER = LINEAR(2)`,
`MIPFILTER = NONE(0)`. Trilinear + 16× anisotropic minification is therefore fully in
effect on the normal-map stage, with all eleven mips of the 1024² DXT5 reachable and
no bias or clamp limiting the level.

### What run 39 does *not* show

The three "approach" frames are not at different distances. The world matrix of node
`1ae7e1b0` (handle 52029) is identical in all three, and the camera-to-node distance
recovered from the logged view matrices is 85774 / 85231 / 85456 world units — a 0.6 %
spread, i.e. 0.009 mip levels. The other two port draws in the same frames sit at
140 k and 159 k units but are different nodes (`229a89a8`, `2af395e8`), and the port
draws at frames 9163/11940 belong to yet another node (`2162b570`, 403–415 k units).
**These captures contain no distance transition for a single port node**, so they
neither confirm nor refute minification; they only establish that state, pair,
textures and constants are fixed. A bounded HDR probe of a 24² window at the projected
node centre gave mean luminance 0.0020 / 0.0018 / 0.0034 across the three frames, but
the window moves with the camera and the geometry is six triangles, so this is not
evidence of a material change.

### Conclusion and the bounded next step

The bytecode **supports** unchanged-state minification as a mechanism and identifies
two channels rather than one:

1. Normal-map (s1) flattening collapsing a specular-dominated, ambient-free lighting
   model — the same decode and the same lost length signal as the asteroid pair, with
   a heavier specular weight (`3*m` vs the asteroid's `m`) and no vertex-light floor.
2. Diffuse-alpha (s0 `.a`) minification scaling the entire source-over composite,
   which the asteroid's opaque path does not have.

A corrective material shader cannot use classic Toksvig here: `z` is reconstructed and
the vector renormalised, so the averaged length is gone by the time the shader runs.
It would need either a precomputed variance / LEAN-style moment channel alongside the
normal mip chain (texture creation and upload interception, memory, binding, Reset and
sharing validation), or a runtime footprint average — explicit `dsx/dsy` gradients with
two or four child-footprint taps of s1, reconstructing and shading each — which fits
PS 3.0 but costs repeated two-light arithmetic per tap. Screen-space `dsx/dsy` of the
already-filtered normal cannot recover sub-texel variance and is a weaker fallback.
Neither addresses channel 2; alpha minification needs a coverage-aware composite, not a
specular filter.

The bounded next diagnostic is **capture-only and needs no shader replacement**: a
readback of the s1 and s0 texels actually addressed by these pixels along a *real*
approach — that is, a capture pair on the same node at clearly different distances
(target a ≥ 2× distance ratio, ≥ 1 mip level) recording, per draw, the decoded
`sqrt(x²+y²)` of the normal sample and the `s0.a` value at the chosen LOD. Failing a
texel readback, a debug view that outputs `s0.a` alone and `saturate(3*d_0)` alone for
this pair would separate the two channels. Run 39 cannot do either: it has no distance
transition on a single port node.

### Texture evidence

Offline archive study, 2026-09-14. No game run, no Wine command; the bottle was read
only. Extraction, decode scripts and the raw DDS bytes stay in `/tmp/x3-port-tex/`
(untracked); only derived numbers are recorded here.

**Which files the capture identities are.** The capture logs no asset name, so the
binding was resolved from the archives. Root `01.cat`..`13.cat` and `addon/*.cat`
decode with the existing `read_catalogue`; a DAT slice is bytewise `XOR 0x33` and, for
`.pck`/`.pbd`/`.pbb`, a gzip stream underneath. `types/dummies.pck` lists the
`SDTYPE_ANIMATED` dock-port bodies, `types/cutdata.pck` maps their cut ids to scene
paths (`19098 → 9098 → stations\docks\M6DockCarrier_quicklaunch_scene`, `19099 → 9099
→ …M6DockCarrier_scene`), and `objects/stations/Shipyards/Argon_SY_scene.pbd`
references exactly those dummy bodies. Body geometry is binary `BOB1`; its `MAT6`
chunk stores `name\0`, `u16` type, value, with type `0x0000` long, `0x0001` bool,
`0x0002` 16.16 fixed, `0x0005` four 16.16 fixed, `0x0008` string. A scan of all 3,451
object entries (26,982 materials) found **1,128** source-over materials
(`g_AlphaBlendEnable=1`, `SrcBlend=5`, `DestBlend=6`, `g_ZWriteEnable=0`,
`g_ALPHATESTENABLE=0`). Exactly one family matches the draw's captured constants
(`3 / 6 / 1 / 0.5`), its effect (`standard_lighting.fx`), its `g_CullMode=1` (NONE —
hence the `standard_lighting2s` archive alias of this pair) and its 1024² DXT5 +
DXT1 texture shapes:

| capture | file (root `01.cat`) | header | SHA-256 of the decompressed DDS |
|---|---|---|---|
| s0 / 1850 / DXT5 1024² / 11 | `dds/metal_argon_lattice_windowedgrid_diff.pck` | DXT5 1024², 11 mips, 1,398,256 B | `7d860ec4a8f2db0b650682876f4ee49a272c0e7e5878c967240904b88afb3b9a` |
| s1 / 1851 / DXT5 1024² / 11 | `dds/metal_argon_lattice_windowedgrid_bump.pck` | DXT5 1024², 11 mips, 1,398,256 B | `14f53a84b37b24b633c97565b2aa718c06b98e42920fb4a1510852ac3b263262` |
| s2 / 1852 / DXT1 1024² / 11 | `dds/metal_argon_lattice_windowedgrid_spec.pck` | DXT1 1024², 11 mips, 699,192 B | `035f54f4722bcd03d905118bc3b923de920c58de10d534ef6ff2e16d9f51ef48` |

The material is `standard_lighting.fx`, diffuse/bump/spec as above, `t_LightMapTexture
= NULL` (hence the shared 32²/1-level dummy at s3) and `t_AlphaTexture =
…_alpha8.tga`, which has no DDS and resolves to the 32²/6-level dummy bound at s5 and
never sampled. It occurs **105** times across Argon station bodies, matching run 11's
observation that every station model carries this refused pair.

The only competing candidate was `metal_argon_specialglass_diff` (also DXT5 1024²/11,
also source-over `standard_lighting.fx`, 98 occurrences). Two capture facts exclude
it. All eleven port draws of run 39 — four different station models — bind the *same*
triple 1850/1851/1852, and so does XT pair `37c34a7478544c14/f1b0e820c7b488c3`
(draw 165, frame 1812); every XT material with the specialglass diffuse binds the dummy
`NONE_NORMAL` bump, never a 1024² normal map, while the XT windowedgrid material binds
all three. That XT draw's PS constants `c9..c12 = 3 / 6 / 1 / 0.5` also match the
windowedgrid XT material (reflection 1.0) and not the specialglass XT material
(reflection 1.5999908). This is an asset-side identification by state, constants,
format and sharing; the capture carries no texel hash, so it is not a byte-level proof
of the runtime upload.

**Diffuse alpha (s0.a), whole texture.** DXT5 alpha decoded from the stored mip chain.
Mip 0: mean **0.7749**, min **110/255 = 0.431**, max 1.0, **no zero texels**, 50.8 % of
texels exactly 1.0, 49.2 % below 0.98, 11.2 % below 0.5. The 16-bin mip-0 histogram is
bimodal: 532,785 texels in the top bin, 503,225 in bins 7–9 (≈0.44–0.62), 12,555 spread over bins 10–14 and only 11 in bin 6 (≈0.38–0.44).
So the mask is a **soft half-transparent mask, not a cutout with holes**. Mean alpha per
stored level is flat — 0.7749, 0.7708, 0.7676, 0.7663, 0.7663, 0.7658, 0.7691, 0.7648,
0.7691, 0.7608, 0.7686 for levels 0…10 — as box filtering requires. **The alpha channel
therefore applies a near-constant ≈0.775 attenuation at every distance and cannot itself
produce a distance-dependent fade of the mean.** Its only distance-dependent part is the
`α`–colour covariance the shader discards: it samples filtered `α` and filtered RGB
separately and multiplies, giving `mean(α)·mean(C)` where correct filtering wants
`mean(α·C)`. Box-filtering mip 0 gives the shader/correct ratio per level (also the
ratio against mip 0, which is the visible change with distance):

| level | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 10 |
|---|---|---|---|---|---|---|---|---|---|
| `mean(ᾱ·C̄) / mean(αC)` | 0.9993 | 0.9974 | 0.9935 | 0.9895 | 0.9824 | 0.9785 | 0.9689 | 0.9528 | 0.9513 |

**Normal map (s1) under the shader's AG decode.** `x = 2·A−1`, `y = 2·G−1`,
`z = sqrt(|1−x²−y²|)`. Mean tilt `sqrt(x²+y²)` and mean `z` of the **stored** mips:

| level | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 8 | 10 |
|---|---|---|---|---|---|---|---|---|---|
| mean tilt | 0.1517 | 0.1688 | 0.1717 | 0.1531 | 0.1279 | 0.0871 | 0.0460 | 0.0190 | 0.0124 |
| mean `z` | 0.9452 | 0.9401 | 0.9516 | 0.9682 | 0.9834 | 0.9933 | 0.9981 | 0.9997 | 0.9999 |

Max tilt at mip 0 is 1.081 — above 1, which is why the `sqrt(abs(q))` fold matters. Per
texel the decoded normal is unit by construction, so the length signal must be recovered
from mip 0: box-filtering the decoded unit normals gives mean `|N̄|` and the Toksvig
factor `|N̄| / (|N̄| + p(1−|N̄|))` at the captured `p = g_MatSpecularPower = 6`, i.e. the
fraction of the specular peak that correct filtering would keep relative to what this
shader produces from the renormalised average:

| level | 1 | 2 | 3 | 4 | 5 | 6 | 8 | 10 |
|---|---|---|---|---|---|---|---|---|
| mean `\|N̄\|` | 0.9931 | 0.9839 | 0.9717 | 0.9581 | 0.9507 | 0.9469 | 0.9455 | 0.9452 |
| Toksvig, `p=6` | 0.9687 | 0.9283 | 0.8795 | 0.8239 | 0.7865 | 0.7629 | 0.7438 | 0.7420 |

The specular map's mean red is level-independent as expected (0.5957, 0.5956, 0.5939,
0.5901, 0.5847 for levels 0…4), so `m` contributes no systematic sign; `m̄ = 0.596`
weights the specular term at `3·m̄ = 1.79` against the diffuse `0.5`.

**Which channel dominates.** A 170×70 px rect for a 1024² texture is level
`log2(1024/170) ≈ 2.59`; trilinear interpolation of the tables gives:

| distance | level | alpha factor vs mip 0 | normal/specular factor |
|---|---|---|---|
| run 39 | 2.59 | 0.995 | **0.908** |
| 2× | 3.59 | 0.991 | **0.847** |
| 4× | 4.59 | 0.985 | **0.802** |

**The normal-map channel dominates by roughly 19× at the run-39 distance** (9.2 %
specular loss against 0.5 % alpha loss) and by about 13× at four times that distance.
The earlier conjecture that alpha minification is "the stronger lever" is **wrong for
this asset**: its alpha has no holes, its mean is level-invariant, and the only
distance-dependent alpha term is the small discarded covariance. The alpha does impose a
constant ≈0.775 attenuation of the whole composite at all distances, which explains why
the port looks washed out but not why it darkens with range. A corrective material
shader for this pair therefore needs the normal-variance path (LEAN/Toksvig moment
channel, or footprint taps); a coverage-aware composite would buy at most ~1.5 % here.

Unknown: the UV bounds of the six-triangle port part. The capture logs no vertex or UV
data, so every number above is over the whole 1024² texture; a part that uses only the
opaque region would see a smaller alpha attenuation and a part confined to the grille
window would see more. The 16× anisotropic minification recorded at these draws also
means the per-pixel LOD is at or below the isotropic estimate along the unminified axis,
so the factors above are the pessimistic end for the given screen rect.

### Detached measurement

Fixture run, 2026-09-14, bottle X3 (`verification/probe/port_distance_fixture.cpp`,
`run_port_distance.py`, record `verification/results/bottle-X3/port-distance1.json`;
no game launch). The real pair is created from the original bytecode
(`vs_4944d81dfe531b37.bin` / `ps_64bac8bb307eb896.bin`, PS SHA-256
`84eeded4…`) and the three original DDS images are bound at s0/s1/s2 with the
captured sampler rows (MAG LINEAR, MIN ANISOTROPIC 16, MIP LINEAR, bias 0,
MAXMIPLEVEL 0) and the captured constant block of draw index 216 (c4..c11 =
light dirs/colours and 3 / 6 / 1 / 0.5; c3 absent ⇒ 0; VS `b0` false, `i0`
`(0,0,1,0)`, `c39` = 1, `c37/c38` identity, `c40` absent ⇒ 0). Both untracked
directories are arguments; the runner refuses to start unless the three texture
hashes are the ones tabulated above. s3 and s4 are black 1×1 stand-ins — the
captured lightmap is the shared 32²/1-level dummy and stage 4 has one level with
MIPFILTER NONE, so neither minifies — which removes the additive and reflective
terms rather than inventing values for them.

A quad carrying the whole 1024² UV domain is drawn at three camera distances in
the ratio 1 : 2 : 4 into a 1280×768 A16B16G16R16F target, giving centre widths
170 / 85 / 43 px, i.e. isotropic levels 2.59 / 3.59 / 4.59. Nothing but the
camera distance changes inside a configuration. Each case is drawn twice: once
with the captured source-over state over black (`ALPHABLENDENABLE`
SRCALPHA/INVSRCALPHA, ZWrite off, alpha test off, mask 7) and once with blending
off and mask 15, which is the only way to read `oC0.a` — the captured mask 7
never writes it. Statistics are Rec.709 luma over the covered pixels eroded by
2 px. The three geometries are the design note's: head-on with light 0 at 30°
from the normal, a mirror configuration (view 45°, light 0 at the exact mirror,
`R·V = 1.000000`) and an off-peak one (view 60°, light 0 20° the other side,
`R·V = 0.174`). The quad is tilted about the world X axis and the captured light
pair is carried rigidly by the minimal rotation onto the requested direction, so
both captured colours and their 112° separation are preserved
(`dot(L0,L1) = −0.3753` in all three).

Shader luminance relative to the run-39 distance (raw pass, before the blend):

| configuration | 2× real | 4× real | 4× flat-normal control | 4× normal channel alone |
|---|---|---|---|---|
| head-on, light 30° | 1.039 | **1.077** | 1.033 | 1.042 |
| view 45°, light at mirror | 1.078 | **1.153** | 1.099 | 1.049 |
| view 60°, light 20° off-peak | 0.950 | **0.940** | 0.976 | 0.964 |

**The real pair brightens with distance wherever the geometry is lit and darkens
only off-peak.** The sign argument of "Sign of the change under minification"
above is therefore **wrong for this pair**: it applies Jensen to `N·L` and `h^6`
but omits that the decode renormalises the shortened `(x,y)` back to unit length,
which raises `N·L`, the lobe and the gate together. Convexity bounds
`mean g(N)` against `g(mean N)` — the filtering *error* — not `g(N_level)`
against `g(N_0)`, which is what distance actually changes. The magnitude of the
error (the Toksvig factors 0.908 / 0.847 / 0.802 in "Texture evidence") stands;
only its attribution to a darkening was unfounded. The detached numbers confirm
the simulation in [specular-antialiasing.md](../architecture/specular-antialiasing.md)
(+6.9 % head-on, +11.5 % mirror, −1.1 % off-peak at 4.59) in sign everywhere and
within about 4 points in magnitude.

Three further results:

- **Isolating the normal channel.** Replacing s1 with a flat normal (1×1,
  A = G = 128) leaves the other channels minifying and still brightens the lit
  configurations (1.033 / 1.099 at 4×), because the shader multiplies separately
  filtered `m`, `A` and `α` and so discards their covariance. Dividing real by
  flat gives the normal map's own contribution: **+4.2 % / +4.9 % / −3.6 %** at
  4×. The normal channel is therefore not the dominant distance term at these
  geometries; at the mirror the larger part of the +15 % is the discarded
  `m`–`A` covariance.
- **Peak versus mean.** Mean luminance and peak luminance move in opposite
  directions where a highlight exists: max luma at 4× is 0.773 (head-on) and
  0.353 (off-peak, real) against 0.764 / 0.712 for the flat control, while the
  means rise. Filtering spreads the highlight — the flattened normal lights a
  larger area more uniformly. Any correction must be judged on both.
- **Alpha.** `oC0.a` is normal-independent as the bytecode says: the real and
  flat runs give bit-identical alpha statistics. Its mean over the port's UV
  domain is 0.755–0.758 at the run-39 distance and falls to **0.980 / 0.972 /
  0.976 of that at 4×** (0.990 / 0.982 / 0.979 at 2×). So the alpha channel does
  change with distance, by about 2 % at 4×, about twice what the stored-mip means
  (0.7749 → 0.7663, −1.1 % at level 4) predict; trilinear interpolation between
  DXT5-requantised levels and the domain edge account for the rest. It is a real
  but second-order darkening lever, an order of magnitude below the colour
  change, and it cannot reverse the sign: the composited luminance ratios at 4×
  are 1.111 / 1.201 / 0.901, still brightening for both lit geometries.

Bounds of the measurement. A flat quad carrying the whole texture is not the
port's six triangles with their real UV bounds; the tilted configurations have a
perspective LOD spread across the quad (the bounding box at 1× is 193–199 px
wide against the 170 px centre width), slightly wider at 1× than at 4×; the
lightmap and the cube reflection are excluded by construction. What the fixture
does establish is the sign and the order of magnitude of the pair's own response
to minification at the captured constants, which is what the contradiction was
about. The originally reported darkening of receding ports is not explained by
this material's colour response, and needs another owner — the candidates left
untouched here are the cube term, the vertex/fog path and the LOD or subset
change at range.

### LOD and subset at range

Read-only study, 2026-09-14: Ghidra 12.1.3 on the existing `/tmp/x3-ghidra-research/X3Render`
project (installed X3AP.exe SHA-256 `fdbf3418…`), plus bounded queries of the run-39 and
run-36 capture logs and of the game archives. No game launch, no Wine command; the bottle
was read only. Raw decompiler/instruction output stays in `/tmp/x3-port-lod/` and the
archive scripts in `/tmp/x3-port-tex/` (both untracked).

**A LOD switch exists and it removes the port material entirely.** Node `2afb76f0`
(model key `5427`) is captured on both sides of one in the run-36 log
(`/tmp/x3-bottleX3-run36/session-20260914-073123-212.log`):

| frame | LOD | draws for that node | pairs |
|---|---|---|---|
| 1476, 1699 | 3 | **1** | `53a0a641107ed76c/8759c7838bbc86c2`, 2592 tri, gate 0, **routed** (run 36 logs no per-draw state fields; the equivalent LOD-3 draw of model `5436` in run 39 is Z-write on, blend off, ONE/ZERO, mask 15) |
| 1917 | 2 | **14** | 7× `4944d81d…/5e0a10fe…` (940 tri), 5× `4944d81d…/ca6bfa4a…` (10 015 tri), 1× `53a0a641…/8759c783…` (6 tri), 1× the source-over port pair `4944d81d…/64bac8bb…`, **refused, gate 4** |

So the coarsest LOD submits the whole station body as a single opaque DEFAULT subset; the
`metal_argon_lattice_windowedgrid` source-over pair — with its `MPF_ALPHABLEND` material and
descriptor `+0x1a4 == 1` — **does not exist at LOD 3**. The same collapse is visible on model
`5436`: 13 draws at LOD 2 (run 36, frame 13681, including exactly one `64bac8bb…` port draw)
against 1 draw of 3002 triangles / 6805 vertices at LOD 3 (run 39, frames 9163 and 11940).
For this mod the consequence is a **source-domain flip at the LOD boundary**: the port surface
is part of a routed, converted-lighting merged material at LOD 3 and becomes a refused,
natively lit source-over draw at LOD 2.

**Selection (`0x0047cfe0`, writes node `+0x14c`).** Per node, per frame, after the
behind-eye, distance and frustum rejections documented in
[render-node-bounds.md](render-node-bounds.md):

| site | contract |
|---|---|
| `0047d1b5..0047d1bb` | `D = FUN_00469ab0(node+0xf0)` = signed max of the three camera-relative coordinates `+0xf0/+0xf4/+0xf8` (`00469ab0` is a three-way max, no absolute value). |
| `0047d1c2..0047d1eb` | if `D < 0x0fffffff`, `D = D * camera[+0x298] / 0x4000`. This is the per-view scale for the metric; the `*(camera+0x1c)+0x2c` context scale is used by the frustum test, not here. |
| `0047d1ef..0047d21a` | small-object measure `ESI = FUN_00469a30(node+0xa0, *(0x608518)+0x5c, D)` = `r·W/D` (`00469a30` is `(a*b)/c`, 0 when `c == 0`), or `0x7000000` when `D < W` or `node+0x12c & 0x400`. |
| `0047d221..0047d250` | **the LOD metric** `s = FUN_00469a30(node+0xa0, 0x280, D)` = `r·640/D`, clamped to ≥1, or `0x7000000` when `D < 0x280`. The reference is the constant 640, not the viewport. |
| `0047d258..0047d29b` | `ESI` versus node `+0x1dc` and versus 20 sets node `+0x130 \| 0x100000 / 0x180000`; `camera+0x270 & 0x1000000` forces `0x180000` and drops nodes with `ESI < 20`. |
| `0047d2a2..0047d2cf` | `ESI` versus `max(node+0x1d8, parent+0x1d8)` clears the renderable bit `+0x12c & 2`. |
| `0047d321..0047d35e` | `EBP = model[+0x10] - 1` (LOD count − 1). If `*(0x606f34)+0xfc & 0x800000` and `s < 0x20`, `s = s*(*(0x606f34)+0x748 + 10)/110`. |
| `0047d429..0047d46e` | **the loop**: for `i = EBP` down to 1, `T_i = (int)((float)LODrec_i[+0x34] * float[*(0x606f34)+0x760])`; the first `i` with `s < T_i` is stored in `+0x14c`, else 0. Thresholds therefore rise toward LOD 0. |
| `0047d472..0047d4d1` | `camera+0x270 & 0x1000000` → `+1`; else `*(0x606f34)+0x768 >= 3` → `−1`; `> 3` → forced 0; then clamped to `[0, EBP]`. |
| `0047d4d7..0047d51e` | LOD `== EBP` together with `+0x12c & 0x8000` clears the renderable bit; LOD ≥ 3 sets `+0x130 \| 0x100000`. |
| `0047d528..0047d546` | recursion over `node+0xc`, propagating the third argument, so the whole subtree uses the same branch. |

**There is no hysteresis.** `+0x14c` is cleared at entry and recomputed from `s` every frame
with strict `<` comparisons; the only stabilisers are the asset thresholds themselves and the
quality adjustments above. The alternative branch taken for a parentless node with
`+0x12c & 0x80000000` (`0047d36d..0047d3fb`) does not use `s` at all: it compares
`D − FUN_00488170(node)` against the fixed 7 000 000 / 15 500 000 / 21 000 000 / 28 500 000
(`0x6acfc0`, `0xec82e0`, `0x1406f40`, `0x1b2e020`) and then applies two asset guards
(`LODrec[+0x34] < 2`, and coarse `LODrec[0] < finer[0]/3`) that step one level back.

**Where the boundary sits for this body.** `node+0xa0` is not in the capture, so `T_i` cannot
be converted to absolute view units; the boundary is given in `D` (engine units, before the
`camera+0x298/0x4000` scale), recovered from the logged world and view matrices:

| model | `D` at LOD 3 | `D` at LOD 2 | Euclidean distance |
|---|---|---|---|
| `5427`, node `2afb76f0` (run 36) | 128 854, 129 090 | 118 833 | 156 665 / 156 393 / **156 108** |
| `5427`, node `2af395e8` (run 39) | — | 110 967 / 114 400 / 114 145 | 156 529 / 156 383 / 156 112 |

The LOD 2/3 boundary for model `5427` is thus `D ∈ (118 833, 129 090]`. The Euclidean range is
**unchanged to 0.18 %** across the transition: the metric is a max-norm of camera-space
coordinates, so the switch is driven as much by camera orientation as by range. (The
world-axis reading of `+0xf0/+0xf4/+0xf8` gives 118 471 / 118 145 / 117 780 and would bracket
the same transition monotonically; `0x0047cfe0`'s behind-eye test `node+0xf8 + node+0xa0 < 0`
argues for the camera-space reading, and this capture cannot separate the two because the node
is nearly ahead of the camera.)

**On-screen size at the switch.** `fade_region` gives the port part's projected rect. For
model `5427` at `D = 110 967` (run 39, frame 1812, draw 267) it is 61×36 px; the boundary is
1.07–1.16× farther, so the port pair stops being submitted at roughly a **52–57 px port
width** on that body. The size is not universal: model `5471` (node `2162b570`) is still at
LOD 2 at `D = 443 450` with a 17×13 px port, and model `543f` (node `1ae7e1b0`) is at LOD 2
at `D = 64 687` with a 170×73 px port. All eleven port-pair draws of run 39, on five station
models and over `D = 64.7 k … 445 k`, are at **LOD 2 with identical `flags12c = 0x01001002`,
`flags130 = 0x40` and identical state** — so within the captured range the port darkening is
not a LOD or subset change; the change is a step at the LOD 2/3 boundary, which run 39 never
crosses on a port node.

**No fog or `b0` path at the far LOD.** At frames 9163 and 11940 the merged LOD-3 station draw
(`53a0a641…/8759c783…`) and the LOD-2 port draws (`64bac8bb…`) all have VS `b0 = 0` and
`D3DRS_FOGENABLE = 0`. The far draw is opaque (Z-write on, blend off, ONE/ZERO, mask 15,
routed) and the port draw keeps the source-over state. The `+0x1a4` words do differ across the
switch, but as material identity — LOD 3's material has no `MPF_*` blend bit and so takes
`4c1897` with `+0x1a4 = 0`, while the port material takes `4c1827` with `+0x1a4 = 1` — not as
any per-draw override.

**Asset side.** BOB1 bodies store the LOD count as a big-endian `u16` immediately after the
`BODY` tag, followed by the `u32` body size, then one `POIN`/`PART` pair per level. The five
Argon shipyard bodies each declare **4 levels**:

| body | materials | points per LOD 0/1/2/3 |
|---|---|---|
| `argon_symain.pbb` | 52 | 44 397 / 21 481 / 7 132 / 2 772 |
| `argon_syarm.pbb` | 52 | 33 862 / 20 425 / 5 771 / 1 070 |
| `argon_sylow.pbb` | 53 | 26 515 / 11 863 / 3 938 / 1 059 |
| `argon_syhigh.pbb` | 52 | 12 543 / 6 921 / 2 035 / 1 826 |
| `argon_sytop.pbb` | 52 | 12 766 / 5 542 / 1 641 / 422 |

(`m6dockcarrier_quicklaunch_body.pbb` has 4 levels and 6 materials; `m6dockcarrier_scene_dummy.pbb`
has 1.) A LOD-3 point count of 2 772 is consistent with the 6 805-vertex, 3 002-triangle merged
draw of model `5436`, but the model-key-to-file mapping is not proved.

Unknown. The per-LOD material lists were **not** read from the assets: the `PART` chunk's group
header (`u32 flags 0x10000001`, `u16 group count`, then `u32 material`, `u32 face count` per
group — validated against `m6dockcarrier_scene_dummy.pbd`, whose 336 faces split 282/54 over
materials 0/1) is understood, but the face record stride is not, so groups beyond the first
cannot be walked. The per-LOD threshold values `LODrec[+0x34]`, their loader provenance and the
quality float at `*(0x606f34)+0x760` were not resolved, so the thresholds are stated as `D`
brackets rather than in absolute view units, and `node+0xa0` is not captured. No hook is
proposed by this study.
