# Review 21: engine scene-end hook, render-state shadow and the pass field

Independent review of the uncommitted tree on top of `9dbd4dd`, limited to
the engine scene-end callsite patch (`src/proxy/scene_hook.{h,cpp}`, its
wiring in `capture.{h,cpp}` and `loader.cpp`), the render-state shadow and
the `SetRenderState`/`GetRenderState` hooks, the resolve at the hook
(`motion_output.{h,cpp}`: `scene_end_hook`, `resolve_allowed`, the
cross-check), `RigidDrawKey::pass` (`motion_history.{h,cpp}`,
`motion_row_history.cpp`, `analyze_motion_history_key.py`), the two
`manage.py` options, the build files, `check_no_x87.py`, the fixture, its
runner (eight new cases) and six documents; plus a docs-only skim of
`compositor-and-glow.md` and `hdr-scene-path.md`. No game was launched; one
Wine runner at a time; result files were queried with scripts, never read
whole.

## Checklist

1. **Callsite patch safety** - `scene_hook::initialize` (`scene_hook.cpp:
   87-99`) installs only with `X3M_SCENE_HOOK=1` (exact one-character
   value), after `object_trace::executable_verified()` (the cached SHA-256 /
   size gate of review 20) and after a `ReadProcessMemory` of the five bytes
   at `0x004721b1` equal to `E8 9A 25 05 00` (`:21`; `0x004721b6 +
   0x0005259a = 0x004c4750`, checked by hand). `patch()` (`:54-85`)
   re-reads the site, requires the `E8` opcode and that the rel32 resolves
   to the expected target, `VirtualProtect(PAGE_EXECUTE_READWRITE)`, one
   `memcpy` of the five bytes (opcode unchanged, rel32 rewritten),
   `FlushInstructionCache`, protection restored; a failed flush or
   re-protect rolls the original bytes back (`patch_rolled_back`) and only a
   failed rollback leaves the patch live (`rollback_protect_failed`,
   observation 2). Ownership (`installed_`, `before_bytes`, `our_bytes`,
   `x3m_scene_end_original`) is published before the store. The store is
   not atomic, but no thread can execute the site meanwhile: the install
   runs from `load_backend` (`loader.cpp:59`, `Direct3DCreate9` under
   `INIT_ONCE`, before any device exists) or from `hook_device`
   (`capture.cpp:1134`, `CreateDevice`, only when `installed()` is false,
   i.e. after the last device was released), and the frame routine needs a
   device; the bytes `0x004721b1-b5` lie in one 16-byte line. Restore:
   `release_device` (`capture.cpp:416`) after the last device is destroyed,
   outside the capture mutex, on the releasing thread; `shutdown()`
   (`:105-121`) refuses unless the site holds our bytes or the originals
   (`shutdown_not_owned`: another patcher wrote there; the listener then
   stays set, harmless), clears the listener, same protect/flush sequence.
   Re-install: `initialize` returns `active()` while installed and `patch()`
   returns false (fixture check "a second install is refused"). Bytes
   changed after install by another patcher: our trampoline is no longer
   reached, the frame resolves at the copy and the cross-check logs
   `StretchOnly with the hook installed` as `Disagree` (`motion_output.cpp:
   1740-1750`). Trampoline (objdump of the built DLL): `pushf; pusha; call
   _x3m_scene_end_signal; popa; popf; jmp *_x3m_scene_end_original`; the
   original's `ret` returns to `0x004721b6`; ESI/EDI/EBX/EBP and the flags
   are the frame routine's (fixture markers checked). The C target realigns
   (`and $-16,%esp`, `force_align_arg_pointer` plus the project-wide
   `-mstackrealign`). Exceptions: `scene_end_signal` calls
   `scene_end_hook` (noexcept) under a `std::lock_guard`; the only throw
   source is the mutex itself and an exception would terminate at the naked
   frame, the same class as every vtable hook (observation 5). Locking:
   the listener takes the capture `recursive_mutex` (`capture.cpp:1262`),
   the same lock every device hook takes; the resolve calls the device
   through the native table only (`native<...>` throughout
   `motion_output.cpp`, `pass.call<...>` in `temporal_pass.cpp`; the grep
   for hooked `device->` calls is empty), so no hook re-enters and the
   recursive mutex would absorb it anyway; no re-entrancy guard exists to
   trip. The CPU boundary of the signal was the one concrete defect
   (finding 1).
