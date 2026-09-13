# Run 23 B: linear-material gameplay comparison

Run 23 is the completed linear-materials-on side of the fixed-EV comparison.
The requested route was active and processed **4,420 of 5,800** otherwise
eligible motion draws in the eight captured frames (76.21% draw coverage),
including 1,036 bump-material draws. The remaining 1,380 draws used their
ordinary motion variants because their exact material pairs are outside the
installed 110-pair contract. There were no combined-stage bind failures or
dynamic HDR/sampler refusals.

The user saw a clear but partial visual change: some station grey panels became
much brighter and less green, and the foreground hull became brighter, while
the background and exhaust looked broadly unchanged. The changed parts also
looked less glossy than the original hull/station materials. That gloss result
is an open final-lighting/reflection tuning issue; it is not by itself a blocker
for this coverage checkpoint unless review finds that the original specular or
cube term was accidentally lost. The captures do not attribute screen pixels
or station parts to draw identities, so the visual boundary cannot be assigned
to a specific pair from screenshots alone.

Selection stutter remained, and the distant selected asteroid still shimmered
until the ship moved closer. The telemetry corroborates long frames around
selection but does not time the native game/VM/UI work that is now the leading
investigation boundary.

## Run identity and completion

The three helper snapshots are byte-identical to their completed live logs
under `/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures/`:

| Run | Snapshot | Bytes / lines | SHA-256 | Extent and interpretation |
| --- | --- | ---: | --- | --- |
| 21 | `/tmp/x3-bottleX3-run21/session-20260913-191311-212.log` | 654,525 / 2,978 | `e1a0603ce66377b8771750f20eaef94d94dc48b2ac28a40dc47d55d2bbb195f3` | Last Present 41.264 s, frame 789; no capture. The user reports it froze while docked after alt-tab and return, then ended it. Cause is unknown and no CrossOver or renderer attribution is made. |
| 22 | `/tmp/x3-bottleX3-run22/session-20260913-191409-648.log` | 8,556,099 / 25,069 | `b138af94c2d9827710e2e4e21d1e116a470eb568ca02ef143cf7452e505707b5` | Full gameplay/return sequence through frame 12,761 and 224.123 s; no F8 readback. This is the completed no-capture B attempt. |
| 23 | `/tmp/x3-bottleX3-run23/session-20260913-191758-404.log` | 152,611,173 / 2,743,546 | `d913f37834241192db6888d691ba8a736167f2c192c44f4c46879081b292c32d` | Full gameplay/return sequence, eight F8 frames, frame 12,000 and last Present 234.385 s. This is comparison B. |

The helper has no external process-exit-status manifest. Completion is instead
established by the user's confirmation, snapshot/live identity, the complete
return-to-menu phase in runs 22 and 23, and internally consistent final reports.
The run-23 tail contains final loading, resource-reader, DAT-pool, camera and
mip-bias summaries rather than a truncated partial row.

The installed source checkpoint remained `10e447b`. Its 12,884,608-byte
`d3d9.dll` has SHA-256
`d8f67c33e0139606f4624600ddc0b9ef95faa1abf14914329016e51f01dd6ee3`,
the same candidate used for run 20 A.

## Actual B configuration

All three attempts report linear materials requested and enabled, with valid
configuration, gamma-2.2 decode and AgX tonemap. Direct, material-emissive and
lightmap-emissive gains are exactly 1. Run 23 otherwise retains the A settings:
HDR/AgX at manual EV 0 with the meter and bloom off; motion output, depth,
jitter and TAA on; sharpen 0.75 and mip bias -0.5; the accepted chase-camera
settings and diagnostics; and the combined crypto-cache, 256 KiB gz-buffer,
fast-reader, DAT-handle and fast-adjacency loading route.

Run 23 created 36 combined material shader objects for 17 distinct stage/hash
identities. All report successful transform and D3D creation. Nineteen rows are
repeated stage/hash identities created for distinct COM objects. The last
occurred by the report at 48.692 s, before the first non-null selection event
at 63.230 s; transformed
shader creation therefore did not occur in the later selection-stutter windows.
The creation rows do not contain compile duration.

## Captured material coverage

The per-draw `motion_route` rows reconcile exactly with each captured frame's
`linear_material_frame` counters:

| Capture | Eligible motion draws | Linear routed | Bump routed | Refused | Linear share | Bind failures |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Frames 10752--10755, distant asteroid | 3,320 | 2,444 | 644 | 876 | 73.61% | 0 |
| Frames 11607--11610, closer asteroid | 2,480 | 1,976 | 392 | 504 | 79.68% | 0 |
| **Total** | **5,800** | **4,420** | **1,036** | **1,380** | **76.21%** | **0** |

These percentages count draw submissions after the ordinary motion gate. They
are not pixel, object, screen-area or whole-frame coverage.

The exact archive-family distribution is:

| Disposition | Exact pair family or archive alias | Captured draws |
| --- | --- | ---: |
| Linear | Argon DEFAULT | 3,304 |
| Linear | Argon BUMPMAP | 932 |
| Linear | Split DEFAULT / BUMPMAP | 80 / 52 |
| Linear | standard-lighting BUMPMAP | 48 |
| Linear | shared-hull DEFAULT | 4 |
| Refused | `xt_standard_lighting` BUMPMAP | 848 |
| Refused | shared `xt_standard_lighting` / `xt_standard_lighting_damage` DEFAULT | 400 |
| Refused | `xt_standard_lighting2s` BUMPMAP | 80 |
| Refused | shared `xt_standard_lighting2s` / `xt_standard_lighting_damage2s` DEFAULT | 8 |
| Refused | glass DEFAULT | 28 |
| Refused | asteroid BUMPMAP | 16 |

