# Asynchronous ID2 video playback direction

2026-09-20. Architecture direction, not a production implementation or installed
feature. The current candidate is described only in [status](../status.md).
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

## Current verification boundary

The worker fixture uses three CPU slots and 18 admitted frames across zero,
ten-second and2.2-second positions. It must observe a naturally full queue,
cancel its generation, reject stale admissions, preserve exact reference RGB
and source start/end timestamps, and retire safely. Actual main D3D heartbeats
run during worker operations, with gaps and API durations reported rather than
a claim that private ownership guarantees freedom from stalls. A separate
16-picture audited suffix checks natural EOF and delayed final-picture delivery.

Neither fixture installs engine hooks. Remaining production work includes owned
clock/scheduling, verified patch spans and ABI, safe callback continuations and
record retirement, destination recovery/Reset, save/restore behavior, bounded
worker/module lifetime, performance and native-platform qualification. No
production merge is authorized while the existing candidate is qualifying.
