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

**Loop behaviour while inactive (corrected 2026-09-23, Run 72 B).** The code
path exists: the message pump `0x004d34b0` (called by the main loop at
`0x00403a89`/`0x00403af5`, before the media manager `0x00498370`) tests
`0x00608adc` (`0x004d34bc`) and, unless the RunInBackground bit `0x4000` of
the input flags word `[*0x00606f3c]` is set, loops on a blocking `GetMessageA`
until the flag is non-zero again (only the WM_ACTIVATE active arm `0x004d36df`
and two init sites write 1). The pump's non-blocking arm (`0x004d357c`,
entered while the flag is 1) drains every pending message, WM_ACTIVATE
inactive included, and returns at `0x004d360b` without re-testing the flag, so
the rest of that frame (script step, media update `0x00498370`) runs with the
flag at 0; the next pump call blocks. But the flight
contradicts "the loop sits in GetMessage": in run271 (CrossOver, measured),
for 10 of the 11 alt-tab stop-alls the first frame after the stop ended
1.4–3.3 ms later and the script's replay followed by 1.7–6.4 ms (the eleventh,
seq 23, fell inside a game pause: 946 ms to the next frame, 950 ms to the
replay), and 146 frames presented while the proxy's
window sample said not foreground (10 of 24 samples). So in that flight the
engine flag was back at 1 almost at once while the window stayed in the
background (**inference**: the driver delivered WM_ACTIVATE active right after
the inactive one; not traced), and the main loop, the script and the media pump
kept running through the alt-tab. On a system where the flag stays 0 the loop
does block (static reading; native Windows unverified). The status query
`0x004d14e0` depends on the same two values: with the flag at 0 and the
RunInBackground bit **clear** it answers **2 = ended** for every record it is
asked about without looking (`0x004d14eb`→`0x004d1500`→`0x004d15a0`); with the
bit **set** the loop keeps running while inactive and the query answers
normally. (Where the bit comes from, and the DLL's `run_in_background` setting that
sets it as `-runinbg` does: [run-in-background.md](run-in-background.md), 2026-09-27.) The bottle X3 runs with the bit clear (InputFlags `0x70200107`, as
reported by the reviewer; not re-read here), so the false "ended" applies to
the frame that follows an alt-tab (§6 "Alt-tab", Patch D). Vanilla: the stop-all's status 1 wakes the
script task, which calls `MOV_PlayMovie` → `0x00498d54` →
`put_CurrentPosition(0.0)` + Run. **The restart is an explicit engine seek to
0, not a winegstreamer seek-to-zero on resume**; no engine code resumes the
paused graph.

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
  frozen, so a running DirectSound-path track would starve — **inference**,
  contradicted by Run 72 B: the loop keeps running; superseded by **skip_all**,
  §6 "Alt-tab");
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
  no-op on a running DirectShow graph; for the DirectSound path the pump
  re-arms the stopped buffer from `[m+0x48]`/`[m+0x64]` (Run 72 B: a ~1 s gap,
  §6 "Alt-tab"). The success path then
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
behaviour; if it plays nothing, a keep-running track plays on with flag `2`
clear, unserviced by the manager (§6, "Orphans"), until the next stop-all,
where the implementation pauses it (review fix F2, 2026-09-23), or its own
end (no completion, silence afterwards until the script acts). A
DirectSound-path record (`0x40`) is never kept running: the pump services
flag-2 records only and the save's file write blocks the loop, so its buffer
would starve (F1).

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
* Behaviour of the DirectSound path (`0x004d0700`) when unpumped (its 5 s ring
  keeps looping) and on native Windows while the window is in the background
  (the buffer is created without `DSBCAPS_GLOBALFOCUS`, flags `0x10088`). The
  re-arm after a stop-all is resolved in §6, "Alt-tab".
* Thread identity of WndProc vs. main loop was not measured.
* The proxy has no WndProc or WM_ACTIVATE hook today (`src/proxy` only polls
  foreground/focus in telemetry); both patches are new engine patches.

## 6. Implementation (2026-09-23)

`src/proxy/music_keep.cpp` / `music_keep.h` / `music_keep_core.h`; launcher
`--music-keep` (`X3M_MUSIC_KEEP=1`, **off by default this round**: the
same-id replay of §5 is unverified) and `--music-trace` (`X3M_MUSIC_TRACE=1`,
opt-in), neither forwarded under `--vanilla`; ledger
[music-keep.md](../verification/music-keep.md); byte check
[`verify_music_keep_writes.py`](../../verification/results/music-restart/verify_music_keep_writes.py)
(reads every window and constant from the core header, models the writes,
raw-scans the site interiors, repeats the caller census, pins the skip_all
target as the loop's continue and Patch D's call site, query head and branch
targets; `PASS`, 48 checks).
Both features install on the backend-load path inside the `engine_patch`
window (after `collide_memo`), only when the variable is exactly `1`, the
executable hash verifies and every window below matches; any later claim is
refused `late_claim`; a failed second claim puts the first back
(`rollback_failed` keeps the module registered so `shutdown()` retries);
LastError is preserved by install, Present and the handlers; the DLL is
pinned once a site is live. The stubs are integer-only assembly (no x87, no
SSE); the handlers run under `LightCallBoundary` and are roots of
`check_no_x87.py` (661 reachable functions, PASS); the line formatter runs
behind `call_preserved`.

**Shared entry sites, either order (2026-09-23, after Run 72 B).** In run271
the trace was refused `bytes_mismatch` because the keep installed first and
the trace compared the two shared heads (`0x004982b0`, `0x00498810`) against
the image while they already held the keep's `e9 rel32`. Every window check
now goes through `core::window_matches`: a read overlapping a live shared
claim is overlaid with that claim's original bytes, but only when the five
bytes there are still exactly the claim's own patch (a foreign byte fails the
comparison, fail closed). The claim protocol itself is `core::acquire_shared`
/ `release_shared` (first user claims, every user chains its stub in front of
the current head, a failed push on a fresh claim restores it, the last release
restores the bytes, and a live claim left without users by a failed rollback is
refused `site_live` rather than reset; rollback and shutdown release a shared
site only for the feature that acquired it, or restore such an orphaned live
claim, whatever install stage was reached); the host test drives it over a fake image in both install
orders, both release orders, a foreign write, a changed byte behind the claim
and a failed push. Chain order follows install order (keep first: trace stub →
keep stub → tail); every stub restores EFLAGS/EAX/ECX/EDX and ESP, so the order
does not matter.

