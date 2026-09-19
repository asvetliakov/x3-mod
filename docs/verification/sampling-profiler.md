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
frame_timing qpc= frame=N frames=300 dt_p50_us= dt_p95_us= dt_max_us= draws_p50= draws_max= present_p50_us= present_p95_us= present_max_us= draw_p50_us= draw_p95_us= draw_max_us= draw_native_p50_us= draw_native_max_us= scene_p50_us= scene_p95_us= scene_max_us= state_p50_us= state_p95_us= state_max_us= draw_calls_p50= scene_calls_p50= state_calls_p50= state_sampled= slow= gap_pre_p50_us= gap_pre_p95_us= gap_pre_max_us= gap_draw_p50_us= gap_draw_p95_us= gap_draw_max_us= gap_post_p50_us= gap_post_p95_us= gap_post_max_us= gap_draw_per_draw_us= state_top=<entry>:<calls_p50>,... state_other_p50= state_redundant=<rs>,<ss>,<tex> state_shadowed=<rs>,<ss>,<tex> redundant_top=<D3DRS>:<count>,...
draw_pairs frame=N draws= draw_pairs_overflow= top=<vs_id>/<ps_id>:<draws>,... cutout_pairs=<hull>,<station>
draw_batch frame=N same_mesh= same_mesh_any_range= same_material= up= draws=
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
* three count-only window diagnostics answer what the timing above cannot
  (run 31): they are totals over the whole window, not per-frame percentiles,
  they are reset with the window, and they cost one predictable branch when the
  option is off. None of them changes behaviour: no state write is elided and
  no draw is merged.
  * `draw_pairs` is the window's program-pair mix: `draws` is every draw that
    reached a draw hook body in the window, `top` the eight most-drawn
    `(vertex, pixel)` pairs as `<vs_id>/<ps_id>:<draws>` with the same 16-hex
    program ids as the `shader kind=ps id=` lines, descending, ties keeping the
    earlier slot. `none` means no program identity was available: fixed
    function, a program the route never registered, or any draw at all while
    the route is off (`--frame-timing` alone then reports every draw as
    `none/none:<draws>`, which is the shape to check before reading the mix).
    `draw_pairs_overflow` counts the draws whose pair found no slot in the
    fixed 64-slot, eight-probe table; they are in `draws` but in no `top`
    entry, and since the table never evicts, a pair first drawn after the table
    filled is invisible to `top`. `cutout_pairs` therefore does not come from
    the table: the two qualified cutout pairs have their own counters,
    incremented on every draw before the table is consulted, in the order hull
    (`4944d81dfe531b37/5e0a10fe752b6140`) then station
    (`53a0a641107ed76c/63f96eba9eea7880`) and reported even at zero, so
    `cutout_pairs=0,0` does prove those pairs never drew in the window.
  * a draw the route refused or substituted is still counted (the counters sit
    at the top of the shared draw path, before the route decides); a draw
    rejected by `draw_submission_blocked()` returns before that point and the
    proxy's own internal draws do not enter the draw hooks at all, so neither
    is counted.
  * `state_redundant` counts the state writes whose incoming value equals the
    value the proxy's shadow already holds, for `SetRenderState`,
    `SetSamplerState` and `SetTexture` in that order, with `state_shadowed` the
    denominators: the calls of each hook that had a shadowed value to compare
    against. The counting sits inside the shadow update, so it only sees what
    the proxy actually shadows: the ~32 render states of the route's shadow
    table (and only while the route is enabled and no state block is
    recording), `D3DSAMP_SRGBTEXTURE` always plus `MIPFILTER`/`MIPMAPLODBIAS`
    only with a mip bias configured, and every `SetTexture` on stage 0-15
    (the pointer shadow always holds a current value). The hooks themselves are
    installed only under the mip-bias, composition, linear-material and
    screen-emission conditions, so `--frame-timing` on its own reports
    `state_redundant=0,0,0 state_shadowed=0,0,0`: that means nothing was
    hooked, not that nothing was redundant. The production command
    (`--motion-output --taa` with the TAA mip bias, `--linear-materials`)
    installs all three and makes the counters meaningful.
    `redundant_top` names the four most redundant render states by `D3DRS`
    index, from a 32-slot table keyed by `index mod 32` with an eight-probe
    limit: `D3DRS_WRAP8..15` alias `WRAP6..13` there, and a state that finds no
    slot is attributed to none, so a hot state can be missing from the list
    while `state_redundant` itself stays exact. This is what decides whether a
    state-manager filter is worth installing; section (d) of
    `docs/architecture/state-call-fast-path.md` says why nothing is elided.
  * `draw_batch` classifies each draw against the previous draw of the same
    frame; the three classes are disjoint and a draw is never compared across a
    frame boundary. `same_mesh` is the same stream-0 vertex buffer, index
    buffer, declaration/FVF, both programs, stage 0-3 textures *and* the same
    primitive type, count, base vertex and start index, so only the constants
    differ: the instancing candidate. `same_mesh_any_range` is the same
    bindings with a different primitive range, and `same_material` the same
    programs and stage 0-3 textures across a different mesh. `up` counts the
    `DrawPrimitiveUP`/`DrawIndexedPrimitiveUP` draws, which are never
    classified and break the chain: D3D9 clears stream 0 on such a call and the
    proxy does not invalidate its shadow, so the shadowed buffers say nothing
    about them or about the draw that follows. `draws` is the same denominator
    as `draw_pairs`.
  * cost: per draw, one table mix with at most eight probes and one key
    comparison (no `Get*` call: the bindings come from the shadow the route
    already keeps); per state write, one comparison plus at most eight probes
    for the render-state attribution. No allocation and no lock beyond the hook
    mutex the path already holds.
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

## Frame phases (`X3M_FRAME_PHASES=1`)

`--frame-timing` says how much of a frame is inside the proxy's hooks and the
native Present; what is left (run87: about 15 ms of a 28.5 ms frame at 457
draws) is game code between D3D calls, which the sampler cannot attribute under
FEX. `--frame-phases` (launcher `tools/manage.py`, requires `--telemetry`,
independent of `--game-phases`; `X3M_FRAME_PHASES=1`) attributes it to the
engine's own per-frame phases with ten byte-verified stamps inside the render
routine `0x00471f50` (`src/proxy/frame_phase_sites.h`, study
`docs/reverse-engineering/frame-loop-phases.md` section 4) plus the Present
hook. Each stamp is one `QueryPerformanceCounter` through the game-phase stub
and CPU boundary (`src/proxy/game_phases.cpp`, `x3m_game_phase_enter` with an
index at or above the phase group's count), so the computational state,
LastError and the game's exact ESP at the displaced instructions are preserved
as for the existing markers. No stamp is on a per-object path: the seven core
sites fire once per frame, the three view sites once per view (three or more
views per frame). The install is one transaction inside the engine-patch
window (preflight of all ten spans, claim in order, rollback of every patched
site on the first failure, `late_claim`/`install_window_closed` after the first
Present); `frame_phase_mode` and ten `frame_phase_site` lines record it.

A frame runs from one native Present return to the next; the interval that
starts at a stamp carries that stamp's name. Per 300-frame window one line in
microseconds (nearest-rank percentiles, the frame index of the window's last
frame) and up to four witnesses for the slowest frames by `dt_us`, keyed by the
same wrapper frame index as `frame_timing_slow` so the two can be joined:

```
frame_phases qpc= frame=N frames=300 incomplete= dt_p50_us= dt_p95_us= pre_render_p50_us= pre_render_p95_us= prologue_p50_us= prologue_p95_us= scene_update_p50_us= scene_update_p95_us= begin_scene_p50_us= begin_scene_p95_us= views_p50_us= views_p95_us= overlays_p50_us= overlays_p95_us= text_p50_us= text_p95_us= scene_end_p50_us= scene_end_p95_us= present_p50_us= present_p95_us= view_setup_p50_us= view_setup_p95_us= view_submit_p50_us= view_submit_p95_us= views_p50= order_errors= clock_errors= unmatched= dropped= early= foreign=
frame_phases_slow frame=F dt_us= pre_render_us= prologue_us= scene_update_us= begin_scene_us= views_us= overlays_us= text_us= scene_end_us= present_us= view_setup_us= view_submit_us= views= complete=
```

| Phase | From | To | Contains | Scales with |
| --- | --- | --- | --- | --- |
| `pre_render` | native Present return | `frame_phase_prologue` `0x00471f6c` | the main loop's input/messages, script VM, deferred callbacks, simulation/AI and cockpit update (`--game-phases` subdivides it) | objects, scripts |
| `prologue` | `0x00471f6c` | `frame_phase_scene_update` `0x00472044` | `0x004f4fc0`, view count scan, `malloc`+`memset` of the view array | views |
| `scene_update` | `0x00472044` | `frame_phase_begin_scene` `0x004720b5` | lens-flare setup, recursive node transform update `0x0047b680`, node-flag reset | linear in scene nodes |
| `begin_scene` | `0x004720b5` | `frame_phase_views` `0x00472186` | `BeginScene`, per-view update, view `_qsort` | views (constant in objects) |
| `views` | `0x00472186` | `frame_phase_overlays` `0x0047238d` | the whole per-view loop: env map, setup, traversal, draw-queue sort, submission, particles, the scene-end hook `0x004721b1` | super-linear in queued objects; O(n²) sort |
| `overlays` | `0x0047238d` | `frame_phase_text` `0x004724ec` | second view pass (2D overlays) and the cockpit view (its own traversal/sort/submission) | constant, plus cockpit objects |
| `text` | `0x004724ec` | `frame_phase_scene_end` `0x00472574` | on-screen text | constant |
| `scene_end` | `0x00472574` | Present hook entry | frame `EndScene`, the two option-gated tails, the view-array `free`, the main loop's post-render, detail and presentation phases | constant |
| `present` | Present hook entry | native Present return | the forwarded native `Present` (the same interval `frame_timing` reports as `present_us`) | GPU/vsync |
| `view_setup` (sum) | `frame_phase_view_setup_begin` `0x0047224c` | `frame_phase_view_submit_begin` `0x00472270` | per-view light selection `0x004892a0`, state build, camera/viewport/clear, pass setup | views |
| `view_submit` (sum) | `0x00472270` | `frame_phase_view_submit_end` `0x004722c8` | the layer loop: traversal `0x0047e920`, sort `0x0047e620`, submission `0x0047e6e0` (the effect state-manager path) | queue length × sub-meshes × passes |

* `dt_us` is the sum of the nine phases; `views` is the number of
  `view_setup_begin` stamps in the frame and `view_setup`/`view_submit` are
  sums over those views. A frame without views lands from `begin_scene` on
  `text` (the routine's `0x0047216a` edge): the skipped phases are zero and the
  frame counts as `incomplete`. A repeated or backward core stamp, a second
  Present begin, or a backward clock drops the frame (`order_errors`,
  `clock_errors`, `dropped`); stamps outside a live frame are `unmatched`.
  `early` counts stamps before the first Present admitted the owner thread,
  `foreign` stamps from any other thread; both are ignored. The first observed
  frame only starts the interval, as in `frame_timing`.
* Contract notes on the two sites the study flagged: `frame_phase_views`
  (`xor ebx,ebx; add esp,0x10` after the `_qsort` call) and
  `frame_phase_view_setup_begin` (`push esi; call 0x004892a0`) run in the arena
  tail at the game's exact ESP, because the stub restores every register and
  the flags (`popad`/`popfd`) before its `jmp [next]` and the dispatcher is a
  plain `jmp [entry]`; the displaced call's return address lies in the tail like
  every displaced call of the phase group. Both sites are kept.
  `frame_phase_overlays` leaves live flags for the `jle` at `0x00472393`, which
  the tail's copy of the `cmp` re-creates after `popfd`.
* cost: 7 + 3 x views stub dispatches per frame (seven core stamps once, the
  three view stamps once per view), each one `QueryPerformanceCounter` read
  through the game-phase stub and CPU boundary. The CPU fixture measures the
  stub plus boundary at 0.51 us per dispatch under the X3 bottle (`GAME PHASE
  BENCH` `disabled_added_loop_us` 9.26 over 18 marker calls, handler returning
  early); the stamp itself adds an unbenchmarked `GetCurrentThreadId` and
  `QueryPerformanceCounter` (about 0.1 us, QPC 68 ns above), so budget about
  0.6 us per stamp and 10 us per frame at three views. No allocation; the window reduction once per 300 frames. Off,
  three relaxed atomic loads per frame and nothing at the sites.
* `begin_scene_p50_us` blends frames that have views with frames that have
  none (the no-views edge lands on `text`, so the whole interval from
  `0x004720b5` to `0x004724ec` is `begin_scene` there): read it together with
  `incomplete`, and use the `frame_phases_slow` witnesses (`complete=1`) for a
  per-frame split.
* verification: `python3 verification/probe/verify_frame_phase_sites.py`
  (exact bytes, whole instructions, no interior branch, the exact incoming-edge
  set per span, the rel32 replay at three arena addresses, no indirect jump and
  one `ret` in the routine, no data reference to any span byte, disjointness
  from the scene hook, the flag consumer and the `_qsort` call, EXE identity);
  `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3
  verification/probe/run_game_phase_cpu.py` (the CPU fixture with one scripted
  frame: install, native register/flag/x87/XMM/LastError parity, stamp order
  and interval accounting with two views, an order error, rollback to the
  original bytes, refusal on a byte mismatch, partial-install rollback on a
  duplicate claim (the group status carries the claim's own reason,
  `bytes_mismatch`), refusal after the install window closed); host
  `verification/analysis/test_game_phase_frame.py` (`frame_phases_host.cpp`
  tracker/window probe, wiring, launcher). Ledger:

| Date | Change | Checks | Result |
| --- | --- | --- | --- |
| 2026-09-16 | Frame-phase group added (ten sites, `--frame-phases`) | `verify_frame_phase_sites.py` PASS (10 sites, all checks); `run_game_phase_cpu.py` under X3: 8033 checks, 0 failures, arena 13896/16384 B; host `test_game_phase_frame` 10 tests OK; DLL RelWithDebInfo 0 warnings, `check_no_x87.py` 0 violations | not yet run in the game; first instrumented flight pending |

## Pass phases (`X3M_PASS_PHASES=1`)

`view_submit` (run91 busy window: 29.4 ms of a 37.3 ms frame) is the engine's
per-view layer loop, and `frame_timing`'s `gap_draw` (23.4 ms in the same
window, 23.1 us per draw) is the part of it outside the proxy's hooked calls.
Neither says how much of that is the D3DX effect applying a pass versus the
engine's own per-object work. `--pass-phases` (launcher `tools/manage.py`,
requires `--telemetry` and `--frame-phases`; `X3M_PASS_PHASES=1`) splits every
material draw with four byte-verified stamps in the D3DX pass loop of the
material submission routine `0x004c0150` (`src/proxy/pass_phase_sites.h`,
study `docs/reverse-engineering/effect-pass-loop.md`): `pass_begin`
`0x004c3ff0` (loop head), `pass_applied` `0x004c4000` (`BeginPass` returned),
`pass_drawn` `0x004c403e` (`DrawIndexedPrimitive` returned), `pass_end`
`0x004c4049` (`EndPass` returned). The loop issues one draw per iteration, so
the stamps are per-draw sites: ~1,006 passes and ~4,024 dispatches per busy
frame.

