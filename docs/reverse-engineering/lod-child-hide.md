# The `0x40000` child hide, the parent LOD index and the `0x100000` detail flag

2026-09-23. Read-only study for the merged-LOD pilot ([lod-selection.md](lod-selection.md),
"Overlay placement"; [merged-lod-feasibility.md](../architecture/merged-lod-feasibility.md)).
Installed `X3AP.exe` SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
preferred VAs, base `0x00400000`. Ghidra 12.1.3 headless (`-readOnly -noanalysis`, the
existing `/tmp/x3-ghidra-research/X3Render` project) with `tools/analysis/X3FunctionContext`,
`X3DecompileFunctions`, `X3XrefsTo`; an `i686-w64-mingw32-objdump -M intel` listing of
the whole `.text`; the decoded `types/*.txt` tables and `L/x3story.obj` from the installed
archives; and read-only queries of the run255 / run257 session logs. No game launch, no Wine,
no build. Raw decompiler output, the listing and extracted game files stay in
`/tmp/x3-lodchild/` and `/tmp/x3-lod/` (untracked). Scripts and small outputs:
[`verification/results/lod-child-hide/`](../../verification/results/lod-child-hide/).

**Answer.** `node+0x14c` is the node's final LOD index of the current view's cull pass, the
same value the draw indexes; for the pilot's coarse record it is 4 (ships) / 3 (outpost). The
`0x40000` gate reads it, but **no code in the EXE sets `0x40000` on a node**: the bit can only
arrive through data (a KC `B3D_InstSetFlags` call, a savegame, a demo replay or a numeric
Effects.txt `i3dflags` value), and none of the 34 k census rows and 12 k drawn-node rows of
runs 255 and 257 carries it. The pilot ships have **no engine effect nodes at all**; their
engine exhaust is hull geometry whose light-mapped exhaust materials the collapsed record `C`
replaces. The engine-glow loss in run257 is therefore not the child hide (inferred, §2.4), and
no engine patch is needed for it. Weapon fire does not read any LOD state. The `0x100000`
flag set at final index >= 3 switches the node's materials from the `BUMPMAP` to the `DEFAULT`
technique and drops the bump texture, render-only.

## 1. `node+0x14c`

### 1.1 Writers

Every `[reg+0x14c]` access in the image was listed (`exe_flag_scan.py`, 54 sites) and mapped
to its function. Render-node writers:

| Site | Function | Value |
| --- | --- | --- |
| `0x00486dd0` | `0x00486d10` node allocator | 0 |
| `0x0047d001` | `0x0047cfe0` cull/LOD pass, entry | 0 (`edx` zeroed at `0x0047cffd`) |
| `0x0047d03e` | same, `node+0x130 & 0x20` early return | 0 |
| `0x0047d37c`..`0x0047d421` | same, distance branch | 0..4, clamped |
| `0x0047d468` | same, threshold loop | first hit from the top |
| `0x0047d482` / `0x0047d499` | same, common tail | `+1` in a `view+0x270 & 0x1000000` view / `-1` at `cfg+0x768 >= 3` |
| `0x0047d4af` | same | 0 when `cfg+0x768 > 3` |
| `0x0047d4d1` | same | **final**: clamp to `[0, n-1]` |
| `0x0042c47c`..`0x0042c50c` | `0x0042c200` (four nodes it creates) | fixed value, with `node+0x12c |= 8` |
| `0x004949fc` | `0x00493b40` native `B3D_InstSetSizeUsed` (case `0x27`) | script value, with `node+0x12c |= 8` |
| `0x0047a06d` / `0x00476c1f` | savegame node load `0x00479d10` / demo replay `0x00476140` | stored value |

The other `+0x14c` sites are different structures: `0x00452ad0`/`0x004524d0` (a game object,
initialised to 10 000 000), `0x004c1cbf`/`0x004c3b3d` (effect parameter handles),
`0x004c55ae`..`0x004c5d79` (device vtable slot `+0x14c`), `0x004205e0`, `0x00434e40`,
`0x00460630` (camera/fog records, see [sector-fog.md](sector-fog.md)). The structures behind
`0x0041f8d0` (stores 0), `0x00439980` and `0x004304b0` (serialisers) were not identified; none
lies in the weapon range of §3.

