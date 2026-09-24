# Complete installed archive shader sweep

The sweep now covers **all 3,480 compiled effects in the 17 installed numbered
CAT/DAT pairs**, rather than only shaders encountered in captured scenes. Every
effect contains recognized streams: **13,407 occurrences, 751 distinct complete
programs, and 751 successful D3DX disassemblies**. The former 260 empty effects
were a parser gap: extended SM2 uses version 2.1 tokens, displayed by D3DX as
`ps_2_x` / `vs_2_x`. Accepting that version added 228 programs.

This is exhaustive automated instruction inspection of those archive programs,
plus an independent representative review of every normalized family. It is
**not a manual proof of all 751 programs**, proof that every program is executable
on this backend, or a live-pass classification. No runtime shader replacement
registry was expanded. The game was not launched.

## Scope and reproducible evidence

The numbered archive scan includes all root and `addon` catalogues. Their
directory totals match the paired DAT lengths. Shader entries are all `.fb`;
the installation audit found no additional loose `shader`/`shaders` directory.
The two shader-bearing archives contain 2,304 root and 1,176 addon effects.
There are **2,352 distinct virtual paths, 2,349 distinct effect byte strings,
98 basenames and 28 normalized families**. Six profile directories and four
toggle directories are retained in the exact alias mapping. Archive precedence
is not inferred; identical paths in two catalogues remain separate records.

Full-program identity includes version, comments, CTAB/preshader payloads and
END. Hashing only version plus GPU token instructions, with comments removed,
gives **642 code variants**. That extra grouping does not make different
effect/preshader metadata semantically interchangeable. Exact full hashes remain
the correct input for a guarded replacement profile.

The tracked artifacts contain derived facts and hashes, not shader programs:

- [Program inventory](../../verification/results/shader-sweep-inventory.json): all
  751 hashes/lengths, CTAB, declarations, instructions/features, possible output
  dependencies, and capture matches. Complete parameter records exist for 731
  programs, with 7,198 top-level parameter entries; 20 have no CTAB entries.
- [Every archive alias](../../verification/results/shader-sweep-aliases.json): all
  3,480 catalogue/path/effect-hash records and per-program occurrence counts.
- [Family inventory](../../verification/results/shader-sweep-families.json): every
  exact basename, profile/toggle directory, model count and program membership.
- [Unknown dependency list](../../verification/results/shader-sweep-unknowns.json):
  every program/output for which the conservative interpreter does not establish
  complete static dependencies, with reasons.
- [Sweep verification](../../verification/results/shader-sweep-verification.json):
  tool/artifact hashes, disassembly result and synthetic-test evidence.
- [Independent family review](shader-family-review.md): all 28 family purposes,
  representative hashes, confidence and important exceptions.

Raw programs, original full index, extraction manifest, disassembly and logs
remain local under `/tmp/x3-shader-sweep/` and `/tmp/x3-shader-index-expanded.json`.
Extraction checks both SHA-256 and FNV against each archive slice, rechecks all
catalogue effect entries against the index, and refuses raw output inside the
repository or game tree. Inventory checks full extracted hashes and disassembled
stage/model. It records tool source hashes and each disassembly digest.

### Restoring the local corpus

`/tmp/x3-shader-sweep/programs` is named `<stage>_<fnv1a64 of the full program,
16 hex>.bin` and is rebuilt from the installed archives, without Wine, in about
10 s (`index_shaders.py <X3 dir> --output /tmp/x3-shader-index-expanded.json`,
then `sweep_shaders.py extract <X3 dir> --index ... --raw-directory
/tmp/x3-shader-sweep/programs --manifest /tmp/x3-shader-sweep/manifest.json`).
Rebuilt on 2026-09-24 after a `/tmp` cleanup: 751 programs, 751/751 inventory
SHA-256s, index digest identical. The manifest digest differs only because the
merged-LOD overlay catalogues `addon/05` and `addon/06` are now listed; they
contain no effects. Session dumps (`ps_/vs_<fnv16>.bin` in `/tmp/x3-bottleX3-runNNN`)
use the same naming but cover only the programs seen in flight: runs 295-303
hold 62 inventory programs, including both `run_motion_output.py` pins (run299).
`tools/analysis/restore_shader_sweep.py '/tmp/x3-bottleX3-run*'` copies or
cross-checks them. It refuses a name/FNV mismatch or a differing existing file,
and it prints the inventory restored and missing counts and the pin status.
No session has to be kept for the corpus: the archives rebuild all of it. The
D3DX `disassembly/` text needs the Wine disassembler and was not rebuilt.

## Stage/model coverage versus captures

All **57 distinct shader binaries ever dumped in the current local capture
collection match archive programs exactly**. This is a collection-wide match,
not a count of shader uses in the newest session. The remaining **694 programs**
were inspected from archives without requiring the user to visit their scenes.

