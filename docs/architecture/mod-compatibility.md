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

Two follow-ups, not yet implemented:

1. Log, once per session, every created program hash that is in no proxy
   table with its profile and the first material state seen, so a mod's effect
   on coverage is visible in the first minute of a session rather than as an
   option that "does nothing".
2. Match programs by the structural fingerprint already documented in
   [shader-fingerprints.md](../reverse-engineering/shader-fingerprints.md)
   as a fallback when the exact hash misses, so a small `.fx` edit still
   resolves to the same family; keep exact-hash matching authoritative for
   anything that inserts instructions.
