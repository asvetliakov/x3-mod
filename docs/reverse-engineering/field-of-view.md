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
- The in-game menu keeps its own number `N` (degrees, class `0x96` member
  `0x16`, script default 90, never re-applied at load) and writes
  `(N<<16)/360` through `INS_SetFocus`; touching it replaces the patched base
  with a vanilla-unit value (§7). Recommended: remap at the two write sites so
  `N` means "horizontal degrees on 16:9" (§7.3).
- At `W·tan(F/2) > 2` the lens-flare collector's horizontal off-screen test
  overflows for distant suns near the view centre and drops the sun's lens
  chain (§9); `F = 0x3470` is exactly at that bound on 32:9, `0x471c` is past it.
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
83.6° at ≥ 4:3). The menu path, the script-side default (90) and the absence
of any startup re-application are in §7. Values below 70 are reachable only by
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

## 6.2 Risks

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

## 7. The in-game FOV menu, its persistence, and the remap (2026-09-24)

Static study after Run 81 A launch 1 (run309), where the user reported that touching the in-game FOV
setting "brings the old FOV back". Capstone listings of the installed EXE and a bytecode walk of
`x3story.obj` (instruction widths from the loader's byte-swap pass `0x0049e1a0`: opcode − 5 indexes the
class table `0x0049e464`, handlers `0x0049e444`; switch opcodes `78`/`79` carry inline tables). Script:
`verification/results/field-of-view/menu_chase_path.py`, output `menu_chase_path.txt` beside it. No
Ghidra run was needed; no game or Wine process was started.

### 7.1 What the menu does [s][m]

- **The number lives in the script, not the engine.** Class `0x96` (the static player/game controller,
  194 methods `0x12faf..0x1ac3d`, 63 static members) keeps the focus in degrees in member `0x16`. The
  CLAS member record gives its default: **90**, type int [m]. Only `SetFocus` (CODE `0x156e6`, store at
  `0x156ec`) writes it; `Init`/`InitClient` do not. The EXE has no persisted setting for it: the
  persisted system natives are `P_Get/SetSysViewDistance`, `D3DFlags2`, `ShaderQuality`,
  `IORequirements` only [m]. No config file, registry key or globals entry holds a FOV value;
  `SG_MIN_FOV`/`SG_MAX_FOV` (70/100) are only the menu clamps (§4).
- **Menu class `0x8d3`, line id `0x2406`** (label read by `SE_ReadText` with the pair `0x23`/`0x10a2`;
  neither the label nor the page title was resolved):
  - `SpecialUpdate` (`0x11439d`) prints `GetFocus()` as `"%d°"` at `0x1144bc`. Display only: **opening
    the menu writes nothing**.
  - `ChangeValue` (`0x1155e6`, switch at `0x115b64`, arm `0x11589d`): `v = GetFocus()`, `v ± 1` by the
    sign of the argument, clamped to `SA_GetGlobalParameter(0x5a, 100)` / `(0x59, 70)`, then
    `SetFocus(v)` at `0x115914`.
  - `Input` (`0x114d79`) has two further arms for `0x2406` (switches `0x115108` → `0x114f35`,
    `0x1154e2` → `0x115303`) that call `SetFocus(SG_MIN_FOV)` / `SetFocus(SG_MAX_FOV)` directly (the
    keys were not identified).
- **`SetFocus(N)`**: member `0x16 = N`; `INS_SetFocus((N << 16) / 360)`, integer and truncating
  (`91 → 0x40b6`, `100 → 0x471c`).
- **`INS_SetFocus`** = dispatcher `0x0042d340` case `0x21` (table `0x0042f064`), `0x0042dbed..0x0042dc0c`:

  ```
  0042dbed 8b 45 18           MOV EAX,[EBP+0x18]        ; marshalled args (5-byte tagged cells)
  0042dbf0 8b 48 01           MOV ECX,[EAX+1]           ; payload of arg 0; no tag check, no clamp
  0042dbf3 a1 e4 85 60 00     MOV EAX,[0x006085e4]      ; VM
  0042dbf8 8b 15 04 85 60 00  MOV EDX,[0x00608504]      ; cockpit registry
  0042dbfe 6a 00              PUSH 0
  0042dc00 50                 PUSH EAX
  0042dc01 8b 45 0c           MOV EAX,[EBP+0xc]         ; task
  0042dc04 89 4a 24           MOV [EDX+0x24],ECX        ; the base
  0042dc07 e8 e4 6b 07 00     CALL 0x004a47f0           ; push the (void) result; then JMP 0x0042f04c
  ```
