# FL font selector readiness at initial display

Independently reviewed static reconstruction, 2026-10-28, supported X3FL 1.3 SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Addresses below are preferred VAs at base `0x400000`; runtime uses admitted
module base plus RVA. This extends [font admission](font-admission.md),
[resource selection](resource-resolver.md), and
[initial display ownership](ui-layout-generation.md).
It supplies the selector prerequisite for the
[runtime handoff design](../../architecture/farnhams-legacy-font-runtime-handoff.md),
not a production reader or an enabled font capability.

## Result

On the qualified normal startup path, language and selected-mod storage are ready
at normal successful return from `0x47765a -> 0x516890`. Both the native-dialog
path and the dialog-skipping path converge **before** selected mods are mounted
at `0x47761d -> 0x50b130`. The constructor and the native dialog can change the
selection earlier. A snapshot at early post-cookie `fl_startup` is invalid.

Read and copy the live selectors at the existing initial-display boundary,
compare them with the preparation's exact language and ordered mod names, and
refuse unavailable, malformed, changed or unsupported input. Do not reconstruct
the live selection from a registry default. Selector equality alone does not
establish mounted-catalogue success or physical root identity; those checks are
specified separately below.

## 1. Exact layout and ownership

`G = *(rebased 0x5a83b0)` is the game configuration/resource owner. Allocation is
**0x7a0 bytes**, `0x509e3c/0x509e46`, with early global publication at `0x509e4d`,
zeroing at `0x509e63`, and member construction at `0x509e6d -> 0x50a940`.
The startup caller republishes the normal returned pointer at `0x476c9e`.

| Field | Exact native representation | Evidence |
| --- | --- | --- |
| G+0x75c | Four-byte language value; signed decimal consumers | DWORD writes `509ec4/50a000/50a15c/50a167`; DWORD argument `504ecc` to `-L%03d` formatting |
| G+0x78c | 32-bit vector begin pointer | Constructor `50a98a`; mount `50b136` |
| G+0x790 | 32-bit one-past-last live element pointer | Constructor `50a994`; append `4d8c92` adds24 |
| G+0x794 | 32-bit allocation-end pointer | Constructor `50a99e`; growth `4d8db6` |
| Each vector element | 24-byte narrow string | Native mount advances24 at `50b270` |
| String+0..15 | Inline byte storage, or heap pointer at+0 | `50b208..50b214`, `4747b8..4747d9` |
| String+0x10 | Unsigned 32-bit byte length excluding NUL | String transfer reads/writes `4747be/4747e1/4747e4` |
| String+0x14 | Unsigned 32-bit capacity; **>=16 means heap** | `50b208/50b20c`; initial capacity15 at `4d8c7c` |

Vector growth replaces all three pointers at `0x4d8db6/0x4d8dc5/0x4d8dc8`.
Move construction transfers heap ownership or copies inline length+1 bytes.
These are borrowed engine objects, not STL objects that a DLL should construct,
copy with its own C++ ABI, free, or retain by pointer. The null triple is an empty
vector; allocated storage with begin=end is also a valid empty vector.

The full-text direct absolute-store census finds six G publications/clears:
`476c9e, 509e4d, 50a082, 50a821, 50afdb, 50b579`. The first two are constructor
publication; the others are constructor/allocation failure or teardown. There
is no second surviving normal-startup incarnation in these paths. `50a9b0`
destroys strings/vector storage, and `50b54c..50b579` destroys/frees G and clears
the global. The catalog-loader failure write at `50afdb` is **zero**, after
testing failed allocation in ESI, not a replacement configuration allocation.
This census does not prove absence of arbitrary indirect writes or foreign code.

## 2. Language inputs and actual precedence

The language's only four immediate-offset typed stores in the full `.text`
census are the four listed above. Initial whole-owner zeroing precedes them.
No language writer was found in the native configuration dialogs or registry
reader; there is no established registry language default.

