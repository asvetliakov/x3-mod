# External camera: the cockpit object, its cameras, the view pose and the aim ray

Static engine analysis (Ghidra 12.1.3, `-readOnly -noanalysis`, project
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
[object-identity.md](object-identity.md) (node layout). The first user flight later confirmed the hook applies. The corrected
anchor-domain and current-view findings are documented in
[chase-camera-first-flight.md](chase-camera-first-flight.md); engine semantics
below remain static claims except where explicitly paired with runtime evidence.

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
| `+0x10` | ref view object (the player ship for the cockpit-body/zoom logic; type test `*(short*)(+0x54 … +0x48)`). **`+0x10 == +0xc` marks the active control cockpit**: the fire control `0x00445170` finds the cockpit by handle (`0x0041cd20`) and gates its aim branch on `*(cockpit+0x10) == this ship`; the registry `*0x00608504` walk (`0x0041cde0`) updates every cockpit, monitor cockpits (`INS_CockpitSetMonitorNumber`, cases 0x1b/0x1c, `+0x1dc`) and `INS_SetActiveControlCockpit` (case 0x48, registry `+0x10`, unconfirmed layout) included — the chase camera uses the `+0x10 == +0xc` predicate (review 31 A1) | cases 0x28/0x2c, `0x004205e0`, `dec:00445170`, `dec:0041cde0` |
| `+0x58` | **sector camera** = the scene camera of the external/internal view (`INS_CockpitSetSectorCamera`, case 5) | FOV, fog (`+0x368..+0x370`), flag `0x10000`, the overlay and the aim ray all use it (§4, §5) |
| `+0x5c`, `+0x60` | galaxy camera (case 6), dust camera (case 7); `+0x5c` receives a copy of `+0x58`'s pose at `0x00421533`.., `+0x60` follows through `0x0041efc0` | `0x004205e0` tail |
| `+0x90/+0x94/+0x98` | view angles alpha/beta/gamma (`INS_CockpitGetViewAlpha/Beta/Gamma`, cases 0x3a–0x3c; binary angles) | `0x00422ca0` copies the targets `+0xa8/+0xac/+0xb0` into them |
| `+0xa8/+0xac/+0xb0` | target view angles (`INS_CockpitChangeView`, case 0x2b) | |
| **`+0xf0`** (12 ints) | **view-relative basis** `R_view`: the camera orientation relative to the ship, built from current view angles at `0x00422c5c` inside `0x004218b0`, before camera construction and consumed by the aim ray (§5) | `camera(+0x58).basis = R_view × B_ship` at `0x00420c02` |
| `+0x120` | lock-view target (`INS_CockpitLockView`, case 0x2e) | `0x00422f40` |
| `+0x128` | no-decay flag (case 0x62) | |
| `+0x130/+0x134/+0x138` | **view position** in the ship frame (`INS_CockpitSetViewPos`, case 0x2f): the boom offset of the external views | native base-domain path `0x00420ad3..0x00420afb` uses `node+0x30` and `0x00450520`; render-domain path `0x00420b27..0x00420b49` uses `node+0xb0/+0xc0` |
| `+0x140..` | view point position (case 0x5b) | |
| **`+0x150`** | **view mode** (`INS_CockpitSetViewMode`, case 0x30). `1` = internal cockpit view: `0x004205e0` sets flag `8` on the ship's render node `+0x134` (hidden) when `+0x150 == 1`, clears it otherwise (`0x004216c0..`); every other value is an external view whose geometry the scripts define through `+0x130`/`+0x90..` | `CMP [EBX+0x150],1` sites at `0x004207bf`, `0x00421169`, `0x004216b0` |
| `+0x160/+0x164/+0x168` | view camera offset (case 0x36): an extra offset applied after the pose (`0x00420c3d..0x00420e03`), with a nearest-object search when the ship type has flag `0x8000` | |
| `+0x1a0` | flags; bit 2 (`& 4`) makes the position and the basis verbatim copies of `+0x130` / `+0xf0` (§3 step 5; tests at `0x004209f7`, `0x00420aaf`, `0x00420bb1`) — the chase camera passes such frames through (review 31 A2) | `0x004205e0` |
| `+0x1d8` | aim gun index; the fire control's mouse-aim branch requires `+0x1d8 ≥ 0` | `dec:00445170` |
| `+0x1e0` | tracked object (`INS_CockpitGetTracking`, case 0x26, returns `*(+0x1e0)+8`; the fire control takes `*(cockpit+0x1e0)` as the aim target) | `dec:0042d340`, `dec:00445170` |
| `+0x1e4` | tracking mode (short): `INS_CockpitIsTracking` = `== 1` (case 0x24), `INS_CockpitIsEnemyTracking` = `== 4` (case 0x25); setter `0x00425a10` not decompiled. The chase camera's combat-tightness predicate is `+0x1e4 ∈ {1, 4}` with a readable `+0x1e0` (review 31 A9); **unverified in game** until the first run's `tracking=`/`locked=` fields are read against a selected target | `dec:0042d340` cases 0x22–0x27 |
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
The base `*0x00608504+0x24` (registry constructor default `0x4000`, script
writer `INS_SetFocus`), the default view plane that makes `0x4000` a 73.74°
vertical FOV, every FOV reader and the write site for a user-chosen FOV are in
[field-of-view.md](field-of-view.md).

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

1. `0x004218b0(cockpit)`: view/connect-mode updates. For ordinary mode 0,
   target view angles `+0xa8/+0xac/+0xb0` feed current angles `+0x90/+0x94/+0x98`
   through the native transition logic, and **every invocation** ends at
   `0x00422c41..0x00422c5c`: ESI = cockpit `+0xf0`, call `0x004f0270` with
   those current angles. It regenerates the vanilla view basis even when the
   angles did not change. This precedes every pose/cockpit-camera consumer.
2. `0x00426360`: cockpit-body model management (internal view only).
3. Shake/angle matrices: `0x004f0270(alpha, beta, gamma)` builds a rotation
   from three binary angles (16.16 output, `ESI` = destination);
   `0x004f17f0(EAX=A, ECX=B, [ESP]=dest)` is `dest = A × B` (row-vector, 16.16);
   `0x004f0da0(ECX=M, ESI=v, EDI=out)` is `out = v × M` (vector by matrix).
4. The temporary shake matrices are composed at `0x00420767`;
   `0x00420787` builds the cockpit-scene camera basis from **current vanilla**
   `+0xf0` and that shake transform. `0x0042079e` builds its position from
   `+0x2c0` through the resulting basis. The old previous-frame interpretation
   was wrong: step 1 already regenerated `+0xf0`.
5. `if (+0xc != 0)` — the follow branch, ordinary external view (no cockpit
   body, `+0x150 != 1`):
   - External view jumps from `0x004207c6` to `0x00420aa0`. The
     `0x00420a27` multiply belongs to the internal-view branch and **reads**
     `+0xf0` (EAX) into a stack destination (`[ESP]`), not vice versa.
   - position: connect mode `5/6` or flag `+0x1a0 & 4` → `camera.pos = +0x130`
     verbatim. Outside those modes, `0x0044fe20(ref)` selects the native
     position domain, with `node = *(ref+0x70)`: the true branch
     `0x00420ad3..0x00420b19` uses **node `+0x30`** as the anchor and
     `0x00450520` as the boom basis; the false branch
     `0x00420b1e..0x00420b67` uses node `+0xb0` and node `+0xc0`.
     `0x00450520` selects `*(ref+0x50)+0x870` for ship type 7 and node `+0x40`
     for other reference types. In both cases `camera.pos = anchor +
     (+0x130) × native_basis`. The
     predicate is exactly non-null `ref+0x54` whose short `+0x48` is 1;
     it is **not established as a docking predicate**.
   - basis: connect mode `3` (unless the ref view object differs from the ref
     object), `5`, `6` or flag `+0x1a0 & 4` → `camera.basis = +0xf0` verbatim
     (`0x00420c0c`); otherwise the selected multiply at `0x00420be5` (true)
     or `0x00420c02` (false) writes:
     **`camera(+0x58).basis = (+0xf0) × native_basis`**, using the same
     `0x0044fe20` branch and `0x00450520` basis selection as the position.
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
- The cockpit-scene camera (`+8`, layer 0) is built in step 4 from the
  **current vanilla** `+0xf0` produced in step 1. It remains on the vanilla view
  while the hook smooths the sector camera. Whether an external-view HUD element
  uses it is a runtime question; the optional correction must compare the new
  view with this current vanilla basis, not a previous-frame basis.

## 5. The mouse-aim ray is cast through the sector camera's FOV and `+0xf0`

`0x00489780(x, y, z, out)` with **ESI = camera** is the screen-to-camera-space
unprojection: from the camera's `+0x298` FOV (sine/cosine tables at
`0x005c6998`/`0x005b6998`…), the view plane `+0x300/+0x304` (or the defaults
`(*0x00606f38)[0x28/0x2c]`), the viewport rectangle `+0x288..+0x294` and the
screen size `*(short*)(**0x00606f38+4/+6)`, it returns `(x_cam, y_cam, z, 0)`
at depth `z` (16.16).

