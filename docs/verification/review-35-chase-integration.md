# Review 35: chase-camera integration

Independent integration review, 2026-09-13. Scope: the chase-camera changes
merged from prototype branch `7f4b251` into the main source candidate, including
the engine-patch rel32 contract, loader/capture lifetime wiring, engine reads and
writes, TAA cut delivery, launcher controls, host tests and the loading fixture.
The existing motion/HDR/exposure/loading code was checked at the changed seams.
This review did not launch the game, install a DLL or run Wine.

## Result

**Source disposition: approved for the full fixture chain after the fixes
below.** No blocking source finding remains. This establishes source and host
test readiness. The completed fixture chain below qualifies the combined
CrossOver build; it does not establish in-game camera behavior or native-Windows
runtime behavior.

| Check | Result |
| --- | --- |
| Clean MinGW CMake build | PASS, no compiler warning or error |
| `python3 -m unittest verification.analysis.test_chase_camera verification.analysis.test_chase_camera_site` | PASS, 42 tests (34 pipeline/integration + 8 executable-site cases) |
| `verification/probe/check_no_x87.py` | PASS, 211 inspected functions, 0 violations; the chase entry uses the documented full CPU boundary |
| `x3m_chase_camera_enter` object audit | PASS: save x87/MXCSR, `fninit`, local `ldmxcsr 0x1f80`, handler, then restore x87/MXCSR/LastError on normal and exception-cleanup exits |
| Full host analysis discovery | PASS, 907 tests in 43.694 s; camera host fixture emits non-failing subprocess-stream ResourceWarnings |
| CLI dry-run/validation controls | PASS, six cases: vanilla, chase, chase+TAA, signed offset, NaN rejection and chase-only-option rejection; inherited combat/scene-fix controls reset off |
| Full Wine/fixture regression chain | PASS, all 18 serialized wrappers; both bottles pass 98 motion cases plus 16 benchmark processes each. See the [qualification summary](../../verification/results/chase-integration-summary.json). |
| Fresh result-record host controls | PASS, 21 tests against final reader, gzip and crypto summaries |
| Exact final DLL audit | PASS, 41 objects, 144 stable motion-source hashes, 17 exports and no-x87 211/0; [artifact record](../../verification/results/chase-final-artifact-audit.json) |

## Findings resolved during integration

| # | Severity | Location | Finding and resolution |
| --- | --- | --- | --- |
| I1 | medium | `src/proxy/chase_camera.h:61-83`, `src/proxy/chase_camera.cpp:41-42,107-109`, `src/proxy/motion_output.cpp:698-706`, `src/proxy/motion_output.h:650` | The prototype exposed a destructive global `take_snap()`: the first `MotionOutput` to resolve consumed the cut, allowing another device to retain pre-snap history. Replaced it with a process-wide generation and one cursor per output. Two independent consumers now observe each generation once. A failed run is safe because all failures after observation invalidate that output's history. |
| I2 | medium | `src/proxy/chase_camera.cpp:107-109,117-125,158-164,199-244`, `src/proxy/chase_camera.h:74-83` | The prototype cut only on an applied snap. Leaving chase for a vanilla view could therefore reuse chase history when the pose change stayed below the ordinary angular threshold. `PoseContinuity` now publishes one cut on the first applied-to-vanilla transition, publishes no cut for an initial failed write, and cuts again when a later successful snapped write re-enters chase. Host coverage includes enter, follow, leave, repeated refusal, re-entry and write failure. |
| I3 | medium | `src/proxy/chase_camera.cpp:264-285` | The full boundary saved and restored MXCSR but let injected SSE2 arithmetic inherit the game's exception masks, rounding mode, FTZ and DAZ. After capture the entry now loads local MXCSR `0x1f80`, beside the existing `fninit`; `PreserveCpuState` restores the caller's exact state on return. |
| I4 | low | `src/proxy/chase_camera.cpp:53-60,88-103,199-219` | Positive writable results were cached for the process lifetime by raw pointer. A destroyed object could be recreated at the same address with different protection. The handler now queries both destination spans immediately before each applied write; the optional scene-camera write is freshly queried too. |
| I5 | low | `src/proxy/engine_patch.cpp:89-117`, `verification/probe/loading_trace_fixture.cpp:40-85` | Rel32 behavior had only arithmetic/source tests. The fixture now executes forward and backward near-Jcc taken/fall-through paths before and after claim, verifies restored bytes and behavior, exercises the cross-qword plain-copy path, rejects unsigned offset overflow and rejects a late claim. Bounds validation uses `offset > length - 4` after the minimum-length check. |
| I6 | info | `src/proxy/chase_camera.cpp:231-260,385-415` | `first_applied` used to capture a mathematically applied verdict even if destination validation refused the write, and no aggregate handler cost was available for the performance pass. It now captures only a written pose. With telemetry enabled, report cadence logs total/mean/max handler CPU time under the camera statistics lock, without per-frame output. |

