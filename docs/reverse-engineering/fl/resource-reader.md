# Farnham's Legacy resource reader and DAT handles

Bounded offline contract, 2026-10-29. Clean AP and FL executables were read from
`/mnt/c/X3AP-orig/X3AP.exe` and `/mnt/c/X3FL-orig/X3FL.exe`; neither was modified
or executed. AP SHA-256 is
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, FL SHA-256 is
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Addresses below are preferred VAs, base `0x400000`. FL bindings must use loaded
base plus RVA, including globals and private CRT functions. This note supplements
[the AP reader](resource-reader.md), [FL selection](resource-resolver.md)
and [the loading ABI/lifetime contract](loading-hooks.md).
It establishes a port contract, not runtime FL qualification.

## Correspondence and ABI

| Role | AP | FL | Established contract |
|---|---|---|---|
| Whole resource reader | `0x4e8880` | `0x505f40` | AP object in EAX; FL object in ECX; buffer/zero in EAX; plain ret |
| Extension-aware open | `0x4e8780` | `0x505e00` | FL ECX object, three stack arguments; ret 0xc; selection precedes payload open |
| Dispatcher | `0x4e9210` | `0x506700` | FL ECX object, stack buffer/element-size/count; ret 0xc; AP private register ABI differs |
| Byte reader | `0x4e91b0` | `0x5066b0` | FL ECX object; gzip header also contains inlined byte-reader paths |
| Close | `0x4e9360` | `0x506940` | ECX object; plain ret; FL normalizes native close status to 0/1 |
| CRT fread | `0x512213` | `0x532c9e` | cdecl `(buffer,size,count,FILE*)`, size_t EAX |
| CRT fseek | `0x510343` | `0x53074e` | cdecl `(FILE*,long,int)`, signed int EAX |
| CRT ftell | `0x5128e6` | `0x531fb0` | cdecl `(FILE*)`, signed long EAX |
| CRT malloc/free | `0x5112c4` / `0x50e1b0` | `0x531395` / `0x52f4e5` | cdecl; FL thunks to `0x5337fd` / `0x533b75` |
| CRT fopen/fclose | `0x512a18` / `0x50f8d4` | `0x532087` / `0x52fb6e` | cdecl; use the edition's own CRT for its streams |
| DAT fopen call | `0x4e87ff` | `0x505eb0` | five-byte direct call, rb mode; loose call is FL `0x505f0f` |
| Close's fclose call | `0x4e9392` | `0x50696c` | five-byte direct call; **not FL's only object close route** |
| Result-size globals | `0x596988`, `0x59698c` | `0x5ab3e8`, `0x5ab3ec` | both equal output size after successful gzip read |
| Live-byte accounting pair | `0x6085f4`, `0x6085f8` | `0x5aae20`, `0x5aaa18` | each gains size; reader failure subtracts size from these two |
| Cumulative bytes / allocations | `0x6089f8` / `0x6089fc` | `0x5aaa14` / `0x5aae1c` | gain size / one; reader failure does not undo them |

The accounting pair is mapped by its update/rollback role; no distinct semantic
name for each AP member is inferred. FL allocator `0x4d8fc0` takes size in ECX
and flags in EDX. Reader calls at `0x506025/0x506095` explicitly set EDX=0.
The allocator calls CRT malloc, retries after the guarded recovery callback at
`0x4d906a`, updates the four counters at `0x4d908e..0x4d90a7`, then zeroes the
allocation at `0x4d90b1`. Binding the fast core's malloc to this wrapper would
double-count and restore the memset cost. Bind **raw CRT malloc** `0x531395`;
retain the core's explicit successful accounting. Failed speculative allocation
falls back to native, where the engine's retry/recovery behavior remains intact.

FL preserves EBX/ESI/EDI/EBP on normal reader exits. ECX/EDX/EAX and EFLAGS are
call-clobbered. Its prologue pushes EBP, sets EBP=ESP, aligns ESP down to eight,
and reserves `0x458`; it restores the incoming frame on each ordinary exit.
There are no stack arguments to pop. The full first **six** bytes end exactly
at `0x505f46`: `push ebp; mov ebp,esp; and esp,-8`. This is a complete >=5-byte
entry-detour candidate with no relative operand and no HIGHLOW operand. Three
bytes alone cannot hold the entry JMP. AP's existing six-byte stack-reservation
claim remains an independent edition witness.

