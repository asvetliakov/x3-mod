# FL gzip and crypto loading sites

## Scope and evidence

2026-10-28 bounded static correspondence and deep lifetime qualification.
**Production activation is not yet qualified.** The exact supported API boundary,
ownership witnesses and remaining admission/installation work are below.
Production remains unchanged; no game, Wine, native fixture, build or install ran.
The [feature matrix](../../architecture/farnhams-legacy-feature-matrix.md) loading
row includes other optimizations outside this note. Resource mount behavior is
owned by [the resolver note](resource-resolver.md).

Inputs: `/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`, and
`/mnt/c/X3AP-orig/X3AP.exe`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
Addresses below are preferred VAs at `0x400000`. FL has a relocation directory
(RVA `0x37d000`, size 79,964 bytes); all 51 audited import-reference instructions
use HIGHLOW-relocated absolute operands. The twelve original indirect-call
witnesses each have that relocation at their address plus two. Their absolute IAT operands
must be interpreted at the loaded base. This differs from assuming AP's fixed
base qualification is automatically sufficient for FL.

## Gzip correspondence

| API | AP IAT | FL IAT | FL instruction witness |
|---|---|---|---|
| gzopen | `0x5323d8` | `0x5503e4` | `0x506627` |
| gzread | `0x5323e4` | `0x5503f8` | `0x5067b0` |
| gzseek | `0x5323fc` | `0x5503e8` | `0x506926` |
| gzgetc | `0x5323f4` | `0x5503f4` | `0x5066ec` |
| gztell | `0x5323ec` | `0x5503ec` | `0x506077` |
| gzclose | `0x5323f0` | `0x550408` | `0x50695d`, also inlined callers |
| gzwrite | `0x5323e0` | `0x5503f0` | `0x506871`, also inlined callers |

All are named imports from `zlib1.dll`. AP uses tail-jump import thunks; these
FL witnesses are direct `call [IAT]`. Each checked call occupies exactly six
bytes `[site, site+6)`. IAT replacement overwrites a four-byte pointer, not those
instructions; there is no displaced instruction or trampoline relocation for
that design. The calls remain validation witnesses, not proposed inline detours.

FL `0x5065e0` opens gzip with ECX=file object, stack path/mode/user-directory
arguments, AL result, `ret 0xc`. It closes an existing stream first, clears open
and progress bits, resolves the path through `0x4eba70`, calls gzopen with the
supplied mode, stores the handle at `+0`, and sets `flags +4 |= 5` on success.
`0x506700` reads with ECX=file object and stack `(buffer, size, count)`, returns
EAX and `ret 0xc`; the gzip arm multiplies size/count and passes three cdecl
arguments to gzread, then caller-cleans twelve bytes. Preserve this FL argument
order rather than copying an AP private calling adapter. File fields agree with
the resolver note: stream `+0`, flags `+4`, CAT base/length/position `+8/+c/+10`.
`0x5066b0` dispatches getc, `0x506820` write, `0x5068a0` seek, and `0x506940`
close. Gzip seek is cdecl with twelve-byte cleanup; engine return conversion
at `0x506931/33` tests the sign of EAX. Close clears the open bit before calling.
The gzip import ABI remains the cdecl signatures in `gz_buffer.h`, with 32-bit
LONG seek/tell and int read/error returns. This does not establish a private
engine-hook ABI for arbitrary interior sites.

### Complete named-import reference census and handle owners

The original `FF 15` search was incomplete. Linear decoding from the known
`.text` origin finds one gzopen/read/seek/tell instruction reference, **four
for gzgetc, 21 for gzwrite, eleven for gzclose**. These are measured instruction
references, not dynamic call counts. At `0x506174/0x5061d0/0x506234` getc is
loaded into EDX and invoked at `0x5061c4/0x506228`; caching a DLL pointer in a
register is covered only if the IAT was routed before that load. No alternative
gz handle API is imported: the remaining zlib imports are inflate/init/end on
separate z_stream objects. This closes the static named-import surface; it is
not a proof against computed aliases or later foreign code replacement.

