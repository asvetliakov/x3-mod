# Project status

Updated 2026-09-11. **The iteration-5 user-managed run is complete. Its captured
gameplay frames preserve scene depth, and every scoped draw has consistent
observed storage lifetimes. The installed build remains unchanged while follow-up
source work is reviewed. See [iteration 5](verification/iteration-05.md).
The overall renderer modernization objective is not complete.**
No HDR/TAA/AgX/material/clustered-lighting
visual enhancement is enabled yet. See the [full user objective](user-objective.md)
and [roadmap](architecture/roadmap.md).

## Latest checkpoint

The finite-position source path now includes a reviewed compact classification
core (214,651 optimized and sanitizer checks), exact Preview managed VB/IB
qualification (461 native checks), and an allocation-owned upload observer
(385 native/wrapped checks). The extended draw reader passes 219 checks and 63
caller-state comparisons. It obtains finite XYZ and actual index bounds from
the game's existing writes, with no extra buffer Lock or GPU readback. The
installed iteration-5 DLL is unchanged. All affected regressions, 18 combined-DLL
cases and the forced native fallback pass. See [finite upload evidence](verification/finite-upload-observer.md)
and [review 6](verification/review-06.md).

Detached rigid-motion production now has bounded CPU correspondence (3,210
checks), exact position profiles and a GPU producer (102 numerical samples,
117 checks and 30 state comparisons). The GPU output feeds the production
temporal resolve in verification; perspective checks also pass at 1280×768 and
5120×1440. See [motion history](verification/motion-history.md) and
[GPU motion](verification/rigid-motion.md). These modules are compiled into the
current source build but have no live game callsites.

The [archive position review](reverse-engineering/archive-position-paths.md)
accounts for all 256 VS: 234 homogeneous row-dot paths, 18 direct-clip bloom paths,
two direct-position GUI/effect paths and two particle billboards. The production
registry now contains all 234 row-dot programs, with separate lookup for the other
22 VS and positive-only coverage profiles for 494 PS. One malformed PS is excluded.
The reviewed registry passes 751 actual-program lookups and rejects 547,927
single-word mutation controls. The other five captured
programs require explicit separate handling; they are not excluded from final
TAA/composition scope. Particle RGB blending and missing prior particle identity
are documented in [particle inputs](reverse-engineering/particle-motion-inputs.md).
The full Python analysis suite passes **279 tests**.
The post-install source registry also retains original shader model, constructor
and position-write order, independently verified for all 234 row-dot profiles.
The detached rigid-motion pass now creates the reviewed fixed SM3 replay program
internally and requires a cached exact-source qualification token plus an explicit
finite-position attestation. The 32-profile program passes 1,573,392 covered
component comparisons and 134 bilateral raster/depth cases; independent review
checks its evidence limits. These source changes have not replaced the installed
iteration-5 DLL.

The [live draw-input reader](verification/draw-input.md) passes 219 checks,
63 caller-state comparisons and seven failed-getter controls. It reads exact
submitted rows and actual layouts/revisions, distinguishes nonindexed draws from
an unused bound IB, and keeps lifetime, source qualification and finite-payload
gates independent. Proxy
capture wiring now records these inputs and composes lifetime evidence around the
native draw. The combined fixture checks record scope and failed submission gates.

[Reactive history](verification/reactive-history.md) now owns current/prior mask
snapshots and rejects contaminated RGB independently of alpha. Actual synthetic
particle birth, disappearance, movement, reordering and occlusion pass, along with
policy/reset/failure handling: 98 numeric checks and 102 state comparisons. The
58-sample resolve and 102-sample rigid-motion regressions still pass. Producing
these masks for live game draws remains unfinished.

[Lifecycle disassembly](reverse-engineering/object-lifetimes.md) now covers the
reviewed central insertion, removal, bulk destruction and renderer-load paths.
The opt-in [observer](verification/object-lifetime-observer.md) passes 533 checks
and 72 original backend calls, including baseline adoption, reuse, foreign
unwind, hook ownership loss and retirement. Six runner-provenance tests pass.
The completed iteration-5 run verifies consistent lifetimes for all 12,753 scoped
draws out of 12,957 successful draws. The observer started without an installation
baseline, then obtained useful identities from observed insertions. Load epoch
1→2 distinguishes 17 handles reused with different storage and serials.
Camera cuts remain a separate policy; see [live lifetime evidence](reverse-engineering/iteration05-lifetimes.md).

