# Geometry evidence performance review

This checkpoint addresses measured CPU overhead in the newly added finite-buffer
evidence and motion geometry lease paths. It does not install a game build or
enable live motion replay. The preceding motion integration evidence is preserved
at commit `0ce0814`; shared result files may be refreshed by this checkpoint.

## Reviewed changes

The closed-buffer query previously authenticated a sidecar using a full native
inspection, then immediately performed another full inspection inside
`validate_closed`. The internal closed-acquisition mode now uses that existing
closed validation once, followed by the same mandatory private-IUnknown
authentication. The native qualifier and its backend, slot, import, descriptor,
heap and mapping checks are unchanged. Upload observation retains its existing
open-mapping path. Independent review accepted the ordering under the existing
serialized caller contract; it does not establish live write exclusion.

Lease handles now encode a fixed slot beneath a shared nonreused serial. Lookup
checks the full stored handle and frame identity. A fixed free list replaces
repeated admission scans, and frame retirement carries its scan cursor forward
across bounded cleanup batches. Global count and byte reservations remain charged
until native and sidecar cleanup actually finish, even when the detached table
slot is reusable. Root and an independent reviewer checked stale identities,
frame reuse, bounded indexing, serial exhaustion and concurrent retirement.

## Measurement and remaining costs

The benchmark runs synthetic indexed buffer operations against the pinned
CrossOver Preview backend, with no draws or game launch. It freezes separate
executables for the baseline, qualification-only change and combined change.
Each stage records seven trials after warmup for four batch sizes and three
allocation/range profiles. Qualifier timing is nested within acquisition and
inspection timing and must not be added again. Frozen intermediate source maps
are historical evidence, not assertions that current source matches each stage.

Native qualification still dominates the measured successful path. Sidecar list
traversal, repeated shader hashing/lookup and per-boundary motion resource creation
remain further optimization candidates. GPU resources currently live only within
the diagnostic callback to satisfy teardown ordering; retaining them requires a
reviewed lifecycle owner. These measurements do not establish game FPS, loading
speed or suitability for every-frame motion replay.

The disabled motion producer allocates no observation/history vectors; its
observation and matrix-history vectors retain capacity across active frames. The draw reader uses fixed 16 KiB
shader scratch rather than allocating it per draw. The new lease free list uses
fixed storage. These source checks do not measure the entire capture logger or
turn synchronous diagnostics into an every-frame rendering path.

The [benchmark report](geometry-performance.md) retains all three timing stages.
For 700 shared-small leases, median acquisition fell from 40.035 to 19.483 ms and
inspection from 39.990 to 19.609 ms. Invalid-handle lookup fell from 3.444 to
0.059 ms per 700 calls. Single-lease retirement increased from about 4.8 to
9.7 microseconds; the fixed table scan remains. Normal-path differences between
the two optimized stages include machine/run variation, so the report does not
attribute that entire difference to direct indexing.

## Regression evidence

| Verification | Result |
| --- | --- |
| Benchmark, each of three stages | 84 timing samples / 831,397 checks |
| Native managed-buffer contract | 461 checks |
| Finite upload observer | 403 checks |
| Geometry leases | 347 checks |
| Draw-input reader | 260 checks / 74 state snapshots |
| Private motion producer | 253 checks / 40 numeric samples / 28 GPU and 28 CPU-state comparisons |
| Production DLL integration | 20 cases / 20 native Clear CPU-state witnesses |
| Forced native fallback | 18 compiled proxy/renderer objects |

Review added a native-open-map regression because wrapper pending/revision checks
alone could hide an accidentally omitted native closure check. The new control
proves rejection with `NativeContract` while wrapper pending remains zero, keeps
evidence unknown after native Unlock alone, and restores it only after a full
observed upload. The lease controls additionally reject forged serials, exchanged
handle types and stale handles targeting a reused slot without releasing its new
reservation. The existing delayed native-destruction quota control still passes.

Independent reviewers checked production changes, fixture controls and current
source/native/executable/report hashes. The optimized ownership source SHA256 is
`e94793d53e82329d2cc8136d2baaecb0957c10771f4bbd5b88bbbb104949159f`.
Unchanged broader component coverage remains at `0ce0814`; this checkpoint reruns
the components affected by these two ownership changes.

The verified production DLL SHA256 is
`e45aaac050aa3c0c816776a72a5877f951ee5d87efc6d58493740175aee8d282`.
The forced-fallback verification DLL is
`b371d99a5ef70a8b7a55005bafa4120d8a0ddb85faba924fee86fc6acab009a3`.
Production symbol checks retain the real qualifier and exclude fixture issuers.
The installed iteration-5 DLL was independently rehashed unchanged as
`ed19a7abf54ae2b9debf912f3d343a0c9217038a2162cb6a9eb174fc8050bbd5`.
Live motion replay remains refused with `write_exclusion_unavailable`.
