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
