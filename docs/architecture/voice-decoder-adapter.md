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

### Patched builds v2-v4

Each later build is a copy of the previous tree with one more patch on the
private gst-libav source; the FFmpeg closure, the build-only SDK, the packaging
and the audit are unchanged, so the four `libx3wma-*.dylib` stay bit-identical
to v1 in every build. The build command differs only in the tree it is run from
and the source directory: `meson setup build/gstlibav src/gst-libav ...`
(v1 used `src/gstreamer/subprojects/gst-libav`), same flags,
`PKG_CONFIG_PATH=/tmp/x3-wma-plugin/pcshim:/tmp/x3-wma-plugin/ffmpeg/lib/pkgconfig`,
`PATH=/tmp/x3-wma-plugin/tools/venv/bin:$PATH`.

| Build | Patches on `ext/libav/gstavauddec.c` | `libgstlibav.dylib` SHA256 | Build record |
| --- | --- | --- | --- |
| v1 `/tmp/x3-wma-plugin` | none | `2817e175…` | `build-record.md` in the tree |
| v2 `/tmp/x3-wma-plugin-v2` | `gst-libav-subbuffer.patch` (cap only) | `9a851c70…` | same |
| v3 `/tmp/x3-wma-plugin-v3` | `gst-libav-subbuffer.patch` (cap + 500 ms tolerance) | `55f4f87b45b4a2554998b71ba5b6393046cb9f04c0f0cb5c7e71492ea618299f` | same |
| v4 `/tmp/x3-wma-plugin-v4` | v3 patch + `gst-libav-float-limit.patch` | `98334d763a296ff10d57a88753e23c1432a4d61766654f0972459d5c3b879a77` | same |

v4's patched source `ext/libav/gstavauddec.c` is
`f5ade4b3f324d74f2cc84630124361398ed1e4f6e7133b12ebcacd4d260908db`, the patch
itself `1ee93bc47efbc5e27e303525b0d8c90a0beeb867b74dc58d583f3e6d0a0172c8`
(repository copy `voice-decoder-float-limit.patch`). Its undefined-symbol set is
identical to v3's, so it adds no import. The `/tmp` trees of v1-v3 no longer
exist (checked 2026-09-23); v4 is tracked in `tools/voice-decoder/v4/`, and the
backups `artifacts/wma-plugin-v3` (plugin `55f4f87b…`, matches the table) and
`artifacts/wma-plugin-v4` remain in the resume directory
`~/x3-mod-resume-2026-09-14/` (see `tools/voice-decoder/v4/README.md`,
Rollback).

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
  variables. How the absolute `LC_RPATH` is re-established for an install
  where CrossOver Preview is not at `/Applications` is undecided.

  `tools/manage.py launch` implements that delivery for the chosen decoder
  directory (explicit `--voice-decoder DIR` or discovered, below). It validates `DIR/runtime/plugins/libgstlibav.dylib` and
  `DIR/runtime/lib`, creates `DIR/registry` if missing (the only write it
  makes anywhere) and requires it to be a writable directory, then sets exactly
  `GST_PLUGIN_PATH_1_0=DIR/runtime/plugins` and
  `GST_REGISTRY_1_0=DIR/registry/x3-arm64.bin` in the launched process
  environment — never `DYLD_LIBRARY_PATH`, never the unversioned
  `GST_PLUGIN_PATH`/`GST_REGISTRY`/`GST_PLUGIN_SYSTEM_PATH`, and nothing in the
  application, bottle or global configuration. An invalid `DIR` is refused with
  a message and a nonzero exit before anything is launched. `--dry-run` prints
  both variables alongside the `X3M_*` environment; without a chosen decoder
  the launch command is unchanged and the environment carries no decoder
  variable. Host coverage is
  `verification/analysis/test_voice_decoder_launch.py`.

  Shipped copy and discovery (2026-09-23): the v4 build is tracked verbatim in
  `tools/voice-decoder/v4/` (runtime, both patches, hash list, symbol audit,
  build record; provenance and licence position in its `README.md`). The
  registry cache is not tracked; GStreamer rebuilds it on first launch.
  Without `--voice-decoder`, a modded launch now delivers the first valid of
  `<game dir>/x3m/voice-decoder` (the drop-in location for a distribution
  without the repository) and `tools/voice-decoder/v4`; a discovered directory
  gets the same checks as an explicit one (plugin, closure, `registry/`
  created when missing and writable; a dry run creates nothing and reports
  `registry will be created`), and one that fails is skipped with a
  `voice decoder: skipping …` note. Without a chosen decoder, stale shell
  values of `GST_PLUGIN_PATH_1_0`, `GST_REGISTRY_1_0` and
  `X3M_VOICE_DMO_FALLBACK` are stripped. `--voice-decoder none` (the literal
  word; `./none` names a directory) opts out, an explicit `DIR` behaves and
  fails exactly as before, and `--vanilla` never discovers.
  `X3M_VOICE_DECODER_REPO` overrides the repository candidate for host tests
  (empty = none) and is never forwarded; the in-process launcher tests patch
  `VOICE_DECODER_REPO` instead, so no test touches the checkout copy.
  The launch (and `--dry-run`, also as the JSON `voice_decoder` field) prints
  `voice decoder: <dir> (<why>)` or `voice decoder: none (<why>)` on stderr.
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

