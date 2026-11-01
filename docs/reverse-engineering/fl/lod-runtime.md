# FL runtime LOD patch candidates

Bounded discovery, 2026-10-29. The
[producer](../../../verification/results/fl-lod-native-discovery/verify.py) verifies
32 instruction anchors and two resource/parameter names against the admitted clean FL image; the
[receipt](../../../verification/results/fl-lod-native-discovery/evidence.json)
contains derived instructions and addresses only. These are candidate bindings,
not qualified production patches. No game files were changed.

## Material occlusion selection

AP's `lod_occlusion_sites.h` patches the nonzero-LOD branch at `0x4c34f7`,
using node offset `+0x14c`. In FL's material routine beginning `0x4e0f30`,
`0x4e56b4` compares node `+0x150` against zero. The short branch at
`0x4e56c0` selects `0x4e5731`; fall-through at `0x4e56c2` attempts a material
lookup through `0x5124f0`, then binding through `0x4da490`, with a fallback.
The alternate branch binds the `+0x34` member of the entry loaded from
`0x5a9014`. Initialization at `0x4f35af..0x4f35bc` requests
`\\NONE_OCCL_DECAL` through `0x510890` and stores the result in that global.
Parameter lookup at `0x4e39ce..0x4e39db` stores `t_OcclusionTexture` in
`[ESI+0x74]`, read by the candidate bind path at `0x4e56a3`. These anchors
establish the placeholder and parameter identity. The completion below qualifies
the caller-side path equivalence statically; runtime behavior remains unverified.

A prospective FL patch would make both outcomes enter `0x4e56c2`, using the
actual short-branch encoding rather than AP's four-byte displacement patch.
The completion below supplies the path, flag and fallback contract.

## Station distance selection

The known FL selector starts at `0x495e50`. Its root-only path at
`0x495e82..0x495e9d` checks the incoming propagated flag and parent, compares
node `+0x12c` against zero, then uses `CMOVL EAX,ECX` at `0x495e96` to set
the flag when the sign bit is set. AP instead has the conditional branch
documented in `terran_lod_sites.h`.

Keeping EAX at this conditional move is a candidate way to suppress this
root-derived distance flag while retaining the incoming propagated-flag path.
The completion below qualifies the flag's downstream use, constructor/save
semantics and in-tree claim separation for the proposed encoding. The
[existing selector contract](cull-chain.md) supplies the current
function boundary and shared cull-site context; it does not itself qualify this
additional edit.

Both candidates should reuse the existing feature lifecycle and edition
bindings. Neither needs a new per-draw callback or allocation. Native execution,
rollback and visual LOD behavior remain unverified.

## Candidate encoding and propagated flag

Local byte-copy decoding establishes that each candidate can change one byte
without changing instruction boundaries: the displacement at `0x4e56c1`
redirects the short JNE to its own fall-through, while the ModRM byte at
`0x495e98` makes the conditional move `CMOVL EAX,EAX`. The latter retains the
zero established by `MOVZX EAX,AL` on the root path; an already-set incoming
flag bypasses that path unchanged. Neither edit changes EFLAGS.

The selector copies the propagated flag at `0x495f2f`, pushes it at
`0x495f33` and recurses at `0x495f37`. The local distance flag is tested at
`0x49628a`, with the zero path branching to `0x496354`. These additional
anchors identify the propagation and selection uses. The proposed encodings
are tested only in local copies; the completion below audits in-tree claim
separation. Native execution remains outstanding.

The existing raw relative-branch scanner finds no encoded branch into either
edited instruction's interior anywhere in `.text`; the full file contains no
DWORD encoding of an interior address. This is a superset scan of direct
encodings, not proof against arbitrary computed third-party entry. The station
context `[0x495e82,0x495ea1)` has no relocations. The material context
`[0x4e56b1,0x4e56c5)` has exactly one HIGHLOW at `0x4e56bc`, the absolute
placeholder-global operand. An edition profile must normalize that operand
for the actual loaded base; the one-byte edits themselves have no relocations.


## Static implementation contract completion — 2026-10-30

