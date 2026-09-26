# Run 27: authored glow, exposure and selection stalls

Evidence: `/tmp/x3-bottleX3-run27/session-20260914-000122-216.log` (245,885,736 bytes, 4,507,980 lines) and the 154 files copied beside it. The streaming queries are `/tmp/analyze_run27.py` and `/tmp/analyze_run27_draws.py`; compact results are `/tmp/x3-run27-analysis.json` and `/tmp/x3-run27-draw-analysis.json`. No image-appearance judgment is made from F8 because those captures precede final bloom.

## User verdict and next change

User run 8 used installed source `541e380`. Bloom is visibly working but too
subtle; +1.5 EV looks good. Selection stutter remains. Distant asteroids and,
less visibly, stations shimmer on lit and dark sides, with the effect disappearing
closer. Some objects become brighter after moving; the user suspects material
fallback changes. Those last two observations are not yet assigned to a proven cause.

The next reviewed source change raises authored glow gain from 0.10 to **0.35**,
keeping highlights at 0.05, the same pyramid, and the accepted +1.5-EV comparison
profile. See [calibration and limits](../architecture/bloom-authored-glow.md).
The existing handoff/reference checks pass 23 tests, including 45 retained-callback
assertions in release and ASan/UBSan modes. Independent review finds no issues.
This is a constant change with no additional shader work, passes or resources;
the installed DLL remains unchanged until the next combined candidate.
No new gameplay run is requested yet.

## Selection phase evidence

The streaming phase query is `/tmp/analyze_run27_selection_phases.py`; its compact
witness is `/tmp/x3-run27-selection-phases.json`. Input SHA-256 is
`3df61e84408b0297c8c7ec102aa8b6466b6f4b665e6f36adedc2ca26b49f9585`.
All 83 recorded frames over 50 ms and all 21 native calls over 10 ms are retained.
Each frame has 15 contiguous, exact segments; overflow/order/clock/read/CPU errors
are zero. Global invalidated/unmatched/foreign counters are 8/12/10 elsewhere;
both report windows around the strongest witness have none.

After excluding the initial state of each cockpit generation, 14 valid target
changes remain. Twelve occur in retained slow frames: eleven are dominated by
phase 6 and one by cockpit/registry phase 8. Phase 6 is a **broad pre-simulation
region**, including registry/sector/deletion work, input/control and synchronous
script/save work; it must not be relabeled as solely input handling. Eight of its
selection-associated spans take 415.195–440.969 ms, with coarse thread CPU of
410–430 ms. The target snapshot follows their end by 0.121–0.205 ms; this is a
same-frame association, not measurement of the exact target store.

The strongest delayed witness uses cockpit `6fefc0f0`, target `3dee22f0`, event 62:

- Selection is first observed at QPC `10595573753837` (10 MHz).
- The mode-3 delayed publisher bracket starts **992.919 ms later** and takes
  **458.847 ms**, returning result 1 with the same cockpit/target.
- Noncapture frame 20235 takes **485.012 ms**. Its enclosing cockpit/registry
  phase takes 459.492 ms, with 450 ms coarse thread CPU.
- The nested publisher accounts for 99.86% of that phase and 94.61% of the frame.
  These are inclusive spans, not additive costs. The inner call has no CPU stamp.

The publisher's synchronous `NotifyTargetLock`/VM chain is now the prime static
investigation target. This bracket alone cannot attribute all its time to one
callback or explain every subjective pause. Frame 20246 then has a 477.931 ms
pending-VM span, starting 290.416 ms after the publisher finishes; that association
is temporal only and does not establish a shared callback identity.

Of 67 noncapture slow frames, the dominant spans are pending VM in 37 frames
(total 11.293 s), broad phase 6 in 14 (3.962 s), cockpit/registry in two (0.902 s),
renderer in ten (0.586 s), and presentation/post-render in four. The sixteen
capture-adjacent slow frames are separately renderer-dominant and excluded from
selection attribution. No user timestamps identify which recorded event corresponds
to each perceived pause.

The 351,785 CPU-query brackets average 14.329 microseconds; nominally fifteen
queries per loop gives about 0.215 ms of bracket wall time. Handler/bridge timings
overlap and must not be added. One unrelated scheduling outlier is 36.818 ms,
but in the key witness window the worst query is 48.7 microseconds and worst
handler 54.4 microseconds: these do not explain the 459 ms stall. The diagnostic
is not a game-FPS measurement.

## Runtime path

The header records HDR/TAA active, `linear_materials=1`, per-draw motion/depth, AgX gamma-2.2 decode, Auto exposure with `ev_max=1.50`, and all 23 game-phase sites active. Device policy saw requested flags `0x52`, effective `0x42`; bloom later attached successfully (`result=0`, `reason=ok`, 13 retained references). The single earlier `scene_handoff` bloom refusal occurred before attachment.

Across 408 sampled frame reports, 395 had an active HDR redirect/tonemap and none used the HDR fallback. TAA resolved 394 samples and used history in 391. Motion apply/restore failures, material bind failures, and the targeted renderer error scan are all zero. The inactive reports are initialization/teardown-like frames; the last sampled frame 23460 has no redirect/camera latch.

## Exposure and bloom

Of 395 active HDR samples, 331 are Auto and 64 fixed/manual. Auto target, fresh, adapted and applied EV never exceed +1.5. The target is exactly +1.5 in 329/331 active Auto samples; the other two are startup zero. Run 27 therefore verifies that the new cap is effective, but again behaves almost as a constant +1.5 boost in the sampled gameplay rather than showing useful adaptation. This is consistent with the user's report that +1.5 looks good; it does not decide the general default.

