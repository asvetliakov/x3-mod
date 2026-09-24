# Texture lookup: from a material texture name to the loaded file

Read-only study, 2026-09-24, for the merged-LOD baker's `texture_unresolved` refusals
([merged-lod-feasibility.md](../architecture/merged-lod-feasibility.md), "texture_unresolved").
Installed `X3AP.exe` SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`,
preferred VAs, base `0x00400000`. Ghidra 12.1.3 headless on `/tmp/x3-ghidra-research/X3Render`
(`-readOnly -noanalysis`; `X3DecompileFunctions.java`, `X3FunctionContext.java`, `X3XrefsTo.java`)
plus capstone windows of the same EXE for every instruction-level claim. Raw decompiler output,
listings and the decoded `Materials` file stay in the session scratchpad, untracked. No game
launch, no Wine command; the bottle was read only. Marks: [s] static (EXE code or data),
[m] measured on the installed files, [i] inferred.

**Result.**

1. **Lookup order.** A material texture name is first turned into a texture **id**
   (`0x004f4cb0`), and the id into a **path** on first use (`0x004f4160`). A named texture's
   path is `textures\<name>`; a numbered texture's path is `tex\true\<n>`. The path then goes
   through `0x004f3510`: **`dds\<basename>` with `pck dds`**, then **`<path>` with `pck dds`**,
   then **`<path>` with `tga`**, then **`<path>` with `jpg`**. Each step is one call of the shared
   resolver `0x004e7590`: a loose file under the game folder first, then the catalogues from the
   highest slot down, the first catalogue with a match wins, and the extension list gives the
   rank. If all four steps fail, the texture becomes a **placeholder chosen by the name's
   suffix** (`diff` → `NONE_GRAY`, `bump` → `NONE_NORMAL_LOW`/`NONE_NORMAL`, `spec` →
   `NONE_WHITE`, `light` and anything else → `NONE_BLACK`, …). The extension the body writes
   is dropped before any of this; `bmp` is never tried. [s]
2. **Bare numbers are ids, not file names.** `25.jpg` → `25` → `sscanf("%d")` = texture id 25,
   which is row 25 of `types\Materials`. Row 25 has no file name, so the path is `tex\true\25`,
   and the load is `tex/true/25.jpg` (`01.cat`). The parse stops at the first non-digit, so
   Khaak's `25_spec.jpg` and `260_bump.jpg` are **the same ids 25 and 260**: the engine binds the
   diffuse image to the specular and bump slots. `69.jpg` is row 69, which names
   `effects\others\envmap_test`, so it loads `dds/envmap_test.pck`. [s] rule, [m] rows and members
3. **`X:\tex\true\340.jpg` does not map anywhere.** `X:\` gets no special handling: the name is
   not numeric (it starts with `X`), so it becomes the named texture `X:\tex\true\340`. The path
   `textures\X:\tex\true\340` fails, and so does `dds\340` (no such member). The result is the
   default placeholder **`dds/NONE_BLACK`**. It never reaches row 340, which is a generated
   512×512 HUD surface anyway. [s] rule, [m] members, [i] loose-scan failure on a mid-path drive
   colon
4. **`dds\unique_argon_hybrid_bump.tga`** is the `t_BumpTexture` parameter, loaded in the normal
   map slot. No `unique_argon_hybrid_bump` exists in any catalogue, so the engine draws the
   placeholder **`NONE_NORMAL_LOW`** (or `NONE_NORMAL`, depending on a device field), a flat
   normal map. With `VideoD3DFlags2 & 0x400` the name first becomes `…_low_bump`, which is
   missing as well. [s] rule, [m] members
5. **Baker rule:** §9.

## 1. Call graph

| address | role |
|---|---|
| `0x004f4cb0` | name → id (`short`, cdecl, args `std::string*` name, `int` flags); callers: text body `0x0048461b`, binary body `0x00482039…`, effect parameters `0x004baae0…` via `0x004baa30`, others [s] |
| `0x004baa30` | effect `STRING` parameter → texture id (`thiscall`, `ECX` = parameter name, args semantic id, value); from `0x00470320` [s] |
| `0x004ba3c0` | bump name rewrite `_bump` → `_low_bump` (used when `*(0x00606f34)+0x100 & 0x400`) [s], direction [i] |
| `0x004f44a0` | loads `Materials` through `0x0046f450`; row count → `0x006069b0` and `0x00608dac`; rows → `0x00608db0` (stride `0x3c`); each row's file name → the texture table `0x006069ac` [s] |
| `0x006069ac` | texture table, stride `0x10`: `+0` name (`char*`, 0 = numbered), `+4` flags, `+8` loaded texture object, `+0xc`; ids `0 … 0x006069b0−1` are Materials rows, then `0x006069b4` named entries [s] |
| `0x004f5110`, `0x004f5070`, `0x004f50c0`, `0x004f5280` | id → texture object / width / height / validity; each calls `0x004f4160` on first use (`+8 == 0`) [s] |
| `0x004f4160` | id → path → `0x004f3510` (`EAX` = id); the result, placeholder included, is cached in `+8` [s] |
| `0x004f3510` | path → loader chain → placeholder (cdecl: path, flags, 0, arg) [s] |
| `0x004dc540` | loader for `pck dds` (string `0x00564468`, pushed at `0x004dc56a`; resource load `0x004e8e10`) [s] |
| `0x004de9c0` | loader for `tga` (string `0x005648dc`) [s] |
| `0x004dd2c0` | loader for `jpg` (string `0x00564470`) [s] |
| `0x004e7590` | resolver shared by all three ([loading-orchestration.md](loading-orchestration.md), [body-format-bob1.md](body-format-bob1.md) §7) [s] |
| `0x004d8f10` | device init: loads the placeholders at `0x004da6e6..0x004da794` [s] |

## 2. Name → id (`0x004f4cb0`)

In order [s]:

1. `""`, `"0"`, `"NULL"`, `"Null"`, `"null"` (exact, case-sensitive compare through `0x00469700` / `0x00401080` against
   `0x00554ef0`, `0x00564fc8`, `0x0055b4c0`, `0x0056101c`, `0x00561024`) → id **0**, no texture.
2. If the name is longer than 4 characters and its **last** `.` (`0x00408e60` searches from
   the end) is at `len − 4`, the function recurses on the name without the extension
   (`0x004f4d35..0x004f4d71`). Any 3-character extension is dropped: `.jpg`, `.tga`, `.dds`,
   `.pck`.
3. If the first character is a digit (`_isdigit`, `0x004f4db1`) or `-`, `sscanf(name, "%d")`
   (`0x004f4ddf`, format `0x00556088`) succeeds and the **integer is the id**. It is returned
   without a range check. `sscanf` stops at the first non-digit, so `25_spec` → 25.
4. Otherwise the named part of `0x006069ac` is searched case-insensitively (`__stricmp`) and a
   match returns its id. A miss appends an entry (grown in steps of 1000 by `0x004b8920`) with
   id `rows + (named count − 1)` and stores the name (without extension) through `0x004f4bb0`.
   With `flags ≠ 0` (not the body or effect callers, which pass 0), the entry flags are also set.

Entry flags come from the using material's `+0x28` word (`0x004f4f50`, called by `0x00470d30`
for the six texture slots of a material, and by `0x004f5a20`); for Materials rows they are
OR'd with the row's `MPF_` flags `& 0xa70118` [s].

## 3. The `Materials` table (`0x004f44a0`)

`Materials` is read through the type-file loader `0x0046f450` [s]. That the file is
`addon\types\Materials.pck` (path table `+0xac`/`+0xb0`, `addon\types\%s.txt|pck`) is [i]. In the bottle,
`addon/01.cat` holds `addon/types/Materials.pck` with **1237** rows, of which 55 have a file
name; the base `03.cat`/`10.cat` copies have 1235. All three agree on rows 25, 36, 39, 69 and
1039 [m]. Per row [s]: bytes `+0..+0xb` colours, **`+0xc` texture id** (`short`), `+0xe`
transparency (`TRANSP_*`), `+0x10` `MPF_*` flags (name table `0x0054dbe0`: `ALPHATEST 1`,
`DESTINATIONBLEND 2`, `ALPHABLEND 4`, … `BESTQUALITY 0x8000`, `FONTSCALE 0x10000`,
`WRITEABLE 0x40000`, `AUTOFREE 0x80000`, `TEXTUREALPHA 0x200000`, `IMPORTPICTURE 0x400000`,
`GENERATED 0x800000`, `HAZE 0x4000000`), `+0x14/+0x16` generated width/height, then map ids and
shine values, and last the file name. The file's header says: "filename (leave blank to use id)".

`0x004f5110` returns no texture for an id `< rows` whose row `+0xc` is 0, and for any id
outside `0 … rows + named − 1` [s].

## 4. Id → path (`0x004f4160`)

Gate: `*(0x00606f34)+0xfc & 0x100` must be set, or nothing loads [s]. A format descriptor
(`*(0x00608518)+0x90` map, key `+0x98 + 1` for a Materials row with `+0xe < 0xff`, else
`+0x94 + 1`) must have flag 4 for a file load. The descriptor comes from the device, not the
name [s]; that it is the same for every file texture is [i].

| entry | path (`0x004ee4c0` formats, path table `*(0x00606f34)+0xd8 = 0x0057c008`, set at `0x004ecab2`) |
|---|---|
| flag `MPF_GENERATED` (`0x800000`) | no file: blank `+0x14 × +0x16` surface from `0x004f3950` |
| flag `MPF_IMPORTPICTURE` (`0x400000`) | logo picture table `0x00578648` |
| **no name** (numbered row) | `"true\%d"` (`0x00564fb4`, at `0x004f43fe`) inside `+0x74` `"tex\%s"` → **`tex\true\<n>`** |
| name, id `< 10000` | `+0x78` `"textures\%s"` → **`textures\<name>`**; with entry flag `0x10000000`: `"%s"` (`0x0054d52c`) → the raw name |
| name, id `≥ 10000` | `"%s_l"` (`0x00564fac`) first, then as above |

With `*(0x00606f34)+0x784 > 1` and `MPF_FONTSCALE`, the id is raised by 10000 before this
(`true\1<nnnn>`; no such member exists [m]). The `"%s_l"` branch is reached that way, or, [i],
once more than 8,763 named textures are registered (not measured).

## 5. Path → file (`0x004f3510`)

[s], `0x004f3510..0x004f38b6`:

- Name ending `.jpg`: `0x004dd2c0` (`jpg`). If that fails, the extension is cut and the function
  recurses. The same for `.tga` with `0x004de9c0`. Material names never take these branches,
  because §2 already dropped the extension; they serve callers that pass a full file name
  (UI, fonts).
- Extensionless (every material texture): the last `\` or `/` splits off the basename
  (`0x004f3660..0x004f3680`). A path containing `\Desktop\` skips all loads. Otherwise:
  1. `+0x7c` `"dds\%s"` of the **basename**, `0x004dc540` `pck dds` (`0x004f36cd`; only when the
     path has a separator, which every path of §4 does);
  2. the **path**, `0x004dc540` `pck dds` (`0x004f36e6`);
  3. the path, `0x004de9c0` `tga` (`0x004f36ff`);
  4. the path, `0x004dd2c0` `jpg` (`0x004f3718`).
- All failed: the placeholder, by a case-insensitive compare of the path's last characters. It
  gets `+0x10 |= 0x20000` and a reference, and `0x004f4160` stores it as the entry's texture, so
  the texture stays the placeholder for the whole session.

| suffix of the path | placeholder global | texture (loaded at device init, `0x004da6e6..0x004da794`) |
|---|---|---|
| last 4 `diff` | `0x00606f70` | `\NONE_GRAY` → `dds/NONE_GRAY.pck` |
| last 4 `bump` | `0x00606f64`, or `0x00606f60` when `*(*(0x00608b3c)+0x18)+0xa0 ≠ 0` | `\NONE_NORMAL_LOW` / `\NONE_NORMAL` |
| last 4 `spec` | `0x00606f58` | `\NONE_WHITE` |
| last 5 `light` | `0x00606f5c` | `\NONE_BLACK` |
| last 4 `occl` | `0x00606f74` | `\NONE_OCCL_DECAL` |
| last 6 `envmap`, last 4 `envi` | `0x00606f6c` | `\ENVI` |
| anything else | `0x00606f5c` | `\NONE_BLACK` |

The placeholders load through the same wrapper (basename → `dds\NONE_*`). All eight
(`NONE_BLACK`, `NONE_GRAY`, `NONE_NORMAL`, `NONE_NORMAL_LOW`, `NONE_WHITE`, `NONE_OCCL_DECAL`,
`NONE_ENVI`, `ENVI`) are `.pck` members of `01.cat` [m].

## 6. Loose and archive precedence (resolver `0x004e7590`)

Each step of §5 is one resolver call with the step's extension list. The details are in
[loading-orchestration.md](loading-orchestration.md) and [body-format-bob1.md](body-format-bob1.md)
§7; for textures [s]:

- The loose scan (`FindFirstFile("<dir>\<base>*.*")` relative to the game folder) runs first.
  Any ranked hit ends the call, and the catalogues are not searched.
- Then catalogue slots from `*(short*)(G+0xc8) − 1` down to 0: a mod catalogue
  (`addon\mods\%s.cat`), then `addon\NN.cat` from the highest number down, then `NN.cat` from
  the highest number down. The **first slot with a matching entry wins**; within it the
  lowest-ranked extension of the list wins (`pck` before `dds`). Keys are upper-cased with
  `/` → `\`.
- A `-L%03d` language variant of the base outranks the plain name.

The bottle has no loose `dds`, `tex` or `textures` folder in the game root or under `addon` [m].
Every texture therefore comes from a catalogue. The catalogues hold `dds/…` (`.pck`),
`tex/true/<n>.jpg|tga` and `textures/…jpg` (e.g. `textures/effects/weapons/fx_bullet_push1_diff.jpg`,
`textures/x38/topRT2_diff.jpg` in `addon/01.cat`) [m].

`X:` is an ordinary character sequence: no drive mapping or path rewrite exists anywhere in the
chain [s]. For the path with entry flag `0x10000000` (raw `%s`), a loose scan would go to drive
`X:`. The census material that carries the `X:` name has flags `0x2000000` [m], so it takes the
`textures\` branch. The bottle's `dosdevices` are `c: d: y: z:` [m].

## 7. Effect parameters and the bump rewrite

`0x00470320` asks `0x004ba500` for the parameter's semantic id and passes the value to
`0x004baa30`. For semantic ids 1, 3–7 with a parameter name starting with `t`, and for the
names `t_BumpTexture`, `t_DiffuseTexture`, `t_SpecularTexture`, `t_LightMapTexture`,
`t_CubeMapTexture` and `t_DetailTexture` when the id is 0, the value goes to `0x004f4cb0`
unchanged. For the bump id 2 (or the `t_BumpTexture` name) with `*(0x00606f34)+0x100 & 0x400`
(a `VideoD3DFlags2` bit, [alternative-video-playback.md](alternative-video-playback.md)), the
value first goes through `0x004ba3c0`, which rewrites `_bump` to `_low_bump` [s]. The direction
is [i], from the decompiled string pair and the shipped `*_low_bump.pck` twins such as
`dds/AGI_M3-body_low_bump.pck` [m]. Every texture slot therefore takes the §2–§5 path, and the
slot matters only for the placeholder suffix and the bump rewrite.

## 8. The 13 census rows

`verification/results/lod-overlay-batch/texture_lookup_rows.py` applies §2–§6 to every
material slot of the census rows refused `texture_unresolved`
(`census.txt`, 13 rows; bodies read with the `addon/05`/`06` overlays skipped).
`texture_lookup_rows_out.txt` holds the output: 70 slot names the baker cannot resolve, over the 15
members of the 13 bodies. Of these, **37 load a file** and **33 fall back to a placeholder**
[m, by the static rule; no in-game check].

| census row (first refused name) | engine result for that name | other refused names in the body |
|---|---|---|
| `effects/menugfx/hud_icon_text_x` (`X:\tex\true\340.jpg`) | `NONE_BLACK` placeholder (§3 of the result) | – |
| `environments/asteroids/asteroid_B_ClassPleasureComplex` (`1039.jpg`) | `tex/true/1039.jpg` (`01.cat`) | 9 more ids load `tex/true/<n>.jpg` or `.tga` (`598.tga`, `590.tga`) |
| `ships/M3/Argon_hybrid` (`dds\unique_argon_hybrid_bump.tga`, bump) | `NONE_NORMAL_LOW` / `NONE_NORMAL` placeholder | – (its diffuse/spec/light resolve by basename in `dds/`) |
| `ships/M6/Khaak_M6Main` (`25.jpg`) | `tex/true/25.jpg` | 16 more slot names: ids 425, 260, 36, 209, 29, 367, 263 → `tex/true/<n>.jpg`; `<n>_spec`/`<n>_bump` → id `n`; `69.jpg` → `dds/envmap_test.pck` |
| `ships/M6/Khaak_M6Sec` (`36.jpg`) | `tex/true/36.jpg` | 9 more, same pattern (ids 209, 25, 425, 263) |
| `ships/terran/atf_m3` (`AGI_M3-body_light.tga`, light) | `NONE_BLACK` placeholder (suffix `light`) | – |
| `ships/terran/atf_m3p` (same) | `NONE_BLACK` | – |
| `ships/x3ap/props/XTC_terran_dock_quicklaunch_LDoorL` (`XTC_terran_door_diff.dds`) | `NONE_GRAY` placeholder | `_bump` → `NONE_NORMAL_LOW`/`NONE_NORMAL`, `_spec` → `NONE_WHITE` |
| `…_LDoorR`, `…_MDoor`, `…_TDoor` (same names) | `NONE_GRAY` | same |
| `ships/xenon/xenon_m4` (`unique_xenon_M5_02_diff.dds`) | `NONE_GRAY` | `_02`/`_03` `bump`/`spec`/`light` → placeholders by suffix; the `.pbd` twin names the same stems under `unique\…tga`, with the same results |
| `stations/docks/test` (`unique\unique_argon_M3_02_diff.tga`) | `NONE_GRAY` (only `unique_argon_M3_02_pirate_diff` exists) | – |

## 9. Rule for the baker

Resolve a material texture name the way the engine does:

1. Treat `""`, `0`, `NULL`, `Null` and `null` as no texture.
2. Drop a trailing 3-character extension (last `.` at `len − 4`).
3. **Numbered names.** If the name then starts with a digit or `-`, take the leading decimal
   integer `n` (`25_spec` → 25). Look `n` up in the winning `types/Materials` (addon first).
   Refuse or skip the name when `n` is outside the rows, when the row's texture-id field is 0,
   or when the row has `MPF_GENERATED`. Otherwise the path is `textures\<row filename>` for a
   named row and `tex\true\<n>` for any other row.
4. **Named textures.** Any other name gives the path `textures\<name>`, keeping its
   directories.
5. **Lookup.** Try `dds/<basename>` with `.pck`/`.dds`, then `<path>` with `.pck`/`.dds`, then
   `.tga`, then `.jpg`. Within each step, loose files come before the catalogues, which are
   searched from the highest slot down; the first slot with a match wins.
6. **Placeholder.** If nothing matches, substitute the placeholder the suffix selects: `diff` →
   `dds/NONE_GRAY`, `bump` → `dds/NONE_NORMAL_LOW` (the engine may pick `NONE_NORMAL`), `spec` →
   `NONE_WHITE`, `light` → `NONE_BLACK`, `occl` → `NONE_OCCL_DECAL`, `envmap`/`envi` → `ENVI`,
   anything else → `NONE_BLACK`.

The present `tex/<stem>` step with `jpg`/`tga`/`bmp` (`lod_atlas.IMAGE_LOOKUP`) has no engine
counterpart. `tex\%s` is only ever built around `true\%d`, and `bmp` is not in any extension
list, so the step should be replaced by steps 3–5. A placeholder result is what the game draws,
so it can be baked rather than refused. For numbered textures the source is a JPEG or TGA,
which the atlas must decode.

No hook is proposed; nothing here is a hook site.

## Unknown

- Which device field `*(*(0x00608b3c)+0x18)+0xa0` is, which decides between `NONE_NORMAL` and
  `NONE_NORMAL_LOW`, and the default of `VideoD3DFlags2 & 0x400`.
- Who sets texture-entry flag `0x10000000` (raw path instead of `textures\`). No `MPF_` name
  has it, and no census material carries it.
- The descriptor map `*(0x00608518)+0x90` and its keys `+0x94`/`+0x98`: only the flag-4 gate
  and the bit-depth use were read.
- Whether a script or runtime path later rebinds the HUD icon's material to the generated
  row-340 surface; the static load gives `NONE_BLACK`.
- The type-file loader `0x0046f450` was not decompiled. That `addon\types\Materials` wins over
  `types\Materials` is inferred from its path strings, and the rows used here agree in all
  three copies.

## Reproduce

```sh
# Ghidra (raw output stays local)
# -process X3AP.exe -noanalysis -readOnly -postScript X3DecompileFunctions.java <out> \
#   004f4cb0 004f4160 004f3510 00470320 004baa30 004dc540 004dd2c0 004f4bb0 00469700 00469a00 \
#   004de9c0 004ba3c0 004f5040 004f4f50 004f44a0 004f5070 004f50c0 004f5110 004f5280 00408e60
# -postScript X3XrefsTo.java <out> 006069b0 006069ac 006069b4 0057c008 00608dac 00606f5c 00606f70 \
#   00606f64 00606f60 00606f58 00606f74 00606f6c 004f4f50 004f4160 004f5110
# Capstone windows: 0x004f4cb0..0x004f4e10, 0x004f4160..0x004f4250, 0x004f43a0..0x004f4480,
#   0x004f3650..0x004f3730, 0x004da6c0..0x004da7a0, 0x004eca9c..0x004ecac0
# Census rows under the rule (about 4 s, bottle X3, read-only)
PYTHONPATH=tools/analysis python3 verification/results/lod-overlay-batch/texture_lookup_rows.py \
  > verification/results/lod-overlay-batch/texture_lookup_rows_out.txt
```
