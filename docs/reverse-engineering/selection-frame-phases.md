# Selection-adjacent frame gaps: native partition and bounded diagnostics

2026-09-13. Reviewed design implemented in isolated worktree
`/tmp/x3-selection-phases`, base `0c57c52`; independent production source review
is approved. Root's corrected X3 fixture passes CPU/ABI checks and the paired
overhead benchmark; evidence review is approved. No gameplay, production DLL
build or installation was performed for this work. Actual game-phase coverage
and native Windows runtime behavior remain unverified.

## What the current evidence establishes

[Run 26](../archive/run26-comparison.md) records nine target changes.
Six retained, wholly subsequent telemetry windows contain 286–454 ms frame
maxima, with window bounds 0.276–2.171 seconds after selection. These are
window bounds, not the precise slow frame's position or duration after the
selection. `/tmp/x3-run26-selection-summary.json` preserves the bounded
analysis; `/tmp/analyze_run26.py` is the original local analysis script.

The new native lead, solver and central-instrument spans are at most 2.245 ms.
The 16.844 ms distance-producer maximum is from a separate no-target window.
Recorded renderer components are at most 2.834 ms around these selections,
recorded loading operations at most 5.347 ms, and the retained witnesses do
not overlap capture. These observations leave substantial unmeasured time;
they do not exclude other game rendering CPU work, uninstrumented resource
work, callbacks, waiting, or scheduling. Selection stutter also occurred with
chase off in run 24. No causal fix follows from this study.

`telemetry::present` currently records `end - last_present` in an aggregate
counter. It discards the actual endpoints and the identity of the frame that
made the maximum. Its `end` stamp is after native Present and the route/history
commit work, before telemetry polling/reporting. The proposed slow record must
preserve that exact definition, with the prior and current frame IDs, rather
than treating a summary's reporting frame as the slow frame.

## Native main-loop findings

Targeted decompilation and instruction inspection confirm the following order
inside `0x403840`. Addresses are preferred VAs in the reviewed X3AP.exe,
SHA-256 `fdbf3418d8f0a897b58a0bbb449b23f598135ba6aa9ea4eca66df33add34f8ab`.
They are game hook identities, not backend prerequisites.

| Boundary | Interval to next boundary | Direct native evidence |
| --- | --- | --- |
| `403ab0` | Loop setup / optional time refresh | Backedge `4041d8` returns here; game iteration counter increments at `403abf` |
| `403af0` | Clock and deferred callback processing | Calls `4b0e00`; this calls `4ee1e0`, drains callback lists and may invoke registered callbacks |
| `403af5` | Window/input pump | Calls `4d34b0`, including its initial `4d2fc0` helper and message dispatch |
| `403afa` | Channel/resource maintenance | Calls `49a130`; scans sound-channel state, queries COM status, stops/releases/restarts entries and invokes callbacks |
| `403aff` | Pending VM execution | Calls `49f770`, which repeatedly calls interpreter `4a26a0` on the active queue |
| `403b04` | Additional queued service callbacks | Calls `498370`; polls entries through `4d14e0`, updates their state and may invoke registered callbacks; no stronger semantic label is needed for admission |
| `403b09` | Input/control/script/save region | Includes the mode-dependent input and synchronous VM paths and optional save; may leave the loop through `4041eb` |
| `403f2a` | Game-object/script simulation | Calls `416750` |
| `403f2f` | Entire cockpit registry update | Calls `41cde0`; includes work before and outside the existing lead/HUD timers |
| `403f34` | Entire native frame renderer | Calls `471f50`, including scene traversal, temporary allocations, view/monitor loops, overlays and EndScene; the existing component counters do not time this whole routine |
| `403f39` | Post-render inline state restoration | Restores temporary settings before the next call |
| `403f5a` | Frame-rate/detail adjustment | Calls `496f80`; inspected body computes averages and adjusts the setting at global context `+748` |
| `403f5f` | Native presentation / optional screenshot | Normal call `403f99 → 4e3e70`; screenshot branches also call `4f3b10` |
| `403f9e` | Loop tail / diagnostics, through next `403ab0` | Frame statistics and conditional text formatting; normal backedge at `4041d8` |

