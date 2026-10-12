# Ship movement: the command pass, the steering law and the integrator

2026-10-12. Static study of X3AP.exe, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`
(2,153,984 B, `/mnt/c/X3/X3AP.exe`, re-hashed this session); nothing was
launched. Structure comes from Ghidra 12.1.4 headless (project
`~/x3-ghidra-research`, `X3Render`, `-readOnly -noanalysis`); raw decompiler
output stayed local under `~/x3-nav-study/movement/`. Every byte string quoted
below is re-read from the file through the PE section map by
[`check_sites.py`](../../verification/results/ship-movement/check_sites.py)
(121 byte strings, the decoded jump tables and the hook-site scans;
[output](../../verification/results/ship-movement/check_sites.txt): `RESULT PASS`).
Markers: **[m]** read from the image bytes or decoded tables (and re-checked by
that script), **[s]** static reading of the decompiler output, not re-read
byte for byte, **[i]** inference.

This is the engine side of the planned steering replacement (smooth,
curvature-limited steering with lookahead, predictive avoidance). The script
side that issues the commands and the game-scale numbers of the type tables are
separate studies; §6 and §7 give only what the engine needs from them.

**Answer.** Movement is split across two per-sector passes and one shared
controller:

1. `0x00452ad0` (pass A, *command pass*) runs each ship's current native flight
   command (word `flight+0x258`, written by the `SA_` script module), which
   calls a **steering leaf** that turns a target point into three *remaining
   angles* (`obj+0x30/+0x34/+0x38`, yaw/pitch/roll) and sets a desired speed
   `obj+0x14`; its tail slews the current speed `obj+0x10` toward it at the
   ship's acceleration.
2. `0x004596e0` (pass B, *integrator*) consumes the remaining angles through
   the per-axis controller `0x0043ad20` (rate cap and angular-acceleration cap
   from the type record), rotates the orientation matrix, sets the velocity to
   the forward axis times `obj+0x10` and adds `velocity × dt` to the integer
   position.

The steering law proper is three leaf functions (`0x0044ccc0`, `0x0044c620`,
`0x0044c970`) plus the dispatcher `0x0044d020` that chooses among them. They are
the only routines that derive the remaining angles from a target (the command
cases otherwise only zero or patch them), are reached from exactly 33 call
sites in `0x00452ad0` (and 3 from the dispatcher), and share one register
contract, so **the steering law is replaceable as one
contract at three 5-byte entry hooks** (§11). It is *not* one routine: the
speed law (when to slow, when "arrived") lives inline in each of the ~20
command cases of `0x00452ad0` and is not replaceable without per-case work.

## 1. Call chain and time step

```
main loop 0x00403840
  0x00403b17 call 0x0043a360                       per-sector driver (main-loop-input-region.md §2)
    pass A, for each active sector (class word 1):
      0x0043a38e call 0x0045d250                   sector_collide (sector-collide.md)
      0x0043a394 call 0x00452ad0                   COMMAND PASS: commands, steering, speed slew
      0x0043a39a call 0x0045b720                   media-cue selector
    pass B, for each active sector:
      0x0043a3bf call 0x004596e0                   INTEGRATOR: rotation, velocity, position
      0x0043a3c4 call 0x004526b0                   attach/transform pass
