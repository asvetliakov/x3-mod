# Running X3AP's rendering on D3D11 or D3D12 through a scoped D3D9 translator in the proxy: feasibility

Question: is it feasible, and how much work, to translate the game's D3D9 calls
inside our proxy into a modern API (D3D11 on DXMT for the CrossOver target,
native D3D11 on Windows; or D3D12) so that the engine's draws and our post
chain run on one API; and (section 7) whether the translation should sit at
the D3D9 API boundary or at an engine-internal seam, with the GPU and CPU goals
kept apart. Vulkan is out of scope on this target by the user's decision
(CrossOver's Vulkan support); DXVK's d3d9 module appears only as prior art for
sizing. Companion: [d3d11-post-chain-feasibility.md](d3d11-post-chain-feasibility.md)
(the mixed D3D9 + D3D11 chain, closed 2026-09-24: cross-API sharing carries no
pixels on this bottle). Every figure is marked measured (M) or inferred (I);
nothing here was run under Wine and the game was not launched.

## Decision

**No-go on writing a translator, on D3D11 or D3D12; measure-first on one
cheap number that could reopen the D3D11 case.** The census (section 1) shows
the game's D3D9 use is narrow enough that a scoped 9-on-11 layer is
technically feasible (section 2), but the two things it would buy are small or
unproven: the CPU it can remove is at most the native draw and state-call
cost, about 1.8 ms of the 21.9 ms stand frame (M, section 7), and the GPU it
can remove is the post chain's own pass overhead, which at 1080p is not the
bottleneck and at 5120x1440 is worth one to two milliseconds by the dilation
measurement (M, feasibility note). Against that stands an estimated 45-70
agent-days (I, section 4), a correctness surface of every engine draw, a
second bottle-coupled translation layer under the game (DXMT under our layer),
and a Windows path the user cannot test and that Windows already covers with
its own `Direct3DCreate9On12`. Between the two targets, D3D11 wins outright
(section 6): it is the only immediate-mode target, DXMT proved the feature set
for a 32-bit process, and D3D12 has no non-Vulkan path for a 32-bit process on
this bottle (D3DMetal ships x86_64 only). The one measurement: a
draw-submission microbenchmark of the game's real per-draw call pattern on
wined3d-D3D9 versus DXMT-D3D11 (section 5).

**Ratified 2026-09-24 (main session):** no-go on a translator of our own on D3D11 or D3D12; the post chain
stays on D3D9 and CPU work goes through engine patches. The section 5 microbenchmark is deferred, not queued:
its own decision rule caps the CPU case under 1 ms per frame at any outcome, so it cannot change the decision
on its own; it is picked up only if the 5120x1440 flight (Run 77 C) shows the post chain GPU-bound in a way
the D3D9 steps S1-S5 cannot recover. HDR output over DXGI remains its own later design.

## 1. API surface census

Sources: the proxy's session log of run286 (`/tmp/x3-bottleX3-run286/session-20260924-004522-216.log`,
36 MB, queried row by row with
`verification/results/bottle-X3/d3d9-api-census/census.py`, output
`census-run286.txt` beside it), the frame-timing rows of run281/run286, the
reverse-engineering notes named per row, and the archive shader sweep
(`verification/results/shader-sweep-{aliases,inventory}.json`). The capture
records eight frames (1637-1644, 1,032 draws) and, per draw, a fixed subset of
32 render states, 7 sampler states on 16 stages, the three transforms, the bound
textures and buffers; states outside that subset (address modes, texture stage
states, WRAP0-15, FVF) are not in the log and are marked unknown below.

