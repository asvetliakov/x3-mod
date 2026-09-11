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
  005fea53 005a5be8 005a257b 005a2832 005919b3 005926c5 00621c0e
```

If the temporary project has been removed, import/analyze the matching local
images first. Do not commit those projects, disassembly or decompiled output.