- **Nothing re-applies the number at startup or load.** `SetFocus` has three callers, all in the menu
  (`0x114f43`, `0x115311`, `0x115914`), none by name; `INS_SetFocus` has one call site (`0x156fb`)
  [m]. The registry therefore holds the constructor value (patched `0x3470`) after every creation,
  whatever member `0x16` says. Whether class statics are part of a savegame is not established [i];
  if they are, a saved `100` shows as "100°" while the view is the constructor value. Vanilla has the
  same mismatch.
- **Why the user saw the old FOV [m].** The menu steps from member `0x16` = 90 and writes vanilla units:
  run309's projections go `0x3470` → `0x40b6` (91) at the first press, then 92..100 (`0x471c`), down to
  70 and back to 100 (`verification/results/run309-run81a-launch1/fov_timeline_out.txt`). Any menu
  value is interpreted in the vanilla 4:3-horizontal model, so the patched default is lost on the first
  touch, and the slider's "90" never meant the patched value.

### 7.2 Who reads the stored base [s][m]

190 loads of `[0x00608504]` in `.text`; exactly three are followed by a `+0x24` access: `0x0041fd95`
(cockpit constructor → cockpit `+0x230`), `0x0042114e` (the per-frame apply, §3) and `0x0042dc04`
(the `INS_SetFocus` store); the constructor itself stores through `ESI` at `0x0041c9d9`. A registry
pointer passed in a register was not traced. Every other FOV reader of §2 (projection, frustum cull,
cull/LOD pass, occluder list, occlusion probe, lens collector, effect state, unprojection and mouse aim,
HUD target list, lead reticle) reads camera `+0x298` or cockpit `+0x230`, i.e. what the per-frame apply
wrote. The script's `GetFocus` returns member `0x16`, never the engine value.

### 7.3 Recommendation: remap at the two write sites

Design direction (user, 2026-09-24): keep the game's number `N` (menu 70..100, script default 90) and
read it as **"N degrees horizontal on 16:9"**: the engine gets `F'` with
`tan(F'/2) = 0.75 · tan(F/2)`, `F = (N<<16)/360`. With `H = 0.75` that is exactly a 16:9 horizontal of
`N` degrees and a vertical of `2·atan(0.5625·tan(N/2))`; `N = 90` gives `F' = 0x3470`, the current
default. Table (`menu_chase_path.txt`, arithmetic [i]):

| N | vanilla `F` | vertical | `F'` | vertical | 16:9 horizontal |
| ---: | --- | ---: | --- | ---: | ---: |
| 70 | `0x31c7` | 55.41° | `0x2768` | 43.00° | 70.00° |
| 75 | `0x3555` | 59.84° | `0x2a8d` | 46.69° | 75.00° |
| 80 | `0x38e3` | 64.36° | `0x2dc5` | 50.53° | 80.00° |
| 85 | `0x3c71` | 68.99° | `0x3110` | 54.53° | 85.00° |
| 90 | `0x4000` | 73.74° | `0x3470` | 58.72° | 90.00° |
| 95 | `0x438e` | 78.60° | `0x37e4` | 63.09° | 95.00° |
| 100 | `0x471c` | 83.58° | `0x3b6f` | 67.67° | 100.00° |

**Recommended: variant (b), remap where the base is written**, so the stored base already is `F'` and
every reader, including the two direct registry readers, sees one value. Two sites:

1. **Registry constructor** `0x0041c9dc` imm32 `0x4000 → 0x3470` (= `remap(0x4000)`): the existing §5
   patch, unchanged.
