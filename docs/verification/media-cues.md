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

