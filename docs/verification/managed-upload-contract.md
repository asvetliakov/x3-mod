# Managed upload qualification

The focused Preview fixture passes **461 checks**, freshly built and run on
2026-09-11 local time (2026-09-10 22:41 UTC). It creates a hidden standalone
native D3D9 device and exercises a managed WRITEONLY vertex buffer and both
INDEX16/INDEX32 index buffers. It does not launch the game, install a DLL, draw,
scan payloads for finite values, or establish rendered-image correctness.

Evidence: [structured summary](../../verification/results/managed-upload-contract-summary.json),
[check log](../../verification/results/managed-upload-contract.txt),
[native diagnostics](../../verification/results/managed-upload-contract-wine.log).
Run with `python3 verification/probe/run_managed_upload_contract.py` only while
no X3 game or other coordinated GPU fixture is running.

## Qualified boundary

[`managed_upload_contract.h`](../../src/ownership/managed_upload_contract.h)
exposes typed VB/IB `inspect`, `validate_window`, and `validate_closed` operations.
They acquire no resource reference, issue no new Lock/Unlock, and read no mapped
payload. Inspection calls only the verified native GetDesc method. The caller
retains the native resource and serializes its lifetime and mapping operations.

The gate hashes the exact installed PE32 D3D9 and WineD3D modules, validates PE
identity, then pins those verified modules for process lifetime. A rejected first
candidate does not poison later qualification. It checks live native slots
4/5/6/11/12/13 and six WineD3D import/export bindings on subsequent validation,
using the [pinned mapping proof](../reverse-engineering/managed-buffer-write-mapping.md).
Replacement of a checked slot/import rejects qualification. Runtime code patches,
corrupted objects and concurrent foreign mutation remain outside the proof.

An actual descriptor must be MANAGED, exact WRITEONLY usage 8, expected buffer
type/format, and a positive size at most 256 MiB. Checked internal fields identify
the native WineD3D buffer, its managed pinned CPU heap, size, resource operations
and absence of a GPU mapping. Full-span memory accessibility is an additional
structural guard, not the coherence proof. The memory-region walk is capped at
64 regions; these conservative bounds may reject otherwise valid allocations.

A borrowed token retains native, backend-resource and heap identities plus an
immutable backend-policy generation. Validation must match this same token; it
does not refresh changed identity. Only ordinary write flags 0/NOSYSLOCK and one
native pending mapping qualify. `(0,0)` means the whole allocation; all other
accepted windows have a nonzero size and fit within the allocation. The returned
application pointer must equal that exact heap plus offset. Post-Unlock validation
requires the same identity and native map count zero; the pinned native queue
finishes unmap before returning.

The ownership observer separately enforces unchanged wrapper forwarding,
same-thread Lock/Unlock, one observed pending operation, allocation/reset
generation, revision, and normal successful closure. It must issue MFENCE plus a
compiler barrier before payload loads and stage classifications until successful
matching Unlock. A qualifier token alone is not finite evidence.

## Verification coverage

The fixture checks full and partial original application locks with flags 0 and
NOSYSLOCK, pointer/normalized-window agreement before and after application
writes, live map counts 0/1/2, normal closure, and final resource release/reset.
It rejects unsupported descriptors, flags, range overflow, nonzero-offset zero
size, wrong/null pointers, null outputs, unreadable native pointers, altered
tokens, and mismatched live allocations. Every checked native method and WineD3D
import is replaced individually and must reject, then restored. Original-lock
failure controls cover both untouched and cleared output pointers and LastError;
the qualifier never turns these failures into valid windows. Reference-count
checks verify the helper adds no resource ownership.

LastError and x87/SSE computational-environment checks cover ordinary state and
nondefault rounding/control/sticky flags with a live x87 stack value. The stronger
control found that FXSAVE/FXRSTOR did not round-trip the live x87 tag/value on this
execution path, despite preserving the default environment. The final guard uses
opaque FNSAVE/immediate FRSTOR at entry and FRSTOR plus LDMXCSR at exit. The live
80-bit 1.0 sentinel, x87 control/status/tag and MXCSR then pass. This is ABI state
transport; production arithmetic remains SSE2. The cause inside the execution
backend has not been established. Volatile XMM registers retain their ordinary
calling-ABI treatment; the helper does not promise to preserve their payloads.

The runner fresh-builds with warnings as errors and the project's x86/SSE2/stack
flags. Source and installed native-module SHA-256 hashes must match before build,
after build and after execution; executable identity must remain stable. The
report requires exactly one terminal `RESULT PASS checks=461`, all 461 matching
check lines, the exact three cases, no failure marker and exit zero. Duplicate or
nonterminal result lines do not qualify. The summary records executable and
report hashes for this run.

Independent platform review requested the strict result parser, before/after
native-module provenance, private-data endpoint checks, and stronger FP control.
All four changes are included in this passing run. Final review accepted the
qualifier with no remaining blocker and independently recomputed every source,
native-module, executable and report hash with zero mismatches. The ownership
observer's integration and publication rules remain a separate review boundary.
