# RESZ depth-copy verification on Preview

**RESZ can copy ordinary D24X8 to a separate D24X8 texture while keeping the
original surface bound and unchanged. That texture is comparison-sampled on
this backend, not directly readable as raw depth by the tested `tex2D` path.**
D24X8→INTZ does not copy: the initialized destination remains unchanged even
though the capability query and trigger return success.

The original standalone [probe](../../verification/probe/depth_resolve.cpp) and
[runner](../../verification/probe/run_depth_resolve.py) use CrossOver Preview,
Steam bottle, process-local `d3d9=b`, a hidden window and synthetic geometry.
They do not launch X3, install anything or change bottle settings.

## Trigger contract

The capability FourCC `MAKEFOURCC('R','E','S','Z')` is **not** the value to submit
as POINTSIZE. The actual trigger is `0x7fa05000`. Wine defines these separately,
and its device implementation resolves the current depth surface into texture
stage zero when that POINTSIZE value is set. [Wine trigger definitions](https://github.com/wine-mirror/wine/blob/wine-10.10/dlls/d3d9/d3d9_private.h),
[Wine resolve implementation](https://github.com/wine-mirror/wine/blob/wine-10.10/dlls/d3d9/device.c).

The original contract-following variant binds the destination texture at sampler zero, makes a dummy draw with
color/depth writes disabled to flush binding state, triggers the resolve, and
restores POINTSIZE and the prior sampler texture. A separate **no-dummy-draw**
variant also passes 108 comparison samples and 18 state checks plus Reset on
this installed backend. Thus its minimal verified sequence is bind destination,
set POINTSIZE trigger, restore POINTSIZE, restore sampler zero; no geometry or
stream state need be disturbed. The ordinary source depth
surface stays bound throughout. Pointer identity, descriptor format, restored
texture identity and exact POINTSIZE bits are checked. The source is then cleared
to 0.9375 before any destination sampling, testing copy independence.

The installed-binary investigation independently identifies a depth/stencil
presence compatibility rejection for D24X8→INTZ. A successful D3D9 render-state
setter does not propagate that internal copy failure. Runtime numeric tests
below are therefore required even when RESZ capability is advertised.

## Numeric outcomes

Every destination is first poisoned to depth 0.125. Source geometry produces
0.25 and 0.75, with a farther overlapping triangle rejected by normal depth
comparison. Other regions retain clear depth 1. Additional triangles use
0.9999 and 0.99999. The sampler is point-filtered with no mipmapping or sRGB.

| Source → destination | Sampling | Result |
| --- | --- | --- |
| D24S8 → INTZ | Raw depth | **36/36 samples**, six state checks and Reset pass |
| D24X8 → INTZ | Raw depth | **0/6 samples**; destination remains poison 0.125; state checks pass |
| D24X8 → DF24 | Raw depth | Destination CreateTexture fails `8876086c`; no copy claim |
| D24X8 → D24X8 | HLSL `tex2D`, float2 UV | Returns 1, fails known 0.25/0.75; not raw depth |
| D24X8 → D24X8 | Explicit comparison-reference coordinate, with dummy draw | **108/108 samples**, 18 state checks and Reset pass |
| D24X8 → D24X8 | Same comparisons, **no dummy draw** | **108/108 samples**, 18 state checks and Reset pass |
| D24S8 → INTZ, incorrect FourCC trigger | Raw depth | **0/6 samples**, unchanged poison; validates trigger negative control |

The source/destination matching positive tests run with RGBA8, RGBA16F and R32F
color outputs, then repeat after releasing resources and resetting the device.
Failure cases stop after their first numeric or creation failure; they do not
claim reset coverage. Their passing clear-region samples are incidental and do
not make the raw D24X8 path usable.

For the compatible D24S8→INTZ path, R32F readback preserves the submitted far-depth
float values `0.999899983` and `0.999989986` in both generations, within tolerance
`1.2e-7` (approximately two D24 steps). FP16/RGBA8 cannot establish that precision.
This is a sampled precision check at chosen depths, not an exhaustive proof for
every 24-bit depth value.

## Native D24X8 comparison semantics

The original assembly pixel shader places a reference value in the third texture
coordinate before `texld`. References 0.1, 0.5 and 0.9 produce the expected
`reference <= stored_depth` comparisons. With reference 0.5, the copied 0.25
region reads 0 and the copied 0.75 region reads 1. At reference 0.9 both read 0;
this remains true after the original source has been cleared to 0.9375. Thus a
real independent copy exists. The earlier all-1 result from a float2 lookup was
consistent with a zero comparison reference, not proof of a failed native copy.

This comparison result is **not a raw depth texture** suitable for ordinary
reprojection sampling. A raw-fetch switch or an explicit GPU comparison decoder
needs independent validation before integrating this route into TAA. The subsequent
[GPU decoder fixture](depth-decode.md) supplies bounded R32F precision, exact
endpoint, inside-BeginScene copy and cost evidence for that fallback. The existing
D24X8 game surface's format and identity can remain intact; changing it to D24S8
merely to satisfy INTZ copy compatibility would be a separate compatibility change.

## Reproduction and evidence

```sh
python3 verification/probe/run_depth_resolve.py --case D24S8_INTZ
python3 verification/probe/run_depth_resolve.py --case D24X8_INTZ
python3 verification/probe/run_depth_resolve.py --case D24X8_DF24
python3 verification/probe/run_depth_resolve.py --case D24X8_D24X8
python3 verification/probe/run_depth_resolve.py --case D24X8_SHADOW
python3 verification/probe/run_depth_resolve.py --case D24X8_SHADOW_NODUMMY
python3 verification/probe/run_depth_resolve.py --case D24S8_FOURCC
```

Each case writes `verification/results/depth-resolve-<source>-<destination>.txt`,
`-wine.log` and `-summary.json`. The comparison case uses `d24x8-shadow`, and the
no-dummy case uses `d24x8-shadow-nodummy`, and the negative trigger control uses
`d24s8-fourcc`. Summaries record command, source and
executable hashes, return status and check counts. Each invocation automatically
rebuilds the executable and verifies fixture/build-script/runner hashes before and
after compilation, then verifies those inputs and executable again after execution.
A failed or changing build cannot reuse stale evidence. The retained seven cases
were rerun with this guard and all report stable inputs/executables; their numeric
outcomes remain unchanged. Offline
[runner tests](../../verification/analysis/test_depth_runner_provenance.py) cover
successful fresh build, failed build with an existing stale executable, source
mutation during build/run and executable mutation during run.
Negative cases intentionally
return exit 1. Earlier exploratory outputs were moved to local
`/tmp/x3-depth-resolve-early/`; the retained case reports all correspond to the
current parameterized probe source.
