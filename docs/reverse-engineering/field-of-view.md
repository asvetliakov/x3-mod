# Field of view: representation, base value, readers and a safe write site

Static study of the installed `X3AP.exe` (SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, base
`0x00400000`) and the installed KC script object `addon/04.cat:L/x3story.obj`
(decoded SHA-256 `ed5786a0…faff7a`), 2026-09-24. Ghidra 12.1.3 headless,
`-readOnly -noanalysis`, project `/tmp/x3-ghidra-research`,
`tools/analysis/X3CameraState.java`; raw listings stayed in the session
scratchpad. No game or Wine process was started. Marks: **[s]** static reading
of code or data, **[m]** measured (a capture, a byte read of the installed file
or a script output), **[i]** inferred.

Builds on [external-camera.md](external-camera.md) (cockpit layout, the zoom
block), [camera-state-and-frame-routine.md](camera-state-and-frame-routine.md)
(§2–4: view and projection construction, per-submission near/far) and
[camera-numerics.md](camera-numerics.md) (captured projections).

## Answer in brief

- The engine's FOV is one binary angle `F` ("focus", 65536 = 360°), default
  `0x4000` = 90°. The projection is `m00 = cot(F/2)/W`, `m11 = cot(F/2)/H`
  with a view plane `(W, H)` fixed at device init: `H = 0.75`,
  `W = 0.75·w/h` for any display at least as wide as 4:3. So `F` is the
  horizontal FOV of the central 4:3 region; the vertical FOV is
  `2·atan(0.75·tan(F/2))` (73.74° at `F = 0x4000`) and the horizontal grows
  with the aspect ("Hor+") [s][m].
- The base is a single integer, `*(*0x00608504 + 0x24)` (the cockpit
  registry), initialised to `0x4000` by the registry constructor and changed
  only by the script command `INS_SetFocus` [s]. Every cockpit update copies it,
  divided by the cockpit zoom in the internal view, into the sector, galaxy and
  dust cameras' `+0x298` and into cockpit `+0x230`; everything that must agree
  with the rendered picture (projection, frustum cull, LOD, mouse-aim
  unprojection, target brackets, lead reticle, detail-map blend) reads one of
  those two fields [s].
- Recommended write site: the registry constructor's immediate at
  `0x0041c9dc` (`MOV [ESI+0x24],0x4000` → `MOV [ESI+0x24],F_user`), plus a
  one-off data write of `registry+0x24` when the registry already exists at
  install. No code runs per frame; zoom, clamps and all consumers follow [s][i].

## 1. Representation and projection

**Units [s].** `+0x298` is a binary angle with 65536 = 360°:
`0x004be460` computes `t = F · (1/65536) · 2π · 0.5` (constants `0x005654e0`
= 2⁻¹⁶, `0x005654f8` = 2π, `0x00565508` = 0.5; float reads [m]), i.e. the
half-angle `F·π/65536`. `0x4000` is 90° — a quarter turn of the 0..0x10000
circle, **not** 16.16 degrees. The script layer confirms it: KC method
`SetFocus` (class `0x96`, CODE `0x156e6`) converts its argument from degrees
with `(deg << 16) / 360` before calling `INS_SetFocus` [s: bytecode `05 10 58
06 01 68 51`, opcode `58` = shift and `51` = divide inferred from use].

**Projection builder [s].** `0x004be460` (`__fastcall`, ECX = camera,
EAX = destination float[16]):

```
(W,H) = (cam+0x300, cam+0x304)/65536 when both > 0, else (*0x00606f38+0x28, +0x2c)/65536
m00 = cot(F·π/65536) / W        m11 = cot(F·π/65536) / H
m22 = 1.0000030   m23 = 1   m32 = -6.0000184     (per-submission overrides: camera-state §4)
```

