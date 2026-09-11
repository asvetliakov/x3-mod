# Geometry lease CPU performance

The portable managed-buffer path and bounded sidecar index substantially reduce the measured CPU cost of geometry evidence. It uses documented resource descriptors, readable MANAGED backing and observed upload state instead of private backend endpoint/map-count qualification. The existing direct handle indexing, fixed free-list and forward retirement cursor remain in place. The measurement includes this change to the [reviewed portable resource contract](portable-managed-upload.md).

These are synthetic CPU wall times on the recorded CrossOver Preview backend. DLL hashes identify the test runtime; they are not feature admission requirements. They are not GPU timings or game FPS results. Actual live motion submission remains refused pending a verified replay-versus-write exclusion contract; the measurements do not override that restriction.

## Method

The production ownership module is compiled with the same i686 SSE2/stack settings as the existing fixtures. Each stage runs 84 recorded samples: three profiles, 1/100/700/4,096 indexed leases, seven trials after one warmup. Buffers are created and uploaded before timed loops. No draw or Present is issued. Timed phases are acquisition, inspection and end-frame retirement; public finite/index queries, invalid-handle lookup and native AddRef/Release are separate controls. Allocation of fixture vectors and output formatting occur outside timed phases. Qualifier ticks overlap acquisition/inspection and must not be added to them.

Each stage freezes a distinct executable after equal pre/post-build source hashes. Source work may proceed afterward; the historical source map identifies that executable rather than claiming it matches current code. The runner checks native hashes, executable immutability, 831,397 successful assertions, the exact 84-sample inventory, a unique terminal PASS, positive QPC frequency and finite nonnegative timing values. The historical baseline/qualification timing fields were also checked offline against these tightened acceptance rules.

## Current bounded sidecar index

The next measured bottleneck was repeated allocation-sidecar list lookup. A weak intrusive 2,048-bucket index now locates a canonical allocation without scanning every owner allocation. Existing COM identity/descriptor/private-sidecar authentication and observed revision checks remain in place. See [index lifetime, budget and collision controls](finite-sidecar-index.md). Owner storage increases by 8 KiB on x86, bounded by the existing 64-owner cap; the per-owner allocation performs this initialization, with no lookup allocation. Expected lookup cost improves; deliberately colliding keys can still form bounded chains.

The unchanged 84-sample/831,397-check benchmark and geometry 421-check regression pass on the indexed source. Medians below are milliseconds; “before” is the retained portable contract stage immediately preceding this index.

| Profile | Leases | Acquire before → index | Inspect before → index | Retire before → index | Public evidence before → index |
| --- | ---: | ---: | ---: | ---: | ---: |
| shared_small | 700 | 1.210 → 1.192 | 1.267 → 1.242 | 0.122 → 0.117 | 1.694 → 1.640 |
| shared_small | 4,096 | 6.932 → 6.864 | 7.402 → 7.323 | 0.662 → 0.619 | 9.742 → 9.594 |
| many_small | 700 | 5.782 → 1.244 | 5.625 → 1.294 | 0.149 → 0.115 | 6.058 → 1.692 |
| many_small | 4,096 | 26.138 → 7.176 | 25.843 → 7.559 | 0.755 → 0.630 | 28.423 → 9.819 |
| shared_varying_range | 700 | 2.363 → 2.304 | 2.461 → 2.401 | 0.136 → 0.119 | 2.848 → 2.774 |
| shared_varying_range | 4,096 | 15.492 → 13.497 | 15.068 → 14.007 | 0.665 → 0.624 | 17.840 → 16.334 |

For 700 leases with 1,024 small allocation pairs present, acquisition plus inspection falls from 11.407 ms to 2.537 ms. The shared-small control changes from 2.476 ms to 2.435 ms. This isolates the practical allocation-diversity penalty much more clearly than comparing total time alone. Descriptor-qualification spans in the many-small case change only from 0.434/0.435 ms to 0.361/0.359 ms; most of the improvement lies outside that narrow timer. Normal run variation remains present.

The varying-range control still spends 4.705 ms in 700 acquire/inspect pairs because it also exercises 256-position range work. At 4,096 many-small leases the total remains 14.735 ms. These costs are not a complete renderer frame budget: reader queries, recording, GPU submissions and other passes are separate. Setup, upload throughput and broad hardware distributions are not covered. This index does not broaden geometry eligibility or enable live replay.