| Actual token model | Archive VS | Captured VS | Archive PS | Captured PS |
| --- | ---: | ---: | ---: | ---: |
| 1.1 | 128 | 8 | 42 | 6 |
| 1.4 | 0 | 0 | 24 | 0 |
| 2.0 | 61 | 3 | 110 | 2 |
| 2.1 / D3DX 2_x | 28 | 0 | 200 | 0 |
| 3.0 | 39 | 10 | 119 | 28 |
| **Total** | **256** | **21** | **495** | **36** |

## Every family and basename

Only the terminal suffixes `2s`, `_0000` and `_0001` are grouped for this table.
Their meanings are not assumed. Basename spelling such as `teladi_nodiff` and
`xt_standard_lighting_damage` stays distinct. Variant notation expands literally:
**A** = base, `2s`, `_0000`, `_0001`; **B** = base, `_0000`, `_0001`;
**C** = base, `_0000`; **D** = base, `2s`. These rows account for all 98 names.
Every family has all six profile directories (`1_1`, `1_4`, `2_0`, `2_a`, `2_b`,
`3_0`) and all four toggle directories (base, `hueshift_off`, `hue_lights_off`,
`v_lights_off`); exact combinations/catalogues are in the alias file.

Counts are distinct full programs **within each family** and overlap between
families. Captured counts mean matching program bytes were dumped; a shared
program does **not prove that particular effect path/family was loaded**. Purpose
and confidence for every row appear in the independent family review linked
above; race/object assignment remains a name hint.

| Family | Variants | Effect entries | Archive VS / PS | Captured matching VS / PS |
| --- | --- | ---: | ---: | ---: |
| adeffects | A | 144 | 4 / 5 | 2 / 1 |
| argon | A | 144 | 45 / 56 | 2 / 4 |
| asteroid | B | 96 | 30 / 24 | 2 / 2 |
| bloom | C | 72 | 18 / 29 | 3 / 5 |
| boron | A | 144 | 45 / 40 | 0 / 0 |
| effects | A | 144 | 12 / 12 | 3 / 2 |
| engine | A | 144 | 10 / 7 | 3 / 2 |
| glass | A | 144 | 18 / 16 | 1 / 1 |
| gui2d | B | 96 | 4 / 4 | 2 / 2 |
| khaak | A | 144 | 45 / 56 | 2 / 2 |
| moon | B | 96 | 11 / 7 | 0 / 0 |
| nebula | A | 144 | 2 / 2 | 1 / 1 |
| nebulafog | A | 144 | 2 / 2 | 1 / 0 |
| paranid | A | 144 | 45 / 56 | 0 / 0 |
| particles | A | 144 | 2 / 2 | 1 / 1 |
| planet_haze | B | 96 | 3 / 3 | 1 / 1 |
| planet_v | A | 144 | 6 / 4 | 0 / 0 |
| split | A | 144 | 45 / 56 | 2 / 2 |
| standard_lighting | A | 144 | 45 / 81 | 2 / 6 |
| stardust | B | 96 | 4 / 2 | 2 / 1 |
| teladi | A | 144 | 45 / 56 | 2 / 2 |
| teladi_nodiff | A | 144 | 45 / 56 | 2 / 2 |
| terran | A | 144 | 45 / 56 | 2 / 0 |
| xenon | A | 144 | 45 / 56 | 2 / 2 |
| xt_standard_lighting | D | 96 | 13 / 28 | 2 / 6 |
| xt_standard_lighting_damage | D | 48 | 13 / 28 | 2 / 4 |
| xt_terraformer | D | 96 | 13 / 28 | 2 / 0 |
| z_only | B | 96 | 4 / 2 | 2 / 1 |

## What the complete sweep establishes

The automated pass isolates each actual GPU model section, excluding preshader
instructions. It records semantic input/output declarations, texture and matrix
operations, control flow, relative addressing, all saturation sites, RGB/alpha/
depth outputs and CTAB register references. Parameter-name hints for lighting,
emissive, fog, time, matrices, skinning and depth are separate from instruction
dependencies. Per-output dependency sets are conservative possible sources;
they do not establish numerical equivalence, coordinate conventions or runtime
values. The compact tracked inventory retains whole-output and RGB/alpha
summaries; the analyzer can reproduce individual component details locally.

- **1,308 SAT sites in 348 programs** are now inventoried. The five original
  material profiles are only a narrow subset. Additional direct COLOR0 RGB
  clamps occur in glass and other material families; normal/specular, mask and
  alpha saturation must remain distinct. No additional patch is authorized by
  motif matching alone.
- **116 programs have control flow; 67 VS use relative addressing.** The latter
  all use repetition with c0/c1/c2 indexed by a0.w. Representative instruction
  review identifies point-light loops, not bone palettes. All 23 skinning-name
  hints are actually `p_DetailMapBlendWeight`, demonstrating why names alone
  would misclassify this feature.
