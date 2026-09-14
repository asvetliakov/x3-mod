# Voice cue timing: how X3AP trims a speech line

2026-09-14. Read-only targeted RE of the installed `X3AP.exe` plus a reduction
of the already-captured probe result. No Wine execution, no build, no game
launch, no production change. This note owns the cue-definition, seek, trim and
cue-end question raised by `voice-stream-creation.md` ("uses sample start/end to
trim cues", fields `+0x90` / `+0x94`).

## Provenance

Ghidra 12.1.3, project `/tmp/x3-voice-stream-study/Voice.gpr`, opened
`-process X3AP.exe -noanalysis -readOnly`; scripted decompile/instruction dumps
to `/tmp/x3-voice-cue/` (untracked). Addresses are preferred VAs for the
installed EXE used throughout this repository. String constants were read from
the EXE with the section delta `VA - 0x401400 = file offset`, cross-checked
against the `v\%05d` string found by Ghidra at `00561008`. Runtime numbers come
from the existing `verification/results/bottle-X3/voice-native-actual.json` and
its local raw `stdout.txt` (96 `VOICE_READ` rows) — no new run.

## 1. Where a cue comes from

The cue table is XML inside a `.pck`, not an offset table beside the archive.
`addon/mov/00044.pck` is the index for the `00144.dat` / `00244.dat` archives.

| Site | Established behaviour |
| --- | --- |
| `00499480` | Script-VM command (dispatch `004999f2`). Formats the numeric argument with `"%05d"` (`005616a8`), prefixes `"addon\"` plus the configured movie directory (`DAT_00606f34+0xd8`, `+0x90`), loads it as type `"pck xml"`, then `xmlReadMemory` + `xmlDocGetRootElement`. |
| `004993c0` | Per root child: requires element name `language`, reads attribute `id` (`atol`). Allocates the per-language hash table at `*DAT_00606f44 + 0x14 + language*4` on first use, then visits `page` children. |
| `004991a0` | Per `page`: attributes `id` (`00555860`) and `stream` (page-level archive/source id). Per `t` child: `id` (`00555860`), `s` (`00555880`), `l` (`00561698`). Allocates a 12-byte record: `[0] = s`, `[1] = l`, `[2] = stream`. Hash key inside the page is `id + 1`. |
| `00499710` | The lookup, `__fastcall` with ECX = language, EDX = page, stack = text id; returns the 12-byte record or 0. Two chained hash tables (`00499715`, `0049972b`). |
| `00499780` / `004997a0` | Script getters returning `l` (length) and `stream` for a cue. |

So a cue is **`s` = absolute start in milliseconds from the start of the
archive, `l` = length in milliseconds**. Units are confirmed by the seek below
(`ms * 0.001` into a `REFTIME` in seconds). The archive is 44,304.046 s long, so
`s` reaches ~4.4e7 — well inside `int`, but only 3 % from the end of `int` range
is irrelevant here; the relevant point is that absolute values are large.

The play request `00498e30(cb_class, cb_arg, source_id, language, page, text_id)`
is dispatched from the VM at `0049982f`. It calls `00499710` at `00498e4a`,
takes `start = rec[0]`, `end = rec[0] + rec[1]`, and treats `start < 0` as
"no cue" (failure callback). The 0x40-byte MOV record keeps `language` at
`+0x34`, `page` at `+0x38`, `text id` at `+0x3c`, and the resolved
`start`/`end` at `+0x1c` / `+0x20` (used to restart a looping cue at
`00498370`).

## 2. Seek, cue fields and the 500 ms pre-roll

`004d0430(start_ms)` (called at `00498f55` for reuse, `0049840a` for loop
restart, `00498d54` for the create path) operates on the 0xb4-byte media object
at record `+0x24`:

| Site | Effect |
| --- | --- |
| `004d045a` | `IDirectSoundBuffer::Stop` (object `+0x44`, vtable `+0x48`) when already playing. |
| `004d0476` | `IMediaControl::Pause` (object `+0x74`, vtable `+0x20`) before seeking. |
| `004d0491` | state `+0x64 = 1` (request a sample update). |
| `004d0494` | **`+0x90 = start_ms`** — the cue start, in ms, untrimmed. |
| `004d049a` | **`+0x94 = 0xffffffff`** — cue end cleared. |
| `004d04aa`–`004d04c7` | If flags `+0x8c & 0x10` and `start_ms > 500`, the **seek target only** is moved back 500 ms (`ADD EDI,-500`). `+0x90` keeps the true start. |
| `004d04af` | Seek value `= ms * 0.001` (`_DAT_00565500 = 0.001`) → `IMediaPosition::put_CurrentPosition` (object `+0x70`, vtable `+0x20`), retried twice. |
| `004d0540` | Alternative for flags `& 0x20`: `IMultiMediaStream::Seek(ms * 10000)`. Not taken for voice. |

Voice flags are `0x150` (`00498140` picks `0x110`, `004cf460` adds `0x40`; the
probe header records `native_flags=336`). Bit `0x10` is therefore **set for
voice**, so every voice cue seeks 500 ms early and relies entirely on sample
timestamps to trim that pre-roll back off. Bit `0x20` is clear, so
`put_CurrentPosition` is the seek used.

Back in `00498e30`, after a successful seek: `if (end > 0) media+0x94 = end`,
then `004d1870` runs the graph (`IMediaControl::Run`, `+0x74` vtable `+0x1c`).

## 3. The trim itself (`004d0700`)

Per refill, with `ESI` = media object:

| Site | Expression |
| --- | --- |
| `004d078d` | `IMemoryData::GetInfo(NULL, NULL, &L)` on the `IAudioData` at `+0x54` — `L` = bytes actually decoded into the staging buffer. |
| `004d07cd` | `IStreamSample::GetSampleTimes(&t0_100ns, &t1_100ns, &cur)` on the sample at `+0x58`, vtable `+0x10`. **The HRESULT is not tested.** |
| `004d07e4` / `004d07fc` | `EDI = t0 / 10000`, `EBP = t1 / 10000` (`__alldiv`, truncating) — sample start/end in ms. |
| `004d0807`–`004d083f` | `if (t0_ms < cue_start) head = ((L * (cue_start - t0_ms) / (t1_ms - t0_ms)) / step) * step` with `step = +0x68`. |
| `004d0843`–`004d086b` | `tail = L` by default; `if (cue_end > 0 && t1_ms > cue_end) tail = ((L * (cue_end - t0_ms) / (t1_ms - t0_ms)) / step) * step`. |
| `004d0873`–`004d0882` | `n = tail - head; if (n <= 0) { state +0x64 = 1; goto end-check }` — the whole buffer is discarded and another update requested. |
| `004d0890`–`004d0959` | `IDirectSoundBuffer::Lock(+0x4c, n, ...)`, `memcpy(lock, pcm + head, ...)`, `Unlock`, write cursor `+0x4c = (+0x4c + (tail - head)) % +0x28`. |
| `004d09fa` | **Cue end: `if (cue_end > 0 && cue_end <= t1_ms)` → state `+0x64 = 8`, `IMediaControl::Pause`, `+0x48 = 4` (drain), `+0x50` = bytes still queued.** |

Supporting fields, from `004d0244`–`004d0274`: `+0x68 = wBitsPerSample/8`
(2 bytes here, so alignment is a sample, not a block), `+0x60 = nAvgBytesPerSec
* 2` (a **2-second** PCM staging buffer), `+0x5c` = that buffer.
`00469a30(a,b,c)` is a 64-bit `MulDiv` that returns 0 only when `c == 0`.

The audible end is byte-driven: after the Pause the drain loop at `004d0aa0+`
waits for the DirectSound play cursor to consume `+0x50` and only then returns
2, which makes `00498370` fire the completion callback or re-seek a loop.

`004d14e0` confirms there is **no second end test for voice**: the
position-based check (`get_CurrentPosition` → `* 1000.0` → `__ftol2` at
`004d0600` / `004d1670`, compared `+0x94 < current_ms`) sits inside
`if ((flags & 0x10) == 0)`, i.e. the video path. Voice (flags `0x10|0x40`) falls
straight through to `FUN_004d0700`. So for a speech line the cue end is decided
**only** by `GetSampleTimes`, with `MS_S_ENDOFSTREAM` as the only other exit.

## 4. Tolerances

None. Both comparisons are signed 32-bit integer compares in milliseconds
(`004d080d` `CMP EDI,EAX` with `JGE`; `004d084d`/`004d09fa` `CMP`+`JLE`), after
a truncating divide by 10000. The only quantisation is that ms truncation
(≤ 1 ms) and the floor to `+0x68 = 2` bytes (≤ 11 µs). There is no epsilon, no
hysteresis and no cross-check against the byte count, the graph position or a
wall clock.

## 5. What the measured timestamp error does to a line

The "0.1–3 % short per 20 s window" framing understates the per-buffer error.
Reduced from the 96 `VOICE_READ` rows of `voice-native-actual.json`'s raw
stdout (92 reads of exactly 88,200 bytes = 1000.000 ms of PCM each):

* reported span per read: **545.590 – 1574.653 ms**, mean 983.706 ms
  (0.546× – 1.575× of the byte duration);
* anchor error against a byte-linear clock started at each segment's first
  reported start: **−701.39 ms … +24.56 ms** over 90 reads; 61 exceed 100 ms and
  23 exceed 300 ms;
* the error **oscillates and returns to ~0** (+0.80, −0.35, +18.79 ms rows); it
  shows no monotone trend over a 20 s window.

This matches the public-source mechanism already recorded in
`/tmp/x3-voice-stream-study/pcm-update-contract.md`: Wine
`amstream/audiostream.c::stream_time_from_position` computes
`receive->start_time + (receive->position * 10000000 + bps/2) / bps` with
`position` and `bps` 32-bit, so the numerator wraps every 429.5 bytes and each
wrap removes `2^32 / 88200 = 48,696` units = **4.8696 ms**. The reported time is
therefore pinned to the *upstream media sample's* start time plus a 0–4.87 ms
sawtooth, and the maximum error equals the duration of one upstream buffer.
`receive->start_time` is re-read from `IMediaSample::GetTime` for every upstream
sample, so the error **resets at each buffer boundary and does not accumulate**.
The observed −0.70 s bound implies upstream decoder buffers of that order. This
remains an exact numerical match to public source, not a trace of the installed
CrossOver `amstream`.

Consequences for a cue, given the code above:

1. **Onset is cut.** The pre-roll is removed by discarding buffers while
   `t0_ms < cue_start` and partially trimming the boundary buffer. Reported time
   lags the true position by `e`, so discarding continues `e` ms past the true
   cue start: the first `e` ms of the line is lost. The 500 ms pre-roll gives no
   margin — it is removed by the same timestamps.
2. **Tail over-runs.** The Pause fires when `t1_ms >= cue_end`, i.e. `e` ms of
   real audio late, appending whatever follows the line in the archive.
3. Net: the audible window is **shifted later by ~e and is not shortened**;
   with `e` up to 0.7 s and typical speech lines of 1–3 s this is a clipped
   first syllable plus audible bleed-through, not a subtly mistimed line.
4. **No rate or pitch error, no session drift.** Interior bytes are `memcpy`ed
   verbatim and played at the DirectSound rate; `+0x90`/`+0x94` are rewritten
   from the cue table on every seek and every loop restart; `+0x4c`/`+0x50` are
   byte offsets modulo `+0x28`. There is no time accumulator anywhere in the
   path, so a 0–3 % error cannot compound across a play session.
5. **A silent cue is possible but not the expected failure.** If a single
   reported buffer already satisfies `cue_end <= t1_ms` while `tail - head <= 0`,
   `004d0882` jumps straight to the end check at `004d09fa` and the cue ends
   having written nothing. That needs timestamps running *ahead*; the measured
   error is predominantly negative.
6. **Conditional robustness risk.** If a refill were ever shorter than one
   upstream buffer, the sawtooth can report `t1_ms < t0_ms`; `00469a30` guards
   only a zero denominator, so `head` would go negative and `004d0959` would
   `memcpy` from before `+0x5c`. The game's 2-second staging buffer makes this
   unlikely (all 92 measured spans were positive) but it is unguarded.

## 6. What the runtime must guarantee

1. `GetSampleTimes` start/end must be absolute media time on the same timeline
   as the cue table's `s`/`l`, re-anchored by the seek. The probe already shows
   the anchor is right: each segment's `first_start` equals the requested seek
   position exactly, and `get_Duration` equals the declared duration for both
   archives. A *rate*-like shortfall anchored at file start instead would be
   catastrophic, because cue offsets reach 4.4e7 ms and 3 % of that is 22 min.
2. `t1 - t0` must equal the buffer's byte duration to within a sample, since it
   is the denominator of both trims. Currently violated by −45 %…+57 % per
   second of PCM.
3. The absolute error must be small against the silence around a line;
   practically ≤ ~20 ms for an unclipped onset. Currently up to 0.70 s.
4. `t1 >= t0` for every non-empty buffer (see 5.6).
5. Monotonic non-decreasing across refills — already observed (each read's start
   equals the previous read's end).

## 7. Bearing on the next user run, and where a correction would live

The defect delays and clips, it does not silence. A user run therefore remains
the decisive test of *whether speech returns at all*, and should proceed; it is
**not** a valid test of cue timing, and the listener should be told to expect
clipped first syllables and lines running into the next line if the timestamps
are still wrong at that point.

The error is produced downstream of our decoder, in the amstream audio sink's
byte→time conversion, and is bounded by the duration of one upstream media
sample. The portable lever on our side is therefore the **decoder's output
buffer duration**: emitting short media samples (tens of ms) bounds the error to
that duration without touching Wine. Native Windows uses Microsoft's `amstream`
and is not affected by this arithmetic, so nothing here may become a Windows
prerequisite. Patching or overriding `amstream` is not an option under the
portability rule.

If a game-side correction is ever wanted instead, the natural site is
**`004d07d3`**, immediately after the ignored `GetSampleTimes` call at
`004d07d1`: `004d07d3` `MOV ECX,[ESP+0x3c]` (4 bytes) and `004d07d7`
`MOV EDX,[ESP+0x38]` (4 bytes) form an 8-byte, two-instruction, relocatable
prologue at a real instruction boundary. `EAX` (the discarded HRESULT), `ECX`
and `EDX` are dead on entry to the site; `ESI` (media object), `EDI`, `EBP` and
`EBX` are live and must be preserved; flags are dead (the next consumer is the
`CMP` at `004d080d`). The routine runs on the game's owner thread from
`00498370` and is not reentrant with respect to one media object. The 64-bit
times live in stack slots `[ESP+0x38]`/`[ESP+0x40]` at that point. This is a
described candidate, not an installed hook, and it treats a symptom of the
runtime rather than the runtime itself.

## Unknown

* Whether the installed CrossOver `amstream` is byte-for-byte the public Wine
  implementation; the match is numerical only.
* The actual upstream media-sample length of the process-local WMA decoder
  (inferred to be ~0.7 s from the error bound, not measured).
* The sub-wrap residual of a few ms (positive errors up to +24.56 ms) is not
  explained by the 32-bit wrap alone; ASF's millisecond-resolution presentation
  times are a plausible but untested source.
* No speech line has been identified, cued or heard. Nothing here establishes
  restored speech.
