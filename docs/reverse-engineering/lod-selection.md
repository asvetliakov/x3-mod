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
engine never produces and no setting can reach. The loop's choice is not the final
index: a shared tail adds `+1` in the env-map view or `-1` at View Distance Very High
(this bottle's setting) and clamps; see "What the selection really does, end to end"
below.

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
will put them out of step with their neighbours. Only race-0x12 (Terran) TDocks/TFactories
roots carry the bit (readers, writers, persistence and patch sites: "Terran stations and bit 31
of `node+0x12c`" below, 2026-09-24). The station bodies of runs 36/39
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

## What the selection really does, end to end (2026-09-23)

Question: why run250 drew `argon_trading_station_partB` (model `0x53a0`, file
thresholds 30/20/10/30 for records 1..4) at LOD 3 although record 4 is tested
first and has the larger threshold, and why the `argon_livingsection` (`0x53ab`,
30/15/5/30) LOD 3 row has record 3's 467 faces and not record 4's 202. Source:
objdump of the installed EXE (`fdbf3418…`), `0047cfe0..0047d551` and the BODY
loader `004823c7..004824b8`; the X3 bottle's `user.reg` (read only); asset
ladders from `tools/analysis/bob1.py info`/`audit`. Raw disassembly stays in
`/tmp/x3-lod/`. No game launch, no Wine.

### 1. The loop `0047d429..0047d46e`

| site | instruction | meaning |
| --- | --- | --- |
| `0047d321`, `0047d32a` | `movsx ebp,word [ebx+0x10]`; `sub ebp,1` | `EBP = n-1`, `n` = LOD count |
| `0047d362` | `cmp byte [esp+0x18],0; je 0047d429` | non-zero flag → distance branch (below); zero → the loop |
| `0047d429..0047d42d` | `test ebp,ebp; mov esi,ebp; jle 0047d472` | `n <= 1`: no loop at all |
| `0047d42f..0047d436` | `ebx = &model+0x0c[n-1]` | cursor starts at the **last** record |
| `0047d440..0047d451` | `fild [rec+0x34]; fmul [cfg+0x760]; call 0052b5d0` | `T_i = trunc(rec_i+0x34 · f)` (ftol truncates) |
| `0047d456..0047d45a` | `cmp [esp+0x2c],eax; jl 0047d468` | **signed, strict** `s < T_i` |
| `0047d45c..0047d464` | `sub esi,1; sub ebx,4; test esi,esi; jg 0047d440` | walk down; stops **before** record 0 (never compared) |
| `0047d468` | `mov [edi+0x14c],esi` | first hit wins, i.e. the highest `i` with `s < T_i` |
| no hit | — | `+0x14c` keeps the `0` stored on entry at `0047d001` |

`s` lives in `[esp+0x2c]`, the stack slot of the pass's second argument (`ret 8`;
the argument's flag byte was copied to the local `[esp+0x18]` at `0047d007`).
It is overwritten at `0047d24a` with `0x00469a30(r = node+0xa0, 640, D)` (the
`r·640/D` metric), set to `1` when that returns `0` (`0047d250`) and to
`0x7000000` when `D < 640` (`0047d229`), and rescaled at `0047d35e` by
`(cfg+0x748 + 10)/110` when `cfg+0xfc & 0x800000` and `s < 32`. Because
`s >= 1`, a record with `T_i <= 1` is never hit.

**Distance branch** (`[esp+0x18] != 0`, `0047d36d..0047d427`): the index comes
from the four distance constants, is clamped to `[0, n-1]`, then steps one finer
if `rec_k+0x34 < 2` (`0047d3ea`) or if `rec_k+0x00 < rec_{k-1}+0x00 / 3`
(`0047d40a..0047d421`; `+0x00` is the record's point count). It then joins the
common tail.

### 2. The common tail `0047d472..0047d51e` and what is *not* read

Both paths end in the same adjustment, applied to whatever the loop or the
distance branch chose:

| site | condition | effect on `+0x14c` |
| --- | --- | --- |
| `0047d472..0047d489` | `view+0x270 & 0x1000000` (the env-map view of the census note) | `+1`, and the next test is skipped |
| `0047d48b..0047d499` | otherwise `cfg+0x768 >= 3` (View Distance "Very High") | **`-1`** |
| `0047d4a0..0047d4af` | `cfg+0x768 > 3` | `= 0` |
| `0047d4b9..0047d4d1` | always | clamp to `[0, n-1]` |
| `0047d4d7..0047d502` | final `> 0`, final `== n-1` and `node+0x12c & 0x8000` | renderable bit cleared (hide at the final coarsest index) |
| `0047d519..0047d51e` | final `>= 3` | `node+0x130 |= 0x100000` |

The pass reads nothing else from a LOD record: only `+0x34` in the loop, `+0x34`
and `+0x00` in the distance branch. The file's per-LOD `u32` flags word is stored
at record `+0x30` (`004824b8`) and never read here; group/subset counts are not
read either. Records are stored in file order (`model+0x0c[i]` written at
`0048243b` for the loop counter `[esp+0x3c]`; `+0x34` = file value for `i >= 1`
at `0048248b`, `100000` for record 0 at `004824a3`). Measured over the 15
non-monotonic bodies (`bob1.py info`): every record of a body carries the same
flags value (`0x0`; `0x40` on both `argon_gate` copies), the last one included.
So neither the flags word, nor a group count, nor the node flag `0x8000` makes
the last record a distinct kind; `0x8000` only hides a node whose *final* index
is `n-1`.

**The bottle runs at Very High.** `HKCU\Software\EGOSOFT\X3AP` in the X3 bottle's
`user.reg` holds `VideoViewDistance = 3` (key time 2026-09-12 13:18 UTC, before
run250), and `004b711e` loads it into `cfg+0x768`. So every main-view LOD in
the captures since then is the loop's choice **minus one**. This is inferred
from the registry, not read in process: no proxy log line reports `cfg+0x768`.

### 3. The corrected rule and the two observations

```
sel   = highest i in 1..n-1 with s < trunc(T_i · f), else 0
adj   = +1 in a view with view+0x270 & 0x1000000; else -1 if cfg+0x768 >= 3; else 0
final = 0 if cfg+0x768 > 3, else clamp(sel + adj, 0, n-1)
```

In the main view at Very High the final index is therefore at most `n-2`: **the
last record of every multi-LOD body is never drawn there, the `0x8000` hide never
fires there, and a two-LOD body always draws LOD 0.** The last record is drawn in
the main view only at View Distance Low..High, and in the `0x1000000` views.

- `0x53a0` partB, f = 2: the loop's reachable set is `{0, 4}` (record 4, `T = 60`,
  is tested first; records 3, 2, 1 have `T` = 20, 40, 60, none larger than 60).
  So `s < 60` → `sel = 4` → `-1` → **LOD 3**, and `s >= 60` → LOD 0. Records 3 and 4
  of this body are the same mesh (3 035 points, 1 275 faces, 29 groups each,
  measured), so the drawn geometry is also what record 4 would give. No
  contradiction.
- `0x53ab` livingsection: the same `{0, 4}` → final `{0, 3}`; the LOD 3 row is
  `sel = 4` shifted to **record 3** (1 079 points, 467 faces), which is why it has
  467 primitives. Record 4 (303 points, 202 faces) is never drawn in the main
  view at Very High.
- Consequence for `verification/results/run250-draws/stand_bodies.py`: its
  `s_bound` column assumes `adj = 0`. At Very High a drawn LOD `k >= 1` means
  `sel = k+1`, i.e. `trunc(T_{k+2} f) <= s < trunc(T_{k+1} f)` for a monotone
  ladder, and LOD 0 means `s >= trunc(T_2 f)`. Example: `argon_TL` (30/15/5) at
  LOD 1 is `10 <= s < 30`, not `30 <= s < 60`; the `argon_spacedock` LOD 1 row is
  `160 <= s < 300`. The same one-level offset applies to earlier ladder-to-distance
  conversions made from main-view captures in this bottle.

### 4. "Shadowed" records and the 15 non-monotonic bodies

A record is **never drawn in a case** when it is not in that case's drawable set:
with `R` = `{0}` ∪ `{i >= 1 : trunc(T_i f) >= 2 and trunc(T_i f) > trunc(T_j f) for all j > i}`,
the main view draws `R` at View Distance Low..High, `{max(0, r-1) : r ∈ R}` at
Very High, `{0}` at 4; a `0x1000000` view draws `{min(r+1, n-1)}`. The old audit
term ("a later record has threshold >= T_i") is the complement of `R` and is
correct only for Low..High. Measured over the 950 installed multi-LOD bodies
(`verification/results/bob1-format/lod_drawn_sets.py`, output beside it, f = 2):

| quantity | bodies |
| --- | --- |
| last record never drawn in the main view at Very High | **950** (all) |
| only LOD 0 drawable in the main view at Very High | 75 |
| some record drawn in no main-view setting | 12 (11 of them non-monotonic) |
| coarsest *drawable* record has > 1 group, Very High / Low..High | 581 / 551 |

Of the 15 non-monotonic bodies, the 11 with the `x/y/z/30` shape (both
`argon_gate` copies, `asteroid_B_ClassMine`, `argon_dock_center`,
`argon_livingsection`, `argon_goner_temple`, and the argon partB, boron, split,
teladi partA/partB trading stations) are genuinely defective: records 1 and 2
are drawn in no main-view setting, and the body pops from LOD 0 straight to
record 3 (Very High) or 4 (Low..High) at `s < 30·f`, the distance where LOD 1
should start. The other four are not: the two wrecks (15/15), the split wreck
(15/150) and `argon_trading_station_partA` (30/15/5/5) lose a record only at
Low..High; at Very High the `-1` makes it drawable. In 5 of the 11 the last
record is an exact copy of record 3 (partB, boron, teladi partA/partB; and
`asteroid_B_ClassMine` in points and faces). That fits an authoring habit of
padding the ladder so the finest-to-coarsest range survives the Very High `-1`,
with the pad's threshold copied from `T_1`; this is an inference, the engine
gives the pad no meaning.

**Overlay placement (corrected 2026-09-23 after Run 68 B).** The engine has no
"far record" semantic, but at Very High the *position* matters: record `k` is
drawn exactly when record `k+1` is the loop's first hit, so the record at index
`n-1` is never drawn in the main view. The earlier **before-last** default
(coarse record inserted before the last one with `T = T_last`) is therefore wrong
at Very High: it is drawn only for `s < T_last·f` (5 px for the `30/15/5` ship
ladders, 15 for `military_outpost_middleb`), never at the run240 stand where the
heavy bodies sit at `s` 21–95 (`verification/results/run255-census/`, measured).
The same census confirms the rule on every multi-LOD row with a finite `s`
(`verification/results/lod-overlay-pilot/pilot_check.py`).

**Compact placement (default since 2026-09-23, after
[lod-child-hide.md](lod-child-hide.md) §4).** `tools/analysis/lod_overlay.py`
writes `[record 0, C:T_1, pad:T_pad]`: record 0 unchanged (byte for byte), the
collapsed coarse record `C` at index 1 with the original record 1's threshold
`T_1`, and a pad copy of `C` at index 2 with `T_pad`. The original records
1..n-1 are dropped. At Very High `s < T_pad·f` hits the pad and the `-1` draws
`C`; `s >= T_pad·f` falls through `C`'s `T_1` (below `T_pad`) to record 0. At
Low..High the pad (same mesh) draws below `T_pad·f` and record 0 above. `T_pad`
comes from `--threshold T` or `NAME=T`, must be `>= 2` and not below `T_1` (the
tool refuses otherwise unless `--force-threshold`, since `T_pad·f <= s < T_1·f`
would then draw `C` at Low..High); a single-LOD body gets `[T_0, C:T_pad,
pad:T_pad]`. Why:

- The engine sets `node+0x130 |= 0x100000` at a final index `>= 3` (`0047d51e`),
  which switches the node's materials from `BUMPMAP` to the `DEFAULT` technique
  and drops a texture slot ([lod-child-hide.md](lod-child-hide.md) §4). Under the
  pad placement below, `C` sat at index 4 (ships) or 3 (outpost) and got the
  flag; whether `DEFAULT` samples the light map is untraced, so the glow groups
  could go dark. In the compact ladder the final index is at most 2 at every
  setting, so the index rule never sets the flag. The other sets (projected size
  against `node+0x1dc` at `0047d26b`, `| 0x180000` below 20 px or in a
  `0x1000000` view at `0047d27c`/`0047d28e`) are unchanged.
- Once `T_pad` exceeds every original threshold, the original records 1..n-1 are
  never the first hit from the top, so they are unreachable in the main view at
  every setting (below). Keeping them only costs file size and load time.

The collision mesh is built from the last record, now the pad, which has the
original coarsest record's points and faces (only the grouping differs), so
collision is unchanged. The `0x1000000` env-map view (`+1`) draws record 1, now
`C`, wherever `s >= T_pad·f`, where it drew the original record 1 before.
Hide-at-coarsest (`0x8000`, final index `n-1` = 2) fires below `T_pad·f` at
Low..High, as with pad, and never at Very High.

**Pad placement** (the default before compact, still `--placement pad`): append
the collapsed coarse record `C` with `T_last`, then a pad copy of `C` with
`T_pad`, giving `[T_0 … T_last, C:T_last, pad:T_pad]` with the original records
untouched. Walking from the end, `s < T_pad·f` hits the pad first and the Very
High `-1` draws `C`; `s >= T_pad·f` falls through every original threshold (all
below `T_pad`) and `C`'s own to no hit, i.e. record 0. At Low..High the pad, the
same mesh, is drawn below `T_pad·f`. `T_pad` must exceed every original
threshold of records 1..n-1 (the tool refuses otherwise unless
`--force-threshold`) and be `>= 2`. A single-LOD body gets `[T_0, C:T_pad,
pad:T_pad]`.

Consequence, intended: LOD 1..n-1 of the original ladder become unreachable in
the main view at every setting. None of their thresholds exceeds `T_pad`, so none
is ever the first hit from the top; the body jumps from LOD 0 straight to `C` at
`T_pad`. (The `0x1000000` env-map view's `+1` and the distance branch can still
pick them.) For the pilot ships those records carry 23–32 draws each and for the
outpost 30–33 (measured, `bob1.py info`), which is what the collapsed `C`
replaces. Hide-at-coarsest (`0x8000`, final index `n-1`) still never fires at
Very High; at Low..High it now fires below `T_pad·f` for flagged nodes.

`C`'s grouping is `--collapse glow` (the default since 2026-09-23). Per part,
materials whose light map is mostly bright keep their own group: these carry
the engine glows. Everything else collapses onto the dominant opaque material,
plus one alpha group for materials that alpha-test or alpha-blend
(`g_AlphaTestEnable`/`g_AlphaBlendEnable`). An alpha texture alone does not
count. Result: 5 draws for the pilot ships and 3 for the outpost.
`--collapse two` (opaque + alpha, the first pilot) and `one` stay selectable.
The light-map census, the alpha rule's evidence and the per-rule draw counts are
in [merged-lod-feasibility.md](../architecture/merged-lod-feasibility.md),
"Overlay tooling".
MAT3 bodies are refused unless `--force-mat3`: the loader gives the coarsest
record of such a body with more than 3 LODs material `0x485`, which would be the
pad.

Two node-set side effects change at Very High (objdump of `0047cfe0..`,
`/tmp/x3-lod/f47cfe0.s`). A child node flagged `node+0x12c & 0x40000` is hidden
when its parent (`node+0x18`) is not renderable or has `+0x14c > 0`
(`0047d055..0047d076`); with the pad placement the parent's final index is `> 0`
below `T_pad` (before: below 15 px for these ladders), so attached children of
the pilot bodies disappear at the stand and their draws count in the saving
(same with compact: `C` is index 1). And a final index `>= 3` sets
`node+0x130 |= 0x100000` (`0047d519`, the `BUMPMAP` → `DEFAULT` switch): with pad
`C` sits at index 4 (ships) or 3 (outpost), so the flag is set at Very High where
these bodies never reached index 3 before; compact avoids it (above).

Follow-up: [lod-child-hide.md](lod-child-hide.md) (2026-09-23): no EXE code sets `0x40000`, no flagged node in runs 255/257; `0x100000` selects the `DEFAULT` technique.

The earlier layouts remain selectable for the record (`--placement before-last`,
`--placement append-pad`, the latter `C:T`, `pad:T-1`, with `T < T_last` on a
multi-LOD body). The former `--keep-coarsest-hidden` option stays removed.

Open: the `0x1000000` view is identified only by the census note's env-map
reading; the other writers of an `+0x14c` field found by a program-wide scan
(`0041fb42`, `0042c47c`, `004c1cbf`, …) were not checked for being render nodes;
why the run250 partB node shows 26 draws / 1 211 primitives against record 3's
29 groups / 1 275 faces was not examined.

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
flags_out= lod= verdict= [scope=] lods= thr=` per entry (the ladder fields
below), `verdict` ∈ `kept` (bit 2 survives),
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

**LOD ladder fields (2026-09-22).** Each row now ends in
` lods=<n|-> thr=<t0,t1,…|->`: the model's LOD count (signed word
`model+0x10`, the `movsx ebp,word [ebx+0x10]` at `0047d321`) and
`LODrec_i+0x34` for `i < min(lods, 8)` (`model+0x0c` is the record-pointer
array, `0047d433`/`0047d440`/`0047d442`). `t0` is record 0's value, which the
loop `0047d440..0047d464` never compares; `t1` is the LOD 0 → 1 switch value.
`0047d321` is not a census site, so the model pointer is taken at the exit
site. The 39-byte exit stub also pushes EBX, `[ESP+0x14]` and `[ESP+0x10]`.
The handler keeps EBX only when it is non-zero, equals `[ESP+0x14]` and
differs from `[ESP+0x10]` (`core::exit_model_pointer`). This rests on the writer
sets of the installed bytes, which the verifier pins. Between the two sites
EBX is written only at `0047d2f6` (`xor ebx,ebx`, negative model id),
`0047d303` (the `0x004863c0` result) and by the loop cursor at
`0047d436`/`0047d45f`, which `0047d46e` restores from `[ESP+0x14]`. That slot's
only writer is `0047d30a`. `[ESP+0x10]` (D) is written only at `0047d1c8`/`0047d1eb`,
where EBX gets the same value. Between the sites ESP is written only by the
balanced `push eax`/`add esp,4` around the `0x004863c0` call
(`0047d2fd`/`0047d305`) and the alignment no-op `lea esp,[esp+0x0]` at
`0047d439`. So a node culled at
`0047d2e7` (`culled_size`/`culled_min` without `0x4000000`) exits with EBX == D,
is refused, and carries `lods=- thr=-`. So do nodes with no model (EBX 0) and
nodes in the env-map view zeroed below 20. A kept node, or one cleared by the
last-LOD fade, carries its ladder. A model pointer that happens to equal D is
dropped, never misread. The ladder is read at Present, not in the pass,
through `engine_memory::read` (committed-readable span check). Present already
saves and restores LastError. One read set per distinct model per captured
frame: the model header (8 bytes), then up to 8 record pointers, then each
record's `+0x34`. A count ≤ 0, a null array, a null or unreadable record, or an
unreadable header yields the count (or `-`) with `thr=-`, never a fault. The
disarmed path is unchanged: `cmp byte [enabled],0; je` → `jmp [next]`. Verifier
additions: `ladder_pattern_bytes`, `ladder_pattern_whole_instructions`,
`ebx_writers_between_sites`, `esp_writers_between_sites` and `slot_writers`;
21/21 on the installed EXE. The CPU fixture's synthetic pass now carries the
model in EBX/`[ESP+0x14]` with the engine's layout. It adds a three-record and
a four-record ladder, a null model, a count-0 model, a record 0 on a
`PAGE_NOACCESS` page, and direct hostile exit arguments. It has been built but
not yet run under Wine. Report: `tools/analysis/draw_accounting.py <run> --ladder`.

**Body name field (2026-09-23).** After `thr=` each row ends in
` body=<name|->`: the name of the row's model id (`model=`, node `+0x140` ==
model `+0x08`) from the engine's body table
([body-format-bob1.md](body-format-bob1.md) §6). The model itself carries no
name. At a captured frame's Present, through `engine_memory::read`: once per
frame the manager `g = *(0x00608518)` and its 12-byte header `g+0xb4` (fixed
count, must be 11000), `g+0xb8` (dynamic count, `0 ≤ n < 1 000 000`), `g+0xbc`
(slot array, non-null); then once per distinct id the slot's name pointer at
`slots + slot*0x1c + 0x0c` (id → slot as `0x0046df60`: `id < 1000` → id,
`9000..19999` → `id − 9000`, `≥ 20000` → `11000 + id − 20000`, `1000..8999` and
`slot ≥ fixed + dynamic` refused) and up to 256 bytes of the name, read in
page-bounded chunks up to the NUL. A null name pointer prints `v\%05d`, the
census's own label: `0x0046df60` itself picks `v\%05d` or `%d` for a null name by
a caller flag (`0x0046dfd6`), so the engine has no single name for that case. Printed names are cut at 63 characters, and every byte outside
`0x21..0x7e` is shown as `?`. `body=-` covers no manager, a wrong header, an
invalid id, any unreadable span and a name with no NUL in 256 bytes. The header
and names are re-read every captured frame, because `0x0046e400`, `0x0046dc20` and
`0x0046ee20` reallocate the slot array and a game load re-binds the ids. The
cache is direct-mapped by id (256 entries) and cleared per captured frame. No
work is added to the off path or to uncaptured frames. The global is the
constant `0x00608518` in production; only the fixture build
(`-DX3M_CULL_CENSUS_FIXTURE`) has the `set_body_table_global()` seam. The layout is pinned
against the installed EXE by `verification/analysis/test_body_table_exe.py`:
24 byte patterns in `0x0046d910`, `0x0046df60` and `0x0046e400` encode the
global, the offsets `+0xb4/+0xb8/+0xbc`, the 11000 count, the id bounds with
their signed branches (`jge` at `0046df76`/`0046df82`/`0046dfac`, `jl` at
`0046df9c`), the
stride (`lea r,[slot*8]; sub r,slot; mov r,[base+r*4+0x0c]`) and the name
offset. `draw_accounting.py --ladder` prints the name per model.

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

**Stub** (82 bytes since 2026-09-23, 64 before the projectile test; no call, no Win32, no floating point): `cmp dword
[threshold],0; jle continue` (the disarmed cost), then `push eax; mov
eax,[threshold]; cmp [esp+0x30],eax; pop eax; jge continue` on the pass's own
`s = r·640/D` at `[ESP+0x2c]` (ESP unchanged: the site is a `jmp`, not a
`call`); below the threshold `test dword [edi+0x130],0x20000000; jne exempt`
lets a projectile node through (see "Projectile nodes" below), otherwise it replays `0047d2a2..0047d2b9` so EAX/ECX arrive
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
`bodies` stub keeps the layout and replaces bytes 39..58 (27..46 in the
64-byte stub before the projectile test) with `jne
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

**Verified.** Since the projectile test (2026-09-23): site verifier 21/21, CPU
fixture 132 checks ([ledger](../verification/cull-small-parts.md)). Before it:
site verifier 19/19 on the installed EXE (18/18 before the scope's
`encoder_bodies` check; an earlier "16/16" here was stale); CPU fixture 113
checks with the scope cases (78 before). Earlier record: CPU fixture 78 checks
(native fidelity of all 1,214 rows, 403 / 458 / 479 draws flip at 2 / 4 / 8 px
and nothing else changes, census + stub together, registers/ESP/x87/LastError,
rollback, refusals). Cost in the fixture harness: 0.235 µs per 12-node pass
native, 0.244 disarmed, 0.237 armed with 7 of 12 culled (Wine/FEX, not game FPS).

### Projectile nodes (2026-09-23, Run 75 B)

Run 75 B (run279, 4 px, chase view, fire held) measured the cull removing the
player's bolts: 51–54 bullet nodes per frame (body `v\00517`, model id
`0x205`, radius 784, 58,634 units of travel per frame), 30–33 `culled_small`
(s = 1–3 against threshold 4), 18–20 `culled_min` by the engine's own
measure, 0–3 kept; Run 271 with the cull off drew 33–35
([bolt-footprint ledger](../verification/bolt-footprint.md), Run 75 B). The
model id cannot identify a projectile (mods add bullet bodies), and the
census's `flags_in` (`+0x12c` = `01001002`) is shared with ship parts.

**The marker [s].** The sector-object creation routine (class in `DX`, switch
at `0x004400d8` through the byte table `0x00441d94` / jump table
`0x00441d44`; class numbering as in
[sector-collide.md](sector-collide.md) §12.1) handles class 0 (`TBullets`) at
`0x004400df`: it allocates the `0xa0`-byte payload, takes the body id from
the type record's first word (`0x0044019b`) and stores
`[esp+0x20] = 0x20800000` at `0x004401ae` (`c7 44 24 20 00 00 80 20`; the
local is zeroed for every class at `0x004400b8`). Only the class-0 node path
(`0x004410f0`: `cmp ax,bx; jne` with BX = 0) consumes it: `0x004410f9`
allocates the root node (`0x00486d10`), `0x00441107` sets the body, the
`+0x58` flags of the bullet type pick the mesh builder (`0x00412450`,
`0x0047eef0`, `0x0047eb90`, `0x0047f0a0`; none allocates a child node), and
`0x0044123b..0x00441248` (`mov eax,[ebp+0x70]; mov edx,[esp+0x20]; or
[eax+0x130],edx`) ORs the marker into the root node's `+0x130`. The other
classes leave through `0x0044124a`/`0x00441255` without touching `+0x130`
(the class-7 subtype `0x113` store at `0x00440665` is dead for that reason).
So every bolt, beam and flak object, including types a mod appends to
`TBullets`, is one root node with `+0x130 & 0x20800000`. The engine itself
reads the bit: `0x00488b00 test dword [edi+0x130],0x20000000` keeps such
nodes out of the script occluder list. `0x800000` alone is not specific
(`0x0046b137..0x0046b316` OR `0x10800000` into `+0x130`; `0x0046ce6f` and
`0x0047e9cb` clear it). The marker local is written three times
(`0x004400b8` zero, `0x004401ae`, the dead `0x00440665`) and read once
(`0x0044123e`). No immediate `and` on `+0x130` clears `0x20000000` (the
masks are `~1`, `~2`, `~0x20000`, `~0x40000`, `~0x180000`, `~0x800000`), no
byte access to `+0x131..+0x133` exists, the register setter/clearer pair
`0x004871a0`/`0x004871b0` is reached only with `0x40` (`0x00441cfe`,
`0x00441d0f`, `0x00450758`, `0x00450774`), and the read-modify-write stores
`0x00487353` (`& ~0x180`) and `0x0047a3af` (`& ~0x80 | 0x100`) keep it. The
whole-word stores (node initialisers, the node copy `0x0047a012`, the save
loader `0x004304fe`, the clone `0x0048721d` that keeps bit 0 only, the
scene loader `0x00494ab8` and a few more) were not all traced to their
records (not verified; a store that dropped the bit would only cull that bolt
as before).

**Measured [m].** In run279 the 794 `object_context` rows (35 models, the
object-trace submission rows carry `flags130` per drawn node) show the bit on
model `0x205` nodes only (2 of 2 bullet nodes, `flags130=24980040`,
`flags12c=01001002`), on no other model
(`verification/results/run279-bolts/projectile_flag.py`).

**Missiles** (class 10, `TMissiles`) take the generic path: a fixed dummy
body `0x1b`, the scene loaded from the type (`0x004412a2`), no marker and a
multi-node scene, so no single node word names them at the cull site. They
are not exempt; at a radius far above a bolt's they rarely fall under a few
pixels. Mines and drones are other classes (drones are ships) and are not
exempt either.

**The exemption.** `X3M_CULL_SMALL_PARTS_PROJECTILES=on` (the default;
`--cull-small-parts-projectiles on|off`) emits `test dword
[edi+0x130],0x20000000; jne exempt` at stub offset 22, after the threshold
compare, so only nodes already below the threshold pay one load and one
not-taken branch; `exempt` increments a per-frame count and leaves through
the same `jmp [next]` as a kept node (the tail re-executes the displaced
`mov`/`test`, EAX was restored by the `pop`, no register is written by the
`test`). `off` emits `jmp 34` over the test. The pass itself rewrites `+0x130`
on the same node at `0047cfed`, so the read is of a live field. Both failure
modes restore behaviour that exists today: a missed marker culls the node as
before, a false one runs the vanilla compare. `initialize()` compares the two
marker instructions (`0x004401ae`, 8 bytes; `0x0044123b`, 13 bytes) and
installs the exemption off (`projectiles=marker_mismatch`) when they differ.
Rows: `cull_small_parts_frame ... projectiles=on exempt_bullet=<n>` (the
stub's count, every marked node below the threshold including those the
engine then culls itself) and `cull_census_frame ... culled_small_exempt_bullet=<n>`
(the same class counted from the census ring); an exempt row is never
`culled_small`. Expected cost in the run279 view: about 31 more bullet
instances per firing frame, about 750 more primitives in the bullet draw
(24 faces each, inferred from the body sizes in the bolt-footprint note), no
extra draw call.

## Terran stations and bit 31 of `node+0x12c` (2026-09-24)

Read-only study for the decision whether a DLL patch should take Terran stations out of the
fixed-distance branch so that the merged-LOD overlay reaches them
([merged-lod-feasibility.md](../architecture/merged-lod-feasibility.md), "Slot 06 LOD switch").
Same EXE (SHA-256 `fdbf3418…34f8ab`), Ghidra 12.1.3 headless on `/tmp/x3-ghidra-research/X3Render`
(`-readOnly`), plus a capstone scan of the raw `.text`. No game, no Wine. Scripts and summaries:
`verification/results/lod-terran-bit31/` (`X3Flag12cScan.java` lists every `[reg+0x12c..0x12f]`
access with a 14-instruction trace of register loads; `classify_scan.py` summarises it;
`raw_disp_scan.py` re-finds the sites without Ghidra's code discovery; `dis_site.py` prints bytes;
`kc_bit31.py` checks the KC story code). Raw listings and decompiler output stay in the session
scratchpad. [s] static, [m] measured on files, [i] inferred.

### 1. Readers: one

**Scope [m].** 292 instructions address `+0x12c..+0x12f` with a non-stack base, in 106 functions
(170 writes, 67 direct tests/compares, 53 register loads, 2 LEA). The raw scan finds all 292 and
11 more candidates, all of which are prefix or mid-instruction decodes or unrelated address
arithmetic (`0x00490988`, `0x00490e4d`: `[ecx+ecx*2+0x12c]`). The decompiled text of the 105
named functions has one expression on bit 31 of the field (`0x0047cfe0`) plus the one write in
`0x0043ffa0`; the `(char)… < 0` tests in `0x0047c570`/`0x0047c5b0` are bit 7.

**The reader [s].** `0x0047d012` in the cull/LOD pass `0x0047cfe0`:

```
0047d00b  75 16                         jne  0047d023   ; arg2 (propagated flag) != 0
0047d00d  39 57 18                      cmp  [edi+0x18],edx   ; edx = 0
0047d010  75 11                         jne  0047d023   ; node has a parent
0047d012  f7 87 2c 01 00 00 00 00 00 80 test dword [edi+0x12c],0x80000000
0047d01c  74 05                         je   0047d023
0047d01e  c6 44 24 18 01                mov  byte [esp+0x18],1   ; distance-branch flag
0047d023  8b 87 2c 01 00 00             mov  eax,[edi+0x12c]
```

The bit is read only on a parentless node (`+0x18 == 0`). The flag byte is read at `0x0047d362`
(branch select, `je 0047d429` = metric loop, fall-through `0047d36d` = distance branch) and passed
to every child at `0x0047d530..0x0047d53c`; nothing else in the pass reads it. The top-level
caller `0x0047e780` pushes 0 for that argument (`0x0047e7a0`), so the bit is the **only** way into
the distance branch. Everything the branch changes is the index `node+0x14c`; the common tail,
the culls and the renderable bit are the same code either way.

**Other consumers of the field, none of which tests bit 31 [s]:**

| Site | Function | Use | Effect if the bit is absent |
| --- | --- | --- | --- |
| `0x0047d012` | LOD pass `0x0047cfe0` | bit 31, as above | subtree selects by `s = r·W/D` against `LODrec_i[+0x34]` instead of by `D − R` |
| `0x00443375`, `0x00451d11` | object flag helpers | register masks, resolved: `0x1000000` (`0x00443299`), `0x800` (`0x00451cd0`) | none |
| `0x0047f5c6`, `0x0047f5f5` | node-tree query `0x0047f560` (callers `0x00413f1c`, `0x00453804`) | `& param_2`; callers pass `0x100000`, `0x8104800` or 0 | none |
| `0x00489c46`, `0x00489e2e`, `0x0048a236` | per-node model helpers `0x00489bf0`, `0x00489da0` (attach), `0x0048a1e0` (body load) | mask `0x1000` | none |
| `0x0046cb3e` | instanced batch `0x0046ca80` | whole word into batch record `+0x14`; its readers (`0x0046c170`, `0x0046cef0`) test `3`, `0x800`, `0x4000000` | none |
| `0x00472cf3`, `0x00473132` | recorder `0x00472ab0` (recursive; entered only from `0x00474144` in `0x00473e10`, render option bit 1) | whole-word change detect (dirty `0x200`) and copy | a runtime clear would emit one changed record |
| `0x00478917` | savegame node writer `0x00478690` | whole word into record `+0x194` (then `& ~0x1000`, `0x00478b67`) | the saved word lacks the bit (section 2) |
| `0x00494a29` | B3D native `B3D_InstGetFlags` in `0x00493b40` | whole word to KC | none found: no `0x80000000`/`0x7fffffff` push within 60 bytes of the 16 flag-native calls in `x3story.obj` [m] |

`0x004d8f10`, `0x004da960`, `0x004dab10`, `0x004c60a0` use `+0x12c` of a device-side object
(a COM pointer), not a node. No culling, fade, docking, collision, picking or sound path reads the
bit: fade exemption is `0x02000000` ([distance-fade.md](distance-fade.md)), the size culls use
`+0x1d8`/`+0x1dc` and bit 2.

### 2. Writers and persistence

**Immediate writers [s].** One OR sets bit 31: `0x00441644`. No `AND` immediate clears it (the
15 `AND` masks all keep it; the 88 `OR` immediates other than `0x00441644` do not contain it).
The two `MOV` immediates are the cockpit display node (`0x004217a7`, word = 4) and a device-side
object (`0x004da990`, 0).

**Register-operand writers [s].** All 64 were resolved. Read-modify-writes of the same word that
only touch other bits: `0x0047cfe0` (8 stores), `0x00434620`, `0x0043d1d0`, `0x004596e0`,
`0x0047c570`, `0x0047c5b0`, `0x00488c70`, `0x0041efc0` (the `test bl,bl`/`jns` at `0x0047d0dd`
and the `test al,al`/`jns` in `0x0047c570`/`0x0047c5b0` are bit 7). Constant masks loaded into a register:
`0x100000` (`0x0042cda0`, `0x0042d140`, `0x0043b0b0`, `0x0043b750`, `0x0043c2d0`, `0x0045b130`,
`0x0045f8dc`, `0x00487f50`, `0x0048a350`, `0x004517a0`), `0xffefffff` (`0x0042d28a`,
`0x0045b080`, `0x00488410`), `0x80000` (`0x0043d371`, `0x0045f51a`), `0x1000` (`0x00489bf0`,
`0x00489da0`, `0x0048a1e0`), `8` (`0x0042c200`, three nodes). The constructor's root word `local_140` (`0x004410d2`,
`0x00441235`, `0x0044128c`) is 0, `0x104000`, `0x2000020`, `0xa000020` or `0x20000`. The
subtree setters carry `0x8100000` (`0x00486cd0`, four calls from `0x00414cf0`) and `0x2000000`
(visitor `0x00487190`, one call `0x004418e2`); the other visitor callbacks write `+0x130`,
`+0x13c`, `+0x1c4`, `+0x1c8`, `+0x1d4`. The cockpit display node `0x004216e0` copies only
`src & 0xc00000` (`0x0042181a`). **No path copies the bit into a child**; children follow the
root through the propagated argument only.

