# Farnham's Legacy startup callback and installation exclusion

This is the focused continuation of [startup ordering](startup.md)
for the [lifetime activation contract](../../architecture/farnhams-legacy-lifetime-activation.md).
It does **not** qualify installation at Direct3DCreate9 or enable FL consumers.
The ordinary first-probe call remains the earliest established EXE-owned proxy
entry: `0x4772bc -> 0x4f13b0 -> 0x4f13cd`, returning to `0x4f13d3`.
Its ordinary caller stack contains no registry helper. Earlier callbacks have
been narrowed below; no earlier engine-helper worker or actual overlapping
helper execution was demonstrated. Process-wide exclusion remains unproved,
which is different from finding a concrete unsafe schedule.

## Reproduction and scope

Run `python3 verification/probe/verify_fl_startup_exclusion.py`. It uses the
project PE reader, Capstone, and the established EXE reference-boundary check;
it never executes a game/DLL instruction. Measured: **46 regions, 2,345
instructions, 138 operand anchors, 17 import identities, eight export
identities, 20 initializer table entries, ten negative witnesses**, 1.171 s.
There are 18 nonzero entries in those two initializer tables. Both qualification
outputs are false. Whole-text surveys below are linear absolute-operand surveys,
not computed-alias or interprocedural reachability proofs. DLL slice boundaries
are checked against linear decoding from their `.text` origins as well as
immutable region digests. Complete decoding alone is never sufficient.

| Local artifact in `/mnt/c/X3FL-orig/` | SHA-256 | Size |
| --- | --- | --- |
| `X3FL.exe` | `e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8` | 2,804,736 |
| `imgdll.dll` | `b091cfd8cb9bd2ec3d4026866869f433305665b1ea311e4bc6fd904c8a4e26d7` | 803,328 |
| `libxml2.dll` | `5f39340bd5af8aff7cdfa746e44c2369137f310297d5fb1416e93fc4f13334c2` | 946,688 |

DLL VAs below are preferred VAs at base `0x10000000`, with module names required
to disambiguate them. Their hashes identify inspected assets, not production
renderer requirements. Raw listings, compact JSON, and producing research
scripts remain ignored under `verification/results/fl-port/startup-exclusion/`.
Negatives reject all three image mismatches, four semantic mutations with
digest/boundary checks bypassed, the wrong IMGDLL ordinal, a substituted CRT
initializer, and a complete decode starting inside an operand.

## Earlier callbacks and thread surfaces

**IMGDLL correction:** the earlier startup note calls `0x401012` ordinal 24;
the actual chain is `0x476c42 -> 0x401012 -> [0x550070]`, **ordinal 25**,
`ImgDLLInitDLL`, export `imgdll!0x10001010`. Ordinal 24 is `[0x55007c]`, thunk
`0x401000`, export `imgdll!0x10001690`. It is a different four-argument routine.
The initialization call pushes integer `0x16568` at `0x476c38`; export 25
forwards one argument to `0x10002640` and returns `ret 4` at `0x1000101d`.
It does not receive a game callback pointer.

The inspected init implementation obtains per-thread state through
`imgdll!0x10010670`, sets state `+0xac`, transforms the integer through
`0x10010b90`, and sets byte `+0xb0` on a comparison. The state allocator uses
TlsGetValue at `0x10010688`, allocates `0xb8` bytes, initializes fields, publishes
with TlsSetValue at `0x100107a9`, and links the state under a DLL critical
section at `0x100107dc/ee`. This is TLS storage, not evidence of a new worker.
The transformation calls internal formatting/conversion routines; this bounded
inspection does not claim their whole CRT closure. The DLL has no PE TLS
directory. Its entry `0x1007acb0` nevertheless invokes CRT attach
`0x1007ab40`, including initializer dispatch via `0x1007ac25 -> 0x1007c450`,
before trivial user DllMain `0x10001000` returns TRUE. Therefore trivial user
DllMain and absence of TLS callbacks do not describe the complete attach path.

**libxml:** `0x476c4c` invokes `xmlCheckVersion(20623)` before installing the
game callbacks. `libxml2!0x10034373` first calls `xmlInitParser` at `0x10022ca0`.
That initializes globals, memory, encoding handlers, SAX tables, default input/
output callbacks, HTML and XPath support. It does not just compare versions.
`xmlInitThreads` is a named initializer, not evidence of a thread-creation call.
The inspected once/TLS path `0x1004a1e0` uses interlocked initialization,
TlsAlloc (`0x1004a1f9`), and records GetCurrentThreadId (`0x1004a204`); contenders
wait using Sleep(0). Neither this path nor TLS state allocation creates a thread.
There is no PE TLS directory. The thread-detach routine at `0x1004a240` tests
reason 3, gets the thread state, clears it and unlinks/frees it, returning
`ret 0xc`; that is an existing thread's cleanup path, not a worker entry.

