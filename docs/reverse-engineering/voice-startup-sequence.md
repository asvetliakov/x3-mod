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
| `plugin-v3-game-ds` | v3, mode `game-ds` (EXE `45206752…`: section-10 DS init, visible 640×480 window, mono pre-`OpenFile` format, no `get_Duration`, `004d1d40` teardown) | completed: primary `0xd1` first try, listener present, `Play`/`SetFormat` `S_OK`; all `stream_run` `S_OK` (39–42 ms), 5 buffers in 86 ms, 6.58 s wall; no `E_FAIL`, no hang |
| `plugin-v3-game-ds-stereo` | v3, `game-ds-stereo` (pre-`OpenFile` `SetFormat` nChannels 2) | all three `OpenFile` fail `80040217` although `avdec_wmav2` is autoplugged (12 libav log lines): the 2-channel audio pin cannot connect the mono decoder output; teardown (Stop/Stop/STOP twice) clean, 3.05 s |
| `control-game-ds` | no plugin, `game-ds` | all `OpenFile` `80040217`, teardown clean, 3.04 s |

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

## 10. DirectSound initialisation and the game-vs-replica gap (2026-09-14)

Same provenance as the header; instruction listings to `/tmp/x3-voice-ds/`
(untracked). Import/GUID resolution by direct PE parse of the installed EXE
(SHA-256 `fdbf3418…`, `.rdata` VA `0x00532000` → file `0x130c00`) and of the
bottle's `syswow64\dsound.dll` export table.

**The global DirectSound object.** `004de060` (audio init, called once from
`00402780` at `004033e1`, after the main window exists) is the only writer of
`0x608aec`; `004de3b0` is the only clearer. Sequence, all on the main thread:

| Site | Call and arguments |
| --- | --- |
| `004de10d` | `CoInitialize(NULL)` — STA, HRESULT ignored |
| `004de141` | `DSOUND.dll` ordinal 11 = **`DirectSoundCreate8`**(`&DSDEVID_DefaultPlayback` at `0x5628a8` = `{DEF00000-9C6D-47ED-AAF1-4DDA8F2B5C03}`, `&0x608aec`, `pUnkOuter=NULL`). Failure nulls `0x608aec` and clears config bits `0x5` |
| `004de180` | `GetCaps(DSCAPS{dwSize=0x60})` (vtable `+0x10`) |
| `004de196` | `SetCooperativeLevel(HWND 0x608ab0, 2 = DSSCL_PRIORITY)` (vtable `+0x18`); failure aborts init |
| `004de201` / `004de253` / `004de2a1` | **primary** `CreateSoundBuffer(DSBUFFERDESC{dwSize=0x24, dwFlags=0xd1, rest 0}, &0x608af0, NULL)`; on failure retried with `dwFlags=0x11` then `0x1`. `0xd1` = PRIMARYBUFFER\|CTRL3D\|CTRLPAN\|CTRLVOLUME, `0x11` = PRIMARYBUFFER\|CTRL3D |
| `004de2cc` | `QueryInterface(IID_IDirectSound3DListener {279AFA84-4981-11CE-A521-0020AF0BE560} at `0x5628c8`)` on the primary buffer → `0x608af4`; failure is tolerated (pointer nulled) |
| `004de2e7` | primary `Play(0, 0, DSBPLAY_LOOPING)`; only on `S_OK` does the rest run |
| `004de2fd` / `004de30f` | primary `SetFormat(&0x608af8)`, `GetVolume(&0x608d14)` (restored by `004de3b0` `SetVolume` at `004de3dd`); then `004de450` |
| `004de332`…`004de390` | listener `SetDopplerFactor(1.0f, DS3D_IMMEDIATE)`, `SetDistanceFactor(_0x565624, IMMEDIATE)`, `SetRolloffFactor(_0x565620, IMMEDIATE)`, `SetPosition(0,0,0, IMMEDIATE)`, `CommitDeferredSettings()` |