2. **Resolve at the hook** - `scene_end_hook` (`motion_output.cpp:617-643`)
   runs only in the selector's `Scene` phase and once per frame
   (`hook_scene_end`), flushes the lazy bindings, finishes the cut verdict
   and, with TAA, reads RT0 through native `GetRenderTarget(0)` and requires
   `same(describe_surface(rt0), main_)` (`:191-194`: identity, container,
   size, format, MSAA), else skip 10 `Target`. The copy path resolves
   `source` of the bloom `StretchRect`, which the selector accepted only as
   the latched main target, so both points resolve the same surface. The
   copy-back is a native `StretchRect` into that surface inside `resolve()`,
   which completes before the trampoline jumps to `0x004c4750`, hence
   before the compositor's `GetRenderTarget(0)` + `StretchRect`. One attempt
   per frame: `resolve_allowed` (`:590-606`) returns false on a second call
   (`t.attempted`), so `before_stretch` (`:569-585`) never resolves a frame
   the hook attempted; it still marks `bloom_copy_seen` for the cross-check.
   Hook off or refused: `hook_scene_end` stays false and `before_stretch`
   behaves as at `9dbd4dd`. A signal outside `Scene` counts
   `hook_outside_scene`, ends nothing and resolves nothing; the frame falls
   back to the copy and, if latched, is logged as `Disagree` (fixture
   frame 2 proves exactly that: `source=stretchrect`, `scene_end_check=4`,
   one `motion_output_scene_hook_disagreement` line). The pass unbinds and
   restores the still-bound depth surface itself (`temporal_pass.cpp:101,
   104, 121`). Draws after the hook are refused by `scene_bound()`
   (`:1229-1234`) and counted (`draws_after_hook`), unrouted and unjittered
   (`apply_jitter` runs only after gate 2, `:1480-1484`); the fixture issues
   none, so that is by construction (observation 7).
3. **State shadow** - the eight states (`motion_output.cpp:170-172`):
   `ZENABLE`, `ZWRITEENABLE`, `ALPHATESTENABLE`, `ALPHABLENDENABLE`,
   `COLORWRITEENABLE`, `SRGBWRITEENABLE`, `COLORWRITEENABLE1`,
   `COLORWRITEENABLE2`. The hook (`capture.cpp:988-998`) stores only on
   `SUCCEEDED(hr)`; `set_render_state` ignores recorded writes
   (`shadow_.recording`, `:129-134`); `resync_shadow` (`:1158`, reached from
   `end_stateblock`, `stateblock_applied` and `after_reset`) drops the
   whole shadow; `invalidate_render_states` is called on every failed
   restoration (`undo` `:1382`, `fill_sentinel` `:965`, the resolve `:518`,
   the lazy flush `:98`). The route's own writes (`COLORWRITEENABLE1/2 = 15`
   around a routed draw, `:34,39,47,53`; the fill's and the pass's touched
   states, `:906,932`) go through the native slot, bypass the hook and are
   restored to the saved application values, so the shadow consistently
   holds the application's state; the lazy-mode flush before an application
   write to a held mask (`before_set_render_state`, `:125-128`) lands the
   write on the application's bindings and the shadow then takes it. Shadow
   off, per-draw mode: slot 57 is not hooked (`capture.cpp:1157`),
   `render_state()` is the native getter with no fill (`:139-149`),
   `flush_bindings<false>` is the old flush with the same native calls,
   counters and metric, `restore_bindings` adds a no-op `record_deferred`,
   and `before_stretch` adds only the `bloom_copy_seen` counter: the code
   paths are those of `9dbd4dd` plus counters, and the six shadow-off twins
   are identical in colour, readbacks and route decisions. Shadow off, lazy
   mode: slots 57/58 are hooked by design to close the lazy hole
   (observation 6). `get_render_state` (`capture.cpp:1000-1008`) restores
   the bindings and forwards natively; nothing answers the application from
   the shadow (the shadow is read only by `render_state()`, a private route
   query).
4. **Pass field** - `pass` is the last field of both `fields()` tuples
   (`motion_history.cpp:16`, `motion_row_history.cpp:14`), set to
   `PassMainScene` after gate 2 (`motion_output.cpp:1521`), required
   non-zero by `MotionRowHistory::key_valid` (`:37`), left 0 on the
   replay-side `MotionHistory` on purpose (header comment; the
   `motion_history.cpp` host fixture keys it that way). A constant field
   changes no match: 10,261 / 10,348 / 14,906 matched pixels in the
   summary, unchanged from before the field. The analyzer appends
   `render_pass = 1` to K1 (K2/K2b inherit), the 33 analyzer tests are among
   the 528 green, the `motion_route` line carries `pass=`, and the three
   log parsers use dict lookups (`fields.get`), so the new field is inert
   for them. Values documented in `motion-history-key.md` and the header.
