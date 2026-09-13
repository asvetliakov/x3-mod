# Selected alpha-tested material route: architecture assessment

2026-09-14. Source/shader architecture review and public D3D9 capability probe. Root has run the capability probe and the selected detached cutout GPU twins in X3; both pass. The production cutout route and live TAA remain unqualified. No production gate change.

## Finding and bounded admission candidate

Yes: alpha-test exclusion is another source-lighting-domain boundary. Run 28 frame 4052 has 20 exact Argon BUMP `4944d81dfe531b37/5e0a10fe752b6140` and 12 Argon DEFAULT `53a0a641107ed76c/63f96eba9eea7880` cutout draws. All 32 use Z-write=1, blend=0, ALPHATEST=1, GREATEREQUAL=7, ALPHAREF=1, RT0 mask=7, CULLNONE=1 and stencil disabled; common shader fog b0=0. They bypass combined material conversion at gate 4, while eligible opaque submeshes use converted source RGB. Thus an object/LOD/material switch between these regimes can switch source domain. The capture does not establish such a transition or identify the photographed port pixels.

A first qualification should own just those two exact pairs and that cutout state, including RGB-only RT0 writes. Keep depth test/write, stencil behavior, primitive order, alpha function/reference and native alpha. This is opaque-or-rejected coverage, not source-over transparency: no separate linear-layer pool or fade compositing equation is required. A shader-only rewrite of alpha testing, forced RGBA mask or blanket gate relaxation is not proposed.

## Existing shader proof can be reused, but coverage remains unqualified

The original programs and existing `linear_material.cpp` contracts preserve:

`a = interpolated(AlphaValue * (EnableFog ? saturate(FogClip.x - FogClip.y * vertexDistance) : 1)) * (EnableGlow * LightMap.a + (1-EnableGlow) * Diffuse.a)`.

DEFAULT lightmap is s2, BUMP s3; diffuse is s0. Pixel COLOR0, LRP and final `MUL_pp oC0.w` keep their original partial precision. `pixel_sites` proves the exact texture/alpha operations and no intervening alpha-lane write; RGB transfer/gain helpers write xyz only. The VS retains native COLOR0.w and its alpha/fog operations. Motion/depth epilogues append separate MRT outputs. Existing detached material tests already compare original/combined alpha and motion/depth bitwise with alpha testing disabled. They do not qualify the driver's alpha comparison, write-mask behavior or rejected-fragment MRT/depth/stencil semantics.

RT0 alpha must remain unwritten under mask 7 even though the shader's oC0.a remains the comparison input. The retained framebuffer alpha is glow/compositor bookkeeping and is not a cutout coverage mask. Do not reconstruct comparison alpha from RGB or an FP16 readback.

## Capability and state proof

D3D9's documented MRT alpha test compares oC0; failure terminates the pixel for all MRTs. MRT post-pixel support is conditional on `MRTPOSTPIXELSHADERBLENDING` and format support. The route must establish `D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING` support for the actual simultaneous FP16 RT0, RGBA32F RT1 and R32F RT2 formats, not merely FP16 blending or render-target creation. Existing motion initialization checks only render-target use for RT1/RT2, so its present success is insufficient. Require the selected `AlphaCmpCaps` bit, independent write masks (RT0=RGB, RT1/RT2=RGBA), existing independent bit depths/MRT count and non-MSAA contracts. Fixed-function fog and dithering must be off because their auxiliary MRT behavior is undefined or restricted; shader fog is separate. [Microsoft MRT contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets), [primitive caps](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpmisccaps), [alpha test capabilities](https://learn.microsoft.com/en-us/windows/win32/direct3d9/alpha-testing-state).

Query immutable capabilities once at device/route setup and cache the result. If any required format/capability is absent, the first route remains unavailable on that device; do not infer support because a driver happens to produce an image. RGBA32F/R32F post-pixel support is a concrete feasibility gate. A different temporal format or explicit shader discard would be a separate design with fresh ABI/alpha-equivalence qualification.