| Registered at `0x476c7c` | Game target and ABI | Inspected behavior |
| --- | --- | --- |
| free | `0x4d91e0`, cdecl(pointer) | Null returns; otherwise tail-forwards to CRT free `0x52f4e5`. |
| malloc | `0x4d9120`, cdecl(size) | CRT allocation, optional game recovery on failure, retry, zero-fill; result EAX. |
| realloc | `0x504030`, cdecl(pointer,size) | ECX/EDX normalization, calls `0x4d8ef0` with policy arguments 0 and 4. |
| strdup | `0x504050`, cdecl(string) | CRT allocation, optional game recovery/retry, copies through terminating zero; result EAX. |
| generic error at `0x476c89` | `0x417bf0`, cdecl variadic | A single RET; no game traversal, allocation, dispatch or retained callback state. |

`libxml2!xmlMemSetup` validates all four pointers and stores them at
`0x100da8e8/ec/f0/f4/f8`; malloc is stored twice. Thus allocator pointers are
DLL-global, while generic-error/context accessors select main-thread globals
or offsets `+0x174/+0x17c` in a thread state. An EXE registering a no-op error
function on its thread does not establish every foreign thread's error handler.
Thread-state allocation here uses imported `msvcrt!malloc`, not the registered
game allocator (`0x1004a310/31d/344`). These details narrow possible callbacks;
they do not establish what threads exist at the first D3D call.

**CRT/game initializers:** the verifier pins both ranges from the prior note.
The C++ range includes CRT initializer `0x51e923`, string/stream support,
exception registration `0x4129b0`, and allocation-backed globals initialized
at `0x4129c0` and `0x412a00`. String-array construction at `0x412987` passes
constructor `0x4328a0` and destructor `0x417b60` to `0x51e32e`; this is a
synchronous initializer callback surface, not an asynchronous registration.
The latter two initializers call `0x4e9a60` and `0x4d6ad0` respectively and
register exit cleanup through `0x51e83f`. Their inspected roots do not establish
the entire exception/CRT callback closure.

CRT initializer `0x51dd16` resolves APIs from an existing kernel module using
GetModuleHandleW/GetProcAddress. It resolves CreateThreadpoolTimer at
`0x51ddf9`, encodes the pointer with the security cookie, and stores it at
`0x51de0b` to `0x5a76a4`; CreateThreadpoolWait similarly uses
`0x51de55/67 -> 0x5a76b4`. The whole-text absolute-operand census finds only
those stores for those two slots: **zero direct reads**. Symbol lookup is not
worker creation; computed aliases are outside this census.

**Exception callback:** CRT `0x51e923 -> 0x51ee4d` first installs filter
`0x51ee59`. Game initializer `0x4129b0` later replaces it with `0x480120`.
The game filter writes diagnostics/MiniDumpWriteDump (`0x48062f`), restores its
SEH link, and normally returns EAX=0 with `ret 4` at `0x480751` (continue search).
It does not certify exception continuation back to startup. Its diagnostic
callee `0x47ff80` checks VM global `0x5aa174` at `0x47ff8b/99/9b` before reading
`VM+0x1434`. A conditional path from that diagnostic through `0x47fd10` can
eventually reach generic string-handle insertion; it must not be described as
an unconditional pre-probe helper caller. The global starts zero-filled, and
the known normal VM publications occur after the probe. Indirect stream calls
and nonlocal exception behavior remain outside this bounded filter inspection.

**Additional exact early foreign entry:** successful configuration initialization
at `0x476c96 -> 0x509e00` calls `0x4d8a60` at `0x50a048`; this calls
`CoInitialize(NULL)` at `0x4d8a64`. This is an earlier callback-capable runtime
initialization boundary, not evidence of an engine writer or a reason to
reconstruct all of ole32. The EXE also statically imports `d3d9.dll` and
`d3dx9_43.dll`; the latter is not present in this local asset directory.
Imported module initialization precedes ordinary EXE entry and has not been
qualified by the normal startup CFG. The already-known EXE TLS callback list
remains empty. No process thread count is inferred from any import census.

## Active helper frames, liveness and the installation contract

The group contains **three generic helpers and one load call site**, not four
generic helpers: insert `0x50cf30`, remove `0x50d040`, destroy `0x50d120`, and
load call `0x47984c`. Their six/six/six/five-byte boundary and ABI evidence is
owned by [object lifetimes](object-lifetimes.md); it is reused.
An ordinary callback on the startup thread after a completed helper invocation
does not itself obstruct patching. Another active helper frame does, even if
its current instruction pointer is outside the overwritten entry bytes.

