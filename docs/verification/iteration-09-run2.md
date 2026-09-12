# Iteration 9, run 2: engine scene-end hook, adjacency cache, sharpness

User-run session on the installed build `1d36c29` with
`--direct --ownership --object-trace --object-lifetime --motion-output --taa
--taa-debug --telemetry --profile --scene-hook --mesh-cache`; route
`hdr=0`, `rt_mode=perdraw`, `jitter_samples=8`, `state_shadow=1`.
Path: start → main menu → load save → flight with several gate jumps.
User impression: **menu and save loads did not feel faster; sector changes felt
faster; objects look slightly blurry.**

Source log `session-20260912-162050-2040.log`, SHA-256
`805abac930b13d3c74608a0976c2f0555f9dad3a713fe23bff39b8d82c8ae2ff`
(3,949,034 records, 207 MiB, untracked), with six four-frame readback bursts.
Evidence: [iteration-09-run2-summary.json](../../verification/results/iteration-09-run2-summary.json)
and its text rendering
[iteration-09-run2.txt](../../verification/results/iteration-09-run2.txt),
produced by `tools/analysis/analyze_iteration09_run2.py`
(synthetic tests: `verification/analysis/test_iteration09_run2.py`, 22 checks).
Run 1 of the same iteration is the comparison session
(`/tmp/x3-iteration09-run1/session-20260912-160404-1632.log`, same build,
`--mesh-cache` off); its loading attribution is
[loading-profile-run1](../../verification/results/loading-profile-run1/).

Reproduce with the untracked captures in place:

```
python3 tools/analysis/analyze_iteration09_run2.py \
  /tmp/x3-iteration09-run2/session-20260912-162050-2040.log \
  --output verification/results/iteration-09-run2-summary.json \
  --text verification/results/iteration-09-run2.txt \
  --baseline-log /tmp/x3-iteration09-run1/session-20260912-160404-1632.log \
  --baseline-label run1 --ideal-frames 1
python3 tools/analysis/analyze_loading_profile.py \
  /tmp/x3-iteration09-run2/session-20260912-162050-2040.log --output <dir> \
  --symbols verification/results/loading-profile-run1/symbols.json --threshold 2.0
python3 -m unittest discover -s verification/analysis -p test_iteration09_run2.py
```

The four headline results:

1. The engine scene-end hook agreed with the selector on **every** frame that
   latched a scene (89/89 `Agree`, no disagreement record) and carried **every**
   resolve; it is safe to make the default resolve point.
2. The adjacency cache produced **zero** hits and zero misses: all 8,718
   `GenerateAdjacency` calls bypassed with reason `floating_point`, because the
   game calls it with x87 precision control at 53-bit and MXCSR FTZ+DAZ set.
   The cache had no effect on any load, which is exactly what the user felt.
   The sector change that *did* get faster got faster for an unrelated reason.
3. Between 87% and 93% of the measured sharpness loss on a stationary camera is
   what an ideal 8-phase jitter supersample of the same content costs. The
   resolve adds 4-11% on top. Lowering the history weight cannot fix it.
4. Routed material stages already run `MINFILTER=ANISOTROPIC`,
   `MIPFILTER=LINEAR`, `MAXANISOTROPY=16`. The snapshot does not record
   `D3DSAMP_MIPMAPLODBIAS`, so this run carries **no** evidence of the current
   LOD bias.

---

## 1. Engine scene-end hook: safe as the default resolve point

`scene_hook active=1 status=active` is the only install record, and it is
sufficient evidence of the byte check. `scene_hook::initialize`
(`src/proxy/scene_hook.cpp`) reaches `state = "active"` only after, in order:

| Gate | Value in this run |
| --- | --- |
| `X3M_SCENE_HOOK=1` seen | yes (`requested`) |
| `object_trace::executable_verified()` (SHA-256 of X3AP.exe) | passed |
| 5 bytes at `0x004721b1` equal `e8 9a 25 05 00` | passed |
| rel32 resolves to `0x004c4750` (`site + 5 + disp`) | passed |
| `VirtualProtect` → write → `FlushInstructionCache` → restore protection | all succeeded |

Any failure leaves a different `status` (`executable_mismatch`,
`callsite_mismatch`, `target_mismatch`, `protect_failed`,
`patch_rolled_back`, `rollback_failed`), and the patch is never written on
differing bytes. The return address `0x004721b6` is unchanged, so compositing
runs exactly as before.

### Per-frame cross-check

94 `motion_output_frame` records (frame 0, then every 60th, plus the capture
bursts). `scene_end_check` (`src/proxy/motion_output.h`) distribution:

