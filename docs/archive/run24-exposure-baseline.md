# Run 24 A: automatic-exposure baseline

Run 24 is the bloom-off side of the automatic-exposure comparison. The new
space-aware meter and AgX write-back route were active and fault-free. The
meter was not visually obvious because its target was pinned to the configured
**+2 EV ceiling in 480 of 485 active reports (98.97%)**; applied exposure was
already +2 EV in 464 reports (95.67%). Every one of the 16 captured frames used
target and applied EV +2, despite their different measured luminance.

The route did adapt during one short sparse-scene interval: its target switched
to neutral EV 0 when fewer than 1% of meter tiles were lit, applied exposure
fell as low as +0.755 EV, and then recovered to +2. This establishes that
automatic adaptation ran; most visited content simply requested the same upper
bound. The +2 limit, a fourfold multiplier, is consistent with the user's
report that the earlier severe overexposure did not return. AgX, legacy scene
encoding and the scene content also affect the displayed result, so the meter
alone is not assigned as the cause of perceived brightness.

The user still saw target-selection stutter with the custom chase camera
disabled. Long-frame summaries corroborate recurring stalls, including a
14-window cluster, while measured renderer spans remain much shorter. The
vanilla run emits no target-transition marker, so this log cannot pair the
user's selection with an exact frame.

## Provenance and actual configuration

The existing helper snapshot is
`/tmp/x3-bottleX3-run24/session-20260913-195919-212.log`: 257,916,370 bytes,
4,812,378 lines and SHA-256
`96a5377ea65ba47022149249c2b7989c170dd7b78455cbb95bccc404b350d8da`.
The helper already verified it against the completed live log at
`/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures/session-20260913-195919-212.log`;
no additional snapshot was made. It reaches frame 28,928 and a last Present at
480.490 s. The tail contains complete telemetry, loading, reader and DAT-pool
summaries rather than a partial record.

The installed checkpoint remains `10e447b`; its 12,884,608-byte `d3d9.dll` has
SHA-256
`d8f67c33e0139606f4624600ddc0b9ef95faa1abf14914329016e51f01dd6ee3`.
The runtime configuration is the requested A side:

| Area | Actual route |
| --- | --- |
| Exposure | Automatic meter enabled and ready; gamma-2.2 decode; AgX; offset 0; EV range -3 to +2; 0.4/1.2 s up/down response; 1/512 background threshold; 1% minimum lit share; 0.9 white target; 0.25 EV deadband. No manual EV. |
| Bloom | Off. There are no bloom mode, prepare, commit or frame rows. `bloom_copy_seen` in general frame diagnostics only observes the game's handoff boundary. |
| Materials | Off. There are no linear-material mode, variant, frame or refusal rows. |
| Camera | Vanilla. There are no custom chase-camera or chase-transition rows. |
| TAA | Motion output, ownership, object trace/lifetime, depth, eight-sample jitter, TAA and debug capture active; sharpen 0 and mip bias 0. |
| Loading | Crypto cache, 256 KiB gz buffer, fast resource reader, DAT handles and fast adjacency active; sampling profiler off. |

HDR attach reports the FP16 target, AgX shader, two-channel meter chain and all
self-tests successful. No telemetry metric failure, HDR fallback/unwind,
MSAA refusal, readback error, motion apply/restore failure or named fault row
occurs. All 16 captured frames resolved TAA with valid history and depth, with
no camera cut.

## Meter behavior by phase

Startup and menu state are distinct from gameplay. Frames 60--660, before the
main save completes, report no redirected HDR scene, meter status 1, no
adaptation step and EV 0. The apparent floor luminance in those rows is retained
state, not a gameplay reading. The first active report is frame 720 after the
20.499 s save gap. It has 46 cumulative meter steps, a +2 target and +1.513
applied EV. The next reports converge through +1.904, +1.982, +1.996 and
+1.999 at frames 780, 840, 900 and 960. Frames 28,860 onward are back in the
menu: status 1, no step, and the last gameplay EV +2 is retained rather than
new menu pixels driving adaptation.

Across frames 720--28,800, 485 reports have successful current meter and
readback results and 3,840 tiles. The target takes only two values:

| Target | Reports | Meaning in this run |
| --- | ---: | --- |
| +2 EV | 480 | The fresh key/highlight policy requested at least the configured upper bound. |
| 0 EV | 5 | Only 31--34 tiles were lit (0.81--0.89%), below the 1% minimum-lit rule, so the policy selected neutral exposure. |

The neutral reports occur at frames 18,300, 18,420, 18,600, 18,660 and 18,720,
approximately 312.741--317.194 s. Because intervening reports briefly exceeded
the lit threshold, the target alternated between 0 and +2. Applied exposure
fell from +2 to +0.755 by frame 18,720, reached +1.907 after the target returned
to +2 at frame 18,840, and was effectively +2 again by frame 18,960
(approximately 321.038 s). This is real policy and temporal response, although
the one-second reporting cadence does not expose every intervening frame.

The meter remained below its +2 target ceiling only during initial convergence
and that sparse-scene interval. This explains why moving between most dark and
bright content did not produce an obvious exposure change: the fresh target
did not change. Bright local stars or nebula can coexist with a low image-wide
lit statistic and do not by themselves prove that exposure should move.