The [contract producer](../../../verification/results/fl-lod-native-discovery/verify_contract.py)
and [receipt](../../../verification/results/fl-lod-native-discovery/contract.json)
reuse the admitted clean image and prior 32-anchor receipt above. They verify
**127 operand/edge anchors across nine complete instruction regions**
(120 new anchors, seven overlapping the prior 32, for 152 combined unique
anchors);
the material interval has 44 instructions, of which 41 are reachable from its
LOD-0 entry before the join. Counts and region hashes are measured on the clean
file; equivalence conclusions below are static inferences. Raw Ghidra output
remains local at `/tmp/fl-lod-runtime-study.local.txt`; the read-only Ghidra
function/reference query found only the ordinary walker and recursive selector
as direct callers of `0x495e50`. No game, Wine, build or native execution ran.

### Material: full native path and fallback

The node argument is saved from EDX to `[EBP-0x1c]` at `0x4e0f64`. At the gate,
ECX holds the effect parameter, EAX is the placeholder entry and EDI is the
binding receiver. Redirecting the nonzero-LOD branch to `0x4e56c2` gives the
same register/stack state as an ordinary LOD-0 arrival, except for arithmetic
flags from comparing a different LOD and the node's unchanged LOD value.
`0x4e56c2/0x4e56c5` are MOVs; `0x4e56cb TEST EAX,EAX` regenerates all condition
flags before their first consumer. There is no subsequent node `+0x150` read
in the interval. The complete material routine `[0x4e0f30,0x4e60ca)` has no
direct entry into `[0x4e56c2,0x4e5742)` except the original gate targeting the
alternate placeholder block. This is an intraprocedural direct-edge check;
the previous raw interior scan and computed-entry limitation still apply.

The complete caller-side behavior is:

- At `0x4e56c2` save the placeholder to `[EBP-0x2c]`; a negative resource ID
  `[EBP-0xa0]` skips the entire bind at `0x4e56cd -> 0x4e5742`.
- `0x4e56d1 -> 0x5124f0` looks up that ID. A missing entry, missing `entry+0x34`
  texture or `entry+0x10 & 0x02000000` takes `0x4e56e9`. A non-null placeholder
  with a non-null `+0x34` replaces the lookup result. The resulting entry is
  still rejected when bit `0x02000000` is set (`0x4e56f7/0x4e56fe`).
- `0x4e570f -> 0x4da490` binds with ECX=EDI, EDX=effect parameter, and two
  caller-cleaned stack arguments `(entry+0x34, resource ID)`. Success in AL
  joins at `0x4e5742`. On failure, `0x4e571b..0x4e572f` requires a non-null
  placeholder and texture, then binds it with ID -1 through `0x4e5738/0x4e573a`;
  this is the same tail used by the original nonzero-LOD placeholder route.

The native LOD-0 path does **not** guarantee binding the placeholder for every
invalid resource: the negative-ID path skips binding, and the fallback at
`0x4e56f7` retains the native valid-entry assumption (it can dereference EAX
if both lookup and usable placeholder are absent). The proposed edit preserves
that existing behavior; it does not add a safe-null contract. It changes all
LOD records to attempt exactly the native LOD-0 path, including its fallback
and failures, rather than promising a texture for every record.

The lookup can lazily call `0x512490` (`0x51251f`). The binding helper has
indirect effect calls at `0x4da4f0` and `0x4da52c`, slot `+0xd0`, and returns
without popping the caller's two arguments. Thus native resource activity,
callback/reentry and foreign exception paths remain possible. The patch adds
no call, retained pointer, lock, global scratch or cleanup; stack-local
fallback state stays invocation-local. The material's existing SEH chain is
unchanged. This proves reuse of the native call/ABI path, not a blanket
no-reentry/no-throw or resource-immutability guarantee.

### Station: propagated flag, constructor and persistence

The selector receives node in ECX, camera at `[EBP+8]`, and the propagated flag
byte at `[EBP+0xc]`; it returns with `RET 8`. Initial `0x495e60` stores the
incoming EAX to `ESP+0x18` **before PUSH EDI**, which becomes the later
`ESP+0x1c` local; only its low byte is consumed. `0x495e65` separately saves the
flag byte to `ESP+0x24`. Nonzero incoming flag or non-null parent bypasses the
root conversion unchanged. On the root/zero-input path, AL is zero,
`0x495e69` supplies ECX=1, and `0x495e8c` compares the full flag word to zero.
`MOVZX EAX,AL` preserves the comparison flags. Native `CMOVL EAX,ECX` therefore
sets EAX=1 precisely for bit 31; replacing its source with EAX leaves zero.
The two local stores at `0x495e99/0x495e9d` still execute. `0x495ea1` overwrites
EAX and `0x495ea7 TEST` overwrites the condition flags. ECX, control flags,
stack, XMM/x87/MXCSR and LastError gain no new operation or dependency.