## DMO fallback hook (2026-09-14)

Root cause of the load hang (startup note §12–13): the game's media
constructor `Init`s a DMO Wrapper with the Windows Media *Speech* decoder
`{874131cb…}`, unregistered in the bottle, and adds the pin-less wrapper
anyway; Wine's `Pause` then fails `E_FAIL`, the splitter stays paused and the
graph's final `Release` spins in `RemoveFilter`. `src/proxy/voice_dmo_fallback.cpp`
is a byte-verified post-call hook at `0x004cfd46` (`mov esi,eax` /
`cmp esi,0x8007000e`, the return of `IDMOWrapperFilter::Init` at `004cfd44`;
site ledger `verification/probe/verify_voice_dmo_site.py`, host test
`test_voice_dmo_fallback.py`). Condition: EAX == `REGDB_E_CLASSNOTREG`
(`0x80040154`). Action, inside `PreserveCpuState` with LastError kept: read the
wrapper from `[EBX+0x9c]` through `engine_memory::read`, `QueryInterface`
`IID_IDMOWrapperFilter`, `Init(CLSID_CWMADecMediaObject {2eeb4adf…},
DMOCATEGORY_AUDIO_DECODER)`, release the view; on success the retry's `S_OK`
replaces EAX in the PUSHAD frame so the game continues as on Windows; on
failure EAX is untouched and the game retries as today. Documented COM only;
one `engine_patch` claim in the install window, preflight bytes, rollback via
`restore` if the stub cannot be chained, refused after the first Present.
Gate: `X3M_VOICE_DMO_FALLBACK=1`, which `manage.py launch --voice-decoder DIR`
sets with the two GStreamer variables; unset by default. Native Windows: the
speech DMO is registered, `Init` returns `S_OK`, the hook only counts a hit.
Log: `voice_dmo_fallback requested=1 installed=… status=…` at install and one
`voice_dmo_fallback activation=N object= filter= qi_hr= init_hr=` line per
activation, formatted after Present from an integer ring. Evidence: replica
`game-dmo-fallback` `stream_run S_OK`, 5 samples, clean teardown (startup note
§13); x87 audit PASS on the built DLL; not yet exercised in the game.
Cost: the first activation loads winegstreamer/mfplat and probes a transform,
about 195 ms once on the main thread inside the constructor; later activations
0.5 ms (loading-time rule: one-time, counted in the next load timing). Windows
exposure is nil unless `X3M_VOICE_DMO_FALLBACK=1` is set, which only
`--voice-decoder` does; on an N edition without the speech DMO the substitution
is the correct decoder for the WMA2 voice files.

**Run 13 crash (2026-09-14) and fix.** The first game execution of the hook died
at session frame 3 with `page fault on execute access to 09870000`. Root cause,
in the compiled code and not in the emitted stub: the hook bound
`IDMOWrapperFilter` as a local abstract C++ class in its anonymous namespace,
and GCC (`-O2`, closed hierarchy for a TU-local type) devirtualised
`view->Init(...)` into a direct `call __cxa_pure_virtual`; that weak symbol
resolved to absolute 0, so the installed DLL (`608b35d8…`, preferred base
`6fb40000`) carried `e8 … call 0` at `6fb9486d` and the relocated image jumped
to `0 + delta` (with the DLL loaded at `793b0000`, exactly `09870000`, the
fault address). The fixture proved it: replica mode `game-dmo-hook`
(`verification/probe/voice_startup_replica.cpp`) installs the production hook
through `engine_patch` on `replica_init_site`, a machine-code copy of
`004cfd0e..004cfd7c` with the game's register contract (EBX media object with
the wrapper at +0x9c, EDI 0, `[ESP+0x14]` view, EAX Init's HRESULT) and the
site's exact eight bytes; built with the old binding (replica EXE
`240073b9…`, record run `plugin-v3-game-dmo-hook-unfixed`, raw
`/tmp/x3-voice-startup-hook6`) it dies inside `dmo_wrapper_init_hooked` with the
witness line `code=c0000005 eip=00000000 … hits=1 activations=1 faults=1`; the
winedbg backtrace of the first such build (`1257cf19…`, not retained, raw
`/tmp/x3-voice-startup-hook1`) showed frame 1 at the stub's call return and
`[esp]` after a `call 0` in the hook's own code, with the emitted stub and tail
bytes decoded correct. The fix binds the interface as an explicit
C vtable (`vtbl->Init(view,…)`, slot +0x0c as the game uses at `004cfd36`), which
cannot be devirtualised; the rebuilt DLL calls `[edx+0xc]`. After the fix the
same mode completes: three activations, `retries_ok=3`, every site return with
ESI = HRESULT, EDI 0, EBX and ESP intact, `stream_run S_OK`, 5 samples /
882000 bytes, clean teardown (`plugin-v3-game-dmo-hook`, 3.6 s). The host test
compiles the hook and rejects any `__cxa_pure_virtual` symbol or relocation.
Install line additions for the next run: arena base/size/used, stub, tail,
dispatcher, entry slot and `enter` addresses. Fault witness: a vectored
exception handler armed between `initialize()` and `shutdown()` (last device
destroyed, DLL detach; `RemoveVectoredExceptionHandler`) records the first
access-violation-class fault (code, address, arena-relative classification,
EIP/ESP/EAX/EBX/ESI/EDI, thread, hit and activation counts) into a fixed record
published by an atomic sequence, counts later faults, and always continues the
search. Counting rule: only the execute-fault signature (an access violation
with DEP kind 8, or EIP equal to the faulting address) takes the one-shot
record and increments `faults`; every other first-chance exception of the
accepted codes (SEH-handled probes, the game's own `__try`, read/write faults)
only increments `other_first_chance`; stack overflow is not accepted at all
since the handler cannot format on the last guard page. It takes no lock and touches no stdio: one preformatted copy of the line
is written unbuffered with `WriteFile` to the log's OS handle (best effort, may
precede buffered lines), and `report()` formats the same line at the next Present
with `faults=N`. No per-frame cost.

