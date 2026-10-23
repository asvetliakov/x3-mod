# Sector and combat music: the KC driver, the engine alert, and where a proxy can steer them

2026-10-23. Static study of `X3AP.exe` (SHA-256 `fdbf3418d8f0a897…`, the native install `C:\X3`) and of the story
object `L/x3story.obj` that this install loads: Mayhem's copy in `addon/07.cat` (decoded SHA-256 `2c3101a8…`, build id
1462141839; no loose `L\x3story.obj` exists), with stock AP (`addon/04.cat`, build 1510658513) compared. Nothing was
launched; no Wine. Builds on [music-restart.md](music-restart.md) §1 and §6 (record layout, flags, stop-all, the keep's
Patches A/C/D and the shared T3 site), [gate-queue-script.md](gate-queue-script.md) §1 (KC encoding, file offset =
CODE + 20) and [script-task-scheduler.md](script-task-scheduler.md) §2–3 (task layout, native ABI). CODE offsets are
the Mayhem object's; stock entries are given where they differ.

| Script (tracked) | Output | What it establishes |
| --- | --- | --- |
| [`verify_music_script_driver.py`](../../verification/results/music-script-driver/verify_music_script_driver.py) | `verify_output.json` | `PASS`: 46 EXE byte patterns, 2 strings, 3 KC opcode handlers, direct-caller census, no raw branch into the interior of `0x0042c974`; 24 decoded KC windows, 13 method entries, the combat id list, the caller census of the music entry points, the MOV natives the music methods use, stock opcode-sequence comparison |
| [`music_driver_listing.py`](../../verification/results/music-script-driver/music_driver_listing.py) | `raw/` (git-ignored) | decoded listings of every method named below, all `MOV_*` call sites, all music-entry callers, both builds |
| [`flight_music_summary.py`](../../verification/results/music-script-driver/flight_music_summary.py) | `flight_music_summary.txt` | the latest flight's music lines (log SHA-256 `fdc7c8d7…`): 8 plays, 6 ids, no combat id, no natural end, `--music-trace` off |

**Answer.**

* **Who plays music.** One KC singleton, class 200, drives all music through `200::PlayMusicWithFade(id, fade, loop)`
  (`0x0da87`). Members: `m17` = current track (> 0 a track, 0 none, −1 "stopping", −2 "old task finished"),
  `m18` = "end after this track", `m0` = busy-drop counter. Every track change is a **new forked task** that stops the
  old record by `MOV_StopMovie`, waits for the old task to free it, loads the new id and loops `MOV_PlayMovie` on it.
  A natural end replays the **same id from 0 at once**. **[m]**
