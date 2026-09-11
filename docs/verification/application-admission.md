# Portable application admission core

The monitor in `src/ownership/application_admission.{h,cpp}` implements
the bookkeeping needed to keep ordinary application transactions out of a private
replay interval. Production entrypoints now use it through the off-by-default
[process configuration](process-admission.md) and ABI adapter. Complete entry
coverage, callback restrictions and CPU-state preservation remain requirements
in the [replay contract](../architecture/motion-replay-exclusion.md).
`motion_live_replay_available` remains false.

## Behavior and lifetime

Ordinary outer application calls count as independent roots. Nested calls on the
same thread inherit their root, allowing normal native callbacks during ordinary
work. The monitor mutex protects only bookkeeping; callers hold no monitor mutex
while executing native code or publishing their associated metadata.

Only the sole active, outermost application boundary can promote to replay.
Promotion refuses if another root is active, if a permanent veto was recorded,
or if callers awakened by an earlier replay are still waiting. It never waits
for another application's transaction to finish. New outer roots wait while
replay is active and resume after its explicit end. Pending mappings that span
API calls are a separate validation requirement; root counting does not track
mapped memory by itself.

Registration and coverage vetoes accumulate permanently, retaining the first
reason. Neither ordinary scope retirement nor later quiet intervals reset them.
Native registration must hold ordinary admission and announce its veto before
dispatch. The monitor's synchronized `veto` method alone does not authorize an
uncounted native call during replay.

Scopes are noncopyable, nonmovable, and require same-thread LIFO lifetimes.
Explicit `finish()` supports ending a child Release transaction before dispatch
to its parent. Wrong-thread and out-of-order explicit finishes refuse; correctly
ordered scopes can subsequently retire. This is diagnostic handling of misuse,
not recovery from destroying an object while another live scope still points at
it. Monitor lifetime must exceed every associated scope and waiting thread.

Same-thread application entry during replay refuses and permanently vetoes future
promotion. The integration must prevent that route through its reviewed callback
contract: this diagnostic result is **not** authority to invent an application
HRESULT or forward through modified device state. A different nested monitor also
refuses and taints both monitors; production must use one process-wide monitor.

## Verification

Run `python3 verification/probe/run_application_admission.py`. The runner verifies
unchanged source and executable hashes, exact case/check inventories and terminal
status. It records compiler versions, commands, logs and elapsed time in the
[summary](../../verification/results/application-admission-summary.json).
The Preview run refuses to start while X3AP is running. It creates no D3D device.

All four variants pass **4,865 checks each**:

| Variant | Scope |
| --- | --- |
| macOS optimized | Portable monitor behavior |
| macOS ASan/UBSan | Memory and undefined-behavior controls |
| macOS ThreadSanitizer | Concurrent monitor and fixture accesses |
| x86 Windows executable under Preview | MinGW build and CPU-only execution with SSE2 and four-byte incoming-stack flags |

Eight cases cover ordinary/nested admission, six barrier-controlled wait
intervals, eight simultaneous roots, permanent vetoes, explicit lifetime misuse,
128 publication races, 4,000 concurrent nested roots, and 32 veto/promotion races.
The six wait intervals model dispatch boundaries; they do not call six actual
D3D methods. Race tests accept either valid scheduling order; deterministic
barriers separately establish admission and refusal behavior. Joins and yields
inside these synthetic tests are not approved operations for the future GPU
replay segment.

The x86 executable uses `-msse2 -mfpmath=sse -mstackrealign
-mincoming-stack-boundary=2` and static support libraries. This is cross-compilation
and Preview evidence, not execution on native Windows. The core deliberately
does not preserve floating-point state or LastError. The separate
[x86 ABI adapter](../architecture/application-admission-abi.md) passed 130
checks for independent entry/retirement state preservation without undoing the
application's intervening native result/state at its standalone checkpoint.
Current production integration is verified separately for
[ownership entries](ownership-admission.md) and [proxy hooks](proxy-application-admission.md).

## Review and cost

The monitor performs no explicit per-entry allocation or native graphics call.
Nested ordinary scopes use TLS and avoid the monitor mutex. Outer admission and
retirement each take one short mutex; replay promotion and retirement also use
short critical sections. Waiting uses a condition variable and occurs only at
an outer boundary before capture, ownership or native locks. Standard-library
initialization and wait machinery are not claimed allocation-free.

Independent source and final-artifact review accepted this standalone checkpoint.
The review identified repeat promotion overtaking awakened roots; the
implemented waiting-root refusal and fixture control address it. A separate
review harness exercised 120,000 ordinary roots with concurrent promotions under
ThreadSanitizer. The ABI adapter's separate benchmark measures disabled, outer
and nested scope cost; complete production overhead still needs measurement.
Fixture process elapsed times are not per-draw timings or game FPS.

Complete wrapper/capture/D3DX/window entry coverage, native callback vetoes,
mapping validation, full restoration and deferred retirement remain required
before live replay can use this core. None is inferred from these tests.
