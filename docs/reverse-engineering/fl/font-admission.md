# FL font launch admission

Offline implementation findings, 2026-10-28, for the supported FL 1.3 image
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Addresses are preferred VAs, base `0x400000`. This closes the specific Fonts
caller and localized-selection questions left by the
[resource resolver](resource-resolver.md) and
[density design](../../architecture/farnhams-legacy-text-density.md#6-font-resource-admission-and-truthful-fallback).
It does not implement launcher integration or a DLL admission parser.

## Active caller and table identity

The active FL objects are `addon2/L/x3story.obj`, `addon2/L/x3intro.obj`, and
`addon2/L/x3galedit.obj`, selected from `addon2/03.cat` on clean media. The root
catalogues also contain `L/...` counterparts. Their presence does **not** make
them the FL callers: those older objects request `types/fonts.txt`, while all
three active FL objects request **`addon2\types\fonts.txt`**.

| Active object | Decoded SHA-256 | B3D_OpenFont CODE offsets |
| --- | --- | --- |
| x3story | `dbb732716e1acab6b8cc7f87f5d31324fb4764dfc22d1cec808749a4f45ec7b9` | `0xd804`, `0xd81d` |
| x3intro | `e960938cce8d0a5630d7d280c683c3213e53bb87254006c6bf5589f879d5eb35` | `0x705e`, `0x7077` |
| x3galedit | `329c93c0af62ff408bb04259719e9ac012d6fd45fd65283f6e0dd91172f34f1e` | `0xd2c8`, `0xd2e1` |

Every listed call pushes cell width16 and raw flags`0xa1`; size/y-offset/family
come from LARGE/HUD. Five-argument KC forwarding at `0x4b2372..0x4b2394` passes
the request unchanged to the ordinary native opener. The native cockpit tuple
remains Tahoma13/cell16/yoff1/flags`0x21`.

KC object reader `0x4b8e20` formats descriptor `0x5a5ca0+0x20` =
`addon2\L\%s` at `0x4b8e55`, constructs extension `obj` (`0x58e1e8`), and calls
the extension reader at `0x4b8eef`, user-directory flag zero. Thus **loose and
localized OBJ overrides matter**, as do selected mod OBJ members. Preparation
uses this same extension policy and refuses any effective active object whose
decoded hash is not the qualified one. It does not assume a mod changes only
the Fonts table. Other unqualified dynamic requests remain native runtime
fallback, not universal custom-script support.

SE_ReadFile is string-table `0x5a6288` index109, string `0x58ead0`, dispatch
`0x4cc138[109] -> 0x4ca5e8`. The caller splits the supplied name at its final
dot through `0x4cd110`; the extension helper `0x4cd3d0` prepends literal `pck `
(`0x58ee38`) to `txt`. `0x4ca72e -> 0x506350 -> 0x505e00` passes the original
stem and `pck txt`, with user-directory flag zero for the one-argument KC calls.
There is no namespace insertion or fallback in this chain.

The preparation parser accepts the witnessed version1 table subset: exactly
one LARGE and one HUD row, each `role;family;size;yoff;`, with comments beginning
`#` and blank lines. Unknown/duplicate/missing rows, extra fields, unsupported
whitespace and numbers refuse. Comment bytes may be Latin-1 (clean Spanish
comments contain non-ASCII punctuation). A missing table never synthesizes the
KC default values. This deliberately narrower parser avoids claiming full KC
string-parser emulation for malformed/custom tables.

## Localized selection

Within a single physical loose directory or a single archive slot:

1. Compare the requested generic stem and the exact current-language stem
   `stem-L%03d`, using `G+0x75c` (`0x504ecc..0x504edb`). Other language suffixes
   do not participate.
2. Any accepted current-language candidate wins over generic candidates,
   regardless of their relative extension ranks.
3. Within the winning variant, the earliest accepted extension wins.

The loose generic rank is `[ebp+0xc]` and localized rank `[ebp-0x18]`.
`0x50504f/0x505058` admits an improved generic candidate only before localized
success; `0x50523c` compares the independent localized rank. Localized publication
sets `[ebp-0x12]=1` at `0x5053a9`. Loose success skips archives at `0x5053e6`.

The archive scan starts from the highest mounted slot. `[ebp-0x5c]` is generic
rank and `[ebp-0x60]` localized rank. The initial candidate and both adjacent
member scans use the same independent-rank/localized-success rule:
`0x505515/0x505520`, `0x505617`, `0x5057a4/0x5057ad`, `0x5058f3`,
`0x505a94/0x505a9d`, `0x505be7`. Success stops lower-slot search at `0x505426`.
Duplicate normalized winning candidates remain an explicit preparation refusal;
native tie behavior for duplicate names is not claimed.

Consequences tested with real synthetic CAT/DAT indexing: localized TXT beats
generic PCK in one layer; loose generic PCK beats an archive-localized TXT;
generic TXT in a later catalogue beats localized PCK in an earlier catalogue;
other-language candidates do not displace either. Payload failure does not retry
a lower source. `tools/fonts/fl_admission.py::localized` adds this policy over
the existing `Resources` mounts/index/path pinning; it does not duplicate the
mount resolver or change its general conservative public methods.

## Actual font atlas dispatch, beyond the initial gate

The `tga bmp` font lookup is a gate, not necessarily the image ultimately read.
The loader formats extensionless `F\<family><size>` through descriptor`+0x3c`
at `0x514a9d/0x514aa7`, then opens `tga bmp` at `0x514b4d`. It compares the
selected extension with literal `bmp` (`0x514bb2/0x514bb9`). A BMP winner enters
the separate bitmap decoder. The TGA arm at `0x514bdf -> 0x514c6a` closes the
gate and passes the **original extensionless name**, still at `[ebp-0x23c]`,
to the general texture dispatcher (`0x514c77/0x514c89`). It does not force the
selected TGA bytes into that dispatcher.

As established by [resource callers](resource-callers.md#texture-names-and-ordered-attempts),
the relevant dispatcher attempts, in order, are:

1. `dds/<basename>` with `pck dds`: descriptor`+0x7c`, call`0x510a20`.
2. Original `F/<name>` with `pck dds`: call`0x510a39`.
3. Original `F/<name>` with `tga`: call`0x510a53`.
4. Original `F/<name>` with `jpg`, reached only if the TGA decoder fails.

Non-null reader results exit at `0x510a2c`, `0x510a45`, and `0x510a5f` to
`0x510c04`. The DDS wrapper's `pck dds` string is`0x5908d0` (push`0x4f52db`,
extension-open call`0x4f534d`); the TGA wrapper's `tga` string is`0x5908e0`
(push`0x4f7621`, extension-open call`0x4f7680`). Thus an earlier DDS candidate
in a catalogue can replace a later loose TGA. Loose precedence is local to an
attempt, not a reordering of the entire dispatcher.

`fl_admission.font_atlas` now models both stages using `localized` and the
existing `fl_resources.texture_attempts`. It requires an initial TGA winner,
then refuses **any effective candidate in an earlier DDS/PCK attempt**, even
if those bytes might fail native decoding. That decoder/fallback behavior is
not qualified by admission and is not assumed to reach TGA. BMP gates also
refuse. The final TGA winner must match the initial gate winner and its actual
bytes must meet the existing source/generated verification. Missing initial
gates cannot be bypassed by a DDS-only font.

This applies to both original source atlases and every candidate generated
density. Freshness validation repeats all these selections. The reviewer
witnesses `dds/Tahoma26.dds`, `f/Tahoma26.dds`, `f/Tahoma26.pck` and
`dds/Tahoma13.dds`, added after preparation, now all refuse instead of leaving
an unchanged admission record. Regression coverage also checks72 combinations:
three sizes13/26/39, two directories, two DDS/PCK suffixes, generic/current-language
variants and loose/numbered/selected-mod layers. Other-language candidates and
later JPG attempts do not displace a valid TGA. No core flag rule or record
schema change is needed; the check's admitted domain is tightened.

## The two raw flag tuples on generated ABC fonts

The native name-only cache at `0x515445..0x515486` would share HUD and cockpit
Tahoma despite raw flags`0xa1` versus`0x21`. Requiring exact raw flags for every
same-name admission would incorrectly reject all clean languages.

The loader saves full raw flags at object`+0x40` (`0x514ab8`), then **masks with
`0xffffc030`** before ORing into persistent object flags`+0x14`
(`0x514abb/0x514ac0`). Both requests therefore produce the same effective flags.
Successful ABC parsing exits through `0x514f43` or `0x514f6e` to `0x51516e`.
It does not enter the grid/prebake path: the later raw-argument uses at
`0x51500e`, `0x5150b3`, `0x5150f4` and `0x51512d` are not on the ABC path.
Both requests also lack the grid-prebake bit`0x10`.

The glyph renderer combines per-draw flags with object`+0x14` at `0x515856`;
measurement does the same at `0x516156..0x51615b`. Neither uses saved`+0x40`.
The inspected font subsystem and direct table wrappers do not read this saved
field. The stored raw flags differ, but ABC source coordinates, code map,
metrics, effective draw flags, and measured/rendered behavior are the same for
these two requests whichever opens first. Bit`0x80` is a meaningful **per-draw**
style flag; that does not make it persistent font state on this ABC path.

`core::cache_compatible` consequently permits only the exact pair `{0x21,0xa1}`
when both `FontEntry.generated_abc_verified` flags are true and **all other tuple
fields, including source/code-map provenance, match**. The field defaults false.
This is not an arbitrary flags mask: other different flags still refuse.
`same_font` and `font_admitted` retain full raw flag comparison. No instruction,
hook, calling ABI, LastError behavior or native state mutation changes here.

Preparation establishes that boolean by matching actual effective loose ABC/TGA
bytes to the trusted generated assets, requiring their installed ownership
manifest entries, checking ABC version5/length/map-index bounds and matching
the complete code-map digest/count to the qualified original source ABC. Both
original ABC and original atlas must match their qualified decoded hashes.
The future DLL parser must independently verify the generated files before
setting the boolean; copying an untrusted JSON boolean is insufficient.

## Preparation record and freshness contract

Schema1 is deterministic ASCII JSON, at most128KiB. It binds canonical root,
edition X3FL, profile `x3fl-1.3-density-abc-v1`, explicit integer language,
ordered selected mods, candidate density set, supported executable hash,
mounted catalogue digests/provenance, effective Fonts bytes/provenance, active
caller provenance, original source ABC/TGA provenance and code map, actual
owned generated pairs/names/hashes/sizes/physical paths, and the three complete
raw request entries. Entries use the C++ `1<<density` mask and a32-byte
SHA-256 provenance represented as64 hex digits. The same source provenance is
used for HUD/cockpit; request role does not artificially split their identity.

Only the canonical four generated families and their qualified size/y-offset
tuples are accepted. Supported custom Fonts tables can select among these
tuples. L081 always refuses before asset lookup; Tahoma yoff2 versus cockpit1
reports a cache tuple conflict. Unknown families, modified sources/callers,
absent pairs, archive-only pairs, localized generated-pair substitutions and
ownership/content mismatches preserve native fallback. No files are installed.

`prepare(root, language=..., mods=...)` takes a fresh Resources snapshot.
`check_current(record, root, language=..., mods=...)` reconstructs it again and
compares the whole canonical record, including source contents. A previously
created Resources instance or installation ownership manifest alone is not a
freshness check. A changed selector, added loose file/catalogue, source target,
source bytes or generated pair invalidates admission. The record self-hash is
corruption detection, not authentication.
Absence of earlier effective DDS/PCK font candidates is also a launch
precondition, re-resolved by `check_current`; it must remain true until launch.

Remaining integration work belongs to the launcher/DLL owner: obtain the actual
CLI/registry language and ordered selected mods; preserve those selections
through process creation; call `check_current` immediately before launch;
publish the bounded record through an agreed Windows-compatible path; map its
profile/root to actual process identity; independently validate loose pair
bytes in the DLL; populate immutable entries and launch-binding/current flags
only after success. No direct launch may trust a leftover record. Concurrent
external resource writes after the final check remain a launch integration
race; the Python function does not lock game resources or authenticate a launch.
WSL evidence paths such as `/mnt/c/...` are host identities, not native Windows
process paths. Integration must establish equivalent canonical installation
identity on the actual launch platform; the DLL must not accept an arbitrary
record root or compare a WSL path literally to a Windows path and call it bound.

## Focused evidence and cost

Reproducer (no Wine/game execution, no installation writes):

```
python3 verification/results/fl-port/text-density/font_admission_evidence.py
PYTHONPATH=verification/probe:verification/analysis python3 -m unittest test_fl_font_admission test_fl_text_density_core.FLDensityCore.test_core_sanitized
```

The first command saves compact derived evidence beside the script:41 native
instruction checks,12 region hashes, three active KC caller hashes and six
font-open sites. It temporarily links clean media into a separate directory and
adds generated assets there; default/L007/L034 each produce three raw entries,
two family assets and four density pairs, while L081 and the four added DDS/PCK
review witnesses refuse. It removes that
temporary directory. No extracted copyrighted bytes are retained.

Measured after the atlas correction: clean preparation214.2–221.6ms and immediate
fresh validation208.7–219.7ms, record sizes10299–10342 bytes. These are offline startup
tool costs, not game loading time or FPS. Reads/indexes happen twice only when
the owner chooses preparation plus immediate revalidation; no per-draw or
per-font-open filesystem work is introduced. Generated files are verified once
per unique family/density during each preparation, shared by HUD/cockpit.
The atlas fix adds two negative DDS/PCK selections per unique font name, using
the same cached directory and catalogue indexes; it adds no payload reads for
absent candidates and no runtime hot-path work.
The core compatibility work is bounded startup-only; the runtime exact tuple
guard retains the existing maximum32-entry comparison. Native Windows execution,
DLL record parsing, actual launch binding and visual acceptance remain unverified.
