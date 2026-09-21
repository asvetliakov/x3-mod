# Live motion route (checkpoint B1 + temporal steps 1 and 3) verification

Synthetic verification of the live same-draw route through the actual proxy
DLL under CrossOver Preview's Steam bottle with the process-local `d3d9=n,b`
override. No game launch; the reviewed shader pair is read from local files
and never enters the repository or the reports.

```sh
python3 verification/probe/run_motion_row_history.py     # host unit fixture, release + ASan/UBSan
python3 verification/probe/run_motion_output.py          # fresh build + 63 DLL runs (four environments, jitter, TAA, bench, lazy, burst, camera, state shadow, scene hook, FP16 HDR stage 1)
python3 verification/probe/check_no_x87.py               # light setter hooks reach no x87 code
```

`check_no_x87.py` disassembles `build/d3d9.dll`, walks the static call graph
from the seven `LightCallBoundary` setter hooks (`SetRenderState` included since 2026-09-12; 129 reachable functions) and fails on any x87 opcode
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
methods, and the same 31 or 91 checks, 39 restoration comparisons, Reset
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

### Render-state shadow (`X3M_STATE_SHADOW`)

The route answers its per-draw render-state queries (the selector's
`ZENABLE`/`ZWRITEENABLE`, the gate-4 `ALPHABLENDENABLE`, `ALPHATESTENABLE`,
`SRGBWRITEENABLE`, `COLORWRITEENABLE`, and `COLORWRITEENABLE1/2` saved around
RT1/RT2) from a shadow fed by the light `SetRenderState` hook
([live-motion-route.md](../architecture/live-motion-route.md#engine-boundaries-and-state-shadow-2026-09-12)).
The regular script now also exercises the shadow's resynchronization rules:
frame 7 captures a second state block with blending off, enables blending
through `SetRenderState` and applies the block (blend off again without a
`SetRenderState` call; the next draw B routes only if the shadow dropped the
recorded TRUE); frame 8 records a `ZWRITEENABLE = FALSE` between
`BeginStateBlock`/`EndStateBlock` that is never applied and reads the state
back (one new fixture check, hence 31/91 instead of 30/90); the burst script
writes `COLORWRITEENABLE1 = 7` while the route holds RT1 in lazy mode, routes
a draw under it, reads it back (must be 7: the lazy-mode hole, closed by the
`SetRenderState`/`GetRenderState` hooks in lazy mode with the shadow on or
off) and restores 15 (nine new checks, 77/32 instead of 68/23).

Six runs repeat with the shadow off (`production-shadow-off`,
`seam-shadow-off`, `seam-taa-shadow-off`, `seam-lazy-shadow-off`,
`seam-burst-perdraw-shadow-off`, `seam-burst-lazy-shadow-off`) and must equal
their shadow-on twins in colour hashes, pre-boundary colour, readback files,
per-draw route decisions (gate/routed/matched per draw), checks,
restorations, motion/matched/RT2 pixels, `set_rt` and `lazy_flushes` per
frame: all identical. The DLL's per-frame counters, summed over the nine
logged frames (0 by telemetry, 1–8 captured):

| Script | Route state queries | Native `GetRenderState`, shadow off | Native, shadow on | Shadow hits | Resyncs |
| --- | ---: | ---: | ---: | ---: | ---: |
| regular (production, seam, seam TAA, seam lazy) | 169 | 295 | 144 | 151 | 3 |
| burst per-draw (seam) | 423 | 549 | 126 | 423 | 0 |
| burst lazy (seam) | 395 | 521 | 126 | 395 | 0 |

With the shadow on the native reads are exactly the sentinel fill's
14-state save per frame (`rs_gets = 14 + queries − hits`, asserted per
frame) plus the refills after a resynchronization (frame 7: two state block
Applies, 10 misses; frame 8: `EndStateBlock`, 8 misses; every other frame
has zero misses, i.e. zero `GetRenderState` from routed-draw evaluation);
with it off every query is a native read (`rs_hits = 0`). In the burst
script the per-draw evaluation's 47 (per-draw) or 43 (lazy: the write masks
are saved at the three binds, not per draw) queries per frame all hit.

#### `motion_route` draw state (capture frames only)

Every `motion_route` line ends with the draw's state signature, appended after
`result=` so a capture frame shows why a draw was refused at gate 4
(`DrawState`) without a new run: `zwrite=` (`ZWRITEENABLE`), `blend=`
(`ALPHABLENDENABLE`), `src=`/`dst=` (`SRCBLEND`/`DESTBLEND`), `atest=`
(`ALPHATESTENABLE`), `mask=` (`COLORWRITEENABLE`), `sepalpha=`
(`SEPARATEALPHABLENDENABLE`) and `fog=` (`FOGENABLE`). The values are the raw
D3D9 integers; a source-over draw is `blend=1 src=5 dst=6`.

They are read from the render-state shadow only - no `GetRenderState`, so the
line adds no device call and cannot move the `rs_queries`/`rs_hits`/`rs_gets`
counters a fixture checks - and a state the shadow has not seen is logged as
`-1`. `src`, `dst` and `sepalpha` live in the composition blend shadow, which
is maintained only while a composition producer is requested (linear emission
or distance fade); with neither requested they are `-1`. The shader-side b0 fog
enable and `AlphaValue` are *not* logged: the proxy tracks no boolean or
material float constants, and adding that tracking would be a new per-draw
read. `verification/probe/motion_route.py` parses the line (unknown state ->
`None`, older lines without the fields parse with every state unknown) and
`census()` groups gate-4 refusals by pair and signature;
`verification/analysis/test_motion_route_parse.py` covers both and checks the
DLL's format string against the parser's field list.

### Mip LOD bias (`X3M_TAA_MIP_BIAS`)

The `mipbias` script and the regular-script twins with the bias on are
recorded in [taa-mip-bias.md](taa-mip-bias.md): the bias sits on exactly the
mip-mapped stages of routed draws, every restore point clears it, the unset
and `0` runs are byte-identical, and the routed material draw samples finer
levels of a LOD-ramp texture (twice as far at −1.0 as at −0.5).

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
  frame). 165 fixture checks.
- `seam-taa-camera-sentinel1-on` (switch off): policy 1 everywhere, no
  camera cut (frame 7 keeps its history), and the colour of every frame is
  bit-identical to `seam-taa-on` (no camera installed) — which itself is
  bit-identical to the pre-change run (compared against the previous
  `motion-output-summary.json` hashes during development). 164 checks.
- `seam-taa-camera-sentinel2-on` (strict, camera readable): identical to
  auto (nothing skipped). 165 checks.
- `seam-taa-sentinel2-nocamera-on` (strict, no camera): every frame is
  attempted and skipped (`taa_skip=9`, `taa_resolved=0`), no debug
  readbacks, the colour equals the jittered raster. 146 checks.
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
median 0.355 ms (resolve off) versus 0.736 ms (on) at 1280x768 and 0.486
versus 2.243 ms at 5120x1440, i.e. **0.381 ms** and **1.757 ms** for the
resolve, its copies and the copy-back, CPU-inclusive (the run before the
state shadow and the scene hook: 0.351 / 0.738 and 0.487 / 2.243 ms, deltas
0.387 / 1.756 ms; earlier program: 0.320 / 1.623 ms from boundaries of
0.683 / 2.239 ms; the differences are within the run-to-run spread of these
EVENT-synchronized samples). Per-case results, the camera decisions per
frame, the environment-map counters, the shadow A/B counters and the hook
verdicts: `verification/results/motion-output-summary.json` (`cases`,
`camera`, `state_shadow`, `scene_hook`).

### Engine scene-end hook (`X3M_SCENE_HOOK`)

