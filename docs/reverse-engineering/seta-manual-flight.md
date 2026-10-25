# SETA and manual flight input: the input stop and real-time steering under warp

2026-10-24, static study of `X3AP.exe` (SHA-256 `fdbf3418d8f0a897…`, verified; native copy `C:\X3`, read from WSL as
`/mnt/c/X3`); nothing was launched, no Wine. Ghidra 12.1.4 (`X3DecompileFunctions.java` on the project
`~/x3-ghidra-research`) for `0x00410100`, its 26 helpers and `0x00450980/b80/c10/cf0/d60/dd0`; capstone over the file
through the PE map for every quoted byte. Raw decompiler output stayed in the session scratchpad. Builds on
[seta-script-toggle.md](seta-script-toggle.md) §1 and §4 (the script side of the stop),
[engine-effects.md](engine-effects.md) §8 (the clock, `cfg = *(u32*)0x00606f34`, `cfg+0xcc`, `cfg+0xd0`),
[ship-movement.md](ship-movement.md) §1–3 and §13.2 (integrator, slip) and
[pause-dialog-input.md](pause-dialog-input.md) (input block `*0x00606f3c`). Markers: **[m]** read from the bytes,
**[i]** inferred, **[u]** unknown.

| Script | Output | What it establishes |
| --- | --- | --- |
| [`seta_manual_flight_study.py`](../../verification/results/seta-manual-flight/seta_manual_flight_study.py) | [`seta_manual_flight_study.txt`](../../verification/results/seta-manual-flight/seta_manual_flight_study.txt) | the nine flags (every absolute writer and reader in `.text`, the records, the pointer writers, the absence of another reader), the stop block and the patch (boundaries, branch targets, alignment, liveness, decoded patched image), the per-row variant (the nine rows, the eight 2-byte rewrites, qword containment, decoded patched rows), the three angular controllers and their hook spans, the manual-rotation integrator, strafe, speed keys, look routine, mouse-delta normalisation, every game-step and `cfg+0xcc` reader of the input family, the warp-aware AI margins, control names; **162 checks, `RESULT PASS`** |

**Answer.**

* **The nine flags are "changed this frame" bits, one per control record** of the input routine `0x00410100`
  (record + 0x10; yaw, pitch, roll, turret yaw, turret pitch, strafe x, strafe y, speed, look). They are set by
  every manual control class: keys, joystick buttons and hat (through the key-action counters), digital and analog
  joystick axes, throttle, mouse steering and cursor steering (through the angular controllers), mouse or joystick
  mapped to "look". The default mouse mapping (script `InputMouseDelta`) and head tracking set none. **The stop
  block `0x004120c5..0x00412133` is their only reader [m].**
* **(A) one byte:** `0x0041211d` `74 17` (`je 0x00412136`) → `eb 17` (`jmp 0x00412136`). The patched path is the
  one the engine takes at warp 1.0 whenever a flag is set, register for register. The input has already been applied when the
  block runs, so the ship steers under warp [m]. Verify `81 ba cc 00 00 00 00 00 01 00 74 17` at `0x00412113`.
  To keep the stop for a speed change only, rewrite the flag rows instead (§2, per-row variant; part A's source uses it
  after release 1.5.0).
* **(B) all player control is a command in game-time units, ramped and integrated with game ms**, so under
  warp k both the turn rate and the angular acceleration are k× per real second [m]. Scaling the command alone by
  1/k gives the right steady turn rate, but the ramp to it then runs k² × faster than unwarped flight in real time.
  The ramp step must be scaled by 1/k² as well [i, from the listing]. Each axis has one span that holds both
  quantities: `0x0040ebde` (yaw), `0x0040ee3e` (pitch) and `0x0040f04d` (roll), 14 bytes each. k is the
  effective rate `(cfg+0xcc · cfg+0xd0 + 0x8000) >> 16`, because that is what the game clock advances by. `cfg+0xcc`
  alone is not.
* **No engine path scales player input by the warp factor [m].** The input family reads `cfg+0xcc` once, at the
  stop. The nearest models are the mouse-delta axis modes, which divide by the game step and are therefore already
  warp-invariant per mouse count, and the auto-detail code `0x00496f85`, which divides `cfg+0x714` by the warp.

## 1. The nine flags (Q1)

**Records [m].** `0x00410b89..0x00410bb3` clears the nine flags; from `0x00410be5` the records are re-seeded from the
ship each frame (current yaw = `obj+0x24 · 2^-16`, command 0, `0x00410bfa`). Both run only when the controlled object
is non-zero (`0x00410b81` `test ebx,ebx; je 0x004121b3`). Each record has
`+0x00` current value (float), `+0x04` "digital input this frame" **[i]**, `+0x10` **changed (the SETA flag)**,
`+0x14` "input under autopilot" (feeds the `SetAutopilot(1, 0)` cancel block `0x00412136..0x004121b1`, a separate
reader), `+0x24` command, `+0x28` scale/maximum **[i]** (strafe and look records). The routine's 30 record-base immediates all lie inside the input
family `[0x0040e960, 0x00412250)`. No dword outside `.text` points into the records. All 26 helpers are called only
from inside the family, and no dword in the file holds a helper address.