| Verdict | Frames |
| --- | --- |
| `Agree` (hook fired once in Scene, then the bloom copy, no scene draw between) | **89** |
| `HookOnly` | 0 |
| `StretchOnly` | 0 |
| `Disagree` | **0** |
| `None` (frame never latched a scene) | 5 |

* `motion_output_scene_hook_disagreement` records: **0**.
* On all 89 latched frames: `hook_signals=1`, `hook_outside_scene=0`,
  `draws_after_hook=0`, `bloom_copy_seen=1`, `taa_attempted=1`,
  `taa_resolved=1`, `taa_skip=0`.
* Resolve location: **89 at the hook, 0 at the StretchRect**
  (`scene_end_source=hook` on every latched frame). The 24 `stretch_rect`
  records are the bloom copies inside the capture bursts; the resolve had
  already run at the hook, so the fallback path was never used.

### The five `None` frames

| Frame | Draws | Latched | Signals | Outside scene | Selector state | `taa_skip` |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | 0 | no | 0 | 0 | Scene | 2 |
| 60 | 638 | no | 1 | 1 | `CameraState` | 2 |
| 120 | 638 | no | 1 | 1 | `CameraState` | 2 |
| 180 | 674 | no | 1 | 1 | `CameraState` | 2 |
| 4140 | 638 | no | 1 | 1 | `CameraState` | 2 |

All five are **menu** frames: frames 60-180 are the main menu before the save
was loaded and frame 4140 is the menu at the end of the session (638-674 draws,
`routed=0`, `camera_policy=1`, no scene latched). Frame 0 is the first frame
after device creation, before any draw.

This is the one thing to know before promoting the hook: **the patched callsite
does fire in the menu**, once per frame, and the selector is in `CameraState`
rather than `Scene` when it does (`hook_outside_scene=1`). It is harmless here
because no scene is latched, so the verdict is `None` and no resolve is
attempted - but the same signal in a frame that *had* latched would be scored
`Disagree`. The menu is therefore a state the default path has to tolerate
explicitly, not a case that happens not to arise.

### Restore at shutdown: not observed

The log contains **no** `scene_hook_shutdown` record, and no release/detach
record of any kind: the last records are an ordinary 1 Hz
`telemetry_summary`/`telemetry_metric` block and a sampling-profiler report at
350.3 s, frame 4157. The session ended without the proxy's teardown path
(`src/proxy/capture.cpp:431`) running. That is not evidence of a failed
restore - it is an absence of evidence, and the rollback remains covered only
by the probe fixture, not by this session.

### Verdict

**Yes - safe to make the default resolve point**, with one condition. 89/89
agreement, zero disagreements, zero scene draws between the hook and the bloom
copy, and every resolve taken at the hook, including the six capture bursts and
the heaviest frames of the session (857 draws, 683 routed). The conditions to
carry into the default:

* Keep the `latched` guard. The hook fires in the menu in the `CameraState`
  selector state; without the guard those frames would score `Disagree`.
* Keep the StretchRect path as the fallback and keep `scene_end_check`
  reporting, so a build where the hook does not install (different executable)
  or does not fire in the scene phase still resolves and still says so.
* Exercise the shutdown restore in a session that ends cleanly before the
  default flips; this run does not cover it.

---

## 2. Adjacency cache: 100% bypassed on `floating_point`

`X3M_MESH_CACHE=1` activated as designed - `mesh_cache ready=1
buffer_contract=public_systemmem_readonly`, both shared native mesh vtables
hooked with all three slots (`mesh_hook table=0/1 installed=1 owned_slots=3`),
`dispatch_enabled=1`, `faulted=0` - and then did nothing at all:

| Counter | Value |
| --- | --- |
| `calls` | 8,718 |
| `hits` | **0** |
| `misses` | **0** |
| `bypasses` | **8,718** (100%) |
| `admissions` / `evictions` / `retained_entries` | 0 / 0 / 0 |
| `contention` / `allocation_failures` / `acquisition_failures` | 0 / 0 / 0 |
| `native_calls` / `native_failures` | 8,718 / 0 |
| `native_ticks` | 332,192,318 → **33.22 s** (3.81 ms/call) |
| `gate_ticks` | 469,647 → 0.047 s (the cost the hook added) |

### The reason code, and why it fires

Single bypass reason, in all 101 cumulative reports:
`mesh_cache_bypass reason=floating_point count=8718`. That is
`BypassReason::FloatingPoint`, raised by `supported_fp()`
(`src/proxy/mesh_adjacency_cache.cpp:22`) before any buffer lock or hash. The
first disqualifying state was published once:

```
mesh_cache_fp_first control=0000027f status=00000000 tag=0000ffff mxcsr=00009fe0
```

Two of the four fields fail the "verified D3D9 default" profile the cache
requires:

| Field | Observed | Required | Meaning |
| --- | --- | --- | --- |
| x87 control word | `0x027f` | `0x007f` | **precision control = 53-bit double** (bits 8-9 = `10`); the profile expects 24-bit single. Rounding is round-to-nearest and all six exceptions are masked, as expected. |
| x87 tag word | `0xffff` | `0xffff` | ok (empty register stack) |
| x87 status word | `0x0000` | masked bits `0xb800` clear | ok |
| MXCSR (ignoring the six sticky flags) | `0x9fc0` | `0x1f80` | **FZ (flush-to-zero, bit 15) set and DAZ (denormals-are-zero, bit 6) set**; rounding and the exception masks are as expected. |

So the caller's FPU mode is a legitimate, common one - double-precision x87 plus
FTZ/DAZ on SSE - and the cache's gate is stricter than it needs to be for a
*read-only* hash of vertex/index bytes. The gate exists so the cache never
normalizes a caller's FP state and never returns adjacency computed under a
different rounding regime than the original would have used; but it currently
refuses every call this game makes. `rejected_fp=0` because that counter guards
the *post-call* admission check, which is never reached.

### Per-phase hit/miss/bypass and the seconds

Grouping the 101 per-window `loading_metric op=ID3DXMesh::GenerateAdjacency`
deltas by a 3 s quiet gap gives the session's adjacency phases. Hit rate is
0% and bypass rate 100% in every one of them - there is no phase-dependent
behaviour to report, because the gate fires before the key is even computed.

| Phase | Window | Calls | Adjacency s | Mean ms | Max single call |
| --- | --- | --- | --- | --- | --- |
| Main-menu load | 24.3 - 41.4 s | 1,017 | 4.214 | 4.14 | 0.660 s |
| Save load, main body | 85.8 - 116.7 s | 3,445 | 17.265 | 5.01 | 1.795 s |
| Save load, tail (one window after a 12.9 s quiet interval) | 129.6 s | 136 | 0.307 | 2.26 | 0.080 s |
| Sector change (one, at a gate) | 281.0 - 295.7 s | 3,013 | 7.784 | 2.58 | 0.610 s |
| Main-menu load (return to menu) | 334.0 - 349.2 s | 1,017 | 3.700 | 3.64 | 0.347 s |
| Scattered in flight (below the 100-call phase floor) | 135 - 315 s | 90 | 0.008 | 0.09 | - |
| **Total** | | **8,718** | **33.28** | 3.82 | |

The two save-load rows together (3,581 calls / 17.572 s) are the 3,584 calls /
17.573 s the presentation-gap detector attributes to that gap in §3; the phase
grouping splits them because 12.9 s pass with no adjacency window between.

The wrapper's own accounting (33.22 s of `native_ticks`) and the independent
`loading_metric` totals (33.28 s) agree to 0.2%, which confirms the hook sees
essentially every `GenerateAdjacency` call and that its gate cost 0.047 s over
the whole session.

Only **one** gate jump produced an adjacency phase. The user made several, so
the rest reused resident meshes and never called `GenerateAdjacency` - a case
in which a cache could not have helped either.

### Adjacency seconds against run 1 (cache off) and iteration 8

| Phase | Iteration 8 | Run 1 (cache off) | Run 2 (cache on, 100% bypassed) |
| --- | --- | --- | --- |
| Menu load | 1,017 / 3.160 s (3.11 ms) | 1,017 / **33.945 s** (33.38 ms, one 26.6 s call) | 1,017 / 4.214 s (4.14 ms) |
| Menu load (second) | 1,017 / 3.217 s (3.16 ms) | 1,017 / 3.590 s (3.53 ms) | 1,017 / 3.700 s (3.64 ms) |
| Save load | 3,584 / 11.331 s (3.16 ms) | 3,585 / 18.771 s (5.24 ms) | 3,584 / 17.573 s (4.90 ms) |
| Sector change | 3,497 / 11.154 s (3.19 ms); gap-local 2,076 / 5.188 s (2.50 ms) and 2,471 / 6.030 s (2.44 ms) | 3,022 / 12.807 s (**4.24 ms**) | 3,011 / 7.783 s (**2.58 ms**) |

Read the *mean per call*, not the totals: the call counts are fixed by content
and the cache changed nothing. Run 2's sector-change mean (2.58 ms) lands back
on iteration 8's (2.44-2.50 ms); run 1's (4.24 ms) is the outlier. Same for the
first menu load, where run 1 contains a single 26.6 s `GenerateAdjacency` call
that no other session shows and that is far larger than any plausible adjacency
computation - a one-off stall inside the call, not adjacency work.

### Why the menu and save loads did not speed up but the sector change did

