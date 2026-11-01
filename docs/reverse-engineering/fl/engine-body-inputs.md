# Farnham's Legacy engine-body generator inputs

Offline input-contract audit, 2026-10-27. Image SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`,
PE32 x86, preferred base `0x400000`. Addresses are preferred-image VAs.
This qualifies input requests and well-formed table interpretation for a future
port of `tools/effects/engine_bodies.py`; it neither ports that generator nor
asserts native runtime texture winners. No game or Wine execution was used.

The [resource resolver](resource-resolver.md) and
[resource callers](resource-callers.md) remain the owners of
mount precedence, selected-payload failure and body payload dispatch. The
[fog palette policy](../../architecture/farnhams-legacy-fog-texture-context.md)
is a separate artistic recipe and does **not** authorize copying its DDS-only
input policy into the engine-colour feature.

## 1. Typed tables and parser boundaries

All four inputs below call `0x487d40`. It concatenates literal prefix
`addon2\types\` at `0x58cac0` with the supplied string through `0x4b2c40`
(the latter appends the two strings at `0x4b2d0d/0x4b2d1b`). The prefix is
not a printf template. The buffer constructor call at `0x487dc7` uses
`pck txt` (`0x58cad0`); these callers pass both option arguments zero.
This is extension-aware loose-first selection, then highest mounted package
containing an accepted extension, then extension rank within that package.
It is not `types/` namespace flattening. The failure byte at `0x487e13`
causes destruction and null return at `0x487e1c/0x487e21`; it supplies no AP,
root-types, or lower-package retry.

| Table | Caller and name witness | Required key stem |
| --- | --- | --- |
| Bodies | `0x43f644` pushes `0x585efc`; `0x43f660` calls typed reader | `addon2/types/Bodies` |
| BodyData | `0x4871aa` pushes `0x58cadc`; `0x4871d8` calls reader | `addon2/types/BodyData` |
| Animations | `0x5125e0` pushes `0x591448`; `0x51260e` calls reader | `addon2/types/Animations` |
| Materials | `0x5116e0` pushes `0x59140c`; `0x51170e` calls reader | `addon2/types/Materials` |

Selection, selected-byte read, unpacking, table parsing and body/image decoding
must remain separate failure stages. Use `Resources.resolve_stem(...,
('pck', 'txt'))`; read/unpack only its winner. Hash the decoded table and retain
its complete request/winner provenance. A selected unreadable or malformed
higher table is not permission to search a lower table.

`0x507720` reads string tokens using `0x506de0/0x506e80`. Semicolon is consumed
at `0x506f9d..0x506fa3`; slash dispatches to comment-through-newline handling
at `0x506f4d`. That applies to a **single slash**, not just `//` headers.
String-token mode preserves internal spaces; enum mode differs. Consequently
resource-key slash normalization does not imply that a forward slash inside a
text-table token is equivalent to a backslash. Preserve empty BodyData filename
fields. Existing AP helpers that discard all empty tokens or only strip `//`
are not general native-parser substitutes. The verifier's strict readers cover
well-formed shipped tables and reject count/section ambiguity; they do not
model malformed input recovery, allocation failure, numeric overflow, or all
native whitespace corner cases.

## 2. Bodies sections, numeric IDs and registered names

`0x43f731..0x43f87e` reads section enum, declared count, then exactly that many
string tokens, registering each through `0x486c60` at `0x43f822`. It stores the
count followed by IDs in the selected section vector (`0x43f7ea/0x43f838`).
The enum pairs at `0x576c68` assign `SBTYPE_JET = 6` and
`SBTYPE_SMALLJET = 12` (strings `0x58642c/0x586494`). There are 13 known labels.
The next section begins only after the count-controlled loop finishes.

The existing `body_lists` regex/first-match/truncate behavior is reusable only
for validated count-correct data. A port should reject unknown or duplicate
sections and short/extra sections explicitly, not silently clip them. Native
unknown enums terminate through the reader failure flag; duplicate sections
can replace a stored vector. Neither is a suitable implicit repair policy.

Body registration differs from texture registration:

- Empty or `NULL`/`Null`/`null` yields ID zero. A leading digit or `-` invokes
  `%d` scanning at `0x486d13`; successful conversion returns a **32-bit** value
  at `0x486d20`. Thus `566suffix` is numeric 566, `+566` is a name, and `-x`
  is a name after failed conversion. Do not apply the current full-digit-only
  Python regex to arbitrary mod input.
