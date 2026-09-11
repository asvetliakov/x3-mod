# Managed upload CPU costs

The portable buffer path passes a separate **36-case / 2,768-check** timing fixture
on CrossOver Preview. Removing native WRITEONLY for MANAGED storage produced no
consistent CPU penalty in this small sample. Finite-atlas maintenance and
classification remain significant costs, particularly for large VBs. These
measurements do not establish game loading time or GPU upload performance.

Evidence: [summary and all component medians](../../verification/results/managed-upload-performance-summary.json),
[report](../../verification/results/managed-upload-performance.txt),
[original fixture](../../verification/probe/managed_upload_performance.cpp),
[runner](../../verification/probe/run_managed_upload_performance.py).

## Workload and boundaries

Each case creates a new MANAGED buffer, maps the entire allocation with NOSYSLOCK,
memsets it to zero and unlocks it. VB, INDEX16 and INDEX32 use 64 KiB, 1 MiB and
8 MiB allocations. Four modes perform identical public calls: native WRITEONLY,
native readable usage 0, wrapped WRITEONLY with finite capture off, and wrapped
WRITEONLY with finite capture on (the native backing is readable). The wrapped
off mode still tracks ordinary buffer revisions.

Every case has two warmups and seven measured new-buffer trials. Creation,
Lock, memset and Unlock each have their own median in the report. Their sums
are **not** measured end-to-end medians. Descriptor checks verify requested versus
actual backing Usage outside the timing intervals. Finite-mode trials also
verify actual finite vertex positions or complete zero-valued index bounds
outside the measured intervals. WRITEONLY mappings are written and never read.

There are no draws, first-use GPU uploads, fences measuring GPU completion or
payload readbacks. QPC boundary overhead is not subtracted. Modes run in a fixed
order, with a hidden standalone native device for each mode; allocator/runtime
warmth can affect comparisons. Seven trials do not justify a general speedup or
causal claim about the usage-bit change. The runner records source, executable,
report and native-input hashes before/after; native hashes are provenance data,
not version allowlists.

## Selected 8 MiB component medians

All entries below are milliseconds, rounded from the retained microsecond report.

| Kind and mode | Create | Lock | memset | Unlock |
| --- | ---: | ---: | ---: | ---: |
| VB native WRITEONLY | 0.028 | 0.158 | 1.393 | 0.013 |
| VB native readable | 0.029 | 0.100 | 1.243 | 0.010 |
| VB wrapped finite off | 0.030 | 0.107 | 1.299 | 0.013 |
| VB wrapped finite on | 0.516 | 1.971 | 1.356 | 4.444 |
| INDEX16 native WRITEONLY | 0.018 | 0.111 | 1.274 | 0.011 |
| INDEX16 native readable | 0.111 | 0.008 | 1.180 | 0.009 |
| INDEX16 wrapped finite off | 0.027 | 0.106 | 1.262 | 0.011 |
| INDEX16 wrapped finite on | 0.026 | 0.114 | 1.216 | 1.928 |
| INDEX32 native WRITEONLY | 0.022 | 0.107 | 1.241 | 0.010 |
| INDEX32 native readable | 0.026 | 0.102 | 1.225 | 0.009 |
| INDEX32 wrapped finite off | 0.030 | 0.112 | 1.208 | 0.014 |
| INDEX32 wrapped finite on | 0.025 | 0.109 | 1.195 | 0.593 |

At 1 MiB, finite VB Lock/Unlock medians were 0.256/0.548 ms, compared with
0.013/0.009 ms for wrapped finite-off. This is an actionable CPU cost to keep in
future loading measurements. The timed methods include public COM observation,
atlas write preparation and classification; this fixture does not attribute each
instruction or prove that all of the difference belongs to one helper.

Native readable and WRITEONLY results are close enough at the larger workloads
that this run does not suggest an additional expensive copy caused by the
creation conversion. It cannot rule out costs deferred until drawing. The
[geometry benchmark](geometry-performance.md) separately measures evidence query
and lease work, and the [portable contract](portable-managed-upload.md) documents
correctness and compatibility limits. No production source was changed for this
benchmark and the game was not launched.