```

Both passes are `void __stdcall f(sector*)`, `ret 4`, single caller **[m]**
([main-loop-input-region.md](main-loop-input-region.md)). Collision therefore tests positions integrated in
the previous frame; commands and steering see the same positions; integration
runs after every sector's pass A **[s]**.

| Clock | Where | Value |
| --- | --- | --- |
| frame dt | `*0x00606f34+0x714`, written by the game clock tick `0x004ee1e0` as the delta of game time `+0x718`, forced to 1 when outside 1..0x7fff ms **[m]** | ms |
| command-pass dt | `0x00452adc` `a1 34 6f 60 00 8b 80 14 07 00 00 3d e8 03 00 00`: frame dt clamped to 1000 ms **[m]** | ms |
| integrator dt | `0x004597dd` `2b b3 a8 00 00 00`: per object, `time - [obj+0xa8]`, forced to 1000 when > 1000 or < 0 **[m]**; `[obj+0xa8] = time` after the object is integrated **[s]** | ms |
| think tick | `flight+0x210` (next decision time) and `flight+0x20c` (interval); most commands re-plan only when `time >= flight+0x210`, then set `flight+0x210 = time + rand()·interval/0x8000` (`0x004ee1c0`) **[m]**. The interval is TShips reaction delay × 50 ms, set at ship creation `0x0044049d` `8b 4c 07 50 6b c9 32 89 8a 0c 02 00 00` **[m]** | ms |

Every row of the TShips table examined has reaction delay 1 (521 of 521,
[tships_columns.txt](../../verification/results/ship-movement/tships_columns.txt)),
so a ship re-plans within 0–50 ms, i.e. every 1–3 frames at 60 fps **[i]**.
Commands with a think gate: FlyObjectBeforeContainer (`0x004549e3`), MovePos
(`0x004564d9`), MovePosContinue (`0x004565a8`), Escort (`0x0045824c`),
Follow (`0x00458b28`) and others; AvoidObjectCollision uses a fixed 200 ms
(`0x00456798` `add ecx,0xc8`); docking (command 5) has no gate and steers
every frame **[m]** (`0x00453fc2`–`0x004549c2` contains no `+0x210` reference).

## 2. Fields

Object (`obj`, class word `[obj+0x48]`, ships are class **7**, missiles 10,
bullets 0; class 7's type table is `*0x00606fd4` = `0x00606fb8 + 4·7`; every
type record is `0xdb8` bytes) **[m]**:

| Field | Meaning | Unit | Evidence |
| --- | --- | --- | --- |
| `+0x10` | current speed | position units / s | SA_GetSpeed `0x00462909` **[m]**; written by the slew `0x004595a3` **[m]** |
| `+0x14` | desired speed | same | SA_SetDesiredSpeed `0x004641f5` **[m]**; every command case **[s]** |
| `+0x18/+0x1c/+0x20` | orientation as Euler angles alpha/beta/gamma (yaw, pitch, roll) | binary angle, 65536 = one turn | SA_GetRotAlpha/Beta/Gamma `0x00462120`/`0x00462147`/`0x00462210` **[m]**; refreshed from the matrix by `0x004f0640(obj+0x18, obj+0x20)` after each rotation **[s]** |
| `+0x24/+0x28/+0x2c` | current yaw/pitch/roll rate | binary angle / s | SA_SetRotSpeed `0x0046329f` **[m]**; integrator **[s]** |
| `+0x30/+0x34/+0x38` | **remaining** yaw/pitch/roll angle still to turn (steering output, consumed by the integrator) | signed binary angle, ±0x8000 | leaves §5 **[m]**; integrator subtracts the per-frame delta **[s]** |
| `+0x40` | flags: `0x80000` **autopilot** (error-driven rotation; set/cleared by SA_SetAutopilot `0x00463e88`/`0x00463ea7` **[m]**), `0x2` the player's ship (gates the odometer and auto-level) **[i]**, `0x200`/`[+0xf8]` attached/docked (no integration) **[m]** | | |
| `+0x50` | class payload; for ships the **flight block** below | | |
| `+0x60/+0x64/+0x68` | slip velocity (lateral momentum, decays) | units / s | `0x0045a610`–`0x0045a63b` **[m]** |
| `+0x70` | physics block | | |
| `+0x7c/+0x80` | module index and waiting script context of the running command; `0x00460560` calls the module's completion callback and clears them **[s]** | | |
| `+0xa4` | collision radius | position units | sector-collide.md |
| `+0xa8` | time of last integration | ms | §1 |

Physics block (`p = [obj+0x70]`): `+0x30/+0x34/+0x38` integer position;
`+0x40` orientation, 3 rows of 4 dwords (stride `0x10`), 16.16, row 2 (`+0x60`)
is the forward axis **[m]** (velocity build §3); `+0x70` node bounding radius
(SA_GetSize `0x0046288b`) **[m]**; `+0x100/+0x104/+0x108` velocity, units/s **[m]**.

Flight block (`f = [obj+0x50]`, class 7 only) **[m]** unless marked:

| Field | Meaning |
| --- | --- |
| `+0x20c`, `+0x210` | think interval, next think time (§1) |
| `+0x224` | command target object (leader, station, gate, attack target) |
| `+0x230..+0x23c` | docking rendezvous point (SA_FlyObjectIntoContainer `0x00460b8b`) |
| `+0x240..+0x24c` | command target point: MovePos position, Escort **offset**, docking port, evasion point |
| `+0x250`, `+0x254` | docking port index (−1 none), port data |
| `+0x258` | **command word** (short), §6 |
| `+0x260` | command distance parameter: MovePos arrival radius, Follow distance, Escort range ×10 |
| `+0x264`, `+0x284` | DefensiveMove manoeuvre choice (0/1/2 → `0x0044d020`/`0x0044c970`/`0x0044c620`) and extra roll **[s]** |
| `+0x270` | rotation tuning, 16.16 fraction added to 1 (SA_SetExtraRotSpeed: n/10) |
| `+0x274` | engine tuning, 16.16 fraction added to 1 (SA_SetExtraSpeed: n/10) |
| `+0x278` | speed limit, −1 none (SA_SetSpeedLimit) |
| `+0x27c` | max-speed override, −1 none (SA_SetMaxSpeedOverride) |
| `+0x280` | max strafe-speed override |
| `+0x28c/+0x290` | command timeout flag and deadline |
| `+0x2b0` | rotation-acceleration factor, 16.16, default 1.0 (SA_SetShipRotAccel: p/100; raised to ≥ 0.7 during evasion, `0x004578c1` **[m]**) |
| `+0x870..+0x89c` | *flight basis*: the orientation the steering and velocity use = physics `+0x40` × attitude offset **[s]** |
| `+0x8a0..+0x8a8`, `+0x8ac..+0x8b4`, `+0x8b8..+0x8c0` | attitude offset angles, their rates, their targets (SA_SetFlightMode worker `0x00450540` writes the targets) **[m]** writers, **[s]** use |

## 3. The integrator `0x004596e0`

Per object of the 32 buckets, skipping deleted (`0x8000000`), flag
`0x800000` (speed recomputed from `|p+0x100|` instead), `[obj+0x44] & 0x2000000`,
attached (`0x200`) and docked (`[obj+0xf8] != 0`) objects **[m]** (`0x0045980a`,
`0x00459900`–`0x00459920`):

**Rotation.** At `0x00459926` `f7 c1 00 00 08 00` (`test ecx,0x80000`) **[m]**:

* *autopilot set*: for each axis with remaining angle `e` ≠ 0, call
  `0x0043ad20(|e|, ±rate, max_rate, accel, &new_rate, &delta, dt)` (stdcall,
  `ret 0x1c` at `0x0043ad58` **[m]**), sign-mirrored for negative `e`; then
  `delta = min(delta, |e|)`, `e -= delta`, `rate = new_rate` **[s]**.
  `max_rate` = `0x00450d60` (yaw), `0x00450cf0` (pitch), `0x00450dd0` (roll):
  record `+0x08/+0x0c/+0x10` × (1 + `f+0x270`) **[s]**.
  `accel = max_rate × k`, `k = f+0x2b0 × record+0xd0 / 100` for ships
  (`0x00459943`–`0x00459972` **[m]**), `k = 1.0` otherwise (`0x00459978` **[m]**).
  The same controller drives the attitude offset `f+0x8a0..` toward its target **[s]**.
* *autopilot clear* (manual rates, from SA_SetRotSpeed or, presumably, the
  player's input **[i]**): `delta = rate × dt / 1000` per axis, no cap here **[s]**.
* Player ship with universe flag `[root+0x3c] & 2` and pitch within ±45°: the
  roll error is forced toward the stored level roll `[root+0x5c]` (auto-level) **[s]**.

`0x004f0270(dyaw, dpitch, droll)` builds a rotation matrix from three binary
angles (`× 1/65536 × 2π`, constants `0x005654e0`, `0x005654f8` **[m]**); it is
multiplied into `p+0x40` (`0x004f17f0`), re-orthonormalised (`0x004f15d0`) and
converted back to `obj+0x18..+0x20` **[s]**. The rotation is applied in the
ship's local frame **[i]** (matrix order not traced).

**Velocity and position** **[m]**:

```
0045a590  8b 43 10 8b 94 0e 20 ff ff ff   speed = [obj+0x10]; row2[k] of the basis
0045a5aa  f7 ea 05 00 80 00 00 83 d2 00 0f ac d0 10   16.16 multiply
0045a5bb  89 04 11                         p+0x100+4k = row2[k] * speed
0045a5d4  66 83 b8 58 02 00 00 05          (ships, command != 5, speed != 0:)
0045a619  03 84 24 90 00 00 00             slip += old velocity minus its new-forward component
0045a69a  8b 4b 60 03 88 00 01 00 00       v = slip + p+0x100
0045a6e9  e8 32 7f fb ff                   d = v * dt / 1000 (0x00412620, 64-bit, truncating)
0045a70d  8b 08 03 ca                      p+0x30 += d (x, y, z)
0045a74e  b9 d0 07 00 00 2b 4c 24 24       slip *= (2000 - dt) / 2000
```

So the ship always moves along its current nose at `obj+0x10`, plus a slip
term: on every frame the lateral part of last frame's velocity (measured in the
new orientation) is added to `obj+0x60` and decays by `(2000 − dt)/2000` per
frame, a time constant of about 2 s **[i]**. There is no lateral thrust, no
velocity-space steering and no momentum beyond the slip term **[s]**.

## 4. The per-axis controller `0x0043ad20`

Arguments: remaining angle `e ≥ 0`, current rate `w`, cap `W`, angular
acceleration `a` (forced ≥ 1), outputs `w'` and the angle turned this frame,
`dt` in ms converted to 16.16 seconds `t` **[s]**:

* `W == 0`: `w' = 0`, nothing turns.
* `w < 0` (turning away): decelerate at `a` (and stop within the frame if possible).
* braking test: if `e < w·(w/a)` (= `w²/a`, **twice** the true stopping
  distance `w²/2a`), decelerate at `a`;
* else accelerate at `a` up to `W`, cruise at `W`, with the frame split at the
  transition points.

It is a time-optimal-style trapezoid that brakes early; it never overshoots
(the caller clamps the delta to `e`). Offline model of this arithmetic
([axis_controller_sim.py](../../verification/results/ship-movement/axis_controller_sim.py),
[output](../../verification/results/ship-movement/axis_controller_sim.txt),
60 fps, one command, settle = remaining < 0.5°) **[i]**:

| Ship (TShips row) | `W` | `k` | 10° | 90° | 180° | peak rate at 180° |
| --- | --- | --- | --- | --- | --- | --- |
| TL, rot X 0.0375, col 51 = 10 | 13.5°/s | 0.10 | 5.5 s | 18.7 s | 27.0 s | 12.8°/s |
| TS, 0.058594, 30 | 21.1°/s | 0.30 | 2.5 s | 8.7 s | 13.0 s | 21.1°/s |
| M4, 0.359375, 50 | 129.4°/s | 0.50 | 0.8 s | 2.7 s | 3.9 s | 88.8°/s |