### 1.2 Readers on render nodes

| Site | Use |
| --- | --- |
| `0x0047d06b` | the child gate (§2) reads the **parent's** value |
| `0x0047d4d7`..`0x0047d51e` | the pass's own tail: `0x8000` last-record hide, `0x100000` detail flag |
| `0x0047dfe0` | `0x0047d9c0` render visit: `record = model+0x0c[node+0x14c]` for the draw |
| `0x0046cfa4` | `0x0046cef0` instanced-batch path, same indexing |
| `0x004c34ea` | `0x004c0150` per-draw state: the material texture parameter at descriptor `+0x74` is bound only when `node+0x14c == 0` |
| `0x00478972`, `0x00472d4b`/`0x004732e2` | savegame writer `0x00478690`, demo recorder `0x00472ab0` |

### 1.3 The value while the pilot's `C` is drawn

The draw at `0x0047dfe0` indexes the LOD record array with `node+0x14c` directly, so the value
is the index of the drawn record: **4 for the ships, 3 for the outpost**, i.e. the final,
post-adjust index (the loop's first hit is the pad, 5 / 4, and the Very High `-1` gives 4 / 3).
Measured in run257 (`pilot_flag130_out.txt`): every drawn `object_context` row of
`argon_TL`/`M1`/`M2` at the coarse record has `lod=4` (112 rows) and the outpost `lod=3`.

A child sees the same value: `0x0047cfe0` handles a node completely (entry, culls, selection,
tail store at `0x0047d4d1`, flag at `0x0047d51e`) and only then walks its child list
`node+0x0c` at `0x0047d528..0x0047d546`, calling itself per child with the same `(view, flag)`
arguments. So the gate at `0x0047d06b` always compares the parent's final index **of the same
view pass**; in an env-map view (`view+0x270 & 0x1000000`) it is the `+1` value. A parent that
returns early (`0x0047d03e`, `node+0x130 & 0x20`; `0x0047d0af`, `node+0x12c & 0x4000000`)
does not visit its children at all.

Before the pilot the ship ladders (30/15/5, `f = 2`) gave a final index `>= 1` for `s < 30`;
with the pad (`T_pad = 50`) it is 4 for `s < 100`. Any `0x40000` child of these bodies would
therefore be hidden over `s < 100` instead of `s < 30` (inferred from §1.3 of
[lod-selection.md](lod-selection.md); no such child exists in the captures, §2.3).

## 2. The child gate `0x0047d055..0x0047d076`

```
0047d04e  8b 4f 18              mov  ecx,[edi+0x18]        ; parent
0047d051  3b ca                 cmp  ecx,edx               ; edx = 0 (0047cffd)
0047d053  74 27                 je   0047d07c
0047d055  8b 87 2c 01 00 00     mov  eax,[edi+0x12c]
0047d05b  a9 00 00 04 00        test eax,0x40000
0047d060  74 1a                 je   0047d07c
0047d062  f6 81 2c 01 00 00 02  test byte [ecx+0x12c],0x2  ; parent renderable?
0047d069  74 08                 je   0047d073              ; no -> hide
0047d06b  39 91 4c 01 00 00     cmp  [ecx+0x14c],edx       ; parent final LOD vs 0
0047d071  7e 09                 jle  0047d07c              ; LOD 0 -> keep
0047d073  83 e0 fd              and  eax,0xfffffffd        ; hide: clear bit 0x2
0047d076  89 87 2c 01 00 00     mov  [edi+0x12c],eax
0047d07c  8b 9f 2c 01 00 00     mov  ebx,[edi+0x12c]
0047d082  f6 c3 02              test bl,0x2
0047d085  0f 84 9d 04 00 00     je   0047d528              ; hidden: skip to the children walk
```

### 2.1 Who sets `node+0x12c & 0x40000`

The only reader of the bit in the image is `0x0047d05b` (scan of direct tests and of tests on a
register loaded from `+0x12c` within eight instructions). For the writers, every memory write
to `+0x12c..+0x12f` with a non-stack base (150 sites) was listed with its immediate or with the
instruction that loaded the source register:

- **No immediate and no traced constant carries `0x40000`.** The subtree setters `0x00486cd0`
  (`+0x12c |= a`, `+0x130 |= b`, recursive) and `0x00487190` (callback of the tree visitor
  `0x00487150`) are called only with `0x8100000` (`0x0041639b`, `0x004163bc`, `0x004165d1`,
  `0x004165f2`) and `0x2000000` (`0x004418e2`); the scene-part builder `0x0043d1d0`, the
  object builder `0x0043ffa0` (masks `0x104000`, `0x2000020`, `0xa000020`, `0x20000`) and the
  light builders use other bits.
- **Data-driven writes** copy a whole word and could carry it:
  - effect elements, `0x00414cf0` (per-object attached effects, called from the object update
    `0x00416750`) at `0x00414e9e`, `0x004157a9`, `0x00415964`: `node+0x12c |= element+0x04`,
    the element's `i3dflags` parsed by `0x004126d0` from `types/Effects.txt` with the name
    table at `0x00554d60` (`I3DF_2D = 0x4000`, `I3DF_HASLENSFLARE = 0x20000000`, nothing
    else). The installed Effects.txt uses only those two names and `0` (190 / 101 / 25
    elements); no numeric value.
  - the B3D native module `0x00493b40` (registered at `0x0046a2e5`, name table `0x0057a420`),
    case `0x2a` `B3D_InstSetFlags`, `0x00494a97`: `node+0x12c = message+6`. Its KC callers are
    in `addon/04.cat:L/x3story.obj`: 8 `B3D_InstSetFlags` call sites, each after a
    `B3D_InstGetFlags`; the OR masks pushed before them are `0x20`, `0x2000000` (4 sites),
    `0x20000000|0x20000|0x80000`, `0x80000|0x800000` and `0x100000`, and no push of
    `0x40000` lies within 60 bytes of any B3D flag native (`kc_instsetflags_out.txt`; the KC
    opcode meanings `07` push int32, `05` push byte, `54` OR are inferred from the pattern).
    Some sites OR the mask into a value held in a local, so a flag passed in from a caller is
    not excluded.
  - savegame node load `0x0047a005` and demo replay `0x00476a16` restore a stored word.

**Which node types carry it:** none by construction. Engine effect nodes carry
`0x08001002` (measured, §2.3), turret and gun dummies are built by `0x0043d1d0` without it
(gun-type dummies get the per-node size threshold `node+0x1d8 = 10` instead, dock-type 8: `0x0043d692`/`0x0043d6a3`, `0x0043d86e`/`0x0043d87f`),
lights come from `0x004885a0`/`0x0043d1d0` with `0x800000/0x400000/0x10/0x20000/0x80000`.
The bit is reachable only through the KC game logic or a restored state.

### 2.2 Render-only

The gate clears bit `0x2` of `node+0x12c` and nothing else. Bit `0x2` is re-armed for every
node of the tree, per view, by the per-view transform walk `0x0047b800` (`0x0047b84b`, called
from the per-view state build `0x0047bc20`, before the cull pass; also `0x0047190b` in
`0x00471660` for the lens scene). Its readers in the whole image are all render-side:
`0x0047cfe0` (`0x0047d062`, `0x0047d082`, `0x0047d1a8`), the render visit `0x0047d9c0`
(`0x0047d9d0`), the traversal driver `0x0047e920` (`0x0047e98e`), the instanced-batch
admission `0x0046ce20`, the per-draw entry `0x004c0150` (`0x004c01a8`) and the lens-flare
occluder list `0x00488a70` ([lens-flare-visibility.md](lens-flare-visibility.md)). Node
positions and bases (`+0xb0`, `+0xf0`) are computed by `0x0047b800` before the gate and are not
touched by it; the hidden node's children are still visited (`0x0047d085` jumps to the child
walk at `0x0047d528`). A hidden child therefore skips drawing, instanced batching and lens-flare
occlusion for that view and frame, and no update or logic path. Scope of the reader scan:
direct `test` of the bit and `test`/`and` of a register loaded from `+0x12c` within eight
instructions; a bit test through shifts or a copied word would not be found.

