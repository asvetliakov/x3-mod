# Farnham's Legacy resource resolver

Offline reconstruction, 2026-10-27, of the clean FL 1.3 executable with SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
All addresses are preferred VAs at image base `0x400000`; FL is relocatable.
This establishes resource-selection layers for the [FL port](../../architecture/farnhams-legacy-port.md),
not runtime hook admission. No game, Wine, installation, registry write or
production change was used. See the [initial engine audit](engine.md)
for image identity and relocation evidence.

## 1. Numbered catalog mounting and gaps

Let `G = *(0x5a83b0)`. The descriptor `0x5a5ca0`, installed at `G+0xc8` by
`0x509e95`, supplies these strings:

| Descriptor offset | Meaning | String |
| --- | --- | --- |
| `+0` | Edition/registry name | `X3FL` |
| `+0x1c` | Object path template | `objects\%s` |
| `+0x34` | Root catalog | `%02d.cat` |
| `+0x38` | Expansion catalog | `addon2\%02d.cat` |
| `+0x40` | Selected mod catalog | `addon2\mods\%s.cat` |

The actual consumer is the constructor's `0x50a609..0x50a93d` mounting block.
When `G+0xac & 1` is enabled on entry, it clears that bit at `0x50a61d/0x50a625`
while counting and loading numbered catalogs. This prevents recursive archive
lookup during the numbered physical-file probes.

1. Root starts at **01** (`0x50a644`), formats descriptor `+0x34`, and opens
   through `0x5063c0` in `rb` mode with user-directory flag zero. Each successful
   open increments root and total counts (`0x50a6ed/0x50a6ee`). The back edge
   `0x50a723 -> 0x50a670` exists only for success. The first unsuccessful open
   ends this series, including an absent or unreadable `01.cat`.
2. The same independent procedure counts **addon2/01 onward** using `+0x38`
   (`0x50a734..0x50a7c5`). Its first failure ends that series. A root gap does
   not suppress addon2 enumeration. Neither series searches beyond a gap.
3. A pointer array of exactly **root_count + addon2_count** slots is allocated
   (`0x50a7d3..0x50a7ec`); FL does not reserve AP's extra single-mod slot here.
   `0x50a860..0x50a895` loads root slots ascending from zero;
   `0x50a8b0..0x50a8eb` loads addon2 into slots `root_count + number - 1`.
   Both call catalog loader `0x50aaf0`. A numbered loader failure exits the
   constructor's success path. The bit is restored and the 16-bit total saved
   to `G+0xb6` at `0x50a8f0/0x50a8f7` after successful loading.

**Established:** this standard mounting path consults root and physical
`addon2`, with no physical `addon` stage. CAT opening determines the counted
prefix; that does not prove DAT integrity. The filename paired by the loader is
the catalog path with `.cat` changed to `.dat` (`0x50ae91`), not an arbitrary
directory inferred from the member key. Invalid catalogs and allocation failure
are outside the healthy-install precedence rule below.

## 2. Selected mods are an ordered list

`0x50b130..0x50b27f` mounts the vector at `G+0x78c` (begin) / `G+0x790` (end).
Elements are **0x18-byte strings**, with capacity at `+0x14` selecting inline
storage versus a heap pointer. It allocates `old_count + vector_length` pointer
slots, copies the old slot pointers, enables archive lookup, and stores the new
count and pointer (`0x50b156..0x50b1fa`). The allocator zeroes the new memory
(`0x4d90ad..0x4d90b6`), so unused trailing slots begin null.

Iteration is forward by `0x18` (`0x50b270`). Each name is formatted with descriptor
`+0x40`; `0x50b233` calls the loader with `ECX = next_slot`, `EDX = catalog path`.
Only a successful load increments `next_slot` (`0x50b23d/0x50b23f`). Thus for
valid selected packages **later list entries occupy higher slots and win over
earlier entries for the same requested member**; all are above numbered slots.
An absent package does not terminate the vector loop. The published count remains
the allocation upper bound. Do not generalize this to safe rollback of corrupt
packages or allocation failures: the loader publishes a catalog object before
all later allocations complete (`0x50adab`), and has a later failure exit.

The read/write registry path is **HKCU\Software\EGOSOFT\X3FL**, value
**ModName**. `0x4d7902/0x4d790a` format descriptor `+0` with
`Software\EGOSOFT\%s`; `0x4d792a/0x4d792f` open HKCU via `RegOpenKeyExA`.
The value read at `0x4d7b23/0x4d7b2b` uses `RegQueryValueExA`. On success it
clears the old vector, splits at **semicolon** (`0x4d7b8b/0x4d7b8e`), skips empty
slices, and appends strings (`0x4d7be7`) in serialized order. The write path uses
the same HKCU key (`0x4d7f7c/0x4d7f81`), joins entries with semicolons
(`0x4d8369`), and writes `ModName` with `RegSetValueExA` (`0x4d83a9/0x4d83b1`).
This proves the registry serialization, not that the registry is the vector's
only writer. The read requests `0x63` bytes; long-list/error behavior needs
separate launcher/configuration treatment before changing selection tooling.

