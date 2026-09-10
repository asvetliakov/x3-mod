# Ownership integration in the app-local proxy

The isolated experimental 0.4 DLL includes the canonical ownership implementation before the existing capture hooks. Its default path still captures native normal-D3D9 objects. `X3M_OWNERSHIP=1` selects wrapper adoption; failure logs the HRESULT and continues using the caller-owned native factory. `Direct3DCreate9Ex` remains native and uninstrumented. The switches are read once during first backend initialization, outside `DllMain`.

`X3M_SAMPLEABLE_DEPTH=1` additionally requests sampleable auto-depth, but is effective only with ownership enabled. The launcher has corresponding `--ownership` and `--sampleable-depth` options; the latter requires the former. Both are off by default. This verification does not authorize or perform game installation or gameplay.

## Reproduction

```sh
cmake -S . -B build-ownership -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-ownership
verification/probe/build_ownership_integration.sh
python3 verification/probe/run_ownership_integration.py
python3 verification/probe/verify_ownership_integration.py
python3 verification/probe/run_ownership_integration_fallback.py
```

The runner also consumes the existing smoke, capture-state and baseline-contract fixture executables built by their respective scripts. It copies the DLL and executables into disposable verification directories and uses `/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine --bottle Steam --no-update`, with app-local D3D9 override. It does not mutate bottle settings, launch X3, install a DLL, or replace `build/d3d9.dll`.

## Verified checkpoint

Experimental DLL SHA256: `ecdc8614a088e8c7f66a99b84df735ce06646580f89d97280649e8fbe65410e6`.
Ownership source SHA256: `5262c47ed58152cb7f5a93a0a0aace970db5b723421ff4fd6fb479ef4d43f08c`.

The 12-case matrix passed:

| Mode | Fixtures | Result |
| --- | --- | --- |
| Native/default | Smoke, capture state, lifetime, 370 baseline contracts, auto-depth API smoke | 5 passed |
| Ownership; sampleable-depth off | Same five fixtures | 5 passed |
| Sampleable-depth requested without ownership | Smoke; logs effective depth switch off | Passed |
| Ownership plus sampleable-depth | Auto-depth API smoke through create, clear, Present and Reset | Passed |

The capture verifier still passes with ownership enabled, and snapshots include all 13 stencil renderstates. Logical texture/surface identities, getter behavior, stateblock restoration, Reset and application-visible fixture results match native mode. Synthetic lifecycle runs release the application factory first, release the device while a texture remains, recover device/factory identities from that child, and release the child last. All 16 device contexts were destroyed in each mode; ownership mode reused 13 device addresses without leaving stale capture hooks. Every case's hooked device IDs has a matching final-destruction record.

The depth-enabled smoke observed requested/available/bound true, logical `D24X8` (77), generation 1 at creation and generation 3 after resizing Reset. This establishes loader wiring and diagnostics only. Separate root-owned numerical/content tests found that physical INTZ versus ordinary D24X8 depth copies do not reproduce every native StretchRect result in this checkpoint. Sampleable-depth is experimental and is not claimed transparent or ready for game use.

Reports and exact per-file provenance are in `verification/results/ownership-integration-build.json`, `ownership-integration-verification.json`, and the corresponding case text/capture/Wine logs. Source hashes were unchanged across the matrix. The installed 0.3 artifact was not changed by these runs.

## Adoption failure and ABI audit

A separate verification DLL links the same five compiled proxy objects against a test-only adoption stub that returns `E_OUTOFMEMORY` without consuming the native factory reference. It is kept outside the production build. The smoke passes, logs `mode=native_fallback result=8007000e`, and destroys both device contexts. Its separate DLL hash and production-object list are recorded in `ownership-integration-fallback.json`; it is never installed.

Capture replaces only the object's primary COM vptr. Ownership's internal `Node` has a separate C++ virtual destructor vptr. In the actual i686 DLL, both Factory and Device public `Release` add four bytes to obtain the `Node` subobject; the internal deletion dispatches via that secondary vptr's deleting-destructor entry. Disassembly is saved with the DLL hash in `ownership-integration-abi.txt`. This confirms that the capture COM table does not replace the destructor table in this build.

Final child cleanup now releases its parent through `parent->application->Release()` after leaving the registry lock. That dispatch is essential: it reaches the capture Release hook for child-induced final device/factory destruction. Directly calling the internal ownership release helper would bypass hook cleanup. The compiled parent dispatch uses COM slot 2; the repeated lifecycle test exercises it with factory-first and device-first application release ordering.

The imports audit shows KERNEL32, USER32 and UCRT dependencies; no external libstdc++, libgcc or libwinpthread DLL is needed. These are synthetic API/lifetime checks, not gameplay, visual-quality, frame-pacing or performance measurements.
