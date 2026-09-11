# Shader-constant upload route

How vertex/pixel shader constants reach `IDirect3DDevice9` during X3 material
draws. Static analysis only (Ghidra 12.1.3, `-readOnly -noanalysis`) of the
installed `X3AP.exe`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, preferred
base `0x00400000`. All addresses are preferred VAs for that image. No game or
device was launched. Decompiler output stayed under the session scratchpad and
is not committed.

Device slot numbering below is the x86 `IDirect3DDevice9` vtable
(`slot * 4` = displacement): 37 `SetRenderTarget` `0x94`, 41 `BeginScene`
`0xa4`, 42 `EndScene` `0xa8`, 43 `Clear` `0xac`, 81 `DrawPrimitive` `0x144`,
82 `DrawIndexedPrimitive` `0x148`, 83 `DrawPrimitiveUP` `0x14c`,
92 `SetVertexShader` `0x170`, 94 `SetVertexShaderConstantF` `0x178`,
96 `SetVertexShaderConstantI` `0x180`, 98 `SetVertexShaderConstantB` `0x188`,
107 `SetPixelShader` `0x1ac`, 109 `SetPixelShaderConstantF` `0x1b4`,
111 `SetPixelShaderConstantI` `0x1bc`, 113 `SetPixelShaderConstantB` `0x1c4`.
`ID3DXEffect` slots: 63 `Begin` `0xfc`, 64 `BeginPass` `0x100`,
65 `CommitChanges` `0x104`, 66 `EndPass` `0x108`, 67 `End` `0x10c`,
71 `SetStateManager` `0x11c`.

X3 never emits `call dword ptr [reg+disp]`; every COM call is
`mov reg,[vtable+disp]` followed by `call reg`. A scan that only matched the
first form finds nothing, which is why the sweep in
[`X3ConstantUploads.java`](../../tools/analysis/X3ConstantUploads.java) matches
the load and then looks forward for the paired `CALL`/`JMP` on the same
register.

## 1. Every constant upload goes through the game's state manager

Whole-image sweep of the six constant-setter displacements. Each row is the
complete set of load-then-call sites in `X3AP.exe`:

| Device method | Call sites | Owning function |
| --- | --- | --- |
| `SetVertexShaderConstantF` `0x178` | `0x004b4bdd` (JMP), `0x004b5362`, `0x004b53a9` | `0x004b4bd0`, `0x004b5340` |
| `SetVertexShaderConstantI` `0x180` | `0x004b4bfd` (JMP) | `0x004b4bf0` |
| `SetVertexShaderConstantB` `0x188` | `0x004b4c1d` (JMP), `0x004b5276` | `0x004b4c10`, `0x004b5240` |
| `SetPixelShaderConstantF` `0x1b4` | `0x004b4c3d` (JMP), `0x004b52c2`, `0x004b5306` | `0x004b4c30`, `0x004b52a0` |
| `SetPixelShaderConstantI` `0x1bc` | `0x004b4c5d` (JMP) | `0x004b4c50` |
| `SetPixelShaderConstantB` `0x1c4` | `0x004b4c7d` (JMP) | `0x004b4c70` |

All twelve owners belong to two `ID3DXEffectStateManager` implementations. **No
game code outside those two classes calls a device constant setter**, and D3DX
is never given the raw device for constants: the effect loader hands it the
state manager.

### The two implementations

`0x004b5550` is the state-manager factory. It reads
`IDirect3DDevice9::GetCreationParameters` (slot 9, call at `0x004b5588`) and
tests `BehaviorFlags & D3DCREATE_PUREDEVICE` (`0x004b5592`: `shr eax,4; and al,1`):

| Branch | Allocation | Constructor | vtable | RTTI name |
| --- | --- | --- | --- | --- |
| pure device | `0x20b0` bytes (`0x004b5599`) | `0x004b4cc0` | `0x00562afc` | `CPureDeviceStateManager` |
| otherwise | `0x10` bytes (`0x004b55db`) | `0x004b4860` | `0x00562a8c` | `CBaseStateManager` |