`4041eb` is the additional exit/abort marker, not a normal per-frame phase.
It invalidates a pending loop interval before shutdown calls. In particular,
`49af10` at `40420d` is shutdown resource cleanup, not routine per-frame work.

`4e3e70 → 4dac30` is the ordinary presentation path. `4dac45` loads device
slot `+44`, and `4dac48` calls it with the device and four null parameters;
this is IDirect3DDevice9::Present. Its continuation at `4dac4a` tests the
returned HRESULT and may subsequently test/reset a lost device. Two additional
markers at `4dac45` and `4dac4a` measure the **complete proxy dispatch**, including
its lock, overlays, reporting and lifetime tail. This complements the existing
narrower Present counter and separates the proxy from the rest of `4e3e70`.

The message pump has two distinct paths. If `608adc != 0` or the flags reached
through `606f3c` contain `4000`, it checks PeekMessage before retrieving pending
messages. Otherwise, `4d3502` calls GetMessage directly in a loop until activity
or exit changes. Therefore a blocking message-pump path exists. The earlier
[loading note](loading-orchestration.md#6-other-observations-in-the-startup-path)
statement that the pump is always drain-until-empty does not describe this
inactive branch. This is a static distinction, not evidence of a run-26 focus
loss. The pump phase should snapshot the readable branch predicates at entry.

## Selection publication and delayed follow-up

The target publisher `425a10` uses custom ABI: EAX is requested tracking mode,
ECX is the cockpit and its stack argument is the target pointer; it returns
with `RET 4`. Mode 2 publishes at `425c08`, mode 3 at `425c4c`.

In the native overlay updater, `42a444–42a44f` tests the elapsed lock timer
against 1000 game milliseconds. Once it expires, `42a45d` calls the publisher
with mode 3. That publisher invokes the named `NotifyTargetLock` event through
`49f4c0` at `425c71`, with ship/target IDs. `49f4c0 → 49f430 → 4a26a0` runs the
VM synchronously before returning. This whole path precedes the existing
lead-block start at `42a6fe`; its duration is not bounded by the new lead,
solver or central-HUD timings. A roughly one-second delay is therefore a real
native follow-up mechanism, but the trace does not prove this event caused
any long frame. Its VM body has not been decoded into speech or monitor work.
Other direct mode-3 calls at `4297b5` and `42dd69` do not require this timer;
the delayed nested pair below covers the overlay route specifically. Those
other calls remain inside the coarse partition, not that nested interval.

Mode-2 acquisition has a separate sound path through `49b050`. Cache lookup
`49a350` can call `4f3e70`, which reads resources through `4e8e10` and processes
RIFF/WAVE PCM through `4f3cd0`. Playback descendants reach `4dd840` and sound
buffer COM operations. This proves an acquisition sound/resource path;
it does not establish that a cache miss, sound driver wait or speech occurred
in any recorded stall.

Three bounded nested intervals belong in the same diagnostic build:

| Interval | Begin / end | Preserved ABI / flow contract |
| --- | --- | --- |
| Delayed mode-3 publication plus synchronous event | `42a45d` / `42a462` | Begin is the five-byte call to `425a10`; EAX=3, ECX=cockpit, `[ESP]`=target. End is a five-byte jump to `42a5d6`; the original callee has consumed its four-byte argument |
| Mode-2 acquisition sound region | `425bac` / `425bed` | Begin is five bytes of argument pushes; ESI=cockpit, EBX=target, EBP=0. End is a six-byte global load; includes both conditional calls to `49b050`, with endpoint also reached by the internal branch at `425bd6` |
| Cold sound resource creation | `49a3e4` / `49a3e9` | Five-byte call to `4f3e70`; end is `ADD ESP,8; TEST EAX,EAX` (five bytes), whose flags must be replayed exactly |

Do not patch a five-byte return endpoint at `425c76`: it would cross the
separate branch target at `425c79`. The higher caller pair above avoids that
hazard and needs neither return-address rewriting nor hooking every publisher
exit. The acquisition interval intentionally includes both sound calls.

## One consolidated diagnostic

Propose one opt-in `X3M_GAME_PHASES=1` diagnostic, independent of chase mode and
rendering enhancements, requiring telemetry and a valid QPC frequency. It adds
15 coarse markers (14 boundaries plus exit), the six nested markers above and
two complete-Present markers: **23 game sites in one installation transaction**.
All 23 claims occur only in `initialize_log`, after checking
`engine_patch::install_window_open()`, before the first Present closes that
window. No lazy claim, late retry or patching from a phase callback is allowed.
Preflight every identity, instruction and incoming-edge contract before any
claim, build the complete group inert, and enable observation only after every
claim/stub succeeds. Failure disables the whole group and restores owned sites
while the installation window remains open. If rollback fails, leave affected
dispatchers/arenas process-lifetime and observation inert, report the failed
site and rollback status, and never advertise partial coverage. Reuse the
existing engine patch/CPU-boundary machinery; unrelated features continue.

The implementation's owning site table and instruction tests must bind these
exact stolen lengths and rel32 fields to the locally verified bytes. All sites
are mid-function markers (`ret_pop=0` in the site specification); actual native
callee stack cleanup is preserved by replay, not synthesized by a stub.

| Site(s) | Stolen bytes | rel32 field offset | Replay |
| --- | --- | --- | --- |
| `403af0`, `403af5`, `403afa`, `403aff`, `403b04`, `403f2a`, `403f2f`, `403f34`, `403f5a` | 5 each | 1 | Original direct CALL, preserving the target listed above |
| `403ab0` | 10 | 0 | Complete AND of the game flags |
| `403b09` | 7 | 0 | Complete TEST of the mode flags |
| `403f39`, `403f5f` | 6 each | 0 | Complete CMP against the global |
| `403f9e`, `4041eb` | 6 each | 0 | Complete MOV |
| `4dac45` | 5 | 0 | MOV EAX from device slot, then indirect CALL EAX |
| `4dac4a` | 5 | 0 | CMP of the returned HRESULT |
| `42a45d` | 5 | 1 | CALL `425a10` |
| `42a462` | 5 | 1 | JMP `42a5d6`, not fallthrough to `42a467` |
| `425bac` | 5 | 0 | Three original argument PUSH instructions |
| `425bed` | 6 | 0 | Complete global MOV |
| `49a3e4` | 5 | 1 | CALL `4f3e70` |
| `49a3e9` | 5 | 0 | ADD ESP,8 and TEST EAX,EAX |

Every incoming target must land outside the stolen span or at its first byte;
the test must reject an interior entry. Relative CALL/JMP replay must be tested
at relocated arena addresses. The byte witness is local
`/tmp/x3-selection-native/site-bytes.json`, with nested endpoint evidence in
`/tmp/x3-selection-followups/endpoint-xrefs.txt` and `transition-xrefs.txt`.

Each boundary records QPC, a monotonically increasing loop generation, thread
ID, phase and validity. The first admitted `403ab0` establishes one
process-lifetime main-loop owner TID. Every coarse, Present and nested endpoint
must match that TID; count and ignore foreign-thread hits without creating an
unattached chain or changing ownership. Use one bounded same-thread nesting
stack. Unexpected same-thread coarse reentry, unexpected order,
QPC failure/regression, exit, Reset or a lost endpoint invalidates that chain
and records a reason. Do not pair a later update with an abandoned start.
No locks or ownership references are held across native work. Do not broaden
or refactor chase timing to share its eight-slot implementation; reuse only
existing small primitives that fit this single-owner contract.

Read user and kernel thread times at the coarse boundaries using documented
GetThreadTimes on GetCurrentThread, keeping both raw counters and validity.
These are CPU execution durations in 100-nanosecond units, while QPC supplies
elapsed-time endpoints. Keep a QPC sandwich around the CPU-time query so its
cost and timestamp uncertainty are visible. A long phase with little thread
CPU is evidence of waiting or descheduling, not proof of which lock or backend;
a large CPU delta identifies execution on this thread. Failed or coarse CPU
accounting must leave the QPC interval valid and CPU classification unknown.
Do not convert CPU cycle counts to seconds or claim GPU time. The documented
contracts support native Windows and CrossOver without private backend APIs.
See [GetThreadTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getthreadtimes)
and [QueryPerformanceCounter](https://learn.microsoft.com/en-us/windows/win32/api/profileapi/nf-profileapi-queryperformancecounter).

Retain a fixed boundary tape in memory across adjacent Present endpoints.
At game marker `4dac45`, capture the opaque raw device value from `[native ESP]`
and push a same-thread dispatch token. The original caller has already pushed
the device; at `4dac4a`, stdcall has consumed all arguments and EAX is HRESULT,
so that endpoint must match the saved token, never reread a presumed device
argument or look up `devices` from the game callback.

While the existing Present hook owns `Device` under its established lock, it
publishes immutable value metadata to that token: raw identity, stable device
ID, device/reset generation, current frame before its increment, capture flags,
HRESULT and the existing telemetry endpoint QPC. A missing/mismatched token is
counted and leaves coverage unknown. This bridge creates no new COM ownership
and performs no unlocked registry access. Nested dispatches use separate
bounded tokens; they cannot overwrite the containing dispatch's metadata.

The telemetry endpoint **stages** the preceding frame interval; it does not
finalize or flush that detail record while the full proxy call is still open.
`4dac4a` later appends the complete-dispatch exit and finalizes the staged
record using the saved frame metadata, even though `ctx.frame` has already
advanced inside the hook. The telemetry-to-game-return tail belongs to the
following frame interval when clipped at the telemetry endpoint; retaining
the complete dispatch as a nested annotation must not add that tail to the
preceding frame's coarse sum. Flush the finalized record at a later existing
summary boundary; never relabel it with that boundary's current frame.

A finalized record carries device ID, previous/current frame
IDs, previous/current endpoint QPC, capture flags, HRESULT, loop generations,
thread ID, valid phase segments and unmatched/residual ticks. Clip segments at
those exact endpoints; nested intervals are annotations, never added again to
the coarse partition. Preserve both parts of the presentation phase when a
Present endpoint splits it. Startup, skipped Present, nested rendering and
missing markers remain explicit coverage gaps. A target sample is identity
metadata only; do not dereference it after recording or retain game ownership.
Record mode-2/mode-3 request witnesses from the nested entry registers,
independently of whether a renderer or chase hook is active. The acquisition
sound endpoint precedes the actual target store, so label its pointer
`requested_target`, not a confirmed publication. These three pairs do not
observe every target-clear or other tracking-mode setter; correlate existing
target-change records when available and keep that coverage limit explicit.

Reset entry and final device Release invalidate open loop/dispatch tokens,
pending endpoint records, tape state and raw device association before the
old `Device` can retire. A lifecycle callback on another thread publishes an
atomic invalidation epoch; the owner checks it before matching or finalizing,
without allowing that thread to mutate the owner's tape. That epoch revokes
the saved raw identity, which the owner clears on its next boundary. Returning `4dac4a` after such an
invalidation is an unmatched/abandoned endpoint, never permission to touch the
retired device. Recreated devices start a new stable device generation. Every
Reset entry invalidates the diagnostic epoch, including a reentrant/busy
refusal. An attempt admitted to native Reset advances the existing
`ctx.reset_generation` before that call, whether it succeeds or fails; the
busy guard preserves its existing behavior and does not advance that production
generation. Recovery and valid rendering state remain contingent on success.
No interval may cross either diagnostic invalidation or production generation.
Sites and main-loop thread ownership remain process-lifetime;
device teardown does not restore live game patches or free their arenas.

Every complete interval contributes count/total/max and a timestamped maximum
to the existing periodic report. Only frames of at least 50 ms or nested calls
of at least 10 ms retain detailed records. Keep four first and four recent
slow records per report window, with omitted/overflow counters; flush at the
existing summary boundary, never at each marker and never with per-frame disk
writes. Each detailed record includes all coarse segments, exact QPC and frame
IDs, target/mode at boundaries, nested events, CPU-time deltas and query cost.
This can identify both a delayed event and unrelated slow frames in one run.
Both endpoint capture flags are retained so analysis can exclude contaminated
adjacent frames.

The handler performs fixed-array updates, checked narrow metadata reads and
clock queries only: no allocation, formatted logging, native callback,
resource lookup, COM ownership, stack walk or thread suspension. All original
registers, flags, stack state, x87/SSE state and LastError must survive both
callbacks and replay. The game call is executed once with its original ABI.

## Qualification before the shared next user run

The implementation is `src/proxy/game_phases.cpp`, with the pure bounded
recorder in `game_phases_core.h` and the exact 23 sites in
`game_phase_sites.h`. The launcher exposes this as `--game-phases --telemetry`.
It installs only when both `X3M_GAME_PHASES=1` and
telemetry are enabled. Include the reviewed change in the same retained
candidate as the bloom correction. Focused checks cover the 23 exact
instruction spans and destinations, rollback,
CPU/LastError preservation, custom-ABI and conditional acquisition endpoints,
foreign-thread rejection, reentry/exit/Reset/final-Release invalidation, exact
Present/frame mapping before and after the hook's frame increment, delayed
finalization, device recreation, clipping and
nested non-additivity, missing/overflow records, CPU-time failures and output
rate bounds. A fake timeline should place a 400 ms event after a target change
and prove its exact QPC range survives aggregation rather than moving to the
summary's reporting frame.

The paired fixture measures disabled and enabled marker paths, including
GetThreadTimes, separately from game FPS. The recorder makes 14 coarse
CPU-time queries per normal loop plus one at the owned Present
bridge. The bridge retains the exact telemetry QPC endpoint and the actual
later CPU-query bracket, preserving CPU evidence on both clipped parts of the
presentation phase. Root accepts the measured additional 0.134 ms per loop
and retains all 15 CPU-time queries for the next shared trace; no adaptive
query mode is needed. No all-thread high-frequency suspension is used.

The host timeline fixture exercises a synthetic 400 ms delayed call, exact
frame/QPC clipping and deferred finalization, CPU-query uncertainty/failure,
Reset/final-release invalidation, recreation, bad order, nested mismatches,
bounded retention and tape overflow. The installer fixture executes the actual
extracted transaction with a failure at every claim/emitter/store/push stage,
including reverse rollback failure remaining inert. Capture's existing
lifetime fixture uses a no-op diagnostic shim and retains its renderer
lifecycle expectations. Site checks read the owned EXE locally and validate
instruction spans, relocation destinations and ABI endpoints.

`verification/probe/build_game_phase_cpu.py` builds only the dedicated fixture
EXE and audits the production callback's CPU boundary. The fixture exercises
all 23 actual emitters and six synthetic native ABI/conditional replay cases;
its paired benchmark reports diagnostic overhead, not game FPS. The successful
root-owned X3 run below supplies runtime evidence for those synthetic ABI
cases and the complete recorder loop. It does not establish game-hook coverage
in a loaded scene, identify the original stall, or verify native Windows.

The focused host modules `test_game_phases`, `test_game_phase_install`,
`test_game_phase_sites` and `test_capture_bloom_lifetime` pass (24 tests across
the affected runs). The new C++ witnesses execute 124 timeline assertions,
1,078 installer assertions, 39 owner/epoch assertions and 19 delayed-read
assertions. The fixture-only address seam adds 18 extracted bounds/lifetime
assertions. The host recorder
occupies 241,008 fixed bytes; normal boundaries allocate nothing, perform no
formatted logging and add no lock. Checked metadata reads use the existing
engine-memory region-cache spinlock; their validation and contention are
included in handler cost. Phase wall time includes instrumentation and any
periodic report work that occurs between its boundaries; the handler/query
metrics and paired fixture quantify that observer cost, without subtracting
estimates from the recorded native timeline. Detailed storage is capped at 96
segments per frame, eight open tokens and eight retained frames/calls per
window. Even the maximum populated report is bounded to 806 lines; its disk
cost remains part of the diagnostic run and must not be interpreted as game
performance.

Affected capture/telemetry objects cross-compile. The dedicated fixture EXE
SHA-256 is `ed5f6f6800802b7755f85fe9b273b6e2085a2fcda8b7286825ca126bbde49ece`.
The production callback audit finds no exception-runtime bookends, with
GetLastError first and SetLastError last around the qualified x87/MXCSR
preservation path. No production DLL was built. The benchmark uses 16 marker
callbacks plus one owned bridge and 15 CPU queries per complete loop, with
the same native-ABI harness in baseline/disabled/enabled modes. The unhooked
baseline corresponds to normal default-off behavior; the disabled-marker
case measures installed but inert stubs, such as a failed rollback left inert.
Its final enabled batch reports five loops and four frame intervals, with zero
ordering, unmatched, overflow or metadata-read errors. Synthetic pump globals
use a fresh dynamically allocated region. The fixture-only address setter
requires the recorder disabled, bounds all three DWORDs within the supplied
size, rejects unaligned/overflowing x86 addresses and clears before release.
Production retains the fixed `608adc`/`606f3c` addresses. The fixture still
exercises all three successful checked metadata reads, and every mode advances
the existing region cache once per loop, matching the motion route's frame
invalidation. Cost metrics include each batch's unmeasured warm-up loop.

Root's first X3 attempt with the `f3d9ac97…98495` fixture returned failure:
811 checks, one failure at its required `0x600000` allocation, and no benchmark.
The 23-emitter/six-replay prefix reported no CPU/ABI failures. That failed run
provides no performance evidence. The raw
114-byte witness is retained locally as
`/tmp/x3-selection-followups/cpu-runtime-r1.txt` (SHA-256
`750954a65babfd05f1e0e68ed89111295c356639540be421ccd39c61ad89aa5b`).
The correction changes fixture address selection only; it never overwrites or
reprotects an existing process region.

The corrected root-owned, locked **X3 R2** run exits zero: 23 actual emitters,
six synthetic ABI replay cases and 815 checks with no failures. The
runner uses CrossOver Preview's `X3` bottle, whose `cxbottle.conf` records
`WineArch = arm64`, `FEX_X87REDUCEDPRECISION = 1` and `WINEMSYNC = 1`;
the retained R2 stderr confirms msync activation. Root confirms its command
set only `X3M_FIXTURE_BOTTLE=X3`, with no FEX/MSYNC override. No other
bottle was run.
The
[retained compact raw report](../../verification/results/bottle-X3/game-phase-cpu-r2.txt)
is 3,920 bytes, SHA-256
`9d157ec3e601c8dc5ee4ea0b52a3d7d959735366bf16f57b64d57ba4c295787e`.
The final enabled batch has five loops/four frame intervals and zero
invalidation, unmatched, overflow, order, clock, foreign-thread, suppression,
metadata-read and CPU-query errors. Aggregate counts are 30,720 handlers,
28,800 CPU queries and 1,920 owned bridges, including the unmeasured warm-ups.

Across three balanced-order trials, each containing 128 batches of four timed
loops per mode, mean paired loop times are 17.690299 microseconds unhooked,
25.714453 microseconds with inert markers and 151.190430 microseconds enabled.
The measured additional enabled cost is 133.500130 microseconds per complete
loop (16 marker callbacks, one owned bridge and 15 CPU queries); inert markers
add 8.024154 microseconds. These are synthetic diagnostic overhead measurements,
not game FPS, native game-call durations or a measurement of periodic report
I/O. The brief final phases have zero user/kernel increments at the thread
accounting granularity; CPU-versus-wait interpretation still needs the next
actual slow-frame trace. Root accepts the cost and keeps complete coarse CPU
sampling for that trace. Independent evidence review is approved.

Raw native exports remain untracked under `/tmp/x3-selection-native/` (main,
phase, terminal exports and site-byte witness) and
`/tmp/x3-selection-followups/` (publisher and audio exports). This note records
derived findings and the implementation contract; no causal stall attribution
or native-Windows runtime validation is claimed.