At that rate the shared game-phase stub is over budget (`PreserveCpuState`,
FNSAVE/FRSTOR, about 0.6 us per dispatch), so the group has its own lean stub
(`src/proxy/pass_phases.cpp` `emit`, 124 bytes): `pushfd`, `push eax/ecx/edx`,
`cld`, XMM0-7 saved, `push index; call x3m_pass_phase_enter`, everything
restored, `jmp [next]`; the displaced instructions run in the claim tail at
the game's exact ESP (every span is a plain copy, no rel32 to re-base, and
the game's flags are dead at all four sites). The handler runs under
`LightCallBoundary` (MXCSR + LastError; it is x87-free, walked by
`verification/probe/check_no_x87.py` from `_x3m_pass_phase_enter`), does one
relaxed `active` load, one owner-thread compare, one `QueryPerformanceCounter`
and one 64-bit subtract/add into the frame's `apply`/`draw`/`end` accumulator
(`src/proxy/pass_phases_core.h`); no tracker, no logging, no allocation, no
lock. The window arithmetic runs once per frame at the frame-phase boundary
(`frame_phases::detail::frame_impl`, under its owner guard), which also joins
the same frame's `view_submit` sum; a frame the frame group dropped is
`dropped` here too. The install is the frame group's transaction (preflight,
in-order claim, reverse rollback, `install_window_closed`/`late_claim`) and
additionally refuses with `frame_phases_off` when the frame group is not
active; `pass_phase_mode` and four `pass_phase_site` lines record it.

Per 300-frame window one line, microseconds, nearest-rank percentiles over
per-frame sums, the frame index of the window's last frame:

```
pass_phases qpc= frame=N frames=300 passes_p50= apply_p50_us= apply_p95_us= draw_p50_us= draw_p95_us= end_p50_us= end_p95_us= sum_p50_us= view_submit_p50_us= self_p50_us= dispatch_cost_ns= orphans= clock_errors= clock_failures= unmatched= dropped= early= foreign=
```

| Field | Interval | Contains |
| --- | --- | --- |
| `apply` | `pass_begin` -> `pass_applied` | `ID3DXEffect::BeginPass` only (no `CommitChanges` in the routine): D3DX applies the pass state through the game's state manager, i.e. the ~63 hooked state calls per draw plus D3DX's own work |
| `draw` | `pass_applied` -> `pass_drawn` | the two geometry calls (`primCount`, `NumVertices`) and the hooked `IDirect3DDevice9::DrawIndexedPrimitive` including its native call |
| `end` | `pass_drawn` -> `pass_end` | `ID3DXEffect::EndPass` (`D3DXFX_DONOTSAVESTATE`, so no state restore) |
| `sum` | | `apply + draw + end` per frame |
| `view_submit` | | the same frame's `frame_phases` `view_submit` sum, for the residual |
| `self` | | `passes * 4 * dispatch_cost_ns / 1000`: the stamps' own estimated cost, from the fixture row below, not subtracted |

Reading it, with the run91 busy window as the yardstick:

* `view_submit_p50_us - sum_p50_us` is the engine's per-object residual
  (draw-queue sort, the ~75 by-name parameter writes per object, `Begin`/`End`,
  the two engine state writes, overlays and particles in the same interval).
  If it carries most of the 29.4 ms, the lever is the engine's submission
  path, not the D3DX pass.
* `apply_p50_us` is what the engine-state-filter note's option B could
  attack. `apply_p50_us - state_p50_us` (`frame_timing`, same window, with
  `--frame-timing-state-stamps`) is D3DX's cost outside the hooked setters.
  Below ~3 ms apply kills option B; at >= 10 ms it is the only lever short of
  drawing fewer objects.
* the pass `draw_p50_us` minus the `frame_timing` `draw_p50_us` is the two geometry calls;
  `gap_draw` (23.4 ms) should be about `apply - state` plus the residual plus
  the geometry calls, since those are the parts of the draw span outside
  hooked calls.
* Each stamp's cost lands in the interval that follows it: the three
  intervals are each inflated by roughly one dispatch per pass and
  `view_submit` by four; subtract `self_p50_us` (about 0.36 ms at 1,006
  passes) before comparing.
* `passes_p50` must track the telemetry `draws_p50` (passes <= draws, the
  difference being overlay and particle draws outside the material path); a
  large gap means multi-pass techniques and the per-draw arithmetic above
  must be redone. `orphans` (a closing stamp with no open interval),
  `clock_errors` (backward clock: interval skipped), `clock_failures` (QPC
  failed: counting only), `unmatched`, `early` and `foreign` (stamps before
  admission or from another thread, ignored) should be zero in a healthy run.
* cost: the CPU fixture measures the lean stub at 90.5 ns per dispatch under
  the X3 bottle (`PASS PHASE BENCH`, best of 7 x 20,000 loops of the four
  spans hooked minus unhooked), i.e. about 0.36 ms per busy frame at 4,024
  dispatches against the 1.5 ms ceiling of the study; the two-stamp fallback
  was not needed. `dispatch_cost_ns` in `pass_phases_core.h` is 87 and the
  fixture refuses a constant more than 2x off the measurement. Off, nothing
  is installed and the frame boundary is one relaxed load.
* verification: `python3 verification/probe/verify_pass_phase_sites.py`
  (exact bytes, whole instructions of the gap-free routine decode, the single
  incoming edge, plain copy, frame-depth anchors, vtable dispatches, raw
  interior-encoding sweep of `.text`, no data reference, point-light patch
  disjoint, EXE identity); `X3M_FIXTURE_BOTTLE=X3 python3
  verification/probe/wine_lock.py python3
  verification/probe/run_game_phase_cpu.py` (the four exact spans replayed
  at frame depth with hostile CPU state: register/flag/XMM/x87/MXCSR/LastError/
  ESP parity, early and foreign stamps ignored, one pass counted and the
  chain closed, the joined sample, a mid-pass entry as one orphan, rollback,
  byte-mismatch refusal, duplicate-claim rollback, late-window refusal, the
  benchmark row); host `verification/analysis/test_pass_phases.py`
  (`pass_phases_host.cpp` accumulator/gate/window probe, wiring, launcher).
  Ledger:

| Date | Change | Checks | Result |
| --- | --- | --- | --- |
| 2026-09-16 | Pass-phase group added (four sites, lean stub, `--pass-phases`) | `verify_pass_phase_sites.py` PASS, `source_present: true`; `run_game_phase_cpu.py` under X3: 8136 checks, 0 failures, `PASS PHASE BENCH dispatch_ns=90.5 implied_busy_frame_us=364 within_budget=1`, fixture arena 15528/16384 B; host `test_pass_phases` 9 tests OK; DLL RelWithDebInfo 0 warnings, `check_no_x87.py` 0 violations with `_x3m_pass_phase_enter` walked | not yet run in the game |
| 2026-09-16 | Lean stub, owner gate, percentile and install transaction factored into `lean_stub.cpp`, `stamp_core.h`, `stamp_install.h` (shared with loop phases); `dispatch_cost_ns` 87 -> 91 (the larger of the two measurements); a `pass_begin` over a pass whose end never arrived now counts in `orphans` | `run_game_phase_cpu.py` under X3 (with the loop group): `PASS PHASE BENCH dispatch_ns=86.6`; host `test_pass_phases` 9 tests OK | not yet run in the game |
| 2026-09-16 | Run 33 install candidate re-measurement on committed main a3cafd5 (DLL `03c0c9f4`) | `check_no_x87.py` 0 violations (74 roots, 492 reachable, `_x3m_pass_phase_enter` walked); `run_game_phase_cpu.py` with a build (`fixture_sha256 377ac95e`) PASS, 8274 checks, 0 failures, `PASS PHASE BENCH dispatch_ns=89.1 implied_busy_frame_us=358 within_budget=1` against `dispatch_cost_ns` 91; record `verification/results/run33-candidate-build.json` | not yet run in the game |
| 2026-09-16 | Run 34 install candidate re-measurement on committed main ee5a406 (DLL `7102a2f1`) | `check_no_x87.py` 0 violations (76 roots, 494 reachable, `_x3m_pass_phase_enter` walked); `run_game_phase_cpu.py` with a build (`fixture_sha256 377ac95e`) PASS, 8521 checks, 0 failures, `PASS PHASE BENCH dispatch_ns=90.3 implied_busy_frame_us=363 within_budget=1` against `dispatch_cost_ns` 91; record `verification/results/run34-candidate-build.json` | not yet run in the game |

## Loop phases (`X3M_LOOP_PHASES=1`)

Run94 put 95.7 % of the sustained 390 ms frame in the main loop's Input phase
and, inside it, `input_part=0` (`[0x00403b09, 0x00403b3a)`, three calls) with
no named call in the tape. The static study
`docs/reverse-engineering/main-loop-input-region.md` names the per-sector
update driver `0x0043a360` as the owner with high confidence and cannot
decide between its five callees. `--loop-phases` (launcher `tools/manage.py`,
requires `--telemetry` and `--frame-phases`; `X3M_LOOP_PHASES=1`) splits it
with six byte-verified stamps inside the driver (`src/proxy/loop_phase_sites.h`):
`sector_collide` `0x0043a38e`, `sector_simulate` `0x0043a394`, `sector_post`
`0x0043a39a`, `sector_pass_a_end` `0x0043a3a0`, `sector_economy` `0x0043a3be`,
`sector_pass_b_end` `0x0043a3ca`. The driver walks the universe container list
twice (class word `[+0x48] == 1`, skip bit `[+0x148] & 1`): pass A calls
`0x0045d250`/`0x00452ad0`/`0x0045b720` per active container, pass B calls
`0x004596e0` then `0x004526b0`. Sites 0, 1, 2 and 4 are `push esi; call rel32`
(the claim tail re-bases the rel32, the callee sees an arena return address,
the contract the game-phase clock/pump/channels sites rely on); sites 3 and 5
are the five-byte `mov esi,[esi]; cmp [esi],0` that the two gate `jne`s of
each pass land on (`0x0043a384`/`0x0043a38c` and `0x0043a3b4`/`0x0043a3bc`),
so the span *is* the patch jump: a gate edge executes the stub, the tail
replays both instructions after the stub's `popfd` and the `cmp` leaves the
flags the following `jne` back edge (`0x0043a3a5` -> `0x0043a380`,
`0x0043a3cf` -> `0x0043a3b0`, both outside every span) consumes. The RE note's
"back edges land on the stamp" is therefore more precisely "the gate skip
edges land on the stamp; the loop back edges go to the loop heads"; the CPU
fixture models both (a skipped container reaches the stamp through the gate
edge, and an entry trampoline jumps onto the patched span start).

The stamp rate is six per active sector plus two per container the gates skip,
per frame, and the container count is unknown (open issue of the RE note), so
the group uses the lean stub shared with pass phases (`src/proxy/lean_stub.cpp`,
124 bytes, no x87 save; handler `x3m_loop_phase_enter` under
`LightCallBoundary`, walked by `check_no_x87.py`). Per frame the accumulator
(`src/proxy/loop_phases_core.h`) keeps four interval sums (collide = site
0 -> 1, simulate = 1 -> 2, post = 2 -> 3, passb = 4 -> 5), the sector count
(site 4 hits: containers that passed the pass-B gate), the container count
(site 5 hits), the dispatch count and the largest single interval with the
interval that owned it, so one pathological sector is visible. A pass end with
nothing open is the container walk, not an error; any other close without its
open, or an open over an interval still open, is an `orphan`. The window
reduction runs at the frame-phase boundary (`frame_phases::detail::frame_impl`,
owner guard) and joins the frame's `dt` and `pre_render`; when `--game-phases`
is also on, the Input phase of the last completed loop
(`game_phases::last_input_us`) replaces `pre_render` as `input`. Install is the
shared transaction (`src/proxy/stamp_install.h`: preflight, in-order claim,
reverse rollback, `install_window_closed`/`late_claim`) plus `frame_phases_off`;
`loop_phase_mode` and six `loop_phase_site` lines record it.

