# Iteration 4: consolidated scene and loading diagnostics

The combined build, independent reviews and all 15 integration cases pass.
The verified diagnostic DLL is ready for reversible installation. This checkpoint
does not enable gameplay TAA, HDR or a loading optimization.

## One coordinated game run

The next build combines original-preserving scene-depth capture, exact-executable
engine submission context, observed vertex/index buffer write revisions, and
mesh loading timings. This batches the evidence needed for temporal motion and
loading work into one user-managed load cycle.

Once the verified build is installed, launch through CrossOver Preview with:

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

## Evidence and boundaries

- [Scene selector](../reverse-engineering/scene-boundary-selector.md) matches a
  verified shader/binding/clear sequence rather than fixed draw indices. The
  [runtime adapter](scene-capture.md) preserves depth before its destructive clear
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
The [review report](review-04.md) records findings, fixes and bounded verification.
