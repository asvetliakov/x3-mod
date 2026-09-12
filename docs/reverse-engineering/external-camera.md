# External camera: the cockpit object, its cameras, the view pose and the aim ray

Static analysis only (Ghidra 12.1.3, `-readOnly -noanalysis`, project
`/tmp/x3-ghidra-research`) of the installed `X3AP.exe`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, preferred
base `0x00400000`, 2026-09-13. Bytes were cross-checked against the file with
`verification/probe/verify_chase_camera_site.py`. No game or Wine process was
started. Decompiler output stayed under `/tmp/x3-camera-study/` and is not
committed. Scripts: `tools/analysis/X3CameraState.java` (`dec:`, `ins:`,
`data:`, `load:`, `range:`, `sym:` specs) plus a short Python pass over the
`.rdata` string tables; the spec lists are at the end.

Builds on [camera-state-and-frame-routine.md](camera-state-and-frame-routine.md)
(view/projection construction, the frame routine) and
[object-identity.md](object-identity.md) (node layout). Everything below is a
static claim; the first `X3M_CAMERA=chase` run is the first runtime check
([chase-camera.md](../architecture/chase-camera.md), "First user run").

## 1. Two engine command tables name the camera code

`.rdata` holds two arrays of C-string pointers registered as command-name
tables of the script/engine command dispatchers:

| Table | Entries | Registered by | Dispatcher |
| --- | ---: | --- | --- |
| `0x0057aef0` `INS_CockpitAlloc` … `INS_SetSidebarInstance` | 113 | `0x0041c8f0` (record stride `0x18` in the registry at `*0x006085e4`) | `0x0042d340`, `switch(param_3)` over the command index |
| `0x0057a420` `B3D_InstAlloc` … `J3D_JobPlayAsyncS` | 168 | `0x00469e80` | its own dispatcher (not needed here) |

The `INS_Cockpit*` command index equals the case label of `0x0042d340`, which
made the cockpit object's layout readable directly from the dispatcher (§2).
There is no parallel function-pointer table; the dispatcher's cases inline the
field accesses or call the worker functions below.

## 2. The cockpit object (`0x810` bytes)

`INS_CockpitAlloc` (case 0): `FUN_004e6a40(0x810)`, constructor `0x0041f8d0`,
then `0x00420260` creates the "Cockpit Scene" (`0x00489070`, name
`FUN_004ee250("Cockpit Scene")`, scale `*(scene+0x2c)` = the float constant at
`0x00565640`) and its layer-0 camera (`0x00488c70`, `+0x270 |= 0x24`, `+0x29c =
0`, position 0), then sixteen `0x004885a0` nodes at `+0x14..`. The current
cockpit is found by handle through `0x0041cd20` (registry `*0x00608504`).

