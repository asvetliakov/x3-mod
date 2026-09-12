# Chase camera for the external back view (`X3M_CAMERA=chase`)

Prototype design and initial implementation (2026-09-13, worktree branch,
pre-review, **not yet run in the game**). Evidence: [external-camera.md](../reverse-engineering/external-camera.md).
Code: `src/proxy/chase_camera.{h,cpp}` (hook, engine reads/writes, diagnostics),
`src/proxy/chase_camera_math.h` (portable pipeline), `engine_patch.h`
(`SiteSpec::rel32_offset`). Tests: `verification/analysis/test_chase_camera.py`
(host pipeline, 21 cases), `test_chase_camera_site.py` (read-only site probe, 8
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
| Live registers | EBX = cockpit object; the stub saves flags, all general registers and XMM0–7 and passes the `pushad` block; the handler runs under the full CPU boundary (`PreserveCpuState`: x87 state, MXCSR, last error) because the log formatter it may reach is x87 code |
| Stub | `pushfd; pushad; sub esp,0x80; movups [esp+16i],xmm_i; lea eax,[esp+0x80]; push eax; call x3m_chase_camera_enter; add esp,4; movups xmm_i,[esp+16i]; add esp,0x80; popad; popfd; jmp [next]` (arena, `engine_patch::Emitter`); `next` = the tail, chained with `push_front` like the loading probes |
| Fail closed | exact executable (`object_trace::executable_verified`: SHA-256 `fdbf3418…`, size, base), exact bytes, install window, valid tunables, QPC available; any refusal logs `chase_camera requested=1 installed=0 status=<reason>` and leaves the vanilla camera |
| Restore | `chase_camera::shutdown()` when the last device is released (with the scene hook); `restore_not_owned` if the bytes are not ours |

## Engine reads and writes per frame

All reads go through `engine_memory::read` (validated, bounded; direct mode
with the per-frame region cache). Pointers must be non-null and 4-aligned.

| Read | Bytes | Use |
| --- | --- | --- |
| cockpit (EBX) | `0x200` | `+0xc` ref object, `+0x58` sector camera, `+0xf0` view-relative basis `R_view` (12 ints), `+0x150` view mode, `+0x1c0` connect mode, `+0x1fc` sector |
| sector camera | `0x310` | `+0x30..+0x38` vanilla position, `+0x40/+0x50/+0x60` vanilla basis, `+0x298` FOV, `+0x300/+0x304` view plane |
| ref object `+0x70` | 4 | render node |
| node `+0xb0` | `0x40` | ship position (`+0xb0..+0xb8`) and basis (`+0xc0..`, diagnostic only) |
| `*0x00606f38` `+0x28/+0x2c` | 8 | default view plane (FOV fallback) |

Writes, only after `Verdict::Applied` and only into the two objects just read,
each re-validated as committed writable memory (`VirtualQuery`, cached per
pointer): camera `+0x30/+0x34/+0x38` (position), camera `+0x40/+0x50/+0x60`
rows (three ints each; the fourth words untouched), cockpit `+0xf0/+0x100/+0x110`
rows (`R_view' = B_cam' × B_shipᵀ`). A range/NaN failure or an unwritable page
counts `write_refused` and leaves the vanilla pose.

## Pose pipeline (`chase_camera_math.h`)

Conventions: basis rows = the camera's right/up/forward axes in world space
(row-vector, left-handed, `world = local × B`); positions are int32 engine
units kept exact in doubles; `B_cam = R_view × B_ship` is the engine's identity.

1. **Inputs**: vanilla camera pose `(p_v, B_v)`, `R_view`, ship position
   `p_ship`, view mode, connect mode, ref object, sector, `tan(half vfov) =
   tan(π · fov298/65536) × H` (H = the camera's `+0x304`/65536 or the default).
2. **Guards** → pass-through (`refused`, state reset): non-finite input; basis
   orthonormality error > 0.02 (`|B·Bᵀ−I|`, `|det−1|`, so mirrors are refused);
   view mode 1 (internal); connect mode 4/5/6/8/9 (scripted); zero boom.
3. **Effective ship basis** `B_ship = R_viewᵀ × B_v` (orthonormalized). The
   boom `o_v = p_v − p_ship`, in the ship frame `o_local = o_v × B_shipᵀ`.
4. **Back-view test** (the external views are script-defined, so the mode
   integer cannot tell them apart): `o_local.z < 0`, `|o_local.x| ≤ 0.6 |z|`,
   `|o_local.y| ≤ 1.5 |z|`, `R_view[2][2] > 0.7`. Otherwise pass-through.
5. **Snap** (state reset to the target, counted, reason bits): first applied
   frame after a gap (1), ref object change (2), sector change (4), view or
   connect mode change (8), ship displacement per frame > 20 × boom (16:
   gate jump, jumpdrive, load), numeric failure (32).
6. **Target orientation** `B_t = pitch_up(δ) × B_v`, `δ = atan(offset_y ×
   tan(half vfov))`, so the ship sits `offset_y` of the half screen height
   below centre.
7. **Orientation spring**: `x = log(B_tᵀ × B_c)` (rotation vector from the new
   target to the current basis, world frame), critically damped closed form
   with `ω = 1/rot_tau`: `x(dt) = (x + (v + ωx) dt) e^{−ω dt}`, `v(dt) = (v −
   ω(v + ωx) dt) e^{−ω dt}` (exact for a fixed target, hence stable and
   frame-rate independent), clamp `|x| ≤ lag_clamp_deg` (outward velocity
   removed), `B_c = B_t × exp(x)`, re-orthonormalized. Roll is part of `x`.
8. **Boom spring**: target boom `o_t = (o_local × distance_scale) × B_ship`;
   `x_p = o_prev − o_t` (both ship-relative: constant velocity gives no lag,
   turns and boom changes swing), same closed form with `pos_tau`, clamp
   `|x_p| ≤ pos_lag_clamp × |o_t|`; `o = o_t + x_p`; `p_c = p_ship + o`.
9. **Outputs**: `p_c` (rounded, int32 range checked), `B_c` (16.16, rounded),
   `R_view' = B_c × B_shipᵀ`. Ship-on-screen excursion is bounded by
   `lag_clamp_deg + atan(pos_lag_clamp)`.

`dt` is wall-clock time between handler invocations (`QueryPerformanceCounter`,
documented Win32), clamped to `max_dt` (0.1 s) after pauses/loads; SETA
scales the engine's game clock (`+0x718`) but not this. A frame the handler
does not see (hook inactive, refused, other views) resets the springs, so the
next applied frame snaps.

## Tunables (environment; `tools/manage.py launch --camera chase …`)

| Variable | Flag | Default | Range | Meaning |
| --- | --- | --- | --- | --- |
| `X3M_CAMERA` | `--camera chase` | vanilla | `chase` | install the hook |
| `X3M_CAMERA_ROT_TAU` | `--camera-rot-tau` | 0.20 s | (0, 10] | orientation spring time constant (63 % of a step in ~1.7τ, 95 % in ~4.7τ for the critically damped form) |
| `X3M_CAMERA_POS_TAU` | `--camera-pos-tau` | 0.30 s | (0, 10] | boom spring time constant |
| `X3M_CAMERA_OFFSET_Y` | `--camera-offset-y` | 0.12 | [−1, 1] | ship below centre, fraction of the half screen height |
| `X3M_CAMERA_DISTANCE_SCALE` | `--camera-distance-scale` | 1.0 | (0, 10] | multiplies the vanilla boom (the scripts already size it per ship class) |
| `X3M_CAMERA_LAG_CLAMP_DEG` | `--camera-lag-clamp-deg` | 10° | [0, 90] | orientation lag clamp = the screen window |
| `X3M_CAMERA_POS_LAG_CLAMP` | – | 0.20 | [0, 1] | boom lag clamp as a fraction of the boom |
| `X3M_CAMERA_COMBAT_TIGHTNESS` | `--camera-combat-tightness` | 0 | [0, 1] | **parsed, inactive**: no readable target-lock/firing state was identified (the fire-control flags are call arguments of `0x00445170`, not stored); reported as `combat=inactive` |
| `X3M_CAMERA_MAX_DT` | – | 0.10 s | (0, 5] | dt clamp |

Invalid values fail closed (`status=invalid_tunables`, nothing patched).

## Interactions

- **TAA cut detector**: `chase_camera::take_snap()` is consumed by the motion
  route where the resolve inputs are assembled (`motion_output.cpp`, beside
  `camera_sentinel_policy`): a snap sets `in.cut` and `camera_cut` for that
  frame even when the rotation stays under `X3M_CAMERA_CUT_DEG` (a gate jump
  keeps the orientation and moves the world). Without the route the flag is
  simply never read. The smoothed pose itself is continuous otherwise, so the
  cut detector's rotation bound keeps working on the view `camera_state` reads
  at the per-view Clear (the engine builds it from the fields written here).
- **Mouse aim**: the fire control unprojects the cursor through the sector
  camera's FOV/viewport and rotates by cockpit `+0xf0`, never by the camera
  basis; writing `+0xf0 = B_c × B_shipᵀ` beside the basis keeps the engine's
  identity `B_cam = R_view × B_ship` exact for the smoothed camera, so the aim
  ray, the overlay projection (`0x0042a2d0` after the site) and the render
  agree. The fire control runs before the cockpit update in the loop, so it
  uses the pose of the frame the player saw — as in vanilla.
- **HUD**: the target overlay and the galaxy/dust camera copies derive from the
  sector camera after the site. The layer-0 cockpit-scene camera is built
  before the site from the previous frame's `+0xf0` (one frame late during
  motion, as in vanilla view transitions); the first run checks whether any
  external-view HUD element lives there.
- **Native Windows**: the hook, reads, writes and timing use documented Win32
  only (`VirtualProtect`, `FlushInstructionCache`, `VirtualQuery`,
  `QueryPerformanceCounter`); nothing depends on Wine.
- **Reset/device loss**: irrelevant, the state is engine-side; the hook is
  restored only when the last device goes.
- **Pause/menus**: the loop keeps running the cockpit update; the dt clamp
  bounds the first step after a long gap.

## Diagnostics

- Install: `chase_camera requested=1 installed=<0|1> status=<active|reason>
  site=0x00420e06 length=10 rel32_offset=6 atomic_write=0 arena_used=…
  rot_tau=… pos_tau=… offset_y=… distance_scale=… lag_clamp_deg=…
  pos_lag_clamp=… combat_tightness=… combat=inactive max_dt=…
  scope=external_back_view`.
- Per 300 frames and on capture frames (with `frame_end`): `chase_camera
  frame=… status=… frames=… applied=… refused=… snaps=… clamps=…
  write_refused=… verdict=<0 applied|1 internal|2 not back view|3 scripted
  connect|4 invalid input|5 degenerate|6 numeric|100 read failure>
  snap_reason=… mode=… connect=… lag_deg=… pos_lag=… distance=… dt_ms=…
  basis_dev_deg=…` (`basis_dev_deg` = angle between the derived ship basis and
  the node's `+0xc0` basis; ~0 in free flight, larger when docked/carried).
- Shutdown: `chase_camera_shutdown restored=<0|1> status=…`.

## Verification plan

1. Host (done): `python3 -m unittest verification.analysis.test_chase_camera
   verification.analysis.test_chase_camera_site` — closed-form step response
   against the analytic solution, frame-rate independence, clamps, snap
   reasons, pass-through verdicts, NaN/mirror guards, `camera = R_view × ship`
   identity, exp/log round trip and 16.16 conversion; the probe passes on the
   installed executable and fails closed on synthetic corruptions.
2. Build (done): `cmake --build build`, `verification/probe/check_no_x87.py
   build/d3d9.dll` PASS (the handler runs under the full boundary, so it is
   outside the light-hook rule); the default `--direct` run patches nothing new.
3. Wine fixture: not added — the engine-patch fixture chain (`loading_trace_fixture`)
   exercises `claim`/chain/restore on synthetic functions; a `rel32_offset`
   case there is cheap and is the first follow-up.
4. **First user run** (`launch --direct --camera chase`, then with the route):
   the install line `status=active`; in the external back view (F2 cycle)
   `chase_camera … verdict=0 applied` growing with `refused` flat, `lag_deg`
   non-zero while turning and `< lag_clamp_deg`, `distance` ≈ vanilla boom ×
   scale, `basis_dev_deg` ≈ 0; the HUD target brackets stay on their ships and
   the crosshair/mouse aim hits where the cursor points (fire at a static
   target while the camera is still lagging after a turn); a gate jump logs a
   snap (`snap_reason` 16, then 4) and the TAA frame line shows `camera_cut=1`
   once; internal (F1) and front/side views show `verdict=1/2` with the vanilla
   pose; SETA on/off leaves the feel unchanged (`dt_ms` tracks wall time);
   docking shows `verdict=3` and a snap on return.

## Open questions (defaults to choose)

- `rot_tau` 0.20 s / `pos_tau` 0.30 s / `lag_clamp_deg` 10 / `offset_y` 0.12 /
  `distance_scale` 1.0 are guesses within the requested ranges; tune on the
  first run.
- Boom lag semantics: ship-relative (constant velocity = no lag, implemented)
  versus world-frame (acceleration lag, big steady lag at X3 speeds relative to
  the boom); the former is the safer default.
- Combat tightness needs a readable "target locked/firing" state; candidates
  are the cockpit's target-overlay tracking state (`INS_CockpitIsTracking`,
  `+0x3ac` block) — not studied yet.
- Whether to also retarget the cockpit-scene camera (`+8`) at the site to
  remove its one-frame lag, if the first run shows an external-view HUD
  element rendered there.
