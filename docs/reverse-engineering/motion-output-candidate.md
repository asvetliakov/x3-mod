# Same-draw motion output: first material candidate

Read-only feasibility review, 2026-09-11. The common Argon SM3 pair has enough
register space to add one previous-clip interpolator and a correspondence MRT while
retaining its original position and color instructions. This is a candidate for
an original synthetic prototype and local shader transformation. This document
records the static review, which performed no source edits or runtime launch.
The subsequent [detached prototype verification](../verification/material-motion.md)
now tests this pair; it remains disconnected from game rendering.

## Exact pair and observed scope

| Stage | Full-program FNV-1a-64 | Bytes / DWORDs | Full SHA-256 |
| --- | --- | ---: | --- |
| VS 3.0 (`fffe0300`) | `53a0a641107ed76c` | 2104 / 526 | `bc402d1c2bfbbcb9fedd98890db845dab2a24da8cfb5a88a74c4eafa40f7a50c` |
| PS 3.0 (`ffff0300`) | `8759c7838bbc86c2` | 5040 / 1260 | `9fd15484fe419295cfb3534bd4f978efc8855c1e3e6a06e776533497dad48dc0` |

Original bytecode is local at `/tmp/x3-shader-sweep/programs/`, named
`vs_53a0a641107ed76c.bin` and `ps_8759c7838bbc86c2.bin`. D3DX text is under
`/tmp/x3-shader-sweep/disassembly/` with `.bin.txt` appended. These copyrighted
programs and disassembly must remain untracked. The tracked
[full inventory](../../verification/results/shader-sweep-inventory.json),
[aliases](../../verification/results/shader-sweep-aliases.json) and
[rigid-position proof](../../verification/results/rigid-position-profiles.json)
provide derived metadata.

The pair shares `shader/3_0/argon.fb` and its `hueshift_off`, `v_lights_off` and
`hue_lights_off` paths. The PS has eight catalogue/path occurrences covering those
four distinct paths; VS aliases are broader and include other races/two-sided
variants. Eligibility must use the exact **pair and draw state**, not the VS alias.

A read-only block audit of `/tmp/x3-iteration05-completed-snapshot.log`
(216,605,445 bytes, SHA-256
`e5beaa861d04659fe9c7df05a01845bd05d656a33c643f4b484ff379cf3ccaf8`)
found **2,180 draw blocks across 20 frames** with exactly this pair. All 2,180
have a successful matching draw_result, Z enable/write both one, alpha test and
alpha blending disabled, COLORWRITEENABLE `15`, separate-alpha blending and
sRGB-write disabled. All use POSITION0 at offset zero, type `FLOAT16_4`, and
RT0 A8R8G8B8, 1280×768, no MSAA. Their VS i0 is `{0,0,1,0}`.
These counts describe the captured blocks, not every game draw or full-frame
motion coverage. One concrete example is frame 1794, draw index 7.

## Register and instruction headroom

| Resource | Original use | Proposed first addition |
| --- | --- | --- |
| VS inputs | v0 POSITION0, v1 TEXCOORD0, v2 NORMAL0 | None; consume the same vertex fetch as the actual color draw. |
| VS outputs | o0 POSITION0; o1 COLOR0; o2–o5 TEXCOORD0–3 | o6 / TEXCOORD4 previous clip. |
| VS temporaries | r0–r6 | No new temporary required for the four extra previous row dots. Existing r1 is the protected homogeneous position through shader end. |
| VS float constants | c0–41 application values, DEF c42 | Four previous WVP rows at c252–255, subject to the relative-access gate below. |
| PS inputs | v0 COLOR0; v1–v4 TEXCOORD0–3 | v5 matched to new TEXCOORD4; full precision, ordinary perspective interpolation. |
| PS temporaries | r0–r4 | r5–r7 for the relocated authored motion PS. |
| PS constants | GPU c0–7 and DEF c8; comments also contain CPU preshader metadata | c216 coordinates (inverse viewport size and previous jitter UV), c217 mode; relocated shader-local DEF c218–220. |
| PS samplers / outputs | s0–3; oC0 only | No sampler; oC1 previous UV/depth/valid. No oDepth writes in the source. |
| Static slot estimate | VS approximately 57; PS approximately 49 | Four additional VS DP4s (approximately 61 total slots); 25 appended authored PS arithmetic instructions (approximately 74 total slots). |

SM3 provides 12 VS outputs, at least 256 VS float constants, 10 PS inputs and
32 temporaries per stage. The proposed one-vector linkage fits without changing
existing semantics or component packing. VS outputs are write-only, so the patch
cannot simply read o0 to copy its clip value. See Microsoft's
[VS register limits](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-vs-registers-vs-3-0)
and [PS register limits](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-registers-ps-3-0).

## Position and interpolation plan

The existing verified constructor makes r1 = `(POSITION0.xyz, 1)` for ordinary
finite values; it deliberately ignores source POSITION0.w. Four unconditional,
unmodified DP4s write o0.xyzw from r1 and c24–27 in XYZW order. Subsequent
instructions do not write r1 or position. There is no position deformation,
position clamp or vertex texture fetch. The input declaration's FLOAT16_4
conversion remains performed by the original draw.

The narrowed first transformation leaves every original instruction intact,
adds one new output declaration, and inserts four previous row dots immediately
after the original final position DP4. It writes previous clip from r1/c252–255.
No new current-clip export or changed original o0 destination/order is required.
New writes must not use `_pp` or saturation. Original centroid modifiers on
TEXCOORD1–3 remain unchanged; the new interpolator must not inherit them. The
first prototype should stay non-MSAA, as observed here.

