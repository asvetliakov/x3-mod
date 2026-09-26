# Review 48: elevated chase camera and softer follow

Independent source review, 2026-09-13. Scope: the positive-pitch chase-camera
geometry, the exact zero-pitch compatibility path, the new spring defaults,
launcher/configuration plumbing, focused host controls, architecture text and
the bounded host performance record. The work is based on `60a26b5`. The
separate mouse-fire investigation was considered only at the camera boundary;
no firing-path code is part of this change.

## Result

**Source disposition: the elevated-camera delta is approved after one test
harness fix.** No code, correctness, portability or performance finding remains
open in this scope. The broader candidate still awaits integration and review
of the separate aim-trace work; this is not a full-build readiness claim. This
review did not build the full DLL, run Wine, launch the game, install anything
or commit the working tree. The new geometry and timing still require the
user's gameplay acceptance; native Windows runtime behavior remains untested.

## Geometry and behavior assessment

For `pitch_down_deg > 0`, the target camera frame is correctly constructed in
ship coordinates. The native relative-view forward row supplies horizontal
heading, the ship-up axis removes native camera-local pitch and roll, and the
requested downward pitch supplies the new forward axis. Multiplying that frame
by the derived ship basis retains the ship's complete world orientation while
adding no separate world-level horizon or native camera roll.

The boom construction is consistent with the game's row-vector convention. If
the native camera-to-anchor ray has horizontal slope `h` and the requested
vertical screen slope is `q = offset_y * tan(half_vfov)`, the implementation
uses camera-frame ship-to-camera direction `(-h,q,-1)`. It rotates that vector
through the new relative frame and normalizes it to
`native_boom_length * distance_scale`. Consequently, on a snapped/settled
frame:

- the camera looks down by the requested ship-relative angle;
- the anchor's horizontal camera-space slope matches the native view;
- its vertical projection is exactly `(1 + offset_y) / 2` of screen height;
- camera-to-anchor distance equals the scaled native boom length.

At the defaults (`pitch_down_deg=20`, `offset_y=0.45`, vertical half-FOV tangent
0.75), the camera boom is 38.65 degrees above ship-back while the camera looks
down 20 degrees. This produces the requested physically elevated view rather
than merely tilting an unchanged native boom.

The extra per-frame guards are coherent with that construction. They reject a
nonfinite screen slope, absolute combined vertical-plane elevation at or above
80 degrees, a native camera ray behind the view or more than 60 degrees off its
forward axis, and a reconstructed boom that is no longer materially behind the
ship. The first two report `InvalidInput`; incompatible view geometry reports
`NotBackView`. Both flow through the existing refusal/reset behavior and leave
the vanilla pose intact.

`pitch_down_deg == 0` selects the original two target expressions exactly:
`pitch_up(atan(offset_y * half_vfov_tan)) * vanilla_camera` and the native
ship-frame boom times `distance_scale`. With the former 0.15/0.20-second tau
values, this is an explicit rollback to the preceding framing and response.

The native-anchor selector and every earlier admission gate run before the new
target branch and are unchanged. The spring code, 8-degree orientation clamp,
0.10-length position clamp, combat scaling, maximum-dt handling, snap reasons,
sector coalescing and state counters are also unchanged. The output still
publishes `view_rel = camera_basis * ship_basis^T`; the existing camera-pose
continuity layer therefore retains its enter/exit snap publication and each TAA
consumer still observes a cut independently.

## Configuration and defaults

`X3M_CHASE_PITCH_DOWN_DEG` is parsed with the existing bounded environment
reader, logged on installation and exposed as `--chase-pitch-down-deg`. Both
the DLL and launcher accept `[0,30]`; NaN and infinity fail comparison and are
rejected. Supplying it without `--camera chase` is refused before launch. A dry
run forwards explicit values without launching an executable.

The compiled defaults are 20 degrees of downward pitch, 0.22 seconds rotation
tau and 0.30 seconds position tau. The existing 0.45 screen offset and both lag
clamps remain unchanged. A later user request selected a 0.6 distance scale;
the bounded follow-up review is recorded below. The larger tau values slow an
unclamped step response without enlarging either bounded lag window.