`0x608af8` is the WAVEFORMATEX initialised at `004b8740`: PCM, 2 ch, 44100 Hz,
176400 B/s, align 4, 16 bit, `cbSize` 0 (`004b7400` can overwrite it from
config). `0x608ab0` is the game's single top-level window, created at
`004dae0d` `CreateWindowExA(0, cls, cls, style, 0, 0, 640, 480, …)` after
`RegisterClassA` at `004dadd1`, with a real WndProc and `WS_VISIBLE` in every
style branch (`0x90000000` fullscreen, else `0x10000000|0xca0000`).

**Everything on the media object between `OpenFile` and `SetState(RUN)`**
(`004cf460`, object `EBX`, stack WAVEFORMATEX at `[ESP+0x40]`):

| Site | Call |
| --- | --- |
| `004d01ea` | `if (0x608aec == NULL) goto fail` |
| `004d0209` | `IAMMultiMediaStream::GetMediaStream(MSPID_PrimaryAudio `0x532b84`, &+0x18)` (vtable `+0x10`) |
| `004d0223` | `QueryInterface(IID_IAudioMediaStream {F7537560-A3BE-11D0-8212-00C04FC32C45} `0x532ad4`, &+0x1c)` |
| `004d023a` | `IAudioMediaStream::GetFormat(&wfx)` (vtable `+0x24`); `+0x60 = nAvgBytesPerSec*2000/1000`, `+0x68 = wBitsPerSample>>3` |
| `004d026a` | `+0x5c = alloc_004b89f0(flags=0, size in ESI = +0x60)`; freed with `_free` in `004d1a89`, i.e. the CRT-heap branch |
| `004d028f` | `CoCreateInstance(CLSID_AMAudioData {F2468580-AF8A-11D0-8212-00C04FC32C45}, NULL, CLSCTX_INPROC_SERVER, IID_IAudioData {54C719C0-AF60-11D0-8212-00C04FC32C45}, &+0x54)` |
| `004d02af` | `IAudioData::SetBuffer(cbSize=+0x60, pbData=+0x5c, dwFlags=0)` (vtable `+0x0c`) |
| `004d02c6` | `IAudioData::SetFormat(&wfx)` — the *same* struct `GetFormat` filled (vtable `+0x1c`) |
| `004d02e8` | `IAudioMediaStream::CreateSample(pAudioData=+0x54, dwFlags=0, &+0x58)` (vtable `+0x2c`); `+0x64 = 1` |
| `004d034c` | `IDirectSound::CreateSoundBuffer(DSBUFFERDESC{dwSize=0x24, dwFlags=0x10088 (LOCSOFTWARE\|CTRLVOLUME\|GETCURRENTPOSITION2), dwBufferBytes = nAvgBytesPerSec*5000/1000, dwReserved=0, lpwfxFormat=&wfx, guid3DAlgorithm=GUID_NULL}, &+0x44, NULL)` on the **global** object; `+0x48 = 1` |
| `004d038a` / `004d03a5` | graph `+0x78` QI `IID_IMediaPosition` `0x532b14` → `+0x70`, `IID_IMediaControl` `0x532b24` → `+0x74` |
| `004d03c4` | `IMediaPosition::CanSeekForward(&local)` — **no `get_Duration`** |
| `004d03de` | QI `IID_IMediaEvent` `0x532b04` → `+0x6c`, **skipped** when `+0x8c & 0x48` (always skipped for voice, bit `0x40`) |
| `004d03f5` | `IAMMultiMediaStream::SetState(STREAMSTATE_RUN)` |

Before `OpenFile` the same constructor does `AddMediaStream(NULL,
MSPID_PrimaryAudio, dwFlags = ~(+0x8c >> 6) & 1, &+0x18)` at `004cf5e0`
(= **0** for voice, bit `0x40`), QI `IID_IAudioMediaStream` at `004cfb1a`, then
`IAudioMediaStream::SetFormat` at `004cfba7` (vtable `+0x28`) with a
WAVEFORMATEX copied from `0x608af8` but overridden to PCM / 16 bit /
`nSamplesPerSec = 0x608afc` / `nChannels = 1 or 2` (2 only when the
constructor reached `004cfaf8` through `004cf8bf`, which sets the local stereo
flag; the other route `004cf99f` sets a different slot and leaves 1), with
`nBlockAlign = nChannels*2` and `nAvgBytesPerSec = nBlockAlign*rate`;
`GetFilterGraph(&+0x78)` at `004cfbe0`.