The fixture vectors, warmup, source workload, allocation sizes and conservative retained-byte charges are unchanged. New source/native/executable/report hashes and the historical portable baseline are bound by `geometry-lease-performance-sidecar-index-comparison.json`. Both stages retain all per-trial values; no historical source map is relabeled as current.

## Portable contract stage, before sidecar indexing

The unchanged benchmark source performs exactly the same 84 samples and 831,397 checks as the retained optimized-stage executable. The new portable run passed with equal source maps before/after compilation and execution. The old source/executable map is retained explicitly; the comparison does not claim that historical code matches current source. Native Windows execution remains untested.

Medians in milliseconds for the shared 48-byte VB/12-byte IB profile:

| Leases | Acquire before → portable | Inspect before → portable | Retire before → portable | Public evidence before → portable |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 0.029 → 0.002 | 0.028 → 0.002 | 0.010 → 0.006 | 0.029 → 0.003 |
| 100 | 2.795 → 0.170 | 2.806 → 0.179 | 0.025 → 0.024 | 2.832 → 0.235 |
| 700 | 19.483 → 1.210 | 19.609 → 1.267 | 0.113 → 0.122 | 20.003 → 1.694 |
| 4,096 | 114.332 → 6.932 | 115.182 → 7.402 | 0.618 → 0.662 | 117.266 → 9.742 |

At 700 shared-small leases, acquisition plus inspection falls from 39.092 ms to 2.476 ms. Their nested descriptor-qualification spans are now 0.365/0.363 ms, versus 18.663/18.735 ms previously. These spans overlap the phase totals. Most remaining cost is outside that narrow descriptor timer; it must not be attributed entirely to one operation without another measurement.

The varying-range profile (256 FLOAT3 positions in a 64 KiB VB) takes 2.363/2.461 ms acquire/inspect for 700 leases. The 1,024-distinct-allocation profile takes 5.782/5.625 ms, versus 24.246/24.148 ms before. At that portable-contract stage, allocation-sidecar list traversal remained linear and was exercised by this profile; the index above addresses that lookup cost. Native calls, synchronization and cache/query work remain part of the measured totals. This checkpoint does not broaden admission to unknown revisions or bypass the required mutation/replay exclusion.

The portable observer intentionally requests readable backing for eligible application WRITEONLY MANAGED buffers while preserving their logical descriptor. Removing the WRITEONLY hint may have a native-runtime allocation/performance tradeoff. This benchmark times warmed lease/query operations after upload; it does not measure full game loading, upload throughput, driver placement or frame rendering. It is not a general every-frame affordability or native-Windows speedup claim.

The remaining sections preserve the earlier pinned-backend optimization stages as historical evidence.

## Historical shared small-buffer results

Medians in milliseconds. One native 48-byte VB and 12-byte IB are reused; every lease still owns its own native and CPU references and charges the full pair size. At 4,096 leases the conservative reservation is 245,760 bytes.

| Leases | Stage | Acquire | Inspect | Retire | Public evidence |
| ---: | --- | ---: | ---: | ---: | ---: |
| 1 | Original | 0.057 | 0.056 | 0.005 | 0.055 |
| 1 | One qualification | 0.031 | 0.030 | 0.005 | 0.031 |
| 1 | Indexed handles | 0.029 | 0.028 | 0.010 | 0.029 |
| 100 | Original | 5.688 | 5.669 | 0.026 | 5.793 |
| 100 | One qualification | 2.957 | 2.954 | 0.026 | 3.044 |
| 100 | Indexed handles | 2.795 | 2.806 | 0.025 | 2.832 |
| 700 | Original | 40.035 | 39.990 | 0.143 | 40.263 |
| 700 | One qualification | 20.577 | 20.784 | 0.146 | 20.992 |
| 700 | Indexed handles | 19.483 | 19.609 | 0.113 | 20.003 |
| 4,096 | Original | 255.353 | 253.718 | 1.108 | 250.580 |
| 4,096 | One qualification | 124.517 | 125.638 | 1.032 | 122.306 |
| 4,096 | Indexed handles | 114.332 | 115.182 | 0.618 | 117.266 |

