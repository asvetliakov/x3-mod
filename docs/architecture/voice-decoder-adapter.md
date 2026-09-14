# Optional process-local WMA decoder adapter

Owning note for the optional CrossOver-only audio backend adapter that restores
compressed WMA decoding for X3's target-name speech. The reviewed build recipe
is kept verbatim in [voice-decoder-recipe.md](voice-decoder-recipe.md); the
engine-side diagnosis is in
[voice-stream-creation.md](../reverse-engineering/voice-stream-creation.md).

## Purpose and scope

The game asks its documented COM/media path to open the voice DAT archives
(`addon/mov/00144.dat`, `addon/mov/00244.dat`) and read PCM. Under CrossOver the
shipped GStreamer pipeline demuxes ASF but has no WMA v2 audio decoder, so the
Opens fail and no speech is produced. The adapter supplies that one missing
decoder to the Unix-side GStreamer of the opted-in process only.

- It is a **backend adapter behind a capability boundary**, not a renderer or
  game prerequisite; nothing in production source depends on it.
- **Native Windows keeps the game's documented COM/media path unchanged.** The
  Windows media stack already decodes WMA, so the adapter is a CrossOver-only
  compensation, never a Windows requirement or a Wine-private dependency.
- It adds no plugin, registry, game-file, bottle, application or system write:
  delivery is entirely environment-variable scoped to one launched process.
- Full PCM pre-expansion of both archives is the rejected alternative at about
  4.202 GB; the source-filter/`IAsyncReader` options are the fallback if
  process-local plugin loading cannot be established (recipe, "Alternatives").

## Compatibility boundary

CrossOver Preview ships GStreamer **1.28.4** and GLib **2.78.0** as arm64
dylibs, with 19 plugins including `libgstasf.dylib`, no `gst-libav`, and no
SDK headers or `.pc` files. The Unix side is arm64 even though X3 and the COM
probe are x86. The adapter is therefore built against public GStreamer 1.28.4 /
GLib 2.78.0 APIs only (`GLIB_VERSION_MIN_REQUIRED`/`MAX_ALLOWED` pinned to 2.78),
links every GStreamer/GLib symbol to the **already installed CrossOver arm64
dylibs** so exactly one core and one type system exist in the process, carries
its own private uniquely named FFmpeg closure for the decoder, and uses no
Homebrew runtime library and no private struct layout or offset. GStreamer's
"same major, no newer minor" plugin check is only the first gate; the
import/dependency audit below is what establishes loadability.

## The isolated build

Built 2026-09-14 entirely under `/tmp/x3-wma-plugin/` (untracked); the build
record with all raw audit output is `/tmp/x3-wma-plugin/build-record.md`.

| Component | Version | Identity and how verified |
| --- | --- | --- |
| GStreamer monorepo (core, base SDK, gst-libav) | 1.28.4 | commit `b46f881eaa8126eddfd21b5ae5512f8d4ff36255` fetched from the official GitHub mirror, `git rev-parse HEAD` compared to the recipe |
| FFmpeg | n7.1.5 | commit `3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587`, same method |
| GLib (build-only SDK) | 2.78.0 | GNOME tarball, `shasum -a 256` = `44eaab8b720877ce303c5540b657b126f12dc94972d9880b52959f43fb537b30` |

Toolchain: local venv meson 1.11.2 / ninja 1.13.2 / pkgconf 2.4.3, Apple clang
21.0.0, `MACOSX_DEPLOYMENT_TARGET=14.0`; no global install. gst-libav source is
unmodified; the build-only SDK supplies headers only, and no SDK library is
packaged, rpath'd or searched. FFmpeg is configured LGPL-2.1-or-later, shared,
`--disable-everything` plus `--enable-decoder=wmav2` and `--disable-swresample`
(exact line in the recipe); the generated-configuration audit shows `CONFIG_GPL`,
`CONFIG_NONFREE`, `CONFIG_VERSION3`, `CONFIG_SWRESAMPLE/SWSCALE/POSTPROC/AVDEVICE`
all 0, enabled decoders = 1 (`CONFIG_WMAV2_DECODER`), 0 encoders/demuxers/muxers/
parsers/protocols/filters/bsfs/hwaccels and no external `CONFIG_LIB*`.