## Timing correction builds (2026-09-14)

The ratified [cue timing correction](voice-cue-timing-correction.md) is applied as
`voice-decoder-subbuffer.patch` on the private gst-libav copy. Build v3 under
`/tmp/x3-wma-plugin-v3/` (that tree is gone; backup `artifacts/wma-plugin-v3`
in the resume directory) caps output buffers at 200 samples and sets a 500 ms decoder
tolerance; the native probe then reports anchor errors within 0.02 ms and
1000 ms spans within 0.02 ms (`verification/results/bottle-X3/voice-native-actual-v3.json`,
v1 error up to 701 ms, v2 47 ms). v3 is the gameplay candidate once the load
hang above is fixed; v1 and v2 stay for rollback.

## Float limit build v4 (2026-09-14)

The voice crackle is decoder-side clipping distortion, not a handover problem.
`avdec_wmav2` negotiates `audio/x-raw, format=F32LE, layout=non-interleaved,
rate=44100, channels=1` and pushes ffmpeg's FLTP samples through unchanged; the
stock audioconvert in winegstreamer's transform converts them to `S16LE,
interleaved` with `unpack F32LE to F64LE` -> `convert F64 to S32`
(`audio_orc_double_to_s32`) -> `quantize to 16 bits, dither 2, ns 0` ->
`pack S32LE to S16LE` (observed under
`GST_DEBUG=libav:5,audioconvert:6,audio-converter:6,GST_CAPS:4`). ffmpeg's
wmav2 output is not limited to [-1, 1] and that S32 step is modular rather than
saturating, so an over-full-scale sample wraps sign: 1.0044 -> 2156986363 ->
-2137980933 -> `-32623` where `+32767` is correct, about 50 times per second of
speech.

CrossOver's GStreamer is not ours to change, so v4 (`/tmp/x3-wma-plugin-v4`,
backup `artifacts/wma-plugin-v4`) limits the decoder's own float output in
`gst_ffmpegauddec_audio_frame`, right after the frame is copied into the output
buffer, to `1 - 2^-14` of full scale (`gst-libav-float-limit.patch`). The
headroom below 1.0 is required, not cosmetic: the quantizer that follows adds
TPDF dither plus the rounding bias to the S32 value with a wrapping add, up to
98302 counts, so a value left at `INT32_MAX` would wrap there instead; 2^-14 of
full scale is 131072 counts and costs 0.0005 dB on samples that were already
clipping. Non-finite samples become silence. Integer output formats return
immediately, so nothing else the plugin can decode is affected.

Measured against a host ffmpeg decode of the same archives (constant
-4096-sample delay): samples differing by more than 30 % of full scale fall
from 290387 / 168094 to **0 / 0**, mean |diff| from 72.64 / 74.61 to 0.32 /
0.33 LSB, max |diff| from 65535 to 3, dump peak 32768 -> 32767, dropouts and
timestamp gaps still 0, and the DMO-fallback hook replica still completes with
3 activations on the v4 plugin path (ledger `docs/verification/voice-decoder.md`).
v4 is the gameplay candidate; v3 stays installed until the user's next run and
is the rollback target.


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