* **They could not.** The cache served 0 of 8,718 calls. Every load in run 2 ran
  the original native algorithm, exactly as run 1 did. The only difference the
  feature made anywhere is +0.047 s of gate overhead.
* **The menu load is the same in both runs.** The *steady-state* menu load is
  32.88 s in run 2 and 32.83 s in run 1 (the returns to menu), with adjacency
  3.70 s and 3.59 s. Run 1's *first* menu load reads 64.06 s only because of the
  26.6 s single-call stall; discount that and both runs' menu loads are ~33 s
  with adjacency at 11% of the gap. Nothing was available to speed up.
* **The save load is the same in both runs.** 85.61 s (run 2) against 86.76 s
  (run 1); adjacency 17.57 s against 18.77 s, 21% of the gap. A 1.3% difference.
* **The sector change really was faster - by 4.6 s, and 5.0 s of that is
  adjacency - but not because of the cache.** The per-call mean fell from
  4.24 ms to 2.58 ms on the same ~3,015 calls. Because the cache served nothing,
  that is run-to-run variance in the native call itself (and run 2 matches the
  iteration-8 baseline while run 1 sits 70% above it). This report cannot
  attribute it further: candidates are machine state (warm page cache for the
  d3dx9 working set, CPU clock/thermal state) and a different destination
  sector's mesh mix. It is **not** a measured effect of `--mesh-cache`.

### What to do about it

Two exclusive options, both needing a decision rather than more measurement:

1. **Widen the FP gate to accept this state.** The gate's purpose is that the
   cache must not change the caller's FP environment and must not hand back
   adjacency computed under different rounding. Neither purpose requires 24-bit
   x87 precision or FTZ/DAZ off: the cache's own work is an integer hash of
   locked buffer bytes plus a `memcpy` of a `DWORD` array, and the epsilon is
   compared bit-exactly. Accept any state whose *rounding mode* and
   *exception masks* match and whose precision control and FTZ/DAZ are merely
   recorded, keep `admissible_fp` unchanged for the post-call check, and keep
   `supported_fp` as the guard for the states that genuinely matter. Expected
   benefit, if the keys then repeat as iteration 4 suggested: up to ~17 s of the
   save load and ~5 s of a sector change.
2. **Retire `--mesh-cache`.** On the evidence of this run its whole effect is
   0.047 s of overhead, and the load time is dominated by the unexplained
   remainder below, not by adjacency.

Either way, the flag must not become a default while it reports 100%
`floating_point` bypasses.

---

## 3. Loading gaps, run 2 against run 1

Presentation gaps over 2 s, from `tools/analysis/analyze_loading_profile.py`
with the run-1 agent's `symbols.json`. "Instrumented" is the additive exclusive
hooked time (a lower bound: concurrent threads overlap); "unexplained" is the
gap minus it.

| Run 2 | Gap | Length | Instrumented | Unexplained | Adjacency | Adjacency share |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | menu load (5.1-40.4 s) | 34.24 s | 10.06 s | 24.18 s | 4.21 s | 12.3% |
| 2 | save load (48.0-135.2 s) | 85.61 s | 36.16 s | 49.45 s | 17.57 s | 20.5% |
| 3 | sector change (274.2-294.3 s) | 19.50 s | 9.60 s | 9.90 s | 7.78 s | 39.9% |
| 4 | menu load (315.4-349.0 s) | 32.88 s | 9.03 s | 23.85 s | 3.70 s | 11.3% |

| Run 1 | Gap | Length | Instrumented | Unexplained | Adjacency | Adjacency share |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | unlabelled (4.8-9.4 s) | 2.96 s | 0.58 s | 2.39 s | 0.003 s | 0.1% |
| 2 | menu load (8.5-73.7 s) | 64.06 s | 39.81 s | 24.25 s | 33.95 s | 53.0% |
| 3 | save load (124.2-211.0 s) | 86.76 s | 41.24 s | 45.52 s | 18.77 s | 21.6% |
| 4 | sector change (383.9-409.1 s) | 24.13 s | 14.71 s | 9.42 s | 12.81 s | 53.1% |
| 5 | menu load (438.0-471.6 s) | 32.83 s | 8.91 s | 23.91 s | 3.59 s | 10.9% |

Matched phase by phase: menu load 32.88 s vs 32.83 s (the comparable pair),
save load 85.61 s vs 86.76 s, sector change 19.50 s vs 24.13 s. Only the sector
change moved, and section 2 shows why it is not the cache.

**Sector-change gaps.** Run 2 contains exactly **one** gap over 0.8 s labelled
sector change (19.50 s, 3,011 adjacency calls, 27.0 MB of texture input);
lowering the threshold to 0.8 s adds only two unlabelled 1.3-1.6 s gaps
elsewhere. The user's other gate jumps produced no gap and no adjacency work.
Run 1 likewise has one (24.13 s, 3,022 calls). So "sector changes felt faster"
rests on a single event in each session, with adjacency at 40% of the run-2 gap
and 53% of the run-1 gap.

