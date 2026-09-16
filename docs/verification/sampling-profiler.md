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

With `X3M_PROFILE_RAW=1` (`--profile-raw`, load hang witness), at most once per
thread per report period and only after the thread was resumed:

```
profile_raw slot=… tid=… eip=… esp=… ebp=… cs=… context_flags=… stack_known=… dwords=…   (leaf outside every pinned module)
profile_raw_stack slot=… tid=… esp=… values=0x…@<module index>+0x<rva> 0x… …           (32 dwords from Esp inside the TEB stack range)
profile_raw_failure slot=… tid=… call=SuspendThread|GetThreadContext error=… [context_flags=…]
```

`profile_thread` then also carries `suspend_failures_delta` and
`context_failures_delta` for the report period. The dwords are copied inside the
suspended window under the existing stack-range rule; classification and the
log lines happen after `ResumeThread`. `profile_start` reports `raw=`,
`raw_dwords=` and `periodic_s=` (the optional 2 s callback the game-phase audio
witnesses register through `set_periodic`; it runs between ticks, never inside
the window).

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

## Frame timing diagnostic (`X3M_FRAME_TIMING=1`)

The profiler answers *which code* is running; this companion option answers
*which frames* were slow. `frame_end` is written only every 300 frames, so a
slow window inside that cadence cannot be located. `--frame-timing` (launcher
`tools/manage.py`, requires `--telemetry`; `X3M_FRAME_TIMING=1`) collects one
sample per Present into fixed 300-entry arrays (`src/proxy/frame_timing.h`,
no allocation after attach) and reduces them at the window boundary with
`std::nth_element`. Off, the cost is one branch on a process-global bool per
frame and per draw. Per window one line, in microseconds, with the wrapper
frame index of the last frame of the window:

```
frame_timing frame=N frames=300 dt_p50_us= dt_p95_us= dt_max_us= draws_p50= draws_max= present_p50_us= present_p95_us= present_max_us= draw_p50_us= draw_p95_us= draw_max_us= draw_native_p50_us= draw_native_max_us= scene_p50_us= scene_p95_us= scene_max_us= state_p50_us= state_p95_us= state_max_us= draw_calls_p50= scene_calls_p50= state_calls_p50= state_sampled= slow= gap_pre_p50_us= gap_pre_p95_us= gap_pre_max_us= gap_draw_p50_us= gap_draw_p95_us= gap_draw_max_us= gap_post_p50_us= gap_post_p95_us= gap_post_max_us= gap_draw_per_draw_us= state_top=<entry>:<calls_p50>,... state_other_p50=
```

followed by up to four witnesses for the slowest frames of that window
(slowest first, a four-slot ring ordered by `dt_us`):

```
frame_timing_slow frame=F dt_us= draws= present_us= prims= draw_us= draw_native_us= scene_us= state_us= draw_calls= scene_calls= state_calls= slow_call= slow_call_us= gap_pre_us= gap_draw_us= gap_post_us=
```

* `dt_us` is the Present-to-Present interval measured by the wrapper at the
  `frame_end` site (`QueryPerformanceCounter`); the first observed frame only
  starts the interval and is not sampled.
* `present_us` is the wall time inside the forwarded native `Present`
  (`QueryPerformanceCounter` immediately around the original call), so a window
  where `present_us` approaches `dt_us` is GPU- or vsync-bound, and one where
  it does not is CPU-bound ahead of Present.
* `draws` is the wrapper's per-frame draw count (the same counter `frame_end`
  reports) and `prims` the primitive count summed over that frame's draws; both
  come from the existing central draw path, so a many-object scene can be tied
  to its draw and primitive counts.
