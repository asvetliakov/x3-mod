# Review 27: loading trampolines, light hooks, resource reader (paused 3: triage of the two X3 failures in progress)

Independent review of the loading branch (`a752167` "Loading: light no-SSE
hooks, probe batch 2 trampolines, gated fast resource reader") on top of main
`28a003d`, merged as `20f7ffd` "Merge main into loading branch (pre-review
27)". Scope: `engine_patch.{h,cpp}` (the trampoline arena and call-site
redirects), `loading_probes.{h,cpp}` (12 byte-verified entry patches with the
return-address hijack), `loading_trace_light.{h,cpp}` (the no-SSE hook rows
and the probe handlers), `resource_reader*.{h,cpp}` (gated fast/verify
resource reader and the catalogue handle pool), the `loading_trace`,
`capture`, `loader`, CMake and `manage.py` wiring, `run_resource_reader.py`
and its fixture, `check_no_x87.py`, the analysis scripts and their tests, and
the five docs. No game was launched. The review ran in two sittings (paused
once for an account switch after the merge and the first read; resumed and
closed on the same day).

## Merge notes

- `git merge main` conflicted only in five regenerated fixture result files
  (`verification/results/loading-mesh-fixture.txt`,
  `loading-trace-fixture.txt`, `loading-trace-mesh-summary.json`,
  `mesh-adjacency-cache-{on,off}-fixture.txt`); main's copies were taken and
  the suite chain below regenerated them.
- The branch commit `a752167` deleted `verification/probe/wine_lock.py`
  (79 lines) although the branch's own docs and `docs/status.md` still refer
  to it and AGENTS.md makes it mandatory. Restored from main (finding 1).
- Clean rebuild before any fix (`cmake -S . -B build
  -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo;
  cmake --build build -j4`): 0 warnings, `build/d3d9.dll` SHA-256
  `8a08a0c798d911516dae0e239e8c9371d3754e24fed1b06eecd9b12e1dcd5d3f`
  (11,005,939 bytes). The final build is recorded under "Suite results".

## Checklist

### 1. Trampolines (`engine_patch`) — closed

- Read `src/proxy/engine_patch.{h,cpp}` and `loading_probes.cpp` in full.
  Mechanism: `claim()` reads `length` bytes, compares against the spec (fail
  closed with `bytes_mismatch`/`unreadable`/`invalid_spec`), emits the tail
  (displaced bytes + `jmp back`), a 4-aligned `entry` word and a `jmp [entry]`
  dispatcher into an 8 KB `PAGE_EXECUTE_READ` arena (RWX only while an
  `Emitter` holds the SRW lock), then writes `jmp dispatcher` over the first
  five bytes under `VirtualProtect` and restores the old protection;
  flush/protect failure rolls the bytes back. `restore()` checks the five
  patched bytes are still ours before writing the originals back and leaves
  the arena callable. `push_front()` swaps the chain head with a single
  aligned 4-byte store; the entry stub writes its continuation before the
  head is swapped (ordered installation).
- Byte verification of the twelve `expected` strings against the installed
  `X3AP.exe` (read only, PE section table) and the Ghidra listings: all
  equal; instruction boundaries clean; no relative or EIP-relative
  instruction displaced; `ret` forms match `ret_pop` (`resource_open`
  `ret 4`, `name_resolve` `ret 0xc`, `read_dispatch` `ret 4`, the rest plain
  `ret`); the five CRT callees and the two pool call sites match. Full table
  in [loading-probes.md](loading-probes.md) "Byte verification".
- Overlap and ordering: the scene hook (`0x004721b1`) and the object-trace
  call-site hook (`0x004c5228`) are `E8` calls in other functions, more than
  16 bytes from every site; scene hook and object trace install from
  `load_backend` before `initialize_log`, the probes and the reader inside
  `initialize_log`, all before the first `Present` (the install window of
  finding 2).
