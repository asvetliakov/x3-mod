# Sector-music track queue

`src/proxy/music_keep.cpp`, `music_keep.h`, `music_keep_core.h` ("The track queue"). Option `music_queue` (`[audio]`,
default 1, `requires music_keep`), launcher `--music-queue` / `--no-music-queue` (`X3M_MUSIC_QUEUE=1`, sent only with
`--music-keep`, refused under `--vanilla`). Engine and script facts:
[music-script-driver.md](../reverse-engineering/music-script-driver.md) sections 1, 3, 7, 8 (the policy is section 8.3
as amended by 8.4) and [music-restart.md](../reverse-engineering/music-restart.md) sections 1 and 6. Ledger:
[verification/music-queue.md](../verification/music-queue.md). Unflown as of 2026-10-23.

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
   `ChangeTrackMusic` return `(3, 0x0dcdb)` at -7. N must be greater than 0 as a signed id (0 and every negative id
   play at once, section 8.3) and not 13 (death), 14 (landing), 1001..1042 (combat) or 8233..8237 (boarding); the alert record `*0x00608504` must read `[R+4] == 0 && [R+8] == 0` (a combat start stops
   inside `NotifyAlert` with `[R+8] != 0`, a combat end with `[R+4] != 0`); the stopped id must not be a combat id; the
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
   under `skip_all` (save, pause, load, P_Leave, session start, unknown; before the walk, so neither O nor P is
   visited), and a nonzero committed alert level `[R+4]` sampled at Present while the queue is active (a combat start
   the script dropped while C was in `TI_Delay`). An orphan that ended with nobody left to wake (flag 2 clear, context
   0) or that is no longer linked under P is freed or forgotten at the next event (`stale`, `lost`). Alt-tab depends on
   the keep's alt-tab mode in effect: under `skip_all` (Patch D live) it leaves the queue alone, as `skip_all` leaves O
   untouched; under `paused` (`status_gate` refused) it ends the queue like every other caller (C woken, O's context
   zeroed), but P is freed at the next Present (`event=free_later`), because alt-tab's T1 can run inside a COM call of
   the manager's walk (media-record-lifetime.md, boundary 1) and the walk then pauses the orphan as vanilla would.

**The KC prerequisite.** Without it a jump never reaches the T3 test. In memory, the 6 bytes `op6f; jt 0x161d7` at
CODE `0x161c3` of `150::WarpToSector` (Mayhem story build 1462141839; the vanilla bytes after the loader's little-endian
operand swap are `6f 33 d7 61 01 00`) become `jmp 0x161d7` plus one `push0` filler that is never reached
(`32 d7 61 01 00 01`). The fork's result cell was consumed by the `jt`, so the stack depth at the continuation is
unchanged; instruction starts outside the window and every method entry are unchanged; no jump lands inside the window
(`verify_music_queue_kc.py`). The patch is maintained from Present on the main thread, where no task runs: the VM
pointer `*0x006085e4` is read every frame, its CODE buffer `[VM+8]`, size `[VM+0x10]` and build id `[VM+0x143c]` and
the window every 8th frame or on a VM change (fault-free reads), the build must be 1462141839, the window must hold
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
stop of the parked id: O plays to its end, N never played. The keep's holds and the save pump are unchanged; the save's
stop-all ends the queue before the walk (C woken, P freed), so after a save the new track starts from 0 and the old one
is cut at the save: the documented degradation of section 8.3. The trace stubs stay chained on the same sites and run
in front of the queue's (the trace installs after the keep, and the newest claim is the chain head), so a stop or play
the queue consumes is still logged by its trace row, followed by the queue row.

## Install and fail-closed

Optional inside the keep (like Patches D and E): with `X3M_MUSIC_KEEP=1` and `X3M_MUSIC_QUEUE=1` the keep compares the
delete-by-id body (51 B), the `MOV_StopMovie` completion delivery (42 B, the slot-table layout), the three KC layout
witnesses and the play entry's head through `core::window_matches` (seeing through a live trace claim), then acquires
the play entry `0x00498c90` through the shared-claim protocol (`core::acquire_shared`, chained in front of or behind the
trace, released by rollback and shutdown for whichever feature acquired it). A refusal logs `music_queue … armed=0
reason=bytes_mismatch|chain_failed|…` and the keep installs without the queue. Without the keep the option is a
no-op (`reason=keep_off`). The stubs are integer-only (EFLAGS, EAX/ECX/EDX saved first and restored before each exit,
ESP as at entry; the consume exits are a plain `ret`); the handlers run under `LightCallBoundary`, the completion and
free shims save EBX/ESI/EDI/EBP, and all of them are roots of `check_no_x87.py`.

## Rows

* `music_queue requested= armed= reason= site_play=0x00498c90 write_play= stub_play= free=0x004986b0 vm=0x006085e4
  story_build=1462141839 kc_window=0x161c3 kc_continue=0x161d7 kc_patch=pending|off key_base=0x7f000000 stack_depth=19
  cells=-18,-13,-11,-10,-9,-7` at install.
* `music_queue_kc frame= action=applied|already|waiting|no_vm|unreadable|build_mismatch|out_of_range|bytes_mismatch|
  unwritable vm= code= size= build= window=0x161c3 bytes=<6 bytes seen> applied= refused= load_begins= markers=` once
  per change of result and per apply (after every completed story reload).
* `music_queue frame= seq= qpc= event=defer|park|collapse|release|end reason= state= old= new= key= record= task=
  stop_task= wake= clear= complete= park= freed= free_later= frames= episode=` per transition (`frames` since the
  defer); `music_queue frame= seq= qpc= event=free_later key= freed=` at the Present after an alt-tab end under
  `paused`; `event=refuse
  reason=not_armed|no_task|cells|immediate_id|alert|combat_old|not_playing|same_id … ip= k= capacity= cells= c-18= c-13=
  c-11= c-10= c-9= c-7=<tag:value> alert=<ok>/<old>/<new> record= flags= ctx=` per non-deferred `PlayMusicWithFade` stop.
  End reasons: `natural_end` (release), `lost`, `ended_before_park`, `stop`, `other_play`, `stop_all`, `alert_edge`,
  `stale`, `claimed`. The `music_queue` rows have a cap of their own, 200 per session (`queue_line_cap`), then one
  `music_queue_cap … note=further_queue_lines_suppressed`, separate from the 1,000-line cap of the trace and keep
  rows, so a trace-heavy flight cannot suppress them.
* `X3M_MUSIC_QUEUE` other than exactly `1` (for example `0`) logs `requested=0`.

## What the first flight must show

With `music_keep = 1`, `music_queue = 1` and `music_trace = 1`:

1. The install row `music_queue … armed=1 reason=ok write_play=atomic`, then `music_queue_kc action=applied build=1462141839
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
5. One save while pending: `event=end reason=stop_all wake=1 freed=1`, then the new track from 0 after the save.
6. A track left to its end with no change pending: a same-id `music_trace_play` with no queue row (vanilla loop).
7. Any `event=refuse reason=cells` row on a `0x0db54` stop with alert 0/0 is the open item of section 8.4: the row
   carries the six cells for the correction.