* the three buckets are the wall time spent inside the proxy's own hooked
  entry points, so a slow frame can be split between the proxy and the
  game/driver. `draw_us` is the four draw hooks from entry to return including
  the forwarded native draw, `draw_native_us` that forwarded call alone (so
  `draw_us - draw_native_us` is the proxy's own per-draw work), `scene_us` the
  proxy's own scene-end passes (the `EndScene` hook including the forwarded
  native `EndScene`, the engine scene-end signal, the AgX/bloom compositor pre
  and post callbacks, and the `Present` hook's pre/post-present work with the
  forwarded native `Present` subtracted) and
  `state_us` every other hooked device call (render/sampler state, textures,
  shaders and constants, render targets, `Clear`, `Lock`/`Unlock`, stream and
  declaration setters), which all share the two dispatch guards of
  `src/proxy/capture.cpp`, so one stamp pair per guard covers them.
  `*_calls` are the calls counted in each bucket. Only the outermost hooked
  entry is timed, so the proxy's own reentrant device calls are attributed to
  the entry the game made rather than counted twice. What `dt_us` leaves over
  after the four buckets and `present_us` is the game's own CPU time, the
  driver and the time between hooked calls. The stamps are taken with the hook
  mutex already held, so no bucket contains hook-lock wait: `dt_us` minus the
  buckets and `present_us` therefore covers lock wait (`telemetry` reports it
  as `LockWait`) as well as the game's own time. The `Present` hook's scope closes
  after the frame boundary, so its scene contribution is accounted to the
  following frame (one call per frame, so a window's `scene_calls_p50` still
  counts it once).
* state calls are counted, not stamped, by default. The two
  `QueryPerformanceCounter` reads of a stamped call cost 2 x 67.8 ns = 135.6 ns
  under FEX (the bottle benchmark below), which at run87's ~30,076 state calls
  per busy frame is 4.1 ms of a 28.5 ms frame; the whole added cost of a
  stamped call is 228-231 ns in the per-setter rows of the benchmark below
  (for example SetRenderState, 349.9 ns against 121.5 ns), which is 6.9 ms of
  that frame, so the diagnostic would dominate what it measures.
  `state_calls_p50` is therefore always the true
  call count, while the line reports `state_us=-1` with `state_sampled=0`
  unless calls are stamped: no calibration constant for the per-call cost of this machine is
  compiled in (`state_calibration_ns` in `src/proxy/frame_timing.h`, zero,
  nanoseconds per hooked state call), and when one is set, `state_us` becomes
  `state_calls x state_calibration_ns / 1000`, an estimate rather than a
  measurement. `--frame-timing-state-stamps N`
  (`X3M_FRAME_TIMING_STATE_STAMPS`, requires `--frame-timing`, default 0)
  stamps one state call in N and scales the sampled sum by N, reported as
  `state_sampled=N`; N=1 stamps every call, which is the old behavior and its
  old cost. The stride is kept per entry-name slot rather than over all state
  calls together, so a fixed period cannot align with the game's repeating
  per-draw setter sequence and sample the same position of it every time. The unstamped path is one hash, one pointer compare and one
  increment per call, with no clock read at all; the outermost-only rule, the
  native-Present subtraction and the nesting depth are unchanged, so the
  proxy's own reentrant calls are still not counted.
* `state_top` is the per-entry call mix of the state bucket: the six
  most-called hooked entries of the window as `<entry>:<calls_p50>`, descending
  by the window median of that entry's per-frame call count, with
  `state_other_p50` the calls whose name found no slot in the fixed 32-slot
  table. The key is the static name pointer the guard already carries
  (`__builtin_FUNCTION()`), so `SetRenderState` against `SetSamplerState`
  against `SetTexture` against `SetVertexShaderConstantF` against the stream
  and declaration setters is measured directly rather than assumed as an equal
  share (`docs/architecture/state-call-fast-path.md`).