The installed iteration-5 DLL passed all 15 integration cases and the forced native
fallback with its 15 compiled proxy/renderer objects. Its installed hash is:
SHA256 `ed19a7abf54ae2b9debf912f3d343a0c9217038a2162cb6a9eb174fc8050bbd5`.
The [installation record](../verification/results/iteration-05-install.json) verifies
unchanged game EXE and bottle configuration. The prior 0.4 DLL is preserved in
`artifacts/rollback/d3d9-iteration04.dll`; the older 0.3 rollback also remains intact.
The latest source DLL separately passes 18 integration cases and the forced
native fallback with all 16 current proxy/renderer objects. Its SHA256 is
`6e21f29a57e97018753212759392ef0b79bd9fc120aa5121f30dd62f70ed3083`; it is
**not installed**. See [review 6](verification/review-06.md). The earlier source
checkpoint and its 15-case evidence remain recorded in [review 5](verification/review-05.md)
and commit `437e95b`.

The [detached adjacency cache](verification/mesh-adjacency-cache.md) passes
721 checks using real native mesh acquisition, exact byte keys, bounded storage,
native downstream cleaning/optimization and computational FP-state parity.
Repeated original synthetic meshes show a large hit-time reduction including
acquisition/lookup cost. The iteration-5 run recorded 7,199 gate rejections and
no cache calls or hits.
Static analysis identifies dynamic SYSTEMMEM mesh options excluded by the installed
gate; the narrow four-option extension now passes 12,781 actual native/wrapped checks
and 733 core checks, with 68 + 123 loading regressions and independent review.
It is a source change, not an installed game speedup.
The cache remains behind an off-by-default switch. Independently reviewed
[native/wrapped hook integration](verification/mesh-cache-hook.md) exercises
all four game option variants across six cases.
Known wrapper state stays truthful to the actual cache/native lock path; existing
uncertainty never becomes known through a cache hit. Acquisition
cleanup failure explicitly disables cache admission and rejects only subsequent
preparation calls intercepted by our hooks; restart is required. It never claims
to repair the native lock or contain calls outside those hooks.

The user-run 0.4 session has 20 complete captured frames and 13,431 successful
draws, including a final third-person burst. The then-installed selector rejected all
frames and attempted no depth copy: planet haze was unnecessarily mandatory,
and later ColorFill invalidation masked the first cause. Source corrections
remove the haze requirement while retaining verified background/binding rules,
check scratch-fill targets, and preserve the first rejection. The installed
corrections now report successful pre-clear copies and confirmed
boundaries in all 24 captured iteration-5 gameplay frames, including the different
planet save; the four menu frames remain rejected. This is live copy/epoch/boundary
evidence, not numerical readback of the game depth texture. The
[depth/motion audit](reverse-engineering/iteration05-depth-motion.md) finds 7,202
gameplay draws pass the current local input checks, all before the selected Clear;
finite vertex payload, replay stability and complete scene coverage remain unproved.

New [camera/object evidence](reverse-engineering/iteration04-camera-motion.md)
shows independent object motion with a stationary camera; camera-only history is
insufficient. Four changing unscoped vertex buffers require separate handling.
[Active one-/two-light inputs](reverse-engineering/iteration04-lights.md) are now
observed. [Loading analysis](reverse-engineering/iteration04-loading.md) finds
21.287 seconds of adjacency work and recurring activity, without proving exact
mesh reuse or explaining the entire 87-second presentation gap.

The [full shader sweep](reverse-engineering/shader-sweep.md) covers 751 programs
across all 3,480 effect files; all disassemble and all 57 runtime-dumped programs
match archive bytes. Static review retains unknowns and does not prove runtime
coverage. A detached, reviewed material transformer preserves HDR RGB for five
exact profiles; 42 structural checks and 192 GPU samples pass. It remains
disconnected from game rendering and needs the FP16 scene path.

Our arithmetic uses SSE2 with explicit four-byte incoming stack alignment;
[ABI verification](verification/sse2-abi.md) and object/temporal/material
regressions pass. This leaves ABI-required ST0 transfers intact.
[HDR transfer investigation](architecture/hdr-transfer.md) rejects stock
WineD3D-to-DXMT shared handles as a pixel-sharing route, while a native FP16
IOSurface GPU proof preserves values above one. Wine integration, EDR
presentation and the requested visual features remain unfinished.

## Completed

- Reversible app-local 32-bit D3D9 proxy, loader/export forwarding, identity-preserving
  per-object interception, bounded capture, F8 trigger, shader dumps and hashes.
- Preview-only installer/launcher/rollback tooling; EXE, archives and bottle
  configuration preserved. User test setting: 1280×768 windowed (was 5120×1440
  borderless). No unused launcher is intentionally left open.
- Independent FP16/depth/D3D11-scRGB capabilities; baseline/proxy smoke passes;
  87 analysis tests and compile-time ABI guards pass.
- Static archive/PE analysis, targeted Ghidra renderer map, shader index and CTAB
  register mapping, documented separately in `docs/reverse-engineering/`.
- Animated menu capture: 690 draws; user-assisted flight: two complete 122-draw
  frames. Exact archive matches for all 47 shaders recorded in flight session.