**Game vs replica** (`verification/probe/voice_startup_replica.cpp`):

| # | Game | Replica | Verdict |
| --- | --- | --- | --- |
| 1 | primary buffer `0x608af0` created (`004de201`, flags `0xd1`/`0x11`/`0x1`), `Play(LOOPING)`, `SetFormat(44100/2ch/16)`, 3D listener with Doppler/distance/rolloff/position set | none: no primary buffer, no `SetFormat`, no `IDirectSound3DListener` | **largest gap.** The device is left at its default mix format and 2D mixer |
| 2 | window is visible (`WS_VISIBLE`), 640×480, real WndProc, created before `DirectSoundCreate8` | `CreateWindowW(WS_OVERLAPPEDWINDOW)` 64×64, `DefWindowProcW`, never shown (l. 220) | focus-dependent; the stream buffer has neither `GLOBALFOCUS` nor `STICKYFOCUS` |
| 3 | pre-`OpenFile` `SetFormat` channel count is route-dependent (1 or 2) | hardcoded `{PCM,1,44100,88200,2,16,0}` (l. 104) | matches only the mono route |
| 4 | `CanSeekForward` only | `get_Duration` then `CanSeekForward` (l. 138–139) | extra call on the paused graph |
| 5 | `DirectSoundCreate8(&DSDEVID_DefaultPlayback,…)`, `DSSCL_PRIORITY`, `CoInitialize(NULL)` STA, main thread | identical (l. 217, 222, 223) | **no difference** |
| 6 | `DSBUFFERDESC` `0x24`/`0x10088`/`avg*5`/`&wfx`/GUID_NULL; `SetBuffer(avg*2)`; `SetFormat(wfx)`; `CreateSample(data,0,&s)` | identical (l. 127–134) | **no difference** |

**`SetState(RUN)` failure path.** `004d03f9 JL 004d0160` jumps to
`MOV EAX,EBX; CALL 004d1d40`, i.e. destruction runs **synchronously on the
constructing (main) thread**, inside the same STA, with the HRESULT already
discarded. `004d1d40` order: `IDirectSoundBuffer::Stop(+0x44)` →
`IMediaControl::Stop(+0x74)` → `IAMMultiMediaStream::SetState(STREAMSTATE_STOP)`
(`+0x04`) → `Release(+0x70)`, `Release(+0x74)` → helper `004d1c20` →
`Release(+0x78)` (graph) → `004d1a40` → `004d1b70` → `Release(+0x04)` → `free`
the `0xb4` record. `004d1a40` repeats `Stop(+0x44)`, `IMediaControl::Stop(+0x74)`
and `SetState(STREAMSTATE_STOP)` on `+0x04` — so `Stop`/`SetState(STOP)` are
each issued **twice**, the second time after the graph reference at `+0x78` was
released — then frees the PCM buffer `+0x5c` and releases `+0x44`, `+0x6c`,
`+0x58`, `+0x54`. The replica's teardown only calls `SetState(STOP)` (l. 186)
and never `IMediaControl::Stop`, so it cannot reproduce a `Stop`-side hang.

**Replica after closing gaps 1–4** (runs `plugin-v3-game-ds*`, `control-game-ds`
in the section 9 table; modes `game-ds`/`game-ds-stereo` of the replica). With
the primary buffer, listener, visible window, route-dependent pre-`OpenFile`
format, no `get_Duration` and the `004d1d40` order in place, the first voice
stream's `SetState(RUN)` still returns `S_OK` under the v3 plugin, so the
`E_FAIL` at `004d03f5` is not produced by any of rows 1–4. The stereo route
fails earlier, at `OpenFile` (`VFW_E_CANNOT_CONNECT`), which would surface as
`fatal=open_file` in the game, not as a `SetState` failure; the game's observed
`E_FAIL` therefore points at the mono route with a difference the replica still
does not carry: the `004b7400` config override of `0x608af8`/`0x608afc`, the
`004d1c20` helper, or process state (loaded DLLs, other DirectSound buffers,
graph count) the game has at load. The two-round Stop/Stop/`SetState(STOP)`
teardown completes in under 2 ms per stream on both the success and the
`OpenFile`-failure paths; no `REPLICA_HUNG`.

