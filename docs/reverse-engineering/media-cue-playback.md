# `0x00498140`: the media-record allocator, its ABI, and a negative-cache hook

2026-09-16. Static study of X3AP.exe, SHA-256 `fdbf3418d8f0a897…` (the bottle
copy); nothing was launched. Every byte, span, edge, caller and flag claim comes
from `i686-w64-mingw32-objdump` on the file bytes and is re-checked by
`verification/probe/verify_media_cue_site.py` (`PASS`, `source_present: false`).
Raw decompiler output stayed local and untracked. Inferences are marked.

**Status (2026-09-16).** Implemented as the default-off gate `src/proxy/media_cue.cpp`
(`--media-cue-trace`, `--media-cue-cache on|off`, `--media-cue-retry-s`):
two-arm stub on the span of §5, outcome captured by return-address
substitution rather than a second site, cache scoped as §2 describes, sector
change by interval only. Qualification, telemetry names and fixture evidence:
[media-cues.md](../verification/media-cues.md) §6. The verifier now also pins
that the routine never reads `[esp]` (`no_return_slot_read`).

**Question.** [sector-post-pass.md](sector-post-pass.md) §2 names `0x00498140`
as the retry engine behind the 380 ms sector stall. Can a trampoline trace it
and negative-cache the failed graph build without touching speech, and is an
early `return 0` behaviour-neutral?

**Answer.** Yes on both. `0x00498140` is cdecl with one stack argument (the
media id) plus `EAX` (constructor flags); the *return address* at `[ESP]` on
entry separates its five call sites exactly, and a second-level read at
`[ESP+0xc]`/`[ESP+0x10]` narrows to the sector selector and its cue kind `0x5a`.
The 5-byte entry `53 8b 5c 24 08` qualifies as a hook span, and an early
`return 0` reproduces the real failure's state exactly except for two cumulative
allocation counters that only feed the on-screen `%d Allocs` line (§6).

## 1. Signature

`0x00498140`–`0x004982ac`, 0x16c bytes, gap-free decode, two `ret` (`0xc3`):
`0x004981fd` failure, `0x004982ab` success; `int3` padding to `0x004982b0`.
Signature `media_record* __cdecl create_media_record(int media_id /*[esp+4]*/)`,
`EAX` in = the constructor flag word.

`0x00498141 mov ebx,[esp+8]` reads `+8` *after* the `push ebx`; the argument
sits at `[esp+4]` at the instant of entry. Non-zero `EAX`
(`0x0049814e mov edi,eax`) bypasses the id→flags map
(`0x0049819f test edi,edi`); four of the five callers pass 0. `EBX, EBP, ESI,
EDI` are pushed at `0x00498140`–`0x00498147` and popped on *both* returns
(`5f 5e 5d 5b`); `ECX`/`EDX` are scratch; the only ESP arithmetic in the routine
is `add esp,N` cleanup, no `enter`/`leave`.

Body: `malloc(0x40)` (`0x005112c4`) with one retry via the memory-recovery
helper `0x004b8b60`; counters `0x006085f4`/`0x006085f8` (live bytes/blocks) and
`0x006089f8`/`0x006089fc` (lifetime bytes/allocs); `memset(rec,0,0x40)`
(`0x00518de0`); the id→flags map; `0x004cf460(id, flags)` (cdecl, `add esp,8`);
`mov [rec+0x24],eax`. The map (only when `EAX == 0`) gives ids `1`,`3` → flags
`8`/`[rec+0x30]=0x42`, `2` → `8`/`0x3d`, `800` and `10001` → `8`, `810`/`811` →
`0x100`, `101`–`999` → `0x110`, else 0 — all on the still-unlinked record.

**Failure** (`0x004981e2`): `free(rec)` (`0x0050e1b0`), `sub` both live counters
by 0x40, `xor eax,eax`, four pops, `ret`; nothing linked, no other global
touched, the caller pops the argument. **Success** (`0x00498248`):
`[rec+0x2c] = [obj+0x8c]`, link at the list head `*0x00606f44`, two optional
`0x004d0570` volume calls under flags `0x100` / `0x80`, `and [rec+0x2c],~2`
("not playing"), `mov [rec+0x10],ebx` (**the id is the record key**),
`eax = rec`.

## 2. The five callers and how to scope a hook

No data reference to `0x00498140` exists anywhere in the image, so these `e8`
sites are the complete entry set. All five are `push <id>` + `call` + a cdecl
`add esp,4`; the return address is therefore a unique, static discriminator.

| Return address at `[ESP]` | Call site | Caller |
| --- | --- | --- |
| `0x0049873f` | `0x0049873a` | `0x00498730` stop/query helper ← `0x004987a0` ← VM `0x004997f3` |
| `0x00498bb3` | `0x00498bae` | `0x00498ad0` savegame `MOVI` restore ← `0x004050a2` |
| `0x00498cdd` | `0x00498cd8` | `0x00498c90` play-by-id (VM `0x0049981f`, `0x00499982`; `0x004f6668`) |
| `0x00498efd` | `0x00498ef8` | `0x00498e30` **speech cue** (VM `0x00499849`) |
| `0x004f6615` | `0x004f6610` | `0x004f65f0` track/emitter play helper |

`0x004f65f0` pushes exactly two dwords between its own return address and the
call (`push ecx` at `0x004f65f0`, `push esi` = the id at `0x004f660d`), so at
`0x00498140` entry `[ESP+0x04]` = the id, `[ESP+0x08]` = the helper's frame
slot, `[ESP+0x0c]` = the helper's caller (**`0x0045c60c`** for the sector
selector `0x0045c607`, else `0x00460429` or `0x004f683b`) and `[ESP+0x10]` = the
cue **kind** (`0x5a` for the selector, `0x0045c605 push 0x5a`).

