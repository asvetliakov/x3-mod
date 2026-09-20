# Media cues: encoding inventory vs. runtime decoders

2026-09-16. Read-only inventory to scope the run-34 stalling cue
(`docs/reverse-engineering/sector-post-pass.md`). No Wine, no game launch. Cat
directories decoded with `read_catalogue()` from `tools/analysis/inspect_x3.py`
(XOR `^((0xdb+i)&255)` on the directory text only); DAT payloads read by seek,
never whole-file.

## 1. Cue tables (`addon\types\Videos` / `VideoLists`)

Both `03.cat`/`03.dat` (base) and `addon/01.cat`/`.dat` carry
`types/Videos.pck` (5859 B) and `types/VideoLists.pck` (300 B). **Not decodable
here**: neither the CAT-directory XOR, nor the resource-reader's documented
gzip/scramble detection (`0x004e8880`, `docs/reverse-engineering/resource-reader.md`
§"Call contract") match — probed both, result `plain-copy` (no gzip magic after
unscrambling). Trying the sector-post-pass.md record layout (`0x30`-byte
records) against the raw bytes gives 122 exact records only with a 3-byte
header, but every 12-`int32` record decodes to high-entropy noise, not
plausible id/count fields. Both `.pck` files share an identical first 6 bytes
(`2c b8 3b 33 31 38`) despite different content, consistent with a
position-keyed cipher stage applied specifically to `types/*.pck`, distinct
from both known schemes and **not documented anywhere in this repo**. Cue ids,
per-cue mov/soundtrack references and the ambient/music-vs-cutscene split
**cannot be extracted from the archives with current tooling** — this is an
open gap, not a zero-count finding.

## 2. Loose media files: format classes

No `addon\mov\*.dat` or `soundtrack\*.mp3/.wma` entries exist in any `.cat`
directory; all referenced media is loose on disk.

| Dir | Count | Extensions |
| --- | ---: | --- |
| `X3/soundtrack` | 151 | `.mp3` only, no loose `.wma` anywhere |
| `X3/mov` | 18 | `.dat` |
| `X3/addon/mov` | 4 | `.dat` |

**MP3 headers** (ID3v2 skipped by its syncsafe size, then first frame sync
`0xFF Ex`): all 151 are **MPEG Layer III** (real MP3), versions 1 and 2 seen,
bitrates 128–320 kbps, sample rates 44100/48000 Hz, ID3v2 present on ~113 of
151. No non-MP3 audio format found loose.

**`.dat` "movie" headers** (first bytes / start code):

| Class | Count | Example |
| --- | ---: | --- |
| MPEG-PS (pack header `00 00 01 ba`) | 17 | `mov/00800.dat` |
| MPEG-ES video (sequence header `00 00 01 b3`) | 2 | `mov/00001.dat` |
| ASF/WMA (Header Object GUID `3026b275-8e66-11cf-a6d9-00aa0062ce6c`) | 3 | `mov/00144.dat`, `addon/mov/00144.dat`, `addon/mov/00244.dat` |

One MPEG-PS sample (`mov/00800.dat`) parsed past the pack header: system
header (`00 00 01 bb`) lists elementary stream ids `e0` (MPEG video) and `c0`
(MPEG audio, i.e. Layer I/II/III, not AC3/LPCM) — the game's own splitter/
decoder filters (`X MPEG-I Stream Splitter`, `X MPEG Video/Audio Decoder`)
target exactly this. `mov/00144.dat` and `addon/mov/00244.dat` match the
speech archives already characterized in `docs/verification/voice-decoder.md`
(same filenames, PCM tag 1/mono/44100/16 after decode) — these are voice, not
sector ambient cues.

Format classes present: **MP3 (MPEG-1/2 Layer III)**, **MPEG-PS** (MPEG video
+ MPEG audio), **raw MPEG-ES video**, **ASF/WMA v2**. All four are formats the
game's own DirectShow filters decode natively on Windows.

## 3. Decoders available under CrossOver X3

`--voice-decoder /tmp/x3-wma-plugin-v4` sets `GST_PLUGIN_PATH_1_0` to a
runtime holding exactly **one** plugin, `libgstlibav.dylib`, linked against a
private FFmpeg closure (`libx3wma-avcodec.61`, `-avformat`, `-avfilter`,
`-avutil`). Per `docs/architecture/voice-decoder-adapter.md` and
`voice-decoder-recipe.md`, that FFmpeg is built `--disable-everything
--enable-decoder=wmav2`: **wmav2 is the only enabled decoder** — confirmed
again here, `results/probe_wmav2.txt`: `avcodec_find_decoder(AV_CODEC_ID_MP3)
= NULL (expected)`. No mpeg1video/mpeg2video/mp3/mpg123 decoder is linked into
this plugin at all.

CrossOver Preview's own `aarch64` GStreamer plugin set (20 files,
`.../CrossOver/lib/aarch64/gstreamer-1.0/`): `libgstasf` (ASF demux),
`libgstaudioparsers`, `libgstaudioconvert/-resample`, `libgstavi`,
`libgstisomp4`, `libgstmatroska`, `libgstwavparse`, `libgstid3demux`,
`libgsttypefindfunctions`, `libgstplayback`, `libgstapplemedia`,
`libgstosxaudio`, `libgstopengl`, `libgstcoreelements`, `libgstdeinterlace`,
`libgstvideofilter`, `libgstvideoparsersbad`, `libgstvideoconvertscale`,
`libgstdeinterlace`. **No `libgstlibav`, no `mpg123`, no MPEG PS/ES demuxer or
decoder, no MP3 decoder** ships in CrossOver's own set.

| Format class | Demux/parse available | Decode available | Verdict |
| --- | --- | --- | --- |
| ASF/WMA v2 | `libgstasf` (CrossOver) | `avdec_wmav2` (v4 plugin) | **decodes** |
| MP3 (Layer III) | `libgstaudioparsers`/`libgstid3demux` (CrossOver) | none anywhere | **cannot decode** |
| MPEG-PS (video+audio) | none (no `mpegpsdemux` in either set) | none anywhere | **cannot decode** |
| MPEG-ES video | none | none | **cannot decode** |

## 4. Hypothesis