5. **check_no_x87** - `set_render_state` is the seventh root (`check_no_x87.
   py:25`); PASS, 129 reachable functions, 0 violations, as
   `motion-output.md` states. The trampoline's signal path is not among the
   roots and, walked experimentally with the same script, is *not* x87-free
   (finding 1): the light contract cannot be claimed for it, so the fix
   gives it the full boundary instead of a root entry.
6. **manage.py** - `--scene-hook` and `--state-shadow off` without
   `--motion-output` are rejected with the documented messages; `--dry-run
   --motion-output --scene-hook --state-shadow off` prints
   `X3M_SCENE_HOOK=1`, `X3M_STATE_SHADOW=0`; the defaults print `0` / `1`;
   `capture.cpp:1233-1234` parses both as exact one-character values.
7. **Report vs results** - from `motion-output-summary.json`: hook script
   134 / 119 checks, 14,906 matched pixels, changed pixels 211 / 186 / 299
   (frames 1-3, both runs) and 314 / 324 (frames 4-5, patched only),
   `scene_end_check` 1,1,4,1,2,2,1 / 3,3,3,3,0,0,3, one disagreement;
   shadow table 169 queries / 295 native off / 144 native on / 151 hits / 3
   resyncs (regular), 423 / 549 / 126 / 423 / 0 (burst per-draw), 395 /
   521 / 126 / 395 / 0 (burst lazy); bench 0.355 / 0.736 ms at 1280x768.
   All as documented. 43 cases plus 4 bench runs are the "47 DLL runs".

Docs-only skim: `compositor-and-glow.md` and `hdr-scene-path.md` contain no
decompiler output (grep for Ghidra variable/parameter patterns: 0 hits
beyond one `FUN_004b6f60` label). The former carries three short annotated
instruction listings (7 + 9 + 9 lines, address, mnemonic, comment) and the
registry-key layout; the latter formulas, matrices and pipeline steps only
(observation 8).

## Findings and fixes