The three axes are independent; nothing couples them or shapes the path.

## 5. The steering law

### 5.1 Contract of the three leaves **[m]**

| Leaf | Entry bytes | Writes | Sequencing |
| --- | --- | --- | --- |
| `0x0044ccc0` yaw+pitch | `83 ec 18 8b 02` | `+0x30` yaw, `+0x34` pitch, `+0x38` = 0 (`0x0044cffd`) | pitch forced 0 while \|yaw\| > 90° (`0x0044cfe7` `81 fe 00 40 00 00 7f 08 81 fe 00 c0 ff ff 7d 02 33 c0`) |
| `0x0044c620` roll+yaw | `83 ec 18 8b 02` | `+0x30` yaw, `+0x34` = 0, `+0x38` roll (`0x0044c94c`) | yaw forced 0 while \|roll\| > 45° (`0x0044c93f` `3d 00 20 00 00`) |
| `0x0044c970` roll+pitch | `83 ec 18 8b 02` | `+0x30` = 0, `+0x34` pitch, `+0x38` roll (`0x0044cc9e`) | pitch forced 0 while \|roll\| > 45° (`0x0044cc91`) |

Calling convention (all three): `EDX` = pointer to the target point
`int[3]` in world position units, one stack argument `obj`, callee pops
(`ret 4` at `0x0044d011`, `0x0044c960`, `0x0044ccb2`), `sub esp,0x18` then
pushes EBX/EBP/ESI/EDI and restores them; x87 used internally (`fpatan`,
`fsin`, `fcos`, ftol `0x0052b5d0`). Inputs read: `p+0x30..+0x38`, the basis
(ships `f+0x870`, others `p+0x40`). Output: only `obj+0x30/+0x34/+0x38`.

Algorithm **[s]**: target minus position; if all three components are below
100 units the leaf returns without writing (`0x0044ccc0` writes zeros); else
the direction is taken into the local frame (dot with the basis rows),
normalised (`0x004effc0`), and the angles come from `fpatan` of local
components. The roll leaves choose the roll (±90°, the shorter way, via
`0x4000 < r < 0xc000 → r −= 0x8000`) that brings the target into the plane of
the faster of yaw/pitch, then turn on that axis only after the roll is within 45°.

### 5.2 The dispatcher `0x0044d020` **[m]**

`void __stdcall steer(obj, const int *target)`, `ret 8`, EBP frame with 16-byte
stack alignment (`55 8b ec 83 e4 f0 83 ec 14`):

* target within 100 units on every axis → choose by rates;
* else if `dot(normalised direction, forward row) >= 0xff3b` (cos 4.4°,
  `0x0044d0fc` `3d 3b ff 00 00`) → `0x0044ccc0`;
* else choose by record rates `r8, rC, r10` (yaw, pitch, roll; `0x0044d115`):
  if the slower of yaw/pitch is strictly faster than roll → `0x0044ccc0`;
  else if pitch is the slower → `0x0044c620` (bank, then yaw); else →
  `0x0044c970` (bank, then pitch). The three sample rows of §7 all have
  yaw = pitch > roll, so ordinary ships take `0x0044ccc0` here **[i]**.

Most commands bypass the dispatcher: MovePos calls `0x0044ccc0` whenever the
yaw rate is non-zero (`0x0045651b` `e8 40 a8 ff ff 85 c0 74 0a 53 8b d7 e8 94 67 ff ff`)
and the dispatcher only otherwise; docking, undocking and the escort's
close-in branch call `0x0044ccc0` directly. Call sites: `0x0044ccc0` 17 rel32 calls (16 in
`0x00452ad0`, 1 in the dispatcher), `0x0044c620` 2, `0x0044c970` 2,
`0x0044d020` 15, all in `0x00452ad0` **[m]**. `0x0044ccc0` also steers
missiles (class 10, `0x004534f3`, `0x00453726`) and class 0x13 (`0x00453bd7`)
**[m]** call sites, **[s]** class attribution (by handler address range).

### 5.3 What the law is, in control terms

* **No lookahead, no curvature limit, no smoothing.** The steering output is a
  pure bearing error to a single point; the dynamics are only the per-axis
  trapezoid of §4. With re-planning every 0–50 ms (§1) it is effectively a
  closed-loop bearing controller **[i]**.
* **Speed is not coupled to the bearing** for ordinary flight. MovePos sets the
  desired speed from distance alone (§6), so a ship flies its current nose at
  speed while it turns: the path is a turning circle of radius `speed / turn
  rate`, not a turn in place **[s]**. A TS at 124 m/s turning at 21°/s has a
  turning radius of about 340 m, a TL at 113 m/s and 13.5°/s about 480 m **[i]**
  (speed scale from §7).
* **Turn-then-thrust exists only on the docking approach:** `0x00454897`
  `3d 33 f3 00 00` (forward·direction < 0.95, 18.2°) → `0x004548a5`
  `c7 43 14 f4 01 00 00` desired speed 500 (≈ 1 m/s **[i]**) until facing **[m]**.
  No other `mov [obj+0x14], imm` gated on a bearing exists in `0x00452ad0`
  (only this one and a 50,000 cap at `0x004550f9` in command 4) **[m]**.
