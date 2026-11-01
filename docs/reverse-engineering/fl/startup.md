# Farnham's Legacy startup and initial registry binding

Offline evidence for the **Startup/install** and **Binding/teardown** obligations
in [the lifetime activation contract](../../architecture/farnhams-legacy-lifetime-activation.md).
This is a bounded normal-control-flow reconstruction of the clean FL executable,
not permission to activate hooks. The two qualification outputs remain false:
process-wide installation quiescence and a safe complete initial baseline have
not been proved. No production guard or hook was changed.

## Decision supported by the evidence

The first normal game-owned Direct3DCreate9 call is the adapter probe at
`0x4f13cd`, reached through `0x4772bc`. It precedes engine creation on the measured
startup CFG. At that callback the engine has not been created **by this startup
path**; an installer can remain armed without reading any engine heap. Neither
this statement nor absence of EXE thread-creation imports establishes that every
thread is outside the four observer helpers. Do not install using the existing
late `object_lifetime::initialize()` position solely on this evidence.

The best bounded candidate for initial binding is successful normal return from
factory `0x48a5a0` to `0x477697`, carrying the engine in EAX. Internal publication
at `0x48a5f1` is too early to claim completed engine initialization. A future
adapter must establish exclusion at the return boundary, inspect a fully formed
map, and take a complete baseline, or separately prove continuous observation
from an empty map. This note does not establish that the returned map is empty.
The normal startup graph supports one factory invocation per process; indirect
entry, exception recovery and engine/map aliases are still outside that proof.

## Image and reproducer

Clean image `/mnt/c/X3FL-orig/X3FL.exe`: 2,804,736 bytes, SHA-256
`e7f3aad8d728e021f589c76591a35d8bf47eb170e4f7164f6b26f1ff401dfbb8`,
PE32 base `0x400000`, entry `0x51ea9d`. No AP labels constitute FL proof.

Run `python3 verification/probe/verify_fl_startup.py`. It reuses the image reader
and whole-text reference boundary validation from the
[lifetime verifier](../../../verification/probe/verify_fl_object_lifetimes.py).
All 21 regions have complete instruction boundaries, immutable region digests
and 78 exact operand witnesses. Measured output: 2,169 decoded instructions,
8 imports, 5 dynamic symbol/name strings, 1,005 startup CFG nodes, 1,127 edges,
9 switch targets, 3 dominance checks and 7 negative witnesses. The measured
initial successful run took 0.676 s. Raw instructions, JSON and the producer stay
local under `verification/results/fl-port/startup/`; the tracked verifier emits
no copyrighted instruction listing into documentation.

The negative checks reject image mismatch, four changed semantic instructions
with both image/region digest checks bypassed, a synthetic CFG bypass, and a
complete but wrongly aligned decode. The last starts at `0x48a75e`, inside the
saved-SEH-link load `mov ecx,[ebp-0xc]` at `0x48a75c`: its operand byte decodes as
HLT, demonstrating why complete Capstone decoding alone is insufficient.

## Normal startup CFG, not address ordering

The image entry `0x51ea9d` initializes the security cookie and jumps to CRT
startup `0x51e935`; the latter calls WinMain-shaped `0x4ed5c0` at `0x51ea26`.
That routine calls `0x476b40` at `0x4ed6dd`; its SEH wrapper calls main startup
`0x476bc0` at `0x476b68`. Startup has a nine-way indirect state dispatch at
`0x477a22`, bounded by `cmp ecx,8` at `0x477a19`, with table `0x477b78`.
The verifier includes every table successor and rejects unresolved CFG edges.

| Dominator | Dominated normal call | Measured result |
| --- | --- | --- |
| `0x4772bc -> 0x4f13b0` adapter probe | `0x47765a -> 0x516890` display setup | Removing probe makes setup unreachable |
| Same adapter probe | `0x477692 -> 0x48a5a0` engine factory | Removing probe makes factory unreachable |
| Display setup `0x47765a` | Engine factory `0x477692` | Removing display setup makes factory unreachable |

All three results hold even when every direct call, including fatal cleanup,
is conservatively allowed to return. Calls are otherwise opaque: the graph does
not silently assert they cannot call the engine, register callbacks or start a
worker. Exception/nonlocal transfer is explicitly outside this normal CFG.

No path goes from factory continuation `0x477697` back to `0x477692` when normal
fatal cleanup `0x4762d0` terminates. Its tail calls `0x52ed79` at `0x476ab7`;
that wrapper supplies `return_to_caller=0` to `0x52eb3a`, whose normal path reaches
`0x52ec53` and `ExitProcess` IAT `0x5501b4` at `0x52ec8d`. CRT exit callbacks run
on that route. Treating fatal cleanup as returning produces a repeat path; the
verifier records both results rather than hiding this boundary. No claim is
made about a callback which transfers control nonlocally out of exit.

