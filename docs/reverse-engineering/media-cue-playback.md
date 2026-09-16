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
whose filter names are static (below).

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

## Reproduce

```sh
PYTHONPATH=verification/probe python3 verification/probe/verify_media_cue_site.py
i686-w64-mingw32-objdump -d -Mintel --insn-width=16 \
  --start-address=0x498140 --stop-address=0x4982ac "$X3AP"
```
