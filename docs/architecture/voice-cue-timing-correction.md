# Voice cue timing correction

2026-09-14. Design note for the main session to ratify or reject. Read-only:
no build, no Wine execution, no production edit. Depends on
[voice-cue-timing.md](../reverse-engineering/voice-cue-timing.md) (how the game
trims a cue), [voice-decoder-adapter.md](voice-decoder-adapter.md) (the opt-in
decoder and its constraints) and the public-source study under
`/tmp/x3-voice-stream-study/` (untracked).

## The problem in one paragraph

A voice cue is `(s, l)` ms from the XML index. The game seeks to `s - 500` and
then decides both the pre-roll trim and the cue end **only** from
`IStreamSample::GetSampleTimes` (`004d07cd`), with exact integer-ms compares.
Under CrossOver the reported start of each 1 s refill lags a byte-linear clock
by up to 0.70 s (measured, 90 reads), so the first `e` ms of a line is cut and
the line over-runs by `e` into whatever follows. Public
`amstream/audiostream.c::stream_time_from_position` explains it:
`receive->start_time + (receive->position * 10000000 + bps/2) / bps` with
`position` a `DWORD`, so the product wraps every 429.5 bytes and each wrap
drops `2^32 / 88200` = 4.8696 ms. The reported time is therefore pinned to the
**start of the upstream media sample** plus a 0–4.87 ms sawtooth; the error
magnitude is proportional to how far into the upstream sample the refill
boundary falls (up to the whole sample duration), the wrap only sets the
quantum, and the error is exactly zero whenever the sample is at most 429 bytes
(`429 * 10^7 + 44100 < 2^32`). `receive->start_time` is re-read from
`IMediaSample::GetTime` for every upstream sample, so nothing accumulates.

Re-reduction of the 96 `VOICE_READ` rows this session (short Python over the
local raw stdout, nothing read whole): interior windows (head and midpoint
seek, 80 reads) have anchor errors in **[−701.4, +0.8] ms**; the only positive
errors (+18.8, +24.6 ms) are in the two 5 s **tail** windows seeded before end
of stream. Per-read spans range 545.6–1574.7 ms, so upstream sample sizes vary
per packet and no single sample length fits (the "~0.7 s" is only the bound).
Two facts from the pinned 1.28.4 SDK narrow the residual: `GstAudioDecoder`
ships `DEFAULT_TOLERANCE 0`, i.e. it re-syncs output timestamps to every input
packet's ASF presentation time (ms resolution), which accounts for the ≤ 1 ms
interior residual; and `gst-libav` emits one buffer per `AVFrame`, while
ffmpeg's `wmadec.c` sets `frame->nb_samples = nb_frames * frame_len`, i.e. one
buffer per WMA **superframe** (up to 15 × 2048 samples = 696.6 ms at 44.1 kHz,
which with the 4.87 ms sawtooth matches the −701.4 ms floor). `wg_parser`
adds only `audioconvert` between the decoder and its sink and queues buffers
one at a time (`sink_chain_cb`), so the decoder's buffer boundaries are the
media-sample boundaries seen by `amstream`.

## Options

### A. Decoder-side: bound the output buffer to ≤ 429 bytes of PCM (recommended)

CrossOver ships no `audiobuffersplit` (19 plugins listed; none splits audio),
`wg_parser` builds `decodebin → audioconvert → sink` with no hook for an
extra element, `avdec_*` exposes no output-size property, and
`GST_PLUGIN_FEATURE_RANK` only reorders factories. The only process-local lever
is therefore the **private gst-libav build we already own**: one local patch in
`ext/libav/gstavauddec.c::gst_ffmpegauddec_frame` replacing the single
`gst_audio_decoder_finish_subframe(outbuf)` with a loop that emits the decoded
frame in chunks of at most `N` samples (`gst_audio_buffer_truncate`, exported by
CrossOver's `libgstaudio-1.0.0.dylib`, handles planar layouts and the
`GstAudioMeta`; the base class documents repeated `finish_subframe` per input
frame and assigns each sub-buffer's PTS/duration from its own sample counter,
so the patch does no timestamp arithmetic). Proposed `N = 200` samples: 400
bytes of mono 16-bit PCM at the negotiated format, under the 429-byte no-wrap
limit with margin; a sample here is one frame across all channels, so a stereo
16-bit format would be 800 bytes and would need a smaller cap, but the game
never negotiates one on this path. That makes the arithmetic **exact** (rounding to 100 ns)
rather than "< 20 ms"; the 10 ms fallback (`N = 441`, at most two wraps,
error in (−9.74, 0] ms) is only worth taking if the per-buffer cost below
proves measurable.

- Correctness bound: reported start/end exact to the decoder's own timeline;
  residual equals the ASF ms-resolution packet times (≤ 1 ms interior,
  observed). `t1 - t0` equals the byte duration, which also removes the
  `t1 < t0` negative-head hazard at `004d0959` for this path.
- Hot path: **nothing on the game thread.** Cost is about 220 buffers/s on
  Wine's streaming threads (audioconvert of 200 samples, one `wg_parser`
  mutex/cond handshake, `get/copy/release_buffer`, one `IMemAllocator` sample,
  one `Receive` → 400-byte `memcpy`), estimated well under 5 ms CPU per second
  of audio; not measured. Loading time unchanged (no extra work at open).