The linear direct-call census finds exactly two Direct3DCreate9 IAT calls:
`0x4f13cd` and `0x4f3c10`. Probe `0x4f13b0` has two direct callers, `0x4772bc`
and `0x4f3c38`; display `0x4f3a30` has caller `0x5168c5`; wrapper `0x516890`
has caller `0x47765a`; engine factory and constructor have one direct caller
each (`0x477692`, `0x48a5d8`). These are direct-operand counts, not a proof of
absence of indirect calls or aliases.

## First D3D callback versus later D3D/device callbacks

WinMain clears `0x24c` bytes starting `0x5aaf00` with the memset call at
`0x4ed5f2`, covering cached D3D state `0x5aaf94`. The cache and engine global
`0x5aa100` are also in the PE zero-fill tail. Probe `0x4f13b0` tests the cache;
when zero, its allocation call at `0x4f13c1` returns, it publishes the cache at
`0x4f13c8`, and calls Direct3DCreate9 at `0x4f13cd` with SDK value `0x20`.
The active ordinary EXE call chain is startup → probe → D3D import. No registry
helper is retained in that specific chain. Earlier completed helper invocations
would not alone invalidate patching; another thread or a suspended callback
with a helper frame would.

Later display setup registers window procedure `0x4ec790` at `0x4f3abc`, calls
RegisterClassA at `0x4f3b67` and CreateWindowExA at `0x4f3ba3`, before its optional
Direct3DCreate9 call at `0x4f3c10`. The device path goes through `0x4f1e10` from
`0x4f3e0a`; `0x4f2489` calls IDirect3D9 vtable `+0x40` with the seven CreateDevice
arguments. The factory pointer is taken from the D3D cache at `0x4f1e2e..3d`.
A device callback therefore follows substantially more callback-capable setup;
it is not interchangeable with the first adapter-probe callback. A later
Present callback is weaker still.

## Initialization and callbacks before the candidate install window

There are concrete earlier initialization/callback surfaces; a missing
CreateThread import cannot dismiss them:

- CRT startup invokes initializer ranges `[0x550444,0x550468)` and
  `[0x550414,0x550440)` at `0x51e985` and `0x51e9ab`. The latter contains game
  initializers as well as CRT support. Initializer `0x4129b0` installs
  unhandled-exception filter `0x480120` through IAT `0x5500c0`. Static TLS directory
  `0x59ac4c` points at an empty callback list at `0x550470`; its later neighboring
  words belong to other initializer tables and are not TLS callbacks. CRT's
  optional dynamic TLS pointers `0x686730/34` begin zero-filled. This closes
  only the static EXE TLS-list question, not imported DLL initialization.
- Startup calls IMGDLL ordinal 25 through thunk `0x401012` at `0x476c42`, before
  the first D3D call. The external implementation and any workers it retains
  have not been inspected. libxml version/memory initialization follows:
  `xmlMemGet` at `0x476c62`, `xmlMemSetup` at `0x476c7c`, then
  `xmlSetGenericErrorFunc` at `0x476c89`. Registered memory callbacks are
  `0x4d91e0`, `0x4d9120`, `0x504030`, `0x504050`; error callback is `0x417bf0`.
  Their complete callback/reentrancy closure is not established here.
- Generic maps already exist: startup's context constructor `0x475e70`, called
  at `0x476c28`, allocates maps and stores them at context `+0x4bc/+0x4c0`
  (`0x475f0d`, `0x475f30`). Thus “engine does not exist” cannot establish
  “generic hash helpers have never been used.” The prefix also runs configuration
  initialization `0x509e00`, allocation and warning/error paths.
- Whole-text leads for dynamic loading include NP_GetSignature lookup at
  `0x4d92ca` and invocation at `0x4d9310`, plus FreeTrackClient.dll and its
  FTGetData/FTGetDllVersion/FTProvider lookups in `0x500480`. The latter is
  reached by input setup `0x500590`, whose ordinary startup call follows engine
  construction at `0x4776fc`. This places that specific input route late, but
  does not prove all dynamic callbacks absent earlier. CRT uses GetProcAddress
  for FLS and other compatibility APIs as well.
- The observed SetTimer call at `0x4fc7af` pushes a null TIMERPROC at `0x4fc7a8`:
  it is a window-message timer, not an explicit worker or timer callback pointer.
  Window procedures, modal dispatch and exception filters still need their own
  reachability/exclusion argument. GetCurrentThreadId occurrences do not prove
  thread count or writer ownership.

Proxy initialization adds its own ordering boundary. In current
`src/proxy/loader.cpp`, `load_backend()` calls `initialize_log()` first, then
LoadLibraryExW for the real D3D9, then `object_lifetime::initialize()`.
`capture.cpp::initialize_log()` starts the session-log writer and optionally the
sampling profiler; their implementations call CreateThread. The profiler can
suspend threads to inspect context. These facts do not demonstrate an engine
writer, but preclude using “proxy init has not started workers or loaded the
backend” as justification at the current lifetime-init position. Moving a
minimal installer ahead of these actions would remove those particular
surfaces, not prove the earlier game/imported-DLL window safe. InitOnce serializes
proxy initialization; it does not exclude arbitrary engine execution.

