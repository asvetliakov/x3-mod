# WMA voice decoder plugin, build v4

A patched gst-libav 1.28.4 plugin (`runtime/plugins/libgstlibav.dylib`) with
its private, LGPL-only FFmpeg 7.1.5 closure (`runtime/lib/libx3wma-*.dylib`),
arm64 macOS 14, built on 2026-09-14 against the GStreamer 1.28.4 that
CrossOver Preview ships, following
[voice-decoder-recipe.md](../../../docs/architecture/voice-decoder-recipe.md).
It gives the game's speech path a WMA2 decoder (`avdec_wmav2`) inside
CrossOver's own GStreamer; nothing in CrossOver is copied or modified. The
plugin imports CrossOver's GStreamer/GLib through one absolute `LC_RPATH`,
`/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/aarch64`,
so it loads only with CrossOver Preview installed there. It is a CrossOver-only
component; a native Windows launch does not use it.

v4 = stock gst-libav 1.28.4 plus two patches, both kept here and in
`docs/architecture/` (byte-identical):

- `gst-libav-subbuffer.patch` (`voice-decoder-subbuffer.patch`): sub-buffer
  cap and timestamp-jitter tolerance (cue timing).
- `gst-libav-float-limit.patch` (`voice-decoder-float-limit.patch`): limits the
  decoder's float output to `1 - 2^-14` of full scale (voice crackle fix).

## Provenance

- Recipe, pinned sources and configuration:
  `docs/architecture/voice-decoder-recipe.md`; adapter and version table:
  `docs/architecture/voice-decoder-adapter.md`; ledger:
  `docs/verification/voice-decoder.md`.
- `build-record.md`: the v4 build record (why v4 exists, meson line,
  packaging, audit, replica acceptance).
- `symcheck.txt`: undefined-symbol audit (560 undefined, 455 resolved by
  CrossOver, 87 by the FFmpeg closure, 18 libSystem C-runtime names).
- `artifact-sha256.txt`: SHA-256 of the five runtime binaries and the two
  patches. Its eighth line names the patched build-tree source
  `src/gst-libav/ext/libav/gstavauddec.c`, which is not shipped (that file is
  stock 1.28.4 plus the two patches). Check with
  `shasum -a 256 -c artifact-sha256.txt` in this directory: 7 OK, 1 missing.
- `registry/` is not tracked (the ignore rule covers the whole directory;
  GStreamer also writes temp files there). The
  launcher creates it when missing and GStreamer rebuilds the registry cache
  `registry/x3-arm64.bin` on the first launch.

## How the launcher uses it

`tools/manage.py launch` sets `GST_PLUGIN_PATH_1_0=<dir>/runtime/plugins`,
`GST_REGISTRY_1_0=<dir>/registry/x3-arm64.bin` and
`X3M_VOICE_DMO_FALLBACK=1` in the game process's environment only. Without
`--voice-decoder`, a modded launch uses the first valid of
`<game dir>/x3m/voice-decoder` (copy this directory there for a distribution
without the repository) and this directory; `--voice-decoder none` turns it
off, `--voice-decoder DIR` picks a directory explicitly, and `--vanilla` never
discovers. A missing `registry/` is created in the chosen directory (a dry run only
reports that it will be). `--dry-run` prints the chosen line, `voice decoder: <dir> (<why>)`
or `voice decoder: none (<why>)`.

## Licence position

gst-libav is LGPL-2.1-or-later (`runtime/licenses/gst-libav-COPYING`). The
FFmpeg closure is configured, as the recipe states, as shared LGPL-only
libraries: `--disable-gpl --disable-nonfree --disable-version3`,
`--disable-everything` with only `--enable-decoder=wmav2`, avcodec, avutil,
avformat and avfilter enabled, avdevice, postproc, swscale and swresample
disabled, no autodetected external dependencies
(`runtime/licenses/FFmpeg-COPYING.LGPLv2.1`). The recipe says that FFmpeg's
LGPL-only dynamic-link, source, configuration and notice requirements apply,
and that no codec-pack redistribution, patented-code assurance or blanket
licence claim is implied. The corresponding source is the pinned official
releases named in the recipe (GStreamer monorepo 1.28.4
`b46f881eaa8126eddfd21b5ae5512f8d4ff36255`, FFmpeg n7.1.5
`3a0867c2bfda4a4d4309ca1a8cbdc6175e67f587`) plus the two patches in this
directory; the exact configure and meson lines are in the recipe and
`build-record.md`.

## Rollback

v4 is the only build in the repository and the one in use. The `/tmp` build
trees of v1-v3 no longer exist (checked 2026-09-23); `/tmp/x3-wma-plugin-v5`
is a separate build with extra MPEG decoders (recipe, v5 section), not a v4
rollback. To launch without a decoder use `--voice-decoder none`. A v3 backup
(runtime only, no registry) is kept outside the repository at
`/Users/asvetl/x3-mod-resume-2026-09-14/artifacts/wma-plugin-v3`, plugin SHA-256 `55f4f87b…` as in the adapter note's version table;
`--voice-decoder <that dir>` would launch it and create its `registry/`.