* **The sector track** comes from the sector object (2001 member `m32`, the map's music id), else a hash of the sector
  coordinates. **The combat track** is a random pick from 1001..1042 (Mayhem; stock has 12 ids), kept if one is already
  playing. **[m]**
* **The combat trigger is engine code, not script.** `0x0042c890` runs once per frame. It sets alert level 1 when any
  object in the player's sector that is **hostile to the player ship by race relations** is within **2,500,000 units**
  on the larger axis. While the alert is on, the range is 5,000,000. Level 2 is a hostile missile targeting the player
  ship. On a 0 → nonzero edge it calls `150::NotifyAlert(1)`, which starts combat music; on → 0 it calls
  `NotifyAlert(0)`, which plays the sector track **from 0**. There is no timer, only the doubled range. **[m]** code,
  **[i]** 500 units/m, so 5 km to enter and 10 km to stay.
* **Proxy levers.**
  * (a) Deferring a sector switch: the old record must keep playing through a vanilla stop, free, load and play.
    Parking the stopping task is unsafe, because `m17 = −1` makes the script drop every other music request (§3).
  * (b) Resume after combat: the sector record is **freed** during combat. The proxy can remember `(id, position)` at
    the T3 stop and seek the post-combat replay at Patch C. The script cannot see the difference (§4).
  * (c) Narrowing the trigger: one engine site, `0x0042c974`, filters the per-frame level. A KC patch can only remove
    combat music or poll (§5).

## 1. The KC music driver (Q1) **[m]** unless marked

### Call sites of the MOV natives in the music methods

The only `MOV_*` natives the music methods (`0x0d9b8..0x0de95`) call are SetVolume, Stop, Load, Play and Free.
They query no position (census in `verify_output.json`).

| Method (Mayhem entry; stock) | Site | Native / effect |
| --- | --- | --- |
| `200::PlayMusicWithFade(id, fade, loop)` `0x0da87`; `0x0d1a6` | `0x0db4f` (task IP after it `0x0db54`) | `MOV_StopMovie(old)`, issued by the **new** task, when `m17 > 0` and not (`id == 0 && fade > 0`) |
| | `0x0db55..0x0db84` | wait while `m17 == −1`: `TI_Delay(100)` up to 9 times, then **give up and return** with `m17` still −1 |
| | `0x0dba3`/`0x0dbaa` | `MOV_LoadMovie(m17, 144)` (flags `0x90`; the flight shows `0xd0`, i.e. the engine added `0x40`, the DirectSound path) |
| | `0x0dbe2` | `MOV_SetVolumeMovie(m17, GetMODVolume())` (or fork `MusicFadeIn` when `fade`) |
| | `0x0dbf2` (resume IP `0x0dbf7`) | `MOV_PlayMovie(m17)`: blocks until the record's completion |
| | `0x0dc02..0x0dc32` | **replay** (`jmp 0x0dbe8`) while `m17 == own id && !m18 && loop`. A zero result clears `loop`. |
| | `0x0dc3b` (IP `0x0dc40`) | else `MOV_FreeMovie(own id)`, `m18 = 0`, `m17 = −2` if it was −1 else 0, return |
| `200::MusicFadeOut(id, ms)` `0x0d9b8` | `0x0da12` | volume ramp with `TI_Delay(100)`, then `MOV_StopMovie(id)` (forked from `0x0db23` when `id == 0 && fade > 0`) |

Entry gates of `PlayMusicWithFade`, which matter for every proxy policy:

* `GetMODVolume() <= 5` turns the request into id 0, i.e. stop (`0x0da96`).
* `m17 == id` returns at once (`0x0daaa`): **re-requesting the playing id is a no-op, with no restart.**
* `m17 < 0` **drops the request** (`0x0dab4..0x0dae0`): `m0 −= 1`, and when `m0` reaches 0 it forces `m17 = 0`,
  `m0 = 5`. An accepted request resets `m0 = 5`.

Wrappers: `LoopMusic(id)` = `PlayMusicWithFade(id, 0, 1)` (`0x0dc76`). `StartMusic(id, fade)` = `(id, fade, 0)`.
`StopMusic(fade)` = `(0, fade, 0)`. `ChangeTrackMusic(id)` (`0x0dcc6`) **forks** `LoopMusic(id)`. KC opcode `6f` is a
fork (handler `0x004a35f6`): it copies the current frame into a new task and links that task at the **tail** of the
runnable list. It **runs the child at once** in a nested interpreter (`0x004a3760`) until it suspends, with result 0 in
the child and the child's task id in the parent. Opcode `70` ends a task (`0x004a4301`). This settles the fork reading
that [ship-navigation-ai.md](ship-navigation-ai.md) §10 left inferred.

### How the ids are chosen

* **Sector:** `ChangeTrackToSectorMusic()` (`0x0dcdf`) returns if `150::GetClientState() & 256`.
  1. It sets `env = g10 ? g10.GetEnvironment() : 0` (g10 = the player ship).
  2. It tries `t = 3001::GetEnvironmentTrack(env, 0)`. That method walks table `m5` and calls each entry's
     `GetEnvironmentTrack`; the only definition is `3100`'s, which returns 0.
  3. If `t` is 0, it takes `t = 200::EnvironmentToMusicTrack(env)` (`0x0deeb`):
     * 13 if `150::GetKilled()`;
     * for a sector (class 2001), `env.GetMusicTrack()` = member `m32` if > 0, else
       `((x·103 + y·37 + 5) mod 22) + 19`;
     * for any other environment (a dock), `2001 + SE_Random(4)`.
  4. It calls `ChangeTrackMusic(t)`.

  `m32` is written only by `2001::SetMusicTrack`, from `300::BuildObjects`, `300::BuildFromEditor`, `202::Patch6` and
  `308::DoAlterSector`. So a sector's id is fixed for the session. The flown ids 8404, 80412, 80202, 81307, 80300 and
  81616 all exist as `soundtrack\%05d.mp3`.
* **Combat:** `ChangeTrackToCombatMusic()` (`0x0dd45`) has the same client-state gate. It tries
  `GetEnvironmentTrack(g9.GetSector(), 1)` (0 as above). Otherwise it builds the list 1001..1042 (`mkarr 42` at
  `0x0ddf9`), keeps `m17` if it is in the list (`SE_ArrayFind`), else takes `list[SE_Random(42)]`, and calls
  `ChangeTrackMusic(t)`. Stock: 12 ids, 1001..8227.
* **Other MOV users** play no music: speech in `200::Speak*`, cut scenes in class 621 (`PlayThread` re-picks combat or
  sector music by `IsAlert` when a scene ends, `0x101e9`), credits, and `2016::StartPlayerShip`'s
  `MOV_PlayMovieFrom` (`0xb2607`). That last call has a negative id, which takes the handler's negative-id branch at
  `0x0049991c`.

### Who requests music (caller census)

| Entry | Callers | Notable |
| --- | --- | --- |
| `ChangeTrackToCombatMusic` | 18 | `150::NotifyAlert` `0x176c1`; `402::EnterSector`/`WarpEnterSector` `0xfb4c4`/`0xfb55b`; `2016::StartPlayerShip`; `621::PlayThread`; 2073/2064 boarding; MD `308::DoPlayMusic`/`DoPlayMovie` |
| `ChangeTrackToSectorMusic` | 23 | the same, plus `2016::RunPlayerTrade` `0xb2173` (docking), `2004::CanLand`, `308::DoAlterSector` |
| `ChangeTrackMusic` | 14 | `150::BeforeLeave` (id 0), `150::DockingAllowed`, `622::GameOver`/`Kill`, `624::StartAudio`, MD `DoPlayMusic` |
| `WaitForEndOfTrack`, `PlayMusicToEnd`, `ChangeTrackMusicDelayed` | 0 | unused in this object, and no by-name reference in the EXE |
| `150::NotifyAlert` | 0 KC | called by the engine only (§2) |

The loop shape: **same id, from 0, immediately**. The completion re-runs `MOV_PlayMovie(m17)` on the record it already
has, so `0x00498c90` finds it and the Patch C site seeks to 0. There is no delay and no next track. The track only
changes when another request writes `m17`, or when `WaitForEndOfTrack(id)` sets `m18` (nobody calls it).

## 2. The combat condition (Q2) **[m]** unless marked

### Engine: alert level `0x0042c890`

`0x0042c890` is stdcall, and its one argument is the cockpit registry `R = *0x00608504`. Its only caller is
`0x0041cdea` in the registry walk `0x0041cde0`, which the main loop calls once at `0x00403f2f`. That is after the
scheduler passes and just before the render at `0x00403f34`.

1. `P = [*0x0060850c+0x38]`, the player ship. Its parent `[P+0x54]` must be a sector (class word 1), otherwise the
   level stays 0. **Docked means no alert.**
2. For each of the sector's 32 per-class child lists (`[sector+0x50]`, 12 B each) and every object `O` in them:
   * skip `O` unless `0x00450890(O, P)`, "hostile", is true;
   * skip ships (class 7) of TShips subtypes `0x10f`, `0x13f`, `0x140`, `0x141` (`0x0042c8e1`);
   * a missile (class 10) whose target `[[O+0x50]+0x10] == P` sets **level 2** (`0x0042c907`, `0x0042c94a`);
   * otherwise **level 1** when the Chebyshev distance `max(|dx|,|dy|,|dz|)` (`0x00419280`, positions at
     `[obj+0x70]+0x30`) is below **2,500,000** if the committed level `[R+4]` is 0, or below **5,000,000** if it is
     already nonzero (`0x0042c915..0x0042c93b`).
3. The result is merged as a maximum into `[R+8]` (`0x0042c965`). The missile homing update also raises `[R+8]` to 2
   for any missile whose target has bit `2` in `[+0x40]`, the player-ship flag **[i]** (`0x00453405`, `0x00459451`).
4. At `0x0042c974`, `new = [R+8]` and `old = [R+4]`. On a change:
   * 0 → 1 or 2: `NotifyAlert(1)`;
   * 1 ↔ 2: only swap the alert sound (ids `0x39b` and `0x39d`, through `0x0049b050` and `0x0049a4d0` **[i]** roles);
   * → 0: `NotifyAlert(0)`.

   The calls go through the synchronous by-name call `0x0049f4c0(…, 0x96 = class 150, "NotifyAlert", 1, 1, flag)` at
   `0x0042c9ac`, `0x0042c9f6` and `0x0042ca42`. Afterwards `[R+4] = new` and `[R+8] = 0` (`0x0042ca4a`). So the edge
   is evaluated every frame. The only hysteresis is the doubled range. There is no timer and no damage term.

**Hostile** (`0x00450890`, with `0x00450810`). In the default branch it is P's stance towards O's owner group
(`[O+0x3c]`):

* −1 when P's foe mask `[[P+0x50]+0x1d0]` has that bit. The friend mask `+0x1cc` wins, and `[P+0x44] & 0x40000000`
  makes P neutral to everything.
* A missile flagged `0x1000000` is hostile unless P launched it or shares its group.
* When `[[P+0x50]+0x1d4] != 0` (meaning not established), O's stance towards P's group counts as well.

This is the race-relation test, not "is attacking": a hostile station, or an idle hostile ship that comes within
5,000,000 units of the player on any axis, starts the alert **[m]** code, **[i]** effect. Units: position units;
500 units/m **[i]** ([gate-queue-script.md](gate-queue-script.md) §9).

### KC: `150::NotifyAlert(on)` (`0x1769a`) and `150::IsAlert()` (`0x1773a`)

* `NotifyAlert` returns if `m9` is set (the player-killed flag, written by `SetKilled` and `StartKilled`).
* `on && !m10`: `m10 = 1`, `ChangeTrackToCombatMusic()`.
* `!on && m10`: `m10 = 0`, `ChangeTrackToSectorMusic()`, then a game tip.
* `IsAlert()` returns `m10`. Twenty-one callers re-pick combat or sector music from it, among them sector entry, ship
  launch, the end of a cut scene and boarding.

**Return to sector music.** `NotifyAlert(0)` calls `ChangeTrackToSectorMusic()`, then `ChangeTrackMusic(sector id)`,
whose fork runs `PlayMusicWithFade`:

1. `MOV_StopMovie(combat id)`;
2. wait;
3. `MOV_LoadMovie(sector id, 0x90)`, which creates a **new** record;
4. `MOV_PlayMovie`, which seeks to **0** at `0x00498d54`.

It always starts from the beginning.

## 3. A sector change, step by step, and deferring it (Q3)

**Sequence** **[m]** code; the task order is read from the link code, not traced:

1. `402::EnterSector` or `402::WarpEnterSector` forks a child (`6f` at `0xfb4a2` / `0xfb539`). The child runs at once
   to `TI_Delay(1000)` and suspends.
2. About one second later, in a scheduler pass, the child calls `IsAlert() ? ChangeTrackToCombatMusic() :
   ChangeTrackToSectorMusic()`, then `ChangeTrackMusic(new)`. That **forks task C**, which runs at once:
   `PlayMusicWithFade(new, 0, 1)` sets `L2 = old`, sets `m17 = −1` and calls **`MOV_StopMovie(old)` from task C**.
3. `0x00498810` handles the stop:
   * it finds the first record with that id, clears flag `2` and pauses it (`0x004d1810`);
   * it completes the **old task T_old** (record `+0x14`) with status 1, which relinks T_old at the runnable head
     (`0x004a4740`);
   * it clears `+0x14`/`+0x18`;
   * it completes **C itself** with 1 (`0x004988be`), so C continues synchronously.
4. C finds `m17 == −1`, calls `TI_Delay(100)` and suspends.
5. In the **same scheduler pass** the loop takes T_old from the head. Its `MOV_PlayMovie` returned 1 and `m17 != old`.
   It calls `MOV_FreeMovie(old)`: `0x004984d0` destroys the media object (`0x004d1d40`), unlinks the record and frees
   it. Then it sets `m17 = −2` and ends.
6. At least 100 ms of game time later, C sets `m17 = new` and `m18 = 0`, then `MOV_LoadMovie(new)` (a new record),
   SetVolume and `MOV_PlayMovie(new)`, which seeks to 0, runs and blocks.

So the old record is stopped by the new task and freed in the same frame. The new id starts at least one
`TI_Delay(100)` later, in a later frame. Nothing reaches stop-all, which is why the flight shows each new id with
`action=vanilla` and no stop line. The keep logs `MOV_StopMovie` only when it drops a hold.

### What an engine-side deferral needs

**Parking C on the old record is a valid VM state but drops other requests.**

* **The parking itself:** if the T3 handler (see the site below) neither pauses the old record nor delivers C's
  completion, C's `task+0x20` stays 0. The interpreter then suspends C at `0x004a3919` with IP `0x0db54`. That is the
  normal native protocol, and no VM field has to be written.
* **Waking C:** at the old record's natural end the manager fires `(T_old ctx, 1)` (`0x0049845b`), and T_old exits
  with `m17 = −2`. The proxy then has to complete C itself: slot = EDI at entry (`[VM+0x1454]`), context = C's task
  id (`[esp+4]` at entry), status 1, through the slot callback (`0x004a4910`). C then proceeds.
* **Why it fails:** for the whole deferral `m17 == −1`, and `PlayMusicWithFade` drops every request that arrives
  then (§1 gates). A combat start is the important case: its fork runs **synchronously inside `NotifyAlert`**, before
  any relinked task can run, so it is dropped even if the proxy ends the deferral at `0x0042c974`. Docking and
  another jump are dropped the same way. The fifth drop forces `m17 = 0`, and the sixth request then starts a track
  without stopping the old one. **[m]** code path; not usable as is.

**Orphaning the old graph keeps the script vanilla** **[i]**: designed from the code paths above, not built or flown.

1. **At the T3 stop** (PlayMusicWithFade discriminator below, music record, request classified as "sector"): run the
   engine body without the pause. Clear flag `2`, complete T_old and then C. The graph keeps playing while the script
   proceeds exactly as in vanilla.
2. **At `MOV_FreeMovie`** for that record (`call 0x004986f0` at `0x00499865`, its only caller; KC IP `0x0dc40`; ECX =
   id, ESI = slot, `[esp+4]` = task id): **do not destroy it.** Keep it linked with flag `2`, re-key `[rec+0x10]` to an
   id no script uses, and leave context 0. The DirectSound feed is the manager's `0x004d0700`, which only visits
   linked records with flag `2` ([music-restart.md](music-restart.md) §6, Alt-tab). An unlinked graph would loop its
   5 s ring.

   At its natural end the manager clears flag `2` and fires `(0, 1)`. The effect of context 0 is unmodelled, as in
   music-restart §6, Orphans. The proxy then frees the record with `0x004984d0` (ESI = record) outside the manager's
   list walk, for example at Present.
3. **New record:** on C's start-0 play while an orphan lives, skip the seek (Patch C) and the run. The run call
   `call 0x004d1870` at `0x00498d71` is a new five-byte site. Extend Patch D (today it answers only for skip_all holds)
   to answer "playing" for the gated record, so that the DirectSound re-arm in `0x004d0700` does not start it. When the orphan's flag `2` clears, seek it to 0 and run
   it (`0x004d0430`, `0x004d1870`, EAX = record), then drop the gate.
4. **Any other request** for the new id ends the orphan (pause and free) and drops the gate: a `MOV_StopMovie` from a
   combat start, a stop-all, or a load. Because `m17 = new` throughout, combat, docking and further jumps are accepted
   normally.

**Hook-site facts.**

* **`0x00498810`, the shared T3 site, already claimed by the keep and the trace.** From `MOV_StopMovie` the entry
  state is: return address `0x00499886`, EAX = id, ESI = calling task (`0x004997c8`), EDI = completion slot
  `[VM+0x1454]` (`0x0049987c`), `[esp+4]` = calling task id. The other two callers (`0x0045c27d`, `0x004f66be`) carry
  no task in ESI, so dispatch on the return address first.
* **KC discriminator:** `[ESI+0x1c]`, written at `0x004a38e1` before the dispatch, is the CODE offset after the native.
  `0x0db54` is the stop in `PlayMusicWithFade`, `0x0da17` the one in `MusicFadeOut`, `0x10228` the one in
  `PlayThread`.
* **New id:** the requested new id (arg0 of the stopping frame) is the cell at index `[ESI+0x18] + 8` (base
  `[ESI+0x14]`, 5-byte cells, the stack grows down). Cells `+5`/`+6` must carry tags 10 and 3 **[i]** (layout from the
  interpreter, not executed).
* **Flags and registers:** flags are dead at the entry; the existing stubs restore EFLAGS/EAX/ECX/EDX and leave
  ESI/EDI/EBP alone.
* **Reentrancy:** the completions inside only relink tasks (`0x004a4740`) and run no bytecode. The site can be entered
  from a nested interpreter inside `0x0042c890`, i.e. in the cockpit phase of the frame, not only from scheduler
  passes.
* **Patch C (`0x00498d54`) and Patch D (`0x004983d9`)** are as documented in [music-restart.md](music-restart.md) §6.

**Script-side fact.** `200::WaitForEndOfTrack(id)` (`0x0de68`) is already the "finish this track, then stop looping"
primitive. It sets `m18 = 1` when `m17 == id` and polls `TI_Delay(210)` until the track's task clears `m18` at its
exit. It has no caller. Calling it from the `EnterSector` fork child before `ChangeTrackToSectorMusic`, with an
`IsAlert` re-check after the wait, would defer in bytecode.

The dead `PlayMusicToEnd` (27 B) and `ChangeTrackMusicDelayed` (35 B) are in-place space, but a combat start during
the wait resets `m18`, and a dock in the meantime re-rolls the random dock track **[m]** code, **[i]** effect. Not
assembled.

## 4. Resuming the sector track after combat (Q4)

**The sector record does not survive** **[m]** code, **[i]** frame attribution:

1. `NotifyAlert(1)` runs synchronously inside `0x0042c890`, in the cockpit phase of frame N. Its fork stops the
   sector record: pause, flag `2` cleared.
2. The sector task runs at the next scheduler pass, in frame N+1, and **frees the record** (`MOV_FreeMovie` at
   `0x0dc3b`).
3. After combat the same id comes back through a fresh `LoadMovie` and a `PlayMovie` from 0. The id is the same unless
   the player changed sector or docked: `m32` or the coordinate hash, with the environment hook returning 0.

**The proxy can remember and seek without the script noticing.**

* **Remember** at the T3 entry (`0x00498810`, return `0x00499886`, `[ESI+0x1c] == 0x0db54`, music record) when the
  stop starts combat. The discriminator is either the frame's new id in 1001..1042, or `[R+8] != 0` while `[R+4] == 0`
  (the commit at `0x0042ca4a` runs after `NotifyAlert`) **[m]** order.
* **Position:** read it before the engine pauses the record. Either call `IMediaPosition::get_CurrentPosition` on
  `[media+0x70]` (vtbl `+0x24`, REFTIME seconds; the engine's own reader `0x004d0600` does exactly this), or keep the
  proxy's own QPC clock from that id's last Patch C start. For a DirectSound-path record (`0x40`, every flown music
  record) the graph position is the decode position, ahead of the audible one by the buffered part of the 5 s ring
  **[i]**.
* **Seek:** at Patch C (`0x00498d54`, EAX = record, start ms on the stack from `push edi`), on the first start-0
  `MOV_PlayMovie` of the remembered id after combat, call `0x004d0430(record, pos)` instead of `(record, 0)`. That is
  the path `MOV_PlayMovieFrom` takes (`0x00499982` → `0x00498c90`), so a nonzero start is native engine behaviour.
  `[rec+0x1c]` keeps 0: it is written from EDI at `0x00498d83` and read only by the loop re-seek of loop-flagged
  records, and `MOV_PlayMovie` passes `a10 = 0` (`0x0049980a`).
* **Invisible to the script:** it blocks in `MOV_PlayMovie` until the record's completion and reads no position (§1
  census). It only sees the track end sooner, and then replays it from 0.
* **Invalidate** the memory on use, on any other music id, on a stop-all `load`/`session_start`, and when combat ends
  in another sector.

## 5. Narrowing the trigger (Q5)

**No native carries the predicate.** The decision is engine code (§2). The KC receives only `NotifyAlert(0/1)` through
`0x0049f4c0`, at return IPs `0x0042c9b1`, `0x0042c9fb` and `0x0042ca47`. `IsAlert` is a KC getter (`m10`), not a
native, so no native result can be hooked from `NotifyAlert`.

**Bytecode** (file offset = CODE + 20), byte counts only:

* **"Never combat music":** keep `frame 44` in `ChangeTrackToCombatMusic`. Replace `0x0dd48..0x0dd5b` (20 B, the
  client-state test) with `push0; pushw 200; callcls 200::ChangeTrackToSectorMusic; drop; push0; ret` (12 B) plus
  8 × `01`. All 18 callers then re-request the sector id, which is a no-op through the `m17 == id` gate. `IsAlert`
  and the alert sound are unchanged.
* **A KC "attacked" gate** cannot work at the edge alone. `NotifyAlert(1)` fires once per episode (0 → ≥ 1), so a fight
  that starts after the enemy came within range would never get combat music. It needs a poll: a fork with a
  `TI_Delay` loop while `IsAlert()`, inside the 256-byte `ChangeTrackToCombatMusic` body (`0x0dd45..0x0de44`).
  The 42-entry literal list there takes about 129 B and can become `1001 + SE_Random(42)`.

  KC data for such a gate:
  * `g10.IsAttacked()` (`2004` `0xdfc5d`): attacker `m49` set less than 60 s of age ago, through `SetAttacker` from
    `2004::AttackedBy`; the name is in STRG.
  * `GetAttacker()` (600 s), `SA_IsEnemy`, `SA_FindNearestEnemy`.
  * That the player ship's KC class inherits `2004::AttackedBy` → `SetAttacker` **[i]**.

**Engine, one site: `0x0042c974`.**

* **Instructions:** `mov ecx,[esi+8]; mov eax,[esi+4]`, six bytes, two whole instructions. The next instruction is
  `cmp ecx,eax` at `0x0042c97a`. The only branch to the site is the `jle` at `0x0042c96f`, which lands on its first
  byte; the raw scan finds no branch into the interior.
* **State:** ESI = R, `[R+8]` = this frame's level, `[R+4]` = the committed level, EDI = P when the sector path ran.
  EBX/EBP are popped at `0x0042ca50`. EAX/ECX are rewritten by the displaced instructions and the flags by the `cmp`.
* **Thunk:** rewrite `[R+8]`, for example to 0 unless "engaged", replay the two moves and `jmp 0x0042c97a`.
* **Call context:** once per frame on the main thread (`0x00403f2f` → `0x0041cde0` → `0x0041cdea`). `NotifyAlert`
  runs after the site, and no script path reaches `0x0042c890`.
* **Effect:** the filtered level drives the hysteresis next frame and the alert sounds, and it produces a fresh
  0 → 1 edge, i.e. `NotifyAlert(1)`, whenever "engaged" becomes true while a hostile is in range.

**Data for an "engaged" rule:**

* **At the site** there is only the level; the candidate list is not kept.
* **Player state:** P and its relation masks are available, and so is the player's selected target, `cockpit+0x1e0`
  ([chase-target-indicator.md](chase-target-indicator.md)).
* **Attack events** come from the engine's four `AttackedBy` script notifications: calls to `0x0049f4c0` at
  `0x0045e46b`, `0x0045e65c`, `0x0045e9c1` and `0x0045e9ef`, with name `0x0055fd9c`. Each pushes the victim's script
  object and two ids, one of them the attacker's engine id `[obj+8]` **[i]** which one is which. A filter there (by
  return address, or on the shared `0x0049f4c0` entry by name pointer) can timestamp "P was hit by X" and "P hit X".