The player ship's fire control `0x00445170` is called from four sites in the
engine script dispatcher `0x00460630`. The earlier attribution to a direct
`0x00416750` call was incorrect; its exact ordering relative to a displayed
camera pose requires a runtime event sequence. It has a mouse-aim branch gated on `param_4 & 2 && param_4 & 0x20 && *0x00607ce8 != 0`
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

`*0x00607cec/*0x00607cf0` are cursor coordinates supplied by the engine command
`X2_UpdateCursorSteering` (`0x00406de0`, case `0x1f`), which also writes cursor
active `*0x00607ce8`. This is script-supplied state, not a direct host mouse
callback. Provided the cursor branch is admitted, the angular input is `unproject(cursor; camera FOV,
viewport) × R_view(+0xf0) × … × B_ship`, and **the camera basis itself is not
read** — `+0xf0` stands in for it through the identity
`camera.basis = R_view × B_ship` that the cockpit update maintains (§3 step 5).
Consequence for an in-engine camera change: writing only `camera+0x40` would
leave the angular input on the vanilla orientation. Writing the camera basis
and `+0xf0 = B_cam_new × B_shipᵀ` together preserves that angular identity,
but does **not** establish cursor-fire admission or finite aim convergence.
The native routine applies a configured cone after this multiply (installed
`SG_CURSORSTEERING_MAXFIREANGLE = 30` degrees), transforms into world space,
and adds the gun-group origin, not the sector camera position. The final
barrel direction converges from its muzzle to that endpoint. Moving the camera
therefore introduces parallax that `+0xf0` alone cannot correct.

