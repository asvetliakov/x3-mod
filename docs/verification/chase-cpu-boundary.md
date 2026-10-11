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
verify native Windows. See [review 49](../archive/review-49-chase-aim-trace.md) and the
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

### 2026-09-16 run68 correction: arm preconditions retry, refusal samples

Run 27 (`/tmp/x3-bottleX3-run68`, installed `46dc822`, four transits; grep
only): consumes were transits 1 and 3 (seam lines 2141468 and 2152278, both
`refusal=0`, followed by `kind=6 requested=258` on generations 2 and 4 and
updates at 258), so consume did restore and no later mode-1 writer exists.
Transits 2 and 4 reset because no arm existed at their destructors: the fresh
generation's first updates (events 29/30, 62/63) carry `view=0` with `ship`
set, the `view == ship` precondition refused (`arm_refusals` 1 and 2 exactly at
generations 2 and 4) and the recorded attempt blocked every later update
although `view == ship` held from events 33/66 on. All four destructor
prefixes were identical; no transfer predicate failed (`refusals` all zero).

Fix: connect/view/handle preconditions are re-checked on rear updates (mode
read plus up to three `field` reads per update, one engine_memory read each;
the registry walk only after connect/view pass), capped at 64 retries per
generation (`arm_retry_exhausted` records the attempt); only the identity
walk records the per-lifetime attempt. `arm_refusals` is now counted per
refused update, not per lifetime; `arm_refusal_reasons=` splits it.
Refusals are counted per reason (`arm_refusal_reasons=` in the state line)
and sampled once per reason per state as `chase_view_restore_arm_refused` and
`chase_view_restore_transfer_refused`, captured under the Guard and emitted
by `handle()` after release.

