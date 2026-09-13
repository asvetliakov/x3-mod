# Selected alpha-tested material route: architecture assessment

2026-09-14. Source/shader architecture review and public D3D9 capability probe. Root has run the capability probe in X3; actual cutout MRT behavior and any production route remain unqualified. No production gate change.

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

Recommended next action: decide whether to qualify this exact two-pair cutout state alongside the existing fade prototype. This is a materially simpler composition case and affects 32 captured draws, but importance for visible brightness or shimmer depends on the newly added target/ancestor capture and a same-object transition. Do not call it the station bug's cause or a solved visual issue.

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