| Cockpit offset | Field | Evidence |
| --- | --- | --- |
| `+0x00` | handle | case 0 stores the `0x004efcc0` result |
| `+0x04` | cockpit scene (context; `+0x2c` float scale) | `0x00420260` |
| `+0x08` | cockpit-scene camera (layer 0; HUD/cockpit-body scene, position 0, basis = `+0xf0` × a per-frame matrix at `0x00420787`) | `0x00420260`, `INS_CockpitGetCamera` (case 2 returns `*(+8)+0x28`), `0x0040e880` returns it for the mouse-steering dead zone (`0x0040e8c0`) |
| `+0x0c` | **ref object** (`INS_CockpitSetRefObject`, case 0xc) — the object the camera follows | `0x004205e0` reads `+0xc` → `+0x70` (its render node) |
| `+0x10` | ref view object (the player ship for the cockpit-body/zoom logic; type test `*(short*)(+0x54 … +0x48)`) | cases 0x28/0x2c, `0x004205e0` |
| `+0x58` | **sector camera** = the scene camera of the external/internal view (`INS_CockpitSetSectorCamera`, case 5) | FOV, fog (`+0x368..+0x370`), flag `0x10000`, the overlay and the aim ray all use it (§4, §5) |
| `+0x5c`, `+0x60` | galaxy camera (case 6), dust camera (case 7); `+0x5c` receives a copy of `+0x58`'s pose at `0x00421533`.., `+0x60` follows through `0x0041efc0` | `0x004205e0` tail |
| `+0x90/+0x94/+0x98` | view angles alpha/beta/gamma (`INS_CockpitGetViewAlpha/Beta/Gamma`, cases 0x3a–0x3c; binary angles) | `0x00422ca0` copies the targets `+0xa8/+0xac/+0xb0` into them |
| `+0xa8/+0xac/+0xb0` | target view angles (`INS_CockpitChangeView`, case 0x2b) | |
| **`+0xf0`** (12 ints) | **view-relative basis** `R_view`: the camera orientation relative to the ship, built from the view angles each frame (`0x00420a27`) and consumed by the aim ray (§5) | `camera(+0x58).basis = R_view × B_ship` at `0x00420c02` |
| `+0x120` | lock-view target (`INS_CockpitLockView`, case 0x2e) | `0x00422f40` |
| `+0x128` | no-decay flag (case 0x62) | |
| `+0x130/+0x134/+0x138` | **view position** in the ship frame (`INS_CockpitSetViewPos`, case 0x2f): the boom offset of the external views | `0x00420b27`: `EDI = (+0x130) × node(+0xc0)` then `+ node(+0xb0)` |
| `+0x140..` | view point position (case 0x5b) | |
| **`+0x150`** | **view mode** (`INS_CockpitSetViewMode`, case 0x30). `1` = internal cockpit view: `0x004205e0` sets flag `8` on the ship's render node `+0x134` (hidden) when `+0x150 == 1`, clears it otherwise (`0x004216c0..`); every other value is an external view whose geometry the scripts define through `+0x130`/`+0x90..` | `CMP [EBX+0x150],1` sites at `0x004207bf`, `0x00421169`, `0x004216b0` |
| `+0x160/+0x164/+0x168` | view camera offset (case 0x36): an extra offset applied after the pose (`0x00420c3d..0x00420e03`), with a nearest-object search when the ship type has flag `0x8000` | |
| `+0x1b8`, `+0x1c4`, `+0x1c8` | view transition duration (`INS_CockpitSetViewDuration`, case 0x52) and its start times in game ms | `0x004218b0` interpolates `+0x130`/`+0x90` over `+0x1b8` ms |
| **`+0x1c0`** | **view connect mode** (`INS_CockpitSetViewConnectMode`, case 0x2c → `0x00422cd0`): `0` follow, `3` locked basis (`camera.basis = +0xf0` verbatim), `4/5/6/8/9` scripted/cinematic (random start angles, fixed positions), `6` also disables the `+0x160` offset | `0x00420b95..0x00420c0c` |
| `+0x1fc` | current sector object (`GetCurrentObjectIDInSector` through the script VM, set after the pose) | `0x00421024` |
| `+0x200`, `+0x208` | update timers (game ms) | `0x004205e0` head |
| `+0x238..+0x248` | zoom state (`INS_CockpitSetZooming`, case 0x1f): FOV = `0x4000 × 0x10000 / zoom`, min `0x106`; timed on the **real** clock `+0x720` | `0x00421280..` |
| `+0x2ac..+0x2b4` | camera shake angles (`B3D_CameraShake` writes them; `0x004205e0` composes them into the cockpit-scene camera) | `0x00420751` |
| `+0x3ac` | target-overlay state (`0x0042a2d0` updates it; `INS_CockpitGetCursorAim` / `GetObjectByTargetOverlayIconPos` search it by screen position) | `0x00425410` |

The FOV write resolves the parameterization left open in
camera-state-and-frame-routine.md §3: the sector camera's `+0x298` is
`(*0x00608504+0x24) × 0x10000 / zoom`, the cockpit-scene camera's is
`0x4000 × 0x10000 / zoom`; with zoom `0x10000` that is the round `0x4000`
(90° binary angle), reading (A).

## 3. The per-frame cockpit update `0x004205e0` builds the view pose