- Otherwise registration searches record names case-insensitively
  (`0x486d61`) and stores a new exact string via `0x4866e0`. It does not strip
  filename extensions or collapse namespaces. ASCII names are the audited
  comparison subset; non-ASCII locale equivalence is not established.
- Manager `*(0x5aa100)` has base count `+0xb4 = 11000` set at `0x48a5f7`,
  dynamic count `+0xb8`, records at `+0xbc`, stride `0x1c`, name at `+0xc`.
  Fixed slots below 1000 map directly; remaining fixed slots map to ID
  `slot + 9000`; dynamic IDs start at 20000 (`0x486ea8/0x486eaa`). Runtime
  registration order is not available to an offline generator.
- The getter maps IDs below 1000 directly, below 20000 by subtracting 9000,
  and dynamic IDs by subtracting 20000 and adding base count. It bounds-checks
  the resulting slot. An unnamed valid slot formats `v\%05d`; invalid/empty
  paths retain the already documented exceptional fallback boundary.

For offline fixed numeric input, canonical ranges 0..999 and 10000..19999
can be mapped without guessing dynamic state. Native alias IDs 9000..9999
are a distinct case, and negative/out-of-range/dynamic numeric references
need explicit refusal or separately supplied state. This is an offline
acceptance boundary, not a claim that registration rejects those integers.

**BodyData is a required dependency.** Constructor `0x48a643` initializes the
body table through `0x487100`. BodyData has a declared row count, then five
fields per row: numeric body ID, three integers, filename. The integer reads
are `0x4872cb`, `0x48731a`, `0x487330`, `0x487347`; filename read and setter
are `0x48735f/0x487372`. Nonempty names override the getter for fixed numeric
IDs; empty names leave numeric formatting. Its absence leaves initialized
unnamed slots, but that absence must be fingerprinted because a new table can
change names. A body key/identity must remain distinct from its resolved name.
Shipped FL has 419 BodyData rows and no named overrides for its 22 numeric
jet IDs; mods can change this. Always requesting `v\%05d` would miss that
contract even though this clean snapshot happens to agree.

## 3. Animations and texture registration

`0x5125c0` reads the table count into signed 16-bit `0x5db3fc` and allocates
records of **0x44 bytes** at `0x61b414`. Type table `0x57fdf0`, row flag table
`0x5800b0` and frame flag table `0x580018` exactly match the existing AP
`lod_atlas.TAT` and `ANIMATION_FLAGS` values. The well-formed grammar is:

1. Row type, row flags, first texture, second texture; four coordinates if
   `TADF_COORDS` (bit 2) is set. Both texture strings register through
   `0x511ff0` (`0x5126db/0x512751`), stored as shorts at row `+6/+8`.
2. `TAT_MOVIE` (4): seven integers. No ordinary frame count/duration follows.
3. Other types: frame count, frames, then trailing total duration. Ordinary
   frames contain flags, texture name, duration, and two coordinates when
   `TATF_COORDS` is set. They occupy 16 bytes: flags `+0`, texture `+2`,
   duration `+4`, coordinates `+8/+0xc`. Duration is stored/read as a signed
   short (`0x512934/0x51296c`).
4. `TAT_TAGCOLLECTION` (8) frames instead contain name plus six integers
   (14-byte records); `TAT_TAGSINGLESTEP` (11) frames contain only name
   (4-byte records). All these texture names still register through
   `0x511ff0`. Nonmovie trailing duration is read at `0x512b18`, including
   a zero-frame row; ordinary frame duration accumulation can supersede it.

The AP parser therefore gives the correct **names and shape** for the audited
well-formed FL rows. It discards durations and accepts repeated flag names by
summing; a port should preserve row/field evidence and reject ambiguous flags
or malformed counts rather than infer equivalence for arbitrary input. Bound
row counts to the valid signed-short domain and frame allocations to the
remaining input. Movie/single-step semantics are not ordinary static frames.