### 2.3 Measured: no flagged node in runs 255 and 257

| Log | Cull-census rows (measured nodes) | with `0x40000` | Drawn `object_context` rows | with `0x40000` |
| --- | --- | --- | --- | --- |
| run255 (before the pilot) | 14 811 | 0 | 7 304 (105 nodes) | 0 |
| run257 (pilot installed) | 19 169 | 0 | 4 759 (77 nodes) | 0 |

`flag40000_census.py`, `flag40000_draws.py`. Limitation: the census records a node at
`0x0047d258`, after the gate, so a flagged child of a parent already past LOD 0 is counted only
as `unmeasured`; a flagged child of a LOD 0 parent would appear, and none does. The
stand scene of these runs is one sector.

### 2.4 What the pilot ships' engines are

- Engine flames of fighters and small ships are separate nodes whose body is
  `objects/effects/engines/fx_engine_*` (flags `0x08001002`; 28 distinct bodies in frame 3351 of run255
  and in 3615 of run257, `engine_nodes_out.txt`). **No such body exists for `argon_TL`, `M1` or
  `M2`** (the archives hold 12 argon engine bodies: M3/M3+/M3p variants, M4/M4p, M5, M8, TM,
  TS), and no engine node near the pilot ships appears in either frame.
- Their exhaust is hull geometry: in the coarsest original record the exhaust materials
  (`metal_argon_exhaust_source_*`, `exhaust_trims_*`) cover 24 / 22 / 22 faces of TL / M2 / M1,
  18–21 of them light-mapped (self-illuminated). `C` keeps two materials: M2 `[17, 31]` (no
  exhaust material), TL `[17, 38]` and M1 `[11, 27]` (one exhaust material as the alpha group's
  material, so only the alpha-textured exhaust faces keep an exhaust texture; the opaque ones
  get the plating material) (`exhaust_materials_out.txt`, measured).