| Area | What the game uses | Evidence |
| --- | --- | --- |
| Factory | `Direct3DCreate9(0x20)`; adapter records walked with stride 0x730; caps read once (`D3DDEVCAPS_HWTRANSFORMANDLIGHT`, `PUREDEVICE`) | ghidra-render-map.md `0x004d8470`; constant-uploads.md `0x004d9517` (RE) |
| Device | one `CreateDevice` at `0x004d9642`, `BehaviorFlags` 0x52 = HW VP, FPU_PRESERVE, PUREDEVICE on this bottle; windowed, A8R8G8B8 back buffer, 1 buffer, DISCARD, auto depth D24X8, flags 3 (LOCKABLE_BACKBUFFER, DISCARD_DEPTHSTENCIL), interval 1, MSAA 0 | `telemetry_presentation` row (M); constant-uploads.md (RE) |
| Draws per frame | stand: 460 `draws_p50` (run286 frame 300 window), 391-416 at run250's stand/busy view; flight: 68-220 | `frame_timing` rows (M) |
| Draw entry points | `DrawIndexedPrimitive` for all scene geometry, issued from `d3dx9_37` (`ID3DXMesh::DrawSubset`, I: the EXE has zero direct call sites, the caller is D3DX or an unidentified wrapper); `DrawPrimitive` at three EXE sites (particles/stardust, dynamic locked strips, bloom/glow quads); `DrawPrimitiveUP` at four sites (2D overlay x2, text x2) with a fixed-function texture-stage preamble; no `DrawIndexedPrimitiveUP`, no patches, no instancing (stream frequency 1 on every captured draw) | camera-state-and-frame-routine.md draw-site table (RE); `draw`/`stream` rows: 976 indexed TRIANGLELIST, 32 TRIANGLESTRIP, 24 TRIANGLELIST non-indexed (M) |
| State calls | 27,019 per stand frame = 59 per draw (run286); 63,311 / 1,006 = 63 per draw busy (run91); all through the game's `ID3DXEffectStateManager` (two classes, 12 thunks) which forwards `SetTransform`, `SetRenderState`, `SetTexture`, `SetTextureStageState`, `SetSamplerState`, `SetVertexShader`, `SetVertexShaderConstantF` (and the PS setters); no game code outside those classes sets constants | `frame_timing state_calls_p50` (M); frame-loop-phases.md slot table, constant-uploads.md (RE) |
| Render states seen on scene draws | ZENABLE 1/0 (848/184), ZWRITEENABLE, ZFUNC LESSEQUAL, ALPHATESTENABLE 1 on 184 of 1,032 with ALPHAFUNC GREATEREQUAL ref 1, ALPHABLENDENABLE 288, SRCBLEND ONE/SRCALPHA, DESTBLEND ZERO/INVSRCALPHA/others, BLENDOP ADD, separate alpha blend on some, CULLMODE CCW/NONE, COLORWRITEENABLE 15/7, BLENDFACTOR, SRGBWRITEENABLE 0 always, FOGENABLE 0 always, STENCILENABLE 0 always (two-sided stencil states set but disabled) | `state` rows, 48 distinct (state,value) pairs in 33,024 rows (M) |
| Sampler states | MAG POINT/LINEAR, MIN POINT/LINEAR/ANISOTROPIC, MIP NONE/LINEAR/POINT, LOD bias 0, MAXMIPLEVEL 0/3/4, MAXANISOTROPY 1/16/4, SRGBTEXTURE 0; 16 distinct (state,value) pairs; address modes not captured (the overlay path sets WRAP by RE) | `sampler` rows (M); sampler-states-and-mips.md (RE) |
| Texture stage states | only the fixed-function overlay/text sites (COLOROP/ALPHAOP SELECTARG1 of DIFFUSE or TEXTURE, stage 1 DISABLE); whether compiled effects set any is unknown (`state_other_p50` 973/frame in run91 is uncounted traffic) | sampler-states-and-mips.md (RE); engine-state-filter.md (M) |
| Transforms | the state manager forwards `SetTransform`; the capture reads WORLD/VIEW/PROJECTION per draw; no shader reads them (all VS use constants); whether any fixed-function vertex path other than pretransformed overlay quads exists is unknown | `transform` rows (M); frame-loop-phases.md (RE) |
| Shaders (archive) | 751 programs (256 VS, 495 PS) in six profile directories; the `3_0` directories the game selects on a ps_3_0 device hold **211 distinct programs: 158 SM3, 23 SM2.0, 30 SM1.1** (root 209 + addon 91, union 211); of these 51 have control flow, 24 relative addressing, 94 preshaders (CPU-side in D3DX), 14 opcodes our interpreter does not model | shader-sweep.md, aliases/inventory JSON query (M) |
| Shaders (runtime) | 57 distinct binaries across the capture collection (21 VS: 8 x 1.1, 3 x 2.0, 10 x 3.0; 36 PS: 6 x 1.1, 2 x 2.0, 28 x 3.0), all archive matches; run286's eight frames used 16 VS + 21 PS; 60 `shader` rows in the session | shader-sweep.md (M); `draw`/`shader` rows (M) |
| Vertex formats | FLOAT16_4 POSITION/TEXCOORD/NORMAL/TANGENT/BINORMAL (meshes, stride 24-40); FLOAT4 POSITION + FLOAT2 TEXCOORD (bloom quads); FLOAT3 POSITION + D3DCOLOR + FLOAT2 (strips/particles); declarations via `CreateVertexDeclaration` (compositor decl at `0x00608a70`); UP draws' FVF unknown (stride 20 at the compositor UP site = XYZRHW + DIFFUSE by arithmetic, I) | `vertex_element` rows, 10 distinct (M); compositor-and-glow.md, bloom-compositor-skip.md (RE) |
| Buffers | VB: MANAGED + WRITEONLY (976/1,032), DEFAULT + WRITEONLY (32), DEFAULT + DYNAMIC + WRITEONLY (24); IB: MANAGED + WRITEONLY, INDEX16 only; lock flags seen: NOSYSLOCK, READONLY, DISCARD, 0 | `vertex_buffer`/`index_buffer`/`buffer_content` rows (M); mesh-buffer-rewrite.md: built once per LOD record (RE) |
| Textures | 2D DXT1 / DXT5 / A8R8G8B8 / X8R8G8B8 up to 11 levels, cube textures on stage 2; created by `D3DXCreateTextureFromFileInMemoryEx` (`CreateTexture` + `LockRect` at load, no AUTOGENMIPMAP); no volume textures observed; 2,276 `resource` rows: 977 VB, 972 IB, 297 textures, 23 surfaces, 7 cube textures | `texture_desc`/`texture`/`resource` rows (M); sampler-states-and-mips.md section 2 (RE) |
| Render targets and copies | back buffer + auto depth; the compositor's `StretchRect(main, scene texture, LINEAR)` once per frame (`0x004c4c8c`), `ColorFill` x3, `Clear` flags 3 then 2, `SetRenderTarget` for the glow chain, `D3DXCreateRenderToEnvMap` (cube RT, import present, live use unverified); `GetRenderTargetData` at three sites (screenshot / video paths via a SYSTEMMEM offscreen surface + `LockRect`) | `capture_event` rows (M); compositor-and-glow.md, lens-flare-visibility.md (RE) |
| Queries, state blocks | none by the engine: `CreateQuery` has 0 call sites; effects run with `D3DXFX_DONOTSAVESTATE` so D3DX records no state block | lens-flare-visibility.md, frame-loop-phases.md (RE) |
| Swap chain, Reset, alt-tab | one implicit swap chain, `Present` once per frame; **no Reset has been recorded in any session log** (Reset call paths exist in the EXE; resolution change untested); the double cursor after alt-tab is a DirectInput / winemac symptom, not a D3D one | cursor-observations.md (RE, M) |
| Video | `amstream` owns a DirectDraw surface; the game locks it (`IDirectDrawSurface::Lock`), converts on the CPU and `LockRect(NULL, 0)`s a game-owned `IDirect3DSurface9` (`0x004d0c40`); only that D3D9 surface lock and the draw of its texture touch the D3D9 device; ddraw stays Wine's (or native Windows') | media-cue-playback.md section 8 (RE) |
| Proxy's own use | FP16 / FP32 / R32F / G32R32F render targets, MRT, D24X8 / D16 / D24S8 depth, the RESZ depth copy, SYSTEMMEM offscreen surfaces + `GetRenderTargetData` readbacks, `StretchRect`, `ColorFill`, EVENT and OCCLUSION queries, `D3DSBT_ALL` state blocks per pass, `SetStreamSourceFreq`, scissor; 52 compiled programs from 36 HLSL sources (ps_3_0 with VPOS, compiled offline by native D3DX) | `grep` over `src/renderer`, `src/proxy` (M) |

