# Run 28: failed native voice-stream creation

2026-09-14. Native disassembly study followed by a root-owned standalone
documented-API probe; no game launch, production audio change or cache implementation. The user confirms that target-name speech is
absent. Restoring speech is the acceptance condition; suppressing repeated
attempts is not a fix.

## Finding and remaining unknown

All **34 retained `498140` creation calls return null**: 33 open source ID **144**,
one ID **244**. The ten measured target publishers all use 144. Native code
already retains and reuses successfully created streams. Repeated construction
is therefore explained by creation failure, not a missing successful-stream
cache. The next task should isolate and repair that failure.

The current trace does **not** identify the first failed HRESULT or which COM
method consumes the CPU. The eight slow target publications spend 3,753.767 ms
inside the containing input segment, including 3,710 ms thread CPU; only
11.996 ms wall is outside creation. This establishes CPU-active creation work,
but cannot distinguish activation, graph building, decoding/cueing, or cleanup.
Do not label `OpenFile`, a codec, or CrossOver as the proven cause.

| Retained create source | Calls | Total ms | Range ms | Return |
| --- | ---: | ---: | ---: | --- |
| 144 | 33 | 11,455.5780 | 37.4788–511.8669 | all null |
| 244 | 1 | 461.9914 | 461.9914 | null |

The exact return marker is sound: `498ef8` calls `498140`, `498efd` only adjusts
ESP, and `498f00` subtracts from EDI. EAX at that endpoint is the constructor's
actual return. The 10 ms retention threshold means this is not a claim that
all calls over the complete run failed; it proves every retained create failed.
No seek call appears in the run.

Derived outcome rows are in `/tmp/x3-voice-stream-study/creation-outcomes.json`, reduced by streaming the
24,040,121-byte log `/tmp/x3-bottleX3-run28/session-20260914-013301-216.log`
(SHA-256 `17ed42692aea7f7d438e1209b4972d621250c4ba8152c486279ffeb1009655c4`).
The parent reduction `/tmp/x3-run28-selection-analysis.md` supplies the exact
publisher nesting and CPU bounds. The 24 other creations occur in pending VM;
proximity does not prove they are selection requests.

## Source identity and format

`498140` chooses voice flags `0x110` for these IDs. `4cf460` adds bit `0x40`,
giving `0x150`: audio-only, manual audio/sample path, voice volume class.
Its IDs 100–299 path resolves to `addon\\mov\\%05d.dat`.

Both exact selected loose files exist below the X3 game directory:

| Path | Bytes | Header-derived media |
| --- | ---: | --- |
| `addon/mov/00144.dat` | 269,633,863 | ASF, WMA v2, mono 44,100 Hz, 48,024 bit/s; 44,304.046 s |
| `addon/mov/00244.dat` | 20,311,023 | ASF, WMA v2, mono 44,100 Hz, 48,024 bit/s; 3,335.779 s |

Metadata came from bounded `ffprobe` inspection, not full decoding or a Windows
playback test. Existence/readable metadata does not prove every compressed cue
is intact or that the Windows graph can decode it. Base `mov/00144.dat` also
exists; base `mov/00244.dat` and the corresponding soundtrack MP3/WMA candidates
are absent. The native addon path makes the missing base-244 file irrelevant.

