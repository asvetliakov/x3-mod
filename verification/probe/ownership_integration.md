# Ownership integration in the app-local proxy

The isolated experimental 0.4 DLL includes the canonical ownership implementation before the existing capture hooks. Its default path still captures native normal-D3D9 objects. `X3M_OWNERSHIP=1` selects wrapper adoption; failure logs the HRESULT and continues using the caller-owned native factory. `Direct3DCreate9Ex` remains native and uninstrumented. The switches are read once during first backend initialization, outside `DllMain`.

`X3M_DEPTH_COPY=1` additionally requests original-preserving depth-copy storage, but is effective only with ownership enabled. The launcher has corresponding `--ownership` and `--depth-copy` options; the latter requires the former. Both are off by default. `--scene-depth-capture` additionally observes requested capture frames and requires both switches. `--object-trace` enables exact-executable submission tracing; when active with ownership, it also enables buffer write revisions. All these diagnostics are off by default. This verification does not authorize or perform game installation or gameplay.

## Reproduction

```sh
python3 verification/probe/run_ownership_integration.py
python3 verification/probe/verify_ownership_integration.py
python3 verification/probe/run_ownership_integration_fallback.py
```

The runner performs a clean CMake build and recompiles every consumed fixture before launching any case. Its contract fixture is named `ownership_integration_baseline.exe`, separate from the standalone ownership runner's executable, so sequential suites preserve each other's binary provenance. Source hashes are recorded before compilation, after compilation and after execution. It rejects changes across those checkpoints and checks EXE/DLL hashes before and after execution, plus hashes of each isolated copy and report. The fallback run requires that fresh-build manifest still match current inputs and checks every expected linked proxy/renderer object before and after execution. It copies the DLL and executables into disposable verification directories and uses `/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine --bottle Steam --no-update`, with app-local D3D9 override. It does not mutate bottle settings, launch X3, install a DLL, or replace `build/d3d9.dll`.

## Historical installed 0.4 checkpoint

Experimental DLL SHA256: `81e3b121659c0fa1811641a5dbe019f668341477a787c6729fb5a848e056c516`.
Ownership source SHA256: `fa3ebaed46e0a6beb0b82b3aaadbb71dc1a0ba852d5efeb965e4d58a78ec45b4`.

The 15-case matrix passed:

| Mode | Fixtures | Result |
| --- | --- | --- |
| Native/default | Smoke, capture state, lifetime, 370 baseline contracts, auto-depth API smoke | 5 passed |
| Ownership; depth-copy off | Same five fixtures | 5 passed |
| Depth-copy requested without ownership | Smoke; logs effective depth switch off | Passed |
| Ownership plus depth-copy | Auto-depth API smoke through create, clear, Present and Reset | Passed |
| Ownership, depth-copy and scene capture | Requested-frame observer activates; unrecognized synthetic sequence never copies depth | Passed |
| Scene capture without prerequisites | Observer remains inactive | Passed |
| Object trace on wrong executable | Exact fingerprint rejects installation; ordinary rendering remains operational | Passed |

Disabled buffer revision diagnostics explicitly report unknown in both native and wrapper capture paths. The capture verifier still passes with ownership enabled, and snapshots include all 13 stencil renderstates. Logical texture/surface identities, getter behavior, stateblock restoration, Reset and application-visible fixture results match native mode. Synthetic lifecycle runs release the application factory first, release the device while a texture remains, recover device/factory identities from that child, and release the child last. All 16 device contexts were destroyed in each mode; ownership mode reused 9 device addresses without leaving stale capture hooks. Every case's hooked device IDs has a matching final-destruction record.

The depth-copy smoke observed requested/available/source_bound true and original source format `D24X8` (77), with a new generation after resizing Reset. `copy_valid` remained false and `copy_epoch` remained zero: the unrecognized synthetic sequence does not trigger a scene copy. This establishes option wiring, native application-depth preservation and allocation diagnostics only. Explicit copy content, state restoration and loss recovery have separate fixtures. The earlier INTZ substitution route and its option/API have been removed; old `sample_depth` logs are historical results and are not included in this checkpoint manifest.