Six direct callers of `0x5065e0` are confirmed by Ghidra entry references and
instruction operands, with three `rb` and three `wb` modes:

| Call | Mode | Owner and lifetime |
| --- | --- | --- |
| `0x478add` | wb | save writer `0x478a10`, stack file `[EBP-0x84]` |
| `0x47949a` | rb | save loader `0x479430`, stack file `[EBP-0x70]`; success closes via `0x479936 -> 0x5049a0` |
| `0x479b49` | rb | 13-slot metadata scan `0x479a30`, stack file `[EBP-0x70]`, closed each iteration at `0x479d9e`, then destructor `0x479db8` |
| `0x48befe` | wb | recording writer `0x48be40`, heap file owned by a 0x3c-byte recording object |
| `0x48f108` | rb | recording playback `0x48efa0`, heap file owned by the same recording-object layout; survives return |
| `0x4ca56a` | wb | VM dispatcher `0x4c84a0`, stack file `[EBP-0x140]` |

The constructor at `0x504930` zeros stream, flags and slice fields and initializes
three string members. The object is **0x64 bytes**, not just its 0x14-byte stream
prefix. Gzip open publishes the real gzFile only to `file+0` at `0x506630`;
its return is a boolean. The resolved filename is freed on success and failure
before open returns. A buffer hook must copy/use the path and mode during open,
never retain these caller pointers. Three write opens remain native.

The playback owner is actually published: `0x48f0cf` stores it into
`[DWORD(0x5aa100)+0xa0]`, and `0x48f106` stores the file object into `owner+0`.
At teardown `0x48f354..0x48f39f`, `0x48f35c` calls `0x5049a0`, then frees the
0x64-byte file, clears `owner+0`, frees the 0x3c-byte owner and clears the global
owner field. Thus neither a stack-only nor an all-handles-single-thread claim
is established. Save decode passes its stack file to synchronous decoder calls
(e.g. `0x4bcb10`, `0x493510`, `0x4b4a50`); this inspection does not certify every
transitive decoder as incapable of retaining an alias.

Every observed close goes through the same FL gzclose slot, including inlined
paths at `0x479d9e`, `0x4c29df`, `0x4c2ee5`, `0x5048f2`, `0x5049bd`,
`0x505e48`, `0x506408`, `0x5065fe`, `0x50695d`, `0x50a68a`, `0x50a77f`.
Destruction `0x5049a0`, owning-close `0x5048d0`, reopen and the metadata loop
clear the open bit before the native close. The stale handle value may remain
at `file+0`; flags, not a null handle, prevent a second close. `0x5048d0` closes,
runs the destructor with the bit already clear, then frees 0x64 bytes. The
read-mode buffer must unregister before invoking real close, so native pointer
reuse starts a new generation; no tracking state can outlive an unobserved close.
Current stale-slot replacement is a fallback, not evidence that bypassed closes
are safe. Allocation failure or a full 32-slot table may leave a newly opened
handle wholly native; abandoning a live read-ahead handle requires cursor/error
resynchronization, not simply clearing its slot.

Ordinary failure cleanup and C++ unwind are distinct. The save and metadata
frames install SEH/C++ handlers `0x54d8ba/0x54d8f4`; cleanup funclets
`0x54d8a3/0x54d8e1` dispatch to `0x5049a0`. These boundary witnesses establish
the destructor route, not full unwinding-table/exception execution coverage.
Ghidra incorrectly removed the metadata gzclose branch because its inferred
private open prototype omitted ECX side effects: machine instructions explicitly
store the stream/flags and execute the flag-gated close. Raw decompilation is
navigation evidence, never the ownership proof on its own.

The original AP and FL zlib DLLs are byte-identical, SHA-256
`27b95a87be89090df67f5f1e7fc88437da400b7c3b6728418d97eca71882cad5`.
This supports reuse of the existing DLL semantics evidence in
[gz-buffer verification](../../verification/gz-buffer.md), including its known
corrupt-stream boundary limitation; it is provenance, not a required runtime
DLL hash. No new DLL execution or performance measurement occurred.

## Crypto correspondence and observable lifetime