The voice branch tries to pre-add the **Windows Media Audio Voice DMO** through
`CLSID_DMOWrapperFilter`; its decoder CLSID is
`874131cb-4ecc-443b-8948-746b89595d20`. That decoder's documented input is Voice
format tag `0x000A`, distinct from these WMA v2 files. This is a useful suspect,
**not a causal diagnosis**: the native branch tolerates optional decoder failures,
and Intelligent Connect can choose another decoder. Record its actual Init and
connection result rather than replacing it blindly. [Microsoft Voice decoder](https://learn.microsoft.com/en-us/previous-versions/dd756599%28v%3Dvs.85%29),
[Intelligent Connect](https://learn.microsoft.com/en-us/windows/win32/directshow/intelligent-connect).

## Native ownership and why selection retries

`606f44` owns a linked list of 0x40-byte MOV records. Relevant derived fields are
source ID `+0x10`, callback/context `+0x14/+0x18`, cue start/end `+0x1c/+0x20`,
media object `+0x24`, and flags `+0x2c`.

- `498e30` searches this list by source ID. A hit reuses its media object,
  seeks through `4d0430`, starts/updates samples through `4d1870`, and replaces
  the request callback/cue state.
- `498140` allocates a record and calls `4cf460`. Only a successful media object
  is linked at `498248–498268`; voice volume is set at `498273–498280` from
  configuration `606f34+0x77c`, then the record is returned at `4982a6`.
  Failure frees the unlinked record. Playback fails its second lookup, invokes
  its unsuccessful completion callback, and returns. The next request sees
  no record and reconstructs the same source.
- `498370` normally clears the active bit/callback when a nonlooping nonzero
  source finishes, retaining its record for reuse. Seek/sample failure and
  error status can destroy the record through `4984d0`.
- Explicit source stop (`4986b0`), manager reset (`497190`), and manager teardown
  (`4980d0`) provide destruction boundaries. The saved MOVI loader `498ad0`
  deliberately skips voice IDs 101–299; do not infer startup preloading.
- `4d1d40` stops the DirectSound buffer and media graph, releases graph/control,
  audio/video sample/filter objects, then frees the 0xb4-byte media object.
  Its audio helper `4d1a40` also releases PCM/sample/buffer resources.

This is one mutable stream cursor, sample buffer, volume and callback owner per
source. A new asynchronous/shared decoder cache would have to preserve those
semantics, not merely retain a COM pointer. The first remedy should let the
existing cache acquire a valid stream.

## Exact bounded failure/CPU boundaries

The local original disassembly gives these call/return sites. They are proposed
diagnostic boundaries, not installed hooks. Record return HRESULT **before**
cleanup, distinguish optional from fatal calls, and time final cleanup separately.

| Boundary | Native sites | Failure question |
| --- | --- | --- |
| AMMultiMediaStream activation / Initialize | `4cf53a→4cf540`, `4cf554→4cf556` | COM class/apartment, READ/NOGRAPHTHREAD setup |
| Add primary audio / set PCM format | `4cf5e0→4cf5e2` or `4cf629→4cf62b`; `4cfb1a`, `4cfba7` | AudioMediaStream availability and type acceptance |
| Get graph / optional speech wrapper | `4cfbe0`; `4cfce4`, `4cfd23`, `4cfd44`, `4cfe18` | Wrapper activation/QI/Init/AddFilter; optional failure is not final failure |
| Source/filter path | `4d0063`, `4d007f`, `4d0093` | Alternate AddSourceFilter/FindPin/Render path if configuration selects it |
| OpenFile | `4d0143→4d0145`, fallback `4d00f5→4d00f7` | Source recognition, decoder discovery/negotiation and graph construction |
| Required audio backend / stream format | `4d01ea`; `4d0209`, `4d0223`, `4d023a` | Null global DirectSound object, GetMediaStream/QI/GetFormat failure |
| AudioData/sample/buffer creation | `4d028f`, `4d02af`, `4d02c6`, `4d02e8`, `4d034c→4d034e` | PCM buffer, sample or DirectSound creation failure |
| Control and cue | `4d038a`, `4d03a5`, `4d03c4`; `4d03f5→4d03f7`, `4d0407→4d0409` | Interface/duration, RUN or Pause failure and cue cost |
| Failure cleanup | `4d0162` | Stop/release CPU cost after the original failure |

Many negative HRESULT paths retry twice; only `E_OUTOFMEMORY (0x8007000e)` invokes
native memory recovery. Thus two attempts are not proof of an allocation fault.
Optional filter insertion results are sometimes ignored. Capture the first
**fatal** result plus preceding optional results rather than only last EAX.
Positive success codes such as Pause's `S_FALSE` must remain successful.
[DirectShow result codes](https://learn.microsoft.com/en-us/windows/win32/directshow/error-and-success-codes).

Startup is a second concrete branch to discriminate. `4b8740` and audio init
`4de060` call `CoInitialize(NULL)` without checking its HRESULT. `4de060` can
leave global `608aec` null after DirectSound creation fails; `4de3b0` clears it
on teardown. The voice constructor can do substantial graph work before its
`4d01ea` null check. Run 28 does not record this pointer/state or the startup
HRESULTs. No DirectSound failure is established yet.

## Proposed next diagnostic and fix decision

Build one small **documented-API native-equivalent audio probe**, not another
broad gameplay tracer. Root owns the serialized X3 fixture execution. It should use the two
actual files, READ/NOGRAPHTHREAD, primary audio, the native PCM request,
optional wrapper sequence, manual AudioData/sample and DirectSound buffer,
RUN/Pause, then seek/read a short cue. For each stage retain QPC/thread CPU,
exact HRESULT, attempt and thread ID, connected filter CLSIDs/media types,
actual format, and cleanup cost. Verify nonzero decoded PCM and advancing
samples; successful COM creation alone does not restore speech.

Batch the unmodified construction first, then a fresh-graph control without the
optional speech wrapper, with repeat construction and same-graph seeking. A
successful no-wrapper control is evidence for a narrow graph-selection remedy,
not permission to bypass later sample/volume/callback tests. If both fail,
select the fix from the **first fatal stage**: source/codec connection versus
manual stream/sample/DirectSound/cue setup. Avoid installing codec packs or
changing global filter merits as a speculative remedy. A portable explicit
filter connection is an option only after actual media types and decoder
capabilities are known; it must work through documented COM APIs.

If the standalone path succeeds and cannot reproduce the game's state, add only
the bounded native stage timings above and audio-init outcome/presence to the
next consolidated diagnostic. Preserve calling ABI, flags, CPU registers and
LastError. No new target identity capture is needed. `IGraphBuilder::SetLogFile`
can optionally explain Intelligent Connect's search; disable it before closing
its handle. [SetLogFile](https://learn.microsoft.com/en-us/windows/win32/api/strmif/nf-strmif-igraphbuilder-setlogfile).

After speech succeeds, test two selections against one retained record, cue
changes, interruption/callback ownership, ordinary completion, stop/load/shutdown,
volume and audio-failure recovery. Only then consider owner-thread prewarming
after COM/audio/movie-manager readiness to move remaining cold-open cost out
of input processing. There is no basis for indefinite negative caching.

`AMMSF_NOGRAPHTHREAD` requires message pumping on the creating thread and releasing
all multimedia objects before it exits. Retain existing owner/apartment and
teardown order; do not move raw graph pointers to a worker or reuse a partially
failed graph without complete cleanup. [Initialize contract](https://learn.microsoft.com/en-us/previous-versions/ms784027%28v%3Dvs.85%29).
The accompanying `/tmp/x3-voice-stream-study/com-contracts.md` contains independently checked interface,
GUID, lifetime and HRESULT references. Native-Windows source compatibility is
required; the preceding disassembly/research phase did not execute the game or a Wine probe.
The later standalone X3 execution is recorded below; native Windows remains unverified.

Local Ghidra exports `lifetime.txt`, `reuse-startup.txt`, and `audio-init.txt`
contain the bounded raw inspection and must remain untracked. Only this derived
note, the small outcome summary and COM contract note are intended for review.

## Documented-API voice probe: graph connection failure

The root-owned X3 probe reproduces failed construction for both actual voice
archives 144 and 244. The retained R1 partial plus its disjoint two-configuration
tail cover **12 configurations / 24 fresh constructions**: three native graph
routes, with/without the optional speech wrapper, two repeats each. All 24 fail
before sample/buffer/RUN or PCM decoding. Source files and codec registrations
were not modified, and no sound output was started.

| Measured boundary | Calls | Result | Wall / owner-thread CPU |
| --- | ---: | --- | ---: |
| Primary audio + requested PCM format | 24 each | all succeed | — |
| Optional speech DMO Init | 24 | `80040154` REGDB_E_CLASSNOTREG | 1.060 / 0 ms |
| Alternate AddSourceFilter / FindPin | 8 each | all succeed | — |
| Alternate Render | 16 | `80040218` VFW_E_CANNOT_RENDER | 5292.910 / 2770 ms |
| OpenFile, including native retries/fallback | 48 | `80040217` VFW_E_CANNOT_CONNECT | 15721.359 / 10260 ms |

Removing the unavailable optional Voice DMO does **not** repair connection.
DirectSound startup succeeds. Retained source-filter CLSIDs identify AsyncReader,
not WM ASF Reader; no connected-pin rows are emitted, although R1 does not record
EnumPins failures and therefore cannot prove an exhaustive disconnected-pin
inventory. This identifies source/decoder **connection building** as the failed
stage in the standalone native-equivalent path, not a uniquely missing codec.
It is not yet an in-game HRESULT trace or restored audible speech.

The game already retains successfully created streams, so a permanent negative
cache would hide missing speech rather than fix it. The next bounded comparison
will explicitly activate WM ASF Reader, Load the unchanged `.dat` file, inspect
offered audio types, and connect to the existing manual PCM sink—with an explicit
Standard WMA decoder control. A `.dat` source-selection repair remains a candidate,
not a proven fix. Microsoft documents the [cannot-connect code](https://learn.microsoft.com/en-us/windows/win32/directshow/error-and-success-codes)
and [WM ASF Reader source/decoder boundary](https://learn.microsoft.com/en-us/windows/win32/directshow/wm-asf-reader-filter).

The two commands took 294.536 s overall, versus 43.176 s in measured method brackets.
The 251.360 s residual includes process/orchestration and other unmeasured work;
it is **not** COM-method CPU or game frame cost. R1 stopped at its budget after
20 cases; only the missing four were run afterwards. Future diagnostics batch
fresh graphs in one owner process to avoid this measured process-lifetime cost.

Compact combined evidence: [voice-stream-create.json](../../verification/results/bottle-X3/voice-stream-create.json).
The record retains both immutable local source-record identities and the exact
EXE identity. The source/runner received independent Sol/high review; the final
runner's eight focused host tests pass. No production audio hook or cache change
is included in this diagnosis checkpoint.

Reproducible sources: [probe](../../verification/probe/voice_stream_probe.cpp),
[build-only script](../../verification/probe/build_voice_stream.sh),
[consume-only runner](../../verification/probe/run_voice_stream_probe.py), and
[focused parser tests](../../verification/analysis/test_voice_stream_probe.py).
The retained EXE was built before source relocation; its native C++ contents
are unchanged. Build with `sh verification/probe/build_voice_stream.sh` only
when new source requires it; existing evidence consumes the frozen EXE.
Every run requires `X3M_FIXTURE_BOTTLE=X3` and the shared `wine_lock.py` wrapper.
The complete default batch has 12 configurations; repeated `--variant 244:2:1
--variant 244:2:0` selects exactly the disjoint tail used here.