## Iteration 2 additions

- Capture v2: typed I/B/F state, per-device frame keys, resource allocation IDs,
  stream/index metadata, draw parameters/results, and texture/surface relationship.
  Actual synthetic traces verify stateblock restoration and resource recreation.
- Camera factorization: 105 draws/frame fit the same multiplication convention;
  three camera coordinate regimes prohibit a blanket single-camera assumption.
- INTZ numeric rendering/sampling: 16 pixel checks pass across 8-bit/FP16 outputs
  and reset. This is sampleable synthetic depth, not game depth substitution.
- Common shader point-light array decoded as eight pos/color/atten structures.
- User turning capture: eight complete frames, 762 successful draws, 45/45 shader
  matches. Camera convention holds through all six adjacent turns (max residual
  2.67e-7). Unique resource/range candidates include changed world transforms;
  repeated keys and reordered draws prohibit naive object matching.
- All 550 named point-light shader-stage observations have explicit count zero;
  stale float array entries are not active light evidence. See turning-camera.md
  and turning-lights.md in reverse-engineering.
- Baseline lifetime probe proves persistent native resources can prevent device
  teardown. Canonical logical COM ownership is required before depth/history.
- User reports double cursor after alt-tab and requests loading-time investigation.
  Both are tracked; flip presentation has not been shown to fix cursor behavior.

- Instruction inspection of all 11 observed vertex shaders confirms direct matrix
  position paths without positional shader animation. Five material pixel shaders
  clamp vertex lighting/emissive RGB before the target, so FP16 alone is insufficient.
  See `docs/reverse-engineering/position-shaders.md`.

## Concrete next work

1. Prepare the next user-managed diagnostic run of the verified finite POSITION
   producer under the reviewed
   [managed-buffer upload contract](reverse-engineering/managed-buffer-write-mapping.md).
   The new [capture audit](verification/finite-upload-capture.md) reports source,
   finite/index coverage and cumulative cost without treating inputs as TAA eligibility.
   The game's dynamic
   SYSTEMMEM mesh configuration now passes native tests; its actual cache hit rate
   still needs a future run.
2. Connect the verified correspondence and GPU motion modules to lifecycle-safe
   draw records, with reload/reuse generations, camera cuts and geometry revision
   gates. Account separately for CPU-changing particles/stardust and overlays.
3. Validate live jitter placement and position/raster equivalence using the full
   archive registry, then connect matched color/depth/motion inputs to temporal resolve.
   Camera-only reprojection cannot satisfy the observed scene; TAA remains required.
4. Establish the FP16 scene path and enable only reviewed material variants there.
   Integrate a GPU-native HDR presentation route; output conversion of clipped
   8-bit color is insufficient. Continue all remaining roadmap features.
5. Measure exact mesh-key reuse and real acquisition/lookup cost before enabling
   bounded adjacency caching. Investigate the still-unattributed loading gap.
   Revisit native/game cursor behavior with presentation changes.

## Test coordination

The user requested notification for future launches that need more than the menu,
and will handle launching/loading gameplay scenes. Do not repeat autonomous full
game launch attempts. Close any unneeded launcher immediately. Current game can
be left to the user; it contains their chosen test scene. F8 writes captures under
`X3/x3-modern-captures/`. Detailed capture causes a diagnostic hitch by design.

## Useful artifacts

- `verification/results/game-flight-capture-summary.json`
- `verification/results/game-menu-capture-summary.json`
- `verification/results/shader-registers.json`
- `verification/results/graphics-capabilities.txt`
- `verification/results/d3d9-smoke-proxy.txt`
- `docs/verification/iteration-01.md`

Raw shader bytes remain local beside X3. The large generated archive shader index
was `/tmp/x3-shader-index.json`; regenerate with `tools/analysis/index_shaders.py`
if missing. Raw game logs are also beside X3, not redistributed source assets.

The preserved 0.3 rollback DLL is `build/d3d9.dll`, checksum
`71f59c8e6422d5bbf2f55c116e2c0388026d956ba45a3a03eee53c4d85a232a6`. See
`docs/verification/iteration-03.md` for the command, coverage and test evidence.
The user supplied two four-frame turning bursts from 0.2; all captured draws
succeeded and camera/light/motion analysis is complete. The combined 0.3 session
is also complete; the user reported docking during it, without a timestamp that
locates docking within the captured bursts. All 2,914 named point-light count
observations are zero. Camera reconstruction error remains below 1.41e-7.

Texture helpers took 16.382 seconds and inflate 7.109 seconds in observed flushed
totals. An 89.092-second presentation gap remains incompletely attributed;
sampling/disassembly identifies an uncovered mesh adjacency/cleaning/optimization
path. See [loading observations](reverse-engineering/loading-observations.md).
No loading speedup is enabled in the game.

