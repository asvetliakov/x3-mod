# SETA restore after a gate transit

`X3M_SETA_RESTORE=1` (schema `seta_restore`, section `[engine]`, default `1`; launcher `--seta-restore` /
`--no-seta-restore`, refused and never forwarded under `--vanilla`). Source: `src/proxy/seta_restore.cpp`,
`src/proxy/seta_restore_core.h`; the shared injection site lives in `src/proxy/chase_transition.cpp`. Ledger:
[seta-restore.md](../verification/seta-restore.md). Mechanism and evidence:
[seta-script-toggle.md](../reverse-engineering/seta-script-toggle.md).

## Mechanism

The story's `150::StartGateWarp` calls `150::StopFastForward` at CODE `15ef3` on every player gate entry, on both the
manual and the autopilot path. That method writes the warp factor 1.0 through `TI_SetTimeWarpFactor` (native at `15588`,
task IP `1558d`) and clears the script's SETA state `m21`. Nothing in the script starts SETA again. The feature
implements option (i) of the note's section 3: it restarts SETA through the script's own `StartFastForward`. That keeps
`m21`, the ramp, the sound and the cockpit indicator consistent, and the next SETA key press stops SETA as the player
expects.

1. **Arm, record only.** An 8-byte trampoline sits at `0x004b148c`, the entry of the TI dispatcher's case 9
   (`TI_SetTimeWarpFactor`). The span is `mov eax,[0x00606f34]; mov edx,[ebx+1]`: two whole position-independent
   instructions, reached only through jump-table entry `0x004b15f8[9]`. EAX, ECX, EDX and EFLAGS are dead there. The
   stub saves EFLAGS, the eight general registers and XMM0..7, and passes EBX (argument cells) and EBP (task) to the
   handler. The handler runs under `LightCallBoundary` and makes checked reads only. It never calls the VM. It reads the
   old factor `[cfg+0xcc]` (not yet stored), the new one `[ebx+1]` with its tag, the task IP `[task+0x1c]`, and the
   first live return record of the task stack. The return scan applies the return opcode's own rule (`0x004a39b3`) and
   reads at most 128 cells. A ticket is armed when all of these hold:
   * old > `0x10000`, new = `0x10000`, tag 1;
   * IP = `1558d` and return offset = `15ef8`;
   * the player ship `*(M+0x38)`, its sector `+0x54`, the sector id `+8` and the ship's flags `+0x40` are readable;
   * the departure sector id differs from that of the previous arm (one arm per transit);
   * the ship is under autopilot: `[ship+0x40] & 0x80000`. A manual gate pass logs `skipped reason=manual` and arms
     nothing; the departure sector is not recorded, so a later autopilot stop from it still arms.

   The autopilot state comes from the engine flag, not from the KC caller of `StartGateWarp`. The flag is one checked
   read at the hook. `SA_SetAutopilot` sets and clears it (`or [eax+0x40],0x80000` at `0x00463e88`, `and` at
   `0x00463ea7`), and `SA_GetFlags` reads it (`mov eax,[eax+0x40]` at `0x004637df`). The manual path's
   `2004::CanWarp` calls `StartGateWarp` only when `SA_GetFlags & 0x80000` is clear, so a manual pass reads the bit
   clear in the same main-loop iteration. The autopilot path `2004::__FlyToNextSector` runs as the ship's script, so
   `SA_SetAutopilot(ship, IsAutopilotActive())` has set it ([seta-script-toggle.md](../reverse-engineering/seta-script-toggle.md)
   section 2, [ship-navigation-ai.md](../reverse-engineering/ship-navigation-ai.md) "Player autopilot"). The caller
   record one frame below `15ef8` (`cef8f` or `df887`) would need a second return-scan step and more story offsets
   bound to the build. The verifier pins the three instructions.

   The stop IP and the gate return are detected at run time in the loaded object (`story_detect.h`,
   [story-builds.md](story-builds.md)): once per VM key (pointer, CODE, size, build), at the first SETA stop after it
   changed. Mayhem gives `1558d`/`15ef8`, stock AP `157a6`/`160d1`. An object where they are not found uniquely
   (x3intro, a changed story; a known build that detects differently from `story_builds.h`) logs one
   `refused reason=story_sites detect=<reason> method=<method>` row per build and reason and never arms. When the VM
   key cannot be read, that write does not arm: one `refused reason=build_unreadable` row is logged (once per
   process), and the feature stays armed for later writes.