For 700 shared-small leases, original qualification alone consumed 38.922 ms of 40.035 ms acquisition and 38.801 ms of 39.990 ms inspection. The merged check performs the same fresh closed-resource proof once instead of twice per resource. An indexed reader/acquire/replay sequence therefore needs six full inspections instead of twelve. DLL file hashes were already cached; this does not remove repeated hashing that never existed.

The qualification-only to indexed-handle phases also show machine/run variation in unchanged qualifier cost. Do not attribute every difference in total wall time to table indexing. The direct lookup control is clearer: 700 invalid lease lookups fell from 3.444 ms to 0.059 ms; 4,096 fell from 21.608 ms to 0.347 ms. At 4,096 leases, median acquisition time excluding its nested qualifier span fell from 9.432 ms to 4.752 ms between those two stages (an accounting residual, not a separate pure-CPU measurement).

End-frame retirement at 4,096 leases fell from 1.108 ms to 0.618 ms. The single-lease case increased from 4.8 µs to 9.7 µs in these runs: cleanup still scans the fixed table once, so the change is not a win at every scale.

## Range and allocation diversity

The varying-range profile reuses a 64 KiB VB and 12 KiB IB, changes the requested first vertex and checks 256 FLOAT3 positions. Its 4,096-lease reservation is 304 MiB. The many-small profile rotates through 1,024 distinct 48/12-byte pairs, exercising allocation-sidecar lookup while retaining the same bounded reservation accounting. Results below are medians for 700 leases, milliseconds.

| Profile | Stage | Acquire | Inspect | Retire |
| --- | --- | ---: | ---: | ---: |
| shared_varying_range | Original | 48.943 | 45.458 | 0.171 |
| shared_varying_range | One qualification | 21.779 | 21.846 | 0.140 |
| shared_varying_range | Indexed handles | 21.092 | 21.021 | 0.133 |
| many_small | Original | 45.206 | 45.460 | 0.187 |
| many_small | One qualification | 25.553 | 25.694 | 0.170 |
| many_small | Indexed handles | 24.246 | 24.148 | 0.156 |

## Historical remaining costs and general limits

At the historical indexed-handle stage, private native qualification still dominated: the shared-small 700-lease acquisition/inspection remain about 19.5/19.6 ms, and public evidence queries take about 20.0 ms. This is not cheap enough to claim broadly affordable per-frame live replay. Native validation still checks current mappings/endpoints, module imports and accessible memory; no proof was cached across unknown mutations. Allocation-sidecar lookup still walks the owner list, which is visible in the many-small profile. The portable replacement and current measurements above supersede that private qualification mechanism.

Handle lookup now uses encoded fixed slots plus full serial/frame equality. Serial exhaustion refuses before overflow; a fixed free-list admits without allocation. Retirement carries a cursor through the table once. Slots become reusable after detachment, but native-byte and global lease-count quotas remain charged until actual native/CPU cleanup completes. Existing lifecycle, forged/stale/type-confused handle, native endpoint, delayed-release quota and state-preservation regressions are required after the change.

All timing ranges and component/cache counters are retained; seven sequential warmed trials do not estimate broad hardware/game distributions. The reference controls perform different operations and are not an uninstrumented game baseline. Setup includes native device/buffer creation and qualification, is reported separately, and is not counted as steady-frame CPU time.

## Retained evidence

- Runner: `verification/probe/run_geometry_lease_benchmark.py`; native source: `geometry_lease_benchmark.cpp`; build: `build_geometry_lease_benchmark.sh`.
- `verification/results/geometry-lease-performance-baseline.{json,txt}` records the original source/executable.
- `verification/results/geometry-lease-performance-qualification.{json,txt}` isolates merged qualification.
- `verification/results/geometry-lease-performance-optimized.{json,txt}` records the final combined optimization.
- `verification/results/geometry-lease-performance-comparison.json` binds the three manifests and component residuals.
- `verification/results/geometry-lease-performance-portable.{json,txt}` records the current portable source/executable.
- `verification/results/geometry-lease-performance-portable-comparison.json` binds the historical optimized and portable manifests and matching workloads.
- `verification/results/geometry-lease-performance-sidecar-index.{json,txt}` records the indexed source/executable.
- `verification/results/geometry-lease-performance-sidecar-index-comparison.json` binds the portable and indexed manifests with matching workloads.
- Each stage also retains its build output and Wine log.