So the engine glow that disappears at the coarse record is the hull's light-mapped exhaust,
lost to the two-material collapse, and possibly dimmed further by the `DEFAULT` technique of
§4 (not examined). This is an inference from the asset and census data; the pixels of run257
were not compared.

## 3. Weapons, turrets and targeting

The player fire control `0x00445170` (and the other laser-component users) find the muzzle
without any LOD state:

1. The gun node is found in the ship's scene graph by `0x00442d40`: it walks
   `object+0x70` (the ship's root node) and its children, matching the scene part index
   `node+0x25c` and the body id `node+0x140` against the ship type's gun table
   (`*(0x00606fd4) + type*0xdb8 + 0x19c`).
2. The muzzle offsets come from the **Components** table (`types/Components.txt`, loaded by
   `0x00434e40` into `0x00607120[type]` / counts `0x0060712c[type]`, types `SCTYPE_LASER`,
   `SCTYPE_COCKPIT`, `SCTYPE_DOCKING`), looked up by `0x00434770(type)` with the body id in
   `EDI` (`model+0x08` of `0x004863c0(id)`, or `node+0x140`). Callers: `0x0044573a` (fire
   control), `0x00440839`, `0x00442f54`, `0x00444eef`, `0x00446d26`, `0x00448f87`,
   `0x004497c5`, `0x0042080a` (cockpit). Offsets are per body, not per LOD record.
3. The offset is transformed with fixed-point vector helpers (`0x0040e780` scale, `0x0040e720`
   add, `0x004f0da0`) and the node and ship transforms, which the scene update and `0x0047b800`
   maintain independently of LOD.

None of these functions touches `node+0x14c` (§1.1: no `+0x14c` access lies in
`0x00440000..0x0044ffff`), bit `0x2` (§2.2) or
`node+0x130 & 0x100000` (§4). A ship drawn with `C` fires from the same muzzle positions.

One non-render consumer of a LOD record exists: the collision tree of a scene part is built at
creation from record `n-1` (`0x0043d8dd..0x0043d8e6` → `0x0047eb90(node, n-1)`, or record 1 for
the type-5 / `0x0043fd00` case; `0x0047eb90` indexes `model+0x0c[idx]` at `0x0047ec12` and
stores `idx` in `model+0x60`). With the pad placement `n-1` is the pad, a copy of `C`. `C` and
the pad keep the point and face counts of the original coarsest record (TL 1313/553,
M1 1584/658, M2 1865/756, outpost 17468/9453; `bob1.py info`, measured), so the hit geometry
is the same set of triangles if `lod_overlay.py` copies the faces unchanged (inferred; the
tool regroups faces, it does not re-mesh). The override at `0x0047ebe8..0x0047ebff`
(`0x0046ed60`) was not examined.

## 4. `node+0x130 & 0x100000`

Cleared with `0x80000` at the pass entry (`0x0047cfed`, `and 0xffe7ffff`), so it is per view and
frame. Set at `0x0047d26b` (projected size against the per-node threshold `node+0x1dc`),
`0x0047d27c` / `0x0047d28e` (`| 0x180000` below 20 px or in a `0x1000000` view) and
`0x0047d51e` (final index `>= 3`). Its only readers are in the per-draw state function
`0x004c0150`:

| Site | Effect when set |
| --- | --- |
| `0x004c09a0`, `0x004c09b6` (effect materials) | technique `"DEFAULT"` (`0x0056346c`) instead of `"BUMPMAP"` (`0x00563464`) |
| `0x004c0b69` (built-in `standard_lighting` / `effects` path) | `"DEFAULT"` instead of `"BUMPMAP_LOW"` (`0x00563474`); the call `0x004f52e0` is skipped |
| `0x004c32f6` | the texture parameter at descriptor `+0x6c` is set through `0x004b9ed0(param, -1)` with the fallback `*(0x00606f60 or 0x00606f64)+0x34` instead of the material's own texture index (read as "bump map off", inferred from the technique pairing) |

It is render-only and applies to the flagged node's own draws, not to its children. Measured
in run257: every drawn row of a pilot body at the coarse index has the flag (TL 32, M1 64,
M2 16, outpost 16 rows), none at LOD 0; in run255 the pilot bodies were drawn only at LOD 0 in
the captured frames, without it (`pilot_flag130_out.txt`). `argon.fb` defines both `BUMPMAP`
and `DEFAULT`; which samplers `DEFAULT` reads (in particular `t_LightMapTexture`) was not
examined.

## 5. Patch options for the gate

No patch is needed for the run257 symptom (§2.3, §2.4). If a flagged child is found later:

**(a) Recommended form: turn the LOD test into "keep".** One byte at VA `0x0047d071`
(file offset `0x7c471`): `7e 09` (`jle 0047d07c`) → `eb 09` (`jmp 0047d07c`). The gate then
hides a `0x40000` child only when its parent is not renderable.

- Instruction boundaries: `0x0047d071` starts a two-byte `jle rel8` (previous instruction
  `0x0047d06b`, six bytes; next `0x0047d073`). The opcode byte changes, the length and the
  displacement do not. Ghidra finds one reference into `0x0047d055..0x0047d07b`, the jump
  `0x0047d069 → 0x0047d073`; nothing targets `0x0047d071` or `0x0047d072`.
- Relocations: none. The EXE has `IMAGE_FILE_RELOCS_STRIPPED` (characteristics `0x123`) and an
  empty base-relocation directory; the patched bytes are not a relocated field.
- Registers and flags: the patch changes no register value. `jmp` consumes no flag and the flags
  of `cmp` at `0x0047d06b` are dead at `0x0047d07c` (rewritten by `test bl,2` at `0x0047d082`
  before any use). EAX, ECX and EDX are dead after `0x0047d07c` on every path (EAX rewritten at
  `0x0047d091`, ECX at `0x0047d0b5`, EDX at `0x0047d0ef`; the `0x0047d528` child walk reads
  none of them).
- Write: `engine_patch::write_code` with a verify window over `0x0047d055..0x0047d07b`
  (expected `8b 87 2c 01 00 00 a9 00 00 04 00 74 1a f6 81 2c 01 00 00 02 74 08 39 91 4c 01 00 00
  7e 09 83 e0 fd 89 87 2c 01 00 00`), as `lod_scale.cpp` does; the byte lies in the aligned
  qword `0x0047d070..0x0047d077`, so the write takes the `lock cmpxchg8b` path. No overlap with
  the proxy's other sites in this function (`cull_census` `0x0047d248..0x0047d25e` and
  `0x0047d519..0x0047d52e`, `cull_small_parts` window from `0x0047d294`, `lod_scale`
  `0x0047d44b`). Reentrancy: the pass recurses into itself (`0x0047d53c`); a static byte has no
  state, every level executes the same instruction. Native D3D parity: none involved.
