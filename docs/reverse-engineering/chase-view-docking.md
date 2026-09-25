# Chase view across docking and undocking

Study of 2026-09-25 for the user request "when you dock the chase camera is
reverted to first person; keep our chase camera if it was active before
docking". It extends [the sector-transit study](chase-view-transition.md)
(same view-mode writer, same script seam, same one-use ticket) to the docking
path. No game, Wine run, build or install was done for it. Source is unchanged.

Provenance: X3AP.exe SHA-256 `fdbf3418…f8ab` (the installed EXE, unchanged),
`addon/04.cat:L/x3story.obj` decoded SHA-256 `ed5786a0…faff7a` (the asset of
[selection-native-vm.md](selection-native-vm.md#recovering-actual-compiled-method-names)),
and the local, untracked Run315 log
`/tmp/x3-bottleX3-run315/session-20260925-004726-212.log` (133,088,955 bytes,
SHA-256 `d585cb83…ca81e0`), a user flight with `--chase-view-restore` and the
transition diagnostics that docks at a station in the rear chase view and
launches again. Script and CODE offsets below are hexadecimal without `0x`;
native addresses carry `0x`. "Measured" means the Run315 log or the bytes;
"static" means the script or EXE decode; "inferred" is marked.

## Result

- **Docking at a station resets the view, undocking does not touch it.** The
  reset is the same script assignment the gate uses: `RestartAllMonitors`
  calls `monitors[0].SelectMode(1)` at `edc8c`, and the optimized store of
  `SelectMode` at CODE `f0c4b` reaches the interpreter seam `0x004a3ffd`.
  Here the caller is `7e0::RunPlayerTrade`, run in a task forked by
  `7e0::StartPlayerTrade` from `7d4::CanLand` (measured and static).
- Launching from a station (`280::Undock` → `7e0::StartPlayerShip` →
  `7d4::__StartInHangar`) writes no view mode and does not recreate the
  cockpit. Run315 has no mode store, cockpit destruction or construction
  between the dock reset and the first key press, 13.785 s later, and the
  launch lies in that interval (measured).
- The engine keeps no saved pre-dock mode. Script monitor cell0 holds 258
  until the `SelectMode(1)` store overwrites it. The native cockpit is freed
  and rebuilt with mode 0 (static; measured in Run315).
- Therefore the only place where the engine itself builds a consistent
  rear view is the reset seam at dock time. The restore ticket consumed there
  (source payload 1 → 258, as for the gate) makes the engine build the new
  cockpit in mode 258 with its own rear geometry. The cost: the docked
  screen shows the engine's rear view of the parked ship instead of the
  cockpit. A restore that waits until launch, as the brief reads the
  request, has no engine seam to use. It would mean writing native view state
  from the proxy (see "Strict reading" below).

## 1. Code paths that change the view mode

The native mode writers are unchanged from the transit study:
- constructor `0x0041fbe8` (writes 0);
- deserializer `0x00419e06`;
- `INS_CockpitSetViewMode`, cockpit command `0x30` in dispatcher `0x0042d340`,
  case `0x0042e72b`, store `0x0042e742` `89 88 50 01 00 00`
  (EAX = cockpit, ECX = mode).

The script calls this command at three places: `StartMonitor` `f078f`
(re-applies cell0), `SelectMode` `f0c5e`, and `SwitchViewCameraTypeTo`
`f42ae`/`f4394`. Cell0 of the class-`25e` monitor is stored only at
`ef867` (Create), `ef944` (Destruct), `f0c4b` (SelectMode) and `f41dd`/`f42f7`
(SwitchViewCameraTypeTo) (static, `kc_dock_view.py`).

| Phase | Path (static) | View write | Value | Patched restore site? |
| --- | --- | --- | --- | --- |
| Landing permission | `96::DockingAllowed` `17202` → `SA_SetPlayerLand` `1727c` | none | – | – |
| Docking start: docking computer or scripted | `96::DockPlayerShipTo` `171bd` (callers `af573` MovePlayerTo, `124e9f` 8b7::MenuAction, `1af8d7`) → `96::LandPlayerShipAt` `17198` → `LOADG 9 .CanLand` `171b5` | none | – | – |
| Docking start: autopilot "dock at" | `7d4::__LandInHangar` `b7c25` flies in, then `LandPlayerShipAt` `b83c7` → `CanLand` | none | – | – |
| Docking start: manual flight | native `0x0045d8a0..0x0045d8c5`: `push 1; push 1; push "CanLand"` (`68 6c fd 55 00` at `0x0045d8b7`, string `0x0055fd6c`), ship script object `+0x94`, container `+8` as the argument, `call 0x0049f4c0` (by-name script call) → `CanLand` | none | – | – |
| Docking "cinematic" | none on this path. The cinematic-loop views (`StartMonitor` `f043d..f048a`: mode bits `0x400`/`0x800`/`0x2000` → connect 4/5/9, `INS_CockpitSetCinematicLoopTime`) are not reached from `CanLand`/`RunPlayerTrade`. Run315 connect requests (kind 8) are all 0 around the dock (measured: events 50, 55, 57) | – | – | – |
| Docked at a station | `CanLand` player branch `c7436..c755c` (`SE_IsClass(7e0, hangar)`): `SA_SetPlayerLand` clear `c74bc`, `SA_MoveObjectIntoContainer`, `SA_SetShipAtDockPort`, `StartPlayerTrade` `c7545`, `Signal_Docked` `c754f`. `StartPlayerTrade` `9bcc4 FORK` → child `RunPlayerTrade` `9bccb` (ret `9bcd0`). The `9bcec` FORK sends the speech block to a child. The parent unconditionally runs `KillMenus`, `StopFastForward`, `StopAllMonitors` `9be92` (ret `9be97`) → `StopMonitor` `edbde` → `25d::StopMonitor` → `Show` → `25e::StopMonitor` `efbfa` → `INS_CockpitFree` `f0085` → native `0x0042d402` → destructor `0x0041ffc0`. `StopAllMonitors` ends with `TI_Interrupt` `edc16` (yield). Next: `SetCockpitNumber` `9bea3`, `SetTracking` `9beb4`, `RestartAllMonitors` `9bebe` (ret `9bec3`) → `SelectMode` `edc8c` with literal source `PUSH1` `edc85` (bytes `02 02 01 0f 0002 10 85 000053ef`) | script cell0 at `f0c4b` (seam `0x004a3ffd`, `03 70 0c 80 3e 08`). `OpenLayout` → `StartMonitor` then allocates the cockpit (`f01c5`) and stores the mode at `f078f` → `0x0042e742` | 1 | **Yes, the same seam** (restore site kind 0). Today the ticket refuses at the transfer (below) |
| While docked | `RunPlayerTrade` loops `9bf56..9bfd0` (`TI_Delay 100` while `IsDocked(G9) == self`). Menus (`280::OpenDockedStation` `ffc0b`, `7e0::CurrentShipDockHere` `9cd9e`) call `Show` → `StartMonitor` → `f078f`, which re-applies cell0 | `0x0042e742` (re-apply) | cell0 | same writer, not a restore site |
| Undock or launch from a station | `280::Undock` `101818` `FORK` → `IsDocked(G9).StartPlayerShip` `101836` → `7e0::StartPlayerShip` `9c07d` → `ship.__StartInHangar(station)` `9c0dc` → `SA_StartInHangar` `b77fd`. The monitor restart at `b79a8`/`b79d4` runs only if `self == G9 && #4 && #3`, where body cell #3 = `SE_IsClass(0x7f1, hangar)` (`b75d5`; at depth 5 `LOADL 2`/`LOADL 3` at `b797c`/`b7990` read #4/#3). Class `7e0` (station) is not a `7f1`, so there is no restart. Afterwards `RunPlayerTrade` calls `StartPlayerShip` again (`9c074`), which is a no-op once undocked | none | – | – |
| Launch from a player carrier (inferred) | `__StartInHangar` with a `7f1`-derived hangar (`7f1 → 819 → 856 → 7d4`), reached from `__StartIntoSpace` `b7451` or `7f1::ChangePlayerShipTo` `d4ec3`: `StopAllMonitors` `b79a8` (ret `b79ad`) … `RestartAllMonitors` `b79d4` (ret `b79d9`) → `SelectMode(1)` | cell0 at `f0c4b`, then `f078f` | 1 | same seam, unmeasured prefix |
| Docking at a player carrier (inferred) | `CanLand` branch `c7306..c7431` (own ship with cockpit): `StopJumping`, `KillMenus`, `SA_SetPlayerLand`, `RequestDockPort`, `MoveTo` (returns at once for the player ship, `af29f..af2aa`) | none found | – | – |

### Run315 witness (measured, `run315_dock_events.py`)

| Event | Kind | State |
| --- | --- | --- |
| 51 | 6 (mode store) | 258 re-applied through `f0794`, task `6817b368` (the `RunPlayerTrade` speech fork, returns `…,b52b,9bd1c,0`). Geometry: offset `+0x160` (0,0,-86400), lock `+0x120` 1 |
| 52 | 2 (destructor, caller `0x0042d402`) | Generation 1, mode 258, connect 0, task `6817b278`, `next_pc=f008a`. Return chain `efbff,edba0,edbe3,9be97,9bcd0,0` (6 pairs, `origin_flags=0`), warp 0, killed 0 |
| 53/54 | 0/1 (construction) | Generation 2, caller `0x0042d397`, mode 0; 1.6–3.7 ms later |
| 56 | 6 | Requested 1, same task `6817b278`, `next_pc=f0794`, returns `efbbb,edb1b,e7b2d,edc98,9bec3,9bcd0` (flags 4: the root pair is beyond the six retained). Offset (0,0,0), lock 0. It follows the destructor by 4.49 ms |
| 57/58 | 8 / 3 | Connect 0; first generation-2 update 7644, mode 1 |
| – | `chase_view_restore_transfer_refused reason=18` | The installed ticket was armed at the dock and refused provenance, because the destructor chain has 6 pairs where the gate proof requires exactly 5 |
| 59–78 | 8/6 | From 13.785 s after event 56: player input (`96::Input` `14a73` → `96::Vbi` `13a34` → `25d::Vbi` `ee0c1` → `ChangeView2`) selects 1 three times, then 258 (event 76, `ChangeViewForMonitor` `ee89a`) |

Between events 56 and 61 there is no kind 0/1/2/6/7 event (measured). The
logged camera is static on frames 9090–9239 (docked) and moves from frame
9240 (launch); the log does not tie those frames to QPC exactly.
Cursor-window QPCs place event 59 near frame 9300 (inferred). Whether the
dock was manual, docking-computer or autopilot is not recorded. All three
reach `CanLand`, and the fork makes `StartPlayerTrade` the task root in every
case (static). The fork semantics are measured twice in Run315: each child's
root frame is the forking method with return 0 (`…,9bcd0,0` and
`…,9bd1c,0`), as for `WarpToSector` on the gate.

## 2. "Flying again" and the earliest safe restore point

With the dock-time consume below, the proxy needs no "flying again" signal.
The consume happens at the `SelectMode(1)` seam in the `RunPlayerTrade`
task, 4.49 ms after the destructor, before the new cockpit exists. The engine
then builds generation 2 in mode 258 and its own rear geometry
(`StartMonitor` external branch `f03fa..f05b1`: ref object, `MoveCockpit(1)`,
camera offset from `SA_GetTotalSize`, `LockView(1)`, connect 0). Nothing
overwrites it: the launch writes no mode (measured). Later `Show`
re-applications (`f078f`) read cell0 = 258, and the key path keeps working
because script and native agree.

Ticket extension, the same state machine:
- **Arm** as today, on an admitted rear update.
- **Transfer** at the destructor (`0x0041ffc0`, caller `0x0042d402`) on a
  second accepted chain, `efbff,edba0,edbe3,9be97,9bcd0,0`: exactly 6 pairs,
  `origin_flags == 0` (measured), **warp 0**, killed 0, player and
  controller unchanged, borrowed class-`25e` context. Record the path (gate
  or dock) in the pending state.
- **Consume** at `f0c4b` with the proof for that path: warp 0 instead of 1,
  live-stack prefix `edc91,9bec3,9bcd0,0`. This prefix is inferred: the
  measured event-56 chain passes through `edc98,9bec3,9bcd0` one frame
  deeper, and `SelectMode` returns to `edc91`. Every other consume check is
  unchanged: thread, task and task ID, epoch; opcode `94` index 0; class
  `25e`; monitor identity; cell0 258, cell1 0, cell16 0; cell17 tag and
  player; source tag 1 payload 1.

The source literal is the same `PUSH1` at `edc85`, because it is the same
`RestartAllMonitors` code.

A launch-time "flying again" signal exists only for the strict variant below.
Candidates: the chase camera's `base_domain` sample (FUN_0044fe20: ref
`+0x54` owner type 1, already read every frame in
`chase_camera_native.h`) turning true, or the script's `IsDocked(G9)`
becoming 0. Neither transition is logged in Run315 (unmeasured).

## 3. Saved pre-dock mode

The engine saves none. `StopAllMonitors`/`StopMonitor` free the native
cockpit and clear only cell1 (`f008b`). Cell0 keeps 258 until `f0c4b`.
`RunPlayerTrade` reads no `GetMode` (static; the 35 `GetMode` call sites are
in classes `96`, `1f5`, `1f6`, `1f7`, `25d`, `8a3`, `8b7` and `8d3`). The native cockpit is freed, and the
constructor initializes mode 0 (measured: event 53 mode 0). The smallest
patch is therefore the existing one-cell source substitution at `f0c4b`,
not a saved slot.

## 4. Interactions

- **Current behavior with `--chase-view-restore`**: fail-closed on docking.
  The transfer is refused with reason 18 and the arm is cleared (measured,
  Run315 `arms=1 transfers=0`).
- **Double arming**: one arm, one pending. The path is chosen only by the
  exact destructor chain, and the consume checks the prefix and warp value
  of that path, so a gate chain cannot consume on a dock seam or the reverse.
  A view key pressed while docked goes through `SelectMode` and cancels as
  today.
- **Jump while docked**: the jumpdrive path `7d4::JumpToSector` calls
  `__StartIntoSpace` at `bdd06`, which launches a docked ship first
  (static). A gate needs flight. A warp therefore cannot overlap the docked
  window (inferred). A dock pending cannot meet a warp chain, because both
  pendings live only between one task's `StopAllMonitors` and
  `RestartAllMonitors`.
- **Save or load while docked**: the save stores script cell0 and the native
  mode, which the deserializer (`0x00419e06`) restores. With the extension
  both are 258; without it both are 1. The load boundary `0x004a0880` and the
  VM clear and construct sites bump the epoch and cancel a pending ticket.
  The pending window is only the 4.49 ms `StopAllMonitors → RestartAllMonitors`
  gap, so a save cannot fall inside it in practice.
- **`X3M_CHASE_SCENE_FIX`**: no docking handling. It re-expresses the layer-0
  cockpit-scene camera on applied frames, so it would also act on docked
  frames once the chase camera admits them. The chase camera has no docking
  branch. A docked ship is in the render-ready position domain (`+0xb0`,
  `base_domain` false), which the camera already follows. At launch the
  domain flips to `+0x30`. A resulting position step larger than the snap
  ratio would trigger teleport snap 16 (inferred; flight check).
- **Carrier docking** (inferred): the view is not reset at docking but is
  reset at launch through `__StartInHangar`'s `RestartAllMonitors`. That
  launch-time reset fits the strict reading. Its destructor chain would be
  `efbff,edba0,edbe3,b79ad,<caller of __StartInHangar>,…`. It is unmeasured
  and may exceed the six retained pairs; until a witness exists it refuses
  (reason 18 or 19).

## 5. Patch plan

**No new native site.** The seven restore spans (`0x004a3ffd`, `0x004a2260`,
`0x004a2420`, `0x0049c9a0`, `0x0049ea80`, `0x004a0880`, `0x0052f298`) and
the destructor observer `0x0041ffc0` keep their bytes, stubs and verifiers.
The dock consume uses the same
6-byte seam (`ADD ESI,[EAX+c]; CMP byte [ESI],8`, flags live into the
following `jb`, all registers and flags restored before replay). It is the
same `[EBX+1]` payload write, in the same kind of task as the gate:
- the interpreter is reentrant and the task yields between transfer and
  consume, so pending stays keyed by thread, task pointer, task ID and epoch;
- the prefilter admits every store while pending, and the pending window
  lasts 4.49 ms here.

Changes belong in `src/proxy/chase_transition_restore_core.h` (a second
transfer row and consume prefix, a warp value per path, `pending_path`) and
in the counters and log of `chase_transition.cpp`. Record `path=gate|dock`
on `chase_view_restore_state` and on the seam line. The origin walker's six
pairs are exactly enough for the dock chain. `origin_flags=4` on the
destructor must refuse.

Option shape: `--chase-view-restore-dock` (`X3M_CHASE_VIEW_RESTORE_DOCK=1`).
It requires `--chase-view-restore` and is off by default. Without it the
dock row is not accepted, which keeps today's behavior. It stays separate
because it changes what the docked screen shows (the rear view of the parked
ship instead of the cockpit) and has not been flown. Fold it into
`--chase-view-restore` once the user accepts the docked look.

Fixture: the portable host fixture `verification/probe/chase_transition_restore_host.cpp`
via `verification/analysis/test_chase_transition.py`. Cases:
- the dock chain transfers only with warp 0;
- the gate chain transfers only with warp 1;
- 5 and 7 pairs, and `origin_flags=4`, refuse;
- the consume accepts the dock prefix only for a dock pending and the gate
  prefix only for a gate pending;
- a same-task selection while docked cancels;
- the option being off refuses the dock row.

`verify_chase_restore_sites.py` / `test_chase_restore_sites.py` are
unchanged. The Wine CPU fixture is not needed, because no emitted stub
changes.

Flight check, with `--camera chase --chase-view-restore
--chase-view-restore-dock --telemetry`. In the rear view, dock once with the
docking computer and once manually or by autopilot. Look at the docked
screen, then launch without pressing a view key. Expected rows:
- `chase_view_restore_state` shows transfers and consumed +1 per dock;
- one `chase_view_restore_seam` line with `prefix_ok=1 refusal=0`;
- a generation+1 `chase_transition_event kind=6 requested=258
  next_pc=0x000f0794` whose returns end `edc98,9bec3,9bcd0`;
- no kind 6 at launch;
- a `chase_camera` window with `verdict=0 mode=258` after launch.

After launch the internal-view key and back must still work. The user
reports whether the docked screen is acceptable.

### Strict reading: restore only after launch (not recommended now)

A station launch has no script selection and no cockpit restart. A
launch-time restore would therefore have to write, from the proxy:
- script cell0 = 258 (otherwise every later `Show` re-applies 1);
- `+0x150`;
- the rear geometry the engine's commands produce. `LockView` is case
  `0x0042e007` → `0x00422f40` or `+0x120 = 0`. `MoveCockpit` is case
  `0x0042d90c` → `0x00426ce0` on `cockpit+0x290`. Then the angles (`2b`),
  view position (`2f`), camera offset (`36`) and connect (`0x00422cd0`).

The chase camera needs the vanilla boom (`vanilla_pos - ship_pos`, back-view
test in `chase_camera_math.h`), so the mode alone would be refused as
degenerate. Two of those native calls are unstudied. Copying the geometry
from the arm snapshot would require that snapshot to cover `+0x120`,
`+0x130`, `+0x160`, `+0xa8..b0` and the `MoveCockpit` state. This is
iteration two of the original design note, with new invariants, and is not
needed for the dock-time plan.

## Unknowns

- The dock consume prefix `edc91,9bec3,9bcd0,0` is inferred. The first
  flight's seam line settles it, and a mismatch refuses without writing.
- What the engine renders while docked in mode 258: whether the parked ship
  is drawn and whether the chase camera admits those frames.
- The carrier-launch chain and whether it fits six pairs.
- Which initiator Run315's dock used.
- Native Windows behavior (unverified, as for the whole hook).

## Reproduce

```sh
python3 verification/results/chase-view-docking/kc_dock_view.py      # static script + EXE facts
python3 verification/results/chase-view-docking/run315_dock_events.py # needs the local run315 log
```

Both print derived offsets, names, return chains and bytes only.

## Dropped (2026-09-25)

`--chase-view-restore-dock` (40cd3023) was removed by user decision after
Run 86 A launch 3 (run335). The user's reason: after undocking, the station and
ship selection boxes (HUD brackets) disappeared, and a save loaded while docked
restores the first-person view anyway, so the feature is not worth it.

Run335 finding: the path worked as designed. The transfer logged `path=dock`,
was accepted and was consumed at the `f0c4b` seam with `cell0=258`; two seam
rows were logged. After the undock the HUD brackets vanished. The cause was not
investigated. The production code is back at the pre-40cd3023 state: the
docking destructor chain refuses with reason 18 and clears the arm, there is no
dock path, and the option and its environment variable are gone. The findings
in sections 1-5 remain valid reverse-engineering facts for any later attempt;
such an attempt must first explain the lost brackets.
