# Farnham's Legacy shader compatibility

Host audit, 2026-10-27, against the current release-1.7.3 source registries.
FL is **not shader-identical to AP**. The new programs are structurally promising
for motion output, but none is admitted by the current exact motion/material
registries. No production changes, game launch, Wine, installation or commit
were made in this audit.

## Scope and effective namespace

[`audit_fl_shaders.py`](../../../tools/analysis/audit_fl_shaders.py) reuses
`effect_passes._archive_effects`, `index_shaders.embedded_shaders`,
`shader_coverage_offline.archive_pairs/census`, `sweep_shaders.gpu_words`, and
`inspect_motion_output_profiles.profile/classify`.
The inputs are `/mnt/c/X3FL-orig`, `/mnt/c/X3AP-orig`, and Mayhem `/mnt/c/X3`.
A temporary symlink view exposes FL's **addon2** as the existing census's addon
layer; the originals are read only. This explicitly avoids the stock AP tool's
failure to inspect addon2. Counts below are effective last-numbered-wins paths,
not a union of every archived revision. No loose shader/shaders files were found
in the selected root/expansion directories; selectable `mods/` are not mounted.

Root plus addon2 is the modeled FL namespace. All **1,176** shader effect paths
in FL's intermediate addon layer occur in addon2, so inserting addon between root
and addon2 makes no difference to these shader results. The executable's actual
mount precedence remains an engine-audit question; these are conditional effective
results, not a runtime observation. Each selected archive effect is hashed after
DAT decoding, and each program includes version, comments and END. Common FNV
identities are independently checked by SHA-256.

## Measured coverage

| Effective set | AP | FL | Mayhem |
| --- | ---: | ---: | ---: |
| Effect paths | 2,352 | 2,352 | 2,352 |
| Technique passes | 4,528 | 4,528 | 4,528 |
| Unique complete programs | 751 | 755 | 751 |
| Unique VS/PS pairs | 817 | 821 | 817 |
| Registered motion pairs | 171 | 141 | 171 |
| Jitter-only prepass pairs | 4 | 4 | 4 |
| Hull-lightmap stage lookup positives (PS) | 100 | 70 | 100 |
| LOD material-strength stage lookup positives (PS) | 100 | 70 | 100 |
| Distance-fade pair lookup positives | 7 | 6 | 7 |

The last three rows execute the actual current C++ functions on a host-built
probe linked with `linear_material.cpp` and `material_motion.cpp`. They prove
lookup recognition only, not transform acceptance, live pair/state eligibility,
shadow caster/receiver ownership, resource lifetime, or visible enhancement.
Motion counts use the real generated pair rows, not a shader-name heuristic.
The census has zero passes missing both programs. The existing census groups
642 AP / 676 FL pairs as `not_analyzed` here because this audit deliberately
supplies no old generator verdict file; those labels do not mean a new FL gap.
Only the 34 new FL pairs receive a fresh generator classification below.

FL and AP share **721** full programs. FL adds **34 ps_3_0** programs and removes
**30 ps_3_0** programs; all **256 vertex programs** remain byte-identical.
There are **1,128** changed effect byte strings at common paths, with no added
or removed effect paths. AP and Mayhem are identical at all these shader paths.
FL has 123 unique SM3 PS versus AP's 119. All other stage/model counts agree.

The changed effect families are Argon, Split, Terran, standard lighting,
Khaak/Teladi/Teladi-nodiff/Xenon (some share identical programs), XT standard
lighting and XT terraformer, including their two-sided forms. Exact aliases and
all 34 identities are in the machine findings; this is shader-family coverage,
not a claim that every object using a similarly named material is affected.

The lost distance-fade registration is
`4944d81dfe531b37 / 64bac8bb307eb896` (`standard_lighting2s`). Its nearest
same-effect removed-AP/new-FL instruction candidate is FL PS
`740346590fe7bff5` (1,518 DWORDs). This pairing is an investigation aid, not proof
that the old fade or material dataflow contract transfers.

## Instruction and shader ABI evidence

No new FL program has an AP twin after stripping comments, nor after additionally
zeroing DEF literals. Thus even the proposed literal-blind
[fingerprint fallback](../../architecture/shader-fingerprint-fallback.md) would not
cover these programs. The source still uses exact lookup: `material_motion.cpp`
`vertex_row`, `pixel_row`, `pair_row` (around lines 730–755), and
`linear_material.cpp` hashes the original before `pixel_for`/`xt_pixel` (around
2340). The fallback design is not an implemented production route.

The existing motion analyzer walks complete instruction boundaries on all 34
new programs. All 34 write only **oC0**, have **no TEXKILL or depth-output writes**,
and leave motion constants **c216–c220** free. Every new PS has its existing,
byte-identical VS partner in the effect pass. Classification produces **34
candidate pairs: 6 A_reference_registers, 16 B_relocated_registers, and 12
C_relocated_registers_with_static_branches**, with **zero blocking reasons**.
Per-pair output/input/temp register choices and exact insertion DWORD boundaries
are retained in `new-program-analysis.json` (`motion_pair_candidates[].plan`).
These are structural generator candidates; the C++ transformer and draw gates
have not been run on new registry rows.

Nearest instruction-sequence comparisons against removed AP programs at the same
effect paths show real arithmetic/operand changes, not only metadata: each has
three additional ADDs, one DP3 and two MOVs in the compared opcode totals.
Most comparisons also add two MADs and two MULs, with DEF changes and broad
operand/register differences; XT has additional declaration/texture-path
variation. Similarity-selected counterparts are explicitly heuristic and do not
establish a lighting equation or semantic cause. The report contains changed
opcode counts only, not original instructions or literal values.