- What the removed test protected: nothing in the stock data this study can see. Its only
  effect is on nodes that the KC logic flags `0x40000`; with the patch such a node stays visible
  while its parent draws LOD 1+ (more draws; possible double geometry if the parent's coarser
  records contain a baked copy of it).

**(b) Compare with the parent's last record.** Needs the parent's model: `0x004863c0(parent+0x140)`
(cdecl, one argument, returns the model or 0, clobbers EAX/ECX/EDX), then `movsx n,[model+0x10]`
and `hide iff parent+0x14c >= n-1`. It does not fit in place; a stub reached by a five-byte
`jmp` over `0x0047d06b..0x0047d072` (eight bytes of two whole instructions) must preserve EAX
(the flags word stored on the hide path) and EDI, and return to `0x0047d073` or `0x0047d07c`.
At Very High the main view's final index is at most `n-2`, so (b) behaves like (a) there; with
the pad as record `n-1` it hides children at Low..High whenever the pad is drawn, i.e. over the
whole coarse range. Not better than (a) for the pilot.

**(c) Per body.** The same stub shape, comparing `parent+0x140` against the overlay's body ids
(a small proxy-owned table, filled from the overlay manifest) and applying (a) only to them.
Only worth it if (a) is shown to double geometry on stock bodies.

