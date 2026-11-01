# FL mounted-resource lifetime for gate clearance

2026-10-29, offline reconstruction of clean FL 1.3 executable SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Addresses are preferred VAs at image base `0x400000`; runtime addresses require
the admitted loaded base. This is the lifetime prerequisite for the
[pending gate handoff](../../architecture/fl-gate-clearance-handoff.md), extending
[resolver/layout evidence](resource-resolver.md) and
[selector readiness](font-selector-readiness.md).
No production patch, game launch, PE/Wine execution, build or installation was
performed. This note establishes bounded native paths, not an arbitrary foreign
code or whole-program alias proof.

## 1. Initial display is before the last catalogue-name mutation

The normal startup ordering is concrete:

| Native point | Effect |
| --- | --- |
| `47761d -> 50b130` | Mount final ordered selected mods above numbered catalogues. |
| `47765a -> 516890`, continuation `47765f` | Existing initial-display observer; caller still owns two stack words. |
| `477692 -> 48a5a0` | Engine factory after normal display return. |
| `48a643/649/64f/655/660` | Factory calls `487100`, `487420`, `485f60`, `483f20`, `4870d0`, then allocates arenas. |
| `48a752/48a757 -> 485d80` | Two catalogue packed-body suppression passes, **after display return**. |
| `48a75f..48a76d -> 477697` | Factory returns EDI in EAX; caller republishes engine EAX. |

At least BodyData loading already occurs inside this factory (`487100`, name
argument `4871aa -> 58cadc = BodyData`). Thus factory completion is after some
resource reads, not an assertion that verification precedes every native read.
Stable prepared payloads and actual post-factory winner verification can still
certify those same inputs; verifying a file after native use is not atomic
protection against external edits. Body and scene loading remain lazy elsewhere.

`485d80` traverses `G+0xb0` slots in ascending order, each catalogue's signed16
member count, and its `+0x14` array with 12-byte stride. It examines member
names, not physical DAT paths. The string compare at `485e09..485e20` starts
four bytes before the name terminator and compares three bytes against **`.PB`**
at `58ca7c`. It therefore admits `.PBB`, `.PBD`, and other `.PB?` suffixes.
For each such entry in slot S it probes each strictly lower slot with the last
character replaced by `D`, then `B` (`485e92/485eba`, exact catalogue lookup
`485ea2/485ec8`). A found lower entry has its name's last byte overwritten with
`X` (`485eae/485ed4`). Offset, length, entry allocation, slot array, count and
DAT path are unchanged by this routine. No re-sort occurs here.

This is packed **PBD/PBB** suppression, not BOD/BOB renaming. Comparing only
`G`, slot/count/path descriptors, or a CAT-file hash misses it. Exact-name
lookup behavior can change while all those descriptors retain their values.

Measured on the clean mounted-directory model: **18 slots, 13,081 entries**;
initial suffixes include 2,049 PBD and 1,751 PBB, with no other `.PB?` suffixes.
The native lower-middle binary search algorithm (`5329fc`) is modeled against
the sorted entry arrays, retaining their order through both mutation passes:
**294 renames, then zero**. For the actual gate receipt's **21 requests**, all
**20 winning `(slot, normalized member, offset, length)` tuples** and the one
absence remain equal before/after suppression. Packed winner hashes/lengths
are checked against the existing receipt: **11,181,815 bytes** total. These are
CAT-derived triples and static-path modeling, **not a live process-memory dump**;
the eventual native admission must copy the actual mounted triples.

This stock equality does not grant arbitrary mod equality. A reproducible
synthetic higher-slot `objects/q.pbx` suppresses a lower `objects/q.pbd` while
being outside `pbb bob pbd bod`; the request changes from selected to missing.
The offline resolver indexes original CAT members and does not execute this
startup suppression. Checking actual post-factory winners catches this case.
The original prefix comparator `504490` groups the requested stem; renaming
only an extension's final byte retains that stem, but does not preserve the
full-key order required by arbitrary later exact searches. No generalized
post-mutation exact-lookup emulator is proposed.