2. **`INS_SetFocus`**, new: `engine_patch::claim` on `0x0042dbf8`, expected `8b 15 04 85 60 00`,
   length 6 (one whole instruction), `rel32_offset` 0, `ret_pop` 0; then `push_front` a generated stub.
   This is the pattern the chase camera (`cockpit_update_pose`, a mid-function `cmp; jz` site) and
   the cull census already use; no new patch mechanism is needed. The stub runs before the displaced
   `MOV EDX,[0x00608504]`, which the tail then executes before jumping back to `0x0042dbfe`:

   ```
   stub: push eax              ; VM pointer, live (pushed by the game at 0x0042dc00)
         push ecx              ; F from the script
         call remap_focus      ; cdecl uint32_t(uint32_t): lookup, no Win32, no x87
         add  esp,4
         mov  ecx,eax          ; F' is what 0x0042dc04 stores
         pop  eax
         jmp  [continuation]   ; -> tail: MOV EDX,[0x00608504]; JMP 0x0042dbfe
   ```

   | Check | Result |
   | --- | --- |
   | Instruction boundary | `0x0042dbf8` starts an instruction; the case body is entered only at `0x0042dbed` (jump-table entry `0x21`); no byte-pattern branch, no other table entry and no absolute dword reference lands in `0x0042dbee..0x0042dc0b` [m] |
   | Atomic write | the five `jmp` bytes `0x0042dbf8..0x0042dbfc` lie in the aligned qword `0x0042dbf8`; one `lock cmpxchg8b`; byte `0x0042dbfd` is left and never executed |
   | Registers | `EAX` live (pushed at `0x0042dc00`) → saved; `ECX` is the output; `EDX` dead at entry (the tail reloads it); `EBX/ESI/EDI/EBP` callee-saved by the handler |
   | Flags | dead: `PUSH/PUSH/MOV/MOV/CALL 0x004a47f0` follow, none reads EFLAGS [s] |
   | Stack, FPU, LastError | 4-byte incoming alignment (build the handler with `-mstackrealign`); handler is a table lookup built at install (no FPU/SSE state change at run time); no API call |
   | Reentrancy, frequency | runs on the script VM (game main thread) once per menu step; the table is immutable after install |
   | Remap | script values are integers `N`, so `N = round(F·360/65536)` recovers `N` exactly from `(N<<16)/360`; a 181-entry `uint16` table `F'(N)` covers `N = 0..180`; any `F` outside `0..0x8000` passes through unchanged |
   | Rollback | restore the six bytes; the registry keeps `F'` until the next `SetFocus` (then vanilla units) |

   Consequences: the menu displays `N` from member `0x16`, so **no inverse map is needed** and the
   slider starts at 90 = `0x3470`; `--fov <degrees>` is no longer needed (only `game` = no claims). If a
   vertical override is kept, it generalises the constant: `tan(F'/2) = k·tan(F/2)` with
   `k = tan(v/2)/0.75` (`k = 0.75` for 58.7155°). The late-install data write must store
   `remap(registry+0x24)` only when the value is not already a remapped one (the existing "restore only
   over our own value" logic), and the `fov_confirm` row must expect `remap` of the value rather than
   the constructor constant. The minimum `F' = 0x2768` stays above the near-plane threshold `0x2147`
   (§2). Displays narrower than 4:3 (`H = h/w`) get a slightly different vertical, as today.

**Variant (a), per-frame remap at `0x00421148`** (for the record): the nine bytes
`8b 15 04 85 60 00 8b 72 24` (two instructions) would carry a stub that maps `ESI` before the zoom
division. EFLAGS from `CMP [EBX+0x10],0` at `0x00421144` are live to `JZ 0x00421163`, so the stub must
`pushfd/popfd`; `EDX` is dead, `ESI` is the output; it runs once per cockpit per frame (the registry walk
updates monitor cockpits too). The cockpit constructor's copy `0x0041fd95` (initial `+0x230`, camera
attach, connect mode 6) would need a second site, and the registry would keep vanilla units. More sites,
per-frame work and a live-flags site for the same result, so (b) is preferred.

