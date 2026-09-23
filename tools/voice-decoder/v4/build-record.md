# Patched gst-libav WMA decoder (v4, sub-buffer cap + jitter tolerance + float limit)

2026-09-14. Fourth plugin build, made by copying `/tmp/x3-wma-plugin-v3`
(which stays untouched, is the plugin the user currently launches with, and
remains the rollback target) and adding one patch. v1/v2/v3 build records
document the shared provenance, source reuse, packaging and audit procedure;
only the differences are recorded here.

## Why v4 exists: the voice crackle

Replica PCM dumps of `00144.dat`/`00244.dat` through the game's own path
(`voice_pcm_scan.py` plus a sample-wise comparison with a host ffmpeg decode of
the same archives, constant -4096-sample decoder delay) showed 290387 / 168094
samples differing from the reference by more than 30 % of full scale, every one
of them a near-full-scale sign inversion (`32767` -> `-32622`), about 50 per
second of speech. Mean |diff| 72.64 / 74.61 LSB, max 65535, dump peak 32768.

Stage, pinned from `GST_DEBUG=libav:5,audioconvert:6,audio-converter:6,GST_CAPS:4`
on a replica run (raw `/tmp/x3-voice-caps.log`, run `/tmp/x3-voice-caps-probe`):

* `avdec_wmav2` negotiates `audio/x-raw, format=F32LE, layout=non-interleaved,
  rate=44100, channels=1` and pushes ffmpeg's FLTP samples unchanged;
* the stock audioconvert inside winegstreamer's transform converts to
  `audio/x-raw, format=S16LE, layout=interleaved, rate=44100, channels=1`
  with the chain `chain_unpack: unpack format F32LE to F64LE` ->
  `chain_convert_out: convert F64 to S32` (`audio_orc_double_to_s32`, multiply
  by 2^31 and convert) -> `chain_quantize: quantize to 16 bits, dither 2, ns 0`
  -> `chain_pack: pack format S32LE to S16LE`.

ffmpeg's wmav2 output is not limited to [-1, 1]; the S32 step here is modular,
not saturating (1.0044 -> 2156986363 -> -2137980933 -> packed -32623), which is
exactly the observed artefact. CrossOver's GStreamer cannot be modified, so the
decoder limits its own float output instead.

## The added patch

`/tmp/x3-wma-plugin-v4/gst-libav-float-limit.patch` (also in the repository as
`docs/architecture/voice-decoder-float-limit.patch`), against the v3 state of
`ext/libav/gstavauddec.c`:

* `GST_FFMPEGAUDDEC_FLOAT_LIMIT` = `1 - 2^-14` of full scale, with the reason
  for the headroom: the quantizer that follows adds TPDF dither plus the
  rounding bias to the S32 value with a wrapping add, up to
  `(1 << 15) + 2 * ((1 << 15) - 1)` = 98302 counts, so anything left within
  that distance of `INT32_MAX` would wrap there instead. 2^-14 of full scale is
  131072 counts. Cost: peak 32766 instead of 32767 on samples that were
  clipping anyway (0.0005 dB).
* `gst_ffmpegauddec_limit_float()` clamps the freshly filled output buffer in
  place for `F32`/`F64` output (both layouts; the buffer is exactly
  `nsamples * bpf` bytes, so planar offsets are covered), maps non-finite
  samples to silence, and returns immediately for integer formats. It is called
  from `gst_ffmpegauddec_audio_frame` right after the `gst_buffer_fill`, before
  the channel reorder and before the v2 sub-buffer split. One pass over the
  samples of the frame just copied, 44100 floats/s on this path.

## Build (same recipe, same flags, only the tree root differs)

```sh
PATH=/tmp/x3-wma-plugin/tools/venv/bin:$PATH MACOSX_DEPLOYMENT_TARGET=14.0 \
PKG_CONFIG_PATH=/tmp/x3-wma-plugin/pcshim:/tmp/x3-wma-plugin/ffmpeg/lib/pkgconfig \
meson setup build/gstlibav src/gst-libav --buildtype=release \
  --default-library=shared --wrap-mode=nofallback -Dauto_features=disabled \
  -Dtests=disabled -Ddoc=disabled \
  -Dc_args="-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_78 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_78"
ninja -C build/gstlibav      # 14/14 targets, 0 warnings, exit 0
```

