# Portable buffer and DLL dependency review

The production geometry evidence path no longer depends on exact Wine/CrossOver
DLLs, private buffer layouts or native map-count fields. Public managed-buffer
descriptors, observed Lock/Unlock transactions and ordinary COM retention replace
the old qualifier. Eligible WRITEONLY managed allocations receive readable native
backing with the original Usage preserved through allocation-owned metadata.
Creation and metadata failures retry the unmodified request; disabled capture
keeps direct descriptor forwarding. See [portable upload](../verification/portable-managed-upload.md).

Independent review covered creation rollback, incoming/native outgoing CPU state,
canonical IUnknown identity, wrapper recreation, metadata lifetime, notified native
mutations and cleanup. Review fixed repeated authentication after metadata tamper
and a known-wrapper forwarding-loss check that previously occurred after the
tracking-disabled early return. The latter also required the mesh cache to honor
failure independently of whether write tracking was requested.

The loading cache now uses public readable SYSTEMMEM buffer contracts and observed
COM method identities instead of DLL hashes, private Wine offsets or fixed D3DX
method addresses. Named PE32 import parsing replaces the loading observer's whole
EXE fingerprint. Module retention happens only when a new shared table is first
observed. Review also identified incoming LastError as a missing cache-key input
once the old implementation-specific preservation assumption was removed.
See [cache integration](../verification/mesh-cache-hook.md).

The shader authoring tool accepts a local compiler and records its actual digest;
regeneration and `--check` preserved the identical 176-word embedded pixel program.
The historical private-buffer qualifier is retained under verification only.
Build review checked actual linked objects, including the forced fallback,
rather than assuming obsolete object files were deleted from the build directory.
Regenerating the 15 interfaces/297 methods into a temporary directory produced
byte-identical checked-in declarations and forwarders. The affected runner/parser
and integration-expectation suite passes 23 host tests.

Current component checks include 534 finite-upload, 421 geometry-lease, 184
execution-observer, 260 reader and 253 motion-capture assertions. Loading fixtures
pass 75 and 123 checks; the detached cache passes 741, including the conditional
LastError regression. Its six native/wrapped integration cases total 12,905 checks.
The portable geometry benchmark passes its 84-sample
inventory and 831,397 assertions: 700 shared-small leases take about 2.5 ms to
acquire and inspect, versus 39.1 ms at the previous indexed-handle checkpoint.
Allocation-list lookup remains visible with many distinct buffers. A separate
36-sample/2,768-check upload benchmark measures creation/mapping/classification:
the readable allocation has no consistent CPU penalty in this sample, but
observing an 8 MiB VB adds substantial atlas/classification work. It excludes
first-draw GPU upload. These are synthetic Preview CPU timings, not game FPS or
native-Windows measurements. See [upload performance](../verification/managed-upload-performance.md).

All 20 actual-DLL integration cases and the current-source verifier pass. The
production link contains 21 objects including the portable helper and excludes
the historical qualifier and fixture-only hooks. The forced native fallback also
passes with its 18 renderer/proxy objects and explicitly unavailable ownership
APIs. See [integration evidence](../../verification/probe/ownership_integration.md).
Independent final review verified source, executable, linked-object and report
provenance, the actual import tables, and all 20 Clear argument/CPU-state witnesses
(ten successful and ten native-invalid-call results).
The uninstalled production DLL SHA-256 is
`aa61e7ff2fcc4541f42d961359bdb7f2f815f6dc3c97b0cb50832ad51aa39ed9`;
the fallback DLL is
`0e1608c8635df7c7e872f9b31f165b375d76192e31d85f86edae23b44b19f652`.

No game launch or installation is part of this work. The installed iteration-5
DLL and original X3AP executable retain their previous hashes. Live motion
replay remains disabled pending write exclusion. RESZ/comparison depth and native
Windows runtime verification remain separate gaps in the
[dependency audit](../architecture/runtime-dependencies.md). Game-private hooks,
patches and disassembly remain explicitly allowed.