Negative IDs are verified end to end: face-group material at `0x4dd446`
branches on sign at `0x4dd450`; `0x4dddcc` negates it and `0x4dddd0` calls
`0x512be0`, which checks `0 <= row < count` at `0x512beb/0x512bfa` before
indexing the 0x44-byte row. Thus group **-N selects row N**, not N-1 and not
`dds/-N`. The matching-material search tests effect parameter IDs 1 and 2
and classic texture shorts, defaulting to material zero if no match
(`0x4ddd20..0x4ddda6`). Existing diffuse-only `group_material` is a narrower
artistic selection rule, not a complete port of this native search.

Instance initialization `0x512f27..0x512f5c` selects the first ordinary frame
when present; collection/single-step or absent frame storage uses row first.
That is different from the generator averaging **all listed frames**. The
existing full-level-zero, alpha-weighted texel statistics, equally weighted
frame means, brightest frame peak, colour clusters and geometric size tiers
are artistic recipe choices. They can be retained explicitly without claiming
native timing-weighted colour or current displayed frame. Rows 79/97 both
have four frames, duration 50 each and trailing duration 200 in this snapshot;
that coincidence does not establish equal durations for other/mod rows.

Texture registration recursively removes a final dot plus three characters
only when length **>4** (`0x51208b/0x51208e`), before name lookup/numeric scan.
Leading numeric conversion uses `%d` then keeps the low **16 bits**
(`0x512124/0x512131`), unlike body IDs. Thus `-79.dds` and `-79.tga` reach
animation 79, `65535.tga` becomes -1, and `.dds` stays a named four-character
string. Refuse unqualified signed integer overflow instead of depending on
Python's unlimited integers. This registration transform must precede any
recipe endpoint construction; direct dispatcher suffix semantics remain
separate and retain the reviewed double-suffix behavior.

## 4. Materials, normal endpoints and missing resources

Materials rows have stride `0x3c`; loader reads texture ID at column 12,
flags at column 15, and filename at column 28 (`0x5118e2`, `0x511937`,
`0x511be6/0x511bf9`). The complete 27-entry FL flag table at `0x57ff40`
matches `lod_atlas.MPF`. The existing parser's unknown-flag-as-zero behavior
is not safe for arbitrary FL mods. Keep zero/no-texture, generated surfaces,
invalid table indices and malformed tables distinct from missing files.

Normal endpoint shape is named `textures\<registered-name>` or
`tex\true\<number>` for unnamed predefined rows; native raw-name flags,
font scale and dynamic indices are additional state. The caller note establishes
`0x5115d1` font-scale-dependent index addition and `0x5115e7..0x511602` `_l`
formatting. Table base count 1239 does not bound dynamic indices. For example,
shipped row 158 has `FONTSCALE|READABLE`, so merely choosing the normal name
is especially not a proof of its runtime winner. No new index/context guess
is supplied by this audit. Config +0x774 is `-fontscale`, not texture quality;
see the later [LOD texture audit](lod-textures.md).

The independent normal-endpoint probes are resource/decoder observations:

- Four blue and four purple animation frames select DDS payloads in `01.cat`.
- Normal numeric 155, 158 and 379 select `tex/true/*.jpg`; numeric 153 has
  no candidate at the normal DDS-basename/original DDS/TGA/JPG attempts.
- `effects\others\navjet_diff.jpg` needs the original-name JPG endpoint
  `textures/effects/others/navjet_diff.jpg`. A DDS-only adaptation loses it.
- Authored `D:\X3 MODDIR\dds\unique_argon_M3_diff.dds` successfully reaches
  safe request `dds/unique_argon_M3_diff` after registration suffix removal.
  **An authored name is not yet a resource key.** Do not call
  `Resources.normalize` on that raw name or broaden the resolver to open
  arbitrary host paths. Extract the safe basename request first; if it is
  absent and a later endpoint cannot be represented safely, retain a refusal
  with the raw/normalized/request evidence. Never escape to that authored drive.

There are 23 distinct raw diffuse/frame names in this probe: 3 animation
references, 19 selected and decoded normal endpoints, and the missing 153
normal endpoint. These are not a generated packet acceptance run.
The ratified [engine-colour-normal-v1 recipe](../../architecture/farnhams-legacy-engine-body-recipe.md)
explicitly adopts AP's suffix-placeholder choice on complete not-found;
that is an artistic policy decision, not a result of this native audit. It must not convert a selected read/unpack/decoder failure or
unsafe endpoint into not-found, or claim the placeholder is a qualified FL
native fallback. Its versioned engine policy is independent of fog's recipe-normal policy;
this probe does not execute that recipe or generate its output.