151 of ~173 loose media files (MP3 soundtrack) and 19 of 22 `.dat` movies
(MPEG-PS/ES) have **no decode path in this runtime at all** — not a caps
mismatch but a fully absent decoder/demuxer, in both CrossOver's stock
GStreamer and the project's own minimal FFmpeg build (wmav2-only by design).
Only the 3 ASF/WMA files decode. Given the ratio, the run-96 stalling sector's
winning `Videos` cue is most likely an **MP3 soundtrack cue or an MPEG-PS/ES
movie cue**, not a WMA one — WMA already works (it's what makes speech play).
This is a hypothesis pending the run-34 `--media-cue-trace` cue id/filename;
if it names `soundtrack\*.mp3` or `addon\mov\*.dat` with an MPEG-PS/ES header,
the fix is adding an MP3 (`mpegaudioparse`+decoder) and/or MPEG-PS
(`mpegpsdemux`+`mpeg1video`/`mpeg2video` decoder) path to the same private
FFmpeg/gst-libav build that already carries wmav2 — a build/enablement change,
not a caps negotiation fix. If it instead names one of the three ASF files,
this hypothesis is wrong and the failure is elsewhere (e.g. a DirectShow
filter registration issue distinct from decode capability).

## 5. v5 runtime: MP3/MPEG-1 decode added and verified on the host (2026-09-16)

`/tmp/x3-wma-plugin-v5` is v4 plus the decoders and the demuxer this inventory
found missing (build recipe and hashes: `docs/architecture/voice-decoder-recipe.md`
§"v5: MP3 and MPEG-1"; untracked build record `/tmp/x3-wma-plugin-v5/build-record.md`).
v4 is untouched and remains the rollback target. Launcher change the user makes:
`--voice-decoder /tmp/x3-wma-plugin-v5` instead of `--voice-decoder /tmp/x3-wma-plugin-v4`.

Verified **on the host only**, no Wine and no game launch, with
`/tmp/x3-wma-plugin-v5/results/host_decode_probe` — a small arm64 program linked
against CrossOver's own GStreamer 1.28.4 dylibs and run with
`GST_PLUGIN_PATH_1_0` at the v5 plugins, `GST_PLUGIN_SYSTEM_PATH_1_0` at
CrossOver's plugin directory and `GST_REGISTRY_1_0` at a separate
`registry/host-verify.bin`. Game files were read read-only through `filesrc`;
nothing in the bottle was written.

Elements registered from the private path (rank in brackets):

| Plugin | Features |
| --- | --- |
| `libav` (v5) | `avdec_wmav2`(64), `avdec_mp3`(64), `avdec_mp3float`(64), `avdec_mp2float`(64), `avdec_mpeg2video`(256), `avdeinterlace`(0), `avvideocompare`(0) |
| `mpegpsdemux` (new) | `mpegpsdemux`(256) |

`avdec_mp2` and `avdec_mpeg1video` are deliberately **not** registered by
upstream gst-libav (MP1/MP2 go to `avdec_mp3`, MPEG-1 video to
`avdec_mpeg2video`); both ffmpeg decoders are nevertheless present in the
closure. `mpegaudioparse`(258, audioparsers) and `mpegvideoparse`(257,
videoparsersbad) come from CrossOver's own set, as does `atdec`(64, osxaudio),
an AudioToolbox audio decoder this inventory had not previously accounted for.

Decode results (`fakesink` buffer counts; span = last PTS - first PTS + last
duration):

| Input | Pipeline | Result |
| --- | --- | --- |
| `soundtrack/00005.mp3` (166272 B, MPEG-1 Layer III, 128 kbps, 48 kHz) | explicit `id3demux ! mpegaudioparse ! avdec_mp3` | EOS, 2532 buffers, span **10.128 s** vs 10.136 s computed from bitrate; `S16LE, 48000, 2ch, non-interleaved` |
| same | automatic `decodebin` | decodes, but picks CrossOver's `atdec`: 423 buffers, span 10.139 s, `S16LE 48000 2ch interleaved` |
| `mov/00800.dat` (329820164 B, MPEG-PS) | automatic `decodebin` | `mpegpsdemux ! mpegvideoparse ! avdec_mpeg2video` for video and `mpegaudioparse ! atdec` for audio; 993 video frames `I420 512x512 30 fps` span 33.100 s and 2187 audio buffers span 52.488 s in 0.31 s wall (stopped at the probe's buffer limit) |
| same | explicit `mpegpsdemux ! {mpegaudioparse ! avdec_mp3, mpegvideoparse ! avdec_mpeg2video}` | 36300 audio buffers span **145.200 s**, 4364 video frames span **145.467 s**, 0.92 s wall |
| `mov/00001.dat` (639970202 B, MPEG-1 video ES) | automatic `decodebin` | `mpegvideoparse ! avdec_mpeg2video`, 1348 frames `I420 256x256 30 fps`, span **44.933 s**, 0.11 s wall |
| `addon/mov/00244.dat` (20311023 B, ASF/WMA v2 speech) | automatic `decodebin` | unchanged from v4: `asfdemux ! avdec_wmav2`, 56881 buffers, span **256.580 s**, `F32LE mono 44100 non-interleaved` (the v3 sub-buffer cap is visible in the buffer count) |

So the three format classes that had **no** decode path now all decode on this
runtime, and the WMA speech path is unchanged. The updated capability table:

| Format class | Demux/parse | Decode | Verdict |
| --- | --- | --- | --- |
| ASF/WMA v2 | `asfdemux` (CX) | `avdec_wmav2` (v5) | decodes (unchanged) |
| MP3 (Layer III) | `id3demux`/`mpegaudioparse` (CX) | `avdec_mp3`/`avdec_mp3float` (v5), or CX `atdec` | **decodes** |
| MPEG-PS | `mpegpsdemux` (**v5**) | `avdec_mpeg2video` (v5) + MPEG audio as above | **decodes** |
| MPEG-1 video ES | `mpegvideoparse` (CX) | `avdec_mpeg2video` (v5) | **decodes** |

## 6. Gate on the allocator: `--media-cue-trace`, `--media-cue-cache` (2026-09-16)

Implementation of the hook [media-cue-playback.md](../reverse-engineering/media-cue-playback.md)
§5 qualifies: `src/proxy/media_cue_sites.h` (the one span), `media_cue_core.h`
(value-only policy), `media_cue.cpp` (gate stub, handlers, install, frame
boundary). Default off; nothing is installed and the per-frame cost is one
relaxed load. Cross-compiled for native Windows with documented Win32 only
(`QueryPerformanceCounter`, `GetCurrentThreadId`, `Get/SetLastError`, the
engine-patch `VirtualProtect`/`FlushInstructionCache`); native execution is
unverified like every other EXE-side patch (`docs/architecture/platform-portability.md`).

**Mechanism.** One byte-verified claim on `push ebx; mov ebx,[esp+8]` at
`0x00498140` carries a two-arm gate stub (300 B, one arena block): flags,
EAX/ECX/EDX and XMM0-7 saved, handler `x3m_media_cue_enter(EnterFrame*)`
reads the id at the game's `[esp+4]`, the return address, `[esp+0xc]`,
`[esp+0x10]` and the EAX flag word, and returns PASS (restore, `jmp [next]`
into the claim tail that replays the span at the game's exact ESP) or REFUSE
(restore, `xor eax,eax; ret`: the caller-visible state of a real failed build,
minus the two lifetime allocation counters of §6 of the RE note). A proceeded
call's outcome is captured by **return-address substitution**: the handler
keeps the original return address in a 4-deep pending stack (keyed by the
stack slot, so nested COM-dispatch re-entry pops innermost first, an unwound
entry is `stale`, an unmatched return is `lost`) and points `[esp]` at the
stub's return trampoline, which records EAX (the record or 0) and the clock,
writes the original address back into a reserved slot and `ret`s with every
register and the flags intact. No second patched site: the verifier's
`cdecl_ret`, `esp_writers_known` and the new `no_return_slot_read` (the
routine never reads `[esp]`) are the contract it rests on. Both handlers run
under `LightCallBoundary` (MXCSR + LastError), execute no x87 opcode
(`check_no_x87.py` roots `_x3m_media_cue_enter`, `_x3m_media_cue_return`),
never log and never allocate. Calls before the first Present or from a thread
other than the Present thread pass unobserved (`early`/`foreign`): the trace and
the cache observe owner-thread (Present-thread) calls only. The selector runs on
that thread: the loop-phase stamps of run96 (`docs/verification/sampling-profiler.md`,
"Run 33 session B") recorded `foreign=0`, so the per-sector create path is on
it. An entry unwound past the trampoline is reclaimed as `stale` by the next
call at the same or a higher ESP; a return matching no entry (`lost`,
unreachable by construction) returns to the last substituted real return
address as a crash-avoidance fail-safe, reported once as `media_cue_lost`. Installed
only when `object_trace::executable_verified()`; refused outside the install
window; preflight, ordered claim and reverse rollback through
`stamp::install_group`.

**Telemetry** (`X3M_MEDIA_CUE_TRACE=1`, launcher `--media-cue-trace`,
requires `--telemetry`), drained at the Present boundary on the owner thread:

    media_cue_mode trace_requested= cache_requested= trace= cache= enabled= status= retry_s= cache_entries=32 pending_depth=4 ring=64 lines_per_second=32 window=300 owner=present_thread sector_change=interval_only qpc_frequency= return_trampoline=
    media_cue_site index=0 address=00498140 length=5 rel32=0 patched= status=
    media_cue_enter frame= qpc= id= kind=<0x5a|0x..|none> caller=<selector|speech|script|savegame|query|other> flags= attempt=
    media_cue frame= qpc= id= kind=<0x5a|0x..|none> caller=<selector|speech|script|savegame|query|other> flags= result=<0x<record>|0|unobserved> us= attempts_frame= cached=<0|1>
    media_cue_window qpc= frame= frames= attempts= failures= successes= refused= unobserved= attempts_frame_p50= attempts_frame_max= ids=<id:count,... up to 8|none> id_overflow= cache_used= evictions= depth_max= overflow= stale= mismatched= lost= suppressed= enter_suppressed= dropped= early= foreign=

`media_cue` lines are the first 32 per clock second (`suppressed=` counts the
rest); `qpc=` is the call's clock, alignable through `clock_anchor`; `us=` is
the build duration from entry to the captured return. The `media_cue_window`
line is emitted once per 300 frames whenever the gate is installed (trace or
cache), so the per-frame attempt count (`attempts_frame_p50`/`_max`) is visible
with the trace off; it is not added to the `frame_phases` line.

**Cache policy** (`X3M_MEDIA_CUE_CACHE=1`, launcher `--media-cue-cache on|off`,
**default `on` since 2026-09-17** (run 34 session A2: the ~390 ms/frame stall
of the failing cue disappears with the cache on); `X3M_MEDIA_CUE_RETRY_S`,
`--media-cue-retry-s N`, default 30, 1..3600). Scope: caller `selector` only, meaning `[esp] ==
0x004f6615` and `[esp+0xc] == 0x0045c60c` with kind `0x5a` at `[esp+0x10]`;
speech, script, savegame, query and the other play-helper callers are never
refused and their failures are never cached. A scoped build that returns 0
enters a 32-entry table with its clock; a scoped call for that id inside the
retry interval is REFUSED and counted; the retry is due at the interval
(exactly `now - failed >= retry`), and a retry that fails restarts the
interval. A success for the id from any caller clears the entry. A full table
evicts the oldest failure; a backward clock never refuses. **Sector change is
the interval alone**: the chase-transition sector pointer is a cockpit field
read through the engine-memory reader inside the chase hook context, not a
global reachable from this handler, so there is no sector-change reset.

**Evidence.**

| Check | Result |
| --- | --- |
| `cmake --build build` (MinGW i686, `-Wall -Wextra`; worktree build) | 0 warnings |
| `python3 verification/probe/check_no_x87.py build/d3d9.dll` (worktree build) | PASS, 494 reachable functions, 0 violations, both handlers rooted |
| `PYTHONPATH=verification/probe python3 verification/probe/verify_media_cue_site.py` | PASS, `source_present: true`, 19 checks incl. `no_return_slot_read` (no `[esp]` read, the one `[esp+0x8]` read, no ESP copy, `add esp,N` writers only) |
| `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_game_phase_cpu.py` | PASS, 8521 checks, 0 failures, 6.7 s; `media_cases=9` (incl. the forced-lost fail-safe case) |
| `MEDIA CUE BENCH` (fixture, X3 bottle) | baseline 11.4 ns/call, PASS arm 285.7 (dispatch **274 ns**, entry + return capture), REFUSE arm 130.7 (dispatch **119 ns**) |
| run 34 install candidate (committed main `ee5a406`, DLL `7102a2f1`) | `check_no_x87.py` PASS, 76 roots / 494 reachable, 0 violations, `_x3m_media_cue_enter` and `_x3m_media_cue_return` both walked; `verify_media_cue_site.py` PASS, `source_present: true`; `run_game_phase_cpu.py` PASS, 8521 checks, 0 failures, `media_sites=1`, `media_cases=9`; `MEDIA CUE BENCH` baseline 11.4, PASS arm 291.2 (dispatch **280 ns**), REFUSE arm 127.6 (dispatch **116 ns**); record `verification/results/run34-candidate-build.json` |
| host probe `verification/probe/media_cue_host.cpp` | 154 checks, 0 failures, no allocation (incl. unwound-entry reclaim at push) |
| `python3 -m unittest verification.analysis.test_media_cue ...` (12 modules) | 94 tests OK |

The CPU fixture models the game's frames with emitted cdecl callers (play
helper `push ecx; push [id]; mov eax,[flags]; call; add esp,4; pop ecx; ret`
called by a `push 0x5a` selector or a `push 0x11` other, plus direct speech
and script callers) on a synthetic allocator that opens with the proved five
bytes and records `ebx == [esp+8]`: PASS-arm GPR/EFLAGS/XMM/x87/MXCSR/
LastError/ESP parity against the unhooked baseline under hostile CPU state,
REFUSE arm EAX 0 with the body never run and ESP as a cdecl return leaves it,
observed success then failure then refusal then retry after 100 ms then
success clearing the entry, speech/script/other never refused, a nested speech
call from inside the body (COM dispatch model) at depth 2 with the inner return
popped first and the cache untouched, a foreign thread passing unobserved,
rollback to byte-identical code, preflight refusal on a corrupted byte,
late-window refusal, and a per-frame attempt count reset at the boundary.

**Arena.** The gate needs 324 B (24 B claim + 300 B stub); with every optional
group on the 16,384-byte production arena would have kept 308 B, below the
320-byte largest single reservation the model requires, so the arena grew by
one page to 20,480 (`engine_patch.cpp`; 16,076 modelled with everything on,
4,404 B headroom; `test_game_phase_sites.py`). The fixture arena stays 32,768.

**Default-on policy (2026-09-17).** `--media-cue-cache` defaults to `on`;
`--media-cue-cache off` restores the stock per-frame rebuild. The cache is
independent of telemetry: `media_cue::initialize` sets `cache_on` straight from
`X3M_MEDIA_CUE_CACHE` and gates only `trace_on` on `telemetry::enabled()`, so
with the cache on and `--telemetry` absent the gate is still installed and
refuses (the `media_cue_mode`/`media_cue_window` lines are written by `log`,
which does not consult telemetry; only the per-call `media_cue` lines need
`--media-cue-trace`, which the launcher still requires `--telemetry` for). Only
`--media-cue-trace` carries a launcher precondition; `--media-cue-retry-s` with
an explicit `off` is rejected as before. An unset variable keeps the gate off, so
an inherited environment cannot enable it behind the launcher's back.

What the player loses with the cache on: the first failed build of a cue still
happens; after it, that cue's sound or video is silently skipped in the sector
selector for the retry interval (30 s by default) instead of being rebuilt every
frame. Nothing else is refused - speech, script, savegame and query callers are
out of scope - and a success for the id from any caller clears the entry
immediately.

### Entry-side trace line (2026-09-17, worktree)

Run100/101 showed the outcome-only schema's gap: a build that never returns
leaves no line, so the hung cue is unnamed. With `--media-cue-trace` the gate
now writes one line at entry, before the claim tail replays the span:

    media_cue_enter frame= qpc= id= kind=<0x5a|0x..|none> caller=<selector|speech|script|savegame|query|other> flags= attempt=

`qpc=` and `attempt=` are the same values the matching `media_cue` outcome
line carries, so the pair joins on them; an entry line with no matching
outcome line at the end of a log is the hung build. **Delivery:** the line is
formatted and written synchronously from `x3m_media_cue_enter` to the session
log's OS handle (`log_handle()`, `WriteFile`, the path the fault reporter
already uses), not through the ring, `log()` or stdio, so it reaches the file
even when the callee never returns and the Present thread never drains again.
The handler never takes the log mutex; the CRT formatter runs under
`call_preserved`'s FNSAVE/FRSTOR envelope through an indirect call, so the
handler's audited graph stays x87-free (`check_no_x87.py` reachable 494 -> 495,
0 violations). Because the write bypasses stdio's buffer, the line may appear
before buffered lines logged earlier and can, rarely, split a line stdio
flushed in two pieces: grep for the prefix, not for order. **Bound:** its own
`RateLimit` with the same `lines_per_second = 32`, admitted with the entry's
clock; the rest are counted in the window line's new `enter_suppressed=` field
(the outcome-line limiter is fed the entry clock at drain time, so sharing one
limiter would have reset it on every nested/out-of-order drain). **Refused
calls write no entry line:** the REFUSE arm never runs the allocator, so it
cannot hang, its `media_cue ... cached=1` outcome line already names it, and
the arm keeps its cost. Proceeded-unobserved calls (pending depth exhausted)
do write one, since they run the allocator. The line is written after the
pending entry's clock is taken, so on the at most 32 traced calls per second
the outcome line's `us=` includes the entry line's formatter plus `WriteFile`
(~1.9 us under Wine); a line that cannot reach the file whole (no handle, a
truncated format, a failed or short write) or a zero clock read counts in
`enter_suppressed=` instead of vanishing. **Off:** `trace_on` is the only
predicate on the entry path; no call, no flush. The documented trace-off costs
now live in `media_cue_core.h` (`pass_dispatch_cost_ns = 280`,
`refuse_dispatch_cost_ns = 116`) and the fixture checks its measurement within
2x of them, as the pass/loop benches do.

