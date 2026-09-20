# Asynchronous ID2 video playback direction

2026-09-20. Architecture and isolated implementation contract; production
admission and the installed feature remain separate. The current candidate is described only in [status](../status.md).
The [consumer/lifetime reconstruction](../reverse-engineering/media-record-lifetime.md)
and [media verification ledger](../verification/media-cues.md) distinguish engine
facts from fixture observations. Native Windows runtime remains unverified.

## Purpose and ownership

Failed construction of the ID2 animated-video source causes periodic game-thread
stalls. Repair must provide the video and keep graph construction, decoding, seek
and retirement off the game thread. Extending the failure retry interval is not
the chosen playback repair.

The selected experiment retains the explicit LAV/amstream sample topology on one
worker. The worker owns its COM apartment, private window, DirectDraw object,
graph, sample and source surface. The engine receives immutable CPU-frame leases
through bounded storage; no engine record, callback context, window or D3D
destination pointer crosses to the worker. Main-thread code never waits for a
graph operation or joins a live worker. Destination resolution and D3D copying
remain on the engine thread, with their own lifetime and recovery obligations.

Use documented DirectDraw/DirectShow APIs. The worker supplies its own DirectDraw
object configured with NORMAL and MULTITHREADED, rather than assuming amstream's
default is suitable. While the graph is actually stopped, set the graph-manager
reference clock to NULL and observe that configuration before/after retained
seek/Run. Decode delivery is then separated from application presentation time.
This combined configuration is under fixture qualification; it inherits no
threading, native-platform or no-stall guarantee from earlier graph tests. Public
source-surface caps and pixel format must be observed. Shared driver locking
can still affect main-thread rendering and must be measured.

## Engine scope and admission

Initial ownership targets ID2 with the selected video source and effective flags8
route. Flags8 alone is insufficient: other IDs and speech/audio share helpers.
Subsequent dispatch uses explicit owned-object identity and lifetime generation.
All unowned routes retain their original behavior; an owned shell must never
fall through into an original consumer expecting real COM fields.

Retain the manager record/list model and a compatible 0xb4 media shell for the
proved immediate flags read and end-position writes. Besides constructor,
seek, Run, pump, position, stop, copy and destructor, cover the direct script
rate setter and inline stop-all. The flags8 volume helper already rejects before
COM access. These are consumer boundaries, not qualified patch spans.

Construction returns an owned shell only after local allocation, initialization
and bounded command/session admission succeed. Eligible construction/existence
notifications stay immediate: success means **record admitted and commands
accepted**, not decoder ready. This intentionally changes synchronous backend
readiness semantics and avoids retaining a new deferred construction callback
whose lifetime has not been established. Rejection retains the existing failure
notification. A later worker error does not issue a second construction result.

An admitted play installs playing intent and distinct operation ownership while
preparing, so stop-all can cancel it. Preserve existing callback meanings:
pre-admission rejection uses status0; admitted runtime failure uses the actual
manager error path's status1, also used by other forms of operation termination.
Status1 is not proof a picture was displayed. Superseded and incoming requests
remain distinct even if their callback keys are equal. Engine callbacks stay on
the engine thread. Exact continuation, reentry and retirement safety remain
implementation requirements.

## Presentation clock and rates

Prepare and seek consume no clip time. On the engine's first eligible current-
generation frame, anchor an owned monotonic clock at the requested source start.
Respect any actual gap to that frame's source PTS. Use the verified public
sample/request time mapping; do not reproduce Wine's incidental clock lead or
invent offsets. Pause freezes time; a stopped seek stays stopped; an automatic
loop keeps its operation but arms a fresh epoch without an external Run.

After starting, normal underrun holds the last picture while time advances.
Discard late frames by their real intervals and consume only a bounded batch per
update. Before copying, apply the original millisecond truncation and strict
positive-end comparison: equality is eligible, greater-than ends the clip. Check
that bound even when no new frame is ready, so underrun cannot keep a finite clip
alive indefinitely. Physical EOF is a separate actual provider terminal event;
queued pictures finish their scheduled intervals before normal completion.

The rate domain is resolved from the pinned LAV 0.81 source: SetRate accepts
positive rates, rejects zero/negative, and exposes backward seeking separately
from reverse playback. Adopt all positive signed engine inputs 1..INT_MAX.
Preserve the EXE's exact float constant, bits0x3727c5ac, value2748779/2^38:
`rate = double(input) * double(original_float_constant)`. In particular, nominal
100000 is slightly below1.0 and is distinct from the constructor's exact1.0.
Reject nonpositive inputs without changing clock, intent or ownership.