Unknown: which constructor route voice takes (hence `nChannels` 1 vs 2), whether
`004b7400` rewrites `0x608afc`, and whether `DAT_00606f34+0x100 & 0x4000`
(`004cf5a4`) is set at load. None of these is settled without a run.

## 11. Every `SetState(RUN)` → `E_FAIL` path in the Wine source (2026-09-14)

Source `wine-11.15` (`/tmp/x3-wine-src/wine`, untracked). CrossOver Preview
27.0.0.40921 (`cxpreview-20260821-rc2`) ships builtin DLLs versioned `11.15`; the
bottle's `syswow64\amstream.dll` is byte-identical to the app's `i386-windows`
copy, and the installed `quartz`/`qasf`/`amstream` carry the upstream trace
strings quoted below. The preview source is not published (403).

**Call chain.** `amstream/multimedia.c:multimedia_stream_SetState` →
`IMediaControl::Run`; the following `GetState(INFINITE)` result is overwritten
with `S_OK`. `quartz/filtergraph.c:MediaControl_Run` returns only the first
failing `IBaseFilter::Pause` of the Stopped→Paused loop (`WARN "Failed to
pause"`, before `graph->state` changes) or the first failing `IBaseFilter::Run`
in `graph_start` (`WARN "Failed to start stream"`); clock, `GetStopPosition`,
`GetState` and `sort_filters` results are discarded. The graph is
`CLSID_FilterGraph` regardless of `AMMSF_NOGRAPHTHREAD` (`create_graph` l. 231);
its MTA message thread only binds monikers; every class involved is
`ThreadingModel=Both` in `system.reg`, so no proxy, apartment wait,
`OpenFile`→`SetState` delay or other graph is on the path.

| E_FAIL producer | Where | On the path? |
| --- | --- | --- |
| `MediaFilter_GetState` filter state ≠ graph state | `filtergraph.c:5246` | reached, **discarded** by amstream |
| `autoplug*`, `wg_parser_connect` | `filtergraph.c:1108–1169`, `wg_parser.c:1791` | `OpenFile` only |
| `dmo_wrapper_init_stream` (`!filter->dmo`, DMO `AllocateStreamingResources`) | `qasf/dmowrapper.c:721,749` | only with a DMO Wrapper in the graph |
| `asf_reader_init_stream`, `file_source_Load` | `qasf/asfreader.c:676,~700` | only with the WM ASF Reader; `.dat` maps to the Async reader (`Source Filter` = `{e436ebb5…}` for every byte pattern) |
| `transform_init_stream` (`wg_transform_create_quartz`) | `winegstreamer/quartz_transform.c:115` | only with a winegstreamer transform filter |
| `filter_WaitUntil` (no clock) | `amstream/filter.c:738` | not reached from `Run` |

For the graph the game's route builds — **MediaStreamFilter, Source (Async
reader), GStreamer splitter filter** — no failure exists: the Async reader has
no state ops, `parser_init_stream` (`quartz_parser.c:1634`) returns `S_OK`
unconditionally (`amt_to_wg_format` is an `assert`, `IMemAllocator_Commit` only
logs), `amstream/filter.c:filter_Pause/filter_Run` return `S_OK`. Replica trace
(run `plugin-v3-game-ds-quartz-trace-cxlog`, `/tmp/x3-voice-startup-trace2-cx.log`,
5.4 MB): every graph is exactly those three filters ("MPEG-I Stream Splitter",
128 ms, and "AVI Splitter", 1 ms, are tried and removed first — the MPEG-I
splitter is a second `wg_parser`, hence three `asfdemux` per `OpenFile`); every
`MediaControl_Run Filter … returned 0` and `graph_start Filter … returned 0`;
`SetState(RUN)` 39–44 ms, 43 ms of it the splitter's `Pause`.

