# Chase callback compiler boundary

2026-09-13. Independent review of the new firing observer exposed an existing
camera build issue: with MinGW's normal C++ exception policy, compiler-generated
SJLJ registration can precede `PreserveCpuState` construction, and unregistration
can follow its destruction. Neither helper is covered by our CPU-state or
LastError preservation. This is a correctness gap at a mid-function injection,
even when a particular CrossOver runtime happens to preserve the observed state.
It is not an explanation established for any reported gameplay symptom.

The camera has no throwing operation or exception handler of its own. CMake now
builds `chase_camera.cpp` with source-specific `-fno-exceptions`, as it already
does for the admission ABI adapter and now does for `chase_aim_trace.cpp`.
Other translation units keep their normal exception policy. SSE2 arithmetic,
four-byte incoming-stack realignment, the emitted register-saving trampoline
and the native displaced instructions are unchanged. This does not add recovery
from a native access violation or authorize exceptions to cross the injected
callback.

## Object inspection

The existing `build-ownership` camera object contains
`_Unwind_SjLj_Register` before `GetLastError`/`fnsave`, and
`_Unwind_SjLj_Unregister` after `SetLastError`. That object is evidence of the
compiler behavior, not a claim that its bytes are the current installed DLL.
The local disassembly is `/tmp/x3-chase-existing-object.txt`.

A new x86 object compiled with `-O2 -g -DNDEBUG -msse2 -mfpmath=sse
-mstackrealign -mincoming-stack-boundary=2 -fno-exceptions` has no SJLJ or
personality reference. Inspection of `x3m_chase_camera_enter` shows:

- Integer stack alignment and XMM zero/store instructions precede the first
  call, `GetLastError`; XMM registers are already saved by the generated stub.
- `fnsave`/`frstor` and `stmxcsr` save the caller's floating-point state before
  `fninit` and the local round-to-nearest, masked MXCSR load.
- Both timed and untimed paths reach the same final `frstor`/`ldmxcsr` and
  `SetLastError` restoration, followed only by an integer stack/register
  epilogue and return.

The exploratory object and disassembly are under `/tmp/x3-chase-abi-audit/`.
Source work continued after this inspection: final qualification must audit the
actual candidate objects and retained build flags, not reuse this exploratory
object as final provenance. Synthetic runtime preservation checks and native
Windows execution are distinct; native Windows remains untested.

## Performance scope

The correction removes per-callback exception registration/unregistration.
It adds no draw work, memory reads, locks or allocations. No timing improvement
or game-FPS benefit is claimed without measurement. The CPU-state saves and
restores remain required even though the camera's arithmetic uses SSE2, because
the interrupted engine may have live x87 state and its own MXCSR settings.

Independent source review approves this correction. A clean RelWithDebInfo
candidate now reproduces the same boundary audit for both the camera and aim
callback objects, with no exception-runtime symbols and the expected
save/initialize/restore instruction inventory. The 164 production input files
match before and after that build; its DLL is SHA-256
`2981bf032be8c7e91013e1de7f355d83fb49f4b2ba778b2b5917787f48a9297c`.
The records are under `/tmp/x3-chase-elevated-qualification-UwDzq7/`, including
`callback-object-audits.json` and the production source manifests.

Runtime qualification is complete for the consolidated candidate. The first two
fixture attempts stopped before any hook because late address reservation
conflicted with startup allocations; they establish no preservation result.
The corrected loader-owned synthetic PE mapping retains the production site
addresses and passes all six X3 cases / 124 checks, including actual trace-stub
CPU-state preservation. A separate X3 camera-math host and the exact DLL export
load checks also pass. These do not execute the live game camera handler or
verify native Windows. See [review 49](review-49-chase-aim-trace.md) and the
[runtime summary](../../verification/results/chase-elevated-runtime-summary.json).
The installed build remains the version at the top of `docs/status.md`.

## 2026-09-15 chase view restore (X3M_CHASE_VIEW_RESTORE) CPU boundary