## 3. Archive members retain their namespace

Catalog loader `0x50aaf0` keeps each member's path, uppercases it and changes `/`
to `\` (`0x50b080..0x50b092`), then sorts entries (`0x50b110/0x50b11c`). It does
not insert the physical catalog's `addon2` directory into member names. Each
entry is **12 bytes**: offset `+0`, length `+4`, member-name pointer `+8`.
The catalog object has member count at `+0`, DAT path at `+0xc`, user-directory
flag at `+0x10`, and entry-array pointer at `+0x14`.

Exact member lookup `0x5044c0` applies the same uppercase/slash normalization
to the **entire requested key**, then calls binary search with the full-string
comparator `0x504450` (`0x50453b..0x504547`). Neither routine strips `addon2`,
prepends it, or retries an `addon` or root candidate. The extension resolver uses
prefix comparator `0x504490` to find the requested stem, then examines adjacent
members and accepted extensions. That prefix comparison is not namespace
fallback; the caller's directory remains part of the stem.

Concrete FL caller `0x487d40..0x487e4b` builds **addon2\types\<name>** at
`0x487d65/0x487d6d`, supplies **pck txt** (`0x487d91`), and constructs the reader
at `0x487dc7`. On failure it destroys the reader and returns null
(`0x487e13..0x487e21`); there is no root-types retry in this function.
The called reader `0x506d30` supplies user-directory flag zero to `0x505e00`.
This proves one important caller's namespace policy; it is not a census of
every engine resource family.

Consequently, **physical mount precedence and member namespace are separate**.
A root catalog may contain `addon2/types/...`, and an addon2 catalog may contain
`objects/...`; those exact keys participate normally. Physical `addon` is not
automatically mounted or searched by these core paths. An explicit caller path
containing `addon\...`, a member with that literal name in a mounted catalog,
or another high-level loader is not ruled out by this bounded audit.

## 4. Loose files and fallback depend on the reader

| Reader path | Proven selection order for ordinary game resources |
| --- | --- |
| Extension-aware `0x505e00 -> 0x504b70` | Matching loose files first; otherwise highest catalog slot containing an accepted stem/extension, then lower slots |
| Exact-name `0x5063c0`, read mode, user-directory flag zero | Highest catalog slot containing the exact normalized key first; physical requested path only when no archive entry was selected |
| Exact catalog search `0x504570` | Starts at signed 16-bit `G+0xb6 - 1`, skips null slots, calls `0x5044c0`, stops immediately on first hit |

For the extension resolver, `0x504f75 -> 0x4ebb20` enumerates the requested
physical directory. The helper combines directory and search pattern and calls
the CRT find-first routine (`0x4ebb9b`); the loop uses find-next at `0x504fbe`.
After a loose match, `0x5053e6 -> 0x505ce0` skips the archive phase completely.
Otherwise the archive loop begins at count minus one (`0x505411/0x505418`),
stops once a candidate was found (`0x505426`), and decrements at `0x505cd4`.
Extension rank helper `0x504a70` returns the smallest matching extension index
(`0x504b38/0x504b3a`), or `INT_MAX`. The extension list is split on spaces into
string records. Localized candidates use `-L%03d` from `G+0x75c`
(`0x504ecc..0x504edb`). **The complete localized-versus-unlocalized tie policy
is not qualified here**; it must be retained as a separate resolver concern.

For exact opens, the call to `0x504570` is at `0x506434`, before physical `fopen`.
The archive-selected path opens the selected DAT (`0x5064d8`). If that DAT open
fails, `0x5064f1 -> 0x5065ad` returns failure: **no lower catalog or loose retry**.
The physical path at `0x506522` is reached when no entry exists (and for the
explicit write-mode bypass). User-directory flag nonzero bypasses catalog
lookup at `0x50642c`. This table concerns read requests; it is not an audit of
every possible CRT mode string.

Similarly, extension selection does not imply fallback after the selected
payload fails to open: DAT open failure branches to failure at `0x505ec5`, and
the loose-file open failure also returns directly. Resolution and subsequent
payload read/decompression are distinct stages. `0x4eba70` normalizes slashes
and optionally prepends the user directory; with flag zero it copies the
requested path without inserting or removing edition directories.

## 5. ABI, lifetime and hook limitations

| Function | Observed entry/return contract |
| --- | --- |
| `0x50aaf0` catalog load | `ECX` low 16 bits = slot, `EDX` = catalog C string; `AL` success; plain `ret` |
| `0x504570` archive lookup | `ECX` = C-string key, `EDX` = optional catalog-output pointer; `EAX` = borrowed entry pointer or zero; plain `ret` |
| `0x5044c0` lookup in one catalog | `ECX` = catalog, `EDX` = C-string key; `EAX` borrowed entry pointer or zero; plain `ret` |
| `0x504b70`, `0x505e00` extension resolve/open | `ECX` = file object; stack arguments = path string object, extension-list string object, user-directory flag; `AL` result; `ret 0xc` |
| `0x5063c0` exact open | `ECX` = file object; stack arguments = path C string, mode C string, user-directory flag; `AL` result; `ret 0xc` |
| `0x506d30` buffer owner constructor | `ECX` = owner; four stack arguments, first two = path/extension string objects; `EAX` owner; `ret 0x10` |

The file object holds stream `+0`, flags `+4`, archive offset `+8`, member length
`+0xc`, position `+0x10`, and resolved-name string at `+0x14`. Flag mask `0x1`
means open; `0x2` marks archive selection. `0x506d30` opens a temporary file object, reads
through `0x505f40`, then closes it through `0x5049a0`. The returned buffer is
retained in owner `+4/+8`; values from globals `0x5ab3e8/0x5ab3ec` are copied to
owner `+0x10/+0xc`, and failure is recorded at `+0x24`.

Those shared globals, archive-array mutation and the engine allocator's failure
callback prevent assuming reentrancy or cross-thread safety. Borrowed catalog
entry pointers are not independent ownership; their validity across remount or
teardown is unqualified. Several functions install FS-based exception frames.
Normal save/restore sequences and stack cleanup have been decoded, but exception
unwind maps, LastError behavior, external callbacks, and arbitrary interior-site
register/flag liveness have not been qualified. **No interception site or safe
runtime calling adapter is proposed by this note.**

## 6. Consequences for the complete port

The AP [body resolver evidence](../body-format-bob1.md#7-which-file-wins-extension-and-archive-precedence)
has analogous numbered gaps and extension lookup, but AP's single selected-mod
slot and `addon` path are different. `sector_fog_census.Assets` models an
`addon/key`-before-`key` candidate rule and globbed catalogs; those are tool
policies, not proof of FL's core engine behavior. `bob1.mounted` already models
contiguous numbered runs, but its edition and selected-mod representation need
the FL rules above.

All baker inputs, fog census inputs, config selection and package/install paths
need one **ordered package-list identity**, including every selected mod in
order and the actual contiguous numbered runs. Taking only the final mod name,
sorting selected names, or treating them as an unordered set changes winners.
Installing a numbered fake patch below selected mods cannot guarantee that it
overrides their members. An output package selected last has higher archive
precedence, but extension-aware loose files still win. Tooling must also
distinguish exact member requests from stem/extension requests and retain the
caller's namespace rather than inventing a global addon2/addon/root fallback.
These requirements apply to every feature's asset dependencies; this audit does
not establish that shadows, HDR/emission, fog, audio, controls or other assets
all use the same reader or namespace. Per-family caller mapping remains required.

## 7. Reproduction and limits

```sh
python3 verification/probe/verify_fl_resource_resolver.py --exe /mnt/c/X3FL-orig/X3FL.exe
```

Measured result: **135 decoded instruction witnesses, 10 string witnesses,
5 descriptor-pointer/string witnesses, 4 import witnesses**, across **14 windows
and 3,734 decoded instructions**. The verifier rejects **9 targeted in-memory
mutations**: root/addon2 gap branches, reverse-slot step, loose-hit exit, mod
success slot increment, registry separator, slash normalization, addon2 name,
and descriptor mod pointer. Mutation checks deliberately bypass whole-image
hash admission and require the relevant structural witness to fail.
It also checks direct branch boundaries within reviewed windows. Some windows
are slices of larger functions; this is not a complete control-flow graph proof.

Raw listing and machine-readable result remain ignored under
`verification/results/fl-port/resolver/`; `reproduce.sh` retains the command.
The host verifier tests these static witnesses, not Windows/CrossOver filesystem
behavior or actual game selection. No runtime or performance measurement was
made. Remaining work: localized tie semantics, resource-family caller policies,
all selected-vector writers/config limits, malformed-catalog and allocation
failure behavior, and any future reader-hook lifetime/reentrancy qualification.