**Ranking.** (1) The game's graph holds a fourth filter whose `Pause` fails
(only qasf's DMO Wrapper/WM ASF Reader and winegstreamer's transform filter
can): `qasf`, `wmvcore`, `mf`, `mfplat`, `devenum` are loaded in the witness
process, though the intro video graph would load them too; consistent with the
quick `E_FAIL` (Pause loop exits before `graph_start`), the busy decode thread
(`wg_parser_connect` leaves streams enabled, so the pipeline prerolls into
`sink_chain_cb` whether or not the graph runs — the replica's never-pumped
streams show the same) and the parked mixer. (2) A CrossOver-private change in
`quartz`/`amstream`. Excluded: timing, apartments, other graphs, the witness
hook (EAX comes from the `PUSHAD` frame at the byte-verified `4d03f7`, and the
game itself took the `JL`). Not reproduced by the replica.

**Witness (no DLL build).** CrossOver's `bin/wine` forces `WINEDEBUG=-all`
unless `CX_LOG` is set (perl l. 228–231; `CX_DEBUGMSG` then gets
`+timestamp,+pid,+seh,+unwind,+process,+module,+loaddll,+threadname` prefixed,
l. 240) — a plain `WINEDEBUG` run (`plugin-v3-game-ds-quartz-trace`) logged
nothing. `tools/manage.py launch` copies the environment and pops only `X3M_*`:
`CX_LOG=/tmp/x3-witness-quartz.log.z CX_DEBUGMSG='-all,trace+quartz,trace+amstream,warn+winegstreamer,+timestamp,+loaddll' python3 tools/manage.py launch --direct --telemetry --game-phases --audio-sites --voice-decoder /tmp/x3-wma-plugin-v3`.
Grep only: `FilterGraph2_AddFilter graph G, filter F, name L"…"` (composition
of the failing stream's graph), `MediaControl_Run Filter F returned 80004005`
or `graph_start Filter F returned 80004005`, the `Failed to pause` / `Failed
to start stream` warns, `filter_Pause filter F L"name"`. If the intro video
makes the log unmanageable, `-all,warn+quartz,trace+amstream,+loaddll` still
separates Pause from Run failure. A DLL witness, if ever needed, reads
`[EBX+0x78]` at `4d03f7` and logs `EnumFilters`/`QueryFilterInfo` names with
per-filter `GetState(0)`.

## 12. Where the DMO wrapper is created, and who owns the RemoveFilter loop (2026-09-14)

Provenance as in the header; GUIDs/xrefs by direct PE parse, listings by
`i686-w64-mingw32-objdump`, untracked in `/tmp/x3-voice-dmo/`; Wine `11.15`.
**GUID constants**, one code xref each, all inside `004cf460`: `0x563ae0`
`DMOCATEGORY_AUDIO_DECODER`, `0x563af0` `CLSID_DMOWrapperFilter
{94297043-bd82-4dfd-b0de-8177739c6d20}`, `0x563b00` the DMO
`{874131cb-4ecc-443b-8948-746b89595d20}` (label `0x563b8c`), `0x54cea0`
`IID_IDMOWrapperFilter`; `{2eeb4adf…}` does not occur in the EXE. **Creation
site: `004cf460`, between `GetFilterGraph` (`004cfbe0`) and `OpenFile`
(`004d0143`).** Object slots: `+0x9c` audio decoder, `+0xa0` video decoder,
`+0xa4` MPEG-I splitter, `+0xa8` source filter, `+0xac` second splitter, `+0x78`
graph.

| Site | Step |
| --- | --- |
| `004cfc32`/`004cfc38` | `EAX = +0x8c`; `TEST AL,8` / `JE 004cfc48` — bit `8` (primary audio never added) diverts |
| `004cfc48` | `CMP [ESP+0x1c],0` / `JE 004cfcaf`. `[ESP+0x1c]` is the stereo-route flag set at `004cfaf0`, read for `nChannels` at `004cfb40`; non-zero takes the MP3 decoder `{38be3000-dbf4-11d0-860e-00a024cfef6d}` at `004cfc6a` instead |
| `004cfcaf` / `004cfcba` | `TEST EAX,0x100` / `JE 004cfd9f`, then `TEST AL,0x10` / `JE 004cfda3`. Both bits required; voice flags are `0x150` (§3), so **voice always takes this branch**. Without `0x100`: MPEG Audio Decoder `0x532b44` at `004cfdbb` |
| `004cfcdf`/`004cfce4` | `CoCreateInstance(CLSID_DMOWrapperFilter 0x563af0, pUnkOuter=NULL, CLSCTX 3, IID_IBaseFilter 0x532aa4, &+0x9c)`; two attempts (`004cfd04 JGE`, `004cfd0a JBE 004cfcd0`), `E_OUTOFMEMORY` → `004b8b60` |
| `004cfd1b`/`004cfd23` | `QueryInterface(IID_IDMOWrapperFilter 0x54cea0, &[ESP+0x14])`; `004cfd27 JL 004cfd68` |
| `004cfd39`/`004cfd3e`/`004cfd44` | `IDMOWrapperFilter::Init(clsidDMO = 0x563b00, catDMO = 0x563ae0)` via `vtable+0x0c`; two attempts (`004cfd5e JGE`, `004cfd66 JBE 004cfd30`) |
| `004cfd70` | `Release` of the `IDMOWrapperFilter` view; `004cfd7c` only picks the name string |
| `004cfe03`/`004cfe15` | `CMP [EBX+0x9c],0` / `JE 004cfe1a`, else **`IFilterGraph::AddFilter(graph +0x78, +0x9c, pName = NULL)`** (`vtable+0x0c`) |

`CoCreateInstance` failing twice leaves `+0x9c` NULL and the filter is silently
not added — the stream is *not* aborted. **A failing `Init` is ignored**:
`004cfd04`, `004cfd27` and `004cfd60` converge on `004cfd68`, and `004cfe03`
adds the filter whenever the pointer is non-NULL — the EXE-side confirmation of
§13. For voice the rest is skipped (`004cfe1a` `& 0x10`, `004cfed4`/`004cff87`
`& 0x48`).

**`004d1c20` is straight-line, not a loop.** `ESI` = object, `EDI` = 0:
`IMediaControl::Stop` (`004d1c38`, `+0x74` `vt+0x24`), `SetState(STOP)`
(`004d1c48`, `+0x04` `vt+0x1c`), then five blocks for `+0xa4`, `+0xa0`, `+0x9c`,
`+0xac`, `+0xa8`:
`RemoveFilter(graph +0x78, slot)` at `vtable+0x10` — `004d1c5e`, `004d1c8c`,
`004d1cba`, `004d1ce8`, `004d1d16` — then `Release` (`vt+0x08`), slot nulled.
The only compares are the NULL guards `004d1c50/52`, `004d1c7e/80`,
`004d1cac/ae`, `004d1cda/dc`, `004d1d08/0a`; **every HRESULT is discarded**, no
backward branch. The EXE holds exactly eight `RemoveFilter` sites; the other
three (`004d1b24` `+0x9c`, `004d1b52` `+0xac`, `004d1bf6` `+0xa0`) are the same
shape. §10's guess that `004d1c20` releases `+0x18`/`+0x1c` is wrong.

**The 3.8 M `RemoveFilter` calls are Wine's loop, not a game retry** (correcting
§13). The game enters it at `004d1dac`–`004d1db2`, `Release([ESI+0x78])` in
`004d1d40` — or at `004d1dc8` (`+0x04`) if amstream holds the last graph
reference. `quartz/filtergraph.c:453` is `while ((cursor =
list_head(&This->filters))) IFilterGraph2_RemoveFilter(…)`, no other exit;
`FilterGraph2_RemoveFilter` returns early **without unlinking** on a pin
`Disconnect` failure (`:711`/`:721`) or a failing `JoinFilterGraph` (`:733` →
`:758`); `source_Disconnect` (`libs/strmbase/pin.c:579`) supplies that failure
with `VFW_E_NOT_STOPPED`, and `MediaFilter_Stop` (`:5082`) cannot clear it while
`graph->state` is `State_Stopped` (§13). There is no game-side retry to bound:
only filter state, the wrapper's presence or `RemoveFilter`'s outcome ends it.

