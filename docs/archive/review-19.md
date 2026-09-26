# Review 19: in-process sampling profiler and the three directory-enumeration hooks

Independent review of the uncommitted tree on top of `4c2efc7`, limited to
`src/proxy/sampling_profiler.{h,cpp}`, the `FindFirstFileA`/`FindNextFileA`/
`FindClose` hooks in `loading_trace.{h,cpp}`, the profiler init/shutdown lines
of `capture.cpp`, `CMakeLists.txt`, the `--profile*` options of `manage.py`,
the profiler fixture, build script, runner, summarizer, Ghidra script and unit
tests, the loading-trace fixture and runner, and the three documents. No game
was launched; one Wine runner at a time; result files were queried with
scripts.

## Checklist

1. **Suspended window** - `capture()` (`sampling_profiler.cpp:127-165`) runs
   only `GetThreadContext(CONTROL|INTEGER)` and plain reads: the target's TEB
   (`TebBaseAddress` from `NtQueryInformationThread(ThreadBasicInformation)`
   on that thread's own handle, 28-byte layout checked; `StackBase/StackLimit`
   read fresh at offsets 4/8, validated `limit < base`, span <= 64 MB, dword
   aligned, `Esp` inside), the stack in `[Esp, StackBase)` only (chain frames
   need `frame >= Esp` and `frame+8 <= base`, the scan stops at
   `min(base, Esp+4096)`), and module code bytes `[addr-7, addr-1]` with
   `addr >= range.begin+8`. The EBP walk requires `next > frame` (no cycle),
   at most 32 frames. `ResumeThread` follows `capture()` unconditionally; no
   allocation, log, lock or other Win32 call in the window (`log` would take
   the proxy's capture `recursive_mutex`, `capture.cpp:1191`). Under the new
   WoW64 mode `ThreadBasicInformation` yields the 32-bit TEB, which the
   fixture confirms (stack known, 100 % frame attribution).
2. **Thread lifetime** - the sampler skips its own tid; a thread that exits
   between refresh and tick gets `SuspendThread == -1`
   (`STATUS_THREAD_IS_TERMINATING` in Wine) and is skipped; unseen threads are
   closed and retired at refresh and their slot is freed after the next
   report; 32 slots ranked init thread first then creation time; at most 256
   candidates; overflow counted as `threads_unsampled`. Finding 2 below.
3. **Module refresh race** - decided: pin (finding 1).
4. **Tables** - open addressing, 64-probe bound, `dropped` accounted per
   table and merged into the cumulative counters; static storage only; keys
   carry `slot+1` (never 0), leaf `{rva, module|0xffff, slot}`, frame
   `{rva, slot}`, pair `{rva, caller, slot}`; `rank()` runs on the sampler
   thread after every resume and holds no lock (each `log` line takes and
   releases the capture mutex). Finding 4 (cumulative slot aliasing).
5. **Timing** - tick cost is measured inside `tick()`; refresh and report
   costs are separate (`refresh_us`, `report_us`); the sleep is
   `max(interval - cost, cost)` so the duty cycle is <= 1/2 (the report cost
   of one iteration every 5 s is not in that sleep: negligible); interval and
   report period are clamped (`env_number`), `manage.py` validates
   100..1000000; `QueryPerformanceCounter`, the `loading_trace::tick()` clock.
6. **Shutdown** - `release_device` computes `last_device_destroyed` inside
   the `HookGuard` scope and calls `shutdown()` after it (`capture.cpp:397-
   399`); `shutdown()` sets the flag, signals the event and joins with a 10 s
   wait before touching the tables; an unjoined sampler leaves its tables
   alone and the proxy stays pinned (`GET_MODULE_HANDLE_EX_FLAG_PIN` at
   initialize); `DllMain` untouched; exit without device release documented.
7. **Log volume** - per delta report 1 + <= 32 thread + 48 + 48 + 32 + 1
   lines; cumulative every 12th report or at shutdown 1 + <= 32 + 256 + 256 +
   256; `profile_thread_seen`/`profile_module` once per thread slot/module;
   nothing per sample.
8. **Find hooks** - `static_assert` against the SDK signatures and
   `import_count == Operation::MeshPointReps` (19); `Span` saves the caller's
   LastError in its constructor and `finish` restores the callee's, identical
   to the other hooks; `ERROR_NO_MORE_FILES` (FindNext) and
   `ERROR_FILE_NOT_FOUND`/`ERROR_NO_MORE_FILES` (FindFirst) are `ambiguous`,
   other failures `failed`; `WIN32_FIND_DATAA` is forwarded, never read;
   `hooks=` is computed from the table; the documents say 19 and 85 (finding 5).
9. **manage.py** - `--profile` sets `X3M_PROFILE=1` (else `0`) and nothing
   else; interval validated; both appear in the `--dry-run` JSON.
10. **Cost when off** - one `GetEnvironmentVariableW` in `initialize()`, one
    two-bool test in `shutdown()`; the static tables live in `.bss` (DLL
    `.bss` 5.2 MB, demand-zero, untouched when off).

## Findings and fixes

1. **Medium, fixed** - `call_precedes` read code bytes of a module whose
   executable ranges came from a `VirtualQuery` up to one second old; a
   module unloaded in between would fault the sampler with a thread
   suspended (the document listed this as a limit). Now each module is
   pinned at first sight (`GetModuleHandleExW(FROM_ADDRESS|PIN)`, result
   checked against the base, `sampling_profiler.cpp:216-224`); a module that
   cannot be pinned is logged `pinned=0` and never enters the range table;
   ranges are also cut at the pinned `AllocationBase` (`:233`) so a stale
   Toolhelp size cannot annex an adjacent mapping. Zero tick cost, no
   exception machinery; the mapping side effect is documented as a
   diagnostics-only behavior. Fixture: 5/5 modules `pinned=1`.
2. **Low, fixed** - `refresh_threads` iterated all 256 `Candidate` entries
   instead of `candidate_count`, calling `OpenThread`/`GetThreadTimes` on
   uninitialized tids (up to ~250 wasted server round trips per refresh);
   bounded and zero-initialized (`:262,278-282`).
3. **Low, fixed** - a failed `CreateToolhelp32Snapshot` (transient
   `ERROR_BAD_LENGTH` on Windows) marked every module unloaded and retired
   every thread (32 handles closed and reopened, per-thread totals reset,
   fresh slots consumed). Both refreshes now keep the previous tables and
   retry at the next refresh (`:199`, `:265`).
4. **Low, documented** - the cumulative tables key by slot, so a slot reused
   by a later thread merges two threads there; deltas and per-thread lines
   are clean and the analysis sums deltas. Stated in the document's limits.
5. **Documentation, fixed** - `loading-performance.md:127` still said the
   ABI fixture "completed 75 checks"; now 85 (75 before the Find hooks).
   Line 173 is the historical mesh-expansion statement and stays.
6. **Low, fixed (process)** - `run_sampling_profiler.py` and
   `run_loading_trace.py` carried their own game checks (`ps` regexes; the
   loading-trace one matched only `comm`); both now use
   `game_guard.game_running()` from review 18, keeping their messages.

Observations, not changed: the profiler's `log` lines contend with hooks for
the capture mutex (a few hundred lines per 5 s, microseconds each; visible in
`LockWait` only with the profiler on); `X3M_PROFILE_INTERVAL_US` is exported by
`manage.py` even without `--profile` (inert); the per-refresh cost is dominated
by the two Toolhelp snapshots (~24 ms each under Wine), not by the profiler.

## Results after the fixes

| Suite | Result |
| --- | --- |
| `run_sampling_profiler.py` | PASS: 22/22 checks; A leaf in `spin_a` 938/939, pair 939/939; B leaf outside exe 940/940, frame `wait_b` 940/940; C leaf `fpo_leaf` 939/939, scan pair 939/939; 940 ticks, 2,536 samples, 0 dropped, tick mean 558 us, max 9.14 ms, refresh 72.7 ms total, reports 2.55 ms; overhead A -10.5 %, C -5.4 %, main -0.9 %; stop 7.1 ms, handles 0 -> 0, threads 1 -> 1; off run silent; 5 modules `pinned=1`, 0 `pinned=0` |
| `run_loading_trace.py` | PASS: `loading-trace` 85 checks, `loading-mesh` 123 checks, both exit 0 |
| `python3 -m unittest discover -s verification/analysis` | 503 tests OK |
| `cmake --build build` | OK (RelWithDebInfo, `-Wall -Wextra -Werror`) |
| `check_no_x87.py build/d3d9.dll` | PASS: 125 reachable functions, 0 violations |

Final `build/d3d9.dll` SHA-256:
`4380a720be5d2e786b502d338e507af750515654177ba4174cf9b346975266ca`.

Verdict: go for the checkpoint commit of the sampling profiler and the three
directory-enumeration hooks. The first game evidence is the next user-run
loading session with `--telemetry --profile --mesh-cache`; check its
`profile_report` lines for `threads_unsampled`, `dropped` and the achieved
`ticks`/`elapsed_us` rate before reading the attribution.
