# Central chase instruments and native timing diagnostics

2026-09-13, isolated implementation after run 20. The user confirms the missing
instrument stays near screen centre. The [native study](../reverse-engineering/chase-target-indicator.md)
identifies the separate crosshair, target-distance and speed-panel gate. This
change is not installed or visually accepted yet.

## Native instrument restoration

The optional gate at `0x42aae0` clears only the native branch's saved zero flag
for the currently applied active main chase cockpit. It checks the actual
constructor generation, current update/thread, native cockpit/ship/camera
identity, mode 258/connect zero/main gun, 2D/display-body flags, active HUD scene,
target-body enable and texture 15's native metadata/cache availability. A
tracked target, tracking mode and successful ballistic solution are not required.

The native instrument code keeps its styles, screen positions, texture ownership
and later distance/speed refresh. No producer is called again. The distance stays
the native ship-to-target instrument reading; the central crosshair remains a
fixed-screen graphic, separate from the corrected predictive marker.

Three legacy entries at overlay offsets `0x118`, `0x140` and `0x12c` are hidden
by the original rejected branch but not updated by the admitted group. All three
active flags must be readable and zero before admission. If any is active, the
original eight-entry hide sequence runs this update; admission can follow on the
next update. This retains native cleanup without calling the recursive,
callback-capable scene detacher from the new hook. Unavailable textures likewise
leave native hiding intact until the existing producer makes the resource ready.

Central admission has its own one-site transaction. Failure leaves the existing
predictive marker and cursor-fire correction operational. Its status distinguishes
installation from an unavailable prerequisite; refusal counters distinguish
ownership, display, cleanup and texture conditions.

## Consolidated native timing

Runs 20/22/23 contain selection-adjacent 0.4–0.48 second frame maxima which the
recorded lead-handler, Present, TAA, HDR and log-flush timings do not explain.
Those maxima also occur outside selection windows. No cause is established.
The newly admitted native solver calls synchronous VM `GetTurretFiringMask`
through `0x447204`, then scans weapons. Its closed-form intercept path skips the
larger target-turret geometry search. Existing native distance text processing
can issue synchronous range notifications and rasterize texture text.

With telemetry enabled, five additional sites measure complete native paths:

| Interval | Entry | Exit |
| --- | --- | --- |
| Lead block | Existing `0x42a6fe` | Central gate `0x42aae0`, after native success or hide |
| Gun-group solver | `0x42a792` call | `0x42a797`, including false return |
| Central instruments | `0x42aae0` | `0x42aed6`, after native draw or hide |
| Distance/speed producer | `0x423007` call | `0x42300c`, including the producer's early return |

The five new sites form one optional transaction, dependent on the central
endpoint's installation and a positive performance-counter frequency. An
unavailable frequency reports `clock_unavailable` and installs no timing
sites. Any failure disables only these timings. No return
addresses are rewritten and no VM-wide hook is installed.

Eight fixed thread slots hold four phase timestamps, native frame witnesses and
cockpit generation/update identities. Constructor/destructor and every update
entry/exit revoke unfinished intervals. Constructor/destructor boundaries
revoke the callback thread even when their argument cannot be read, and also
revoke a decoded cockpit across other threads. Read/clock errors also revoke them;
an escaping native exception cannot pair an old timestamp with a later update.
No resources or locks are held across original engine calls. Timers retain four
initial and four recent intervals of at least 10 ms per report window; all other
calls contribute only totals/maxima. A target/mode change inside a measured
interval is retained in its ending fields. Common endpoints without a start are
counted as unmatched, not fabricated durations.

These spans include intervening hook/CPU-boundary overhead. Separate per-kind
transition handler totals include lock acquisition and checked reads but exclude
the outer CPU boundary and counter aggregation. The fixture's paired RET-baseline
benchmark measures emitted-stub overhead under normal CPU state with callbacks
disabled; it does not establish game FPS or native-call cost. No timing callback
writes logs or allocates, and no additional native producer is invoked.

## Focused verification