2. **Transit complete.** The chase restore's injection site `0x00410100` (the input routine's entry, once per main-loop
   iteration, main thread, outside the interpreter) runs the consumer's poll only while a ticket is active. The poll
   takes no new reader. Once per presented frame it runs the fog hand-over's walk of
   [sector-transit-order.md](../reverse-engineering/sector-transit-order.md) section 5 (`fog_prefill::walk`, at most 25
   checked reads, `engine_memory::revalidate` first). The destination is Ready when the walk returns `Found` with an id
   other than the departure sector's and the player ship's parent `+0x54` is that node. The call waits two further
   presented frames. It also waits while the game is paused (`[*0x0057fc60+0x4a0] & 1`) and while `[ESP+4]`, the
   engine's controlled object, is not the armed ship.
3. **Restart.** The site makes the call `0x0049f4c0(0, 0, 0x96, "StartFastForward", 1, 0)` (cdecl, six dwords, AL = 0
   when the method does not resolve). It runs under the engine's incoming x87/MXCSR/LastError image, and the clean state
   is re-established afterwards. `StartFastForward` is idempotent: it does nothing when `m21 = 1` (the user already
   restarted SETA) or `m9 = 1` (killed). It only sets the state, and `150::Vbi` then ramps the factor in ten steps of at
   least 250 ms each, as after a key press. The chase restore uses `0x0049f570` for its calls. For a class-static call
   without a result the right wrapper is `0x0049f4c0`, the EXE's own wrapper for `StopFastForward` and `InputAction`
   (note section 3 (i), "Call address").
4. **Cancel.** Any of these ends the ticket:
   * any later `TI_SetTimeWarpFactor` while armed or due: the user's key (the ramp writes), a cut-scene, speech, the
     cheat keys;
   * the player ship becoming unreadable, or a player-ship change that is not the transit's re-pointing (below);
   * VM clear `0x0049ea80` (session start) or VM load `0x004a0880` (save load);
   * the EH adapter `0x0052f298` (a script error unwound);
   * the ship leaving the Ready sector before the call;
   * a call that never reported back;
   * **expiry**: 60 s of wall time after the arm. There is no frame bound: a frame count would expire sooner at a high
     frame rate (3600 frames are 30 s at 120 fps).

   The warp-in scene, the fade and the stall have unmeasured wall time (note section 5), so the bound is generous. From
   the arm on the factor is 1.0, so game time runs at wall time and no SETA scaling applies to the bound. Docking,
   death and saving after the gate stop reach `StopFastForward` with `m21 = 0` and write nothing. Only the ship, epoch
   and expiry checks see them. The window between Ready and the call is two frames.

   **Player-ship re-bind.** The gate transit re-points `*(M+0x38)` once. In Run 190 every arm (three) was cancelled
   `ship_change` at the first injection-site entry after the stall, 183 to 202 frames after the arm, before any Ready
   check, and each new pointer stayed stable until the next arm. So one change is accepted while armed or due. The new
   object must read fault-free as a live ship (class word `+0x48` = 7, marker `+0x9c` = `0xcafe`) whose ship-script id
   `+0x94` equals the one read at the arm. That id is the script key the engine passes to `0x0049f4c0` for the ship
   (`SA_GetEventObject` `0x004607f4`, the `CanLand` call `0x0045d8a3`); Run 190's chase rows show one value,
   `0xfffebb1e`, across every re-pointed object. The arm refuses `unreadable` when it reads 0 or not at all. The new
   object's parent `+0x54` must be a live sector (class 1, `0xcafe`) with a non-zero id `+8`, and that id must not be
   the departure sector's. The ticket then follows the new ship (`rebound` row, with the new ship's flags `+0x40`
   recorded) and continues at the next entry.

   A live ship with the right id whose parent is not yet a live sector (null, unreadable, misaligned, not class 1 or
   not alive, id unreadable or 0) defers. Nothing is cancelled and the re-bind is not used up. A `rebind_pending` row
   is logged once per ticket, and the check runs again at each later entry until the parent reads as a live sector or
   the 60 s expiry cancels the ticket. The check runs against whatever `*(M+0x38)` holds at that entry. A second
   change, an unreadable or non-ship object, an unreadable script id, a different script id, or a change inside the
   departure sector cancels `ship_change`. The identity check runs before any sector read, so a different ship never
   defers. The row's `ship_check` names which of these it was (`second`, `unreadable`, `not_ship`, `identity`,
   `same_sector`). It keeps the ticket's own ship in `ship=` and adds the new pointer as `new_ship=`.

