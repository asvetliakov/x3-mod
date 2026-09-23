# Elevated chase framing and softer follow

2026-09-13. The corrected native-anchor build no longer trembles in the user's
second flight. The user now requests a camera physically above and behind the
ship, looking down to show its top, and softer/slower following. This iteration
changes the target geometry and spring defaults. It does not change the native
anchor reader or implement a firing correction. The latest user flight accepts the 13-degree angle and reports no tremble.
The next requested tuning increases distance and softens follow again; that
new tuning has not yet been tested in gameplay.

## Geometry contract

`X3M_CHASE_PITCH_DOWN_DEG` / `--chase-pitch-down-deg` defaults to **13 degrees**,
following the user’s next-flight request to reduce the angle from 20 degrees.
`--chase-pitch-down-deg 25` is also within the supported range.
The accepted configuration range is `[0,30]`; zero explicitly selects the old
geometry, rather than an elevated camera with zero depression. Combined with
`--chase-rot-tau 0.15 --chase-pos-tau 0.20 --chase-distance-scale 1`, zero
restores the previous framing and following behavior. The user requested a
revised distance after that flight: distance scale became **0.90** (the 2026-09-16 state), placing
the settled camera at 90% of the native boom length (previously 85%, and
60% in the earlier elevated-framing flight). **2026-09-23:** the default is now **1.05** (user decision: the boom
15–20 % longer so corvettes fit; compiled default and launcher); the 0.90 figures below are the 2026-09-16 state. The
existing `offset_y=0.45`, rotation lag limit 8 degrees and position lag limit
0.10 are retained.

The 13-degree / 0.85 follow-up passed all 56 focused camera and camera-site
tests. The orchestrator reviewed the author's source, CLI, geometry oracles
and documentation delta with no open findings. This changes constants only;
the existing performance analysis applies. That 13-degree / 0.85 configuration was subsequently installed and accepted
for angle and stability in user run `/tmp/x3-bottleX3-run18`. The user requested
0.90 distance and a slower/softer response for the next iteration.

For positive pitch, the target frame uses the ship's up axis and the native
view forward axis projected onto the ship's horizontal XZ plane. This preserves
native view yaw and the ship's own world yaw/pitch/roll. Native camera-local
pitch and roll are replaced by the requested downward pitch and zero extra
roll. It is intentionally a ship-relative frame, not a world-level horizon.
Only the existing ordinary external back-view/connect-mode gates admit it;
internal/front/side/scripted views continue through vanilla.

In the target camera frame, let `q = offset_y * tan(half vertical FOV)` and let
`h` be the native anchor's horizontal camera-space slope (`native_ray.x/z`).
The desired camera-to-ship ray is proportional to `(h,-q,1)`. Therefore the
ship-to-camera boom is `(-h,q,-1)`, rotated through the target frame and
normalized to `native_boom_length * distance_scale`. The resulting target
satisfies all three constraints together:

- The camera forward axis points down by the requested angle relative to ship
  forward, with the native horizontal heading.
- The anchor projects to vertical screen fraction `(1+offset_y)/2` (72.5% at
  the default) and retains its native horizontal projection slope.
- Distance from the ship anchor remains the scaled native boom length.

For a horizontally centered native view, boom elevation is
`pitch_down + atan(q)`. With `tan(half vertical FOV)=0.75`, the default gives
31.65 degrees of boom elevation while looking down 13 degrees. This shows why
simply pitching the old camera downward would fail: it would move the ship
upward on screen without raising the camera. Native boom elevation and view
pitch are accounted for by reconstructing the boom, not added twice.
These guarantees describe the settled anchor. The ship silhouette and bounded
spring lag can move its visible center. Pitch alone cannot guarantee the entire
ship silhouette fits; that also depends on ship dimensions, FOV and distance.
The 0.90 distance (2026-09-16 state) is farther out than the previous 0.85 setting, but remains
closer than the native boom and can still crop the hull; it preserves the
anchor placement, not a full-hull visibility guarantee.