## Finding resolved during review

| # | Severity | Location | Finding and resolution |
| --- | --- | --- | --- |
| R1 | low | `verification/analysis/test_chase_camera.py`, launcher helper | The initial dry-run forwarding control created a fake Wine executable but no `X3AP.exe`, so `manage.py` correctly rejected its temporary game directory and the positive forwarding case exited 2. The helper now creates the required inert file. The subprocess call remains mocked to fail if reached, and the repaired focused suite passes. |

## Verification

I independently ran:

```sh
python3 -m unittest \
  verification.analysis.test_chase_camera \
  verification.analysis.test_chase_camera_site -v
```

All **56** cases passed: 48 portable pipeline/CLI controls and eight read-only
site checks, including the installed executable identity. The added controls
cover exact vertical projection at four FOVs, downward orientation, native
horizontal projection, noncommuting ship/native yaw-pitch-roll transforms,
scaled boom distance, signed offset and extreme geometry, exact zero-pitch
targets, view gates, translation invariance, reentry/teleport snaps, softer
response and a 100,000-frame turning/rolling run with unchanged lag limits and
camera/view identity.

I also compiled the changed production translation unit directly for x86
Windows with warnings as errors and the required arithmetic/stack contract:

```sh
i686-w64-mingw32-g++ -std=c++17 -O3 -Wall -Wextra -Werror \
  -msse2 -mfpmath=sse -mstackrealign -mincoming-stack-boundary=2 \
  -I src/proxy -c src/proxy/chase_camera.cpp \
  -o /tmp/chase-camera-review-final.o
```

It passed and produced a 63,283-byte object. `git diff --check` also passed.
No new ABI boundary, export, heap allocation, lock, memory query or draw-time
operation was introduced. The target math executes once per admitted camera
update inside the existing full CPU-state boundary; the production compilation
retains SSE2 and four-byte incoming-stack realignment flags.

The host performance JSON is internally consistent: three one-million-step
samples for each geometry branch, six million applied frames, no refusals, and
recomputed medians of 0.17052825 us/step for zero pitch and 0.169637834 us/step
for an explicit ten-degree, distance-scale-1 positive-pitch sample. The
difference is measurement noise. The later user-selected 20-degree / 0.6-distance
defaults take the same positive branch, so the constant-only updates did not
justify repeating this coarse host timing.
The record includes synthetic input generation and does not measure the assembly
stub, CrossOver, native Windows, load time or game FPS.

Reviewed identities at the initial 20-degree / distance-scale-1 freeze follow.
The distance-scale addendum below supersedes the identities of files changed by
that bounded update. Concurrent aim-trace edits are outside this review.

| File | SHA-256 |
| --- | --- |
| `src/proxy/chase_camera.cpp` | `1038053f3d1f803e217b5927ef8571c67fdbedbad5256efd1f8b333c81faaade` |
| `src/proxy/chase_camera_math.h` | `50c48b67a04f670f0218c4e3dec1380bbc4b96aa7df3f7b04080f188168779be` |
| `tools/manage.py` | `14b550db0e4ee8dbaea71017e9f3f910d3e2c976982ca2ee0d2467e0552232ac` |
| `verification/analysis/test_chase_camera.py` | `45ea8edd9adc0bf1e6e09a02aa55bf8547a3462cd4170fa90bb69be485f5171c` |
| `verification/probe/chase_camera_host.cpp` | `f3a14f3edc90fbbfabcfd6e55a0b6b78ec9e57f34fc27b7227a6d2cbdf96baec` |
| `verification/probe/chase_camera_native_host.h` | `747e02a00af3002a29462ebecf77bd25ae25bc8a06f5a102b910271ba904763f` |
| `docs/architecture/chase-camera.md` | `1363d14a87b053c0f7e49fc123cad0955b159b99ef727ff423d901cf8456d380` |
| `docs/architecture/elevated-chase-camera.md` | `11a34218dab8a077cedb2e7d75544d2260122891ed119e39577b231aeb4e945e` |
| `verification/results/chase-elevated-host-performance.json` | `24c60d0b6fe60d9bc25977d8295e4df71f52ddc6580a262e4eab7dffb57b1e42` |

