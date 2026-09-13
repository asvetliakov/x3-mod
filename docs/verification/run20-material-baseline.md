# Run 20 A: fixed-exposure material baseline

Run 20 A is the linear-materials-off side of the fixed-EV comparison. The
requested HDR/TAA route, sharpen 0.75, mip bias -0.5 and the combined loading
features were active. Three four-frame capture bursts are complete. The user
reported much faster loading, no overexposure, a visible predictive aiming
hint, and accepted the 13-degree / 0.9-distance / 0.28/0.38-second camera
settings. Keep those camera values.

The user also reported a stutter on target selection and another one one to two
seconds later, plus distant star-lit asteroid shimmer that disappears closer.
The selection interval overlaps repeated 0.43--0.46-second frame maxima, but no
timed renderer or loading operation accounts for them. The log cannot assign a
cause. The asteroid observation was made with TAA, sharpen and mip bias active;
whether linear materials change it requires the matched B run.

## Provenance and actual configuration

The helper snapshot
`/tmp/x3-bottleX3-run20/session-20260913-185854-216.log` is 151,396,504
bytes and 2,585,294 lines, SHA-256
`59b3294811336b924f04f10f36e3800aa2ff86559dc047fe22e3a9e1798a32cc`.
Its size, modification time and hash match the completed live log at
`/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures/session-20260913-185854-216.log`, and `cmp` reports exact
identity. The installed checkpoint was `10e447b`; `d3d9.dll` is 12,884,608
bytes with SHA-256
`d8f67c33e0139606f4624600ddc0b9ef95faa1abf14914329016e51f01dd6ee3`.

The runtime reports renderer 0.4, schema 2 and a 32-bit process. The actual A
configuration was:

| Feature | Evidence |
| --- | --- |
| Linear materials | off; there is no `linear_material_mode`, material variant or material-refusal row. The source emits its mode row whenever the option is requested. |
| HDR / exposure | HDR target and AgX tonemap active; gamma-2.2 decode; manual EV exactly 0; meter off; no fallback. |
| Bloom | off; no bloom mode/device/frame row. `bloom_copy_seen` in the general frame row observes the game's copy boundary and does not enable the opt-in compositor. |
| TAA | motion output, scene hook, jitter, depth and TAA active; sharpen 0.75; mip bias -0.5; per-draw route. |
| Camera | chase active at pitch 13 degrees, distance 0.9 and response 0.28/0.38 seconds; lead and transition diagnostic hooks active. |
| Loading | crypto cache, 256 KiB gz buffer, fast resource reader, DAT handles and fast adjacency active; mesh cache and engine loading probes off. |

There are no sampling-profiler blocks. The local streamed loading analysis is
`/tmp/x3-run20-loading-profile/loading-profile.{json,md}`.

## Combined loading route

All requested loading paths stayed admitted:

| Path | Completed work | Fault/admission result |
| --- | --- | --- |
| Resource reader | 4,238 calls; 4,101 handled (96.77%); 790,291,682 B input and 2,102,886,261 B output; 11.861 s | 137 fallbacks, all ordinary non-gzip records; every other reason zero |
| DAT handles | 3,537 opens; 3,525 reused (99.66%); 12 real opens | 0 errors and 0 full-table events; 12 held entries |
| Mesh adjacency | 11,371 calls/computations; 9,997,563 faces; 1.237 s | 0 fallbacks, faults or native calls |
| Crypto cache | 2,538 logical acquires; 845 hits / 1 cold miss; 846 imports / 845 import hits | expected one cold failed passthrough; no busy fallback, import passthrough or eviction; one provider/key retained |
| Gz read-ahead | save stream served 14,461,803 logical calls / 45,754,974 B through 175 real reads | 0 direct reads and 0 error; close returned success |

The cache suppressed 846 qualified releases and key destroys and emulated 1,691
container deletes. The one cold probe remained `NTE_BAD_KEYSET`, matching the
accepted run-17 sequence. During the save gap only two physical
`CryptAcquireContextA` calls (14.441 ms) and one `CryptImportKey` call
(0.859 ms) crossed the imports. This run did not enable the engine signature
probe or the remaining hash-operation rows, so preservation of every RSA
verification relies on run 17's direct gameplay acceptance and the unchanged
reviewed implementation.

The gz buffer retained all 14,461,803 logical save reads but collapsed the
traced zlib calls to 175. Their measured wrapper tail was 20 microseconds,
versus 1.147 seconds for the unbuffered telemetry path in run 19. This is the
intended removal of observation overhead; existing fixture evidence still says
the buffer is not a plain-game loading optimization.

