# Chase camera for the external back view (`X3M_CAMERA=chase`)

Integrated design and implementation (2026-09-13, reviewed:
[review 31a](../verification/review-31-chase-camera-architecture.md) /
[31b](../verification/review-31-chase-camera-implementation.md) and
[review 35](../verification/review-35-chase-integration.md), their findings
applied). The first user flight confirmed the hook applies, but reported
trembling while flying and requested lower framing. The native-anchor
correction and new framing default await another user run; see
[first-flight investigation](../reverse-engineering/chase-camera-first-flight.md). Evidence: [external-camera.md](../reverse-engineering/external-camera.md).
Code: `src/proxy/chase_camera.{h,cpp}` (hook, engine reads/writes, diagnostics),
`src/proxy/chase_camera_math.h` (portable pipeline),
`src/proxy/chase_camera_native.h` (native anchor selection), `engine_patch.h`
(`SiteSpec::rel32_offset`). Tests: `verification/analysis/test_chase_camera.py`
(host pipeline/integration, 37 cases), `test_chase_camera_site.py` (read-only site probe, 8
cases, including the installed executable).

## Decision recap

The camera is modified **in the engine**: the smoothed pose is written into the
cockpit's sector camera before the frame routine builds the view matrix from it,
so the scene, the HUD target overlay, the mouse-aim ray and the proxy's TAA
reprojection all read one camera. Only the external back view is replaced;
internal/front/side/scripted views pass through, so the game's own view keys
remain the in-game switch. Default off: without `X3M_CAMERA=chase`
(`tools/manage.py launch --camera chase`) nothing is patched and the plain
`--direct` run is unchanged.

## Hook site (verified bytes)

| Item | Value |
| --- | --- |
| Function | `FUN_004205e0`, the per-frame cockpit update (called from `0x0041cde0` at `0x00403f2f`, right before the frame routine `0x00471f50` at `0x00403f34`) |
| Site | `0x00420e06`, the first instruction after the sector camera's vanilla pose is final and before the engine derives camera-relative object coordinates, the target overlay, the galaxy/dust camera copies and the render from it |
| Expected bytes | `83 7b 54 00 0f 84 09 02 00 00` (`cmp dword ptr [ebx+0x54],0` ; `jz 0x00421019`), length 10 |
| Relocation | `engine_patch::claim` with `rel32_offset = 6`: the tail holds the two instructions with the `jz` re-based to the same absolute target, then `jmp 0x00420e10`; the site's first five bytes become `jmp dispatcher`; no other branch in the function targets the interior of the span (probe check) |
| Atomic write | the five bytes straddle the qword at `0x00420e08`: plain copy, protected by the install window (claimed on the backend-load path before the device exists, `late_claim` afterwards) |
| Live registers | EBX = cockpit object; the stub saves flags, all general registers and XMM0–7 and passes the `pushad` block; the handler runs under the full CPU boundary (`PreserveCpuState`: x87 state, MXCSR, last error) because the libm transcendentals and the log formatter it may reach are x87 code. `PreserveCpuState` restores the caller's x87 stack after saving it, and this is a mid-function site rather than a call boundary, so the handler executes `fninit` and loads the standard masked, round-to-nearest MXCSR `0x1f80` after the save. Its x87 and SSE2 arithmetic therefore cannot inherit the game's stack, exception masks, rounding mode, FTZ or DAZ; the destructor restores the game's full x87 and MXCSR state before the relocated `cmp`/`jz` runs |
| Stub | `pushfd; pushad; sub esp,0x80; movups [esp+16i],xmm_i; lea eax,[esp+0x80]; push eax; call x3m_chase_camera_enter; add esp,4; movups xmm_i,[esp+16i]; add esp,0x80; popad; popfd; jmp [next]` (arena, `engine_patch::Emitter`); `next` = the tail, chained with `push_front` like the loading probes |
| Fail closed | executable identity (`object_trace::executable_verified`: structure, global anchors, size, base; no file hash, `docs/reverse-engineering/executable-identity.md`), exact bytes, install window, valid tunables, QPC available; any refusal logs `chase_camera requested=1 installed=0 status=<reason>` and leaves the vanilla camera |
| Lifetime (review A3) | the site is kept for the **process lifetime**, like the loading probes and the object-trace patch. The install window closes at the first Present, so a restore at the last-device release could never be undone for a device recreated after a resolution change (the engine_patch rule: no claim over code other threads may execute), and the handler needs no device (it runs inside the cockpit update on the main thread). The last-device path logs `chase_camera_last_device kept=1` instead of restoring; `chase_camera::shutdown()` (`restore_not_owned` if the bytes are not ours) exists for explicit teardown only and nothing calls it on the device path. The arena is never freed either way |