The narrow parts: no fixed-function fog, stencil, sRGB, MSAA, queries, state
blocks, volume textures, 32-bit indices, instancing, patches or Reset in
evidence. The wide parts: 211 live shader programs across SM1.1 / 2.0 / 3.0,
the D3DX loaders' lock-and-fill expectations on managed resources, four
fixed-function `DrawPrimitiveUP` sites, and 59-63 state calls per draw.

## 2. Translation architecture (what a scoped 9-on-11 layer would be)

**Where it sits.** `src/ownership` already fronts the game with canonical COM
wrappers for 15 interfaces / 297 methods and hands the renderer a borrowed
native device. A translator is a second "native" behind those wrappers, chosen
at `wrap_factory` (the capability boundary): every method in the census gets an
implementation over `ID3D11Device` / `ID3D11DeviceContext`; every method not in
the census fails closed with `D3DERR_NOTAVAILABLE` and a log row, so an
unexpected call is a finding rather than a crash. The proxy's `Direct3DCreate9`
export chooses the backend from a launcher switch; the D3D9 path stays the
default.

**Resources.** MANAGED VB / IB: `D3D11_USAGE_DEFAULT` buffer plus a CPU shadow;
`Lock` returns the shadow, `Unlock` uploads the dirty range with
`UpdateSubresource` (the game builds them once per LOD record, so this is
load-time work). DYNAMIC VB (strips, bullets): `D3D11_USAGE_DYNAMIC` with
`D3DLOCK_DISCARD` / `NOOVERWRITE` mapped to `MAP_WRITE_DISCARD` /
`NO_OVERWRITE`. `DrawPrimitiveUP`: a per-frame ring buffer. Textures: D3DX
locks every level at load, so a managed texture is a staging fill followed by
one upload; DXT1 / DXT5 to BC1 / BC3, A8R8G8B8 / X8R8G8B8 to
`B8G8R8A8_UNORM` / `B8G8R8X8_UNORM` (DXMT accepted `BGRA_SUPPORT`, M), cube
textures native, D24X8 to `D24_UNORM_S8_UINT` on a TYPELESS resource so the
depth is a native SRV (replacing the RESZ trick), FP16 / FP32 / R32F / G32R32F
native. SYSTEMMEM surfaces: STAGING textures; `GetRenderTargetData`:
`CopyResource` to staging + `Map`; `StretchRect`: `CopyResource` when
same-size and same-format, otherwise a blit draw (the compositor's LINEAR
downscale needs the draw); `ColorFill` and rect `Clear`: `ClearView`
(a feature-level 11_1 API; DXMT reports 11_1, M; `ClearView` itself unprobed, I) or a scissored draw. D3DCOLOR vertex
elements: `R8G8B8A8_UNORM` input plus a swizzle in the translated VS, as DXVK
does. FLOAT16_4: `R16G16B16A16_FLOAT` native.

