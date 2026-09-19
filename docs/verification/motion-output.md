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
