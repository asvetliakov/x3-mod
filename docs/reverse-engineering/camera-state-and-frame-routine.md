# Live camera state and the frame routine

Static analysis only (Ghidra 12.1.3, `-readOnly -noanalysis`) of the installed
`X3AP.exe`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, preferred
base `0x00400000`. All addresses are preferred VAs for that image. No game or
device was launched; no Wine process was started. Decompiler output stayed under
`/tmp/x3-camera/` and is not committed. The original script is
[`tools/analysis/X3CameraState.java`](../../tools/analysis/X3CameraState.java).

Device slot numbering is the x86 `IDirect3DDevice9` vtable (`slot * 4` =
displacement), as tabulated in [constant-uploads.md](constant-uploads.md).
Additional slots used below: 38 `GetRenderTarget` `0x98`, 47 `SetViewport`
`0xbc`, 57 `SetRenderState` `0xe4`, 67 `SetTextureStageState` `0x10c`,
81 `DrawPrimitive` `0x144`, 82 `DrawIndexedPrimitive` `0x148`,
83 `DrawPrimitiveUP` `0x14c`.

## 1. The four matrix globals

Each of `0x00608a38/3c/40/44/48` is a **pointer to a 64-byte, 16-float matrix
buffer**, not a matrix. The buffers are heap-allocated once during renderer
init and freed at renderer shutdown; only two functions ever write the pointers:

| Global | Content | Allocated at | Freed at |
| --- | --- | --- | --- |
| `0x00608a38` | current **projection** | `0x004b9a60` (`0x004b9770`) | `0x004bb1d3` (`0x004bb140`) |
| `0x00608a3c` | alternate projection, used only by the node-flag-`0x200` branch of `0x004c0150` | `0x004b9770` (same init block) | `0x004bb1f7` |
| `0x00608a40` | current **view** (world→view) | `0x004b9a08` | `0x004bb1af` |
| `0x00608a44` | current **world** | `0x004b9958` | `0x004bb167` |
| `0x00608a48` | current **world basis** (source of `g_mWorldIT`) | `0x004b99b0` | `0x004bb18b` |

Init defaults (`0x004b9b0a`–`0x004b9b5c`): view ← `0x005786a8` (identity with
element 14 = `-7.0`), world and basis ← `0x005786e8` (identity), projection ←
the result of `0x004be3f0`. Constant `0x005654e0` = `0x37800000` = 2^-16 =
1/65536 is the engine's fixed-point conversion scale.

There is no fifth "camera node" matrix. `g_mViewInverse` (VS c34–c36) is not
stored anywhere: `0x004c0150` computes it per draw with
`D3DXMatrixInverse(scratch, NULL, *0x00608a40)` at `0x004c2316`.

### Storage convention

Row-major, **row-vector** (`v' = v · M`), left-handed — the D3D default:

- `0x004bfd40` calls `D3DXMatrixMultiply(out, localVP, *0x00608a38)` at
  `0x004bffdb`, i.e. `view-ish * projection`, which is only correct row-vector.
- `0x004c0150` builds WVP as `Multiply(t, *0x00608a44, *0x00608a40)` then
  `Multiply(t, t, projection)` (`0x004c21ce`, `0x004c21f2`).
- The view builder writes the translation into elements 12/13/14 and 1.0 into 15.
- `0x004c5ea0` uses `D3DXMatrixLookAtLH` (`0x004faee2`, IAT `0x0053231c`).

Float element indices used below are `matrix[i]`, byte offset `4*i`:
`m00=0, m11=5, m20=8, m21=9, m22=10, m23=11, m32=14, m33=15`.

This is the transpose of the register-row representation used in
[camera-numerics.md](camera-numerics.md); both descriptions agree numerically.

## 2. View matrix construction — `0x004be520`

`FUN_004be520(context, camera)`, `__cdecl`, caller cleans 8 bytes.
`context` is the owning spatial/render context (the same object reached through
render-node `+0x1c` in [object-identity.md](object-identity.md)); `camera` is a
`0x790`-byte camera node (allocator `0x00488c70`).

| Camera field | Type | Use |
| --- | --- | --- |
| `+0x30/+0x34/+0x38` | signed int | camera position (the **base** translation triple, not the render-ready `+0xb0` triple that render nodes use) |
| `+0x40/+0x44/+0x48` | signed int | basis row 0, fixed point / 65536 |
| `+0x50/+0x54/+0x58` | signed int | basis row 1 |
| `+0x60/+0x64/+0x68` | signed int | basis row 2 |
| `+0x270` | uint flags | `0x800000` per-view near/far regime; `0x400000` nebula-star regime; `0x80000` environment-map marker; `0x4000` particles; `0x40000`, `0x1000`, `0x8`, `0x1` other phase bits |
| `+0x278` | ptr | `D3DVIEWPORT9` passed to `SetViewport` (slot 47, call at `0x004be646`); also gates the per-view `Clear` |
| `+0x298` | int | field of view, binary angle (65536 = 360°) |
| `+0x29c` | int | **view layer index**; the frame routine inserts bloom before the first view with `+0x29c > 0x11` |
| `+0x300/+0x304` | int 16.16 | view-plane width / height; fall back to `*0x00606f38 + 0x28 / +0x2c` when either is ≤ 0 |
| `+0x360` | uint | far distance in context units |
| `+0x77c`, `+0x78c`, `+0x37c` | — | secondary-pass and env-map bookkeeping |

Construction (`0x004be557`–`0x004be66x`), with `s = *(float*)(context+0x2c)`:

```
V[0]=b00/65536  V[4]=b01/65536  V[8]=b02/65536       (camera basis, transposed)
V[1]=b10/65536  V[5]=b11/65536  V[9]=b12/65536
V[2]=b20/65536  V[6]=b21/65536  V[10]=b22/65536
V[12] = -( p.x*V[0] + p.y*V[4] + p.z*V[8]  ) * s
V[13] = -( p.x*V[1] + p.y*V[5] + p.z*V[9]  ) * s
V[14] = -( p.x*V[2] + p.y*V[6] + p.z*V[10] ) * s
V[15] = 1.0        (elements 3,7,11 come from the identity template 0x0054e8e0)
```

The 3×3 block is the transpose of the camera basis — the inverse only if the
basis is orthonormal; `camera-numerics.md` measured `Cᵀ·C - I` up to 3.62e-5, so
treat it as approximately orthonormal and keep using a full inverse when
inverting. **Translation units are the same context-scaled units as the world
matrices built by `0x004bdee0`** (which multiplies by `*(camera+0x1c)+0x2c`, the
same `+0x2c` field). The ~10,000× translation groups in `camera-numerics.md` are
therefore different **views** with different context scale, not different draws
inside one view.