| flag | record | quantity | writers | physical input that reaches it |
| --- | --- | --- | --- | --- |
| `0x00608e60` | `0x00608e50` | ship yaw rate | **abs** `0x00411036`, `0x004111a2`, `0x00411329`, `0x00411496` (digital joystick X/Y mapped to yaw, both directions); `0x0041173f` (action 527 Yaw Right), `0x00411783` (526 Yaw Left); **controller** `0x0040ec28` | keyboard, joystick buttons/hat mapped to yaw, digital and analog stick, mouse steering, cursor steering (all via the controller) |
| `0x00608e88` | `0x00608e78` | ship pitch rate | controller `0x0040ee88` only | as yaw (actions 524/525) |
| `0x00608eb0` | `0x00608ea0` | ship roll rate | controller `0x0040f0ed` only | as yaw (actions 528/529) |
| `0x00608ed8` | `0x00608ec8` | turret yaw | turret controller `0x0040f1e0` at `0x0040f2b8` (record from `0x00412048`) | any yaw input while a turret view is active (cockpit entry `+0x1d8 > 0`) |
| `0x00608f0c` | `0x00608efc` | turret pitch | `0x0040f2b8` (record from `0x0041205f`) | as above, pitch |
| `0x00608f40` | `0x00608f30` | strafe x | abs `0x00411f0c` | strafe keys 540/541, stick axes mapped to strafe |
| `0x00608f6c` | `0x00608f5c` | strafe y | abs `0x00411e6e` | strafe keys 542/543, stick axes |
| `0x00608f98` | `0x00608f88` | desired speed | `0x0040f3e7` (speed key step `0x0040f350`: 531 Accelerate, 532 Decelerate, 544 Brake to Stop, digital axis), `0x0040f568` (534 Boost extension, `0x0040f4f0`), `0x0040f63b` (analog speed axis `0x0040f580`), `0x0040f712` (slider `0x0040f690`), `0x0040f7ba` (throttle `0x0040f720`) | keyboard speed keys, joystick throttle/slider |
| `0x00608fd0` | `0x00608fc0` | look direction | `0x0040f9ba` in the look routine `0x0040f850` | stick axis, digital axis or mouse axis mapped to "look" (axis types 9/10) |

The action numbers and names come from the counters `0x0057fc68 + action` (`0x0057fe74` = 524 … `0x0057fe88` = 544)
and the AP control names (text page 301903, id 10000 + action) **[m]**.

**When each is set [m]:**

* **Angular controllers** (`0x0040eb60` yaw, `0x0040edc0` pitch, `0x0040efd0` roll, called every frame from
  `0x00411db5/0x00411dbf/0x00411dcb`) set the flag when the current rate changes by more than `[0x005656a0]` =
  0.00499 turns/s (1.8°/s) in one frame. The comparison is `fcomp` at `0x0040ec18` (pitch `0x0040ee78`). They also
  set it while the rate decays after a key is released, because the command is reset to 0 each frame and the
  controller slews toward it. They return at once under autopilot (`0x0040eb65`: `obj+0x40 & 0x80000`). A pitch or
  roll key sets no flag directly, so it stops SETA only once the per-frame rate change exceeds 1.8°/s. That change
  is accel × game ms, so it is reached more easily the higher the warp **[i]**.
* **Turret**: the per-frame angle step `rate × game ms / 1000` exceeds `±0x51e` binary units (7.2°) (`0x0040f2aa`).
* **Strafe**: whenever the strafe command and the ship's strafe maximum are both non-zero (`0x00411e6e`, `0x00411f0c`).
* **Speed**: the speed-key step exceeds 250 units (`0x0040f3db`), or the throttle-derived desired speed moves by more
  than 250 (`0x0040f712`, `0x0040f7ba`). Boost sets it unconditionally (`0x0040f568`).