| Check | Result |
| --- | --- |
| `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_media_cue verification.analysis.test_game_phase_sites` | 25 tests OK, 26.5 s |
| `python3 verification/probe/verify_media_cue_site.py` | PASS, `source_present: true` (site bytes unchanged) |
| `cmake --build build/entry-trace` (fresh configure, RelWithDebInfo) | 0 warnings |
| `python3 verification/probe/check_no_x87.py build/entry-trace/d3d9.dll` | PASS, 76 roots, 495 reachable, 0 violations |
| `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_game_phase_cpu.py` | PASS, 8659 checks, 0 failures, 6.5 s, `media_cases=10` |
| `MEDIA CUE BENCH` (X3 bottle) | trace off: baseline 10.9 ns, PASS dispatch **263.5 ns** (run 34: 280), REFUSE dispatch **110.0 ns** (run 34: 116); trace on, limiter saturated: PASS dispatch 225.8 ns (the predicate and `admit` are inside noise); one admitted entry line **~1.9 us** above the PASS cost (formatter + `WriteFile` syscall under Wine; at most 32 per second) |
| `python3 tools/manage.py launch --dry-run ... --telemetry --media-cue-trace` | env `X3M_MEDIA_CUE_TRACE=1`, `X3M_MEDIA_CUE_CACHE=1`, `X3M_MEDIA_CUE_RETRY_S=30`; no launcher change |

The fixture's new media case stands in for the session log with a
delete-on-close file behind `log_handle()`: the first traced PASS call's line
is compared byte-for-byte (`frame=1 qpc=<entry qpc> id=812 kind=0x5a
caller=selector flags=0x0 attempt=1`) and its byte count is read back from
inside the allocator body, proving the line is on disk before the callee runs;
the early, refused and foreign-thread calls write nothing; speech/script/other
lines carry `kind=none`/`0x11`; 40 rapid calls after a limiter reset give
`lines <= 32` and `lines + enter_suppressed == 40`; the trace-off benchmark's
140,000 proceeded calls write nothing.

### Video blit witness (2026-09-17, worktree)

The RE note's §8.3 places the only D3D9 traffic of a playing video in the
consumer `0x004d0c40`: `dst->LockRect(&lr, NULL, 0)` at `0x004d0d24` and
`dst->UnlockRect()` at `0x004d14b7`, on a surface the proxy handed out. With
`--media-cue-trace` (and `--ownership`, which every user run carries: without
the wrapper no proxy shell sees the game's surfaces) the ownership layer's
`Surface::LockRect`/`UnlockRect` shell reports each call to
`media_cue.cpp`'s witness with the caller's return address taken at the
shell's entry (`__builtin_return_address(0)` in the generated forwarder, so a
helper frame cannot shift it). A caller in `[0x004d0c40, 0x004d14e0)` (the
consumer decodes gap-free to its `ret` before the next function, the pump
`0x004d14e0`; RE note §8.6) is a video blit and writes:

    media_video_blit frame= qpc= texture=%p width= height= format= flags= result=<pending|0x%08lx> stage=<lock_enter|lock|unlock_enter|unlock> blits= unlocks=