* **Sequencing** gives the abrupt look: yaw before pitch beyond 90°, bank before
  turn beyond 45° (§5.1), each axis at its own trapezoid **[m]**/**[s]**.
* **Dead band**: 100 position units per axis around the target **[m]**.
* **Arrival deceleration** is per command (§6), a linear ramp of desired speed
  with distance; the speed slew then follows at the ship's acceleration.

## 6. Commands, speed law and arrival

The SA_ module (dispatcher `0x00460630`, registered at `0x004344c7` in the
module table `*0x006085e4`, 378 names at `0x0057a7f0` (NULL-terminated), case table `0x004690f0` with `cmp ebx,0x179` at `0x0046065a`)
writes the command word **[m]**; `0x00452ad0` dispatches it through
`0x00453ce0` `0f bf 86 58 02 00 00 83 f8 24` and the tables `0x00459694`/`0x0045963c` **[m]**:

| Word | SA_ command (case) | Handler | Steering | Desired speed / arrival |
| --- | --- | --- | --- | --- |
| 0 | SA_StopObject (250) | `0x00458f8e` | none | rates decay unless autopilot **[s]** |
| 4 | SA_FlyObjectBeforeContainer (10), …Pos (12) | `0x004549c2` | `0x0044d020` | max speed, ramps near the target; done when nearer than `clamp(k·r_own, 22200, 111000)` (k = 3, or 1 when `0x0043fd00` holds; 111000 for a ship target, ¾ of the radius for a gate; `0x00454b48`, `0x00454b5d`) **[m]** constants, **[s]** rule |
| 5 | SA_FlyObjectIntoContainer (9) | `0x00453fc2` | `0x0044ccc0`, roll match | §9 |
| 6 | SA_TurnToObject (22) | `0x004554af` | | done when facing within cos `0xfd70` (8.1°) **[s]** |
| 8 | SA_WarpObjectAway (14) | `0x004551fe` | | |
| 0x10/0x11/0x18 | SA_AttackObject(Big) (17/18), SA_CaptureObject (16) | `0x00455e8c` | `0x0044d020` (`0x004560f7`, `0x00456343`, `0x004563de`, `0x00456482`) | not read in detail |
| 0x12 | SA_AvoidObjectCollision (15) | `0x00456722` | `0x0044ccc0`/`0x0044d020` to an evasion point in `f+0x240` | re-plans every 200 ms; obstacle list `f+0x1e0` (count), `+0x1e4..`, `+0x1f8..` **[s]** |
| 0x14 | SA_EscortObject (20) | `0x0045820d` | `0x0044d020`/`0x0044ccc0` to the slot | §8 |
| 0x15 | SA_FollowObject (27) | `0x00458aea` | | distance `f+0x260` |
| 0x16 | SA_FollowObjectByCut (28) | `0x00458e93` | | |
| 0x19 | SA_MovePos (30) | `0x004564cd` | `0x0044ccc0` (yaw rate ≠ 0) else `0x0044d020`, target `f+0x240` | ramp below |
| 0x1d | SA_Idle (26) | `0x00453cfe` | | |
| 0x1e | SA_ClearObjectFromHangar (13) | `0x00457bc3` | `0x0044ccc0` (`0x00457d2e`, `0x00458160`, `0x004581c5`) | undock |
| 0x1f | SA_RotateToAngles (29) | `0x0045920e` | | |
| 0x20 | SA_GlidePos (32) | `0x00459141` | | |
| 0x22 | SA_DefensiveMove (19) | `0x00455696` | leaf chosen at random (`f+0x264`) | |
| 0x23 | SA_MovePosContinue (31) | `0x0045659c` | as MovePos | does not stop at the point |
| 0x24 | SA_CollectDebris (375) | `0x00455ae5` | | |

All mapping rows are **[m]** (writer instruction in the SA_ case, handler from
the table; `check_sites.py` checks ten of them); the steering and speed columns
are **[s]** except where an address is quoted.

MovePos, the plain "fly to position" **[m]**:

```
0045654b  68 88 13 00 00        y0 = 5000 at distance 0
0045655c  e8 cf 34 01 00        x1 = brake distance = vmax * vmax / accel  (0x00469a30)
00456568  e8 b3 95 fd ff 89 43 14   desired = ramp(distance; 0 -> 5000, x1 -> vmax)  (0x0042fb20)
00456576  3b b0 60 02 00 00     if distance <= f+0x260 (script radius):
00456584  89 4b 14              desired = 0, command = 0, completion callback 0x00460560
```

The distance is `0x0042f850`, a float32 Euclidean distance **[m]**. The ramp
uses `v²/a`, again twice the physical stopping distance, so a ship arrives at
5000 units/s and stops inside the radius only if the script radius covers the
remaining slew **[i]**.

**Speed slew** (tail of `0x00452ad0`, classes 7, 0x13 and 10) **[m]**:
desired clamped to `0x00450980` (`0x004594ef`) and to `f+0x278` when ≥ 0
(`0x0045950d`); then `obj+0x10` moves toward `obj+0x14` by
`accel × dt / 1000` (`0x00459562` call `0x00450b80`, `0x0045956c` `/1000`),
at least 1 unit (`0x0045958e`), symmetric for braking.

**Caps**: max speed `0x00450980` = record `+0x44` (`0x0043a6a6`) × (1 +
`f+0x274`), overridden by `f+0x27c` ≥ 0 **[m]**; reduced by hull damage
unless `[obj+0x40] & 0x40000000`: a ramp from 30 % of the speed at 10 % hull
to full speed at 90 % hull, current hull `[obj+0xec]`, type maximum from
`0x0044e910` (ships: record `+0xb8`, TShips column 46) (`0x00450a20`,
`0x00450a3a`, `0x00450a53`, `0x00450a6e`) **[m]**; further by a timed factor
`f+0x914` until `f+0x918`, by `f+0x278` when the short `[obj+0x3c]` is 10
(meaning not established), and capped at 20000 in sectors with
`[sector+0x44] & 0x8000` **[s]**; acceleration `0x00450b80` = record `+0x48` ×
(1 + `f+0x274`) (`0x00450bc6`, `0x00450be3`) **[m]**.

## 7. Type-record fields (TShips) and runtime scaling

The loader `0x00434e40` fills a `0xdb8`-byte record per row; the common header
is columns 0–6, ship columns start at `+0x44` (`0x004373eb`) **[m]**. Decimal
columns go through `0x004ec490`, which multiplies by 65536.0 (`0x004ec5ce`
`dc 0d c0 54 56 00`, constant `0x005654c0`) **[m]**.

| TShips column | Record | Runtime use | Scale |
| --- | --- | --- | --- |
| 2 rotation X | `+0x08` | yaw rate cap `0x00450d60` | decimal × 65536 = binary angle/s, so the column is **turns per second** **[m]** scale, **[i]** axis name |
| 3 rotation Y | `+0x0c` | pitch rate cap `0x00450cf0` | same |
| 4 rotation Z | `+0x10` | roll rate cap `0x00450dd0` | same |
| 5 ship class (`SG_SH_*`) | `+0x14` | docking speed class switch (`0x00454280`ff.) | enum via `0x004ea440` |
| 7 speed | `+0x44` | max speed `0x0043a690` | position units/s × (1 + tuning) |
| 8 acceleration | `+0x48` | `0x00450b80` | units/s² × (1 + tuning) |
| 10 reaction delay | `+0x50` | think interval × 50 ms (`0x0044049d`) | ms |
| 51 rotation acceleration | `+0xd0` | angular accel = rate cap × col/100 per s (× `f+0x2b0`) | %/s; default 100 for table versions < 0x1b (`0x004376f1`) |

Yaw/pitch/roll naming: `+0x30` pairs with `+0x24`, `+0x08`, alpha (SA_GetRotAlpha);
the auto-level code (§3) treats `+0x38/+0x2c/+0x10` as roll; the TL row has
X = Y > Z, as expected for yaw = pitch > roll **[i]**. Size is not a TShips
column: the collision radius `obj+0xa4` and node radius `p+0x70` come from the
model **[s]**.

Cross-check against `/mnt/c/X3/ZMap/TShips.txt` (version 50, 521 rows; whether
it equals the packed AP table is for the type-table study):
[tships_columns.py](../../verification/results/ship-movement/tships_columns.py).
Units: one position unit is 1/500 m **[i]** — the player odometer adds
`|speed|·dt/1000` to `[root+0x50]` and carries every 500,000 into `[root+0x54]`
(`0x0045a641` ff.) **[s]**, and TShips speeds of 56,500 and 62,000 then read as
113 and 124 m/s for the Mammoth and Mercury **[i]**.

## 8. Formation, follow and escort are native

SA_EscortObject (case 20) writes command 0x14, the leader to `f+0x224`, an
offset vector `x, y, z` to `f+0x240..+0x248` and a range parameter to `f+0x260`
(`0x004618e3`–`0x0046191f`) **[m]**. Each think tick the slot is
`leader.p+0x30 + R(leader alpha, leader beta, 0) · offset`
(`0x00458290` `8b 48 1c 8b 50 18 6a 00 51 52` → `0x004f0270`, `0x004582c0`
rotate, `0x004582cf` add leader position) **[m]**: the offset follows the
leader's heading and pitch, not its roll. Then **[s]**: beyond `10 · f+0x260`
the ship steers at the slot (`0x0044d020`) at max speed; inside it, if roughly
aligned with the leader (forward·forward > 0.9, cos 25.8°) it steers along the
leader's heading (`0x0044ccc0` toward a point 20·r ahead) and matches speed
through `0x0042fb20` ramps between the leader's speed and its own cap, with a
5000 floor. Leader selection, wing membership and slot assignment are script
side; slot keeping is engine side. FollowObject (0x15) keeps the distance
`f+0x260` to its target (`0x004619c4` **[m]**); its handler `0x00458aea` was
not read in detail.

## 9. Docking and gate transit

SA_FlyObjectIntoContainer computes the rendezvous point `f+0x230`, the port
point `f+0x240` and port index `f+0x250` and sets command 5 **[m]**
(`0x00460b8b`, `0x00460bb0`, `0x00460c25`, `0x00460c72`). Command 5, every frame:

1. **Approach** (outside the final zone): the target is the rendezvous point
   `f+0x230` while the ship is farther from the port than the rendezvous point
   is (plus its radius), then the port `f+0x240` **[s]**; steering `0x0044ccc0`
   (`0x00454745`) **[m]**; once all three remaining angles are below `0x51e`
   (7.2°, `0x00454770`) the roll error is replaced by the port's roll **[m]**/**[s]**;
   desired speed 500 while forward·direction < 0.95 (§5.3), else a distance
   ramp with a class-dependent cap (75,000 for ship-class enum values
   0, 1, 3, 4, 8, 9, 0xb–0xf, else 50,000; `0x004542d2`, `0x004542f1`) **[m]**.