**Whole-word and data-driven writers [s].** They can carry the bit but do not create it:

- savegame node load `0x00479d10`: `mov [ebx+0x12c],edx` at `0x0047a005` from record `+0x194`,
  unmasked;
- recorder replay `0x00476140`: `0x00476a16`, value `& ~0x1000`;
- `B3D_InstSetFlags` `0x00494a97`: `node+0x12c = message+6` (the KC callers OR a mask into the
  word from `B3D_InstGetFlags`, [lod-child-hide.md](lod-child-hide.md) §2.1);
- effect elements `0x00414cf0` (`0x00414e9e`, `0x004157a9`, `0x00415964`): `|= element+4`, parsed
  by `0x004ea9d0`, which also accepts a numeric token (a token starting with a digit or `%` goes
  to the number parser `0x004ebe00` instead of the name table). Shipped `Effects.txt` uses names only (lod-child-hide §2.1),
  so the bit is not set by data today; a mod could.

**Persistence [s].** Yes. The savegame writer stores the live word (`0x00478917` → record
`+0x194`), the loader restores it verbatim (`0x0047a005`), and on a save load the station object
does **not** run the constructor: `0x004421a0` → `0x0042fe10` reads the node handle and resolves
it with `0x00486eb0` (object `+0x70`, then `node+0x130 |= 0x80`). So a station loaded from a save
has whatever bit the save carries [s]; the record transport between `+0x194` and the gz stream was
not traced byte by byte [i]. Consequences:

- a constructor patch changes only stations built after it is installed (new game, script
  `SA_AllocObject`, cut-scene instancing); every Terran station in an existing save keeps the bit;
- a runtime clear of the root bit is written into the next save; that save then loads without
  the bit in a vanilla game too, and the clear has to be repeated after every load (after
  `0x0047a720` returns, `0x0040508d` seam in [object-lifetimes.md](object-lifetimes.md));
- a patch of the reader (section 3) needs neither and leaves saves byte-identical.

### 3. Patch sites

**Constructor race test [s].** One block, shared by both classes. The class switch
`jmp [eax*4+0x441db4]` at `0x00441402` with the byte map at `0x00441dec` sends class 5 (TDocks) to
entry 3 `0x00441653` and class 6 (TFactories) to entry 4 `0x004415fe`; no other entry reaches the
block. TDocks jumps in at `0x0044162f` (`eb a9` at `0x00441684`); TFactories falls through from
`0x00441629`.

```
0044162f  0f bf 55 4a                    movsx edx,word [ebp+0x4a]      ; jump target (0x00441684)
00441633  69 d2 b8 0d 00 00              imul  edx,edx,0xdb8
00441639  83 7c 0a 5c 12                 cmp   dword [edx+ecx+0x5c],0x12
0044163e  0f 85 08 05 00 00              jne   00441b4c
00441644  81 88 2c 01 00 00 00 00 00 80  or    dword [eax+0x12c],0x80000000
0044164e  e9 f9 04 00 00                 jmp   00441b4c
```

