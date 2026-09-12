# In-process sampling profiler (`X3M_PROFILE=1`)

`src/proxy/sampling_profiler.{h,cpp}` is an opt-in CPU sampling profiler inside
the d3d9 proxy. It exists to attribute the loading time that no API hook sees
(iteration-08: ~21 s of the menu load, ~73 s of the save load, ~8 s per sector
change; see [iteration08-loading.md](../reverse-engineering/iteration08-loading.md)).
It is diagnostics only: nothing about rendering or loading changes, and with the
switch unset there is no thread, no handle, no allocation and no log line.

Verified so far by the synthetic Wine fixture below. **No game was launched**;
game-session numbers will come from the next user-run loading session
(`python3 tools/manage.py launch --direct --telemetry --profile --mesh-cache`).

## What it measures

One sampler thread (`THREAD_PRIORITY_ABOVE_NORMAL`) wakes every
`X3M_PROFILE_INTERVAL_US` microseconds (default 2000) and, for each discovered
application thread in turn: `SuspendThread`, `GetThreadContext(CONTROL|INTEGER)`,
copy `Eip/Esp/Ebp`, read `StackBase/StackLimit` from that thread's TEB, walk the
EBP frame chain (strictly increasing frames inside the stack range, return
address inside a loaded module's executable range, at most 32 frames), scan at
most 1,024 dwords of stack above `Esp` for values that lie in an executable
range and whose preceding bytes decode as a call (`E8 rel32`, `FF D0..D7`,
`FF 10..17`, `FF 50..57 disp8`, `FF 14 sib`, `FF 54 sib disp8`, `FF 90..97
disp32`, `FF 15 disp32`, `FF 94 sib disp32`), then `ResumeThread`. Chain frames
and scan hits are merged by stack slot: a scan hit below the chain's next slot
is a frame the chain skipped (frame-pointer-omitting code).

After the resume, the sample is aggregated into fixed-size open-addressing
tables (no allocation after start; 64 probes, then the sample is counted as
`dropped`):

| Table | Key | Meaning |
| --- | --- | --- |
| leaf (65,536) | thread slot, module index, exact RVA | where the thread was executing |
| frame (65,536) | thread slot, RVA of the first frame in the main executable | attributes DLL/wait time to the calling engine function; the leaf itself when it is engine code; RVA 0 = no main-module frame found |
| pair (16,384) | thread slot, that frame, the next main-module frame | caller of the attributed engine function |
| per thread | slot | sample count and a leaf-module histogram: `x3ap`, `ntdll`, `wine` (modules under the Windows directory), `d3dx`, `zlib`, `xml`, `proxy`, `other` |

RVAs are exact instruction addresses relative to the module base; frame/pair
RVAs are return addresses (the instruction after the call).
`tools/analysis/X3ProfileSymbols.java` maps them to Ghidra functions.

Thread discovery: a `TH32CS_SNAPTHREAD` snapshot refreshed every second; handles
opened with `THREAD_SUSPEND_RESUME|THREAD_GET_CONTEXT|THREAD_QUERY_INFORMATION`;
at most 32 slots, ranked by the proxy-initializing thread first, then creation
time (`GetThreadTimes`). A thread that disappears keeps its slot until the next
report so a slot never mixes two threads inside one delta block; new threads
beyond 32 are counted as `threads_unsampled`. The module table
(`TH32CS_SNAPMODULE`, at most 128) is refreshed at the same cadence. Each module
is pinned at first sight (`GetModuleHandleExW` with
`GET_MODULE_HANDLE_EX_FLAG_PIN`, review 19) because the tick reads its code
bytes while a thread is suspended; executable ranges then come from
`VirtualQuery` over the live mapping inside that pinned allocation, never from
headers. A module that cannot be pinned (already unloading) is logged with
`pinned=0` and never read. A failed Toolhelp snapshot (transient on Windows)
keeps the previous module and thread tables instead of retiring everything.

## Log schema

All timestamps are the `QueryPerformanceCounter` clock that `loading_metric` and
`telemetry_summary` use, so reports align with the loading gaps. Reports are
written by the sampler thread through the proxy log writer, never while any
thread is suspended.

```
profile_start schema=1 enabled=1 qpc=… frequency=… interval_us=… report_s=… sampler_tid=… init_tid=… main_base=… proxy_base=… …
profile_module index=… name=… kind=… base=… size=… text_rva=… text_size=… ranges=… pinned=1   (once per module; `pinned=0` when the pin failed, `unloaded=1` if it goes away)
profile_thread_seen slot=… tid=… teb=… start_module=… start_rva=… creation=…           (once per discovered thread)
profile_report scope=delta qpc=… frequency=… since_start_us=… elapsed_us=… interval_us=… samples=… threads=… threads_unsampled=… modules=… ticks=… tick_us_mean=… tick_us_max=… refresh_us=… dropped=… table_used=…
profile_thread scope=delta qpc=… slot=… tid=… samples=… total=… leaf_x3ap=… leaf_ntdll=… leaf_wine=… leaf_d3dx=… leaf_zlib=… leaf_xml=… leaf_proxy=… leaf_other=… stack_unknown=… suspend_failures=… context_failures=… start_module=… start_rva=… retired=…
profile_leaf scope=delta qpc=… slot=… module=… name=… rva=… count=…        (top 48)
profile_frame scope=delta qpc=… slot=… rva=… count=…                       (top 48)
profile_pair scope=delta qpc=… slot=… rva=… caller=… count=…               (top 32)
profile_report_end qpc=… report_us=…
```

A delta report is emitted every `X3M_PROFILE_REPORT_S` seconds (default 5) and
its tables are then cleared and merged into cumulative tables. Every twelfth
report, and at the quiescent shutdown, a `scope=cumulative` block follows with
top 256 rows per table (`final=1` at shutdown). `tick_us_*` is the sampler's own
per-tick wall cost (suspend, context, walk, resume and aggregation of every
thread); `refresh_us` and `report_us` are measured separately and excluded from
it. Because the log is streamed and the periodic deltas are complete, the
analysis sums deltas; the cumulative block is a convenience for a run that ends
without the quiescent path.

Lifecycle in the proxy: `initialize()` runs in `initialize_log` after the
telemetry and loading-trace setup (outside the loader lock, on the thread that
first entered the proxy). `shutdown()` runs when the last D3D device is
released, after the capture mutex is dropped: it stops the thread (bounded
wait), writes the final cumulative report and closes every handle. A process
that exits without releasing its device simply leaves the periodic deltas in the
log (`ExitProcess` terminates the sampler thread; the proxy's static teardown
takes no lock the sampler could be holding). `DllMain` does nothing; the proxy
pins itself while the sampler runs.

## Safety rules

* Inside the suspended window the sampler executes only `GetThreadContext`
  and plain reads of memory whose mapping was verified in the refresh step:
  the target's own committed stack between its TEB `StackLimit` and
  `StackBase` (both read fresh: fibers switch them), and the executable
  ranges of pinned modules recorded with `VirtualQuery`, so nothing it reads
  can be unmapped between a refresh and a tick. No heap, no log, no CRT I/O,
  no other Wine or Win32 call, so a thread suspended while holding the heap,
  loader or log lock cannot deadlock the sampler. The proxy log writer
  serializes on the proxy's capture mutex, so `log` is equally off limits in
  the window.
* `ResumeThread` runs on every path, including a failed `GetThreadContext`.
* Aggregation, module/thread refresh and reporting happen with every thread
  running. The tables belong to the sampler thread; other threads only read
  atomic counters (`status()`).
* The sampler skips itself; the sleep after each tick is never shorter than
  the tick just cost, capping the sampler's duty cycle at one half. When the
  per-tick cost exceeds the interval the effective sampling rate drops rather
  than the suspended fraction rising; `ticks`, `samples` and `elapsed_us` in
  each report give the achieved rate.
* Off by default. `X3M_PROFILE` must be exactly `1`; interval and report period
  are clamped to 100..1,000,000 µs and 1..3600 s.
* `shutdown()` must not be called under a proxy lock: the final report takes
  the log lock, and the sampler may be blocked on it.

## Limits

* **Thread-context accuracy under Wine/Rosetta.** Contexts come from Wine's
  `NtGetContextThread` for a signal-suspended thread (under Rosetta 2 on Apple
  silicon). A thread blocked in a Wine syscall reports the 32-bit context saved
  at the syscall thunk, so its leaf is `ntdll.dll` and the engine attribution
  comes from the frame chain or scan. Sample timing is subject to signal
  delivery latency; per-tick cost is Wine-server bound (three round trips per
  thread) and grows linearly with the thread count.
* **Frame-pointer-omitting functions.** The EBP chain skips or stops at them.
  The scan recovers their return addresses, but a stale return address left
  above `Esp` by an earlier deeper call can be mis-read as live; the
  call-opcode check filters non-return values, not stale ones. Treat pair
  counts as evidence to confirm with Ghidra call graphs, not as exact call
  counts. The chain is also stopped by any frame outside the stack range or
  whose return address is outside a known executable range.
* **Table truncation.** Per-block tables carry the top 48/48/32 rows; summed
  table counts over a window are lower bounds. Per-thread histograms and
  sample totals are exact.
* **Pinned modules.** While the profiler runs, a module the application
  frees stays mapped (its `DllMain` detach still runs at the application's
  last `FreeLibrary`; only the mapping is kept). Diagnostics only; X3AP does
  not unload modules during loading.
* **Thread churn.** A thread that exits between a refresh and the tick counts
  one `suspend_failures`; its slot is reused after the next report. Threads
  created and destroyed within one second may never be seen. The cumulative
  tables key by slot, so a reused slot merges its successive threads there;
  the delta blocks and the per-thread lines never mix threads, and the
  analysis sums deltas.
* **Cost.** Sampling perturbs the game: each thread loses roughly the
  suspend/context/resume round trip per tick. The fixture measured 0.7 %–5.8 %
  throughput loss for spinning threads at 2 ms with four threads; a game
  session with more threads will see a lower effective rate rather than more
  loss per thread. `tick_us_mean/max`, `refresh_us` and `report_us` are in
  every report so the cost is separated from game time.

## Synthetic verification (Wine)

`verification/probe/run_sampling_profiler.py` builds
`verification/probe/build_sampling_profiler.sh` (production object compiled
with the DLL's flags and `-Werror`) and runs the fixture twice under CrossOver
Preview's Steam bottle (`X3M_PROFILE=0` then `1`, report period 1 s, interval
2 ms). The fixture: thread A spins for 3 s in `spin_a` (EBP frame) called from
`caller_a`; thread B waits on an event in `wait_b`; thread C spins in
`fpo_leaf` called from `fpo_spin`, both built with `-fomit-frame-pointer` and
no 64-bit values (this MinGW target keeps a frame for those); the main thread
allocates/frees, writes a file and logs throughout. Provenance: the runner
checks with objdump that `spin_a`/`wait_b` set up EBP frames and
`fpo_leaf`/`fpo_spin` do not.

Result 2026-09-12 (`verification/results/sampling-profiler.txt`, `-summary.json`,
raw outputs `sampling-profiler-{on,off}-fixture.txt`): 22/22 checks.

| Check | Result |
| --- | --- |
| A leaf in `spin_a` / pair (`spin_a`, `caller_a`) | 918/918 = 100 % / 100 % |
| B leaf outside the exe (all `ntdll`) / frame in `wait_b` | 919/919 / 919/919 |
| C leaf in `fpo_leaf` / pair (`fpo_leaf`, `fpo_spin`) via the scan | 918/918 / 918/918 |
| Sampler cost, 4 threads | 919 ticks, 2,476 samples; tick mean 605 µs, max 8.73 ms; refresh 81.9 ms total (3 refreshes, Toolhelp snapshots); reports 2.6 ms total |
| Throughput with sampler on vs off | A −0.7 %, C −5.8 %, main alloc/write loop −1.5 %; wall 3,046 vs 3,043 ms |
| Deadlock / teardown | main thread logged and allocated throughout; stop 9.9 ms; handles 0 → 0, threads 1 → 1 after shutdown; the off run emitted no `profile_*` line |
| Drops | 0; tables used 26/15/15 |

The per-thread cost (~150 µs per suspended thread per tick) is Wine's server
round trips, so at 2 ms the four-thread fixture ran at a 30 % duty cycle. In
the game the sampler will stretch its interval to keep the duty cycle at or
below one half; expect roughly 100–250 samples per second per thread rather
than 500. `tools/analysis/summarize_profile.py` reports the achieved rate.

`tools/analysis/X3ProfileSymbols.java` was checked once against the research
project (`/tmp/x3-ghidra-research`, program `X3AP.exe`): `0xbb470`/`0xbb480` →
`FUN_004bb470` (1,684 bytes), `0xe8e10`/`0xe8e25` → `FUN_004e8e10`,
`0xe7590` → `FUN_004e7590`, and RVA 0 → `null`.

## Reading a game session

```
python3 tools/analysis/summarize_profile.py <session.log> --from-s 7.0 --to-s 38.0 \
    --window save=50.3:158.1 --rva-list /tmp/rvas.txt --output /tmp/profile.json
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  /opt/homebrew/opt/ghidra/libexec/support/analyzeHeadless /tmp/x3-ghidra-research X3Render \
  -process X3AP.exe -readOnly -noanalysis -scriptPath tools/analysis \
  -postScript X3ProfileSymbols.java /tmp/rvas.txt /tmp/symbols.json
python3 tools/analysis/summarize_profile.py <session.log> --from-s 7.0 --to-s 38.0 --symbols /tmp/symbols.json
```

Windows are seconds after proxy initialization, the same axis as
`analyze_iteration08_loading.py`'s gap bounds; a delta block is attributed to a
window by overlap of its report interval. Unit tests:
`verification/analysis/test_profile_summary.py` (8 synthetic tests).

### Loading attribution pipeline

`tools/analysis/analyze_loading_profile.py` runs the whole chain on one log and
writes a report that sets hooked time and sampled attribution side by side for
every presentation gap over 2 s and every report stall inside it. For the next
user-run session (`python3 tools/manage.py launch --direct --telemetry --profile
--mesh-cache`, log under the session directory it prints):

```
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home \
  python3 tools/analysis/analyze_loading_profile.py <session.log> \
    --output verification/results/loading-profile-<label>/ --ghidra
```

The log is streamed once (262 MB in 1.6 s without Ghidra). `--ghidra` collects
every main-module RVA that appears in a gap's leaf/frame/pair tables into
`<dir>/rvas.txt`, runs `X3ProfileSymbols.java` headless on
`/tmp/x3-ghidra-research` (`X3Render`, program `X3AP.exe`, read-only, ~5 s) and
writes `<dir>/symbols.json`; `--symbols <json>` reuses an earlier run instead,
and without either the tables stay at RVA level. `--ghidra-project`,
`--ghidra-name` and `--ghidra-program` override the project location.

What the report contains, per gap and per stall:

* gap detection, phase label and hooked accounting exactly as
  `analyze_iteration08_loading.py` computes them (labels are mechanical: a
  `gzread`/successful `gzopen` inside the gap is a **save load**, ≥500
  `GenerateAdjacency` calls with ≥200 MB of 2D texture-helper input is a
  **menu load**, ≥100 adjacency calls after an earlier labelled phase is a
  **sector change**, anything else is **unlabelled**; the evidence line
  states the counts used);
* header numbers: interval, hooked exclusive seconds, unexplained seconds,
  delta blocks (inside/straddling), samples, covered seconds, samples/s,
  ticks/s per thread, drops, sampler tick mean/max and busy share;
* the hooked operations table and the per-thread leaf-module split;
* the **top functions** table — leaf samples aggregated by *containing
  function* (self), frame samples by function (inclusive-by-frame), the busiest
  slot's share, the role label from `tools/analysis/x3ap_function_labels.json`
  (addresses and short roles copied from the reverse-engineering notes) and,
  when the function is a documented caller of a hooked import, that import's
  calls and inclusive seconds in the same interval;
* top caller pairs with both ends symbolized and labelled;
* a purely mechanical candidates paragraph (shares, known/unknown routine).

`loading-profile.json` additionally holds the top 40 leaf RVAs, frames and
pairs per interval. A log without `profile_*` lines (or a gap no delta block
overlaps) renders a **No profile data** note and the hooked half only, so the
same command works on the iteration-08 logs. Unit tests:
`verification/analysis/test_loading_profile.py` (11 synthetic tests: profile
windows inside a gap, function aggregation, a block straddling the gap end,
stall sub-intervals, missing profile lines, label rules, the Ghidra command).
