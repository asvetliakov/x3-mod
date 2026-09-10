# Iteration 5: depth, lifetime and loading-cache integration

Preparation is in progress. This document describes the consolidated next test;
it does not establish that a new DLL is installed or that the game test passed.
The installed artifact remains the historical iteration-4 build until a verified
installation record says otherwise.

## Combined changes

- The corrected scene selector permits a sector without planet haze, records
  scratch-fill target identities and preserves the first rejection reason.
- Captured draws include actual motion-input gates and exact submitted-row
  hashes. The opt-in registry observer supplies separately verified storage
  lifetimes, load epochs and before/after mutation diagnostics.
- The opt-in adjacency cache reuses exact native mesh results within bounded
  storage. Backend fingerprints, native buffer contracts and known buffer state
  gate entry; native computation remains the path for ordinary ineligible calls.
  A cache acquisition cleanup failure is explicit and requires a process restart.
  The cache does not claim to repair a failed unlock.

The motion producer and temporal resolve are still detached. This build does not
enable gameplay TAA, HDR, new materials, or a cursor/presentation fix. Any loading
improvement must be measured from actual accepted cache hits and their total cost.

## User-managed test

After combined verification and installation, run from the repository:

```sh
python3 tools/manage.py launch --direct --telemetry --ownership --depth-copy --scene-depth-capture --object-trace --object-lifetime --mesh-cache --capture-frames 4
```

The user launches and loads gameplay. Keep the same scene/save available for the
reload portion; a planet is not required.

1. Load a flying scene and press F8 once while stationary.
2. Slowly turn and press F8 again. A moving ship in view is useful if available.
3. Switch between cockpit and third-person view, then capture another burst.
4. Reload the same save/start in this process and capture once after loading.
   This checks load invalidation and repeated mesh work in one launch.

Detailed capture deliberately causes a hitch; these frames are not a steady-state
performance measurement. Loading telemetry runs independently of F8. Report the
scene, whether the camera switch and reload completed, any new visual or loading
failure, and whether the already-known double cursor persists after alt-tab.

## Evidence required before the next renderer step

- A complete capture with matching device/frame/draw coordinates and successful
  draw results, or the exact first failure.
- Scene-depth acceptance and a successful pre-clear copy with matching source
  and snapshot identities. A successful RESZ trigger alone is insufficient.
- Observer activation/baseline status, known node/camera serials where available,
  and fresh load/registry generations after reload. A camera mode change may keep
  the same storage lifetime and still require temporal-history invalidation.
- Cache gate rejection counts, calls/hits/misses, retained bytes and acquisition,
  lookup, copy, native and gate costs. Separate startup fingerprint cost and
  overlapping telemetry spans; do not infer a full loading speedup from hit time.
- Unknown or changing geometry, particles, HUD and other unsupported contributors
  stay explicit. Draw-input proof masks alone do not authorize whole-frame TAA.

All gameplay remains user-managed. Synthetic probes and source reviews precede
this test; no additional launch is needed just to finish those checks.