Eight new XT programs exceed the material transform's **1,791-DWORD** input
guard (`linear_material.cpp`, combined original-shading path, around 2339).
The largest is **1,911 DWORDs**, `e57c48fc06198e20` (`xt_standard_lighting2s`);
`6f745daaf928a297` is 1,900 DWORDs. These are complete bytecode lengths, **not
instruction-slot limits**. A table-only extension cannot pass this guard.
The 4,096-DWORD caller scratch size documented in the registry note is larger,
but that alone does not prove downstream transformer bounds or register safety.

## Next bounded validation and ownership

1. Motion: use `inspect_motion_output_profiles.py` to generate reviewed FL rows
   for `src/renderer/motion_output_profiles_inc.h` while preserving AP rows.
   Run host transform/corpus fixtures with the new plans, off-path byte identity,
   mutation rejection, register/declaration linkage and rollback checks. The
   current candidate classifier is sufficient to start that implementation; it
   is not sufficient to declare TAA or shadow replay working in FL.
2. Hull lightmap, original-shading shadow share and LOD strength: reconstruct
   offsets/dataflow with `inspect_linear_materials.py` and
   `inspect_xt_materials.py`; update the owning derived profile notes and
   `linear_material.cpp` Pixel/pair contracts / `linear_xt_profiles_inc.h` only
   after review. Prove the actual light-map term, lobe sum, sun-colour seeds,
   texture stages, temp/constant liveness and all new input-length bounds.
   Raise the 1,791 bound only with that proof. Host transform tests must exercise
   gained/ungained, strength, fade and combined variants, not merely stage lookup.
3. Distance fade: re-prove the changed standard-lighting pair before extending
   `linear_distance_fade_pair` and its transformer. The six unchanged asteroid
   fade pairs remain recognized; their live route still needs engine validation.
4. After host review, native D3D/CrossOver creation/execution and user-run flight
   validation remain necessary. No draw/lifetime/hook safety is inferred from
   shaders. Route any unresolved engine ABI or hook-liveness contract to the
   parent's `disassemble_deep` owner.

## Reproduction and evidence

```sh
python3 tools/analysis/audit_fl_shaders.py
PYTHONPATH=verification/probe python3 -m unittest \
  verification.analysis.test_motion_output_profiles \
  verification.analysis.test_shader_coverage_offline \
  verification.analysis.test_shader_sweep
python3 verification/results/fl-port/shaders/validate.py
```

Derived results, producing-script copy, host probe source embedded in that script,
and test log are under
[`verification/results/fl-port/shaders/`](../../../verification/results/fl-port/shaders/).
`summary.json`, `ap.json`, `fl.json`, `mayhem.json` and
`new-program-analysis.json` contain hashes, aliases and structural facts only.
Raw programs remain **local and untracked**, at
`/tmp/x3-fl-port-shaders/{ap,fl,mayhem}/{vs,ps}_<hash>.bin`.
The affected host checks ran **58 tests, 57 passed, one skipped** (the existing archive override test reports
`no installed archives` at its default location; this audit used explicit paths); no Wine or D3D creation was performed. The host C++
lookup driver compiled with `-std=c++17 -O2 -Wall -Wextra -Werror` and exited 0.

### Audit review corrections

The audit now validates resolved destinations before creating directories or
writing: raw/output roots may not overlap any of the three game trees, including
symlink aliases, or each other; raw assets remain outside the repository.
Compact provenance includes the direct extraction/semantic Python dependencies
and the host probe's transitive local C++ include closure (30 source files).

`validate.py` independently recomputes stage/model totals from raw version words,
FNV and SHA-256 identities, pass/class occurrence totals, coverage from the current
motion/prepass registries, material lookup totals from probe records, FL/AP/Mayhem
set comparisons and new-program classification summaries. It calculates the
reported raw-program total from records. This checks internal evidence consistency;
it does not replace execution of the material transform or live draw admission.
`review_checks.py` exercises 12 invalid destination combinations, a valid disjoint
case, CLI refusal before directory creation, required provenance dependencies,
and rejection of seven deliberately corrupted headline totals. Reproduction:

```sh
python3 verification/results/fl-port/shaders/review_checks.py
python3 verification/results/fl-port/shaders/validate.py
```

## Jitter coverage inside the high-quality effect directory, 2026-10-28

The `3_0` directory label is not a shader-model guarantee. Filtering the audited
FL archive to this directory still leaves **29 depth-candidate pairs** whose
vertex program lacks a current motion/prepass jitter row: **17 SM2.0, three
SM1.1 and nine SM3.0**. The nine SM3 pairs are bloom aliases; the lower-model
pairs include effects, engines, planets/moon, particles, GUI and nebula programs.
These are archive candidates with dynamic/inherited depth settings, not evidence
that all of them write depth in the main scene.

Consequently, proving selection of the high-quality directory alone cannot close
FL TAA coverage. The remaining engine investigation must establish actual pass
state and scene placement for these families, or qualify jitter for reachable
writers. This also preserves the distinction between an unchanged AP shader and
an established FL rendering contract. The pre-existing directory/model distinction
is documented in [effects and archives](../effects-and-archives.md).

Reproduction and input hashes:
`verification/results/fl-port/motion-depth-selection/profile_directory_census.py`
and its adjacent JSON. The producer reads the audited archive metadata and the
current motion/prepass registries, reports the individual pairs and asserts the
model counts; it does not execute the game or extract new shader bytes.
