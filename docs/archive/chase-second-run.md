# Chase camera: corrected second gameplay run

2026-09-13. The user ran the corrected chase build with
`--direct --camera chase --telemetry`. They report that the camera no longer
trembles. This accepts the native-anchor correction for this gameplay run. The
user now wants the camera elevated above the ship and looking down, with softer
follow motion. They also report that, in the chase view, mouse position does not
change the firing direction and shots go straight; right-mouse firing works in
first person. Those are new acceptance requirements, not changes present in
this build.

## Provenance

| Item | Value |
| --- | --- |
| Session | `session-20260913-063605-212` |
| Source | `/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures/session-20260913-063605-212.log` |
| Immutable copy | `/tmp/x3-chase-second-run-eNOsg6/session-20260913-063605-212.log` |
| Size / lines | 10,758,346 bytes / 212,221 lines |
| Stable mtime | `2026-09-13T06:40:13+0400`; `1789267213896602775` ns before and after copying |
| Log SHA-256 | `0c24ac50a4770948cd26cd65039000dbb3544e225b673a7ad7435b8e7f001f58` |
| Installed source checkpoint | `0c642dfc44998be1382c72bdfdb1df40db7bb8e7` |
| Installed DLL | 11,606,225 bytes; SHA-256 `16d016d2f12847dbc47c88c3a241a628d7390354ffb02465b63723b14187de01` |
| Guard after the run | `game_guard.game_running() == []` |

The source log size, nanosecond mtime and hash still matched the copy after the
analysis. The installed DLL and app-local manifest match the reviewed
[installation record](../../verification/results/chase-feedback-install.json).
The log has no last-device, shutdown or destroy row, so all session totals below
are the last flushed values, not a teardown summary.

The X3 game capture directory contained 37 `session-*.log` files. The corrected
run above was the newest by mtime. The next newest was the 10,171,479-byte
first-flight session `session-20260913-054829-212.log`, followed by the
10,486,233-byte loading session `session-20260913-024655-216.log`. This ordering
and the clear game guard identify the copied log as the completed new flight.

The log names 54 dumped shader binaries totaling 171,652 bytes. They were
copied to `/tmp/x3-chase-second-run-eNOsg6/shaders`; every copy is byte-identical
to its source. The SHA-256 of sorted `<file SHA-256> <basename>` lines is
`c9123b4e2b701b11387f0c4149495093879fc07245c29535760f7235fb422ed0`.
They preserve session provenance but do not provide camera-position evidence.
The one `capture=1` frame was frame 120, before the first chase write, and the
log contains no color, depth or readback row for it.

The large log was not read into the report. Small streaming queries retained
only install, chase-camera, timing, capture and terminal-marker fields.

## Configuration and activation

The install row is `requested=1 installed=1 status=active` at `0x00420e06`.
It records the corrected defaults: `rot_tau=0.150`, `pos_tau=0.200`,
`offset_y=0.450`, `distance_scale=1.000`, rotation/position clamps of 8 degrees
and 0.10, combat tightening off, scene correction off, and handler timing on.
The installed manifest and DLL hash independently match the qualified build.

Motion output, TAA, jitter, scene hooks and HDR were all off. Consequently the
run has no temporal `camera_cut` rows and cannot assess TAA cuts or ghosting.
The 0.45 offset changes the view orientation to place the native anchor lower
on screen. It does not elevate the native boom: distance scale remains 1.0 and
there is no height offset in this build. The requested elevated, downward view
therefore needs a separate camera-position/orientation change.

The first successful write occurred at handler frame 592 and was reported at
render frame 1200. It used external mode 258, connect mode 0, flags 0, matching
reference/view-object pointers, the base native branch and a 17,403.2-unit
negative-Z boom. `half_vfov_tan=0.7500` and `fov298=0x4000` match the preceding
run.

## Applied frames and the native anchor

The last chase report, at render frame 18,900, contains:

| Counter | Corrected run | First run |
| --- | ---: | ---: |
| Active-cockpit frames | 18,154 | 6,650 |
| Applied | 12,615 | 5,871 |
| Refused/pass-through | 5,539 | 779 |
| Inactive-cockpit visits | 75 | 0 |
| Snaps / coalesced | 2 / 0 | 2 / 0 |
| Combined clamp events | 6,211 | 2,724 |
| Write refusals | 0 | 0 |

The comparison values come from the retained
[first-run report](../verification/chase-first-run.md). The longer corrected run spent much
more sampled time in internal mode, so its higher refusal count is a view-use
difference rather than evidence of a new refusal fault.