The mechanical presentation-gap markers are:

| Label | Interval after initialization | Gap | Hooked exclusive | Outside import hooks |
| --- | ---: | ---: | ---: | ---: |
| Menu load | 4.984--13.172 s | 7.569 s | 2.021 s | 5.548 s |
| Save load | 22.774--43.781 s | **20.685 s** | 8.458 s | 12.227 s |
| Sector change | 261.389--267.118 s | 4.955 s | 1.795 s | 3.159 s |
| Sector change | 310.429--316.523 s | 5.517 s | 2.625 s | 2.893 s |
| Return/menu work | 412.656--419.891 s | 6.873 s | 1.437 s | 5.436 s |

The save gap is descriptively 20.886 seconds (50.2%) shorter than run 19's
41.571-second gap and 6.889 seconds shorter than run 17's 27.574-second gap.
This agrees with the user's “much faster” observation and the direct
crypto/gz admission counters. It is still not a controlled A/B: build,
rendering load, run order and cache state differ. The sector gaps remain in the
same approximate range as run 19 and do not establish a sector-load gain.

## Preserved A captures

The three bursts cover frames **3788--3791**, **4818--4821** and
**5042--5045**. Every frame has all six successful 1280x768 readbacks:

| Plane | Files | Bytes each |
| --- | ---: | ---: |
| HDR FP16 | 12 | 7,864,320 |
| TAA FP16 | 12 | 7,864,320 |
| Motion FP32 | 12 | 15,728,640 |
| Pre-TAA color BGRA8 | 12 | 3,932,160 |
| Presented color BGRA8 | 12 | 3,932,160 |
| Depth R32F | 12 | 3,932,160 |

All 72 referenced readback files exist under `/tmp/x3-bottleX3-run20/` at
their logged sizes, with no missing or extra readback. Their total is
519,045,120 bytes. All 59 unique referenced shader files (22 vertex, 37 pixel)
also exist at their logged sizes with no extra shader file.

Each captured frame reports HDR redirected and AgX-tonemapped at manual EV 0,
TAA resolved with history, sharpen applied, mip bias -0.5, and zero route apply,
restore or HDR fallback failures. The bursts are not semantically labelled in
the log, so no settled/turning/moving assignment is inferred. They generated
7,259 per-draw `capture_event` rows and six readbacks per frame; their deliberate
capture overhead is not gameplay frame-time evidence.

## Selection-stutter evidence and limits

Only 98 `frame_end` rows were emitted across 25,582 frames. Their `dt_ms` is the
time since the previous emitted row at this sparse cadence, not the duration of
one frame. The usable witness is the one-second `frame_normal` summary, which
reports an exact maximum duration but not the frame or operation that contained
it.

The first dense chase-target update cluster (events 83--97) spans 177.017 to
249.146 seconds after initialization. Thirty of its 70 report windows contain a
0.40-second-or-longer maximum frame (peak 0.456 s); the preceding 17 gameplay
windows contain none and peak at 0.053 s. Four later target changes at 343.590,
368.294, 399.538 and 408.808 seconds likewise overlap immediate or next-window
maxima of 0.444--0.455 s. This timing supports the user's immediate/delayed
selection-stutter report. Overlapping target updates and one-second aggregation
prevent pairing every reported hitch to a unique selection.

In the late selection windows, timed maxima were at most 1.264 ms for Present,
0.580 ms for TAA, 0.292 ms for HDR writeback, 0.156 ms for route fill and
0.015 ms for log flush. No shader, meter or readback work was reported there.
Loading-import work within two seconds of each selection was generally below a
millisecond; one event additionally overlapped about 29 ms of mesh work, still
far below the 0.455-second maximum. These counters do not expose the remaining
game/VM/UI work, so absence of a large timed renderer row is not proof of its
cause.

First-time transformed-shader creation can be excluded for these later stalls.
The log has 52 successful `motion_output_variant` creations for 30 unique
kind/hash keys. Seven hashes were recreated for multiple COM objects (22 extra
object-level creations), but all 52 creations completed by 67.517 seconds: 44
during load and eight in early gameplay. None occurred in either selection
interval. The rows contain no compile duration, so no creation-cost claim is
made.

The final capture event ended at 112.330 seconds, well before the first dense
selection cluster. Thus the preserved-capture work does not overlap the
reported selection stutters. A finer cause needs a targeted timer or sampler
around the native selection/lead/UI path; this log only establishes the frame
stall and rules out capture work and late transformed-shader creation.