`0x004b4cc0` chains to `0x004b4860` and then overwrites the vtable pointer at
`0x004b4cee` (`mov dword ptr [ebx],0x562afc`). Both objects hold the device at
`+4` and the last HRESULT at `+0xc`.

Which one is live depends on the driver. `CreateDevice` is called at
`0x004d9642` inside `0x004d8f10` with `BehaviorFlags` taken from a config field
written at `0x004d9517` / `0x004d951f`: if the cached caps dword `[cfg+0x470]`
has `0x10000` (`D3DDEVCAPS_HWTRANSFORMANDLIGHT`) the flags are
`(caps & 0x100000) >> 16 | 0x42`, i.e. `0x42`
(`HARDWARE_VERTEXPROCESSING|FPU_PRESERVE`) plus `0x10` (`PUREDEVICE`) when the
driver reports `D3DDEVCAPS_PUREDEVICE`; otherwise `0x20`
(`SOFTWARE_VERTEXPROCESSING`). Wine/CrossOver's D3D9 normally advertises
`D3DDEVCAPS_PUREDEVICE`, so `CPureDeviceStateManager` is the expected live
class, but this is a runtime decision and both must be assumed possible.

### Registration

`ID3DXEffect::SetStateManager` (slot 71) has exactly one load-then-call site in
the image: `0x004bb076` / `call` at `0x004bb07e`, inside the effect loader
`0x004bae10`. It passes `*(renderer+0x1c)` with the renderer global at
`0x00608b3c`. Both factory call sites store into that field:
`0x004da7a9` (device init `0x004d8f10`) and `0x004dbe30` (`0x004dbd50`,
device-recreate path), each followed by `mov [edi],eax` with
`edi = renderer+0x1c`. So `renderer+0x1c` is the registered state manager and
it is rebuilt when the device is.

### ABI of the forwarding methods

`FUN_004b5340` (`CPureDeviceStateManager::SetVertexShaderConstantF`, vtable slot
14) and `FUN_004b52a0` (slot 18, pixel) are `__stdcall`, `ret 0x10`:

```
this      [esp+4]    state manager
Start     [esp+8]    UINT StartRegister
pData     [esp+0xc]  const float*
Count     [esp+0x10] UINT Vector4fCount
```

Both begin with `cmp Start,0x100`. **`StartRegister >= 256` is forwarded to the
device unconditionally with no caching** (`0x004b5362`, `0x004b52c2`). Below
256 they consult a shadow copy:

- vertex float shadow: `this + 0x1090 + Start*16`, 256 registers
  (`lea ecx,[esi+0x109]; shl ecx,4` at `0x004b5380`)
- pixel float shadow: `this + 0x90 + Start*16`, 256 registers
  (`lea ecx,[esi+9]; shl ecx,4` at `0x004b52e0`)
- bool shadow: `this + 0x50`, 16 dwords (used by `FUN_004b5240`)
- last `SetVertexShader` / `SetPixelShader` argument: `this+0x2098` /
  `this+0x209c` (`FUN_004b50e0`, `FUN_004b5120`) — those two methods skip the
  device call entirely when the pointer is unchanged
- constructor `0x004b4cc0` zeroes `+0x50..+0x90` and `+0x1090..+0x2090`

`FUN_004b47f0` compares and `FUN_004b47b0` copies `Count` 16-byte registers. If
the comparison reports "unchanged" the method returns `S_OK` **without calling
the device**; otherwise it forwards and then updates the shadow.

The `SetVertexShaderConstantI` / `ConstantB` / pixel-integer / pixel-bool
methods installed in the pure-device vtable are the base class' unconditional
tail-jumps (`0x004b4bf0`, `0x004b4c50`, `0x004b4c70`), so **`i0` is forwarded on
every D3DX write**. `CBaseStateManager` forwards everything unconditionally.