* **Look**: `0x0040f850` maps a look vector beyond `0x4000` to one of eight `InputAction(0x2cd..0x2d4)` (717 "View
  from west" … 724 "View from southwest") and sets the flag (`0x0040f9ba`). Two modes return before it with no flag:
  the direct-angle look mode (`+0x48`, axis types 0x12/0x13, `0x0040f851`) and head tracking (`+0x44`, filled from the
  6-value provider `0x004d25b0`). **Correction to seta-script-toggle.md §1:** `0x0040f9ba` is this look routine; it
  is not a hat routine. The hat itself becomes engine keys `0x140..0x147` (`0x004108f8`, `0x00410912`) and from
  there key actions.

**Input classes [m path].** Keys, joystick buttons and hat go through `TranslateKey`, which increments
`0x0057fc68[action]` (`0x00410200`). The key-action loops then add ±max rate to the yaw/pitch/roll command, ±max to
the strafe command, or call the speed step. Analog stick axes go through `0x0040fc20` and the deflection
`axis × min(max rate, 0x10000 · [0x00608c9c] / 100)` (`0x0040e960`, sensitivity in percent). The mouse goes through
`0x0040fec0` by axis type (`0x00608e40/e44`, turret view `0x00608e48/e4c`):

| type | mouse axis does | flag path |
| --- | --- | --- |
| 1 / 2 / 3 | delta → yaw / pitch / roll command (`0x0040eaa0` …) | controller |
| 4 / 5 | delta → turret (`0x0040f130`) | turret |
| 9 / 10 | \|delta\| ≥ 15 / ≥ 12 counts → look (`0x0040f7d0` / `0x0040f810`) | look `0x0040f9ba` |
| 0xb / 0xc (**default**, `0x0040fb2b..0x0040fbbb`) | delta accumulated into `0x00609010/14` → `InputMouseDelta(dx, dy)` (`0x00412230`) | **none** |
| 0xd / 0xe | cursor offset from screen centre, FOV dead zone (`0x0040e8c0`) → yaw / pitch command (mouse steering) | controller |

**Mouse-look and camera panning.** With the default mapping the mouse sets no flag [m]. The script-side camera
handling of `InputMouseDelta` was not traced [u]. The mapping values come from the script through the X2 command
dispatcher `0x00406de0` (call to the setter `0x004123a0` at `0x0040747e`) **[m site]**. That mouse steering selects
0xd/0xe is **[i]**. The cursor-steering state `0x00607c64` (`X2_UpdateCursorSteering`) has one reader,
`0x0042ab24`, outside the input family [m].

**Other readers [m].** The nine flags have one set of absolute readers, the nine `cmp`s `0x004120c5..0x00412105`.
The family has eight `[reg+0x10]` reads in all, and none of them has a record base: ship speed
(`0x0040f39e`, `0x0040f4f2`, `0x0040f5cc`), the cockpit registry `[*0x00608504+0x10]` (`0x0040f860`, `0x0041022c`,
`0x00410cff`, `0x00411c8d`) and the target's speed (`0x00411ce0`). **Removing the SETA call therefore breaks no other
consumer.**

## 2. The patch (Q2) **[m]**

```
004120c5  39 1d 60 8e 60 00 75 40   cmp [0x00608e60],ebx; jne 0x0041210d     (x8, EBX = 0 since 0x004120a5)
00412105  39 1d d0 8f 60 00 74 29   cmp [0x00608fd0],ebx; je 0x00412136
0041210d  8b 15 34 6f 60 00         mov edx,[0x00606f34]
00412113  81 ba cc 00 00 00 00 00 01 00   cmp [edx+0xcc],0x10000
0041211d  74 17                     je 0x00412136            <- patch: eb 17 (jmp 0x00412136)
0041211f  53 6a 01 68 1c 5e 55 00 68 96 00 00 00 53 53 e8 8d d3 08 00 83 c4 18
                                    0x0049f4c0(0, 0, 0x96, "StopFastForward", 1, 0); add esp,0x18
00412136  f7 47 40 00 00 08 00      test [edi+0x40],0x80000  (autopilot-cancel block)
```

* **Bytes.** Original `74` at `0x0041211d`, patched `eb`; one byte. With `engine_patch`:
  `verify_bytes(0x00412113, {81 ba cc 00 00 00 00 00 01 00 74 17}, 12)`, which compares the cmp and the je as two
  whole instructions within the 5..16-byte limit. Then `VirtualProtect`, `write_code(0x0041211d, {0xeb}, 1)` and
  `FlushInstructionCache`, as `fov.cpp`/`lod_occlusion.cpp` do for their in-place patches. `0x0041211d` lies in the
  aligned qword `0x00412118`, so `write_code` takes the `lock cmpxchg8b` path. Restore writes `74`.
* **Boundaries and entries.** `0x0041211d` is an instruction start, and the rel8 is unchanged. The only branches into
  `0x0041210d..0x00412136` are the eight `jne → 0x0041210d` and the two `je → 0x00412136`. No `.text` dword (study scan; `.rdata`, `.data` and `.rsrc` were scanned once in review on 2026-10-24, 0 hits) holds an
  address in that range. In the patched image every instruction start from `0x004120bb` to `0x00412135` is
  unchanged, and `0x0041211d` decodes as `jmp 0x00412136`.
* **Side effects.** None beyond skipping the call. The patched path is the engine's own `cfg+0xcc == 0x10000`
  path: EDX = `cfg`, EBX = 0, the same stack. EDX is not referenced anywhere in `0x00412136..0x0041224f`, and
  `0x00412136` rewrites EFLAGS. EAX and ECX are written before any read on both continuations (autopilot path up to
  its call at `0x004121a2`; join path `0x00412205`, `0x00412218`), so the call's clobbers were never consumed.
* **The input still takes effect in that frame.** The block runs after the controllers wrote `obj+0x24..+0x2c`
  (`0x0040ec3c` …), the strafe increments went into the slip `obj+0x60..` (`0x00411ff6..0x0041201f`) and the speed
  keys wrote `obj+0x14`. The original call too only stops SETA for the following frames, through
  `150::StopFastForward` → `TI_SetTimeWarpFactor(1.0)`. With the patch the ship turns, strafes and changes speed
  under warp at the warped rates of §3.
* **Coexistence.** The byte lies outside the `X3M_SETA_RESTORE` compare window `0x0041211f + 23`
  (`src/proxy/seta_restore_core.h` `stop_call`), so that installer's raw comparison still passes. No proxy claim sits
  in `0x004120c5..0x00412136` (grep of `src/`). The flags are still set and cleared every frame; that is harmless.
  Install it in the backend-load install window like every other claim.

**Per-row variant: the stop for speed only [m].** The nine rows are `39 1d <flag> <jcc> <rel8>` at
`0x004120c5 + 8k`, each flag operand as in the §1 table: rows 1–8 `jne 0x0041210d` with rel8 `0x40 - 8k`
(`0x004120cb` `75 40` … speed `0x00412103` `75 08`), row 9 (look) `0x0041210b` `74 29` `je 0x00412136`. Rewriting
the seven non-speed `jne` (`0x004120cb + 8k`, k = 0..6) to `66 90` and the look `je` to `eb 29` leaves the speed
row's `jne` as the only branch to `0x0041210d`, so the stop runs exactly when the speed flag is set, still behind the
`cfg+0xcc == 0x10000` test at `0x00412113` (`0x0041211d` unchanged). Every rewrite is two bytes at the jcc's
instruction start, at offset 3 of an aligned qword (`0x004120c8 + 8k`, `0x00412108`), all on page `0x00412000`. No
`.text` branch targets `0x004120bc..0x0041210c` (so none targets a rel8 byte) and no `.text` dword points there; in
the patched image every instruction start from `0x004120bb` to `0x00412135` is unchanged. The rewrites lie outside
the `stop_call` window `0x0041211f + 23` and the chase restore's entry claim `0x00410100`+6. The side-effect argument
above holds unchanged: a non-speed or look flag now reaches `0x00412136` with EBX = 0 and the stack as before, and
EFLAGS is rewritten there. Checked by the study script's section 2b.

## 3. Where commands are produced and integrated (Q3)

**Rotation [m].** Per axis the command `rec+0x24` is accumulated from the inputs of §1, in binary angle per
**game** second (0x10000 = one turn). The controller then:

1. clamps it to ± the ship's maximum rate (yaw `0x00450d60` = type record `+0x08` × (1 + `f+0x270`); pitch `+0x0c`,
   roll `+0x10`) (`0x0040eb72..0x0040eb86`);
2. computes the step `accel × cfg+0x714 × 0.001` (`0x0040ebde..0x0040ebef`). For ships (class 7) accel is
   (yaw + pitch + roll maximum) × `0x7999/65536` (0.475 per second; roll 0.25, `0x4000`); for other classes it is
   0.8 turns/s² (`[0x005656a4]`);
