# Voice/movie streams at game load: what the main thread does synchronously

2026-09-14. Read-only targeted RE of the installed `X3AP.exe` plus the already
retained public Wine sources under `/tmp/x3-voice-stream-study/public-source/`.
No Wine execution, no build, no game launch, no production change. This note
owns the question raised by the load hang recorded in
[voice-decoder-adapter.md](../architecture/voice-decoder-adapter.md): what the
game creates and starts during load, what it polls afterwards, and which thread
must satisfy each exit condition. It complements
[voice-stream-creation.md](voice-stream-creation.md) (creation route and cache)
and [voice-cue-timing.md](voice-cue-timing.md) (trim arithmetic).

## Provenance

Ghidra 12.1.3, project `/tmp/x3-voice-stream-study/Voice.gpr`, opened
`-process X3AP.exe -noanalysis -readOnly`; scripted xref/instruction/decompile
dumps to `/tmp/x3-voice-startup/` (untracked). EXE SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
2,153,984 bytes, preferred base `0x00400000`; addresses are preferred VAs.
Cited call sites were byte-checked against the installed file through the PE
section map (`.text` VA `0x00401000` → file `0x400`, i.e. offset = VA −
`0x400C00`): `004d03ea` `8b 43 04 8b 08 8b 51 1c 6a 01 50 ff d2`, `004d0407`
`ff d2 85 c0 0f 8c 4f fd ff ff`, `004d0754` `83 7e 64 02 75 1a`, `004d0a61`
`ff d2 8b f8 81 ff 0e 00 07 80`, `004d3532` `6a 00 6a 00 6a 00 8d 44 24 24 50`.
Wine behaviour is quoted from the retained public copies
`amstream_audiostream.c` / `amstream_filter.c`; it is **not** a trace of the
installed CrossOver `amstream`.

## 1. The game is single-threaded and owns no wait primitive

The import name table of `X3AP.exe` contains **no** `CreateThread`, no
`_beginthread`/`_beginthreadex`, and **no `WaitForSingleObject` or
`WaitForMultipleObjects`** (byte scan of the whole file: zero occurrences of
each name; `Sleep`, `CoInitialize` and `PeekMessage` are present once each).
The only `Sleep` references are `00515b92`, `00515bda`, `00515c25`, `00515c75`,
all inside the statically linked CRT lock helpers (`00515b75`…`00515c48`), not
in game code.

Consequences, all load-bearing for this question:

* every COM call on a media object — `CoCreateInstance`, `Initialize`,
  `OpenFile`, `CreateSample`, `SetState`, `Run`, `Pause`,
  `put_CurrentPosition`, `Update`, `CompletionStatus`, `GetSampleTimes`, and
  every DirectSound call — happens on the single WinMain thread;
* the game can never block on a handle. Any "wait" it performs is a poll inside
  one of its own loops, and any true blocking is inside a Wine DLL called from
  that thread;
* `AMMSF_NOGRAPHTHREAD` (below) requires message pumping on the creating
  thread; the game satisfies this only because the same thread runs its message
  pump.

## 2. The five-call pump and where it runs

`00403840` is the session routine (game loop). Its per-iteration head increments
the frame counter at `DAT_00606f34+0x724`, and every iteration runs the same
five calls in order:

| Call | Role |
| --- | --- |
| `004b0e00` | pre-frame housekeeping |
| `004d34b0` | **Win32 message pump** (`PeekMessageA`/`GetMessageA`/`TranslateMessage`/`DispatchMessageA` via the IAT slots `005322b8`/`005322b4`/`005322c0`/`005322c4`) |
| `0049a130` | timing/input step |
| `0049f770` | script-engine step |
| `00498370` | **movie/audio manager update** — the only route to sample refill |

The same quintuple runs twice before the loop (`00403a7f`, `00403a98`) and once
inside it (`00403b04`), and once in `00497200` (`0049729b`) right after the
`_Init` script event is dispatched. Two further `00498370` sites are asset
loaders that pump only the media manager while the loading screen is up:
`00486809` in `004863c0` (guarded by `DAT_00606f34+0x108 & 0x800`) and
`00492dbe` in `00492970`. Those six sites are the complete xref set of
`00498370`; all are on the single thread of section 1.

