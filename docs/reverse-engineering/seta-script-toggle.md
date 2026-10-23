# SETA in the story script: the toggle, the gate switch-off and a safe re-engagement

2026-10-23, static study of `X3AP.exe` (SHA-256 `fdbf3418d8f0a897…`, verified) and the two installed
`L/x3story.obj` builds; nothing was launched, no Wine. **Installed object**: `addon/07.cat`, 2,182,503 B, SHA-256
`2c3101a8…199298`, build id 1462141839 (Mayhem; named Mayhem 4 in [gate-queue-script.md](gate-queue-script.md), the
user's mod is called Mayhem 3 in `docs/status.md`; the build id is what binds). **Stock AP 3.3**: `addon/04.cat`,
`ed5786a0…faff7a`, build 1510658513, for comparison only. No loose `L\x3story*.obj`; `addon/07.cat` is the newest
catalogue carrying it ([gate-queue-script.md](gate-queue-script.md) §8). KC encoding and the tools are those of
gate-queue-script.md §1; CODE offsets below are Mayhem's, and **file offset = CODE offset + 20**. Builds on
[engine-effects.md](engine-effects.md) §8 (the clock, `cfg = *(u32*)0x00606f34`, `cfg+0xcc`),
[sector-transit-order.md](sector-transit-order.md) §2 and §5, [chase-view-transition.md](chase-view-transition.md)
(the `0x00410100` injection) and [script-task-scheduler.md](script-task-scheduler.md) §2–3 (VM, tasks, natives).

| Script | Output | What it establishes |
| --- | --- | --- |
| [`seta_toggle_study.py`](../../verification/results/seta-script-toggle/seta_toggle_study.py) | [`seta_toggle_study.txt`](../../verification/results/seta-script-toggle/seta_toggle_study.txt) | per build: class-150 SETA members and accessors, decoded listings of the start/stop/toggle/ramp methods, the key-action switch, every caller of the stop/start/toggle methods and of `TI_SetTimeWarpFactor` with its return offset, the gate entry paths, the MSCI case, call trees, option (iii) built in memory; text entries; an MSCI usage scan; 29 EXE byte/table checks and a branch-target scan; **`RESULT PASS`** (0 failures) |

The script reuses `kc_obj.py` from `verification/results/ship-navigation-ai/gate-queue/`. Decoded instructions only;
no script bytes are printed except the one mod-authored operand of option (iii).

**Answer.**

* **SETA is a KC state machine in the game singleton (class 150 = `0x96`).** `m21` = on, `m22` = ramp step 0..10,
  `m23` = real-time stamp of the last step, `m24` = target factor (10 on this install), `m60` = game time at start.
  The SETA key is input action **675** → `501::HandleKeyInput` → `150::ToggleFastForward` → `150::StartFastForward`,
  which only sets the state. The per-frame `150::Vbi` then ramps `TI_SetTimeWarpFactor` from 1.0 to `m24` in ten
  steps, at most one step per 250 ms of real time **[m]**.
* **The gate switch-off is `150::StartGateWarp` → `150::StopFastForward` (CODE `15ef3`)**, on both gate paths:
  manual flight (the engine's gate-collision event `CanWarp`, EXE `0x0045d7aa`) and autopilot
  (`2004::__FlyToNextSector`). It runs at gate entry, before the warp-in scene, the fade and the stall. It is
  unconditional for a player transit with SETA on. It is not the engine's input-flag path at `0x00412113`, and not
  a bare `TI_SetTimeWarpFactor(1)` **[m]**. Nothing restarts SETA after the arrival: `StartFastForward` has exactly one
  caller, `ToggleFastForward` **[m]**.
* **Safest re-engagement: call the script's own `StartFastForward` after the transit,** through the class-call wrapper
  the EXE already uses for `StopFastForward`: `0x0049f4c0(0, 0, 0x96, "StartFastForward", 1, 0)`. Call it from the
  input-routine entry `0x00410100`, the flight-proven chase-restore inject site. It is idempotent, keeps all
  script state consistent, and ramps exactly like a key press. To tell the transit's switch-off from the user's, use
  a log/record-only hook at the `TI_SetTimeWarpFactor` case entry `0x004b148c` (8 bytes). It reads the old and new
  factor and the stopping method's return offset (`15ef8` = StartGateWarp) **[m sites, i design]**.
  A direct `cfg+0xcc` write leaves `m21 = 0`, so every stop path becomes a no-op and the next SETA key press
  "turns it on". A one-byte KC patch (retarget the `15ef3` call to `IsFastForward`) keeps SETA through the transit,
  with side effects in §3.

## 1. The on/off path (Q1) **[m]** unless marked

**Class-150 state** (members are per build; stock numbers are Mayhem − 3, e.g. stock `m18` = Mayhem `m21`):

| member | meaning | writers | readers |
| --- | --- | --- | --- |
| `m21` | SETA on | `StartFastForward` (1), `StopFastForward` (0) | `IsFastForward`, `Start/Stop/ToggleFastForward`, `Vbi` |
| `m22` | ramp step 0..10 | `StartFastForward` (0), `StopFastForward` (0), `Vbi` (+1) | `Vbi`, `GetWarpMultiplier` |
| `m23` | `TI_GetRealTime` at start / last step | `StartFastForward`, `Vbi` | `Vbi` |
| `m24` | target factor (integer) | `InitClient`: `SE_StringToInt(SE_ReadText(68, 200))` = **10** (`addon/t/8383-L044.xml` page 68 id 200); `SetWarpFactor` (from menu class 2259) | `Vbi`, `GetWarpFactor`, `GetWarpMultiplier` |
| `m60` | `TI_GetAbsTime` at start | `StartFastForward`, `NotifyCollisionWarn` | `NotifyCollisionWarn` (3 s throttle only) |
| `m9` | player killed | `SetKilled`, `StartKilled` | `StartFastForward` (refuses), `StopFastForward` (mutes the sound), others |
| `m6` | warp in progress (1, 2 during the warp-in scene, 0 at the end) | `StartGateWarp`, `StartSectorWarp`, `WarpToSector` (`16189`, `162bd`, `1641b`, `16967`) | `IsWarping`, `StartGateWarp`/`StartSectorWarp` (re-entry guard) |

**Methods** (class 150, entries `15509`…`15599`; listings in the output file):

* `StartFastForward` (`15509`, argc 0): if `!m9 && !m21`: `200::PlaySample(980)`, `m21 = 1`, `m22 = 0`,
  `m23 = TI_GetRealTime()`, `m60 = TI_GetAbsTime()`. **It does not call `TI_SetTimeWarpFactor`.** No equipment or
  docking test.
* `StopFastForward` (`15557`, argc 0): if `m21`: `PlaySample(981)` unless `m9`, `m21 = 0`, `m22 = 0`,
  **`TI_SetTimeWarpFactor(65536)`** (native at `15588`, return offset `1558d`). **When `m21 = 0` it does nothing.**
* `ToggleFastForward` (`15599`): if `m21` → `StopFastForward`; else if `g9.CheckEquipment(23)` (the SETA ware) and
  neither the active monitor's control object nor `g10` is docked → `StartFastForward` (`15611`); else
  `NotifyNoEquipment(23)`.
* `Vbi` ramp (`142df`–`1435e`; `Vbi` runs from `150::Input` when `TI_GetRealTime` changed, `14898`): if `m21`, `m22 < 10`
  and `TI_CmpTime(real, m23) > 250`: `m22 += 1`, `TI_SetTimeWarpFactor(0x10000 + SE_MulDivFix(m22, (m24 − 1) << 16, 10))`
  (`1433c`), `m23 = real`, `PlaySample(982)`. Then, every `Vbi`, `INS_SetTimeWarpFactor(m22)` (`1435e`, the cockpit
  indicator **[i]**). At `m24 = 10` the steps are 1.9, 2.8 … 10.0, and step 10 stores `0xa0000`, which confirms
  engine-effects.md's ×10 = `0xa0000`. The `<<` reading of opcode `0x58` and the `MulDiv` semantics are **[i]**;
  they reproduce `m24` exactly at step 10. Full ramp ≥ 2.5 s real time.
* `IsFastForward` = `frame; ldmem m21; ret`. `GetWarpMultiplier` returns the same ramp expression; its readers are
  `605::UpdateDisplayPanel` and `2001::DoGalaxyTake`.
* Cheat actions 971/972 → `203::IncreaseTimeWarp`/`DecreaseTimeWarp`: ×2 / ÷2 straight through
  `TI_SetTimeWarpFactor` (bounds `0x100000`, `0x1000`), bypassing `m21`.

**Key path.** `501::HandleKeyInput` (`1ce97`) is one `0x78` switch (`1da33`, 495 slots from action 516, 108 cases;
handler `0x004a37bc`: u16 n, base, default, n targets). **Action 675 → `1d345` → `ToggleFastForward`**, the only
toggle case. 825 → `PauseGame`. The control names agree: AP text page 301903 id 10000 + action gives 10675 "SETA
time accelerator" and 10825 "Pause game". `HandleKeyInput` is reached from `150::InputAction(action)` (`148b7`,
the EXE's named call `InputAction`, e.g. `0x0040f9b2` for the eight look directions 0x2cd–0x2d4, actions 717–724
"View from …", seta-manual-flight.md §1). It is also reached from
six sites in `501::PerFrameInput`, which `150::Input(key)` calls (`148a5`; the EXE's per-key `Input`,
chase-view-transition.md). How `PerFrameInput` maps a key code to 675 was not traced **[u]**. The second
toggle caller is `605::ClickDisplayPanel` (`101a67`), the cockpit display click.

**`StopFastForward` in the EXE.** The string `0x00555e1c` has one push in `.text`, at `0x00412122`.
`0x0041210d..0x00412133`: if `cfg+0xcc ≠ 0x10000`, the input routine `0x00410100` calls
`0x0049f4c0(0, 0, 0x96, "StopFastForward", 1, 0)` (6 dwords, caller pops `0x18`). It does so when any of nine
input flags `0x00608e60 … 0x00608fd0` is set; all nine are cleared at `0x00410b8b..0x00410bb3` (reached when the controlled object `EBX` is non-zero). Writers set them
in the key/axis handlers of the same routine, e.g. `0x00411036`, `0x004111a2` (inside a switch over mapped
key actions, skipped while autopilot bit `0x80000` is set), `0x00411e6e`, `0x00411f0c`, and the look routine
`0x0040f850` (`0x0040f9ba`). Every writer, record and input class, and the one-byte patch that keeps SETA on manual
input, are in [seta-manual-flight.md](seta-manual-flight.md) §1–2 **[m]**. `0x0049f4c0` resolves the method by key and name
through `0x0049f330`, builds a task, pushes the integer arguments and runs it synchronously via `0x0049f430`; it
returns `AL = 0` when the method does not resolve. The EXE has no literal "StartFastForward", "ToggleFastForward" or
"IsFastForward".

## 2. The gate-transit switch-off (Q2) **[m]**

```
manual flight:  engine gate collision 0x0045d7aa: 0x0049f4c0([M+0x60], 0, ship key [obj+0x94], "CanWarp", 1, 1, gate)
                -> 2004::CanWarp (df75c): this == g10 (player) and !150::IsWarping() and !(SA_GetFlags(m3) & 0x80000)
                -> df887 150::StartGateWarp(gate, dest)
autopilot:      2004::__FlyToNextSector (ceea8): __EnterHyperspace(...) == 3 and this == g10
                -> cef8f 150::StartGateWarp
150::StartGateWarp (15ed2): if m6: return;  m6 = 1;  503::KillMenus(1);
                15ef3  150::StopFastForward()      -> 15588 TI_SetTimeWarpFactor(65536)   (return offset 15ef8)
                ... 5006::StopJumping(); SetWarpSector; 15f6b 150::WarpToSector(...)
```

* **Where:** CODE `15ef3` (file +0x15f07), the call `push0; pushw 150; callcls StopFastForward; drop` (`15eef..15ef8`)
  in `StartGateWarp`; the factor write is `15588` in `StopFastForward`. Stock: `160cc` / `157a1`.
* **Unconditional** for a player transit with SETA on. The only exits before it are `m6 ≠ 0`, where no second
  warp starts, and `m21 = 0`, where SETA is already off. Mayhem's `503::KillMenus` no longer stops SETA itself, though
  stock's does.
* **When:** at gate entry, in the main-loop iteration of the collision. In Mayhem's `WarpToSector` (`16185`) the order
  after it is: `m6 = 1`; `ShowWarpInScene` and `m6 = 2` (`162bd`; `PerFrameInput` starts the fade on key 752 while `IsWarping() == 2`, a skip
  **[i]**);
  fade loops with `TI_Delay(1)` (frames are presented, at 1.0); `m6 = 1` (`1641b`), `StartFade`, `TI_Delay(1)`
  (`16434`, last yield); the stall (`150::EnterSector` `164af`, player `EnterSector`/`WarpEnterSector` `164ea`/`1653c`,
  `605::RestartAllMonitors` `16546`, `ShowWarpOutScene` `16553`); then a Mayhem block for core sectors with notoriety
  ≤ −100 (`165a2..1695b`, up to `TI_Delay(55000)` at `16765`); `m6 = 0` (`16967`). The stock rows in
  sector-transit-order.md §2 have the same order at other offsets. The factor therefore reads 1.0 for the whole
  warp-in scene and fade before the destination exists; their wall duration was not measured **[u]**.
* **Also through `StartGateWarp`:** `2004::JumpToSector` (`d61d0`, `d642a`, jumpdrive to a gate). The jumpdrive also
  stops SETA itself (`d62dd`, `d63fd`), and `StartSectorWarp` (`15f9c`) has the same stop; whether `d62dd` precedes
  `d61d0` at run time was not traced **[u]**.
* **Not involved:** `__EnterHyperspace` (no SETA call, `seta_toggle_study.txt` callers) and the engine flag path
  `0x00412113` (only with manual input during the approach). No restart: `StartFastForward`'s only caller is
  `ToggleFastForward`.

## 3. Re-engagement options (Q3), safest first

### (i) Re-run the script's own start **[m sites, i design]**

**Call.** `0x0049f4c0(0, 0, 0x96, "StartFastForward", 1, 0)`: cdecl, six dwords, caller pops `0x18`, `AL` = 0 on
refusal. This is the EXE's own call shape at `0x0041211f..0x00412133`, with the mod-supplied name (the resolver matches by
content, as for the chase restore's "SelectMode"). Argument 0 must stay 0 (Run147: a non-zero completion slot
crashed). `StartFastForward` and `200::PlaySample` reach no blocking native and no dynamic call (`seta_toggle_study.txt`),
so the task completes inside the call.