1. **Medium, fixed** - `x3m_scene_end_signal` (`scene_hook.cpp:32-40`)
   wrapped the listener in a light boundary (MXCSR, x87 control word, last
   error) on the stated grounds that "the DLL's own arithmetic is SSE2".
   Walking the built DLL from the signal with `check_no_x87.py`'s own
   walker reaches x87 code on the listener's path: `fildll`/`fstpl` in
   `telemetry::record` and `telemetry::summary` (int64-to-double, which
   i386 SSE2 cannot do), the CRT `fabs` (`fldl; fabs; fstpl`) called by
   `camera_far_plane_reprojection`, an `fstpl` in `MotionOutput::resolve`
   and `resolve_allowed`, and the static mingw formatter (`__pformat_float`,
   `__gdtoa`, `fldt`/`fstpt`) behind every `%f` log line - exactly the case
   `cpu_state.h:47-58` names as excluded from the light contract ("no
   logging, since the printf formatter is x87 code"). At the copy point the
   same resolve ran under `CpuCallBoundary` (`stretch_rect`,
   `capture.cpp`). The use is balanced and the stack is empty at the cdecl
   site, so the residue was the x87 status word, not a crash, but the
   documented contract was wrong and the resolve had lost the boundary
   every other heavy path has. Fixed: the signal now holds a
   `PreserveCpuState` (`cpu_state.h`: FNSAVE/FRSTOR, MXCSR, last error),
   once per frame; verified in the rebuilt DLL (`fnsave`/`frstor` around
   the indirect call); the header comment, `live-motion-route.md` and
   `camera-state-and-frame-routine.md` updated. The light-hook checker is
   unchanged (PASS 129).
2. **Low, open** - `patch()` on `rollback_protect_failed` leaves our bytes
   live with the page execute-read-write, `installed_` true and `active()`
   false; `configure_scene_hook(false)` then tells the route the hook is
   absent while signals keep arriving. The route handles them (the verdict
   is Agree/HookOnly, not Disagree), so this is a status inconsistency in
   an unreachable-in-practice branch (a second `VirtualProtect` on a page
   the first one succeeded on).
3. **Low, open** - after a refused install (`executable_mismatch`,
   `callsite_mismatch`) `requested_` is true and `installed_` false, so
   every later `CreateDevice` re-runs `initialize` (cached gate, one
   five-byte read) and logs `reinstalled=1 active=0`. Cosmetic.
4. **Observation** - `-Werror` is set in the fixture build
   (`build_motion_output.sh:10`) only; the CMake production build has
   `-Wall -Wextra` (`CMakeLists.txt:27`, `CMAKE_CXX_FLAGS` empty). The
   clean rebuild here emitted 0 warnings.
5. **Observation** - the RAII guard in the signal makes GCC register an
   SjLj unwind context per call (`__Unwind_SjLj_Register`), as every hook
   with a boundary object already does; a listener exception would still
   terminate at the naked frame. `scene_end_signal` could be declared
   `noexcept` to state that; not changed.
6. **Observation** - lazy RT mode with the shadow off is intentionally not
   bit-identical to `9dbd4dd`: the `SetRenderState`/`GetRenderState` hooks
   are installed in lazy mode regardless of the shadow to close the write
   mask hole (`capture.cpp:1157-1159`); the burst lazy twins prove
   on/off equality within the new build, and the burst script's mask
   read-back (`mask == 7`) proves the hole is closed.
7. **Observation** - no fixture frame issues a draw between the hook and
   the bloom copy (`draws_after_hook=0` everywhere), so the "compositing
   draws are neither routed nor jittered" claim rests on `scene_bound()`
   alone. If the game's compositor draws before its depth unbind, every
   glow-on frame will report `scene_end_check=4` with `draws_after_hook>0`
   while still resolving at the hook; read that counter on the first
   `--scene-hook` gameplay log before treating `Disagree` as a fault.
8. **Observation** - the annotated instruction listings in
   `compositor-and-glow.md` are the first of that form in the tracked
   documents (the existing notes use tables of addresses); 25 instructions
   with comments, no decompiler text. Within the recorded permission for
   documenting disassembly findings.

## Results after the fixes

| Suite | Result |
| --- | --- |
| `cmake --build build --clean-first -j4` | OK (RelWithDebInfo, `-Wall -Wextra`, 0 warnings) before the review; incremental rebuild after finding 1 (only `scene_hook.cpp`), 0 warnings; `run_motion_output.py` then relinked with `--clean-first` (the hash below is that DLL; no later runner relinked it) |
| `python3 -m unittest discover -s verification/analysis` | 528 tests OK |
| `check_no_x87.py build/d3d9.dll` | PASS: 129 reachable functions, 0 violations (before and after finding 1) |
| `run_motion_output.py` (full) | PASS: 47 runs (43 cases + 4 bench), all exit 0; `seam-taa-hook-on` 134 checks (`active`, sources hook/hook/stretchrect/hook/hook/hook/hook, `scene_end_check` 1,1,4,1,2,2,1, one disagreement line at frame 2, changed pixels 0/211/186/299/314/324/281), `seam-taa-hook-unpatched` 119 (`callsite_mismatch`, glow-off frames skipped), frames 0-3 bit-identical between the two; all six shadow-off twins identical to their shadow-on runs (169 queries / 295 native off / 144 native on / 151 hits / 3 resyncs on the regular script); `seam-taa-on` 152, `seam-taa-camera-on` 165, `seam-taa-envmap` 62 (rejected 1, 3); bench medians 0.361 / 0.723 ms at 1280x768 and 0.611 / 2.250 ms at 5120x1440 (resolve deltas 0.362 / 1.639 ms; the 5120 off-side median is 0.12 ms above the author's 0.486, within the spread of these EVENT-synchronized samples) |
| `run_temporal_pass.py` | PASS: 416 numerical / 164 state checks, 2 generations, 386 samples; camera drift (px) static 0.069, yaw 0.124, pitch 0.176, yaw unjittered 0.062, narrow 0.026, single step 0.033 / 0.029, identity control 0.862, swapped control 1.051, after the cut 0.100 (the review-20 numbers) |
| `temporal_run.py` | PASS: 78/78 sample checks, reset passed, 2 device generations, sources and executable unchanged during the run |
| `run_ownership_integration.py` | PASS: 26 runs exit 0, build report `PASS`, every per-mode smoke/capture/lifetime/auto-depth/fallback report 0 failures (contracts 370 checks) |
| `run_scene_capture.py` | PASS: 4,908 checks, 16 samples, 36 scenarios, sources and executable unchanged during the run |
| `manage.py --dry-run` | two rejections and the accepted form as in checklist 6 |

Final `build/d3d9.dll` SHA-256:
`9cd7d7d85cac213edda739611f3f477e621d0b9513776fd44d403ca8f7545993`.

Verdict: go for the checkpoint commit of the scene-end hook, the render-state
shadow and the pass field, with finding 1 applied (the signal's full CPU
boundary) and `--scene-hook` left off by default as the documents say. The
first game evidence is a user-run flight session with `--taa --telemetry
--scene-hook`: read the `scene_end_check` distribution and
`draws_after_hook` on the frame lines (observation 7) and any
`motion_output_scene_hook_disagreement` / `scene_hook active=0` lines
before treating the engine boundary as confirmed; a `shutdown_not_owned`
or `rollback_*` status would mean another patcher shares the site.