3. slews the current rate toward the command by at most that step (`0x0040e9a0`);
4. stores `rate × 65536` into `obj+0x24` / `+0x28` / `+0x2c` (`0x0040ec2f..0x0040ec3c`).

The current rate is re-seeded from `obj+0x24..` at `0x00410be5..` each frame. The integrator's manual branch
(`0x00459926` autopilot clear → `0x00459cdb`) turns the ship by `rate × dt / 1000` per axis with dt in game ms
(`0x00459cdb` pitch, `0x00459cf7` yaw, `0x00459ece` roll). **Both the ramp and the rotation therefore run in game
time.** At warp k a full-deflection turn reaches k× the real-time rate, and reaches it k× sooner, in real seconds
[i, from the bytes].

**What scaling gives unwarped feel [i].** Write the game rate as `r_g = r_real / k`. Unwarped flight follows
`dr_real/dt_real = a` toward ω. The warped game state that reproduces it needs **target ω/k and per-frame step
`a · game_ms / (1000 · k²)`** (= `a · wall_ms / (1000 · k)`). Scaling only the command (1/k deflection) keeps the
step at `a · game_ms / 1000`. The ramp to the reduced target then takes `ω / (a k²)` real seconds instead of `ω / a`.
At ×10 a turn that ramps in 0.7 s unwarped (equal maximum rates: `1 / (0.475 · 3)`) ramps in 7 ms. The steady turn
rate is right, but the ramp disappears. **The ramp needs its own 1/k² scale.**

**One site per axis.** Each controller has a 14-byte span that holds both quantities:

| axis | span | instructions | entered |
| --- | --- | --- | --- |
| yaw | `0x0040ebde..0x0040ebeb` | `a1 34 6f 60 00` mov eax,[cfg]; `da 88 14 07 00 00` fimul [eax+0x714]; `8b 4f 24` mov ecx,[edi+0x24] (target) | only at its first byte (join from `0x0040ebd6`) |
| pitch | `0x0040ee3e..0x0040ee4b` | same, `8b 4f 24` | join from `0x0040ee36` |
| roll | `0x0040f04d..0x0040f05a` | same, `8b 4e 24` mov ecx,[esi+0x24] | join from `0x0040f045` |

**Hook-site suitability.**

* **Boundaries [m].** Three whole, position-independent instructions with no relative branch, so they fit
  `engine_patch::claim` with length 14 and `rel32_offset` 0. Nothing branches into a span's interior.
* **Live in [m].** x87 `st0` = accel (one slot used). Yaw/pitch: EDI = record, ESI = ship; roll: ESI = record,
  EDI = ship. `[esp]` holds the pushed ECX.
* **Live out [m].**
  * `st0` = accel · step.
  * ECX = the target; it is pushed at `0x0040ec02`.
  * EAX is dead: `0x0040e9a0` touches it only through `fnstsw`.
  * EDX is dead: no read in `0x0040e9a0` or in the span's tail.
  * EFLAGS is dead: `sub esp,8` follows.
* **Thread and reentrancy [m sites, i reentrancy].** Main thread, inside the input routine. The VM is not active
  there and the code is not reentrant.
* **What a stub does [i].** It would replace the fimul with a multiply by `game_ms / k²` and ECX with
  `round(target / k)`, using EAX/EDX as scratch. It must leave the x87 stack depth unchanged.

The controllers already return under autopilot, so the scale can never act on autopilot. The roll controller writes
`obj+0x2c` through the auto-level branch (`[*0x0060850c+0x3c] & 2`). That branch stays in game time (§5).

**Strafe [m bytes, i reading of the x87 sequence].** Per axis the increment is `clamp(command, ±20000 · game_ms / 1000)`
(`[0x00565650]` = 20000, `0x00411dd0..0x00411f16`); game ms enters only through the limit, loaded once by the
`fild [esi+0x714]` at `0x00411e0c`/`0x00411eb2`. The increment is added to the slip velocity `obj+0x60..+0x68`
(`0x00411ff6..0x0041201f`). The integrator then moves the ship by `slip × dt / 1000` and decays the slip by
`(2000 − dt) / 2000` per frame (`0x0045a74e`), all in game time. In steady state the slip before the move is
`increment · 2000 / dt`, so the ship moves `2 × increment` per frame whatever dt is. Unscaled warp therefore changes the
real-time strafe speed only while the limit binds (`|command| > 20 · game_ms`): the increment then grows k× and so does
the displacement per real second. Below the limit the increment and the displacement per frame are the same at every
k. The scale that restores unwarped strafing in both regimes is the limit alone: `game_ms / k` at the `fild`, giving
`clamp(command, ±20 · game_ms / k)` = the unwarped increment. Scaling the command loads `0x00411e18`/`0x00411ec2` as
well would make the ship strafe k× too slowly whenever its strafe maximum is below the limit. The slip settles k×
faster in real time, because the 2 s game time constant is fixed in the integrator, which all objects share. The proxy
applies the limit scale (`X3M_SETA_MANUAL_FLIGHT=2`, [seta-manual-flight.md](../architecture/seta-manual-flight.md)).

**Speed [m].** Speed keys change the desired speed `obj+0x14` by `max · min(game_ms, 100) / 750 · (max/4 + |v|) / max`
per frame (`0x0040f35f`, `0x0040f36c`, `0x0040f38b`). That is k× faster per real second, up to the 100 ms cap: at ×10
and 60 fps the cap binds (160 → 100 game ms), so the change is 6.25× faster. Throttle and slider axes set the desired
speed absolutely, with no time term (`0x0040f690`, `0x0040f720`). The actual speed slews toward it at accel ×
game dt / 1000 in the command-pass tail (`0x00459562`, ship-movement.md §1). For unwarped key handling, use
`game_ms / k` at `0x0040f35f` (boost `0x0040f521`, analog axis `0x0040f58f` likewise) [i]. Forward motion itself must
stay warped, since that is what SETA is for.

