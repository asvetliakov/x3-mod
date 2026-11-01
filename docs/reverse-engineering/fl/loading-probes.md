# FL loading diagnostic probes

Offline contract and implementation plan, 2026-10-30. This concerns the twelve
optional engine probes selected by `X3M_LOADING_PROBES`, not the already ported
loading optimizations. No production code, build, fixture executable, Wine,
native Windows run, game launch or install was performed. The current
`loading_probes::initialize()` remains AP-gated. This note does not admit FL
instrumentation or claim a loading speedup.

Inputs are `/mnt/c/X3AP-orig/X3AP.exe`, SHA-256
`fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`, and
`/mnt/c/X3FL-orig/X3FL.exe`, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`.
Addresses below are preferred VAs at base `0x400000`; FL bindings use loaded
base plus RVA. The [AP probes](loading-probes.md),
[FL reader](resource-reader.md),
[FL resolver](resource-resolver.md) and
[gzip/crypto contract](loading-hooks.md) retain their ownership.
This note records the additional diagnostic contract and the actual present
source ordering; it does not replace those feature implementations.

## Twelve-site correspondence

`length` is the complete instruction span displaced into the existing
`engine_patch` tail; the entry JMP writes its first five bytes. `pop` is the
number of stack argument bytes removed by **every ordinary return** in the
reviewed FL body. Exact expected bytes are reproducible only in the ignored
`sites.local.json`; the tracked profile contains derived instruction witnesses
and full-body digests rather than raw program bytes.

| # | Probe name | AP VA | FL VA / RVA | length | pop | FL input/result and measurement |
| ---: | --- | --- | --- | ---: | ---: | --- |
| 0 | resource_load | `0x4e8e10` | `0x506350` / `0x106350` | 5 | 0 | Timed; ECX path, EDX extensions, stack user-directory flag; EAX buffer/null |
| 1 | resource_open | `0x4e8780` | `0x505e00` / `0x105e00` | 6 | 12 | Timed; ECX file object, stack path/extensions/user-directory; AL success; flags after success classify loose/catalogue |
| 2 | name_resolve | `0x4e7590` | `0x504b70` / `0x104b70` | 5 | 12 | Timed; ECX file object, three stack arguments; AL hit/miss |
| 3 | resource_read | `0x4e8880` | `0x505f40` / `0x105f40` | 6 | 0 | Timed; ECX file object; EAX buffer/null; output bytes from `0x5ab3e8` |
| 4 | read_dispatch | `0x4e9210` | `0x506700` / `0x106700` | 7 | 12 | Count only; ECX file object, stack buffer/element-size/count; requested bytes = size × count |
| 5 | crt_fgetc | `0x50fff5` | `0x52fc2a` / `0x12fc2a` | 7 | 0 | Count only; cdecl FILE pointer; EAX character/EOF |
| 6 | sopen_helper | `0x527869` | `0x545dff` / `0x145dff` | 5 | 0 | Timed; FL `__sopen_nolock`, seven cdecl stack arguments; EAX status |
| 7 | find_wrapper | `0x4d2950` | `0x4ebb20` / `0x0ebb20` | 6 | 0 | Timed; ECX path, EDX optional suffix, stack user-directory; EAX find record/null; original caller address bucket |
| 8 | signature_check | `0x4cabc0` | `0x4f8b50` / `0x0f8b50` | 5 | 0 | Timed; ECX text, EDX text length, five cdecl stack arguments |
| 9 | texture_body | `0x4dd2c0` | `0x4f6040` / `0x0f6040` | 6 | 0 | Timed; JPG path; ECX name, EDX options, one caller-cleaned stack argument; EAX result |
| 10 | texture_loader | `0x4dc540` | `0x4f52a0` / `0x0f52a0` | 6 | 0 | Timed; `pck dds` path, same input placement; EAX result |
| 11 | mesh_body | `0x4bc680` | `0x4dcbe0` / `0x0dcbe0` | 5 | 0 | Timed; ECX mesh owner; adjacency/clean/optimize body |

The selected FL entry instructions are, respectively: `push ebp; mov ebp,esp;
push -1`; `push ecx; push ebx; mov ebx,[esp+0x14]`; the same first sequence
for resolve; `push ebp; mov ebp,esp; and esp,-8`; `push edi; mov edi,ecx;
test byte [edi+4],1`; `push 0x10; push handler`; `mov edi,edi; push ebp;
mov ebp,esp`; `push ebx; push ebp; push esi; push edi; mov edi,edx`;
`sub esp,0x18; push ebx; push ebp`; `push ebx; mov ebx,esp; sub esp,8`
for both textures; and `sub esp,0xc; push ebx; push ebp` for mesh.
No selected displaced instruction is PC-relative. In dispatch the original
conditional jump at `0x506707` consumes the displaced TEST's flags; neither the
tail jump nor the continuation may alter them.

The fgetc handler operand at **`0x52fc2d`, entry +3**, is HIGHLOW-relocated.
It is the only relocation inside these twelve displaced spans. The expected
entry must use the loaded handler VA, and the tail copies the already relocated
operand. Copying preferred bytes into a rebased image or comparing against
unadjusted AP-style constants is wrong. The other reviewed bodies contain
additional HIGHLOW operands; their complete lists are in the compact profile.
Do not relocate a relative branch or normalize arbitrary DWORDs that resemble
addresses. `sopen_helper` at `0x545dff` straddles an aligned eight-byte word for
its five-byte write, unlike the other eleven entries; quiescent startup remains
required. It must not be moved into an unproved preceding hotpatch area merely
to obtain the atomic-write fast path.

### Changed coverage, without dropping a probe role

The AP shared resource-load wrapper is largely **inlined in FL**. The surviving
`0x506350` is an exact open/read/destruct functional counterpart, but has one
direct call, `0x4ca72e`. Open and reader each have twelve direct calls in FL;
texture functions themselves construct a local file, open, read and destroy
it. Keep `resource_load` for the actual out-of-line helper and log that scope.
Its count is not a census of all resource loads. Adding interior probes to
reconstruct AP counts is unnecessary for this diagnostic port.

The newer FL CRT has replaced AP's private sopen body. The chosen narrow
`__sopen_nolock` at `0x545dff` converts the filename, calls
`__wsopen_nolock` at `0x545e38 -> 0x545e70`, frees the converted name and returns.
Its seven arguments are unlock flag pointer, output descriptor pointer,
narrow filename, open flags, share flags, permission mode and validation flag.
The relevant open chain is
`fopen 0x532087 -> common_fsopen<char> 0x531fbb -> common_openfile<char>
0x53d3eb -> __sopen_s 0x545e50 -> common_sopen_dispatch<char> 0x545825
-> __sopen_nolock 0x545dff`. Its inclusive timing includes conversion and the
wide worker, but does not count independent wide-only callers. AP's old CRT
ABI must not be reused to invoke this function; the probe only observes it.

FL fgetc's body is `0x52fc2a`, not just the eleven-byte thunk at `0x52fd2c`.
Three direct calls target the body; six engine calls target the thunk, whose
`0x52fd32` tail jump reaches the same body. Hooking the body covers both.
Other byte reads can be inlined or use gzip; the metric is fgetc entry count,
not bytes read. Neither this probe nor dispatch substitutes a return address.

Measured direct-call instruction counts for the twelve chosen entries are
**1, 12, 1, 12, 626, 3, 1, 8, 3, 2, 2, 1**, in table order. These are static
references, not dynamic frequencies. Find's eight callers fit the existing
eight-bucket table exactly; their return addresses are `0x4a1f21`, `0x4b38a5`,
`0x4ca929`, `0x4ca9b9`, `0x4fc4e7`, `0x4fd8a5`, `0x504f7a`, `0x510e8c`.
Store actual loaded return addresses at runtime; normalize to RVAs only for
cross-base comparison. Future indirect callers can exceed the bounded table.

## Classifier binding and object lifetime

Preserve `loading_trace_light.cpp` and its shared counters, shadow stack,
LastError transport and portable Win32 APIs. Supply immutable per-edition
argument selectors to its existing probe configuration; duplicating the trace
handlers or resource reader is unnecessary. After `pushfd; pushad`, saved
slots are EDI=0, ESI=1, EBP=2, saved ESP=3, EBX=4, EDX=5, ECX=6, EAX=7,
flags=8, original return slot=9, and stack arguments start at slot 10.

FL open/read/dispatch context is **slot 6**, instead of AP's ESI/EAX/ESI.
Dispatch size/count are **slots 11/12** (slot 10 is the buffer), instead of
AP's ECX/EAX. Multiply as unsigned 64-bit requested bytes; this is not actual
bytes transferred and can exceed a clamped catalogue read. Output classifiers
still consume saved EAX/AL at slot 7. FL open and resolve require `ret_pop=12`;
dispatch's value is also 12 although its count-only path never shadows a return.
No additional dereference is needed for find caller capture at slot 9.

The only classifier object read is the existing flags DWORD at `object+4`.
Constructor `0x504930/0x504938` initializes the stream/flags; open saves ECX in
EDI at `0x505e0d`, sets catalogue flags at `0x505ec7`, loose flags at
`0x505f32`, and returns success AL at `0x505eeb/0x505f36`. It closes/replaces
streams but does not destroy the file object itself. Thus open's saved context
remains the caller-owned object through its ordinary return; read/dispatch
read flags immediately on entry while that object is an active native operand
(`0x505f61` and `0x506701/0x506703`). Callers destroy the object after use, e.g.
helper destructor `0x50639c` and buffer-owner destructor `0x506d8b`.
This establishes the selected synchronous operand lifetime, not arbitrary
concurrent use/close or a permanent loader-thread requirement.

Use the existing flags buckets: read plain/catalogue/gz-or-unopened plus
null-result; dispatch plain/catalogue/gz/unopened; open loose/catalogue/failed;
resolve AL hit/miss. The complete object remains 0x64 bytes, but classifiers
need only its proven prefix. `probe_read32` alignment/range/readability checks
and its 16-page, 100 ms cache are advisory admission checks, not a pin or a
proof against a foreign concurrent free/unmap. Valid native object lifetime
supplies that premise; do not claim the cached VirtualQuery result supplies it.
An implausible pointer remains counted and unclassified. The existing below
`0x7fff0000` classifier policy also leaves high pointers unclassified.

Bind size pointer once to **loaded base + `0x1ab3e8`**. The second global,
loaded base + `0x1ab3ec`, corroborates the existing reader contract. Plain
success stores both at `0x506089/0x506090`; gzip success stores both at
`0x50630e/0x506313`. They can retain previous values on failure: sample only
when EAX is nonzero. Do not add the two equal values. The storage is stable
image data; its value is shared non-atomic native state. Arbitrary concurrent
whole-reader calls or nested foreign modification between native return and
sampling are not newly made safe by diagnostics. No new reader lock follows
from this bounded contract.

## Return addresses, CPU state, reentrancy and exceptions

The entry stub preserves flags and all GPRs, records the original slot address
and return address, then changes only slot 9 for timed probes. The target and
its original displaced entry see the same arguments and incoming CPU state.
A call inside a probed function still pushes its own original instruction
return address: the FL CryptoAPI return gates at `0x4f8bc2/0x4f8bdb/...` and
find's callsite identities are not replaced by shadowing their *caller's*
return slot. There is no need to change the qualified crypto gates.

The two texture prologues explicitly copy their incoming return address:
`0x4f604d/0x4f52ad` read `[EBX+4]` after saving incoming ESP in EBX, and
`0x4f6050/0x4f52b0` copy it into the aligned frame. Their ordinary exits restore
ESP from EBX, pop saved EBX, then RET through the **original incoming slot**;
all three JPG and both DDS return preludes are checked. This is a stack-frame
copy, with no observed comparison/classification of the numerical address.
The substituted exit stub therefore returns with the expected original slot
arithmetic. This bounded inspection does not assert that every transitive CRT
callee is indifferent to all stack-walking/debugger observations.

Let the original return slot be S and callee cleanup N. RET leaves ESP at
`S+4+N`; the shared exit stub reserves a placeholder, saves flags/GPRs and
passes the frame to `x3m_probe_exit`. Its `&regs[10]` is `S+4+N`, so
`here-4-ret_pop` recovers S for both N=0 and N=12. The handler writes the saved
original return address to the placeholder; `popad; popfd; ret` restores native
EAX/EDX and all other GPRs, then returns to the original caller. Eight finite
slot/cleanup arithmetic cases are checked offline; they are not executed stubs.

**Concrete existing shared-stub correction required:** `emit_exit_stub()`
currently begins `sub esp,4; pushfd`. SUB changes arithmetic flags before they
are saved, so it does **not** preserve the native return EFLAGS claimed by the
older AP verification prose. Replace that one adjustment with
`lea esp,[esp-4]` before PUSHFD. The frame offsets stay identical; source-counted
stub size grows from 23 to 24 bytes, within its existing 32-byte emitter
reservation. This is a candidate, not an applied or executed correction. It
needs one focused shared AP/FL seeded-return-flags/GPR/LastError fixture when
implemented. Entry and exit compiled handlers must retain their no-SSE/no-MMX/
no-x87 discipline and four-byte incoming-stack support; no built object was
requalified here. The stub itself saves no XMM/x87 state, so emitted handler
checks remain necessary. This is a finite changed-root qualification, not a
new theorem about every reachable CRT routine.

A bounded return-site liveness scan covers **43 direct calls to timed sites**:
31 first redefine flags and twelve first reach another call boundary; none
reads returned flags before that event. It stops at that first event and does
not follow a complete caller CFG. It supports the observed usage but cannot
replace the minimal shared correction or promise flags are universally dead.

Retain the existing per-thread, 64-entry shadow stack, nested timing and
fail-open overflow/allocation behavior. A timed recursive/nested call gets a
separate entry; count-only dispatch/fgetc never add one. Unwound frames are
removed and counted as desync on a later return according to their saved slot,
not by running an exit callback during unwind. SEH/C++ registration/cleanup
exists in resolve, resource-load and texture bodies; their reviewed ordinary
returns and local cleanup do not constitute executed exception coverage.
Implementation acceptance needs mixed ret/ret-12 nesting and nonlocal-unwind
cases as well as normal returns. Do not reset/rebind configs, size pointer,
TLS indices or shadow storage while any substituted return can survive.
The current empty-shadow exit has no valid original target; preserving its
matching entry is a control-flow requirement, not just telemetry correctness.

Keep handler paths free of logging and game calls. QPC, TLS, HeapAlloc and
Interlocked operations remain existing bounded mechanics; only the first timed
entry on a thread allocates a shadow block. The classifier cache may query a
page on a miss and scans at most sixteen entries; caller recording scans at
most eight. Every call/exit updates counters and times; fgetc/dispatch remain
count-only because of their frequency. No per-draw code or new scan/allocation
is proposed. These static costs do not establish game loading overhead.

## Canonical reader sharing and installation order

The root decision for FL is **existing reader first, diagnostic outside it**:

`entry -> dispatcher -> diagnostic probe -> existing reader stub -> native tail`

`resource_reader::canonical_site()` is the one shared Site. The current
`loading_probes::install_site()` calls `engine_patch::claim()` unconditionally;
`claim()` returns `already_claimed` on an existing owner. FL cannot use that
path unchanged. Narrowly recognize the already prepared canonical Site by
loaded address, complete original six-byte entry, length and clean ownership/
protection state; stage the probe's next pointer from its current `entry` word,
then use existing `store_pointer` / `push_front` to attach. Preserve the reader's
original continuation, binding, restore responsibility and transaction debt.
Do not repatch its EXE entry or create another reader Site/core.

For FL this outer `resource_read` metric counts/times **external requests,
including fast-reader successes**. Verify's internal `call_original` invokes
the reader's already captured continuation (native tail), so it does not enter
the outer probe a second time. Native-only mode still lets the diagnostic claim
and own the canonical Site when no reader is prepared. Pool-only mode likewise
does not by itself install a reader entry stub.

Current AP source does the opposite insertion order: `loading_trace::initialize`
installs probes, then `capture.cpp` calls `resource_reader::initialize`, which
pushes the reader in front. AP is therefore `reader -> probe -> tail`:
fast success bypasses that probe and verify's internal original call enters it.
The older AP prose diagram's opposite order is not the present source order.
Keep AP behavior unchanged and label FL's outer scope explicitly; those two
`resource_read` counts/timings are not directly comparable native-only metrics.

### Narrow ownership and rollback evidence

The present source contracts are reproduced as fourteen ordered source witnesses
and full source digests in `summary.json`; these are static witnesses, not an
executed attachment implementation. In `resource_reader.cpp::install_stub`,
`store_pointer(next, *target.entry)` precedes `core_bind(..., next, ...)` and
`push_front(target, stub)`. `resource_reader_core.cpp::core_bind` stores that
private slot as `continuation`; verify calls `call_original(*continuation,
object)`, and the FL bridge places the object in ECX (`input_slot == 6`). Thus
attaching only to the canonical head later leaves the private native tail
unchanged. Rebinding the core or rewriting the reader's next slot would break
this argument and is outside the attachment contract.

`engine_patch.cpp::claim` refuses `restore_pending` before `claimed`. A
**successful** claim nevertheless leaves `claimed`, `patched_in` and
`restore_pending` all true: the last bit records its eventual restoration
obligation. Therefore "clean" attachment admission must mean a recognized active
reader, owned entry bytes and restored code/arena protection; it must not require
`restore_pending == false`. An incomplete state with restore debt and no active
patch is refused. The diagnostic must neither clear the reader's restore bit
nor call `claim` again to acquire its ownership.

`store_pointer` writes `*slot` before checking arena finalization and can return
false after publication; `push_front` then reports null although the head may
already equal the new stub. A failed return alone cannot establish that nothing
became reachable. Quiescent rollback must inspect the head, conditionally restore
its saved predecessor only if it still equals this diagnostic stub, retain all
reachable continuations/configuration, and finish arena recovery before treating
the transaction as clean. A foreign head must not be overwritten. The existing
function is an ordinary locked store, not a compare/exchange conditional detach;
the startup owner's quiescence is the premise for that comparison and write.

`engine_patch::restore` restores EXE entry bytes, with owned-prefix and displaced
tail checks; it does not pop a chain node. Calling it to remove an externally
owned diagnostic attachment would disable the reader as well. Conversely the
current probe report requires `reader_probe_claimed`, so leaving that ownership
bit false would hide the attached metric. Keep a distinct attachment/report
state and code-site restore owner. Existing FL `resource_reader::shutdown`
returns without late unhook; this port must retain that process-lifetime policy.
Cold reverse-order rollback must remove the diagnostic attachment before the
reader owner restores its entry, while preserving either owner's sticky recovery
debt on failure. These are required implementation conditions; no conditional
detach, rollback or uninstall fixture has run here.

The finite cold sequence is:

1. At the recognized FL pre-CRT boundary, outside DllMain and before game
   threads/workers resume, freeze the diagnostic request, edition/base and
   classifier configuration. Retain the existing telemetry/debug flag policy.
   Validate the original diagnostic windows, relocation metadata, result-global
   bounds and overlaps **before reader/pool writes**. This matters because the
   existing reader's own full-window qualifier includes open, dispatcher and
   helper bodies, and the pool edits a call inside the open body. Installing
   diagnostics first would invalidate the existing reader qualifier; validating
   unmodified diagnostic body hashes only after reader/pool writes also fails.
   Staged pristine preflight avoids weakening either qualifier or teaching it a
   generalized foreign-patch normalizer.
2. Run the existing `request_fl_startup` / `prepare_fl_startup` reader/pool
   owner unchanged. Stage common handler readiness, configured size pointer,
   exit stub and continuations before publishing diagnostic entries. Handler
   readiness requires the already initialized proxy runtime and direct Win32
   support, not an EXE CRT function call during preparation. The proposed FL
   probe request/prepare entry points do not yet exist.
3. Claim the other selected sites and attach the resource-read probe to the
   canonical entry word as above, or claim that Site if it is genuinely unowned.
   Keep per-site refusal/reporting. A failed allocation or byte/ownership check
   disables the affected diagnostic; it must not tear down a healthy reader or
   silently drop one of the twelve report slots. Finalize the shared executable
   arena and retain any reachable storage/debt before resuming game entry.
4. Roll back only owned diagnostic writes while still quiescent. If attachment
   fails after pointer publication, restore the saved entry word only when it
   still equals this attachment; retain the reader's entry JMP/owner. An
   unowned canonical Site claimed by the probe has separate restore debt.
   Reader transaction rollback must not leave a live diagnostic chain targeting
   invalid state. Use the existing startup reverse-order rollback boundary;
   exact conditional attachment/rollback ownership needs a narrow fault fixture.
   Arena finalization failure remains sticky debt, not successful rollback.
5. After resumption there is no new live reinitialization/unhook protocol.
   Reporting/shutdown must distinguish diagnostic attachment from Site ownership;
   the existing AP `reader_probe_claimed` flag cannot mean both. In particular,
   current report's skip when that flag is false would omit an externally owned
   FL reader, while giving it true would let current shutdown restore the
   reader's entry. Retain reachable stub/TLS/shadow storage through process exit.

This requires a small edition profile and narrow attachment/ownership handling,
not a new general hook framework. Common portable IAT loading rows remain owned
by `loading_trace` and the existing FL adapters; they are not implemented again
inside this pack. No second resource resolver, decoder or read bridge is needed.

## Offline acceptance and remaining work

Run `python3 verification/results/fl-loading-probes/produce.py`.
The [producer](../../../verification/results/fl-loading-probes/produce.py),
[derived profile](../../../verification/results/fl-loading-probes/profile.json) and
[summary](../../../verification/results/fl-loading-probes/summary.json) are the
reproducible compact artifacts. `--raw` emits ignored exact site bytes;
`--decompile` uses the existing read-only FLRegistry Ghidra project and writes
ignored targeted output. No image or analyzed database is modified.

Measured PASS: **12 AP entry definitions**, **18 FL windows**, **3,363 window
instructions**, **261 semantic anchors**, **168 HIGHLOW/in-image absolute
operands**, three synthetic bases (`0x400000`, `0x17000000`, `0x90000000`), and
**276 rejected mutations**. The 18 windows comprise twelve complete selected
bodies and six ABI/caller/prefix witnesses. Complete body digests bind the
reviewed code; sparse anchors alone do not establish complete behavior.
Both window edges and each displaced endpoint match independent linear
`.text` decoding from `0x401000`. The whole `.text` scan has 428,900 decoded or
skipdata rows, finds zero direct incoming edges into selected displaced
interiors, and finds no literal DWORD entry pointers anywhere in the image.
It is not a computed-alias or reachability proof.

There are zero overlaps among selected probe spans and zero with the **19**
existing pool call spans (one open plus eighteen closes). Resource-read's exact
six-byte overlap with its canonical entry is intentional and requires the
single-Site chain, not competing entry writes. Gzip/crypto integration modifies
IAT pointers rather than these code-entry spans. This is the named loading
patch set comparison; it is not a general proof of every future patch owner.

Negatives bypass image/region digest rejection: 261 instruction first-byte
changes; six classifier/global displacement changes; three RET cleanup payload
changes; missing/wrong-kind/opcode-overlapping relocation; both interior window
edges; and a synthetic incoming edge into the open entry. Synthetic rebasing
checks boundary stability and normalized digests, not a mapped-image execution.
Eight shadow-slot arithmetic cases, fourteen source ownership witnesses and the
concrete SUB-versus-LEA EFLAGS metadata are also checked. JSON round-trips through
the parser. Command timing
is retained in the summary; no Wine lock was acquired because nothing ran under
Wine. Raw `*.local.*` files are excluded by the directory's own ignore file.

Finite remaining implementation acceptance is: immutable FL classifier binding;
relocated expected fgetc operand; native shared exit-stub flags/GPR/LastError
and four-byte-stack checks; the conditional canonical attachment/rollback and
reader-native/fast/verify coexistence fixture; mixed cleanup/nesting/unwind
and allocation-overflow cases; exact emitted no-FP checks for changed handlers;
and first-call/report behavior after the pre-CRT preparation. Native Windows
and CrossOver execution through the queue owner, and user-supplied loading
captures/overhead measurements, remain separate gaps. They are not prerequisites
for recording this offline map and are not claimed by its PASS result.
