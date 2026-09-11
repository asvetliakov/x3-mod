# Game and bundled D3DX callback routes

Read-only research, 2026-09-11. The reviewed creation paths support observing
D3DX-created resources through the ordinary wrapped device. This audit **does not
prove the absence of private-IUnknown registrations throughout the game**. It
identifies concrete callback routes and the remaining coverage needed by the
[portable replay admission proposal](../architecture/motion-replay-exclusion.md).
No game, GUI, device fixture or GPU work was launched for this research.

## Provenance and scope

All addresses below are preferred VAs (both PE images have preferred base
`0x00400000`), not relocatable hook specifications. Hashes identify the inspected
files; they are not proposed runtime version gates.

| Local input | SHA-256 |
| --- | --- |
| `X3/X3AP.exe` | `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` |
| `X3/d3dx9_37.dll` | `c2ccb84c672a9d8966e82a28005a4269886ee304972ac3590c0b8a9c1622a3d8` |

Used Ghidra 12.1.3's existing analyzed project with `-readOnly -noanalysis`,
targeted decompilation, PE imports/exports and assembly validation of argument
setup. Raw output remains under `/tmp/x3-callback-*.c` and
`/tmp/x3-d3dx-callback-*.c`, outside the repository. The new original
[`X3CallbackResearch.java`](../../tools/analysis/X3CallbackResearch.java) reports
containing functions, direct references and decompilation for explicitly supplied
addresses; it does not analyze every function or resolve arbitrary indirect calls.

## Resource registration: no positive game finding yet

Resource and volume `SetPrivateData` use COM slot 4, offset `0x10` on x86;
`D3DSPD_IUNKNOWN` is flag value `1`, and its valid pointer-sized payload has
size `4` here. The corresponding documented lifetime can call application COM
code when data is replaced/freed or the object is destroyed. Retrieving this
kind of entry increments its interface reference. This is why later inspection
of a few known GUIDs cannot certify that an object never retained a foreign
callback. See Microsoft's [SetPrivateData](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dresource9-setprivatedata)
and [GetPrivateData](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dresource9-getprivatedata)
contracts.

A bounded assembly triage examined loaded slot-4 call candidates in the EXE and
direct slot-4 calls with nearby argument setup in bundled D3DX. **No actual resource
`SetPrivateData(..., D3DSPD_IUNKNOWN)` was confirmed.** This is a search result,
not a whole-program negative proof: register propagation, tail calls, alternate
call shapes and dynamically obtained implementations remain outside that scan.
In particular, selecting a vtable displacement and a literal `push 1` alone
produces convincing false positives:

| Site | Resolved meaning / reason it is not evidence of private registration |
| --- | --- |
| X3 `0x4d590b` | DirectInput8 object from `DirectInput8Create` in `0x4d56d0`; slot 4 enumerates device type 4 with game callback `0x4d4aa0`. |
| X3 `0x4ceb47` | Same five-argument enumeration-shaped call with callback `0x4ce670`; GUID would otherwise be invalid address `4`. Receiver ownership was not fully recovered, so treat the DirectInput identity as a strong lead, not a second independently complete proof. |
| X3 render region `0x4bb8e0`, `0x4bc6a6`, `0x4bcafa` and similar | Slot 4 called with only the mesh `this` pointer; counts feed geometry bookkeeping. These do not have the resource method's four additional arguments. |
| D3DX `0x621c85`, `0x621cf5`, `0x621d32` | Shader validator token submission, resolved from the preceding dynamic factory lookup described below. |

Do not turn these findings into an exemption for D3DX or game callers. A complete
startup registration observer must cover Texture/CubeTexture/VolumeTexture,
Surface, Volume, VB and IB before native dispatch. The proposed permanent veto
still applies to any application/unknown IUnknown registration, including a failed
attempt; GUID identity alone must not exempt a mod sidecar.

## Positive D3DX creation routing