FL signature checker `0x4f8b50` corresponds to AP `0x4cabc0` described in
[the signature note](../script-signature-check.md). Entry moves ECX to EBP (text)
and EDX to ESI (text length). Remaining stack arguments are signature string,
signature length, key blob, key length, output length pointer. Both normal exits
use plain `ret`; the caller cleans the five stack arguments (twenty bytes at
`0x4f95c1`). Thus the private checker ABI changed from AP's EAX text argument.
No production cache hook needs to call this private function.

| Operation | FL call span (half-open) | FL return gate | FL IAT |
|---|---|---|---|
| Initial delete | `0x4f8bbc–0x4f8bc2` | `0x4f8bc2` | `0x550010` |
| New keyset | `0x4f8bd5–0x4f8bdb` | `0x4f8bdb` | `0x550010` |
| Import | `0x4f8c02–0x4f8c08` | `0x4f8c08` | `0x550000` |
| Destroy key | `0x4f8dab–0x4f8db1` | `0x4f8db1` | `0x550008` |
| Release | `0x4f8db7–0x4f8dbd` | `0x4f8dbd` | `0x550034` |
| Final delete | `0x4f8dd0–0x4f8dd6` | `0x4f8dd6` | `0x550010` |

These are stdcall CryptoAPI imports with 5/6/1/2 arguments for
acquire/import/destroy/release, matching `crypt_cache.h`; the game does not
caller-pop their arguments. Preserve the existing exact return-address gate,
translated for FL, rather than admitting every named import caller. A preceding
crypto function also references acquire/import/destroy/release and is outside
the cache contract.

The provider/container strings at `0x5908e4/0x590910` are respectively
Microsoft Base Cryptographic Provider v1.0 and X2EgosoftCSPContainer. The observed
sequence is delete flags 0x10, create flags 8, type 1, key import with null
wrapping key and zero flags, MD5 hash creation/hash data, native signature
verification, digest extraction, hash/key cleanup, release flags zero, final
delete. Delete return values are ignored; failed create branches directly to
zero return. Other failures converge through cleanup of initialized handles.
The checker holds provider/hash/key in stack locals (at stable post-prologue
ESP offsets `+0x18/+0x14/+0x20`); pushes temporarily shift their addressing.
Native hash and signature verification must remain active. The returned EAX is
an allocated digest string (or zero), and the last argument receives its length;
no provider/hash/key is returned or written to a global. Their stack addresses
are passed only to the corresponding native output APIs. Validation/create
failure returns zero; after successful creation, import/hash/decode/allocation/
verification failures converge on hash cleanup (`0x4f8d93`), conditional key
cleanup (`0x4f8da2`), release and delete. The zero-allocation arm at `0x4f8de0`
rejoins `0x4f8d7a`; it does not leak by taking the early validation exit. There
is no local FS:[0] exception frame in this checker. A foreign exception or
nonlocal exit can bypass cleanup; neither shim nor cache may manufacture a
successful logical destroy in that event. Retaining an outstanding entry rather
than reissuing its handle is the safe bounded outcome.

### Key bytes, caller lifetime and the second native crypto user

Both story calls (`0x4f9472/0x4f95bc`) use the same caller-owned decoded key
allocation, length `0x114`; the 0x115-byte allocation is freed by `0x4f8df0`
after its calls. VM call `0x4cbf47` constructs a separate byte array, passes
its actual length and address (`0x4cbf1e/1f`), and frees it after return at
`0x4cbf5c`. This is concrete evidence against pointer-identity or constant-key
caching. Cache admission must validate the existing 276-byte RSA public blob
format, and copy/compare its full contents synchronously while that caller
allocation is live. Native import consumes those bytes before return; no
borrowed pointer is retained by the existing cache. Story/VM signature text and
output-length pointers likewise remain caller-owned for the call.

