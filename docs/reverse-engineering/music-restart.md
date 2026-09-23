# Sector music restarts after alt-tab, save and pause

2026-09-23. Static study of X3AP.exe, SHA-256 `fdbf3418d8f0a897…` (the bottle
copy); nothing was launched, no Wine. Every byte, call edge and caller count
below is from `i686-w64-mingw32-objdump` on the file bytes and is pinned by
[`verify_music_restart_sites.py`](../../verification/results/music-restart/verify_music_restart_sites.py)
(`PASS`: 38 byte patterns, 8 jump-table entries, 4 command-name/handler
pairs, direct-caller census, zero literal references, no raw branch into the
interior of the six sites named in §4 except one operand-byte false hit it
rejects; output beside it in `verify_output.json`). Raw disassembly stayed local.
Builds on [media-cue-playback.md](media-cue-playback.md) (record allocator,
id→flags map, pump) and [media-record-lifetime.md](media-record-lifetime.md)
(completion registry, stop-all span, WndProc boundaries). Inferences are marked.

**Question.** User report: the sector music starts again from the beginning
(a) after alt-tab out and back in and (b) after a save; per the user only a
sector change and similar flows may interrupt it. Where does the restart come
from, and where can the proxy prevent it?

**Answer.** Neither is winegstreamer. Both are the same engine sequence:
a **stop-all** (`0x004982b0`) pauses every playing media record, clears its
"playing" bit and fires its completion callback with status **1**, which is
the same status the pump reports when a track ends naturally. The music is
driven by the story script (KC, `l/x3story.obj`) through the `MOV_` native
commands; the script task that was blocked in `MOV_PlayMovie` is released as
if the track had ended and (inference: its loop) issues `MOV_PlayMovie` again,
which **explicitly seeks to 0 ms** (`IMediaPosition::put_CurrentPosition(0.0)`)
and runs the graph. Stop-all is called directly by the WM_ACTIVATE inactive arm
(`0x004d36bd`), as the first statement of the save routine (`0x0040455c`) and
by the pause command `X2_SetPause` (`0x00407064`). Nothing on reactivation,
after the save or on unpause resumes the paused graph. The proxy can prevent
all three with one patch in stop-all and one at the shared seek (§4).

## 1. Music playback state machine

### Music is the `MOV_` media path, not `SFX_*MOD`

