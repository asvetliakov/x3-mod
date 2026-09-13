# Chase camera: elevated third gameplay run and mouse-fire trace

2026-09-13. The user ran the installed chase build with telemetry and performed
right-mouse fire with the cursor at left, centre and right in first person,
then repeated the sequence in external chase view. The trace isolates the
view-dependent behavior: first-person fire admits the cursor ray, while
external mode 258 receives the same fire flags but the game's cursor writer
sets cursor-active to zero. External fire consequently skips the cursor-ray
and cone path and reaches the final barrel path close to the ship's forward
axis.

This run used the current 20-degree pitch and 0.6 distance scale. The later
request for 13 degrees and 0.85 was not present in this run and is not assessed
here.

## Provenance

| Item | Value |
| --- | --- |
| Session | `session-20260913-074758-212` |
| Source | `/Users/asvetl/Library/Application Support/CrossOver/Bottles/X3/drive_c/X3/x3-modern-captures/session-20260913-074758-212.log` |
| Immutable copy | `/tmp/x3-chase-third-run-2SRYIg/session-20260913-074758-212.log` |
| Size / lines | 10,875,768 bytes / 212,588 lines |
| Stable mtime | `2026-09-13T07:51:13.969860+04:00`; `1789271473969859837` ns before and after copying |
| Log SHA-256 | `f483566c2a655ea4f4154ff4272b0a9e14e2236a128c4fbddde8ba1bbef10e80` |
| Installed source checkpoint | `dac2994f08de45186693ad02440fabb3b99beabd` |
| Installed DLL | 11,709,039 bytes; SHA-256 `2981bf032be8c7e91013e1de7f355d83fb49f4b2ba778b2b5917787f48a9297c` |
| App-local manifest | 210 bytes; SHA-256 `045ed4243a1fdcfe6339f2bd696ffcc440262750134220d91888629968733706`; names the same DLL hash |
| Guard after the run | `game_guard.game_running() == []` |

The current on-disk installation was independently checked after the run: its
`d3d9.dll` has the exact qualified candidate hash above, and its
`x3-modern-install.json` is 210 bytes and names that same hash. The repository
status record binds that install to checkpoint `dac2994`. The large session log
was first copied with equal source/copy byte size and nanosecond mtime, then
read only through bounded streaming parsers. No shader or image artifacts were
needed for this input/camera diagnosis.

The capture directory contained 38 session logs and this was newest by mtime.
There is no chase teardown, proxy shutdown or capture-complete row. The final
chase report is frame 12,300 and the final telemetry row is frame 12,384, so all
totals are last-flushed values rather than teardown totals. The empty game
guard and stable source file establish that the user process had ended before
analysis.

## Installed configuration and camera path

Initialization records `chase_camera requested=1 installed=1 status=active`
at the verified `0x00420e06` site. The four read-only aim sites also record
`installed=1 status=active sites=4`. Camera defaults were rotation and position
time constants 0.220 and 0.300 seconds, offset 0.450, downward pitch 20 degrees,
distance scale 0.600, rotation and position lag clamps 8 degrees and 0.100,
combat tightening off, maximum step 0.100 seconds, and three-frame snap
coalescing. TAA, jitter, scene hooks and HDR were disabled.

The first applied camera sample was reported at frame 1,200, handler frame 542.
It was external mode 258, connect 0, cockpit flags 0, base native anchor,
matching reference/view-object addresses and `aim_gun=0`. Its native boom was
approximately 17,403 units; the 0.6 scale is reflected in the later reported
10,442-unit camera distance.

The last camera report counts 11,761 active-cockpit visits: 9,140 external
applications and 2,621 pass-through visits. The fire samples identify mode 1
as internal with `pose_applied=0` and mode 258 as external with
`pose_applied=1`. There were no inactive-cockpit visits and no write refusals.
All 9,140 applied samples used the base native anchor; none used the render
anchor.

The camera recorded two reason-1 entry/re-entry snaps, no coalesced snaps,
2,123 rotation clamps and 2,518 position clamps. Window extrema were 8 degrees
rotation lag, 1,044.3 position units, 7,757.4 units between native base/render
position domains, 0.4099 degrees native-basis deviation and 0.8053 degrees
render-basis deviation. These are diagnostic extrema over this trajectory,
not visual quality or frame-rate measurements.

## Ordered fire windows

The report cadence groups events into windows, so the listed report frame is
not the exact instant of a button press. The user's stated test order and the
ordered coordinate clusters support the following correlation without an
exact-time claim:

| View and cursor region | Report frames | Logged cursor x,y in retained events | Player entries | Gate / rays | Retained cone result |
| --- | --- | --- | ---: | --- | --- |
| First person, left | 5,700 / 6,000 | `270,459` | 67 | 67 gate 0 / 67 rays | 16/16 sampled rays clamped |
| First person, centre | 6,300 / 6,600 | `642,419` or `642,422` | 96 | 96 gate 0 / 96 rays | 0/16 sampled rays clamped |
| First person, right | 6,900 / 7,200 | `1136,438` | 66 | 66 gate 0 / 66 rays | 14/14 sampled rays clamped |
| External, left | 9,000 / 9,300 | `216,349` or `203,349` | 76 | 76 gate 3 / 0 rays | Cone path not reached |
| External, centre | 9,600–10,500 | approximately `614–644,340–360` | 59 | 59 gate 3 / 0 rays | Cone path not reached |
| External, right | 10,800 | `1041,354` | 23 | 23 gate 3 / 0 rays | Cone path not reached |

Later external windows 11,400–12,000 contain another 22 gate-3 entries while
the logged coordinates move through `445,339` and `792–884,269–321`. They are
retained as additional external evidence, but are not assigned to a particular
user-intended left/centre/right press.

