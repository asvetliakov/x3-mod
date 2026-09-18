# Asteroid distance fog and temporal admission

The run23 far/near change is consistent with a native **distance-fog render-state
switch**, not a geometry or LOD change. The engine enables alpha blending and
turns off depth writes for the fogged draw; the shader fades its RGB contribution
against the already-rendered background. Even an opaque diffuse texture would
not make that draw an opaque replacement of the background.

This is a bounded static analysis of the existing X3AP.exe (preferred base
`0x00400000`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`), paired
with the existing [run23 comparison](../verification/run23-material-comparison.md).
No game, Wine or build was run. Extracted effects, shader disassembly and native
disassembly remain local under `/tmp`. No renderer policy is changed here.

## Captured boundary

The compared candidate is node `7056e938`, model `4fee`, LOD 0, with 1,712
triangles / 1,002 vertices and original VS `167eb2d5629ab9d3` plus PS
`d44db87778a43b61`. The association with the pictured target remains a
high-confidence candidate, not a directly captured target-to-node identity.

| Property | Far frames 10752–10755 | Near frames 11607–11610 |
|---|---|---|
| RGB blending | SRCALPHA / INVSRCALPHA, ADD | Disabled; ONE / ZERO |
| Separate alpha blend | Off | Off |
| Depth write | Off | On |
| RT0 color-write mask | RGB, 7 | RGBA, 15 |
| VS fog boolean b0 | True | False |
| VS c39.x, `g_AlphaValue` | 1 | Sparse observed value 0 |
| VS c41.xy, `g_FogClip` | 1.0526316, 2.1052632e-7 | Fog branch disabled |
| Existing temporal treatment | RT1/RT2 sentinel; background-camera path | Object motion / depth valid |

The original shader pair, node/model and geometry remain the same across these
windows. The table records observed states, not a claim that the native branch
was instrumented at its instruction address.

## Effect pass applies host parameters

The bounded archive check covers **16 effect occurrences and 32 passes**:
`01.cat` has `shader/3_0/` in base, `hueshift_off/`, `hue_lights_off/` and
`v_lights_off/`, each with `asteroid.fb`, `asteroid_0000.fb` and
`asteroid_0001.fb`; `addon/01.cat` has the four unsuffixed paths. Every occurrence
has `DEFAULT/P0` and `BUMPMAP/P0`. The eight unsuffixed occurrences bind the
captured BUMP pair. This establishes alias membership, not runtime archive
precedence.

All 384 parameter-driven render-state expressions in those passes are single
scalar identity expressions: none decides fog, distance or blending itself.
The relevant bindings are:

| Native effect parameter | D3DRS state | Archive default |
|---|---:|---:|
| `g_ZEnable` | ZENABLE, 7 | 1 |
| `g_ZWriteEnable` | ZWRITEENABLE, 14 | 1 |
| `g_ALPHATESTENABLE` | ALPHATESTENABLE, 15 | 0 |
| `g_SrcBlend` | SRCBLEND, 19 | 2 = ONE |
| `g_DestBlend` | DESTBLEND, 20 | 1 = ZERO |
| `g_AlphaBlendEnable` | ALPHABLENDENABLE, 27 | 0 |
| `g_ColorWriteEnable` | COLORWRITEENABLE, 168 | 7 = RGB |

All occurrences default `g_EnableFog=false`, `g_AlphaValue=1`,
`g_FogClip=(1,0)` and `g_SeparateAlphaBlend=false`. The unsuffixed passes use
ADD blend operations and alpha comparison GREATEREQUAL/reference 1. Alpha
testing is nevertheless disabled in the compared draws. The far blend 5/6,
far depth-write 0 and near color-write 15 require host parameter changes.

Local reproducible archive evidence is
`/tmp/x3-asteroid-fog-effect/{inspect.py,derived.json}`; it reuses the existing
catalogue/effect parser and keeps extracted data untracked.

## Native producer and distance decision

[Material submission](object-identity.md) at `0x004c0150` receives the render
node as its second stack argument and camera/render context as its third. It
applies the material's cached D3DX parameter block, changes parameters for the
current node/camera, and then begins the effect pass. The existing effect state
manager applies the resulting states; see [constant uploads](constant-uploads.md).
The fog decision is therefore per native material submission, not a separate
shader identity or LOD decision.

The sector-camera update supplies the fog settings upstream. At
`0x00421533–0x00421586`, the sector/type index from `*(cockpit+0x54)+0x13c`
selects a record in `*0x00606fc0` with stride `0xdb8`; record fields `+0x148`
and `+0x14c` are copied to sector camera `+0x36c` and `+0x370`. The same region
updates camera `+0x368` and fog flag `+0x270 & 0x10000`. This extends the
existing [sector-camera trace](external-camera.md). The exact sector record
used in run23 was not captured by this bounded study.

At `0x004c2b43–0x004c2b63`, fog is bypassed if camera flag `0x10000` is clear,
or node flags `+0x12c` contain `0x02000000`. Otherwise define:

- `N`: signed native near-fog distance at camera `+0x36c`.
- `F`: camera `+0x370`, with a minimum of 100,000,000 native coordinate units
  when the configuration integer at `*0x00606f34 + 0x768` equals 2, or
  500,000,000 when it is at least 3. Values below 2 use the camera value.
  This field's UI setting name is not established here.
- `D`: distance between node translation `+0xb0/+0xb4/+0xb8` and camera
  position `+0x30/+0x34/+0x38`, computed from integer differences, a
  floating squared sum, square root (`0x00412440`) and integer conversion
  (`0x0052b5d0`, truncation for the ordinary finite nonnegative domain).

The compare at `0x004c2c63–0x004c2c6f` tests `F-D` against `F-N`. For ordinary
non-overflowing distances, **D < N bypasses fog; D >= N enables it**. This is a
node-origin threshold, without a radius, vertex extent or LOD term. Its exact
boundary follows native intermediate precision/integer conversion; it is not a
new floating-point distance test implemented by the proxy.

The enabled branch sets `g_ZWriteEnable=false` at `0x004c2cea` and
`g_AlphaBlendEnable=true` at `0x004c2cff`. For an ordinary previously unblended
material without the special material marker at descriptor `+0x1a4`, it also
sets `g_SrcBlend=5` and `g_DestBlend=6` at `0x004c2d27/0x004c2d3c`.
The branch does not force every pre-existing transparent material to this blend
mode. The separate per-node alpha override at node `+0x13c` can independently
enable blending and supplies `g_AlphaValue` scaled by 1/255.

Let `s` be the context conversion scale at `*(camera+0x1c)+0x2c`. At
`0x004c2d3e–0x004c2df2`, the engine writes:

`g_FogClip = (F*s / ((F-N)*s), 1 / ((F-N)*s), 0, 0)`.

It then sets `g_EnableFog=true` at `0x004c2e04–0x004c2e59`. The bypass branch
writes `(1,0,0,0)` and disables that boolean. Native material parameter-block
application restores the baseline parameters before per-node overrides;
parameter-block construction includes `g_AlphaValue=1` at `0x004c1771`.
The far case therefore need not inherit the preceding opaque draw's alpha zero.

## Exact shader alpha and the write mask

For the captured VS, CTAB identifies b0 as `g_EnableFog`, c39 as
`g_AlphaValue`, and c41 as `g_FogClip`. Its native world/camera calculation
produces the vertex-to-camera vector. With fog enabled it writes:

`COLOR0.a(vertex) = c39.x * saturate(c41.x - c41.y * distance(vertex,camera))`.

Without fog it writes `COLOR0.a = c39.x`. Distance is evaluated per vertex,
then COLOR0 is interpolated; the CPU decision above uses the node origin.
Thus a large mesh can straddle the fog threshold when its draw-level state
changes. No new shader or altered vertex geometry is required for that change.

The PS's last diffuse fetch supplies s0 alpha. Subsequent detail mixing changes
RGB only, and the sole alpha output is the original partial-precision multiply:

`RT0.a(source) = diffuseSample.a * interpolated_COLOR0.a`.

There is no TEXKILL in this PS. Neither the detail map nor its weight determines
alpha. Base-texture alpha remains a real dependency; this study does not prove
that the bound resource has alpha 1 everywhere or that all raster samples have
nonzero alpha.

Captured c41 implies a scaled far distance of approximately **5,000,000** and
near distance of approximately **250,000** shader-world units, since
`F*s = c41.x/c41.y` and `N*s = (c41.x-1)/c41.y`. This does not alone identify
native integer distances, scale, quality setting, or the camera's sector row.

The native write-mask path explains the otherwise surprising near c39=0.
After the fog changes, `0x004c300e/0x004c3022` reads the current effect alpha
blend/test booleans into stack slots `+0x78/+0x7c`. In the ordinary no-glow path,
when config byte `*0x00606f34+0x100` has bit `0x80` and renderer capability
`*(*0x00608b3c+0x18)+0x94` is set, `0x004c379a–0x004c3818` selects:

- Blend or alpha test enabled: `g_ColorWriteEnable=7`, preserving RT0 alpha.
- Both disabled: `g_ColorWriteEnable=15`; with no node-alpha override or
  special material marker, `g_AlphaValue=0`.

This is consistent with the engine's glow/alpha attachment bookkeeping, not
transparent RGB in the near case: RGB blending is off there. Other glow and
special-view branches have their own masks and separate-alpha settings; the
above is not a universal rule for every X3 pass.

## Consequences and remaining evidence limits

The far draw has triangle raster coverage and no shader discard, but its native
visible RGB is `a*surfaceRGB + (1-a)*backgroundRGB`, with depth testing retained
and **depth writes disabled**. It is an otherwise solid model rendered through
a genuine overlap/compositing operation. This is not evidence for an opaque
post-fog replacement: forcing alpha 1 or enabling depth writes would change
native background visibility, overlap ordering and potentially later draws.

A future temporal treatment must distinguish raster coverage from this mixture
of surface and background contributions. The far draw may even contain pixels
with zero source alpha; these are still rasterized but add no surface RGB.
Treating its motion/depth as an opaque surface everywhere is not justified by
this analysis. The existing run23 sentinel/object-valid transition is explained
by the native state change, but the best temporal representation of that fade
is a separate design decision.

Remaining limits are the exact run23 sector/type record and native scale,
bound diffuse alpha values and visible overlap/order at individual pixels,
and direct target-to-node association. None requires guessing that the shader
or LOD changed. The owning quantitative note records the captured windows;
this note supplies the native cause and its alpha/depth constraints.

## Run 47: triangles missing and reappearing

Read-only diagnosis of user run 19 (snapshot `/tmp/x3-bottleX3-run47`, log
`session-20260914-225307-216.log`, queried with grep/python only; no Wine, no
build, no source change). Scratch images stayed outside the repository.

### Mechanism 1 (supported by witnesses): jitter parity break against the engine's depth-only prepass

The engine draws every **fogged** asteroid twice in the same frame: first a
depth-only prepass with VS `c78b4c68a87fce74` (the `z_only` alias,
[shader-fingerprints.md](shader-fingerprints.md)) and a **null pixel shader**,
then the fogged material draw with the asteroid pair. Captured states:

| Draw | VS / PS | ZENABLE | ZWRITEENABLE | ZFUNC | ALPHABLENDENABLE | COLORWRITEENABLE | `motion_route` |
|---|---|---|---|---|---|---|---|
| Prepass (frame 14639 index 6) | `c78b4c68a87fce74` / null | 1 | **1** | 4 LESSEQUAL | 0 | **0** | `gate=3 jittered=0` |
| Fog draw (frame 14639 index 19) | `167eb2d5629ab9d3` / `d44db87778a43b61` | 1 | 0 | 4 LESSEQUAL | 1 (5/6) | 7 | `gate=4 jittered=1` |

Same-node pairing from `object_context` in frame 14639: nodes `183f5fc8`,
`183f5848`, `183fbc60`, `183fa860` appear at indices 6/19, 7/21, 12/27, 13/29
(prepass, then fog draw). Frame 18555: prepass indices 6–9 then fog draws
10–13 with identical primitive counts 7152/1712/1712/1712. The **near, opaque**
asteroid nodes (`183f2dc8` index 26, `183faae0` index 28, `183f64c8` index 18)
have no prepass: they are drawn once, opaque, routed and jittered.

The route jitters "every scene draw whose VS has a table row, routed or not"
(`src/proxy/motion_output.cpp:3549-3551`, `apply_jitter` runs before gate 3),
so the fogged asteroid draw is jittered although gate 4 refuses it. The
`z_only` VS has no row in `motion_output_profiles_inc.h` (only in the rigid
position table, `rigid_position_profiles_inc.h:180`), so `shadow_.vs_row` is
null and the prepass is **not** jittered: all 15 `c78b4c68a87fce74` route
records in the log are `gate=3 jittered=0`. The fog draw's fragment at pixel
p shows content at `p - jitter` and depth-tests LESSEQUAL against the
prepass depth of the same surface at p. On a planar facet the difference is
`grad(z) . (-jitter)`, uniform in sign over the facet: facets whose depth
increases along `-jitter` fail everywhere and drop out as a whole, showing
the background (dark against the sunlit side, hence "dark triangles"). The
eight-sample Halton sequence changes the sign pattern every frame, so facets
vanish and reappear per frame. Vanilla has no jitter, so both draws agree
and no facet is lost. Blending in FP16 and the linear-material work are not
involved: the refused draw keeps its original VS/PS and states
(`bind_variant_pair` and the material variant are applied only inside the
routed apply, `motion_output.cpp:3477-3486`; `linear_material_refusal` runs
only after the motion gates); the only change to that draw is the jittered
clip rows, restored bit-exactly after it (`restore_jitter`). `mip_bias=0`
in this run.

Witnesses from the run-47 capture frames (`hdr_1_<frame>.rgba16f` is the
FP16 scene target read **before** the write-back and is not the resolved
image, `motion_output.cpp:4289`; the resolve output stays in `hdr_resolved_`,
`:1244`):

- Frame 18555 (`jitter_x=-0.125 jitter_y=-0.277778`, only fogged asteroids
  and unrouted draws, RT2 sentinel on 100 % of pixels): both large asteroids
  show triangular holes on the **lower** limb only; the sunlit upper facets
  are intact. Raster moves up by 0.28 px, so at p the fragment is the content
  0.28 px lower, farther on the lower limb, closer on the upper limb.
- Frame 14639 (`jitter_x=-0.25 jitter_y=+0.166667`): the three unrouted
  asteroids (crops at x 330–420/y 440–530, 440–530/320–410, 580–660/240–330,
  routed-depth fraction 0.000) show holes on the **upper-right** limb; the
  three routed near asteroids (crops with routed-depth fraction 0.49–0.56)
  have no holes. The hole side flips with the jitter sign, as the mechanism
  predicts; the resolve cannot have produced them because they exist in the
  pre-resolve image.

### Mechanism 2 (rejected): resolve-side rejection or clamp on sentinel pixels

For a pixel covered only by the unrouted blended draw: RT2 holds the fill
sentinel -1 and RT1 alpha -1. Run 47 ran sentinel policy 2 on 24,190 of
24,728 frames (`camera_policy=2 reason=0`; policy 1 on 538 frames), so the
resolve sets `depth = 1`, reprojects through the camera far-plane path, keeps
that path when no closer neighbour wins the 3x3 dilation, proves the
disocclusion test by the sentinel previous depth, and accepts history clamped
to the current 3x3 mean ± 1.25 sigma (weighted domain, `taa_k=1`) at weight
0.9. Consequences: a stationary far asteroid accumulates normally; a whole
missing facet makes the current neighbourhood dark, the clamp pulls the
history into it, and the hole shows at nearly full contrast the same frame.
The resolve therefore neither hides nor causes the flicker; it has no
per-triangle term (fog alpha is per-vertex, interpolated, and the resolve
never reads alpha). Evidence against this as the cause: holes in the
pre-resolve FP16 image; `camera_state reason=3` 0.01 %, `cutout_missed` 0.

Gate 4 refusal of the fog draw is expected (ALPHABLENDENABLE on, ZWRITEENABLE
off fail `draw_state_ok`, `motion_output.cpp:3570-3581`) and the refusal path
leaves the draw untouched except for the jitter rows, which is exactly the
hazard: the jitter invariant "the whole scene moves together" assumes every
depth writer in the scene is jittered, and the null-PS prepass is not.

### Diagnostic for run 20 (no build needed for the witness)

Same launch as run 19 plus `--capture-frames 8` (X3M_CAPTURE_FRAMES=8;
`--taa-debug` is optional: on the HDR route the pre-resolve `hdr_<dev>_<frame>.rgba16f`
readback is already written on every capture frame). Zoom on a far asteroid,
hold still, press **F8** once: eight consecutive capture frames give
`motion_output_frame ... jitter_x jitter_y` per frame, the `draw` /
`object_context` / `state` records, and eight FP16 images. Expected: the
same nodes drawn twice (`c78b4c68a87fce74`/null then the asteroid pair), the
hole side following the sign of the jitter each frame, no holes on routed
asteroids. Negative control if wanted: the same launch without `--taa`
(jitter off) shows no holes. There is no runtime TAA or jitter hotkey
(Ctrl+Shift+F9/F10/F11 are EV, bloom and AO); `X3M_SCENE_DEPTH_CAPTURE` and
`X3M_TAA_DEBUG` add nothing the mechanism needs.

Optional one-counter addition if a per-frame number is wanted without
images: at `src/proxy/motion_output.cpp:3551`, after
`if (jitter_active_ && shadow_.vs_row) apply_jitter(route);`, add
`else if (jitter_active_ && write == 1) ++counters_.unjittered_depth_writers;`
and print it in `motion_output_frame`. `write` (ZWRITEENABLE) is already read
for every tracked draw (`:3527`), so the cost is one compare per unrouted
scene draw; no D3D call.

### Fix: the z_only prepass is jittered with the scene

`src/renderer/depth_prepass_profiles.h` names the two z_only aliases
(`c78b4c68a87fce74`, 89 dwords; `803ebfd17f79e413`, 95 dwords; both vs_1_1,
clip rows in c0-3, the same `MadXYZIdentityWFromX` row-dot path the rigid
position table records: `r0 = v0.xyz * c4.x + c4.y`, `oPos = dp4(r0, c0..c3)`).
Registration binds a program to that table only when it has no pair row;
`evaluate_draw` then jitters it exactly like a pair row's VS (`apply_jitter`
takes the register from either table, `restore_jitter` puts the application
rows back bit-exactly) and nothing else changes: the draw keeps its null PS,
ZWRITE/ZFUNC/COLORWRITE state and never routes (gate 3 still needs a pair
row). The c0-3 window was already shadowed (62 pair rows use register 0), so
the vs_1_1 constant window needs no new shadow state; the windows are derived
from both tables with a static assertion. Jitter off (no TAA) takes the
unchanged path. Per draw the cost is one pointer test.

Counter: `unjittered_depth_writers` on `motion_output_frame` counts scene
draws with ZENABLE and ZWRITEENABLE on that went out unjittered while the
jitter was active (after gate 2, so background and overlay draws are not
counted). Run 20 must show it at 0 on every frame; a nonzero count names
another depth writer without a table row.

Fixture: `run_motion_output.py` cases `production-zonly` and `seam-zonly`
(fixture mode `zonly`, nine frames, Reset after frame 3). Each frame draws the
real `c78b4c68a87fce74` prepass (null PS, COLORWRITEENABLE 0, ZWRITE on,
LESSEQUAL, rows in c0-3) and then the blended z-write-off material draw of the
same triangle with perspective rows (depth slope along x). Regular frames
require zero interior holes and the coverage oracle's agreement; control
frames 2, 4 and 6 (jx > 0) pre-shift the prepass rows by the negative jitter
so the route's jitter cancels and the prepass lands unjittered as the game's
did: the material draw must then lose more than half its interior, proving
the oracle detects the mechanism. The runner also checks every frame line
(`jittered=2 routed=0 unjittered_depth_writers=0`) and the route records
(prepass gate 3, material gate 4, both jittered) on the capture frames 1-3;
the Reset after frame 3 closes the capture window, so the later frames carry
the frame line and the fixture's own hole count only.

### Other unjittered scene programs (run 47)

The other unjittered scene programs in run 47 (`d5e1c75351ed3f04`,
`5e484a06672e28fb`, `36f98d151fd6b0c6`) were seen with ZWRITEENABLE 0 where
checked (frame 18555 indices 34/35) and are a lesser, sub-pixel-offset
concern, not this symptom.

## Run 49: a station section trembles at distance

Read-only diagnosis (no build, no Wine, no launch) on the run-49 snapshot
`/tmp/x3-bottleX3-run49` (`session-20260915-010311-212.log`, 495 MB, queried
with grep/Python only) and the user screenshot `screenshots/jitter1.png`.
Capture group 11940–11947 is the screenshot view (same station pose; the circled
region is x 490–640, y 210–360 at 1280×768). Scratch scripts stayed under the
session scratchpad; nothing here needs a fixture rerun.

### Mechanism (witnessed): the fade-band asteroid behind the hangar gap resolves current-only

The trembling content is not a station draw. It is asteroid node 53195 (model
`4fee`, 1712 triangles, pair `167eb2d5629ab9d3`/`d44db87778a43b61`) in its
linear-distance-fade band, seen through the open hangar section. Per frame it
is drawn twice: the engine's depth-only prepass (`c78b4c68a87fce74`, index 6,
gate 3, `jittered=1`) and the colour pass (index 20) with alpha blend on, which
the route refuses at gate 4 (DrawState) but still jitters. So its raster moves
with the scene, but its pixels carry no motion rows: RT1 alpha −1, RT2 −1.

The fade route binds the draw as a fade region every frame from 11615 to 12898:
`fade_region … index=20 status=bound jittered=1 rect=542,247,626,332
f_permille=7` (`f_permille` is the rect's share of the viewport, 7140 px of
983040), and the composition frame at 11940 reports `prepared=1 fade_prepared=1
mask_valid=1 in_place=1 region_pixels=7140` with `fade_witness … rects=1
covered=3055 union=7140`. That coverage is the reactive mask M
(`motion_output.cpp` 1189–1197: `in.reactive = composition_mask`,
`SupplementalMaskWithDepthSentinel`; `temporal_pass.cpp` 250–252 → resolve
`options.y`), and the resolve returns the current sample wherever M is set
(`resolve.hlsl` 156–157, and 95–97/`reactive` for history taps). This is the
documented contract ("M rejects their history (current-only TAA, possible edge
aliasing) — the asteroid contract", linear-station-source-over.md), but with
uniform jitter a current-only pixel of a *jittered* draw shows the raw jitter:
the section moves by Δjitter every frame (up to 0.81 px in x, 0.94 px in y for
the 8-sample Halton set) while the routed station around it is reprojected and
stable. It stops closer because the asteroid leaves the gap: at 12213 the
region is still bound (`rect=640,285,726,369 covered=2944`), at 13181 the
frame's only fade region is another draw (index 433) and `covered=0`.

Witness, raw FP16 captures (`hdr_1_11940..11947`, the pre-resolve scene target;
`hdr_writeback` reads `hdr_->target()` before the resolve writes `out.color`):
a per-tile Lucas–Kanade shift (16-px tiles, 3 iterations, log-luminance)
between consecutive frames, projected onto the logged Δjitter (f = 1 means the
tile moved exactly by the jitter delta):

| pair | Δjitter px | inside rect 542–626 × 247–332 | tower (control) | body (control) |
|---|---|---|---|---|
| 11940→41 | (−0.250, −0.556) | (−0.274, −0.566) f=1.03 | f=1.14 | f=0.90 |
| 11941→42 | (+0.500, +0.333) | (+0.475, +0.279) f=0.91 | f=1.04 | f=0.86 |
| 11942→43 | (−0.812, +0.333) | (−0.724, +0.390) f=0.93 | f=0.85 | f=0.87 |
| 11943→44 | (+0.438, −0.556) | (+0.364, −0.536) f=0.91 | f=0.80 | f=0.97 |
| 11944→45 | (−0.250, +0.333) | (−0.262, +0.265) f=0.89 | f=0.75 | f=1.05 |
| 11945→46 | (+0.500, −0.556) | (+0.402, −0.603) f=0.96 | f=0.81 | f=0.90 |
| 11946→47 | (−0.625, +0.333) | (−0.608, +0.318) f=0.97 | f=0.95 | f=1.05 |

So the raw raster inside the rect is jittered exactly like the station (the
camera is static: HUD 0 m/s, `camera_state … rotation_deg=0.0000 policy=2`).
The routed station is stable after the resolve because its motion rows are
exact: for every routed pixel of frame 11940, `motion.xy + jitter − uv` has a
median of 0.001 px (p90 0.002 px) in the circle, tower, body, arm and ship
regions, and `motion.z` equals the RT2 depth bit for bit. The only pixels the
resolve cannot stabilise in the circle are the mask-covered asteroid pixels
(and their 3×3 expansion), which is where the RT2 map shows the unrouted
share of 10–50 % per tile (rows 16–19, cols 30–36) that the routed station
does not cover: the gap.

### What the triage's unrouted rows are (not this symptom)

- Nodes 58844–58851 and 58872 (`4944d81dfe531b37` with `0c1f3f0f…`/`ca6bfa4a…`/
  `c30104cb…`) are children of 58843 and project to screen (640, 557) at
  w = 148 on all six capture frames: the player's ship in chase view, not the
  station. 58854 (`d5e1c75351ed3f04`/`8360f422de08b5bd`, 360 triangles, gate 3,
  `jittered=0`, Z-write off, ONE/INVSRCCOLOR) is its engine effect at (640, 657).
- `motion_route … gate=3 jittered=0 node=00000000`: the effects draw above plus
  the particle billboards (`36f98d151fd6b0c6`/`222bee0defcb1852`, 2–180
  primitives) and the stardust batch (`5e484a06672e28fb`/`0a523f33ac47ae05`,
  ≈1000 primitives), all Z-write off and blended; the view-only VS has no table
  row so `apply_jitter` skips them (motion_output.cpp 3568). Sub-pixel, no
  depth, as recorded for run 47.
- Handles never in `motion_route`: 58245/58246 are the background dome/planet
  (indices 1–5, camera 60312, drawn before the scene is bound, gate 2 =
  `MotionGate::Scene`); 60278–60280, 51946–51948, 58875, 58877 are two-triangle
  quads drawn after the scene-end signal (gate 2, overlays). Gate 2 draws get no
  route row by design; `gate2=20` = 5 + 15 on every capture frame.
- `unjittered_depth_writers=0` is correct: every depth writer in the scene phase
  is jittered, including the asteroid's prepass.

### Fix direction and cost

Route the fade-band draw instead of masking it. The colour pass is a reviewed
opaque pair (gate 0 at frame 4900 when the same asteroid was closer and not
fading) refused only for `ALPHABLENDENABLE` inside the fade band, and its depth
is already the jittered prepass depth. Admitting a fade-region draw of a
reviewed pair to the route (write RT1/RT2 from its own rows, treat it as opaque
for the sentinel, and keep it out of M while the fade factor is above a
threshold, e.g. ≥ 0.5) gives the resolve exact per-object motion; the
neighbourhood clip absorbs the slow alpha change. Cost: one routed draw per
fading object (frame 11940: 1 of 51 gate-4 draws; run 36 measured 1–3 fade
draws per frame), the same constant upload and MRT as any routed draw, no extra
pass. **Implemented 2026-09-15** as the fade-band arm of gate 4
(`src/proxy/fade_route_core.h`, `X3M_FADE_ROUTE`, default threshold 500
permille of the program's own `g_AlphaValue · saturate(g_FogClip.x −
g_FogClip.y · d)` at the origin distance, a 100 ‰ hysteresis band per node;
RT2 masked, RT1 blended exactly at alpha 1): linear-distance-fade-region.md,
"Fade-band route". Risk: history of a blended surface mixes the background behind it near
the transparent end of the band (hence the threshold), and a moving fading
object with wrong rows would ghost instead of tremble, so the admission must
keep gates 5–6 (scope/history) intact. The cheaper alternative, dropping M for
covered pixels whose 3×3 has no closer routed neighbour and letting policy 2
reproject them at the far plane, is exact only for a static camera and ghosts
on turns; not recommended.

Fixture: `run_linear_distance_fade.py` (fade region + composition mask) plus a
`run_motion_output.py` case in the `production`/`seam` family that draws a
reviewed pair with blend on inside a bound fade region across the 8 jitter
phases and checks the route record (`gate=0 routed=1 jittered=1`, RT1 alpha 1
on covered pixels, M clear there) and a resolved readback whose per-tile shift
is 0 while the raw shift equals Δjitter.

## Run 125: solar-panel arrays shimmer under original shading

Offline diagnosis (no build for the diagnosis, no Wine, no launch) on the
run-41 B snapshot `/tmp/x3-bottleX3-run125` (`session-20260918-070846-216.log`,
378 MB, queried with grep/Python only), screenshot `screenshots/flicker1.png`
(the presented frame at 1280×768, the two marked arrays: the upper-right array
x 700–1000, y 80–320 and the large right array x 900–1270, y 300–560).
Bursts 22624–22631 (the plant at linear-depth lane 117k–153k) and
24486–24493 (the screenshot pose; big array at lane 18.9k, upper array 51k).
Dump kinds in the bursts: `hdr_1_<f>.rgba16f` (the raw FP16 scene before the
write-back — stage 3 keeps the resolved image in its own texture, so no
resolved dump exists; `X3M_TAA_DEBUG=0`), `motion_1_<f>.rgba32f` (RT1),
`depth_1_<f>.rgba32f` (RT2, four lanes: device depth, share, linear depth) and
shadow maps; no reactive-mask dump.

### Mechanism (witnessed): the panel faces are fade-band draws the arm never got to judge

- **The draws.** Every burst frame has 16 gate-4 (`DrawState`) draws of the
  pair `4944d81dfe531b37`/`64bac8bb307eb896` (four station nodes × four draws,
  4752 primitives each, 512² textures on stages 0–2) in exactly the fade-band
  state: Z on, Z-write off, alpha test off, `SRCALPHA/INVSRCALPHA` ADD,
  separate alpha off, RGB mask 7 — `linear_material.cpp`'s `station_fade`
  pair and one of the seven `fade_route::vertex_programs`. The same nodes'
  hull (`ca6bfa4a…`, 40258 primitives) and alpha-tested (`5e0a10fe…`) draws
  route at gate 0. In the near burst all 16 gate-4 draws are the station
  pair; the far burst adds two or three two-primitive
  `494fe349…`/`7c83ed50…` quads (HUD sprites).
- **Coverage.** In RT1/RT2 the panel *faces* carry the fill sentinel
  (alpha −1, depth −1) and only the frames and dividing struts are routed:
  big-array box 64k sentinel vs 32k routed pixels, upper-array box 58.6k vs
  13.4k (`validity_24486.png` in the session scratchpad matches the
  screenshot panel by panel). No opaque draw lies under the faces: the
  blended pass is the only one that paints them.
- **What the resolve does with them.** `X3M_TAA_SENTINEL=auto` selected
  policy 2 on every burst frame (`camera_policy=2`): a sentinel pixel is
  reprojected through the camera path at the far plane. The camera does not
  rotate in either burst (`camera_rotation_deg=0.0000`) but translates 136
  units per frame, so the far-plane path has **zero** flow while the routed
  strut pixels beside the faces move **3.0–3.8 px/frame** (big array, p50 of
  `|previousUV − uv|` over the routed pixels of the box, frames 24487–24493),
  0.4–1.2 px/frame (upper array) and 0.2–0.5 px/frame in the far burst. The
  panel history is therefore fetched 3–4 px (near) or a sub-pixel amount
  (far) off every frame; the neighbourhood clamp turns that into a
  per-frame re-roll of the fine dividing lines — the shimmer. Shadows do not
  enter (run126 toggles agree).
- **Why the arm refused.** The route rows show `fade_permille=0` for all
  sixteen draws although the draw's own constants give fraction 1.0:
  `c39 = (1,0,0,0)` (`g_AlphaValue`), `c41 = (1,0,0,0)` (`g_FogClip`),
  `b0 = 0` → 1000 permille ≥ the default threshold 500. No
  `fade_refused_rect` row exists in the whole log. The arm returned before
  the fraction: `fade_arm_admits` requires `cutout_caps_ == Ready`, and
  `probe_cutout_caps` returned early with `X3M_LINEAR_MATERIALS=0` (the run's
  configuration; `linear_materials=0` on the sun-shadow line, no
  `linear_cutout_device` row in the log). The run-49 fade-band route was
  inert under original shading since it was written; run 51 verified it with
  linear materials on.

### Ranking

(a) confirmed, in its policy-2 form: not "masked current-only" (there is no M
without the composition) but "sentinel → far-plane camera path", missing the
panels' own parallax. (b) and (c) are not needed to explain the symptom and
are not separable in the raw dumps: in the raw FP16 scene the panel faces
(sentinel pixels with luma > 0.12) have a temporal luma standard deviation of
0.0095 (7 % of their mean, 34 % of pixels change by > 10 % frame to frame) in
the near burst and 0.0015 (1.2 %) in the far one, against 0.14 (23 %) on the
routed struts and 0.032 (20 %) on the corvette hull; the raw-shift predictor
correlates with the jitter delta at +0.3 to +0.56 on routed geometry and at
−0.03 on the faces (low-gradient content). Whether the resolved output
re-rolls on the panel texture after the fix is a run-42 question.

### Fix (2026-09-18)

`probe_cutout_caps` runs when either consumer is configured (linear materials
requested or the fade-band threshold ≤ 1000); the cutout arm itself stays
gated on the linear-material request. The admission predicate is unchanged
(`fade_route::state` on a `fade_route::vertex_programs` pair, fraction ≥
threshold); the cutout-miss exemption and the reactive-mask rules are
untouched. Fixture `seam-taa-fade-route-original` (`run_motion_output.py`):
the sentinel script with `X3M_LINEAR_MATERIALS=0`, `X3M_LINEAR_DISTANCE_FADE=0`
on the hover schedule — the verdict must be Ready without linear materials,
the arm routes, holds and refuses the hover frames without a bracket, routed
quads carry their own RT1 rows and the resolved shift stays a fraction of the
raw one; the new `fade_route_frame` log line carries the arm's counters under
original shading (ledger: `docs/verification/motion-output.md`, 2026-09-18;
record `verification/results/bottle-X3/fade-route-original.json`).

Not derived: the texel footprint of the panel texture (the UV mapping is not
in the capture rows), so a mip-bias or per-program reactive tuning stays
unassessed until a run-42 capture with the arm active shows whether any
residual re-roll remains.

## Run 130: one leg still shimmers — the station module's origin is behind the camera

Run 42 B (`/tmp/x3-bottleX3-run130`, run42 candidate `1a5dd46c`, fade route
active, original shading). Plant leg 1 (frames 3501–3508) routes all 16
fade-pair draws (`4944d81dfe531b37`/`64bac8bb307eb896`, `fade_permille=1000`);
leg 2 (5354–5361) routes 13 of 14 and refuses one draw per frame (index 336,
`gate=4 routed=0 matched=0 node=00000000 fade_arm=0 fade_permille=0`), the
residual "one leg" shimmer. The distant-object burst (12801–12808) carries no
fade-pair refusal: its gate-4 rows are 8 HUD sprites and 2 hull draws
(`53a0a641…`/`63f96eba…`) with `zwrite=0` (no depth writer, the opaque chain's
`no_zwrite` step), a different class.

### Mechanism (witnessed): the arm's origin-distance step refuses w ≤ 0

`node=00000000` on the row is not a trace miss: gate 4 precedes scope
sampling, so a gate-4 row never carries the key. The `object_context` row of
the same draw (`index=336 scoped=1 valid=127 node=1d879878 node_handle=25074
camera=2ab2ee78`) shows the trace resolved node, camera and registry on every
frame. Draw 336 is in the exact fade-band state (`blend=1 src=5 dst=6 mask=7
atest=0 zwrite=0`, c39 = c41 = (1, 0, 0, 0): fraction 1 regardless of
distance) like its routed neighbours 334/335; what differs is its clip
translation column: `c27.w = c5eddc74 = −7611.6` (5354) through `−8563.2`
(5361), i.e. the module's origin lies behind the camera plane (the ship is
flying through or past the module; draw 335 has `c27.w = +8837`).
`fade_route::origin_distance` refused `w ≤ 0` ("False for w <= 0"), so
`fade_arm_admits` returned before the fraction was computed
(`fade_permille` stayed 0) and the draw took the plain jittered native path.
Across the whole run: 248 fade-state rows of this pair, 8 at gate 4, and
exactly those 8 have `w ≤ 0`; the 240 with `w > 0` are routed
(`scan_w.py` over the session log). The same class appears in run125 only as
part of its 512/512 refusals (the arm was inert there) and not in run129 or
run131 (0 and 2 fade-state rows, none refused).

The guard was unnecessary: with a valid camera the inversion
`x_v = (x_c − w·m20)/m00`, `y_v = (y_c − w·m21)/m11`, `z_v = w` holds for any
sign of `w`, and the Euclidean origin distance is the same quantity; the
mesh in front of the camera is admitted by its origin exactly as any
straddling mesh (the estimate for this pair is distance-independent anyway).
The cut detector keeps its own `w > 1e-6` sample guard.

### Fix (2026-09-18)

- `fade_route_core.h`: `origin_distance` refuses nonfinite inputs and an
  invalid camera only; `w ≤ 0` yields the distance. Host: `test_fade_region`
  (`w = −1` at distance 1 like `w = 1`, `w = 0` at the camera, off-axis
  `w = ±2` equal, nonfinite refused).
- Every scene `motion_route` row carries `unmatched=<reason>`: the first
  failing step, recorded on the refusal path from values the gate already
  read (gates 1–3 `feature|scene|xt_pair|unregistered|pair`; gate 4 opaque
  chain `user_memory|read_failed|no_zwrite|blended|state|instanced|rows|
  geometry`; a recognised fade-band draw the arm's own step `fade_caps|
  fade_instanced|fade_rows|fade_geometry|fade_constants|fade_origin|
  fade_threshold`; routed rows `scope|history`; matched `none`). No getter,
  lookup or allocation; the name lookup is on the capture log path only.
- Fixture `seam-taa-fade-route-behind` (`motion_output_fade_route_inc.h`):
  the `original` script (original shading, hover schedule over the sentinel
  fill) with the quads' clip rows `diag(2, 2, 2)`, row 3 = (0, 0, 10, −1):
  every vertex keeps `w = 2` (raster, depth .3 and jitter shift identical to
  the identity rows) while the origin's clip is (0, 0, 0, −1), which the
  previous guard refused (the host test's former `w = −1 → ok 0` case). With
  the fix the schedule routes, holds and refuses exactly as `original`
  (capture rows frame 2 `routed=1 fade_permille=449 fade_held=1`, frames 3–4
  `gate=4 unmatched=fade_threshold`; ledger:
  `docs/verification/motion-output.md`).

### The distant object's own class: the hull pair's same-node source-over sub-mesh

The B3 burst's two refused hull draws per frame (`53a0a641107ed76c`/
`63f96eba9eea7880`, `argon2s.fb`, the cutout pair) are not a second pass of
the hull: each is its own vertex buffer in the node's sub-mesh sequence
(run130 frame 12801: node `286daa70` draws vb 6613, 6615, 6617, 6619, 6621
routed, then vb 6623 refused, then 6625 routed; node `2e804920` likewise at
index 535), with its own stage-0..2 textures (1024² DXT5 diffuse, 1024² DXT1,
32²) and the node's cube map at stage 3, drawn in the exact fade-band state
(`state id=7 1, 14 0, 15 0, 27 1, 19 5, 20 6, 171 1, 168 7`: Z on, Z-write
off, alpha test off, SRCALPHA/INVSRCALPHA ADD, mask 7) — the model's
translucent material layer (glass/window panes, the "glowing windows" the
user sees). Census over both logs (`scan_zw0.py`): run131 (run117 station,
frames 4991–4992) 4 such rows, 2 per frame; run130 16 rows, all in B3; every
one immediately after a routed draw of the same node (20/20), always in that
state. No other reviewed pair appears with Z-write off: the remaining
`zwrite=0` scene rows are HUD sprites (`494fe349…`, ONE/ONE, 8/frame),
`d5e1c75351…`, `36f98d15…` and `5e484a06…` (gate 3, unreviewed). The refusal
was the opaque chain's `no_zwrite`, so the layer took the plain jittered
native path and its history resolved current-only — the same trembling as
the fade band's, on the ship's windows. It is distinct from the documented
alpha-tested source-over pass of the cutout pair (`atest=1`,
alpha-tested-materials.md, runs 11/14), which keeps its native path.

Fix (2026-09-18, the overlay arm): a reviewed pair that is not a fade
program, drawn under original shading in the exact fade-band state as the
very next draw after a routed draw of the same node (same frame, adjacent
draw index, node identity at gate 4, lifetime serial at the scope gate; latch
cleared at Reset), is admitted through the fade arm at permille 1000 with no
fraction or hysteresis (its alpha is the material's): own RT1 rows from its
own clip rows, RT2 masked, counted `overlay_routed`/`overlay_refused` on the
`fade_route_frame` and `linear_material_frame` lines, refusals attributed
`unmatched=overlay_node`. The arm is on with `X3M_FADE_ROUTE` ≤ 1000. Fixture
`seam-taa-fade-route-overlay`: the hull pair's quads carry A's scope with
their own vertex buffers right after A, over A, original shading — both
routed every frame (`overlay_routed=2`, `fade_routed=0`), the depth target
unchanged by the draw, worst resolved residual 0.071 px against a raw shift
equal to the jitter; `seam-taa-fade-route-foreign` (the quads on their own
nodes) is refused every frame (`overlay_refused=2`, `unmatched=overlay_node`);
`seam-taa-cutout-blended` (alpha test on) is unchanged.

Remaining, attributed by the new field next run: the HUD sprites
(`494fe349…`, ONE/ONE, no engine node) and any `overlay_node` refusal.