Anomaly worth recording: the shadow is compared and updated against
`pConstantData + StartRegister*4` **bytes**, not `pConstantData`
(`lea eax,[edx + esi*0x4]` at `0x004b537c` and `0x004b52dc`), while the device
receives the unshifted pointer. The compare/store pair is self-consistent, so it
still behaves as a change detector, but its key is not the uploaded payload and
for `StartRegister > 0` it reads past the caller's buffer. Consequence for us:
the shadow's skip decisions are independent of actual device state, so a foreign
write to the same registers neither corrupts nor is corrupted by it — but
"the manager skipped the call" must never be read as "the device already holds
these values". Not verified at runtime.

## 2. Per-material-draw shape

Material submission `0x004c0150` (entry ABI in
[object-identity.md](object-identity.md)) drives the effect directly; it makes
**no** device constant, shader or draw calls of its own.

Order along the path, with the outer loop over material subsets closing at
`0x004c4082` (`jl 0x004c0223`, subset count from the word at `descriptor+8`,
stride `0x1a8`):

| Stage | Sites |
| --- | --- |
| one-time parameter binding | `GetParameterByName` (slot 9, `0x24`) ×~60 at `0x004c19fb`–`0x004c1e89`, including a 10-iteration light loop `0x004c1e40`–`0x004c1ea5`; handles cached in the material descriptor. Guards `0x004c0c67` / `0x004c0ded` (`cmp [edi+0x2c],0`) jump straight to `0x004c1eab` once bound, and `0x004c0de5` after a late bind. |
| technique | `SetTechnique` (slot 58, `0xe8`) at `0x004c0c34` |
| begin | `Begin(effect, &passes, 1)` at `0x004c1ead`; the literal `push 1` is `D3DXFX_DONOTSAVESTATE`, so D3DX captures/restores no state block |
| per-draw parameter writes | `SetMatrix` (slot 38, `0x98`) at `0x004c21ff`, `0x004c2229`, `0x004c2270`, `0x004c22e5`, `0x004c2320` (branch-guarded, 1–5 taken); `SetVector` (slot 34, `0x88`) ×10 at `0x004c24df`–`0x004c2e2f`; `SetInt` (slot 26, `0x68`) ×15 at `0x004c2a30`–`0x004c3f77`; `SetFloat` (slot 30, `0x78`) ×10 at `0x004c2b38`–`0x004c3f8e` |
| pass loop | `BeginPass(i)` at `0x004c3ff6`, one draw through a game render object's own vtable slot at `+0x148` (`0x004c403c`, five arguments — not the device), `EndPass` at `0x004c4040`, loop back at `0x004c405b` |
| end | `End` at `0x004c405f` |

**`ID3DXEffect::CommitChanges` is never called by X3.** All nine load-then-call
sites at displacement `0x104` in the image are `IDirect3DDevice9::SetTexture`
(slot 65, same displacement) in the state manager and in the GUI paths. So the
constants a draw needs are uploaded by D3DX inside `BeginPass`, once per pass.

Per pass, D3DX issues one `SetVertexShaderConstantF` per dirty float4 parameter
range declared by that pass's vertex shader. The common material VS declares
five ranges (`camera-and-lights.md`): `g_LightPoint` c0–23,
`g_mWorldViewProjection` c24–27, `g_mWorld` c28–30, `g_mWorldIT` c31–33,
`g_mViewInverse` c34–36, plus `g_nNumLightPoint` in `i0`. That bounds a
single-pass material draw at **≤ 5 `SetVertexShaderConstantF`, ≤ 1
`SetVertexShaderConstantI`, and one `SetPixelShaderConstantF` per dirty PS
parameter** (pixel shaders in the sweep declare nothing above c23), before the
pure-device shadow suppresses any of them. Multi-pass techniques multiply this
by the pass count. Exact counts are a runtime measurement, not a static result;
the numbers above are the static upper bound from the parameter layout.

## 3. No game write at or above c216 / c252

Two independent checks, both negative:

- Static: the only device constant-setter call sites are the twelve listed in
  section 1, all inside the state managers, all driven by D3DX from effect
  parameter bindings. No game code chooses a `StartRegister` itself.