All 12,615 applied samples in the new `chase_camera_window` rows used the
native base-position branch. There were zero render-branch samples, and every
applied sample also supplied a valid anchor-domain diagnostic. The distance
between base and render anchors ranged from 0.0 to 14,324.4 engine units. The
largest separation occurred in the frame-12,300 window; that window ended with
only 0.001 degrees of rotation lag and 3.2 units of position lag. The large
domain difference is therefore an input-domain difference, not spring lag.

The first-run DLL always used the render anchor and did not record the two
domains, so the old flight has no numeric separation to compare. This run
shows that the domains can diverge substantially while the game is in the
accepted external chase path. Together with the user's report that trembling
is gone, it supports the corrected native-anchor selection as the relevant
fix. It does not identify which individual old-frame anchor changes produced
the visible trembling.

During applied report windows, the latest matching-native basis deviations
ranged from 0.2402 to 0.4116 degrees. Latest render-basis diagnostics ranged
from 0.2402 to 0.7330 degrees. No reported window's latest diagnostic was
missing. These are the latest sample in each reporting window rather than
per-frame extrema.

The old flight's sampled `basis_dev_deg` ranged from 0.280 to 0.789 degrees.
That field then compared against the unconditional render-ready basis; the
corrected field compares against the basis matching the selected native
position branch and reports the render-ready diagnostic separately. The old
and new ranges therefore are not a direct before/after accuracy measure. Their
similar scale provides no evidence of a new basis discontinuity.

The two cumulative snaps, as in the first run, were reason 1 entry/re-entry
snaps; there were no coalesced snaps. Sampled external rows were mode 258 with
verdict 0, while sampled pass-through rows were internal mode 1 with verdict 1.
Every report had connect mode 0 and `flags_1a0=0`; no sampled row showed an
invalid-input, numeric, read, verbatim-basis or write failure. The 300-frame
report cadence cannot exclude a transient unsampled verdict.

## Lag and clamps

The corrected telemetry separates the 6,211 clamp events into 2,192 rotation
and 4,019 position events. The window extrema were 0.0 to 8.0 degrees of
rotation lag and 0.0 to 1,740.4 units of position lag. Both configured limits
were reached repeatedly.

There were 49.2 combined clamp events per 100 applied samples in this run,
versus 46.4 in the first run. The new split is 17.4 rotation and 31.9 position
events per 100 applied samples. These ratios are event densities, not the
percentage of frames clamped: rotation and position can both increment on one
frame, and the two flights are not controlled-identical trajectories. The
user's accepted smoothness despite comparable clamp density is evidence that
clamping alone did not explain the original trembling. The frequent limiting
does remain relevant when choosing the requested softer follow response; this
run does not determine new time constants or clamp values by itself.

## Mouse firing fields

The first-applied row records `aim_gun=0x0`, not `0xffffffff`. Interpreted as
the signed cockpit field used by the native fire-control gate, this is gun 0
and satisfies `aim_gun >= 0`; it is not the disabled `-1` state. That row also
has `flags_1a0=0`, connect mode 0, `tracking=0`, a null tracked object and
`locked=0`. The cockpit camera field `flags_1a0` is **not** the fire-call
argument whose bits `0x2` and `0x20` gate cursor aiming; those fire flags were
not captured. Later sampled rows changed
to tracking mode 3 but remained unlocked. All 65 periodic chase reports kept
flags 0 and connect mode 0.

The log has no mouse-ray, fire-vector or input-event telemetry, and `aim_gun`
is emitted only on the first-applied row. It therefore rules out a disabled
gun value at that sample and supplies no logged flag-gate explanation, but it
cannot locate the firing-direction defect. The user's first-person/chase-view
difference requires the separate fire-control and external-view code study.

## Handler timing and performance scope

The 64 nonempty report windows contain 18,229 handler calls, exactly the 18,154
active plus 75 inactive visits. They total 385,070.7 microseconds, for a
call-weighted mean of 21.124 microseconds. Report-window means ranged from
13.545 to 28.490 microseconds, with a median of 22.987 microseconds. The largest
single call was 1,252.3 microseconds in the frame-900 window, before the first
successful chase write and while the sampled view was internal.

The 41 windows consisting entirely of applied chase samples contain 12,300
calls, with a weighted mean of 23.841 microseconds and a maximum single call of
160.2 microseconds. Four mixed windows contain the other 315 applied samples;
the log cannot assign each mixed-window maximum to an applied or refused call.

These values have the explicit log scope `handler_cpu`, `window=report`, and
exclude the assembly stub and CPU save/restore. They do not include renderer
work, present time or GPU time. The general `frame_normal` telemetry covers the
whole game and includes loading, menus, pauses and long stalls; it cannot be
attributed to the chase handler. This run therefore measures the C++ handler,
not total camera overhead or game FPS.

No Wine command, game launch, build, install or native-Windows run was performed
for this analysis. Native Windows behavior remains untested.
