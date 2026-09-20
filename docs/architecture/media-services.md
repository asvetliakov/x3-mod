# Canonical media Services

`media_services::MediaServices` connects the engine `Services` interface to the
existing Adapter, exactly two LavWorkers, canonical owned Clock and Destination.
It adds assignment and lease bookkeeping, not a decoder, playback queue or clock
algorithm. This checkpoint implements the coordinator and qualifies connected
state behavior; root still owns startup installation, housekeeping placement,
consumer admission and a subsequent actual connected runtime acceptance.

## Stable ownership and bootstrap handoff

Root creates process-lifetime Adapter, Destination, startup Controller and
MediaServices objects. Its `Counter` supplies the documented QPC frequency and a
non-reentrant scalar counter read. `prepare_on_bootstrap` loads PackageConfig
relative to the startup callback's retained proxy module. Preparation owns a
single immutable package snapshot and two canonical worker handles. Each
WorkerConfig independently retains the typed `package_owner`, so immutable file
and directory pins also outlive detached/unsafe worker storage.

Preparation accepts only source 2 with effective flags 8. It copies the immutable
manifest/source paths and starts both services off the engine thread. Partial
start failure requests asynchronous shutdown; no caller waits or joins. A
release/acquire publication transfers the bundle exactly once. After owner
adoption, bootstrap never calls worker main-side methods again. The production
`NativeWorkers` interface forwards directly to two LavWorkers; the fixture
implements that thin transport interface to inject scalar events and commands.

`maintenance_on_owner(Readiness)` must run on an already qualified, regular owner
boundary, even when no media record exists. It adopts preparation, polls each
worker and advances draining assignments. Both private DD workers must actually
report `WorkerState::ready` before initialization is latched and the startup
Controller receives `service_ready()`. That milestone means DD services are
initialized; it does not mean a source graph, sample or first picture is ready.
`ready()` additionally requires the complete root-supplied installation/domain
readiness and no explicit admission closure. Per-graph failure does not erase a
healthy second worker's initialized state; reusable capacity is checked per slot.

Services never installs patches or enables Consumer. Observers must already cover
initial publications before device/loading work. A delayed bootstrap is not
permission to install them late. Initial source requests before readiness and
actual worker-ready versus first-flight timing remain startup acceptance issues;
there is no engine-thread wait or claim that early scheduling removes contention.

## Assignment, offers and cancellation

Each fixed slot holds one full SessionHandle, nonwrapping assignment serial,
source, logical snapshot, canonical Clock and three actual FrameLease holders.
Two independently admitted engine records may both select eligible source 2;
assignment uniqueness is by record/session identity, never by source ID. New
record lifetimes reset the canonical rate to exact default 1x. No engine address
is delivered to a worker or dereferenced by Services.

`observe_record` forwards complete session/record values to Destination. An
unpublished constructor observation creates an inactive watch; only subsequent
actual published flags authorize a bound destination. There is no late raw table
or wrapper read.

`publish` first synchronizes the logical snapshot, publishes full desired state,
then offers at most one canonical command per slot/pass. A rejected submit leaves
that command pending. Successful submit and Adapter acknowledgement are adjacent,
non-reentrant operations. An accepted submission with failed acknowledgement is
quarantined and never retried; this defensive failure path is tested with an
explicit transport-contract violation. Production workers have no observer that
can call the engine during submit, publication, lease release or readiness.

Ordinary stop retains the worker assignment, stops the Clock once, revokes local
leases and drains revoked READY transport slots even if no later pump/play occurs.
`cancel` is record/constructor retirement: it invalidates local revision, revokes
held leases, cancels the watch and publishes the exact current cancellation with
`live=false`. It does not shut down or replace the DD worker.

A draining assignment remains occupied until
`LavWorker::poll_assignment_quiescent(const Publication&)` acknowledges that exact
latest cancellation. The canonical worker additionally checks a monotonic desired
publication serial, consumed cancellation, no graph, no local/mailbox command,
empty ordinary publication, acknowledged terminal facts and all three physical
slots FREE. Services also requires zero active pump depth and zero held/copy
leases. No timeout or `WorkerState::ready` substitutes for this acknowledgement.
Unsafe retention quarantines the worker; the other slot continues independently.

## Clock, events and physical leases

