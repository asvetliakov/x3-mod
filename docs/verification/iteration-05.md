# Iteration 5: depth, lifetime and loading-cache integration

The reviewed combined build is installed, and the user completed the gameplay
run. The installed DLL has not been replaced by follow-up source work.
No gameplay was launched by the agent.

DLL SHA256: `ed19a7abf54ae2b9debf912f3d343a0c9217038a2162cb6a9eb174fc8050bbd5`.
The [installation record](../../verification/results/iteration-05-install.json)
verifies the installed hash and unchanged game executable/bottle configuration.
The source checkpoint is `c8a3fb6`, following lifetime `83998e0` and cache `0ffeb52`.
All 15 combined integration cases and the 15-object native fallback pass. Separate
reviewed fixtures pass 533 lifetime checks, 5,757 actual cache-hook checks and
68 + 123 loading regression checks; the analysis suite passes 213 tests.

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

## Completed user-managed test

The user captured stationary first-person, turning/steering first-person,
third-person stationary and moving views, then loaded a different planet save.
The different save supplies a real load discontinuity; repeating the same region
is not currently required. The completed trace has 28 captured frames in seven
bursts, so exact burst-to-action correspondence is not inferred from five labels.

All 24 captured gameplay frames report successful pre-clear scene-depth copies
and confirmed boundaries. All 12,753 scoped draws have consistent observed
lifetimes; 204 draws are unscoped. Load epoch advances from 1 to 2, distinguishing
17 reused handles. See [lifetime analysis](../reverse-engineering/iteration05-lifetimes.md)
and [depth/motion analysis](../reverse-engineering/iteration05-depth-motion.md).
The loading cache rejected all 7,199 candidate calls before acquisition and
recorded no hits. The follow-up dynamic SYSTEMMEM source extension is reviewed
and passes native/wrapped fixtures, but is not installed. See the
[gate diagnosis](../reverse-engineering/iteration05-cache-gate.md). This run does
not demonstrate a loading speedup. The current analysis suite passes 246 tests.

The command and original combined test plan are retained for reproduction:

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

## Rollback

The previous tested iteration-4 DLL is preserved locally, hash
`81e3b121659c0fa1811641a5dbe019f668341477a787c6729fb5a848e056c516`.
With the game closed, restore it using:

```sh
python3 tools/manage.py install --dll-source artifacts/rollback/d3d9-iteration04.dll
```

The older iteration-3 `build/d3d9.dll` rollback is also unchanged. Installation
replaces only the owned app-local proxy and its ownership manifest; no bottle-wide
override or game asset is edited.