Reports and exact per-file provenance are in `verification/results/ownership-integration-build.json`, `ownership-integration-verification.json`, and the corresponding case text/capture/Wine logs. Source hashes were unchanged across the matrix. The installed 0.3 artifact was not changed by these runs. Raw startup and reset diagnostics are emitted as `ownership_copy_depth` with requested/available/source_bound/copy_valid, generation/source_epoch/copy_epoch and the complete source description.

## Adoption failure and ABI audit

A separate verification DLL links the same compiled proxy/renderer objects against a test-only adoption stub that returns `E_OUTOFMEMORY` without consuming the native factory reference. It is kept outside the production build. The smoke passes, logs `mode=native_fallback result=8007000e`, and destroys both device contexts. Its separate DLL hash and production-object list are recorded in `ownership-integration-fallback.json`; it is never installed.

Capture replaces only the object's primary COM vptr. Ownership's internal `Node` has a separate C++ virtual destructor vptr. In the actual i686 DLL, both Factory and Device public `Release` add four bytes to obtain the `Node` subobject; the internal deletion dispatches via that secondary vptr's deleting-destructor entry. Disassembly is saved with the DLL hash in `ownership-integration-abi.txt`. This confirms that the capture COM table does not replace the destructor table in this build.

Final child cleanup now releases its parent through `parent->application->Release()` after leaving the registry lock. That dispatch is essential: it reaches the capture Release hook for child-induced final device/factory destruction. Directly calling the internal ownership release helper would bypass hook cleanup. The compiled parent dispatch uses COM slot 2; the repeated lifecycle test exercises it with factory-first and device-first application release ordering.

The imports audit shows ADVAPI32, KERNEL32, USER32 and UCRT dependencies; no external libstdc++, libgcc or libwinpthread DLL is needed. These are synthetic API/lifetime checks, not gameplay, visual-quality, frame-pacing or performance measurements.

## Installed iteration-5 checkpoint: motion inputs, lifetime and cache integration

Verified 2026-09-11. CMake now also compiles the actual draw-input reader, registry
lifetime observer and bounded adjacency cache. Captured draws carry scoped motion
input records; lifetime proof requires matching before/after epochs, mutation
revision and serials. `--object-lifetime` requires `--object-trace --ownership`;
`--mesh-cache` requires `--telemetry`. Both new switches are off by default. The
mesh cache has a separate actual-hook fixture; this combined matrix disables it.
The object-requested case enables both engine observers and verifies they refuse
the synthetic executable fingerprint while ordinary rendering remains operational.
The material, motion-history, rigid-motion and temporal modules remain detached
from live rendering. No visual feature is enabled by this integration.

A fresh build passes all 15 integration cases, capture/lifetime verification and
the forced adoption-failure fallback. The fallback links all **15** compiled
proxy/renderer objects unchanged; only the ownership implementation is replaced
by the test stub. Source and binary hashes match before and after both runs.
That checkpoint's result files are retained in Git history and the iteration
notes; current result files describe the finite-position checkpoint below.

Verified production DLL SHA256:
`ed19a7abf54ae2b9debf912f3d343a0c9217038a2162cb6a9eb174fc8050bbd5`.
This artifact was subsequently installed by the
[iteration-5 checkpoint](../../docs/verification/iteration-05.md). The previous
installed diagnostic DLL `81e3b121659c0fa1811641a5dbe019f668341477a787c6729fb5a848e056c516`
is preserved as a local rollback.
This matrix verifies combined API/lifetime behavior; the numerical motion and
material contracts have their own fixtures, and gameplay integration remains
pending.

## Verified finite-position wiring checkpoint (before motion adapter)

The verified source matrix preserves all 15 cases above and adds three actual-DLL
cases: ownership plus `X3M_FINITE_POSITIONS=1` with the smoke and capture fixtures,
and finite positions requested without ownership with the capture fixture. The
18-case runner explicitly sets `X3M_FINITE_POSITIONS=0` for every other case so an
inherited shell setting cannot enable the producer accidentally. Object tracing
is disabled in both finite-enabled cases: their known buffer revisions must come
from the finite option's independent tracking prerequisite.

