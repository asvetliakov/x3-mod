# Review 31a: chase camera architecture

Independent architecture review of the chase-camera prototype on the worktree
branch at `0296154` ("Chase camera: disassembly study, design, initial
implementation"). Read-only: no source edited, nothing installed, no game or
Wine process started. Inputs: [external-camera.md](../reverse-engineering/external-camera.md)
(the study), [chase-camera.md](../architecture/chase-camera.md) (the design),
[camera-state-and-frame-routine.md](../reverse-engineering/camera-state-and-frame-routine.md),
[temporal-integration.md](../architecture/temporal-integration.md) ("Camera
reprojection for sentinel pixels"), `src/proxy/engine_patch.{h,cpp}`,
`scene_hook.{h,cpp}`, `chase_camera.{h,cpp}`, `chase_camera_math.h`,
`loader.cpp`, `capture.cpp`, `motion_output.cpp`, [loading-probes.md](loading-probes.md).
Static cross-checks against the installed `X3AP.exe` (SHA-256 `fdbf3418…`)
used raw byte reads and the study's own decompiler output under
`/tmp/x3-camera-study/` (`f4205e0.lst`, the `dec:0042d340`, `dec:0041cde0`,
`dec:0041cd20`, `dec:00445170` sections of `run4/5/9/10.txt`); the numbers
and addresses are reported here, the listings stay untracked.
`verify_chase_camera_site.py --json` on the installed executable: `PASS`.
The implementation review is separate
([review-31-chase-camera-implementation.md](review-31-chase-camera-implementation.md)).

Everything below about runtime behaviour is static reasoning; the first
`--camera chase` run is the first observation.

## Findings

| # | Severity | Finding | Where | Recommendation |
| --- | --- | --- | --- | --- |
| A1 | **High** | The cockpit update runs **once per registered cockpit** per frame, and the engine has more than one cockpit: `FUN_0041cde0` walks the registry `*0x00608504` by handle and calls `FUN_004205e0` for every entry (`dec:0041cde0`: the `FUN_004efda0` next-handle loop); the dispatcher `0x0042d340` has `INS_CockpitSetMonitorNumber`/`GetMonitorNumber` (cases `0x1b`/`0x1c`, cockpit `+0x1dc`) and `INS_SetActiveControlCockpit` (case `0x48`, writes registry `+0x10`), and the fire control selects the cockpit by handle and gates on `cockpit+0x10 == this ship` (`dec:00445170`, the `FUN_0041cd20()` / `*(iVar7+0x10) == param_1` tests). A cockpit whose `+0xc` is 0 still reaches the site (`0x004207a3` `cmp [ebx+0xc],0` / `0x004207a7` `jz 0x00420e06`, bytes `83 7b 0c 00 0f 84 59 06 00 00`). The handler keeps **one** global `pipeline`, `qpc_last` and `stats_` (`chase_camera.cpp:35-40`): every other cockpit's visit either refuses (`ref_object == 0` → `note_gap` → reset, `:80,:94`) or snaps (ref-object change, bit 2, `chase_camera_math.h:245`), so the springs restart every frame, `dt` is split between cockpits (`:91-92`), and a monitor cockpit whose vanilla pose passes the back-view test would be smoothed too. | `chase_camera.cpp:71-148`, `chase_camera_math.h:241-251` | Key the state on the cockpit pointer (a small fixed table, or one slot plus "only this cockpit"): act only on the cockpit whose `+0x10` (ref view object) equals `+0xc` (ref object) — the player's own ship seen by its own view, the same predicate the fire control uses — or whose handle (`+0x00`) equals the active control cockpit at `*(*0x00608504+0x10)` once that field is confirmed; every other cockpit's visit must leave the state, `qpc_last` and the counters untouched (a separate `other_cockpits` counter). Log the number of distinct EBX values seen per frame on the first run. The site itself stays right (Q1); this is a keying gap inside it. |
| A2 | Medium | Connect mode `3` and flag `+0x1a0 & 4` make the engine write `camera.basis = +0xf0` **verbatim** (`0x00420c0c`; tests at `0x004209f7`, `0x00420aaf`, `0x00420bb1`) instead of `+0xf0 × B_ship`. The pipeline derives `B_ship = R_viewᵀ × B_v`, which is then the identity: the back-view test runs in the world frame and the boom spring treats world axes as ship axes. The handler reads `+0x1c0` but not `+0x1a0`, and passes connect mode 3 through as follow. | `chase_camera_math.h:229-239` | Pass through (`SpecialConnect`) for every connect mode other than 0 and whenever `+0x1a0 & 4` is set (the byte is inside the `0x200` block already read). |
| A3 | Medium | Hook lifetime across a device recreate: `chase_camera::shutdown()` restores the bytes when the last device is released (`capture.cpp:461`), `initialize()` runs once from `load_backend` (`loader.cpp:69`), and every later claim is `late_claim` (window closed at the first Present, `capture.cpp:518`). A resolution/mode change that destroys and recreates the device leaves the vanilla camera for the rest of the session, silently except for the `chase_camera_shutdown` line. The scene hook reinstalls on a later device; the engine-patch sites cannot. | `capture.cpp:455-462`, `engine_patch.cpp:92` | Do not restore at last-device release: keep the site for the process lifetime like the loading probes (the arena is never freed anyway; the bytes are restored at process exit or never). If a restore is wanted, log `status=restored_no_reinstall` so the first run can tell. |
| A4 | Low | A gate jump produces two snaps two frames apart: the displacement snap (bit 16) on the frame the ship node moves, then the sector snap (bit 4) when `+0x1fc` follows one frame later (study §3 step 7). Each is a TAA cut (`motion_output.cpp:701-703`), so two current-only frames. | `chase_camera_math.h:244-251` | Coalesce: a snap within a few frames of the previous snap resets the springs but does not raise `snap_pending` again, or suppress bit 4 when bit 16 fired on the previous applied frame. |
| A5 | Low | The layer-0 cockpit-scene camera (`cockpit+8`) is built at `0x00420787` from the **previous** frame's `+0xf0`. In vanilla `+0xf0` changes only during view transitions; with the chase camera it changes every frame the ship turns, so anything rendered in that scene during an external view follows one frame late (per-frame change ≈ the change of the lag angle, tenths of a degree at most). | study §4, design "HUD" | Keep the runtime check. If an element shows, the fix fits the same site without touching stack temporaries: the handler already holds the previous applied `view_rel'` (what step 4 consumed), so `basis(+8)' = R_view' × R_view_prevᵀ × basis(+8)`. |
| A6 | Low | View transitions animate `+0x130`/`+0x90..` over `+0x1b8` ms (`0x004218b0`); mid-animation the geometric back-view test (`|x| ≤ 0.6|z|`, `R_view[2][2] > 0.7`) can flip, each flip refuses (`NotBackView` → `reset`), and the next passing frame snaps: a pop at the end of a front→back transition. | `chase_camera_math.h:209-215,223` | Hysteresis on the back-view predicate (enter at 0.7, leave at 0.5) and keep the springs alive through up to N consecutive `NotBackView` frames while the view mode is unchanged. |
| A7 | Low | Defaults: a critically damped spring lags a constant angular rate Ω by `2τΩ`; at `rot_tau` 0.20 s the 10° clamp saturates for every Ω > 25°/s, i.e. in every fighter turn, and the settle after the turn is ~4.7τ ≈ 0.94 s. Combined worst-case ship excursion is `10° + atan(0.20)` ≈ 21° (design §"Pose pipeline" step 9), a third of the 74° vertical FOV. | `chase_camera_math.h:150-161` | Start the first run at `rot_tau` 0.12–0.15 s, `lag_clamp_deg` 8, `pos_tau` 0.20 s, `pos_lag_clamp` 0.10 (excursion ≤ 14°); keep `offset_y` 0.12 and `distance_scale` 1.0. See Q6. |
| A8 | Info | Pause/menu behaviour is unverified statically: `0x00403f2a/2f/34` are unconditional in the shown range, but whether the loop takes that path while a menu is open or the game is paused is not established. Both outcomes are safe (Q3): the springs either settle against a static target or take one `max_dt` step after the gap. | design "Pause/menus" | First-run check: open the sidebar while the camera is lagging; expect a settle, no jump on close. |
| A9 | Info | Readable combat state exists after all: cockpit `+0x1e4` (short; `INS_CockpitIsTracking` = `== 1`, `IsEnemyTracking` = `== 4`, cases `0x24`/`0x25`), `+0x1e0` (tracked object; `INS_CockpitGetTracking` returns `*(+0x1e0)+8`; the fire control takes `param_2 = *(cockpit+0x1e0)` as the aim target when `+0x1d8 ≥ 0`), the weapon record's next-fire game-ms (`*(weapon+8) = +0x718 + interval` at fire time, `dec:00445170` lines around the `param_4 & 2` branches), and the fire input bit itself (`param_4 & 2`, a call argument of `0x00445170`, reachable through a byte-verified entry trampoline of the loading-probe kind). | `dec:0042d340` cases `0x22-0x27`, `dec:00445170` | Follow-up study (Q6): `dec:00425a10` (the tracking setter), `load:0x1e0`, `load:0x1e4`, `load:0x1d8`, `dec:00450e70`, `dec:00450f50`, and the ship → weapon-record chain for the next-fire stamps. |
| A10 | Info | Per-frame cost on the game thread: one `QueryPerformanceCounter`, `engine_memory` validation (one `VirtualQuery` per distinct region per frame: cockpit, camera, node, ref object, the globals slot ≈ 4–5, without the route's `next_frame` the ~100 ms tick bound applies), a cached `VirtualQuery` pair for the writes, `FNSAVE/FRSTOR` of the full boundary. Microseconds; acceptable, but unmeasured. | `chase_camera.cpp:78-92,121-122,156-158` | Add the handler to the telemetry spans on the first run so the number is on record. |

No finding says the integration point is wrong. A1 is the one item that would make the first run look "not smoothing at all" or "snapping every frame" and should be fixed (or at least instrumented with a per-frame cockpit count) before the run.

## Q1. Integration point `0x00420e06` — sound

The site is the first instruction after the sector camera's pose is final
(the `+0x160` offset ends at `0x00420e03`) and before any consumer of it in
the same loop iteration. The consumers, all after the site, are: the object
loop `0x00420e10..0x00421013` (camera-relative node `+0xf0..+0xf8` for
sound/radar/overlay), the target overlay `0x0042a2d0(cockpit+0x3ac)` (reads
`cockpit+0x58`, projects through `0x00489e90`), the galaxy camera copy of
`+0x58`'s pose (`0x00421533..`) and the dust camera (`0x0041efc0`), the
motion-blur/shake vectors (`+0x100`, `+0x350`, from the previous position
`+0x310`: `0x0042141c`), then the frame routine `0x00471f50` at `0x00403f34`
(bytes at `0x00403f2a`: `e8 21 28 01 00` `call 0x00416750`, `e8 ac 8e 01 00`
`call 0x0041cde0`, `e8 17 e0 06 00` `call 0x00471f50` — confirmed from the
image), whose view activation `0x0047c84d` → `0x004be520` builds
`*0x00608a40` from camera `+0x30/+0x40/+0x50/+0x60`. Nothing between
`0x00420e03` and `0x00420e06` reads the camera, and the study's check that
nothing after the site writes `+0x30..+0x6f` holds in the listing
(`f4205e0.lst`: the only later `+0x310` reference is the motion-blur delta).

Readers **before** the site in the same iteration: the fire control
`0x00445170` (inside the object update `0x00416750`, before the cockpit
update) reads the sector camera's FOV/viewport and `+0xf0` — the values the
previous iteration rendered with, which after our write are the smoothed
ones; that is the vanilla one-frame structure and is consistent. The
cockpit-scene camera build at `0x00420787` reads the previous `+0xf0` (A5).
No culling or LOD reads the camera before the site: the view frustum is
per-view state built in `0x004be520`/`0x0047c640` after the pose.

Alternatives, all worse:

- **`0x004be520` (view-matrix builder)**: runs per view (3+ per frame) after
  the object loop, the overlay projection and the HUD camera copies, so the
  brackets, radar and dust would use the vanilla pose while the scene used
  the smoothed one; it receives any camera, so selecting the sector camera
  needs the cockpit anyway; and `+0xf0` would still have to be rewritten
  for the aim ray. Also misses `0x004be670` (env-map faces) by design.
- **Cockpit-update entry `0x004205e0`**: a pre-hook can only change the
  inputs (`+0x130`, `+0x90..`), which `0x004218b0` re-interpolates every
  frame toward script targets (a fight), and would have to re-derive the
  pose; a return hook is after the overlay and the copies.
- **Proxy-side view-matrix override** (write `*0x00608a40` at the per-view
  Clear or via the `0x0047c84d` callsite): scene-only; every HUD/aim/sound
  consumer stays on the vanilla pose, off by up to the lag clamp.

The site's mechanics are as documented: ten bytes, two whole instructions,
one `jz rel32` re-based by the tail (`engine_patch.cpp:104-110`), the three
inbound branches land on `0x00420e06` itself (probe `interior_branches`
PASS), EBX live and forwarded through the `pushad` block (`regs[4]`,
`chase_camera.cpp:72`; `pushad` order EDI ESI EBP ESP EBX EDX ECX EAX is
right). The five-byte jump straddles the qword at `0x00420e08` → plain copy;
safe because the claim runs on the thread that calls `Direct3DCreate9`
inside `load_backend` (`loader.cpp:69`), i.e. the main thread that also runs
the cockpit update, which therefore cannot be executing the span.

## Q2. Aim consistency — sound, two pass-through gaps (A2)

`dec:00445170` confirms the study: the mouse-aim branch is gated on
`param_4 & 2`, `param_4 & 0x20`, `*0x00607ce8 != 0`, the cockpit found by
handle (`FUN_0041cd20`) with `*(cockpit+0x10) == this ship` and
`+0x1d8 ≥ 0`; the ray is unprojected through the sector camera
(`0x00445b3d` `mov esi,[edi+0x58]`; `0x00445b4d` `call 0x00489780`) and for
gun 0 rotated by `lea ecx,[edi+0xf0]` / `call 0x004f0da0` (`0x00445b58/63`),
then mounts and the ship basis. The camera basis is never read. Writing
`+0xf0' = B_c × B_shipᵀ` beside `camera.basis = B_c` keeps
`B_cam = R_view × B_ship` exact for the smoothed camera
(`chase_camera_math.h:284`), with `B_ship` derived from the engine's own
identity (`R_viewᵀ × B_v`, `:233`), which is right also when docked (the
parent basis is what the engine multiplied by). The overlay pick
`INS_CockpitGetCursorAim` (`0x00425410`, used at the same branch when
`+0x1e0` is empty) works on icons projected after the site — consistent.

Other consumers of the two fields:

- `+0xf0`: recomputed from the view angles every frame at `0x00420a27`
  before our write, so our value never feeds the next frame's computation;
  read only by step 4 (A5), the fire control, and connect mode 3 /
  `+0x1a0 & 4` as the verbatim basis (A2). No script command returns
  `+0xf0` (the `INS_CockpitGetView*` cases return `+0x90..`).
- `+0x130` (the boom) is **not written** (the handler writes camera
  `+0x30`, `chase_camera.cpp:123`), so the transition interpolator
  `0x004218b0`, `INS_CockpitSetViewPos` and the docking/cinematic modes are
  never fought. Right choice.
- The FOV/zoom writes (`0x00421390..`) and the shake angles (`+0x2ac`)
  are untouched.

Transitions: a mode change (`+0x150`, `+0x1c0`) snaps (bit 8); the
animated part of a transition moves the vanilla target smoothly and the
spring follows, except for the predicate flip of A6. The scripted modes
4/5/6/8/9 pass through and snap on return. The "target view" (a monitor
cockpit following another object) is exactly the A1 case.

## Q3. Spring/clamp model — sound; snap list complete enough; pause safe

- **Rotation-vector critically damped spring, closed form**: exact for a
  fixed target, unconditionally stable for any `dt`, no gimbal or up-vector
  assumption, so full roll follow with mouse steering is handled (the log
  at π is covered, `chase_camera_math.h:86-94`). The per-frame re-measure
  against the new target with the velocity carried over is the standard
  moving-target form; its ramp lag is `2τΩ`, hence A7.
- **Size range**: both springs are size-invariant — the rotation spring is
  in angle space and the boom clamp is a fraction of the boom
  (`pos_lag_clamp × |o_t|`), which the scripts already size per class.
  What differs by class is Ω: capitals (1–5°/s) get 0.4–2° of lag, fighters
  saturate the clamp. The time constants therefore behave as "settle time
  after a manoeuvre" for capitals and "rigid offset + settle" for fighters,
  which is a reasonable single-parameter feel; per-class `τ` is not needed.
- **Ship-relative boom lag** (constant velocity → no lag): right for X3.
  A world-frame spring lags a steady velocity by `2τv`, at `pos_tau` 0.3 s
  and typical fighter speeds that is 0.6 s of travel, larger than a
  fighter's boom, so the camera would sit inside or in front of the ship.
  Acceleration feel, if ever wanted, should be a separate small clamped term.
- **Snap conditions**: first applied frame / any gap (1), ref object (2:
  ship change, eject, respawn, load game), sector (4), view or connect mode
  (8: view switch, docking sequences, cutscenes on return), displacement
  `> 20 × boom` per frame (16: gate jump, jumpdrive, load into the same
  sector). SETA needs none (wall-clock `dt`); pause/menu need none (below);
  death/respawn changes `+0xc`. Complete for the listed events; A4 notes
  the double snap on gate jumps.
- **Wall-clock `dt` in pause/menus**: if the cockpit update keeps running
  (design's assumption), the vanilla target is static and the springs
  settle within ~1 s; the view behind the menu moves slightly, then stands
  still, and nothing jumps on close. If it does not run, the first frame
  after the gap takes one clamped `max_dt` (0.1 s) step: no jump either.
  Verified as A8 on the first run. Using the engine's game clock instead
  would speed the camera up under SETA, which is what the design avoids.

## Q4. Failure modes — sound, with A3 and the keying of A1

- **Bytes/identity/window**: `object_trace::executable_verified` first,
  then `claim` compares all ten bytes (`engine_patch.cpp:95`,
  `bytes_mismatch`), `late_claim` after the first Present; any refusal logs
  and leaves vanilla (`chase_camera.cpp:212-221`). Same discipline as the
  twelve loading-probe sites ([loading-probes.md](loading-probes.md), "Byte
  verification").
- **Pointer validity**: EBX is the engine's live cockpit by construction of
  the call context; every dereference goes through `engine_memory::read`
  (committed, readable, non-guard spans; 4-alignment checked), writes only
  to the two objects just read after a `VirtualQuery` writability check.
  A NaN cannot reach the engine: inputs are finite- and
  orthonormality-checked, the state is finite-checked after the step
  (`chase_camera_math.h:281`), `to_fixed`/`to_int` reject NaN and range
  overflow (a NaN fails the range comparison), and what is written is
  int32 — so the view matrix the TAA reads is finite by construction. MXCSR
  and x87 state are restored by the full boundary.
- **Reset/device loss**: the state is engine-side, unaffected; A3 covers
  the last-device restore.
- **Thread**: the cockpit update and the frame routine are consecutive
  calls of the main loop (`0x00403f2f`, `0x00403f34`), so the handler runs
  on the render thread; it takes only `stats_lock` (SRW) and no capture
  mutex, `report()` runs from Present under a shared lock — no inversion.
- **Other hooks**: the object-trace callsite `0x004c5228`
  (`0x004c4fc0`), the scene hook `0x004721b1` (frame routine) and the
  loading-probe/resource-reader sites (`0x004e8780`, `0x004e9360`, …) lie
  outside `0x004205e0..0x004216dc`: no span overlap, no chain sharing. The
  chase handler precedes the scene-end signal within each iteration, so a
  snap raised here is consumed by the same frame's resolve. The shared
  8 KB arena fails closed (`arena_full`); the install line's `arena_used`
  tells.
- **Native Windows**: `VirtualQuery`, `VirtualProtect`,
  `FlushInstructionCache`, `ReadProcessMemory`, `QueryPerformanceCounter`,
  `InterlockedCompareExchange64`; `force_align_arg_pointer` for the 4-byte
  incoming stack; `movups` for the XMM saves on an unaligned stack. Nothing
  Wine-specific. Unverified on Windows, like the rest of the patch stack.

## Q5. TAA interaction — sound

The route's camera read takes `*0x00608a40` at the scene view's per-view
Clear (temporal-integration.md, "Camera read"), and `0x004be520` builds it
from camera `+0x30/+0x40..` inside the frame routine that follows the
cockpit update in the same iteration — the smoothed pose. Routed draws get
their motion vectors from the same view, so the reprojection is against the
camera the player sees. A snap is at most `lag_clamp_deg + atan(offset_y ×
tan(half vfov))` ≈ 15° of rotation and can move the world without rotating
(gate jump), so the 20° rotation detector would miss it; the explicit
`take_snap()` → `in.cut` / `camera_cut` path (`motion_output.cpp:698-703`)
is necessary and correct. If a frame does not resolve, the pending flag
survives to the next resolve, which is what the stale history needs.

Jitter: the output is a deterministic function of the vanilla pose and
`dt`. Variation in `dt` (say 1 ms at 60 fps) changes the per-frame increment
by ≈ `|x|·ω·Δdt` — at the 10° clamp ≈ 0.05°, ≈ 0.6 px at 1280 px — but that
is a variation of the camera's angular velocity, which the TAA reprojects
exactly every frame; it is not noise around a still pose. At rest the
spring converges monotonically, `log_rotation` returns zero below 1e-9 rad,
and the written 16.16 ints become constant, so the history converges with
no per-frame flicker. The 16.16 quantisation (≤ 7.6e-6 rad per element,
≈ 0.01 px) and the state kept in doubles (no rounding feedback: the vanilla
pose is recomputed from the view angles every frame, `0x00420a27`) keep
the reprojection well below the resolve's tolerance; the rounded basis's
orthonormality error (~1e-5) is under the camera-state gate (1e-3). The
clamp's velocity removal is a kink in angular velocity, not in pose — no
cut.

## Q6. Defaults, boom semantics, combat state

- **`rot_tau` 0.20 s / `lag_clamp_deg` 10°**: with `2τΩ` the clamp is
  reached at 25°/s, so in practice every fighter turn is a rigid 10° offset
  followed by a ~0.9 s settle; that reads as "the camera is slow", not as
  a spring. Comparable chase views in arcade-leaning space games keep the
  follow tighter (on the order of 0.1–0.15 s) with a smaller visible window
  (5–8°) and a fast catch-up; the more cinematic "external camera" style
  (a free vanity camera with heavy smoothing, as in Elite's external view)
  is not what mouse-steered flight wants because the nose leaves the
  crosshair region. Recommendation: `rot_tau` 0.12–0.15 s, `lag_clamp_deg`
  8; then a 60°/s fighter turn shows 8° (still clamped) but settles in
  ~0.6 s, and a 5°/s capital turn shows 1.2–1.5°.
- **`pos_tau` 0.30 s / `pos_lag_clamp` 0.20**: the boom swing adds up to
  `atan(0.2)` ≈ 11° to the excursion in the same direction as the rotation
  lag (both put the ship on the outside of the turn), 21° combined.
  Recommend 0.20 s and 0.10 (≈ 5.7°), so the ship stays within ~14° of
  centre at the clamp, and consider clamping the boom lag by screen angle
  rather than by boom fraction if the first run shows the two lags stacking.
- **`offset_y` 0.12**: the ship at 12 % of the half height below centre is
  in the usual range (roughly a tenth to a sixth); keep. Note that
  `X3M_CAMERA_OFFSET_Y` pitches the camera rather than raising the boom,
  so the aim ray stays on the crosshair — correct.
- **`distance_scale` 1.0**: right as the starting point since the scripts
  size the boom per class; 1.1–1.2 is a plausible fighter tweak later.
- **Ship-relative vs world-frame boom lag**: ship-relative (see Q3). A
  world-frame acceleration term (lag ∝ `a·τ²`, clamped to a few percent of
  the boom) can be added later for a "thrust" feel without the velocity
  problem.
- **Combat tightness state** (A9): the study's "no readable state" holds
  only for the fire-control flags. Readable candidates, in order of
  preference: (1) cockpit `+0x1e4` tracking mode and `+0x1e0` tracked
  object (the same fields the aim ray uses: a locked target is the natural
  "combat" predicate), (2) the weapon record's next-fire game-ms stamp
  (fired within the last 1–2 s), (3) `param_4 & 2` at the entry of
  `0x00445170` through a byte-verified trampoline (fire button held). The
  target-overlay block `+0x3ac` is a fallback. Study spec as in A9; the
  tightness then scales `rot_tau`/clamp toward tighter values.

## Recommendations (ordered)

1. Fix A1 before the first run: per-cockpit keying or the `+0x10 == +0xc`
   selector, other cockpits leave the state alone; add a per-frame distinct
   cockpit count to the `chase_camera` line.
2. A2: pass through connect modes ≠ 0 and `+0x1a0 & 4`.
3. A3: keep the site for the process lifetime (or log the non-reinstall).
4. A7: run with `rot_tau` 0.15, `lag_clamp_deg` 8, `pos_tau` 0.20,
   `pos_lag_clamp` 0.10 as the first trial, defaults unchanged until judged.
5. A4/A6 after the first run if observed; A5 only if an external-view
   element renders in the cockpit scene.
6. A9 as the next study; A10 as a telemetry span.

## Acceptance criteria for the first user run

1. Install line: `status=active atomic_write=0`, no `bytes_mismatch`, the
   tunables echoed; the default `--direct` run has no `chase_camera` lines.
2. Back view (F2 cycle), 300-frame lines: `verdict=0`, `applied` growing,
   `refused` flat (a `refused` counter growing in step with `applied` is A1),
   the new distinct-cockpit count stable; `write_refused=0`.
3. `lag_deg` > 0 while turning and ≤ the clamp, back under 0.1° within
   ~1 s after the turn stops; `dt_ms` ≈ the frame time and unchanged with
   SETA on; `basis_dev_deg` ≈ 0 in free flight.
4. HUD target brackets stay on their ships during a turn; the cursor aim
   hits a static target fired at while the camera is still lagging.
5. Gate jump: `snap_reason` 16 then 4, `camera_cut=1` on the corresponding
   TAA frame lines, no ghosting afterwards.
6. Internal view `verdict=1`, front/side `verdict=2` with the vanilla pose;
   docking `verdict=3` and a snap on undock; a cutscene passes through.
7. Menu open while lagging: the view settles, nothing jumps on close (A8).
8. TAA at rest in the back view: no shimmer; the ship sits ~12 % of the
   half height below centre.
9. Change the resolution in the options and return to the back view:
   report whether the camera is still chase (A3).
10. Frame time unchanged within noise; the handler's telemetry span (if
    added) in the microsecond range.