**Natural publication boundary:** reuse the existing outer normal factory
completion owned by `object_lifetime.cpp` / `object_lifetime_fl_core.h`, at
`477692..477697`. Its `finish_factory` already checks normal return, outer depth,
matching returned engine and construction poison. Display completion may stage
preallocated data or run checks that do not publish a lifetime; final actual
entry/winner copy belongs after the two suppression passes. No second factory
hook is needed. Factory completion alone does not grant resource immutability.

## 2. Mount lifetime, teardown and surviving mutations

The full `.text` direct-call census and HIGHLOW function-address census are
combined with the decoded allocation/publication/free paths:

| Function | Direct callers | Bounded lifetime conclusion |
| --- | --- | --- |
| Configuration/numbered mount `509e00` | `476c96` | Initial allocation/publication; numbered construction precedes dialog/mod/display. |
| Catalogue loader `50aaf0` | `50a87e`, `50a8d1`, `50b233` | Root/addon2 construction and selected-mod mount only in these native paths. |
| Selected-mod mount `50b130` | `47761d` | Replaces slot allocation before display, copies old pointers, frees old array (`50b1ca`), publishes new count/pointer (`50b1eb/1f4`). |
| Entry suppression `485d80` | `48a752`, `48a757` | Final factory work; no later native caller in this census. |
| Whole resource-owner teardown `50b330` | `476a91`, `50ad85`, `50afd6` | Shutdown/fatal exit, or mount allocation failure; not a normal reload/remount operation. |

None of these function addresses appears as a relocated function-pointer
publication in the executable. Six absolute G stores are
`476c9e, 509e4d, 50a082, 50a821, 50afdb, 50b579`: two publications and four
failure/teardown clears. The previously decoded slot-array writers are
`50a7ec` and `50b1f4`; loader publication of a catalogue is `50adab`, and loader
normalization/sort finishes at `50b11c` before successful return `50b127`.
This establishes the known normal construction/retirement topology; it is
stronger than absence of a later direct call alone. It does not exclude an
arbitrary computed entry or a generic write through an unqualified alias.

Teardown frees catalogue names (`50b413`), DAT path (`50b453`), the entry array
(`50b476/50b48b`), each catalogue (`50b4b3`), and the slot array (`50b4f8`). It
then destroys members (`50b54c`), frees G (`50b55d`), and clears G (`50b579`).
Borrowed entry/name pointers are already invalid before the global clear.
Shutdown calls it within `4762d0`; the path ends at `476ab7 -> 52ed79`, with
INT3 following, not a resumed healthy game. Mount failure paths can also tear
down G, so abnormal/fatal/OOM continuations cannot be admitted as ordinary
resource-generation restoration.

No native remount/rebuild of these arrays during ordinary save restoration or
sector changes is established. Save restoration's KC reload is a separate path
below. **Two surviving synchronous native commands nevertheless change future
resource resolution after factory completion without touching these arrays:**

| Command | Exact route | Effect |
| --- | --- | --- |
| `SE_ChangeDir`, index117 | Name table `5a6288[117] -> 58eb38`; dispatcher `4cc138[117] -> 4caa2f`; `4caa38 -> 5319c7 -> 53188a -> 5318c7` | Calls `SetCurrentDirectoryA`, changing the process directory used for relative loose paths and DAT opens. |
| `P_SetSysFlags`, index6 | Registered P dispatcher `4b2e98 -> 4b3b00`, names `4b2ecf -> 5a6128`; `5a6128[6] -> 58db58`, `4b3f34[6] -> 4b3b6b` | Full DWORD argument written to **G+0xac** at `4b3b7c`; includes archive-enable **bit0**, tested by exact and extension lookup `50458a/505404`. |

`P_SetSysFlags` obtains an integer cell from the fifth native argument
(`[ESP+0x1c]` in the case after two saved-register pushes), reads its unaligned
value at cell+1 into ECX, loads G into EAX, and writes ECX unchanged. This is
not merely toggling a cosmetic flag. Initial G+0xac is1 (`509e8b`); numbered
mount temporarily clears bit0 and restores it, selected-mod mount sets it.
The runtime command may replace all bits with any argument value. A resource
invalidation observer should distinguish bit0 from unrelated flag changes.