2. **Final zone**, distance ≤ `2·r + 25000` (`0x004543da`
   `8b 8b a4 00 00 00 8d 84 09 a8 61 00 00`) **[m]**: speeds and slip are
   zeroed (`0x004543f3`), the remaining angles are zeroed, and the ship is
   moved **kinematically** along the port path by `0x004190f0` (`0x004544f3`)
   at a ramp between 500 and `min(vmax, 20000)` × dt/1000, its orientation
   interpolated directly (`0x004f1c50`, `0x004f04f0`, `0x004f17f0`) **[s]**;
   landing at ≤ 1500 units or when aligned, completion `0x00452280` **[s]**.
   Neither the steering leaves nor the integrator's rotation path run in this
   phase (errors are zero; position comes from the glide) **[s]**.
3. **Gates** (target class 0x12) take no port: the target gate gets
   `[gate+0x40] |= 0x2000000` (`0x004548cf`), the ship flies at the gate point
   with a ramp around half its max speed, arrival at
   `|v|/10 + r` scaled by `1 + (time factor − 1)/4` (`0x00454323`) **[m]**; the
   jump itself is the collision pass's `"CanWarp"` (sector-collide.md) **[s]**.

Consequence: a replacement of the leaves changes the docking *approach* (it
goes through `0x0044ccc0`) but not the final glide; it must keep the leaf
contract that the docking code tests afterwards (all three remaining angles
small → roll match, `0x00454760`–`0x00454790`).

