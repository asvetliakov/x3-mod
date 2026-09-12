# Live motion route (checkpoint B1 + temporal steps 1 and 3) verification

Synthetic verification of the live same-draw route through the actual proxy
DLL under CrossOver Preview's Steam bottle with the process-local `d3d9=n,b`
override. No game launch; the reviewed shader pair is read from local files
and never enters the repository or the reports.

```sh
python3 verification/probe/run_motion_row_history.py     # host unit fixture, release + ASan/UBSan
python3 verification/probe/run_motion_output.py          # fresh build + 26 DLL runs (four environments, jitter, TAA, bench)
python3 verification/probe/check_no_x87.py               # light setter hooks reach no x87 code
```

`check_no_x87.py` disassembles `build/d3d9.dll`, walks the static call graph
from the six `LightCallBoundary` setter hooks and fails on any x87 opcode
other than the project's own fnsave/frstor transport pairs and the MXCSR
transfers; it backs the boundary choice recorded in
[review 12](review-12.md).

## Fixture

`verification/probe/motion_output_fixture.cpp` creates a 64×64 windowed
A8R8G8B8 device with a D24X8 auto depth surface, the reviewed VS/PS pair, an
original flat ps_3_0 and two original triangles (an oversized one covering the
right of the viewport and a small one in the upper left), each with its own
synthetic node scope. Every frame: hostile render states and stream
bindings; color+depth Clear (the route latches and schedules the fill); the
hostile scissor test enabled (after the Clear, so the whole target holds the
clear colour and the coverage oracle below can predict it); a
background draw with the reviewed VS and the flat PS (the fill runs inside this
hook and every touched state, RT2 and `COLORWRITEENABLE2` included, is
compared before and after); scene states; depth-only Clear; scene draws with
known submitted rows (`c24–27`), each followed by a full state comparison
that includes the rows themselves (so a jittered draw's rows must be written
back bit-exactly); EndScene; color readback; coverage oracle; motion and
depth readback; Present.

With `X3M_MOTION_JITTER=1` the fixture computes the route's Halton(2,3)
sample for each frame (`(frame mod 8) + 1`, centred) and expects every scene
draw to be rasterized at the jittered position. The **coverage oracle** runs
in every case: per pixel it finds the front-most scene draw at the
unjittered object point behind `(x - jx, y - jy)` (+X right, +Y down in
pixels) and classifies the colour as material program, flat program (exact
colour) or background (the frame's own uncovered colour, sampled from the
first pixel no scene draw covers; the hostile background wireframe never
touches a pixel inside its scissor rect); pixels within 0.03 px of any
scene-draw edge are skipped. With jitter off the offsets are zero and the
oracle is its own control; a wrong sign or scale would move every scene edge
by up to one pixel and fail. The route's `COVERAGE` line and the DLL's
`motion_output_frame` jitter fields are cross-checked against the same
sequence, and the colour hashes of a jittered run must differ from the
unjittered ones in at least six of twelve frames while agreeing between the
production and seam DLLs.

Two DLLs are exercised:

- **production** – `build/d3d9.dll` unchanged. Since the structural
  background rule the selector enters the synthetic scene phase here too, but
  the game observers are absent, so every scene draw that passes gates 3-4
  routes in sentinel-only mode (gate 5, never matched): this proves fill,
  restoration, Reset with RT1 owned, readback files, all-sentinel readbacks,
  bit-identical color and device release without any live motion.
- **seam** – the production objects linked with `capture.cpp` and
  `motion_output.cpp` compiled under `X3M_MOTION_OUTPUT_FIXTURE`, exporting
  `x3m_motion_output_fixture_configure` (per-draw synthetic scope; its
  background-signature fields are deprecated and ignored by the selector) and
  `x3m_motion_output_fixture_readback` (RT1), `..._readback_depth` (RT2) and
  `..._last_pixel_abi` (the `c216–217` values the last routed draw uploaded).
  The game observers cannot run in a
  synthetic process, so this seam is the only way to reach gate 6 and matches.

The seam script (DLL frame numbers): f0 first frame (mode 0 sentinel), f1
matched with the application writing `c252–255` and PS `c216–217` first, f2
scope withheld (sentinel-only mode, gate 5), f3 nothing recorded in f2 (gate
6), f4 matched plus one blend-enabled and one flat-PS draw that must not route
(gates 4 and 3), f5 duplicate key consumed once, f6 the duplicate poisoned the
key, f7 a `D3DSBT_ALL` state block Apply rebinding the flat PS is honored
(gate 3, flat PS still bound afterwards), f8 no f7 record, Reset, f9 history
restarted, f10 matched, shader release/recreate, f11 matched.

