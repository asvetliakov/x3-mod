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

- The actual values of `LODrec_i[+0x34]` were not read. The loader that writes
  that field was not located (the `BOB1`/`BODY` tags are not compared as
  immediates anywhere in the image, so the parser uses a byte compare this study
  did not chase), and `node+0xa0` is not in any capture, so `T_i` cannot be
  converted to absolute units and the truncation headroom of option 1/2 is
  unbounded from below. This is the one measurement that would decide between
  option 1 and option 3.
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
