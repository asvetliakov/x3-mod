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
`[*0x006085e4 + 0x34 + slot*0x18](task_id, result)` and clears `[obj+0x7c]`/`[obj+0x80]`. For a call from a
script the slot is the VM's own native group (`[VM+0x1454]`) and the callback is `0x004a4910`. It finds the task in the
VM hash, sets `task+0x20 = 1` and the result cell, and links the task at the head of the runnable list. The task runs
in the next frame's scheduler pass (§10.12 Item 5, corrected 2026-10-14). `0x004604d0`, read here earlier as the SA
callback, is SA's own slot: it marks an engine object (`+0x84`, `+0x8c`) and is used only when the game calls SA
itself.
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
| 0x21 | `SA_WarpObjectIn(ship,gate)` | `0x00465b22` | 0x21 | **writes the position directly** (`phys+0x30..`) to the gate exit; `+0x214`, `+0x218`. On a KC gate arrival order 0x21 is already running (started by the launch `0x00444170`), so the case only waits (§10.10) |
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
through orders 5 and 8. Arrival: the engine's post-pass launch `0x00444170` places the ship and starts order 0x21;
`WarpObjectIn` only waits for it, then `ClearObjectFromHangar` (order 0x1e) moves it clear (§10.10). The
`SA_Distance` < 1750000 loop in `__LeaveHyperspace` belongs to the tow branch only (§10.10).

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

* ~~How a woken task returns to the runnable list.~~ Settled in §10.12 Item 5: the next frame's scheduler pass
  **[m]**.
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
  speed; nothing restarts it), and polls every 1–11 s: TI case 6 waits `arg0 + rand(arg1)` ms (`0x004b13ee`), so
  1–10 s was wrong (corrected 2026-10-12, [gate-queue-script.md](gate-queue-script.md) §3) **[m]**.