**Which factor [m clock, i choice].** The game clock advances `(clamp(wall_ms, 1, 200) · R + 0x8000) >> 16` per tick,
with `R = (cfg+0xcc · cfg+0xd0 + 0x8000) >> 16`; `+0xd0` is the load governor, 0.3..1.0
(engine-effects.md §8). k must be `R / 65536`, not `cfg+0xcc / 65536`. With `+0xcc` alone, a governor at 0.5
would turn the ship at half the unwarped rate. The per-frame ratio `cfg+0x714 / cfg+0x71c` equals k except on frames
with a wall step over 200 ms or a step forced to 1, so R is the steadier source. Gate the scaling on
`cfg+0xcc > 0x10000`. At warp 1 the governor can also lower R below 1.0, and the scale would then speed the ship up
[i].

## 4. Existing warp scaling (Q4) **[m]**

None for player input. Across the input family `[0x0040e960, 0x00412250)` the only `cfg+0xcc` reference is the stop
compare `0x00412113`, and there is no `+0xd0` reference. The family has 13 reads of the game step `+0x714` and 4 of the
wall clock `+0x720` (key tap/hold, the speed key's 1000 ms window), and none of `+0x71c`. Warp-aware code elsewhere:

* **Mouse-delta axis modes 1–5** (`0x0040eaa0`, `0x0040ecc0`, `0x0040ef00`, `0x0040f130`):
  `rate = delta · 0x8000 / game_ms`, clamped to ±0x10000 (`0x0040eb1e..0x0040eb36`). Steps under 10 ms are pooled
  first (`0x0040eaab`). The integrator multiplies by game ms again, so the angle per mouse count is fixed
  (0x8000/1000 binary units ≈ 0.18°) whatever the warp. Only the rate cap and the ramp are warped [i]. This
  normalises by the step, not by the factor, but it is the one input path already in real-time terms.
* **Auto-detail** `0x00496f85`: frame ms = `cfg+0x714 / cfg+0xcc` when the warp exceeds 1.0 (ignores `+0xd0`).
* **AI margins** `0x0044d67d` and `0x0045431d` (docking): a distance term × `(1 + (cfg+0xcc − 1) / 4)`. Not input
  scaling.
* The effective rate `(cc · d0) >> 16` is used only by the clock and the fps overlay `0x004e3fe4` (engine-effects.md §8).

So the model for (B) is the clock's own R, applied at the controller spans of §3.

## 5. Consistency and risks (Q5)

**(B) inconsistencies [m sites, i effects].**

* **Mouse-delta modes 1–5 are already warp-invariant**, so a controller-target scale would make them k× too weak.
  The stub should skip an axis whose mouse type (`0x00608e40/e44`, turret `e48/e4c`) is 1–5. The default type
  0xb/0xc and cursor steering 0xd/0xe need the scale.
* **Turret** rotation (`0x0040f1e0`, game ms at `0x0040f217`, step × dt/1000) is warped too and is not covered by
  the ship controller spans.
* **Auto-level roll**: the integrator runs it through `0x0043ad20` with game dt (`0x00459d2c..0x00459e27`; player
  ship, universe flag 2, no roll command). It levels k× faster in real time.
* **Slip/strafe transient** as in §3. The integrator's slip decay is shared by all objects, so it is not a player-only
  site.
* **Speed**: the displayed speed is `obj+0x10` [i, the HUD reader was not traced]. It is unaffected if (B) leaves
  forward speed warped.
* **Autopilot** is unaffected. The controllers return under `0x80000`, and autopilot rotation is the integrator's
  `0x0043ad20` path in game time.
* **The SETA ramp** changes R in ten steps 250 ms apart (seta-script-toggle.md §1). The stored game rate then gives
  a real-time rate that jumps by `R_new / R_old` at each step, until the controller re-converges at its scaled ramp.
  A full stop (k → 1) mid-turn leaves the rate k× low, and it ramps back at the normal acceleration.

**(A) risks.**

* **Nothing else consumes the flags [m]**, so no engine state assumes warp 1 after manual input. The stop appears to
  be game design: SETA is a cruise aid that cancels on manual control, like the autopilot cancel next to it [i].
* **Every script stop remains** (seta-script-toggle.md §4): gate entry through `CanWarp` → `150::StartGateWarp`,
  docking/landing, death, jumpdrive, cut-scenes, save, ship change, the speed-bar click `605::ClickInfoPanel`, the
  SETA key. The patch touches only the engine's input stop. The proxy's SETA-restore stop classification lists the
  input stop as "bottom record (offset 0)". That row no longer occurs [i].
* **Manual flight at warp uses the large game steps** that cruising under SETA already uses. At ×10 and 60 fps the
  step is about 167 game ms (integrator cap 1000 ms, speed-key cap 100 ms). Per-frame turn angles and strafe
  increments are k× larger, and collision sees positions k× further apart. This is the existing SETA behaviour,
  with a turning ship [i].
* **The autopilot cancel** (`+0x14` flags, `0x00412136`) is a separate block and keeps working [m].

## 6. What remains unknown

* How the script maps the mouse-steering option onto axis types (the X2 command that reaches `0x004123a0`), and the
  script's use of `InputMouseDelta` for external-camera panning.
* The HUD's speed reader; the exact strafe maximum `0x00450c10` per equipment (the study needs only that it is
  non-zero).
* Felt behaviour: the 1/k² ramp is derived, not flown. A flight check would log `obj+0x24` and wall time for one
  full-deflection yaw at ×1 and at ×10 with the scale on. Expected: equal rise time and equal steady
  `obj+0x24 · k`.

## 7. The SETA key, and SETA restarting after a key stop (2026-10-25)

User report on release 1.5.1 (`seta_manual_flight = 2`): turning SETA off with its key sometimes makes it start ramping
up again at once, mostly while yawing or pitching. This is a static study of `X3AP.exe` (same hash), the Mayhem story
object (`addon/07.cat`, build 1462141839; stock `addon/04.cat` for the filter), the user's control profile
(`Documents\Egosoft\X3AP\profiles\profile0.xpf`) and the 2026-10-25 log. Nothing was launched. Ghidra was used for
`0x00410100`, `0x00404280`, `0x00402670`, `0x004d5a90` and `0x004d3b60`; raw output stayed local.