Cache relevant alpha function/reference, fog/dither and masks through existing state-change tracking; do not add per-draw capability queries or repeated getters. Extend shadow invalidation/state-block/Reset handling and counters proportionally if implemented. Preserve failed-setter-mutation recovery, feature-off/native twins and lazy/per-draw state restoration.

## Small complete qualification

Reuse the existing detached linear-material fixture plus the selected live motion fixture; no new framework or full material-corpus rerun is needed.

1. For each exact pair, run original native RT0, original+motion MRT and combined-material+motion MRT twins. With alpha test temporarily off in a diagnostic reference draw, compare actual shader alpha before storage can conceal precision differences; then restore actual GREATEREQUAL/ref1 and RGB mask7. Require identical binary pass/fail coverage and native RT0 alpha bits. Linear RGB on accepted samples must match the existing material reference. The coverage oracle is the actual original draw under identical state, not a guessed floating `a >= 1/255` rule.
2. Supply neighboring passing/failing samples and values around the native comparator boundary, including zero, exact threshold witnesses and one, partial AlphaValue, deliberately different diffuse/lightmap alpha, EnableGlow 0/1/intermediate, both faces and nonzero normal/gloss inputs. Include actual sampler filtering/mips and jitter positions. Preserve `_pp`; CPU algebraic equality does not prove driver threshold parity. Test shader fog on/off if it is not separately excluded by the admitted state/contract.
3. Poison RT0 alpha, RT1 motion and RT2 depth independently. Passing samples write correct same-draw motion/depth but preserve RT0 alpha; alpha-rejected and hardware-depth-rejected samples leave every color target and hardware depth/stencil as the original. Use front/back occluders and later diagnostic depth probes, plus interleaved opaque/cutout draws and overlapping primitives, to detect writes hidden by clear colors. Qualify both existing depth modes or initially refuse an untested mode.
4. Move the cutout over independently moving opaque/background geometry. Passing samples own the foreground depth/correspondence; holes retain the real underlying writer or the original sentinel. Verify appearance/disappearance, jittered edges and minification, cutout/opaque and LOD transitions, Reset, feature toggles and failures. Reuse existing depth-disocclusion/history-reset behavior; no blanket mask is necessary merely because alpha testing is on. Do not fabricate foreground motion/depth in holes. Changing texture alpha/thresholds or nearly coplanar coverage may evade depth rejection: if those cases fail, add a specifically proved coverage-change invalidation/reactive policy before claiming general temporal support. A reactive fallback alone would not complete cutout TAA.
5. Exercise missing caps/formats, wrong compare/ref/mask/blend/fog/dither/MSAA, unknown shadow state, setter mutation/failure and recovery, state-block invalidation, lazy/per-draw transitions and Reset. All refusal paths preserve native alpha-test behavior. Measure paired completion cost of only the selected route; there is no new required full-screen pass, though extra MRT bandwidth/fragment cost and any driver alpha-test scheduling cost remain to be measured.

Root approved qualification of this exact two-pair cutout state alongside the existing fade prototype. This is a materially simpler composition case and affects 32 captured draws, but importance for visible brightness or shimmer depends on the newly added target/ancestor capture and a same-object transition. Do not call it the station bug's cause or a solved visual issue.

## Feasibility probe prepared

The isolated `qualification/alpha-test-materials` branch starts at `028d24e`.
`capability_probe.exe --alpha-test-caps` queries the default HAL adapter through
public `IDirect3D9` APIs. It loads only D3D9, creates no window/device/shaders,
and performs no draws, Present, benchmark or game work. The existing no-argument
probe retains its broader behavior; use the explicit mode for this decision.

It reports the adapter identity/display format, raw capability masks and shader
limits, then exactly two queries for each of FP16 (113), RGBA32F (116), and R32F
(114): render-target use and render-target plus POSTPIXELSHADER_BLENDING use.
`complete=1 eligible=0` is a successful measurement of an unsupported proposed
route, not a GPU failure or permission to bypass the missing capability.
`complete=0` exits 1 and leaves feasibility unknown. Only `complete=1 eligible=1`
permits proceeding to the coverage/motion/depth qualification described above.
Even that result is advertised capability evidence, not a successful actual MRT
alpha-test draw or native-Windows qualification.