The CPU oracle replays each frame's draws per pixel with the depth test using
D3D9's integer raster sample convention (at the jittered sample position for
jittered draws), writing the previous-UV / previous clip Z/W / validity ABI
for matched routed draws and the sentinel otherwise, and, for RT2, the z/w of
the front-most routed draw or the -1 sentinel; pixels within 1.5 px of a
coverage edge are skipped. Tolerances: 0.01 px UV, 4e-6 previous depth, 4e-6
current depth. The expected previous UV is built from the **unjittered**
previous rows with no jitter term, and the seam checks that every routed draw
uploaded `c216 = (1/W, 1/H, 0, 0)`: zero prior jitter, as the
[integration design](../architecture/temporal-integration.md) requires.

### Lazy RT binding equivalence (`X3M_MOTION_RT_MODE`)

The route's default binds RT1/RT2 and `COLORWRITEENABLE1/2` around every
routed draw (`perdraw`). The `lazy` experiment keeps them bound across
consecutive routed draws and restores them before the first application
call that could observe or depend on them (the list is in
[telemetry.md](telemetry.md#route-and-boundary-cost)). Two kinds of runs
prove the modes equivalent:

- the regular script with `X3M_MOTION_RT_MODE=lazy` (`production-lazy-on`,
  `seam-lazy-on`, `seam-ownership-lazy-on`, `seam-taa-lazy-on`): every
  fixture check, restoration comparison, colour hash and readback file must
  equal the per-draw twin's. Here the fixture's state snapshot after each
  draw is itself a restore point (`GetRenderTarget`), so the DLL's
  `set_rt` count stays four per routed draw with one `lazy_flushes` per
  routed draw: equivalence by construction, covering every hook that restores;
- the **burst** script (`burst` mode of the fixture, `production-burst-*`
  and `seam-burst-*`): nine frames of `A B | A(flat PS, gate 3) | A B |
  A(blend, gate 4) | B | application SetRenderTarget(0) (even frames) or
  depth-only Clear inside the scene (odd frames) | A (gate 2: the selector
  rejected)` with **no application getter between the routed draws**, scope
  withheld so every routed draw is sentinel-only on both DLLs. The pre-burst
  state snapshot (taken with the bindings the last draw leaves) must equal
  the post-burst snapshot (`RESTORE label=burst differences=0`), and the
  fixture prints per frame the colour hash, a process-independent `STATE`
  signature of the snapshot and, seam, the FNV hash of the RT1/RT2
  readbacks. The runner requires all of them identical between the modes,
  the DLL's capture-frame readback files (frames 7-8) byte-identical, and
  the DLL's per-frame `set_rt` to be 20 in per-draw mode (five routed
  draws x four) and 12 in lazy mode (three bind/flush pairs: the flat draw,
  the blend draw and the application call each end a run) in frames 0-6;
  frames 7-8 are capture frames, whose diagnostics restore before every
  draw, and count 20 in both modes with five flushes. The frame line is
  logged every frame there (`X3M_MOTION_FRAME_LOG=1`).

Each DLL runs with the route off and on in four environments (`VARIANTS` in
`run_motion_output.py`), plus one plain run per DLL with the route and the
jitter on, plus the TAA and bench runs described in
[Temporal resolve (step 3)](#temporal-resolve-step-3): **plain** (the original four runs), **ownership**
(`X3M_OWNERSHIP=1`, the wrapper the gameplay run needs for object lifetime),
**depth** (`X3M_OWNERSHIP=1 X3M_DEPTH_COPY=1 X3M_SCENE_DEPTH_CAPTURE=1`, the
copy-depth storage and the scene-depth adapter active in the eight requested
capture frames) and **admission** (`X3M_OWNERSHIP=1 X3M_ADMISSION=1`, the
process admission monitor the launcher environment may carry). The fixture
itself is unchanged: through the wrapper its device, shaders, buffers and
surfaces are canonical wrappers, the route's native slots are the wrapper's
methods, and the same 30 or 90 checks, 39 restoration comparisons, Reset
with RT1 and RT2 owned and the zero final device/factory Release apply. The wrapper
environments add these witnesses from the capture log: exactly one
`ownership_factory mode=wrapped`, no fallback, `ownership_copy_depth` at
`create_after` and `reset_after` (depth mode: storage available, original
bound, no copy, generation increasing across Reset), `ownership_copy_depth
phase=present` in each captured frame while the route is requested (depth
mode: `source_epoch` equals two application depth clears per frame), the
scene-depth adapter's `scene_depth_frame` begin/end for frames 1–8 with
nothing attempted or copied and no game boundary selected, and the admission
mode's `application_admission_mode/final` rows (`verify_admission` from the
ownership integration) plus `admission_metric` per captured frame with zero
veto bits. Balanced adoption/retirement is proven by the fixture's zero final
Release: the wrapper's logical device count reaches zero only when every child
wrapper, the route's included, has been released.

## Temporal resolve (step 3)

With `X3M_TAA=1` the fixture ends every frame like the game: inside the scene
it unbinds the depth surface (the selector's Scene to AwaitCopy transition),
reads the main target back, snapshots the device state, copies the main
target into a bloom source texture with a full-rect `StretchRect` (the route
resolves inside that hook, before the application's copy), compares the state
snapshot (which for these runs also covers textures and sampler states of
stages 0-7, PS `c0-7`, the stream-0 frequency and the indices the resolve
touches), reads the main target and the bloom source back, and rebinds the
depth surface after `EndScene` (the game does this after its bloom passes;
without a bloom sequence the selector rejects the rest of the frame, which
must not disturb the history). Per frame it requires:

- (c) the bloom source equals the main target after the copy: the
  application's copy received the resolved image;
- history use follows the script: none in frame 0, after Reset (frame 9) and
  in the seam's cut frames (3, 5, 6, 8: keyed draws missing above the 0.25
  bound), history in the other seam frames (1, 2, 4, 7, 10, 11); production
  routes sentinel-only, so it never uses history;
- (b) a frame without history leaves the 8-bit main target bit-identical
  (the FP16 round trip and the current-only resolve change nothing, alpha
  included);
- (a) seam: the main target after the copy equals, byte for byte, a
  **reference** `TemporalPass` (the production class with the same embedded
  resolve bytecode) run on a plain second device of the system d3d9 from the
  same inputs read back through the seam (RT1, RT2, the 8-bit main target
  before the copy, the frame's jitter, the DLL's cut rule and the Reset
  points), followed by the same point-filtered copy-back; the reference FP16
  output is written as `reference_taa_<frame>.rgba16f` and must equal the
  DLL's `X3M_TAA_DEBUG` file `taa_<device>_<frame>.rgba16f` byte for byte in
  capture frames 1-8;
- (e) zero differences in the boundary state comparison (51 restoration
  comparisons per run instead of 39);
- (d) Reset in the middle of the script keeps working (frame 9 resolves
  current-only, frame 10 accumulates again).

The runner adds: the presented color hashes of the production TAA runs (plain
and through the wrapper) equal the jitter-only run in all 12 frames (f: a
current-only resolve is invisible), the seam TAA runs equal it in every
frame without history and differ in history frames (the moving edges blend
the previous frame), the pre-boundary image is the jittered raster in every
TAA run, the two seam runs agree, every frame line reports
`taa_attempted=1 taa_resolved=1 taa_skip=0` with the expected `taa_history`
and zero result codes, one lazy `motion_output_taa initialize=00000000
references=1`, `taa_references=1` after Reset (only the resolve shader
survives it), no `motion_output_taa_failed`, and the analyzer's
[`taa_image`](motion-readback.md#6-resolved-image-sanity-signal-taa_image)
check passes on the TAA capture logs with a zero differing fraction for the
production runs. The final device and factory Release still return zero
through the wrapper with the pass's objects counted.

Four bench runs (`bench WxH` mode, production DLL, route and jitter on,
resolve off and on) time the boundary `StretchRect` at 1280x768 and
5120x1440: 24 frames of two scene draws each, an EVENT query drained before
and after the call, QPC around it, the first four frames discarded. The
difference between on and off is the resolve, its two copies, the state
block capture/apply and the copy-back, CPU-inclusive on the Preview backend
(not a GPU timestamp).

Results (2026-09-12, fresh `build/`): the four TAA runs pass with the check
and restoration counts of the table above; the seam runs compare all 12
frames against the reference resolve byte for byte and frames 1-8 at FP16
against the reference file; history follows the script (seam frames 1, 2, 4,
7, 10, 11; the production pass reports valid history from frame 1 on while
every pixel resolves current-only); 374 pixels per seam run are changed by
history (frames 1, 4, 7, 10, 11: 93, 104, 25, 28, 124), none in production;
the production presented image equals the jitter-only run in all 12 frames
and the pre-boundary image equals it in every TAA run; the analyzer's
`taa_image` check passes on all four capture logs (differing fraction 0 in
production with a maximum RGB difference of one FP16 ulp, 0.0044 / 0.0229 /
0.0046 in seam frames 1 / 4 / 7); `taa_references` is 7 natively and 12
through the wrapper while the pass is allocated and 1 after Reset; the final
device and factory Release reach zero in every run. Bench: boundary median
0.330 ms (resolve off) versus 1.004 ms (on) at 1280×768 and 0.711 versus
2.754 ms at 5120×1440 (review-16 rerun), i.e. about **0.67 ms** and **2.04 ms** for the
resolve, its copies and the copy-back, CPU-inclusive. Details, hashes and
the per-frame bench samples: `verification/results/motion-output-summary.json`
(`cases` and `bench`), capture logs `motion-output-*-taa-on-capture.log`.

### Camera reprojection cases

Five seam runs cover the route's camera read, the sentinel policy switch
and the environment-map exclusion
([temporal-integration.md](../architecture/temporal-integration.md#camera-reprojection-for-sentinel-pixels-2026-09-12)).
The fixture keeps its own projection and view buffers and, with
`X3M_FIXTURE_CAMERA=rotate`, installs the two pointer slots as the engine
camera globals through the seam export `x3m_camera_state_fixture_install`
(the identity gate is bypassed; the production DLL and the seam without the
install report `camera=executable_mismatch` and read nothing). The scene
view yaws one degree per frame with a 30-degree jump at frame 7 (`m00` 0.8,
`m11` 4/3, a translation of `(12.5, -3, 1000)` that the far-plane transform
must ignore), written before the depth-only Clear where the route reads it.
The reference resolve on the second device is driven by the same builder
(`camera_sentinel_policy` from the fixture's current and history views), so
the byte-for-byte comparison of the DLL's resolved image also proves the
DLL read the buffers, validated them, built the same matrix and chose the
same policy; the fixture prints its decision per frame (`CAMERA_EXPECT`) and
the runner compares it with the DLL's `motion_output_frame` fields and
`camera_state` lines (policy, reason, cut, rotation, `p00 p11`, `r00..r22`,
`t`, read counts, the history's view).

- `seam-taa-camera-on` (auto): policy 2 on every frame with a previous
  view (1-6, 8, 10, 11), policy 1 on frame 0 (no previous view), frame 7
  (rotation 31 degrees: a camera cut, `camera_cut=1`, current-only, the
  raster unchanged) and frame 9 (after Reset); history on frames 1, 2, 4,
  10, 11 (the script's cuts plus the camera cut); the resolved image equals
  the reference on all 12 frames; the camera path changes the colour of
  history frames only (unrouted pixels now blend the reprojected previous
  frame). 164 fixture checks.
- `seam-taa-camera-sentinel1-on` (switch off): policy 1 everywhere, no
  camera cut (frame 7 keeps its history), and the colour of every frame is
  bit-identical to `seam-taa-on` (no camera installed) — which itself is
  bit-identical to the pre-change run (compared against the previous
  `motion-output-summary.json` hashes during development). 163 checks.
- `seam-taa-camera-sentinel2-on` (strict, camera readable): identical to
  auto (nothing skipped). 164 checks.
- `seam-taa-sentinel2-nocamera-on` (strict, no camera): every frame is
  attempted and skipped (`taa_skip=9`, `taa_resolved=0`), no debug
  readbacks, the colour equals the jittered raster. 145 checks.
- `seam-taa-envmap` (mode `envmap`, camera, auto): five frames — routed,
  environment map between the background draw and the depth Clear, routed,
  environment map before the initial Clear, routed. The environment-map
  frames show `routed=0 gate2=9 draws=9 selector_state=9 taa_skip=2
  camera_valid=0` (the selector rejected at the first face's
  `SetRenderTarget`, before any face Clear could latch; the second placement
  does not even latch: `latched=0 filled=0 camera_reads=0`), no
  `motion_route` line, RT1 all sentinel in the captured frame, and the
  `camera_state` line with the scene view unread and the history's view
  dropped; the routed frames after them resolve without history
  (`cut=1 cut_missing=1.0000`: the rejected frame recorded no rows) and
  start again without a previous view (`camera_reason=3`). 62 checks, 18
  restorations.

Bench with the 3,840-word resolve (policy 1 in the bench: the production
DLL has no camera in the synthetic process, and the camera read adds two
64-byte copies per frame at the Clear hooks, outside the boundary): boundary
median 0.351 ms (resolve off) versus 0.738 ms (on) at 1280x768 and 0.487
versus 2.243 ms at 5120x1440, i.e. **0.387 ms** and **1.756 ms** for the
resolve, its copies and the copy-back, CPU-inclusive (previous program:
0.320 / 1.623 ms from boundaries of 0.683 / 2.239 ms; the 1280x768 delta
moved with the off-side median, 0.362 to 0.351 ms, within the run-to-run
spread of these EVENT-synchronized samples). Per-case results, the camera
decisions per frame and the environment-map counters:
`verification/results/motion-output-summary.json` (`cases`, `camera`).

## Ownership wrapper interaction

With `X3M_OWNERSHIP=1` the loader wraps the factory, `CreateDevice` returns
the wrapper's device and capture hooks the wrapper's vtable, so the route's
"native" slots are the wrapper's forwarders (`src/ownership/d3d9_forwarders_inc.h`),
which unwrap inputs, forward to the backend, wrap outputs and pass every
HRESULT through `observe_result`. Review 12 left this unexercised; the analysis
and the fixture environments above close it. Line numbers are as of this
checkpoint.

- **(a) Wrapper state.** The forwarders the route calls (`SetRenderTarget`
  :252, `SetDepthStencilSurface` :262, `SetRenderState` :336,
  `DrawPrimitiveUP` :444, `SetVertexShader` :486, `SetPixelShader` :554 and the
  constant setters) keep no binding shadow. The only depth bookkeeping is
  `clear_device` (`src/ownership/d3d9_ownership.cpp:1131`), which queries the
  live backend depth binding at the application's Clear and advances
  `source_epoch`; the fill's depth unbind is applied and restored inside one
  draw hook, so no application Clear or `copy_auto_depth` can observe it. The
  state-block recording flag changes only through Begin/EndStateBlock, which
  the route never calls; `scene_transition` (:1147) is reached only by the
  attach self test, outside the application scene, and execution observation
  is disabled by the loader (`src/proxy/loader.cpp:91`). Every wrapper method
  enters a nested admission scope; `unwrap` (:629) vetoes only foreign
  pointers (:644), and every pointer the route passes came from a wrapper
  getter or creation. `SceneCapture` observes the capture hooks, not the
  wrapper, so the route's calls stay invisible to scene-depth capture as in
  native mode. Witness: `ownership_copy_depth phase=present` (added to the
  Present hook for captured frames while the route is requested,
  `src/proxy/capture.cpp:430`) shows `source_epoch = 2 × (frame + 1)` with the
  route on, i.e. every application depth clear found the original bound.
- **(b) Reset.** `reset_device` (:1082) retires only resources adopted through
  `retain_renderer_resource` (:1691), which the route does not use, then
  forwards. The capture reset hook calls `MotionOutput::before_reset`
  (`src/proxy/capture.cpp:461`) before the wrapper's Reset, so the default-pool
  target is gone before the backend Reset runs: nothing is released twice or
  missed. A failed Reset marks the wrapper lost; the route's next allocation
  fails and stays off until the following Reset (`target_failed_`).
- **(c) Final-Release probe — defect, fixed.** Every child wrapper owns one
  logical device reference (`adopt`, :705), and `Texture::GetSurfaceLevel`
  (:677) adopts the level surface as a child of the *device*. The route held
  the texture and its level surface, two logical references, while
  `device_references()` counted one (natively a texture level shares the
  texture's count: one device reference). The probe `after == held + 1` in
  `release_device` (`src/proxy/capture.cpp:368`) could therefore never match
  through the wrapper and the application's final Release would have returned
  nonzero: a leaked device, never a use-after-free. `MotionOutput::ensure_target`
  (`src/proxy/motion_output.cpp:187`) now retains only the level-0 surface,
  which keeps its container alive on D3D9 and through the wrapper, so one owned
  object is one device reference in both models; `device_references()` counts
  it. Verified by the zero final device and factory Release in every wrapper
  run and by `motion_output_release` once per run.
- **(d) HRESULT observation.** A route call returns D3DERR_DEVICELOST or
  DEVICENOTRESET only when the backend is actually lost; `observe_result`
  (:1106) then retires copy-depth storage, geometry and finite evidence,
  exactly what the application's next call would trigger. Because the fill runs
  before the application's draw in the same hook, the wrapper may learn of a
  loss one call earlier; no route call manufactures a loss, and a failed fill
  or routed draw is already counted and restored by the route.

## Results

| Environment | Case | Fixture checks | Restoration comparisons | Motion pixels | Matched pixels | RT2 written pixels | Coverage pixels |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| plain | production off / on | 30 / 30 | 39 / 39 | – | – | – | 48,242 |
| plain | seam off / on | 30 / 90 | 39 / 39 | – / 44,284 | – / 10,261 | – / 27,170 | 48,242 |
| ownership | production off / on | 30 / 30 | 39 / 39 | – | – | – | 48,242 |
| ownership | seam off / on | 30 / 90 | 39 / 39 | – / 44,284 | – / 10,261 | – / 27,170 | 48,242 |
| depth | production off / on | 30 / 30 | 39 / 39 | – | – | – | 48,242 |
| depth | seam off / on | 30 / 90 | 39 / 39 | – / 44,284 | – / 10,261 | – / 27,170 | 48,242 |
| admission | production off / on | 30 / 30 | 39 / 39 | – | – | – | 48,242 |
| admission | seam off / on | 30 / 90 | 39 / 39 | – / 44,284 | – / 10,261 | – / 27,170 | 48,242 |
| plain, jitter | production on / seam on | 30 / 90 | 39 / 39 | – / 44,279 | – / 10,348 | – / 27,293 | 48,957 |
| plain, TAA | production on / seam on | 69 / 150 | 51 / 51 | – / 44,279 | – / 10,348 | – / 27,293 | 48,957 |
| ownership, TAA | production on / seam on | 69 / 150 | 51 / 51 | – / 44,279 | – / 10,348 | – / 27,293 | 48,957 |
| plain, lazy RT mode | production on / seam on | 30 / 90 | 39 / 39 | – / 44,284 | – / 10,261 | – / 27,170 | 48,242 |
| ownership, lazy RT mode | seam on | 90 | 39 | 44,284 | 10,261 | 27,170 | 48,242 |
| plain, TAA, lazy RT mode | seam on | 150 | 51 | 44,279 | 10,348 | 27,293 | 48,957 |
| burst, per-draw / lazy | production | 23 / 23 | 18 / 18 | – | – | – | 35,505 |
| burst, per-draw / lazy | seam | 68 / 68 | 18 / 18 | 31,932 | 0 | 21,447 | 35,505 |

All 39 restoration comparisons in every run report zero differences,
including the application's `c24–27` after every jittered draw. Every
seam-on run: maximum error 0.0016 px UV, 3.7e-8 previous depth, 3.7e-6
current depth (RT2 against the oracle's z/w on 27,170 written pixels,
sentinel elsewhere; 171 written pixels in frame 7, where only the small
triangle routes), 19 routed-draw decisions matched against the script,
`c216 = (1/64, 1/64, 0, 0)` uploaded by every routed draw. The jitter runs:
the coverage oracle matches at the Halton offsets in all 12 frames
(48,957 pixels, 195 ambiguous; the `motion_output_frame` jitter index and
values and the `EXPECT`/`motion_route` `jittered` flags agree per draw, every
scene draw jittered), the colour differs from the unjittered run in all 12
frames and is identical between the production and seam DLLs, and the seam
readback still matches the oracle built from the unjittered previous rows
(0.0019 px UV, 7.2e-8 previous depth, 3.7e-6 current depth). This proves the
sign convention `jx_ndc = 2·jx_px/W`, `jy_ndc = -2·jy_px/H` for +X right, +Y
down raster pixels. Cut detector: per frame `cut_samples` equals the matched
draws, the median origin displacement is 1.6 px (0.05 NDC at 64 px), the
bound 2.4 px (48 px scaled by 64/1280) and 0.25, and the verdict is 1 exactly
in the frames whose keyed draws miss (3, 5, 6, 8: missing fraction 1.0 or
0.5). Every production-on run (structural
selector): the same 19 scene draws reach the route, `selector_state=2` at
Present, one gate-2 background draw per frame, the blend and flat-PS draws
stop at gates 4 and 3, and every remaining draw routes sentinel-only at gate 5
with `matched=0`; readbacks are all sentinel. The color hashes of all 12 frames form
one set across all sixteen runs: identical with the route off and on, and
identical between the plain, wrapper, depth and admission environments. The
DLL's per-frame gate histogram and per-draw `motion_route` decisions in capture
frames 1–8 match the script exactly (routed 2/2/2/2/3/2/1/2, matched
2/0/0/2/2/1/1/1). Frames 1–8 also write `motion_<device>_<frame>.rgba32f`
readback files (65,536 bytes) containing only ABI values. The final device and
factory Release return zero in every run, through the wrapper as well, because
the route drops its owned objects first (`motion_output_release` once per
enabled run).

Lazy RT mode (2026-09-12, fresh `build/`): the four lazy regular-script
runs equal their per-draw twins in every colour hash (all 12 frames, the
pre-boundary hashes of the TAA pair included), every readback file of
frames 1-8 (16 files per pair, byte-identical), checks, restorations,
motion/matched/RT2 pixel counts; their frame lines report `set_rt` = 4 x
routed and `lazy_flushes` = routed (each fixture snapshot restores). The
burst runs: both DLLs, both modes, 9 frames each, `RESTORE label=burst
differences=0` in every frame, identical colour hashes, `STATE` signatures
and (seam) RT1/RT2 hashes between the modes, identical `motion_1_7/8` and
`depth_1_7/8` files, per-frame counters `draws=9 routed=5 gate2=2 gate3=1
gate4=1 gate5=5 selector_state=9`, no apply/restore failure, and the DLL's
route-issued `SetRenderTarget` count per frame **20 in per-draw mode
against 12 in lazy mode** in frames 0-6 (three flushes per frame: before
the flat-PS draw, before the blend draw and before the application
`SetRenderTarget`/`Clear`), 20 with five flushes in the capture frames 7-8.
The seam oracle still matches on 31,932 pixels (21,447 with RT2 depth, all
sentinel in RT1 since scope is withheld). Per-frame cost fields of the
64x64 runs (CPU QPC on the Preview backend, indicative only):
`route_draw_us` 20-30 for five routed draws per-draw against 8-13 lazy,
`set_rt_us` 9-17 against 4-11 plus `lazy_flush_us` 5-10, `fill_us` 20-60,
`gate_us` 5-15, and 1.6-2.8 ms of `readback_us` in a capture frame (two
64x64 files), which is why capture frames are classed separately.

Wrapper witnesses: one `ownership_factory mode=wrapped` per wrapper run and no
fallback; depth mode storage `available=1 source_bound=1 copy_valid=0` with
generation 1 at creation and 3 after Reset (retire + allocate), `source_epoch`
4, 6, …, 18 in captured frames 1–8 with the route on (two application depth
clears per frame, none missed while the fill unbound depth), `scene_depth_frame`
begin/end for frames 1–8 with `attempted=copied=confirmed=0` and no
`scene_depth_copy`; admission mode `application_admission_mode requested=1
enabled=1`, device and factory finals quiescent (`active_roots=0`), about
3,950 admitted roots per run, `veto_bits=0` in every captured frame and
`vetoes=0` at teardown, route off and on.

The device gate reports the three-format MRT self test passing on the
Preview backend (`color_errors=0 motion_errors=0 depth_errors=0 targets=3`,
`depth=1 depth_reason=ok r32f=00000000`), four MRTs, 256 VS constants and
`D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`; both targets are created at the
first latch and after Reset (`depth_create=00000000`), and frames 1–8 write
`depth_<device>_<frame>.r32f` (16,384 bytes, values in [0,1] or -1, every
motion-valid pixel written).

A backend finding from the coverage oracle: a full Clear is deferred and its
colour is encoded with the sRGB-write state of the first draw that follows.
With the route on that draw is the fill (sRGB off), so a hostile
`SRGBWRITEENABLE=TRUE` at the Clear made the clear colour differ between
route off and on (`ff203040` versus `ff637889`) with no routed pixel changed;
the fixture now keeps sRGB write off at the Clear and samples the frame's
background colour from an uncovered pixel. The game clears the scene to
black, for which both encodings coincide.

Existing suites after temporal step 1 are listed in
[temporal-integration.md](../architecture/temporal-integration.md). After the
169-row table (review 15):
material-motion structure (169 rows, 1,691 check groups, 1,551,936
mutations, 1,014 aliases, lookup oracle over all rows), material-motion GPU
(Argon 1,182 checks, 2,952 samples, 82 configurations; 169 of 169 rows,
1,230 configurations, 17,413 checks), ownership integration (26 cases, rebuilt `build-ownership/`
with the `frame=` field added to `ownership_copy_depth`), fallback (23
production objects), verification and `check_no_x87.py` (6 light hooks, 125
reachable functions, 0 x87 opcodes) all pass.

Exact hashes, commands and case inventories: `verification/results/motion-output-summary.json`
(per case: `variant`, `ownership` with epochs, generations and admission
counters); fixture output: `motion-output.txt`; capture logs of the eight
enabled runs: `motion-output-<case>-capture.log`; host unit summary:
`motion-row-history-summary.json`.

## Gameplay diagnostic run

### A/B cost runs (route and boundary telemetry)

The iteration-7 run measured 38.5 ms per scene frame with TAA against
16.1 ms with the route alone, with no metric covering the boundary or the
route's own calls ([iteration-07.md](iteration-07.md), Timing). The proxy
now reports both ([telemetry.md](telemetry.md#route-and-boundary-cost)).
To attribute the difference, three runs of the **same save, same view,
same sequence** (load, stand still for ten seconds, turn for ten seconds,
fly forward for ten seconds, quit), each with `--telemetry`, no capture
key pressed until the sequence is over (readbacks are excluded from
ordinary frames but a captured frame is still a hitch):

1. TAA on: `python3 tools/manage.py launch --direct --ownership --object-trace
   --object-lifetime --motion-output --taa --telemetry --capture-start 999999
   --capture-frames 4`
2. Motion on, TAA off: the same command without `--taa` (the route, RT1/RT2,
   the fill, the jitter off).
3. Proxy with the route off: `python3 tools/manage.py launch --direct
   --ownership --telemetry --capture-start 999999 --capture-frames 4`
   (hooks and the ownership wrapper only).

Optionally a fourth run repeats 1 with `--motion-rt-mode lazy`. For each
session log run `python3 tools/analysis/summarize_telemetry.py <session.log>
--output <summary.json>`; it prints the route/boundary table. Compare:

- `frame_normal` per-window mean/minimum between the runs (the regression
  itself; contains application work and pacing);
- run 1 against run 2: `taa_run` count/mean/max and its phases
  `taa_state_capture`, `taa_copy_color`, `taa_copy_depth`,
  `taa_resolve_draw`, `taa_state_apply`, then `taa_copy_back`, and the
  per-frame `taa_run_us` means of the `normal` class (capture frames are
  listed separately). If the sum is a small fraction of the per-frame
  difference, the CPU-side boundary is not the cause and the remainder is
  GPU time or backend stalls elsewhere (`stretch_backend` of the
  application's own bloom copy shows whether the backend now blocks in
  the application's `StretchRect` right after the resolve);
- run 2 against run 3: `route_gate`, `route_draw`, `route_set_rt`,
  `route_jitter`, `route_fill` totals per second and the per-frame
  `gate_us + route_draw_us + jitter_us + fill_us`, against the frame
  difference; `draw_backend` mean between the runs (a routed draw's own
  backend cost);
- run 4 against run 1: `set_rt` and `route_draw_us` per frame (the lazy
  mode's saving) against the per-frame difference.

All spans are CPU-inclusive wall clock; a phase that shows up large is
where the render thread waited, not proof of GPU cost, and a phase that
is small does not exclude GPU cost of the same work.

Two user-managed runs are defined: the diagnostic run below (route, no
resolve) and the TAA run, which is the same command plus `--taa` (and
`--taa-debug` for the offline comparison):

```sh
python3 tools/manage.py install
python3 tools/manage.py launch --direct --ownership --object-trace --object-lifetime --motion-output --taa --taa-debug --telemetry --capture-start 999999 --capture-frames 4
```

`--taa` implies `--motion-jitter` and requires `--object-trace
--object-lifetime` (without history the resolve is current-only and only the
jitter would reach the screen). Do the same still / turning / flying
sequence as below, once with this command and once with the diagnostic
command, and compare: visually, whether edges of ships and station parts
are stable while still and free of ghosting while turning (the current-only
fallback for background, particles and unknown programs shows as ordinary
aliasing there); in the log, `motion_output_device ... taa=1 taa_reason=ok`,
`motion_output_taa initialize=00000000`, per captured frame
`taa_attempted=1 taa_resolved=1 taa_history=1 taa_skip=0` (a `taa_skip` of
2 means the selector never reached the copy in that frame, 6 an open
application query, 3 no jitter), no `motion_output_taa_failed`; offline,
`python3 tools/analysis/analyze_motion_readback.py <session.log> --label
taa` on the TAA capture: the `taa_image` check must pass (finite) and its
differing fraction shows how much of each frame the history touched, and
the motion/depth checks of the diagnostic run must still pass with the
resolve on. Expect a hitch per F8 press: each captured frame now reads back
RT1, RT2, the pre-resolve color and the resolved FP16 image.

The first gameplay checkpoint is a user-managed capture with the route, the
ownership wrapper and both object observers active (object lifetime requires
the wrapper; without the observers the route runs sentinel-only). Install the
current `build/d3d9.dll` and launch:

```sh
python3 tools/manage.py install
python3 tools/manage.py launch --direct --ownership --object-trace --object-lifetime --motion-output --telemetry --capture-start 999999 --capture-frames 4
```

Loading profile run (added 2026-09-12): the same install, then the loading
diagnostics with the in-process sampling profiler
([sampling-profiler.md](sampling-profiler.md)) and the verified adjacency
cache (`--mesh-cache` requires `--telemetry`); the route is not needed to
profile the menu, save-game and sector loads:

```sh
python3 tools/manage.py launch --direct --telemetry --profile --mesh-cache
```

`--profile-interval-us` (default 2000) sets the sampling interval; the
profiler writes `profile_*` reports into the same session log, on the same
QPC clock as `loading_metric`.

`launch --dry-run` with the same options validates them and prints the command
and `X3M_*` environment without starting the game; verified against a scratch
game directory: `X3M_OWNERSHIP=1 X3M_OBJECT_TRACE=1 X3M_OBJECT_LIFETIME=1
X3M_MOTION_OUTPUT=1 X3M_TELEMETRY=1 X3M_CAPTURE_START=999999
X3M_CAPTURE_FRAMES=4 X3M_MOTION_RT_MODE=perdraw`, depth copy, scene depth, finite positions, mesh cache
and motion capture `0`, `d3d9=n,b` for this child only. The launcher does not
set `X3M_ADMISSION`; it is inherited from the shell, and the fixture covers the
route with it on and off.

What the user does:

1. Let the direct start skip the intro, then load a save with several ships in
   view (a station approach or a busy sector).
2. Fly forward and turn the camera for a few seconds so ships and station
   parts move across the screen.
3. Press **F8**, keep flying/turning for three to five seconds, press **F8**
   again. Each press captures four consecutive frames (`--capture-start
   999999` disables the automatic capture); expect a hitch per press because
   every captured frame reads RT1 back and writes it to disk.
4. Quit the game normally (Esc → quit to desktop) so the device's final
   Release and the telemetry summaries are logged.

Files produced under
`~/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/x3-modern-captures/`:

- `session-<date>-<pid>.log`: the capture log. Lines to check:
  `ownership_factory mode=wrapped`, `object_trace active=1`,
  `object_lifetime active=1`, `motion_output_device ... enabled=1 reason=ok
  history_available=1` (capability gate on the game's device),
  `motion_output_variant` per reviewed program the game created,
  `motion_output_target`, `motion_output_frame` (gate histogram and
  routed/matched counts, every 60 frames and in captured frames),
  `motion_route` per scene draw in captured frames, `motion_output_readback`,
  `ownership_copy_depth phase=present` per captured frame,
  `motion_output_release` and `device_destroy` at exit, and no
  `motion_output_fill_failed`/`apply_failed`/`restore_failed`.
- `motion_<device>_<frame>.rgba32f`: eight files (two presses × four frames),
  row-major RGBA32F at the back-buffer size, 16 bytes per pixel (1920×1080 →
  about 33 MB each). Keep them untracked.
- The usual shader dumps and telemetry summaries of a capture session.

Offline analysis of a capture (matched `motion_route` draws against the
readback and the stored rows) is the next step once such a capture exists.

## Limits

Synthetic device program only: no gameplay, no temporal image quality, one
reviewed pair. Jitter is proven by coverage on a 64×64 target; the resolve is
proven by byte-exact agreement with the same `TemporalPass` on a plain
device from the same inputs (not an independent implementation) and by the
8-bit round trip on current-only frames; the bench numbers are
CPU-inclusive. Object scope is injected; the game observers are not exercised,
so the wrapper environments prove the route's fill, routing, Reset and release
through the wrapper, not object history through it. Native Windows is
cross-compiled but not executed. Setter-hook cost in the game is unmeasured.