Runtime operation/epoch and Clock operation/generation are explicitly mapped;
their numeric values need not match. New playing operations call canonical
`begin`; new playing epochs call `seek`. Same-epoch end-bound changes use the
reviewed canonical `set_end`, preserving queued metadata, generation, rate and
continuous position. Pre-play/stopped seeks store source position without
inventing a playing operation. Position queries observe committed milliseconds
without calling destructive Clock update.

Rate transactions decode the existing positive engine rate, validate the current
assignment and exact supplied value, take one counter observation and call
canonical `set_rate`. Success increments local revision. A returned rate thunk
is for the existing immediate, non-reentrant Adapter transaction; it is not a
retained capability across slot reuse or subsequent rate-thunk requests. Rates
persist across operations/seeks of a record. No provider SetRate is introduced.

Each pass polls a bounded number of cumulative terminal and ordinary events,
including retired/stale identities so transport acknowledgement can progress.
Current failure is latched and later returned through PumpResult; Services does
not independently dispatch callbacks or terminal Adapter transitions. Prepared
events may update preparing to playing, but cannot erase already observed EOF or
failure. Older tuple/graph/revision facts cannot revive current scheduling.

EOF is admitted to Clock only after previously published READY frames have been
ingested, or all three actual slots are already held. The pending selection, FIFO
and temporary copy lease together occupy at most those same three physical slots.
FIFO entries are only indices/tokens for real leases; no pixel bytes are copied
into a second queue. A terminal poll overtaking acquisition cannot cause final
frames to be rejected as after-EOF. Graph retirement alone is neither EOF nor
presentation completion. The manager alone initiates a loop seek after a real
Clock endpoint; Services never issues an independent loop restart.

## Copy and reentry

Pump validates the Consumer and Adapter traversal, rejects same-slot nested pump
without queue mutation, ingests bounded leases and runs canonical Clock update
before any external D3D call. It releases exactly the consumed/dropped leases and
moves the selected lease into an independent stack owner. Missing destinations
may retain one still-eligible selection for retry, within the three-slot limit;
supersession, interval expiry, stop or end releases it without timestamp resubmit.

Destination receives the real lease and a stable CurrentCheck ticket containing
assignment serial, full runtime tuple, traversal and local revision. It owns copy
admission and destination Unlock/final release ordering. After it returns,
Services rechecks only stable values. A written/current result commits frame
sequence and binding acknowledgement; it never restores an earlier Clock copy.
Nested rate, seek or stop therefore cannot be overwritten by outer cleanup.
Retired traversal returns the Adapter's pointer-free invalidation route.

Lock/Unlock/descriptor failure or missing/replaced destination is availability,
not decoder failure or EOF. Existing D3D contents remain during underrun. The
Clock can subsequently reach its independent real endpoint, but no copy result
advances an endpoint or dispatches a completion callback.

## Qualification and integration limits

The [compact record](../../verification/results/media-services-2026-09-20.json)
and [media ledger](../verification/media-cues.md#canonical-services-coordinator-2026-09-20)
bind the four frozen source/test files and execution provenance. The connected
host fixture passes 1,783 checks and repeats them under ASan+UBSan. It uses actual
Adapter, reviewed Clock, canonical FrameStorage/FrameLease and Destination copy
core with synthetic workers and an injected CopyBackend. A thousand empty owner
and pump passes allocate zero memory. Strict Win32 x86 object compilation passes;
there is no native worker, COM/DD/D3D, game or Windows runtime execution here.

While dependencies were separately owned, tests used a root-authorized symlink
overlay. The canonical bound setter came from `/tmp/x3-media-clock-bound`, worker
API from `/tmp/x3-media-worker-sample`, and Destination from
`/tmp/x3-media-destination-integration`. Root must integrate reviewed dependencies
and qualify an actual connected build. Documentation-time dependency hashes are
not presented as a contemporaneous compiler-input manifest. Temporary binaries
and the x86 object were removed automatically; their hashes were not captured.
The retained sanitizer file contains fixture stdout, not a compiler transcript.

The coordinator adds bounded two-slot work only at owner housekeeping, command
publication and media pump. Event reads are capped at four per slot/pass; frame
acquisition/draining at three; FIFO/selection storage is fixed. Package/config
allocation and worker creation occur only during bootstrap. No per-draw work,
engine wait, per-frame allocation or second decoder queue is introduced. The
allocation check measures the tested empty passes, not all decoder behavior or
actual game performance. The existing injected CPU/LastError envelopes and
Destination ordinary-return cleanup contract remain their owners' obligations;
this host test and object compile add no new SEH or native ABI guarantee.
