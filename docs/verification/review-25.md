# Review 25: gz read-ahead buffer, direct engine reads, exact-match adjacency, fixture bottle switch

Independent review of the uncommitted working tree on top of `ca0d234`
(`git diff` + `git status`; `.claude/` and worktrees ignored). Four items:
(1) the exact-equality `GenerateAdjacency` service and its performance pass
(`src/proxy/mesh_adjacency_fast.{h,cpp}`, the cache's FP-state gate in
`mesh_adjacency_cache.{h,cpp}`, the services in `loading_trace.cpp`, the three
mesh fixtures and runners); (2) validated direct engine reads
(`src/proxy/engine_memory.{h,cpp}`, built with `-mno-sse -mno-mmx
-mfpmath=387`, the two object observers, `X3M_TELEMETRY_DRAW`); (3) the gz
read-ahead buffer (`src/proxy/gz_buffer.{h,cpp}`, the four new import rows and
the telemetry-off gate in `loading_trace.{h,cpp}` / `capture.cpp`, `manage.py
--gz-buffer`, the fixture against the bottle's real `zlib1.dll`); (4) the
fixture bottle switch (`verification/probe/bottle.py`, `wine_lock.py`, the
profiler runner's idle wait, `.gitignore`, `docs/verification/bottles.md`).
Read in full: the three new/changed source units, the `loading_trace.cpp`
diff and its adjacency services, the four handoff notes, the gz and
mesh-adjacency documents, the runners, the fixture's comparison core. Nothing
was committed; no game was launched; every Wine command ran under
`wine_lock.py`, one at a time; result files were queried by script, never read
whole. Ghidra was not rerun: the retained decompile under
`/tmp/x3-gz-study/decompile.txt` shows `FUN_004e9210` issuing exactly one
`gzread(handle, buffer, size * count)` with no loop, which is what the buffer's
model assumes.

## Checklist

1. **gz buffer against zlib 1.2.3** - The model is `R = base + fill` (real
   stream) and `L = base + pos` (caller), with the real error made visible
   only when `L` reaches `R` (`gz_buffer.cpp:9-16`). Checked against gzio.c of
   1.2.3: `gzread` partial-then-`-1` (`buffered_read` returns `done` when the
   real call fails after some bytes, `-1` only for none, `:79`; the error is
   sticky through `s.error`), `gzgetc` = 1-byte `gzread` (`:179-186`),
   `gztell` = `gzseek(0, SEEK_CUR)` (`-1` while the error is visible, else
   `base + pos`, `:187-193`), forward `SEEK_SET`/`SEEK_CUR` beyond `R` drops the
   buffer and lets the real seek read-and-discard (`:104-106`; a seek that runs
   into the end returns `-1` and `resync` re-reads the position, `:60-65`),
   backward seek rewinds and re-inflates in zlib (the real `gzseek(SEEK_SET)`
   after `drop`; when the real stream already holds a not-yet-visible error the
   buffer calls `gzrewind` first, which is what zlib's own backward seek does,
   `:103`), `SEEK_END` refused by zlib (the buffer realigns the real stream to
   `L` and passes the call through, `:108-119`), the `n = 0` read (`-1` only
   in the visible error state, matching 1.2.3's order of checks, `:79-81`), no
   `(int)len < 0` check (that is 1.2.4's; the fixture's `0xFFFFFFFF` request
   goes down the direct path and matches the raw DLL), transparent files
   (served like any other; the 4 GB request on one is excluded for the
   documented Wine msvcrt reason, below zlib). Every one of these is exercised
   by `gz_buffer_fixture.cpp` against the bottle's `zlib1.dll` on two handles
   of the same file (`op_read/op_getc/op_tell/op_seek` compare return values
   and bytes, `:63-90`), including truncated, bad-CRC, mid-stream-corrupt,
   two-member and non-gzip inputs, and the recorded run (bottle X3) has
   735,871 checks, 0 failures over 20 cases (804,135 buffered calls, 12,924
   chunk reads, 24,116 seeks of which 1,311 in place, 9 files closed in the
   error state). Against the game's use (`savegame-gz-stream.md`): the
   savegame path is `gzopen("rb")` → 13.9 M `gzread` → `gzclose`, nothing
   else; `gzgetc`/`gztell`/`gzseek` reach the dispatchers only through the
   archive member reader on CAT-slice or plain handles, so the seek/tell code
   is exactness insurance, not a hot path.
2. **Thread safety, handle reuse, gate, size parsing** - The slot table is
   32 `{atomic<void*>, State}` entries; lookup is lock-free (hint + scan of
   acquire loads, `:38-44`), registration/unregistration take the SRW lock
   (`:160-168`, `:203-207`), the per-handle `State` is touched only by the
   handle's own caller (zlib's `gzFile` is single-threaded by contract, so two
   loader threads on two files never share a `State`). Handle reuse after
   `gzclose`: `close` clears the slot before the real close (`:205-209`), so a
   reallocated `gzFile` at the same address registers fresh; the one way to a
   stale slot was a close that bypassed the patched import (not reachable
   through the documented main-module IAT, but finding 1 makes `open` reset
   such a slot in place instead of registering a shadowed duplicate). The
   telemetry-off gate: `capture.cpp:1312` initializes the machinery when
   either `X3M_TELEMETRY=1` or `X3M_GZ_BUFFER=1`; `install()` resolves every
   row's slot as before, binds the buffer's real functions (the traced
   wrappers when telemetry is on, the raw imports otherwise, `:986-992`),
   requires all six gz rows to be present (`gz_buffer_row`: every `zlib1.dll`
   row except `inflate` and `gzwrite`), and in the buffer-only branch nulls
   every other row's slot and patches only those six (`:1004-1013`; the mesh
   vtable hooks, cache, adjacency services, file/find rows and `inflate` stay
   untouched, `installed` reflects the six, the periodic `report()` is only
   called from the telemetry summary). `gz_buffer_active` is set before any
   patch and never cleared (`:410`); with the buffer off the six entry points
   are the traced wrappers of before (same timing). Size: `X3M_GZ_BUFFER_KB`
   clamped to 1..65536 KB, default 256 (finding 2 for the sign case);
   `manage.py` exports `X3M_GZ_BUFFER=0/1` and the KB always, rejects
   `--gz-buffer-kb` without `--gz-buffer` and outside the range. Reset/unload:
   nothing in the buffer depends on the device; `loading_trace::shutdown()`
   restores the six rows without draining open handles, which is process
   teardown only (observation 8). No-x87: `check_no_x87.py` roots
   `gz_read`/`gz_getc`/`gz_tell`/`gz_seek` next to the seven light hooks (11
   roots, 144 reachable functions, 0 violations on the reviewed build); the
   rebuilt `gz_buffer.cpp.obj` has XMM references only in `statistics`,
   `open`, `initialize`, `close`, `requested*` (integer moves for the struct
   copies), none in `lookup`/`buffered_read`/`read`/`getc`/`tell`/`seek`, and
   its only x87 opcodes are the `fnsave`/`frstor` pair of `PreserveCpuState`
   around the close-time log line.
3. **engine_memory.cpp** - The unit has no floating-point arithmetic (read
   in full: pointer/size arithmetic, `DWORD` ticks, a 32-bit epoch), so
   `-mfpmath=387` is inert; `objdump` of the rebuilt object: 0 `%xmm`/`%mm`/
   `%st` references, 0 `f*` mnemonics, one `rep movsb`; undefined symbols are
   the seven Win32 imports plus the SjLj unwinder, no `memcpy`. 32-bit epoch
   wrap: a cached region is trusted when `hit->frame == frame` *and*
   `tick - hit->tick <= 100 ms` (`:62`, unsigned subtraction, so the
   `GetTickCount` wrap is harmless too); a wrap of the epoch would need a
   region untouched for 2^32 frames yet touched within 100 ms, which cannot
   happen, so the header's "wraps are harmless" holds. Invalidation policy:
   first touch per frame re-queries (`next_frame()` from
   `MotionOutput::begin_frame`, `motion_output.cpp:1385`), or after 100 ms
   against a tick sampled every 64 reads or at `next_frame` (`:105`,
   `:113`); fresh regions evict overlapping stale entries and fill the first
   empty slot or the round-robin victim (`:70-76`). Residual window: as the
   header says, a page committed at validation and decommitted before the
   `rep movsb` - within a frame the only cross-thread free of a dereferenced
   pointer is a registry rehash, which the lifetime observer's in-flight guard
   rejects before the read; the window is that check-to-read interval and
   needs a bucket array large enough for the heap to decommit. `rpm`
   everywhere: `object_trace.cpp:42` and `object_lifetime.cpp:65` are the only
   engine reads in the two observers and both go through `engine_memory::read`,
   which branches on `mode()` (`configure()` reads `X3M_ENGINE_READS` once;
   exactly `rpm` selects `ReadProcessMemory`, `:83-87`); `scene_hook.cpp`'s
   own `ReadProcessMemory` is outside this change, as `route-cost-run1.md`
   says. Spinlock: held only across the region check (no callbacks inside), so
   no re-entrancy on the observer's mutation path; the copy runs outside it.
4. **mesh_adjacency_fast.cpp** - Thread-local arena: `thread_local Arena
   arena` (`:18`), one `malloc` per call only when the layout grows
   (`:96-99`), retained up to 16 MB, larger arenas released after the call
   (`Retain`, `:100`), `release_scratch()` exported but called by nothing in
   production. The callers are whichever game threads call
   `ID3DXMesh::GenerateAdjacency` through the vtable hook (the loading
   threads); the arena for the 99,458-face grid is 12.6 MB, so a persistent
   loader thread keeps up to 16 MB for the process lifetime (observation 4:
   design, left to the orchestrator). Thread exit runs the `Arena` destructor
   through libstdc++'s `__cxa_thread_atexit` (emutls; the object's undefined
   symbols are `malloc`, `free`, `memset`, `__emutls_get_address`,
   `__cxa_thread_atexit`), which is plausible under MinGW's winpthreads TLS
   callback but was not observed in the game. MXCSR: both services capture
   the caller's x87 environment and MXCSR, load `0x1f80`, and restore on every
   exit path - fast: fault, computed and fallback (`loading_trace.cpp:262-270`;
   the native fallback runs *after* the restore, so D3DX sees the caller's
   state); verify: native first under the caller's state, then capture, pin,
   and restore on the failed-native, fault, no-scratch and normal paths
   (`:271-303`). The module itself contains no x87 opcode (rebuilt object: 0
   `f*` mnemonics; `sqrtss` and SSE arithmetic only), so a pending unmasked
   x87 exception in the caller's state cannot fire inside it. Gate: NaN/Inf
   coordinates refused by exponent test (`:110`), epsilon must be finite,
   non-negative, with a normal square (`:85-87`); quantized inputs pass when
   every representative lies on the `2^(E+2)` grid (> 2·ε for ε = 1.m·2^E);
   otherwise the 27-cell scan at cell `4ε` refuses any distinct pair with
   squared distance `<= (2ε)^2` (`:147-186`), which is conservative for both a
   Euclidean and a per-component D3DX test and rules out welding chains.
   Verify mode: the native result first, then the module into a `HeapAlloc`
   scratch, entry-by-entry compare, counters `verify_meshes/equal/mismatched/
   entries/mismatch_entries`, one `mesh_adjacency verify ... equal=0` line per
   mismatching mesh bounded by `adjacency_mismatch_line_limit = 64`
   (`:284-296`); the fixture drives 37 cases through it (35 computable, 35
   byte-identical, 0 mismatching entries; `verify_mismatched=0`). Cache FP
   gate: `supported_fp` now requires masked x87 and SSE exceptions, an empty
   x87 stack with TOP zero (`mesh_adjacency_cache.cpp:22-29`); precision,
   rounding, FTZ/DAZ are keyed (`admissible_fp` compares control, tag and
   MXCSR minus the flag bits), so the game's `0x027f`/`0x9fc0` is keyed and
   reused (the hook fixture's `GAME_FP_STATE` case). The unproven corner
   stays: near-tie normal selection (the module's `float` dot products under
   SSE against D3DX's x87 arithmetic) has no fixture that constructs an exact
   tie on real content; **it remains the game-run acceptance item**
   (`--telemetry --mesh-adjacency verify`, `verify_mismatched=0` in the last
   `mesh_adjacency_metric` line before `fast` is used).
5. **Runner and bottle changes** - `bottle.py`: default `Steam`, `X3M_FIXTURE_BOTTLE`
   overrides, `results_dir` routes non-Steam records to
   `verification/results/bottle-<name>/`, `describe()` records WineArch and
   the two emulation lines from `cxbottle.conf` (finding 3 for the empty
   value). `run_sampling_profiler.py::wait_for_idle_wine` skips rows whose
   text contains `wine_lock.py`: the wrapper's own row carries the wrapped
   command, but the wrapped runner or `wine ... fixture` is a separate `pgrep`
   row without that text and is still counted, so a running fixture is not
   skipped - only a wrapper still waiting for the lock. `wine_lock.py`:
   `flock(LOCK_EX)` on `/tmp/x3-wine-runner.lock` opened `0o666` (umask
   applies; single user), holder text `pid time label` written after
   acquisition and truncated on release (a SIGKILLed holder leaves stale text
   but the kernel drops its lock, and the next holder overwrites it), a note
   every 30 s while waiting, exit status = the command's, 75 on `--timeout`,
   130 on Ctrl-C (the child is killed by `subprocess.call`'s cleanup).
   `.gitignore`: `verification/results/bottle-*/*-capture.log` (the
   6.5 MB motion-output captures) and `.claude/worktrees/`; the remaining
   untracked `bottle-X3/` files top out at 386 KB (`motion-output-summary.json`),
   comparable to the tracked Steam records.
6. **Performance pass** - gz read path: `lookup` (one relaxed load, one
   acquire load on a hit) then `buffered_read` (four counter increments, a
   byte loop for `n <= 16`, `memcpy` above); no allocation, no lock, no log,
   no QPC; the per-chunk cost is one real `gzread` and one real `gztell`
   (`probe`). Fixture: 42.2 ns per 3-byte call buffered against 32.9 ns raw
   and 1,267.6 ns inside the hook envelope (FEX bottle) - the buffer's value
   is removing 13.9 M envelopes from a *telemetry* run, not speeding up the
   plain game, and `gz-buffer.md` says exactly that. Engine reads per routed
   draw: one atomic load, a spinlock, a scan of at most 32 regions per piece
   of the span, `rep movsb`; `GetTickCount` every 64 reads; no allocation.
   Adjacency per mesh: one retained arena (allocation only on growth), two
   `malloc`s for the normal cache only on a multi-candidate chain, two QPC
   stamps; verify mode adds a `HeapAlloc` scratch (diagnostic mode). Host
   before/after numbers in `mesh-adjacency-fast.md` reproduce from
   `mesh-adjacency-fast-host-benchmark.txt` (checksums equal, 1.10-1.25x on
   the timing meshes, 2.0x on the fan).
7. **Docs** - Every number in `gz-buffer.md` matches
   `bottle-X3/gz-buffer-summary.json` (735,871 / 0 / 20 cases; 32.9 /
   1,267.6 / 42.2 ns); the "≈ 16 s of the instrumented stall" is an
   extrapolation and is labelled as one; the in-game gate path is stated as
   unverified. `mesh-adjacency-fast.md` matches
   `mesh-adjacency-fast-suite-2026-09-12.json` (85 / 123 / 2179 / 2219; 767;
   1,714 / 2,003 / 2,011 / 2,189 / 2,673 / 2,681 = 13,271; grid 1.25 s vs
   11.3 / 11.5 ms = 111x) and the host benchmark. `route-cost-run1.md`,
   `object-identity.md`, `object-lifetimes.md`, `live-motion-route.md` carry
   the handoff's numbers (166 / 120,017; 3.36 vs 1.31 µs route, 7.75 vs 2.17
   capture, 7.01 vs 0.69 lifetime; 0.0040 queries per call). `bottles.md`
   records the X3 runs with the two FEX limitations and does not alter any
   expectation. No gameplay claim beyond fixture evidence anywhere.
   `docs/status.md` was not edited (what it must say is listed at the end).

## Findings and fixes

1. **Low, fixed** - `gz_buffer.cpp` `open()`: a `gzFile` pointer that was
   still registered (possible only if a `gzclose` bypassed the patched
   import, after which zlib reuses the allocation) registered a second slot
   for the same pointer; `lookup` scans in slot order, so the stale slot could
   answer with its old buffer contents and position. Fixed: the registration
   scan first looks for a slot already holding the pointer and resets it in
   place (old buffer freed, `registered` unchanged). Not reachable through the
   documented IAT; defensive. Recorded in `gz-buffer.md`.
2. **Low, fixed** - `requested_capacity()` parsed `X3M_GZ_BUFFER_KB` with
   `wcstoul`, which accepts a sign and leading blanks: `-5` became
   `ULONG_MAX - 4`, clamped to 65,536 KB, i.e. 64 MB per open read handle
   instead of the 256 KB default (`manage.py` validates its own flag; the
   variable can be set by hand). Fixed: decimal digits only, anything else is
   the default. Recorded in `gz-buffer.md`.
3. **Low, fixed** - `bottle.py`: `X3M_FIXTURE_BOTTLE=` (set but empty) selected
   a bottle named `''` (`wine --bottle ''`, records under `bottle-/`). Fixed:
   an empty value is the default.
4. **Observation (open, design)** - the adjacency module's thread-local
   arena keeps up to 16 MB per thread that ever ran the service; nothing in
   production calls `release_scratch()`, and thread-exit release relies on
   libstdc++'s `__cxa_thread_atexit` under MinGW (not observed in the game).
   For a diagnostic, off-by-default switch on a loading thread this is
   acceptable; if `fast` becomes a default, a release at the end of a loading
   burst (or a smaller retention limit) is the orchestrator's call.
5. **Observation (open, acceptance)** - near-tie normal selection under SSE
   against D3DX's x87 remains unproven by fixture; the game run with
   `--telemetry --mesh-adjacency verify` and `verify_mismatched=0` is the
   acceptance test, as the handoff and `mesh-adjacency-fast.md` state.
6. **Observation (open, acceptance)** - the buffer-only gate path
   (`X3M_GZ_BUFFER=1` without telemetry) has no fixture evidence: the
   loading-trace stub codec exports none of `gzgetc`/`gztell`/`gzclose`, so
   the suite records the four rows as `installed=0`. The first user-run load
   with `--gz-buffer` must show `gz_buffer requested=1 enabled=1 ... imports=1
   rewind=1`, six `loading_hook ... installed=1` lines, a `loading_trace ...
   scope=gz_buffer` line and a `gz_buffer_file` line per savegame at close.
7. **Observation** - the 100 ms staleness bound of the region cache is
   measured against a tick sampled every 64 reads (or at `next_frame`), so
   outside the route it is "100 ms plus up to 63 reads", microseconds in
   practice; the header's "~100 ms" is accurate.
8. **Observation** - `loading_trace::shutdown()` restores the six gz rows
   without draining registered handles; a `gzread` after teardown would go
   to the real stream at `R` while the caller is at `L`. Teardown is process
   exit only, so not reachable; note it if the hooks ever become
   re-installable.
9. **Observation** - the `SEEK_END`/other-whence branch realigns the real
   stream to `L` (a rewind and re-inflate when `L < R`) before passing through
   a call that zlib refuses without moving; exact but wasteful. Not on the
   game's gz path (the archive reader's handles are CAT-slice or plain).
10. **Observation** - the corner `gz-buffer.md` documents (a data error
    detected by the unbuffered stream only on the next call when the bad input
    straddles zlib's 16 KB input buffer) is real and unobservable on a valid
    file; the fixture's corrupt cases pass because their corruption is
    detected within the call.

## Suite results

Clean rebuild first (`cmake --build build --clean-first -j4`, README
command): `build/d3d9.dll` `4c539fe6…` before the chain; `check_no_x87.py`
on it PASS, 11 roots, 144 reachable, 0 violations. Then the chain, one Wine
runner at a time under `wine_lock.py --holder review25`, bottle `Steam`:

| suite | result |
| --- | --- |
| `run_motion_output.py` | PASS: 90 runs exit 0, 78 cases, `{"passed": true}`; the runner's own `--clean-first` rebuild produced the final DLL below (20 production cases on `38562f3a…`, 58 seam cases on the seam build `71796089…`); 3 min wall |
| `run_temporal_pass.py` | PASS: 386 samples, 204 state restorations, 2 device generations |
| `temporal_run.py` | PASS on the first attempt (61 s wall, inside its fixed 60 s fixture timeout; no rerun needed): sources and executable unchanged after the run |
| `run_ownership_integration.py` | PASS: 26 runs exit 0, one DLL `d1be9fd1…` (`build-ownership`), build and verification manifests written |
| `run_scene_capture.py` | PASS: 4,908 checks, 16 samples, 36 scenarios |
| `run_loading_trace.py` | PASS: loading-trace 85, loading-mesh 123, mesh-adjacency-cache-off 2,179, cache-on 2,219; verify `calls=37 computed=35 verify_meshes=35 verify_equal=35 verify_mismatched=0 mismatch_entries=0 quantized=33 unquantized=2` (`non_finite` 1, `epsilon_neighbour` 1); fast `calls=74 computed=70 fallbacks=4 faults=0` |
| `run_mesh_adjacency_cache.py` | PASS: 767 checks |
| `run_mesh_cache_hook.py` | PASS: 1,714 / 2,003 / 2,011 / 2,189 / 2,673 / 2,681 (13,271) |
| `run_object_lifetime.py` | PASS: 574 checks / 80 backend calls; identity `853bfaca11e07f83` equal in both modes; `current` 6.894 µs rpm vs 0.697 direct (12 reads, 0.0237 `VirtualQuery` per call) |
| `run_object_trace.py` | PASS: 166 checks / 120,017 backend calls; route reads 3.302 µs rpm vs 1.294 direct, capture 7.816 vs 1.798 (baseline 0.03 subtracted), 0.0040 queries per route call, identity equal |
| `run_gz_buffer.py` | PASS (bottle Steam, a new record `verification/results/gz-buffer-summary.json` next to the X3 one): 735,871 checks, 0 failures, 20 cases, 58 `gz_buffer_file` lines; 10 M × 3-byte reads: raw 37.8 ns, inside the hook envelope 284.4 ns (7.5×; 38.5× on FEX), buffered 28.5 ns — under Rosetta the buffer is 1.33× faster than the raw DLL, on FEX 0.78× (the FEX record stands as the X3 number) |
| `run_sampling_profiler.py` | attempt 1 **FAIL 17/22** (`a_leaf_attribution`, `a_caller_pair`, `c_leaf_attribution`, `c_scan_caller_pair`, `a_samples_present`: 0 of 1,092 thread-A/C samples in the spin functions, every leaf `ntdll`, i.e. the creation-time-context pattern of `bottles.md` limitation 1 — on the Steam bottle, 8 s after the gz fixture; mechanics 1,092 ticks / 2,912 samples / 0 dropped, walls 3.02 / 3.02 s); **rerun PASS 22/22** under the lock four minutes later (A 1,089 / 1,089, C 1,088 / 1,089, tick mean 268.6 µs, walls 3.02 / 3.02 s). The tracked record is the rerun; attempt 1's summary and report are kept in the review scratchpad, not tracked. `sampling_profiler.cpp` is not part of this tree's diff; observation 11 |
| `generate_rigid_motion_pixel.py --check` | PASS: seven programs recompiled and equal to the checked-in artifacts (`current_depth` 35 words `d097c156…`, `hdr_meter_level0` 1929 `4a59a0d1…`, the rest as review 24) |
| `check_no_x87.py build/d3d9.dll` | PASS: 11 roots (7 light hooks + `gz_read`/`gz_getc`/`gz_tell`/`gz_seek`), 144 reachable functions, 0 violations — on the pre-chain build `4c539fe6…` and, in the chain after the motion-output rebuild, on the final `38562f3a…` |
| `unittest discover -s verification/analysis` | 714 tests OK (33.5 s), `test_gz_buffer.py` re-parsing both recorded gz summaries included |

11. **Observation (open, outside this diff)** - the sampler's thread-context
    failure mode is not FEX-only: the first Steam attempt above delivered the
    creation-time context for every sample of the two spinning threads, the
    rerun the live one. Intermittent under x86_64 Wine, deterministic under
    FEX (`bottles.md`); the profiler's leaf attribution should be treated as
    valid only when the run's `a_*`/`c_*` fractions say so.

Final `build/d3d9.dll` SHA-256: 38562f3a7e2bbb03c6ffd1e746540b062dbf1ec9d184163407d771d5cbfbc1f8 (the `--clean-first` relink by `run_motion_output.py` from the same sources as the pre-chain `4c539fe696dcf3cadba1a6d1931d1d964035f35a21e352e56a521064576634c2`; seam fixture DLL `71796089…`, ownership DLL `d1be9fd1…`; not installed).

## What `docs/status.md` must say (orchestrator)

Items 1 (adjacency), 2 (engine reads), 4 (bottle switch) and 5 (gz buffer)
of the handoff are complete and reviewed here with findings 1-3 fixed; the
suite counts above; the DLL hash; nothing installed. Open acceptance items
for the next user runs on X3: `--telemetry --mesh-adjacency verify`
(`verify_mismatched=0`), then `fast`; a load with `--gz-buffer` (finding 6's
log lines); the `ZwReadVirtualMemory` line at the observer gate with
`X3M_TELEMETRY=1`. The X3-bottle records and the two FEX limitations
(sampler context, non-finite float formatting) from `bottles.md`; the
profiler stays validated on Steam only. The `.gitignore` and `bottle.py`,
`wine_lock.py`, `gz_buffer.*`, `savegame-gz-stream.md`, `gz-buffer.md`,
`test_gz_buffer.py`, `run_gz_buffer.py`, `gz_buffer_fixture.cpp`,
`build_gz_buffer.sh` and the `bottle-X3/` records are untracked and must be
added in the checkpoint.

Verdict: go for items 1, 2, 4 and 5 as they stand in the tree with
findings 1-3 applied (the in-place slot reset, the digits-only chunk size,
the empty bottle name), `X3M_GZ_BUFFER` and `X3M_MESH_ADJACENCY` off by
default, `engine_memory` direct reads the default with `rpm` as the A/B
switch. What the evidence supports: the buffer reproduces zlib 1.2.3's
observable behaviour on every hooked import against the game's own DLL over
735,871 comparisons on both bottles, costs nothing per call beyond the copy,
and patches only the six gz rows when telemetry is off; the module's
adjacency equals D3DX byte for byte on every computable fixture case and the
services restore the caller's FP state on every path; the direct reads are
XMM/x87-free in the object, return identical records to `ReadProcessMemory`
and cost a third to a tenth of it in the fixtures; the runners select
bottles without changing a Steam expectation. What it does not support: any
gameplay claim - the buffer-only gate path in the game (finding 6), the
near-tie normal selection on real meshes (finding 5), the `VirtualQuery`
count and the residual decommit window in a real frame, and the arena's
retention on the game's loader threads (finding 4) are the next user-run
items; the profiler's Steam attribution is intermittent (observation 11).