* **Release:** at the exit of `__EnterHyperspace`, i.e. after `SA_WarpObjectAway` has run its 900 ms. Also on the
  reset paths (attack → 7, order-5 result 7, re-entering the sector). Each released slot is closed for 60 s. The
  through slot is exempt when traffic keeps the same direction **[m]**, but only when the requesting gate has a native
  object (`gm3 != 0`, the player's sector **[i]**); out of sector every transit waits 60 s. The bytecode, the patch
  sites and the file-level patch options are in [gate-queue-script.md](gate-queue-script.md).

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
  `[ship+0x40] & 2` or manual control (§4). Bit 2 is the player object (`SA_SetAsPlayerObject`), so AI ships never
  raise it (§10.10).
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
* ~~the meaning of `TI_WaitRandomForInterrupt`'s arguments~~: `arg0 + rand(arg1)` ms, object `arg2` (gate-queue-script.md §3);
* ~~the gate's `m3` in the same-direction exemption~~: the requesting gate's `m3`, i.e. in-sector only **[i]** (gate-queue-script.md §2);
* the time-factor reading of `[*0x606f34+0xcc]`;
* ~~whether AI ships carry `[ship+0x40] & 2`~~: bit 2 is the player object, so they do not (§10.10);
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
* **Warp-in (exit spread).** Superseded by §10.10: on a KC arrival the case below is not reached. `SA_WarpObjectIn`
  writes `f+0x240 = P` (the ship's position at the call, after the site-W offset). It then places the ship at `P + 1,348,650 · row 2` (`0x00465bec..0x00465c0a`), and the handler moves it back
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
* ~~The ship's position `P` at `SA_WarpObjectIn`~~: traced in §10.10. The launch `0x00444170` places the ship, and
  site W is not reached on KC arrivals.
* No runtime `[gate+0xa4]` on record. A Run 153 `ship_gate_approach` row (`offset_m` = 0.25 r) would measure it.
* Whether Mayhem places other colliding objects at its gates (stations, debris from the director or the map) was not
  checked.

### 10.10 The transfer trigger, the arrival placement and arrival signals

2026-10-13, static study of the same `X3AP.exe` (SHA-256 `fdbf3418…`) and both `x3story.obj` versions, for the gate
traffic controller. Nothing was launched and no Wine command ran. Every engine fact below comes from capstone instruction
listings over the PE map (raw listings stayed in `~/x3-nav-study/traffic/`).
[`arrival_sites.py`](../../verification/results/ship-navigation-ai/gates/arrival_sites.py) re-reads every quoted byte
string, the SA and order jump-table entries, the two doubles of the exit fan and the proposed hook window
(121 checks, `RESULT PASS`, [`arrival_sites_out.txt`](../../verification/results/ship-navigation-ai/gates/arrival_sites_out.txt)).
[`kc_arrival_flow.py`](../../verification/results/ship-navigation-ai/gates/kc_arrival_flow.py) prints the KC arrival
methods for stock and Mayhem ([`kc_arrival_flow.txt`](../../verification/results/ship-navigation-ai/gates/kc_arrival_flow.txt)).
`r` = `[gate+0xa4]`, `r_s` = the ship's `[obj+0xa4]`, `f` = `[obj+0x50]`, `p` = `[obj+0x70]`, gate rows R, U, F = rows
0, 1, 2 of `[gate+0x70]+0x40` as in §10.

**Answer.**

* **Capital off-axis pass: yes, within a bound.** Order 5 to a gate ends on the distance from the ship's **centre** to
  the **stored order point** `f+0x240` (the gate origin for a capital). The radius is the ship's own,
  `(|v|/10 + r_s)·(1 + (tf − 1)/4)`, not the gate's. There is no plane test and no collision test. A capital whose path
  passes the origin at lateral distance `ℓ` transfers iff `ℓ` is below that radius. At 0.5 r (552 m on a 1104.5 m gate)
  that holds for the M2/M1/TL medians (1337/1170/1135 m) and fails for hulls below 552 m (the M1/M2/M7 minima are
  227/307/216 m, the M7 median 560 m)
  **[m]** rule, **[i]** class numbers. Our steering leaf's point is not what is tested.
* **Small ship off-axis pass: no.** Its `f+0x240` is the pass point B on the lower 0.55 r arc (§10.5), and the radius is
  `r_s + |v|/10` (about 40 m for an M3). Only a pass within that distance of B transfers **[m]**.
* **`"CanWarp"` is not involved for AI ships.** Its bit-2 condition is "the player's ship" (`SA_SetAsPlayerObject`), and
  the alternative is manual player control **[m]**.
* **Capital off-axis placement at W: no; at the launch, yes.** The arrival position is computed by the engine's
  post-pass launch `0x00444170`, which also starts order 0x21 itself. `SA_WarpObjectIn` then finds order 0x21 already
  running at that gate and returns before the placement code, so site W (`0x00465bac`) is never reached on a KC arrival
  **[m]**. Run159 logged 0 W entries in 476 summaries **[t]**. A 6-byte window inside the launch, `0x00444606`, is the
  site where a lateral offset carries through the whole arrival (table below).
* **Arrival signal.** There is nothing for capitals before the arrival run itself. A non-capital polls
  `SA_IsWarpOutSlotAvailable(destination gate)` first. With a free slot it arrives in the same KC run **[i]**. Once the engine
  object exists, the ship is launched within the same main-loop iteration **[i]**, appears 2.7 km behind its exit point
  and reaches it 900 ms later.

#### Q1: what ends order 5 at a gate, and what the transfer is [m]

| Step | Rule | Site |
| --- | --- | --- |
| distance | `d = \|f+0x240 − p+0x30\|` each frame (`0x00412490`: `[arg] − [ecx]`, length `0x00469b80`) into `[esp+0x1c]` | `0x0045413e`, `0x0045414f`, `0x00454182` |
| leaf target | a stack copy of `f+0x240` (`[esp+0x4a0]`) goes to the leaf `0x0044ccc0` | `0x00454192`, `0x00454733`, `0x00454745` |
| radius | `t = (\|[obj+0x10]\|/10 + r_s)·(1 + (tf − 1)/4)`, tf = `[*0x606f34+0xcc]` 16.16 (time factor **[i]**) | `0x00454303..0x00454368` |
| test | gate target → `d <= t` (`setle`), evaluated on every frame | `0x00454384`, `0x004546e3` |
| end | speeds 0, order 0, result 1 (P4 window of §10.8) | `0x00454700` |

* **The transfer is script-driven.** Result 1 wakes `__EnterHyperspace`, which issues `SA_WarpObjectAway` (order 8,
  900 ms kinematic launch along +F from wherever the test fired, §10.5). The exit path then runs `ReleaseFromSlot` and
  `Remove()`, and `__FlyToNextSector` calls `__LeaveHyperspace` in the destination (§10.1) **[m]**.
* **Leaf point versus stored point.** Site A (and any replacement leaf target) changes only the copy at `[esp+0x4a0]`.
  The test reads `f+0x240`, so the module can route the ship anywhere without moving the trigger **[m]**.
* **Where a capital stops.** It stops at `d = t`, not at the plane: for an M2 median at 104 m/s, 1347 m from the origin
  on the axis **[i]**. At lateral `ℓ` the test fires `√(t² − ℓ²)` before the plane (1229 m at ℓ = 552 m). Order 8 then
  starts there and crosses the plane at `ℓ`. The ordinary installed gates have no collision mesh. On a Terran ring,
  `ℓ + r_s ≤ 0.663 r` must hold (§10.9) **[i]**.
* **If `ℓ ≥ t`** the centre passes the plane without arrival. Order 5 keeps steering at the origin, and the 500 units/s
  misaligned branch of §10.5 turns the ship back **[i]**.
* **`"CanWarp"` (`0x0044d4c0` → 3), the player's path only.** The pair loop (`0x0045d727`, centre distance from
  [sector-collide.md](sector-collide.md)) calls it for a ship against a class-0x12 object (`0x0044d4fe`). It requires:
  * `[ship+0x40] & 2` (`0x0044d50c`) or `0x0043ff80` (`& 0x40000` and not autopilot `0x80000`, `0x0043ff83`/`0x0043ff8a`);
  * not order 0x21 (`0x0044d523`);
  * centre distance `< r` (`0x0044d531`) and `< k·r` with k = 0.7, or 0.14 for subtypes 5..8 (`0x0044d5de`/`0x0044d5e8`/`0x0044d606`);
  * the velocity `p+0x100 + [obj+0x60]` (`0x0044d6b8`) heading toward the plane, i.e. the same sign as the axial offset
    `(G − S)·F`;
  * `|(G − S)·F| <= 0.75 r_s + (|v|/10 + 0.9·|0.01 r|)·(1 + (tf − 1)/4)` (`0x0044d618`, `0x0044d64a`, `0x0044d703`).

  **Bit 2 is the player object:** the only immediate writers in `.text` are `0x0044ada1` (`|= 0x40002`) and
  `0x0044ae93` (`&= ~2`), both inside `SA_SetAsPlayerObject` (case `0x00463de6`). KC `2004::Activate` calls it with
  `this == g9`. AI ships never carry the bit, which closes the §10.8 open item **[m]**.

#### Q2: the arrival placement path

**Sequence [m]** (KC statements from `kc_arrival_flow.txt`, stock offsets; Mayhem `__LeaveHyperspace` d592c differs only by a
`m58 = TI_GetAbsTime()` stamp):

1. **`__LeaveHyperspace`** (bd16d) sets the KC position `m0` to the gate's KC position and `m0[6..8]` to
   `−gate.GetDirectionVector()`. When the destination gate has an engine object and the ship has none, it sets
   `m1 = gate` and calls `Activate()`. Because `m1` is a gate, not a sector (2001), `2004::Activate` calls
   `SA_AllocObject(7, …)` and **`SA_MoveObjectIntoContainer(m3, gate)`**. It does not call `SA_StartObjectInSpace` or
   `SA_SetPos[Safe]AndDirection` (aedbd..aee2f). `0x004497b0` links the ship into the gate's class-7 list and sets
   `[obj+0x54] = gate`; it writes no position (`0x004498f9`).
2. **`SA_StartInHangar(m3, gate, −1)`** (bd2b8, case `0x004626e2` → core `0x00443920`):
   * sets `f+0x250 = −1` and clears the flight mode;
   * a **non-capital** (`0x0043fd50` false, `0x00443a93`) takes a random free entry among **6 warp-out slots** (7 for the
     player ship) in the gate's flight block: occupant `[gate.f+0x1dc+4i] = ship`, expiry
     `[gate.f+0x1c0+4i] = now + 20000` (`0x00443b3d`, `0x00443b53`);
   * with no free slot it returns 0, which becomes KC result 10, and the KC retries after `TI_Delay(1000)` (bd2fa).
     Capitals take no slot;
   * then `[obj+0x40] |= 0x80` and core result 1 (`0x00444157`, `0x00444160`). The case leaves the task **waiting**
     (`0x0046272e` → `0x00460e92`) with no order set.
3. **The launch `0x00444170(ship, gate)`.** The sector post pass `0x0045b720` walks every gate's contained class-7 list
   `[gate.f+0x54]` and launches each ship with flag 0x80 (`0x0045b777..0x0045b797`). The post pass runs after the order
   interpreter in pass A (`0x0043a395`). In the main loop the KC scheduler (`0x00403aff`) runs before the sector update
   (`0x00403b17`). The launch:
   * sets `[gate.f+0x180] = now + 10000` and order 0;
   * computes the **exit point P** in the gate frame (table below) and writes `p+0x30 = P`;
   * builds the orientation as the gate matrix times angles (0x8000, 0, 0) (`0x00444594`, `0x0044459d`, `0x004445b4`):
     a 180° yaw, so the ship faces −F, out of the gate face **[m]** angle, **[i]** sign;
   * writes **order 0x21 itself**: `f+0x258 = 0x21`, `f+0x218 = 1`, `f+0x214 = now + 900`, `f+0x240 = P`
     (`0x00444612`), `[obj+0x40] |= 0x400`, `p+0x30 = P + 1,348,650·F` (2697 m behind the plane side it exits from),
     `f+0x224 = gate`;
   * sets speed 120000 (240 m/s), or 60000 for a capital, and desired 20000 clamped to vmax (`0x004448b3`, `0x004448c0`);
   * moves the ship into the sector through `0x00449510`, the routine `SA_StartObjectInSpace` uses (`0x00462066`), and
     clears 0x80;
   * only for the player's capital (bit 2), runs `0x00443620` on class-7 ships within the Chebyshev distance
     `2·(r_a + r_b)` (`0x00444906..0x004449fb`, not traced);
   * **wakes the KC task with 1** (`0x00444a32`).
4. **Order 0x21, 900 ms** (`0x0045533b`). Each frame it writes `p+0x30 = f+0x240 + 1.665·u²·F` with `u` = ms left, sets
   0x400 and **zeroes `[obj+0x14]` and `[obj+0x10]`** through the shared order-8 tail (`0x00455330`). At the deadline it
   snaps `p+0x30 = f+0x240` (`0x0045537c`), clears 0x400, sets order 0 and wakes with 1. The ship therefore ends at P
   **at rest, facing −F**.
   * The launch frame's integrator (pass B) still moves it one step at the launch speed before the handler's absolute
     writes take over **[i]**.
5. **`__LeaveHyperspaceWarpIn`** (bd671), in the resumed task:
   * calls `SA_SetDesiredSpeed(0)`, then **`SA_WarpObjectIn(m3, gate)`**. The case finds `f+0x258 == 0x21` and
     `f+0x224 == gate` and jumps to `0x00465c9c` (`0x00465b68`). It only registers the waiting task: **no placement, and
     site W is skipped**;
   * then calls **`SA_ClearObjectFromHangar(m3, gate, −1)`** (order 0x1e; port < 0, so no exit point, `0x00461236`);
   * then `m45 = 0` and, if flagged, `SA_AvoidObjectCollision`. `__LeaveHyperspace` returns 3 right after. Its
     `SA_Distance < 1750000 + 2·size` loop (bd49a) belongs to the `m80` (tractor tow) branch only, which corrects §4.
6. **Order 0x1e at a gate** (`0x00457bc3`):
   * `dz = (S − G)·F` (`0x00457c5e`);
   * the order ends with result 1 once `|dz| > r` for the "large" set `0x0043fd00` {TL, M0, M1, M2, TL_P, M6, M7, TM, M8},
     or `|dz| > r/4` for the others (`0x00457c7e`, `0x00457c89`);
   * until then it steers at `S + F·(dz ∓ r)`, a point on the ship's **own** axis-parallel line, so the lateral offset is
     kept;
   * it forces the speed to 60000 (large) or 120000 every frame and the desired speed to vmax (`0x00457d48`, `0x00457d5c`).
   The ship jumps from rest to 120 or 240 m/s at the first 0x1e frame.

**Exit geometry [m]** (local `(x, y, z)` in R, U, F; rotated by `0x004f0da0`, the helper order 5 uses for B):

| Ship | Local exit point P | Sites | Installed ordinary gate (r = 1104.5 m) **[i]** |
| --- | --- | --- | --- |
| non-capital | `(y0·sin φ·r, −y0·cos φ·r, z0·r − r_s)` with the pass vector `(0, y0, z0) = [gate.f+0x190..]` (y0 = −0.55, or −0.05 for subtypes 5..8; z0 = −0.01) and `φ = (i − 3)·65536/24` (15° steps) from the ship's warp-out slot index i | `0x00444335..0x004444f2`, doubles `0x005657b8` = 1/24, `0x005654c0` = 65536 | a fan on the **upper** 0.55 r arc (607 m from the axis) at φ ∈ {−45, −30, −15, 0, 15, 30}° (+45° player), 159 m apart, `11 m + r_s` in front of the plane |
| capital (`0x0043fd50`) | `(0, 11100 units, z0·r − r_s)`: on the axis, 22.2 m up | `0x00444506..0x0044458d` | M2 median: 1348 m in front, appearing 1349 m behind the plane |

The sin/cos tables at `0x005b6998`… are filled at run time. That they hold sin and cos is read from the quadrant
structure of the lookups (`0x00444374..0x0044442c`) **[i]**. Every capital arrival at one gate uses the same P **[m]**.
Consecutive capitals are spaced only by the departure side's through slot (§10.2).

**What depends on P [m].**

| Follow-on state | Depends on the position? | Evidence |
| --- | --- | --- |
| orientation | no: built from the gate matrix before P is copied | `0x00444590..0x004445cd` |
| order 0x21 path and its end point | derived only from `f+0x240`, copied from `p+0x30` after the site | `0x00444612`, `0x0044463a..0x004446bf`, `0x0045537c` |
| order 0x1e | keeps the lateral offset (axis-parallel exit); its end test uses `dz` only | `0x00457c5e..0x00457d2e` |
| KC `SA_WarpObjectIn` / waits | order and target only | `0x00465b68` |
| warp-out slots | ship pointer and a 20 s timer, released by the post pass (`0x0045b7c7..0x0045b7d2`); capitals hold none | `0x00443b3d`, `0x00443b53` |
| KC `m0` | read back by `SA_GetPosRotAndDirection` right after the launch (bd375); no KC test re-centres | `kc_arrival_flow.txt` |
| safe-position search | not on this path (`m1` is the gate, so `SA_SetSafePosAndDirection` is skipped) | aedbd..aee2f |

No engine or KC step re-centres the ship. Mayhem's `2004::Activate` additionally starts `2004::Bounce` for non-2019
ships. It begins with `TI_Delay(1000)` and calls only read natives (`SA_GetCollisionObjects`, `SA_Distance`, `SA_GetInst`);
its dynamic calls were not traced.

**Hook site for a lateral exit offset (replaces W for arrivals) [m]:**

| Site | Window | Live in / out | Stub contract |
| --- | --- | --- | --- |
| launch `0x00444606` | `8b 43 70` / `8b 4b 50` (6 B, two whole instructions; instruction start in the linear sweep; no direct branch into bytes 1..5) | EBX = ship, EDI = gate (set at `0x004444ce` and `0x00444575`, read at `0x00444651`), EBP = frame; `p+0x30` = P already written. EAX/ECX/EDX dead (rewritten at `0x00444606..0x0044460c`); ESI dead (rewritten at `0x0044466f` before any read); flags dead (`add` at `0x0044460f`, no reader before `0x00444630`). ESP = frame − 8: the two cdecl arguments of `0x004f0640` are still on the stack until `add esp, 8` at `0x00444637` | add `ℓ_R·R + ℓ_U·U` to `p+0x30..+0x38` (int32, guarded) for a class-7 ship that is not the player object (bit 2) and not under player control; preserve EBX/EDI/EBP/ESI semantics and ESP exactly (do not disturb `[esp..esp+8]`); no x87 state change (x87 was last used at `0x0044435f..0x0044436f` and popped); re-execute `mov eax,[ebx+0x70]`, `mov ecx,[ebx+0x50]`; `jmp 0x0044460c` |

* **Safety.** It runs on the game thread inside the sector post pass, once per launched ship and frame. The function is
  not re-entered. No script runs below it, because the KC wake comes later in the same function (`0x00444a32`).
* **Coverage.** Every gate arrival passes it, capitals included (both branches join at `0x00444590`). Non-gate launches
  (carriers, stations) take `0x004446d3` and never reach it.
* **What the offset reaches.** The case's start point, every 0x21 frame, the final snap and the 0x1e exit line all carry
  the offset.
* **Bound for a capital at up to 0.6 r.** Ordinary installed gates impose none, because they have no collision mesh
  (§10.9). On a Terran ring, `ℓ + r_s ≤ 0.663 r`, since the 0x21 path crosses the ring band and order 0x21 is not
  exempt from the pair filter (§10.9) **[i]**.
* **Small ships** already arrive on the 15° fan, 607 m up, so an offset composes with it.
* **Initial state after the arrival [m].** The ship is at P (plus any offset), facing −F, with speed and desired speed 0.
  Order 0 then lasts at least one frame until the KC issues 0x1e **[i]**. Order 0x1e then moves it at 120 m/s (large)
  or 240 m/s, axis-parallel, out to `|dz| > r` or `r/4`.
  * A capital whose `r_s ≥ 0.99 r` is already beyond `r`, so its 0x1e ends on the first frame **[i]**.
  * An M6 median runs 1003 m (8.4 s) and an M3 250 m (1.0 s) **[i]**.

#### Q3: arrival signals in the destination sector

| Signal | Where | Ships | Lead before the ship appears | Observable by a DLL hook |
| --- | --- | --- | --- | --- |
| through slot of the gate pair granted | KC only: controller gate members (§10.2); out-of-sector capitals request it at the source gate when their galaxy flight reaches it (stock bd012 `p6 = 1` → bccfb) | capitals (2019) | from the grant to the end of `__EnterHyperspace`, at least one more `__galaxyFlightStep` **[i]** | no engine mirror; it needs a KC object reader (not established) |
| `SA_IsWarpOutSlotAvailable(dest gate id)` | `__EnterHyperspace` out-of-sector branch, only when `this != g9`, the ship is not 2019 and the destination gate has an engine object (bd07e..bd0cb); polled every 100–1100 ms while all 6 slots are reserved | non-capitals | 0 when a slot is free (the arrival follows in the same KC run **[i]**); otherwise the congestion wait. Congestion needs more than 6 non-capital arrivals within 20 s at one gate | yes: SA case `0x00465ab4` (`0x00465acc..0x00465b00`), with the gate id as its only argument; no ship object exists yet (the dispatcher's task id is available) |
| engine object created inside the gate | `SA_AllocObject(7)`, then `SA_MoveObjectIntoContainer(ship, gate)` (`0x004497b0`), then `SA_StartInHangar(ship, gate, −1)` (`0x00443920`), all in one KC run | all | 0 to 1 main-loop iteration before the launch **[i]**; failed non-capital `StartInHangar` calls retry every 1 s with the object already in the gate's list (no 0x80) | yes: `0x00443920` (stdcall ship, container, port; `ret 0xc`) with a class-0x12 container, or SA case `0x004626e2` |
| warp-out slot table | `[gate.f+0x1c0..+0x1d8]` expiry, `[gate.f+0x1dc..+0x1f4]` occupant | non-capitals | written at `StartInHangar`; it marks an arrival that is in progress or less than 20 s old, not a future one | readable field, no hook needed |
| launch | `0x00444170` from the post pass: the ship enters the sector at P + 2697 m·F on order 0x21 | all | 0: the first frame in the sector; 900 ms until it reaches P, plus the 0x1e leg | `0x00444170` entry (stdcall ship, gate; `ret 8`) or the window above |

**Answer to Q3.** No engine-side record announces a capital before its arrival run. For a non-capital the earliest
engine-visible event is the `SA_IsWarpOutSlotAvailable` poll on the destination gate, which precedes the arrival only
under slot congestion. The earliest per-ship event is the `SA_StartInHangar` call. It falls 0–1 main-loop iterations
before the launch **[i]**, and the ship reaches its exit point 900 ms after the launch.

**Corrections to earlier sections.**
* §1.2, row 0x21: the case writes the position only when order 0x21 is not already running at that target; a KC arrival
  takes the early exit.
* §4 "Arrival":
  * the placement is the engine's launch `0x00444170` (sequence above), not `WarpObjectIn`;
  * the 1750000 compare in `__LeaveHyperspace` is in the tow branch only.
* §10.9 "Warp-in (exit spread)": `P` is the launch's exit point (table above), and site W is not reached on KC arrivals,
  so that bullet's offset never applies.

**Still inferred or open.**
* The time factor reading of `[*0x606f34+0xcc]`.
* The sin/cos reading of the run-time tables `0x005b6998`… (the 15° fan geometry depends on it).
* The −F facing (the 0x8000 angle is read; its axis is inferred from `0x004f0270`'s use in order 0x1f).
* `0x00443620` (the player-capital neighbour pass) and `0x00449510` beyond "enters the sector".
* Whether the KC resumes the woken task in the next iteration: yes, §10.12 Item 5.
* The lead of the through slot for out-of-sector capitals.
* Site W's zero count in Run159 corroborates the static finding. It does not prove it: that run has no row that counts
  arrivals into the sector. A `ship_gate_launch` row at `0x00444606` (ship, class code, slot index, P, gate) would settle
  both.

```sh
python3 -I verification/results/ship-navigation-ai/gates/arrival_sites.py      # RESULT PASS (121 checks)
python3 -I verification/results/ship-navigation-ai/gates/kc_arrival_flow.py   # KC arrival methods, stock + Mayhem
python3 -I verification/results/ship-navigation-ai/gates/arrival_sites.py --log verification/results/ship-steering/run159-triage/x3m.log
# -> rows 476, gate_warp_calls 0, gate_p4 167, gate_p5 2093 (local log, untracked)
```

### 10.11 Stage-B questions: the misalignment reference, the order-point readers, the escort switch, stale remainders

2026-10-13, static study of the same `X3AP.exe` (SHA-256 `fdbf3418…`), both `x3story.obj` versions and the installed
Mayhem loose scripts (`addon/scripts`), for stage B of the gate traffic controller
([gate-traffic-controller.md](../architecture/gate-traffic-controller.md) D.2.2, D.3, D.5, D.11). Nothing was launched
and no Wine command ran. Engine facts come from capstone listings over the PE map (raw listings stayed in the session
scratch, untracked; no Ghidra decompile was needed).
[`stageb_sites.py`](../../verification/results/ship-navigation-ai/gates/stageb_sites.py) re-reads every quoted byte
string, the SA and order jump-table entries, and runs three `.text` sweeps (flag `0x10000000`, `f+0x230` references,
order-0 remainder writes, integrator writes) (97 checks, `RESULT PASS`,
[`stageb_sites_out.txt`](../../verification/results/ship-navigation-ai/gates/stageb_sites_out.txt)).
[`kc_escort_flow.py`](../../verification/results/ship-navigation-ai/gates/kc_escort_flow.py) prints the KC escort and
sector-move methods for stock and Mayhem, the four `__runScript` call sites of `__EscortObject`, and the deciding MSCI
lines with the scripts' SHA-256
([`kc_escort_flow.txt`](../../verification/results/ship-navigation-ai/gates/kc_escort_flow.txt)).
[`stageb_creep.py`](../../verification/results/ship-navigation-ai/gates/stageb_creep.py) is the creep-zone model
([`stageb_creep.txt`](../../verification/results/ship-navigation-ai/gates/stageb_creep.txt), **[i]**). Symbols as in
§10.10; `t = (|v|/10 + r_s)·(1 + (tf − 1)/4)` is the order-5 trigger radius.

**Answer.**

* **Q1. The misalignment test measures the stored point.** It compares the ship's nose (row 2 of the flight basis
  `f+0x870`) with the direction from the ship's centre to the live field `f+0x240`, reached through a pointer, not
  the leaf's stack copy. For a capital that is the gate origin. It runs after the leaf (site A) in the same frame
  **[m]**.
* **The gate flag `0x10000000` does nothing on a gate.** It is the per-frame "being approached" flag of §11.6. Its
  only reader is the slew loop, for class-7 objects (a carrier being docked at). The post pass clears it every frame
  **[m]** (sweep).
* **A both-direction site-B write overrides the 1 m/s creep on every frame.** Site B (`0x004593dd`) is the target of
  the misaligned branch's own `jmp`. After it, the engine touches `[obj+0x14]` only through the slew loop's caps (vmax,
  the `f+0x278` speed limit) and the carrier freeze. The handler writes 500 again on the next frame and site B
  overrides it again **[m]**.
* **Consequence for D.2.2 [i].** On an F-parallel final line at lateral `ℓ`, the bearing to the origin exceeds 18.195°
  when the axial distance `z < 3.04 ℓ`. The trigger fires at `z = √(t² − ℓ²)`. The ship therefore creeps before the
  trigger whenever `ℓ > 0.312 t`.
  * For the Run160 census windows (`t = ℓ_max + 100 m`) the limits are Titan 344 m, Colossus 322 m, and the two M7s
    240 and 246 m.
  * Those limits lie below the whole Titan and Colossus windows and below most of the M7 windows. So the
    both-direction write is needed on **every** off-axis capital order-5 leg, not only for slots 13–19.
  * Without it, the creep distance is 88–2,590 m for the Titan.
* **Q2. On a gate leg only the order-5 handler reads the order point.** It reads `f+0x240` every frame, and nothing
  caches it beyond the frame. Nothing reads `f+0x230` on a gate leg. It is a copy of `f+0x240` written by
  `SA_FlyObjectIntoContainer`; its order-5 reader is the non-gate branch, and its other readers belong to orders
  0x12 and 0x1e **[m]**. A site-A rewrite of B is seen by that frame's misalignment test and by every test from the
  next frame on **[m]**.
* **Q3. The escort switch is in the Mayhem MSCI scripts, not in the KC.**
  * While the leader is still in the sector, the escort loops on `escort ship … timeout 30–34 s` (order 0x14) with a
    2–3 s wait between legs.
  * Once the leader's sector differs, the template calls `!move.jump`, and for an NPC owner that calls
    `Mayhem.Task.Move`. That script flies `follow object <next gate> with precision 5000 m : timeout 5000 ms`
    (order 0x15, 5 s legs) while `get distance between [THIS] and $nextGate > 10000`.
  * At 10 km or less it calls `fly to sector`. That reaches `__MoveToSector`, then `__FlyToNextSector`, then
    `__EnterHyperspace`: KC order 4 to the escort's own warp-in grid point.
  * Nothing reacts to an escort being far from its slot. There is no re-issued escort order, no teleport for an NPC
    owner and no wait beyond the ordinary capital queue **[m]** script text, **[i]** for the distance metric.
* **Q4. Order 0 keeps a stale remainder.** Under autopilot, order 0 zeroes the desired speed and the target only. It
  never writes `obj+0x30/+0x34/+0x38`. The integrator consumes any remainder regardless of the order **[m]**.
  * A capital handed over at P2 with a remainder keeps turning through order 0.
  * The remainder also survives an order 0x1f that ends on its first frame, because that end path zeroes only the
    roll remainder. It then survives the through-slot wait.
  * So a 55° pitch remainder turns the nose about 55° away from the axis while the ship waits **[m]** rules, **[i]**
    magnitude.

#### Q1: the order-5 misalignment test [m]

| Step | Rule | Site |
| --- | --- | --- |
| pointer | `esi = &f+0x240`; `[esp+0x2cc] = esi`: a pointer to the live field. The stack copy `[esp+0x4a0]` is only the leaf's argument | `0x0045413e`, `0x0045418b`, `0x004541a6`, `0x00454733` |
| gate branch | a class-0x12 target jumps to `0x00454261`. That skips the rendezvous read of `f+0x230` and the switch of the pointer to `f+0x230` (non-gate targets, while far) | `0x004541b4`, `0x004541c7`, `0x004541cd`, `0x004541fa` |
| order in the frame | distance and trigger (`0x0045413e..0x004546e3`), then the leaf `0x0044ccc0` (site A), then the misalignment test. The port-roll match is skipped for a gate (`0x00454760`) | `0x00454745`, `0x004547d9` |
| direction | `dir = normalise(*[esp+0x2cc] − p+0x30)` to 16.16 unit length (`0x49c8b0`, scale `[0x565510]` = 1/65536) | `0x004547d9..0x00454821` |
| basis | `0x00450520(ship)`: class 7 → `f+0x870` (flight basis, ship-movement.md §2); row 2 = the nose (`+0x20/+0x24/+0x28`) | `0x00454828`, `0x0045482f`, `0x0045484f`, `0x00454873` |
| test | `nose·dir < 0xf333` (0.95, 18.195°) → desired `[obj+0x14] = 500`, `[target+0x40] \|= 0x10000000`, `jmp 0x004593dd`. The aligned branch, with its ramp and `[gate+0x40] \|= 0x2000000`, is skipped | `0x00454897`, `0x004548a5`, `0x004548ac`, `0x004548b3` |
| aligned | `[gate+0x40] \|= 0x2000000`, ramp, `[target+0x40] \|= 0x10000000` (`0x0045498f`), `jmp 0x004593dd` | `0x004548cf`, `0x00454996` |

So the test is relative to `f+0x240`, which is the gate origin for a capital (§10.5). A leaf target off the axis (site
A's carrot or a funnel line) does not change it.

**The flag `0x10000000` [m]** (sweep of every immediate `0x10000000` in `.text`; register forms matched to a load of
`[reg+0x40]` within the 8 previous instructions):

| Role | Sites |
| --- | --- |
| setters on `[target+0x40]` | order 5 port branch `0x004543ad`, order 5 misaligned `0x004548ac`, order 5 aligned `0x0045498f`, order 4 `0x00455083`, order 0x1b `0x00455196`: exactly these five |
| reader | `0x00459533`, the slew loop. It reaches the test only for class 7 (`0x00459503`) with bits `0x100000` and `0x80000`. It then zeroes desired speed and remainders (`0x0045954d`; §11.6 "carrier held still"). The slew loop skips every class other than 7, 0x13 and 0xa (`0x004594b0`), so a gate never reaches it |
| clear | the post pass `0x0045b720`: `ebp = 0xedffffff` (`0x0045b741`), `and [obj+0x40], ebp` (`0x0045b7f9`) for every object, every frame. No immediate `and` on `[reg+0x40]` clears bit 28 |
| `0x2000000` (aligned branch, order 8) | read by the gate's post-pass update `0x0044b0c0` (`0x0044b124`), which drives part animation via `0x0044aee0` **[i]** meaning. The misaligned branch does not set it |

**Site B against the creep [m].**

| Step in the frame | Writes `[obj+0x14]` | Site |
| --- | --- | --- |
| order-5 handler, misaligned | 500 | `0x004548a5` |
| site B, the object-loop continue, reached by the handler's `jmp` (order 5 has five exits to it: `0x00454647`, `0x00454723`, `0x004548b3`, `0x00454996`, `0x004549bd`) | the module's value | `0x004593dd` |
| slew loop, after the object loop (classes 7, 0x13, 0xa) | `min(desired, vmax)` | `0x004594ef..0x00459500` |
| slew loop, class 7 | `min(desired, f+0x278)` when the limit is ≥ 0 | `0x0045950d..0x0045951c` |
| slew loop, class 7 with `0x100000`, `0x80000` and (`0x10000000` or `now < f+0x180`) | 0, and remainders 0 | `0x0045951f..0x00459558` |
| slew | `[obj+0x10]` moves toward `[obj+0x14]` by `accel·dt` | `0x0045955b..0x004595b1` |

* **The write stands on every frame.** The handler rewrites 500 each frame and site B overrides it each frame.
  This is the mechanism small ships already use (`kShipFlagGateDock`; Run155 counted 11,360 such writes, a counter
  only **[t]**).
* **Hook-site facts.** No new site is needed. Site B's contract is unchanged (§6, `ship_steering_sites.h`): all GPRs
  live, flags dead, ESP exact. It runs on the game thread inside `0x00452ad0` with no script below it, and is not
  re-entered.
* **One exception: the carrier freeze.** A departing capital with bits `0x100000` and `0x80000` is frozen for a frame
  whenever another ship sets `0x10000000` on it that frame: a ship on order 5 to it, or on order 4 inside `d_slow`.
  That frame its desired speed and remainders are zeroed after site B. The integrator writes no `[obj+0x14]`
  (listing `0x004596e0..0x0045a7a0`), so there is no other later writer.

**Creep zone for an off-axis capital Q [i]** (`stageb_creep.txt`):

* On an F-parallel line at lateral `ℓ` with the nose along F, the bearing to the origin is `atan(ℓ/z)`. The test fires
  for `z < ℓ·cot 18.195° = 3.04 ℓ`.
* The trigger fires at `z_t = √(t² − ℓ²)`. The creep zone is therefore reached first iff `ℓ > t·sin 18.195° = 0.312 t`.

| Census hull (D.2.2 window, `t = ℓ_max + 100 m`) | Creep iff ℓ > | Creep distance at the window's low / middle / high ℓ |
| --- | --- | --- |
| Titan r 1092 w 583 (370–1,002 m) | 344 m | 88 / 1,225 / 2,590 m |
| Colossus 1022/546 (331–932 m) | 322 m | 30 / 1,103 / 2,392 m |
| M7 760/382 (162–670 m) | 240 m | 0 / 618 / 1,659 m |
| M7 778/352 (135–688 m) | 246 m | 0 / 578 / 1,709 m |

#### Q2: readers of `f+0x240` and `f+0x230` [m]

Sweep of every non-stack `+0x230` displacement, `lea` and `add` in `0x00440000..0x0046a000` (11 sites). A function
is assigned to a site by the nearest handler or SA case entry below it **[i]**; the listings confirm it at each site
quoted. The `+0x240` sites are the full displacement list of the same range.

| Field | Reader or writer | When | Site |
| --- | --- | --- | --- |
| `f+0x240` | order-5 handler: trigger distance, leaf copy, misalignment pointer | every frame of order 5 (no re-plan gate) | `0x0045413e`, `0x00454192`, `0x004541a6` |
| `f+0x240`, `f+0x230` | order-5 handler: rewritten from the port, live | **only** a class-7 target (`cmp [edi+0x48],7; jne` at `0x0045406a`); never for a gate | `0x004540b8`, `0x0045411e` |
| `f+0x240` | `SA_FlyObjectIntoContainer`, gate branch: B (non-capital) or the origin (capital) | on each KC issue of order 5 | `0x00460c25` |
| `f+0x230` | same case: `f+0x230 = f+0x240` | same | `0x00460c49..0x00460c6c` |
| `f+0x230` | order-5 handler, rendezvous read | non-gate targets only (the gate jumps over it at `0x004541c7`) | `0x004541cd` |
| `f+0x230` | proximity handler `0x0044e450`: no warn within 88,800 units (177.6 m) of the 0x1e start point | order 0x1e only (`0x0044e492`) | `0x0044e4af`, `0x0044e4bd` |
| `f+0x230` | order 0x12: own start point, written by `SA_AvoidObjectCollision` (`0x004610df`) and the handler | order 0x12 only | `0x00456f32`, `0x00457842` |
| `f+0x230` | `SA_ClearObjectFromHangar`: the 0x1e start point | order 0x1e | `0x00461211` |
| `f+0x240` | `SA_LandAllObjectsInHangar` (SA 0x121) core `0x00449930`: every ship on order 5 to the container within `r/2` of its `f+0x240` gets order 0 | only when a script calls it on that container | `0x0044999b`, `0x00449a05` |
| `+0x218..+0x24c` | serializer `0x00478690` / deserializer `0x00479d10` (save and load **[i]**) | save, load | `0x00478bc7`, `0x0047a2c6` |

Every other `+0x240` site in the range belongs to the handler or SA case of another order, each reading its own
parameter (§1.3). No SA case returns `f+0x240` or `f+0x230` to a script. `SA_StopObject` reads `f+0x240` only on
order 0x21 (`0x00461ed2`).

**For the small-ship B rewrite of D.5:**

* Rewrite `f+0x240` from site A and every order-5 reader sees it.
  * The rewrite happens during the leaf call (`0x00454745`). On that frame the trigger distance (`0x0045413e`) and the
    leaf copy were already taken with the old B; the misalignment test (`0x004547d9`) reads the new B.
  * From the next frame on, all three use the new B.
  * The handler never rewrites `f+0x240` for a gate, so the value holds until the KC re-issues order 5.
* **Writing `f+0x230` is not needed.** Nothing reads it on a gate leg. Writing it does no harm: orders 0x12 and 0x1e
  overwrite it before they use it.
* **Saves.** A save taken during the leg stores the rewritten B **[i]**. After a load, the guard "field equals the
  engine's arc point" fails, so the module leaves the field alone, and that B is still a lower-arc point.
* **Capitals [i].** The same reader list means that a site-A rewrite of a capital's `f+0x240` to Q would move the
  trigger centre and the misalignment reference to Q together.

#### Q3: the escort switch [m] script text, [i] where marked

**KC (stock = Mayhem after masking, 164 / 29 / 61 statements).**

`2004::__EscortObject(leader, dist, follow, timeout)` (stock `b8b2a`, Mayhem `d10f7`) loops as follows:

* return 7 when attacked;
* return 8 when the leader is gone, when `m1 != leader.GetEnvironment()` (the leader left the sector), or for a
  different owner beyond `param12 + sizes` while one of the two is hidden;
* return 12 on the timeout;
* in sector: `follow` → `SA_FollowObject` (order 0x15); else `SA_EscortObject` (order 0x14);
* out of sector: `__galaxyFlightStep` toward the leader.

`__runScript` (702) calls it from four MSCI commands (`kc_escort_flow.txt`):

| Command | Call | Order |
| --- | --- | --- |
| `escort ship` | `(target, 0, 0, 0)` | 0x14 |
| `escort ship … timeout` | `(target, 0, 0, t)` | 0x14 |
| `follow object … with precision d m` | `(target, 500·d, 1, 0)` | 0x15 |
| `… and timeout t` | `(target, 500·d, 1, t)` | 0x15 |

`2004::__MoveToSector` (`b7470`) loops `__FlyToNextSector` until `m1` is the target sector. `__FlyToNextSector`
(`b7231`) takes the next gate from `GS_Route` and runs `__EnterHyperspace`. Nothing in these methods tests the
distance to a slot or to a leader.

**MSCI (installed loose scripts; SHA-256 in `kc_escort_flow.txt`).** `!move.follow.template` (Mayhem-edited, mtime
2026-10-02) runs the escorts of a squadron leader:

| Step | Rule | Line |
| --- | --- | --- |
| loop | `while $leader exists`: wait 2–3 s, attack checks, then `Follow:` | 70, 97 |
| in the leader's sector | `escort ship $target : timeout = $timeout ms` with `$timeout` = 30,000–34,000 ms: the measured 0x14 legs of about 33 s with 3 s order-0 gaps | 261, 270 |
| leader left | `CheckLeader`: `[SECTOR] != $leader.sector` → `!move.jump : target = $leader withsupportships = [TRUE]`, then wait 1–2 s and restart | 314, 320 |
| `!move.jump`, NPC owner | not Player/Phanon → `MoveThroughGates` → `Mayhem.Task.Move : sector = leader's sector, posArrayOrTarget = $leader`. The beacon teleport (`set position`) exists only for Player/Phanon owners | 18–22, 83, 134 |
| `Mayhem.Task.Move` loop | while `[SECTOR] != $sector`: the next gate on the (safe) route → `FlyThroughNextGate` → `goto Restart`, re-reading the leader's sector each pass | 87–159 |
| `FlyThroughNextGate` | `$distance = get distance between [THIS] and $nextGate`; **`> 10000` → `follow object $nextGate with precision 5000 m : timeout = 5000 ms`** (order 0x15 to the gate, a 5 s leg); otherwise **`fly to sector $nextSector`** | 220–225 |
| `fly to sector` | KC `__MoveToSector` → `__FlyToNextSector` → `__EnterHyperspace`: for a capital, warp-in slot → **order 4 to its grid point**, then 0x1f, through slot, order 5 (§10.1) | KC above |

* **The condition [m].** Two things must hold before an escort gets order 4: the leader is no longer in the escort's
  sector, and the escort is within 10,000 m of the next gate on its route. The template only notices the leader's
  sector change after its current 0x14 leg ends and its 2–3 s wait has run. The distance is checked once per
  `Mayhem.Task.Move` pass, i.e. at most every 5 s while the escort is beyond 10 km.
* **The distance [i].** `get distance between` is taken as the centre distance (`SA_Distance`, gate-queue-script.md
  §4). Flight 1's switch at 10.0 km agrees.
* **Timing [i].** The M7s' order 4 came 4.2–4.5 s after the leader's launch: one 2–3 s wait plus the
  `!move.jump`/`Mayhem.Task.Move` overhead. Run160b's escorts were beyond 10 km, so they flew 5 s 0x15 legs
  (`cap_orders.txt`: 5.0 s 0x15 segments with about 0.1 s gaps) until 100.5 s and 187.3 s.
* **An escort far from its slot [m].** Nothing re-issues an escort order, teleports the ship (NPC owner) or waits on
  it. `__EnterHyperspace` treats the escort as any capital:
  * it requests a warp-in slot (refused: `SA_Idle` wander for 1–11 s, §10.2);
  * it flies order 4 from wherever it is to its grid point;
  * it rotates and requests the through slot.

  The template stays blocked inside `fly to sector` until the next sector is reached or the transit fails. On
  failure, `__FlyToNextSector` waits 2–3 s and `Mayhem.Task.Move` restarts.
* **For D.3.**
  * There is no KC-side 10 km switch. A petal 8–10 km from the gate gets order 4 at the first `FlyThroughNextGate`
    pass after the leader has left. A petal at 10–12 km first flies one or more 5 s 0x15 legs, which are steered
    orders, aimed at the gate.
  * No escort can receive order 4 before its leader is out of the sector.

#### Q4: order 0 and the stale remainder [m]

| Step | Rule | Site |
| --- | --- | --- |
| order 0, autopilot `0x80000` | `[obj+0x14] = 0` unless `[obj+0x44] & 4`; `f+0x224 = 0`; then the shared tail: `f+0x218` cleanup, `f+0x28c = 0`, `f+0x210 = now`. **No write to `+0x30/+0x34/+0x38`**: the sweep of `0x00458f8e..0x00459141` finds none | `0x00458f8e..0x00458fa8`, `0x004590e0..0x0045913c` |
| order 0, manual | decays the rates `+0x24..+0x2c` (not the AI path) | `0x00458fad..0x004590db` |
| integrator, autopilot | per axis with `e ≠ 0`: controller `0x0043ad20`, `e −= δ` (roll `0x004599f4`, pitch `0x00459b0e`, yaw `0x00459c2c`). No read of the order word in `0x00459926..0x00459cdb` | `0x00459926`, `0x00459980`, `0x004599d4` |
| order-4 arrival (P2 frame) | speeds and wake only. The dispatcher/leaf call (`0x00454f83`) lies after the arrival exit, so no leaf runs on the P2 frame | `0x00454b70`, `0x00454b91`, `0x00454f83` |
| order 0x1f, aligned on the first frame, no roll target | `[obj+0x38] = 0`, order 0, result 1; **yaw and pitch remainders untouched**, leaf not called | `0x004592d9`, `0x004592e4`, `0x004592f4` |
| order 0x1f with a roll target | yaw = pitch = 0, roll = target − roll | `0x0045932b` |
| order 0x1f, not aligned | leaf `0x0044ccc0` toward position + gate axis (rewrites all three) | `0x004593a6` |

**What happens to a ship with a stale remainder.**

* **Capital.** Whatever the last order-4 leaf call wrote, minus what the integrator consumed since, survives P2 and
  the ≥ 1 order-0 frame.
  * If 0x1f then ends on its first frame (amendment 13's goal), the yaw and pitch remainders survive 0x1f as well.
    They keep turning the ship through the refused-slot wait (`TI_WaitRandomForInterrupt`, order 0) until they are
    used up.
  * Order 5 then starts misaligned, and Q1's 1 m/s creep applies unless site B writes the speed.
  * If 0x1f is not aligned on its first frame, its leaf replaces the stale values with a turn toward the axis.
* **Small ship.** Order 5 follows P2 after one KC hand-over, and its first frame's leaf rewrites the remainders. The
  stale remainder lasts only the order-0 frames.
* **Who carries `0x80000` [i].** The Run160 capitals' trace rows come from site B, whose stub admits only class 7
  with autopilot, so those ships carried `0x80000`.
* **Hook-site facts.** P2 (`0x00454b91`) has EBX = obj with flags dead. Writing `[ebx+0x30]/[ebx+0x34]` there (for
  example 0, or the braking angle of ship-movement.md §13.4 item 3) stays inside its existing contract. Nothing reads
  the remainders between that site and the integrator (pass B) except a 0x1f leaf on a later frame **[m]** listing,
  **[i]** for the proposal.

**Still inferred or open.**

* The meaning of `0x2000000` in `0x0044b0c0` (part animation).
* The save/load reading of `0x00478690`/`0x00479d10`.
* The distance metric of `get distance between`.
* Whether every Mayhem capital escort runs `!move.follow.template`. `Mayhem.Squadron.Add` and the squadron task
  scripts start it; others (`!ship.cmd.protect.std`) also reach it. The Run160 order pattern matches it.
* The order-0x14 end on a lost leader (§1.3) is listed in §10.12 Item 3.

```sh
python3 -I verification/results/ship-navigation-ai/gates/stageb_sites.py     # RESULT PASS (97 checks)
python3 -I verification/results/ship-navigation-ai/gates/kc_escort_flow.py   # KC escort/sector methods + MSCI lines
python3 -I verification/results/ship-navigation-ai/gates/stageb_creep.py     # creep-zone model [i]
```

### 10.12 Trail line questions

2026-10-14, static study of the same `X3AP.exe` (SHA-256 `fdbf3418…`), both `x3story.obj` versions and the installed
TShips, for the convoy gate transit design ([convoy-gate-transit.md](../architecture/convoy-gate-transit.md) §8
items 2, 3, 4, 5, 8 and 1). Nothing was launched and no Wine command ran. Engine facts come from capstone listings
over the PE map (raw listings stayed in the session scratch, untracked; no Ghidra decompile was needed).
[`trail_sites.py`](../../verification/results/ship-navigation-ai/gates/trail_sites.py) re-reads every quoted byte
string and SA table entry, runs three `.text` sweeps (`[obj+0x44]` bit 2, `0x2000000` on `[obj+0x40]`, non-indexed
`+0x180`), prints the KC bytecode and statements quoted below for stock and Mayhem, and the TShips column-10 census
(`RESULT PASS`, [`trail_sites_out.txt`](../../verification/results/ship-navigation-ai/gates/trail_sites_out.txt)).
Symbols as in §10.10: `f = [obj+0x50]`, `r` = `[gate+0xa4]`, `S` the ship centre, `C` the gate centre, `P` the order
point. **[s]** = a site byte string re-read by the script, **[m]** = read from the listing, **[i]** = inferred.
"Frame N" is one main-loop iteration: the KC scheduler pass, then the sector update with the order handlers.

**Answer.**

* **Item 2. The order-4 point is read live, at every re-plan tick, and nowhere else.** The capital case
  `SA_FlyObjectBeforeContainerPos` writes `f+0x240..+0x248` from the script's cells and nothing else of the point.
  The handler copies the live field into a stack local at each tick, tests arrival (`|P − S| ≤ 3r/4`) on that copy,
  and only then runs the container detour and the leaf. The detour writes only the stack local. It cannot engage
  while the ship and `P` are both on an axis-parallel line in front of the gate plane, nor anywhere beyond
  `√5·r` (2.47 km) from the gate centre. Every installed capital re-plans every 0–49 ms of game time **[m]**.
* **Item 3. The escort order ends one frame after the leader's order 8, with result 3; `__EscortObject` returns 8
  one frame later.** The leader's `Remove()` frees its engine object. The free path clears every class-7 `f+0x224`
  that names it. The escort's 0x14 handler tests for a null leader on every frame **[m]**.
* **Item 4. Order 0 zeroes the desired speed on every frame, and site B runs after it on every frame.** Site B's
  write then stands through the slew loop. The only caps are vmax, `f+0x278` and the carrier freeze. The `[obj+0x44] & 4` exemption is set only
  for decoupled or scene-started objects **[m]**.
* **Item 5. A woken KC task runs in the next frame's scheduler pass.** The leader's `ReleaseFromSlot` and `Remove()`
  therefore run one frame after its order 8 ends, before that frame's order handlers **[m]**. This closes the §7 and
  [script-task-scheduler.md](script-task-scheduler.md) §2 open point: script SA calls complete through the VM's own
  callback `0x004a4910`, not through `0x004604d0`.
* **Item 8. Player-owned is `(int16)[obj+0x3c] == 10`. The piloted ship is `[obj+0x40] & 2`.** The KC writes the
  owner's race id into the native group, and the player race's id is 10 **[m]**.
* **Item 1. The engine has no per-gate exclusivity for orders 5 and 8.** `SA_WarpObjectAway` cannot refuse. The
  per-gate state the two orders write is a per-frame animation flag, and `[gate.f+0x180]` gates only that animation.
  The KC through slot is the only serialiser **[m]**.

#### Item 2: the order-4 point, the arrival test and the detour [m]

| Step | Rule | Site |
| --- | --- | --- |
| issue | `SA_FlyObjectBeforeContainerPos(ship, gate, x, y, z)` (SA 0x0c): `f+0x240/+0x244/+0x248` = the raw cells (world units, no transform), order 4, `f+0x224` = gate, port `f+0x250 = −1`, `[obj+0x7c]/[obj+0x80]` = the caller's completion slot and task. No write to `f+0x230`, `f+0x24c`, `f+0x210` or a speed | `0x0046537c` [s], `0x00465388` [s], `0x00465394` [s], `0x0046539d` [s], `0x004653ac` [s], `0x004653b9` [s] |
| re-plan gate | `now − f+0x210 < 0` → `jmp 0x4593dd` (site B). Between ticks the handler does nothing: no leaf, no arrival test, no speed write | `0x004549e3` [s] |
| interval | `f+0x210 = now + rand(f+0x20c)`, `f+0x20c = 50·type[+0x50]` ms. TShips column 10 is 1 on every installed row (M1 27, M2 45, M7 46, TL 13, M0 1 rows), so the next tick comes 0–49 ms after the last | `0x004549ef` [s], `0x00454a17` [s], `0x004404a1` [s]; census in `trail_sites_out.txt` |
| point | port < 0: `[esp+0x360] = f+0x240..+0x24c`, rebuilt from the live field at every tick; nothing else holds it | `0x00454a20` [s], `0x00454a86` [s], `0x00454a99` [s] |
| distance | `d = \|P − S\|` (`0x412490`, length `0x469b80`, xyz only) into `[esp+0x2c8]` | `0x00454acf` [s], `0x00454b02` [s] |
| arrival | class-0x12 target: radius `3r/4`; `d ≤ radius` → P2: port −1, order 0, `[obj+0x14] = [obj+0x10] = 0`, wake 1 | `0x00454b09` [s], `0x00454b1a` [s], `0x00454b70` [s], `0x00454b91` [s] |
| detour | conditions a–e of §11.1 with `r_C` = `0x488170(gate)` = `[gate+0xa4]` when ≥ 0. The replacement `P'` goes to `[esp+0x360]` only; `d` is recomputed for the speed law | `0x00454c2c`…`0x00454d0a` [s], `0x00454ed5` [s], `0x00454efc` [s], `0x00488176` [s] |
| leaf | `0x44d020(obj, &[esp+0x360])` (site A below it), after the arrival test | `0x00454f81` [s] |

* **When a rewrite takes effect.** Site A runs inside the leaf call, after that tick's arrival test. A rewrite of
  `f+0x240` from site A is therefore first used by the next tick, for both the arrival test and the goal. That is
  ≤ 50 ms of game time later: at 60 fps every 1–3 frames, every frame once a frame step reaches 50 ms (SETA)
  **[i]** arithmetic. The first tick of every leg (the issue and every KC re-issue) tests the KC's grid point.
* **No other writer during order 4.** The handler writes `f+0x240` only on the port ≥ 0 path, through a stack
  local, not the field. The other `+0x240` writers are SA cases that set a different order (§10.11 Q2 sweep).
  `SA_SetDestination` writes `f+0x224` only.
* **When the detour can engage [i] from rules a, b, d.** Rules a and d put the projection of `C` on the ship's line
  between `S` and `P`, within `r` of the line. Rule b adds `|P − S| < 2r`. Together they give
  `|C − S|² < |P − S|² + r² < 5r²`, so there is no detour while `|S − C| ≥ √5·r` (2.47 km at r = 1105 m). On an
  axis-parallel line Λ with `S` behind `P` and `P` in front of the plane, `u` is F and `u·(P − C) = z_P < 0`, so rule a
  fails at any distance. Only a ship that joins Λ from the side (a merge or a re-departure U-turn) within 2.47 km of
  the gate centre can be detoured. Its leaf then steers at `S + r·m̂` (a tangent of length r, with y halved), while
  the arrival test still uses `P`.
* **Answer: yes on all three counts.** `f+0x240` is read uncached at every tick, no copy is taken at the issue, and
  the detour never touches the field.
  * **Constraint 1.** A write takes effect at the next tick, not on the frame of the write.
  * **Constraint 2.** Keep the rewritten point in front of the gate plane, with the ship behind it on Λ. A member
    that merges from the side must still be ≥ √5·r from the gate centre when its point is rewritten, or accept that
    the engine's tangent replaces its goal until it is on the line.
  * **Constraint 3.** The `f+0x224 == Γ` guard holds only while order 4 runs: order 0 clears `f+0x224` on every
    frame (Item 4).

#### Item 3: the escort order's lost-leader end, and the return-8 path [m]

| Step | Rule | Site |
| --- | --- | --- |
| test (every frame, before the re-plan gate) | `L = f+0x224`; end if `L == 0`, `L == ship`, `[L+0x54] == 0` (no parent), or `[[L+0x54]+0x48] != 1` (parent not a sector, e.g. docked) | `0x0045820d` [s], `0x0045821c` [s], `0x0045822a` [s], `0x00458252` [s] |
| end | order 0, wake with **3**; no speed write on that frame | `0x00458ad0` [s] |
| timeout | `f+0x28c ≠ 0` and `now ≥ f+0x290`: order 0, wake with **13**, also tested every frame | `0x00458aac` [s], `0x00455ac8` [s] |
| leader pointer | `SA_EscortObject` stores the leader's object pointer in `f+0x224` at issue | `0x004618ef` [s] |
| free | `SA_FreeObject` → `0x430840(obj, 1)` → `0x43fca0` → teardown `0x43f990`. That wakes a task waiting on the freed object with 8, then calls `0x44aab0`, which walks the sector list `[[0x60850c]+8]` through `0x44a7c0`. For every class-7 object, `f+0x224 == freed` becomes 0 | `0x0046074a` [s], `0x0043084b` [s], `0x0043fcc3` [s], `0x0043f9e1` [s], `0x0043fb24` [s], `0x0044aad4` [s], `0x0044a88d` [s] |

**The frame sequence for a leader leaving through a gate.**

1. **Frame N, sector update.** The leader's order 8 ends and its task is woken with 1 (`0x0045520a`, `0x00455228`
   [s]).
2. **Frame N+1, scheduler pass (Item 5).**
   * `__EnterHyperspace` returns 3.
   * `ReleaseFromSlot()` runs, then `Remove()` (`this != g9`), which calls `Deactivate()`.
   * `Deactivate()` calls `SA_StopObject(m3, 6)` … `SA_FreeObject(m3)` and `304::Add(this)`.
   * `Remove()` then calls `m1.RemoveShip(this)`, which calls `SetEnvironment(0)`. The KC object itself survives.
   * The statements are identical in stock and Mayhem (`trail_sites_out.txt` §3).
3. **Frame N+1, sector update.** The escort's 0x14 handler finds `f+0x224 == 0` and ends with 3.
4. **Frame N+2, scheduler pass.**
   * `__EscortObject` resumes. A result other than 13 goes back to the loop head (b8c7e / d124b).
   * `m1 != leader.GetEnvironment()` holds there: `GetEnvironment` returns `m1` (`0f 00 01`), which is now 0, or the
     destination gate once `__LeaveHyperspace` has run.
   * The method returns **8** (b8cf4).
   * `SE_ObjectExists(leader)` stays true because `Deactivate` keeps the KC object **[i]**.

An escort that is in its 2–3 s order-0 gap at that moment reaches the same `return 8` at the head of its next
`escort ship` call, without issuing 0x14. From there the MSCI path of §10.11 Q3 takes over: the template's 2–3 s
wait, `CheckLeader`, `!move.jump`, then `Mayhem.Task.Move`.

* **Answer.** The test is a null, self, parentless or non-sector leader, run on every frame. It fires in the sector
  update of frame N+1, one frame after the leader's order-8 end, with result 3. `__EscortObject` returns 8 in frame
  N+2. The engine plus KC part of λ is therefore 2 frames, and the rest is the MSCI part (§10.11 Q3).
* **Constraint.** From frame N+1 the escort's `f+0x224` is 0, and it stays 0 through its order-0 gap (Item 4). The
  board must hold the escort role in its own state, not read it from `f+0x224`.

#### Item 4: order 0 under autopilot and site B [m]

| Step | Rule | Site |
| --- | --- | --- |
| order 0, `[obj+0x40] & 0x80000` | every frame (no re-plan gate): `[obj+0x14] = 0` unless `[obj+0x44] & 4`; `f+0x224 = 0` | `0x00458f8e` [s] |
| tail | `f+0x218` cleanup, `f+0x28c = 0`, `f+0x210 = now`, `jmp 0x4593dd` (site B) | `0x00459136` [s] |
| `[obj+0x44] & 4` | the only setters are `0x43b0b0` (from `SA_DecoupleShips`), `0x43bb00`/`0x43c2d0` (`SA_StartInGameScene`/`…2`) and `0x43b750` (two calls in the interpreter's type-table model path `0x004534e6`/`0x00453ac3`). No and-mask on `[obj+0x44]` clears bit 2 (sweep) | `0x0043b216`, `0x0043c048`, `0x0043c3bd`, `0x0043b8a2` (sweep); `0x00466df1` [s], `0x00466e37` [s], `0x00466e54` [s] |
| slew loop, after the object loop | no slew at all if `[obj+0x40] & 0x800200` or `[obj+0x44] & 0x2000000`. Otherwise desired is capped at vmax `0x450980`, then for class 7 at `f+0x278 ≥ 0`, then the carrier freeze applies (`0x100000` and `0x80000` and (`0x10000000` or `now < f+0x180`) → desired 0, remainders 0). After that `[obj+0x10]` slews toward desired | `0x004594ca` [s], `0x004594d7` [s], `0x004594ef` [s], `0x0045950d` [s], `0x00459533` [s] |

* A KC ship (`2004::Activate` → `SA_AllocObject`) passes through none of the bit-2 setters, so AI capitals do not
  carry it **[i]**: their order 0 zeroes the desired speed on every frame. Between site B and the slew nothing else
  writes `[obj+0x14]` (§10.11 Q1, integrator listing).
* **Answer.** Order 0 writes `[obj+0x14] = 0` on every frame, then reaches site B on every frame. Site B's write
  stands through the slew loop on every order-0 frame, subject only to vmax, `f+0x278` and the carrier freeze.
* **Constraint.** The hold must be written on every order-0 frame, because a single write is undone by `0x00458f9f`
  on the next frame. `f+0x224` is 0 during the gap. A speed limit set by a refused warp-in slot (`f+0x278`, §8 item 9)
  caps the hold. Hook-site contract unchanged: site B as in §10.11 Q1.

#### Item 5: the KC task's resume after an order ends [m]

| Step | Rule | Site |
| --- | --- | --- |
| wake | `0x460560(obj, result)`: slot = `[obj+0x7c]`, callback = `[VM + 0x34 + slot·0x18]` (task id `[obj+0x80]`, result) | `0x00460560` [s], `0x0046057c` [s], `0x00460587` [s] |
| which callback | every native call from a script gets `ctx = [VM+0x1454]` as its first argument, and the SA case stores it as the slot. `[VM+0x1454]` is the VM's own native group, whose callback is **`0x004a4910`**. `0x004604d0` is SA's own slot, used only when the game calls SA itself | `0x004a38ff` [s], `0x0049cf72` [s], `0x0049cf2e` [s], `0x004653a9` [s] |
| resume | `0x4a4910`: task by id in the VM hash `[VM+0]`; `task+0x20 = 1`; return cell = (int, result); `0x4a4740`: a suspended task (state 2) gets its arguments popped, the result pushed, state 1, and is linked at the **head** of the runnable list `VM+0x12d8` | `0x004a4910` [s], `0x004a494b` [s], `0x004a4968` [s], `0x004a4740` [s], `0x004a47b5` [s], `0x004a47c9` [s] |
| run | scheduler `0x49f770` runs the runnable list from its head until it is empty (or a native returns 0). The main loop calls it once per iteration (`0x00403aff`) before the sector driver (`0x00403b17`); back edge `0x004041d8` → `0x00403ab0`. The fourth caller, `0x497200`, runs once before the loop (`0x004039ab`) | `0x0049f777` [s], `0x00403aff` [s], `0x00403b17` [s], `0x004041d8` [s], `0x004039ab` [s] |

* **The order-8 end to the slot release.** Order 8 ends in frame N's sector update (`0x0045520a`, `0x00455228` [s]).
  The leader's task runs in frame N+1's scheduler pass. Everything up to `Remove()` (`ReleaseFromSlot` →
  `gate.ReleaseSlot`) is KC code plus non-blocking natives, so the through slot and the warp-in slot are released in
  that pass, before frame N+1's order handlers.
* **Order within a pass.** Each wake links its task at the head, so tasks woken in the same frame run
  last-woken-first. Take a follower whose 0x1f ended later in the same sector update (later in the object walk) than
  the leader's order 8. Its `RequestWarpThroughSlot` runs before the leader's release and is refused (1–11 s wait,
  §10.2).
* **Answer.** The slot release follows the engine's order-8 end by exactly one frame: one game frame step,
  SETA-scaled.
* **Constraint.** A follower's request runs two frames after its P2 frame: P2 in frame k, its task issues 0x1f in
  k+1, 0x1f ends in k+1 when aligned, and the loop head requests in k+2.
  * It is granted (other rules permitting, §10.2) when its P2 frame is not earlier than the leader's order-8 end
    frame N.
  * P2 in frame N−1 is decided by list order.
  * The design's two-frame margin is therefore one frame more than needed. With a misaligned 0x1f, add its turn time.

#### Item 8: recognising a player-owned ship [m]

| Fact | Rule | Site |
| --- | --- | --- |
| native owner group | int16 `[obj+0x3c]`: −1 at object init; written only through `SA_SetGroup` (SA 0x6f → `0x450730`) from script; read by `SA_GetGroup` (SA 0x70, `movsx`), relations `0x450790`/`0x450810` | `0x0044001a` [s], `0x00462bc9` [s], `0x00450730` [s], `0x00462bf3` [s] |
| KC writer | every `SA_SetGroup` call passes the owner's id: `2004::Activate`, `2004::SetOwner` and `2004::__SetCoverRaceRecursive` pass `GetOwner().GetID()`; `2016::Activate2`/`SetOwner` (stations) pass the owner member's id. 5 call sites in stock and in Mayhem | `trail_sites_out.txt` §3 |
| player id | `402::GetID` = `6e 00 01 05 0a 83` (push 10, return) in both versions. `0x450730` special-cases group 10 (a different scene callback, `0x4871b0` vs `0x4871a0`; meaning **[i]**) | `trail_sites_out.txt` §3, `0x00450730` [s] |
| piloted ship | `[obj+0x40] & 2`, set only by `SA_SetAsPlayerObject` (§10.10 Q1) | `0x0044ada1` |

* `2004::GetOwner` returns a member (`m41` stock, `m43` Mayhem). `__SetCoverRaceRecursive` re-sends that same owner
  id, so a cover race does not change the group **[i]**: the method was read only as far as the call.
* Other native writers of `+0x3c` were not traced (`0x0042ff39`, `0x004465a8`, `0x00448d0c`, `0x0044a1ce`,
  `0x004421d1` = −1, `0x004f7f90`), so their object kinds are unknown. A ship's group could in principle be rewritten
  by one of them **[i]**.
* **Answer.** A ship is player-owned iff `(int16)[obj+0x3c] == 10`. The player's piloted ship is `[obj+0x40] & 2`.
* **Constraint.** Read the group at each decision, because ownership changes through `SetOwner`, and before KC
  `Activate` the value is −1. Excluding both tests covers the player's own capitals and the piloted ship.

#### Item 1: two capitals on order 5 / order 8 at one gate [m]

| Fact | Rule | Site |
| --- | --- | --- |
| `SA_WarpObjectAway` (case 0x0e) | resolves ship and gate, requires class 7, then writes order 8, `f+0x218 = −1`, `f+0x214 = now + 900`, `f+0x240` = position, and for a class-0x12 target `[gate+0x40] \|= 0x2000000`, `[ship+0x40] \|= 0x400`, `f+0x224` = gate. **No test of the gate or of other ships; it cannot refuse** | `0x004612a7` [s], `0x004612cc` [s], `0x00461302` [s], `0x00461322` [s] |
| order 8 | per frame: `[gate+0x40] \|= 0x2000000`, `[ship+0x40] \|= 0x400`, kinematic position, speeds 0. At the deadline: `0x400` cleared, order 0, wake 1. No per-gate counter or owner | `0x00455252` [s], `0x0045520a` [s] |
| `0x2000000` on `[obj+0x40]` | setters: order 5 aligned `0x004548cf`, order 8 `0x00455252`, `SA_WarpObjectAway` `0x00461309`, pair loop `0x0045d811` (the player's `CanWarp` path **[i]**). **One reader**: the gate post-pass update `0x0044b0c0` at `0x0044b124`. Cleared for every object every frame by the post pass (mask `0xedffffff`) | sweep; `0x0044b124` [s], `0x0045b741` [s] |
| `0x0044b0c0` | counts the gate's flagged parts and steps the part animation `0x44aee0` every 150 ms. It runs the opening branch only while `[gate.f+0x180] < now`, and the other branch while `0 < [gate.f+0x180] − now < 40 s`. It changes no ship and no order | `0x0044b134` [s], `0x0044b30f` [s], `0x0044b155` [s] |
| `[gate.f+0x180]` | written by the arrival launch (`now + 10000`). Read only at `0x0044b134`/`0x0044b30f`. The other 12 non-indexed `+0x180` sites in `0x440000..0x46a000` belong to ship flight blocks or object init (`0x00459545` is the slew loop's carrier freeze on the ship's own `f+0x180`; `0x0045b7f3` is the post-pass stamp of non-gate classes) **[i]** attribution by address | `0x004441c3` [s]; sweep list in `trail_sites_out.txt` |

* **Answer: yes.** The engine has no per-gate exclusivity for orders 5 and 8. Two capitals can be on order 5 or order
  8 at one gate in the same frames, as small ships already are.
  * The per-gate writes are a per-frame animation flag, and the arrival stamp `[gate.f+0x180]` only delays the
    animation; nothing reads it before a launch.
  * The only serialiser is the KC through slot, one per gate pair (§10.2).
  * The pair filter is per ship (`0x0045c7b3..0x0045c7e3`).
* **Constraint for the abreast refinement.** It must hold or bypass the KC through slot for the second hull. `ReleaseSlot`
  `0x90c9c` with two holders was not studied here: it is KC, §8 item 1's remaining part. It must also keep the two
  900 ms launches clear of each other itself, because order 8 moves the ship kinematically along the axis from
  wherever its trigger fired (§10.5).

**Hook sites.** No new site and no new byte pattern. The implementation claims only the installed sites A, B, P2 (and
P4/P5), whose contracts are unchanged (§10.8, §10.11). `verify_ship_steering_sites.py` was not touched.

**Still inferred or open.**

* The meaning of group 10's scene callback.
* The untraced `+0x3c` writers.
* That `SE_ObjectExists` stays true after `Deactivate`. It does not change the result: a false would return 8 one
  statement earlier.
* The MSCI part of λ (§10.11 Q3, measured +4.1…+16.9 s).
* KC `ReleaseSlot` with two holders (Item 1).

```sh
python3 -I verification/results/ship-navigation-ai/gates/trail_sites.py > trail_sites_out.txt   # RESULT PASS
```

## 11. Docking approach

2026-10-13, static study of the same `X3AP.exe` (SHA-256 `fdbf3418…`) and both `x3story.obj` versions, for the
docking-approach design ("ships evading their own dock station"). Nothing was launched and no Wine command ran. Engine
facts come from capstone listings over the PE map and Ghidra 12.1.4 headless decompiles (read-only, under
`flock /tmp/x3-ghidra.lock`); raw listings and decompiles stayed in `~/x3-nav-study/dock/` (untracked).
[`dock_sites.py`](../../verification/results/ship-navigation-ai/docking/dock_sites.py) re-reads every quoted site, the
SA and order jump-table entries (175 checks, `RESULT PASS`,
[`dock_sites_out.txt`](../../verification/results/ship-navigation-ai/docking/dock_sites_out.txt)).
[`kc_dock_flow.py`](../../verification/results/ship-navigation-ai/docking/kc_dock_flow.py) prints the KC methods
([`kc_dock_flow.txt`](../../verification/results/ship-navigation-ai/docking/kc_dock_flow.txt)): stock and Mayhem
`__LandInHangar` differ only in `SetShield` vs `304::Add`, `__StartInHangar` only in Mayhem's `SafeUndock` call;
`CollisionWarn`, `__Idle` and `GetFormationSize` are equal. Mayhem member numbers are used below (`m47` = stock `m45`,
the `CollisionWarn` mode word; `m88` = the reserved port).

Symbols: `C` container centre (`phys+0x30`), `r_C` = `[cont+0xa4]` (bounding radius), `S`, `r_S` the ship, `N` the
port's path node (world), `P` the approach point, `R` the order-5 rendezvous, `f` = the ship's flight block. Worked
numbers use a 3 km station and the Mayhem TP medians (r_S 74 m, 160 m/s, 15 m/s²,
[game-scale-and-ship-classes.md](game-scale-and-ship-classes.md) §4); they are **[i]**.

**Answer.**

* **Approach point [m].** `P` lies on the port node's axis, outward from the node: the first candidate is
  `N + dir·T/2`, then steps of `2·r_S + 250000` (2 r_S + 500 m) until `|P − C| >= T`, with
  `T = r_C + 2·r_S + 250000` (`+ 1110000` for the undock exit point). `dir` is −z of the node frame (tilted 30° or 60°
  toward −y for port types flagged `0x800000` / `0x1000000`). So `P` is always outside the container's bounding sphere,
  by at least `2·r_S + 500 m`, and at least `T/2` out from the port. 3 km station, TP: `T` = 3648 m; a port on the
  sphere facing radially out gives `|P − C|` = 4.82 km **[i]**.
* **The straight line to `P` is not protected [m]+[i].** Nothing makes the ship→`P` line clear the station. The order-4
  detour (sphere tangent) engages only when the ship is within `2·r_C` of `P` and farther from `C` than
  `1.05·|P − C|`. A ship that starts order 4 on the far side (the KC far leg ends at `4·r_C`) can only meet that
  condition from inside the sphere, so it flies straight at the station until the warn fires.
* **Why a TP on order 4 warns on its own station [m].** The warn test has no own-target exemption on orders 4 or 5. A
  station is a mesh obstacle: the warn needs (1) centre distance `<= b + clamp(rA + rB/10, 30 m, 1 km) + rA·cosθ +
  2(rA + rB)` (7.2 km for the TP at a 3 km station), (2) the cone (θ < 66° at d = 4 km), and (3) the part-box test of
  `0x0044d9c0`: the ship is inside, or its forward ray hits, a part's model box inflated to `1.2·(half-extent + rA)`
  within reach `|v| + b` (760 m at 160 m/s). During order 4 `m47 = 0`, so `CollisionWarn` stops the ship.
* **Order 5 [m].** The engine still sets the warn flag, but the KC sets `m47 = 0xA0001` for the whole order-5 leg, so
  `CollisionWarn` takes no action. Within `2·r_S + 50 m` of the node the engine moves the ship kinematically: no leaf
  call, speeds zeroed, ends 3 m from the node. This is not order 0x20.
* **Carriers [m].** The same port code serves class-7 containers. Order 4 rebuilds `P` from the carrier's live pose
  at every re-plan, and order 5 rebuilds `N` and `R` every frame. Neither adds a velocity lead. Any ship on order 5 at
  a dock-capable container with bit `0x80000` forces that container's desired speed and turn commands to 0 every
  frame. Undocking does not. A carrier never warns against non-large ships.

### 11.1 Q1: the approach point and the port path [m]

**Port record (`[cont.f+0x1c8] + port·0x44`, count from `0x0043a8d0(class, subtype)`).**

| Field | Meaning |
| --- | --- |
| `+0x00` | index into the type's dock-port table `cont.f+0x1c0` (`[[+0x1c0]+4][idx]` → entry; entry `+4` = port type flags) |
| `+0x04..+0x18`, count `+0x1c` | animated sub-objects (doors), driven by `0x004522b0` |
| `+0x20..+0x2c`, count `+0x30` | path nodes: bodies in the container's model tree (local pose `+0x30`/`+0x40`, parent `+0x18`) |
| `+0x3c` | state: `1` order 5 in progress, `2` undock in progress, `0x10`/`0x20` player, `0x40` door state |

Port type flags used here: `0x20000` reversible axis, `0x100000` path-indexed node and no final zone, `0x200000` tube
launch on undock, `0x800000` / `0x1000000` tilted axis, `0x1800000` short final zone.

**Node choice `0x00451e70`** (ESI = record, EAX = path, ECX = ship type code, stack = container): with type flag
`0x100000` and path ≥ 0, `node[path % count]`. Otherwise by ship type: M3/M4/M5 take node 0, then node 1; TS, TS_P,
GO and TP take node 1; all others take node 2; failing those, the first non-null node (`0x00451ec0..0x00451f07`). The
node's world pose is composed live through the parent chain (`0x00488480` position, `0x004884f0` rotation).

**`0x00451f30`** is a fastcall wrapper (ECX = port, EDX = container; stack: ship, path, out, reverse flag, extra;
`ret 0x14`) around **`0x00451f80`** (EAX = ship; stdcall 9 args, `ret 0x24`):

| Step | Rule | Site |
| --- | --- | --- |
| margins | `T = r_C + (2·r_S + 250000) + extra` (25000 instead of `2·r_S` with no ship); step `= 2·r_S + 250000` | `0x00451fe5`, `0x00451fef`, `0x00451ff4` |
| axis | plain port: `−row 2` of the node frame (`+row 2` when reverse is requested and type flag `0x20000` is set); `0x800000`: local `(0, −0.5, ∓0.866)`; `0x1000000`: local `(0, −0.866, ∓0.5)`, rotated local→world | `0x00452029..0x004520d3` |
| first candidate | `N + dir·T/2` | `0x00452104`, `0x0045211b`, `0x00452128` |
| stepping | while `|P − C| < T`: `P += dir·step` | `0x00452146`, `0x00452198`, `0x004521a2`, `0x0045222b` |

**Frames.**

* `SA_FlyObjectBeforeContainer` (port ≥ 0, container flag `0x100000`) calls it with reverse 0 and extra 0. It stores
  `P − C` projected on the container's rows into `f+0x240`. That is a **container-local** offset (`0x4f0c00`,
  world→local, `0x00460d7a`, `0x00460d9c`). Order 4 re-composes it every re-plan with the live container rotation and
  position (`0x00454a29`, `0x00454a48`).
* With port < 0 the case uses the gate vector `[cont.f+0x1a0]·r_C`. A container without flag `0x100000` gets its
  centre.

**Order 5 points (`SA_FlyObjectIntoContainer`, `0x00460a58..0x00460bc3`):**

* Final point: `f+0x240 = N` (world).
* Rendezvous: `f+0x230 = R = N + 0.7·(P − N)`, where `P` comes from the same `0x00451f80` call (`0xb333` at
  `0x00460b50`).
* Both are world coordinates fixed at issue for a station. For a class-7 container the handler rebuilds them every
  frame (§11.6).
* The KC passes the same random path `p8 = SE_Random(10)` to both orders. The path matters only for `0x100000` ports.

**Order 5 at a port, handler `0x00453fc2` (non-gate path):**

| Phase | Condition | Engine action | Site |
| --- | --- | --- | --- |
| to the rendezvous | `d_N > |R − N| + r_S` | leaf `0x44ccc0` at `R` | `0x004541cd`, `0x004541e2` |
| along the axis | otherwise | leaf at `N + r_S·unit(N − R)` (a point `r_S` past the node, inward) | `0x00454218` |
| misaligned | `forward·dir(R)` (then `dir(N)`) `< 0.95` | desired 500 (1 m/s) | `0x00454897` |
| speed | aligned | `ramp(d_N; r_C'/2 → min(vmax, 20000), r_C' → min(vmax, cap))`, `r_C' = min(r_C, 1000000)`, cap 75000/50000 by class | `0x0045494c` |
| busy flags | every frame | port record `|= 1`; container `|= 0x10000000` | `0x0045438f`, `0x004543ad` |
| **final zone** | `d_N <= 2·r_S + 25000` (plain ports) | §11.5 | `0x004543da` |
| flagged port | type flag `0x100000` | gate rule: arrival at `d <= (|v|/10 + r_S)(1 + (tf − 1)/4)` | `0x004543cf`, `0x004546ce` |
| end | `d_N <= 1500` (3 m) in the final zone | speeds 0, port −1, order 0, result 1 | `0x00454510`, `0x00454700` |

So order 5 at a dock is **not** the gate test: it ends on 3 m to the node after the kinematic glide. Only `0x100000`
ports use the gate-style radius.

**Order 4 at a station:**

* Arrival radius: `clamp(3·r_S (r_S for "large"), 22200, 111000)`, 222 m for the TP. A class-7 container gets
  111000 and a gate 0.75 r (`0x00454b2e`, `0x00454b48`). The ship stops dead on arrival (`0x00454b91`, as for gates).
* Speed: floor = the container's current speed + 5000 inside `d_slow` (`0x00455060`), container flag `0x10000000`.
* Alignment slowdown within `d_slow + 3·r_C + 2·r_S` (`0x00455095`).

**The order-4 container detour, operands now read [m]** (corrects §3.4's **[i]**). With `u = unit(P − S)` and
`W = C − S`, the handler replaces `P` only if all five hold:

| # | Condition | Site |
| --- | --- | --- |
| a | `u·(P − C) > 0` | `0x00454c2c` |
| b | `|P − S| < 2·r_C` | `0x00454c41` |
| c | `|C − S| > 1.05·|P − C|` | `0x00454c68`, `0x00454c84` |
| d | the line `S + t·u` passes within `r_C` of `C` | `0x00454cef` |
| e | `|C − S| > r_C/20` | `0x00454d0a` |

The replacement is `P' = S + r_C·unit(m)` with `m = W × (u × W)` (the travel direction's component perpendicular to
the ship→centre line), its y component halved before normalising (`0x00454eb8..0x00454ef2`). It is recomputed every
re-plan and handed to `0x0044d020` (`0x00454f81`). It is a bounding-sphere detour; it knows nothing of the parts.

**Does the line to `P` cross the station? [i] from the rules above.**

* `P` itself is outside the sphere by construction.
* The KC far leg ends at `4·r_C` from `C` (§11.3). The detour needs `|P − S| < 2·r_C`.
* For a port on the far side, `|P − S| < 2·r_C` implies the ship is already within about `r_C − 0.6 km` of `C`,
  i.e. inside the sphere. The order flies straight through the station's volume and only the warn reacts.
* For a port at 90° to the approach bearing, the detour engages at `|S − C| < √(4r_C² − |P − C|²)` (3.6 km for the
  3 km station with `|P − C|` = 4.8 km), just outside the sphere.

### 11.2 Q2: the warn between a ship and its own container [m]

`0x0044e450(A, B, d)` → filter `0x0044d2f0` → `0x0044dd90`. Findings for B = the ship's `f+0x224` container:

* **No own-target exemption** on orders 4, 5, 0x14, 0x15 or 0x19. The only container skips are on **order 0x1e**: the
  container itself (`0x0044e4d6`), and every obstacle while `|A − C| <= 0.8·r_C` (`0x0044e4ec`, `0x0044e50d`). Order
  0x12 only refreshes the list (`0x0044e488`). Gates are not skipped either; for both gates and docks the order-5
  exemption is the KC's `m47 = 0xA0001`, not the engine's.
* **Mesh obstacles:** class 5/6, capital-type ships (`0x0043fd50` on B, `0x0044ddd5`) and the class-0x14 subtypes of
  §3.1.
* **Close range (corrects §3.1):** `d <= 1.2·(rA + rB)` returns true **only for non-mesh B** (`0x0044def1`). For a
  station or capital, `d <= rA + rB` goes to the mesh test instead (`0x0044e282`). The attack-target rule
  (`d <= rA + rB`) applies only on orders 0x10/0x11 and random-avoid 0x12.
* **Range gate:** false if `d > b + clamp(rA + rB/10, 15000, 500000) + rA·cosθ⁺ + 2·(rA + rB)` (`rA + rB` enters
  twice, once in `L` and once in the comparison).
* **Cone** (when `d > b`): `cos²θ > 1 − ((rA + rB + 20000)/(d − b))²`, and B must be ahead.
* **Mesh test `0x0044d9c0(B.body, |vA| + b, out, 1.2, 0)`** (thiscall ECX = A, recursive over the body's children
  `[body+0xc]`). Each part with a model (`[body+0x140] >= 0`, `0x004863c0`) yields an oriented box: half-extents =
  model box `[+0x20..+0x28]` × part scale `[body+0x70]`, plus `rA`, × 1.2. Its centre and rows are the part's world
  pose (`+0xb0`, world→local `0x4f0c00`). Hit if A's centre is inside a box. Otherwise the Euclidean distance to the
  box must be within the reach `|vA| + b`, **and** a slab test of A's forward ray (`flight+0x870` row 2, t ≥ 0) must
  meet the box. So the decision is part boxes, not the bounding sphere and not the collision mesh. The reach adds the
  speed in units/s to a distance, i.e. one second of travel **[i]** on intent.

**Numbers, TP at a 3 km station [i]:**

* `b = min((160·cosθ)²/30, 600) m`.
* Range gate: 7.2 km head-on.
* Cone: 66° at d = 4 km, 45° at d = 5 km.
* Mesh: forward ray within 760 m of a part box grown by `0.2·half-extent + 89 m`. For a part 2 km across that is
  289 m beyond the hull. `P`'s margin is `2·r_S + 500 m` = 648 m beyond the *sphere*. That covers the hull but not
  every box corner, so a ray toward `P` that grazes the station can hit.

**Which case fires in practice [i]:** order 4 aimed through or past the station (the far-side case of §11.1) gives a
forward ray into a part box within 760 m. The ship then stops (`CollisionWarn` default branch, `m47 = 0` during order
4). While stopped on arrival (`[obj+0x10] = [obj+0x14] = 0`) the test returns false (`0x0044dda2`).

### 11.3 Q3: the avoid order against a container, and the KC resume [m]

**Order 0x12 (§3.3), with the end rule and escape distance now read:**

* `SA_AvoidObjectCollision(ship, 0)` from `__LandInHangar` takes the default radius `f+0x260 = r_S + 150000`.
* Every 200 ms with obstacles still listed, the escape point is `S + dir·4440000` (8.88 km, `0x00457863`), and
  `f+0x230` is reset to the current position (`0x00457842`).
* A station counts as "must turn away": weight 50 for candidates with `dot <= −0.8`.
* The order ends once nothing warns any more *and* the ship is more than `f+0x260` (+ the random term) from
  `f+0x230` (`0x00456f2c`, `0x00456f73`). Each leg therefore ends `r_S + 300 m` past the last re-plan at which the
  station still warned.

**KC `__LandInHangar` loop (Mayhem `0xcf94e`, stock `0xb7c25`):**

```
loop (head cf9f3):  m47 = 0                                   CollisionWarn default: SA_StopObject(ship, 2)
  p0 = 0 unless (m87 == station, m88 >= 0, SA_IsObjectBeforeContainer(ship, station, m88))
  if !p0:
      SA_SetFlightMode(1); SA_SetSpeedLimit; SA_SetShipDockPortNr(m88); SA_SetDestination(station)
      SA_CollisionObject(ship) -> SA_AvoidObjectCollision(ship, 0); continue
      d > 5·size -> carrier (2033): SA_FollowObject(ship, c, 4·size, 0) | else SA_MovePos(ship, centre, 4·size); continue
      no port reserved -> SA_SetShipDockPortNr(-1); carrier: SA_EscortObject(slot, §11.4) | else __Idle(15000); continue
      p8 = SE_Random(10); r = SA_FlyObjectBeforeContainer(ship, station, m88, p8); r == 1 -> p0 = 1; continue
  else:
      m47 = 0xA0001 (CollisionWarn ignore); SA_SetFlightMode(0)
      r = SA_FlyObjectIntoContainer(ship, station, m88, p8)
      r == 1 -> SA_MoveObjectIntoContainer, SA_SetShipAtDockPort, Signal_Docked (or relaunch if a carrier bay is full)
      r == 7 -> p0 = 0;  r == 5 -> abort
```

**The resume after a warn [m]:**

1. `SA_StopObject(2)` wakes `FlyObjectBeforeContainer` with 2.
2. The loop finds bit `0x1000` still set and issues the avoid order.
3. When the avoid order ends, the loop re-issues `FlyObjectBeforeContainer` **from scratch**: same port, a new `p8`,
   and `P` recomputed. For a static station and a plain port, `P` is the same point **[m]**.
4. The ship again flies the straight line from wherever the avoid leg left it.

**Repeated legs [i]:** the cycle (warn → stop → 8.88 km escape heading away → re-issue → straight back) repeats until
an avoid leg happens to end where the line to `P` clears the station's part boxes, or where detour conditions a–e
hold. That is the mechanism behind the Run157 trace (six 0x12 legs, 56.5 s, own dock station, order 4).

### 11.4 Q4: holding while queued [m]

When `RequestDockPort(this, 0) < 0` within `5·size` (`cfc53..cfced`):

* **Carriers (KC class 2033):** `SA_EscortObject(ship, carrier, x, y, z, GetSize(), 15000)`. The slot is
  `x = U(−p2, p2)`, `y = U(−p2, p2)`, `z = −2·p2`, with `p2 = GetFormationSize() + carrier.GetSize()`.
  `GetFormationSize` is the largest member `GetFormationDistToLeader`, else `GetFormationDist = 4·GetSize()` of the
  ship. The slot is in the leader frame rotated by the carrier's yaw/pitch (order 0x14, §1.3), i.e. behind the carrier
  **[i]** (−z read as aft). Its distance from `C` is at least `2·p2 >= 2·r_C + 8·r_S`, so it is always outside the
  carrier's sphere. Escort pairs get half the braking distance against the leader and against co-escorts (§3.1).
  15 s timeout, precision = the ship's own size.
* **Stations:** no slot. `__Idle(15000)` issues `SA_Idle(ship, 60000)` (order 0x1d; `CollisionWarn` default). Order
  0x1d cruises to a wander point at least 2.2 km **ahead along the current heading** at vmax/2 (§10.2). A ship queued
  within `5·r_C`, having just flown toward the station centre, therefore heads on toward the station **[i]**. It meets
  the same warn → stop → avoid → re-idle cycle. That fits a TP circling a station with hundreds of 0x12 rows
  (Run160 `circ_cause.txt`) **[i]**. The engine has no holding pattern for stations. How long the in-sector
  `__Idle` waits (the 60 s order versus the 15000 argument) was not traced.

### 11.5 Q5: the final zone [m]

The boundary is confirmed: `d_N <= 2·r_S + 25000` (`0x004543da`), for ports without type flag `0x100000`. Inside it
order 5 does not call a leaf (it jumps past `0x00454745`). Every frame:

* `[obj+0x14] = [obj+0x10] = 0` and the drift velocity `[obj+0x60..0x6c] = 0` (`0x004543fc`).
* The turn commands `[obj+0x30..0x38]` are zeroed. The orientation is slerped toward the node's at
  `0x71c` (`0x38e` for some type codes) per second **[i]** (`0x0045451b..0x00454647`).
* While `d_N > 1500`, the **position is written directly**:
  * `S += (N − S)·step/d_N` (`0x004544f3`, `0x004544f8`);
  * `step = ramp(d_N; 0 → 500, L → min(vmax, 20000))·dt/1000`;
  * `L = 2·r_S + 25000` for type flag `0x1800000`, else `vmax²/(2a) + 10000`.
* Doors animate below 8880 units (`0x0045465d`). The order ends at 3 m.

This is not `SA_GlidePos` (order 0x20). `SA_MoveObjectIntoContainer` is the KC's step *after* result 1. It links the
ship into the container (`0x004497b0`, §10.10) and writes no position. For a `0x100000` port there is no final zone;
the ship flies to the gate-style radius and `SA_SetShipAtDockPort` parks it.

### 11.6 Q6: docking at a moving carrier [m]

| Item | Rule | Site |
| --- | --- | --- |
| code path | same as a station: `0x00451f30`/`0x00451f80`, node choice `0x00451e70` | §11.1 |
| order 4 point | container-local offset, re-composed with the carrier's **live** rotation and position at every re-plan (`rand(50·type[+0x50])` ms); no velocity lead | `0x00454a29`, `0x00454a48` |
| order 4 arrival | 111000 (222 m) for a class-7 container | `0x00454b2e` |
| order 4 speed floor | carrier's current speed + 5000 inside `d_slow` | `0x00455060` |
| order 5 points | **every frame** for a class-7 container: `f+0x240 = N` (live), `f+0x230 = N − (r_C/2)·row 2(node)`; the tilt flags and the 0.7·`P` rendezvous of stations are not used | `0x0045406a..0x00454135` |
| order 5 speed | the dock ramp of §11.1, no carrier-speed term | `0x0045494c` |
| final zone | kinematic step toward the **live** node, ≤ 40 m/s; drift velocity zeroed | §11.5 |
| carrier held still | speed loop `0x00459490`: a class-7 object with `[+0x40]` bits `0x100000` and `0x80000` and either `0x10000000` (set this frame by an approaching ship) or `now < f+0x180` gets desired speed 0 and turn commands 0 | `0x0045951f..0x00459558` |
| flag lifetime | the post pass `0x0045b720` clears `0x10000000` and `0x2000000` every frame and sets `f+0x180 = now` for non-gate objects | `0x0045b741`, `0x0045b7f3` |

Who sets `0x10000000` on the container:

* every frame of a docking ship's order 5 (`0x004543ad`, `0x004548ac`, `0x0045498f`);
* order 4 inside `d_slow`, on re-plan frames only (`0x00455083`);
* the player's order 0x1b near the station (`0x00455196`).

So a carrier that carries bit `0x80000` stops and stops turning while any ship is on order 5 to it.

Whether AI carriers carry bit `0x80000` is **[i]** open. It is the `SA_SetAutopilot` bit (§2.2), which the KC sets
from `IsAutopilotActive`. If they do not, nothing slows the carrier: order 5's ramp ends at 40 m/s, so a carrier
faster than that outruns the final glide **[i]**.

**Warn pairs at a carrier:**

* Fighter A on 4/5 → its carrier: mesh obstacle, no exemption. `m47` gives the same KC handling as at a station.
* Fighter → the carrier's escorts (order 0x14 to the carrier): ordinary obstacles. The pair exemption covers only B
  escorting or following *A* (`0x0044d2f0`). The braking halving needs A itself on 0x14/0x15.
* Carrier A → fighters: a capital A ignores non-"large" class-7 B entirely (`0x0044d2f0`). The carrier warns only on
  M6/TM/M8 and larger.
* The far leg to a carrier is `SA_FollowObject(ship, carrier, 4·size, 0)`: order 0x15, braking halved against the
  carrier.

### 11.7 Q7: undocking [m]

**KC `__StartInHangar` (Mayhem `0xcf2eb`).** The sequence is `RequestDockPort(this, 0x200000)` → door animation →
`SA_StartInHangar(ship, container, port)` → `SafeUndock()` (Mayhem) → `RemoveShip`/`AddShip` →
`SA_ClearObjectFromHangar(ship, container, m88)` with `m47 = 0x90002` (ignore) or `0x50003` (targeted with
`m48 = 0`, so it never stops **[i]**). Then `m47 = 0`, an avoid if bit `0x1000` is set, and `Flags2 &= ~8`.
`CollisionWarn` is therefore inert for the whole undock order.

**`SafeUndock` (`0xcf240`)** sets `Flags2 |= 0x20000` on the ship, then waits `TI_Delay(3000)` until more than
2000 ms have passed, then clears the bit. The body sits between markers `6f`/`70`, and the caller continues at the
end marker. The same pattern encloses `__LandInHangar`'s background abort loop, so it is read as a forked block
**[i]**. On that reading the 3 s window opens a few statements before `SA_ClearObjectFromHangar` and covers the first
≈3 s of order 0x1e. In that window the ship is out of the pair loop as B ([sector-collide.md](sector-collide.md) §3)
and the KC skips avoid orders.

**Exit point.** `SA_ClearObjectFromHangar` (`0x0046117e`, containers of class 5/6/7/0x12):

* order 0x1e, `f+0x224` = container, `f+0x230` = current position;
* `f+0x240 = 0x00451f30(port, path −1, reverse 1, extra 1110000)` (`0x0046123b`), in **world coordinates fixed at
  issue**;
* `T = r_C + 2·r_S + 2.72 km`. The axis is the approach axis (node −z, tilted per type), reversed to +z for ports of
  type flag `0x20000`.

**Order 0x1e handler `0x00457bc3`, non-gate containers:**

| Port type | Rule | Site |
| --- | --- | --- |
| tube (`0x200000`) | the ship's orientation is set to the node's; position `= N + row 2·50000·t/1000` (100 m/s along node +z, kinematic, no leaf); clearance `25000 + 4·r_S + 45000` (AI) | `0x00457e0c`, `0x00457f9c`, `0x00457ff8`, `0x00457e76` |
| tube, cleared | `[obj+0x10] = 50000`, desired `min(vmax, 50000)`. A carrier: drift velocity `[obj+0x60..] = carrier phys+0x100 + [carrier+0x60]` (the fighter **inherits the carrier's velocity**) and the order ends. A station: the ship coasts straight on until `|S − C| >= r_C` | `0x00457eb2`, `0x00457ed6`, `0x00457f0e` |
| plain | port record `|= 2`; within `2·r_S` of the node, stopped and facing inward (`forward·row 2 >= −0.45`): turn in place; then leaf `0x44ccc0` to the world exit point. Carrier: desired vmax. Station: ramp up to `min(vmax, 50000)` inside `r_C/2` | `0x0045809e`, `0x004580ef`, `0x0045811d`, `0x0045814e`, `0x00458199` |
| end | centre distance ship–container `>= r_C` (`0x00419220`), order 0, result 1 | `0x004581d7` |

**Into the carrier's path or its escorts [i]:**

* The exit point is a world point on the port axis, fixed when the order is issued. Nothing checks it against the
  carrier's motion or its escorts.
* For a moving carrier and a plain port, the ship steers at full speed toward a point the carrier may be overrunning.
  The engine's warn test skips the carrier always, and every obstacle while `|S − C| <= 0.8·r_C`; the KC ignores
  warns until order 0x1e ends.
* A tube launch leaves along the node's +z with the carrier's velocity added, so its relative path is the tube axis.

**Carrier side [m]:**

* The carrier gets no warns against launching or landing fighters: a capital ignores non-large ships (§11.6). That is
  the vanilla counterpart of our amendment-15 `r/3` rule.
* Undocking writes no container speed or flag: order 0x1e sets only the port record bit `2`. So a carrier does not
  stop for launches.
* It does stop for landings, through the order-5 flag of §11.6, subject to the `0x80000` condition.
* The dock post pass `0x0044b750` (container port state; it calls `0x00451f80` three times) was not examined.

### 11.8 What the leaf hook (site A) sees during docking [m]

| Phase | Site A call | Point |
| --- | --- | --- |
| order 4 | `0x0044d020` (`0x00454f81`), at the re-plan rate | `P` (live for carriers) or the sphere detour `P'` |
| order 5, outside the final zone | `0x0044ccc0` (`0x00454745`), every frame | `R`, then `N + r_S·axis` |
| order 5, final zone | none | kinematic position writes |
| order 0x1e plain port | `0x0044ccc0` (`0x00458160`, `0x004581c5`) | the world exit point |
| order 0x1e tube | none | kinematic position writes |
| holding | order 0x14 (carrier slot) or 0x1d (station idle wander) through their own leaf calls | slot / wander point |

The order-5 and 0x1e end tests read `f+0x240`/`f+0x230` and centre distances, not the leaf point (as at gates,
§10.10), so a replacement leaf target never moves a trigger.

**Corrections to earlier sections.**
* §3.1: the close-range return applies to non-mesh obstacles only; for stations and capitals `d <= rA + rB` goes to the
  part-box test. `rA + rB` enters the range gate twice. The mesh test is per-part oriented boxes plus a forward ray,
  not a collision mesh.
* §3.4: the detour's operand roles are measured (table in §11.1). The 2·r_C bound is on the ship's distance to `P`.
* §4 "AI docking": the holding slot exists for carriers only; stations use `__Idle` (order 0x1d). Order 5 at a
  plain port ends at 3 m from the node after a kinematic final zone.
* §7: `0x0044d9c0` internals are now read (§11.2).

**Still inferred or open.**
* Whether AI carriers carry `[obj+0x40] & 0x80000`, the condition for the carrier stop rule. A row at `0x00459533`
  (carrier, flags, approaching count) would settle it.
* The fork reading of KC markers `6f`/`70` (`SafeUndock` window timing).
* How long the in-sector `__Idle(15000)` waits.
* The slerp rate unit in the final zone.
* `0x0044b750`.
* The part-box sizes of real stations, which decide how often a ray toward `P` grazes. A `ship_dock_warn` row at
  `0x0044e52b` filtered to `B == f+0x224` (order, d, the part hit from `out`, `|P − C|`, `|S − C|`) would measure the
  far-side case directly.

```sh
python3 -I verification/results/ship-navigation-ai/docking/dock_sites.py      # RESULT PASS (175 checks)
python3 -I verification/results/ship-navigation-ai/docking/kc_dock_flow.py    # KC docking methods, stock + Mayhem
```