## 8. Chase distance (engine side; the proxy's chase camera owns the pose)

Not needed for the proxy (the `--camera chase` pose scales the vanilla boom itself,
`chase_camera_math.h` `target_length = boom · distance_scale`), recorded for reference [s]:
the vanilla external distance is script-computed per ship, not FOV-derived. `0x25e::StartMonitor`
(`0xf0509..0xf0598`) and `SelectMode` (`0xf0aee..0xf0b73`) compute `m1b = 2·SA_GetTotalSize(ship)`
(or `SE_LinFunc(size; 11000 → 800 %, 222000 → 200 %)·size/100` when the monitor mode has bits
`0xc00`; argument order of `SE_LinFunc` and the identity of the stack-relative local slots [i]),
`m1c = size/2` for mode bit `0x10`, and call
`INS_CockpitSetViewCameraOffset(cockpit, 0, m1c, −m1b)` (dispatcher case `0x36`, stores
`0x0042e1e9..0x0042e1fb` into cockpit `+0x160..+0x168`). The external "zoom" (`SetZoom` `0xf452b`,
`SetZoomAbsolute` `0xf47e8`) rewrites the same offset, i.e. it changes distance, not FOV; the cockpit
zoom of §3 runs only in the internal view. The engine applies the offset at `0x00420c95..0x00420df0`
(`0x004f0da0` rotates `+0x160` by the sector camera basis at `0x00420c9f`, added to the camera
position at `0x00420deb`, skipped for connect modes 4/5/6/8/9). Holding the ship's screen size under a
new `F` means scaling the distance by `tan(F_vanilla/2)/tan(F/2)` (`1/0.750006 = 1.3333` at `0x3470`);
the chase math already has `half_vfov_tan` for that.

## 9. The sun's lens chain vanishes near the view centre at large `F` (run309)

**Symptom** (run309 triage, `verification/results/run309-run81a-launch1/`): after the menu moved `F`
to `0x471c`, the sun's post-HDR group (13 draws, the lens chain) is not submitted while the sun is
within about 30° of the view centre; at `0x3470` (and at `0x4000` in runs 304/305) it is present from
2.5–5° outward [m, triage].

**Mechanism [s][m].** The lens collector in the render visit `0x0047d9c0` (reached for the TSuns lens
node, `+0x12c & 0x20000000`, in views with `+0x270 & 0x100`) reads the view's `F` at `0x0047e149`,
builds 16.16 `tan(F/2)` (`[esp+0x1c]`) and `cot`, and loads the plane `W → [esp+0x18]`,
`H → [esp+0x14]` (`0x0047e23b..0x0047e26a`). Its gate (`0x0047e315..0x0047e3fc`) is `z > 100`,
`z > 2r`, then the horizontal off-screen test

```
0047e334..0047e354  ECX = |x|/2                         ; x = node+0xf0 (camera space)
0047e356..0047e361  [esp+0x10] = z/2                    ; z = node+0xf8
0047e365..0047e37b  [esp+0x10] = FixMul(tan, z/2)       ; imul (64-bit), +0x8000, shrd eax,edx,16
0047e37f..0047e391  EAX = FixMul(W, [esp+0x10])         ; same; EDX (the high half) is discarded
0047e395 3b c8      CMP ECX,EAX
0047e397 0f 8d ..   JGE 0x0047e5b6                      ; "off-screen": record+0x30 stays 0
```

`FixMul` keeps only the low 32 bits of the shifted product, so when `W·tan(F/2)·z/2 ≥ 2^31` the bound
turns negative and every `|x|` fails: **the sun is declared off-screen**. `z = D·cos θ` is largest on
the axis, so the failure covers a disc around the centre and ends where `D·cos θ < z_crit`,
`z_crit = 2^32 / (W·tan(F/2))`. With `+0x30` clear the accumulator drops by 100 per frame and the
bodies are destroyed after two frames ([lens-flare-visibility.md](lens-flare-visibility.md) §4). The `y` test uses `H = 0.75` and
cannot overflow. Evidence script: `verification/results/field-of-view/sun_collector_overflow.py`
(output beside it).

