# Scene-boundary adapter GPU integration

The original standalone fixture passes **36 scenarios, 4,908 checks and sixteen
numeric GPU samples** through the production `SceneCapture` adapter, ownership
layer, scene selector, resource identities and depth decoder. Eighteen scenarios run
on both ordinary and pure D3D9 devices in CrossOver Preview. This verifies the
adapter in isolation; it does not load the proxy DLL into X3 or prove the game's
future scene classification, jitter or TAA behavior.

The proxy switch is `X3M_SCENE_DEPTH_CAPTURE=1`, or launcher
`--scene-depth-capture --ownership --depth-copy`. The adapter runs only in
requested capture frames, including the scheduled capture interval and F8 bursts.
Outside those frames its callbacks do not query rendering state or copy depth.
The optional device hooks reject UpdateSurface, UpdateTexture, DrawRectPatch and
DrawTriPatch rather than leaving their writes unobserved. ColorFill is an explicit
event: only a successful, known, single-sample A8R8G8B8 texture surface distinct
from the main color and original depth by both surface and container identity is
accepted, and only between the scene depth unbind and main-color StretchRect.
Standalone/unknown targets and all other phases fail closed. Full and partial
rectangles are safe only under that distinct-target proof; target/rectangle
metadata is logged for the next capture.
Resource CPU writes through LockRect/GetDC and swap-chain Present remain outside
this adapter. Its coverage is the supported device submission path, not every
possible D3D9 content mutation or presentation route.

## What executes

[The fixture](../../verification/probe/scene_capture_fixture.cpp) compiles original
passthrough vertex and background/material/bloom pixel shaders. Their actual
compiled FNV hashes populate a copied `SceneSignatures` profile. During draws,
the production adapter retrieves and hashes the actual bound shader bytecode.
No game hashes are substituted for fixture shader data. Production's default
profile remains unchanged.

`x3m::log` is replaced to collect diagnostics. The post-Clear query regression
also temporarily replaces one native vtable entry on its standalone test device,
as described below. The build links the actual
`scene_capture.cpp`, `capture_state.cpp` allocation IDs, and ownership code.
Original D3D9 objects and calls execute on the real backend. The fixture manually
pairs adapter before/after callbacks around these calls; it does not compile
`capture.cpp` or verify its complete vtable-hook wiring.

The positive sequence is:

1. Full color/depth clear and five verified primary-background draws. The fixture
   profile has no haze entry: haze is an allowed background family, not mandatory.
2. Separate depth clear, scene geometry with depths 0.25 and 0.75 and an occluded
   far triangle. These are actual rasterized original triangles.
3. Unbind depth, optionally perform three actual scratch ColorFill calls (including
   a partial rectangle), and copy main color into a full-size texture.
4. Four bloom draws with bound inputs copy/A/B/A and targets A/B/A/main.
   A and B are distinct half-size textures; the shaders actually sample them.
5. Rebind the original D24X8 depth, then call `before_clear`. The adapter selects
   the candidate and invokes the production RESZ copy before the real Clear.
6. Clear the original source depth to one, complete `after_clear`, decode the
   private snapshot using the production HLSL, and read numerical R32F results.
7. Present and finish the frame. Exactly one reported successful copy, boundary
   confirmation and frame-end confirmation are required.

The preserved snapshot reads 0.25, 0.75 and clear-depth one **after** destruction
of the original scene depth. Sampling the live cleared source, skipping the
copy, or retaining an uninitialized output would fail. Ownership's source clear
counter must be exactly one ahead of the preserved copy counter. All four successful
frames independently produce four numeric samples, with tolerance 1.3e-7.

## Failure and inactive cases

