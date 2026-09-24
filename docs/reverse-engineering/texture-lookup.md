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
5. **Baker rule:** §9, amended by §10.4 for negative ids.
6. **Negative ids are animated textures, not files.** A face group whose material index is
   `-N` (and the material slot name `-N.tga` that goes with it) draws row `N` of
   `types\Animations`: an animation instance per object, advanced every frame, whose current
   frame is an ordinary texture id. `-79` is the Argon engine glow loop
   `fx_engine_blue1..4_diff`, `-81` the advert surface `StaticAdverts`. The shipped
   `dds/-79.pck`-style members are never reached. [s] path, [m] rows and members, §10
7. **`\Desktop\` names load nothing.** All five census names with an author's desktop path skip
   the four load steps and take the suffix placeholder (three `NONE_OCCL_DECAL`, one
   `NONE_BLACK`, one `NONE_GRAY`), although `dds/<basename>` exists for each. [s], §11

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
   or when the row has `MPF_GENERATED`. A negative `n` is an Animations row, not a Materials
   row: §10.4. Otherwise the path is `textures\<row filename>` for a
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

## 10. Negative ids: texture animations (`types\Animations`)

Static study, 2026-09-24, same EXE and tools (Ghidra `X3DecompileFunctions.java`,
`X3FunctionContext.java`; capstone windows for every instruction-level claim). The bodies, rows and
members are [m] from `verification/results/texture-lookup-animations/animation_rows.py`
(`animation_rows_out.txt`, bottle X3 read only, overlay slots 05/06 skipped).

### 10.1 What `-79` is in a body

The negative number is first of all a **face-group material index**. In
`objects/effects/engines/fx_engine_argon_m3.bob` the only group of LOD0 has material `-79`; in
`objects/others/argon_adsign_a.bob` the groups are `-81, 0…7` (LOD0) and `-81, 1, 2, 3` (LOD1) [m].
The material list carries, next to it, a material whose `t_DiffuseTexture` is `-79.tga` (effect
material, `engine.fx`) or whose classic texture is `-75.tga` (`objects/v/10660.bod`) [m]. The
number in the name is the same `N`.

Across the winning vanilla bodies [m]: 26 distinct negative ids; `-79` in 131 bodies, `-81` in 39,
the rest in one or two each (explosions, flak, bullets, `qm_icon`). 188 negative group indices (distinct
per body); 177 have a material whose diffuse name carries the same id, 11 have none; 31 materials
with a negative diffuse are referenced by no negative group; no negative-diffuse material is
also referenced by a non-negative group index.

### 10.2 The path, by address

1. **Name → id.** `0x004f4cb0` turns `-79.tga` into `-79` (`sscanf "%d"`, `0x004f4ddf`) [s]. The id
   lands in the effect parameter (`0x00470320`: `+0x20` type 0, `+0x24` id) or in the classic
   material's `+2` (`0x00482042`). No texture-table entry is created: the `sscanf` branch returns
   before registration, and `0x004f4bb0` returns for a negative id (`0x004f4bb3`) [s].
2. **Animations table.** `0x004f5460` (from `0x0048af76`) loads `Animations` through the type-file
   loader into `*0x00608db8` (count `short 0x00608db4`, stride `0x44`) [s]:
   `+0` type (`TAT_LOOP 1`, `PINGPONG 2`, `ONESHOT 3`, `MOVIE 4`, `TAGLOOP 5`, `TAGPINGPONG 6`,
   `TAGONESHOT 7`, `TAGCOLLECTION 8`, `TAGONESHOT_REINIT 9`, `SINGLESTEP 10`, `TAGSINGLESTEP 11`,
   `TAGARRAYSINGLESTEP 12`; name table `0x0054de40`), `+4` `TADF_` flags (`TADF_COORDS 2`),
   `+6`/`+8` first/last texture id, `+0xc…+0x18` start/end coordinates when `TADF_COORDS`,
   `+0x1c` frame count, `+0x20` frame list (stride `0x10`: `+0` `TATF_` flags, `+2` texture id,
   `+4` duration, `+8/+0xc` coordinates; `TAGCOLLECTION` stride `0xe`; `TAGSINGLESTEP` an `int`
   id list), `+0x24` total duration, and for `TAT_MOVIE` seven ints at `+0x28…+0x40`. Every
   texture name in the file goes through `0x004f4cb0` with flags 0, so frames are ordinary named
   or numbered textures (§2–§5). `addon/01.cat` holds `addon/types/Animations.pck`, 106 rows;
   the loader's grammar consumes every token of it [m].
3. **Object bind.** `0x00487e30` calls `0x00487810`, which walks the body's face groups (a
   three-level list; that the levels are LOD, part and group is [i]) [s]:
   - group index `< 0` (`0x004878b0..0x004878bf`, `0x00487a4b`): add Animations row `-index`;
   - index into the body's materials, effect material with `+0x28 & 0x8000000`: add the
     diffuse and the bump parameter id, negated (`0x004878d7..0x00487a02`). That flag is set by
     `0x00481780` only when `TexAnimDuration` or `TexAnimRotation` is non-zero (semantic ids
     `0x18`/`0x19`, `0x004ba500`): the entry is then a static texture with UV animation, or a
     row when the parameter id itself is negative;
   - other materials: classic `+2 < 0` or `+0x36 < 0` adds row `-id` (`0x00487a04..0x00487a4d`);
     a body without its own material list (`+0x50 & 0x10` clear) uses the `Materials` row's
     `+0x18` field instead (`0x00487a2d`).
   At most 20 entries (`cmp ecx, 0x14`). `0x004f5b60` builds the instance array (stride `0x80`)
   at object `+0x1ac`, count `+0x1a8`: `+0` row (or `-texture` with `+0xc & 1` for a static
   entry), `+0x40` UV matrix (16.16 fixed; translation from row `+0xc/+0x10` or from the first
   frame's coordinates), `+0x70` current texture id, initialised to row `+6` when the row has no
   frame list or is `TAGCOLLECTION`/`TAGSINGLESTEP`, else to the first frame's texture.
   The vanilla bodies need at most 12 entries, so no negative group loses its instance [m].
4. **Mesh build.** `0x004bd830` → `0x004bcee0` (`0x004bd720..0x004bd7b8`): for a negative group
   index, find the material whose diffuse or bump parameter (effect) or `+2` (classic) equals it
   and call `0x004f5a20(row)`, which applies that material's flags to every frame texture of
   the row (`0x004f4f50`) [s].
5. **Per frame.** `0x0047e6e0` (deferred draw list drain) calls `0x004f66e0(count, array)`, which
   advances each instance on the game clock `*(0x00606f34)+0x718`: frame texture `+0x70`, UV
   matrix `+0x40`; a `TAT_MOVIE` row calls `0x004f65f0` with the frame texture id and the row
   ints `+0x2c…+0x40` [s].
6. **Draw.** `0x004c0150` (`0x004c0236..0x004c0458`): a group index `< 0` searches the object's
   instances for row `-index` (`0x004c02a8..0x004c0308`) and takes its `+0x70` texture and
   `+0x40` UV matrix. The material is the first effect material whose diffuse parameter id
   equals the group index, else material 0 (`0x004c0310..0x004c0390`). A negative bump
   parameter is resolved the same way (`0x004c03d0..0x004c0435`). The effect binder then uses the
   frame id for a negative diffuse or bump parameter (`0x004c06a6..0x004c06c4`,
   `0x004c06d3..0x004c06f6`). If no instance matches, the id stays negative and the function
   returns 0 (`0x004c0458` → `0x004c40a4`) [s]; that the group is then not drawn is [i].
7. **Frame → file.** The frame id takes §2–§6 unchanged [s]. Rows the vanilla bodies use [m]:

| id | row type | initial frame → file | frames |
|---|---|---|---|
| `-79` | `TAT_TAGLOOP`, 200 ms | `effects\engines\fx_engine_blue1_diff` → `dds/fx_engine_blue1_diff.pck` | `blue1…4`, all in `01.cat` |
| `-97` | `TAT_TAGLOOP` | `fx_engine_purple1_diff` → `dds/fx_engine_purple1_diff.pck` | `purple1…4` |
| `-81` | `TAT_MOVIE`, `TADF_COORDS` | `test\StaticAdverts` → `dds/StaticAdverts.pck` (2048² DXT5), UV start `0.25, 0.25` | movie ints `2 0 0 0 0 1 640` |
| `-26` | `TAT_ONESHOT` 93…99 | id 93 → `tex/true/93.jpg` | ids 93…99 |
| `-39` | `TAT_TAGLOOP` | id 93 → `tex/true/93.jpg` | ids 93…99 |
| `-82`, `-83`, `-90`…`-105`, `-73`, `-75`, `-77`, `-78`, `-85` | `TAT_TAGONESHOT`/`TAGLOOP` | first named frame → `dds/<frame>.pck` | explosion, flak, beam atlases |
| `-67` | `TAT_TAGSINGLESTEP` | row `+6` = `0`: no texture until a script steps it | 76 menu icons |

### 10.3 The shipped `dds/-N` members and the candidate paths

`01.cat` (the base catalogue) holds 21 members `dds/-61.pck`, `-72…-75`, `-77…-85`, `-90…-94`,
`-97`, `-792` [m]. `dds/-79.pck`, `-81.pck` and `-97.pck` are 512² DXT1, and none equals its row's
frame texture (`fx_engine_blue1_diff` 512² uncompressed, `StaticAdverts` 2048² DXT5) [m]. No material path loads
them [s]:

- (a) `dds\<basename>` exists only inside `0x004f3510`, which only ever sees the path of a
  registered id (`0x004f4160`); a negative id never gets an entry or a path (10.2 step 1).
- (b) No caller treats `-N` as a file stem. The name reaches the texture system only as the
  integer, and the draw replaces it with the animation frame.
- (c) Every id accessor tests the sign first (`0x004f5040`, `0x004f5070`, `0x004f50c0`,
  `0x004f5110` `test cx, cx; jl`, `0x004f5180`, `0x004f51c0`, `0x004f5280`, `0x004f52e0`); no
  unsigned use of the 16-bit id was found.
- (d) No `MPF_GENERATED` row or runtime texture entry is involved; the runtime object is the
  per-object animation instance.
- The other four callers of `0x004f3510` load the loading screen (`0x00401bb0`, `true\LoadScrHD`),
  logo pictures (`0x004972d0`, `true/%s`, `h256/%s`), fonts (`0x004f7830`, `tga bmp`) and a
  display-list image (`0x00493b40`); `.rdata` has no `-%d` format [s]. That no UI or script string
  names a `-N` file is [i].

The members are therefore leftovers (by their `01.cat` placement from X3 Reunion's content).
Without them the materials still draw, because the engine draws the frame texture.

### 10.4 Rule for the baker (replaces §9 step 3 for negative numbers)

A face group with material index `-N` is drawn with Animations row `N` and with the first effect
material whose `t_DiffuseTexture` name parses to `-N` (material 0 if none; classic materials:
texture name `-N`). Its diffuse texture is not `dds/-N`: it is the row's **current frame**,
which changes over time. For a static bake, use the frame the instance starts with
(`0x004f5b60`): row `+6` when the row has no frame list or is `TAT_TAGCOLLECTION`/`TAT_TAGSINGLESTEP`,
else the first frame's name. Resolve that name with §2–§6 (Materials id → `tex\true\<n>`, name →
`textures\<name>` → `dds\<basename>` first). A negative bump parameter follows the same rule.
Parse `types\Animations` with the 10.2 step 2 grammar, from the winning catalogue (`addon`
first, as for `Materials`). The frame's UV translation (`TADF_COORDS`/`TATF_COORDS`) shifts the
lookup inside atlas sheets: it is 0 for the engine loops `-79`/`-97` and non-zero for the explosion
atlases and `-81`. So the baker should either apply the start translation to the group's UVs
or refuse rows with non-zero start coordinates. Also refuse `TAT_MOVIE` (`-81`, played through
`0x004f65f0`) and `TAT_TAGSINGLESTEP` (`-67`, no initial texture): an animated surface baked as a
still would be wrong. Never use `dds/-N.pck`: the game does not draw it.

## 11. `\Desktop\` paths

`0x004f3510` runs `strstr(path, "\Desktop\")` (`0x004f3682`, CRT `strstr` at `0x005108e0`,
case-sensitive) on every extensionless path before the four load steps. A hit jumps to the
placeholder selection (`0x004f3692` → `0x004f372a`), so neither `dds\<basename>` nor the path is
tried [s]. The path keeps the body's spelling: `0x004f4bb0` copies the name (`0x004ee250`) and
`0x004f4160` only formats it (`0x004ee4c0`, `textures\%s` or raw `%s`), so the body's
capitalisation reaches the test [s]. The five census names [m, names from the worktree's
`texture_lookup_old_new_out.txt`]:

| name (extension dropped by §2) | suffix | engine result |
|---|---|---|
| `C:\Documents and Settings\Bobby\Desktop\…\argon_M2_BLU_occl` | `occl` | `NONE_OCCL_DECAL` |
| `C:\Documents and Settings\Bobby\Desktop\…\argon_M2_BRI_occl` | `occl` | `NONE_OCCL_DECAL` |
| `C:\Documents and Settings\Mox\Desktop\GREEBLE\argon_M2_N_occl` | `occl` | `NONE_OCCL_DECAL` |
| `C:\Documents and Settings\Markus.EGOSOFT\Desktop\…\envmap_test` | none (`_test`) | `NONE_BLACK` |
| `E:\m3m4m5\AGI_BOX\Documents and Settings\Markus.EGOSOFT\Desktop\…\exp_impact_glow_diff` | `diff` | `NONE_GRAY` |

`dds/argon_M2_BLU_occl.pck`, `argon_M2_BRI_occl.pck`, `argon_M2_N_occl.pck`, `envmap_test.pck` and
`exp_impact_glow_diff.pck` exist in `01.cat` but are not loaded for these names. The same
basenames under any other path load normally. Baker rule: a name containing the exact
substring `\Desktop\` goes straight to §9 step 6.

## Unknown

- Which device field `*(*(0x00608b3c)+0x18)+0xa0` is, which decides between `NONE_NORMAL` and
  `NONE_NORMAL_LOW`, and the default of `VideoD3DFlags2 & 0x400`.
- Who sets texture-entry flag `0x10000000` (raw path instead of `textures\`). No `MPF_` name
  has it, and no census material carries it.
- The descriptor map `*(0x00608518)+0x90` and its keys `+0x94`/`+0x98`: only the flag-4 gate
  and the bit-depth use were read.
- Whether a script or runtime path later rebinds the HUD icon's material to the generated
  row-340 surface; the static load gives `NONE_BLACK`.
- `TAT_MOVIE` playback (`0x004f65f0`, row ints `+0x28…+0x40`) and how the instance UV matrix
  `+0x40` reaches the shader or the texture stage were not traced; the `-81` start translation
  `0.25, 0.25` is read from the file and its screen effect is [i].
- Which material a negative group without a matching effect material draws with when material
  0 is not an effect material (the `0x004c0321` test fails): not traced (11 such groups, §10.1).
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
# Section 10/11: 004f5460 004f5400 004f5a20 004f5b60 004f5d70 004f61e0 004f63d0 004f66e0 00487810
#   00487e30 0047e6e0 00470320 00481780 004ba500 004baa30 decompiled; capstone windows
#   0x00487810..0x00487b10, 0x004bd720..0x004bd7e0, 0x004c0200..0x004c0490, 0x004c06a0..0x004c0700,
#   0x004f3510..0x004f37d0, 0x004f4bb0..0x004f4cb0, 0x004f5040..0x004f52f0
# Negative ids -> Animations rows, frames and members (about 150 s, bottle X3, read-only)
PYTHONPATH=tools/analysis python3 verification/results/texture-lookup-animations/animation_rows.py \
  > verification/results/texture-lookup-animations/animation_rows_out.txt
# Census rows under the rule (about 4 s, bottle X3, read-only)
PYTHONPATH=tools/analysis python3 verification/results/lod-overlay-batch/texture_lookup_rows.py \
  > verification/results/lod-overlay-batch/texture_lookup_rows_out.txt
```