* Native command modules are registered as 0x18-byte slots of the script owner
  `*0x006085e4` (+0x30 dispatcher, +0x34 completion, +0x40 name table;
  [media-record-lifetime.md](media-record-lifetime.md#registry-construction-and-writers)).
  `0x00499b30` registers the **MOV** module (dispatcher `0x004997c0`, names at
  `0x0057a344`); `0x00499bc0` registers **SFX** (dispatcher `0x0049c210`, names
  at `0x0057a2a0`, owner `*0x006085dc`).
* `SFX_LoadMOD`/`SFX_PlayMOD`/`SFX_StopMOD` (`0x0049c309`/`0x0049c333`/
  `0x0049c34d`) only store a file name and a requested slot (`owner+0xe8`); the
  only readers of `owner+0xe8` are the clear routine at `0x0049b748`/`0x0049b759`.
  No player consumes it: the MOD path is a vestigial stub.
* The soundtrack path strings `soundtrack\%05d.mp3` / `.wma` (`0x00563b3c`,
  `0x00563b50`) are used only by the media-object constructor `0x004cf460`,
  whose single caller is the record allocator `0x00498140`. The soundtrack
  directory holds `00005.mp3`…`08700.mp3`; the run101 cue trace
  ([media-cues.md](../verification/media-cues.md)) created ids 2004, 8404 and
  8509 with flags `0xd0`/`0x90` from `caller=query`, i.e. `MOV_LoadMovie`.
* **Music-volume class = record flag `0x80`.** `SFX_SetMODVolume`
  (`0x0049c3de` → `0x0049bfe0`) writes the volume to `[*0x00606f34+0x774]` and,
  when `SFX_SetMusicType(3)` has set `*0x00606f40 |= 0x800` (`0x0049c711`),
  re-applies it to every record with `[rec+0x2c] & 0x80` (`0x004985e0`). The
  allocator applies the same volume to new `0x80` records (`0x00498285`).
  Both observed music flag words (`0x90`, `0xd0`) carry `0x80` and `0x10`
  (audio only); `0xd0` also carries `0x40` (the DirectSound-buffer path).

### Entry points (MOV dispatcher `0x004997c0`, jump table `0x00499aec`)

| Command | Handler | Native call | Effect |
| --- | --- | --- | --- |
| `MOV_LoadMovie(id, flags)` | `0x004997dd` | `0x004987a0` → `0x00498730` → `0x00498140` with **EAX = flags** (`0x00498735 push eax; mov eax,[esp+0x10]`) | create + link record (not playing); completion `(ctx,1)` at once |
| `MOV_PlayMovie(id)` | `0x00499803` | `0x00498c90(slot, ctx, id, 0,0,0, -1,-1,-1, 0)` (`0x0049981f`) | **start at 0 ms**; task stays blocked (no `0x004a48d0` result push) |
| `MOV_PlayMovieFrom(id, m,s,ms, m,s,ms)` | `0x00499911` | `0x00498c90` (`0x00499982`) | start at a given time |
| `MOV_StopMovie(id)` | `0x00499875` | `0x00498810` → `0x004d1810` | pause + completion `(ctx,1)` |
| `MOV_FreeMovie(id)` | `0x00499859` | `0x004986f0` → `0x004984d0` | destroy record |

`0x00498c90` (cdecl, ten dwords; `a1` = completion slot index, `a2` = context
= script task key) finds the **first** record with `[rec+0x10] == id`
(creating it through `0x00498cd8` if absent), then on the found path:

| Site | Action |
| --- | --- |
| `0x00498d1d`–`0x00498d4d` | start ms = `(a4*60+a5)*1000+a6` → EDI; end ms from `a7..a9` → EBX |
| `0x00498d53`/`0x00498d54` | `push edi; call 0x004d0430` (EAX = record): **seek** |
| `0x00498d71` | `call 0x004d1870` (EAX = record): **run** |
| `0x00498d83`–`0x00498d8f` | `[rec+0x1c]` = start, `[rec+0x20]` = end, `[rec+0x2c] |= 2 | (a10 != 0)` |
| `0x00498d96`–`0x00498dd5` | previous request's completion `(old ctx, 1)`, then store `a2`/`a1` in `+0x14`/`+0x18` |

`0x004d0430` (EAX = record, one stack arg, cdecl, preserves EBX/ESI/EDI/EBP,
aligns its own frame): DirectSound path (`0x40` set, `8` clear, `[m+0x48] != 1`)
`IDirectSoundBuffer::Stop` (`[m+0x44]` vtbl `+0x48`); `IMediaControl::Pause`
(`[m+0x74]` `+0x20`) when `[m+4]`; `[m+0x64]=1`, `[m+0x90]=start`,
`[m+0x94]=-1`; then `IMediaPosition::put_CurrentPosition(start*0.001)`
(`[m+0x70]` `+0x20`, `0x004d04f4`; constant `0x00565500` = 0.001) — for audio-only
(`0x10`) records a start above 500 ms is reduced by 500 ms first
(`0x004d04bf`–`0x004d04d7`) — or `IAMMultiMediaStream::Seek` when `0x20`.
`0x004d1870` (EAX = record): video sample setup unless `0x10`; for the
DirectSound path `[m+0x48]=[m+0x64]=1`; `IMediaControl::Run` (`+0x1c`,
`0x004d19fa`), two attempts.

### Natural end and the completion status

The pump `0x00498370` (main loop `0x00403a7f`/`0x00403a98`/`0x00403b04`) visits
records with the playing flag `2` and calls `0x004d14e0`. The audio-only arm (`0x004d17d1`)
compares `IMediaPosition::get_CurrentPosition` (`+0x24`) with `get_Duration`
(`+0x1c`) and returns **2** when equal; the DirectSound arm tail-jumps to
`0x004d0700`. On 2 without the loop bit the pump clears flag `2` and calls
completion `(ctx, 1)` (`0x0049845b`). **Stop-all passes the identical status 1**
(`0x0049834b`), so the script cannot tell an interrupted track from a finished one.
The script-slot completion `0x004a4910` wakes the blocked task
([media-record-lifetime.md](media-record-lifetime.md#four-completion-handler-bodies-and-abi));
what the task then plays is decided inside the encoded KC object and is not
decoded here (**inference** from the user report: the same id, from 0).

### Data: current track and position

| Where | Layout |
| --- | --- |
| list `*0x00606f44` | `mov ebp,[list]`; nodes until one with `next == 0` (sentinel) |
| record (0x40 B) | `+0x00` next, `+0x04` prev, `+0x10` **id** (`%05d`), `+0x14` completion context (task key), `+0x18` completion slot index, `+0x1c` requested start ms, `+0x20` end ms, `+0x24` media object, `+0x28` destination, `+0x2c` flags (`1` loop, **`2` playing**, `4` bound, plus media flags `0x10` audio-only, `0x40` DirectSound, `0x80` music volume, `0x100` second volume class), `+0x30` destination slot, `+0x34`–`+0x3c` saved, unidentified |
| media object | `+0x04` IAMMultiMediaStream, `+0x44` IDirectSoundBuffer, `+0x48`/`+0x64` DirectSound streaming state, `+0x6c` IBasicAudio, `+0x70` IMediaPosition, `+0x74` IMediaControl, `+0x8c` media flags, `+0x90` start ms, `+0x94` end ms |

"The current music track" is the record with `[rec+0x2c] & 0x82 == 0x82`.
**No engine field holds the playback position**: it lives only in the graph.
The engine's own reader `0x004d0600` (EAX = record, ESI = `int*` out, ms via
`get_CurrentPosition*1000`, `0x00565550`; or `IAMMultiMediaStream::GetTime`
for `0x20`) writes into a pump local that is discarded (`0x004983e7`).

## 2. Activation path

WndProc `0x004d3620` (stdcall, `ret 0x10`; message in EDI at `0x004d366b`):

| Message | Code | Media effect |
| --- | --- | --- |
| WM_ACTIVATE, `LOWORD(wParam)==WA_INACTIVE` | `0x004d3697` input helper `0x004d4950(1)`; `0x004d36b1` `[esi+0x484]=0`; `0x004d36b7` active `0x00608adc=0`; **`0x004d36bd call 0x004982b0`**; return 0 | stop-all |
| WM_ACTIVATE, active | `0x004d36cd`–`0x004d36e4`: active = 1, `[esi+0x484]=1`; return 0 | **none** |
| WM_ACTIVATEAPP | `0x004d36f5`: only `0x004d4950(wParam==0)` | none |

Stop-all `0x004982b0`–`0x00498366` (no args, preserves EBX/EBP/ESI/EDI): for
each record with flag `2` — `IMediaControl::Pause` (`[m+0x74]` vtbl `+0x20`,
`0x004982f0`, only when `[m+4]`); DirectSound path `IDirectSoundBuffer::Stop`
(`0x00498311`) and `[m+0x48]=[m+0x64]=1`; clear flag `2` (`0x00498328`); completion
`(ctx, 1)` (`0x0049834e`) regardless of the Pause result; clear `+0x18`/`+0x14`.
It pauses, it does not Stop and does not seek.

While inactive the loop does not run: the pump `0x004d34b0` tests `0x00608adc`
(`0x004d34bc`) and, unless dev flag `[*0x00606f3c] & 0x4000`, sits in a
blocking `GetMessageA` loop. (With that dev flag the loop would run and
`0x004d14e0` reports **2 = ended** for every record while inactive,
`0x004d14eb`→`0x004d15a0`.) After WM_ACTIVATE active the woken script task
runs and calls `MOV_PlayMovie` → `0x00498d54` → `put_CurrentPosition(0.0)` +
Run. **The restart is an explicit engine seek to 0, not a winegstreamer
seek-to-zero on resume**; no engine code resumes the paused graph.

## 3. Save path

Main loop `0x00403dc5`: save request `[*0x0057fc60+0x4a0] & 4` → script event
`BeforeSave` (`0x00403df0 call 0x0049f570`, string `0x00555718`) → clear the
request (`0x00403e07`) → **save routine `0x00404530`** (`0x00403e18`, sole
caller) → events `SaveFinished2`/`SaveFinished` (`0x00403ec1`/`0x00403eed`).
`0x00404530` calls stop-all as its first statement (`0x0040455c`), then writes
the file; its `MOVI` chunk writer `0x004988d0` (`0x00404a0b`) stores per record
0x28 bytes: `+0x10,+0x14,+0x18,+0x1c,+0x20,+0x2c,+0x30,+0x34,+0x38,+0x3c` (no
position). No call after the save resumes media. So the restart after a save
is the same explicit sequence as §2, triggered by an explicit call, not by a
sector-change selector.

Why the save stops everything (**inference** from the byte order): stop-all
runs before the MOVI writer and the script state are written, so no saved
record carries a live completion context and no saved script task is waiting
on a media completion. The MOVI restore `0x00498ad0` (`0x004050a2`) recreates
records by id (ids 101–299 skipped, `0x00498b7e`) with EAX = saved flags
`& ~2` (`0x00498b99`), rebinds `+0x30`, and **ignores the saved `+0x14`/`+0x18`**;
it never starts playback.

The `Videos`/`VideoLists` selector of `0x0045b720` (kind `0x5a`) is not the
music: `addon\types\Videos` rows are four ids, a movie id (1 in the rows
inspected) and a start/end time into it (comm and advert clips), `VideoLists` names advert,
product-loop and news lists. It drives screen textures.

## 4. Every trigger, the choke point, and the hooks

Requirement (user, 2026-09-23): only a sector change and similar flows
(another sector's track, docking/undocking if the script switches tracks,
game start, load) may interrupt the track; alt-tab, save, menus, pause and
everything else must leave it playing at its position.

### Triggers

| Trigger | Native entry | Stop-all return address | Policy |
| --- | --- | --- | --- |
| Alt-tab (WM_ACTIVATE inactive) | `0x004d36bd` | `0x004d36c2` | preserve |
| Save (`X2_Save` sets `+0x4a0 |= 4`, `0x00406e35`; routine `0x00404530`) | `0x0040455c` | `0x00404561` | preserve |
| Pause (`X2_SetPause`, main-module command 9, `0x0040705d`: `+0x4a0 |= 1` gates the universe update at `0x00403b09`; unpause `0x004043dd`) | `0x00407064` | `0x00407069` | preserve |
| Load (`0x00404cc0`) | `0x00404ced` | `0x00404cf2` | vanilla |
| `P_Leave` (`0x00497ba8`, location switch/quit, after `0x00401dd0`) | `0x00497bb6` | `0x00497bbb` | vanilla |
| Main-loop entry `0x00403840` (session start) | `0x00403878` | `0x0040387d` | vanilla |
| Script `MOV_StopMovie` | `0x00499881` → `0x00498810` | — | vanilla (script's own track decision) |
| Script `MOV_PlayMovie` of another id (sector change; docking if scripted) | `0x0049981f` → `0x00498c90`, other record | — | vanilla |
| Script `MOV_PlayMovie` of the id already held or playing | `0x00498c90` found path | — | **choke point** |

These six are the complete direct caller set of stop-all (raw E8/E9 scan, no
literal references). Pause runs scripts and the media pump every iteration
(`0x00403aff`, `0x00403b04` precede the pause test), so in vanilla the woken
script restarts the track while paused (**inference**). Menus: the only native
pause entry is `X2_SetPause`; whether a menu also calls `MOV_StopMovie` is
script behaviour and not decoded.

### Shared choke point and the one extra patch

Every restart of an existing track, whatever stopped it, arrives as the script's
`MOV_PlayMovie` → `0x00498c90` → **`0x00498d54 call 0x004d0430`** (the seek);
stop-all itself only pauses. So alt-tab, save, pause and a same-id replay while
still playing all share one site. The stop-all needs its own patch, because it
is what pauses the graph and wakes the script; one patch inside stop-all covers
all six callers by return address.

**Patch A — stop-all classifier, `0x004982db`, 6 bytes `8b 7e 24 39 5f 04`**
(`mov edi,[esi+0x24]` / `cmp [edi+4],ebx`, two whole instructions; entered only
by fall-through from `0x004982d9`; no branch into its interior; continuation
`0x004982e1 je` consumes the CMP flags, so the tail replays both). State at
entry: ESI = playing record, EBP = cached next node, EBX = 0, EDI about to be
loaded, EAX/ECX/EDX and flags dead; the stop-all return address is at
`[esp+0x10]` (four pushes since entry). This is the span already reviewed as the
"stop-all dispatch candidate" in
[media-record-lifetime.md](media-record-lifetime.md#owned-rate-and-stop-all-dispatch-candidates-2026-09-20).
Stub: if `[esi+0x2c] & 0x80` (music) and the return address is a preserve
caller, record a hold `(record, [rec+0x10], [rec+0x24], mode)`;

* save and pause, mode **keep-running**: jump to `0x00498322`, skipping only
  `IMediaControl::Pause` and the DirectSound `Stop`. The vanilla bookkeeping
  (flag `2` cleared, completion `(ctx,1)`, `+0x14`/`+0x18` cleared) still runs, so
  the MOVI chunk is what vanilla writes and the script is woken exactly as
  before; the stub leaves by `jmp 0x00498322` with ESI/EBP/EBX/ESP as at entry;
* alt-tab, mode **paused**: fall through to the vanilla Pause (the loop is
  frozen, so a running DirectSound-path track would starve — **inference**);
* a vanilla caller (load, `P_Leave`, session start) drops all holds.

**Patch C — the choke point, `claim_call` at `0x00498d54`**
(`e8 d7 76 03 00` → `0x004d0430`; no branch into its interior). In: EAX =
record (= ESI), `[esp+4]` = start ms (= EDI), EBX = end ms; the caller pops 4 and
tests EAX; ECX/EDX/EBP dead after; the callee contract preserves
EBX/ESI/EDI/EBP. Asm thunk rule:

* music record (`& 0x80`), requested start 0 (`MOV_PlayMovie`), and either a
  hold for this `(record, id, media)` or flag `2` still set (the requested track is
  the one playing): **return 1 without seeking**. The following `Run` at
  `0x00498d71` resumes an alt-tab-paused graph from where it stopped and is a
  no-op on a running DirectShow graph; for the DirectSound path `0x004d1870`
  re-arms `[m+0x48]`/`[m+0x64]` (effect unverified). The success path then
  re-sets flag `2` and stores the new completion context, so the script waits on
  the track's natural end as before;
* a music play of a **different** id while a keep-running hold exists: first
  pause the held record with the engine's `0x004d1810` (EAX = record), else two
  tracks overlap; then fall through to the original seek;
* everything else, including `MOV_PlayMovieFrom` with a nonzero start and every
  non-music record: original `0x004d0430`. Holds are dropped after the first
  music play.

A natural end is unaffected: the pump clears flag `2` before its completion, no
hold exists, the replay seeks to 0. Sector change, load and game start keep
vanilla behaviour through the "different id" and "vanilla caller" rules.
The hold replaces an `IMediaControl::GetState` query (the engine never calls
one); Patches A and C add no COM call except the conditional `0x004d1810`.

Threads and reentrancy: stop-all's completion handlers do not run the VM;
Patch C runs inside the script step (`0x0049f770`), Patch A inside the WndProc
or the main loop; the WndProc is dispatched by the pump `0x004d34b0` on the main
thread, so the hold table is single-threaded (**inference**; the window thread
was not measured). Both stubs must preserve x87/MXCSR state and the
callee-saved registers; LastError is not read by the surrounding code.

Risks: depends on the script replaying the same id after the status-1 wake
(unverified). If it plays another id, the different-id rule gives vanilla
behaviour; if it plays nothing, a keep-running track plays to its end with
flag `2` clear and no completion (silence afterwards until the script acts).

### Rejected shapes for the brief's candidates

* **Early return in the activation handler** (NOP `0x004d36bd`, or keeping
  music out of the alt-tab stop-all and resuming at `0x004d36da`,
  `b8 01 00 00 00`): alt-tab only, needs a second patch for save and pause.
  Skipping the stop-all around a save would also write a live completion
  context into MOVI and a script task still blocked on a completion that the
  restore never reattaches (**inference**: probable music stall after loading
  that save).
* **Media-object intercept**: the proxy does not own the music graphs (quartz
  /amstream objects from `0x004cf460`; the owned objects are the ID2 video
  path). The completion at `0x0049834e` fires whether Pause succeeds or not, and
  the restart arrives as `put_CurrentPosition(0.0)` + Run, indistinguishable at
  the COM level from a legitimate start without Patch A's mark.

## 5. Not established

* What the story script does after status 1 (same id vs. next track, delay),
  whether menus call `MOV_StopMovie`, and what the `BeforeSave`/`SaveFinished`
  handlers do: the story object is encoded. A trace of `0x00498c90` (id,
  start, return address) and of the stop-all return address across one alt-tab,
  one save and one pause would settle it; the existing `media_cue` trace only
  sees record creation.
* Behaviour of the DirectSound path (`0x004d0700`) when unpumped or re-armed by
  `0x004d1870` while running.
* Thread identity of WndProc vs. main loop was not measured.
* The proxy has no WndProc or WM_ACTIVATE hook today (`src/proxy` only polls
  foreground/focus in telemetry); both patches are new engine patches.

## Reproduce

```sh
python3 verification/results/music-restart/verify_music_restart_sites.py
i686-w64-mingw32-objdump -d --start-address=0x4982b0 --stop-address=0x498367 "$X3AP"  # stop-all
i686-w64-mingw32-objdump -d --start-address=0x4d3620 --stop-address=0x4d3790 "$X3AP"  # WndProc head
i686-w64-mingw32-objdump -d --start-address=0x498c90 --stop-address=0x498e28 "$X3AP"  # play
i686-w64-mingw32-objdump -d --start-address=0x404530 --stop-address=0x404570 "$X3AP"  # save entry
```