Build-only command: `sh verification/probe/build.sh --capability-only`.
This stops after the capability EXE and uses the project's SSE2/four-byte-stack
flags. MinGW GCC 16.2.0 produced the retained 259,631-byte EXE at
`/tmp/x3-alpha-test-materials/verification/probe/build/capability_probe.exe`,
SHA-256 `405102275099d9cf25776f821f214bee65ae5fda14d411c61813d086594bdd13`.
Two focused `test_alpha_test_caps_probe` methods pass, including 15 synthetic
adapter cases and the exact six query arguments. No Wine has been run by this
agent. Independent Sol/high source review is approved with no findings; the reviewer reproduced the two focused tests in 1.444 seconds and verified the retained EXE size/hash.

Root-owned consume-only invocation (no runner rebuild):

```sh
X3M_FIXTURE_BOTTLE=X3 WINEDLLOVERRIDES='d3d9=b' \
python3 /Users/asvetl/x3-mod/verification/probe/wine_lock.py \
  --holder alpha-test-caps --timeout 60 \
  '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine' \
  --bottle X3 --no-update \
  /tmp/x3-alpha-test-materials/verification/probe/build/capability_probe.exe \
  --alpha-test-caps > /tmp/x3-alpha-test-caps.txt 2> /tmp/x3-alpha-test-caps-wine.log
```

Root should retain the small stdout result with the EXE hash and ordinary X3
bottle provenance (`bottle.describe('X3')`: WineArch and FEX/WINEMSYNC configuration,
plus any intentional invocation overrides). No new manifest or production build
is needed for this read-only capability decision. No shared MotionOutput source
has been changed; coordinate any eventual route work with the fade owner first.
The initial two-pair state is an iteration, not completion of all cutouts;
remaining exact alpha-preserving pairs need their own bounded qualification.

## X3 capability result

Root's retained capability EXE completed with exit 0: `complete=1 eligible=1`,
4 simultaneous render targets, `PrimitiveMiscCaps=0x002ecff2`,
`AlphaCmpCaps=0x000000ff`, VS/PS 3.0 and 256 vertex constants. All six RT and
RT+POST queries returned `S_OK` for FP16 (113), RGBA32F (116), and R32F (114).
This clears the advertised capability gate for the selected fixture; it does not
prove alpha-test coverage or authorize a production gate relaxation.