Per 300-frame window one line, microseconds, nearest-rank percentiles over
per-frame sums:

```
loop_phases qpc= frame=N frames=300 sectors_p50= containers_p50= collide_p50_us= collide_p95_us= simulate_p50_us= simulate_p95_us= post_p50_us= post_p95_us= passb_p50_us= passb_p95_us= sum_p50_us= input_p50_us= self_p50_us= dispatch_cost_ns= max_interval_us= max_interval_owner= slow= orphans= clock_errors= clock_failures= unmatched= dropped= early= foreign=
```

and, for each of the first 64 frames of the window whose `sum` exceeds 50 ms
(`slow` counts all of them):

```
loop_phases_slow qpc= frame= dt_us= sectors= containers= collide_us= simulate_us= post_us= passb_us= sum_us= input_us= max_interval_us= max_interval_owner=
```

| Field | Interval | Contains |
| --- | --- | --- |
| `collide` | `sector_collide` -> `sector_simulate` | `0x0045d250`: flag clearing over the 32 object buckets, then collision detect/respond on bucket 0 (`0x0045cab0` query, `0x0045e130` response, the `"CollisionWarn"`/`"MakeDamage"` script notifications) |
| `simulate` | `sector_simulate` -> `sector_post` | `0x00452ad0` (27,390 bytes, 401 calls): the per-object simulation body over all 32 buckets |
| `post` | `sector_post` -> `sector_pass_a_end` | `0x0045b720`: the global object chain walk seeded by `0x0044e600` |
| `passb` | `sector_economy` -> `sector_pass_b_end` | `0x004596e0` (own time accumulator, catch-up work) **and** `0x004526b0` (a seventh site would split them) |
| `sum` | | the four intervals per frame |
| `input` | | the same frame's `pre_render` (`frame_phases`), or the `--game-phases` Input phase of the last completed loop when that group is on |
| `self` | | `dispatches * dispatch_cost_ns / 1000`, the stamps' own estimated cost, not subtracted |
| `max_interval` | | the largest single interval of the frame (window line: of the window) and its owner (`collide`/`simulate`/`post`/`passb`) |

Reading it, with run94 as the yardstick (`input_part=0` p50 391,500 us on the
slow sector):

* `input_p50_us - sum_p50_us` is the residual of the input region outside the
  driver: the cut-event driver `0x0048f550`, the deferred-delete sweep
  `0x0045b660`, both list walks and (when `input` is `pre_render`) everything
  else between the Present return and the render routine. If the residual
  carries the stall, the driver was not the owner and the note's §3 ranking
  is wrong.
* The interval whose p95 tracks `sum_p95` names the callee; on a slow frame
  `max_interval_owner` says which routine, and `max_interval_us` against
  `sum_us` says whether one sector or every sector carries it (`sectors` and
  `containers` in the same line give the per-sector cost).
* `sectors_p50` answers the RE note's open question: ~1 means only the
  player's sector is active and the stamp rate is ~6 + 2 x `containers`
  dispatches per frame; a large value means every sector is simulated every
  frame and the per-sector cost, not the sector count, is the lever.
* `orphans`, `clock_errors`, `clock_failures`, `unmatched`, `early` and
  `foreign` should be zero in a healthy run.
* cost: the CPU fixture measures the lean stub on the mirrored driver at
  89.7 ns per dispatch under the X3 bottle (`LOOP PHASE BENCH`, best of
  7 x 20,000 loops of the 16-dispatch body hooked minus unhooked; the tracked
  record `verification/results/bottle-X3/game_phase_cpu.json`;
  `dispatch_cost_ns` is 91, the fixture refuses a constant more than 2x off).
  Implied cost per frame: 0.54 us for 1 active sector (6 dispatches), 108 us
  for 200 active sectors (1,200 dispatches), plus 0.18 us per skipped
  container. Off, nothing is installed and the frame boundary is one relaxed
  load.
* arena: with every optional group on (resource reader, 47 game-phase sites,
  10 frame, 4 pass, 6 loop, chase camera/transition/lead/aim/fire, voice DMO
  fallback, 12 loading probes) the modelled use is 15,752 of 16,384 B, 632 B
  free (`test_game_phase_sites.py`); the six loop sites take 888 B. The CPU
  fixture build alone gets a 32 KiB arena because it installs and rolls back
  every group several times and the arena is never freed.
