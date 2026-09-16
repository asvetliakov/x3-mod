# Optional process-local WMA decoder: isolated build recipe

Reviewed recipe, copied verbatim into the repository. The build described here
was actually carried out on **2026-09-14**; its record, audit numbers and raw
output are in `/tmp/x3-wma-plugin/build-record.md` (untracked), and the
architecture note that owns this work is
[voice-decoder-adapter.md](voice-decoder-adapter.md).

This is the preferred small feasibility checkpoint, not a selected production repair.
No application/bottle/system plugin, registry, game-file or launcher writes.
Native Windows keeps the game's documented COM/media path; this optional macOS
backend adapter is not a Windows prerequisite. The earlier full-source-filter
preference in voice-repair-options.md is superseded by this assessment.

## Established compatibility boundary

R3 actually runs GStreamer1.28.4 built against1.28.4. Its ASF demuxer succeeds;
the missing component is a suitable WMA8/audio-x-wma-v2 decoder. Unix-side code
is **arm64**, even though X3/our COM probe is x86. Static inventory establishes
CrossOver GLib2.78.0, ordinary GStreamer audio decoder APIs and19 shipped plugins.
No gst-libav or usable development SDK is shipped. Homebrew has GLib2.88.3 and
GPL-enabled FFmpeg9.0.1 with many external dependencies, but no gst-libav plugin.
Do not use those Homebrew runtime libraries for this package. See the compact
local dependency inventory, `gstreamer-local-inventory.md`, kept beside the
original recipe outside the repository.