The run used CrossOver Preview, bottle X3. Its directly read configuration is
`WineArch=arm64`, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`; stderr corroborates
msync startup. The command above requests builtin D3D9 and sets the fixture
bottle explicitly. The retained EXE hash is the one recorded above. Small raw
stdout is `/tmp/x3-alpha-test-caps.txt`, SHA-256
`70e3b36620907dd771d1ef9f52d944674446d8d490ca4bfbede3721c403dd36b`;
stderr is `/tmp/x3-alpha-test-caps-wine.log`, SHA-256
`06bb0bf4cb250155ea8c208eb08f13662e526c8f474265eac9a41e565eafaf31`.
Native Windows remains source/API-compatible and untested. The next bounded
step is actual original/combined two-pair cutout twins in the existing material
fixture; live MotionOutput/TAA acceptance remains a subsequent requirement.

## Selected detached cutout fixture

`run_linear_material.py --alpha-test-cutout --exe <retained fixture>` consumes
one explicitly built EXE and the existing local original-program directory.
It selects only pair 0 (Argon DEFAULT) and pair 21 (Argon BUMP), and bypasses the
ordinary corpus/timing mode. `--glass-only` and this selector are mutually
exclusive. No production shader, MotionOutput gate, capability cache or shared
fade route is changed.

There are 48 cases: both exact pairs, both current-depth modes and faces, each
with six input variants. Those vary AlphaValue 0/0.625/1, Glow 0/1/0.375,
asymmetric diffuse/lightmap alpha, float point samples around 1/255, actual UNORM
point/linear sampling and mip 0/1, UV jitter, and shader fog off/on. RGB stays
constant across each alpha texture so the existing independent float64 material
reference can check the combined RGB without a second lighting implementation.
The native alpha path is not rewritten. DEFAULT's exact final alpha multiply is
at PS DWORD 1287 (END 1291); BUMP's is at DWORD 1349 (END 1353). Both use
`mul_pp oC0.w,r2.w,v0.w`, with the native preceding `lrp_pp r2.w,c3.x,r0.w,r1.w`.
Both vertex layouts use b0, c39.x AlphaValue and c41.xy FogClip.
Their native UV producer explicitly builds `(u,v,1)`, then reads c37/c38 with
DP3 (DEFAULT DWORDs 475/349; BUMP 502/358). The fixture's .z translations therefore
use that explicit 1: at D3D9 integer pixel centers the tested UV is
`((x+0.5+jitter)/16,(y+0.5)/16)`. Current c24..c27 and appended previous c252..c255
are four rows dotted with an explicit homogeneous position. The foreground
previous-X shift is -0.125; the actual opaque writer uses -0.25. This input math
was independently checked against both original programs before the GPU run.

For every case, alpha-test-off RGBA32F draws of untouched original, original
plus motion, and combined plus motion must preserve all 256 native alpha values
bitwise. The first ordered row is emitted as float bits before FP16 storage or
mask 7 can hide the comparison input. A separate FP16 unmasked draw of each
variant supplies that variant's exact accepted RGB storage. The actual original
GREATEREQUAL/ref1 draw supplies coverage; no CPU `alpha >= 1/255` rule replaces it.
Point threshold rows must retain zero/high-alpha anchors and the expected native
sample positions, while the near-threshold pass bits are measured evidence.

Each case has six scenes and three variants (864 twins total): clear depth
pass/reject, diagnostic stencil pass/depth-fail twins, and actual opaque draws
behind/in front of the cutout. The opaque draw has a distinct previous transform,
so holes must retain the underlying draw's actual motion and current depth.
RT0 alpha, motion and depth start with nontrivial poison. Every passing pixel
must match its own unmasked RGB, the independently calculated previous UV/depth,
and the original's coverage. Every rejected pixel must retain its prior values.
Later equal-depth and equal-stencil draws expose the hardware D24S8 state through
ordinary color readback: alpha rejection leaves depth/stencil unchanged; a
stencil-enabled accepted alpha increments on depth pass or decrements on depth
failure. Native and combined variants are checked against the same binary masks.
This tests stencil semantics diagnostically; the proposed initial live state
still has stencil disabled.

The report parser requires ordered unique alpha/RGB/coverage rows, every scene
and variant, all 20 exact shader creations, independent RGB bounds, nonvacuous
neighboring coverage (except the intentional zero-AlphaValue cases), and exact
check accounting. It rejects failed, incomplete, duplicate, malformed and
out-of-scope reports. A host fixture extracts the actual per-pixel reducer and
injects RT0-alpha, RGB, motion, depth and reference corruption; R32F comparisons
intentionally use only the stored lane.

Performance scope: this is detached correctness work. Allocations/readbacks and
shader creation are outside gameplay and no normal per-draw work has been added.
The selected mode deliberately skips the old corpus benchmark, which would test
alpha-test-off state. Its runtime does not estimate game FPS or a live route's
cost. A paired live route completion measurement is required when that route is
implemented. The root-owned locked X3 fixture run passed;
independent Sol/high source review is approved with no findings. Live MotionOutput state admission, failures,
Reset/recovery and moving TAA edges remain unimplemented and unqualified here.

Prepared fixture build: `sh verification/probe/build_linear_material.sh` passed
with MinGW GCC 16.2.0 and the existing SSE2/four-byte-stack flags. The retained
11,280,788-byte EXE is
`/tmp/x3-alpha-test-materials/verification/probe/build/linear_material_fixture.exe`,
SHA-256 `5756a0a8c4a4aca9231b4b58e854649f738b9b508d73a9532dbdecbbff209fca`.
The affected report batch passed 49 methods in 59.620 seconds (four selected-mode
methods plus 45 existing report methods). The additional extracted actual-pixel
reducer method passed in 1.152 seconds, covering 48 injected pass/failure
combinations. All four selected local originals match the existing profile
provenance. No Wine or production build was run by this agent.

Root-owned consume-only qualification command, after source review:

```sh
X3M_FIXTURE_BOTTLE=X3 python3 /Users/asvetl/x3-mod/verification/probe/wine_lock.py \
  --holder alpha-test-cutout --timeout 60 \
  python3 /tmp/x3-alpha-test-materials/verification/probe/run_linear_material.py \
  --alpha-test-cutout \
  --exe /tmp/x3-alpha-test-materials/verification/probe/build/linear_material_fixture.exe \
  --raw-dir /tmp/x3-linear-alpha-test-gpu
