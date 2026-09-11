# Live motion route (checkpoint B1) verification

Synthetic verification of the live same-draw route through the actual proxy
DLL under CrossOver Preview's Steam bottle with the process-local `d3d9=n,b`
override. No game launch; the reviewed shader pair is read from local files
and never enters the repository or the reports.

```sh
python3 verification/probe/run_motion_row_history.py     # host unit fixture, release + ASan/UBSan
python3 verification/probe/run_motion_output.py          # fresh build + sixteen DLL runs (four environments)
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
synthetic node scope. Every frame: hostile render states, scissor and stream
bindings; color+depth Clear (the route latches and schedules the fill); a
background draw with the reviewed VS and the flat PS (the fill runs inside this
hook and every touched state is compared before and after); scene states;
depth-only Clear; scene draws with known submitted rows (`c24–27`), each
followed by a full state comparison; EndScene; color readback hash; motion
readback; Present.

Two DLLs are exercised:

- **production** – `build/d3d9.dll` unchanged. The default signatures do not
  recognize the synthetic background, so the selector never enters the scene
  phase: this proves fill, restoration, Reset with RT1 owned, readback files
  and device release without routing.
- **seam** – the production objects linked with `capture.cpp` and
  `motion_output.cpp` compiled under `X3M_MOTION_OUTPUT_FIXTURE`, exporting
  `x3m_motion_output_fixture_configure` (fixture background signature and
  per-draw synthetic scope) and `x3m_motion_output_fixture_readback`. The
  game observers cannot run in a synthetic process, and the selector's
  signatures are game hashes, so this seam is the only way to reach gates 5–6.

The seam script (DLL frame numbers): f0 first frame (mode 0 sentinel), f1
matched with the application writing `c252–255` and PS `c216–217` first, f2
scope withheld (sentinel-only mode, gate 5), f3 nothing recorded in f2 (gate
6), f4 matched plus one blend-enabled and one flat-PS draw that must not route
(gates 4 and 3), f5 duplicate key consumed once, f6 the duplicate poisoned the
key, f7 a `D3DSBT_ALL` state block Apply rebinding the flat PS is honored
(gate 3, flat PS still bound afterwards), f8 no f7 record, Reset, f9 history
restarted, f10 matched, shader release/recreate, f11 matched.

The CPU oracle replays each frame's draws per pixel with the depth test using
D3D9's integer raster sample convention, writing the previous-UV / previous
clip Z/W / validity ABI for matched routed draws and the sentinel otherwise;
pixels within 1.5 px of a coverage edge are skipped. Tolerances: 0.01 px UV,
4e-6 previous depth.

Each DLL runs with the route off and on in four environments (`VARIANTS` in
`run_motion_output.py`): **plain** (the original four runs), **ownership**
(`X3M_OWNERSHIP=1`, the wrapper the gameplay run needs for object lifetime),
**depth** (`X3M_OWNERSHIP=1 X3M_DEPTH_COPY=1 X3M_SCENE_DEPTH_CAPTURE=1`, the
copy-depth storage and the scene-depth adapter active in the eight requested
capture frames) and **admission** (`X3M_OWNERSHIP=1 X3M_ADMISSION=1`, the
process admission monitor the launcher environment may carry). The fixture
itself is unchanged: through the wrapper its device, shaders, buffers and
surfaces are canonical wrappers, the route's native slots are the wrapper's
methods, and the same six or thirty checks, 39 restoration comparisons, Reset
with RT1 owned and the zero final device/factory Release apply. The wrapper
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

| Environment | Case | Fixture checks | Restoration comparisons | Motion pixels | Matched pixels |
| --- | --- | ---: | ---: | ---: | ---: |
| plain | production off / on | 6 / 6 | 39 / 39 | – | – |
| plain | seam off / on | 6 / 30 | 39 / 39 | – / 44,284 | – / 10,261 |
| ownership | production off / on | 6 / 6 | 39 / 39 | – | – |
| ownership | seam off / on | 6 / 30 | 39 / 39 | – / 44,284 | – / 10,261 |
| depth | production off / on | 6 / 6 | 39 / 39 | – | – |
| depth | seam off / on | 6 / 30 | 39 / 39 | – / 44,284 | – / 10,261 |
| admission | production off / on | 6 / 6 | 39 / 39 | – | – |
| admission | seam off / on | 6 / 30 | 39 / 39 | – / 44,284 | – / 10,261 |

All 39 restoration comparisons in every run report zero differences. Every
seam-on run: maximum error 0.0016 px UV, 3.7e-8 previous depth, 19 routed-draw
decisions matched against the script. The color hashes of all 12 frames form
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

The device gate reports the mixed-format MRT self test passing on the
Preview backend (`color_errors=0 motion_errors=0`), four MRTs, 256 VS
constants and `D3DPMISCCAPS_MRTINDEPENDENTBITDEPTHS`.

Existing suites after the change: material-motion structure (16 rows, 161
check groups, 925,248 mutations, 96 aliases), material-motion GPU (Argon
1,182 checks, 2,952 samples, 82 configurations; 16 of 16 rows, 168
configurations, 2,376 checks), ownership integration (26 cases, rebuilt `build-ownership/`
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
reviewed pair. Object scope is injected; the game observers are not exercised,
so the wrapper environments prove the route's fill, routing, Reset and release
through the wrapper, not object history through it. Native Windows is
cross-compiled but not executed. Setter-hook cost in the game is unmeasured.