| Exact entry/path | Observed pointer flow | Consequence for wrapper coverage |
| --- | --- | --- |
| `D3DXCreateTexture` `0x5fea53` | Retains its input device in `EDI`; invokes that pointer's CreateTexture slot 23 at `0x5feadc`. | With a wrapped input device, the creation wrapper returns the resource before D3DX sees it. No private native-pointer extraction in this entry. |
| `D3DXCreateMesh` `0x5a5be8` → constructors `0x5a257b` / `0x5a2832` | Constructors store the supplied device and call its AddRef. | Mesh internal allocation alone does not create an unobserved native device. |
| Mesh allocation/resize `0x5919b3` / `0x5926c5` | Use that retained device's CreateIndexBuffer slot 27 at `0x591a9d` / `0x5927ac`, and CreateVertexBuffer slot 26 at `0x591cb0` / `0x5929bf`. | VB/IB output wrapping happens before D3DX can access the returned buffers. Later Lock/Unlock calls use those returned interfaces. |
| X3 effect loader `0x4bae10` | Passes the device stored at `**(renderer + 0x18)` to D3DXCreateEffect at `0x4bafca`. | Supports the same application-device route; actual factory startup coverage must establish that this pointer is wrapped. |
| X3 environment helper `0x4c4100` | Passes the same renderer device to D3DXCreateRenderToEnvMap at `0x4c4145`, and also directly creates cube textures with it. | Another useful input-device identity anchor; the full environment helper's later resource operations were not exhaustively followed. |

The mesh/texture constructors probe `IID_IDirect3DDevice9Ex`
(`b18b10ce-2649-405a-870f-95f777d4313a`, local constant `0x401a60`) under managed
resource conditions. For the normal device route this may simply fail with
`E_NOINTERFACE`; do not mark every failed probe as an escape. A successfully
exposed native/Ex interface needs the admission proposal's coverage refusal.

These are positive proofs for the listed pointer chains, not every D3DX export.
The DLL's static imports are `msvcrt`, `GDI32`, `KERNEL32` and `ADVAPI32`: it has
no static D3D9 factory or USER32 import, but does perform dynamic lookup. No claim
that it cannot obtain another interface follows from the static import list.

## Real non-resource callback routes

The effect loader registers the game object at `renderer + 0x1c` through
`ID3DXEffect::SetStateManager` (slot 71, offset `0x11c`) after creating the effect.
This is a real graphics callback interface. Its methods can forward state to the
game device. The restricted native replay must not invoke an effect Begin/pass,
commit or state-manager operation merely because it uses a known game effect.
The current fixed motion shaders avoid that route. The effect creation call
passes null macro/include/pool pointers in this path; that does not prove every
other compilation path uses no callbacks.

Bundled D3DX helper `0x621c0e` dynamically obtains
`Direct3DShaderValidatorCreate9` from `d3d9.dll` using
GetModuleHandleA/LoadLibraryA/GetProcAddress. It registers D3DX callback
`0x621b74` through validator slot 3, then submits shader token spans through slot
4. This resolves the apparent `flags=1` calls above. Shader validation/compilation
belongs outside the exclusive replay segment, even though this particular callback
is D3DX-owned. It is not evidence of an additional rendering device.

The X3 effect/environment setup paths also invoke the game function pointer at
`0x608a00` on allocation-error recovery, guarded by `0x6090f0`. Avoid calling game
allocation/recovery helpers inside exclusion; direct native calls and their original
HRESULTs have different callback coverage from such game helper wrappers.

## Validator lifetime and a bounded admission scope

Follow-up on the dynamic validator export, using the same D3DX image above.
The initial factory finding does **not** establish that every normal X3 effect
load invokes it. The analyzed helper `0x621c0e` has one direct code reference:
`0x6241d4`, inside `0x623eea`. Its three direct callers belong to the fragment
linker vtable at `0x408ac4`:

| Public method | Slot / entry | Call to common linker helper |
| --- | --- | --- |
| ID3DXFragmentLinker::LinkShader | 11 / `0x624282` | `0x624366` |
| ID3DXFragmentLinker::LinkVertexShader | 12 / `0x6243e4` | `0x624547` |
| ID3DXFragmentLinker::LinkPixelShader | 13 / `0x62471e` | `0x624880` |

Slot identities match the SDK interface order and the corresponding raw-shader,
vertex-shader and pixel-shader output paths. The common helper skips validation
when its flags contain bit `2`. X3's static D3DX imports include CreateEffect but
no fragment-linker factory. D3DXCreateEffectEx (`0x62c27e`) has a compiled-effect
input branch and a separate effect-compiler branch; the reviewed direct caller
chain does not connect either to this fragment-linker helper. This is not proof
that no indirect/internal path can do so, and the table above is not a complete
shader-compiler call graph.

### Smallest scope covering the validator