The producer enumerates all uses of both local offsets: initialization,
root conversion, recursive propagation at `0x495f2f/0x495f33/0x495f37`, and the
sole selection test at `0x49628a`. A nonzero flag takes the distance ladder
`0x496295..0x496352` (depth minus radius, thresholds 7M/15.5M/21M/28.5M), with
its native record refinement. Zero takes the metric loop at `0x496354`, using
pixel local `ESP+0x10`, record `+0x34` and the native adaptive factor. Both join
at `0x496396` for unchanged quality adjustment, clamping, publication to
`node+0x150`, renderability and child traversal. The ordinary walker explicitly
pushes zero at `0x497740`. The edit suppresses the root-derived flag while
preserving any nonzero incoming argument; it does not force that argument to
zero or alter the native culls. Recursion requires no new guard or lifetime
mechanism because the edit introduces no state or callback.

FL constructor `0x449910` copies its class argument to object `+0x48`
(`0x44995b/0x44995f`). Its second dispatch at `0x44a9bd/0x44a9c4`, byte map
`0x44b2b4` and table `0x44b27c`, sends only classes 5/6 to `0x44ab79/0x44ab25`.
Both load root from object `+0x70` and share `0x44ab5f`, which checks subtype
row `+0x5c == 0x12` with stride `0xdb8`. `0x44ab6a` ORs bit 31 into root
`+0x12c`. A decoded `.text` scan finds this one immediate `OR [reg+0x12c],
0x80000000`; this is not an exhaustive census of indirect/data-driven writers
or other consumers. The evidence establishes the expected Terran docks/factories
creator without claiming that mods or loaded flags cannot supply the bit.

Save writer `0x4917d0` reads the live word at `0x491ba4`, stores it at
`ESP+0x194` (`0x491c39`), masks only bit `0x1000` (`0x491c98`), and publishes the
masked word at `0x491d70`. It writes the `0x1f0`-byte record from `ESP+0xd0`
through `0x491dc8 -> 0x506820`. Hence the logical flags field is record **`+0xc4`**,
not AP's `+0x194`: the latter is FL's local stack offset. Reader `0x492ae0`
loads the same `0x1f0`-byte record (`0x492b00 -> 0x506700`; its address expression
`ESP+0xd8` includes two already-pushed arguments), then `0x492e71/0x492e78`
restores the word to node `+0x12c` without masking. This is logical-word
persistence, not an assertion that the raw serialized DWORD is unchanged.

The writer first calls `0x491daf -> 0x502f90`; the reader applies the same helper
after reading at `0x492b16 -> 0x502f90`. Both pass record base in ECX, descriptor
`0x5a5e74` in EDX and length `0x1f0` on the stack. The descriptor is the opcode
sequence `{4,0}`. Dispatch table `0x502fe0` maps opcode 4 to `0x502fc9`: load a
DWORD, `0x502fcc BSWAP EAX`, write it back and advance the byte offset by four.
Opcode 0 maps to `0x502fb1`, which resets the descriptor index and loops without
resetting that byte offset. Thus every DWORD in the record, including aligned
field `+0xc4`, is byte-swapped before writing and byte-swapped again after
reading. The paired involution preserves logical bit 31 (and all remaining
masked word bits), while its raw stream byte position changes. The producer
checks the descriptor, dispatch and both call paths, plus six derived
round-trip word cases. Compressed-stream transport remains untraced byte by byte.
The reader edit works on the flag whenever a constructed or loaded node
reaches selection. It writes neither node flags nor serialized data and needs
no constructor/save hook. This does not claim that all save bytes are identical:
selected LOD and unrelated gameplay can naturally differ after rendering.

### Minimal edit, ownership and remaining acceptance