- Native Windows: the plugin does not exist there; the game keeps Microsoft
  `amstream` on the documented COM path and this note adds nothing to the
  shared source. Windows cue accuracy stays the shipped behaviour, unverified.
- Portability rule: the patch lives inside the CrossOver-only adapter behind
  the existing `--voice-decoder` capability boundary, uses public GStreamer
  1.28 API only, and reverts with the two environment variables. The recipe's
  "gst-libav source is unmodified" becomes "one documented local patch"; the
  LGPL modified source stays retained beside the build.
- Risk: it treats Wine's arithmetic without touching Wine, and it assumes the
  installed `amstream` matches public source. The acceptance run below tests
  that assumption directly and cheaply.
- Cost: ~20 lines of C, a plugin rebuild (the existing recipe), one native
  probe run.

### B. Game-side trampoline at `004d07d3` (fallback)

Overwrite the 8-byte two-`MOV` prologue after the ignored `GetSampleTimes`
with a jump into the proxy; recompute `t1 = t0 + L * 10^7 / bps` (`bps` =
media object `+0x60 / 2`) and keep a per-media-object byte-linear anchor.
Exact times independent of the runtime, and it would also cover a different
`amstream` implementation. It loses because it needs **two** hook sites: the
anchor must reset on every seek, and a "big jump" detector cannot tell a seek
from continuation when the next cue starts within ~1 s of the previous end
(adjacent target-name fragments do exactly that), so `004d0430` (seek target
in `EDI` after the −500 adjustment) must be hooked as well. It also needs the
stack slot of `L` from `GetInfo` at `004d078d`, which is not yet in the RE note
(disassembly required), plus expected-bytes validation, `VirtualProtect` /
`FlushInstructionCache`, CPU/LastError preservation and rollback in the
pattern of `src/proxy/scene_hook.cpp`. Same opt-in as the decoder; never
installed on Windows. Per-refill cost is one 64-bit multiply, negligible.
Keep as the fallback if A's acceptance fails.

### C. COM shim around the `IStreamSample` at media `+0x58`

Wrap or vtable-hook `GetSampleTimes` so it returns byte-derived times. Same
information problem as B (it cannot see the seek without a second hook and
needs a hook to obtain the object at `CreateSample`, `004d02e8`), plus
`AddRef`/`Release`/`Update`/`CompletionStatus` forwarding and lifetime through
the game's teardown at `4d1a40`. Strictly more risk than B for no gain.

### D. Do nothing until run 10 reports

Run 10 (open in `docs/verification/user-runs.md`) is a valid test of restored
speech and an invalid test of cue timing; it should proceed regardless. It
cannot settle this decision: the listener can only confirm clipped onsets that
the numbers already predict, and a mid-stream `e` up to 0.7 s makes any
"timing sounds fine" report untrustworthy. D therefore does not compete with
A; A's acceptance is a probe, not a user run.

## Recommendation and first bounded step

Adopt **A**. First step: patch the private gst-libav copy under
`/tmp/x3-wma-plugin/src/gstreamer/subprojects/gst-libav/ext/libav/gstavauddec.c`
to emit ≤ 200-sample sub-buffers, rebuild with the existing recipe (unchanged
provenance otherwise), keep the previous plugin for rollback, and re-run the
existing native actual-file probe with the patched plugin:

```
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py \
  python3 verification/probe/run_voice_native_update.py --mode actual ...
```

with the runner extended (verification side only) to emit per-read anchor
error `e = start − (first_start + bytes_before / 88200 s)` and per-read span.

Acceptance, all 90 interior/tail reads of both archives:

1. interior (head + seek) reads: `|e| ≤ 2 ms` and span within `1000 ± 2 ms`
   (today: −701.4 ms and 545.6–1574.7 ms);
2. tail reads: `|e| ≤ 25 ms`, reported separately (the unexplained residual);
3. decoded bytes, `get_Duration`, seek landing, `MS_S_ENDOFSTREAM` and
   `live_root_objects = 0` unchanged from `voice-native-actual.json`;
4. process wall within 10 % of the current 7.97 s as a coarse cost check.

If 1 fails with the wrap gone, the installed `amstream` is not the public one
and B becomes the plan.

## Unknowns and what settles each

- **Installed `amstream` identity** (numerical match only): settled by
  acceptance point 1, or by a targeted disassembly of the installed
  `amstream.dll` `stream_time_from_position` multiply width.
- **Actual upstream sample length** (inferred ≤ 0.7 s, variable): settled
  without Wine by a host harness linking CrossOver's dylibs (the adapter's
  host `dlopen` check already exists) running
  `filesrc ! asfdemux ! avdec_wmav2 ! fakesink` on `00144.dat` with a pad
  probe printing per-buffer PTS, duration and `nb_samples`; run once with the
  unpatched plugin (measures superframes) and once patched (proves every
  buffer ≤ 200 samples and PTS continuity).
- **Positive residual up to +24.6 ms, tail windows only**: the same host dump
  over the last 5 s of each archive shows whether `asfdemux` packet PTS jump
  against cumulative samples at end of file; if so it is an end-of-archive
  effect irrelevant to interior cues and is documented, not fixed.
- **`t1 < t0` hazard at `004d0959`**: absent under A (span exact and positive
  for `L > 0`); it stays unguarded for any other runtime and is not this
  note's problem to patch.
- **Windows cue accuracy**: untestable here; tracked as shipped behaviour in
  `platform-portability.md` if the user ever reports a Windows run.