## 10. Native collision avoidance (command 0x12)

SA_AvoidObjectCollision exists and is engine side: every 200 ms it walks an
obstacle list in the flight block (`f+0x1e0` count, objects from `f+0x1e4`,
per-obstacle data from `f+0x1f8`), inflates radii by 1.2 (`0x13333`), computes
an evasion point into `f+0x240` (relative to the ship), raises `f+0x2b0` to at
least 0.7 and steers there with `0x0044ccc0`/`0x0044d020`; it ends when the
obstacle list is exhausted and the distance test against `f+0x260` passes,
and on its exits sets `f+0x2b0` back to 1.0 or to 0.5 (`0x00457b05`,
`0x00457b4e`, `0x00457b84`) **[s]**.
Who fills the list (the collide pass's `"CollisionWarn"` notification and the
script that answers it) belongs to the script-side study **[i]**.

## 11. Hook sites

Common facts **[m]** unless marked: all candidates run on the main-loop thread
**[i]** (they are reached only from the per-sector driver `0x0043a360` and the
cockpit update, the same thread that runs `sector_collide`, whose stubs were
measured on the Present thread, [sector-collide.md](sector-collide.md) §11.7);
none is recursive **[s]**; the steering leaves are called
only from `0x00452ad0` and `0x0044d020`; no data dword in `.rdata`/`.data`
points into any patch window; no rel32 branch in `.text` and no branch inside
the containing function (linear sweep) lands inside a patch window; each
patch window ends on an instruction boundary (`check_sites.py`).

| | Site | Window | At entry | What a trampoline must preserve |
| --- | --- | --- | --- | --- |
| **A** | `0x0044ccc0`, `0x0044c620`, `0x0044c970` (leaf entries) | `83 ec 18` + `8b 02` = 5 B each | `EDX` = `const int[3]*` target, `[esp+4]` = obj, `[esp]` = return; `EAX`/`ECX` dead (overwritten by the leaf before any read) **[m]**; flags dead | EBX, EBP, ESI, EDI; return with `ret 4`; write exactly `obj+0x30/+0x34/+0x38`; leave the x87 stack empty; the target buffer is often a caller stack local — read, never write it |
| **B** | `0x0044d020` (dispatcher entry) | `55`, `8b ec`, `83 e4 f0` = 6 B | stdcall `(obj, target*)` | as A, `ret 8`; covers only the 15 dispatcher calls |
| **C** | `0x00459926` inside the integrator | `f7 c1 00 00 08 00` = 6 B | `EBX` = obj, `ECX` = `[obj+0x40]`, `ESI` = this object's dt (also at `[esp+0x24]`), `EDI` = game time; flags dead before, live after (`je 0x00459cdb`) | all GPRs; re-execute `test ecx,0x80000` last and jump to `0x0045992c`; runs per object per frame for every integrated object, so filter class 7 + autopilot in the stub before calling C++ **[i]** cost |
| **D** | `0x0043ad20` (per-axis controller entry) | `83 ec 08`, `55`, `56` = 5 B | stdcall 7 args (`e, w, W, a, &w', &delta, dt`) | EBX, EBP, ESI, EDI, `ret 0x1c`; 20 callers: 14 in the integrator, **6 in the cockpit view transition `0x004218b0`**, so filter by return address |