Run from `/tmp/x3-wma-plugin-v4`; the FFmpeg closure and the build-only SDK are
still the v1 ones under `/tmp/x3-wma-plugin`, so the four `libx3wma-*.dylib`
are the bit-identical v1 files (hashes below equal the v1/v2/v3 record).
Packaging identical to v1-v3: `-id @loader_path/libgstlibav.dylib`, FFmpeg
imports to `@loader_path/../lib/libx3wma-*.dylib`, the seven CrossOver imports
to `@rpath/...`, both build rpaths deleted and one
`LC_RPATH = /Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/lib/aarch64`
added, then `codesign -f -s -`. Logs: `logs/gstlibav-{setup,build}.log`.

## Audit

* `otool -L`: the same 13 load commands as v3; `otool -l | grep -c
  'x3-wma-plugin|cxoffice'` = 0 build-tree paths; single CrossOver `LC_RPATH`.
* `results/symcheck-v4.sh` (v3 script, root switched): `undefined_total=560`,
  `resolved_by_crossover=455`, `resolved_by_ffmpeg_closure=87`, `unresolved=18`
  (the known libSystem C-runtime names), `gst_glib_imports=438`,
  `gst_glib_unresolved_by_cx=0`. The undefined-symbol set is **identical** to
  v3's (`comm` both directions empty), so the patch adds no new import.

## SHA256 (`results/artifact-sha256.txt`)

```
98334d763a296ff10d57a88753e23c1432a4d61766654f0972459d5c3b879a77  runtime/plugins/libgstlibav.dylib
4d708049adb8714ae26794ada06d23b1d36d2560d38a2ed4274992e105840130  runtime/lib/libx3wma-avcodec.61.dylib
c766bf7477f2d9981aa7600934b79838d8026fcb6d97bc9e9eb6f79582fb313a  runtime/lib/libx3wma-avfilter.10.dylib
d49e36bc461446f22fc18e506a15d5766be59ae5b2840d64f0dce2469f3ca31a  runtime/lib/libx3wma-avformat.61.dylib
be42b14f70286374654e7d1e618b6135447c40580aa727bf740993a0e546ffc0  runtime/lib/libx3wma-avutil.59.dylib
1ee93bc47efbc5e27e303525b0d8c90a0beeb867b74dc58d583f3e6d0a0172c8  gst-libav-float-limit.patch
6620755c7d2a48a747b785c8384edee02d281fc9f046a3b4789f14417c9e6430  gst-libav-subbuffer.patch
f5ade4b3f324d74f2cc84630124361398ed1e4f6e7133b12ebcacd4d260908db  src/gst-libav/ext/libav/gstavauddec.c
```

v3 plugin hash for contrast:
`55f4f87b45b4a2554998b71ba5b6393046cb9f04c0f0cb5c7e71492ea618299f`.

## Runtime environment (unchanged contract, v4 paths)

```
GST_PLUGIN_PATH_1_0=/tmp/x3-wma-plugin-v4/runtime/plugins
GST_REGISTRY_1_0=/tmp/x3-wma-plugin-v4/registry/x3-arm64.bin
```

Pointing the two variables back at `/tmp/x3-wma-plugin-v3/...` rolls back to
the plugin currently installed for the user.

## Acceptance (replica, bottle X3, 2026-09-14)

`run_voice_startup_replica.py --mode game-dmo-hook --dump-pcm --dump-seconds 60`
with the v4 paths, replica EXE `5163978a…`, on both archives, then the same
scan and ffmpeg comparison as the v3 measurement:

| Dump | samples>30 % FS | mean abs diff | max abs diff | peak | dropouts | ts gaps |
| --- | --- | --- | --- | --- | --- | --- |
| 00144 v3 | 290387 | 72.64 | 65535 | 32768 | 0 | 0 |
| 00144 v4 | **0** | **0.32** | 3 | 32767 | 0 | 0 |
| 00244 v3 | 168094 | 74.61 | 65535 | - | 0 | 0 |
| 00244 v4 | **0** | **0.33** | 3 | 32767 | 0 | 0 |

Interior jump counts fall from 410611 to 220825 (00144) and 200765 to 96292
(00244); the rest are genuine speech transients. The hook replica still
completes with 3 activations, `retries_ok=3`, on the v4 plugin path.