Seven new sites from the run60 contract (`docs/reverse-engineering/chase-view-transition.md`,
"Implementation" under the run60 section): the shared optimized store seam
`4a3ffd` and six cancellation boundaries. Default off; `initialize()` claims
none of them unless `X3M_CHASE_VIEW_RESTORE=1` and the base transition set is
installed. Worktree branch rebased onto `24055db`; deep review passed the
seam, cancellation sites, install/rollback and fail-closed proofs and its four
should-fixes are applied (epoch cancellation resets the arm attempt; pending
admission by monitor ID only; kind7 deserialization cancel on the existing
mode-load observer; JSON evidence record). Not built or installed as a
candidate.

- Site verification against the installed EXE (SHA-256 `fdbf3418…`):
  `python3 verification/probe/verify_chase_restore_sites.py` → PASS: seven
  exact spans, whole instructions, no relative control, `4a4027` ends in
  `jmp 4a3ffd` with `4a3ff0` falling through, and no direct branch anywhere in
  `.text` targets a span interior. `verify_chase_transition_sites.py` still
  PASS for the nine diagnostic sites (source check scoped to its own table).
- Host: `PYTHONPATH=verification/probe python3 -m unittest
  verification.analysis.test_chase_transition
  verification.analysis.test_chase_transition_sites
  verification.analysis.test_chase_restore_sites
  verification.analysis.test_chase_camera.ChaseCameraLaunchOptions` → 24 tests
  OK, including the new portable restore-core host (32 checks: decode, proof
  order, bounded live stack, refusal subreasons) and the launcher option.
