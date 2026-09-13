# Production compositor CPU/SEH bridge packaging

2026-09-13. New `src/proxy/compositor_bridge.{h,S}` and
`compositor_bridge_seh.c` implement the reusable CPU/SEH transport qualified by
[review 39](../verification/review-39-bloom-return-bridge.md). They are **not
wired into CMake, scene_hook or capture**, and no game installation changed.
Native Windows remains untested. Device selection, lifetime pins, Reset
revocation and GPU behavior belong to the caller, not this transport.

## API and invocation lifetime

Publish an immutable `X3mCompositorBinding` containing original, pre, post,
cleanup and caller context using the quiescent-only bind API. All callbacks are
required. One atomic pointer publishes the binding; entry snapshots that entire
immutable binding into its own stack record. A callback never reads a
half-written global struct, and no mutable global ticket is shared between
invocations. The caller must stop new hook entry and establish real rendering
quiescence before replacing/freeing binding storage or unbinding.

Callbacks receive a readonly frame, a mutable 512-byte zeroed/16-byte-aligned
opaque buffer and context. The frame records actual incoming ESP and the
return PC read from that ESP, both complete CPU snapshots and an
`ORIGINAL_RETURNED` flag. Output is invalid before that flag. Callers must check
their payload's size/alignment and explicitly release constructed objects,
locks, context pins and COM references in cleanup. No C++ constructor or GCC
RAII destructor is implied by raw storage or by Windows SEH.

After any normal pre return, original runs exactly once. Pre refusal is a
caller-owned state in opaque storage; it does not suppress original. If pre
raises an exception, original and post do not run; finally cleans partial pre
and the original exception propagates. After normal original return, assembly
captures all output before C executes, then marks output valid and calls post.
Finally always calls cleanup, including on pre/original/post abnormal unwind.
Cleanup must not throw/raise. The API does not promise to transport C++
exceptions between GCC and MSVC exception runtimes.

An atomic active counter is incremented by entry. Normal return decrements it
after output restoration, immediately before POPAD/POPFD/RET. Abnormal unwind
decrements it after explicit cleanup in the compiler-generated finally funclet.
Binding replacement/unbind refuses an observed active invocation. **Zero is a
necessary check, not proof of quiescence**: entry/exit have small instruction
windows around counter operations. In particular, the count does not authorize
unloading module code which another thread may still be executing. Patch and
binding teardown must retain the external render-quiescence contract.

The stack record is 1,104 bytes plus bounded alignment and register-save
storage. There is no heap allocation, lock or per-draw work; two interlocked
counter updates, fixed CPU copies and opaque-buffer zeroing happen once per
compositor invocation. CPU transport still uses two FNSAVE/two FRSTOR operations,
all eight XMM registers, MXCSR, flags/GPRs and four public LastError API calls.
Incremental callback-empty cost is not yet measured.

## Compiler and runtime arrangement

GCC builds the `.S` unit with the project's SSE2/four-byte incoming stack flags.
Only the small plain-C SEH unit uses Clang's `i686-pc-windows-msvc` target with
`-fms-extensions -msse2 -mfpmath=sse -mstackrealign -mstack-alignment=4`.
The build is explicit:

```sh
python3 tools/build/build_compositor_bridge.py --output-dir verification/probe/build/compositor_bridge --gnu-pe-no-safeseh
python3 verification/probe/compositor_bridge_build.py
```