## Engine reads and writes per frame

All reads go through `engine_memory::read` (validated, bounded; direct mode
with the per-frame region cache). Pointers must be non-null and 4-aligned
(cockpit, sector camera, ref object, render node, tracked object).

**Active-cockpit gate (review A1).** The registry walk `0x0041cde0` calls the
cockpit update once per registered cockpit object per frame, and the engine
keeps more than one (monitor cockpits, `INS_CockpitSetMonitorNumber`; the
"target view" following another object; `INS_SetActiveControlCockpit`). The
handler acts only on the cockpit whose **ref view object `+0x10` is non-zero
and equals its ref object `+0xc`** — the player's own ship seen by its own
view, the predicate the fire control `0x00445170` uses (`*(cockpit+0x10) ==
this ship`). Every other cockpit's visit increments `refused_inactive`, records
the pointer in the per-window distinct-cockpit count and returns before the
clock (`dt`), the pipeline state, `frames`/`applied`/`refused` or the
snap flag are touched. Behind the predicate the old guard stays as defence: if
the active cockpit pointer still changes between invocations the pipeline
takes a gap (the next frame snaps rather than mixing two objects' poses and
dt). The registry's active-control handle (`*(*0x00608504+0x10)`, the native
resolver `0x0041cd20` walk) admits one more case: a fresh cockpit generation
whose `+0x10` is still 0 (run78: 36 and 12 updates after a gate transit while
the engine already renders its rear view from it) is admitted when
`chase_transition::active_control_cockpit` proves a complete lifetime whose
handle row maps to the cockpit (`admitted_unbound` in the report line;
docs/reverse-engineering/chase-view-transition.md, "Run 28 (run78) pose
gap"). Gameplay consequence of that phase: the chase pose is written from the
first valid frame, but cursor-fire, lead and aim contexts stay cleared until
`+0x10` binds (0.4-1.1 s in run78), as they would under a refusal. `cockpits_seen`
(distinct EBX values per 300-frame window) and `refused_inactive` tell how
many cockpits reach the site.

| Read | Bytes | Use |
| --- | --- | --- |
| cockpit (EBX) | `0x200` | `+0xc` ref object, `+0x10` ref view object (gate), `+0x58` sector camera, `+0xf0` view-relative basis `R_view` (12 ints), `+0x150` view mode, `+0x1a0` flags (bit 2 = verbatim basis), `+0x1c0` connect mode, `+0x1d8` aim gun (logged), `+0x1e0` tracked object, `+0x1e4` tracking mode (short), `+0x1fc` sector |
| tracked object `+0x1e0` `+8` | 4 | pointer validity for the lock predicate (A9): non-null, 4-aligned, readable |
| cockpit-scene camera `+0x08` | `0x70` | only with `X3M_CHASE_SCENE_FIX=1` (A5): `+0x30` position, `+0x40/+0x50/+0x60` basis |
| sector camera | `0x310` | `+0x30..+0x38` vanilla position, `+0x40/+0x50/+0x60` vanilla basis, `+0x298` FOV, `+0x300/+0x304` view plane |
| ref object | `0x74` | type `+0x48`, data `+0x50`, owner `+0x54`, render node `+0x70` |
| owner `+0x48` | 2 | native position-domain predicate: non-null owner with type 1 selects the base domain |
| node `+0x30` or `+0xb0` | 12 | required native anchor: base domain selects `+0x30`, otherwise `+0xb0` |
| node `+0xb0`, basis `+0xc0`; native basis node `+0x40` or ship data `+0x870` | 12 / 48 each | optional domain/basis diagnostics; unavailable diagnostic reads do not reject the anchor |
| `*0x00606f38` `+0x28/+0x2c` | 8 | default view plane (FOV fallback) |

Writes, only after `Verdict::Applied` and only into the objects just read,
each re-validated as committed writable memory with a fresh `VirtualQuery`
before that frame's writes: camera `+0x30/+0x34/+0x38` (position), camera `+0x40/+0x50/+0x60`
rows (three ints each; the fourth words untouched), cockpit `+0xf0/+0x100/+0x110`
rows (`R_view' = B_cam' × B_shipᵀ`). A range/NaN failure or an unwritable page
counts `write_refused` and leaves the vanilla pose.

**Cockpit-scene camera (`X3M_CHASE_SCENE_FIX=1`, default off).** The layer-0
camera at cockpit `+8` is built at `0x00420787`, before the site, from the
**current vanilla** `+0xf0`, regenerated earlier at `0x00422c5c`. The original
review's previous-frame premise was incorrect. On an applied frame the optional
correction is `basis(+8)' = R_view'_now × R_view_vanilla_nowᵀ × basis(+8)`,
re-orthonormalized, with its shake position re-expressed through the same
rotation. This retains the native shake transform while matching the smoothed
view. `scene_fix_deg` is the full view correction, not a one-frame lag delta.
The flag stays off until an external-view element is shown to use that scene;
its runtime appearance remains unverified.

## Pose pipeline (`chase_camera_math.h`)

Conventions: basis rows = the camera's right/up/forward axes in world space
(row-vector, left-handed, `world = local × B`); positions are int32 engine
units kept exact in doubles; `B_cam = R_view × B_ship` is the engine's identity.

1. **Inputs**: vanilla camera pose `(p_v, B_v)`, `R_view`, ship position
   `p_ship` selected with the native branch above, view mode, connect mode, ref object, sector, `tan(half vfov) =
   tan(π · fov298/65536) × H` (H = the camera's `+0x304`/65536 or the default).
2. **Guards** → pass-through (`refused`, state reset): non-finite input; basis
   orthonormality error > 0.02 (`|B·Bᵀ−I|`, `|det−1|`, so mirrors are refused);
   view mode 1 (internal); **connect mode 3 or flag `+0x1a0 & 4`** (verdict 7,
   review A2: the engine wrote `camera.basis = +0xf0` verbatim at `0x00420c0c`,
   so the derived ship basis would be the identity and the back-view test
   would run in world axes); **any other non-zero connect mode** (verdict 3:
   4/5/6/8/9 are scripted/cinematic, 1/2/7 have no study semantics and pass
   through as a precaution — the frame line's `connect=` shows whether the
   back view ever uses one); zero boom.
3. **Effective ship basis** `B_ship = R_viewᵀ × B_v` (orthonormalized). The
   boom `o_v = p_v − p_ship`, in the ship frame `o_local = o_v × B_shipᵀ`.
4. **Back-view test** (the external views are script-defined, so the mode
   integer cannot tell them apart): `o_local.z < 0`, `|o_local.x| ≤ 0.6 |z|`,
   `|o_local.y| ≤ 1.5 |z|`, `R_view[2][2] > 0.7` to **enter**; once tracked the
   view **leaves** only past `0.8 |z|`, `2.0 |z|`, `0.5` (hysteresis, review
   A6: a transition animating `+0x130`/`+0x90..` through the boundary cannot
   flip the verdict every other frame, each flip being a refusal and each
   return a snap). Otherwise pass-through. The constants have no runtime
   evidence yet (review O3): the frame line carries the observed `boom_local`.
5. **Snap** (state reset to the target, counted, reason bits): first applied
   frame after a gap (1), ref object change (2), sector change (4), view or
   connect mode change (8), ship displacement per frame > 20 × boom (16:
   gate jump, jumpdrive, load; a per-frame ratio, not a velocity — at a
   ~200-unit boom that is ~4000 units per frame, far above flight speeds and
   far below a jump), numeric failure (32). **Coalescing** (review A4): a gate
   jump is the teleport snap (16) and, one frame later when `+0x1fc` catches
   up, a sector-only snap (4); a sector-only snap within `snap_coalesce_frames`
   (3) applied frames of the previous snap re-seats the springs (a fraction of
   a degree of lag at most that soon after a snap) but does **not** raise the
   TAA cut again (`coalesced` counter). Every other reason moves the world or
   the view and always cuts.
6. **Target orientation/framing**: positive `pitch_down_deg` uses the
   native view yaw in a ship-up frame, looks down by that angle, and rebuilds
   the boom to preserve scaled distance and project the anchor at `offset_y`
   below center. Native camera-local pitch/roll are replaced; ship world roll
   is retained. See [elevated geometry and guards](elevated-chase-camera.md).
   Explicit zero preserves the old target `B_t = pitch_up(δ) × B_v`,
   `δ = atan(offset_y × tan(half vfov))`, and the native scaled boom.
7. **Orientation spring**: `x = log(B_tᵀ × B_c)` (rotation vector from the new
   target to the current basis, world frame), critically damped closed form
   with `ω = 1/τ`: `x(dt) = (x + (v + ωx) dt) e^{−ω dt}`, `v(dt) = (v −
   ω(v + ωx) dt) e^{−ω dt}` (exact for a fixed target, hence stable and
   frame-rate independent), clamp `|x| ≤ lag_clamp_deg` (outward velocity
   removed), `B_c = B_t × exp(x)`, re-orthonormalized. Roll is part of `x`.
   `τ = rot_tau × (1 − combat_tightness)` while a target is locked (step 7a),
   `rot_tau` otherwise. Known approximation (review O1, no code change): `v`
   is a world-frame vector that is not transported when the target rotates
   between frames — `x` is re-measured, `v` keeps its old direction; second
   order at the 8° clamp, a small extra swing only if the clamp were tuned
   far above ~30°. The fix, if ever needed, is `v ← v × exp(log(B_t_prevᵀ ×
   B_t))` before the step.
7a. **Combat tightness (review A9, `X3M_CHASE_COMBAT_TIGHTNESS`, default 0 =
   off, unverified in game)**: the handler reads the cockpit's tracking state
   — `+0x1e4` (short) is what the dispatcher tests for `INS_CockpitIsTracking`
   (`== 1`) and `INS_CockpitIsEnemyTracking` (`== 4`, cases `0x24`/`0x25`), and
   `+0x1e0` is the tracked object (`INS_CockpitGetTracking` returns
   `*(+0x1e0)+8`; the fire control takes `*(cockpit+0x1e0)` as the aim target
   when `+0x1d8 ≥ 0`). `target_locked = (+0x1e4 ∈ {1, 4}) ∧ +0x1e0 is a
   non-null, 4-aligned pointer whose `+8` reads through engine_memory`. While
   locked both time constants are scaled by `(1 − tightness)`; tightness 1
   gives τ = 0, which the spring treats as a rigid follow (zero lag). The
   frame line reports `tracking=<+0x1e4> locked=<0|1> locked_frames=`, so the
   first run validates the field semantics with tightness 0 before any value
   is used; the study's field evidence is in [external-camera.md §2](../reverse-engineering/external-camera.md).
8. **Boom spring**: target boom is the elevated framing construction in step
   6, or `o_t = (o_local × distance_scale) × B_ship` with explicit zero pitch;
   `x_p = o_prev − o_t` (both ship-relative: constant velocity gives no lag,
   turns and boom changes swing), same closed form with `pos_tau` (scaled by
   the same tightness factor while locked), clamp `|x_p| ≤ pos_lag_clamp ×
   |o_t|`; `o = o_t + x_p`; `p_c = p_ship + o`.
9. **Outputs**: `p_c` (rounded, int32 range checked), `B_c` (16.16, rounded),
   `R_view' = B_c × B_shipᵀ`. Ship-on-screen excursion is bounded by
   `lag_clamp_deg + atan(pos_lag_clamp)`.

`dt` is wall-clock time between handler invocations (`QueryPerformanceCounter`,
documented Win32), clamped to `max_dt` (0.1 s) after pauses/loads; SETA
scales the engine's game clock (`+0x718`) but not this. A frame the handler
does not see (hook inactive, refused, other views) resets the springs, so the
next applied frame snaps.

## Tunables (environment; `tools/manage.py launch --camera chase …`)

The mode switch stays `X3M_CAMERA=chase|vanilla` (`--camera`); the tunables
are `X3M_CHASE_*` / `--chase-*` (review O7) so they cannot collide with the
TAA camera read's `X3M_CAMERA_CUT_DEG` / `X3M_CAMERA_LOG`. The second user
flight reported no trembling after the native-anchor correction. New softer
defaults are 0.28 s rotation / 0.38 s position; lag limits remain 8 degrees /
0.10 of boom length. Larger tau softens onset and extends settling while
sustained turns retain the same clamp bounds. See the
[elevated framing iteration](elevated-chase-camera.md) for geometry, legacy
compatibility and verification; these new defaults await game acceptance.
Framing defaults changed on 2026-09-16 after run 26 accepted the raised camera:
pitch-down 13° → 0.5° and `offset_y` 0.45 → 0.50, the near-parallel elevated
camera of [chase-hud-reticle-survey.md](chase-hud-reticle-survey.md) (ship at
75 % of screen height, forward vanishing point ~4.5 px above centre, so the
`centre` HUD anchor, the default until 2026-09-23, is a true boresight cue). A non-zero 0.5 keeps the
elevated construction; only an explicit 0 selects legacy framing. The launcher
forwards both constants explicitly in chase mode.
Defaults changed again on 2026-09-23 (user decision): distance scale 0.90 → 1.05 (compiled default and launcher, now forwarded explicitly in chase mode like the two framing constants) and the HUD anchor `centre` → `forward` ([hud anchor](chase-view-restore-and-hud-anchor.md)).

| Variable | Flag | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `X3M_CAMERA` | `--camera chase` | vanilla | `chase` | install the hook |
| `X3M_CHASE_ROT_TAU` | `--chase-rot-tau` | 0.28 s | (0, 10] | orientation spring time constant (for the critically damped form 63 % of a step is done in ~2.15τ and 95 % in ~4.75τ) |
| `X3M_CHASE_POS_TAU` | `--chase-pos-tau` | 0.38 s | (0, 10] | boom spring time constant |
| `X3M_CHASE_OFFSET_Y` | `--chase-offset-y` | 0.50 | [−1, 1] | ship below centre, fraction of the half screen height (negative = above centre); 0.50 projects the settled anchor at 75% of screen height in elevated mode; silhouette and lag can shift the visible center |
| `X3M_CHASE_PITCH_DOWN_DEG` | `--chase-pitch-down-deg` | 0.5° | [0, 30] | downward look in the ship-up/native-yaw frame; zero restores legacy framing geometry |
| `X3M_CHASE_DISTANCE_SCALE` | `--chase-distance-scale` | 1.05 (0.90 before 2026-09-23) | (0, 10] | multiplies the vanilla boom (the scripts already size it per ship class) |
| `X3M_CHASE_FOV_COMPENSATE` | `--chase-fov-compensate` | 1 (on) | 0/1 | also multiplies the boom by `clamp(0.75 / half_vfov_tan, 0.5, 2)` from the live projection, so the ship keeps its vanilla screen size under `--fov`, the FOV menu and zoom ([ledger](../verification/field-of-view.md), "Chase camera compensation") |
| `X3M_CHASE_LAG_CLAMP_DEG` | `--chase-lag-clamp-deg` | 8° | [0, 90] | orientation lag clamp = the screen window |
| `X3M_CHASE_POS_LAG_CLAMP` | `--chase-pos-lag-clamp` | 0.10 | [0, 1] | boom lag clamp as a fraction of the boom |
| `X3M_CHASE_COMBAT_TIGHTNESS` | `--chase-combat-tightness` | 0 | [0, 1] | scales both time constants by (1 − tightness) while the cockpit reports a target lock (step 7a; field semantics unverified in game); the install line says `combat=off` at 0 and `combat=tracking_1e4_unverified` otherwise |
| `X3M_CHASE_SCENE_FIX` | `--chase-scene-fix` | 0 | 0/1 | also re-express the cockpit-scene camera (A5) each applied frame |
| `X3M_CHASE_MAX_DT` | – | 0.10 s | (0, 5] | dt clamp |

Invalid values fail closed (`status=invalid_tunables`, nothing patched); an
over-long value (≥ 64 characters) is invalid too, an empty one counts as
unset. `snap_coalesce_frames` (3) is compiled in.

## Interactions

- **TAA cut detector**: the camera publishes a process-wide monotonic snap
  generation. Each `MotionOutput` has its own `SnapCursor` and observes the
  generation where resolve inputs are assembled (`motion_output.cpp`, beside
  `camera_sentinel_policy`), so every device receives the cut independently.
  A successful snap write, and the first transition from an applied chase pose
  back to a vanilla pose, sets `in.cut` and `camera_cut` even when rotation is
  below `X3M_CAMERA_CUT_DEG`; a failed first write publishes no nonexistent
  pose, while a write failure after an applied pose publishes the exit cut.
  The cursor advances before the temporal run because every failure after that
  point invalidates that device's history. Without the route the generation is
  simply unobserved. Between transitions the ordinary rotation bound continues
  to operate on the view `camera_state` reads at the per-view Clear.
- **Mouse aim**: the cursor-fire branch unprojects through the sector camera's
  FOV/viewport and rotates main-gun input by cockpit `+0xf0`. Updating the
  camera basis and `+0xf0` together preserves that angular identity. Admission
  still depends on script-supplied fire/cursor state, the native cone remains,
  and the finite endpoint originates at the gun group rather than the camera.
  Fire control is called by the script dispatcher; its exact ordering relative
  to the displayed camera pose has not been measured. Cursor-fire alignment
  remains unresolved; see [the targeted study](../reverse-engineering/chase-mouse-fire.md).
- **HUD**: the target overlay and the galaxy/dust camera copies derive from the
  sector camera after the site. The layer-0 cockpit-scene camera is built
  before the site from the current vanilla `+0xf0`, so its view remains vanilla
  unless the optional correction is enabled; the next run checks whether any
  external-view HUD element lives there, and `X3M_CHASE_SCENE_FIX=1` corrects
  it in the same handler if one does (above).
- **Native Windows**: the hook, reads, writes and timing use documented Win32
  only (`VirtualProtect`, `FlushInstructionCache`, `VirtualQuery`,
  `QueryPerformanceCounter`); nothing depends on Wine.
- **Reset/device loss**: irrelevant, the state is engine-side; the hook is
  kept for the process lifetime (A3 above), so a resolution change that
  recreates the device keeps the chase camera.
- **Pause/menus**: whether every menu continues the cockpit update is a
  first-run observation. In either case the wall-clock `dt` of the next handler
  invocation is clamped, bounding the first resumed spring step.

## Diagnostics

- Install: `chase_camera requested=1 installed=<0|1> status=<active|reason>
  site=0x00420e06 length=10 rel32_offset=6 atomic_write=0 arena_used=…
  rot_tau=… pos_tau=… offset_y=… pitch_down_deg=… distance_scale=… lag_clamp_deg=…
  pos_lag_clamp=… combat_tightness=… combat=<off|tracking_1e4_unverified>
  max_dt=… snap_coalesce_frames=3 scene_fix=<0|1> handler_timing=<0|1>
  predicate=view_object_is_ref_object lifetime=process scope=external_back_view`.
- With telemetry enabled, per reporting window: `chase_camera_timing frame=…
  calls=… total_us=… mean_us=… max_us=… scope=handler_cpu window=report
  excludes=stub_and_cpu_save_restore`. It aggregates under the camera's own
  statistics lock; there is no per-invocation log.
- Once, at the first report after the first successfully written applied frame
  (review O2/O3, the static inferences): `chase_camera first_applied frame=… handler_frame=…
  half_vfov_tan=… fov298=0x… plane_w=0x… plane_h=0x… default_plane_h=0x…
  mode=… connect=… flags_1a0=0x… tracking=… aim_gun=0x… tracked=0x…
  locked=… ref=0x… view_obj=0x… boom_local=x,y,z cockpits_seen=…`. Expected:
  `half_vfov_tan` ≈ 0.75 at 4:3 / 0.5625 at 16:9 with `fov298=0x4000`
  (zoom off), `plane_h` the 16.16 view-plane height, `ref == view_obj`,
  `boom_local` with `z < 0` and small `x`.
- Per 300 frames and on capture frames (with `frame_end`): `chase_camera
  frame=… status=… frames=… applied=… refused=… refused_inactive=…
  admitted_unbound=… cockpits_seen=… snaps=… coalesced=… clamps=… write_refused=…
  verdict=<0 applied|1 internal|2 not back view|3 other connect mode|4 invalid
  input|5 degenerate|6 numeric|7 verbatim basis (connect 3 / +0x1a0&4)|100
  read failure> snap_reason=… mode=… connect=… flags_1a0=0x… tracking=…
  locked=… locked_frames=… lag_deg=… pos_lag=… distance=… dt_ms=…
  basis_dev_deg=… half_vfov_tan=… boom_local=x,y,z scene_fixed=…
  scene_fix_deg=…` (`frames`/`applied`/`refused` count the active cockpit
  only; `refused_inactive` the other cockpits' visits; `cockpits_seen` distinct
  cockpit pointers in the window; `basis_dev_deg` = angle between the derived
  ship basis and the matching native basis (not unconditionally node `+0xc0`);
  an unavailable basis diagnostic is −1).
- `chase_camera_window` at the same bounded report cadence adds the applied
  sample count, native base/render branch counts, latest branch and anchor-domain
  separation, valid domain-sample count and minimum/maximum separation. It also
  reports the matching native and render-ready basis deviations, separate
  report-window `rotation_clamps` / `position_clamps`, and report-window minimum /
  maximum rotation and position lag. Missing diagnostics are −1; extrema are
  zero when the corresponding sample count is zero. No per-frame log or heap
  allocation is added.
- Last device released: `chase_camera_last_device kept=1 status=active
  lifetime=process` (no restore; A3).

## Verification plan

1. Host (done): `python3 -m unittest verification.analysis.test_chase_camera
   verification.analysis.test_chase_camera_site` — closed-form step response
   against the analytic solution, frame-rate independence, clamps, snap
   reasons, pass-through verdicts, NaN/mirror guards, `camera = R_view × ship`
   identity, exp/log round trip and 16.16 conversion; the probe passes on the
   installed executable and fails closed on synthetic corruptions.
2. Prior integrated build (done; new native-anchor correction requires fresh qualification): `cmake --build build`, `verification/probe/check_no_x87.py
   build/d3d9.dll` PASS (the handler runs under the full boundary, so it is
   outside the light-hook rule: the object code is SSE2 arithmetic plus x87
   libm/CRT paths, review O10); the default `--direct` run patches nothing new
   (`launch --dry-run --direct` sets only `X3M_CAMERA=vanilla`).
3. Wine fixture (added, full runner result recorded in review 35): the
   engine-patch fixture (`loading_trace_fixture`) executes the taken and
   fall-through edges of forward and backward near-Jcc sites before and after
   relocation, restores the original bytes and behavior, checks the
   cross-qword plain-copy path, rejects an overflowing `rel32_offset`, and
   rejects a claim after the install window closes.
4. **Next user acceptance** (`launch --direct --camera chase --telemetry`, then with the route):
   the acceptance list in
   [review 31a](../verification/review-31-chase-camera-architecture.md)
   ("Acceptance criteria for the first user run") is the checklist: the
   install line `status=active`; the `first_applied` line's inferences; in the
   external back view (F2 cycle) `verdict=0` with `applied` growing,
   `refused` flat and `cockpits_seen` stable, `lag_deg` non-zero while turning
   and ≤ 8°, back under 0.1° within ~1 s, `basis_dev_deg` ≈ 0; HUD brackets
   and the mouse aim consistent; a gate jump: `snap_reason` 16 then 4 with
   `coalesced` +1 and exactly one `camera_cut=1`; internal/front/side/docking
   verdicts 1/2/3 with the vanilla pose; SETA-independent `dt_ms`; menu
   settle; a resolution change keeps the chase camera (A3); `tracking`/`locked`
   plausible with a target selected before any tightness is tried.

## Open questions

- The corrected native anchor no longer trembles in the second user flight.
  Elevated framing and softer spring defaults still need visual acceptance.
- Boom lag semantics: ship-relative (constant velocity = no lag, implemented)
  versus world-frame (acceleration lag, big steady lag at X3 speeds relative to
  the boom); the former is the safer default. A small clamped world-frame
  acceleration term could be added later for a "thrust" feel.
- Combat tightness: the `+0x1e4`/`+0x1e0` predicate is static reasoning; the
  weapon record's next-fire stamp and the fire-input bit (`param_4 & 2` at
  `0x00445170`) remain the alternatives if the lock state proves too coarse.
- `X3M_CHASE_SCENE_FIX`: enable only if the first run shows an external-view
  element rendered in the cockpit scene.
- The back-view thresholds (0.6/1.5/0.7, hysteresis 0.8/2.0/0.5) and whether
  connect modes 1/2/7 ever carry the back view (they pass through now).