**Call address: `0x0049f4c0`, not the chase restore's `0x0049f570`** (EXE bytes, 2026-10-23, for
[seta-restore.md](../architecture/seta-restore.md) **[m]**). Both wrappers take the same arguments
`(slot, cookie, key, name, return kind, argc, args...)`. Both marshal them identically: `0x0049f4c0..0x0049f4fd` and
`0x0049f570..0x0049f5ad` differ only in the scratch register (`ebx` against `ebp`). Both resolve through `0x0049f330`,
both run the task through `0x0049f430`, and both return `AL = 0` when the method does not resolve. After the run,
`0x0049f570` alone looks up the task id `[task+8]` in the hash `[*0x006085e4]`. When the task is still registered with
`[entry+8] ≠ 0` it calls `0x004a21a0`, a read-only scan of the task's stack whose result is discarded (`mov al,1`
follows). It has 46 call sites, among them the input routine's `TranslateKey` and `Input` calls. Whether
the lookup serves a result is **[i]**. `0x0049f4c0` (56 call sites) returns straight after the run. It is the wrapper the EXE itself uses for class key
`0x96`: `StopFastForward` at `0x0041212e` (`add esp,0x18`), `InputAction` at `0x0040f9b2`, `SetAutopilot` at
`0x00410f84`. `StartFastForward` returns nothing, so the restart uses `0x0049f4c0`. The chase restore's `0x0049f570`
stays right for its own calls. `verify_seta_restore_sites.py` pins the whole wrapper (173 bytes) and the EXE's call shape.