The helper preserves the raw compiler object and assembly. It rejects any
undefined dependency except the original-call assembly helper and the
compiler-generated `_except_handler3`. Microsoft documents
[`_except_handler3`](https://learn.microsoft.com/en-us/cpp/c-runtime-library/except-handler3?view=msvc-170)
as CRT exception support used by generated code. We do not manually call it or
implement its internal registration layout. Clang emits its complete SEH
registration, scope table, finally funclet and unlink; the source requests
`__try/__finally`.

The global pointer/counter operations use documented
[Microsoft interlocked intrinsics](https://learn.microsoft.com/en-us/cpp/intrinsics/interlockedcompareexchange-intrinsic-functions?view=msvc-170),
which compile into x86 atomic instructions. They introduce no DLL import.
GetLastError/SetLastError use the normal kernel32 import slots. No TEB offset,
Wine export, backend lock/layout or graphics DLL hash is involved.

A **narrow generated import library** provides only
`msvcrt.dll!_except_handler3`. Do not add the prototype's broad `-lmsvcrt-os` to
the production link: doing that earlier in the search order can rebind unrelated
CRT functions away from the existing UCRT setup. The new fixture links its
ordinary MinGW startup/stdio with the existing default CRT and uses the narrow
library solely for the compiler helper.

A cross-linked packaging-smoke DLL containing just the production bridge
(`-shared -nostdlib`, no entrypoint) was audited to import exactly:

| Module | Imports |
| --- | --- |
| KERNEL32.dll | GetLastError, SetLastError |
| msvcrt.dll | _except_handler3 |

That DLL is an untracked packaging artifact, never installed or loaded by the
builder. Its link demonstrates dependency isolation, not native Windows runtime
verification or a final full-proxy audit.

## Explicit SafeSEH decision

The current GNU-linked project DLL has no SafeSEH load-configuration table.
GNU PE ld does not consume the MS COFF `.sxdata` symbol-index metadata produced
by this Clang target; leaving it present creates a malformed section below the
image base. The helper therefore requires explicit `--gnu-pe-no-safeseh` to
produce a separate GNU-linkable object with `.sxdata` removed. Without the flag
it produces only the raw object and import library.

The raw object remains available for a future compatible linker. The helper
checks that the link copy retains every other section's original bytes (allowing
only objcopy's added zero alignment padding) and the same relocations. Runtime
scope tables and handler/cleanup code are preserved. This is **not SafeSEH
support**; it maintains the current project's lack of a SafeSEH table. It does
not request disabling DEP or ASLR. A future SafeSEH-enabled build must use a
linker that consumes the original metadata and cover every relevant handler;
this helper must not be used to strip metadata from such a build.

Proposed final packaging is thus: retain the current GNU/no-SafeSEH policy
explicitly, consume the audited link object and narrow import archive, and audit
the final proxy's imports/load configuration. The main integration owner must
make that linker-policy decision when editing CMake; these isolated files do
not silently make it for the full DLL.

## Verification artifacts

The new fixture compiles **the production bridge sources** and reuses only the
reviewed synthetic original/caller/FP harness from `bloom_return_bridge.S`.
The old prototype entry and original-call wrapper are not linked. A generated
label directly after the harness CALL checks actual caller-PC capture.
The separately compiled synthetic outer `__except` frame catches exceptions;
the production finally stays in its own production C object.

The Steam and X3/FEX executions each pass **393 checks, zero failures**, with
identical binary/source hashes and stable before/after inputs. The inventory is: the original CPU/SEH cases over four stack
alignments plus caller metadata, opaque storage, active/binding lifetime,
output-validity and declined-admission forwarding checks. Six host parser
controls pass and reject incomplete/duplicate/failed/wrong-count results.
The retained `compositor_bridge_{Steam,X3}.{json,txt}` records bind these new
production-source runs. The review-39 prototype's 240-check results are separate
evidence. This validates synthetic CPU/SEH behavior on CrossOver Preview, not
owner admission, live game execution or native Windows.

Build/provenance, generated assembly, section/relocation checks, the narrow
import definition/archive, fixture executable and packaging smoke DLL remain
under untracked `verification/probe/build/compositor_bridge/`. The runner
records source/executable hashes before and after each run and refuses changes.
Run it only through `wine_lock.py`, with a separately granted fixture lease and
a closed game:

```sh
python3 verification/probe/wine_lock.py --holder compositor-bridge --timeout 1 python3 verification/probe/compositor_bridge_run.py
```

`X3M_FIXTURE_BOTTLE=X3` selects the game bottle for a separate synthetic run.
Neither command launches the game. Native Windows is always recorded separately
as unverified until an actual Windows execution exists.