**The unexplained remainder is where the loading time actually is**: 24.2 s of
the 34.2 s menu load, 49.5 s of the 85.6 s save load, 23.9 s of the second menu
load. The sampling profiler's largest identified main-executable leaves inside
those intervals are the collision-tree build - `FUN_004e1220` ("recursive split
body", 4.6% of main-module samples in the menu load, 2.4% in the save load),
`FUN_004e0770`, `FUN_004dfef0`, and the `sqrtf` helper `FUN_00412440` - and, in
the sector change, mesh build `FUN_004bc1c0` ("vertex positions int16 x 1/16384
and three `D3DXVec3Normalize`", 3.1% self / 5.8% inclusive of main-module
samples). Those shares are small fractions of the main-module samples, which are
themselves a small fraction of all samples, so treat them as the ranking of
identified engine leaves, not as an attribution of the gap. The run-1 agent owns
this analysis.

---

## 4. Sharpness: 87-93% of the softness is the supersampling itself

The user's "objects look slightly blurry" is the question this section answers.
Six four-frame bursts, classified from the route's own cut detector
(`cut_median_px`) and `camera_rotation_deg`:

| Burst | Peak `cut_median_px` | Max rotation | Class | Routed interior px | Routed edge px | Sentinel px |
| --- | --- | --- | --- | --- | --- | --- |
| 629-632 | 0.22 | 0.51° | **stationary** | 34,361 | 55,765 | 892,914 |
| 903-906 | 50.10 | 4.88° | turning | 4,428 | 256,577 | 722,035 |
| 1427-1430 | 0.30 | 0.45° | **stationary** | 167,472 | 62,832 | 752,736 |
| 1584-1587 | 29.37 | 2.14° | turning | 2,180 | 485,310 | 495,550 |
| 1950-1953 | 78.39 | 5.81° | turning | 4,591 | 269,270 | 709,179 |
| 3685-3688 | 8.56 | 1.20° | slow | 5,815 | 67,322 | 909,903 |

Classes are `analyze_iteration08_taa.classify_pixels`: `routed_interior` needs
valid RT2 depth, motion alpha 1 and a burst-constant depth in every frame;
everything else routed is `routed_edge`; a pixel with negative depth in every
frame is `sentinel`, which the resolve returns unfiltered by contract.

### Measured resolved/raw ratios, stationary bursts

Mean squared central-difference luma gradient, resolved over raw, same frame:

| | Burst 629-632 | Burst 1427-1430 |
| --- | --- | --- |
| routed interior | 0.488 / 0.513 / 0.521 / 0.488 (mean **0.505**) | 0.642 / 0.623 / 0.621 / 0.624 (mean **0.628**) |
| routed edge | 0.719 - 0.746 | 0.650 - 0.661 |
| sentinel | 0.745 - 0.767 | 0.715 - 0.725 |
| Laplacian energy, interior | 0.253 - 0.275 | 0.218 - 0.249 |
| energy above 1/4 Nyquist (best tile) | 0.391 - 0.419 | 0.528 - 0.559 |
| iteration-7/8 comparable mask | 0.699 / 0.725 / 0.710 / 0.700 | 0.659 / 0.639 / 0.637 / 0.640 |

The last row uses iteration 7/8's exact measurement - their mask (this frame's
motion-validity mask eroded by two), their operator, their band - so it is
directly comparable to **iteration 8's 0.64-0.75**. Run 2 reads 0.64-0.73.
**No regression: the resolve is as sharp as it was in iteration 8.**

The sentinel ratio is not 1.0 (0.71-0.77) although the resolve returns the
current colour for those pixels unconditionally. That is the 8-bit raw readback
against the FP16 resolved readback, plus the resolve's luminance weighting
round-trip - a floor on how close to 1.0 any ratio here can get, and a reminder
that ~0.75 of these numbers is measurement, not filtering.

### The ideal-supersampling baseline

Three baselines, weakest assumption first.

**(a) The real thing, no interpolation.** A stationary burst supplies four raw
frames of the same scene at four different jitter phases. Their plain mean *is*
a four-phase jitter supersample, built from real rasterized phases with no
resampling anywhere:

| | Burst 629-632 | Burst 1427-1430 |
| --- | --- | --- |
| 4-phase average, routed interior | 0.6497 | 0.6723 |
| analytic white-source factor, this 4-phase kernel | 0.7615 | 0.6869 |
| analytic white-source factor, the resolve's kernel (8 phases, geometric weights at 0.9) | 0.6651 | 0.6698 |
| scale from the 4-phase kernel to the resolve's | 0.8735 | 0.9751 |
| **ideal floor for the resolve's kernel** | **0.5675** | **0.6556** |
| measured | 0.5050 | 0.6277 |
| measured / ideal | **0.890** | **0.958** |
| share of the loss that is ideal supersampling | **87.4%** | **92.5%** |