It is idempotent: it does nothing when `m21 = 1` (the user restarted SETA)
or `m9 = 1` (killed). It skips `ToggleFastForward`'s ware and dock tests. Both hold after a gate arrival (SETA ran on the
same ship a moment earlier; the player is not docked after `__LeaveHyperspace`) **[i]**.

* **Not `InputAction(675)`.** `0x0049f4c0(0, 0, 0x96, 0x00555e00 "InputAction", 1, 1, 675)` would replay the key
  with all its tests, but it toggles. Inside 250 ms after a user's own start, `m21 = 1` while the factor still reads
  1.0, and the replay would turn SETA off.
* **Site: the input-routine entry `0x00410100`** (`55 8b ec 83 e4 f0`, three whole instructions). It is already the proxy's
  `chase_restore_inject` (`src/proxy/chase_transition.cpp:44`), verified by `verify_chase_restore_sites.py`, and
  flight-proven for a by-name script call (Run 148/149: restored, `exception=0`). It runs once per main-loop iteration
  on the main thread, outside the interpreter (the routine itself makes 13 `0x0049f570` calls plus the
  `0x0049f4c0` calls of §1). The stub saves the general and XMM registers (`pushad`, `src/proxy/chase_transition.cpp`)
  and restores the engine's x87/MXCSR/LastError image for the callee. `[ESP+4]` is the controlled object. The
  site is installed today only with the chase-restore option, so a SETA consumer must share that stub and handler
  rather than patch the site a second time. The call precedes the routine's own stop check at `0x0041210d`. That check
  stays inert because the factor remains 1.0 until the first ramp step, more than 250 ms later.