- Shadow-stack hijack (`x3m_probe_enter`/`x3m_probe_exit`): frame is
  `pushfd; pushad`, so `regs[9]` is the return-address slot; the exit stub
  reserves a slot (`sub esp,4`) so `&regs[10]` is ESP after the callee's
  `ret n` and the matching slot is `here-4-ret_pop`. Stale entries (frames
  unwound past by exception/longjmp) have lower slot addresses and are
  discarded from the top while `depth>1`; a popped entry's slot mismatch is
  counted as `desync`. Callee EAX/EDX and flags survive via `popad`/`popfd`
  (`mov [esp+0x24],eax` targets the reserved slot). Per-thread `Shadow` (64
  entries, 1.5 KB) is `HeapAlloc`ed lazily under a CAS-guarded `TlsAlloc`.
  Overflow at depth 64 leaves the frame un-hijacked (counted, still called).
  The fixture's longjmp case (`probe_desync_recovered`: `calls=4 exits=2
  desync=2`, then `probe_clean_after_recovery`) and the two-thread case (1,600
  entries/exits, `desync=0 overflow=0`) pass on the X3 (arm64 Wine + FEX)
  bottle in the chain below, which is the CPU-state contract check available
  without the game: the stubs save/restore EFLAGS and the GPRs, the handlers
  are in the no-SSE unit, `check_no_x87.py` PASS.

### 2. Light hooks TU — closed

- Integer-only: 64-bit counters through `lock cmpxchg8b` (`exchange64` /
  `load64` / `add64` correct; `max64` is a documented racy read-then-exchange);
  `Span` accounting is plain 64-bit integer add/sub/compare; the widening
  multiply `uint64_t(regs[6])*regs[7]` is integer. No logging, no double.
  `TlsAlloc` once in `initialize()`. `check_no_x87.py build/d3d9.dll` on the
  final build: PASS, 53 roots, 195 reachable functions, 0 violations (the two
  new light entry points `probe_read32`/`shadow_blocks` are reached from the
  probe roots). The `frame_end elapsed_ms/dt_ms/qpc` addition is one QPC per
  logged line with integer arithmetic, in every mode.
- Analysis: `analyze_iteration08_loading.py` parses `loading_probe_site` as a
  key/value dictionary (`f.get`), so the new `atomic_write=` field is
  ignored by the existing tests; 726 unit tests OK.

### 3. Resource reader and handle pool — closed

- `decode()` follows the decompiled contract (state checks, whole-extent
  `fread`, word XOR, in-memory header walk with the FNAME/FCOMMENT overrun
  guard, `isize` from the trailer, single inflate, `Z_STREAM_END` and
  `produced == isize` both required, exact bookkeeping on success, stream and
  cursor restored on every fallback path through `RestoreGuard`). Verify mode
  restores the stream before calling the original through the chain
  (`call *%1` with EAX in/out and ECX/EDX clobbered — the contract of
  `0x004e8880`) and compares bytes, both size globals, the four counter
  deltas and the cursor; the caller always receives the original's buffer.
  Fast mode returns through the stub's `popad; popfd; ret`, the plain `ret`
  of the site.
- Pool: `x3m_pool_fopen`/`x3m_pool_fclose` never share a kept handle between
  two open objects (state 1 while in use), close on `_IOERR`, bounded (32
  entries, 128-byte paths), drain at shutdown; the `_fopen` in the catalogue
  branch and the single `_fclose` of `0x004e9360` are the only redirected
  call sites, byte-verified (`E8` + displacement to the expected callee).
- Finding 6 (fixed): `statistics()`/`pool_statistics()` snapshot race and the
  `held` decrement outside the lock.

### 4. Gates and defaults — closed

- `tools/manage.py launch --dry-run --direct` (no Wine): `X3M_TELEMETRY=0`,
  `X3M_LOADING_PROBES=0`, `X3M_RESOURCE_READ=native`, `X3M_DAT_HANDLES=0`,
  `X3M_GZ_BUFFER=0`, `X3M_SCENE_HOOK=0`, `X3M_MOTION_OUTPUT=0`,
  `X3M_PROFILE=0`. With that environment `loading_trace::initialize` is not
  called (telemetry off, gz buffer off), so `loading_probes::initialize`
  never runs, `resource_reader::initialize` returns `disabled` before any
  verification, and nothing is patched; `frame_end … elapsed_ms= dt_ms=
  qpc=` is unconditional in `Present`. `--loading-probes` requires
  `--telemetry` (parser error otherwise); the reader and the pool require the
  exact executable, the site bytes, the five callee prologues and the zlib
  exports, each failing closed with a `status=` in the log.

### 5. Performance pass — closed

- Light rows: two QPC reads plus one for the wrapper tail, two TLS calls, ~10
  `lock cmpxchg8b` per call (envelope 354 ns on FEX per the gz-buffer fixture,
  3.4× cheaper than `CpuCallBoundary`). Probes: one QPC, one TLS read, the
  shadow push/pop and the `add64`s per timed call; `note_caller` is a linear
  scan of 8 slots for `find_wrapper` only.
- New in this review: the file-object dereference costs one `GetTickCount`
  (a shared-page read) and a 16-entry scan per classified call, plus one
  `VirtualQuery` per distinct page per 100 ms — for ≈ 1,400 `resource_open` /
  `resource_read` calls per load that is a few dozen queries. The reader's
  `statistics()` moved from 30 `exchange64` to 30 `load64` (same
  instruction). The atomic patch write is install-time only.
- Nothing per draw changed; `record_path` stays bounded at 16 records.

### 6. Docs — closed

- `docs/reverse-engineering/loading-probes.md`: "Install window and the write
  itself", "Register dereferences in the handlers", new "Shutdown" section.
- `docs/verification/loading-probes.md`: "Byte verification of the twelve
  sites", the new log fields.
- `docs/verification/resource-reader.md`: "Review 27 notes".
- The stale claim in the old `loading_probes.cpp` header comment ("bytes
  verified … on 2026-09-12") is now backed by the table above.

## Findings and fixes

1. **Fixed (blocking).** `a752167` removed `verification/probe/wine_lock.py`;
   every runner invocation in AGENTS.md, `docs/status.md` and the branch's
   own docs requires it. Restored from main unchanged.
2. **Fixed (medium, design).** `claim()` wrote the five-byte `jmp` with
   `memcpy` over live code. Now: an install window — every production claim
   runs on the backend-load path before the device exists; `capture` closes
   the window at the first `Present` (`engine_patch::close_install_window`)
   and later `claim()`/`claim_call()` fail with `late_claim`, logged with the
   closing reason by `loading_probes::initialize` and visible in the
   `resource_reader`/`dat_handle_pool` status fields. Defence in depth:
   `engine_patch::write_code` writes the five bytes with one `lock cmpxchg8b`
   when they lie inside an aligned qword (11 of 12 sites and the `_fclose`
   call site; `crt_fgetc` and the `_fopen` call site straddle a boundary and
   take the plain copy), for the patch, the rollback and the restore.
   Documented in `loading-probes.md` "Patch mechanics".
3. **Fixed (low).** `rollback_failed` left the site patched with
   `patched_in=false`, so `restore()` skipped it. Both `claim()` and
   `claim_call()` now keep `patched_in=true` on that path; `restore()` writes
   the five patched bytes back (the displaced remainder was never changed).
4. **Documented (low), intentionally retained.** The per-thread `Shadow`
   blocks and the two TLS slots are never freed: a frame hijacked before
   `loading_probes::shutdown()` restored the sites returns into the exit stub
   later and reads its shadow through the TLS slot, so freeing either (or
   reusing the TLS index) would be a use-after-free on a late return, exactly
   the reason the arena is retained. `shutdown()` now also disables further
   hijacks (`probe_set_exit_stub(nullptr)`) and logs
   `retained_shadow_blocks= retained_bytes=`; the OS reclaims them at process
   exit, the only time the DLL unloads in production
   (`loading-probes.md` "Shutdown").
5. **Fixed (low).** The `ResourceOpen`/`ResourceRead`/`ReadDispatch`
   handlers dereferenced `ctx+4` after `plausible()` alone. Now
   `light::probe_read32` validates the page with `VirtualQuery` (committed,
   readable, not guard) through a 16-entry page cache with a 100 ms TTL
   (mirroring `engine_memory.cpp`'s no-frame bound) before the read; no SEH,
   no `IsBadReadPtr`. Kept self-contained in the light unit so the gz-buffer,
   loading-trace and resource-reader fixtures link unchanged.
6. **Fixed (low, new).** `resource_reader_core.cpp` `statistics()` and
   `pool_statistics()` snapshotted every counter with
   `exchange64(p, *p)` — a plain read followed by an exchange, which rewrites
   a stale value over a concurrent `add64` from the loader thread. Replaced
   with `load64`. `x3m_pool_fopen` decremented `held` after releasing the pool
   lock; moved inside, and `pool_statistics()` reads it under the lock.
7. **Noted, no change.** `x3m_resource_read_entry` dereferences EAX without a
   plausibility check: the site has exactly two callers, both passing the heap
   file object, and the replacement dereferences the same object the original
   body would; a guard would only protect a caller the original also crashes
   on (`resource-reader.md` "Review 27 notes").

## Suite results

Chain run on the final sources (`build/d3d9.dll` rebuilt clean at the start,
SHA-256 `87d6588c4cada65f2214bad5859205934c2c44fe71b42dddcccee8f0756097eb`,
11,015,608 bytes; the runners that rebuild the DLL themselves (`fresh_build`)
left `38afa8c6c975cb3a3a1e64c432af81f43528324138ab0e481274010aad49d001` in
`build/` at the end of the chain — same sources, MinGW PE timestamp). Every
Wine command ran as `python3 verification/probe/wine_lock.py --holder review27 …`,
one at a time, sharing the lock with two other agents' chains. Steam bottle
unless noted.

| Suite | Result |
| --- | --- |
| `run_motion_output.py` | PASS (`{"passed": true}`; seam and production cases all `exit=0`, e.g. `seam-taa-lazy-on` 164 checks) |
| `run_temporal_pass.py` | PASS: 386 samples, 228 state restorations, 2 device generations |
| `temporal_run.py` | PASS first attempt: 78/78 sample checks, reset passed, 2 device generations, sources/executable unchanged |
| `run_ownership_integration.py` | PASS: 26 cases, all `exit=0` |
| `run_scene_capture.py` | PASS: 4,908 checks, no failed checks |
| `run_loading_trace.py` | PASS (`phase=complete`, native D3DX hash unchanged before/after) |
| `run_gz_buffer.py` (`X3M_FIXTURE_BOTTLE=X3`) | PASS: 735,876 checks, 0 failures, 10 M timing calls |
| `run_resource_reader.py` (X3) | PASS: 405 checks, 0 failures (`quick=false`; includes the probe cases: `probe_b_garbage_esi_unclassified` with the new page guard, longjmp `desync=2` recovery, two threads) |
| `run_object_lifetime.py` | **FAIL: 574 checks, 10 failures** — TODO triage (below) |
| `run_object_trace.py` | PASS: 166 checks, 0 failures, 120,017 backend calls |
| `run_mesh_adjacency_cache.py` | **FAIL** (`exit_code=1`, the fixture itself reports `cache=1 checks=2219 failures=0`; the runner's own expectation "Persistent acquisition unlock failure has distinct origin, permanently disables cache and does not call native" failed with `error=native`) — TODO triage (below) |
| `run_mesh_cache_hook.py` | PASS (`phase=complete`, admission 0) |
| `generate_rigid_motion_pixel.py --check` | PASS (all programs recompiled equal) |
| `check_no_x87.py build/d3d9.dll` | PASS: 53 roots, 195 reachable functions, 0 violations |
| `unittest discover -s verification/analysis` (`PYTHONPATH=verification/probe`) | 726 tests OK (30.5 s) |

**Triage state (paused 3, orchestrator's account switch).** The two
failures were not Steam runs: the chain script's `X3M_FIXTURE_BOTTLE=X3 run …`
prefix on a shell *function* persisted (POSIX assignment-before-function
semantics), so every step after `run_gz_buffer.py` — `object_lifetime`,
`object_trace`, `mesh_adjacency_cache`, `mesh_cache_hook`, the generator — ran
in the **X3** bottle (arm64 Wine + FEX), and their records went to
`verification/results/bottle-X3/` (committed with the paused-2 checkpoint).
The table's bottle column for those rows is therefore X3, not Steam;
`verification/probe/bottle.py` `DEFAULT_BOTTLE` is still `Steam` (verified).

* `run_object_lifetime.py` on X3: the 10 failures are `input/output
  x87/SSE/MXCSR matches original` and `all boundaries input/output FX state
  preserved` (4 boundaries × 2) — FX-state fidelity under FEX
  (`FEX_X87REDUCEDPRECISION=1`); every functional check passes.
* `run_mesh_adjacency_cache.py` on X3: `RESULT FAIL checks=303 error=native
  FP status changes observed` — the same class (x87 status word through the
  native D3DX call under FEX).
* Neither suite is in the X3 validation record (`docs/verification/bottles.md`
  validated five suites on X3; these two were never run there), so nothing
  says they should pass on X3.
* Plain main `b10d129` in the scratch worktree `/tmp/x3-b10d129` (left in
  place; `cmake` of its `d3d9.dll` fails there — `loading_trace.h:61
  ID3DXMesh not declared` from `capture.cpp`, main's own WIP state, the
  runners build their fixtures independently): `run_object_lifetime.py`
  **Steam PASS 574/0**, `run_mesh_adjacency_cache.py` **Steam PASS
  (checks=767)**; the X3 reruns on `b10d129` were queued but not started
  before the pause.

Remaining before the closing commit: (a) rerun the four suites above in
this worktree on Steam (`run_object_lifetime.py`, `run_object_trace.py`,
`run_mesh_adjacency_cache.py`, `run_mesh_cache_hook.py`, plain invocation,
no bottle prefix) and record them; (b) optionally the two X3 runs on
`b10d129` to show the X3 failures are identical on main; (c) rewrite the
suite table with the correct bottle per row, drop the paused wording, record
the final `build/d3d9.dll` hash; (d) `git worktree remove /tmp/x3-b10d129`.

## What `docs/status.md` must say (orchestrator)

Review 27 code and docs complete on the branch: findings 1–3, 5, 6 fixed, 4
documented, 7 noted; the twelve trampoline sites byte-verified against the
installed executable and the Ghidra listings; 13 of 15 chain entries green on
the final build, `run_object_lifetime.py` and `run_mesh_adjacency_cache.py`
failing in units this branch does not touch (TODO triage against main's
`b10d129` WIP before merging). Do not merge or install until that triage is
done; then the branch is ready, and the next game
run is the one `docs/verification/loading-probes.md` and
`resource-reader.md` describe (`--telemetry --loading-probes`, then
`--resource-read verify --dat-handles`), and `loading_probe_site …
status=late_claim` in any log would mean an install order regression.