The existing relative CALL at D3DX `0x6241d4` targets `0x621c0e`. Its five bytes
are `e8 35 da ff ff`; immediately before it, the caller pushes the token pointer
and loads the diagnostic/linker context into ECX. Assembly establishes the helper
ABI as x86 thiscall: one ECX context, one stack DWORD token pointer, HRESULT in EAX,
callee `ret 4` at `0x621d73`. ESI/EDI/EBX/EBP are preserved. This is a concrete
candidate for a **counted call-site wrapper**, avoiding prologue relocation.
These observed bytes and addresses alone are not a completed hook qualifier.

A ticket beginning immediately before that call and ending only after the helper
returns covers this entire ordinary lifetime:

| Stage | Callsite / result handling |
| --- | --- |
| Locate native export and create | GetProcAddress at `0x621c49`, factory call `0x621c59`; returned interface stays in EDI. |
| Register D3DX diagnostic callback | Slot 3 at `0x621c6f`, callback `0x621b74`, caller's context, final argument zero. Negative result joins cleanup. |
| Submit version/instruction/end tokens | Slot 4 at `0x621c85`, `0x621cf5`, `0x621d32`; each negative result joins cleanup. |
| Finish validation | Slot 5 at `0x621d3e`; negative result joins cleanup. |
| Check callback diagnostic status | Reads the original context's error flag at `+0x90`, and can change a successful result to E_FAIL. |
| Release the obtained reference | Non-null EDI always reaches slot 2 call `0x621d6a` before the ordinary return. |

The helper initializes EDI to null. Missing module/export or a null factory result
returns failure without Release. A non-null result followed by failed callback
registration, any token failure, finalization failure, diagnostic error, or success
all releases exactly the one locally obtained reference. There is no validator
AddRef, object/global/output-field publication, worker dispatch or retained validator
pointer in this D3DX helper. The LinkVertexShader/LinkPixelShader caches retain
created **shader** objects, not this validator.

The registered callback `0x621b74` writes diagnostics through context `+0x58` and
sets `+0x90` for errors. The common linker helper temporarily makes `+0x58` refer
to its local diagnostic list, then consumes that list and clears the field before
returning. This is evidence that D3DX expects callback completion inside the
validation call sequence. It is not a general contract for an arbitrary unknown
implementation of the undocumented factory. No asynchronous D3DX publication was
found in this bounded chain. Native Windows validator internals have not been
inspected or executed.

For comparison only, the previously inspected Preview D3D9 image
(SHA-256 `58cc36cf74128ae4b6211100430d146c3692808146d8d2075e6c5d846162f8cf`)
returns a static validator from RVA `0x3010`. Its slot-3 method at RVA `0x3910`
does not retain the supplied callback/context, and Release at RVA `0x38c0` returns
one rather than destroying that static object. Thus calling this D3DX Release
must be described as relinquishing its obtained reference, not universally proving
final object destruction. This backend observation is not an activation gate or
an assumption to impose on Windows.

### Authority and failure limits

The candidate ticket is ordinary application admission, never permission to do
compilation during exclusive replay. An implementation would need positively
validated D3DX call-site/ABI ownership, safe process-local patch/rollback and
module lifetime, then a narrow factory exemption associated with that exact
active helper invocation and factory callsite (`0x621c59`, return `0x621c5b`).
A factory call merely being nested under another application ticket, originating
somewhere in D3DX, or occurring during CreateEffect is **insufficient authority**.
Unknown direct export callers can still permanently mark interface coverage unknown.
No whole-DLL digest should become a normal runtime prerequisite.

The observed helper has no local SEH cleanup that releases its EDI reference if
an exception unwinds before `0x621d6a`. Ordinary HRESULT failures are covered;
exceptional abandonment is not proven safe by the normal cleanup branch. A
counted wrapper must unwind its own monitor state without swallowing/changing the
application exception, and must retain a permanent coverage refusal if normal
helper completion/reference cleanup is not established. An exception-safe ticket
alone does not certify native callback retirement.

This scope is sufficient to avoid treating the **reviewed synchronous D3DX use**
as an arbitrary published interface merely because the export returns a pointer.
Before exempting it in production, the caller/registration/normal-return contract
and failure controls still need implementation and independent verification.
It does not authorize all dynamic validator callers or remove the other admission
coverage requirements.

## Main window procedure and threading

