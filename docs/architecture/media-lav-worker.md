# Owned LAV transport

**Retired from production (2026-09-21).** The user accepts missing ID2 animated
textures after the Run55 LAVVideo crash. The owned playback implementation and
its runtime/build prerequisites have been removed. The description below is
historical; `--media-package` is no longer a launcher/install option. New managed
installs retire the active selection while retaining the previous installation
and provider files for rollback. See the [media ledger](../verification/media-cues.md#id2-video-omission-and-owned-playback-retirement-2026-09-21).

The canonical [LavWorker](../../src/media/lav_worker.h) now supplies the actual
transport exercised by the concurrent clock/render and two-fresh-graph EOF
fixtures. The [qualification record](../../verification/results/media-lav-worker-transport-2026-09-20.json)
binds the production inputs, two frozen executables, original RGB/time references,
package records and scoped results. Independent source/runtime review is clear on
CrossOver X3. Engine admission, destination/Reset integration and native Windows
execution remain separate work.

## Ownership and publication

Each service creates its own STA, private HWND, DirectDraw object with
`DDSCL_NORMAL | DDSCL_MULTITHREADED`, graph, decoder and source sample. It passes
that DirectDraw service to AMStream and observes the actual source surface format.
The worker owns all COM calls and source Lock/Unlock/normalization. No engine
callback, destination D3D object or borrowed source/config pointer enters it.

The main owner uses the state core's existing SessionHandle, operation and epoch
values. One bounded command mailbox carries self-contained construct/play/seek/run
work. A separate coherent desired publication remains available when that mailbox
is full. Its three scalar cells transfer ownership with lock-free 32-bit atomic
exchanges; no torn x86 64-bit tuple or spinning seqlock is used. Commands are
removed from the state owner's offer queue only after actual transport acceptance,
with adjacent nonreentrant submit/ack. A failed submit leaves the offer intact.

Exactly three preallocated 1 MiB pixel slots move through FREE, WRITING, READY and
READING. Only the producer reserves FREE and publishes immutable READY bytes. Main
holds a move-only FrameLease through upload/Unlock and explicitly releases it.
The lease retains its storage independently of the service handle. Canonical
[Clock](../../src/media/owned_clock/clock.h) references those three real leases
through metadata; it supplies presentation time and completion policy, not LAV.
There is no second pixel queue, transport presentation clock or raw-EOF callback.

Prepared facts use bounded ordinary publication. Failure, source EOF and physical
retirement accumulate in a separate terminal publication that cannot be replaced
until its final revision has been observed. New frame admission rejects failed,
unsafe-retained and shutdown services. Existing held leases remain readable until
the consumer releases them. Failure without any graph is final and can be
acknowledged; it does not permanently consume the prepared service.

`poll_assignment_quiescent(canceled)` is stronger than service readiness or an empty
frame poll. The worker must have consumed that canceled publication, retired its
graph, discarded both local-pending and mailbox commands, emptied ordinary events,
observed final terminal acknowledgement, and seen all three physical slots FREE.
A coherent fact carries the complete canceled identity and a nonwrapping desired
publication serial. Main compares its latest serial, so even a republished identical
cancel cannot reuse an old idle fact. Serial exhaustion closes admission. Main
continues polling events and disposing of revoked leases while waiting. The actual
fixture verifies initial no-graph cancellation on both services and stale-fact
refusal after a newer live publication; graph-retirement quiescence has source/host
coverage, but was not separately exercised by that initial smoke.

## Graph and service lifetime

The graph uses an explicit NULL reference clock and per-graph
`SupportSeeking(TRUE)`. `IMediaEventEx` notification flags are set to zero and read
back before provider construction/Run, then checked before drains. Every retrieved
event is inspected and freed exactly once. Batches of 32 provide fairness, not a
queue-capacity assumption; immutable operation/sample deadlines bound continued
work. Cancellation does not reset those deadlines or suppress provider errors.

Retained integer seeks preserve the selected-allocator Decommit, Stop/stopped,
ABORT and separate terminal-observation guards. Release or WRITING abandonment
requires the corresponding actual public guard. A failed Load has one narrow
preconnection cleanup branch: actual failure before any connection, allocator,
Run, sample or Update. Other unproved retirement retains the interfaces and STA.
C++ exceptions at the thread boundary publish failure and retain live state rather
than unwinding unguarded COM or terminating the host process.

Cancellation closes main admission immediately and is checked around worker calls.
It cannot interrupt a blocked native call. During construction the worker may
continue to the selected allocator boundary so cleanup remains provable; no new
partial-construction release shortcut is assumed. Actual sample EOS must pair with
one fresh `EC_COMPLETE` and an empty event queue. The EOS WRITING reservation is
abandoned only after retirement, then replay creates a fresh graph on the retained
DD service. Six pictures alone never establish EOF.

Startup pins the containing module using documented `GetModuleHandleExW` with
FROM_ADDRESS and PIN; failure refuses service startup. The pin lasts for the
process. Unloadable-plugin teardown is not supported. The live thread retains its
Impl, pixel storage, observer and immutable PackageConfig owner even if the public
handle disappears or retirement is unsafe. No production join, watchdog kill or
DLL unload is performed from the main pump or DllMain.

## Package integration and measured scope

The fixture exercises the real [module-relative package reader](media-package-config.md)
on a preparation thread, from an unrelated CWD and a relocated Unicode/space
module directory. Both workers receive the same typed immutable package owner.
The fixture drops its temporary reference after transfer and verifies ownership
through source/graph/service lifetime, then release after actual worker and
watchdog exits. Once per service, public module enumeration checks exactly one
matching runtime basename and normalized full path for each of the nine modules.
Runtime readiness uses public identities and interfaces, not binary hash gates.

Clock v5 passed 4,586 exact QPC transactions, 35 exact rendered captures (A8/B27)
and 43 readbacks, including a real B selection while A held three paused leases,
retained seeks/rates/loops, failed-Load containment and 18 later B selections.
Both services ended with all three slots FREE. EOF v4 passed 12 exact suffix
captures/readbacks, two actual sample EOS/fresh graph-complete pairs, guarded
retirement and retained DD identity. The production transport inputs are identical;
v5 changes only fixture pause progression to wait for the required actual peer
selection. EOF does not require another execution for that fixture-only change.

Main transport polling performs no graph COM, frame allocation, mutex wait or join.
Production observation is optional and null by default. Per-frame source copies
and event drains remain worker work; module/path validation and allocations occur
during preparation or graph construction. Exact clock-header bytes are unchanged.
Measured graphics calls include upload, submission and diagnostic readback costs;
they do not measure GPU execution or game FPS. Clock v5's maximum main completion
gap was **226.4 ms**, with **110.57 ms Clear** and **113.3303 ms EndScene** in that
cold interval. It overlaps DD creation/cooperative calls, which is correlation,
not isolated causality. DD creation took **111.1465/219.9964 ms**; after the first A
selection heartbeat, the largest later gap was **15.2747 ms**. Diagnostic readback
Lock reached **13.4969 ms**. These observations establish no universal no-stall
claim and do not erase earlier 47 ms combined-copy or 195 ms readback observations.
EOF's maximum gap was **71.9719 ms**. Native Windows and actual game loading,
window/cursor behavior, rendering recovery and callback completion remain untested
by these standalone fixtures.

## Same-epoch clock bound update

After the frozen worker qualification above, the canonical Clock gains
`set_end(end_ms, now)` for Services' same-epoch bound changes. It advances the
existing clock and changes only the bound: queued frames, generation, rate, pause
and settled termination are preserved. Inactive intent and regressing QPC refuse
without mutation. Nonpositive bounds disable future positive-end checks; the
existing strict comparison remains in `update`. This is the independently reviewed
seven-line addition, not a change to worker transport or its frozen runtime oracle.
The prior clock/EOF runtime records retain their original clock hash.
