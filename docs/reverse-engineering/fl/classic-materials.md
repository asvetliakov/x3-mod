# FL classic material registration and LOD conversion

Read-only study, 2026-10-28. Preferred x86 VAs, image base `0x400000`.
FL `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`;
clean AP `/mnt/c/X3AP-orig/X3AP.exe`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Resource selection is explicitly `mods=[]`. No production edit, game launch,
Wine/native execution, build, installation or commit.

This owns the classic extension of [FL texture registration](lod-textures.md).
The [AP classic recipe](../non-effect-materials.md) remains the conversion policy;
its source registration context must be reconstructed independently for FL.
The result supports implementing that inherited domain. It does not justify
feeding a planning view's synthesized `0x02000000` flags into source lookup.

## Established loader contract

Binary loader `0x49ae00` selects MAT3/MAT5/MAT6 at `0x49b115..12e`.
The model material array is at model `+0x58`, count `+0x54`, stride **0x60**
(allocation `0x49b143..14e`, loop increment `0x49b5a8`). MAT6 flags are read
into record `+0x28` at `0x49b198`; bit `0x02000000` selects the effect reader.
The classic branch destroys any prior effect object and clears `+0x5c`
(`0x49b214..223`). MAT5/older initializes flags to zero (`0x49b206`).
This is a retained classic record, not an effect conversion by the loader.

| Record field | Runtime offset | FL binary evidence / interpretation |
| --- | --- | --- |
| index; diffuse texture ID | `+0`, `+2` | `0x49b183`, `0x49b276` |
| ambient, diffuse, specular RGB | `+4..+0x14`, signed words when used | twelve consecutive colour words through `+0x1a`, `0x49b2b9..362` |
| unused word; transparency; self illumination | `+0x16`, `+0x18`, `+0x1a` | transparency is not the ordinary draw's blend selector |
| specular map/value; extra map/value | `+0x1c/+0x1e`, `+0x20/+0x22` | registration `0x49b4a1/4d7`; ordinary classic slots use `+0x1c`, not `+0x20` |
| shininess; strength | `+0x24`, `+0x26` | stores `0x49b370/37d` |
| flags; diffuse strength word | `+0x28`, `+0x2c` | flags are DWORD; strength is a signed word at use |
| cube map/value; bump map/value; light map/value | `+0x2e/+0x30`, `+0x32/+0x34`, `+0x36/+0x38` | draw reads at `0x4e1341..1411` |
| effect object | `+0x5c` | null on ordinary classic branch |

MAT6 registers **six names directly** through `0x511ff0`, with ECX pointing
to the string object and EDX=0, returning the short ID in AX. Calls are
`0x49b26e` diffuse, `0x49b3bb` cube, `0x49b407` bump, `0x49b453` light,
`0x49b4a1` specular and `0x49b4d7` extra. Their destination writes follow
eight bytes later. All six bypass the effect parameter reader's config test
and `_bump` rewrite (`0x4db1a2` / `0x4da910`). **A classic bump name never
gets that effect-only rewrite**, regardless of the global low-bump option.
MAT5 reads numeric shorts instead of registering strings.

The actual binary classic flags are:

1. Start with authored MAT6 flags, or zero for MAT5.
2. Register/read the diffuse ID. If `0 <= ID < Materials count`, replace the
   complete flag DWORD with that diffuse row's flags (`0x49b293..2b6`).
   This includes ID **0**, and names that match a predefined row. Registration
   must occur before this decision; numeric spelling alone is insufficient.
3. MAT5 ORs its flag word after signed-16 extension (`0x49b388..391`).
   MAT6 instead ORs **0x8000** after its extra pairs (`0x49b4f6`). That is an
   engine-supplied bit, not permission for the baker to invent flags.
4. Apply the resulting record `+0x28` flags to diffuse, bump, cube, extra,
   light and specular IDs, in that order. The six `ECX=signed ID`,
   `EDX=record flags` calls to `0x512280` are
   `0x49b55d/569/575/581/58d/599`.