There are 94 accepted control requests: 37 F9 and 57 F10. The paired frame acknowledgements report bloom ready/used in both contribution modes, with 62 ON and 32 OFF toggle-state observations. OFF always records `bloom_off_filter_runs=1`. The 77 sampled bloom prepares and 77 commits all report ready/committed, operation and restore `0`, and preserved state; state-timeline association places 62 samples in ON and 15 in OFF. The last cumulative admission snapshot has 23,400 calls, 734 scene-handoff misses, and zero pass-unavailable, boundary, reset, owner, thread, nested or post-qualification failures.

Installed source `541e380` supplies authored glow gain 0.10 and highlight gain 0.05 (`src/proxy/capture.cpp:612-613`). The session log does not print the uploaded gain constants, so it proves the installed pass executed but does not independently read back those two values. User feedback is the appearance evidence: bloom is visible but too subtle and should move toward native strength.

All captured HDR and resolved-TAA alpha planes are byte-identical. Per 983,040-pixel frame, 11,679-13,234 pixels have alpha above zero and 1,217-1,513 exceed 0.5; every sampled alpha is finite and within [0,1]. Thus the corrected pass had a nonempty retained-alpha source available. These pre-bloom files still cannot show the resulting halo.

## F8 inventory and mode association

The snapshot contains 16 complete frame sets at 1280x768: 16 each of HDR RGBA16F, resolved TAA RGBA16F, motion RGBA32F, depth R32F, pre-TAA color BGRA8 and presented BGRA8, plus 36 PS dumps, 21 VS dumps and the log. All 16 HDR readbacks report success and the expected 7,864,320 bytes; all 12,408 capture events return zero.

| Frames | Exposure actually applied | Bloom contribution |
| --- | --- | --- |
| 20324-20327 | Auto, EV/target +1.5, `k=2.82843` | OFF |
| 20470-20473 | fixed/manual EV 0, `k=1` | OFF |
| 20744-20747 | Auto, EV +1.49999 to +1.5, target +1.5 | OFF |
| 21001-21004 | fixed/manual EV 0, `k=1` | OFF |

The first Auto/fixed pair has 0.934 exact-pair multiset overlap; the second has 0.793. They are related views, not pixel-aligned material/exposure twins. All four bursts were taken while bloom contribution was OFF, and F8 is pre-bloom in any case.

## Material coverage and fallback

The 408 material reports total 121,088 converted draws, including 48,816 bump draws, 1,700 refusals and zero bind failures. Only refusal reason 1 appears: unsupported exact pair `c30104cb0efb6675/a66fb1981ba755b2`, with `required=00`, `unknown=00`, `srgb_enabled=00`. No reason-2 missing-variant, reason-3 HDR-mode, or reason-4 sampler-sRGB fallback was logged.

The captured aggregate refusals are 5/5/5/5, 4/4/4/4, 2/2/2/2 and 5/5/5/5 across the four bursts. Every detailed refused draw that was captured uses that same unsupported pair, Z writes on and alpha blending off. It occurs on several nodes. Node `14b3b350`, model `00004f9b`, changes from LOD 2 to LOD 1 across the capture sequence but retains the same shader pair and native fallback. The records therefore do not show position or LOD switching a supported object between native and linear routes. They do show that a small fixed unsupported opaque population remains native among the converted scene; object names and screen ownership are unavailable, so it cannot yet be identified as the user's dark/bright object.

Creation logs contain 28 unique transformed original program identities (6 VS, 22 PS) and four unique XT DEFAULT repair outputs. Twelve source identities belong to the newly installed asteroid/XT families. No Boron or Paranid source identity appears in this session. Detailed F8 draws prove ten converted exact pairs; five are newly added: asteroid BUMP, two XT BUMP and two XT DEFAULT pairs. Detailed capture begins at the F8 boundary and misses one of frame 20744's 895 draws, so frame summaries own aggregate totals.

The asteroid pair is `167eb2d5629ab9d3/d44db87778a43b61` throughout:

- Far node `1af31630`, model `00004fee`, LOD 0 appears in all 16 captures with Z writes off, alpha blending `SRCALPHA/INVSRCALPHA`, additive blend operation, and no separate-alpha mode. Its ordinary opaque motion/depth gate rejects it, so material conversion is never attempted and native color remains.
- A distinct near node `1af313b0`, model `00004fef`, LOD 0 appears in frames 20744-20747 with Z writes on, alpha blending off, valid motion/depth and admitted material conversion.

Run 27 far VS constants explicitly enable `b0` with `c39.x=1` and
`c41.xy=(1.0526316166,2.10526366e-7)`; the near draw disables `b0`.
The [existing native fog study](../reverse-engineering/asteroid-fog-temporal.md)
identifies the distance-triggered state producer and the prior run23 same-node
far/near candidate. This avoids assuming a geometry LOD switch.

This is firm admission evidence for native transparent/background versus converted opaque asteroid draws. It is not a demonstrated same-object distance transition: node handles and models differ, selected-target identity is absent, both report LOD 0, and the capture state does not include `FOGENABLE`. It therefore cannot by itself assign the user's distance-dependent shimmer or darkening to the route change.

## Exit limit

The log ends on a complete newline after frame-23463 cursor telemetry. It has no crash/fatal/exception prefix and the game is no longer running, but it also has no device/factory release, Reset, or final retirement summary. A normal user quit is plausible; clean teardown and release behavior are not proved by this snapshot, and absence of those lines is not evidence of a crash.