## Unknown

- Which KC methods, if any, pass a caller-supplied value containing `0x40000` into the
  `B3D_InstSetFlags` sites that OR into a local; a KC disassembler or a census counter at
  `0x0047d073` (hide taken, with parent model) would settle it.
- Whether the `DEFAULT` technique of `argon.fb` samples the light map, i.e. whether a `C` that
  kept the exhaust materials would glow at index >= 3.
- The collision-index override `0x0046ed60` in `0x0047eb90`.
- `B3D_InstSetSizeUsed` stores a fixed index with `node+0x12c |= 8`, but `0x0047d001` zeroes
  `+0x14c` before `0x0047d314` honours flag `8`; whether the fixed index survives to the draw was
  not followed.
- Readers of `node+0x130 & 0x80000` (set together with `0x100000` below 20 px): none found by
  the same scan.

## Reproduce

```sh
i686-w64-mingw32-objdump -d -M intel "$HOME/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/X3AP.exe" > /tmp/x3-lod/full.s
python3 verification/results/lod-child-hide/exe_flag_scan.py /tmp/x3-lod/full.s
python3 verification/results/lod-child-hide/kc_instsetflags.py
python3 verification/results/lod-child-hide/flag40000_census.py <session log>
python3 verification/results/lod-child-hide/flag40000_draws.py <session log>
python3 verification/results/lod-child-hide/pilot_flag130.py <session log>
python3 verification/results/lod-child-hide/engine_nodes.py <session log> <frame>
(cd tools/analysis && python3 ../../verification/results/lod-child-hide/exhaust_materials.py)
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/Cellar/ghidra/12.1.3/libexec/support/analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process X3AP.exe -noanalysis -readOnly -scriptPath tools/analysis \
  -postScript X3XrefsTo.java /tmp/x3-lodchild/xref1.txt 0047d071 0047d072 0047d073
```

The session logs are `/tmp/x3-bottleX3-run255/session-20260923-021931-212.log` (frame 3351) and
`/tmp/x3-bottleX3-run257/session-20260923-024913-212.log` (frame 3615); the outputs beside the
scripts were produced from them.
