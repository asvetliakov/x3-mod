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

**Correction (Run 189, §8.4):** this sequence is the path of combat, leave and other non-warp changes. A gate or
jumpdrive change first fades the old track out at the gate (`150::WarpToSector` → `StopMusic(500)`, stop IP
`0x0da17`), and the new id is then played without a stop.

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

**Implementation (2026-10-23; amended the same day: the id test below is replaced by the alert edge, no id list.**
A stop counts only on the rising edge `[R+4] = 0`, `[R+8] != 0`; a play with either level nonzero is a combat track.
See [story-builds.md](../architecture/story-builds.md) §3.**)** Under the existing `music_keep` option (`src/proxy/music_keep.cpp`, state machine
`ResumeMemory` in `music_keep_core.h`, host-tested in `verification/analysis/test_music_keep.py`). At the shared
`0x00498810` entry (already claimed by the keep), for the native caller `0x00499886` and an id outside 1001..1042, a
found record with music + playing flags has its position read as `0x004d0600` does (`[m+0x8c] & 0x20` →
`IMultiMediaStream::GetTime` on `[m+4]`, else `IMediaPosition::get_CurrentPosition` on `[m+0x70]`; both through the
engine's interface pointers, SSE2 only) and remembered. Combat plays keep it; the first start-0 play of that id whose
seek is not skipped is seeked to it: Patch C returns bit 4 and the thunk rewrites the seek's stack argument
(`[esp+4]` at its entry) before `jmp 0x004d0430`, EDI and so `[rec+0x1c]` stay 0. Another non-combat id, a positioned
or still-playing play of the id, a vanilla stop-all (load, session start, P_Leave, unknown) and the one use clear it; a
sector stop without a readable position forgets the old memory. The reader's 120 bytes are compared at install; a
mismatch logs `resume=bytes_mismatch` and nothing is remembered. The task-IP / alert discriminator of the bullets above
is not used: any script stop of a sector track is remembered and a sector change clears it at the next play. Lines:
`music_keep_resume id= saved_ms= action=remember|forget at=stop_movie`, `… action=seek|clear_sector|clear_played at=play
other_id=`, `… action=clear_stop_all at=load|session_start|…`, and `music_keep_seek … resume_ms=<ms>` on the seeked
play. For a DirectSound record the saved position is the decode position, ahead of the audible one by the unplayed
ring (up to about 5–7 s, inferred), so the resumed track starts that much later than where it was cut.

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

Since settled statically: run-time order (§7, up to I2), SETA (§7.2), the context-0 completion (§8.1) and the
excluded subtypes (§7.4). §8.3 replaces the orphan policy of §3.

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

## 7. Scheduler order of a sector change (2026-10-23)

Static, same EXE and story object. Every address and byte window below is pinned by
[`verify_track_queue.py`](../../verification/results/music-script-driver/verify_track_queue.py) (`PASS`: 54 byte
patterns, the TI jump table and names, the QPC import, direct-caller census, no raw branch into the interior of
`0x0049981f` or `0x00499865`, KC return IPs and literal ids, TShips rows; output `verify_track_queue_output.json`
beside it). **[m]** unless marked.

### 7.1 One frame of the main loop

The loop runs `0x00403ab0`..`0x004041d8` (back edge `je 0x00403ab0`). Per frame, in this order:

| Step | Call site | What it does for music |
| --- | --- | --- |
| 1 | `0x00403ac5` | if paused (bit 0 of `[*0x0057fc60+0x4a0]`, set by `X2_SetPause` at `0x0040705d` just before its stop-all), an extra clock call `0x004d1df0` and a resync of the frame-delta bases `0x00609104`/`0x00609108` |
| 2 | `0x00403af0` → `0x004b0e00` | **TI tick**: advances the game clock, then fires every due timer |
| 3 | `0x00403af5` → `0x004d34b0` | message pump; WndProc's WM_ACTIVATE inactive arm runs the alt-tab stop-all here |
| 4 | `0x00403aff` → `0x0049f770` | **scheduler pass** |
| 5 | `0x00403b04` → `0x00498370` | **media manager**: DirectSound pump, natural end |
| 6 | `0x00403f2f` → `0x0041cde0` → `0x0042c890` | alert level; `NotifyAlert` runs here as a synchronous call |
| 7 | `0x00403f34` | render, then Present |

Before the loop, `0x00403a6b`..`0x00403a98` run the tick, pump, scheduler and manager twice. `0x00497287`/`0x00497296`
call the tick and the scheduler outside the loop (**[i]** a load or leave path; not read).

### 7.2 `TI_Delay` and the game clock

* **Registration.** `0x004b11ba` installs dispatcher `0x004b12d0` with a NULL completion and names at `0x00579f80`
  (16 natives, `TI_Delay` = 0, `TI_SetTimeWarpFactor` = 9, `TI_SetAbsTime` = 13, `TI_GetRealTime` = 14; jump table
  `0x004b15f8`).
* **`TI_Delay(ms)`** (`0x004b1321`; with two arguments `s·1000 + ms`) calls `0x004b0b00` with EAX = ms, ESI = the TI
  owner `*0x006085ec`, and the stack holding slot and task id. That function:
  1. reads `now = [*0x00606f34+0x718]`, the game milliseconds;
  2. adds a jitter `rand()·j/32768` with `j = ms/16 + 1`, or 256 from 4,096 ms up. `TI_Delay(100)` is therefore due
     in 100..106 game ms, `TI_Delay(1000)` in 1000..1062;
  3. stores `due` at node `+0x10`, task id `+0xc` and slot `+8`, and links the node at the tail of owner list `+0`.

  It returns 1 **without a completion**. The interpreter cleared `task+0x20` before the call (`0x004a38a5`), so the
  task suspends at `0x004a3919` (state 2, `0x004a437e`). Only when no node can be allocated is the completion
  `(task, 0)` delivered at once.
* **TI tick `0x004b0e00`.**
  1. It first calls `0x004ee1e0` → `0x004d1df0`, the clock advance, which also writes the frame deltas
     `+0x714`/`+0x71c`.
  2. For every node with `due − now ≤ 0` (`jle` at `0x004b0ef5`) it delivers `(task id, 0)` through
     `[VM+0x34+slot·0x18]` (`0x004b0f29`). For script tasks that is `0x004a4910`, which relinks the task at the
     **head** of the runnable list.

  The `TI_Delay` result is dropped by every music caller.
* **Clock `0x004d1df0`.**
  1. It reads QPC (IAT `0x005320e4`) and converts to whole real ms, spinning until at least 1 ms has passed.
  2. `+0x720 += dt` is the real clock (`TI_GetRealTime`).
  3. An override `[0x00608a98] ≠ 0` is added to `+0x718` once. Otherwise
     `+0x718 += clamp(dt, 1, 200) · (([+0xcc]·[+0xd0]) >> 16) >> 16`.

  The two factors:
  * `[+0xcc]` is the 16.16 time-warp factor. It is written only by `TI_SetTimeWarpFactor` (`0x004b148c`), whose KC
    callers include `150::Vbi` (the SETA ramp, up to ×10) and `150::StopFastForward` (`65536` = 1.0), plus
    `203::Increase/DecreaseTimeWarp`, speech and cut-scene methods.
  * `[+0xd0]` is a load throttle written by the TI tick (`0x004b0f85`..`0x004b1071`). It is multiplied by 0.7, down
    to 0.3, while more than 1,000 timers fall due within the next `40·warp` ms. It recovers by 1.2 up to 1.0 below
    200, and resets to 1.0 when the warp factor drops.
* **SETA.** Every music wait is in **game ms and scales with SETA.** At ×k, `TI_Delay(100)` takes about 100/k real ms,
  and `402::EnterSector`'s `TI_Delay(1000)` about 1000/k. Two bounds apply: a wait ends no earlier than the next
  frame's tick, and real time per frame is clamped to 200 ms before scaling. Track playback runs on the graph's real
  clock, so a track does not end sooner under SETA.
* **Pause.** In the paused arm (step 1) the clock call still adds the scaled dt to `+0x718`. The following base resync
  keeps the frame delta `+0x714` near 1 ms. Whether the story also sets the warp factor to 0 on pause was not traced.
  **Not established:** whether TI waits expire during a pause.

### 7.3 A sector change, frame by frame

(Run 189, §8.4: the C-stop path below is the one combat, leave and non-warp changes take. Gate jumps fade out at
`0x0da17` first.)

1. **Frame F, TI tick:** the `EnterSector` fork child E's `TI_Delay(1000)` falls due. E is linked at the runnable
   head.
2. **Frame F, scheduler pass:** E runs `ChangeTrackToSectorMusic` → `ChangeTrackMusic`, whose fork links task C at
   the tail, sets `[VM+0x1434] = C` and runs C nested at once (`0x004a3760`).
3. **C stops the old track.** C sets `m17 = −1` and calls `MOV_StopMovie(old)`, which enters `0x00498810`:
   1. it takes the first record with that id;
   2. clears flag `2` and pauses the record (`0x004d1810`);
   3. delivers `(T_old, 1)` through the record's slot (`0x0049888c`). `0x004a4910` → `0x004a4740` then moves T_old
      from state 2 to 1, replaces its native arguments with the result and links it at the **head**
      (`0x004a47c9`..`0x004a47e0`);
   4. zeroes `+0x18` and `+0x14` (`0x00498898`/`0x0049889f`);
   5. delivers `(C, 1)` through EDI (`0x004988c1`). C is in state 1, so this only sets `+0x20 = 1` (the relink is
      skipped at `0x004a4745`), and C continues synchronously.
4. **C waits.** It sees `m17 == −1` and calls `TI_Delay(100)`: suspended, with a node due at `now_F + 100..106`.
5. **E finishes.** It resumes after the fork, returns and ends (op `70`).
6. **Same pass, T_old frees the old record.** `0x0049f770` re-reads the head after every task step (`0x0049f7a2`), so
   T_old runs next. Its `MOV_PlayMovie` returned 1 and `m17 ≠ own`, so it calls `MOV_FreeMovie(old)`
   (`0x004986f0` → `0x004986b0` → `0x004984d0`: completion skipped since the context is 0, media teardown, unlink,
   free). It then sets `m18 = 0` and `m17 = −2`, and ends.
7. **Frame F, manager:** nothing is left of the old id.
8. **Frame F+n, n ≥ 1, TI tick:** C's node falls due and C is linked at the head. In that pass C sees `m17 = −2`,
   sets `m17 = new`, `m18 = 0`, then runs `MOV_LoadMovie` (a new record, completion 1), `MOV_SetVolumeMovie` and
   `MOV_PlayMovie`. That last call is the `0x00498c90` seek to 0, Run, flag `2` and context store; C then blocks on
   the record.

| # | Invariant | Enforced by | Depends on run-time state |
| --- | --- | --- | --- |
| I1 | The stop wakes T_old before C continues, and T_old is at the runnable head before C suspends | order `0x0049888c` → `0x004988c1`; head link `0x004a47c9` | no |
| I2 | When the stop happens inside a scheduler pass, T_old frees the record in that same pass | `0x0049f770` loops while the head is non-empty and the step returned non-zero (`0x0049f7a0`) | **yes**: a native returning EAX = 0 later in the pass (`0x004a390e`) ends it, and T_old then runs first in the next pass |
| I3 | C's load and play come at least one frame after its stop | the tick precedes the pass (`0x00403af0` < `0x00403aff`); `due > now` at insertion; the due test at the tick only | **yes**, the count: the first frame whose accumulated scaled dt reaches 100..106 game ms, about 6 frames at 60 fps and ×1, 1 frame at ×10 |
| I4 | Free (frame F) precedes C's load (frame ≥ F+1) on a sector change | I2 + I3 | as I2 |
| I5 | Combat start: the stop runs in the cockpit phase, so T_old frees in the next frame's pass. If C's node fires in that frame, the LIFO head link puts C in front of T_old; C sees `m17 = −1` and waits once more | `0x00403f2f` after `0x00403b04`; head insertion | SETA and frame rate |
| I6 | C gives up (returning with `m17 = −1`) only after 9 waits with `m17` still −1 | KC `0x0db55`..`0x0db75` | only if T_old never runs |

This settles the §6 bullets "Run-time order" (statically, up to I2's dependence) and "SETA". Gap at a sector change:
at least one frame and about 100 game ms of silence between the old record's free and the new record's Run.

### 7.4 Two §6 data unknowns

* **The alert's excluded ship subtypes are mines.** TShips rows `0x10f` = `SS_SH_MINE_1`, `0x13f` = `SS_SH_TR_MINE`,
  `0x140` = `SS_SH_MINE_2` and `0x141` = `SS_SH_MINE_3`, the same in every addon catalogue that carries TShips
  (`01`, `02`, `03`, `07`, `10`, `12`). **[m]** `verify_track_queue.py`. That `[O+0x4a]` is the TShips row index
  is **[i]**, consistent across the builds.
* **The SETA scaling of the music delays:** §7.2.

## 8. Deferral site contracts and the track-queue policy (2026-10-23)

### 8.1 Completion facts a deferral relies on **[m]** unless marked

* **A released record's natural end wakes nobody.** The stop zeroes both `+0x18` (slot) and `+0x14` (context). At the
  natural end the manager therefore reads slot 0's completion `[VM+0x34]`, which is NULL:
  * the VM constructor zeroes all 32 slots (`0x0049ced4`..`0x0049cef3`, EBX = 0);
  * the script slot is index 1 (`0x0049cf2e`), and every later registration takes the next index;
  * so the call at `0x00498461` is skipped (`0x00498459`).

  With the script slot left in `+0x18`, context 0 would still be a no-op. `0x004a4910` looks key 0 up in the task
  hash, and task ids come from the auto-id `0x004efcc0`, which issues 1..`0x7ffffffe` and wraps to 1
  (`0x004efcc5`..`0x004efcde`). **[i]** for tasks restored from a save, because the loader's key source was not read.
  This settles music-restart.md §6 "ctx 0 undecoded": §3's orphan with context 0 is safe and silent.
* **Natural end** (`0x0049842b`..`0x00498469`) needs flag `2` (the walk visits only flagged records, `0x00498395`),
  the loop bit clear (`0x00498402`), id ≠ 0 (`0x0049842e`: an id-0 record never ends) and status 2 from
  `0x004d14e0`. It then clears flag `2`, delivers `([rec+0x14], 1)` through `[rec+0x18]` and zeroes both. It never
  checks which task stopped the record. **Any flag-`2` record is pumped and serviced, and its natural end wakes
  whatever task its `+0x14`/`+0x18` name**, in the manager phase; that task runs in the next frame's pass.
* **Results the script sees.**
  * `MOV_FreeMovie`: delete-by-id `0x004986b0` frees the first record with media ≠ 0 and that id and returns 1, else
    0; `0x004986f0` delivers `(task, result)`; the KC **drops** it (`0x0dc40`).
  * `MOV_StopMovie`: dropped too (`0x0db54`). The caller's completion still matters: without it C would suspend on
    the stop.
  * `MOV_PlayMovie`: 0 clears the loop flag (`0x0dbf7`).
* **Ids.** `MOV_LoadMovie` reuses an existing record with the id and completes 1 at once (`0x004987ae`..`0x004987c3`,
  `0x00498803`), so a record the proxy keeps must not keep a script id. The MOVI writer saves **every** linked record
  (`0x00498955`). The restore recreates absent ids through the allocator, except 101..299 (`0x00498b7e`), and ignores
  `+0x14`/`+0x18`.
* **`0x00498c90`:**
  * an absent id is created once with default flags (`0x00498cd5`, EAX = 0);
  * a seek or run failure frees the record and completes `(ctx, 0)` (`0x00498dde`);
  * on success it stores `a2`/`a1` into `+0x14`/`+0x18` (`0x00498dca`).

### 8.2 The sites

| Site | Bytes and boundary | Entry state | Contract | Used by §8.3 |
| --- | --- | --- | --- | --- |
| T3 `0x00498810` (shared: keep, trace) | `8b 0d 44 6f 60 00`; qword offset 0 | from `MOV_StopMovie`: return `0x00499886`, EAX = id, EDI = slot, `[esp+4]` = caller task id, ESI = task | preserves EBX/EBP/ESI/EDI; EAX/ECX/EDX scratch; the caller ignores EAX (`0x0049988a`) and the flags (`add esp,4`). Consume with `ret` | yes |
| Free call `0x00499865` | `e8 86 ee ff ff`, between `mov ecx,[edx+1]` (`0x00499862`) and `add esp,4` (`0x0049986a`); qword offset 5, so the five bytes cross a qword (plain write); no raw branch into the interior | ECX = id, ESI = slot, `[esp+4]` = task id | the callee completes `(task, found)` itself; a thunk that skips it must deliver `(task, 1)` through ESI | **no**: with the re-key at the stop, T_old's free is a miss, returns 0 and is dropped |
| Patch C `0x00498d54` (keep) | as music-restart.md §6 | EAX = ESI = record, `[esp]` = start ms | 0 return frees the record (`0x00498dde`) | no |
| Run call `0x00498d71` (planned Patch E, music-restart.md §7.5) | `e8 fa 8a 03 00` | EAX = ESI = record | after a skipped Run the play still sets flag `2` and the context (`0x00498d8f`, `0x00498dd2`), so the manager queries the record each frame and only Patch D could hide it | no |
| Patch D `0x004983d9` (keep) | as music-restart.md §6 | ECX = record | | no |
| T2 `0x00498c90` (trace; shared claim needed) | `51 53 8b 5c 24 14`; qword offset 0 | from `MOV_PlayMovie`: return `0x00499824`, `[esp+4]` = slot, `[esp+8]` = task id, `[esp+0xc]` = id, `+0x10..+0x18` = 0, `+0x1c..+0x24` = −1, `+0x28` = 0; ESI = task, `[ESI+0x1c]` = `0x0dbf7` | cdecl, the caller pops `0x28`; preserves EBX/EBP/ESI/EDI; EAX and flags ignored. Consume with `ret`: the handler never completes synchronously (`0x00499824`..`0x0049982e`), so the task suspends exactly as in vanilla | yes |
| or the handler's call `0x0049981f` | `e8 6c f4 ff ff`, between `push ecx` (`0x0049981e`) and `add esp,0x28` (`0x00499824`); qword offset 7 (plain write); no raw branch into the interior | as T2 at the callee | as T2 | alternative to sharing T2 |
| T1 `0x004982b0` (shared: keep, trace) | `a1 44 6f 60 00` | `[esp]` = caller return (`alt_tab` `0x004d36c2`, `save` `0x00404561`, `pause` `0x00407069`, `load` `0x00404cf2`, `p_leave` `0x00497bbb`, `session_start` `0x0040387d`) | before the stop-all walk | yes |

**Calling and reentrancy.** Everything runs on the main thread.

* **Where T2 and T3 run.** Only from MOV natives inside an interpreter step: a scheduler pass, a nested fork child, or
  `NotifyAlert`'s synchronous call in the cockpit phase. T3 has two more callers without a task, `0x0045c27d` and
  `0x004f66be`; they stay vanilla.
* **Completions** go through the slot table exactly as the engine does them: `VM ≠ 0`, `[VM+4] ≠ 0`, slot < 32,
  function ≠ 0, cdecl `(ctx, status)`. They reach only `0x004a4910` → `0x004a4740`, list edits that run no bytecode
  ([media-record-lifetime.md](media-record-lifetime.md)), so no handler re-enters itself.
* **The proxy's one free** is `0x004986b0` with ECX = key. It pushes and pops ESI, clobbers EAX/ECX/EDX and frees only
  a linked record with that id and media ≠ 0, so a stale record pointer can never be freed. Call it only from T2/T3
  (the context `MOV_FreeMovie` uses) or from T1 for the non-`alt_tab` callers, after the queue state is updated.
* **Never free from T1 for `alt_tab`.** WndProc can run inside a COM call of the manager walk, whose saved next record
  is EBP (media-record-lifetime.md, boundary 1).

**Frame cells at T3** (the stop at `0x0db4f`). Index `k = [ESI+0x18]`; cell `i` sits at `[ESI+0x14] + 5·i`; deeper
cells have higher `i`. **`k` is negative** (`[ESI+0x14]` is the end of the buffer, valid `−capacity ≤ i ≤ −1`);
Run 189 measured `k = −19`, so `k+18 = −1` is the bottom cell (§8.4).

| Cell | Content |
| --- | --- |
| `k` | argc 1 |
| `k+1` | old id |
| `k+2..k+4` | three locals |
| `k+5` | tag 10 |
| `k+6` | tag 3 = `0x0dc84`, the return into `LoopMusic` |
| `k+7` | argc 3 |
| `k+8` | new id |
| `k+9` | fade |
| `k+10` | loop |
| `k+11` | tag 10 |
| `k+12` | tag 3 = `0x0dcdb`, the return into `ChangeTrackMusic`'s forked frame |
| `k+13`, `k+14` | argc 1, id |
| `k+15` | tag 10 |
| `k+16` | tag 3 = 0 |

**[i]**: assembled from the handlers, not executed; the decoded stack depths of `PlayMusicWithFade` carry 4
conflicts. The handlers are `callself` `0x004a3ee1` (two new cells), `callcls` `0x004a3d1d` (the return IP
overwrites the class cell), the pushes `0x004a8620` (tag 3, IP) and `0x004a8640` (tag 10), and the fork copy
`0x004a36c4`..`0x004a36e9`, which **zeroes every tag-3 cell**. The requester (sector music, docking, boarding, the MD)
is therefore **not recoverable from C**; only "forked by `ChangeTrackMusic`" is.

### 8.3 Simplest policy: park the new task, keep the old record **[i]** design from the code above; built 2026-10-23 as `music_queue` ([music-queue.md](../architecture/music-queue.md))

**Mechanism.** Keep the old record O playing under a private key and let the new task C block in its own
`MOV_PlayMovie`, as in vanilla, with O carrying C's wake.

* The engine's own natural-end completion wakes C. C's KC replay (`0x0dc02`..`0x0dc66` → `0x0dbe8` →
  `MOV_PlayMovie(m17)`) then starts the new track through the vanilla play path.
* The proxy never seeks, runs or completes a media record itself. No Patch C, Patch D or run-call change is needed,
  and no Present work.
* `m17 = new` during the whole wait, so the script accepts every other request normally.
* None of it depends on I2: the re-key makes T_old's free a miss whenever T_old runs.

**Prerequisite for gate and jumpdrive changes:** the `150::WarpToSector` patch of §8.4. Without it a jump never
reaches this test.

**Deferrable stop.** All of the following must hold at T3, otherwise vanilla:

* return `0x00499886` and `[ESI+0x1c] == 0x0db54`;
* the cells `k+6 = (3, 0x0dc84)`, `k+10 = (1, 1)` and `k+12 = (3, 0x0dcdb)`, i.e. a loop request from a
  `ChangeTrackMusic` fork child. This excludes class 621's direct `StartMusic`/`LoopMusic`;
* the new id `N` (cell `k+8`, tag 1) is greater than 0 and not one of:
  * 13 (death);
  * 14 (landing, `DockingAllowed`);
  * 1001..1042 (Mayhem combat; stock AP has a different list);
  * 8233..8237 (boarding);
* `R = *0x00608504 ≠ 0` with `[R+4] == 0` and `[R+8] == 0`. A combat start stops inside `NotifyAlert` with
  `[R+8] ≠ 0`; a combat end with `[R+4] ≠ 0`;
* the found record is music and playing (`& 0x82 == 0x82`), loop bit clear, `+0x14 ≠ 0`, media ≠ 0.

Docking (`2001..2004`) and the MD's `DoPlayMusic` pass this test. Add `2001..2004` to the exclusions if docking must
switch at once.

**States:** IDLE; ORPHANED (O, key P, pending N, task C); PARKED (as ORPHANED, plus C's slot). P is a private nonzero
id outside 101..299 and every script id, for example `0x7f000000 + n`.

| From | Event | Action | To | Line |
| --- | --- | --- | --- | --- |
| IDLE | deferrable stop(old) by C at T3 | `[O+0x10] = P`; keep flag `2` and skip the pause; deliver `([O+0x14], 1)` through `[O+0x18]` (T_old); zero both; deliver `(C, 1)` through EDI; `ret` | ORPHANED | `music_queue_defer old= new= record= key= task=` |
| ORPHANED | T_old's `MOV_FreeMovie(old)` | none: a miss, result 0, dropped | ORPHANED | – |
| ORPHANED | T2 play from `0x00499824`, `[ESI+0x1c] == 0x0dbf7`, `a2 == C`, `a3 == N`; O linked under P with flag `2` | `[O+0x14] = a2`, `[O+0x18] = a1`; `ret` (N gets no seek, no Run, no context) | PARKED | `music_queue_park task= id= record=` |
| ORPHANED | that play, but O has ended or is gone | free P; continue vanilla | IDLE | `music_queue_end reason=ended_before_park` |
| PARKED | O ends naturally | the engine delivers `(C, 1)` (§8.1); C replays N in the next pass | PARKED | – |
| PARKED | C plays N again at T2 | free P (a miss if the engine already freed O); continue vanilla, so N starts from 0 | IDLE | `music_queue_release reason=natural_end\|stop_all\|lost` |
| PARKED | deferrable stop(N) by D (a further jump to M) | deliver `([O+0x14] = C, 1)` through `[O+0x18]`; zero both; deliver `(D, 1)` through EDI; `ret`. C frees N's unplayed record and exits with `m17 = −2` | ORPHANED (N := M, task := D) | `music_queue_collapse from= to=` |
| PARKED | non-deferrable stop(N): combat, `StopMusic`, any other | as the row above, then free P; `ret` | IDLE | `music_queue_end reason=stop new=` |
| any | T1 stop-all, `alt_tab` | depends on the keep's live alt-tab mode (implementation, 2026-10-23): `skip_all` leaves O alone and the queue stays; `paused` (Patch D refused) ends the queue like every other caller, O's private-key record is freed at the next Present (`event=free_later`, never inside the stop-all's walk). Keep off: the stop-all pauses O and wakes C through O's context, and C's replay ends the queue | – | – |
| ORPHANED, PARKED | T1 stop-all, any other caller | if PARKED, deliver `(C, 1)` through O's slot; zero O's context; free P; let the stop-all continue | IDLE | `music_queue_end reason=stop_all name=` |
| any | any T2/T3 event while O has ended (flag `2` clear, context 0) and is unowned | free P | IDLE | `music_queue_end reason=stale` |

`music_queue_refuse reason=alert|not_fork_child|immediate_id|not_playing|cells` is written once per non-deferred
`PlayMusicWithFade` stop. Cost: per music request one bounded list walk and at most 17 cell reads; nothing per frame.

**Failure modes and fallbacks:**

* **The pending id's record freed while PARKED.** Nothing in the KC frees N except C itself (subsystem clear and
  manager shutdown aside). If it happened, C's replay through vanilla `0x00498c90` recreates it with default flags
  (`0x00498cd5`), which is vanilla's own behaviour for an absent id.
* **O lost.** The manager's error path (`0x004984ae`) frees O inside its own walk and wakes C through O's context; the
  replay frees a miss and continues vanilla.
* **Combat start.**
  * PARKED: the combat task's stop(N) is non-deferrable. C is woken, O is freed, and combat music starts after
    `TI_Delay(100)` as in vanilla.
  * ORPHANED: C is still in `TI_Delay`, `m17 < 0`, and the KC drops the request (`0x0dab4`). Vanilla has the same
    window.
* **Save, load, pause, leave.** T1 ends the queue before the walk. The save's MOVI chunk then holds only N and C is
  runnable: the vanilla savegame, which avoids music-restart.md §7.3's suspended-task trap and keeps the private key
  out of the restore. Under keep or vanilla, the new track then starts from 0 after the save or pause: a visible
  degradation, with the old track cut at the save.
* **Alt-tab.** As in the T1 row: under the keep's `skip_all` mode O plays on; under its `paused` mode the queue ends
  (O freed at the next Present); without keep, the vanilla stop-all wakes C, and the new track starts from 0.
* **`StopMusic` with a fade while PARKED.** `MusicFadeOut` fades N, which is silent, then stops it. The queue ends
  with O cut, not faded.
* **Task killed or the VM rebuilt.** O's context names a dead task, its natural end is a no-op lookup, and the stale
  row frees O at the next event.
* **Any claim refused.** No queue; vanilla.
* **Interaction with the combat resume (§4).** Its T3 "remember" must ignore N's record while PARKED: N never ran,
  and the audible track is O.

**Not established statically, and the trace that settles it.**

* **(a) The cell layout above.** Settled by one flight with a log-only build: at every T3 stop with IP `0x0db54`, log
  `music_queue_cells` with the tag and value of cells `k..k+16`, `[R+4]`, `[R+8]` and the decision it would take
  (`would_defer`/`refuse`); at T2 with return `0x00499824`, log `a1`, `a2`, `a3` and `[ESI+0x1c]`. The flight needs
  two gate jumps, one dock and one combat.
* **(b) Audible continuity** of O across its re-key (the pump depends only on flag `2` and media state, §8.1) and the
  gap at the release: about one frame, plus the DirectSound prebuffer the vanilla same-id replay also has. Settled by
  the first armed flight.
* **(c) Pause and TI** (§7.2) matters only if pause is later kept out of the "end at T1" row.

### 8.4 Run 189 flight (2026-10-23)

Rows from `verification/results/run189-flight/x3m.log` (local copy; `music_trace = 1`; several gate jumps, one save,
one dock, two combats), extracted by
[`run189_music_rows.py`](../../verification/results/music-script-driver/run189_music_rows.py) (output
`run189_music_rows.txt` beside it). QPC runs at 10.000 MHz (from the `frame_end` rows), and the flight ran at about
170–200 fps. **[m]** unless marked.

| Stop (frame) | KC IP | `k` / capacity | alert old/new | Stopping task = next player? | Next play | Δ frames | Δ ms |
| --- | --- | --- | --- | --- | --- | --- | --- |
| gate jump ×5 (17604, 26712, 36090, 45891, 53381) | `0x0da17` (`MusicFadeOut`) | −18 / 32 | 0/0 | no | the new sector id | 208, 291, 226, 222, 282 | 3906, 2293, 3403, 3465, 2730 |
| combat start (23839) | `0x0db54` | −19 / 32 | 0/1 | yes | 1010 | 19 | 111.0 |
| combat start (61050) | `0x0db54` | −19 / 32 | 0/1 | yes | 1014 | 2 | 20.6 |
| combat end (25742, 63568) | `0x0db54` | −19 / 32 | 1/0 | yes | the sector id | 2, 2 | 29.4, 16.9 |
| leave to menu (64740) | `0x0db54` | −19 / 32 | 0/0 | no | 8404 after `session_start` | 192 | 2580 |
| speech (25758), intro (4) | `0x0d31f` (`200::SpeakWithPriorityAndNoise`), `0x078eb` (`202::Patch18`) | −46 / 96, −15 / 64 | 0/0 | – | – | – | – |

* **The record's context at every stop is the task id of the play that started it** (10 of 10). This is T_old, as
  §7.3 assumes. `arg_task == task_id` in every row.
* **The dock produced no MOV row.** No `200x` dock id was played and no stop fell on it.
* **The save** gave stop-all `save` at frame 56228, `keep_running`, then a same-task, same-id replay at 56229 with the
  seek skipped and `music_keep_save_pump`. That is the next pass, as §7 predicts.

**(1) Frame cells.** Every row says `cells=invalid` because `k` is **negative**. The trace's bounds test is wrong,
and so was §8.2's reading of the index:

* `[ESI+0x14]` is the **end** of the cell buffer (task allocator `0x004a21e0`: base + capacity·5), `+0x18` starts at
  0, and pushes decrement it;
* valid cells are `−capacity ≤ i ≤ −1`, and the bottom cell is −1.

The measured depths match the §8.2 layout exactly:

* the `ChangeTrackMusic` fork child stops at `k = −19`: 19 cells, `k..k+18`, with `k+18 = −1` its bottom;
* the `MusicFadeOut` fork child stops at `k = −18`: the 9-cell copy of the `PlayMusicWithFade` frame, plus the
  2-argument call (3 cells), 2 return cells, 2 locals and the stop's argc and argument.

So, by absolute index for the 0x0db54 stop:

| Cell | Index | Content |
| --- | --- | --- |
| old id | −18 | `k+1` |
| return into `LoopMusic` | −13 | `k+6`, (3, `0x0dc84`) |
| new id | −11 | `k+8` |
| fade | −10 | `k+9` |
| loop | −9 | `k+10` |
| return into `ChangeTrackMusic` | −7 | `k+12`, (3, `0x0dcdb`) |
| the fork-zeroed return | −3 | `k+16`, (3, 0) |

Depth is **[m]**; the values are **[i]**: the rows carry no cell values. The ids the plays then used match the
stopping task's request: the combat ids 1010 and 1014, and the sector ids after combat. That is consistent but does
not read the cell.

**(2) Task order.**

* **The C-stop path** (combat start and end) is as §7.3 says: the stopping task is the next player. The leave asked
  for id 0, so no play followed it. The
  minimum gap is **2 frames**: the stop runs in the cockpit phase of F, T_old frees in pass F+1, and C's TI node fires
  in tick F+2. Three of four gaps are exactly 2 frames (16.9–29.4 ms), which needs about 100 game ms in at most
  29 ms, i.e. warp ≥ 3.4 (**[i]** SETA on, ×10 consistent). The other gap is 19 frames, 111 ms: `TI_Delay(100..106)`
  at ×1 plus frame rounding (**[i]** SETA off).
* **No row shows C running before T_old** (that would give ≥ 3 frames). The free itself is not traced.
* **Gate jumps take another path** (point 3).

**(3) The stop IPs.**

* Gate and jumpdrive changes never reach `0x0db54`. `150::WarpToSector` (callers `150::StartGateWarp` and
  `150::StartSectorWarp`; the same code in stock AP) **forks a child that calls `StopMusic(500)`**: Mayhem
  `0x161c3`..`0x161d6`, stock `0x1639c`..`0x163af`. Its `PlayMusicWithFade(0, 500, 0)` sets `m17 = −1` and forks
  `MusicFadeOut`. That task ramps the volume over 500 game ms with `TI_GetAbsTime`/`TI_CmpTime` and `TI_Delay(100)`
  steps, so the fade is SETA-scaled, then stops the record at IP **`0x0da17`**. T_old frees it, and the fade child
  sets `m17 = 0`.
* At the arrival, `EnterSector`'s child requests the new id with `m17 = 0`, so **C stops nothing** and plays at once.
* Combat start and end and the leave stop at `0x0db54`; the dock stopped nothing.

**(4) What this contradicts.**

* **§3's and §7.3's "a sector change" sequence** (C stops the old track) is the path of combat, leave and other
  non-warp changes. A gate jump is a fade-out by a separate task at the gate, followed by an unstopped play after
  arrival.
* **§8.3's deferrable test (`0x0db54`) never fires on a jump.** Without a change it would defer nothing in this
  flight; the leave stop at 64740 passes the alert test but has new id 0, so `N > 0` rejects it.
* **§8.2's cell index** is negative (above).
* **Confirmed:**
  * the alert values the test reads: 0/1 at the combat start (inside `NotifyAlert`), 1/0 at the combat end, 0/0
    otherwise;
  * T_old = record context;
  * the next-pass wake after a stop-all;
  * nothing in the rows contradicts §7.1/§7.2.

**§8.3 amended.** One prerequisite for gate and jumpdrive changes, and three amendments.

* **Prerequisite:** remove the `StopMusic(500)` fork in `150::WarpToSector`. Replace the 6 bytes `op6f; jt <cont>` at
  CODE `0x161c3` (Mayhem; stock `0x1639c`; file offset +20) by `jmp <cont>` (`32` + target, 5 bytes) plus one filler
  byte that never runs.
  * Checks: no branch lands inside the window (`run189_music_rows.py`); stack balance is unchanged (the fork's result
    cell was consumed by the `jt`); instruction starts outside the window and saved task IPs (`0x161d5` and later)
    are unchanged. The same constraints apply as to the KC options of [gate-queue-script.md](gate-queue-script.md) §4 (same
    length, build id and instruction starts).
  * With the patch the old track plays through the warp, and the arrival's request becomes the §8.3 entry: C stops
    the old record at `0x0db54` with alert 0/0. This is the path the combat stops measured here.
  * Without the queue, the patch alone means "no fade at the gate; cut 1000 game ms after arrival".
* **Cell checks** use the absolute indices above and the correct bounds; on any mismatch the stop takes the vanilla
  path and logs `music_queue_refuse reason=cells` with the values.
* **A `0x0da17` stop is never deferrable.** Fades by cut scenes and the MD stay vanilla.
* **No other state or transition changes.** An engine-only alternative (hide `MusicFadeOut`'s volume ramp and orphan
  at `0x0da17`) cannot tell a warp fade from a cut-scene fade, because the fork zeroes the requester's return IP.

**Can it be built now?** Yes, with one condition. Every engine contract is static, and this flight confirms the C-stop
mechanics, the owner context, the alert test values and the stack depth. Two things are unflown:

* the cell **values**, not logged because of the trace's bounds test;
* the patched warp flow.

Neither needs a diagnostic flight of its own if the build fails safe on the cell checks and logs them. The first armed
flight (one gate jump, one track left to end, one jump while a change is pending, one combat while pending, one save
while pending) is then the acceptance flight. Remaining open: cell values; whether docking changes music at all
(nothing in this flight); pause and TI (§7.2).

## Reproduce

```sh
python3 -I verification/results/music-script-driver/verify_music_script_driver.py > verification/results/music-script-driver/verify_output.json   # PASS
python3 -I verification/results/music-script-driver/music_driver_listing.py        # decoded listings into raw/ (git-ignored)
python3 -I verification/results/music-script-driver/verify_track_queue.py > verification/results/music-script-driver/verify_track_queue_output.json   # PASS (§7-§8)
python3 -I verification/results/music-script-driver/run189_music_rows.py > verification/results/music-script-driver/run189_music_rows.txt   # §8.4
python3 -I verification/results/music-script-driver/flight_music_summary.py /mnt/c/X3/x3m.log > verification/results/music-script-driver/flight_music_summary.txt
i686-w64-mingw32-objdump -d --start-address=0x42c890 --stop-address=0x42ca61 /mnt/c/X3/X3AP.exe   # alert level
```

## 9. Engagement data for a narrowed trigger

2026-10-23. Static study of the same `X3AP.exe` (SHA-256 `fdbf3418d8f0a897…`) and of both `L/x3story.obj` builds.
Nothing was launched; no Wine. It answers what an "engaged only" rule at the `0x0042c974` site (§5) can read: the
player attacking, the player attacked, or a player-owned ship near the player attacking or attacked.

| Script (tracked) | Output | What it establishes |
| --- | --- | --- |
| [`verify_engagement_data.py`](../../verification/results/music-script-driver/verify_engagement_data.py) | `verify_engagement_output.json` | `PASS`: 37 instruction windows, 3 strings, 14 `SA_` name/handler pairs, 7 complete direct-caller lists, the writers and readers of the Flags2 bits below (raw byte census), no branch into the interior of four candidate spans, and the KC census of all 10 `SA_FireOnObject` call sites in each build (only `606::FireLaser` sets the player-shot bit) |

**Answer.**

* **A hit leaves a per-frame bit, not an attacker.** Every projectile hit sets Flags2 `0x80000` on the victim. A hit
  by a player shot also sets `0x40` on the victim and bumps a global counter. The collision pass clears both bits
  before the hits of each frame, and it runs before the alert routine in the same main-loop iteration. No engine
  field keeps the attacker or the time; `AttackedBy(attacker id, projectile id)` goes only to the script. **[m]**
* **A player shot is marked.** The cockpit script's `FireLaser` is the only caller that passes fire flag bit `0x2`.
  That bit marks the bullet (Flags2 `0x100`) and counts it in `SA_GetLaserFired`. Hits by such shots count in
  `SA_GetLaserHits`/`SA_GetMissileHits`. Projectiles keep their shooter and its owner group, but not a target. **[m]**
* **Owned ships:** a ship is player-owned when `(int16)[obj+0x3c] == 10`. It is attacking when its order is
  `0x10`/`0x11` with target `flight+0x224`. It is attacked when its Flags2 has `0x80000` this frame, or when a
  hostile ship's attack order targets it. **[m]** fields, **[i]** that the AI always fights through these orders.
* **So a thunk at `0x0042c974` needs no second hook.** It can evaluate the whole rule per frame from fields, counters
  and its own walk of four class lists, with the game clock `cfg+0x718` (§9.4, §9.5).

### 9.1 The four `AttackedBy` sites (Q1) **[m]** unless marked

All four are in `0x0045e130`. It is stdcall `(A, B)` (`ret 8`) and resolves one colliding pair. Its five call sites
`0x0045d233`, `0x0045d2ff`, `0x0045d691`, `0x0045da3c` and `0x0045df5c` all lie in the collision pass `0x0045d250`;
`0x0045d233` sits in the helper `0x0045d1b0`, which only `0x0045d250` calls. The pass runs once per active sector per
main-loop iteration (`0x0043a38f`, from `0x0043a360` at `0x00403b17`), on the main thread.

**Attacker.** At entry `[esp+0xc]` is the attacker of B and `[esp+0x14]` is the attacker of A (`0x0045e14c`):

* A is a bullet (class 0): `[[A+0x50]+4]`, the shooter;
* A is a missile (class 10) with `[A+0x40] & 0x1000000`: `[[A+0x50]+0xc]`, the launcher (`0x0045e2b1..0x0045e2dc`);
* otherwise 0.

A projectile that hits its own shooter ends the call (`0x0045e290`, `0x0045e2ab`).

| Site | Event | Victim | Arguments `(arg0, arg1)` |
| --- | --- | --- | --- |
| `0x0045e46b` | A is a bullet or armed missile and hits B. Damage `0x00449a60(B, A, …)` (stdcall, `ret 0x10`) is applied **before** the call | B (ESI) | (attacker id `[[esp+0xc]+8]` or 0, projectile id `[A+8]`) |
| `0x0045e65c` | the mirror: B hits A | A (EBX) | (`[[esp+0x14]+8]` or 0, `[B+8]`) |
| `0x0045e9c1` | contact of two bodies (neither class 0 nor 10, `0x0045e6b0..0x0045e6d8`) when A or B is under manual control (`0x0043ff80`: `[+0x40] & 0x40000` and not `0x80000`) and neither is TShips subtype `0x10b` (`0x004191c0`) | B | (A id, 0) |
| `0x0045e9ef` | the same contact | A | (B id, 0) |

Each call is skipped when the victim has no script object (`[victim+0x94] == 0`). The calls go through the by-name
call `0x0049f4c0` (`[M+0x60]`, 0, script object, `"AttackedBy"`, 1, argc 2, arg0, arg1), the same encoding as
`NotifyAlert` in §2.

* **The event is an impact.** It is not a lock or a launch. A launch is announced separately as `"MissileLaunched"`
  (`0x0055fcac`, `0x00449339`), and no lock notification was found.
* **Bullet damage** comes from the bullet type record `*0x00606fb8 + subtype·0xdb8` (`+0x44`, `+0x80`). With record
  flags `+0x58 & 6` it is scaled by the frame step `cfg+0x714` (`0x0045e32a`; continuous weapons **[i]**).
* **Argument roles in the script:** `2004::AttackedBy(p-4, p-5)` calls `SA_GetEventObject(p-4).GetOwner()`, then
  `SA_GetMainType(p-5) == 10` and `SA_GetFlags2(p-5) & 256`. So arg0 is the attacker and arg1 the projectile. This
  settles the open role question in §5 and §6.

**Engine fields written per hit.** Flags2 is `[obj+0x44]` (`SA_GetFlags2`/`SA_SetFlags2`, ids `0x66`/`0x65`,
`0x0046382e`/`0x00463854`).

| Write | Where | Meaning |
| --- | --- | --- |
| victim Flags2 `\|= 0x80000` | `0x0045e4b5`, `0x0045e6a6` (the only two writers) | hit by a projectile this frame, any shooter except itself; not set by a contact |
| victim `\|= 0x40`, projectile `\|= 0x800`, `M+0x48 += 1` (bullet) or `M+0x4c += 1` (missile hitting a non-bullet) | `0x0045e473..0x0045e4ae`, mirror `0x0045e664..0x0045e69f` | only if the projectile has `0x100` (a player shot, §9.2) and not yet `0x800`: hit by the player this frame, counted once per projectile |
| `and [obj+0x44], 0xfff7ffbc` for every object in the sector's 32 lists | `0x0045d287`, head of the collision pass | clears `0x80000`, `0x40`, `2`, `1` before this frame's collisions |

* **Other readers.** The only other reader of `0x80000` is the HUD routine `0x00427d50` (`0x004283ab`). It arms a
  1000 game-ms blink timer `[obj+0x124]` and counts it down by `cfg+0x714` only for objects it draws, so it is not a
  general signal. No direct `test` of `0x40` on `[reg+0x44]` exists in `.text`; a register-copy test was not
  searched.
* **No attacker field.** The engine keeps no attacker pointer and no hit time on the victim. The script keeps them
  (2004 `m49`/`m54`).
* **Frame order.** At `0x00403b17` the main loop runs `0x0043a360`: for each sector, collision `0x0045d250`, simulate
  `0x00452ad0`, then `0x0045b720`. At `0x00403f2f` it runs the alert routine (§2). So at `0x0042c974` the bits
  describe the current frame's hits. **[m]** call order; **[i]** that no other pass sets or clears them in between,
  apart from a script-side `SA_SetFlags2` write.

**Record-only hook (diagnostics only; the rule in §9.5 does not need it).**

* **`0x0045e4b5`.** `or dword [esi+0x44],0x80000`, 7 bytes `81 4e 44 00 00 08 00`, next instruction `0x0045e4bc`.
  * Entries: fall-through from `0x0045e4ae` and the rel8 branches `0x0045e481`/`0x0045e488` onto the first byte. The
    raw scan finds no branch into the interior.
  * State: ESI = victim, EBX = projectile, `[esp+0xc]` = attacker or 0, `[ebp+8]`/`[ebp+0xc]` = the pair, time
    `cfg+0x718`.
  * Liveness: the flags written by the `or` are dead (`mov`, `movzx`, `test` follow). ESI is reloaded at
    `0x0045e4bc`, and EAX is rewritten at `0x0045e4bf`. Preserve every other register.
* **Mirror `0x0045e6a6`.** `or dword [ebx+0x44],0x80000` (`81 4b 44 00 00 08 00`), next instruction `0x0045e6ad`.
  Entries: `0x0045e672`/`0x0045e679`. State: EBX = victim, EDI = projectile, `[esp+0x14]` = attacker.
* **Rate and context.** One call per projectile hit, inside the collision pass on the main thread. The synchronous
  `AttackedBy` script call has already returned. The thunk must not call the engine (no re-entry) and must leave the
  x87 state as found.
* **Pointer lifetime.** The death fix-up `0x0044a7c0` (reached from the universe walk `0x0044aab0`, 18 callers) sets
  to null:
  * the bullet owner `+4` (`0x0044a84e`);
  * the missile launcher `+0xc` and target `+0x10` (`0x0044a826`, `0x0044a835`);
  * a ship's `flight+0x224` (`0x0044a895`).

  So a nonzero owner or target is a live object. **[m]** writes, **[i]** that every removal runs the fix-up.

### 9.2 The player's own attacks (Q2) **[m]** unless marked

* **Fire control** is `0x00445170(ship, target, group, flags, mask)`, `ret 0x14`. It is reached only from
  `SA_FireOnObject` (`0xfc`, `0x00461b04`/`0x00461b15`) and `SA_FireAhead` (`0xfd`, target 0, `0x00461b54`/`0x00461b66`).
* **KC census** of the fire flags, 10 sites in each build (Mayhem `07.cat` and stock `04.cat` alike):
  * `606::FireLaser` (the cockpit class) calls `SA_FireOnObject(ship, 0, group, 10 | (…?32:0) | (…?1:0), mask)`, so
    bit `0x2` is always set. KC opcode `0x54` is bitwise OR (handler `0x004a33d6`); `kc_obj.py` does not decode it.
  * `2004::FireLasersOnTarget`, `2004::FireLasersOnDebris` (three sites), `2011::___startDefendThread`,
    `308::DoFireLaser` and `621::HandleEvent` (three sites) all pass **1**.
  * So **bit `0x2` marks a player shot.**
* **Target.** A player shot has target 0. When the cockpit's aim gun maps to the firing group, the fire control
  substitutes `cockpit+0x1e0` (`0x00445255`), but only for aiming. **No projectile stores a target.**
* **Bullet.** Allocated by `0x004e6a40(0x130)` and constructed by `0x0043f900` (`0x00446392`). With flags & 2:
  `M+0x40 += 1` and bullet Flags2 `|= 0x100` (`0x004463c7`, `0x004463ca`). Further writes:
  * `[[b+0x50]+0]` = now + lifetime (`0x00446593`);
  * `[[b+0x50]+4]` = shooter (`0x0044659b`);
  * `[b+0x3c]` = the shooter's owner group (`0x004465a8`).
* **Missile.** `SA_ShootMissile` (`0xff`) calls `0x00448b30`; that is its only caller (`0x00461df0`). It writes:
  * `[[m+0x50]+0xc]` = launcher, `+0x10` = target, `+8` = launch time `cfg+0x718`;
  * `[m+0x3c]` = the launcher's group (`0x00448ced..0x00448d0c`);
  * with flags bit `0x2`: `M+0x44 += 1` and Flags2 `|= 0x100` (`0x00449318`, `0x0044931c`).

  The collision pass gives `0x100` missiles a 2000 ms arming window after launch (`0x0045cc29..0x0045cc4b`). Which KC
  caller passes bit `0x2` for missiles was not traced: the two `2004::ShootMissileSECOBID` sites pass a variable.
* **Counters** at `M = *0x0060850c`, global, the player's statistics:
  * `+0x40` `SA_GetLaserFired`;
  * `+0x44` `SA_GetMissileFired`;
  * `+0x48` `SA_GetLaserHits`;
  * `+0x4c` `SA_GetMissileHits` (`0x00464797..0x004647ca`).
* **The relation test** the alert uses is `0x00450890`. It is the same routine as `SA_IsEnemy` (`0x6b`, `0x004668c9`).
  * **ABI:** a register ABI, EAX = O and **EDI = P** (implicit); it returns AL = 1 when O is hostile to P.
  * **Step 1:** if P is of class 7, 5 or 6 and `[[P+0x50]+0x1d4] != 0` (written only by `SA_SetEnemyOfEnabled`
    through `0x00450950`), O's stance towards P is tested first, and < 0 means hostile.
  * **Step 2:** P's stance towards O, `0x00450810` (ESI = subject, EAX = other, returns −1/0/+1):
    * a missile flagged `0x1000000` whose launcher is not the subject and whose group differs gives −1;
    * subject Flags2 `& 0x40000000` gives 0;
    * a subject class outside 7/5/6 gives 0;
    * an other group `g = (int16)[other+0x3c] < 0` gives 0;
    * the friend mask `[[S+0x50]+0x1cc]` bit g gives +1, else the foe mask `+0x1d0` bit g gives −1.

**Readable without a hook, per frame at `0x0042c974`:**

* **"Fired this frame":** `M+0x40` or `M+0x44` grew. **"Hit something this frame":** `M+0x48` or `M+0x4c` grew; the
  victims are the objects with Flags2 `0x40`.
* **The current target:** `cockpit+0x1e0`. The cockpit is the bounded hash lookup of `[R+0x10]` that
  `chase_fire.cpp::active_cockpit` already does, and R = ESI at the site.
* **"Target hostile and fired within N s":** a stamp of the last frame the fire counters grew, plus the relation rule
  above applied to `cockpit+0x1e0`.
* **Pointer rule:** compare the target pointer with objects found in the thunk's own walk rather than dereference it.
  The death fix-up does not cover cockpits **[i]**.
* **Baseline [i]:** the counters are statistics. Treat a decrease, or the first sample after a load, as a new
  baseline.

### 9.3 Player-owned ships (Q3) **[m]** unless marked

* **Enumeration.** The alert routine walks P's sector (`[P+0x54]`, class 1) through `[sector+0x50] + class·12`
  (`[sector+0x50]` is a pointer to the 12-byte header array, not the array itself: `0x0042c8bf`, `0x004495d8`,
  `0x0045d275` all load it first; Run 190 showed the inline reading finds no node), one
  list per class. The class index is used directly (`SA_StartObjectInSpace`, `0x004495d8..0x004495e2`). Node `+0` is
  next, and a node whose `[node] == 0` ends the list. Every frame it visits all 32 lists, calling `0x00450890` for
  every object and `0x00419280` (Chebyshev distance over `[obj+0x70]+0x30..0x38`) for every hostile.
* **Ownership.** `(int16)[obj+0x3c] == 10`, the player's group ([ship-navigation-ai.md](ship-navigation-ai.md) §10.12
  Item 8). The piloted ship has `[obj+0x40] & 2`. Bullets and missiles carry their shooter's group in `+0x3c` too.
* **Engine fields of a ship S** (`flight = [S+0x50]`):

  | Field | Value | Writer |
  | --- | --- | --- |
  | order word `flight+0x258` | `0x10`/`0x11` | `SA_AttackObject`/`…Big`, target in `flight+0x224` (`0x0046151a`, `0x00461526`) |
  | order word `flight+0x258` | `0x22` | `SA_DefensiveMove`, **attacker** in `flight+0x224` (`0x00461818`, `0x00461824`) |
  | Flags2 `0x80000` | hit this frame | §9.1 |

  * **Not kept:** no last attacker and no time (only the script's `m49`/`m54`).
  * **Turret fire** leaves nothing on the shooter. It shows only as bullets with group 10 and without `0x100` in the
    class-0 list.
  * **Lifetime:** the death fix-up nulls `flight+0x224`.
* **Cost.** The vanilla walk already visits every object of all 32 lists (bullets and asteroids included) with one
  call per object. The rule adds:
  * O(1) reads every frame: `[P+0x44]`, four counters, `cfg+0x718`, the cockpit lookup (≤ 32 hash links);
  * a walk of lists 5, 6, 7 (the classes the relation test admits) and 10 (missiles), only on frames where
    `[R+8]` or `[R+4]` is nonzero. That walk reads about 7 dwords per object: `+0x3c`, `+0x44`, `[+0x50]+0x258`,
    `[+0x50]+0x224`, and `[+0x70]+0x30..0x38` for the range test. For a hostile it adds the relation rule's masks.

  This is a subset of the objects the vanilla walk visits. Object counts per frame were not measured **[i]**.

### 9.4 The clock (Q4) **[m]**

* **Fields** at `cfg = *0x00606f34`:
  * `+0x718` is the absolute game clock in ms (`TI_GetAbsTime`);
  * `+0x714` is this frame's game step, held in 1..`0x7fff`.
* **How it runs with SETA.** The tick `0x004d1df0` advances `+0x718` by `clamp(wall ms, 1, 200)` multiplied by the
  SETA factor `(cfg+0xcc · cfg+0xd0) >> 16`. It runs through the frame clock `0x004ee1e0`, which `0x004b0e00` calls at
  `0x00403af0` on every main-loop iteration, before the sector pass (`0x00403b17`) and the alert routine
  ([engine-effects.md](engine-effects.md) §8). A window measured in `+0x718` therefore scales with SETA.
* **The engine's own windows** on these data use the same clock: bullet expiry, missile launch time, the 2000 ms
  arming window and the 1000 ms HUD blink.
* **Rewrites.** `TI_SetAbsTime` (13) rewrites `+0x718`, so treat a stamp newer than now as expired.

### 9.5 The thunk at `0x0042c974` (Q5)

**Inputs at the site** (in addition to §5):

* ESI = R.
* EDI = P. It is 0 when there is no player ship; the walk's callees `0x00450890`, `0x00450810` and `0x00419280` do not
  change EDI.
* BP = this frame's walk level (`movsx` at `0x0042c969`).
* `[R+8]` = max(walk level, the level 2 raised by missile homing at `0x00459451`), and `[R+4]` = the committed level.
* EAX, ECX, EDX and the flags are dead. EDX is written before it is read on every path, including `0x0049a4d0`,
  `0x0049b050`, `0x0049f4c0` and the caller after return.
* **The triggering object is not kept:** ESI is overwritten at `0x0042c965`, and EBX is the list cursor.
* **Optional trigger capture:** the record-only site `0x0042c93b` `mov ebp,1` (5 bytes, no branch into its interior;
  ESI = candidate O, EAX = its Chebyshev distance, EDI = P; flags dead) yields the vanilla trigger and its distance.
* **Vanilla quirk:** after a missile sets level 2 (`0x0042c94a`) the walk continues with the next list, and an
  in-range hostile in any later list sets EBP back to 1 (`0x0042c93b`). The homing path still supplies 2.

**Rule** (`now = cfg+0x718`; each stamp is a game-ms time):

1. **Every frame with EDI = P in a sector (O(1)):**
   * `[P+0x44] & 0x80000` → `t_attacked`;
   * `[R+8] == 2` → `t_attacked`;
   * `M+0x40`/`M+0x44` grew while `cockpit+0x1e0` is a hostile found by the walk → `t_attack`;
   * `M+0x48`/`M+0x4c` grew and an object with Flags2 `0x40` is in lists 5, 6 or 7 (not an asteroid) → `t_attack`.
2. **When `[R+8]` or `[R+4]` is nonzero, walk lists 5, 6, 7 and 10:**
   * owned S ≠ P within Chebyshev 5,000,000 of P with Flags2 `0x80000`, or with order `0x10`/`0x11` on a hostile
     → `t_owned`;
   * hostile O with order `0x10`/`0x11` whose target is P → `t_attacked`;
   * hostile O with order `0x10`/`0x11` whose target is an owned ship in that range → `t_owned`;
   * record the nearest hostile's distance and the counts for the log.
3. **Filter:**
   * `engaged = now − max(t_attacked, t_attack, t_owned) < H`, where H is in game ms (an ini value; 20,000 as a
     starting point);
   * `[R+8] = engaged ? [R+8] : 0`;
   * replay `mov ecx,[esi+8]; mov eax,[esi+4]` and `jmp 0x0042c97a`.

**Effects.**

* **Entry.** It still needs a hostile within vanilla range: 5 km, or 10 km once the alert is on.
* **Hysteresis.** The committed `[R+4]` becomes the filtered level, so the doubled stay range applies only while
  engaged. The music drops when H runs out or when the hostiles leave 10 km, whichever comes first.
* **Variant: vanilla stay.** A single switch keeps vanilla behaviour once the alert is on: filter only while
  `[R+4] == 0`.
* **Edges.** A fight that starts after a hostile is already in range now produces a 0 → 1 edge, so `NotifyAlert(1)`
  fires on the first engagement event.

**Hook-site facts:**

* six bytes on two whole instructions (§5), reached by the `jle` at `0x0042c96f` onto the first byte and by
  fall-through;
* once per frame on the main thread, with `NotifyAlert` after the site;
* the thunk only reads memory: no engine call, so no re-entry;
* guarded reads for every dereference, x87 state untouched, SSE2 arithmetic.

**First-flight log** (one consolidated diagnostic build):

* **On every change of the filtered committed level:** `music_alert from=<old> to=<new> vanilla=<[R+8] before>
  reason=<p_hit|p_missile|p_fire|p_hit_other|owned_hit|owned_attack|hostile_on_p|hostile_on_owned> age_ms=<now −
  stamp> nearest=<units> hostiles=<n> owned_near=<n> now=<+0x718> step=<+0x714> seta=<factor>`.
* **Once per episode in which vanilla goes 0 → nonzero but the filter holds 0:**
  `music_alert_suppressed vanilla= nearest= hostiles=`.
* **Per alert episode:** the list sizes 5, 6, 7 and 10 walked and the thunk's QPC cost, which settle the cost
  question.

**Not established statically:**

* object counts per frame;
* whether the AI always fights through orders `0x10`/`0x11`; a script may fly order `0x15` and use turrets with no
  attack order, which is then visible only as hits;
* whether player missiles carry bit `0x2`;
* whether turret cockpits manned by the player go through `606::FireLaser` **[i]**;
* friendly fire, which counts as `p_hit`;
* the stale bits in paused frames (`0x00403b10` skips the sector pass);
* the meaning of `+0x1d4` beyond its `SA_` name;
* a suitable H.

**Reproduce:**

```sh
python3 -I verification/results/music-script-driver/verify_engagement_data.py > verification/results/music-script-driver/verify_engagement_output.json   # PASS
i686-w64-mingw32-objdump -d -M intel --start-address=0x45e130 --stop-address=0x45eea5 /mnt/c/X3/X3AP.exe   # hit handler (raw/, untracked)
```