Two lines per witnessed call, on the first lock and then one lock in every
60 (`video_blit_line_interval`), and on the first unlock: the `*_enter` line
goes down before the native call with `result=pending`, the result line after
it with the native HRESULT, so a hang inside `LockRect`/`UnlockRect` (an
`_enter` line with no result line) is told apart from a hang in the copy
between them (a `lock` line with no `unlock_enter`) and from a hang before the
first frame ever reached D3D (no line at all). `width/height/format` come from
`GetDesc` on the borrowed native surface (one call per written line, the only
D3D call the witness makes). Delivery is the `media_cue_enter` path: `WriteFile`
to the session log's OS handle, no ring, no `log()`, no mutex; a line that does
not reach the file whole counts as `video_suppressed`. The window line gains
`video_blits= video_unlocks= video_failures= video_suppressed= video_reentries=
video_foreign= video_early=` (per window). Owner thread only: an in-range call
from another thread or before the owner is admitted writes nothing and counts
as `video_foreign`/`video_early`, so a log with no blit line still tells "no
blit" from "blit elsewhere"; a call that re-enters the range between an enter
and its result (COM apartment dispatch inside the native call, §8.6) counts
as `video_reentries`, writes nothing and leaves the outer pair matched. The
shell owns the CPU-state envelope (the layer's `ExecutionState`: x87, MXCSR,
LastError): the game's incoming state is restored before the native call and
the native outgoing state after the second observer call, so the witness
itself guards nothing. The observer is withdrawn when the last device is
destroyed (before the log closes) and at `DLL_PROCESS_DETACH`. The startup
line `media_video_witness registered=1 blit_range=004d0c40-004d14e0
interval=60` confirms registration. **Off:** nothing is registered and the shell's
`Surface::LockRect`/`UnlockRect` pay one relaxed load of the observer slot,
a `test` and a `je` and the return-slot load (four instructions read from the
built object) before the unchanged native forward; the observed arm is out of
line. **On, outside the range:** the call through the observer pointer, the
`active` load and one range compare, twice per call.

| Check | Result |
| --- | --- |
| `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_media_cue verification.analysis.test_capture_device_creation` | 7 + 1 tests OK (`media_cue_host` range/cadence checks, wiring, generator route, fixture labels) |
| `rm -rf build && cmake -S . -B build ... && cmake --build build -j8` (clean worktree configure, RelWithDebInfo; after review fixes) | exit 0, 0 warnings |
| `python3 verification/probe/check_no_x87.py build/d3d9.dll` | PASS, 498 reachable, 0 violations |
| `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_game_phase_cpu.py --no-build` | PASS, 8684 checks, 0 failures, 8.1 s, `media_cases=11` (after review fixes) |
| `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_ownership.py` | baseline 370 and wrapped 563 checks, 0 failures, both exit 0 (run from the worktree; the runner writes `verification/results/bottle-X3/ownership-*`) |

The fixture's video case drives the published observer with synthetic
events through a stand-in `IDirect3DSurface9` that answers `GetDesc` only: a
lock returning to `0x004d0d27` writes the enter and result pair (byte-checked
after the varying `qpc=`: `texture=<p> width=512 height=256 format=22
flags=0x0 result=0x00000000 stage=lock blits=1 unlocks=0`), one from
`0x00401000` or from the range's end bound writes nothing and is not counted,
a call before the owner is admitted or from a foreign thread writes nothing,
119 further locks (one failing) write exactly the blit-61 pair and count one
failure, the first unlock writes its pair and the second only counts, a lock
re-entering the range inside blit 121 writes nothing and leaves the outer pair
matched, the pre-admission and foreign-thread calls count as `video_early=1`
and `video_foreign=1`, and with the trace off `video_lock_observer()` is null.
Not yet observed in the game: the next comm-dialog run with the trace on is
the first real witness.
## 7. Transcoding experiment (2026-09-17)

**Hypothesis.** The game never demuxes a byte itself: it builds
`mov\%05d.dat` and hands the *path* to `IAMMultiMediaStream::OpenFile` or
`AddSourceFilter`+`Render` (`docs/reverse-engineering/media-cue-playback.md`
§8.5). Container and codec are therefore free as long as the file keeps its
`%05d.dat` name and location and the graph can autoplug a decoder whose output
reaches `amstream`'s primary video pin. CrossOver's GStreamer set has no
MPEG-1 decoder at all (§3) but does ship `avi`, `videoparsersbad`
(`h264parse`), `isomp4` and `applemedia` (VideoToolbox `vtdec`, H.264/HEVC);
native Windows DirectShow demuxes AVI and decodes H.264 with the stock
DTV-DVD decoder but has no stock MP4 source filter. **H.264 in AVI is the one
combination plausibly decodable on both targets with no custom codec
runtime**; MP4 is built as the CrossOver-only control.

**Tool.** `tools/media_transcode.py` (host-side, no Wine):

* `probe [--game-dir DIR]` — ffprobe table of every `mov/*.dat`;
* `build --id N [--container avi|mp4] --out DIR` — libx264, `-pix_fmt
  yuv420p`, `-profile:v baseline` (Constrained Baseline in practice), CRF 20,
  `-preset veryfast`, `-threads 0`, `-g 12 -keyint_min 12 -sc_threshold 0`,
  `-bf 0`, `-fps_mode cfr`, source size and frame rate forced, `-an`;
  AVI gets `-vtag H264`. Writes `DIR/NNNNN.<container>.dat` plus a
  `....dat.json` sidecar (source/result sha256, sizes, frame rate, frame count,
  duration, the exact ffmpeg command, wall time) and fails if frame count or
  duration drift by more than one frame, or size/rate change. It refuses to
  write into the game directory.
* `install --id N --from DIR` / `restore --id N` — the only subcommands that
  touch the bottle; both refuse while `pgrep -fl X3AP.exe` matches.

Raw MPEG-1 elementary streams carry no index, so ffprobe's `format.duration`
is bitrate-derived (`00001.dat`: 3011.62 s = size*8/1.7 Mbit) and
`r_frame_rate` is the *field* rate (60 for a 30 fps stream). The tool takes
the frame rate from the demuxer's first packet duration in the stream time
base (40000/1200000 s = exactly 30 fps) and the frame count from
`-count_packets`, which for an ES is one packet per picture; duration is then
`frames / fps`. Hence 30 fps / 90295 frames / 3009.833 s for `00001.dat`, not
the 60 fps / 3011.6 s that a plain `ffprobe -show_streams` suggests.

**Source inventory** (`python3 tools/media_transcode.py probe`, all
`mpeg1video`; `00144.dat` is the ASF voice archive and has no video stream):

| file | size | fps | frames | duration s | MiB |
| --- | --- | ---: | ---: | ---: | ---: |
| 00001.dat | 256x256 | 30 | 90295 | 3009.83 | 610.3 |
| 00002.dat | 512x512 | 25 | 48488 | 1939.52 | 508.9 |
| 00800.dat | 512x512 | 30 | 21600 | 720.00 | 314.5 |
| 00810.dat | 1024x576 | 30 | 7386 | 246.20 | 605.3 |
| 00811.dat | 1024x576 | 30 | 12150 | 405.00 | 999.4 |
| 00812-00822.dat | 1024x576 | 30 | 298 | 9.93 | 12.8 each |
| 00831.dat | 1024x576 | 29.97 | 421 | 14.05 | 12.1 |

**First transcodes** of cue 1, `--out /tmp/x3-media-transcode`, host encode,
no Wine:

| | source | AVI result | MP4 result |
| --- | --- | --- | --- |
| codec | mpeg1video | h264 Constrained Baseline | h264 Constrained Baseline |
| size / fps / frames | 256x256 / 30 / 90295 | identical | identical |
| duration | 3009.833 s | 3009.833 s | 3009.833 s |
| bytes | 639,970,202 | 137,276,460 | 135,196,866 |
| sha256 | `993357ac...15db3713` | `fb6ac196...07501c3a` | `5d2c9c48...c65dc93b` |
| encode wall | - | 7.1 s | 7.3 s |

`frame_count_delta = 0` and `duration_delta_s = 0.0` in both sidecars
(`/tmp/x3-media-transcode/00001.{avi,mp4}.dat.json`); the game directory was
not touched.

**Install / restore** (run by the user or the orchestrator, game not running):

```sh
python3 tools/media_transcode.py install --id 1 --from /tmp/x3-media-transcode --container avi
# ... test, then:
python3 tools/media_transcode.py restore --id 1
```

`install` copies the current `mov/00001.dat` to `mov/00001.dat.orig` first,
prints both sha256s, and refuses when `.orig` already exists and differs from
the current file (i.e. a transcode is already installed - restore first).
`restore` copies `.orig` back, verifies the hash and keeps `.orig` in place.

**What this can and cannot decide.** The run100/101 freeze sits *after*
decoding: §8.5 excludes `Render`/`OpenFile`/`SetState` by evidence and leaves
`CreateSample(NULL,...)`, `GetSurface`, `GetSurfaceDesc`, `Run`,
`CompletionStatus`, `Update` and the two `Lock`/`LockRect` calls - the
DirectDraw-surface and blit side, not the codec. Supplying a decodable
stream may therefore reproduce exactly the same freeze; that is expected and
is not evidence against the transcode. **The blit witness line decides**: if
the per-frame trace reaches `Lock`/`LockRect` and frames advance, the codec
was the only missing piece; if it stops at the same site as run100/101, the
fault is in Wine's `amstream`/DirectDraw path and a transcode cannot fix it.
Also untested here: whether winegstreamer's autoplugged H.264 output
negotiates to `amstream`'s required RGB16/RGB32 primary video pin (§8.5
requirement 4) - a black avatar with advancing frames would point there.