### Sites and bytes

| Patch | Site | Displaced / redirected bytes | Written | Continuation |
| --- | --- | --- | --- | --- |
| A (keep) | `0x004982db` | `8b 7e 24 39 5f 04` (`mov edi,[esi+0x24]; cmp [edi+4],ebx`) | `e9 rel32` to the claim dispatcher, byte 6 (`04`) untouched | tail = the six bytes + `jmp 0x004982e1` (vanilla, paused); or `mov edi,[esi+0x24]; jmp 0x00498322` (keep_running); or `jmp 0x00498359` (skip_all: the loop's continue; EDI dead there) |
| C (keep) | `0x00498d54` | `e8 d7 76 03 00` (`call 0x004d0430`) | `e8 rel32` to the thunk | `jmp 0x004d0430` with the same stack (vanilla), `mov eax,1; ret` (skip), or `call 0x004d1810` (EAX = held record) then the vanilla seek |
| D (keep, optional) | `0x004983d9` | `e8 02 91 03 00` (`call 0x004d14e0`, the status query's only caller) | `e8 rel32` to the thunk (atomic: `& 7 = 1`) | `jmp 0x004d14e0` with EAX/ECX/EDX/EFLAGS restored (forward), or `mov eax,1; ret` (playing, the skip_all gate) |
| T1 (trace; also the keep's entry stub) | `0x004982b0` | `a1 44 6f 60 00` (`mov eax,[0x606f44]`) | `e9 rel32` | tail = the five bytes + `jmp 0x004982b5`; shared site: claimed once, the keep's orphan stub and the trace stub chain in front of each other |
| T2 (trace) | `0x00498c90` | `51 53 8b 5c 24 14` (`push ecx; push ebx; mov ebx,[esp+0x14]`) | `e9 rel32`, byte 6 (`14`) untouched | tail = the six bytes + `jmp 0x00498c96` (the ESP-relative load replays at the entry ESP: the stub restores it exactly) |
| T3 (trace; also the keep's stop-movie stub) | `0x00498810` | `8b 0d 44 6f 60 00` (`mov ecx,[0x606f44]`) | `e9 rel32`, byte 6 (`00`) untouched | tail = the six bytes + `jmp 0x00498816`; shared site like T1 |

Write kinds (`engine_patch`): A (`0x004982db & 7 = 3`, five bytes inside one
qword) and the three entry sites (`& 7 = 0`) are written with one
`lock cmpxchg8b` (`write_a=atomic`, `write_entry=atomic`, …); C is a plain
five-byte copy, because `0x00498d54 & 7 = 4` puts the call across a qword
boundary (`write_c=plain`), and relies on the install window alone (no other
thread executes engine code on the backend-load path). The manager's loop
re-seek at `0x0049840a` is the second caller of `0x004d0430` (the third is
`0x00498f55`); both stay deliberately vanilla, only the play routine's call at
`0x00498d54` is redirected.

Windows compared before any write (all from the core header, all pinned by
the verifier): `0x004982cc` (26 B around A), `0x00498322` (15 B, the
bookkeeping label), `0x00498359` (14 B, the loop's continue `cmp [ebp],ebx;
jne 0x004982d0` and the epilogue; the verifier also checks that the loop's own
`je` at `0x004982d9` lands on it and its `jne` on the loop head), `0x00498d4d` (7 B), `0x00498d59` (29 B through the run
call, whose target `0x004d1870` is checked), `0x00498d8c` (6 B, flag `2`
set), `0x004d0430` (26 B head), `0x004d1810` (whole 87 B body: it clobbers
EAX/ECX/EDX only), `0x004d1870` (14 B head), the three trace heads (16/18/14
B), the six stop-all callers, the three play callers and the three
`0x00498810` callers (E8 target and return address). Raw scan: no rel8/rel32
branch into any site interior (`0x00498c6f` is the ModRM byte of
`lea edi,[esp+0x18]`, rejected as an operand-byte hit like `0x00498d4e` in
the site verifier). New census fact: `0x00498810` has three direct callers,
`0x00499881` (`MOV_StopMovie`), `0x0045c27d` (inside the selector
`0x0045b720`) and `0x004f66be`.

### Decisions

* Stop-all (A), per playing record: caller by return address (`alt_tab`
  `0x004d36c2` → skip_all; `save` `0x00404561`, `pause` `0x00407069` →
  keep_running; `load` `0x00404cf2`, `p_leave` `0x00497bbb`,
  `session_start` `0x0040387d` and any unknown address → vanilla, and every
  hold is dropped). A non-music record (`[rec+0x2c] & 0x80` clear) is never
  held and takes the vanilla path under every caller. A DirectSound-path music
  record (`0x40`) is paused, never kept running, under save and pause (F1).
  Alt-tab, music record (either path): **skip_all**, the whole per-record stop
  is skipped (no Pause/Stop, flag `2` and `+0x14`/`+0x18` kept, no completion),
  no hold is taken and other holds are left alone (see "Alt-tab" below). A held record is `(record, id, media, mode)` in a four-slot
  table (oldest replaced). The classifier's `[esp+0x10]` depth is pinned by
  the 38-byte window from `0x004982c0` (`push ebp` … `push esi; push edi`)
  plus `push ebx` in the 16-byte head (F5).
* Stop-all entry (shared site, any caller, before the record loop): every
  keep_running hold that is still linked with the same id and media, flag `2`
  clear, context 0 and music is paused with `0x004d1810` and dropped
  (`music_keep_orphan`); a keep_running hold that is unlinked, playing again
  or claimed by a task is dropped without a pause; paused-mode holds stay
  (F2). So a track the script never replayed cannot play on through an
  alt-tab or a load.
* `MOV_StopMovie` native entry (shared site, EAX = id): the id's holds are
  dropped (`music_keep_stop_movie`); the engine pauses the record itself, and
  the replay that follows a script stop seeks to 0 as vanilla does (F4).
* Seek (C), when `0x00498c90` found the record and is about to seek: non-music
  → vanilla (no log). Music, start 0, and a hold for `(record, id, media)` or
  flag `2` still set → **skip** (return 1; the Run at `0x00498d71` resumes or
  is a no-op; flag `2` and the new completion context are stored as vanilla).
  Otherwise, a keep_running hold whose record is still linked with the same
  id and media, flag `2` clear, `+0x14` context 0 (not claimed by a script
  task again) and music → `0x004d1810` on it, then the vanilla seek
  (`pause_then_vanilla`); else vanilla. Every music play drops the holds.

### Orphans: a keep_running track is unserviced until the script replays it

The manager update `0x00498370` visits only records with flag `2` (§1,
"Natural end"): after a save or
pause the keep_running record has flag `2` clear (the vanilla bookkeeping at
`0x00498322` still runs), so until the script replays the same id the track
gets no end-of-track poll (`0x004d14e0`), no completion and no loop re-seek
(`0x0049840a`). In the expected flow the replay arrives on the next script
step and re-sets flag `2`, so nothing is missed; if the script does not
replay, the track plays on with the graph running and unserviced until its
own end or the next stop-all (where F2 pauses it). This is the **second thing
the trace flight must settle**: a `music_trace_stop name=save|pause` with no
following same-id `music_trace_play` means the keep leaves an orphan. Leaving
flag `2` set instead (skipping the `and [esi+0x2c],~2` as well) would keep the
manager servicing the track, but the stop-all's completion `(ctx,1)` and the
clearing of `+0x14`/`+0x18` still run, so the natural end would fire a second
completion with context 0 into the script slot (`0x0049845b`, callee
`0x004a4910`, ctx 0 undecoded) and the MOVI chunk would carry flag `2`
(masked on restore, `0x00498b99`, so harmless). The note's invariant that
"the script is woken exactly as before and the MOVI chunk is what vanilla
writes" is what the keep relies on, and the ctx-0 completion is unmodelled,
so the behaviour stays: flag `2` cleared, orphan paused at the next stop-all.

### Alt-tab: the DirectSound re-arm and skip_all (after Run 72 B)

Run 72 B (run271, measured): the sector music records were on the DirectSound
path (`flags 0xd2`), so every caller got `mode=paused`; each of the 18 stops
was followed by a same-id replay whose seek was skipped and the position was
kept, but alt-tab left a ~1 s audible gap. Mechanism (from the disassembly,
the pump arm confirmed at `0x004d070e`–`0x004d0735`): the stop-all's
DirectSound arm calls `IDirectSoundBuffer::Stop` and writes `[m+0x48]` =
`[m+0x64]` = 1 (`0x0049831c`/`0x0049831f`). When the replay's Run sets flag `2`
again, the pump `0x004d0700` sees `[m+0x48]==1 && [m+0x64]==1`, stops the
buffer, zeroes both cursors (`[m+0x4c]`, `[m+0x50]`), calls
`SetCurrentPosition(0)` and starts an asynchronous `IStreamSample::Update`;
`Play` follows only after the next 2 s sample (`[m+0x60]`) has decoded into the
5 s ring. Because the loop keeps running through an alt-tab (§2), the gap is
that re-arm, not the time spent in the background.

Rule since then: for the `alt_tab` caller a music record takes **skip_all**:
the A stub restores EFLAGS/EAX/ECX/EDX and jumps to `0x00498359`, the
per-record loop's continue (the displaced `mov edi,[esi+0x24]` is not replayed:
EDI is dead there). That address is the target
of the loop's own `je 0x00498359` at `0x004982d9` for a record without flag `2`,
so the state there is exactly that branch's: EBP already holds the next node
(`0x004982d6` runs before the site), EBX = 0, the stack four pushes deep; ESI,
EDI, EAX/ECX/EDX and the flags are rewritten before any read (the loop head
reloads ESI, the site reloads EDI, the epilogue pops EDI/ESI/EBP/EBX). Nothing
of the record is written: no Pause/Stop, no
`[m+0x48]`/`[m+0x64]`, flag `2` stays, no completion `(ctx,1)`, `+0x14`/`+0x18`
stay. The script is not woken, so there is no replay. While the window is active the pump
keeps servicing the record; on a gated frame (Patch D answering "playing" with the active
flag at 0) the update takes the position-read path 0x004d0600 and the DirectSound pump is not
reached, so the record is left unpumped for that frame, as vanilla leaves it too (the flight
measures how many such frames occur); its natural end fires the one completion with the real context
(unlike the rejected "flag 2 kept, completion sent" variant above). The record
is held with the **skip_all** kind, read only by Patch D below; the seek rule
ignores it (flag `2` alone decides), so the next start-0 play of the same record
after its natural end still seeks to 0, and every music play drops it.
Non-music records (speech, video) stay vanilla under alt-tab. Save and pause
keep the flown behaviour (DirectSound: paused, position kept through the
replay; the loop blocks there, so a running buffer would starve).

**The false "ended" and Patch D (second review).** Because the record keeps
flag `2`, the media update asks the status query about it. In the frame that
follows the alt-tab the flag `0x00608adc` is 0 (§2: the pump's non-blocking arm
returned without re-testing), and with the RunInBackground bit clear (this
bottle) the query answers 2 without looking. The update would then end the
record (`0x0049842b`: flag `2` cleared, completion `(ctx,1)`; a looped record
re-seeks to its start at `0x0049840a`), the script would replay at 0, and Patch
C, finding neither flag `2` nor a matching hold, would seek to 0: a restart,
worse than mode paused. Patch D redirects the update's call of the query
(`0x004983d9` → `0x004d14e0`, its only caller; `e8 rel32`, five bytes inside
one qword, atomic write) to a thunk. For a music record still flagged playing
that holds a skip_all hold, while the flag is 0 and the RunInBackground bit is
clear, the thunk answers 1 (playing: the caller reads the position into a
discarded local at `0x004983ed` and moves on); every other call is forwarded
unchanged, including the whole bit-set case, where the engine's answer is
genuine. Contract: ECX = record, EAX = `[rec+0x28]`, no stack argument, `ret`;
the caller reads AX only (`movzx eax,ax`), and the callee preserves
EBX/EBP/ESI/EDI, which the thunk does not touch. The common path (engine
active) costs three loads in the handler and no call. Windows compared before
the claim: `0x004983d4` (41 B, the caller's `cmp ax,1`/`cmp ax,2` contract),
the query head `0x004d14e0` (38 B, through the inactive test) and its "ended"
block `0x004d15a0` (12 B); the verifier also checks the branch targets and
that the call is the query's only caller. Patch D is optional: if its windows
or claim fail, the keep installs without it, alt-tab falls back to mode paused
and the install line says `status_gate=<reason> alt_tab_mode=paused`. With the
keep armed, Present samples the flag once per frame and writes
`music_keep_active … from= to= run_in_background= gated=` on each change, so the
next flight measures how long the flag stays 0 around an alt-tab.

Limits: while the flag stays 0 and the loop keeps running (the bit set) the
query answers normally and the pump services the record. Where the engine flag stays 0 through the alt-tab (the static path of
§2; native Windows unverified), the main loop blocks in `GetMessageA` and the
DirectSound buffer, left playing, loops its 5 s ring unserviced until
reactivation; the buffer lacks `DSBCAPS_GLOBALFOCUS`, so whether that is
audible in the background there is untested. A DirectShow-path music record
(`0x40` clear) under skip_all keeps playing audibly in the background on every
system, because its graph runs on its own threads whatever the main loop does,
where vanilla pauses it and goes silent (unflown; the flown tracks were
DirectSound). Portability gap recorded in
[platform-portability.md](../architecture/platform-portability.md).

### Lines and how to read a flight

Written synchronously through the session log's OS handle (they may precede
buffered lines written earlier; sort by `seq=`, not by file order). `frame=`
is the last Present's counter (frozen while inactive), `qpc=` the clock.
Cap: 1,000 event lines per session, then one `music_trace_cap` line.

* `music_trace_stop frame= seq= qpc= caller=0x… name=alt_tab|save|pause|load|p_leave|session_start|unknown`
* `music_trace_play frame= seq= qpc= id= start_ms= caller=0x… name=MOV_PlayMovie|MOV_PlayMovieFrom|helper_0x004f6640|unknown record=0x… flags=0x…`
  (`record`/`flags` from a bounded walk of the list at entry: `flags & 2`
  = still registered as playing, `& 0x80` = music class)
* `music_trace_stop_movie frame= seq= qpc= id= caller=0x… name=… record=0x… flags=0x…`
* trace fields for the track-queue design (music-script-driver.md §8.2, §8.3 "Not established" (a)); reads through the
  fault-free engine reader, no behaviour change, only with `music_trace = 1`:
  * `music_trace_stop_movie` adds `ctx= slot=` (the record's `+0x14`/`+0x18`), `task=` (ESI at `0x00498810`),
    `task_ok=`, `task_id=` (`[task+8]`), `arg_task=` (`[esp+4]`, the native's task id), `ip=` (`[task+0x1c]`; `0xdb54`
    for `PlayMusicWithFade`'s stop), `k=` (`[task+0x18]`), `capacity=` (`[task+0x10]`), `cells=ok|invalid` and the raw
    cells `c6= c8= c10= c12=` as `tag:dword` (cell `i` at `[task+0x14] + 5·(k+i)`, read only when the base is nonzero and
    `k + 12 < capacity`), and `alert= alert_old= alert_new=` (`[R+4]`, `[R+8]`, `R = *0x00608504`);
  * `music_trace_play` adds `ctx= slot=`, `task= task_ok= task_id= ip=` (`0xdbf7` for `MOV_PlayMovie` from the driver)
    and `a1_slot= a2_task=` (the play routine's first two arguments);
  * the pass order: the existing `music_trace_stop frame= seq= qpc=` at the stop-all entry (T1) shares `seq=` with
    every other row, so stop-all, stop-movie and play order within and across frames reads from `seq` and `frame`.
  Expected for the design: a sector change shows `stop_movie ip=0xdb54 cells=ok c6=3:0x0000dc84 c8=1:<new id>
  c10=1:0x00000001 c12=3:0x0000dcdb alert_old=0 alert_new=0`, then `play id=<new> ip=0xdbf7 a2_task=<arg_task of the
  stop>`; a combat start the same stop with `alert_new` ≠ 0 and `c8` in 1001..1042; a dock `c8` in 2001..2004.
* with the keep on: `music_keep_stop … name= record= id= flags= mode=skip_all|keep_running|paused|vanilla held= holds= pump=`,
  `music_keep_seek … record= id= start_ms= flags= action=skip|vanilla|pause_then_vanilla hold_id= pause_record= holds_before=`,
  `music_keep_orphan … caller= name= record= id= action=pause holds=` (an unreplayed keep_running track paused at a
  stop-all: the F2 path, and evidence of the orphan case above), `music_keep_stop_movie … id= caller= name= dropped= holds=`,
  and `music_walk_cut … walk=find_record|live_record limit=4096 cuts=` when a record-list walk hit its bound (never expected),
  `music_keep_status … record= id= flags= input_flags= action=playing gated=` (Patch D answered for a skip_all record while
  the engine was inactive) and `music_keep_active frame= … from=unsampled|0|1 to=0|1 run_in_background= gated=` (the engine's
  active flag changed between two Presents; frames between the 1→0 and 0→1 lines are the time spent inactive with the loop running)
* install: `music_keep requested= patched= reason=ok|… site_a=0x004982db site_c=0x00498d54 write_a= write_c= … status_gate=ok|<refusal>
  site_d=0x004983d9 write_d= thunk_d= alt_tab_mode=skip_all|paused … skip=0x00498322 next_record=0x00498359 …`,
  `music_trace requested= patched= reason= site_stop_all= site_play= site_stop_movie= … cap=1000`
  (both `reason=ok` when both variables are set).

Expected after an alt-tab out and back, keep off: `music_trace_stop name=alt_tab`,
then `music_trace_play id=<same id> start_ms=0 name=MOV_PlayMovie
flags=0x90|0xd0` (flag `2` clear: the stop-all cleared it). With the keep on:
`music_trace_stop name=alt_tab`, `music_keep_stop name=alt_tab mode=skip_all
held=1`, possibly `music_keep_active to=0` and `music_keep_status action=playing`
(the frame after the alt-tab ran with the engine inactive), and **no**
following `music_trace_play`/`music_keep_seek` for that id (the script was not
woken). With `alt_tab_mode=paused` (Patch D refused) the flown sequence of Run 72 B. Save and pause: the stop, `music_keep_stop
name=save|pause mode=keep_running` (DirectShow) or `mode=paused` (DirectSound,
`flags & 0x40`), then the same-id play with `music_keep_seek action=skip`. A sector change: `music_trace_play` of another id with
`action=vanilla` (or `pause_then_vanilla` when a keep_running track was still
running). What settles §5: whether the play after the stop carries the same
id (the `restart_pairs()` helper of the verifier pairs each stop with the next
play and marks `same_id` and `seek_skipped`); a play of another id or no play
at all means the keep must not ship as is.

## 7. Music through the save (2026-10-23)

Question: can the sector music stay audible through a savegame write on native Windows instead of the
measured 2.88 s silence (`music_keep_stop … name=save flags=0xd2 mode=paused` →
`music_keep_seek … action=skip`, 28.8 M QPC ticks with no Present in between; measured, user host)? Static
study of the same EXE (SHA-256 `fdbf3418d8f0a897…`, re-verified on `/mnt/c/X3/X3AP.exe`), nothing launched.
Byte patterns, the IAT slot, the census, the pump's x87 count and call set are pinned by
[`verify_music_through_save.py`](../../verification/results/music-restart/verify_music_through_save.py)
(`PASS`, 33 patterns; output `verify_music_through_save_output.json`); the story-script side by
[`kc_save_handlers.py`](../../verification/results/music-restart/kc_save_handlers.py) (output
`kc_save_handlers.txt`, both story builds). Raw disassembly stayed local. This section resolves the §5
bullet on the unpumped DirectSound path.

**Answer.** Yes, by keeping the save's stop-all off the DirectSound music record in the existing
`keep_running` shape (the vanilla bookkeeping still runs, so the MOVI chunk and the script state are
what vanilla writes), pumping that one record from the proxy's `gzwrite` import hook during the save, and skipping the
replay's re-arming Run (new Patch E at `0x00498d71`). `skip_all` under the save is not safe (it saves a
suspended music task that a load never wakes), and forcing the DirectShow path works in principle but
changes every music track's playback engine.

### 7.1 The DirectSound streaming pump `0x004d0700`

Media-object fields (ESI = media = `[rec+0x24]`); all from `0x004cf460` (`0x004d0244`–`0x004d0379`) and the pump:

| Field | Meaning |
| --- | --- |
| `+0x20`–`+0x43` | `DSBUFFERDESC`: `dwFlags` `+0x24` = **`0x10088`** = `GETCURRENTPOSITION2 \| CTRLVOLUME \| LOCSOFTWARE` (no `CTRLPOSITIONNOTIFY`, no `GLOBALFOCUS`/`STICKYFOCUS`); `+0x28` **ring bytes = `nAvgBytesPerSec * 5000 / 1000` (5 s)** (`0x004d02f8 imul 0x1388`) |
| `+0x44` | `IDirectSoundBuffer` (`IDirectSound::CreateSoundBuffer` on `*0x00608aec`, `0x004d034c`) |
| `+0x48` | buffer state: 1 stopped/needs first fill (set at `0x004d0372` after creation), 2 playing, 4 draining after end of stream |
| `+0x4c` | write offset in the ring (next byte to fill) |
| `+0x50` | drain: bytes still to play after the last data |
| `+0x54` / `+0x58` / `+0x5c` | `IAudioData` / `IAudioStreamSample` (`IAudioMediaStream::CreateSample`, `0x004d02e5`) / its buffer |
| `+0x60` | **sample bytes = `nAvgBytesPerSec * 2000 / 1000` (2 s)** (`0x004d024d imul 0x7d0`) |
| `+0x64` | decode state: 1 idle (issue `Update`), 2 update pending, 4 sample complete (waiting to be copied), 8 ended |
| `+0x68` | `wBitsPerSample / 8`, the rounding unit of the start/end clip |
| `+0x90` / `+0x94` | requested start / end ms (clip of the first / last sample) |

At 44.1 kHz 16-bit stereo (format assumed, not read from the soundtrack files) that is a 882,000 B ring and
352,800 B samples. One call (EAX = media):

1. `[+0x48]==1 && [+0x64]==1` (`0x004d070e`): re-arm (`Stop`, write offset and drain 0, `SetCurrentPosition(0)`).
2. `GetCurrentPosition(&play,&write)` (`0x004d074a`); failure returns 0.
3. state 2: `CompletionStatus(0, 0)` (timeout 0); `S_OK` → 4 (`0x004d0754`).
4. state 4: actual bytes from `IAudioData::GetInfo`; unless the buffer is stopped, copy only when
   **free = `(ring − wpos + play) mod ring` > actual** (`0x004d079f`–`0x004d07b3`). Clip by the sample times
   against `+0x90`/`+0x94`, `Lock(wpos, n)` (two tries, `DSERR_BUFFERLOST` → `Restore`), `memcpy`, `Unlock`,
   `wpos = (wpos + n) mod ring`, state 1; if the buffer was stopped, **`Play(0, 0, DSBPLAY_LOOPING)`**
   (`0x004d09de push 1`) and `[+0x48]=2` (`0x004d09ef`). A sample end at or past `+0x94` pauses the graph and starts the drain.
5. state 1: `IStreamSample::Update(SSUPDATE_ASYNC, 0, 0, 0)` (`0x004d0a61`); `S_OK` → 4, pending or error → 2
   (`0x004d0b0a`), `MS_S_ENDOFSTREAM` with 0 bytes → graph `Pause`, state 8, drain (`0x004d0adc`).
6. draining: remaining = `(wpos − play) mod ring`; when it grows past `+0x50` or reaches 0 the buffer is
   stopped, `[+0x48]=1`, `wpos=0`, return **2** (`0x004d0c0c`); otherwise silence is written from `wpos` up to
   the play cursor so the looping buffer does not replay old data. Otherwise return **1**.

So the ring is refilled **one 2 s sample per call, as soon as more than 2 s of it is free**: just after a
copy up to 5 s is queued ahead of the play cursor, just before the next one 3 s; with a per-frame pump the
lead stays in about [3 s − one frame, 5 s) (inference from the condition; decoding of the next sample is
asynchronous and issued right after each copy, so it is ready long before space is). The pump keeps no
lap counter and no position: its only bookkeeping is the modular write offset `+0x4c`, the drain remainder
`+0x50` and the two state words; the track position lives in the multimedia stream.

**Left playing, unpumped for 3 s** (inference from the code above): if the lead at the stall start
exceeds the stall nothing is audible and the next call refills normally. Otherwise the looping buffer runs
past the written data into ring bytes written one lap earlier: the listener hears the **previous 5 s of the
track again** (not silence, not the next 2 s: the next decoded sample sits in `+0x5c` until a call copies
it). After the stall, free is computed modulo the ring, so the pump keeps writing contiguously at the old
write offset, which the play cursor reaches only after it wraps: in total one whole lap (5 s; a whole number
of laps for stalls longer than lead + 5 s) of stale audio, then the track continues seamlessly from where
the valid data ended. No content is skipped and nothing stays desynchronised after that lap, but the lap is
audible. For the measured save the unpumped span (2.88 s plus parts of two frames) sits within about 0.1 s
of the minimum lead, so leaving the buffer running without a pump would be marginal now and fail for a
larger savegame.

Side finding (inference): the flown `mode=paused` resume is not exact either. The stop-all's DirectSound
arm sets `[+0x48]=[+0x64]=1` (`0x0049831c`/`0x0049831f`), and the replay's Run (`0x004d19dd`–`0x004d19e5`) does the same,
so the next pump call re-arms: the 3–5 s already in the ring and a completed but uncopied sample (state 4
is the common resting state) are discarded, and the stream continues from its decode position: a forward
jump of roughly 3–7 s besides the ~1 s re-arm gap of §6 "Alt-tab".

### 7.2 Who pumps, and calling the pump from inside the save

Only the main loop. `0x004d0700` has exactly one entry, the tail jump `0x004d17cc` of the status query
`0x004d14e0` (DirectSound arm: `[m+0x8c] & 0x10`, then `& 8` clear and `& 0x40` set); the query has one
caller, the media manager `0x00498370` at `0x004983d9`, which visits flag-2 records only. The manager's six
callers are the main loop (`0x00403a7f`/`0x00403a98`/`0x00403b04`, each right after the scheduler
`0x0049f770`) and three other synchronous loops (`0x00486809`, `0x00492dbe`, `0x0049729b`). No literal
references to the pump, the query or the Run helper; the buffer has no position notifications (no
`DSBCAPS_CTRLPOSITIONNOTIFY`, no `IID_IDirectSoundNotify` in the image); the EXE imports no `CreateThread`.
Nothing services the ring while the main thread is inside `0x00404530`.

Entry contract of `0x004d0700` (`0x004d0700`–`0x004d0c30`): **EAX = media object** (not the record), no stack
arguments, plain `ret`; preserves EBX/EBP/ESI/EDI (pushed at `0x004d0703`–`0x004d0706`, popped on all three
exits), clobbers EAX/ECX/EDX/EFLAGS; returns AX = 0 (cursor read, `Play` or a sample call failed), 1
(streaming), 2 (ended and stopped). 0x54-byte frame, no alignment requirement, **no x87 instruction**
(capstone count 0). Direct calls: `memcpy` `0x00515810`, `memset` `0x00518de0`, `__alldiv` `0x00518e60`, the
integer `MulDiv`-like `0x00469a30`, and on `E_OUTOFMEMORY` only the OOM hook `0x004b8b60` / `[0x00608a00]` =
`0x00406d80` (installed at `0x004035b1`; resource purges `0x0046d2a0`/`0x004f5200`, guarded by `0x006090f0`);
every other call (21 register calls) is a COM method on `+0x44`, `+0x54`, `+0x58`, `+0x70` or `+0x74`. It
reads and writes only media fields (`+0x28` … `+0x94`, writes `+0x48`/`+0x4c`/`+0x50`/`+0x64`); it does not
read the record, walk the record list, call the engine allocator `0x005112c4`, run script code or take an
engine lock. Under the manager a 0 return destroys the record (`0x004984b0`) and a 2 fires the completion;
called directly, the caller owns both outcomes.

Hazards inside `0x00404530` and the safe region:

* **The stop-all must have left the graph and the buffer running.** Under the current F1 `mode=paused` the
  graph is paused (`0x004982f0`) and `[+0x48]=[+0x64]=1`; a pump call would re-arm a buffer whose stream no
  longer delivers samples. Under `keep_running` (`jmp 0x00498322`) the span `0x004982e1`–`0x00498321` with the
  Pause, the `Stop` and both state writes is skipped, so the pump state is live and flag 2 / context are what
  vanilla leaves (cleared, completion fired). The pump does not read either.
* **MOVI writer `0x004988d0` walking the list**: no conflict; the pump touches neither nodes nor record fields.
* **Allocator / lock**: none on the pump path except the OOM purge, which every engine allocation site
  inside the save can reach as well (`0x004b8b60` has many callers).
* **Thread and apartment**: the amstream/DirectSound objects were created on the main thread; call the pump
  only on that thread (the save routine runs on it; record the thread at the Patch A save decision).
* **Return 0/2 inside the save**: do not fire a completion (the script state may already be written); stop
  pumping that record. A 2 means the track ended during the save; the replay then re-arms at end of stream
  and ends at once, and the script's next same-id play seeks to 0 as after any natural end.

`gzwrite` is called only between `gzopen` (`0x00404601`) and `gzclose` (`0x00404a6f`): the routine's own
fields (`0x0040468e`–`0x004049d9`), the script writer `0x0049f930` (`0x004049ec`), `0x00479010`, MOVI
(`0x00404a0b`), `0x00417610`, `0x0043d920`, `0x0041f660`, `0x0049b7e0` and the trailer (`0x00404a66`). The
universe writers call the import thunk `0x004fae46` (`jmp [0x005323e0]`, IAT slot verified =
`zlib1.dll!gzwrite`) directly (52 sites), the rest go through the dispatcher `0x004e92e0`, so **the IAT slot
sees every save write**. Safe region for a pump call: the whole interval after the stop-all returns
(`0x00404561`) to the routine's return (`0x00404cb2`); the reachable part is every `gzwrite` call, before the
real `gzwrite` runs (not inside zlib). Unpumped by this hook: `0x00404561`–`0x00404601` (free lists
`0x00417100`/`0x00416fe0`, the object walk `0x00486990`, path setup) and `0x00404a74`–`0x00404cb2` (slot
files, game-clock resync `0x004d1df0`), plus the `SaveFinished` events, the frame's render and the next
scheduler pass up to the replay. Their durations are not known statically.

### 7.3 `skip_all` under the save

With the existing `skip_all` exit for the save caller the MOVI chunk stores the record as playing: flags
`0xd2` (bit 2 set), `+0x14` the live task key, `+0x18` the slot. The restore `0x00498ad0` tolerates that (bit
2 masked at `0x00498b99`, `+0x14`/`+0x18` ignored, §3). The script state does not: the script writer
(`0x0049f930`, called at `0x004049ec`, before MOVI) stores each task's state word `+0x3a` (`0x004a0584`), and
the loader puts a state-2 task back on the suspended list `VM+0x12e4` (`0x004a1c69`–`0x004a1c8c`) without
waking it. With `skip_all` the music task is still suspended in `MOV_PlayMovie` (no completion), and after a
load no record carries its key, so it never resumes: **the music player of that save stays blocked after
loading it** (inference: unless another story path restarts the music with a new task; the stuck task would
then leak). In vanilla and under `keep_running` the stop-all's completion makes the task runnable before the
script writer runs, which is why the vanilla savegame is self-consistent. `skip_all` is therefore not a
candidate for the save, with or without a pump.

Story side (decoded, both builds, `kc_save_handlers.txt`): `150::BeforeSave` = `SE_FreeText()`;
`150::SaveFinished(r)` = `201::RestoreTexts()`, `640::ResetSavegameCache()`, `150::SetLeave()` when
`STA_GetDIDState() != 0`, otherwise a block with `TI_Delay(1000)` and `200::SpeakWithPriority(13, 39, 100)`;
`SaveFinished2` calls `SaveFinished`. Their call closure (41–45 methods) reaches only voice natives
(`MOV_PlayMovieVoice`, `MOV_GetVoice*`, `MOV_StopMovie` in `200::SpeakWithPriorityAndNoise`) and the screen-movie
helper `200::PlayMovieArrayDirect`, never `MOV_LoadMovie`, `MOV_PlayMovie` or `MOV_SetVolumeMovie`: **the
handlers do not touch the music** (inference for the arguments of the reached `MOV_StopMovie` calls: voice and
movie ids). The music player `200::PlayMusicWithFade` loads the track as `MOV_LoadMovie(m17, 144)` (flags
`0x90`) and loops on `MOV_PlayMovie(m17)` (decode has stack-depth conflicts, so the loop shape is partial),
consistent with the measured same-id replay.

### 7.4 The DirectShow path

The path is chosen inside `0x004cf460`: **any request with `0x10` (audio only) gets `0x40` forced**
(`0x004cf507`–`0x004cf50b`), the audio stream is then added without the default renderer
(`0x004cf5ce`–`0x004cf5d7`: `AMMSF_ADDDEFAULTRENDERER` only when `0x40` is clear), and a failed DirectSound
buffer fails the whole constructor for an audio-only request (`0x004d0352`–`0x004d035a`) instead of falling
back. The allocator copies the media flags into the record (`0x00498254`). The story requests `0x90`, so
every sector track is a DirectSound record (`0xd0`), as flown on native.

A DirectShow-path music record (`0x40` clear) plays from the graph's own threads; under the save's existing
`keep_running` (or `skip_all`) it would simply keep playing (inference from the DirectShow model; the only
per-frame engine touch is the end-of-track position/duration poll at `0x004d17d1`), and the replay's seek
skip plus Run is a no-op on a running graph. Forcing it means clearing `0x10` from music requests before the
constructor: at the allocator's single constructor call `0x004981d3` (`push edi; push ebx; call 0x004cf460`,
flags = `[esp+8]` at the callee) or at `MOV_LoadMovie`'s `0x00498735`. Cost and risk: the constructor then
also adds an `MSPID_PrimaryVideo` stream with the object `*0x00608ad4` (`0x004cf56b`–`0x004cf587`; GUID at `0x00532b94` verified, the object is DirectDraw by inference), discovers there is no
video, sets `0x10` and tears it down (`0x004d01bf`, `0x004d1b70`); the audio goes through the DirectShow
DirectSound renderer only when bit `0x4000` of `[*0x00606f34+0x100]` is set (`0x004cf59e`; otherwise the
stream gets no renderer and the music is silent; the bit's source was not traced); the restart seek keeps the
500 ms back-off of `0x10` records; the renderer's focus behaviour in the background is unknown. It changes the
playback engine of every music track to fix one interruption, and is not flown.

### 7.5 Recommendation

The cheapest safe shape, three pieces on the existing keep:

1. **Patch A rule (no new site, `0x004982db`)**: for the `save` caller (`0x00404561`), a DirectSound music
   record takes `keep_running` instead of F1's `paused`, and arms a save-pump target
   `(record, id, media, thread)`. Bookkeeping and savegame content stay vanilla (§7.3).
2. **Save pump in the `zlib1.dll!gzwrite` import hook** (IAT slot `0x005323e0`; chain whatever already
   occupies the slot, e.g. the telemetry row of `loading_trace.cpp`): while a target is armed and on its
   thread, at most every 250 ms of wall time (read QPC every 256 calls; the save makes on the order of
   millions of calls, inferred from the load profile's 13.9 M reads) check that the record is still linked
   with the same id and media, `[m+0x8c] & 0x48 == 0x40` and `[m+0x48]` is 2 or 4, then call `0x004d0700`
   with EAX = media through an asm shim, before forwarding to the real `gzwrite`; preserve LastError; a 0
   or 2 return disarms. Cost when idle: one load and branch per `gzwrite`. With a 250 ms cadence the lead
   stays above about 2.75 s, so every unpumped span (§7.2) must stay below that.
3. **Patch E, `0x00498d71`** (`e8 fa 8a 03 00` = `call 0x004d1870`, whole instruction between
   `mov eax,esi` at `0x00498d6f` and `test eax,eax; je 0x00498dde` at `0x00498d76`; raw scan: only the
   rejected operand byte `0x00498d4e`; `& 7 = 1`, so one `lock cmpxchg8b`). In: EAX = ESI = record; the callee
   preserves EBX/EBP/ESI/EDI and returns 1 on this path; after it ECX/EDX and the flags are dead
   (`0x00498d7a` reloads EAX, `xor ecx,ecx`, EDX is loaded at `0x00498d96`). Thunk: when Patch C has just
   skipped the seek for this record under a save-pump target (pass a one-shot marker from the C handler,
   because C drops the holds) and `[m+0x48]==2`, `mov eax,1; ret` (no re-arm, no Run on the running graph);
   otherwise `jmp 0x004d1870`. The second Run caller `0x00498f76` stays vanilla.

Disarm the target at the replay, at the next stop-all entry, or after a wall-time bound; until the replay
the record has flag 2 clear and the manager does not pump it (measured once: the replay came in the next
iteration), so an optional Present-time pump with the same checks covers a delayed replay, and the F2 orphan
pause stays the backstop for a script that never replays. Pause (`0x00407069`) could take the same
`keep_running` + Patch E without a pump (the loop keeps running and the woken task replays in the same
scheduler pass; inference), which would also remove the 3–7 s jump of §7.1; not part of this question.

Only a flight settles: the durations of the unpumped spans (stop-all → first `gzwrite`, the largest gap
between `gzwrite` calls, last `gzwrite` → replay), which a consolidated diagnostic build should log per save
together with each pump call's return, `[m+0x48]`/`[m+0x64]` and the lead (`ring − free`); that amstream
completes the asynchronous `Update` while the main thread is blocked in the save (DirectShow design, not
observed); that the replay keeps arriving in the next iteration; and the audible result.

### 7.6 Implementation (2026-10-23)

Built under the existing `music_keep` option (no new entry) in `src/proxy/music_keep.cpp`, constants and the two
state machines (`SavePump`, `ResumeMemory`) in `music_keep_core.h`; byte check
[`verify_music_keep_writes.py`](../../verification/results/music-restart/verify_music_keep_writes.py) (`PASS`, 65
checks; 69 after the review fixes), host test `verification/analysis/test_music_keep.py`; ledger
[music-keep.md](../verification/music-keep.md).
**Patch E** (`0x00498d71`, `claim_call`, one `lock cmpxchg8b`) is optional like D: its windows (pump head 55 B, drain
exit 37 B, status DS tail 28 B, the continuation 35 B from `0x00498d76`, the `gzwrite` thunk) are compared first and a
refusal logs `patch_e=<reason>` and keeps the save `paused`. The **save pump** reuses the proxy's only import writer:
the keep registers a save-write observer with `loading_trace` (`set_save_write_observer`), which then patches its
`gzwrite`/`gzclose` rows also without telemetry and binds them once; the keep accepts the bind only on slot
`0x005323e0` holding `zlib1.dll!gzwrite` (`GetProcAddress`), the gzclose row on `0x005323f0`, both rows live, the
thunks matched, E live and the install window open, else `music_keep_save_pump_hook reason=<refusal> save_mode=paused`. Patch A: `save` + DirectSound music
record + bound pump + no other target in this stop-all → `keep_running` and arm `(record, id, media, thread)`; pause
and alt-tab unchanged. The observer reads QPC every 32 `gzwrite` calls (one decrement otherwise; the idle row costs
two loads and a branch) and, on the armed thread, at most every 250 ms (the first sampled call always pumps), checks
the record (linked, same id and media, flags music + `0x40` with flag 2 clear, `[m+0x8c] & 0x48 == 0x40`, `[m+0x48]`
2 or 4) and calls `0x004d0700` through an integer shim under `LightCallBoundary` (the engine's MXCSR and LastError
for the call, restored after). Pumping stops at the save's own gzclose only: the save closes through the wrapper
`0x004e9360` (called at `0x00404a6f`, six callers in all), whose gzclose call returns to `0x004e9384`; the import hook
gets that return and the dword above its argument, the wrapper's own return `0x00404a74`, and any other pair is
ignored. Every other stop is an early stop: a refused check, a 0/2/other return, 120 s, or (checked at each Present
on the armed thread) no replay within `run_skip_bound_ms` = 2500 of the last pump (5 s ring minus one 2 s sample,
less a margin). An early stop cancels the Run skip and **pauses the record through `0x004d1810`** when it is still
linked as the target, flag 2 clear, unclaimed and music (the F2 rule), and its hold becomes a paused one; the replay's
seek skip and the vanilla Run then resume it exactly as the flown paused mode. The episode ends at the first music
play (the replay of the kept record with the seek skipped leaves a one-shot marker; Patch E then answers 1 without the
Run only for that record with `[m+0x48]` = 2 and its last pump younger than 2.5 s, else the Run re-arms:
`fallback_reason=stale`), at `MOV_StopMovie` of the id and at the next stop-all entry, and writes one line. The handlers'
fast paths (gzwrite observer, Patch E) end in a tail jump to a noinline helper that holds the boundary; the build
uses table-based unwinding (no SjLj registration anywhere in the DLL).

Lines a flight shows: install `music_keep … patch_e=ok site_e=0x00498d71 write_e=atomic thunk_e=… save_pump=pending
resume=ok`, then `music_keep_save_pump_hook reason=ok save_mode=keep_running iat=0x005323e0 write_row=1 close_row=1
… interval_ms=250 stride=32 bound_ms=120000` and `loading_trace … scope=…music_keep`. Per save: `music_keep_stop
name=save flags=0xd2 mode=keep_running held=1 pump=1`, `music_keep_seek … action=skip`, then one `music_keep_save_pump
record= id= calls= ret0= ret1= ret2= ret_other= writes= samples= span_ms= first_call_ms= max_gap_ms= end_ms=
pump_end=gzclose close_caller=0x00404a74 end=replay run=skipped buffer_state=2 fallback=none fallback_reason=none
fallback_ms=0 interval_ms=250` (all spans measured with QPC
from the arm: `span_ms` to the end of pumping, `first_call_ms` the stop-all → first pump, `max_gap_ms` the largest
unpumped span up to gzclose, `end_ms − span_ms` the gzclose → replay span). Healthy: `ret1 = calls ≈ span_ms/250`,
`max_gap_ms` and `end_ms − span_ms` well below 2.75 s, `run=skipped`, no repeated audio after the save.
`fallback=paused fallback_reason=refused|failed|ended|bound|overdue fallback_ms=` means the record was paused at
that time (the flown paused mode followed); `fallback=unlinked` that there was nothing to pause; `run=vanilla
fallback_reason=stale` that the replay came more than 2.5 s after the last pump and re-armed.

Reproduce:

```sh
python3 -I verification/results/music-restart/verify_music_through_save.py /mnt/c/X3/X3AP.exe
python3 -I verification/results/music-restart/kc_save_handlers.py /mnt/c/X3
i686-w64-mingw32-objdump -d --start-address=0x4d0700 --stop-address=0x4d0c31 /mnt/c/X3/X3AP.exe  # pump
i686-w64-mingw32-objdump -d --start-address=0x4d0244 --stop-address=0x4d0379 /mnt/c/X3/X3AP.exe  # ring/sample/buffer
```

## Reproduce

```sh
python3 verification/results/music-restart/verify_music_restart_sites.py
i686-w64-mingw32-objdump -d --start-address=0x4982b0 --stop-address=0x498367 "$X3AP"  # stop-all
i686-w64-mingw32-objdump -d --start-address=0x4d3620 --stop-address=0x4d3790 "$X3AP"  # WndProc head
i686-w64-mingw32-objdump -d --start-address=0x498c90 --stop-address=0x498e28 "$X3AP"  # play
i686-w64-mingw32-objdump -d --start-address=0x404530 --stop-address=0x404570 "$X3AP"  # save entry
```
