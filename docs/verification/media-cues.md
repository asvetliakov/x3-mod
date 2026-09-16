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