**A real sibling path prevents treating the six-site gate as full namespace
closure.** The VM also calls signer `0x4f7c90` at `0x4cc02d`. It loads
CryptAcquireContextA into EBX at `0x4f7c9e`, then calls EBX at
`0x4f7cca/0x4f7cdf` with the same provider/container and delete/new flags.
It imports a different-size key natively at `0x4f8a02`, signs through the native
hash/signature APIs, and destroys/releases/deletes at
`0x4f8b15/0x4f8b21/0x4f8b3a`. Its VM invocation is guarded by state and the
story checker (`0x4cbfdf..0x4cc02d`), not an arbitrary private call. The all-text
reference census finds five acquire-reference instructions (including the
register load), two import, two destroy and two release references. A raw DWORD
match at `0x77a720` is relocation data, not another release caller.

The present outer wrappers send unqualified callers directly to native, so
these sibling mutations do **not** update `crypt_cache`'s generation. Even a
sequential verify → sign → verify can detach the retained cached provider/key
from the recreated/deleted named namespace. No observed wrong verdict is claimed;
retained-provider behavior and failure equivalence across that sequence are
unqualified. The existing deliberate longer-lived-container policy does not
silently authorize a second native user. Before activation, either qualify and
coordinate this exact sibling sequence or invalidate/retire relevant cache
entries on its native lifecycle mutations without admitting its private-key
imports into the public-key cache. Observing just the six verifier return sites
cannot do that. This is the finite crypto frontier, not a request to prove the
absence of every external process or arbitrary injector.

### Reentrancy and concurrency

The checker calls game allocator `0x4d8fc0` while provider/key/hash are live.
Allocation failure can reach callback `DWORD(0x5aae24)` at `0x4d906a`; normal
startup registers `0x47b500` at `0x4778f3`. That callback invokes resource
reclamation at `0x485c00/0x512410`. The early-startup default-zero fact is
irrelevant to this later loading path. No nested signature call was demonstrated
in that reclamation closure. The cache must nevertheless hold no bookkeeping
lock across native CSP calls, allocations or callbacks; its existing owning-
thread, outstanding-key and generation rules protect observed qualified nested
operations. They do not protect the currently bypassed sibling operations.
Distinct signature calls own separate stack locals; concurrency is not a reason
to share an outstanding key or provider. No exception may escape proxy C++
bookkeeping through a C API boundary.

### Gzip native-use contract clarification, 2026-10-28

