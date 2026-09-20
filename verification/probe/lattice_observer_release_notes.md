# Standalone observer Release probe

This probe tests real backend callbacks caused by descriptor-only queries and
resource-alias Release. It never draws, accesses payload bytes or launches X3.
The production hypothesis is `resource Release -> hooked device Release ->
restore_bindings`; this fixture tests only the first arrow. Its device hooks count
and forward, without binding restoration. A positive result is not proof that
Run193 was perturbed or authorization for a production lifetime change.

The hidden HAL D3D9 device follows the retained lattice GPU fixture's documented
creation/resource pattern. Only retained `vertex.bin` and `candidate.bin` are
needed; the selector, geometry and textures are not replayed. Three 64x64 MRTs,
one depth surface, a texture-backed RT0 container, shaders/declaration, small
managed VB/IB and one managed texture bound at s0/s3 cover the observer's COM
families. No sampling content or actual GPU writes are claimed. Target format is
A8R8G8B8, a deliberate callback test rather than Run193 FP16 equivalence.

Four cases cover retained creation references versus creation references dropped
while bound, before and after successful Reset. Getter references are never
released through stale pointers. Each explicit getter/metadata query/Release has
raw saved-slot38 MRT samples before and after it. Samples release all their own
aliases and use the distinct `mrt_sample` callback label, so their own effects
cannot become evidence attributed to the tested operation. Explicit alias counts,
Reset and the final device Release returning zero provide lifetime checks.
Container E_NOINTERFACE and missing existing private IDs are expected.

Then the unchanged compiled `Capture::effective` is called directly after arm,
with its selector deliberately bypassed. The submit route is true; native target
call count verifies execution reached the late MRT query family. All remaining
helper calls follow its normal sequential body. This is no game selector,
production Release hook or native DIP integration qualification. Actual effective
shader/stream/target/texture aliases are managed by the production helper; no
private-data IDs are assigned. Private metadata content remains irrelevant.

Only documented base-device vtable data slots 1/2 are replaced in the standalone
process; saved pointers are forwarded with the existing `CpuCallBoundary`.
There are no instruction patches or trampolines. Counters/events use fixed storage
and no allocation, locks or formatting in hooks. Both slots and page protection
are restored on successful exit and exception paths before the device's final
Release; an unrecoverable rollback error aborts the standalone process. Every
measured operation checks the qualified full x87/MXCSR/LastError envelope. The
reference-control pair calls real device AddRef once and releases exactly that
new reference; it is hook reachability evidence, not a fabricated resource callback.

Parent-owned build and execution, after source/evidence review:

```sh
python3 verification/probe/lattice_observer_release_build.py --output /tmp/x3-lattice-observer-release-build-v1 --inputs /tmp/x3-lattice-gpu-inputs-v5
X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py --timings-json /tmp/x3-lattice-observer-release-lock-v1.json python3 verification/probe/lattice_observer_release_run.py --build /tmp/x3-lattice-observer-release-build-v1 --output /tmp/x3-lattice-observer-release-run-v1
python3 verification/probe/lattice_observer_release_report.py /tmp/x3-lattice-observer-release-run-v1/observations.jsonl --output /tmp/x3-lattice-observer-release-run-v1/checked.json
```

Build/run/report outputs refuse overwrite. The build freezes the two local shader
inputs and records source/input/executable hashes and compiler flags; the runner
verifies them before/after execution, uses builtin D3D9, and records X3/WineArch
and both emulation environment lines. No production DLL is built or installed.
Native Windows can execute the standalone EXE with its input directory argument;
the CrossOver-specific runner is optional and native runtime remains unverified.

Affected host command:
`PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_lattice_observer_release`.
The synthetic checker records test validation only and are never callback proof.
Callback absence is a valid negative result; the report separates explicit
resource Release callbacks, actual-helper callbacks and sampling callbacks.

Next evidence, only if parent ratifies: actual production restoration integration
with observation off/on, raw MRTs at native DIP, unchanged DIP arguments/results,
auxiliary pixel writes, cleanup/Reset/final-release parity. Nothing here identifies
live geometry/texture bytes, Run177 fragment owners, or a TAA/crawl correction.