`SE_ChangeDir` obtains the string, pushes it, calls cdecl `_chdir`, cleans the
one word, compares returned EAX with -1, and returns a Boolean through the VM.
It saves no prior directory and performs no automatic restoration. Failed CRT
postprocessing after a successful OS directory change is another reason not
to infer unchanged CWD from the final Boolean. The two direct `_chdir` callers
are this native and constructor argv `-w` (`50a579`), the latter before mounting.
The only executable IAT call to `SetCurrentDirectoryA` is `5318c7`; there is no
import of `SetCurrentDirectoryW`. Arbitrary imported DLL/foreign API callers
are outside this executable census.

Both commands use the existing synchronous interpreter native transport,
including indirect `4c0783` and the registered native dispatch tables. They
can run from a custom KC object after startup, between loads or during a
script-driven resource load. Neither command is intrinsically tied to the
existing save-load epoch, so load-boundary retirement alone misses them.

## 3. Stock script scope and reload limits

Measured complete method/global instruction walks of the effective clean
objects find zero direct opcode82 calls to either native command:

| Effective object | Entries | Decoded KC instructions | Decoded SHA-256 prefix |
| --- | ---: | ---: | --- |
| addon2/L/x3config.obj | 2 | 43 | a77611e0fad6a0cb |
| addon2/L/x3intro.obj | 486 | 43,720 | e960938cce8d0a56 |
| addon2/L/x3story.obj | 9,046 | 1,010,000 | dbb732716e1acab6 |
| addon2/L/x3galedit.obj | 8,233 | 944,407 | 329c93c0af62ff40 |

This confirms default stock behavior, not a universal script lifetime grant.
These are the four default effective objects; direct `4b8e20` callers are
config startup `477586`, controller-selected fresh object `477d15`, and save
restoration `4bcc27`. The fresh path reads controller+0x4d4 and discards/reloads
VM state (`477d09 -> 4b30b0`); it can execute again without a second engine
factory or the save-loader hook. Native argv handling can replace module-name
globals (`4771bc..477236`). A save supplies an object-name string; the restore
path may replace it with global `5a84b0` (`4bcc1a/4bcc21`). Therefore checking
only four files initially cannot qualify arbitrary custom starts or saves.
The stock story/editor also contain one `SE_ScriptComputeExpression` call each;
its full generated-expression/native-call closure was not reconstructed here.
No dynamic-module qualification infrastructure is proposed by this note.

The bounded alternative is to observe the two actual resolver mutations rather
than impose a new policy on every KC object. Loader/name changes then need no
separate rule merely because a custom object *could* request those mutations.
Any additional arbitrary native-code patches or external CWD/array writers
still require their own contract.

## 4. Minimum generation and observer boundaries

For a future implementation, the minimum supported generation is an **owned,
immutable gate table**, initially verified against actual post-factory mounted
winner tuples plus selected packed bytes and absence checks. Keep G/slot/name
addresses inside a bounded owner-thread copy, with no engine, VM, COM, allocator
or message-pumping call during native borrowing. All filesystem/hash work
follows copied data; recheck descriptors, affected tuples and generation before
publication. Descriptor rereads detect inconsistency; they do not lock bytes.

Retire that generation before an actual archive-enable bit change and before
the live script CWD-changing call. Conservative retirement on attempted chdir
is sufficient and avoids a wrong success inference. Existing factory/load/
destroy/rollback owners retire copied consumers as already required; they do
not replace these two mutation observations. No per-draw/arrival resource walk,
file check, hash, new lock or engine call is needed.

Candidate spans, measured as complete instructions, with zero surveyed direct
interior edges and zero HIGHLOW overlap:

| Span | Native liveness and bounded adapter requirement |
| --- | --- |
| Existing `477692..477697` factory CALL | Zero explicit arguments, ordinary RET, returned EAX engine live into `477697` store/test. Reuse existing normal/unwind owner, four-byte incoming stack and CPU/LastError envelope. |
| `4caa38..4caa3d` chdir CALL | One caller-cleaned stack word, C-string argument already pushed; cdecl target `5319c7`, returned EAX status consumed after `add esp,4`. Retire synchronously, forward the exact call, preserve output/LastError. The five bytes fit their aligned qword. |
| `5319c7..5319cc` optional CRT thunk entry | Instructions sizes2/1/2: `mov edi,edi; push ebp; mov ebp,esp`; native tail pops EBP then jumps to the CRT implementation. Caller argument at entry ESP+4; int EAX return, plain RET. This entry crosses an aligned qword, so no atomic five-byte install claim. It covers argv too but adds no supported post-publication route beyond the native CALL above. |
| `4b3b7c..4b3b82` sysflag store | One six-byte MOV, **EAX=G and ECX=new full flags** both live; EBP carries native success value, saved ESI/EBP and two pushed VM result arguments remain on stack. Preserve registers/flags/FP/LastError, retire on bit0 difference before replaying the exact MOV. Six bytes cross an aligned qword. |

These are **candidate observer boundaries, not an installable hook profile**.
The early qualified exclusion window, patch ownership/rollback, wrapper unwind
and nested/foreign-thread tests remain implementation work. No reentrant hash
or read work belongs inside either invalidation observer. Conservatively revoke
before forwarding; an exception or nonlocal exit cannot revive a generation.
Failed, repeated or nested observer entry must leave retirement sticky.

Do not automatically re-arm when CWD or bit0 returns to its old value. A
transient resolver change may already have populated native model/type caches.
The existing clearance finding that shared model+0x5c retains its first tree
(`497ba5/497bb0`) makes this material. The factory does not eagerly load every
gate/model. A later cold load may repeat input verification, but **this work has
not proved that it clears every relevant model/type cache**. Sticky retirement
for the remaining engine/process incarnation is the minimal safe policy until
that recovery contract is separately established. A fresh qualified startup
can verify again. World/load generations and resource selection identity are
distinct; a save-load epoch counter alone cannot certify fresh geometry.

## 5. Reproduction, acceptance and remaining evidence

```sh
python3 verification/results/fl-resource-lifetime/verify.py --game /mnt/c/X3FL-orig
python3 -m py_compile verification/results/fl-resource-lifetime/verify.py
```

The [producer](../../../verification/results/fl-resource-lifetime/verify.py) writes
validated [derived evidence](../../../verification/results/fl-resource-lifetime/evidence.json):
**63 instruction/operand witnesses**, 16 aligned reviewed regions / **907 decoded
instructions**, 428,814 full-text instructions in the direct-reference census,
eight exact direct-call target censuses, six G stores, four candidate spans,
three rejected instruction mutations, the actual 21 requested tuple pairs and
packed payload checks, the 18-slot suppression model, and the four stock KC
censuses. The synthetic PBX counterexample is asserted. Full script identities
and relative member/slot/offset/length provenance are retained; no raw shader,
asset, byte listing or decompiler output is tracked. Scratch listings are ignored
by the owning result directory's local `.gitignore`.

This is static source/image evidence plus host metadata modeling, not native
Windows/CrossOver game behavior or a runtime mounted-memory capture. Full alias
closure, arbitrary indirect entries, imported DLL callbacks, concurrent foreign
writers, malformed catalogue/allocation recovery and exception handler behavior
are not claimed. The existing reader's shared globals/failure callbacks still
forbid calling it as a reentrant DLL service.

The producer takes about four host seconds here, including executable census,
CAT metadata, stock KC parsing and packed-byte verification; this is **not cold
I/O latency, native loading time or game FPS**. The eventual cold verifier's
11,181,815 selected bytes may be deduplicated and read with bounded scratch;
there is no need to hash every mounted member or any byte on arrival callbacks.
Native observer preservation/rollback and live native entry-copy acceptance
remain required before production integration.

Independent review reproduced all recorded fields except elapsed time (3.806 s),
including all three rejected mutation controls, and accepted this bounded RE
checkpoint. Candidate spans remain unqualified for production injection.
