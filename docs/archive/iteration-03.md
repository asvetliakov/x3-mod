# Iteration 03: consolidated diagnostics

This checkpoint prepares one user-run session for loading, render-boundary and
cursor investigation. It does not enable a visual enhancement or claim a loading
speed improvement. See [telemetry coverage and verification](../verification/telemetry.md) and
[loading disassembly](../reverse-engineering/loading-performance.md).

## Changes

- Optional `--telemetry` launcher flag, scoped to the child process. Normal launches
  explicitly disable the additional telemetry.
- Bounded QPC timing for rendering/resource/shader APIs and separate capture work,
  startup anchors, cursor/focus/window observations and ordered depth/copy/clear
  boundaries during F8 capture.
- Fingerprint-gated main-executable import timing for file, asset and compressed
  loading paths. No EXE/archive bytes on disk are changed. Unknown executables
  decline the import hooks, while standalone device telemetry remains usable.
- Offline reports keep captured intervals separate, reject inconsistent metric
  records and retain operation ordering. API wall times can overlap and must not
  be added into a supposed total loading time or interpreted as GPU timings.

## Next user-run session

After installing the verified DLL, launch from the repository root:

```sh
python3 tools/manage.py launch --direct --telemetry --capture-start 999999 --capture-frames 4
```

In that one run:

1. At the main menu, optionally press **Ctrl+Shift+F7** to mark menu readiness.
2. Start/load the test game; when controllable, optionally press the same chord.
3. Press **F8** once while gently turning, preferably with a ship/station and a
   local light source or weapon effect visible if convenient.
4. Switch away and back using the usual shortcut. Observe whether two cursors
   appear, whether they move together, and whether moving out/in or clicking
   clears it. A second F8 after the symptom is useful if it is easy to reproduce.
5. Exit normally when finished so final device summaries are written.

Phase markers are optional, sampled during Present and not consumed by the proxy;
pressing them during a stalled load can be missed. Timing coverage begins when
this proxy initializes, so earlier process startup is explicitly unmeasured.
No game launch is performed by the agent. The direct command skips the separate
always-on-top launcher.

## Verification and installation

The installed 0.3 DLL matches the tested build:
`71f59c8e6422d5bbf2f55c116e2c0388026d956ba45a3a03eee53c4d85a232a6`.
Installer ownership verification succeeds. The executable, archives and bottle
configuration remain unchanged.

- 51 offline analysis tests pass; production and SDK vtable assertions compile.
- The loading-import fixture passes 57 checks, covering all 14 hooks, arguments,
  return values, LastError, pending reads and safe IAT restoration.
- Baseline/telemetry-off/telemetry-on graphics fixture outputs match exactly,
  including readback pixels, API results and cursor LastError sentinels.
- The integrated build passes the original smoke test and extended capture
  fixture: normal/pure devices, stateblocks, typed constants, allocation identity,
  reset and final COM releases. The actual capture verifier accepts ten frames.
- The telemetry verifier confirms summaries without Present, bounded cursor
  reporting and ordered render events. The report parser accepts 26 metric series
  with zero rejected records. A reset gap is excluded from ordinary frame timing.

Evidence is recorded in `verification/results/analysis-tests-v3.txt`,
`loading-trace-fixture.txt`, `telemetry-verification.json`,
`telemetry-build-verification.json`, and `capture-state-v3-summary.json`.
This is synthetic verification of the installed diagnostics; X3 loading attribution
and cursor reproduction await the single user-run session above.

## Interpretation and next implementation

The existing turning capture already validates camera factorization and exposes
ambiguous object correspondence. Instruction inspection identifies actual WVP
consumption and pre-target material-lighting clamps. This build supplies the
remaining ordering/performance observations together; it does not replace them
with a fixed draw-index classifier.

Before persistent INTZ/FP16/history resources are introduced, the ownership layer
must pass the documented [resource-lifetime gates](../architecture/resource-lifetime.md).
A native texture held until device Release reaches zero is experimentally unsafe.
The next rendering checkpoint should implement that ownership contract and then
validate scene depth and scene-only jitter, with history/reprojection following.