## Open issues

* `Videos.pck`/`VideoLists.pck` encoding is unidentified; cue ids and the
  ambient/music-vs-cutscene split are not established. Needs targeted RE
  (disassemble the `types/*.pck` loader, distinct from `0x004e8880`) before
  the tables can be read directly instead of inferred from file inventory.
  ~19 MP3 files' MPEG-2 bitrate/sample-rate table lookup was not completed
  (version detected only); does not affect the format-class conclusion.
* This inventory does not identify *which* cue the stalling sector selects;
  that requires the run-34 trace naming a media id, per
  `docs/reverse-engineering/sector-post-pass.md` §6.
* **Automatic selection of the audio arm is not ours.** With equal rank 64,
  GStreamer autoplugging picked CrossOver's `atdec` over `avdec_mp3` for both
  the MP3 file and the program stream's MPEG audio; only the demuxer (256) and
  the video decoder (256) are unambiguously ours. Whether the game's
  `IGraphBuilder::Render` arm under winegstreamer ends at the same elements was
  not tested here (no Wine in this task), and no rank override is used.
* **Sub-buffer cap sizing for stereo.** `voice-decoder-subbuffer.patch` caps
  decoder output at 200 samples because 200 samples of *mono* 16-bit PCM is
  400 B, under Wine amstream's 429-byte position-arithmetic bound. Stereo
  48 kHz MP3 sub-buffers are 800 B, so that bound does not hold for soundtrack
  cues; cue-time trimming for MP3 may be imprecise even though decoding is
  correct. Not measured.
* CrossOver ships no `gst-plugin-scanner`, so both plugins are scanned
  in-process (the host probe logs the usual external-loader warning); a plugin
  fault would be taken by the game process.

### Run 34 session A1 (run98), 2026-09-17

Identity: proxy_identity sha256=7102a2f1… matches docs/status.md installed
build (commit ee5a406). `loaded_module`: d3d9.dll from
`C:\windows\system32\d3d9.dll` (187968 B, builtin) and `d3dx9_37.dll` from
`C:\X3\d3dx9_37.dll` size=3786760 (the native game-dir copy, not the
1,646,144 B system32 builtin). `clock_anchor` present:
utc=2026-09-16T19:56:20.260Z qpc=13180193782578 qpc_frequency=10000000.
`media_cue_mode` gate: one site (`00498140`, 5 B, `push ebx`+`mov`), status=ok,
arena/ring healthy (cache=0 per `X3M_MEDIA_CUE_CACHE=0`).

**Cue table.** Session total: 107 `media_cue` lines, 97 `result=0`
(failure), 10 non-zero (success). 93 of those lines are `id=2 kind=0x5a`
(87 `caller=selector`, 2 `caller=other`) plus 4 `id=2 kind=0x520
caller=other`; all 93 are `result=0`. Per-attempt `us=` for the 87
`id=2 kind=0x5a caller=selector` rows: n=87, min=160791, median=392942,
max=448898 (session log lines 79589–92668). `media_cue_window` at
frame=12899: attempts=8 failures=8 ids=2:7,24:1 (one very slow frame,
`loop_phases_slow` frame=12753 dt_us=1394988, 7 retries inside it). At
frame=14099: attempts=4 failures=4 ids=2:4, one retry per frame across
frames 14096–14183 (`game_phase_slow_frame` cluster, covered_ticks up to
4,177,724 = ~418 ms). Per `docs/reverse-engineering/media-cue-playback.md`
§3, id=2 is outside the 100–299/810/840–899 `addon\mov` ranges, so its path
resolves (by the documented rule, not directly logged) to
`soundtrack\00002.mp3` with a `.wma` fallback — the note's open question
"which id the stalling sector selects" is answered as id=2, kind 0x5a
(selector). Baseline (non-stall) attempts (ids 144, 244, 1, 8404, 2004, 31,
8509, 24) mostly succeed (10/17 non-zero `result=`), confirming media cues
normally resolve; only id=2's selector path fails every time it is attempted.

**Alignment.** `clock_anchor` maps qpc→UTC: frame=14100 attempt
(qpc=13182399828226) → 2026-09-16 20:00:00.8646; nearest
`launcher-stderr.log` GStreamer-CRITICAL at `[2026-09-16T20:00:00.848Z]
gst_element_set_state: assertion 'GST_IS_ELEMENT (element)' failed` (16 ms
earlier). frame=14101 (qpc→20:00:01.2205) aligns with
`[2026-09-16T20:00:01.214Z]` (6 ms). frame=14180 (qpc→20:00:34.0211) aligns
with `[2026-09-16T20:00:34.032Z]` (11 ms). 752 of the session's 760
GStreamer-CRITICAL lines fall inside the 19:59:33–20:00:35 UTC window that
brackets the id=2 retries; no other Wine fixme/err naming quartz,
winegstreamer, mp3, mpeg or a file path appears anywhere in the stderr log.

**Frame shape.** `loop_phases` window ending frame=14100: post_p50_us=12,
post_p95_us=20, but max_interval_us=260463 owner=post, slow=5 — the p50/p95
stay tiny because only the handful of retry frames are slow; each slow
frame's `loop_phases_slow` `post_us` (160825–260463) alone accounts for the
observed ~200–260 ms dt, i.e. one media-cue attempt per frame, not several.

**Sanity.** All `media_cue_window` early/foreign/stale/lost/mismatched/
dropped/refused/overflow counters are 0 for the whole session; no
`shader_unknown` lines; chase-camera refusal counters are 0 at frame=0
(no other windows sampled for chase in this session).