**(b) Analytic, white source, ideal 1x1 px box prefilter.** The measured
quantity is the mean square of `(I[x+1]-I[x-1])/2`, whose response magnitude is
`|sin(2*pi*f)|`; a 1 px box has transfer `sinc(fx)sinc(fy)`. Over a white source
on the Nyquist square the double integral separates and gives
**0.6158**. So an ideal box over ±0.5 px removes 38% of the gradient energy on
its own - the same order as everything measured above, and a number that
depends on nothing but the filter.

**(c) Resampled from one frame (upper bracket).** Reconstructing the eight
phases from a single raw frame with the resolve's own Catmull-Rom filter and
combining them with the resolve's geometric weights gives 0.728 (burst 629) and
0.763 (burst 1427) on routed interior. Catmull-Rom's negative lobes partly
re-sharpen what the resample blurs, so this brackets (a) from above rather than
replacing it.

**Conclusion: 87% (burst 629) to 93% (burst 1427) of the measured gradient-energy
loss on a stationary camera is what an ideal 8-phase jitter supersample of this
content costs. The resolve adds 11% and 4% respectively.** The residual is
accounted for by the neighbourhood clip, the Catmull-Rom history tap at nonzero
fractional velocity, and the 8-bit/FP16 round-trip - not by a defect.

### Raw against raw: the raw frame is not the variable

The four raw frames of a stationary burst differ by only the jitter phase. Their
gradient energies spread by **1.8%** (all pixels) / 7.0% (routed interior) in
burst 629 and **1.0%** / 1.6% in burst 1427. The raw frame is essentially the
same picture at every phase, so the measured drop is real filtering, not "the
raw frame happened to be sharper at this phase". (For contrast, the turning
bursts spread by 8-46% - which is why a ratio measured during motion says little.)

### Edge spread / MTF

Slanted-edge construction on the strongest locally one-dimensional edges inside
the routed-interior mask, each profile aligned on its own 50% crossing before
averaging (without that alignment the mean profile is widened by the spread of
sub-pixel edge positions, not by blur):

| Burst | 10-90% rise, raw | 10-90% rise, resolved | MTF50 raw | MTF50 resolved | Edges |
| --- | --- | --- | --- | --- | --- |
| 629-632 | 0.86 - 0.90 px | 1.07 - 1.67 px | 0.68 - 0.69 c/px | 0.35 - 0.46 c/px | ~2,100 |
| 1427-1430 | 1.81 - 1.83 px | 1.93 - 2.03 px | 0.34 - 0.74 c/px | 0.27 - 0.29 c/px | 4,000 |

Burst 629 is the visible effect in one number: a **one-pixel edge becomes a
1.7-pixel edge, and MTF50 halves** (0.69 → 0.35 cycles/px). Burst 1427's
content is already soft in the raw frame (1.8 px rise) and the resolve adds
0.2 px. Read these as a raw-vs-resolved comparison on the same content, never as
an absolute MTF: the edge population, not a target, sets the scale.

### Turning bursts, and one thing worth knowing

During fast rotation the resolve **stops filtering almost everywhere**: frames
906, 1951, 1952 and 1953 read 0.9993-0.9996 in every pixel class, meaning the
resolved image equals the raw one. The per-tap depth rejection
(`rejection.x = 1e-4` absolute, 2% relative) fails at 50-78 px of displacement,
so `proven < considered` and the shader returns the current colour. That is
correct behaviour, and it means the softness the user sees is confined to the
stationary/slow case - which matches the report. It also means there is no
temporal anti-aliasing at all in those frames.

### The history weight is not the lever; the reconstruction filter barely is

White-source gradient factors for the session's own jitter table (higher is
sharper), separating the two candidate levers:

| History weight | Offsets only (what a *static* scene gets) | Catmull-Rom taps | penalty | Bilinear taps | penalty |
| --- | --- | --- | --- | --- | --- |
| 0.95 | 0.6653 | 0.5547 | 0.834 | 0.3761 | 0.565 |
| **0.90 (current)** | **0.6651** | 0.5560 | 0.836 | 0.3840 | 0.577 |
| 0.85 | 0.6658 | 0.5581 | 0.838 | 0.3934 | 0.591 |
| 0.80 | 0.6675 | 0.5614 | 0.841 | 0.4044 | 0.606 |
| 0.70 | 0.6745 | 0.5722 | 0.848 | 0.4321 | 0.641 |
| 0.50 | 0.7077 | 0.6187 | 0.874 | 0.5166 | 0.730 |