* **When:** at the first entry after all of: (a) an armed ticket (below); (b) the destination sector Ready by the
  existing walk of sector-transit-order.md §5, with the player ship (`*(M+0x38)`) parented to it (`+0x54`);
  (c) not paused (`[*0x0057fc60+0x4a0] & 1` clear; defer, do not cancel); (d) `[ESP+4]` equal to the armed ship.
  Calling during `ShowWarpOutScene` or Mayhem's core-sector block (`m6 ≠ 0`) is allowed by the script. The proxy
  cannot read `m6` **[u]**; those phases then run at the ramping factor **[i]**.
* **Knowing SETA was on and that the transit ended it.** Per-frame reads of `cfg+0xcc` at Present
  (engine-effects.md §8 Q4) show a drop to exactly `0x10000`. They cannot attribute it. A user stop (key, display or
  speed-bar click, manual input) followed by a gate within the warp-in window looks the same.
  **Recommended: a record-only hook at `0x004b148c`**, the `TI_SetTimeWarpFactor` case of the TI dispatcher
  `0x004b1300` (thunk `0x004b12d0`, jump table `0x004b15f8[9]`):
  * Bytes `a1 34 6f 60 00` (`mov eax,[0x00606f34]`) + `8b 53 01` (`mov edx,[ebx+1]`): 8 bytes, two whole
    position-independent instructions. Entry only through the jump table. No `.text` branch and no dword reference
    targets `0x004b148d..0x004b1493`; the one byte-pattern candidate, `0x004b147c`, is the rel32 of the call at
    `0x004b147b`, and its target `0x004b14b1` lies outside the span.
  * Live: `EBX` = argument cells (tag `[ebx]` = 1 int, value `[ebx+1]`), `EBP` = task, `EDI` = `[task+8]`, `ESI` = TI
    module, four saved registers on the stack. Dead: `EAX`, `EDX`, `ECX` (written at `0x004b148c`/`0x004b1491`/
    `0x004b1494` before any read) and `EFLAGS` (no consumer before `ret 0x10`). Main thread, inside a native dispatch:
    the hook must not call the VM, only record.
  * Rare: SETA changes, ten ramp steps, speech/cut-scene writes. No proxy hook sits in `0x004b1300..0x004b15f8`
    (grep of `src/`).
  * Per call it reads the old factor `[cfg+0xcc]` (not yet stored) and the new one `[ebx+1]`. It walks the task stack
    (`[task+0x14]` cells, `[task+0x18]` top) to the first return record (tag-10 cell directly above a tag-3 cell, the
    walk of `chase_transition_restore_core.h` `RestoreCaller`) and takes its return offset. That record order comes from
    that header, not re-derived here **[i]**.
  * **Arm** when old > `0x10000`, new = `0x10000`, the task IP `[task+0x1c]` is `1558d` (inside `StopFastForward`) and the
    return offset is **`15ef8`** (StartGateWarp). Stock values: `157a6`, `160d1`. Bind the constants to the VM build id
    `[*0x006085e4+0x143c]` = 1462141839; any other build disables the feature. Record the player ship.
  * Other return offsets (Mayhem): `155ad` = user toggle (key or display), `15fa1` = `StartSectorWarp`, `16a07` = QuickWarp,
    `d62e2`/`d6402` = jumpdrive, bottom record (offset 0) = the EXE's input-flag stop.

  Without the hook, a heuristic is the fallback: arm on the drop when the next Ready sector follows within a bound
  W with no other change of `cfg+0xcc`. W is the warp-in scene plus fade and is unmeasured **[u]**. Its false
  positive is the user-stop-then-gate case above.
