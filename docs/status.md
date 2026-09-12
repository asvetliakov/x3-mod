# Project status

Updated 2026-09-12. **Direction change: per-pixel motion now comes from the
game's own material draws through transformed shader variants writing a second
render target, not from deferred geometry replay.** The replay, admission,
execution-scope and geometry-lease modules stay in the tree as a numerical
reference and are no longer a prerequisite for any visual feature. See the
[live motion route](architecture/live-motion-route.md).
The overall renderer modernization objective is not complete: no HDR, TAA, AgX,
material or clustered-lighting enhancement is visible in the game yet. See the
[full user objective](user-objective.md) and [roadmap](architecture/roadmap.md).
Native Windows/Direct3D remains a required target alongside CrossOver Preview;
tests still run only on CrossOver. See
[portability requirements and gaps](architecture/platform-portability.md).

## Latest checkpoint: live same-draw motion route (source, not installed)

The proxy can now, behind the off-by-default `X3M_MOTION_OUTPUT=1` switch,
substitute the reviewed Argon SM3 pair with its motion variant during the main
scene, bind an owned RGBA32F motion target as RT1, supply the previous frame's
submitted WVP rows from a cross-frame history keyed by node/camera lifetime
serials plus buffer identity and draw range, and restore all touched state
after each draw. The motion target is filled with the invalid sentinel at the
frame's latching Clear, released before Reset and on device release, and read
back in requested capture frames as `motion_<device>_<frame>.rgba32f`. See the
[implementation section](architecture/live-motion-route.md) for the exact
hooked slots, gates, restoration list and failure behavior, and
[motion output verification](verification/motion-output.md).

Evidence at this checkpoint:

- Synthetic actual-DLL fixture: color bit-identical with the route off and on,
  39/39 full state restorations with zero differences, 44,284 motion pixels
  against a CPU oracle with 10,261 matched (max 0.0016 px UV, 3.7e-8 depth),
  Reset and shader recreation, sentinel-only mode without object scope, and
  application writes to the reserved constants restored.
- History key validation on the 24 iteration-5 gameplay frames: the full key
  matches 99.97% of keyable scene draws across 18 adjacent frame pairs with no
  in-frame duplicates; dropping buffer identity leaves 174 ambiguous sub-mesh
  splits. See [motion history key](reverse-engineering/motion-history-key.md).
- Shader coverage is archive-wide: all 3,480 effect files were parsed for
  their 817 VS/PS pairings, and the transformer table now holds 169 of the
  180 SM3 pairs (56 class A, 101 B, 12 C, including asteroid, moon and planet
  haze programs with spaced position quads). The 11 unsupported SM3 pairs are
  bloom quads and two compare-branch damage shaders. All 169 rows pass the
  structural fixture (1,551,936 mutations) and the GPU fixture with color
  identical. Of 466 SM2 pairs, 115 could host a ps_2_0 fragment and 267 a
  ps_2_x one; SM1 has no MRT. Pair lookup is a binary search over sorted
  index tables; the constant shadow captures every matrix window the table
  names. See [motion output profiles](reverse-engineering/motion-output-profiles.md),
  [material motion](verification/material-motion.md) and reviews
  [13](verification/review-13.md), [14](verification/review-14.md),
  [15](verification/review-15.md).