| Binding | Material occlusion | Station distance flag |
| --- | --- | --- |
| Context, end exclusive | `[0x4e56b1,0x4e56c5)` | `[0x495e82,0x495ea1)` |
| Complete edited instruction | `0x4e56c0: JNE rel8` | `0x495e96: CMOVL EAX,ECX` |
| Owned write | `0x4e56c1`, `0x6f -> 0x00` | `0x495e98`, `0xc1 -> 0xc0` |
| Replacement | `JNE 0x4e56c2` | `CMOVL EAX,EAX` |
| Aligned atomic word | `[0x4e56c0,0x4e56c8)` | `[0x495e98,0x495ea0)` |
| Setting semantics to preserve | `record0` vanilla / `all` edit | `distance` vanilla / `size` edit |

A small edition binding in the existing two feature owners is sufficient:
resolve these RVAs once from the recognized image, authenticate the full
contexts (normalizing the material HIGHLOW global operand already identified
above), and reuse the existing early quiescent installation, exact readback
and documented page-protection/flush operations with the FL ownership and debt
requirements below. The AP lifecycle templates alone do not establish those
requirements. Preserve AP settings,
logging and shutdown lifetime. Each FL mutation is one byte inside a single
aligned qword and can use the existing documented Win32 plus atomic writer
path. It needs no arena, trampoline, new node reader or per-draw adapter.
No new arithmetic, allocation, lock or per-node validation is required;
material mode `all` can increase native lookups/binds and loading work relative
to vanilla nonzero LOD, which remains a runtime cost to measure.

Current in-tree claims are separate: shared cull owns the redirect at
`[0x4961d3,0x4961d8)` (nine-byte saved span ends `0x4961dc`); material admission
owns call `[0x4e7142,0x4e7147)`; light-range binding owns six bytes at
`0x4e4915`. Its detailed material-region validation also ends before the new
occlusion context. The producer checks FL header/source and shared cull
literals against both new contexts; no collisions occur. AP-only common
UI/text descriptors contain numerically nearby VAs but are not FL claims.
This is source-level in-tree separation, not a live registry or third-party
patch exclusion proof. The implemented arbitration scope is the exclusive early
startup interval, one singleton owner per feature, and exact runtime context-byte
comparison before and after opening the page, together with this in-tree
nonoverlap evidence. A changed context is refused without touching its owner.
Recognized-image admission and an open install window remain required. This
does not require a global span-reservation registry or claim protection against
arbitrary third-party writers; see the [implementation ledger](../../verification/farnhams-legacy-lod-runtime.md).

The AP templates have two reuse limits. Legacy station
`terran_lod_sites.h::restore` writes vanilla even when its prior read is
unreadable or mismatched, then returns `restore_not_owned`; material restoration
already refuses a non-owned code write. **Both** templates ignore the rollback
write/flush result and clear their live registration after a vanilla-byte
readback even when original protection remains unrestored. Their already-vanilla
restore fast paths also return without settling outstanding flush/protection
obligations. A byte readback alone does not prove instruction-cache visibility
or original page protection.

The FL implementation must compare ownership before restoring code: write only
its owned patched byte, or recognize an already-vanilla byte without rewriting
it. On unreadable/mismatched bytes, refuse the code mutation and retain cleanup
registration. Track pending instruction-cache flush and original-protection
restoration obligations independently of whether vanilla bytes are readable.
Rollback, failed install and shutdown may release the runtime claim/cleanup
registration only after the required original bytes, successful flush and
original protection have all been established. Already-vanilla readback must
still settle any outstanding obligations before release. Keep registration on
failed debt settlement and report that state accurately. These are FL acceptance
requirements, not properties established by reusing the AP templates; AP
behavior remains unchanged unless separately reviewed. No production lifecycle
change is made by this study.

Static evidence is sufficient to implement these bounded bindings. Acceptance
still requires affected host fault/rollback checks, real x86 execution of each
edited instruction and native-path state preservation, admitted loaded-base
normalization, conservative restore/mismatch behavior and startup-owner
integration. Runtime material fallback/cold-resource cost and loaded-station
LOD behavior require user-supplied FL execution/captures. Native Windows
compatibility follows the documented-API design; native Windows execution,
Reset/recovery interaction and visuals are unverified. No callback ABI,
shared cull claim or D3D state hook needs to change for either edit.
