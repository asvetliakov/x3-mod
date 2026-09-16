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
    media_cue frame= qpc= id= kind=<0x5a|0x..|none> caller=<selector|speech|script|savegame|query|other> flags= result=<0x<record>|0|unobserved> us= attempts_frame= cached=<0|1>
    media_cue_window qpc= frame= frames= attempts= failures= successes= refused= unobserved= attempts_frame_p50= attempts_frame_max= ids=<id:count,... up to 8|none> id_overflow= cache_used= evictions= depth_max= overflow= stale= mismatched= lost= suppressed= dropped= early= foreign=

`media_cue` lines are the first 32 per clock second (`suppressed=` counts the
rest); `qpc=` is the call's clock, alignable through `clock_anchor`; `us=` is
the build duration from entry to the captured return. The `media_cue_window`
line is emitted once per 300 frames whenever the gate is installed (trace or
cache), so the per-frame attempt count (`attempts_frame_p50`/`_max`) is visible
with the trace off; it is not added to the `frame_phases` line.

**Cache policy** (`X3M_MEDIA_CUE_CACHE=1`, launcher `--media-cue-cache on`,
default `off` in this build; `X3M_MEDIA_CUE_RETRY_S`, `--media-cue-retry-s N`,
default 30, 1..3600). Scope: caller `selector` only, meaning `[esp] ==
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
