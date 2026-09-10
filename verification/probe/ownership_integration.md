# Ownership integration in the app-local proxy

The isolated experimental 0.4 DLL includes the canonical ownership implementation before the existing capture hooks. Its default path still captures native normal-D3D9 objects. `X3M_OWNERSHIP=1` selects wrapper adoption; failure logs the HRESULT and continues using the caller-owned native factory. `Direct3DCreate9Ex` remains native and uninstrumented. The switches are read once during first backend initialization, outside `DllMain`.

`X3M_DEPTH_COPY=1` additionally requests original-preserving depth-copy storage, but is effective only with ownership enabled. The launcher has corresponding `--ownership` and `--depth-copy` options; the latter requires the former. Both are off by default. `--scene-depth-capture` additionally observes requested capture frames and requires both switches. `--object-trace` enables exact-executable submission tracing; when active with ownership, it also enables buffer write revisions. All these diagnostics are off by default. This verification does not authorize or perform game installation or gameplay.

## Reproduction

```sh
python3 verification/probe/run_ownership_integration.py
python3 verification/probe/verify_ownership_integration.py
python3 verification/probe/run_ownership_integration_fallback.py
```

The runner performs a clean CMake build and recompiles every consumed fixture before launching any case. Source hashes are recorded before compilation, after compilation and after execution. It rejects changes across those checkpoints and checks EXE/DLL hashes before and after execution, plus hashes of each isolated copy and report. The fallback run requires that fresh-build manifest still match current inputs and checks the twelve linked proxy/renderer objects before and after execution. It copies the DLL and executables into disposable verification directories and uses `/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine --bottle Steam --no-update`, with app-local D3D9 override. It does not mutate bottle settings, launch X3, install a DLL, or replace `build/d3d9.dll`.

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

## Current source checkpoint: detached renderer modules

Verified 2026-09-11. CMake now compiles the material-radiance transformer, motion
history, exact rigid-position lookup and rigid-motion pass alongside the existing
temporal pass. No live renderer callsites or visual features are enabled by this
link change. The mesh-adjacency cache remains detached and is not compiled into
this DLL.

A fresh build passes all 15 integration cases, capture/lifetime verification and
the forced adoption-failure fallback. The fallback links all **12** compiled
proxy/renderer objects unchanged; only the ownership implementation is replaced
by the test stub. Source and binary hashes match before and after both runs.
The current result files describe this build; historical installed evidence is
retained in Git history and the iteration notes.

Uninstalled production DLL SHA256:
`53d91a676ddb855ed936079d128ac47f06666c88b1ed6870b5453f7ea21cd9c4`.
The installed diagnostic DLL remains
`81e3b121659c0fa1811641a5dbe019f668341477a787c6729fb5a848e056c516`.
This matrix verifies combined API/lifetime behavior; the numerical motion and
material contracts have their own fixtures, and gameplay integration remains
pending.