The user confirmed a game cursor and macOS arrow at different positions, persisting
after focus changes. Win32 focus/clipping/hiding restore in the trace, but native
cursor state was not sampled. See [cursor observations](reverse-engineering/cursor-observations.md)
for a scoped synthetic investigation; no cursor fix is deployed.

An independent canonical D3D9 ownership layer passes 370 baseline / 431 wrapped
fixture checks with matching shared HRESULTs and output mutations. It releases
renderer-owned resources before Reset and native device teardown. It is not yet
enabled by default; opt-in 0.4 produced 13,431 successful captured draws, without a terminal teardown summary in that log. See [ownership source](../src/ownership/README.md)
and [verification](../verification/probe/ownership.md). Generated fragments use
`*_inc.h` per the user's editor preference.

The original shader interpolation fixture passes 96/96 numeric samples across
32 cases and Reset. On this backend SM3 COLOR0 preserves values above one into
FP16; SM2 COLOR0 clips before interpolation. Explicit pixel-shader saturation
still clips both paths. See [HDR varying verification](verification/vertex-color-hdr.md).
This supports targeted SM3 material changes once the FP16 scene path exists;
it is not a game HDR implementation.

## Historical 0.4 checkpoint (gameplay analyzed above)

The ownership layer is connected to the loader behind `X3M_OWNERSHIP=1` in the
separate `build-ownership/` build. All 15 actual-DLL integration cases and a forced
adoption-failure fallback pass. Child-induced final device/factory releases now
reach the capture hooks; repeated device address reuse leaves no stale contexts.
Stencil states and depth selection status are included in consolidated capture
diagnostics. See [integration verification](../verification/probe/ownership_integration.md).

The incompatible D24X8-to-INTZ substitution experiment has been removed. The
replacement preserves the original application surface and explicitly copies it
to native D24X8 storage through RESZ. The installed binary investigation and
numeric positive/negative controls establish why D24X8-to-INTZ fails despite a
successful trigger HRESULT. See [RESZ verification](verification/depth-resolve.md)
and [backend investigation](reverse-engineering/depth-resolve-backend.md).
The opt-in `X3M_DEPTH_COPY=1` switch allocates storage and reports diagnostics;
the optional scene adapter invokes the explicit copy before a recognized destructive clear in requested capture frames. The installed rules rejected this game session; successful game depth preservation remains pending. See
[copy verification](verification/copied-depth.md): 634 checks / 32 samples pass,
with 357 additional loss-regression checks across 33 cases.

The bounded original mesh-adjacency reuse fixture passes 1,218 checks and complete
downstream mesh parity. It demonstrates a synthetic speed benefit for repeated
identical meshes, not a game loading improvement; actual repetition/cost remains
unmeasured. See [mesh preparation](verification/mesh-preparation.md).

A standalone temporal resolve shader now passes 58 numeric GPU checks including
camera/object reprojection, disocclusion, HDR preservation, actual jittered
history accumulation and Reset. Independent review caught and corrected a raw
D3D9 viewport half-texel error using a rasterized-geometry regression. See
[temporal resolve](verification/temporal-resolve.md). Camera/object-motion routing,
scene-boundary integration and gameplay TAA remain incomplete.

The native D24X8 snapshot exposes shadow comparisons rather than raw depth. A
separate GPU decoder reconstructs R32F device depth with 26 comparisons per pixel;
precision and cost limits are recorded in [decoder verification](verification/depth-decode.md).
Its isolated timings do not establish frame cost at the user's full resolution.
Independent [code review findings and fixes](verification/review-04.md) are
recorded with the checkpoint evidence.

The consolidated 0.4 diagnostic build was verified and installed for that run, with SHA256
`81e3b121659c0fa1811641a5dbe019f668341477a787c6729fb5a848e056c516`. It includes
an exact-executable engine submission scope, buffer write revisions, scene-depth
preservation during requested captures, and mesh loading timings. The standalone
scene adapter passes 20 scenarios / 2,228 checks / eight samples; buffer tracking
passes 530 checks, and mesh timing passes 68 ABI plus 123 native mesh checks.
Independent reviews found and fixed post-clear query confirmation and hook
recovery/foreign-chain defects. See [review](verification/review-04.md) and the
[completed coordinated run](verification/iteration-04.md).

The detached production temporal runtime has paired FP16 color/R32F depth history,
explicit motion policy, failure-safe publication and caller-state restoration;
44 numeric checks and 40 complete state comparisons pass. It remains disconnected
from game rendering. No camera jitter or motion producer is enabled. Verified
engine handles are not yet lifetime-safe across reload/reuse, and post-bloom color
is not a complete matched color/depth input for whole-frame TAA.

No game was launched by the agent. No visual enhancement has been enabled.
Commit each completed logical checkpoint.