**Replica checklist** (`voice_startup_replica.cpp`; `game-dmo` matches 1–4 bar
the `CLSCTX`):
1. after `GetFilterGraph`: `CoCreateInstance(CLSID_DMOWrapperFilter, NULL,
   CLSCTX_INPROC_SERVER|CLSCTX_INPROC_HANDLER (3), IID_IBaseFilter)` ×2;
2. `QueryInterface(IID_IDMOWrapperFilter)` ×1;
3. `Init({874131cb…}, DMOCATEGORY_AUDIO_DECODER)` ×2, result ignored; release it;
4. `AddFilter(wrapper, NULL)` if non-NULL, *before* `OpenFile`; then `OpenFile`,
   `SetState(RUN)`, `IMediaControl::Pause`;
5. teardown: `IMediaControl::Stop`, `SetState(STOP)`, then a **fixed**
   `RemoveFilter(slot); Release(slot)` per slot, HRESULT discarded — no
   `EnumFilters`, no retry; `remove_filters()` as written is not the game;
6. `Release` the graph, then the `IAMMultiMediaStream`, and time *that release*:
   the hang step to report is `release_graph`, not a removal loop.

Unknown: which filter Wine's loop settles on is not derivable from the EXE
(`list_add_head`); run 12 and the replica observe `MediaStreamFilter`.