| Scenario, repeated on both device types | Evidence required |
| --- | --- |
| Unsupported operation before final Clear | Adapter invalidates; no copy diagnostic and native copy view stays invalid |
| Failed full-color copy | Real StretchRect with an out-of-bounds source rectangle fails; no depth copy or confirmation |
| Failed final Clear callback | A real Clear executes, but an explicitly injected failure HRESULT reaches `after_clear`; preserved candidate is never published as a boundary |
| Depth copy rejected during stateblock recording | Actual ownership copy fails with recording active; successful Clear cannot turn that into confirmed history |
| Resource generation retirement | A real held-resource Reset fails and retires/advances the already-copied storage generation; even injected successful Clear/Present callbacks cannot confirm the retired snapshot |
| Draw failure after selection | Actual indexed draw without an index buffer fails after a valid copied/selected boundary; final confirmation is cleared |
| Present failure after selection | Injected DEVICELOST callback after a successful native Present clears final confirmation |
| Post-Clear source-binding query failure | A test-only native GetDepthStencilSurface E_FAIL occurs only after successful Clear bookkeeping; valid storage/epochs cannot compensate for unavailable source-query evidence |
| Main/depth/unknown/failed/wrong-phase/standalone ColorFill | Real fills or explicitly labeled callback/query faults never cause a depth copy; the first rejecting ColorFill is logged |
| Earlier invalid initial Clear followed by unsupported operation | First Pattern rejection remains event 1; exactly one first-rejection record is emitted |
| Outside requested capture frames | No adapter frame-end/copy records and private copy content remains invalid |

Failed ColorFill callback tests pass a null target and an unreadable RECT sentinel
after a separate valid backend fill. These invalid arguments never reach D3D; the
adapter must reject/log the failure without querying either argument. The proxy
hook similarly logs only pointer values/result on failure. The failed StretchRect
case first obtains a real failure using valid surfaces and an out-of-bounds RECT,
then gives unreadable source/destination surface sentinels only to the adapter
callback. Failed copies must not query these surface arguments either; the actual
proxy StretchRect hook already guards its surface queries behind success.

The Clear, Present and post-retirement success HRESULT injections are **adapter
callback fault tests**, explicitly marked `INJECT` in the output. They do not
claim the native API returned those injected results. The generation case omits
the normal earlier Reset-hook invalidation deliberately, challenging the adapter's
own borrowed-view/generation checks. The actual failed Reset also leaves the
backend unable to Present; injecting callback success isolates rejection from
that ordinary failed-Present route. No backend D3D implementation is replaced wholesale. The separate query regression
patches only GetDepthStencilSurface during one adapter callback.

A boundary can be reported as confirmed immediately after its Clear and later
be invalidated by a subsequent failed draw or Present. The fixture specifically
checks the final `phase=end confirmed=0` in these cases, while requiring the
initial successful copy/boundary so a never-selected frame cannot pass them.

The source clears, shaders and geometry are original synthetic assets. Each
case releases all fixture children and checks final logical device release.
This is not a separate proof of native GPU destruction; ownership lifetime
verification covers that responsibility. No game launch, installation, registry
changes or user scene loading occurs.

## Reproduction and provenance

```sh
python3 verification/probe/run_scene_capture.py
```

The runner freshly builds with `-msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2`
and `-Wall -Wextra -Werror`, hashes every compiled
production/fixture source plus runtime decoder before/after compilation and
execution, and verifies the executable hash remains unchanged. It requires all
36 distinct scenario/device combinations, all sixteen samples, successful process
exit and no failing check. The process has a 90-second timeout.

- [Scenario and numeric output](../../verification/results/scene-capture.txt)
- [Source/executable provenance](../../verification/results/scene-capture-summary.json)
- [Backend diagnostics](../../verification/results/scene-capture-wine.log)

The revised background gate is supported by the 0.4 gameplay trace: its five
verified background draws do not include planet haze. That trace's ColorFill
targets were not recorded, so the safe-target condition cannot be proven
retrospectively and no claim is made that those actual frames would now pass.
A future capture-only build must collect the new target evidence. The separate
portable tests retain all existing full-clear/depth/writer/bloom constraints;
this integration test proves that real COM bindings, shader hashes, production
callbacks and depth preservation connect correctly for the modeled sequence.

## Post-Clear binding-query regression

The recognized-wrapper `get_copy_depth_view` API returns S_OK even if its internal
source-binding query fails; its returned `status` and `source_bound` carry that
failure. The adapter therefore requires both a successful returned status and
confirmed source binding before publishing a boundary.

On both device types, the regression completes a real successful scene copy and
destructive Clear, checks otherwise-valid storage/generation/copy/clear epochs,
then temporarily redirects **only native GetDepthStencilSurface** to ordinary
E_FAIL during `after_clear`. Exactly one injected query is required. The original
native vtable is restored immediately afterward. A further real view query must
prove the snapshot remains allocated/valid with identical epochs and generation,
and the native source query succeeds again. Nevertheless, the adapter's boundary
and final confirmation must remain false. This distinguishes rejection of missing
post-call evidence from a trivial failed Clear, retired resource, or stale epoch.