The stock GStreamer1.28.4 gst-libav plugin uses public core/base/audio/video/
pbutils and GLib APIs. It requires libavcodec, libavutil, libavformat and
libavfilter even for an audio-only decoder build. That is a bounded existing
upstream adapter, substantially less new lifecycle code than a DirectShow source.
Its own pinned FFmpeg subproject uses7.1.1; select the maintained7.1.5 release
from the same ABI branch, not the broader Homebrew binary or a development HEAD.
[Plugin build](https://github.com/GStreamer/gstreamer/blob/1.28.4/subprojects/gst-libav/meson.build),
[pinned upstream branch](https://github.com/GStreamer/gstreamer/blob/1.28.4/subprojects/FFmpeg.wrap),
[FFmpeg7.1.5 release](https://ffmpeg.org/download.html#releases).

## Reproducible sources and isolated layout

Use official release sources only. GitHub's official project mirrors report
these signed tags verified; retain the tags/signatures and source identities,
then verify checkout identities locally before any compilation:

| Source | Release | Peeled commit |
|---|---|---|
| GStreamer monorepo (core/base/libav public SDK and plugin) |1.28.4|`b46f881eaa8126eddfd21b5ae5512f8d4ff36255`|
| FFmpeg |n7.1.5|`3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587`|
| GLib, build-only matching public SDK |2.78.0|`3c543ef69ffab7c78e29eaf383e7fe2c7df6cd49`|

Release archives/signatures from gstreamer.freedesktop.org, ffmpeg.org and
GNOME are an alternative. The GNOME2.78.0 tar.xz SHA256 is
`44eaab8b720877ce303c5540b657b126f12dc94972d9880b52959f43fb537b30`.
The GStreamer checksum endpoint was temporarily unavailable during research;
do not silently substitute an unverified binary download. Source signatures
are provenance, not normal runtime feature gates.

All output stays under `/tmp/x3-wma-plugin/`: `src/`, `build/`, `sdk/`,
`runtime/plugins/`, `runtime/lib/`, `registry/`, `results/`. A local Python venv
may hold pinned Meson build tooling; existing Ninja/Apple Clang are available.
No global brew/pip install. The SDK is build-only and must never enter a runtime
plugin/library path. No CrossOver library is copied or modified.

## Build stages and fail-closed checks

1. Generate/install a **build-only public SDK** from matching GLib2.78.0 and
   GStreamer1.28.4 core/base sources. This supplies generated glibconfig,
   gstconfig and audio/video/pbutils enum headers missing from the app. Disable
   SDK tests/examples/tools/introspection/NLS/plugins and optional dependencies;
   build only the public libraries/headers needed to produce a consistent SDK.
   If header generation needs supporting libraries, keep them only in `sdk/`.
   Do not invent private layouts or substitute newer Homebrew GLib headers.
   No generated SDK runtime library is packaged or searched by the probe.
2. Build **arm64 macOS14** FFmpeg7.1.5 with Apple Clang, shared LGPL-only libs.
   Proposed configure core (final generated configuration must be audited):

   ```sh
   ./configure --prefix="$TASK_ROOT/ffmpeg" --cc=clang --arch=aarch64 --target-os=darwin \
     --disable-autodetect --disable-everything --disable-programs --disable-doc \
     --disable-network --disable-gpl --disable-nonfree --disable-version3 \
     --disable-static --enable-shared --enable-pic --enable-decoder=wmav2 \
     --enable-avcodec --enable-avutil --enable-avformat --enable-avfilter \
     --disable-avdevice --disable-postproc --disable-swscale --disable-swresample \
     --extra-cflags="-arch arm64 -mmacosx-version-min=14.0" \
     --extra-ldflags="-arch arm64 -mmacosx-version-min=14.0"
   ```

   Disable swresample explicitly: `--disable-everything` does not disable
   libraries, and the inspected stock plugin/wmav2 closure does not require it.
   Re-enable only if unexpected configure/link evidence proves a requirement
   and root reviews that bounded dependency change. Enable no demuxer, protocol, encoder, device,
   filter or unrelated decoder. The ASF demuxer and PCM conversion already live
   in the retained CX pipeline. Audit generated configuration: wmav2 is the only
   enabled decoder, GPL/nonfree/version3 disabled, no Homebrew/system codec deps.
3. Build stock gst-libav1.28.4 against that restricted FFmpeg and the matching
   public SDK headers. Meson must use `--wrap-mode=nofallback`, shared library,
   `-Dauto_features=disabled -Dtests=disabled`, `GLIB_VERSION_MIN_REQUIRED` and
   `GLIB_VERSION_MAX_ALLOWED` set to `GLIB_VERSION_2_78`. Use explicit pkg-config
   shim files that point **headers to sdk/** but every GStreamer/GLib link input
   to the already-installed CX arm64 public dylib. No Homebrew or SDK runtime
   link path. Restrict includes/link flags to this dependency set; SDK generation
   is not permission to bundle a second core. No plugin-source rewrite planned.
4. Package only `libgstlibav.dylib` plus the restricted FFmpeg dependency closure.
   Private FFmpeg dylib IDs/load commands must resolve inside runtime/lib using
   explicit loader-relative paths/unique recognizable names. Change only our
   artifacts. Set the plugin's CX imports to `@rpath` with an explicit rpath to
   the selected CX arm64 directory; preserve that one existing public core/type
   system. Do not add DYLD_LIBRARY_PATH or a second GLib/GStreamer to force load.
   Re-sign our final arm64 artifacts ad hoc if needed after load-command edits.
5. Before any load: `file`/`otool -L/-l` and imported/exported-symbol checks must
   establish arm64/macOS14, all required public symbols present, exact allowed
   dependency resolution, no `/opt/homebrew`, build/sdk runtime, `/opt/cxoffice`
   dangling imports or second core. Record versions/configuration/licenses,
   final dependency closure and total bytes. Do not promise compatibility from
   GStreamer minor-version admission alone. Fail and report any needed ABI/API
   change rather than adding private offsets or globally replacing libraries.

GStreamer accepts plugins with the same major and a no-newer minor, but that
is only its first compatibility check; actual imports and loading still need
qualification. FFmpeg's LGPL-only dynamic-link/source/configuration/notice
requirements apply; package the corresponding source and exact configuration.
No codec-pack redistribution, patented-code assurance or blanket license claim
is implied. [Public plugin loader](https://github.com/GStreamer/gstreamer/blob/1.28.4/subprojects/gstreamer/gst/gstplugin.c),
[FFmpeg license](https://ffmpeg.org/legal.html),
[Apple loader-relative/rpath contract](https://developer.apple.com/library/archive/documentation/DeveloperTools/Conceptual/DynamicLibraries/100-Articles/RunpathDependentLibraries.html).

## Precise reversible probe after root accepts the retained build

Use the retained R3 EXE and runner first, with **only process-local** additions:

```text
GST_PLUGIN_PATH_1_0=/tmp/x3-wma-plugin/runtime/plugins
GST_REGISTRY_1_0=/tmp/x3-wma-plugin/registry/x3-arm64.bin
```

Leave bundled system plugins and feature ranks unchanged. CrossOver's launch
script sets unversioned GST_PLUGIN_SYSTEM_PATH and GST_REGISTRY, but the public
GStreamer1.28.4 implementation checks the `_1_0` variants first. Never use an
empty system path: ASF/base conversion and existing graphs still need it.
No external plugin scanner is shipped here; allow the existing scanner/fallback
behavior initially. A scanner failure is a diagnostic, not permission to disable
isolation or import another GStreamer core. Private-registry creation must be
observed, and default registry/app/bottle changes must be absent.
[Environment contract](https://gstreamer.freedesktop.org/documentation/gstreamer/running.html).

The retained sync runner already preserves unrelated environment fields and
sets only its scoped Wine/GStreamer debugging. Root owns the shared Wine lock.
A successful next result requires plugin discovery from the private path,
`avdec_wmav2` selected, one actual core/GLib dependency set, both source Opens
succeeding and valid output metadata. R3's no-plugin failure is the retained
negative control; no need to rerun the original8/24 graph matrix.

Then, in the same bounded qualification checkpoint, use the corrected native
null-event sample contract on both actual voice DATs through the native graph.
Retain nonzero PCM/10-and60-second cues, repeated/backward seeks, cold/warm open,
cleanup and no-audible-output checks. Do not call Open success restored speech.
The synthetic control's sample timestamp interval is95.1304ms for100ms of PCM;
the native game uses start/end for cue trimming, so exact cue timing remains a
separate acceptance concern even if compressed decoding is restored.

Removing the two environment variables reverts to native behavior; discard only
our runtime/private registry files. No module remains after process teardown.
Gameplay integration/opt-in UX, real speech and selection-pause acceptance follow
only after the retained diagnostic proves decoder/seek/lifetime behavior.

## Alternatives if isolated plugin loading cannot be established

A custom virtual PCM WAV **IAsyncReader** source is smaller than owning a full
push filter: reuse the native WAV parser/manual sink, map absolute PCM byte
ranges to bounded ASF decode windows, and preserve a virtual full-file duration.
It still needs aligned async reads/cancellation/seek-cache/lifetime correctness;
a short-WAV path replacement cannot satisfy the native cached stream's later
absolute seeks. Full PCM expansion costs≈4.202GB for both archives and is not the
default. A full app-owned push source remains the larger last option. None is
implemented while the smaller process-local adapter is feasible.

## Independent recipe review

Sol/high review completed 2026-09-14. Its one finding was that FFmpeg keeps
swresample enabled unless explicitly disabled. The configure line now includes
`--disable-swresample`; the rest of the recipe was approved. Source
tag identities were checked, but local cryptographic signature verification
has not been performed.

## v5: MP3 and MPEG-1 (2026-09-16)

`/tmp/x3-wma-plugin-v5` is a superset of v4 for the media cues the soundtrack
and movie files need (`docs/verification/media-cues.md`): the same patched
gst-libav decoder, an FFmpeg closure with six more decoders, and the one
demuxer CrossOver's plugin set lacks. v4 is untouched and stays the rollback
target. Untracked build record with the full audit: `/tmp/x3-wma-plugin-v5/build-record.md`.
Sources are the pinned ones already on disk (FFmpeg n7.1.5 `3a0867c2…`,
GStreamer monorepo 1.28.4 `b46f881e…`); the toolchain is the same venv
meson 1.11.2 / ninja 1.13.2 / pkgconf 2.4.3 and Apple clang with
`MACOSX_DEPLOYMENT_TARGET=14.0`.

FFmpeg configure — the v1 line plus six `--enable-decoder`, nothing else changed:

```sh
MACOSX_DEPLOYMENT_TARGET=14.0 /tmp/x3-wma-plugin/src/ffmpeg/configure \
  --prefix=/tmp/x3-wma-plugin-v5/ffmpeg --cc=clang --arch=aarch64 --target-os=darwin \
  --disable-autodetect --disable-everything --disable-programs --disable-doc \
  --disable-network --disable-gpl --disable-nonfree --disable-version3 \
  --disable-static --enable-shared --enable-pic \
  --enable-decoder=wmav2 --enable-decoder=mp3 --enable-decoder=mp3float \
  --enable-decoder=mp2 --enable-decoder=mp2float \
  --enable-decoder=mpeg1video --enable-decoder=mpeg2video \
  --enable-avcodec --enable-avutil --enable-avformat --enable-avfilter \
  --disable-avdevice --disable-postproc --disable-swscale --disable-swresample \
  --extra-cflags="-arch arm64 -mmacosx-version-min=14.0" \
  --extra-ldflags="-arch arm64 -mmacosx-version-min=14.0"
```

Generated-configuration audit: 7 `*_DECODER 1` (`WMAV2, MP3, MP3FLOAT, MP2,
MP2FLOAT, MPEG1VIDEO, MPEG2VIDEO`), 0 demuxers/muxers/parsers/protocols/
encoders/filters/bsfs/hwaccels, no `CONFIG_LIB*`, `GPL/NONFREE/VERSION3/
SWRESAMPLE/SWSCALE/POSTPROC/AVDEVICE` all 0, LGPL-2.1-or-later.

gst-libav is rebuilt with the **identical** flags, patches and source as v4
(`gstavauddec.c` = `f5ade4b3…`, i.e. sub-buffer cap + 500 ms tolerance + float
limit); only `PKG_CONFIG_PATH` points at the v5 FFmpeg. So the float-limit and
cue-timing behavior of the speech path is unchanged, and the same clamp now
also applies to the float MP3/MP2 decoders.

New second plugin, `libgstmpegpsdemux.dylib`, from the pinned monorepo's
`gst-plugins-bad/gst/mpegdemux` (deps `gstbase`/`gsttag`/`gstpbutils`, all
CrossOver dylibs):

```sh
PKG_CONFIG_PATH=/tmp/x3-wma-plugin-v5/pcshim \
meson setup build/gstbad src/gst-plugins-bad --buildtype=release --default-library=shared \
  --wrap-mode=nofallback -Dauto_features=disabled -Dtests=disabled -Dexamples=disabled \
  -Ddoc=disabled -Dmpegdemux=enabled -Dintrospection=disabled -Dorc=disabled -Dnls=disabled \
  -Dc_args="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_78 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_78"
ninja -C build/gstbad gst/mpegdemux/libgstmpegpsdemux.dylib
```

v5 has its own `pcshim/` (v1's shims with the CrossOver `libdir`, plus
`gstreamer-{tag,controller,net,allocators,app,riff,rtp,rtsp,fft}-1.0.pc`,
`gio-2.0.pc` and glib's tool variables pointing at `sdk/bin`), because
`gst-plugins-bad`'s top-level `meson.build` resolves those before reaching the
one subdirectory we build. No SDK runtime library is packaged and no Homebrew
runtime library is linked. `mpegaudioparse` (audioparsers) and `mpegvideoparse`
(videoparsersbad) already ship with CrossOver and were **not** rebuilt.

Packaging and audit are the v1-v4 procedure, now over six artifacts
(2272 KB total): `@loader_path` IDs, FFmpeg imports as
`@loader_path/../lib/libx3wma-*.dylib`, CrossOver imports as `@rpath/...` with
the single absolute CrossOver `LC_RPATH`, `codesign -f -s -`. All six are arm64
`minos 14.0` with zero homebrew/cxoffice/SDK/build-tree load commands.

| Artifact | Bytes | SHA256 (prefix) |
| --- | ---: | --- |
| `runtime/plugins/libgstlibav.dylib` | 343232 | `ba95962b…` |
| `runtime/plugins/libgstmpegpsdemux.dylib` | 137824 | `5e93887f…` |
| `runtime/lib/libx3wma-avcodec.61.dylib` | 654640 | `19643621…` |
| `runtime/lib/libx3wma-avfilter.10.dylib` | 166208 | `f06fdff0…` |
| `runtime/lib/libx3wma-avformat.61.dylib` | 244304 | `56a3168e…` |
| `runtime/lib/libx3wma-avutil.59.dylib` | 715008 | `6aa4f9aa…` |

Delivery is unchanged: `tools/manage.py launch --voice-decoder /tmp/x3-wma-plugin-v5`
sets only `GST_PLUGIN_PATH_1_0`/`GST_REGISTRY_1_0` (and the DMO gate) for the
launched process. Native Windows is unaffected: it keeps its own MP3/MPEG
codecs and never loads this runtime. Host verification results are in
`docs/verification/media-cues.md` §5.