| Script | Output | What it establishes |
| --- | --- | --- |
| [`seta_key_toggle_study.py`](../../verification/results/seta-key-toggle/seta_key_toggle_study.py) | [`seta_key_toggle_study.txt`](../../verification/results/seta-key-toggle/seta_key_toggle_study.txt) | the key ring (writer, reader, slot 3), one read and one `Input` call per main-loop iteration, the action counters, the script's repeat filter and the 675 dispatch, every SETA starter, the ramp guard, the profile binding, and the ring model; **52 checks, `RESULT PASS`** |
| [`log_facts.py`](../../verification/results/seta-key-toggle/log_facts.py) | [`log_facts.txt`](../../verification/results/seta-key-toggle/log_facts.txt) | what the flight log carries (SETA row kinds, the frame rate) |

**Answer.** The SETA key is a held-state stream, not a key-down event. The engine pushes every held key into a key ring
on every poll and reads one entry per main-loop iteration into the script's `Input`. The script toggles SETA
whenever the action read differs from the action of the previous `Input` call. Any other entry between two reads of
the held SETA key re-arms the toggle, so one press can toggle two or more times. The other entry can be a steering key
held at the same time, or the engine's own empty or stale read. With an even count the press undoes itself: SETA
stops, then starts again, and its ramp audibly resumes 250 ms later. Vanilla hid the restart while steering, because
the steering flags stopped SETA again at the first ramp step. Part A removed exactly those stops **[m chain, i
effect]**. Every other starter is excluded below. The fix is a stop latch on the SETA key; it keeps part A (§7.4).

### 7.1 The key, end to end (Q1) **[m]** unless marked

1. **DirectInput, buffered, applied to a persistent state.** The poll `0x004d5a90` (in flight once per main-loop iteration,
   through the pump `0x004d34b0` at `0x00403af5`; its other callers `0x0049728c/742c/747c` are wait loops [i]) reads keyboard events with `GetDeviceData`: 1 per poll when
   `cfg+0x728 > 10`, else 4 (`0x004d5b97`). It applies each event to the 256-byte DIK state `ctx+0x210`
   (`0x004d5cc4`). It then walks DIK 0..255 and, **for every key that is down**, sets `ctx+0x10[code] = 1` and pushes
   the DIK into the ring at `0x00595ee8` (12-byte slots, `0x004d5db0..0x004d5e2a`; both Shift keys are excluded). There
   is no edge test, so a held key is pushed on every poll. `WM_KEYDOWN`/`WM_CHAR` feed the ring only without a
   DirectInput keyboard (`0x004d37a3`), so OS auto-repeat plays no part. The keyboard device is always created
   (`0x004d57d1`).
2. **The ring has two index rules.** The DirectInput writer advances `wi` and resets it when the *new* value reaches 3,
   so it writes slots 1, 2, 0 (`0x004d5de9`). The reader `0x004d3b60` returns 0 when `ri == wi`. Otherwise it resets
   `ri` only when the *old* value was ≥ 3, so it reads slots 1, 2, 3, 0 (`0x004d3ba2`). The WM writer `0x004d3a88`
   uses the reader's rule. In DirectInput mode, slot 3's DIK byte `0x00595f0c` is never written. It lies in .bss, and
   no reference to it exists in the file, so it stays 0, and DIK 0 maps to engine code 0 (`0x004d6850` →
   `0x004d6bcc`). Neither writer checks for overflow, so with more held keys than reads, older entries are overwritten.
3. **One read, one `Input` per iteration.** `0x00404280` (from `0x00403db8`, game mode) reads one entry (`0x0040428c`),
   stores it in `0x0060903c` and calls `150::Input(key)` through `0x00402670`. It does so for an empty read (0) too,
   unless the previous `0x00410100` made a script call of its own (`[0x00607c60]`, `0x004042d1`). Then
   `0x00410100` runs in the same iteration.
4. **The engine's own per-key work.** `0x00410100` clears the action counters `0x0057fc68[0x400]` at entry
   (`0x0041013c`). Then, for every engine code down this poll, it calls `TranslateKey`. A real action (1..0x3ff, not
   the key) gets `counter[action] += 1` (`0x00410a6a`). Any other result records a press time, and the release sends
   `Input(code | 0x2000)` for ≤ 500 ms or `| 0x4000` for a longer hold. The counter for SETA is the byte at
   **`0x0057ff0b`** (`0x0057fc68 + 675`). At the `0x00410100` inject site it still holds the previous iteration's
   count of held keys bound to SETA.
5. **The binding.** The profile binds action 675 to engine code `0x6a` (DIK_J), plain, with no tap/hold qualifier.
   `500::ResolveQualifierConflicts` moves a plain binding to `| 0x2000` only when the same key also has a `| 0x4000`
   binding, and only buttons `0x129`/`0x132` have one. So `TranslateVKey(0x6a) = 675` **[i, profile +
   method]**, and J reaches the script through the ring, not through the release event. Yaw and pitch are on the arrow
   keys `0x102..0x105`, roll on `q`/`e`.