The shared setter's established formula remains
`(old & 0x20000000) | actual_classic_flags | (slot_row_flags & 0xa70118)`;
the final term applies only to predefined slot IDs. Negative/out-of-range IDs
are ignored and zero supplied flags leave the entry unchanged. MAT6's native
0x8000 means an otherwise zero-flags classic still takes the nonzero setter
path. The setter does not clear an already cached texture. These operations
establish first ordinary load inputs under the existing texture note's
assumptions, not all interleaved registration histories.

**AP semantic comparison:** AP `0x482039` calls registration `0x4f4cb0`
with two stack arguments, string then option zero; row overwrite is
`0x48206e..093`, MAT6 OR 0x8000 is `0x482314`.
AP `0x482385 → 0x470d30` applies the same six offsets in the same order;
its setter `0x4f4f50` receives ECX=ID and EAX=flags, and has the same
0x20000000 preservation and 0xa70118 predefined mask. Thus the material
layout/order/flag semantics agree, while the calling conventions differ.
This is operand/data-flow comparison, not inference from similar offsets.

## Text materials and parser provenance

FL text classic loading repeats direct registration, row overwrite, MAT6
0x8000 and six-slot application: diffuse registration `0x49e2bd`, overwrite
`0x49e324..349`, bump registration `0x49e536`, OR `0x49e712`, setters
`0x49e78d/79c/7ab/7ba/7c9/7d8`. No classic bump rewrite occurs here either.
Text switches for additive, two-sided and wireframe are ORed **after** the row
overwrite: `0x49e451` OR 2, `0x49e46f` OR 0x10, `0x49e484` OR 8.

The default/AP `bob1` parser retains its folded MAT6 `flags` representation.
FL `BodyPayload.parse` now opts into `classic_provenance`, preserving authored
flags and post-overwrite switches separately in `text_classic`. The FL classic
adapter can therefore distinguish an authored 0x10 bit, which the row replaces,
from a two-sided switch, which survives it.

Binary conversion still needs a separate equivalence check: merely putting
switch bits into a MAT6 file flag cannot preserve them through predefined-row
overwrite. `Textures.serialise_body` checks the resulting native flags and
refuses a changed result; ordinary AP serialization remains unchanged.
Complete text-preserving output or a qualified equivalent conversion remains
required for those kept text records. This is a concrete output limitation,
not uncertainty in the native instruction order.
MATERIAL3's nearest-global-material path remains outside
the inherited atlas conversion; MAT5 is preserved by the recipe.

## Draw, normal and alpha semantics

The ordinary per-body draw reads the same classic slots as AP
(`0x4e1313..1414`), including the cube-as-diffuse substitution when cube and
bump are both present and the draw option has bit 2. It loads record flags at
`0x4e1948`; ordinary world classics select `standard_lighting` at `0x4e1a0f`.
The cockpit/effects and z-only alternatives remain separate paths.

Bump, cube or light ID nonzero selects **BUMPMAP_LOW**
(`0x4e1a30..1a56`), otherwise **DEFAULT** (`0x4e1a58`); node +0x130 bit
0x100000 forces DEFAULT. A nonzero classic bump sets model +0x50 bit 4
(the tangent declaration) at `0x49b51c..523`. The output effect path selects
BUMPMAP for a positive bump ID, otherwise DEFAULT (`0x4e1865..18ad`);
it cannot select BUMPMAP_LOW by copying the source technique condition.

The classic constant block matches the AP recipe over its admitted values:
self illumination at +0x1a chooses emissive RGB = signed diffuse RGB * signed
self illumination /25500 and diffuse strength zero (`0x4e252e..2653`);
otherwise diffuse strength is signed +0x2c *0.01. Specular strength is signed
+0x26 *0.01 (`0x4e2708..272b`); power is signed shininess+1, or signed
specular RGB sum *100/768 +1 when shininess is zero (`0x4e276d..283b`).
Reflection is cube value *0.04 when cube exists (`0x4e283b..28df`).
The scalar literal values are retained in `static.json`.