A trampoline that applies the cache only when `[ESP] == 0x004f6615` **and**
`[ESP+0xc] == 0x0045c60c` (optionally also `[ESP+0x10] == 0x5a`) is scoped to
the per-sector cue restart alone and can never reach the speech path at
`0x00498ef8`, the savegame restore or the script VM. Both discriminators are
fixed addresses in a non-relocatable EXE; the verifier pins all seven bytes
sequences of the chain from `0x0045c605` to `0x004f6615`.

## 3. Cue identity available at entry

`Videos`: count `DAT_006070b8` (written at `0x00435214`), base `DAT_00606fb4`,
**stride 0x30** (`0x00435220 lea edi,[ebp+ebp*2]; shl edi,4`). The selector
indexes it with `[sector+0x178]`, the winning index, written in the whole
routine at exactly one place, `0x0045c38b`. The restart reads (`0x0045c5e8`–
`0x0045c607`) `+0x14` = the **media id** (→ `ESI` → the allocator's argument)
and `+0x18`…`+0x2c` = six playback parameters (**inference**) pushed after the
kind `0x5a`, plus a trailing 0. "Already playing" is a *manager-record* test,
not a `Videos` field: the head probe (`0x0045b887`–`0x0045b8b5`) walks
`*0x00606f44` for `[rec+0x10] == id`, and `0x0045c26c`/`0x0045c272` accept it
only when `[rec+0x30] == 0x5a` and `[rec+0x2c] & 4`.

**The stable key at entry is the media id alone**: no record exists yet, and
neither the `Videos` record pointer nor the sector pointer is on the stack at
`0x00498140`. The id is the key the game itself uses (`[rec+0x10]`), so
`(id, kind)` is a complete cache key for the selector scope. The path is
recomputable from the id with `0x004cf460`'s rules: `"%05d.dat"` (`0x00563b20`)
under `"addon\mov\%s"` (`0x00563b2c`) for ids 100–299, 810 and 840–899, else
under the configured movie directory via `"%sData\mov\%s"` (`0x00563b64`);
fallbacks `"soundtrack\%05d.mp3"` (`0x00563b3c`) and `"soundtrack\%05d.wma"`
(`0x00563b50`).

## 4. What `0x004cf460` builds, and what it reports on failure

`0x004cf460` (single caller `0x004981d3`) returns **0** and nothing else. Its
four failure arms (`0x004cfa62`, `0x004cfaca`, `0x004d0114`, `0x004d0162`)
destroy the half-built object through `0x004d1d40`, `xor eax,eax`, unwind the
SEH frame and return. Over its whole gap-free decode (`0x004cf460`–`0x004d0428`)
the **only** globals it writes are the four allocator counters; the HRESULTs it
handles stay in locals and registers (eight `cmp esi,0x8007000e` =
`E_OUTOFMEMORY` retry gates). **So the HRESULT and the failing filter are not
observable at `0x00498140`'s return.** A trace there can log the id, the derived
path candidates, the caller and — with a matching return hook — wall time; the
HRESULT and the failing step need a hook inside `0x004cf460` at those four arms,
whose filter names are static (below). Since 2026-09-17 the gate also writes a
synchronous `media_cue_enter` line at entry (before the span is replayed), so a
build that never returns still names its cue; it changes nothing the routine or
its callers observe (the neutrality argument of §6 is about the REFUSE arm's
early `return 0`, which the entry line does not touch).

### Which filters the EXE creates, and which step reaches quartz

The game ships no `.ax`, links no DirectShow library, and imports from `ole32`
only `CoInitialize`, `CoCreateInstance`, `CoSetProxyBlanket`, `CoUninitialize` —
no `CoRegisterClassObject`, no class factory of its own. **Every filter is a
system COM object**, created with `CoCreateInstance(CLSID, NULL,
CLSCTX_INPROC_SERVER, IID_IBaseFilter, …)` and inserted with
`IFilterGraph::AddFilter` (vtable `+0xc`); the `X …` strings are only the game's
names for them, kept in two `std::string` locals and compared at
`0x004cfea7`–`0x004cfed9` to decide whether the splitter is needed. All eight
`CoCreateInstance` sites are inside `0x004cf460`; names and servers below come
from the bottle's `system.reg` (all present in `system32` as Wine built-ins).

| VA | CLSID | Registered as | Server | Created for |
| --- | --- | --- | --- | --- |
| `0x004cf53a` | `49C47CE5-…` | AMMultiMediaStream | `amstream.dll` | every cue |
| `0x004cfc6f` | `38BE3000-…` | **MPEG Layer-3 Decoder** | `l3codecx.ax` | the soundtrack/MP3 arm (`[esp+0x1c] != 0`) → `X MPEG Layer-3 Decoder` |
| `0x004cfce4` | `94297043-…` | **DMOWrapperFilter** | `qasf.dll` | flags `0x100`+`0x10` (speech); `IDMOWrapperFilter::Init(874131CB-…, DMOCATEGORY_AUDIO_DECODER 57F2DB8B-…)` at `0x004cfd44` → `X WMSpeech Decoder DMO` |
| `0x004cfdc0` | `4A2286E0-…` | **MPEG Audio Decoder** | `quartz.dll` | any other audio cue → `X MPEG Audio Decoder` |
| `0x004cfe52` | `FEB50740-…` | **MPEG Video Decoder** | `quartz.dll` | video cue (`flags & 0x10 == 0`, not the soundtrack arm) |
| `0x004cfeff` | `336475D0-…` | **MPEG-I Stream Splitter** | `quartz.dll` | only when an MPEG audio/video decoder was created |
| `0x004cffbf` | `79376820-…` | DirectSound Audio Renderer | `quartz.dll` | `flags & 0x48 == 0` and config bit `0x4000` clear |
| `0x004d028f` | `F2468580-…` | AMAudioData | `amstream.dll` | the audio sample buffer |

A failed `CoCreateInstance` is **not** fatal: `0x004cfd7c` clears the name and
continues at `0x004cfe03`. The graph is finished either by
`AddSourceFilter(L"X File Source")` + `FindPin(L"Output")` +
**`IGraphBuilder::Render(pin)`** (`0x004d0046`–`0x004d0093`) or, under the
`DAT_00606f34+0x100 & 0x4000` config bit and as the fallback when `Render` fails,
by `IAMMultiMediaStream::OpenFile` (`0x004d00f5`, `0x004d0143`) — both let
quartz choose filters automatically, which under CrossOver is where
winegstreamer is pulled in. So an MP3 soundtrack cue works only if the system
`l3codecx.ax` really decodes; otherwise the automatic `Render`/`OpenFile` reaches
winegstreamer, which per [media-cues.md](../verification/media-cues.md) has no
MP3 or MPEG decoder (only `wmav2` from the project's plugin). The stalling cue
therefore fails at the `Render`/`OpenFile` arms `0x004d0114`/`0x004d0162`, not at
a `CoCreateInstance` — consistent with the GStreamer criticals seen once or twice
per slow frame. Speech is the one kind the EXE decodes through an explicitly
named DMO, which is why the project's WMSpeech hook fixes it and does not help
the sector cue.

## 5. Hook-site qualification

| Name | Address | Bytes | Len | Shape |
| --- | --- | --- | ---: | --- |
| `media_create_enter` | `0x00498140` | `53 8b 5c 24 08` | 5 | function entry, gate (proceed or `return 0`) |

* **Instruction boundaries.** Exactly two instructions, `push ebx` (1 B) +
  `mov ebx,[esp+8]` (4 B) = 5 B, both whole; the routine decodes gap-free to the
  `ret` at `0x004982ab`.
* **Incoming edges, data references, relocation.** No direct branch inside the
  routine targets the span start or interior; the raw sweep of all 1,247,232
  `.text` byte offsets for `e8/e9/eb/0f8x/7x/e0-e3` encodings reaching
  `0x00498141`–`0x00498144` returns **no hit at all** (unlike the post-pass
  sites, not even an unreachable one). The dword `0x00498140` occurs nowhere in
  the file — no vtable slot, jump table or `.rsrc` coincidence, so no indirect
  entry. No `call`/`jmp`/`jcc` in the span: `rel32_offset = 0` and the arena
  tail is the original bytes verbatim at any arena base.
* **ESP contract — the one subtlety.** The span is *ESP-relative*: `push ebx`
  moves ESP and `mov ebx,[esp+8]` reads the argument through it, so the pair is
  self-consistent only when the tail runs at the game's exact ESP. It does:
  `engine_patch::claim` patches `e9 rel32` → a `jmp [entry]` dispatcher (no
  pushed return address) and `lean_stub` restores ESP exactly before
  `jmp [next]`. Any stub here must preserve ESP byte-for-byte on the proceed arm.
* **Registers and flags.** Incoming flags are dead (first consumer: the
  `test esi,esi` after `malloc`); `EBX/EBP/ESI/EDI` are saved by the routine
  itself; `EAX` carries flags in and the result out. A gate stub's suppress arm
  must leave `EBX/EBP/ESI/EDI` untouched, set `EAX = 0` and `ret` (not `ret 4`:
  all five callers pop with `add esp,4`). The real failure ends
  `xor eax,eax; pop×4; ret`, so a suppress arm ending in the same
  `xor eax,eax; ret` reproduces the caller-visible flags exactly
  (`ZF=1, CF=OF=SF=0, PF=1`); the proceed arm must `popfd` before the tail.
* **Re-entrancy.** The direct-`call` closure of `0x004cf460` is 278 functions
  and contains neither `0x00498140`, the selector `0x0045b720`, `0x004f65f0`,
  `0x00498c90`, `0x00498e30` nor the message pump `0x004d34b0`, so the
  constructor cannot statically re-enter the allocator or the selector. **Not
  excluded**: COM apartment message dispatch inside `CoCreateInstance`/graph
  rendering — the handler must count and pass through a re-entrant or
  foreign-thread hit rather than assume none. The unlocked `*0x00606f44` walks
  at every call site are evidence of a single-threaded subsystem (**inference**).
* **Conflicts.** The span is disjoint from every site in the installed
  `game_phase`, `frame_phase`, `pass_phase`, `loop_phase` and proposed
  `post_phase` tables.

A site inside the selector is **not needed**; scoping by return address at the
entry is exact. If a call-site patch is preferred, `0x004f6610` is the right one
(`engine_patch::claim_call` validates the encoded target); `0x0045c607` would
also skip the play helper's bookkeeping, which a create failure does not.

## 6. Behaviour neutrality of an early `return 0`

What a *real* failed build leaves behind, from §1, against the early return:

| Real failure | After an early `return 0` at entry |
| --- | --- |
| 0x40-byte record allocated then freed | never allocated; no leak either way |
| id→flags map writes to `[rec+0x2c]`/`[rec+0x30]`, `[rec+0x24] = 0` | on the discarded record only; unobservable |
| **nothing linked into `*0x00606f44`** | same |
| live counters `0x006085f4`/`0x006085f8` `+0x40` then `-0x40` | untouched — net identical |
| lifetime counters `0x006089f8 += 0x40`, `0x006089fc += 1`, never unwound | **not incremented — the only divergence** |
| `EAX = 0`, `EBX/EBP/ESI/EDI` restored, caller pops the argument | same |
| the constructor's COM objects, file probes (all released/discarded) | skipped |

The lifetime counters are written in ~190 places image-wide and read in exactly
two, `0x004e3f9d` and `0x004e402d`, both inside the status-line formatter for
`"%d fps, Warp: %d%% (real %d%%), %3.3f MB, %d Allocs"` (`0x0056494c`). No
simulation, AI, economy or script branch reads them; the divergence is a smaller
displayed allocation count, in the direction the fix intends.

Nothing on the caller's side depends on the create having been attempted:
`0x004f65f0` re-scans the manager list afterwards and, finding no record for the
id, executes `pop ecx; ret` (`0x004f6633`); it sets `[rec+0x2c] |= 4`,
`[rec+0x30] = kind` and calls `0x00498c90` only on the success arm. The selector
performs its own mutations before the call and regardless of the outcome — the
winning index `[sector+0x178]` at `0x0045c38b`, the "already playing" result at
`[esp+0x38]` (cleared `0x0045b8bc`, set `0x0045b974`), the "restart forced" flag
at `[esp+0x44]` (`0x0045c1e4`/`0x0045c262`) and the stop call `0x00498810` at
`0x0045c27d` — so **the retry continues either way**: the cache removes the cost
of the retry, not the retry, and the selector's state machine is bit-identical.

**What the player loses**: that cue's music or video stays silent for the
backoff window instead of being re-attempted every frame, so use an expiring
backoff keyed on `(id, kind)` and clear it on sector change. Under §2's scoping
this touches only the per-sector soundtrack/ambience cue; speech, menu videos,
cutscenes, script playback and savegame restore never reach the gate.

## 7. What this does not establish

* No runtime measurement: which id the stalling sector selects, and whether it
  resolves under `addon\mov` or `soundtrack\`, is unknown (`Videos` is runtime
  data, not EXE bytes); the trace of §3–4 is what would answer it.
* The HRESULT and failing filter are unreachable from this site (§4); a hook
  inside `0x004cf460` is separate work. COM-internal message dispatch during
  construction is not statically excluded (§5). Whether the bottle's
  `l3codecx.ax` and `quartz.dll` MPEG filters actually decode (rather than
  registering a CLSID and failing to connect) was not tested — only that the
  servers exist and the EXE asks for them.
* The six `Videos` playback parameters and the single-thread claim are
  inferences from layout and from the unlocked list walks.

## 8. Video consumer and the comm-dialog path

2026-09-17. Same discipline as the header: nothing was launched, no Wine
command was run. Ghidra 12.1.3 headless on `/tmp/x3-ghidra-research/X3Render`
(`-process X3AP.exe -noanalysis -readOnly`, `X3CameraState.java` dumps to
`/tmp/x3-video-consumer/`, untracked); every cited site re-checked with
`i686-w64-mingw32-objdump` on the installed EXE (SHA-256 `fdbf3418…`,
2,153,984 bytes, `.rdata` VA `0x00532000` → file `0x130c00`). The GUID
inventory and the branch sweeps are direct PE parses of the same file.

**Question.** How does the EXE consume the graph's video output, what does the
main loop wait on, and is the run100/101 comm-dialog freeze a game-side
expectation Wine does not meet (the voice-DMO pattern) or a sink hang?

**Answer.** The game consumes video by polling `amstream`'s own
`IDirectDrawMediaStream`: it never inserts a filter, never asks for a renderer,
never takes a window and never waits on anything. There is **no game-named
video component** to substitute, so the DMO-fallback pattern has no analogue
here. The freeze is therefore a block inside a Wine call made on the main
thread, and §8.5 narrows it to eleven call sites.

### 8.1 The complete DirectShow/DirectDraw GUID inventory of the image

A byte scan of all 2,153,984 bytes for 48 known DirectShow/amstream/DirectDraw
GUIDs finds exactly **20**, all in `.rdata`, and — except `IID_IUnknown` — each
is referenced from exactly one `.text` site, all inside `0x004cf460`:

| VA | GUID | Referenced at |
| --- | --- | --- |
| `0x00532aa4` | `IID_IBaseFilter` | `004cfc62`, `004cfcd8`, `004cfdb3`, `004cfe45`, `004cfef2`, `004cffb2` (the six `CoCreateInstance` riids) |
| `0x00532ab4` | **`IID_IDirectDrawMediaStream`** | `004d01aa` |
| `0x00532ac4` | `IID_IAudioData` | `004d0282` |
| `0x00532ad4` | `IID_IAudioMediaStream` | `004cfb15`, `004d021e` |
| `0x00532ae4` / `0x00532af4` | `CLSID_AMMultiMediaStream` / `IID_IAMMultiMediaStream` | `004cf51e` / `004cf516` |
| `0x00532b04` | `IID_IBasicAudio` | `004d03d9` |
| `0x00532b14` / `0x00532b24` | `IID_IMediaPosition` / `IID_IMediaControl` | `004d0385` / `004d03a0` |
| `0x00532b34`…`0x00532b74` | `CLSID_DSoundRender`, `CLSID_MPEGAudioCodec`, `CLSID_CMpegVideoCodec`, `CLSID_MPEG1Splitter`, `CLSID_AMAudioData` | the `CoCreateInstance` sites of §4 |
| `0x00532b84` / `0x00532b94` | `MSPID_PrimaryAudio` / `MSPID_PrimaryVideo` | `004cf5d9`, `004cf622`, `004d0204` / `004cf57e`, `004d0194` |
| `0x00532ba4` | `IID_IUnknown` | `004b4946` (unrelated) |
| `0x00563af0`/`0x00563b00`/`0x00563b10` | `CLSID_DMOWrapperFilter`, `CLSID_WMSpeechDecoder`, `CLSID_MP3Decoder` | `004cfce0`, `004cfd3f`, `004cfc6b` |

**Absent from the whole image**: `IID_IPin`, `IID_IMemInputPin`,
`IID_IMemAllocator`, `IID_IFilterGraph`, `IID_IGraphBuilder`, `IID_IMediaEvent`,
`IID_IMediaEventEx`, `IID_IMediaSeeking`, `IID_IBasicVideo`, `IID_IBasicVideo2`,
`IID_IVideoWindow`, `IID_ISampleGrabber`, `CLSID_SampleGrabber`,
`CLSID_NullRenderer`, `CLSID_VideoRenderer`, `CLSID_FilterGraph`, both VMR
CLSIDs and `IID_IVMRSurfaceAllocator9`. The name strings `CoRegisterClassObject`,
`CreateThread`, `WaitForSingleObject`, `WaitForMultipleObjects`,
`MsgWaitForMultipleObjects` and `DirectDrawCreate` occur **zero** times
(byte count over the file; `CoCreateInstance` occurs once).

Consequences, all load-bearing:

* the EXE **implements no `IBaseFilter`, `IPin`, `IMemInputPin`, allocator or
  allocator-presenter**. A COM object of its own passed into a graph would have
  to answer `QueryInterface` for `IID_IPin`/`IID_IMemInputPin`, whose constants
  do not exist in the file, and there is no class factory to register;
* there is no sample grabber, no null renderer, no VMR, no `IVideoWindow`, no
  `GetCurrentImage`, and **no window handle is ever handed to a renderer**;
* `IGraphBuilder`/`IFilterGraph` are used without an IID because the pointer
  comes from `IAMMultiMediaStream::GetFilterGraph` (`+0x34`) directly.

### 8.2 The sink is `amstream`'s own DirectDraw media stream

| Site | Call |
| --- | --- |
| `0x004cf56b` | `[obj+0xb0] = 1` (the video pump state, set before the stream exists) |
| `0x004cf587` | `IAMMultiMediaStream::AddMediaStream(*0x00608ad4, MSPID_PrimaryVideo, 0, NULL)` (`+0x3c`), guarded by `flags & 0x10 == 0` at `0x004cf55e` |
| `0x004d0199` | `IAMMultiMediaStream::GetMediaStream(MSPID_PrimaryVideo, &obj+0x08)` (`+0x10`) |
| `0x004d01af` | `obj[0x08]->QueryInterface(IID_IDirectDrawMediaStream, &obj+0x0c)` (`vtable[0]`) |
| `0x004d01bd` | on failure: `flags & 8` → destroy via `0x004d0160`; else `flags \|= 0x10` and `0x004d1b70` demotes the object to audio-only |

`0x00608ad4` has **exactly one reference in the image** — the read at
`0x004cf575` — and no writer anywhere, so the stream object argument is always
`NULL`: `amstream` must create its own `IDirectDraw` and its own surfaces. The
game supplies no DirectDraw object, imports no `ddraw` entry point and never
calls `IDirectDrawMediaStream::SetFormat` (`+0x28`) or `SetDirectDraw`
(`+0x30`); it accepts whatever surface format the graph negotiated.

Media-object layout used by the video path (`malloc(0xb4)` at `0x004cf4aa`,
retried at `0x004cf4c2` through `0x004b8b60`):

| Offset | Contents |
| --- | --- |
| `+0x04` | `IAMMultiMediaStream` |
| `+0x08` / `+0x0c` | primary-video `IMediaStream` / `IDirectDrawMediaStream` |
| `+0x10` / `+0x14` | `IDirectDrawSurface` / `IDirectDrawStreamSample` |
| `+0x70` / `+0x74` / `+0x6c` | `IMediaPosition` / `IMediaControl` / `IBasicAudio` |
| `+0x78` / `+0xa0` | `IGraphBuilder` / the `L"X File Source"` `IBaseFilter` |
| `+0x7c` | the `RECT` returned by `GetSurface` |
| `+0x8c` | constructor flags |
| `+0xb0` | video pump state: 1 = needs `Update`, 2 = `Update` outstanding, 4 = frame ready |

### 8.3 How a decoded frame reaches a D3D surface, and what the loop calls

`0x00498370` (the manager update, one of the five per-iteration calls of the
main loop) processes only records with `[rec+0x2c] & 2` and, when
`[rec+0x2c] & 4`, refreshes the destination first
(`0x004983c4`–`0x004983d1`): `idx = (short)[rec+0x30]`, bounds-checked against
`DAT_006069b0 + DAT_006069b4`, then `[rec+0x28] = *(0x006069ac + idx*0x10 + 8)`.
It then calls the pump with **`ECX` = the record and `EAX` = `[rec+0x28]`**
(`0x004983d4 mov eax,[edi+0x28]; 004983d7 mov ecx,edi; 004983d9 call 0x4d14e0`)
and switches on the returned `short`: `1` → `0x004d0600` (position → ms),
`2` → finished, `0` → destroy.

`0x004d14e0`, video arm (taken when `[obj+0x8c] & 0x10 == 0`, `EAX != 0`,
`[EAX+0x30] != 0` and `[obj+0x14] != 0`), a three-state machine on `[obj+0xb0]`:

| State | Call | Site |
| --- | --- | --- |
| 2 | `IStreamSample::CompletionStatus(0, 0)` (`+0x1c`) — flags 0, **timeout 0** | `0x004d154b` |
| 1 | `IStreamSample::GetSampleTimes` (`+0x10`) | `0x004d15fe` |
| 1 | `IStreamSample::Update(SSUPDATE_ASYNC=1, NULL, NULL, 0)` (`+0x18`) | `0x004d162b` |
| 4 | `IMediaPosition::get_CurrentPosition` (`+0x24`), only when `[obj+0x94] > 0` | `0x004d16f0` |
| 4 | the blit `0x004d0c40([obj+0x10], [[rec+0x28]+0x30])`, cdecl, 2 args | `0x004d1735` |

`CompletionStatus` decodes: `0x40001` `MS_S_PENDING` → return 1 (try again next
frame); `0x40002` `MS_S_NOUPDATE` → state 1; `0x40003` `MS_S_ENDOFSTREAM` →
`IMediaControl::Pause` and return 2; `0x80004004` `E_ABORT` → state 1; `S_OK` →
state 4. `Update` decodes additionally `0x80070057` `E_INVALIDARG` and
`0x80004003` `E_POINTER` → 0 (destroy the record) and `0x80040406` `MS_E_BUSY`
→ `[obj+0x48] = 2`. Each of the three calls is wrapped in a **two-attempt**
retry that re-runs only on a negative HRESULT and invokes the memory-recovery
helper `0x004b8b60` on `E_OUTOFMEMORY`.

`0x004d0c40(IDirectDrawSurface *src, IDirect3DSurface9 *dst)` is the whole
consumer, a CPU copy:

| Site | Call |
| --- | --- |
| `0x004d0c7f` | `src->Lock(NULL, &DDSURFACEDESC{dwSize=0x6c}, 0, NULL)` (`+0x64`), two attempts |
| `0x004d0cd3` | `dst->GetDesc(&D3DSURFACE_DESC)` (`+0x30`); on failure `src->Unlock(NULL)` at `0x004d0ce4` and return 0 |
| `0x004d0d24` | `dst->LockRect(&D3DLOCKED_RECT, NULL, **Flags = 0**)` (`+0x34`), two attempts |
| — | software convert: source `ddpfPixelFormat.dwSize == 0x20` and `dwRGBBitCount` **16** (mask/shift extraction from `dwR/G/BBitMask`) or **32**; three scalings chosen by comparing widths — 1:1, 2× point up, 2× box down; destination bytes are always `B,G,R,0xFF` (`D3DFMT_X8R8G8B8`/`A8R8G8B8`) |
| `0x004d14b7` / `0x004d14cb` | `dst->UnlockRect()` (`+0x38`) then `src->Unlock(NULL)` (`+0x80`), return 1 |

Any other source depth (24 bpp, or a FourCC/YUV surface) falls through every
arm, still unlocks and still returns 1 — a silently black video, not an error.
**The surface the proxy sees is `dst`**: `[[rec+0x28]+0x30]`, i.e. slot `idx` of
the engine object table at `0x006069ac`, locked with `Flags = 0` once per frame
while the cue plays.

The sample itself is created lazily by the play path `0x004d1870`
(`EAX` = the record), not by the constructor:

| Site | Call |
| --- | --- |
| `0x004d18b0` | `IDirectDrawMediaStream::CreateSample(NULL, NULL, 0, &obj+0x14)` (`+0x34`) — **NULL surface**, so `amstream` allocates it |
| `0x004d1912` | `IDirectDrawStreamSample::GetSurface(&obj+0x10, obj+0x7c)` (`+0x20`) |
| `0x004d198b` | `IDirectDrawSurface::GetSurfaceDesc(&DDSURFACEDESC{0x6c})` (`+0x58`) |
| `0x004d19fa` | `IMediaControl::Run()` (`+0x1c`) |

**What the main loop waits on: nothing.** Over the whole media region
`0x004cf460`–`0x004d1f00` the only methods ever called on `[obj+0x74]`
(`IMediaControl`) are `+0x1c` `Run`, `+0x20` `Pause` and `+0x24` `Stop`; the
only ones on `[obj+0x04]` (`IAMMultiMediaStream`) are `+0x10` `GetMediaStream`,
`+0x1c` `SetState`, `+0x30` `Initialize`, `+0x3c` `AddMediaStream` and `+0x40`
`OpenFile`. There is **no `IMediaControl::GetState` (`+0x28`)**, no
`IMultiMediaStream::GetState` (`+0x18`), no `GetEndOfStreamEventHandle`
(`+0x2c`), no `WaitForCompletion`, no `IMediaEvent` at all; `Update` is issued
with a NULL event and NULL APC; `CompletionStatus` is called with timeout 0;
every retry is bounded at two attempts. Combined with §1 of
[voice-startup-sequence.md](voice-startup-sequence.md) (no thread, no wait
import), **the game cannot block by construction — it polls once per pump.**

### 8.4 Which cues build a video branch: the comm avatar vs. the sector selector

The id→flags map at `0x0049819f`–`0x00498246`, byte-checked:

| id | flags passed to `0x004cf460` | also written |
| --- | --- | --- |
| `1`, `3` | **`8`** | `[rec+0x2c] \|= 4`, `[rec+0x30] = 0x42` |
| `2` | **`8`** | `[rec+0x2c] \|= 4`, `[rec+0x30] = 0x3d` |
| `800`, `10001` | `8` | — |
| `810`, `811` | `0x100` | — |
| `101`–`999` | `0x110` | — |
| anything else | `0` | — |

`0x004cf55e` tests `flags & 0x10`: when set, **no primary video stream is ever
added** and `0x004d017e` skips the `IDirectDrawMediaStream` acquisition
entirely. So:

* the **sector selector** path of §2 (`0x0045c607` → `0x004f65f0` →
  `0x004f6610`, cue kind `0x5a`) selects `Videos` ids in `101`–`999` → flags
  `0x110` → `0x10` set → **audio-only, no video branch at all**. The selector is
  not on the video path;
* ids `1`, `2`, `3` get flags `8`: `0x10` clear (video stream added) and `8` set
  (primary **audio** stream suppressed, `0x004cf5xx`) — the only silent-video
  kind. For these the `IDirectDrawMediaStream` QI is **mandatory**: the
  `flags & 8` arm at `0x004d01bd` destroys the object instead of demoting it;
* `[rec+0x30]` doubles as the destination slot for §8.3's table lookup
  (`0x42` for ids 1 and 3, `0x3d` for id 2) and `[rec+0x2c] |= 4` is exactly the
  bit `0x00498370` needs to refresh `[rec+0x28]` — i.e. these three ids are
  wired to fixed engine render targets at map time.

The comm-dialog avatar reaches `0x00498140` through the **script-VM play-by-id**
caller `0x00498cd8` (`0x00498c90`, VM `0x0049981f`/`0x00499982`), not through
`0x004f6610`. `0x00498c90` then runs, in the same frame,
`0x004d0430` (`IMediaControl::Pause` + `IMediaPosition::put_CurrentPosition`)
and `0x004d1870` (the four calls above), and sets `[rec+0x2c] |= 2` only on
success — so the record starts being pumped on the next `0x00498370`.

**This matches the run101 evidence exactly.** In
`/tmp/x3-bottleX3-run101/session-20260917-000809-212.log` the six completed
creates are ids `144`/`244` (`flags=0x110`), `1` (`flags=0x8`), `8404`/`8509`
(`flags=0x90`) and `2004` (`flags=0xd0`) — every one of them has bit `0x10` set
in the incoming `EAX`, i.e. **no video stream**. The **last line of the log** is
`media_cue frame=1006 id=1 kind=none caller=script flags=0x0 result=0x1343ed10
us=102171`: `EAX = 0` means the map applies, id 1 → flags `8`, which is the
session's **only** media object with a primary video stream. It was created
successfully (non-zero `result`) in 102 ms; frame 1007's telemetry then
completed in full; nothing follows. The freeze begins one to two pumps after
the first and only video graph starts.

### 8.5 Verdict

**Not the voice pattern.** The voice hang was the EXE naming a specific
component — `IDMOWrapperFilter::Init(CLSID_WMSpeechDecoder,
DMOCATEGORY_AUDIO_DECODER)` at `0x004cfd44` — that the bottle did not provide,
which is why `X3M_VOICE_DMO_FALLBACK` could substitute one. The video branch
names **no component at all**: it adds `CLSID_CMpegVideoCodec` and
`CLSID_MPEG1Splitter` as hints, then lets `IGraphBuilder::Render`
(`0x004d0093`) or `IAMMultiMediaStream::OpenFile` (`0x004d00f5`/`0x004d0143`)
choose, and the sink is `amstream`'s internal DirectDraw stream. There is
nothing for a proxy to supply and nothing the game is waiting to be told.

**Therefore the block is inside a Wine call on the main thread**, and the
candidate set is exactly these eleven sites, in the order they are first
reached for id 1: `0x004d0093`/`0x004d0143` (`Render`/`OpenFile`, already known
to have returned — the create logged `result != 0`), `0x004d03f5`
(`SetState(STREAMSTATE_RUN)`), `0x004d0407` (`Pause`) — also returned —, then
`0x004d18b0` (`CreateSample(NULL, …)`, where Wine's `amstream` must create a
`IDirectDraw` of its own), `0x004d1912` (`GetSurface`), `0x004d198b`
(`GetSurfaceDesc`), `0x004d19fa` (`Run`), and per frame `0x004d154b`
(`CompletionStatus`), `0x004d162b` (`Update`), `0x004d0c7f`
(`IDirectDrawSurface::Lock`) and `0x004d0d24`
(`IDirect3DSurface9::LockRect`). Since frames 1006 and 1007 completed, the
first four are excluded by the evidence and the realistic set is the last
seven — with `0x004d18b0` and `0x004d0c7f` the strongest candidates, because
both force Wine to instantiate and map a DirectDraw surface inside a process
that already holds a D3D9 device (**inference**, not established here).

**The game-side expectations Wine must meet**, none of which is a named
component, all of which are hard requirements for ids 1/2/3:

1. `AddMediaStream(NULL, MSPID_PrimaryVideo, 0, NULL)` must succeed **with a
   NULL stream object** — `amstream` provides the DirectDraw;
2. the primary video stream must `QueryInterface` to
   `IID_IDirectDrawMediaStream`, or the object is destroyed (`0x004d01bd`);
3. `CreateSample(NULL, NULL, 0, …)` must allocate its own surface;
4. `GetSurface` must return a lockable `IDirectDrawSurface` whose
   `ddpfPixelFormat` is **16- or 32-bpp RGB** (anything else renders black);
5. `Update(SSUPDATE_ASYNC, NULL, NULL, 0)` must be genuinely asynchronous — it
   is called on the thread that also runs the message pump and the renderer;
6. `CompletionStatus(0, 0)` must be a true zero-timeout poll and must eventually
   report `S_OK` or `MS_S_ENDOFSTREAM`;
7. `IDirectDrawSurface::Lock(NULL, …, 0, NULL)` — **no `DDLOCK_WAIT`,
   no `DDLOCK_NOSYSLOCK`** — and `IDirect3DSurface9::LockRect(…, NULL, 0)` —
   **no `D3DLOCK_DISCARD`/`NOSYSLOCK`/`READONLY`** — must return rather than
   block behind GPU or CS work.

**A proxy-side fix, if one is wanted**, has three shapes and none is a decoder
substitution: (a) refuse video cues at `0x00498140` for the video ids the way
the sector cache already refuses selector cues — behaviourally the avatar stays
black, identical to the pre-v5 state; (b) intercept `0x004d14e0`/`0x004d1870`
and force the `flags |= 0x10` demotion that `0x004d01bd` already implements, so
the object degrades to the audio-only path the game itself uses when the QI
fails; (c) leave the graph alone and make the destination lock cheap in the
D3D proxy. Only (a) and (b) are in the project's existing idiom.

**Does the game demux?** No. It builds a path with `"%05d.dat"`
(`0x00563b20`) under `"addon\mov\%s"` (`0x00563b2c`) or the configured movie
directory, converts it to a `WCHAR` buffer and hands **the path** to
`IAMMultiMediaStream::OpenFile` or to `IGraphBuilder::AddSourceFilter(path,
L"X File Source")` + `IBaseFilter::FindPin(L"Output")` + `Render`. It never
reads a byte of the container. Transcoding `mov\*.dat` is therefore possible
in principle, with three hard constraints: the file must keep the `%05d.dat`
name and location; the new format must be decodable by a **DirectShow filter or
DMO reachable inside the bottle** (registry-registered, or autoplugged by
winegstreamer through `Render`/`OpenFile`); and the decoder's output must
connect to `amstream`'s primary video pin as **RGB16 or RGB32**. A host-side
macOS decoder such as VideoToolbox is not reachable from this graph at all —
the only bridge would be a GStreamer element inside `winegstreamer`, i.e. the
same plugin mechanism as the v5 audio experiment.

### 8.6 Hook-site qualification for the next trace build

If the remaining seven candidates are to be separated, the three enclosing
functions are hookable; the indirect `call edx`/`call eax` sites themselves are
2 bytes and are not.

| Name | Address | Bytes | Len | Shape |
| --- | --- | --- | ---: | --- |
| `media_video_pump` | `0x004d14e0` | `83 ec 24 53 55` | 5 | `sub esp,0x24` + `push ebx` + `push ebp` |
| `media_video_run` | `0x004d1870` | `83 ec 70 53 8b 58 24` | 7 | `sub esp,0x70` + `push ebx` + `mov ebx,[eax+0x24]` |
| `media_video_blit` | `0x004d0c40` | `81 ec 14 01 00 00` | 6 | one instruction, `sub esp,0x114` |

* **Instruction boundaries.** Each span is a whole number of instructions
  (5/7/6 bytes); all three functions decode gap-free to their `ret`.
* **Incoming edges and data references.** A raw sweep of all 1,247,232 `.text`
  byte offsets for `e8/e9/eb/0f8x/7x/e0-e3` encodings reaching the interior of
  any of the three spans returns **no hit**, and the dwords `0x004d14e0`,
  `0x004d1870`, `0x004d0c40` occur **nowhere in the file** — no vtable slot, no
  jump table, no indirect entry. Direct callers:
  `0x004983d9` for the pump, `0x00498d71` (play-by-id) and `0x00498f76` (speech
  cue) for the run helper, `0x004d1735` for the blit.
* **ESP contract.** All three spans begin with `sub esp,N` and, for two of
  them, pushes: like `0x00498140` the tail is ESP-relative and only valid at
  the game's exact ESP, so a stub must restore ESP byte-for-byte.
* **Registers and flags.** `0x004d14e0` takes `ECX` = the record and `EAX` =
  `[rec+0x28]` (neither is a standard `__fastcall` pair — `EDX` is scratch) and
  returns a `short` in `AX`; `0x004d1870` takes `EAX` = the record and returns
  0/1; `0x004d0c40` is cdecl with two stack arguments. In all three, incoming
  flags are dead (the first flag consumer follows an `xor`/`cmp` that the
  routine itself sets) and `EBX/EBP/ESI/EDI` are saved by the routine.
* **Re-entrancy.** Same caveat as §5: COM apartment message dispatch inside the
  called Wine methods is not statically excluded, and `0x004d14e0` runs from
  `0x00498370`, which is itself reached from six sites including the two
  loading-screen pumps — a handler must count re-entrant hits rather than
  assume none.
* **Cheaper first step.** No new hook is needed to learn whether the blit was
  reached: the destination of `0x004d0d24` is a `IDirect3DSurface9` the proxy
  itself handed out, so a single `LockRect` observation on the surface behind
  object-table slot `0x42` distinguishes "hung before the first frame arrived"
  (no lock ever) from "hung in the copy" (lock entered, unlock never).

### 8.7 What §8 does not establish

* Which of the seven remaining call sites blocks. That needs either the trace
  build of §8.6 or a hang witness with guest `EIP`; the run-31/replica work in
  [voice-startup-sequence.md](voice-startup-sequence.md) §9 shows host-only
  samples cannot answer it.
* Whether Wine's `amstream` actually creates a DirectDraw object at
  `0x004d18b0` and what it does when the process holds a D3D9 device. That is a
  claim about the installed CrossOver DLLs, not about these bytes; the public
  Wine source was not re-read for this section.
* The identity of the engine object table at `0x006069ac` beyond "slot `idx`,
  stride 0x10, pointer at `+8`, D3D surface at `+0x30`", and of the helper
  `0x004df250` called between `GetDesc` and `LockRect`.
* That ids 1/2/3 are the comm-dialog avatar rather than some other fixed-slot
  overlay. What is established is that id 1 is the only video-building cue in
  run101, that it is the last event before the freeze, and that its destination
  slot is fixed at `0x42` by the map.
* The cheaper first step of §8.6 is implemented: with `--media-cue-trace
  --ownership` the proxy's surface shell writes `media_video_blit` lines for
  every `LockRect`/`UnlockRect` returning into `[0x004d0c40, 0x004d14e0)` —
  the bound is the next function, the pump of §8.6, since the consumer decodes
  gap-free to its `ret` and the pump has no interior edge (enter and result,
  first lock, one per 60, first unlock;
  [docs/verification/media-cues.md](../verification/media-cues.md) §6), so the
  next comm-dialog log separates "no frame reached D3D", "hung inside the
  lock/unlock" and "hung in the copy" without a new hook.

## Reproduce

```sh
PYTHONPATH=verification/probe python3 verification/probe/verify_media_cue_site.py
i686-w64-mingw32-objdump -d -Mintel --insn-width=16 \
  --start-address=0x498140 --stop-address=0x4982ac "$X3AP"
i686-w64-mingw32-objdump -d -Mintel --insn-width=10 \
  --start-address=0x4d017e --stop-address=0x4d01e0 "$X3AP"   # video QI
i686-w64-mingw32-objdump -d -Mintel --insn-width=10 \
  --start-address=0x49819f --stop-address=0x498248 "$X3AP"   # id -> flags map
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -noanalysis -readOnly \
  -scriptPath tools/analysis -postScript X3CameraState.java \
  /tmp/x3-video-consumer/a.txt dec:004cf460 dec:004d14e0 dec:004d0c40 \
  dec:00498370 dec:004d1870 dec:004d0430 data:00608ad4
```