It is called from the view activation `0x004be520` (which also caches
`*0x005786a0 = F`), the secondary/env builder `0x004be670`, and the frustum
test `0x004c6aa0` ([render-node-bounds.md](render-node-bounds.md) §1). The
init-time twin `0x004be3f0` is fed `*0x005786a0` from `0x004b9770`
(`0x004b9aef`).

**Where the aspect comes from [s].** `0x004dac90` (display/device init),
`0x004db167..0x004db1e3`, with `w, h` = `*(short*)(**0x00606f38 + 4/+6)`:

```
r = trunc(h·65536 / w)                    ; 0x00412450(h, w)
if r > 0xc000:  W = 0x10000,  H = trunc(0x10000·h / w)     ; narrower than 4:3 (5:4)
else:           H = 0xc000,   W = trunc(0xc000·w / h)      ; 4:3 and wider
```

These are the only writers of `+0x28/+0x2c` of the display struct (data
references to `0x00606f38`); nothing else rescales them [s]. Every gameplay
camera leaves `+0x300/+0x304` at 0 (allocator `0x00488c70` stores 0; the only
script writers are `B3D_CameraSetAspectRatio`/`SetAspect`, used by the map and
explicit-viewport cameras, below) [s][i]. Hence the captured `m00 = 0.8,
m11 = 1.3333` at 1280×768 and `m00 = 0.375` at 5120×1440 are both `F = 0x4000`
with `H = 0.75` [m: camera-numerics.md; the 5120×1440 figure from the brief].
The engine therefore fixes the **4:3-horizontal** FOV, not the vertical one;
the vertical is constant only because `H` is pinned to 0.75 for every aspect
≥ 4:3.

## 2. Every reader of the FOV

`load:0x298` over the whole image finds 45 operand sites (41 without `ESP`-relative locals); the camera ones are:

| Reader | Site | Camera | Use | Follows a new base? |
| --- | --- | --- | --- | --- |
| projection `0x004be460` | `0x004be4ae` | any activated view | `m00`, `m11` | yes [s] |
| frustum cull `0x004c6aa0` | via `0x004be460` | the view | six planes from view×projection | yes [s] |
| near plane in `0x004c4fc0` | `0x004c50a4` | the view (`+0x270 & 0x800000`) | `zn = 6 + (F < 0x2147 ? 100·(1 − 4F/65536) : 0)` | yes; `zn` stays 6 for `F ≥ 0x2147` (46.8°) [s] |
| cull/LOD pass `0x0047cfe0` | `0x0047d1ce` | the view | LOD distance `D·F/0x4000` (linear, not `tan`) before the LOD metric `r·640/D'` and the small-object measure | yes [s] |
| occluder list `0x00488a70` | `0x00488ac2` | the view | same `D·F/0x4000` scale | yes [s] |
| screen-point helper `0x00488720` | `0x004887fe` | the view | `tan(F/2)` × view plane | yes [s] |
| render visit `0x0047d9c0` | `0x0047e149` | view with `+0x270 & 0x100`, node `+0x12c & 0x20000000` | `tan/cot(F/2)` × view plane, screen list at camera `+0x2a0` | yes [s] |
| effect state `0x004c0150` | `0x004c3c35` | the view | `p_DetailMapBlendWeight = (0.5·scale/D) / tan(F/2)`, clamp 0.9 (`0x00565710`) | yes [s] |
| unprojection `0x00489780` | `0x004897ad` | ESI camera | screen → camera ray with `F`, view plane, viewport | yes [s] |
| – mouse-aim fire `0x00445170` | `0x00445b4d` | sector (`cockpit+0x58`) | cursor ray | yes |
| – `INS_CockpitProjectPosition` `0x004899f0` | – | sector | script projection | yes |
| – steering dead zone `0x0040e8c0` | `0x0040e8fb` | cockpit scene (`+8`) | `abs(v) − F·0xa3d/0x4000` | no: cockpit-scene `F` stays `0x4000` (outside zoom) |
| HUD target list `0x00427d50` (cockpit `+0x79c`) | `0x00427e3f`, `0x004280b4` | copies cockpit-scene `F`, plane, viewport into its own camera; scales positions by `1/tan(cockpit+0x230 · π/65536)` when `+0x230 ≠ 0x4000` | brackets follow the sector `F` | yes [s] |
| HUD view `0x0042d140` (cockpit `+0x7e4`) | `0x0042d220` | copies cockpit-scene `F`, plane, viewport | HUD layer | stays `0x4000` |
| lead reticle in `0x0042a2d0` | `0x0042a859` | `cockpit+0x230` angular scale, cockpit-scene plane/viewport | [chase-lead-reticle.md](chase-lead-reticle.md) | yes [s] |
| B3D script dispatcher `0x00493b40` | `0x00494f34` (case `0x3d` `B3D_CameraSetFocus`), `0x00494f52` (`0x3e` GetFocus) | any script camera | | – |
| node animation `0x0048dc20` | `0x0048e78e`, `0x0048e7a8` | animated camera nodes | keyed FOV | – |
| scene stream loader `0x00479d10` / writer `0x00478690` | `0x0047a435` / `0x00478d58` | stored cameras | savegame scene stream | overwritten per frame (§3) [i] |
| record/playback `0x004735c0` / `0x00476140` | `0x00473781` / `0x00477464` | render-option-gated bits 1/2 of the frame routine | camera delta record / replay [i] | – |