`004d34b0` also decides the pump mode from `DAT_00608adc` (set to 1 at
`004cc022`/`004cc04c` etc. around `DialogBoxParamA`, i.e. "no modal dialog /
app active") and the config bit `*DAT_00606f3c & 0x4000`:

* active: `004d351f` `PeekMessageA`; if a message is waiting it enters the drain
  loop `004d3532`…`004d3562` (`GetMessageA` → `TranslateMessage` →
  `DispatchMessageA` → `PeekMessageA`, `JNZ` back to `004d3532`). **This loop is
  unbounded and never blocks**: it exits only when `PeekMessageA` reports an
  empty queue;
* inactive: `004d34f7` blocking `GetMessageA` (a real kernel wait).

## 3. Which subsystem creates a stream during load, and from what

Every media object in the process comes from one constructor: `004cf460`, whose
only caller is `004981d3` inside the record allocator `00498140`. `00498140` is
called from exactly five sites:

| Site | Caller | When |
| --- | --- | --- |
| `00498bae` | `00498ad0` savegame `MOVI` restore, called at `004050a2` from the savegame loader `00404cc0` | **during load** |
| `004f6610` | `004f65f0` track/emitter play helper (`0045c607` in `0045b720`, `00460424` in `00460390`, `004f6836` in `004f66e0`) | scene/sector update, render walk |
| `00498cd8` | `00498c90` play-by-id (VM dispatch `0049981f`, `00499982`; also `004f6668`) | script VM |
| `0049873a` | `00498730` ← `004987a0` | stop/query helper |
| `00498ef8` | `00498e30` cue play (VM `00499849`) | speech line |

`00498ad0` is the load-time producer: it parses the `MOVI` chunk of the
savegame and, for every stored source id outside the voice range 101–299,
looks the id up in the manager list and calls `00498140` when absent, then sets
only record flag bit `4` and the object index at `+0x30`. It never sets bit `2`
(playing). The run-31 observation of three ASF graphs being built at load is
consistent with this site.

`00498140` maps the id to constructor flags before calling `004cf460`:
ids `1`/`3` → `8` (`+0x30`=`0x42`), `2` → `8` (`+0x30`=`0x3d`), `800`/`10001` →
`8`, `810`/`811` → `0x100`, and `101`–`999` → `0x110`. `004cf460` adds `0x40`
when `0x10` is set, giving the voice/manual-audio value `0x150`. Flag `8` means
"no primary audio stream added" (the `& 8` gate at `004cf5xx` and at
`004d14e0`/`004d0700`), i.e. the silent-video ids.

`004cf460` resolves the file with `"%05d.dat"` and then, for ids 100–299, 810
and 840–899, the literal `"addon\\mov\\%s"`; otherwise the configured movie
directory (`DAT_00606f34+0xd8`, `+0x90`). Fallbacks are
`"soundtrack\\%05d.mp3"` and `"soundtrack\\%05d.wma"`. The installed bottle has
`soundtrack/000NN.mp3` files and `addon/mov/00144.dat`, `00244.dat`,
`00810.dat`, `00841.dat`, so ASF/WMA sources are the `addon\mov` archives, not
the music tracks.

On failure `00498140` frees the unlinked 0x40-byte record and returns 0. On
success it links the record, copies the media flags into `+0x2c`, and then
**explicitly clears bit `2`** — a newly created stream is not playing.

## 4. The constructor runs the graph before any consumer exists

Established from `004cf460`:

| Site | Call |
| --- | --- |
| `004cf53a` | `CoCreateInstance` of the AMMultiMediaStream (IAT `005323c8`) |
| `004cf554` | `IAMMultiMediaStream::Initialize(STREAMTYPE_READ=0, AMMSF_NOGRAPHTHREAD=1, NULL)` |
| `004cf57d`… | `AddMediaStream` for primary video (`0x532b94`) / primary audio (`0x532b84`) |
| `004d0143` / `004d00f5` | `OpenFile` (`vtable+0x40`), plus the `AddSourceFilter`/`FindPin`/`Render` alternative under `DAT_00606f34+0x100 & 0x4000` |
| `004d02e8`… | `IAudioMediaStream::CreateSample` → object `+0x58`; `IAudioData` `+0x54`; DirectSound buffer `+0x44` |
| `004d038a` / `004d03a5` | QI `IMediaPosition` → `+0x70`, `IMediaControl` → `+0x74` |
| `004d03c4` | `IMediaPosition::CanSeekForward` (`vtable+0x40`) |
| **`004d03f5`** | **`IMultiMediaStream::SetState(STREAMSTATE_RUN)`** on `+0x04` (`vtable+0x1c`, arg 1) |
| **`004d0407`** | **`IMediaControl::Pause()`** on `+0x74` (`vtable+0x20`) |

Both are unconditional for every created object and both destroy the object via
`004d1d40` on failure. So **construction alone starts the graph and then pauses
it**, before the record is marked playing and before any `Update` is issued.
`+0x90` is initialised to 0 and `+0x94` to `0xffffffff` at `004cf524`/`004cf52a`.

Consumption cannot begin at that point even if the record were pumped: Wine's
`audio_sample_Update` returns `MS_E_NOTRUNNING` while
`sample->parent->state != State_Running`, and the stream only reaches
`State_Running` when the game calls `IMediaControl::Run` in `004d1870`
(`+0x74` `vtable+0x1c`), which happens only from the play paths `00498d71`
(`00498c90`) and `00498f76` (`00498e30`).

For a savegame-restored record this never happens: bit `2` stays clear, so
`00498370` skips it entirely and no `Update` is ever issued for the life of the
object. The graph stays paused with whatever the source pushed during the brief
`RUN`.

## 5. Every synchronous loop the main thread enters for a stream

`00498370` walks the manager list `*DAT_00606f44` and processes **only records
with flag bit `2`**. Per record it calls `004d14e0` once and switches on the
returned short: `1` → `004d0600` (position → ms), `2` → finished (clear bit 2,
fire the completion callback, or re-seek a looping cue via `004d0430`), `0` →
error, destroy through `004984d0`. There is no retry loop and no wait here.

`004d14e0` guards first: `if (DAT_00608adc == 0 && (*DAT_00606f3c & 0x4000) == 0)
return 2` — while the app is inactive every stream reports "finished". Then:

* **video / non-manual path** (`flags & 0x10 == 0`): drives the video sample at
  `+0x14` with `CompletionStatus` (`vtable+0x1c`), `GetSampleTimes`
  (`vtable+0x10`) and `Update(1,0,0,0)` (`vtable+0x18`), each wrapped in a
  **two-attempt** retry that only re-runs on a negative HRESULT and calls the
  memory-recovery helper `004b8b60` on `E_OUTOFMEMORY`. It then blits through
  `004d0c40` and, for `+0x94 > 0`, polls `IMediaPosition::get_CurrentPosition`
  (`+0x70` `vtable+0x24`) against `+0x94`;
* **manual audio path** (`flags & 0x40`, i.e. voice and every `0x150` object):
  falls straight through to `004d0700`;
* tail for non-manual audio: one `get_CurrentPosition` plus one `get_Duration`
  (`+0x70` `vtable+0x24` / `+0x1c`) and returns 2 when they compare equal.

`004d0700` is the only place an audio `Update` is issued. Per call it performs a
**fixed** sequence, never a spin:

| Site | Step and exit condition |
| --- | --- |
| `004d0735` | `IDirectSoundBuffer::GetCurrentPosition` (`+0x44` `vtable+0x10`) → play/write cursors; a negative HRESULT returns 0 (error). `004d0721` stops the buffer first when a restart is pending |
| `004d0754`/`004d0767` | if state `+0x64 == 2`: `IStreamSample::CompletionStatus(0, 0)` on `+0x58` (`vtable+0x1c`). `TEST EAX,EAX; JNZ` — **only `S_OK` advances to state 4**; `MS_S_PENDING` (`0x40001`) leaves state 2 and the routine returns 1 |
| `004d078d`… | state 4: `IMemoryData::GetInfo`, `GetSampleTimes`, the trim arithmetic of voice-cue-timing, `Lock`/`memcpy`/`Unlock`, `Play`; if the ring has no room the copy is skipped and state stays 4 with **no new `Update`** |
| `004d0a4c` | state 1: `IMediaPosition::get_CurrentPosition` |
| `004d0a61` | state 1: `IStreamSample::Update(SSUPDATE_ASYNC=1, NULL, NULL, 0)`, two attempts; `MS_S_ENDOFSTREAM` → drain, `S_OK` → state 4, anything else → state 2 |
| `004d0b18` | if `+0x48 == 4` (draining): one comparison of the play cursor against `+0x50` (`004d0b36`/`004d0b3f`); returns 2 when drained, otherwise 1 |

`MS_S_ENDOFSTREAM` reaching the game through `CompletionStatus` instead of
through `Update` is **not handled**. `004d0769` is `TEST EAX,EAX` followed by
`JNZ 004d0774`, so only `S_OK` advances state `+0x64` from 2 to 4; the end-of-
stream code `0x00040003` is nonzero and leaves the object in state 2. Wine sets
`sample->update_hr = MS_S_ENDOFSTREAM` in `process_updates` exactly when an
update was queued, nothing was copied into it (`!sample->position`) and EOS then
arrived — i.e. when the game issued `Update` shortly before the stream ended.
The object then re-polls the same `MS_S_ENDOFSTREAM` on every pump forever:
`004d0700` keeps returning 1, `00498370` never clears bit `2` and never fires
the record's completion callback. A requester that waits for that callback waits
indefinitely while the main thread stays busy in its ordinary loop. The
end-of-stream branch the game does handle is the synchronous one at `004d0aa1`
(`Update` itself returning `0x40003`).

So the main thread never spins on stream data. It issues **at most one
outstanding `Update` per media object** and re-checks it once per pump. The exit
condition of the only audio poll is `CompletionStatus == S_OK`, which in Wine's
`process_update` is set only when `sample->position == sample->length`, i.e.
when the upstream has delivered a **complete** `IAudioData` buffer — `+0x60` =
`nAvgBytesPerSec * 2`, two seconds of PCM (176,400 bytes at the measured
44,100 Hz mono 16-bit) — or `MS_S_ENDOFSTREAM`. A partial fill stays
`MS_S_PENDING`. The thread that must satisfy it is the upstream streaming
thread calling `IMemInputPin::Receive`, i.e. the decoder pipeline; Wine's
`audio_meminput_Receive` itself never blocks — it queues the sample, `AddRef`s
it and returns `S_OK`, so back-pressure appears further upstream (allocator
exhaustion, then the `wg_parser` sink).

## 6. Why the fast failure hid all of this

Before the decoder existed, `004cf460` returned 0 at `OpenFile`
(`VFW_E_CANNOT_CONNECT`) and `00498140` freed the unlinked record. No graph
survived, no record was linked, bit `2` was never set, `00498370` had nothing to
iterate, and the per-pump cost of the audio path was zero. The loading screen
therefore completed. With a working decoder the same code path now produces
live, `RUN`-then-`Pause`d graphs at load, each with an upstream pipeline that
begins producing immediately and, for savegame-restored records, with **no
consumer at all**.

## 7. The exact wait the decoder must satisfy, and where the property lives

* The game never waits; it polls, and only for records it has explicitly
  started. There is no stream-ready event, no completion event and no callback
  from the pipeline into the game: `Update` is issued with a NULL event and NULL
  APC (`004d0a61`), and completion is observed only by the next
  `CompletionStatus(0,0)` from the same thread.
* For a **started** stream the decoder must deliver a full 2-second `IAudioData`
  buffer per `Update`, and it will be asked for the next one only when the main
  thread next reaches one of the six `00498370` sites. During loading those are
  the per-asset sites `00486809`/`00492dbe` and the frame-loop site `00403b04`,
  so the consumer cadence is arbitrary and can be seconds apart.
* The decoder must not let an `Update` complete with `MS_S_ENDOFSTREAM` and
  zero bytes copied: see the unhandled `004d0769` path above. Delivering at
  least one byte for every accepted update, or letting `Update` itself return
  end of stream, keeps the object out of that state.
* For a **created but not started** stream — the savegame `MOVI` restore case —
  the decoder must tolerate being run for an instant and then paused with *no*
  consumer, indefinitely, without blocking any thread whose progress the game's
  main thread depends on. This is not a cadence the game can be asked to
  improve: `00498140` clears bit `2` by design, and `Update` before
  `004d1870`'s `Run` is refused by `amstream` with `MS_E_NOTRUNNING`.

Attribution of the three candidate properties:

1. **Game**: creating a graph, running it and pausing it before a consumer
   exists (`004d03f5`/`004d0407`), and single-threaded, poll-only consumption.
   Established here; unchangeable without patching the EXE.
2. **CrossOver `amstream`/`wg_parser`**: the sink accepts and holds media
   samples without blocking but without bound, so the block lands on the
   upstream allocator and then in the `wg_parser` sink, where it is not a
   DirectShow state/flush-aware wait. Established from public Wine source only.
3. **Plugin**: an unbounded blocking push whose release depends on a consumer
   that may legitimately never appear. This is the only one of the three we can
   change, and the requirement is therefore: *the decoder chain must never let a
   stalled downstream block progress of anything the game's main thread calls*
   — not a faster or larger buffer.

A mitigation that only avoids issuing `Run` until a consumer exists is **not
available to us**: the `Run` that starts production is the constructor's own
`SetState(STREAMSTATE_RUN)` at `004d03f5`, three instructions before the
`Pause`. If a game-side mitigation is ever wanted, `004d03ea` is a usable
prologue seam — `MOV EAX,[EBX+4]` / `MOV ECX,[EAX]` / `MOV EDX,[ECX+0x1c]`,
three whole instructions, exactly 8 bytes, no relative operand, at a real
instruction boundary. `EAX`, `ECX`, `EDX` are dead on entry and flags are dead
(the next flag producer is `TEST EAX,EAX` at `004d03f7`); `EBX` (media object),
`ESI` (`&obj+0x70`) and `EDI` (`&obj+0x74`) are live and must be preserved.
Reentrancy is not a concern for the reason in section 1, and the constructor is
not reentrant for one object. This is a described seam, not a proposed hook:
skipping the `RUN`/`Pause` pair changes documented `AMMultiMediaStream`
lifecycle behaviour on native Windows too, and would need its own design.

## 8. What this does not settle

* **The hang itself is not explained by a game-side loop.** No loop in
  `00498370`, `004d14e0` or `004d0700` is unbounded; every retry is capped at
  two attempts. The only unbounded, never-blocking loop the main thread can
  occupy in this sequence is the message drain `004d3532`…`004d3562` inside the
  pump `004d34b0`, which exits only when `PeekMessageA` finds the queue empty.
  Given `AMMSF_NOGRAPHTHREAD` at `004cf554`, that loop is the first thing to
  test against the frozen-process sample. The second candidate is that the
  sampled addresses are not game code at all but the Wine PE-side DLLs.
* **Discriminator for the existing sample**, no new run needed: bucket the
  sampled guest addresses by range — `00403840`–`00404277` (session/frame
  loop), `004d34b0`–`004d3612` (message pump), `00498370`–`004984c3` (manager
  update), `004d14e0`–`004d180f` (per-object update), `004d0700`–`004d0c30`
  (refill), `004cf460`–`004d0427` (construction), anything outside `0x00400000`–`0x0060a000`
  (Wine DLLs). Addresses inside `004d0700`/`004d14e0` would mean the game is
  servicing the streams and the cost is per-call latency in the Wine DLLs;
  addresses concentrated in `004d3532`…`004d3562` would confirm the message
  drain; addresses outside the EXE range would move the question out of the game
  entirely. The reported "~419 JIT addresses" is consistent with a loop that
  dispatches messages, and not with a tight two-instruction spin.
* A third candidate for "alive but not progressing", distinct from the message
  drain: any record stuck in state 2 by the unhandled `MS_S_ENDOFSTREAM` above
  never fires its completion callback, so a script or load step that waits for
  that callback never advances while the frame loop keeps running. This is
  consistent with the reported symptom but is not established for this run: it
  requires a record with bit `2` set, which savegame restore alone does not
  produce.
* Which source ids the three run-31 graphs correspond to is not established;
  `00498ad0` reads them from the savegame, so it is savegame-specific.
* Whether the installed CrossOver `amstream` matches the public Wine source
  cited here is unverified, as in voice-cue-timing.
* `DAT_00608adc` is identified as an app-active/no-modal-dialog flag from
  `004cc020`/`004cc060`/`004cc0a0`/`004cc0e0`; its full set of writers
  (`004cbf70`, `004d3620`, `004dac90`, `00401e0e`) was not audited.

## 9. Startup replica without the game (2026-09-14)

`verification/probe/voice_startup_replica.cpp` (runner
`run_voice_startup_replica.py`, host test
`verification/analysis/test_voice_startup_replica.py`) performs the sequence of
sections 4–5 on the main thread: three `AMMultiMediaStream` objects
(`Initialize(READ, NOGRAPHTHREAD)`, primary audio, `OpenFile` on
`addon\mov\00144.dat`/`00244.dat`/`00144.dat`, 2-second `IAudioData`,
DirectSound ring, `SetState(RUN)` then `Pause`), streams 2 and 3 never pumped,
stream 1 `Run` (`004d1870`) then the `004d34b0` drain interleaved with the
`004d0700` state machine (`Update(ASYNC,0,0,0)` two attempts,
`CompletionStatus(0,0)` advancing on `S_OK` only). A 15 s per-step watchdog
prints `REPLICA_HUNG step=`; the runner then `sample`s and kills the process.
Compact record: `verification/results/bottle-X3/voice-startup-replica.json`
(EXE `8dfa7d5f…`, raw output under `/tmp/x3-voice-startup-{r1,c1,r2,r3}`).

| Run | Environment | Outcome |
| --- | --- | --- |
| `plugin-v3-game` | v3 plugin | completed, 3 graphs, 5 full 176,400-byte buffers in 87 ms, 5.56 s wall |
| `control-game` | no plugin | completed, all three `OpenFile` fail `80040217` (VFW_E_CANNOT_CONNECT), 2.05 s |
| `plugin-v3-game-dwell3000` | v3, 3 s pump-only dwell after each construction and before play | completed, 11.1 s |
| `plugin-v3-explicit-dwell3000` | v3, `AddSourceFilter`/`FindPin("Output")`/`Render` route then `OpenFile` fallback | completed (`Render` succeeds, 138–263 ms), 11.1 s |

**The hang does not reproduce.** What the replica establishes against the
run-31 evidence (`/tmp/x3-gst-run31.log`, `/tmp/x3-run31-sample-game.txt`):

* Every `OpenFile` creates **three** `asfdemux` instances, the first two torn
  down about 0.5 ms after creation before any decoder is autoplugged
  (`pad_removed_cb: No pin matching pad`), the third connecting `avdec_wmav2`.
  Run 31 shows exactly this pattern once (`asfdemux0`/`1` at 2.948/2.951 s,
  `asfdemux2` + libav at 3.004–3.041 s): the game built **one** stream at
  load, not three, so the savegame `MOVI` restore explanation of section 3 is
  not supported by that log.
* In the run-31 sample the game's main thread (`Thread_26904311`) is in FEX
  JIT code for all 2539 samples with **no host frame** — no syscall, no
  `ntdll.so`, no `win32u.so`. It is not blocked in quartz, amstream,
  winegstreamer or the kernel; it is running pure x86 user code (game or PE
  DLL) for the whole sample. The `004d3532` drain loop is excluded (it calls
  `NtUserPeekMessage`/`NtUserGetMessage`), as is any `Sleep`-based wait.
* The pipeline threads are in the no-consumer state section 7 predicts:
  `multiqueue2:src_0` blocked in `gst_ffmpegauddec_handle_frame → gst_pad_push
  → winegstreamer.so+0x98c8` (sink chain waiting for the PE side to take the
  buffer), one PE thread in `winegstreamer.so+0x76f4 → pthread_cond_wait`, one
  PE thread in `NtWaitForSingleObject`, `wine_qz_async_reader_io` inside
  `read()`, `wine_qz_graph_worker` idle in `NtUserGetMessage`. This state is
  also reached by the replica's never-pumped streams without any effect on the
  main thread.

What the replica cannot model is therefore the game step itself: the main
thread's pure user-mode loop after the single open. The frozen sample carries
host JIT addresses only; the guest-address bucketing of section 8 needs a
sample with x86 program counters (or an in-game hang witness that records
`EIP`), which no fixture can provide without launching the game.