```

The runner hashes supplied binaries/originals/scoped source and records X3
bottle provenance. Successful compact output is
`verification/results/bottle-X3/linear-alpha-test-gpu.json` in that isolated
worktree; raw stdout, Wine stderr and binary cases stay in the explicit `/tmp`
directory. A failed run preserves the prior accepted summary and saves a local
`failed-result.json`. This mode neither rebuilds nor installs a DLL.

The same reviewer independently reproduced the five selected-mode host methods
in 4.839 seconds, reconciled the exact check formula and shader/input ABI,
verified the unchanged EXE hash above, and approved the detached qualification
with no findings. The old detached fade runner now hashes the included cutout
header as a compiled dependency; no fade behavior changed and no fade rerun was
needed for that provenance entry. The same reviewer approved the root-owned GPU evidence with no findings, as recorded below.

## X3 detached cutout result

Root ran the retained fixture above under the Wine lease, consuming the existing
EXE. Exit 0 and the strict parser both report PASS: **48 cases, 288 scenes,
864 twins, 20 shader creations, 2,609,760 checks, 12,288 alpha samples and
5,376 accepted native pixels**. The largest combined-RGB error was
`2.5301338717302897e-05` of its existing reference tolerance. Alpha, native versus
motion RGB, MRT ownership and hardware depth/stencil checks remain exact.
No runtime completion benchmark was run or inferred from fixture duration.

All eight point threshold rows (both pairs, faces and depth modes) retained
native alpha bits `00000000,3b808080,3b808081,3b808082,3b000000,3c000000,3f000000,3f800000`
and native coverage `00110111`. In particular the binary32 sample immediately
below the normalized ref1 value is rejected; its nearest binary32 representation
and next higher sample pass. This is an observed native GREATEREQUAL boundary,
with zero and high-alpha anchors, not a CPU replacement for the comparison.

The compact tracked result is
[`linear-alpha-test-gpu.json`](../../verification/results/bottle-X3/linear-alpha-test-gpu.json),
SHA-256 `6f59921e509974c0bf3d25de2d7b765f29bea8934bbbd4b2b7d462de1637f4f5`.
Raw `/tmp/x3-linear-alpha-test-gpu/report.txt` is 60,420 bytes, SHA-256
`d0826c7bcb7145518830832fcb9f4fb80166ccec8bb2f8b8d5bb1f29006b6055`;
Wine stderr remains beside it. The result records CrossOver Preview bottle X3,
WineArch arm64, configured `FEX_X87REDUCEDPRECISION=1` and `WINEMSYNC=1`; stderr
corroborates msync. The retained EXE hash above, all 17 recorded source hashes and
all four local original-program hashes match. The same Sol/high reviewer replayed
the strict parser without Wine, independently reconciled raw counts, coverage
and check totals, and approved this evidence with no findings.

This establishes the selected detached shader/MRT contract in X3. It does not
establish a port-specific cause, all cutout coverage, production state admission,
recovery/Reset, temporal edge quality, live-route overhead or native Windows
runtime behavior. Those remain the next bounded route qualification.