Across all report windows there were 409 player fire entries: 229 gate-0
entries with 229 admitted rays in mode 1, and 180 gate-3 entries with zero rays
in mode 258. No other gate occurred. Final-barrel callbacks still occurred in
both paths: 291 in the first-person windows and 242 in external windows. This
explains why external fire still produces shots even though cursor aiming is
not admitted.

## The external failure is the cursor-active gate

Every retained event in both modes carried fire flags `0x2a`, which include
both required bits `0x2` and `0x20`. Every retained event also recorded gun
group 0, `aim_gun=0`, gun count 2 and mapped gun 0. The view-object address
matched the firing ship, the camera pointer was nonnull, the camera/fire ship
identity matched, and there were zero engine-read failures. Thus flags, aim
index, gun mapping, player identity and readable cockpit state do not explain
the view difference in this run.

In mode 1 all 46 retained fire samples had `cursor_active=1`, gate 0,
`admitted=1`, a cursor marker and complete `valid=0x1ff` ray/final evidence. In
mode 258 all 64 retained samples had `cursor_active=0`, gate 3,
`admitted=0`, no cursor marker and `valid=0x5f`. The missing ray/endpoint bits
in `0x5f` are the expected result of never reaching the admitted-ray site;
they are not failed reads.

The cursor-writer trace independently confirms that the engine supplies the
view-dependent state. It records 2,931 writes: 564 active and 2,367 inactive,
with three active-state changes and 2,888 coordinate moves. At the transition
reported at frame 5,400, the state changes from external inactive to internal
active. At frame 7,500 it changes from internal active to external inactive.
All internal firing windows retain active state; all external firing windows
retain inactive state, even while their coordinates continue to move. The
cursor tuple at every one of the 110 retained fire events exactly matches its
linked latest writer tuple.

Gate 3 is therefore an observed native input/script-state restriction for
external mode in this configuration. The read-only chase hook did not create a
bad flag, disabled gun index, wrong ship, stale pose or invalid mapping. A fix
must decide deliberately whether to override or replace that native external
cursor-active policy; the trace itself makes no such mutation.

## Ray, cone and final direction

The retained first-person rays behave as expected. Left and right inputs reach
approximately the configured 30-degree cone boundary: their sampled final
directions are 30.02–30.27 degrees from camera/ship forward and all 30 retained
edge samples are marked clamped. The centre samples are not clamped and finish
3.79–4.16 degrees from forward, consistent with the logged centre position
being slightly below geometric centre. These distinct results prove that
cursor position changes the admitted first-person ray.

The first clear external left/centre/right groups never reach the ray site.
Their final directions remain only 0.134–0.228 degrees from the live ship
forward axis despite their widely separated cursor coordinates, while they
are 20.123–20.162 degrees from the displayed camera forward axis. The later
external samples remain 0.134–0.227 degrees from ship forward. Camera lag and
ship motion increase their camera-forward separation to 22.57–27.52 degrees,
but do not create a cursor ray. This is the logged form of “fires straight.”

Within a retained event, first-to-last barrel direction differs by at most
0.292 degrees. Multiple final callbacks reflect individual muzzle/barrel
geometry after the shared admission decision; they do not show cursor response
when the ray site was skipped.

The sampled cone count is bounded evidence. Reports retain at most eight events:
110 of 409 entries were retained, 299 were dropped after counters and gate
classification, and dropped events caused 525 intentionally omitted follow-up
samples. There were zero orphan correlations, read failures and thread-slot
overflows. Aggregate gate and phase counters cover all player entries; detailed
coordinates, cones, identities and directions cover the retained subset.

## Pose freshness and source

All 110 retained events link to the latest camera context with the same cockpit,
firing ship and camera pointer. Camera-event and fire-event camera position and
basis are equal in every sample; relative bases and ship IDs also match. Pose
age is 10.058–13.274 ms in mode 1 and 5.018–14.451 ms in mode 258, far inside
the diagnostic two-second limit. The external samples explicitly say
`pose_mode=258 pose_applied=1`; the first-person samples say
`pose_mode=1 pose_applied=0` because that view is passed through. Ship basis can
advance slightly between the previous displayed-camera context and the live
fire read, as expected from normal frame ordering, but no sample is classified
as stale or mismatched.

This establishes that the external straight-fire result uses coherent live
ship/camera evidence. It does not establish projectile impact accuracy, target
selection behavior or the finite convergence correction that would be needed
after external cursor admission is enabled.

## Diagnostic overhead and limits

The aim observer recorded 4,102 measured callbacks: 2,931 cursor writes, 409
accepted player-fire entries, 229 admitted-ray callbacks and 533 final-barrel
callbacks. They total 7,236.8 microseconds, a call-weighted mean of 1.764
microseconds, with a maximum of 33.8 microseconds. Its declared scope begins
after the first QPC and includes the diagnostic lock. It excludes the injected
stub, full CPU save/restore, first QPC and the early AI/player filter. The
global cursor writer has no player filter.

The camera handler recorded 11,761 calls totaling 256,728.8 microseconds, a
21.829-microsecond call-weighted mean. Nonempty report-window means ranged from
13.685 to 28.247 microseconds; the largest single call was 1,541.0 microseconds
in the frame-600 window. Camera timing includes the aim diagnostic's context
snapshot callback and excludes its stub and CPU save/restore.

Both timing series are partial diagnostic handler costs. They are not per-draw
GPU cost, whole-frame CPU time, frame pacing or game FPS. This run used
CrossOver Preview's X3 bottle; it supplies no native-Windows runtime result. It
does not visually accept the new camera framing, verify projectile impacts, or
test a future external cursor-state override.
