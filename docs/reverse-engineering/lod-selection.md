# LOD selection: the threshold scale, its settings, and how to move it

Read-only study, 2026-09-15. Installed X3AP.exe SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
preferred VAs, base `0x00400000`. Ghidra 12.1.3 headless on a freshly re-imported
`/tmp/x3-ghidra-research/X3Render` project (the previous project's `.gpr`,
`project.prp` and database files had been removed by the system's `/tmp` cleaner),
plus bounded joins of the existing run-36/39/47/48/49 capture logs and one
archive text-page read. No game launch, no Wine command; the bottle was read only.
Raw decompiler and instruction output stays in `/tmp/x3-lod/` (untracked).

This note answers the question "what is the cheapest safe way to push the engine's
LOD switch distances out by 2–3×". It extends the selection contract already
tabulated in [station-material-distance.md](station-material-distance.md),
"LOD and subset at range", which established the metric `s = r·640/D`, the
threshold loop `0047d429..0047d46e` and the side effects at `0047d4d7`.

## The multiplier at `*(0x606f34)+0x760`

`0x00606f34` holds a pointer to the global video-configuration structure
(824 code references to the global in the image). A program-wide scan of every
instruction carrying displacement `0x760` with a register base finds **four**
sites and no others:

| site | function | instruction |
| --- | --- | --- |
| `004d97c9` | `004d8f10` | `FSTP float ptr [EAX+0x760]` (writes `1.0`) |
| `004d9a23` | `004d8f10` | `FSTP float ptr [EDI+0x760]` |
| `004d9a82` | `004d8f10` | `FSTP float ptr [EDI+0x760]` |
| `0047d44b` | `0047cfe0` | `FMUL float ptr [ECX+0x760]` |

So the float is **written only by the backend/device bring-up function `004d8f10`
and read only by the LOD threshold loop.** Nothing else in the image consumes it:
not texture quality, not particle counts, not far-plane or distance culling, not
the fog block in `004c0150`. Scaling it changes the LOD ladder and nothing else.

`004d8f10` is reached from exactly one call, `004db058` in `004dac90` (the
window/device creation path of [ghidra-render-map.md](ghidra-render-map.md)). It
first stores the constant `1.0` (`004d97c9`), then, after the pixel-shader profile
probe (`D3DXGetPixelShaderProfile`, four `00469700` capability checks), selects the
final value from the **shader-quality** field `*(0x606f34)+0x754`:

| `+0x754` | value written to `+0x760` | source |
| --- | --- | --- |
| `>= 2` | `1.0` | immediate `0x3f800000`, `004d9a82` via `004d9a5x` |
| `== 1` | `1.15` | `float [0x005656f0]` |
| `== 0`, ps_1_1 path | `1.4` | `float [0x005656f4]` |
| `== 0`, ps_1_1 fallback | `1.3` | `float [0x005656f8]` |
| `== 0`, third fallback | `1.2` | `float [0x005656fc]` |

The observed range is therefore `[1.0, 1.4]`, default `1.0` on any ps_2_0/ps_3_0
adapter. `004b6be0` (built-in defaults) sets `+0x754 = 2` at `004b6cbe`.

**Direction.** In `0047d429..0047d46e` the loop walks `i = LODcount-1` down to `1`,
computes `T_i = (int)((float)LODrec_i[+0x34] * f)` and takes the first `i` with
`s < T_i`; `s = r·640/D` falls with range. A **larger** `f` therefore selects a
**coarser** LOD nearer the camera — consistent with the mapping above, where weaker
shader hardware gets `1.2…1.4`. The switch distance is `D_i = r·640/T_i ∝ 1/f`, so
**keeping LOD 2 two to three times farther means `f ≈ 0.5 … 0.333`**, a value the
engine never produces and no setting can reach.

## What the settings actually expose

`004b6f60` opens `HKCU\Software\EGOSOFT\%s` (format string `0x00562bb8`, `%s` = the
module name) with `RegOpenKeyExA` (`0x00532010`) and reads values with
`RegQueryValueExA` (`0x0053200c`); `004b7b40` writes the same set back. The fields
that matter here:

| registry value | config field | read | written |
| --- | --- | --- | --- |
| `VideoViewDistance` (`0x00562c18`) | `+0x768` | `004b711e` | `004b7cea` |
| `VideoTextureQuality` (`0x00562c2c`) | `+0x74c` | `004b7153` | `004b7d18` |
| `VideoShaderQuality` (`0x00562c40`) | `+0x754` | `004b7189` | `004b7d47` |
| `VideoAntialiasMode` (`0x00562c54`) | `+0x758` | `004b71bf` | `004b7b40`, past the dumped window |
| `VideoFilterMode` (`0x00562c68`) | `+0x75c` | `004b71f4` | `004b7b40`, past the dumped window |

The same fields are driven by the game's native Win32 **"Graphics Settings"**
dialog (code at `0x004cc160…0x004cf1xx`, `GetDlgItem` `0x00532270` /
`SendDlgItemMessageA` `0x005322a0`; Ghidra creates no function for the handler
bodies, which are reached through a dialog-procedure pointer). `004cc250`
populates combo control **`0x4EC`** with text ids `10000..10003` of page `1912`
and pre-selects `+0x768` when it lies in `[0,3]`, else index 2. Reading
`t/0001-l044.pck` page 1912 from the archives names them exactly:

- entries `10000..10003` = **Low / Medium / High / Very High**. The page's one
  view-distance label is id `1259`, **"&View Distance"**; the dialog resource was
  not parsed, so the label-to-control binding is by name, while the
  control-to-field binding (`0x4EC` ↔ `+0x768`) is proved by the code.
- id `1247` is **"&Shader Quality"**; its control is `0x4DD` (see the OK-path map
  below), populated by `004cc3c0`, which pre-selects from `+0x754` at `004cc451`.
- the page also carries `110` "Graphics Settings", `1248` "&Texture Quality",
  `1068` "Automatic &Quality Control".

The OK path reads `CB_GETCURSEL (0x147)` of one control per step and stores the
**previous** step's result, so the control-to-field map has to be read off the
interleaving: `0x4EC` → `+0x768` (store `004ccf17`), `0x434` → `+0x74c`
(`004ccf5e`), `0x4DD` → `+0x754` (`004ccf7e`), `0x3EE` → `+0x758` (`004ccf9e`),
`0x3EC` → `+0x75c` (`004ccfbe`). The populate side agrees: `004cc250` fills
`0x4EC` from `+0x768`, `004cc2f0` fills `0x434`, `004cc3c0` reads `+0x754`,
`004cc610` reads `+0x75c`. A second writer of `+0x768` exists
at `00497ce3`, an entry of the 31-way jump table at `0x00498054` dispatched from
`00497b8d` (registered into a callback table by `00496ec0`); it takes its operand
from a byte stream at `[arg+1]`, and `00497cb3` is the matching reader. Both the
dialog and that handler set `*(0x606f34)+0xfc |= 0x800000` when the stored value
is `<= 0` and clear it otherwise.

**So the only user-facing LOD control is "View Distance", and raising it does not
scale the thresholds.** Its four values act as follows, all inside `0047cfe0`
unless noted:

| `+0x768` | effect |
| --- | --- |
| `<= 0` (Low) | sets `cfg+0xfc & 0x800000`, which enables the adaptive detail controller `00496f80`: it steps `cfg+0x748` in ±5 between 0 and 100 against the frame-time target `cfg+0x738`, and `0047d321..0047d35e` then rescales small metrics (`s < 0x20`) by `(cfg+0x748 + 10)/110` — i.e. only ever coarser. With the bit clear, `00497004` pins `+0x748 = 100` (identity). |
| `== 2` (High) | `0047d174..0047d187` raises the node's **distance-cull** limit to at least `0x05f5e100` (100,000,000) before the `CMP EAX,ECX / JBE` at `0047d18e` clears the renderable bit `+0x12c & 2`; `0047eaf5` and `004c2b6f` apply the same clamp on the fog far distance. |
| `>= 3` (Very High) | same clamps raised to `0x1dcd6500` (500,000,000) at `0047d15a..0047d16d` / `0047eacf..0047eaee`; and `0047d48b..0047d499` subtracts 1 from the selected LOD index — **one step finer, globally**. |
| `> 3` | `0047d4a6..0047d4af` forces LOD `0` for every node. Not reachable from the dialog (it clamps display to `[0,3]` and stores a `CB_GETCURSEL` index), but reachable by writing `VideoViewDistance = 4` into the registry. `00403840` sets `+0x768 = 4` and `+0x748 = 0x64` around a scoped render and restores both (`00403f20`/`00403f54`). |

"Very High" therefore already buys one LOD step, and a registry-only `4` buys
"always LOD 0" — all-or-nothing, with the 500 M far plane attached. Neither is the
requested 2–3× ladder shift. Raising "Shader Quality" cannot help either: its
maximum is the one that yields `f = 1.0`.

## Uniformity and the one exception

`0047cfe0` recurses over `node+0xc` (`0047d528..0047d546`) and applies the same
global `f` to `LODrec_i[+0x34]` of whatever model the node resolves to
(`node+0x140` → `004863c0`). The scale is therefore **uniform across every node and
every model** that takes the metric branch.

The exception is the alternative branch `0047d36d..0047d3fb` (taken when
`[ESP+0x18] != 0`, the propagated third argument; per the earlier study this is the
parentless `node+0x12c & 0x80000000` case). It selects the LOD from fixed distance
constants `0x6acfc0` / `0xec82e0` / `0x1406f40` / `0x1b2e020` and **never reads
`+0x760`**. Any global scale leaves those nodes where they are, so a large factor
will put them out of step with their neighbours. The station bodies of runs 36/39
are not in that branch: their measured LOD 2/3 boundary (`D ∈ (118 833, 129 090]`
for model `5427`) matches neither constant.

Downstream, the selected index `node+0x14c` is read by the submission and mesh
paths (`004c0150` at `004c34ea`/`004c3b3d`, `004c53d0`, `004c55d0`, `004c5830`,
`0047d9c0`) and by `0047d4d7..0047d51e` for the renderable bit. The small-object
measure `ESI = r·W/D` used for the `+0x1d8`/`+0x1dc` culling at
`0047d258..0047d2cf` is a **separate** quantity and is not affected by `+0x760`.
No particle or effect system reads `+0x760`; there is no shared "effect LOD"
threshold behind this float.

## Cost of a 2–3× ladder shift

Because the whole ladder scales together, an object at distance `D` after the
change draws what an object at `D/k` draws today. The per-object step cost is the
ratio between adjacent LOD levels, joined from the capture logs on
`(frame, index)` between the `object_context` (model, lod) and `draw`
(`primitives`) records:

| model | LOD 3 | LOD 2 | ratio | seen in |
| --- | --- | --- | --- | --- |
| `542a` Argon station (the photographed port body) | 1 draw / **1 842** tri | 15 draws / **11 538** tri | **6.3× tri, 15× draws** | run49 / run36 |
| `5427` | 1 / 2 592 | 14 / 10 967 | 4.2× | run36 |
| `5436` | 1 / 3 002 | 13 / 11 921 | 4.0× | run39 / run36 |
| `543f` | 1 / 3 439 | 15 / 16 191 | 4.7× | run49 / run36 |
| `542b` | 1 / 2 517 | 15 / 18 544 | 7.4× | run48 |
| `546b` | 3 / 1 207 | 24 / 17 060 | 14.1× | run48 |

Ships behave the same way one level up:

| model | coarse | fine | ratio |
| --- | --- | --- | --- |
| `4fef` (the run-19 ship) | LOD 3: 1 / 82 | LOD 0: 1 / 1 712 | 20.9× |
| `53ab` | LOD 3: 1 / 467 | LOD 0: 17 / 16 011 | 34.3× |
| `5411` | LOD 1: 20 / 54 709 | LOD 0: 35 / 107 397 | 2.0× |
| `546d` | LOD 1: 18 / 77 128 | LOD 0: 25 / 152 900 | 2.0× |
| `5530` | LOD 1: 19 / 44 395 | LOD 0: 20 / 90 006 | 2.0× |

So a factor of 2–3 costs roughly **4–7× the triangles and 13–15× the draw calls
per distant station body**, and about 2× per large ship that crosses the 0/1 step.
The draw-call multiplication is the larger risk: it also multiplies the proxy's
per-draw motion/material work. How many bodies are simultaneously beyond their
current boundary in a busy sector was not measured; that needs a scene census, not
another disassembly.

## Implementation options, ranked

`src/proxy/engine_memory.h` is a **read-only** bounded reader (no write entry
point); `src/proxy/engine_patch.h` provides byte-verified code patching
(`verify_bytes`, `write_code`, `claim`, `restore`) with an install window that
closes at the first `Present`. Nothing in the tree writes engine data today.

The exact bytes of the read site, for every option below (17 bytes,
`0047d440..0047d450`):

```
0047d440  8b 03                 MOV   EAX,[EBX]
0047d442  db 40 34              FILD  dword [EAX+0x34]
0047d445  8b 0d 34 6f 60 00     MOV   ECX,[0x00606f34]
0047d44b  d8 89 60 07 00 00     FMUL  dword [ECX+0x760]
```

### 1. Replace the one instruction at `0047d44b` with an absolute-address FMUL (recommended)

`D8 89 60 07 00 00` → `D8 0D <disp32>`, where `disp32` is the address of a
proxy-owned `float`. Both encodings are **exactly 6 bytes**, so no instruction
boundary moves and no trampoline, arena or dispatcher is needed; `0047d451` stays
the `CALL 0x0052b5d0` ftol. `FMUL m32fp` touches no general register and no
EFLAGS (the x87 status word is written identically by both encodings), so
register and flag liveness are unchanged by construction: `ECX` is loaded by the
preceding instruction, becomes dead, and is reloaded on the next iteration.
Reentrancy is not a concern — the instruction is a pure load-multiply and the
site is re-entered recursively per node with no state of ours.

- Validation: `verify_bytes(0x0047d440, …, 17)` against the sequence above before
  writing; refuse on any mismatch (this also pins the FILD and the global load, so
  a different build cannot be patched by accident).
- Write: `VirtualProtect` the page, `write_code(0x0047d44b, …, 6)`. The span
  `0x47d44b..0x47d451` crosses the aligned qword at `0x47d450`, so `write_code`
  takes the plain-copy path; that is acceptable only inside the existing install
  window (backend load, before the device exists), because `0047cfe0` runs only
  while rendering. `FlushInstructionCache` afterwards.
- Rollback: restore the original 6 bytes. The multiplier itself needs no rollback.
- Fail-closed: mismatched bytes, `VirtualProtect` failure, or a configured factor
  outside a sane band (see below) ⇒ do not patch, log, run vanilla.
- **Constraint:** the `disp32` points into the proxy DLL's data. The patch must be
  restored before the DLL could be unloaded, or the module pinned; a `d3d9` proxy
  is not normally unloaded, but the restore path must exist.
- Windows parity: documented Win32 only (`VirtualProtect`,
  `FlushInstructionCache`); nothing Wine-specific.

### 2. Write the float into the live config struct each frame

Read `*(0x00606f34)` through `engine_memory::read`, validate, and store the scaled
float at `base+0x760`. No code is modified at all, so this is the option with the
smallest blast radius and it is trivially reversible (store `1.0` back).

- The game rewrites the field only in `004d8f10`, i.e. on the `004dac90`
  device/window bring-up path — not per frame and not from `Reset` as far as this
  study established. A once-per-`Present` store (one aligned 4-byte write, which
  cannot tear on x86 against the render thread's aligned 4-byte read) makes the
  re-write harmless without having to detect it.
- Validation before the first store: pointer non-null and readable; `base+0x768`
  in `[0,4]`; `base+0x760` currently holds one of `{1.0, 1.15, 1.2, 1.3, 1.4}`
  (a strong, cheap identity check for this build). Fail-closed on any of these.
- Cost: needs a small bounded **write** helper next to `engine_memory::read`
  (VirtualQuery-validated span, `PAGE_READWRITE`/`PAGE_EXECUTE_READWRITE`, no
  new locking). That helper does not exist today and is the only new machinery.
- LastError: the store path calls `VirtualQuery` on a cache miss, so it must
  save/restore `GetLastError()` the way `object_context` already does.
- Weakness relative to option 1: it depends on the config struct layout at
  runtime rather than on verified code bytes, and it shares a field the game
  owns.

### 3. Scale the metric `s` instead of the thresholds

`T_i` is computed with `FILD` on an **integer** `LODrec_i[+0x34]` and truncated by
the `0052b5d0` ftol, so a factor below 1 quantises the ladder: a record with
`LODrec[+0x34] = 2` becomes `T = 0` at `f = 0.4` and that level is then never
selectable (`s` is clamped to `>= 1`, so `s < 0` and `s < 1` are both false),
collapsing the node to LOD 0. The guard at `0047d3ea` (`LODrec[+0x34] < 2` steps
one level back) shows the engine expects such small values to exist in real
assets. Multiplying `s` up by an integer factor avoids the problem entirely and
preserves the ladder's ordering exactly.

The site is `0047d42f`, seven bytes, two `MOV`s, no relative branch:

```
0047d42f  8b 4c 24 14   MOV ECX,[ESP+0x14]
0047d433  8b 51 0c      MOV EDX,[ECX+0xc]
```

It qualifies for `engine_patch::claim` (length 7 >= 5, whole instructions,
`rel32_offset = 0`). Because the claim installs a `jmp` and not a `call`, `ESP` is
unchanged inside the stub, so `[ESP+0x2c]` (the metric) is directly addressable.
Liveness at that point: `EAX` is dead (it is loaded at `0047d440`, and the site is
reached only when `EBP > 0`), `ECX` and `EDX` are written by the displaced
instructions, and EFLAGS is dead (the next flag consumer is the `CMP` at
`0047d456`). The stub must preserve `EBX`, `EBP`, `ESI`, `EDI`, `ESP`, the x87
stack (empty here — `FILD` pushes at `0047d442` and the ftol pops) and
`GetLastError`. It runs per node per frame on the render thread and must be
lock-free. Saturate the product: `s` can legitimately be `0x7000000` when
`D < 640`, so `3 × s` must be clamped below `INT_MAX`.

This is the most precise option and the only one that is exact under the integer
truncation, but it is also the only one that adds a hot-path stub.

### 4. No-code option, for completeness

`VideoViewDistance = 3` ("Very High" in the dialog) already biases every LOD one
step finer and raises the far plane to 500 M; `= 4`, registry-only, forces LOD 0
everywhere. Neither is a 2–3× ladder shift and both change fog/cull distance as a
side effect, but "Very High" is the correct baseline to set before measuring
anything else.

## Unknown

- ~~The actual values of `LODrec_i[+0x34]` were not read.~~ **Answered
  2026-09-22 from the assets** ([merged-lod-feasibility.md](../architecture/merged-lod-feasibility.md)
  §2): a `.pbb` body is `BOB1 / MAT6 / BODY <u16 LOD count> … POIN/PART per LOD`
  with the switch value as a big-endian `u32` between consecutive LOD blocks.
  `objects/stations/station_scenes/others/argon_L_solarpowerplant.pbb`: 5 LODs,
  thresholds 250 / 150 / 80 / 30.
  `objects/stations/x3ap/others/xtc_teladi_eqd_ring1b.pbb`: 4 LODs, 50 / 24 / 13.
  Values that small do exist, so option 1/2's truncation headroom is real:
  `f = 0.333` turns 13 into 4 and 2 into 0. `node+0xa0` is in the cull census
  (`radius=`) since run131, so `T_i` can now be converted to absolute units for
  a censused node. The loader that writes the field was still not located.
- Whether `004dac90` (and hence the `+0x760` write) re-runs on a resolution change
  or a device `Reset`, as opposed to only at startup.
- Which menu entry opens the "Graphics Settings" dialog, and whether the
  `0x00498054` jump-table handler that also writes `+0x768` is reachable from a
  savegame script or only from internal code.
- How many bodies in a busy sector sit beyond their current boundary, i.e. the
  frame-level cost of the change as opposed to the per-body cost tabulated above.
- No hook, patch or write proposed here has been implemented or measured.

## Reproduce

```sh
mkdir -p /tmp/x3-ghidra-research
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/Cellar/ghidra/12.1.3/libexec/support/analyzeHeadless \
  /tmp/x3-ghidra-research X3Render \
  -import "$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe" \
  -analysisTimeoutPerFile 900
```

then `-process X3AP.exe -noanalysis -postScript` with the scan/dump scripts kept
in the session scratchpad (a displacement scan over `0x760`/`0x768`/`0x754` and a
byte/disassembly/xref dumper); the capture join is a 20-line reduction over
`object_context` and `draw` records keyed on `(frame, index)`.

## Cull census sites (`--cull-census`, 2026-09-18)

Read-only telemetry on the same pass, so the projected-size lever of
[engine-frame-time.md](../architecture/engine-frame-time.md) 2.3 can be sized
from the engine's own numbers instead of the world-scale proxy of the run124
census. Objdump of the installed EXE (`fdbf3418…`), function `0047cfe0..0047d551`
(373 instructions, `ret 8` at `0047d54f`). Code: `src/proxy/cull_census.{h,cpp}`,
`src/proxy/cull_census_core.h`; verifier `verification/probe/verify_cull_census_sites.py`;
fixture `verification/probe/cull_census_fixture.cpp`; summariser
`tools/analysis/cull_census.py`; ledger [cull-census.md](../verification/cull-census.md).

**Why not the 7-byte site `0047d42f`.** It sits inside the threshold loop's
branch, which only nodes that survived the small-object cull and resolved a
model reach (`[ESP+0x18] == 0`, `EBP > 0`); the culled nodes, the ones the
lever is about, never execute it, and the selected index is not final there.
Two sites see every evaluated node instead:

| site | bytes | displaced | reached from | dead at the site |
| --- | --- | --- | --- | --- |
| **measure** `0047d258` | `8b 87 dc 01 00 00` = `mov eax,[edi+0x1dc]` | one instruction, 6 bytes | `0047d231` (jmp), `0047d24e` (jne), fall-through; nothing branches into `0047d259..0047d25d` | EAX (written by the displaced load), ECX (`mov ecx,0x180000` at `0047d260`), EDX (never read before `0047d3e4`/`0047d472`, and the cdecl call at `0047d2fe` clobbers it), EFLAGS (`test eax,eax` at `0047d25e` writes them first) |
| **exit** `0047d528` | `8b 7f 0c 83 3f 00` = `mov edi,[edi+0xc]; cmp dword [edi],0` | two instructions, 6 bytes | `0047d085`, `0047d0a4`, `0047d0ea`, `0047d112`, `0047d1a2`, `0047d1af`, `0047d2e7`, `0047d51c` and fall-through; nothing branches into `0047d529..0047d52d` | EFLAGS (the displaced `cmp` regenerates them for the `je 0047d548` at `0047d52e`); EAX/ECX/EDX are saved anyway because they reach the caller on a leaf's return path (the callers `0047e7a5` and `0047d53c` ignore EAX) |

Live at the measure site and read by the stub: `EDI` = node, `ESI` = the
small-object measure `r·W/D` (`0047d218`, or `0x7000000`), `[ESP+0x2c]` = the
LOD metric `s = r·640/D` (`0047d24a`/`0047d250`, or `0x7000000`), `[ESP+0x10]` =
`D` after the `camera+0x298/0x4000` scale, `[ESP+0x28]` = the view. The stub
also reads node `+0x140` (model id, the key of `object_context model=`),
`+0xa0`, `+0x12c`, `+0x1d8`, `+0x1dc` and `parent+0x1d8` through `+0x18`: every
one dereferenced by the pass itself on the same node before the site
(`0047d08b`, `0047d19b`, `0047d1af`, `0047d258`, `0047d2a2..0047d2af`). The exit
stub reads `+0x12c` (renderable bit 2, final) and `+0x14c` (the LOD index after
the `0047d48b..0047d4d1` adjustments, final). The x87 stack is empty at both
sites (`fld`/`fstp` at `0047d0f2`/`0047d0fa` and `fild`/ftol at `0047d442`/`0047d451`
are balanced). Both stubs are `cmp byte [enabled],0; je continue` outside a
capture frame and otherwise push `EAX/ECX/EDX`, call an integer-only cdecl
handler (`-mno-sse -mfpmath=387`, no Win32 call, LastError untouched) and pop;
`engine_patch::claim` provides the tails and the atomic five-byte writes, and a
failed second claim restores the first.

**Recording.** Ring of 8,192 entries committed once at install; per node the
measure handler stores node, model, view, `s`, measure, `D`, radius, `+0x1dc`,
`+0x1d8`, the effective limit `max(+0x1d8, parent+0x1d8)` and `+0x12c`; the exit
handler completes the last entry with the final `+0x12c` and `+0x14c`. A node
that exits without a measure entry (rejected before `0047d1b5`: behind the eye,
frustum, distance, hidden latch) is counted as `unmeasured`; a node measured
when the ring is full is counted as `overflow`. Rows at Present, capture frames
only: `cull_census_frame … entries= overflow= unmeasured= exited=` and one
`cull_census … s= measure= d= radius= thr_1dc= thr_1d8= limit= flags_in=
flags_out= lod= verdict=` per entry, `verdict` ∈ `kept` (bit 2 survives),
`culled_size` (`measure < limit > 0`), `culled_min` (`measure < 1` without
`0x4000000`), `culled_other` (cleared later: the env-map view's `< 20` test or
the last-LOD fade), `no_exit`. Pixels: `px = s · m00 · width / 1280` (`s` is the
projected radius at a 640-wide reference; the fixture and summariser use the
frame's projection `m00` and the rt0 width).

**Verified.** The CPU fixture re-implements the pass with the two windows
byte-exact and proves the patched copy leaves every node with the same
renderable bit, LOD and flags as the unpatched copy over a 12-node tree in the
main, env-map and view-distance-4 cases, returns the same EAX/ECX/EDX/EFLAGS,
preserves EBX/ESI/EDI/EBP/ESP and the empty x87 stack, records the expected
rows (order, values, verdicts), counts two early exits as unmeasured, keeps
8,192 of 8,201 nodes with `overflow=9`, records nothing when disarmed,
preserves LastError, restores both sites exactly and refuses changed window
bytes and the closed window. Cost in the fixture harness: 0.234 µs per
12-node pass native, 0.244 disarmed, 0.311 armed (Wine/FEX, not game FPS).

## Cull small parts site (`--cull-small-parts <px>`, 2026-09-18)

The projected-size lever of [engine-frame-time.md](../architecture/engine-frame-time.md)
2.3, sized by the run131 census (frame 4991 of the run117 station view, 901
draws / 32 ms, 878 of them attributed to census nodes: nodes under 2 px are 403 draws, under 4 px 458, under 8 px 479,
and every surviving tiny node has `+0x1d8 = +0x1dc = 0`). Code:
`src/proxy/cull_small_parts.{h,cpp}`, `src/proxy/cull_small_parts_core.h`;
verifier `verification/probe/verify_cull_small_parts_site.py`; fixture
`verification/probe/cull_small_parts_fixture.cpp` replaying the tracked rows
`verification/fixtures/run131-cull-census-rows.json`; ledger
[cull-small-parts.md](../verification/cull-small-parts.md).

**Site.** Neither census site can carry the lever: at `0047d258` the limit is
not computed yet, at `0047d528` the verdict is final. The compare that consumes
the limit (`0047d2bb..0047d2c1`) is four rel8 branches in eight bytes, which
`engine_patch` cannot displace, so the stub sits one instruction earlier:

| site | bytes | displaced | reached from | dead at the site |
| --- | --- | --- | --- | --- |
| **limit** `0047d2a2` | `8b 4f 18 85 c9` = `mov ecx,[edi+0x18]; test ecx,ecx` | two instructions, 5 bytes; the `je 0047d2ad` after the next instruction consumes the displaced `test`, which the tail re-executes | `0047d28c` (je), `0047d297` (jge), fall-through; nothing branches into `0047d2a3..0047d2a6` | EAX (written at `0047d2a7`), ECX (written by the displaced load), EFLAGS (regenerated by the tail's `test`, overwritten by the cull's `and`); EDX untouched; x87 empty |

The verified window is `0047d294..0047d2cc` (56 bytes), which pins the
env-map zeroing, the whole `max(+0x1d8, parent+0x1d8)` computation, the size
compare and the engine's own cull instruction `0047d2c3 83 a7 2c 01 00 00 fd`
(`and dword [edi+0x12c],~2`, then `eb 05` to `0047d2d1`). The claim
`0047d2a2..0047d2a6` is disjoint from the census's `0047d258`/`0047d528` and
the lod_scale's `0047d44b`; the fixture installs the census and this stub on
the same synthetic pass and both report correctly.

**Stub** (64 bytes, no call, no Win32, no floating point): `cmp dword
[threshold],0; jle continue` (the disarmed cost), then `push eax; mov
eax,[threshold]; cmp [esp+0x30],eax; pop eax; jge continue` on the pass's own
`s = r·640/D` at `[ESP+0x2c]` (ESP unchanged: the site is a `jmp`, not a
`call`); below the threshold it replays `0047d2a2..0047d2b9` so EAX/ECX arrive
as the engine leaves them (both dead there), increments a per-frame count and
jumps to `0047d2c3`. The node therefore takes exactly the path of a node whose
measure is under a positive limit: renderable bit cleared, then the
`0x4000000` test at `0047d2d1` and, for nodes carrying that flag, the LOD
selection with the bit already clear. `s` rather than ESI is compared because
ESI is zeroed for the env-map view at `0047d299` and `s` is what the census
bucketed.

**Consequences beyond the node.** Culling a node clears bit 2 of `+0x12c`, and `0047d055..0047d076` culls a child carrying flag `0x40000` whose parent's bit 2 is clear, so a culled part can take descendants with it: the 403 / 458 / 479 figures (of the 878 census-attributed draws; 901 in the frame) are lower bounds and popping can cascade. The threshold applies in every view, so small casters also leave the shadow and env maps, and the one main-view `m00` scales every view.

**Units.** The census summariser buckets `px = s · m00 · width / 1280`, so the
per-frame threshold is the smallest integer `t` with `t · m00 · width / 1280 >=
px`; `s < t` is exactly the summariser's class (m00 is the float bits
`3f4ccccc`, not 0.8: at 1280 wide, 2 px -> 3, 4 px -> 6, 8 px -> 11). `m00` is
read once per frame from the engine's projection buffer through the
`camera_state` latch (P[0], valid perspective only; an invalid read leaves the
frame vanilla with threshold 0), the width from the back buffer at
CreateDevice/Reset. The engine's measure in this session was `r·640/D` as
well (`[0x608518]+0x5c = 640`), so the rows replay exactly. Culling by `s`
alone treats a node as a sphere of its radius `+0xa0`. Correction (the earlier
sentence here had it inverted): taking `+0xa0` as the bounding radius, a long
thin part such as an antenna has a *large* radius for its visible area, so it
is kept until its whole length is under the threshold, not culled early; the
parts that go first are the compact ones (clamps, lamps, small pods), and the
visible risk is the loss of their glow while it still reads as a few pixels.
The engine's own `+0x1d8` cull has the same sphere bias. That `+0xa0` bounds
the mesh was not measured separately.

**Scope** (`X3M_CULL_SMALL_PARTS_SCOPE`, `--cull-small-parts-scope all|bodies`,
default `all` since 2026-09-19; an absent or empty variable is `all` too).
`bodies` culls only nodes without a parent link
(`[node+0x18] == 0`), the test the displaced `mov ecx,[edi+0x18]; test ecx,ecx`
already performs; `all` is the behaviour described above. The scope is fixed at
install and selects the emitted stub, so there is no per-node scope read: the
`bodies` stub keeps the 64-byte layout and replaces bytes 27..46 with `jne
continue; mov eax,[edi+0x1d8]; jmp cull` (int3 padding). A parented node below
the threshold leaves through the same `jmp [next]` as every kept node: the tail
re-executes the displaced `mov`/`test`, so ECX and EFLAGS reach `0047d2a7`
exactly as native and EAX is not written; a parentless one arrives at
`0047d2c3` with ECX = 0 and EAX = `+0x1d8`, as the engine's own `je` path
leaves them. Cost: one extra branch, only on nodes already below the threshold.
An unknown scope value fails closed (`reason=invalid_scope`, nothing patched).
Rationale (user, 2026-09-18): a whole station of 2 px is invisible anyway,
while the glowing sub-parts of a nearer station are a few px and visible; the
review of the run131 replay found the 2 px class to be mostly whole distant
objects (89 of 97 nodes body-flagged `0x1000000`/`0x8000000`, 395 of 403
draws). Run 43 B flew `bodies` against `all` and settled it the other way: at
2 px `bodies` culled only 36 nodes per frame and saved nothing (nearly every
small node has a parent), while `all` took the busy view from 884 to 477 draws
and ~30 to ~42 fps with no visible pop-in, so `all` is the default and 2 px the
launcher default (docs/verification/cull-small-parts.md). The census rows carry no parent
link, so the 89 / 395 (4 px: 120 / 450) figures are the fixture's parent
assignment (proven parent when `limit > +0x1d8`, otherwise no body flag), not a
measured parentless set; census rows now record `+0x18` for the verdict and
print `scope=` on `culled_small` rows.

**Further consequences (second review, 2026-09-18).** With
`--shadow-caster-retention` a culled static caster keeps casting: the retention
store replays it although the node left the pass. The script occluder list
built at `0x00488aef` / `0x004886a0` is the one non-render consumer of the
renderable bit and loses culled nodes. The `m00` latch follows a zoom with one
frame of lag (read at frame begin, from the previous frame's last activated
view) and its `cull_small_parts_value` log line is capped at 16 per session.
`camera_state::reset()` is called only from the motion-output Reset path, so it
is unreachable with only this option on; `cull_small_parts::after_reset`
disarms the threshold until the next `begin_frame` re-reads the live buffer.

**Verified.** Site verifier 19/19 on the installed EXE (18/18 before the scope's
`encoder_bodies` check; an earlier "16/16" here was stale); CPU fixture 113
checks with the scope cases (78 before). Earlier record: CPU fixture 78 checks
(native fidelity of all 1,214 rows, 403 / 458 / 479 draws flip at 2 / 4 / 8 px
and nothing else changes, census + stub together, registers/ESP/x87/LastError,
rollback, refusals). Cost in the fixture harness: 0.235 µs per 12-node pass
native, 0.244 disarmed, 0.237 armed with 7 of 12 culled (Wine/FEX, not game FPS).