The earlier lifetime audit left shared playback ownership and save-decoder
borrowing as a serialization-or-new-protocol admission question. The bounded
follow-up narrows that requirement: the buffer inherits **native zlib's serialized
per-handle operation and lifetime precondition**, already used by the
[AP buffer contract](../../verification/gz-buffer.md#switches-and-gate). One operation
at a time may use a live handle; close follows completed operations, and no use
follows close. Externally synchronized thread handoff is allowed. A permanent
thread affinity, whole-engine scheduling proof or added per-handle lock is not
required merely because the game exposes a shared owner or synchronous borrower.

The bundled DLL supplies concrete evidence for that native precondition.
Its exported `gzread` at preferred DLL VA `0x10003a60` directly writes stream
fields `+0xc/+0x10` at `0x10003aab/ae`; these accesses have no surrounding stream
lock. Read-mode `gzclose` at `0x100043a0` calls destructor `0x100039b0` at
`0x100043e1`; it calls imported `free` on the stream at `0x10003a4b`. Concurrent
same-handle operation/close already risks native state corruption or use after
free. The buffer is not required to make that unsupported usage safe. Gzip APIs
have no engine callback argument; no reentrant operation inside an active gzip
call was demonstrated by this inspection.

The concrete playback updater is `0x48f570..0x491783`. Its sole direct caller
is `0x48bded` in `0x48b6d0`, itself called at `0x4782cf`. The updater loads the
published owner at `0x48f584`, file at `0x48f593`, and reads at `0x48f5bb` and
later sites. Normal end-of-record teardown calls `0x48f280` at `0x491778`, after
preceding reads return. Script/control open and close calls `0x4b1e19/42` are
gated by plain `engine+0x24` flags, not a lifetime lock. Save calls
`0x47983c/4c/5c` synchronously pass the same `[EBP-0x70]` file to
`0x4bcb10/0x493510/0x4b4a50`. These witnesses establish ordinary sequencing;
they do not prove outer dispatcher exclusion or transitive borrower nonescape.
**No actual concurrent close/operation was demonstrated.** Those unknowns alone
do not establish a new buffering failure under native's supported-use contract.

Source inspection of `gz_buffer.cpp` finds no new different-handle lifetime
exposure under that precondition. Open initializes state before release-publishing
its atomic slot pointer; lookup acquire-loads pointers and touches state only for
its matching handle. The table lock serializes slot assignment and retirement.
Close copies state under that lock before unpublishing the slot, then uses only
its independent copy after unlocking. A different open can therefore reuse the
retired slot without the old close touching replacement state. Shared hints,
registration counts and totals are atomic. Originals/configuration remain
immutable after early installation; live reinitialization is outside this
argument. Complete API/close routing remains mandatory: stale-slot replacement
cannot qualify bypassed closes. Returning to native while buffered cursors differ
is still invalid. A new synchronization protocol is justified only by concrete
supported-use exposure, not an unproved hypothetical native race.

## Installation, liveness and remaining frontier

The reviewed [early install decision](../../architecture/farnhams-legacy-install-boundary-decision.md)
and [startup evidence](../../verification/farnhams-legacy-startup-install.md) supply
the boundary: supported initial static attach arms the post-cookie entry JMP;
ordinary preparation runs outside DllMain, after config load and before the EXE
CRT, with proxy pinning and ready dispatch. Reuse its executable-entry ownership
argument. No second late-quiescence analysis, thread-free-process assumption or
late D3D installation fallback is needed or authorized. No gzip/crypto operation
or helper call belongs in the attach arm.

A production FL adaptation must stage a dedicated loading transaction there.
The present AP `loading_trace::install` is not that transaction: it logs and
initializes gzip activation **before** individually patching slots, tolerating
partial gzip coverage. Required concrete order:

1. Recognize FL, translate each preferred VA to loaded-base+RVA, validate named
   import identities, all relevant instructions and complete HIGHLOW operand
   sets. Resolve all originals, including optional rewind, and stage ready
   forwarding state before exposing a shim. An unknown pre-existing IAT chain
   needs explicit qualification or refusal, not blind trust in an executable
   pointer. No runtime zlib or CSP hash prerequisite is introduced.
2. With enhancement admission false, claim all six gzip slots as one group;
   the four crypto lifecycle slots form their own group. Check exact ownership
   on every write, page protection and restoration. Do not publish a buffered
   open until read/getc/seek/tell/close are all owned and ready. Only then publish
   activation, still before handing off to game CRT. All requests derive from
   the already loaded config; defer diagnostics/backend/workers as the startup
   contract requires. This requires adapting current AP ordering, even though
   early entry exclusion prevents native game entrants during preparation.
3. Any partial failure leaves native forwarding, verifies rollback and protection
   restoration, and retains reachable shim/original storage under the existing
   process pin. A failed restoration is not a successful rollback. Foreign
   ownership loss must not be overwritten. Once the game resumes, there is no
   live reinitialization/unhook or cache shutdown; gzip state lives through its
   observed closes and crypto objects follow the existing bounded exit policy.

These are IAT hooks: a four-byte pointer is written; no code bytes are displaced.
The six-byte instructions are validation witnesses. Gzip is cdecl, CryptoAPI
stdcall; hooks must preserve EBX/ESI/EDI/EBP, stack cleanup and four-byte incoming
alignment, return EAX/AL as required and retain the qualified x87/MXCSR behavior.
EFLAGS and volatile EAX/ECX/EDX are API-call clobbers; this does not justify
interior detours. The caller uses explicit tests on returned EAX/AL, not saved
pre-call flags. The checker does not inspect LastError; native failure errors
must still pass through. Cache hits preserve incoming LastError (except emulated
absent-delete), as the existing cache contract specifies. Gzip read-ahead does
not promise equality of every incidental native LastError/errno side effect;
its native result/stream-error semantics and known corruption-edge limitation
are the acceptance target. Existing emitted-code/no-x87 and seeded ABI evidence
is reusable for unchanged cores; a new FL wrapper/transaction needs its own
emitted ABI, fault and rebased-IAT execution checks.

**Minimal remaining frontier:** (1) the native sibling crypto namespace sequence
and cache coordination above; (2) implementing/fault-testing the early grouped
transaction and binding it to the startup candidate; (3) qualifying the FL gzip
wrapper/transaction with a native valid-use fixture, retaining the existing
serialized-handle contract and covering ordinary read/close, different-handle
slot reuse and synchronized ownership handoff. Unsupported same-handle overlapping
read/close does not create an added-lock prerequisite. The private checker ABI,
known handle cleanup routes, caller key lifetimes, named IAT surface and selected
relocation boundaries are established offline. This bounds the next work; it is
not permission to copy AP gates and declare FL loading supported. Native
Windows/CrossOver execution, game loading latency and user acceptance remain
separate evidence gaps. No speedup is measured by this audit.

## Reproduction

`python3 verification/probe/verify_fl_loading_hooks.py`

Measured: **52 windows, 1,213 decoded instructions, eleven import identities,
51 import-reference instructions, twelve original six-byte calls, 83 ABI/flow
anchors, four strings, 115 HIGHLOW operands and 150 rejected mutations**.
The 52 windows consist of 25 flow slices plus 27 isolated import-reference
instructions. Three synthetic bases (`0x400000`, `0x17000000`, `0x90000000`)
preserve decoded boundaries and normalized window digests. These are offline
rebase/normalization checks, not executing a mapped image.

The 150 negative cases comprise 130 instruction first-byte flips (opcode/prefix),
four string first-byte flips, eleven import-name changes, four relocation
changes and one interior-boundary case. Instruction/string/import mutations
bypass image/window hashes; they do not mutate immediate/displacement payloads.
The relocation negatives remove/change an operand relocation, relocate an
opcode or cross an instruction boundary. The boundary case starts a complete
decode inside an operand.
Both slice edges are checked against the project linear `.text` reference,
not just self-consistent slice decoding. The census includes register loads;
it is not a complete computed-alias or reachability proof. Full region hashes
bind reviewed flow, rather than claiming sparse anchors certify all behavior.

Ignored evidence and producers are under
`verification/results/fl-port/loading-hooks/`: `summary.json`, `decoded.txt`,
`reproduce.py` (validated command/timing producer), `reproduce.sh`,
`inspect_lifetime.py`, `ownership.c`, `close-owners.c`, and
`ghidra*.log`. `reproduce.py --decompile` reproduces the Ghidra outputs using the existing analyzed
`registry-writers/ghidra/FLRegistry` project read-only. The raw decompilation and
instruction listings remain local and untracked. The verifier restricts raw
outputs to `verification/results/`. The initial 614-instruction/43-mutation
correspondence pass is superseded by these counts; its opcode-only census and
open lifetime questions are corrected above.

The gzip native-use clarification has a compact derived
[producer](../../../verification/results/fl-port/loading-hooks/gzip-serialization/native_contract.py)
and [summary](../../../verification/results/fl-port/loading-hooks/gzip-serialization/native-contract.json).
Run `python3 verification/results/fl-port/loading-hooks/gzip-serialization/native_contract.py`.
Measured: **nine windows, 386 instructions and 19 checked anchors**, with named
DLL export/import identity checks, both window edges checked against independent
linear `.text` decoding, complete input hashes and a source hash binding the
manual slot-ordering review. The producer emits no raw instructions or bytes;
its summary round-trips through the JSON parser. Static source ordering supplies
the different-handle safety inference; this is not an executed concurrency test.
The local `query.sh/query2.sh` and `owners.c/dispatch-owners.c/bodies.txt` in that
ignored directory retain read-only Ghidra caller/function navigation evidence.
Only the compact producer and derived summary are intended for tracking.

Independent contract review accepted the native-use argument and source ordering,
and reproduced the compact JSON exactly. No production synchronization change
or claim of executed FL concurrency qualification follows from that review.

## Port (2026-11-03)

`gz_buffer` and `crypt_cache` shipped in the fl edition; the shared cores
(`src/proxy/gz_buffer.cpp`, `crypt_cache.cpp`) are reused unchanged behind new FL
transactions (`src/engine/fl/gz_buffer_fl.cpp`, `crypt_cache_fl.cpp`;
sites in `gz_buffer_sites_fl.h`, `crypt_cache_sites_fl.h`; windows in
`loading_windows_fl_inc.h`, generated by
`verification/results/fl-loading/generate_windows.py`). Both install inside the
`engine_patch` window from `capture.cpp`'s FL block, after the music keep, all
slots or none, with read-back and reverse-order rollback; the FL loader has no
unload path, so the slots live for the process lifetime.

- **gz_buffer**: the six zlib1.dll slots (`gzopen/gzread/gzseek/gzgetc/gztell`
  `0x1503e4/0x1503f8/0x1503e8/0x1503f4/0x1503ec`, `gzclose 0x150408`) point
  straight at the shared buffer's cdecl functions; `gzwrite 0x1503f0` is the music
  keep's and is not claimed. The **gzclose slot is shared with the keep**
  (`gz_close_share_fl.h`): whichever installs first owns the slot and the other
  chains, so in both orders the keep's close observation stays ahead of the
  buffer's unregister-before-real-close (buffer-first: keep forwards to the
  buffer's entry; keep-first: the buffer chains behind the keep's forward word).
  No slot the keep holds is ever exchanged.