`0x004be520` also caches `*0x005786a0 = *(camera+0x298)` (current FOV) and calls
`SetViewport(*(camera+0x278))` at `0x004be646`. The other `SetViewport` sites are
`0x004c6351` (`0x004c6300`, env-map) and `0x004d96b3` (device init).

## 3. Projection construction — `0x004be460` / `0x004be3f0`

`FUN_004be460` is `__fastcall`: **ECX = camera, EAX = destination float[16]**.
It starts from the all-zero template `0x0054e920` and writes five elements.
`FUN_004be3f0(fovRadiansScaled, W, H)` (EAX = destination) is the init-time
variant with the same body.

```
W = (cam[0x300] > 0 && cam[0x304] > 0) ? cam[0x300]/65536 : (*0x00606f38)[0x28]/65536
H = (same guard)                       ? cam[0x304]/65536 : (*0x00606f38)[0x2c]/65536
t = (cam[0x298]/65536) * 6.2831855 * 0.5          // = binaryAngle * pi  = half-FOV in radians
P[0]  = cot(t) / W          // m00
P[5]  = cot(t) / H          // m11
P[10] = 1.0000030           // m22, from 0x005655cc
P[11] = 1.0                 // m23  -> w_clip = z_view, left-handed
P[14] = -6.0000184          // m32, from 0x005655c8
```

Constants: `0x005654f8 = 6.2831855` (2π), `0x00565508 = 0.5`,
`0x005655cc = 0x3f800019 = 1.0000030`, `0x005655c8 = 0xc0c00026 = -6.0000184`.

The two defaults are exactly the standard D3D LH pair
`m22 = zf/(zf-zn)`, `m32 = -zn*zf/(zf-zn)` for **zn = 6, zf = 2,000,000**
(`2e6/(2e6-6) = 1.0000030`, `-6 * 1.0000030 = -6.0000180`). That resolves the
ill-conditioned far-plane recovery in `camera-numerics.md`: the far plane is the
clamp constant `0x00565570 = 2000000.0`, and near is `0x00565768 = 6.0`.

The FOV field is a binary angle with 65536 = 360°: the thresholds `0xccc` and
`0x2147` in `0x004c4fc0` are 18.000° and 46.80°, and 18° being exact supports
this reading.

**Uncertainty.** The captured projection is `m00 = 0.8`, `m11 = 1.333333`
(`camera-numerics.md`), so `W/H = 1280/768` and `cot(t) = 0.8·W`. Two
parameterizations fit and static analysis cannot separate them:

- (A) `cam[0x298] = 0x4000` (90°), `cot(t) = 1`, `W = 1.25`, `H = 0.75`
  (i.e. pixels/1024). Round FOV value; most likely.
- (B) `cam[0x298] = 13424` (73.7398°), `cot(t) = 4/3`, `W = 5/3`, `H = 1`.

Either way the **effective** vertical FOV is `2·atan(1/m11) = 73.7398°` and the
effective horizontal FOV is `2·atan(1/m00) = 102.68°`. One live read of
`cam+0x298`, `+0x300`, `+0x304` settles it.

## 4. The projection is rewritten per submission

`0x004c4fc0` (the material-submission wrapper that calls `0x004c0150`) rewrites
`P[10]` and `P[14]` **before every material draw**, from the *view's* flags:

| Branch | Condition | `P[10]` / `P[14]` |
| --- | --- | --- |
| per-view near/far | `view[0x270] & 0x800000` | `zf = max(view[0x360] * contextScale, 2000000.0)`; `zn = 6.0 + (fov < 0x2147 ? 100.0*(1 - 4*fov/65536) : 0.0)`; `m22 = zf/(zf-zn)`, `m32 = -m22*zn` |
| nebula stars | `!0x800000 && (view[0x270] & 0x400000)` and the node's model id equals the cached id of `environments\nebulae\uranus\nebula_uranus_stars_01` | `1.0000001` / `-0.35000005` (`0x00565764` / `0x00565760`) |
| default | otherwise | `1.0000030` / `-6.0000184` |

`0x004bfd40` (`0x004bfe8a`, `0x004bffba`) and the `flags & 0x200` branch of
`0x004bdee0` (`0x004be3ce`) also reset `P[10]`/`P[14]` to the defaults.