**Shaders.** Three ways to get SM1.1 / 2.0 / 3.0 programs onto SM4/5:

1. Hand-written HLSL for the 211 live programs: highest fidelity per program,
   but 211 programs at one to three agent-hours each with a capture-diff
   fixture is 30-60 agent-days by itself (I), and every mod or override that
   ships a new `.fb` is unsupported.
2. A bytecode-to-HLSL generator, compiled at load by `d3dcompiler_47` (the
   bottle's is vkd3d-shader and compiled all eight probe shaders including
   `cs_5_0`, M; on Windows the system compiler) and cached by the full program
   hash: `tools/analysis/shader_semantics.py` already tokenises every SM1-3
   opcode the archive uses, so the generator starts from an existing parser.
   The work is the semantics, not the parsing: ps_1_x modifiers and coissue
   (63 programs), the legacy texture ops (`texbem`, `texm3x3*`, 66 programs,
   mostly in lower profile directories), `_pp`, relative addressing,
   predication, the D3D9 half-pixel convention (a `pos.xy += float2(-1/w, 1/h)
   * pos.w` epilogue in every VS), D3DCOLOR swizzle, `VPOS` to `SV_Position`.
3. A bytecode-to-DXBC converter: this is what Microsoft's D3D9On12 `ShaderConv`
   does (open source, MIT, from knowledge of the public repository, not verified here); adopting it is the least new code but brings a
   large foreign codebase into the proxy.

Option 2 is the one to estimate against: 8-12k lines (I, sized from DXVK's
DXSO, the equivalent SM1-3 front end). Alpha test (184 of 1,032 draws) becomes
a `clip()` driven by a small constant buffer (func, ref) with a uniform
branch. Fixed-function: one pass-through VS for pretransformed vertices
(viewport transform in the shader) and one generated PS per observed
texture-stage combination (three EXE sites; the combinations are SELECTARG1 of
DIFFUSE or TEXTURE, stage 1 disabled). Fog, sRGB and stencil need nothing
(never enabled). Constants: three constant buffers per stage (float 4 KB, int,
bool), dirty-range upload per draw (`MapNoOverwriteOnDynamicConstantBuffer`
= 1 on DXMT, M, so a ring sub-allocation works); the observed register ranges
are c0-c46 (VS) and c0-c35 (PS).

**State.** Render, sampler and blend / depth / rasterizer states are cached
D3D11 state objects keyed by the value tuple; the observed value space is 48
render-state pairs and 16 sampler pairs, so a few dozen objects. State calls
set dirty bits (5-10 ns, I); the draw resolves them. The hot path per draw is:
dirty-bit resolve, 16 SRV / sampler slots (4.5 `SetTexture` changes per draw,
M), two constant-buffer uploads, one `DrawIndexed`. D3D9 features with no D3D11
state: WRAP0-15 (texture coordinate wrapping; the state shadow scans them, use
by the game unknown), SHADEMODE flat, point sprites, TRIANGLEFAN (not in eight
captured frames; the UP overlay sites are unmeasured and a fan there needs
index conversion), line primitives (unmeasured).

**Swap chain and present.** A DXGI flip-model swap chain on the game's HWND
(FLIP_DISCARD, 2-3 buffers, `B8G8R8A8_UNORM`, or `R16G16B16A16_FLOAT` with
scRGB for HDR: DXMT accepted the format and the colour spaces, M; the display
path is unverified). `Present` maps to `IDXGISwapChain::Present(1, 0)`; `Reset`
to `ResizeBuffers` plus a release / recreate of our own targets; `TestCooperativeLevel`
always `S_OK` (D3D9Ex semantics; the game's lost-device paths have never run
in evidence). `MakeWindowAssociation(NO_ALT_ENTER | NO_WINDOW_CHANGES)` keeps
DXGI from adding its own alt-enter and focus behaviour.