## 13. The DMO wrapper the game adds, and why it fails and spins (2026-09-14)

(Section 12, the `0x004d1c20` / DMO-creation disassembly, is written separately.)
User run 12's `CX_LOG` trace (`/tmp/x3-witness-quartz.log.z`, thread `00d8`)
and the `wine-11.15` tree settle §11's ranking (1):

**Who adds it.** The game. Between `GetFilterGraph` and `OpenFile` the game's
own thread calls `DllGetClassObject {94297043…}` (`CLSID_DMOWrapperFilter`,
`outer 00000000`), QIs `IDMOWrapperFilter {52d6f586…}`, calls
`Init(CLSID_CWMSPDecMediaObject {874131cb-4ecc-443b-8948-746b89595d20},
DMOCATEGORY_AUDIO_DECODER {57f2db8b…})` twice (two attempts) and then
`AddFilter(wrapper, NULL)` (joins as `L"0001"`). amstream never adds a decoder
before the source, and quartz creates filters only through `create_filter` on
its message thread. The CLSID is the **Windows Media Speech decoder**
(`wmspdmod.dll`), not the WMA decoder `{2eeb4adf…}` (`wmadmod.dll`, registered
in the bottle). `{874131cb…}` has no `CLSID` key in `system.reg`, so
`qasf/dmowrapper.c:599` `CoCreateInstance` fails `REGDB_E_CLASSNOTREG`
(`0x80040154`) and `filter->dmo` stays NULL; the game adds the filter anyway.

**Why `Pause` fails.** `MediaControl_Run` (graph Stopped) pauses every filter;
`dmo_wrapper_init_stream` returns `E_FAIL` at `dmowrapper.c:721` (`!filter->dmo`)
before `AllocateStreamingResources` (l. 749) or any pin is involved — the
wrapper has no pins at all. `MediaControl_Run` returns that `E_FAIL` with the
MediaStreamFilter and (after the loop continues) the splitter left **Paused**
and `graph->state` still Stopped. On native Windows the class is registered,
`Init` succeeds and the `!dmo` branch never arises; the fatal step is the
missing speech DMO plus Wine's `E_FAIL` for a wrapper without a DMO.