`0x00419430` (`0x0041a75f`, `0x0041ba2c`) and dispatcher case `0x32`
(`0x0042e7a8`, `INS_CockpitEnableCockpitBody`) use a `+0x298` of other
structures, not a camera [s].

**Cameras [s].** The cockpit update writes `F` to the sector (`+0x58`), galaxy
(`+0x5c`) and dust (`+0x60`) cameras, so the background and dust layers keep
the same FOV as the scene. The cockpit-scene camera (`+8`: cockpit body in the
internal view, HUD layers) gets `0x4000` outside the internal view and
`0x4000·0x10000/zoom` inside it, independent of the base. Env-map/cube
cameras are not written by the cockpit update and keep their own `+0x298`
(allocator default `0x4000`) [i: the env-map camera's own writer not traced].

## 3. The base value and zoom

**Base [s].** `*(*0x00608504 + 0x24)`:

| Access | Site | Value |
| --- | --- | --- |
| registry constructor `0x0041c960` | `0x0041c9d9` `c7 46 24 00 40 00 00` [m] | `0x4000` at every registry creation (`0x00403a26` in the main function `0x00403840`, `0x004050f4` in `0x00404cc0`) |
| `INS_SetFocus`, `0x0042d340` case `0x21` | `0x0042dc04` `89 4a 24` [m] | script argument |
| cockpit constructor `0x0041f8d0` | `0x0041fd95` | read into cockpit `+0x230` (initial sector FOV for camera attach) |
| cockpit update `0x004205e0` | `0x0042114e` | read every frame |

Data references to `0x00608504` show no other `+0x24` store; a registry
pointer passed in a register was not traced exhaustively [s/i]. The registry
is built by the constructor at both creation sites [s] and no loader of
`+0x24` was found, so it is not restored from a savegame [i]; whether a savegame stores the script-side degrees (class `0x96` member
`0x16`) and replays them is not established — no code path re-issues
`INS_SetFocus` on load (§4) [i].

**Per-frame application [s]** (`0x0042113a..0x004213e5`, only when cockpit
`+0xc` (ref object) is non-zero; `0x0042113e` skips the whole block otherwise):

```
base = registry+0x24                                     ; 0x00421148/0x0042114e
if (+0x10 != 0 && +0x150 == 1):                          ; internal cockpit view: zoom
    advance zoom state +0x238..+0x248 (real clock +0x720)
    scene  = zoom ? 0x4000·0x10000/zoom : 0               ; EDI, 0x0042131e
    sector = zoom ? base·0x10000/zoom   : 0               ; ESI, 0x00421345
    scene  = max(scene, 0x106)
else:                                                     ; 0x00421588
    scene  = 0x4000
    sector = (+0x1c0 == 6) ? +0x230 : base                ; connect mode 6 holds the last value
sector = max(sector, 0x106)                               ; 0x0042137c
+8.+0x298 = scene; +0x58/+0x5c/+0x60 .+0x298 = sector; cockpit+0x230 = sector   ; 0x00421389..0x004213e5
```

Zoom therefore **scales the base**; it never overwrites it. An option that
writes the base gets zoom for free; one that writes `camera+0x298` directly
would be undone by the next cockpit update and would fight the zoom. The
camera-attach commands `INS_CockpitSetSectorCamera/Galaxy/Dust` (cases 5–7,
`0x0042d509`, `0x0042d53c`, `0x0042d584`) set the new camera's `+0x298` to
cockpit `+0x230` [s].

## 4. The script layer: an in-game FOV setting exists

From the KC object (scan `verification/results/field-of-view/kc_fov_calls.py`,
output beside it) [m]:

- `INS_SetFocus` has one caller, `SetFocus` of class `0x96` (the player
  controller class of [selection-native-vm.md](selection-native-vm.md)); it
  stores the degrees in member `0x16` (`GetFocus`, CODE `0x156dd`, returns it)
  and passes `deg·65536/360`.
- `SetFocus` is called three times, all from a menu class (class id `0x8d3`,
  methods `Create`, `AddScriptOptions`, `GenOnOff`, `Input`, `ChangeValue`):
  `Input` at CODE `0x114f43` and `0x115311` with
  `SA_GetGlobalParameter(0x59, 70)` and `(0x5a, 100)`, and `ChangeValue` at
  `0x115914` with a member stepped by one and clamped to the same two globals.
- Tuning IDs `0x59`/`0x5a` are `SG_MIN_FOV`/`SG_MAX_FOV` (name table
  `0x00552e10`, `{name, id}` pairs); every installed `types/Globals.pck`
  (`03`, `04`, `10`, `addon/01`, `addon/02`) sets them to **70** and **100**.
  The EXE never reads them itself (no `0x59`/`0x5a` comparison on the tuning
  table `0x00606fa4`) [s].
- `ShowSpace` (class `0x25e`, the monitor `Show` path) calls
  `B3D_CameraSetFocus(member 5, 0x4000)`; `___sectorCamOn` sets focus from a
  local and `B3D_CameraSetAspectRatio(cam, 1.0)` on its own camera (member
  `0x65`); `UpdateCamera` (cut-scene system) sets focus from locals;
  `B3D_CameraCalcFOV` (`0x00493b40` case `0x46`) is a pure size/distance helper.

So the game has a menu-driven FOV in focus degrees 70..100 (vertical 55.4°..
83.6° at ≥ 4:3). Which menu shows it, and whether the value survives a
reload, were not established [i]. Values below 70 are reachable only by
`INS_SetFocus` directly, which is what a proxy write emulates.

## 5. Recommended write site

**Site [s][m].** Registry constructor `0x0041c960`, instruction
`0x0041c9d9 c7 46 24 00 40 00 00` (`MOV dword [ESI+0x24],0x4000`). Replace
the imm32 at `0x0041c9dc..0x0041c9df` with `F_user`; the instruction keeps its
length, operands and semantics.

| Check | Result |
| --- | --- |
| Instruction boundary | unchanged: the same 7-byte instruction, only its immediate differs; no branch can land inside an immediate |
| Registers, flags, x87/SSE, LastError | untouched: a `MOV m32, imm32` reads and writes nothing else before or after the patch |
| Atomicity | `0x0041c9dc..0x0041c9df` lies in the aligned qword `0x0041c9d8`, so `engine_patch::write_code` (one `lock cmpxchg8b`) publishes it in one store |
| Reentrancy | no proxy code executes at run time; the constructor runs on the game's main thread |
| Timing | the proxy installs patches at its first `Direct3DCreate9` (`loader.cpp` `load_backend`, `capture.cpp` initialisation); the EXE calls `Direct3DCreate9` from the init body `0x00402780` (`0x00402edc` → `0x004d8470`) and creates the device at `0x0040332a`, while the registry is built in `0x00403840` at `0x00403a26`. The order of `0x00402780` before `0x00403840` is inferred, not traced [i]. Cover a late install with a one-off data write: if `*0x00608504 != 0` and readable, store `F_user` at `+0x24` |
| Rollback | restore `00 40 00 00`; the live registry keeps whatever value it holds until the next creation (write `0x4000` back if the option is removed at run time) |
| Identity | verify the 7 bytes at `0x0041c9d9` and, for the reader contract, `8b 15 04 85 60 00 8b 72 24` at `0x00421148` and `89 4a 24` at `0x0042dc04` [m: `site_bytes.py`] |

**Semantics.** The proxy value becomes the game's default focus: every
registry creation starts from it, the cockpit constructor copies it into
`+0x230`, the zoom divides it, and the in-game FOV menu (§4) can still change
it for the running session. That matches "`--fov` default = the game's own
value": with the option absent nothing is patched.

**Enforcement variant (only if the menu must not override `--fov`) [s].**
Replace the two-instruction span `0x00421148..0x00421150`
(`8b 15 04 85 60 00 8b 72 24`, `MOV EDX,[0x00608504]; MOV ESI,[EDX+0x24]`)
with `8b 35 <&proxy_focus>` + `90 90 90` (`MOV ESI,[abs32]`). EFLAGS from the
`CMP [EBX+0x10],0` at `0x00421144` are live to the `JZ` at `0x00421163`; `MOV`
and `NOP` leave them intact. EDX from `0x00421148` is dead (overwritten at
`0x0042115e` without a read); ESI is the live output and is produced
identically. The intra-function listing has no branch into `0x00421144..
0x00421169`; the whole-`.text` byte scan finds only `0x0042113b → 0x00421149`,
which is the interior of the `CMP [EBX+0xc],0` at `0x0042113a`, not an
instruction [s][m]. The site is disjoint from `chase_lead_final_fov`
(`0x004213dd`). The span crosses the qword boundary `0x00421150`, so the write
is not a single atomic store; install it in the install window as the chase
camera site is. This variant still needs the constructor patch for the
cockpit constructor's `+0x230` copy (`0x0041fd95`), which feeds camera attach
and connect mode 6.

**Value.** `F_user = round(65536/π · atan(tan(v/2)/H))` with `v` the requested
vertical FOV and `H` the default plane height (0.75 for ≥ 4:3; `h/w` for
narrower displays), i.e. the law of §1 inverted. Examples
(`verification/results/field-of-view/fov_numbers.py`) [i: arithmetic]:

| Request | Display | `F` | Vertical | Horizontal |
| --- | --- | --- | ---: | ---: |
| vanilla | any ≥ 4:3 | `0x4000` (90°) | 73.74° | 106.26° (16:9), 138.89° (5120×1440) |
| `--fov 59` | 16:9 or 5120×1440 | `0x34aa` (74.06°) | 59.00° | 90.33° / 127.13° |
| 90° horizontal on 16:9 (`v` = 2·atan(9/16) = 58.7155°; a rounded 58.72 gives `0x3471`) | ≥ 4:3 | `0x3470` (73.74°) | 58.72° | 90.00° / 126.87° |
| `SG_MIN_FOV` 70 | ≥ 4:3 | `0x31c7` | 55.41° | 86.07° / 123.66° |
| `SG_MAX_FOV` 100 | ≥ 4:3 | `0x471c` | 83.58° | 115.63° / 145.06° |
| near-plane threshold `0x2147` | ≥ 4:3 | 46.80° | 35.96° | 59.96° / 98.17° |

## 6. What the option means for the proxy

- **Camera reader.** `renderer::camera_state_from_matrices` latches `m00`,
  `m11`, `m20`, `m21` from the game's `*0x00608a38` and checks only positivity,
  `m23 = 1` and view orthonormality; no FOV constant is assumed
  (`src/renderer/camera_reprojection.h`). TAA, motion vectors, the fog march
  and the sun-shadow apply read the latched `m00/m11` and follow any `F` [s].
- **Jitter** is added in projection space (`m20/m21`, NDC units), independent
  of `F` [s].
- **Depth range.** `projection_default_m22/m32` (`motion_output.cpp`: sun
  shadow apply, fog) assume `zn = 6`. That holds while the sector `F ≥ 0x2147`
  (vertical ≥ 35.96°). Bound `--fov` to ≥ 36° vertical; zoom already crosses the
  threshold today (pre-existing) [s].
- **Small-parts cull.** `cull_small_parts_core.h::threshold_for` converts
  pixels with `px = s·m00·width/1280`, where `s = r·640/D'` and the engine's
  `D' = D·F/0x4000` (`0x0047d1ce`). That is exact only at `F = 0x4000`; for
  another base the true pixel size is `s·m00·width/1280 · F/0x4000`, so the
  threshold must include the factor `F/0x4000` (0.823 at `--fov 59`), taken
  from the installed base (and the zoom, if it matters) or from
  `cot(F/2) = m11·H` [s][i]. The same applies to any LOD-overlay footprint that
  reasons in the engine's `s` metric.
- **LOD.** The engine scales LOD distances linearly by `F/0x4000` while the
  on-screen size scales by `cot(F/2)`; at `--fov 59` objects appear 1.326× larger
  but LOD distances shrink only by 0.823 (≈ 1.215×), so LOD switches happen at
  about 8 % smaller screen size than vanilla — slightly more detail pressure,
  never less [i].
- **Chase camera and lead reticle** read `camera+0x298` and the plane at run
  time (`chase_camera.cpp`, `chase_lead.cpp` final-FOV site) and follow [s].
- **Launcher.** `--fov` should carry the vertical degrees and the proxy should
  compute `F` from the actual back-buffer `h/w` with the rule of §1; above
  `SG_MAX_FOV` (83.6° vertical) the engine's own UI never goes, and `F` must
  stay below `0x8000`.

## 6.1 Implemented (2026-09-24)

`--fov <vertical degrees 36..120>|game` / `X3M_FOV` (`src/proxy/fov.cpp`, site core
`src/proxy/fov_sites.h`) implements §5 as recommended: after the structural identity, the reader
contract (`0x00421148`, `0x0042dc04`) and a 28-byte window compare at `0x0041c9cc`, the imm32 at
`0x0041c9dc` becomes `F = round(65536/π·atan(tan(v/2)/0.75))` with one `lock cmpxchg8b` (upper half of
the aligned qword `0x0041c9d8`), read back, rolled back on failure, and restored on a dynamic unload only
over our own value; if the registry already exists, `+0x24` gets one validated
`InterlockedCompareExchange`. The launcher default is the exact 90°-horizontal-on-16:9 value
58.7155° (`F = 0x3470`; the rounded 58.72 gives `0x3471`); `game` or 73.74 patches nothing. `H` is
fixed at 0.75, so displays narrower than 4:3 get a slightly larger vertical angle. The enforcement
variant (`0x00421148`) is not installed: the in-game FOV menu still overrides the base for the running
session. The small-parts cull threshold carries `F/0x4000` with the view's F taken from the latched
projection, `cot(F/2) = max(0.75·m11, m00)` (zoom included, no engine read; `registry+0x24` only as a
fallback), and `tools/analysis/cull_census.py` buckets with the same factor (§6). Log rows
`fov`, `fov_confirm` (first Present: `registry+0x24` against the configured value; the sector camera's
`+0x298` is not read, because the camera reader exposes only the projection buffers) and
`fov_restore`; evidence in [field-of-view ledger](../verification/field-of-view.md).

## 7. Risks

- **HUD in screen space [s].** HUD layers (`+0x79c`, `+0x7e4`) and the
  cockpit-scene camera stay at `0x4000`; their positions of *sector* objects
  (target brackets, lead reticle, cursor picking via overlay icons) use
  `cockpit+0x230`, so they stay registered with the scene. The 2-D readers of the view plane
  (`0x0041e030`, `0x0041e350`, `0x00471660`, the pixel conversion in
  `0x0042a2d0`) see an unchanged plane, because the option changes `F` only.
  The `+0x79c` list copies the cockpit-scene camera before the same frame's
  FOV write, so it runs one cockpit update behind a base change [s].
- **Cockpit scene [s].** In the internal view the cockpit body renders with
  `F = 0x4000` (or zoomed) while space uses the user base: the 3-D cockpit
  keeps its framing, the scene outside changes. The same split already exists
  during zoom. The mouse-steering dead zone uses the cockpit-scene FOV and is
  unaffected.
- **Saved games [i].** Camera `+0x298` is written to and read from the scene
  stream (`0x00478690`/`0x00479d10`) but rewritten every frame from the base
  while a ref object exists; the registry is rebuilt with the (patched)
  constant. No FOV state from the proxy reaches a save.
- **Minimum `0x106` (1.44°) [s].** Applies after zoom to both the sector and
  the cockpit-scene value; a user base far below 90° makes the deepest zoom
  hit the floor earlier (`zoom ≤ F/0x106`).
- **No ref object [s].** When cockpit `+0xc == 0` the block is skipped and the
  cameras keep their last FOV; script cameras (`ShowSpace`, cut scenes) set
  their own `B3D_CameraSetFocus` values and are not affected by the base.
- **Narrow displays [s].** For `h/w > 0.75` (5:4) the plane switches to
  `W = 1`: the vertical then follows `h/w`, and `F` must be computed with that
  `H`.
- **Resolution change [i].** The default plane is computed only in
  `0x004dac90`; a mode change that does not go through it would keep the old
  plane. Unverified.

## Reproduce

```sh
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process X3AP.exe -noanalysis -readOnly -scriptPath tools/analysis \
  -postScript X3CameraState.java <scratch>/out.txt \
  dec:004be460 dec:004be3f0 dec:004be520 load:0x298 load:0x300 load:0x304 load:0x230 \
  data:005786a0 data:00608504 data:00606f38 data:00606fa4 dec:0042d340 ins:004205e0 \
  dec:004dac90 ins:004dac90 dec:0041c960 ins:0041c960 ins:0041f8d0 dec:0042d140 dec:00427d50 \
  dec:0047cfe0 dec:0047d9c0 dec:00488720 dec:00488a70 dec:0048dc20 dec:004c0150 dec:00493b40 \
  dec:004735c0 dec:00478690 dec:0040e8c0 dec:00412450 dec:00469a30 dec:004333a0 \
  sym:Direct3DCreate9 data:004d8470 data:00403840
python3 verification/results/field-of-view/site_bytes.py
python3 verification/results/field-of-view/kc_fov_calls.py
python3 verification/results/field-of-view/fov_numbers.py
```

## Open

- Which menu page shows the class-`0x8d3` FOV entry, and whether a chosen value
  persists across a reload (class `0x96` member `0x16` in the save, no
  re-issue of `INS_SetFocus` found).
- The call order of `0x00402780` (proxy install at `Direct3DCreate9`) relative
  to `0x00403840` (first registry creation); the one-off data write covers
  either order.
- The env-map camera's own `+0x298` writer.
- No runtime read yet: one `camera+0x298` / `registry+0x24` log at the first
  flight with the patch would confirm §3 (`0x4000` vanilla, `F_user` patched,
  `F_user·0x10000/zoom` in a zoomed cockpit view).