- **crypt_cache**: the four ADVAPI32.dll lifecycle slots
  (`CryptAcquireContextA 0x150010`, `CryptImportKey 0x150000`, `CryptDestroyKey
  0x150008`, `CryptReleaseContext 0x150034`) route to stub entries that admit the
  cache only from the six checker return sites `0x0f8bc2/0x0f8bdb/0x0f8c08/`
  `0x0f8db1/0x0f8dbd/0x0f8dd6`; every other caller forwards natively. The sibling
  signer `0x0f7c90`'s unadmitted acquire on the scratch container first calls
  `crypt_cache::retire_foreign()` (new FL-only), which drops every cached object
  so that signer's native delete/create/delete sees the namespace native alone
  would have — resolving frontier (1) of this note.

Verified (offline/host): the two site verifiers
(`verify_gz_buffer_sites_fl.py`, `verify_crypt_cache_sites_fl.py`) PASS (windows,
mutation refusals, named slots and their other users, the gate return sites, AP
refusal); the native fixture `verification/probe/fl_loading.cpp` (real relocated
X3FL.exe, MinGW i686, no Wine) PASS: gz claim/read-back/rollback, LastError kept
across install, a buffered read byte-identical to a synthetic stream, nonvolatile
registers / ESP / XMM7 / x87 CW / MXCSR / LastError kept across a read, a buffered
hit and a close entered from the register harness (added 2026-11-03 after review),
the gzclose share in both install orders with the keep modelled by a hand-written
forward stub (the real keep's share code, `music_keep_fl.cpp`'s behind-buffer
claim and uninstall, is not executed by any fixture; production installs the keep
first); the crypt cache hit/miss, the sibling retire (the post-retire miss check
corrected after review), hit-path LastError preservation.

Install order with the DAT pool (2026-11-03): the FL DAT handle pool
(`src/engine/fl/resource_reader_fl.cpp`, [resource-reader.md](resource-reader.md),
"Port (2026-11-03)") rewrites the rel32 of the `call fclose` at base+0x10696c,
inside the `close_wrapper` window (0x106940+0x3a) that `gz_buffer` and the music
keep check. Both must therefore install first: `capture.cpp`'s FL block runs the
keep, then `gz_buffer`, then the reader and pool, and each window is checked once,
at its owner's initialize. The gzclose call at 0x10695d and the window's slot
operand are not touched; a window re-check after the pool would refuse. `cmake -DX3M_EDITION=fl` builds 0/0.
Not verified: not flown; native Windows and CrossOver; a real zlib/CSP loading
run; loading time. Frontiers (2) early grouped-transaction fault-testing and (3)
the native valid-use gzip fixture are covered at the FL window/transaction level
here; a live CrossOver qualification remains.
