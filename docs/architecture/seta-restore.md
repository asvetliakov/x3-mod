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
   * the player ship `*(M+0x38)`, its sector `+0x54` and the sector id `+8` are readable;
   * the departure sector id differs from that of the previous arm (one arm per transit).

   The script constants are bound to story build `[*0x006085e4+0x143c]` = 1462141839 (the installed Mayhem
   `addon/07.cat`). The first SETA stop on a readable other build logs one `refused reason=story_build` row, and the
   feature then stays off for the process. When the build id cannot be read, that write does not arm: one
   `refused reason=build_unreadable` row is logged (once per process), and the feature stays armed for later writes.
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
   * a player-ship change, or the player ship becoming unreadable;
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

## Sharing the injection site

`0x00410100` and the three VM cancel sites are claimed once, by whichever user attaches first:
`chase_transition::inject_attach` for this consumer (in `initialize_log`, the earlier of the two), or the chase restore's
`initialize()`. The second user attaches to the live sites. The stub's prefilter word has bit 0 for the chase ticket and
bit 1 for this consumer (`inject_want`). At each entry the chase ticket runs first, then the consumer's queued call. The
VM clear/load sites call the consumer's `vm()`, and the EH adapter calls its lock-free `unwind()`. Neither module holds
a lock across the engine call. A refused attach rolls the hook at `0x004b148c` back. The feature then logs
`seta_restore refused reason=<site status>`, and nothing of it stays live.

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
| `installed=1` | install | hook, write kind, stub, inject site status, call, key, method, story build, IP, return, due frames, expiry ms |
| `refused reason=executable_mismatch / late_claim / bytes_mismatch / pin_failed / <claim status> / <inject status> / rollback_failed` | install refusal | first mismatching window (`window`, `window_name`, `expected`/`got` first 8 bytes; `-` otherwise), hook status, inject status, patched |
| `refused reason=story_build` | first SETA stop on a readable other story build (once; latched off) | build, expected build |
| `refused reason=build_unreadable` | a SETA stop whose build id could not be read (once; not latched) | old factor, IP, return |
| `refused reason=same_transit / unreadable` | arm refused | old factor, IP, return, sector id |
| `armed` | the gate stop | old, new factor, IP, return offset and name, ship, sector, sector id, build |
| `cancelled reason=factor_write / ship_change / vm_clear / vm_load / epoch / expiry / sector_left / call_unwound` | ticket ended | frames and ms since the arm; for `factor_write` the write's old/new/IP/return offset and name (`toggle`, `sector_warp`, `quick_warp`, `jumpdrive`, `engine_input`, `other`) |
| `restored` | after the call, AL = 1 | frames after the transit (Ready to call), frames and ms since the arm, `result=1`, call, key, method |
| `restore_failed` | after the call, AL = 0 (the method did not resolve) | the same fields, `result=0` |

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
  per presented frame. The ticket lasts from the gate stop to a few frames after arrival.
* No allocation. Rows go through the session log, and the only ones are the event rows above.