* **Not found:** no engine "last attacker" field on the player object (the KC keeps it, in 2004 `m49`/`m54`). Player
  ownership of other ships was not found as an engine field either; the KC uses `GetOwner() == g9`.

So "player attacking or attacked within T s, and a hostile in range" is computable with the `0x0042c974` filter plus
an `AttackedBy` timestamp hook. "A player-owned ship attacking" needs more RE.

## 6. Not established, and the flight that settles it (Q6)

* **Run-time order:** that T_old runs in the same scheduler pass as C's stop on a sector change, and in the next frame
  after a combat start. This is read from the head and tail link code only.
* **SETA:** whether `TI_Delay(100)` and `(1000)` scale with SETA (game milliseconds; not traced).
* **Audible position:** how far the DirectSound-path graph position leads the audible position.
* **Unnamed engine data:** which Mayhem TShips rows `0x10f`, `0x13f`, `0x140` and `0x141` are; the meaning of
  `[[P+0x50]+0x1d4]`; the argument roles in `AttackedBy`; and 500 units/m.
* **The orphan policy (§3)** is unbuilt: the context-0 completion, the DirectSound re-arm of a gated record, and the
  free outside the manager's walk.
* **The latest flight** (`flight_music_summary.txt`) had `--music-trace` off. It has 8 plays and 6 ids, all from 0, no
  combat id, and the save replay `skip`. Plays came 7.5–106.9 s apart **[m]**, shorter than the tracks' CBR-estimated
  108–302 s **[i]**, so no natural end and no alert occurred.