Two readings:

* **Lowering the history weight buys almost no sharpness.** 0.90 → 0.80 moves
  the static factor from 0.6651 to 0.6675 (+0.4%), while halving the effective
  sample count. Even 0.90 → 0.50 buys only +6%, at the cost of most of the
  anti-aliasing. The eight jitter offsets, not the weight, are the kernel.
* **The Catmull-Rom history tap is already the right choice** and costs 16%
  while the camera moves (bilinear would cost 42%). On a *static* camera it
  costs nothing: `resolve.hlsl` takes the `[branch] if (all(f == 0))` path and
  reads exactly one texel. So the reconstruction filter is not the cause of the
  stationary softness either.

There is no cheap knob inside the resolve. The kernel is doing what an 8-phase
jitter TAA at 1x resolution does.

---

## 5. Sampler state on routed draws: the LOD-bias baseline

From the 12,184 captured draws (9,152 with a `motion_route routed=1` record,
3,032 without), joining each draw's sampler snapshot with its route verdict.
Counts are per bound stage, not per draw.

Routed draws, 64,064 stage samples:

| MINFILTER | MAGFILTER | MIPFILTER | MAXANISOTROPY | SRGBTEXTURE | Samples | Share |
| --- | --- | --- | --- | --- | --- | --- |
| ANISOTROPIC | LINEAR | LINEAR | 16 | 0 | 51,755 | **80.8%** |
| LINEAR | LINEAR | NONE | 16 | 0 | 12,309 | **19.2%** |

Per stage (routed, 9,152 draws each):

| Stage | Configuration |
| --- | --- |
| 0 | 100% ANISOTROPIC / MIP LINEAR / aniso 16 |
| 1 | 100% ANISOTROPIC / MIP LINEAR / aniso 16 |
| 2 | 98.5% ANISOTROPIC / MIP LINEAR; 1.5% LINEAR / MIP NONE |
| 3 | 65.3% LINEAR / MIP NONE (cube textures, `type=5`); 34.7% ANISOTROPIC / MIP LINEAR |
| 4 | 67.7% LINEAR / MIP NONE; 32.3% ANISOTROPIC / MIP LINEAR |
| 5 | 100% ANISOTROPIC / MIP LINEAR / aniso 16 |
| 6 | 100% ANISOTROPIC / MIP LINEAR / aniso 16 |

Unrouted draws (21,224 stage samples) are the same two dominant rows (76.5% /
19.0%) plus small tails: 2.4% LINEAR/LINEAR/LINEAR, 1.3% POINT/POINT/NONE
(UI and blits), and 0.8% at `MAXANISOTROPY=4`.

**The log does not contain `D3DSAMP_MIPMAPLODBIAS`.** `src/proxy/capture.cpp:374`
snapshots only `MINFILTER`, `MAGFILTER`, `MIPFILTER`, `MAXANISOTROPY` and
`SRGBTEXTURE`. `D3DSAMP_MIPMAPLODBIAS` and `D3DSAMP_MAXMIPLEVEL` are absent, so
this run carries **no evidence of the game's current LOD bias**, and its absence
from the log is not proof of the 0.0 default. Adding those two states to that
loop is a one-line change and is a prerequisite for any bias work, because a
bias must be applied relative to whatever the engine set and restored exactly.

What this run *does* establish as the baseline:

* Four of the seven stages (0, 1, 5, 6) are unconditionally
  `ANISOTROPIC`/`MIP LINEAR`/`aniso 16` on routed material draws, so a bias
  applied there has a mip chain to act on and 16x anisotropy to limit the
  shimmer it would otherwise add.
* 19.2% of routed stage samples have `MIPFILTER=NONE` and no mip chain; a bias
  would do nothing for them, so any measurement of a bias change must be split
  by stage.

---

## 6. Health

| Item | Value |
| --- | --- |
| `motion_output_frame` records / latched | 94 / 89 |
| Draws / routed / matched (latched frames) | 30,697 / 22,134 / 21,656 (72.1% routed, 97.8% of routed matched) |
| Gate rejections | gate1 0, gate2 2,028, gate3 1,025, gate4 5,510, gate5 0, gate6 478 |
| `apply_failures` / `restore_failures` | **0 / 0** |
| TAA attempted / resolved / with history | 89 / **89** / 83 |
| `taa_skip` | 0 on all 89 latched frames; 2 on the 5 unlatched menu frames (no scene) |
| Camera policy | 2 (`active`) on all 89 latched; 1 on the 5 menu frames |
| `camera_reason` | 0 on all latched frames |
| `camera_cut` | **0** for the whole session; max rotation 5.81°/frame |
| Cut detector | 6 of 89 frames flagged; max median 87.35 px; max `cut_missing` 0.171 |
| Device resets | **0** (`telemetry_first_present reset_count=0`) |
| `hdr_unwind` records | 0 (HDR off in this run) |
| Render-state shadow | 199,481 of 199,484 queries served from the shadow; `rs_resyncs=0` on all 94 frames |

