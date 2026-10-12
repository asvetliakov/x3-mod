# Ship navigation AI: where movement goals come from, and how collisions are avoided today

2026-10-12. Static study of X3AP.exe, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` (2,153,984 B, the native
`C:\X3` copy, re-hashed this session), and of the compiled KC script object `L/x3story.obj` in two
versions: stock AP `addon/04.cat` (decoded SHA-256 `ed5786a0…faff7a`) and the installed Mayhem 4
override `addon/07.cat` (decoded SHA-256 `2c3101a8c04ddea76643099758bfdaefd18d79cdf37580095a12a8ce72199298`,
2,182,503 B; the newest addon catalogue that carries the member). Nothing was launched. Structure and
semantics come from Ghidra 12.1.4 headless (project `~/x3-ghidra-research`, `X3Render`, `-readOnly
-noanalysis`) and capstone; every EXE byte string quoted here is re-read through the PE section map by
[`check_sites.py`](../../verification/results/ship-navigation-ai/check_sites.py) (138 checks,
`RESULT PASS`, output beside it). KC facts come from the loader contract of
[selection-native-vm.md](selection-native-vm.md) (STRG decode, CLAS rows, instruction widths of
`0x0049e1a0`) through
[`kc_movement_calls.py`](../../verification/results/ship-navigation-ai/kc_movement_calls.py) and
[`kc_method_flow.py`](../../verification/results/ship-navigation-ai/kc_method_flow.py). Raw decompiler
output and extracted script bytes stayed in `~/x3-nav-study/ai/` (untracked). **[m]** = read from bytes,
**[i]** = inferred. Distances are native integer units, **500 units = 1 m**
([camera-state-and-frame-routine.md](camera-state-and-frame-routine.md), the HUD formats `d/500`).

This is the second of three parallel studies. The per-frame integrator and the steering leaves are in
`ship-movement.md` (the integrator study); this note stops where the goal is written into the ship's
flight block and where the order interpreter hands a target point to the steering leaves.

**Question.** Where do a ship's movement goals come from (AI jobs, player autopilot, follow / escort /
formation, docking, gates), how does the game avoid collisions today, and which boundary should a
replacement steering law and a predictive avoidance layer intercept?

**Answer.**

* **Goal representation [m].** Movement goals are *orders*: the `SA_` native command module writes an
  order code (word `flight+0x258`, `flight = [obj+0x50]` for class-7 ships) plus its operands into the
  flight block — a target **point** `flight+0x240/+0x244/+0x248` (integer world units, or a slot offset in
  the leader's frame for escort), a target **object** `flight+0x224`, an arrival radius / distance
  `flight+0x260`, timing `flight+0x210` / `+0x290`, and the waiting script task in `[obj+0x7c]`/`[obj+0x80]`.
  There is no waypoint list anywhere in the engine. Desired speed is a separate scalar, `[obj+0x14]`.
* **Order interpreter [m].** Every frame, `0x00452ad0` (sector_simulate) switches on the order code at
  `0x00453ce0` and, at a re-plan time `flight+0x210`, turns the order into a steering target **point**
  that it hands to the steering leaves through `0x0044d020` (stdcall `(obj, int32 point[3])`) or
  `0x0044ccc0` (`EDX` = point, `[esp+4]` = obj), and a desired speed in `[obj+0x14]`.
* **Collision avoidance today [m].** It is reactive and runs on two rates. Each frame the sector collision pass flags a
  ship whose predicted path (braking distance + margin, cone test against the inflated obstacle sphere)
  meets another object (`0x0044e450` → `0x0044dd90`, flag `[obj+0x40] |= 0x1000`, obstacle list
  `flight+0x1e0..`) and calls the KC handler `CollisionWarn` synchronously. For AI ships that handler
  stops the order (`SA_StopObject(ship, 2)`). The waiting KC movement method then issues the native
  avoid order `SA_AvoidObjectCollision` (order `0x12`). That order samples escape directions every
  200 ms until the list is clear. The KC method then re-issues the original goal.
* **Best single interception boundary [m]+[i].** The steering-target hand-off is the boundary to intercept:
  the entries of `0x0044ccc0` (5-byte prologue `83 ec 18 8b 02`) and its wrapper `0x0044d020`. That is
  "goal point in, adjusted goal point out" for every point-steering order, at the order layer's re-plan
  rate, with no script re-entry (§6).

## 1. The `SA_` command module and the goal writes (Q1)

### 1.1 Registration and dispatch [m]

The movement commands are not in `INS_`/`B3D_`/`MOV_`/`P_`. They are in the **`SA_`** module (378
names, ids `0..0x179`). The module is registered inside the init function containing `0x004344b9`. That
code uses the same 0x18-byte record layout as the `INS_` registration `0x0041c8f0`:

| Record field | `SA_` value | Site |
| --- | --- | --- |
| `+0x30` dispatcher | `0x00460630` | `0x004344c7` `c7 04 c8 30 06 46 00` |
| `+0x34` completion callback | `0x004604d0` | `0x004344d4` `c7 44 d0 34 d0 04 46 00` |
| `+0x40` name table | `0x0057a7f0` (flat `char*[378]`, index = command id) | `0x004344f6` `c7 44 c8 40 f0 a7 57 00` |

The dispatcher `0x00460630` (35,513 B) has the shared native ABI of
[script-task-scheduler.md](script-task-scheduler.md) §3: `cdecl (slot, task, u16 cmd, argc, cell* args)`,
5-byte cells, argument *k* value at `args + 1 + 5k`. It bounds-checks at `0x0046065a` (`cmp ebx, 0x179`) and jumps
through `[cmd*4 + 0x004690f0]` at `0x0046067a`. The first argument is stored as the waiting slot in
`[obj+0x7c]`; the task id `[task+8]` goes to `[obj+0x80]`.

**Blocking protocol [m]+[i].** The order-setting cases leave through `0x00462e00`. That exit returns 1
without pushing a result cell (the value path is `0x00462df9` → `0x004a47f0`). The calling task therefore
waits **[i]**. When the order ends, the interpreter or `SA_StopObject` calls `0x00460560` with
`ESI = obj` and one stack argument, the result. That function calls the module callback
`[*0x006085e4 + 0x34 + slot*0x18](task_id, result)` and clears `[obj+0x7c]`/`[obj+0x80]`. For `SA_` the
callback is `0x004604d0`. It looks the task id up in the hash at `[[0x0060850c]+0x14]`, sets
`task+0x84 = 1` (`0x00460510`) and stores the result as an int cell at `task+0x8c`. The path that puts the
task back on the runnable list was not traced (open in the scheduler note as well).
Observed result values: **1** = arrived (e.g. `0x00456587 push 1` for MovePos), **3** on the
container-lost exits of orders 4/5 (`0x004549ab`, `0x00455115`; "target lost" **[i]**), and the caller-supplied value for `SA_StopObject` (KC passes **2**,
"interrupted").

### 1.2 Movement commands, their case addresses and goal writes [m]

`flight = [obj+0x50]`. All order-setting cases first resolve the ship through `0x0043a4f0` and require
class 7. Order codes are the values written to the word at `flight+0x258`.

| Id | Command | Case | Order | Writes (besides `flight+0x258`, `[obj+0x7c]`, `[obj+0x80]`) |
| --- | --- | --- | --- | --- |
| 0x1e | `SA_MovePos(ship,x,y,z,prec)` | `0x0046232c` | **0x19** | point `+0x240/4/8` = x,y,z; `+0x260` = precision (arrival radius) |
| 0x1f | `SA_MovePosContinue` | `0x0046239c` | **0x23** | as MovePos; arrival does not stop the ship (speed kept when aligned) |
| 0x14 | `SA_EscortObject(ship,leader,dx,dy,dz,prec,t)` | `0x004618a6` | **0x14** | `+0x224` = leader; `+0x240..` = **slot offset in the leader's frame**; `+0x260` = precision; `+0x28c/+0x290` timeout |
| 0x1b | `SA_FollowObject(ship,target,dist,t)` | `0x0046196f` | **0x15** | `+0x224` = target; `+0x260` = distance; timeout |
| 0x1c | `SA_FollowObjectByCut` | `0x00461a13` | 0x16 | `+0x224`, `+0x260`, `+0x264` (cut path); `+0x210` = now + rand(1000) |
| 0x11 / 0x12 | `SA_AttackObject` / `…Big` | `0x004614da` / `0x004616e0` | **0x10** / **0x11** | `+0x224` = target; `+0x240/+0x244` = ring offset, radius `min(r_ship + r_target, 100000)` at a random angle (`0x004ee190`), `+0x248 = 0`; `+0x288` next manoeuvre (now + arg or 50 ms); optional timeout |
| 0x13 | `SA_DefensiveMove` | `0x004617cf` | 0x22 | `+0x260` = arg; `+0x224` = attacker; `+0x290` = now + (arg or 300000 ms) |
| 0x0a | `SA_FlyObjectBeforeContainer(ship,cont,port,path)` | `0x00460cae` | **4** | `+0x224` = container; `+0x240..` = container position, or the dock-port approach point (`0x00451f30`) when `port >= 0`; `+0x250/+0x254` = port, path |
| 0x0c | `SA_FlyObjectBeforeContainerPos(ship,cont,x,y,z)` | `0x0046533c` | 4 | explicit point `+0x240..`; `+0x224` = container |
| 0x09 | `SA_FlyObjectIntoContainer(ship,cont,port,path)` | `0x0046082e` | **5** | `+0x224`; final point `+0x240..` and rendezvous `+0x230..` (dock port path `0x00451f80`, or a gate-axis point for class `0x12` gates); `+0x250/+0x254` |
| 0x0d | `SA_ClearObjectFromHangar(ship,cont,port)` | `0x0046117e` | 0x1e (undock) | `+0x224`, `+0x230` = current position, `+0x250`; exit point via `0x00451f30` |
| 0x0e | `SA_WarpObjectAway(ship,gate)` | `0x0046126a` | 8 | `+0x214` = now + 900 ms; `+0x240` = current position; gate flags |
| 0x21 | `SA_WarpObjectIn(ship,gate)` | `0x00465b22` | 0x21 | **writes the position directly** (`phys+0x30..`) to the gate exit; `+0x214`, `+0x218` |
| 0x0f | `SA_AvoidObjectCollision(ship,radius,rand)` | `0x0046104c` | **0x12** | `+0x230..` = start position; `+0x260` = radius, default `[obj+0xa4] + 150000` (`0x00461106` `05 f0 49 02 00`); `+0x264` = 0 or 1..10 random; `+0x278 = -1` (speed limit off) |
| 0x16 | `SA_TurnToObject` | `0x00461fdd` | 6 | `+0x260` = arg or 400000 |
| 0x1d | `SA_RotateToAngles` | `0x004653d1` | 0x1f | `+0x240/4/8` = angles (`0x80000000` = none) |
| 0x20 | `SA_GlidePos` | `0x004656f3` | 0x20 | point; zeroes `[obj+0x30/0x34/0x38]` |
| 0x1a | `SA_Idle(ship,ms)` | `0x0046274e` | 0x1d | point = current position; `+0x290` = now + ms |
| 0x10 / 0x177 | `SA_CaptureObject` / `SA_CollectDebris` | `0x0046142f` / `0x00468c96`¹ | 0x18 / 0x24 | `+0x224` (debris: point from the cell) |
| 0x119 | `SA_SetPlayerLand(ship,cont,port)` | `0x00463bc4` | **0x1b** | `+0x224`, `+0x250`; `+0x210` = now + 60000. Not a steering order: landing clearance for a player-flown ship |
| 0xfa | `SA_StopObject(ship,result)` | `0x00461e89` | **0** | from 0x1b raises `NotifyDockingAbort`; from 0x21 restores the position; `[obj+0x40] &= ~0x80`; wakes the task with `result` |
| 0x42 | `SA_SetDesiredSpeed` | `0x004641c6` | — | `[obj+0x14]` |
| 0x40 | `SA_SetSpeed` | `0x00464226` | — | `[obj+0x10]` (current speed) |
| 0x148 / 0x149 | `SA_SetSpeedLimit` / `…MaxSpeedOverride` | `0x00465d1a` / `0x00465d4e` | — | `flight+0x278` / `flight+0x27c` (both read by max speed `0x00450980`) |
| 0x144 | `SA_SetDestination` | `0x00465a03` | — | `flight+0x224` only (order unchanged) |
| 0x178 | `SA_SetFlightMode` | `0x00468db4` | — | `flight+0x860` → attitude offset `+0x8b8..` folded into the steering frame `flight+0x870` (`0x00450540`) |
| 0x120 | `SA_SetAutopilot` | `0x00463e6e` | — | `[obj+0x40]` bit `0x80000` (`0x00463e88`) |
| 0x105 / 0x106 | `SA_CollisionObject` / `SA_GetCollisionObjects` | `0x0046101f` / `0x00463a7e` | — | read `[obj+0x40]` bit `0x1000` / the list `flight+0x1e4[flight+0x1e0]` |
| 0x52 | `SA_SetMaxAvoidShield` | `0x00463689` | — | `flight+0x26c` |
| 0x80 / 0x43 / 0x4a | `SA_SetShipRotAccel` / `…ExtraSpeed` / `…ExtraRotSpeed` | `0x00467677` / `0x0046428b` / `0x004642ec`¹ | — | `flight+0x2b0` / `+0x274` / `+0x270` (16.16 fractions) |

¹ Case addresses of the ids not listed in `check_sites.py` were read from the same jump table; the
script checks 31 ids.

**The goal write, named for the integrator study:** the *command layer* writes the flight block in the
`SA_` cases above, e.g. `0x00462352 66 c7 82 58 02 00 00 19 00` (`flight+0x258 = 0x19`) and
`0x0046236d 89 91 40 02 00 00` (`flight+0x240 = x`). The *order layer* in `0x00452ad0` turns that into
steering. It writes the turn command `[obj+0x30]/[obj+0x34]/[obj+0x38]` through the leaves
(`0x0044ccc0`, `0x0044c620`, `0x0044c970`, dispatched by `0x0044d020`) and the desired speed
`[obj+0x14]`. Those leaves and the speed slew belong to the integrator study.

### 1.3 The order interpreter `0x00452ad0` [m]

The object-loop body of `0x00452ad0` dispatches on the class word. For class 7 it calls `0x0044f7b0`
(cargo/energy tick) and switches on `movsx eax, word [flight+0x258]` (`0x00453ce0`), with a byte table at `0x00459694` and a
jump table at `0x0045963c`:

| Order | Handler | Order | Handler | Order | Handler |
| --- | --- | --- | --- | --- | --- |
| 0 idle | `0x00458f8e` | 0x12 avoid | `0x00456722` | 0x1e undock | `0x00457bc3` |
| 4 fly-before | `0x004549c2` | 0x14 escort | `0x0045820d` | 0x1f rotate | `0x0045920e` |
| 5 fly-into | `0x00453fc2` | 0x15 follow | `0x00458aea` | 0x20 glide | `0x00459141` |
| 6 turn-to | `0x004554af` | 0x16 follow-cut | `0x00458e93` | 0x21 warp-in | `0x0045533b` |
| 8 warp-away | `0x004551fe` | 0x19 move-pos | `0x004564cd` | 0x22 defensive | `0x00455696` |
| 0x0a | `0x00455486` | 0x1b player land | `0x0045512c` | 0x23 move-cont | `0x0045659c` |
| 0x10/0x11/0x18 attack/capture | `0x00455e8c` | 0x1d idle-timed | `0x00453cfe` | 0x24 collect | `0x00455ae5` |

All other codes (and the end of every handler) reach the object-loop continue `0x004593dd`.

**Re-plan cadence [m].** Point-steering handlers act only when `now − flight+0x210 >= 0`. They then set
`flight+0x210 = now + rand(flight+0x20c)` (`0x004ee1c0`: `rand()·n/32768`). Ship init writes
`flight+0x20c = type[+0x50] × 50` ms (`0x004404a1` `6b c9 32 89 8a 0c 02 00 00`). So each ship re-steers
at random intervals in `[0, 50·type[+0x50])` ms; with `+0x20c < 1` it re-steers every frame. The avoid
order uses a fixed 200 ms (`0x00456798` `81 c1 c8 00 00 00`). The interpreter runs once per frame per
active sector, after the collision pass (`0x0043a38e` `56 e8 bc 2e 02 00 56 e8 36 87 01 00`). Its dt is
clamped to 1000 ms (`0x00452ae7`).

**MovePos handler, as the canonical goal-to-steering step [m].** At the re-plan time, `0x004564cd` calls
`0x0044ccc0` with `EDX = flight+0x240` (`0x00456527`); it calls `0x0044d020` (`0x00456530`) only when
`0x00450d60` (yaw rate) is 0. It then computes distance (`0x0042f850`), max speed (`0x00450980`) and
acceleration (`0x00450b80`), and sets `[obj+0x14]` through the piecewise map `0x0042fb20`. On
`dist <= flight+0x260` (`0x00456576`) it sets `[obj+0x14] = 0` and the order to 0, then wakes the task with
result 1 (`0x00456592`).

How the other handlers derive their point [m]:

* **escort 0x14:** leader position + the slot offset `+0x240..` rotated by the leader's yaw/pitch
  (`[leader+0x18]/[+0x1c]`, `0x004f0270` + `0x004f0da0`). Speed matches the leader's `[leader+0x10]`,
  floor 5000. A catch-up branch runs beyond `10 × flight+0x260`. Near the slot and aligned with the
  leader (cos > 0.9, `0xe665`) it aims `20·r_ship` ahead along its own axis instead of at the slot
  **[i]** (operand roles). Ends on the timeout or a lost leader.
* **follow 0x15:** the target's position. When the ship is within `1.5 × v²/(2a) + dist` behind a
  moving target, the point is moved ahead of the target. Ends with result 1 at `dist < flight+0x260`.
* **fly-before 4:** the point (or the port approach point). The arrival radius depends on size (§5).
  The container detour is described in §3.4.
* **attack 0x10/0x11:** the target plus the ring offset, re-randomised at `flight+0x288`.

## 2. How the KC side drives the orders (Q2)

### 2.1 Layers [m]

| Layer | Where | Role |
| --- | --- | --- |
| MSCI scripts (AI jobs, player commands) | `scripts/*.xml`, `*.pck` | not decoded here; they invoke MSCI movement commands |
| MSCI interpreter | KC class `0x2be` `__runScript` (stock CODE `0x38137`, Mayhem `0x38afa`) | dynamic calls (`85 <name>`) to the class-`0x7d4` methods below, e.g. `__MovePos` at stock `0x410a0`, `__EscortObject` ×4, `__MoveToStation` ×2, `__MoveToSector`, `__Idle`, `__AttackRunOnTarget` ×3, `__DefensiveMove`, `JumpToSector`, formation setters |
| Ship pilot methods | KC class `0x7d4` (the ship base class, 670 methods stock / 693 Mayhem) | blocking loops around one `SA_` order each |
| Native orders | `SA_` module, §1 | write the flight block; the order interpreter steers |

### 2.2 The movement wrappers and their re-issue rule [m]+[i]

Every wrapper in class `0x7d4` has the same shape. It sets the speed limit (`SA_SetSpeedLimit`, from
`CheckSpeedLimit`) and the flight mode. Then it checks `SA_CollisionObject(ship)` (warn bit `0x1000`) and,
unless `SA_GetFlags2 & 0x20000`, issues **`SA_AvoidObjectCollision`** first. Otherwise it issues its own
order. The `SA_` call blocks until the order ends (§1.1). Result 1 ends the method. Any other result
(`SA_StopObject(…, 2)` from `CollisionWarn`) loops back to the head with the same stored goal: `__MovePos`
`0xb88f9` → `02 5b 34` (compare with 1) → `UpdatePosition` → loop head `0xb87c4` **[i]** from the jump
targets. So the KC re-issues a goal only when an order *ends*. It does not poll and does not compute
intermediate waypoints within a sector.

| Method (class `0x7d4`) | Stock entry | Mayhem entry | Natives (CODE order; full lists in `kc_method_flow.txt`) |
| --- | --- | --- | --- |
| `__MovePos` | `0xb87ae` | `0xd0d7b` | SetSpeedLimit, SetFlightMode, CollisionObject, [AvoidObjectCollision(ship,0)], SetShipRotAccel, GetSafePos, **MovePos(ship,x,y,z,prec)**; out of sector `__galaxyFlightStep(…,1000)` |
| `__MovePosContinue` | `0xb896c` | `0xd0f39` | same with **MovePosContinue** |
| `__EscortObject` | `0xb8b2a` | `0xd10f7` | [Avoid(ship, GetSize()+50000)], **FollowObject(ship,target,prec,t)** or **EscortObject(ship,leader,slot.x,slot.y,slot.z,prec,t)** |
| `__DefensiveMove` | `0xbb9c9` | `0xd3fba` | [Avoid(ship, GetSize()+25000)], **DefensiveMove**, `TI_WaitForInterrupt` |
| `__Idle` | `0xb84a8` | `0xd0a75` | [Avoid], **Idle** (constants 60000), random wander point (`SE_Random` with 2500000, 5 km **[i]**), `TI_WaitForInterrupt` |
| `__MoveToStation` | `0xb7526` | `0xcf19d` | `__MoveToSector` if not in the station's sector, then `__LandInHangar` |
| `__MoveToSector` → `__FlyToNextSector` | `0xb7470` → `0xb7231` | `0xcf0e7` → `0xceea8` | **`GS_Route`** (gate route), `GetGate`, `__EnterHyperspace`, `TI_DelayRandom(2000,3000)`, `__LeaveHyperspace` |
| `__EnterHyperspace` (gate) | `0xbca8e` | `0xd524d` | `RequestWarpInSlot` / `RequestWarpThroughSlot`, `SA_Idle` (queue), **FlyObjectBeforeContainerPos**, **FlyObjectBeforeContainer**, **RotateToAngles**, **FlyObjectIntoContainer**, **WarpObjectAway** |
| `__LandInHangar` (dock) | `0xb7c25` | `0xcf94e` | `RequestDockPort`, SetShipDockPortNr, SetDestination, [Avoid], **FollowObject** / **MovePos** (far approach), **EscortObject** (holding slot while queued), `__Idle(15000)`, **FlyObjectBeforeContainer**, **FlyObjectIntoContainer**, `SA_MoveObjectIntoContainer` |

**AI job across a sector [m]+[i].** The MSCI layer calls `__MovePos` / `__MoveToStation` /
`__MoveToSector`. Within a sector the goal is one straight-line point, held until arrival. The only
intermediate points are made natively: the avoid order's escape points (§3.3), the container detour
(§3.4), dock and gate approach points (§4), and the gate sequence from `GS_Route` between sectors.
Out of sector (no update focus) the wrappers use `__galaxyFlightStep` (`0xb6ef0`). That is a KC
position integrator (`SA_SetPosAndDirection`, `TI_WaitForInterrupt`, 1000 ms steps **[i]**) without
collision.

**Player autopilot [m].** Player commands ("fly to", "dock at", "follow") start the same MSCI scripts on
the player's ship. `CallbackBeginStartThisScript` / `CallbackEndStartThisScript` /
`CallbackStopThisScript`, `SetAsPlayerShip` and `Enable/DisableScriptFromMonitor` call
`SA_SetAutopilot(ship, IsAutopilotActive())`. `IsAutopilotActive` (`0xc9dec`) is
`702::IsScriptTaskRunning`. The natives and the order layer are therefore the same as for AI. Bit
`0x80000` changes two things. Order 0 zeroes the desired speed and the target instead of damping the
turn rates (`0x00458f8e`). `CollisionWarn` treats the ship as AI (§3.2). Manual flight is
`[obj+0x40] & 0x40000` without `0x80000` (`0x0043ff80`; bit `0x40000` set by `SA_SetPlayerControl`).

**Formation, escort and wing [m]+[i].**

* Slots are offsets in the leader's frame, stored per follower in `member0[9..11]` by
  `SetFormationPos(x,y,z)` (stock `0xaafdf`).
* `ReorgFormation` (stock `0xab4b0`, Mayhem `0xc2956`) computes the slots per formation type (an
  `0x78` switch). Its only caller of `SetFormationPos` is itself (26 sites). It uses `SE_Sin`/`SE_Cos`/`SE_MulFix`
  and constants 17408, 31000, 94208, 65535, ±(`SE_Random(100)` − 30/50)·20 and ×4/×8 multiples **[m]**;
  the per-type geometry was not decoded.
* Spacing unit: `GetFormationDist = 4 × GetSize()` (stock `0xab2f6`: `01 87 GetSize 05 04 50 83`).
  `GetSize` = `SA_GetTotalSize(ship)`, else 10000. That is `0x00488170`, i.e. `[obj+0xa4]`, the collision
  radius, computed lazily from the child bounds.
* Formation speed: `GetMaxFormationSpeed` = the slowest follower's `GetAllowedMaxSpeed` (followers below
  5000 ignored) × 98 / 100 (`0xab38c` `05 62`, `0xab38f` `05 64`). `CheckSpeedLimit` applies it as the
  leader's `SA_SetSpeedLimit`.
* Followers fly **`SA_EscortObject(ship, leader, slot, prec, timeout)`** (native order 0x14; the slot is
  re-rotated by the leader's attitude at every re-plan, §1.3). With no slot they fly
  `SA_FollowObject(ship, target, prec, timeout)` (order 0x15).
* Escort and follow pairs get half the braking distance in the warn test (§3.1), so a formation does
  not warn on itself as early.

**Stock vs Mayhem [m].** The call/constant sequences of `__MovePos`, `__MovePosContinue`,
`__EscortObject`, `CollisionWarn`, `ReorgFormation`, `GetFormationDist`, `__Idle`, `__DefensiveMove`,
`__EnterHyperspace`, `__FlyToNextSector`, `__MoveToStation`, `__MoveToSector`, `CheckSpeedLimit` and
`__galaxyFlightStep` are identical (member slots shift: e.g. 41/45/46 → 43/47/48). Mayhem differs in:

* `__AttackRunOnTarget`: station checks within 3000000 (6 km) using `B3D_InstDistanceToNearestVertex` and
  `GetSize` + 500/300/600 offsets.
* `__StartInHangar` calls a new `SafeUndock`, which sets `Flags2 0x20000` for ≈3 s and clears it again.
* A new `Bounce` (`0xc180e`, started from `Activate`; gated by `0x96::IsBounceEnabled`) polls
  `SA_GetCollisionObjects` every 250–5000 ms. Near a station (`SA_Distance` vs size + 250000) it calls
  `SetRot` (away vector) and `SetSpeed(GetMaxSpeed)`.
* A new `SetSpeedForTime` and a new `__MoveToHangar` (no static caller); `__runScript` also calls
  `SA_GetCollisionObjects`.
* `0x96::NotifyCollisionWarn` no longer stops SETA.

## 3. Collision avoidance today (Q3)

### 3.1 Native proximity test [m]

Pair selection is in the sector collision pass `0x0045d250`, loop L2 ([sector-collide.md](sector-collide.md)
§2–3):

* For every pair with a class-7 side that passes the integer box `|Δx|,|Δy|,|Δz| <= 3·(rA+rB) + 250000`
  (`0x0045d51c` `ba 00 00 03 00`, `0x0045d538` `05 90 d0 03 00`), the pass computes `d` = truncated
  Euclidean centre distance (float32 sum of squares).
* After the contact and dock/gate checks it calls `0x0044e450(A, B, d)` and `0x0044e450(B, A, d)`
  (`0x0045d844`, `0x0045d867`) for each class-7 side without `[obj+0x40] & 0x800`.

`0x0044e450` (stdcall, `ret 0xc`):

* Skips when `[A+0x44] & 0x10` or when the obstacle filter `0x0044d2f0` rejects B. Capital ships (type
  codes TL, M0, M1, M2, TL_P, M7) ignore small ships; escort/follow partners of A are ignored.
* While A is avoiding (order 0x12) it only refreshes the list. During undock (0x1e) it skips the carrier
  being left.
* Otherwise it calls **`0x0044dd90(A, B, d, &out)`**. On true it sets `[A+0x40] |= 0x1000`
  (`0x0044e52b`), appends B and `d` to `flight+0x1e4[]`/`+0x1f8[]` (max 5, `0x0044e552`) and sets `|= 0x2000`
  (`0x0044e56c`).

`0x0044dd90` (stdcall, `ret 0x10`), the warn test. Values are in native units; cos is 16.16:

* Not moving (`[A+0x10] = [A+0x14] = 0`) or no acceleration → no warn.
* **Close range:** when B is large relative to A (`rB >= 0.75·rA`), a station (class 5/6) or class
  `0x11`, or A cannot move: warn if `d <= 1.2·(rA+rB)` (`0x0044ded9` `0x13333`). For the attack target
  the limit is `d <= rA+rB`.
* **Margin** `L = clamp(rA + rB/10 (not attacking) + rA (avoiding), 15000, 500000) + (rA+rB)`
  (`0x0044df25`, `0x0044df34`; 30 m, 1 km). When B is ahead, `rA·cosθ` is added, where θ is the angle
  between A's forward axis (`flight+0x870` row 2) and A→B.
* **Braking distance** `b = v_close² / (2·a)`, with `a = 0x00450b80` (type `+0x48` × (1 + `flight+0x274`))
  and `v_close = |vA|·cosθ` + B's speed toward A. The B term (B's speed along its forward axis projected on
  A→B) is added only when A heads at B within cos 0.8 (`0x0044e07c`), or, for a ship B, when A heads
  away from it (cos < −0.8); it is doubled for some types. `b` is capped at 300000 (600 m)
  and halved when A and B escort or follow the same leader or each other.
* **Warn** if `b + L + (rA+rB) >= d` and, when `d > b`, B's sphere inflated by 20000 (40 m) covers the
  heading ray seen from the braking point: `cos²θ > 1 − ((rA+rB+20000)/(d−b))²`.
* Class 5/6 objects (stations and docks **[i]**), class-`0x14` subtypes `0x26..0x2a`, `0x2d..0x2f`,
  `0x7c/0x7d` and capital-type ships as B go to a mesh test instead of the cone:
  `0x0044d9c0(B.phys, |vA|+b, out, 1.2, 0)`.

So the warn distance is predictive and scales with closing speed, size and acceleration. It is not a
fixed radius **[m]** (formula); the resulting distances per ship class are the game-scale study's to
tabulate.

### 3.2 From the flag to the KC handler and back [m]

The handler is reached as follows:

* Loop L0 of the next collision pass clears bits `0x1000/0x2000` and `flight+0x1e0`
  (`0x0045d280`, `0x0045d298`).
* L3 (end of the same pass) calls `0x0049f4c0(…, [obj+0x94], "CollisionWarn", 1, 0)` synchronously
  (`0x0045e079 push 0x55fd74`, `0x0045e089`). It does so for every object with both bits set,
  order ≠ 0x12 (`0x0045e05a`), a script object, and no `[obj+0x40] & 0x8000000`.

KC `CollisionWarn` (class `0x7d4`, stock `0xbdfd6` / Mayhem `0xd67f6`) branches:

| Branch | Condition | Action |
| --- | --- | --- |
| player, manual | `member41 == global 8` **[i]: this is the player's ship** and `SA_GetFlags & 0x80000` clear | `0x96::NotifyCollisionWarn`, throttled to 3 s. Stock stops SETA (`StopFastForward`); Mayhem only throttles. **No avoidance for the manually flown player ship.** |
| ignore | `member45 & 0x80000` | return |
| targeted | `member45 & 0x40000` | `SA_StopObject(ship, 2)` only if `SA_GetCollisionObjects` contains `member46` |
| default | — | **`SA_StopObject(ship, 2)`** |

Class overrides: `0x816` (methods `CreateSwarm`, `CatchWare`, …) always calls `SA_StopObject(ship, 2)`.
`0x7db` (`EquipOnEject`, `___startDefendThread`, …) has an **empty** handler and never avoids.

**The loop, end to end [m]+[i]:**

1. Frame N, collision pass: warn flag set, `CollisionWarn` runs inside the pass, `SA_StopObject` sets
   order 0 and wakes the movement task (result 2).
2. Frame N+1: the scheduler runs before the sector update (callers `0x00403a7a..0x00403aff`, before
   `0x00403b17`; [script-task-scheduler.md](script-task-scheduler.md)). The wrapper loops. Bit `0x1000` is
   still set because L0 of frame N+1 has not run yet, so it issues `SA_AvoidObjectCollision` (order 0x12).
   **[i]**: one frame of latency, assuming the woken task is runnable by then.
3. Frames N+1…: order 0x12 runs every frame and re-plans every 200 ms (§3.3). L3 sends no further
   `CollisionWarn` while it runs.
4. The avoid order ends (list empty and the ship more than `flight+0x260` from the start point, plus a
   random term when `+0x264 > 0`). It wakes the task (`0x00460560`). The wrapper loops and re-issues
   the stored goal (`SA_MovePos` …).

### 3.3 The native avoid order 0x12 (`0x00456722`) [m]

Every 200 ms the handler re-scores the recorded obstacles. It re-runs `0x0044dd90` for each, or the mesh
test `0x0044d9c0` for large ones, and drops those that no longer warn. If any remain, it samples escape
directions:

* The start is a fixed basis perpendicular to the obstacle direction, or a random yaw when
  `flight+0x264 > 0`.
* Steps are `0x200` (2.8°) up to `0x7800` (≈169°), each with a random elevation.
* A candidate is blocked if it still points into any recorded obstacle. Each obstacle it clears adds
  a weight. Obstacles that must be turned away from (`rB >= 0.75·rA`, stations, class `0x11`) count
  when the candidate points away (dot ≤ −0.8, −0.9 for gates): gate 150, `0x11/0x1c` 100,
  `0x1d/0x1e`/stations 50, large ships 10, others 1. The rest count when outside their cone: gate 100,
  `0x11/0x1c` 50, `0x1d`/`0x1e`/stations 25, large ships 10, others 1.
* It takes the first unblocked direction, else the blocked candidate with the highest weight sum. The
  escape point is position + direction × `0x0042f970` scale, written to `flight+0x240..` (start point
  refreshed in `+0x230..`).

It steers with `0x0044d020`, or with `0x0044ccc0` when the escape direction is behind
(dot < −0.1) or a must-turn-away obstacle is present. `flight+0x2b0` (turn-acceleration factor) is
raised to ≥ 0.7, and to 1.0 with a must-turn-away obstacle. The desired speed is the `0x0042fb20` map,
or the slowest obstacle's speed (floor 5000) when one is recorded. **Order 0x12 is the native "steer around" layer; it is entered only through the KC
round trip above.**

### 3.4 Other native avoidance on the goal-to-steering path [m]

* **Container detour in order 4** (`0x004549c2`). When the container lies ahead and the approach point
  is close to it, the handler replaces the point by a tangent point around the container's bounding
  sphere (cross products of the ship→point and ship→container directions, scaled by `0x0042f970`)
  before calling `0x0044d020`. Trigger [m]: a dot product > 0 (`0x005654d8` = 0.0), a distance below
  2·r_container (`0x20000`), and a distance comparison with factor 1.05 (`0x10ccc`); which vectors
  each operand holds is **[i]**. The integrator study will not see this from below: the leaf receives
  an already-detoured point.
* **Arrival slow-down in orders 4/5:** speed capped near the target, container flagged
  `[cont+0x40] |= 0x10000000` (approaching) **[i]** (bay/traffic use not traced).
* **None** in the order handlers 0x19/0x23/0x14/0x15/0x10: they steer straight at their point.
  The integrator part of `0x00452ad0` before the class switch was not examined here.

## 4. Gates, docking, undocking (Q4)

**AI docking [m].** `__LandInHangar` reserves a port (`RequestDockPort`, `SA_SetShipDockPortNr` →
`flight+0x250`). It flies far legs with `FollowObject`/`MovePos`, holds a slot while queued
(`EscortObject`, `__Idle(15000)`), then issues **`FlyObjectBeforeContainer(ship, station, port, path)`**
(order 4: approach point of the port) and **`FlyObjectIntoContainer`** (order 5: port path; rendezvous
`+0x230`, final `+0x240`). Order 5 caps the approach speed by type code: **75000** for TL, TS, M1, M2,
TL_P, TS_P, M6, TP, M7, TM, M8 and **50000** for the others (`0x004542d2`, `0x004542f1`). It ends with
result 1 inside the port, after which KC calls `SA_MoveObjectIntoContainer` (the ship leaves space).
**Hand-back point:** order 4 entry / order 5 entirely belong to the engine's port path.

**Player docking [m].** `SA_SetPlayerLand` sets order 0x1b (clearance, no steering). Each frame
`0x0044d4c0` in the collision pass returns 2 within `[obj+0xa4] + 25000` of a port point
(`0x0044d89b`, range floor 1,331,000 `0x0044d7d9`) and slow enough (`[obj+0x10] <= max(vmax/4, 20000)`,
`0x0044d990`). The pass then calls `"CanLand"` → KC `CanLand` / `LandPlayerShipAt`
([chase-view-docking.md](chase-view-docking.md)). Order 0x1b aborts with `NotifyDockingAbort` after
300000 ms or beyond 2·r_station.

**Gates [m].** `__EnterHyperspace` queues for a slot (`SA_Idle`), flies
`FlyObjectBeforeContainerPos` / `FlyObjectBeforeContainer`, aligns with `RotateToAngles` (gate angles),
then `FlyObjectIntoContainer(ship, gate)` (order 5 with a gate-axis point) and `WarpObjectAway` (order 8,
900 ms). A ship crossing a gate plane triggers `0x0044d4c0` → 3 → `"CanWarp"`. That happens within
0.7·r_gate, or 0.14·r_gate for gate subtypes 5..8 (`0x0044d5de`, `0x0044d5e8`), and only when
`[ship+0x40] & 2` (meaning not established) or manual player control (`0x0043ff80`). AI ships transit
through orders 5 and 8. Arrival: `WarpObjectIn` (order 0x21) places the ship at the exit, then
`ClearObjectFromHangar` (order 0x1e) moves it clear. `__LeaveHyperspace` compares `SA_Distance` with
1750000 (3.5 km) before the next leg **[i]**.

**Undocking [m].** Order 0x1e from `__StartInHangar`. The proximity test ignores the carrier being left
(§3.1). Mayhem additionally sets `Flags2 0x20000` for ≈3 s (`SafeUndock`). As candidate B that bit
removes the ship from the pair loop entirely ([sector-collide.md](sector-collide.md) §3), and the KC
wrappers skip `SA_AvoidObjectCollision` while it is set.

**Hand back control [i]:** a replacement steering law should not alter goals for orders 5, 8, 0x21 and
0x1e. In order 4 it should keep the container out of its obstacle set inside about 2·r_container (the
engine detours natively, §3.4).

## 5. Ship classes in AI decisions (Q5)

**Class source [m].** `type[+0x14]` of the ship record (`*0x00606fd4` = the class-7 slot of the per-class
table array at `0x00606fb8`, stride `0xdb8`) is the `SG_SH_*` code. The enum table at `0x00552758`:

| Code | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Name | TL | TS | M0 | M1 | M2 | M3 | M4 | M5 | TL_P | TS_P | GO | M6 | TP | M7 | TM | M8 |

The code comes from TShips column 5 (e.g. `SG_SH_M3`), measured in the Mayhem `TShips`.

**Size predicates [m]:**

* "Large" `0x0043fd00` = {TL, M0, M1, M2, TL_P, M6, M7, TM, M8}.
* "Capital" `0x0043fd50` = {TL, M0, M1, M2, TL_P, M7}.
* `0x0043fda0` tests codes `0x10e..0x171`, which no `SG_SH` code reaches **[i]: always false for ships**.

**Size-aware movement today [m]:**

| Where | Rule |
| --- | --- |
| order 4 arrival radius | gate: 0.75·r_gate; target ship: 111000 (222 m); otherwise 3·rA (rA for "large"), clamped to [22200, 111000] (`0x00454b48`, `0x00454b5d`) |
| order 5 dock approach | speed caps 75000 / 50000 by class (§4) |
| avoid order default radius | `rA + 150000` (300 m); KC passes `GetSize()+50000` (escort) or `+25000` (defensive) |
| warn test | margin, close-range rule and braking distance all scale with rA, rB and acceleration (§3.1); capitals ignore small ships; large and capital obstacles use the mesh test |
| avoid scoring | large obstacles ×10 |
| formation | spacing 4·rA (`GetFormationDist`); slots per formation type |
| re-plan cadence | `type[+0x50] × 50` ms window (§1.3) |

Object flags used on these paths:

| Flag | Meaning |
| --- | --- |
| `[obj+0x40]` `0x1000` / `0x2000` | warn / listed |
| `[obj+0x40]` `0x80000` | autopilot |
| `[obj+0x40]` `0x40000` | player control |
| `[obj+0x40]` `0x800` | skip proximity as A |
| `[obj+0x40]` `0x100000` | has a hangar or dock (container) |
| `[obj+0x40]` `0x8000000` | no `CollisionWarn` |
| `[obj+0x44]` `0x10` | skip proximity |
| `[obj+0x44]` `0x20000` | out of the pair loop as B (= KC `Flags2`) |

`SA_GetFlags` is `[obj+0x40]` (`0x004637df`) and `SA_GetFlags2` is `[obj+0x44]`.

## 6. Hook sites for a predictive avoidance layer (Q6)

| Candidate | Sees | Contract | Verdict |
| --- | --- | --- | --- |
| **A. `0x0044ccc0` entry + `0x0044d020` entry** (steering-target hand-off) | every point the order layer steers to (orders 4, 5, 6, 0x10/11/18, 0x12, 0x14, 0x15, 0x19, 0x1d, 0x1e, 0x1f, 0x22, 0x23, 0x24), at the re-plan rate | see below | **best: goal point in → adjusted point out** |
| B. `0x004593dd` object-loop continue | every object after its order step, every frame; `[obj+0x14]` and `[obj+0x30..0x38]` final for the frame | `8b 1b 83 3b 00` = `mov ebx,[ebx]` + `cmp [ebx],0` (5 B, two whole instructions), followed by `jne 0x00452b60`. 104 incoming jumps, all to `0x004593dd` itself; none into `0x004593df..e1` [m]. EBX = current object on entry; the flags from the replayed `cmp` are live into the `jne`. ESP-relative frame (locals at `esp+0x2f8..`) → the stub must keep ESP exact. | complement: per-frame speed clamp / emergency brake; sees commands, not goals |
| C. `SA_` cases (`0x0046232c` …, or the dispatcher entry `0x00460630` `55 8b ec 83 e4 f0`) | the command once, when issued | per-case stores; dispatcher is cdecl, 5 args | logging only: escort, follow and attack goals are recomputed per tick inside the order layer and never pass here again |
| D. `0x0044dd90` entry / result | the warn decision per (ship, obstacle) pair | stdcall 4 args, `ret 0x10`; prologue `55 8b ec 83 e4 f0` (3 instructions, 6 B) | the place to **suppress** the reactive round trip (below) |

**Contract of candidate A [m]:**

* `0x0044ccc0`: `EDX` = `const int32 point[3]` (world units), `[esp+4]` = obj, callee pops 4
  (`0x0044d011 c2 04 00`). Prologue `83 ec 18` | `8b 02` is exactly 5 bytes on two instruction
  boundaries; no xref into `0x0044ccc1..c4`. Live-in: EDX and the stack argument only (EAX and ECX are
  overwritten at `0x0044ccc3`/`0x0044ccc5` before any read); flags dead. Callee-saved
  EBX/EBP/ESI/EDI are pushed by the function itself. It calls only math helpers (`0x004effc0`,
  `0x0052b5d0`, x87 `fpatan/fsin/fcos`).
* `0x0044d020`: stdcall `(obj, const int32* point)`, `ret 8` (`0x0044d112`). Prologue `55` | `8b ec` |
  `83 e4 f0` (6 B, three instructions, no xref inside). It reads its arguments from `[ebp+8]/[ebp+0xc]`
  after the frame setup; EAX/ECX/EDX are dead on entry. It tail-dispatches to `0x0044c620` /
  `0x0044c970` (the roll leaves, which bypass `0x0044ccc0`) or calls `0x0044ccc0` at `0x0044d107`.
* To adjust each decision once, hook both entries and skip the `0x0044ccc0` hook when its return
  address is `0x0044d10c` (the call from `0x0044d020`). The hook copies the point into its own buffer
  and passes the adjusted copy on. It must not write `flight+0x240`, which the order layer reuses as
  state (e.g. the avoid order).
* **Reentrancy [m]:** both run on the game thread inside `0x00452ad0` (once per sector per frame, after
  the collision pass), with no script calls below them. The hook must not call script natives
  (`0x0049f4c0` re-enters the VM). x87 state: the leaves use the FPU; an SSE2 hook must leave the x87
  stack and control word as found.

**What the hook can read [m]:** position `phys+0x30..`, speed `[obj+0x10]`, desired speed `[obj+0x14]`
(for most orders set *after* the steering call this tick, so the previous value), heading frame
`flight+0x870`, radius `[obj+0xa4]`, order `flight+0x258`, target `flight+0x224`, and the current
obstacle list `flight+0x1e0..` (refilled each frame by the collision pass). It also has its own sector
census: the bucket lists at `[sector+0x50]`.

**The KC handler [i].** Leave `CollisionWarn` and the KC wrappers unchanged. They are shared by stock
and Mayhem scripts and are the only path into the native avoid order. Instead, stop the warn at its
native source for obstacles the predictive layer has already resolved: return false from `0x0044dd90`
(candidate D) for that (ship, obstacle) pair, or keep `[obj+0x40] & 0x1000` clear. Then no
`CollisionWarn`, no `SA_StopObject`, no avoid order and no goal churn follow. When the predictive
layer cannot resolve a case (obstacle inside the braking distance, or a large mesh obstacle), let the
original warn through so the existing reactive loop stays the fallback. Do not use `Flags2 0x20000` for
this: it also removes the ship from collision response. Bit `0x8000000` silences `CollisionWarn`
entirely and would disable the fallback.

## 7. Not established, and the runtime rows that would settle it (Q7)

Open points:

* How a woken task (`task+0x84 = 1`) returns to the runnable list. The one-frame `CollisionWarn` →
  avoid latency in §3.2 depends on it **[i]**.
* KC stack semantics are read from opcode patterns, not executed. Open as a result: the `member41`
  test (player ship), the meaning of `member45` bits `0x80000`/`0x40000` and `member46`, the
  `ReorgFormation` per-type slot geometry, the `__EscortObject` fallback slot, the timeout and precision
  values the wrappers pass, and the `__galaxyFlightStep` step length.
* The MSCI script layer (which job scripts call which movement commands, and how often). Not decoded.
* `0x0044d9c0` (mesh warn test) internals; the meaning of `0x0043fda0`; the `flight+0x870` frame vs the
  flight-mode offset (integrator study).
* `type[+0x50]` and `type[+0x48]` TShips columns (game-scale study).
* Whether the 0x44c620/0x44c970 roll leaves are reached in practice.

One consolidated opt-in diagnostic on the existing `src/proxy/` engine-patch infrastructure (SiteSpec
entry sites; counters plus thresholded rows, no per-call logging) would settle the runtime parts in one
flight past a station with traffic:

1. `0x00460630` entry, filtered to the movement ids of §1.2: (task id, ship, cmd, args), counted per
   command. This gives the KC re-issue rate and which wrappers AI ships use.
2. `0x0044ccc0` and `0x0044d020` entries: (ship, order `flight+0x258`, return address → handler, point),
   sampled, plus the interval since that ship's previous call. This confirms the re-plan cadence
   `rand(50·type[+0x50])`, which roll leaves are reached, and that candidate A covers all point steering.
3. `0x0044e450` after the `0x0044dd90` call (or at `0x0044e52b`): (A, B class, d, rA, rB, `[A+0x10]`,
   order). This validates the warn distances per class.
4. `0x0045e089` (`CollisionWarn` dispatch) and `0x00460560` entry (ship, result): with row 1 this gives
   the frame delta warn → `SA_AvoidObjectCollision` → order end → re-issue, and the result-value
   distribution (1/2/3).

## Reproduce

```sh
python3 -I verification/results/ship-navigation-ai/check_sites.py      # RESULT PASS, 138 checks
python3 -I verification/results/ship-navigation-ai/kc_movement_calls.py --all-sites   # stock + Mayhem call sites
python3 -I verification/results/ship-navigation-ai/kc_method_flow.py   # wrapper call/constant flow
```

Ghidra (read-only, serialised): `X3DecompileFunctions.java` on `00460630 00452ad0 0044e450 0044dd90
0044d4c0 0044d020 0044ccc0 00450980 00450b80 00460560`, `X3XrefsTo.java` on the hook sites of §6,
`X3GrepInsns.java` with `0x25[08]\]|0x224\]` for the order-field users.

## 10. Gate transit, step by step

2026-10-12, static study of the same `X3AP.exe` (SHA-256 `fdbf3418…`) and both `x3story.obj` versions; nothing was
launched. Scripts and their outputs are under
[`verification/results/ship-navigation-ai/gates/`](../../verification/results/ship-navigation-ai/gates/):
[`kc_gate_flow.py`](../../verification/results/ship-navigation-ai/gates/kc_gate_flow.py) (symbolic stack walk of the
KC methods, pseudo-statements only, zero depth conflicts in every listed method; stock and Mayhem equal after masking
member/global slots), [`check_sites.py`](../../verification/results/ship-navigation-ai/gates/check_sites.py) (every
EXE byte string below plus the hook-window branch scan, 73 checks, `RESULT PASS`) and
[`gate_timeline.py`](../../verification/results/ship-navigation-ai/gates/gate_timeline.py) (kinematic model of the
engine laws with the Mayhem class medians; its numbers are **[i]**). Raw decompiles stayed in `~/x3-nav-study/`.
`r` = the gate's collision radius `[gate+0xa4]`; the Mayhem map's ordinary gates are 1105 m
([game-scale-and-ship-classes.md](game-scale-and-ship-classes.md)). Gate-local axes: z is the gate axis (row 2 of
`[gate+0x70]+0x40`, the axis order 8 and the `"CanWarp"` test use), ships enter from −z and leave along +z **[m]**
(order 8 moves along row 2; KC `GetWarpInPos` places capitals at −z; `RotateToAngles` turns them to the gate's +z).

**Answer.**

* **No ship uses the gate disc.** A non-capital ship flies order 4 to **one approach point shared by every ship** at
  that gate, `A = G + R·(0, −0.8, −1.0)·r` (1 r in front, 0.8 r below the model origin), then order 5 to a pass point
  `B = G + R·(0.55 sin θ, −0.55 cos θ, −0.01)·r` with θ uniform in ±45° per issue (−0.05 instead of −0.55 for gate
  subtypes 5..8): an arc **on the gate disc**, 0.55 r from the centre within ±45° of straight down, on the gate plane
  (corrected 2026-10-12 from Run154's 91 transitions and the listing: the earlier reading put the arc in the
  vertical y–z plane) **[m]**. Capital ships get one of
  20 per-slot approach points (a 5 × 4 grid, 666 m pitch, `2·(r_gate + r_ship)` in front) and then fly order 5 to
  the gate's **origin** **[m]**.
* **Main cause of the clunk:** both engine legs end with an instantaneous stop. Order 4 and order 5 each write 0 to
  the *current* speed `[obj+0x10]` at arrival (`0x00454b94`, `0x00454703`), and order 5 starts with a turn at
  500 units/s (1 m/s) while the ship is more than 18.2° off its pass point. Order 8 then launches the ship from rest
  along the axis **[m]**. The slot queue exists only for capital ships (KC class 2019: M1, M2, TL and class 2027). Small ships never queue
  and never call `RotateToAngles` **[m]**. Every small ship converges on the same point A with `CollisionWarn` live,
  so the reactive stop/avoid loop also fires there **[m]** mechanism, **[i]** frequency.
* **The installed steering law makes the last ~1 km slower, not smoother:** order 4 to a gate has the arrival ramp
  to the 5000 units/s floor at `d_arr = 0.75 r`, so the ship reaches the stop at 10 m/s instead of the vanilla
  37–49 m/s (model) **[i]**.

### 10.1 The script loop, `2004::__EnterHyperspace` [m]

Stock `0xbca8e`, Mayhem `0xd524d`; all 221 statements are equal after masking member/global slots (stock m84/m85 =
Mayhem m87/m88, m45 = m47, g9 = g10). Arguments are listed with argument 0 first. `big = SE_IsClass(this, 2019)`.
`m3` is the ship's native object, `phase` (p0) 0/1/2, `slot_type` (p6) 0/1/2, `pos` (p3) the warp-in position.

```
loop (head 0xbcb21):
  (m12 & 0x80) -> return 7                              attacked; __FlyToNextSector returns at once
  gate gone / destination gate changed -> return 8
  m45 = 0                                               CollisionWarn active (default branch: SA_StopObject)
  if big && no slot && slot_type == 0:
      s = gate.RequestWarpInSlot(this)                  0xbcc2b
      s < 0: SA_SetSpeedLimit; SA_SetFlightMode(1); SA_Idle(ship, 1000 + SE_Random(10000)); continue   0xbcc7e
  if slot held && slot_type == 0: pos = gate.GetWarpInPos(slot, this)                                 0xbccd9
  if slot_type == 1 && big:
      gate.RequestWarpThroughSlot(this) < 0: TI_WaitRandomForInterrupt(1000, 10000, this); continue  0xbcd34
      slot_type = 2
  phase 0:  SA_CollisionObject(ship) ? SA_AvoidObjectCollision(ship, 0)
            : r = pos ? SA_FlyObjectBeforeContainerPos(ship, gate, pos) : SA_FlyObjectBeforeContainer(ship, gate, -1, 0)
            r == 1 -> phase 1;  r == 5 -> return 8
  phase 1 && big:  SA_RotateToAngles(ship, SA_GetRotAlpha(gate), SA_GetRotBeta(gate)) == 1 -> phase 2, slot_type 1
  else (phase 1 small, phase 2 big):
            SA_SetFlightMode(ship, 0); m45 = 0xA0001 (bit 0x80000: CollisionWarn returns without action)
            r = SA_FlyObjectIntoContainer(ship, gate, -1)                                             0xbcebc
            r == 1 -> (this != g9) SA_SetDesiredSpeed(ship, 0); SA_WarpObjectAway(ship, gate); return 3
            r == 7 -> phase 0, ReleaseFromSlot, slot_type 0, pos 0
exit (0xbd10e): ReleaseFromSlot(); r == 3 -> Remove() (this != g9), gate.SetKnown(1) (m41 == g8)
```

`g9` is read as the player ship and `m41 == g8` as "owned by the player" **[i]** (`__FlyToNextSector` sends
`this == g9` to `150::StartGateWarp`). Every `SA_` call blocks until its order ends (§1.1). Each step is therefore serialised behind the previous order's
completion, plus the scheduler's resume of the task (one frame **[i]**, §7). `__FlyToNextSector` (stock `0xb7231`) calls
`TI_DelayRandom(2000, 3000)` only when `__EnterHyperspace` returns neither 3 nor 7 (`0xb72e0`, the failure path); after
a successful transit (3) a non-player ship goes straight to `__LeaveHyperspace` **[m]**. The out-of-sector branch
(`m3 == 0`, `__galaxyFlightStep`, `SA_IsWarpOutSlotAvailable`, `TI_WaitRandomForInterrupt(100, 1000)`) is not
visible flight and is not covered here.

**Which ships take the capital path [m]:** KC class 2019 has no name on text page 2006. Its parent chain is 2033
"Carrier" → 2073 "Big Ship" → 2134 "Moveable Ship" → 2004 "Ship". Its children are 2021 "M1", 2022 "M2",
2032 "TL" and 2027 (no name). M6 (2026), TM (2141), TS, TP, M3–M5 are outside it. The native side makes the
same split with the capital predicate `0x0043fd50` {TL, M0, M1, M2, TL_P, M7}
(`0x004608ea`). Whether an M7 also derives from 2019 in KC was not established.

### 10.2 Q1: the slot queue (capital ships only) [m]

Both slot methods are KC (gate class 2014), not natives:

| Method | Stock entry | Rule |
| --- | --- | --- |
| `RequestWarpInSlot(ship)` | `0x90a59` | 20 slots per gate in `m14[0..19]`, release stamps in `m14[20..39]`. One pass clears dead entries and the ship's own entry. It takes the first empty slot whose stamp is `<= now`, stores the ship and returns the index, else −1. |
| `GetWarpInPos(slot, ship)` | `0x90d61` | `G − 2·(GetSize(gate) + GetSize(ship))·F + 333000·(a·R + b·U)`, with `a = {0, 1, −1, 2, −2}[slot % 5]`, `b = −(slot / 5)`. `R, U, F` are the rows of `SE_GetRotMatrixFromAngles` of the gate's angles: a 5 × 4 grid with a 666 m pitch in front of the gate. |
| `RequestWarpThroughSlot(ship)` | `0x90b6e` → `__requestWarpThroughSlot` `0x90bca` | **One slot per gate pair**, held at the "controller" gate (the first of the pair to ask sets `m18`). It is granted when free and either the cooldown `m16 <= now` has passed or the request comes from the same side as the last holder (`m17`) with the requesting gate's `m3` non-zero. Otherwise −1. |
| `ReleaseSlot(ship)` | `0x90c9c` | Also forwarded to the controller. It frees the through slot with `m16 = now + 60` and the ship's warp-in slot with `m14[i+20] = now + 60`. |
| `2004::ReleaseFromSlot()` | `0xaae41` | `gate.ReleaseSlot(this)`, slot fields reset, `SA_SetShipDockPortNr(ship, −1)` |

`now` is `g8.GetAge()` = class-402 `m8`, which `UpdateAge` (`0xe497b`) advances by the `TI_GetAbsTime` delta / 1000. The
60 is therefore **60 game seconds** **[m]** arithmetic, **[i]** that the delta is milliseconds of game time.

* **How many at once:** up to 20 capital ships per gate hold a warp-in slot (approach, rotate, wait). Only **one** per
  gate pair is past the through-slot gate at any time **[m]**.
* **How a ship waits:** for a warp-in slot, `SA_Idle(ship, 1000 + rand(10000))`. That is **not** a stop. Order 0x1d
  re-plans to a wander point ≥ 1,110,000 units (2.2 km) ahead along the heading, at a world y in ±1,110,000 (+5,000,000
  for capital types), clamped to the sector, at `vmax/2` (`0x00453e64`, `0x00453ec1`, `0x00453eea`, `0x00453fb8`).
  So the queued capital cruises on for 1–11 s per poll. For the through slot, `TI_WaitRandomForInterrupt(1000,
  10000)` with **no order**: the ship sits stopped at its grid point (`2·(r_gate + r_ship)` in front: 4.9 km for the M2 median, 4.5 km for TL **[i]**), rotated to the gate axis (order 4 zeroed its
  speed; nothing restarts it), and polls every 1–10 s **[m]** calls, **[i]** the argument meaning from the name.
* **Release:** at the exit of `__EnterHyperspace`, i.e. after `SA_WarpObjectAway` has run its 900 ms. Also on the
  reset paths (attack → 7, order-5 result 7, re-entering the sector). Each released slot is closed for 60 s. The
  through slot is exempt when traffic keeps the same direction **[m]**.

### 10.3 Q2: approach geometry and order 4 [m]

* **Non-capital ships:** `SA_FlyObjectBeforeContainer(ship, gate, −1, 0)` (`0x00460cae`). The gate has
  `[gate+0x40] |= 0x100000` (`0x00440e95`), and port < 0 takes `0x00460dd5`. That path rotates the gate's approach
  vector `[gate+0x50]+0x1a0 = (0, −0.8, −1.0)` (16.16, written for every class-0x12 object at
  `0x00440e75..0x00440e8b`) by the gate frame, scales it by `r` and adds the gate position. Order 4 then reads this point from
  `flight+0x240` (port < 0, `0x00454a20`). **Every non-capital ship gets the same point A** **[m]**.
* **Capital ships:** `SA_FlyObjectBeforeContainerPos(ship, gate, GetWarpInPos(slot))` (`0x0046533c`, port −1),
  one grid point per slot (§10.2).
* **Arrival:** `dist(ship, A) <= 3·r/4` for a gate target (`0x00454b1a`, compare `0x00454b70`). The test sits inside the
  re-plan gate (`now − flight+0x210 >= 0`), so it is checked every `rand(50·type[+0x50])` ms. On arrival:
  `flight+0x250 = −1`, order 0, **`[obj+0x14] = 0` and `[obj+0x10] = 0`** (`0x00454b91` `89 43 14 89 43 10`), wake with 1.
* **Speed law to a gate [m]:** desired = vmax beyond `d_slow = 3r/4 + vmax²/a` (`0x00455014`). Inside it,
  `ramp(dist; 0 → 5000, d_slow → vmax/2)` (`0x00455052`), floor `container speed + 5000`. Inside
  `d_slow + 3·r + 2·r_ship` the result is scaled by alignment: `ramp(forward·dir; 0.7 → 500, 1.0 → desired)`
  (`0x004550c7`), so a ship more than 45.6° off the point slows to 1 m/s. "Large" ships are capped at 50000
  (`0x004550ed`). The container detour of §3.4 applies to the gate too.
* **Crossing speed at the 3r/4 stop (model, axial approach, Mayhem medians) [i]:** M5 45, M3 49, TS 37, M6 37 m/s
  vanilla. With the installed steering law (`d_arr = 0.75 r`, floor 10 m/s), 10 m/s. Either way it is then zeroed.

### 10.4 Q3: `RotateToAngles` is order 0x1f, not 6 TurnTo [m]

* `SA_RotateToAngles(ship, α, β)` (`0x004653d1`): order 0x1f, `flight+0x240/+0x244` = the gate's yaw/pitch
  (`SA_GetRotAlpha/Beta(gate)`). With three arguments `+0x248 = 0x80000000` (no roll target, `0x00465466`). The case
  writes no speed.
* Handler `0x0045920e`: target direction from the angles. While `forward·target < 0xfd70` (cos 8.1°, `0x004592d9`) it
  steers `0x0044ccc0` at `position + direction`. Once inside, with no roll target, the order ends with result 1.
* **The ship is stationary throughout:** order 4 left `[obj+0x14] = [obj+0x10] = 0`, and neither the case nor the
  handler raises them. The turn runs at the leaf's rates. Only capital ships reach it (§10.1). `SA_RotateToAngles` has
  one KC call site, in `__EnterHyperspace` (stock `0xbce63`, Mayhem `0xd5622`, `kc_movement_calls.txt`), so in
  `x3story.obj` order 0x1f always means "align before a gate". Other KC objects were not scanned.
* The installed steering module passes order 0x1f through to the original leaf (§1.4 of the steering design).

### 10.5 Q4: order 5 to a gate, and the 900 ms of order 8 [m]

**Pass point (`SA_FlyObjectIntoContainer` `0x0046082e`, class-0x12 branch `0x004608de`).** A capital ship (`0x0043fd50`)
gets the **gate origin** (`0x00460c1a`). Any other ship gets θ = `rand(0x4000) − 0x2000` (`0x004608f7`, ±45°) and the
gate's pass vector `[gate+0x50]+0x190 = (0, y0, −0.01)` with `y0 = −0.55`, or −0.05 for subtypes 5..8 (`0x00440e37`,
`0x00440e4f`, `0x00440e5b`, `0x00440e68`). From these it computes `(−y0·sin θ, y0·cos θ, z0)`: at `0x004609d5..0x004609e4`
the first `MulFix(y0, sin θ)` (`0x004609db`) is negated and stored through `[esp+0x20]` with **four** pushes live, i.e.
the local **x** (`[esp+0x10]`), and the second `MulFix(y0, cos θ)` (`0x004609e8`) lands in the local y (`[esp+0x14]`,
`0x00460a01`); z keeps the pass vector's −0.01. It scales by `r`, rotates by the gate frame (rows) and adds the
position. The rendezvous `flight+0x230` is the same point. So θ moves the target on a radius-0.55 r arc **on the gate
disc** within ±45° of straight down (Run154: 91 of 91 transitions at hypot(x, y) = 0.544–0.556 r, z = −0.01 r;
corrected 2026-10-12, the earlier reading had the arc in the y–z plane). A new θ is drawn each time the KC
re-issues order 5 **[m]**.

**Order 5 handler `0x00453fc2`, gate path:**

| Step | Rule | Site |
| --- | --- | --- |
| steer | `0x0044ccc0` toward B every frame (no re-plan gate) | `0x00454745` |
| misaligned | `forward·dir < 0.95` (18.2°) → desired **500** (1 m/s), gate flagged `0x10000000` | `0x00454897`, `0x004548a5` |
| aligned | gate `[+0x40] |= 0x2000000`; desired = `ramp(dist; r/5 → vmax/2, r → vmax)` from `0x00450980` directly | `0x004548cf`, `0x004548f5`, `0x00454903`, `0x00454977` |
| arrival | `dist <= (|v|/10 + r_ship)·(1 + (tf − 1)/4)`, tf = `[*0x606f34+0xcc]` (time factor **[i]**) | `0x0045430c`, `0x00454323`, `0x004546e3` |
| on arrival | **`[obj+0x14] = 0`, `[obj+0x10] = 0`**, port −1, order 0, result 1 | `0x00454700`, `0x00454713` |

* **The 75000/50000 docking caps do not apply to gates.** They bound a local copy of the max speed that only the
  non-gate branch uses (`[esp+0x2f4]` at `0x00454969`); the gate branch calls `0x00450980` itself. This corrects
  ship-movement.md §9 ("a ramp around half its max speed"): the ramp runs from vmax at `r` down to vmax/2 at `r/5`.
* **What ends it:** the arrival test above, not `"CanWarp"`. `0x0044d4c0` raises `"CanWarp"` only with
  `[ship+0x40] & 2` or manual control (§4). Whether AI ships ever carry bit 2 was not established.
* The KC sets `m45 = 0xA0001` before order 5, so `CollisionWarn` takes its "ignore" branch for the whole leg.
* **Order 8 (`0x004551fe`):** `SA_WarpObjectAway` stores the current position in `flight+0x240` and sets
  `+0x214 = now + 900`. Each frame until then the handler sets `[gate+0x40] |= 0x2000000` and `[ship+0x40] |= 0x400`,
  takes `t` = elapsed ms (`0x00455260`), `s = t²·1665000/1000000` units (`0x0045526f`, `0x00455278`), and writes
  `position = start + s·(gate row 2)` (`0x00455291`) with `[obj+0x14] = [obj+0x10] = 0` (`0x00455330`). That is
  a kinematic launch from rest along the gate axis: 2697 m in 0.9 s, ending at 5994 m/s. Then order 0, flag `0x400`
  cleared, the task wakes, KC releases the slots and calls `Remove()`. The KC's `SA_SetDesiredSpeed(ship, 0)`
  before it (`0xbcef7`) is redundant with the handler.

### 10.6 Q5: time from the approach point to the warp [i]

Small ship, from rest at A (vanilla engine laws, Mayhem medians, `r` = 1105 m, rotation at the full yaw rate, so turn
times are lower bounds; `gate_timeline.txt`):

| Class | KC hand-overs | Turn at 1 m/s (arriving 90° off) | Order 5 flight | Speed zeroed at B | Order 8 |
| --- | --- | --- | --- | --- | --- |
| M5 | 2 × ≥ 1 frame | 0.5 s | 6.5 s | 271 m/s | 0.9 s |
| M3 | 2 × ≥ 1 frame | 0.8 s | 9.0 s | 134 m/s | 0.9 s |
| TS | 2 × ≥ 1 frame | 4.0 s | 15.6 s | 62 m/s | 0.9 s |
| M6 | 2 × ≥ 1 frame | 2.2 s | 12.1 s | 93 m/s | 0.9 s |

`|AB|` is 814–1601 m depending on θ. The bearing A→B lies 13.2–33.9° off the gate axis toward local +y. For 31 %
of the θ range it exceeds 18.2° even for a ship that arrives exactly along the axis, so a turn at 1 m/s happens
at the start of order 5 even in the best case.

Capital ship (M2 / TL medians): `RotateToAngles` from 90° ≥ 7.3 / 6.1 s. Then the through-slot wait: 0 when free,
otherwise 1–10 s polls up to the 60 s cooldown on a direction change. Order 5 to the origin from slot 0 takes
39.2 / 37.3 s and stops dead at 104 m/s. From grid slots 13–19 (M2) or 8, 9, 11–19 (TL) the origin is more than
18.2° off the axis the ship was just rotated to, so order 5 starts with a second turn at 1 m/s.

### 10.7 Q6: causes of the clunk, and the smallest engine-side change

| # | Cause | Evidence | Side | Reachable by the installed hooks? |
| --- | --- | --- | --- | --- |
| 1 | Hard stop at A (`[obj+0x10] = 0`) | `0x00454b91` | engine | No: site B lowers `[obj+0x14]` only and runs after; a new 6-byte site is needed (P2) |
| 2 | All small ships target the same A, with `CollisionWarn` live (`m45 = 0`) | `0x00460dd5`, KC `0xbcb56` | engine point, script mode | Partly: site A can steer to a different point without touching `flight+0x240`, and site E suppresses warns for flyable detours. The order still ends at A |
| 3 | Turn at 1 m/s at the start of order 5 (arrival heading ≠ bearing to B) | `0x00454897`/`0x004548a5` | engine | Indirectly: site A can make the ship arrive heading toward B (P1); B cannot raise 500 |
| 4 | Re-acceleration from 0 over ~1.1 km | follows from 1 | engine | with P2 |
| 5 | Hard stop at B, then a launch from rest | `0x00454700`, `0x00455330`, `0x0045526f` | engine | No: new sites P4/P5 |
| 6 | Installed arrival ramp: 10 m/s at `0.75 r` before stop 1 | `ship_steering.cpp` order-4 `d_arr` for class 0x12 | mod | Yes: policy change (P1) |
| 7 | Capital: warp-in queue with `SA_Idle` wander, single through slot with 60 s cooldown and 1–10 s polls, stationary `RotateToAngles`, second in-place turn toward the origin | §10.2–10.4 | script (queue, cooldown, polls); engine (rotate, turn) | Queue and cooldowns: no. Rotate and turn: only by new sites |
| 8 | Random delays | `TI_DelayRandom(2000, 3000)` only on failed transits; the `SA_Idle`/wait timers only for capitals | script | not a cause for small ships |

**Smallest change that makes the small-ship approach continuous (P1 + P2) [i] design, [m] sites:**

* **P1 (no new site; steering-module policy for order 4 with a class-0x12 target):** drop the arrival ramp for this
  case and pass the vanilla speed through. Steer the leaf at a lead-in point, not at A, so that the ship crosses
  `0.75 r` from A heading 23.6° above the gate axis in the gate's y–z plane: the middle of the 13.2–33.9° bearing spread
  of B. That leaves ≤ 10.4° to any B, below the 18.2° turn threshold. The lead-in point goes through the existing
  ring-buffer redirect. `flight+0x240` stays A, so order 4's own arrival test is unchanged. Inputs: target
  `flight+0x224`, class word `+0x48 == 0x12`, subtype `+0x4a`, `[gate+0x70]+0x30/+0x40..` (position, frame rows),
  `[gate+0xa4]`, `[gate+0x50]+0x190/+0x1a0`.
* **P2 (new site `0x00454b91`):** skip the `[obj+0x10] = 0` write when the order-4 target is a gate and the ship
  is not a capital. Keep `[obj+0x14] = 0`: order 0 runs for the one or more frames until the KC issues order 5, so
  the slew decelerates at `a` for that time only. Order 5 then starts at the crossing speed and, when aligned, ramps
  toward vmax.
* Capital ships keep the stop in P2: their next step, `RotateToAngles`, sets no speed and expects a stationary ship.
* **Second step (P4 + P5), for the stop at B:** at `0x00454700`, record `|v|` for a gate target (and optionally keep
  it). At `0x0045526f`, replace `s = 1.665·t²` with `s = v0·t + 1.665·t²`. The launch then continues the arrival speed
  along the axis instead of restarting from rest. The kink between the arrival heading and the axis remains
  (13–34° for small ships).
* **What the mod cannot fix without the script:** these all live in KC classes 2004/2014, shared by stock and
  Mayhem. A script patch, or an interception of the `SA_` calls at the dispatcher, could change them:
  * the capital slot queue: 20 warp-in slots, one through slot per gate pair, the 60 s cooldowns, the 1–10 s polls;
  * the `SA_Idle` wander while queued;
  * `RotateToAngles` as a separate blocking step;
  * the per-step blocking hand-over (≥ 1 frame each);
  * `m45 = 0` (`CollisionWarn` active) on the leg to A.

  Engine-side, the rotate step can be shortened by loosening the 0xfd70 tolerance at `0x004592d9`. Order 0x1f is
  issued only by the gate path in `x3story.obj`.

### 10.8 Q7: hook-site facts

All windows are instruction starts in a linear sweep of `.text`, with no direct branch into their interior bytes
(`check_sites.py`). The only nearby join, `0x00454710` (from `0x00459191`, order 0x20), lies after the P4 window.
All run on the game thread inside `0x00452ad0` with no script call below them. The x87 contract of §6 applies.

| Site | Window | Live in / out | Stub contract |
| --- | --- | --- | --- |
| P2 `0x00454b91` | `89 43 14` / `89 43 10` (6 B, two instructions), then `call 0x00460560` | EAX = 0, EBX = ESI = obj, ECX = flight; `push 1` already on the stack; flags dead | write `[ebx+0x14] = eax`; write `[ebx+0x10] = eax` unless `[[ebx+0x50]+0x224]` has class word 0x12 and the record class code is not capital; preserve EAX/ECX/EDX/ESI; ESP exact; `jmp 0x00454b97` |
| P4 `0x00454700` | `89 43 14` / `89 43 10` (6 B) | EAX = 0 (used at `0x00454717` for the order word), EBX = obj, ECX = flight; flags dead | as P2; record `[ebx+0x10]` per ship for P5; `jmp 0x00454706` |
| P5 `0x0045526f` | `b8 e8 67 19 00` (5 B) | EAX = t (ms), ECX = t², `[esp+0x10]` = t²; EDX/ECX are overwritten at `0x00455281`/`0x00455291` | EAX = `v0·t/1000 + t²·1665000/1000000` in units; ESP exact; `jmp 0x00455281` (skips the `imul`/`idiv`) |
| P3 (optional) `0x004548a5` | `c7 43 14 f4 01 00 00` (7 B) | EAX = container (`[esp+0x2ac]`), EBX = obj; flags dead (`or` follows) | write a cornering speed instead of 500 when EAX has class 0x12; `jmp 0x004548ac` |
| order-0x1f tolerance `0x004592d9` | `3d 70 fd 00 00` (immediate) | flags live into `jl` | immediate patch only, no stub |

P1 uses the installed site A as it is. It adds one class-word test per order-4 steering call and a few multiplies
for gate targets.

**Still inferred or open:**

* the resume latency of a woken KC task (§7);
* the meaning of `TI_WaitRandomForInterrupt`'s arguments;
* the gate's `m3` in the same-direction exemption;
* the time-factor reading of `[*0x606f34+0xcc]`;
* whether AI ships carry `[ship+0x40] & 2`;
* the order-5 result 7;
* whether M7 derives from KC class 2019;
* the `m41 == g8` reading (owned by the player), which differs from the §3.2 reading ("the player's ship"); both
  are **[i]**;
* ~~the visual size of the hole relative to the 0.55 r pass offset~~: resolved in §10.9;
* every timing in §10.6 (a model, not flight data).

A flight row that settles the model:

* at `0x00454b91` and `0x00454700`: ship, class code, `[obj+0x10]` before the write, target class;
* at the `SA_` dispatcher, ids 0x09/0x0a/0x0c/0x0e/0x1d: frame number.

These give the real crossing speeds and hand-over gaps per class.

```sh
python3 -I verification/results/ship-navigation-ai/gates/check_sites.py     # RESULT PASS (73 checks)
python3 -I verification/results/ship-navigation-ai/gates/kc_gate_flow.py    # KC pseudo-statements, stock + Mayhem
python3 -I verification/results/ship-navigation-ai/gates/gate_timeline.py   # model timings [i]
python3 -I verification/results/ship-navigation-ai/gates/gate_hole.py       # §10.9, RESULT PASS (41 byte strings), ~40 s
```

### 10.9 Gate hole geometry

2026-10-12, static study of the same `X3AP.exe` and of the installed catalogues (stock view = the pinned AP set
`01–13.cat` + `addon/01–04.cat`; installed view = every catalogue plus loose files). Nothing was launched.
[`gate_hole.py`](../../verification/results/ship-navigation-ai/gates/gate_hole.py) re-reads the 41 EXE byte strings
quoted below (`RESULT PASS`). It resolves every TGates scene and body, applies the radius rule of
[game-scale-and-ship-classes.md](game-scale-and-ship-classes.md) §2 with a fresh body cache per view, applies the
collision-tree rules below to every part, and measures the meshes. Its output is
[`gate_hole.txt`](../../verification/results/ship-navigation-ai/gates/gate_hole.txt). Raw decompiles stayed in the
session scratchpad. Gate-local frame as in §10: z is the gate axis (row 2), and every gate scene part has zero rotation
keys **[m]**. A mesh point is `point × LOD-0 value / 65536 + part offset` **[i]** ([body-format-bob1.md](body-format-bob1.md)
§3). **Hole `h`** = the clear coaxial radius: the smallest distance from the axis `x = y = 0` to any triangle of the
collision mesh projected along z, over the whole axial extent. A ship that moves parallel to the axis with
`lateral + r_ship ≤ h·r` never touches the mesh.

**Answer.**

* **Installed ordinary gates (TGates rows 0–3 and 5–14, which includes subtypes 5..8): there is no ring.** The Mayhem
  scene `others\terraformer_gate_active_scene` (`addon/06.cat`) has one part, `others\terraformer_gate_effect`
  (`addon/06.cat`, `.bob`), at `(0, 0, 47500)`. That part is a blended `effects.fx` **sphere shell**: every vertex lies at
  0.914 r (504,774 units, 1010 m) from its centre, and the centre is 0.086 r along +z. It has no colliding group, so it
  gets no collision tree, and a ship can never make contact with these gates **[m]**. The stock ring bodies are
  overridden by 28-byte comment-only stubs (`addon/11.cat`, "NoAdsign-mod"), and the Mayhem scene does not reference
  them anyway. **Physical hole: unbounded.** Visual bound: the bubble's section in the plane z = 0 has radius 0.910 r.
  `r` = 552,274 units = **1104.5 m** **[m]**.
* **Installed Terran gates (rows 21–24, `patch20\terran_gate_scene`): the only installed ring.** `h` = **0.663 r**
  (2125 m), with `r` = 1,603,754 units = 3207.5 m **[m]**.
* Installed row 4 (Xenon, blended disc only) and rows 15–20 (inactive hubs, whose body is an empty stub) have no
  collision mesh. The pair loop never raises `"CanWarp"` for subtypes 4 and 0xf..0x14 (`0x0045d73b..0x0045d77b`) **[m]**.
* Stock AP, for reference: Argon ring (rows 0–3) **0.605 r**, terraformer ring (9–20) **0.607 r**, Terran ring (21–24)
  **0.654 r**, catapult (5–8) a tube of inner radius **0.127 r**, destroyed gate (4) no hole **[m]**.
* Consequence for amendment 8: `kGateHoleFraction = 0.55` and the 0.5 r chord bound are conservative in the installed
  game. The ordinary gates impose no physical limit. On a Terran gate a chord crossing at ≤ 0.5 r, 34° off the axis,
  with r_ship = 140 m still clears the rim by 0.026 r (83 m) **[i]** (arithmetic below).

**Q1: geometry per TGates family** (`r` from the radius rule; hole, outer radius and axial extents from the collision
mesh; the rim band is the triangles within 0.05 r of the hole, i.e. the inner lip's axial thickness) **[m]**:

| View | Rows | Scene (catalogue) | Collision part: body (catalogue), LOD-0 value, collision LOD | `r` units / m | Hole `h` | Outer | Body z extent | Rim band z |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| installed | 0–3, 5–14 | `terraformer_gate_active_scene` (addon/06) | none: `terraformer_gate_effect` (addon/06) is blended, sphere 0.914 r centred at z = +0.086 r | 552,274 / 1104.5 | unbounded (visual 0.910 r at z = 0) | (0.914) | (−0.828..+1.000) | — |
| installed | 4 | `argon_gate_scene` (addon/06; ring part commented out) | none: `argon_gate_effect` (addon/11, 1,449,877) is a blended disc | 1,449,877 / 2899.8 | unbounded | (1.002) | (−0.091..+0.133) | — |
| installed | 15–20 | `terraformer_gate_inactive_scene` (01) | none: `terraformer_gate` (addon/11) is an empty stub, no model | 47 (root only) | — | — | — | — |
| installed | 21–24 | `patch20\terran_gate_scene` (07) | `patch20\terran_gate` (addon/11 `.bob`, 1,601,122, 1 LOD → LOD 0; material 3 blended, out) | 1,603,754 / 3207.5 | **0.663 r = 2125 m** | 1.026 r | −0.498..+0.179 r | −0.116..+0.138 r (817 m) |
| stock | 0–3 | `argon_gate_scene` (01) | `others\argon_gate` (01, 678,369, LOD 4 of 5; its 12 materials are opaque) | 681,001 / 1362.0 | **0.605 r = 824 m** | 0.996 r | −0.648..+0.655 r | −0.121..+0.130 r (342 m) |
| stock | 4 | `argon_gate_destroyed_scene` (01) | `argon_gate_destroyed` (01, 1,248,973, its only LOD) | 1,248,973 / 2497.9 | 0 (debris across the axis) | 1.013 r | −0.411..+0.246 r | — |
| stock | 5–8 | `terran_catapult_gate_scene` (01) | `terran_catapult_gate` (01, 1,797,099 at z = +1,793,560, LOD 3 of 4) | 3,590,659 / 7181.3 | **0.127 r = 912 m** (tube interior) | 0.169 r | −0.001..+0.539 r (visual to +1.000) | tube length |
| stock | 9–14, 15–20 | `terraformer_gate_active_scene` / `_inactive_scene` (01) | `others\terraformer_gate` (01, 679,472, LOD 3 of 4) | 679,472 / 1358.9 | **0.607 r = 825 m** | 1.000 r | −0.651..+0.300 r | −0.124..+0.125 r (339 m) |
| stock | 21–24 | `patch20\terran_gate_scene` (07) | `patch20\terran_gate` (07 `.pbb`, 711,610, LOD 2 of 3; material 3 out) | 714,242 / 1428.5 | **0.654 r = 934 m** | 1.024 r | −0.495..+0.180 r | −0.111..+0.120 r (330 m) |

* **The radius cross-check.** The installed 552,274 units (1104.5 m) is the same rule and the same number as the 1105 m
  of game-scale §6. It is a static estimate. No runtime `[gate+0xa4]` is on record: the Run152 `ship_gate_approach`
  rows predate `offset_m`, which is 0.25 r. The rule itself was validated once, on a station (9,205,962) **[m]**.
  **Correction:** the stock gate radii in game-scale §6 (2999, 2995, 3208 m) were computed with the installed bodies'
  cached values (`scene_radius._BV` is keyed by name across views, and `map_stats.py` runs the installed view first).
  The stock values are those in the table above.
* **The hole is centred on the axis.** The largest clear disc anywhere in the projection is at most 0.006 r larger than
  `h`, and its centre lies within 0.008 r of the axis, for all four rings **[m]**.
* **Off the rim, the clear radius grows.** In 0.05 r slabs the Terran ring clears 0.663–0.680 r for |z| ≤ 0.15 r and
  0.72–0.84 r beyond. The stock Argon ring clears 0.605–0.623 r for |z| ≤ 0.15 r and ≥ 0.70 r beyond. The stock
  terraformer ring clears 0.607 r for |z| ≤ 0.15 r and ≥ 0.73 r beyond (`gate_hole.txt`) **[m]**.
* **Visual vs collision.** The visual (LOD 0, all groups) holes are within 0.005 r of the collision holes. The
  exception is the catapult, where two LOD-0 triangles cross the axis at z = 0.022–0.046 r; they are not in the
  collision LOD **[m]**.
* **The subtype 5..8 special case** (pass y = −0.05 r, `"CanWarp"` within 0.14 r, §10.5) fits the stock catapult: a tube
  of inner radius 0.127 r whose mouth sits at the gate origin. In the installed game rows 5–8 use the ordinary
  bubble scene, so only the smaller pass offset and trigger distance remain **[i]**.

**Q2: which parts collide, and is there a mesh test for gates [m]:**

* **Broad phase.** Ship × gate takes the class-7 path of the pair loop (`[obj+0xa4]` box `3·r_sum + 250000`, then the
  float distance against `1.01·r_sum`, [sector-collide.md](sector-collide.md) §3). The filter `0x0045c780` then **skips
  the pair outright when the gate's `[+0x40]` has bit 0x100000** (every class-0x12 object, `0x00440e95`) **and the
  ship's order `[flight+0x258]` is 5 or 0x1e with `[flight+0x224]` = this gate** (`0x0045c7b3..0x0045c7e3`). So an AI ship
  flying order 5 (the pass) or 0x1e (clear after arrival) to a gate cannot collide with that gate at all. Orders 4,
  8 and 0x21 are not exempt.
* **Narrow phase.** A surviving pair goes to `0x0048ac80` → `0x0048a890`, which walks both scene-node trees and runs
  the RAPID mesh test only for a node pair where both nodes carry `[+0x12c] & 0x1000000` (a collision tree) and neither
  carries 0x8000000 (`0x0048a92f..0x0048a95d`). **There is no sphere-only contact**: a pair without trees yields 0 and no
  response (`0x0045e130` runs only on > 0).
* **Which scene parts get a tree.** The scene-part builder `0x0043d1d0` (`0x0043d801..0x0043d8e6`) gives no tree to:
  * a node flagged 0x10004000, i.e. a body in `types/Bodies` SBTYPE_2D (→ 0x4000, `0x004346d0`) or SBTYPE_2D2
    (→ 0x10000000, `0x004346e7`);
  * a JET (`[+0x130] & 1`);
  * a DOCKPOINT body (`0x0043d822`), or body 0x4a56;
  * a part already flagged by the builder.

  Every other part with a body gets `0x0047eb90(node, nLOD − 1)`, the **coarsest LOD record** (record 1 for a class-5
  owner or a `0x0043fd00` ship). A LOD record with flags & 0x10 is built first, all groups, by the body setter
  (`0x00487f10..0x00487f1f`). A body with the BodyData flag (`model+0x50 & 0x40`) gets 0x8100000 (`0x00487eea`). None of
  the gate bodies is in any `types/Bodies` list or has a 0x10 LOD (both views, `gate_hole.txt`).
* **Which groups.** `0x0047eb90` skips parts with flags & 0xa0 (`0x0047ec95`) and groups with `[+0xc] & 1`
  (`0x0047ecb4`). The binary loader sets `[+0xc] = 1` when the material flags have any of **0x486** (DESTINATIONBLEND,
  ALPHABLEND, MULTIPLY, SRCCOLOR; `0x00482adb`). For an effect material the flags come from `0x00481780`: a LONG
  parameter `g_AlphaBlendEnable` (semantic 0x13) ≠ 0 adds 2 unless `g_ALPHATESTENABLE` (0x12) ≠ 0, which adds 1 instead
  (`0x00481a46..0x00481a8e`). So **a blended effect material without alpha test never collides**. When no group
  collides, `0x0046ed00` marks the body slot −1 and no node with that body is ever flagged 0x1000000
  (`0x0047ecde..0x0047ecf5`, `0x0046ed4b`).
* Result: every `argon_gate_effect` and `terraformer_gate_effect` (blend 1, alpha test 0) is non-colliding. The rings
  collide with their opaque groups (Terran material 3 is blended and drops out). The stock Argon ring's blended
  material 1 carries alpha test and is not in the collision LOD anyway. The stock destroyed gate's blended materials
  carry alpha test and collide.
* **`0x0044d9c0` is not a mesh test.** It is the box test that `0x0044dd90` (`CollisionWarn`, §3.1) calls at
  `0x0044e2b5`/`0x0044e3ad`. Per part node with a loaded body it compares the LOD-0 record box (`+0x20..+0x2c`) ×
  node scale, plus the ship radius, × 1.2, against the node distance. It ignores materials, so the Mayhem bubble's cube
  (±0.914 r about z = +0.086 r) is a warn obstacle even though it can never be hit.

**Q3: the safe crossing fraction** (`h` from Q1; installed non-capital ship radius ≤ 140 m, the M6 maximum in
`ships_by_class.json`):

| Family | `h` | Axis-parallel crossing at lateral ≤ | Oblique crossing at α ≤ 34° (rim half-thickness `w`) | Exit spread 0.3 r |
| --- | --- | --- | --- | --- |
| installed ordinary (0–3, 5–14) | none | any (no collision mesh) | any | no contact possible; ends inside the bubble (0.3 r < 0.910 r) |
| installed Terran (21–24) | 0.663 | 0.663 r − r_ship | 0.663 r − r_ship − 0.093 r (`w` = 0.138 r) | clear iff r_ship ≤ 1163 m; margin 1023 m at 140 m |
| stock Argon (0–3) | 0.605 | 0.605 r − r_ship | − 0.088 r (`w` = 0.130 r) | clear iff r_ship ≤ 416 m |
| stock terraformer (9–20) | 0.607 | 0.607 r − r_ship | − 0.084 r (`w` = 0.125 r) | clear iff r_ship ≤ 417 m |
| stock Terran (21–24) | 0.654 | 0.654 r − r_ship | − 0.081 r (`w` = 0.120 r) | clear iff r_ship ≤ 506 m |
| stock catapult (5–8) | 0.127 | 0.127 r − r_ship (inside the tube) | tube length 0.54 r: keep within the tube | 0.3 r is outside the tube (outer 0.169 r) |

* The table values are **[m]**. The oblique bound is conservative **[i]**: the lateral position can change by at most
  `w·tan α` while the ship is inside the rim band.
* For amendment 8 on a Terran gate: a chord crossing at 0.5 r, 34° off the axis, needs 0.5 + 0.093 + 140/3207.5 =
  0.637 r ≤ 0.663 r. The 0.55 r arc check needs `w·tan α` ≤ 0.07 r at r_ship = 140 m, i.e. α ≤ 27° on a Terran gate
  **[i]**.
* **Order 8 launch (vanilla).** It starts at B, whose lateral offset is 0.55·cos θ·r ∈ [0.389, 0.55] r, and runs
  parallel to the axis. It clears the installed Terran ring for r_ship ≤ 0.113 r (362 m). On the stock Argon ring it
  would touch for r_ship > 0.055 r (75 m) **[i]**.
* **Warp-in (exit spread).** `SA_WarpObjectIn` writes `f+0x240 = P` (the ship's position at the call, after the site-W
  offset). It then places the ship at `P + 1,348,650 · row 2` (`0x00465bec..0x00465c0a`), and the handler moves it back
  to `P` along the axis over 900 ms. The path is axis-parallel at the offset lateral distance, so the clear-cylinder
  rule applies over its whole length. 1,348,650 units is 2.44 r for the 1104.5 m gates and 0.84 r for the installed
  Terran gate. Order 0x21 is not exempt from the pair filter; the following 0x1e is.
* With `P` at the gate origin before the offset **[i]**, the arriving ship sits at 0.3 r in the gate plane:
  * on an ordinary installed gate it cannot collide and ends inside the bubble;
  * on a Terran gate it clears the ring by 1023 m at r_ship = 140 m.

**Q4: still inferred or open:**

* The point scale `value / 65536` ([body-format-bob1.md](body-format-bob1.md) §3). Supporting evidence: the Mayhem
  bubble's vertices lie exactly at the LOD-0 value from its centre, and the engine's own pass arc (0.55 r) and
  `"CanWarp"` radii sit just inside the stock holes (0.605 r, and 0.127–0.14 r for the catapult).
* The effect-parameter object layout read at `0x00481a44..0x00481a63`: `+0` semantic, `+0x20` type word (0 = LONG),
  `+0x24` value.
* That a 28-byte comment-only text body loads as no model. The engine's text loader was not traced for this case;
  `bob1.parse` refuses it.
* That named bodies keep the BodyData slot `+0x04` = −2 (default LOD, no "all groups"), per body-format §6.
* The gate root node (TGates column 0: body 0, the 47-unit test body, or 477, missing). It may carry a ≤ 0.1 m
  collision mesh at the origin; not traced.
* The ship's position `P` at `SA_WarpObjectIn`: the sector-entry placement was not traced.
* No runtime `[gate+0xa4]` on record. A Run 153 `ship_gate_approach` row (`offset_m` = 0.25 r) would measure it.
* Whether Mayhem places other colliding objects at its gates (stations, debris from the director or the map) was not
  checked.
