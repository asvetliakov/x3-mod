# Station material distance changes: shared producer and Run 28 limits

Native/shader research and capture-only diagnostic implementation, 2026-09-14. The user's reproduction is the red-marked docking-port structure on Federal Argon Shipyard; similar changes were seen elsewhere. Neither that description nor a projected node center identifies its shader or mesh. No game, Wine, DLL build or installation was performed. The isolated diagnostic source is not installed.

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