Success means a real synchronous local clock transaction: accumulate elapsed
position under the old rate, install the new slope, and preserve position,
source-coordinate clip bounds and pause/loop intent. Decoder timestamps remain
at1x with no graph clock. No optimistic worker HRESULT or cached provider result
stands in for applying the rate. Accepting extreme forward rates does not promise
to decode every picture; bounded dropping and underrun still apply. Clock
arithmetic, rate changes, endpoint behavior and actual scheduling need testing.

Primary references: [pinned LAV splitter implementation](https://raw.githubusercontent.com/Nevcairiel/LAVFilters/c97e4049aff5d2ed86a2aa517b6a75357daf83b0/demuxer/LAVSplitter/LAVSplitter.cpp),
[graph clock selection](https://learn.microsoft.com/en-us/windows/win32/directshow/setting-the-graph-clock),
[DirectShow playback rate](https://learn.microsoft.com/en-us/windows/win32/directshow/setting-the-playback-rate).
Local derivation and source anchors are in `/tmp/x3-media-async-engine-policy.md`.

## Initial worker verification boundary

The worker fixture uses three CPU slots and 18 admitted frames across zero,
ten-second and2.2-second positions. It must observe a naturally full queue,
cancel its generation, reject stale admissions, preserve exact reference RGB
and source start/end timestamps, and retire safely. Actual main D3D heartbeats
run during worker operations, with gaps and API durations reported rather than
a claim that private ownership guarantees freedom from stalls. A separate
16-picture audited suffix checks natural EOF and delayed final-picture delivery.

Neither standalone worker fixture installs engine hooks. The subsequent consumer,
destination, Services and root checkpoints implement their integration separately.
User-launched playback, real game callback/Reset behavior, startup timing and native
Windows runtime remain acceptance gaps; the installed candidate is described only
in [status](../status.md).


## Concrete engine consumer checkpoint (2026-09-20)

The default-disabled `media_engine_adapter` now implements 24 whole-instruction
consumer detours and 11 caller-return envelopes. The production encoder is also
the fixture encoder. This qualifies a reusable consumer group, not enabled game
playback. The existing `media_cue` owner at 0x498140 supplies the eligible ID2 /
effective-flags8 predicate; it is never patched twice. The consumer group alone
cannot satisfy admission readiness.

Consumer references the canonical playback Adapter, runtime and clock. Its bounded
eight-entry map relates the record and compatible 0xb4 shell to a session; it does
not duplicate commands, frames, destination storage or clock state. Record
retirement invalidates traversal before callbacks or external work, including for
unowned list members. Shell identity remains until matching destruction. Allocation
uses the matching game allocator and live/cumulative accounting; unpublished
failure undoes live accounting, and owned destruction bypasses legacy COM cleanup.

Explicit play and shared speech reserve and commit before metadata writes. Rejected
incoming requests follow their original status0 paths and retain the accepted
operation. Success resumes the original old-callback/new-callback commit path;
speech preserves its existing loop bit and uses its own saved-register epilogue.
Stop-all covers the inline COM bypass, and rate success means an actual local clock
transaction with the original HRESULT convention. No helper fabricates decoder
readiness or forwards an owned shell into legacy graph code.

At loop completion, the operation remains active until atomic
`loop_seek(session, start, end, &backpressure)` succeeds. Capacity pressure leaves
its epoch, end and callback obligation unchanged, skips to the validated next
record at 0x4984b5 and retries once on a later pass. Permanent live refusal uses
0x498473; invalid traversal uses the pointer-free 0x4984be exit. Each drops the
return address and start argument as required. This avoids starving the next
record while preserving the pending loop. Services must forbid uploads past the
finite end and must not consume the loop's terminal event before rearm.

`Services` is the external transport/destination boundary. Except for `pump`, all
methods are bounded, CPU-only, allocation-free, non-reentrant and `noexcept`.
`reserve` binds an already prepared worker slot; `publish` uses canonical
peek/offer/submit/ack and retains commands under pressure. `pump` holds an independent
local immutable FrameLease across destination calls, rechecks session, operation,
epoch and traversal after final release/gate close, then commits actual scheduling.
Only observed scheduling/provider outcomes may report completion or failure.
Initial record observation carries slot, initial flags and anticipated publication
flags as values; constructor bit4 is not proof of a published binding.

Destination observers use the public value-only API
`Consumer::binding_owner(uint32_t record, SessionHandle& session, EngineKey& key)`.
It admits only a live owned record in the bound owner domain, reads no engine
memory and leaves both outputs unchanged on failure. Destination watches and
surface ownership stay in their canonical modules; no worker receives record keys.

### Owner thread, stack and exceptional escape

Before publication, native binding fixes one owner TID and stack allocation using
public `GetCurrentThreadStackLimits`, with a bounded `VirtualQuery` allocation walk
fallback. Each mutable dispatch validates that domain, the complete 168-byte CPU
frame and 64-byte argument extent. A separate stack allocation fails even on the
same TID. The owner thread and allocation must outlive every owned object; custom
stack switching within the same allocation is outside this contract.

Two append-only lock-free atomic tables retain up to 256 record keys and 256 shell
keys, including tombstones. Capacity is checked before allocation/accounting and
both keys are published before owned exposure. Keys are never evicted, so a miss
cannot forget an admitted object. Exhaustion permanently closes new admission;
existing owner cleanup remains available. Historical address reuse can
conservatively refuse a foreign unowned call. Never-seen foreign keys use the exact
original replay without a token-dependent return observer. A foreign owned or
root/list operation closes admission and takes its qualified failure/abort route,
without reading mutable Adapter state or the list. Once claimed, closed eligible
construction is locally refused rather than falling back to synchronous graphs.

Return guards retain 32 bounded value snapshots. On the supported downward-growing
owner stack, a new call prunes abandoned same/higher normalized stack addresses;
a return prunes deeper abandoned addresses and then matches its own ID and stack.
There are no locks or leases to unwind in these snapshots. Production neither
catches nor swallows exceptions. The fixture executes actual `RaiseException`,
then its handler restores ESP/EIP manually and returns `ExceptionContinueExecution`.
This verifies lazy cleanup after those escapes and nested catches, **not** general
SEH/RtlUnwind or C++ unwinding across substituted pump/loop/retire return addresses.
Unchanged unowned helpers retain their internal COM reentry behavior; caller guards
do not claim to repair inherited helper-internal stale accesses or accesses before
an interior hook seam.

### Installation, cost and remaining integration

The group preflights complete spans, checks emitted writes/readback/RX protection
and instruction-cache flushes, then uses aligned-qword compare/exchange for each
five-byte branch. Failed installation restores claimed words in reverse order and
retains redirect, protection and cache debt when restoration fails. Emitted code
is process-lifetime storage. Live-shell uninstall is prohibited. Helpers preserve
GPRs, EFLAGS, XMM0..7, x87/MXCSR and LastError with SSE2/four-byte-stack compilation.

The source cost pass finds bounded eight-identity/32-return-guard work on owner
paths, no graph calls, waits, logging or allocations in ordinary consumer updates.
The foreign classifier scans at most 256 atomic keys per queried kind. Staging
reserves 24 process-lifetime 4 KiB code blocks; construction alone uses the matched
shell allocation. These are source bounds, not measured game latency or FPS.

The [ledger](../verification/media-cues.md#concrete-engine-consumer-qualification-2026-09-20)
records 4,490 host assertions and 5,055 executed x86 fixture checks. The x86 fixture
uses authored engine memory/callbacks in its own checked PE image, not a loaded
game. Native Windows runtime remains unverified. The consumer checkpoint alone keeps admission disabled. The root composition below
adds actual worker/clock/destination services, binding and Reset observers, startup
lifetime and cue ownership. Real game callbacks, source eligibility beyond ID2,
startup timing and user-launched playback/Reset acceptance remain open.
## Destination provenance and synchronous copying

The isolated destination component in `src/proxy/media_destination*` connects
nineteen qualified engine observations to the canonical `FrameLease`, canonical
surface lease and existing presentation gate. Complete startup installation,
worker/clock services and consumer admission remain the root integration owner's
responsibility. Standalone qualification does not enable production playback.

The destination stores values for every media-selectable published slot and at
most eight active record/session watches. A shared wrapper has one indexed
lifetime and linked slot aliases. Final retirement removes its address index and
keeps its tombstone until old aliases disappear; device publication and table
growth cannot turn that tombstone back into a live index entry. Every successful
CreateDevice publication invalidates live provenance even when its address key
is reused. Reset observations are separate and do not manufacture device-creation
events. The payload is 24-byte slots, 64-byte wrappers and two 8-byte index entries
per slot: at most 3,407,872 bytes, or 6.5 MiB during cold growth. Allocation occurs
only at table creation/growth; no per-copy or per-draw allocation is introduced.

One short domain protects the qualified producer's original store and wrapper
surface-key read. Lifetime ingress takes the same domain before original free or
mutation. A 64-entry nonwrapping token stack keeps nested publication unavailable
until the correct outer normal return. Ownership-registry snapshots occur after
unlocking and publish only against the exact unchanged token. Late binding reads
only this provenance, including completion of a recorded pending publication or
recovery transaction. It never dereferences an old wrapper to seed a missing map.
If an outer cleanup observes an unknown/new provenance lifetime at its key, the
physical lifetime is ambiguous and copying is permanently disabled. An escaped
inner observer similarly leaves acquisition vetoed while a surviving outer call
retains its original return address; no storage is resurrected by stack pruning.

Copying takes the caller's independently held canonical frame lease. Frame
sequence zero is the canonical worker's valid first publication; owning storage,
session/operation/epoch and graph establish validity. Successful Services
acknowledgment records the binding epoch and releases `selected_uploaded`, even
when its presented sequence remains zero. The accepted copy scope spans canonical
surface acquisition, descriptor checks, one LockRect,
bounded row writes, exactly one Unlock after a successful Lock, and final Release.
It holds no domain lock over COM. Binding, operation and pointer-free traversal
values are checked after external calls and again after final Release; only a
current successful write is acknowledged. A failed Unlock preserves its HRESULT
and invalidates only its captured publication, leaving a reentrant replacement
untouched. Internal exceptions at completed-stage checkpoints clean up before
copy depth is cleared. The descriptor is currently queried once per selected
copy; there is no persistent destination COM reference or second frame queue.

The whole engine Reset token begins before precleanup. The ownership boundary
reports native begin/end outside its registry mutex under an audited CPU/LastError
shell and carries the actual HRESULT. The emitted presentation gate defers Reset
while copy depth is live. Recovery requires successful native Reset followed by
normal whole-helper completion and exact canonical serial revalidation; failed
Reset keeps copying unavailable. The focused results and native-platform limit
are recorded in the [media ledger](../verification/media-cues.md#destination-provenance-copy-and-reset-checkpoint-2026-09-20).


## Qualified startup root and common record ingress (2026-09-20)

`media_root` registers callbacks during backend initialization but constructs its
process-lifetime context only in the matched ordinary factory return, inside the
existing startup CPU envelope. That qualified thread/stack owns Consumer,
Admission, Destination and all callback contexts. A documented process module pin
precedes publication; neither DLL detach nor failure frees reachable contexts.
The existing startup proof places this seam before the initial media-table loader.

The root preflights/stages all 44 spans (presentation gate, 19 destination and 24
consumer sites) before installing gate → destination → consumer. Routes are the
actual group members referenced by the observers. The existing allocator cue owner
is reused only after verifying its installed bytes and chain, or verifying the
original pristine allocator span; a foreign/conflicting claim refuses composition.
The eligibility predicate is published last. Failure first disables admission,
then attempts consumer → destination → gate rollback, retaining all code and any
unpaid restoration/emission debt. Reset and destination dispatch registrations are
exclusive process-lifetime claims; identical-context retries are idempotent,
whereas null/replacement claims cannot silently displace their owner.

`RecordIngress` shares Destination's short mutex for immediate heap copies and
value-watch cancellation before record/shell retirement or list clearing. Adapter
invalidation and Services cancellation happen after unlock. Covered foreign owned
or list/return-guard refusal permanently vetoes this domain without reading the
owner's mutable identity map. Future heap access refuses; bounded owner-stack
argument/return cleanup can still access the immutable qualified stack range.
Capacity exhaustion is separate: it closes new constructors while preserving
current sessions and their copy admission. No domain lock spans engine calls,
registry lookup, worker methods, frame copying or COM.

An ordinary successful CreateDevice publishes its application value only after
capture's lock is released and canonical ownership membership is established.
Repeated Present never republishes it; same-address device replacement invalidates
old provenance. Every capture mutex scope, including compositor and factory QI,
contributes to a cheap exclusion witness. Nested Present skips maintenance. A
successful nested device creation atomically disables acquisition, with owner-side
Services cancellation deferred until the next unlocked boundary.

Regular owner Present performs bounded Services maintenance before capture's lock,
including when no record exists. Static per-field installation evidence is cached
once; live readiness combines owner, actual device, recovery and permanent-veto
state. Admission additionally requires the canonical validated package and two
actually prepared workers. Failed Reset cannot be bypassed by repeated Present.
Worker construction, graph I/O, waits, transcoding and a second clock/scheduler are
absent from this path. Root readiness/setup failures preserve owned cleanup routes.

F8 edges alone emit `media_owned_snapshot`, `media_owned_services` and two
`media_owned_slot` rows. The report copies fixed owner-safe scalar state; wrong
owner or active maintenance/copy reports unavailable/busy, without domain,
registry, COM or worker calls under capture's lock. It includes startup QPC/status,
installation/debt/admission/device/veto, assignment/drain/quarantine/failure masks,
leases and presented-frame/binding/clock/rate counters. It adds no periodic log.

The [root verification record](../../verification/results/media-root-wiring-2026-09-20.json)
separates authored host/native fixtures from real game and native Windows gaps.
The hot path adds bounded maintenance and short immediate-memory locks; capture
exclusion uses ordinary depth updates under the existing mutex and outermost
release/acquire stores, without TLS, additional locking or per-call allocation.