- Declared registers: `verification/results/shader-registers.json` (19 VS, 28 PS
  captured at runtime) tops out at **VS float c45** (`g_Color_HighlightPower`,
  `vs_37c34a7478544c14`), **PS float c23** (`g_Color_Weighting`,
  `ps_496049cec2066ed3`), bool b1, int i0. Nothing declares c216 or above; the
  `>= 256` bypass branch in `FUN_004b5340`/`FUN_004b52a0` therefore has no
  observed caller.

`i0` is the exception: `g_nNumLightPoint` is a real game-written integer
register on the material path, forwarded on every D3DX write.

Limit: this covers the shaders present in the existing capture sweep. A GUI,
particle or bloom effect not yet captured could declare higher registers; the
static result (no game-chosen `StartRegister` anywhere) is the stronger of the
two claims.

## 4. Scene boundaries

Complete load-then-call inventory for the scene/clear slots:

| Method | Sites |
| --- | --- |
| `BeginScene` | `0x004720c2`, `0x00472236` (frame routine `0x00471f50`); `0x004d968f` (device init `0x004d8f10`) |
| `EndScene` | `0x004c5263` (wrapper `0x004c5250`, single caller `0x00472574`), `0x004c6290` (wrapper `0x004c6280`, single caller `0x00472201`), `0x004d96e3` (device init) |
| `Clear` | `0x0047e899` (`0x0047e820`, single caller `0x00472210`), `0x004bb311` (`0x004bb280`), `0x004d96ce` (device init), `0x004db112`, `0x004db1ff` (`0x004dac90`), `0x004db402` (`0x004db3d0`), `0x004db632` (`0x004db520`) |

Frame routine `0x00471f50`: `BeginScene` at `0x004720c2`, then the scene
traversal loop `0x004720e1`–`0x0047215e` (`0x0047c3d0`, `0x004be7d0`,
`0x0046c0f0` — the path that reaches material submission `0x004c4fc0` →
`0x004c0150`), then the original bloom `0x004c4750` at `0x004721b1`, then
`EndScene` at `0x00472574`. **All scene geometry, the effect passes and the
compositor run between that `BeginScene` and `EndScene`.**

A conditional branch (node flag `0x80000` at `0x004721c1` plus a render-option
test) takes a second route at `0x00472201`: `EndScene`, helper `0x004b9660`,
then `0x0047e820` — which issues `Clear(0, NULL, D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,
0, 1.0f, 0)` at `0x0047e899` — and re-enters `BeginScene` at `0x00472236`. That
`Clear` is deliberately outside a scene, which is legal.

The per-view scene clear is `0x004bb280`: it builds `Flags` from the view flags
(`+0x270` bits `0x10`, `0x20|0x4`, `0x800` against render options at
`0x00606f34+0xfc`) and the clear color from camera bytes `+0x2ac/+0x2b0/+0x2b4`,
`Z=1.0`, `Stencil=0`. Its single caller `0x0048a190` walks a camera list and is
itself reached through the generic dispatcher `0x004e3e70` (main loop
`0x00403840` and others), so its static position relative to `BeginScene` is not
resolvable from the call graph alone.

Consequence for the proxy's deferred fill: the schedule-at-`Clear`,
execute-at-next-draw design is safe **by construction rather than by clear
ordering**. D3D9 rejects `DrawPrimitive`/`DrawIndexedPrimitive` outside a scene,
so if the game draw the hook is riding on succeeds, the injected fill is inside
`BeginScene` too. The only residual case is a latched `Clear` with no subsequent
draw before `EndScene`/`Present` (the `0x0047e820` route above can produce a
clear that is not immediately followed by a draw in the same scene); the proxy
must drop or flush such a latch rather than carry it across the scene boundary.

## Implication for `src/proxy/motion_output.cpp`

The setter hooks (`set_vertex_shader`, `set_pixel_shader`,
`set_vertex_constants_f`, `set_vertex_constants_i`, `set_pixel_constants_f`,
`set_stream_source`, `set_indices`, `set_vertex_declaration`) sit on the
**per-draw hot path**, but a shallow one:

- Frequency per material draw (single-pass, before the pure-device shadow
  suppresses anything): ≤ 5 `SetVertexShaderConstantF`, ≤ 1
  `SetVertexShaderConstantI`, 1 `SetVertexShader`, 1 `SetPixelShader`, plus the
  pixel-constant writes. The upper bound is the parameter count of the pass's
  shader, not a per-register call storm: `g_LightPoint` arrives as a single
  24-register write, `g_mWorldViewProjection` as a single 4-register write.
- Each hook body is a bounds test plus a ≤ 64-byte `memcpy`, so per-draw
  shadowing cost is a small constant. `set_vertex_declaration` is the expensive
  one — it calls `GetDeclaration` — and it fires once per declaration change,
  not per draw.
- The `start > 4096 / count > 4096` guards are cheap but never exercised by this
  game: D3DX drives every write from CTAB ranges bounded by c45.

Reserved ranges:

- **VS c252–255 and PS c216–217 are never written by the game.** No game code
  picks a `StartRegister`, and no captured shader declares a register above VS
  c45 / PS c23. `shadow_.vs_reserved_written` / `ps_reserved_written` will
  therefore normally stay false after the initial `GetVertexShaderConstantF`
  probe, and `undo()` correctly skips the restore.
- These registers are below 256, so they fall inside the pure-device manager's
  shadow window (VS `this+0x1090+252*16`, PS `this+0x90+216*16`). That costs
  nothing here: D3DX never targets those registers, so the shadow slots are
  never consulted, and the shadow's compare key is the caller's buffer rather
  than device state (section 1), so our writes cannot flip a skip decision.
- `i0` is different: it is genuinely game-written (`g_nNumLightPoint`) and
  forwarded unconditionally, so `set_vertex_constants_i` sees it on every
  material draw. The proxy only reads it; nothing reserves it.

The one hard requirement the state manager imposes is the shader restore.
`CPureDeviceStateManager` memoizes the last `SetVertexShader`/`SetPixelShader`
argument at `+0x2098`/`+0x209c` and skips the device call when unchanged. The
routed draw replaces both shaders, so `undo()` restoring exactly
`shadow_.vs` / `shadow_.ps` is not defensive tidiness — without it the manager's
shadow is stale and the next effect pass silently renders with our variant
shader. The existing reverse-order `undo()` already satisfies this. The same
reasoning applies to `SetRenderTarget(1, …)` and `D3DRS_COLORWRITEENABLE1`:
`CPureDeviceStateManager::SetRenderState` (`0x004b4f80`) only filters through
`FUN_004b5620` and does not memoize, and `Begin` uses `D3DXFX_DONOTSAVESTATE`,
so D3DX will not restore those for us either.

## Reproduce

```sh
JAVA_HOME='/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home' \
  '/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless' \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -readOnly -noanalysis \
  -scriptPath tools/analysis -postScript X3ConstantUploads.java \
  /tmp/x3-uploads.txt tally disp:0x178 disp:0x180 disp:0x188 disp:0x1b4 \
  disp:0x1bc disp:0x1c4 disp:0xa4 disp:0xa8 disp:0xac disp:0x144 disp:0x148

JAVA_HOME=... analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3ConstantUploads.java /tmp/x3-sm.txt \
  vtable:0x00562afc:21 xref:0x00562afc xref:0x004b4cc0 \
  dec:0x004b5340 dec:0x004b52a0 dec:0x004b50e0 dec:0x004b5120 dec:0x004b47f0

JAVA_HOME=... analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3ConstantUploads.java /tmp/x3-effect.txt owner:0x004c0150 \
  disp:0x68 disp:0x78 disp:0x88 disp:0x98 disp:0xfc disp:0x100 disp:0x104 \
  disp:0x108 disp:0x10c
```

`owner:<addr>` restricts site listing to a containing function; `disp:` sweeps a
vtable displacement; `tally` prints per-displacement counts; `vtable:`, `xref:`
and `dec:` dump pointer slots, references and decompilation. Generated output is
game-derived and must stay untracked.

The remaining open item is a runtime count: instrument the proxy's existing
per-draw counters to record actual `SetVertexShaderConstantF` call counts and
`StartRegister` histograms for one flight capture, and confirm the ≤ 5 / ≤ 1
bound and the absence of any `StartRegister >= 216` above.