The PS appends a relocated copy of our existing
[`rigid_motion_ps.hlsl`](../../src/temporal/rigid_motion_ps.hlsl) compiled program:
v0 becomes v5, r0–2 become r5–7, c0–4 become c216–220, and oC0 becomes oC1.
Its DEF/DCL instructions belong in the declaration header; append only its
executable instructions before original END, without a second version/END or
misleading relocated CTAB. Preserve the original shader's comment/preshader
metadata and all material instructions. The original PS has no references to
any of those new registers, no relative addressing and no control flow, so these
ranges do not collide.

This produces the existing **RGBA32F previous-UV / previous-depth / valid ABI**,
not a newly invented velocity sign convention. The authored fragment divides the
interpolated previous clip by W, handles invalid W/depth/large or invalid values,
applies the established half-texel and prior-jitter convention, and writes the
existing invalid sentinel when mode is off. The current screen coordinate already
comes from the current raster sample in the history consumer. Perspective-correct
interpolation followed by per-pixel division is essential; interpolating
vertex-divided previous NDC is not the same calculation.

### Exact original insertion boundaries

All indices are zero-based DWORD offsets from the version token, including all
comments. These are derived structural facts, not an implemented patch manifest.

| Site | Original DWORD index / invariant |
| --- | --- |
| VS literal DEF | 302, c42; six DWORDs |
| VS declarations | 308, 311, 314, 317, 320, 323, 326, 329, 332; three DWORDs each |
| VS new output declaration | Insert at 335, before original first arithmetic |
| VS homogeneous constructor | 335; r1 written once, untouched through all position writes |
| VS original position DP4 X/Y/Z/W | 450 / 454 / 458 / 462, four DWORDs each |
| VS previous-clip arithmetic | Insert at 466, after original final DP4 and before following material instruction |
| PS original literal DEF | 1041, c8; six DWORDs |
| PS relocated new definitions | Insert at 1047, after original DEF |
| PS original declarations | 1047 through 1071 in steps of three |
| PS new v5 declaration | Insert at 1074, before original first arithmetic |
| PS appended authored arithmetic | Insert at 1259, before original END |

For example, the original last position dot at 462 has instruction token
`03000009`, destination `e0080000`, source `80e40001`, constant `a0e4001b`.
Reframe and revalidate the whole program rather than patching a byte substring.
Apply original-offset insertions in descending order or track shifts explicitly.
Keep modified-program identities distinct from both original shader hashes.
The present embedded fragment has 18 DEF DWORDs, three DCL DWORDs and 111
executable DWORDs after excluding its comment/version/END. The described splice
therefore yields 545 VS DWORDs and 1,392 PS DWORDs before any other changes.

Same-draw output removes the separate geometry replay and its depth-equality,
vertex-buffer retention and duplicate rasterization requirements for this
contribution. It does not establish that a current object corresponds to last
frame: previous submitted rows still need the reviewed object/camera lifetime,
load/reset and geometry-change policy. Other materials, particles, overlays and
unmatched contributors remain outside this one-pair prototype.

## Relative constants and original color coverage

The VS light loop is real relative addressing: r0.w starts at zero, each iteration
uses a0.w = `3 * r0.w`, reads c0/c1/c2 at that offset, then increments r0.w. i0.x
controls the REP count. Therefore “highest direct constant is c42” does **not** by
itself prove c252–255 unused. Requiring integer i0.x in `[0,8]` bounds those reads
to c0–23; the eight-element CTAB array supports the intended bound but is not the
runtime check. Captured i0.x is zero for this pair. Without a checked bound,
refuse substitution instead of overwriting potentially read constants. Save and
restore application c252–255 around the actual transformed draw; D3DX/effect
constant uploads do not know about the extension.

The PS has no dynamic flow, relative constants, texkill/discard or oDepth write.
It **does write material alpha**: texture/lightmap interpolation is multiplied by
COLOR0.w, whose VS path includes fog and g_AlphaValue. Preserve that alpha and
all oC0 arithmetic. This is an opaque candidate because the captured alpha-test
and blend states are off, not because the PS lacks alpha. The existing
[radiance-clamp profile](material-radiance.md) is a separate change and must not
be silently combined with a motion prototype; its altered bytecode has a different
identity.

## MRT and actual parity gates

A new oC1 needs a compatible, writable RT1 that does not replace any existing
application MRT. RT0 and the scene depth stay exactly the original objects.
The narrowed prototype uses the existing RGBA32F motion ABI, so RT1 is
A32B32G32R32F (128 bits/pixel), while the captured RT0 is 32 bits/pixel.
This requires checking NumSimultaneousRTs, format renderability and
**D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS**, plus an actual mixed-format MRT test.
A passing independent RGBA32F render test alone does not prove this simultaneous
combination. Dimensions and MSAA must match, and RT1 color-write state needs
explicit setup/restoration. Microsoft documents
[MRT restrictions](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets).
A more compact motion payload would be a later ABI/precision change.

Blending is shared across MRTs in D3D9; the initial draw gate should retain the
observed blending/alpha-test/sRGB-off case. Binding RT1 and different shaders can
still affect driver compilation, raster or color behavior despite unchanged
original instruction words. The next original synthetic fixture must compare
color/depth bits and coverage with motion off/on, then check stationary, camera,
object and perspective motion numerically. Include unavailable/mismatched MRT,
relative-loop bound, near-plane and invalid-history refusals. Runtime draw-scoped
state binding/restoration and concurrent application-call admission are still
needed; this approach reduces replay requirements, not all interposition work.
