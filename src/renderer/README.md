# Detached temporal runtime

`TemporalPass` executes the existing depth-comparison decoder and temporal resolve shaders on a borrowed native D3D9 device. It is a reusable GPU module, currently disconnected from the X3 loader, pass selection, scene copying and jitter injection. It does not identify the scene, infer motion, perform a RESZ copy, tonemap, present, or read pixels to the CPU.

## Ownership and calls

Create an explicit `TemporalPass` instance and call `initialize` with the borrowed native device and compiled `ps_3_0` bytecode for `src/temporal/depth_decode.hlsl` and `src/temporal/resolve.hlsl`. Shader creation consumes the bytecode synchronously; the module retains only its created native shaders. There is no compiler dependency in production. There is no device AddRef, application wrapper reference, global registry or ownership cycle.

The caller must hold the native device alive and serialize rendering, reset and teardown. Call `before_reset()` before native Reset and initialize again after a successful Reset. Destroy or `shutdown()` the pass before final native device teardown. All shaders, texture references and surface references are released. Do not let a borrowed output remain in use across `run`, `invalidate`, `before_reset`, shutdown or destruction.

Each `run` accepts scene-linear FP16 color and the already-copied native D24X8 comparison texture at identical dimensions. This format is the explicitly verified CrossOver Preview comparison-sampling route; raw INTZ or R32F inputs must not be passed as the comparison snapshot. Decode writes the next R32F history directly. Resolve then writes the next FP16 color history, sampling the previous pair only when valid. The two native color and two native depth textures are allocated on first use or resize. No application input texture is overwritten.

`FrameInputs` supplies the unjittered clip-to-previous matrix, current/previous raster-pixel jitter, rejection constants and blend weight with the existing [shader ABI](../temporal/README.md). The stable epoch distinguishes camera/scene/resource regimes; it must not increment for ordinary frames or depth clears. `camera_cut`, disabled `history_allowed`, dimension/epoch changes, explicit invalidation, failed input validation, any failed pass, failed restoration, reset and shutdown reject previous history.

Motion policy is mandatory. `KnownCameraOnly` asserts that the producer knows camera reprojection covers the inputs; it is not a fallback for missing dynamic-object correspondence. `PerPixel` requires a same-sized native RGBA32F texture following the shader's exact alpha/UV/depth contract. Missing or unsupported policy fails closed. The runtime cannot verify semantic motion coverage, exposure consistency, correct scene boundaries or matrix provenance.

The output consists of borrowed native FP16 color and R32F depth textures plus a generation and whether previous history was allowed into the resolve. Outputs are published only after the decoder draw, temporal draw, any owned EndScene, and restoration all succeed. The prior index is not advanced on a partial failure; history validity is cleared, so partially written storage cannot be consumed next frame. `used_history` indicates enabled history input, not that every pixel accepted a history tap. It is not a GPU completion guarantee.

## Caller state and failures

The caller explicitly reports whether it already has an open scene and whether a state block is being recorded. It must also set `caller_queries_idle=true` only after positively establishing that no application occlusion/statistics query is active. The default false means unknown/active and is rejected before GPU state changes: injected draws would change query results, which cannot be restored by a state block. Future integration must track query Issue transitions; this module cannot infer them. Recording is rejected before GPU state changes. An existing scene is borrowed without BeginScene or EndScene; otherwise the pass opens and closes its own scene.

Every dispatch captures `D3DSBT_ALL`, all supported render-target slots, the depth surface, viewport and scissor. It clears conflicting texture/RT bindings, disables depth/stencil/blend/alpha/fog/sRGB/clip/scissor and vertex blending, forces solid unculled rasterization and full color writes, resets stream frequencies and TEXCOORD0 wrapping, and binds point/clamp samplers with no mip/sRGB. The fullscreen XYZRHW primitive uses the required -0.5 raster-pixel correction. Restore returns all RT/depth bindings, applies the saved state block, then restores viewport/scissor after target changes. The module uses no application state-block Begin/End calls.

An ordinary dispatch failure still attempts full restoration. A restoration failure returns failure and invalidates both histories. Observed DEVICELOST/DEVICENOTRESET stops ordinary state restoration; the caller must perform its normal device recovery. `Diagnostics` reports the operation and restoration HRESULTs separately. Exact restoration cannot be promised after a backend setter or StateBlock Apply fails. No resource destruction or state manipulation is attempted concurrently with another device user.

## Verification

Run `python3 verification/probe/run_temporal_pass.py`. It freshly compiles the fixture together with the actual production module, loads the production shader files, checks source/executable stability, and runs only the standalone program in CrossOver Preview's Steam bottle with builtin D3D9. Validation-only readback exists in the fixture, never in production.

The fixture compares snapshots of caller MRT/depth, viewport/scissor, all streams and frequencies, indices, declaration/FVF, shaders, 20 texture slots and sampler states, shader constants, touched renderstates, texture-stage states, transforms and clip planes. Hostile sentinels would suppress or alter drawing unless normalized. Numerical cases cover history startup and accumulation, camera cuts, epoch changes, depth disagreement, explicit motion rejection, matrix/jitter/object routing, second-draw failure followed by recovery, and native Reset. The scene-open mode is exercised by requiring the caller's EndScene to succeed after each borrowed-scene run.

This is synthetic verification of a detached module. No scene selection, game motion, jitter placement, HDR display output, temporal image quality or performance claims follow from it. The decoder's 26 comparison fetches per pixel and state-block overhead remain material costs for later measurement.

The verified fixture includes generic restoration failure followed by a synthetic DEVICELOST, checks that loss takes precedence and stops further setters, and rejects unknown motion enum values and unknown/active query state. Synthetic loss injection changes only the fixture vtable; it does not simulate real GPU loss or claim recovery without Reset.

Current checkpoint: **44 numerical checks and 40 complete fixture state comparisons passed across two pure-device generations with native Reset**, including output-as-input alias refusal. The source and executable remained unchanged throughout the fresh-build run. Exact hashes, command and raw report hash are in `verification/results/temporal-pass-summary.json`; output is `temporal-pass.txt`.

Production implementation SHA256: `b1ccd1bbc8d398c4a39f1f94980f83faa5a16c883eca20cd1d9a8ef1e52d1f5e`.
Fixture executable SHA256: `8e3403ba098f50b5800250e9a3f724c5bda26288999d7c6eec2270d9902b1f8b`.
