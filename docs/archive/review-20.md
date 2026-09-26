# Review 20: camera reprojection for sentinel pixels and the loading-profile pipeline

Independent review of the uncommitted tree on top of `5ce7ab4`, limited to
(A) the live camera read (`src/proxy/camera_state.{h,cpp}`,
`object_trace::executable_verified`), the far-plane transform and policy
(`src/renderer/camera_reprojection.h`), their use in `motion_output.{h,cpp}`,
`capture.cpp`, `loader.cpp`, the resolve's fill-alpha far-plane branch
(`src/temporal/resolve.hlsl` and the regenerated
`temporal_resolve_program_inc.h`), the three `manage.py` options, the
motion-output and temporal-pass fixtures and runners, the two host unit tests,
`analyze_camera_state.py` and the five documents; and (B) the analysis-only
loading attribution pipeline (`analyze_loading_profile.py`,
`x3ap_function_labels.json`, `X3ProfileSymbols.java` invocation, its tests
and two documents). No game was launched; one Wine runner at a time; result
files were queried with scripts, never read whole.

## Checklist A

1. **Engine memory safety** - `camera_state::initialize()`
   (`camera_state.cpp:37-49`) runs only with `X3M_MOTION_OUTPUT=1` and
   `X3M_TAA=1` (the same exact-string test `capture.cpp:1170,1181` uses) and
   only after `object_trace::executable_verified()` (`object_trace.cpp:140-
   157`: base `0x400000`, DOS/NT headers, `SizeOfImage > 0x208b40` so both
   slots lie inside the image, file SHA-256; evaluated once per process and
   cached, shared with the trace's own `initialize`). The two slots are
   `VirtualQuery`-validated once at initialize (`readable(slot, 4)`); each
   buffer pointer is validated when its value changes and the verdict cached
   per index (`read_buffer`, `:26-35`). `readable()` (`:15-23`) requires
   `MEM_COMMIT`, no `PAGE_NOACCESS`/`PAGE_GUARD`, a readable protection and
   `address + size <= BaseAddress + RegionSize` with overflow guarded, so a
   64-byte buffer starting within 64 bytes of a region end is rejected as a
   whole (a buffer straddling two committed regions is rejected too:
   conservative). Reads happen in `after_clear` only (`motion_output.cpp:
   1127-1132`: the Background-to-Scene depth Clear and the latching Clear),
   never per draw; nothing in `camera_state.cpp` writes (no
   `WriteProcessMemory`/`VirtualProtect`; grep 0). `GetLastError` is saved
   and restored around both calls. The fixture seam
   (`fixture_install`) exists only under `X3M_MOTION_OUTPUT_FIXTURE`; the
   production DLL exports neither it nor the other fixture symbols (objdump
   0 hits). Observation 3 below on the cached verdict.
2. **Math** - derived independently: with `view = world * V` and orthonormal
   `R`, `world_i = sum_j view_j R[i][j]` (the header's `world[i] += view[j] *
   r[i*3+j]`) and `prev_j = sum_i world_i R_prev[i][j]`; with `m23 = 1`,
   `x_ndc = x_view m00 / z + m20`, so the direction is `((x - m20)/m00,
   (y - m21)/m11, 1)`; the builder's `A` (`camera_reprojection.h:96-97`)
   carries exactly the `-m20/m00, -m21/m11` constant row, `Q = R_cur^T
   R_prev` (`:101`, index `k*3+i` is the transpose), `B` re-projects with the
   previous off-center terms, and the transposition into column-vector rows
   with rows 2 and 3 both `W` (`:109-110`) gives `z/w = 1` and `w > 0` iff
   `prev_z > 0`. The resolve forms `currentClip` from the unjittered
   position with `y` up (`resolve.hlsl:142-143`), flips `y` back and adds the
   half texel plus the CURRENT jitter (`:150-153`), the routed path's
   convention; the far-plane `z` column is zero, so the `nearest` depth the
   dilation substituted is irrelevant. Orthonormality within 1e-3 against
   the measured 3.6e-5 residual; the `test_validation_failures` case proves
   3e-5 passes and codes 2-6 fire on the first failed check. Policy
   (`:133-149`): `auto`/`2` reach policy 2 only with both views valid, the
   rotation `<= cut` (a non-finite or non-positive bound always cuts) and a
   built transform; the cut sets `d.cut`, which the route ORs into
   `in.cut` (`motion_output.cpp:409`) so the pass resets history, not just
   the policy. Same device is implicit: `camera_previous_` is per
   `MotionOutput`, one per device.
3. **Shader** - the new branch (`resolve.hlsl:163-172`) runs only inside
   `options.x > 0.5 && motion.w != 1 && motion.w != 0`, and keeps the camera
   path only when `farPlane` (set solely by `depth <= -0.5` under policy 2,
   `:102-106`) and `dilate == 0` and `motion.w` is exactly -1 (`>= -1 &&
   <= -1`). A routed pixel has RT2 depth in `[0, 1]`: `farPlane` false,
   unchanged. A far-plane pixel with any routed neighbour in its 3x3 has
   `dilate != 0` (the neighbour's valid depth beats `nearest = 1`) and takes
   that neighbour's motion or rejects, as before. NaN depth: `depth <= -0.5`
   false, `validDepth` false, current-only; NaN alpha fails both `>=` and
   `<=`. `generate_rigid_motion_pixel.py --check` reproduced the 3,840-word
   program (table below); `temporal-resolve-program.json` records 3,840 and
   the documents say 3,840 (finding 1 for the two stale 3,794 statements).
4. **Environment-map exclusion** - the fixture's `env_faces()` issues
   `EndScene`, six `SetRenderTarget(face)` + `SetDepthStencilSurface` +
   `Clear(TARGET|ZBUFFER)` + a per-face view written to the fake globals + a
   material draw, then the main pair and `BeginScene`. `SceneBoundarySelector::
   advance` (`scene_boundary.h:174-193`) accepts in `AwaitInitialClear` only
   the initial Clear, in `Background` only phase draws and the depth Clear, in
   `Scene` only phase draws and the depth unbind, so the first face's
   `SetRenderTarget` returns false and `observe` rejects the frame
   (`Pattern`). From then on gate 2 stops every draw, the Background-to-Scene
   transition never fires (`camera_scene_` stays the `begin_frame` default),
   `before_stretch` returns at `state() != AwaitCopy`, and `before_present`
   (`motion_output.cpp:1593-1601`, pre-existing) calls `invalidate_taa()`,
   which now also clears `camera_previous_` (`:361`): dropped, not kept.
   The capture confirms both placements: frames 1 and 3 `draws=9 routed=0
   gate2=9 selector_state=9 taa_skip=2`, frame 3 `latched=0 filled=0`. Cost
   and a cheaper alternative: finding 2 (open).
5. **Bookkeeping** - `camera_previous_` is written only after a successful
   run and copy-back (`:450`), cleared by `invalidate_taa()` (run failure,
   copy failure, every `skip()` including `CameraState`, `before_present` of
   a frame that did not resolve, a failed Present) and by `before_reset`
   (`:844`, which also drops the pointer cache); `begin_frame` (`:1106-
   1107`) resets the per-frame scene/background states so a frame without
   the depth Clear cannot reuse the previous frame's view. Device release
   destroys the per-device object. `TaaSkip::CameraState = 9` fires only in
   strict mode for `CurrentInvalid`/`TransformFailed` (`:492-501`); a
   `PreviousInvalid` frame resolves (bootstrap) and a `RotationCut` frame
   resolves current-only with `cut`. The `camera_state` line is emitted from
   `after_present` for capture frames and every `camera_log_interval_`
   frames (`:1639`), one line, bounded; the runner asserts frames 0-8.
6. **Colour bit-identity** - against `5ce7ab4:verification/results/
   motion-output-summary.json`: `seam-taa-camera-sentinel1-on` and the
   rerun `seam-taa-on` both equal the committed `seam-taa-on` in all 12
   `color_hashes` and all 12 `color_hashes_before_boundary` (so the new
   program under policy 1 is bit-identical to the old one, and the switch at
   1 with a camera installed changes nothing). `seam-taa-camera-on` differs
   on history frames only (1, 4, 10, 11) and `seam-taa-camera-sentinel2-on`
   equals it; the runner asserts all of this per run.
7. **manage.py** - `--taa-sentinel 2` without `--taa`, `--camera-cut-deg
   0`, `--camera-cut-deg nan`, `--camera-log 0` all rejected with the
   documented messages; `--dry-run` with `--taa --taa-sentinel 2
   --camera-cut-deg 15 --camera-log 100` prints `X3M_TAA_SENTINEL=2`,
   `X3M_CAMERA_CUT_DEG=15.0`, `X3M_CAMERA_LOG=100`; the defaults print
   `auto`, `20.0`, `300`; `capture.cpp:1186-1192` parses them with the same
   bounds and falls back to `auto`/20/300.

## Checklist B

`test_loading_profile.py` holds the 11 tests the document names (all green,
table below). `scan()` (`analyze_loading_profile.py:59-82`) iterates the file
in binary line by line, hashing as it goes and keeping only `profile_*` lines
and the `loading.KEEP` names; nothing reads the log whole
(`test_scan_keeps_only_sparse_lines`). `x3ap_function_labels.json` has 84
entries with the fields `label`, `source`, `hooked_apis` only (longest label
82 characters, addresses `0x004xxxxx`/`0x005xxxxx`, sources are our own
notes; no decompiled text). `ghidra_command()` (`:289-294`) is
`analyzeHeadless <project> <name> -process X3AP.exe -readOnly -noanalysis
-scriptPath <dir> -postScript X3ProfileSymbols.java <rvas> <symbols>`: no
`-import`, no analysis, project not saved; the script reads `args[0]` and
writes `args[1]` only (`X3ProfileSymbols.java:33,75`), both under the
`--output` directory. The test asserts `-readOnly` and the argument order.

## Findings and fixes

1. **Documentation, fixed** - `src/temporal/README.md:195` still said "the
   compiled program is 3,794 words" and
   `docs/architecture/temporal-integration.md:459` still listed
   "Rebenchmark the boundary cost with the 3,794-word program at 5120x1440"
   as open although `motion-output.md` now carries that bench with the
   3,840-word program (resolve 1.756 ms). Both updated.
2. **Low, open** - an environment-map frame costs the whole frame's TAA and
   the next frame's (its keyed draws all miss, `cut_missing=1.0`), as
   documented. A cheaper selector behaviour is possible: tolerate, in
   `Background` and `Scene`, a bracketed sub-sequence `SetRenderTarget(cube
   face) ... SetRenderTarget(main)` whose Clears hit a different target and
   whose draws never touch the main pair (they would stay unrouted by
   `phase_draw`), and keep the frame. The scene camera read would stay
   correct in both placements because the game's main view activation
   (`0x0047c840`, with its own per-view Clear) runs after the face pass. The
   risk is in the selector's fail-closed design (a third depth epoch on the
   main pair must still reject) and in the history: the resolve would blend
   scene-to-scene across a frame whose faces were rendered with other views,
   which is correct since only the main pair enters the history. Not changed
   here: whether the game renders environment maps every frame or rarely is
   unknown until a gameplay log shows the `selector_state=9` count on frame
   lines, and the fixture's `envmap` script already pins the current
   behaviour, so the change would need its own fixture update.
3. **Low, documented here** - the per-pointer `VirtualQuery` verdict is
   cached for as long as the slot holds the same value; a buffer freed and
   decommitted while the slot still points at it would be read without a
   check. Mitigations already in place: the buffers are allocated once at
   renderer initialization (basis document), a device Reset drops the cache,
   and the read runs only from a Clear hook, which needs a live device. The
   alternative (a `ReadProcessMemory` on the own process, as
   `object_trace::read_memory` does) would remove the class at about a
   microsecond per read, twice per frame, and remains available if a
   gameplay log ever shows `read_failure=3` after a Reset.
4. **Observation** - `camera_previous_frame_` is not cleared with
   `camera_previous_`, so the `camera_state` line shows the stale frame
   number beside `history_view_valid=0` (the runner asserts the flag, not
   the number). Cosmetic; noted for the next edit of `invalidate_taa`.
5. **Observation** - `camera_state::initialize()` hashes the 2 MB executable
   at DLL load even with `X3M_OBJECT_TRACE=0` (once, cached, tens of
   milliseconds), and the `camera_state` line is written every 300 frames
   also when the status is `executable_mismatch`/`disabled` (one line per
   five seconds at 60 fps). Both bounded; not changed.
6. **Observation** - strict mode's `CameraState` skip invalidates the
   history, so one unreadable frame costs two frames (the skip and the
   bootstrap). That is the documented diagnostic intent of `2`; `auto`
   degrades to policy 1 for the frame instead.

## Results after the fixes

| Suite | Result |
| --- | --- |
| `cmake --build build -j4` | OK (RelWithDebInfo, `-Wall -Wextra -Werror`); no source edits by this review, so the incremental build was a no-op; `run_motion_output.py` then relinked with `--clean-first` (the PE link timestamp changes the file hash on every relink; the code is identical) |
| `python3 -m unittest discover -s verification/analysis` | 528 tests OK (503 + 9 camera reprojection + 5 camera-state analysis + 11 loading profile) |
| `check_no_x87.py build/d3d9.dll` | PASS: 125 reachable functions, 0 violations |
| `run_motion_output.py` (full) | PASS: 35 cases, all exit 0; `seam-taa-on` 151 checks, `seam-taa-camera-on` 164 (policy 2 on frames 1-6, 8, 10, 11; history 1, 2, 4, 10, 11), `seam-taa-camera-sentinel1-on` 163 (colour equal to `seam-taa-on` and to `5ce7ab4`), `seam-taa-camera-sentinel2-on` 164 (equal to auto), `seam-taa-sentinel2-nocamera-on` 145 (12 frames `taa_skip=9`), `seam-taa-envmap` 62 checks / 18 restorations, rejected frames 1 and 3; camera path changed frames 1, 4, 10, 11 only. Bench rerun: boundary 0.360 / 0.793 ms at 1280x768 and 0.503 / 2.271 ms at 5120x1440 (resolve 0.433 / 1.768 ms; the author's run 0.387 / 1.756, within the spread of these samples) |
| `run_temporal_pass.py` | PASS: 416 numerical / 164 state checks, 2 generations, 386 samples; camera cases drift (px) static 0.069, yaw 0.124, pitch 0.176, yaw unjittered 0.062, narrow 0.026, single step 0.033 / 0.029, identity control 0.862, swapped control 1.051, after the cut 0.100 - the numbers the document states |
| `temporal_run.py` | PASS: 78/78 sample checks, sources and executable unchanged during the run |
| `run_ownership_integration.py` | PASS: 26 runs exit 0; build, verification, symbols and fallback reports all PASS |
| `run_scene_capture.py` | PASS: 4,908 checks, 16 samples, sources and executable unchanged during the run |
| `generate_rigid_motion_pixel.py --check` | PASS: `current_depth` 35 words `d097c156...`, `rigid_motion` 176 words `a604ce8c...`, `temporal_resolve` 3,840 words `5ce14b95...`, each equal to the embedded header and the provenance record; only our authored shaders |
| `manage.py --dry-run` | four rejections and two accepted forms as in checklist A.7 |

Final `build/d3d9.dll` SHA-256:
`27f693429e2c85692db8437efdf7b8010410aba1336387f0b76bb05e4892638a`.

Verdict: go for the checkpoint commit of the camera reprojection and the
loading-profile pipeline. The first game evidence is the next user-run
flight session with `--taa --telemetry` (default `--taa-sentinel auto`):
read its `camera_state` lines with `analyze_camera_state.py --metadata
verification/results/shader-registers.json` (draw-constant and object-trace
agreement, `read_failure`, `background_fov_mismatch`), the `camera_policy`
/ `camera_cut` distribution and the `selector_state=9` count on frame
lines (finding 2) before judging the background stability by eye.