* the three gaps split what `dt_us` leaves over after every hooked call
  (all buckets plus the native `Present`) by position relative to the frame's
  draws, from the stamps already taken, with no additional
  `QueryPerformanceCounter` read: `gap_pre_us` from the frame boundary to the
  entry of the first draw hook, `gap_draw_us` from that entry to the return of
  the last draw hook (the game's per-object scene traversal between hooked
  calls), `gap_post_us` from that return to the next frame boundary (UI,
  script, physics and AI after rendering). This is game time between hooked
  calls, not proxy time; unstamped or sampled-out state-call time is not
  measured and therefore also appears inside the gaps. A frame with no draw
  puts the whole remainder in `gap_post_us`. `gap_draw_per_draw_us` is the
  window's `gap_draw_p50` divided by `draws_p50`, in microseconds with three
  decimals: the game's own per-object cost between draws. The three gaps, the
  three buckets and `present_us` sum to `dt_us` exactly, at any sampling
  stride: a sampled state call enters the hooked total scaled by N, the same
  estimate `state_us` reports, so the sampled-out time is removed from the gaps
  rather than left in them. Two exceptions: a hooked call straddling the frame
  boundary (the `Present` hook's scope) is charged to the following frame in
  both its bucket and `gap_pre_us`, which can clamp `gap_pre_us` at zero on a
  frame with little pre-draw work; and with the stamps off entirely
  (`state_sampled=0`) there is no state estimate at all, so the state-call time
  stays inside the gaps and `state_us=-1` is not part of the sum.
* `slow_call` is the slowest single hooked call of that frame and
  `slow_call_us` its wall time; the name is the hooked function's own name
  (`__builtin_FUNCTION()` at the guard's call site) or the literal of an
  explicitly scoped entry, always static storage, never allocated.
* cost: with the option off, one predictable branch on a process-global bool
  per hooked call. On, one `QueryPerformanceCounter` pair per stamped outermost
  hooked call and no clock read at all for a counted-only state call, with no
  division and no logging on either path. Measured by
  `verification/probe/frame_timing_host.cpp --cost` (the production scope with
  the stand-in reading `clock_gettime(CLOCK_MONOTONIC_RAW)` in place of QPC;
  the mode is not run by the test): on macOS arm64 over two runs, 0.27-0.44 ns
  per call off, 2.7-3.6 ns counted-only and 30-35 ns stamped, so 2-3 ns added
  per counted state call against about 30-35 ns for a stamped one, that is
  70-95 us per frame at 30,000 counted state calls. The QPC cost under Wine/FEX is not
  measured here; the bottle benchmark below measures 67.8 ns per
  `QueryPerformanceCounter` read, which at run87's ~30,076 state calls per busy
  frame is why state calls are counted rather than stamped by default.
* percentiles are nearest-rank over the samples of the window, index
  `min(count-1, count*p/100)` of the ascending order; `slow` counts the frames
  of the window whose `dt_us` exceeds twice the window's `dt_p50`.
* memory: the fixed window grew from 26,864 to 114,416 bytes of
  zero-initialised static data (+87,552, measured on the host build; the 32-bit
  DLL differs only by the pointer in each retained `Frame`), all of it the gap
  split (3 x 300 values) and the per-entry state counts (32 slots x 300 values
  plus the overflow row). Nothing is allocated at any point, on or off.

These are diagnostic timings taken inside the proxy, not game FPS. Under FEX
the sampler attributes nothing (run84: every leaf is the ntdll syscall thunk or
an unattributed sentinel), so these buckets, not the profiler, provide the
proxy/game split. Host tests: `verification/analysis/test_frame_timing.py` with
`verification/probe/frame_timing_host.cpp` (window statistics, slow count, the
four-slot ring, the bucket/call-count reduction and the slow-call witness,
partial and over-long windows, no allocation while sampling). The probe also
compiles `src/proxy/frame_timing.cpp` itself against the Win32 stand-in of
`verification/probe/frame_timing_standin` (scripted `QueryPerformanceCounter`,
one tick per microsecond) and runs a scripted 300-frame window, so the
outermost-only accounting, the nesting depth, the native-Present subtraction,
the slow-call witness and the emitted line text are executed, not inspected.
The scripted frames also cover the counted-only state path (no clock read at
all, asserted on the stand-in's counter), sampling at N=4 (a quarter of the
calls stamped, the sum scaled by four), the per-entry counts and their
ordering, and two frames with explicit game time between the hooked calls
where the three gaps and the hooked time sum to `dt_us` exactly, one at N=1 and
one at N=4 with the scaled state estimate.

## Per-call cost of the hooked state setters under the X3 bottle (2026-09-16)

`verification/probe/state_hook_benchmark.cpp` (build
`build_state_hook_benchmark.sh`, runner `run_state_hook_benchmark.py`) drives
the installed candidate DLL `bbadc568` from a fixture process against a
synthetic windowed HAL device (no draws, no Present): an equal-share mix of
SetRenderState, SetTexture, SetSamplerState, SetTextureStageState,
SetVertexShaderConstantF (4 registers) and SetStreamSource, 1,000,002 calls per
repetition, plus the same calls per setter, the four application getters at
1,000,000 calls each and 200,000 SetStreamSource + DrawIndexedPrimitive pairs
inside one scene, three repetitions per configuration
(medians). run87's `frame_timing` carries no per-entry breakdown of the `state`
bucket (one counter per bucket plus the slowest call's name), so the mix is
equal shares. The run records which module owns each hooked vtable slot, so the
record proves the setter hooks were live; `SetTextureStageState` is not hooked
by the proxy at all and serves as the pass-through control (11.2 ns native,
11.5 ns through the proxy). Record:
`verification/results/bottle-X3/state-hook-benchmark.json` (X3 bottle,
WineArch=arm64, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`).

ns per call, medians of three repetitions:

| | native | proxy, timing off | proxy, timing on |
| --- | --- | --- | --- |
| equal-share mix | 15.6 | 316.8 | 510.8 |
| SetRenderState | 11.3 | 119.9 | 351.1 |
| SetTexture | 16.4 | 130.0 | 362.8 |
| SetSamplerState | 11.0 | 109.8 | 339.7 |
| SetVertexShaderConstantF (4) | 16.7 | 79.7 | 314.5 |
| SetStreamSource | 16.2 | 1395.4 | 1678.5 |
| SetTextureStageState (unhooked) | 11.2 | 11.1 | 11.5 |
| GetRenderState (unhooked) | 8.9 | 8.8 | 8.9 |
| GetSamplerState (unhooked) | 9.0 | 8.7 | 8.9 |
| GetTexture + Release (unhooked) | 17.4 | 17.3 | 17.6 |
| GetVertexShaderConstantF (4, unhooked) | 15.8 | 15.6 | 16.1 |
| SetStreamSource + DrawIndexedPrimitive pair | 396.2 | 4197.4 | 5276.4 |

Primitives, same bottle: `QueryPerformanceCounter` 67.8 ns (10,000,000 calls),
uncontended `std::recursive_mutex` lock+unlock 6.8 ns, GetLastError/SetLastError
pair 3.9 ns, the full four-stamp `LightCallBoundary` envelope 9.9 ns, the full
`CpuCallBoundary` envelope 1008.2 ns (2,000,000 iterations; the FNSAVE/FRSTOR
pairs under FEX).

The four application getters are not hooked in this configuration (the proxy
installs `GetRenderState` and `GetRenderTarget` only for the lazy binding mode
and the HDR redirect), and the measurement confirms it: the module owning each
getter slot is the backend, and their cost is unchanged through the proxy
(within 0.3 ns). `SetTextureStageState` is never hooked.

Attribution of the 297 ns per hooked state call of run87, taking the four
hooked light-guard setters (the per-state-write calls; SetStreamSource takes
the heavy guard and is a per-draw call): 13.8 ns native backend, 96.0 ns proxy
guard and state shadow (of which 6.8 ns lock and 9.9 ns CPU envelope, so about
79 ns is the device-map lookup, dispatch and the shadow write), and 232.2 ns
for the frame-timing stamps, whose QPC pair alone is 128.6 ns. The reported
bucket value excludes the parts of the two stamps that fall outside the
measured interval, which is why 297 ns is reported for about 342 ns of wall
cost. So roughly two thirds of run87's 8.9 ms `state` bucket is the diagnostic
itself: without `--frame-timing` the same ~30,076 calls cost about 3.3 ms.

The isolated draw pair reproduces run87's per-draw figure directly: the
SetStreamSource + DrawIndexedPrimitive pair costs 396 ns native, 4197 ns
through the proxy with the timing off and 5276 ns with it on, so 4.88 us of
proxy work per pair with the timing on against run87's measured 5.0 us of
proxy per-draw work (run87 accounts SetStreamSource in the `state` bucket, so
the two are close but not the same quantity). The 1008 ns `CpuCallBoundary`
envelope is most of it: each draw passes several heavy-guard hooks
(SetStreamSource, SetIndices, SetVertexDeclaration/SetFVF and the draw hook
itself), each paying that envelope once.