**Why the teardown spins.** `MediaFilter_Stop` (`filtergraph.c:5082`) returns
`S_OK` immediately because `graph->state == State_Stopped`, so nothing stops
the paused splitter. The game's `004d1d40`/`004d1c20` teardown removes `"0001"`
(pin-less, succeeds) and then `RemoveFilter(MediaStreamFilter)`:
`FilterGraph2_RemoveFilter` (l. 678) disconnects the sink's peer first,
strmbase `source_Disconnect` (`pin.c:579`) returns `VFW_E_NOT_STOPPED`
(`0x80040224`) because the splitter is Paused, `RemoveFilter` returns it with
the filter still listed, and **`filter_graph_Release`'s own `while` loop retries
forever** (§12 — the retry is Wine's, not the game's): 3,802,638 `Removing
filter L"MediaStreamFilter"` lines in run 12, 1,086,051 in the replica's 15 s.

**Replica reproduction** (mode `game-dmo`, EXE `575cbd48…`,
`verification/results/bottle-X3/voice-startup-replica.json`):

| Run | `dmo_wrapper_init` | `open_file` | `stream_run` | teardown |
| --- | --- | --- | --- | --- |
| `plugin-v3-game-dmo` | `80040154` ×2, `dmo_wrapper_add` `S_OK` | `S_OK` 1580 ms | **`80004005`** 44 ms | `remove_dmo_wrapper` `S_OK`; `remove_filters` first `RemoveFilter` `80040224`, **`REPLICA_HUNG step=remove_filters`** at 15 s (`/tmp/x3-voice-startup-dmo1`, trace `/tmp/x3-voice-startup-dmo1-cx.log`) |
| `control-game-dmo` (no plugin) | `80040154` ×2 | `80040217` ×2 | not reached | `remove_filters` 3 iterations, clean, 5.6 s |

**Fix (implemented, 2026-09-14): the DMO fallback hook.** `src/proxy/voice_dmo_fallback.cpp`
(`X3M_VOICE_DMO_FALLBACK=1`, set by `manage.py launch --voice-decoder`) patches
the return of the `Init` call, `004cfd46` `mov esi,eax` / `cmp esi,0x8007000e`
(eight bytes, no relative branch, verified by
`verification/probe/verify_voice_dmo_site.py`): when EAX is
`REGDB_E_CLASSNOTREG` it QIs `[EBX+0x9c]` for `IDMOWrapperFilter` and calls
`Init(CLSID_CWMADecMediaObject {2eeb4adf…}, DMOCATEGORY_AUDIO_DECODER)`; a
successful retry is returned to the game in EAX, so `004cfd5e` takes the
Windows path (one attempt) and the wrapper carries a DMO, pauses, runs and is
removed. Proof, replica mode `game-dmo-fallback` (EXE `ebb569c3…`,
`voice-startup-replica.json`): `plugin-v3-game-dmo-fallback` — `dmo_wrapper_init`
`80040154`, `dmo_wrapper_init_fallback` `S_OK` (195 ms first, 0.5 ms after),
`open_file` `S_OK`, **`stream_run` `S_OK` 40 ms**, `control_pause` `S_OK`, play
5 samples / 882000 bytes in 90 ms, `remove_dmo_wrapper` and `release_graph`
`S_OK`, completed in 6.6 s; `control-game-dmo-fallback` (no plugin) — the retry
itself fails (`d0000001`, winegstreamer without the decoder), `open_file`
`80040217` ×2, clean teardown 5.6 s. With the §12 teardown (straight-line
`RemoveFilter`/`Release`, then graph and `IAMMultiMediaStream` release) the
unfixed mode `game-dmo` (`plugin-v3-game-dmo-teardown12`) still hangs, now at
`release_multimedia` (amstream holds the last graph reference; §12's `004d1dc8`
case). Alternatives not needed: `game-dmo-skip` (release the wrapper, skip
`AddFilter`) was not run; (c) bounding the removal has nothing to bound (Wine's
loop); (a) and (d) as before.
