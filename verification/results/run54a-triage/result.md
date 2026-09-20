# Run54A crash triage — 2026-09-21

## Outcome

**Observation.** The two reported fault PCs are outside the actually loaded
Run54 candidate DLL, not merely outside its preferred PE range.  No captured
runtime module list, register context, or stack maps either PC to an image.
The evidence cannot establish a media-worker cause.

## Candidate identity and runtime range

| Fact | Evidence |
|---|---|
| Candidate identity | Each session line 3: SHA-256 `124d98898a66413d2400f99c851e8e2574c6a8238b8d584d06db2a6d48d4d6da`, commit `693a0670c8428dccac95da5f1dc384710d22df88`. |
| PE geometry | `/tmp/x3-run54-candidate/build/d3d9.dll`: ImageBase `0x6fb40000`, SizeOfImage `0x3d19000`. |
| Actual loaded base | `0x76880000`, independently derived in every session: retained symbol `x3m_voice_dmo_fallback_enter=0x6fbeb120` vs log `enter=0x7692b120`; `x3m_collide_sat_thunk=0x6fc09400` vs `handler=0x76949400`; `x3m_collide_memo_thunk=0x6fc0a500` vs `handler=0x7694a500`. |
| Actual candidate range | `[0x76880000, 0x7a599000)`.  PCs `0x6eb2413a` and `0x6eb2a0fa` are outside. |

## Crash witnesses and timing

| Run | Launcher / fault witness | Session extent |
|---|---|---|
| 195 | `launcher-stderr.log`: `2026-09-20T19:57:53.413Z`, read `ffffffff`, PC `6eb2413a`, thread `0344`. | `session-20260920-235537-216.log`, 32,152,180 B, 96,846 lines; last QPC record `frame_end` line 96,798. |
| 196 | No `Unhandled page fault` line. | `session-20260920-235826-212.log`, 13,069,708 B, 42,268 lines; final media record at line 42,256 (selector ID2, frame 3792). |
| 197 | `launcher-stderr.log`: `2026-09-20T20:01:41.454Z`, read `00000004`, PC `6eb2a0fa`, thread `02e0`. | `session-20260921-000016-212.log`, 142,102,728 B, 2,390,785 lines; last QPC record `frame_end` line 2,390,767. |

**Observation.** The crash lines give no stack, register context, loaded-module
list, or exception code beyond Wine's read-access rendering.  They do not
identify a proxy hook, the provider, GStreamer, or a worker thread.

## Actual options and media observations

All three session `proxy_options` records enable `X3M_MEDIA_CUE_CACHE=1`,
`X3M_MEDIA_CUE_TRACE=1`, `X3M_OWNERSHIP=1`, `X3M_OBJECT_LIFETIME=1`,
`X3M_OBJECT_TRACE=1`, `X3M_VOICE_DMO_FALLBACK=1`, HDR/TAA/motion output, and
fog strength `0.03`. `X3M_CAPTURE_START=999999` defers **automatic scheduled**
capture; it does not disable manual F8 capture. Run197 contains the 32 manual
capture frames `2633..2664` (the user confirmed this as first-person).

| Run | Measured media state |
|---|---|
| 195 | 48 cue records, zero `media_owned_snapshot`; last cue window is line 94,150, frame 7,799, with zero attempts and zero video blits/unlocks/failures. |
| 196 | 32 cue records, zero snapshot; line 42,243 enters selector ID2 (`id=2`, kind `0x5a`), line 42,256 returns success. |
| 197 | One snapshot at line 29,580: `startup=10 closed=1 claimed=1 installed=1 debt=0`, `consumer_admission=1`, `copy_admission=1`; no worker, submission, retirement, completion, or shutdown event is emitted. Last cue window at line 2,387,862/frame 3,599 has zero attempts and zero video blits/unlocks/failures. |

The snapshot formatter is [capture.cpp](/tmp/x3-media-production-integration/src/proxy/capture.cpp:1471);
it reports root/admission state, not worker lifecycle. The cue gate's logged
scope is [media_cue.cpp](/tmp/x3-media-production-integration/src/proxy/media_cue.cpp:340).

**Additional Run197 observation.** The two lines immediately after the snapshot
are `media_owned_services prepared=1 initialized=1 admission=1 assigned=1
draining=0 quarantine=0 leases=3 failed_mask=0` and an active slot 0
(`presented=479 binding=439844 clock_generation=4 revision=5`). Slot 1 has zero
presented/binding/clock-generation/revision fields, but its default rate numerator
is nonzero. `startup=10` means ready and `closed=1` means first-device seen. `presented`
is a frame sequence and `binding` a binding epoch, not a copy count. Their
nonzero values are set only after written/current, so they establish at least one
successful owned copy before the F8 snapshot. The snapshot precedes arming the
32-frame capture; it does not prove the copy occurred in those frames or in
first-person mode. The user confirms the capture itself was first-person. These
rows do not supply fault-thread lifecycle or fault-time state.

## Corroborating GStreamer observation

Run195 has 32 `GStreamer-CRITICAL` lines (last `19:57:25.378Z`, 28.035 s
before its fault); Run196 has 16 and no logged fault; Run197 has 16 (last
`20:00:38.525Z`, 62.929 s before its fault). They name only
`gst_element_set_state`, `gst_object_unref`, and `gst_element_set_bus`; no
addresses, source paths, or stack are present.

**Inference.** Temporal co-occurrence makes the GStreamer/provider path a
reasonable investigation target, but neither the PC mapping nor the captured
media telemetry supports assigning either crash to it.

## Needed next evidence

Before any source fix, obtain a fault-time module map plus `EIP`, `ESP`, and a
bounded stack for the fault thread. The smallest decisive diagnostic is a
read-fault-safe exception witness that records those fields and an enumerated
module base/size/name *before* normal crash termination; it must also record
media worker creation/exit and per-record submission/retirement IDs. That
would decide whether `0x6eb2*` is a relocated builtin/provider image and whether
the faulting TID is a media worker.
