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