Runtime layout, 1856 KB total: `runtime/plugins/libgstlibav.dylib` (342912 B),
`runtime/lib/libx3wma-{avcodec.61,avfilter.10,avformat.61,avutil.59}.dylib`
(422656 / 166208 / 244304 / 715008 B) and the two license texts. The private
dylibs use `@loader_path` IDs and sibling references; the plugin reaches them as
`@loader_path/../lib/...` and the CrossOver libraries as `@rpath/...`.

Process-local environment, and nothing else:

```
GST_PLUGIN_PATH_1_0=/tmp/x3-wma-plugin/runtime/plugins
GST_REGISTRY_1_0=/tmp/x3-wma-plugin/registry/x3-arm64.bin
```

CrossOver's unversioned `GST_PLUGIN_SYSTEM_PATH`/`GST_REGISTRY`, the shipped
plugin directory and all feature ranks stay untouched; no `DYLD_LIBRARY_PATH`.
Removing the two variables and deleting `/tmp/x3-wma-plugin` reverts everything.

Verified statically and on the host: all five artifacts arm64, `minos 14.0`
(equal to CrossOver's `libgstreamer-1.0.0.dylib`); undefined symbols 557 = 452
CrossOver + 87 private FFmpeg + 18 libSystem (18/18 confirmed via `ctypes.CDLL`),
so unresolved imports 0, with 435 `_gst_*`/`_g_*` imports and 0 unresolved by
CrossOver; no `homebrew`, SDK, build-tree or `cxoffice` load command anywhere;
host `dlopen` returns `avcodec_version` 4002661 (61.19.101), decoder `wmav2` /
"Windows Media Audio 2", `AV_CODEC_ID_MP3` correctly NULL, and
`DYLD_PRINT_LIBRARIES` shows 12 libraries from CrossOver plus 5 from
`/private/tmp/x3-wma-plugin/runtime`, no second core.

Not verified: no **cryptographic signature** verification was performed —
provenance is the commit SHAs fetched over HTTPS from the official mirrors plus
the GNOME tarball SHA256. Symbols were checked against the union of allowed
providers, not per two-level-namespace ordinal. The build-only `gstconfig.h`
differs from CrossOver's in `GST_DISABLE_CAST_CHECKS` and had
`GST_DISABLE_PARSE` corrected by hand, so it is not a byte-identical CrossOver
SDK; neither symbol is used by gst-libav.

Two review findings constrain reuse: the plugin's single `LC_RPATH` is the
**absolute** `/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/aarch64`,
so the artifacts resolve only under that exact application install (another
location or version needs a rebuild or load-command edit); and CrossOver ships
**no `gst-plugin-scanner`**, so registry scanning is in-process and a plugin
fault is taken by the host process. The private registry is written only under
`/tmp/x3-wma-plugin/registry`.

## Fixture evidence so far

`/tmp/x3-voice-sync-local-wma-r1/result.json` (bottle X3, WineArch arm64,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`), the retained synchronous voice
probe re-run with only the two environment variables added:

- both `00144.dat` (269633863 B) and `00244.dat` (20311023 B):
  `created/opened/metadata/outputs/closed = 1`, `open_hr 00000000`,
  `fatal none`, `cleanup 1`;
- negotiated media type on both: PCM `tag 1`, mono, 44100 Hz, 16-bit, block
  align 2, 88200 B/s, 0 extra bytes;
- stderr names the selected element "libav Windows Media Audio 2 decoder"
  (2 occurrences), i.e. `avdec_wmav2` from the private plugin;
- open cost 321.0931 ms wall for 00144 and 3.5838 ms for 00244; process wall
  3.575 s, exit code 0.

The retained negative control is the prior R3 run without the plugin, where both
Opens returned `E_FAIL` (`0x80004005`). `restored_speech` is still `false`: the
probe proves decoder availability, not audible speech.

Next step, owned by another agent: the corrected **native null-event sample
contract on both actual voice DATs through the native graph** — nonzero PCM at
the 10 s and 60 s cues, repeated and backward seeks, cold and warm open, cleanup
and no-audible-output checks. Result will land at
`verification/results/bottle-X3/voice-native-actual.json` (not yet produced).

## Open decisions

- **Delivery at game launch.** The two variables must reach the game process
  through the launcher's own environment, process-locally, with no application,
  bottle or global write and no change to CrossOver's unversioned GStreamer
  variables. Where the artifacts live for a non-`/tmp` opt-in, and how the
  absolute `LC_RPATH` is re-established for the user's install, are undecided.

  `tools/manage.py launch --voice-decoder DIR` implements that delivery and is
  off by default. It validates `DIR/runtime/plugins/libgstlibav.dylib` and
  `DIR/runtime/lib`, creates `DIR/registry` if missing (the only write it
  makes anywhere) and requires it to be a writable directory, then sets exactly
  `GST_PLUGIN_PATH_1_0=DIR/runtime/plugins` and
  `GST_REGISTRY_1_0=DIR/registry/x3-arm64.bin` in the launched process
  environment — never `DYLD_LIBRARY_PATH`, never the unversioned
  `GST_PLUGIN_PATH`/`GST_REGISTRY`/`GST_PLUGIN_SYSTEM_PATH`, and nothing in the
  application, bottle or global configuration. An invalid `DIR` is refused with
  a message and a nonzero exit before anything is launched. `--dry-run` prints
  both variables alongside the `X3M_*` environment; without the option the
  launch command and environment are unchanged. Host coverage is
  `verification/analysis/test_voice_decoder_launch.py`.
- **Cue timing.** The synthetic control's sample timestamp interval is
  95.1304 ms for 100 ms of PCM, and the native game trims cues by start/end
  timestamps, so correct decoding does not by itself make cue timing correct.
- **Acceptance.** Decoder success proves neither restored target-name speech nor
  any improvement in selection latency; a user run with the adapter loaded and
  the selection-pause measurement are required before any claim.
- **Rank.** `avdec_wmav2` registers at `GST_RANK_MARGINAL`, so autoplugging picks
  it only when nothing better exists — intended here, but it makes selection
  sensitive to any other plugin appearing. No rank override is used or proposed.

## Limits

This is a diagnostic build plus one synchronous probe: no gameplay integration,
opt-in UX, install candidate or launcher change exists. No codec pack is
redistributed and no patent assurance is implied; FFmpeg and gst-libav are
LGPL-2.1-or-later, dynamically linked, with corresponding sources and the exact
configuration retained under `/tmp/x3-wma-plugin/src` and `logs/`.

## Gameplay attempt 2026-09-14: load hang

Runs 29–31 with `--voice-decoder /tmp/x3-wma-plugin` stop on the loading screen
at session frame 3 with no sound; the control launch without the option loads.
The run 31 `GST_DEBUG=3` log shows the plugin loading, three ASF streams being
built, the libav decoder connecting on the third at 3.04 s, then an unhandled
`convert` query and silence. The frozen-process sample is being triaged; the
adapter is not usable in the game until the cause is fixed and this section is
updated.

## Load hang witness build

One consolidated diagnostic build records, from inside the process, where the
main thread loops during the hang. Both mechanisms are default-off and change
nothing when unset. `--profile --profile-raw` (`X3M_PROFILE_RAW=1`) makes the
sampling profiler log, once per thread per report period, the raw
`Eip/Esp/Ebp/SegCs`, `ContextFlags` and 32 stack dwords (each with module
index + RVA when it falls in a pinned executable range) whenever a sampled
thread's EIP resolves to no pinned module (the constant `module=0xffff
rva=0x10000` leaf of runs 29–35), plus `profile_raw_failure` lines with the
`SuspendThread`/`GetThreadContext` error codes and per-report failure counts on
`profile_thread` (`docs/verification/sampling-profiler.md`).
`--game-phases --audio-sites` (`X3M_AUDIO_SITES=1`) adds fourteen byte-verified
markers to the game-phase group (`src/proxy/game_phase_sites.h`, indices 33–46,
qualified by `verification/probe/verify_game_phase_sites.py`): the returns of
`SetState(RUN)` (`4d03f5`) and `Pause` (`4d0407`) with their HRESULTs, the pump
entry `4d34b0` and its drain-loop body `4d3532` (one hit per iteration), the six
`00498370` manager-update call sites (`403b04` is the existing `services`
marker), the refill entry `4d0700`, the `CompletionStatus` poll (`4d0762` call
setup paired with the `4d0774` join, HRESULT bucketed as S_OK / MS_S_PENDING /
MS_S_ENDOFSTREAM / other), the `Update` return after `4d0a61` and the cue play
entry `498e30`. Every marker counts `total/current-frame/last-frame` (frames
delimited by the main-loop head `403ab0`); the whole group installs
transactionally and rolls back on any byte or claim mismatch, exactly as the
phase markers do. One `game_phase_audio scope=window` line accompanies each
`game_phase_window` report and, because the hang stops frame progression at
session frame 3, the sampler thread also writes `scope=timed` every 2 s
(`--profile` required for the timed line). Launch:
`python3 tools/manage.py launch --direct --telemetry --game-phases --audio-sites --profile --profile-raw --voice-decoder DIR`.

## Timing correction builds (2026-09-14)

The ratified [cue timing correction](voice-cue-timing-correction.md) is applied as
`voice-decoder-subbuffer.patch` on the private gst-libav copy. Build v3 under
`/tmp/x3-wma-plugin-v3/` (backup `artifacts/wma-plugin-v3` in the resume
directory) caps output buffers at 200 samples and sets a 500 ms decoder
tolerance; the native probe then reports anchor errors within 0.02 ms and
1000 ms spans within 0.02 ms (`verification/results/bottle-X3/voice-native-actual-v3.json`,
v1 error up to 701 ms, v2 47 ms). v3 is the gameplay candidate once the load
hang above is fixed; v1 and v2 stay for rollback.


## Alternative: native WMP10 codecs in the bottle

Instead of a process-local adapter, the bottle can carry Microsoft's own WMA
codecs. That was tried on 2026-09-14 with user authorization (full record in
`docs/verification/bottles.md`): `wmp10` cannot install into bottle X3, which
is a `win64` prefix, so `winetricks --unattended wmp11` installed the XP x64
package and its WOW64 32-bit `wmvcore`, `WMASF`, `wmadmod`, `MFPLAT`,
`WMSPDMOD` and `l3codecp.acm`, with `native` overrides for `wmvcore`, `wmasf`,
`mfplat`, `wmp`, `wmpnssci`, `wmplayer.exe` and `l3codeca.acm`. It does work
for the reader interface: the retained sync probe opens both voice archives
with `hr=0`, reads metadata and reports PCM 44100/1/16 with no winegstreamer
trace in stderr. It does not work for the route the game uses: the
native-update probe and the startup replica both fail to build a DirectShow
graph for the `.dat` archives, `hr=80040217` (`VFW_E_CANNOT_CONNECT`) on every
stream, zero reads and zero bytes, so there is no anchor error to compare with
v1 (-701 ms) or v3 (0.02 ms). Native quartz filters and the native ASF/WMA
DMOs do not connect under this Wine build, and replacing `mfplat` bottle-wide
is a large, non-portable side effect besides. This alternative is therefore not
a substitute for the adapter: the adapter stays process-local and keeps working
on stock Wine and on native Windows, whereas this route requires a
redistributable install per machine, changes every process in the bottle, and
in its current state leaves the game's own voice path unable to open a stream.
The bottle was reverted the same day - all thirteen `native` overrides deleted
and `gdiplus.dll` restored from the CrossOver builtins, with the
`DllOverrides` section diffing identical to the pre-change `user.reg` - and the
sync probe reconfirms the two known points: plain, both archives fail
`open_hr=80004005` at `sync_open`; with the v3 libav plugin on
`GST_PLUGIN_PATH_1_0`/`GST_REGISTRY_1_0`, both open with `hr=0` and report PCM
44100/1/16. The adapter remains the route to pursue.