The direct-call census finds **12 FL reader calls** in eleven containing
functions, including helper `0x506350`, buffer owner `0x506d30` and two font
paths inside `0x514a40`. Call sites are in the compact record. FL callers supply
ECX and use EAX after return; they do not depend on incoming EFLAGS. No direct
branch/call targets the interior of the six-byte candidate. A whole-file DWORD
search finds no literal FL reader pointer. This is not a proof against a
computed alias. AP has **three**, not the earlier AP note's two, direct calls:
`0x4e8ead`, `0x4f7a71`, **`0x4f7c0c`**; the last also puts its object in EAX.

## Resource object, selection and decoding

The prefix consumed by `FileObject` is identical in both editions: stream `+0`,
flags `+4`, record offset `+8`, length `+0xc`, cursor `+0x10`. Masks 1/2/4/0x10
remain open/catalogue/gz/progress. FL's complete object is 0x64 bytes, with
0x18-byte strings at `+0x14/+0x2c/+0x4c` and catalogue-related field `+0x48`.
AP's later string offsets differ; the shared reader must use only the proven
0x14-byte prefix. Constructor `0x504930` initializes it; destructor `0x5049a0`
closes if open, then destroys its three strings. The open bit, not a null stream
pointer, controls lifetime: close clears the bit but may retain a stale pointer.

Keep selection outside the decoder. FL extension open `0x505e00 -> 0x504b70`
chooses loose matching resources first, then catalogues in descending priority.
It opens only the selected payload, and a DAT-open failure is not a request to
try a lower catalogue or loose file. The exact-name open `0x5063c0` has a different
catalogue-first policy and its own DAT fopen at `0x5064d8`; pooling only
`0x505eb0` intentionally retains the existing AP extension-open scope. The fast
reader sees an already opened, positioned stream and must not repeat selection.
See the resolver note for root/addon2/mod ordering and namespace rules.

FL's DAT open stores entry offset/length at `0x505ecd/0x505ed8`, zeroes cursor
at `0x505edc`, and fseeks to the record at `0x505ee3`. Loose opens begin at zero.
The dispatcher clamps **requested** bytes against length minus cursor, fread's
with element-size one, XORs 0x33, and advances cursor by the clamped request,
not actual fread result (`0x50678e`). Thus short native reads retain the original
quirk of XORing/advancing the requested range; the fast path must fall back.
FL already vectorizes the catalogue XOR with SSE at `0x506754..0x50677f` and has
a scalar tail at `0x506785`. No new speed estimate follows from AP scalar costs.

The decoded reader agrees with the existing core's accepted-format semantics:
three-byte probe; direct gzip magic or key `(first_byte XOR 0xc8)` for following
bytes; catalogue XOR 0x33 first; trailer ISIZE; CM=8; no reserved FLG bits;
FEXTRA/FNAME/FCOMMENT/FHCRC skipping; raw inflate with version `1.2.3`, stream
size 0x38, windowBits=-15. FL imports init/inflate/end through
`0x5503fc/0x550400/0x550404`. Trailer CRC is read but not checked. Original gzip
allocation is zero-filled. Invalid header or inflate-init failure frees and
subtracts the two live counters (unless size is zero); result-size globals keep
their prior value. An inflate error is **not** a native failure: the 1 KiB loop
continues to EOF and returns its partially written, otherwise zero buffer.
The loop at `0x506292..0x5062f6` stops on stream end or read-zero. Hence the AP
short-cursor formula remains applicable, with first deflate byte position H,
deflate bytes consumed C and extent L:

`final = min(L, H + 1024 * ceil(C / 1024))`.

The fast core handles only valid, nonempty gzip with exact produced ISIZE; all
other cases restore position/cursor and run native. Successful output bytes,
both size globals, all four counter deltas and final stream/cursor match this
FL algorithm by static correspondence. Existing AP fixture evidence is reusable
for unchanged core behavior, including the 1..8-byte trailer-tail cursor class;
it is **not** an executed FL adapter comparison. Plain-copy, gz-handle and
progress-callback paths remain native. No promise of exact malformed-input
termination, incidental errno/LastError or unread buffer contents is added.
The current restore guard ignores failed restoration seeks: existing evidence
does not qualify a failed fseek as an exact-entry-state fallback. Preserve this
limitation explicitly in later fault testing rather than claim every I/O error
is reversible.