Thus 1,336 of the 1,380 refused draws (96.81%) belong to the `xt_*` archive
aliases. The only logged refusal class is reason 1. In the implementation this
means the bound pair has no material sampler mask: it is outside the exact
installed material contract. The bounded witness is asteroid pair
`167eb2d5629ab9d3` / `d44db87778a43b61`, with `required=00`, `unknown=00` and
`srgb_enabled=00`. No reason-2 missing-combined-object, reason-3 HDR/tonemap, or
reason-4 sampler-state refusal was logged; each reason would emit its own first
witness. Together with zero bind failures, this distinguishes incomplete exact
pair coverage from a live-state or shader-publication failure.

The eligible list is dominated by Argon DEFAULT/BUMPMAP draws, so those native
specular/reflection equations and transfer edits are the focused review area
for the reported gloss change. This is only a draw-family priority: the log has
no draw-to-pixel ownership data and cannot say which pair rendered a particular
station panel.

## Capture inventory and visual limits

Run 23 preserves two complete four-frame bursts: **10752--10755** and
**11607--11610**. Each frame has successful 1280x768 HDR and TAA RGBA16F,
motion RGBA32F, color and present BGRA8, and depth R32F readbacks. All 48 files
exist at their logged sizes with no missing plane, totalling 346,030,080 bytes.
The directory also contains 59 shader binaries saved by the run (175,692
bytes), for 107 non-log files total. Every shader identity in the captured
route rows has a corresponding file.

The first burst is the user's distant selected asteroid while it shimmered; the
second is the closer view after the shimmer disappeared. This repeats the
distance-dependent issue with linear materials enabled and therefore does not
support treating the material toggle as its fix. Run 20 and run 23 use
substantially different scene/view geometry, so pixel-aligned A/B subtraction
would not measure the material change. The user's screenshot comparison remains
useful qualitative evidence for the brightness, color and gloss observations.

## Loading comparison

The same combined loading routes stayed admitted in B. Run 23 handled 4,088 of
4,215 resource-reader calls (97.01%), with all 127 fallbacks being ordinary
non-gzip records; it processed 789,474,274 input bytes and 2,101,072,576 output
bytes in 12.009 s. The DAT pool reused 3,502 of 3,514 opens with 12 real opens,
zero errors and no full-table event. Fast adjacency computed all 10,864 calls
covering 9,967,006 faces in 1.245 s, with zero fallback, fault or native call.

The save stream's 14,461,803 logical gz reads / 45,754,974 bytes were served by
175 real reads, with no direct read or error. Crypto windows total 2,538
acquires, 845 hits and one cold miss; 846 imports include 845 import hits. The
cold setup completed without failed passthrough in this process, and the steady
window has no busy fallback, import passthrough or eviction.

The mechanical Present-gap comparison is:

| Phase | Run 20 A | Run 23 B | B minus A |
| --- | ---: | ---: | ---: |
| Menu load | 7.569 s | 7.279 s | -0.290 s |
| Save load | 20.685 s | 18.864 s | -1.821 s |
| First sector change | 4.955 s | 4.807 s | -0.148 s |
| Second sector change | 5.517 s | 5.551 s | +0.034 s |
| Return/menu work | 6.873 s | 6.809 s | -0.064 s |

Run 23's save interval has 5.311 s in traced imports and 13.553 s outside those
imports. The latter also contains proxy reader work and uninstrumented engine
work; it is not a residual obtained by subtracting every known subsystem.
Run order, cache state and gameplay sequence differ, so these descriptive gaps
do not establish that linear materials changed loading speed. They do show that
the already accepted combined loading route remained healthy during B. The
streamed analyzer outputs are local under `/tmp/x3-run21-loading-profile/`,
`/tmp/x3-run22-loading-profile/` and `/tmp/x3-run23-loading-profile/`; none of
these runs enabled the sampling profiler.

## Selection stutter and frame summaries

For a broad descriptive comparison, removing mechanically labelled load gaps
and deliberate F8 capture spans leaves 335 run-20 and 163 run-23 one-second
`frame_normal` windows between save completion and return-to-menu work:

| Run | Median window maximum | 95th percentile | Largest maximum | Windows at least 0.4 s |
| --- | ---: | ---: | ---: | ---: |
| 20 A | 21.775 ms | 438.310 ms | 861.773 ms | 41 / 335 |
| 23 B | 24.702 ms | 409.454 ms | 684.392 ms | 10 / 163 |

The sessions contain different views, interaction schedules and durations.
These figures characterize the logs; their different rates are not evidence of
an A/B frame-time improvement or regression.

Run 23 has non-null mode-258 target events at 63.230, 65.520 and 66.704 s.
Four consecutive report windows ending 67.179--70.541 s contain 0.408--0.480 s
maximum frames. Later target events at 147.351 and 149.082 s are followed by a
0.432 s maximum in the window ending 150.085 s, and the 187.741 s target event
overlaps a 0.451 s maximum in the window ending 187.905 s. Run 22 independently
contains repeated 0.4--0.47 s maxima around its target-update clusters. A
one-second summary identifies the maximum duration but not the exact frame, so
this is timing correlation rather than per-event causal attribution.

Across the run-23 stutter windows that overlap selection or its next two
seconds, the largest timed renderer operations were 4.212 ms HDR writeback,
0.635 ms TAA, 0.241 ms Present and 0.200 ms route fill. They are far below the
0.408--0.480 s frame maxima. No material-variant creation occurred there, and
the capture bursts began later at 190.095 s and 217.165 s. The current counters
therefore exclude capture work and late transformed-shader creation as these
stalls, but they do not measure the native target-selection, VM, lead-solver or
central-HUD path. Focused timers around that native path are the supported next
diagnostic step.