Consequence: `*0x00608a38` read at an arbitrary draw is **not** the frame's
canonical projection — only `m00`, `m11`, `m20`, `m21`, `m23` are stable across a
view; `m22`/`m32` are per-submission scratch. Read the canonical depth range from
the constants above (or recompute with `0x004be460`'s formula), not from a
sampled draw. This is the same hazard `object-identity.md` flagged as "can be
adjusted per submission", now quantified.

## 5. Where the effect state manager reads the matrices — `0x004c0150`

All five `ID3DXEffect::SetMatrix` (slot 38, `0x98`) calls in the material path,
with the matrix each one uploads. Handles are cached in the material descriptor
(`EDI`); `EBX` is the effect.

| SetMatrix site | Handle | Matrix source | Shader parameter |
| --- | --- | --- | --- |
| `0x004c220f` | `[EDI+0x198]` | `Multiply(t, *0x00608a44, *0x00608a40)` at `0x004c21ce`, then `Multiply(t, t, *0x00608a38 or *0x00608a3c)` at `0x004c21f2` | `g_mWorldViewProjection` (c24–27) |
| `0x004c2230` | `[EDI+0x40]` | `*0x00608a44` **directly, no copy** | `g_mWorld` (c28–30) |
| `0x004c2280` | `[EDI+0x44]` | `D3DXMatrixInverse(s, NULL, *0x00608a48)` `0x004c2251`, then `D3DXMatrixTranspose` `0x004c2266` | `g_mWorldIT` (c31–33) |
| `0x004c22f5` | `[EDI+0x48]` | same WVP product, branch-selected by node `[+0x130] & 0x200` (`0x004c2293`) between `*0x00608a38` and `*0x00608a3c` | second WVP-class parameter |
| `0x004c2330` | `[EDI+0x50]` | `D3DXMatrixInverse(s, NULL, *0x00608a40)` `0x004c2316` | `g_mViewInverse` (c34–36) |

D3DX then uploads the dirty ranges inside `BeginPass` (`0x004c3ff6`) through the
game's state manager, exactly as `constant-uploads.md` describes. **The proxy can
read the identical source by reading the five globals — no new hook is needed for
the values themselves, only for the timing.**

D3DX helper thunks: `0x004faf00` `D3DXMatrixMultiply` (IAT `0x0053236c`),
`0x004faf0c` `D3DXMatrixInverse` (`0x00532364`), `0x004faf1e`
`D3DXMatrixTranspose` (`0x00532358`), `0x004faee2` `D3DXMatrixLookAtLH`
(`0x0053231c`).

## 6. When the values are final — `0x0047c840`

`FUN_0047c840` is the **view activation** routine. It takes two *register*
arguments: **EAX = view/camera**, **EDI = context** (EDI is forwarded untouched
to both `0x004be520` and `0x0047c640`). It saves and restores ECX/EBX/ESI:

```
0x0047c84d  CALL 0x004be520      ; build view + projection into the globals, SetViewport
0x0047c860  CALL 0x004bb280      ; per-view Clear, only if view[0x278] != 0
0x0047c8ad  CALL 0x0047c640      ; remaining view state (EDI, ESI)
```

It is called from three sites, all in the frame routine: `0x00472260`
(main per-view render), `0x004723c8` (the `view[0x77c] != 0` second pass) and
`0x00472461` (the `*0x00608518+0x64` view). `FUN_0047e820` uses the sibling
builder `0x004be670` instead (see §7).

**The camera state is per view, not per frame.** A frame activates several views
(background/sky, main scene, overlay layers), each with its own context scale and
its own FOV. The globals are final from `0x0047c852` until the next activation,
except for the `m22`/`m32` scratch of §4.

The cheapest observable timing signal that needs no code patch: the per-view
`Clear` issued by `0x004bb280` immediately after the build. The proxy already
recognizes that Clear (`scene_boundary.h`'s latching Clear); reading the globals
in that `Clear` hook yields the view matrix and the stable projection terms for
every draw that follows, until the next Clear or the next frame.

## 7. Frame routine `0x00471f50`

Confirmed and corrected against the summary in
[assessment-2026-09-12.md](../architecture/assessment-2026-09-12.md).

```
0x00471f6c  FUN_004f4fc0                      frame prologue
0x00472044  FUN_004714c0
0x00472066  FUN_0047b680 (loop)               per-sector update
0x004720c8  device BeginScene   (slot 41)     <-- SCENE BEGIN
0x004720ed  FUN_0047c3d0 (loop)               view update; builds the view array
0x00472141  FUN_004be7d0 / 0x00472155 FUN_0046c0f0
            _qsort(views, n, 4, cmp=0x004715a0)
            ---- per-view loop, 0x004721a0 .. 0x00472383 ----
0x004721a8  CMP [ESI+0x29c],0x11
0x004721b1  CALL 0x004c4750                   <-- BLOOM, once, before the first
                                                  view with layer > 0x11
0x004721c1  AND EAX,0x80000                   env-map marker view?
0x00472201  CALL 0x004c6280  (EndScene 0xa8)  <-- SECOND-SCENE BEGIN
0x00472206  CALL 0x004b9660
0x00472210  CALL 0x0047e820                   6-face environment map (see below)
0x0047223d  device BeginScene   (slot 41)     <-- SECOND-SCENE END
0x0047224d  FUN_004892a0 / 0x00472256 FUN_0047bc20
0x00472260  CALL 0x0047c840                   <-- CAMERA/VIEWPORT/CLEAR for this view
0x0047226b  FUN_0047e780
0x00472295/0x004722a8 FUN_0047e920, 0x004722af FUN_0047e620,
0x004722b5  FUN_0047e6e0                      deferred material draws
0x00472307  FUN_004bf4c0                      particles, if view[0x270] & 0x4000
0x00472358  FUN_00489bf0 / 0x00472370 FUN_004715d0
            ---- second pass over views with view[0x77c] != 0 ----
0x004723c8  FUN_0047c840   0x004723d5 FUN_004c53d0
            ---- the *0x00608518+0x64 view ----
0x00472442  FUN_00471660 ... 0x00472461 FUN_0047c840 ... 0x004724a7 FUN_004c53d0
0x0047253f  CALL 0x004c5830                   on-screen text
0x00472574  CALL 0x004c5250  (EndScene 0xa8)  <-- FRAME SCENE END
0x00472585  FUN_00473e10   (render option bit 1)
0x004725c4  FUN_00476140   (render option bit 2)
```

### The bloom gate is *not* the glow video option

`0x004721b1` is reached whenever any view has `+0x29c > 0x11`; there is **no**
render-option test at the callsite. The option checks live inside `0x004c4750`
(`ghidra-render-map.md`). So hooking the callsite gives a scene/overlay boundary
that survives glow being disabled — which is precisely the failure mode recorded
in the assessment ("TAA's insertion point is a game video option").
Residual case: if no view has layer > `0x11`, the call never happens and the only
boundary left is `0x00472574`.

### The "second scene" is the environment map, not the cockpit

`FUN_0047e820` (`in_EAX` = view, `unaff_ESI` = context) is gated on
`(*0x00606f34 + 0x100) & 0x100 == 0`, copies `view[0x78c]` fields `+0x30..+0x3c`
into the view, then:

```
FUN_004c6020()                                  begin render-to-env-map
for face in 0..5:
    (*0x00608a5c)->Face(face, 1)                slot 0x24 = ID3DXRenderToEnvMap::Face
    device Clear(0, NULL, TARGET|ZBUFFER, 0, 1.0f, 0)     slot 43, 0x0047e899
    FUN_004be670(context, view)                 view/projection for this face;
                                                calls 0x004be460 and GetRenderTarget (slot 38)
    FUN_0047c640 / FUN_004892a0 / FUN_0047bc20 / FUN_0047e780
    FUN_0047e920 / FUN_0047e620 / FUN_0047e6e0  full material traversal, per face
FUN_004c60a0()                                  end render-to-env-map
```

`*0x00608a5c` is the `ID3DXRenderToEnvMap` created at `0x004c4145`
(`D3DXCreateRenderToEnvMap`). Six full scene traversals into cube faces run
between the `EndScene` at `0x00472201` and the `BeginScene` at `0x0047223d`.
Every draw in that span must be excluded from TAA history and from motion output:
it uses a different view, a different target and a different projection.

`FUN_004be670` differs from `0x004be520`: it sets world *and* view to identity,
then replaces the view with `D3DXMatrixLookAtLH`-derived values from
`0x004c5ea0` using the camera position scaled by `context+0x2c`, and post-processes
the 16 floats through `0x004c5dc0`. It does **not** call `SetViewport`.

## 8. Hook shapes

All three are five-byte `CALL rel32` callsites, the same shape as the already
proven `object_trace` patch at `0x004c5228`, so the existing install/rollback
machinery (SHA-256 + size gate, PE/base check, expected-byte check, single
relative-call redirect, no prologue relocation) applies unchanged.

| Boundary | Recommended site | Bytes | Contract | Patch required? |
| --- | --- | --- | --- | --- |
| **View activation / camera state** | `0x0047c84d` `CALL 0x004be520` | `E8 rel32`, returns to `0x0047c852` | `__cdecl`, args already pushed (`[ESP]` = context, `[ESP+4]` = camera); caller cleans 8 at `0x0047c852`. Result in EAX is consumed. Must preserve EBX/ESI/EBP/EDI — `0x0047c855` reads `[ESI+0x278]` and ESI must survive. Free: EAX (after forwarding), ECX, EDX, flags | **No, if the per-view Clear is used instead.** `0x004bb280` runs right after and the proxy already sees that Clear. Patch only when `view[0x278] == 0` coverage is needed |
| **Scene end / compositing begin** | `0x004721b1` `CALL 0x004c4750` | `E8 rel32`, returns to `0x004721b6` | No arguments, no stack cleanup; `0x004c4750` is `void(void)`. ESI holds the current view across the call (`0x004721bb` reads `[ESI+0x270]`), so ESI/EDI/EBX/EBP must be preserved | **Yes.** This is the only way to get a glow-independent boundary. The current StretchRect selector is a fallback, not a substitute |
| **Environment-map pass** | `0x00472210` `CALL 0x0047e820` | `E8 rel32`, returns to `0x00472215` | `in_EAX` = view, `unaff_ESI` = context — **both are register arguments**, so a trampoline must forward EAX and ESI unchanged and preserve ESI/EDI/EBX/EBP. Returns 0/1 in EAX, tested at `0x00472215` | **No, for detection.** A mid-frame `EndScene` followed by `Clear(TARGET|ZBUFFER, Z=1.0)` and a `BeginScene`, with six cube-face target changes in between, is unique and observable at the D3D level. Patch only to bracket the pass cheaply |
| Frame scene end (fallback) | `0x00472574` `CALL 0x004c5250` | `E8 rel32` | `void(void)`, returns 0/1; nothing reads it | No — the last `EndScene` before `Present` is already observable |

### Implemented hook: scene end / compositing begin (2026-09-12)

`src/proxy/scene_hook.cpp` (`X3M_SCENE_HOOK`; on by default with
`X3M_MOTION_OUTPUT=1` since review 26, `tools/manage.py launch --scene-hook
off` disables it) patches the second row of the table. Exact
contract as built and fixture-verified
([motion-output.md](../verification/motion-output.md#engine-scene-end-hook-x3m_scene_hook)):

- Site `0x004721b1`, five bytes, expected exactly `E8 9A 25 05 00`
  (`CALL 0x004c4750`: rel32 `0x0005259a = 0x004c4750 − 0x004721b6`), read
  with `ReadProcessMemory` after the object-trace identity gate (SHA-256 and
  size of X3AP.exe) passed. Any other bytes, any other executable: no write,
  status `callsite_mismatch` / `executable_mismatch`.
- Patch: only the rel32 changes, to `trampoline − 0x004721b6`; the `E8`
  opcode and the return address `0x004721b6` stay. `VirtualProtect`
  (execute-read-write), write, `FlushInstructionCache`, protection restored;
  a failure after the write rolls the bytes back (`patch_rolled_back`).
- Trampoline (naked): `pushfl; pushal; call _x3m_scene_end_signal; popal;
  popfl; jmp *_x3m_scene_end_original`. EAX–EDI and the flags reach
  `0x004c4750` as the frame routine left them (ESI = current view survives,
  `0x004721bb` reads `[ESI+0x270]` after the return); the original's `ret`
  returns to `0x004721b6`. No arguments, no stack cleanup (`void(void)`),
  nothing relocated.
- Signal: cdecl, `force_align_arg_pointer` (the game's stack is 4-byte
  aligned at the call), wraps the listener in the full CPU boundary of
  `cpu_state.h` (FNSAVE/FRSTOR of the x87 state, MXCSR, the thread's last
  error), the same transport as the heavy device hooks: the listener's path
  (resolve, telemetry, log formatter) executes x87 code — int64-to-double
  conversions and the CRT's float formatting — so the light MXCSR-only
  contract does not apply (review 21). Once per frame. The listener
  (`capture.cpp::scene_end_signal`) runs under the capture mutex on the
  render thread and may run the full temporal resolve (heavy device work) —
  the boundary is light on state, not on time.
- Shutdown: the original five bytes are written back (same protect/flush
  sequence) when the last device is released, refused if the site no longer
  holds our bytes or the originals (`shutdown_not_owned`); a device created
  later reinstalls. Nothing on disk changes.
- Use: the route treats the signal as the scene end (routing/jitter stop,
  cut verdict, and with `X3M_TAA=1` the resolve on the bound RT0 before the
  compositor runs), so the glow option no longer decides whether the resolve
  runs; the `StretchRect` selector becomes the fallback and the two are
  cross-checked per frame (`scene_end_source`, `scene_end_check`).
  Unverified in gameplay as of this note; the hook is exercised on the
  motion-output fixture's own `E8` callsite through the seam DLL.

Reentrancy: `0x0047c840` runs 3× per frame minimum (more with multiple views) and
`0x004721b1` at most once; neither is recursive and both are on the render thread
under the frame routine. `0x0047e820` re-enters the same traversal helpers six
times, so any hook that scopes "inside env map" must use a counter, not a flag.

### Injecting a jittered projection

In the row-vector layout of §1, an NDC jitter `(jx, jy)` is
`P[8] += jx`, `P[9] += jy` (byte offsets `0x20`, `0x24`), because
`w_clip = z_view` (`P[11] = 1`). This matches the hypothesis in
`camera-numerics.md` transposed into memory order.

Those two elements are **zero in the template `0x0054e920` and are never written
by any other function**: the complete writer set for `*0x00608a38` is
`0x004be520`, `0x004be670`, `0x004be3f0`/`0x004be460` (whole buffer),
`0x004c4fc0`, `0x004bfd40`, `0x004bdee0` (elements 10 and 14 only). So writing
`P[8]`/`P[9]` after view activation survives every per-submission fixup and
reaches every draw whose WVP is built by `0x004c0150`, plus the particle and trail
paths that consume the same global. Re-apply after **each** `0x004be520`
(and `0x004be670` if env-map jitter is wanted — normally it is not).

Readers to consider before writing: the complete reader set for `*0x00608a38` is
`0x004bdee0`, `0x004be520`, `0x004be670`, `0x004bf4c0`, `0x004bfd40`,
`0x004c0150`, `0x004c4fc0` — **all rendering**. No culling, picking, GUI or
gameplay code references the global. The fixed-function overlay paths
(`0x004c53d0`, `0x004c55d0`, `0x004c5830`) do not read it at all, so HUD and text
stay unjittered automatically. This is a static claim about references to that
address; a separate frustum/pick structure elsewhere in the engine is not excluded.

Compared with the current per-draw constant rewrite, one write of eight bytes per
view activation replaces a per-draw WVP patch, and it jitters exactly the draws
that consume the engine camera.

## 9. Draw callsites

Complete inventory of device draw-method dispatch in the image.

| Site | Method | Owning routine | Role |
| --- | --- | --- | --- |
| `0x004bf81a` | `DrawPrimitive` `0x144` | `0x004bf4c0` | particles / stardust; binds `g_mView` c0–3 and `g_mProj` c4–7. Called at `0x00472307` when `view[0x270] & 0x4000` |
| `0x004c008a` | `DrawPrimitive` `0x144` | `0x004bfd40` | dynamic locked-buffer strips (`t_DiffuseTexture`, `g_mViewProjection`); trails/beams. Stride `0x18`, `D3DPT_TRIANGLELIST`, `count/3` primitives |
| `0x004c4e54` | `DrawPrimitive` `0x144` | `0x004c4750` | bloom / glow composite quads, triangle strip, 2 primitives |
| `0x004c55b4` | `DrawPrimitiveUP` `0x14c` | `0x004c53d0` | fixed-function 2D overlay; long `SetRenderState` (slot 57) / `SetTextureStageState` (slot 67) preamble. Called at `0x004723d5` and `0x004724a7` |
| `0x004c57ff` | `DrawPrimitiveUP` `0x14c` | `0x004c55d0` | second overlay path; sole caller `0x0049774c` (outside the frame routine) |
| `0x004c5d46`, `0x004c5d8b` | `DrawPrimitiveUP` `0x14c` | `0x004c5830` | on-screen text; called at `0x0047253f` with the frame routine's string |
| `0x004c403c` | vtable slot `0x148`, **5 pushes** | `0x004c0150` | main material draw, inside the `BeginPass`/`EndPass` loop; reached from `0x004c5228` (already hooked by `object_trace`) |

### `DrawIndexedPrimitive` has no engine callsite

A whole-image sweep found **zero** `IDirect3DDevice9::DrawIndexedPrimitive`
(displacement `0x148`) load-then-call sites, and exactly one instruction anywhere
that forms that displacement as an immediate: `0x004c4018 ADD EDI,0x148` inside
`0x004c0150`, where `EDI = *[ESP+0xcc]` (an object's vtable). The call at
`0x004c403c` pushes five dwords — `(obj, 4, 0, 0, primitiveCount)` — while
`DrawIndexedPrimitive` is a seven-push `__stdcall`
`(this, Type, BaseVertexIndex, MinVertexIndex, NumVertices, StartIndex, PrimCount)`.
The argument count therefore rules out the device method at that site.

Conclusion: the engine's indexed scene geometry does not call the device's draw
method directly. The immediate caller the proxy sees for those draws is either
`d3dx9_37.dll` or a further wrapper reached through slot `0x148` of that
unidentified class. **Return-address bucketing cannot label the main material
pass.** Two consequences:

- Use the existing thread-local scope at `0x004c5228` (`object_trace`) to label
  material draws; it is already installed and needs no return-address heuristics.
- Add the §8 boundary hooks to label phase (background / scene / env-map /
  overlay); the remaining six engine draw callsites above are `DrawPrimitive` /
  `DrawPrimitiveUP` and *are* return-address identifiable, because those three
  displacements do have direct load-then-call sites in the EXE.

**Open, one live measurement.** Log `_ReturnAddress()` in the proxy's
`DrawIndexedPrimitive` for one frame and bucket by module: inside
`0x00400000`–`0x00600000` means the sweep missed a register-indexed dispatch
form; inside `d3dx9_37.dll` confirms the conclusion above. An EBP-chain walk is
not a reliable substitute — `d3dx9_37` frames are not guaranteed to be
frame-pointer based, while `0x004c0150` does establish EBP.

## Reproduce

```sh
JAVA_HOME='/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home' \
  '/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless' \
  /tmp/x3-ghidra-research X3Render -process X3AP.exe -readOnly -noanalysis \
  -scriptPath tools/analysis -postScript X3CameraState.java \
  /tmp/x3-camera/out.txt data:00608a38 data:00608a40 data:00608a44 data:00608a48 \
  dec:004be460 dec:004be520 dec:004be670 dec:004c4fc0 dec:00471f50 dec:0047e820 \
  ins:00471f50 range:004c21c0:130 range:004c3fd0:60 \
  ptr:005655c8:2 ptr:0054e920:16 disp:0x144 disp:0x14c load:0x148 txt:,0x148
```

Specs: `dec:` decompile, `ins:` full instruction listing, `range:ADDR:N` N
instructions from ADDR, `data:` classified references to a data address,
`sym:` symbol-name substring plus its references, `disp:` the
load-then-`CALL`/`JMP` vtable sweep, `load:` every operand ending in a
displacement, `txt:` every instruction whose text contains a string,
`ptr:ADDR:N` raw dwords. Generated output is game-derived and stays untracked.

## Uncertainty summary

- Nothing here was observed at runtime. Every offset, constant and control-flow
  claim is static.
- The FOV/view-plane parameterization in §3 has two consistent readings;
  the resulting projection is identical either way.
- `*0x00608a3c` (the alternate projection) has only one recorded reference
  besides the `0x004c0150` branch selector and the shutdown free; what fills it,
  and what the node flag `0x200` means, is not established.
- The class behind vtable slot `0x148` at `0x004c403c` is unidentified.
- `0x004c4fc0`'s three-way depth regime was read from the decompiler's rendering
  of a short-circuit condition; the branch *addresses* (`0x004c512b`,
  `0x004c51d7`, `0x004c51e7`, `0x004c51fb`) are from the instruction listing, but
  the exact predicate order deserves an assembly re-read before code depends on it.
- The `+0x29c > 0x11` bloom threshold and the `0x80000` env-map marker are single
  static observations; neither has been correlated with a capture.
- No claim is made that jittering `P[8]`/`P[9]` is visually correct; it is the
  algebraically consistent injection point for the documented layout.

## Ambient occlusion inputs (2026-09-14)

Static answers to `../architecture/ambient-occlusion.md` §8, with two cross-checks
from existing captures. No launch, no Wine; raw Ghidra output stays in
`/tmp/x3-ao-re/` (`X3CameraState.java`, specs `dec:00424e00 dec:004bdee0
dec:00488c70 dec:00479d10 dec:00493b40 ins:004c0150 ins:004c4fc0 txt:0x360]`).

**View-unit scale: 1 view unit = 0.2 m (1 m = 5 units).** (1) The HUD target
readout `FUN_00424e00` formats the distance helper result (`0x0042f850`, called at
`0x00424e7b..0x00424e87`) as `d/500` with a metre suffix, `(d/500)*0.001` as "K"
(double `0x00565500`) and `*1e-6` as "M" (`0x00565758`) — **500 native units = 1
m**. (2) The view translation is `-p·B * s` (§2) and the world translation is
`node[+0xb0/b4/b8] * s` (`0x004be38b..0x004be3a9`, `s = *(float*)(context+0x2c)`),
while the 3×3 carries only `basis/65536` × the per-node scale
(`0x004be000..0x004be069`) — **no `s`** — so object-space `POSITION0` is already in
view units and view distance = native · s. (3) `s = 0.01`: the "Cockpit Scene"
constant `0x00565640 = 0x3c23d70a`, and every gameplay camera in the run-28/36
`object_fade` rows reports `scale_bits=0x3c23d708`; only the background/nebula view
(`+0x270 & 0x400000`) uses ~1e-5. So near 6 = 1.2 m, far 2·10⁶ = 400 km, and the
captured sector fog pair `+0x36c/+0x370` of 25e6/30e6 and 50e6/55e6 native reads as
50/60 km and 100/110 km. An AO radius of `R` metres is `5·R` view units.

**`view[0x270] & 0x800000` is the ordinary gameplay regime.** Re-read of
`0x004c5093..0x004c5145`: `zn = 6.0 + (fov < 0x2147 ? 100·(1 − 4·fov/65536) : 0)`
(`100.0` at `0x00565734`, `4.0` at `0x005656b0`), `zf = max((unsigned)cam[0x360]·s,
2e6)`, then `m22 = zf/(zf−zn)`, `m32 = −m22·zn`; the `fov < 0xccc` / `400.0`
(`0x0056576c`) arm at `0x004c50d8` is **dead**, reached only when `fov ≥ 0x2147`.
*Writers*: nothing ORs `0x800000` into a `+0x270` displacement — the flags dword is
assigned wholesale by the script/graph command dispatcher `0x00493b40` case `0x3b`
(store `0x00494f13`; `0x3a` reads it back) and loaded from the scene stream by
`0x00479d10` (`_Dst[0x9c]`), while `0x00489bf0` only ORs bit `0x1` and
`0x004891e0`/`0x0042157c`/`0x004215a0`/`0x00494232` only touch `0x10000` beside the
fog pair. It is authored scene state: the run-28/36 rows show the main sector camera
at `flags270=0x0085492d` in ordinary flight and on the station approach, not a
menu/cutscene branch. *Value*: `cam+0x360` has no writer — allocator `0x00488c70`
memsets `0x790`, loader `0x00479d10` writes `_Dst[0xda..0xdd]` (`+0x368..+0x374`)
but skips `_Dst[0xd8]`, and the only non-`ESP` `0x360` displacements in the image
are the reads at `0x004c50f4`/`0x004c50fa`. With `+0x360 = 0` and the allocator's
default `+0x298 = 0x4000` (90°, settling §3 in favour of reading (A)) the regime
yields exactly `zf = 2e6`, `zn = 6` — the default pair, as every captured projection
shows (`camera-numerics.md`). Cockpit zoom (`INS_CockpitSetZooming`, FOV to
`0x106`) does cross `0x2147` and steps `zn` from 6 to ~54 and on to ~104 units
(20.9 m), so the AO pass must derive `zn`/`zf` per frame from the view (`+0x270`,
`+0x298`, `+0x360`, `s`) instead of hard-coding them; `m22`/`m32` in `*0x00608a38`
stay per-submission scratch.

**`LightDir_Dir0` is world space and per draw.** Handle cache in `0x004c0150`
(`0x004c1a3e..0x004c1a9d`, each store following the *next* push): `+0x54`
`LightDir_Dir0`, `+0x58` `LightDir_Color0`, `+0x5c` `LightDir_Dir1`, `+0x60`
`LightDir_Color1`, `+0x64` `g_LightAmbientIntensity` (`+0x50` is `g_mViewInverse`).
Write site `0x004c234d..0x004c245e`: the vector is `light[+0xb0/b4/b8] −
node[+0xb0/b4/b8]`, both raw render-domain ints with the *submitted* node (param 2,
`[EBP+0xc]`) as origin, normalized through `FSQRT` and the double `2^-16` at
`0x00565510`, rounded by `0x0052b5d0` to ×65536 fixed point, reloaded ×`1/65536`
and set as a float4 with `w = 0` via effect vtable `+0x88`. So: world space,
object→light, recomputed **per submitted node** — never view space, never per frame
— zeroed when the light is absent (`0x004c24bb`, `0x004c2674`). The two lights are
chosen per submission batch by `0x004c4fc0` (`0x004c5030..0x004c508f`): walk
`*0x00608518 + 0x628c` (stride 12, count `+0x6288`, initialised to 8 at
`0x004b9bbb`) into the object array `+0x5e8c`, admit on node `+0x12c & 0x800000` or
`+0x158 > 0x256250`, pass first/second as params 5/6. The sun is therefore readable
from globals without a hook: world position `+0xb0/b4/b8`, colour floats at
`light[+0x16c] + 0x04/0x08/0x0c`, record maintained by `0x004bdda0`.

**`g_LightAmbientIntensity` has no writer and no consumer.** Its handle is cached at
descriptor `+0x64` (`0x004c1a9d`) and never read — no `[EDI+0x64]` load exists in
`0x004c0150`, the only resolver (the string `0x005630d8`'s other reference,
`0x004ba652`, is the effect-name validator `FUN_004ba500`). No shader declares it:
zero hits across the 751-program archive sweep
(`verification/results/shader-sweep-inventory.json`) and the 47-program CTAB set
(`shader-registers.json`), where `LightDir_Dir0` appears in 17 of 47. AO v2 cannot
weight by a true ambient share. Nor is there a per-sector ambient colour: the
material path's colour inputs are the two directional lights and the point array
(`g_LightPoint`/`g_nNumLightPoint`), and D0/D1 come from the light object's node
words `+0x150/+0x152/+0x154` × 1/256 (`material-color-inputs.md`), so the ambient
estimate must still be derived from D1 as the design assumes.

**Cockpit/HUD marker: per view, not per draw.** No per-draw flag was found. The
cockpit HUD scene camera is created with `+0x270 |= 0x24` at `0x004202a7`
(`FUN_00420260`); the sector camera also carries `0x10000` (fog: `0x004891e0`,
`0x0042157c`, `0x004215a0`) and `0x8000` (`0x0042d571`). Captured values separate
them — sector `0x0085492d` versus cockpit-scene `0x25`, `0x2025`, `0x1` — so a hook
can test `+0x270 & 0x810000` for the sector view. Crosshair and target indicator
attach to `cockpit+4`, the cockpit HUD scene (`chase-target-indicator.md`), a
separate view activation rather than a late draw inside the sector view; whether any
Z-test-off draw still precedes the sector scene end stays the open run-14 question.

*Uncertainty*: all static except the `object_fade` and projection cross-checks, from
the run-28/run-36 captures on installed `3f06979`. "No writer for `cam+0x360`" is a
displacement sweep — an aliased-base write is not excluded, and one Clear-hook read
would close it. Bits of `0x0085492d` beyond `0x800000/0x40000/0x10000/0x4000` are
unattributed.

### Round 2 — the three `ambient-occlusion.md` §8 unknowns, closed (2026-09-14)

Second pass on the same EXE (`fdbf3418…`), Ghidra read-only on
`/tmp/x3-ghidra-research X3Render` with `X3CameraState.java`; raw output in
`/tmp/x3-ao-re2/out1..out6.txt` (untracked). Specs: `data:005630d8 data:00563104
ins:004c0150 ins:004c4fc0 dec:004bdda0 dec:004bdbf0 dec:004bdd20 dec:004bdea0
dec:00479d10 dec:00488c70 load:0x360 load:0x270 load:0x16c txt:0x790 disp:0xcc
range:004c5093:80 range:004c51fb:44`. Cross-checks are `grep`/stream reads of the
run-39 and run-40 session logs (`/tmp/x3-bottleX3-run39|40`) and of
`verification/results/shader-sweep-inventory.json` (751 programs) and
`shader-registers.json` (47). No Wine command, no launch.

**1. `g_LightAmbientIntensity`: no register, no upload, no value — confirmed from
three directions.** The handle is cached at descriptor `+0x64` (`PUSH 0x5630d8` at
`0x004c1a86`, store `0x004c1a9d`) and the whole `0x004c0150` body
(`004c0150-004c40fb`) contains exactly two non-`ESP` `+0x64` loads, neither of them
the descriptor: `0x004c0b0f` reads `*0x00608518 + 0x64` and `0x004c0bb7` is a vtable
slot immediately called. There is therefore no `SetPixelShaderConstantF` /
`SetVertexShaderConstantF` for it and nothing to grep in the capture. No program
declares it either: 0 hits for any parameter name containing `Ambient` across the
751-program archive sweep (731 carry a CTAB) and across `shader-registers.json` and
`game-docking-shader-registers.json`, while `LightDir_Dir0` is declared by 507 of the
751. Finally the engine's only ambient *field* is dead: the per-node light record is
`malloc(0x6c)` + `memset 0` at `0x004bdd20` (a D3DLIGHT9 plus a dirty dword at
`+0x68`), and its filler `0x004bdbf0` writes Type, Diffuse (`rec[1..3]` from node
`+0x150/+0x152/+0x154` × the double `1/256` at `0x00565568`, alpha 1.0), Specular (1,1,1,1), Range,
falloff and attenuation, and explicitly stores **`rec[9..0xc] = 0`**, i.e.
`D3DLIGHT9.Ambient = (0,0,0,0)`. AO v2 has no true ambient share to weight by; the
D1-derived estimate stands.

**2. `LightDir_Dir0` is world space — now also proven from the capture.** The static
read of `0x004c234d..0x004c245e` reproduced round 1 exactly (vector = light
`[+0xb0/b4/b8]` − submitted node `[EBP+0xc][+0xb0/b4/b8]`, normalized, quantized
through `0x0052b5d0` and `1/65536`, `w = 0`, no matrix anywhere). Run-39 evidence:
for each capture frame, the per-draw `ps` constant was read at the register each
program's own CTAB declares (`c0` for 3, `c4` for 7, `c5` for 6 of the 47-program
set); 180 (node, model, program) triples are common to frames 1812, 2071 and 2316 and
**every one has bit-identical float bits in all three frames**, while the camera basis
rotates 5.514° (1812→2071), 1.316° (2071→2316), 6.294° overall and translates
(68, 345, 98) units. A view-space vector cannot do that. Stronger: fitting the 615
frame-1812 (direction, `object_position`) pairs to a single world point converges on
`(−4.708e8, +7.144e8, −1.3151e9)` native with **median residual 0.0003°, max
0.0007°**, i.e. `dir = normalize(L_world − node_world)` to within the `1/65536`
quantization; `|L| ≈ 1.57e9` native ≈ 3.14e6 m ≈ 3100 km. The scene-wide spread is
0.6° (`c4.x` from −0.310425 to −0.300079), so AO v2 may use one world direction per
frame — but it must rotate it into view space itself. The register is *not* fixed:
over the 751-program sweep the declarations sit at c4 (153), c1 (128), c22 (64),
c5 (58), c0 (38), c19 (32), c7 (14), c21 (6), c18 (6), c39 (5), c13 (3); a consumer
must read the CTAB. Light-record side: `0x004bdda0` writes `rec[0x34..0x3c]` = node
`+0xb0/b4/b8` × `s` (world position in view units) and, for Type 3, `rec[0x40..0x48]`
= −node position × `1/65536` — again no view transform. `0x004bdbf0` picks Type 2
(spot) on node `+0x12c & 0x10`, Type 1 (point) on `& 0x400000`, else Type 3 and
**sets `node+0x12c |= 0x800000`**, which is exactly the admission test of the
two-light selector at `0x004c5061`: that flag means "directional light".

**3. Per-view near/far: the AO pass does not need the regime.** Byte-level re-read of
`0x004c5093..0x004c5145` settles the round-1 formula from the opcodes (`dc c9` =
`FMUL ST(1),ST(0)` then `de e1` = `FSUBRP`, so the near term is `100·(1 − 4·fov/65536)`,
not `99·4·fov/65536`): `zn = 6.0 + (fov < 0x2147 ? 100·(1 − 4·fov/65536) : 0)`,
`zf = max(unsigned(view[+0x360])·s, 2e6)` (`0x00565570` = 2e6, `0x0056554c` = 2³² for
the sign fixup, `0x00565768` = 6.0), `m22 → (*0x00608a38)[+0x28]` (`0x004c513e`),
`m32 → [+0x38]` (`0x004c51fd`) — indices 10 and 14 of the row-major 4×4. *Writers of
`+0x360`*: the full sweep of operands ending in `0x360]` returns 18 instructions, of
which only `0x004c50f4`/`0x004c50fa` use a non-`ESP` base, both reads. Aliasing
narrowed by the object size: `0x790` appears at four code sites only — `0x00479d5a`
(loader), `0x00487df1` (`FUN_00487be0`), `0x00488c73` (allocator, `memset 0`),
`0x00488f37` (`FUN_00488de0`), the last two in the same `push size / push fill /
push dst` memset shape, and there is no 484-dword `rep movsd` clone (the only
`MOV ECX,0x1e4` sites, `0x00424ee8` and `0x0044a8db`, are offsets). The scene-stream
loader `0x00479d10` writes `_Dst[0xc0..0xd6]` and `_Dst[0xda..0xdd]` and skips
`0xd7/0xd8/0xd9`, so `+0x360` is never authored; the allocator memsets `0x790` and
defaults `_Dst[0xa6] = 0x4000` (+0x298, 90°). *Writers of the `+0x270` bit*: 83 sites
end in `0x270]`; the only bit-setting writers are `OR 0x200` (`0x0041f511`), `OR 0x24`
(`0x004202a7`, cockpit scene), `OR/AND 0x10000` (`0x0042157c`/`0x004215a0`,
`0x004891fe`/`0x0048920b`, `0x00494232` — fog), `OR 0x8000` (`0x0042d571`) and
`OR/AND 0x1` (`0x00477744`, `0x00489fdf`, `0x00489c1f`, `0x00489d34`, `0x0048a024`).
**Nothing ORs `0x800000`**; it can only arrive through the six wholesale dword stores
(`0x0041f91d`, `0x00420009`/`0x004201f5`, `0x00431a24`, `0x0046433a`, `0x004772ac`,
`0x0047a3f9` = loader `_Dst[0x9c]`, and `0x00494f13` = script command `0x3b`) — the
bit is authored scene state. *Capture cross-check, two further runs*: run-39 and
run-40 `object_fade` rows show one camera per frame at `flags270=0x0085492d` with
`near36c/far370` = 25e6/30e6 native (both runs) or 50e6/55e6 (run 39, second session
camera) and `scale_bits=0x3c23d708/0a` (0.01), beside the background camera
`0x00400135` at ~1e-5 and cockpit-scene cameras `0x00000025`, `0x00002025`,
`0x00000001`; so gameplay takes the `0x800000` arm and nothing authored looks like a
far plane except the fog pair. *Measured constants*: in every run-39 capture frame
`vs_36f98d151fd6b0c6` uploads `g_mProj` at c4..c7 as `(0.79999995,0,0,0)`,
`(0,1.33333325,0,0)`, `(0,0,1.00000298,−6.00001812)`, `(0,0,1,0)` — **m22 =
1.00000298, m32 = −6.00001812**, exactly `zn = 6`, `zf = 2·10⁶`; `vs_5e484a06672e28fb`'s
`g_mViewProjection` rows c2/c3 differ by 6.000 to 6.003 in the w term in all five
capture frames (1812, 2071, 2316, 9163, 11940) — the same `zn` within the float
cancellation of a ~10³ translation. Consequence: linearize as **`z_view = m32/(d − m22)`** — it reproduces
`zn` at `d = 0` and `zf` at `d = 1` from the two floats the route already latches
(`camera_reprojection.h:44` validates all 16 projection floats; m22 = `projection[10]`,
m32 = `projection[14]`), it needs neither `+0x270` nor `+0x360` nor `+0x298`, and it
follows the cockpit-zoom FOV path for free. Do **not** recover `zf` from `m22`:
`m22 − 1 = 2.98e−6` in float32, so `zf` carries about ±4 %.

**Hook-site notes (no hook added).** `0x004c5093 MOV EAX,[EDI+0x270]` is six bytes
(`8b 87 70 02 00 00`), is not a branch target anywhere in the `FUN_004c4fc0` listing,
and both EAX and EFLAGS are dead-in (EAX is redefined by the MOV itself, EFLAGS by
`TEST EAX,0x800000` at `0x004c5099`); no x87 instruction executes between the entry
`0x004c4fc0` and the site (its one CALL, `0x004c5013`, returns an integer compared at
`0x004c501b`), EDI holds the view object and ESI is `this`. A 5-byte relative JMP fits
inside the instruction with one pad byte and splits nothing. Caveats: the function
installs an SEH frame in its first three instructions (`PUSH -1`, `PUSH 0x530668`,
`FS:[0]` chain) and runs once per view submission — sector, background and cockpit
scene in the same frame — so a detour must be reentrant and must not assume one call
per frame. The per-draw sun upload `0x004c245e CALL ECX` is two bytes and its
neighbour `0x004c2460 FLD float ptr [0x005654e0]` six, so a 5-byte patch there splits
instructions and 8 bytes must be relocated (both position-independent); the x87 stack
is empty at that point (drained by the FSTPs at `0x004c2440/47/4e` and `0x004c2457`).
Neither hook is needed for AO: the projection floats are already latched at the Clear
and the sun is a global read (`*0x00608518 + 0x628c` → `+0x5e8c` → `light[+0x16c]`).

*Uncertainty*: the capture cross-checks come from the run-39/40 session logs of those
runs' installed build; everything else is static on `fdbf3418…`. "No writer for
`+0x360`" is now a displacement sweep plus a size-constant sweep, still not proof
against an aliased base. The fitted sun world position is a least-squares inference
from one frame, not a read of the light object. Whether `FUN_00487be0` and
`FUN_00488de0` clear a camera object (not merely a `0x790`-sized one) was not
established, and `+0x270` bits `0x4000/0x40000/0x100/0x1000000` remain unattributed.
