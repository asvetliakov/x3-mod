# FL static material textures for LOD atlases

Read-only reconstruction, 2026-10-28. FL image SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`,
`/mnt/c/X3FL-orig/X3FL.exe`, preferred VAs, base `0x400000`.
Clean AP comparison SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
No production change, hook, engine/Wine execution, install or native rendering
qualification. This extends [resource callers](resource-callers.md)
and [engine-body inputs](engine-body-inputs.md), and owns the
texture side of the [LOD migration boundary](../../architecture/lod-overlay-mods.md#fl-migration-boundary-2026-10-28).

**Result:** the ordinary MATERIAL6 texture path is reconstructible. Exact
registration order is unnecessary when its index *domain* or branch outcome is
proved. Fixed Materials rows and names matching those rows provide immediate
real-texture cases. Arbitrary dynamic names are not universally invariant:
indices at least 10000 receive `_l`. AP has the same rule; its existing atlas
baker uses the unsuffixed approximation. Preserving that accepted AP input policy
in a port is distinct from asserting a native runtime winner. This note does
not substitute the engine-colour recipe for the atlas or ratify a new recipe.

## 1. MATERIAL6 registration, IDs and path flags

The existing caller note connects MATERIAL6 to parameter constructor `0x488f40`
and classifier `0x4daa30`. The classifier maps strings as follows:

| Parameter | Semantic ID | Return witness |
| --- | --- | --- |
| `t_DiffuseTexture` | 1 | `0x4dace2` |
| `t_BumpTexture` | 2 | `0x4dacc9` |
| `t_SpecularTexture` | 3 | `0x4dacfb` |
| `t_LightMapTexture` | 4 | `0x4dad14` |
| `t_CubeMapTexture` | 5 | `0x4dad2d` |
| `t_DetailTexture` | 6 | `0x4dad46` |
| `t_OcclusionTexture` | 7 | `0x4dad5f` |
| `t_AlphaTexture` | 8 | `0x4dad78` |

`0x4daf90` receives ECX=semantic ID, EDX=parameter-name string object, one
caller-cleaned stack argument=value string object; AX returns the texture ID.
Its nonzero-ID route requires the parameter name to begin with literal `t`
(`0x4db17a`) and admits IDs 1..7 (`0x4db183..89`). Byte table `0x4db254`
selects the bump case only for ID 2; all other admitted IDs share the ordinary
registration case. Every such call passes EDX=0 to `0x511ff0`. ID 8 is not
another admitted texture registration here. The ID-zero name fallback admits
bump, diffuse, specular, light, cube and detail, using case-insensitive name
comparisons. Do not generalize this fallback to arbitrary string parameters.

Only bump is rewritten: config `*(0x5a83b0)+0xf0 & 0x400` at `0x4db1a2`
selects `0x4da910`, which replaces the **last case-sensitive byte occurrence**
of `_bump` anywhere in the raw value with `_low_bump`, then registers the
result. Helper `0x47dca0` takes ECX=string, three stack words (needle, unused,
needle length), returns position or -1 with `ret 0xc`. It starts at length-5
(`0x47dcb5/0x47dcc3`), compares five bytes using `0x432a40`, then decrements
the candidate pointer (`0x47dce9`). That comparator compares bytes/dwords
without case folding. The pushed middle word at `0x4da990` is not read.
`0x474d10` replaces five bytes with the whole nine-byte replacement, moves
the remaining tail (`0x474e4a`), copies the replacement (`0x474e7b`) and
updates length/NUL (`0x47508c/95`). This establishes both flag-clear and
flag-set support for bounded well-formed byte strings, without execution:
`a_bump_bump.dds` becomes `a_bump_low_bump`; `a_low_bump.dds` becomes
`a_low_low_bump`; `a_BUMP.dds` becomes `a_BUMP`. The final forms include
registration suffix removal. This is neither an all-occurrences replacement
nor a suffix-only rewrite. It is separate from the font-scale branch below.

Registration `0x511ff0`, ECX=string object, EDX=registration option, plain RET,
returns a short in AX. Empty, `0`, `NULL`, `Null`, `null` return zero; a final
three-character extension is recursively removed when length >4. Digit/`-`
starts use `%d` then low 16 bits (`0x512124/31`); failed conversion searches
names. Negative values denote Animations where consumed as animation IDs,
not negative-number DDS files. Arbitrary signed overflow is not qualified.

Names are searched case-insensitively from table index zero through
`base_count + dynamic_count - 1` (`0x512146..19f`), **including predefined
Materials names**, returning the first match at `0x512238`. Thus a name match
in the predefined table is independent of later dynamic registration order;
normalize extensions before this search, but do not strip a Materials row's
stored filename a second time without evidence. A miss appends at
`base_count + dynamic_count - 1` after increment (`0x5121ae/0x5121f0..ff`).
Both counts are consumed as signed shorts; exclude wrap/exhaustion states.
For clean FL, base count is 1239, including 55 named rows (measured).

Texture table `*0x5cb3f4` uses 16-byte entries: name pointer +0, flags +4,
cached texture object +8, auxiliary lifetime field +0xc. Materials records at
`*0x61b418` have stride 0x3c; +0xc is the nonzero-texture eligibility short,
+0x10 flags. The row's texture-ID field is an eligibility test in getters,
not a redirection from index N to another filename index. Unnamed rows use N.

At material use, semantics 1..7 apply material +0x28 flags through `0x512280`
(`0x4e156f`, `0x4e15a4`, `0x4e15d1`, `0x4e15f6`, `0x4e161b`,
`0x4e1643`, `0x4e1664`). Zero texture values are skipped. For nonzero supplied
flags and an in-range nonnegative index, the setter writes:

`effective = (old & 0x20000000) | material_flags | (predefined_flags & 0xa70118)`

The final term applies only to predefined rows (`0x5122b5..dc`). Zero supplied
flags leave the old flags unchanged. Passing zero at registration therefore
does **not** establish zero effective flags. Ordinary effect material flags
`0x02000000` do not set raw-name, FONTSCALE, GENERATED or IMPORTPICTURE.

Loader `0x5113b0` takes ECX=index, caches the returned object at entry +8 and
returns integer success in EAX. Before filenames it requires config +0xec
bit 0x100 and a usable format descriptor; non-generated file loading requires
format descriptor +4 bit 4 (`0x51156f`). GENERATED `0x800000` creates a
surface; IMPORTPICTURE `0x400000` selects runtime logo state. Neither is an
ordinary static file source. No usable device/format is a separate failure.

For the ordinary file branch, let `k = index + 10000` only when font scale
`config+0x774 > 1` and effective flags contain `0x10000`; otherwise k=index.
The original index still addresses the original entry (`0x5113d6`,
`0x5115dc/e0`); k only changes path formatting:

| Entry | Dispatched path |
| --- | --- |
| Named, k <10000 | `textures\<stored-name>`, or raw `<stored-name>` if flag `0x10000000` |
| Named, k >=10000 | `textures\<stored-name>_l`, or raw `<stored-name>_l` with that flag |
| Unnamed | `tex\true\<k>` |

The signed compare and branch are `0x5115e7/ed`; there is no modulo, index
subtraction or unsuffixed retry. **Config +0x774 is `-fontscale`, default 1**,
not texture quality: argument store `0x50a371`, default `0x509ed5`, also
[font-scale evidence](engine.md#text-density-graph-primitives-and-lifetime).

## 2. Proving offline endpoints without process registration order

Supported context must state: selected resource view; successful ordinary
file-loading capability; first-load flags (or equivalent cached state); font
scale; bump rewrite bit; normal encoding/profile if bump defaults are needed;
no runtime logo/generated-surface input or texture replacement. The draw path
has object-specific diffuse replacement (`0x4e5322..0x4e54d1`), so this study
qualifies static authored material inputs, not every object's eventual binding.

Under that context these proofs are useful, without enumerating an execution
order:

1. Numeric nonnegative ID within the selected Materials table, or a registered
   name whose **first** table-name match is predefined: exact index and flags
   can be determined. Check eligibility, generated/import flags and name path.
2. A dynamic name with a justified bound wholly below 10000: use its normal
   path if font scale does not force the high branch. For clean base 1239,
   at most **8761 total dynamic registrations** keeps all IDs <=9999; this is
   arithmetic, not an observed process bound. A body-only or texture-member
   census is not a proof of all registrations. Conversely a justified domain
   wholly >=10000 always selects `_l`.
3. FONTSCALE set and font scale >1 forces the named high branch for every
   admitted nonnegative index, without knowing order. This establishes a
   path, not that its image exists, and is not a recommendation to add flags.
4. Evaluate all possible low/high and raw/prefixed paths under the exact view.
   If selected payload/hash, decoder/normal interpretation and fallback
   semantics agree, endpoint equivalence proves the result despite unknown
   index. Missing attempts must be retained as dependencies. A DDS-basename
   winner can make raw/prefix variants agree, but does not remove `_l`.

The first case already gives actual decoded textures in clean FL, with effect
flags `0x02000000`, font scale 1, ordinary file capability and no prior
conflicting cached entry:

| Raw token | Established index | Selected member in `01.cat` |
| --- | --- | --- |
| `25.jpg` | 25 | `tex/true/25.jpg` |
| `69.jpg` | 69 | `dds/envmap_test.pck` |
| `1039.jpg` | 1039 | `tex/true/1039.jpg` |
| `effects\others\envmap_test.dds` | 69, first predefined name match | `dds/envmap_test.pck` |

Measured counterexamples: dynamic `unique_argon_M3_diff` and
`fx_engine_blue1_diff` each load their real DDS under the low branch but
`NONE_BLACK` under the high branch. `_l` changes the suffix too, so a missing
`..._diff_l` does not fall back to gray. Font scale 1 alone cannot prove the
low branch. This is a narrow unresolved domain, not grounds to refuse every
static material or to request impossible exact process-order replay.

## 3. NULL, NONE, file failure and draw fallback

Keep three separate concepts: no registered texture (zero); an authored name
such as `NONE_WHITE`; and a nonzero texture whose file attempts fail.
`NONE_*` names themselves take ordinary registration/path selection, including
index uncertainty; the device-created fallback globals below are loaded by
direct dispatcher requests, independent of material dynamic indices.

Global initialization `0x4f353f..0x4f3615` passes names `\NONE_WHITE`,
`\NONE_BLACK`, `\NONE_NORMAL`, `\NONE_NORMAL_LOW`, `\NONE_OCCL_DECAL`,
`\NONE_GRAY`, `\NONE_ENVI`, `\ENVI` to `0x510890`. The leading separator
makes `dds/<basename>` the first attempt. Read the winning placeholder asset;
its bytes can be modded. Its existence/content is not a universal constant.

For an effect exposing the relevant handle, final ID zero/no usable texture
selects these defaults (normal successful device state):

| Slot | Native fallback | Witness |
| --- | --- | --- |
| diffuse | global `0x5a9010`, NONE_GRAY | `0x4e537f`, `0x4e54d6` |
| bump | `0x5a9000`, NONE_NORMAL when profile byte +0x8c !=0; otherwise `0x5a9004`, NONE_NORMAL_LOW | `0x4e55a0..b3` |
| specular | `0x5a8ff8`, NONE_WHITE | `0x4e562d..0x4e5661` |
| light | `0x5a8ffc`, NONE_BLACK | `0x4e5742..0x4e575b`, `0x4e584e..65` |
| occlusion | `0x5a9014`, NONE_OCCL_DECAL; forced at nonzero final LOD | node +0x150 compare `0x4e56b4`, branch `0x4e56c0` |

Node +0x130 bit 0x100000 also forces the bump default (`0x4e5566..0x4e559e`).
NULL light additionally disables its associated controls, so preserving a
black image alone is not full effect-equivalence proof. Cube/detail have
additional shader/device behavior outside this atlas-input qualification.
Do not equate absent effect parameters with a universal sampled constant.

For nonzero ordinary texture paths, dispatcher `0x510890` first handles direct
`.jpg`/`.tga` requests, including their retained suffix; then the general chain
is DDS basename (only if a separator exists), original DDS, original TGA,
original JPG (`0x510a20/39/53/6c`). DDS accepts `pck dds`. Within an attempt,
use the FL resolver's selected member; never retry a lower catalog implicitly.
The literal `\Desktop\` substring skips file attempts.

All wrapper-null results enter suffix defaults: `diff` gray; `bump` the same
profile-dependent normal pair; `spec` white; `light` black; `occl` occlusion;
`envmap`/`envi` ENVI; otherwise black (`0x510a7e..0x510beb`). The dispatcher
marks +0x10 bit 0x20000 and adds a reference. This uses the final dispatched
name, not slot semantics: an unresolved arbitrary diffuse name can be black.

**Failure order is not file-existence order.** A wrapper's null also covers
selected read/decode/device failure. The DDS wrapper calls D3DX image-info and
texture creation (`0x4f5416`, `0x4f54ce`, `0x4f5658`), retries the API at most
twice on reviewed failure paths, and has memory-pressure callbacks; it frees
its input and returns zero at `0x4f56d1..0x4f5700`. A null makes the dispatcher
advance to the next *format attempt*, not the next archive in the same attempt.
Image-info failure is not immediately returned: later creation still runs.
The offline baker should preserve its existing conservative policy: selected
read/unpack/decoder failure is a refusal with provenance, not permission to
try a lower package or claim native fallback. Successful host decoding also
does not prove D3DX creation on every device. Qualify ordinary successfully
decodable source images; GPU failure emulation is not needed for an atlas port.

## 4. AP reuse and the actual FL boundary

Clean AP disassembly independently confirms its same high-index test at
`0x4f43a2/0x4f43a9`, font-scale test at `0x4f4378`, and raw flag test at
`0x4f43c6`. Existing [AP texture study](../texture-lookup.md#4-id--path-0x004f4160)
already records this. AP `lod_atlas.engine_path` does not simulate dynamic
indices, predefined-name matches or material flag history; it selects normal
names. Its `lookup` chooses a suffix placeholder on complete not-found,
`Textures` caches by lowercased name, and selected read/decode failures stop.

The AP atlas also deliberately freezes animation start frames, filters and
resamples source mip zero, treats NULL bump as flat, and defaults unresolved
bump suffixes to NONE_NORMAL_LOW irrespective of runtime profile. Its generic
NULL specular tile is black (`null_texel`), while the native slot default is
white; [AP's own binding study](../non-effect-materials.md)
already records white. These are inherited approximation/discrepancy boundaries,
not new FL-only incompatibilities or evidence of native equality. This work
does not fix or expand them. Reuse of an accepted AP approximation must be
identified as such in output provenance, separately from proved native cases.

Required FL differences are the qualified resource view and exact namespaces,
ordered selected packages, body and table winners, FL registrations/flags and
font-scale/profile locations, native per-slot defaults if equality is claimed,
and final LOD +0x150 instead of AP +0x14c. Never use AP Assets canonicalization
or AP files as fallbacks. `engine-colour-normal-v1` averages engine animation
colours and is not the atlas input contract.

## 5. Implementation contract, safety and remaining evidence

`SourceView.resources` from `fl_lod_inputs` exposes the qualified
`fl_resources.Resources`; use `resolve_stem`, the returned selection's
`provenance()`, `winner` and `read()`. A texture adapter must additionally take
raw token, slot semantic, material flags and explicit context. Return a typed
outcome (no texture, selected image, native fallback, unsupported state,
selected-payload failure), with normalized registration name, fixed ID or
proved index domain, table/hash, dispatched name(s), every missing/selected
attempt, payload/hash, decoder, default identity and approximation policy.
Cache by every input affecting the result, not raw name alone. Share decoded
images by winning source/hash. Do this offline once per source/context;
there is no per-draw work.

Narrow native support can ship first for the proved fixed/predefined and
branch-invariant cases above. Ordinary dynamic names can also be resolved
conditionally under an explicit proved low-index domain. Reusing AP's existing
normal-path approximation permits actual hull texture baking with the same
accepted algorithm, but must record `native_selection_not_asserted` and its
specific assumptions; calling that exact native selection would be false.
A bounded compatibility default can therefore use font scale 1, ordinary
effect flags and the existing AP normal-path/start-frame policy. Carry the
user's bump-rewrite bit explicitly and apply the proved last-occurrence rewrite
when it is set; do not restrict the port permanently to the bit-clear mode. Reject GENERATED/IMPORTPICTURE, invalid/dynamic numeric IDs, malformed
tables, unsafe unresolved paths and selected decoder failures. Ordinary dynamic
*names* are not a refusal in that declared policy: resolve their normal paths
and record the index-domain approximation. If the adapter instead chooses native
slot defaults, use white for zero specular and read the selected normal default
in its matching encoding; this changes an inherited AP approximation and must
be an explicit policy choice, not an unlabelled equivalence claim.
No unsupported case should silently turn into the engine-colour recipe.
Animations, MATERIAL3/classic conversion, non-ASCII registration comparison, object replacements, conflicting first-load flags,
GPU failures and runtime pixel/filter equivalence remain separate boundaries.

ABI/lifetime: registration/parameter construction have SEH frames and owned
string temporaries; table growth can relocate records. Loader preserves
EBX/EBP/ESI/EDI and returns EAX success; entry flags and +8 cached pointers are
shared state. Setter does not invalidate +8, so first load can retain a path
chosen by earlier flags. DDS loader aligns its own stack and may call the
memory-pressure callback guarded by `0x62de10`; that is not a general
reentrancy lock. Shared read-size globals, global placeholders and COM objects
preclude assuming thread safety. No hook span or flag/LastError preservation
contract is admitted; callbacks, exceptions and all indirect entrants have not
been qualified for injection. The checked window edges below are disassembly
boundaries only.

## Reproduction

`python3 verification/results/fl-port/lod-textures/reproduce.py` reads the two
images and clean FL resources. It uses the project's PE32 reader, Capstone,
shared boundary verifier, FL resolver and existing host image decoders.
Measured: **17 FL windows, 2759 instructions, 70 operand anchors, 11 strings,
34 independently checked window edges**, one AP comparison window, four fixed
image probes, two low/high counterexample pairs and seven derived bump-rewrite
examples; approximately **0.6 s**
on this host. `summary.json` retains full source hashes and derived results;
validate it by parsing, not a raw output dump. `reproduce.py` and `summary.json`
are compact retainable evidence under `verification/results/fl-port/lod-textures/`;
raw `*.txt` disassembly there remains ignored. These are static/host image
observations, not a game capture, complete registration census, native decoder
validation or completed FL atlas bake.

Independent bounded review accepted this contract with no material findings.
The reviewer reproduced 17 FL windows, 2,759 instructions, 70 anchors, 34 boundary
checks, four fixed-ID/name probes (three distinct image sources), two dynamic
low/high counterexamples and seven low-bump examples. The declared AP atlas
approximation policy is sufficient for implementation; it does not prove all
runtime index domains, animation or device decoding behavior.