## The CRT FILE contract and the pool's real closure surface

FL uses a different static CRT. Observed prefix: current pointer `+0`, buffer
base `+4`, remaining count `+8`, flags `+0xc`, descriptor `+0x10`, buffer size
`+0x18`. AP/VC8 instead has count `+4`, base `+8`. **Same flag offset does not
mean same flag values.** FL fread's `__fread_nolock_s` establishes flags address
at `0x532b44/0x532b47` and sets **error=0x10** at `0x532c83/0x532c89`; read-zero uses
**EOF=0x08** at `0x532c9a`. FL fseek clears EOF at `0x530682/0x530688`; its
ordinary seek path clears count and resets current pointer to base at
`0x5306cd/0x5306d1`. A binary-read fast-track can reposition within its buffer,
so it is inaccurate to require every successful FL seek to discard the buffer.
The equivalent stream position/read semantics are the relevant pool contract.
The AP pool's default `error_flag=0x20` must never be inherited for FL. Existing
`PoolEnvironment{flag_offset,error_flag}` suffices: FL needs `{0xc,0x10}`.
Neither edition's FILE may be passed to the proxy's CRT; scratch HeapAlloc and
game output malloc/free retain their separate existing allocator ownership.

The pair `0x505eb0/0x50696c` is **insufficient** for a safe FL pool port. Normal
buffer owner `0x506d30` reads at `0x506d81`, then calls destructor `0x5049a0` at
`0x506d8b`; the destructor's actual fclose is **`0x5049c5`**, bypassing
`0x506940`. Extension/exact reopen also inline closes (`0x505e50/0x506410`), as
do owning-close and other file-object paths. A table entry left "in use" after
one such real fclose is stale; later FILE address reuse can make a pool close
misidentify a different stream. This is an actual reachable ownership mismatch,
not merely an unproved engine thread protocol.

The all-text direct-reference census finds **18 fclose calls** and no other
instruction operand referencing `0x52fb6e`; a whole-file DWORD search finds no
literal pointer to it. The compact record lists all 18 addresses. A bounded
shared-pool adaptation can redirect this finite set of call sites to the same
existing cdecl pool_fclose, passing unknown handles through to original fclose.
Only the selected DAT fopen is pooled. This avoids guessing which compiler-
inlined closer a borrowed object may reach. Each call is five bytes, and none
has a direct incoming edge into its interior. This is an implementable binding
option, not a claim that these writes have been installed or fault-qualified.
If a narrower subset is chosen, its participating-handle closure coverage must
be justified with actual call flow; the two-site AP assumption cannot supply it.

AP also has a pre-existing bypass: owning-close `0x4e7350` clears open and calls
fclose directly at **`0x4e737e`**, before invoking destructor `0x4e73f0`. The
current AP pool only replaces `0x4e9392`. This contradicts the AP note's broad
"no other path closes" statement. It does not demonstrate that the recorded AP
resource loads exercised the bypass. Keep the accepted AP behavior/evidence
unchanged; do not silently generalize its pool-closure claim to FL.

## Lifetime, reentrancy, installation and cost

Ordinary reader callers own temporary file objects, close them synchronously
and retain only the returned buffer. `0x506d30` stores that allocation in owner
`+4/+8` and copies the result sizes into `+0x10/+0xc`. Reader itself has no local
FS exception registration; callers such as `0x506350/0x506d30` install SEH/C++
frames and destructor cleanup. Ordinary cleanup and compiler metadata were
inspected, not executed unwind paths. Foreign exceptions/nonlocal exits can
bypass speculative scratch cleanup; no exception should escape proxy C++ across
these private C boundaries.