| Display (`W`) | `F = 0x3470` | `0x4000` | `0x3b6f` (N 100 remapped) | `0x471c` |
| --- | --- | --- | --- | --- |
| 1920×1080 (1.3333) | 4.30e9 | 3.22e9 | 3.60e9 | 2.70e9 |
| 2560×1080 (1.7778) | 3.22e9 | 2.42e9 | 2.70e9 | **2.03e9** |
| 5120×1440 (2.6667) | **2.147e9** (2^31 − ~1.1e4) | **1.61e9** | **1.80e9** | **1.35e9** |

Bold: below `2^31`, i.e. reachable by an `int32` `z`. The run309 boundary (27.5–32.5° at `0x471c`,
5120×1440) puts the sun at `D ≈ 1.52–1.60e9` units in that sector [i]; at `0x4000` the same sun would
stay just below `z_crit = 1.61e9`, which fits its presence in the vanilla runs.

**Other readers ruled out.** The frustum test `0x004c6aa0` returns 1 at once for node flags
`0x20c80000` (`0x004c6ad0`), which includes the TSuns bit [s]; the sun body (31, `v\00031`) never
appears in run309's cull census on the capture frames with the sun on screen, present (frame 7910,
34°) or absent (8710, 12.2°), so the `D·F/0x4000` size cull (`0x0047d1ce`) does not gate it [m]; the
occlusion probe `0x00488720` rebuilds the direction from the same `F` and plane consistently, and the
candidate walk `0x00488a70` only prunes more with a larger `F` [s]; the lens camera's `+0x298` is not
touched by the lens code (`0x004714c0..0x00472600`) and keeps the allocator's `0x4000`
(`0x00488d69`) [s].

**Soundness at `0x3470`.** The test is sound while `W·tan(F/2) ≤ 2`, because then `z_crit ≥ 2^31`.
At 32:9 `0x3470` gives `W·tan = 2.00001`: formally reachable only for `z > 2^31 − ~1.1e4`, sound in
practice. Vanilla `0x4000` already fails on 32:9 for suns beyond 1.61e9 (a pre-existing engine bug at
ultra-wide aspects). Under the §7.3 remap `W·tan(F'/2) = 0.75·W·tan(N/2)`: on 32:9 every `N ≤ 90` is
safe and `N > 90` is reachable (`N = 100`: 1.80e9); at 21:9 and 16:9 the whole menu range is safe.
Fix: §9.1 (a saturating stub at `0x0047e391`, or the one-byte `JGE → JAE` at `0x0047e398`).

### 9.1 Fix design for the horizontal-bound overflow (static, not built)

Script: `verification/results/field-of-view/sun_collector_fix.py` (output `sun_collector_fix.txt`):
site bytes, branch scan, stub assembly, and an emulation of the gate for the fixture vectors [m/i].

**Site [m].** The second `FixMul` and the compare (instruction boundaries as listed, qword = aligned
8-byte word):

```
0047e37f 8b 44 24 18        MOV EAX,[ESP+0x18]      ; W (16.16)
0047e383 8b 54 24 10        MOV EDX,[ESP+0x10]      ; t1 = FixMul(tan, z/2), < 2^31 while tan(F/2) < 2
0047e387 f7 ea              IMUL EDX                ; EDX:EAX = W*t1 (64-bit, exact)
0047e389 05 00 80 00 00     ADD EAX,0x8000
0047e38e 83 d2 00           ADC EDX,0
0047e391 0f ac d0 10        SHRD EAX,EDX,0x10       ; qword 0x0047e390 -- the low 32 bits of the bound
0047e395 3b c8              CMP ECX,EAX             ; ECX = |x|/2
0047e397 0f 8d 19 02 00 00  JGE 0x0047e5b6          ; off-screen
0047e39d                    (y test follows)
```