`0x4dac90` constructs the WNDCLASSA record, assigns `lpfnWndProc = 0x4d3620`
at `0x4dad30`, and calls RegisterClassA at `0x4dadd1`. It creates the window stored
at `0x608ab0` and synchronously proceeds through device initialization
`0x4d8f10`, which supplies the same HWND to IDirect3D9::CreateDevice. This local
call chain has no thread handoff. It proves window/device creation are on the
same calling thread on this path, not that all subsequent render work stays there.

The main procedure `0x4d3620` does not directly call a D3D device or Reset in its
reviewed body. Its helper and indirect paths still matter:

| Message/path | Observed work |
| --- | --- |
| `WM_ACTIVATEAPP` / `WM_ACTIVATE` | Calls `0x4d4950`: input state clearing and DirectInput Acquire/Unacquire paths. Deactivation additionally calls `0x4982b0`, which walks event records and can invoke a game callback from table `0x6085e4 + 0x34 + event * 0x18`. Those callback targets were not exhaustively resolved. |
| `MM_MCINOTIFY` (`0x3b9`) | On matching device IDs stored at `0x608b50` / `0x608b54` and success parameter `1`, calls the function at `*(0x606f40) + 0x10`; indirect call instruction `0x4d374d`. |
| `WM_SYSCOMMAND / SC_CLOSE` | Calls shutdown/script notification helper `0x401d60`; this is not a safe injected operation. |
| Cursor/input paths | Call GetCursorPos, SetCursor and SetCursorPos; separate from the D3D replay operation list. |

Message pump `0x4d34b0` uses PeekMessage/GetMessage/TranslateMessage/DispatchMessage;
callers include main-loop function `0x403840` and several loading/menu helpers.
The launcher/dialog procedure `0x4ce080` installs a timer at `0x4ce4eb` with
**null TIMERPROC** (window message delivery), not an arbitrary timer callback.
No complete rendering-thread identity proof was derived from these local edges.

This supports excluding all message pumping, window calls and mode-changing D3D
operations from the candidate replay segment. It does not establish that a WndProc
is harmless if a native operation unexpectedly reenters it. Microsoft's
[threading discussion](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multithreading-issues)
explicitly identifies CreateDevice, Reset and final device Release as mode-message
hazards; those already belong outside exclusion. Native Windows behavior has not
been tested here.

## Smallest consolidated validation

A future startup-to-game trace should record metadata, not private payloads:

1. Before all seven private-data registration methods dispatch, classify
   `flags & D3DSPD_IUNKNOWN`, capture caller module/RVA, interface kind and tracked
   allocation identity, and latch the process refusal. Keep the original result
   separately; do not clear on failed calls or removal. Authenticate internal mod
   sidecars through owned-object/private-entrypoint provenance.
2. At D3DX helper entry, record whether the supplied device is the already-owned
   application identity. Correlate child CreateTexture/VB/IB outputs before they
   return. Unknown native inputs, successful unsupported QI and late adoption
   require coverage refusal, not a silent assumption of a clean private store.
3. Record the HWND owner thread, CreateDevice thread, first/changed submission
   thread and WndProc entry/exit depth around focus and mode transitions. Include
   D3D calls made while that procedure or its event callbacks are active. Use
   aggregate counts and bounded first-callsite samples; no synchronous per-message
   logging while graphics admission is held.
4. Keep effect state manager, compiler/validator, game recovery, consumers and
   reference retirement outside exclusivity. Confirm the actual injected native
   method inventory against that boundary; absence of registration in a capture
   alone is never permission to drop cold-start coverage.

These findings narrow the routes to validate. They do not justify removing the
live replay gate by themselves.

## Reproduce the targeted inspection

With the existing local Ghidra project and JDK configured:

```sh
analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3CallbackResearch.java /tmp/x3-callback-review.c \
  004d3620 004dac90 004d8f10 004d34b0 004d4950 004982b0 004bae10 004c4100
analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process d3dx9_37.dll -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3CallbackResearch.java /tmp/x3-d3dx-callback-review.c \
  005fea53 005a5be8 005a257b 005a2832 005919b3 005926c5 00621c0e \
  00623eea 00624282 006243e4 0062471e 00621b74 0062c27e
```

If the temporary project has been removed, import/analyze the matching local
images first. Do not commit those projects, disassembly or decompiled output.