* **Cancel** an armed ticket on: any later `TI_SetTimeWarpFactor` (the user's own start produces ramp writes from `14341`;
  cut-scenes and speech write too); VM clear or load (the chase restore's cancel sites `0x0049ea80`, `0x004a0880`) or
  the EH epoch; player-ship change; expiry (no Ready sector within a bound). After the gate stop `m21 = 0`, so every
  later `StopFastForward` (docking, death, save) is a silent no-op. Those cases are caught only by the ship,
  epoch and expiry checks.

### (ii) Calling `TI_SetTimeWarpFactor` / writing `cfg+0xcc` directly **[m mechanism, i consequences]**

Possible at the same `0x00410100` entry, or as a plain aligned store at Present (main thread, between frames). The
script state stays `m21 = 0`, `m22 = 0`, and that breaks:

* Every stop becomes a no-op because `StopFastForward` tests `m21`: docking (`CanLand`, `__LandInHangar`), death,
  cut-scenes, save, the next gate, and the EXE's input-flag stop at `0x00412113`. SETA can then only be ended by a
  key press or a direct write.
* **The next SETA key press "turns it on":** `ToggleFastForward` sees `m21 = 0` and calls `StartFastForward`. The
  factor keeps the injected value until the first ramp step, more than 250 ms later. That step drops it to 1.9, and
  the ramp climbs back to `m24`. Only a second press ends SETA.
* No start sound (980). The cockpit indicator `INS_SetTimeWarpFactor(m22)` and `GetWarpMultiplier` (display panel,
  galaxy take) read "off".
* A save made in this state stores the factor (`0x0040481e`, restored at `0x00404f9d`) with `m21 = 0`, so the
  inconsistency survives a load.

### (iii) Bytecode patch: keep SETA through the gate **[m patch, i side effects]**

* **Patch.** Retarget the `callcls` at `15ef3` from `StopFastForward` (`15557`) to `IsFastForward` (`15590`; argc 0,
  `frame; ldmem; ret`, pure). The BE32 operand is at file +0x15f08..+0x15f0b and only its last byte changes: **1 byte**.
  Stock: `160cc`, `15770` → `157a9`, file +0x160e1..+0x160e4, also 1 byte. Built in memory for both builds:
  length, build id and every instruction start are unchanged, and the stack balance is the same
  (`argc; class; call; drop`). Saves stay compatible under gate-queue-script.md §1/§6.
* **Delivery.** A loose `<game dir>\L\x3story.obj` (plain bytes; §8 there). It must be produced by the same patcher as
  any gate-queue option, because only one file wins.
* **Callers affected.** `StartGateWarp` runs only for the player: both gate callers test `this == g10`. The other caller,
  `JumpToSector`, has its own stops (`d62dd`, `d63fd`); their run-time order relative to its `StartGateWarp` call is
  untraced (§2). AI ships are unaffected **[m]** guards.
* **Side effects [i]:**
  * The warp-in scene and the game-time fade (`150::Input`'s `m49..m51`, `TI_GetAbsTime`) run k× faster.
  * The first frame after the stall steps up to `round(200·f)` game ms (engine-effects.md §8 table): 2 s at ×10
    instead of 0.2 s. The arriving ship and nearby traffic move 2 s in one frame.
  * Mayhem's core-sector block runs k× faster.
  * The user hears no stop/start sounds.
  * This changes the game design rather than restoring a state, and it does not reach the jumpdrive.

## 4. Other stops, so a restore never follows one of them (Q4) **[m]** sites

Mayhem callers of `StopFastForward`. Each is a no-op while `m21 = 0`, so after an armed gate stop none produces a
factor write.

| Trigger | Site(s) | Note |
| --- | --- | --- |
| SETA key / cockpit display click | `ToggleFastForward` `155a8` (actions: 675; `605::ClickDisplayPanel`) | the user's own stop, return `155ad` |
| manual speed/steer input with warp ≠ 1 | EXE `0x0041212e` | engine-originated task (bottom record) |
| gate (manual and autopilot) | `StartGateWarp` `15ef3` | the transit stop |
| sector warp / player transfer | `StartSectorWarp` `15f9c` (`2004::MovePlayerTo` `c6a71`, `JumpToSector`) | |
| jumpdrive | `2004::JumpToSector` `d62dd`, `d63fd` | before its own warp start |
| QuickWarp | `150::QuickWarp` `16a02` (`2228::Input` hotkey, `308::DoWarpPlayer`) | |
| player death | `150::Killed` `144f6` | `m9 = 1` also blocks `StartFastForward` |
| planet collision | `150::NotifyPlanetCollision` `17c7f` (EXE by name at `0x0045da9a`) | |
| docking / landing | `2004::CanLand` `dfa8c`, `__LandInHangar` `cfe4a`, `__MoveToHangar` `d06f0`, `5003::Action2` `1ca7a7` | |
| ship change | `2004::MovePlayerTo` `c6af7`, `2004::ChangePlayerShipTo` `df6a6`, MSCI 1335 "move player to ship" (`702::__runScript` `72f32`) | |
| speed-bar click | `605::ClickInfoPanel` `102441` | |
| save | unnamed helper `0bd7f` (no CLAS row; builds the save description; called by `640::StartSave`, `150::SetLeave`, `2226::ExitMenu`, `2016::RunPlayerTrade`, `__runScript`) `0be1c` | role from its body **[i]** |
| trade with player | `2016::RunPlayerTrade` `b2315` | |
| cut-scene / movie / beam | `624::Activate` `23e07`, `308::DoPlayMovie` `1b3840`, `5005::BeamStart` `1cc102`, `5006::ContextMenuReturn` `1cc8ed`, `1cc9bc` | |
| MSCI "stop SETA" | `702::__runScript` case 1927 `613cf` | no installed script uses it (0 of 1,994) |
| debug | `203::B3DReload` `00ab2` | |

Direct factor writes that bypass `m21`: `621::CutEvent` (1.0), speech with a duration
(`200::SpeakArrayWithPriorityAndFaceAndDuration[WithSubtitle]`: factor = duration / voice length;
`2225::SpeakQuestionWithDuration` resets 1.0), and the cheat actions 971/972.

**Mayhem removed** stock's stops in `NotifyCollisionWarn` (ship-navigation-ai.md §2), `NotifyMissileAlert`,
incoming messages (`ReceiveMessageWithPriority`, `PlayMessageWithPrioAndFace[AndNoise]`), `501::PerFrameInput` and
`503::KillMenus`. **Being attacked does not stop SETA in either build:** `NotifyAttacked` has no stop. Missiles, messages and
menus do not stop it under Mayhem.

## 5. What remains unknown (Q5)

* The wall duration from the gate stop to the Ready destination (warp-in scene + fade), so W for the hook-less
  fallback; and whether `JumpToSector`'s own stop precedes its `StartGateWarp`.
* The stack-record layout at the TI native (taken from the chase core), and that `0x0049f4c0` resolves the
  mod-supplied `"StartFastForward"` for key `0x96`. The latter is the same resolver `0x0049f330` as the flight-proven
  chase call, not yet exercised with this name.
* How `PerFrameInput` maps keys to action 675; whether `m6` (the warp phase) is readable from the class-150 static
  context. (The nine input flags are resolved in [seta-manual-flight.md](seta-manual-flight.md).)
* The behaviour of `ShowWarpOutScene` and Mayhem's core-sector block when SETA restarts before `m6 = 0`.

**One trace settles the first two items.** Add a log-only hook at `0x004b148c` to the next consolidated
diagnostic build. Per call it logs frame, wall ms, old/new factor, `[task+0x1c]`, the first return-record offset and
the build id. Add the existing Ready-sector row and a per-entry counter at `0x00410100`. Fly one gate with SETA on
and press the SETA key once afterwards. Expected: an arm row with IP `1558d` and return `15ef8` at the gate frame;
the Ready row N frames later, which is W; a user-toggle row with return `155ad`. A row with any other return offset
names a stop this note has not mapped.

## Reproduce

```sh
python3 -I verification/results/seta-script-toggle/seta_toggle_study.py > verification/results/seta-script-toggle/seta_toggle_study.txt   # RESULT PASS
python3 -I verification/results/seta-script-toggle/verify_seta_restore_sites.py   # the X3M_SETA_RESTORE constants; RESULT PASS
```