**Replacing the steering law: site A**, all three entries, with one shared
implementation that receives `(obj, target)` and writes the three remaining
angles. It catches every steering decision of the command pass (33 sites, plus
the 3 dispatcher calls), keeps the command and speed logic untouched, and the
contract is narrow (one input point, three outputs, no other side effect).
Because the leaves are re-invoked every 0–50 ms, a replacement can implement
lookahead and curvature limiting statelessly from `(obj, target, rates, speed)`
or keep per-object state keyed by the object (object lifetime:
[object-lifetimes.md](object-lifetimes.md)).
A smooth law also needs to own the dynamics: the trapezoid of §4 is per axis
and brakes at `w²/a`; to shape the angular-rate profile (rather than only the
commanded angle) add site **D** filtered to the integrator's 14 call sites.
Filter by class (7) and, when the replacement is ship-only, leave missiles
(class 10) and class 0x13 on the original path.

**Pre-steering avoidance: also site A.** The stub copies `*EDX` to a per-call
buffer, lets the avoidance module move the point (predicted closest approach
over the obstacles known from the sector buckets), and calls the original leaf
(or the replacement) with `EDX` = the buffer. The command's own distance tests
stay on the original target, so arrival, docking stages and escort slot
keeping are unaffected. Exclude command 5 inside the final zone (the leaves are
not called there anyway) and consider excluding command 0x12, which is the
engine's own avoidance.

Site C is the alternative when the replacement must run every frame regardless
of think ticks (for example per-frame lookahead at very low frame rates); it
sees the remaining angles after the command pass and before rotation. Site B
alone is insufficient (MovePos for ships with a yaw rate, docking, undocking
and the escort's close-in branch bypass it).

## 12. Not established, and the observation that settles each

| Open point | Observation (existing proxy patterns: entry trampoline + per-frame ring + Present-side line, as `collide_narrow_census`, `own_ship_cache.h`) |
| --- | --- |
| Whether NPC ships run with the autopilot flag set (`0x80000`) all the time, and who sets it at spawn | one row per second per ship in the player's sector: `obj`, class, `[obj+0x40] & 0x80000`, `f+0x258` |
| Real think cadence in traffic (the interval comes from the type record at creation; other writers of `f+0x20c` in `0x00419430`, `0x0041f8d0`, `0x004310a0` were not read) | count of leaf entries per ship per second (site A census) |
| Which leaves dominate in normal traffic, and the commands they come from | site A census: return address histogram (maps to the command case) |
| The exact axis meaning of `+0x30` vs `+0x34` | log `obj+0x30/34/38` and the next frame's `obj+0x18/1c/20` delta for one ship |
| Whether the slip term is visible (drift while turning) | `obj+0x60..+0x68` magnitude vs `obj+0x10` on the player ship in a hard turn |
| Position unit (1/500 m) | player odometer `[root+0x50/0x54]` against a known flight distance |
| Who fills the avoidance obstacle list `f+0x1e0..` and when command 0x12 is issued | script-side study; runtime: `f+0x1e0` count when `f+0x258 == 0x12` |
| The player's rotation input path into `obj+0x24..+0x2c` (only the speed keys `0x0040f350`/`0x0040f4f0`/`0x0040f720` were read) | not needed for AI steering; one disassembly pass over `0x00410100` |
| Ship types with record `+0xd0` = 0 (12 TShips rows): angular acceleration 0, and the integrator skips the axis when it is 0 (`0x004599aa` `3b c7 0f 84 ea 00 00 00`), so they do not rotate under autopilot **[m]** | identify the rows (type-table study); runtime: a ship of that type under MovePos |

## Reproduce

```sh
cd /home/asvetl/x3-mod
python3 verification/results/ship-movement/check_sites.py        # RESULT PASS
python3 verification/results/ship-movement/axis_controller_sim.py
python3 verification/results/ship-movement/tships_columns.py
JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64 flock /tmp/x3-ghidra.lock \
  ~/ghidra_12.1.4_PUBLIC/support/analyzeHeadless ~/x3-ghidra-research X3Render \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3DecompileFunctions.java ~/x3-nav-study/movement/decomp.txt \
  00452ad0 004596e0 0043ad20 0044d020 0044ccc0 0044c620 0044c970 00450980 00450b80
```

Listings for the quoted ranges came from capstone over the file through the PE
map (`.text` RVA `0x1000` → raw `0x400`); xrefs and call trees from
`X3XrefsTo.java`, `X3CallTree.java`, `X3FunctionContext.java`, `X3GrepInsns.java`.