6. **The script's repeat filter.** `150::Input` → `501::PerFrameInput` → `500::InputFrameMap(key)`. That sets
   `m15 = m14` (the previous call's action) and `m14 = TranslateVKey(key)`; `TranslateVKey(0) = 0`.
   `GetLastInputFrameKey` returns `m15`. At `1c625`, `PerFrameInput` returns without acting when the action equals the
   previous call's action or the input context changed. Otherwise the switch `1c674` sends 675 to
   `501::HandleKeyInput` → `1d345` → `150::ToggleFastForward`. Neither has a time debounce. Stock AP's `InputFrameMap`
   has the same statements: this is vanilla.
7. **Start, stop, ramp** (seta-script-toggle.md §1). `ToggleFastForward` calls `StopFastForward` when `m21` is set, and
   otherwise `StartFastForward` (after the ware and dock tests). Start sets `m21 = 1`, `m22 = 0` and
   `m23 = real time`, plays 980, and **writes no factor**. `150::Vbi` runs inside `150::Input` whenever real time has
   changed, before `PerFrameInput`. While `m21` is set it adds one step when more than 250 ms have passed (`1433c`).
   The first step after a start is therefore ≥ 250 ms later (×1.9 at `m24 = 10`). **No ramp task exists to survive a
   stop.** `StopFastForward` sets `m21 = 0` and `m22 = 0`, writes 1.0 and plays 981, and the next `Vbi` does nothing.
   Everything is synchronous inside one `Input` call.
8. **What can raise `cfg+0xcc` after a key stop:** only `StartFastForward` followed by `Vbi` steps. Its one caller is
   `ToggleFastForward`, whose callers are action 675 and `605::ClickDisplayPanel`. The proxy's `seta_restore`
   by-name call also reaches it. No script calls `150::InputAction`, and the EXE calls it only for 717–724 and the
   mouse-steering 560/568. The direct writes that bypass `m21` do not produce a ramp: cheat 971/972 (×2 / ÷2),
   speech with a duration (`200::Speak…WithDuration`), `621::CutEvent` (1.0) and `2225::SpeakQuestionWithDuration`
   (1.0).

### 7.2 Which path fires, and why steering makes it likelier (Q2)

The ring model (study §7) applies the exact index rules, one poll and one read per iteration, and the filter. It counts
toggles per press over every start phase; the frames-held figures map to wall time at the measured rate below.

| held with J | frames held: 4 / 10 / 22 / 30 → mean toggles, share with an even count |
| --- | --- |
| nothing | 2.0 (100 %), 4.0 (100 %), 8.0 (100 %), 10.7 (33 %) |
| one arrow key | 1.5 (50 %), 3.5 (50 %), 7.5 (50 %), 10.1 (58 %) |
| two arrow keys | 0 (the press is lost: three pushes per poll overwrite J) |
| one arrow key, if slot 3 behaved (consistent period 3) | 2.0 (100 %), 5.0 (0 %), 11.0 (0 %), 15.0 (0 %); J alone: always exactly 1 |

Ranked, with evidence:

1. **The SETA key re-delivered within one press: most likely.** Every step is in the bytes (§7.1). Steering adds a
   second held key: the arrow's DIK (`0xc8..0xd0`) is pushed after J on every poll, so reads alternate J and arrow, and
   each J after an arrow is a new 675. That holds with or without the slot-3 effect (last table row). Vanilla masked
   it. Yaw keys set the yaw flag every frame they are held (`0x0041173f`, `0x00411783`), and pitch sets its flag
   through the controller's rate-change test (§1). So the restarted SETA was stopped again at its first ramp step, the
   first frame with `cfg+0xcc ≠ 0x10000` (`0x0041210d`), a single frame at ×1.9. A user in vanilla also rarely presses
   J while steering, because steering already ends SETA. Part A removed those rows, so the restart now runs to ×10.
   **Measured:** the median frame rate is 222 fps (`log_facts.txt`, 320 windows of 3,600 frames; p10 168, p90 238).
   A 100 ms press therefore spans about 22 reads **[i]**.
2. **Cockpit display click** (`605::ClickDisplayPanel` → toggle): possible only with a click on the SETA display.
   Unlikely, and unrelated to steering **[i]**.
3. **`seta_restore`: excluded.** It arms only on a stop whose return offset is `15ef8` (`StartGateWarp`) under
   autopilot. A key stop returns to `155ad` and never arms, and any other factor write cancels an armed ticket
   (seta-restore.md). **Log, measured:** 18 `armed` (all `gate`, `autopilot=1`), 18 `restored` (all
   `frames_after_transit=2`), 3 `skipped reason=manual`, 0 `cancelled`. It restarted SETA only two frames after a gate
   arrival.
4. **A ramp surviving the stop: excluded [m].** It is not a task; `Vbi` is gated on `m21` (7.1 item 7).
5. **A script or Mayhem hotkey restart: excluded [m].** No method other than the toggle calls `StartFastForward`. No
   script calls `InputAction`, and the only MSCI SETA command is the stop (case 1927). Custom hotkeys
   (`500::TriggerCustomHotkey`) run MSCI scripts, which cannot start SETA.
6. **A part-A row side effect: excluded [m].** The nine flags have no reader other than the stop block (§1). The block
   only ever calls `StopFastForward`, and the flags are cleared every frame. Part A removes stops and adds no start.

**Unresolved [u].** With nothing held but J, the model predicts that the slot-3 read (code 0 → `Input(0)` →
`m14 = 0`) re-arms the toggle every fourth read. At 222 fps that would make an ordinary press fail to start SETA about
as often as it fails to stop it, and the user has not reported failed starts. Either the user has not noticed or not
reported it, or a factor the bytes do not show breaks the model for the lone key. The steering case does not depend on
this (last table row). The diagnostic below settles it.

**What the log can and cannot show [m].** The tier is off (`seta_manual_flight … debug=0`). The log has no row for a
user toggle, a ramp step, a key read or a by-name toggle. `seta_restore` logs only gate arms, skips, rebinds,
restores and cancels. It can exclude `seta_restore` for any symptom not within about 3 s after a gate arrival, but the
user gave no time, so no row can be tied to the report.

### 7.3 Diagnostic rows for one flight, if the cause must be measured first (Q3)

These rows go in a debug-tier consolidated build, with no behaviour change:

* **`seta_factor`**: every `TI_SetTimeWarpFactor` from the existing hook `0x004b148c`, not only the gate arms. Fields:
  frame, QPC ms, old, new, task IP `[task+0x1c]`, return offset and name. A key stop is IP `1558d`, return `155ad`;
  a ramp step is IP `14341` with old `0x10000` → new `0x1e666` for the first step **[i offsets/values]**.
* **`seta_key`**: from the `0x00410100` inject handler, on every entry while `[0x0057ff0b] ≠ 0` and for 8 entries
  after. Fields: frame, QPC ms, the SETA counter `[0x0057ff0b]`, this iteration's ring read `[0x0060903c]`,
  `ri = [0x00578488]`, `wi = [0x0057847c]`, the steering counters `[0x0057fe74..0x0057fe79]` (524–529), mouse
  buttons `ctx+0x454` and joystick buttons `ctx+0x450` (`ctx = *0x00606f3c`), and `cfg+0xcc`. Arm the handler
  through the prefilter word only from `seta_factor`'s toggle rows, so the idle cost stays zero; that misses the
  presses that start SETA. To see those as well, the debug build must keep the prefilter bit set.
* **Flight.** With SETA at ×10, stop it with J five times while holding an arrow key and five times with no key held.
  Then start it five times with no key held.
* **Reading.** The reported case shows as a `155ad` row with old > `0x10000`, then the `seta_key` read sequence of
  the same press, then `14341` ramp rows from about 250 ms later. If the read before the second dispatched J was an
  arrow code, the cause is interleaving; if it was 0 with `ri` 3, or the ring was empty, it is the stale or empty read.
  A `155ad` row with old = `0x10000` during a press with no steering proves the lone-key repeat (§7.2 unresolved). A
  ramp after a key stop with no further J read, and no `seta_restore` row, names another starter: reopen §7.2.

### 7.4 The smallest fix that keeps part A (Q3) **[m sites, i design]**

**A stop latch on the SETA key.** The design puts behaviour in the two sites `seta_restore` already owns:

* **Arm** in the factor hook `0x004b148c` on a key stop: IP `1558d`, return `155ad` (`toggle`), old > `0x10000`,
  new = `0x10000`. This is the first toggle of a press made to stop SETA. Set a new prefilter bit (`inject_want` bit 2)
  so that the shared `0x00410100` stub runs the handler; while idle it stays one `cmp; je`.
* **Hold.** At each `0x00410100` entry while armed, read `c = *(u8*)0x0057ff0b`. It is written only by the input
  routine on the same thread (`0x0041013c` clear, `0x00410a6a` and `0x00410200` increments), so the read needs no
  lock. Then call `0x0049f4c0(0, 0, 0x96, "StopFastForward", 1, 0)`. That is the EXE's own call shape
  (`0x0041211f..0x00412133`) on the wrapper `seta_restore` already calls there. It does nothing when `m21 = 0`: no
  sound, no write. If a re-delivered toggle restarted SETA earlier in this iteration, it stops it again before any
  ramp step, because a step needs more than 250 ms. The entry follows the iteration's ring read and `Input` (item 3),
  so the latch always runs after the re-toggle.
* **Release.** Disarm after `c = 0` on 8 consecutive entries. The ring drains in at most 4 reads [i], and the counter
  lags one iteration. Add a wall-time cap of 10 s for a stuck key. A new press is a rise of `c` after release: it
  disarms the latch, and an ON press is never latched.
* **Hook-site suitability.** No new site. The inject entry `0x00410100` is three whole instructions (`55 8b ec 83 e4
  f0`), and its stub saves the general and XMM registers and restores the engine's x87, MXCSR and LastError for the
  callee. It runs on the main thread, outside the interpreter (seta-script-toggle.md §3(i), seta-restore.md). The
  call re-enters the factor hook through `StopFastForward`'s write, return offset 0 (bottom record). That only
  cancels a `seta_restore` ticket, which cannot be armed during a key press because only a gate stop arms it.
  `0x00410100` is not re-entered: `StopFastForward` reaches `PlaySample` and `TI_SetTimeWarpFactor` only.
* **Cost.** Zero while idle. During an OFF press, one byte read and one by-name call per frame, for about 20–40
  frames at 222 fps.
* **Part A** is untouched: steering still never stops SETA; the latch acts only after the user's own key stop.

Starts are not covered. If the diagnostic shows that the lone key repeats (a `155ad` stop with old = `0x10000` while
`c ≠ 0`), the ON press needs the mirror: `StartFastForward` once after release. That stop row proves a start in the
same press passed `ToggleFastForward`'s ware and dock tests, and `StartFastForward` is a no-op when the count left
SETA on. Telling such a press from a deliberate second press within 250 ms needs the press boundaries from `c`. That
is design work and stays open. An engine-side fix also stays open. Changing the DirectInput writer's limit (`66 3d
03 00` at `0x004d5df3`, imm 3 → 4) would make the ring consistent, but it does not stop interleaving, and every
held-key repeat in menus would change with it.

## Reproduce

The strafe reading (steady displacement `2 × increment` per frame, the limit scale equal to the unwarped increment at
k = 1, 2.5, 10 for strafe maxima below, at and above the limit) is checked by the host core of
`verification/probe/seta_manual_flight_host.cpp`, run by `verification/analysis/test_seta_manual_flight.py`.

```sh
python3 -I verification/results/seta-manual-flight/seta_manual_flight_study.py > verification/results/seta-manual-flight/seta_manual_flight_study.txt   # RESULT PASS, 162 checks
python3 -I verification/results/seta-key-toggle/seta_key_toggle_study.py /mnt/c/X3 <profile0.xpf> > verification/results/seta-key-toggle/seta_key_toggle_study.txt   # §7, RESULT PASS
python3 -I verification/results/seta-key-toggle/log_facts.py /mnt/c/X3/x3m.log > verification/results/seta-key-toggle/log_facts.txt
JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64 flock /tmp/x3-ghidra.lock \
  ~/ghidra_12.1.4_PUBLIC/support/analyzeHeadless ~/x3-ghidra-research X3Render \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3DecompileFunctions.java <scratch>/decomp.txt \
  00410100 0040fec0 0040eb60 0040edc0 0040efd0 0040e9a0 0040eaa0 0040fc20 0040f1e0 0040f350 0040f4f0 0040f580 \
  0040f690 0040f720 0040f7d0 0040f810 0040f850 0040ea30 0040f2f0 00450980 00450c10 00450d60   # local only, untracked
```
