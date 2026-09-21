# Partial sun occlusion: verification ledger

Design: [sun-partial-occlusion.md](../architecture/sun-partial-occlusion.md). Sites, ABI and record
layout: [lens-flare-visibility.md](../reverse-engineering/lens-flare-visibility.md) sections 11-16.
Append outcomes here.

## 2026-09-22: step 1 implemented, not installed, not flown

Default off (`--sun-occlusion` / `X3M_SUN_OCCLUSION=1`); nothing is patched and the draw hooks pay
one flag test when neither it nor `X3M_SUN_OCCLUSION_LOG=1` is set. Bottle X3, CrossOver Preview.
No game launch. Nothing below is evidence about the picture in flight.

| Check | Command | Result |
| --- | --- | --- |
| Static sites on the installed EXE | `/usr/bin/python3 verification/probe/verify_sun_occlusion_sites.py` | 16 / 16: SHA-256, 21 context bytes per site, both rel32 targets, the probe's 148-byte prologue (FNV-1a `0x0e5f40888a926996`) and its decoded gate shape, the `xor eax,eax` exit of the rect test, the traversal's entry, callers exactly `{0x471630}` and `{0x4722b5, 0x472491, 0x47e8f6}`, no branch into either call's interior, no absolute reference, one overlapping claim (`submit_phase_sort_return_b`) |
| Host: decision, readiness, footprint, blend classes, main-view walk, pixel wrap, launcher, sites | `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_sun_occlusion` | 14 tests, 0 failing. The gate table is compared with a Python restatement of the vanilla prologue over 648 input combinations, including "outside the rect with `0x8000000` set = 0" |
| Neighbouring host modules | `... --modules test_check_no_x87 test_submit_phases test_collide_memo test_cull_census test_sun_occlusion` and `... --modules test_object_capture test_sun_share_lane` | 53 tests / 0 failing; 15 tests / 0 failing (the host doubles that include `motion_output.h` compile) |
| Hook fixture (synthetic sites with the engine's shapes) and GPU fixture (detached pass) | `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_occlusion.py` | passed: hook 52 checks / 0 failures, GPU 86 checks / 0 failures; record `verification/results/bottle-X3/sun-occlusion.json` |
| Production scratch build | `cmake -S . -B <scratch> -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DPython3_EXECUTABLE=/usr/bin/python3 && cmake --build <scratch> -j` | exit 0, 0 warnings |
| x87 audit of that DLL | `python3 verification/probe/check_no_x87.py <scratch>/d3d9.dll` | 563 reachable functions, 0 violations; five new roots (both thunks, the probe handler, begin, end) |
| Launcher | `tools/manage.py launch --dry-run --ownership --object-trace --object-lifetime --motion-output --taa --hdr --sun-occlusion --sun-occlusion-log --sun-occlusion-radius 1.5` | exit 0; env gains `X3M_SUN_OCCLUSION=1`, `X3M_SUN_OCCLUSION_LOG=1`, `X3M_SUN_OCCLUSION_RADIUS=1.5000` |

Hook fixture, what the 52 checks cover: redirect bytes; the original reached by `jmp` on the
caller's exact frame (its return address is the site + 5) with both answers; override answers
without the original running; EBX / EBP (a value) / ESI / EDI, ESP and the two stack arguments
intact after every call; two live x87 stack entries, a non-default MXCSR and LastError intact across
the probe thunk, and across the lens thunk with a listener that runs x87 code, rewrites MXCSR and
sets LastError; bracket order begin < original < end with the flag set only inside; the traversal's
argument still on the caller's stack afterwards; no bracket before the first probe; gates 1 and 3
hide, gate 2 precedes gate 3; foreign view, foreign record, second sun, failed pass, blocked,
device Reset, a frame gap and an unknown main view all reach the original; `present()` closes a
bracket left open; a second thread goes to the original and is counted; log mode runs the original
exactly once per probe and logs `vanilla=` / `answer=`; observe-only never answers; second-claim
failure restores the first site; `shutdown()` restores both; a late claim is refused with the bytes
untouched.

GPU fixture, measured on X3 (320 x 180 RT2, disc radius 0.1 u, 32 taps):

| Scene | open / valid taps (CPU twin) | raw | used |
| --- | --- | --- | --- |
| open | 32 / 32 | 1.0 | 1.0 (exact) |
| covered | 0 / 32 | 0.0 | 0.0 (exact) |
| edge through the centre | 16 / 32 | 0.5 | 0.5 |
| edge at +r/2 (area 0.8045) | 25 / 32 | 0.78125 | 0.79883 |
| edge at -r/2 (area 0.1955) | 7 / 32 | 0.21875 | 0.20068 |
| centre on the screen edge, open / covered | 16 / 16, 0 / 16 | 1.0 / 0.0 | 1.0 / 0.0 |

No valid tap: a seed gives 1, a smoothed frame keeps the history. Radius 0 (a record's first
frame) is refused untouched and the fallback radius runs. Step response at weight 0.25 from 1
towards 0: 0.75, 0.5625, 0.42188, 0.31641, 0.2373, 0.17798, 0.13342, 0.10004 (expected (3/4)^n;
the last two differ by FP16 storage), then exact 0 in under 20 frames; the rise reaches used = 1.0
exactly within 13 frames at smoothed 0.976 (the dead band absorbs the FP16 stall). A32B32G32R32F
and G32R32F RT2 accepted, A8R8G8B8 refused; a failed draw restores, invalidates, makes lens draws
`not_ready` and the next frame seeds by itself. Lens draws at fraction 0.5: ps_2_0 and ps_3_0
originals under SRCALPHA/INVSRCALPHA, SRCALPHA/ONE, ONE/ONE, ONE/INVSRCCOLOR and
ONE/INVSRCALPHA match `f x` the reference within 2.5 / 255 (10 draws), unwrapped draws match the
plain law; wraps are created once per program and blend class (no creation over five repeated
draws); refusals (blend off, other law, sRGB write, alpha test at ref 128, no pixel shader, no
hash, ps_1_1, a failed bind) change nothing. Every transaction is bracketed by a byte comparison of
RT0-3, depth, viewport, scissor, shaders, declaration / FVF, stream 0, indices, 16 + 4 textures, 13
sampler states on 16 samplers, 31 render states, two stage states and c0-c3 under hostile state.
Native Reset succeeds after `before_reset()` (six objects released, programs and wraps kept) and
the next frame seeds and matches the twin; `detach()` leaves 0 references. Program: 469 ps_3_0
slots of 512.

Not verified: anything in flight (which programs the lens draws use, whether every one wraps,
whether RT2 holds the sentinel where the sun shows, the derived radius against the visible disc,
the look of the fade); native Windows execution; the motion-route fixture DLLs were not rebuilt
here (queue owner); cost in the game (the counts are: per frame one probe handler call per probe,
one pass of roughly 110 device calls (the state normalisation of the sibling passes), per lens draw about 30 device calls; none timed).

## 2026-09-22: review fixes on merged main (f61a3bd7), still not installed, not flown

Deep review of `cb8267f9`: hook / ABI side sound; six findings fixed.

1. A refused lens draw, and the rest of its bracket, is dropped in the frame that sets the block (the
   fraction is GPU-only, so "f = 1" cannot be known); the block is sticky for the process, so a Reset
   cannot replay that frame. Transient refusals (shadow state unknown, device error) cost the frame and
   the next without blocking.
2. `classify_blend` refuses `D3DRS_FOGENABLE`; the fingerprint logs `fog=` (and `srgbwrite=`, `alphafunc=`).
3. The probe handler runs inside a LastError envelope; both thunks save EFLAGS and clear DF around the C
   handlers. The hook fixture links the production `engine_memory.cpp` and drives the production
   main-view walk (VirtualQuery refuses the unmapped engine globals) under hostile LastError / MXCSR / x87,
   and runs override path, original path and bracket at every 4-byte alignment of ESP (pads 0, 4, 8, 12)
   with DF clear and set: handlers see DF = 0, the site gets DF back.
4. A pass skipped untouched holds the last fraction for at most four consecutive frames (`core::Hold`,
   host-tested); failures and exhausted holds drop the frame as before.
5. Device calls, measured by the GPU fixture: visibility pass **33 per frame (was about 110)**, wrapped
   lens draw **5 (was about 30)**. The per-frame setters became one recorded block; `D3DSBT_ALL` is gone;
   per-draw state comes from the route's shadow and a recorded per-sampler block that also holds the
   application's shader reference. No device getter runs per lens draw.
6. One `sun_occlusion_foreign_thread` line when a probe arrives from a thread other than the fixed owner.

| Check | Result |
| --- | --- |
| `git merge main` (f61a3bd7) | clean, no conflicts |
| `/usr/bin/python3 verification/probe/run_host_suite.py --modules test_sun_occlusion` | 15 tests, 0 failing |
| `... --modules test_check_no_x87 test_submit_phases test_collide_memo test_cull_census test_object_capture test_sun_share_lane` | 7 modules, 69 tests, 0 failing |
| `X3M_FIXTURE_BOTTLE=X3 python3 verification/probe/wine_lock.py python3 verification/probe/run_sun_occlusion.py` | hook 64 checks / 0 failures, GPU 89 checks / 0 failures, `CALLS execute=33 lens_draw=5` |
| scratch build + `check_no_x87.py` | exit 0, 0 warnings; 565 reachable functions, 0 violations |

Still unverified: everything in flight, native Windows, the hold and drop paths inside `MotionOutput`
(they sit behind the route and have no fixture of their own; `core::Hold` and the pass are tested), cost
in the game.