The verifier requires one `finite_upload_mode` record for each requested process,
correct effective enablement, scoped `motion_geometry` records for every captured
draw, and batched `finite_upload_metric` records. The existing fixtures use
usage-zero managed buffers, transformed-position declarations and/or UP draws;
they must retain unknown finite/index evidence and an unqualified replay source.
The finite capture case must explicitly report the native-contract refusal for
its unsupported buffer usage. Its memory, publication and classification counters
remain zero. This is option wiring and refusal verification; successful managed
upload classification has separate original-data fixtures. Cumulative metric and
reason snapshots are not summed as independent events.

The forced adoption-failure case also requests finite positions. The loader's
option remains requested/enabled, but native query stubs must return unknown
metadata, with no finite payload or fabricated proof. It links the **16** current
proxy/renderer objects unchanged against the ownership stub. Its disposable DLL
and executable are hashed before and after execution in addition to the source,
production binary and object provenance checks. Captured application API results
are compared byte-for-byte with the corresponding finite-disabled cases.

Both runners refuse execution if X3AP is running or process inventory fails; the
main runner repeats that check before each child launch. All 18 cases, the current-source verifier and forced adoption-failure fallback
passed after the executable namespace correction. The separate standalone
ownership executable hashes remain unchanged. The installed iteration-5 DLL is
unchanged.

Verified production DLL SHA256:
`6e21f29a57e97018753212759392ef0b79bd9fc120aa5121f30dd62f70ed3083`.
`ownership-integration-symbols.json` records an `nm -C --defined-only` audit of
the fresh ownership and rigid-motion objects: fixture scheduling callbacks and
`original_synthetic_sm3_contract` are absent; `qualify_rigid_replay_source` is
present. Source, executable, DLL and object hashes match across the final runs.

## Verified motion-diagnostic wiring checkpoint

The final matrix retains all 18 cases and adds `motion_requested` on the auto-depth
fixture, with ownership, depth/scene capture, finite positions, object trace and
object lifetime requested; it also adds `motion_without_prereqs` on the capture
fixture with only `X3M_MOTION_CAPTURE=1`. Every other case explicitly sets that
variable to zero. The verifier requires the effective `motion_capture_mode` gate
to be one only in the fully configured case. Every case must also report
`enabled=0 reason=write_exclusion_unavailable` and `temporal_consumer=0`. Live
GPU replay and application execution-state tracking remain disabled: the live
producer cannot yet exclude worker-thread buffer writes during the replay window.
The exact engine observers must reject the synthetic executable fingerprint.
No `motion_replay` event may appear: these programs cannot establish the game's
scene boundary or lifetime scope, and this matrix does not fabricate either.
Application API output must match the corresponding diagnostic-disabled case.
Actual GPU motion-adapter execution belongs to its separate original-data fixture.

The fallback requests motion, finite positions and scene capture while ownership
adoption deliberately fails. Its disposable DLL links 18 unchanged production
objects: eleven proxy objects, six renderer objects, and the standalone observed
execution-state core. The ownership replacement explicitly refuses execution-view
and geometry-frame/lease queries with `E_INVALIDARG`; output flags, native
pointers and opaque handles remain unknown/null/zero. No temporal eligibility or
query-idle proof is manufactured by the fallback. The core object is retained to
check compilation/link integration, not to claim a native fallback execution view.

The final fresh build passed all **20 cases**, the current-source verifier, and
the **18-object** forced fallback. Source, DLL, executable and linked-object
hashes remained stable. The separate standalone ownership executables retain
their current verified hashes. The installed DLL is unchanged.

Verified production DLL SHA256:
`e45aaac050aa3c0c816776a72a5877f951ee5d87efc6d58493740175aee8d282`.
The production symbol audit again finds no fixture scheduling callbacks or
synthetic replay-contract issuer, and confirms the real qualifier is present.
The embedded pixel-program refresh passes 102 numeric samples, 118 checks and
30 complete state comparisons on its own final source hashes.

### Actual Clear-call CPU boundary control

The capture fixture now installs an original-call witness before the app-local
proxy adopts each native device. It loads the app-local DLL first, then patches
the shared backend factory's CreateDevice slot process-locally. That forwarder
calls the actual backend and gives each resulting native device a fixture-owned
vtable with a Clear witness before returning to the proxy. A scoped owner restores
the factory slot/page protection before factory teardown, including error exits.
The native device tables remain alive through both normal and pure device runs.