Native CRT operations use stream locks, but those do not make overlapping
object close/use valid. Retain native's live, serialized per-handle operation
precondition, allowing synchronized handoff between threads. The pool never
hands an outstanding FILE to two objects; nested same-DAT opens obtain distinct
handles. There is no evidence requiring permanent loader-thread affinity or a
new owner framework. Global result sizes and accounting are non-atomic engine
state; arbitrary concurrent whole-reader calls are not proved safe. Recovery
callback `0x4d906a` can reclaim resources; do not hold bookkeeping locks across
native allocations/callbacks. The existing reader uses per-call stack/zlib/
scratch state and atomic statistics; immutable bindings must be published before
entry and must not be rebound while live. No additional reentrancy guard or
per-read lock is justified by this inspection.

The smallest production shape is the existing decode/pool core plus one
immutable edition binding: entry SiteSpec, input-register adapter, CRT functions,
counters/globals, pool call lists and the two FILE flag parameters. FL's entry
wrapper reads saved ECX (`pushad` slot 6) and publishes result in saved EAX
(slot 7); AP keeps EAX input/output (slot 7). Verify-mode original-call bridge
must similarly set ECX for FL, EAX for AP, preserve nonvolatiles and use plain
ret. Reuse a single canonical reader Site when loading_probes also participates;
FL must not accidentally obtain AP's existing `0x4e8880` claim through
`loading_probes::resource_read_site()`. This requires only shared site identity,
not a port of unrelated diagnostic probes.

Install in the already established [early startup boundary](../../architecture/farnhams-legacy-install-boundary-decision.md),
outside DllMain before handing control to EXE CRT. Stage binding, original
continuations and ready adapters first; validate/claim all required spans,
rollback owned partial writes, and retain reachable pinned storage. Pool close
coverage must be ready before pooled open is exposed. There is no authorized
late reinitialization/unhook or new stop-the-world protocol. Preserve four-byte
incoming alignment, flags/nonvolatiles at entry, and existing no-SSE/no-x87 core
qualification; any new adapter needs its emitted-code/seeded-register checks.
FL's native dispatcher clobbers XMM0/XMM1, but this does not justify introducing
unreviewed CPU-state changes in a new entry bridge. Fast mode retains the AP
policy of preserving incoming LastError; verify must capture native LastError
before diagnostics and return it. Pool reuse intentionally substitutes open/
close results for healthy immutable read-only DAT files: filesystem replacement
while a handle is retained and native close failures are not qualified by
byte-equal decode evidence.

Static cost: one scratch allocation per accepted decode plus one game output
allocation; one whole-extent read/XOR/header walk/inflate; a reposition only for
the short-cursor class; atomic counters/timing already present. Pool scans at
most 32 entries and holds one SRW table lock per reuse/keep/registration; actual
fopen/fclose occurs outside that lock in ordinary paths (`pool_drain` currently
closes kept handles while holding it). No per-draw work or new hot-path search
for edition addresses is needed. Resolve and validate once; pass immutable
pointers. FL already improves native XOR and has a newer CRT, so AP speed ratios
are not an FL performance prediction. No new timing, game FPS, loading or native
Windows qualification was measured here.

## Reproduction and remaining evidence

Run `python3 verification/results/fl-port/resource-reader-contract/reproduce.py`.
The [producer](../../../verification/results/fl-port/resource-reader-contract/reproduce.py)
and [compact record](../../../verification/results/fl-port/resource-reader-contract/summary.json)
are the only result artifacts intended for tracking. Measured: **65 windows,
2,067 window-decoded instructions, 128 semantic anchors, five named imports,
51 FL HIGHLOW operands, three synthetic bases, 147 rejected mutations**. Window
counts include overlapping caller/close witnesses; they are not unique CFG-node
counts. Both edges are checked against independent linear text decoding. Direct
incoming-edge checks cover both edition entry candidates and pool call spans.
Negatives bypass hashes: 128 opcode changes, nine immediate/displacement changes,
five import-identity changes, four relocation changes and one interior boundary.
Full-region digests bind the reviewed flow; sparse anchors do not alone certify
all behavior. Rebased decoding validates operand movement, not mapped execution.
The deterministic JSON is parsed again before writing.