Invalid tunables prevent installation. Per-frame geometry separately refuses
nonfinite FOV/slope, a combined signed vertical-plane elevation with absolute
value at least 80 degrees, native anchor rays behind the camera or with
horizontal angle over 60 degrees, or a reconstructed boom whose ship-local
Z is not below `-0.1 * scaled_length`. Extreme FOV combinations return
`InvalidInput`; incompatible native view rays/target positions return
`NotBackView`. Refusal resets tracking and leaves the vanilla pose; valid
reentry snaps. Negative `offset_y` remains supported and can deliberately put
the camera below the ship. The near-vertical guard applies to either sign.

## Softer following

Rotation tau changes from 0.22 to **0.28 seconds** and position tau from 0.30
to **0.38 seconds** after the latest user flight. The critically damped closed form, world-frame rotation
velocity approximation, ship-relative boom spring, clamps, snap/coalescing
rules, combat scaling and maximum dt are unchanged. Larger tau softens turn
onset and extends settling. A sustained fast turn still reaches the same lag
limits, so this does not promise unlimited/slack following. For an unclamped
step, 95% settling takes approximately 4.75 tau: about 1.33 seconds for
orientation and 1.81 seconds for position with the new defaults.

## Verification and cost

The exact production math is exercised by host controls for downward forward
orientation, anchor projection across four FOVs, preserved distance, native
pitch/yaw/roll, rotated ships, exact legacy target compatibility, pure
translation, reentry/teleport, view gates, signed offsets, invalid/extreme
geometry and a 100,000-frame turning/rolling stability run. CLI controls check
forwarding, chase-mode dependency and invalid numbers using a fake executable
and `--dry-run`; no executable is launched. The camera/site suite passes **56 tests** with the new 0.90 / 0.28 / 0.38
defaults, including comparison against the preceding 0.22 / 0.30 spring
response. Command: `PYTHONPATH=verification/probe python3 -m unittest
verification.analysis.test_chase_camera verification.analysis.test_chase_camera_site`. `chase_camera.cpp` cross-compiles for x86 Windows with SSE2 and the
required four-byte incoming-stack realignment flags. This is not verified
native-Windows or game behavior.

The extra work is once per admitted camera update: fixed-size vector/matrix
math and a few scalar transcendentals. There is no added per-draw work,
allocation, lock, memory probe or per-frame logging. Three alternating
one-million-step host samples per mode, including synthetic input generation,
measured median 0.17053 us/step for legacy geometry and 0.16964 us/step for
elevated geometry at the earlier explicit 10-degree / distance-scale-1 setting (the algorithm is
unchanged by the then-current 13-degree / distance-scale-0.90 defaults); the small difference is noise, not a claimed speedup. All
six million frames applied without refusal. Local evidence:
`verification/results/chase-elevated-host-performance.json`. These host timings
do not measure CrossOver, the complete hook boundary, game FPS or load time.
The next user run can use the existing aggregate handler timings for comparison.

The install line records `pitch_down_deg` beside the existing tunables. No
window/presentation behavior changes here; the existing double-cursor-after-
alt-tab and loading checks remain separate acceptance items. Mouse fire needs
its own engine-path investigation: matching `view_rel` to camera orientation
alone does not compensate a gun-origin ray for camera-origin displacement.


## Latest flight follow-up: lead marker and sector travel

User run `/tmp/x3-bottleX3-run18` reports working cursor fire, no camera tremble,
and acceptance of the 13-degree angle. It also reports a missing predictive
lead marker and a camera view reset after sector travel; docking remains
untested. These are separate engine behavior questions. The default tuning
above does not repair either of them. See the [lead-marker gate and projection
study](../reverse-engineering/chase-lead-reticle.md).

### Transition reset: verified native sites and remaining proof

Targeted Ghidra disassembly of the same documented X3AP.exe image identifies:

| Site | Native operation | Consequence |
| --- | --- | --- |
| `0x0041fbe8` in constructor `0x0041f8d0` | Initialize cockpit `+0x150` view mode to zero | Cockpit recreation can discard native view state; address reuse alone cannot prove continuity. |
| `0x00419e06` in serializer `0x00419430` | Load saved cockpit view mode directly into `+0x150` | Proven alternate writer, bypassing script requests; see the later VM/load study below. |
| `0x0042e742`, dispatcher `0x0042d340` case `0x30` (`INS_CockpitSetViewMode`) | Write requested mode ECX into cockpit EAX `+0x150`; argument block ESI supplies the mode at `+6` | This is the native script-controlled assignment. It contains no sector predicate or user-intent distinction. |
| `0x0042e05d..0x0042e069`, case `0x2f` | Set native view boom `+0x130/+0x134/+0x138` | Restoring only mode 258 need not restore an ordinary rear chase pose. |
| `0x00422cd0`, case `0x2c` destination | Set connect mode `+0x1c0`; mode zero can rewrite view angles while internal | Mode/connect/angle ordering matters when returning from a transition. |
| `0x0042d4fe`, case `5` | Set sector camera `+0x58` | Camera identity can change independently of the cockpit. |
| `0x0042d670`, case `0xb` | Set cockpit reference sector `+0x54`, then `0x00420360` | This command is a useful transition observation, but does not itself change view mode. |
| `0x0042d6ba` / `0x0042d6eb`, cases `0xc` / `0xd` | Set followed object `+0xc` / view object `+0x10` | Ref/view replacement must revoke any pending restore. |
| `0x00421024` / `0x004210d6` in camera update | Clear / publish current sector `+0x1fc` after the pose hook | The current chase hook at `0x00420e06` can still see the previous sector for one frame. |

The targeted direct-displacement scan found no view-mode store in the per-frame
camera update `0x004205e0`. Constructor initialization, save deserialization and the script dispatcher
are proven writers; this is not a whole-program proof against indirect writes.
It has not yet been established whether run18 recreates the cockpit or invokes
the mode command, or which script operation is responsible. The locally
extracted `x3story.obj` has compiled CODE/SYMB/CLAS data without usable source
names; the native command alone does not identify player intent. No transition
restore is implemented in this iteration.

The bounded fix should be a one-use transition ticket, armed only when an
identified sector-transition entry observes the player's admitted ordinary rear
chase view. Let the transition's native camera sequence run. Consume the ticket
at a proven completion boundary for the same player ship, after validating the
new camera and native rear-view geometry; use the native view command sequence
rather than forcing `+0x150` at each pose. Cancel on explicit player view input,
ship replacement/ejection, unsupported connect/cinematic mode, cockpit lifetime
uncertainty, timeout, or failed validation. After the one restore, leave every
ordinary user view change authoritative. Sector change alone is not sufficient:
a player could deliberately change view during the loading interval.

The missing prerequisite is a source distinction between transition-reset and
user-request commands, plus the complete rear-view command sequence. A single
bounded diagnostic batch should observe mode writes at `0x0042e742`, related
mode/boom/connect/ref/camera commands and cockpit creation/destruction, retaining
script command identity and before/after values only on state changes. The
writer receives an already-marshalled argument block, so a stable script origin
must be verified in the VM before treating that block's address as provenance.
Pair the record with one deliberate view change and one user-driven sector
transition; docking is a separate unproven case. This avoids a broad every-frame
restore that would silently defeat deliberate view changes.

Private raw study output: `/tmp/x3-camera-study/chase-sector-reset{,2,3}.txt`;
reproduce with the existing `X3CameraState.java` read-only workflow, selecting
`load:0x150`, `ins:0041f8d0`, `ins:0042d340`, `dec:00420360`, and
`dec:00422cd0`. No extracted engine bytes or decompiler output are tracked.

The subsequent [VM command-origin study](../reverse-engineering/chase-view-transition.md)
identifies task-relative instruction and method provenance at the actual mode
writer, a separate save-deserialization mode writer, native input dispatch
paths, and a consolidated bounded diagnostic proposal. Which writer caused the
run18 reset remains unproven.