- X3 CPU fixture: `python3 verification/probe/build_chase_transition_cpu.py`
  (no Wine; audits `x3m_chase_transition_enter`, `x3m_chase_restore_enter` and
  `x3m_chase_lead_enter`: no exception-runtime symbols, fnsave/frstor/stmxcsr/
  fninit/ldmxcsr inventory, GetLastError first, SetLastError last), then
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  verification/probe/run_chase_transition_cpu.py` →
  `stubs=18 restore_stubs=7 checks=722 failures=0`, record
  `verification/results/chase-restore-cpu.json` (bottle X3, arm64, timings). Coverage through the actual
  emitted stubs and, for install/rollback, the production `restore_sites_install`
  on fixture-owned copies of the seven spans: GPR/flags/XMM/x87/MXCSR/LastError
  and four-byte stack preservation on the idle-skip and full paths; partial
  install (corrupted fourth span) rolls the first three back to original bytes;
  seven-site install, execution through the patched spans with displaced
  semantics intact, restore to original bytes, late-window refusal
  (`late_claim`, bytes untouched); option off claims nothing; exactly-once
  payload write (1→258) with the prefilter returning to idle; same-valued and
  direct-caller selections; foreign-monitor selections ignored, malformed
  context cancels; A→B→A player/controller stores; unknown global 8/9 store
  while pending; killed zero keeps, nonzero cancels; yield/re-entry by another
  task and by another thread; both task terminations (unrelated task keeps
  pending); VM construct/clear/load epoch cancellation; EH-adapter lock-free
  epoch bump observed at the next store; ten bad-tag/identity proofs, 65-cell
  overflow and five-byte misalignment refusing without a write; arbitrary and
  second destruction; wrong warp prefix; 600-update expiry; arm refusals and
  one identity walk per lifetime/mode re-entry; a secondary monitor whose
  variable11 is the player visited first while pending leaves the ticket
  untouched and the main monitor then consumes once; arm → EH epoch bump →
  admitted rear update re-arms → gate → consume once; kind7 deserialization
  (existing mode-load observer) clears pending/arm, advances the epoch and
  allows a later re-arm.
- Paired benchmark (same harness, not game FPS): the seam stub's idle prefilter
  skip costs 0.004 µs mean (best trial within noise, −0.017 µs; first run
  0.012 µs) against 0.53 µs for a full register-saving stub with a disabled
  callback (kinds 0–17). While armed the
  stub admits only five published operand addresses; while pending it also
  admits opcode-93 stores to global slots 8/9.
- Clean DLL: `cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
  -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j4`, then
  `python3 verification/probe/check_no_x87.py build/d3d9.dll` → PASS, 63 roots,
  225 reachable functions, 0 violations (clean `--clean-first` build on the
  rebased tree). Worktree DLL SHA-256
  `4886774ec62b61bb200988e121508f0c8aaf75528fc73105dd056e707cf925e0` (evidence
  only; the candidate owner rebuilds from the reviewed commit).
- Launcher: `./x3run --help` lists `--chase-view-restore`; `./x3run --camera
  chase --chase-view-restore --dry-run` prints `X3M_CHASE_VIEW_RESTORE=1`,
  without the flag `0`; the flag without `--camera chase` is a usage error.

Not verified: live game behaviour (gameplay acceptance is a separate user run)
and native Windows execution (documented Win32 only: VirtualQuery,
WriteProcessMemory, GetEnvironmentVariableW, SRW lock, atomics).

### 2026-09-16 run65 correction: cell16/cell17 consume predicate

Run 26 (`/tmp/x3-bottleX3-run65`) armed 3×, transferred 2×, reached the seam
2× and refused both with `refuse_ref_cell` (10): the contract's "variable11"
is the camera-priority constant 20, not the player ref (RE note, "Run65:
consume refusal on the ref cell"). The consume proof now tests cell16 == 0
(`refuse_monitor_number`, 22) and cell17 tag equal to global cell9's tag
(`refuse_ref_tag`, 21, tag reported) with payload == validated global cell9
(`refuse_ref_cell`, 10), in the same ordered position; source-cell and
live-stack checks stay after it. While pending every seam call logs one
`chase_view_restore_seam` line with cell0/cell1/cell16/cell17_tag/cell17,
source tag/payload, `prefix_ok` and `refusal` (no per-draw cost). The record
is captured under the SRW lock and emitted after release; the CPU fixture's
`log()` stub asserts no call arrives while the chase lock is held and that
the seam lines are emitted.

- `python3 verification/probe/build_chase_transition_cpu.py` then
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  verification/probe/run_chase_transition_cpu.py` →
  `stubs=18 restore_stubs=7 checks=739 failures=0`, record regenerated at
  `verification/results/chase-restore-cpu.json` (seam idle prefilter 0.007 µs
  mean, within noise; full stub 0.54 µs). New cases: cell17 tag 2 and tag 0
  refuse under `refuse_ref_tag`; cell17 payload != global9 refuses
  (`refuse_ref_cell`); cell16 == 1 refuses (`refuse_monitor_number`); cell11
  set to the player is never consulted; cell17 tag 8 (differing from global
  cell9's tag 1) refuses; a side monitor with unset cell17 (tag 0) issuing
  SelectMode(0) while armed and while pending leaves the ticket untouched and
  the main monitor then consumes once; the happy path consumes once. Synthetic monitor
  layout updated (41 variables, cell11 = 20, cell16 = 0, cell17 = player).
- Host: `test_chase_transition`, `test_chase_transition_sites`,
  `test_chase_restore_sites`, `test_chase_camera.ChaseCameraLaunchOptions` →
  24 tests OK (restore-core host 37 checks, including cell17 beyond the class
  variable count refusing);
  `verify_chase_restore_sites.py` → PASS.
- Clean DLL (`cmake --build build --clean-first -j4`) then
  `check_no_x87.py build/d3d9.dll` → PASS, 63 roots, 225 functions, 0
  violations; worktree DLL SHA-256
  `9569e4d0c7683f17250f4600df64913b8272d4ed214c99985eb4752e09ac3532` (evidence
  only).

Still unmeasured in game: the cell17 tag, the literal source cell and the
`edc91,16724,0` prefix; the next gate run's seam lines settle them.