1. `0x509ec4` initializes language **44**.
2. Before argv processing and before numbered mounting, `509ef0..509f55`
   builds stem **`addon2/lang`**, extension list **`dat`**, and user-directory
   flag0, then calls ordinary extension-open `505e00`. This is not a `pck dat`
   request. The existing localized policy uses the then-current44, so an
   effective loose `addon2/lang-L044.dat` can win over generic `lang.dat`.
   Catalogues are not mounted yet. `509f6a -> 505f40` obtains contents.
   `509fda/509fe0` uses format **`%d`** through the scanf wrapper `412ad0`.
   Only exactly one successful conversion writes the value at `50a000`;
   missing/read/parse failure retains44. Parsed **1 maps to44** at
   `509ff5/509ff8`. The native one-conversion test does not require the whole
   file to be only digits: do not claim a stricter file grammar as native fact.
3. The argv loop compares the real literal **`-language`** (`590e94`) at
   `50a136..50a148`. Comparator `53744d` has an ASCII case-folding path
   `537412..53744c` and a locale-aware CRT path. The immediately following
   argv entry goes to decimal integer helper `52eafd` at `50a14e` and is
   stored as a full DWORD at `50a15c`; **1 again maps to44** at `50a167`.
   The helper passes radix10 at `52eb05`. The outer loop advances one argv
   entry at `50a5e1` and continues at `50a5ee`, so repeated valid occurrences
   are processed in order and the last wins. No positive/range gate surrounds
   the language store. Malformed, missing, overflow and locale-sensitive input
   are outside a launcher predictor's qualified subset.

The actual **`-w`** argument (`590f94`) changes CWD through
`50a579 -> 5319c7 -> 53188a -> SetCurrentDirectoryA` (`5318c7`). It occurs in
the argv loop, **after the initial lang read and before numbered mounting**.
Therefore lang-file default lookup and later resource mounting can use different
directories in a native launch with `-w`. A launcher should refuse conflicting
root-changing `-w` input unless it deliberately qualifies that ordering.

A validated language choice can be conveyed with the real `-language <decimal>`
argument and verified against the live final DWORD. This avoids inventing a new
game switch or using a manual assertion as the source of truth. Appending it
last controls duplicate valid occurrences, but a launcher must still reject
malformed argument sequences. Any policy that predicts the default from files
must use the initial-CWD/dat/localized44 behavior above; integer1's effective
identity is44. The offline evidence does not implement a complete CRT parser.

## 3. Last selected-mod writers and startup branches

| Phase/branch | Effect before initial display |
| --- | --- |
| Constructor `509e6d -> 50a940` | Null vector triple; language defaults and overrides as above |
| `50a048 -> 4d8a60 -> 4d71e0` | Defaults clear live vector: destroy range `4d72a7`, end=begin at `4d72b7` |
| `50a609..50a90c` | Contiguous numbered root/addon2 mount; selected mods not mounted yet |
| Startup `4772d6/4772d9` | G+0xf8 bit8 skips registry reader; otherwise `4772e3 -> 4d78a0` |
| Successful ModName read | `4d7b4b/4d7b59` destroys old range/clears end; semicolon slices appended in order at `4d7be7 -> 4d8c20`; failure retains prior vector |
| Dialog skip | Actual `/noabout` or `-noabout` comparisons `476d20..476d4a` reach `47723d`, set stack byte+0x67; `477556` jumps to `47760e` |
| Native dialog | Loads x3config at `477586`, passes live language to `_Init` at `4775d7`, calls DialogBoxParamA at `4775f4`; low-word zero return reaches fatal exit; nonzero reaches `477609`, then convergence |
| Dialog mod selection | Main callback `4fc360`, control0x4da at `4fc40e -> 4f9a90`, opens callback `4fd550`; accepted selection clears vector at `4fd63f`, appends selected list-box text at `4fd6b1`; no selection leaves it empty |
| Dialog defaults acceptance | `4fc300` calls `4fc230`, then if G+0xf8 bit8 is set, `4fc335/4fc33a` resets defaults via `4d71e0`, clearing selected mods; otherwise `4fc341` writes configuration |
| Both surviving paths | Converge `47760e`, call **`47761d -> 50b130`**, then display call `47765a`; no intervening selector mutation in the decoded straight-line caller |