* verification: `python3 verification/probe/verify_loop_phase_sites.py`
  (exact bytes, whole instructions of the gap-free routine and region decode,
  the exact incoming edges, rel32 targets and their re-based arena copies,
  plain-copy contract, ESP contract, single-caller chain, raw
  interior-encoding sweep, no data reference, disjoint from the 47 game-phase,
  10 frame-phase and 4 pass-phase sites, EXE identity); `X3M_FIXTURE_BOTTLE=X3 python3
  verification/probe/wine_lock.py python3
  verification/probe/run_game_phase_cpu.py` (the driver mirrored instruction
  for instruction with fixture callees and a four-container list, executed
  through the production stubs with hostile CPU state: GPR/EFLAGS (the
  terminator's `cmp` flags at exit)/XMM/x87/MXCSR/LastError/ESP parity, exact
  native call counts and arguments, early and foreign stamps ignored, two
  sectors/four containers/sixteen dispatches with no orphan, the slow-simulate
  owner, the joined sample, an entry on the pass_a_end stub through the gate
  edge with the replayed `cmp` deciding the back edge, a mid-chain entry as one
  orphan, rollback, byte-mismatch refusal, duplicate-claim rollback,
  late-window refusal, the benchmark row); host
  `verification/analysis/test_loop_phases.py` (`loop_phases_host.cpp`
  accumulator/gate/window/slow-limit probe, wiring, launcher). Ledger:

| Date | Change | Checks | Result |
| --- | --- | --- | --- |
| 2026-09-16 | Loop-phase group added (six sites, shared lean stub, `--loop-phases`) | `verify_loop_phase_sites.py` PASS, `source_present: true`; `run_game_phase_cpu.py` under X3: 8274 checks, 0 failures, `LOOP PHASE BENCH dispatch_ns=89.7 implied_frame_us_1_sector=0.54 implied_frame_us_200_sectors=108`, fixture arena 17980/32768 B; host `test_loop_phases` 9 tests OK (`loop_phases_host` 40 checks); DLL RelWithDebInfo 0 warnings, `check_no_x87.py` 0 violations with `_x3m_loop_phase_enter` walked (492 reachable functions). Both DLL figures were measured on the worktree build (31c0c79 plus the uncommitted change), and the fixture record has `fixture_sha256: null` because that run used `--no-build`. The run 33 install candidate re-measured both on committed main a3cafd5 (DLL `03c0c9f4`): `check_no_x87.py` 0 violations, 74 roots / 492 reachable functions with `_x3m_loop_phase_enter` and `_x3m_pass_phase_enter` walked, and `run_game_phase_cpu.py` with a build (`fixture_sha256 377ac95e`) PASS at 8274 checks, 0 failures, `LOOP PHASE BENCH dispatch_ns=90.9 implied_frame_us_1_sector=0.55 implied_frame_us_200_sectors=109`; record `verification/results/run33-candidate-build.json`. The run 34 candidate on committed main ee5a406 (DLL `7102a2f1`) repeated both: `check_no_x87.py` 0 violations, 76 roots / 494 reachable, and `run_game_phase_cpu.py` PASS at 8521 checks, 0 failures, `LOOP PHASE BENCH dispatch_ns=88.6 implied_frame_us_1_sector=0.53 implied_frame_us_200_sectors=106`, fixture arena 19,448/32,768 B; record `verification/results/run34-candidate-build.json` | not yet run in the game |

## Residual phases (`X3M_RESIDUAL_PHASES=1`)

Run95's busy window leaves two remainders unattributed: `view_submit − sum`
(4,520 us, "the engine's work between passes", 4.6 us per draw) and the part
of the `views` phase outside `view_setup` and `view_submit` (23,072 − 20,115
− setup, roughly 2 ms per busy frame). `--residual-phases` (launcher
`tools/manage.py`, requires `--telemetry`, implies `--frame-phases` and
`--pass-phases`; `X3M_RESIDUAL_PHASES=1`) splits each with one
byte-verified stamp (`src/proxy/residual_phase_sites.h`, studies
`docs/reverse-engineering/effect-pass-loop.md` section 7 and
`frame-loop-phases.md` section 5d):

* `material_setup` `0x004c1eab` (`mov edx,[ebx]; mov ecx,[edx+0xfc]`, 8
  bytes), the `ID3DXEffect::Begin` dispatch of the material submission
  routine, once per sub-mesh. It is the first instruction of the
  steady-state D3DX path: the two material-initialisation guards
  (`0x004c0c67`, `0x004c0ded`), the initialisation path's exit
  (`0x004c0de5`) and its parameter-loop skip (`0x004c1e23`) all land on the
  span start.
* `view_particles` `0x0047230c` (`mov edx,[0x608518]; add esp,4`, 9 bytes),
  the return of the particles call `0x004bf4c0` in the frame routine's
  per-view loop, once per view whose `view[0x270] & 0x4000` gate admitted the
  call (the `je 0x472315` that skips it lands exactly on the span end).

Neither stamp opens an interval of its own: each pairs with a clock a sibling
group retains on the same owner thread. The pass accumulator keeps
`end_clock` (the last `pass_end`, one store per pass) and `begin_clock` (the
first `pass_begin` after this group armed it, one predicted branch per
`pass_begin`); the frame tracker keeps `submit_end` (the view's
`view_submit_end`, one store per view). At `material_setup`, `prepare += now
− end_clock` and the previous material's `setup += begin_clock − p` closes
(the frame's last material closes at the frame boundary); at
`view_particles`, `particles += now − submit_end`. The frame boundary
(`frame_phases::detail::frame_impl`, ahead of the pass group's own reduction
so the pass count and clocks are still open) computes `other = views −
view_setup − view_submit − particles`. Same lean stub and handler shape as
the pass group (`LightCallBoundary`, one `QueryPerformanceCounter`, no log,
no allocation; `check_no_x87.py` walks `_x3m_residual_phase_enter`); the
install is the shared transaction and additionally refuses with
`frame_phases_off` or `pass_phases_off`; `residual_phase_mode` and two
`residual_phase_site` lines record it. About 1,000 material stamps and a
handful of view stamps per busy frame.

Per 300-frame window one line, microseconds, nearest-rank percentiles over
per-frame values, the frame index of the window's last frame:

```
residual_phases qpc= frame=N frames=300 materials_p50= particle_views_p50= passes_p50= views_p50= prepare_p50_us= prepare_p95_us= setup_p50_us= setup_p95_us= prepare_per_pass_p50_ns= setup_per_pass_p50_ns= particles_p50_us= particles_p95_us= other_p50_us= other_p95_us= self_p50_us= dispatch_cost_ns= prepare_skipped= setup_skipped= view_skipped= other_underflow= clock_errors= clock_failures= unmatched= dropped= early= foreign=
```

| Field | Interval | Contains |
| --- | --- | --- |
| `prepare` | last `pass_end` -> `material_setup` | the engine's per-object preparation: the pass loop's tail, `ID3DXEffect::End`, the routine's exit (SEH unlink), the caller's queue walk or traversal step, cull and world matrix, the next node's entry (SEH record, `0x4c8`-byte frame, `SetSoftwareVertexProcessing`), the sub-mesh head, the three engine-wrapper binds and the two initialisation guards; the D3DX work inside it is `End`, and per draw `GetTechniqueByName` (slot 13) plus `SetTechnique` (slot 58; `FindNextValidTechnique` only on a name miss), never the parameter setters |
| `setup` | `material_setup` -> first `pass_begin` | D3DX: `Begin(&passes, 1)`, the ~75 parameter setters (`SetInt`/`SetVector`/`SetBool`/`SetFloat`/`SetMatrix`, `GetBool`, `ApplyParameterBlock`, the cached texture setter) and, engine-side, the two render-state writes and the geometry guard |
| `particles` | `view_submit_end` -> `view_particles` | the particles/stardust pass `0x004bf4c0` of the view (plus the small state reset loop before it) |
| `other` | `views − view_setup − view_submit − particles` | the rest of the per-view loop: the scene-end composite `0x004c4750` (the proxy's scene hook), the env-map pass, post-view fixup `0x00489bf0`, `0x004715d0`, the loop's own tail |
| `*_per_pass_ns` | | `prepare`/`setup` × 1000 / the frame's `passes` (the pass group's count), the per-draw figure the pass-replay note estimates |
| `self` | | `(materials + particle_views) × dispatch_cost_ns / 1000`, not subtracted |

Reading it, against the run95 busy window (`view_submit − sum` 4,520 us at
981 passes): `setup_p50_us` is what option A of
`docs/architecture/effect-pass-replay.md` can take over with parameter
ownership (the setters and `Begin`); `prepare_p50_us` is the engine's own
cost and is reducible only by engine patches (the O(n²) sort, the SEH frame
per node). `prepare + setup` should be close to `view_submit − sum −
(traversal, sort and everything outside the sub-mesh loop)`, so the gap to
4,520 us is the per-view work outside `0x004c0150`. `particles_p50_us`
against `other_p50_us` says whether the ~2 ms outside submission is the
particles pass or the composite/env-map path. `prepare_skipped` is about one
per frame plus one per material whose geometry guard skipped the pass loop
(those also count in `setup_skipped`); `view_skipped` counts a second
particles return against the same view (not expected); `other_underflow`
counts frames whose `views` phase was shorter than its parts (clock
granularity; `other` reported as 0); `orphans`-class counters, `clock_*`,
`unmatched`, `early` and `foreign` should be zero in a healthy run. Off,
nothing is installed and the frame boundary is one relaxed load.

Verification: `python3 verification/probe/verify_residual_phase_sites.py`
(exact bytes, whole instructions of both gap-free routine decodes, the exact
incoming-edge sets, plain copy, the Begin-dispatch and particles-call
anchors, the `je` landing on the span end, every jump between
`view_submit_end` and the span staying inside that range, raw
interior-encoding sweep of `.text`, no data reference, disjoint from the 61
installed game/frame/pass sites, the scene hook and the point-light patch,
EXE identity); `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py
python3 verification/probe/run_game_phase_cpu.py` (both exact spans on a
synthetic material-plus-view body with the pass group on the same body and
the frame group on the frame body, so the pairing uses the real retained
clocks and the frame boundary runs through `frame_phases::frame`: hostile CPU
state parity, early and foreign stamps ignored, the frame's first material
skipped, prepare/setup pairing, a skipped pass loop, the pending setup closed
at the boundary, `other` or its underflow, rollback, byte-mismatch refusal,
duplicate-claim rollback, late-window refusal, the benchmark row); host
`verification/analysis/test_residual_phases.py` (`residual_phases_host.cpp`
accumulator/gate/window probe, wiring, launcher) and the arena accounting in
`test_game_phase_sites.py` (production arena 24,576 B, 16,380 modelled with
every optional group on, 8,196 B free). Ledger:

| Date | Change | Checks | Result |
| --- | --- | --- | --- |
| 2026-09-17 | Residual group added (two sites, shared lean stub, `--residual-phases`); production arena 20,480 -> 24,576 B (the accounting would still fit in 20,480 with 4,100 B free; the page is headroom); pass accumulator retains `end_clock`/`begin_clock`, frame tracker retains `submit_end` | `verify_residual_phase_sites.py` PASS (61 installed sites checked disjoint), the game/frame/pass/loop verifiers PASS; `run_game_phase_cpu.py` under X3 (worktree build, `fixture_sha256 377ac95e`): 8839 checks, 0 failures, `RESIDUAL PHASE BENCH dispatch_ns=90.7` (92.7 in a first run; documented 91), `PASS PHASE BENCH dispatch_ns=95.8` (91.5 in the first run, 89.1-92.3 in the previous four runs: the retained-clock branch is within noise), `LOOP PHASE BENCH dispatch_ns=91.7`, fixture arena 23,876/32,768 B; host `test_residual_phases` 9 tests OK (`residual_phases_host` probe), `test_game_phase_sites` + `test_media_cue` 26 tests OK; DLL RelWithDebInfo 0 warnings, `check_no_x87.py` 0 violations with `_x3m_residual_phase_enter` walked (505 reachable) | not yet run in the game |

## Attribution options: segment threshold and per-draw cost (2026-09-18)

Two launcher options serve the run of `docs/architecture/engine-frame-time.md`
section 3. `--game-phase-threshold-ms N` (requires `--game-phases`, 1..10000,
default **20**, written as `X3M_GAME_PHASE_THRESHOLD_MS` on every launch so an
inherited value cannot change it) sets the frame time at or above which the
game-phase group stages its segment tape, so a 25-45 ms frame now produces a
`game_phase_slow_frame` line and its up to 96 `game_phase_segment` rows; the
recorder keeps its built-in 50 ms when the variable is absent or out of range
(`frame_threshold` 0 in `game_phases_core.h`, one branch per frame in
`bridge()`, nothing extra when the option is off), and the chosen value is
echoed on the `game_phase_mode` line as `frame_threshold_ms=`. A low threshold
on a steadily slow scene is verbose: 96 rows per qualifying frame.
`--telemetry-draw` (requires `--telemetry`) sets `X3M_TELEMETRY_DRAW=1`, which
the DLL already honours: the per-draw metrics add `gate_us`, `route_draw_us`,
`set_rt_us`, `lazy_flush_us` and `jitter_us` to the `motion_output_frame` line
(off, the route takes no QPC stamp per draw; each stamp is a Wine syscall,
`docs/verification/route-cost-run1.md`). Run recipe: `python3 tools/manage.py
launch --bottle X3 --direct --motion-output --hdr --telemetry --frame-phases
--loop-phases --game-phases --pass-phases --residual-phases --telemetry-draw
--frame-end-stride 10 --capture-frames 0`, stand in the run125 corvette area
(90 s), empty space (30 s) as control and the run117 station view (60 s); read
`loop_phases` intervals against `input_p50_us`, `residual_phases`
`prepare`/`other`, `pass_phases` apply/draw, the new `game_phase_segment` rows
and `motion_output_frame` `gate_us`/`route_draw_us`/`set_rt_us`. Host checks:
`verification/analysis/test_game_phases.py` (the core probe stages a 25 ms
synthetic frame at the 20 ms threshold and nothing at the built-in 50 ms; both
launcher options, their refusals and the production wiring).

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

Light envelope on the binding and draw hooks (state-call-fast-path.md,
Envelope; worktree DLL `207d4ede`, record overwritten in place): SetStreamSource
15.9 / 133.3 / 371.5, the draw pair 389.2 / 1275.8 / 1877.2, and the fixture's
`PRESERVE` rows show the seeded x87 image, MXCSR and LastError unchanged
through every hooked binding and draw call. `lock_wait` (`HeldHookLock`) is now sampled only by the remaining `HookGuard` users: begin_scene, end_scene, present, reset, clear, set_rt, set_depth, get_rt, get_rt_data, stretch_rect, color_fill, update_surface, update_texture, the create_* resource/shader/query/state-block entries, query_issue/query_release, stateblock begin/end/apply/release, get_render_state, draw_rect_patch/draw_tri_patch, the cursor hooks and device/factory release; its count per interval drops by the draw and binding calls (run87: most of the 249 per second), which tools/analysis/analyze_iteration07_taa.py, analyze_iteration09_cost.py and analyze_iteration10.py read as `lock_wait`.

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

### Run 31 session A (run89), 2026-09-17

Session `/tmp/x3-bottleX3-run89/session-20260916-074655-212.log` (68,530
lines), `--frame-timing --frame-phases --telemetry`, state unstamped
(`state_us=-1` throughout). Identity line 2: sha256 `a9ebfa3b...`, source
`4adf3dd3...`, matching the installed candidate; options line 3 confirm
`FRAME_TIMING=1`, `FRAME_PHASES=1`, `BLOOM_SOURCE_CLAMP=1.0`,
`EMISSION_SOURCE_GAIN=2.0`, `SCREEN_EMISSION_ADDITIVE=2.0`/`_ALPHA=0.0`.
Install (line 11): `status=ok sites=10`, all ten `frame_phase_site` lines
`patched=1 status=active`. 38/38 `frame_timing`/`frame_phases` windows, no
`truncated=1`; `order_errors`/`clock_errors`/`unmatched`/`dropped`/`early`/
`foreign` all 0 everywhere. `incomplete`: `frame=300` incomplete=3,
`frame=600` incomplete=1 (both in the opening/loading windows), 0 elsewhere.

Busy window `frame=5700` (`draws_p50=987`, the session's highest) vs. empty
window `frame=6900` (`draws_p50=51`, low-draw plateau of windows 20-37):

| Window | dt p50/p95 | draws_p50 | state_calls_p50 | draw p50 | draw_native p50 | scene p50 | gap_pre/draw/post p50 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| busy 5700 | 37.48/41.69 ms | 987 | 61,896 | 7.93 ms | 2.53 ms | 1.25 ms | 4.34/23.44/0.45 ms |
| empty 6900 | 6.80/13.52 ms | 51 | 2,501 | 0.38 ms | 0.12 ms | 1.07 ms | 3.42/1.66/0.15 ms |

run87: 28.5 ms p50 at 457 draws, ~30,076 state calls, 8.9 ms *stamped* state
bucket; run89 has no state stamps, so the state bucket is not comparable
(folded into `gap_draw_us`). Proxy share (draw+scene only) is 9.18 ms of
37.48 ms (24.5%). run89's busy frame is slower (37.5 vs 28.5 ms) at more than
double the scene load (987 draws/61,896 state calls vs 457/30,076) — not a
matched-load comparison. `state_top` busy: `set_sampler_state:31149,
set_render_state:18449, set_texture:4451, set_vs:987, set_ps:987,
set_declaration:984`, `state_other_p50=993`; empty: `set_sampler_state:1049,
set_render_state:899, set_texture:151, set_vs:51, set_ps:51,
set_declaration:48`, `state_other_p50=56`. Worst slow-call witness:
`frame=5642 dt_us=88635 slow_call=draw_indexed slow_call_us=1702`.

Frame-phase medians (us):

| Phase | busy p50/p95 | empty p50/p95 |
| --- | --- | --- |
| pre_render | 4057/5703 | 2946/9777 |
| prologue | 49/113 | 48/69 |
| scene_update | 65/170 | 57/69 |
| begin_scene | 308/392 | 281/313 |
| views | 32505/35419 | 3205/3418 |
| overlays | 171/186 | 2/2 |
| text | 1/1 | 1/1 |
| scene_end | 265/366 | 100/190 |
| present | 12/15 | 9/11 |
| view_setup (sum) | 1810/2064 | 400/484 |
| view_submit (sum) | 29396/31754 | 1656/1737 |

Sum of nine phases: busy 37,433 us vs `dt_p50=37,498`; empty 6,649 us vs
`dt_p50=6,794` (both match within rounding). `views` dominates the busy
frame (32.5/37.5 ms, 87%), almost all `view_submit` (29.4 ms) against
`view_setup` (1.8 ms). `scene_update` is 65 us busy vs 57 us empty —
negligible — so the busy/empty gap is carried by `views`/`view_submit`
(submission and its O(n^2) sort at `0x0047e620`), not the render-list build.

No `arena_full`, claim failure, `chase_restore` refusal, or `shader_unknown` anywhere in the log; `shader_population` reports `unknown=0 overflow=0` at
all three cadence points (lines 2243, 5121, 27669). `lock_wait` values are
small (max 6.5 us seen). No crash/assert lines.

### Run 32 session A1 (run91), 2026-09-17

Session `/tmp/x3-bottleX3-run91/session-20260916-165038-212.log` (57,979
lines), `--frame-timing --frame-phases --telemetry`, launcher default
`--state-shadow auto` (no `X3M_STATE_SHADOW` in env). Identity:
`proxy_identity sha256=11c1f119...` `source_commit=baee232...`, matching
`docs/status.md` "Installed build". `state_hooks device=1 installed=1
reason=frame_timing state_shadow=1 rs_mode=shadow`; `frame_phase_mode
requested=1 enabled=1 status=ok sites=10 window=300`.

Busy `frame=4200` (draws_p50=1006, closest to run89's 987) vs. empty
`frame=6900` (draws_p50=75, lowest plateau, windows 5700-9900):

| Window | dt p50/p95 | draws_p50 | state_calls_p50 | draw p50 | draw_native p50 | scene p50 | gap_pre/draw/post p50 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| busy 4200 | 37.32/39.78 ms | 1006 | 63,311 | 7.75 ms | 2.52 ms | 1.19 ms | 3.99/23.26/0.40 ms |
| empty 6900 | 7.14/13.98 ms | 75 | 3,892 | 0.56 ms | 0.20 ms | 1.04 ms | 3.23/2.16/0.14 ms |

Both windows have `reason=frame_timing` (hooks installed) — comparable to
run89 (also hooked), not an unhooked baseline. Busy tracks run89's busy
(37.5 ms/987/61,896); empty same shape, not identical (run89 6.8 ms/51 vs
here 7.1 ms/75). `views_p50` busy/empty 32,115/3,857 us (`view_submit_p50`
29,191/2,330); phase-sum matches `dt_p50` in both.

Redundancy, busy 4200 (rs/ss/tex order): `state_redundant=2754453,1590735,
534520`, `state_shadowed=2902118,1601271,1329862`, `redundant_top=
7:292241,25:291642,24:291639,14:287371` (D3DRS_ZFUNC=7, ZWRITEENABLE=14;
24/25 need the RS-index table, not in this doc). Ratios: rs 94.9%, ss
99.3%, tex 40.2% — per-window cumulative counters vs. a single-frame
median `state_calls_p50`, different units, not a valid fraction of 63,311.
Empty 6900: `state_redundant=201990,86295,30396`, `state_shadowed=216390,
88395,73596` (~93/97/41%, same shape).

Program pairs, busy 4200 (`draws=292841 draw_pairs_overflow=0`), top-8:
`53a0a641.../8759c783...:113,282` (argon hull DEFAULT, exact pair),
`4944d81d.../ca6bfa4a...:38,177` (argon hull BUMPMAP, exact pair),
`37c34a74.../5f82ecac...:37,520`, `494fe349.../fffdabd9...:24,036`,
`37c34a74.../f1b0e820...:10,608` (`XT_standard_lighting.fx`; the
`494fe349.../fffdabd9...` pair is the XT DEFAULT left original by design),
`53a0a641.../63f96eba...:23,509` and `4944d81d.../5e0a10fe...:9,073`
(the two sun-lane cutout pairs, `cutout_pairs=9073,23509` matches: **the
cutout programs draw in this busy Argon view**, about 108 draws per frame,
so run 32 session B belongs here, not at a station of another type),
`c78b4c68.../none:8,012` (the `z_only` depth-prepass vertex shader with a
null pixel shader, `depth_prepass_profiles.h`). Empty 6900 (`draws=22599`):
`4944d81d.../ca6bfa4a...:4,500` tied `53a0a641.../8759c783...:4,500`,
`7b6393fe.../6109cf64...:4,200`, two engine-glow pairs (`494fe349.../
7c83ed50...:2,799`, `4944d81d.../0c1f3f0f...:1,800`), engine/gate
`d5e1c753.../8360f422...:1,200`, two more at 600; `cutout_pairs=600,300`.
No overflow either window.

Batchability: busy 4200 `same_mesh=14677 same_mesh_any_range=0
same_material=17644 draws=292841` — 5.0% instancing candidates, 6.0%
material-sortable. Empty 6900: `same_mesh=3079 same_material=920
draws=22599` — 13.6% instancing candidates, 4.1% material-sortable.

No anomalies: `truncated=1`, `shader_unknown`, `chase_restore`,
`arena_full`, `claim_fail`, crash/assert/ERROR/FATAL all 0.
`shader_population unknown=0 overflow=0` throughout (last known=57).
`lock_wait` max 18.9 us. Error-path refusal counters (`anchor_refused`,
`arm_refusals`, `origin_refusals_total`, `last_refusal`) all 0;
`identity_refused`/`identity_valid` and one `bloom_refusal
reason=scene_handoff count=1` are ordinary diagnostic counters, not
failures. `rs_mode=shadow` throughout (167 lines).

### Run 32 session A2 (run92), 2026-09-17

Preserved `/tmp/x3-bottleX3-run92/session-20260916-165414-216.log`. Identity
matches: `proxy_identity sha256=11c1f119...` `source_commit=baee232...`.
`proxy_options` has no `X3M_STATE_SHADOW`, `X3M_FRAME_TIMING=0`,
`X3M_FRAME_PHASES=1`. Line 122: `state_hooks device=1 installed=0
reason=none state_shadow=0 rs_mode=get` — hooks correctly absent under
`--state-shadow auto` with no `--frame-timing`; `rs_gets=0 rs_hits=0
rs_queries=0` (unshadowed Get* path, not tracked by shadow counters).

175 `frame_phases` lines, 35 full windows. Busy `frame=5400` (highest
`views_p50_us`) vs. empty `frame=7800`:

| Window | dt p50/p95 | begin_scene p50 | views p50/p95 | view_setup p50 | view_submit p50/p95 |
| --- | --- | --- | --- | --- | --- |
| busy 5400 | 26.49/28.09 ms | 291 us | 22,155/23,026 us | 1,699 us | 19,191/19,880 us |
| empty 7800 | 6.59/13.38 ms | 255 us | 2,977 us | 460 us | 1,448 us |

No `draws_p50` field exists without `--frame-timing`; `views_p50` count (=9)
is the view-loop count, not draws, so busy/empty here is not draw-confirmed.
vs. run89 busy (37.48 ms hooked): dt −10.99 ms (−29.3%), views −31.8%,
view_submit −34.7%. vs. run91 busy (37.32 ms): dt −10.83 ms (−29.0%).
Consistent with skipping run91's ~63,311 state calls/frame plus no hook
install, but sessions are not matched-load (no draw counter), so this is a
plausible attribution, not proven.

No anomalies: `truncated=1`, `capability`/`claim` failures, `chase_restore`
refusals, `shader_unknown` all 0/absent; `shader_population unknown=0
overflow=0` (last `known=57`). `incomplete`: 2 windows at 1, 1 at 3 (opening
only), 32 at 0. `mip_bias_sets=restores=8,091,791 failures=0` (final
summary) — no leak. No `motion_state_lost`/`restore_failures`/
`apply_failures` nonzero. `lock_wait` samples max 2–3 us.

### Run 32 session B (run93): slow sectors

`/tmp/x3-bottleX3-run93/session-20260916-165735-212.log` (206,769 lines), DLL
`11c1f119…`. Sectors by `chase_transition_event` `sector=` changes (log
lines): s1 5514-28864 (`0x1acc9810`), s2 28875-169345 (`0x5fc9d188`), s3
169356-198742 (`0x1ae49060`), s4 198753-end (`0x998c0238`).

Top 8 `frame_timing` windows by `dt_p50_us` (line, frame, sector, dt_p50/p95,
draws_p50, state_calls_p50, gap_pre/draw/post p50, µs):

| line | frame | sector | dt_p50 | dt_p95 | draws_p50 | state_calls_p50 | gap_pre | gap_draw | gap_post |
|---|---|---|---|---|---|---|---|---|---|
| 187749 | 26400 | 3 | 421745 | 513480 | 151 | 7962 | 411935 | 3995 | 281 |
| 10168 | 1200 | 1 | 32843 | 36118 | 760 | 49321 | 5091 | 18901 | 471 |
| 177197 | 25500 | 3 | 27981 | 31261 | 851 | 45197 | 4253 | 16550 | 249 |
| 174654 | 25200 | 3 | 26014 | 31277 | 794 | 40681 | 3880 | 15339 | 250 |
| 169308 | 24600 | 2 | 24811 | 27735 | 804 | 39041 | 3029 | 14649 | 217 |
| 12199 | 1500 | 1 | 23217 | 26700 | 461 | 30210 | 5531 | 11831 | 341 |
| 180018 | 25800 | 3 | 22721 | 26102 | 581 | 32469 | 4442 | 12161 | 243 |
| 201609 | 28200 | 4 | 21756 | 23761 | 513 | 31477 | 4433 | 11123 | 234 |

`frame_phases` for the busy windows (1200, 25200/25500/25800): `views` is
85-87 % of `dt` (e.g. 1200: views 27627/dt 32843), matching run91's
busy-render shape — not script-side.

**Frame=26400 answers the decisive question**: `pre_render` (Present-return
through input/message pump, script VM, deferred callbacks, simulation/AI,
cockpit update — line 545 above) is 411,427/421,745 µs = 97.6 % of the
window; `views_p50_us`=6,787 (1.6 %). `frame_phases_slow` confirms with 8
individual frames across 25827-26292 (not one glitch), all pre_render 90-98 %
of dt: 25827 (542,062/528,617), 26085 (452,156/435,675), 26080
(443,057/423,106), 25882 (435,052/418,126), 26285 (575,332/520,364), 26203
(567,812/556,514), 26286 (545,591/537,395), 26292 (537,855/529,473). No
`chase_transition_event` falls in that line range (nearest ~5514-10202 and
~198703-201609) — not a loading/transit spike, but sustained slowness inside
sector 3, outside rendering, in the phase covering the game's script/
simulation/AI update. Matches the user's report of slowdown in a
not-visually-busy sector and their memory of the same on vanilla Windows.
The log has no `--game-phases` subdivision, so which sub-phase inside
`pre_render` is responsible cannot be resolved further; a follow-up launch
adding `--game-phases` to the current flags would attribute it.

Loading vs sustained: windows before (24600-26100, draws_p50 497-851) and
after (26700-27900, draws_p50 69-145) the spike are normal busy/quiet render
frames, `incomplete=0` throughout — the spike is not loading. No anomalies:
`truncated=1`, `shader_unknown`, `motion_state_lost`, `lock_wait` outliers, or
`mip_bias_failures`; `incomplete` sums to 4 across all 95 windows (opening
only).

### Run 32 session C (run94): slow sector sub-phases

Preserved log `/tmp/x3-bottleX3-run94/session-20260916-171827-212.log` (82,225
lines), installed DLL `11c1f119…` from `baee232`, command = run 32 session A1
(`--frame-timing --frame-phases --telemetry`) plus `--game-phases`
(`X3M_GAME_PHASES=1`). Identity: `game_phase_mode requested=1 enabled=1
status=ok sites=33 ... frame_threshold_ms=50 call_threshold_ms=10 tape=96`;
`frame_phase_mode requested=1 enabled=1 status=ok sites=10 window=300`.

**Windows (dt_p50 top, plus the slow one).** The slow sector is the
`frame=12000` window: `slow=111` frames over 50 ms (all other top-dt_p50
windows have `slow≤1`).

| frame window | dt_p50_us | dt_p95_us | draws_p50 | pre_render_p50/p95_us | views_p50/p95_us |
|---|---:|---:|---:|---:|---:|
| 900 | 29106 | 34098 | 727 | — | — |
| 1200 | 25425 | 35834 | 547 | — | — |
| **12000** | 24715 | **426097** | 472 | 5842 / **413588** | 14196 / 19599 |
| 11700 | 24448 | 28371 | 692 | 5377 / 7949 | 18958 / 20322 |

`frame_phases frame=12000`: `pre_render_p95_us=413588` dominates `dt_p95_us=426151`
(97 %); `views_p95_us=19599` is normal. Reproduces run93's shape: pre_render
dominant, not views.

**Decisive sub-phase (`--game-phases`, 111 slow frames, `game_phase_slow_frame`
frame 11890–12006, `game_phase_segment`).** Total covered time 44,805.2 ms;
one phase owns nearly all of it:

| phase (site) | n | p50_us | p95_us | total_ms | share |
|---|---:|---:|---:|---:|---:|
| **Input** (`game_phase_input` 0x00403b09, region [403b09,403f2a)) | 333 | 1350 | 403778 | 42873.5 | **95.7 %** |
| Render (`0x00403f34`) | 111 | 11973 | 15210 | 1384.4 | 3.1 % |
| PendingVm (script VM, `0x00403aff`) | 111 | 2757 | 7915 | 391.8 | 0.9 % |
| Cockpits | 111 | 437 | 789 | 54.0 | 0.1 % |
| all others | — | — | — | ~10 | <0.1 % |

Within Input, `input_part=0` alone carries p50 391,500 us / max 454,653 us
(total 42,701 ms) — essentially the whole slow frame; `input_part=1` (script/VM
sub-path) is 1,350 us p50, `input_part=2` negligible. Per
`docs/reverse-engineering/selection-native-vm.md`, phase 6 `[403b09,403f2a)` is
a broad region (registry/sector/object work, input/control/script dispatch, an
explicit input-wait branch, optional save) — calling it a pure "input" cost
overclaims. `game_phase_call_sample`/`game_phase_slow_call` (the call tape) has
only 5 rows in the whole log, none in frames 11700–12000, all `caller=00000000`:
no named engine call site is attributable inside the stall from this run: it is
an unattributed remainder inside the broad Input phase, not the script VM,
simulation/AI, cockpit update or deferred-callbacks group narrowly (PendingVm,
Simulation, Cockpits, Clock are all <1 % combined).

**Normal-frame comparison.** The diagnostic only emits `game_phase_segment` rows
for frames over `frame_threshold_ms=50`, so no true <50 ms frame has a segment
breakdown; the lightest recorded slow frame in the session (frame=3425, 50.8 ms
total) is dominated by **PendingVm 70.8 %** and Render 24.7 %, with Input only
2.5 % — the opposite ranking from the sector stall, where Input is 95.7 % and
PendingVm <1 %. This is the closest available before/after contrast and shows
Input is what grew.

**Anomalies.** No `arena_full`, `bytes_mismatch`, `truncated`, `shader_unknown`
or chase refusals anywhere in the log. `lock_wait` (616 hits) is an unrelated
`telemetry_metric`, all `failures=0`, microsecond-scale. `game_phase_window
unmatched=3` appears twice: frame=326 (mid-session, isolated) and frame=12005
(`frames=0`, the window that closed at quit) — consistent with session
termination during the stall, not a claim failure.

### Run 33 session A (run95), 2026-09-17

Preserved `/tmp/x3-bottleX3-run95/session-20260916-214129-216.log`. Identity:
`proxy_identity sha256=03c0c9f4b69ceaa...` `source_commit=a3cafd515...`,
matching `docs/status.md` "Installed build". `proxy_options` carries
`X3M_FRAME_TIMING=0` (line 3), so `state_hooks device=1 installed=0
reason=none state_shadow=0 rs_mode=get` (line 127): the proxy runs unhooked,
production configuration. `frame_phase_mode requested=1 enabled=1 status=ok
sites=10` and `pass_phase_mode requested=1 enabled=1 status=ok sites=4
dispatch_cost_ns=91` (lines 11, 22).

Busy window frames 6300-7800 (six 300-frame samples, `view_submit_p50_us`
19.4-20.6 ms, the held view) vs. empty window frames 8100-9900 (quit,
`view_submit_p50_us` 1.3-1.7 ms):

| Window | dt p50/p95 | views p50 | view_submit p50 | passes_p50 | apply p50/p95 | draw p50/p95 | end p50/p95 | sum p50 | self p50 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| busy (avg of 6) | 26.9/29.2 ms | 23,072 us | 20,115 us | 981 | 6,537/6,920 us | 8,581/8,974 us | 111/120 us | 15,238 us | 357 us |
| empty (avg of 7) | 7.1/13.5 ms | 3,212 us | 1,475 us | 42 | 292/343 us | 370/429 us | 5/6 us | 667 us | 14 us |

Decisive split of busy `view_submit_p50=20,115 us`: apply (BeginPass) 6,537 us
(32.5%), draw (`DrawIndexedPrimitive`) 8,581 us (42.7%), end (`EndPass`) 111 us
(0.6%), remainder = view_submit − sum − self = 20,115 − 15,238 − 357 = 4,520 us
(22.5%, the engine's per-object work between passes: matrix/constant setup,
object iteration, not captured by any pass stamp). Per pass: apply 6.66
us/pass, draw 8.75 us/pass, end 0.11 us/pass (981 passes/frame) — draw, not
apply, dominates per-draw cost here; the earlier "apply loop dominates"
estimate (`docs/reverse-engineering/effect-pass-loop.md`) is not borne out at
this pass count.

Sanity: `sum_p50` (15,238) vs `apply+draw+end` (15,229) agree to 0.06%.
`self_p50` (357 us) = `passes_p50 × 4 sites × dispatch_cost_ns` = 981 × 4 ×
91 ns = 357,084 ns, exact. `orphans/unmatched/early/foreign/clock_errors/
clock_failures` are 0 across all 44 `pass_phases` lines; `dropped=1` total
(frame=300, startup transient). `frame_phases incomplete=4` total (startup/
shutdown transient), `order_errors/unmatched/dropped/early/foreign=0`. Busy
`passes_p50` 929-1,010 (avg 981) matches run91's ~1,006 draws (hooked) in
magnitude. No `claim_fail`, `arena_full`, `truncated`, `shader_unknown`,
`chase_refus*`, `motion_state_lost`; `mip_bias_failures=0` (223 lines).

### Run 33 session B (run96): slow sector loop split

Preserved `/tmp/x3-bottleX3-run96/session-20260916-214604-212.log` (98,459
lines). Identity: `loop_phase_mode ... status=ok sites=6 window=300
slow_threshold_us=50000 slow_limit=64 dispatch_cost_ns=91` (line 75);
`game_phase_mode ... sites=33` (11), `frame_phase_mode ... sites=10` (59),
`pass_phase_mode ... sites=4` (70); `state_hooks device=1 installed=0
reason=none` (182) - unhooked.

Stall spans frames ~13200-14700; worst window frame=14700 (`slow=143`):

| Window | dt p50/p95 | pre_render p50/p95 | sectors/containers p50 | collide p50/p95 | simulate p50/p95 | post p50/p95 | passb p50/p95 | sum p50 | input p50 | slow |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 13500 | 21.1/23.2ms | 3.2/9.4ms | 1/1 | 498/577us | 41/66us | 14/26us | 87/102us | 654us | 1,779us | 1 |
| 14100 | 17.9/21.3ms | 5.9/7.3ms | 1/1 | 451/476us | 69/79us | 15/65us | 86/100us | 623us | 1,732us | 1 |
| 14400 | 16.1/21.0ms | 3.9/6.7ms | 1/1 | 442/477us | 62/81us | 13/27us | 82/113us | 597us | 1,724us | 1 |
| **14700** | 18.9ms/**416.8ms** | 6.6/**405.3ms** | 1/1 | 442/474us | 69/82us | 28/**396,390us** | 110/178us | 762us | 2,317us | **143** |
| 15000 (normal) | 12.2/13.0ms | 2.6/3.4ms | 1/1 | 1,388/1,423us | 4/5us | 10/14us | 63/69us | 1,467us | 1,698us | 0 |

`max_interval_owner=post` in all 4 stall windows; 15000 owner=collide,
max_interval_us=3,098 (an order of magnitude below the stall).

**Decisive split.** Across all 70 `loop_phases_slow` lines (64 in
14400-14700, plus 5198/7095/7457/13222/14014/14346), `max_interval_owner=post`
in **70/70 (100%)**. In the 14400-14700 cluster:

| Interval | p50 us | p95 us | share of sum_us |
| --- | --- | --- | --- |
| collide | 437 | 459 | 0.1% |
| simulate | 75 | 85 | <0.1% |
| **post** | **381,385** | **388,893** | **99.8%** |
| passb | 160 | 185 | <0.1% |

`sum_p50=382,049us`, `input_p50=383,330us`; remainder p50/p95 = 1,346/1,925us
(<0.6%): the six stamps account for essentially the whole Input segment.
`sectors=1, containers=1` on all 70 slow lines: one sector walked per slow
frame; its `sector_post` (`0x0043a39a`, calls `0x0045b720` global object
pass, `docs/reverse-engineering/main-loop-input-region.md` line 128) stalls.

**Sanity.** `orphans/unmatched/clock_errors/clock_failures/early/foreign=0`
over all 50 `loop_phases` windows; `dropped=1` at startup (frame=300) only.
No `claim_fail`/`arena_full`/`truncated`/`shader_unknown`/`chase_refus*`.
`self_p50_us=0` (50/50), consistent with `6 x sectors_p50(1) x 91ns = 546ns`.

### Run 33 session B (run96): audio correlation

No launcher/Wine stderr capture exists in run96/run94/run95 (59-63 files each:
`session-*.log`, `ps_*.bin`/`vs_*.bin`, `loading-intervals-*.bin`; no
`wine.log`/`x3run*.log`/stderr file) - the reported GStreamer bursts cannot be
aligned to frames from this evidence; **open, not settled**.

`grep -ci gst` = **0** in all three session logs; no GStreamer token is
recorded by the proxy itself.

`voice_dmo_fallback` (only voice/dmo token) totals **6 activations per
session**, not per-frame: run96 frames 4, 584, 14695; run94 frames ~4, ~584,
12003-12005; run95 frames ~4, ~584, 13265-13267. Activations 5/6 land at the
tail of each stall (run96 stall ~13200-14700, `game_phase_slow_frame` last
covers 14693 before covered_ticks drops to ~0.8M at 14696, activation at
14695; run94 stall's last slow frame is 12003, `covered_ticks=4083429`,
activation at 12003-12005) - but the same event also fires in run95 at a
comparable frame (13265-13267) despite **zero** `game_phase_slow_frame` lines
there (no stall). The event tracks a scripted/sector trigger present with or
without the stall, not per-frame stream churn.

No `.wav`/`.mp3`/`.ogg`/`.wma` path appears in any log
(`grep -oE '(path|name|file)=\S*\.(wav|mp3|ogg|wma)'` = 0); `resource`/
`dat_handle_pool_metric`/`loading_*` counters carry no filename field, so
per-file audio opens during the stall cannot be attributed from this
telemetry.

`--game-phases` has no audio-named sites: `game_phase_metric` uses numeric
`category=phase index=N`; run96's only `game_phase_segment` site values are
`not_resolved` (1046x) and raw addresses. No `game_phase_call_sample`/
`game_phase_slow_call` token exists in this build.

**Conclusion: hypothesis not supported.** No GStreamer/audio failure signal
and no per-frame voice-stream churn during the stall in run96 or run94; the
one audio event repeats once per session near a sector transition in
stalling and non-stalling sessions alike. Settling the reported GStreamer
bursts needs a new diagnostic: pipe launcher stderr to a file under the run
directory on the next launch so CRITICAL lines share the session's QPC clock
with `loop_phases_slow`/`game_phase_slow_frame`.

#### Audio correlation: aligning a terminal burst with a frame

The GStreamer/GLib messages the user sees during a stall are printed by the
child process on its terminal and carry a **local** wall-clock stamp
(`HH:MM:SS.mmm`); the proxy's lines carry frame numbers and QPC ticks. Both are
preserved in one run directory now:

- `tools/manage.py launch` tees the child's stdout and stderr to
  `<game>\x3-modern-captures\launcher-stderr.log` (one fresh file per launch),
  prefixing every line with the launcher's own UTC stamp
  `[YYYY-MM-DDTHH:MM:SS.mmmZ]`. The terminal output is unchanged. The first
  line is `launcher_tee pid=<launcher pid> log=<path>`, so a file replaced by a
  second concurrent launch still names its writer; the pump drains the child's
  pipe even when a sink fails, so the game never blocks on a full pipe.
  `tools/analysis/snapshot_x3_run.py` copies that file next to the session log
  and counts it among the referenced files; `--dry-run` prints the path
  (`launcher_stderr`) without creating anything.
- The session log opens with
  `clock_anchor utc=<ISO8601 with ms> qpc=<ticks> qpc_frequency=<Hz> local_offset_min=<minutes>`
  (one `GetSystemTimePreciseAsFileTime` reading next to one
  `QueryPerformanceCounter` reading; `local_offset_min` is the negated
  `GetTimeZoneInformation` bias, i.e. `local = UTC + local_offset_min`).
- `frame_timing`, `frame_phases`, `pass_phases` and `loop_phases` window lines
  and the `loop_phases_slow` lines carry `qpc=<ticks at emission>`
  (`frame_timing` reuses the frame-boundary stamp; the others read the counter
  once per window). `game_phase_slow_frame` already carries `qpc_begin=` /
  `qpc_end=` for its own frame and needs no extra field.

Recipe, from a GLib line to a frame:

1. Prefer the launcher's `[...Z]` prefix on that line; it is already UTC. Use
   the GLib local stamp only as a cross-check:
   `utc = local - local_offset_min` (minutes), date taken from the anchor.
2. `qpc = anchor.qpc + (utc - anchor.utc) * qpc_frequency` (seconds).
3. Find the window line whose `qpc=` is nearest and ahead of that value: its
   `frame=` closes a 300-frame window, so the burst falls in
   `(previous window qpc, this window qpc]`. Within a window, interpolate with
   the per-frame `dt_p50_us`, or use the `loop_phases_slow` lines of that
   window (same emission `qpc=`) whose `frame=` values name the slow frames.

Two clock caveats: the anchor pairs the two clocks once at attach, so a long
session inherits any drift between the system clock and the performance counter
(seconds per hour at worst, well inside a 300-frame window); and the launcher's
stamp is taken when the line is read from the pipe, after the child's own
buffering, so it is an upper bound on the message's own time.

### Run 33 session C (run97): linear-material cost

Same busy Argon view as run95, now with `--linear-materials
--linear-distance-fade --sun-shadow-lane` active (`/tmp/x3-bottleX3-run97`,
log lines 31404-38331, frames 4500/4800/5100/5400). `dt_p50_us` ~29.0-29.8k
(p50 ~29ms) vs run95's 26.5ms (+~2.5-3.3ms, +9-12%); `view_submit_p50_us`
~21.3-22.2k (~21.7ms) vs run95's 20.1ms (+~1.2-2.1ms, +6-10%). Per-pass cost
(passes_p50 ~953-984): `apply_p50_us`/passes ~6.65-6.99µs vs run95's 6.7µs
(roughly flat); `draw_p50_us`/passes ~9.9-10.4µs vs run95's 8.7µs
(+~1.2-1.7µs/pass, +14-20%). The extra draw-phase cost tracks the
`cutout_opaque_routed` lane draws (p50 90/frame in this window, see
directional-shadows.md run97 entry); apply-phase cost is not materially
higher, consistent with the lane reusing existing material state rather
than adding per-pass setup.

### Run 35 session A (run103), 2026-09-17: D3DX builtin vs native

`/tmp/x3-bottleX3-run103`, the installed run34 build launched with
`--d3dx builtin` (override string `d3d9=n,b;d3dx9_37=b`, which reached the
Windows process intact). Visuals were unchanged; nothing failed to render.

**The identity line could not prove which D3DX loaded.** The `d3dx9_37`
`loaded_module` line read `path=C:\X3\d3dx9_37.dll size=3786760`, identical to
a native run. A probe EXE showed why: with `WINEDLLOVERRIDES=d3dx9_37=b` and a
native copy beside the EXE, Wine loads the builtin but keeps the module's
`FullDllName` at the native path, so `GetModuleFileName` and the on-disk size
are the native file's. Only the mapped image differs: native
`image_size=3895296 stamp=47cdef5d`, builtin `image_size=585728 stamp=00000000`,
and the builtin carries the string `Wine builtin DLL` inside the first 0x80
bytes of the module base. The `loaded_module` line now reports `image_size=`,
`stamp=`, `exports=` and `wine_builtin=` for exactly this reason.

Numbers, over 14 busy windows matched to run95 by pass count (±15 %), medians
per pass: BeginPass 8.90 µs (run95 6.64), draw 9.21 (8.74), engine between
passes 5.42 (4.89); `dt` p50 27.4 ms (run95 22.9 ms). The empty view gave
≈8.7 µs vs run95's ≈6.9–7.0 µs. `pass_phases` health was clean: 0 orphans,
0 clock errors, 1 dropped window in 60.

The comparison is confounded twice: different builds (run95 is the run33 build)
and unproven module identity. It repeats as **run 36 A1/A2** on one build with
the new identity fields. The 25 "zero area" lines at 23:10:38 are exit-time
teardown noise (run98 had 31 of them).

## Run 36 sessions A1/A2 (run104/run105), 2026-09-17: builtin vs native D3DX on one build

Both on the installed run36 build `51a3d764…`, same busy Argon view,
`--frame-phases --pass-phases`. Identity settled by the new `loaded_module`
fields: run104 (`--d3dx builtin`) `d3dx9_37.dll image_size=585728
stamp=00000000 wine_builtin=1`; run105 `image_size=3895296 stamp=47cdef5d
wine_builtin=0`; the launcher's first stderr line records the override
string in each. Busy windows matched by pass count (±15 %), 19 pairs;
empty-view 25 pairs. Medians per pass:

| Per pass | A1 builtin | A2 native |
|---|---|---|
| BeginPass (busy) | 8.71 µs | 6.58 µs |
| BeginPass (empty view) | 8.22 µs | 6.33 µs |
| Draw | 8.99 µs | 8.76 µs |
| Engine between passes | 4.77 µs | 4.45 µs |
| EndPass | 0.111 µs | 0.112 µs |
| Passes per frame p50 | 844 | 787 |
| Frame dt p50 | 25.9 ms | 22.2 ms |

Health clean in both (orphans 0, clock errors 0, dropped 1 startup transient;
no claim failures or unknown shaders; the 25 exit-time "zero area" lines and
the two GStreamer bursts as before). **Verdict:** Wine's builtin D3DX costs
about +2.1 to +2.4 µs per pass (+32 %) over the native redistributable on this
backend; native stays. Run103's 8.90 µs was the builtin after all. The
experiment is closed; the per-pass split stands at draw 8.8 / BeginPass 6.6 /
engine 4.5 µs on the native path.

## Run 37 sessions A1/A2 (run107/run108), 2026-09-17: FEX TSO off, wined3d CSMT off

Installed run37 build `61725145…`, same busy Argon view as run105, about
30 s each (6 and 5 busy windows matched to run105 by pass count ±15 %).

| Per pass (busy) | run105 reference | A1 `FEX_TSOENABLED=0` | A2 `csmt=0x0` |
|---|---|---|---|
| BeginPass | 6.58 µs | 6.53 µs | 6.57 µs |
| Draw | 8.76 µs | 8.68 µs | 17.63 µs |
| Engine between passes | 4.45 µs | 4.45 µs | 4.39 µs |
| EndPass | 0.112 µs | 0.110 µs | 0.119 µs |
| Frame dt p50 | 22.2 ms | 22.8 ms | 32.5 ms |

Health clean in both (orphans 0, clock errors 0, one startup `dropped`; no
claim failures or unknown shaders; the usual GStreamer bursts and 25
exit-time "zero area" lines). The launcher header records `fex_tso=off` /
`wined3d=csmt=0x0`; the proxy logs only `X3M_*` variables, so in-process
delivery is attested by the launcher line and, for A2, by the effect itself.

**Verdicts.** Disabling wined3d's command-stream thread doubles the draw call
(8.8 to 17.6 µs per pass) and adds 46 % to the frame: CSMT stays on. Relaxing
FEX's memory ordering changes nothing measurable (every emulated bucket within
1 %); the likely reason is that on Apple silicon FEX uses the hardware TSO
mode, so strong ordering costs nothing to begin with and there is no lever.
Both environment experiments are closed; the busy frame's levers are back to
code (pass replay, which now needs an FXLC evaluator per the classification).

## Native BeginPass attribution fixture (2026-09-17, no game)

`verification/probe/effect_beginpass_fixture.cpp` +
`run_effect_beginpass.py`; result
`verification/results/bottle-X3/effect-beginpass.json`, host test
`verification/analysis/test_effect_beginpass.py` (11 tests). One synthetic
windowed HAL device (64×64, no draws), the game's own compiled effect
`shader/3_0/argon.fb` (`addon/01.cat`, 41,004 bytes,
`bc504b32dee758b1…`) read out of the bottle's archives into a scratch
directory outside the repository, technique `DEFAULT` pass `P0`, and the
game's own `d3dx9_37.dll` (3,786,760 bytes) selected per case by
`--dll d3dx9_37=n|b`. The loop is the game's (effect-pass-loop.md §2):
`Begin(&passes, D3DXFX_DONOTSAVESTATE)`, `BeginPass`, `EndPass`, `End`, never
`CommitChanges`; only `BeginPass` is inside the QPC pair. A forwarding,
counting `ID3DXEffectStateManager` (the game installs one too) counts the
callbacks issued inside the timed call. 45 of 54 top-level parameters are
written per iteration in regimes (i)/(ii); 10,000 timed iterations ×
3 repetitions per regime after a 500-iteration warm-up; the table is the
median of the three per-repetition medians. Identity proven from the mapped
image, matching run104/105: native `image_size=3895296 stamp=47cdef5d
wine_builtin=0`, builtin `image_size=585728 stamp=00000000 wine_builtin=1`.

| Regime | native µs/BeginPass | builtin µs/BeginPass | Set\* block µs (native / builtin) |
|---|---|---|---|
| (i) same values re-set | 1.60 | 1.70 | 0.40 / 1.24 |
| (ii) values change | 3.30 | 3.70 | 0.57 / 1.42 |
| (iii) no `Set*` | 1.50 | 1.50 | 0.07 / 0.07 |

State-manager callbacks per `BeginPass`: **57 in every regime and both
implementations** — 19 render state, 28 sampler state, 4 texture, 1 vertex
shader, 1 pixel shader, 4 shader-constant calls (52 registers), 0 texture
stage, 0 other.

**What it settles.** (1) A same-value `Set*` costs +0.10 µs (native) /
+0.20 µs (builtin) inside `BeginPass` and **zero extra callbacks**: native
D3DX does not pay a dirty-driven re-evaluation for a write that does not
change the value, so regime (i) ≈ regime (iii). (2) A changed value costs
+1.8 µs (native) / +2.2 µs (builtin) per pass, all of it D3DX-internal
(expression/preshader evaluation and constant assembly): the callback count
does not move. (3) D3DX re-applies **all 57 pass states on every
`BeginPass`** regardless of dirtiness, so any replay must issue the same 57
device calls (or dedup them at the device level); they are paid either way.
(4) Wine's builtin is 6–13 % slower than native on `BeginPass` here and ~3×
slower on the parameter setters themselves (1.24 vs 0.40 µs per pass for the
45 writes) — the same direction as run104/105, much smaller in magnitude on
this idle device.

**Scope.** 1.5 µs is not the in-game 6.58 µs: this device sees no draws and
repeats the same device state, so wined3d's redundant-state filtering makes
the 57 forwarded calls cheap, and the fixture bounds the parameter-dirty and
D3DX-walk shares, not the wined3d share of the in-game number. The forwarding
manager adds one virtual call per callback to every regime equally.

## Run 38 session B (run113), 2026-09-17: prepare/setup split, GetTechniqueByName decision

Main build e575136 (not run38's installed candidate 5b4be52e), `--frame-phases
--pass-phases --residual-phases`, session log
`/tmp/x3-bottleX3-run113/session-20260917-075940-216.log` (12.9 MB, 44
300-frame windows, `residual_phase_mode ... sites=2 status=ok`, `pass_phase_mode
... sites=4 status=ok`). Selection: busy Argon-station plateau = windows 22-25
(`frame=6900..7800`, `dt_p50_us` 23.1-23.3 ms, `passes_p50` 823-827, stable
across four consecutive windows — the requested several-hundred-to-~1000-draw
busy view); peak window 18 (`frame=5700`, `dt_p50` 26.2 ms, 970 passes) noted
separately; quiet control = window 34 (`frame=10500`, `dt_p50` 6.34 ms, 60
passes).

**Per-phase stamps, busy plateau (windows 22-25, per-draw ns from the
per-pass fields, p50):**

| Phase | source stamp pair | per-draw | ms/frame (827 draws) |
|---|---|---|---|
| BeginPass (apply) | pass_begin→pass_applied | 6.57-6.58 µs | 5.42-5.43 ms |
| draw (Commit+DrawIndexedPrimitive) | pass_applied→pass_drawn | 8.71-8.77 µs | 7.17-7.24 ms |
| EndPass | pass_drawn→pass_end | 0.109-0.111 µs | 0.090-0.091 ms |
| **prepare** (engine walk + `GetTechniqueByName`+`SetTechnique`+`End`, mixed) | last pass_end→`0x004c1eab` | 6.35-6.38 µs | 5.22-5.26 ms |
| **setup** (`Begin`+param setters+2 RS writes+guard) | `0x004c1eab`→first pass_begin | 1.628-1.636 µs | 1.34-1.35 ms |
| particles | view_submit_end→particles-call return | 0.015-0.017 ms/frame total | ~0.017 ms |
| other (scene-end/env-map/fixups) | frame-boundary arithmetic | ~1.12 ms/frame total | 1.12 ms |

Sum check (window 22): pass `sum_p50_us` 12,679 + residual prepare 5,223 +
setup 1,340 + particles 15 + other 1,113 = 20,370 µs vs frame `views_p50_us`
19,414 µs (+4.9%, close to the combined stub `self_us` 299+74=373 µs plus
rounding) vs frame `dt_p50_us` 23,116 µs (views excludes pre_render 2.5-7.4 ms,
prologue/scene_update/begin_scene/overlays/text/scene_end/present, which the
frame group reports separately, not in the pass/residual chain). No overflow
or drop counter fired at steady state: `dropped=0`, `orphans=0`,
`clock_errors=0/clock_failures=0/unmatched=0` in every busy window;
`prepare_skipped=300` in every window (300 frames), i.e. exactly one per
frame — the documented first-material-of-frame case, not an anomaly. Window 0
(install transition) shows `dropped=1` on both groups once. The 24,576 B
arena figure is a static build-time budget (`test_game_phase_sites.py`), not
a runtime counter; no runtime arena-overflow counter exists in this log
format to check.

**The decisive number cannot be produced from this capture.** The
`residual_phases` group has exactly one stamp (`material_setup` at
`0x004c1eab`, effect-pass-loop.md §7) splitting the loop into two buckets:
`prepare` (last `pass_end` → `0x004c1eab`) and `setup` (`0x004c1eab` → first
`pass_begin`). Per §7's own call-histogram, `prepare` bundles the engine's
per-object work (queue walk `0x0047e6e0`/traversal `0x0047d9c0`, cull
`0x004f66e0`, world matrix `0x004bdee0`, this routine's prologue, three
engine-wrapper binds) together with `End` **and** the D3DX
`GetTechniqueByName`/`FindNextValidTechnique`/`SetTechnique` calls in one
6.35-6.38 µs/draw bucket at busy-view; there is no second stamp inside
`prepare` to separate them. `docs/architecture/effect-pass-replay.md` ("The
4.5 ms residual") already specifies the two additional stamps that would do
it — sub-mesh loop head `0x004c0223` and `Begin`'s return `0x004c1ec0` — and
this build does not install them. So: no ms/frame figure for
`GetTechniqueByName`+`SetTechnique` alone, and no per-draw technique-change
rate, can be read from run113; both require a new diagnostic build with
those two stamps (or the `sub-mesh head → Begin returned` interval alone),
one launch at the same busy view, before the handle-cache decision can be
made on evidence rather than the existing arithmetic estimate (a (a) 1-5 µs
+ (b) 0.3-0.8 µs D3DX share vs (c) engine-only remainder, itself only
plausible, not measured).

*Correction, 2026-09-17:* the paragraph above calls `0x004f66e0` the cull. It is the per-node
animated-texture stepper; the cull/LOD pass is `0x0047cfe0`
([shadow-caster-lifetime.md](../reverse-engineering/shadow-caster-lifetime.md) §0, §3). The
bucket arithmetic is unaffected; the entry is left as written.

**Quiet control (window 34, 60 draws/frame, 6.34 ms frame):** apply 6.58 µs,
draw 6.88 µs, end 0.117 µs — apply/draw per-draw cost is nearly identical to
the busy view (draws are the same shader work, just fewer of them). `prepare`
per-draw jumps to 29.97 µs, because `prepare` includes fixed per-frame
traversal/cull/prologue cost that does not shrink with draw count and here is
divided by only 60 materials — this is expected from the bucket's contents
(§7), not evidence about `GetTechniqueByName` cost, and is further proof the
bucket is dominated by non-technique-lookup work at low draw counts.

**Consistency with the earlier split.** Busy-plateau BeginPass 6.57-6.58 µs
matches the prior split's "6.6-6.7 µs" figure and run105's 6.58 µs
(effect-pass-replay.md) closely; draw 8.71-8.77 µs matches the prior "Wine
draw 8.7 µs" and run105's 8.76 µs. Draws/frame 823-970 across windows 18/22-25
matches the brief's "several hundred to ~1000" at ~22-26 ms frames. These are
diagnostic-timing numbers (telemetry stamp overhead `self_us` 373-441 µs/frame
included), not game FPS.

**Open issue.** The task as framed (split engine prepare vs
`GetTechniqueByName`+`SetTechnique`) needs a diagnostic this build does not
have; the existing capture only bounds the *combined* prepare+setup at
6.35-6.38 + 1.63 µs/draw busy (≈6.56-6.58 ms/frame combined at 827 draws) as
a loose upper bound on what any handle cache could touch, not a measurement
of the technique-lookup share.

## Technique lookup microbenchmark (2026-09-17, no game)

Answers the open issue above: what the engine's per-draw
`GetTechniqueByName` (`0x004c0bfa`) + `SetTechnique` (`0x004c0c34`) actually
cost, so the run 38 B `prepare` bucket (6.35-6.38 µs/draw) can be split
without another load/test cycle.
`verification/probe/effect_technique_lookup_fixture.cpp` +
`run_effect_technique_lookup.py`; result
`verification/results/bottle-X3/effect-technique-lookup.json`, host test
`verification/analysis/test_effect_technique_lookup.py` (6 tests). Bottle
**X3**, `WineArch=arm64`, `FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`. It
reuses the BeginPass fixture's effect loading and device: one synthetic
windowed HAL device (64×64, no draws), the game's own compiled effects read
out of the bottle's archives into a scratch directory outside the
repository, and the game's own `d3dx9_37.dll` (3,786,760 bytes,
`c2ccb84c672a9d89…`) via `--dll d3dx9_37=n`, proven native from the mapped
image (`image_size=3895296 stamp=47cdef5d exports=336 wine_builtin=0`, the
same image as the BeginPass fixture); a builtin image fails the run closed.
No state manager: none of these calls reaches the device.

**Method.** The bottle's QPC ticks at 0.1 µs, coarser than any of these
calls — a first run returned exactly 0.100 µs for every measurement
including an empty timed region. Timing is therefore batched: one QPC pair
around 100 identical calls, divided by 100; median and p90 are over the
1,000 batches of a repetition, and the table is the median of three
per-repetition medians. 100,000 timed calls per measurement and repetition
after a 400-call warm-up. The baseline is the same batch loop with an empty
body (loop + QPC pair), 0.001 µs/call, and is subtracted; a confirmation run
at batch 1,000 × 1,000,000 calls reproduced every net figure within
0.001 µs, so the divisor and per-batch overhead are not shaping the result.
The names are the engine's literal material technique names DEFAULT /
BUMPMAP / BUMPMAP_LOW (xt-materials.md `0x004c0996`-`0x004c0b85`); the miss
path (`FindNextValidTechnique`) is not timed because steady-state draws hit.

Net µs per call (median, p90 in parentheses), baseline subtracted:

| Effect (addon/01.cat) | techniques | gtbn DEFAULT | gtbn BUMPMAP | gtbn BUMPMAP_LOW | SetTechnique same | SetTechnique alternating | Begin+End |
|---|---|---|---|---|---|---|---|
| `argon.fb` 41,004 B (busy-view hull) | 2 | 0.004 (0.004) | 0.005 (0.005) | — | 0.004 (0.004) | 0.004 (0.004) | 0.005 (0.006) |
| `argon2s.fb` 41,236 B | 2 | 0.004 (0.005) | 0.005 (0.006) | — | 0.004 (0.004) | 0.004 (0.004) | 0.005 (0.006) |
| `standard_lighting.fb` 55,872 B | 3 | 0.004 (0.005) | 0.005 (0.006) | 0.009 (0.010) | 0.004 (0.005) | 0.004 (0.005) | 0.006 (0.006) |
| `xt_standard_lighting.fb` 70,172 B | 3 | 0.004 (0.004) | 0.005 (0.005) | 0.009 (0.009) | 0.004 (0.004) | 0.004 (0.004) | 0.005 (0.006) |

`GetTechniqueByName` rises with the technique's position in the declaration
order (4 / 5 / 9 ns for the 1st / 2nd / 3rd name) — a linear name search, as
expected — and does not grow with effect size. `SetTechnique` costs the same
whether the handle equals the current technique or alternates, i.e. native
D3DX does no work proportional to a technique change here, and there is no
"redundant call is free" and no "redundant call is expensive" effect to
exploit.

**Conclusion arithmetic.** At 825 draws per frame, per draw
`GetTechniqueByName` 0.0045 µs + redundant `SetTechnique` 0.004 µs =
0.0085 µs, so a perfect per-(effect, name) handle cache that removed the
lookup entirely and the redundant `SetTechnique` with it would save
0.0085 × 825 = **0.007 ms per frame** — 0.03 % of a 23 ms frame, and 0.13 %
of the 5.2 ms `prepare` bucket that contains them. Even including the
`End`/`Begin` bookends (0.005 µs each pair, 0.004 ms/frame), the whole D3DX
side of `prepare` is under 0.012 ms/frame. The 6.35-6.38 µs/draw of `prepare`
is therefore ≈99.8 % engine traversal, cull and wrapper binds, not D3DX
technique handling. **A technique-handle cache trampoline is not worth
building**, and the site hooks it would need (`0x004c0bfa` / `0x004c0c34`)
buy nothing measurable.

These are diagnostic microbenchmark timings on an idle synthetic device, not
game FPS; they bound the D3DX call cost, and the in-game call may differ by
cache state, but not by the three orders of magnitude the conclusion has in
hand.

**Run 42 A (run129, 2026-09-18).** Telemetry-only session on the run42 candidate (`--loop-phases --game-phases
--pass-phases --residual-phases --telemetry-draw --frame-end-stride 10`), corvette save. Stands from `frame_end`:
(a) the run125 24 fps area, frames 7000–9000, 89 s, dt p50 44.3 ms at 448 draws; (b) empty space, dt 8.4 ms at
70 draws; (c1) 558 draws, dt 26.7 ms; (c2) 865 draws, dt 35.2 ms (another save; no `loading_phase` marker for that
load, two hitches of 6.7 s and 17.7 s instead). **Pre-render owner:** `loop_phases` windows 7200–9000 have
`collide` (call site `0x0043a38e` → `0x0045d250`) at 26.1–26.3 ms = 96 % of `input_p50` 27–29 ms, flat for ≈ 66 s,
`max_interval_owner=collide` in every window; `game_phase_segment` (11,968 rows at the 20 ms threshold) gives the
Input site 27.4 ms/frame and `PendingVm` 0.81 ms; baseline window 6900 pre_render 9.25 ms. **Proxy per-draw:**
`motion_output_frame` (67 sampled frames) totals ≈ 6.5 ms at ≈ 530 routed draws (12.0–12.8 µs/draw) and ≈ 10.2 ms
at ≈ 830 (11.3–13.3 µs/draw), `lazy_flush_us=0`; `view_submit` 17.8 ms / 574 draws (31.1 µs) and 25.5 ms / 841
(30.3 µs). Whether the draw fields include the forwarded native draw is being established before the share is
quoted. Self-cost: pass_phases 124 µs, residual 31 µs per frame; frame_phases/motion_output_frame have no self
field. Sanity: threshold 20 ms, `fade_route_mode enabled=1`, light-map gain 4, guide lights 2, 0 DEVICELOST.
Next: disassembly of `0x0045d250`/`0x0045cab0` (docs/reverse-engineering/sector-collide.md) and the proxy
per-draw breakdown and trims (engine-frame-time.md §2.2).

**Run 42 D (run132, 2026-09-18).** View Distance A/B by the FPS overlay at the run117 station, no proxy option:
user reading ≈ 1.5 fps gained by "High" over "Very High"; `frame_end` 30 s busy windows 923 draws / 33.4 ms
(Very High) vs 861 / 31.2 ms (High). The LOD-bias lever (engine-frame-time.md §2.4) is closed: one full LOD
step buys ≈ 7 % of the draws.

**Run 43 A (run133/run134, 2026-09-19).** `--collide-box-cull` A/B at the same held spot (~60 s each), 25 fps
observed in both. Sources: `/tmp/x3-bottleX3-run133/session-20260919-030256-212.log` (option off, 25.7 MB) and
`/tmp/x3-bottleX3-run134/session-20260919-030610-212.log` (option on, 27.2 MB); queried with grep/python only,
never read whole.

1. **Install.** run134: `collide_box_cull requested=1 patched=1 reason=ok p1_site=0x0045d58e p2_site=0x0045cc7c
   write_p1=plain write_p2=plain stub_p1=0x01d70b44 stub_p2=0x01d70bd4 counters=1 enabled=1` (1 line, matches the
   brief's two sites). run133: 0 matches for `collide_box_cull requested` — the flag was off, no install attempt.

2. **`collide_census` (run134, 22 windows of 300 frames, `p1_pairs`/`p1_rejected`/`p2_cands`/`p2_rejected`
   p50/max/sum).** Steady-hold windows (frames 899–6599, 21 windows, excluding the two loading-spike windows at
   299/599): p1_pairs_p50 197–210 (median ≈ 205), p1_rejected_p50 constant at 36, reject share 17.1–18.2%
   (median ≈ 17.4%). **`p2_cands` is 0 in every one of the 22 windows, start to end** — the P2 site
   (0x0045cc7c) never counted an entered pair in this session, so `p2_rejected` is also 0 throughout. With the
   reject share ~17–18% of ~205 pairs/frame, ≈ 34 pairs/frame are turned away at P1 and ≈ 170 pairs/frame reach
   the engine's own comparison at P1's continuation; none of them are observed reaching P2 at all.

3. **loop_phases (`collide_p50_us`, `sum_p50_us`) and `frame_phases` (`dt_p50_us`).** Both sessions show collide
   time and frame dt climbing steadily over the hold and converging to the same plateau: run133 collide_p50 rises
   from ~1.3 ms (window 300) to 22.1 ms (window 6300, max window 25.97 ms p95); run134 rises from ~1.4 ms
   (window 300) to 21.7 ms (window 6600, p95 23.1 ms) — same shape, timing offset by ~1 window (run134's climb
   starts later: windows 300–4800 stay under 3.1 ms then jump at 5100–6600, vs run133's jump at 4200–4800).
   `frame_phases dt_p50_us` mirrors this: both plateau at 37.4–37.9 ms (≈ 26–27 fps), matching the user's 25 fps
   read. No `loop_phases_slow` lines were emitted in either session (window `slow=0` throughout) even at the
   22 ms plateau, so the slow-frame threshold was never crossed. **The 26 ms collide episode is present in both**;
   the option does not visibly change the plateau value or the frame-time plateau — the difference between runs
   is consistent with run-to-run timing noise in when the ramp starts, not with an effect of the cull.

4. **Implied cost per pair vs. the ~70 ns/pair fixture number.** At the run134 plateau (collide_p50 ≈ 21.5–21.7 ms,
   p1_pairs_p50 ≈ 202–210/frame): 21667000 ns / 202 pairs ≈ **107,000 ns/pair**, roughly 1500× the fixture's
   ~70 ns/pair for the pair test alone. Evidence supports: the two probed pair-test sites (P1/P2) account for a
   small, bounded fraction of the 0x0045d250 budget — cutting ~17–18% of P1's own candidates (and P2 apparently
   not being reached at all) left the collide-phase plateau and frame dt plateau unchanged within noise. Evidence
   does not distinguish which of the other candidates (per-object work ahead of the pair loops, the swept query
   0x0045cab0, or narrow-phase work on accepted pairs) is responsible — no counters/timestamps in either session
   isolate those paths. **Resolved statically (2026-09-19):** the narrow phase on accepted pairs, see
   [sector-collide.md](../reverse-engineering/sector-collide.md) §11. The 107,000 ns/pair above divides by the
   broadphase denominator; the accepted-pair count (the `dist <= R` branch at `0x0045d665`) is not counted by
   any site in this session. §11 also records that run129 carries **zero** `profile_*` lines, so the 26 ms was a
   `loop_phases` bracket around the whole call, never a leaf-EIP attribution.

5. **Anomalies (run134).** No error/refused/exception lines beyond routine zero-valued startup counters
   (`error=0`, `failures=0`, `refused=0` throughout steady state — checked with grep for
   error|refus|except|fail|mismatch). The one real anomaly is `p2_cands=0` for all 22 windows (item 2): either
   the P2 site is not on the execution path for this sector/spot, or its counter/stub is not being reached for
   another reason not visible in these logs — needs a targeted check (e.g., a build that logs whether the P2
   stub's trampoline is ever entered, independent of the 300-frame window) to settle which.

## Collide narrow census: fixture and site qualification (2026-09-19, no game)

`--collide-narrow-census` (`X3M_COLLIDE_NARROW_CENSUS=1`); design and output fields in
[sector-collide.md](../reverse-engineering/sector-collide.md) §11.7. Bottle X3, WineArch arm64,
`FEX_X87REDUCEDPRECISION=1`, `WINEMSYNC=1`. Nothing launched.

| Check | Command | Result |
| --- | --- | --- |
| Site verifier | `python3 verification/probe/verify_collide_sites.py` | PASS, 51 checks (29 box cull + 22 census), 123 / 129 other claims |
| x87 audit | `python3 verification/probe/check_no_x87.py` | PASS; roots include `_x3m_collide_narrow_pre`, `_x3m_collide_narrow_post` |
| Stub audit | `python3 verification/probe/build_collide_narrow_census.py` | exact 66-instruction site-5 sequence, `inc; jmp`, `inc; mov; jmp`; 0 x87 instructions in the module object |
| CPU fixture | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_collide_narrow_census.py` | 40 checks, 0 failures, 5.2 s; `verification/results/collide-narrow-census-cpu.json` |
| Box-cull fixture (module unchanged) | same wrapper, `run_collide_box_cull.py` | 38 checks, 0 failures |
| Host tests | `test_collide_narrow_census.py` (10), `test_collide_box_cull.py` (10) | OK |

The first fixture run failed one check: EFLAGS differed in AF only, on every scenario. AF is undefined after the
engine's `test eax,eax` (the last flag writer before the exit) and FEX derives it lazily, so a `popfd` earlier on
the path changes it; the fixture masks that one bit and compares the rest. Cross-thread Present: 14,399 frames,
19,309 accepted + 691 dropped = 20,000. Harness-inclusive cost: 80.0 -> 300.2 ns per accepted pair, 1.97 -> 3.44 ns
per node-pair visit (diagnostic timing, not game FPS).

Not verified: anything in the game. The first flight must show `dropped=0 deferred=0 cross_thread_frames=0
foreign=0` and `accepted_sum = recorded_sum + ring_overflow`.