No byte-pattern branch and no absolute reference lands in `0x0047e365..0x0047e39c`; the gate window
`0x0047e315..0x0047e402` branches only to `0x0047e315`, `0x0047e354/356`, `0x0047e3b9/3bb` and
`0x0047e5b6` [m]. Live at `0x0047e391`: `EDX:EAX` (the product, the input of `SHRD`), `ECX` (`|x|/2`,
read by the `CMP`), `EBX` node, `ESI` lens record, `EDI` view, and the `ESP`-relative locals
`[ESP+0x10..0x1c]` (`[ESP+0x14]` = H is read by the y test). EFLAGS are dead there (`ADC`'s flags are
overwritten by `SHRD`).

**Key property [s].** `|x|/2 ≤ 2^30`, so whenever `W·t1 >> 16 ≥ 2^31` the true bound exceeds every
int32 `|x|/2`: an overflow always means "inside horizontally". Saturating the bound to `INT32_MAX`
(design a) and skipping the test on overflow (design b) therefore give the same, exact answer, and
neither changes the branch for any non-overflowing input.

**(a) Saturating stub, recommended.** `engine_patch::claim` with
`{"lens_collector_x_bound", 0x0047e391, {0f ac d0 10 3b c8}, length 6, ret_pop 0, rel32_offset 0}`
(two whole instructions, no relative branch; first five bytes in the qword `0x0047e390`, one
`lock cmpxchg8b`; byte `0x0047e396` is left and never executed), then `push_front` of:

```
81 fa 00 80 00 00   CMP EDX,0x8000        ; (EDX:EAX)>>16 >= 2^31 ?  (signed: a negative product is left alone)
7c 0a               JL  +10
ba ff 7f 00 00      MOV EDX,0x7fff
b8 ff ff ff ff      MOV EAX,0xffffffff    ; the displaced SHRD then yields 0x7fffffff
ff 25 <slot>        JMP [continuation]    ; tail: SHRD EAX,EDX,16; CMP ECX,EAX; JMP 0x0047e397
```

It touches only `EAX`/`EDX` (the values it corrects) and dead flags, pushes nothing (the `ESP`
locals are unchanged), calls nothing (no LastError, FPU or alignment concern), and the `CMP` that feeds
the original `JGE` still runs in the tail; `JMP` preserves its flags. 20 bytes + slot, per lens source
per view per frame: negligible.

**(b) Skip on overflow.** By the key property it is (a) with a different encoding; in the chain model
a stub cannot jump past the tail without bypassing later stubs, so (b) is best expressed as (a). No
separate design is needed.

**One-byte alternative (no stub).** `0x0047e398` `8d → 83` turns `JGE` into `JAE`; `ECX ≥ 0`, and the
low 32 bits read unsigned are exact while `W·t1 >> 16 < 2^32`, i.e. `W·tan(F/2) < 4` for any int32 `z`
(32:9: `F < 112.6°`; 48:9: `F < 90°`). Single aligned byte, trivially atomic and reversible, but
it has an aspect limit that (a) does not have.

**Hazards [s].**
- *Callers.* The gate sits inside the render visit `0x0047d9c0` (callers `0x0047e5e5`, `0x0047e600`
  recursion, `0x0047eb32`, `0x0047eb63`) and runs only for nodes with `+0x12c & 0x20000000` (TSuns
  lens sources, `0x0047e129`) in views with `+0x270 & 0x100` (`0x0047e139`). The fix changes the
  result only on overflow frames, for every such view.
- *Vertical test* (`0x0047e3ca..0x0047e3fc`): `H·t1` with `H = 0.75` (≤ 1 for narrow displays) cannot
  reach `2^31` while `tan(F/2) < 2`. The first `FixMul` (`t1`) itself overflows only for
  `tan(F/2) ≥ 2` (`F ≥ 126.9°`, script cameras only). Neither is covered or needed for the menu range.
- *Downstream of a now-visible record.* Position (`FixDiv(x,z)`, `MulDiv` by `cot`) and size
  (`0x0047e402..0x0047e4be`) do not scale with `W·tan·z`; the occlusion probe `0x00488720` rebuilds the
  direction from the record position and `W·tan` without `z`. The probe now runs for centred suns at
  large `F`, at the same cost as for a centred sun at `0x3470`.