**Engine hooks.** Every `engine_patch` site (more than eighty named EXE addresses across
chase camera, frame / game / light / pass / submit / residual phases, cull,
collide, media cue, loading probes, music keep, pause key, resource reader,
lod scale, sun occlusion, voice DMO) reads engine memory, never a D3D9 object's
layout (`src/ownership/README.md`: no guessed wrapper layout is read;
ghidra-render-map.md: no hook uses the inferred renderer layouts). They
survive unchanged. What is D3D9-behaviour-dependent and would be re-based:
the ownership invariants (managed-pool and lock semantics, Reset admission,
surface leases), the finite-buffer evidence and resource reader (read the
game's managed buffers at lock time; simpler when we own the shadow), the
RESZ depth snapshot (replaced by a depth SRV), the motion-output constant
observers (API-level, survive), the sun-shadow replay (re-issues the game's
draws through `SetStreamSource` / `SetIndices` / `SetVertexDeclaration`;
survives if the translator implements them, which it must), and our own
`D3DSBT_ALL` state blocks (either implemented in the translator or replaced by
explicit save / restore, which is easier once we own the state cache).

**Our post chain on D3D11.** The 36 HLSL sources become `ps_5_0` / `cs_5_0`
(cbuffers, `Texture2D.Sample`, `SV_Position`); the 512-slot limit disappears
(the resolve is at 505 today); the three mask passes become one compute
dispatch (190 vs 414 us at 1080p, 657 vs 1,138 at 5120x1440, M); the HDR
meter and fog sky reductions become group-shared reductions; timestamp
queries (1 GHz on DXMT, M) replace the event-query spin brackets whose end
spins sum to about 9.1 ms per frame in the serialised `--gpu-sync-timing`
mode (M, run274); readbacks (hdr_readback 0.40 ms, M) become a two-frame
staging ring; HDR present goes straight from the FP16 tonemap to the scRGB
swap chain, dropping the 8-bit write-back.

## 3. Risks

- **A translator on a translator.** DXMT is CrossOver's bundled D3D11-on-Metal
  (product string "DXMT", i386 PE front end plus `winemetal.so`; feature level
  11_1, M). Its behaviour moves with CrossOver releases, and the project has
  already lost one bundled backend to exactly this class of change (the
  bottle's DXVK d3d9 rendered black on 2026-09-17 because MoltenVK refused its
  dual texture binding, archive handoff 2026-09-17). Our layer would carry the game's whole draw
  stream over DXMT with no fallback but the D3D9 path, and every CrossOver
  update re-qualifies it.
- **Correctness surface.** Every engine draw in every sector, both technique
  paths (`DEFAULT`, `INSTANCE_BULLETS`), the compositor and glow chain, the
  env-map render, video textures, the GUI's SM1.1 programs, the fixed-function
  overlay and text sites, ps_1_x modifier semantics, the half-pixel
  convention. The existing capture format (HDR / depth / motion readbacks per
  frame) gives a diff tool, but only for frames the user flies.
- **Media path.** Structurally unaffected (the DirectDraw surface stays in
  Wine's ddraw and the game copies on the CPU into a D3D9 surface lock), but
  the destination surface must be lockable every frame under the translator
  (a staging texture and an upload per video frame), and the media-cue freeze
  (a block inside amstream / ddraw) is untouched.
- **Native Windows.** A home-made translator would be the first component
  unverified on both targets at once. On Windows the OS already ships the
  equivalent: `Direct3DCreate9On12` (Windows 10 2004+, system d3d9.dll,
  forwarded by `src/proxy/loader.cpp:275` when present) runs D3D9 on D3D12
  with documented resource unwrapping (`ID3DDevice9On12::UnwrapUnderlyingResource`).
  That is Microsoft's translator with interop; it does not exist in Wine's
  d3d9. A translator of ours has no Windows justification that 9On12 does not
  already cover.
- **Double cursor and alt-tab.** Presentation moves to DXGI on the same window;
  the cursor symptom is DirectInput / winemac, so it must be rerun rather than
  assumed unchanged, and DXGI's window association must be disabled.
- **Fixtures.** Every D3D9 fixture (bloom, fog, TAA, shadows, ownership,
  state-hook benchmark, interop) tests the D3D9 chain. A translated chain needs
  a new family: a conformance fixture that renders the same scene through
  wined3d-D3D9 and the translator and diffs readbacks; a per-program
  translation test that compiles all 211 live programs and compares outputs on
  synthetic inputs; the post-chain fixtures re-hosted on D3D11. The current
  fixtures' readback outputs remain valid golden data.
- **Kept.** All reverse engineering (engine structures, materials, the 751
  program inventory), every EXE-level patch and observer, the capture format
  and analysis tools, the launcher and install machinery, the HLSL sources of
  our passes (ported, not rewritten), the ownership layer's COM discipline.

## 4. Effort by phase, the kill milestone, the performance case

Agent-days, inferred from the size of comparable work in this repository (the
shader sweep, the ownership layer, the fog and TAA passes); not measured.

| Phase | Content | Agent-days |
| --- | --- | --- |
| 0 | Measure-first: the draw-submission microbenchmark, wined3d-D3D9 versus DXMT-D3D11 (section 5) | 2-3 |
| 1 | Census completion: a one-session full-state capture (every `SetRenderState` / `SetSamplerState` / `SetTextureStageState` / `SetTransform` / `SetFVF` value, the UP draws' FVFs and primitive types, `RenderToEnvMap` use, lock flags), a user run that changes resolution in the options menu to exercise Reset | 2-3 |
| 2 | Minimal translator that boots to the menu and the loading screen: device, swap chain, buffers, textures, locks, declarations, the bytecode-to-HLSL generator with the shader cache, state cache, fixed-function overlay and text, `Clear` / `ColorFill` / `StretchRect` | 10-15 |
| 3 | Draw parity in one sector: every live program, alpha test, the compositor chain, particles, strips, bullets, env map, ps_1_x semantics, WRAP states, fans and lines if found; parity by capture diff against a wined3d run of the same route | 15-25 |
| 4 | Our passes ported: 36 HLSL sources to SM5 with cbuffers, masks and reductions as compute, timestamp queries, staging readbacks, FP16 scRGB present; ownership / lease / Reset re-based; fixtures re-hosted | 10-15 |
| 5 | Qualification: loading time, stalls, double cursor, alt-tab, capture parity on the user's routes, 1080p and 5120x1440 timing; Windows cross-compile only | 5-10 plus user runs |
| | Total | **45-70** |

**The milestone that proves or kills it cheaply is phase 0, not a translator
that renders the loading screen.** A loading-screen translator (phase 2) is
already 10-15 days and proves only that the layer exists; the numbers that
decide whether any of it is worth having are available for 2-3 days:

- *Draw submission.* The stand is CPU-bound at 21.9 ms with 460 draws (M,
  run286 window at frame 300; run250: 21.6-21.9 ms, 391 draws). Of that frame
  the native draw is `draw_native_p50` 1,479 us (3.2 us per draw) and the
  27,019 state calls at 10-15 ns each are about 0.3-0.4 ms (M per-call costs
  from `state-hook-benchmark-elision.json`; product inferred). That is the
  whole CPU budget an API translation can touch: about 1.8 ms, 8 % of the
  frame. The fixture question is whether DXMT's `DrawIndexed` plus state
  resolve, for the game's real per-draw pattern (59 state calls, 4.5 texture
  binds, two shader binds, two constant uploads, one indexed draw), is cheaper
  than wined3d's; if it is not at least 2x cheaper the CPU case is dead, and
  even 2x is under 1 ms.
- *GPU.* At 1080p the proxy passes sum to 11.7 ms serialised against 5.15 ms
  for the engine's own draws (M, run274, `--gpu-sync-timing`, which drains the
  GPU per pass; the pipelined frame is 22 ms CPU-bound). The engine's GPU time
  under DXMT versus wined3d is unknown and unlikely to change much (same
  shaders, same geometry, the same Metal driver underneath either path, I).
  The post chain's gain is the one measured number: one compute dispatch is
  1.7-2.2x three D3D9 passes on the dilation, worth 0.2 ms at 1080p and 0.5 ms
  at 5120x1440 on that pass, perhaps 1-2 ms across the whole chain at
  5120x1440 (I). The 5120x1440 frame is where the chain becomes GPU-bound and
  where this would matter; it is also where the split-loop and pass
  restructuring already under way inside D3D9 attacks the same cost.

## 5. Recommendation and what to measure

**No-go on the translator; measure-first, one item, no game launch by
agents.** Extend the `state_hook_benchmark` / `d3d11_interop_fixture` pattern
with a draw-submission microbenchmark: build the game's per-draw call pattern
from the census (59 state calls of which about 1.3 change value, 4.5
`SetTexture`, VS / PS bind, 47 + 36 float constants, one
`DrawIndexedPrimitive` of ~1,000 primitives, 460 draws per frame, `Present`)
and time 300 frames of it on (a) wined3d D3D9 as today and (b) the same
sequence expressed in D3D11 on DXMT (`D3D11CreateDevice`, state objects
pre-created, cbuffer `Map` per draw). Report CPU per draw and per frame, and
GPU timestamps where the API has them. Run as `X3M_FIXTURE_BOTTLE=X3 python3
verification/probe/wine_lock.py python3 verification/probe/run_<fixture>.py`,
one backend per invocation, 32-bit fixture as the game. Decision rule: below
2x on (b) the D3D11 translator is closed for CPU and this note closes with it;
at or above 2x the CPU case is still under 1 ms per frame and the decision
returns to the GPU case, which is the 5120x1440 post-chain question and is
better answered inside D3D9 first.

## 6. D3D11 versus D3D12 as the translation target

Bottle provenance was read from the CrossOver install without Wine
(`/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib`,
directory listings, `strings`, Info.plists; M unless marked).

| | D3D11 | D3D12 |
| --- | --- | --- |
| Bottle, 32-bit process (X3AP is i386) | DXMT `lib/dxmt/i386-windows/{d3d11,dxgi,winemetal}.dll`, selected by the bottle's `CX_GRAPHICS_BACKEND=dxmt`; feature level 11_1, compute, typed UAV loads, FP16 flip swap chain with PQ / scRGB, timestamp queries at 1 GHz (probe, M) | Apple's D3DMetal (Game Porting Toolkit): two bundled versions, `lib/apple_gptk/external/D3DMetal.framework` 4.0b2 and `lib/apple_gptk3/…` 3.0, each with `wine/x86_64-windows/{d3d11,d3d12,dxgi}.dll` and **no i386-windows directory**: not loadable by a 32-bit process (M). The only 32-bit `d3d12.dll` is Wine's (`lib/wine/i386-windows/d3d12.dll`, 97,856 B, vkd3d strings), which is D3D12 over Vulkan / MoltenVK, i.e. the excluded path. Feature level: D3DMetal's is unprobed and moot for this process; vkd3d's unprobed |
| Native Windows | system D3D11 (Windows 7+) | system D3D12 (Windows 10+); also the OS's own `Direct3DCreate9On12`, which is D3D9 on D3D12 with documented resource unwrapping, so on Windows a 9-on-12 translator already exists and needs no code of ours |
| Fit to the call stream | immediate-mode: `SetRenderState` becomes a dirty bit, `SetTexture` a slot write, the draw a `DrawIndexed`; the runtime (DXMT or Windows) owns barriers, residency, descriptor management and swap-chain sync | explicit: resource-state barriers per bind and per render-target switch; descriptor heaps and per-draw descriptor tables for 16 SRV + 16 sampler slots changing 4.5x per draw; fence-tracked upload rings for the per-draw constants, `DrawPrimitiveUP` data and the managed-pool uploads; pipeline state objects keyed by (VS, PS, blend, depth, raster, input layout, RT formats) compiled at first draw, which hitches unless a cache is warmed; command allocators per frame; explicit present sync. The game's stream gives no batching or lifetime hints, so all of this is inferred per call |
| Effort delta for the same X3 scope | baseline (section 4, 45-70 agent-days, I) | roughly 2-3x (I): the explicit-API machinery above is the size of a small renderer on its own, and the shader target is DXIL / DXBC SM5.1+ rather than SM4/5 |
| Prior art and size | DXMT's own D3D11 coverage (probe-proved for our feature set); D3D9On12 (Microsoft, MIT, incl. the SM1-3 to DXBC `ShaderConv`) is the reference 9-on-modern translator | D3D9On12 is itself 9-on-12; vkd3d / vkd3d-proton (~150k lines, I) show the size of the 12-on-lower side |
| Post-chain gain beyond D3D11 | compute, typed UAV loads, timestamps, HDR colour spaces: all measured on DXMT | async compute queues (our chain is a serial data dependency: masks, resolve, bloom, tonemap), bindless resources, lower per-call CPU for our ~40 passes: nothing material for this chain |

**Recommendation between the two: D3D11.** It is the only immediate-mode
target, so the game's immediate-mode call stream maps call-for-call; DXMT
proved the feature set for a 32-bit process on this bottle; D3D12 has no
32-bit path here except through Vulkan, costs 2-3x for the same scope, and
gives the post chain nothing D3D11 lacks. On Windows D3D12 is reachable for
free through the OS's 9On12, which is a reason not to write a 9-on-12 layer
rather than a reason to target D3D12.

**Vulkan, one paragraph.** Excluded on this target by the user's decision
(CrossOver's Vulkan support). It appears here only for sizing: DXVK's d3d9
module, a complete 9-on-Vulkan (front end ~35-40k lines plus the DXSO SM1-3
shader translator ~10k on the dxvk core, I from knowledge of the repository),
is the best existing measure of what a general D3D9 translator implements;
the X3-scoped layer of section 2 is a fraction of it because the census rules
out fixed-function TnL, fog, stencil, queries, state blocks, volume textures,
instancing and Reset. The bottle does bundle a DXVK d3d9 (`lib/dxvk/i386-windows/d3d9.dll`,
`dxvk::D3D9DeviceEx` symbols, M) which rendered black on 2026-09-17; no
further analysis.

## 7. API boundary versus engine-internal seams; GPU versus CPU; the hybrid

**Is there an internal abstraction above D3D9?** Yes, three layers, all
mapped: (1) material records (`MATERIAL6`: effect-source name plus the pass's
render-state parameters `g_AlphaBlendEnable`, `g_SrcBlend`, `g_ZEnable`,
`g_CullMode`, textures; effect-shader-users.md); (2) the effect loader
`0x004bae10` (`D3DXCreateEffect` at `0x004bafca`, IAT-hooked, about 18
creations per session) and the material dispatch `0x004c0150` (`BeginPass` /
`DrawSubset` / `EndPass` per submesh, its scope at `0x004c5228` already hooked
as `object_trace`); (3) the game's two `ID3DXEffectStateManager` classes
(vtables `0x00562afc` / `0x00562a8c`), which receive every state, constant,
shader and texture set from D3DX and forward them one call each. A replacement
backend could be hooked at (2): replace `ID3DXEffect::BeginPass` with our own
apply of the parsed `.fb` (state ops 146 / 147 are parsed by
`tools/analysis/effect_passes.py`; effect-pass-replay.md Option B), so the
backend receives one "apply pass P with parameter block" per draw plus one
`DrawSubset`, i.e. per-draw deltas instead of 59-63 calls. Its costs are
known from that note: FXLC preshader evaluation (94 of the 211 live programs
carry preshaders), parameter ownership (the engine calls `SetMatrix` /
`SetFloat` / `SetTexture` on the effect, `0x004c21ff` and siblings, so the
wrapper must implement enough of `ID3DXEffect` to hold them), the native
`D3DXFX_DONOTSAVESTATE` semantics, and the fact that it was measured to
remove only D3DX's own walk, about 1.5 us per pass, because a same-value
`Set*` already costs nothing (ratified closed 2026-09-17). The RE cost is low
(the facts exist), the risk moderate (one COM wrapper, no D3DX byte patch,
same on Windows), and the benefit is bounded at about 0.7 ms per stand frame.
An engine-level seam therefore does not reduce what a translator must
implement (the D3DX mesh and texture loaders still hit the device), it only
compresses the state stream, which a translator with dirty flags compresses
anyway. The API boundary remains the right place for any backend change: every
call is already captured, wrapped and fixture-testable there.

**Two goals, separated.**

- *GPU cost.* The post chain: 11.7 ms serialised at 1080p (M, run274), scales
  with pixels, 3.5x at 5120x1440; this is the only cost an API change
  addresses (compute, no slot limit, pipelined timestamps), and by the
  measured dilation the gain is 1.7-2.2x on the mask class of pass, not on
  the frame. The engine's own draws: 5.15 ms GPU at 1080p (M); a "faster
  backend" for them is a hypothesis with no number, and the Metal driver is
  the same under wined3d and DXMT.
- *CPU submission cost.* Stand 21.9 ms (M, run286): engine + D3DX code between
  hooked calls `gap_draw` 9.7 ms (21 us per draw; run250 27 us per draw), the
  proxy's scene hooks 7.4 ms (its passes plus the GPU-sync spins in that
  session), draw hooks 1.9 ms of which native 1.5 ms, state calls about
  0.3-0.4 ms (27,019 x 10-15 ns, M per call), `Present` 9 us. Any API
  translation still receives the 27,019 state calls and 460 draws one by one
  and can shrink only the native 1.5 + 0.3 ms. The levers that reach the
  9.7 ms are engine patches, ranked in view-submit-hot-path.md (R4 per-node
  cache walk 0.3-2 ms, R3 `SetTechnique` skip 0.05-1.5 ms, R5 draw-queue sort
  0-1 ms, R1 + R2 constants 0.08-0.35 ms) and the pass-replay 0.7 ms above.
  State-call elision is closed by measurement: a redundant native
  `SetRenderState` costs 10.1 ns against a 9.5 ns empty virtual call
  (`state-hook-benchmark-elision.json`), so eliding all of them saves 0.24 ms
  in the hooked configuration and nothing in production.

**Hybrid.** The split the question suggests is right in principle (API
translation for GPU, engine patches for CPU) but the GPU half has no
justified vehicle today: a D3D11 translator costs 45-70 agent-days for a gain
that only appears at 5120x1440 and is 1-2 ms there, and the post chain's
pixel cost is being attacked inside D3D9 already. Concretely: keep the post
chain on D3D9 and finish the pixel-cost work inside it; take CPU from engine
patches in the ranked order; run section 5's microbenchmark so the CPU side of
the D3D11 case has a number; and treat HDR output as its own design question
later, because it is the one thing D3D9 on wined3d cannot do at all and the
one thing that would justify a D3D11 presentation path on its own terms.

## Alternatives considered and why they lose

- **Write the 9-on-11 translator (the question as asked).** Loses on the
  ratio of 45-70 agent-days to at most ~1.8 ms CPU and 1-2 ms GPU at
  5120x1440, on the second bottle-coupled layer under the game, and on the
  untestable Windows path that 9On12 already covers.
- **Hand-translate the live shaders instead of generating them.** Loses on
  30-60 agent-days for 211 programs and on mods; a generator is smaller and
  covers overrides.
- **D3D12 as the target.** Loses on 2-3x the work for no post-chain gain and
  on having no 32-bit path on this bottle outside Vulkan; D3DMetal is
  x86_64-only.
- **Engine-seam replacement (BeginPass replay) as the backend boundary.**
  Loses because it compresses the state stream without removing the device
  work the loaders and draws still need, and its CPU gain was measured at
  about 1.5 us per pass.
- **Mixed chain (D3D9 engine + D3D11 post chain on shared targets).** Closed
  by the companion probe: no direction of sharing carries pixels on this
  bottle.

## Unknowns and what settles them

- DXMT's per-draw CPU cost for the game's pattern versus wined3d: the section
  5 fixture.
- The full render-state, texture-stage, transform and FVF stream of a
  session, the UP draws' primitive types, `RenderToEnvMap` use, and whether
  the game ever Resets: a one-session full-state capture and a user
  resolution-change run (phase 1). Not needed unless a translator is revived.
- The engine's own GPU time under DXMT versus wined3d: unmeasurable without a
  translator; assumed equal (same shaders, geometry and driver).
- Whether the 9On12 factory is a usable Windows path for HDR output: not
  testable here; a Windows tester would run the proxy with
  `Direct3DCreate9On12` answering the game's `Direct3DCreate9`.