## Construction completeness and a possible bind seam

Constructor `0x4825c0` takes the new engine in ECX, returns it in EAX, and has no
stack arguments (`ret` at `0x482a15` or `0x482a2d`). Registry creation at
`0x482601` uses the zeroing allocator; successful allocation gets counter 1 and
capacity 8 at `0x48260a/611`, then engine `+0xc` receives the pointer at
`0x48261a`. This is the creation-time layout, not a promise that the map remains
empty to a later binding point. The constructor also registers VM dispatcher
`0x4afbf0` at `0x482897` and its name table at `0x4828ce`. It can reach an
allocation-recovery indirect call at `0x48297e`; recovery global `0x5aae24`
starts zero-filled and the visible startup registration occurs later at
`0x4778f3`. This does not by itself prove absence of every indirect write to
that callback or of external/reentrant dispatch.

Factory `0x48a5a0` allocates `0x6a48`, calls the constructor at `0x48a5d8`, and
publishes EDI to engine global `0x5aa100` at `0x48a5f1`. It then initializes
`+0xb4..+0xc8`, allocates more tables, calls `0x487100`, `0x487420`, `0x485f60`,
`0x483f20`, `0x4870d0`, reserves arenas with VirtualAlloc, and calls `0x485d80`
twice. Only after this does it restore its SEH chain and return EDI in EAX at
`0x48a75f..76d`. Consequently a callback observing the global after publication
can see an engine whose factory has not finished. This work has not closed the
transitive callback closure of those initialization callees or proved their
registry cannot receive objects.

The outer call at `0x477692` is one complete five-byte direct call. Its normal
continuation `0x477697` stores EAX to the engine global again; `0x47769c` tests
EAX before proceeding. A qualified call-site adapter could forward the exact
zero-argument factory, then baseline the returned engine/map before resuming
that continuation. EAX is live; ECX/EDX have no factory input contract, and
callee-saved registers and caller stack must remain intact. The continuation
redefines flags at TEST, but this is not an excuse to alter machine state
across the original call. No new CPU/XMM/LastError or unwind wrapper is qualified
by this note. In particular, factory SEH restoration on normal return does not
make an injected C++ wrapper unwind-safe; exception/nonlocal exits must leave
observation unbound/quarantined, never publish a partial baseline. No new hook
signature or production patch is proposed as already safe.

## Incarnation, destruction and remaining proof boundaries

The direct absolute MOV-store census finds only `0x48a5f1` and `0x477697` writing
engine global `0x5aa100`. It cannot exclude stores through computed aliases.
The one normal factory call supports a one-shot design once the remaining
entry/alias proofs are closed. It does not authorize rebinding from a fresh
nonzero global after destruction or address reuse.

Existing [lifetime evidence](object-lifetimes.md) establishes
registry destroy at `0x48abad`, engine destructor at `0x48ad6c`, then engine free
at `0x48ad77`. The global remains stale. Destruction must revoke the binding
before forwarding and future generic callbacks must check dead/unbound state
before reading any supplied engine/map address. A map observer does not cover
later engine free or protect external consumers retaining engine pointers.

The actionable unresolved boundaries are:

1. **Installation exclusion:** audit earlier imported-DLL/CRT/configuration and
   registered callback paths enough to exclude another active generic-helper
   frame or writer at the first probe, or provide a separate qualified exclusion
   mechanism. This note proves ordinary caller ordering only.
2. **Initial baseline exclusion:** complete factory-initialization callback
   reachability and writer ownership at normal return. Binding there requires a
   complete baseline; do not infer empty registry from constructor allocation.
3. **Identity stability:** establish all engine `+0xc` writes/aliases and any
   indirect construction, replacement or destruction entries. Direct-reference
   absence is not that proof. Keep one-shot binding a conditional design choice.
4. **Foreign control flow and retirement:** qualify callback SEH/nonlocal exits
   and any delayed callback after map/engine destruction; preserve the separate
   writer/free and consumer proofs in the activation contract.

These are concrete missing evidence, not a request to weaken the architecture
or invent a game lock. AP-only executable guards remain the production boundary.

## Independent review

Bounded deep review found no blocker. Independent verifier execution reproduced
21 regions, 2,169 instructions, 78 anchors, 1,005 CFG nodes, 1,127 edges, nine
switch targets, three dominance checks and seven negatives in 0.715 s. Python
compilation and rejection of AP (exit 1) also passed. Both qualification flags
remain false. Targeted inspection of 554 fatal-cleanup instructions supports
the terminating-cleanup interpretation; opaque calls and exception edges remain
outside the CFG and cannot establish process-wide exclusion.

The follow-up [callback/exclusion study](startup-exclusion.md)
corrects the IMGDLL import to ordinal 25 and narrows the earlier callback and
allocator paths. No actual competing helper was demonstrated, and process-wide
installation exclusion remains unproved at the first D3D callback.