The common parameter block sets brightness/contrast/saturation 1 and hue 0
before the effect/classic split (`0x4e1d78..1ef5`). Classic blend admission
uses mask **0x4c7** (`0x4e2eda`), two-sided bit 0x10, point filtering bit
0x100 (`0x4e35f8`), and the part-wrap branch (`0x4e2d6a..d86`). The ordinary
opaque one-sided class records CullMode 3. The recipe's effect parameter
CullMode 2 is also converted to 3: effect semantic 0x11 maps value 1 to 1,
all other values to 3 (`0x4e22e4..22fe`). Effect filter semantics 0xe/0xf
retain the same device-selected anisotropic/linear behavior (`0x4e236c..2431`).
Other ordinary constants use generic setter `0x4d9730` at `0x4e2436`.
Arbitrary negative/high-bit material scalar words require signed normalization;
the current shared recipe uses the parser's unsigned Python values. The real
admitted witnesses do not exercise that discrepancy.

There is no separate alpha texture slot in this ordinary classic path.
Extra map +0x20 is registered and flag-updated, but is not read into an
ordinary classic texture slot. Transparency word +0x18 does not replace
flag-controlled blending. The three FL standard_lighting PS programs retain
`alpha = lerp(diffuse.a, light.a, g_EnableGlow) * vertex_alpha`; texture
registration alone does not determine glow or blending. Blend/alpha-tested
classics remain separate groups under the inherited recipe.

The selected clean FL 3_0 programs are **not byte-identical to AP**:

| Technique | FL PS FNV-1a | VS relationship to AP |
| --- | --- | --- |
| DEFAULT | `ced7edef48059e84` | identical, `494fe349b8bc12ec` |
| BUMPMAP | `1b54cbfa2155b61c` | identical, `4944d81dfe531b37` |
| BUMPMAP_LOW | `3c39d45636d95744` | identical, `4944d81dfe531b37` |