Within captured frame 1, a valid target-rectangle Clear and invalid depth Clear
without a depth surface pass through the actual public proxy hook. The witness
compares exact incoming arguments and records a hostile caller state: live x87
stack/tag/control image, MXCSR rounding/sticky flags and LastError. It forwards
the native operation, retains its exact HRESULT, then deliberately supplies a
different outgoing live stack/control/MXCSR/error state. The public caller must
observe that outgoing state unchanged after capture logging and cleanup. The
fixture uses its own FNSAVE/FRSTOR snapshot implementation, not the production
guard. It restores the original host state before printing any result.

All capture modes must produce four passing `CPU_CLEAR` records: success and
failure on each of the normal and pure devices. No frame, draw or Present is
added; only two Clear events appear inside each device's captured frame 1.
Compilation uses SSE2 with four-byte incoming-stack realignment and static runtime
linking. Across the five capture-fixture modes, all **20 CPU_CLEAR records** pass:
ten real successes (`S_OK`) and ten real invalid depth failures (`8876086c`), with
exact incoming/outgoing CPU state and arguments. This control tests the actual
hook boundary; the separate motion-adapter fixture covers CPU preservation while
the diagnostic GPU adapter executes.

The first extended matrix stopped before entering the fixture because adding its
RAII witness pulled in an unavailable dynamic `libwinpthread-1.dll`. The capture
fixture now links with `-static`; an import audit confirms that dependency is
absent. A focused actual-DLL control then passed, followed by the complete fresh
20-case run above. The runner records the child exit and binary hashes before
requiring a capture log, so future pre-entry failures retain useful evidence.


### Optimized ownership refresh

The 20-case matrix, 18-object fallback and all 20 Clear CPU-state witnesses were
rerun against the final ownership qualification deduplication and constant-time
lease bookkeeping implementation. All pass with current source/binary provenance;
the live write-exclusion gate remains unchanged. The symbol report additionally
retains matching pinned native D3D9/WineD3D file hashes around the final cases and
fallback, plus the capture fixture's import list confirming no external GCC,
libstdc++ or libwinpthread runtime DLL. Standalone ownership executable hashes
remain intact. These API checks establish compatibility; lease performance has
its own bounded benchmark and does not establish a live game frame-time gain.

## Portable managed-buffer and cache refresh

The fresh combined DLL now passes all **20 cases**, the updated current-source
verifier and the **18-object forced adoption-failure fallback**. The linked
production archive contains **21 objects**, including
`portable_managed_upload.cpp.obj`; the historical
`managed_upload_contract.cpp.obj` is absent from the actual link response and
archive. The new managed-buffer producer uses public D3D9 contracts, and the
loading/cache adapter uses its separately verified public buffer acquisition
path. This combined matrix keeps mesh-cache dispatch disabled; the actual cache
hook and behavior have their own verification.

Verified DLL SHA-256:
`aa61e7ff2fcc4541f42d961359bdb7f2f815f6dc3c97b0cb50832ad51aa39ed9`.
Hashes identify the tested source, build products and reports. There is no native
DLL hash allowlist in this integration runner or in the portable buffer feature.
The symbol audit records the available runtime DLL files after the run solely
as provenance. It does not claim their hashes were checked before this run or
that a particular hash is needed for feature support.

The portable acceptance changes the finite-enabled capture expectations:
managed buffers created with usage zero are readable and now receive evidence.
For each of the normal and pure devices, the original fixture creates a 76-byte
VB and writes only its final 60 bytes, writes a complete 6-byte IB containing
`{0,1,2}`, then replaces the VB with another allocation and another 60-byte write.
The retained cumulative samples verify **two uploads/publications/scans and
66 classified bytes** through frame 3, then **three and 126 bytes** from frame 4
through Reset. These samples are not added together. Sixteen metric rows cover
frames 0–5 and Reset-before/after on both devices. The owner generation advances
from 1 to 2 and Reset retires atlas payload. Positive sidecar, metadata and
position-atlas reservations are observed without assuming a fixed metadata
structure size.