- **154 of 256 VS position outputs have complete conservative static dependency
  sets; 102 stay unknown.** Particle programs explicitly add billboard XY between
  view and projection. Direct-position GUI/bloom programs have no WVP matrix.
  Four DP4 position writes do not prove an ordinary rigid-mesh input contract.
- No GPU-body screen found VS texture fetch, explicit pixel depth output,
  `texkill`, derivative instructions, or time-named CTAB parameters. These are
  instruction/name observations, not proof that engine-side animation, alpha
  test, depth passes, fixed-function effects or shadows are absent.
- Older planet/moon programs use COLOR varyings under SM2 even in a `3_0`
  directory. Their HDR limitation is different from an explicit SM3 PS clamp.
  Shared scene/GUI programs prevent shader-hash-only scene/UI classification.
- Original bloom has highlight masks and auxiliary alpha, varying sample counts
  across profiles, and scene/glow composition. Its `Exposure` parameter does not
  establish automatic exposure. Fog/haze representative programs are textured
  or lookup-based; the inspected programs do not establish volumetric integration.

## Explicit unknowns and anomaly

The interpreter marks **183 programs** globally unknown for output proofs:
116 have control flow, 66 use unsupported legacy texture operations, 63 have
coissued instructions and one has an unusual destination. Those categories
overlap. In total **490 programs have at least one unresolved output dependency**,
including partial/unused varying lanes and uncertain temporary components.
This is deliberately conservative and does not mean 490 invalid shaders. Every
affected program/output/reason is in the linked unknown list. All 28 families
are represented, but lower-profile/toggle semantics were not manually proved
variant by variant, and runtime pass identities remain unclassified where no
draw evidence exists.

The isolated anomalous program is PS `d66dd16fc0a6c3a3`, SHA-256
`f464a7bb862d808a4f895f8963dc34a6d96731637dc20ea29251ee5259d696f8`, aliased only by
root `01.cat:shader/2_b/standard_lighting_0000.fb`. D3DX returns S_OK but prints
an unusual `x521.y` destination for one DP4. The semantic pass rejects that
destination rather than inventing its meaning. Neither this program nor the
whole collection has been qualified here through backend `CreateShader` or
numerical execution. Disassembly success is a syntax-inspection result, not
proof of driver acceptance.

The archive baseline does not enumerate shaders generated dynamically by the
engine, fixed-function backend programs, future mods/loose overrides, or a
different installation. Those require separate provenance. It does ensure the
current design is no longer constrained to the shader families visible in a
few captured scenes.

## Reproduce without launching the game

```sh
python3 tools/analysis/index_shaders.py \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3" \
  --output /tmp/x3-shader-index-expanded.json
python3 tools/analysis/sweep_shaders.py extract \
  "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3" \
  --index /tmp/x3-shader-index-expanded.json \
  --raw-directory /tmp/x3-shader-sweep/programs \
  --manifest /tmp/x3-shader-sweep/manifest.json
mkdir -p /tmp/x3-shader-sweep/disassembly
i686-w64-mingw32-g++ -std=c++17 -O2 -Wall -Wextra -static \
  tools/analysis/disassemble_shaders.cpp -o /tmp/x3-shader-sweep/disassemble_shaders.exe
'/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine' \
  --bottle Steam --no-update --workdir /tmp/x3-shader-sweep \
  /tmp/x3-shader-sweep/disassemble_shaders.exe 'C:\X3\d3dx9_37.dll' \
  'Z:\tmp\x3-shader-sweep\programs' 'Z:\tmp\x3-shader-sweep\disassembly'
python3 tools/analysis/sweep_shaders.py inventory \
  --manifest /tmp/x3-shader-sweep/manifest.json \
  --raw-directory /tmp/x3-shader-sweep/programs \
  --disassembly-directory /tmp/x3-shader-sweep/disassembly \
  --output /tmp/shader-sweep-inventory.json \
  --aliases-output /tmp/shader-sweep-aliases.json \
  --families-output /tmp/shader-sweep-families.json \
  --unknowns-output /tmp/shader-sweep-unknowns.json \
  --runtime-directory "$HOME/Library/Application Support/CrossOver/Bottles/Steam/drive_c/X3/x3-modern-captures"
python3 -m unittest verification.analysis.test_shader_sweep verification.analysis.test_shader_semantics
```

The standalone disassembler creates no D3D device. The **34 original synthetic
tests** cover model/preshader separation, conservative arithmetic/dependencies,
unknown behavior, archive alias preservation, code/full-hash distinctions,
index completeness and raw-output restrictions. Raw assets/disassembly must
remain outside the repository. The complete current analysis suite also passed
**148 tests**, with no failures. Independent review added a fail-closed regression
for unsupported `sincos` model/arity behavior; none of the 751 archived programs
uses that instruction, and every program's derived inventory remained unchanged.
