# Application admission ABI adapter

The standalone `application_admission_abi` adapter preserves the x86 caller's
complete x87 state, MXCSR and Win32 LastError independently around admission
bookkeeping. It is not yet integrated into the ownership wrappers or proxy hooks.
The portable [admission core](../verification/application-admission.md) owns serialization and
replay admission; this adapter owns ordinary-return state transport at its own
function boundaries.

## Boundary and lifetime contract

`ApplicationAdmissionAbi(nullptr)` is disabled: it creates no core scope and
performs no lock, TLS access, FP save or Win32 call. `requested()` distinguishes
that option-off state from an enabled admission. Its result is
`InactiveBoundary`, not `Admitted`. Enabled entry, explicit finish, destruction,
active-boundary access, replay entry/finish, veto and snapshot each save and
restore state around their own bookkeeping. No save spans the application's
native operation: finishing afterward preserves that operation's new outgoing
state, HRESULT and output values.

The adapter uses opaque 108-byte `FNSAVE`/immediate `FRSTOR` transport, plus
`STMXCSR`/`LDMXCSR`; it performs no x87 arithmetic. FP capture precedes
`GetLastError`, and FP restoration follows `SetLastError`. Volatile XMM register
values follow the ordinary C++ ABI. The x86 build uses SSE2 arithmetic and the
four-byte incoming stack flags required by the project.

Inline raw storage contains the core scope without an implicit member cleanup
after state restoration. Successful explicit finish also destroys the core
inside the same boundary. Failed same-thread/LIFO finish retains the active
storage so the caller can correct ordering and retry. An immutable construction
thread ID is checked before mutable scope state in foreign-thread finish and
borrowed-boundary requests. Other scope access and destruction retain the core's
same-thread contract. Copying and moving are forbidden.

`result()` records the constructor decision; it may remain `Admitted` after
finish. `admitted()` and `boundary()` describe current activity. A borrowed core
boundary lasts only until its owning application scope finishes or dies; replay
must finish first. This supports explicit child-to-parent final-Release handoff
without retaining a dead child scope. The caller must keep a failed out-of-order
scope alive. Refusal does not authorize skipping native application work,
inventing an HRESULT, or using a TLS bypass through active replay.

## Compiler boundary

**Compile only `application_admission_abi.cpp` with `-fno-exceptions`.** The file
rejects compilation when `__EXCEPTIONS` is enabled. The standalone build script
compiles the portable core and fixture separately with their ordinary exception
policy. There is no global exception-policy change and no production CMake
integration in this checkpoint.

The initial optional/RAII prototype passed the CPU witnesses but emitted MinGW
SJLJ registration before its guard and unregistration after restoration, including
on disabled paths. Trivial raw storage alone did not eliminate every compiler
bookend. The per-source option removes those calls without depending on a
particular unwinder's LastError behavior. The retained final object disassembly
has no SJLJ, personality or exception-table references. Disabled entry branches
past all calls and state instructions; the enabled paths capture before their
first call and restore after their last call.

The promise covers **ordinary return only**. Exceptions crossing this shell have
no supported recovery contract; the adapter does not promise state restoration
or admission cleanup during exceptional unwinding. A future enclosing hook may
still emit its own exception prologue/epilogue and needs separate emitted-code
and runtime verification. The fixture's exception-enabled caller can initialize
unwinder machinery before the first adapter entry, so these tests do not claim a
cold-process unwinder result.

## Verification and diagnostic timing

`python3 verification/probe/run_application_admission_abi.py` builds the separate
objects, rejects an incorrect exception-policy build, checks the adapter object,
and runs a CPU-only x86 fixture through CrossOver Preview. It does not create a
D3D device or launch the game. The final run passed **130 checks and 21 timing
samples**. State witnesses include live x87 payloads, nondefault controls/sticky
state, MXCSR, LastError, four-byte stack entry, waiting admission, refusals,
foreign-thread finish, explicit handoff, destructor finish, and preservation of
simulated native outgoing state and result. The runner requires exact case,
check and sample inventories and exactly one terminal result.

Each timing mode has one 10,000-iteration warmup and seven 100,000-iteration
samples. One measured entry includes adapter construction and destruction;
nested mode keeps one outer scope alive outside the timed loop. QPC boundaries and
route-count validation are outside the per-entry body; the timed loop includes
small requested/admitted counter accumulations in both versions. Medians from the retained
prototype and current reports are:

| Mode | Initial prototype | Current adapter |
| --- | ---: | ---: |
| Disabled | 63.773 ns | 2.270 ns |
| Enabled outer | 231.585 ns | 168.769 ns |
| Enabled nested | 217.066 ns | 161.577 ns |

These are diagnostic CPU timings in Preview, measured in fixed mode order in
separate same-session runs. They do not measure contested waits, actual D3D hook
cost, game performance, native Windows, or total frame cost across all entry
points. No aggregate hook budget is inferred from them.

Current evidence is `verification/results/application-admission-abi-summary.json`
and its raw report/object dump. The runner records matching source maps before
build/after build/after run, executable/object/report hashes, compiler version,
and observed Wine launcher stability; no runtime-version allowlist is used.
`application-admission-abi-initial-summary.json` and its report are explicitly
historical prototype evidence, with their own source maps. Local original source
copies and executable remain untracked for review; the current runner does not
pretend to rebuild that prototype. It recomputes both sets of medians directly
from the retained raw reports.

Independent review accepted the source/lifetime contract, emitted boundaries,
current and historical artifact provenance, and the stated limits. No production
entry coverage or live replay is claimed by this checkpoint.
