# "Use Alternative Video Playback" (launcher checkbox)

2026-09-23. Static study of X3AP.exe, SHA-256 `fdbf3418d8f0a897…` (the bottle
copy), and of the bottle's Wine `syswow64/amstream.dll`, SHA-256
`a280ac310690a7fb…`. Nothing was launched, no Wine. Every byte, constant and
census count below is pinned by
[`verify_alt_video_sites.py`](../../verification/results/alternative-video-playback/verify_alt_video_sites.py)
(`PASS`: 29 EXE byte patterns, 10 GUIDs, 11 strings, a 12-CLSID whole-file census, the bit-`0x4000` census,
the effective caption from the text archives, 5 amstream patterns; output beside
it in `verify_output.json`). Raw disassembly stayed local. Builds on
[media-cue-playback.md §4](media-cue-playback.md#4-what-0x004cf460-builds-and-what-it-reports-on-failure)
(the filter list of the media-object constructor `0x004cf460`) and
[music-restart.md](music-restart.md) (the `MOV_` commands and record flags).
Inferences are marked.

**Question.** What does the Egosoft launcher's "Use alternative video playback"
checkbox do in the engine; which `mov/*` playback path does it select; could it
restore the omitted ID2 clip; does it touch music; what would the proxy need to
test it?

**Answer.** It is bit **`0x4000` of `VideoD3DFlags2`** (`*0x00606f34+0x100`).
The bit selects no alternative renderer, decoder, source filter or texture path.
It is read in exactly three places, all inside the media-object constructor
`0x004cf460`, and all three concern **audio rendering and who finishes the
graph**: (D1) the primary-audio stream is added with `AMMSF_ADDDEFAULTRENDERER`
instead of as an app-read audio stream, (D2) the engine stops pre-adding the
DirectSound Audio Renderer, (D3) the engine stops building the graph itself
(`AddSourceFilter` + `Render`) and always uses `IAMMultiMediaStream::OpenFile`.
The resource name of the control is **"Disable Manual Codec Control"**; the
caption the user sees comes from the text database. The video chain (MPEG-I
splitter → MPEG Video Decoder → amstream DirectDraw stream) does not depend on
the bit, so it cannot restore ID2, and for audio-only records (music, speech)
the bit is never reached or has no effect.

## 1. Storage, load, save and the dialog

| What | Where | Detail |
| --- | --- | --- |
| Registry value | `HKCU\Software\EGOSOFT\X3AP` → `VideoD3DFlags2` | key format `Software\EGOSOFT\%s` (`0x00562bb8`) with the app name from `[*0x00606f34+0xd8]`; value name `0x00562c94` |
| Load | `0x004b6f60` (`RegOpenKeyExA(HKCU, …, KEY_READ)`, then `RegQueryValueExA` per value) | `0x004b7291 mov [eax+0x100],edx` — the whole dword into the settings word |
| Save | `0x004b7b40` (`RegCreateKeyExA`) | `0x004b7e27`–`0x004b7e4b`: `RegSetValueExA(…, "VideoD3DFlags2", 0, **REG_BINARY (3)**, &word, 4)`; callers `0x00401e42`, `0x004b88d9`, `0x004ce05f` (launcher start) |
| Default | `0x004b6d2a` (in the defaults routine `0x004b6be0`) | `mov [ecx+0x100],0x281` — bit clear |
| Startup | `0x00402ee6`–`0x00402f3e` | unless `[*0x00606f34+0x108] & 8`, calls the loader (`0x00402f01` → `0x004b7400` → `0x004b6f60`) and ORs the pre-load bit `8` back; then always sets bit `8` (`0x00402f3e`) |
| Bottle X3 now (observation) | `user.reg` `[Software\\EGOSOFT\\X3AP]` | `"VideoD3DFlags2"=hex:89,02,00,00` = `0x289`: **bit `0x4000` clear** |

The glow note calls the value `REG_DWORD`; the reader accepts either type, the
writer stores `REG_BINARY`, which is what the bottle holds.

**Launcher dialog.** Graphic Settings is dialog resource 110, proc `0x004ccaf0`
(`DialogBoxParamA` at `0x004cc083`). Control **1258** (`0x4ea`), a checkbox in
the "Troubleshooting" group, has the resource text "Disable Manual Codec
Control" (`.rsrc` `0x006f2788`). At init the proc replaces every caption with
text page 1912 (`0x778`): `0x004cda36` `push 0x4ea; mov edx,0x778; call 0x004ab200`
→ `SetWindowTextA(GetDlgItem(dlg,1258))`. The effective English entry
(`addon/04.cat`, `addon/t/0001-L044.pck`, page 1912 id 1258) is
**"Use Alternative Video &Playback"**; the older `t/0001-L044.pck` in
`03.cat`–`11.cat` has no id 1258 (hence "not present" in
[compositor-and-glow.md](compositor-and-glow.md)).

| Site | Action |
| --- | --- |
| `0x004cd7c3` (WM_INITDIALOG arm `0x004cd563`), `0x004cce36` ("Reset to Default" 1101 arm `0x004ccb9b`, after the defaults routine `0x004b6be0`) | `test [*0x00606f34+0x100],0x4000` → `SendMessageA(item 1258, BM_SETCHECK, bit, 0)` |
| `0x004cd1e3` (OK 1102 arm `0x004cce90`; command ids via `sub 0x3ed` and the byte map `0x004cdb00` → table `0x004cdaec`) | `BM_GETCHECK` on 1258 → `0x004cd1f8 or [..+0x100],0x4000` / `0x004cd204 and [..+0x100],~0x4000` |

**Script access.** Command module `P_` (dispatcher `0x00497b80`, names at
`0x0057a390`) index 9 `P_GetSysD3DFlags2` (`0x00497c68`) returns the whole word;
index 10 `P_SetSysD3DFlags2` (`0x00497c8a`) overwrites it. Whether any shipped
script calls them is not decoded.

**Census.** A raw `.text` scan for every direct disp32 encoding of bit `0x4000`
on `[reg+0x100]` (`test`, `or`, `and ~`, byte test of `+0x101`/`0x40`, `bt 14`)
finds exactly the seven sites above and below: `test` at `0x004cce36`,
`0x004cd7c3`, `0x004cf5a4`, `0x004cff96`, `0x004d0036`; `or` `0x004cd1f8`;
`and` `0x004cd204`. The loads of the whole word into a register
(`0x00402188`, `0x00402efb`, `0x00497c6e`, `0x004b7e2c`, `0x004ce779`,
`0x004ced30`, `0x004d9bba`) test other bits or copy the word. **The engine
consumers are the three constructor sites only.** The bit is read at every
construction, not cached.

## 2. The three branches in `0x004cf460`

Constructor context (all bits are the media-object flags `[m+0x8c]`, i.e. the
incoming flags argument `[ebp+0xc]`): `0x004cf507` forces **`0x40` onto every
audio-only (`0x10`) object**. `CoCreateInstance(CLSID_AMMultiMediaStream,
IID_IAMMultiMediaStream)` → `m+4` (`0x004cf53a`); `Initialize(STREAMTYPE_READ,
AMMSF_NOGRAPHTHREAD, NULL)` (`0x004cf554`); for non-`0x10` objects
`AddMediaStream(*0x00608ad4, MSPID_PrimaryVideo, 0, NULL)` (`0x004cf587`) — the
DirectDraw video stream that the texture path consumes. None of this reads the
bit.

| # | Site | Reached when | Bit clear (default) | Bit set ("alternative") |
| --- | --- | --- | --- | --- |
| D1 | `0x004cf5a4` | `[m+0x8c] & 8 == 0` (`0x004cf591`) | `AddMediaStream(NULL, MSPID_PrimaryAudio, 0, &m+0x18)` (`0x004cf629`): an app-read audio stream, later `QI(IAudioMediaStream)` → `m+0x1c` and `SetFormat` (`0x004cfb14`, `0x004cfba7`) | `AddMediaStream(NULL, MSPID_PrimaryAudio, (flags & 0x40) ? 0 : AMMSF_ADDDEFAULTRENDERER, &m+0x18)` (`0x004cf5c0`–`0x004cf5e0`) |
| D2 | `0x004cff96` | `[m+0x8c] & 0x48 == 0` (`0x004cff87`) | `CoCreateInstance(CLSID_DSoundRender)` → `m+0xac`, `AddFilter(NULL name)` (`0x004cffbf`, `0x004cfff9`) | skipped |
| D3 | `0x004d0036` | local "manual render" `[esp+0x18] != 0` (`0x004d0026`) | `IGraphBuilder::AddSourceFilter(path, L"X File Source")` (`+0x38`, `0x004d0063`) → `IBaseFilter::FindPin(L"Output")` (`+0x2c`) → `IGraphBuilder::Render(pin)` (`+0x30`, `0x004d0093`, two tries); on failure `OpenFile` | `IAMMultiMediaStream::OpenFile(path, 0)` (`+0x40`, `0x004d0143`, two tries) |

On a failed D1 (two tries, `E_OUTOFMEMORY` retry gate) `0x004cf64a` sets
`[m+0x8c] |= 8`: the object has no audio stream, and every later `& 8` test
treats it as silent.

The "manual render" local is set at `0x004cff6f` only when the audio decoder
name is `X MPEG Audio Decoder` **and** the video decoder name is
`X MPEG Video Decoder` (`0x004cff4b`–`0x004cff6d`, `0x00469700` is
`std::string::compare`, 0 = equal): an MPEG-1 A/V clip for which both
`CoCreateInstance`s succeeded. It is never set for audio-only objects (no video
decoder, `0x004cfe1a`) or for objects with bit `8`.

The pre-added decoders are **not** keyed on the bit: MP3 decoder (soundtrack
`.mp3` arm), WMSpeech DMO (`0x100`+`0x10`), MPEG Audio Decoder (non-`0x10`,
no bit `8`), MPEG Video Decoder (non-`0x10`, not soundtrack), MPEG-I splitter
(if an MPEG decoder name is set) — see media-cue-playback.md §4. No video
renderer CLSID occurs anywhere in the EXE (whole-file byte census: Video
Renderer, VMR-7, VMR-9, EVR, Null Renderer, Sample Grabber, Overlay Mixer,
Async Reader, LAV Video and LAV Splitter Source all 0; DSoundRender and
AMMultiMediaStream 1 each); video always goes to the amstream DirectDraw
stream. Both D3 arms let the system choose the source filter (the engine's
`AddSourceFilter`, or `OpenFile`'s own `AddSourceFilter` in amstream).

### What amstream does with the alternative arm (CrossOver bottle)

`syswow64/amstream.dll` (Wine builtin, hash above):

* `AddMediaStream` (`0x1000d21f` loads `ppNewStream` into EBX): when
  `flags & AMMSF_ADDDEFAULTRENDERER` (`0x1000d2b0`) **and `ppNewStream != NULL`**
  it returns `E_INVALIDARG` (`0x1000d35c`) without adding anything. The engine
  always passes `&m+0x18`, so **under Wine D1-alternative always fails** for
  non-`0x40` objects and `0x004cf64a` marks them silent (bit `8`).
* `OpenFile(path, flags)`: unless `AMMSF_NORENDER` it calls
  `IFilterGraph2::RenderEx(pin, ~flags & 1, NULL)` (`0x1000d7f2`–`0x1000d806`);
  the engine's `flags = 0` gives `AM_RENDEREX_RENDERTOEXISTINGRENDERERS`.
  `VFW_S_PARTIAL_RENDER` becomes `S_OK`, `VFW_E_CANNOT_RENDER` becomes
  `VFW_E_CANNOT_CONNECT` (`0x1000d80b`–`0x1000d81d`).

**Net effect under CrossOver** (inference from those bytes, not run): a video
object with flags `0` (no `0x40`) takes the same path as one with flags `8`:
no audio stream, no DSound renderer, no engine `Render`; `OpenFile` renders only
into existing renderers, so the video pin reaches the DirectDraw stream (when a
decoder path exists at all) and the clip's audio pin, if any, stays unconnected (partial render → success): **video
plays silent**. Native Windows `amstream.dll` was not examined; whether it also
rejects `AMMSF_ADDDEFAULTRENDERER` with a non-NULL `ppNewStream` is **unknown**
(if it accepted it, the default DirectSound renderer would play the audio and
`m+0x18` would hold whatever native returns).

## 3. ID2 and music

**ID2** (`mov\00002.dat`, MPEG-1 video elementary stream, requested with flags
`0` or `8`; [media-cues.md](../verification/media-cues.md#id2-video-omission-and-owned-playback-retirement-2026-09-21)):

* Flags `8`: none of D1–D3 is even reached (`& 8` at `0x004cf591`, `& 0x48` at
  `0x004cff87`, no MPEG audio decoder so no manual render). The graph is
  byte-for-byte the same with the bit set or clear.
* Flags `0` with the bit set under Wine: D1 fails, bit `8` is set, and the rest
  is the flags-`8` graph (MPEG Video Decoder + MPEG-I splitter + `OpenFile`).
* That flags-`8` graph (config bit clear, which is the same graph) is exactly
  what the 2026-09-20 standalone fixture ran on the v4 runtime: `OpenFile`
  failed `0x80040217` (`VFW_E_CANNOT_CONNECT`). The video decoder that
  intelligent connect picks is not changed by the bit, so it cannot steer away
  from a crashing video decoder either. The LAVVideo crash (Run55, thread
  `CLAVOutputPin Video`) came with the owned replacement playback that has
  since been retired (media-cues.md); this constructor names no LAV filter.

**The bit cannot restore ID2**; at best it turns a flags-`0` request into the
flags-`8` request that is already known to fail on this bottle.

**Music and other audio-only records** (`MOV_LoadMovie`/`MOV_PlayMovie`, record
flags `0x90`/`0xd0`, speech `0x100|0x10`): `0x004cf507` gives every `0x10` object
`0x40`, so D1 computes flags `0` (identical to the default call), D2 is skipped
by `& 0x48` in both modes, and D3 is never reached (no manual render for
audio-only). **The bit changes nothing for audio-only records**; seek, run,
pause, the pump and the DirectSound streaming path (`0x004d0430`, `0x004d1870`,
`0x004d14e0`, `0x004d0700`) never read it.

What changes, under Wine, is **video clips with an audio track and no `0x40`**
(comm and advert clips with sound, if any are requested without `0x40`): they
would lose their audio (inference above).

## 4. What a test would need

* **The setting alone needs no hook**: set bit `0x4000` in
  `HKCU\Software\EGOSOFT\X3AP\VideoD3DFlags2` (the checkbox writes it; `0x289`
  → `0x4289`). The bit is read per construction, so a proxy could equally set
  it in memory after the settings load, but `0x004b7b40` writes the word back
  to the registry (launcher start, exit paths), so an in-memory toggle is not
  process-local unless the proxy restores it before that save.
* **ID2 is never constructed under the proxy**: the allocator gate
  ([media-cue-playback.md](media-cue-playback.md), `src/proxy/media_cue.cpp`)
  refuses `id==2 && flags∈{0,8}` before `0x004cf460` and has no opt-out. An
  ID2 test would need a source change (a gate opt-out) plus the registry bit,
  and §3 predicts the known `OpenFile` failure on this bottle. **Not
  recommended.**
* A cheaper witness, if one is ever wanted: the existing standalone media
  fixture with its D1 call switched to `AMMSF_ADDDEFAULTRENDERER` would show
  the `E_INVALIDARG` and the silent flags-`8` graph without the game.

## Open

* Native Windows `amstream` behaviour for D1-alternative (non-NULL
  `ppNewStream`) is not established; the Windows outcome for video audio is
  therefore unknown.
* Which script cues request video clips without `0x40` (and so would go
  silent) is not enumerated; nor whether any script calls
  `P_SetSysD3DFlags2`.
* Why Egosoft offered the switch (presumably codec/renderer trouble with the
  engine's own `Render` on some systems) is not recorded in the binary.