- `python3 verification/probe/build_chase_transition_cpu.py` then
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  verification/probe/run_chase_transition_cpu.py` →
  `stubs=18 restore_stubs=7 checks=759 failures=0`; record regenerated at
  `verification/results/chase-restore-cpu.json` (seam idle prefilter within
  noise, full stub 0.55 µs). New cases: arm → consume → new generation with
  `view=0` refused twice without recording an attempt → later update with
  `view == ship` re-arms on the new generation → next destructor transfers →
  second consume; connect precondition retried on the next update; identity
  refusal still recorded once; one arm-refusal sample per reason (identity,
  connect, view, no handle, CODE base, retry exhausted), one transfer-refusal
  sample per reason (repeat not re-sampled); missing registry handle refused
  per update; zero CODE base recorded once; 64-retry cap records the attempt
  once and a later mode re-entry resets the budget; no log call under the SRW
  lock. Arm-refused samples carry `readable=` (connect/ship/view field reads)
  and the armed flag. The "later mode-1 writer after
  consume" case was not added because the log shows no such writer.
- Host: `test_chase_transition`, `test_chase_transition_sites`,
  `test_chase_restore_sites`, `test_chase_camera.ChaseCameraLaunchOptions` →
  24 tests OK; `verify_chase_restore_sites.py` → PASS.
- Clean DLL (`cmake --build build --clean-first -j4`) then
  `check_no_x87.py build/d3d9.dll` → PASS, 64 roots, 230 functions, 0
  violations; worktree DLL SHA-256
  `05ebd272edcc24a4c470a78bbf9f3c61058d24c59768f4a32b9b69c265b0b5e9` (evidence
  only).

### 2026-09-25 docking path (`--chase-view-restore-dock`), source checkpoint

Design in [chase-view-restore-and-hud-anchor.md](../architecture/chase-view-restore-and-hud-anchor.md)
("2026-09-25: docking path"); spec in
[chase-view-docking.md](../reverse-engineering/chase-view-docking.md). No new
site or stub. The destructor and seam state transitions moved into
`destroy_step`/`consume_step` of `chase_transition_restore_core.h`. No Wine
run, no install, unflown.

- Host fixture `chase_transition_restore_host.cpp` →
  `chase restore host: 69 checks PASS`. Before the change it had 37 checks
  (static count). The new cases:
  - The dock chain with the option off refuses with reason 18, clears the arm
    and leaves no pending. The verdicts equal a verbatim copy of the
    pre-change gate on 16 chain shapes and warp values.
  - With the option on, the dock chain transfers `path=dock` and keys the
    pending ticket by thread, task, task ID and epoch. The dock seam writes
    258 once.
  - These refuse: warp 1 on the dock chain, warp 0 on the jump chain, a wrong
    return, `origin_flags=4`, a null root context. Another destructor caller
    clears the arm.
  - These refuse without writing: a wrong dock prefix, the jump prefix under
    a dock pending, warp 1 at the dock seam, a jump pending on a dock seam.
  - The jump chain still transfers and consumes, with `path=jump`, with the
    option on and off.
  - After a dock transfer, a jump chain cancels the pending ticket (one
    transfer in total, filter idle, no second pending), and the reverse
    order does the same.
  - A stale epoch at the dock seam refuses without writing, and
    `clear_all(cancel_epoch)` clears the dock pending once.
  - Review fixes: the pending epoch is read after the provenance walk, as in
    47d5e3d9. An epoch bump inside the walk stamps the new epoch, and the
    fixture checks this on both paths.
  - The option-off equivalence drives `destroy_step`/`consume_step` and the
    47d5e3d9 sequencing, with the verbatim pre-change `seam_consume_proof`
    and `transfer_proof` copied into the fixture, through every length-4
    sequence over a 27-event alphabet. The events are re-arm, 6 destructor
    shapes × 2 warp values, and 3 seam stacks × 2 warp values × 2 epochs,
    plus a not-writable seam and a failed write. Result: `sequences=531441
    mismatches=0 transfers=41007 consumed=1413`.
  - The accepted-transfer line is written once per path per session.
- Mutation witness
  `python3 verification/results/chase-view-docking/mutate_restore_host.py` →
  `mutations=8 caught=8`. The mutations: dock warp, option gate, the
  second-destruction cancel, a second write, the dock chain, the per-path
  consume prefix, the epoch read before the walk, and a dropped
  `reset_attempt` on refusal. On its own, the dropped `reset_attempt` gives
  360,846 mismatching sequences in the equivalence check.
- `PYTHONPATH=verification/probe python3 -m unittest verification.analysis.test_chase_transition`
  → 5 tests OK. `test_chase_camera` → 58 tests OK, including the new launcher
  case: the option requires `--chase-view-restore` and `--camera chase`, the
  environment variable is always explicit and a stale `1` becomes `0`, and
  `--vanilla` refuses. `test_chase_restore_sites` and
  `test_chase_transition_sites` → 12 tests OK.
- `cmake --build build` (fresh worktree configure, MinGW i686 RelWithDebInfo)
  → 0 warnings. `check_no_x87.py build/d3d9.dll` → PASS, 119 roots, 690
  functions, 0 violations. The worktree DLL is 56,882,055 B (after the review fixes), SHA-256
  `b6a5eb9f…a35c9d8e` (evidence only).
  `build_chase_transition_cpu.py` builds the Wine CPU fixture, and its audit
  shows `GetLastError` first and `SetLastError` last in both callbacks. The
  fixture was not run under Wine: no emitted stub changed.
- Dry run: the Run 84 A stand command (`--bottle X3 --dll-source
  build/d3d9.dll --direct --camera chase` plus `/tmp/x3-run84-candidate/stand.txt`)
  with `--chase-view-restore-dock` → exit 0 and `X3M_CHASE_VIEW_RESTORE_DOCK=1`.
  Without the option → `0`. The 183-variable environments differ only in
  that variable.
- Open: the dock consume prefix is inferred. The first flight settles it
  through the `chase_view_restore_seam … path=dock` line, and a mismatch
  refuses without writing. The docked-screen look and native Windows
  behaviour are unverified.

### 2026-09-25 docking path dropped

Removed by user decision after Run 86 A launch 3 (run335): after undocking the
HUD selection brackets for stations and ships disappeared (cause not
investigated), and a save loaded while docked restores the first-person view
anyway. In run335 the dock transfer was accepted and consumed at `f0c4b` with
`cell0=258` (two seam rows), as designed. Source reverted to pre-40cd3023
(`git revert` of 40cd3023, refactor included; `mutate_restore_host.py` deleted):
the docking chain refuses with reason 18, the jump path is unchanged, and
`--chase-view-restore-dock` is an unknown launcher option. Details in
[chase-view-docking.md](../reverse-engineering/chase-view-docking.md), "Dropped".

## 2026-10-12 chase view restore, engine path (`SelectMode(258)` from the input routine's entry)

Source checkpoint for the ratified design
[chase-view-restore-engine-state.md](../architecture/chase-view-restore-engine-state.md)
section 4 with its three amendments (cockpit-replacement trigger, any ship or
sector; direct by-name `SelectMode(258)` on the monitor object captured from
the VM frame instead of key replay). The script-seam path (seven sites, the
`0xf0c4b` payload write, `chase_view_restore_seam`/`transfer_refused` rows)
is removed. Built on the WSL2 host; the Wine fixtures were built, not run
(no Wine on this host). Nothing installed.

- Static invariants (Ghidra 12.1.4, read-only on the analysed `X3AP.exe`;
  queries kept in `verification/results/native-chase-view-restore-2026-10-11/ghidra_impl_queries_*`):
  `0x0049f570` is cdecl with seven dwords, caller pops `0x1c`; its third
  argument is the lookup key for `0x0049f1e0` (`EAX = [ESP+0x44]` at
  `0x0049f59c`; non-negative → static class table `0x004b06f0`, negative →
  dynamic object table `vm+0x12d0` keyed `~id`), the method is found by a
  content `strcmp` (`0x004b0740`) over `[desc+0x2c]`/`[desc+0x28]` with the
  parent chain `[desc+0x14]`, `task+0x3c` receives the resolved context and
  `task+0x30` the first argument, and `AL = 0` is returned when the object or
  the method is missing (`0x0049f66c`); the interpreter's `DYNCALL` (opcode
  0x85) sets `task+0x3c` from the same resolver. The input routine
  `0x00410100` has exactly two callers (`0x00404363`, `0x00404379` in
  `0x00404280`, which skips it under `[game+0x4a0] & 1`), a body of 13 ranges
  with its last byte at `0x0041224f`, and 13 calls to `0x0049f570`; the
  key-up sequence `0x00410ac3..0x00410b61` is as the design note states
  (listed, unused after amendment 3). The site prologues match the installed
  image (`0x00410100` `55 8b ec 83 e4 f0`, `0x0049ea80`, `0x004a0880`,
  `0x0052f298`).
- Script programs: `check_selectmode.py` over the two extracted assets
  (STRG decode and 16-byte CLAS method rows per the loader `0x0049d030`):
  stock `addon/04.cat` (SHA-256 `ed5786a0…`, the RE note's asset) and Mayhem
  `addon/07.cat` (`2c3101a8…`) each carry exactly one method row named
  `SelectMode`, argument count 1, locals 10 (CODE entries `0xf07cc` and
  `0x106b1f`). Output in `check_selectmode_out.txt`.
- Site verification: `python3 verification/probe/verify_chase_restore_sites.py
  --exe /mnt/c/X3/X3AP.exe` → PASS (four exact spans, whole instructions, no
  relative control, no `.text` branch into a span interior, input routine
  extent `[0x00410100, 0x00412250)` gap-free, ends in `ret`, no jump leaves it,
  13 script-call returns inside it); `verify_chase_restore_sites_out.txt`.
- Host: `cd verification/analysis && PYTHONPATH=../probe:../.. python3 -m
  unittest test_chase_restore_sites test_chase_transition test_config_schema
  test_launcher_defaults test_chase_camera test_chase_transition_sites` →
  103 tests OK. The restore core host (`chase_transition_restore_host.cpp`,
  212 checks) covers: the acceptance path (arm → free `0x0042d402` →
  construct `0x0042d397` → monitor captured at the store → two due frames →
  one call with the captured object → 258 inside the call → `restored`,
  cleared before the call); same sector with a changed `+0x94` → restored;
  different sector, same ship → restored; 36 unbound updates then bound (run78
  shape) → due only after binding; load-chain constructor → `lifetime`; kind
  7 → `load`; EH/VM epoch → `epoch`; mode left before due and a mode change
  between due and the entry → `mode_left`; no captured monitor, a monitor
  captured for another cockpit, unreadable frames → `no_monitor` / nothing
  captured; `no_effect` retries spaced ≥ 60 updates up to the cap, retries 0;
  `call_refused`; `other_mode` terminal; stores on other cockpits or threads
  ignored; 600-update expiry (armed tickets do not expire); controller change
  → `identity`, unregistered ship and no control wait; deleting destructor,
  another cockpit's free, second destruction while unbound, bound cockpit
  destroyed, destruction during the call; the seven arm refusal reasons with
  once-per-reason samples; pause bit and controlled-object skips; side
  cockpits never bind.
- Build: `cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake
  -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build` → 0 warnings;
  `python3 verification/probe/check_no_x87.py build/d3d9.dll` → PASS, 925
  reachable functions, 0 violations. Worktree DLL SHA-256 `7132d807…`
  (57,785,068 B, evidence only). `python3 tools/config/generate.py --check`
  clean (262 settings, 112 in the template; `chase_view_restore_retries`
  added).
- CPU fixture: `python3 verification/probe/build_chase_transition_cpu.py`
  builds; the audit of `x3m_chase_transition_enter`, `x3m_chase_restore_enter`
  and the new `x3m_chase_inject_enter` shows the same boundary inventory
  (fnsave/frstor/stmxcsr 1, fninit/ldmxcsr 2), `GetLastError` first,
  `SetLastError` last, no exception-runtime symbols. Not run (no Wine on this
  host): the scenarios through the emitted stubs (site-I prefilter idle and
  published paths, the mock `0x0049f570` recording its seven arguments and
  the x87 CW/MXCSR it was entered with, LastError across the call, outcomes,
  install/rollback of the four synthetic spans, late window, option off,
  retries parsing) await the next X3-bottle run:
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  verification/probe/run_chase_transition_cpu.py`.
- Not verified: live behaviour (the user's native run with `--chase-view-restore`
  after one gate jump and one jumpdrive decides `result=restored` versus
  `no_effect`/`call_refused`/`no_monitor`), `task+0x30` semantics beyond
  "the first argument" (passed as the monitor id, as the engine's own callers
  pass their subject object), native Windows execution.
- Review fixes (2026-10-12, before the candidate): monitor capture keyed by
  (address, generation) so a reused cockpit address restores (host case 17,
  reviewer's repro `reuse_repro.cpp` under the results directory); only a
  negative dynamic object id accepted from the VM frame; epoch checked at the
  injection entry; a call that never reports back expires as
  `cancel=call_unwound` (case 18); refusals on other cockpits no longer
  cancel the arm (case 20); the VM frame is read only past the arm; re-arm
  frames skip the walks (`rearm_frames`). Restore host 236 checks; the six
  focused modules rerun, the DLL rebuilt and audited (figures in the report).