## F8 exposure baseline

Four complete four-frame bursts were captured. These labels identify only their
order; the log does not encode the user's dark/bright/turn semantics.

| Burst / time | `avg_log_l` | Mean luma | Lit tiles | p99 tile max | Fresh/applied EV |
| --- | ---: | ---: | ---: | ---: | ---: |
| 9823--9826, 158.056--162.805 s | -8.358 to -8.347 | 0.003048--0.003072 | 59.95--60.23% | 0.764--0.774 | +2 / +2 |
| 10398--10401, 176.423--181.192 s | -7.739 | 0.004680--0.004682 | 67.79--67.81% | 0.779--0.792 | +2 / +2 |
| 28288--28291, 452.728--456.628 s | -11.259 to -11.258 | 0.0004081--0.0004084 | 18.59--18.85% | 0.490--0.504 | +2 / +2 |
| 28464--28467, 460.169--464.200 s | -10.416 to -10.415 | 0.0007318--0.0007325 | 29.09--29.22% | 0.620--0.630 | +2 / +2 |

In the four bursts, the unconstrained key and highlight limits are all above
+2 EV: approximately +3.98 to +4.78 and +4.21 to +4.90 respectively. The
configured range therefore clamps every fresh target to +2. The first frame of
each burst reports a normal 21--24 ms adaptation delta; deliberate readback
stalls clamp subsequent deltas to the designed 200 ms maximum. Exposure is
already at the target, so those capped deltas do not alter it.

Every frame has successful 1280x768 HDR and TAA RGBA16F, motion RGBA32F,
pre-resolve color and presented BGRA8, and depth R32F files. All 96 logged
readbacks exist and total 692,060,160 bytes. The directory also holds 59 shader
binaries (175,692 bytes), for 155 non-log files and 692,235,852 bytes total.
The saved HDR plane is the unresolved FP16 target; the scalar fields above are
the live meter's actual resolved-TAA input statistics from `hdr_frame`, not a
reconstruction from that file.

## Loading route

All combined loading paths stayed admitted and fault-free:

| Path | Completed work | Admission/fault result |
| --- | --- | --- |
| Resource reader | 4,227 calls; 4,097 handled (96.93%); 789,244,287 B input, 2,102,136,758 B output; 11.892 s | 130 fallbacks, all non-gzip; every other reason zero |
| DAT handles | 3,526 opens; 3,514 reused; 12 real opens | zero errors and full-table events; 12 held entries |
| Mesh adjacency | 11,843 calls; 9,974,747 faces; 1.243 s | all computed; zero fallback, fault or native call |
| Crypto cache | 2,538 acquires; 845 hits / one cold miss; 846 imports / 845 import hits | zero failed/busy/import passthrough or eviction; provider and key retained |
| Gz read-ahead | Main save: 14,461,803 logical calls / 45,754,974 B through 175 real reads | zero direct reads and errors |

The mechanical Present-gap analysis at
`/tmp/x3-run24-loading-profile/loading-profile.{json,md}` finds an initial
unlabelled 2.811 s gap, a 7.495 s menu load, the 20.499 s bulk-save load, sector
changes of 4.773 and 5.490 s, and 6.991 s return/menu work. It also labels a
7.530 s interval at 144.223--151.822 s as a save load because one `gzopen`
occurs, but there are no bulk `gzread` calls in that interval; treat the phase
identity as uncertain. The main save has 8.284 s in traced imports and 12.215 s
outside them. This run adds no controlled loading comparison and no sampling
profile.

## Selection stutter and measured spans

Only 113 `frame_end` rows cover 28,928 frames, so their `dt_ms` values are
report-cadence intervals rather than individual frame durations. Excluding all
mechanically identified load gaps and deliberate F8 spans leaves 376 gameplay
`frame_normal` report windows. Their median maximum is 20.323 ms, their 95th
percentile maximum is 467.321 ms, and 23 windows contain a maximum of at least
0.4 s; the largest is 753.527 ms.

Fourteen consecutive windows from 229.673 through 242.567 s contain
0.467--0.508 s maxima. Other long frames occur in isolated or short clusters,
including 130--134, 154--156, 170--171, 355--357, 395--396, 419--422,
439--440 and 447--449 s. The run has no custom chase/transition events, so none
is an exact selection timestamp. The user's direct report establishes that the
selection stutter occurs in vanilla camera mode; its persistence excludes the
custom chase-camera apply/smoothing path as a necessary cause, while leaving
the native target-selection, VM and central-HUD work open.

Within those 23 long-frame windows, the largest measured renderer spans are
3.653 ms for TAA, 3.224 ms for HDR write-back, 3.151 ms for the exposure meter,
0.885 ms for Present, 0.438 ms for meter readback and 0.197 ms for route fill.
All report zero failures and are far below the 0.4--0.754 s frame maxima.
Loading and capture intervals were excluded from these figures. A targeted
timer around the native selection/VM/HUD path remains the evidence-supported
next diagnostic; this baseline cannot infer its cause from missing renderer
time.
