# Run 26: same-run exposure, bloom and chase HUD

User run 7, completed 2026-09-13 on installed source `75dbbed`, X3 bottle,
CrossOver Preview. The preserved session is
`/tmp/x3-bottleX3-run26/session-20260913-211839-212.log`, 152,354,255 bytes,
SHA-256 `566b1b8dd610e5c9f7489ac3a7f1cedb82d8806de6b0ebb74f03ed3caeb098df`.
The snapshot contains 156 files. No game was launched by the agent.

## User observations and disposition

- Bloom OFF/ON has no obvious visual effect; previously glowing emitters have
  lost their halos. This is a failed visual acceptance, despite successful pass execution.
- The user prefers the brighter Auto screenshots and suggests a lower +1.3–1.5 EV
  boost. The paired station and ship views support more readable surfaces;
  the brightest nebula views warrant restraint. Screenshot overlays, where
  present, are stronger mode evidence than the attachment order.
- Selection stutter still occurs (explicit follow-up confirmation). New native
  solver/HUD timing analysis is in progress; no performance fix is claimed.
- The central chase crosshair/distance is visible in the supplied screenshots.
  Other camera acceptance items and sector-view restoration remain separate.

## Bloom execution and content failure

The pure-device correction takes effect (`0x52` requested, `0x42` effective),
and bloom attaches successfully. All 153 sampled preparations are ready and
all sampled commits succeed with state preserved. This closes the earlier
attachment failure, not the visual-quality requirement.

The modern extractor ignores alpha and thresholds exposed-linear luminance.
The native DEFAULT compositor instead has a distinct alpha-authored colored
Glow contribution, in addition to its highlight term. The current replacement
therefore removes native glow without retaining its input-selection rule.
In captured frame 36969, 7,163 of the 8,858 pixels with alpha above 0.5 lie below
the modern soft-knee floor (linear luminance 0.5), producing no extracted bloom.
The marker survives TAA. Corrected native shader/constant findings and the
replacement policy are under investigation; increasing global brightness alone
would not repair that omission.

The `present_*.bgra8` readbacks occur before the original compositor and final
bloom commit. They cannot establish final bloom OFF/ON pixel parity. Supplied
screenshots are the current final-image evidence; no final-composite readback
was present in this build.

## Exposure comparison and next artistic candidate

Run 26 starts at fixed EV 0 and records 127 accepted exposure-toggle requests
(64 Auto, 63 fixed). Across all 376 active Auto meter reports, both fresh and
held targets remain exactly +2 EV. The unconstrained key requests
+3.00045…+5.63413 EV and the highlight limit permits +4.34268…+5.81142 EV.
Applied Auto exposure ranges from +0.14854 to +2 during resets/convergence;
222 reports are exactly +2. This run demonstrates a largely constant boost
after settling, rather than meaningful scene-dependent target variation.
Lowering only the Auto ceiling to +1.5 would still clamp every sampled target.

All 16 F8 frames are fixed EV 0, with no HDR fallback/unwind. Bursts
10955–10958 and 36773–36776 have bloom requested off; 11079–11082 and
36969–36972 have it requested on. These are bloom comparisons, not captured
Auto/fixed exposure pairs; the user's separate Auto screenshots require their
own context. The selected post-TAA files are
`/tmp/x3-bottleX3-run26/taa_1_{10955,11079,36773,36969}.rgba16f`.

Offline application of the existing AgX reference to the first post-TAA frame
of each burst gives the following median display luminance for covered,
lit geometry (valid depth and decoded luminance at least 1/512):

| Frame | Fixed 0 | Fixed +1.3 | Fixed +1.5 | Fixed +2 |
| --- | ---: | ---: | ---: | ---: |
| 10955 | 0.2251 | 0.3611 | 0.3844 | 0.4446 |
| 11079 | 0.2123 | 0.3450 | 0.3679 | 0.4274 |
| 36773 | 0.1215 | 0.2213 | 0.2400 | 0.2901 |
| 36969 | 0.1426 | 0.2516 | 0.2715 | 0.3245 |

+1.5 EV retains a substantial geometry lift over zero while using 29.29% less
scene-linear gain than +2. No selected image reaches an RGB channel of 0.99
at any evaluated setting, including +2. This does **not** establish that +2
looks balanced: excessive brightness may concern nebula mood or tonal
compression without hard clipping. Depth coverage is not a semantic hull or
nebula mask; the sentinel also includes uncovered scene writers. These
transforms precede bloom and late overlays and do not rerender TAA, so they
cannot establish final bloom/glow appearance or every sector's visual balance.

The user's brighter preference and screenshots support **fixed +1.5 EV as
the next artistic baseline candidate**, with fixed 0 retained as the
comparison reference and Auto optional. This is a tone-calibration choice,
not acceptance of the current meter's adaptation policy. Production remains
unchanged until the bloom/glow regression's root cause is resolved, since
missing glow can affect that judgment.

Bounded local evidence is retained in
`/tmp/x3-run26-exposure-log.json`, `/tmp/x3-run26-exposure-study.json` and
`/tmp/x3-run26-exposure-candidates.json`. The study reuses the reviewed
post-TAA decode contract and AgX oracle; it performs no game launch, Wine run,
build, install or repository edit.
