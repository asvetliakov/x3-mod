# Elevated chase framing and softer follow

2026-09-13. The corrected native-anchor build no longer trembles in the user's
second flight. The user now requests a camera physically above and behind the
ship, looking down to show its top, and softer/slower following. This iteration
changes the target geometry and spring defaults. It does not change the native
anchor reader or implement a firing correction. Gameplay acceptance of the
new framing and timing is pending.

## Geometry contract

`X3M_CHASE_PITCH_DOWN_DEG` / `--chase-pitch-down-deg` defaults to **13 degrees**,
following the user’s next-flight request to reduce the angle from 20 degrees.
`--chase-pitch-down-deg 25` is also within the supported range.
The accepted configuration range is `[0,30]`; zero explicitly selects the old
geometry, rather than an elevated camera with zero depression. Combined with
`--chase-rot-tau 0.15 --chase-pos-tau 0.20 --chase-distance-scale 1`, zero
restores the previous framing and following behavior. The user requested a
revised distance after that flight: distance scale is now **0.85**, placing
the settled camera at 85% of the native boom length (previously 60%). The
existing `offset_y=0.45`, rotation lag limit 8 degrees and position lag limit
0.10 are retained.

The 13-degree / 0.85 follow-up passed all 56 focused camera and camera-site
tests. The orchestrator reviewed the author's source, CLI, geometry oracles
and documentation delta with no open findings. This changes constants only;
the existing performance analysis applies. These defaults are not installed
yet; the third gameplay run below used 20 degrees / 0.6.

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
The 0.85 distance is farther out than the previous 0.6 setting, but remains
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

Rotation tau changes from 0.15 to **0.22 seconds** and position tau from 0.20
to **0.30 seconds**. The critically damped closed form, world-frame rotation
velocity approximation, ship-relative boom spring, clamps, snap/coalescing
rules, combat scaling and maximum dt are unchanged. Larger tau softens turn
onset and extends settling. A sustained fast turn still reaches the same lag
limits, so this does not promise unlimited/slack following. For an unclamped
step, 95% settling takes approximately 4.75 tau: about 1.05 seconds for
orientation and 1.43 seconds for position with the new defaults.

## Verification and cost

The exact production math is exercised by host controls for downward forward
orientation, anchor projection across four FOVs, preserved distance, native
pitch/yaw/roll, rotated ships, exact legacy target compatibility, pure
translation, reentry/teleport, view gates, signed offsets, invalid/extreme
geometry and a 100,000-frame turning/rolling stability run. CLI controls check
forwarding, chase-mode dependency and invalid numbers using a fake executable
and `--dry-run`; no executable is launched. The camera/site suite passes **56
checks**. `chase_camera.cpp` cross-compiles for x86 Windows with SSE2 and the
required four-byte incoming-stack realignment flags. This is not verified
native-Windows or game behavior.

The extra work is once per admitted camera update: fixed-size vector/matrix
math and a few scalar transcendentals. There is no added per-draw work,
allocation, lock, memory probe or per-frame logging. Three alternating
one-million-step host samples per mode, including synthetic input generation,
measured median 0.17053 us/step for legacy geometry and 0.16964 us/step for
elevated geometry at the earlier explicit 10-degree / distance-scale-1 setting (the algorithm is
unchanged by the current 13-degree / distance-scale-0.85 defaults); the small difference is noise, not a claimed speedup. All
six million frames applied without refusal. Local evidence:
`verification/results/chase-elevated-host-performance.json`. These host timings
do not measure CrossOver, the complete hook boundary, game FPS or load time.
The next user run can use the existing aggregate handler timings for comparison.

The install line records `pitch_down_deg` beside the existing tunables. No
window/presentation behavior changes here; the existing double-cursor-after-
alt-tab and loading checks remain separate acceptance items. Mouse fire needs
its own engine-path investigation: matching `view_rel` to camera orientation
alone does not compensate a gun-origin ray for camera-origin displacement.