Called once per cockpit from `0x0041cde0` (the registry walk), which the main
loop `0x00403840` calls at `0x00403f2f` **immediately before** the render frame
routine `0x00471f50` at `0x00403f34`, after the game-object update
`0x00416750` (`0x00403f2a`). So the order inside one loop iteration is: input
and object update (including the player ship's fire control, §5) → cockpit
update (camera pose) → frame routine (view activation `0x0047c840` →
`0x004be520` builds the view matrix from the camera's `+0x30/+0x40`, then the
scene, HUD overlays and text). The pose the frame renders is the one this
function leaves in the sector camera.

Order of business (`FUN_004205e0(cockpit)`; EBX = cockpit throughout):

1. `0x004218b0(cockpit)`: view transitions — interpolates `+0x130` and `+0x90..`
   toward their targets over `+0x1b8` ms of **game time** (`*0x00606f34+0x718`),
   per connect mode (`switch(+0x1c0)`).
2. `0x00426360`: cockpit-body model management (internal view only).
3. Shake/angle matrices: `0x004f0270(alpha, beta, gamma)` builds a rotation
   from three binary angles (16.16 output, `ESI` = destination);
   `0x004f17f0(EAX=A, ECX=B, [ESP]=dest)` is `dest = A × B` (row-vector, 16.16);
   `0x004f0da0(ECX=M, ESI=v, EDI=out)` is `out = v × M` (vector by matrix).
4. `0x00420767`: `[ESP+0x54] = R(view angles) × [ESP+0xcc]`; `0x00420787`:
   cockpit-scene camera basis `= (+0xf0) × [ESP+0xc4]` (uses the previous
   frame's `+0xf0`); `0x0042079e`: its position `= (+0x2ac) × basis`.
5. `if (+0xc != 0)` — the follow branch, ordinary external view (no cockpit
   body, `+0x150 != 1`):
   - `0x00420a27`: **`+0xf0 = [ESP+0x54] × [ESP+0x90]`** (the view-relative
     basis `R_view`).
   - position: connect mode `5/6` or flag `+0x1a0 & 4` → `camera.pos = +0x130`
     verbatim; otherwise `0x00420b27..0x00420b67`: `camera(+0x58).pos =
     node(+0xb0) + (+0x130) × node(+0xc0)` where `node = (+0xc)->+0x70` is the
     ref object's render node (`0x0044fe20(ref) != 0` = docked/carried:
     `0x00450520` supplies the parent transform instead).
   - basis: connect mode `3` (unless the ref view object differs from the ref
     object), `5`, `6` or flag `+0x1a0 & 4` → `camera.basis = +0xf0` verbatim
     (`0x00420c0c`); otherwise `0x00420bfc..0x00420c02`:
     **`camera(+0x58).basis = (+0xf0) × node(+0xc0)`** (or `× parent basis` when
     docked).
   - `0x00420c3d..0x00420e03`: the `+0x160` view camera offset (skipped for
     connect modes 4/5/6/8/9): rotated and added to `camera.pos`.
6. **`0x00420e06`**: `cmp [ebx+0x54],0; jz 0x00421019` — the pose of the sector
   camera is final here. The object loop that follows (`0x00420e10..0x00421013`)
   writes every sector object's node `+0xf0/+0xf4/+0xf8` = `(node.pos − cam.pos)
   · cam.basis rows` (camera-relative coordinates for sound/radar/overlay).
7. `0x004216e0`, sector lookup into `+0x1fc`, `0x004237e0`, `0x00423120`,
   `0x0042a2d0(+0x3ac)` (target overlay, reads `cockpit+0x58`), `0x0042c5d0`,
   `0x0042c200(+0x6f8)`, `0x004271e0`, `0x00427790(+0x79c)`, `0x00426d90`,
   `0x0042d140(+0x7e4)`; zoom/FOV writes to `+8`, `+0x58`, `+0x5c`, `+0x60`
   (`0x00421390..0x004213d7`); `0x00422fc0` (laser-low notification); the
   motion-blur/shake vectors of the sector camera (`+0x100`, `+0x350`, from
   `+0x310` = previous position); fog from the sector type table; then the
   `+0x5c` copy of the `+0x58` pose and `0x0041efc0(+0x60, +0x58)`.

Nothing after step 6 writes the sector camera's `+0x30..+0x3c` or
`+0x40..+0x6f` again (checked: `0x004216e0` writes node `+0x30` of cockpit
display nodes from `+0xf0` of *other* nodes; `0x00422fc0` is the laser
notification; `0x0041efc0` reads `+0x58` to drive `+0x60`).

### Hook site

`0x00420e06`: `83 7b 54 00` `cmp dword ptr [ebx+0x54],0`; `0x00420e0a`:
`0f 84 09 02 00 00` `jz 0x00421019`. Ten bytes, two whole instructions, one
relative branch (rel32 at offset 6, re-based by the tail copy). The three
branches that reach this address (`0x004207a7`, `0x00420c5c`, `0x00420c6b`..)
all target `0x00420e06` itself; a byte scan of the function finds no branch
into `0x00420e07..0x00420e0f` (`verify_chase_camera_site.py`,
`interior_branches`). The function's prologue is `55 8b ec 83 e4 f0` (frame
pointer, `and esp,-16`), so the site is mid-function with EBX = cockpit live.
The five-byte jump straddles the qword boundary at `0x00420e08` (plain copy;
the install window covers it — the site is claimed before the device exists).

## 4. The scene, the HUD overlay and the HUD cameras read the same camera

- Render: the frame routine activates the views in the context's list;
  `0x004be520` builds `*0x00608a40` from **camera `+0x30` (position) and
  `+0x40/+0x50/+0x60` (basis)** — the fields the cockpit update writes into the
  sector camera. The view-plane/FOV fields `+0x298`, `+0x300/+0x304` drive the
  projection (`0x004be460`).
- Target overlay (brackets, reticle icons): `0x0042a2d0(cockpit+0x3ac)` runs in
  step 7, after the pose, reads `*(cockpit+0x324 back-pointer)+0x58` (the sector
  camera) and projects through the scene helper `0x00489e90`.
  `INS_CockpitGetCursorAim` (`0x00425410`) then selects the object whose overlay
  icon is under the cursor (`0x004299a0(cockpit+0x3ac, x, y)`), so the icon
  positions and the picked object both derive from the sector camera's pose of
  the same frame.
- Screen projection for scripts: `INS_CockpitProjectPosition` (case 0x66) →
  `0x004899f0(cockpit+0x58, object, out)` → `0x00489780` with the sector camera.
- The galaxy/dust cameras (`+0x5c`, `+0x60`) copy the sector camera's pose in
  step 7, i.e. after the hook site.
- The cockpit-scene camera (`+8`, layer 0) is built in step 4 from `+0xf0` of
  the **previous** frame (the fresh `+0xf0` is written in step 5). With the
  chase camera writing `+0xf0` at the hook site, that camera follows the
  smoothed orientation one frame late — the same one-frame structure vanilla has
  during view transitions; the first run must look for HUD elements that live
  in that scene (cockpit body/displays are internal-view only).

## 5. The mouse-aim ray is cast through the sector camera's FOV and `+0xf0`

`0x00489780(x, y, z, out)` with **ESI = camera** is the screen-to-camera-space
unprojection: from the camera's `+0x298` FOV (sine/cosine tables at
`0x005c6998`/`0x005b6998`…), the view plane `+0x300/+0x304` (or the defaults
`(*0x00606f38)[0x28/0x2c]`), the viewport rectangle `+0x288..+0x294` and the
screen size `*(short*)(*0x00606f38+4/+6)`, it returns `(x_cam, y_cam, z, 0)`
at depth `z` (16.16).

The player ship's fire control `0x00445170` (called from the object update
`0x00416750`, i.e. before the cockpit update of the same loop iteration) has a
mouse-aim branch gated on `param_4 & 2 && param_4 & 0x20 && *0x00607ce8 != 0`
(cursor active) and the cockpit's ref view object being this ship:

```
00445b3d  mov esi,[edi+0x58]          ; EDI = cockpit -> sector camera
00445b4d  call 0x00489780(cursor_x=*0x00607cec, cursor_y=*0x00607cf0, range, &v)
00445b52  cmp [ebx+0x10],0 ; jnz      ; gun index 0:
00445b58  lea ecx,[edi+0xf0]          ;   v = v × cockpit(+0xf0)   (camera -> ship frame)
00445b63  call 0x004f0da0
...
00445c40  lea ecx,[ebp-0x200]         ; v = v × R(gun mount angles)   (0x004f0270 at 0x0044593e)
00445c4d  mov ecx,[ebp-0x34]          ; v = v × mount matrix (weapon record +0x20)
00445c55  mov ecx,[[ebx+8]+0x70]+0x40 ; v = v × ship node basis      (ship frame -> world)
00445c72  call 0x0040e720             ; + position
```

`*0x00607cec/*0x00607cf0` are the cursor coordinates written by the input
handler `0x00406de0`. So the aim direction is `unproject(cursor; camera FOV,
viewport) × R_view(+0xf0) × … × B_ship`, and **the camera basis itself is not
read** — `+0xf0` stands in for it through the identity
`camera.basis = R_view × B_ship` that the cockpit update maintains (§3 step 5).
Consequence for an in-engine camera change: writing only `camera+0x40` would
leave the aim ray on the vanilla orientation; writing **both** the camera basis
and `+0xf0 = B_cam_new × B_shipᵀ` keeps `camera.basis = +0xf0 × B_ship` true and
the aim ray, the overlay projection and the rendered view identical. The
`INS_CockpitGetCursorAim` object pick (§4) uses the overlay icons, which are
projected through the camera after the hook — consistent as well. The mouse
steering dead zone (`0x0040e8c0`, from `0x0040fec0`) uses only the
cockpit-scene camera's FOV and the cursor offset from the screen centre; it does
not depend on the orientation.

## 6. Clocks

`0x004d1df0` (per loop iteration): `QueryPerformanceCounter`, `ms = Δticks /
*0x00608a94` (busy-waits until at least 1 ms passed), then
`*(0x00606f34)+0x720 += ms` (**real-time milliseconds**) and
`+0x718 += clamp(ms, 1, 200) × ((+0xcc × +0xd0) >> 16)` (**game milliseconds**,
scaled by the SETA factor at `+0xcc` and the time-scale at `+0xd0`; `*0x00608a98`
overrides one step when non-zero). The cockpit update's own timers (`+0x200`,
`+0x208`, the view transitions, `0x00445170`'s weapon timers) use `+0x718`; only
the zoom animation uses `+0x720`. A camera smoothing that must not speed up
under SETA therefore needs its own wall-clock `dt` (the proxy uses
`QueryPerformanceCounter` directly), not `+0x718`.

## 7. Events that change the pose discontinuously

- View switch: the scripts call `INS_CockpitSetViewMode`/`SetViewPos`/
  `ChangeView`; `+0x150`, `+0x130`, `+0x90..` change, then `0x004218b0`
  animates over `+0x1b8` ms (game time) when a duration is set.
- Connect-mode changes (`+0x1c0`): docking sequences and cinematics use 4/5/6/8/9
  with random start angles (`_rand()` at `0x00422cd0`).
- Ref object change (`+0xc`): ship change, ejecting, spacesuit.
- Sector change (gate jump, jumpdrive): the ship node's `+0xb0` position jumps
  by sector-scale distances in one update; `+0x1fc` follows one frame later.
- Vanilla does no camera collision or distance clamping in the follow branch;
  the only clamps are the FOV minimum `0x106` and the `+0x160`-offset
  nearest-object search for ship types with flag `0x8000`.

## 8. Function labels added

`x3ap_function_labels.json`: `0x00403840` (main loop, cockpit update → frame
routine order), `0x0041c8f0`, `0x0041cd20`, `0x0041cde0`, `0x0041f8d0`,
`0x00420260`, `0x00420480`, `0x004205e0`, `0x004216e0`, `0x004218b0`,
`0x00422ca0`, `0x00422cd0`, `0x00422f40`, `0x00425410`, `0x00426360`,
`0x0042a2d0`, `0x0042d340`, `0x00445170`, `0x00469e80`, `0x00489780`,
`0x004899f0`, `0x0040e880`, `0x0040e8c0`, `0x0040fec0`, `0x004d1df0`,
`0x004f0270`, `0x004f0da0`, `0x004f17f0`, `0x0040e720`, `0x00406de0`.

## Reproduce

```sh
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process X3AP.exe -noanalysis -readOnly -scriptPath tools/analysis \
  -postScript X3CameraState.java /tmp/x3-camera-study/out.txt \
  sym:INS_CockpitGetCursorAim data:00488c70 dec:0042d340 dec:004205e0 ins:004205e0 \
  dec:004218b0 dec:00422cd0 dec:00425410 dec:00489780 dec:00445170 ins:00445170 \
  dec:004d1df0 dec:0041cde0 data:0041cde0 data:00471f50 data:00607cec dec:0040e8c0 \
  dec:004f17f0 dec:004f0da0 dec:004f0270 load:0x150 load:0x298 load:0xf0 load:0x718 \
  range:00445b32:64 range:00445c3d:40 range:00420afb:70 range:00420b49:60
python3 verification/probe/verify_chase_camera_site.py --json
```

The command-name tables were located with a Python pass over the PE sections
(pointer arrays whose targets are identifier strings; the registration
functions are the sole references to the tables' first entries).

## Uncertainty

- Every claim is static. Which script sets which view mode/position for the
  "back" view is in the game's KC scripts, not in the executable; the chase
  camera detects the back view geometrically from the vanilla pose instead.
- The gun-0 `+0xf0` multiply and the mount-matrix chain in `0x00445170` were
  read from the listing; the decompiler drops the register-passed matrix
  arguments, so the exact frame each intermediate vector is in is inferred from
  the operand order, not proven by execution.
- The cockpit-scene camera's one-frame-old `+0xf0` (step 4) is a listing-order
  observation; whether any external-view HUD element renders in that scene is a
  runtime question.
- `0x0044fe20`/`0x00450520` (docked/carried parent transform) were not
  decompiled; the chase camera derives the effective ship basis from the
  vanilla identity and passes scripted connect modes through.
