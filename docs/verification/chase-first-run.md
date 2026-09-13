# Chase camera: first game run

The first user-run chase session proves that the byte-verified hook installed
and wrote camera poses in X3 under CrossOver Preview. It does **not** accept the
camera's appearance: the user found the placement too subtle, reported
trembling during flight, and saw the ship appear displaced before returning.

## Provenance

| Item | Value |
| --- | --- |
| Session | `session-20260913-054829-212` (Windows PID 212; host PID previously observed as 14912) |
| Retained log | `/tmp/x3-game-session-20260913-054829-212/session-20260913-054829-212.log` |
| Log size | 10,171,479 bytes; 208,592 lines |
| Log SHA-256 | `590601a918c6135edeb7787b0fb2ede0adabdb36c30cb795546bad10886307f7` |
| Stable source mtime | `1789264245841285896` ns before and after copying |
| Local metadata | `/tmp/x3-game-session-20260913-054829-212/provenance.plist` |
| Matching artifacts | 54 logged shader binaries; aggregate SHA-256 `9a65077b62f59bb3ffca4f79742dd0cc77c5eaa1c7a3a483741bfb3ddc39fe99` |

`game_guard` returned `[]` throughout the copy. No readback was logged. The
single detailed capture was frame 120, before the first applied chase pose, so
it cannot verify placement or trembling.

## Configuration and activation

The user launched `--direct --camera chase` without telemetry. The install row
records `requested=1 installed=1 status=active` at `0x00420e06`. Defaults were
rotation/position time constants 0.15/0.20 s, vertical offset 0.12, distance
scale 1.0, rotation clamp 8 degrees and position-lag clamp 0.10. Combat
tightness and the optional scene fix were off. Motion output, TAA, HDR and the
scene hook were also off; `handler_timing=0`.

The first successful write occurred at handler frame 584 and was reported at
frame 1200. The row recorded `half_vfov_tan=0.7500`, `fov298=0x4000`, equal
reference/view-object pointers and a negative-Z back-view boom. These are
recorded values only; they do not establish aspect correctness for the
1280x768 screenshot.

At the last report, frame 7500, the cumulative counters were:

| Counter | Value |
| --- | ---: |
| Active-cockpit frames | 6,650 |
| Applied | 5,871 |
| Refused/pass-through | 779 |
| Inactive-cockpit visits | 0 |
| Distinct cockpits in the report window | 1 |
| Snaps / coalesced snaps | 2 / 0 |
| Clamp events | 2,724 |
| Write refusals | 0 |

Only verdict 0 (applied) and verdict 1 (internal view) appeared in the sampled
rows. Refusals stopped increasing after frame 2700 while applied frames grew
from 1,343 to 5,871, demonstrating sustained external-back-view admission. The
two snaps both had reason 1 and correspond to initial entry or re-entry; the
snap count did not grow during the continuous chase interval. The user's
"snaps back" description therefore is not a logged chase discontinuity.

Both spring limits were exercised. Sampled rotation lag reached exactly 8.000
degrees, and sampled position lag reached 1,740.3 at a camera distance of
17,355.9, approximately the configured 10% limit. `clamps=2724` counts clamp
events, not distinct frames: rotation and position can each increment it on one
applied frame. Frequent clamping is relevant evidence for the visual report,
but these 300-frame samples do not establish the cause of trembling or the
reported return motion.

## User acceptance and limits

In the supplied X3 screenshot, the ship appears at approximately 58% of screen
height; the supplied reference suggests a bottom-center target around 70–75%.
Those percentages are our approximate visual readings, not numbers supplied by
the user. The current `offset_y=0.12` is defined as 12% of half-screen height
below center, which nominally places the ship near 56%. Under that mapping,
70–75% corresponds to an initial tuning range of approximately
`offset_y=0.40–0.50`; the final value requires another visual test.

The session sampled chase state only every 300 frames. Without telemetry there
is no handler-cost measurement, and without TAA there are no `camera_cut` rows
or ghosting evidence. The log contains no invalid-input, numeric, write-refusal
or other explicit chase fault. It ends at the frame-7500 chase row without a
last-device or shutdown marker.

The installation and applied-write path are accepted for this CrossOver run.
Visual placement and smoothness are not accepted. A position-domain cause and
any corrective implementation remain hypotheses until code review and a later
gameplay run verify them; this session alone does not validate a bug fix.