## Sharing the injection site

`0x00410100` and the three VM cancel sites are claimed once, by whichever user attaches first:
`chase_transition::inject_attach` for this consumer (in `initialize_log`, the earlier of the two), or the chase restore's
`initialize()`. The second user attaches to the live sites. The stub's prefilter word has bit 0 for the chase ticket and
bit 1 for this consumer (`inject_want`). At each entry the chase ticket runs first, then the consumer's queued call. The
VM clear/load sites call the consumer's `vm()`, and the EH adapter calls its lock-free `unwind()`. Neither module holds
a lock across the engine call. A refused attach rolls the hook at `0x004b148c` back. The feature then logs
`seta_restore refused reason=<site status>`, and nothing of it stays live.

Since 2026-10-25 the hook and the consumer also carry the SETA key stop latch of `seta_manual_flight`
([seta-manual-flight.md](seta-manual-flight.md#key-stop-latch-mode--1)). The factor hook arms it on
`ToggleFastForward`'s stop. The consumer's `poll()` hands out `StopFastForward` on entries the ticket does not use.
Bit 1 is set while a ticket is active, while the latch is armed, or always under `debug = 1`. The ticket's decisions
and rows are unchanged; any factor write still cancels an armed ticket, the latch's own stop included.

## Fail closed

Before any write, the installer checks:
* `object_trace::executable_verified()` and the engine_patch install window (`late_claim` otherwise);
* ten byte windows: the hook span; the rest of case 9; the dispatcher head; the native thunk; the jump-table entry; the
  interpreter's native-call operand and IP stores; the return scan; the whole wrapper `0x0049f4c0`; the EXE's own
  `StopFastForward` call shape (`core::windows`, each read whole and compared raw: no other claim overlaps one).

Any mismatch logs one refusal row, and nothing is claimed. The globals read are `0x00606f34`, `0x006085e4`,
`0x0060850c` and `0x0057fc60`. Each is an anchor of `executable_identity.h`.
`verification/results/seta-script-toggle/verify_seta_restore_sites.py` pins every constant of the core header against
`X3AP.exe`.

## Log rows (`seta_restore …`)

| Row | When | Fields |
| --- | --- | --- |
| `installed=1` | install | hook, write kind, stub, inject site status, call, key, method, `detect=runtime`, `expected=` (the known builds' IP and return), due frames, expiry ms |
| `refused reason=executable_mismatch / late_claim / bytes_mismatch / pin_failed / <claim status> / <inject status> / rollback_failed` | install refusal | first mismatching window (`window`, `window_name`, `expected`/`got` first 8 bytes; `-` otherwise), hook status, inject status, patched |
| `refused reason=story_sites` | a SETA stop in an object whose sites were not detected (once per build and reason; never arms) | build, object, `detect`, `method`, known builds |
| `refused reason=build_unreadable` | a SETA stop whose build id could not be read (once; not latched) | old factor, IP, return |
| `refused reason=same_transit / unreadable` | arm refused | old factor, IP, return, sector id |
| `armed` | the gate stop under autopilot | old, new factor, IP, return offset and name, ship, ship id, `script_id` (`+0x94`), sector, sector id, build, `autopilot=1`, `flags` |
| `skipped reason=manual` | the gate stop with the autopilot bit clear (nothing armed) | the `armed` fields, `autopilot=0` |
| `rebound` | the transit's one player-ship change accepted | frames and ms since the arm, old and new ship, old and new ship id, `script_id`/`new_script_id`, departure sector id, new sector and its id, `ship_check=rebound`, the new ship's `new_flags_read`, `new_flags` (`+0x40`) and `new_autopilot` (record only) |
| `rebind_pending` | first entry (per ticket) at which the new ship has the right identity but no live sector yet | the `rebound` fields, `ship_check=no_sector`, `new_flags_read=0` |
| `cancelled reason=factor_write / ship_change / vm_clear / vm_load / epoch / expiry / sector_left / call_unwound` | ticket ended | frames and ms since the arm; for `factor_write` the write's old/new/IP/return offset and name (`toggle`, `sector_warp`, `quick_warp`, `jumpdrive`, `engine_input`, `other`); `ship=` (the ticket's ship), `ship_check`, `new_ship`, `script_id`, `new_script_id`, `new_sector`, `new_sector_id` (for `ship_change`) |
| `restored` | after the call, AL = 1 | frames after the transit (Ready to call), frames and ms since the arm, `result=1`, call, key, method |
| `restore_failed` | after the call, AL = 0 (the method did not resolve) | the same fields, `result=0` |

Every row except `refused` also carries `speed_ship speed_read speed desired_speed`: the current player ship
`*(M+0x38)` and its `+0x10` (current) and `+0x14` (desired) speed, int32 position units/s
([ship-movement.md](../reverse-engineering/ship-movement.md) section 2), read when the row is formatted
(`speed_read=0`: unreadable). Record only, for the open question of the ship's speed reset to 0 after a gate: the
`armed` row shows the speed at the gate stop, the `rebound`/`cancelled`/`restored` rows the speed after the arrival.
`SA_WarpObjectAway` (order 8) writes `[obj+0x14] = [obj+0x10] = 0` every frame before its deadline
(ship-movement.md section 13.3); whether the player's gate transit runs it is not established.

## Windows and CrossOver parity

The feature uses no D3D or Wine call. It needs the same EXE offsets, documented Win32 only (`GetModuleHandleExW` pin,
`GetTickCount64`, SRW locks) and the engine_patch claim protocol. Shared production code behaves the same on native
Windows and CrossOver. Not verified on either yet: no flight has run.

## Hot-path cost

* **Hook:** runs only on a factor write. That is rare: a SETA start or stop, the ten ramp steps, speech and cut-scenes.
  Each write costs 5 checked reads and one uncontended SRW lock. The build read and the stack scan (3 task fields, one
  read of at most 640 bytes, about one context read per record) run only for a write that can arm or cancel.
* **Per-frame check:** only while armed. The default launch claims the chase restore's four shared sites for this
  feature too, so they are live even with the chase camera or its restore off. While idle, the stub's prefilter compare skips the injection site
  (`cmp [filter],0; je`), and nothing else runs per frame except `present()`'s relaxed store. While armed, each entry
  costs the chase stub's CPU-state capture, about 4 checked reads (player, pause), and the transit walk at most once
  per presented frame. The ticket lasts from the gate stop to a few frames after arrival. The re-bind check (at most 9
  reads, plus 1 for the flags when it re-binds) runs once per transit, or once per entry while it is pending. Each row
  adds 4 reads for the speed fields (player, speeds), and the arm 3 (flags, ship id, script id).
* No allocation. Rows go through the session log, and the only ones are the event rows above.
