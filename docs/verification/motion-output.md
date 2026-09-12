# Live motion route (checkpoint B1 + temporal step 1) verification

Synthetic verification of the live same-draw route through the actual proxy
DLL under CrossOver Preview's Steam bottle with the process-local `d3d9=n,b`
override. No game launch; the reviewed shader pair is read from local files
and never enters the repository or the reports.

```sh
python3 verification/probe/run_motion_row_history.py     # host unit fixture, release + ASan/UBSan
python3 verification/probe/run_motion_output.py          # fresh build + eighteen DLL runs (four environments + jitter)
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

Each DLL runs with the route off and on in four environments (`VARIANTS` in
`run_motion_output.py`), plus one plain run per DLL with the route and the
jitter on: **plain** (the original four runs), **ownership**
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

The first gameplay checkpoint is a user-managed capture with the route, the
ownership wrapper and both object observers active (object lifetime requires
the wrapper; without the observers the route runs sentinel-only). Install the
current `build/d3d9.dll` and launch:

```sh
python3 tools/manage.py install
python3 tools/manage.py launch --direct --ownership --object-trace --object-lifetime --motion-output --telemetry --capture-start 999999 --capture-frames 4
```

`launch --dry-run` with the same options validates them and prints the command
and `X3M_*` environment without starting the game; verified against a scratch
game directory: `X3M_OWNERSHIP=1 X3M_OBJECT_TRACE=1 X3M_OBJECT_LIFETIME=1
X3M_MOTION_OUTPUT=1 X3M_TELEMETRY=1 X3M_CAPTURE_START=999999
X3M_CAPTURE_FRAMES=4`, depth copy, scene depth, finite positions, mesh cache
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

Synthetic device program only: no gameplay, no TAA, no temporal consumer, one
reviewed pair. Jitter is proven by coverage on a 64×64 target and the cut
detector's verdict is data only. Object scope is injected; the game observers are not exercised,
so the wrapper environments prove the route's fill, routing, Reset and release
through the wrapper, not object history through it. Native Windows is
cross-compiled but not executed. Setter-hook cost in the game is unmeasured.