## 5. Implementation boundary and provenance

Reuse pure geometry/statistics/cluster code and validated table grammars;
replace AP `Assets`, `original_assets`, bare `types/` requests, AP body resolver
and AP suffix/namespace-flattening texture lookup with FL adapters. Keep
payload-magic dispatch and explicit refusal reasons. Cache once per resource
view/policy; each cache key must include any input that changes its result.
Do not retain the old first-member-only colour provenance or truncated hashes.

The future generation/check record needs edition, recipe version, ordered
contiguous mounted packages, ordered selected mods, mount stops, exclusion view,
and table requests/hashes; raw body token, numeric identity, resolved name and
body winner/hash; selected material/group rule; raw/registered texture name,
Materials or Animations row, each frame and attempt/winner/hash/decoder result;
all partial reads before a refusal; generated output hashes and output layout.
Record `native_selection_not_asserted`. Changes to failed/missing inputs must
invalidate records as well as changes to successful inputs. Runtime packet
layout and edition-specific paths are separate already-owned contracts.

Overlay exclusion must be an explicit edition-aware source view. Mount the
actual contiguous CAT runs first, then exclude verified owned overlay inputs
without creating an artificial numbered gap. Retain their mounted order and
excluded identities/reasons. Derived selected packages need their documented
original-package mapping at the same ordered position. Do not drop arbitrary
packages based on a filename pattern, infer AP marker paths for addon2, or
silently read a previous generated LOD/texture as the new source. FL LOD/overlay
production and ownership migration remain pending; existing AP
`lod_overlay.original_assets` is not an FL implementation.

## 6. Reproduction and measured coverage

Run `python3 verification/probe/verify_fl_engine_body_inputs.py`; defaults read
`/mnt/c/X3FL-orig` and separately pinned `/mnt/c/X3AP-orig` for comparison.
The producer `verification/results/fl-port/engine-body-inputs/reproduce.py`
retains the command; `summary.json` contains compact derived evidence only. Full successful body
and endpoint rows remain local in `census.json`, which is not tracked. The probe
checks image identity, 17 complete-instruction windows, 77 operand anchors,
five enum tables, typed strings, and negative witnesses without executing
machine code. Window edges also match the shared linear decode from known
`.text` entry; this is instruction-boundary evidence, not CFG coverage or hook
admission. Region hashes bind the reviewed bytes; mutation negatives also
check operands/tables without relying on those hashes.

Clean FL measurements: Bodies jet count 249 and smalljet count 1 produce 249
unique keys, including 22 numeric bodies. **226 parse, 13 are missing, 10 are
refused as MATERIAL3.** Animations has 106 rows and Materials 1239 rows. All
four typed tables select `addon2/04.cat`, with complete winning member names
and hashes retained. Body parsing is not complete generator success: colour
policy and generated output are deliberately not exercised here.

Every one of those **23 body exclusions is inherited**: each occurs in pinned
stock AP's 247-key jet set; AP misses the same 13, and all ten refused decoded
payloads have identical full AP/FL SHA-256 values and the same parser refusal.
The refusal text quotes an AP parser address; it is not new FL MATERIAL3 RE.
No AP resource is ever substituted into FL resolution. Supporting MATERIAL3
is not established as a new prerequisite for preserving the existing feature.

The first final probe indexed FL once in approximately **0.12 seconds** and
finished static checks, body census, separate AP comparison and image probes
in approximately **1.5 seconds** on this host. These are offline diagnostic
wall times, not game performance; exact timings are recorded per replay.
Requests use the indexed candidates, with no per-draw work or runtime changes.
Generator integration under the ratified artistic policy, malformed-mod acceptance
and overlay/launcher/regeneration integration remain separate work.

## Independent review

Bounded deep review found no blocker and reproduced the producer in 1.398 s:
17 windows, 2,139 instructions, 77 anchors, five enum tables, six mutations,
four malformed-section negatives and seven edge witnesses. The 249-body census,
all 23 inherited AP exclusions and ten identical refused payloads reproduced.
Producer/helper hashes and compact/full census linkage matched. Targeted
inspection confirmed numeric/16-bit, slash, override and animation semantics.
Normal endpoint observations remain distinct from native runtime winners.