- *Install window.* A claim before the first Present, as all `engine_patch` sites; rollback restores
  the six bytes.

**Fixture on real image pages** (pattern of `collide_memo_fixture.cpp`: a fixture image with a
zero-filled section over `0x00400000..`, engine bytes copied to their own addresses from an untracked
`*_inc.h` extracted from the installed EXE). Copy the gate window `0x0047e315..0x0047e402`, write
landing pads at `0x0047e402` ("on") and `0x0047e5b6` ("off") that restore the saved `ESP` and return a
code, and drive it from a naked thunk that builds the locals (`[ESP+0x14] = H = 0xc000`,
`[ESP+0x18] = W`, `[ESP+0x1c] = tan16`), sets `EBX` to a fake node (`+0xa0` r = 1000, `+0xf0` x,
`+0xf4` y, `+0xf8` z) and jumps to `0x0047e315`. Run each vector unpatched, then after the production
claim, then after rollback; check the landing pad, `ECX`, `EBX/ESI/EDI/EBP`, `ESP`, and that the six
bytes are restored. Expected (emulated [i]; `W = 174762` for 5120×1440, `tan16 = round(tan(F/2)·65536)`):

| Case | W, F, z | x, y | vanilla | (a) / JAE |
| --- | --- | --- | --- | --- |
| A run309 | 32:9, `0x471c`, 1.5e9 | 0, 0 | off (`0x0047e5b6`) | on (`0x0047e402`) |
| B below `z_crit` | 32:9, `0x471c`, 1.2e9 | 0, 0 | on | on |
| C vanilla bug | 32:9, `0x4000`, 1.7e9 | 0, 0 | off | on |
| D off left | 32:9, `0x471c`, 5e8 | −1.05·z·W·tan, 0 | off | off |
| E inside edge | 32:9, `0x471c`, 5e8 | 0.99·z·W·tan, 0 | on | on |
| E2 largest x | 32:9, `0x471c`, 1.5e9 | 2^31−1, 0 | off | on |
| F off top | 32:9, `0x471c`, 1.5e9 | 0, z | off (x) | off (y) |
| G no overflow | 16:9, `0x3470`, 2.1e9 | 0, 0 | on | on |
| H at the bound | 32:9, `0x3470` (tan16 49152), 2147483000 | 0, 0 | on | on |

Case H shows the `0x3470` bound depends on the LUT's 16.16 tan: with `tan16 = 49152` exactly,
`W·tan = 1.99999` and the bound is not reached; a LUT value one ulp larger would reach it only for
`z` within about 1e4 of `2^31` [i].

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
python3 verification/results/field-of-view/menu_chase_path.py      # §7, §8 (capstone, KC walk)
python3 verification/results/field-of-view/sun_collector_overflow.py  # §9
python3 verification/results/field-of-view/sun_collector_fix.py       # §9.1
```

## Open

- The page title of menu class `0x8d3` and the two `Input` keys that jump to
  `SG_MIN_FOV`/`SG_MAX_FOV`; whether class `0x96` statics (member `0x16`) are
  saved with a game (nothing re-issues `INS_SetFocus` at load either way, §7.1).
- The `INS_SetFocus` remap site (§7.3) is not built or fixture-tested.
- The sun's camera-space `z` is inferred from the run309 angle boundary
  (§9), not read; a one-row log of the TSuns node `+0xf8` would confirm it.
- The lens collector fix (§9.1) is designed, not built; its fixture is specified, not written.
- The call order of `0x00402780` (proxy install at `Direct3DCreate9`) relative
  to `0x00403840` (first registry creation); the one-off data write covers
  either order.
- The env-map camera's own `+0x298` writer.
- No runtime read yet: one `camera+0x298` / `registry+0x24` log at the first
  flight with the patch would confirm §3 (`0x4000` vanilla, `F_user` patched,
  `F_user·0x10000/zoom` in a zoomed cockpit view).
