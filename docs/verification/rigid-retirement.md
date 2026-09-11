# Bounded replay reference retirement

The detached rigid motion pass accepts an optional caller-owned
`RigidMotionRetirement`. Its seven fixed slots retain the references acquired by
one invocation: one stateblock, up to four render targets, one depth surface and
one output texture-level surface. Transfer does not allocate or add references.
The ordinary two-argument call keeps immediate release behavior.

The batch must be empty before `run`. It reserves itself before the first native
getter, remains unavailable through transfer, and rejects same-batch reentry
without changing the pass diagnostics or generation. `release()` first detaches
all slots, then releases the detached references. Recursive release is harmless;
`empty()` remains false and another `run` is rejected until callbacks finish.
These guards are not thread synchronization. The caller must serialize the pass
and batch; arbitrary same-pass reentry using a different batch is unsupported.

Declare the batch outside any future exclusive replay scope and call `release()`
after leaving that scope, before Reset or device teardown. Its destructor is a
fallback at that same outer lifetime. No current MotionCapture call supplies the
batch. This step neither implements portable exclusion nor establishes that
native setters, getters, draws, stateblock application or other reference drops
are free of application callbacks.

## Native verification

Run from the repository root:

```sh
python3 verification/probe/run_rigid_retirement.py
python3 -m unittest verification/analysis/test_rigid_retirement_report.py
python3 verification/probe/run_rigid_motion.py
```

The retirement fixture passed **168 checks, 10 injected cases, 84 real native
resource private-IUnknown destruction callbacks, two devices and two actual
Resets** on the Steam bottle in CrossOver Preview. Normal and pure hardware
vertex-processing devices both bind all four render targets. The fixed fixture
uses original resources and embedded original shader programs, with no game
source contracts or game launches.

Each resource cohort attaches independent IUnknown witnesses to four original
render targets, the original depth surface, the motion texture's level-zero
surface, and a sampler texture retained by the captured stateblock. After `run`,
the fixture unbinds and releases its own resources. Complete capture keeps every
witness alive until explicit batch release. A failed depth getter captures six
of the seven relevant owners; the uncaptured depth witness alone can retire
early. Callback counts and per-witness exactly-once checks prevent an empty batch
or leaked references from passing.

The tested exits are successful execution, partial state capture, ordinary
restoration failure, and an injected DEVICELOST result from the invalid-target
initializer. The loss is synthetic: the fixture closes the still-real open scene
and repairs its bindings outside the tested call before releasing application
references. This proves the loss return path transfers ownership; it is not a
physical device-loss experiment.

Native capture injection reenters `run` with the same batch and calls `release`
while collecting. Real destruction callbacks likewise recurse into both APIs
while retiring. Both paths refuse reuse without clearing or duplicating the
batch. Further controls cover nonempty rejection, repeated reuse, idempotent
release, and destructor retirement. The unchanged default-path rigid fixture
retains the existing numerical and state-restoration coverage; this dedicated
fixture adds lifetime evidence rather than new motion-quality claims.

The fresh-build runner records source, executable, Wine launcher and report
SHA-256 values, refuses a running X3AP process, bounds execution, and accepts only
the exact terminal/count/device/Reset inventory. Nine portable parser controls
reject missing, failed, duplicated, zero-check and trailing-output reports.
Results are in `verification/results/rigid-retirement-summary.json` and
`rigid-retirement.txt`. Generated binaries remain untracked.