- **First gameplay run ([iteration 6](verification/iteration-06.md))**: across
  several sectors and a ship kill, 17,390 routed draws with 99.5% matched
  history, zero apply/restore/fill/Reset failures, lock time 0.34% of wall.
  All 1,022,880 valid readback pixels are explained by the stored row pairs
  at 0.149 px maximum error, and an independent origin cross-check agrees in
  sign on every axis (magnitude ratio 1.0006). The main scene is 92.7% SM3;
  sub-SM3 draws there are depth-only or blended, so no shader rewrite is
  needed. The selector rejected 47% of captured frames on unseen background
  pairs and null-PS depth passes; it is now structural (background = draws
  before the scene's depth-only Clear, null PS tolerated) and replays 68/68
  iteration-6 and 24/4 iteration-5 frames correctly. Load/registry epochs did
  not advance across sector changes, so the temporal design gained a
  displacement-based cut detector.
- The motion-output fixture now runs in 16 environments including the
  ownership wrapper, depth copy and admission, which the gameplay run needs.
  That coverage found and fixed a refcount defect that would have leaked the
  device under the wrapper. See [motion output](verification/motion-output.md).
- [Motion readback analyzer](verification/motion-readback.md) checks capture
  readbacks without geometry: integrity, static consistency, row-pair
  consistency (3,402/3,402 fixture pixels explained at 0.0018 px), displacement
  statistics and temporal cross-checks. The depth comparison stays unavailable
  until the route writes a depth image.
- [Temporal integration design](architecture/temporal-integration.md): resolve
  at the pre-bloom copy point, current depth from a third R32F target written by
  the variants, reactive coverage derived from its sentinel, per-draw explicit
  jitter because the game's state manager skips repeated uploads.
- [Constant upload disassembly](reverse-engineering/constant-uploads.md): all
  game shader/constant setters come from its two D3DX effect state managers in
  BeginPass; no game code writes the reserved constant ranges; the pure-device
  manager memoizes the last shader pointer, so restoring VS/PS after a routed
  draw is mandatory.
- [Review 12](verification/review-12.md) fixed a possible terminate in a
  noexcept readback path, hook installation for refused devices and heavy
  FP-state saving on every constant setter; a new static checker proves the
  light hook path reaches no x87 instruction. Existing suites still pass:
  26 ownership integration cases, the fallback link, material-motion structure
  and GPU fixtures, and 388 Python analysis tests.

This is CPU/synthetic evidence. The route has not run in the game, its per-draw
cost in gameplay is unmeasured, and no temporal consumer reads the output.

**Installed for the user-managed run (2026-09-12):** `build/d3d9.dll` from
commit `66d91a4`, SHA256
`fb08b324ea8ad6303ab40e346957c8fcfeeeb996a47c679cb12b6b721191b077`, through
`tools/manage.py install` with bottle configuration unchanged. The previous
iteration-5 DLL (`ed19a7ab…`) is preserved as
`artifacts/rollback/d3d9-iteration05.dll`. The route is off unless the launcher
passes `--motion-output`; see the run command in
[motion output](verification/motion-output.md). The installed build predates the
archive-wide table and the selector correction.

## Concrete next work

1. Implement step 1 of the temporal integration design: R32F current-depth
   target written by the variants, per-draw jitter, fixtures, and depth support
   in the readback analyzer.
2. User-managed diagnostic run with the installed route (command in
   [motion output](verification/motion-output.md), "Gameplay diagnostic run"):
   capture runs of at least three consecutive frames including one stationary
   view, then run the readback analyzer, compare color against a route-off
   capture of the same scene, and read the per-frame gate histogram for
   unclassified shader pairs and frame time.
3. Add projection jitter in the same hook and connect matched color/depth/motion
   to the temporal resolve; define the camera-cut policy from the observed
   camera-serial and view-delta evidence.
4. Establish the FP16 scene path and enable only reviewed material variants
   there; integrate a GPU-native HDR presentation route. Continue the remaining
   roadmap features.
5. Loading-time gap and alt-tab cursor remain tracked and unfixed.

## Replay/admission line (reference only, superseded 2026-09-12)


The detached [same-draw material prototype](verification/material-motion.md)
now writes color and motion correspondence together for one common opaque SM3
pair. Its 82 configurations pass 1,182 checks, 2,952 numerical motion samples
and 164 bilateral depth cases. All compared color components are unchanged, and
motion matches the independent replay reference exactly. At 5120×1440, the
small synthetic workload averages 1.60 ms for one draw versus 2.24 ms for two
passes, including submission and completion; this is not a game FPS result.
Both the game's A8R8G8B8 color format plus RGBA32F motion and an equal-format
control pass. Host optimized/ASan/UBSan checks also preserve the original
programs and reject 57,152 input mutations. The transformer is **not linked into
the proxy or installed**. Live history, binding and broader material coverage
remain next work; see the [module contract](architecture/material-motion-prototype.md).

The portable geometry path removes DLL-version allowlists, private Wine buffer
layouts and native method RVAs. Eligible managed WRITEONLY buffers receive
readable native backing, preserving application-visible Usage and observing only
existing Lock/Unlock uploads. The loading cache likewise replaces DLL fingerprints
and private method addresses with public COM contracts. The
[dependency audit](architecture/runtime-dependencies.md) documents the proxy
mechanisms and remaining platform gaps. Game EXE/DLL patches, private structures
and disassembly remain explicitly allowed.

The reviewed bounded sidecar index removes the linear allocation-list lookup.
In the same synthetic Preview benchmark, acquiring and inspecting 700
many-buffer leases fell from 11.407 ms to 2.537 ms combined; the shared-buffer
control remained near 2.44 ms. The index passes 552 observer checks, 421 geometry
checks and 84 benchmark samples. This is CPU evidence, not game FPS
or proof that complete live replay is affordable. See
[sidecar index verification](verification/finite-sidecar-index.md),
[performance measurements](verification/geometry-performance.md) and
[review 10](verification/review-10.md). The installed DLL remains unchanged.

The [application admission core](verification/application-admission.md)
passes 4,865 checks in each of four standalone builds, including ASan/UBSan,
ThreadSanitizer and a CPU-only x86 Preview run. Independent review accepted its
root counting, nesting, permanent vetoes and nonblocking replay promotion.
The standalone x86 [ABI adapter](architecture/application-admission-abi.md)
passed 130 CPU-state/behavior checks and 21 timing samples at `127c3da`. Its emitted
code removes compiler exception bookends from the adapter. CMake disables
exceptions for that source file; generated ownership entry definitions use a
separate scoped option, while handwritten helpers retain exception handling.
The disabled adapter path makes no runtime calls.
Both modules are now linked into production behind the off-by-default
`X3M_ADMISSION=1` option. All 297 generated ownership entries, eleven loader
exports, thirty capture bodies, sixteen loading IAT roots and twenty-four bounded
mesh thunks enter the same process monitor before their work. The
[ownership fixture](verification/ownership-admission.md) passes 147 checks in
twelve modes, including actual callback registration and final child/parent
release. [Process configuration](verification/process-admission.md) preserves
CPU state and publishes one immutable mode. These are ordinary entry boundaries;
complete callback/window coverage, trusted native-helper authority, mapping
validation and the exclusive GPU segment still gate live replay. See
[proxy integration](verification/proxy-application-admission.md). The
[game callback disassembly](reverse-engineering/game-callback-registration.md)
identifies D3DX device routes, effect-state callbacks and window-message hazards.

The [actual-DLL cost comparison](verification/hook-admission-performance.md)
passes seven cases and 196 timing samples. Admission adds about 134–154 ns to
the tested single-boundary calls and 285–316 ns to capture-plus-ownership calls.
These are synthetic CPU timings including their native operation, not replay
GPU cost or game frame time. Loading/cache verification passes sixteen explicit
off/on cases with balanced admission retirement. See
[review 11](verification/review-11.md).

A private motion producer now connects main-scene draw observations to
bounded native geometry leases, CPU storage correspondence and an actual
pre-Clear GPU replay. It releases the motion target and replay resources within
the boundary callback. Its synthetic integration passes, but **live GPU dispatch
is refused until buffer-write/replay exclusion is implemented**: the capture
mutex alone does not serialize worker VB/IB mappings. Only a successful Clear, surviving scene selection and
successful Present can commit CPU matrix history. Camera-cut continuity and
complete scene-color coverage remain explicitly unknown; no temporal-color
consumer is enabled. See [motion capture](verification/motion-capture.md),
[geometry leases](verification/geometry-leases.md),
[execution scopes](verification/execution-state.md) and
[review 7](verification/review-07.md). The combined DLL and forced native fallback verification pass;
the installed iteration-5 DLL remains unchanged.

The finite-position source path includes the reviewed compact classification
core, public managed VB/IB descriptors and allocation-owned upload
observer. It obtains finite XYZ and actual index bounds from existing writes,
with no extra buffer Lock or game-pixel readback. The new reader can acquire a
native geometry lease while the actual getter references remain alive, then
revalidate the immutable requests at replay. See
[finite upload evidence](verification/finite-upload-observer.md) and
[draw inputs](verification/draw-input.md).

CPU correspondence now distinguishes diagnostic storage pairs from temporal
continuity; its 3,404 checks pass. The embedded motion PS is compiled from our
original HLSL and needs no runtime compiler. Detached numerical verification
also feeds the production temporal resolve, but the live diagnostic output is
not consumed by that resolve. See [motion history](verification/motion-history.md)
and [GPU motion](verification/rigid-motion.md).

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
The full Python analysis suite passes **289 tests**.
The post-install source registry also retains original shader model, constructor
and position-write order, independently verified for all 234 row-dot profiles.
The detached rigid-motion pass now creates the reviewed fixed SM3 replay program
internally and requires a cached exact-source qualification token plus an explicit
finite-position attestation. The 32-profile program passes 1,573,392 covered
component comparisons and 134 bilateral raster/depth cases; independent review
checks its evidence limits. These source changes have not replaced the installed
iteration-5 DLL.

The [live draw-input reader](verification/draw-input.md) passes 260 checks,
74 caller-state comparisons and seven failed-getter controls. It reads exact
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
The latest combined source DLL passes 26 actual-DLL integration cases, including
admission off/on, native escape vetoes, final transaction retirement and 24 native
Clear CPU-state witnesses. Unsafe live motion dispatch remains explicitly refused.
Its SHA256 is
`5a5f8a78d7c9a802d844368c7a68572c009edd1272b03e8dab306e6bcda39007`; it is
**not installed**. The production link contains 23 objects; the 20-object forced
native fallback also passes. See [review 11](verification/review-11.md).
The earlier `aa61e7ff` build is retained for the hook-cost comparison and described
in [review 10](verification/review-10.md). Earlier source evidence remains in
[review 8](verification/review-08.md) at `5196f31`,
[review 7](verification/review-07.md) at `0ce0814`,
[review 6](verification/review-06.md) at `c4f3d45` and
[review 5](verification/review-05.md) at `437e95b`. Shared result paths now refer
to the latest verified source; historical commits preserve their prior reports.

The [detached adjacency cache](verification/mesh-adjacency-cache.md) passes
741 checks using real native mesh acquisition, exact byte keys, bounded storage,
native downstream cleaning/optimization and computational FP-state parity.
Repeated original synthetic meshes show a large hit-time reduction including
acquisition/lookup cost. The iteration-5 run recorded 7,199 gate rejections and
no cache calls or hits.
Static analysis identifies dynamic SYSTEMMEM mesh options excluded by the installed
gate; the portable four-option implementation now passes 12,905 actual
native/wrapped checks, with 75 + 123 loading regressions and independent review.
DLL fingerprints/private method RVAs are removed, and incoming LastError is part
of the exact cache key.
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

### Superseded next-work list (2026-09-11)

1. Prepare limited live routing for the verified
   [motion output alongside color](architecture/motion-output-strategy.md).
   Connect existing object/transform history, define shader/constants/MRT binding
   ownership and failure handling, and invalidate motion for unsupported
   contributors. Broaden profiles only with their own register/coverage review.
   Replay remains a reference/fallback candidate; no motion route is enabled in
   gameplay yet.
2. Consolidate the next user-managed diagnostic run around the chosen motion
   route, history/coverage and loading observations. Include the existing finite
   POSITION capture only where required by retained replay or geometry work;
   same-draw shader output does not reread saved geometry. The game's dynamic
   SYSTEMMEM mesh configuration now passes native tests; its actual cache hit rate
   still needs a future run.
3. If replay remains part of the live renderer, establish explicit buffer-write/replay exclusion before enabling the private
   GPU motion diagnostic on live finite uploads. Complete
   camera-cut policy and scene-color/reactive masks before consuming its output
   in temporal resolve. Account separately for CPU-changing particles/stardust
   and overlays; storage correspondence alone does not prove temporal continuity.
4. Validate live jitter placement and position/raster equivalence using the full
   archive registry, then connect matched color/depth/motion inputs to temporal resolve.
   Camera-only reprojection cannot satisfy the observed scene; TAA remains required.
5. Establish the FP16 scene path and enable only reviewed material variants there.
   Integrate a GPU-native HDR presentation route; output conversion of clipped
   8-bit color is insufficient. Continue all remaining roadmap features.
6. Measure exact mesh-key reuse and real acquisition/lookup cost before enabling
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