There are two direct append callers (`4d7be7`, `4fd6b1`), two defaults callers
(`4d8af4`, `4fc33a`), one registry-read caller and one selected-mod-mount caller
in the full-text census. Generic vector helpers write through a vector pointer;
their writes cannot be excluded by looking only for literal G offsets.
The dialog's append path picks a single current list-box choice; do not impose
that UI behavior on the registry path, which preserves multiple names in order.
Both paths supply strings without a `.cat` extension for descriptor formatting.

The normal display return establishes completed native startup ordering, not a
successful mount merely because the vector was nonempty. Neither registry
re-reading nor an early copied vector can account for these dialog branches.

## 4. Mount correspondence and failure qualifications

`50b130` reads the final vector, allocates old-count + vector-length pointer
slots, copies numbered slots, and mounts names forward with
`addon2\\mods\\%s.cat`. It never clears or changes the selected vector. The
following surviving failures make selectors alone insufficient:

- Pointer-array allocation failure: `50b181 -> 50b27c` returns with old mounts.
- Mod-path formatting allocation failure: `50b22c -> 50b26a` skips that entry.
- Catalog payload unavailable: loader `50ab1e/50ab24` returns AL=0 before slot
  publication. The loop advances the selected name but advances the slot only
  on successful loader return (`50b23d/50b23f`). Unused trailing slots are null.

The bounded loader study additionally resolves the old partial-publication
uncertainty for **normal surviving native startup**. Loader slot publication is
early (`50adab`), but subsequent explicit failures at `50ae3e` (DAT-path
allocation), `50ae73` (path suffix) and `50afd1` (member-name buffer allocation)
reach `50b330`, destroying the mounted owner and clearing G at `50b579`.
The last also explicitly writes null G at `50afdb`. The only success return is
AL=1 at `50b127`, after name normalization and sorting. A valid normal display
return with the original nonnull G cannot be the ordinary continuation of those
post-publication teardown paths. Foreign callback repair, corrupted control flow
or exception recovery are not covered.

Minimum additional correspondence check for healthy, unchanged prepared inputs:

| Live field | Check against the already-prepared ordered catalogue list |
| --- | --- |
| G+0xac bit0 | Archive lookup enabled |
| G+0xb6 | **Signed16** count, nonnegative and exactly expected count; do not accept wraparound |
| G+0xb0 | Array of 32-bit catalogue pointers, all expected slots readable/non-null |
| Catalogue+0xc | Pointer to NUL-terminated narrow **DAT path**, not a string object; exact expected ordered path |
| Catalogue+0x10 | Byte user-directory flag, zero for these mounts |
| Catalogue+0 | Signed16 member count; refuse unsupported/nonpositive counts under a conservative admission policy |
| Catalogue+0x14 | Entry-array pointer; entries12 bytes (offset,length,name-pointer) |
| Catalogue+4/+8 | Owned member-name buffer pointer/byte count, published `50afcc/50aff5` |

Exact count plus nonnull ordered slots catches array-allocation failure and skipped
packages without implementing another resource resolver. Structural checks can
reject incomplete objects; there is **no separate initialized-success marker**.
This is not an independent proof that every live index entry equals the current
on-disk CAT bytes, nor that DAT files have not changed. Retain preparation hashes,
freshness and launch immutability constraints; do not advertise arbitrary corrupt
catalogue/OOM recovery. Expected ordering comes from the existing resolver,
including duplicate selected names if admitted, not from sorting names.

## 5. Physical root and config-object qualification

Descriptor `G+0xc8` supplies relative paths, not a canonical installation root.
G's string fields at +0x20/+0x38/+0x50/+0x68 are not established as a game-root
identity. The relative numbered/selected paths and userdir0 loose reader resolve
under process CWD. Consequently actual executable directory alone is insufficient;
bind actual final CWD, process image/proxy identities and expected root using the
handoff design's documented Windows path/file identity operations. No WSL string
or registry guess substitutes for the process directory.

One further native CWD writer exists: **SE_ChangeDir**, native table index117,
dispatch `4cc138[117] -> 4caa2f`, string `5a6288[117] -> 58eb38`.
It calls the same directory helper at `4caa38`. Thus a custom x3config could
change CWD **after numbered mounting**. Final matching relative DAT paths would
then not prove those indices were built under that directory.