The `hook` script (seam, TAA, three runs: `X3M_SCENE_HOOK=1`, `=0` and unset,
the last being the default since review 26, on with the route) verifies the callsite patch of
`src/proxy/scene_hook.cpp` on the fixture's own code
([camera-state-and-frame-routine.md](../reverse-engineering/camera-state-and-frame-routine.md#implemented-hook-scene-end--compositing-begin-2026-09-12)):
the fixture VirtualAllocs a frame-routine stub that saves the callee-saved
registers, loads ESI/EDI/EBX/EBP with markers, `CALL`s a fake compositor
through a five-byte `E8 rel32` site, stores the four registers after the
call and returns; two more sites hold an `E8` to another function and five
NOPs. The seam export `x3m_scene_hook_fixture_install(site, target)` runs
the production `patch` (identity gate bypassed, everything else identical:
the `E8` check, the rel32-resolves-to-target check, protect/write/flush/
restore); the production `initialize` also runs in this process and refuses
with `executable_mismatch` (logged), so the loader never patches the fixture.
The compositor records the signal count at entry and, in "glow on" frames,
issues the frame routine's depth unbind and bloom `StretchRect`; in "glow
off" frames it issues nothing. Seven frames: glow on, glow on, a frame whose
only signal arrives in the selector's Background phase (before the depth
Clear) with the compositor then called directly, glow on, glow off, glow off,
glow on. Checks (134 with the patch, 119 unpatched, 28 restoration
comparisons with zero differences, the motion/depth oracle on all 7 frames,
14,906 matched pixels):

- install refused on the `CALL` to another target (`target_mismatch`) and on
  the NOP site (`callsite_mismatch`), with every byte of the three sites
  unchanged; with `X3M_SCENE_HOOK=1` the verified site is patched to an `E8`
  whose target lies in the DLL (`active`), a second install is refused while
  the patch is live, and shutdown restores the original five bytes
  (`restored`; a second shutdown is a no-op);
- exactly one signal per callsite call, none once unpatched, the signal
  precedes the compositor (its entry count equals the previous count plus
  one), and ESI/EDI/EBX/EBP carry their markers across the patched call;
- the resolve at the hook: with the patch, every frame resolves
  (`source=hook`, except the outside-Scene frame, which the copy path
  resolves as the fallback, `source=stretchrect`, with one logged
  `motion_output_scene_hook_disagreement` and `scene_end_check=4`), the
  bloom copy receives the resolved main target, the 8-bit image equals the
  reference resolve of the same inputs byte for byte and the DLL's FP16
  debug readback equals the reference FP16 output on frames 1–6; the
  glow-off frames resolve at the hook with history (`scene_end_check=2`
  HookOnly; 314 and 324 changed pixels), the glow-on frames report
  `scene_end_check=1` Agree with zero draws after the hook;
- unpatched (`X3M_SCENE_HOOK=0`, `scene_hook=0`, loader status `disabled`):
  the glow-on frames resolve at the copy (`scene_end_check=3` StretchOnly,
  not a disagreement without the patch), the glow-off frames are not
  attempted (`taa_skip=2`, `source=none`, the 8-bit target untouched) and
  drop the history, so 3 history frames instead of 6;
- across the two runs the pre-boundary raster is identical in every frame
  and the presented colour is identical in frames 0–3 (the frames both
  resolve, at the hook and at the copy respectively: 0, 211, 186 and 299
  changed pixels in both) and differs in 4, 5 (hook only) and 6 (history
  only with the hook): the resolve at the engine boundary is
  indistinguishable from the resolve at the bloom copy.

The trace also shows the `scene_hook active=0 status=executable_mismatch`
line of the production install path with the switch on or unset (the
default fails closed on the fixture executable exactly as on any other
executable; `seam-taa-hook-default` is byte for byte the `hook-on` run
otherwise). Every other case sets `X3M_SCENE_HOOK=0` explicitly. The game's
own callsite is not exercised here; iteration 10 confirmed it in gameplay
(214/214 agreement) and review 26 made it the default.

### FP16 HDR scene path, stage 1 (`X3M_HDR`)

Sixteen runs of the suite enable the FP16 redirect: twins of the regular,
ownership, TAA, hook and environment-map scripts compared per pixel against
the runs without the switch (equal to within one 8-bit code on lit material
pixels, everything else identical, RT1/RT2 readbacks byte-identical), the
value script (2.0 + 8.0 additive read back as 10.0 from the FP16 target, the
presented frame clamped, alpha carried; the in-range (0.75, 0.25, 0.375,
0.625) presented as the exact codes 191/64/96/159; across a Reset with a
dimension change, a Reset issued while the redirect is active and a
mid-scene RT0 switch), the injected-fault ladder script (write-back rungs,
device lost, target creation, latch bind, recovery self test, a failed
latching Clear) and two forced-absent capability runs, plus four bench
runs. Every frame's presented
image is dumped as `presented_<frame>.bgra8` beside the fixture in every
mode (one fixture check per frame, hence the +12/+9/+5/+7 check counts since
this record's earlier tables). Numbers and findings:
[hdr-scene-path verification](hdr-scene-path.md).

### Native-Windows fixes (pre-review 28: quad vertex program, copy mode, MSAA)

Three seam runs cover D1–D3 of the
[native-Windows audit](../architecture/native-windows-audit-2026-09-12.md).
`seam-taa-quad-fvf` sets `X3M_FIXTURE_QUAD_FVF=1`, a switch compiled only into
the seam DLL and the fixture's reference pass (`X3M_QUAD_FVF_SWITCH`), so
every proxy quad draws through the previous XYZRHW fixed-function path instead
of the vs_3_0 pass-through; it must equal `seam-taa-on` byte for byte in
presented frames, pre-boundary colour, RT1/RT2 readbacks, FP16 history files
and check counts. `seam-taa-copy-draw` sets `X3M_FIXTURE_STRETCH_FAULT=1`, which
fails the attach-time StretchRect round trip (`taa_copy=draw
taa_stretch_test=fault`) so the route copies the 8-bit target to FP16 and back
by same-format StretchRect plus identity draws while the reference pass does
the same; its FP16 history must equal the stretch twin's byte for byte and the
presented frames stay within one code (identity expected). `seam-msaa` creates
the device with a 2-sample back buffer (`X3M_FIXTURE_MSAA=2`) and presents three
plain frames: the selector never latches the multisampled RT0, the route logs
`motion_output_msaa_refused` once, every frame line carries `msaa=2` with
nothing routed, jittered or filled, no RT1/RT2 is created and the resolve skip
is 11 (Msaa). The device line now reports `taa_copy=`, `taa_stretch_query=`,
`taa_stretch_test=` and `quad_fvf=`; the pass holds four device references
after its lazy initialization (resolve, copy and quad vertex programs, the
declaration; five with the sharpen). Record (Steam bottle, 97 cases + 26
benches PASS; X3 bottle the same): `seam-taa-quad-fvf` identical to
`seam-taa-on` (12 presented frames, 16 RT1/RT2 readback files, 8 FP16
history files, 164 checks / 51 restorations each); `seam-taa-copy-draw`
history identical and presented frames 49,152 of 49,152 pixels exact
(`max_code_difference=0`); `seam-msaa` `motion_output_msaa_refused device=1
frame=0 msaa=2 width=64 height=64`, three frame lines `msaa=2 routed=0
jittered=0 taa_skip=11`, 5 checks. Summary key `native_windows`.

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
| plain | production off / on | 31 / 31 | 39 / 39 | – | – | – | 48,242 |
| plain | seam off / on | 31 / 91 | 39 / 39 | – / 44,284 | – / 10,261 | – / 27,170 | 48,242 |
| ownership | production off / on | 31 / 31 | 39 / 39 | – | – | – | 48,242 |
| ownership | seam off / on | 31 / 91 | 39 / 39 | – / 44,284 | – / 10,261 | – / 27,170 | 48,242 |
| depth | production off / on | 31 / 31 | 39 / 39 | – | – | – | 48,242 |
| depth | seam off / on | 31 / 91 | 39 / 39 | – / 44,284 | – / 10,261 | – / 27,170 | 48,242 |
| admission | production off / on | 31 / 31 | 39 / 39 | – | – | – | 48,242 |
| admission | seam off / on | 31 / 91 | 39 / 39 | – / 44,284 | – / 10,261 | – / 27,170 | 48,242 |
| plain, jitter | production on / seam on | 31 / 91 | 39 / 39 | – / 44,279 | – / 10,348 | – / 27,293 | 48,957 |
| plain, TAA | production on / seam on | 71 / 152 | 51 / 51 | – / 44,279 | – / 10,348 | – / 27,293 | 48,957 |
| ownership, TAA | production on / seam on | 71 / 152 | 51 / 51 | – / 44,279 | – / 10,348 | – / 27,293 | 48,957 |
| plain, lazy RT mode | production on / seam on | 31 / 91 | 39 / 39 | – / 44,284 | – / 10,261 | – / 27,170 | 48,242 |
| ownership, lazy RT mode | seam on | 91 | 39 | 44,284 | 10,261 | 27,170 | 48,242 |
| plain, TAA, lazy RT mode | seam on | 152 | 51 | 44,279 | 10,348 | 27,293 | 48,957 |
| burst, per-draw / lazy | production | 32 / 32 | 18 / 18 | – | – | – | 35,505 |
| burst, per-draw / lazy | seam | 77 / 77 | 18 / 18 | 31,932 | 0 | 21,447 | 35,505 |
| shadow off, regular | production on / seam on | 31 / 91 | 39 / 39 | – / 44,284 | – / 10,261 | – / 27,170 | 48,242 |
| shadow off, TAA / lazy | seam on | 152 / 91 | 51 / 39 | 44,279 / 44,284 | 10,348 / 10,261 | 27,293 / 27,170 | 48,957 / 48,242 |
| shadow off, burst per-draw / lazy | seam | 77 / 77 | 18 / 18 | 31,932 | 0 | 21,447 | 35,505 |
| hook script, patched / unpatched | seam, TAA | 134 / 119 | 28 / 28 | 25,811 | 14,906 | 17,367 | 28,553 |

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

Adjacency run (added 2026-09-12): the exact-equality replacement of D3DX's
epsilon welding ([mesh-adjacency-fast.md](mesh-adjacency-fast.md)) is switched
with `--mesh-adjacency` (requires `--telemetry`). Use `verify` first: it keeps
the native result and compares; the last `mesh_adjacency_metric` line of the
session log must show `verify_mismatched=0`, and `native_us` versus `fast_us`
there is the measured speed-up on the game's own meshes. Then repeat with
`fast`:

```sh
python3 tools/manage.py launch --direct --telemetry --profile --mesh-adjacency verify
python3 tools/manage.py launch --direct --telemetry --profile --mesh-adjacency fast
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
The engine scene-end hook is exercised on the fixture's own `E8` callsite
through the seam; the game's `0x004721b1` patch is verified for its expected
bytes and identity gate only, not in gameplay, and stays off by default.

## TAA invalidation-site diagnostic (2026-09-14)

Diagnosis of the `camera_state reason=3` clusters of user runs 11 and 14
(history dropped on 15.1 % / 11.2 % of gameplay frames in a fast-translating
segment; absent in run 15): named from the existing logs as the
`cutout_missed` site (`after_draw`, `cutout::missed`: a source-over blended,
alpha-tested draw of the cutout pair refused at gate 4 every frame), a
documented conservative rule, not a cut-detector or camera-read defect. The
evidence and the policy question are in
[live-motion-route.md](../architecture/live-motion-route.md#temporal-resolve-at-the-bloom-copy-temporal-step-3).

Change: every `invalidate_taa` caller is tagged (`TaaInvalidateSite`, 20
sites), the route logs `taa_invalidate device= frame= site=` at most once per
frame per site (integer-only at the call; flushed at the frame end and at a
frame begin), and `camera_state` carries `prev_valid_at_policy=`. The runner
asserts the exact per-frame site sets of every 12-frame TAA case
(`{9: {reset}}`; strict skip `{skip, not_resolved}` on all 12 frames) and of
the environment-map script (`not_resolved` on frames 1 and 3 only), and
`prev_valid_at_policy` against the decision reason on every `camera_state`
line.

| Check | Result |
| --- | --- |
| `python3 verification/probe/check_no_x87.py build/d3d9.dll` | PASS, 224 reachable functions, no violations (the sites on the light setter paths stay integer-only) |
| Host tests (`test_motion_route_parse`, `test_camera_state_analysis`, `test_shimmer_trace`, `test_motion_output_runner`) | 28 tests OK |
| `run_motion_output.py` full suite (bottle X3) | FAILS at the first case, `production-on` frame 0, `check_render_state` (`rs_queries=22 rs_hits=20`, no resync), before any TAA case: pre-existing since the last suite run at 2e5f1af (motion_output.cpp changed by 2,467 lines since, including the two `mark_cutout_candidate` state reads on refused scene draws); this change adds no render-state query |
| Diagnostic partial (retained binaries of that build, `check_render_state` bypassed, `motion-output-partial.json`) | 9 TAA cases pass with the new assertions: production-taa-on 83 checks, seam-taa-on 164, seam-taa-camera-on 177, camera-sentinel1 176, camera-sentinel2 177, sentinel2-nocamera 158 (12 × {skip, not_resolved} + reset at 9), envmap 67 (not_resolved at 1 and 3), hook-on 141, hdr-tonemap-on 140 |

Open: the `production-on` render-state count regression needs its own owner
(the suite has not passed since the cutout arm merged); the `cutout_missed`
policy for known-blended cutout draws (whole-frame history drop versus the
ordinary native fallback) is the orchestrator's decision.

## Live suite repaired on main (harness only, 2026-09-14)

The suite passes again: 98 cases, 9,774 fixture checks, `motion-output-summary.json`
`passed: true`. Four harness defects had accumulated behind the first failure;
no production source was touched (the `invalidate_sites` / `prev_valid_at_policy`
assertions of the invalidation-site diagnostic are unchanged).

| Symptom | Cause | Harness repair |
| --- | --- | --- |
| `production-on` frame 0 `rs_queries=22 rs_hits=20`, no resync | the per-routed-draw wrap save reads WRAP4/WRAP5 (the route's motion and depth texcoord indices); with `X3M_FIXTURE_WRAP=0` no fixture setter ever wrote them, so the first read of each was a cold shadow miss. Frames 1-8 were 22/22. Per-draw query set: ZENABLE, ZWRITEENABLE, ALPHABLENDENABLE, ALPHATESTENABLE, SRGBWRITEENABLE, COLORWRITEENABLE, COLORWRITEENABLE1/2, WRAP4, WRAP5 - ten distinct states, none read twice | `seed_wrap_states()` in `frame_begin` writes the sixteen WRAP states with their D3D9 default 0 before the frame's first draw (device state unchanged, application shadow warm); skipped when the script owns the WRAP states |
| `seam-hdr-ramp-*` `meter=1 meter_reason=ok` against an expected `0` | 75dbbed sets `HdrConfig::allow_auto_toggle` unconditionally, so `meter_requested()` holds in manual-EV mode and the meter capability is prepared | expectation is now `('1','ok')` with the tonemap, `('0','off')` without it, and `('0','self_test')` under `decode=none` |
| `seam-hdr-exposure` `ev_max` 1.50 against the reference default 2.0 | the DLL's own `params.ev_max` default is 1.5 (75dbbed, "the milder AUTO appearance"); the runner still clamped its reference adaptation at `exposure_ref.EV_MAX` | `HDR_EV_MAX_DEFAULT = 1.5` in the runner; `seam-hdr-exposure-offset` pins `X3M_HDR_EV_MAX=2`, the ceiling its level stimulus was designed for (with 1.5 and the +1 EV offset levels A-C clamp onto the grey target) |
| `seam-taa-mipbias-on` 176 checks against 164 | 39b31d5 added one check per frame at the bloom copy ("the DLL's mip bias sits on exactly the expected stages") | `expected_checks += 12` for a live bias in the TAA script, and the mip-bias twin comparison allows exactly that difference |

| Check | Result |
| --- | --- |
| `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py` | PASS, 98 cases, 9,774 checks (`build/d3d9.dll` b6f8dbb8..., seam b73ca62a..., fixture 4f414bb8...) |
| `python3 verification/probe/check_no_x87.py build/d3d9.dll` | PASS, 224 reachable functions, no violations |
| Host tests (`test_motion_output_runner`, `test_motion_route_parse`) | 16 tests OK |

Open: under `X3M_HDR_DECODE=none` the HDR meter self-test's GPU level-0 value
disagrees with `meter_level0()` by more than 1e-4 and the pass refuses the meter
(`meter_reason=self_test`); every other decode passes. Auto exposure would be
off in that configuration. Production question, not a harness one.

## Blended cutout pass is not a coverage miss (2026-09-14, worktree)

Root cause of the run 11/14 distant shimmer (finding above): the game's
source-over pass of the cutout pair (`blend=1 src=5 dst=6 atest=1 mask=7
zwrite=0`), refused at gate 4 every frame the object is in view, counted as
`cutout::missed` and dropped the frame's TAA history (11–15 % of frames).
Decision implemented (review applied): the exact source-over triple
(ALPHABLENDENABLE on, SRCBLEND SRCALPHA, DESTBLEND INVSRCALPHA, all known;
`cutout::source_over`, independent of ZWRITEENABLE by design) is not a
candidate; any other or unknown factor stays a conservative miss.
`mark_cutout_candidate` reads the warm ALPHABLENDENABLE shadow slot and the
factors from the composition blend shadow when maintained, otherwise one native
GetRenderState each on that draw; the shadow-off configuration adds one
GetRenderState per refused cutout-pair draw. The opaque miss rule is
unchanged. The reactive-rectangle follow-up stays open
([alpha-tested-materials.md](../architecture/alpha-tested-materials.md)).

Fixture: `X3M_FIXTURE_CUTOUT_SCRIPT=blended|opaque` in
`motion_output_cutout_inc.h` (twelve static frames, pair 0, one refused draw
per frame; frame 0 is arm-inactive because the capability verdict lands at the
frame's HDR latch, after the `begin_frame` latch). Plan 35 of the 70-plan
cutout suite (`wrong=3`, ALPHABLENDENABLE on with ONE/ZERO) still misses.
Command of record for that suite (seam DLL): `X3M_FIXTURE_BOTTLE=X3 python3
verification/probe/wine_lock.py python3 verification/probe/run_linear_cutout_live.py
--fixture verification/probe/build/motion_output_fixture.exe --dll
verification/probe/build/motion-output-seam/d3d9.dll`; reviewer's run on the
first revision: PASS, 13 runs, 2,111,281 checks, `paired_cost_qualified`,
`linear-cutout-live.json`. Rerun on the review revision (exact source-over key):
PASS, 13 runs, 2,111,281 checks, `paired_cost_qualified` (`build/d3d9.dll`
6619baf4..., seam b9a4cd2d...); the two cutout twins re-pass with the same
numbers (blended 49,624 checks, history 10/12, 0 `cutout_missed`; opaque
49,612 checks, history 0/12, `cutout_missed` frames 1–11) and `check_no_x87.py`
PASS (224 reachable functions). The full suite was not rerun after the review
revision; the review-applied change is confined to the exemption key.

| Check | Result |
| --- | --- |
| Before (HEAD production, new fixture script) `run_motion_output.py seam-taa-cutout-blended seam-taa-cutout-opaque` | blended fixture aborts at frame 1: `CHECK cutout actual routing counters FAIL` (`missed_delta` 1 where the script expects 0) |
| After, `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py` | PASS, 100 cases + 26 bench, 109,010 checks; `seam-taa-cutout-blended` 49,624 checks, history on 10 of 12 frames (frame 0 and the scripted cut current-only), `invalidate_sites {}`, 1,536 accepted pixels, `routed_delta=0 missed_delta=0` every frame; `seam-taa-cutout-opaque` 49,612 checks, history 0 of 12, `cutout_missed` on frames 1–11, `unavailable=1` (`build/d3d9.dll` 6b0a2205..., seam 48ab1ec5..., fixture ed87f1cb...) |
| `python3 verification/probe/check_no_x87.py build/d3d9.dll` | PASS, 224 reachable functions, no violations |
| Host tests `test_motion_output_runner`, `test_motion_route_parse`, `test_linear_cutout_contract` | 19 tests OK (contract 41 scenarios, 278 checks: known-blended not a candidate / not a miss, blend-off still misses, unknown blend conservative) |

## User run 19 (run47): the cutout exemption holds, the asteroids do not (2026-09-14)

Acceptance evidence for the cutout-miss exemption above, from user run 19
(snapshot `/tmp/x3-bottleX3-run47/`, log `session-20260914-225307-216.log`,
installed DLL `ab6e17ba…` from `5d06316`; log queried, never read whole).

| Measure | Run 19 | Reference |
| --- | --- | --- |
| `camera_state` lines | 24,729 | — |
| reason distribution | `{0: 24190, 1: 529, 3: 3, 4: 6}` | — |
| `reason=3` (history dropped) | 3 lines, 0.01 % | 15.1 % (run 11), 11.2 % (run 14) |
| `taa_invalidate` sites | `not_resolved` 529 only; `cutout_missed` 0 | run 11/14: the `cutout_missed` clusters |
| `linear_material_frame` final counters | `cutout_missed=0 cutout_routed=0` | — |

The exemption itself has no log token of its own (`src/proxy/linear_cutout.h:48`,
applied in `motion_output.cpp` near line 835), so it is confirmed indirectly: no
`cutout_missed` site and no `cutout_routed` draw in a session where run 11/14
produced them on 11–15 % of frames.

Not fixed by it: the user still sees distant asteroids lose triangles that
reappear (`screenshots/asteroids-shimmer1.png`, zoomed view). The far asteroids
draw the native fog pass — `motion_route gate=4 routed=0 matched=0`; frame 18555
holds only such draws and 14639 is mixed — and the route-scoped depth cannot show
holes in a pass the route never owns, so this run cannot localise the dropout.
The diagnosis stays open under
[asteroid-fog-temporal.md](../reverse-engineering/asteroid-fog-temporal.md).

## z_only depth-prepass jitter: live fixture on the merged tree (2026-09-14, night)

Commits `0a3a2db` (fix, reviewed on Fable: merge-clean, four low findings fixed in `1d49489`) and
`1d49489`. Bottle X3, CrossOver Preview, `X3M_FIXTURE_BOTTLE=X3`, `wine_lock.py` wrapper, lock free.

- `run_motion_output.py production-zonly seam-zonly`: exit 0, 31.1 s; checks 73 and 109;
  `capture_frames=[1,2,3]`, `routes=6`; every `motion_output_frame` line
  `jittered=2 routed=0 unjittered_depth_writers=0`; ZONLY holes 0 on frames 0, 1, 3, 5, 7, 8 and
  2425 / 2414 / 2418 (= pixels) on the control frames 2, 4, 6. The first run of the same cases had
  the same GPU result and failed only in `validate_zonly`, which expected route lines outside the
  capture window (`Reset` after frame 3 stops the capture); the validator now pins the window.
- Full default suite: 118 cases, all `exit=0`, `{"passed": true, "status": "PASS"}`, 3 min 55 s.
  Results local under `verification/results/bottle-X3/motion-output-summary.json`.


## User run 20 (run48): the asteroid dropout is gone (2026-09-15)

User run 20 (snapshot `/tmp/x3-bottleX3-run48/`, log
`session-20260915-002408-212.log`, 335 MB, installed DLL `39b090d0…` from
`77a649b`, record `verification/results/run20-candidate-install.json`; log
queried, never read whole). This is the first gameplay run of the z_only
depth-prepass jitter.

**The user reports no asteroid shimmer at all** — the triangles that vanished
and reappeared in runs 11, 14 and 19 (`screenshots/asteroids-shimmer1.png`) do
not do so on this build. The dropout tracked in
[asteroid-fog-temporal.md](../reverse-engineering/asteroid-fog-temporal.md) is
therefore **fixed and accepted in game**.

| Measure | Run 20 | Run 19 (run47) |
| --- | --- | --- |
| `motion_output_frame` lines | 924, `unjittered_depth_writers=0` on all | — |
| `camera_state` lines | 50,655 | 24,729 |
| reason distribution | `{0: 50230, 1: 418, 3: 4, 4: 2}` | `{0: 24190, 1: 529, 3: 3, 4: 6}` |
| `reason=3` (history dropped) | 4 lines, 0.008 % | 0.01 % |
| `taa_invalidate` sites | `not_resolved` 418 only | `not_resolved` 529 only |

The ten F8 capture groups (8 frames each, first frames 4817, 8791, 10663, 23902,
43314, 43990, 44490, 45156, 47022, 47433) carry both the prepass
`vs=c78b4c68a87fce74` and the fog `vs=167eb2d5629ab9d3` in six of the ten. In
the 47022–47029 group every prepass line is `gate=3 jittered=1` and every fog
line `gate=4 jittered=1`; run 47 had the prepass at `jittered=0`. That is the
route-side change the fix was built for, observed in the game.

Limits of the log evidence: no group holds a camera still on a far asteroid
(`camera_rotation_deg` ≈ 1° in all ten), so no pixel-flip comparison across the
8 pre-resolve frames is conclusive, and the fix rests on the user's in-game
acceptance plus the jitter gating above, not on a captured before/after pixel
pair.


## User run 21 (run49): a station section trembles at ~4.7 km (2026-09-15)

User run 21 session A (snapshot `/tmp/x3-bottleX3-run49/`, log
`session-20260915-010311-212.log`, 495 MB, installed DLL `39b090d0…` from
`77a649b`; log queried, never read whole). The asteroid dropout accepted in run
20 did not return, but the user reports a **new symptom**: one section of a
station — the hangar/dock bay area, `screenshots/jitter1.png` — trembles up and
down at about 4.7 km in chase view, and the trembling stops as the camera comes
closer.

Triage of the six capture-group frames (4900, 5485, 9889, 11940, 12213, 13181;
`target_id 1173`, an asteroid, in all of them):

| Measure | Run 21 |
| --- | --- |
| `motion_output_frame` `unjittered_depth_writers` | 0 on all six frames |
| Routed station nodes 58844–58851 and 58872 | `gate=0 jittered=1` on every line (`vs=4944d81dfe531b37`, PS `0c1f3f0f440e4a0c` / `ca6bfa4a6cca7e2a` / `c30104cb0efb6675`) |
| Docking-port pair (`ps=64bac8bb307eb896`) | `gate=4 jittered=1` |
| `motion_route` rows with `jittered=0 node=00000000` | up to 13 per frame |
| Node handles 58245, 58246, 60278–60280, 51946–51948 | never appear in any `motion_route` row |

So the station geometry the route does own is jittered consistently, and nothing
in the route writes depth unjittered; the candidates for a sub-object that moves
against its neighbours are the unattributed `node=00000000` draws and the node
handles that the route never sees at all. That is not enough to name the
mechanism — a same-draw jitter mismatch, a per-node transform seam and an LOD
switch at that distance all fit the rows above.

Diagnosis is in progress on Fable; it will name and own the note where the
mechanism is written up.


## User run 22 (run51): the trembling is gone (2026-09-15)

User run 22 (snapshot `/tmp/x3-bottleX3-run51/`, log
`session-20260915-030036-468.log`, large; queried, never read whole; installed
DLL `53a0d8a7…` from `509a273`, record
`verification/results/run22-candidate-install.json`). This is the first gameplay
run of the fade-band motion arm.

**The user reports no trembling anywhere** — the station hangar/dock section
that moved up and down at ~4.7 km in run 21 (`screenshots/jitter1.png`) is
steady on this build, near and far. The symptom tracked in
[asteroid-fog-temporal.md](../reverse-engineering/asteroid-fog-temporal.md),
"Run 49", is therefore **fixed and accepted in game**.

| Measure | Run 22 (run51) | Run 21 (run49) |
| --- | --- | --- |
| `linear_material_frame` lines | 507 | — |
| `fade_routed` | sums to 860 (0–5 per frame) | route absent (masked) |
| `fade_refused` / `fade_held` | 7 / 0 on every line | — |
| `motion_output_frame` `unjittered_depth_writers` | 0 on all 507 lines | 0 on all six frames |
| `camera_state` reason | 493×`0`, 14×`1` | `{0: 24190, 1: 529, …}` |
| `taa_invalidate` sites | `not_resolved` 823 only | `not_resolved` only |
| draws per frame | mean 179.1, max 1216 | mean 165.1 |

So the arm ran on every frame it was meant to: a small number of fading draws
per frame took the routed path, seven estimates were refused (camera invalid),
the hysteresis never had to hold a node, and nothing in the route wrote depth
unjittered.

Frame cost is **not** decided by this run. `frame_end` steady state is about
15.9 ms/frame against 9.0 ms in run 49, but the user flew a different route with
more draws per frame (179.1 vs 165.1, peaks to 1216), and the build also carries
the LOD scale at 2×. The difference is confounded and is not attributable to the
LOD scale; run 23 repeats the run-49 route for a controlled before/after
([lod-scale.md](../architecture/lod-scale.md), "In game (run 22)").


## 2026-09-15 — fade-band arm: live cases repaired, hysteresis, sentinel and hover scripts

The merged fade-band arm (6a895ee) failed its own live case for harness
reasons: the asteroid VS maps `oT0` through the c37/c38 rows, whose live-fixture
values sample one texel of Q's ramp, and the masked composite (shader alpha
.078) carried under one 8-bit code per pixel. Q now gets identity UV rows, alpha
1 and a camera at the origin; the resolved shift is measured on the reference
FP16 resolve the fixture asserts equal to the presented frame. Review findings
folded in: a 100 ‰ per-node hysteresis band (`fade_route::Hysteresis`,
`fade_held` counters), the camera-invalid estimate refused instead of using the
view depth, the sentinel-policy wording corrected, and two new scripts. Design
and numbers: `docs/architecture/linear-distance-fade-region.md`, "Fade-band
route". Five live cases on X3 with the seam DLL: routed / routed-perdraw /
sentinel 5100 checks, raw error ≤ 0.006 px, resolved residual ≤ 0.069 px;
masked 408 checks, resolved residual 0.000 px; hover 3536 checks, 8 routed
frames of which 5 held, switch step 3.7 % at 449 ‰. `run_linear_distance_fade_live.py`
PASS, 34 cases. Host: 49 tests OK across the runner, fade-region and
live-report modules.


## 2026-09-16 — hybrid unhook (state-call-fast-path.md step 5)

Production no longer hooks `SetRenderState`/`SetSamplerState`; the route reads
its draw-time state with `GetRenderState`/`GetSamplerState` once per state per
draw and restores the mip bias right after each routed draw. `X3M_STATE_SHADOW=1`,
lazy RT mode, `X3M_FRAME_TIMING=1` and a failed Get* capability check keep the
hooks; the per-frame summary reports `rs_mode=get|shadow|native`. The six
shadow-off twins are identical to their shadow-on twins and to the committed
record (`state_hashes`, `motion_pixels`, colour, route decisions, checks,
restorations); four of them run in `get` mode, the two lazy ones in `native`.
Benchmark (committed `state-hook-benchmark-hybrid.json`, DLL `c136e425…`):
production SetRenderState 15.0 ns and SetSamplerState 10.4 ns (native
14.1/11.1, hooked 78.8/67.7), the ten-read per-draw set 90.9 ns, a routed draw
with two biased stages 78.0 ns (native 78.3); the mip-bias apply/restore per
routed draw is modelled at two stages, the fade-mask pairs bind up to five
(projection about 0.2 ms per 987-draw frame at five stages, not measured).
Review follow-up on the merged tree: eighteen production-DLL cases now run
the production configuration (`X3M_STATE_SHADOW` unset) and the new
`seam-taa-cutout-opaque-get` case proves the per-draw cache (426 queries, 66
hits over twelve frames); all nineteen equal the committed record. Numbers,
the A/B of the four drifted HDR cases and the harness findings (the TAA
sharpen/mip-bias pin; the fade-route and HDR drift resolved on main by
39d9863): [state-call-fast-path.md](../architecture/state-call-fast-path.md),
"Hybrid unhook (step 5, implemented)".

## User run 41 B (run125): solar-panel arrays shimmer — fade-band arm inert under original shading (2026-09-18)

Snapshot `/tmp/x3-bottleX3-run125` (run41 candidate, `X3M_LINEAR_MATERIALS=0`,
`X3M_TAA_SENTINEL=auto` → policy 2, mip bias −0.5, sharpen 0.75), bursts
22624–22631 and 24486–24493 (`screenshots/flicker1.png`). Diagnosis in
[asteroid-fog-temporal.md](../reverse-engineering/asteroid-fog-temporal.md),
"Run 125": the panel faces are the station fade pair
`4944d81dfe531b37`/`64bac8bb307eb896` drawn in the exact fade-band state at
fraction 1000 (c39/c41 = 1, b0 = 0), 16 draws per frame refused at gate 4
because `probe_cutout_caps` never ran without linear materials, so
`fade_arm_admits` failed its readiness check (`fade_permille=0` on every
record, no `fade_refused_rect`, no `linear_cutout_device` row). The faces
carry the fill sentinel (big-array box: 64k sentinel vs 32k routed pixels)
and take the far-plane camera path, which has zero flow (rotation 0.0000°)
while the routed struts beside them move 3.0–3.8 px/frame (near) and 0.2–0.5
px/frame (far): the history is fetched that far off every frame.

Fix: the probe runs when either consumer is configured (linear materials or
the fade-band threshold ≤ 1000); the cutout arm stays gated on the
linear-material request. Evidence (worktree build of `44e6b5f0` + this change,
seam DLL `32d2377f…` and fixture from `build_motion_output.sh`; one
retained-binary run of the three fade-route cases on that DLL,
`X3M_FIXTURE_BOTTLE=X3`, compact record
`verification/results/bottle-X3/fade-route-original.json`, 2026-09-18):

- `seam-taa-fade-route-original` (new: the hover schedule over the sentinel
  fill with `X3M_LINEAR_MATERIALS=0`, `X3M_LINEAR_DISTANCE_FADE=0`): the DLL's
  verdict is Ready without linear materials (`linear_cutout_device …
  verdict=1`, fixture status 30); the arm routes 8, holds 5 and refuses 4
  frames exactly as the hover tables say, without a bracket (`prepared=0`,
  `mask_valid=0`, no `linear_material_frame` line; the new
  `fade_route_frame` line carries `fade_routed/fade_refused/fade_held/
  fade_route/cutout_caps` per frame); the route records pin the blend
  triple `-1/-1/-1` (no composition shadow under original shading); raw
  shift equals the jitter delta and the resolved residual is ≤ 0.056 px (10 samples; 3461
  fixture checks) on routed and refused frames alike (a refused quad over the sentinel fill
  takes the far-plane path, exact under the rotating camera). The case was
  not run against the pre-fix DLL (the pre-fix witness is the run125 log).
- `seam-taa-fade-route-routed` and `-sentinel` (linear materials on) on the
  same DLL, in the same record: 5100 checks each, worst raw error 0.0063 px,
  worst resolved residual 0.0688 px (16 samples). The runner's
  `motion-output-partial.json` is a run artifact and stays at HEAD.
- Host: `test_motion_output_runner` (12 tests), `test_linear_cutout_contract`
  (43 scenarios, 287 checks: a configured arm probes without linear
  materials, the cutout arm stays off; neither consumer → no probe),
  `test_fade_region` (13 tests).

Open: whether the resolved panels still re-roll on the 512² grid texture with
the arm active (raw dumps only in run125; no resolved or reactive-mask dump)
is for the next user run with the candidate that carries this change.

## User run 42 B (run130): one leg still shimmers — fade-band origin behind the camera (2026-09-18)

Snapshot `/tmp/x3-bottleX3-run130` (run42 candidate `1a5dd46c`, fade route
active, original shading). Diagnosis in
[asteroid-fog-temporal.md](../reverse-engineering/asteroid-fog-temporal.md),
"Run 130": leg 2's one refused fade draw per frame (index 336, frames
5354–5361) has its object node resolved (`object_context node=1d879878
valid=127`) and the exact fade-band state, but its clip translation `w`
is −7611.6 … −8563.2 (origin behind the camera plane), which
`fade_route::origin_distance` refused; 8 of 248 fade-state rows in the run,
exactly the 8 with `w ≤ 0`. Fix: the distance is defined for any finite `w`;
every scene `motion_route` row now carries `unmatched=<reason>`.

- `seam-taa-fade-route-behind` (new: the `original` script with the quads'
  origin at `w = −1`): exit 0, 3461 checks, 10 history frames, worst raw
  error 0.004 px, worst resolved residual 0.055 px; capture rows frame 2
  `routed=1 fade_permille=449 fade_held=1 unmatched=none`, frames 3–4
  `gate=4 fade_permille=390/449 unmatched=fade_threshold`.
- `seam-taa-fade-route-original`: exit 0, 3461 checks, 0.004 / 0.055 px;
  `seam-taa-fade-route-hover`: exit 0, 3536 checks, 0.006 / 0.053 px (both
  validate the new `unmatched` field). Partial run (selected cases, no
  cross-case comparison); `motion-output-partial.json` stays at HEAD.
- Overlay arm (the distant object's glass/window sub-mesh: the hull pair
  `53a0a641…`/`63f96eba…` in the fade-band state right after the same node's
  routed draw; run131 2/frame, run130 16 rows in B3, 20/20 same-node):
  `seam-taa-fade-route-overlay` (new): exit 0, 5101 checks, 10 history
  frames, worst raw error 0.004 px, worst resolved residual 0.071 px; capture
  rows frames 2–4 `gate=0 routed=1 matched=1 node=00001000 fade_arm=1
  fade_permille=1000 unmatched=none`, `fade_route_frame overlay_routed=2
  overlay_refused=0 fade_routed=0`, depth target unchanged by each draw.
  `seam-taa-cutout-blended` (alpha-tested source-over pass, must stay native):
  exit 0, 49624 checks, 10 history frames, 0 missed frames,
  `overlay_routed=0 overlay_refused=0`; `seam-taa-cutout-opaque`: exit 0,
  49612 checks, 11 missed frames (as before); `seam-taa-fade-route-original`
  rerun on the same DLL: 3461 checks, 0.004 / 0.055 px.
- Review fixes (same day): the overlay witness is the very next draw index of
  the same frame, the node identity at gate 4 and the lifetime serial at the
  scope gate, cleared at Reset; the arm is off with linear materials
  requested. `seam-taa-fade-route-foreign` (new; P on A's node address under
  another lifetime serial, Q on its own node): exit 0, 397 checks, both
  refused every frame (`gate=4 unmatched=overlay_node`, `overlay_refused=2`),
  resolved residual 0.070 px. One run of the seven cases (behind, hover,
  original, overlay, foreign, cutout-blended, cutout-opaque) on the final
  seam DLL `8e2dd29e…` reproduced the numbers above; compact record
  `verification/results/bottle-X3/fade-route-cases.json`.
- Host: `test_fade_region` (16 tests: `w = −1` at distance 1, `w = 0` at the
  camera, off-axis `w = ±2` equal, nonfinite refused),
  `test_motion_output_runner` (12 tests, 60 HDR cases).

### 2026-09-19 — fade route behind/overlay: Fable second review

After merging main: seven fade-route cases reproduce `fade-route-cases.json` (behind 3461, original 3461, hover 3536,
overlay 5101, foreign 397, cutout-blended 49624, cutout-opaque 49612 checks; resolved ≤ 0.071 px). Blocking finding
fixed: the `linear_material_live` snippet mock lacked the `last_routed_*` latch (host module now 22 tests OK;
the module was missing from the earlier host list). Accepted as non-blocking, to read in run 43: `overlay_refused`
and `unmatched=overlay_node` also count ordinary source-over draws in the fade-band state that were never overlay
candidates; such draws now pay up to eight state reads (unmeasured); a first-frame overlay writes an unmatched
motion row for one frame; overlays take the plain motion variant (no fill or light-map gain, as the native draw);
no fixture covers a Reset between a routed draw and its overlay (checked by reading).

## 2026-09-18 — routed-draw cost bench and the depth-lease trim

Numbers and attribution: [engine-frame-time.md](../architecture/engine-frame-time.md)
§2.2 "Measured 2026-09-18". Final worktree build: production DLL `da272a78…`,
seam `d6b3ca77…`, fixture `3b6a61eb…`; pre-trim build (same tree before the
source change, fixture with the bench mode) production `25fad8dc…`, seam
`1ddfbbbe…`, fixture `dfebac3b…`. A second trim (pixel-ABI upload skip) was
built, found inert on a real device in review and removed; nothing below was
measured on it.

- New fixture mode `routebench [draws]` (`motion_output_fixture.cpp`, fixture
  only) and runner `verification/probe/run_route_bench.py` (thirteen
  configurations; `--label`, `--configs`, `--seam`, `--fixture`). Tracked
  results, X3 bottle, WineArch arm64, `FEX_X87REDUCEDPRECISION=1 WINEMSYNC=1`:
  `route-bench-before.json`, `-before-rest.json`, `-before-attr.json`
  (pre-trim attribution), `-final.json`, and the alternating A/B
  `-ab-base-{1,2,3}.json` / `-ab-final-{1,2,3}.json` (pre-trim seam via
  `--seam`). QPC pair 0.06-0.12 µs in every process.
- Pre-trim comparison, retained witness
  `verification/results/bottle-X3/route-trim-pretrim-comparison.json`: ten
  cases run on the pre-trim build and on the final build in this session
  (seam-burst-perdraw/-lazy, seam-taa-cutout-opaque-get,
  seam-ownership-shadow-replay-on/-cascades/-cascades-casters-20/-far-refused,
  seam-ownership-shadow-retention-live/-census, seam-taa-fade-route-original);
  per case the binary hashes, checks, restorations and a SHA-256 of the case
  record with timing/hash/path keys removed. `all_equal: true` (10/10): checks
  86/86/49612/203/278/278/203/9743/9740/3461 on both builds. The multistream
  refusal frame of the shadow-replay script (its verdict now comes from the
  declaration hook) is inside those records. seam-on, seam-taa-on and
  production-on also ran on the final build (exit 0; `final_only` in the
  witness).
- Against the committed record (`motion-output-summary.json`, 2026-09-15),
  measured on the intermediate build that still carried the dropped skip:
  production-off/on, seam-off/on, seam-taa-on, production-lazy-on,
  seam-lazy-on, seam-taa-hook-default, seam-taa-fade-route-routed/-masked
  equal after dropping binary hashes and the harness's later schema
  (`render_state[].mode`, `frames_changed_by_history`); the two burst cases
  differ from that record in `state_hashes` only and equal the pre-trim build,
  so that drift predates this change.
- `run_state_hook_benchmark.py --dll build/d3d9.dll` on the final DLL
  (`state-hook-benchmark-route-trim.json`): native/production/hooked
  SetRenderState 13.4/13.7/80.3 ns, SetSamplerState 13.1/11.3/70.8, ten-read
  set 94.6/94.3/93.7, mip-bias routed draw 79.8/79.4/476.9, draw pair
  345.5/1043.2/919.1; every PRESERVE line (SetVertexDeclaration and SetFVF
  included) x87/MXCSR/LastError intact. The committed
  `state-hook-benchmark-hybrid.json` (2026-09-16, DLL `c136e425…`) is a
  different build: compare magnitudes, not digits.
- `run_sun_share_live.py --fixture … --dll <final seam>`: 22 cases,
  `passed: true`.
- The runner's `motion-output-partial.json`, the two retention fixture JSONs,
  `state-hook-benchmark.json` and `sun-share-live.json` were rewritten by
  these runs and restored to HEAD.

## 2026-09-19 — run43 qualification: behind-camera origin leaked into shadow-caster admission

- Leak: the run-130 change dropped `w > 0` from `fade_route::origin_distance`,
  which `MotionOutput::note_candidate_distance` shares. A shadow candidate
  whose origin is behind the camera plane then got a distance (an origin rule)
  instead of `d = -1` (extent only). `seam-ownership-shadow-pool-hull` on main
  5360d460: `shadow_replay_candidates` frame 0 origin/slice0/fallback/managed/
  leased/c0/c1 = 2 each; accepted is 1 each (origin 1 on frames 1-5).
- Fix: `fade_route::origin_distance_front` (`rows[15] > 0.f &&
  origin_distance(...)`, the pre-run-130 contract; one SSE compare, no x87) is
  the shadow caller's entry; the fade arm keeps `origin_distance`. Shadow rule
  and `POOL_EXPECT` untouched.
- Evidence (one `wine_lock` run, bottle X3, fresh build, 27 selected cases,
  all exit 0): pool-hull 77 checks, frame 0 origin=1 slice0=1 fallback=1
  managed=1 leased=1, frames 1-5 origin=1; the other ten `shadow-pool-*` and
  eleven `shadow-replay-*` cases pass; fade-route behind 3461, original 3461,
  hover 3536, overlay 5101, foreign 397 (equal to `fade-route-cases.json`).
  Rewritten tracked results restored to HEAD.
- Host drift fixed in tests only: `cull_small_parts` mocks and
  `D3DPRESENT_PARAMETERS::BackBufferWidth` in the bloom-lifetime and
  device-creation fixtures; `test_sun_share_lane` pins 3 copies of the
  tested-opaque arm (gate, `UnmatchedReason::State` mirror,
  `SunUntrackedReason::State` diagnostic mirror), none with a linear-material
  prerequisite. Seven-module host set: 51 tests OK.

## Run 43 C (run139) — triage: solar-plant / distant-station shimmer persists, evidence points away from unrouted draws

Source: `/tmp/x3-bottleX3-run139/session-20260919-032345-212.log` (375 MB,
queried with grep/Python, never read whole). Four F8 captures, 8 frames each:
cap1 frames 4126-4133 (solar plant ~4-5 km), cap2 frames 4326-4333 (same,
~4-5 km, second pass), cap3 frames 5367-5374 (same plant, ~1 km), cap4 frames
8126-8133 (after a save load, fighter, busy distant station).

**Q1 — startup.** `fade_route_mode threshold=500 hysteresis=100 enabled=1
taa=1 hdr=1 linear_materials=0 source=default` (line 9). `linear_cutout_device
… verdict=1` (line 142); `fade_route_frame frame=0 … cutout_caps=1` (line
157) — cutout caps Ready. No separate "overlay arm enabled" startup line
exists; the overlay arm is unconditionally compiled in and only visible via
its per-frame `overlay_routed`/`overlay_refused` counters (first nonzero in
capture 4, see below). `X3M_LINEAR_MATERIALS=0` — original (non-linear)
shading, per `proxy_options` line 4.

**Q2 — per-capture totals** (sum of the 8 frames' `motion_output_frame` /
`fade_route_frame` lines):

| capture | draws | routed | unrouted | gate2(scene) | gate3(pair/unreg) | gate4(drawstate) | gate6(history) | fade_routed | fade_refused | overlay_routed | overlay_refused |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 (4126-4133) | 3290 | 3088 | 202 | 168 | 8 | 26 | 0 | 128 | 0 | 0 | 0 |
| 2 (4326-4333) | 3266 | 3090 | 176 | 168 | 8 | 0 | 2 | 128 | 0 | 0 | 0 |
| 3 (5367-5374) | 3192 | 3016 | 176 | 168 | 8 | 0 | 0 | 128 | 0 | 0 | 0 |
| 4 (8126-8133) | 7220 | 6920 | 300 | 160 | 56 | 84 | 0 | 8 | 0 | 16 | 0 |

Unmatched-reason histogram over `motion_route` lines whose `routed=0` (only
gate3/gate4/gate6 draws get an individual `motion_route` row; gate2 "scene"
refusals are frame-counted only, no per-draw row exists for them):
cap1 `no_zwrite=26 unregistered=8`; cap2 `unregistered=8` (+2 `history`,
frame 4333 only); cap3 `unregistered=8`; cap4
`no_zwrite=84 unregistered=104`. Across the whole log only three
`UnmatchedReason` values ever fire: `no_zwrite`, `unregistered`, `history`.
None of `FadeCaps/FadeInstanced/FadeRows/FadeGeometry/FadeConstants/
FadeOrigin/FadeThreshold/OverlayNode/Blended/State/Instanced/Rows/Geometry`
appear anywhere in this session (`grep -o 'unmatched=[A-Za-z_0-9]*' | sort |
uniq -c`).

**Q3 — grouping the unrouted draws.** Every `no_zwrite`/`unregistered`
`motion_route` row in all four captures has `node=00000000 node_handle=0
primitives=0 vertex_count=0 indexed=0` (e.g. line for frame 4126 index=386:
`vs=494fe349b8bc12ec ps=7c83ed50c9894e44 … node=00000000 … primitives=0 …
zwrite=0 blend=1 … unmatched=no_zwrite`; index=398:
`vs=5e484a06672e28fb ps=0a523f33ac47ae05 node=00000000 … unmatched=
unregistered`). These are the same two vs/ps pairs (plus one more,
`36f98d151fd6b0c6`/`222bee0defcb1852`, and `d5e1c75351ed3f04`/
`8360f422de08b5bd`, only in capture 4) in every capture, zero geometry, no
scene node — screen-space/post passes, not solar-plant or station mesh
geometry. **No group of large on-screen unrouted geometry exists in any
capture; nothing in the unrouted set is plausibly the shimmering panel legs
or window glow.**

**Q4 — did fade/overlay arms fire?** Fade arm: 128 `fade_routed` per capture
in captures 1-3 (16/frame, `fade_refused=0` throughout); only 8 total (1/frame)
in capture 4. Overlay arm: `overlay_routed=0` in captures 1-3, `16` total
(2/frame) in capture 4, `overlay_refused=0` everywhere. Both arms fire and
never refuse in this session — they are not blocked/starved here. Since the
unrouted set contains no plant/station geometry (Q3), the remaining shimmer
cannot be attributed to a class these arms are failing to catch.

**Q5 — evidence the shimmer is not an unrouted-draw problem.** Routed draws in
captures 1-3 include `atest=1` (alpha-tested) rows at 800/capture (100/frame,
vs=`4944d81dfe531b37`, ps=`64bac8bb307eb896`/`5e0a10fe752b6140`, 128
primitives each) — these *are* routed with `matched=1`, i.e. they do get
motion vectors. `motion_output_frame` TAA fields for every capture frame:
`taa_attempted=1 taa_resolved=1 taa_history=1 taa_skip=0`,
`cut=0`, `cut_missing=0.0000` (0.0052 once, frame 4333),
`cut_median_px` ranging 0.005-0.17 px — no history rejection spikes, no
disocclusion counter in this schema beyond `cut`/`cut_missing`, both near
zero. `history_previous` equals the frame's `routed` count every frame
(e.g. 386/386, 377/377, 865/865) confirming history carries forward
correctly. This is consistent with the alpha-tested/thin-geometry hypothesis
(sub-pixel panel struts and window cutouts, routed but under-sampled by TAA
jitter) rather than a routing gap; the log cannot further distinguish
sub-pixel coverage failure from light-map gain amplification — no per-pixel
luminance/variance counter exists in this schema to test that separately.

**Open issue / what one more launch should record:** this schema has no
per-draw screen-space bounding-box or triangle-density field, so "sub-pixel
geometry" here is inferred from `primitives<50` per draw plus known distance,
not measured directly. A diagnostic that logs each routed draw's screen-space
AABB area in pixels (or a coverage/derivative estimate) alongside its
`unmatched`/`fade_arm`/`atest` fields would let a future triage confirm or
rule out sub-pixel coverage as the shimmer cause without guessing from
primitive counts.

## Run 139: resolve-side shimmer diagnosis

Inputs: the run139 capture dumps only (`hdr_1_<f>.rgba16f` = the jittered FP16
scene BEFORE the resolve, `motion_1_<f>.rgba32f`, `depth_1_<f>.rgba32f`,
1280x768) and the `motion_output_frame` lines. `taa_debug=0` in this session,
so **no resolved or presented frame was dumped**: every output number below is
from an offline numpy re-implementation of `src/temporal/resolve.hlsl`
(dilation, producer motion, disocclusion test, 16-tap Catmull-Rom, luma
weighting k, min/max box intersected with mean +/- 1.25 sigma, w = 0.9), history
seeded with the first capture frame (7 resolves, so not converged), far-plane
pixels reprojected as static, display proxy = Reinhard(k*luma)^(1/2.2) in 8-bit
codes, RCAS approximated on luma at the run's `--taa-sharpen 0.75`. It is a
model, not a measurement of the installed resolve. Flicker metric: half the
temporal second difference (removes convergence drift), frames 5-7 of each
capture.

Session facts: jitter = 8-sample Halton(2,3), full +/-0.5 px, index advances
every frame (4126: idx 4 ... 4133: idx 3); `taa_k=2.4276` (2.4153 in capture 4);
`taa_skip=0`; camera `cut_median_px` 0.08 (cap 1/2), 0.16 (cap 3), 0.025 (cap 4).

**1. Signature.** The shimmer is in the input, and it is sub-pixel geometry
coverage, not texture, not emissive fireflies, not a period-2 artefact.
- Cap 1 plant ROI (y 180-520, x 520-900): 13 826 px carry valid depth in all 8
  frames, 16 345 px toggle between geometry and the depth sentinel with the
  jitter phase (more than half of the plant's footprint), background px (99 029)
  have raw range 0.5 codes. Raw range of the toggling px: mean 79.5 codes, p90
  126; of the always-covered px: mean 54, p90 138; 1-px horizontal neighbour
  contrast inside the panels: mean 29, p90 90 codes (a ~1 px bright lattice, ~240
  codes, against ~90). Hottest 32-px blocks: (x 832, y 224), (x 704, y 384),
  (x 800, y 256) — the panel arms.
- Raw flicker on geometry: cap 1 mean 23.2 codes, p99 133, 51% of px > 8 codes;
  cap 4 station ROI (y 120-420, x 300-900) mean 27.1, 68% > 8; cap 3 (motion
  compensated) mean 11.7, 33% > 8.
- Period: the period-2 component is 7.4 codes against a total temporal std of
  18.8; a linear model in (jitter_x, jitter_y) explains 43% of the variance. It
  follows the 8-phase jitter sequence, as toggling coverage should.
- Motion: cap 1 routed px move 0.12 px/frame median (p95 0.37); in cap 3 the
  plant's parts move 1.6-3.2 px/frame on screen (blocks x 1088-1152, y 448)
  while the camera median is 0.16 px: the arms rotate. E.g. px (1132,443),
  (1130,453), (1131,449): raw 88/215/88/186/160 codes over frames 5370-5374 with
  depth valid 1/0/1/1 — a ~1 px strut passing, which a fixed-pixel metric
  misreads as flicker; cap 3 is therefore measured along the motion vectors.

**2. What the resolve does with it (model).**
- Nothing is rejected: disocclusion rejections 0.0000 of geometry px and valid
  = 1.0000 in all three scenes (the relative depth tolerance 0.02 exceeds the
  whole depth span 0.98-1.0 at these distances, so the test is inert here;
  harmless for this symptom).
- Closest-depth 3x3 dilation exists; history filter is Catmull-Rom, not
  bilinear; the blend is already luma weighted (k = exposure). Removing the
  weighting makes it worse (cap 1 px > 8 codes 2.2% -> 4.2%, p99 10.5 -> 13.9),
  so "linear HDR blend" is not the cause.
- The clamp is a minor contributor: it moves history by > 1 code on 5.1% of
  plant px (mean 6.8 codes). Disabling it entirely: cap 1 mean 1.61 -> 1.37,
  cap 4 1.88 -> 1.61, cap 3 0.90 -> 0.79. Widening gamma 1.25 -> 2.0 or 99 (box
  only) changes nothing (1.61 -> 1.58): the min/max box, not the sigma clip, is
  what bites, and a lattice px has both extremes in its 3x3 most frames.
- The residual is the exponential-average ripple (1 - w) x input contrast:

| scene (geometry px) | raw mean / >8 | base mean / p90 / p99 / >8 | sharpened p99 / >8 | w 0.95 mean / >8 | filtered current (a=1.0) mean / >8 | both |
|---|---|---|---|---|---|---|
| cap 1 plant 4-5 km | 23.2 / 51% | 1.61 / 4.5 / 10.5 / 2.2% | 12.1 / 4.0% | 1.00 / 1.2% | 0.84 / 0.8% | 0.76 / 1.0% (a=2.29) |
| cap 4 station | 27.1 / 68% | 1.88 / 4.8 / 12.7 / 3.7% | 14.3 / 5.1% | 1.16 / 1.6% | 0.95 / 0.7% | 0.82 / 1.0% (a=2.29) |
| cap 3, speed < 0.5 px | 11.7 / 33% | 0.90 / 2.5 / 8.3 / 1.1% | 9.5 / 1.5% | 0.57 / 0.5% | 0.51 / 0.5% | 0.38 / 0.4% (a=1.0) |
| cap 3, speed 0.5-8 px | 9.7 / 37% | 1.30 / 3.2 / 6.7 / 0.5% | 8.7 / 1.5% | 1.08 / 0.3% | 0.95 / 0.2% | 0.94 / 0.3% (a=1.0) |

  The resolve removes ~93% of the input flicker; what is left is 2-5% of the
  object's pixels swinging 16-25 codes peak to peak at the jitter cadence, and
  RCAS at 0.75 adds ~25% to the mean and doubles the count above 8 codes.
  Bright lattice/window pixels next to dark ones are where it is largest simply
  because the contrast is largest (gain 4 raises the contrast, hence "less
  visible" with the gain off).

**3. Ranked mechanisms and smallest fixes.**
1. *(1 - w) ripple of a full-contrast sub-pixel input* (supported by every row
   above; ~85% of the residual). Fixes, both resolve-side:
   a. Filtered current sample: replace the point current colour in the blend
      (not in the clamp statistics) by the 3x3 Gaussian exp(-a d^2) of the
      neighbourhood the shader already fetches, d measured from the pixel
      centre to each neighbour's jittered sample position. Model: -35% (a=2.29,
      Blackman-Harris-like) to -48% (a=1.0) mean, px > 8 codes 2.2% -> 0.8%. Cost:
      zero extra fetches, 9 exp or a 9-entry constant table per frame; slight
      softening (RCAS is already there); no ghosting risk.
   b. History weight 0.9 -> 0.95: -38% mean. Cost: none in GPU time;
      clamp-bounded ghost trails last twice as long, convergence after a cut 20
      -> 40 frames. There is no launcher option for w today.
   Combined the model gives -53% to -58%.
2. *Min/max box clamp on lattice pixels* (~15% of the residual in cap 1/4). Not
   worth touching alone: removing it costs all ghost protection, and gamma has
   no effect.
3. *RCAS amplification* (+25%). `--taa-sharpen 0` is an existing A/B.
Not supported by the dumps: routing, disocclusion rejection, missing dilation,
bilinear history blur, unweighted HDR blend, jitter/resolve phase mismatch (a
static px reads f = 0 exactly in the shader contract; not checkable without a
resolved dump).

**4. Not implemented.** The model is not validated against a resolved frame
and cannot see anything slower than 8 frames (a 1-px lattice drifting at
0.06-0.4 px/frame beats against the pixel grid with a period of 3-16 frames,
which would look like the reported "tremble" and is outside this evidence).
The discriminating flight is the same scenes with `--taa-debug` (dumps
`taa_1_<f>.rgba16f` resolved FP16 and the presented target) and
`X3M_CAPTURE_FRAMES=32` or more: (i) compare the resolved dump with the model
(if the real second difference is well above the table, the installed resolve
departs from its source contract and that is the bug); (ii) the 32-frame
spectrum separates 8-frame ripple from slow lattice crawl. A second capture
with `--taa-sharpen 0` bounds mechanism 3. If (i) agrees with the model,
implement 1a behind a default-off option, verified by the temporal-pass fixture
with a 1-px lattice case.

## 2026-09-19 — lever 1 stage A and 2a (route-per-draw-cost.md): implemented, unflown

- Change: the route's seven value-only calls go to the borrowed native device
  under `X3M_OWNERSHIP=1` (`bind_direct`, `direct_call`, cold `direct_failed`
  → `ownership::observe_native_result`; aliases of `native_`/`device_` without
  a wrapper or with a published admission monitor; dropped first in
  `release_resources`); the two draw-path lock views use
  `ownership::get_buffer_lock_view_light` (no FNSAVE/FRSTOR shell). Base
  `56843d14`, worktree build, seam `53e3a0a8151a`, main's seam `3c5f2ceeecdf`
  built from `git archive HEAD` for the comparisons. Bottle X3, every Wine
  command under `wine_lock.py`.
- Route bench, µs per DrawPrimitive, medians (`route-bench-lever1-base.json`
  main, `-lever1.json`, `-lever1-r2.json`): off 1.475 / 1.458 / 1.472; perdraw
  8.721 / 8.848 / 8.861; perdraw-ownership 10.870 / 9.618 / 9.777;
  perdraw-depth 12.854 / 10.516 / 10.511; perdraw-cascades 13.507 / 11.404.
  Wrapper (ownership − perdraw): **2.15 → 0.77 and 0.92**; acceptance ≤ 0.6
  **not met**. Lease (depth − ownership): **1.98 → 0.90 and 0.73**;
  acceptance ≤ 0.9 met. perdraw-cascades −2.10. Default path unchanged within
  noise (perdraw +0.13, lazy +0.03).
- Why 0.6 is out of stage A's reach (I): all 43 value-only sites are direct;
  what still crosses the wrapper per routed draw is eight interface-input
  calls (SetRenderTarget ×4, SetVertexShader ×2, SetPixelShader ×2), each with
  `unwrap` under `registry_mutex`: 0.77 / 8 ≈ 96 ns per entry, not the 65 ns
  the note averaged. That residue is stage B, which stays closed.
- `run_motion_output.py` full suite on the change: PASS, 162 cases. The 49
  `seam-ownership-*` cases against main's binaries (retained-binary partial
  run): 0 field differences outside `us` timing blocks, equal check counts
  (retention-live 9743, -census 9740, -live-poll 9890, cascades 278, pool-hull
  77); `frames_changed_by_history` exists only in full runs and equals the
  tracked value. Suite logs: `motion_direct enabled=1 admission=0` on 69
  devices, `enabled=0 admission=1` on 4, no `motion_direct_loss_code`.
- `check_no_x87.py`: PASS, 548 reachable functions, the light entry a required
  root, no state transport beneath it. `run_ownership.py`: reports
  byte-identical to HEAD (370 / 563 checks, 0 failures); `verify_ownership.py`
  fails on its pinned 553 against HEAD's own tracked 563 (pre-existing).
  `run_ownership_integration.py`: 26 cases exit 0, verifier PASS.
  `run_ownership_integration_fallback.py`: link fails on stub symbols missing
  on main too (`get_buffer_lock_view`, `get_locked_prefix_view`,
  `set_surface_lock_observer`, `x3m_compositor_bridge_*`), now also the two
  new entries. `run_ownership_admission.py`: PASS modes=12 checks=147.
  `run_application_admission_abi.py`: fixture PASS checks=130 samples=21
  (equal to the tracked record), runner then stops on the
  `application-admission-abi-initial-summary.json` that bottle-X3 never had.
  `run_state_hook_benchmark.py --dll build/d3d9.dll`: 28 PRESERVE rows, 0 bad.
  `run_object_lifetime.py`: 13 PASS, timing-only deltas. Rewritten tracked
  results restored to HEAD.
- Host: snippet mocks gained `direct_call`/`drop_direct`
  (`motion_wrap_state_fixture.cpp`, `linear_material_live_fixture.cpp`,
  `test_linear_cutout_contract.py`), source-gain pins follow the new call text.

### 2026-09-19 — lever 1 stage A + 2a: reviews

Opus review and Fable second review: nothing blocking. The seven forwarders are admission scope + native call +
`observe_result` only and none short-circuits while the device is lost, so the direct path bypasses nothing; the
proxy's hooks swap the wrapper's vptr to a private table, so `direct_` always holds d3d9's own entries in every hook
configuration and needs no refresh; the borrowed device is fixed at wrapper construction; the light lock view's two
callers are reached only from `after_draw` under `LightCallBoundary`. Fixed after review: stale LastError comment,
log flush before the seam's loss-code `TerminateProcess`, `static_assert` on `direct_slot_count`. Accepted result:
wrapper overhead 2.15 -> 0.77-0.92 us per routed draw (target 0.6 needs stage B, which stays closed), lease
1.98 -> 0.73-0.90. Follow-up: a seam-ownership case with a Reset and a simulated DEVICENOTRESET through
`observe_native_result`; the fallback stub needs two more exports when that runner is restored.

**5. Implementation of 1a and 1b (default off, for one A/B flight).** Not flown;
the model is still unvalidated against a resolved frame.
- `--taa-current-filter A` (`X3M_TAA_CURRENT_FILTER`, 0..4, absent/0 = off) and
  `--taa-history-weight W` (`X3M_TAA_HISTORY_WEIGHT`, 0.5..0.98, absent = 0.9);
  both require `--taa`, are forwarded only when given (a stale shell value is
  dropped) and are logged on `motion_output_mode` (`taa_current_filter`,
  `taa_history_weight`), `motion_output_taa` (`current_filter`,
  `history_weight`) and every `motion_output_frame` (`taa_filter`,
  `taa_weight`).
- Filter definition (the model's): the blend's current colour is
  sum(g c) / sum(g) over the finite 3x3 samples in the luminance-weighted
  domain, g = exp(-A d^2), d in pixels from the pixel centre to the neighbour's
  jittered sample position, (n - jitter) with the jitter in raster pixels (the
  sample at p shows content at p - jitter). Clip box, variance and luminance
  weighting unchanged; no extra fetches. A rides in c22.y, W in the existing
  c5.z; no per-frame allocation, no extra constant upload.
- Off path: a `#ifdef` in `resolve.hlsl` plus the wrapper
  `src/temporal/resolve_filter.hlsl`, so the plain program is byte-identical
  (bytecode sha256 `2bfba715...9b09c8`, 1891 words, as before; the embedded
  header is unchanged) and is the one bound whenever A = 0. The filtered
  variant (`temporal_resolve_filter_program_inc.h`, 1939 words, sha256
  `0f283235...60abb9`) is created only when A > 0.
- Instruction budget (D3DXDisassembleShader): plain 507 slots, filtered **521**
  (26 texture + 495 arithmetic; +14, 7 of them in the rolled 3x3 loop), against
  the 512 this backend advertises and every ps_3_0 device guarantees. This
  backend creates and runs it regardless. A native driver whose
  `MaxPixelShader30InstructionSlots` is 512 would refuse it: `TemporalPass`
  treats a failed creation of the optional program as "filter unavailable"
  (`current_filter_available()`, `current_filter_result()`), and the route logs
  `motion_output_taa_current_filter unavailable=1` and runs the plain resolve.
  Fitting 512 needs 9 of the 14 slots back; CPU-side separable weights would
  save an estimated 5 (not tried), so this is open.
- Fixture (`run_temporal_pass.py`, mode `lattice`,
  `verification/results/bottle-X3/temporal-lattice.txt`, summary key `lattice`):
  static 1-px lines at pitch 4 px (x = 4.31 + 4i), 8-sample Halton jitter, 128
  frames, flicker = mean half second difference over the last period, 32x32
  FP16. 28 numerical / 9 state checks; the existing run is unchanged at 508 /
  278 / 386 samples.

| config | flicker (codes of 255) | ratio | run-139 model |
|---|---|---|---|
| baseline = off (bit-identical over all 128 frames) | 2.75 | 1 | 1.61 |
| A = 1.0 | 1.64 | 0.594 | 0.52 |
| W = 0.95 | 1.32 | 0.481 | 0.62 |
| A = 1.0, W = 0.95 | 0.81 | 0.295 | - |
| A = 2.29 | 1.84 | 0.670 | 0.65 |
| A = 2.29, W = 0.95 | 0.89 | 0.324 | 0.47 |
| k = 2.4276: A = 1.0 / W = 0.95 | 0.99 / 1.26 of 2.62 | 0.380 / 0.481 | - |

  Asserted within 0.15 of the modelled ratio (A = 1.0, W = 0.95, A = 2.29 with
  W = 0.95). The filter's ratio depends on the line pitch (CPU model: 0.14 /
  0.35 / 0.60 at pitch 2 / 3 / 4), so the flight, not this scene, validates the
  0.52; W's 0.48 is the pure (1 - w) law, the model's 0.62 includes the clamp
  share that W does not scale. Shader against the CPU definition of the filter:
  max error 0.0024 (W 0.9) and 0.0049 (W 0.95), bound 0.0003 / (1 - w) (FP16
  rounding). Flat regions against the baseline: 0.0005 max (bound 1/255).
  Moving 6x6 square at 1 px/frame: pixels with no square pixel in their 3x3
  stay the background within 0.0001; in the square's rows, at columns wholly
  outside its jittered extent, the colour added over the baseline is 0.009
  (A = 1.0), 0 (W = 0.95), 0.004 (both) against the stated bound
  (1 - w) 0.75 F + (w - 0.9) 0.75 + 1/255 = 0.036 / 0.041 / 0.058, F = 0.43 the
  largest side-column share of the kernel. The filter also softens every edge
  by its own footprint (a background pixel beside a bright edge takes up to
  0.43 of the contrast in one frame, about 0.2 on the phase average).
- `--taa-debug` dumps per capture frame: `hdr_1_<f>.rgba16f` and
  `taa_1_<f>.rgba16f` (resolved history) 7.9 MB each, `present_1_<f>.bgra8`
  3.9 MB, `motion` 15.7 MB, `depth` 3.9 MB (15.7 MB on the sun lane) at
  1280x768: about 40 MB per frame (51 MB sun lane), raw. `--capture-frames`
  was capped at 8 in the launcher and the DLL; both now accept 0..64
  (`--motion-capture` keeps 2..8), so `--capture-frames 32` is 1.3-1.7 GB.

### 2026-09-19 — TAA current filter / history weight: review

Opus review: nothing blocking. Off path byte-identical (plain program hash `2bfba715…`, no shader created and no
per-frame work at A = 0); lattice ratios reproduced (A = 1.0 0.5945, W = 0.95 0.4811, both 0.2952); `seam-taa-on`
164 checks unchanged. Fixed: three manifests (`current-depth`, `sun-shadow-apply`, `sun-shadow-cascade-apply`)
regenerated for the changed generator hash (headers unchanged); portability gap recorded. Accepted low points:
`filterTotal` can underflow only for a jitter beyond about 6 px (the route's Halton is within 0.5);
`kHistoryWeight*` constants in `resolve.h` are not the ones `capture.cpp`/`motion_output.h` use; no fixture Resets
a pass holding the filtered program; the shared 32-wchar env buffer pattern.

## Run 44 B (run142–146): angle-dependent shimmer

Inputs: `/tmp/x3-bottleX3-run142` (baseline, `--taa-debug`, two 32-frame F8s: plant 6140-6171, distant station
10840-10871), run143 (filter 1.0, 4958-4989 / 10406-10437), run146 (filter 1.0 + weight 0.95 + sharpen 0, three
8-frame F8s: 4241 plant angled, 6289 plant second view, 10011 station; no resolved dumps). Dumps 1280x768; display
proxy and flicker metric as in the run139 section (Reinhard(k·luma)^(1/2.2) codes, half second difference). All
sessions ran `X3M_TAA_MIP_BIAS=-0.5` (launcher default, `tools/manage.py` `TAA_MIP_BIAS_DEFAULT`), `mip_bias=-0.5`
in `motion_output_mode`. "Far object" = geometry px with depth > 0.999 (the player ship in the chase view is
excluded; it is half of all geometry px).

**A. Offline model vs resolved dumps (`taa_1_<f>.rgba16f`, pre-sharpen).** Geometry px, mean / p90 / p99 / >8 codes:

| capture | raw HDR | resolved measured | model | presented (tonemap + RCAS 0.75) |
|---|---|---|---|---|
| run142 plant | 18.1 / 48.6 / 91.5 / 51% | 1.02 / 2.68 / 4.76 / 0.1% | 1.61 / 4.5 / 10.5 / 2.2% | 1.82 / 5.16 / 9.20 / 2.0% |
| run142 station | 11.9 / 31.5 / 67.2 / 40% | 0.88 / 2.14 / 4.70 / 0.1% | 1.88 / 4.8 / 12.7 / 3.7% | 1.24 / 2.79 / 6.23 / 0.3% |
| run143 plant (filter 1.0) | 16.4 | 0.49 / 1.01 / 3.31 | 0.84 | 0.74 / 1.84 / 4.19 |
| run143 station | 11.8 | 0.42 / 0.98 / 2.59 | 0.95 | 0.48 / 1.10 / 2.43 |

The model's absolute level was pessimistic by 1.6-2.1x (it ran 7 unconverged resolves); its relative prediction
holds (filter 1.0: measured -52% on the plant, predicted -48%). The installed resolve matches its contract: on
run142 plant coverage-toggling px the raw band rms is 27.0 (period 2-4) / 21.1 (period 4-8) codes and the resolved
1.49 / 2.42, against the w=0.9 exponential-average gains 0.053-0.074 / 0.136 (predicted about 1.7 / 2.9). The
residual is dominated by the **period-8 component** (the jitter cycle; 5-8 Hz at the user's frame rate), and RCAS
0.75 raises the presented mean by 40-80%. The plant scene of run142 is static (resolved period 8-32 rms 0.31
codes), so no slow lattice crawl is involved there; the station capture has real motion (slow band 11 codes) and
cannot be separated by this metric.

**B. Where the flicker energy is.** Classes per px over the capture: *flip* = depth toggles between geometry and the
sentinel; *stable interior* = depth valid in every frame and in all 8 neighbours; *edge* = the rest (all-valid px
next to a toggling px, or with a depth jump; the jump test is unreliable at depth -> 1 and is merged here).
Share of far-object px / share of raw flicker energy / mean raw flicker:

| capture | flip | edge | stable interior |
|---|---|---|---|
| run146 #1 plant angled (88 142 px) | 58% / 64% / 27.3 | 42% / 36% / 20 | 0.1% / 0.1% |
| run146 #2 plant second view (149 648 px) | 44% / 65% / 32.9 | 34% / 27% / 18-30 | 22% / 7.9% / 11.6 |
| run146 #3 station (25 163 px) | 26% / 40% / 22.4 | 48% / 55% / 15-20 | 26% / 5.6% / 6.7 |
| run142 plant (63 130 px) | 52% / 63% / 32.3 | 29% / 29% | 19% / 8.1% / 12.8 |
| run142 station (27 056 px) | 45% / 50% / 15.3 | 54% / 50% | 1.6% / 0.2% |

In run142 the resolved fast-band energy splits the same way (plant: flip 63%, edge 30%, stable interior 7%;
station: 63% / 37% / 0.5%). Flip px are covered in 0.48-0.52 of the frames. **Colour aliasing inside
constant-coverage surfaces carries at most 8% of the flicker**; the texture-colour (light-map minification)
hypothesis is refuted for both the lattice and the station. The lattice is a field of ~1 px lines that write depth,
2-3 px apart, over a panel body drawn without depth write (blended, `ps=64bac8`, 76 032 prims): the depth mask
shows every line dashed by the pixel grid. The dumps cannot tell whether a given line is a thin triangle strip or an
alpha-test cutout (both toggle depth and colour together); no per-draw id is dumped. The raw input does not
differ between the two plant views (#2 flickers more per flip px than #1), so the user's angle dependence arises
after the resolve, consistent with line spacing: where the lines are 2-3 px apart each resolves to a steady grey
line, where perspective compresses them toward 1 px pitch the 3x3 min/max box and the period-8 ripple act on every
px. There is no resolved dump for run146 to measure this.

**C. Draws, samplers, textures** (capture rows `texture`/`texture_desc`/`sampler`/`state`, frames 4241 and 10011;
the rows are logged before the route applies its bias, so `bias` is the game's).
- Every material stage of every routed draw: MINFILTER 3 (anisotropic), MAGFILTER 2, MIPFILTER 2 (linear),
  MAXANISOTROPY 16, game MIPMAPLODBIAS 0, MAXMIPLEVEL 0, DXT1/DXT5 with **full mip chains** (256 -> 9 levels ...
  2048 -> 12). Single-level textures on stages 0-3 are only 32x32 placeholders, HUD/post A8R8G8B8 targets and the
  unrouted 2048x1024 DXT1 background (`vs=7b6393 ps=6109cf`, blend, no z-write). Cube stage 4: linear, no mip filter.
- Ours: -0.5 on the mip-mapped stages of routed draws (eligibility `levels > 1 && MIPFILTER != NONE`), so on all of
  the above, alpha-tested included.
- Plant (16 instances, `vs=4944d81d...`): opaque `ps=ca6bfa` 256 draws / 703 168 prims (one group alone 96 draws /
  650 496 prims, 1024x1024 DXT1+DXT5), alpha-tested `ps=5e0a10` 64 draws / 91 328 prims (512x128 DXT5 x3 + DXT1;
  1024x1024 DXT5), `ps=64bac8` 16 alpha-tested draws / 64 prims and the blended panel body. Alpha test:
  ALPHAFUNC 7 (GREATEREQUAL), ALPHAREF 1, z-write on, routed (`atest=1`).
- Frame totals: 4241: routed opaque 280 draws / 868 225 prims, routed alpha-tested 92 / 137 544; 10011 (station):
  391 / 705 195 and 65 / 20 868.

**D. Ranking.**
1. *Sub-pixel coverage toggling x exponential-average ripple at the 8-frame jitter period* (>= 63% of raw and
   resolved energy on the plant, 40-63% on the station, plus most of the edge class). Measured, and the resolve
   behaves as specified.
2. *RCAS* +40-80% on the presented residual (run142 1.02 -> 1.82, 0.88 -> 1.24; the presented figure is in AgX
   8-bit codes, the resolved one in the Reinhard proxy, so the ratio is indicative only).
3. *Texture colour aliasing inside surfaces*: <= 8%. Forced anisotropy and mip-chain generation have nothing to fix:
   the game already runs anisotropic 16 with linear mip filtering over full chains.
4. *Our -0.5 bias*: unmeasurable from these dumps (no bias-0 capture exists). It can only matter through (3) and
   through the alpha-tested share of (1) (11.5% of plant prims, 14% of the plant frame's routed prims, 3% of the station frame's; ALPHAREF 1 means a finer level
   thins the cutout lines). An upper bound needs the A/B below. The general default stays.
Nothing is implemented: the mechanism that a small sampler-side option would address is not shown to carry any
measurable share, and the existing `--taa-mip-bias 0` already gives the decisive A/B without a new build.

**One-flight diagnostic** (installed build, no new candidate): plant at the shimmering angle, `--taa-debug`,
`X3M_CAPTURE_FRAMES=32`, same pose twice: (a) defaults, (b) `--taa-mip-bias 0`. Read: resolved fast-band rms on
flip px (run142: 2.84) and the flip-px count. If (b) drops either by more than about 20%, the lattice is an
alpha-test cutout and a selective bias is justified: bias 0 on draws with ALPHATESTENABLE, which the route already
reads per draw for `atest=`, so the cost is one compare in `apply_mip_bias` and no new state query; default-off
option, the `mipbias` fixture script gains an alpha-tested twin. If (b) changes nothing, the lines are geometry and
the remaining levers are resolve-side: the period-8 ripple (filter 1.0 measured -52%; weight 0.95) and sharpen
strength on high-contrast thin features.

**run148/run149: mip bias A/B.** Inputs: `/tmp/x3-bottleX3-run148` (`X3M_TAA_MIP_BIAS=-0.5`, default) and
`/tmp/x3-bottleX3-run149` (`X3M_TAA_MIP_BIAS=0`), each one 32-frame F8 at the plant, `--taa-debug`. Logged
`mip_bias=-0.5` / `mip_bias=0` confirmed in `motion_output_frame`. The two poses are **not** the same: geometry
bbox x355-990/y159-767 (run148, 139 046 px) vs x420-1066/y120-767 (run149, 136 502 px); scene-motion px/frame
median 0.59 (run148) vs 0.38 (run149) — comparable order of magnitude, not an identical hold, so this is not the
clean same-pose-twice protocol the diagnostic above calls for. Flip (coverage-toggling) px: 50 244 (run148) vs
47 908 (run149), -4.6%, under the 20% bar. Resolved fast-band rms on flip px: run148 4.81, run149 6.22 codes
(period 2-4 rms 2.31/3.05, period 4-8 4.23/5.42, period 8-32 "slow crawl" 11.1/12.3 — the slow band dominates
both and was not visible in the 8-frame run146 captures). That is +29% with bias 0, **the opposite sign** from the
diagnostic's alpha-test-cutout prediction (a drop). Resolved mean/p99 on flip px: 1.93/6.25 -> 2.43/9.07 (+26%/+45%);
presented (post-sharpen) flip-px mean/p99: 3.93/12.30 -> 4.90/16.89. Verdict: flip-px count does not move >20%
either way; resolved fast-band rms moves >20% but increases, not decreases, so it does not support "bias 0 removes
alpha-test cutout flicker" and cannot be attributed to the bias alone given the pose difference above — the A/B is
confounded and inconclusive; a same-pose repeat (identical camera transform logged, only bias varied) is needed to
settle it.

**run148: strut draw type and motion-compensated shimmer.** Same input as above (`/tmp/x3-bottleX3-run148`,
`X3M_TAA_MIP_BIAS=-0.5`, 32-frame F8, plant). Scripts: `tools/analysis/taa_resolve_replay.py` (worktree
`agent-abfdd9ffa87514720`, not yet on `main`) and a private grep/numpy pair in the scratchpad (paths in the
triage report), not committed.

*Q1 (triangle vs alpha-test cutout).* `draw` rows for frame 4411 (and 4420/4430/4442, stable to +/-60 prims):
302 opaque (`D3DRS_ALPHATESTENABLE=0`) draws / 874 077 prims, 92 alpha-tested (`ALPHATESTENABLE=1`,
`ALPHAREF=1`) draws / 137 544 prims — 13.6% of frame prims, all 394 draws total (this run's own counts; they
do not match the 256/703 168 + 64/91 328 figures quoted as priors, which were not reproduced from this capture
and may be a different frame or a node-filtered subset). `src/proxy/capture.cpp`'s `snapshot()` (draw/state
dump, ~line 672) logs render state, samplers and transforms per draw but **no per-draw screen-space bounds and
no per-pixel draw-id tag**; the depth/motion dumps have no draw-id channel either. There is therefore no way to
attribute a specific flip pixel (coverage-toggling across the 32 frames, x355-990/y159-767, 50 238 px) to a
specific draw from this capture — the question is **not decidable** from the existing dumps. The run148/run149
mip-bias A/B already in this section is the only indirect evidence and it points away from alpha-test cutouts
(bias 0 made the flip-px band rms worse, not better) but is confounded by the pose difference. One-capture
diagnostic that would decide it: an extra debug target written only from alpha-tested draws (stencil tag or a
spare render-target channel set to 1 in `apply_mip_bias`'s alpha-tested branch, cleared 0 otherwise), dumped
alongside `motion_1`/`depth_1`, so flip pixels can be classified by draw type directly.

*Q2 (motion-compensated shimmer).* `taa_resolve_replay.py options` on the same bbox (`crop px 386 080, flip px
50 238, stable interior px 81 453`) reports `CLASSIFY ... median strut speed 0.32 px/frame` — the resolve's own
per-pixel reprojection (dumped motion vectors where routed, else the logged camera rotation `R`/projection `P`)
already removes almost all of the logged ~0.3-0.6 px/frame drift before blending; residual reprojected motion is
sub-pixel. Its output is therefore already a motion-compensated series. Measured on it (`dumped-resolve bands on
flip px`, ground truth, not the offline model): period 2-4/4-8/8-32 rms = **3.71 / 5.53 / 17.81 codes** (`OPTION
base` block-mean bands 1.50/1.91/7.17). This does not match the 2.31/4.23/11.1 recorded earlier in this section
for run148 under the same bbox and script; the discrepancy is unresolved (open issue below) and this entry's
3.71/5.53/17.81 is the number this session actually reproduced with the command below.

A separate, cruder check — full-frame phase-correlation registering each `taa_1` frame to frame 0 with a single
global 2D translation, then bilinear-warping before computing the same bands — found a cumulative shift of only
0.049 px by the last frame (vs. 2.15 px on the raw, un-resolved `hdr_1` sequence), i.e. this scene's drift is not
a pure translation (consistent with the logged per-frame rotation matrix `R`) and a global-translation warp adds
essentially nothing beyond the resolve's own per-pixel reprojection: post-warp bands on flip px were 3.74/5.43/
17.45, matching the dumped-resolve ground truth above within noise. Stable-interior px (81 453 px, valid depth in
every frame and 8 neighbours) under the same warp: 1.60/1.90/6.50 codes — the warp/measurement noise floor.

Compared with the near-static run142 plant (resolved flip-px rms 1.49/2.42, period 8-32 0.31, no drift, Run 44 B
section A/B above): run148's motion-compensated flip-px shimmer is **larger in every band**, not comparable — the
fast bands are ~2.5x (3.71 vs 1.49, 5.53 vs 2.42) and the slow band is ~57x (17.81 vs 0.31). The slow-band
explosion under drift, absent when static, is the dominant new artefact; it survives the resolve's own per-pixel
motion compensation and is not attributable to un-compensated true motion (residual reprojection speed measured
0.32 px/frame, sub-pixel).

Commands run this session:
```sh
python3 tools/analysis/taa_resolve_replay.py /tmp/x3-bottleX3-run148 355 159 990 767 options
```
(plus a private grep/numpy draw-row parser and a phase-correlation warp script, scratchpad-local, not committed).

## TAA flicker suppression steps 0-3 (2026-09-19, implemented, unflown)

Design and implementation numbers: `docs/architecture/taa-flicker-suppression.md`, section 10. Bottle X3, worktree build.

- `run_temporal_pass.py`: PASS; main suite 508 numerical / 278 state / 386 samples, `temporal-pass.txt` byte-identical to
  the pre-change file; lattice mode 191 numerical / 13 state (run-139 cases 28 / 9 unchanged, flicker cases +163 / +4).
  Slots: plain 433, filter 444, snapshot 46, thin 468, thin + filter 480, age 494, age + filter 506, all <= 512.
- Drifting lattice (0.8-px lines, base -> soft 0.75 -> soft + w 0.97 wide gate; per-px p2-4 / p4-8 / p8-32, block
  p2-4 / p4-8 / p8-32, contrast): pitch 2.37, 0.4 px/frame: 8.6 / 14.1 / 14.4, 3.2 / 3.0 / 4.2, 0.40 -> 8.3 / 14.2 / 14.2,
  3.0 / 3.0 / 4.0, 0.39 -> 12.8 / 14.0 / 19.4, 4.1 / 3.4 / 5.9, 0.49. Predicted for soft (1-D model): block p2-4 -50..-63 %;
  measured -4 %. 1.25-px lines, pitch 4, 0.4 px/frame: 4.2 / 6.9 / 38.4, 1.6 / 1.7 / 3.6, 0.57 -> within 6 % -> 2.0 / 3.4 / 20.0,
  0.8 / 0.8 / 1.6, 0.30. Static 1.25-px ripple ratio w 0.97: 0.284 (predicted 0.29). Full table: `FLICKER_DRIFT` lines of
  `verification/results/bottle-X3/temporal-lattice.txt` (64 rows), oracle error <= 0.0113 (bound 0.02 at w 0.97).
- Finding: the thin-feature soft clip has no measurable effect (mask blind on the collapse phases of sub-pixel lines,
  where the disocclusion test rejects the history first; clamp not binding on lines >= 1 px), and the adaptive weight makes
  sub-pixel lines flicker more (age restarts on every rejection). Details and a possible remedy in section 10.
- `run_motion_output.py seam-taa-on seam-taa-hdr-tonemap-on`: 164 / 140 checks, as before (`TAA_BASE_REFERENCES` 4 -> 5 for
  the snapshot program). `run_object_lifetime.py`: exit 0, timing-only deltas. Generator `--check`: the seven resolve
  programs, `current_depth`, `sun_shadow_apply`, `sun_shadow_cascade_apply` and the nine bloom programs PASS (manifests
  re-pinned to the generator; bytecode of the unrelated ones unchanged). Host suite: 2269 tests OK after the merge of main (2259 before it).

### Replay of run148 / run142 through the resolve oracle (2026-09-19)

`docs/architecture/taa-flicker-suppression.md`, section 10.1. The numpy oracle reproduces the installed resolve free-running
over 31 frames within 0.41 codes (mean 0.03-0.05) on all three captures. On the real struts the disocclusion test never
rejects (depths 0.984-1.0 against the 0.02 tolerance; the fixture's line depth 0.5 was unrepresentative), the thin mask is
true on 63-99 % of flip px-frames and the clamp moves history by more than a code on 2-3 %. Drifting plant: resolved block
p8-32 6.76 against the raw input's 6.89, no-clip bound 6.73: the slow band is scene motion, no option moves it. Static plant:
thin + w 0.97 takes per-px p2-4 / p4-8 1.93 / 2.53 -> 0.59 / 0.79 (x 0.31), contrast x 1.00, stable-px gradient energy x 0.95;
thin clip alone x 0.97; current filter 1.0 x 0.55 with gradient energy x 0.43. No shader change; fixture depths to be rescaled.

### TAA flicker review fixes (2026-09-19)

`docs/architecture/taa-flicker-suppression.md`, sections 10 (re-baselined) and 10.2. Fixture lines at depth 0.99 plus eight
near-depth rows; lattice mode 210 numerical / 13 state, `temporal-pass.txt` still byte-identical; `seam-taa-on` 164,
`seam-taa-hdr-tonemap-on` 140; resolve programs `--check` PASS (no shader change). Static-plant replay, thin + w 0.97 default
gate: x 0.31 unchanged, 0.0000 of thin flip px exceed the gate's LO (speed already excludes the dilation offset).
Supersedes the adaptive-weight lattice numbers and the "sub-pixel lines flicker more" finding of the steps 0-3 entry above:
those were measured with line depth 0.5 and now hold for the near-depth rows only.

### TAA line filter, implemented unflown (2026-09-19)

`docs/architecture/taa-lattice-crawl.md`, sections 8-9. Real AgX + RCAS in the replay (`taa_resolve_replay.py ... lattice`,
0.45 / 0.42 codes from the dumped present): the sharpen multiplies the plant crawl by 1.14 / 1.08 (run148 / run142), not the
proxy's 1.6; sharpen exclusion rejected. `--taa-line-filter A[,W]` (`X3M_TAA_LINE_FILTER`, default off): current-sample
Gaussian on a depth-only line mask (own program, two draws, `s8`); run148 presented crawl 7.46 -> 5.09 (A = 2) / 4.32
(A = 1), equal to the note's glass-specific mask; run142 station flip-px bands -5 / -4 / -6 %, big-object edge gradient
x 0.986. Slots: line mask 125, `resolve_line` 446, `resolve_thin_line` 483, `resolve_age_line` 509; existing programs'
bytecode unchanged. `run_temporal_pass.py` exit 0 (lattice mode 255 numerical / 15 state; oracle error 0.0024, silhouette
0 masked / 0 differing of 2688, bead amplitude x 0.63 / x 0.45, pass +0.40 ms at 1280x768); `run_motion_output.py
seam-taa-on seam-taa-hdr-tonemap-on production-taa-hdr-tonemap-on`: 164 / 140 / 59 checks, exit 0; generator `--check` PASS
for all programs, every manifest and the nine bloom manifests re-pinned to the generator (eight `*_inc.h` headers changed in
their stale `Reproduce:` comment line only).

### TAA far stabiliser, implemented unflown (2026-09-19)

`docs/architecture/taa-distant-line-fade.md`, section 9. Replay first (`taa_resolve_replay.py ... far`): the note's table
reproduced (run153 station whole-crop hot std x 0.19 / 0.45 / 0.14); gated 80,130 units/px the station keeps x 0.19 (weight
0.985 alone) / x 0.14 (with filter A = 1), run142 drifting station x 0.55 / x 0.43 (gate 90,150), the near plant's far end loses 2 % of its
gradient energy to the weight and 31 % to weight + filter, the near end is untouched; far sharpen removal x 0.97-0.99, not
implemented. `--taa-far-stabiliser W[,A[,F0,F1]]` (`X3M_TAA_FAR_STABILISER`, default off, W and A separate): `resolve_far`
508 slots, mask program 153; all other bytecode hashes unchanged. `run_temporal_pass.py` exit 0, lattice mode 316 numerical /
17 state: oracle error 0.00024, near pixels 0 of 39 936 differing, static far ripple x 0.154 = oracle, gate-off and
mask-allocation-failure runs bit-identical to the plain resolve with the history kept. Line-filter review findings: the nine
exp taps measure -0.07 ms (noise, left); mask allocation failure now disables the options for the session with one log line.


## 2026-09-19 — lever 3, hook-free lazy RT (route-per-draw-cost.md)

`X3M_MOTION_RT_MODE=lazy` (`--motion-rt-mode lazy`; initially opt-in, promoted after Run52 below; `perdraw` remains the rollback mode) holds RT1/RT2
across consecutive routed draws and **never a write mask**: each routed draw reads `COLORWRITEENABLE1/2` (the reads per-draw
mode already makes), writes a mask only when it differs from what the draw needs (15, or 0 on RT2 for a fade-band draw)
and the undo puts the application's value back; the flush is two unbinds. `lazy_rt` is no `state_hooks` reason any more,
slots 57/69 stay unhooked, the `get_render_state` hook (58) and `before_set_render_state` are removed; `get_rt` /
`get_rt_data` stay. New frame-line count `lazy_mask_writes` (masks other than 15 met by a lazy routed draw: the flight's
fallback-rate counter). This supersedes the hook statements of "Lazy RT binding equivalence" above: lazy now runs
`rs_mode=get` under `auto` and `X3M_STATE_SHADOW=0`, `shadow` only under the explicit switch.

Bench (`run_route_bench.py`, 400 routed draws, median us per DrawPrimitive, one run each, noise +-0.15;
`route-bench-lever3-{before,after}.json`; before = HEAD `9b871c28` built from a source archive of that commit):

| config | before | after |
| --- | --- | --- |
| off | 1.40 | 1.47 |
| perdraw | 8.78 | 8.69 |
| lazy | 6.69 (`installed=1 reason=lazy_rt`, shadow) | 6.90 (`installed=0 reason=none`, `rs_mode=get`, 4,016 gets) |
| perdraw-ownership | 9.61 | 9.64 |
| lazy-ownership | 7.19 | 7.49 |
| perdraw-depth | 10.43 | 10.52 |
| perdraw-masked (application masks 7 / 7) | - | 8.80 |
| lazy-masked (`lazy_mask_writes` 400 of 400) | - | 7.12 |

After (the review-fix rerun; the first run read 1.91 / 2.26): plain route 7.22 us over `off`, ownership wrapper +0.95
(perdraw) / +0.59 (lazy), lease +0.89, RT binding share **1.79 us** plain and **2.15 us** through the wrapper (25 % of
the plain route). Mask != 15 fallback on every routed draw (`X3M_FIXTURE_BENCH_MASK=1`, both masks 7): lazy 7.12, that
is +0.22 us over unmasked lazy and still **1.68 us** under per-draw with the same masks. The mask reads cost 0.2 us against the
hooked lazy mode, inside two noise bands, and buy the removal of the two setter hooks (3.1 ms per frame in
route-per-draw-cost.md section 3). The premise (>= 0.5 us) holds.

Fixtures (X3, retained binaries of this tree, all exit 0): 25 lazy cases and 9 per-draw controls. New selected-only
`X3M_FIXTURE_BURST_MASK=1` burst (`seam-burst-perdraw-mask`, `seam-burst-lazy-mask`, `-mask-shadow`,
`seam-ownership-burst-lazy-mask`, `production-burst-lazy-mask`; 104 / 59 checks): both masks written (7 / 5) under a held
binding, a routed draw under them, both read back with no getter hook; a hold that starts masked; mid-scene StretchRect
(frames 2, 5, 8), application SetRenderTarget / depth Clear (other frames) and a Reset inside the scene under a held binding
(frame 4, the frame restarts; the DLL's frame counters restart with it). Every lazy mask run equals the per-draw twin in
colour, STATE signature, RT1/RT2 hashes and readback files; `set_rt` 24 per-draw against 12 lazy (24 on capture frames),
`lazy_flushes` 3, `lazy_mask_writes` 2 per frame after the review fix below (1 in the plain burst), `state_hooks installed=0 reason=none` except the
explicit-shadow case. Existing cases on the changed DLL against HEAD's DLL in the same session (`production-on`,
`seam-on`, `seam-ownership-on`, `seam-taa-on`, `seam-ownership-taa-on`, the three per-draw bursts, the per-draw wrap
burst): 0 field differences. Lazy cases against the tracked summary: 0 differences outside `render_state` (lazy moved to
`rs_mode=get` by design) and the burst `state_hashes`; that hash and the `render_state` counts differ from the tracked
summary on HEAD's own DLL too (stale record, not this change), and lazy equals per-draw in-session for the plain and
the wrap burst. Regular-script lazy twins, cutout (2), fade-route (8), mipbias (2) lazy cases pass unchanged validators.
Host: `test_motion_wrap_states`, `test_linear_material_live` (the held-depth mock case now undoes the depth row first:
78 attempted-state checks), `test_motion_hdr_scene`, `test_linear_cutout_contract`, `test_motion_output_runner`,
`test_frame_timing` (new launcher test) OK; `check_no_x87.py` 0 violations. Not run: `run_state_hook_benchmark.py` under
lazy (it pins `perdraw`). Native Windows: documented calls only, unverified.

### 2026-09-19 — lever 3: review fixes

(1) The `set_depth` hook (slot 39) had no restore point although route-per-draw-cost.md section 3 lists it; it now calls
`restore_bindings()` before the native call (host check in `test_linear_cutout_contract`). The mask burst gained a step:
with RT1/RT2 held the application sets a same-size depth surface, one of half the size, then the original; all three
succeed and every lazy run equals its per-draw twin (113 / 68 checks). (2) The caller-less quiet flush is removed
(`flush_bindings` is a plain function; `record_deferred` and the `deferred_*` fields are gone); the mock scenarios that
injected a deferred failure now inject the lazy-flush or mip failure they stood in for (`linear_material_live` 20,481
checks). (3) `lazy_mask_writes` counts draws (one per routed draw that met an application mask other than 15 on a target
it writes; the route's own fade-band RT2 = 0 write is excluded): 2 per mask-burst frame, 1 per plain-burst frame, 400 of
400 in `lazy-masked`. (4) Masked bench above. (5) `route-bench-lever3-{before,after}.json` are tracked. (6) A selected
lazy mask case without its per-draw twin fails the run; each lazy run is paired with the per-draw run of the same DLL
(`production-burst-perdraw-mask` added), the wrapper run with the plain seam twin. Reran on the rebuilt DLL: the six mask
cases, `seam-burst-perdraw`, `seam-burst-lazy`, `production-burst-lazy`, `seam-lazy-on`, `seam-ownership-lazy-on`,
`seam-taa-lazy-on`, `seam-taa-cutout-blended`, `seam-taa-fade-route-overlay`: exit 0.

### 2026-09-19 — lever 3: depth-routed bench, mask cases in the default suite

`run_route_bench.py --label lever3-depth` (`route-bench-lever3-depth.json`, one run, median µs per DrawPrimitive, 400
routed draws; `lazy-depth` / `lazy-cascades` are the `perdraw-depth` / `perdraw-cascades` configurations, wrapper and
lease per draw, in lazy RT mode): off 1.54, perdraw 8.70, lazy 6.99, perdraw-depth 10.67, **lazy-depth 8.15**,
perdraw-cascades 11.24, **lazy-cascades 8.73**; `set_rt` 1600 against 4 with one flush per frame. Saving per
depth-routed draw **2.53 µs** (cascades 2.50), against 1.71 plain in the same run: the wrapper makes each of the four
removed SetRenderTarget calls dearer, as `lazy-ownership` showed. The six mask burst cases are now in the default `CASES`
list (184 cases; six short burst runs) and `compare_mask_twins` runs in the full and the selected path; the full suite
itself was not run.

### TAA far stabiliser, speed gate narrowed after run160 / run161 (2026-09-19)

`docs/architecture/taa-distant-line-fade.md`, section 10. Flight: shimmer almost gone, far detail blurry under camera motion.
Replay (`taa_resolve_replay.py ... far`, `GATES=`): the loss is the ~65-fold Catmull-Rom resampling at W 0.985 during SLOW
motion (run160, 0.085 px/frame: gradient energy x 0.775 under the flown 0.5-2 px/frame gate; 3 % at 0.8 px/frame; clip removal
+1.5 points; sharper Keys cubic brings the shimmer back, x 1.00). Gate now `LO,HI` = 0.03,0.25 px/frame by default and
settable as fields 5-6 of `--taa-far-stabiliser`: run160 shimmer x 0.55 -> x 0.70, gradient x 0.775 -> x 0.843; static unchanged
x 0.19. Constants only, no program changed (`resolve_far` 508 slots). `run_temporal_pass.py` exit 0, lattice mode 375
numerical / 17 state: ripple ratio 0.154 / 0.192 / 0.577 / 1.000 at 0 / 0.04 / 0.14 / 0.30 px/frame, past HI bit-identical
to the plain resolve; `test_taa_image_defaults` OK; dry-run forwards `0.985,0,80,130,0.03,0.25`.


### Run 47 B (2026-09-19) — session B, per-draw vs lazy RT mode at the busy station view

User runs (busy station, fighter save, ~60 s held each; `/tmp/x3-bottleX3-run{165,166,167}`): run165
`--motion-rt-mode perdraw --frame-timing` (user ~40 fps), run166 `--motion-rt-mode lazy --frame-timing`
(user ~38 fps). Reference run147 (no diagnostics): ~50 fps, 20.3 ms, 478-510 draws.

Mode lines confirmed by `grep`: both runs log `rt_mode=perdraw`/`rt_mode=lazy` correctly in
`motion_output_mode`/`motion_output_device`/`motion_output_frame`, and `--frame-timing` installs the
state-shadow hooks in both (`state_hooks installed=1 reason=frame_timing state_shadow=1 rs_mode=shadow`,
165: 101, 166: 98 occurrences of `rs_mode=shadow`). Contrast: run167 (`--profile`, no `--frame-timing`) logs
`state_hooks installed=0 reason=none rs_mode=get` throughout (156/156 lines) — state hooks are gated on
`--frame-timing`, not on RT mode.

Draw counts over the session follow the same shape in both runs (dip to ~185-282 draws/frame in the
`frame=600..2700` windows, back to ~478-512 in the tail), so it is the same scripted view, but the tail
window's draw count is not identical: 165 settles at 479 draws/frame for windows 2700-6000, 166 climbs to
511-512 for windows 3600-5700 (vs 477-478 at 3000-3300). Restricting to `frame_timing` windows with
`draws_p50>=400` (165: 13/20 windows, 166: 11/19) and averaging: `dt_p50_us` 165=23998 vs 166=24705
(+707 us/frame, i.e. 41.7 vs 40.5 fps in this window — consistent in direction with the user's 40/38 fps),
`draws_p50` 165=476 vs 166=502 (+26, +5.4%), `draw_p50_us` (frame total, all draw hooks) 165=4490 vs
166=4016 (-474), `draw_native_p50_us` (frame total, native draw call only) 165=1166 vs 166=1591 (+425),
`scene_p50_us` 165=2806 vs 166=2962 (+156). Proxy-side per-draw overhead (`draw_p50_us - draw_native_p50_us`)
fell from 3324 to 2425 us/frame (-899 us), in the direction and rough magnitude of the bench's predicted
route/set_rt saving. `motion_output_frame` lazy counters in the same steady windows: `set_rt` 1560.6/frame
(165, perdraw) vs 3.3/frame (166, lazy); `lazy_flushes` 0.0 vs 0.82/frame; `lazy_mask_writes` 0 in both;
`restore_failures`/`apply_failures` 0 in both runs, whole session (`grep -c` for nonzero `_failures=`: 0 in
both logs); no `what=lazy_flush` lines (not a log line this build emits) and no nonzero `_errors=` counters.

**No FPS gain despite the set_rt drop**, because the predicted saving is real but smaller than an increase
elsewhere that this telemetry does not explain: at matched draw counts (165 frame=2700, draws=479,
draw_native_p50_us=1153 -> 2.41 us/draw; 166 frame=3000, draws=478, draw_native_p50_us=1520 -> 3.18 us/draw)
the *native* per-draw D3D9 call cost is ~32% higher in lazy mode, and this persists across the whole tail
(166 windows at draws=511-512 hold ~3.2 us/draw native cost). Since `route_set_rt` sits inside
`route_draw`/`route_lazy_flush`, not inside the native draw span, this rise in `draw_native_us` is not
route bookkeeping by the field definitions in `docs/verification/telemetry.md`; it is unexplained by the
counters gathered here. The two runs are also not perfectly comparable: the tail-window draw count differs
by ~5-7% between them (511-512 vs 479), which is at most a partial explanation (native cost per draw rose
more than draw count did). **Open**: this run does not settle why lazy's native draw cost rose; a same-session
A/B (toggle RT mode mid-run, same camera script, matched draw count) with `--frame-timing` on both halves
would isolate whether it is scene variance or a real lazy-mode cost, which the telemetry summarized here
cannot distinguish.

### Light-map far fade, `--light-map-far-fade P0,P1[,G]` (2026-09-19): implemented, unflown, default off

Design and the law: `docs/architecture/taa-distant-line-fade.md` section 11. Tracked compact record of the rerun after review (case, exit, checks, uploaded gains, FP16 hashes, DLL/EXE/trace sha): `verification/results/bottle-X3/lightmap-far-fade-seam.json`, seam DLL `9b9f052f5e3d...`; capture logs local and untracked beside it (`motion-output-<case>-capture.log`) and in each case's `directory`. Bottle X3
(arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`), worktree build, nothing installed.

- **Seam cases** (`run_motion_output.py`, mode `lightmapfade`, `motion_output_lightmap_fade_inc.h`): hull pair
  `494fe349b8bc12ec / 7c83ed50c9894e44`, gain 4, fixture camera (P[0] 0.8, width 64), rows `diag(w)` with w = 1 / 128 / 64
  (identical raster), `P0,P1 = 1.25,3.75` (w 32 and 96), eight frames: near, far, mid, F4 off near/far, F4 on mid, Reset,
  mid, near. `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_motion_output.py
  seam-lightmap-far-fade-off seam-lightmap-far-fade-on seam-lightmap-far-fade-floor2 seam-ownership-lightmap-far-fade-on
  seam-hdr-on`: all exit 0, 56 checks each (seam-hdr-on 103, unchanged). Lock wait 8 min behind the user's game session.
  - off: uploaded `c217.w` 0 on every frame, near = far = mid = Reset images one FP16 hash (`..1c3a72`), F4-off base `..c16124`.
  - on (`G = 1`): uploaded gain 4 / 1 / 2.5 exactly at both ends; near hash = the off run's near hash, **far hash = the
    F4-off base hash** (the game's own image bit for bit), mid = base + 1.5 x light-map term within 1 FP16 code, the same
    after the F4 round trip and after Reset; `hull_lightmap_far_fade_frame` faded = 0,1,1,1,1,0, camera latched without TAA.
  - floor2 (`G = 2`): 4 / 2 / 3, far differs from the base, within 0 / 1 code. Ownership wrapper: identical hashes to plain.
- **Transformer** (`test_hull_lightmap_gain`, host, 100 programs): 600 dynamic variants (2 fills x 2 depth modes + 2
  share) equal the constant-gain variant minus its `def c223` with the MUL operand `c223.x -> c217.w`, byte for byte;
  the 8 programs without the term stay untouched; constant-gain output unchanged (existing oracle). 9 tests OK.
- **Host**: `test_motion_wrap_states`, `test_linear_material_live` (mock mirrors the option inertly),
  `test_motion_hdr_scene`, `test_comparison_hotkeys`, `test_original_fill`: OK. Launcher test: default exports no
  variable and drops an inherited one; `60,120 -> 60,120,1`; 10 malformed or out-of-range values, gain 1, linear
  materials, no HDR and `G > gain` refuse. `manage.py launch --dry-run ... --taa-far-stabiliser 0.985
  --light-map-far-fade 40,110` exports `X3M_LIGHT_MAP_FAR_FADE=40,110,1`. Law on the host (scratch): monotone, no step
  above 8.4e-5 gain per world unit, NaN / behind-camera / no camera keep the gain.
- **Performance pass**: the gain rides the existing c216-c217 upload (still one `SetPixelShaderConstantF` in
  `evaluate_draw`, asserted), no `Get*`, no allocation; one division per draw of a pair that has a gain variant.
- **Review fixes (rerun, same numbers)**: the latch is armed by `camera_state::request_consumer()` only after the DLL parser
  accepted the value, and without TAA/candidates the option reads a private `P[0]` (`lightmap_fade_m00_`), so
  `camera_scene_` and with it the fade-band arm's origin rule are exactly as without the option; `P1 <= 1e6` in the DLL
  too; creation-time `device_` guard; `light_map_far_fade_configured accepted=` logs the configure result; launcher help
  corrected. Coverage with the option on for the hull cutout pair and its fade-band/overlay arm:
  `seam-taa-fade-route-overlay-lightmap` (gain 4) and `...-lightmap-far-fade` (gain 4, fade on, near footprint), 5101
  checks each, 12 gained frames, 12 far-fade frame lines with faded = 0, every `FADE_ROUTE` line identical between the
  twins; plus host assertions that the single bind site's two gained selections are covered by the upload gate and that
  the arm never binds a gained variant. A FAR cutout draw on the sun lane is covered structurally only (the gained
  share variants are among the 600 byte-compared dynamic programs).
- **Not done**: no flight; no offline estimate on the run168 dumps; native Windows is source-compatible (documented D3D9
  constant upload only), not executed.

### TAA thin-region stabiliser, implemented unflown (2026-09-19)

`docs/architecture/taa-lattice-crawl.md`, sections 12-13. The crawl is the 8-phase shuffle of the plant's near-edge-on arm
(run175 arm 16.9 codes rms, p2p p99 151): the 3x3 variance clip discards an accepted history because the fringes are wider
than the box. `--taa-thin-region W[,RELAX[,LO,HI]]` (default off): on the 7x7 around depth-fragmented pixels, clip off and
history weight min(n/(n+1), W), closed by the fastest pixel within 6 px (gate 0.03-0.25 px/frame). Replay: arm 16.9 / 151 / 827
-> 3.3 / 23 / 6 (W 0.97), 2.7 / 19 / 6 (0.985); panels 4.5 -> 1.5; run153 station 5.3 -> 4.0; ghost on masked background with
the neighbourhood gate: run148 (0.49 px/frame) p99 0.7 codes, run161 (0.84) region closed (57 codes with a per-pixel gate).
`resolve_far` 495 slots, mask program 270; all other bytecode unchanged. `run_temporal_pass.py` exit 0, lattice mode 420
numerical / 19 state (shard ripple x 0.086, plain silhouette 0 of 29 952 px-frames differing, past the gate bit-identical);
pass +0.79 ms at 1280x768; generator and bloom `--check` PASS.

### TAA thin-region review fixes (2026-09-19)

`docs/architecture/taa-lattice-crawl.md`, section 13.1. Region grown by 5 px (run175 arm 3.20 / 24 / 6 -> 2.34 / 22 / 0 at W 0.97,
trails unchanged, frame share +0.01-0.03, no measurable pass-time cost); exact per-pixel weight targets when the far stabiliser
and the thin region are both on (`c5.x`, `resolve_far` 496 slots, mask 305); the far stabiliser alone bit-identical to the flown
program (fixture reference of commit dee6608c); line filter alone back to two mask draws (+0.22 ms); `--taa-thin-clip` beside
the far stabiliser / thin region refused on purpose (deliberate removal: inert on real data); options validated separately in
the route; one shared speed gate made explicit in the launcher. `run_temporal_pass.py` exit 0, lattice mode 459 numerical / 19
state. Seam cases, record `verification/results/bottle-X3/motion-output-partial.json`: 164 / 140 / 59 checks. Generator
`--check` PASS; only `temporal_line_mask` and `temporal_resolve_far` bytecode moved.



### Run 48 A (2026-09-20) — plant and distant-station flight

User-supplied sessions `/tmp/x3-bottleX3-run176`, `run177`, `run178` use bottle
**X3**, CrossOver Preview. Each session logs `FEX_X87REDUCEDPRECISION=1` and
`WINEMSYNC=1`; current `cxbottle.conf` records `WineArch=arm64` (the session does
not independently log the WineArch key). No agent launched the game or ran Wine.

- run176: baseline; plant capture, then distant-station save loaded.
- run177: user confirms stationary arm crawl fixed with thin region 0.97, far
  stabiliser 0.985, light-map far fade 40,110; crawl remains during camera rotation.
  Captures: frames 5674–5705 stationary, 6392–6423 rotating.
- run178: distant station judged acceptable, with minor residual flicker especially
  during rotation. No frame-image burst. All 8,195 fade records have camera valid
  and minimum gain 1, with 25–517 faded draws/frame. Aggregate minimum is not a
  station-specific measurement. Keep 40,110,1 as the next default recommendation;
  stronger dimming is untested. See [threshold recommendation](../architecture/taa-distant-line-fade.md#12-run-48-a-distant-station-default-recommendation-2026-09-20).

The only launcher edit in this checkpoint aligns its help suggestion with 40,110;
no resolved default or renderer input changed. Focused host check:
`PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_hull_lightmap_gain`
completed successfully (3 tests reported; 1 skipped because the local original
137-program shader corpus is absent). The check is not shader/runtime validation.

Run177 real-dump gate reconstruction on the visually checked gridded truss
(`900 50 1100 170`) gives stationary speed p50/p99 0.001/0.006 px/frame,
100% gate active on fragmented-region samples; rotation 6.852/10.142 px/frame,
only 170 of 256,122 region pixel-frames active (0.07%). This is replay-derived,
not a shader mask readback or a motion-compensated flicker score. The inherited
run175 spar crop was discarded. See [moving-arm limit](../architecture/taa-lattice-crawl.md#14-run-48-a-stationary-success-camera-rotation-limit-2026-09-20).

Reproduce the local gate witness:
`python3 verification/results/run48a-lattice-triage/reconstruct_gate_witness.py`
(loads `tools/analysis/taa_resolve_replay.py` with the shipped gate and an ungated
region). Local results JSON and wrapper syntax validate. Independent review ran
the host reproducer in 14.7 s and reproduced both burst result dictionaries exactly.


### Run52: lazy render-target binding accepted as launcher default (2026-09-20)

A/run187 measures corrected attribution; B/run188 and C/run189 compare
`perdraw` and `lazy` without the intrusive frame-timing setter hooks. The user
reports 48–50 / 49–51 / 52–53 FPS and no visible issues in C. All three runs
match the intended source/DLL identity. B/C proxy options differ only in RT mode;
both report `state_hooks installed=0 reason=none`.

At 478 draws, valid camera-status records bracket 63 B and 239 C ten-frame
samples. Median frame time is **19.70 / 18.90 ms** (about **50.8 / 52.9 FPS**),
with nearest-rank p95 20.9 / 19.7 ms. The 510- and 514-draw strata also favor
lazy by about 0.7 ms. These are separate-session strata, not temporal paired
measurements or proof of an exact causal saving. Transition samples are excluded
by bracketing, not simply by carrying the previous camera status forward.

The 11/39 periodic 478-draw route rows have the same 453 matched/routed/depth
draws. Render-target setter counts fall **1812 → 4**, with **0 → 1** lazy flush
and zero lazy mask writes. Apply/restore failures are zero. This is the intended
mechanism; no object, draw or image-quality reduction is involved.

**Decision:** together with the earlier byte-identical color/depth/motion/state
fixtures and this eligible flight's visual acceptance, make lazy the launcher
default when motion output is enabled. Explicit `--motion-rt-mode perdraw`
remains the fallback; feature-off launch behavior stays unchanged. No DLL
replacement is required. The earlier Run47 instrumented counter remains valid
history, but does not override this production-mode comparison. No hitch
elimination or native Windows runtime qualification is claimed.

Independent source/evidence review passed after correcting selection, percentile,
configuration and window-alignment checks. Five focused launcher tests pass.
Reproduction and compact results: [run52-triage](../../verification/results/run52-triage/result.md).
Submission/HDR/lease decisions are recorded in the [frame-time note](../architecture/engine-frame-time.md#run52-corrected-attribution-and-remaining-optimization-scope-2026-09-20).

## Run56 station flash: two-burst audit (2026-09-21)

The user identifies an asteroid-attached mine/refinery in The Hole, possibly
also the solar plant, flashing during left/right camera motion, including with
fog disabled. Either run200 F8 burst may have been intended. The earlier burst
16450–16481 does contain substantial camera rotation; only the final burst
43051–43082 is nearly stationary. Do not interpret the latter as contradicting
the reported movement.

A read-only audit reconstructs the light-map gain and matches admitted/faded/min
telemetry for all 64 frames. Earlier: 3,449 eligible draws, 3,321 faded; the 128
full-gain draws are player geometry. Final: 1,920 eligible draws, all correctly
full gain; the identified mine footprint is 24.884, below the accepted fade
start of 80. No gain bypass or apply/restore failure is demonstrated. The mine's
raw-image mean variation is 2.344%, versus 0.189% after TAA; a sampling
contribution is plausible, but no isolated light-map filtering comparison exists.

Earlier capture frames take a median 796 ms, versus approximately 14.2 ms/frame
before capture. Its 18/32 history resets cannot establish normal-play reset
frequency or the flash cause. A normal-speed recording with the bright/dim event
identified is the preferred next witness. Preserve the accepted 80,220 default.
[Local diagnosis and reproduction command](/tmp/x3-run56-lightmap-fault/diagnosis.md);
[compact session witness](../../verification/results/run56-triage/report.md).
No production patch or additional Wine run was made for this audit.

### Normal-speed recording and targeted counter flight

`lightmap1.mov` records flashes at 2.000, 4.600, 6.908333, 16.308333 and
16.425 seconds. Target-aligned native video crops show bursts of fine white
points/lines while adjacent asteroid brightness is comparatively steady. This
is presented-video evidence, not an isolated light-map shader measurement.
The user states this is a different sector from run200; do not label it The Hole.

A 166-sample camera-motion fit strongly associates run202 with video start
208.770 seconds into the log (0.610-degree RMS); video creation metadata agrees.
The user has not confirmed that association. Exact game-frame alignment remains
uncertain by roughly 1–2 frames. All five events align with net additions equal to 25.6–33.6% of the current
routed draw count; these are not measured missing-key fractions.

The leading hypothesis is the global TAA missing-key safeguard, whose threshold
is 0.25. Per-pixel invalid correspondence already rejects newly observed
surfaces, but the global guard historically also protects sector/destruction
transitions with stale epochs. Do not disable it as a production default on this
evidence. Run57 compares process-local bounds 0.25 and 1 while logging every
frame, without F8 readback, preserving median-motion and camera protections.
Logging cost is unmeasured; this is a visual/reason-code comparison, not an FPS
benchmark. [Local report/reproducers](/tmp/x3-lightmap-video/diagnosis.md).

## Shader-shadow restoration lifetime fix (2026-09-21)

The first ownership-on Capture lifecycle fixture crashed restoring a pixel shader
on the routed final-application-release case: MotionOutput restored weak
application wrapper pointers that the application had already released, and the
unknown-pointer unwrap forwarded the dead wrapper natively
([diagnosis](../architecture/ownership-shadow-lifetime-diagnosis.md)). The
ratified fix owns scoped restoration references from the public shader getters
per routed draw and releases them from a scope guard at the top of `after_draw`.
Rerun of the unchanged fixture: **PASS, 159 checks**, routed row
`renderer_before=8 live=0 retired=1`, 5.4 s under the X3 lock; measured cost
exactly two getters and two Releases per injected draw, zero on unrouted draws,
zero declines. Independent review accepted with non-blocking findings (two
fixed before merge). Not witnessed: failed-getter decline, VS-only/PS-only,
state-block/Reset with a route in flight, inherited callback SEH. The B2a
Capture accounting and its fixture merge with the fix, default-off and unwired;
the upload diagnostic itself is held.
[Record](../../verification/results/run201-lattice/shader-restore-lifetime.json).
