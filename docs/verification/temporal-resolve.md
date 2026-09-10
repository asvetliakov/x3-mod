# Original temporal resolve GPU verification

The production [resolve shader](../../src/temporal/resolve.hlsl) and
[CPU parameter/history contract](../../src/temporal/resolve.h) pass **58 of 58
numeric GPU readback checks**, across two complete resource generations separated
by successful device Reset. This is an implemented temporal reconstruction
algorithm verified in isolation. It is **not yet connected to X3** and does not
establish actual game motion vectors, scene-depth routing, jitter eligibility or
final TAA visual quality.

The original fixture uses a hidden standalone 16×16 D3D9 pure device in CrossOver
Preview's Steam bottle with process-local `d3d9=b`. It compiles the production
`resolve.hlsl` directly using the local D3DX compiler, uploads synthetic FP16 color, RGBA32F motion
and R32F depth, executes the SM3 shader into FP16 targets, and reads numerical
results. No game process is launched, no game shader bytes are used, and no
installation/settings are changed. Full parameter/sampler/pixel-center contracts
are in [the source README](../../src/temporal/README.md).

## Numeric checks in each generation

| Case | Expected RGB | What the input distinguishes |
| --- | --- | --- |
| Raw viewport geometry, center/neighbor | 0.5 / 0.53125 | Unadjusted clip-space plane at x2 zoom is rasterized by D3D9; sample positions come from the rasterizer |
| Zoom with rasterized history | 0.375 | Current center 0.25 reprojects to actual GPU history at optical center 0.5 |
| Zoom / 90° rotation optical center | 0.5 | Raw center pixel remains center; textured-UV half-texel errors move it |
| Static accumulation | 0.5 | Current 0.25 plus matching history 0.75 at weight 0.5 |
| Invalid history | 0.25 | Explicit invalidation suppresses the old sample |
| Disocclusion | 0.25 | Current depth 0.5, previous 0.25 rejects history |
| Camera one-pixel shift | 0.5625 | Matrix selects previous x+1 (0.875), not current x (0.25) |
| Perspective previous depth | 0.5625 | Previous W=2, expected previous depth=0.25, offset selects x+1 |
| Mixed-depth footprint | 0.5 | Accept surviving history 0.75 and reject occluded half; rejecting all would incorrectly give 0.25 |
| Out-of-bounds / behind-camera | 0.25 | Invalid projected UV/W rejects history |
| Object motion | 0.5625 | Explicit previous UV and object previous depth override camera path |
| Full precision object depth | 0.5625 | Expected/previous depth 0.5002 matches through RGBA32F; FP16 would quantize to 0.5 and reject |
| Invalid object sentinel / reserved state | 0.25 | Unknown correspondence and noncontract motion states reject history |
| Explicit static fallback | 0.5 | Motion state zero selects camera accumulation, distinct from rejecting history |
| Motion plus previous jitter | 0.5625 | Object UV excludes jitter; previous +0.25 pixel is applied once |
| Neighborhood clipping | 0.625 | History 8 clips to current neighborhood maximum 1 before blending |
| HDR preservation | 5 | Current 2, history 8, neighborhood up to 16; no saturation |
| Invalid history color | 0.25 | NaN old color cannot poison output |
| Invalid current color | 0 | Infinite current color falls back to finite black |
| Invalid current/previous depth | 0.25 | NaN depth prevents accumulation |
| Camera jitter signs | 0.5625 | Current -0.5 and previous +0.5 raster-pixel jitters select x+1 |
| Static jittered coverage, frames 14/15 | 0.4375 / 0.5625 | GPU history reduces alternating raw 0/1 aliasing around analytic coverage 0.5 |
| New epoch | 0.25 | Old ping-pong history cannot leak into a new scene epoch |

Each numeric check verifies all three color channels and alpha one. Ordinary
tolerance is 0.002, HDR tolerance 0.01. The coverage test independently compares
each result to analytic 0.5 with tolerance 0.07; its measured deviation is 0.0625
versus raw deviation 0.5. This is evidence of actual temporal accumulation of
subpixel-jittered samples, not just a shader compile or single-frame spatial
filter. It is not a universal TAA-quality metric.

The coverage sequence uses a static binary stripe signal with boundaries passing
through raster sample centers. Alternating ±0.25-pixel raster jitter yields raw
0/1 values. Sixteen resolves ping-pong real GPU history textures with history
weight 0.875; CPU code supplies no filtered replacement history. Analytic pixel
coverage is 0.5. Camera/motion cases use separately hand-specified expected
results rather than running a CPU copy of the shader as the oracle.

CPU checks independently exercise FP16 rounding carry (values just below 2 and
8) and nearest-even tie behavior. They also reject NaN weight and invalidate on
resize, explicit reset and
new scene epoch. The fixture unbinds/releases all default-pool objects, executes
actual D3D9 Reset, recreates shader resources and repeats every numeric case.

## Reproduce

```sh
python3 verification/probe/temporal_run.py
```

The runner freshly compiles the fixture before each execution, verifies source
hashes match before/after compilation and execution, and checks the executable
hash after the run. This prevents reporting current source hashes for a stale
binary. The build passes `-Wall -Wextra` without warnings. The runner has a 60-second
process timeout and records source and executable SHA-256, process command,
exit status, sample counts and reset result. Evidence:

- [Numeric results](../../verification/results/temporal-resolve.txt)
- [Metadata](../../verification/results/temporal-resolve-summary.json)
- [Wine diagnostics](../../verification/results/temporal-resolve-wine.log)

The backend reports NVIDIA GeForce 8800 GTX/nvd3dum.dll through Wine's adapter
interface; this is an emulated capability description, not identification of the
physical Mac GPU. The current fixture does not measure game performance. Depth
rejection uses configurable raw device-depth tolerance; distant geometry needs
real camera/depth validation before adopting defaults. Renderer integration must
supply accurate previous object correspondence, separate camera/depth epochs,
matching jittered inputs and safe resource/state ownership.

## Review regression: camera half-texel conversion

Independent review identified that the original shader used texture-center UV
directly in raw camera reprojection. Identity and translation fixtures canceled
the missing offset and passed. The expanded geometry fixture was deliberately
run against that original shader first: D3D9 rasterized optical-center history
near 0.5, but its zoomed reprojection resolved **0.382568 versus expected 0.375**,
exceeding tolerance 0.002 and failing the test. The corrected shader subtracts a
half texel before raw VP inversion and restores it after prior projection.
Additional x2 zoom and 90-degree roll tests keep raster pixel (8,8) fixed as the
raw optical center. The separate object-motion input contract already contains
texture-center UV, so it adds only prior raster jitter.

[Deliberately failing regression output](../../verification/results/temporal-viewport-regression.txt)
and [source/executable hashes](../../verification/results/temporal-viewport-regression.json)
preserve this negative proof. Final passing results use the corrected source.