Optional `--decompile` reproduces read-only Ghidra navigation using the existing
FLRegistry analyzed project. Raw output/logs remain ignored/local; no EXE or
copyrighted dump is committed. Finite remaining implementation evidence is the
FL ECX bridge/canonical site, complete closer binding, rollback and seeded ABI,
FL accepted-format comparison including cursor/error restoration cases, and
native Windows/CrossOver execution through the established queue owner. This
analysis does not establish an FL speedup or claim the enhancements are active.

## Port (2026-11-03)

Both settings now work on FL (`src/engine/fl/resource_reader_fl.cpp`, sites in
`resource_reader_sites_fl.h`, identity windows in `loading_windows_fl_inc.h`
tables `reader` and `pool`). The decoder, the entry handler and the pool are the
shared `src/proxy/resource_reader_core.cpp`, unchanged and AP byte-identical; the
FL module provides `resource_reader::initialize/report` in place of the AP hook
module. The earlier "not ported" state (one `ap_only reason=not_ported` row each,
ini text "on Farnham's Legacy the game reads natively") is withdrawn: AP also reads
natively until the mod's reader replaces it; the settings exist because the
reader is faster.

**Reader.** `engine_patch::claim` on base+0x105f40 (the six bytes above, atomic
five-byte jump, the tail re-executes them) with the FL stub in front: `pushfd;
cld; pushad`, copy saved ECX into the saved-EAX slot the core reads (`regs[7]`)
while keeping the caller's EAX in EDX, call `x3m_resource_read_entry`; handled →
`popad; popfd; ret` with EAX = the buffer; fallback → the caller's EAX written
back into the slot, `popad; popfd; jmp [next]`, so the original sees the caller's
EAX, ECX and EFLAGS (DF included). Verify mode: the core's `call_original` passes
the object in EAX, so its continuation is a bridge `mov ecx,eax; jmp [next]`.
Bindings (RVAs, each checked from the bytes by the verifier, not from a list):
fread 0x132c9e (the dispatcher's calls), fseek 0x13074e (seek, DAT open), ftell
0x131fb0, raw malloc 0x131395 (the allocator's own calls; the allocator 0xd8fc0
is not bound: it would double-count and zero-fill), free 0x12f4e5; counters
{0x1aae20, 0x1aaa18, 0x1aaa14, 0x1aae1c} as the allocator's `add` / `add
[ebx*4+…]` / `inc` operands with EDX=0 at both reader calls; size globals
0x1ab3e8/0x1ab3ec (the reader's two stores); inflate from zlib1.dll by name,
refused unless the reader's own slots 0x1503fc/0x150400/0x150404 hold the same
exports. Install checks the eleven `reader` windows (the whole reader,
dispatcher, seek, byte reader, allocator, five CRT heads) first; the jump is read
back; any later failure restores the entry and reads it back as the original
(`rollback_failed` otherwise). Row: `resource_reader mode=… installed= status=
site= input=ecx … window= edition=fl`; `resource_reader_metric … edition=fl`
from the telemetry report and at last-device teardown.

**Pool.** The `.text` scan finds 18 E8 calls to fclose (base+0x12fb6e), equal to
the `fl-codex` reference list, no E9 and no relocated pointer to it. Seventeen
are redirected to `x3m_pool_fclose` (unknown handles pass through to the CRT
fclose), then the DAT open's `call fopen` at 0x105eb0 (the `"rb"` push before
it, the string at 0x18bb9c) to `x3m_pool_fopen`: close coverage is live before a
pooled handle can exist. The eighteenth, 0x1397e4, is **not redirected**: it is
the static CRT's fcloseall loop (0x13979c), whose only caller is the exit-time
stdio teardown 0x12ee72, which nothing calls directly and only the `.rdata`
pointer at 0x150480 reaches. Through the pool it would close kept handles while
they stay listed as kept (and skip in-use ones), so a DAT open racing process
exit could be handed a closed FILE; left native it closes every stream at exit,
which is what the CRT does without the pool. Each write is read back; any
failure puts every write of the transaction back, newest first, each read back
as the original call (`live=0`). If even that fails (`rollback_failed`) the pool
stays bound, so a site left redirected still reaches the CRT's fclose for every
handle it does not keep (an unbound core pool close returns -1 without
closing). The pool tests FL's error flag 0x10 at FILE+0x0c (fread's `lock or`);
EOF 0x08 alone keeps the handle. Row: `dat_handle_pool requested=1 installed=
status= open_site= open_status= close_sites=17 live=18 window= entries=32
error_flag=0x10 … edition=fl`.

**Order and the keep.** The fclose call at 0x10696c lies inside the
`close_wrapper` window (0x106940+0x3a) that the music keep and `gz_buffer` both
check at their own install. Both run first in `capture.cpp`'s FL block; this
module claims last (the order is fixed in `capture.cpp` and each window is
checked once, at its owner's initialize). The pool rewrites only that call's
rel32 (window +0x2d..+0x30), clear of the window's relocated gzclose operand
(+0x1f), the gzclose call at 0x10695d and its return 0x106963 that the keep's
stub compares at run time. A re-check of the keep's window after this install
refuses, so neither the keep nor `gz_buffer` may initialize after it (no FL
path re-initializes them). Cross-references: `music-keep.md` section 3,
`loading-hooks.md` "Port (2026-11-03)".

**Evidence (host, 2026-11-03).** `verify_resource_reader_sites_fl.py` PASS (19
checks; reader 69 / pool 68 window mutations refused; the exit-time exclusion
proven from the bytes; the AP image and AP addresses refused). Native fixture
`fl_resource_reader.cpp` PASS (69 checks): it runs the real relocated FL reader
as the native route over an in-memory CRT with FL's FILE layout and the game's
zlib1.dll, then the production install over it. 14 cases (11 catalogue records
in one 88,816-byte `.dat`, 3 loose files: plain, scrambled,
FNAME/FEXTRA/FCOMMENT/FHCRC, 100 B to 200 KB, two short-cursor records, the last
record ending at EOF, not-gzip, empty, cut streams) are identical to the native
route in bytes, both size globals, all four counter deltas, cursor and position
(10 handled, 4 fallbacks); 16 sequential and out-of-order reads on one handle
identical; verify mode 10/10 equal; registers, EFLAGS/DF, XMM7, x87 CW, MXCSR,
LastError and errno kept on the handled and the fallback path (the original
receives the caller's EAX/ECX). Per-site redirection is proven by the install
read-back (17 calls decode to the pool close, the exit call still to fclose);
the close wrapper and the destructor (engine code) pool a DAT handle; the pool
close, reached once through each of the 17 site targets (all the same
function), pools the DAT handle and releases an unpooled one; reuse across
reopen, distinct handles for concurrent opens, error-flag release, non-`rb`
pass-through; 20 fault points each roll back their transaction; the late window
refuses with nothing written. Ledger: `docs/verification/farnhams-legacy.md`,
"resource reader and DAT handles on FL (host evidence, 2026-11-03)".

**Cost.** The FL adapter adds the stub (about twenty instructions per call) and
no allocation; per handled read the shared core keeps its AP shape: one scratch
`HeapAlloc` of the extent plus the one game `malloc` the native allocator also
makes (fixture: 1.00 game allocation per read on both routes). Fixture timing
(native Windows via WSL interop, in-memory CRT, not game time;
`verification/results/fl-loading/run_resource_reader_fixture.py --runs 3`):
200 KB text about 2.4× faster (13.3–17.0 µs vs 32.9–37.1 µs); 70 KB random
taken as parity (this checkout's runs 24.1–24.3 µs vs 31.0–42.4 µs, the
reviewer's 31.60 vs 31.44 µs; the fixture's byte-loop fread dominates both);
5 KB text slower, 5.0–5.5 µs vs 2.5–2.7 µs; 100 B 1.6–1.7 µs vs 1.4 µs. Small
records are slower in the fixture (its `HeapAlloc`-based allocator shows
1.3–1.7 µs in the core's allocation phase at 5 KB, plus the core's fixed scratch
and QPC work); AP's fixture ratio was 1.11× on a 31 KB file. No FL loading-time
gain is claimed until a flight's `resource_reader_metric` (verify: `original_us`
vs `fast_us`) measures it.

**Limits.** Not flown; native Windows and CrossOver not run. The restore guard
still ignores a failed restoring fseek (above). The pool scope is the extension
open's DAT fopen only (the exact-name open 0x1064d8 is not pooled, as on AP).
Handles still kept at process exit are closed by the CRT's own fcloseall.
