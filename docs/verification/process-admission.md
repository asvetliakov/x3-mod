# Process admission configuration

The ownership entries, loader exports and capture/loading hooks share one
DLL-lifetime monitor, selected on first use by exactly `X3M_ADMISSION=1`.
An absent value, `01`, or a longer value leaves admission disabled. Changing the
environment after initialization does not replace the monitor underneath live
objects. This option remains off by default and does not enable live replay.

`process_admission_monitor()` uses public Win32 `InitOnceExecuteOnce` for its
one-time environment read. An acquire/release atomic publishes the immutable
pointer. Later calls perform an atomic read and return that pointer without
entering Win32 or admission bookkeeping. The cold configuration path preserves
the complete x87 state, MXCSR and LastError. It never performs graphics work or
waits for an application admission ticket. `DllMain` does not call it.

The loader enters admission before backend initialization in all eleven exports.
Successful unwrapped factory or Ex results, failed wrapper adoption returning a
native factory, and nonnull native shader validators permanently veto replay
with `UnobservedRoute`. Native results and outputs are still forwarded. Ordinary
nested entry is not authority to exempt an escaped interface; a future narrowly
validated D3DX helper would need its own lifetime contract.

## Verification

`python3 verification/probe/run_process_admission.py` builds the real getter and
adapter with their portable core. Four fresh x86 processes cover enabled, absent,
malformed and long environment settings. Each releases eight workers from one
event to attempt concurrent first use, compares their published pointers, and
verifies balanced scope retirement. At least the first lookup is cold; scheduling
can let later workers observe the hot path. This fixture does not count eight
independently observed cold or contended executions.

Each worker seeds live x87 values, nondefault floating-point control/sticky
state, MXCSR and LastError around its getter call. The fixture also changes the
environment and checks that a subsequent hot lookup preserves CPU state and the
original selection. These direct getter witnesses do not certify an enclosing
proxy hook's compiler prologue, cold unwinder, or all native driver callbacks.

The runner requires exactly 49 passing checks per mode, records source,
executable, object, report and runtime hashes, and invalidates an old summary
before beginning another run. It refuses to run while X3AP is active. Evidence
is retained in `verification/results/process-admission-summary.json` and the
four paired stdout/stderr reports. Execution uses CrossOver Preview; native
Windows runtime behavior remains unverified.

The final run passed all **196 checks across four modes** with matching source,
object, executable and runtime hashes. Independent review accepted the getter,
fixture and qualified first-use claim; the runner's old-result invalidation fix
was included in this final run.
