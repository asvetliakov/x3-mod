# Run 19: fast loading-path co-activation

Run 19 accepts co-activation of the fast resource reader, DAT-handle pool and
fast mesh-adjacency service on the visited X3 workload. All three requested
routes installed, performed substantial work and stayed inside their admitted
paths. There were no reader faults, DAT-pool errors/capacity events, or
adjacency fallbacks/faults. The user noticed no stutter or other issue, but also
no obvious loading improvement; the sector change may have been marginally
quicker.

This is a functional fast-mode acceptance, not a controlled loading benchmark.
The presentation gaps are shorter than run 18's verify-mode gaps, but the
sessions differ in sequence, cache state and diagnostic work. Those differences
must not be assigned to the fast paths.

## Provenance and configuration

The existing helper snapshot is
`/tmp/x3-bottleX3-run19/session-20260913-184230-212.log`: 10,655,701 bytes,
213,365 lines and SHA-256
`58bdbea736c3a057c3c989cdc17406e64295e8c3c12deb332d747e1292bd2474`.
It is byte-identical (`cmp`) to the completed live log at
`/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures/session-20260913-184230-212.log`;
no second snapshot was made. The log reaches frame 9,430 and the last Present
at 155.655 seconds after proxy initialization; the first successful Present was
at 3.675 seconds.

The installed `d3d9.dll` remained the run-18 build: 12,368,396 bytes, SHA-256
`b11bff61b2a2f798b565969d8684923781d2b1bec0e3b22cf0cbe2f024d45702`.
The runtime reports renderer 0.4, schema 2 and a 32-bit process. All 23 named
loading hooks installed. The requested rows report `resource_reader mode=fast
installed=1 status=active`, both DAT sites active, and `mesh_adjacency
mode=fast`. The first incoming adjacency state was accepted and restored
(`x87 control=0x023f`, empty tag, `MXCSR=0x9fc0`).

This command did not enable the crypto cache, gz read-ahead, engine loading
probes, mesh cache or sampling profiler: `loading_trace` reports
`crypt_cache=0 probes=0`, there is no `gz_buffer` row, and there are no
`profile_*` blocks. The local streamed analyzer output is
`/tmp/x3-run19-loading-profile/loading-profile.{json,md}`.

## Fast-path route and work

| Path | Completed work | Route/fault result | Diagnostic time |
| --- | --- | --- | ---: |
| Resource reader | 4,229 calls; 4,096 handled (96.86%); 789,601,301 B encoded input and 2,101,442,995 B output | 133 fallbacks, all ordinary non-gzip inputs; every other fallback reason zero; no fault line | 11.839 s total; 187.480 ms maximum |
| DAT handles | 3,528 logical opens; 3,516 reused (99.66%); 12 real opens | 0 errors; 0 full-table events; 12 held entries | not isolated |
| Mesh adjacency | 7,642 calls/computations; 6,618,064 faces and 10,537,256 vertices | 0 fallbacks, faults or native calls; no verification counters expected in fast mode | 0.815 s total; 13.150 ms maximum |

The reader's measured phases were 10.850 s in the single zlib inflate (91.65%
of its total), 0.878 s reading, 0.029 s scanning and 0.016 s allocating. The
remaining 0.066 s was outside those four phase counters. This explains the limited headroom
inside the reader itself: it has removed the game's per-kilobyte calls and
other structural overhead, while most of its remaining time is the same bundled
zlib used by the original path. Run 19 does not execute the original reader, so
it supplies no same-run reader speed ratio. Run 18's exact verify coverage is
what admitted this route; run 19 does not repeat equivalence comparison in fast
mode.

The adjacency timing does show the intended route change on matching logical
phase vectors. The save-load `GenerateAdjacency` row fell from 1.966--2.018 s
over 3,342 calls in run 18 to 0.524 s over the same count here. The 1,017-call
initial-menu row fell from 0.513 s to 0.133 s, and the approximately 2,216-call
sector row from 0.995 s to 0.281 s. These are instrumented operation costs, not
whole-gap speedups.

## Loading phases and limits

The analyzer has only mechanical Present-gap markers; there are no UI phase
events or sampling blocks. Its four gaps over two seconds are:

| Label | Interval after initialization | Run 19 gap | Hooked exclusive | Outside import hooks | Run 18 reference |
| --- | ---: | ---: | ---: | ---: | ---: |
| Menu load | 4.898--12.595 s | 7.284 s | 1.750 s | 5.534 s | 10.425 s |
| Save load | 19.272--60.916 s | 41.571 s | 12.905 s | 28.667 s | 48.573 s and 44.905 s |
| Sector change | 132.900--137.994 s | 4.586 s | 1.673 s | 2.913 s | 6.798 s and 6.455 s |
| Return/menu work | 148.567--154.833 s | 5.997 s | 1.340 s | 4.657 s | 9.777 s and 10.042 s |

The save and sector vectors are recognisable: the save still makes 14,461,803
`gzread` calls and 3,342 adjacency calls, while the sector phase makes 2,216
adjacency calls. Descriptively, run 19's save gap is 3.334--7.002 s shorter and
its sector gap 1.869--2.212 s shorter than run 18's occurrences. Cache warmth,
run order and verify-mode extra work prevent treating either range as a
controlled gain. The user's observation of no obvious save-load improvement is
therefore consistent with the evidence.

The fast reader is inside the proxy rather than the main executable's import
table, so its own time appears in the analyzer's "outside import hooks" column.
The nearest cumulative reports around the save gap bracket about 5.51 s of
reader work. More decisively, the save's 22.991-second report stall contains
6.971 s in listed import hooks but only about 0.405 s of additional reader
work. Roughly 15.6 s in that interval remains outside both measurements; the
report boundaries and concurrent-update rules make that a diagnostic bound,
not exact additive attribution.

The save stream's 14,461,803 tiny `gzread` calls account for 2.094 s of the
hooked table, including 1.147 s of measured wrapper-tail overhead. Gz read-ahead
was intentionally absent. Existing fixture evidence says read-ahead is useful
with telemetry to remove observer overhead, but offers no plain-game speedup;
it should not be presented as the missing user-visible optimization.

The stronger existing next-step evidence is the crypto cache, which was also
absent here. Run 17 measured the same 844-check signature batch shrinking from
12.835 s to 0.135 s in the signature probe and observed a 27.574-second save
gap, while preserving every hash and RSA verification. A combined acceptance
run should therefore retain the now-accepted reader/DAT/adjacency fast modes
and enable the reviewed crypto cache. If further attribution is needed, enable
the sampling profiler and loading probes, plus gz read-ahead to suppress the
telemetry-only tiny-call overhead. Without those probes, run 19 cannot identify
the engine functions responsible for the remaining uninstrumented interval.