A concrete **conditional retained-frame witness** is insert allocation at
`0x50cf6b/89/95 -> 0x4d8fc0 -> 0x531395 -> 0x5337fd`: HeapAlloc failure can
reach `0x53381c -> 0x53e390 -> 0x53e3b7`, an indirect new-handler call while
insert's frame remains live. But `0x533819` takes that route only when new-mode
accessor `0x537378` returns nonzero. CRT initialization **sets that mode to 0**:
`0x51e928 -> 0x47cda0` returns zero; `0x51e92e -> 0x53737e` exchanges it into
`0x5a7e80`. The direct-call survey found only this caller of the setter. Therefore
this is not a demonstrated early reentrant path. The game allocator's separate
recovery call (`0x4d906a`, also `0x4d9150/0x504097` for XML callbacks) reads
`0x5aae24`; it begins zero-filled, and the sole surveyed absolute MOV write is
the later registration at `0x4778f3`. Both default-disabled facts must accompany
the conditional frame witness. Data aliases and nonlocal exits are not proved
absent by these direct surveys.

To use the first normal proxy entry, a future installer would need all of:

1. Identify the exact supported image and the intended first entry; a foreign
   import/DLL calling Direct3DCreate9 is not automatically the `0x4f13cd` seam.
2. Establish that no thread is inside any target helper/load operation and no
   other thread can enter during the full verify/prepare/write/flush/rollback
   interval. The ordinary caller chain only establishes the invoking thread's
   normal stack. Checking instruction pointers in the patch spans is weaker.
3. Prepare observer state and complete ownership/rollback storage before making
   any jump reachable; remain unbound and perform no engine/global-map heap
   reads. Preserve the proxy call's SDK argument, return/stack ABI, LastError
   and required CPU state. Do not retain an observer lock across foreign code.
4. Run before `initialize_log`, backend LoadLibraryExW and profiler/log-worker
   creation. Current `loader.cpp::load_backend` does those before lifetime
   initialization. Moving the call removes these proxy-created surfaces but
   does not establish item 2. InitOnce only serializes its own initialization.

**Decision:** item 2 remains unproved, so this entry cannot yet authorize live
patching. This inspection supplies neither an actual competing helper thread nor
a fully closed proof excluding one. The remaining concrete boundaries are the
earlier imported-module/CRT initializer dispatch, configuration's foreign entry,
and exception/alias closure above. They are bounded evidence gaps, not a claim
that every opaque OS call can invoke an arbitrary game helper. Runtime thread
counts or a single clean stack sample would not close item 2: useful runtime
evidence must identify each earlier callback/worker's actual entry and lifetime
and show both absence of retained helper frames and exclusion of future entry
for the whole transaction. Such observations support a specific exclusion
contract; repeated clean samples alone do not create that contract. No game run,
global SuspendThread scheme or speculative engine lock is proposed here.

## Factory-return baseline guidance

The first-probe seam still precedes engine creation. Factory `0x48a5a0` publishes
the engine at `0x48a5f1` before table/file initialization. A now-resolved concrete
chain is `0x48a643 -> 0x487100`, `0x4871d8 -> 0x487d40`,
`0x487dc7 -> 0x506d30`, `0x506d6c -> 0x505e00` (file open), then
`0x506d81 -> 0x505f40` (read). The reader stores its data at `+0x10`, count at
`+4/+8`, related global size at `+0xc`, and an error flag at `+0x24`; its constructor
returns with `ret 0x10`. These are file-parser object fields, **not engine
registry layout**. The wrapper returns with `ret 0xc`. This chain is a concrete
next callback/failure-path audit, not a proof that factory work creates no nodes.

At factory time, VM publication at `0x477681` and display/window construction
at `0x47765a` have already happened. The earlier exception-filter VM gate is
therefore no longer justified by pre-probe zero state. Normal factory return
still restores SEH, moves EDI into EAX (`0x48a75f`), and returns to `0x477697`;
EAX remains live for publication/testing. A baseline adapter must preserve that
result, callee-saved registers/ESP and original exception behavior, publish no
baseline on unwind/nonlocal exit, and exclude map mutations across its complete
copy. Factory-local SEH cleanup does not qualify an injected wrapper's unwind.

The next baseline task can start from this reader chain and the established
constructor/arena calls, classify callbacks and complete versus failed return,
then prove registry writer ownership at `0x477697`. It must not infer an empty
registry from the constructor's capacity 8/counter 1 or treat early publication
as completed construction. Process-wide installation exclusion and complete
baseline exclusion remain separate obligations.

## Independent review

Bounded deep review found no material finding. The reviewer reproduced 46 regions,
2,345 instructions, 138 anchors, 17 imports, eight exports, 20 initializer entries
and ten negatives in 1.320 s, with both qualification flags false. Three input
hashes/sizes matched. Additional same-length return-size, store-address and
indirect-call-register mutations were rejected with digest/boundary checks
disabled. Python compilation, relative links and result JSON validation passed.
No runtime execution or broader exclusion claim follows from these checks.