The stock x3config hash and call subset are already qualified in the
[initial-layout note](ui-layout-generation.md#2-initial-device-owner-and-publication):
it uses table allocation, formatting, file-read and XML-import natives, not
SE_ChangeDir or font opens. For non-direct startup, qualify the **effective
pre-mod-mount x3config** (loose/localized/numbered selection using final language)
against that stock hash, or refuse admission. Selected-mod overrides are not yet
mounted when x3config loads. This additional file qualification is a prerequisite
for admitting custom resource environments through the native dialog, not a
claim that the existing Python font record already contains it. The noabout path
does not load x3config here. Unknown custom VM behavior cannot be admitted merely
by checking the three later story/intro/editor callers.

## 6. Minimal live-copy contract and hook boundaries

The existing five-byte call span `47765a..47765f` and its wrapper ABI are owned
by the initial-layout note. Entry ECX/DX and two caller-cleaned stack words must
reach the native callee unchanged. On normal return EAX is the display container;
preserve it through the reader and replay `add esp,8` at `47765f`, followed by the
native store/test. There is no new interior hook proposed. That note's relocation,
interior-entry and returned-owner checks remain mandatory.

A proposed typed copied result is `(int32 language, uint32 mod_count,
bounded ordered {uint32 byte_length, owned bytes[]}[])`, plus separately checked
root/mount identities. Bounds are **DLL admission-policy limits**, not inferred
game limits. Choose compile-time maximum count/name bytes/total bytes and refuse
overflow rather than truncate. Read all fields with checked 32-bit address
arithmetic; require begin<=end<=allocation-end and both differences divisible
by24. Reject inconsistent null triples, unreadable storage, length>capacity,
inline length>15, missing heap pointers, missing terminators or embedded NULs.
The native formatter consumes C strings, so an embedded NUL must not produce a
different DLL comparison identity. Preserve exact byte order/case and validate
the record's accepted encoding/path subset; never sort, deduplicate or silently
normalize selected names. Never use unbounded strlen.

Copy on the startup owner thread after normal nonnull display return, with
fresh nonnull G/descriptor checks. Keep the entire native read phase call-free
with respect to engine/VM/COM: no allocator, registry call, message pumping,
catalogue open, or engine string method while borrowing pointers. Use owned
preallocated output and a bounded access-fault containment mechanism. Re-read
the root/vector descriptor and refuse changed descriptors before publishing;
that is a consistency check, **not** a lock against concurrent byte mutation.
No general cross-thread or reentrant safety is established. One-shot admission
must reject nested/duplicate publication and exception/nonlocal exits; it must
not retain the copied addresses for later font-open work.

The read phase must preserve native return registers, live EAX, flags, stack,
LastError and FP/SIMD state under the existing wrapper contract. Although the
caller's first `add esp,8` overwrites arithmetic flags before the native test,
the shared preservation envelope should remain intact. DLL-side filesystem or
hash work, if required, follows copied data under its own non-reentrant startup
transaction; this note does not establish its SEH/unwind implementation.

Concrete refusal reasons include selector-not-ready, abnormal/null display
return, absent/changed G, invalid vector/string, bounded-copy failure, mismatched
language/ordered names, unknown non-direct config object, wrong CWD/root, or
mount-count/path/slot mismatch. Refusal retains native font behavior and must not
fabricate default selectors or publish partial admission.

## Reproduction and limits

```sh
python3 verification/results/fl-port/text-density/selector_readiness_evidence.py
```

Measured static result: **98 instruction/operand witnesses, eight string
witnesses, 13 aligned regions / 2,555 decoded instructions**, six direct-call
target censuses, four typed language stores and six absolute G stores. Producing
script and validated compact JSON are local under
`verification/results/fl-port/text-density/selector_readiness_evidence.{py,json}`.
Raw listings remain ignored in that directory. The region hashes bind complete
reviewed slices; instruction witnesses and manual dataflow qualify the stated
branches, not arbitrary indirect aliases or a whole-engine control-flow graph.

No production/tooling change, game launch, Wine/native execution, build, install
or commit was performed. Remaining implementation evidence: checked-copy and
exception tests, malformed/changed selectors, actual directory identity,
post-dialog effective config qualification, mount correspondence, and actual
Windows/CrossOver startup acceptance. The launch's external-write race remains
with the handoff/freshness owner.