`INS_CockpitGetCursorAim` object picking (§4) uses the overlay icons, projected
through the sector camera. Object picking and the finite muzzle ray are
separate operations. The mouse-steering dead zone (`0x0040e8c0`, from
`0x0040fec0`) uses the cockpit-scene camera's FOV and cursor offset from screen
centre; it does not depend on orientation. See
[chase-mouse-fire.md](chase-mouse-fire.md) for the corrected fire call chain,
range/cone analysis, user evidence and the bounded diagnostic proposal.

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

- Every claim is static. The `+0x1e0`/`+0x1e4` tracking fields and the
  `+0x10 == +0xc` active-cockpit predicate come from the dispatcher and the
  fire control listings (review 31 A1/A9); the registry's own active-control
  slot (`*0x00608504+0x10`) is not confirmed and not used. Which script sets which view mode/position for the
  "back" view is in the game's KC scripts, not in the executable; the chase
  camera detects the back view geometrically from the vanilla pose instead.
- The gun-0 `+0xf0` multiply and the mount-matrix chain in `0x00445170` were
  read from the listing; the decompiler drops the register-passed matrix
  arguments, so the exact frame each intermediate vector is in is inferred from
  the operand order, not proven by execution.
- The earlier previous-frame cockpit-scene interpretation and docked/carried
  label for `0x0044fe20` were disproved by the targeted first-flight study.
  The exact native branch is known; its owner-type label is not required by
  the implementation. Quantifying the first flight's node-domain separation
  and confirming the corrected view's appearance await the next instrumented
  user run. See [the correction](chase-camera-first-flight.md).