## Distance-scale 0.6 follow-up

The user subsequently requested a closer default camera. The compiled
`distance_scale`, launcher help and architecture tables now consistently use
**0.6**. This is a target-length change only: the elevated branch normalizes the
same camera-frame ray to `0.6 * native_boom_length`, so pitch, horizontal slope
and the requested 72.5% settled-anchor projection are unchanged. The absolute
position-lag clamp becomes 0.10 of that shorter target length, as required by
the existing fractional clamp contract. Admission, guards, springs, snaps and
TAA-cut behavior are unchanged.

The default geometry control independently requires the 72.5% projection at
four FOVs and distance `0.6 * hypot(40,200)`. The compiled-default control
requires 0.6. The native-anchor fixture explicitly sets distance scale 1 because
its oracle compares an unscaled native camera position; this keeps that older
selector test independent of the new presentation default. Existing arbitrary
distance coverage still checks a 2.0 multiplier, and zero-pitch rollback still
uses an explicit distance scale rather than relying on the compiled default.

The documentation correctly avoids a whole-hull guarantee. Moving closer makes
the ship larger and may crop its hull depending on ship dimensions and FOV;
only the selected anchor projection and scaled anchor distance are guaranteed.
The retained performance artifact is explicitly labeled as an earlier
ten-degree / distance-scale-1 sample of the same positive-pitch algorithm.

The current status, goals and next-run plan are consistent with this boundary.
They distinguish the installed `0c642df` anchor-correction DLL from the pending
20-degree / 0.6-distance / 0.22/0.30-second candidate, state that the 0.6 update
still awaits final candidate compilation and installation, and require waiting
for that installation before the next flight. The run plan asks separately
whether the top is visible and whether the whole hull fits; it does not promise
either from anchor projection alone. It also describes the consolidated
cursor/fire work as read-only diagnostics and makes no aim-fix claim.

I reran the same focused command after the 0.6 freeze: all **56** camera/site
cases passed, including the installed-executable read-only checks, and
`git diff --check` passed. Per the bounded scope, I did not compile a production
object or run Wine, the game, a full build, installation or commit. No finding
was opened by this follow-up.

Distance-follow-up identities:

| File | SHA-256 |
| --- | --- |
| `src/proxy/chase_camera_math.h` | `e49147dffcebdf763c636587fe6fcec655bbae0bb0db0eedd0c5712f8e1e01f8` |
| `tools/manage.py` | `abcdb92a9793df4da46c73df5fefe4c623e3d97fe36201f6e6f488617d457503` |
| `verification/analysis/test_chase_camera.py` | `f2340b92bd209d52df7d3a32d02c53072930c580110e3d2b6e504b4057f5e54b` |
| `verification/probe/chase_camera_native_host.h` | `72523ef74f9593f515f8678052a934b64e6ed5fa1564e77d290796385034c8ef` |
| `docs/architecture/elevated-chase-camera.md` | `f3fa9a26e66e00a1846f00d38a6f2253b513891213dce5a29f8d5a1035f5053b` |

`docs/architecture/chase-camera.md` also carries the 0.6 table entry, but its
current file identity includes concurrent aim-trace documentation outside this
review, so it is deliberately omitted from the scoped identity table.

## Remaining acceptance

The host evidence establishes the target geometry and state-machine behavior,
not the visible silhouette, subjective follow feel or live handler writes. The
user must launch/load the scene for the elevated-camera visual check. Aggregate
handler timing, menu/pause behavior, resolution changes and the existing
double-cursor-after-alt-tab item remain runtime checks. Native Windows source
compatibility is retained by portable C++ and documented Win32 interfaces, but
execution there is unverified.

Mouse fire remains a separate reverse-engineering and implementation task. The
camera continues to keep its basis and `view_rel` angular identity aligned, but
an elevated camera and a gun-origin finite ray have parallax; this review makes
no fire-correction claim.
