# Iteration 4: consolidated scene and loading diagnostics

The combined build, independent reviews and all 15 integration cases pass.
The verified diagnostic DLL is installed; the user-managed gameplay test is complete. This checkpoint
does not enable gameplay TAA, HDR or a loading optimization.

## Completed coordinated game run

The installed build combines original-preserving scene-depth capture, exact-executable
engine submission context, observed vertex/index buffer write revisions, and
mesh loading timings. This batches the evidence needed for temporal motion and
loading work into one user-managed load cycle.

The command supplied for the user-managed CrossOver Preview run was:

```sh
python3 tools/manage.py launch --direct --telemetry --ownership --depth-copy --scene-depth-capture --object-trace --capture-frames 4
```

The user launches and loads gameplay. Capture a stationary view with F8, then a
slow turning view with F8, and, if available, a moving ship in view with another
F8 burst. Docking or undocking in the same run adds useful coverage. Keep the
gameplay scene and actions identifiable when reporting completion. Detailed
capture causes a diagnostic hitch; this run is not a steady-state performance
benchmark. Ctrl+Shift+F7 adds a marker only when Present is running, so a blocked
loading interval cannot be marked in real time through this key.

Record whether the game and macOS cursors both remain visible after returning
from alt-tab. No cursor or presentation fix is included yet.

## Results and follow-up

The completed session `session-20260910-234001-212.log` contains 20 complete
captured frames and 13,431 successful draws, including a user-reported final
third-person burst. The user selected a different game start/sector; no repeat
of the previous planet scene is needed just to diagnose this run.

- [Camera/object analysis](../reverse-engineering/iteration04-camera-motion.md)
  verifies submitted transforms and independent object motion. Node handles are
  still observations, not lifetime guarantees; four changing unscoped vertex
  buffers need separate temporal handling.
- The installed selector attempted no depth copies. It unnecessarily required
  planet haze, and later ColorFill invalidation obscured the first rejection.
  Source fixes permit verified background without haze and positively identified
  nonalias scratch texture fills, log their targets, and retain the first failure.
  Old logs lack ColorFill targets, so acceptance in the game remains unproven.
- [Point-light analysis](../reverse-engineering/iteration04-lights.md) finds
  active one- and two-light inputs; these are the engine's selected per-draw
  inputs, not its complete light population.
- [Loading analysis](../reverse-engineering/iteration04-loading.md) measures
  21.287 seconds in adjacency generation and recurring activity. Exact content
  reuse still needs validation; no loading cache is enabled.
- The [archive-wide shader sweep](../reverse-engineering/shader-sweep.md)
  covers 751 unique programs in all 3,480 effect files, including uncaptured
  material/effect paths. Its unknowns remain explicit.
- Our CPU arithmetic now targets SSE2 with an explicit legacy stack contract;
  see [ABI evidence](../verification/sse2-abi.md). Source changes made after this session have
  not been installed. The installed hash below still identifies the tested 0.4.

Further diagnostics should be batched before another user-managed gameplay run.

The corrected source build passes all 15 actual-DLL integration cases, the
forced adoption-failure native fallback, and 148 Python analysis tests. The
scene adapter separately passes 36 scenarios / 4,908 checks / 16 GPU samples.
Its freshly built `build-ownership/d3d9.dll` SHA256 is
`b81af5d3c0c9c0fd7cbed54ee7fe613453f8309288b17d7602916e3fa0bd1896`.
This is an uninstalled source checkpoint, distinct from the historical installed
artifact below. The preserved 0.3 rollback DLL is also unchanged.

## Evidence and boundaries

- [Scene selector](../reverse-engineering/scene-boundary-selector.md) matches a
  verified shader/binding/clear sequence rather than fixed draw indices. The
  [runtime adapter](../verification/scene-capture.md) preserves depth before its destructive clear
  only during requested capture frames. Missing evidence rejects the boundary.
- [Object context](../reverse-engineering/object-identity.md) is collected inside
  a verified engine submission call. Pointer/handle tuples are observations,
  not lifetime-safe identity across save reloads or reuse. Current matrices are
  captured; prior transforms and motion vectors still require validation.
- Buffer revision diagnostics distinguish observed CPU/GPU writes from transform
  changes. A revision is not a content hash. Disabled, ambiguous, pending-lock or
  failed metadata states must never be interpreted as unchanged geometry.
- The production temporal pass has standalone GPU verification but is not
  connected to game rendering. Post-bloom color includes background and effects
  whose depth is not represented by the main-scene snapshot; this cannot yet
  serve as a blanket whole-frame TAA input.
- Loading instrumentation measures additional mesh work. It does not assume
  repeated meshes, enable a cache, or claim a game loading speedup.

## Reversible installation

The experimental DLL is built separately in `build-ownership/`. Installation uses
`python3 tools/manage.py install --dll-source build-ownership/d3d9.dll` after the
combined verification passes. The preserved `build/d3d9.dll` is the owned 0.3
rollback artifact; reinstall it using `python3 tools/manage.py install`.
Neither command changes executable/assets or bottle-wide overrides.

Verified DLL SHA256: `81e3b121659c0fa1811641a5dbe019f668341477a787c6729fb5a848e056c516`.
The [review report](../verification/review-04.md) records findings, fixes and bounded verification.

[Installation record](../../verification/results/iteration-04-install.json) verifies
the installed hash, preserved 0.3 rollback hash and unchanged X3 executable hash.
The agent did not launch the game.
