# Original temporal resolve GPU verification

The production [resolve shader](../../src/temporal/resolve.hlsl) and
[CPU parameter/history contract](../../src/temporal/resolve.h) pass **58 of 58
numeric GPU readback checks**, across two complete resource generations separated
by successful device Reset. This is an implemented temporal reconstruction
algorithm verified in isolation. It is **not yet connected to X3** and does not
establish actual game motion vectors, scene-depth routing, jitter eligibility or
final TAA visual quality.

The current shader also supports current/previous reactive RGB coverage. Its
owned-history and original particle-blending tests are documented separately in
[reactive-history verification](reactive-history.md). This 58-sample fixture
explicitly exercises scenes without reactive contributors and remains passing
against the extended shader.

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

## TemporalPass route-input cases (step 2)

The production runtime fixture (`verification/probe/temporal_pass_fixture.cpp`,
runner `run_temporal_pass.py`) now passes **154 numerical checks and 158
complete state comparisons** (142 `SAMPLE` lines) across two device
generations; the earlier 98/102 regression cases are unchanged inside it. The
new cases exercise the inputs the live route provides at the pre-bloom copy
point, on the same hidden 16x16 pure device:

| Case | Result | What it establishes |
| --- | --- | --- |
| 8-bit main surface copy | all 768 RGB samples within one FP16 ulp of `v/255`, alpha one | A plain `CreateRenderTarget` A8R8G8B8 surface (not a texture level) filled with every 8-bit code is copied by `StretchRect` into the FP16 scratch under hostile sRGB sampler/write states; the backend truncates (421 of 768 also match round-to-nearest); max absolute error 0.000486 < 2^-11; no gamma curve |
| Copy path equals FP16 path | bit-exact color and depth, first frame and accumulation | An FP16 texture holding the detected conversion of the same bytes resolves to identical bits through a second `TemporalPass` instance |
| Copy-back round trip | exact bytes | `StretchRect` of `Output::color_surface` back into the 8-bit target restores the original 256 pixels exactly; `color_surface` is level 0 of `Output::color` |
| Input exclusivity | `E_INVALIDARG` | Both or neither color inputs, both or neither depth inputs, resolved surface or owned depth history as input, wrong depth format |
| R32F depth equals decoded D24X8 | bit-exact color; depth max error 2.98e-8 (half a D24 step) | One synthetic scene (clear 0.5, quad 0.25 shrinking between frames) rasterized into the D24X8 snapshot and modeled into R32F; outside/inside stable regions blend to 0.5, the uncovered region rejects to 0.25 |
| Depth-sentinel reactive | 0.25 / -1 / 0.5 / 0.25 / 0.375 | Sentinel pixel resolves current-only and its -1 reaches the depth history; the opaque neighbor accumulates; a motion correspondence onto a previous sentinel tap contributes nothing; a correspondence onto opaque history keeps accumulating; the policy establishes history without a mask; it refuses the D24X8 input; transitions to and from it invalidate |
| Route cut | current-only, then resumes | `cut=true` rejects history for that frame only |
| Motion-path jitter | 0.625 / 0.625 / 0.25 / 0.25 / 0.625 | Previous jitter +1 with RG at the pixel's own center selects the neighbor once; adding current jitter changes nothing on the motion path; current jitter alone leaves the motion path in place while it moves the camera path; RG that already subtracted the previous jitter cancels it |
| Reset continuity | history rebuilt without `initialize` | One pass accumulates, `before_reset` pends and refuses runs, `after_reset(E_FAIL)` keeps refusing, a real device `Reset` then `after_reset(S_OK)` resumes with invalid history and accumulates again |

Evidence: [report](../../verification/results/temporal-pass.txt) and
[summary](../../verification/results/temporal-pass-summary.json). The
conversion-rule detection is printed as `COPY rule=...`; the D24 comparison
as `DEPTH decoded_vs_r32f_max_error=...`.

Step 3 changed the pass without changing these counts: every device call goes
through numbered vtable slots (the route hands it the original table; this
fixture passes none, so its vtable-swapping fault injection still reaches the
pass), one `D3DSBT_ALL` state block is created per device generation and
captured/applied per run instead of created per run, the decoder bytecode is
optional, and the resolve's output alpha is the current color's alpha (this
fixture's inputs carry alpha one, so its samples are unchanged). The suite was
rerun after these changes with the same 154/158 result; the route-side
evidence is in [motion output](motion-output.md#temporal-resolve-step-3).
Review 16 added stage 0's `D3DTSS_TEXCOORDINDEX = 0` and
`D3DTSS_TEXTURETRANSFORMFLAGS = D3DTTFF_DISABLE` to `normalize`: the fixture's
hostile state now sets a texture transform (scale 0.5, offset 0.25) with
`D3DTTFF_COUNT2` and coordinate index 1 on stage 0 before every run, and
without the reset the Preview backend applied them to the pre-transformed
resolve quad (`matrix routing` sampled 0.25 instead of 0.625); the counts
are unchanged at 154/158 and the state comparison (texture stage states of
stages 0-7 and `D3DTS_TEXTURE0`) proves the hostile values come back.

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