Eight successful indexed draws have exact known IB extrema 0–2, matching revision
and generation, and a verified range. The fixture's missing-index failed draw
and UP draw remain without that proof. This is specific to the missing-index
control: a failed draw in general can retain truthful storage metadata. The
POSITIONT declaration still prevents XYZ evidence queries; all draws retain
unknown finite-position evidence, unqualified replay source and missing lifetime
proof. No renderer candidate or live replay is manufactured from successful
buffer uploads. Six original host metadata tests exercise these expectations;
independent review checked them against the authored fixture and producer before
the combined run.

All **20 actual Clear CPU-state controls** still pass, and diagnostic-on/off
fixture API reports agree. The forced fallback's new native-buffer invalidation
entrypoint explicitly returns `E_INVALIDARG`, as do its unavailable execution and
geometry APIs. Both separate standalone ownership executables retain their prior
hashes. DLL, fixture, source and fallback object hashes remain unchanged across
the runs. The final import audit finds no external libgcc, libstdc++ or
libwinpthread DLL dependency. Public Factory/Device Release still adjusts to the
separate Node subobject, with internal destruction and parent COM Release using
the verified separate dispatch paths; the refreshed disassembly records this
DLL's hash.

Evidence is retained in the existing build, verification, fallback, symbol and
ABI reports under `verification/results/ownership-integration-*`. Reproduction
uses the three commands above; the symbol audit records its `nm`/`objdump`
commands and actual CMake object inventory. These are combined API, metadata,
lifetime and compilation checks on Preview. They are not native-Windows
execution, gameplay, cache performance or enabled HDR/TAA evidence. The installed
game DLL is unchanged, and the live replay/write-exclusion gate remains closed.

## Application admission integration (verified)

The fresh matrix passes **26 cases**. The original 20 cases explicitly set
`X3M_ADMISSION=0`; five `admission_on` cases repeat ownership smoke, capture,
lifetime, contracts and auto-depth controls with admission enabled. One
`admission_native` smoke case exercises the native escape route. The forced
adoption-failure DLL also requests admission. The existing public API reports
must remain identical within each matched pair, excluding only the smoke
fixture's distinct disposable DLL-directory line.

Startup records must report the requested admission mode with `live_replay=0`
and `coverage_complete=0`. Final records distinguish `phase=factory` from
`phase=device`, with exact fixture-specific inventories. A child-driven final
Release can expose a nested factory snapshot with one active device root; it
must be immediately followed by a finished device snapshot with zero roots.
The last serial teardown record must have zero active/waiting roots, and no
record may show replay promotion. Disabled mode reports no admission activity;
enabled mode must report positive, nondecreasing cumulative admitted roots.
These witnesses establish observed entry bookkeeping, not complete callback
coverage or authorization to enable live replay.

CMake now supplies **23 production objects**. The forced fallback retains
**20 current production objects**, including admission core and ABI adapter,
while substituting only the ownership implementation. Standalone ownership and
loading fixtures compile fresh target-local admission objects through
`build_admission_dependencies.sh`; only the ABI adapter is compiled with
`-fno-exceptions`. The core and callers retain their usual exception policy,
SSE2 arithmetic and four-byte incoming stack contract. All source manifests
include the helper and admission source/header dependencies.

Ten original admission-log parser tests cover enabled/disabled modes, nested
teardown completion, missing/duplicate fields and records, active/waiting roots,
promotion, bad counters and unsupported coverage claims. They pass alongside
seven existing portable-upload expectation tests. Veto bits cannot clear and the
first veto reason cannot change; the native-route smoke and forced fallback
must specifically retain the unobserved-route veto. The final native matrix and current-source verifier pass all 26 cases. The
forced fallback passes with 20 current production objects, and all **24 actual
Clear CPU-state witnesses** pass (12 successful calls and 12 failed calls).
Exact empty-owner metadata controls require 8192 bytes for an enabled owner’s
2048-pointer x86 index; native fallback requires zero, and both paths still
reject unexpected payload, sidecar or publication activity.

Final production DLL SHA-256:
`5a5f8a78d7c9a802d844368c7a68572c009edd1272b03e8dab306e6bcda39007`.
Disposable forced-fallback DLL SHA-256:
`1f6c966e920a24104b67aed7b366c669b53af9f9576f3d00addb3795bd816693`.
The [artifact audit](../results/admission-integration-artifact-audit.json)
records all 23 linked production objects, the 20-object fallback subset, the
83-file source map and current manifest hashes. No game was launched and no DLL
was installed.