**Frame time against run 1** (per-window `frame_normal`, scene regime only;
`analyze_iteration08_taa.stream_scene_windows`). Both sessions are bimodal, so
the within-regime split matters more than the median:

| | Run 2 | Run 1 | Ratio |
| --- | --- | --- | --- |
| Scene windows | 151 | 200 | |
| Per-window mean, median | 43.01 ms | 36.64 ms | 1.174 |
| Per-window minimum, median | 38.54 ms | 34.71 ms | 1.110 |
| Fast-regime fraction | 0.570 | 0.655 | |
| Fast-regime mean | 34.45 ms | 33.08 ms | **1.042** |
| Slow-regime mean | 68.22 ms | 66.52 ms | **1.026** |
| `present_normal`, scene mean | 36.7 µs | 36.0 µs | 1.022 |
| `draw_backend`, scene mean | 2.8 µs | 2.7 µs | 1.034 |

The 17% difference in the raw median is mix, not cost: run 2 spent 57% of its
scene windows in the light regime against run 1's 66%. Within a regime run 2 is
2.6-4.2% slower - consistent with the mesh-cache hook's gate plus the different
route load, and not a regression worth chasing. Absolute cost is unchanged:
~29 fps in the light regime, ~15 fps in the heavy one, in both sessions.

---

## 7. Recommendations

**Scene hook.** Promote to the default resolve point, keeping the `latched`
guard, the StretchRect fallback and `scene_end_check` reporting, and exercise
the shutdown restore in a cleanly-ended session first (§1).

**Adjacency cache.** Decide between widening `supported_fp` to accept 53-bit
x87 precision and FTZ/DAZ (recording them rather than refusing) and retiring
the flag. Do not default it on while it reports 100% `floating_point`
bypasses (§2).

**Sharpness, in priority order.**

1. **Add a post-resolve contrast-adaptive sharpen.** This is the only lever
   with real headroom. The measured resolve is already at 89-96% of the ideal
   floor for its own kernel, so nothing inside the blend can recover the
   38-50% of gradient energy that the supersampling itself removes - but a
   sharpen can put it back. Size it to restore MTF50 from ~0.35 to ~0.6-0.65
   cycles/px on stationary content, which is the raw frame's value in burst 629.
   The resolve already computes the current 3x3 mean and per-channel sigma for
   the neighbourhood clip, so an RCAS/CAS-style term costs almost no extra
   fetches; clamp it with the same neighbourhood box so it cannot ring, apply it
   after `unweigh`, and gate it on the frame having actually blended
   (`total >= 0.5` and history taken) so the current-only paths - sentinel
   pixels and every fast-rotation frame - are untouched. Expected: the full
   0.35 → 0.6 c/px, and no change at all during fast motion, where the resolve
   already returns the raw frame.
2. **Do not change the history weight.** 0.90 → 0.80 buys +0.4% sharpness for
   half the effective sample count (§4). Keep 0.90.
3. **Texture LOD bias: instrument before deciding, then try -0.5.** This run
   cannot justify a value, because `D3DSAMP_MIPMAPLODBIAS` is not in the
   snapshot. Add `D3DSAMP_MIPMAPLODBIAS` and `D3DSAMP_MAXMIPLEVEL` to
   `src/proxy/capture.cpp:374` and re-read one burst first. If the engine's
   value is the 0.0 default, **-0.5** is the value to try: the theoretically
   matched bias for the 8-phase kernel is `-0.5*log2(8) = -1.5`, but that
   assumes an ideal reconstruction filter and no rejection, and this resolve
   returns the raw frame outright during motion - where a -1.5 bias would be
   pure aliasing with no accumulation to average it. -0.5 keeps the mip level
   within the range 16x anisotropy already filters, and applies to the 80.8% of
   routed stage samples that have a mip chain (stages 0, 1, 5, 6 unconditionally).
   Set it only on routed material stages with `MIPFILTER != NONE`, restore the
   engine's value on the same draw, and measure with the same burst comparison:
   the raw-frame gradient energy should rise and the *resolved* ratio should
   stay near the ideal floor. Do it after the sharpen, so the two effects are
   measured separately.
4. **Keep the Catmull-Rom history tap.** It costs 16% during motion against a
   perfect reconstruction and nothing at all on a static camera, where the
   shader's zero-fraction branch reads a single texel (§4). Bilinear would cost
   42%.