**Correction (orchestrator, file check):** the create routine tries
`"%05d.dat"` in `mov\` first and falls back to `soundtrack\%05d.mp3/.wma`
only when that file is absent (media-cue-playback.md §4). `mov\00002.dat`
exists (533,575,370 bytes, header `000001b3`: MPEG-1 video elementary stream), while
`soundtrack\00002.*` does not, so cue id 2 is the **MPEG-1 video elementary stream** `mov\00002.dat`,
not a soundtrack track. Its graph needs an MPEG-1 video decoder, which the v4
runtime and CrossOver's GStreamer set lack (§3) and the v5 runtime adds
(`avdec_mpeg2video`, `mpegvideoparse`): run 34 session A3 is the direct test.

### Run 34 session A2 (run99), 2026-09-17

Cache on (`--media-cue-cache on`), retry_s=30, cache_entries=32 vs run98's
same options with cache=0 (session log `media_cue_mode` line 83 both runs;
`loaded_module` d3d9.dll/d3dx9_37.dll paths/sizes/hashes match run98, only
`hash_us` timing differs — same DLLs).

**Cache behaviour.** Entry into the sector at frame=12643 produces 7 real
attempts (`cached=0`, `result=0`) inside 1.32 s (us=155088–352209 each,
`caller` selector then `other`, `kind` 0x5a/0x520) before the cache commits
(window frame=12899: attempts=8 failures=8 cache_used=1). After that only
two more real attempts occur for the whole 30 s stay: frame=14150
(qpc 13185398691265, +28.9 s from the last frame-12643 attempt, result=0,
us=275326) and frame=16689 (+30.27 s later, result=0, us=249896) — matching
the 30 s retry interval. All other frames in those windows show
`cached=1`/`refused=1` per frame (windows frame=13799..16799: attempts=300
refused=300 per 300-frame window, except the two windows holding a real
retry which show failures=1/refused=299). No ids other than 2 (and one-off
id=24 at the burst) attempted in this stretch.

**Frame time.** `frame_phases` `dt_p50_us`/`dt_p95_us` per 300-frame window
in the sector: frame=12900 22451/33183 us (during the entry burst), settling
to frame=16800 8366/8990 us — no window near run98's ~400 ms or run96's
380 ms average. `frame_phases_slow` shows the entry-burst cost concentrated
at frame=12643 (dt_us=4,370,575, one-time) then frame=12644 (1,781,668),
frame=12668 (326,773); after settling, the two real retries cost
frame=14151 dt_us=290,911 and frame=16690 dt_us=259,777 — single frames, not
sustained stalls. `loop_phases_slow`: exactly 4 lines in the sector range —
frame 12644, 14151, 16690 (all matching real cue attempts) plus an
unexplained frame=15219 (dt_us=92,615, post_us=81,239) whose window
(frame=15299, failures=0, refused=300) shows no cue-2 failure; cause not
identified by media-cue evidence. `game_phase_slow_frame`: 14 lines in range,
clustered at the entry burst (12643/12644/12645/12659/12661/12662/12668)
and at the three retry frames (14151, 14644, 15219 — 15219 recurs here too).

**stderr.** 88 GStreamer-CRITICAL lines in 8 distinct-second clusters (run98:
760 lines) — consistent with roughly one small burst per real attempt
instead of continuous failure. `Warning!  Some triangles have zero area!`
appears 32 times, all at the same timestamp `2026-09-16T20:05:41.51[34]Z`,
at the very end of `launcher-stderr.log` (last 32 of 275 lines), preceded by
the last GStreamer-CRITICAL cluster (20:05:34) with nothing after —
consistent with an EXE shutdown-teardown message flood, not a new symptom:
run98's `launcher-stderr.log` carries the same warning 31 times (verified by
grep), so it is pre-existing at exit in both runs, unrelated to the media-cue
fix.

**Sanity.** All `media_cue_window` early/foreign/stale/lost/mismatched/
overflow counters are 0 throughout; the single `game_phase_window foreign=10`
is a startup-phase (frame=53) transient in both runs, not sector-related.
`mip_bias_fail` count 284 (run98: 239) — present at similar order in both
runs, pre-existing, not attributable to the cache change. No
`shader_unknown`, `claim_fail`, `truncated`, or `chase_refus` lines.

### Run 34 session A3 (run100/run101): freeze under v5

Both sessions (run100 10632 lines, run101 8077 lines) end abruptly mid
normal per-frame telemetry (last frame run100=1437, run101=1007) with no
crash line and no extra `launcher-stderr.log` content beyond the 3-line
msync banner — a hard hang of the main loop, not a decode exception.

`media_cue` lines in both runs are only `caller=query|savegame|script`
(ids 1,144,244,2004,8404,8509, `kind=none`), all completed. **No
`caller=selector` or `caller=speech` line in either run.** This schema
prints only on return; a hang before return of a comm-avatar cue leaves no
line, so evidence cannot distinguish "hung before return" from "this path
isn't reached via the five instrumented callers." No `media_cue_window`
covers the freeze (last window run100 frame=1199, run101 frame=899; next
due ~300 frames later, after the hang), so pending/stale/lost counters that
would show a stuck entry were never re-emitted.

`voice_dmo_fallback` shows 4 speech activations per run (`qi_hr=init_hr=0`),
matching v4-era speech. No evidence v5's GStreamer path loaded: `loaded_module`
lists only d3d9.dll/d3dx9_37.dll, no `GST_PLUGIN_PATH_1_0` in `proxy_options`,
no `X3M_VOICE_DECODER*` key despite `--voice-decoder /tmp/x3-wma-plugin-v5`
on the command line. Does not prove v5 caused the freeze; open gap.

**Next launch should record:** an "entering" line at the graph constructor
(`0x004cf460`) and each CLSID create/`Init` call before the call returns, so
a hang shows as an unmatched entry; and confirmation the launcher applied
`--voice-decoder` (echo resolved `GST_PLUGIN_PATH_1_0` into `proxy_options`).

**Decision (orchestrator, 2026-09-17):** the v5 runtime is parked as a
documented experiment. Under v5 the start-of-session GStreamer criticals are
absent (the graphs now build), and the first comm dialog hangs the main loop
inside a graph build or decode that the return-side trace cannot see; the
comm avatar videos are MPEG program streams whose video branch would go
through Wine's quartz video path, which is beyond this project's decode fix.
The quiet-sector stall is solved by the negative cache (default on since
run 99); the v5 audio decoders bring nothing the cache does not already cover
(cue 2 is a video). Not pursued further unless the user wants avatar or
sector videos; the recipe stays for that case, and the next trace build adds
an entry-side `media_cue_enter` line so a hung build is attributable.


### Run 37 session C (run110), 2026-09-17: H.264/AVI avatar file, freeze reproduced

`mov\00001.dat` replaced by the H.264 AVI transcode (original restored after
the session, hash verified), v4 audio runtime only, `--media-cue-trace
--ownership`. The comm dialog froze the game exactly as run100/101 did with
the MPEG-1 runtime: the script-triggered `media_cue id=1` graph build
succeeded (226 ms), the next frame's telemetry is the last line, and the blit
witness (registered, range `004d0c40–004d14e0`) recorded zero entries, so no
decoded frame ever reached the DirectDraw `Lock`/D3D9 `LockRect` blit. The two
`gst_video_info_from_caps: caps not fixed` assertions on stderr align with
the two graph builds (the startup probe and the comm build's completion), not
with a later `Update`. Remaining candidate blocking sites (RE §8.5):
`CreateSample`, `GetSurface`, `GetSurfaceDesc`, `Run`, `CompletionStatus`,
`Update`; the witness excludes the two lock sites. **Conclusion:** the block
is codec- and container-independent (H.264 and MPEG-1 freeze at the same
post-create stage) inside Wine's `amstream`/DirectDraw hand-off; a
transcode cannot fix it. Pinning the exact site needs entry stamps at the six
calls (three hookable enclosing functions, §8.6); even then the fix would be
a Wine-side workaround or a proxy-side replacement of the media stream
object. **Parked** unless the user wants avatars badly enough to fund that.


## Run50: periodic retries directly explain Argon freezes (2026-09-20)

User run186 (60 referenced files) reports roughly 30-second stutters. Current
selector cache/trace are enabled, retry 30 seconds. Five real selector-ID-2
entries occur at logged frames 1290/3639/6298/8779/11143, separated by
30.350/30.399/30.424/30.440 seconds. Trace frame labels precede the enclosing
measured game-frame labels here; joins use QPC, not log order alone.

Two complete entry/outcome joins return zero after **347.937/397.679 ms**,
wholly inside measured frames 1291/3640 (**359.282/408.326 ms**) and their
Input/sector-post segments (**348.813/398.351 ms**). Each constructor interval
contains eight GStreamer critical lines. These two events directly attribute
the recurring freeze to failed media construction, rather than merely showing
a matching cadence. The three later entries align with frames 6299/8780/11144
(435.586/442.436/429.623 ms) and failure/refusal window counts; their outcome
lines were suppressed, so exact outcome identity/duration is inferred there.

Camera-valid frames 654–11372 contain twelve >100 ms frames. Eleven have a
media entry inside the measured interval: five periodic selector events,
one ID8100 query event and five first-view ID2 `other`/kind0x520 frames
(some have multiple attempts). Frame655 (111.087 ms) remains unattributed;
nearby prior outcomes ended before its interval. The first-view `other` path
is outside the current selector cache, so a longer selector retry interval
cannot be claimed to solve every observed stall.

Trace inventory: 29 entries, 3,946 outcomes, seven entries without logged
outcomes and 6,161 rate-suppressed outcomes. Overflow/stale/mismatched/lost,
entry suppression/dropped/early/foreign are zero. Independent entry/outcome
limits explain why an entry without an outcome is not a hang witness.
Join entry `qpc`/`attempt` to outcome `qpc`/`attempts_frame`.

The installed `mov/00002.dat` was rechecked: 533,575,370 bytes, header
`000001b3` (MPEG-1 video ES). A native FFmpeg decode at ten seconds succeeds
and shows a grid of glowing symbols/panels, an animated-atlas appearance;
this is not evidence that the game's Wine playback hand-off succeeds.
It is not sector music. Existing §run34 file resolution and decoder inventory
explain the missing MPEG decode path under v4; run186 does not expose the
constructor's failing HRESULT/filter. Earlier v5/comm and H.264 playback hangs
remain relevant counterevidence to simply enabling the old decoder experiment.

**Decision:** prepare run51 with only retry 30→3600 changed to separate recurring
selector failures from first-view failures. This is an explicitly temporary
counter that delays legitimate recovery, not a default change or playback fix.
Investigate the non-selector caller and failed-construction semantics before
broadening cache scope. The user has asked whether actual playback can be
repaired; no decoder/stream replacement is qualified by this trace.

Local reproducible evidence: `verification/results/run50-media/` (script,
validated JSON and report); original `/tmp/x3-bottleX3-run186`. Reviewer rerun
of the reproducer matched its JSON byte-for-byte. Run51 launcher dry-run also
passes and differs in that one retry value only. No game/Wine run or installed
file modification was performed by the agent.


## Standalone ID2 playback boundary (2026-09-20)

A separate x86 documented-API fixture reproduces the ID2 flags8 video-only
route without launching X3. It uses the original `mov/00002.dat`, process-local
builtin D3D9, a private GStreamer registry and runtime selection. It does not
enable the experimental decoder in the game. Configuration and seek values
are diagnostic assumptions (config0, start0), not recovered live game values;
the eventual copy target is a managed 512² X8R8G8B8 surrogate.

The frozen fixture source SHA-256 is
`2bcc8ee1b5c96ea06e0adc31648600a1fdeadbe3b80d1142356949285762c7bf`;
GCC16.2.0 builds EXE
`43bf687e9884612c00b7abf164bd66b76a75c2aa6e9f2cfee969a1c425834c1b`
with the required SSE2/four-byte incoming-stack options. Sixteen focused host
tests pass. Source and watchdog/parser contracts received independent deep
review before execution. Independent runtime review reproduced all three parsed
results and verified unchanged protected-file hashes.

All three executions used `X3M_FIXTURE_BOTTLE=X3 python3
verification/probe/wine_lock.py python3
verification/probe/run_media_playback_fixture.py ...`, serially, in CrossOver
Preview bottle X3 (arm64 Wine/FEX; `FEX_X87REDUCEDPRECISION=1`,
`WINEMSYNC=1`). Results:

- v4 constructor: `OpenFile` fails with HRESULT `80040217`; outer exit2,
  11.117 seconds including startup and diagnostics.
- Fixture-only v5 constructor: opens and completes SetState(RUN)/Pause, exit0, 5.980
  seconds. This is constructor success, not successful playback.
- Fixture-only v5 sample-run: constructor and second Pause succeed, then
  `IMediaPosition::put_CurrentPosition(0)` does not return within the ten-second
  call deadline. The watchdog terminates/reaps its own child (`call_seek`);
  outer exit2, 14.322 seconds. Sample creation, frame progress and copy are
  not reached.

These results localize a blocking call in this fixture, not necessarily the
previous comm flight's exact blocking call. Independent review confirms each requested v4/v5 `libgstlibav` plugin path
loaded, with `avdec_mpeg2video` factory creation in both v5 runs. CrossOver
builtin `libgstvideoparsersbad` loads in both versions. This verifies the
plugin/factory witnesses, not every transitive dependency hash; the runner
leaves its manual `backend_modules_verified` field false by design.
Native Windows behavior, Reset and live destination lifetime remain unverified.
Local evidence is `/tmp/x3-media-fixture-v4-constructor`,
`/tmp/x3-media-fixture-v5-constructor` and
`/tmp/x3-media-fixture-v5-sample-run`; the isolated fixture checkout is
`/tmp/x3-media-playback-fixture`. EXE, bottle configuration and installed proxy
hashes match their pre-test values.

For visual identification only, native FFmpeg exported a ten-second PNG and
a twelve-second H.264 preview under `verification/local/media-id2-preview/`.
The user suggests animated billboard icons, consistent with the atlas image
and texture destination path; the consuming object remains unidentified.


### Seek-isolation counter: frame delivery succeeds

One reviewed diagnostic changes only the initial zero-seek: it resumes the
constructor's current position, explicitly logs the skipped call and rejects
nonzero start values. The default fixture retains the original seek sequence.
Twenty-two focused host tests pass. Reviewed variant source SHA-256
`6a8f610262dbdb71f7a45c9ed26c6cb9b14563a38f56d0d30430b0047b877fd7`
builds EXE `6cac12ce3a788077af54db446e76af3e0e7830d0154098a8818dd8a22e025bea`.

The same serialized X3 runner with fixture-only v5, `--stage copy
--skip-zero-seek --diagnostics`, exits0 in **6.491 s**. Six samples have
progressing timestamps and six distinct nonempty copied-frame hashes, each
covering 1,048,576 destination bytes. Update/CompletionStatus and source/target
lock/copy/unlock chains pass the parser; cleanup completes without errors.
The first sample spans 0–400,000 stream ticks and the last 3,200,000–3,600,000.
`diagnostic_variant_accepted` and `variant_frame_delivery_proven` are true;
baseline `accepted` and `playback_proven` deliberately remain false. Evidence:
`/tmp/x3-media-fixture-v5-skip-zero-copy`. Independent runtime review reproduced
the results and verified paired sample/copy chains, cleanup and protected hashes.

This proves decoded frame delivery through the fixture’s DirectDraw-to-D3D9
copy path in this diagnostic. It narrows the blocker to the initial seek in
this sequence; it does not establish a safe seek replacement, nonzero cue
starts, looping, comm playback, game texture lifetime or native Windows
behavior. Do not enable v5 in the game on this evidence alone. Protected game
EXE/configuration/proxy hashes remain unchanged after the counter.

The reviewed fixture and compact four-run record are checkpointed as
`e8071b1e` on isolated branch `experiment/media-playback-seek-2026-09-20`.
They have not been merged into the qualifying candidate.


### Stopped seek: no hang, but target-frame qualification fails

The next independently reviewed diagnostic adds Stop before the original seek,
reads back position while stopped, then restores Pause before the unchanged
sample/Run/copy sequence. Thirty host tests pass. Reviewed source
`3160aefef7c018971652d5e8fc75d4195c55c9d533d8c487316059671383637d`
builds fixture EXE
`f2b8e22e2d8b1f0e2db090d4774e2f5110d0498fb0b48d8f33de1c6a25e823fb`.
The serialized X3 run uses v5, `--stage copy --stop-before-seek --start-ms 10000
--diagnostics`; local result `/tmp/x3-media-fixture-v5-stopped-seek-10s`.

The executable exits 0 in 7.155 s with six progressive, distinct frame copies and
clean teardown. Stop and seek return in 4.692/2.046 ms, stopped position reads
10.000 s, and restored Pause takes 48.715 ms. However, the first sample starts at 0
and five of six frame hashes match frames from the skip-zero run. The parser
correctly rejects the experiment as
`sample_target_or_timestamp_domain_unqualified`; no playback/seek acceptance
flag is true. Independent review reproduces the verdict and paired call chains.

Native FFmpeg frames at 0 and 10 s differ in 178,808 of 1,048,576 BGRA bytes; these
asset frames are not a ten-second repeat. Decoder colour differences prevent
treating native and Wine hashes as interchangeable, but the same-backend frame
matches strongly suggest playback restarted at the head. Position readback
alone is insufficient. The next diagnostic records position after Pause,
sample creation and Run to localize the lost target, without changing playback
behavior. Repeat/loop and live game integration remain unqualified.


Four diagnostic position readings subsequently remain at 10.000 s after Pause,
after sample creation and immediately before Run, and 10.004 s after Run. The
first two copied frames still match zero-position playback. Graph-position
metadata therefore survives these observed boundaries; it does not prove the
source stream sought correctly. This readback-only run also exits cleanly
(7.931 s), with all seek/playback acceptance flags false. Local result:
`/tmp/x3-media-fixture-v5-stopped-seek-positions`, fixture EXE
`b2c7d123abf3d0747822d22a5d9bd6bfbd18182c3e221456b4c6719267167aa5`,
reviewed source `118174b3822227690665ed4b529115de67e1682bb4129c1e5679b22570683db4`.
Thirty-six focused host tests pass. The native content comparison is retained
in `/tmp/x3-media-native-seek-reference/result.json` with both local raw frames.


### Indexed derived-media seek exposes displayed preroll (2026-09-20)

A fixture-only stream-copy Matroska container preserves all 48,488 MPEG packets
and 533,575,370 payload bytes; both payload hashes equal the original file hash
`401e4192e150a848621d60666b2946e9bfa725a17c5a28e231b7cd55098e4c76`.
Generated timestamps fill 16,276 missing packet PTS values. Checked decoded
ordinals are pixel-identical under native FFmpeg. Its output-side ten-second
seek selects adjacent ordinals for raw/container input despite equal reported
decoded timestamps; this is not proof of a true timeline offset. Schema 2
requires explicit generated-timestamp opt-in and keeps original playback and
timeline acceptance false. Original game media remains untouched.

The reviewed first-frame dump fixture and derived input completed six copied
frames with clean cleanup in 6.6183 s (X3, arm64, FEX reduced precision and
WINEMSYNC enabled). The ten-second request reaches Matroska's index: the first
source packet is the 9.4-second keyframe, 44,319 bytes at native packet offset
2,537,960. The decoder receives that packet but emits it at sample time zero.
Graph position still reports ten seconds. The captured destination is uniquely
closest to native decoded ordinal 234 / 9.4 seconds among twelve nearby frames:
RGB MAE 0.4128, maximum channel error 5, versus MAE 3.8746 / max 230 against the
requested ten-second frame. This identifies displayed preroll with small
decoder/conversion differences; it is not exact cross-decoder byte equality.
The parser correctly leaves all original and derived acceptance flags false.

The backend trace's “Wrapped … PTS none” line occurs before timestamp assignment;
it alone does not prove untimed decoder input. The packet, subsequent timestamp
assignment and content witnesses establish the stronger result above. Private
v5 libavcodec 61 and CrossOver's video converter differ from native FFmpeg 9,
so the small residual is not yet assigned to decoder versus color conversion.
A native same-v5 pipeline seek is the next bounded discriminator, retaining the
segment through one pipeline. A proper repair must preserve signed preroll and
segment timing through decoder reorder, discard decoded preroll before display,
and flush old samples on seek/loop. No application call into private Wine
interfaces or decoder hack is authorized by this result.

Local evidence: `/tmp/x3-media-fixture-v5-mkv-seek10/result.json`,
`/tmp/x3-media-remux-mkv/derived-record-v2.json`, and the
[packet/content diagnosis](/tmp/x3-media-mkv-seek-diagnosis.md). The retained
fixture EXE is `fe8c2be662ac77506f7a30be1094c5e40ac8ac2ff45da3f72c9ae5ac63cb93ee`;
58 focused host tests and independent source/provenance review passed.
Original media, EXE, bottle configuration, derived media and its record hashes
were unchanged. No game launch, production decoder change or native Windows
qualification follows from this experiment.


### Native same-v5 pipeline proves preroll reaches the Wine destination (2026-09-20)

The independently reviewed native host fixture uses the actual v5 libgstlibav
decoder and CrossOver 1.28.4 Matroska demuxer, MPEG parser and video converter.
One pipeline decodes sequentially to ten seconds; a fresh pipeline pauses,
issues a ten-second TIME/FLUSH seek and collects six frames. This runs directly
on macOS arm64: no Wine process, bottle, game, installation or production change.
The missing appsink plugin is handled by registering the documented public
GstAppSink type from CrossOver's own libgstapp. Its registry and library search
path are private to the diagnostic child.

The seek's first image is **byte-identical** to the sequential ten-second
reference: SHA256
`b6120226714dc8a5eb1848aec57441038be41a5b473224b53db3d610f58d4c43`.
Six source timestamps are 10.00, 10.04, 10.08, 10.12, 10.16 and 10.20 seconds;
the corresponding running times are 0.00 through 0.20 seconds. Both pipelines
complete cleanup. The native diagnostic qualifies in 0.4652 seconds, with
media, reference and Wine-dump hashes unchanged. These timings are diagnostic
measurements, not game performance.

The retained Wine first-frame dump is also **byte-identical** to the native
sequential sample at **9.4 seconds**, SHA256
`904571754fa004a5afa4f9b00516b0728f573ee643d723e0b9ac01d4c5b75b12`.
This upgrades the prior nearest-frame comparison to exact same-stack content
identity: the Wine graph presented preroll. The same decoder/converter clips
preroll correctly when the segment stays inside a single GStreamer pipeline.
Native same-stack ten-second pixels still differ from the separate FFmpeg 9
reference by RGB MAE 0.4304 / maximum channel error 5; that cross-stack residual
is distinct from the displayed-preroll failure. No tolerance replaces exact
content proof.

The [compact qualification record](../../verification/results/media-native-seek-2026-09-20.json)
binds source, command, selected module paths/hashes, versions, timestamps and
results. Raw images and plugin logs remain local under
`/tmp/x3-native-media-seek-v5-10s-final/`. Five focused host tests and independent
source/evidence review pass. The native fixture is
`verification/probe/native_media_seek_fixture.py`.

This establishes the decoder's seek/clip capability for the indexed derived
container; it does not repair the original elementary-stream seek, Wine graph
segment/preroll bridge or initial seek deadlock. Generated timestamps remain
a separate diagnostic timeline. A required playback provider must retain a
portable documented API boundary and native Windows implementation; no game
texture lifetime, repeat/loop, original playback or native Windows integration
is qualified here.

### Explicit app-local decoder trial: constructor teardown rejected (2026-09-20)

A fixture-only LAV 0.81 x86 source/software decoder now connects explicitly to
the existing amstream RGB32 sink through supported COM activation-context APIs.
The pinned local provider loads correctly; two ConnectDirect calls negotiate
512x512 RGB32, and constructor Run/Pause complete. No registry/merit changes,
autoplug fallback, game installation or production decoder change was made.
**The ordinary constructor remains rejected:** final IMediaControl::Stop times
out under the existing ten-second call watchdog, which terminates/reaps the child.
No reference, seek or playback qualification follows.

After source review, 81 focused tests pass. The corrected owner trace takes
17.242253 seconds and independently verifies provider module closure and unchanged
protected inputs. Its 35,876-byte trace observes selection of amstream's allocator,
a request for four 1 MiB buffers, Commit, the initial zero-start NewSegment, and a
streaming thread entering GetBuffer 1.212 seconds before graph Stop. Stop reaches the sink's
stopped-state entry; no allocator Decommit or Receive is logged. The constructor
has not created a sample or queued Update. This supports an allocator-starvation
explanation, but entry-only tracing does not directly establish the waiting stack,
LAV receive-lock ownership or exact installed CrossOver source identity.

The [compact constructor record](../../verification/results/media-lav-constructor-2026-09-20.json)
binds the frozen EXE/provider, wrapper options and raw result/trace hashes.
Earlier attempts supplied WINEDEBUG/WINEDLLOVERRIDES only through the environment;
inspection showed CrossOver's wrapper replaced/deleted those values. The corrected
LAV adapter uses explicit supported `--debugmsg` and `--dll` arguments. Prior
records prove environment intent, not enforcement; their actual graph/module/
content witnesses retain their separate evidential value. The first empty trace
was not evidence that the backend lacked tracing.

The next bounded diagnostic is terminal-only selected-allocator Decommit before
unchanged Stop, using public interfaces and retained/released COM references.
It is separately labeled and cannot qualify normal construction or playback.
Pre-seek decommit/recommit, flush/epoch correctness, loops, game lifetime/Reset,
performance and native Windows execution remain open.

### Selected allocator Decommit unblocks terminal cleanup (2026-09-20)

A separately labeled constructor diagnostic retains the selected sink input and
allocator through public COM interfaces, then calls Decommit before the unchanged
graph Stop and stream Stop. The original media fixture now returns all 347 calls
and cleans up in **6.931640 seconds**: Decommit 3.158 ms, graph Stop 9.392 ms,
stream Stop 2.386 ms. Both retained references are released. The trace observes
33 renewed GetBuffer entries on the same streaming thread after Decommit, followed
by Stop/flush completion. This establishes a sufficient terminal intervention in
this fixture, not exact internal lock ownership or GetBuffer return values.

The original result was rejected by a parser defect: pinned LAV's runtime-only
software and thread-count setters apply their values, then return S_FALSE when
SaveSettings skips registry persistence. Only these two setters now accept
S_OK/S_FALSE. All other gates remain unchanged; 91 host tests and independent
source/runtime review pass. Host-only reanalysis accepts the terminal diagnostic
using the unchanged executable and raw logs; the original rejected result and all
three hanging ordinary-constructor controls remain preserved. The
[compact terminal record](../../verification/results/media-lav-terminal-decommit-2026-09-20.json)
binds the source, executable, parser, commands and original/reanalysis hashes.

Ordinary constructor acceptance, playback and content flags remain false.
This does not establish a safe seek/decommit/recommit protocol, initial-zero seek,
loop handling, game integration or native Windows behavior. The next step is a
bounded transport design using the selected allocator and sample lifecycle.
No production decoder or installed DLL changed.


### Deterministic transport: zero passes, nonzero content fails (2026-09-20)

The reviewed standalone transport diagnostic now pauses, decommits the selected
allocator, stops, retires any old request, seeks while stopped and restarts with
the provider's own Commit. It keeps the sample and surface across repeated
0/10/0/10-second epochs. This is an explicitly changed diagnostic transport,
not unchanged game playback. Source is retained at `diag/media-transport-v8`
(`ccfadef9`); 106 focused tests and both LAV/default cross-builds pass.

The first v7 exact-content comparison failed despite matching timestamps: every
changed colour byte differed by only one and alpha was identical. Pinned LAV
source defaults to random dithering seeded from time. V8 selects Ordered through
the public runtime settings API before connection and verifies the actual enum;
its reference binding rejects old or missing setting evidence. The exact byte
oracle is unchanged and the rejected v7 runs are preserved.

A fresh original-ES sequential reference delivers 260 frames in 26.741 s. Fresh
zero-seek then matches all six retained reference images and timestamps exactly
in 7.044 s. Repeated 0/10/0/10 transport completes cleanly in 10.242 s, but its
content verdict is **pass/fail/pass/fail**. Both ten-second epochs reproduce the
same six images. Their first three are byte-exact reference images at
10.12/10.16/10.20 seconds; the next three match only retained reference FNV hashes,
since those reference raw images were not saved. The first returned sample reports
zero, so timestamp success alone cannot establish requested content. Pending-
transition testing stopped at this failed gate. Five Commit and eleven Decommit
observations, retained sample/surface identity and cleanup pass; protected inputs
remain unchanged.

Native packet inspection identifies the selected key packet at byte 2733234:
decode timestamp 10.000, no PTS, decoded presentation 10.120. Native seek at ten
seconds independently selects that packet. Together with the pinned provider's
missing-PTS handling this supports a decode-order/presentation-order seek-index
explanation; the exact bundled decoder branch was not directly traced. The
[local diagnosis](/tmp/x3-lav-v8-epochs-diagnosis.md) retains source references,
commands and witnesses. The [compact transport record](../../verification/results/media-lav-transport-2026-09-20.json)
binds all three runtime results and independent review. A bounded seek-repair
design is next; no fixed timestamp offset or relaxed content gate is accepted.
No production decoder change, game playback qualification or native Windows
execution is established.


### Derived-source exact seeking checkpoint (2026-09-20)

The explicit lossless Matroska counter supplies completed presentation timestamps
without changing compressed packet payloads. This is a separate input path; the
original elementary-stream nonzero-seek failure remains rejected. Reviewed
verification source is retained on `diag/media-derived-v9` (`99c50a5d`); 109
focused tests and both provider/default cross-builds pass.

The derived sequential run delivers 260 frames in 28.217489 seconds. All twelve
retained head/target images and their index/time labels exactly match the original
sequential oracle. Repeated 0/10/0/10-second seeking then passes all four epochs:
24 exact images in 11.380830 seconds, with five Commit and eleven Decommit
observations, three settled retirements, retained sample/surface identity and
clean shutdown. Independent review verifies all 36 captures and protected inputs.
The [compact derived-source record](../../verification/results/media-lav-derived-2026-09-20.json)
binds both results, source, executable, commands and bottle provenance.

This qualifies only the tested derived-source content and transport. Reported
source duration differs (1939.56 versus 1940.274073 seconds); whole-timeline and
EOF equivalence are unproved. The diagnostic ten-second transitions took
332.778/375.784 ms to the first completed sample, so correct content is not yet a
stutter-performance solution. Millisecond boundaries and pending cancellation
are next, followed by the engine's seek-only loop behavior and record/target
lifetime. No production decoder change, game playback acceptance or native
Windows execution follows from this checkpoint.
