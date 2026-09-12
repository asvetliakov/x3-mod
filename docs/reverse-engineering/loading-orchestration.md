# Loading orchestration: startup, resource lookup, save loading and mesh preparation

Static analysis only, 2026-09-12. No game launch, no Wine run, no writes into the
bottle. All addresses are preferred virtual addresses for X3AP.exe SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab` (image base
`0x00400000`, `.text` `0x00401000`, `.rdata` `0x00532000`, `.data` `0x00573000`).
They are research anchors, not approved patch sites. Decompiler output stays in
`/tmp/x3-loading-orch/` and is not committed.

Reproduce (read-only, existing Ghidra project):

```sh
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
/opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process X3AP.exe -noanalysis -scriptPath tools/analysis \
  -postScript X3LoadingOrchestration.java /tmp/x3-loading-orch/out.txt \
  callers 004bb470 4 -- decompile 004e7590 -- strings 00434e40 -- listing 004bc680 40
```

`tools/analysis/X3LoadingOrchestration.java` supports `xrefs`, `callers`, `callees`,
`decompile`, `listing`, `funcs`, `strings` (printable constants referenced in a
function) and `search`, batched with `--` so the project opens once per run.

## Call-chain map

| Address | Size | Role |
| --- | ---: | --- |
| `0x00402710` → `0x00402780` | 3,943 | Process init; parses command-line switches (`-faststart`, `-skipintro`, `-altdir`, `-load`, `-config`, …) |
| `0x004ec9e0` (from `0x0040283a`) | 3,412 | File-system / catalogue mount: `%02d.cat`, `addon\%02d.cat`, `addon\mods\%s.cat`; `__chdir` at `0x004ed27e` |
| `0x004ed750` | 1,687 | CAT directory parse: `_atol` sizes, `_toupper` normalise, **`_qsort` at `0x004edde0`** |
| `0x004ce080` | 1,518 | Mod enumeration (`addon\mods`, `*.cat`) via find-first |
| `0x0048af70` (from `0x00403497`) | 70 | Graphics/body subsystem init; calls `0x004869e0` only when dev flag `0x800` is set |
| `0x00434e40` (from `0x0040356a`) | **17,843** | **Type-table load**: `addon\types\{Globals,Flight,Videos,VideoLists,Dummies,Bodies,Components,Matches,WareLists}` and `TBullets … TShipsWrecks` |
| `0x0046f450` | 353 | Loads one type file as a text buffer (extension list `pck txt`) |
| `0x00403840` | 2,616 | Main/menu loop; message pump at `0x00403a70` |
| `0x00404cc0` (from `0x004038ba`) | 1,461 | Save-game open/validate (`gzopen` at `0x00404d81`, magic `XSA3`) |
| `0x0043f420` (from `0x004050c9`) | 1,246 | Universe restore: re-runs `0x00434e40`, then `SECT` / `SOBJ` chunks |
| `0x004ab880` | **15,895** | **Script VM / command dispatch** (`SE_XMLImport`, `ReadText%d-%d`, `unknown function`) |
| `0x004863c0` | 1,368 | **Body (model) cache + loader**, extension list `pbb bob pbd bod` |
| `0x00492970` | 1,199 | Cut-scene body loader (`cut\%05`, same extension list) |
| `0x004bd830` → `0x004bb470` → `0x004bc680` | 556 / 1,684 / — | Per-body GPU mesh build → `D3DXCreateMesh` → adjacency/clean/optimise |
| `0x00489da0`, `0x00486b40`, `0x0048a1e0`, `0x00489bf0` | — | Scene attach paths that trigger `0x004bd830` |
| `0x004dc540` | 1,096 | Texture loader (`pck dds`), `D3DXGetImageInfoFromFileInMemory` `0x004dc68b`, cube `0x004dc73d`, 2D `0x004dc841` |
| `0x004f3510` | 944 | Texture-file wrapper (`.jpg`, `.tga`) |
| `0x004f7830` | 2,269 | Font loader (`tga bmp`, `abc siz`) |
| `0x004e9840` | 84 | Generic "load named resource into a buffer" (name + extension list on the stack) |
| `0x004e8e10` | 192 | Resource load = `0x004e8780` open + `0x004e8880` read + `0x004e73f0` close |
| `0x004e7590` | **4,538** | **Name resolver** (loose scan + catalogue search) |
| `0x004d2950` | 268 | `__findfirst64i32` wrapper (builds `<root>\<dir>\<pattern>`) |
| `0x004e7470` | — | Extension ranking (`__stricmp` against ≤10 candidates) |
| `0x004e6f20` / `0x004e6fa0` | — | Per-catalogue `_bsearch` / top-down catalogue scan |
| `0x004e9210` | 204 | Read dispatcher: `fread`, `gzread`, or XOR-`0x33` bounded archive slice |
| `0x004e9360` / `0x004e73f0` | — | Close (`fclose`/`gzclose`) / close + free the file object's strings |
| `0x004e90f0` | — | `gzopen` wrapper (sets flags `|5`) |
| `0x004d34b0` | 355 | Win32 message pump (`PeekMessageA`/`GetMessageA`) |

Global file-system object: `DAT_00606f34` (pointer). Observed fields: `+0xbc` flags
(bit 0 = catalogues usable), `+0xc0` catalogue record array, `+0xc6` last-hit
catalogue index (`short`), `+0xc8` catalogue count (`short`), `+0xd8` path-template
struct, `+0x108` dev/debug flag word (bit `0x800` gates body dumping and the `v\`
preload), `+0x76c` language id.

Catalogue record: `+0x00` entry count (`short`), `+0x0c` `.dat` path (`char*`),
`+0x14` sorted entry array. Entry record is **12 bytes**: `{offset, length, char* name}`.

Path-template struct at **`0x0057c008`** (reached as `*(DAT_00606f34+0xd8)`):
`+0x18 "%s"`, `+0x1c "objects\%s"`, `+0x20/+0x24 "L\%s"`, `+0x28 "S\%03d"`,
`+0x2c "save"`, `+0x30 "save\X%02d.sav"`, `+0x34 "%02d.cat"`, `+0x38 "addon\%02d.cat"`,
`+0x3c "F\%s"`, `+0x40 "addon\mods\%s.cat"`, `+0x58 "addon\T\%d.txt"`,
`+0x60 "addon\T\%06d"`, `+0x74 "tex\%s"`, `+0x78 "textures\%s"`, `+0x7c "dds\%s"`,
`+0x90 "mov\%s"`, `+0xac "addon\types\%s.txt"`, `+0xb0 "addon\types\%s.pck"`.

## 1. Asset preload orchestration

**There is no single "iterate every ship/station type and build its mesh" routine.**
The load is split into a *data* pass and a *lazy geometry* pass.

- The data pass is `0x00434e40`, called once at startup from `0x0040356a` **and again
  from `0x0043f43d` on every new game / save load**. It loads, by name, the files
  `addon\types\Globals`, `Flight`, `Videos`, `VideoLists`, `Dummies`, `Bodies`,
  `Components`, `Matches`, `WareLists` and the type tables `TBullets`,
  `TBackgrounds`, `TPlanets`, `TSuns`, `TDocks`, `TFactories`, `TShips`, `TLaser`,
  `TShields`, `TMissiles`, `TWareE/N/B/F/M/T`, `TAsteroids`, `TDebris`, `TGates`,
  `TSpecial`, `TCockpits`, `TDocksWrecks`, `TFactoriesWrecks`, `TShipsWrecks`
  (string references at `0x00434e76`–`0x004392eb`), each through `0x0046f450`
  (`addon\types\%s.pck` then `.txt`, extension list `pck txt`). It also builds the
  nebula/background names (`environments\nebulae\`, `\nebula_`, `_stars_`,
  `_background_`, `_outside_part`, `_dust_part`).
- Inside that pass it *does* force some bodies to load: `0x004381f7` calls the body
  loader `0x004863c0(id)` and `0x0043586b` calls the cut-scene body loader
  `0x00492970`. The surrounding code at `0x004381c2`–`0x004381f4` is a **linear scan
  over an already-collected id array capped at `0x2710` (10,000)** before appending,
  i.e. O(n²) in the number of referenced bodies (see §"Loop complexity").
- Bodies are cached. `0x004863c0` first probes a **hash table** at
  `*(DAT_00608518+0x14)`: bucket = `(size-1) & (id+1)`, chain walk comparing
  `node[1] == id+1`, value at `node[2]`. On a miss it formats the name (`v\%05`
  fallback, otherwise the name from `0x0046e0a0`/`0x0046df60`) into the
  `objects\%s` template, pushes the extension list `pbb bob pbd bod`, calls
  `0x004e9840`, and parses either the `BOB` binary form (`0x00481aa0`) or the text
  form (`0x00483f20`). So **there is an in-engine model cache keyed by numeric body
  id**, and it is a hash lookup, not a list scan.
- GPU meshes are built per **body**, not per object instance. `0x00489da0` (scene
  attach, 27 call sites) checks object flag bit `0x1000` at `obj[0x4b]`; if clear it
  resolves the body id at `obj[0x50]` through `0x004863c0` and calls `0x004bd830`,
  then sets `0x1000`. `0x004bd830` iterates the body's parts
  (`*(short*)(body+0x10)` of them) and only builds a part whose `+0x3c` slot is
  still null, so the D3DX meshes live in the body structure and are shared by every
  object that uses that body.
- Consequence for the recurring identical 344-mesh blocks in
  [iteration04-loading.md](iteration04-loading.md): identical repeats require the
  **body cache to have been emptied** between phases, because a surviving body would
  short-circuit both `0x004863c0` and `0x004bd830`. `0x0043f420` starts with
  `0x00412d00` and `0x00439530` (both reached only from the two universe-load call
  sites) before re-running `0x00434e40`; those are the teardown candidates. This
  document does **not** yet prove which one frees the body hash — that is the single
  most valuable remaining static question for question 1.
- The only startup routine that enumerates model files wholesale is `0x004869e0`:
  `find-first` over the loose directory `"v"` (`DAT_00555934`), `_atol` of each
  `.pbd` name, `0x004863c0(id)`. It is gated by dev flag `0x800`
  (`0x0048af70`), so on a stock install it costs one failing `FindFirstFileA`.
- Sector change was not located as a distinct preload routine; the evidence is
  consistent with sector change going through the same lazy `0x00489da0` path.

## 2. The file-open / probe pattern

`0x004e8e10` (resource load) = `0x004e8780` (resolve + open) → `0x004e8880` (read /
inflate) → `0x004e73f0` (close + free). The file object is:
`[0]` `FILE*` or gzFile, `[1]` flags (bit0 open, bit1 archive slice, bit2 gz, bit4
progress callback), `[2]` slice offset, `[3]` slice length, `[4]` slice cursor,
`[6..0xb]` name (`std::string`, SSO), `+0x4c` catalogue index.

Resolution order inside `0x004e7590`, for a request `dir\base` plus a
space-separated ordered extension list (up to 10 tokens, split loop at `0x004e761a`):

| # | Step | Mechanism / evidence |
| --- | --- | --- |
| 1 | Split the extension list on `' '` into ≤10 `std::string`s (rank = index) | array of 10 × `0x1c` built by `eh_vector_constructor_iterator` at `0x004e75de` |
| 2 | Split the name at the last `/` or `\` | `0x004ea3a0(2)` over the two-char set `"/\\"` at `0x005649c4` |
| 3 | Build the language variant `"<base>-L%03d"` | `"-L%03d"` at `0x005649c8`, language id `*(DAT_00606f34+0x76c)` |
| 4 | Build the wildcard `"<base>" + "*.*"` | `"*.*"` at `0x00563ce0` loaded at `0x004e7852`, concatenated at `0x004e785b` (order inferred from the register convention, not separately proven) |
| 5 | **Loose scan**: `FindFirstFileA(root\dir\<base>*.*)` then `FindNextFileA` until exhausted | `0x004d2950` → `__findfirst64i32`; loop at `0x004e7965`/`0x004e797e` |
| 6 | Rank every hit: build `<candidate>` for each of the ≤10 extensions and `__stricmp` the found file name; keep the lowest rank. A hit on the language-suffixed name outranks the plain name (separate best `local_258`, sticky flag) | `0x004e7470` |
| 7 | If any loose hit: stop (`0x004e7cda` → `0x004e8629`) | flag `local_266` |
| 8 | **Catalogue scan**: for catalogue index `count-1 … 0`, stop at the first hit | `0x004e7cec`; count `*(short*)(DAT_00606f34+0xc8)` |
| 9 | Per catalogue: heap-copy the name, upper-case it and map `/`→`\` (`0x004ec960`), then `_bsearch` over the sorted 12-byte entry array (`0x004e7d8d`, comparator `0x004e6ee0`) | index is sorted at mount time by `_qsort` at `0x004edde0` |
| 10 | Around the bsearch hit, walk **backwards and forwards** while `_strncmp` of the request prefix still matches, ranking each neighbour's extension | backward walk `0x004e8065`, mirrored forward walk immediately after |
| 11 | Open: archive → `_fopen(<catalogue>.dat,"rb")` at `0x004e87ff` + `_fseek(offset)` at `0x004e8827`, flags `|=3`; loose → path build `0x004e8849` + `_fopen(path,"rb")` at `0x004e8856` | `0x004e8780` |
| 12 | Read: `fread`, or `gzread`, or archive slice = `fread` + byte-wise `^0x33` loop, clamped to the entry length | `0x004e9210` |
| 13 | Compressed resources: gzip header / transformed header detection, then `inflateInit2_`/`inflate` with a **1,024-byte input chunk** (`MOV EAX,0x400` at `0x004e8d55`) | `0x004e8880` |
| 14 | Close: `fclose`/`gzclose` and free the three strings | `0x004e9360`, `0x004e73f0` |

Answers to the specific sub-questions:

- **Maximum `CreateFileA` probes per resource is one.** The engine never opens
  candidate files speculatively; extension selection happens entirely against
  directory-enumeration results and the in-memory catalogue indices. Every game
  `CreateFileA` in this binary comes from the CRT (`0x00527a99`, `0x00527dea` inside
  the CRT open path; the only other reference is `0x0040cc81`). 4,268 observed opens
  therefore correspond to roughly 4,268 resource loads, **not** to probing.
- **The catalogue index is a binary search, not a linear scan.** `_bsearch` over a
  `_qsort`-sorted, upper-cased array (7,015 entries for root `01.cat`). It cannot
  account for the unexplained CPU windows.
- **Negative results are not cached.** A name that is absent from the loose tree and
  from all catalogues costs one `FindFirstFileA` plus up to *N* upper-case copies +
  bsearches (N = mounted catalogue count, 17 in this install) **every time it is
  requested**. There is also no positive-resolution cache: the same resource
  requested twice repeats the whole sequence, including the directory enumeration.
- **The archived `.dat` file is re-opened for every archived resource** and closed
  again by `0x004e73f0`. There is no retained handle for the catalogue payload file.
- After resolution, `0x004e6fa0` re-does the lookup (`0x004e6f20`: another heap
  copy + upper-case + bsearch per catalogue, top-down) to fetch the offset/length
  pair. That is a second full index search per archived resource.

## 3. Save-game loading and new game

- `0x00404cc0` builds the path from template `+0x30` (`save\X%02d.sav`) with
  `slot+1`, calls `0x004e90f0` (`gzopen(path,"rb")`), reads five bytes with
  `0x004e9210` and compares them against `"XSA3"` at `0x0055583c`, then reads a
  count bounded by `0x3e8` (1,000). The 24 of 26 failed `gzopen` calls in the
  instrumented run are consistent with **enumerating the save slots for the load
  menu**: absent slots fail `gzopen` and cost 0.015 s in total. Nothing about the
  gzopen failures indicates a loading problem.
- On a successful load `0x004050c9` calls `0x0043f420`, which:
  1. calls `0x00412d00` and `0x00439530` (teardown/reset candidates),
  2. **re-runs the whole type-table load `0x00434e40`**,
  3. reads the `"SECT"` chunk, a sector count, then allocates one `0x180`- or
     `0x130`-byte sector record per sector and registers it through `0x004efbf0`,
  4. reads `"SOBJ"` chunks and resolves each parent by **hash lookup**
     (`bucket = (size-1) & key`, chain compare) — not a list scan.
- The byte order is big-endian in the stream (explicit byte swaps at `0x0043f55x`).
- The save file is a gzip stream, not XML; `xmlReadMemory` (`0x00499480`,
  `0x004e6b70`) serves the `pck xml` resource class (`0x005616b8` referenced at
  `0x00499545`) and the mission-director/cut-scene data, and measured only 0.406 s.
- **No superlinear pass was found in the save reader itself.** The identified
  quadratic pattern is in the *type* pass, not the save pass (next section).

## 4. The script engine

- `0x004ab880` (15,895 bytes, reached through a dispatch table at `0x004ab86e`) is
  the script command interpreter: string constants `"ReadText%d-%d"`,
  `"ReadText%d-%d-%d"`, `"ReadText%d"`, `"unknown function"`, `"unknown object"`,
  `"SE_XMLImport"`, `"SE_XMLImportText"`, `"SE_XMLImportValue"`. It loads resources
  itself (`0x004adc96` → `0x004e8e10`, extension lists `pck txt` at `0x004adbeb` and
  `pck ` at `0x004adc4a`) and can enumerate directories (`0x004ade9c` →
  `0x004d2950`) with the directory **and** pattern taken from script string
  arguments (`0x004a93a0` twice at `0x004ade7a`/`0x004ade83`).
- **The literal `scripts` does not occur anywhere in X3AP.exe, nor in any shipped
  DLL** (`d3d9.dll`, `d3dx9_37.dll`, `dvm.dll`, `iconv.dll`, `ijl10.dll`,
  `imgdll.dll`, `libxml2.dll`, `steam_api.dll`, `zlib1.dll` — checked for ASCII and
  UTF-16). Neither does `director`. The mounted catalogues contain no `scripts\`
  entries either (`s\` in the CATs is 205 `.wav` sound files, matching template
  `S\%03d`; `f\` is fonts, matching `F\%s`). The loose install has 531 +
  698 `.pck` scripts.
- Therefore the script/mission-director directory names are **not compile-time
  constants**: they arrive as runtime strings (script arguments or parsed data) and
  reach the same resolver. This analysis did **not** locate a startup "parse and
  compile every script file" loop, and the absence of the literal means it cannot be
  found from string anchors. The per-file work, when a script *is* loaded, is the
  normal resource path (`.pck` = gzip-with-transformed-header, 1 KiB inflate chunks)
  followed by interpretation in `0x004ab880`.
- Script execution is **completely uninstrumented**: `0x004ab880` makes no D3D, no
  zlib and no libxml2 calls in its hot loop, so a long script/mission-director
  initialisation appears in the telemetry as a report gap with near-zero API time.
  That matches the two ~28 s windows better than any measured category.

## 5. Mesh preparation (`0x004bc680`)

Exact consumption of the adjacency output, from the decompilation (mesh pointer is
`*(param_1+0x14)`; vtable slots are `ID3DXBaseMesh`/`ID3DXMesh` public slots):

1. `GetNumFaces` (slot `+0x10`) → `n`; allocate **two** `n*3` DWORD arrays
   (`_Dst` = adjacency in, second array = adjacency out), both `memset` to 0.
2. `GenerateAdjacency` (slot `+0x58`) with epsilon `_DAT_00565600` (≈1e-6) into the
   first array, retried up to 2 times.
3. On failure, `ConvertPointRepsToAdjacency` (slot `+0x50`) with `pPointReps = NULL`
   into the same array, retried up to 2 times; total failure frees both arrays and
   returns 0.
4. `GetNumFaces` and `GetNumVertices` again (slots `+0x10`, `+0x14`).
5. `D3DXCleanMesh(cleanType = 3, pMeshIn = mesh, pAdjacency = array1,
   ppMeshOut = <stack>, pAdjacencyOut = array2, ppErrorsAndWarnings = NULL)`,
   retried up to 2 times. Success → `0x004bc9c0` (installs the cleaned mesh), frees
   array1, re-queries face/vertex counts. Failure after two tries → frees array2,
   releases the candidate output mesh and **still proceeds to optimisation**.
6. `OptimizeInplace(Flags = 0x04000000 (D3DXMESHOPT_VERTEXCACHE),
   pAdjacencyIn = array2, pAdjacencyOut = NULL, pFaceRemap = NULL,
   ppVertexRemap = NULL)`, retried up to 2 times.
7. `free(array2)`, `free(array1-slot)`, return 1.

So: **adjacency is a pure temporary.** Nothing downstream reads it, no adjacency
output is requested from `OptimizeInplace`, and **no face remap and no vertex remap
are requested at all** (`pFaceRemap` and `ppVertexRemap` are both literal `0`). The
game therefore does nothing with remap arrays — it relies only on the mesh's own
vertex/index buffers after optimisation.

Implication for replacing `GenerateAdjacency` with a cheaper welded-edge algorithm:
byte-identical adjacency arrays are **sufficient but not necessary**. What must be
identical is the *downstream* result — the cleaned mesh's vertex/index/attribute
buffers and the vertex-cache-optimised order — because `D3DXCleanMesh` with
`cleanType = 3` consumes the adjacency to decide splits. Parity tests must therefore
compare the final mesh buffers (and the `CleanMesh`/`OptimizeInplace` HRESULTs),
not only the adjacency arrays. The existing exact-input adjacency cache remains the
lower-risk option because it reproduces the array bit-exactly.

## 6. Other observations in the startup path

- **No sleeping or polling loop on the loading path.** `Sleep` (IAT `0x005320e8`) is
  referenced only by the CRT low-memory retry helpers (`__malloc_crt`,
  `__calloc_crt`, `__realloc_crt`, `__recalloc_crt`).
- The only message pump is `0x004d34b0` (`PeekMessageA` `0x005322b8`, `GetMessageA`
  `0x005322b4`), called from the main loop `0x00403a70`, from `0x004043b9`, and from
  `0x0049728c` / `0x0049742c` — the latter two sit next to `0x00497160`/`0x00497190`
  which startup calls around the loading screen (`P_InitLoadingScreen`,
  `P_ShowNextLoadingScreen`, `P_ViewLoadingScreen`, `addon\loadscr`). Pumping is
  bounded (drain-until-empty), not a wait.
- **No archive CRC/checksum pass.** No integrity strings, no hashing loop over the
  catalogues; `x3files.xml` holds base64 signatures for a small list of data files
  but no per-archive scan was found in the mount path.
- `0x004ec9e0` performs a `__chdir` (`0x004ed27e`), so all resource paths are
  resolved relative to the game root; it also parses the display/`-language`
  switches and embeds the build stamp `"Nov 28 2017"`, `"15:20:09"`.
- Command-line switches parsed at `0x00402895`–`0x00402c8a` include `-faststart`,
  `-skipintro`, `-altdir`, `-load`, `-config`, `-galedit`, `-runinbg`. `-faststart`
  and `-skipintro` are cheap experiments for isolating menu-time cost and cost
  nothing to try.
- Dev-only work is gated behind `DAT_00606f34+0x108 & 0x800`: the `v\` body preload
  (`0x004869e0`) and the body writer `0x00482fb0` (`%05d.bob`, `%s.bob`). Neither is
  on the normal path.
- Textures go through `0x004f3510` → `0x004dc540`; `D3DXGetImageInfoFromFileInMemory`
  (`0x004dc68b`) is **not** in the current instrumented import set, so a share of the
  texture-helper cost is currently attributed to the outer helper span only.

### Loop-complexity notes

| Site | Shape | Bound |
| --- | --- | --- |
| `0x004381c2`–`0x004381e0` (inside `0x00434e40`) | linear scan of the already-collected body-id array before each append, then `0x004863c0` | array cap `0x2710` (10,000) → O(n²), ~5·10⁷ compares worst case. Real but small in absolute time; it recurs on every game load because `0x00434e40` re-runs |
| `0x004e7590` loose stage | one `FindFirstFileA` + `FindNextFileA` per hit, each hit ranked by ≤10 `__stricmp` with two `std::string` constructions per comparison | O(files matching `<base>*.*`) × 10 per lookup, ~4,300 lookups |
| `0x004e7590` catalogue stage | ≤17 × (heap copy + upper-case + `bsearch` over ≤7,015 entries) + bidirectional prefix walk | O(C·log E); not a linear index scan |
| `0x004e6fa0` / `0x004e6f20` | repeats the catalogue bsearch after resolution | one extra allocation + upper-case + bsearch per catalogue per archived resource |
| `0x004e8880` / `0x004e9210` | 1 KiB `fread` per `inflate` iteration; archive slices additionally run a byte-wise `^0x33` loop over each chunk | 882,627 inflate calls observed ⇒ ~0.9 GB through the 1 KiB path |
| `0x0043f420` | sector and object registration through hash tables | O(1) per record |

## Hookable points and optimization candidates

Ranked by (evidence strength × expected payoff) ÷ risk. Nothing here is implemented
and nothing is claimed to make the game load faster.

1. **Instrument `FindFirstFileA` / `FindNextFileA` / `FindClose`** — IAT
   `0x005321c0`, `0x005321bc`, `0x005321b8`. Pure measurement through the mechanism
   we already own; no behaviour change. Evidence: every resource lookup runs a
   directory enumeration (`0x004d2950` from `0x004e7942`) that is currently invisible
   to telemetry, and Wine's per-enumeration cost is of the same order as its
   per-open cost (4.17 s over 4,268 opens). This is the cheapest way to test whether
   the unexplained ~28 s windows are lookup-bound. Do this before anything else.
2. **Sampling profiler target list.** With `FindFirst*` measured, point a sampler at
   (a) `0x004ab880` (script VM — no instrumented API in its hot loop, the best
   structural fit for a 28 s window with near-zero API time), (b) `0x00434e40`
   (17.8 KB type parser that re-runs on every load, with a documented O(n²) inner
   scan), (c) `0x004e7590` (resolver string churn), (d) `0x0043f420` and its
   callees `0x004421a0` / `0x00442c00` / `0x0044aab0` (universe construction).
3. **Adjacency reuse / replacement at the existing shared-vtable hook.** Highest
   measured category (21.287 s, 6,588 calls). §5 establishes that adjacency and both
   remap outputs are discarded, which removes the "does something downstream keep
   the arrays" risk entirely; the remaining gate is that `D3DXCleanMesh(3, …)`
   consumes the adjacency, so parity must be proven on the cleaned+optimised mesh
   buffers and HRESULTs, not just on the array. Exact-input caching stays the lower
   risk variant; an equivalent-but-faster generator is admissible only with
   buffer-level parity evidence.
4. **Investigate and, if confirmed, avoid the body-cache flush between phases.**
   `0x004863c0`'s hash cache and `0x004bd830`'s per-part `+0x3c` guard mean a
   surviving body costs nothing on the second visit; the identical 344-mesh blocks
   therefore imply a flush. Next static step: decompile `0x00412d00` (103 bytes) and
   `0x00439530` (1,100 bytes) and check whether either walks `DAT_00608518+0x14`.
   Retaining bodies across a load would remove whole recurring blocks rather than
   making each mesh cheaper — a strictly larger win than (3) if the flush is
   avoidable, but it changes engine lifetime semantics and needs careful validation.
5. **Retain the catalogue `.dat` handle.** Every archived resource does
   `fopen` + `fseek` + `fclose` on one of ≤17 `.dat` files (`0x004e87ff`,
   `0x004e8827`, `0x004e9360`). 4,268 opens cost 4.17 s. Safe hook options are limited: the CRT is
   statically linked (no `fopen` import), and `0x004e8780` passes arguments in
   `EAX`/`ESI`, which `AGENTS.md`-era notes already flag as unsafe to detour from a
   guessed prototype. The realistic IAT-level variant is a bounded read-only handle
   cache behind `CreateFileA` (`0x005320cc`) served by `ReOpenFile`/`DuplicateHandle`
   — note that a duplicated handle **shares the file pointer**, so only `ReOpenFile`
   (or a genuine re-open) preserves semantics; `DuplicateHandle` must not be used.
   Measure first; only pursue if `FindFirst*` instrumentation shows opens still
   dominate.
6. **Negative-lookup cache.** There is no negative caching anywhere in `0x004e7590`.
   A resolver-level memo is impossible to add at IAT level, but a *per-directory*
   memo of the `FindFirstFileA` enumeration would be, because the same directory is
   re-enumerated for every resource in it. Gate: mods and loose overrides can be
   added while the game runs; invalidation policy and a kill switch are mandatory.
7. **Texture path coverage gap.** Add `D3DXGetImageInfoFromFileInMemory`
   (called at `0x004dc68b`) to the instrumented set so the 13.976 s helper span can
   be split between inspection and decode/convert/upload.
8. **Low payoff, high effort — do not pursue yet.** Enlarging the 1 KiB inflate
   chunk (`0x004e8d55`) needs a rebuilt loader path with a correctly sized buffer,
   and inflate itself is only 9.3 s. Vectorising the `^0x33` slice loop
   (`0x004e9210`) is a few hundred milliseconds at most.

### What this document does not establish

No loading measurement was taken here and no cause is assigned to the 87–89 s
presentation gaps. The script-VM hypothesis for the two ~28 s windows is a
structural argument (an uninstrumented CPU-bound interpreter on the load path), not
a measurement. The body-cache flush is inferred from repeated identical telemetry
vectors plus the cache structure, and is not yet proven. The wildcard concatenation
order in step 4 of the probe table is inferred from the register calling convention.