## Engine patch and lifetime assessment

The site remains exact-executable and exact-byte gated at `0x00420e06`. Its
ten-byte displaced span contains `cmp [ebx+0x54],0` and a near `jz`; the declared
rel32 field starts at offset 6. `claim()` copies the full instructions into the
tail, rebases the branch to the original absolute target, appends the jump back
to `0x00420e10`, and installs the five-byte dispatcher jump. The first five
bytes straddle a qword, so the non-atomic copy is restricted to the loader's
install window. Initialization runs after backend load and before any factory
is returned; the first Present closes the window and later claims fail as
`late_claim`.

The hook is intentionally process lifetime. Last-device release logs
`kept=1` and does not restore it because a recreated device could not safely
reclaim the site after the install window closed. The handler owns no D3D
device, so device reset and recreation do not invalidate it. `shutdown()` is an
explicit teardown helper with no production device-path caller.

## Compatibility and performance assessment

With `X3M_CAMERA=vanilla` (the launch default), `wanted()` is false: camera
initialization, arena allocation and engine patching do not run. The launcher
also emits deterministic `X3M_CHASE_SCENE_FIX=0` and
`X3M_CHASE_COMBAT_TIGHTNESS=0` defaults instead of inheriting those two controls
from its parent environment.

The loader adds camera initialization after the existing scene-hook setup and
before object exposure. The TAA integration is additive at resolve-input
assembly; it does not alter motion-vector generation, the camera sentinel
matrix, HDR routing, exposure, sharpen, render-state recovery or loading-probe
logic. A cut generation is only observed when a temporal pass is actually
submitted, and each post-observation failure already invalidates that device's
history. Reset creates or retains an independent cursor with invalid history.

The active handler allocates no heap memory and emits no per-frame log. Its new
safety work is two `VirtualQuery` calls before an applied camera/cockpit write
(plus one only when the optional scene fix writes). After one bounded cockpit
read, other cockpit visits return before the clock and pose pipeline; they take
the diagnostic lock only to update the cockpit counters. Telemetry
adds two QPC reads and one aggregate-lock update when explicitly enabled; its
reported scope excludes the assembly stub and CPU save/restore. In-game timing
is still required before making an FPS or overhead claim. When chase is off, a
TAA resolve adds one relaxed atomic generation load and cursor comparison; it
does not allocate, lock, query memory or patch code.

## Remaining runtime checks

- First CrossOver gameplay acceptance: exact install line, active-cockpit
  predicate, back-view thresholds, FOV/boom inference, HUD and aim alignment,
  gate-jump coalescing and one TAA cut per device, vanilla transition cuts,
  menu/pause behavior, resolution/device recreation and aggregate handler time.
- Native-Windows behavior remains unverified. The implementation uses documented
  Win32 APIs and portable C++ around the allowed private game hook/structures;
  no Wine-private renderer interface is required.
- There is no chase-entry-specific hostile-FP-state execution fixture. The
  emitted object ordering was audited above, and other fixtures exercise the
  shared `PreserveCpuState` transport; an in-game run is still the first test of
  this exact generated stub and entry together.
- The accepted 8-degree clamp and the documented world-frame spring-velocity
  approximation remain unchanged by this integration.

## Final qualification

The 18-wrapper chain ran sequentially through `wine_lock.py`, with no game
launched. Source hashes remained frozen throughout. Both Steam and X3/FEX
completed full motion suites: 98 cases plus 16 benchmark processes (26 benchmark
result entries include derived comparisons). All other planned temporal,
ownership/fallback, scene, loading, exports, lifetime, cache, shader, reader,
gzip and crypto gates passed. The final DLL has SHA-256
`47f1452e09351bb306d0c5225134665aa9bf7c8cc5c46609fa67604db82027ad`.

A local evidence-driver review tightened required artifact retention, dynamic
fallback-DLL binding, report copying, terminal-state consistency and driver
revision tracking. The first two successful suites' extant artifacts were
reconciled afterward against their original runner hashes before continuing;
this did not rerun or retroactively strengthen missing runtime observations.
The final reader/gzip/crypto record controls passed 21 tests; their shell wrapper
then failed on a reserved zsh variable assignment, after unittest had reported
OK. No production test failed and no rerun was needed for that shell-only error.

The camera remains opt-in and gameplay-unverified. First-user acceptance is the
[controlled run plan](next-runs-2026-09-13.md#7-modern-chase-camera),
including a same-build vanilla baseline and per-frame camera logging in the
separate TAA diagnostic run.

Independent artifact review approved the exact candidate for commit and installation:
all 18 terminal manifests, 440 frozen sources, 41 linked objects, retained
binaries/results and bottle provenance matched. The six-case CLI control manifest
precedes the later `--camera-log 1` diagnostic addition; that updated command
has a separate dry-run and will be included in the post-install record.