- `0x0044163e` `0f 85 08 05 00 00` → `e9 09 05 00 00 90` (JMP `0x00441b4c`), or `0x00441644`
  (10 bytes) → NOPs. No reference into `0x00441630..0x0044164d` other than the entry at
  `0x0044162f` and fall-through; `0x0044164e` and `0x00441b4c` are unchanged. The shared tail
  `0x00441b4c` (24 references) starts `mov ecx,[ebp+0x70]; cmp ecx,ebx`, so flags are dead; EAX,
  ECX, EDX are rewritten there or before any read. Game thread, per new object, not reentered.
- Scope: only TDocks/TFactories rows with `+0x5c == 0x12` (37 shipped rows; any mod row with race
  18 too), only newly built objects (section 2).

**Reader [s].** `0x0047d01c` `74 05` → `eb 05` (JMP `0x0047d023`, same length and target). Site
bytes to verify: `f7 87 2c 01 00 00 00 00 00 80 74 05 c6 44 24 18 01` at `0x0047d012`. References
into `0x0047d012..0x0047d022`: none (`0x0047d023` is the target of `0x0047d00b`, `0x0047d010`,
`0x0047d01c`); `0x0047d01e` becomes unreachable. Flags from the `test` are dead: `0x0047d023` is a
`mov`, `0x0047d029` a `test`, before any conditional. No register changes. The pass runs on the
game thread once per node per view; a one-byte opcode store is atomic, and installing at proxy
attach (before the first frame, `VirtualProtect` + `FlushInstructionCache`, same on native
Windows) avoids a racing fetch. No existing DLL site lies in `0x0047cfe0..0x0047d08a` (earliest
referenced is `0x0047d08b`, cull census). Zero per-node cost. It covers constructed and loaded
stations alike, does not touch saved data, and also neutralises the bit from any data-driven
writer (none in shipped data).

### 4. What the branch serves

Only race-0x12 station roots [s]: the sole creator of the bit is `0x00441644`, gates (class 18),
suns (3), planets (4), asteroids (17), ships (7) and other classes get `local_140` words without
it, and `0x0047e780` never passes the flag. Descendants follow the root, including anything
attached under a Terran root (the 16 `argon_newdock_center` rows in the slot-06 triage, inferred
docking ports). A patch in the constructor or the reader therefore changes LOD selection only for
Terran TDocks/TFactories subtrees. Why Egosoft added it is not in the code; the four constants
(7.0M / 15.5M / 21.0M / 28.5M, i.e. 7–28.5 km if the unit is mm) suggest fixed distance bands for
the very large Terran scenes [i].

**What changes for vanilla Terran bodies [i].** They move to the metric loop with their shipped
`+0x34` thresholds, which exist (run273 `usc_small_station_d` 30/15). The triage found 528 of
1,081 Terran census rows where the loop would pick a different record; how much finer or coarser
the vanilla switch becomes, and its draw cost, has not been measured.

**Unknown.** In-process confirmation (census field `branch=[esp+0x18]`) after a patch; whether any
installed mod `Effects.txt` or KC script sets bit 31; the record transport of `+0x194` inside the
save stream.
