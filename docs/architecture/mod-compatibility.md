# Mod compatibility

How content mods interact with the proxy, and what keeps the proxy safe when
they change what the game compiles. Written 2026-09-17 after inspecting the
Mayhem 3 package (`Install_540`, 5.7 GB: eight CAT/DAT pairs under `addon/`,
loose scripts, types, director, maps, textures, soundtrack, a galaxy generator).

## What the proxy keys on

Every transform is keyed on the SHA-256 of the compiled program bytes the game
hands to `CreatePixelShader` / `CreateVertexShader`: the 137 material originals,
the 20 reviewed emission pairs, the nine bullet pairs, the cutout and fade
routes and the sun-share extraction. A program whose hash is not in a table is
drawn native; nothing is transformed and nothing breaks (fail closed). The
game compiles its programs from the `.fx` effect sources in the archives
(`shader\<profile>\<name>`, format string at EXE `0x00563358`), so the program
population is fixed by the `.fx` files: the archive census counts 817 pass
identities, and only 39 pixel / 24 vertex programs have ever been created
across 81 preserved sessions ([effect shader users](../reverse-engineering/effect-shader-users.md)).

## What a mod can change

- **Content only (models, textures, materials, scripts, types):** the game
  keeps compiling the stock `.fx`, so every hash the proxy knows stays valid.
  New materials can select stock techniques that vanilla content never used;
  those programs are inside the 817 archive identities but may be outside the
  137 covered originals, so their draws run native, exactly as uncovered
  vanilla draws do. New materials can also carry blend states the admissions
  refuse (the counters name the reason).
- **Shader sources (`shader\*.fx` in a CAT or as loose files):** the compiled
  bytes change, the hashes stop matching, and every transform for the edited
  programs silently stops applying. This is the one class of mod that disables
  proxy features, and it does so per program, not globally.
- **Executable patches or hook DLLs:** outside this note; the proxy's own hook
  sites are byte-verified at install and refuse on a mismatch.

## Mayhem 3 (`Install_540`)

The eight CAT listings (`05`–`12.cat`, rolling-XOR decoded, 6,152 entries)
contain no `shader` directory, no `.fx`, `.fxo`, `.psh` or `.vsh` entry; they
hold `objects/` (3,566 entries), `dds/` (2,218), `s/`, `tex/`, `l/`, `addon/`
and `types/`. No loose `shader` directory exists in the package. The only DLL
is `ZMap/Newtonsoft.Json.dll`, a dependency of the galaxy generator tool, not a
game hook. Conclusion: Mayhem 3 does not touch shaders; every proxy feature
keyed on stock programs applies unchanged, and its new bodies can only reach
stock programs, some of which may be outside the covered 137.

## Mayhem update package (`x3-mod-example-2`)

A 124 MB Mayhem/Zero Hour update: one CAT/DAT pair (`addon/12.cat`, 272
entries: `objects/` 222, `s/` 20, `addon/` 15, `dds/` 14, `l/` 1), loose
`addon/scripts` and `addon/t` text files, references and the galaxy generator.
No `shader` directory, no `.fx`/`.fxo`/`.psh`/`.vsh` entry, no game DLL. Same
conclusion as above: no shader is touched; new bodies reach only stock programs.

## Making unknown programs visible

1. Implemented. With `--telemetry` on, the proxy classifies each distinct
   created program once, at `CreateVertexShader`/`CreatePixelShader`, against
   every table it keys on: `src/renderer/shader_population.{h,cpp}` consults
   22 tables and 1810 entries (the motion pairs and their depth-only aliases,
   the material originals with their pairs, palette, sun-share exposure seeds
   and XT rows, the distance-fade and cutout pairs, the fade route, the twenty
   linear-emission pairs, the nine SM1 bullet pairs, the screen-emission
   admission, the scene/bloom signatures, the rigid-position, position-path
   and pixel-coverage profiles and the radiance profiles). Each table is
   enumerated in the translation unit that owns it through a provider
   function, so no hash literal is duplicated. A hash in none of them is
   recorded in a fixed 256-entry session table (no allocation; further
   distinct unknowns only advance an overflow counter) and reported at the
   first Present after it appeared:

   ```
   shader_unknown kind=ps|vs id=<hash> version=<bytecode version token> bytes=<size> tables=<tables consulted>
   ```

   `version` is the raw header DWORD (`0xffff0300` = ps_3_0, `0xfffe0101` =
   vs_1_1). At the 300-frame telemetry cadence, and only when a counter moved,
   one further line gives the session population:

   ```
   shader_population known=<n> unknown=<n> overflow=<n>
   ```

   Counts are distinct programs: the create path already dedupes by hash. With
   telemetry off nothing is classified, recorded or logged, and the
   classification never runs per draw. Host test:
   `verification/analysis/test_shader_population.py` over
   `verification/probe/shader_population_host.cpp`.
2. Not yet implemented. Match programs by the structural fingerprint documented in
   [shader-fingerprints.md](../reverse-engineering/shader-fingerprints.md)
   as a fallback when the exact hash misses, so a small `.fx` edit still
   resolves to the same family; keep exact-hash matching authoritative for
   anything that inserts instructions.
