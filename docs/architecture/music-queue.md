# Sector-music track queue

`src/proxy/music_keep.cpp`, `music_keep.h`, `music_keep_core.h` ("The track queue"). Option `music_queue` (`[audio]`,
default 1, `requires music_keep`), launcher `--music-queue` / `--no-music-queue` (`X3M_MUSIC_QUEUE=1`, sent only with
`--music-keep`, refused under `--vanilla`). Docking: option `music_dock_keep` (`[audio]`, default 1, `requires
music_queue`), launcher `--music-dock-keep` / `--no-music-dock-keep` (`X3M_MUSIC_DOCK_KEEP=1`, sent only with the queue,
refused under `--vanilla`). Engine and script facts:
[music-script-driver.md](../reverse-engineering/music-script-driver.md) sections 1, 3, 7, 8, 10 (the policy is section
8.3 as amended by 8.4) and [music-restart.md](../reverse-engineering/music-restart.md) sections 1, 6 and 7.7. Ledger:
[verification/music-queue.md](../verification/music-queue.md). First flown in release 1.5.1 (2026-10-25 log: 15
episodes); the save keep and the docking keep (2026-10-25) are unflown.

## Problem

Vanilla switches the sector track at once: the story's new track task C stops the playing record at the `MOV_StopMovie`
native (KC IP `0x0db54`), the old task frees it in the same scheduler pass, and C loads and plays the new id about
100 game ms later. A gate or jumpdrive change does not even reach that stop: `150::WarpToSector` forks a child that fades
the old track out at the gate (`StopMusic(500)`, stop IP `0x0da17`), and the arrival's request plays with nothing to stop.
The user wants the playing track to finish first, the newest sector's track to follow it, and a track that ends with no
change pending to loop as vanilla does.

## Mechanism

Three states, kept in `TrackQueue` (pure functions in the core header, host-tested): IDLE, ORPHANED and PARKED.