**One flight settles the run-time questions.** Fly with `--music-trace` (`X3M_MUSIC_TRACE=1`; the keep may stay on).
Cover one gate jump, one hostile ship within about 5 km and its end, and one track left to end naturally (more than
5 minutes in one quiet sector). The existing lines already show:

* `music_trace_stop_movie id=<old> name=MOV_StopMovie` followed by `music_trace_play id=<new> start_ms=0`, with the
  `qpc` gap; the expected gap is at least 100 ms of game time, about 1 s after arrival;
* the combat pair `stop_movie(<sector>)` → `play(10xx)`;
* the post-combat pair `stop_movie(10xx)` → `play(<sector> start_ms=0)`;
* a natural end as a same-id `play` with no stop.

Not covered today, and worth adding to the same diagnostic build:

* on `music_trace_stop_movie` with return `0x00499886`: `task_ip=[ESI+0x1c]` and `new_id=` (the frame cell above,
  tag-checked);
* `alert_old=[R+4] alert_new=[R+8]`;
* `pos_ms=`, the graph position before the pause;
* a Present-sampled `music_trace_alert from= to=` line on every change of `[*0x00608504+4]`.

## Reproduce

```sh
python3 -I verification/results/music-script-driver/verify_music_script_driver.py > verification/results/music-script-driver/verify_output.json   # PASS
python3 -I verification/results/music-script-driver/music_driver_listing.py        # decoded listings into raw/ (git-ignored)
python3 -I verification/results/music-script-driver/flight_music_summary.py /mnt/c/X3/x3m.log > verification/results/music-script-driver/flight_music_summary.txt
i686-w64-mingw32-objdump -d --start-address=0x42c890 --stop-address=0x42ca61 /mnt/c/X3/X3AP.exe   # alert level
```