The actual central admission functions are exercised with synthetic native
memory, including no-target admission, all display/ownership guards and native
cleanup followed by admission on a later update. The portable timing state and
actual installation transaction receive focused lifetime, failure and rollback
checks. Actual callback dispatch tests bind indices 4–8 to their phases, retain
a false solver result, close all four intervals, and revoke an open interval
when its native context becomes invalid. The installed-EXE probe verifies all nine lead/HUD/timing spans, their
relocations, and complete normal success/failure/hide paths to the shared
endpoints. Calls are assumed to return normally for that static path proof;
exceptions produce abandoned, not completed, samples.

The retained eighteen-stub CPU fixture passed in X3: **353 checks, zero
failures, exit 0**, without rebuilding. The
[compact runtime result](../../verification/results/bottle-X3/chase-central-hud-cpu.json)
binds the executed EXE hash, immutable stdout/stderr and runner command. It
checks hostile GPR/flags/DF, live x87, XMM0–7, MXCSR, LastError and the four-byte
caller stack. Transition handlers and their timing are enabled on guarded
fixture inputs; lead, central-HUD and native-timing callbacks are disabled,
and continuations are RET. Active native cockpit policy, native hide/solver/HUD
calls and displaced game tails are therefore not qualified by this CPU run;
active handler policy and site/control-flow evidence remain the separate host
and installed-EXE checks described here.

The same run's bounded paired benchmark uses 4096 calls per trial, three trials
per stub, normal CPU state and **all callbacks disabled**. Across eighteen
stubs, the mean full-stub-minus-RET increment ranges **0.521–0.567 µs per call**,
with a median of **0.549 µs**. The shared harness itself is included in each
raw measurement and removed by this paired difference. This establishes only
emitted-boundary/disabled-guard overhead in this fixture, excluding active
handler work and native producers; it cannot explain selection stutters or
establish game FPS. No production DLL was built or installed for this evidence
closure. Gameplay alignment and native-Windows execution remain unverified.

Current source checkpoint passes **32 focused host tests**: **60 lead/HUD
scenarios with 194 checks**, **12 timing-state scenarios with 34 checks**, and
**147 transition assertions**. The extracted diagnostic installer passes every
claim/emission/continuation-store/chain failure at each of its five sites,
reverse rollback, rollback failure with disabled callbacks, and unavailable
prerequisites. The separately extracted central-HUD installer likewise covers
its claim/emission/store/chain and restore failures, disabled callbacks after
failed rollback, and preservation of all three existing lead sites. Both site probes and strict x86 object compilation pass. The
18-stub fixture and both callback CPU-boundary audits cross-build successfully;
the scoped X3 preservation run and disabled-callback benchmark now pass as
recorded above. The earlier twelve-stub result remains historical evidence.

Reproduction (host and optional fixture build):

```sh
PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_chase_lead verification.analysis.test_chase_lead_sites verification.analysis.test_chase_native_timing verification.analysis.test_chase_native_timing_install verification.analysis.test_chase_central_hud_install verification.analysis.test_chase_transition verification.analysis.test_chase_transition_sites
python3 verification/probe/build_chase_transition_cpu.py
```

The recorded runtime command used the retained EXE, not an implicit rebuild:

```sh
X3M_FIXTURE_BOTTLE=X3 WINEDEBUG=-all python3 verification/probe/wine_lock.py --holder chase-central-hud-cpu-r1 --timeout 60 '/Applications/CrossOver Preview.app/Contents/SharedSupport/CrossOver/bin/wine' --bottle X3 --no-update /tmp/x3-chase-central-hud/build/verification/chase-transition/chase_transition_cpu_fixture.exe
```

The root runner executed from `/tmp/x3-chase-central-hud-cpu-r1` with an
absolute lock-script path and separate `stdout.txt`/`stderr.txt` redirects;
the compact result records that exact command. This reproduction line assumes
a repository working directory. All new Wine execution still requires the
shared runner lease and the game to be closed.

The correction and timing hooks are now installed in combined candidate
`75dbbed`; [run 7](../verification/user-runs.md) covers the missing central
display and selection pauses. No additional gameplay or native-tail timing
claim follows from the CPU fixture.