1. **Defer (T3, the shared `0x00498810` entry, IDLE → ORPHANED).** At a `MOV_StopMovie` from the native caller
   `0x00499886` whose calling task (ESI) sits at KC IP `0x0db54` (`PlayMusicWithFade`'s stop), the handler reads the
   frame cells of the `ChangeTrackMusic` fork child by absolute index from the end of the cell buffer
   (`[ESI+0x14]`; valid cells `-capacity..-1`; the stack index `[ESI+0x18]` is -19): old id at -18 (must equal EAX),
   the `LoopMusic` return `(3, 0x0dc84)` at -13, the new id N at -11 (int), fade at -10 (0), loop at -9 (1) and the
   `ChangeTrackMusic` return `(3, 0x0dcdb)` at -7. The IPs and the two return offsets are the ones detected in the loaded
   object at run time (Mayhem values shown; [story-builds.md](story-builds.md)). N must be greater than 0 as a signed
   id (0 and every negative id play at once, section 8.3) and not 13 (death), 14 (landing) or 8233..8237 (boarding);
   the alert record `*0x00608504` must read `[R+4] == 0 && [R+8] == 0` (a combat start stops inside `NotifyAlert` with
   `[R+8] != 0`, a combat end with `[R+4] != 0`); the stopped id must not be the id of the last music play made with
   the alert on (`combat_old`; no combat-id list since 2026-10-23); the
   first record with that id must be music and playing (`& 0x83 == 0x82`), owned (`+0x14 != 0`, not by the caller),
   with a slot below 32 and a media object. Then the handler re-keys the record to a private id `P = 0x7f000000 + n`
   (outside 101..299 and every script id, so `MOV_LoadMovie` never reuses it and the save's MOVI writer never sees it
   armed), keeps flag 2 and skips the engine's pause, delivers `([O+0x14], 1)` through the record's own slot `[O+0x18]`
   (T_old wakes and its `MOV_FreeMovie(old)` misses), zeroes both, delivers `(C, 1)` through EDI (C continues
   synchronously, exactly the engine's own order at `0x0049888c`/`0x004988c1`), and the stub returns to
   `MOV_StopMovie`'s caller instead of running the body. Completions go through the slot table as the engine does them
   (`owner = *0x006085e4`, `[owner+4] != 0`, slot < 32, function at `owner + 0x34 + 0x18 * slot`, cdecl `(ctx, status)`;
   they only relink tasks). Any mismatch takes the vanilla path with one `music_queue event=refuse` row carrying the
   cell values. `m17 = N` from here on, so the script accepts every other request as vanilla.
2. **Park (T2, the shared play entry `0x00498c90`, ORPHANED → PARKED).** C's first `MOV_PlayMovie(N)` from
   `0x00499824` (`[ESI+0x1c] == 0x0dbf7`, a2 == C, a3 == N) while O is still linked under P with flag 2 and context 0:
   the handler writes C's task id and slot into O's `+0x14`/`+0x18` and the stub returns without the play routine, so C
   suspends as vanilla does (the handler never completes synchronously) and N's record (created by `MOV_LoadMovie`)
   stays unplayed. If O already ended, the play proceeds vanilla and P is freed (`ended_before_park`).
3. **Natural end and release (PARKED → IDLE).** The media manager's natural end of O clears flag 2 and fires
   `(C, 1)` through O's slot (it never checks who stopped the record). C's KC replay (`m17 == N`, loop set) calls
   `MOV_PlayMovie(N)` again; at T2 the handler frees P through the engine's delete-by-id `0x004986b0` (ECX = key; a
   miss if the engine freed O) and lets the vanilla play run: N seeks to 0 and starts. The keep's Patch C sees an
   unplayed, unheld record and seeks to 0 as vanilla.
4. **Collapse (PARKED → ORPHANED).** A further deferrable stop of N by another task D (another jump before O ended):
   deliver `(C, 1)` through O's slot, zero both, deliver `(D, 1)` through EDI, return. C frees N's unplayed record and
   exits with `m17 = -2`; D loads and plays the newest id M, which parks on the same O. The newest id always wins.
5. **Ends with the vanilla fallback.** C is woken through O's slot when O still names it, O's context and slot are
   zeroed, P is freed, and the event continues vanilla. The calling task itself is never woken (its `+0x20` must stay
   clear so the native suspends it as vanilla does); when O still names it, O's `+0x14`/`+0x18` are zeroed before the
   free all the same (`clear=1`), so the free's teardown `0x004984d0` cannot deliver `(C, 1)` either. The events: a
   stop of N that is not deferrable (a combat start inside `NotifyAlert`, `StopMusic` with new id 0, a `MusicFadeOut`
   stop at IP `0x0da17`, which is never deferred), a stop of any other music record, any music play at T2 other than
   C's own `MOV_PlayMovie(N)` (every caller of `0x00498c90`: `MOV_PlayMovie`, `MOV_PlayMovieFrom` and the helper
   `0x004f6640`; the record's music flag `0x80` decides, not the caller), every stop-all at the T1 entry but alt-tab
   under `skip_all`, a save item 6 keeps and a pause item 6 hands back (pause, alt-tab under `paused`, load, P_Leave,
   session start, unknown, and a save item 6 refuses; before the walk, so neither O nor P is visited), and a nonzero committed alert level `[R+4]` sampled at Present while the queue is active (a combat start
   the script dropped while C was in `TI_Delay`). An orphan that ended with nobody left to wake (flag 2 clear, context
   0) or that is no longer linked under P is freed or forgotten at the next event (`stale`, `lost`). Alt-tab depends on
   the keep's alt-tab mode in effect: under `skip_all` (Patch D live) it leaves the queue alone, as `skip_all` leaves O
   untouched; under `paused` (`status_gate` refused) it ends the queue like every other caller (C woken, O's context
   zeroed), but P is freed at the next Present
   (`event=free_later`), because alt-tab's T1 can run inside a COM call of the manager's walk (media-record-lifetime.md,
   boundary 1).
6. **Through a save (event `keep`), and a pause of a same-id episode (`end reason=handback`), 2026-10-25.** At the
   save's T1 (caller `0x00404561`, only with the save pump bound: `music_keep_save_pump_hook reason=ok`) the queue stays
   when O is still as the queue left it: linked under P with the same media, music, DirectSound path (`0x40`), flag 2
   set, loop bit clear, and context C (PARKED) or 0 (ORPHANED). The same stop-all's walk meets O at Patch A, which
   returns `skip_all` for it (no hold; flag 2, context and graph untouched). **Pause:** a pause cannot be kept the same
   way. The iteration that sets it runs the scheduler and the manager once, then enters the pause wait `0x00404280`
   (from `0x00403db8`), whose loop `0x004043a0`..`0x004043d6` calls only `0x004d34b0` and `0x004d3b60` until the
   unpause (`0x004043d8`), so nothing pumps O and its DirectSound ring would repeat stale audio; the keep resumes a
   paused track only through the script's replay of the same id (Patch C, Run), and nothing replays P. So a pause ends
   the queue as before (C woken, P freed, N from 0 after the unpause), except a same-id episode (old id = new id: what
   `music_dock_keep` leaves while docked), which is **handed back**: with O still as the queue left it and the first
   record under the id being the task's own (unplayed, unowned, with media) or none, the handler frees that record by
   id while O is still under P, then writes the id back into O. That is vanilla's state (the playing record under the
   script's `m17`, the task waiting on it), so the walk pauses O as the keep pauses any music record under pause
   (`paused`, held; the vanilla completion wakes the task through O's slot) and the task's replay after the unpause
   finds O, skips the seek and resumes it at its position. Should another record still carry the id after the free,
   O keeps P and P is freed instead (the end of before; `freed=2`). One race window remains: a docking defer wakes
   T_old, which runs only in the next scheduler pass; a pause in the same iteration hands O back under S, and T_old's
   `MOV_FreeMovie(S)` after the unpause then frees O, so C plays S from 0. That is no worse than the old end, and the
   keep's hold left on the freed record is dropped safely (its checks go through `live_record`). A pause while an
   ordinary sector change is pending
   ends the queue as before. **Alt-tab under `paused`** (Patch D not live) ends it as before: without RunInBackground
   `GetMessageA` blocks the loop in `0x004d34b0`, and its T1 may run inside the manager's walk, where nothing is freed.
   **Save:** PARKED delivers `(C, 1)` through O's slot and zeroes O's `+0x14`/`+0x18`, exactly the
   completion vanilla's stop-all gives the music task, so the script writer saves C runnable (a task saved suspended on
   a media completion is never woken after a load, [music-restart.md](../reverse-engineering/music-restart.md) §7.3);
   the state goes back to ORPHANED with the same N and C. The walk also arms the save pump on O in queue mode
   (`SavePump::arm_queue`: the check expects flag 2 set, nothing waits for a replay, no early stop pauses it; the
   episode and its `music_keep_save_pump … run=queue` row end at the save's gzclose, also after an early stop). When the
   pump is already taken by another record of the same save, Patch A gives O the vanilla stop instead (paused, flag 2
   and context cleared) and C's replay after the save ends the queue (`ended_before_park`), N from 0 as before. A pump
   return of 2 (O ended during the save; its buffer stopped, `[m+0x48]` back to 1, which the pump head would re-arm at
   the manager's next poll) makes the handler clear O's flag 2 at once, as the manager's natural end would (its context
   is 0 after the save's wake), so the manager never polls it again; C's replay then ends the queue
   (`ended_before_park`) and N starts, any other event frees O (`stale`). After the save the manager
   services O again (flag 2), C replays `MOV_PlayMovie(N)` in the next scheduler pass and parks on O again (item 2),
   and O's natural end releases N as without the save. The savegame carries O under its key P with flag 2 and context
   0: the restore masks the flag and recreates P through the allocator, whose constructor finds no
   `soundtrack\2130706xxx.*` file, destroys the media and returns 0, so a load recreates nothing (§7.7 [i]); N's
   unplayed record and C runnable with `m17 = N` are what a load restores, so that save plays N from 0 when loaded.
   Any mismatch (pump unbound for a save, O ended, not DirectSound, loop-flagged, another context, gone) ends the queue
   as before (item 5). The entry stub calls its handler again while it returns holds to pause; the repeat call in the
   same stop-all changes nothing (`walk_caller` lives until the walk meets O or the next Present).
7. **Docking (`music_dock_keep`, 2026-10-25).** At T3 a request whose new-id cell (-11) is a docking track, 14
   (`150::DockingAllowed`'s `ChangeTrackMusic(14)`, the only literal request of it) or 2001..2004
   (`EnvironmentToMusicTrack` for a non-sector environment, `2001 + SE_Random(4)`), is taken as a request for the
   stopped id (EAX, the script's `m17`): every other test of item 1 (cells, alert 0/0, `combat_old`, a playing owned
   record) and item 4 applies unchanged, and on a consumed defer or collapse the handler writes that id into the
   requesting task's id cell before completing it. `PlayMusicWithFade` reads the cell again after the stop (`m17 =
   p-4` at `0x0db88`), so the task loads and plays the id the script already had, its play parks on O, O keeps playing,
   and O's natural end replays the same id from 0: the vanilla loop. While a sector change is pending the docking
   request collapses to the pending id, which stays pending. Undocking requests the sector id, which equals `m17`, so
   `PlayMusicWithFade` returns at its `m17 == id` gate (`0x0daaa`) and nothing stops. A docking request under an
   alert or over the combat track stays vanilla (the docking track plays); a docking track that got through that way
   (or plays after a load in a dock) is refused as the old track of a non-docking request (`dock_old`), so the next
   sector request switches at once instead of waiting for it.

**The KC prerequisite.** Without it a jump never reaches the T3 test. In memory, the 6 bytes `op6f; jt 0x161d7` at
CODE `0x161c3` of `150::WarpToSector` (Mayhem; stock `0x1639c` → `0x163b0`; the window is detected at run time by its
fork child's signature once the load gate opens for a new VM key, [story-builds.md](story-builds.md); the vanilla
bytes after the loader's little-endian
operand swap are `6f 33 d7 61 01 00`) become `jmp 0x161d7` plus one `push0` filler that is never reached
(`32 d7 61 01 00 01`). The fork's result cell was consumed by the `jt`, so the stack depth at the continuation is
unchanged; instruction starts outside the window and every method entry are unchanged; no jump lands inside the window
(`verify_music_queue_kc.py`). The patch is maintained from Present on the main thread, where no task runs: the VM
pointer `*0x006085e4` is read every frame, its CODE buffer `[VM+8]`, size `[VM+0x10]` and build id `[VM+0x143c]` and
the window every 8th frame or on a VM change (fault-free reads), the sites must be detected for that exact VM key
(else `action=no_sites` names the method and the reason), the window must hold
exactly the vanilla bytes (our patch: nothing; anything else, a threaded or fused window: refused with the bytes
seen), and the 6 bytes are written only after `VirtualQuery` reports the page committed and writable. Shutdown puts
the vanilla bytes back when the window still holds the patch. The three EXE witnesses of that layout (the jump handler
`0x004a317a` adding the operand to `[VM+8]`, the loader's stores at `0x0049d332`/`0x0049d34c`/`0x0049d2a7`) are
compared at install. The patch is maintained only while the queue is armed: with the queue off or refused it is never
applied, and a gate jump fades the old track at the gate as vanilla.

**When the KC patch is written (the load gate).** Never while a story load may be in progress; once per completed
load. The story object is reloaded on every save load (`0x004a0880` → `0x0049d030`, a new buffer). A load begins at
the stop-all of the session function (caller `0x0040387d`, before it calls the loader `0x00404cc0` at `0x004038ba`)
or of the loader itself (`0x00404cf2`, before its KC VM restore `0x004a0880`); the keep's T1 entry sees both and
closes the gate (`load_begins=`). It opens for:

* the first check after the `save_load_complete` marker (capture.cpp's Present path, the event `fov::loaded` and
  `music_alert::loaded` take; `markers=`), which the proxy emits once per process, after the first save load; or
* for every load without a marker (the initial story load at session start and every later save load), two
  consecutive checks 8 frames apart after the last load begin that read the same observation: VM pointer, CODE
  buffer, size, build and the six window bytes. A window still in the file's byte order or being rewritten differs
  between the two reads, or is not the vanilla memory form, and is not written.

A vanilla window while the gate is closed is `action=waiting` (no write, not a refusal). The VM clear and load sites
(`0x0049ea80`, `0x004a0880`) are not used: they belong to the chase restore's single `InjectConsumer`, which
`seta_restore` holds. So a reloaded object is patched 8 to 16 frames after its fields settle, or at the first check
after the marker. **[i]**: that both stop-alls precede the story reload is read from the call order of `0x00403840`
and `0x00404cc0` (sector-transit-order.md section 3, shadow-caster-lifetime.md section 5a), not traced.

**Interactions.** The combat resume (music-script-driver.md section 4) never remembers a stop the queue deferred or a
stop of the parked id: O plays to its end, N never played. The keep's holds are unchanged. The save keeps the queue
(item 6; the save pumps O instead of the keep's replayed record), a pause hands a same-id episode back to the keep; a
pause during a pending change, an alt-tab under `paused`, a load, P_Leave and session start still end it before the
walk (C woken, P freed), so after those the new track starts from 0. In the 1.5.1 flight 6 of 9 `stop_all` ends were saves and 3 were synchronous ends followed by an alt-tab in the same
frame, the pattern of the pause stop-all that precedes an alt-tab there (`queue_save_gaps.py`; the row now names the
caller). The trace stubs stay chained on the same sites and run
in front of the queue's (the trace installs after the keep, and the newest claim is the chain head), so a stop or play
the queue consumes is still logged by its trace row, followed by the queue row.

## Install and fail-closed

Optional inside the keep (like Patches D and E): with `X3M_MUSIC_KEEP=1` and `X3M_MUSIC_QUEUE=1` the keep compares the
delete-by-id body (51 B), the `MOV_StopMovie` completion delivery (42 B, the slot-table layout), the three KC layout
witnesses and the play entry's head through `core::window_matches` (seeing through a live trace claim), then acquires
the play entry `0x00498c90` through the shared-claim protocol (`core::acquire_shared`, chained in front of or behind the
trace, released by rollback and shutdown for whichever feature acquired it). A refusal logs `music_queue … armed=0
reason=bytes_mismatch|chain_failed|…` and the keep installs without the queue. Without the keep the option is a
no-op (`reason=keep_off`). The save keep and `music_dock_keep` add no site and no byte window: the docking keep is
read only with the queue requested and acts only while it is armed (`dock_keep=` on the install row), the save keep
only with the save pump bound. The stubs are integer-only (EFLAGS, EAX/ECX/EDX saved first and restored before each exit,
ESP as at entry; the consume exits are a plain `ret`); the handlers run under `LightCallBoundary`, the completion and
free shims save EBX/ESI/EDI/EBP, and all of them are roots of `check_no_x87.py`.

## Rows

* `music_queue requested= armed= reason= site_play=0x00498c90 write_play= stub_play= free=0x004986b0 vm=0x006085e4
  detect=runtime expected=mayhem:1462141839:0x161c3:0x161d7,stock_ap:1510658513:0x1639c:0x163b0 kc_patch=pending|off
  key_base=0x7f000000 stack_depth=19 cells=-18,-13,-11,-10,-9,-7` at install.
* `music_queue_kc frame= action=applied|already|waiting|no_vm|unreadable|no_sites|out_of_range|bytes_mismatch|
  unwritable vm= code= size= build= object= detect=pending|none|<reason> method=<method>|- window=<detected> bytes=<6
  bytes seen> applied= refused= load_begins= markers=` once per change of result, per apply (after every completed
  story reload) and for each new build that has no sites (x3config at frame 0, x3intro at the menu).
* `music_queue frame= seq= qpc= event=defer|park|collapse|release|end reason= state= old= new= key= record= task=
  stop_task= wake= clear= complete= park= freed= free_later= frames= episode= caller= dock=` per transition (`frames`
  since the defer; `caller` names the stop-all on `end reason=stop_all` and `keep`, `-` otherwise; `dock` is the
  requested docking id a defer or collapse replaced, 0 otherwise); `event=keep caller=save` at a save that keeps the
  queue (`wake=1` while PARKED), followed in the walk by `music_keep_stop … name=save … mode=skip_all held=0 … pump=1
  queue=1` (`mode=vanilla pump=0` when the pump was taken) and at its gzclose by `music_keep_save_pump … end=gzclose
  run=queue`; `event=end reason=handback caller=pause freed=<1: the task's record freed, 0: none, 2: fallback, P
  freed>` at a pause of a same-id episode, followed by the keep's `music_keep_stop name=pause id=<the id> … mode=paused
  held=1`; `music_queue frame= seq= qpc= event=free_later key= freed=` at the Present after an alt-tab end under
  `paused`; `event=refuse
  reason=not_armed|no_task|cells|immediate_id|alert|combat_old|not_playing|same_id|dock_old … ip= k= capacity= cells= c-18= c-13=
  c-11= c-10= c-9= c-7=<tag:value> alert=<ok>/<old>/<new> record= flags= ctx=` per non-deferred `PlayMusicWithFade` stop.
  End reasons: `natural_end` (release), `lost`, `ended_before_park`, `stop`, `other_play`, `stop_all`, `alert_edge`,
  `stale`, `claimed`, `handback`. The `music_queue` rows have a cap of their own, 200 per session (`queue_line_cap`), then one
  `music_queue_cap … note=further_queue_lines_suppressed`, separate from the 1,000-line cap of the trace and keep
  rows, so a trace-heavy flight cannot suppress them.
* `X3M_MUSIC_QUEUE` other than exactly `1` (for example `0`) logs `requested=0`. The install row ends with
  `dock_keep=1` when the docking keep is armed.

## What the first flight must show

With `music_keep = 1`, `music_queue = 1` and `music_trace = 1`:

1. The install row `music_queue … armed=1 reason=ok write_play=atomic`, then `music_queue_kc action=applied build=1462141839 detect=none
   bytes=6f33d7610100` once the story object has settled (possibly after one `action=waiting` row) and again after every
   save load (`applied=` counts them, `load_begins=` the reloads seen); no
   `bytes_mismatch` row (one would mean the loader's peephole pass rewrote the window: the queue then never defers on a
   jump and the flight is a cell-value flight only).
2. One gate jump: `music_queue event=defer old=<sector id> new=<new id> key=0x7f00000n`, then `event=park`; the old
   track keeps playing audibly through the warp and after arrival. The park's consumed play is logged first by the
   trace, which runs in front of the queue stub: one `music_trace_play id=<new>` directly before `event=park`, and no
   other `music_trace_play` of the new id until the release. At the old track's natural end the replay logs
   `music_trace_play id=<new> start_ms=0`, then `event=release reason=natural_end freed=1`, within about two frames of
   the end, and the new track plays.
3. One jump while a change is pending: `event=collapse old=<pending> new=<newest>`, `event=park task=<D>`, and the
   release plays the newest id.
4. One combat start while pending: `event=end reason=stop wake=1 freed=1` and the combat track plays as vanilla
   (`music_trace_play id=10xx`); after the alert the sector track of the current sector from 0.
5. One save while pending (changed 2026-10-25): `event=keep caller=save` (`wake=1` if parked), `music_keep_stop
   name=save … mode=skip_all … pump=1 queue=1`, `music_keep_save_pump … ret1=<n> ret0=0 ret2=0 pump_end=gzclose
   end=gzclose run=queue`; the old track audible through and after the save; then `event=park` of the same N by the
   same task right after the save (the consumed play logged by the trace just before it, as at the first park, and
   no `music_keep_seek` of the new id), and at the old track's end
   `event=release reason=natural_end`. No `end reason=stop_all caller=save`. A save whose pump returned 2 (`ret2=1`):
   `event=end reason=ended_before_park` at the task's replay after the save and the new track from 0. One pause (Pause
   key) while docked under `music_dock_keep`: `event=end reason=handback caller=pause freed=1`, `music_keep_stop
   name=pause id=<sector id> … mode=paused held=1`, after the unpause `music_keep_seek id=<sector id> action=skip` and
   the track continuing at its position. Only the Pause key's script handler is shown to call `X2_SetPause`
   ([pause-dialog-input.md](../reverse-engineering/pause-dialog-input.md)); whether the Esc menu reaches it is not
   established, so an Esc-menu pause may show no `caller=pause` row at all. One pause while a sector change is pending: `event=end reason=stop_all
   caller=pause wake=1 freed=1` and the new track from 0 after the unpause (unchanged). Loading that save afterwards plays the new track from 0 and logs no queue
   row for the private key.
6. A track left to its end with no change pending: a same-id `music_trace_play` with no queue row (vanilla loop).
6a. One dock and undock with no change pending (`music_dock_keep = 1`): `event=defer old=<S> new=<S> dock=14` and
   `event=park` at the docking clearance, `event=collapse old=<S> new=<S> dock=200x` and `event=park` once docked,
   no landing or docking track audible, the sector track continuous; at undock no queue row and no
   `music_trace_play`; at S's natural end `event=release` and `music_trace_play id=<S> start_ms=0`. A dock while a
   sector change is pending: `event=collapse old=<N> new=<N> dock=…`, and N plays at O's end. A dock during combat
   (alert on): `event=refuse reason=alert new=14` and the vanilla landing track; the first sector request after it:
   `event=refuse reason=dock_old` and the sector track at once.
7. Any `event=refuse reason=cells` row on a `0x0db54` stop with alert 0/0 is the open item of section 8.4: the row
   carries the six cells for the correction.