Token inspection establishes the normal operations, not just identities:
LOW normalizes `2*RGB-1` in the tangent frame; BUMPMAP reconstructs x from
alpha, y from green, z from sqrt(1-x*x-y*y), with the same tangent inputs.
The FL quality-colour extension is described in
[the material shader contract](materials.md#changed-colour-law-and-constant-abi).
All three techniques use it. The draw sets `g_MatQualityColor` from node
+0x140 through common code `0x4e4f7e..5191`, after the earlier material split;
its default vector is (0.5,0.5,0.5,1). A classic replacement using the same
FL effect retains this native object-dependent grading. Do not bake an AP
colour law into FL texels or infer a CPU replacement for the quality law.

Common slot fallback bindings are those in the texture note: NONE_GRAY for
diffuse, NONE_WHITE for specular, NONE_BLACK for light, and the profile-selected
NONE_NORMAL / NONE_NORMAL_LOW for bump. Measured clean texels are respectively
(132,130,132,255), (255,255,255,255), (0,0,0,0),
(132,130,132,128), (128,128,255,255). On the normal profile a LOW classic's
missing bump therefore reads the tilted RGB normal, even though the same
placeholder is normally interpreted through alpha/green by BUMPMAP.
The AP RGB-to-swizzled conversion remains appropriate for this case; convert
once and use the explicitly selected profile placeholder. The zero-tangent
assumptions remain unproved, so retain the existing no-tangent LOW exclusion.

## Measured clean body domain

`census.py` repeated the binary census: **1707** selected inventory entries,
**1653** parsed, **54** parser refusals, **19577** material records; **166**
classic records in **62** bodies, **165 MAT6 + 1 MAT5**, with **20** distinct
record shapes. All authored classic file flags are zero. **150** have empty
diffuse names. Across all parsed LOD records, **58** classic records have a
visible direct-index use under the existing host HIDDEN_PART rule.

The inherited policy classifies 151 records as opaque/no-map conversion
candidates (48 visibly used), 10 as blend/alpha kept (8 visible), four as
real-bump kept (one visible), and one as MAT5 kept (visible). These are
material-level classifications, not complete body-bake eligibility; 151
includes one unused opaque textured `v/flasher` record. No visible textured
classic is a no-map conversion candidate in this clean binary census.
This does not remove support for opaque textured classics in selected mods.

| Real witness | Actual flags / behavior |
| --- | --- |
| `p39/ships/paranid_athena_ts`, material 15, LOD 0: 216 faces | 0x8000, empty diffuse, DEFAULT, tangent declaration; useful real mixed-body conversion witness |
| `ships/x3ap/paranid/xtc_paranid_drone`, material 0 LOD 0: 124 faces; material 11 LODs 1/2/3: 224/167/16 | 0x8000, untextured classic among effects |
| `ships/x3ap/usc/xtc_terran_tm`, material 5 LODs 0/1/2/3: 188/168/120/27 faces | same opaque untextured source class |
| `stations/docks/dock9portsdummy`, material 1: 54 faces | 0x8000, empty diffuse, self illumination 100; emissive constant witness |
| `stations/docks/dock5portsdummy`, material 20: 90 faces | `155.jpg`, diffuse row 155 flags 0x8002; additive, kept |
| `v/00517`, material 0: 24 faces | `380.jpg`, row flags 0x8012; additive/two-sided, kept |
| `v/00586`, material 0: 6 faces | MAT5 numeric ID 1163, flags 0x8012; kept |
| `v/flasher`, material 1: 24 faces | `40.jpg` diffuse, `418.jpg` real bump, actual flags 0x8000; LOW and tangent declaration, kept by existing real-bump policy |

Twenty actual source slot requests were host-decoded with the current FL
texture resolver using explicitly reconstructed native flags and
`low_bump=False`. `40.jpg` and `418.jpg` both select 256x256 JPGs in `01.cat`;
`155.jpg` selects its 128x128 JPG there. The census summary retains selected
body hashes, texture provenance/hashes and receipts. No source-binding test
was misreported as an atlas bake.

## Bounded implementation and acceptance recommendation

1. Add an FL classic shape/source context alongside the synthesized effect
   view. Retain original record kind, authored flags, text switches, original
   names, computed diffuse registration identity and actual runtime flags.
   Derive row flags through the **same native registration grammar and first
   predefined-name match** as textures, including ID 0. Do not use the current
   AP `classic_flags` against an AP-shaped FL asset facade.
2. Resolve original diffuse/light/specular/bump names using actual classic
   flags, slot semantics and **no effect bump rewrite**. Use these bindings
   for eligibility, dimensions, pixels, identity and the independent oracle.
   Preserve the inherited keeps: MAT5, animation, blend/alpha, two-sided,
   point/wire, cube or diffuse replacement, real bump, image-changing specular,
   part-wrap, LOW without qualified tangents and the bump-off tilt case.
   Retain the existing per-constant classic grouping and helper filters.
3. Separate **recipe inputs** from authored texture names. CLASSIC_BUMP is an
   internal RGB-to-swizzled request, never a native registration string.
   A native default uses the selected direct placeholder resource and profile,
   not a synthesized name passed through dynamic registration. This distinction
   matters with predefined-name collisions, raw-path flags and `_l` routing.
   The current FL `_resolve(CLASSIC_BUMP)` registers `NONE_NORMAL`; simply
   removing `binding`'s refusal does not fix that mismatch. NONE_GRAY recipes
   need the same explicit default/source distinction. Preserve disclosed AP
   NULL-specular/filter approximations rather than silently changing them.
4. Only the newly emitted effect material uses flags 0x02000000 and the normal
   effect-slot low-bump behavior. Its generated names still need the existing
   FL collision/low-bump/high-index planning. Original source flags must not
   leak into generated output, nor output flags into source receipts. Texture
   loading remains once per material/slot; no per-face lookup is needed.
5. Preserve text switch provenance before admitting fixed-row text classics.
   When generating binary output, bake equivalent effect constants for merged
   groups and preserve native behavior of kept text classic groups explicitly.
   Do not silently round-trip the lossy current record into a wrong binary
   material. A narrow explicit refusal is acceptable until this conversion is
   implemented, but not as the permanent classic-domain policy.

Focused implementation acceptance should extend
`test_fl_lod_atlas`, `test_fl_lod_textures`, `test_bob1`, and
`test_lod_overlay_batch.ClassicMaterials`. Run the affected modules with
`PYTHONPATH=verification/probe:verification/analysis python3 -m unittest test_fl_lod_atlas test_fl_lod_textures test_bob1 test_lod_overlay_batch.ClassicMaterials`. Cover: conflicting authored versus
row flags (including row 0 and named aliases); MAT6 native 0x8000; MAT5 signed
flagword; text switch-after-overwrite counterexamples; classic/effect same raw
bump with the global rewrite on; source raw-path/high flags differing from
output effect flags; direct placeholder versus colliding authored name;
normal/low profile RGB conversion exactly once; alpha keeps; opaque textured
mod fixture, untextured mixed fixture and LOW/tangent fixture; generated
fresh-resource rebind; independent pixel oracle rejecting a corrupted binding.
A real mixed-body witness can use the coarsest Terran TM or Paranid drone
record listed above. Record source receipts, generated hashes, geometry
mapping and inherited error metrics. User and native rendering acceptance
remain later gates; no engine launch is required for this implementation.

## ABI, lifetime, reentrancy and proof limits

Binary entry realigns its stack (`0x49ae00..ae2b`), establishes SEH, retains
ECX input in ESI and EDX stream in EDI. Within the classic loop ESI is the
0x60-byte record; string-return EAX is moved to ECX, EDX is zeroed before
registration, and AX is consumed immediately into the ID field. Temporary
string lifetimes are guarded by SEH state and destruction calls; registration
copies/interns the spelling rather than borrowing that temporary. Registry
array growth can relocate entries, so no table-entry pointer may be retained
across registration. The setter preserves EBX/EDI and conditional ESI, returns
plain RET, clobbers volatile registers/flags and is not an ID-return function.
AP's setter and registration use different argument registers/stack layout.

Shared registry flags, cached texture pointers, global defaults and D3DX
objects preclude assuming thread safety or reentrancy. Texture pressure paths
and COM calls can fail or call out. No claim is made that arbitrary exceptions,
CPU flags or LastError survive a new hook: **no hook span is admitted**.
The boundary checks below establish decoded window edges only, not injection
safety. Original source first-load/cache history, dynamic registration order,
object replacement, arbitrary shader mods, non-3_0 shader equivalence and GPU
pixel/filter equivalence remain separate uncertainties. The existing declared
offline normal-index and NULL policies still apply.

## Reproduction and factual checks

All producers and compact derived results are under
`verification/results/fl-port/classic-materials/` (local/ignored until the
owner explicitly retains selected `.py`/`.json` files). Raw listings and the
full intermediate census remain ignored `.txt` files.

- `python3 verification/results/fl-port/classic-materials/reproduce.py`:
  measured **14 FL windows + 4 AP windows**, **3795 instructions**, **59**
  operand anchors, **28** independently checked FL window edges, six exact
  registration/store and six flag-setter argument triples; exit 0, about 0.25 s.
- `python3 verification/results/fl-port/classic-materials/shaders.py`:
  measured six FL/AP technique programs, five decoded placeholders, explicit
  normal/alpha token checks; exit 0. It retains hashes and semantic checks,
  not shader bytes or token dumps, in `shaders.json`.
- `python3 verification/results/fl-port/classic-materials/census.py`, then
  `python3 verification/results/fl-port/classic-materials/summarize.py`:
  measured census 95.954 s, exit 0; reduction validates counts and 20 real slot
  bindings, exit 0. `summary.json` is the compact result; the producer moves
  the full intermediate to ignored `census-full.txt`.

Documentation-only qualification: factual/source/link review and JSON/hash
validation, without unrelated host-suite or Wine runs. These checks establish
the bounded static and host-input contract, not completed FL classic baking.
